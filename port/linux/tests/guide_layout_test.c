/*
GUIDE_LAYOUT_TEST.C

Tests of the Guide's pane layouts (port/linux/src/guide_layout.c): the
Workshop item's page and the Steam sign-in's window keep their pieces apart
and inside their boxes at any length of text, Steam's markup is stripped,
and text cut or wrapped stays in its width. tools/test_guide_layout.py runs
them.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../src/guide_layout.h"
#include "../src/text_shown.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...) \
	do \
	{ \
		if (!(condition)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

/* (a font a little wider than the menus': each byte 0.62 of the size, a
capital 0.75) */
static float measure(void *context, float size, const char *text)
{
	float width = 0;

	(void)context;
	for (; *text; text++)
		width += size * (*text >= 'A' && *text <= 'Z' ? 0.75f : 0.62f);
	return width;
}

static const char long_title[] =
	"HCE: Multiplayer Extended - The Complete Collection of Every Classic Map Remastered and Rebalanced";

static const char steam_description[] =
	"[h1]**THIS MOD WILL CONTINUE WITH POST-LAUNCH UPDATES**[/h1]\r\n\r\n"
	"[b]Multiplayer Extended[/b] brings [i]every[/i] map back. See [url=https://example.com/a]our page[/url] "
	"and [Discord](https://example.com/d).\n"
	"[img]https://example.com/picture.png[/img]\n"
	"## Features\n"
	"- New weapons\n"
	"* Fixed spawns\n"
	"[list][*]Bigger maps[*]More players[/list]\n"
	"[previewyoutube=abc123;full][/previewyoutube]\n"
	"`code` __under__ ~~gone~~ [quote=someone]A quote[/quote] "
	"Supercalifragilisticexpialidociousandthensomemorewordsjoinedtogetherwithoutanyspaces end.";

static void check_plain(void)
{
	char plain[2048];

	guide_layout_plain(steam_description, plain, sizeof(plain));
	CHECK(!strchr(plain, '['), "a bracket left: %s", plain);
	CHECK(!strchr(plain, ']'), "a bracket left: %s", plain);
	CHECK(!strstr(plain, "**"), "bold marks left: %s", plain);
	CHECK(!strchr(plain, '#'), "a heading mark left: %s", plain);
	CHECK(!strchr(plain, '`'), "a code mark left: %s", plain);
	CHECK(!strstr(plain, "http"), "an address left: %s", plain);
	CHECK(!strchr(plain, '\r'), "a carriage return left");
	CHECK(!strstr(plain, "\n\n"), "a blank line left: %s", plain);
	CHECK(!strstr(plain, "  "), "two spaces left: %s", plain);
	CHECK(strstr(plain, "THIS MOD WILL CONTINUE WITH POST-LAUNCH UPDATES") == plain, "the first words: %s", plain);
	CHECK(strstr(plain, "Multiplayer Extended brings every map back") != NULL, "the words of tags: %s", plain);
	CHECK(strstr(plain, "our page") != NULL, "a link's words: %s", plain);
	CHECK(strstr(plain, "Discord") != NULL, "a markdown link's words: %s", plain);
	CHECK(strstr(plain, "\nNew weapons") != NULL, "a list mark: %s", plain);
	CHECK(strstr(plain, "\nBigger maps") != NULL, "a list item's line: %s", plain);
	CHECK(strstr(plain, "A quote") != NULL, "a quote's words: %s", plain);
	/* (nothing to strip: as it was) */
	guide_layout_plain("Plain words, a [bracket] and 3 * 4.", plain, sizeof(plain));
	CHECK(!strcmp(plain, "Plain words, a [bracket] and 3 * 4."), "plain text changed: %s", plain);
	/* (a small buffer: ends in its room) */
	CHECK(guide_layout_plain(steam_description, plain, 16) == 15, "the small buffer's length");
	/* (a description ending in blank lines of no-break, ideographic or
	zero-width spaces: no line left after its words) */
	guide_layout_plain("Last words.\r\n\xC2\xA0\r\n\xE3\x80\x80\n\xE2\x80\x8B\n\xEF\xBB\xBF\n", plain, sizeof(plain));
	CHECK(!strcmp(plain, "Last words."), "a blank ending kept: [%s]", plain);
	guide_layout_plain("Two\xC2\xA0words", plain, sizeof(plain));
	CHECK(!strcmp(plain, "Two words"), "a no-break space: [%s]", plain);
}

