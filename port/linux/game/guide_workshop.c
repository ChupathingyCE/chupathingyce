/*
GUIDE_WORKSHOP.C

The Guide's WORKSHOP section (guide_workshop.h): Halo: MCC's Steam Workshop
items for Halo CE, as Steam lists them (workshop_browser.h), and where each
stands here. The player flow it follows: workshop-flow.md.

- The left list, top to bottom: RETRY (while the list can't load), STEAM
  (while Steam isn't linked: it opens the sign-in window), SEARCH, SHOW and
  SORT (rows that are their own value: left and right, or a click on an
  arrow, step it; A steps it on), the items, and PAGE (Steam's list a page
  at a time; LT and RT, Page Up and Page Down, from anywhere). The focus
  starts on the first item; one click on an item opens its page.
- The right pane is the chosen item's page: its title, kind, size, author,
  where it stands, picture, the author's whole description (it scrolls)
  and a line over its buttons. The buttons are fixed by where the item
  stands: GET; CANCEL while getting it; RETRY once that failed; STORE when
  the account doesn't own MCC; PLAY and REMOVE once installed (UPDATE
  between them when Steam has a newer one). An item Steam itself installed
  has OPEN IN STEAM in REMOVE's place (workshop_decisions.h).
- GET while Steam isn't linked opens the sign-in window (its QR, for the
  Steam app); the item comes once it is scanned.
- One window for questions (REMOVE's, UNLINK's), CANCEL on the left with the
  focus: B, CANCEL, a right-click or a click beside it leave things as they
  were.
- PLAY hands the item to the menus' own Create Game or level select
  (menu_functions.c); when that can't open, the page says so.

No Steamworks SDK, no Steam password, nothing written to Steam's own
folders.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
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
#include "../src/workshop_decisions.h"
#include "../src/steam_workshop.h"
#include "../src/steam_client_bridge.h"
#include "../src/browser.h"
#include "../src/settings_rows.h"
#include "../src/guide_layout.h"
#include "guide_workshop.h"
#include "mcc_maps.h"

/* menu_functions.c's: PLAY handed to the menus' own flows */
boolean pc_menu_workshop_play(char const *item, char const *title);
void pc_menu_workshop_play_update(void);
#include "input/input.h"
#include "interface/virtual_keyboard.h"

/* (a key stroke's control modifier: input_xbox.c's, as browser_screen.c has it) */
#define KEY_MODIFIER_CONTROL_BIT 1

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int config_boolean(char const *name);
char const *config_string(char const *name);
boolean main_menu_is_active(void);
boolean game_map_loaded(void);
void *global_network_game_server_get(void);
void platform_text_field(int typing);
unsigned long system_milliseconds(void);
int platform_clipboard_get(char *text, int size);
int config_write_boolean(char const *name, int value);
void platform_log(char const *format, ...);
void platform_open_url(char const *url);

#define MCC_STORE_PAGE "https://store.steampowered.com/app/976730"

enum
{
	LIST_WIDTH = 262,
	ROW_HEIGHT = 30,
	/* SEARCH, SHOW, SORT, STEAM, RETRY and PAGE: one line */
	CONTROL_HEIGHT = 22,
	ROW_GAP = 4,
	HEADING_HEIGHT = 16,
	DESCRIPTION_SIZE = 9,
	DESCRIPTION_LINE = DESCRIPTION_SIZE + 4,
	/* the list's scroll bar, in the gap beside it */
	LIST_BAR = 3,
	/* Steam's sign-in: "Linked" shown this long before its window closes,
	and an expired code made again at most this many times */
	LINK_DONE_TIME = 1500,
	LINK_REFRESHES = 3,
	/* PLAY that couldn't open its screen: said this long on the page */
	PLAY_FAILED_TIME = 8000,
};

/* the theme's (ui_style_color), and white the same in all */
#define COLOR_INSTALLED ui_style_color(UI_COLOR_GOOD)
#define COLOR_WAITING ui_style_color(UI_COLOR_WARNING)
#define COLOR_NOT_INSTALLED ui_style_color(UI_COLOR_STATUS)
#define COLOR_DESCRIPTION ui_style_color(UI_COLOR_TEXT_PALE)
enum
{
	COLOR_VALUE = 0xFFFFFFFF,
};

/* the list's rows */
enum
{
	_row_retry,
	_row_steam,
	_row_search,
	_row_show,
	_row_sort,
	_row_item,
	_row_page,
};

#define MAXIMUM_ROWS (WORKSHOP_BROWSER_MAXIMUM_ITEMS + 8)

/* where an item stands, for its page's buttons and its row's word */
enum
{
	_page_not_installed,
	_page_getting,
	_page_failed,
	_page_needs_mcc,
	_page_installed,
	_page_update,
	_page_updating,
};

/* an item page's buttons */
enum
{
	_option_get,
	_option_cancel,
	_option_retry,
	_option_store,
	_option_play,
	_option_update,
	_option_remove,
	_option_open_steam,
	/* STEAM'S MCC MAPS (an item in Steam's own folders while they're off):
	the row is the setting */
	_option_steam_maps,
	/* an old install path's setup row (debug.workshop_steamcmd only) */
	_option_setup,
	/* the old paths' item buttons (decision 4) */
	_option_old_subscribe,
	_option_old_sign_in,
	_option_old_unsubscribe,
	MAXIMUM_OPTIONS
};

/* the question window's */
enum
{
	_confirm_none,
	_confirm_remove,
};

static struct
{
	/* the rows as last built, and the one with the focus (its kind and
	item, so the list can change under it) */
	short row_kind[MAXIMUM_ROWS];
	short row_view[MAXIMUM_ROWS];
	short row_count;
	short row;
	short focus_kind;
	char focus_id[WORKSHOP_BROWSER_ID_SIZE];
	/* the focus to go to the first item once the list has one (the
	section's first look, a page turned) */
	boolean focus_first_item;
	boolean opened_once;
	/* the first row shown, and whether it follows the focus (the wheel
	scrolls without moving it); how many rows show (as last drawn) */
	short first;
	boolean follow;
	short shown_rows;
	float list_x0, list_y0, list_x1, list_y1;
	/* the item whose page shows on the right (the last item focused) */
	char page_id[WORKSHOP_BROWSER_ID_SIZE];
	/* the focus in the item's page, on its buttons */
	boolean page;
	short option;
	/* an item waits on Steam: the folders are watched with the Guide closed too */
	boolean waiting;
	/* the section showed last frame (the bridge stops when it no longer does) */
	boolean showing;
	/* the question window: which, the button with the focus (0 the left,
	CANCEL, as it opens), its window (for a click beside it), and the item
	asked about */
	short confirm;
	short modal_option;
	float modal_x0, modal_y0, modal_x1, modal_y1;
	char confirm_id[WORKSHOP_BROWSER_ID_SIZE];
	char confirm_title[WORKSHOP_BROWSER_TITLE_SIZE];
	/* LINK's QR: Steam's text for it, and its modules (made again when the text changes) */
	char link_text[256];
	short link_qr_size;
	unsigned char link_qr[BROWSER_CONNECT_QR_SIZE * BROWSER_CONNECT_QR_SIZE];
	/* Steam's sign-in window: up, when it saw the scan through, whether a
	code was shown (a failure after it: expired, made again), how often,
	and the item whose GET asked for it (fetched once linked) */
	boolean link_modal;
	unsigned long link_done_time;
	boolean link_shown_code, link_scanned, link_refreshing;
	short link_refreshes;
	char get_after_link[WORKSHOP_BROWSER_ID_SIZE];
	/* PLAY couldn't open its screen: the item and when */
	char play_failed_id[WORKSHOP_BROWSER_ID_SIZE];
	unsigned long play_failed_time;
	/* the chosen item's description as plain text, and whose it is */
	char plain_id[WORKSHOP_BROWSER_ID_SIZE];
	char plain[WORKSHOP_BROWSER_DESCRIPTION_SIZE];
	/* SEARCH's text box: typing into it (enter keeps, escape drops) */
	boolean searching;
	char search[64];
	unsigned long debug_search_time;
	/* a controller's search: the game's on-screen keyboard (as the map
	choosers' Search has it), the Guide closed under it and opened again
	on WORKSHOP once it is done */
	boolean keyboard;
	wchar_t keyboard_text[64];
	/* the item page's description (its words, then a pack's maps): the
	focus on it, the first line shown, how many lines it has and shows (as
	last drawn), its box and whose it is */
	boolean description_on;
	short description_scroll, description_count, description_shown;
	float description_x0, description_y0, description_x1, description_y1;
	char description_id[WORKSHOP_BROWSER_ID_SIZE];
	/* a line too long for its box, focused: read left to right from when
	it took the focus (guide_layout_marquee) */
	int marquee_key;
	unsigned long marquee_time, marquee_drawn;
} workshop_view;

/* ---------- private code */

static boolean link_needed(void);
static boolean description_scrolls(void);
static void description_scroll(short step);

/* the old install paths (the Steam client's subscribe, steamcmd): shown to
players only as workshop_decisions.h says */
static boolean old_paths(void)
{
	return !WORKSHOP_OLD_INSTALL_PATHS_DEBUG_ONLY || config_boolean("debug.workshop_steamcmd");
}

/* ---------- the rows */

static void rows_build(void)
{
	short count = (short)workshop_browser_count(), index, rows = 0;

	if (workshop_browser_status() == WORKSHOP_LIST_FAILED)
	{
		workshop_view.row_kind[rows] = _row_retry;
		workshop_view.row_view[rows++] = -1;
	}
	if (link_needed())
	{
		workshop_view.row_kind[rows] = _row_steam;
		workshop_view.row_view[rows++] = -1;
	}
	workshop_view.row_kind[rows] = _row_search;
	workshop_view.row_view[rows++] = -1;
	workshop_view.row_kind[rows] = _row_show;
	workshop_view.row_view[rows++] = -1;
	workshop_view.row_kind[rows] = _row_sort;
	workshop_view.row_view[rows++] = -1;
	for (index = 0; index < count && rows < MAXIMUM_ROWS - 1; index++)
	{
		struct workshop_browser_item const *item = workshop_browser_item(index);

		/* (the browser's own find, link and page rows are these rows here;
		its setup rows, the old paths', only with them) */
		if (!item || item->setup == WORKSHOP_ROW_FIND || item->setup == WORKSHOP_ROW_LINK ||
			item->setup == WORKSHOP_ROW_PAGE || (item->setup && !old_paths()))
		{
			continue;
		}
		workshop_view.row_kind[rows] = _row_item;
		workshop_view.row_view[rows++] = index;
	}
	if (guide_workshop_pages())
	{
		workshop_view.row_kind[rows] = _row_page;
		workshop_view.row_view[rows++] = -1;
	}
	workshop_view.row_count = rows;
}

