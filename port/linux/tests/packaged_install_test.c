/*
PACKAGED_INSTALL_TEST.C

Tests of a packaged install's mark and where its files go
(port/linux/src/packaged_install.h). Built and run by
tools/test_packaged_install.py with a scratch folder as its argument (and
once more built as a packaged build, HALO_PACKAGED, with "built" as its
second); exits nonzero on a failure.
*/

#include "../src/packaged_install.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

int main(int argc, char **argv)
{
	char folder[1024], with_separator[1100], path[1100];
	FILE *file;

	if (argc < 2)
		return 2;
	if (argc >= 3 && !strcmp(argv[2], "built"))
	{
		/* (configure.py --packaged: marked whatever the environment says) */
		CHECK(packaged_install_marked(NULL, NULL));
		CHECK(packaged_install_marked("0", argv[1]));
		return failures != 0;
	}
	snprintf(folder, sizeof(folder), "%s/game", argv[1]);
	mkdir(folder, 0755);
	snprintf(with_separator, sizeof(with_separator), "%s/", folder);

	/* the environment: set to anything but a no */
	CHECK(!packaged_install_marked(NULL, NULL));
	CHECK(!packaged_install_marked(NULL, folder));
	CHECK(packaged_install_marked("1", NULL));
	CHECK(packaged_install_marked("pacman", NULL));
	CHECK(packaged_install_marked("TRUE", folder));
	CHECK(!packaged_install_marked("", folder));
	CHECK(!packaged_install_marked("0", folder));
	CHECK(!packaged_install_marked("false", folder));
	CHECK(!packaged_install_marked("No", folder));
	CHECK(!packaged_install_marked("OFF", folder));

	/* the file beside the executable (a folder of that name is not it) */
	snprintf(path, sizeof(path), "%s/" PACKAGED_INSTALL_FILE, folder);
	mkdir(path, 0755);
	CHECK(!packaged_install_marked(NULL, folder));
	rmdir(path);
	file = fopen(path, "w");
	CHECK(file != NULL);
	if (file)
	{
		fputs("manager = \"pacman\"\n", file);
		fclose(file);
	}
	CHECK(packaged_install_marked(NULL, folder));
	CHECK(packaged_install_marked(NULL, with_separator));
	/* (the environment's no does not undo the package's file) */
	CHECK(packaged_install_marked("0", folder));
	remove(path);
	CHECK(!packaged_install_marked(NULL, folder));

	/* where the data, config.toml and logs go: the save root's usual place
	for an AppImage, a packaged install, or a folder the game cannot write
	with no setup of its own; beside the executable otherwise */
	CHECK(!packaged_install_files_in_home(0, 0, 1, 0));
	CHECK(!packaged_install_files_in_home(0, 0, 1, 1));
	CHECK(!packaged_install_files_in_home(0, 0, 0, 1));
	CHECK(packaged_install_files_in_home(0, 0, 0, 0));
	CHECK(packaged_install_files_in_home(1, 0, 1, 1));
	CHECK(packaged_install_files_in_home(0, 1, 1, 1));
	CHECK(packaged_install_files_in_home(0, 1, 0, 0));

	if (!failures)
		printf("ok\n");
	return failures != 0;
}
