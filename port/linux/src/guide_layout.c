/*
GUIDE_LAYOUT.C

Where the Guide's panes put their pieces (guide_layout.h).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "guide_layout.h"

#include <string.h>

#define ELLIPSIS "..."

int guide_rect_overlap(struct guide_rect a, struct guide_rect b)
{
	if (a.width <= 0 || a.height <= 0 || b.width <= 0 || b.height <= 0)
		return 0;
	return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

int guide_rect_inside(struct guide_rect inner, struct guide_rect outer)
{
	/* (a little slack for float sums) */
	const float slack = 0.01f;

	return inner.x >= outer.x - slack && inner.y >= outer.y - slack &&
		inner.x + inner.width <= outer.x + outer.width + slack && inner.y + inner.height <= outer.y + outer.height + slack;
}

/* ---------- Steam's markup */

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int starts_with(const char *text, const char *word)
{
	for (; *word; text++, word++)
	{
		if (lower((unsigned char)*text) != *word)
			return 0;
	}
	return 1;
}

/* a BBCode tag at text ("[b]", "[/url]", "[url=...]"): its length, 0 if
not one; *skip_to the closing tag whose contents go with it ([img]) */
static int bbcode_tag(const char *text, const char **skip_to)
{
	static const char *const names[] =
	{
		"b", "i", "u", "s", "h1", "h2", "h3", "url", "img", "list", "olist", "*", "quote", "code", "spoiler",
		"strike", "hr", "noparse", "table", "tr", "td", "th", "previewyoutube", "color", "size",
	};
	const char *at = text + 1;
	int index, closing = 0;

	*skip_to = NULL;
	if (*at == '/')
	{
		closing = 1;
		at++;
	}
	for (index = 0; index < (int)(sizeof(names) / sizeof(names[0])); index++)
	{
		size_t length = strlen(names[index]);
		const char *end;

		if (!starts_with(at, names[index]) || (at[length] != ']' && at[length] != '=' && at[length] != ' '))
			continue;
		end = strchr(at + length, ']');
		if (!end || end - text > 512)
			return 0;
		/* (a picture or a video: nothing to read, its address dropped too, up
		to its own closing tag; none: the tag alone) */
		if (!closing && (!strcmp(names[index], "img") || !strcmp(names[index], "previewyoutube")))
		{
			const char *close;

			for (close = end; *close; close++)
			{
				if (close[0] == '[' && close[1] == '/' && starts_with(close + 2, names[index]) &&
					close[2 + length] == ']')
				{
					*skip_to = close + 3 + length;
					break;
				}
			}
		}
		return (int)(end - text) + 1;
	}
	return 0;
}

/* text's first length bytes, less a last UTF-8 character cut short */
static int complete_characters(const char *text, int length)
{
	int start = length, need;
	unsigned char lead;

	while (start > 0 && length - start < 4 && ((unsigned char)text[start - 1] & 0xC0) == 0x80)
		start--;
	if (start == 0)
		return length;
	lead = (unsigned char)text[start - 1];
	if (lead < 0xC0)
		return length;
	need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
	return length - (start - 1) < need ? start - 1 : length;
}

