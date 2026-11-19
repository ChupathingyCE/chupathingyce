/*
LOBBY_LAYOUT.C

The game lobby's layout: see lobby_layout.h.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "lobby_layout.h"

#include <string.h>

enum
{
	/* the room left and right of the pieces, from the screen's edge */
	EDGE = 28,
	/* the list's top (its head's), the head's height, and where the list
	and the menu must end (above the prompt bar) */
	LIST_TOP = 80,
	LIST_HEAD = 14,
	CONTENT_BOTTOM = 400,
	ROW_HEIGHT = 28,
	COLUMN_GAP = 6,
	/* between the list and the card */
	CARD_GAP = 14,
	CARD_HEIGHT = 118,
	FACT_LINE = 13,
	MENU_ROW = 22,
	/* the narrowest the card's column may be, and the widest the list */
	CARD_LEAST = 230,
	LIST_MOST = 480,
};

static float least(float a, float b)
{
	return a < b ? a : b;
}

static int clamp(int value, int low, int high)
{
	return value < low ? low : value > high ? high : value;
}

int lobby_rect_overlap(struct lobby_rect a, struct lobby_rect b)
{
	return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

int lobby_rect_inside(struct lobby_rect inner, struct lobby_rect outer)
{
	return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
		inner.y + inner.height <= outer.y + outer.height;
}

static struct lobby_rect rect(float x, float y, float width, float height)
{
	struct lobby_rect r;

	r.x = x;
	r.y = y;
	r.width = width;
	r.height = height;
	return r;
}

/* a column's first row so that `selected` (its index in the column, or
-1 for none) shows, moved as little as it must from `first` */
static int scrolled(int first, int count, int rows, int selected)
{
	int most = count - rows > 0 ? count - rows : 0;

	first = clamp(first, 0, most);
	if (selected >= 0)
	{
		if (selected < first)
			first = selected;
		else if (selected >= first + rows)
			first = selected - rows + 1;
	}
	return clamp(first, 0, most);
}

int lobby_layout_compute(const struct lobby_layout_inputs *inputs, struct lobby_layout *layout)
{
	float width = inputs->screen_width < 640 ? 640 : inputs->screen_width;
	float left, right, content, list_width, card_width, column_width, y;
	int players = clamp(inputs->players, 0, LOBBY_LAYOUT_MAXIMUM_PLAYERS);
	int maximum = clamp(inputs->maximum_players, players, LOBBY_LAYOUT_MAXIMUM_PLAYERS);
	int menu_rows = clamp(inputs->menu_rows, 0, LOBBY_LAYOUT_MAXIMUM_MENU_ROWS);
	int facts = clamp(inputs->facts, 0, LOBBY_LAYOUT_MAXIMUM_FACTS);
	int fact_lines = (facts + 1) / 2;
	int column, fits = 1;

	memset(layout, 0, sizeof(*layout));
	layout->margin = (width - 640) * 0.5f;
	left = -layout->margin + EDGE;
	right = 640 + layout->margin - EDGE;
	content = right - left;
	layout->band = rect(-layout->margin, 0, width, LOBBY_LAYOUT_BAND_BOTTOM);
	/* (the bar to the screen's bottom) */
	layout->bar = rect(-layout->margin, LOBBY_LAYOUT_BAR_TOP, width, LOBBY_LAYOUT_HEIGHT - LOBBY_LAYOUT_BAR_TOP);

	/* the list takes a little over half; the card's column keeps its least */
	list_width = least(content * 0.58f, LIST_MOST);
	if (content - list_width - CARD_GAP < CARD_LEAST)
		list_width = content - CARD_LEAST - CARD_GAP;
	card_width = content - list_width - CARD_GAP;
	layout->list_head = rect(left, LIST_TOP, list_width, LIST_HEAD);
	layout->row_height = ROW_HEIGHT;
	layout->rows = (CONTENT_BOTTOM - LIST_TOP - LIST_HEAD) / ROW_HEIGHT;

	/* one column, or a team's on each side when one can't show them all */
	layout->columns = inputs->teams && players > layout->rows ? 2 : 1;
	column_width = layout->columns == 2 ? (list_width - COLUMN_GAP) * 0.5f : list_width;
	for (column = 0; column < layout->columns; column++)
	{
		layout->column[column] = rect(left + column * (column_width + COLUMN_GAP), LIST_TOP + LIST_HEAD, column_width,
			layout->rows * ROW_HEIGHT);
	}
	if (layout->columns == 2)
	{
		int red = clamp(inputs->red, 0, players), blue = clamp(inputs->blue, 0, players - red);
		int selected = inputs->selected;

		layout->count[0] = red;
		layout->count[1] = blue;
		/* (open slots: a team's share of the game's, while they fit) */
		for (column = 0; column < 2; column++)
		{
			int room = layout->rows - layout->count[column];
			int open = maximum / 2 - layout->count[column];

			layout->open[column] = room > 0 && open > 0 ? (open < room ? open : room) : 0;
		}
		layout->first[0] = scrolled(inputs->first[0], red, layout->rows, selected >= 0 && selected < red ? selected : -1);
		layout->first[1] = scrolled(inputs->first[1], blue, layout->rows,
			selected >= red && selected < red + blue ? selected - red : -1);
	}
	else
	{
		int room = layout->rows - players, open = maximum - players;

		layout->count[0] = players;
		layout->open[0] = room > 0 && open > 0 ? (open < room ? open : room) : 0;
		layout->first[0] = scrolled(inputs->first[0], players, layout->rows,
			inputs->selected >= 0 && inputs->selected < players ? inputs->selected : -1);
	}

	/* a row's pieces: the right column narrow in two columns (a mark, a
	score), wide in one (READY, NOT READY) */
	layout->stripe_width = 4;
	/* (the team's stripe is the row's color: no armor-color square, and no
	platform logos, so the name takes their room) */
	layout->name_x = 12;
	layout->right_width = layout->columns == 2 ? 22 : 48;
	layout->right_x = column_width - 6;
	layout->voice_width = 10;
	layout->voice_x = layout->right_x - layout->right_width - 6 - layout->voice_width;
	layout->name_width = layout->voice_x - 6 - layout->name_x;

	/* the card, the facts and the menu down the right */
	y = LIST_TOP;
	layout->card = rect(left + list_width + CARD_GAP, y, card_width, CARD_HEIGHT);
	y += CARD_HEIGHT + 6;
	layout->fact_line = FACT_LINE;
	layout->facts = rect(layout->card.x, y, card_width, fact_lines * FACT_LINE);
	y += fact_lines * FACT_LINE + (fact_lines ? 8 : 2);
	layout->menu_row_height = MENU_ROW;
	layout->menu = rect(layout->card.x, y, card_width, menu_rows * MENU_ROW);
	if (y + menu_rows * MENU_ROW > CONTENT_BOTTOM)
		fits = 0;
	return fits;
}

struct lobby_rect lobby_layout_row(const struct lobby_layout *layout, int column, int row)
{
	struct lobby_rect c = layout->column[column < 0 ? 0 : column >= layout->columns ? layout->columns - 1 : column];

	return rect(c.x, c.y + row * layout->row_height, c.width, layout->row_height);
}

struct lobby_rect lobby_layout_menu_row(const struct lobby_layout *layout, int index)
{
	return rect(layout->menu.x, layout->menu.y + index * layout->menu_row_height, layout->menu.width,
		layout->menu_row_height);
}

void lobby_layout_place(const struct lobby_layout *layout, const struct lobby_layout_inputs *inputs, int order,
	int *column, int *row)
{
	int c = 0, index = order;

	if (layout->columns == 2 && order >= layout->count[0])
	{
		c = 1;
		index = order - layout->count[0];
	}
	(void)inputs;
	*column = c;
	*row = index - layout->first[c];
	if (order < 0 || index >= layout->count[c] || *row < 0 || *row >= layout->rows)
		*row = -1;
}