static void check_fit(void)
{
	char out[256];
	float widths[] = { 40, 120, 200, 400 };
	int index;

	for (index = 0; index < (int)(sizeof(widths) / sizeof(widths[0])); index++)
	{
		int cut = guide_layout_fit(long_title, widths[index], 14, measure, NULL, 0, out, sizeof(out));

		CHECK(measure(NULL, 14, out) <= widths[index], "fit past %g: %s", widths[index], out);
		CHECK(cut && strstr(out, "...") != NULL, "a cut title without its dots: %s", out);
		cut = guide_layout_fit("/Users/someone/Library/Application Support/Chupathingy/steam", widths[index], 11,
			measure, NULL, 1, out, sizeof(out));
		CHECK(measure(NULL, 11, out) <= widths[index], "a path fit past %g: %s", widths[index], out);
		CHECK(!cut || !strncmp(out, "...", 3), "a path cut at its end: %s", out);
	}
	CHECK(!guide_layout_fit("Short", 200, 14, measure, NULL, 0, out, sizeof(out)) && !strcmp(out, "Short"),
		"short text cut: %s", out);
}

static void check_wrap(void)
{
	char plain[2048], lines[16][GUIDE_LAYOUT_LINE_SIZE];
	int maximum, count, cut, index;

	guide_layout_plain(steam_description, plain, sizeof(plain));
	for (maximum = 1; maximum <= 12; maximum++)
	{
		count = guide_layout_wrap(plain, 214, 9, measure, NULL, maximum, lines, &cut);
		CHECK(count <= maximum, "%d lines for %d", count, maximum);
		for (index = 0; index < count; index++)
			CHECK(measure(NULL, 9, lines[index]) <= 214, "line %d past its width: %s", index, lines[index]);
		if (count == maximum && cut)
			CHECK(strstr(lines[count - 1], "...") != NULL, "a cut text's last line without dots: %s",
				lines[count - 1]);
	}
	/* (a blank line between paragraphs: an empty line, no control
	character left in any line to be drawn as a box) */
	count = guide_layout_wrap("Words here\n\nMaps in this pack (3):\nUnderground", 214, 9, measure, NULL, 8, lines,
		&cut);
	CHECK(count == 4 && !cut, "a blank line's text in %d lines", count);
	CHECK(count >= 3 && !strcmp(lines[0], "Words here") && lines[1][0] == 0 &&
		!strcmp(lines[2], "Maps in this pack (3):"), "a blank line not empty: [%s] [%s] [%s]", lines[0], lines[1],
		lines[2]);
	for (index = 0; index < count; index++)
		CHECK(strpbrk(lines[index], "\n\r\t") == NULL, "a control character in line %d", index);
	/* (a text ending in blank lines: none of them a line to scroll to) */
	count = guide_layout_wrap("Words here\n\nMore words\n\n\n \n", 214, 9, measure, NULL, 8, lines, &cut);
	CHECK(count == 3 && !cut && !strcmp(lines[2], "More words"), "trailing blank lines kept: %d lines", count);
	count = guide_layout_wrap("Words here\n\nMore words\n\nLast", 214, 4, measure, NULL, 4, lines, &cut);
	CHECK(count == 4 && !strcmp(lines[3], "..."), "a cut text's blank line: %d [%s]", count, lines[3]);
	count = guide_layout_wrap("Two words", 214, 9, measure, NULL, 4, lines, &cut);
	CHECK(count == 1 && !cut && !strcmp(lines[0], "Two words"), "short text wrapped: %d %s", count, lines[0]);
}

static void check_no_overlap(const char *what, struct guide_rect const *rects, int count, struct guide_rect outer)
{
	int a, b;

	for (a = 0; a < count; a++)
	{
		if (rects[a].width <= 0 || rects[a].height <= 0)
			continue;
		CHECK(guide_rect_inside(rects[a], outer), "%s: box %d (%g,%g %gx%g) outside (%g,%g %gx%g)", what, a,
			rects[a].x, rects[a].y, rects[a].width, rects[a].height, outer.x, outer.y, outer.width, outer.height);
		for (b = a + 1; b < count; b++)
			CHECK(!guide_rect_overlap(rects[a], rects[b]), "%s: boxes %d and %d overlap", what, a, b);
	}
}