int guide_layout_plain(const char *text, char *out, int size)
{
	int length = 0, line_start = 1, spaces = 0, newlines = 0;

	if (size <= 0)
		return 0;
	while (text && *text && length < size - 1)
	{
		unsigned char c = (unsigned char)*text;
		const char *skip_to;
		int tag;

		if (c == '\r')
		{
			text++;
			continue;
		}
		if (c == '[' && (tag = bbcode_tag(text, &skip_to)) > 0)
		{
			/* ([*] and [hr] start a line, as the list item or the rule do) */
			if (starts_with(text, "[*]") || starts_with(text, "[hr"))
				newlines = newlines ? newlines : 1;
			text = skip_to ? skip_to : text + tag;
			continue;
		}
		/* (markdown's [words](link): the words) */
		if (c == '[')
		{
			const char *close = strchr(text, ']');

			if (close && close[1] == '(' && strchr(close, ')') && close - text < 256)
			{
				const char *word;

				for (word = text + 1; word < close && length < size - 1; word++)
				{
					if (newlines || spaces)
					{
						if (length && length < size - 1)
							out[length++] = newlines ? '\n' : ' ';
						newlines = spaces = 0;
						/* (the separator took the last room) */
						if (length >= size - 1)
							break;
					}
					out[length++] = *word;
				}
				text = strchr(close, ')') + 1;
				line_start = 0;
				continue;
			}
		}
		if ((c == '*' && text[1] == '*') || (c == '_' && text[1] == '_') || (c == '~' && text[1] == '~'))
		{
			text += 2;
			continue;
		}
		if (c == '`')
		{
			text++;
			continue;
		}
		if (line_start && (c == '#' || ((c == '-' || c == '*' || c == '>') && text[1] == ' ')))
		{
			while (*text == '#' || *text == '-' || *text == '*' || *text == '>')
				text++;
			continue;
		}
		if (c == '\n')
		{
			newlines++;
			spaces = 0;
			line_start = 1;
			text++;
			continue;
		}
		if (c == ' ' || c == '\t')
		{
			spaces = 1;
			text++;
			continue;
		}
		/* (a no-break or ideographic space: a space; a zero-width one, a
		joiner or a byte order mark: nothing. A description ending in them
		ended in a line that looked blank) */
		if (!strncmp(text, "\xC2\xA0", 2) || !strncmp(text, "\xE3\x80\x80", 3))
		{
			spaces = 1;
			text += c == 0xC2 ? 2 : 3;
			continue;
		}
		if (!strncmp(text, "\xE2\x80\x8B", 3) || !strncmp(text, "\xE2\x80\x8C", 3) ||
			!strncmp(text, "\xE2\x80\x8D", 3) || !strncmp(text, "\xE2\x81\xA0", 3) || !strncmp(text, "\xEF\xBB\xBF", 3))
		{
			text += 3;
			continue;
		}
		/* (what came between: one line break, or one space; none at the start) */
		if (length && (newlines || spaces) && length < size - 2)
			out[length++] = newlines ? '\n' : ' ';
		newlines = spaces = 0;
		line_start = 0;
		out[length++] = (char)c;
		text++;
	}
	length = complete_characters(out, length);
	out[length] = 0;
	return length;
}

/* ---------- text in its room */

/* a UTF-8 character's start at or before at (never inside one) */
static int character_start(const char *text, int at)
{
	while (at > 0 && ((unsigned char)text[at] & 0xC0) == 0x80)
		at--;
	return at;
}

int guide_layout_fit(const char *text, float width, float size, guide_layout_measure measure, void *context,
	int from_start, char *out, int out_size)
{
	char line[GUIDE_LAYOUT_LINE_SIZE + 4];
	int length = (int)strlen(text), keep;

	if (out_size <= 0)
		return 0;
	if (measure(context, size, text) <= width && length < out_size)
	{
		memcpy(out, text, (size_t)length + 1);
		return 0;
	}
	keep = length < GUIDE_LAYOUT_LINE_SIZE - 1 ? length : GUIDE_LAYOUT_LINE_SIZE - 1;
	if (keep > out_size - 4)
		keep = out_size - 4;
	for (; keep > 0; keep--)
	{
		if (from_start)
		{
			int start = length - keep;

			while (start < length && ((unsigned char)text[start] & 0xC0) == 0x80)
				start++;
			memcpy(line, ELLIPSIS, 3);
			memcpy(line + 3, text + start, (size_t)(length - start) + 1);
		}
		else
		{
			int end = character_start(text, keep);

			/* (no space left before the dots) */
			while (end > 0 && text[end - 1] == ' ')
				end--;
			memcpy(line, text, (size_t)end);
			memcpy(line + end, ELLIPSIS, 4);
		}
		if (measure(context, size, line) <= width)
			break;
	}
	if (keep <= 0)
		memcpy(line, ELLIPSIS, 4);
	if ((int)strlen(line) >= out_size)
		line[out_size - 1] = 0;
	memcpy(out, line, strlen(line) + 1);
	return 1;
}