static struct workshop_browser_item const *row_item(short row)
{
	if (row < 0 || row >= workshop_view.row_count || workshop_view.row_kind[row] != _row_item)
		return NULL;
	return workshop_browser_item(workshop_view.row_view[row]);
}

static short first_item_row(void)
{
	short row;

	for (row = 0; row < workshop_view.row_count; row++)
	{
		if (workshop_view.row_kind[row] == _row_item)
			return row;
	}
	return NONE;
}

static void focus_remember(void)
{
	struct workshop_browser_item const *item = row_item(workshop_view.row);

	workshop_view.focus_kind = workshop_view.row >= 0 && workshop_view.row < workshop_view.row_count ?
		workshop_view.row_kind[workshop_view.row] : _row_search;
	snprintf(workshop_view.focus_id, sizeof(workshop_view.focus_id), "%s", item ? item->id : "");
	if (item)
		snprintf(workshop_view.page_id, sizeof(workshop_view.page_id), "%s", item->id);
}

/* the rows built again and the focus found on them: the same row, or the
same item; one gone, the row now in its place */
static void rows_sync(void)
{
	short row;

	rows_build();
	/* (once the list has come: the items found installed show first,
	while it loads) */
	if (workshop_view.focus_first_item && first_item_row() != NONE &&
		workshop_browser_status() != WORKSHOP_LIST_LOADING && workshop_browser_status() != WORKSHOP_LIST_NOT_SET_UP)
	{
		workshop_view.focus_first_item = FALSE;
		workshop_view.row = first_item_row();
		workshop_view.follow = TRUE;
		workshop_view.first = 0;
		focus_remember();
		return;
	}
	for (row = 0; row < workshop_view.row_count; row++)
	{
		struct workshop_browser_item const *item = row_item(row);

		if (workshop_view.row_kind[row] == workshop_view.focus_kind &&
			(workshop_view.focus_kind != _row_item || (item && !strcmp(item->id, workshop_view.focus_id))))
		{
			workshop_view.row = row;
			return;
		}
	}
	workshop_view.row = (short)PIN(workshop_view.row, 0, MAX(workshop_view.row_count - 1, 0));
	/* (the item's page left with its item gone: the list, on the row in its
	place) */
	if (workshop_view.page && workshop_view.focus_kind == _row_item)
		workshop_view.page = FALSE;
	focus_remember();
}

static struct workshop_browser_item const *chosen(void)
{
	return row_item(workshop_view.row);
}

/* the item whose page shows: the focused one, else the last focused */
static short page_row(void)
{
	short row;

	if (chosen())
		return workshop_view.row;
	for (row = 0; workshop_view.page_id[0] && row < workshop_view.row_count; row++)
	{
		struct workshop_browser_item const *item = row_item(row);

		if (item && !strcmp(item->id, workshop_view.page_id))
			return row;
	}
	/* (that one not in the list now: the first item's) */
	return first_item_row();
}

/* ---------- where an item stands */

static boolean updating(struct workshop_browser_item const *item)
{
	return item->state == WORKSHOP_ITEM_INSTALLED && item->total > 0 && item->downloaded < item->total &&
		!item->steam_error[0];
}

static short page_state(struct workshop_browser_item const *item, short view)
{
	if (item->state == WORKSHOP_ITEM_INSTALLED)
	{
		if (updating(item))
			return _page_updating;
		return workshop_browser_update_offered(view) ? _page_update : _page_installed;
	}
	if (item->steam_error[0])
		return !strcmp(item->steam_error, "Not owned") ? _page_needs_mcc : _page_failed;
	if (item->state == WORKSHOP_ITEM_WAITING || item->state == WORKSHOP_ITEM_DOWNLOADING ||
		item->state == WORKSHOP_ITEM_INSTALLING)
	{
		return _page_getting;
	}
	return _page_not_installed;
}

/* a failure in the player's words: the downloader's, as few as there are */
static char const *failure_word(char const *error)
{
	static char const *const kept[] = { "Offline", "Disk full", "Too large", "Not owned" };
	short index;

	for (index = 0; index < (short)NUMBEROF(kept); index++)
	{
		if (!strcmp(error, kept[index]))
			return kept[index];
	}
	if (!strcmp(error, "Sign in"))
		return "Link Steam again";
	if (!strcmp(error, "Not connected"))
		return "Steam busy";
	return "Failed";
}

/* Steam's own folders' maps: listed (STEAM'S MCC MAPS on)? */
static boolean steam_maps_on(void)
{
	return config_boolean("game.workshop_maps");
}

/* whether an installed item's maps are in the map lists now */
static boolean item_listed(short view)
{
	if (!WORKSHOP_OWN_ITEMS_ALWAYS_LISTED)
		return steam_maps_on();
	return steam_maps_on() || workshop_browser_in_own_library(view);
}

static short options_of(struct workshop_browser_item const *item, short view, short *options)
{
	short count = 0;

	if (!item)
		return 0;
	if (item->setup)
	{
		if (workshop_browser_setup_button(view))
			options[count++] = _option_setup;
		return count;
	}
	/* (the old paths: no downloader of the game's own here) */
	if (!workshop_browser_link_offered() && old_paths() && item->state != WORKSHOP_ITEM_INSTALLED)
	{
		boolean bridge = workshop_browser_bridge_status() == STEAM_BRIDGE_READY;

		if (bridge && steam_bridge_sign_in_needed())
			options[count++] = _option_old_sign_in;
		else if (!(bridge && item->steam_subscribed))
			options[count++] = _option_old_subscribe;
		else if (steam_bridge_can_unsubscribe())
			options[count++] = _option_old_unsubscribe;
		return count;
	}
	switch (page_state(item, view))
	{
	case _page_not_installed:
		options[count++] = _option_get;
		break;
	case _page_getting:
		options[count++] = _option_cancel;
		break;
	case _page_failed:
		options[count++] = _option_retry;
		break;
	case _page_needs_mcc:
		options[count++] = _option_store;
		break;
	case _page_updating:
		break;
	default:
	{
		boolean own = workshop_browser_in_own_library(view);

		/* (its maps not in the lists: the row that turns them on in PLAY's
		place, the state its own control) */
		if (!item_listed(view))
			options[count++] = _option_steam_maps;
		else
			options[count++] = _option_play;
		if (own && workshop_browser_update_offered(view))
			options[count++] = item->steam_error[0] ? _option_retry : _option_update;
		if (own)
			options[count++] = _option_remove;
		else if (WORKSHOP_STEAM_ITEMS_OPEN_IN_STEAM)
			options[count++] = _option_open_steam;
		break;
	}
	}
	return count;
}

static char const *option_label(short view, short option)
{
	switch (option)
	{
	case _option_get: return "GET";
	case _option_cancel: return "CANCEL";
	case _option_retry: return "RETRY";
	case _option_store: return "STORE";
	case _option_play: return "PLAY";
	case _option_update: return "UPDATE";
	case _option_remove: return "REMOVE";
	case _option_open_steam: return "OPEN IN STEAM";
	case _option_steam_maps: return "STEAM'S MCC MAPS";
	case _option_setup:
	{
		char const *word = workshop_browser_setup_button(view);

		return word ? word : "";
	}
	case _option_old_subscribe: return "GET";
	case _option_old_sign_in: return "SIGN IN";
	case _option_old_unsubscribe: return "CANCEL";
	default: return "";
	}
}

int ui_overlay_keyboard(void);

/* ---------- SEARCH */

/* the text box: open (the keys typed before it don't count), and closed;
with a controller and the menus up, the game's on-screen keyboard instead */
static void search_start(void)
{
	struct key_stroke key;

#if defined(HALO_CONSOLE_MAP_BROWSER) || defined(HALO_MAP_LIST)
	if (!ui_overlay_keyboard() && main_menu_is_active())
	{
		char const *words = workshop_browser_search();
		short index;

		for (index = 0; words[index] && index < (short)NUMBEROF(workshop_view.keyboard_text) - 1; index++)
			workshop_view.keyboard_text[index] = (wchar_t)(unsigned char)words[index];
		workshop_view.keyboard_text[index] = 0;
		guide_close();
		workshop_view.keyboard = virtual_keyboard_launch_plain(workshop_view.keyboard_text,
			sizeof(workshop_view.keyboard_text), L"SEARCH");
		if (!workshop_view.keyboard)
			guide_open_workshop();
		return;
	}
#endif

	workshop_view.searching = TRUE;
	snprintf(workshop_view.search, sizeof(workshop_view.search), "%s", workshop_browser_search());
	while (input_get_key(&key))
		;
	platform_text_field(TRUE);
}

static void search_end(boolean keep)
{
	workshop_view.searching = FALSE;
	platform_text_field(FALSE);
	if (keep)
	{
		workshop_browser_set_search(workshop_view.search);
		workshop_view.page = FALSE;
		/* (the focus stays on SEARCH, the results under it) */
		workshop_view.focus_kind = _row_search;
		workshop_view.first = 0;
		workshop_view.follow = TRUE;
	}
}

/* the keys typed since the last frame (Ctrl+V pastes) */
static void search_type(void)
{
	struct key_stroke key;

	while (input_get_key(&key))
	{
		size_t length = strlen(workshop_view.search);

		if (key.key_code == _key_backspace)
		{
			if (length)
				workshop_view.search[length - 1] = 0;
		}
		else if (TEST_FLAG(key.modifier_flags, KEY_MODIFIER_CONTROL_BIT) && key.key_code == _key_v)
		{
			char clipboard[64];
			char const *character;

			if (!platform_clipboard_get(clipboard, sizeof(clipboard)))
				continue;
			for (character = clipboard; *character && length < sizeof(workshop_view.search) - 1; character++)
			{
				if (*character >= ' ' && *character <= '~')
					workshop_view.search[length++] = *character;
			}
			workshop_view.search[length] = 0;
		}
		else if ((unsigned char)key.ascii_code >= ' ' && (unsigned char)key.ascii_code <= '~' &&
			length < sizeof(workshop_view.search) - 1)
		{
			workshop_view.search[length] = key.ascii_code;
			workshop_view.search[length + 1] = 0;
		}
	}
}

