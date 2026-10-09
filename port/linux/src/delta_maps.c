/*
DELTA_MAPS.C

Halo PC maps' identity for Delta Peer's ce_maps capability (docs/delta.md,
"Map identity"): a map's family, its file's name, its size and the
BLAKE2b-256 hash of every byte of it, as MAP carries them (delta_wire.h).

A map file is hashed once, on a thread of its own (a Halo PC map can be
hundreds of megabytes: the game's frames never wait for it), and the hash
kept by the file's path, size and modification time, so a game on the same
map again, or a host's next round of it, reads nothing.
*/

#include "platform.h"
#include "delta_wire.h"
#include "delta_maps.h"
#include "halo_map_families.h"
#include "monocypher.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	/* the files whose hashes are kept */
	DELTA_MAPS_CACHED = 16,
	/* the bytes read at a time */
	DELTA_MAPS_CHUNK = 1 << 20,
	DELTA_MAPS_PATH_SIZE = 256
};

enum
{
	_entry_empty = 0,
	_entry_pending,
	_entry_hashed,
	_entry_failed
};

struct delta_maps_entry
{
	int state;
	char path[DELTA_MAPS_PATH_SIZE];
	delta_u32 size_low;
	delta_u32 size_high;
	delta_u32 time_low;
	delta_u32 time_high;
	unsigned char hash[DELTA_WIRE_MAP_HASH_SIZE];
	/* the order entries are replaced in */
	delta_u32 used;
};

static struct
{
	pthread_mutex_t lock;
	struct delta_maps_entry entries[DELTA_MAPS_CACHED];
	delta_u32 clock;
	/* a hashing thread is running */
	int working;
} delta_maps = { PTHREAD_MUTEX_INITIALIZER };

/* the file's size and modification time; 0 if it cannot be opened */
static int file_stamp(const char *path, delta_u32 *size_low, delta_u32 *size_high, delta_u32 *time_low,
	delta_u32 *time_high)
{
	HANDLE file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD high = 0;
	FILETIME written;

	if (file == INVALID_HANDLE_VALUE)
		return 0;
	*size_low = (delta_u32)GetFileSize(file, &high);
	*size_high = (delta_u32)high;
	memset(&written, 0, sizeof(written));
	GetFileTime(file, NULL, NULL, &written);
	*time_low = (delta_u32)written.dwLowDateTime;
	*time_high = (delta_u32)written.dwHighDateTime;
	CloseHandle(file);
	return 1;
}

/* the whole file's hash; 0 if it could not be read whole */
static int hash_file(const char *path, delta_u32 size_low, delta_u32 size_high, unsigned char *hash)
{
	HANDLE file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	crypto_blake2b_ctx context;
	unsigned char *buffer;
	unsigned long long left = ((unsigned long long)size_high << 32) | size_low;
	int ok = 1;

	if (file == INVALID_HANDLE_VALUE)
		return 0;
	buffer = (unsigned char *)malloc(DELTA_MAPS_CHUNK);
	if (!buffer)
	{
		CloseHandle(file);
		return 0;
	}
	crypto_blake2b_init(&context, DELTA_WIRE_MAP_HASH_SIZE);
	while (left && ok)
	{
		DWORD wanted = left < DELTA_MAPS_CHUNK ? (DWORD)left : DELTA_MAPS_CHUNK;
		DWORD read = 0;

		ok = ReadFile(file, buffer, wanted, &read, NULL) && read == wanted;
		if (ok)
		{
			crypto_blake2b_update(&context, buffer, read);
			left -= read;
		}
	}
	crypto_blake2b_final(&context, hash);
	free(buffer);
	CloseHandle(file);
	return ok;
}

/* the hashing thread: every pending entry, one at a time, until none is */
static void *hash_thread(void *argument)
{
	(void)argument;
	for (;;)
	{
		struct delta_maps_entry job;
		unsigned char hash[DELTA_WIRE_MAP_HASH_SIZE];
		unsigned long started;
		int index;
		int ok;

		pthread_mutex_lock(&delta_maps.lock);
		for (index = 0; index < DELTA_MAPS_CACHED && delta_maps.entries[index].state != _entry_pending; index++)
			;
		if (index == DELTA_MAPS_CACHED)
		{
			delta_maps.working = 0;
			pthread_mutex_unlock(&delta_maps.lock);
			return NULL;
		}
		job = delta_maps.entries[index];
		pthread_mutex_unlock(&delta_maps.lock);

		started = GetTickCount();
		ok = hash_file(job.path, job.size_low, job.size_high, hash);
		platform_log("Delta Peer: map identity: %s %lu MB hashed in %lu ms%s", strrchr(job.path, '\\') ?
			strrchr(job.path, '\\') + 1 : job.path, (unsigned long)((((unsigned long long)job.size_high << 32) |
			job.size_low) >> 20), (unsigned long)(GetTickCount() - started), ok ? "" : " (could not be read)");

		pthread_mutex_lock(&delta_maps.lock);
		/* (the entry may have been taken for another file meanwhile) */
		if (delta_maps.entries[index].state == _entry_pending && !strcmp(delta_maps.entries[index].path, job.path) &&
			delta_maps.entries[index].size_low == job.size_low && delta_maps.entries[index].size_high == job.size_high &&
			delta_maps.entries[index].time_low == job.time_low && delta_maps.entries[index].time_high == job.time_high)
		{
			delta_maps.entries[index].state = ok ? _entry_hashed : _entry_failed;
			memcpy(delta_maps.entries[index].hash, hash, sizeof(hash));
		}
		pthread_mutex_unlock(&delta_maps.lock);
	}
}

