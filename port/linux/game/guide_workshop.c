/*
GUIDE_WORKSHOP.C

The Guide's WORKSHOP section (guide_workshop.h): Halo: MCC's Steam Workshop
items for Halo CE, as our site lists them (workshop_browser.h), and where
each stands in the player's own Steam libraries.

- The left list is the items: each one's title, its kind and author, and
  where it stands (Not installed, Waiting, Installing,
  Installed). Items found installed that the list lacks follow the list's.
- The right pane is the chosen item's page: its title, kind, size, author
  and description. A on an item opens its options: SUBSCRIBE (OPEN once
  Steam has it), which opens the item's page in the
  Steam client, where the player subscribes; Steam downloads it into MCC's
  own Workshop folder. With game.workshop_maps off, TURN ON turns it
  on, so the item's maps join the map lists.
- While the section shows, or an item waits on Steam, the item folders are
  looked at every second: an item that becomes installed is rescanned into
  the map lists (mcc_maps_rescan) and gets a toast.

The section shows only on a computer with MCC from Steam. No Steamworks,
no Steam password, nothing written to Steam's folders.
*/
#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h, above): a console builds this to nothing */

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "guide.h"

#ifdef HALO_GUIDE

#include "../src/ui_overlay.h"
#include "../src/workshop_browser.h"
#include "guide_workshop.h"
#include "mcc_maps.h"

#include <stdio.h>
#include <string.h>

int config_boolean(char const *name);
int config_write_boolean(char const *name, int value);

enum
{
	LIST_WIDTH = 262,
	ROW_HEIGHT = 30,
	ROW_GAP = 4,
	OPTION_HEIGHT = 24,
	OPTION_GAP = 4,
	HEADING_HEIGHT = 16,
	DESCRIPTION_SIZE = 9,
	DESCRIPTION_LINES = 8,
};

enum
{
	COLOR_VALUE = 0xFFFFFFFF,
	COLOR_INSTALLED = 0x7FE39AFF,
	COLOR_WAITING = 0xFFCF6BFF,
	COLOR_NOT_INSTALLED = 0x8FC4FFFF,
	COLOR_DESCRIPTION = 0xC8D3E0FF,
};

enum
{
	_option_steam,
	_option_show_maps,
	MAXIMUM_OPTIONS
};

static struct
{
	short row;
	short first;
	boolean page;
	short option;
	/* an item waits on Steam: the folders are watched with the Guide closed too */
	boolean waiting;
} workshop_view;

/* ---------- private code */

static struct workshop_browser_item const *chosen(void)
{
	return workshop_browser_item(workshop_view.row);
}

static short options_of(struct workshop_browser_item const *item, short *options)
{
	short count = 0;

	if (!item)
		return 0;
	options[count++] = _option_steam;
	if (!config_boolean("game.workshop_maps"))
		options[count++] = _option_show_maps;
	return count;
}

static char const *option_label(struct workshop_browser_item const *item, short option)
{
	if (option == _option_show_maps)
		return "TURN ON";
	return item->state == WORKSHOP_ITEM_NOT_INSTALLED ? "SUBSCRIBE" : "OPEN";
}

static void option_run(short option)
{
	if (option == _option_show_maps)
	{
		config_write_boolean("game.workshop_maps", 1);
		mcc_maps_rescan();
		return;
	}
	if (workshop_browser_subscribe(workshop_view.row))
		workshop_view.waiting = TRUE;
}

static unsigned int state_color(int state)
{
	switch (state)
	{
	case WORKSHOP_ITEM_INSTALLED: return COLOR_INSTALLED;
	case WORKSHOP_ITEM_WAITING:
	case WORKSHOP_ITEM_INSTALLING: return COLOR_WAITING;
	default: return COLOR_NOT_INSTALLED;
	}
}

