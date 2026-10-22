/*
XBOX_FILES.C

Win32 file API for the Linux build, over POSIX descriptors.

Xbox paths are translated below two roots. d:\ is the data root, the
directory holding the game's maps/ folder: paths.data (port_config.c), else the current
directory when it has maps/, else assets/ in the current directory or two
levels above the executable (the repository root for build/linux/halo).
Every other drive letter X:\ is the subdirectory X/ of the save root (z:\ holds the persistent cache and saves,
u:\ user data, t:\ title data): paths.saves, else the game's own folder
(save_folder.c): $XDG_DATA_HOME/chupathingyce or ~/.local/share/chupathingyce,
%APPDATA%/ChupathingyCE on Windows. Path components are
matched case-insensitively, like the Xbox's FATX volumes.

The game's own folders are beside them (port_folders.h): data/ and logs/ in
the data root, cache/ and private/ in the save root (platform_folder). The
game's log, d:\debug.txt (and debug.old.txt), is in logs/.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "port_folders.h"
#include "save_folder.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HALO_WINDOW_REBASE
#include <strings.h>
#endif
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/* ---------- paths */

static BOOL directory_exists(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0;
}

/* whether directory has a maps folder, in any case */
static BOOL has_maps(const char *directory)
{
	char on_disk[256];

	return directory_exists(directory) &&
		posix_find_entry_case_insensitive(directory, "maps", on_disk, sizeof(on_disk));
}

#ifdef HALO_64BIT
char platform_log_path[MAX_PATH];
#endif

static void trim_separators(char *path)
{
	while (strlen(path) > 1 && path[strlen(path) - 1] == '/')
		path[strlen(path) - 1] = '\0';
}

const char *platform_data_root(void)
{
	static char root[MAX_PATH];

	if (!root[0])
	{
		const char *environment = config_string("paths.data");

		if (*environment)
		{
			snprintf(root, sizeof(root), "%s", environment);
		}
		else if (has_maps("."))
		{
			snprintf(root, sizeof(root), ".");
		}
		else
		{
			char executable[MAX_PATH];
			char executable_directory[MAX_PATH] = "";
#ifdef __APPLE__
			uint32_t ex_size = sizeof(executable);
			ssize_t length = _NSGetExecutablePath(executable, &ex_size) == 0 ? (ssize_t)strlen(executable) : -1;
#else
			ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
#endif

			if (length > 0)
			{
				char *slash;

				executable[length] = '\0';
				snprintf(executable_directory, sizeof(executable_directory), "%s", executable);
				slash = strrchr(executable_directory, '/');
				if (slash)
					*slash = '\0';
			}
			snprintf(root, sizeof(root), ".");
			if (executable_directory[0] && has_maps(executable_directory))
			{
				/* next to the executable, where the desktop ports' first start
				extracts it (platform_offer_game_data) */
				snprintf(root, sizeof(root), "%s", executable_directory);
			}
			else if (has_maps("assets"))
			{
				snprintf(root, sizeof(root), "assets");
			}
			else if (length > 0)
			{
				/* <repository>/build/linux/halo -> <repository>/assets */
				char *slash;
				int level;

				executable[length] = '\0';
#ifdef __APPLE__
				/* ... and <repository>/build/macos/ChupathingyCE.app/Contents/MacOS/halo:
				the nearest assets folder up to five levels up */
				for (level = 0; level < 6 && (slash = strrchr(executable, '/')); level++)
				{
					char test_assets[MAX_PATH];

					*slash = '\0';
					snprintf(test_assets, sizeof(test_assets), "%s/assets", executable);
					if (has_maps(test_assets))
					{
						snprintf(root, sizeof(root), "%s", test_assets);
						break;
					}
				}
#else
				for (level = 0; level < 3 && (slash = strrchr(executable, '/')); level++)
					*slash = '\0';
				if (level == 3 && strlen(executable) + sizeof("/assets") <= sizeof(executable))
				{
					strcat(executable, "/assets");
					if (has_maps(executable))
						snprintf(root, sizeof(root), "%s", executable);
				}
#endif
			}
#ifndef HALO_ANDROID
			{
				/* (the macOS application's data goes in its folder in
				Application Support, not in the application, and an
				AppImage's in the save root's place: port_config.c) */
				char app_folder[MAX_PATH];
				const char *destination = platform_app_folder(app_folder, sizeof(app_folder))
					? app_folder : executable_directory;

				if (!has_maps(root) && destination[0] && has_maps(destination))
					snprintf(root, sizeof(root), "%s", destination);
				else if (!has_maps(root) && destination[0] && platform_offer_game_data(destination) &&
					has_maps(destination))
				{
					snprintf(root, sizeof(root), "%s", destination);
				}
				/* (with no maps anywhere, the log still goes where it can
				be written) */
				else if (!has_maps(root) && destination == app_folder)
					snprintf(root, sizeof(root), "%s", destination);
			}
#endif
			if (!has_maps(root))
				platform_log("no maps/ folder found; set paths.data in config.toml to the folder that holds maps/");
		}
		trim_separators(root);
		/* (the logs folder first, the log's place: "logs folder: ...", the
		game's log, debug.txt, in it) */
		platform_folder(_port_folder_logs);
		platform_log("data root: %s", root);
	}
	return root;
}

/* creates every missing directory along path */
static void make_directories(const char *path)
{
	char partial[MAX_PATH];
	unsigned long index;

	snprintf(partial, sizeof(partial), "%s", path);
	for (index = 1; partial[index]; index++)
	{
		/* (Windows' separators too, but not a drive's: C:\) */
		if ((partial[index] == '/' || partial[index] == '\\') && partial[index - 1] != ':')
		{
			char separator = partial[index];

			partial[index] = '\0';
			if (!directory_exists(partial))
				posix_make_directory(partial);
			partial[index] = separator;
		}
	}
	if (!directory_exists(partial))
		posix_make_directory(partial);
}

/* ---------- the game's own folders */

