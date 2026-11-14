/*
LOBBY_LAYOUT.H

The game lobby's layout (port/linux/game/lobby_screen.c), in the menus'
640x480 units: where the band, the player list (one column, or a team's
on each side when the players don't fit one), the map card, the facts and
the menu go for a screen of a given width (640 at 4:3, wider for the
widescreen margins) and a given number of players, and which rows show.
No game types, so the build machine can test it
(port/linux/tests/lobby_layout_test.c): every piece stays inside the
screen and apart from the others at every width and count.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef LOBBY_LAYOUT_H
#define LOBBY_LAYOUT_H

enum
{
	/* the native builds' sessions (halo_port_limits.h) */
	LOBBY_LAYOUT_MAXIMUM_PLAYERS = 128,
	LOBBY_LAYOUT_COLUMNS = 2,
	LOBBY_LAYOUT_MAXIMUM_MENU_ROWS = 6,
	LOBBY_LAYOUT_MAXIMUM_FACTS = 6,
	/* the menus' height, the band as Online Games has it (browser_screen.c),
	and the prompt bar's top (it runs to the screen's bottom) */
	LOBBY_LAYOUT_HEIGHT = 480,
	LOBBY_LAYOUT_BAND_BOTTOM = 70,
	LOBBY_LAYOUT_BAR_TOP = 409,
};

struct lobby_rect
{
	float x, y, width, height;
};

struct lobby_layout_inputs
{
	/* the menus' width: 640, or more at widescreen (halo_screen_width) */
	float screen_width;
	/* the players listed, and the game's most (open slots up to it) */
	int players;
	int maximum_players;
	/* a team game: the red team's rows then the blue team's, in two columns
	(red left, blue right) when one column can't show them all */
	int teams;
	int red;
	int blue;
	/* the selected row (its place in the list's order), kept in view, and
	each column's first row as last shown (the scroll moves no more than it
	must) */
	int selected;
	int first[LOBBY_LAYOUT_COLUMNS];
	int menu_rows;
	int facts;
};

struct lobby_layout
{
	/* the widescreen margin either side of the 640 (0 at 4:3) */
	float margin;
	/* the title band, the list (its head and its columns), the map card,
	the facts grid, the menu and the prompt bar */
	struct lobby_rect band;
	struct lobby_rect list_head;
	int columns;
	struct lobby_rect column[LOBBY_LAYOUT_COLUMNS];
	float row_height;
	/* the rows a column shows at once, and each column's first row (its
	scroll), count and the rows of open slots it shows */
	int rows;
	int first[LOBBY_LAYOUT_COLUMNS];
	int count[LOBBY_LAYOUT_COLUMNS];
	int open[LOBBY_LAYOUT_COLUMNS];
	/* a row's pieces, from its column's left: the team stripe, the name's
	left and the width it may take, the voice icon's left, and the right
	column's right edge (READY, a score) */
	float stripe_width;
	float name_x, name_width;
	float voice_x, voice_width;
	float right_x, right_width;
	/* the map card, the facts (two a line) and the menu */
	struct lobby_rect card;
	struct lobby_rect facts;
	float fact_line;
	struct lobby_rect menu;
	float menu_row_height;
	struct lobby_rect bar;
};

/* the layout for the inputs: 1 if every piece fits, 0 if the menu or
facts ran out of room (drawn clipped: the caller shows fewer) */
int lobby_layout_compute(const struct lobby_layout_inputs *inputs, struct lobby_layout *layout);

/* the row-th visible row of a column */
struct lobby_rect lobby_layout_row(const struct lobby_layout *layout, int column, int row);
/* the index-th menu row */
struct lobby_rect lobby_layout_menu_row(const struct lobby_layout *layout, int index);
/* where a player at place `order` in the list goes: its column, and its
row in it (NONE, -1, when scrolled out of view) */
void lobby_layout_place(const struct lobby_layout *layout, const struct lobby_layout_inputs *inputs, int order,
	int *column, int *row);
/* whether two rects overlap, and whether inner lies wholly in outer */
int lobby_rect_overlap(struct lobby_rect a, struct lobby_rect b);
int lobby_rect_inside(struct lobby_rect inner, struct lobby_rect outer);

#endif