/* ---------- Steam's link */

/* the game's own Steam download is the installer here, and Steam isn't
linked: GET and UPDATE need it (playing and removing never do) */
static boolean link_needed(void)
{
	return workshop_browser_link_offered() && !steam_link_linked(NULL, 0);
}

/* Steam's sign-in window over whatever shows, its QR up at once; get:
the item fetched once the scan links Steam (NULL: none) */
static void link_begin(char const *get)
{
	int link = steam_link_poll(NULL, 0, NULL, 0);

	snprintf(workshop_view.get_after_link, sizeof(workshop_view.get_after_link), "%s", get ? get : "");
	workshop_view.link_modal = TRUE;
	workshop_view.link_done_time = 0;
	workshop_view.link_shown_code = workshop_view.link_scanned = workshop_view.link_refreshing = FALSE;
	workshop_view.link_refreshes = 0;
	workshop_view.modal_option = 0;
	if (link != STEAM_LINK_STARTING && link != STEAM_LINK_WAITING && link != STEAM_LINK_SCANNED)
		steam_link_start();
}

static boolean link_busy(int link)
{
	return link == STEAM_LINK_STARTING || link == STEAM_LINK_WAITING || link == STEAM_LINK_SCANNED;
}

/* UNLINK's question: STEAM ACCOUNT pressed while linked */
static boolean unlink_asked(void)
{
	return steam_link_armed() && steam_link_linked(NULL, 0);
}

/* SETTINGS' STEAM ACCOUNT pressed: the sign-in window, or UNLINK's question
when linked (the focus on CANCEL: a press by mistake undoes nothing) */
static void link_settings_press(void)
{
	if (steam_link_linked(NULL, 0))
	{
		steam_link_set_armed(1);
		workshop_view.modal_option = 0;
		return;
	}
	link_begin(NULL);
}

/* SETTINGS' WORKSHOP FOLDER pressed: the system's folder picker */
static void folder_settings_press(void)
{
	if (!workshop_browser_choose_folder())
		platform_log("Workshop: no folder picker here");
}

/* the item an id names: its browser index (NONE: not listed now) */
static short view_of(char const *id)
{
	short index, count = (short)workshop_browser_count();

	for (index = 0; id && id[0] && index < count; index++)
	{
		struct workshop_browser_item const *item = workshop_browser_item(index);

		if (item && !item->setup && !strcmp(item->id, id))
			return index;
	}
	return NONE;
}

/* the window's steps, each frame: a code that expired made again (a few
times), the item asked for once linked, the window closed a moment after */
static void link_follow(void)
{
	int link = steam_link_poll(NULL, 0, NULL, 0);
	unsigned long now = system_milliseconds();

	workshop_browser_link_follow();
	if (link == STEAM_LINK_WAITING)
	{
		workshop_view.link_shown_code = TRUE;
		workshop_view.link_refreshing = FALSE;
	}
	if (link == STEAM_LINK_SCANNED)
		workshop_view.link_scanned = TRUE;
	if (!workshop_view.link_modal)
		return;
	if (link == STEAM_LINK_DONE || (link == STEAM_LINK_IDLE && steam_link_linked(NULL, 0)))
	{
		if (!workshop_view.link_done_time)
		{
			workshop_view.link_done_time = now ? now : 1;
			if (workshop_view.get_after_link[0])
			{
				short view = view_of(workshop_view.get_after_link);

				if (view != NONE && workshop_browser_subscribe(view))
					workshop_view.waiting = TRUE;
				workshop_view.get_after_link[0] = 0;
			}
		}
		else if (now - workshop_view.link_done_time >= LINK_DONE_TIME)
		{
			workshop_view.link_modal = FALSE;
			workshop_view.link_done_time = 0;
		}
		return;
	}
	/* (a code shown, never scanned, then failed: it expired; another) */
	if (link == STEAM_LINK_FAILED && workshop_view.link_shown_code && !workshop_view.link_scanned &&
		workshop_view.link_refreshes < LINK_REFRESHES)
	{
		workshop_view.link_refreshes++;
		workshop_view.link_shown_code = FALSE;
		workshop_view.link_refreshing = TRUE;
		platform_log("Steam: the sign-in code expired, another asked for");
		steam_link_start();
	}
}

/* the sign-in window's failure (no code at all, or expired too often):
RETRY and BACK */
static boolean link_failed(int link)
{
	return link == STEAM_LINK_FAILED && !workshop_view.link_refreshing;
}

boolean guide_workshop_link_modal(void)
{
	return workshop_view.confirm != _confirm_none || unlink_asked() || workshop_view.link_modal ||
		link_busy(steam_link_poll(NULL, 0, NULL, 0));
}

static void link_modal_close(void)
{
	if (link_busy(steam_link_poll(NULL, 0, NULL, 0)))
		steam_link_cancel();
	workshop_view.link_modal = FALSE;
	workshop_view.link_done_time = 0;
	workshop_view.get_after_link[0] = 0;
}

/* the question put away, unanswered */
static void confirm_close(void)
{
	workshop_view.confirm = _confirm_none;
	workshop_view.confirm_id[0] = 0;
	steam_link_set_armed(0);
}

void guide_workshop_link_modal_back(void)
{
	if (workshop_view.confirm != _confirm_none || unlink_asked())
	{
		confirm_close();
		return;
	}
	link_modal_close();
}

void guide_workshop_link_modal_press(void)
{
	int link;

	/* (a question: the left button CANCEL, the right its yes) */
	if (workshop_view.confirm == _confirm_remove)
	{
		short view = view_of(workshop_view.confirm_id);

		if (workshop_view.modal_option && view != NONE && workshop_browser_remove(view))
			workshop_view.option = 0;
		confirm_close();
		return;
	}
	if (unlink_asked())
	{
		if (workshop_view.modal_option)
		{
			platform_log("Steam: unlinked from the Guide");
			steam_link_unlink();
		}
		confirm_close();
		return;
	}
	/* (the sign-in window: failed, RETRY on the left and BACK; else its one
	button, CANCEL while it waits, DONE once linked) */
	link = steam_link_poll(NULL, 0, NULL, 0);
	if (link_failed(link) && !workshop_view.modal_option)
	{
		char get[WORKSHOP_BROWSER_ID_SIZE];

		snprintf(get, sizeof(get), "%s", workshop_view.get_after_link);
		link_begin(get[0] ? get : NULL);
		return;
	}
	link_modal_close();
}

void guide_workshop_link_modal_move(short step)
{
	workshop_view.modal_option = (short)(step < 0 ? 0 : 1);
}

void guide_workshop_link_modal_click(short index)
{
	workshop_view.modal_option = index ? 1 : 0;
	guide_workshop_link_modal_press();
}

void guide_workshop_link_modal_click_outside(short x, short y)
{
	/* (a click beside the window is its way out: CANCEL, BACK) */
	if (x < workshop_view.modal_x0 || x >= workshop_view.modal_x1 || y < workshop_view.modal_y0 ||
		y >= workshop_view.modal_y1)
	{
		guide_workshop_link_modal_back();
	}
}

char const *guide_workshop_link_note(void)
{
	/* (linked, with no keyring or Keychain to take the sign-in: said) */
	if (steam_link_linked(NULL, 0) && !steam_link_store_secure())
		return "Your Steam sign-in is kept in a private file, not encrypted";
	return NULL;
}

/* text measured in a font (guide_layout.h's measure: the font its context) */
static float measure_font(void *context, float size, char const *text)
{
	return ui_overlay_text_width((int)(intptr_t)context, size, text);
}

/* text of a kind (ui_style_text's) cut with "..." to width */
static void text_fit(int kind, char const *text, float width, char *out, int size)
{
	guide_layout_fit(text, width, ui_style_text_size(kind), measure_font, (void *)(intptr_t)ui_style_text_font(kind), 0,
		out, size);
}

static float kind_width(int kind, char const *text)
{
	return ui_overlay_text_width(ui_style_text_font(kind), ui_style_text_size(kind), text);
}

/* text of a kind on one line, cut to width */
static void text_line(int kind, float x, float y, float width, int align, char const *text)
{
	char shown[GUIDE_LAYOUT_LINE_SIZE];

	text_fit(kind, text, width, shown, sizeof(shown));
#ifndef HALO_RELEASE
	/* (the drawn text in the menus' own font past its box: said once, for
	the screenshot runs to catch what the layout test's font can't) */
	{
		static boolean said;

		if (!said && kind_width(kind, shown) > width + 0.5f)
		{
			said = TRUE;
			platform_log("Guide: text past its box (%.0f of %.0f): %s", kind_width(kind, shown), width, shown);
		}
	}
#endif
	ui_style_text(kind, x, y, align, shown);
}

/* (a focused line not drawn for this long lost the focus in between) */
#define MARQUEE_GAP 250

/* a focused line: as text_line, but read left to right when it doesn't
fit (the same marquee for every focused line: key tells them apart) */
static void text_line_focused(int kind, float x, float y, float width, int align, char const *text, int key)
{
	char shown[GUIDE_LAYOUT_LINE_SIZE];
	unsigned long now = system_milliseconds();

	if (key != workshop_view.marquee_key || now - workshop_view.marquee_drawn > MARQUEE_GAP)
	{
		workshop_view.marquee_key = key;
		workshop_view.marquee_time = now;
	}
	workshop_view.marquee_drawn = now;
	guide_layout_marquee(text, width, ui_style_text_size(kind), measure_font, (void *)(intptr_t)ui_style_text_font(kind),
		now - workshop_view.marquee_time, shown, sizeof(shown));
	ui_style_text(kind, x, y, align, shown);
}

