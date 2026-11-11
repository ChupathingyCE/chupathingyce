/*
LAUNCHER.C

Chupathingy's screen before the game (launcher.h): its own small loop in
the game's window (read input, clear, draw, present) before the first map
loads, drawn with the overlay (ui_overlay.c) and its built-in fonts and
button glyphs, in the Guide's look (ui_overlay_style.c: translucent glass
panels and rows, and the Xbox menus' plasma, from the overlay's own noise,
on the selection alone). No map, tag or bitmap of the game's is read, so nothing of the
retail game's is on screen. It draws with OpenGL, or with bgfx when that is
the renderer (xgpu_bgfx_launcher_frame).

Screens: Welcome (the first start), and PLAY, MAPS, SETTINGS, LOGS & HELP
and ABOUT (about.c's page) with QUIT, as a list of sections on the left and the open one in a panel on the
right. Maps shows each family of maps (halo_map_families.h) by its folder,
with its state read from the files' headers; a family's own screen opens
its folder, checks its maps and adds them from a file (xiso.c, pc_disc.c,
ce_installer.c, through sdl_platform.c's import thread). Logs & help sends
debug.txt and the last crash report (and config.toml, if the player says
so) to the log dump site, with addresses and the home folder taken out
first (crash_report_redact), and copies the version.

Everything here runs on the game's main thread, but the import and the
upload, which have threads of their own.
*/

#include "launcher.h"

#ifdef HALO_LAUNCHER

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "posix.h"
#include "gl.h"
#include "xgpu.h"
#include "ui_overlay.h"
#include "crash_report.h"
#include "browser_http.h"
#include "build_identity.h"
#include "about.h"
#include "settings_rows.h"
#include "map_download.h"
#include "update.h"

#include <SDL3/SDL.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* (updater.c's) */
const char *updater_version(void);
/* (halo_port_limits.h's: the network version this build announces) */
int delta_legacy_announce(void);
/* (sdl_platform.c's) */
SDL_Window *platform_sdl_window(void);

/* where Send my logs goes (puma's dumps.py: the page's form, POST /upload),
unless debug.dump_url says otherwise; and the Discord invite */
#define LAUNCHER_DUMP_URL "https://dump.chupathingy.org/upload"
#define LAUNCHER_DUMP_SITE "dump.chupathingy.org"
#define LAUNCHER_DISCORD_URL "https://discord.gg/chupathingy"

/* debug.txt's end that is sent, at most (the site takes 10 MB a file) */
#define LAUNCHER_LOG_TAIL (4 * 1024 * 1024)

enum
{
	MAXIMUM_FAMILIES = 16,
	MAXIMUM_ROWS = 128,
	MAXIMUM_MAP_ENTRIES = 512,
	MAXIMUM_TARGETS = 48,
	MAXIMUM_DIALOG_ROWS = 4,
	MAXIMUM_MODES = 8,
	PATH_SIZE = 1024,
};

/* ---------- colors: the look's palette, by name (ui_overlay_style.c's
ui_style_color) */

#define COLOR_TITLE ui_style_color(UI_COLOR_TITLE)
#define COLOR_TITLE_GLOW ui_style_color(UI_COLOR_TITLE_GLOW)
#define COLOR_NAV ui_style_color(UI_COLOR_NAV)
#define COLOR_NAV_GLOW ui_style_color(UI_COLOR_NAV_GLOW)
#define COLOR_WHITE ui_style_color(UI_COLOR_TEXT)
#define COLOR_GREY ui_style_color(UI_COLOR_TEXT_DIM)
#define COLOR_FAINT ui_style_color(UI_COLOR_FAINT)
#define COLOR_GREEN ui_style_color(UI_COLOR_GOOD)
#define COLOR_AMBER ui_style_color(UI_COLOR_WARNING)
#define COLOR_RED ui_style_color(UI_COLOR_BAD)
#define COLOR_HEAD ui_style_color(UI_COLOR_HEAD)
#define COLOR_HEAD_GLOW ui_style_color(UI_COLOR_HEAD_GLOW)
#define COLOR_ROW ui_style_color(UI_COLOR_LABEL)
#define COLOR_ROW_DIM ui_style_color(UI_COLOR_LABEL_DIM)
#define COLOR_ROW_GLOW ui_style_color(UI_COLOR_LABEL_GLOW)
#define COLOR_SELECTED_GLOW ui_style_color(UI_COLOR_SELECTED_GLOW)
#define COLOR_RULE ui_style_color(UI_COLOR_LINE)
#define COLOR_RULE_GLOW ui_style_color(UI_COLOR_LINE_GLOW)
#define COLOR_SEPARATOR ui_style_color(UI_COLOR_SEPARATOR)
#define COLOR_TAG_GREEN ui_style_color(UI_COLOR_GOOD)
#define COLOR_TAG_BLUE ui_style_color(UI_COLOR_TAG)
#define COLOR_SHADE ui_style_color(UI_COLOR_SHADE)
#define COLOR_PROMPT ui_style_color(UI_COLOR_PROMPT)
#define COLOR_PROMPT_GLOW ui_style_color(UI_COLOR_PROMPT_GLOW)
#define COLOR_EDGE ui_style_color(UI_COLOR_FIELD_EDGE)
#define COLOR_PATH_FIELD ui_style_color(UI_COLOR_FIELD)

/* a color, its alpha alpha (0 to 255) */
static unsigned int with_alpha(unsigned int color, unsigned int alpha)
{
	return (color & 0xFFFFFF00) | (alpha & 0xFF);
}

/* ---------- what is shown */

enum
{
	_screen_welcome,
	_screen_play,
	_screen_maps,
	_screen_family,
	_screen_map_list,
	_screen_logs,
	_screen_settings,
	/* ALL's: one section's keys */
	_screen_setting_keys,
	_screen_about,
};

enum
{
	_section_play,
	_section_maps,
	_section_settings,
	_section_logs,
	_section_about,
	_section_quit,
	NUMBER_OF_SECTIONS
};

static const char *const section_names[NUMBER_OF_SECTIONS] = { "PLAY", "MAPS", "SETTINGS", "LOGS & HELP", "ABOUT",
	"QUIT" };

/* Settings' tabs: settings_rows.h's groups, then PATHS and ALL */
enum
{
	_tab_display,
	_tab_audio,
	_tab_network,
	_tab_paths,
	_tab_updates,
	_tab_all,
	NUMBER_OF_TABS
};

static const char *const tab_names[NUMBER_OF_TABS] = { "DISPLAY", "AUDIO", "NETWORK", "PATHS", "UPDATES", "ALL" };

enum
{
	_action_none,
	_action_play,
	_action_play_mode,
	_action_start_mode,
	_action_family,
	_action_move_old_folders,
	_action_add_file,
	_action_add_halo_pc,
	_action_make_folders,
	_action_open_game_folder,
	_action_open_family_folder,
	_action_check_family,
	_action_downloads,
	_action_pick_folder,
	_action_send_logs,
	_action_open_logs,
	_action_open_crashes,
	_action_copy_version,
	_action_discord,
	/* Settings' */
	_action_setting,
	_action_setting_key,
	_action_settings_section,
	_action_open_path,
	_action_check_updates,
	_action_install_update,
	/* the dialogs' */
	_action_send_with_settings,
	_action_send_without_settings,
	_action_ask_each_time,
	_action_always_play,
	_action_close_dialog,
};

/* a family of maps, as its folder holds it */
enum
{
	_family_xbox,
	_family_custom_edition,
	_family_halo_pc,
	_family_halomd,
	_family_pack,
};

struct family
{
	int kind;
	/* its folder's name in the data root ("maps", "maps_ce", "maps_cex") */
	char folder[64];
	char name[48];
	char tag[8];
	/* (packs: known to the library, and its name for the library's maps) */
	char pack_id[16];
	int exists;
	int good;
	int broken;
	/* MCC maps (cache version 13): not played yet */
	int unsupported;
	/* Halo PC maps in the Xbox's folder */
	int stray;
	/* a known pack's maps the folder doesn't have; Custom Edition's resource
	maps it doesn't have */
	int missing;
	char missing_names[160];
	/* the Xbox's ui.map's cache version (0: none) */
	long ui_version;
};

struct row
{
	int action;
	int argument;
	char tag[8];
	unsigned int tag_color;
	char label[72];
	char sub[96];
	char value[72];
	unsigned int value_color;
	char help[200];
	int disabled;
	/* drawn dimmed, still picked (a mode not installed) */
	int dim;
	/* left and right change its value */
	int cycles;
	/* a gap above it */
	int gap;
	/* Settings': its row of settings_rows.h's, or (ALL) its key, and the
	tag after its label */
	const struct settings_row *setting;
	char key[64];
	char badge[24];
};

/* a way to play: the game as it is (vanilla), or a pack with its own menus
(game.pack_menus: Halo CE+'s maps_cex/ui.map) */
struct mode
{
	/* "vanilla", or the pack's id ("cex") */
	char id[16];
	/* its name on its row ("HALO CE+"), and in a sentence ("Halo CE+") */
	char name[48];
	char title[48];
	char tag[8];
	char folder[64];
	int installed;
};

struct map_entry
{
	char name[96];
	char state[32];
	unsigned int color;
};

/* a place the mouse can point at (layout coordinates) */
enum
{
	_target_row,
	_target_section,
	_target_dialog_row,
	_target_button,
	_target_tab,
};

struct target
{
	float x0, y0, x1, y1;
	int kind;
	int index;
};

/* the input, as the screens take it */
enum
{
	_button_none,
	_button_up,
	_button_down,
	_button_left,
	_button_right,
	_button_a,
	_button_b,
	_button_x,
	_button_y,
	_button_previous,
	_button_next,
};

enum
{
	_dialog_none,
	_dialog_picking,
	_dialog_import,
	_dialog_message,
	_dialog_send_ask,
	_dialog_sending,
	_dialog_sent,
	_dialog_mode_question,
};

/* the file chooser's answer (SDL's dialogs answer through a callback, on
some systems on another thread) */
struct chooser
{
	SDL_AtomicInt done;
	char path[PATH_SIZE];
};

/* the upload of the logs, on a thread of its own */
struct upload
{
	SDL_AtomicInt done;
	char url[512];
	char *body;
	size_t body_length;
	char content_type[96];
	char headers[256];
	int status;
	char response[2048];
	char error[256];
};

static struct
{
	int shown;
	int open_on_maps;
	int done;

	/* the first start: the game folder, which may change ("I already have a
	maps folder") */
	int welcome;
	char *destination;
	unsigned long destination_size;

	/* the data root, whole */
	char root[PATH_SIZE];

	int screen;
	int section;
	int in_panel;
	int selected;
	int scroll;
	/* the family screen's family, and the Maps row to go back to */
	int family;
	int maps_row;
	int family_row;

	struct family families[MAXIMUM_FAMILIES];
	int family_count;
	int old_folders;

	struct mode modes[MAXIMUM_MODES];
	int mode_count;
	int installed_modes;
	/* the mode PLAY starts (the last one picked, else the default) */
	int mode;

	/* Settings: the tab, ALL's section (its name) and row, and whether a
	setting that shows only after a restart was changed */
	int tab;
	char setting_section[32];
	int settings_row;
	int restart_needed;

	struct map_entry map_entries[MAXIMUM_MAP_ENTRIES];
	int map_entry_count;

	struct row rows[MAXIMUM_ROWS];
	int row_count;

	/* the last action's result, shown in place of the help line until the
	selection moves */
	char status[256];
	unsigned int status_color;

	int dialog;
	char dialog_title[64];
	char dialog_text[512];
	unsigned int dialog_text_color;
	struct row dialog_rows[MAXIMUM_DIALOG_ROWS];
	int dialog_row_count;
	int dialog_selected;

	struct chooser chooser;
	/* what the chosen file or folder is for (_action_add_file, ...) */
	int chooser_purpose;

	int import_kind;
	char import_file[256];
	unsigned long long import_done, import_total;

	struct upload upload;
	char sent_code[32];

	/* the layout: its left and right edges (the 640 columns, and what a
	wider window adds either side) */
	float left, right;
	/* the picture's place in the window (GL's: from the lower left) */
	int picture_x, picture_y, picture_width, picture_height;
	int window_width, window_height;

	struct target targets[MAXIMUM_TARGETS];
	int target_count;
	int hovered_kind, hovered_index;

	/* a held direction, repeated */
	int held_button;
	Uint64 held_next;
	int stick_direction;

	/* debug.launcher_test */
	char script[2048];
	const char *script_cursor;
	Uint64 script_next;
	char screenshot[128];

	/* ABOUT's page */
	struct about_view about;
} launcher;

static void modes_find(void);

/* ---------- small helpers */

static int ends_with(const char *name, const char *ending)
{
	size_t length = strlen(name), ending_length = strlen(ending), index;

	if (length < ending_length)
		return 0;
	for (index = 0; index < ending_length; index++)
	{
		if (tolower((unsigned char)name[length - ending_length + index]) != tolower((unsigned char)ending[index]))
			return 0;
	}
	return 1;
}

static void upper(char *text)
{
	for (; *text; text++)
		*text = (char)toupper((unsigned char)*text);
}

static int is_directory(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && (information.flags & _posix_file_is_directory);
}

static int file_exists(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && !(information.flags & _posix_file_is_directory);
}

/* every missing folder along path made */
static void make_directories(const char *path)
{
	char partial[PATH_SIZE];
	size_t index;

	snprintf(partial, sizeof(partial), "%s", path);
	for (index = 1; partial[index]; index++)
	{
		if ((partial[index] == '/' || partial[index] == '\\') && partial[index - 1] != ':')
		{
			char separator = partial[index];

			partial[index] = '\0';
			if (!is_directory(partial))
				posix_make_directory(partial);
			partial[index] = separator;
		}
	}
	if (!is_directory(partial))
		posix_make_directory(partial);
}

/* a folder in the data root by its name, in the case it has on disk (or as
named, when there is none) */
static void root_path(const char *name, char *path, size_t size)
{
	char on_disk[256];

	if (!posix_find_entry_case_insensitive(launcher.root, name, on_disk, sizeof(on_disk)))
		snprintf(on_disk, sizeof(on_disk), "%s", name);
	snprintf(path, size, "%s/%s", launcher.root, on_disk);
}

/* a path as the player's system writes it (Windows' separators) */
static void shown_path(const char *path, char *shown, size_t size)
{
	snprintf(shown, size, "%s", path);
#ifdef _WIN32
	for (; *shown; shown++)
	{
		if (*shown == '/')
			*shown = '\\';
	}
#endif
}

/* the data root, whole: a relative one from the working folder */
static void set_root(const char *root)
{
	char *working = port_folder_absolute(root) ? NULL : SDL_GetCurrentDirectory();

	if (working)
	{
		/* (SDL's ends with its separator) */
		if (!strcmp(root, "."))
			snprintf(launcher.root, sizeof(launcher.root), "%s", working);
		else
			snprintf(launcher.root, sizeof(launcher.root), "%s%s", working, !strncmp(root, "./", 2) ? root + 2 : root);
		SDL_free(working);
	}
	else
		snprintf(launcher.root, sizeof(launcher.root), "%s", root);
	while (strlen(launcher.root) > 1 && (launcher.root[strlen(launcher.root) - 1] == '/' ||
		launcher.root[strlen(launcher.root) - 1] == '\\'))
	{
		launcher.root[strlen(launcher.root) - 1] = '\0';
	}
}

/* the logs folder (before the data root is known, as near as can be told) */
static void logs_folder(char *path, size_t size)
{
	if (launcher.welcome)
	{
		if (!platform_logs_folder_early(path, (unsigned long)size))
			snprintf(path, size, "%s/logs", launcher.root);
		return;
	}
	snprintf(path, size, "%s", platform_folder(_port_folder_logs));
}