int guide_layout_wrap(const char *text, float width, float size, guide_layout_measure measure, void *context,
	int maximum, char lines[][GUIDE_LAYOUT_LINE_SIZE], int *cut)
{
	int count = 0;

	if (cut)
		*cut = 0;
	while (text && *text && count < maximum)
	{
		char line[GUIDE_LAYOUT_LINE_SIZE];
		int length = 0, fits = 0;

		while (*text == ' ')
			text++;
		/* (only spaces and line breaks left: no empty lines at the end to
		scroll to) */
		if (!text[strspn(text, " \n")])
			break;
		/* (a line break here: an empty line, as a blank line between
		paragraphs; never the break itself drawn) */
		if (*text == '\n')
		{
			text++;
			lines[count][0] = 0;
			/* (the last line, with more after it: its dots) */
			if (count == maximum - 1 && *text)
			{
				memcpy(lines[count], ELLIPSIS, 4);
				if (cut)
					*cut = 1;
			}
			count++;
			continue;
		}
		/* (as many words as fit) */
		while (text[length] && text[length] != '\n' && length < GUIDE_LAYOUT_LINE_SIZE - 1)
		{
			int end = length;

			while (text[end] && text[end] != ' ' && text[end] != '\n' && end < GUIDE_LAYOUT_LINE_SIZE - 1)
				end++;
			/* (a word cut by the line's size: never inside a character) */
			if (end == GUIDE_LAYOUT_LINE_SIZE - 1 && text[end] && text[end] != ' ' && text[end] != '\n')
				end = character_start(text, end);
			if (end <= length)
				break;
			memcpy(line, text, (size_t)end);
			line[end] = 0;
			if (measure(context, size, line) > width)
				break;
			fits = end;
			length = end;
			while (text[length] == ' ')
				length++;
		}
		/* (a word longer than the line: as much of it as fits) */
		if (!fits)
		{
			int end = 0;

			while (text[end] && text[end] != ' ' && text[end] != '\n' && end < GUIDE_LAYOUT_LINE_SIZE - 1)
			{
				int next = end + 1;

				while (((unsigned char)text[next] & 0xC0) == 0x80)
					next++;
				/* (the line's buffer full: what fits so far) */
				if (next > GUIDE_LAYOUT_LINE_SIZE - 1)
					break;
				memcpy(line, text, (size_t)next);
				line[next] = 0;
				if (end && measure(context, size, line) > width)
					break;
				end = next;
			}
			/* (not even one character fits: one anyway, whole) */
			if (!end)
			{
				end = 1;
				while (end < GUIDE_LAYOUT_LINE_SIZE - 1 && ((unsigned char)text[end] & 0xC0) == 0x80)
					end++;
			}
			fits = end;
		}
		memcpy(line, text, (size_t)fits);
		line[fits] = 0;
		while (fits > 0 && line[fits - 1] == ' ')
			line[--fits] = 0;
		text += fits;
		while (*text == ' ')
			text++;
		if (*text == '\n')
			text++;
		/* (the last line, with more after it: ends in "...") */
		if (count == maximum - 1 && *text)
		{
			char joined[GUIDE_LAYOUT_LINE_SIZE + 4];

			memcpy(joined, line, (size_t)fits + 1);
			memcpy(joined + fits, ELLIPSIS, 4);
			guide_layout_fit(joined, width, size, measure, context, 0, line, sizeof(line));
			/* (the dots on even when the line fit with them) */
			if (strstr(line, ELLIPSIS) == NULL)
			{
				size_t used = strlen(line);

				if (used + 3 < sizeof(line))
					memcpy(line + used, ELLIPSIS, 4);
			}
			if (cut)
				*cut = 1;
		}
		memcpy(lines[count], line, strlen(line) + 1);
		count++;
	}
	return count;
}

/* ---------- the panes */

static struct guide_rect rect(float x, float y, float width, float height)
{
	struct guide_rect result;

	result.x = x;
	result.y = y;
	result.width = width > 0 ? width : 0;
	result.height = height > 0 ? height : 0;
	return result;
}

void guide_layout_page(struct guide_rect pane, int note, int buttons, float line_height,
	struct guide_page_layout *out)
{
	float x = pane.x + GUIDE_PAGE_PADDING, width = pane.width - 2 * GUIDE_PAGE_PADDING;
	float top = pane.y + GUIDE_PAGE_PADDING, bottom = pane.y + pane.height - GUIDE_PAGE_PADDING;
	float picture_height, room, picture_cap;
	int index;