/* the QR of Steam's sign-in in a square: dark on white with the quiet edge
readers need (made again when Steam's text changes); with no code yet, the
square alone, dimmed */
static void link_qr_draw(struct guide_rect square, char const *text)
{
	short qr_x, qr_y, run, size;
	float module;

	if (!text || !text[0])
	{
		ui_overlay_rect(square.x, square.y, square.width, square.height, 4, 0xFFFFFF1A);
		return;
	}
	if (strcmp(text, workshop_view.link_text))
	{
		snprintf(workshop_view.link_text, sizeof(workshop_view.link_text), "%s", text);
		workshop_view.link_qr_size = (short)browser_text_qr(text, workshop_view.link_qr);
	}
	size = workshop_view.link_qr_size;
	ui_overlay_rect(square.x, square.y, square.width, square.height, 4, 0xFFFFFFFF);
	if (size <= 0)
		return;
	module = square.width / (size + 8);
	for (qr_y = 0; qr_y < size; qr_y++)
	{
		for (qr_x = 0; qr_x < size; qr_x = (short)(qr_x + run))
		{
			unsigned char dark = workshop_view.link_qr[qr_y * size + qr_x];

			for (run = 0; qr_x + run < size && workshop_view.link_qr[qr_y * size + qr_x + run] == dark; run++)
				;
			if (dark)
				ui_overlay_rect(square.x + (qr_x + 4) * module, square.y + (qr_y + 4) * module, run * module + 0.3f,
					module + 0.3f, 0, 0x000000FF);
		}
	}
}

/* a window's button, its word in the middle */
static void modal_button(struct guide_rect box, char const *word, boolean on, short index,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index))
{
	ui_style_row(box.x, box.y, box.width, box.height, on);
	ui_overlay_text(UI_FONT_MENU, 10.0f, box.x + box.width * 0.5f, box.y + (box.height - 10) * 0.5f, UI_ALIGN_CENTER,
		COLOR_VALUE, word);
	if (target)
		target(box.x, box.y, box.x + box.width, box.y + box.height, GUIDE_WORKSHOP_TARGET_MODAL, index);
}

void guide_workshop_link_modal_render(float left, float top, float right, float bottom,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index))
{
	char text[256], error[24], status[48], question[96], line[160];
	int link = steam_link_poll(text, sizeof(text), error, sizeof(error));
	boolean asking = workshop_view.confirm != _confirm_none || unlink_asked();
	struct guide_rect area = { left, top, right - left, bottom - top };
	struct guide_modal_layout layout;
	unsigned int status_color = ui_style_color(UI_COLOR_LABEL);
	float center;

	guide_layout_modal(area, !asking, &layout);
	center = layout.window.x + layout.window.width * 0.5f;
	/* (the Guide under it dimmed past reading, the window solid) */
	ui_overlay_rect(-200, 0, 1040, 480, 0, 0x000000E6);
	ui_overlay_rect(layout.window.x, layout.window.y, layout.window.width, layout.window.height, 10,
		ui_style_color(UI_COLOR_WINDOW));
	ui_style_panel(layout.window.x, layout.window.y, layout.window.width, layout.window.height, 1.0f);
	workshop_view.modal_x0 = layout.window.x;
	workshop_view.modal_y0 = layout.window.y;
	workshop_view.modal_x1 = layout.window.x + layout.window.width;
	workshop_view.modal_y1 = layout.window.y + layout.window.height;
	if (asking)
	{
		char lines[2][GUIDE_LAYOUT_LINE_SIZE];
		int count, index;
		char const *yes;

		if (workshop_view.confirm == _confirm_remove)
		{
			char shown[WORKSHOP_BROWSER_TITLE_SIZE];

			size_t length;

			if (!ui_overlay_text_shown(UI_FONT_MENU, workshop_view.confirm_title, shown, sizeof(shown)) || !shown[0])
				snprintf(shown, sizeof(shown), "this item");
			for (length = strlen(shown); length > 0 && shown[length - 1] == ' '; length--)
				shown[length - 1] = 0;
			snprintf(question, sizeof(question), "Remove %s?", shown);
			snprintf(line, sizeof(line), "Its files are deleted.");
			yes = "REMOVE";
		}
		else
		{
			snprintf(question, sizeof(question), "Unlink Steam?");
			snprintf(line, sizeof(line), "Your maps stay. Getting and updating maps needs Steam again.");
			yes = "UNLINK";
		}
		text_line(UI_TEXT_TITLE, center, layout.title.y, layout.title.width, UI_ALIGN_CENTER, question);
		count = guide_layout_wrap(line, layout.line.width, ui_style_text_size(UI_TEXT_LINE), measure_font,
			(void *)(intptr_t)ui_style_text_font(UI_TEXT_LINE), 2, lines, NULL);
		for (index = 0; index < count; index++)
			ui_style_text(UI_TEXT_LINE, center, layout.line.y + index * 12, UI_ALIGN_CENTER, lines[index]);
		modal_button(layout.cancel, "CANCEL", !workshop_view.modal_option, 0, target);
		modal_button(layout.button, yes, workshop_view.modal_option != 0, 1, target);
		return;
	}
	{
		boolean done = link == STEAM_LINK_DONE || (!link_busy(link) && link != STEAM_LINK_FAILED &&
			steam_link_linked(NULL, 0));
		boolean failed = !done && link_failed(link);

		text_line(UI_TEXT_TITLE, center, layout.title.y, layout.title.width, UI_ALIGN_CENTER, "LINK STEAM");
		link_qr_draw(layout.qr, link == STEAM_LINK_WAITING || link == STEAM_LINK_SCANNED ? text : NULL);
		text_line(UI_TEXT_LINE, center, layout.line.y, layout.line.width, UI_ALIGN_CENTER, "Scan with the Steam app");
		if (done)
		{
			snprintf(status, sizeof(status), "Linked");
			status_color = COLOR_INSTALLED;
		}
		else if (workshop_view.link_refreshing && link != STEAM_LINK_WAITING)
			snprintf(status, sizeof(status), "Expired, new code");
		else if (link == STEAM_LINK_SCANNED)
			snprintf(status, sizeof(status), "Approve on your phone");
		else if (link == STEAM_LINK_WAITING)
			snprintf(status, sizeof(status), "Waiting");
		else if (failed && workshop_view.link_shown_code && !workshop_view.link_scanned)
			snprintf(status, sizeof(status), "Expired");
		else if (failed)
			snprintf(status, sizeof(status), "%s", !strcmp(error, "Offline") ? "Offline" : "Failed");
		else
			snprintf(status, sizeof(status), "Getting a code");
		if (failed)
			status_color = COLOR_WAITING;
		{
			char shown[GUIDE_LAYOUT_LINE_SIZE];

			text_fit(UI_TEXT_LABEL, status, layout.status.width, shown, sizeof(shown));
			ui_overlay_text(ui_style_text_font(UI_TEXT_LABEL), ui_style_text_size(UI_TEXT_LABEL), center,
				layout.status.y, UI_ALIGN_CENTER, status_color, shown);
		}
		/* (failed: RETRY, with the focus, and BACK, side by side where the
		one button is) */
		if (failed)
		{
			struct guide_rect retry = layout.button, back = layout.button;
			float each = 100, gap = 8;

			retry.width = back.width = each;
			retry.x = center - gap * 0.5f - each;
			back.x = center + gap * 0.5f;
			modal_button(retry, "RETRY", !workshop_view.modal_option, 0, target);
			modal_button(back, "BACK", workshop_view.modal_option != 0, 1, target);
			return;
		}
		modal_button(layout.button, done ? "DONE" : "CANCEL", TRUE, 0, target);
	}
}

/* ---------- PLAY

To the menus' own Create Game or level select, in the menu set chosen, a
game or lobby left first if the player says so (menu_functions.c). */

static boolean play(struct workshop_browser_item const *item)
{
	if (!item || item->setup || item->state != WORKSHOP_ITEM_INSTALLED)
		return FALSE;
	if (pc_menu_workshop_play(item->id, item->title))
	{
		workshop_view.play_failed_id[0] = 0;
		guide_close();
		return TRUE;
	}
	/* (the screen couldn't open: said on the page, never silence) */
	platform_log("Workshop: PLAY on item %s could not open its screen", item->id);
	snprintf(workshop_view.play_failed_id, sizeof(workshop_view.play_failed_id), "%s", item->id);
	workshop_view.play_failed_time = system_milliseconds() | 1;
	return FALSE;
}

#ifndef HALO_RELEASE
/* debug.workshop_play: PLAY on that installed item once, a few seconds
after the main menu or a game is up, for tests */
static void debug_play(void)
{
	static unsigned long since;
	static boolean done, opened;
	char const *setting = config_string("debug.workshop_play");
	char id[WORKSHOP_BROWSER_ID_SIZE];
	unsigned long wait = 4000;
	char const *colon;
	int index;

	if (done || !setting[0])
		return;
	/* (":lobby" at its end: in a lobby this machine hosts; ":game": in a
	game) */
	if ((!main_menu_is_active() && !game_map_loaded()) ||
		(strstr(setting, ":lobby") && !(main_menu_is_active() && global_network_game_server_get())) ||
		(strstr(setting, ":game") && main_menu_is_active()))
	{
		since = 0;
		return;
	}
	if (!since)
		since = system_milliseconds() | 1;
	snprintf(id, sizeof(id), "%s", setting);
	colon = strchr(setting, ':');
	if (colon)
	{
		id[colon - setting < (int)sizeof(id) ? colon - setting : (int)sizeof(id) - 1] = 0;
		wait = (unsigned long)(atof(colon + 1) * 1000);
	}
	if (system_milliseconds() - since < wait)
		return;
	if (!opened)
	{
		opened = TRUE;
		workshop_browser_open(0);
	}
	if (workshop_browser_poll())
		mcc_maps_rescan();
	for (index = 0; index < workshop_browser_count(); index++)
	{
		struct workshop_browser_item const *item = workshop_browser_item(index);

		if (item && !item->setup && !strcmp(item->id, id) && item->state == WORKSHOP_ITEM_INSTALLED)
		{
			done = TRUE;
			platform_log("Workshop: debug.workshop_play: PLAY on item %s", id);
			play(item);
			return;
		}
	}
}
#else
static void debug_play(void)
{
}
#endif

