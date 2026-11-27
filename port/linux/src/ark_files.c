/*
ARK_FILES.C

Where the player's own files for Project Ark are, for the mode
(port/linux/game/ark_mode.c). Two places, in this order:
- maps_ark beside maps in the data root: a copy of (or a link to) the
  player's own folder of them from MCC;
- MCC's install in a Steam library (steam_workshop.c finds it, as it does for
  the Workshop maps' shared files), read in place: nothing is copied.

A folder counts once it holds shared.map, which every one of its maps needs,
as an MCC cache file of the mode's version, and textures.dat beside it (MCC's
Halo 1 maps have the same version, but neither file). In MCC's install the
folder is found by what it holds, a game's folder or one folder below it.
*/

#include "ark_files.h"
#include "posix.h"
#include "steam_workshop.h"

#include <stdio.h>
#include <string.h>

/* (platform.h's, without the rest of it: tools/test_ark_mode.py builds this
unit alone) */
const char *platform_data_root(void);
void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

#ifdef HALO_ARK

#define ARK_FILES_PATH_SIZE STEAM_WORKSHOP_PATH_SIZE

/* an MCC cache file's header: its signature ('head', little-endian) and the
mode's maps' version */
#define ARK_CACHE_SIGNATURE 0x68656164UL
#define ARK_CACHE_VERSION 13UL

static int searched;
static int found_local;
static char found[ARK_FILES_PATH_SIZE];

static unsigned long read_le32(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

static int holds_ark(const char *folder)
{
	char on_disk[256], path[ARK_FILES_PATH_SIZE];
	unsigned char header[8];
	FILE *file;
	int result = 0;

	if (!posix_find_entry_case_insensitive(folder, "textures.dat", on_disk, sizeof(on_disk)) ||
		!posix_find_entry_case_insensitive(folder, "shared.map", on_disk, sizeof(on_disk)) ||
		snprintf(path, sizeof(path), "%s/%s", folder, on_disk) >= (int)sizeof(path))
	{
		return 0;
	}
	file = fopen(path, "rb");
	if (file)
	{
		result = fread(header, 1, sizeof(header), file) == sizeof(header) &&
			read_le32(header) == ARK_CACHE_SIGNATURE && read_le32(header + 4) == ARK_CACHE_VERSION;
		fclose(file);
	}
	return result;
}

static int is_folder(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && (information.flags & _posix_file_is_directory);
}

/* a folder in MCC's install (a game's, or one below it) holding the mode's
maps: 1 and its path in found */
static int search_install(const char *install)
{
	void *games = posix_directory_open(install);
	char game[256], below[256], path[ARK_FILES_PATH_SIZE], inner[ARK_FILES_PATH_SIZE];
	int result = 0;

	while (games && !result && posix_directory_next(games, game, sizeof(game)))
	{
		void *folders;

		if (snprintf(path, sizeof(path), "%s/%s", install, game) >= (int)sizeof(path) || !is_folder(path))
			continue;
		if (holds_ark(path))
		{
			snprintf(found, sizeof(found), "%s", path);
			result = 1;
			break;
		}
		folders = posix_directory_open(path);
		while (folders && !result && posix_directory_next(folders, below, sizeof(below)))
		{
			if (snprintf(inner, sizeof(inner), "%s/%s", path, below) < (int)sizeof(inner) && is_folder(inner) &&
				holds_ark(inner))
			{
				snprintf(found, sizeof(found), "%s", inner);
				result = 1;
			}
		}
		if (folders)
			posix_directory_close(folders);
	}
	if (games)
		posix_directory_close(games);
	return result;
}

static void search(void)
{
	char on_disk[256], path[ARK_FILES_PATH_SIZE];
	const char *root = platform_data_root();

	searched = 1;
	found[0] = 0;
	found_local = 0;
	if (root && root[0] && posix_find_entry_case_insensitive(root, ARK_FILES_FOLDER, on_disk, sizeof(on_disk)) &&
		snprintf(path, sizeof(path), "%s/%s", root, on_disk) < (int)sizeof(path) && holds_ark(path))
	{
		snprintf(found, sizeof(found), "%s", path);
		found_local = 1;
	}
	else
	{
		char install[ARK_FILES_PATH_SIZE];
		int which;

		for (which = 0; !found[0] && steam_workshop_mcc_install(which, install, sizeof(install)); which++)
			search_install(install);
	}
	if (found[0])
		platform_log("ark: the player's files are in %s", found_local ? ARK_FILES_FOLDER : "MCC's install");
}

int ark_files_folder(char *path, unsigned long size)
{
	if (!searched)
		search();
	if (!found[0] || strlen(found) >= size)
		return 0;
	if (path)
		snprintf(path, size, "%s", found);
	return 1;
}

int ark_files_local(void)
{
	if (!searched)
		search();
	return found_local;
}

#else

int ark_files_folder(char *path, unsigned long size)
{
	(void)path;
	(void)size;
	return 0;
}

int ark_files_local(void)
{
	return 0;
}

#endif