const char *platform_folder(enum port_folder folder)
{
	static char paths[k_port_folder_count][MAX_PATH];
	char path[MAX_PATH];
	const char *override = "";

	if ((int)folder < 0 || (int)folder >= k_port_folder_count)
		return ".";
	if (paths[folder][0])
		return paths[folder];
	if (folder == _port_folder_logs)
		override = config_string("paths.logs");
	else if (folder == _port_folder_cache)
		override = config_string("paths.cache");
	if (!port_folder_path(folder, override, platform_data_root(),
			port_folder_in_save_root(folder) ? platform_save_root() : "", path, sizeof(path)))
	{
		snprintf(path, sizeof(path), "%s", port_folder_name(folder));
	}
	make_directories(path);
#ifdef HALO_64BIT
	/* the platform layer's log goes to the game's (xbox_kernel.c) */
	if (folder == _port_folder_logs)
		snprintf(platform_log_path, sizeof(platform_log_path), "%s/debug.txt", path);
#endif
	/* (the first caller's: they are the same) */
	if (!paths[folder][0])
	{
		snprintf(paths[folder], sizeof(paths[folder]), "%s", path);
		platform_log("%s folder: %s", port_folder_name(folder), path);
	}
	return paths[folder];
}

/* the executable's folder, without its separator; 0 when it is not known */
static int executable_folder(char *path, unsigned long size)
{
	char executable[MAX_PATH];
	char *slash;
#ifdef __APPLE__
	uint32_t executable_size = sizeof(executable);
	ssize_t length = _NSGetExecutablePath(executable, &executable_size) == 0 ? (ssize_t)strlen(executable) : -1;
#else
	ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
#endif

	if (length <= 0)
		return 0;
	executable[length] = '\0';
	slash = strrchr(executable, '/');
#ifdef _WIN32
	if (!slash || (strrchr(executable, '\\') && strrchr(executable, '\\') > slash))
		slash = strrchr(executable, '\\');
#endif
	if (!slash)
		return 0;
	*slash = '\0';
	snprintf(path, (size_t)size, "%s", executable);
	return 1;
}

int platform_logs_folder_early(char *path, unsigned long size)
{
	const char *logs = config_string("paths.logs");
	const char *data = config_string("paths.data");
	char folder[MAX_PATH];

	if (*logs)
		snprintf(path, (size_t)size, "%s", logs);
	/* (where platform_data_root looks, without asking the player anything) */
	else if (*data)
		snprintf(path, (size_t)size, "%s/logs", data);
	else if (has_maps("."))
		snprintf(path, (size_t)size, "logs");
	else if (executable_folder(folder, sizeof(folder)) && has_maps(folder))
		snprintf(path, (size_t)size, "%s/logs", folder);
	else if (has_maps("assets"))
		snprintf(path, (size_t)size, "assets/logs");
#ifndef HALO_ANDROID
	else if (platform_app_folder(folder, sizeof(folder)))
		snprintf(path, (size_t)size, "%s/logs", folder);
#endif
	/* (where the first start puts the maps: platform_offer_game_data) */
	else if (executable_folder(folder, sizeof(folder)))
		snprintf(path, (size_t)size, "%s/logs", folder);
	else
		return 0;
	trim_separators(path);
	make_directories(path);
	return 1;
}

#ifdef __APPLE__
/* whether this Mac has saves in the folder the game used before the
application had its own (~/.local/share/halo-linux) */
static BOOL saves_exist(const char *home)
{
	char path[MAX_PATH];

	snprintf(path, sizeof(path), "%s/.local/share/halo-linux", home);
	return directory_exists(path);
}
#endif

#ifndef __APPLE__
/* once, on the first start with the game's own save root: the saves of the
folder it shared with other builds, copied into it (never moved: they still
use it) */
static void copy_shared_saves(const char *shared, const char *root)
{
	unsigned long files = 0;

	switch (save_folder_copy_once(shared, root, &files))
	{
	case _save_folder_copy_copied:
		platform_log("copied %lu files of saves (profiles, saved games, game types) from %s, "
			"the save folder shared with other builds; it is left as it was", files, shared);
		break;
	case _save_folder_copy_failed:
		platform_log("could not copy the saves from %s; the next start tries again", shared);
		break;
	default:
		break;
	}
}
#endif

const char *platform_save_root(void)
{
	static char root[MAX_PATH];

	if (!root[0])
	{
		const char *environment = config_string("paths.saves");
		const char *data_home = getenv("XDG_DATA_HOME");
		const char *home = getenv("HOME");
#ifdef __APPLE__
		if (*environment)
			snprintf(root, sizeof(root), "%s", environment);
		/* the macOS application keeps everything in its folder in
		Application Support (platform_app_folder), unless this Mac has saves
		from before (and its player key: browser.c), in the folder below */
		else if (platform_app_folder(root, sizeof(root)) &&
			!(home && *home && saves_exist(home)))
		{
			strncat(root, "/saves", sizeof(root) - strlen(root) - 1);
		}
		else if (data_home && *data_home)
			snprintf(root, sizeof(root), "%s/halo-linux", data_home);
		else if (home && *home)
			snprintf(root, sizeof(root), "%s/.local/share/halo-linux", home);
#else
		char shared[MAX_PATH];
#ifdef _WIN32
		const enum save_folder_system system = _save_folder_windows;
#else
		const enum save_folder_system system = _save_folder_linux;
#endif

		/* paths.saves (or HALO_SAVE_ROOT) as it is, else the game's own
		folder (save_folder.c), with the saves copied from the folder it
		shared with other builds before, once */
		if (save_folder_choose(system, environment, getenv("APPDATA"), data_home, home,
				root, sizeof(root), shared, sizeof(shared)))
		{
			if (shared[0])
				copy_shared_saves(shared, root);
		}
#endif
		else
			snprintf(root, sizeof(root), "%s", platform_data_root());
		trim_separators(root);
		make_directories(root);
		platform_log("save root: %s", root);
	}
	return root;
}

