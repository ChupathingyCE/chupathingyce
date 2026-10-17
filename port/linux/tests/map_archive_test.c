/*
MAP_ARCHIVE_TEST.C

The map downloader's checks (port/linux/src/map_archive.c), run by
tools/test_map_download.py on archives and maps it makes (and on the real
repository's, where it can reach it):

  map_archive_test name <text>                    valid | invalid
  map_archive_test setting <text>                 ask | always | never
  map_archive_test extract <zip> <name> [<out>]   ok <size> <crc32> | refused: <why>
  map_archive_test header <map> <name> <version>  ok | refused: <why>
  map_archive_test hashed <text>                  hashed <base> <digits> | plain
  map_archive_test display <text>                 the name a player knows the map by
  map_archive_test hash <map>                     its whole-file hash, in hex
  map_archive_test version <map> <host version>   <hash version> <checksum> host's | other
  map_archive_test hashversion <64 hex digits>    that hash's version (network version 29)
  map_archive_test selftest                       ok, or what failed

No file is written but <out>; nothing in an archive names a path.
*/

#include "map_archive.h"
#include "zlib_prefixed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct memory
{
	unsigned char *archive;
	unsigned int archive_size;
	unsigned char *map;
	unsigned int map_size;
	unsigned int map_capacity;
	/* (selftest: stop after this many writes, or fail them) */
	int stop_after_writes;
	int fail_writes;
	int writes;
};

static int memory_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	struct memory *memory = (struct memory *)context;

	if (offset > memory->archive_size || size > memory->archive_size - offset)
		return 0;
	memcpy(buffer, memory->archive + offset, size);
	return 1;
}

static int memory_write(void *context, const void *buffer, unsigned int size)
{
	struct memory *memory = (struct memory *)context;

	memory->writes++;
	if (memory->fail_writes)
		return 0;
	if (memory->map_size + size > memory->map_capacity)
	{
		unsigned int capacity = (memory->map_size + size) * 2;
		unsigned char *grown = (unsigned char *)realloc(memory->map, capacity);

		if (!grown)
			return 0;
		memory->map = grown;
		memory->map_capacity = capacity;
	}
	memcpy(memory->map + memory->map_size, buffer, size);
	memory->map_size += size;
	return 1;
}

static int memory_stop(void *context)
{
	struct memory *memory = (struct memory *)context;

	return memory->stop_after_writes && memory->writes >= memory->stop_after_writes;
}

static unsigned char *read_file(const char *path, unsigned int *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = (unsigned char *)malloc(length > 0 ? (size_t)length : 1);
	if (data && length > 0 && fread(data, 1, (size_t)length, file) != (size_t)length)
	{
		free(data);
		data = NULL;
	}
	fclose(file);
	*size = (unsigned int)length;
	return data;
}

static int extract(struct memory *memory, const char *name, char *error, int error_size, unsigned int *size)
{
	struct map_archive_io io;

	memset(&io, 0, sizeof(io));
	io.context = memory;
	io.archive_size = memory->archive_size;
	io.read = memory_read;
	io.write = memory_write;
	io.stop = memory_stop;
	return map_archive_extract(&io, name, size, error, error_size);
}

