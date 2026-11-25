/*
RENAME_STUB.H

The game's program was halo (halo.exe on Windows) before 0.9.0, and is
chupathingy (chupathingy.exe) now. 0.9.0 and 0.9.1's desktop downloads
also carried a small halo that started chupathingy beside it with the same
arguments, for shortcuts, Steam entries and scripts made for halo, and for
the updaters from before the rename, which start halo again after they
install:

- Windows: halo.exe (port/rename_stub/halo_stub.c, removed in 0.9.2);
- Linux and FreeBSD: halo, a shell script (tools/tests/rename_stub_halo.sh
  keeps it, for the tests).

0.9.2's downloads have none. Each stub carries RENAME_STUB_MARK. At its
start the game removes a stub still beside it (updater.c,
updater_remove_old_program), and only a file with the mark: any other halo
or halo.exe there (an old game, or Halo PC's own) stays.

No SDL, system or game dependency: port/linux/tests/rename_stub_test.c builds
it on its own (tools/test_rename_stub.py).
*/

#ifndef __HALO_RENAME_STUB_H
#define __HALO_RENAME_STUB_H

#include <stddef.h>
#include <string.h>

/* the text every stub carries (halo.sh's second line, a string in
0.9.0 and 0.9.1's halo.exe) */
#define RENAME_STUB_MARK "chupathingy-halo-stub 1"
/* a stub is far smaller than this; the old game is far larger */
#define RENAME_STUB_MAXIMUM_SIZE (1024u * 1024u)

/* whether a and b are the same name, ignoring ASCII case (Windows' names) */
static inline int rename_stub_same_name(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		char x = (char)(*a >= 'A' && *a <= 'Z' ? *a + 32 : *a);
		char y = (char)(*b >= 'A' && *b <= 'Z' ? *b + 32 : *b);

		if (x != y)
			return 0;
	}
	return !*a && !*b;
}

/* the name of the game's old program beside the running one, given the
running one's file name: halo.exe beside chupathingy.exe, halo beside
chupathingy; NULL for any other name (an old build, or a copy renamed by
its player, removes nothing) */
static inline const char *rename_stub_old_name(const char *running)
{
	if (!running)
		return NULL;
	if (rename_stub_same_name(running, "chupathingy.exe"))
		return "halo.exe";
	if (rename_stub_same_name(running, "chupathingy"))
		return "halo";
	return NULL;
}

/* whether data, the first size bytes of a file of file_size bytes, is a
stub: small, and with the mark in it */
static inline int rename_stub_is_stub(const unsigned char *data, size_t size, unsigned long long file_size)
{
	size_t length = sizeof(RENAME_STUB_MARK) - 1, index;

	if (!data || file_size > RENAME_STUB_MAXIMUM_SIZE || size < length)
		return 0;
	for (index = 0; index + length <= size; index++)
	{
		if (data[index] == RENAME_STUB_MARK[0] && !memcmp(data + index, RENAME_STUB_MARK, length))
			return 1;
	}
	return 0;
}

#endif
