/*
TITLE_TOOL.H

The title tool: the menus' titles in Halo's own lettering, made on the
player's machine from the player's own ui.map (display.title_font = "halo").

The maps' titles are pictures of text set in a commercial typeface, which
the project cannot ship, as a font or as pictures drawn with one. The tool
ships neither: it reads the title pictures out of the player's ui.map,
rebuilds each letter at a higher resolution from every place it appears in
them (the same letter at many sub-texel positions, put together), and sets
the titles again with those letters, at 4x, where the map's letters are,
over their own plates (as tools/title_assets.py does for the shipped sets).
What it makes stays in memory: nothing is written or exported.

It knows each title's text and where its letters are from titles.json
(port/assets/titles, the "from_files" titles; tools/embed_assets.py makes
them title_tool_titles), and only uses a picture whose pixels are the ones
those were measured on (its CRC). No SDL or game dependency, so the tests
(port/linux/tests/title_tool_test.c) build it on their own.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef TITLE_TOOL_H
#define TITLE_TOOL_H

/* a title the tool can make: its bitmap (its bitmap group tag's name, its
index there, the CRC-32 of its first mip level's pixels), how much larger
the picture is made, its text, and where each of its letters (the text's
characters other than spaces) starts, in the bitmap's texels */
struct title_tool_title
{
	const char *tag;
	int bitmap;
	unsigned int crc;
	int scale;
	const char *text;
	const float *lefts;
};

/* the titles the build knows (generated: hud_hires_assets.c) */
extern const struct title_tool_title title_tool_titles[];
extern const unsigned int title_tool_title_count;

struct title_tool;

/* reads the titles' pictures out of the ui.map at path (an Xbox map) and
rebuilds their letters; NULL if it has none of them (or cannot be read) */
struct title_tool *title_tool_new(const char *path, const struct title_tool_title *titles, long count);
/* whether title index (of those given to title_tool_new) can be made: its
picture was in the map, with the pixels its letters were measured on */
int title_tool_has(const struct title_tool *tool, long index);
/* title index's picture, 8-bit RGBA, rows top first (malloc'd), and its
size: scale times its bitmap's; NULL if it cannot be made */
unsigned char *title_tool_render(const struct title_tool *tool, long index, unsigned long *width,
	unsigned long *height);
void title_tool_free(struct title_tool *tool);

#endif