static void check_page(void)
{
	/* (the Guide's right pane, then shorter and narrower ones) */
	struct guide_rect panes[] =
	{
		{ 338, 134, 238, 264 },
		{ 338, 134, 238, 200 },
		{ 338, 134, 180, 140 },
		{ 338, 134, 300, 330 },
		{ 0, 0, 120, 90 },
	};
	char plain[2048], title[256], lines[64][GUIDE_LAYOUT_LINE_SIZE];
	int pane, note, buttons;

	guide_layout_plain(steam_description, plain, sizeof(plain));
	for (pane = 0; pane < (int)(sizeof(panes) / sizeof(panes[0])); pane++)
	{
		for (note = 0; note <= 1; note++)
		{
			for (buttons = 0; buttons <= GUIDE_PAGE_MAXIMUM_BUTTONS; buttons++)
			{
				struct guide_page_layout layout;
				struct guide_rect rects[8];
				int count = 0, index, shown, cut;
				char what[64];

				guide_layout_page(panes[pane], note, buttons, 13, &layout);
				snprintf(what, sizeof(what), "page %d note %d buttons %d", pane, note, buttons);
				rects[count++] = layout.title;
				rects[count++] = layout.status;
				rects[count++] = layout.picture;
				rects[count++] = layout.description;
				rects[count++] = layout.note;
				for (index = 0; index < layout.button_count; index++)
					rects[count++] = layout.buttons[index];
				check_no_overlap(what, rects, count, panes[pane]);
				/* (the panes the Guide has keep all of it; a tiny one, the main button) */
				/* (the short pane: three buttons and the note don't all fit, the
				note goes first, then the third button) */
				if (pane == 2 && buttons == 3)
					CHECK(layout.button_count >= 2, "%s: %d buttons", what, layout.button_count);
				else if (pane < 4)
				{
					CHECK(layout.button_count == buttons, "%s: %d buttons", what, layout.button_count);
					CHECK(!note || layout.note.height > 0, "%s: no room for the note", what);
				}
				else
					CHECK(layout.button_count == (buttons ? 1 : 0), "%s: %d buttons", what, layout.button_count);
				/* (the Guide's own pane: a picture, and words under it) */
				if (pane == 0)
				{
					CHECK(layout.picture.height >= 60, "%s: picture %g tall", what, layout.picture.height);
					CHECK(layout.description_lines >= 2, "%s: %d lines", what, layout.description_lines);
				}
				if (layout.picture.height > 0)
				{
					float aspect = layout.picture.width / layout.picture.height;

					CHECK(aspect > 1.77f && aspect < 1.78f, "%s: picture not 16:9 (%g)", what, aspect);
				}
				/* (the description's lines fill no more than their box) */
				shown = guide_layout_wrap(plain, layout.description.width, 9, measure, NULL, layout.description_lines,
					lines, &cut);
				CHECK(shown * 13 <= layout.description.height + 0.01f, "%s: %d lines in %g", what, shown,
					layout.description.height);
				CHECK(!layout.description_lines || cut || shown < layout.description_lines, "%s: the long text not cut", what);
				guide_layout_fit(long_title, layout.title.width, 14, measure, NULL, 0, title, sizeof(title));
				CHECK(measure(NULL, 14, title) <= layout.title.width, "%s: title past its box", what);
			}
		}
	}
}

static void check_modal(void)
{
	/* (the Guide's panel, and smaller ones) */
	struct guide_rect areas[] =
	{
		{ 48, 38, 544, 398 },
		{ 48, 38, 544, 300 },
		{ 0, 0, 320, 240 },
	};
	int area, qr;

	for (area = 0; area < (int)(sizeof(areas) / sizeof(areas[0])); area++)
	{
		for (qr = 0; qr <= 1; qr++)
		{
			struct guide_modal_layout layout;
			struct guide_rect rects[6];
			char what[64];

			guide_layout_modal(areas[area], qr, &layout);
			snprintf(what, sizeof(what), "modal %d qr %d", area, qr);
			rects[0] = layout.title;
			rects[1] = layout.qr;
			rects[2] = layout.line;
			rects[3] = layout.status;
			rects[4] = layout.button;
			rects[5] = layout.cancel;
			check_no_overlap(what, rects, 6, layout.window);
			/* (a question's two buttons: both there, as wide as each other) */
			CHECK(qr || (layout.cancel.width >= 80 && layout.cancel.width == layout.button.width &&
				layout.cancel.y == layout.button.y), "%s: the question's buttons %g and %g", what, layout.button.width,
				layout.cancel.width);
			CHECK(!qr || layout.cancel.width == 0, "%s: a CANCEL beside the QR's button", what);
			CHECK(guide_rect_inside(layout.window, areas[area]), "%s: the window past its area", what);
			CHECK(!qr || layout.qr.width == layout.qr.height, "%s: the QR not square", what);
			if (area == 0 && qr)
				CHECK(layout.qr.width >= 140, "%s: QR %g wide", what, layout.qr.width);
			CHECK(!qr || layout.qr.width > 0, "%s: no QR", what);
		}
	}
}