	memset(out, 0, sizeof(*out));
	buttons = buttons < 0 ? 0 : buttons > GUIDE_PAGE_MAXIMUM_BUTTONS ? GUIDE_PAGE_MAXIMUM_BUTTONS : buttons;
	if (line_height <= 0)
		line_height = GUIDE_PAGE_LINE_HEIGHT;
	out->title = rect(x, top, width, GUIDE_PAGE_TITLE_HEIGHT);
	top += GUIDE_PAGE_TITLE_HEIGHT + 4;
	out->status = rect(x, top, width, GUIDE_PAGE_LINE_HEIGHT);
	top += GUIDE_PAGE_LINE_HEIGHT + GUIDE_PAGE_GAP;
	/* (a pane too short for it all: the note goes first, then the second
	button; the main one stays) */
	while (note || buttons > 1)
	{
		float needed = buttons * (GUIDE_PAGE_BUTTON_HEIGHT + GUIDE_PAGE_BUTTON_GAP) +
			(note ? GUIDE_PAGE_NOTE_HEIGHT + GUIDE_PAGE_GAP : 0);

		if (needed <= bottom - top)
			break;
		if (note)
			note = 0;
		else
			buttons--;
	}
	/* (the buttons from the bottom up, the main one on top) */
	out->button_count = buttons;
	for (index = buttons - 1; index >= 0; index--)
	{
		bottom -= GUIDE_PAGE_BUTTON_HEIGHT;
		out->buttons[index] = rect(x, bottom, width, GUIDE_PAGE_BUTTON_HEIGHT);
		bottom -= GUIDE_PAGE_BUTTON_GAP;
	}
	if (buttons)
		bottom -= GUIDE_PAGE_GAP - GUIDE_PAGE_BUTTON_GAP;
	if (note)
	{
		bottom -= GUIDE_PAGE_NOTE_HEIGHT;
		out->note = rect(x, bottom, width, GUIDE_PAGE_NOTE_HEIGHT);
		bottom -= GUIDE_PAGE_GAP;
	}
	/* (the picture 16:9, no taller than a third of the pane, and two lines
	of the description kept below it when they fit) */
	room = bottom - top;
	picture_cap = pane.height * 0.34f;
	picture_height = width * 9.0f / 16.0f;
	if (picture_height > picture_cap)
		picture_height = picture_cap;
	if (picture_height > room - GUIDE_PAGE_GAP - 2 * line_height)
		picture_height = room - GUIDE_PAGE_GAP - 2 * line_height;
	if (picture_height < 24)
		picture_height = 0;
	{
		float picture_width = picture_height * 16.0f / 9.0f;

		if (picture_width > width)
			picture_width = width;
		out->picture = rect(x + (width - picture_width) * 0.5f, top, picture_width, picture_height);
	}
	if (picture_height > 0)
		top += picture_height + GUIDE_PAGE_GAP;
	room = bottom - top;
	out->description_lines = room > 0 ? (int)(room / line_height) : 0;
	out->description = rect(x, top, width, out->description_lines * line_height);
}

void guide_layout_modal(struct guide_rect area, int qr, struct guide_modal_layout *out)
{
	const float padding = 16, gap = 8, title = 16, line = 12, button = 26;
	float side = 0, width, height, y, x;

	memset(out, 0, sizeof(*out));
	/* (the QR as large as the panel allows, up to 168: the phone's camera
	reads it from across a room) */
	if (qr)
	{
		side = area.height - (2 * padding + title + gap + line + 4 + line + gap + button + 2 * gap);
		side = side > 168 ? 168 : side;
		side = side < 64 ? 64 : side;
	}
	width = side + 2 * padding;
	width = width < 280 ? 280 : width;
	width = width > area.width - 24 ? area.width - 24 : width;
	height = padding + title + gap + (qr ? side + gap : 0) + line + 4 + line + 2 * gap + button + padding;
	if (!qr)
		height += line;
	x = area.x + (area.width - width) * 0.5f;
	y = area.y + (area.height - height) * 0.5f;
	out->window = rect(x, y, width, height);
	y += padding;
	out->title = rect(x + padding, y, width - 2 * padding, title);
	y += title + gap;
	if (qr)
	{
		out->qr = rect(x + (width - side) * 0.5f, y, side, side);
		y += side + gap;
	}
	/* (a question's line may take two) */
	out->line = rect(x + padding, y, width - 2 * padding, qr ? line : 2 * line);
	y += out->line.height + 4;
	out->status = rect(x + padding, y, width - 2 * padding, line);
	y += line + 2 * gap;
	if (qr)
	{
		out->button = rect(x + (width - 120) * 0.5f, y, 120, button);
		return;
	}
	/* (a question: its yes and CANCEL, as wide as each other) */
	{
		float each = (width - 2 * padding - gap) * 0.5f;

		each = each > 120 ? 120 : each;
		/* (CANCEL on the left, where the focus starts; the yes on the right) */
		out->cancel = rect(x + width * 0.5f - gap * 0.5f - each, y, each, button);
		out->button = rect(x + width * 0.5f + gap * 0.5f, y, each, button);
	}
}

