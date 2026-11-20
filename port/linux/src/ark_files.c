/*
ARK_FILES.C

Where the player's own files for Project Ark are, for the mode
(port/linux/game/ark_mode.c): maps_ark beside maps in the data root, a copy
of (or a link to) the player's own folder of them from MCC.

A folder counts once it holds shared.map, which every one of its maps needs.
*/

#include "ark_files.h"
#include "platform.h"
#include "posix.h"

#include <stdio.h>
#include <string.h>

#ifdef HALO_ARK

#define ARK_FILES_PATH_SIZE 1024

static int searched;
static int found_local;
static char found[ARK_FILES_PATH_SIZE];

static int holds_ark(const char *folder)
{
	char on_disk[256];

	return posix_find_entry_case_insensitive(folder, "shared.map", on_disk, sizeof(on_disk));
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
	if (found[0])
		platform_log("ark: the player's files are in %s", ARK_FILES_FOLDER);
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