/* text wrapped into width, at most lines lines, from y; the last line's bottom */
static float text_wrapped(char const *text, float x, float y, float width, short lines)
{
	char line[256];
	short shown = 0;

	while (*text && shown < lines)
	{
		int length = 0, fits = 0;

		while (*text == ' ')
			text++;
		/* (as many words as fit; a word longer than the line is cut) */
		while (text[length] && text[length] != '\n' && length < (int)sizeof(line) - 1)
		{
			int end = length;

			while (text[end] && text[end] != ' ' && text[end] != '\n' && end < (int)sizeof(line) - 1)
				end++;
			memcpy(line, text, (size_t)end);
			line[end] = 0;
			if (fits && ui_overlay_text_width(UI_FONT_MENU, DESCRIPTION_SIZE, line) > width)
				break;
			fits = end;
			length = end;
			while (text[length] == ' ')
				length++;
		}
		if (!fits)
			fits = length ? length : 1;
		memcpy(line, text, (size_t)fits);
		line[fits] = 0;
		if (shown == lines - 1 && text[fits] && text[fits] != '\n')
		{
			/* (the last line shown: marked as cut) */
			int cut = fits;

			while (cut > 0 && ui_overlay_text_width(UI_FONT_MENU, DESCRIPTION_SIZE, line) +
				ui_overlay_text_width(UI_FONT_MENU, DESCRIPTION_SIZE, "...") > width)
			{
				line[--cut] = 0;
			}
			strcat(line, "...");
		}
		ui_overlay_text(UI_FONT_MENU, DESCRIPTION_SIZE, x, y, UI_ALIGN_LEFT, COLOR_DESCRIPTION, line);
		y += DESCRIPTION_SIZE + 4;
		shown++;
		text += fits;
		if (*text == '\n')
			text++;
	}
	return y;
}

/* ---------- public code */

boolean guide_workshop_shown(void)
{
	return workshop_browser_mcc_present() != 0;
}

void guide_workshop_open(void)
{
	workshop_view.page = FALSE;
	workshop_view.option = 0;
	workshop_browser_open(0);
}

void guide_workshop_update(boolean showing)
{
	short index, installed;

	if (!showing && !workshop_view.waiting)
		return;
	installed = (short)workshop_browser_poll();
	if (installed > 0)
	{
		/* (the map lists read again when they next open: mcc_maps.c) */
		mcc_maps_rescan();
		for (index = 0; index < workshop_browser_count(); index++)
		{
			struct workshop_browser_item const *item = workshop_browser_item(index);

			if (item->subscribed && item->state == WORKSHOP_ITEM_INSTALLED)
			{
				guide_toast("WORKSHOP", item->title);
				break;
			}
		}
	}
	workshop_view.waiting = FALSE;
	for (index = 0; index < workshop_browser_count(); index++)
	{
		struct workshop_browser_item const *item = workshop_browser_item(index);

		if (item->subscribed && item->state != WORKSHOP_ITEM_INSTALLED)
			workshop_view.waiting = TRUE;
	}
}

short guide_workshop_rows(void)
{
	return (short)workshop_browser_count();
}

void guide_workshop_move(short step)
{
	short options[MAXIMUM_OPTIONS];
	short count = (short)workshop_browser_count();

	if (workshop_view.page)
	{
		short option_count = options_of(chosen(), options);

		if (option_count > 0)
			workshop_view.option = (short)PIN(workshop_view.option + step, 0, option_count - 1);
		return;
	}
	if (count > 0)
		workshop_view.row = (short)PIN(workshop_view.row + step, 0, count - 1);
}

void guide_workshop_activate(void)
{
	short options[MAXIMUM_OPTIONS];
	short count = options_of(chosen(), options);

	if (!count)
		return;
	if (!workshop_view.page)
	{
		workshop_view.page = TRUE;
		workshop_view.option = 0;
		return;
	}
	if (workshop_view.option >= 0 && workshop_view.option < count)
		option_run(options[workshop_view.option]);
}

boolean guide_workshop_back(void)
{
	if (!workshop_view.page)
		return FALSE;
	workshop_view.page = FALSE;
	return TRUE;
}

void guide_workshop_row_click(short row, boolean touch)
{
	boolean again = row == workshop_view.row && !workshop_view.page;

	workshop_view.page = FALSE;
	workshop_view.row = row;
	if (again || touch)
		guide_workshop_activate();
}

void guide_workshop_option_click(short option)
{
	workshop_view.page = TRUE;
	workshop_view.option = option;
	guide_workshop_activate();
}

char const *guide_workshop_prompt(void)
{
	return chosen() ? "SELECT" : NULL;
}

boolean guide_workshop_in_page(void)
{
	return workshop_view.page;
}

