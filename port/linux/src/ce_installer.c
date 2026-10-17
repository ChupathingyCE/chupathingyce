/*
CE_INSTALLER.C

Writes the maps of Halo PC's Custom Edition, from its installer
(halocesetup_en_1.00.exe: Microsoft's "Games AutoRun/Setup" of 2004, a
self-extracting program, its files packed into it),
into maps_ce beside the maps folder, for the desktop ports' optional import
of Custom Edition's maps (sdl_platform.c). Custom Edition's resource maps
(bitmaps.map, sounds.map, loc.map) are what its maps, HaloMD's and Halo
PC's are read with (halo_map_families.h); the game itself needs only the
Xbox's maps (xiso.c).

The installer is a Windows program (it starts "MZ") with one Microsoft
cabinet inside it, after its code: setup's own files, then everything
installed, among them maps\bitmaps.map, sounds.map, loc.map, ui.map and the
multiplayer maps, in LZX (read by cabinet.c). Nothing in the program says
where the cabinet is, so the file is searched for a cabinet's header, and
the first one that holds maps is read.
*/

/* (the desktop ports only: the Android app imports the game data itself) */
#ifndef HALO_ANDROID

#include "ce_installer.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

enum
{
	SEARCH_CHUNK = 1 << 20,
	SIGNATURE_SIZE = 8,
};

static const unsigned char cabinet_signature[SIGNATURE_SIZE] = { 'M', 'S', 'C', 'F', 0, 0, 0, 0 };

/* where the cabinet with maps in it starts (counted into the import); 0 if
there is none */
static int find_cabinet(struct cabinet_import *import, unsigned long long *found)
{
	unsigned char *chunk;
	unsigned char start[2];
	unsigned long long offset = 0;
	char *error = import->error;
	int error_size = import->error_size;
	char ignored[64];
	int result = 0;

	if (!cabinet_read(import, 0, start, sizeof(start)) || start[0] != 'M' || start[1] != 'Z')
		return 0;
	chunk = malloc(SEARCH_CHUNK + SIGNATURE_SIZE);
	if (!chunk)
		return 0;
	/* (a cabinet that turns out to be none is not the player's reason) */
	import->error = ignored;
	import->error_size = sizeof(ignored);
	while (!result && offset < import->file_size)
	{
		unsigned long long left = import->file_size - offset;
		unsigned int size = left < SEARCH_CHUNK + SIGNATURE_SIZE ? (unsigned int)left : SEARCH_CHUNK + SIGNATURE_SIZE;
		unsigned int at;

		if (size < SIGNATURE_SIZE || !cabinet_read(import, offset, chunk, size))
			break;
		for (at = 0; at + SIGNATURE_SIZE <= size && at < SEARCH_CHUNK; at++)
		{
			if (chunk[at] != 'M' || memcmp(chunk + at, cabinet_signature, SIGNATURE_SIZE))
				continue;
			import->found = import->kept = 0;
			import->total = 0;
			if (cabinet_is_cabinet(import, offset + at, import->file_size - offset - at) &&
				cabinet_count_maps(import, offset + at, import->file_size - offset - at) && import->found)
			{
				*found = offset + at;
				result = 1;
				break;
			}
		}
		offset += SEARCH_CHUNK;
	}
	import->error = error;
	import->error_size = error_size;
	free(chunk);
	return result;
}

int ce_installer_is_installer(const char *path)
{
	struct cabinet_import import;
	unsigned long long offset;
	char error[64];
	int result;

	if (!cabinet_import_open(&import, path, ".", "maps_ce", NULL, NULL, error, sizeof(error)))
		return 0;
	result = find_cabinet(&import, &offset);
	cabinet_import_close(&import);
	return result;
}

int ce_installer_extract_maps(const char *installer_path, const char *destination, cabinet_progress_proc progress,
	void *context, char *error, int error_size)
{
	struct cabinet_import import;
	unsigned long long offset;
	int result = 0;

	if (!cabinet_import_open(&import, installer_path, destination, "maps_ce", progress, context, error,
		error_size))
	{
		return 0;
	}
	if (!find_cabinet(&import, &offset))
	{
		cabinet_fail(&import, "This is not Custom Edition's installer (halocesetup_en_1.00.exe).%s", NULL);
		goto done;
	}
	platform_log("the Custom Edition installer has %d maps, %d of them already in %s", import.found, import.kept,
		import.folder);
	result = cabinet_extract_maps(&import, offset, import.file_size - offset);

done:
	cabinet_import_close(&import);
	return result;
}

#endif
