/*
MAP_PACK_FOLDERS_TEST.C

Tests of the map packs' folders "Make the folders" makes
(port/linux/src/map_pack_folders.c), in the empty scratch folder given as
the argument. Built and run by tools/test_map_pack_folders.py; exits
nonzero on a failure.
*/

#include "../src/map_pack_folders.h"
#include "../src/posix.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static int is_directory(const char *path)
{
	struct stat information;

	return stat(path, &information) == 0 && S_ISDIR(information.st_mode);
}

/* the file's text, or "" */
static const char *read_text(const char *path)
{
	static char text[2048];
	FILE *file = fopen(path, "rb");
	size_t length = 0;

	if (file)
	{
		length = fread(text, 1, sizeof(text) - 1, file);
		fclose(file);
	}
	text[length] = 0;
	return text;
}

static void write_text(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");

	if (file)
	{
		fputs(text, file);
		fclose(file);
	}
}

/* how many entries of root are named name, in any case */
static int entries_named(const char *root, const char *name)
{
	void *directory = posix_directory_open(root);
	char entry[256];
	int count = 0;

	while (directory && posix_directory_next(directory, entry, sizeof(entry)))
	{
		if (!strcasecmp(entry, name))
			count++;
	}
	if (directory)
		posix_directory_close(directory);
	return count;
}

static void make_root(char *root, size_t size, const char *scratch, const char *name)
{
	snprintf(root, size, "%s/%s", scratch, name);
	CHECK(posix_make_directory(root) == 0);
}

/* an empty folder: all four made, each with its README.txt; no other
folder (maps_stubbs stays unlisted) */
static void test_empty(const char *scratch)
{
	char root[1024], path[1100];
	int index;

	make_root(root, sizeof(root), scratch, "empty");
	CHECK(map_pack_folders_make(root) == MAP_PACK_FOLDER_COUNT);
	for (index = 0; index < MAP_PACK_FOLDER_COUNT; index++)
	{
		snprintf(path, sizeof(path), "%s/%s", root, map_pack_folder_name(index));
		CHECK(is_directory(path));
		snprintf(path, sizeof(path), "%s/%s/README.txt", root, map_pack_folder_name(index));
		CHECK(!strcmp(read_text(path), map_pack_folder_readme(index)));
	}
	CHECK(!strcmp(map_pack_folder_name(0), "maps_cex"));
	CHECK(!strcmp(map_pack_folder_name(1), "maps_spv3"));
	CHECK(!strcmp(map_pack_folder_name(2), "maps_mcc"));
	CHECK(!strcmp(map_pack_folder_name(3), "maps_os"));
	CHECK(map_pack_folder_name(MAP_PACK_FOLDER_COUNT) == NULL);
	CHECK(map_pack_folder_readme(-1) == NULL);
	snprintf(path, sizeof(path), "%s/maps_stubbs", root);
	CHECK(!is_directory(path));
	/* (again: nothing changes) */
	CHECK(map_pack_folders_make(root) == MAP_PACK_FOLDER_COUNT);
	snprintf(path, sizeof(path), "%s/maps_cex/README.txt", root);
	CHECK(!strcmp(read_text(path), map_pack_folder_readme(0)));
}

/* a README.txt there already is kept, and a folder's files with it */
static void test_existing(const char *scratch)
{
	char root[1024], path[1100];

	make_root(root, sizeof(root), scratch, "existing");
	snprintf(path, sizeof(path), "%s/maps_cex", root);
	CHECK(posix_make_directory(path) == 0);
	snprintf(path, sizeof(path), "%s/maps_cex/README.txt", root);
	write_text(path, "the player's own\n");
	snprintf(path, sizeof(path), "%s/maps_cex/maplist.txt", root);
	write_text(path, "bloodgulch - Abyss\n");
	snprintf(path, sizeof(path), "%s/maps_mcc", root);
	CHECK(posix_make_directory(path) == 0);
	snprintf(path, sizeof(path), "%s/maps_mcc/readme.TXT", root);
	write_text(path, "lower case\n");
	CHECK(map_pack_folders_make(root) == MAP_PACK_FOLDER_COUNT);
	snprintf(path, sizeof(path), "%s/maps_cex/README.txt", root);
	CHECK(!strcmp(read_text(path), "the player's own\n"));
	snprintf(path, sizeof(path), "%s/maps_cex/maplist.txt", root);
	CHECK(!strcmp(read_text(path), "bloodgulch - Abyss\n"));
	snprintf(path, sizeof(path), "%s/maps_mcc", root);
	CHECK(entries_named(path, "README.txt") == 1);
	snprintf(path, sizeof(path), "%s/maps_mcc/readme.TXT", root);
	CHECK(!strcmp(read_text(path), "lower case\n"));
	snprintf(path, sizeof(path), "%s/maps_spv3/README.txt", root);
	CHECK(!strcmp(read_text(path), map_pack_folder_readme(1)));
}

/* a folder there in another case is that folder: no second one */
static void test_case(const char *scratch)
{
	char root[1024], path[1100];

	make_root(root, sizeof(root), scratch, "case");
	snprintf(path, sizeof(path), "%s/Maps_SPV3", root);
	CHECK(posix_make_directory(path) == 0);
	CHECK(map_pack_folders_make(root) == MAP_PACK_FOLDER_COUNT);
	CHECK(entries_named(root, "maps_spv3") == 1);
	snprintf(path, sizeof(path), "%s/Maps_SPV3/README.txt", root);
	CHECK(!strcmp(read_text(path), map_pack_folder_readme(1)));
}

/* an older name there: none made beside it (maps_ceplus is CE+'s too;
GulchCE's mcc_maps is read only while there is no maps_mcc) */
static void test_older_names(const char *scratch)
{
	char root[1024], path[1100];

	make_root(root, sizeof(root), scratch, "older");
	snprintf(path, sizeof(path), "%s/maps_ceplus", root);
	CHECK(posix_make_directory(path) == 0);
	snprintf(path, sizeof(path), "%s/mcc_maps", root);
	CHECK(posix_make_directory(path) == 0);
	CHECK(map_pack_folders_make(root) == 2);
	CHECK(entries_named(root, "maps_cex") == 0);
	CHECK(entries_named(root, "maps_mcc") == 0);
	CHECK(entries_named(root, "maps_spv3") == 1);
	CHECK(entries_named(root, "maps_os") == 1);
	snprintf(path, sizeof(path), "%s/maps_ceplus", root);
	CHECK(entries_named(path, "README.txt") == 0);
	snprintf(path, sizeof(path), "%s/mcc_maps", root);
	CHECK(entries_named(path, "README.txt") == 0);

	make_root(root, sizeof(root), scratch, "older2");
	snprintf(path, sizeof(path), "%s/maps_ce+", root);
	CHECK(posix_make_directory(path) == 0);
	CHECK(map_pack_folders_make(root) == 3);
	CHECK(entries_named(root, "maps_cex") == 0);
}

/* a root that isn't there: nothing made */
static void test_missing_root(const char *scratch)
{
	char root[1024];

	snprintf(root, sizeof(root), "%s/not_there", scratch);
	CHECK(map_pack_folders_make(root) == 0);
	CHECK(!is_directory(root));
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		printf("usage: map_pack_folders_test <empty scratch folder>\n");
		return 2;
	}
	test_empty(argv[1]);
	test_existing(argv[1]);
	test_case(argv[1]);
	test_older_names(argv[1]);
	test_missing_root(argv[1]);
	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("map pack folders: all tests passed\n");
	return 0;
}