static void option_run(short option)
{
	short view = workshop_view.row_view[workshop_view.row];
	struct workshop_browser_item const *item = chosen();

	if (!item)
		return;
	switch (option)
	{
	case _option_get:
	case _option_old_subscribe:
		/* (not linked: the sign-in window, and the item once it's scanned) */
		if (link_needed())
		{
			link_begin(item->id);
			return;
		}
		if (workshop_browser_subscribe(view))
			workshop_view.waiting = TRUE;
		workshop_view.option = 0;
		return;
	case _option_cancel:
		workshop_browser_cancel(view);
		workshop_view.option = 0;
		return;
	case _option_retry:
		if (link_needed())
		{
			link_begin(item->state == WORKSHOP_ITEM_INSTALLED ? NULL : item->id);
			return;
		}
		/* (an update's failure: the update again; a GET's: the GET) */
		if (item->state == WORKSHOP_ITEM_INSTALLED)
			workshop_browser_update(view);
		else if (workshop_browser_subscribe(view))
			workshop_view.waiting = TRUE;
		workshop_view.option = 0;
		return;
	case _option_store:
		platform_open_url(MCC_STORE_PAGE);
		return;
	case _option_play:
		play(item);
		return;
	case _option_update:
		if (link_needed())
		{
			link_begin(NULL);
			return;
		}
		if (workshop_browser_update(view))
			workshop_view.waiting = TRUE;
		return;
	case _option_remove:
		/* (asked first, the focus on CANCEL) */
		workshop_view.confirm = _confirm_remove;
		workshop_view.modal_option = 0;
		snprintf(workshop_view.confirm_id, sizeof(workshop_view.confirm_id), "%s", item->id);
		snprintf(workshop_view.confirm_title, sizeof(workshop_view.confirm_title), "%s", item->title);
		return;
	case _option_open_steam:
		workshop_browser_open_in_steam(view);
		return;
	case _option_steam_maps:
		/* (the row is the setting: pressed, it turns, the player's own choice) */
		config_write_boolean("game.workshop_maps", !steam_maps_on());
		mcc_maps_rescan();
		return;
	case _option_setup:
		workshop_browser_setup_press(view);
		return;
	case _option_old_sign_in:
		steam_bridge_sign_in();
		return;
	case _option_old_unsubscribe:
		workshop_browser_unsubscribe(view);
		return;
	default:
		return;
	}
}

static unsigned int state_color(short state)
{
	switch (state)
	{
	case _page_installed:
	case _page_update: return COLOR_INSTALLED;
	case _page_getting:
	case _page_updating:
	case _page_failed:
	case _page_needs_mcc: return COLOR_WAITING;
	default: return COLOR_NOT_INSTALLED;
	}
}

static char const *state_word(short state)
{
	switch (state)
	{
	case _page_getting: return "Getting";
	case _page_failed: return "Failed";
	case _page_needs_mcc: return "Needs MCC";
	case _page_installed: return "Installed";
	case _page_update: return "Update";
	case _page_updating: return "Updating";
	default: return "Not installed";
	}
}

/* a row's word and color: a setup row's step, an item's state */
static char const *item_word(struct workshop_browser_item const *item, short view)
{
	if (item->setup)
		return workshop_browser_step_name(item->step);
	return state_word(page_state(item, view));
}

static unsigned int item_color(struct workshop_browser_item const *item, short view)
{
	if (!item->setup)
		return state_color(page_state(item, view));
	switch (item->step)
	{
	case WORKSHOP_STEP_READY: return COLOR_INSTALLED;
	case WORKSHOP_STEP_WORKING: return COLOR_WAITING;
	case WORKSHOP_STEP_NOT_AVAILABLE: return COLOR_DESCRIPTION;
	default: return COLOR_NOT_INSTALLED;
	}
}

/* an item's title as drawn: the menus' fonts have no Chinese, Japanese or
Korean letters, which are left out rather than drawn as boxes, with the
spaces around them (ui_overlay_text_shown); a title of only those is the
item's number */
static char const *shown_title(struct workshop_browser_item const *item)
{
	static char shown[sizeof(item->title)];
	size_t length;

	if (item->setup)
		return item->title;

	if (!ui_overlay_text_shown(UI_FONT_MENU, item->title, shown, sizeof(shown)) || !shown[0])
		snprintf(shown, sizeof(shown), "Workshop item %s", item->id);
	/* (no space left before a "?" that follows it) */
	for (length = strlen(shown); length > 0 && shown[length - 1] == ' '; length--)
		shown[length - 1] = 0;
	return shown;
}

/* ---------- public code */

boolean guide_workshop_shown(void)
{
	return workshop_browser_offered() != 0;
}

boolean guide_workshop_page_step(short step)
{
	/* (step 0: RETRY) */
	if (!step)
		return workshop_browser_retry() != 0;
	if (workshop_view.searching || !(step > 0 ? workshop_browser_page_next() : workshop_browser_page_previous()))
		return FALSE;
	/* (the new page's first item gets the focus once it is here) */
	workshop_view.focus_first_item = TRUE;
	workshop_view.page = FALSE;
	workshop_view.option = 0;
	workshop_view.first = 0;
	return TRUE;
}

boolean guide_workshop_pages(void)
{
	return workshop_browser_page_offered(1) || workshop_browser_page_offered(-1);
}

boolean guide_workshop_page_offered(short step)
{
	return workshop_browser_page_offered(step) != 0;
}

void guide_workshop_open(void)
{
	static boolean debug_done;
	char const *debug_search = config_string("debug.workshop_search");

	workshop_view.page = FALSE;
	workshop_view.option = 0;
	workshop_view.description_on = FALSE;
	/* (the first look: the focus on the first item, once there is one; back
	again: the row left) */
	if (!workshop_view.opened_once)
	{
		workshop_view.opened_once = TRUE;
		workshop_view.focus_first_item = TRUE;
		workshop_view.focus_kind = _row_search;
	}
	workshop_view.follow = TRUE;
	workshop_browser_open(0);
	/* (debug.workshop_search: SEARCH's text box with these words for a few
	seconds, then searched, for screenshots) */
	if (!debug_done && debug_search && debug_search[0])
	{
		debug_done = TRUE;
		workshop_view.focus_first_item = FALSE;
		workshop_view.focus_kind = _row_search;
		search_start();
		snprintf(workshop_view.search, sizeof(workshop_view.search), "%s", debug_search);
		workshop_view.debug_search_time = system_milliseconds();
	}
}

void guide_workshop_update(boolean showing)
{
	short index, installed;

	/* (PLAY's item handed over once the main menu is back) */
	pc_menu_workshop_play_update();
	debug_play();

	workshop_browser_own_library();
	settings_rows_set_steam_link_action(workshop_browser_link_offered() ? link_settings_press : NULL);
	settings_rows_set_folder_action(folder_settings_press);
	settings_rows_set_workshop_offered(workshop_browser_offered());
	workshop_browser_folder_follow();
	link_follow();
	/* (the on-screen keyboard done: its words searched, the Guide back on WORKSHOP) */
#if defined(HALO_CONSOLE_MAP_BROWSER) || defined(HALO_MAP_LIST)
	if (workshop_view.keyboard && !virtual_keyboard_active())
	{
		workshop_view.keyboard = FALSE;
		if (virtual_keyboard_last_exit_saved_text())
		{
			char words[64];

			for (index = 0; workshop_view.keyboard_text[index] && index < (short)sizeof(words) - 1; index++)
				words[index] = workshop_view.keyboard_text[index] < 0x80 ? (char)workshop_view.keyboard_text[index] : '?';
			words[index] = 0;
			workshop_browser_set_search(words);
		}
		workshop_view.page = FALSE;
		workshop_view.focus_kind = _row_search;
		guide_open_workshop();
	}
#endif
	{
		void browser_screen_debug_workshop_offer(void);

		browser_screen_debug_workshop_offer();
	}
	if (workshop_view.searching && workshop_view.debug_search_time &&
		system_milliseconds() - workshop_view.debug_search_time > 10000)
	{
		workshop_view.debug_search_time = 0;
		search_end(TRUE);
	}
	if (workshop_view.searching)
	{
		if (showing)
			search_type();
		else
			search_end(FALSE);
	}
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
				guide_toast("WORKSHOP", shown_title(item));
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
	rows_sync();
	return workshop_view.row_count;
}

void guide_workshop_move(short step)
{
	short options[MAXIMUM_OPTIONS];

	if (workshop_view.searching)
		return;
	rows_sync();
	if (workshop_view.page)
	{
		short option_count = options_of(chosen(), workshop_view.row_view[workshop_view.row], options);

		/* (the description: scrolled while it has the focus; it takes it
		past the first button up or the last down, when it scrolls) */
		if (workshop_view.description_on)
		{
			description_scroll(step);
			return;
		}
		if (description_scrolls() && (option_count == 0 || (step < 0 && workshop_view.option <= 0) ||
			(step > 0 && workshop_view.option >= option_count - 1)))
		{
			workshop_view.description_on = TRUE;
			return;
		}
		if (option_count > 0)
			workshop_view.option = (short)PIN(workshop_view.option + step, 0, option_count - 1);
		return;
	}
	workshop_view.focus_first_item = FALSE;
	if (workshop_view.row_count > 0)
		workshop_view.row = (short)PIN(workshop_view.row + step, 0, workshop_view.row_count - 1);
	workshop_view.follow = TRUE;
	focus_remember();
}

/* SHOW's and SORT's values, stepped (the value after the last the first) */
static void show_step(short step)
{
	workshop_browser_set_filter((workshop_browser_filter() + step + WORKSHOP_FILTER_COUNT) % WORKSHOP_FILTER_COUNT);
	workshop_view.first = 0;
}

static void sort_step(short step)
{
	workshop_browser_set_sort((workshop_browser_sort() + step + WORKSHOP_SORT_COUNT) % WORKSHOP_SORT_COUNT);
	workshop_view.first = 0;
}

void guide_workshop_side(short step)
{
	if (workshop_view.searching || workshop_view.page)
		return;
	rows_sync();
	switch (workshop_view.row_kind[workshop_view.row])
	{
	case _row_show:
		show_step(step);
		break;
	case _row_sort:
		sort_step(step);
		break;
	case _row_page:
		guide_workshop_page_step(step);
		/* (the focus stays on PAGE for another turn) */
		workshop_view.focus_first_item = FALSE;
		break;
	default:
		break;
	}
}

/* a row's A (a click on it is the same) */
static void row_press(void)
{
	short options[MAXIMUM_OPTIONS];

	switch (workshop_view.row_kind[workshop_view.row])
	{
	case _row_retry:
		workshop_browser_retry();
		return;
	case _row_steam:
		link_begin(NULL);
		return;
	case _row_search:
		search_start();
		return;
	case _row_show:
		show_step(1);
		return;
	case _row_sort:
		sort_step(1);
		return;
	case _row_page:
		if (!guide_workshop_page_step(1))
			guide_workshop_page_step(-1);
		workshop_view.focus_first_item = FALSE;
		return;
	default:
		break;
	}
	/* (an item: its page, the focus on its first button) */
	if (!options_of(chosen(), workshop_view.row_view[workshop_view.row], options) && !description_scrolls())
		return;
	workshop_view.page = TRUE;
	workshop_view.option = 0;
	workshop_view.description_on = FALSE;
}

