/*
MENU_TEXT_FIT.C

See menu_text_fit.h.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "menu_text_fit.h"

#include <string.h>
#include <wchar.h>

static const wchar_t dots[] = L"...";
enum { DOTS = 3 };

struct fit
{
	wchar_t *out;
	long size, length;
	/* where the line being filled starts in out, and the lines so far */
	long line_start, lines;
	long width, line_limit;
	menu_text_measure measure;
	void *context;
	int changed;
};

/* whether `length` characters of `text` fit a line (nothing always does) */
static int fits(const struct fit *fit, const wchar_t *text, long length)
{
	return length <= 0 || fit->measure(fit->context, text, length) < fit->width;
}

static int put(struct fit *fit, const wchar_t *text, long length)
{
	if (length <= 0)
		return 1;
	if (fit->length + length >= fit->size)
		return 0;
	memcpy(fit->out + fit->length, text, (size_t)length * sizeof(wchar_t));
	fit->length += length;
	return 1;
}

/* the line being filled ended with "...", shortened (at its end, spaces
dropped) until that fits; nothing when it ends with "..." already */
static void end_with_dots(struct fit *fit)
{
	wchar_t *line = fit->out + fit->line_start;
	long length = fit->length - fit->line_start;
	wchar_t try[MENU_TEXT_FIT_LENGTH + DOTS];

	if (length >= DOTS && !memcmp(line + length - DOTS, dots, DOTS * sizeof(wchar_t)))
		return;
	for (;;)
	{
		while (length > 0 && line[length - 1] == L' ')
			length--;
		if (length > MENU_TEXT_FIT_LENGTH)
			length = MENU_TEXT_FIT_LENGTH;
		memcpy(try, line, (size_t)length * sizeof(wchar_t));
		memcpy(try + length, dots, DOTS * sizeof(wchar_t));
		if (length == 0 || fits(fit, try, length + DOTS))
			break;
		length--;
	}
	fit->length = fit->line_start + length;
	put(fit, dots, DOTS);
	fit->changed = 1;
}

/* a word at the start of a line: whole when it fits, else cut with "..."
where it stops fitting */
static void put_word(struct fit *fit, const wchar_t *word, long length, int *cut)
{
	wchar_t try[MENU_TEXT_FIT_LENGTH + DOTS];
	long keep;

	if (fits(fit, word, length))
	{
		put(fit, word, length);
		return;
	}
	for (keep = length; keep > 0; keep--)
	{
		if (keep > MENU_TEXT_FIT_LENGTH)
			continue;
		memcpy(try, word, (size_t)keep * sizeof(wchar_t));
		memcpy(try + keep, dots, DOTS * sizeof(wchar_t));
		if (fits(fit, try, keep + DOTS))
			break;
	}
	put(fit, word, keep);
	put(fit, dots, DOTS);
	fit->changed = 1;
	*cut = 1;
}

int menu_text_fit(wchar_t *text, long size, long width, long lines, menu_text_measure measure, void *context)
{
	wchar_t in[MENU_TEXT_FIT_LENGTH];
	wchar_t try[MENU_TEXT_FIT_LENGTH + 1];
	struct fit fit;
	long at = 0, length;
	/* the rest of a paragraph after a word that was cut: left out */
	int cut = 0;

	if (!text || size < 1 || width <= 0 || !measure)
		return 0;
	for (length = 0; text[length] && length < MENU_TEXT_FIT_LENGTH - 1 && length < size - 1; length++)
		in[length] = text[length];
	in[length] = 0;
	memset(&fit, 0, sizeof(fit));
	fit.out = text;
	fit.size = size;
	fit.lines = 1;
	fit.width = width;
	fit.line_limit = lines;
	fit.measure = measure;
	fit.context = context;
	fit.changed = length != (long)wcslen(text);
	while (in[at])
	{
		long word, line_length;

		if (in[at] == L'\r' || in[at] == L'\n')
		{
			/* the text's own line break */
			long count = in[at] == L'\r' && in[at + 1] == L'\n' ? 2 : 1;

			if (fit.line_limit > 0 && fit.lines >= fit.line_limit)
			{
				int more = 0;
				long rest;

				/* (nothing left to show: the break goes without "...") */
				for (rest = at + count; in[rest]; rest++)
				{
					if (in[rest] != L' ' && in[rest] != L'\r' && in[rest] != L'\n')
						more = 1;
				}
				if (more)
					end_with_dots(&fit);
				fit.changed = 1;
				break;
			}
			if (!put(&fit, in + at, count))
				break;
			at += count;
			fit.lines++;
			fit.line_start = fit.length;
			cut = 0;
			continue;
		}
		if (in[at] == L' ')
		{
			at++;
			continue;
		}
		for (word = 0; in[at + word] && in[at + word] != L' ' && in[at + word] != L'\r' && in[at + word] != L'\n';)
			word++;
		if (cut)
		{
			at += word;
			continue;
		}
		line_length = fit.length - fit.line_start;
		if (line_length > 0)
		{
			/* after the line's words, with a space, when that fits */
			if (line_length + 1 + word <= MENU_TEXT_FIT_LENGTH)
			{
				memcpy(try, fit.out + fit.line_start, (size_t)line_length * sizeof(wchar_t));
				try[line_length] = L' ';
				memcpy(try + line_length + 1, in + at, (size_t)word * sizeof(wchar_t));
				if (fits(&fit, try, line_length + 1 + word))
				{
					if (!put(&fit, L" ", 1) || !put(&fit, in + at, word))
						break;
					at += word;
					continue;
				}
			}
			/* on the next line, when there may be one */
			if (fit.line_limit > 0 && fit.lines >= fit.line_limit)
			{
				end_with_dots(&fit);
				cut = 1;
				at += word;
				continue;
			}
			if (!put(&fit, L"\r\n", 2))
				break;
			fit.changed = 1;
			fit.lines++;
			fit.line_start = fit.length;
		}
		put_word(&fit, in + at, word, &cut);
		at += word;
	}
	fit.out[fit.length] = 0;
	return fit.changed || wcscmp(fit.out, in) != 0;
}
