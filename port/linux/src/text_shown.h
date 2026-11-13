/*
TEXT_SHOWN.H

Text as a font can show it (ui_overlay.c): the characters the font has no
glyph for (Chinese, Japanese and Korean in the menus' faces, among others)
are left out rather than drawn as empty boxes, and the spaces around them
made one. A string that would show nothing gets a stand-in, so that no
title or name is left blank: a Workshop item's number, a player's
"Player", "?" for anything else.

No game or font types: whether the font has a character is asked through
a function the caller gives, so the build machine can test it
(port/linux/tests/guide_layout_test.c). Header only: every build that
draws the overlay has it without a source of its own.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef TEXT_SHOWN_H
#define TEXT_SHOWN_H

#include <stdio.h>
#include <string.h>

/* whether the font draws a character (a code point) */
typedef int (*text_shown_has)(void *context, unsigned int codepoint);

/* the next UTF-8 character at *cursor (0 at the end), *cursor past it */
static inline unsigned int text_shown_next(const char **cursor)
{
	const unsigned char *bytes = (const unsigned char *)*cursor;
	unsigned int codepoint;
	int extra;

	if (!*bytes)
		return 0;
	if (bytes[0] < 0x80) { codepoint = bytes[0]; extra = 0; }
	else if ((bytes[0] & 0xE0) == 0xC0) { codepoint = bytes[0] & 0x1F; extra = 1; }
	else if ((bytes[0] & 0xF0) == 0xE0) { codepoint = bytes[0] & 0x0F; extra = 2; }
	else { codepoint = bytes[0] & 0x07; extra = 3; }
	*cursor += 1;
	while (extra-- > 0 && (**cursor & 0xC0) == 0x80)
	{
		codepoint = (codepoint << 6) | (unsigned int)(**cursor & 0x3F);
		*cursor += 1;
	}
	return codepoint;
}

/* text as the font shows it, in out: the characters it lacks left out,
runs of spaces one, none at either end. Its length (0: nothing to show) */
static inline int text_shown(const char *text, text_shown_has has, void *context, char *out, int size)
{
	const char *cursor = text, *start;
	int length = 0, space = 0;
	unsigned int codepoint;

	if (size <= 0)
		return 0;
	while (text && (start = cursor, codepoint = text_shown_next(&cursor)) != 0)
	{
		int bytes = (int)(cursor - start);

		if (codepoint == ' ' || codepoint == '\t')
		{
			space = length > 0;
			continue;
		}
		if (codepoint < ' ' || !has(context, codepoint))
			continue;
		if (space && length + 1 < size)
			out[length++] = ' ';
		space = 0;
		if (length + bytes >= size)
			break;
		memcpy(out + length, start, (size_t)bytes);
		length += bytes;
	}
	out[length] = 0;
	return length;
}

/* as text_shown, or fallback when it would show nothing */
static inline void text_shown_or(const char *text, text_shown_has has, void *context, const char *fallback, char *out,
	int size)
{
	if (size > 0 && !text_shown(text, has, context, out, size))
		snprintf(out, (size_t)size, "%s", fallback);
}

/* a player's name as shown: as text_shown; one of only letters the font
lacks, its ASCII letters alone, or "Player" when it has none */
static inline void text_shown_name(const char *name, text_shown_has has, void *context, char *out, int size)
{
	int length = 0, index;

	if (size <= 0 || text_shown(name, has, context, out, size))
		return;
	for (index = 0; name && name[index] && length < size - 1; index++)
	{
		unsigned char c = (unsigned char)name[index];

		if (c > ' ' && c < 0x7F)
			out[length++] = (char)c;
		else if (c == ' ' && length && out[length - 1] != ' ')
			out[length++] = ' ';
	}
	while (length && out[length - 1] == ' ')
		length--;
	out[length] = 0;
	if (!length)
		snprintf(out, (size_t)size, "Player");
}

#endif