/* whether an Xbox path is the game's log, d:\debug.txt or d:\debug.old.txt
(errors.c), which is in logs/: its name, or NULL */
static const char *log_file_name(const char *xbox_path)
{
	static const char *const names[] = { "debug.txt", "debug.old.txt" };
	const char *name = xbox_path;
	unsigned long index;

	if ((name[0] != 'd' && name[0] != 'D') || name[1] != ':')
		return NULL;
	name += 2;
	while (*name == '\\' || *name == '/')
		name++;
	for (index = 0; index < sizeof(names) / sizeof(*names); index++)
	{
		const char *a = name, *b = names[index];

		while (*a && tolower((unsigned char)*a) == *b)
		{
			a++;
			b++;
		}
		if (!*a && !*b)
			return names[index];
	}
	return NULL;
}
#ifdef HALO_WINDOW_REBASE
/* A saved game is the game state written out raw, pointers and all
(saved games/game_state_xbox.c): z:\savegame.bin, the checkpoint the game
reverts to, and savegame.bin in the player's profile folder on u:\, the one
"save and quit" leaves for the next run. Its pointers are into the window
where it was written, and its header does not say where that was, so read
with the window elsewhere it points at memory the process is not using.
With the window moved (cseries/xbox_address.h) the file is named for the
window's address, savegame-20000000.bin: a run with the window elsewhere
does not see it and starts the level over, and a later run at that address
finds it again. Nothing else the game keeps holds a pointer, and stays where
it is. With the window at its linked address the name is the game's own.
Returns xbox_path, or the renamed path in buffer. */
static const char *window_save_path(const char *xbox_path, char *buffer, unsigned long buffer_size)
{
	const char *name = xbox_path;
	const char *cursor;

	if (PLATFORM_CONTIGUOUS_BASE == 0x80000000UL)
		return xbox_path;
	for (cursor = xbox_path; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/' || *cursor == ':')
			name = cursor + 1;
	}
	if (strcasecmp(name, "savegame.bin"))
		return xbox_path;
	snprintf(buffer, buffer_size, "%.*ssavegame-%08lx.bin", (int)(name - xbox_path), xbox_path,
		(unsigned long)PLATFORM_CONTIGUOUS_BASE);
	return buffer;
}
#endif

void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size)
{
	char resolved[1024];
#ifdef HALO_WINDOW_REBASE
	char renamed[1024];
	const char *cursor = window_save_path(xbox_path, renamed, sizeof(renamed));
#else
	const char *cursor = xbox_path;
#endif
	const char *log_name = log_file_name(xbox_path);
	unsigned long length;

	if (log_name)
	{
		snprintf(host_path, host_path_size, "%s/%s", platform_folder(_port_folder_logs), log_name);
		return;
	}
	snprintf(resolved, sizeof(resolved), "%s", platform_data_root());
	if (((cursor[0] >= 'a' && cursor[0] <= 'z') || (cursor[0] >= 'A' && cursor[0] <= 'Z')) && cursor[1] == ':')
	{
		char drive = (char)(cursor[0] | 0x20);

		if (drive != 'd')
		{
			struct posix_file_information information;

			snprintf(resolved, sizeof(resolved), "%s/%c", platform_save_root(), drive);
			/* every Xbox drive always exists; create its directory on first use */
			if (posix_stat(resolved, &information) != 0)
				posix_make_directory(resolved);
		}
		cursor += 2;
	}

	while (*cursor)
	{
		char component[256];
		char on_disk[256];
		unsigned long component_length = 0;

		while (*cursor == '\\' || *cursor == '/')
			cursor++;
		while (*cursor && *cursor != '\\' && *cursor != '/' && component_length + 1 < sizeof(component))
			component[component_length++] = *cursor++;
		component[component_length] = '\0';
		/* . and .. (and Windows' spellings of them, with trailing dots and
		spaces) are skipped: FATX names are never only dots, and .. would
		leave the root */
		if (!component_length || strspn(component, ". ") == component_length)
			continue;

		if (posix_find_entry_case_insensitive(resolved, component, on_disk, sizeof(on_disk)))
		{
			/* prefer an exact match when several spellings exist */
			char exact[1100];
			struct posix_file_information information;

			snprintf(exact, sizeof(exact), "%s/%s", resolved, component);
			if (posix_stat(exact, &information) == 0)
				snprintf(on_disk, sizeof(on_disk), "%s", component);
		}
		else
		{
			snprintf(on_disk, sizeof(on_disk), "%s", component);
		}
		length = strlen(resolved);
		snprintf(resolved + length, sizeof(resolved) - length, "/%s", on_disk);
	}
	snprintf(host_path, host_path_size, "%s", resolved);
}

/* ---------- the maps folder's maps */

enum
{
	/* a cache file's header: 'head' and its version */
	MAP_HEADER_SIGNATURE = 0x68656164,
	XBOX_MAP_VERSION = 5,
	/* (Halo PC's: retail's, HaloMD's maps' too, and Custom Edition's) */
	HALO_PC_MAP_VERSION = 7,
	CUSTOM_EDITION_MAP_VERSION = 609,
};

/* a map file's cache version, by its header; 0 if it is not a cache file */
static long map_file_version(const char *path)
{
	unsigned char header[8];
	FILE *file = fopen(path, "rb");
	long version = 0;

	if (!file)
		return 0;
	if (fread(header, 1, sizeof(header), file) == sizeof(header) &&
		(header[0] | header[1] << 8 | header[2] << 16 | (unsigned long)header[3] << 24) == MAP_HEADER_SIGNATURE)
	{
		version = (long)(header[4] | header[5] << 8 | header[6] << 16 | (unsigned long)header[7] << 24);
	}
	fclose(file);
	return version;
}

static BOOL map_version_is_pc(long version)
{
	return version == HALO_PC_MAP_VERSION || version == CUSTOM_EDITION_MAP_VERSION;
}

