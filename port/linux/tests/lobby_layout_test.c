/*
LOBBY_LAYOUT_TEST.C

Tests of the game lobby's layout (port/linux/src/lobby_layout.c): at every
screen shape (4:3, 16:10, 16:9, 21:9) and with 1, 2, 8, 16, 32 and 128
players, in a team game or not, every piece stays inside the screen and
apart from the others, the rows stay in their columns, a name has room,
the selected row stays in view and the scroll moves only when it must.
tools/test_lobby_layout.py runs them.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../src/lobby_layout.h"

#include <stdio.h>
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

static const float widths[] = { 640, 768, 853, 1120 };
static const int counts[] = { 0, 1, 2, 8, 16, 32, 128 };

static void check_layout(float width, int players, int teams, int menu_rows, int facts)
{
	struct lobby_layout_inputs in;
	struct lobby_layout layout;
	struct lobby_rect screen, content;
	int column, row, index, red = teams ? (players + 1) / 2 : 0;

	memset(&in, 0, sizeof(in));
	in.screen_width = width;
	in.players = players;
	in.maximum_players = players > 16 ? players : 16;
	in.teams = teams;
	in.red = red;
	in.blue = players - red;
	in.menu_rows = menu_rows;
	in.facts = facts;
	CHECK(lobby_layout_compute(&in, &layout), "no room at %g wide, %d players, %d rows, %d facts", width, players,
		menu_rows, facts);
	screen.x = -layout.margin;
	screen.y = 0;
	screen.width = width;
	screen.height = LOBBY_LAYOUT_HEIGHT;
	content.x = screen.x;
	content.y = LOBBY_LAYOUT_BAND_BOTTOM;
	content.width = width;
	content.height = LOBBY_LAYOUT_BAR_TOP - LOBBY_LAYOUT_BAND_BOTTOM;
	CHECK(lobby_rect_inside(layout.band, screen) && lobby_rect_inside(layout.bar, screen), "the band or bar is off screen");
	CHECK(layout.bar.y + layout.bar.height == LOBBY_LAYOUT_HEIGHT, "the bar stops short of the bottom");
	CHECK(lobby_rect_inside(layout.list_head, content), "the list head leaves the content at %g", width);
	CHECK(lobby_rect_inside(layout.card, content), "the card leaves the content at %g", width);
	CHECK(lobby_rect_inside(layout.facts, content), "the facts leave the content at %g", width);
	CHECK(lobby_rect_inside(layout.menu, content), "the menu leaves the content at %g", width);
	CHECK(!lobby_rect_overlap(layout.card, layout.facts) && !lobby_rect_overlap(layout.facts, layout.menu) &&
		!lobby_rect_overlap(layout.card, layout.menu), "the card's column overlaps itself");
	CHECK(layout.columns == (teams && players > layout.rows ? 2 : 1), "%d columns for %d players (teams %d)",
		layout.columns, players, teams);
	CHECK(layout.rows >= 8, "only %d rows", layout.rows);
	for (column = 0; column < layout.columns; column++)
	{
		struct lobby_rect c = layout.column[column];

		CHECK(lobby_rect_inside(c, content), "column %d leaves the content at %g", column, width);
		CHECK(!lobby_rect_overlap(c, layout.card) && !lobby_rect_overlap(c, layout.menu) &&
			!lobby_rect_overlap(c, layout.facts), "column %d overlaps the card's column at %g", column, width);
		CHECK(!lobby_rect_overlap(c, layout.list_head), "column %d overlaps the head", column);
		if (column)
			CHECK(!lobby_rect_overlap(c, layout.column[0]), "the columns overlap at %g", width);
		for (row = 0; row < layout.rows; row++)
		{
			struct lobby_rect r = lobby_layout_row(&layout, column, row);

			CHECK(lobby_rect_inside(r, c), "row %d leaves its column", row);
			if (row)
				CHECK(!lobby_rect_overlap(r, lobby_layout_row(&layout, column, row - 1)), "rows %d overlap", row);
		}
		CHECK(layout.count[column] + layout.open[column] <= layout.rows || !layout.open[column],
			"open slots scroll in column %d", column);
		CHECK(layout.first[column] >= 0 && layout.first[column] <= (layout.count[column] > layout.rows ?
			layout.count[column] - layout.rows : 0), "column %d scrolled to %d of %d", column, layout.first[column],
			layout.count[column]);
	}
	/* a row's pieces, left to right, inside the column's width */
	CHECK(layout.stripe_width < layout.name_x, "the stripe and name overlap");
	CHECK(layout.name_x + layout.name_width <= layout.voice_x - 6, "the name runs into the voice icon at %g (%d columns)",
		width, layout.columns);
	CHECK(layout.voice_x + layout.voice_width <= layout.right_x - layout.right_width,
		"the voice and right column overlap at %g (%d columns)", width, layout.columns);
	CHECK(layout.right_x <= layout.column[0].width, "the right column leaves the row at %g", width);
	CHECK(layout.name_width >= 60, "a name has only %g at %g wide with %d columns", layout.name_width, width,
		layout.columns);
	for (index = 0; index < menu_rows; index++)
	{
		struct lobby_rect r = lobby_layout_menu_row(&layout, index);

		CHECK(lobby_rect_inside(r, layout.menu), "menu row %d leaves the menu", index);
		if (index)
			CHECK(!lobby_rect_overlap(r, lobby_layout_menu_row(&layout, index - 1)), "menu rows %d overlap", index);
	}
	/* every player has a place or is scrolled out, never both columns */
	for (index = 0; index < players; index++)
	{
		int c, r;

		lobby_layout_place(&layout, &in, index, &c, &r);
		CHECK(c >= 0 && c < layout.columns, "player %d in column %d", index, c);
		CHECK(r == -1 || (r >= 0 && r < layout.rows), "player %d in row %d", index, r);
		if (teams && layout.columns == 2)
			CHECK(c == (index >= red), "player %d (team %d) in column %d", index, index >= red, c);
	}
	lobby_layout_place(&layout, &in, players, &column, &row);
	CHECK(row == -1, "a player past the end has a row");
}