/* (each a valid UTF-8 run: no character cut short) */
static int utf8_whole(const char *text)
{
	const unsigned char *at = (const unsigned char *)text;

	while (*at)
	{
		int need = *at >= 0xF0 ? 4 : *at >= 0xE0 ? 3 : *at >= 0xC0 ? 2 : 1, index;

		if ((*at & 0xC0) == 0x80)
			return 0;
		for (index = 1; index < need; index++)
		{
			if ((at[index] & 0xC0) != 0x80)
				return 0;
		}
		at += need;
	}
	return 1;
}

/* the review's cases: past the line's buffer and the output's end, a cut
character, a picture's tag swallowing the words after another tag (run
under AddressSanitizer by tools/test_guide_layout.py) */
static void check_bounds(void)
{
	char word[600], lines[8][GUIDE_LAYOUT_LINE_SIZE];
	int offset, index, count, cut;

	/* (a word longer than a line, a 2, 3 or 4 byte character about the
	line's size, with room for all of it and for little) */
	for (offset = GUIDE_LAYOUT_LINE_SIZE - 6; offset <= GUIDE_LAYOUT_LINE_SIZE + 2; offset++)
	{
		static const char *const characters[] = { "\xC3\xA9", "\xE2\x80\xA6", "\xF0\x9F\x98\x80" };
		int which;

		for (which = 0; which < 3; which++)
		{
			float widths[] = { 100000, 300, 10 };
			int width;

			memset(word, 'a', (size_t)offset);
			word[offset] = 0;
			strcat(word, characters[which]);
			for (index = (int)strlen(word); index < 400; index++)
				word[index] = 'b';
			word[400] = 0;
			for (width = 0; width < 3; width++)
			{
				count = guide_layout_wrap(word, widths[width], 9, measure, NULL, 8, lines, &cut);
				CHECK(count > 0, "a long word wrapped to nothing");
				for (index = 0; index < count; index++)
				{
					CHECK(strlen(lines[index]) < GUIDE_LAYOUT_LINE_SIZE, "line %d too long", index);
					CHECK(utf8_whole(lines[index]), "line %d cut inside a character (at %d, %d)", index, offset,
						which);
				}
			}
		}
	}
	/* (a markdown link right where the output ends: exactly size bytes) */
	{
		int size;

		for (size = 1; size <= 12; size++)
		{
			char *out = malloc((size_t)size);

			guide_layout_plain("ab [cd](https://x) ef [gh](y)", out, size);
			CHECK((int)strlen(out) < size, "plain past its %d bytes", size);
			free(out);
			out = malloc((size_t)size);
			guide_layout_plain("ab \xC3\xA9\xE2\x80\xA6 [\xC3\xA9\xC3\xA9](x)", out, size);
			CHECK(utf8_whole(out), "plain ended inside a character at %d bytes", size);
			free(out);
		}
	}
	{
		char out[4];

		guide_layout_plain("ab\xC3\xA9", out, sizeof(out));
		CHECK(!strcmp(out, "ab"), "a cut character kept: %s", out);
	}
	/* (a picture's tag: up to its own closing tag, not another's) */
	{
		char out[128];

		guide_layout_plain("[img]https://example.com/a[/url]leak[/img]after", out, sizeof(out));
		CHECK(!strcmp(out, "after"), "a picture's tag let words through: %s", out);
		guide_layout_plain("[b]bold[/b] [img]no closing tag", out, sizeof(out));
		CHECK(strstr(out, "bold") != NULL && strstr(out, "[") == NULL, "an unclosed picture: %s", out);
		guide_layout_plain("[IMG]x[/IMG]kept [previewyoutube=a;full][/previewyoutube]too", out, sizeof(out));
		CHECK(!strcmp(out, "kept too"), "capitals or a video: %s", out);
	}
}