static BOOL name_ends_with(const char *name, const char *ending)
{
	size_t length = strlen(name);
	size_t ending_length = strlen(ending);
	size_t index;

	if (length < ending_length)
		return FALSE;
	for (index = 0; index < ending_length; index++)
	{
		if (tolower((unsigned char)name[length - ending_length + index]) != tolower((unsigned char)ending[index]))
			return FALSE;
	}
	return TRUE;
}

/* the Halo PC maps in a folder (an Xbox path): how many, and the first
few's names after names' (the maps named <name>@ce.map, <name>@md.map or
<name>@pc.map, which are played from maps/ as Halo PC's, left out if asked) */
static long pc_maps_in(const char *xbox_folder, BOOL skip_suffixed, char *names, unsigned long names_size)
{
	char folder[1024];
	char name[256];
	long count = 0;
	void *directory;

	platform_translate_path(xbox_folder, folder, sizeof(folder));
	directory = posix_directory_open(folder);
	if (!directory)
		return 0;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		char path[1300];

		if (!name_ends_with(name, ".map") ||
			(skip_suffixed && (name_ends_with(name, "@ce.map") || name_ends_with(name, "@md.map") ||
				name_ends_with(name, "@pc.map"))))
		{
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s", folder, name);
		if (!map_version_is_pc(map_file_version(path)))
			continue;
		if (names && count < PLATFORM_MAPS_FOLDER_NAMED)
		{
			size_t length = strlen(names);

			snprintf(names + length, names_size - length, "%s%s", length ? ", " : "", name);
		}
		count++;
	}
	posix_directory_close(directory);
	return count;
}

void platform_maps_folder_check(struct platform_maps_folder *maps)
{
	const char *root = platform_data_root();
	char path[1024];

	memset(maps, 0, sizeof(*maps));
	/* the data root as the player finds it (whole: Windows' is the
	executable's folder's, or paths.data's) */
#ifndef _WIN32
	if (root[0] != '/' && getcwd(maps->root, sizeof(maps->root)))
	{
		size_t length = strlen(maps->root);

		if (strcmp(root, "."))
			snprintf(maps->root + length, sizeof(maps->root) - length, "/%s", root);
	}
	else
#endif
	{
		snprintf(maps->root, sizeof(maps->root), "%s", root);
	}
	platform_translate_path("d:\\maps\\ui.map", path, sizeof(path));
	maps->ui_version = map_file_version(path);
	maps->stray_pc_maps = pc_maps_in("d:\\maps", TRUE, maps->stray_names, sizeof(maps->stray_names));
	maps->pc_maps_beside = pc_maps_in("d:\\maps_ce", FALSE, NULL, 0) + pc_maps_in("d:\\maps_md", FALSE, NULL, 0) +
		pc_maps_in("d:\\maps_pc", FALSE, NULL, 0) + pc_maps_in("d:\\maps\\ce", FALSE, NULL, 0) +
		pc_maps_in("d:\\md_maps", FALSE, NULL, 0) + pc_maps_in("d:\\custom_maps", FALSE, NULL, 0);
	if (maps->ui_version == XBOX_MAP_VERSION)
		maps->state = _maps_folder_xbox;
	else if (map_version_is_pc(maps->ui_version))
		maps->state = _maps_folder_halo_pc;
	else
		maps->state = _maps_folder_no_ui;
}

/* ---------- the older map folders */

static const struct
{
	const char *from;
	const char *to;
	/* the folder from is in, which must be no link either (or NULL: the
	data root) */
	const char *parent;
} old_map_folders[] =
{
	{ "d:\\maps\\ce", "d:\\maps_ce", "d:\\maps" },
	{ "d:\\md_maps", "d:\\maps_md", NULL },
};

/* the two folders' paths, and whether the old one can be moved to the new */
static BOOL old_map_folder_movable(int index, char *from, char *to, unsigned long size)
{
	struct posix_file_information information;
	char parent[1024];

	platform_translate_path(old_map_folders[index].from, from, size);
	platform_translate_path(old_map_folders[index].to, to, size);
	if (posix_stat(from, &information) != 0 || !(information.flags & _posix_file_is_directory))
		return FALSE;
	if (posix_stat(to, &information) == 0 || posix_is_link(to))
		return FALSE;
	if (posix_is_link(from))
		return FALSE;
	if (old_map_folders[index].parent)
	{
		platform_translate_path(old_map_folders[index].parent, parent, sizeof(parent));
		if (posix_is_link(parent))
			return FALSE;
	}
	return TRUE;
}

int platform_old_map_folders(char *description, unsigned long size)
{
	int count = 0;
	int index;

	if (size)
		description[0] = 0;
	for (index = 0; index < (int)(sizeof(old_map_folders) / sizeof(old_map_folders[0])); index++)
	{
		char from[1024], to[1024];

		if (!old_map_folder_movable(index, from, to, sizeof(from)))
			continue;
		if (size)
		{
			size_t length = strlen(description);

			/* (as the player sees them: maps/ce, maps_ce) */
			snprintf(description + length, size - length, "%s%s to %s", length ? "\n" : "",
				index == 0 ? "maps/ce" : "md_maps", index == 0 ? "maps_ce" : "maps_md");
		}
		count++;
	}
	return count;
}

int platform_old_map_folders_move(void)
{
	int moved = 0;
	int index;

	for (index = 0; index < (int)(sizeof(old_map_folders) / sizeof(old_map_folders[0])); index++)
	{
		char from[1024], to[1024];

		if (!old_map_folder_movable(index, from, to, sizeof(from)))
			continue;
		if (posix_rename_directory(from, to) == 0)
		{
			platform_log("map folders: moved %s to %s", from, to);
			moved++;
		}
		else
		{
			platform_log("map folders: could not move %s to %s; its maps stay there and still play", from, to);
		}
	}
	return moved;
}

/* ---------- file handles */

struct platform_file
{
	int descriptor;
	char path[1024];
};

static void file_destroy(struct platform_handle *handle)
{
	struct platform_file *file = handle->data;

	close(file->descriptor);
	free(file);
}

