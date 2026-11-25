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

int main(int argc, char **argv)
{
	static unsigned char big[RENAME_STUB_MAXIMUM_SIZE + 64];
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
		CHECK(rename_stub_is_stub(script, size, size));
		/* (its mark is near its start, where a short read finds it) */
		CHECK(rename_stub_is_stub(script, 64 < size ? 64 : size, size));
	}

	/* a stub's mark anywhere in a small file, as in the Windows one's data */
	memset(big, 0x90, sizeof(big));
	memcpy(big + 40000, RENAME_STUB_MARK, sizeof(RENAME_STUB_MARK) - 1);
	CHECK(rename_stub_is_stub(big, 50000, 50000));
	/* the old game: no mark, or too big to be a stub */
	CHECK(!rename_stub_is_stub(big, 30000, 30000));
	CHECK(!rename_stub_is_stub(big, 50000, RENAME_STUB_MAXIMUM_SIZE + 1));
	CHECK(!rename_stub_is_stub(big, sizeof(big), 12ull * 1024 * 1024));
	/* a mark cut short is not one */
	CHECK(!rename_stub_is_stub(big + 40000, sizeof(RENAME_STUB_MARK) - 2, 100));
	CHECK(!rename_stub_is_stub((const unsigned char *)"#!/bin/sh\nexec ./halo-real \"$@\"\n", 33, 33));
	CHECK(!rename_stub_is_stub(NULL, 0, 0));

	if (failures)
		printf("%d failure(s)\n", failures);
	else
		printf("ok\n");
	return failures ? 1 : 0;
}
