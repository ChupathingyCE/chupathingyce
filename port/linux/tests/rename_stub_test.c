/*
RENAME_STUB_TEST.C

Tests of the halo stub's rules (port/linux/src/rename_stub.h): which old
program the renamed game removes, and how a stub is told from the old game
or any other file. Built and run by
tools/test_rename_stub.py with tools/tests/rename_stub_halo.sh as its argument; exits
nonzero on a failure.
*/

#include "../src/rename_stub.h"

#include <stdio.h>
#include <stdlib.h>
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

static int same(const char *a, const char *b)
{
	return a && b && !strcmp(a, b);
}

/* the kind of a file of size bytes, scanned in pieces of piece bytes */
static int kind_of(const unsigned char *data, size_t size, size_t piece)
{
	struct rename_stub_scan scan;
	size_t at;

	memset(&scan, 0, sizeof(scan));
	for (at = 0; at < size; at += piece)
		rename_stub_scan_add(&scan, data + at, size - at < piece ? size - at : piece);
	return rename_stub_kind(&scan, size);
}

/* the kind, the same for every piece size */
static int kind(const unsigned char *data, size_t size)
{
	static const size_t pieces[] = { 1, 2, 3, 7, 13, 26, 27, 28, 64, 4096, 65536 };
	int first = kind_of(data, size, size ? size : 1);
	size_t index;

	for (index = 0; index < sizeof(pieces) / sizeof(*pieces); index++)
	{
		if (pieces[index] < size && size < (1u << 20) && kind_of(data, size, pieces[index]) != first)
		{
			printf("FAIL: kind %d in pieces of %lu, %d whole\n", kind_of(data, size, pieces[index]),
				(unsigned long)pieces[index], first);
			failures++;
		}
	}
	return first;
}

int main(int argc, char **argv)
{
	/* (a program bigger than any stub: the old game, Halo PC's halo.exe) */
	static unsigned char big[3u * 1024 * 1024];
	unsigned char script[8192];
	size_t size;
	FILE *file;

	if (argc < 2)
	{
		printf("usage: rename_stub_test <halo.sh>\n");
		return 2;
	}

	/* the old program's name beside the renamed one, and none beside any other */
	CHECK(same(rename_stub_old_name("chupathingy.exe"), "halo.exe"));
	CHECK(same(rename_stub_old_name("Chupathingy.EXE"), "halo.exe"));
	CHECK(same(rename_stub_old_name("chupathingy"), "halo"));
	CHECK(rename_stub_old_name("halo") == NULL);
	CHECK(rename_stub_old_name("halo.exe") == NULL);
	CHECK(rename_stub_old_name("chupathingy-server") == NULL);
	CHECK(rename_stub_old_name("my-chupathingy.exe") == NULL);
	CHECK(rename_stub_old_name("") == NULL);
	CHECK(rename_stub_old_name(NULL) == NULL);

	/* the Unix stub 0.9.0 and 0.9.1 shipped is a stub */
	file = fopen(argv[1], "rb");
	CHECK(file != NULL);
	if (file)
	{
		size = fread(script, 1, sizeof(script), file);
		fclose(file);
		CHECK(size > 0 && size < sizeof(script));
		CHECK(kind(script, size) == _rename_stub_stub);
	}

	/* a Windows stub: a small program with the mark in its data */
	memset(big, 0x90, 50000);
	memcpy(big, "MZ\x90\x00", 4);
	memcpy(big + 40000, RENAME_STUB_MARK, sizeof(RENAME_STUB_MARK) - 1);
	CHECK(kind(big, 50000) == _rename_stub_stub);
	/* a mark cut short is not one */
	CHECK(kind(big + 40000, sizeof(RENAME_STUB_MARK) - 2) == _rename_stub_other);

	/* Halo PC's halo.exe, or any other big program: no mark, never ours */
	memset(big, 0x90, sizeof(big));
	memcpy(big, "MZ\x90\x00", 4);
	CHECK(kind(big, sizeof(big)) == _rename_stub_other);
	CHECK(kind(big, 30000) == _rename_stub_other);
	/* (the stub's mark in a file too big to be the stub, and no game's mark) */
	memcpy(big + 2000000, RENAME_STUB_MARK, sizeof(RENAME_STUB_MARK) - 1);
	CHECK(kind(big, sizeof(big)) == _rename_stub_other);

	/* the game from before the rename: a program with the repository's name */
	memcpy(big + 2500000, RENAME_STUB_GAME_MARK, sizeof(RENAME_STUB_GAME_MARK) - 1);
	CHECK(kind(big, sizeof(big)) == _rename_stub_old_game);
	memcpy(big, "\x7f" "ELF", 4);
	CHECK(kind(big, sizeof(big)) == _rename_stub_old_game);
	/* (found across any two pieces) */
	{
		size_t at;

		for (at = 2500000 - 30; at <= 2500000 + 30; at++)
		{
			struct rename_stub_scan scan;

			memset(&scan, 0, sizeof(scan));
			rename_stub_scan_add(&scan, big, at);
			rename_stub_scan_add(&scan, big + at, sizeof(big) - at);
			CHECK(rename_stub_kind(&scan, sizeof(big)) == _rename_stub_old_game);
		}
	}
	/* a script that names the repository is not a program: a player's own */
	{
		static const char text[] = "#!/bin/sh\n# from github.com/" RENAME_STUB_GAME_MARK "\nexec ./other \"$@\"\n";

		CHECK(kind((const unsigned char *)text, sizeof(text) - 1) == _rename_stub_other);
	}
	CHECK(kind((const unsigned char *)"#!/bin/sh\nexec ./halo-real \"$@\"\n", 33) == _rename_stub_other);
	{
		struct rename_stub_scan scan;

		memset(&scan, 0, sizeof(scan));
		rename_stub_scan_add(&scan, NULL, 0);
		CHECK(rename_stub_kind(&scan, 0) == _rename_stub_other);
	}

	if (failures)
		printf("%d failure(s)\n", failures);
	else
		printf("ok\n");
	return failures ? 1 : 0;
}
