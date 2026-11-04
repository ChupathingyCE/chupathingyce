/*
MAP_DOWNLOAD_FUZZ.C

libFuzzer's entry for the bytes a map download takes in (tools/test_host_fuzz.py
builds it): what a repository's answer holds, however it got there. The first
byte of an input picks the reader: 0, a zip archive (a map's unpacking, its
header's check); 1, a compressed map; 2, a playlist's manifest; 3, a map's
name in the several forms the game reads (as a file name, a hashed name,
the settings' words). A map is written nowhere: the harness counts what is
unpacked and stops one past 64 MiB, as the player's cancel does. Each in an
allocation of exactly its size.
*/

#include "../src/map_archive.h"
#include "../src/map_compressed.h"
#include "../src/playlist_manifest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct memory
{
	const unsigned char *bytes;
	unsigned int size;
	unsigned long long written;
};

static int memory_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	const struct memory *memory = context;

	if (offset > memory->size || size > memory->size - offset)
		return 0;
	memcpy(buffer, memory->bytes + offset, size);
	return 1;
}

static int memory_write(void *context, const void *buffer, unsigned int size)
{
	struct memory *memory = context;
	volatile unsigned char sink;

	if (size)
		sink = ((const unsigned char *)buffer)[size - 1];
	(void)sink;
	memory->written += size;
	return memory->written <= (64ull << 20);
}

static int memory_stop(void *context)
{
	const struct memory *memory = context;

	return memory->written > (64ull << 20);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static const char *const names[] = { "bloodgulch", "Sneak", "a", "gulch_af352483", "..", "con.map" };
	struct memory memory;
	char error[160], text[64], base[40];
	unsigned int map_size, prefix;
	unsigned char *copy, hash[MAP_ARCHIVE_HASH_SIZE];
	char *string;
	int mode, index;

	if (size < 2)
		return 0;
	mode = data[0] & 3;
	index = data[0] >> 2;
	size--;
	data++;
	copy = malloc(size);
	if (!copy)
		return 0;
	memcpy(copy, data, size);
	memory.bytes = copy;
	memory.size = (unsigned int)size;
	memory.written = 0;
	if (mode == 0)
	{
		struct map_archive_io io;

		io.context = &memory;
		io.archive_size = memory.size;
		io.read = memory_read;
		io.write = memory_write;
		io.stop = memory_stop;
		map_archive_extract(&io, names[index % 6], &map_size, error, sizeof(error));
		/* (the header is the file's first MAP_ARCHIVE_HEADER_SIZE bytes, which the caller read) */
		if (memory.size >= MAP_ARCHIVE_HEADER_SIZE)
			map_archive_check_header(copy, memory.size, names[index % 6], (unsigned int)index * 0x01010101u, error,
				sizeof(error));
		memory.written = 0;
		map_archive_map_hash(memory_read, NULL, &memory, memory.size, hash);
	}
	else if (mode == 1)
	{
		struct map_compressed_io io;

		map_compressed_version(copy);
		io.context = &memory;
		io.compressed_size = memory.size;
		io.read = memory_read;
		io.write = memory_write;
		io.stop = memory_stop;
		map_compressed_expand(&io, 64u << 20, &map_size, error, sizeof(error));
	}
	else if (mode == 2)
	{
		static struct playlist_manifest manifest;

		playlist_manifest_parse((const char *)copy, size, &manifest, error, sizeof(error));
	}
	else
	{
		string = malloc(size + 1);
		if (string)
		{
			memcpy(string, copy, size);
			string[size] = 0;
			map_archive_name_valid(string);
			map_archive_setting(string);
			map_archive_hashed_name(string, base, sizeof(base), &prefix);
			map_archive_hashed_name(string, NULL, 0, NULL);
			map_archive_display_name(string, text, sizeof(text));
			map_archive_stock_name(string);
			map_archive_stock_message(string, text, sizeof(text));
			playlist_manifest_id_valid(string);
			playlist_manifest_file_valid(string);
			playlist_manifest_folder_valid(string);
			playlist_manifest_path_valid(string);
			free(string);
		}
	}
	free(copy);
	return 0;
}
