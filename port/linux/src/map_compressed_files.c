/*
MAP_COMPRESSED_FILES.C

Compressed Halo PC maps (Chimera's chimera-compress: map_compressed.c) in
the game's files (map_compressed.h). The game reads a map through its file
as it plays it (cache_files_windows.c), and hashes, checks and shares it as
a file (map_file_hash.c, ce_map_checks.c, map_archive.c), so a compressed
map is played from an expanded copy: in the save root's cache, z:\ (never
beside the maps: a player's maps folder may be read only, or shared by
several copies of the game), made the first time the map is looked for
(map_families.c) and kept for the next runs. Chimera expands into memory
when its maps-in-RAM setting is on; this game always streams a map from a
file, so its memory is the same for a compressed map as for any other.

A copy is named for the map's file, its header's checksum and the
compressed file's size, so a changed map gets a new one; the older copies
of that map's name are deleted when it is made. Copies are written under a
temporary name (tmp_..., which a save folder's copy leaves out, as it
leaves out cache*.map: save_folder.c) and renamed once whole.

The format is Chimera's, by Snowy Mouse and its contributors (GPL-3.0,
github.com/SnowyMouse/chimera).

SPDX-License-Identifier: GPL-3.0-only
*/

#ifdef HALO_COMPRESSED_MAPS

#include "platform.h"
#include "map_archive.h"
#include "map_compressed.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	MAP_COMPRESSED_PATH_SIZE = 256,
	/* the header's checksum */
	HEADER_CHECKSUM_OFFSET = 0x64,
	HEADER_FILE_SIZE_OFFSET = 0x8,
};

#define CACHE_PREFIX "z:\\cache_compressed_"
#define TEMPORARY_PREFIX "z:\\tmp_compressed_"

/* ---------- globals */

/* (one map expanded at a time: the game's thread, the server's and a map
download's may all look for one) */
static pthread_mutex_t map_compressed_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- private code */

struct expand_files
{
	HANDLE source;
	HANDLE destination;
};

static int source_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	struct expand_files *files = (struct expand_files *)context;
	DWORD read = 0;

	if (SetFilePointer(files->source, (LONG)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return 0;
	return ReadFile(files->source, buffer, size, &read, NULL) && read == size;
}

static int destination_write(void *context, const void *buffer, unsigned int size)
{
	struct expand_files *files = (struct expand_files *)context;
	DWORD written = 0;

	return WriteFile(files->destination, buffer, size, &written, NULL) && written == size;
}

static unsigned int little_long(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8) | ((unsigned int)bytes[2] << 16) |
		((unsigned int)bytes[3] << 24);
}

/* the file's first MAP_COMPRESSED_HEADER_SIZE bytes, and its size: 1 if
read */
static int header_read(const char *path, unsigned char *header, unsigned int *size)
{
	HANDLE file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD read = 0, high = 0;
	int ok;

	if (file == INVALID_HANDLE_VALUE)
		return 0;
	ok = ReadFile(file, header, MAP_COMPRESSED_HEADER_SIZE, &read, NULL) && read == MAP_COMPRESSED_HEADER_SIZE;
	if (size)
	{
		*size = (unsigned int)GetFileSize(file, &high);
		ok = ok && !high && *size != INVALID_FILE_SIZE;
	}
	CloseHandle(file);
	return ok;
}

/* whether the expanded copy at path is whole: an uncompressed map of the
version, of the size its header gives (when it gives one) */
static int copy_whole(const char *path, unsigned int version, unsigned int stated_size)
{
	unsigned char header[MAP_COMPRESSED_HEADER_SIZE];
	unsigned int size = 0;

	if (!header_read(path, header, &size))
		return 0;
	return little_long(header) == 0x68656164 && little_long(header + 4) == version &&
		(stated_size <= MAP_COMPRESSED_HEADER_SIZE || size == stated_size);
}