static struct platform_file *file_from_handle(HANDLE handle)
{
	struct platform_handle *record = platform_handle_get(handle, _platform_handle_file);

	return record ? record->data : NULL;
}

/* The game is about to write a new map over one of its map cache files
(source/cache/cache_files_windows.c). The Switch's host keeps those files in
memory (port/switch/host/host_memory_files.c): the file is emptied and given
its size back, so the old map's memory goes before the new map's is taken.
A file on a disk is written over as it is, and the other builds have no
such call. */
#ifdef HALO_SWITCH_CONSOLE
void platform_file_discard(HANDLE handle)
{
	struct platform_file *file = file_from_handle(handle);
	struct posix_file_information information;

	if (file && posix_fstat(file->descriptor, &information) == 0)
	{
		posix_truncate(file->descriptor, 0, 0);
		posix_truncate(file->descriptor, information.size_low, information.size_high);
	}
}
#endif

HANDLE WINAPI CreateFileA(LPCSTR file_name, DWORD desired_access, DWORD share_mode,
	LPSECURITY_ATTRIBUTES security_attributes, DWORD creation_disposition,
	DWORD flags_and_attributes, HANDLE template_file)
{
	struct platform_file *file;
	struct platform_handle *handle;
	struct posix_file_information information;
	char path[1024];
	int flags = 0;
	int existed;
	int descriptor;

	(void)share_mode;
	(void)security_attributes;
	(void)flags_and_attributes;
	(void)template_file;

	platform_translate_path(file_name, path, sizeof(path));
	existed = posix_stat(path, &information) == 0;

	if ((desired_access & GENERIC_READ) && (desired_access & GENERIC_WRITE))
		flags = O_RDWR;
	else if (desired_access & GENERIC_WRITE)
		flags = O_WRONLY;
	else
		flags = O_RDONLY;

	switch (creation_disposition)
	{
	case CREATE_NEW:
		flags |= O_CREAT | O_EXCL;
		break;
	case CREATE_ALWAYS:
		flags |= O_CREAT | O_TRUNC;
		break;
	case OPEN_EXISTING:
		break;
	case OPEN_ALWAYS:
		flags |= O_CREAT;
		break;
	case TRUNCATE_EXISTING:
		flags |= O_TRUNC;
		break;
	default:
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_HANDLE_VALUE;
	}
	if ((flags & O_TRUNC) && (flags & O_ACCMODE) == O_RDONLY)
		flags = (flags & ~O_ACCMODE) | O_RDWR;

	if (existed && (information.flags & _posix_file_is_directory))
	{
		SetLastError(ERROR_ACCESS_DENIED);
		return INVALID_HANDLE_VALUE;
	}

	descriptor = open(path, flags | O_CLOEXEC, 0644);
	if (descriptor < 0)
	{
		platform_set_last_error_from_errno(errno);
		if (errno == ENOENT && !existed && (flags & O_CREAT))
			SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}

	file = calloc(1, sizeof(*file));
	handle = file ? platform_handle_new(_platform_handle_file, file, file_destroy) : NULL;
	if (!handle)
	{
		close(descriptor);
		free(file);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return INVALID_HANDLE_VALUE;
	}
	file->descriptor = descriptor;
	snprintf(file->path, sizeof(file->path), "%s", path);

	/* Win32 reports whether an OPEN_ALWAYS/CREATE_ALWAYS target existed */
	SetLastError((existed && (creation_disposition == OPEN_ALWAYS || creation_disposition == CREATE_ALWAYS)) ?
		ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
	return handle;
}

#define READ_BOUNCE_SIZE 0x40000UL

static ssize_t read_some(struct platform_file *file, void *buffer, size_t count, BOOL positioned,
	unsigned long long offset)
{
	return positioned ?
		pread(file->descriptor, buffer, count, (off_t)offset) :
		read(file->descriptor, buffer, count);
}

static BOOL read_at(struct platform_file *file, LPVOID buffer, DWORD count, LPDWORD bytes_read,
	BOOL positioned, unsigned long long offset)
{
	DWORD total = 0;
	/* The renderer write-protects guest memory it caches (memory_watch.c),
	and the kernel fails a read() into a protected page instead of
	faulting. Guest memory is therefore filled through a bounce buffer: the
	copy faults like any other write, at the moment the data really lands,
	so a texture uploaded while the read is in flight is refreshed. */
#ifdef HALO_CONSOLE_NO_WRITE_WATCH
	/* The console will not write-protect guest memory (the first, the
	Switch: its host's host_mman.c), so there is no watch for a bounce to keep honest, and the
	staging buffer was a 128 KB malloc and free per read - each one a map and
	an unmap in the host - that made loading a map crawl. */
	BOOL bounce = FALSE;
#else
	BOOL bounce = platform_is_contiguous(buffer) ||
		platform_is_contiguous((char *)buffer + (count ? count - 1 : 0));
#endif
	char *staging = bounce ? malloc(count < READ_BOUNCE_SIZE ? count : READ_BOUNCE_SIZE) : NULL;

	if (bounce && !staging)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return FALSE;
	}
	while (total < count)
	{
		size_t wanted = count - total;
		ssize_t result;

		if (bounce && wanted > READ_BOUNCE_SIZE)
			wanted = READ_BOUNCE_SIZE;
		result = read_some(file, bounce ? staging : (char *)buffer + total, wanted, positioned, offset + total);
		if (result < 0)
		{
			if (errno == EINTR)
				continue;
			platform_set_last_error_from_errno(errno);
			if (bytes_read)
				*bytes_read = total;
			free(staging);
			return FALSE;
		}
		if (result == 0)
			break;
		if (bounce)
#ifdef HALO_64BIT
			/* (read-only guest pages, which a host page holds with others) */
			platform_contiguous_write((char *)buffer + total, staging, (size_t)result);
#else
			memcpy((char *)buffer + total, staging, (size_t)result);
#endif
		total += (DWORD)result;
	}
	free(staging);
#ifdef HALO_CONSOLE_NO_WRITE_WATCH
	/* read straight into guest memory, past the write watch (above): if it
	was texture memory, its textures are drawn again from it */
	if (total && platform_is_contiguous(buffer))
		memory_watch_prepare_write(buffer, total);
#endif
	if (bytes_read)
		*bytes_read = total;
	return TRUE;
}

