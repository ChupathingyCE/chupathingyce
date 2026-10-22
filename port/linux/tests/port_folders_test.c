/*
PORT_FOLDERS_TEST.C

Tests of the game's own folders beside the maps and the saves
(port/linux/src/port_folders.c). Built and run by tools/test_port_folders.py;
exits nonzero on a failure. It writes nothing.
*/

#include "../src/port_folders.h"

#include <stdio.h>
#include <string.h>

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

static void check_path(enum port_folder folder, const char *override, const char *data_root, const char *save_root,
	const char *expected)
{
	char path[256];
	int found = port_folder_path(folder, override, data_root, save_root, path, sizeof(path));

	if (expected ? !found || strcmp(path, expected) : found)
	{
		printf("FAIL %s with override \"%s\", data \"%s\", saves \"%s\": got %d \"%s\", expected \"%s\"\n",
			port_folder_name(folder), override ? override : "(none)", data_root ? data_root : "(none)",
			save_root ? save_root : "(none)", found, path, expected ? expected : "(none)");
		failures++;
	}
}

static void check_file(const char *folder, const char *value, const char *expected)
{
	char path[256];
	int found = port_folder_file(folder, value, path, sizeof(path));

	if (expected ? !found || strcmp(path, expected) : found)
	{
		printf("FAIL file \"%s\" in \"%s\": got %d \"%s\", expected \"%s\"\n", value ? value : "(none)",
			folder ? folder : "(none)", found, path, expected ? expected : "(none)");
		failures++;
	}
}

int main(void)
{
	char small[8];

	/* names, and which root each is in */
	CHECK(!strcmp(port_folder_name(_port_folder_data), "data"));
	CHECK(!strcmp(port_folder_name(_port_folder_logs), "logs"));
	CHECK(!strcmp(port_folder_name(_port_folder_cache), "cache"));
	CHECK(!strcmp(port_folder_name(_port_folder_private), "private"));
	CHECK(!strcmp(port_folder_name(_port_folder_gametypes), "gametypes"));
	CHECK(!strcmp(port_folder_name(k_port_folder_count), ""));
	CHECK(!port_folder_in_save_root(_port_folder_data));
	CHECK(!port_folder_in_save_root(_port_folder_logs));
	CHECK(port_folder_in_save_root(_port_folder_cache));
	CHECK(port_folder_in_save_root(_port_folder_private));
	CHECK(port_folder_in_save_root(_port_folder_gametypes));

	/* each in its root (desktop, Docker, Android, Windows) */
	check_path(_port_folder_data, NULL, "/home/p/halo", "/home/p/.local/share/chupathingyce", "/home/p/halo/data");
	check_path(_port_folder_logs, NULL, "/home/p/halo", "/home/p/.local/share/chupathingyce", "/home/p/halo/logs");
	check_path(_port_folder_cache, NULL, "/home/p/halo", "/home/p/.local/share/chupathingyce",
		"/home/p/.local/share/chupathingyce/cache");
	check_path(_port_folder_private, "", "/home/p/halo", "/home/p/.local/share/chupathingyce",
		"/home/p/.local/share/chupathingyce/private");
	check_path(_port_folder_logs, "", "/data", "/data/saves", "/data/logs");
	check_path(_port_folder_cache, "/data/cache", "/data", "/data/saves", "/data/cache");
	check_path(_port_folder_logs, NULL, "/storage/emulated/0/Android/data/dev.horrible.chupathingyce/files",
		"/storage/emulated/0/Android/data/dev.horrible.chupathingyce/files/save",
		"/storage/emulated/0/Android/data/dev.horrible.chupathingyce/files/logs");
	check_path(_port_folder_logs, NULL, "C:\\Games\\Halo", "C:\\Users\\p\\AppData\\Roaming\\ChupathingyCE",
		"C:\\Games\\Halo/logs");
	check_path(_port_folder_logs, NULL, ".", NULL, "./logs");
	/* (a root's trailing separators are not doubled) */
	check_path(_port_folder_logs, NULL, "/data/", "/data/saves", "/data/logs");
	check_path(_port_folder_cache, NULL, "/data", "/data/saves//", "/data/saves/cache");
	check_path(_port_folder_logs, NULL, "/", NULL, "/logs");
	check_path(_port_folder_logs, NULL, "C:\\", NULL, "C:\\logs");

	/* overrides: logs and cache only, as they are */
	check_path(_port_folder_logs, "/tmp/run1/logs/", "/data", "/data/saves", "/tmp/run1/logs");
	check_path(_port_folder_cache, "relative/cache", "/data", "/data/saves", "relative/cache");
	check_path(_port_folder_data, "/elsewhere", "/data", "/data/saves", "/data/data");
	check_path(_port_folder_private, "/elsewhere", "/data", "/data/saves", "/data/saves/private");
	check_path(_port_folder_gametypes, "/elsewhere", "/data", "/data/saves", "/data/saves/gametypes");

	/* no root */
	check_path(_port_folder_logs, NULL, NULL, "/data/saves", NULL);
	check_path(_port_folder_cache, NULL, "/data", "", NULL);
	check_path(k_port_folder_count, NULL, "/data", "/data/saves", NULL);
	/* too long */
	CHECK(!port_folder_path(_port_folder_private, NULL, "/data", "/data/saves", small, sizeof(small)));
	CHECK(small[0] == '\0');

	/* absolute paths */
	CHECK(port_folder_absolute("/usr/share/fonts/a.ttf"));
	CHECK(port_folder_absolute("\\\\server\\share\\a.ttf"));
	CHECK(port_folder_absolute("C:\\Windows\\Fonts\\a.ttf"));
	CHECK(port_folder_absolute("c:/fonts/a.ttf"));
	CHECK(!port_folder_absolute("a.ttf"));
	CHECK(!port_folder_absolute("fonts/a.ttf"));
	CHECK(!port_folder_absolute(""));
	CHECK(!port_folder_absolute(NULL));

	/* a setting's file: relative in the folder, absolute as it is */
	check_file("/data/data/fonts", "MyFont.ttf", "/data/data/fonts/MyFont.ttf");
	check_file("/data/data/fonts", "sub/MyFont.otf", "/data/data/fonts/sub/MyFont.otf");
	check_file("/data/data/fonts", "./MyFont.ttf", "/data/data/fonts/./MyFont.ttf");
	check_file("/data/data/fonts", "/usr/share/fonts/a.ttf", "/usr/share/fonts/a.ttf");
	check_file("/data/data/fonts", "C:\\Windows\\Fonts\\a.ttf", "C:\\Windows\\Fonts\\a.ttf");
	check_file("/data/data/fonts", "..a.ttf", "/data/data/fonts/..a.ttf");
	/* (refused: one that leaves the folder, none, or no folder) */
	check_file("/data/data/fonts", "../secret.ttf", NULL);
	check_file("/data/data/fonts", "sub/../../a.ttf", NULL);
	check_file("/data/data/fonts", "sub\\..\\a.ttf", NULL);
	check_file("/data/data/fonts", "..", NULL);
	check_file("/data/data/fonts", "", NULL);
	check_file("/data/data/fonts", NULL, NULL);
	check_file("", "a.ttf", NULL);
	check_file(NULL, "a.ttf", NULL);

	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("port folders: all passed\n");
	return 0;
}