/* a focused line too long for its box reads left to right: its start
first, then a character at a time, then its end, then its start again;
always within width, never with "..."; one that fits doesn't move */
static void check_marquee(void)
{
	char out[256], first[256];
	float width = 120;
	unsigned long end_time;
	int moved;

	moved = guide_layout_marquee(long_title, width, 14, measure, NULL, 0, first, sizeof(first));
	CHECK(moved && !strncmp(first, long_title, strlen(first)) && strlen(first) > 0, "the marquee's start: %s", first);
	guide_layout_marquee(long_title, width, 14, measure, NULL, GUIDE_MARQUEE_HOLD - 1, out, sizeof(out));
	CHECK(!strcmp(out, first), "the marquee moved during its hold: %s", out);
	guide_layout_marquee(long_title, width, 14, measure, NULL, GUIDE_MARQUEE_HOLD + GUIDE_MARQUEE_STEP * 3, out,
		sizeof(out));
	CHECK(!strncmp(out, long_title + 3, strlen(out)), "three steps in, not three characters on: %s", out);
	/* (past its end and hold, every so often) */
	end_time = GUIDE_MARQUEE_HOLD + (unsigned long)strlen(long_title) * GUIDE_MARQUEE_STEP;
	for (unsigned long time = 0; time < end_time * 2; time += 37)
	{
		guide_layout_marquee(long_title, width, 14, measure, NULL, time, out, sizeof(out));
		CHECK(measure(NULL, 14, out) <= width, "the marquee past its box at %lu: %s", time, out);
		CHECK(strstr(out, "...") == NULL, "the marquee with dots: %s", out);
		CHECK(strstr(long_title, out) != NULL, "the marquee not a piece of its text: %s", out);
	}
	{
		int found_end = 0;

		for (unsigned long time = 0; time < end_time * 2; time += 37)
		{
			guide_layout_marquee(long_title, width, 14, measure, NULL, time, out, sizeof(out));
			if (strlen(out) && !strcmp(out, long_title + strlen(long_title) - strlen(out)))
				found_end = 1;
		}
		CHECK(found_end, "the marquee never showed its end");
	}
	CHECK(!guide_layout_marquee("Short", 200, 14, measure, NULL, 5000, out, sizeof(out)) && !strcmp(out, "Short"),
		"short text moved: %s", out);
}

/* the description's box scrolls within its lines, and its bar's thumb
stays in the track, its place following the scroll */
static void check_scroll(void)
{
	struct guide_rect track = { 300, 100, 3, 120 }, thumb, before;
	int scroll;

	CHECK(guide_layout_scroll_clamp(-3, 40, 10) == 0, "scrolled above the top");
	CHECK(guide_layout_scroll_clamp(99, 40, 10) == 30, "scrolled past the last line");
	CHECK(guide_layout_scroll_clamp(5, 8, 10) == 0, "a box that shows it all scrolled");
	CHECK(!guide_layout_scroll_thumb(track, 8, 10, 0, 8, &thumb), "a bar for words that all show");
	before = track;
	before.y = -1;
	for (scroll = 0; scroll <= 30; scroll++)
	{
		CHECK(guide_layout_scroll_thumb(track, 40, 10, scroll, 8, &thumb), "no bar for 40 lines in 10");
		CHECK(guide_rect_inside(thumb, track), "the thumb out of its track at %d", scroll);
		CHECK(thumb.height >= 8 && thumb.height < track.height, "the thumb's height %g", thumb.height);
		CHECK(thumb.y > before.y, "the thumb didn't move down at %d", scroll);
		before = thumb;
	}
	CHECK(thumb.y + thumb.height > track.y + track.height - 0.01f, "the thumb not at the bottom at the end");
	guide_layout_scroll_thumb(track, 1000, 10, 0, 8, &thumb);
	CHECK(thumb.height >= 8, "a thumb too small to see: %g", thumb.height);
}