static BOOL write_at(struct platform_file *file, LPCVOID buffer, DWORD count, LPDWORD bytes_written,
	BOOL positioned, unsigned long long offset)
{
	DWORD total = 0;

	while (total < count)
	{
		ssize_t result = positioned ?
			pwrite(file->descriptor, (const char *)buffer + total, count - total, (off_t)(offset + total)) :
			write(file->descriptor, (const char *)buffer + total, count - total);

		if (result < 0)
		{
			if (errno == EINTR)
				continue;
			platform_set_last_error_from_errno(errno);
			if (bytes_written)
				*bytes_written = total;
			return FALSE;
		}
		total += (DWORD)result;
	}
	if (bytes_written)
		*bytes_written = total;
	return TRUE;
}

static unsigned long long overlapped_offset(LPOVERLAPPED overlapped)
{
	return ((unsigned long long)overlapped->OffsetHigh << 32) | overlapped->Offset;
}

BOOL WINAPI ReadFile(HANDLE handle, LPVOID buffer, DWORD count, LPDWORD bytes_read, LPOVERLAPPED overlapped)
{
	struct platform_file *file = file_from_handle(handle);
	BOOL result;
	DWORD done = 0;

	if (!file)
		return FALSE;
	result = read_at(file, buffer, count, &done, overlapped != NULL, overlapped ? overlapped_offset(overlapped) : 0);
	if (bytes_read)
		*bytes_read = done;
	if (overlapped)
	{
		overlapped->Internal = result ? 0 : GetLastError();
		overlapped->InternalHigh = done;
		if (overlapped->hEvent)
			SetEvent(overlapped->hEvent);
	}
	return result;
}

BOOL WINAPI WriteFile(HANDLE handle, LPCVOID buffer, DWORD count, LPDWORD bytes_written, LPOVERLAPPED overlapped)
{
	struct platform_file *file = file_from_handle(handle);
	BOOL result;
	DWORD done = 0;

	if (!file)
		return FALSE;
	result = write_at(file, buffer, count, &done, overlapped != NULL, overlapped ? overlapped_offset(overlapped) : 0);
	if (bytes_written)
		*bytes_written = done;
	if (overlapped)
	{
		overlapped->Internal = result ? 0 : GetLastError();
		overlapped->InternalHigh = done;
		if (overlapped->hEvent)
			SetEvent(overlapped->hEvent);
	}
	return result;
}

/* ReadFileEx/WriteFileEx complete immediately; as on Win32, the completion
routine runs during the issuing thread's next alertable wait. */

static void file_completion_apc(void *routine, void *overlapped, void *unused)
{
	LPOVERLAPPED request = overlapped;

	(void)unused;
	((LPOVERLAPPED_COMPLETION_ROUTINE)routine)((DWORD)request->Internal, (DWORD)request->InternalHigh, request);
}

