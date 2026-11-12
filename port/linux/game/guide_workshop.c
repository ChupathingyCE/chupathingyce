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

- With game.workshop_steam on (USE STEAM on an item's page, after a note
  that Steam shows MCC as running while the tab is open), the Steam bridge
  (steam_client_bridge.h) runs while the section shows: GET and REMOVE go to
  Steam from here, items show Downloading and
  Installed, and without a list of ours the list is the Steam client's.
  With steamcmd (macOS), SIGN IN opens a terminal for steamcmd's own
  sign-in, and a "Halo CE from MCC" row installs MCC's Halo CE files.

The section shows only on a computer with MCC from Steam, or steamcmd.
No Steamworks SDK, no Steam password, nothing written to Steam's own
folders but what Steam or steamcmd write.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "guide.h"

#ifdef HALO_GUIDE

#include "../src/ui_overlay.h"
#include "../src/workshop_browser.h"
#include "../src/steam_client_bridge.h"
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
	NOTE_LINES = 3,
	PICTURE_HEIGHT = 150,
};

enum
{
	_option_steam,
	_option_unsubscribe,
	_option_sign_in,
	_option_use_steam,
	_option_show_maps,
	/* the find row's: a filter's chip, a sort (+ its WORKSHOP_FILTER_* / WORKSHOP_SORT_*) */
	_option_filter,
	_option_sort = _option_filter + WORKSHOP_FILTER_COUNT,
	MAXIMUM_OPTIONS = _option_sort + WORKSHOP_SORT_COUNT
};

static struct
{
	short row;
	short first;
	boolean page;
	short option;
	/* an item waits on Steam: the folders are watched with the Guide closed too */
	boolean waiting;
	/* the section showed last frame (the bridge stops when it no longer does) */
	boolean showing;
	/* USE STEAM pressed once: its note shows, and CONFIRM turns it on */
	boolean confirm;
} workshop_view;

/* ---------- private code */

static struct workshop_browser_item const *chosen(void)
{
	return workshop_browser_item(workshop_view.row);
}

static short options_of(struct workshop_browser_item const *item, short *options)
{
	short count = 0;

	boolean bridge = workshop_browser_bridge_status() == STEAM_BRIDGE_READY;

	if (!item)
		return 0;
	if (item->setup == WORKSHOP_ROW_FIND)
	{
		short index;

		/* (the chips Steam's tags give, and Installed, only when some item has it) */
		for (index = 0; index < WORKSHOP_FILTER_COUNT; index++)
		{
			if (index != workshop_browser_filter() && workshop_browser_filter_offered(index))
				options[count++] = (short)(_option_filter + index);
		}
		for (index = 0; index < WORKSHOP_SORT_COUNT; index++)
		{
			if (index != workshop_browser_sort())
				options[count++] = (short)(_option_sort + index);
		}
		return count;
	}
	if (item->setup)
	{
		if (workshop_browser_setup_button(workshop_view.row))
			options[count++] = _option_steam;
		return count;
	}
	if ((!item->setup || item->state == WORKSHOP_ITEM_NOT_INSTALLED) && !(bridge && item->steam_subscribed))
		options[count++] = _option_steam;
	if (bridge && item->steam_subscribed && steam_bridge_can_unsubscribe())
		options[count++] = _option_unsubscribe;
	if (bridge && steam_bridge_sign_in_needed())
		options[count++] = _option_sign_in;
	if (!steam_bridge_enabled() && workshop_browser_bridge_backend() != STEAM_BRIDGE_BACKEND_NONE)
		options[count++] = _option_use_steam;
	if (!item->setup && !config_boolean("game.workshop_maps"))
		options[count++] = _option_show_maps;
	return count;
}