void guide_workshop_render(float left, float top, float right, float bottom,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index))
{
	short count = (short)workshop_browser_count(), index, visible;
	short status = (short)workshop_browser_status();
	struct workshop_browser_item const *item;
	float y = top + HEADING_HEIGHT, x = left + LIST_WIDTH + 12, width = right - x;
	char heading[48], line[160], size[24];

	if (workshop_view.row >= count)
		workshop_view.row = (short)MAX(count - 1, 0);
	if (!count)
	{
		ui_style_text(UI_TEXT_LINE, (left + right) * 0.5f, (top + bottom) * 0.5f - 14, UI_ALIGN_CENTER,
			status == WORKSHOP_LIST_LOADING ? "Loading" : status == WORKSHOP_LIST_FAILED ? "Can't reach the site" :
			"No Workshop items");
		return;
	}
	snprintf(heading, sizeof(heading), "MCC WORKSHOP (%d)", count);
	ui_style_text(UI_TEXT_HEADING, left, top, UI_ALIGN_LEFT, heading);
	if (status == WORKSHOP_LIST_LOADING || status == WORKSHOP_LIST_FAILED)
		ui_style_text(UI_TEXT_LABEL, left + LIST_WIDTH, top + 1, UI_ALIGN_RIGHT,
			status == WORKSHOP_LIST_LOADING ? "Loading" : "Offline");
	visible = (short)((bottom - y + ROW_GAP) / (ROW_HEIGHT + ROW_GAP));
	if (workshop_view.row < workshop_view.first)
		workshop_view.first = workshop_view.row;
	else if (workshop_view.row >= workshop_view.first + visible)
		workshop_view.first = (short)(workshop_view.row - visible + 1);
	for (index = workshop_view.first; index < count && index < workshop_view.first + visible; index++)
	{
		boolean on = index == workshop_view.row && !workshop_view.page;
		char const *state;

		item = workshop_browser_item(index);
		state = workshop_browser_state_name(item->state);
		ui_style_row(left, y, LIST_WIDTH, ROW_HEIGHT, on);
		ui_style_text(UI_TEXT_NAME, left + 10, y + 3, UI_ALIGN_LEFT, item->title);
		if (item->author[0])
			snprintf(line, sizeof(line), "%s - %s", workshop_browser_kind_name(item->kind), item->author);
		else
			snprintf(line, sizeof(line), "%s", workshop_browser_kind_name(item->kind));
		ui_style_text(UI_TEXT_STATUS, left + 10, y + 17, UI_ALIGN_LEFT, line);
		ui_overlay_text(UI_FONT_MENU, 8.0f, left + LIST_WIDTH - 10, y + 17, UI_ALIGN_RIGHT, state_color(item->state),
			state);
		if (target)
			target(left, y, left + LIST_WIDTH, y + ROW_HEIGHT, 0, index);
		y += ROW_HEIGHT + ROW_GAP;
	}
	/* the chosen item's page */
	item = chosen();
	if (!item)
		return;
	ui_style_glass(x, top, width, bottom - top, 8, ui_style_color(UI_COLOR_PANE), ui_style_color(UI_COLOR_ROW_EDGE));
	ui_style_text(UI_TEXT_TITLE, x + 12, top + 12, UI_ALIGN_LEFT, item->title);
	workshop_browser_size_text(item->size, size, sizeof(size));
	snprintf(line, sizeof(line), "%s%s%s", workshop_browser_kind_name(item->kind), size[0] ? " - " : "", size);
	ui_style_text(UI_TEXT_LINE, x + 12, top + 32, UI_ALIGN_LEFT, line);
	ui_overlay_text(UI_FONT_MENU, 9.0f, x + width - 12, top + 33, UI_ALIGN_RIGHT, state_color(item->state),
		workshop_browser_state_name(item->state));
	y = top + 48;
	if (item->author[0])
	{
		snprintf(line, sizeof(line), "by %s", item->author);
		ui_style_text(UI_TEXT_LABEL, x + 12, y, UI_ALIGN_LEFT, line);
		y += 16;
	}
	y = text_wrapped(item->description, x + 12, y + 4, width - 24, DESCRIPTION_LINES);
	{
		short options[MAXIMUM_OPTIONS], option_count = options_of(item, options);

		y = bottom - 12 - option_count * (OPTION_HEIGHT + OPTION_GAP) + OPTION_GAP;
		/* (an action line over the options: what they do, in a few words) */
		if (item->state == WORKSHOP_ITEM_WAITING)
			ui_style_text(UI_TEXT_LABEL, x + 12, y - 16, UI_ALIGN_LEFT, "Open Steam to subscribe");
		else if (option_count > 1)
			ui_style_text(UI_TEXT_LABEL, x + 12, y - 16, UI_ALIGN_LEFT, "Workshop maps are off");
		for (index = 0; index < option_count; index++)
		{
			boolean on = workshop_view.page && index == workshop_view.option;

			ui_style_row(x + 12, y, width - 24, OPTION_HEIGHT, on);
			ui_overlay_text(UI_FONT_MENU, 10.0f, x + 22, y + 7, UI_ALIGN_LEFT, COLOR_VALUE, option_label(item, options[index]));
			if (target)
				target(x + 12, y, x + width - 12, y + OPTION_HEIGHT, 1, index);
			y += OPTION_HEIGHT + OPTION_GAP;
		}
	}
}

#endif
#endif /* HALO_MCC */