BOOL WINAPI ReadFileEx(HANDLE handle, LPVOID buffer, DWORD count, LPOVERLAPPED overlapped,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine)
{
	struct platform_file *file = file_from_handle(handle);
	DWORD done = 0;
	BOOL result;

	if (!file || !overlapped)
	{
		if (file)
			SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	result = read_at(file, buffer, count, &done, TRUE, overlapped_offset(overlapped));
	overlapped->Internal = result ? ERROR_SUCCESS : GetLastError();
	if (result && done == 0 && count > 0)
		overlapped->Internal = ERROR_HANDLE_EOF;
	overlapped->InternalHigh = done;
	platform_queue_apc(file_completion_apc, (void *)completion_routine, overlapped, NULL);
	SetLastError(ERROR_SUCCESS);
	return TRUE;
}

BOOL WINAPI WriteFileEx(HANDLE handle, LPCVOID buffer, DWORD count, LPOVERLAPPED overlapped,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine)
{
	struct platform_file *file = file_from_handle(handle);
	DWORD done = 0;
	BOOL result;

	if (!file || !overlapped)
	{
		if (file)
			SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	result = write_at(file, buffer, count, &done, TRUE, overlapped_offset(overlapped));
	overlapped->Internal = result ? ERROR_SUCCESS : GetLastError();
	overlapped->InternalHigh = done;
	platform_queue_apc(file_completion_apc, (void *)completion_routine, overlapped, NULL);
	SetLastError(ERROR_SUCCESS);
	return TRUE;
}

DWORD WINAPI SetFilePointer(HANDLE handle, LONG distance, PLONG distance_high, DWORD method)
{
	struct platform_file *file = file_from_handle(handle);
	unsigned long low, high;
	int whence;

	if (!file)
		return INVALID_SET_FILE_POINTER;
	switch (method)
	{
	case FILE_BEGIN: whence = SEEK_SET; break;
	case FILE_CURRENT: whence = SEEK_CUR; break;
	case FILE_END: whence = SEEK_END; break;
	default:
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_SET_FILE_POINTER;
	}
	if (posix_seek(file->descriptor, distance,
		distance_high ? *distance_high : (distance < 0 ? -1 : 0), whence, &low, &high) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return INVALID_SET_FILE_POINTER;
	}
	if (distance_high)
		*distance_high = (LONG)high;
	SetLastError(ERROR_SUCCESS);
	return (DWORD)low;
}

DWORD WINAPI GetFileSize(HANDLE handle, LPDWORD size_high)
{
	struct platform_file *file = file_from_handle(handle);
	struct posix_file_information information;

	if (!file)
		return INVALID_FILE_SIZE;
	if (posix_fstat(file->descriptor, &information) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return INVALID_FILE_SIZE;
	}
	if (size_high)
		*size_high = information.size_high;
	SetLastError(ERROR_SUCCESS);
	return information.size_low;
}

BOOL WINAPI SetEndOfFile(HANDLE handle)
{
	struct platform_file *file = file_from_handle(handle);
	unsigned long low, high;

	if (!file)
		return FALSE;
	if (posix_seek(file->descriptor, 0, 0, SEEK_CUR, &low, &high) != 0 ||
		posix_truncate(file->descriptor, low, high) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI GetFileTime(HANDLE handle, LPFILETIME creation_time, LPFILETIME last_access_time, LPFILETIME last_write_time)
{
	struct platform_file *file = file_from_handle(handle);
	struct posix_file_information information;

	if (!file)
		return FALSE;
	if (posix_fstat(file->descriptor, &information) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	if (creation_time)
		platform_unix_time_to_filetime(information.creation_seconds, information.creation_nanoseconds, creation_time);
	if (last_access_time)
		platform_unix_time_to_filetime(information.access_seconds, information.access_nanoseconds, last_access_time);
	if (last_write_time)
		platform_unix_time_to_filetime(information.modification_seconds, information.modification_nanoseconds, last_write_time);
	return TRUE;
}

BOOL WINAPI SetFileTime(HANDLE handle, CONST FILETIME *creation_time, CONST FILETIME *last_access_time,
	CONST FILETIME *last_write_time)
{
	struct platform_file *file = file_from_handle(handle);
	unsigned long access_seconds = 0, access_nanoseconds = 0;
	unsigned long write_seconds = 0, write_nanoseconds = 0;

	(void)creation_time;
	if (!file)
		return FALSE;
	if (last_access_time)
		platform_filetime_to_unix_time(last_access_time, &access_seconds, &access_nanoseconds);
	if (last_write_time)
		platform_filetime_to_unix_time(last_write_time, &write_seconds, &write_nanoseconds);
	if (posix_set_file_times(file->path, access_seconds, access_nanoseconds, write_seconds, write_nanoseconds) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	return TRUE;
}

/* ---------- operations on paths */

static void fill_attribute_data(const struct posix_file_information *information, WIN32_FILE_ATTRIBUTE_DATA *data)
{
	data->dwFileAttributes = 0;
	if (information->flags & _posix_file_is_directory)
		data->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
	if (information->flags & _posix_file_is_read_only)
		data->dwFileAttributes |= FILE_ATTRIBUTE_READONLY;
	if (!data->dwFileAttributes)
		data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	platform_unix_time_to_filetime(information->creation_seconds, information->creation_nanoseconds, &data->ftCreationTime);
	platform_unix_time_to_filetime(information->access_seconds, information->access_nanoseconds, &data->ftLastAccessTime);
	platform_unix_time_to_filetime(information->modification_seconds, information->modification_nanoseconds, &data->ftLastWriteTime);
	data->nFileSizeHigh = information->size_high;
	data->nFileSizeLow = information->size_low;
}

DWORD WINAPI GetFileAttributesA(LPCSTR file_name)
{
	struct posix_file_information information;
	WIN32_FILE_ATTRIBUTE_DATA data;
	char path[1024];

	platform_translate_path(file_name, path, sizeof(path));
	if (posix_stat(path, &information) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return (DWORD)-1;
	}
	fill_attribute_data(&information, &data);
	return data.dwFileAttributes;
}

BOOL WINAPI GetFileAttributesExA(LPCSTR file_name, GET_FILEEX_INFO_LEVELS level, LPVOID file_information)
{
	struct posix_file_information information;
	char path[1024];

	if (level != GetFileExInfoStandard)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	platform_translate_path(file_name, path, sizeof(path));
	if (posix_stat(path, &information) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	fill_attribute_data(&information, file_information);
	return TRUE;
}

BOOL WINAPI SetFileAttributesA(LPCSTR file_name, DWORD attributes)
{
	char path[1024];
	struct posix_file_information information;

	platform_translate_path(file_name, path, sizeof(path));
	if (posix_stat(path, &information) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	/* only the read-only bit has a POSIX equivalent */
	if (posix_set_read_only(path, (attributes & FILE_ATTRIBUTE_READONLY) != 0) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI DeleteFileA(LPCSTR file_name)
{
	char path[1024];

	platform_translate_path(file_name, path, sizeof(path));
	if (unlink(path) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI MoveFileA(LPCSTR existing_file_name, LPCSTR new_file_name)
{
	char from[1024], to[1024];
	struct posix_file_information information;

	platform_translate_path(existing_file_name, from, sizeof(from));
	platform_translate_path(new_file_name, to, sizeof(to));
	if (posix_stat(to, &information) == 0)
	{
		SetLastError(ERROR_ALREADY_EXISTS);
		return FALSE;
	}
	if (rename(from, to) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI CopyFileA(LPCSTR existing_file_name, LPCSTR new_file_name, BOOL fail_if_exists)
{
	/* (the copy's buffer is on the heap: 64 KB on the stack overflowed the
	small stacks of handheld threads, the PSP's profile save among them) */
	enum { COPY_BUFFER_SIZE = 65536 };
	char from[1024], to[1024];
	char *buffer;
	int source, destination;
	BOOL success = TRUE;

	platform_translate_path(existing_file_name, from, sizeof(from));
	platform_translate_path(new_file_name, to, sizeof(to));
	buffer = (char *)malloc(COPY_BUFFER_SIZE);
	if (!buffer)
	{
		platform_set_last_error_from_errno(ENOMEM);
		return FALSE;
	}
	source = open(from, O_RDONLY | O_CLOEXEC);
	if (source < 0)
	{
		platform_set_last_error_from_errno(errno);
		free(buffer);
		return FALSE;
	}
	destination = open(to, O_WRONLY | O_CREAT | O_CLOEXEC | (fail_if_exists ? O_EXCL : O_TRUNC), 0644);
	if (destination < 0)
	{
		platform_set_last_error_from_errno(errno);
		close(source);
		free(buffer);
		return FALSE;
	}
	for (;;)
	{
		ssize_t count = read(source, buffer, COPY_BUFFER_SIZE);

		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
		{
			if (count < 0)
			{
				platform_set_last_error_from_errno(errno);
				success = FALSE;
			}
			break;
		}
		if (write(destination, buffer, (size_t)count) != count)
		{
			platform_set_last_error_from_errno(errno);
			success = FALSE;
			break;
		}
	}
	close(source);
	close(destination);
	free(buffer);
	return success;
}

BOOL WINAPI CreateDirectoryA(LPCSTR path_name, LPSECURITY_ATTRIBUTES security_attributes)
{
	char path[1024];

	(void)security_attributes;
	platform_translate_path(path_name, path, sizeof(path));
	if (posix_make_directory(path) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI RemoveDirectoryA(LPCSTR path_name)
{
	char path[1024];

	platform_translate_path(path_name, path, sizeof(path));
	if (rmdir(path) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI GetDiskFreeSpaceExA(LPCSTR directory_name, PULARGE_INTEGER free_bytes_available,
	PULARGE_INTEGER total_bytes, PULARGE_INTEGER total_free_bytes)
{
	char path[1024];
	unsigned long free_low, free_high, total_low, total_high;

	platform_translate_path(directory_name ? directory_name : "d:\\", path, sizeof(path));
	if (posix_disk_space(path, &free_low, &free_high, &total_low, &total_high) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	if (free_bytes_available)
	{
		free_bytes_available->LowPart = free_low;
		free_bytes_available->HighPart = free_high;
	}
	if (total_free_bytes)
	{
		total_free_bytes->LowPart = free_low;
		total_free_bytes->HighPart = free_high;
	}
	if (total_bytes)
	{
		total_bytes->LowPart = total_low;
		total_bytes->HighPart = total_high;
	}
	return TRUE;
}

/* ---------- directory enumeration */

struct platform_find
{
	void *directory;
	char directory_path[1024];
	char pattern[MAX_PATH];
};

static void find_destroy(struct platform_handle *handle)
{
	struct platform_find *find = handle->data;

	posix_directory_close(find->directory);
	free(find);
}

/* Win32 wildcard match: '*' any run, '?' one character, case-insensitive;
"*.*" also matches names without a dot */
static BOOL wildcard_match(const char *pattern, const char *name)
{
	if (!strcmp(pattern, "*.*") || !strcmp(pattern, "*"))
		return TRUE;
	while (*pattern)
	{
		if (*pattern == '*')
		{
			while (*pattern == '*')
				pattern++;
			if (!*pattern)
				return TRUE;
			for (; *name; name++)
			{
				if (wildcard_match(pattern, name))
					return TRUE;
			}
			return FALSE;
		}
		if (!*name)
			return FALSE;
		if (*pattern != '?' && tolower((unsigned char)*pattern) != tolower((unsigned char)*name))
			return FALSE;
		pattern++;
		name++;
	}
	return *name == '\0';
}

static BOOL find_next_entry(struct platform_find *find, LPWIN32_FIND_DATAA data)
{
	char name[MAX_PATH];

	while (posix_directory_next(find->directory, name, sizeof(name)))
	{
		char full_path[1400];
		struct posix_file_information information;
		WIN32_FILE_ATTRIBUTE_DATA attributes;

		if (!wildcard_match(find->pattern, name))
			continue;
		snprintf(full_path, sizeof(full_path), "%s/%s", find->directory_path, name);
		if (posix_stat(full_path, &information) != 0)
			continue;
		fill_attribute_data(&information, &attributes);
		memset(data, 0, sizeof(*data));
		data->dwFileAttributes = attributes.dwFileAttributes;
		data->ftCreationTime = attributes.ftCreationTime;
		data->ftLastAccessTime = attributes.ftLastAccessTime;
		data->ftLastWriteTime = attributes.ftLastWriteTime;
		data->nFileSizeHigh = attributes.nFileSizeHigh;
		data->nFileSizeLow = attributes.nFileSizeLow;
		snprintf(data->cFileName, sizeof(data->cFileName), "%s", name);
		return TRUE;
	}
	SetLastError(ERROR_NO_MORE_FILES);
	return FALSE;
}

HANDLE WINAPI FindFirstFileA(LPCSTR file_name, LPWIN32_FIND_DATAA data)
{
	struct platform_find *find = calloc(1, sizeof(*find));
	struct platform_handle *handle;
	char xbox_directory[MAX_PATH];
	const char *separator = strrchr(file_name, '\\');

	if (!find)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return INVALID_HANDLE_VALUE;
	}
	if (!separator)
		separator = strrchr(file_name, '/');
	if (separator)
	{
		snprintf(xbox_directory, sizeof(xbox_directory), "%.*s", (int)(separator - file_name), file_name);
		snprintf(find->pattern, sizeof(find->pattern), "%s", separator + 1);
	}
	else
	{
		xbox_directory[0] = '\0';
		snprintf(find->pattern, sizeof(find->pattern), "%s", file_name);
	}
	platform_translate_path(xbox_directory, find->directory_path, sizeof(find->directory_path));
	find->directory = posix_directory_open(find->directory_path);
	if (!find->directory)
	{
		platform_set_last_error_from_errno(errno);
		free(find);
		return INVALID_HANDLE_VALUE;
	}
	handle = platform_handle_new(_platform_handle_find, find, find_destroy);
	if (!handle)
	{
		posix_directory_close(find->directory);
		free(find);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return INVALID_HANDLE_VALUE;
	}
	if (!find_next_entry(find, data))
	{
		CloseHandle(handle);
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	return handle;
}

BOOL WINAPI FindNextFileA(HANDLE find_file, LPWIN32_FIND_DATAA data)
{
	struct platform_handle *handle = platform_handle_get(find_file, _platform_handle_find);

	if (!handle)
		return FALSE;
	return find_next_entry(handle->data, data);
}

/* FindClose is an XDK macro for CloseHandle */