/* the other expanded copies of the map's name deleted */
static void older_copies_delete(const char *name, const char *kept)
{
	char pattern[MAP_COMPRESSED_PATH_SIZE];
	WIN32_FIND_DATAA data;
	HANDLE find;

	snprintf(pattern, sizeof(pattern), CACHE_PREFIX "%s_*.map", name);
	find = FindFirstFileA(pattern, &data);
	if (find == INVALID_HANDLE_VALUE)
		return;
	do
	{
		char path[MAP_COMPRESSED_PATH_SIZE];

		snprintf(path, sizeof(path), "z:\\%s", data.cFileName);
		/* (only <name>_<8 digits>_<8 digits>.map: never another map's
		whose name begins with this one's) */
		if (strlen(data.cFileName) == strlen(CACHE_PREFIX) - 3 + strlen(name) + 22 && _stricmp(path, kept))
		{
			DeleteFileA(path);
			platform_log("map: deleted an older expanded copy, %s", data.cFileName);
		}
	}
	while (FindNextFileA(find, &data));
	CloseHandle(find);
}

/* ---------- public code */

unsigned int map_compressed_file_version(const char *path)
{
	unsigned char header[MAP_COMPRESSED_HEADER_SIZE];

	return header_read(path, header, NULL) ? map_compressed_version(header) : 0;
}

int map_compressed_file_expand(const char *source, const char *destination, char *error, int error_size)
{
	struct map_compressed_io io;
	struct expand_files files;
	unsigned int expanded = 0;
	DWORD high = 0;
	int ok;

	files.source = CreateFileA(source, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (files.source == INVALID_HANDLE_VALUE)
	{
		snprintf(error, (size_t)error_size, "the compressed map could not be read");
		return 0;
	}
	files.destination = CreateFileA(destination, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (files.destination == INVALID_HANDLE_VALUE)
	{
		CloseHandle(files.source);
		snprintf(error, (size_t)error_size, "the expanded map could not be written");
		return 0;
	}
	memset(&io, 0, sizeof(io));
	io.context = &files;
	io.compressed_size = (unsigned int)GetFileSize(files.source, &high);
	io.read = source_read;
	io.write = destination_write;
	if (high || io.compressed_size == INVALID_FILE_SIZE || io.compressed_size > MAP_ARCHIVE_MAXIMUM_SIZE)
	{
		snprintf(error, (size_t)error_size, "the compressed map is too large");
		ok = 0;
	}
	else
		ok = map_compressed_expand(&io, MAP_ARCHIVE_MAXIMUM_SIZE, &expanded, error, error_size);
	CloseHandle(files.source);
	CloseHandle(files.destination);
	if (!ok)
		DeleteFileA(destination);
	else
		platform_log("map: expanded a compressed map, %u bytes into %u", io.compressed_size, expanded);
	return ok;
}

int map_compressed_file_resolve(char *path, int size)
{
	unsigned char header[MAP_COMPRESSED_HEADER_SIZE];
	char name[MAP_COMPRESSED_PATH_SIZE], copy[MAP_COMPRESSED_PATH_SIZE], temporary[MAP_COMPRESSED_PATH_SIZE];
	char error[160] = "";
	char const *base;
	unsigned int version, compressed_size = 0;
	size_t length;
	int ok = 1;

	if (!header_read(path, header, &compressed_size) || !(version = map_compressed_version(header)))
		return 1;
	/* (the map's file name, without its folder or .map) */
	base = strrchr(path, '\\');
	base = base ? base + 1 : path;
	snprintf(name, sizeof(name), "%s", base);
	length = strlen(name);
	if (length > 4 && !_stricmp(name + length - 4, ".map"))
		name[length - 4] = 0;
	snprintf(copy, sizeof(copy), CACHE_PREFIX "%s_%08x_%08x.map", name, little_long(header + HEADER_CHECKSUM_OFFSET),
		compressed_size);
	if ((int)strlen(copy) >= size)
		return 0;
	pthread_mutex_lock(&map_compressed_lock);
	if (!copy_whole(copy, version, little_long(header + HEADER_FILE_SIZE_OFFSET)))
	{
		snprintf(temporary, sizeof(temporary), TEMPORARY_PREFIX "%s.map", name);
		platform_log("map: %s is compressed (Chimera's format): expanding it into the cache", base);
		DeleteFileA(copy);
		ok = map_compressed_file_expand(path, temporary, error, sizeof(error)) && MoveFileA(temporary, copy);
		if (ok)
			older_copies_delete(name, copy);
		else
		{
			DeleteFileA(temporary);
			platform_log("map: %s could not be expanded: %s", base, error[0] ? error : "the cache could not be written");
		}
	}
	pthread_mutex_unlock(&map_compressed_lock);
	if (ok)
		snprintf(path, (size_t)size, "%s", copy);
	return ok;
}

#endif
