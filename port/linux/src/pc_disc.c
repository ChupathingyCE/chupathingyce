/*
PC_DISC.C

Writes the maps of Halo PC's retail CD, from an image of it (an ".iso"),
into maps_pc beside the maps folder, for the desktop ports' optional import
of Halo PC's maps (sdl_platform.c). The game needs only the Xbox's maps
(xiso.c); Halo PC's are played as <name>@pc (halo_map_families.h).

A CD's file system (ISO 9660, ECMA-119) is made of 2048-byte sectors.
Sector 16 on holds volume descriptors: a type byte, "CD001" and a version;
the primary one (type 1) has the root directory's record at byte 156. A
directory is a run of sectors of records, each:

	0	the record's length (0: the rest of the sector is padding)
	2	its file's or directory's first sector (32 bits, then the same
		big-endian)
	10	its size in bytes (the same)
	25	flags (2: a directory, 0x80: more of the file follows in another
		record)
	32	the name's length, then the name: uppercase, with ";1" (its
		version) after a file's ("\0" is the directory itself, "\1" the
		one holding it)

Records never cross a sector. A file is one run of sectors.

Halo PC's disc (2003) has its maps in Microsoft cabinets, FilesCab\Cab1.CAB
to Cab6.CAB: the campaign's and multiplayer's maps and its bitmaps.map,
sounds.map and ui.map, in LZX (read by cabinet.c). Every cabinet on the
disc is read, and every map in them (a .map file in a folder named maps) is
written, so another edition with its cabinets named or placed otherwise
works too, as does a disc with its maps folder stored as it is.
*/

/* (the desktop ports only: the Android app imports the game data itself) */
#ifndef HALO_ANDROID

#include "pc_disc.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	SECTOR_SIZE = 2048,
	DESCRIPTOR_SECTOR = 16,
	/* (a disc has a few volume descriptors; past these is not one) */
	MAXIMUM_DESCRIPTORS = 16,
	DESCRIPTOR_PRIMARY = 1,
	DESCRIPTOR_END = 255,
	ROOT_RECORD = 156,
	RECORD_SIZE = 33,
	FLAG_DIRECTORY = 2,
	FLAG_MORE = 0x80,
	MAXIMUM_DIRECTORY_SIZE = 4 << 20,
	MAXIMUM_FOLDER_DEPTH = 8,
	MAXIMUM_FILES = 256,
	MAXIMUM_PATH = 512,
};

struct disc_file
{
	char path[MAXIMUM_PATH];
	unsigned int sector;
	unsigned int size;
	/* a cabinet, else a map stored as it is (its name in map) */
	int cabinet;
	char map[64];
};

struct disc_files
{
	struct disc_file files[MAXIMUM_FILES];
	int count;
	int damaged;
};

static unsigned int read_u32(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] | (unsigned int)bytes[1] << 8 | (unsigned int)bytes[2] << 16 |
		(unsigned int)bytes[3] << 24;
}

static int same_name(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		int x = *a >= 'A' && *a <= 'Z' ? *a - 'A' + 'a' : *a;
		int y = *b >= 'A' && *b <= 'Z' ? *b - 'A' + 'a' : *b;

		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
}

/* the root directory's record, from the primary volume descriptor */
static int find_root(struct cabinet_import *import, unsigned char *root)
{
	int index;

	for (index = 0; index < MAXIMUM_DESCRIPTORS; index++)
	{
		unsigned char descriptor[SECTOR_SIZE];

		if (!cabinet_read(import, (unsigned long long)(DESCRIPTOR_SECTOR + index) * SECTOR_SIZE, descriptor,
			sizeof(descriptor)) || memcmp(descriptor + 1, "CD001", 5) || descriptor[0] == DESCRIPTOR_END)
		{
			return 0;
		}
		if (descriptor[0] == DESCRIPTOR_PRIMARY)
		{
			memcpy(root, descriptor + ROOT_RECORD, 34);
			return 1;
		}
	}
	return 0;
}

