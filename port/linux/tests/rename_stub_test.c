/*
RENAME_STUB_TEST.C

Tests of the halo stub's rules (port/linux/src/rename_stub.h): which old
program the renamed game removes, how a stub is told from the old game,
and the arguments the Windows stub passes on. Built and run by
tools/test_rename_stub.py with port/rename_stub/halo.sh as its argument; exits
nonzero on a failure.
*/

#include "../src/rename_stub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

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

	/* the shipped Unix stub is a stub */
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

	/* the Windows stub's arguments: what follows its own name */
	CHECK(!wcscmp(rename_stub_arguments(L"\"C:\\Games\\Chupathingy\\halo.exe\" -window -connect 1.2.3.4"),
		L"-window -connect 1.2.3.4"));
	CHECK(!wcscmp(rename_stub_arguments(L"halo.exe   \"halo://join/abc\""), L"\"halo://join/abc\""));
	CHECK(!wcscmp(rename_stub_arguments(L"\"C:\\a b\\halo.exe\""), L""));
	CHECK(!wcscmp(rename_stub_arguments(L"halo.exe"), L""));
	CHECK(!wcscmp(rename_stub_arguments(L"halo.exe\t-x"), L"-x"));
	CHECK(!wcscmp(rename_stub_arguments(L"\"unclosed"), L""));
	CHECK(!wcscmp(rename_stub_arguments(L""), L""));
	CHECK(!wcscmp(rename_stub_arguments(NULL), L""));

	if (failures)
		printf("%d failure(s)\n", failures);
	else
		printf("ok\n");
	return failures ? 1 : 0;
}