void guide_workshop_activate(void)
{
	short options[MAXIMUM_OPTIONS], count;

	if (workshop_view.searching)
	{
		search_end(TRUE);
		return;
	}
	rows_sync();
	if (!workshop_view.page)
	{
		row_press();
		return;
	}
	/* (A on the description gives the buttons the focus again) */
	if (workshop_view.description_on)
	{
		workshop_view.description_on = FALSE;
		return;
	}
	count = options_of(chosen(), workshop_view.row_view[workshop_view.row], options);
	if (workshop_view.option >= 0 && workshop_view.option < count)
		option_run(options[workshop_view.option]);
}

boolean guide_workshop_back(void)
{
	if (workshop_view.searching)
	{
		search_end(FALSE);
		return TRUE;
	}
	if (!workshop_view.page)
		return FALSE;
	/* (B on the description: back to the buttons, the page kept) */
	if (workshop_view.description_on)
	{
		workshop_view.description_on = FALSE;
		return TRUE;
	}
	/* (back to the list, on the same row) */
	workshop_view.page = FALSE;
	workshop_view.follow = TRUE;
	return TRUE;
}

/* the wheel over the description scrolls it, over the list the list (the
focus where it was): TRUE when taken. A click on the description gives it
the focus */
static boolean over_description(short x, short y)
{
	return x >= workshop_view.description_x0 && x < workshop_view.description_x1 &&
		y >= workshop_view.description_y0 && y < workshop_view.description_y1;
}

boolean guide_workshop_wheel(short x, short y, short steps)
{
	if (over_description(x, y) && workshop_view.description_count > workshop_view.description_shown)
	{
		description_scroll((short)-steps);
		return TRUE;
	}
	if (x >= workshop_view.list_x0 && x < workshop_view.list_x1 && y >= workshop_view.list_y0 &&
		y < workshop_view.list_y1)
	{
		short most = (short)MAX(workshop_view.row_count - workshop_view.shown_rows, 0);

		workshop_view.first = (short)PIN(workshop_view.first - steps, 0, most);
		workshop_view.follow = FALSE;
		return TRUE;
	}
	return FALSE;
}

boolean guide_workshop_click(short x, short y)
{
	if (!over_description(x, y) || workshop_view.description_count <= workshop_view.description_shown)
		return FALSE;
	rows_sync();
	if (!chosen())
		return FALSE;
	workshop_view.page = TRUE;
	workshop_view.description_on = TRUE;
	return TRUE;
}

void guide_workshop_row_click(short row, boolean touch)
{
	(void)touch;
	rows_sync();
	if (row < 0 || row >= workshop_view.row_count)
		return;
	/* (one click does the row's A: an item's page opens) */
	workshop_view.page = FALSE;
	workshop_view.row = row;
	workshop_view.focus_first_item = FALSE;
	focus_remember();
	if (workshop_view.searching)
		search_end(FALSE);
	row_press();
}

void guide_workshop_arrow_click(short index)
{
	short row = (short)(index / 2);

	rows_sync();
	if (row < 0 || row >= workshop_view.row_count || workshop_view.searching)
		return;
	workshop_view.page = FALSE;
	workshop_view.row = row;
	workshop_view.focus_first_item = FALSE;
	focus_remember();
	guide_workshop_side((index & 1) ? 1 : -1);
}

void guide_workshop_option_click(short option)
{
	rows_sync();
	/* (the page shown while the focus is on SEARCH and such: its item's) */
	if (!chosen() && page_row() != NONE)
	{
		workshop_view.row = page_row();
		focus_remember();
	}
	if (!chosen())
		return;
	workshop_view.page = TRUE;
	workshop_view.option = option;
	/* (a click on a button presses it, the description's focus or not) */
	workshop_view.description_on = FALSE;
	guide_workshop_activate();
}

char const *guide_workshop_prompt(void)
{
	if (workshop_view.searching)
		return "SEARCH";
	if (workshop_view.page)
		return workshop_view.description_on ? "BUTTONS" : "SELECT";
	switch (workshop_view.row_kind[workshop_view.row])
	{
	case _row_show:
	case _row_sort: return "CHANGE";
	case _row_page: return "NEXT PAGE";
	case _row_search: return "SEARCH";
	case _row_steam: return "LINK";
	case _row_retry: return "RETRY";
	default: return "OPEN";
	}
}

boolean guide_workshop_in_page(void)
{
	return workshop_view.page || workshop_view.searching;
}

/* ---------- drawing */

/* the chosen item's description as plain text (Steam's markup stripped),
made once an item */
static char const *plain_description(struct workshop_browser_item const *item)
{
	if (strcmp(workshop_view.plain_id, item->id) || !workshop_view.plain[0])
	{
		snprintf(workshop_view.plain_id, sizeof(workshop_view.plain_id), "%s", item->id);
		guide_layout_plain(item->description, workshop_view.plain, sizeof(workshop_view.plain));
	}
	return workshop_view.plain;
}

/* what of an installed item can't be listed in the map lists, and why
(steam_workshop_unlisted), after its page's text: a short plain status */
#define PAGE_UNLISTED_LINES 6

static void page_unlisted(char const *id, short listed, char *text, size_t size)
{
	char map[64], reason[STEAM_WORKSHOP_REASON_SIZE];
	int count = steam_workshop_item_unlisted(id, 0, NULL, NULL, NULL, NULL), which, pass, maps = 0, shown = 0;
	size_t length;

	for (which = 0; which < count; which++)
	{
		steam_workshop_item_unlisted(id, which, NULL, map, NULL, NULL);
		maps += map[0] != 0;
	}
	for (pass = 0; pass < 2; pass++)
	{
		for (which = 0; which < count; which++)
		{
			steam_workshop_item_unlisted(id, which, NULL, map, reason, NULL);
			if (!map[0] != !pass)
				continue;
			length = strlen(text);
			if (!pass)
			{
				snprintf(text + length, size - length, "%s%s: %s", length ? "\n\n" : "",
					listed ? "Some maps can't be listed" : "Can't be listed", reason);
				continue;
			}
			if (!shown)
			{
				snprintf(text + length, size - length, "%s%d of %d maps can't be listed:", length ? "\n\n" : "",
					maps, maps + listed);
				length = strlen(text);
			}
			if (shown++ < PAGE_UNLISTED_LINES)
				snprintf(text + length, size - length, "\n%s: %s", map, reason);
		}
	}
	length = strlen(text);
	if (shown > PAGE_UNLISTED_LINES)
		snprintf(text + length, size - length, "\n...and %d more", shown - PAGE_UNLISTED_LINES);
}

/* the chosen item's page text: its author's words (never words of ours in
their place), then, for a pack, the maps in it (the installed item's, from
the map catalog: mcc_maps\<id>\...) */
static char const *page_text(struct workshop_browser_item const *item)
{
	static char text[WORKSHOP_BROWSER_DESCRIPTION_SIZE + 1024];
	char prefix[WORKSHOP_BROWSER_ID_SIZE + 16];
	short index, count, maps = 0;
	size_t length;

	snprintf(text, sizeof(text), "%s", plain_description(item));
	if (item->setup || item->state != WORKSHOP_ITEM_INSTALLED)
		return text;
	snprintf(prefix, sizeof(prefix), MCC_MAPS_LEVEL_PREFIX "%s\\", item->id);
	count = mcc_maps_count(FALSE);
	for (index = 0; index < count; index++)
	{
		char const *level = mcc_maps_level_name(index);

		if (level && !_strnicmp(level, prefix, strlen(prefix)))
			maps++;
	}
	page_unlisted(item->id, maps, text, sizeof(text));
	if (maps < 2)
		return text;
	length = strlen(text);
	snprintf(text + length, sizeof(text) - length, "%sMaps in this pack (%d):", length ? "\n\n" : "", maps);
	for (index = 0; index < count; index++)
	{
		char const *level = mcc_maps_level_name(index);
		wchar_t const *name = mcc_maps_name(index);
		short character;

		if (!level || _strnicmp(level, prefix, strlen(prefix)))
			continue;
		length = strlen(text);
		if (length + 4 >= sizeof(text))
			break;
		text[length++] = '\n';
		for (character = 0; name && name[character] && length + 2 < sizeof(text); character++)
			text[length++] = name[character] < 0x80 ? (char)name[character] : '?';
		text[length] = 0;
	}
	return text;
}

/* the description's box: its lines from the first scrolled to, a bar at its
right when they don't all show (the words kept clear of it), its edge lit
while it has the focus */
#define DESCRIPTION_MAXIMUM_LINES 160
#define DESCRIPTION_BAR 3.0f
#define DESCRIPTION_BAR_ROOM 10.0f

static void description_render(struct workshop_browser_item const *item, struct guide_rect box, short lines)
{
	static char wrapped[DESCRIPTION_MAXIMUM_LINES][GUIDE_LAYOUT_LINE_SIZE];
	struct guide_rect track, thumb;
	int count, index;
	float y = box.y, width = box.width - DESCRIPTION_BAR_ROOM;

	if (strcmp(workshop_view.description_id, item->id))
	{
		snprintf(workshop_view.description_id, sizeof(workshop_view.description_id), "%s", item->id);
		workshop_view.description_scroll = 0;
		workshop_view.description_on = FALSE;
	}
	count = guide_layout_wrap(page_text(item), width, DESCRIPTION_SIZE, measure_font, (void *)(intptr_t)UI_FONT_MENU,
		DESCRIPTION_MAXIMUM_LINES, wrapped, NULL);
	workshop_view.description_count = (short)count;
	workshop_view.description_shown = lines;
	workshop_view.description_scroll = (short)guide_layout_scroll_clamp(workshop_view.description_scroll, count, lines);
	workshop_view.description_x0 = box.x;
	workshop_view.description_y0 = box.y;
	workshop_view.description_x1 = box.x + box.width;
	workshop_view.description_y1 = box.y + lines * DESCRIPTION_LINE;
	if (workshop_view.description_on && workshop_view.page)
		ui_overlay_outline(box.x - 4, box.y - 3, box.width + 8, lines * DESCRIPTION_LINE + 4, 4, 1.0f,
			COLOR_VALUE);
	for (index = workshop_view.description_scroll; index < count && index < workshop_view.description_scroll + lines;
		index++)
	{
		ui_overlay_text(UI_FONT_MENU, DESCRIPTION_SIZE, box.x, y, UI_ALIGN_LEFT, COLOR_DESCRIPTION, wrapped[index]);
		y += DESCRIPTION_LINE;
	}
	track.x = box.x + box.width - DESCRIPTION_BAR;
	track.y = box.y;
	track.width = DESCRIPTION_BAR;
	track.height = lines * DESCRIPTION_LINE - 2;
	if (guide_layout_scroll_thumb(track, count, lines, workshop_view.description_scroll, 8.0f, &thumb))
	{
		ui_overlay_rect(track.x, track.y, track.width, track.height, 1.5f, 0xFFFFFF1F);
		ui_overlay_rect(thumb.x, thumb.y, thumb.width, thumb.height, 1.5f,
			workshop_view.description_on && workshop_view.page ? COLOR_VALUE : 0xFFFFFF80);
	}
}