/* the selected row stays in view, the scroll moving only when it must */
static void check_scroll(void)
{
	struct lobby_layout_inputs in;
	struct lobby_layout layout;
	int selected, column, row;

	memset(&in, 0, sizeof(in));
	in.screen_width = 853;
	in.players = 32;
	in.maximum_players = 32;
	in.menu_rows = 4;
	in.facts = 4;
	/* free for all: one column of 32, scrolled */
	for (selected = 0; selected < 32; selected++)
	{
		in.selected = selected;
		lobby_layout_compute(&in, &layout);
		lobby_layout_place(&layout, &in, selected, &column, &row);
		CHECK(column == 0 && row >= 0, "selected %d out of view (first %d)", selected, layout.first[0]);
		CHECK(layout.first[0] >= in.first[0] || selected < in.first[0], "the scroll moved back for %d", selected);
		in.first[0] = layout.first[0];
	}
	CHECK(layout.first[0] == 32 - layout.rows, "scrolled to %d at the end", layout.first[0]);
	/* back up: the first row scrolls only when the selection leaves the top */
	in.selected = 31 - layout.rows;
	lobby_layout_compute(&in, &layout);
	CHECK(layout.first[0] == 31 - layout.rows, "scrolled to %d going back", layout.first[0]);
	in.first[0] = layout.first[0];
	in.selected = 0;
	lobby_layout_compute(&in, &layout);
	CHECK(layout.first[0] == 0, "not back at the top");
	/* two team columns: each scrolls on its own */
	in.teams = 1;
	in.red = in.blue = 16;
	memset(in.first, 0, sizeof(in.first));
	in.selected = 31;
	lobby_layout_compute(&in, &layout);
	CHECK(layout.columns == 2 && layout.first[0] == 0 && layout.first[1] == 16 - layout.rows,
		"the blue column scrolled to %d, the red to %d", layout.first[1], layout.first[0]);
	lobby_layout_place(&layout, &in, 31, &column, &row);
	CHECK(column == 1 && row == layout.rows - 1, "the last blue player at column %d row %d", column, row);
	lobby_layout_place(&layout, &in, 15, &column, &row);
	CHECK(column == 0 && row == -1, "the last red player shows at row %d while the red column is at the top", row);
	/* a player count past the rows but no teams: one column, open slots gone */
	in.teams = 0;
	in.players = 12;
	in.maximum_players = 16;
	in.selected = 0;
	lobby_layout_compute(&in, &layout);
	CHECK(layout.columns == 1 && layout.open[0] == 0, "open slots shown past the rows");
	in.players = 6;
	lobby_layout_compute(&in, &layout);
	CHECK(layout.open[0] == layout.rows - 6, "%d open slots for 6 of 16 (rows %d)", layout.open[0], layout.rows);
	in.players = 6;
	in.maximum_players = 8;
	lobby_layout_compute(&in, &layout);
	CHECK(layout.open[0] == 2, "%d open slots for 6 of 8", layout.open[0]);
}

int main(void)
{
	int w, c, teams, menu_rows, facts;

	for (w = 0; w < (int)(sizeof(widths) / sizeof(widths[0])); w++)
	{
		for (c = 0; c < (int)(sizeof(counts) / sizeof(counts[0])); c++)
		{
			for (teams = 0; teams < 2; teams++)
			{
				for (menu_rows = 2; menu_rows <= 5; menu_rows++)
				{
					for (facts = 0; facts <= 6; facts += 2)
						check_layout(widths[w], counts[c], teams, menu_rows, facts);
				}
			}
		}
	}
	check_scroll();
	if (failures)
	{
		printf("%d checks failed\n", failures);
		return 1;
	}
	printf("lobby_layout_test: ok\n");
	return 0;
}
