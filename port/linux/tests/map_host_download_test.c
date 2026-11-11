/*
MAP_HOST_DOWNLOAD_TEST.C

A map from a game's host (port/linux/src/map_host_download.c and
map_quick_check.c), run by tools/test_map_host_download.py:

  map_host_download_test setting <text>              off | ask | always
  map_host_download_test step <setting> <state> <source> <blocked>
                                                     0 none | 1 ask | 2 start
  map_host_download_test limit <megabytes>           the cap in bytes
  map_host_download_test check <map> <name> <limit>  ok | refused: <why>
  map_host_download_test install <root> <name> <version> <offered> <limit> [block]
      the quarantine folder's <name>.map (<root>/maps_quarantine) checked and
      moved into <root>/maps_ce: ok <file it went in> | refused: <why> |
      blocked: <why>. <version> and <offered> (the map hash, or "file" for
      the file's own) in hex; with block, the blacklist holds the file's map
      hash
  map_host_download_test hash <map>                  its map hash, in hex

Its game paths (d:\...) are <root>'s files.
*/

#include "map_archive.h"
#include "map_host_download.h"
#include "map_quick_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct test_files
{
	const char *root;
	int block;
};

static void real_path(const struct test_files *files, const char *path, char *out, size_t size)
{
	size_t used;

	/* (d:\a\b as <root>/a/b) */
	if (!strncmp(path, "d:\\", 3))
		path += 3;
	snprintf(out, size, "%s/%s", files->root, path);
	for (used = strlen(files->root); out[used]; used++)
	{
		if (out[used] == '\\')
			out[used] = '/';
	}
}

static long long test_size(void *context, const char *path)
{
	char real[1024];
	struct stat status;

	real_path((const struct test_files *)context, path, real, sizeof(real));
	return stat(real, &status) == 0 ? (long long)status.st_size : -1;
}

static int test_read(void *context, const char *path, unsigned int offset, void *buffer, unsigned int size)
{
	char real[1024];
	FILE *file;
	int ok;

	real_path((const struct test_files *)context, path, real, sizeof(real));
	file = fopen(real, "rb");
	if (!file)
		return 0;
	ok = fseek(file, (long)offset, SEEK_SET) == 0 && fread(buffer, 1, size, file) == size;
	fclose(file);
	return ok;
}

static int test_move(void *context, const char *path, const char *target)
{
	char from[1024], to[1024];
	struct stat status;

	real_path((const struct test_files *)context, path, from, sizeof(from));
	real_path((const struct test_files *)context, target, to, sizeof(to));
	if (stat(to, &status) == 0)
		return 0;
	return rename(from, to) == 0;
}

static void test_remove(void *context, const char *path)
{
	char real[1024];

	real_path((const struct test_files *)context, path, real, sizeof(real));
	remove(real);
}

struct whole_file
{
	FILE *file;
};

static int whole_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	FILE *file = ((struct whole_file *)context)->file;

	return fseek(file, (long)offset, SEEK_SET) == 0 && fread(buffer, 1, size, file) == size;
}

static int file_map_hash(const char *real, unsigned char *hash)
{
	struct whole_file whole;
	struct stat status;
	int ok;

	if (stat(real, &status) != 0)
		return 0;
	whole.file = fopen(real, "rb");
	if (!whole.file)
		return 0;
	ok = map_archive_map_hash(whole_read, NULL, &whole, (unsigned int)status.st_size, hash);
	fclose(whole.file);
	return ok;
}

static int test_hashes(void *context, const char *path, unsigned char *sha256, unsigned char *map_hash)
{
	char real[1024];

	real_path((const struct test_files *)context, path, real, sizeof(real));
	/* (the SHA-256 isn't what's tested: the blacklist here names map hashes) */
	memset(sha256, 0, 32);
	return file_map_hash(real, map_hash);
}