static char const *option_label(struct workshop_browser_item const *item, short option)
{
	static char word[24];

	if (option >= _option_filter && option < _option_sort)
	{
		short index;

		snprintf(word, sizeof(word), "%s", workshop_browser_filter_name(option - _option_filter));
		for (index = 0; word[index]; index++)
			word[index] = (char)(word[index] >= 'a' && word[index] <= 'z' ? word[index] - 32 : word[index]);
		return word;
	}
	if (option >= _option_sort && option < MAXIMUM_OPTIONS)
	{
		short index;

		snprintf(word, sizeof(word), "%s", workshop_browser_sort_name(option - _option_sort));
		for (index = 0; word[index]; index++)
			word[index] = (char)(word[index] >= 'a' && word[index] <= 'z' ? word[index] - 32 : word[index]);
		return word;
	}
	switch (option)
	{
	case _option_show_maps: return "TURN ON";
	case _option_unsubscribe: return "REMOVE";
	case _option_sign_in: return "SIGN IN";
	case _option_use_steam: return workshop_view.confirm ? "CONFIRM" : "USE STEAM";
	default: break;
	}
	if (item->setup)
		return workshop_browser_setup_button(workshop_view.row);
	/* (installed, or on its way: its page in Steam) */
	return item->state == WORKSHOP_ITEM_NOT_INSTALLED ? "GET" : "OPEN";
}

/* the note USE STEAM shows before CONFIRM */
static char const *use_steam_note(void)
{
#ifdef __APPLE__
	/* (no Steam client here: steam_client_bridge.c) */
	return "Downloads use steamcmd and the sign-in it saved. Chupathingy never sees your password.";
#else
	return workshop_browser_bridge_backend() == STEAM_BRIDGE_BACKEND_STEAMCMD ?
		"Steam shows MCC as running while this tab is open. Downloads use steamcmd and the sign-in it saved; "
		"Chupathingy never sees your password." :
		"Steam shows MCC as running while this tab is open.";
#endif
}

static void option_run(short option)
{
	if (option >= _option_filter && option < _option_sort)
	{
		workshop_browser_set_filter(option - _option_filter);
		workshop_view.option = 0;
		return;
	}
	if (option >= _option_sort && option < MAXIMUM_OPTIONS)
	{
		workshop_browser_set_sort(option - _option_sort);
		workshop_view.option = 0;
		return;
	}
	switch (option)
	{
	case _option_show_maps:
		config_write_boolean("game.workshop_maps", 1);
		mcc_maps_rescan();
		return;
	case _option_unsubscribe:
		workshop_browser_unsubscribe(workshop_view.row);
		return;
	case _option_sign_in:
		steam_bridge_sign_in();
		return;
	case _option_use_steam:
		if (!workshop_view.confirm)
		{
			workshop_view.confirm = TRUE;
			return;
		}
		workshop_view.confirm = FALSE;
		config_write_boolean("game.workshop_steam", 1);
		/* (the bridge starts, and the list is asked for again) */
		workshop_browser_open(1);
		return;
	default:
		break;
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
	case WORKSHOP_ITEM_DOWNLOADING:
	case WORKSHOP_ITEM_INSTALLING: return COLOR_WAITING;
	default: return COLOR_NOT_INSTALLED;
	}
}

/* a row's kind: the find row's filter and sort as they are */
static char const *kind_text(struct workshop_browser_item const *item)
{
	return item->setup == WORKSHOP_ROW_FIND ? item->kind : workshop_browser_kind_name(item->kind);
}

/* a row's word and color: a setup row's step, an item's state */
static char const *item_word(struct workshop_browser_item const *item)
{
	if (item->setup == WORKSHOP_ROW_FIND)
		return "";
	return item->setup ? workshop_browser_step_name(item->step) : workshop_browser_state_name(item->state);
}