/* adds the cabinets and maps in a directory, and in every folder in it */
static void list_directory(struct cabinet_import *import, unsigned int sector, unsigned int size,
	const char *prefix, int depth, struct disc_files *list)
{
	unsigned char *table;
	unsigned int position = 0;

	if (list->damaged)
		return;
	if (depth > MAXIMUM_FOLDER_DEPTH || size > MAXIMUM_DIRECTORY_SIZE)
	{
		list->damaged = 1;
		return;
	}
	table = malloc(size ? size : 1);
	if (!table || !cabinet_read(import, (unsigned long long)sector * SECTOR_SIZE, table, size))
	{
		free(table);
		list->damaged = 1;
		return;
	}
	while (position < size && !list->damaged)
	{
		const unsigned char *record = table + position;
		unsigned int length = record[0];
		unsigned int name_length;
		char name[256], path[MAXIMUM_PATH];
		char *version;

		if (!length)
		{
			/* (padding to the next sector) */
			position = (position / SECTOR_SIZE + 1) * SECTOR_SIZE;
			continue;
		}
		name_length = record[32];
		if (length < RECORD_SIZE || position + length > size || RECORD_SIZE + name_length > length)
		{
			list->damaged = 1;
			break;
		}
		position += length;
		memcpy(name, record + RECORD_SIZE, name_length);
		name[name_length] = 0;
		/* (this directory, the one holding it) */
		if (name_length == 1 && (name[0] == 0 || name[0] == 1))
			continue;
		version = strchr(name, ';');
		if (version)
			*version = 0;
		if (*name && name[strlen(name) - 1] == '.')
			name[strlen(name) - 1] = 0;
		if (!*name || strchr(name, '/') || (size_t)snprintf(path, sizeof(path), "%s%s%s", prefix,
			*prefix ? "/" : "", name) >= sizeof(path))
		{
			continue;
		}

		if (record[25] & FLAG_DIRECTORY)
		{
			list_directory(import, read_u32(record + 2), read_u32(record + 10), path, depth + 1, list);
		}
		else if (!(record[25] & FLAG_MORE))
		{
			struct disc_file *file = &list->files[list->count];
			size_t length_of_name = strlen(name);
			int is_cabinet = length_of_name > 4 && same_name(name + length_of_name - 4, ".cab");

			if (!is_cabinet && !cabinet_map_name(path, file->map, sizeof(file->map)))
				continue;
			if (list->count == MAXIMUM_FILES)
			{
				list->damaged = 1;
				break;
			}
			snprintf(file->path, sizeof(file->path), "%s", path);
			file->sector = read_u32(record + 2);
			file->size = read_u32(record + 10);
			file->cabinet = is_cabinet;
			list->count++;
		}
	}
	free(table);
}

int pc_disc_is_image(const char *path)
{
	struct cabinet_import import;
	unsigned char root[34];
	char error[64];
	int result;

	if (!cabinet_import_open(&import, path, ".", "", NULL, NULL, error, sizeof(error)))
		return 0;
	result = find_root(&import, root);
	cabinet_import_close(&import);
	return result;
}

int pc_disc_extract_maps(const char *image_path, const char *destination, cabinet_progress_proc progress,
	void *context, char *error, int error_size)
{
	struct cabinet_import import;
	struct disc_files *list;
	unsigned char root[34];
	int result = 0;
	int index;

	if (!cabinet_import_open(&import, image_path, destination, "maps_pc", progress, context, error, error_size))
		return 0;
	list = calloc(1, sizeof(*list));
	if (!list)
	{
		cabinet_fail(&import, "Out of memory.%s", NULL);
		goto done;
	}
	if (!find_root(&import, root))
	{
		cabinet_fail(&import, "This is not a CD image.%s", NULL);
		goto done;
	}
	list_directory(&import, read_u32(root + 2), read_u32(root + 10), "", 0, list);
	if (list->damaged)
	{
		cabinet_fail(&import, "The CD image's file system is damaged.%s", NULL);
		goto done;
	}

	/* what there is, and what maps_pc lacks */
	for (index = 0; index < list->count; index++)
	{
		const struct disc_file *file = &list->files[index];
		unsigned long long offset = (unsigned long long)file->sector * SECTOR_SIZE;

		if (!file->cabinet)
			cabinet_count_plain_map(&import, file->map, file->size);
		else if (cabinet_is_cabinet(&import, offset, file->size) && !cabinet_count_maps(&import, offset, file->size))
			goto done;
	}
	if (!import.found)
	{
		cabinet_fail(&import, "This CD image has no Halo PC maps.%s", NULL);
		goto done;
	}
	platform_log("the Halo PC disc has %d maps, %d of them already in %s", import.found, import.kept, import.folder);

	for (index = 0; index < list->count; index++)
	{
		const struct disc_file *file = &list->files[index];
		unsigned long long offset = (unsigned long long)file->sector * SECTOR_SIZE;

		if (!file->cabinet)
		{
			if (!cabinet_copy_plain_map(&import, file->map, offset, file->size))
				goto done;
		}
		else if (cabinet_is_cabinet(&import, offset, file->size))
		{
			platform_log("reading %s", file->path);
			if (!cabinet_extract_maps(&import, offset, file->size))
				goto done;
		}
	}
	result = 1;

done:
	cabinet_import_close(&import);
	free(list);
	return result;
}

#endif
