/*
MAP_QUICK_CHECK_FUZZ.C

libFuzzer's entry for a map from a game's host (tools/test_host_fuzz.py
builds it, target map_quick_check): the quick checks (map_quick_check.c) on
any bytes, then the install from the quarantine folder (map_host_install,
map_host_download.c) through files held in memory. The first byte picks
how: its low two bits the name, bit 2 a small cap, bit 3 the install, bit 4
the host's version from the header, bit 5 a file in the way in maps_ce, bit
6 the blacklist refusing it. The rest is the map, in an allocation of
exactly its size.
*/

#include "../src/map_archive.h"
#include "../src/map_host_download.h"
#include "../src/map_quick_check.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct memory
{
	const unsigned char *bytes;
	unsigned int size;
	int flags;
	int moves;
	int removed;
};

static int memory_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	const struct memory *memory = context;

	if (offset > memory->size || size > memory->size - offset)
		return 0;
	memcpy(buffer, memory->bytes + offset, size);
	return 1;
}

static long long files_size(void *context, const char *path)
{
	const struct memory *memory = context;

	(void)path;
	return memory->removed ? -1 : (long long)memory->size;
}

static int files_read(void *context, const char *path, unsigned int offset, void *buffer, unsigned int size)
{
	(void)path;
	return ((struct memory *)context)->removed ? 0 : memory_read(context, offset, buffer, size);
}

static int files_move(void *context, const char *path, const char *target)
{
	struct memory *memory = context;

	(void)path;
	/* (a file of the map's own name in the way: the first move refused) */
	if (!target[0] || (memory->flags & 0x20 && !memory->moves++))
		return 0;
	return 1;
}

static void files_remove(void *context, const char *path)
{
	(void)path;
	((struct memory *)context)->removed = 1;
}

static int files_hashes(void *context, const char *path, unsigned char *sha256, unsigned char *map_hash)
{
	(void)path;
	memset(sha256, 0, 32);
	return map_archive_map_hash(memory_read, NULL, context, ((struct memory *)context)->size, map_hash);
}

static int files_blocked(void *context, const unsigned char *sha256, const unsigned char *map_hash, char *reason,
	int reason_size)
{
	(void)sha256;
	(void)map_hash;
	if (!(((struct memory *)context)->flags & 0x40))
		return 0;
	memcpy(reason, "x", reason_size > 1 ? 2 : 0);
	return reason_size > 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static const char *const names[] = { "testmap", "TESTMAP", "testmap_1a2b3c4d", "bloodgulch" };
	static const struct map_host_files calls =
	{
		NULL, files_size, files_read, files_move, files_remove, files_hashes, files_blocked,
	};
	struct memory memory;
	struct map_host_files files = calls;
	unsigned char *copy, offered[MAP_ARCHIVE_HASH_SIZE], sha256[32], map_hash[MAP_ARCHIVE_HASH_SIZE];
	char error[200], installed[MAP_HOST_NAME_SIZE];
	unsigned int limit, version = 0;
	int flags, blocked;

	if (size < 1)
		return 0;
	flags = data[0];
	data++;
	size--;
	copy = malloc(size ? size : 1);
	if (!copy)
		return 0;
	memcpy(copy, data, size);
	memset(&memory, 0, sizeof(memory));
	memory.bytes = copy;
	memory.size = (unsigned int)size;
	memory.flags = flags;
	limit = flags & 4 ? 0x1000u : map_quick_check_limit(0);
	map_quick_check(memory_read, &memory, memory.size, names[flags & 3], limit, error, (int)sizeof(error));
	if (flags & 8)
	{
		files.context = &memory;
		if (!map_archive_map_hash(memory_read, NULL, &memory, memory.size, offered))
			memset(offered, 0, sizeof(offered));
		if (flags & 0x10 && size >= MAP_ARCHIVE_HEADER_SIZE)
			version = map_archive_header_checksum(copy);
		map_host_install(&files, names[flags & 3], version, offered, limit, installed, sha256, map_hash, &blocked,
			error, (int)sizeof(error));
	}
	free(copy);
	return 0;
}