static int test_blocked(void *context, const unsigned char *sha256, const unsigned char *map_hash, char *reason,
	int reason_size)
{
	(void)sha256;
	(void)map_hash;
	if (!((const struct test_files *)context)->block)
		return 0;
	snprintf(reason, (size_t)reason_size, "crashes games");
	return 1;
}

static void hex(const unsigned char *bytes, int count, char *text)
{
	int index;

	for (index = 0; index < count; index++)
		snprintf(text + 2 * index, 3, "%02x", bytes[index]);
}

static int parse_hex(const char *text, unsigned char *bytes, int count)
{
	int index;

	if ((int)strlen(text) != 2 * count)
		return 0;
	for (index = 0; index < count; index++)
	{
		unsigned int value;

		if (sscanf(text + 2 * index, "%2x", &value) != 1)
			return 0;
		bytes[index] = (unsigned char)value;
	}
	return 1;
}

static int memory_file_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	return whole_read(context, offset, buffer, size);
}

int main(int argc, char **argv)
{
	char error[200] = "";

	if (argc == 3 && !strcmp(argv[1], "setting"))
	{
		printf("%s\n", map_host_setting_word(map_host_setting_from(argv[2])));
		return 0;
	}
	if (argc == 6 && !strcmp(argv[1], "step"))
	{
		printf("%d\n", map_host_step(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5])));
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "limit"))
	{
		printf("%u\n", map_quick_check_limit(atol(argv[2])));
		return 0;
	}
	if (argc == 5 && !strcmp(argv[1], "check"))
	{
		struct whole_file whole;
		struct stat status;
		int ok;

		if (stat(argv[2], &status) != 0 || !(whole.file = fopen(argv[2], "rb")))
			return 2;
		ok = map_quick_check(memory_file_read, &whole, (unsigned int)status.st_size, argv[3],
			(unsigned int)strtoul(argv[4], NULL, 10), error, (int)sizeof(error));
		fclose(whole.file);
		printf(ok ? "ok\n" : "refused: %s\n", error);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "hash"))
	{
		unsigned char hash[MAP_ARCHIVE_HASH_SIZE];
		char text[2 * MAP_ARCHIVE_HASH_SIZE + 1];

		if (!file_map_hash(argv[2], hash))
			return 2;
		hex(hash, MAP_ARCHIVE_HASH_SIZE, text);
		printf("%s\n", text);
		return 0;
	}
	if ((argc == 7 || argc == 8) && !strcmp(argv[1], "install"))
	{
		static const struct map_host_files calls =
		{
			NULL, test_size, test_read, test_move, test_remove, test_hashes, test_blocked,
		};
		struct test_files files;
		struct map_host_files with = calls;
		unsigned char offered[MAP_ARCHIVE_HASH_SIZE], sha256[32], map_hash[MAP_ARCHIVE_HASH_SIZE];
		char installed[MAP_HOST_NAME_SIZE];
		int blocked = 0;

		files.root = argv[2];
		files.block = argc == 8 && !strcmp(argv[7], "block");
		with.context = &files;
		if (!strcmp(argv[5], "file"))
		{
			char part[MAP_HOST_PATH_SIZE], whole[MAP_HOST_PATH_SIZE], real[1024];

			map_host_quarantine_paths(argv[3], part, whole);
			real_path(&files, whole, real, sizeof(real));
			if (!file_map_hash(real, offered))
				memset(offered, 0, sizeof(offered));
		}
		else if (!parse_hex(argv[5], offered, MAP_ARCHIVE_HASH_SIZE))
			return 2;
		if (map_host_install(&with, argv[3], (unsigned int)strtoul(argv[4], NULL, 16), offered,
			(unsigned int)strtoul(argv[6], NULL, 10), installed, sha256, map_hash, &blocked, error, (int)sizeof(error)))
		{
			printf("ok %s\n", installed);
		}
		else
			printf("%s: %s\n", blocked ? "blocked" : "refused", error);
		return 0;
	}
	fprintf(stderr, "usage: see map_host_download_test.c\n");
	return 2;
}