/* whether the description has more lines than show (it can be scrolled) */
static boolean description_scrolls(void)
{
	return workshop_view.description_count > workshop_view.description_shown &&
		chosen() && !strcmp(workshop_view.description_id, chosen()->id);
}

static void description_scroll(short step)
{
	workshop_view.description_scroll = (short)guide_layout_scroll_clamp(workshop_view.description_scroll + step,
		workshop_view.description_count, workshop_view.description_shown);
}

/* the line over an item's buttons: what is happening, or why it stopped
(NULL: none); a download's share in *bar (else -1) */
static char const *page_note(struct workshop_browser_item const *item, short view, char *line, int size, float *bar)
{
	short state = page_state(item, view);

	*bar = -1;
	if (item->setup)
		return NULL;
	if (workshop_view.play_failed_id[0] && !strcmp(workshop_view.play_failed_id, item->id) &&
		system_milliseconds() - workshop_view.play_failed_time < PLAY_FAILED_TIME)
	{
		return item_listed(view) ? "Couldn't open the game's screen for it" : "Its maps aren't in the map lists";
	}
	if (state == _page_failed || (item->steam_error[0] && item->state == WORKSHOP_ITEM_INSTALLED))
		return failure_word(item->steam_error);
	if (state == _page_needs_mcc)
		return "Needs MCC";
	if ((state == _page_getting || state == _page_updating) && item->total > 0)
	{
		char done[24], total[24];

		workshop_browser_size_text((long long)item->downloaded, done, sizeof(done));
		workshop_browser_size_text((long long)item->total, total, sizeof(total));
		snprintf(line, (size_t)size, "%s %d%% - %s of %s", state == _page_updating ? "Updating" : "Getting",
			(int)(item->downloaded * 100 / item->total), done[0] ? done : "0 MB", total);
		*bar = (float)item->downloaded / (float)item->total;
		*bar = *bar < 0 ? 0 : *bar > 1 ? 1 : *bar;
		return line;
	}
	if (state == _page_getting)
		return "Starting";
	return NULL;
}

/* the list's heading: its page, and where the list stands */
static void heading_render(float left, float top, short status)
{
	char heading[48], word[40];
	float right = left + LIST_WIDTH;
	int busy = workshop_browser_busy_seconds();

	if (workshop_browser_filter() != WORKSHOP_FILTER_INSTALLED)
		snprintf(heading, sizeof(heading), "MCC WORKSHOP - PAGE %d", workshop_browser_page());
	else
		snprintf(heading, sizeof(heading), "MCC WORKSHOP");
	ui_style_text(UI_TEXT_HEADING, left, top, UI_ALIGN_LEFT, heading);
	word[0] = 0;
	if (status == WORKSHOP_LIST_LOADING)
		snprintf(word, sizeof(word), "Loading");
	else if (status == WORKSHOP_LIST_FAILED && busy)
		snprintf(word, sizeof(word), "Steam busy, %ds", busy);
	else if (status == WORKSHOP_LIST_FAILED)
		snprintf(word, sizeof(word), "%s", workshop_browser_list_why());
	if (word[0])
		ui_style_text(UI_TEXT_LABEL, right, top + 1, UI_ALIGN_RIGHT, word);
}

static float row_height(short row)
{
	return workshop_view.row_kind[row] == _row_item ? ROW_HEIGHT : CONTROL_HEIGHT;
}

/* SHOW's, SORT's and the page row's arrows, for the pointer (as
ui_style_setting puts them) */
static void row_arrows(short row, float x, float y, float width, float height, char const *value,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index))
{
	float size = ui_style_text_size(UI_TEXT_NAME) * 0.9f, right = x + width - size * 1.2f;
	float value_width = ui_overlay_text_width(UI_FONT_MENU, size, value);
	float left_arrow = right - size * 1.8f - value_width;

	if (!target)
		return;
	target(left_arrow - size * 1.2f, y, left_arrow + size * 0.3f, y + height, GUIDE_WORKSHOP_TARGET_PAGE,
		(short)(row * 2));
	target(right - size * 0.4f, y, x + width, y + height, GUIDE_WORKSHOP_TARGET_PAGE, (short)(row * 2 + 1));
}

static void upper(char const *text, char *out, int size)
{
	int index;

	for (index = 0; text[index] && index < size - 1; index++)
		out[index] = (char)(text[index] >= 'a' && text[index] <= 'z' ? text[index] - 32 : text[index]);
	out[index] = 0;
}

static void control_render(short row, float left, float y, boolean on,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index))
{
	char value[80], shown[GUIDE_LAYOUT_LINE_SIZE];

	value[0] = 0;
	switch (workshop_view.row_kind[row])
	{
	case _row_retry:
	{
		int busy = workshop_browser_busy_seconds();

		if (busy)
			snprintf(value, sizeof(value), "%ds", busy);
		ui_style_setting_plain();
		ui_style_setting(left, y, LIST_WIDTH, CONTROL_HEIGHT, 0.9f, "RETRY", NULL, value, on);
		return;
	}
	case _row_steam:
		ui_style_setting_plain();
		ui_style_setting(left, y, LIST_WIDTH, CONTROL_HEIGHT, 0.9f, "STEAM", NULL, "NOT LINKED", on);
		return;
	case _row_search:
		if (workshop_view.searching)
		{
			boolean blink = (system_milliseconds() / 500) % 2 == 0;
			char line[80];

			snprintf(line, sizeof(line), "%s%s", workshop_view.search, blink ? "_" : " ");
			ui_style_row(left, y, LIST_WIDTH, CONTROL_HEIGHT, TRUE);
			ui_overlay_outline(left + 1, y + 1, LIST_WIDTH - 2, CONTROL_HEIGHT - 2, 6, 1.0f, COLOR_VALUE);
			guide_layout_fit(line, LIST_WIDTH - 24, 10.0f, measure_font, (void *)(intptr_t)UI_FONT_MENU, 1, shown,
				sizeof(shown));
			ui_overlay_text(UI_FONT_MENU, 10.0f, left + 10, y + (CONTROL_HEIGHT - 10) * 0.5f, UI_ALIGN_LEFT,
				COLOR_VALUE, shown);
			return;
		}
		ui_style_setting_plain();
		ui_style_setting(left, y, LIST_WIDTH, CONTROL_HEIGHT, 0.9f, "SEARCH", NULL, workshop_browser_search(), on);
		return;
	case _row_show:
		upper(workshop_browser_filter_name(workshop_browser_filter()), value, sizeof(value));
		ui_style_setting(left, y, LIST_WIDTH, CONTROL_HEIGHT, 0.9f, "SHOW", NULL, value, on);
		row_arrows(row, left, y, LIST_WIDTH, CONTROL_HEIGHT, value, target);
		return;
	case _row_sort:
		upper(workshop_browser_sort_name(workshop_browser_sort()), value, sizeof(value));
		ui_style_setting(left, y, LIST_WIDTH, CONTROL_HEIGHT, 0.9f, "SORT", NULL, value, on);
		row_arrows(row, left, y, LIST_WIDTH, CONTROL_HEIGHT, value, target);
		return;
	case _row_page:
		snprintf(value, sizeof(value), "%d", workshop_browser_page());
		ui_style_setting(left, y, LIST_WIDTH, CONTROL_HEIGHT, 0.9f, "PAGE", NULL, value, on);
		row_arrows(row, left, y, LIST_WIDTH, CONTROL_HEIGHT, value, target);
		return;
	default:
		return;
	}
}

static void item_row_render(short row, float left, float y, boolean on)
{
	struct workshop_browser_item const *item = row_item(row);
	short view = workshop_view.row_view[row];
	char line[160], shown[GUIDE_LAYOUT_LINE_SIZE];
	char const *state;
	float state_width;

	if (!item)
		return;
	state = item_word(item, view);
	state_width = state[0] ? ui_overlay_text_width(UI_FONT_MENU, 8.0f, state) + 8 : 0;
	ui_style_row(left, y, LIST_WIDTH, ROW_HEIGHT, on);
	if (on)
		text_line_focused(UI_TEXT_NAME, left + 10, y + 3, LIST_WIDTH - 20, UI_ALIGN_LEFT, shown_title(item), 1 + row);
	else
		text_line(UI_TEXT_NAME, left + 10, y + 3, LIST_WIDTH - 20, UI_ALIGN_LEFT, shown_title(item));
	/* (an author's name of letters the font lacks: left out, as a title's) */
	ui_overlay_text_shown(UI_FONT_MENU, item->author, shown, sizeof(shown));
	if (item->setup)
		snprintf(line, sizeof(line), "%s", "Setup");
	else if (shown[0])
		snprintf(line, sizeof(line), "%s - %s", workshop_browser_kind_name(item->kind), shown);
	else
		snprintf(line, sizeof(line), "%s", workshop_browser_kind_name(item->kind));
	text_line(UI_TEXT_STATUS, left + 10, y + 17, LIST_WIDTH - 20 - state_width, UI_ALIGN_LEFT, line);
	ui_overlay_text(UI_FONT_MENU, 8.0f, left + LIST_WIDTH - 10, y + 17, UI_ALIGN_RIGHT, item_color(item, view), state);
}