int guide_layout_marquee(const char *text, float width, float size, guide_layout_measure measure, void *context,
	unsigned long elapsed, char *out, int out_size)
{
	int length = (int)strlen(text), last = 0, start, step, steps, end;
	unsigned long travel, cycle;

	if (out_size <= 0)
		return 0;
	if (measure(context, size, text) <= width || length >= GUIDE_LAYOUT_LINE_SIZE)
	{
		/* (it fits; or too long to read this way: the usual cut) */
		if (measure(context, size, text) <= width && length < out_size)
		{
			memcpy(out, text, (size_t)length + 1);
			return 0;
		}
		guide_layout_fit(text, width, size, measure, context, 0, out, out_size);
		return 0;
	}
	/* (the first start from which the rest fits: its end showing) */
	for (last = 0; last < length; last++)
	{
		if (((unsigned char)text[last] & 0xC0) != 0x80 && measure(context, size, text + last) <= width)
			break;
	}
	/* (the characters to move past) */
	for (steps = 0, start = 0; start < last; start++)
	{
		if (((unsigned char)text[start] & 0xC0) != 0x80)
			steps++;
	}
	travel = (unsigned long)steps * GUIDE_MARQUEE_STEP;
	cycle = GUIDE_MARQUEE_HOLD + travel + GUIDE_MARQUEE_HOLD;
	elapsed %= cycle;
	step = elapsed < GUIDE_MARQUEE_HOLD ? 0 : elapsed >= GUIDE_MARQUEE_HOLD + travel ? steps :
		(int)((elapsed - GUIDE_MARQUEE_HOLD) / GUIDE_MARQUEE_STEP);
	for (start = 0; step > 0 && start < length; start++)
	{
		if (((unsigned char)text[start + 1] & 0xC0) != 0x80)
			step--;
	}
	/* (from there, as much as fits, cut between characters) */
	end = length;
	while (end > start)
	{
		char line[GUIDE_LAYOUT_LINE_SIZE];

		memcpy(line, text + start, (size_t)(end - start));
		line[end - start] = 0;
		if (measure(context, size, line) <= width)
			break;
		end = character_start(text, end - 1);
	}
	if (end - start >= out_size)
		end = start + character_start(text + start, out_size - 1);
	memcpy(out, text + start, (size_t)(end - start));
	out[end - start] = 0;
	return 1;
}

int guide_layout_scroll_clamp(int scroll, int count, int shown)
{
	int most = count - shown;

	if (most < 0)
		most = 0;
	return scroll < 0 ? 0 : scroll > most ? most : scroll;
}

int guide_layout_scroll_thumb(struct guide_rect track, int count, int shown, int scroll, float min_height,
	struct guide_rect *thumb)
{
	float height;

	if (count <= shown || shown <= 0 || track.height <= 0)
	{
		*thumb = track;
		thumb->height = 0;
		return 0;
	}
	scroll = guide_layout_scroll_clamp(scroll, count, shown);
	height = track.height * (float)shown / (float)count;
	if (height < min_height)
		height = min_height;
	if (height > track.height)
		height = track.height;
	*thumb = track;
	thumb->height = height;
	thumb->y = track.y + (track.height - height) * (float)scroll / (float)(count - shown);
	return 1;
}
