/*
RENAME_STUB.H

The game's program was halo (halo.exe on Windows) before 0.9.0, and is
chupathingy (chupathingy.exe) now. For one release series the desktop
downloads also carry a small halo that starts chupathingy beside it with
the same arguments, so shortcuts, Steam entries and scripts made for halo
keep working, and so does an updater from before the rename, which starts
halo again after it installs:

- Windows: halo.exe, port/rename_stub/halo_stub.c (tools/windows_build.py);
- Linux and FreeBSD: halo, the shell script port/rename_stub/halo.sh;
- macOS: none (the application keeps its name; only the program inside
  it, Contents/MacOS, is chupathingy now). An AppImage has none either.

Each stub carries RENAME_STUB_MARK. At its start the renamed game removes
an old halo beside it, the game from before the rename (updater.c,
updater_clean_up), but never a file with the mark: the stub stays.

TODO(after 0.9.x): stop shipping the stubs, and let the clean-up remove
them too (drop the mark check).

No SDL, system or game dependency: port/linux/tests/rename_stub_test.c builds
it on its own (tools/test_rename_stub.py).
*/

#ifndef __HALO_RENAME_STUB_H
#define __HALO_RENAME_STUB_H

#include <stddef.h>
#include <string.h>
#include <wchar.h>

/* the text every stub carries (halo.sh's second line, a string in
halo_stub.c's program) */
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

/* the arguments of a Windows command line, after its program's name (as
the C runtime splits it: a quoted name to its closing quote, else to the
first space or tab), and the spaces after it; the stub passes them on
as they are */
static inline const wchar_t *rename_stub_arguments(const wchar_t *command_line)
{
	const wchar_t *at = command_line;

	if (!at)
		return L"";
	if (*at == L'"')
	{
		at++;
		while (*at && *at != L'"')
			at++;
		if (*at == L'"')
			at++;
	}
	else
	{
		while (*at && *at != L' ' && *at != L'\t')
			at++;
	}
	while (*at == L' ' || *at == L'\t')
		at++;
	return at;
}

#endif
