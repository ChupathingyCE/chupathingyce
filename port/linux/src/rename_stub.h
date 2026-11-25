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

0.9.2's downloads have none. At its start the game removes, beside it
(updater.c, updater_remove_old_program), only what it can prove is its own
(rename_stub_kind):

- a stub: RENAME_STUB_MARK in a file no bigger than RENAME_STUB_MAXIMUM_SIZE;
- the game from before the rename, which an updater from 0.7.5d or older
  leaves as halo when it installs a release without one (it would start it
  again, and it would offer the update again): a program (ELF or PE, by
  its first bytes) with RENAME_STUB_GAME_MARK, the game's own repository,
  in it.

Any other halo or halo.exe there (Halo PC's own, OpenCE's, a player's
script) stays.

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
/* the biggest file looked at whole for the game's mark (the game was tens of
megabytes; a debug build's, a few hundred) */
#define RENAME_STUB_MAXIMUM_SCAN (1024ull * 1024 * 1024)

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

/* the text the game's own builds carry, from before the rename on (its
updater's address: updater.c, UPDATE_REPOSITORY), and nothing else's */
#define RENAME_STUB_GAME_MARK "ChupathingyCE/chupathingyce"
/* (the longer of the two marks' lengths, less one: what a scan keeps of one
piece to find a mark that runs into the next) */
#define RENAME_STUB_SCAN_KEEP (sizeof(RENAME_STUB_MARK) > sizeof(RENAME_STUB_GAME_MARK) ? \
	sizeof(RENAME_STUB_MARK) - 2 : sizeof(RENAME_STUB_GAME_MARK) - 2)

enum rename_stub_kinds
{
	/* not the game's: never removed */
	_rename_stub_other,
	_rename_stub_stub,
	_rename_stub_old_game,
};

/* a file read in pieces, in order, for rename_stub_kind */
struct rename_stub_scan
{
	unsigned char first[4];
	size_t first_size;
	unsigned char kept[RENAME_STUB_SCAN_KEEP * 2];
	size_t kept_size;
	int stub_mark;
	int game_mark;
};

static inline int rename_stub_find(const unsigned char *data, size_t size, const char *mark, size_t length)
{
	size_t index;

	for (index = 0; index + length <= size; index++)
	{
		if (data[index] == (unsigned char)mark[0] && !memcmp(data + index, mark, length))
			return 1;
	}
	return 0;
}

static inline void rename_stub_scan_marks(struct rename_stub_scan *scan, const unsigned char *data, size_t size)
{
	scan->stub_mark = scan->stub_mark || rename_stub_find(data, size, RENAME_STUB_MARK, sizeof(RENAME_STUB_MARK) - 1);
	scan->game_mark = scan->game_mark ||
		rename_stub_find(data, size, RENAME_STUB_GAME_MARK, sizeof(RENAME_STUB_GAME_MARK) - 1);
}

/* the next piece of the file */
static inline void rename_stub_scan_add(struct rename_stub_scan *scan, const unsigned char *data, size_t size)
{
	size_t take, index = 0;

	if (!data || !size)
		return;
	while (scan->first_size < sizeof(scan->first) && index < size)
		scan->first[scan->first_size++] = data[index++];
	/* (a mark across the last piece's end and this one's start) */
	take = size < RENAME_STUB_SCAN_KEEP ? size : RENAME_STUB_SCAN_KEEP;
	memcpy(scan->kept + scan->kept_size, data, take);
	rename_stub_scan_marks(scan, scan->kept, scan->kept_size + take);
	rename_stub_scan_marks(scan, data, size);
	/* (this piece's end, kept for the next) */
	if (size >= RENAME_STUB_SCAN_KEEP)
	{
		memcpy(scan->kept, data + size - RENAME_STUB_SCAN_KEEP, RENAME_STUB_SCAN_KEEP);
		scan->kept_size = RENAME_STUB_SCAN_KEEP;
	}
	else
	{
		size_t total = scan->kept_size + size, drop = total > RENAME_STUB_SCAN_KEEP ? total - RENAME_STUB_SCAN_KEEP : 0;

		memmove(scan->kept, scan->kept + drop, total - drop);
		scan->kept_size = total - drop;
	}
}

/* what a file of file_size bytes, scanned whole, is */
static inline int rename_stub_kind(const struct rename_stub_scan *scan, unsigned long long file_size)
{
	int program = scan->first_size >= 4 &&
		((scan->first[0] == 0x7F && scan->first[1] == 'E' && scan->first[2] == 'L' && scan->first[3] == 'F') ||
		(scan->first[0] == 'M' && scan->first[1] == 'Z'));

	if (scan->stub_mark && file_size <= RENAME_STUB_MAXIMUM_SIZE)
		return _rename_stub_stub;
	if (scan->game_mark && program)
		return _rename_stub_old_game;
	return _rename_stub_other;
}

#endif