/* the cached hash of a file as it is now (1), being made (0), or not to be
had (-1); a hash begun if there is none */
static int file_hash(const char *path, unsigned char *hash, delta_u32 *size_low, delta_u32 *size_high)
{
	delta_u32 time_low, time_high;
	struct delta_maps_entry *entry = NULL;
	int result;
	int index;

	if (strlen(path) >= DELTA_MAPS_PATH_SIZE || !file_stamp(path, size_low, size_high, &time_low, &time_high))
		return -1;
	pthread_mutex_lock(&delta_maps.lock);
	for (index = 0; index < DELTA_MAPS_CACHED && !entry; index++)
	{
		struct delta_maps_entry *candidate = &delta_maps.entries[index];

		if (candidate->state != _entry_empty && !strcmp(candidate->path, path) && candidate->size_low == *size_low &&
			candidate->size_high == *size_high && candidate->time_low == time_low && candidate->time_high == time_high)
		{
			entry = candidate;
		}
	}
	if (!entry)
	{
		/* (an empty entry, or the one used longest ago that is not being
		hashed) */
		for (index = 0; index < DELTA_MAPS_CACHED; index++)
		{
			struct delta_maps_entry *candidate = &delta_maps.entries[index];

			if (candidate->state == _entry_pending)
				continue;
			if (!entry || candidate->state == _entry_empty ||
				(entry->state != _entry_empty && candidate->used < entry->used))
			{
				entry = candidate;
			}
		}
		if (!entry)
		{
			pthread_mutex_unlock(&delta_maps.lock);
			return 0;
		}
		memset(entry, 0, sizeof(*entry));
		entry->state = _entry_pending;
		strcpy(entry->path, path);
		entry->size_low = *size_low;
		entry->size_high = *size_high;
		entry->time_low = time_low;
		entry->time_high = time_high;
		if (!delta_maps.working)
		{
			pthread_t thread;

			delta_maps.working = 1;
			if (pthread_create(&thread, NULL, hash_thread, NULL) == 0)
				pthread_detach(thread);
			else
			{
				delta_maps.working = 0;
				entry->state = _entry_failed;
			}
		}
	}
	entry->used = ++delta_maps.clock;
	result = entry->state == _entry_hashed ? 1 : entry->state == _entry_pending ? 0 : -1;
	if (result == 1)
		memcpy(hash, entry->hash, DELTA_WIRE_MAP_HASH_SIZE);
	pthread_mutex_unlock(&delta_maps.lock);
	return result;
}

int delta_maps_identity(const char *map_name, struct delta_wire_map *map)
{
	char file[DELTA_WIRE_MAP_NAME_SIZE + 1];
	char path[DELTA_MAPS_PATH_SIZE];
	short family;
	int result;

	memset(map, 0, sizeof(*map));
	if (!map_name || !map_name[0])
		return -1;
	family = map_family_parse(map_name, file, sizeof(file));
	if (!delta_wire_map_name_valid(file))
		return -1;
	strcpy(map->name, file);
	map->family = (unsigned char)family;
	/* (an Xbox map is named alone: every copy of the game has the same) */
	if (family == _map_family_xbox)
		return 1;
#ifdef HALO_CUSTOM_EDITION
	if (!map_family_find(family, file, path, sizeof(path)))
		return -1;
	result = file_hash(path, map->hash, &map->size_low, &map->size_high);
	if (result == 1)
		map->flags = DELTA_WIRE_MAP_HASHED;
	return result;
#else
	(void)path;
	(void)result;
	return -1;
#endif
}

int delta_maps_name(const struct delta_wire_map *map, char *name, int size)
{
	if (!map || map->family >= NUMBER_OF_MAP_FAMILIES || !delta_wire_map_name_valid(map->name))
		return 0;
	snprintf(name, (size_t)size, "%s%s", map->name, map_family_suffix(map->family));
	return 1;
}