static void list_render(float left, float top, float bottom,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index))
{
	short status = (short)workshop_browser_status(), row, shown = 0, items = 0, last;
	float y = top + HEADING_HEIGHT, room = bottom - y, used;

	heading_render(left, top, status);
	for (row = 0; row < workshop_view.row_count; row++)
		items += workshop_view.row_kind[row] == _row_item;
	/* (the rows from first that fit, the focus kept in them unless the
	wheel moved the list) */
	if (workshop_view.follow)
	{
		if (workshop_view.row < workshop_view.first)
			workshop_view.first = workshop_view.row;
		for (;;)
		{
			used = 0;
			for (last = workshop_view.first; last <= workshop_view.row && last < workshop_view.row_count; last++)
				used += row_height(last) + ROW_GAP;
			if (used - ROW_GAP <= room || workshop_view.first >= workshop_view.row)
				break;
			workshop_view.first++;
		}
		workshop_view.follow = FALSE;
	}
	workshop_view.first = (short)PIN(workshop_view.first, 0, MAX(workshop_view.row_count - 1, 0));
	workshop_view.list_x0 = left;
	workshop_view.list_y0 = y;
	workshop_view.list_x1 = left + LIST_WIDTH + 8;
	workshop_view.list_y1 = bottom;
	for (row = workshop_view.first; row < workshop_view.row_count && y + row_height(row) <= bottom + 0.5f; row++)
	{
		boolean on = row == workshop_view.row && !workshop_view.page;

		if (workshop_view.row_kind[row] == _row_item)
			item_row_render(row, left, y, on);
		else
			control_render(row, left, y, on, target);
		if (target)
			target(left, y, left + LIST_WIDTH, y + row_height(row), GUIDE_WORKSHOP_TARGET_ROW, row);
		y += row_height(row) + ROW_GAP;
		shown++;
		/* (after SORT, with no items: why the list is empty) */
		if (workshop_view.row_kind[row] == _row_sort && !items && y + CONTROL_HEIGHT <= bottom)
		{
			char line[96];

			if (status == WORKSHOP_LIST_LOADING || status == WORKSHOP_LIST_NOT_SET_UP)
				snprintf(line, sizeof(line), "Loading");
			else if (status == WORKSHOP_LIST_FAILED)
				line[0] = 0;
			else if (workshop_browser_search()[0])
				snprintf(line, sizeof(line), "No items for '%s'", workshop_browser_search());
			else
				snprintf(line, sizeof(line), "No items");
			if (line[0])
			{
				text_line(UI_TEXT_LINE, left + 10, y + 6, LIST_WIDTH - 20, UI_ALIGN_LEFT, line);
				y += CONTROL_HEIGHT + ROW_GAP;
			}
		}
	}
	workshop_view.shown_rows = (short)MAX(shown, 1);
	/* (more rows than show: a slim bar beside them) */
	if (workshop_view.first > 0 || row < workshop_view.row_count)
	{
		struct guide_rect track, thumb;

		track.x = left + LIST_WIDTH + 4;
		track.y = top + HEADING_HEIGHT;
		track.width = LIST_BAR;
		track.height = bottom - track.y;
		if (guide_layout_scroll_thumb(track, workshop_view.row_count, workshop_view.shown_rows, workshop_view.first,
			10.0f, &thumb))
		{
			ui_overlay_rect(track.x, track.y, track.width, track.height, 1.5f, 0xFFFFFF1A);
			ui_overlay_rect(thumb.x, thumb.y, thumb.width, thumb.height, 1.5f, 0xFFFFFF80);
		}
	}
}

static void page_render(short row, float x, float top, float width, float bottom,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index))
{
	struct workshop_browser_item const *item = row_item(row);
	short view = workshop_view.row_view[row];
	struct guide_rect pane = { x, top, width, bottom - top };
	struct guide_page_layout layout;
	short options[MAXIMUM_OPTIONS], option_count, index;
	char note_text[96], line[160], size[24], shown[GUIDE_LAYOUT_LINE_SIZE];
	float bar, state_width;
	char const *note, *state;
	boolean live = row == workshop_view.row;

	if (!item)
		return;
	option_count = (short)MIN(options_of(item, view, options), GUIDE_PAGE_MAXIMUM_BUTTONS);
	note = page_note(item, view, note_text, sizeof(note_text), &bar);
	state = item_word(item, view);
	guide_layout_page(pane, note != NULL, option_count, DESCRIPTION_LINE, &layout);
	if (workshop_view.page && live)
		text_line_focused(UI_TEXT_TITLE, layout.title.x, layout.title.y, layout.title.width, UI_ALIGN_LEFT,
			shown_title(item), -1 - row);
	else
		text_line(UI_TEXT_TITLE, layout.title.x, layout.title.y, layout.title.width, UI_ALIGN_LEFT, shown_title(item));
	/* (its kind and size, its author, and where it stands) */
	workshop_browser_size_text(item->size, size, sizeof(size));
	{
		char author[WORKSHOP_BROWSER_AUTHOR_SIZE];

		ui_overlay_text_shown(UI_FONT_MENU, item->author, author, sizeof(author));
		snprintf(line, sizeof(line), "%s%s%s%s%s", item->setup ? "Setup" : workshop_browser_kind_name(item->kind),
			size[0] ? " - " : "", size, author[0] ? " - by " : "", author);
	}
	state_width = state[0] ? ui_overlay_text_width(UI_FONT_MENU, 9.0f, state) + 8 : 0;
	text_line(UI_TEXT_LINE, layout.status.x, layout.status.y, layout.status.width - state_width, UI_ALIGN_LEFT, line);
	ui_overlay_text(UI_FONT_MENU, 9.0f, layout.status.x + layout.status.width, layout.status.y + 1, UI_ALIGN_RIGHT,
		item_color(item, view), state);
	/* (its picture in its box, the item's own or Steam's; until it comes, or
	when there is none, the dark box alone) */
	if (item->setup)
	{
		float room = layout.description.y + layout.description.height - layout.picture.y;

		layout.description.y = layout.picture.y;
		layout.description_lines = (int)(room / DESCRIPTION_LINE);
	}
	else if (layout.picture.height > 0)
	{
		int picture_width, picture_height, serial;
		unsigned char const *picture = workshop_browser_preview(view, &picture_width, &picture_height, &serial);
		struct guide_rect box = layout.picture;

		ui_overlay_rect(box.x, box.y, box.width, box.height, 4, 0x0000004D);
		if (picture && picture_width > 0 && picture_height > 0)
		{
			float draw_width = box.width, draw_height = draw_width * (float)picture_height / (float)picture_width;

			if (draw_height > box.height)
			{
				draw_height = box.height;
				draw_width = draw_height * (float)picture_width / (float)picture_height;
			}
			ui_overlay_picture(serial, picture, picture_width, picture_height, box.x + (box.width - draw_width) * 0.5f,
				box.y + (box.height - draw_height) * 0.5f, draw_width, draw_height, 0xFFFFFFFF);
		}
	}
	if (layout.description_lines > 0)
	{
		if (live)
			description_render(item, layout.description, (short)layout.description_lines);
		else
		{
			/* (the last item's page, while the focus is on SEARCH and such:
			shown, not taking the wheel) */
			char wrapped[24][GUIDE_LAYOUT_LINE_SIZE];
			int count = guide_layout_wrap(page_text(item), layout.description.width - DESCRIPTION_BAR_ROOM,
				DESCRIPTION_SIZE, measure_font, (void *)(intptr_t)UI_FONT_MENU, MIN(layout.description_lines, 24),
				wrapped, NULL);
			float y = layout.description.y;

			for (index = 0; index < count; index++, y += DESCRIPTION_LINE)
				ui_overlay_text(UI_FONT_MENU, DESCRIPTION_SIZE, layout.description.x, y, UI_ALIGN_LEFT,
					COLOR_DESCRIPTION, wrapped[index]);
			workshop_view.description_x1 = workshop_view.description_x0;
		}
	}
	if (note)
	{
		text_line(UI_TEXT_LABEL, layout.note.x, layout.note.y - (bar >= 0 ? 1 : 0), layout.note.width, UI_ALIGN_LEFT,
			note);
		if (bar >= 0)
		{
			ui_overlay_rect(layout.note.x, layout.note.y + layout.note.height - 2, layout.note.width, 3, 1.5f,
				0xFFFFFF26);
			ui_overlay_rect(layout.note.x, layout.note.y + layout.note.height - 2, layout.note.width * bar, 3, 1.5f,
				COLOR_WAITING);
		}
	}
	for (index = 0; index < layout.button_count; index++)
	{
		struct guide_rect button = layout.buttons[index];
		boolean on = workshop_view.page && live && index == workshop_view.option && !workshop_view.description_on;

		/* (STEAM'S MCC MAPS: the row is the setting, drawn as SETTINGS'
		rows are, its value between their arrows) */
		if (options[index] == _option_steam_maps)
			ui_style_setting(button.x, button.y, button.width, button.height, 0.9f, option_label(view, options[index]),
				NULL, steam_maps_on() ? "ON" : "OFF", on);
		else
		{
			ui_style_row(button.x, button.y, button.width, button.height, on);
			text_fit(UI_TEXT_NAME, option_label(view, options[index]), button.width - 20, shown, sizeof(shown));
			ui_overlay_text(UI_FONT_MENU, 10.0f, button.x + 10, button.y + (button.height - 10) * 0.5f, UI_ALIGN_LEFT,
				COLOR_VALUE, shown);
		}
		if (target)
			target(button.x, button.y, button.x + button.width, button.y + button.height, GUIDE_WORKSHOP_TARGET_OPTION,
				index);
	}
}

void guide_workshop_render(float left, float top, float right, float bottom,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index))
{
	float x = left + LIST_WIDTH + 12, width = right - x;
	short shown_page;

	rows_sync();
	workshop_view.description_x0 = workshop_view.description_x1 = 0;
	list_render(left, top, bottom, target);
	/* the chosen item's page (the last one chosen while the focus is above
	the items) */
	ui_style_glass(x, top, width, bottom - top, 8, ui_style_color(UI_COLOR_PANE), ui_style_color(UI_COLOR_ROW_EDGE));
	shown_page = page_row();
	if (shown_page != NONE)
		page_render(shown_page, x, top, width, bottom, target);
}

#endif
#endif /* HALO_MCC */