/* the mouse on a scroll bar: a press on the track pages, a press on the
thumb drags it (to the end and back), the thumb hovered under the pointer,
a press elsewhere not the bar's */
static void check_scroll_bar(void)
{
	struct guide_scroll_bar bar;
	struct guide_rect track = { 300, 100, 3, 120 };
	int scroll = 0, taken, wanted;

	memset(&bar, 0, sizeof(bar));
	bar.track = track;
	bar.count = 40;
	bar.shown = 10;
	guide_layout_scroll_thumb(track, 40, 10, scroll, 8, &bar.thumb);
	/* (no bar: nothing taken) */
	{
		struct guide_scroll_bar none;

		memset(&none, 0, sizeof(none));
		CHECK(guide_layout_scroll_bar_pointer(&none, 0, 301, 150, 1, 1, 301, 150, &taken) == -1 && !taken,
			"a bar with no thumb took a press");
	}
	CHECK(guide_layout_scroll_bar_pointer(&bar, scroll, 100, 150, 0, 1, 100, 150, &taken) == -1 && !taken,
		"a press beside the bar was taken");
	CHECK(guide_layout_scroll_bar_pointer(&bar, scroll, 304, 105, 0, 0, 0, 0, &taken) == -1 && bar.hover,
		"the thumb not hovered under the pointer (in reach of the slim bar)");
	CHECK(guide_layout_scroll_bar_pointer(&bar, scroll, 304, 200, 0, 0, 0, 0, &taken) == -1 && !bar.hover,
		"the thumb hovered with the pointer below it");
	wanted = guide_layout_scroll_bar_pointer(&bar, scroll, 301, 200, 0, 1, 301, 200, &taken);
	CHECK(taken && wanted == 10, "a press below the thumb didn't page down: %d", wanted);
	scroll = wanted;
	guide_layout_scroll_thumb(track, 40, 10, scroll, 8, &bar.thumb);
	wanted = guide_layout_scroll_bar_pointer(&bar, scroll, 301, 101, 0, 1, 301, 101, &taken);
	CHECK(taken && wanted == 0, "a press above the thumb didn't page up: %d", wanted);
	scroll = 0;
	guide_layout_scroll_thumb(track, 40, 10, scroll, 8, &bar.thumb);
	/* (pressed on the thumb: a drag; to the track's end is the last page) */
	CHECK(guide_layout_scroll_bar_pointer(&bar, scroll, 301, 105, 1, 1, 301, 105, &taken) == -1 && taken &&
		bar.dragging, "a press on the thumb didn't start a drag");
	wanted = guide_layout_scroll_bar_pointer(&bar, scroll, 301, 400, 1, 0, 0, 0, &taken);
	CHECK(wanted == 30 && bar.hover, "a drag to the end didn't reach the last line: %d", wanted);
	scroll = wanted;
	wanted = guide_layout_scroll_bar_pointer(&bar, scroll, 301, 105 + 45, 1, 0, 0, 0, &taken);
	CHECK(wanted > 0 && wanted < 30, "a drag halfway: %d", wanted);
	scroll = wanted;
	wanted = guide_layout_scroll_bar_pointer(&bar, scroll, 301, 0, 1, 0, 0, 0, &taken);
	CHECK(wanted == 0, "a drag above the top: %d", wanted);
	CHECK(guide_layout_scroll_bar_pointer(&bar, 0, 301, 300, 0, 0, 0, 0, &taken) == -1 && !bar.dragging,
		"the drag went on with the button up");
}

/* (a font with Latin letters only, as the menus' faces: no CJK) */
static int latin_only(void *context, unsigned int codepoint)
{
	(void)context;
	return codepoint < 0x2000;
}

static void check_shown(void)
{
	char out[128];

	/* (a Workshop title: its CJK letters left out, the spaces around them one) */
	CHECK(text_shown("\xE7\xA0\x82\xE6\xBC\xA0 Desert Run \xE7\xA0\x82", latin_only, NULL, out, sizeof(out)) &&
		!strcmp(out, "Desert Run"), "a mixed title: [%s]", out);
	/* (a Workshop title of only CJK letters: its number, never blank) */
	text_shown_or("\xE6\xB2\x99\xE6\xBC\xA0\xE7\x96\xA3\xE7\x8C\xAA", latin_only, NULL,
		"Workshop item 3000000002", out, sizeof(out));
	CHECK(!strcmp(out, "Workshop item 3000000002"), "an all-CJK title: [%s]", out);
	/* (a friend's name of only CJK letters: "Player", never blank) */
	text_shown_name("\xE5\xB0\x8F\xE6\x98\x8E", latin_only, NULL, out, sizeof(out));
	CHECK(!strcmp(out, "Player"), "an all-CJK name: [%s]", out);
	/* (a friend's name with some Latin letters: those) */
	text_shown_name("\xE5\xB0\x8F\xE6\x98\x8E Ming", latin_only, NULL, out, sizeof(out));
	CHECK(!strcmp(out, "Ming"), "a mixed name: [%s]", out);
	/* (a name the font draws whole: as it is, accents too) */
	text_shown_name("Jos\xC3\xA9  Church", latin_only, NULL, out, sizeof(out));
	CHECK(!strcmp(out, "Jos\xC3\xA9 Church"), "a Latin name: [%s]", out);
	/* (a small buffer: never half a character) */
	CHECK(text_shown("Jos\xC3\xA9", latin_only, NULL, out, 5) == 3 && !strcmp(out, "Jos"), "cut: [%s]", out);
}

/* a page of lines: SETTINGS' captions and rows */
enum { A = GUIDE_LINE_ACTION, I = GUIDE_LINE_INFO, C = GUIDE_LINE_CAPTION, T = GUIDE_LINE_TEXT };

static float lines_height(const float *heights, int first, int shown)
{
	float used = 0;
	int line;

	for (line = first; line < first + shown; line++)
		used += heights[line];
	return used;
}

