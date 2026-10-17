/*
MAP_FILE_HASH.C

Map files' whole-file hash prefixes (map_file_hash.h), each read once for
the run: a map is a few to a few hundred MB, and the game asks for its
file again whenever it looks for the map.
*/

#include "platform.h"
#include "map_archive.h"
#include "map_file_hash.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the files whose prefixes are kept */
	MAP_FILE_HASHES_KEPT = 32,
	MAP_FILE_HASH_PATH_SIZE = 256,
};

/* ---------- globals */

struct map_file_hash_entry
{
	char path[MAP_FILE_HASH_PATH_SIZE];
	unsigned int size;
	unsigned int time_low;
	unsigned int time_high;
	unsigned int prefix;
};

static struct
{
	pthread_mutex_t lock;
	struct map_file_hash_entry entries[MAP_FILE_HASHES_KEPT];
	int count;
	/* the entry replaced next, once all are used */
	int next;
} map_file_hashes = { PTHREAD_MUTEX_INITIALIZER };

/* ---------- private code */

static int file_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	HANDLE file = *(HANDLE *)context;
	DWORD read = 0;

	if (SetFilePointer(file, (LONG)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return 0;
	return ReadFile(file, buffer, size, &read, NULL) && read == size;
}

/* ---------- public code */

int map_file_hash_prefix(const char *path, unsigned int *prefix)
{
	unsigned char hash[MAP_ARCHIVE_HASH_SIZE];
	struct map_file_hash_entry entry;
	FILETIME written;
	DWORD high = 0;
	HANDLE file;
	int index;
	int ok;

	if (!path || strlen(path) >= MAP_FILE_HASH_PATH_SIZE)
		return 0;
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	memset(&entry, 0, sizeof(entry));
	snprintf(entry.path, sizeof(entry.path), "%s", path);
	entry.size = (unsigned int)GetFileSize(file, &high);
	memset(&written, 0, sizeof(written));
	GetFileTime(file, NULL, NULL, &written);
	entry.time_low = (unsigned int)written.dwLowDateTime;
	entry.time_high = (unsigned int)written.dwHighDateTime;
	/* (no map is 4 GB, nor more than map_archive's limit) */
	if (high || entry.size > MAP_ARCHIVE_MAXIMUM_SIZE)
	{
		CloseHandle(file);
		return 0;
	}
	pthread_mutex_lock(&map_file_hashes.lock);
	for (index = 0; index < map_file_hashes.count; index++)
	{
		struct map_file_hash_entry const *kept = &map_file_hashes.entries[index];

		if (!strcmp(kept->path, entry.path) && kept->size == entry.size && kept->time_low == entry.time_low &&
			kept->time_high == entry.time_high)
		{
			*prefix = kept->prefix;
			pthread_mutex_unlock(&map_file_hashes.lock);
			CloseHandle(file);
			return 1;
		}
	}
	pthread_mutex_unlock(&map_file_hashes.lock);
	ok = map_archive_map_hash(file_read, NULL, &file, entry.size, hash);
	CloseHandle(file);
	if (!ok)
		return 0;
	entry.prefix = map_archive_hash_prefix(hash);
	*prefix = entry.prefix;
	pthread_mutex_lock(&map_file_hashes.lock);
	if (map_file_hashes.count < MAP_FILE_HASHES_KEPT)
		index = map_file_hashes.count++;
	else
	{
		index = map_file_hashes.next;
		map_file_hashes.next = (map_file_hashes.next + 1) % MAP_FILE_HASHES_KEPT;
	}
	map_file_hashes.entries[index] = entry;
	pthread_mutex_unlock(&map_file_hashes.lock);
	return 1;
}

int map_file_hash_version(const char *path, unsigned int *version)
{
	unsigned int prefix;

	if (!map_file_hash_prefix(path, &prefix))
		return 0;
	/* (0 is the version a client checks nothing for: map_archive_hash_version) */
	*version = prefix ? prefix : 1;
	return 1;
}
