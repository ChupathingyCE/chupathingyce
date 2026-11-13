/*
GUIDE_LAYOUT.H

Where the Guide's panes put their pieces (port/linux/game/guide_workshop.c),
in the menus' 640x480: the Workshop item's page top to bottom, the Steam
sign-in's window in the middle of the panel, and text cut or wrapped to the
room it has. Steam's descriptions come with their own markup ([b], [url=],
**, # headings), stripped here to plain text.

No game or SDL types: text is measured through a function the caller
gives, so that the build machine can test that nothing overlaps or runs
past its box (port/linux/tests/guide_layout_test.c).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef GUIDE_LAYOUT_H
#define GUIDE_LAYOUT_H

struct guide_rect
{
	float x, y, width, height;
};

/* a text's width at a size (the caller's font) */
typedef float (*guide_layout_measure)(void *context, float size, const char *text);

/* whether two boxes share any area (touching edges don't count) */
int guide_rect_overlap(struct guide_rect a, struct guide_rect b);
/* whether inner lies wholly in outer */
int guide_rect_inside(struct guide_rect inner, struct guide_rect outer);

/* Steam's description as plain text: BBCode tags dropped ([b], [/b],
[url=...] keeping its words, [img]...[/img] and [previewyoutube] whole),
markdown's ** __ ` and leading #'s and list marks dropped, [text](link)
as its text, runs of blank lines and spaces made one. Its length */
int guide_layout_plain(const char *text, char *out, int size);

/* text on one line in width: as it is, or cut with "..." (at its start
when from_start, as for a folder's path). 1 when cut */
int guide_layout_fit(const char *text, float width, float size, guide_layout_measure measure, void *context,
	int from_start, char *out, int out_size);

/* text wrapped into at most maximum lines of width (a word longer than a
line is cut), the last ending "..." when the text goes on; each line in
lines. How many; *cut whether the text went on */
#define GUIDE_LAYOUT_LINE_SIZE 192
int guide_layout_wrap(const char *text, float width, float size, guide_layout_measure measure, void *context,
	int maximum, char lines[][GUIDE_LAYOUT_LINE_SIZE], int *cut);

/* the Workshop item's page in pane, top to bottom: its title, its kind
and state line, the picture's box (16:9; its height capped so that the
words below have room), the description's box (description_lines lines of
line_height), a note over the buttons (note: a line saying why, as
"Workshop maps are off"), and the buttons (buttons: 0 to 2, the first the
main one). Nothing overlaps; all of it in pane */
enum
{
	GUIDE_PAGE_PADDING = 12,
	GUIDE_PAGE_TITLE_HEIGHT = 16,
	GUIDE_PAGE_LINE_HEIGHT = 12,
	GUIDE_PAGE_NOTE_HEIGHT = 12,
	GUIDE_PAGE_BUTTON_HEIGHT = 24,
	GUIDE_PAGE_BUTTON_GAP = 4,
	GUIDE_PAGE_GAP = 6,
	GUIDE_PAGE_MAXIMUM_BUTTONS = 2,
};
struct guide_page_layout
{
	struct guide_rect title, status, picture, description, note;
	struct guide_rect buttons[GUIDE_PAGE_MAXIMUM_BUTTONS];
	int button_count;
	int description_lines;
};
void guide_layout_page(struct guide_rect pane, int note, int buttons, float line_height,
	struct guide_page_layout *out);

/* the Steam sign-in's window in the middle of area (the Guide's panel):
its title, the QR (square: qr 0 leaves it out, for a question), one line
under it, the status line, and its button; a question has two, side by
side, the button (its yes) and cancel (empty with a QR). Nothing overlaps;
all of it in the window, and the window in area */
struct guide_modal_layout
{
	struct guide_rect window, title, qr, line, status, button, cancel;
};
void guide_layout_modal(struct guide_rect area, int qr, struct guide_modal_layout *out);

/* one line of text that doesn't fit, focused, read left to right (a
marquee): elapsed milliseconds after it took the focus, it holds at its
start GUIDE_MARQUEE_HOLD, moves a character each GUIDE_MARQUEE_STEP until
its end shows, holds there, and starts again. out is the part that shows,
cut to width with no "..."; text that fits is as it is. 1 when it moves */
enum
{
	GUIDE_MARQUEE_HOLD = 1000,
	GUIDE_MARQUEE_STEP = 120,
};
int guide_layout_marquee(const char *text, float width, float size, guide_layout_measure measure, void *context,
	unsigned long elapsed, char *out, int out_size);

/* a scrolled box of count lines, shown of them at once: the first line
shown, kept in 0..count - shown; and its scroll bar's thumb in track (the
bar's length, the thumb's height its share of it, at least min_height).
0 when everything shows (no bar) */
int guide_layout_scroll_clamp(int scroll, int count, int shown);
int guide_layout_scroll_thumb(struct guide_rect track, int count, int shown, int scroll, float min_height,
	struct guide_rect *thumb);

#endif