static void check_lines(void)
{
	/* (a caption 16 high, a row 30, as SETTINGS draws them) */
	static const int kinds[] = { C, A, A, A, C, A, A, C, A, I, I, C, A, A, A, A };
	float heights[16];
	int count = 16, line, first, chosen, step;
	static const int none[] = { C, I, T, I };

	for (line = 0; line < count; line++)
		heights[line] = kinds[line] == C ? 16.0f : 30.0f;
	/* (the cursor: only on actions, past captions and info) */
	CHECK(guide_lines_snap(kinds, count, 0, 1) == 1, "a caption snaps to the action under it");
	CHECK(guide_lines_snap(kinds, count, 4, -1) == 3, "up from a caption: the action over it");
	CHECK(guide_lines_move(kinds, count, 3, 1) == 5, "down past a caption");
	CHECK(guide_lines_move(kinds, count, 8, 1) == 12, "down past info and a caption");
	CHECK(guide_lines_move(kinds, count, 12, -1) == 8, "up past them");
	CHECK(guide_lines_move(kinds, count, 1, -1) == 1, "the first action stays");
	CHECK(guide_lines_move(kinds, count, 15, 1) == 15, "the last stays");
	CHECK(guide_lines_move(kinds, count, 1, 100) == 15, "a long step stops at the last");
	CHECK(guide_lines_move(none, 4, 0, 1) == -1 && guide_lines_snap(none, 4, 2, 1) == -1, "no action: none");
	CHECK(!guide_lines_focusable(kinds, count, -1) && !guide_lines_focusable(kinds, count, count), "out of range");
	/* (scrolling: every action in view as the cursor walks the page, and
	nothing past the room) */
	for (step = 1; step >= -1; step -= 2)
	{
		first = 0;
		chosen = step > 0 ? 1 : 15;
		for (;;)
		{
			int shown;

			first = guide_lines_scroll(kinds, heights, count, chosen, first, 160.0f);
			shown = guide_lines_shown(kinds, heights, count, first, 160.0f);
			CHECK(chosen >= first && chosen < first + shown, "line %d shows (first %d, %d shown)", chosen, first, shown);
			CHECK(lines_height(heights, first, shown) <= 160.5f, "lines %d..%d fit", first, first + shown - 1);
			CHECK(kinds[first + shown - 1] != C, "no caption left at the bottom (first %d)", first);
			line = guide_lines_move(kinds, count, chosen, step);
			if (line == chosen)
				break;
			chosen = line;
		}
	}
	/* (a block's first action brings its caption with it) */
	CHECK(guide_lines_scroll(kinds, heights, count, 5, 6, 160.0f) == 4, "back up to line 5: its caption shows");
	CHECK(guide_lines_scroll(kinds, heights, count, 1, 3, 160.0f) == 0, "the first block's caption too");
	/* (information over a block's first action comes with it too) */
	{
		static const int about[] = { C, I, I, A, C, A, A };
		static const float about_heights[] = { 18, 20, 20, 30, 18, 30, 30 };

		CHECK(guide_lines_snap(about, 7, 0, 1) == 3, "the first action under the information");
		CHECK(guide_lines_scroll(about, about_heights, 7, 3, 3, 120.0f) == 0, "its caption and lines show with it");
		CHECK(guide_lines_scroll(about, about_heights, 7, 3, 3, 60.0f) == 2, "as many of them as fit");
	}
	/* (the end never floats up: a first past the last full start is pulled back) */
	first = guide_lines_scroll(kinds, heights, count, -1, 15, 160.0f);
	CHECK(lines_height(heights, first, count - first) <= 160.5f && first > 0 &&
		lines_height(heights, first - 1, count - first + 1) > 160.5f, "the last lines fill the room (first %d)", first);
	/* (everything fits: from the top) */
	CHECK(guide_lines_scroll(kinds, heights, count, 15, 4, 1000.0f) == 0, "a short page from its top");
	CHECK(guide_lines_shown(kinds, heights, count, 0, 1000.0f) == count, "all of it shows");
	/* (room for less than a line: still one) */
	CHECK(guide_lines_shown(kinds, heights, count, 1, 10.0f) == 1, "at least one line");
	/* (a caption whose row won't fit is left for the next screen) */
	CHECK(guide_lines_shown(kinds, heights, count, 1, 30 * 3 + 16 + 10) == 3, "the caption waits for its row");
}

