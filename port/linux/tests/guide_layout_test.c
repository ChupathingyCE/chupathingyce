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
				if (pane < 4)
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
			struct guide_rect rects[5];
			char what[64];

			guide_layout_modal(areas[area], qr, &layout);
			snprintf(what, sizeof(what), "modal %d qr %d", area, qr);
			rects[0] = layout.title;
			rects[1] = layout.qr;
			rects[2] = layout.line;
			rects[3] = layout.status;
			rects[4] = layout.button;
			check_no_overlap(what, rects, 5, layout.window);
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

int main(void)
{
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