static void set_status(unsigned int color, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void set_status(unsigned int color, const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(launcher.status, sizeof(launcher.status), format, arguments);
	va_end(arguments);
	launcher.status_color = color;
}

/* ---------- the maps on disk */

enum
{
	MAP_HEADER_SIGNATURE = 0x68656164,
	MAP_VERSION_XBOX = 5,
	MAP_VERSION_HALO_PC = 7,
	MAP_VERSION_MCC = 13,
	MAP_VERSION_CUSTOM_EDITION = 609,
	/* (a Chimera-compressed map's version: the real one in the low half) */
	MAP_VERSION_COMPRESSED = 0x861A0000,
};

/* a map file's cache version by its header (a compressed map's as its own);
0 when it is not a cache file */
static long map_version(const char *path)
{
	unsigned char header[8];
	FILE *file = fopen(path, "rb");
	unsigned long version = 0;

	if (!file)
		return 0;
	if (fread(header, 1, sizeof(header), file) == sizeof(header) &&
		(header[0] | header[1] << 8 | header[2] << 16 | (unsigned long)header[3] << 24) == MAP_HEADER_SIGNATURE)
	{
		version = header[4] | header[5] << 8 | header[6] << 16 | (unsigned long)header[7] << 24;
		if ((version & 0xFFFF0000UL) == MAP_VERSION_COMPRESSED)
			version &= 0xFFFF;
	}
	fclose(file);
	return (long)version;
}

/* the library of known Xbox map packs (map_packs.c's, xbox_mod_maps.h):
their folders' names and their maps' files, for the packs' rows */
struct xbox_mod_pack
{
	char const *id;
	char const *aliases;
	char const *name;
	char const *tag;
	char const *team;
	char const *credits;
	char const *source;
};

struct xbox_mod_map
{
	unsigned long fingerprint;
	char const *pack;
	char const *file;
	char const *name;
	char const *pack_version;
	char const *author;
	char const *source;
};

#include "../game/xbox_mod_maps.h"

/* the known pack whose folder (maps_<name>) this is, by its id or one of
its aliases; NULL for none */
static const struct xbox_mod_pack *known_pack(const char *name)
{
	size_t index;

	for (index = 0; index < sizeof(xbox_mod_packs) / sizeof(*xbox_mod_packs); index++)
	{
		const char *alias = xbox_mod_packs[index].aliases;

		if (!SDL_strcasecmp(name, xbox_mod_packs[index].id))
			return &xbox_mod_packs[index];
		while (alias && *alias)
		{
			size_t length = strcspn(alias, ",");

			if (strlen(name) == length && !SDL_strncasecmp(name, alias, length))
				return &xbox_mod_packs[index];
			alias += length;
			if (*alias == ',')
				alias++;
		}
	}
	return NULL;
}

/* whether a map file is one the family's folder has besides its maps: the
Custom Edition's resource maps */
static int resource_map(const char *name)
{
	return !SDL_strcasecmp(name, "bitmaps.map") || !SDL_strcasecmp(name, "sounds.map") ||
		!SDL_strcasecmp(name, "loc.map");
}

/* the version a family's maps have */
static int family_version_good(const struct family *family, long version)
{
	switch (family->kind)
	{
	case _family_custom_edition:
		return version == MAP_VERSION_CUSTOM_EDITION;
	case _family_halo_pc:
	case _family_halomd:
		return version == MAP_VERSION_HALO_PC;
	default:
		return version == MAP_VERSION_XBOX;
	}
}

static void map_entry_add(const char *name, const char *state, unsigned int color)
{
	struct map_entry *entry;

	if (launcher.map_entry_count >= MAXIMUM_MAP_ENTRIES)
		return;
	entry = &launcher.map_entries[launcher.map_entry_count++];
	snprintf(entry->name, sizeof(entry->name), "%s", name);
	snprintf(entry->state, sizeof(entry->state), "%s", state);
	entry->color = color;
}

static void append_name(char *names, size_t size, const char *name)
{
	size_t length = strlen(names);

	if (length + strlen(name) + 3 < size)
		snprintf(names + length, size - length, "%s%s", length ? ", " : "", name);
}

/* whether a family's maps read Custom Edition's resource maps (every family
past the Xbox's: halo_map_families.h) */
static int family_needs_resources(const struct family *family)
{
	return family->kind == _family_custom_edition || family->kind == _family_halo_pc ||
		family->kind == _family_halomd;
}

/* whether a resource map's paths name one ending with ending: Custom
Edition's bitmaps.map has <path>__pixels, its sounds.map
<path>__permutations; Halo PC retail's are named <path>_0 and <path>__0__0
(map_families.c's map_family_resources_ready, which the game asks) */
static int resource_map_is_custom_edition(const char *path, uint32_t type, const char *ending)
{
	uint32_t header[4];
	FILE *file = fopen(path, "rb");
	int result = 0;

	if (!file)
		return 0;
	if (fread(header, sizeof(header), 1, file) == 1 && header[0] == type && header[3] && header[1] >= sizeof(header) &&
		header[1] <= header[2] && (!ending || header[2] - header[1] <= 0x400000))
	{
		uint32_t size = header[2] - header[1];
		char *paths = ending ? malloc((size_t)size + 1) : NULL;

		if (!ending)
			result = 1;
		else if (paths && fseek(file, (long)header[1], SEEK_SET) == 0 && fread(paths, 1, size, file) == size)
		{
			size_t ending_length = strlen(ending);
			uint32_t start = 0, at;

			paths[size] = '\0';
			for (at = 0; at <= size && !result; at++)
			{
				if (paths[at])
					continue;
				result = at - start >= ending_length && !memcmp(paths + at - ending_length, ending, ending_length);
				start = at + 1;
			}
		}
		free(paths);
	}
	fclose(file);
	return result;
}

/* Custom Edition's resource maps a family's maps need: each one missing from
maps_ce (and its older places, maps/ce and custom_maps), or not Custom
Edition's, counted, named in the family's missing_names and, with list,
given an entry */
static void family_check_resources(struct family *family, int list)
{
	static const char *const resources[] = { "bitmaps.map", "sounds.map", "loc.map" };
	static const char *const endings[] = { "__pixels", "__permutations", NULL };
	static const char *const places[] = { "maps_ce", "maps/ce", "custom_maps" };
	size_t index, place;
	int first_entry = launcher.map_entry_count;

	for (index = 0; index < sizeof(resources) / sizeof(*resources); index++)
	{
		char folder[PATH_SIZE], name[256], path[PATH_SIZE + 256];
		int found = 0, good = 0;

		for (place = 0; place < sizeof(places) / sizeof(*places) && !found; place++)
		{
			snprintf(folder, sizeof(folder), "%s/%s", launcher.root, places[place]);
			if (posix_find_entry_case_insensitive(folder, resources[index], name, sizeof(name)))
			{
				found = 1;
				snprintf(path, sizeof(path), "%s/%s", folder, name);
				good = resource_map_is_custom_edition(path, (uint32_t)index + 1, endings[index]);
			}
		}
		if (good)
			continue;
		family->missing++;
		append_name(family->missing_names, sizeof(family->missing_names), resources[index]);
		if (list)
			map_entry_add(resources[index], found ? "Not Custom Edition's" : "Missing", COLOR_AMBER);
	}
	/* (listed first, above the maps that need them) */
	if (list && launcher.map_entry_count > first_entry && first_entry > 0)
	{
		struct map_entry moved[3];
		int count = launcher.map_entry_count - first_entry;

		memcpy(moved, &launcher.map_entries[first_entry], count * sizeof(*moved));
		memmove(&launcher.map_entries[count], &launcher.map_entries[0], first_entry * sizeof(*moved));
		memcpy(&launcher.map_entries[0], moved, count * sizeof(*moved));
	}
}

/* a family's folder read: its maps counted by their headers; with list,
each one's entry for the map list */
static void family_scan(struct family *family, int list)
{
	char folder[PATH_SIZE];
	char name[256];
	void *directory;

	family->exists = 0;
	family->good = family->broken = family->unsupported = family->stray = family->missing = 0;
	family->missing_names[0] = '\0';
	family->ui_version = 0;
	if (list)
		launcher.map_entry_count = 0;
	root_path(family->folder, folder, sizeof(folder));
	directory = posix_directory_open(folder);
	if (!directory)
		return;
	family->exists = 1;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		char path[PATH_SIZE + 256];
		long version;

		if (!ends_with(name, ".map"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, name);
		if (family->kind == _family_custom_edition && resource_map(name))
			continue;
		version = map_version(path);
		if (family->kind == _family_xbox && !SDL_strcasecmp(name, "ui.map"))
			family->ui_version = version;
		if (family_version_good(family, version))
		{
			family->good++;
			if (list)
				map_entry_add(name, "Good", COLOR_GREEN);
		}
		else if (family->kind == _family_xbox &&
			(version == MAP_VERSION_HALO_PC || version == MAP_VERSION_CUSTOM_EDITION))
		{
			/* (Halo PC's maps, which play from their own folders) */
			family->stray++;
			if (list)
				map_entry_add(name, "Halo PC map", COLOR_AMBER);
		}
		else if (version == MAP_VERSION_MCC)
		{
			family->unsupported++;
			if (list)
				map_entry_add(name, "MCC map, not yet", COLOR_GREY);
		}
		else
		{
			family->broken++;
			if (list)
				map_entry_add(name, "Broken", COLOR_RED);
		}
	}
	posix_directory_close(directory);
	/* what the folder should have and doesn't (Custom Edition's resource
	maps, for its maps, HaloMD's and Halo PC's) */
	if (family_needs_resources(family) && family->good)
		family_check_resources(family, list);
	if (family->kind == _family_pack && family->pack_id[0])
	{
		size_t index;

		for (index = 0; index < sizeof(xbox_mod_maps) / sizeof(*xbox_mod_maps); index++)
		{
			char file[128];

			if (strcmp(xbox_mod_maps[index].pack, family->pack_id))
				continue;
			snprintf(file, sizeof(file), "%s.map", xbox_mod_maps[index].file);
			if (!posix_find_entry_case_insensitive(folder, file, name, sizeof(name)))
			{
				family->missing++;
				append_name(family->missing_names, sizeof(family->missing_names), file);
				if (list)
					map_entry_add(file, "Missing", COLOR_AMBER);
			}
		}
	}
}

static struct family *family_add(int kind, const char *folder, const char *name, const char *tag)
{
	struct family *family;

	if (launcher.family_count >= MAXIMUM_FAMILIES)
		return NULL;
	family = &launcher.families[launcher.family_count++];
	memset(family, 0, sizeof(*family));
	family->kind = kind;
	snprintf(family->folder, sizeof(family->folder), "%s", folder);
	snprintf(family->name, sizeof(family->name), "%s", name);
	snprintf(family->tag, sizeof(family->tag), "%s", tag);
	return family;
}

/* every family's folder read again: the four of the families, then each
pack's folder (maps_<pack>) */
static void scan(void)
{
	void *directory;
	char name[256];
	int index;

	launcher.family_count = 0;
	family_add(_family_xbox, "maps", "XBOX MAPS", "XBX");
	family_add(_family_custom_edition, "maps_ce", "CUSTOM EDITION", "HCE");
	family_add(_family_halo_pc, "maps_pc", "HALO PC", "GBX");
	family_add(_family_halomd, "maps_md", "HALOMD", "HMD");
	directory = posix_directory_open(launcher.root);
	while (directory && posix_directory_next(directory, name, sizeof(name)))
	{
		char path[PATH_SIZE + 256];
		const char *id = name + 5;
		const struct xbox_mod_pack *pack;
		struct family *family;
		size_t length = strlen(id);
		size_t character;
		int valid = length >= 1 && length <= 8;

		if (SDL_strncasecmp(name, "maps_", 5))
			continue;
		for (character = 0; valid && character < length; character++)
			valid = isalnum((unsigned char)id[character]) || id[character] == '+';
		if (!valid || !SDL_strcasecmp(id, "ce") || !SDL_strcasecmp(id, "pc") || !SDL_strcasecmp(id, "md"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", launcher.root, name);
		if (!is_directory(path))
			continue;
		pack = known_pack(id);
		if (pack)
		{
			char title[48];

			snprintf(title, sizeof(title), "%s PACK", pack->name);
			upper(title);
			family = family_add(_family_pack, name, title, pack->tag);
			if (family)
				snprintf(family->pack_id, sizeof(family->pack_id), "%s", pack->id);
		}
		else
		{
			char title[48], tag[8];

			snprintf(title, sizeof(title), "%s PACK", id);
			upper(title);
			snprintf(tag, sizeof(tag), "%.4s", id);
			upper(tag);
			family_add(_family_pack, name, title, tag);
		}
	}
	if (directory)
		posix_directory_close(directory);
	for (index = 0; index < launcher.family_count; index++)
		family_scan(&launcher.families[index], 0);
	/* (Custom Edition's folder says so too when only HaloMD's or Halo PC's
	maps need its resource maps) */
	if (!launcher.families[1].good && (launcher.families[2].good || launcher.families[3].good))
		family_check_resources(&launcher.families[1], 0);
	/* (the older folders: never while the data root is still being found) */
	launcher.old_folders = launcher.welcome ? 0 : platform_old_map_folders(NULL, 0);
	modes_find();
}

static int xbox_maps_ready(void)
{
	return launcher.family_count && launcher.families[0].ui_version == MAP_VERSION_XBOX;
}

/* a family's state, in words, and its color */
static void family_state(const struct family *family, char *text, size_t size, unsigned int *color)
{
	*color = COLOR_GREEN;
	if (family->kind == _family_xbox)
	{
		if (family->ui_version == MAP_VERSION_XBOX)
			snprintf(text, size, "Ready");
		else if (family->ui_version == MAP_VERSION_HALO_PC || family->ui_version == MAP_VERSION_CUSTOM_EDITION)
		{
			snprintf(text, size, "Halo PC maps, not Xbox");
			*color = COLOR_AMBER;
		}
		else if (family->good || family->broken || family->stray)
		{
			snprintf(text, size, "No ui.map");
			*color = COLOR_AMBER;
		}
		else
		{
			snprintf(text, size, "Not added yet");
			*color = COLOR_AMBER;
		}
		return;
	}
	if (!family->good && !family->broken && !family->unsupported)
	{
		if (family_needs_resources(family) && family->missing)
			snprintf(text, size, "Resource maps missing");
		else if (family->missing)
			snprintf(text, size, "%d maps missing", family->missing);
		else
			snprintf(text, size, "Not added yet");
		*color = family->missing ? COLOR_AMBER : COLOR_GREY;
		return;
	}
	if (family_needs_resources(family) && family->missing)
	{
		snprintf(text, size, "Resource maps missing");
		*color = COLOR_AMBER;
		return;
	}
	if (family->missing)
	{
		snprintf(text, size, "%d map%s missing", family->missing, family->missing == 1 ? "" : "s");
		*color = COLOR_AMBER;
		return;
	}
	if (family->broken)
	{
		snprintf(text, size, "%d good, %d broken", family->good, family->broken);
		*color = COLOR_AMBER;
		return;
	}
	snprintf(text, size, "%d map%s", family->good, family->good == 1 ? "" : "s");
}

/* the result of a family's check, in words */
static void family_check_text(const struct family *family, char *text, size_t size, unsigned int *color)
{
	*color = COLOR_GREEN;
	if (!family->good && !family->broken && !family->unsupported && !family->stray)
	{
		snprintf(text, size, "No maps yet");
		*color = COLOR_GREY;
	}
	else if (family_needs_resources(family) && family->missing)
	{
		snprintf(text, size, "Resource maps missing");
		*color = COLOR_AMBER;
	}
	else if (family->broken)
	{
		snprintf(text, size, "%d good, %d broken", family->good, family->broken);
		*color = COLOR_AMBER;
	}
	else if (family->stray)
	{
		snprintf(text, size, "%d good, %d Halo PC", family->good, family->stray);
		*color = COLOR_AMBER;
	}
	else
		snprintf(text, size, "%d good", family->good);
}

/* ---------- the ways to play

Read without opening any map but the menus' (this runs at every start):
vanilla, then each pack folder (maps_<pack>) with its own ui.map, then the
packs the library knows that aren't here. */

static struct mode *mode_add(const char *id, const char *name, const char *tag, const char *folder, int installed)
{
	struct mode *mode;
	int index;

	for (index = 0; index < launcher.mode_count; index++)
	{
		if (!SDL_strcasecmp(launcher.modes[index].id, id))
			return NULL;
	}
	if (launcher.mode_count >= MAXIMUM_MODES)
		return NULL;
	mode = &launcher.modes[launcher.mode_count++];
	memset(mode, 0, sizeof(*mode));
	snprintf(mode->id, sizeof(mode->id), "%s", id);
	snprintf(mode->name, sizeof(mode->name), "%s", name);
	upper(mode->name);
	snprintf(mode->title, sizeof(mode->title), "%s", name);
	snprintf(mode->tag, sizeof(mode->tag), "%s", tag);
	snprintf(mode->folder, sizeof(mode->folder), "%s", folder);
	mode->installed = installed;
	if (installed)
		launcher.installed_modes++;
	return mode;
}

static void modes_find(void)
{
	void *directory;
	char name[256], on_disk[256];
	size_t index;

	launcher.mode_count = launcher.installed_modes = 0;
	mode_add("vanilla", "Vanilla Halo", "XBX", "maps", 1);
	directory = posix_directory_open(launcher.root);
	while (directory && posix_directory_next(directory, name, sizeof(name)))
	{
		char path[PATH_SIZE + 256], id[16];
		const char *suffix = name + 5;
		const struct xbox_mod_pack *pack;
		size_t character, length = strlen(suffix);
		int valid = length >= 1 && length <= 8;

		if (SDL_strncasecmp(name, "maps_", 5))
			continue;
		for (character = 0; valid && character < length; character++)
			valid = isalnum((unsigned char)suffix[character]) || suffix[character] == '+';
		if (!valid || !SDL_strcasecmp(suffix, "ce") || !SDL_strcasecmp(suffix, "pc") || !SDL_strcasecmp(suffix, "md"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", launcher.root, name);
		if (!posix_find_entry_case_insensitive(path, "ui.map", on_disk, sizeof(on_disk)))
			continue;
		pack = known_pack(suffix);
		if (pack)
			mode_add(pack->id, pack->name, pack->tag, name, 1);
		/* (Stubbs the Zombie's maps, a preliminary first look:
		port/linux/game/stubbs_mode.c) */
		else if (!SDL_strcasecmp(suffix, "stubbs"))
			mode_add("stubbs", "Stubbs the Zombie", "STUB", name, 1);
		else
		{
			char tag[8];

			snprintf(id, sizeof(id), "%s", suffix);
			for (character = 0; id[character]; character++)
				id[character] = (char)tolower((unsigned char)id[character]);
			snprintf(tag, sizeof(tag), "%.4s", suffix);
			upper(tag);
			mode_add(id, suffix, tag, name, 1);
		}
	}
	if (directory)
		posix_directory_close(directory);
	for (index = 0; index < sizeof(xbox_mod_packs) / sizeof(*xbox_mod_packs); index++)
	{
		char folder[64];

		snprintf(folder, sizeof(folder), "maps_%s", xbox_mod_packs[index].id);
		mode_add(xbox_mod_packs[index].id, xbox_mod_packs[index].name, xbox_mod_packs[index].tag, folder, 0);
	}
}

/* a mode by its id (or a pack's folder name or alias); -1 for none */
static int mode_find(const char *id)
{
	const struct xbox_mod_pack *pack;
	int index;

	if (!id || !*id)
		return -1;
	if (!SDL_strncasecmp(id, "maps_", 5))
		id += 5;
	pack = known_pack(id);
	if (pack)
		id = pack->id;
	for (index = 0; index < launcher.mode_count; index++)
	{
		if (!SDL_strcasecmp(launcher.modes[index].id, id))
			return index;
	}
	return -1;
}

/* the mode the game starts in without asking: launcher.default_mode, else
the pack whose menus game.pack_menus asks for, else vanilla */
static int mode_default(void)
{
	int index = config_chosen("launcher.default_mode") ? mode_find(config_string("launcher.default_mode")) : -1;

	if (index < 0 || !launcher.modes[index].installed)
		index = mode_find(config_string("game.pack_menus"));
	if (index < 0 || !launcher.modes[index].installed)
		index = 0;
	return index;
}

/* whether the player chose to be asked each time (or hasn't chosen yet) */
static int mode_asks(void)
{
	return strcmp(config_string("launcher.mode_choice"), "default") != 0;
}

/* how to come back to this screen once a mode skips it (a pack's menus,
Halo CE+'s, have no way here of their own) */
static const char *reopen_hint(void)
{
	if (config_boolean("launcher.hold_to_open"))
		return "To come back here, hold Shift or a controller's Back as Chupathingy starts.";
	return "To come back here, start Chupathingy with --setup.";
}

/* the mode for this run: its menus (game.pack_menus, for this run only). A
player who never picked one keeps what config.toml says */
static void mode_apply(int index, int picked)
{
	if (index < 0 || index >= launcher.mode_count || !launcher.modes[index].installed)
		return;
	/* (nor one HALO_PACK_MENUS gives for this run) */
	if (!picked && (!config_chosen("launcher.default_mode") || getenv("HALO_PACK_MENUS")))
		return;
	config_set("game.pack_menus", index ? launcher.modes[index].id : "");
	platform_log("launcher: playing %s", index ? launcher.modes[index].id : "vanilla");
}

static void mode_make_default(int index)
{
	config_write("launcher.mode_choice", "default");
	config_write("launcher.default_mode", launcher.modes[index].id);
	/* (chosen for this run too: config_write leaves config_chosen as the
	file said at the start, and mode_default goes by it) */
	config_set("launcher.default_mode", launcher.modes[index].id);
}

/* ---------- the rows of each screen */

static struct row *row_add(int action, const char *label)
{
	struct row *row;

	if (launcher.row_count >= MAXIMUM_ROWS)
		return &launcher.rows[MAXIMUM_ROWS - 1];
	row = &launcher.rows[launcher.row_count++];
	memset(row, 0, sizeof(*row));
	row->action = action;
	row->value_color = COLOR_WHITE;
	row->tag_color = COLOR_TAG_BLUE;
	snprintf(row->label, sizeof(row->label), "%s", label);
	return row;
}

static const char *downloads_word(void)
{
	const char *setting = config_string("network.map_downloads");

	if (!strcmp(setting, "always"))
		return "ALWAYS";
	if (!strcmp(setting, "never"))
		return "NEVER";
	return "ASK FIRST";
}

/* the newest crash report waiting in the logs folder: its time, 0 for none */
static time_t last_crash(void)
{
	char folder[PATH_SIZE + 16], name[256];
	void *directory;
	time_t newest = 0;

	logs_folder(folder, sizeof(folder));
	strncat(folder, "/crashes", sizeof(folder) - strlen(folder) - 1);
	directory = posix_directory_open(folder);
	while (directory && posix_directory_next(directory, name, sizeof(name)))
	{
		char path[PATH_SIZE + 300];
		struct posix_file_information information;

		if (!ends_with(name, ".txt"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, name);
		if (posix_stat(path, &information) == 0 && (time_t)information.modification_seconds > newest)
			newest = (time_t)information.modification_seconds;
	}
	if (directory)
		posix_directory_close(directory);
	return newest;
}

/* ---------- Settings' rows */

/* whether ALL lists a section: debug's only when one of its keys is set,
the console's never on the desktop */
static int section_listed(const char *section)
{
	const char *name;
	int index;

	if (!strcmp(section, "console"))
		return 0;
	if (strcmp(section, "debug"))
		return 1;
	for (index = 0; (name = config_setting_name(index)) != NULL; index++)
	{
		if (!strncmp(name, "debug.", 6) && config_chosen(name))
			return 1;
	}
	return 0;
}

/* ALL: each section with its count, or one section's keys */
static void all_rows(void)
{
	char sections[24][32];
	int counts[24], section_count = 0, index, section;
	const char *name;
	struct row *row;

	for (index = 0; (name = config_setting_name(index)) != NULL; index++)
	{
		const char *dot = strchr(name, '.');
		char section_name[32];

		if (!name[0] || !dot || (size_t)(dot - name) >= sizeof(section_name))
			continue;
		snprintf(section_name, sizeof(section_name), "%.*s", (int)(dot - name), name);
		if (launcher.screen == _screen_setting_keys)
		{
			if (strcmp(section_name, launcher.setting_section))
				continue;
			row = row_add(_action_setting_key, dot + 1);
			snprintf(row->key, sizeof(row->key), "%s", name);
			settings_key_value(name, row->value, sizeof(row->value));
			row->cycles = config_setting_kind(name) != CONFIG_KIND_STRING || config_setting_word(name, 0) != NULL;
			continue;
		}
		for (section = 0; section < section_count && strcmp(sections[section], section_name); section++)
			;
		if (section == section_count && section_count < 24)
		{
			snprintf(sections[section_count], sizeof(sections[0]), "%s", section_name);
			counts[section_count++] = 0;
		}
		if (section < section_count)
			counts[section]++;
	}
	for (section = 0; section < section_count; section++)
	{
		if (!section_listed(sections[section]))
			continue;
		row = row_add(_action_settings_section, sections[section]);
		upper(row->label);
		for (index = 0; row->label[index]; index++)
		{
			if (row->label[index] == '_')
				row->label[index] = ' ';
		}
		snprintf(row->key, sizeof(row->key), "%s", sections[section]);
		snprintf(row->value, sizeof(row->value), "%d", counts[section]);
		row->value_color = COLOR_GREY;
	}
}

/* a tab's rows of settings_rows.h's group */
static void group_rows(int group)
{
	const struct settings_row *setting;
	int index;

	for (index = 0; (setting = settings_row_at(group, index)) != NULL; index++)
	{
		struct row *row = row_add(_action_setting, settings_row_label(setting));

		row->setting = setting;
		row->cycles = 1;
		settings_row_value(setting, row->value, sizeof(row->value));
		if (settings_row_restart(setting))
			snprintf(row->badge, sizeof(row->badge), "NEEDS A RESTART");
	}
}

static void path_row(const char *label, int folder, const char *path)
{
	struct row *row = row_add(_action_open_path, label);

	char whole[PATH_SIZE];
	size_t length;

	row->argument = folder;
	shown_path(path, whole, sizeof(whole));
	/* (a long one: its end, where the folders differ) */
	length = strlen(whole);
	if (length > 72)
		snprintf(row->sub, sizeof(row->sub), "\xE2\x80\xA6%s", whole + length - 70);
	else
		snprintf(row->sub, sizeof(row->sub), "%s", whole);
}

static void settings_rows_build(void)
{
	struct row *row;

	if (launcher.screen == _screen_setting_keys)
	{
		all_rows();
		return;
	}
	switch (launcher.tab)
	{
	case _tab_display:
		group_rows(SETTINGS_GROUP_DISPLAY);
		break;
	case _tab_audio:
		group_rows(SETTINGS_GROUP_AUDIO);
		break;
	case _tab_network:
		group_rows(SETTINGS_GROUP_NETWORK);
		/* (network.map_downloads_from_host's: the disclaimer, the one place
		besides the download's own question where "always" is picked) */
		{
			int index;

			for (index = 0; index < launcher.row_count; index++)
			{
				if (launcher.rows[index].setting &&
					!strcmp(settings_row_key(launcher.rows[index].setting), "network.map_downloads_from_host"))
				{
					snprintf(launcher.rows[index].help, sizeof(launcher.rows[index].help), "%s",
						MAP_HOST_DISCLAIMER_SETTING);
				}
			}
		}
		break;
	case _tab_paths:
	{
		char logs[PATH_SIZE];

		logs_folder(logs, sizeof(logs));
		path_row("GAME DATA", _port_folder_data, launcher.root);
		path_row("SAVES", -1, platform_save_root());
		path_row("LOGS", _port_folder_logs, logs);
		path_row("CACHE", _port_folder_cache, platform_folder(_port_folder_cache));
		break;
	}
	case _tab_updates:
	{
		char latest[32], progress[sizeof(launcher.status)];
		int state = updater_check_state(latest, sizeof(latest));
		/* (an install begun here: how it goes, in the status line, on this
		page, with no window or dialog, so under gamescope too) */
		int installing = updater_install_status(progress, sizeof(progress));

		row = row_add(_action_none, "THIS VERSION");
		snprintf(row->value, sizeof(row->value), "%s", build_identity_version());
		/* (the build's own channel; the one it updates from is CHANNEL,
		update.channel, below) */
		row = row_add(_action_none, "BUILD");
		snprintf(row->value, sizeof(row->value), "%s", updater_channel());
		upper(row->value);
		if (state != UPDATER_NOT_CHECKED)
		{
			row = row_add(_action_none, "NEWEST");
			row->value_color = state == UPDATER_NEWER ? COLOR_GREEN : COLOR_GREY;
			snprintf(row->value, sizeof(row->value), "%s", state == UPDATER_CHECKING ? "Checking\xE2\x80\xA6" :
				state == UPDATER_NEWER ? latest : state == UPDATER_LATEST ? "You have it" : "Couldn't check");
		}
		group_rows(SETTINGS_GROUP_UPDATES);
		/* (update.channel's: what Testing and Nightly mean, and that going
		back never installs an older build) */
		{
			int index;

			for (index = 0; index < launcher.row_count; index++)
			{
				if (launcher.rows[index].setting &&
					!strcmp(settings_row_key(launcher.rows[index].setting), "update.channel"))
				{
					snprintf(launcher.rows[index].help, sizeof(launcher.rows[index].help), "%s",
						UPDATE_CHANNEL_HELP);
				}
			}
		}
		if (installing != UPDATER_INSTALL_IDLE)
		{
			snprintf(launcher.status, sizeof(launcher.status), "%s", progress);
			launcher.status_color = installing == UPDATER_INSTALL_FAILED ? COLOR_AMBER : COLOR_GREEN;
		}
		row = row_add(_action_check_updates, "CHECK NOW");
		row->gap = 1;
		/* (in UPDATE's place while it installs, with no second check or
		install: the selection stays where it was) */
		if (installing == UPDATER_INSTALL_DOWNLOADING || installing == UPDATER_INSTALL_INSTALLING)
		{
			row = row_add(_action_none, "UPDATING");
			row->value_color = COLOR_GREEN;
			snprintf(row->value, sizeof(row->value), "%s", installing == UPDATER_INSTALL_INSTALLING ?
				"Restarting" : updater_install_amount(progress, sizeof(progress)));
		}
		else if (state == UPDATER_NEWER || updater_ready(NULL, 0))
		{
#ifdef __APPLE__
			row_add(_action_install_update, "OPEN THE DOWNLOAD PAGE");
#else
			row_add(_action_install_update, installing == UPDATER_INSTALL_FAILED ? "TRY AGAIN" : "UPDATE");
#endif
		}
		break;
	}
	default:
		all_rows();
		break;
	}
}

static void build_rows(void)
{
	struct row *row;
	int index;

	launcher.row_count = 0;
	switch (launcher.screen)
	{
	case _screen_welcome:
		row_add(_action_add_file, "COPY FROM MY XBOX DISC IMAGE");
		row_add(_action_make_folders, "MAKE THE FOLDERS, I'LL ADD THE FILES");
		row_add(_action_add_halo_pc, "ADD HALO PC OR CUSTOM EDITION TOO");
		row_add(_action_pick_folder, "I ALREADY HAVE A MAPS FOLDER");
		break;
	case _screen_play:
		for (index = 0; index < launcher.mode_count; index++)
		{
			const struct mode *mode = &launcher.modes[index];

			row = row_add(_action_play_mode, mode->name);
			row->argument = index;
			snprintf(row->tag, sizeof(row->tag), "%s", mode->tag);
			row->tag_color = index ? COLOR_TAG_BLUE : COLOR_TAG_GREEN;
			if (!mode->installed)
			{
				snprintf(row->value, sizeof(row->value), "Not installed");
				row->value_color = COLOR_GREY;
				row->tag_color = COLOR_GREY;
				row->dim = 1;
			}
			else if (!xbox_maps_ready())
			{
				snprintf(row->value, sizeof(row->value), "Xbox maps needed");
				row->value_color = COLOR_AMBER;
				snprintf(row->help, sizeof(row->help), "Add your Xbox maps first.");
			}
			else
			{
				snprintf(row->value, sizeof(row->value), "Ready");
				row->value_color = COLOR_GREEN;
			}
		}
		if (launcher.installed_modes > 1)
		{
			row = row_add(_action_start_mode, "START WITH");
			row->cycles = 1;
			row->gap = 1;
			if (mode_asks())
				snprintf(row->value, sizeof(row->value), "ALWAYS ASK");
			else
				snprintf(row->value, sizeof(row->value), "%s", launcher.modes[mode_default()].name);
		}
		row = row_add(_action_play, "PLAY");
		row->gap = launcher.installed_modes <= 1;
		if (launcher.mode >= 0 && launcher.mode < launcher.mode_count && launcher.installed_modes > 1)
			snprintf(row->value, sizeof(row->value), "%s", launcher.modes[launcher.mode].name);
		row->value_color = COLOR_WHITE;
		if (!xbox_maps_ready())
			snprintf(row->help, sizeof(row->help), "Add your Xbox maps first.");
		break;
	case _screen_maps:
		for (index = 0; index < launcher.family_count; index++)
		{
			const struct family *family = &launcher.families[index];

			row = row_add(_action_family, family->name);
			row->argument = index;
			snprintf(row->tag, sizeof(row->tag), "%s", family->tag);
			row->tag_color = family->kind == _family_xbox ? COLOR_TAG_GREEN : COLOR_TAG_BLUE;
			family_state(family, row->value, sizeof(row->value), &row->value_color);
			if (row->value_color == COLOR_GREY)
				row->dim = 1;
		}
		if (launcher.old_folders)
		{
			row = row_add(_action_move_old_folders, "MOVE TO THE NEW FOLDERS");
			row->gap = 1;
		}
		row = row_add(_action_add_file, "ADD MAPS FROM A FILE");
		row->gap = !launcher.old_folders;
		row_add(_action_make_folders, "MAKE THE FOLDERS");
		row = row_add(_action_open_game_folder, "OPEN GAME FOLDER");
		shown_path(launcher.root, row->help, sizeof(row->help));
		break;
	case _screen_family:
	{
		const struct family *family = &launcher.families[launcher.family];

		row_add(_action_open_family_folder, "OPEN FOLDER");
		row = row_add(_action_check_family, "CHECK MY MAPS");
		family_check_text(family, row->value, sizeof(row->value), &row->value_color);
		/* (what is wrong, where something is: the missing files by name) */
		if ((family->kind == _family_custom_edition || family_needs_resources(family) ||
			family->kind == _family_pack) && family->missing)
		{
			snprintf(row->help, sizeof(row->help), "Missing: %s", family->missing_names);
		}
		else if (family->stray)
			snprintf(row->help, sizeof(row->help), "Halo PC maps go in maps_ce, maps_pc or maps_md.");
		switch (family->kind)
		{
		case _family_xbox:
			row_add(_action_add_file, "ADD FROM MY DISC IMAGE");
			break;
		case _family_custom_edition:
			row_add(_action_add_halo_pc, "ADD THE CE INSTALLER");
			row = row_add(_action_downloads, "DOWNLOADS");
			row->cycles = 1;
			snprintf(row->value, sizeof(row->value), "%s", downloads_word());
			break;
		case _family_halo_pc:
			row_add(_action_add_halo_pc, "ADD FROM MY CD IMAGE");
			break;
		default:
			break;
		}
		break;
	}
	case _screen_map_list:
		for (index = 0; index < launcher.map_entry_count && launcher.row_count < MAXIMUM_ROWS; index++)
		{
			row = row_add(_action_none, launcher.map_entries[index].name);
			snprintf(row->value, sizeof(row->value), "%s", launcher.map_entries[index].state);
			row->value_color = launcher.map_entries[index].color;
		}
		if (index < launcher.map_entry_count)
		{
			snprintf(launcher.rows[MAXIMUM_ROWS - 1].label, sizeof(launcher.rows[0].label), "AND %d MORE",
				launcher.map_entry_count - MAXIMUM_ROWS + 1);
			launcher.rows[MAXIMUM_ROWS - 1].value[0] = '\0';
		}
		break;
	case _screen_settings:
	case _screen_setting_keys:
		settings_rows_build();
		break;
	case _screen_logs:
	{
		time_t crash = last_crash();

		row = row_add(_action_send_logs, "SEND MY LOGS");
		snprintf(row->value, sizeof(row->value), "%s", LAUNCHER_DUMP_SITE);
		row->value_color = COLOR_GREY;
		row_add(_action_open_logs, "OPEN LOGS FOLDER");
		if (crash)
		{
			struct tm when;
			time_t now = time(NULL);
			struct tm today;

			row = row_add(_action_open_crashes, "LAST CRASH");
			when = *localtime(&crash);
			today = *localtime(&now);
			if (when.tm_year == today.tm_year && when.tm_yday == today.tm_yday)
				strftime(row->value, sizeof(row->value), "Today %H:%M", &when);
			else
				strftime(row->value, sizeof(row->value), "%b %d, %H:%M", &when);
			row->value_color = COLOR_AMBER;
		}
		row_add(_action_copy_version, "COPY VERSION INFO");
		row_add(_action_discord, "GET HELP ON DISCORD");
		break;
	}
	}
	if (launcher.selected >= launcher.row_count)
		launcher.selected = launcher.row_count ? launcher.row_count - 1 : 0;
}

/* ---------- drawing */

static float layout_width(void)
{
	return launcher.right - launcher.left;
}

/* a string at size, made smaller (to 70%) to fit width, then cut */
static float text_fit(int font, float size, float x, float y, int align, unsigned int color, unsigned int glow,
	float width, const char *text)
{
	char cut[256];
	float fitted = size;
	size_t length;

	while (fitted > size * 0.7f && ui_overlay_text_width(font, fitted, text) > width)
		fitted -= size * 0.05f;
	snprintf(cut, sizeof(cut), "%s", text);
	length = strlen(cut);
	while (length > 1 && ui_overlay_text_width(font, fitted, cut) > width)
	{
		/* (whole UTF-8 characters off the end, then the ellipsis) */
		length--;
		while (length > 1 && ((unsigned char)cut[length] & 0xC0) == 0x80)
			length--;
		snprintf(cut + length, sizeof(cut) - length, "\xE2\x80\xA6");
		if (ui_overlay_text_width(font, fitted, cut) <= width)
			break;
		cut[length] = '\0';
	}
	/* (a little lower when made smaller, so it stays centered on its line) */
	y += (size - fitted) * 0.5f;
	if (glow)
		return ui_overlay_text_glow(font, fitted, x, y, align, color, glow, cut);
	return ui_overlay_text(font, fitted, x, y, align, color, cut);
}

/* text wrapped to width (a path breaks after its separators): up to
maximum lines, line_height apart, the last one cut short; how many */
static int text_wrapped(int font, float size, float x, float y, float width, float line_height, int maximum,
	unsigned int color, const char *text)
{
	char line[512];
	const char *cursor = text;
	int lines = 0;

	while (*cursor && lines < maximum)
	{
		size_t taken = 0, scanned = 0;

		while (*cursor == ' ')
			cursor++;
		if (!*cursor)
			break;
		for (;;)
		{
			size_t next = scanned;

			while (cursor[next] == ' ')
				next++;
			while (cursor[next] && cursor[next] != ' ' && cursor[next] != '/' && cursor[next] != '\\')
				next++;
			if (cursor[next] == '/' || cursor[next] == '\\')
				next++;
			if (next == scanned || next >= sizeof(line))
				break;
			memcpy(line, cursor, next);
			line[next] = '\0';
			if (taken && ui_overlay_text_width(font, size, line) > width)
				break;
			taken = scanned = next;
			if (!cursor[next])
				break;
		}
		if (!taken)
			taken = strlen(cursor) < sizeof(line) - 1 ? strlen(cursor) : sizeof(line) - 1;
		if (lines == maximum - 1 && cursor[taken])
			text_fit(font, size, x, y + (float)lines * line_height, UI_ALIGN_LEFT, color, 0, width, cursor);
		else
		{
			memcpy(line, cursor, taken);
			line[taken] = '\0';
			text_fit(font, size, x, y + (float)lines * line_height, UI_ALIGN_LEFT, color, 0, width, line);
		}
		cursor += taken;
		lines++;
	}
	return lines;
}

static void target_add(float x0, float y0, float x1, float y1, int kind, int index)
{
	if (launcher.target_count >= MAXIMUM_TARGETS)
		return;
	launcher.targets[launcher.target_count].x0 = x0;
	launcher.targets[launcher.target_count].y0 = y0;
	launcher.targets[launcher.target_count].x1 = x1;
	launcher.targets[launcher.target_count].y1 = y1;
	launcher.targets[launcher.target_count].kind = kind;
	launcher.targets[launcher.target_count].index = index;
	launcher.target_count++;
}

/* the backdrop: dark space and stars (the plasma is the selection's: the
overlay's own noise, as no map is loaded yet) */
static void draw_backdrop(void)
{
	float left = launcher.left, width = layout_width();
	unsigned int seed = 7;
	int index;

	ui_overlay_gradient(left, 0, width, 265, 0, ui_style_color(UI_COLOR_BACKDROP_TOP),
		ui_style_color(UI_COLOR_BACKDROP_MIDDLE));
	ui_overlay_gradient(left, 265, width, 215, 0, ui_style_color(UI_COLOR_BACKDROP_MIDDLE),
		ui_style_color(UI_COLOR_BACKDROP_BOTTOM));
	for (index = 0; index < 160; index++)
	{
		float x, y, size, alpha;

		seed = seed * 1103515245u + 12345u;
		x = (float)(seed >> 8 & 0xFFFF) / 65535.0f;
		seed = seed * 1103515245u + 12345u;
		y = (float)(seed >> 8 & 0xFFFF) / 65535.0f;
		seed = seed * 1103515245u + 12345u;
		size = 0.35f + (float)(seed >> 8 & 0xFF) / 255.0f * 0.8f;
		seed = seed * 1103515245u + 12345u;
		alpha = 0.15f + (float)(seed >> 8 & 0xFF) / 255.0f * 0.55f;
		ui_overlay_rect(left + x * width, y * 480.0f, size, size, size * 0.5f,
			with_alpha(ui_style_color(UI_COLOR_STAR), (unsigned int)(alpha * 255.0f)));
	}
}

/* a glowing rule across the screen, as the Guide's mockups have under the
title and over the prompts */
static void draw_rule(float y)
{
	ui_overlay_rect(launcher.left, y - 2.0f, layout_width(), 5.0f, 0, COLOR_RULE_GLOW);
	ui_overlay_rect(launcher.left, y - 0.5f, layout_width(), 1.5f, 0, COLOR_RULE);
}

static const char *screen_title(void)
{
	static char title[64];

	switch (launcher.screen)
	{
	case _screen_welcome:
		return "WELCOME";
	case _screen_maps:
		return "MAPS";
	case _screen_family:
	case _screen_map_list:
		snprintf(title, sizeof(title), "%s", launcher.families[launcher.family].name);
		return title;
	case _screen_logs:
		return "LOGS & HELP";
	case _screen_settings:
	case _screen_setting_keys:
		return "SETTINGS";
	case _screen_about:
		return "ABOUT";
	default:
		return "CHUPATHINGY";
	}
}

static void draw_header(void)
{
	float width = layout_width();
	float corner = launcher.right - width * 0.05f;
	char line[96];

	text_fit(UI_FONT_TITLE, 15.5f, launcher.left + width * 0.086f, 25.0f, UI_ALIGN_LEFT, COLOR_TITLE, COLOR_TITLE_GLOW,
		width * 0.62f, screen_title());
	snprintf(line, sizeof(line), "Chupathingy %s", build_identity_version());
	ui_overlay_text(UI_FONT_BOLD, 10.0f, corner, 27.0f, UI_ALIGN_RIGHT, COLOR_WHITE, line);
	snprintf(line, sizeof(line), "%s %s", build_identity_platform(), sizeof(void *) == 8 ? "64-bit" : "32-bit");
	ui_overlay_text(UI_FONT_BOLD, 8.7f, corner, 41.0f, UI_ALIGN_RIGHT, COLOR_FAINT, line);
	draw_rule(75.0f);
}

/* the sections, Xbox main-menu rows: the one open lit while it has the
focus, its capsule kept (unlit) while the panel has it */
static void draw_sections(void)
{
	float width = layout_width();
	float x = launcher.left + width * 0.075f;
	float column = width * 0.234f;
	float y = 93.0f;
	int index;

	for (index = 0; index < NUMBER_OF_SECTIONS; index++)
	{
		int picked = index == launcher.section;
		int lit = picked && !launcher.in_panel;

		if (index == _section_quit)
		{
			ui_overlay_rect(x + 9.0f, y + 6.0f, column - 18.0f, 1.3f, 0, COLOR_SEPARATOR);
			y += 13.0f;
		}
		if (picked)
			ui_style_row(x, y, column, 29.0f, lit);
		text_fit(UI_FONT_MENU, 16.0f, x + 9.5f, y + 6.5f, UI_ALIGN_LEFT, picked ? COLOR_WHITE : COLOR_NAV,
			lit ? COLOR_SELECTED_GLOW : COLOR_NAV_GLOW, column - 18.0f, section_names[index]);
		target_add(x, y, x + column, y + 29.0f, _target_section, index);
		y += 33.0f;
	}
}

/* a triangle of rects (left or right), height high, centered on y */
static void draw_arrow(float x, float y, int left, unsigned int color)
{
	int slice;

	for (slice = 0; slice < 5; slice++)
	{
		float height = 9.0f - (float)slice * 2.0f;
		float column = left ? x + 4.0f - (float)slice : x + (float)slice;

		ui_overlay_rect(column, y - height * 0.5f, 1.05f, height, 0, color);
	}
}

/* the same, up or down: more rows that way */
static void draw_arrow_vertical(float x, float y, int up, unsigned int color)
{
	int slice;

	for (slice = 0; slice < 4; slice++)
	{
		float width = 7.0f - (float)slice * 2.0f;
		float row = up ? y + 3.0f - (float)slice : y + (float)slice;

		ui_overlay_rect(x - width * 0.5f, row, width, 1.05f, 0, color);
	}
}

/* a row's height */
static float row_height(const struct row *row)
{
	return row->sub[0] ? 35.0f : 32.0f;
}

/* a row: its capsule (ui_style_row), its tag, its label (and the line under
it), its value; its height */
static float draw_row(const struct row *row, int index, float x, float y, float width, int picked, int lit)
{
	float height = row_height(row);
	float label_x = x + 11.0f;
	float value_width = 0.0f;
	unsigned int label_color = row->disabled || row->dim ? COLOR_ROW_DIM : picked && lit ? COLOR_WHITE : COLOR_ROW;

	/* (a setting: the row the Guide's SETTINGS draws too) */
	if ((row->setting || row->key[0]) && row->cycles)
	{
		ui_style_setting(x, y, width, height, 1.3f, row->label, row->badge, row->value, picked && lit);
		target_add(x, y, x + width, y + height, _target_row, index);
		return height;
	}
	ui_style_row(x, y, width, height, picked && lit);
	if (row->tag[0])
	{
		float tag_y = y + (height - 12.0f) * 0.5f;

		ui_overlay_outline(label_x, tag_y, 29.0f, 12.0f, 3.0f, 0.8f, with_alpha(row->tag_color, 0x99));
		text_fit(UI_FONT_MENU, 8.5f, label_x + 14.5f, tag_y + 1.5f, UI_ALIGN_CENTER, row->tag_color, 0, 26.0f,
			row->tag);
		label_x += 38.0f;
	}
	if (row->cycles)
	{
		float right = x + width - 11.0f;
		float center = right - 62.0f;

		draw_arrow(right - 5.0f, y + height * 0.5f, 0, COLOR_NAV);
		draw_arrow(right - 124.0f, y + height * 0.5f, 1, COLOR_NAV);
		text_fit(UI_FONT_MENU, 12.5f, center, y + (height - 12.5f) * 0.5f, UI_ALIGN_CENTER, COLOR_WHITE, 0, 104.0f,
			row->value);
		value_width = 130.0f;
	}
	else if (row->value[0])
	{
		float maximum = width * 0.5f;

		value_width = ui_overlay_text_width(UI_FONT_MENU, 12.5f, row->value);
		if (value_width > maximum)
			value_width = maximum;
		text_fit(UI_FONT_MENU, 12.5f, x + width - 11.0f, y + (height - 12.5f) * 0.5f, UI_ALIGN_RIGHT,
			row->value_color, 0, maximum, row->value);
		value_width += 10.0f;
	}
	if (row->sub[0])
	{
		text_fit(UI_FONT_MENU, 15.0f, label_x, y + 3.0f, UI_ALIGN_LEFT, label_color, picked && lit ? COLOR_ROW_GLOW : 0,
			x + width - 11.0f - value_width - label_x, row->label);
		text_fit(UI_FONT_BOLD, 9.0f, label_x, y + 21.5f, UI_ALIGN_LEFT, COLOR_GREY, 0,
			x + width - 11.0f - label_x, row->sub);
	}
	else
	{
		text_fit(UI_FONT_MENU, 15.0f, label_x, y + (height - 15.0f) * 0.5f + 0.5f, UI_ALIGN_LEFT, label_color,
			picked && lit ? COLOR_ROW_GLOW : 0, x + width - 11.0f - value_width - label_x, row->label);
	}
	target_add(x, y, x + width, y + height, _target_row, index);
	return height;
}

/* the panel (ui_style_panel): its heading, what goes above its rows, the
rows (scrolled to the selected one), then the status or the row's line */
static void draw_panel(float x0, float y0, float x1, float y1)
{
	float width = x1 - x0;
	float inner = width - 30.0f;
	float x = x0 + 15.0f;
	float y = y0 + 13.0f;
	const char *heading = "";
	const char *line = "";
	char text[600];
	float bottom, rows_top;
	int index, first, last, lines;
	int lit = launcher.in_panel || launcher.screen == _screen_welcome;

	ui_style_panel(x0, y0, width, y1 - y0, 1.0f);
	switch (launcher.screen)
	{
	case _screen_welcome:
		heading = "ADD YOUR MAPS";
		break;
	case _screen_play:
		heading = "CHOOSE HOW TO PLAY";
		break;
	case _screen_maps:
		heading = "YOUR MAPS";
		break;
	case _screen_family:
		heading = "FOLDER";
		break;
	case _screen_map_list:
		heading = "MAPS";
		break;
	case _screen_logs:
		heading = "HELP";
		break;
	}
	if (launcher.screen == _screen_about)
	{
		/* (about.c's page, its headings its own) */
		about_render(&launcher.about, x + 1.0f, y, x + inner, y1 - 14.0f);
		if (launcher.about.licenses_x1 > launcher.about.licenses_x0)
		{
			target_add(launcher.about.licenses_x0, launcher.about.licenses_y0, launcher.about.licenses_x1,
				launcher.about.licenses_y1, _target_button, _button_a);
		}
		return;
	}
	if (launcher.screen == _screen_settings || launcher.screen == _screen_setting_keys)
	{
		float starts[NUMBER_OF_TABS], ends[NUMBER_OF_TABS];

		ui_style_tabs(x, x + inner, y - 3.0f, 20.0f, tab_names, NUMBER_OF_TABS, launcher.tab, "Q", "C", starts, ends);
		for (index = 0; index < NUMBER_OF_TABS; index++)
			target_add(starts[index], y - 3.0f, ends[index], y + 17.0f, _target_tab, index);
		y += 25.0f;
		if (launcher.screen == _screen_setting_keys)
		{
			char section[40];

			snprintf(section, sizeof(section), "%s", launcher.setting_section);
			upper(section);
			ui_style_text(UI_TEXT_HEADING, x + 1.0f, y + 1.0f, UI_ALIGN_LEFT, section);
			y += 18.0f;
		}
	}
	else
	{
		ui_style_text(UI_TEXT_HEADING, x + 1.0f, y + 1.0f, UI_ALIGN_LEFT, heading);
		y += 22.0f;
	}
	if (launcher.screen == _screen_family)
	{
		char folder[PATH_SIZE], shown[PATH_SIZE];
		int path_lines;

		root_path(launcher.families[launcher.family].folder, folder, sizeof(folder));
		shown_path(folder, shown, sizeof(shown));
		path_lines = ui_overlay_text_width(UI_FONT_BOLD, 10.0f, shown) > inner - 16.0f ? 2 : 1;
		ui_overlay_rect(x, y, inner, 8.0f + 13.0f * (float)path_lines, 6.0f, COLOR_PATH_FIELD);
		ui_overlay_outline(x, y, inner, 8.0f + 13.0f * (float)path_lines, 6.0f, 0.75f, COLOR_EDGE);
		text_wrapped(UI_FONT_BOLD, 10.0f, x + 8.0f, y + 4.5f, inner - 16.0f, 13.0f, 2, COLOR_WHITE, shown);
		y += 14.0f + 13.0f * (float)path_lines;
	}
	rows_top = y;
	/* (room for the line under the rows, and Logs' version line) */
	bottom = y1 - 12.0f - 22.0f - (launcher.screen == _screen_logs ? 18.0f : 0.0f) -
		(launcher.screen == _screen_welcome ? 16.0f : 0.0f);
	/* the rows that fit, from the first shown; the selected one in them */
	if (launcher.selected < launcher.scroll)
		launcher.scroll = launcher.selected;
	for (;;)
	{
		float end = rows_top;

		for (index = launcher.scroll; index < launcher.row_count; index++)
		{
			end += row_height(&launcher.rows[index]) + 5.0f +
				(launcher.rows[index].gap && index > launcher.scroll ? 7.0f : 0.0f);
			if (end > bottom + 5.0f)
				break;
		}
		if (launcher.selected < index || launcher.scroll >= launcher.selected)
			break;
		launcher.scroll++;
	}
	first = launcher.scroll;
	last = index;
	for (index = first; index < last; index++)
	{
		if (launcher.rows[index].gap && index > first)
			y += 7.0f;
		y += draw_row(&launcher.rows[index], index, x, y, inner, index == launcher.selected, lit) + 5.0f;
	}
	/* (more above or below: a small arrow) */
	if (first > 0)
		draw_arrow_vertical(x1 - 20.0f, rows_top - 8.0f, 1, COLOR_GREY);
	if (last < launcher.row_count)
		draw_arrow_vertical(x1 - 20.0f, y - 1.0f, 0, COLOR_GREY);
	y += 4.0f;
	if (launcher.screen == _screen_logs)
	{
		float kx = x + 2.0f;

		snprintf(text, sizeof(text), "%s", build_identity_version());
		kx += ui_overlay_text(UI_FONT_MENU, 11.5f, kx, y, UI_ALIGN_LEFT, COLOR_NAV, "Version: ");
		kx += ui_overlay_text(UI_FONT_MENU, 11.5f, kx, y, UI_ALIGN_LEFT, COLOR_WHITE, text) + 9.0f;
		snprintf(text, sizeof(text), "%s %s", build_identity_platform(), sizeof(void *) == 8 ? "64-bit" : "32-bit");
		kx += ui_overlay_text(UI_FONT_MENU, 11.5f, kx, y, UI_ALIGN_LEFT, COLOR_NAV, "Build: ");
		kx += ui_overlay_text(UI_FONT_MENU, 11.5f, kx, y, UI_ALIGN_LEFT, COLOR_WHITE, text) + 9.0f;
		snprintf(text, sizeof(text), "%d", delta_legacy_announce());
		kx += ui_overlay_text(UI_FONT_MENU, 11.5f, kx, y, UI_ALIGN_LEFT, COLOR_NAV, "Network: ");
		ui_overlay_text(UI_FONT_MENU, 11.5f, kx, y, UI_ALIGN_LEFT, COLOR_WHITE, text);
		y += 18.0f;
	}
	/* the last action's result, else the selected row's line (a value: a
	path, what is missing), else what the screen's state is */
	if (launcher.status[0])
	{
		lines = text_wrapped(UI_FONT_MENU, 12.0f, x + 2.0f, y, inner, 14.5f, 3, launcher.status_color, launcher.status);
		y += 14.5f * (float)lines;
	}
	else
	{
		unsigned int color = COLOR_WHITE;

		if (launcher.row_count && launcher.rows[launcher.selected].help[0] && lit)
			line = launcher.rows[launcher.selected].help;
		else if (launcher.screen == _screen_map_list && !launcher.row_count)
			line = "No maps here yet.";
		else if (!launcher.in_panel && launcher.screen == _screen_play && !xbox_maps_ready())
		{
			line = "Add your Xbox maps first.";
			color = COLOR_AMBER;
		}
		if (line[0])
		{
			lines = text_wrapped(UI_FONT_MENU, 12.0f, x + 2.0f, y, inner, 14.5f, 3, color, line);
			y += 14.5f * (float)lines;
		}
	}
	if (launcher.screen == _screen_welcome)
	{
		char shown[PATH_SIZE];

		shown_path(launcher.root, shown, sizeof(shown));
		text_wrapped(UI_FONT_BOLD, 9.5f, x + 2.0f, y1 - 12.0f - 26.0f, inner, 13.0f, 2, COLOR_GREY, shown);
	}
}

/* the prompt bar's prompts (ui_style_prompts), from the right: each a
button's glyph, then =WORD; smaller when the screen is too narrow for them */
static struct ui_style_prompt prompts[6];
static int prompt_targets[6];
static char prompt_words[6][32];
static int prompt_count;

static void prompt_add(int button, const char *word, int enabled, int target)
{
	if (!enabled || prompt_count >= (int)(sizeof(prompts) / sizeof(*prompts)))
		return;
	snprintf(prompt_words[prompt_count], sizeof(prompt_words[prompt_count]), "=%s", word);
	prompts[prompt_count].button = button;
	prompts[prompt_count].key = NULL;
	prompts[prompt_count].word = prompt_words[prompt_count];
	prompt_targets[prompt_count] = target;
	prompt_count++;
}

static void prompts_draw(void)
{
	float right = launcher.right - layout_width() * 0.078f;
	float room = right - (launcher.left + layout_width() * 0.05f);
	float size = 20.0f, total = 0.0f, starts[6], ends[6];
	struct ui_style_prompt ordered[6];
	int index;

	/* (gathered first-to-the-right: drawn left to right) */
	for (index = 0; index < prompt_count; index++)
		ordered[index] = prompts[prompt_count - 1 - index];
	for (index = 0; index < prompt_count; index++)
	{
		total += ui_overlay_button_width(ordered[index].button, size) + 4.0f +
			ui_overlay_text_width(UI_FONT_MENU, size * 0.72f, ordered[index].word) + (index ? size : 0.0f);
	}
	if (total > room && total > 0.0f)
		size *= room / total;
	draw_rule(419.0f);
	ui_style_prompts(right, 441.0f - size * 0.5f, size, ordered, prompt_count, starts, ends);
	for (index = 0; index < prompt_count; index++)
	{
		target_add(starts[index] - 4.0f, 441.0f - size * 0.5f - 6.0f, ends[index] + 4.0f, 441.0f + size * 0.5f + 6.0f,
			_target_button, prompt_targets[prompt_count - 1 - index]);
	}
	prompt_count = 0;
}

static void draw_prompts(void)
{
	const struct row *row = launcher.row_count ? &launcher.rows[launcher.selected] : NULL;

	if (launcher.dialog == _dialog_sent)
	{
		prompt_add(UI_BUTTON_A, "DONE", 1, _button_a);
		prompt_add(UI_BUTTON_Y, "COPY CODE", 1, _button_y);
		return;
	}
	if (launcher.dialog == _dialog_send_ask)
	{
		prompt_add(UI_BUTTON_A, "SEND", 1, _button_a);
		prompt_add(UI_BUTTON_B, "CANCEL", 1, _button_b);
		return;
	}
	if (launcher.dialog == _dialog_mode_question)
	{
		prompt_add(UI_BUTTON_A, "SELECT", 1, _button_a);
		prompt_add(UI_BUTTON_B, "LATER", 1, _button_b);
		return;
	}
	if (launcher.dialog == _dialog_message)
	{
		prompt_add(UI_BUTTON_A, "OK", 1, _button_a);
		return;
	}
	if (launcher.dialog == _dialog_picking)
	{
		prompt_add(UI_BUTTON_B, "CANCEL", 1, _button_b);
		return;
	}
	if (launcher.dialog)
		return;
	switch (launcher.screen)
	{
	case _screen_welcome:
		prompt_add(UI_BUTTON_A, "SELECT", 1, _button_a);
		prompt_add(UI_BUTTON_B, "QUIT", 1, _button_b);
		prompt_add(UI_BUTTON_X, "CHECK AGAIN", 1, _button_x);
		prompt_add(UI_BUTTON_Y, "OPEN FOLDER", 1, _button_y);
		return;
	case _screen_map_list:
		prompt_add(UI_BUTTON_B, "BACK", 1, _button_b);
		return;
	default:
		break;
	}
	if (!launcher.in_panel)
	{
		prompt_add(UI_BUTTON_A, launcher.section == _section_quit ? "QUIT" : "SELECT", 1, _button_a);
		return;
	}
	if (launcher.screen == _screen_about)
	{
		if (about_a_word(&launcher.about))
			prompt_add(UI_BUTTON_A, about_a_word(&launcher.about), 1, _button_a);
		prompt_add(UI_BUTTON_B, "BACK", 1, _button_b);
		return;
	}
	prompt_add(UI_BUTTON_A, launcher.screen == _screen_play ? "PLAY" :
		launcher.screen == _screen_logs && row && row->action == _action_send_logs ? "SEND" :
		row && row->action == _action_open_path ? "OPEN" : row && row->cycles ? "CHANGE" : "SELECT",
		row && row->action != _action_none, _button_a);
	prompt_add(UI_BUTTON_B, "BACK", 1, _button_b);
	if (launcher.screen == _screen_play && row && row->action == _action_play_mode &&
		launcher.modes[row->argument].installed && launcher.installed_modes > 1)
	{
		prompt_add(UI_BUTTON_X, "MAKE DEFAULT", 1, _button_x);
	}
	if ((launcher.screen == _screen_settings || launcher.screen == _screen_setting_keys) && row)
	{
		if (row->setting || (row->key[0] && row->action == _action_setting_key))
			prompt_add(UI_BUTTON_Y, "DEFAULT", 1, _button_y);
		else if (row->action == _action_open_path && row->argument == _port_folder_data)
			prompt_add(UI_BUTTON_Y, "CHANGE", 1, _button_y);
	}
	if (launcher.screen == _screen_maps)
	{
		prompt_add(UI_BUTTON_X, "CHECK AGAIN", 1, _button_x);
		if (row && row->action == _action_family)
			prompt_add(UI_BUTTON_Y, "OPEN FOLDER", 1, _button_y);
	}
	else if (launcher.screen == _screen_family)
		prompt_add(UI_BUTTON_X, "SHOW MAPS", 1, _button_x);
}

static void draw_dialog(void)
{
	float width = layout_width();
	float dialog_width = width - 60.0f < 430.0f ? width - 60.0f : 430.0f;
	float x0 = 320.0f - dialog_width * 0.5f;
	float x = x0 + 22.0f, inner = dialog_width - 44.0f, y;
	float y0, height;
	int index;

	if (!launcher.dialog)
		return;
	/* (its height: the title, then what it holds) */
	switch (launcher.dialog)
	{
	case _dialog_import:
		height = 46.0f + 72.0f;
		break;
	case _dialog_sent:
		height = 46.0f + 104.0f;
		break;
	case _dialog_picking:
	case _dialog_sending:
		height = 46.0f + 50.0f;
		break;
	default:
		height = 46.0f + 8.0f + (launcher.dialog_text[0] ? 15.0f * (float)(ui_overlay_text_width(UI_FONT_MENU, 12.0f,
			launcher.dialog_text) / inner + 1.0f) : 0.0f) + 33.0f * (float)launcher.dialog_row_count + 10.0f;
		break;
	}
	y0 = 240.0f - height * 0.5f - 10.0f;
	ui_overlay_layer(2);
	ui_overlay_rect(launcher.left, 0, width, 480.0f, 0, COLOR_SHADE);
	ui_style_panel(x0, y0, dialog_width, height, 1.0f);
	text_fit(UI_FONT_TITLE, 11.5f, x, y0 + 18.0f, UI_ALIGN_LEFT, COLOR_TITLE, COLOR_TITLE_GLOW, inner,
		launcher.dialog_title);
	y = y0 + 46.0f;
	switch (launcher.dialog)
	{
	case _dialog_picking:
		text_wrapped(UI_FONT_MENU, 12.0f, x, y, inner, 15.0f, 3, COLOR_WHITE, launcher.dialog_text);
		break;
	case _dialog_import:
	{
		char line[320];
		float fraction = launcher.import_total ? (float)((double)launcher.import_done /
			(double)launcher.import_total) : 0.0f;

		text_fit(UI_FONT_BOLD, 11.0f, x, y, UI_ALIGN_LEFT, COLOR_WHITE, 0, inner,
			launcher.import_file[0] ? launcher.import_file : "Reading the file...");
		y += 26.0f;
		ui_overlay_rect(x, y, inner, 12.0f, 6.0f, COLOR_PATH_FIELD);
		ui_overlay_outline(x, y, inner, 12.0f, 6.0f, 0.75f, COLOR_EDGE);
		if (fraction > 0.0f)
			ui_overlay_gradient(x, y, inner * (fraction > 1.0f ? 1.0f : fraction), 12.0f, 6.0f,
				ui_style_color(UI_COLOR_PROGRESS_TOP), ui_style_color(UI_COLOR_PROGRESS_BOTTOM));
		y += 22.0f;
		if (launcher.import_total)
		{
			snprintf(line, sizeof(line), "%llu of %llu MB", launcher.import_done >> 20, launcher.import_total >> 20);
			ui_overlay_text(UI_FONT_BOLD, 10.0f, x, y, UI_ALIGN_LEFT, COLOR_GREY, line);
		}
		break;
	}
	case _dialog_sending:
		ui_overlay_text(UI_FONT_MENU, 12.0f, x, y, UI_ALIGN_LEFT, COLOR_WHITE, "Sending\xE2\x80\xA6");
		break;
	case _dialog_sent:
		text_wrapped(UI_FONT_MENU, 12.0f, x, y, inner, 15.0f, 2, COLOR_WHITE, launcher.dialog_text);
		ui_style_row(x, y + 36.0f, inner, 46.0f, 1);
		ui_overlay_text_glow(UI_FONT_MENU, 28.0f, x + inner * 0.5f, y + 45.0f, UI_ALIGN_CENTER, COLOR_WHITE,
			COLOR_PROMPT_GLOW, launcher.sent_code);
		break;
	default:
		if (launcher.dialog_text[0])
		{
			y += 15.0f * (float)text_wrapped(UI_FONT_MENU, 12.0f, x, y, inner, 15.0f, 4, launcher.dialog_text_color,
				launcher.dialog_text) + 8.0f;
		}
		for (index = 0; index < launcher.dialog_row_count; index++)
		{
			const struct row *row = &launcher.dialog_rows[index];
			float row_y = y + (float)index * 33.0f;
			int picked = index == launcher.dialog_selected;

			ui_style_row(x, row_y, inner, 28.0f, picked);
			text_fit(UI_FONT_MENU, 14.0f, x + 11.0f, row_y + 7.0f, UI_ALIGN_LEFT, picked ? COLOR_WHITE : COLOR_ROW,
				picked ? COLOR_ROW_GLOW : 0, inner - 22.0f, row->label);
			target_add(x, row_y, x + inner, row_y + 28.0f, _target_dialog_row, index);
		}
		break;
	}
	ui_overlay_layer(0);
}

static void draw(void)
{
	float width = layout_width();

	launcher.target_count = 0;
	ui_style_clock((unsigned int)SDL_GetTicks());
	ui_style_theme(ui_style_theme_named(config_string("display.theme")));
	build_rows();
	draw_backdrop();
	draw_header();
	if (launcher.screen == _screen_welcome)
		draw_panel(launcher.left + width * 0.117f, 93.0f, launcher.right - width * 0.117f, 393.0f);
	else
	{
		draw_sections();
		draw_panel(launcher.left + width * 0.343f, 93.0f, launcher.right - width * 0.078f, 393.0f);
	}
	draw_prompts();
	draw_dialog();
	/* (the prompts over a dialog's shade) */
	ui_overlay_layer(launcher.dialog ? 3 : 0);
	prompts_draw();
	ui_overlay_layer(0);
}

/* ---------- acting */

static void open_folder(const char *path)
{
	static const char hex[] = "0123456789ABCDEF";
	char url[PATH_SIZE * 3 + 16];
	size_t length;
	const unsigned char *cursor;

	make_directories(path);
	/* (under gamescope, the Steam Deck's Game Mode, there is no file manager
	to show it, and the system's dialogs can stop the game: Desktop Mode
	has it) */
	if (updater_gamescope())
	{
		char shown[PATH_SIZE];

		shown_path(path, shown, sizeof(shown));
		set_status(COLOR_AMBER, "Open this folder in Desktop Mode. It's at %s", shown);
		return;
	}
	length = (size_t)snprintf(url, sizeof(url), "file://%s", path[0] == '/' ? "" : "/");
	for (cursor = (const unsigned char *)path; *cursor && length + 4 < sizeof(url); cursor++)
	{
		unsigned char character = *cursor == '\\' ? '/' : *cursor;

		if (isalnum(character) || strchr("/-_.~:", character))
			url[length++] = (char)character;
		else
		{
			url[length++] = '%';
			url[length++] = hex[character >> 4];
			url[length++] = hex[character & 15];
		}
	}
	url[length] = '\0';
	if (SDL_OpenURL(url))
	{
		platform_log("launcher: opened a folder");
		return;
	}
	{
		char shown[PATH_SIZE];

		platform_log("launcher: could not open a folder: %s", SDL_GetError());
		shown_path(path, shown, sizeof(shown));
		set_status(COLOR_AMBER, "We couldn't open it. It's at %s", shown);
	}
}

static void message(const char *title, unsigned int color, const char *text)
{
	launcher.dialog = _dialog_message;
	snprintf(launcher.dialog_title, sizeof(launcher.dialog_title), "%s", title);
	snprintf(launcher.dialog_text, sizeof(launcher.dialog_text), "%s", text);
	launcher.dialog_text_color = color;
	launcher.dialog_row_count = 0;
}

/* the welcome done: Home (PLAY), the maps ready */
static void go_home(void)
{
	launcher.screen = _screen_play;
	launcher.section = _section_play;
	launcher.in_panel = 1;
	launcher.selected = 0;
	launcher.scroll = 0;
}

static void make_folders(void)
{
	static const struct
	{
		const char *folder;
		const char *note;
	} folders[] =
	{
		{ "maps", "Put the Xbox maps here: the files in the maps folder of your Halo disc, with ui.map.\n" },
		{ "maps_ce", "Put Custom Edition maps here, with Custom Edition's bitmaps.map, sounds.map and loc.map.\n" },
		{ "maps_pc", "Put the maps from your Halo PC disc here.\n" },
		{ "maps_md", "Put HaloMD maps here.\n" },
	};
	size_t index;

	for (index = 0; index < sizeof(folders) / sizeof(*folders); index++)
	{
		char path[PATH_SIZE], note[PATH_SIZE + 32];

		root_path(folders[index].folder, path, sizeof(path));
		make_directories(path);
		snprintf(note, sizeof(note), "%s/What goes here.txt", path);
		if (!file_exists(note))
		{
			FILE *file = fopen(note, "wb");

			if (file)
			{
				fputs(folders[index].note, file);
				fclose(file);
			}
		}
	}
	platform_log("launcher: made the maps folders");
	scan();
	open_folder(launcher.root);
	set_status(COLOR_GREEN, "The folders are ready. Put your maps in, then check again.");
}

static void SDLCALL chosen(void *userdata, const char * const *files, int filter)
{
	struct chooser *chooser = userdata;

	(void)filter;
	chooser->path[0] = '\0';
	if (files && files[0])
		snprintf(chooser->path, sizeof(chooser->path), "%s", files[0]);
	SDL_SetAtomicInt(&chooser->done, 1);
}

/* the system's file (or folder) dialog, for purpose; its answer comes to
chooser_answered */
static void choose(int purpose)
{
	static const SDL_DialogFileFilter xbox_filters[] =
	{
		{ "Xbox disc images", "iso;xiso" },
		{ "Halo PC CD images and the Custom Edition installer", "iso;exe" },
		{ "All files", "*" },
	};
	static const SDL_DialogFileFilter halo_pc_filters[] =
	{
		{ "Halo PC CD images and the Custom Edition installer", "iso;exe" },
		{ "Xbox disc images", "iso;xiso" },
		{ "All files", "*" },
	};

	/* (no file dialog under gamescope, the Steam Deck's Game Mode: it would
	never show, and the screen would wait for it) */
	if (updater_gamescope())
	{
		set_status(COLOR_AMBER, "Do this in Desktop Mode. Game Mode can't show the file picker.");
		return;
	}
	memset(&launcher.chooser, 0, sizeof(launcher.chooser));
	launcher.chooser_purpose = purpose;
	launcher.dialog = _dialog_picking;
	launcher.status[0] = '\0';
	if (purpose == _action_pick_folder)
	{
		snprintf(launcher.dialog_title, sizeof(launcher.dialog_title), "PICK YOUR FOLDER");
		snprintf(launcher.dialog_text, sizeof(launcher.dialog_text),
			"Pick the folder that holds your maps folder.");
		/* (debug.launcher_test answers it itself) */
		if (!launcher.script[0])
			SDL_ShowOpenFolderDialog(chosen, &launcher.chooser, platform_sdl_window(), launcher.root, false);
		return;
	}
	snprintf(launcher.dialog_title, sizeof(launcher.dialog_title), "PICK A FILE");
	snprintf(launcher.dialog_text, sizeof(launcher.dialog_text), "%s",
		purpose == _action_add_halo_pc ? "Pick a Halo PC CD image or the Custom Edition installer." :
		"Pick your Xbox disc image, a Halo PC CD image or the Custom Edition installer.");
	if (!launcher.script[0])
	{
		SDL_ShowOpenFileDialog(chosen, &launcher.chooser, platform_sdl_window(),
			purpose == _action_add_halo_pc ? halo_pc_filters : xbox_filters, 3, NULL, false);
	}
}

/* "I already have a maps folder": the folder picked, or the one above it
when the player picked the maps folder itself */
static void folder_picked(const char *picked)
{
	char root[PATH_SIZE], maps[PATH_SIZE + 8], on_disk[256], ui[PATH_SIZE + 300];
	long version;

	snprintf(root, sizeof(root), "%s", picked);
	while (strlen(root) > 1 && (root[strlen(root) - 1] == '/' || root[strlen(root) - 1] == '\\'))
		root[strlen(root) - 1] = '\0';
	if (posix_find_entry_case_insensitive(root, "ui.map", on_disk, sizeof(on_disk)) &&
		!posix_find_entry_case_insensitive(root, "maps", on_disk, sizeof(on_disk)))
	{
		char *slash = strrchr(root, '/');
		char *backslash = strrchr(root, '\\');

		if (backslash && (!slash || backslash > slash))
			slash = backslash;
		if (slash && slash != root)
			*slash = '\0';
	}
	if (!posix_find_entry_case_insensitive(root, "maps", on_disk, sizeof(on_disk)))
	{
		message("NO MAPS THERE", COLOR_WHITE, "That folder has no maps folder. Pick the folder that holds maps.");
		return;
	}
	snprintf(maps, sizeof(maps), "%s/%s", root, on_disk);
	if (!posix_find_entry_case_insensitive(maps, "ui.map", on_disk, sizeof(on_disk)))
	{
		message("NO MAPS THERE", COLOR_WHITE, "That maps folder has no ui.map. Pick the one with your Xbox maps.");
		return;
	}
	snprintf(ui, sizeof(ui), "%s/%s", maps, on_disk);
	version = map_version(ui);
	if (version != MAP_VERSION_XBOX)
	{
		message("NOT THE XBOX MAPS", COLOR_WHITE, version == MAP_VERSION_HALO_PC ||
			version == MAP_VERSION_CUSTOM_EDITION ? "Those are Halo PC maps. Chupathingy needs the Xbox maps." :
			"That ui.map isn't one we can read. Pick the folder with your Xbox maps.");
		return;
	}
	config_write("paths.data", root);
	platform_log("launcher: the maps folder is elsewhere; paths.data is set");
	if (launcher.destination)
		snprintf(launcher.destination, launcher.destination_size, "%s", root);
	set_root(root);
	scan();
	/* (from Settings' Paths: the game's folders were found already) */
	if (launcher.screen == _screen_settings)
	{
		launcher.restart_needed = 1;
		set_status(COLOR_GREEN, "Found your Xbox maps.");
		return;
	}
	go_home();
	set_status(COLOR_GREEN, "Found your Xbox maps.");
}

static void import_file(const char *path)
{
	char error[512];
	int kind = platform_import_kind(path);

	if (kind == PLATFORM_IMPORT_UNKNOWN)
	{
		platform_log("launcher: the file picked is no disc image or installer this reads");
		message("THAT FILE DIDN'T WORK", COLOR_WHITE,
			"That isn't an Xbox disc image, a Halo PC CD image or the Custom Edition installer.");
		return;
	}
	make_directories(launcher.root);
	if (kind == PLATFORM_IMPORT_XBOX_DISC)
	{
		char maps[PATH_SIZE], note[PATH_SIZE + 32], on_disk[256];

		/* (the disc's maps take the place of an empty maps folder, as Make
		the folders leaves it: its note goes first) */
		root_path("maps", maps, sizeof(maps));
		if (!posix_find_entry_case_insensitive(maps, "ui.map", on_disk, sizeof(on_disk)))
		{
			snprintf(note, sizeof(note), "%s/What goes here.txt", maps);
			remove(note);
		}
	}
	if (!platform_import_start(kind, path, launcher.root, error, sizeof(error)))
	{
		message("THAT FILE DIDN'T WORK", COLOR_WHITE, error);
		return;
	}
	platform_log("launcher: importing maps (%s)", kind == PLATFORM_IMPORT_XBOX_DISC ? "Xbox disc image" :
		kind == PLATFORM_IMPORT_HALO_PC_DISC ? "Halo PC CD image" : "Custom Edition installer");
	launcher.import_kind = kind;
	launcher.import_file[0] = '\0';
	launcher.import_done = launcher.import_total = 0;
	launcher.dialog = _dialog_import;
	snprintf(launcher.dialog_title, sizeof(launcher.dialog_title), "%s",
		kind == PLATFORM_IMPORT_XBOX_DISC ? "COPYING YOUR XBOX MAPS" :
		kind == PLATFORM_IMPORT_HALO_PC_DISC ? "COPYING YOUR HALO PC MAPS" : "COPYING THE CE MAPS");
}

static void import_finished(int succeeded, const char *error)
{
	launcher.dialog = _dialog_none;
	scan();
	if (!succeeded)
	{
		platform_log("launcher: the import failed: %s", error);
		message("THAT DIDN'T WORK", COLOR_WHITE, error[0] ? error : "The maps couldn't be copied.");
		return;
	}
	platform_log("launcher: imported %llu MB of maps", launcher.import_done >> 20);
	if (launcher.import_kind == PLATFORM_IMPORT_XBOX_DISC)
	{
		set_status(COLOR_GREEN, launcher.import_done ? "Your Xbox maps are in." : "You had these maps already.");
		if (launcher.screen == _screen_welcome && xbox_maps_ready())
		{
			char status[sizeof(launcher.status)];

			snprintf(status, sizeof(status), "%s", launcher.status);
			go_home();
			set_status(COLOR_GREEN, "%s", status);
		}
		return;
	}
	if (!launcher.import_done)
		set_status(COLOR_GREEN, "You had these maps already.");
	else if (launcher.import_kind == PLATFORM_IMPORT_HALO_PC_DISC)
		set_status(COLOR_GREEN, "The Halo PC maps are in maps_pc.");
	else
		set_status(COLOR_GREEN, "The Custom Edition maps are in maps_ce.");
	if (launcher.screen == _screen_welcome && !xbox_maps_ready())
	{
		size_t length = strlen(launcher.status);

		snprintf(launcher.status + length, sizeof(launcher.status) - length, " Next, add your Xbox maps.");
	}
}

/* the version, as Copy version info puts it on the clipboard */
static void version_text(char *text, size_t size)
{
	snprintf(text, size, "%s\nNetwork version %d\n", build_identity(), delta_legacy_announce());
}

/* ---------- Send my logs */

struct text
{
	char *data;
	size_t length, size;
};

static void text_add(struct text *text, const void *data, size_t length)
{
	if (!text->data && text->size == (size_t)-1)
		return;
	if (text->length + length + 1 > text->size)
	{
		size_t size = (text->length + length + 1) * 2;
		char *grown = realloc(text->data, size);

		if (!grown)
		{
			free(text->data);
			text->data = NULL;
			text->size = (size_t)-1;
			return;
		}
		text->data = grown;
		text->size = size;
	}
	memcpy(text->data + text->length, data, length);
	text->length += length;
	text->data[text->length] = '\0';
}

static void text_string(struct text *text, const char *string)
{
	text_add(text, string, strlen(string));
}

/* a file's text, its last maximum bytes from the start of a line, with
its addresses and home folder taken out; NULL when there is none */
static char *redacted_file(const char *path, size_t maximum, size_t *length)
{
	FILE *file = fopen(path, "rb");
	long size, start;
	char *data, *redacted;
	size_t read, out_size;
	const char *from;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	start = size > (long)maximum ? size - (long)maximum : 0;
	fseek(file, start, SEEK_SET);
	data = malloc((size_t)(size - start) + 1);
	if (!data || size <= 0)
	{
		free(data);
		fclose(file);
		return NULL;
	}
	read = fread(data, 1, (size_t)(size - start), file);
	fclose(file);
	data[read] = '\0';
	from = data;
	if (start > 0)
	{
		const char *line = memchr(data, '\n', read);

		if (line)
			from = line + 1;
	}
	out_size = 3 * (read - (size_t)(from - data)) + 1;
	redacted = malloc(out_size);
	if (!redacted)
	{
		free(data);
		return NULL;
	}
	*length = crash_report_redact(from, read - (size_t)(from - data), redacted, out_size);
	free(data);
	return redacted;
}

/* the newest crash report in the logs folder's crashes */
static int newest_crash_report(char *path, size_t size)
{
	char folder[PATH_SIZE + 16], name[256];
	void *directory;
	time_t newest = 0;

	path[0] = '\0';
	logs_folder(folder, sizeof(folder));
	strncat(folder, "/crashes", sizeof(folder) - strlen(folder) - 1);
	directory = posix_directory_open(folder);
	while (directory && posix_directory_next(directory, name, sizeof(name)))
	{
		char candidate[PATH_SIZE + 300];
		struct posix_file_information information;

		if (!ends_with(name, ".txt"))
			continue;
		snprintf(candidate, sizeof(candidate), "%s/%s", folder, name);
		if (posix_stat(candidate, &information) == 0 && (time_t)information.modification_seconds >= newest)
		{
			newest = (time_t)information.modification_seconds;
			snprintf(path, size, "%s", candidate);
		}
	}
	if (directory)
		posix_directory_close(directory);
	return path[0] != '\0';
}

static void form_field(struct text *body, const char *boundary, const char *name, const char *value)
{
	text_string(body, "--");
	text_string(body, boundary);
	text_string(body, "\r\nContent-Disposition: form-data; name=\"");
	text_string(body, name);
	text_string(body, "\"\r\n\r\n");
	text_string(body, value);
	text_string(body, "\r\n");
}

static int form_file(struct text *body, const char *boundary, const char *name, const char *path, size_t maximum)
{
	size_t length = 0;
	char *data = redacted_file(path, maximum, &length);

	if (!data)
		return 0;
	text_string(body, "--");
	text_string(body, boundary);
	text_string(body, "\r\nContent-Disposition: form-data; name=\"files\"; filename=\"");
	text_string(body, name);
	text_string(body, "\"\r\nContent-Type: text/plain\r\n\r\n");
	text_add(body, data, length);
	text_string(body, "\r\n");
	free(data);
	return 1;
}

static void *upload_thread(void *context)
{
	struct upload *upload = context;

	upload->status = posix_browser_send(upload->url, upload->body, upload->body_length, upload->content_type,
		upload->headers, upload->response, (int)sizeof(upload->response), upload->error, (int)sizeof(upload->error));
	SDL_SetAtomicInt(&upload->done, 1);
	return NULL;
}

/* the site's platform names (puma's dumps.py PLATFORMS) */
static const char *dump_platform(void)
{
	const char *platform = build_identity_platform();
	const char *deck = getenv("SteamDeck");

	/* (SteamOS: the site still takes it by its old name) */
	if (deck && !strcmp(deck, "1"))
		return "Steam Deck";
	if (!strcmp(platform, "macOS"))
		return "Mac";
	if (!strcmp(platform, "Windows") || !strcmp(platform, "Linux"))
		return platform;
	return "Other";
}

static void send_logs(int with_settings)
{
	static const char boundary[] = "chupathingy-launcher-logs-5f3a";
	struct text body = { NULL, 0, 0 };
	char folder[PATH_SIZE], path[PATH_SIZE + 64], note[256];
	const char *url = *config_string("debug.dump_url") ? config_string("debug.dump_url") : LAUNCHER_DUMP_URL;
	const char *host, *host_end;
	int files = 0;
	pthread_t thread;

	snprintf(note, sizeof(note), "Sent from the game: %s", build_identity());
	form_field(&body, boundary, "name", "Chupathingy player");
	form_field(&body, boundary, "platform", dump_platform());
	form_field(&body, boundary, "note", note);
	logs_folder(folder, sizeof(folder));
	snprintf(path, sizeof(path), "%s/debug.txt", folder);
	files += form_file(&body, boundary, "debug.txt", path, LAUNCHER_LOG_TAIL);
	if (newest_crash_report(path, sizeof(path)))
		files += form_file(&body, boundary, "crash_report.txt", path, CRASH_REPORT_MAXIMUM_FILE);
	if (with_settings)
	{
		config_folder(path, sizeof(path));
		strncat(path, "config.toml", sizeof(path) - strlen(path) - 1);
		files += form_file(&body, boundary, "config.toml", path, 1024 * 1024);
	}
	text_string(&body, "--");
	text_string(&body, boundary);
	text_string(&body, "--\r\n");
	if (!body.data || !files)
	{
		free(body.data);
		message("NOTHING TO SEND", COLOR_WHITE, "There's no debug.txt yet. Play once, then send.");
		return;
	}
	free(launcher.upload.body);
	memset(&launcher.upload, 0, sizeof(launcher.upload));
	snprintf(launcher.upload.url, sizeof(launcher.upload.url), "%s", url);
	launcher.upload.body = body.data;
	launcher.upload.body_length = body.length;
	snprintf(launcher.upload.content_type, sizeof(launcher.upload.content_type),
		"multipart/form-data; boundary=%s", boundary);
	/* (the page's own origin, as its form sends: the site takes uploads
	from that page) */
	host = strstr(url, "://");
	host = host ? host + 3 : url;
	host_end = host + strcspn(host, ":/");
	snprintf(launcher.upload.headers, sizeof(launcher.upload.headers), "Origin: https://%.*s\r\n",
		(int)(host_end - host), host);
	if (pthread_create(&thread, NULL, upload_thread, &launcher.upload) != 0)
	{
		message("THAT DIDN'T SEND", COLOR_WHITE, "The upload couldn't start.");
		return;
	}
	pthread_detach(thread);
	platform_log("launcher: sending the logs (%d files%s)", files, with_settings ? ", with config.toml" : "");
	launcher.dialog = _dialog_sending;
	snprintf(launcher.dialog_title, sizeof(launcher.dialog_title), "SEND MY LOGS");
}

/* a string field of the site's answer ("ref", "error") */
static int json_string(const char *json, const char *name, char *value, size_t size)
{
	char key[64];
	const char *cursor;
	size_t length = 0;

	snprintf(key, sizeof(key), "\"%s\"", name);
	cursor = strstr(json, key);
	if (!cursor)
		return 0;
	cursor += strlen(key);
	while (*cursor == ' ' || *cursor == ':')
		cursor++;
	if (*cursor++ != '"')
		return 0;
	while (*cursor && *cursor != '"' && length + 1 < size)
	{
		if (*cursor == '\\' && cursor[1])
			cursor++;
		value[length++] = *cursor++;
	}
	value[length] = '\0';
	return 1;
}

static void upload_finished(void)
{
	struct upload *upload = &launcher.upload;
	char error[256];

	free(upload->body);
	upload->body = NULL;
	if (upload->status == 200 && json_string(upload->response, "ref", launcher.sent_code, sizeof(launcher.sent_code)))
	{
		upper(launcher.sent_code);
		platform_log("launcher: the logs are sent");
		launcher.dialog = _dialog_sent;
		snprintf(launcher.dialog_title, sizeof(launcher.dialog_title), "SENT");
		snprintf(launcher.dialog_text, sizeof(launcher.dialog_text), "Paste this code in Discord with your report.");
		return;
	}
	platform_log("launcher: the logs were not sent (status %d)", upload->status);
	if (upload->status && json_string(upload->response, "error", error, sizeof(error)))
		message("THAT DIDN'T SEND", COLOR_WHITE, error);
	else if (!upload->status)
		message("THAT DIDN'T SEND", COLOR_WHITE, "We couldn't reach the site. Check your connection and try again.");
	else
		message("THAT DIDN'T SEND", COLOR_WHITE, "The site couldn't take them right now. Try again in a while.");
}

/* ---------- the actions */

static void enter_screen(int screen)
{
	if (screen == _screen_about && launcher.screen != _screen_about)
		about_reset(&launcher.about);
	launcher.screen = screen;
	launcher.selected = 0;
	launcher.scroll = 0;
	launcher.status[0] = '\0';
}

/* the game's executable (an AppImage: its file), to start it again */
static int executable_path(char *path, size_t size)
{
	if (update_appimage_path(path, (int)size))
		return 1;
#ifdef __APPLE__
	{
		char found[PATH_SIZE];
		uint32_t found_size = sizeof(found);

		/* (the update path's /proc is Linux's) */
		if (_NSGetExecutablePath(found, &found_size) != 0 || !realpath(found, path))
			return 0;
		return 1;
	}
#else
	return update_executable_path(path, (int)size);
#endif
}

/* Chupathingy started again, in mode (its id) and past this screen, for
the settings that show only after a restart; 0 if it could not be */
static int restart_with(const char *mode)
{
	char path[PATH_SIZE], argument[64];

	if (!executable_path(path, sizeof(path)))
	{
		platform_log("launcher: could not find the game's executable to start it again");
		return 0;
	}
	snprintf(argument, sizeof(argument), "--mode=%s", mode);
#ifdef __linux__
	/* (this same process, as an update's restart: Steam and gamescope keep
	the game, rather than a second one beside it) */
	if (update_start_in_place_with(path, argument))
	{
		platform_log("launcher: starting again in this same process for the settings that need it");
		return 1;
	}
#endif
	if (!update_launch_with(path, argument))
	{
		platform_log("launcher: could not start again for the settings that need it");
		return 0;
	}
	platform_log("launcher: started again for the settings that need it");
	return 1;
}

int launcher_restart_to_setup(void)
{
	char path[PATH_SIZE];

	if (!executable_path(path, sizeof(path)))
	{
		platform_log("launcher: could not start again on the setup screen");
		return 0;
	}
#ifdef __linux__
	/* (this same process: see restart_with) */
	if (update_start_in_place_with(path, "--setup"))
	{
		platform_log("launcher: SETUP: starting again on the setup screen in this same process");
		exit(EXIT_SUCCESS);
	}
#endif
	if (!update_launch_with(path, "--setup"))
	{
		platform_log("launcher: could not start again on the setup screen");
		return 0;
	}
	platform_log("launcher: SETUP: starting again on the setup screen");
	exit(EXIT_SUCCESS);
	return 1;
}

static void play(void)
{
	if (!xbox_maps_ready())
	{
		launcher.section = _section_maps;
		enter_screen(_screen_maps);
		launcher.in_panel = 1;
		launcher.selected = 0;
		set_status(COLOR_AMBER, "Add your Xbox maps first.");
		return;
	}
	platform_log("launcher: play");
	mode_apply(launcher.mode, 1);
	if (launcher.restart_needed && restart_with(launcher.mode >= 0 && launcher.mode < launcher.mode_count ?
		launcher.modes[launcher.mode].id : "vanilla"))
	{
		exit(EXIT_SUCCESS);
	}
	launcher.done = 1;
}

static void act(const struct row *row)
{
	char path[PATH_SIZE + 32];

	if (!row || row->disabled)
		return;
	launcher.status[0] = '\0';
	switch (row->action)
	{
	case _action_play:
		play();
		break;
	case _action_play_mode:
		if (!launcher.modes[row->argument].installed)
		{
			launcher.section = _section_maps;
			enter_screen(_screen_maps);
			set_status(COLOR_AMBER, "Put the %s maps in %s, with its ui.map, then check again.",
				launcher.modes[row->argument].title, launcher.modes[row->argument].folder);
			break;
		}
		launcher.mode = row->argument;
		play();
		break;
	case _action_family:
		launcher.family = row->argument;
		launcher.maps_row = launcher.selected;
		enter_screen(_screen_family);
		break;
	case _action_move_old_folders:
	{
		int moved = platform_old_map_folders_move();

		scan();
		if (moved)
			set_status(COLOR_GREEN, "Moved. Your maps are in the new folders.");
		else
			set_status(COLOR_AMBER, "We couldn't move them. They still play from where they are.");
		break;
	}
	case _action_add_file:
	case _action_add_halo_pc:
	case _action_pick_folder:
		choose(row->action);
		break;
	case _action_make_folders:
		make_folders();
		break;
	case _action_open_game_folder:
		open_folder(launcher.root);
		break;
	case _action_open_family_folder:
		root_path(launcher.families[launcher.family].folder, path, sizeof(path));
		open_folder(path);
		break;
	case _action_check_family:
	{
		char text[96];
		unsigned int color;

		family_scan(&launcher.families[launcher.family], 0);
		family_check_text(&launcher.families[launcher.family], text, sizeof(text), &color);
		set_status(color, "Checked: %s.", text);
		break;
	}
	case _action_downloads:
		break;
	case _action_send_logs:
		launcher.dialog = _dialog_send_ask;
		launcher.dialog_selected = 1;
		snprintf(launcher.dialog_title, sizeof(launcher.dialog_title), "SEND MY LOGS");
		snprintf(launcher.dialog_text, sizeof(launcher.dialog_text), "Send your settings too?");
		launcher.dialog_text_color = COLOR_WHITE;
		launcher.dialog_row_count = 2;
		memset(launcher.dialog_rows, 0, sizeof(launcher.dialog_rows));
		launcher.dialog_rows[0].action = _action_send_with_settings;
		snprintf(launcher.dialog_rows[0].label, sizeof(launcher.dialog_rows[0].label), "YES, WITH CONFIG.TOML");
		launcher.dialog_rows[1].action = _action_send_without_settings;
		snprintf(launcher.dialog_rows[1].label, sizeof(launcher.dialog_rows[1].label), "NO, JUST THE LOGS");
		break;
	case _action_open_logs:
		logs_folder(path, sizeof(path));
		open_folder(path);
		break;
	case _action_open_crashes:
		logs_folder(path, sizeof(path));
		strncat(path, "/crashes", sizeof(path) - strlen(path) - 1);
		open_folder(path);
		break;
	case _action_copy_version:
	{
		char text[512];

		version_text(text, sizeof(text));
		if (SDL_SetClipboardText(text))
			set_status(COLOR_GREEN, "Copied.");
		else
			set_status(COLOR_AMBER, "We couldn't copy it.");
		break;
	}
	case _action_settings_section:
		snprintf(launcher.setting_section, sizeof(launcher.setting_section), "%s", row->key);
		launcher.settings_row = launcher.selected;
		enter_screen(_screen_setting_keys);
		break;
	case _action_open_path:
		if (row->argument < 0)
			open_folder(platform_save_root());
		else if (row->argument == _port_folder_data)
			open_folder(launcher.root);
		else if (row->argument == _port_folder_logs)
		{
			logs_folder(path, sizeof(path));
			open_folder(path);
		}
		else
			open_folder(platform_folder((enum port_folder)row->argument));
		break;
	case _action_check_updates:
		/* (not while an update installs: the tab shows it) */
		if (updater_install_status(NULL, 0) == UPDATER_INSTALL_DOWNLOADING ||
			updater_install_status(NULL, 0) == UPDATER_INSTALL_INSTALLING)
		{
			break;
		}
		if (!updater_check_now() && updater_check_state(NULL, 0) != UPDATER_CHECKING)
			set_status(COLOR_GREY, "This build doesn't update itself.");
		break;
	case _action_install_update:
		/* (on its own thread: the tab shows how it goes, updater_install_status) */
		if (!updater_install_begin())
			set_status(COLOR_AMBER, "That didn't work. Get it from the release page.");
		else
			launcher.status[0] = '\0';
		break;
	case _action_discord:
		if (!SDL_OpenURL(LAUNCHER_DISCORD_URL))
			set_status(COLOR_AMBER, "We couldn't open your browser. The link is %s", LAUNCHER_DISCORD_URL);
		break;
	default:
		break;
	}
}

/* a row's value changed with left (-1) or right (+1) */
static void cycle(const struct row *row, int step)
{
	static const char *const words[] = { "ask", "always", "never" };
	const char *setting;
	int index;

	if (row->action == _action_start_mode)
	{
		/* (always ask, then each mode installed) */
		int choices[MAXIMUM_MODES + 1], count = 0, current = 0, mode;

		choices[count++] = -1;
		for (mode = 0; mode < launcher.mode_count; mode++)
		{
			if (launcher.modes[mode].installed)
				choices[count++] = mode;
		}
		for (index = 0; index < count; index++)
		{
			if (choices[index] == (mode_asks() ? -1 : mode_default()))
				current = index;
		}
		index = ((current + step) % count + count) % count;
		if (choices[index] < 0)
			config_write("launcher.mode_choice", "ask");
		else
		{
			mode_make_default(choices[index]);
			launcher.mode = choices[index];
		}
		return;
	}
	if (row->action == _action_setting)
	{
		char before[72];

		snprintf(before, sizeof(before), "%s", row->value);
		settings_row_change(row->setting, step);
		if (settings_row_restart(row->setting))
			launcher.restart_needed = 1;
		if (!strcmp(settings_row_key(row->setting), "display.particle_limit"))
		{
			char now[72];

			settings_row_value(row->setting, now, sizeof(now));
			if (!strcmp(now, "POTATO"))
				set_status(COLOR_WHITE, "Lower shadows, particles and lights, no anti-aliasing, drawn at 640x480.");
		}
		return;
	}
	if (row->action == _action_setting_key)
	{
		settings_key_change(row->key, step);
		return;
	}
	if (row->action != _action_downloads)
		return;
	setting = config_string("network.map_downloads");
	for (index = 0; index < 3 && strcmp(setting, words[index]); index++)
		;
	index = ((index % 3) + step + 3) % 3;
	config_write("network.map_downloads", words[index]);
}

static void back(void)
{
	switch (launcher.screen)
	{
	case _screen_welcome:
		platform_log("launcher: quit from Welcome");
		exit(EXIT_SUCCESS);
	case _screen_map_list:
		launcher.screen = _screen_family;
		launcher.selected = launcher.family_row;
		launcher.scroll = 0;
		launcher.status[0] = '\0';
		break;
	case _screen_setting_keys:
		launcher.screen = _screen_settings;
		launcher.selected = launcher.settings_row;
		launcher.scroll = 0;
		launcher.status[0] = '\0';
		break;
	case _screen_family:
		launcher.screen = _screen_maps;
		launcher.selected = launcher.maps_row;
		launcher.scroll = 0;
		launcher.status[0] = '\0';
		scan();
		break;
	default:
		if (launcher.in_panel)
		{
			launcher.in_panel = 0;
			launcher.status[0] = '\0';
		}
		break;
	}
}

/* the section picked: its screen in the panel */
static void open_section(int section)
{
	static const int screens[NUMBER_OF_SECTIONS] = { _screen_play, _screen_maps, _screen_settings, _screen_logs,
		_screen_about, _screen_play };

	launcher.section = section;
	if (section != _section_quit)
		enter_screen(screens[section]);
}

static void open_tab(int tab)
{
	launcher.tab = tab;
	enter_screen(_screen_settings);
}

static void dialog_input(int button)
{
	/* (B gives up on a file dialog that never came, or that is behind the
	window: its answer, if it comes, is not taken) */
	if (launcher.dialog == _dialog_picking)
	{
		if (button == _button_b)
		{
			launcher.dialog = _dialog_none;
			platform_log("launcher: the file dialog was given up on");
		}
		return;
	}
	if (launcher.dialog == _dialog_message)
	{
		if (button == _button_a || button == _button_b)
			launcher.dialog = _dialog_none;
		return;
	}
	if (launcher.dialog == _dialog_sent)
	{
		if (button == _button_y)
		{
			SDL_SetClipboardText(launcher.sent_code);
			set_status(COLOR_GREEN, "Your code %s is copied. Paste it in Discord with your report.", launcher.sent_code);
			launcher.dialog = _dialog_none;
		}
		else if (button == _button_a || button == _button_b)
		{
			set_status(COLOR_GREEN, "Sent. Your code is %s.", launcher.sent_code);
			launcher.dialog = _dialog_none;
		}
		return;
	}
	if (launcher.dialog != _dialog_send_ask && launcher.dialog != _dialog_mode_question)
		return;
	switch (button)
	{
	case _button_up:
		if (launcher.dialog_selected > 0)
			launcher.dialog_selected--;
		break;
	case _button_down:
		if (launcher.dialog_selected + 1 < launcher.dialog_row_count)
			launcher.dialog_selected++;
		break;
	case _button_b:
		launcher.dialog = _dialog_none;
		break;
	case _button_a:
	{
		int action = launcher.dialog_rows[launcher.dialog_selected].action;

		launcher.dialog = _dialog_none;
		if (action == _action_ask_each_time)
		{
			config_write("launcher.mode_choice", "ask");
			platform_log("launcher: the player picks a mode each time");
		}
		else if (action == _action_always_play)
		{
			mode_make_default(launcher.mode);
			platform_log("launcher: the player always plays %s", launcher.modes[launcher.mode].id);
			play();
		}
		else
			send_logs(action == _action_send_with_settings);
		break;
	}
	default:
		break;
	}
}

static void input(int button)
{
	const struct row *row;

	if (button == _button_none)
		return;
	if (launcher.dialog)
	{
		dialog_input(button);
		return;
	}
	build_rows();
	row = launcher.row_count ? &launcher.rows[launcher.selected] : NULL;
	if (launcher.screen != _screen_welcome && !launcher.in_panel)
	{
		switch (button)
		{
		case _button_up:
		case _button_previous:
			if (launcher.section > 0)
				open_section(launcher.section - 1);
			break;
		case _button_down:
		case _button_next:
			if (launcher.section + 1 < NUMBER_OF_SECTIONS)
				open_section(launcher.section + 1);
			break;
		case _button_a:
		case _button_right:
			if (launcher.section == _section_quit)
			{
				if (button == _button_a)
				{
					platform_log("launcher: quit");
					exit(EXIT_SUCCESS);
				}
				break;
			}
			launcher.in_panel = 1;
			break;
		default:
			break;
		}
		return;
	}
	if (launcher.screen == _screen_about)
	{
		switch (button)
		{
		case _button_up:
			about_input(&launcher.about, ABOUT_INPUT_UP);
			break;
		case _button_down:
			about_input(&launcher.about, ABOUT_INPUT_DOWN);
			break;
		case _button_a:
			about_input(&launcher.about, ABOUT_INPUT_A);
			break;
		case _button_b:
		case _button_left:
			if (!about_input(&launcher.about, ABOUT_INPUT_B))
				launcher.in_panel = 0;
			break;
		case _button_previous:
		case _button_next:
		{
			int section = launcher.section + (button == _button_next ? 1 : -1);

			if (section >= 0 && section < _section_quit)
				open_section(section);
			break;
		}
		default:
			break;
		}
		return;
	}
	switch (button)
	{
	case _button_up:
	case _button_down:
		if (button == _button_up ? launcher.selected > 0 : launcher.selected + 1 < launcher.row_count)
		{
			launcher.selected += button == _button_up ? -1 : 1;
			launcher.status[0] = '\0';
			/* (PLAY starts the mode last picked out) */
			if (launcher.rows[launcher.selected].action == _action_play_mode &&
				launcher.modes[launcher.rows[launcher.selected].argument].installed)
			{
				launcher.mode = launcher.rows[launcher.selected].argument;
			}
		}
		break;
	case _button_left:
	case _button_right:
		if (row && row->cycles)
			cycle(row, button == _button_left ? -1 : 1);
		else if (button == _button_left && launcher.screen != _screen_welcome && launcher.screen != _screen_family &&
			launcher.screen != _screen_map_list)
		{
			launcher.in_panel = 0;
		}
		break;
	case _button_previous:
	case _button_next:
		/* (Settings: its tabs) */
		if (launcher.screen == _screen_settings || launcher.screen == _screen_setting_keys)
		{
			open_tab((launcher.tab + (button == _button_next ? 1 : NUMBER_OF_TABS - 1)) % NUMBER_OF_TABS);
			break;
		}
		if (launcher.screen != _screen_welcome && launcher.screen != _screen_family &&
			launcher.screen != _screen_map_list)
		{
			int section = launcher.section + (button == _button_next ? 1 : -1);

			if (section >= 0 && section < _section_quit)
				open_section(section);
		}
		break;
	case _button_a:
		if (row && row->cycles)
			cycle(row, 1);
		else
			act(row);
		break;
	case _button_b:
		back();
		break;
	case _button_x:
		if (launcher.screen == _screen_welcome || launcher.screen == _screen_maps)
		{
			scan();
			if (launcher.screen == _screen_welcome && xbox_maps_ready())
			{
				go_home();
				set_status(COLOR_GREEN, "Found your Xbox maps.");
			}
			else if (launcher.screen == _screen_welcome)
				set_status(COLOR_AMBER, "No Xbox maps yet. Put them in the maps folder, with ui.map.");
			else
				set_status(COLOR_GREEN, "Checked.");
		}
		else if (launcher.screen == _screen_play && row && row->action == _action_play_mode &&
			launcher.modes[row->argument].installed && launcher.installed_modes > 1)
		{
			mode_make_default(row->argument);
			set_status(COLOR_GREEN, "%s starts by default now. %s", launcher.modes[row->argument].title,
				reopen_hint());
		}
		else if (launcher.screen == _screen_family)
		{
			launcher.family_row = launcher.selected;
			family_scan(&launcher.families[launcher.family], 1);
			enter_screen(_screen_map_list);
		}
		break;
	case _button_y:
		if (launcher.screen == _screen_welcome)
			open_folder(launcher.root);
		else if (launcher.screen == _screen_maps && row && row->action == _action_family)
		{
			char path[PATH_SIZE];

			root_path(launcher.families[row->argument].folder, path, sizeof(path));
			open_folder(path);
		}
		else if (row && row->action == _action_setting)
		{
			settings_row_reset(row->setting);
			if (settings_row_restart(row->setting))
				launcher.restart_needed = 1;
		}
		else if (row && row->action == _action_setting_key)
			config_reset(row->key);
		else if (row && row->action == _action_open_path && row->argument == _port_folder_data)
			choose(_action_pick_folder);
		break;
	default:
		break;
	}
}

/* ---------- the window's events */

/* a point of the window (its units) in the layout */
static void layout_point(float x, float y, float *layout_x, float *layout_y)
{
	int points_width = 1, points_height = 1;
	float pixel_x, pixel_y, scale;

	SDL_GetWindowSize(platform_sdl_window(), &points_width, &points_height);
	pixel_x = x * (float)launcher.window_width / (float)(points_width > 0 ? points_width : 1);
	pixel_y = y * (float)launcher.window_height / (float)(points_height > 0 ? points_height : 1);
	scale = (float)launcher.picture_height / 480.0f;
	*layout_x = launcher.left + (pixel_x - (float)launcher.picture_x) / scale;
	*layout_y = (pixel_y - (float)(launcher.window_height - launcher.picture_y - launcher.picture_height)) / scale;
}

static const struct target *target_at(float x, float y)
{
	int index;

	for (index = launcher.target_count - 1; index >= 0; index--)
	{
		const struct target *target = &launcher.targets[index];

		if (x >= target->x0 && x < target->x1 && y >= target->y0 && y < target->y1)
		{
			/* (a dialog's up: only its own) */
			if (launcher.dialog && target->kind != _target_dialog_row && target->kind != _target_button)
				return NULL;
			return target;
		}
	}
	return NULL;
}

static void point_at(float x, float y, int click)
{
	float layout_x, layout_y;
	const struct target *target;

	layout_point(x, y, &layout_x, &layout_y);
	target = target_at(layout_x, layout_y);
	if (!target)
		return;
	switch (target->kind)
	{
	case _target_row:
		if (launcher.screen != _screen_welcome && !launcher.in_panel)
			launcher.in_panel = click;
		if (target->index != launcher.selected && (launcher.in_panel || launcher.screen == _screen_welcome))
		{
			launcher.selected = target->index;
			launcher.status[0] = '\0';
		}
		if (click)
			input(_button_a);
		break;
	case _target_section:
		if (click)
		{
			if (target->index != launcher.section)
				open_section(target->index);
			launcher.in_panel = 0;
			if (target->index == _section_quit)
				input(_button_a);
		}
		break;
	case _target_tab:
		if (click)
		{
			launcher.in_panel = 1;
			open_tab(target->index);
		}
		break;
	case _target_dialog_row:
		launcher.dialog_selected = target->index;
		if (click)
			input(_button_a);
		break;
	case _target_button:
		if (click)
			input(target->index);
		break;
	}
}

static int key_button(SDL_Scancode scancode)
{
	switch (scancode)
	{
	case SDL_SCANCODE_UP: case SDL_SCANCODE_W: return _button_up;
	case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: return _button_down;
	case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: return _button_left;
	case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: return _button_right;
	case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: return _button_a;
	case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: return _button_b;
	case SDL_SCANCODE_E: return _button_x;
	case SDL_SCANCODE_TAB: return _button_y;
	case SDL_SCANCODE_Q: case SDL_SCANCODE_PAGEUP: return _button_previous;
	case SDL_SCANCODE_C: case SDL_SCANCODE_PAGEDOWN: return _button_next;
	default: return _button_none;
	}
}

static int pad_button(int button)
{
	switch (button)
	{
	case SDL_GAMEPAD_BUTTON_DPAD_UP: return _button_up;
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return _button_down;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return _button_left;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return _button_right;
	case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: return _button_a;
	case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_BACK: return _button_b;
	case SDL_GAMEPAD_BUTTON_WEST: return _button_x;
	case SDL_GAMEPAD_BUTTON_NORTH: return _button_y;
	case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return _button_previous;
	case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return _button_next;
	default: return _button_none;
	}
}

/* a held direction repeats (keys repeat by themselves) */
static void hold(int button)
{
	launcher.held_button = button >= _button_up && button <= _button_right ? button : _button_none;
	launcher.held_next = SDL_GetTicks() + 400;
}

static void events(void)
{
	SDL_Event event;

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_EVENT_QUIT:
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			platform_exit_window_closed();
			break;
		case SDL_EVENT_KEY_DOWN:
		{
			int button = key_button(event.key.scancode);

			platform_input_scheme_used(NULL);
			/* (a key held repeats a direction, nothing else) */
			if (!event.key.repeat || (button >= _button_up && button <= _button_right))
				input(button);
			break;
		}
		case SDL_EVENT_GAMEPAD_ADDED:
			SDL_OpenGamepad(event.gdevice.which);
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		{
			int button = pad_button(event.gbutton.button);

			platform_input_scheme_used(SDL_GetGamepadFromID(event.gbutton.which));
			input(button);
			hold(button);
			break;
		}
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			if (pad_button(event.gbutton.button) == launcher.held_button)
				launcher.held_button = _button_none;
			break;
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY || event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX)
			{
				int vertical = event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY;
				int direction = event.gaxis.value > 20000 ? (vertical ? _button_down : _button_right) :
					event.gaxis.value < -20000 ? (vertical ? _button_up : _button_left) : _button_none;

				if (direction != _button_none && direction != launcher.stick_direction)
				{
					platform_input_scheme_used(SDL_GetGamepadFromID(event.gaxis.which));
					input(direction);
					hold(direction);
				}
				else if (direction == _button_none && (launcher.stick_direction == (vertical ? _button_up : _button_left) ||
					launcher.stick_direction == (vertical ? _button_down : _button_right)))
				{
					if (launcher.held_button == launcher.stick_direction)
						launcher.held_button = _button_none;
				}
				else
					break;
				launcher.stick_direction = direction;
			}
			break;
		case SDL_EVENT_MOUSE_MOTION:
			if (fabsf(event.motion.xrel) + fabsf(event.motion.yrel) > 2.0f)
				platform_input_scheme_used(NULL);
			point_at(event.motion.x, event.motion.y, 0);
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			platform_input_scheme_used(NULL);
			if (event.button.button == SDL_BUTTON_LEFT)
				point_at(event.button.x, event.button.y, 1);
			else if (event.button.button == SDL_BUTTON_RIGHT)
				input(_button_b);
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			if (event.wheel.y > 0.0f)
				input(_button_up);
			else if (event.wheel.y < 0.0f)
				input(_button_down);
			break;
		case SDL_EVENT_DROP_FILE:
			/* a disc image or installer dropped on the window: added, as
			if picked (an invite link, macOS's way of opening one, is the
			game's: it waits for PLAY) */
			if (event.drop.data && strncmp(event.drop.data, "halo://", 7) && !launcher.dialog)
				import_file(event.drop.data);
			break;
		default:
			break;
		}
	}
	if (launcher.held_button && SDL_GetTicks() >= launcher.held_next)
	{
		input(launcher.held_button);
		launcher.held_next = SDL_GetTicks() + 90;
	}
}

/* ---------- the dialogs' work */

static void poll_work(void)
{
	if (launcher.dialog == _dialog_picking && SDL_GetAtomicInt(&launcher.chooser.done))
	{
		launcher.dialog = _dialog_none;
		if (!launcher.chooser.path[0])
			return;
		if (launcher.chooser_purpose == _action_pick_folder)
			folder_picked(launcher.chooser.path);
		else
			import_file(launcher.chooser.path);
		return;
	}
	if (launcher.dialog == _dialog_import)
	{
		BOOL succeeded = FALSE;
		char error[512];

		if (platform_import_poll(launcher.import_file, sizeof(launcher.import_file), &launcher.import_done,
			&launcher.import_total, &succeeded, error, sizeof(error)))
		{
			import_finished(succeeded, error);
		}
		return;
	}
	if (launcher.dialog == _dialog_sending && SDL_GetAtomicInt(&launcher.upload.done))
		upload_finished();
	/* (an update begun on UPDATES, once it is ready: put in place, and the
	game restarts) */
	updater_install_restart();
}

/* ---------- debug.launcher_test: scripted input and screenshots, for the
showcase and for checking the layout at sizes (comma-separated steps: up,
down, left, right, a, b, x, y, lb, rb; shot:<name> into
debug.screenshot_directory; size:<width>x<height>; wait:<ms>; pick:<file>
and folder:<folder>, the answer of the file dialog open; quit) */

static void screenshot(const char *name)
{
	const char *directory = config_string("debug.screenshot_directory");
	unsigned char header[54] = { 'B', 'M' };
	unsigned long size = (unsigned long)launcher.window_width * (unsigned long)launcher.window_height * 4;
	unsigned char *pixels;
	char path[PATH_SIZE];
	FILE *file;
	unsigned long index;

	if (!*directory || !size)
		return;
	pixels = malloc(size);
	if (!pixels)
		return;
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glReadPixels(0, 0, launcher.window_width, launcher.window_height, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
	for (index = 0; index < size / 4; index++)
		pixels[index * 4 + 3] = 0xFF;
	snprintf(path, sizeof(path), "%s/%s.bmp", directory, name);
	file = fopen(path, "wb");
	if (file)
	{
		*(unsigned int *)(header + 2) = (unsigned int)(54 + size);
		*(unsigned int *)(header + 10) = 54;
		*(unsigned int *)(header + 14) = 40;
		*(int *)(header + 18) = launcher.window_width;
		*(int *)(header + 22) = launcher.window_height;
		*(unsigned short *)(header + 26) = 1;
		*(unsigned short *)(header + 28) = 32;
		*(unsigned int *)(header + 34) = (unsigned int)size;
		fwrite(header, 1, sizeof(header), file);
		fwrite(pixels, 1, size, file);
		fclose(file);
	}
	free(pixels);
}

static void script_step(void)
{
	static const struct
	{
		const char *word;
		int button;
	} words[] =
	{
		{ "up", _button_up }, { "down", _button_down }, { "left", _button_left }, { "right", _button_right },
		{ "a", _button_a }, { "b", _button_b }, { "x", _button_x }, { "y", _button_y },
		{ "lb", _button_previous }, { "rb", _button_next },
	};
	char step[512];
	size_t length, index;
	Uint64 now = SDL_GetTicks();

	if (!launcher.script_cursor || !*launcher.script_cursor || now < launcher.script_next)
		return;
	length = strcspn(launcher.script_cursor, ",");
	snprintf(step, sizeof(step), "%.*s", (int)length, launcher.script_cursor);
	launcher.script_cursor += length + (launcher.script_cursor[length] == ',');
	launcher.script_next = now + 350;
	for (index = 0; index < sizeof(words) / sizeof(*words); index++)
	{
		if (!strcmp(step, words[index].word))
		{
			input(words[index].button);
			return;
		}
	}
	if (!strncmp(step, "shot:", 5))
		snprintf(launcher.screenshot, sizeof(launcher.screenshot), "%s", step + 5);
	else if (!strncmp(step, "size:", 5))
	{
		int width = atoi(step + 5);
		const char *by = strchr(step + 5, 'x');

		if (width > 0 && by)
			SDL_SetWindowSize(platform_sdl_window(), width, atoi(by + 1));
		launcher.script_next = now + 900;
	}
	else if (!strncmp(step, "wait:", 5))
		launcher.script_next = now + (Uint64)atoi(step + 5);
	else if ((!strncmp(step, "pick:", 5) || !strncmp(step, "folder:", 7)) && launcher.dialog == _dialog_picking)
	{
		snprintf(launcher.chooser.path, sizeof(launcher.chooser.path), "%s", strchr(step, ':') + 1);
		SDL_SetAtomicInt(&launcher.chooser.done, 1);
	}
	else if (!strcmp(step, "quit"))
	{
		platform_log("launcher: debug.launcher_test is done");
		exit(EXIT_SUCCESS);
	}
}

/* ---------- the loop */

static void frame(void)
{
	int width, height;

	platform_video_drawable_size(&width, &height);
	if (width <= 0 || height <= 0)
	{
		SDL_Delay(16);
		return;
	}
	launcher.window_width = width;
	launcher.window_height = height;
	/* (a window narrower than 4:3 shows the 4:3 layout across its width,
	centered) */
	launcher.picture_x = 0;
	launcher.picture_width = width;
	launcher.picture_height = height;
	launcher.picture_y = 0;
	if (width * 3 < height * 4)
	{
		launcher.picture_height = width * 3 / 4;
		launcher.picture_y = (height - launcher.picture_height) / 2;
	}
	{
		float layout = 480.0f * (float)launcher.picture_width / (float)launcher.picture_height;

		launcher.left = 320.0f - layout * 0.5f;
		launcher.right = 320.0f + layout * 0.5f;
	}
	draw();
#ifdef HALO_BGFX
	if (xgpu_bgfx)
	{
		char path[PATH_SIZE] = "";

		if (launcher.screenshot[0] && *config_string("debug.screenshot_directory"))
			snprintf(path, sizeof(path), "%s/%s.bmp", config_string("debug.screenshot_directory"), launcher.screenshot);
		launcher.screenshot[0] = '\0';
		/* (bgfx that does not start, a blocklisted Vulkan driver say:
		OpenGL, once, rather than trying again each frame) */
		if (!xgpu_bgfx_launcher_frame(ui_style_color(UI_COLOR_CLEAR), launcher.picture_x, launcher.picture_y,
			launcher.picture_width, launcher.picture_height, path))
			platform_video_reopen_for_gl();
		return;
	}
#endif
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glViewport(0, 0, width, height);
	glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	{
		unsigned int clear = ui_style_color(UI_COLOR_CLEAR);

		glClearColor((float)(clear >> 24) / 255.0f, (float)(clear >> 16 & 0xFF) / 255.0f,
			(float)(clear >> 8 & 0xFF) / 255.0f, 1.0f);
	}
	glClear(GL_COLOR_BUFFER_BIT);
	ui_overlay_present(launcher.picture_x, launcher.picture_y, launcher.picture_width, launcher.picture_height, width,
		height);
	/* (the window is opaque, as the game's frames: d3d8_gl.c's Present) */
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	if (launcher.screenshot[0])
	{
		screenshot(launcher.screenshot);
		launcher.screenshot[0] = '\0';
	}
	platform_video_swap();
}

static void run(void)
{
	Uint64 last = SDL_GetTicks();

	launcher.shown = 1;
	launcher.done = 0;
	snprintf(launcher.script, sizeof(launcher.script), "%s", config_string("debug.launcher_test"));
	launcher.script_cursor = launcher.script;
	launcher.script_next = SDL_GetTicks() + 700;
	platform_mouse_capture(FALSE);
	SDL_ShowCursor();
	scan();
	modes_find();
	if (launcher.mode < 0 || launcher.mode >= launcher.mode_count || !launcher.modes[launcher.mode].installed)
		launcher.mode = mode_default();
	while (!launcher.done)
	{
		Uint64 now;

		events();
		script_step();
		poll_work();
		if (launcher.done)
			break;
		frame();
		/* (at most about 120 frames a second: a static screen) */
		now = SDL_GetTicks();
		if (now - last < 8)
			SDL_Delay((Uint32)(8 - (now - last)));
		last = SDL_GetTicks();
	}
	free(launcher.upload.body);
	launcher.upload.body = NULL;
	platform_mouse_capture(TRUE);
}

/* whether the window can be drawn in yet: with the bgfx renderer, only once
the game's memory is up (launcher_at_start, from the device's start), as
bgfx starts with the device's state; the first start's Welcome comes before
that, and keeps the message boxes there */
static int drawable(int early)
{
#ifdef HALO_BGFX
	return !early || !platform_bgfx_chosen();
#else
	(void)early;
	return 1;
#endif
}

int launcher_welcome(char *destination, unsigned long size)
{
	/* (the window's own launcher_at_start: not again) */
	launcher.shown = 1;
	if (!drawable(1) || !platform_video_initialize(640, 480))
		return 0;
	platform_log("launcher: no Xbox maps yet: Welcome");
	memset(&launcher.families, 0, sizeof(launcher.families));
	launcher.welcome = 1;
	launcher.destination = destination;
	launcher.destination_size = size;
	set_root(destination);
	launcher.screen = _screen_welcome;
	launcher.selected = 0;
	launcher.scroll = 0;
	launcher.in_panel = 1;
	run();
	launcher.welcome = 0;
	launcher.destination = NULL;
	return xbox_maps_ready();
}

void launcher_open_on_maps(void)
{
	launcher.open_on_maps = 1;
}

/* the command line: --setup, --play, --mode <id> (or --mode=<id>), and
whether it joins a game (an invite) */
static void command_line(int *setup, int *play, char *mode, size_t mode_size, int *joining)
{
	char argument[512];
	int index;

	*setup = *play = *joining = 0;
	mode[0] = '\0';
	for (index = 1; posix_command_line_argument(index, argument, sizeof(argument)); index++)
	{
		if (!strcmp(argument, "--setup"))
			*setup = 1;
		else if (!strcmp(argument, "--play"))
			*play = 1;
		else if (!strncmp(argument, "--mode=", 7))
			snprintf(mode, mode_size, "%s", argument + 7);
		else if (!strcmp(argument, "--mode"))
		{
			if (posix_command_line_argument(index + 1, argument, sizeof(argument)))
			{
				snprintf(mode, mode_size, "%s", argument);
				index++;
			}
		}
		/* (Discord's discord-<application>:// starts the game for a Join,
		which comes once it is up: p2p_discord.c) */
		else if (!SDL_strncasecmp(argument, "halo://", 7) || !SDL_strncasecmp(argument, "discord-", 8) ||
			strstr(argument, "connect"))
			*joining = 1;
	}
}

/* whether the setup screen may open at start by itself (the mode question,
launcher.show_at_start, "ask each time"): not under gamescope, the Steam
Deck's Game Mode, where it opens only when asked for (Back held, --setup,
SETUP in the menus) */
static int launcher_opens_by_itself(void)
{
	return !updater_gamescope();
}

/* whether the game runs where Steam Input brings its controllers, which can
take a moment or two to show them: gamescope (the Steam Deck's Game Mode)
or the Deck's Desktop Mode (SteamDeck). Not a desktop's Steam
(SteamAppId alone), where a player with no controller would wait each
start */
static int launcher_started_by_steam(void)
{
	const char *deck = getenv("SteamDeck");

	return updater_gamescope() || (deck && deck[0] && strcmp(deck, "0"));
}

/* how long held_at_start waits for a held button: a pad's first report,
and where the system can't say, the window's keyboard focus, take a moment
(shift_unknown: posix_shift_key_held can't say). Under Steam, until a pad
shows up (Steam Input's, up to 2 s) and then its first report; elsewhere
no wait at all with no pad, as the desktop's start is not slowed */
#define HELD_WAIT_FOR_A_PAD_UNDER_STEAM_MS 2000
#define HELD_WAIT_FOR_A_REPORT_MS 120
#define HELD_WAIT_FOR_THE_KEYBOARD_MS 250

static Uint64 held_wait(int steam, int pads, int shift_unknown)
{
	if (!pads && steam)
		return HELD_WAIT_FOR_A_PAD_UNDER_STEAM_MS;
	if (!pads && !shift_unknown)
		return 0;
	return shift_unknown ? HELD_WAIT_FOR_THE_KEYBOARD_MS : HELD_WAIT_FOR_A_REPORT_MS;
}

/* Shift, or a controller's Back (View), held as the game starts */
static int held_at_start(void)
{
	int shift = posix_shift_key_held();
	int steam = launcher_started_by_steam();
	SDL_JoystickID *pads;
	int count = 0, index, held = shift > 0, had_pads;
	Uint64 started = SDL_GetTicks(), now, until;

	if (held)
		return 1;
	pads = SDL_GetGamepads(&count);
	for (index = 0; pads && index < count; index++)
		SDL_OpenGamepad(pads[index]);
	SDL_free(pads);
	had_pads = count > 0;
	until = SDL_GetTicks() + held_wait(steam, count, shift < 0);
	if (until <= SDL_GetTicks() && !count && shift >= 0)
		return 0;
	while (!held && SDL_GetTicks() < until)
	{
		SDL_PumpEvents();
		SDL_UpdateGamepads();
		pads = SDL_GetGamepads(&count);
		for (index = 0; pads && index < count; index++)
		{
			/* (one that showed up as it waited: opened, and its first
			report waited for) */
			SDL_Gamepad *gamepad = SDL_GetGamepadFromID(pads[index]);

			if (!gamepad)
				gamepad = SDL_OpenGamepad(pads[index]);
			if (gamepad && SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK))
				held = 1;
		}
		SDL_free(pads);
		if (count && !had_pads)
		{
			had_pads = 1;
			now = SDL_GetTicks();
			if (now + HELD_WAIT_FOR_A_REPORT_MS < until)
				until = now + HELD_WAIT_FOR_A_REPORT_MS;
			platform_log("launcher: a controller showed up %llu ms into the start",
				(unsigned long long)(now - started));
		}
		if (shift < 0 && (SDL_GetModState() & SDL_KMOD_SHIFT))
			held = 1;
		SDL_Delay(10);
	}
	return held;
}

/* the question, once a second mode is installed: be asked each time, or
always play the one the player has now */
static void mode_question(void)
{
	launcher.dialog = _dialog_mode_question;
	launcher.dialog_selected = 0;
	snprintf(launcher.dialog_title, sizeof(launcher.dialog_title), "HOW DO YOU WANT TO PLAY?");
	snprintf(launcher.dialog_text, sizeof(launcher.dialog_text),
		"%s", reopen_hint());
	launcher.dialog_text_color = COLOR_WHITE;
	launcher.dialog_row_count = 2;
	memset(launcher.dialog_rows, 0, sizeof(launcher.dialog_rows));
	launcher.dialog_rows[0].action = _action_ask_each_time;
	snprintf(launcher.dialog_rows[0].label, sizeof(launcher.dialog_rows[0].label), "ASK EACH TIME");
	launcher.dialog_rows[1].action = _action_always_play;
	snprintf(launcher.dialog_rows[1].label, sizeof(launcher.dialog_rows[1].label), "ALWAYS PLAY %s",
		launcher.modes[launcher.mode].name);
}

void launcher_at_start(void)
{
	int setup, play_now, joining, open = 0, question = 0;
	char mode[64];
	const char *because = "";

	if (launcher.shown || platform_run_is_unwatched() || !drawable(0))
		return;
	command_line(&setup, &play_now, mode, sizeof(mode), &joining);
	set_root(platform_data_root());
	modes_find();
	launcher.mode = mode_default();
	if (mode[0])
	{
		int index = mode_find(mode);

		if (index >= 0 && launcher.modes[index].installed)
			launcher.mode = index;
		else
			platform_log("launcher: --mode %s: no such mode here; playing the default", mode);
	}
	if (launcher.open_on_maps)
	{
		open = 2;
		because = "the maps folder isn't the Xbox maps";
	}
	else if (setup)
	{
		open = 1;
		because = "of --setup (SETUP in the menus starts it so)";
	}
	else if (!joining && !play_now && !mode[0])
	{
		/* (under gamescope, the Steam Deck's Game Mode, only when asked for:
		a held Back or Shift, --setup, SETUP in the menus. The mode question,
		show_at_start and "ask each time" play the default there, as before
		0.9.1: launcher_opens_by_itself) */
		int always = !strcmp(config_string("launcher.show_at_start"), "always");
		int asked = launcher.installed_modes > 1 && !config_chosen("launcher.mode_choice");
		int asks = launcher.installed_modes > 1 && mode_asks();

		if ((always || asked || asks) && !launcher_opens_by_itself())
		{
			platform_log("launcher: under gamescope, not opened by itself (%s): playing %s; hold Back as it "
				"starts, or SETUP in the menus, for the setup screen",
				always ? "launcher.show_at_start is always" : asked ? "the mode question" : "ask each time",
				launcher.mode >= 0 && launcher.mode < launcher.mode_count ? launcher.modes[launcher.mode].id :
				"vanilla");
			always = asked = asks = 0;
		}
		if (always)
		{
			open = 1;
			because = "launcher.show_at_start is always";
		}
		else if (config_boolean("launcher.hold_to_open") && held_at_start())
		{
			open = 1;
			because = "Back or Shift was held";
		}
		else if (asked)
		{
			open = question = 1;
			because = "of the mode question";
		}
		else if (asks)
		{
			open = 1;
			because = "launcher.mode_choice asks each time";
		}
	}
	if (!open)
	{
		/* (a mode named, or the default, for this run) */
		mode_apply(launcher.mode, mode[0] != '\0');
		return;
	}
	if (updater_gamescope())
		platform_log("launcher: opened at start under gamescope because %s", because);
	launcher.welcome = 0;
	launcher.in_panel = 1;
	if (open == 2)
	{
		platform_log("launcher: the maps folder isn't the Xbox maps: Maps");
		launcher.section = _section_maps;
		enter_screen(_screen_maps);
		scan();
		if (launcher.families[0].ui_version == MAP_VERSION_HALO_PC ||
			launcher.families[0].ui_version == MAP_VERSION_CUSTOM_EDITION)
			set_status(COLOR_AMBER, "Your maps folder has Halo PC maps. Chupathingy needs the Xbox maps there.");
		else
			set_status(COLOR_AMBER, "Add your Xbox maps to play.");
	}
	else
	{
		platform_log("launcher: opened at start");
		launcher.section = _section_play;
		enter_screen(_screen_play);
		launcher.selected = launcher.mode;
		if (question)
			mode_question();
	}
	run();
}

#endif