/* the sections down the left, and a list beside its detail */
static void check_sidebar(void)
{
	struct guide_rect area = { 32, 100, 116, 298 };
	struct guide_sidebar_layout sidebar;
	struct guide_rect list, detail, body = { 162, 100, 446, 298 };
	int count, index, other;

	for (count = 0; count <= GUIDE_SIDEBAR_MOST + 2; count++)
	{
		guide_layout_sidebar(area, count, 24.0f, &sidebar);
		CHECK(sidebar.count == (count > GUIDE_SIDEBAR_MOST ? GUIDE_SIDEBAR_MOST : count), "%d sections", count);
		for (index = 0; index < sidebar.count; index++)
		{
			CHECK(guide_rect_inside(sidebar.items[index], area), "item %d of %d inside", index, count);
			CHECK(sidebar.items[index].height > 0, "item %d of %d has height", index, count);
			for (other = index + 1; other < sidebar.count; other++)
				CHECK(!guide_rect_overlap(sidebar.items[index], sidebar.items[other]), "items %d and %d", index, other);
		}
	}
	/* (eight sections fit at their full height with their gaps) */
	guide_layout_sidebar(area, 8, 24.0f, &sidebar);
	CHECK(sidebar.items[7].height == 24.0f && sidebar.items[1].y - sidebar.items[0].y == 24.0f + GUIDE_SIDEBAR_GAP,
		"eight at full size");
	/* (the sections start at the top: nothing of the Guide's own at the foot) */
	CHECK(sidebar.items[0].y == area.y, "from the top");
	/* (more than fit: squeezed, the last one still inside at the foot) */
	guide_layout_sidebar(area, GUIDE_SIDEBAR_MOST, 30.0f, &sidebar);
	CHECK(sidebar.items[GUIDE_SIDEBAR_MOST - 1].y + sidebar.items[GUIDE_SIDEBAR_MOST - 1].height <=
		area.y + area.height + 0.01f, "squeezed into the area");
	/* (the list and its detail: apart, in the body, the detail at least its minimum) */
	guide_layout_columns(body, 12.0f, 0.53f, 190.0f, &list, &detail);
	CHECK(guide_rect_inside(list, body) && guide_rect_inside(detail, body), "columns in the body");
	CHECK(!guide_rect_overlap(list, detail) && detail.x - (list.x + list.width) >= 11.99f, "columns apart");
	CHECK(detail.width >= 190.0f - 0.01f, "the detail's minimum (%.1f)", detail.width);
	CHECK(list.x + list.width + 12 + detail.width <= body.x + body.width + 0.01f, "they fill the body");
	/* (a narrow body: the list keeps half) */
	body.width = 300;
	guide_layout_columns(body, 12.0f, 0.53f, 250.0f, &list, &detail);
	CHECK(list.width >= (300 - 12) * 0.5f - 0.01f && guide_rect_inside(detail, body), "a narrow body");
}

/* the sections listed: PLAYERS HERE (3 here) only in a game */
static void check_sections(void)
{
	/* FRIENDS, PARTY, CLAN, PLAYERS HERE, RECENT, WORKSHOP, SERVICE RECORD, SETTINGS */
	static const int menus[] = { 1, 1, 1, 0, 1, 1, 1, 1 }, game[] = { 1, 1, 1, 1, 1, 1, 1, 1 };
	static const int none[] = { 0, 0, 0 };
	struct guide_rect area = { 32, 100, 116, 298 };
	struct guide_sidebar_layout sidebar;
	int listed, index;

	/* (in the menus: RB from CLAN and LB from RECENT pass it) */
	CHECK(guide_sections_go(menus, 8, 2, 3) == 4, "RB past PLAYERS HERE in the menus");
	CHECK(guide_sections_go(menus, 8, 4, 3) == 2, "LB past PLAYERS HERE in the menus");
	/* (in a game: it is there) */
	CHECK(guide_sections_go(game, 8, 2, 3) == 3 && guide_sections_go(game, 8, 4, 3) == 3, "PLAYERS HERE in a game");
	/* (the game ends with it open: the next one) */
	CHECK(guide_sections_go(menus, 8, 3, 4) == 4, "a game's end moves on to RECENT");
	/* (wrapping past either end, and nothing listed) */
	CHECK(guide_sections_go(menus, 8, 7, 8) == 0 && guide_sections_go(menus, 8, 0, -1) == 7, "wraps");
	CHECK(guide_sections_go(none, 3, 0, 1) == -1, "none listed");
	/* (the sidebar: seven in the menus, eight in a game, all inside) */
	for (listed = 7; listed <= 8; listed++)
	{
		guide_layout_sidebar(area, listed, 24.0f, &sidebar);
		CHECK(sidebar.count == listed, "%d listed", listed);
		for (index = 0; index < listed; index++)
			CHECK(guide_rect_inside(sidebar.items[index], area), "item %d of %d inside", index, listed);
	}
}

int main(void)
{
	check_sections();
	check_sidebar();
	check_lines();
	check_shown();
	check_marquee();
	check_scroll();
	check_scroll_bar();
	check_bounds();
	check_plain();
	check_fit();
	check_wrap();
	check_page();
	check_modal();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("guide layout: all passed\n");
	return 0;
}