static unsigned int item_color(struct workshop_browser_item const *item)
{
	if (!item->setup)
		return state_color(item->state);
	switch (item->step)
	{
	case WORKSHOP_STEP_READY: return COLOR_INSTALLED;
	case WORKSHOP_STEP_WORKING: return COLOR_WAITING;
	case WORKSHOP_STEP_NOT_AVAILABLE: return COLOR_DESCRIPTION;
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
	return workshop_browser_offered() != 0;
}

void guide_workshop_open(void)
{
	workshop_view.page = FALSE;
	workshop_view.option = 0;
	workshop_view.confirm = FALSE;
	workshop_browser_open(0);
}

void guide_workshop_update(boolean showing)
{
	short index, installed;

	workshop_browser_own_library();
	/* (the bridge only while the section shows) */
	if (workshop_view.showing && !showing)
		workshop_browser_close();
	workshop_view.showing = showing;
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
	workshop_view.confirm = FALSE;
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
		state = item_word(item);
		ui_style_row(left, y, LIST_WIDTH, ROW_HEIGHT, on);
		ui_style_text(UI_TEXT_NAME, left + 10, y + 3, UI_ALIGN_LEFT, item->title);
		if (item->author[0])
			snprintf(line, sizeof(line), "%s - %s", kind_text(item), item->author);
		else
			snprintf(line, sizeof(line), "%s", kind_text(item));
		ui_style_text(UI_TEXT_STATUS, left + 10, y + 17, UI_ALIGN_LEFT, line);
		ui_overlay_text(UI_FONT_MENU, 8.0f, left + LIST_WIDTH - 10, y + 17, UI_ALIGN_RIGHT, item_color(item),
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
	snprintf(line, sizeof(line), "%s%s%s", kind_text(item), size[0] ? " - " : "", size);
	ui_style_text(UI_TEXT_LINE, x + 12, top + 32, UI_ALIGN_LEFT, line);
	ui_overlay_text(UI_FONT_MENU, 9.0f, x + width - 12, top + 33, UI_ALIGN_RIGHT, item_color(item), item_word(item));
	y = top + 48;
	if (item->author[0])
	{
		snprintf(line, sizeof(line), "by %s", item->author);
		ui_style_text(UI_TEXT_LABEL, x + 12, y, UI_ALIGN_LEFT, line);
		y += 16;
	}
	{
		/* (its picture, when there is one: the item's own, or Steam's) */
		int picture_width, picture_height, serial;
		unsigned char const *picture = workshop_browser_preview(workshop_view.row, &picture_width, &picture_height, &serial);

		if (picture && picture_width > 0 && picture_height > 0)
		{
			float draw_width = width - 24, draw_height = draw_width * (float)picture_height / (float)picture_width;

			if (draw_height > PICTURE_HEIGHT)
			{
				draw_height = PICTURE_HEIGHT;
				draw_width = draw_height * (float)picture_width / (float)picture_height;
			}
			ui_overlay_picture(serial, picture, picture_width, picture_height, x + 12, y + 4, draw_width, draw_height,
				0xFFFFFFFF);
			y += draw_height + 8;
		}
	}
	y = text_wrapped(item->description, x + 12, y + 4, width - 24,
		(short)MAX(2, MIN(DESCRIPTION_LINES, (bottom - 12 - 3 * (OPTION_HEIGHT + OPTION_GAP) - 20 - y) /
		(DESCRIPTION_SIZE + 4))));
	{
		short options[MAXIMUM_OPTIONS], option_count = options_of(item, options);

		y = bottom - 12 - option_count * (OPTION_HEIGHT + OPTION_GAP) + OPTION_GAP;
		/* (an action line over the options: what they do, in a few words) */
		if (workshop_view.confirm)
			text_wrapped(use_steam_note(), x + 12, y - 4 - NOTE_LINES * (DESCRIPTION_SIZE + 4), width - 24, NOTE_LINES);
		else if (workshop_browser_bridge_status() == STEAM_BRIDGE_READY && steam_bridge_sign_in_needed())
			ui_style_text(UI_TEXT_LABEL, x + 12, y - 16, UI_ALIGN_LEFT, "Sign in to steamcmd");
		else if (item->state == WORKSHOP_ITEM_DOWNLOADING && item->total > 0)
		{
			snprintf(line, sizeof(line), "Downloading %d%%", (int)(item->downloaded * 100 / item->total));
			ui_style_text(UI_TEXT_LABEL, x + 12, y - 16, UI_ALIGN_LEFT, line);
		}
		else if (item->state == WORKSHOP_ITEM_WAITING && workshop_browser_bridge_status() != STEAM_BRIDGE_READY)
			ui_style_text(UI_TEXT_LABEL, x + 12, y - 16, UI_ALIGN_LEFT, "Open Steam to subscribe");
		else if (!item->setup && !config_boolean("game.workshop_maps"))
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
