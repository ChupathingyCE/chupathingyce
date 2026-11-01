/*
GAMETYPE_FOLDER_TEST.C

Tests of the player's game type files (port/linux/src/gametype_folder.c):
their names, the list of a folder's, and files written whole, read and
deleted. Built and run by tools/test_gametype_folder.py with a scratch
folder as its argument; exits nonzero on a failure. Everything it writes
is under that folder.
*/

#include "../src/gametype_folder.h"

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

static void write_raw(const char *folder, const char *name, const char *text)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", folder, name);
	file = fopen(path, "wb");
	if (file)
	{
		fputs(text, file);
		fclose(file);
	}
}

int main(int argc, char **argv)
{
	char folder[1024];
	char names[8][GAMETYPE_FOLDER_NAME_SIZE];
	char text[64];
	char big[GAMETYPE_FOLDER_TEXT_SIZE + 2];

	if (argc < 2)
		return 2;
	/* the names a server's game type takes */
	CHECK(gametype_folder_name_valid("ctf_pro"));
	CHECK(gametype_folder_name_valid("oddball50"));
	CHECK(gametype_folder_name_valid("9-lives"));
	CHECK(!gametype_folder_name_valid(""));
	CHECK(!gametype_folder_name_valid("_x"));
	CHECK(!gametype_folder_name_valid("CTF"));
	CHECK(!gametype_folder_name_valid("../x"));
	CHECK(!gametype_folder_name_valid("a b"));
	CHECK(!gametype_folder_name_valid("abcdefghijklmnopqrstuvwxyz0123456"));
	CHECK(gametype_folder_name_valid("abcdefghijklmnopqrstuvwxyz01234"));
	/* (the folder is made by the first write) */
	snprintf(folder, sizeof(folder), "%s/gametypes", argv[1]);
	CHECK(gametype_folder_list(folder, names, 8) == 0);
	CHECK(gametype_folder_read(folder, "ctf_pro", text, sizeof(text)) == -1);
	CHECK(gametype_folder_write(folder, "ctf_pro", "base = \"ctf\"\n"));
	CHECK(gametype_folder_read(folder, "ctf_pro", text, sizeof(text)) == 13 && !strcmp(text, "base = \"ctf\"\n"));
	/* written whole over the old one, nothing left beside it */
	CHECK(gametype_folder_write(folder, "ctf_pro", "base = \"slayer\"\n"));
	CHECK(gametype_folder_read(folder, "ctf_pro", text, sizeof(text)) == 16);
	CHECK(gametype_folder_write(folder, "alpha", "base = \"race\"\n"));
	/* the list: valid names only, sorted; others' files left out */
	write_raw(folder, "Upper.toml", "x");
	write_raw(folder, "notes.txt", "x");
	write_raw(folder, ".toml", "x");
	CHECK(gametype_folder_list(folder, names, 8) == 2 && !strcmp(names[0], "alpha") && !strcmp(names[1], "ctf_pro"));
	CHECK(gametype_folder_list(folder, names, 1) == 1);
	/* refused: a name that is not one, a file too large to read */
	CHECK(!gametype_folder_write(folder, "../escape", "x"));
	CHECK(gametype_folder_read(folder, "../escape", text, sizeof(text)) == -1);
	memset(big, 'a', sizeof(big) - 1);
	big[sizeof(big) - 1] = 0;
	CHECK(gametype_folder_write(folder, "big", big));
	CHECK(gametype_folder_read(folder, "big", big, GAMETYPE_FOLDER_TEXT_SIZE) == -1);
	/* deleted */
	CHECK(gametype_folder_remove(folder, "alpha"));
	CHECK(!gametype_folder_remove(folder, "alpha"));
	CHECK(gametype_folder_read(folder, "alpha", text, sizeof(text)) == -1);
	if (failures)
		printf("%d failed\n", failures);
	return failures ? 1 : 0;
}