static int selftest(void)
{
	static unsigned char empty[64];
	struct map_archive_io io;
	struct memory memory;
	char error[160];
	unsigned int size = 0;
	int failures = 0;

	/* (an archive over the limit, refused before anything is read) */
	memset(&memory, 0, sizeof(memory));
	memory.archive = empty;
	memory.archive_size = sizeof(empty);
	memset(&io, 0, sizeof(io));
	io.context = &memory;
	io.archive_size = MAP_ARCHIVE_MAXIMUM_SIZE + 1u;
	io.read = memory_read;
	io.write = memory_write;
	if (map_archive_extract(&io, "map", &size, error, sizeof(error)) || !strstr(error, "over"))
	{
		printf("selftest: an archive over the limit was not refused (%s)\n", error);
		failures++;
	}
	/* (a name that is not one, refused before anything is read) */
	io.archive_size = sizeof(empty);
	if (map_archive_extract(&io, "../map", &size, error, sizeof(error)))
	{
		printf("selftest: a path was taken as a map's name\n");
		failures++;
	}
	/* (not a zip) */
	if (map_archive_extract(&io, "map", &size, error, sizeof(error)) || !strstr(error, "not a zip"))
	{
		printf("selftest: zeros were taken for a zip archive (%s)\n", error);
		failures++;
	}
	/* (the settings' words) */
	if (map_archive_setting("ALWAYS") != MAP_DOWNLOADS_ALWAYS || map_archive_setting("never") != MAP_DOWNLOADS_NEVER ||
		map_archive_setting("ask") != MAP_DOWNLOADS_ASK || map_archive_setting("") != MAP_DOWNLOADS_ASK ||
		map_archive_setting(NULL) != MAP_DOWNLOADS_ASK || map_archive_setting("sometimes") != MAP_DOWNLOADS_ASK ||
		strcmp(map_archive_setting_word(MAP_DOWNLOADS_NEVER), "never"))
	{
		printf("selftest: network.map_downloads's words\n");
		failures++;
	}
	if (!failures)
		printf("ok\n");
	return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	char error[160] = "";

	if (argc == 3 && !strcmp(argv[1], "name"))
	{
		printf("%s\n", map_archive_name_valid(argv[2]) ? "valid" : "invalid");
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "setting"))
	{
		printf("%s\n", map_archive_setting_word(map_archive_setting(argv[2])));
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "selftest"))
		return selftest();
	if ((argc == 4 || argc == 5 || argc == 6) && !strcmp(argv[1], "extract"))
	{
		struct memory memory;
		unsigned int size = 0;

		memset(&memory, 0, sizeof(memory));
		memory.archive = read_file(argv[2], &memory.archive_size);
		if (!memory.archive)
		{
			printf("cannot read %s\n", argv[2]);
			return 2;
		}
		/* (a sixth argument: stop after that many writes, as a cancel) */
		if (argc == 6)
			memory.stop_after_writes = atoi(argv[5]);
		if (extract(&memory, argv[3], error, sizeof(error), &size))
		{
			if (size != memory.map_size)
			{
				printf("wrote %u bytes, said %u\n", memory.map_size, size);
				return 1;
			}
			printf("ok %u %08lx\n", size, (unsigned long)z_crc32(0L, memory.map, size));
			if (argc >= 5 && strcmp(argv[4], "-"))
			{
				FILE *out = fopen(argv[4], "wb");

				if (!out || fwrite(memory.map, 1, size, out) != size)
					return 2;
				fclose(out);
			}
		}
		else
			printf("refused: %s (%u bytes written)\n", error, memory.map_size);
		free(memory.archive);
		free(memory.map);
		return 0;
	}
	if (argc == 5 && !strcmp(argv[1], "header"))
	{
		unsigned int size = 0;
		unsigned char *map = read_file(argv[2], &size);
		unsigned char header[MAP_ARCHIVE_HEADER_SIZE];

		if (!map)
		{
			printf("cannot read %s\n", argv[2]);
			return 2;
		}
		memset(header, 0, sizeof(header));
		memcpy(header, map, size < sizeof(header) ? size : sizeof(header));
		if (map_archive_check_header(header, size, argv[3], (unsigned int)strtoul(argv[4], NULL, 16), error,
			sizeof(error)))
		{
			printf("ok\n");
		}
		else
			printf("refused: %s\n", error);
		free(map);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "hashed"))
	{
		char base[64];
		unsigned int prefix = 0;

		if (map_archive_hashed_name(argv[2], base, (int)sizeof(base), &prefix))
			printf("hashed %s %08x\n", base, prefix);
		else
			printf("plain\n");
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "stock"))
	{
		char text[384];

		printf("%d\n", map_archive_stock_name(argv[2]));
		if (map_archive_stock_message(argv[2], text, (int)sizeof(text)))
			printf("%s\n", text);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "display"))
	{
		char display[64];

		map_archive_display_name(argv[2], display, (int)sizeof(display));
		printf("%s\n", display);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "version"))
	{
		struct memory memory;
		unsigned char hash[MAP_ARCHIVE_HASH_SIZE];
		unsigned int checksum, hash_version;

		memset(&memory, 0, sizeof(memory));
		memory.archive = read_file(argv[2], &memory.archive_size);
		if (!memory.archive || memory.archive_size < MAP_ARCHIVE_HEADER_SIZE)
		{
			printf("cannot read %s\n", argv[2]);
			free(memory.archive);
			return 2;
		}
		if (!map_archive_map_hash(memory_read, NULL, &memory, memory.archive_size, hash))
		{
			printf("refused: not read whole\n");
			free(memory.archive);
			return 1;
		}
		checksum = map_archive_header_checksum(memory.archive);
		hash_version = map_archive_hash_version(hash);
		printf("%08x %08x %s\n", hash_version, checksum,
			map_archive_version_matches((unsigned int)strtoul(argv[3], NULL, 16), checksum, hash_version) ?
			"host's" : "other");
		free(memory.archive);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "hashversion"))
	{
		unsigned char hash[MAP_ARCHIVE_HASH_SIZE];
		int index;

		if (strlen(argv[2]) != MAP_ARCHIVE_HASH_SIZE * 2)
			return 2;
		for (index = 0; index < MAP_ARCHIVE_HASH_SIZE; index++)
		{
			char pair[3] = { argv[2][index * 2], argv[2][index * 2 + 1], 0 };

			hash[index] = (unsigned char)strtoul(pair, NULL, 16);
		}
		printf("%08x\n", map_archive_hash_version(hash));
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "hash"))
	{
		struct memory memory;
		unsigned char hash[MAP_ARCHIVE_HASH_SIZE];
		int index;

		memset(&memory, 0, sizeof(memory));
		memory.archive = read_file(argv[2], &memory.archive_size);
		if (!memory.archive)
		{
			printf("cannot read %s\n", argv[2]);
			return 2;
		}
		if (!map_archive_map_hash(memory_read, NULL, &memory, memory.archive_size, hash))
		{
			printf("refused: not read whole\n");
			return 1;
		}
		for (index = 0; index < MAP_ARCHIVE_HASH_SIZE; index++)
			printf("%02x", hash[index]);
		printf(" %08x\n", map_archive_hash_prefix(hash));
		free(memory.archive);
		return 0;
	}
	fprintf(stderr, "usage: see map_archive_test.c\n");
	return 2;
}
