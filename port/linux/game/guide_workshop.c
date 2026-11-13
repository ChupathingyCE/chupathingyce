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
  own Workshop folder. With game.workshop_maps off, the page's one row says
  so (WORKSHOP MAPS  OFF), and pressing it turns them on, so the item's
  maps join the map lists.
- While the section shows, or an item waits on Steam, the item folders are
  looked at every second: an item that becomes installed is rescanned into
  the map lists (mcc_maps_rescan) and gets a toast.

- The game's own Steam download (steam_client_bridge.h's link) installs
  items on every computer: GET without the link opens Steam's sign-in
  window right here (its QR for the Steam app), and the item downloads
  once it's scanned; linked, GET downloads in the game with its progress
  (Installing 45%), never opening Steam. Only when it can't get an item
  ("Not owned") does OPEN IN STEAM show, beside TRY AGAIN.
- An item's page has one main button for where it stands (WORKSHOP MAPS
  while they are off, PLAY, LINK STEAM, GET) and at most one more. A
  question (REMOVE's, CONNECT STEAM's, UNLINK's) has its yes and CANCEL,
  the focus on CANCEL: B, CANCEL or a click beside UNLINK's window leaves
  things as they were.
  Where the game's own download isn't built, CONNECT STEAM (after a note)
  turns on the Steam client's bridge instead.
- Steam's list comes a page at a time: LT and RT (Page Up, Page Down), the
  heading's PREV and NEXT, or the list's last row.

The section shows only on a computer with MCC from Steam, or steamcmd.
No Steamworks SDK, no Steam password, nothing written to Steam's own
folders but what Steam or steamcmd write.

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

enum
{
	LIST_WIDTH = 262,
	ROW_HEIGHT = 30,
	ROW_GAP = 4,
	OPTION_HEIGHT = 24,
	OPTION_GAP = 4,
	HEADING_HEIGHT = 16,
	DESCRIPTION_SIZE = 9,
	DESCRIPTION_LINE = DESCRIPTION_SIZE + 4,
	/* the list's heading: PREVIOUS and NEXT, for the pointer */
	PAGE_BUTTON_WIDTH = 46,
	/* Steam's sign-in: "Linked" shown this long before its window closes,
	and an expired code made again at most this many times */
	LINK_DONE_TIME = 1500,
	LINK_REFRESHES = 3,
};

/* the theme's (ui_style_color), and white the same in all */
#define COLOR_INSTALLED ui_style_color(UI_COLOR_GOOD)
#define COLOR_WAITING ui_style_color(UI_COLOR_WARNING)
#define COLOR_NOT_INSTALLED ui_style_color(UI_COLOR_STATUS)
#define COLOR_DESCRIPTION ui_style_color(UI_COLOR_TEXT_PALE)
enum
{
	COLOR_VALUE = 0xFFFFFFFF,
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
	_option_search,
	_option_play,
	_option_update,
	_option_remove,
	/* the content client couldn't get it ("Not owned"): the item's page in Steam */
	_option_open_steam,
	/* the list failed: asked again */
	_option_retry,
	/* a question's no (REMOVE's, CONNECT STEAM's): nothing done */
	_option_cancel,
	/* the link row's LINK (to SETTINGS' STEAM ACCOUNT, its QR shown) */
	_option_link,
	/* the page row's */
	_option_page_next,
	_option_page_previous,
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
	/* REMOVE pressed once: DELETE deletes the files */
	boolean confirm_remove;
	/* where the button that asked stood: CANCEL takes its place, so a
	second press or click there answers no */
	short confirm_slot;
	/* UNLINK's question: the button with the focus (1: CANCEL, as it opens),
	and its window, for a click beside it */
	short modal_option;
	float modal_x0, modal_y0, modal_x1, modal_y1;
	/* LINK's QR: Steam's text for it, and its modules (made again when the text changes) */
	char link_text[256];
	short link_qr_size;
	unsigned char link_qr[BROWSER_CONNECT_QR_SIZE * BROWSER_CONNECT_QR_SIZE];
	/* Steam's sign-in window: up (it stays for a failure or a moment after
	the scan), when it saw the scan through, whether a code was shown (a
	failure after it: expired, made again), how often, and the item whose
	GET asked for it (fetched once linked) */
	boolean link_modal;
	unsigned long link_done_time;
	boolean link_shown_code, link_scanned, link_refreshing;
	short link_refreshes;
	char get_after_link[WORKSHOP_BROWSER_ID_SIZE];
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
	focus on it (the d-pad, the stick and the wheel scroll it), the first
	line shown, how many lines it has and shows (as last drawn), its box
	(for the wheel and a click) and whose it is */
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

static struct workshop_browser_item const *chosen(void)
{
	return workshop_browser_item(workshop_view.row);
}

static boolean link_needed(void);
/* (the item page's description: whether it scrolls, and a scroll) */
static boolean description_scrolls(void);
static void description_scroll(short step);

static short options_of(struct workshop_browser_item const *item, short *options)
{
	short count = 0;

	boolean bridge = workshop_browser_bridge_status() == STEAM_BRIDGE_READY;

	if (!item)
		return 0;
	if (item->setup == WORKSHOP_ROW_LINK)
	{
		options[count++] = _option_link;
		return count;
	}
	if (item->setup == WORKSHOP_ROW_PAGE)
	{
		options[count++] = _option_page_next;
		if (workshop_browser_page() > 1)
			options[count++] = _option_page_previous;
		return count;
	}
	if (item->setup == WORKSHOP_ROW_FIND)
	{
		short index;

		if (workshop_browser_status() == WORKSHOP_LIST_FAILED)
			options[count++] = _option_retry;
		options[count++] = _option_search;
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
	/* (one main action for where the item stands, then at most one more:
	Workshop maps off, the row that says so and turns them on; installed,
	PLAY; not linked, LINK STEAM; else GET) */
	if (!config_boolean("game.workshop_maps"))
	{
		options[count++] = _option_show_maps;
		return count;
	}
	/* (a question asked: its yes and CANCEL, CANCEL where the button that
	asked was) */
	if ((workshop_view.confirm_remove && workshop_browser_removable(workshop_view.row)) || workshop_view.confirm)
	{
		short yes = workshop_view.confirm_remove ? _option_remove : _option_use_steam;

		options[count++] = workshop_view.confirm_slot ? yes : _option_cancel;
		options[count++] = workshop_view.confirm_slot ? _option_cancel : yes;
		return count;
	}
	if (item->state == WORKSHOP_ITEM_INSTALLED)
	{
		/* (in a game or a lobby too: PLAY asks before leaving it) */
		options[count++] = _option_play;
		if ((bridge || (workshop_browser_link_offered() && !link_needed())) &&
			workshop_browser_update_offered(workshop_view.row))
		{
			options[count++] = _option_update;
		}
		if (count < 2 && workshop_browser_removable(workshop_view.row))
			options[count++] = _option_remove;
		return count;
	}
	if (workshop_browser_link_offered())
	{
		/* (the game's own download: LINK STEAM, then GET; its progress shows
		while it works; Steam's page only when it can't get the item) */
		if (link_needed())
			options[count++] = _option_steam;
		else if (item->steam_error[0])
		{
			options[count++] = _option_steam;
			options[count++] = _option_open_steam;
		}
		else if (item->state == WORKSHOP_ITEM_NOT_INSTALLED)
			options[count++] = _option_steam;
		else if (bridge && item->steam_subscribed && steam_bridge_can_unsubscribe())
			options[count++] = _option_unsubscribe;
		return count;
	}
	/* (no download of the game's own: the Steam client's, or steamcmd's) */
	if (bridge && steam_bridge_sign_in_needed())
		options[count++] = _option_sign_in;
	else if (!(bridge && item->steam_subscribed))
		options[count++] = _option_steam;
	else if (steam_bridge_can_unsubscribe())
		options[count++] = _option_unsubscribe;
	if (count < 2 && !steam_bridge_enabled() && workshop_browser_bridge_backend() != STEAM_BRIDGE_BACKEND_NONE)
		options[count++] = _option_use_steam;
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
	case _option_search: return "SEARCH";
	case _option_play: return "PLAY";
	case _option_update: return link_needed() ? "LINK STEAM" : item->steam_error[0] ? "RETRY" : "UPDATE";
	case _option_remove: return workshop_view.confirm_remove ? "DELETE" : "REMOVE";
	case _option_show_maps: return "WORKSHOP MAPS";
	case _option_cancel: return "CANCEL";
	case _option_unsubscribe: return "CANCEL";
	case _option_open_steam: return "OPEN IN STEAM";
	case _option_retry: return "RETRY";
	case _option_sign_in: return steam_link_available() ? "LINK STEAM" : "SIGN IN";
	case _option_link: return "LINK";
	case _option_page_next: return "NEXT PAGE";
	case _option_page_previous: return "PREVIOUS";
	case _option_use_steam: return workshop_view.confirm ? "CONNECT" : "CONNECT STEAM";
	default: break;
	}
	if (item->setup)
		return workshop_browser_setup_button(workshop_view.row);
	/* (installed, or on its way: its page in Steam; GET needs the Steam link) */
	if (item->state == WORKSHOP_ITEM_NOT_INSTALLED && link_needed())
		return "LINK STEAM";
	if (item->steam_error[0])
		return "TRY AGAIN";
	return item->state == WORKSHOP_ITEM_NOT_INSTALLED ? "GET" : "OPEN";
}

/* the note CONNECT STEAM shows before CONFIRM (one line) */
static char const *use_steam_note(void)
{
	return workshop_browser_bridge_backend() == STEAM_BRIDGE_BACKEND_STEAMCMD ?
		"Downloads use steamcmd's own sign-in" : "Steam shows MCC as running while open";
}

int ui_overlay_keyboard(void);

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
			sizeof(workshop_view.keyboard_text), L"Search");
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
		workshop_view.row = 0;
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

/* the built-in Steam download is the installer here, and Steam isn't linked:
GET and UPDATE need it (playing what is installed never does) */
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
	if (link != STEAM_LINK_STARTING && link != STEAM_LINK_WAITING && link != STEAM_LINK_SCANNED)
		steam_link_start();
}

static void link_go(void)
{
	struct workshop_browser_item const *item = chosen();

	link_begin(item && !item->setup && item->state == WORKSHOP_ITEM_NOT_INSTALLED ? item->id : NULL);
}

/* the find row's index (the new page's first item is under it) */
static short find_row(void)
{
	short index;

	for (index = 0; index < (short)workshop_browser_count(); index++)
	{
		struct workshop_browser_item const *row = workshop_browser_item(index);

		if (row && row->setup == WORKSHOP_ROW_FIND)
			return index;
	}
	return 0;
}

boolean guide_workshop_page_step(short step)
{
	/* (step 0: the heading's RETRY) */
	if (!step)
		return workshop_browser_retry() != 0;
	if (workshop_view.searching || !(step > 0 ? workshop_browser_page_next() : workshop_browser_page_previous()))
		return FALSE;
	/* (the new page's first item chosen, the list from its top) */
	workshop_view.first = find_row();
	workshop_view.row = (short)(workshop_view.first + 1);
	workshop_view.page = FALSE;
	workshop_view.option = 0;
	workshop_view.confirm = workshop_view.confirm_remove = FALSE;
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

/* ---------- PLAY

To the menus' own Create Game or level select, in the menu set chosen, a
game or lobby left first if the player says so (menu_functions.c). A
campaign item's levels are listed whatever game.mcc_campaigns says (it
only adds the missions' order: mcc_maps.c). */

static void play(struct workshop_browser_item const *item)
{
	if (item && !item->setup && item->state == WORKSHOP_ITEM_INSTALLED &&
		pc_menu_workshop_play(item->id, item->title))
	{
		guide_close();
	}
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
	/* (the list, as the section showing asks for it) */
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

/* a question on the item's page put away, unanswered (CANCEL, B, another
item or the page left) */
static void confirms_clear(void)
{
	if (workshop_view.confirm || workshop_view.confirm_remove)
		workshop_view.option = 0;
	workshop_view.confirm = workshop_view.confirm_remove = FALSE;
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
	case _option_search:
		search_start();
		return;
	case _option_play:
		play(workshop_browser_item(workshop_view.row));
		return;
	case _option_update:
		if (link_needed())
		{
			link_go();
			return;
		}
		if (workshop_browser_update(workshop_view.row))
			workshop_view.waiting = TRUE;
		return;
	case _option_remove:
		/* (asked first: DELETE deletes the files, the focus on CANCEL, in
		REMOVE's place) */
		if (!workshop_view.confirm_remove)
		{
			workshop_view.confirm_remove = TRUE;
			workshop_view.confirm_slot = workshop_view.option ? 1 : 0;
			workshop_view.option = workshop_view.confirm_slot;
			return;
		}
		workshop_view.confirm_remove = FALSE;
		workshop_view.option = 0;
		workshop_browser_remove(workshop_view.row);
		return;
	case _option_show_maps:
		/* (the row is the setting: pressed, Workshop maps are on, the
		player's own choice) */
		config_write_boolean("game.workshop_maps", 1);
		mcc_maps_rescan();
		workshop_view.option = 0;
		return;
	case _option_cancel:
		confirms_clear();
		return;
	case _option_unsubscribe:
		workshop_browser_unsubscribe(workshop_view.row);
		return;
	case _option_sign_in:
		/* (the built-in download: Steam's sign-in window) */
		if (workshop_browser_link_offered())
		{
			link_begin(NULL);
			return;
		}
		steam_bridge_sign_in();
		return;
	case _option_link:
		link_begin(NULL);
		return;
	case _option_open_steam:
		workshop_browser_open_in_steam(workshop_view.row);
		return;
	case _option_retry:
		workshop_browser_retry();
		workshop_view.option = 0;
		return;
	case _option_page_next:
	case _option_page_previous:
		guide_workshop_page_step(option == _option_page_next ? 1 : -1);
		return;
	case _option_use_steam:
		/* (asked first: CONNECT turns it on, the focus on CANCEL, in CONNECT
		STEAM's place) */
		if (!workshop_view.confirm)
		{
			workshop_view.confirm = TRUE;
			workshop_view.confirm_slot = workshop_view.option ? 1 : 0;
			workshop_view.option = workshop_view.confirm_slot;
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
	/* (GET without the Steam link: to Settings' STEAM ACCOUNT, its QR up) */
	if (!chosen()->setup && chosen()->state == WORKSHOP_ITEM_NOT_INSTALLED && link_needed())
	{
		link_go();
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
	case WORKSHOP_ITEM_DOWNLOADING:
	case WORKSHOP_ITEM_INSTALLING: return COLOR_WAITING;
	default: return COLOR_NOT_INSTALLED;
	}
}

/* a row's kind: the find row's filter and sort as they are */
static char const *kind_text(struct workshop_browser_item const *item)
{
	return item->setup == WORKSHOP_ROW_FIND || item->setup == WORKSHOP_ROW_LINK || item->setup == WORKSHOP_ROW_PAGE ?
		item->kind :
		workshop_browser_kind_name(item->kind);
}

/* a row's word and color: a setup row's step, an item's state */
static char const *item_word(struct workshop_browser_item const *item)
{
	if (item->setup == WORKSHOP_ROW_FIND)
		return "";
	if (item->setup == WORKSHOP_ROW_LINK)
		return "To do";
	if (item->setup == WORKSHOP_ROW_PAGE)
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

/* an item's title as drawn: the menus' fonts have no Chinese, Japanese or
Korean letters, which are left out rather than drawn as boxes, with the
spaces around them (ui_overlay_text_shown); a title of only those is the
item's number */
static char const *shown_title(struct workshop_browser_item const *item)
{
	static char shown[sizeof(item->title)];

	if (item->setup)
		return item->title;
	if (!ui_overlay_text_shown(UI_FONT_MENU, item->title, shown, sizeof(shown)))
		snprintf(shown, sizeof(shown), "Workshop item %s", item->id);
	return shown;
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

	/* (from its start each time it takes the focus: another line focused
	since, or none drawn focused a moment, the Guide closed or the focus
	gone elsewhere) */
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

/* text wrapped into width, at most lines lines, from y; the last line's bottom */
static float text_wrapped(char const *text, float x, float y, float width, short lines)
{
	char wrapped[16][GUIDE_LAYOUT_LINE_SIZE];
	int count, index;

	count = guide_layout_wrap(text, width, DESCRIPTION_SIZE, measure_font, (void *)(intptr_t)UI_FONT_MENU,
		MIN(lines, (short)NUMBEROF(wrapped)), wrapped, NULL);
	for (index = 0; index < count; index++)
	{
		ui_overlay_text(UI_FONT_MENU, DESCRIPTION_SIZE, x, y, UI_ALIGN_LEFT, COLOR_DESCRIPTION, wrapped[index]);
		y += DESCRIPTION_LINE;
	}
	return y;
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

static boolean link_busy(int link)
{
	return link == STEAM_LINK_STARTING || link == STEAM_LINK_WAITING || link == STEAM_LINK_SCANNED;
}

/* UNLINK's question: STEAM ACCOUNT pressed once while linked */
static boolean unlink_asked(void)
{
	return steam_link_armed() && steam_link_linked(NULL, 0);
}

/* SETTINGS' STEAM ACCOUNT pressed: LINK (the sign-in window), UNLINK's
question when linked */
static void link_settings_press(void)
{
	if (steam_link_linked(NULL, 0))
	{
		/* (UNLINK's question, the focus on CANCEL: a press by mistake
		undoes nothing) */
		steam_link_set_armed(1);
		workshop_view.modal_option = 1;
		return;
	}
	link_begin(NULL);
}

/* the window's steps, each frame: a code that expired made again (a few
times), the item asked for once linked, the window closed a moment after */
static void link_follow(void)
{
	int link = steam_link_poll(NULL, 0, NULL, 0);
	unsigned long now = system_milliseconds();

	workshop_browser_link_follow();
	if (link_busy(link))
		workshop_view.link_modal = TRUE;
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
				short index;

				for (index = 0; index < (short)workshop_browser_count(); index++)
				{
					struct workshop_browser_item const *item = workshop_browser_item(index);

					if (item && !item->setup && !strcmp(item->id, workshop_view.get_after_link) &&
						workshop_browser_subscribe(index))
					{
						workshop_view.waiting = TRUE;
						break;
					}
				}
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

boolean guide_workshop_link_modal(void)
{
	return unlink_asked() || workshop_view.link_modal || link_busy(steam_link_poll(NULL, 0, NULL, 0));
}

/* the window's one button: CANCEL while it waits, BACK once failed, UNLINK
for the question */
static void link_modal_close(void)
{
	workshop_view.link_modal = FALSE;
	workshop_view.link_done_time = 0;
	workshop_view.get_after_link[0] = 0;
}

void guide_workshop_link_modal_press(void)
{
	if (unlink_asked())
	{
		steam_link_set_armed(0);
		/* (CANCEL: linked as it was) */
		if (workshop_view.modal_option)
			return;
		steam_link_unlink();
		return;
	}
	if (link_busy(steam_link_poll(NULL, 0, NULL, 0)))
		steam_link_cancel();
	link_modal_close();
}

void guide_workshop_link_modal_back(void)
{
	if (unlink_asked())
	{
		steam_link_set_armed(0);
		return;
	}
	if (link_busy(steam_link_poll(NULL, 0, NULL, 0)))
		steam_link_cancel();
	link_modal_close();
}

void guide_workshop_link_modal_move(short step)
{
	/* (the question's two buttons: UNLINK left, CANCEL right) */
	if (unlink_asked())
		workshop_view.modal_option = (short)(step < 0 ? 0 : 1);
}

void guide_workshop_link_modal_click(short index)
{
	if (unlink_asked())
		workshop_view.modal_option = index ? 1 : 0;
	guide_workshop_link_modal_press();
}

void guide_workshop_link_modal_click_outside(short x, short y)
{
	/* (UNLINK's question: a click beside its window is CANCEL; Steam's
	sign-in keeps waiting for the scan, only its button closes it) */
	if (unlink_asked() && (x < workshop_view.modal_x0 || x >= workshop_view.modal_x1 || y < workshop_view.modal_y0 ||
		y >= workshop_view.modal_y1))
	{
		steam_link_set_armed(0);
	}
}

char const *guide_workshop_link_note(void)
{
	/* (linked, with no keyring or Keychain to take the sign-in: said) */
	if (steam_link_linked(NULL, 0) && !steam_link_store_secure())
		return "Your Steam sign-in is kept in a private file, not encrypted";
	return NULL;
}

void guide_workshop_link_modal_render(float left, float top, float right, float bottom,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index))
{
	char text[256], error[24], status[48];
	int link = steam_link_poll(text, sizeof(text), error, sizeof(error));
	boolean question = unlink_asked();
	struct guide_rect area = { left, top, right - left, bottom - top };
	struct guide_modal_layout layout;
	unsigned int status_color = ui_style_color(UI_COLOR_LABEL);
	char const *button;
	float center;

	guide_layout_modal(area, !question, &layout);
	center = layout.window.x + layout.window.width * 0.5f;
	/* (the Guide under it dimmed past reading, the window solid) */
	ui_overlay_rect(-200, 0, 1040, 480, 0, 0x000000E6);
	ui_overlay_rect(layout.window.x, layout.window.y, layout.window.width, layout.window.height, 10,
		ui_style_color(UI_COLOR_WINDOW));
	ui_style_panel(layout.window.x, layout.window.y, layout.window.width, layout.window.height, 1.0f);
	if (question)
	{
		char lines[2][GUIDE_LAYOUT_LINE_SIZE];
		int count, index;

		text_line(UI_TEXT_TITLE, center, layout.title.y, layout.title.width, UI_ALIGN_CENTER, "UNLINK STEAM?");
		count = guide_layout_wrap("Workshop downloads will need you to scan again.", layout.line.width,
			ui_style_text_size(UI_TEXT_LINE), measure_font, (void *)(intptr_t)ui_style_text_font(UI_TEXT_LINE), 2,
			lines, NULL);
		for (index = 0; index < count; index++)
			ui_style_text(UI_TEXT_LINE, center, layout.line.y + index * 12, UI_ALIGN_CENTER, lines[index]);
		button = "UNLINK";
		status[0] = 0;
	}
	else
	{
		boolean done = link == STEAM_LINK_DONE || (!link_busy(link) && link != STEAM_LINK_FAILED &&
			steam_link_linked(NULL, 0));

		text_line(UI_TEXT_TITLE, center, layout.title.y, layout.title.width, UI_ALIGN_CENTER, "LINK STEAM");
		link_qr_draw(layout.qr, link == STEAM_LINK_WAITING || link == STEAM_LINK_SCANNED ? text : NULL);
		text_line(UI_TEXT_LINE, center, layout.line.y, layout.line.width, UI_ALIGN_CENTER, "Scan with the Steam app");
		if (done)
		{
			snprintf(status, sizeof(status), "Linked");
			status_color = COLOR_INSTALLED;
		}
		else if (workshop_view.link_refreshing && link != STEAM_LINK_WAITING)
			snprintf(status, sizeof(status), "Expired, refreshing");
		else if (link == STEAM_LINK_STARTING)
			snprintf(status, sizeof(status), "Getting a code");
		else if (link == STEAM_LINK_SCANNED)
			snprintf(status, sizeof(status), "Approve on your phone");
		else if (link == STEAM_LINK_WAITING)
			snprintf(status, sizeof(status), "Waiting");
		else if (link == STEAM_LINK_FAILED && workshop_view.link_shown_code && !workshop_view.link_scanned)
			snprintf(status, sizeof(status), "Expired");
		else
			snprintf(status, sizeof(status), "%s", error[0] ? error : "Not linked");
		button = link_busy(link) ? "CANCEL" : done ? "DONE" : "BACK";
	}
	if (status[0])
	{
		char shown[GUIDE_LAYOUT_LINE_SIZE];

		text_fit(UI_TEXT_LABEL, status, layout.status.width, shown, sizeof(shown));
		ui_overlay_text(ui_style_text_font(UI_TEXT_LABEL), ui_style_text_size(UI_TEXT_LABEL), center, layout.status.y,
			UI_ALIGN_CENTER, status_color, shown);
	}
	workshop_view.modal_x0 = layout.window.x;
	workshop_view.modal_y0 = layout.window.y;
	workshop_view.modal_x1 = layout.window.x + layout.window.width;
	workshop_view.modal_y1 = layout.window.y + layout.window.height;
	ui_style_row(layout.button.x, layout.button.y, layout.button.width, layout.button.height,
		!question || !workshop_view.modal_option);
	ui_overlay_text(UI_FONT_MENU, 10.0f, layout.button.x + layout.button.width * 0.5f,
		layout.button.y + (layout.button.height - 10) * 0.5f, UI_ALIGN_CENTER, COLOR_VALUE, button);
	if (target)
	{
		target(layout.button.x, layout.button.y, layout.button.x + layout.button.width,
			layout.button.y + layout.button.height, GUIDE_WORKSHOP_TARGET_MODAL, 0);
	}
	/* (the question's CANCEL) */
	if (question && layout.cancel.width > 0)
	{
		ui_style_row(layout.cancel.x, layout.cancel.y, layout.cancel.width, layout.cancel.height,
			workshop_view.modal_option != 0);
		ui_overlay_text(UI_FONT_MENU, 10.0f, layout.cancel.x + layout.cancel.width * 0.5f,
			layout.cancel.y + (layout.cancel.height - 10) * 0.5f, UI_ALIGN_CENTER, COLOR_VALUE, "CANCEL");
		if (target)
		{
			target(layout.cancel.x, layout.cancel.y, layout.cancel.x + layout.cancel.width,
				layout.cancel.y + layout.cancel.height, GUIDE_WORKSHOP_TARGET_MODAL, 1);
		}
	}
}

/* ---------- public code */

boolean guide_workshop_shown(void)
{
	return workshop_browser_offered() != 0;
}

void guide_workshop_open(void)
{
	static boolean debug_done;
	char const *debug_search = config_string("debug.workshop_search");
	workshop_view.page = FALSE;
	workshop_view.option = 0;
	workshop_view.confirm = FALSE;
	workshop_browser_open(0);
	/* (debug.workshop_search: the find row's text box with these words for a
	few seconds, then searched, for screenshots) */
	if (!debug_done && debug_search && debug_search[0])
	{
		debug_done = TRUE;
		search_start();
		snprintf(workshop_view.search, sizeof(workshop_view.search), "%s", debug_search);
		workshop_view.debug_search_time = system_milliseconds();
		workshop_view.page = TRUE;
		workshop_view.row = 0;
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
	settings_rows_set_workshop_offered(workshop_browser_offered());
	link_follow();
	/* (the on-screen keyboard done: its words searched, the Guide back on WORKSHOP) */
#if defined(HALO_CONSOLE_MAP_BROWSER) || defined(HALO_MAP_LIST)
	if (workshop_view.keyboard && !virtual_keyboard_active())
	{
		workshop_view.keyboard = FALSE;
		if (virtual_keyboard_last_exit_saved_text())
		{
			char words[64];
			short index;

			for (index = 0; workshop_view.keyboard_text[index] && index < (short)sizeof(words) - 1; index++)
				words[index] = workshop_view.keyboard_text[index] < 0x80 ? (char)workshop_view.keyboard_text[index] : '?';
			words[index] = 0;
			workshop_browser_set_search(words);
			workshop_view.row = 0;
		}
		workshop_view.page = FALSE;
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
	return (short)workshop_browser_count();
}

void guide_workshop_move(short step)
{
	if (workshop_view.searching)
		return;
	short options[MAXIMUM_OPTIONS];
	short count = (short)workshop_browser_count();

	if (workshop_view.page)
	{
		short option_count = options_of(chosen(), options);

		/* (the description: scrolled while it has the focus; it takes it
		past the first button up or the last down, when it scrolls) */
		if (workshop_view.description_on)
		{
			description_scroll(step);
			return;
		}
		if (description_scrolls() && ((step < 0 && workshop_view.option <= 0) ||
			(step > 0 && workshop_view.option >= option_count - 1)))
		{
			workshop_view.description_on = TRUE;
			return;
		}
		if (option_count > 0)
			workshop_view.option = (short)PIN(workshop_view.option + step, 0, option_count - 1);
		return;
	}
	if (count > 0)
		workshop_view.row = (short)PIN(workshop_view.row + step, 0, count - 1);
	confirms_clear();
}

void guide_workshop_activate(void)
{
	if (workshop_view.searching)
	{
		search_end(TRUE);
		return;
	}
	short options[MAXIMUM_OPTIONS];
	short count = options_of(chosen(), options);

	if (!count)
		return;
	if (!workshop_view.page)
	{
		workshop_view.page = TRUE;
		workshop_view.option = 0;
		workshop_view.description_on = FALSE;
		return;
	}
	/* (A on the description gives the buttons the focus again) */
	if (workshop_view.description_on)
	{
		workshop_view.description_on = FALSE;
		return;
	}
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
	workshop_view.page = FALSE;
	confirms_clear();
	return TRUE;
}

/* the wheel over the description: scrolled, the focus where it was (TRUE:
taken); a click on it gives it the focus */
static boolean over_description(short x, short y)
{
	return workshop_view.page && x >= workshop_view.description_x0 && x < workshop_view.description_x1 &&
		y >= workshop_view.description_y0 && y < workshop_view.description_y1;
}

boolean guide_workshop_wheel(short x, short y, short steps)
{
	if (!over_description(x, y) || !description_scrolls())
		return FALSE;
	description_scroll((short)-steps);
	return TRUE;
}

boolean guide_workshop_click(short x, short y)
{
	if (!over_description(x, y) || !description_scrolls())
		return FALSE;
	workshop_view.description_on = TRUE;
	return TRUE;
}

void guide_workshop_row_click(short row, boolean touch)
{
	boolean again = row == workshop_view.row && !workshop_view.page;

	/* (another item, or the list: a question put away) */
	if (row != workshop_view.row || workshop_view.page)
		confirms_clear();
	workshop_view.page = FALSE;
	workshop_view.row = row;
	if (again || touch)
		guide_workshop_activate();
}

void guide_workshop_option_click(short option)
{
	workshop_view.page = TRUE;
	workshop_view.option = option;
	/* (a click on a button presses it, the description's focus or not) */
	workshop_view.description_on = FALSE;
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
	/* (the whole item's first, then its maps'; a mixed item's listed maps
	counted, so it never reads as the whole item missing) */
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
the map catalog: mcc_maps\<id>\...; an item not installed lists none) */
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
		/* (the catalog's name, its plain letters) */
		for (character = 0; name && name[character] && length + 2 < sizeof(text); character++)
			text[length++] = name[character] < 0x80 ? (char)name[character] : '?';
		text[length] = 0;
	}
	return text;
}

/* the description's box: its lines from the first scrolled to, a bar at its
right when they don't all show, its edge lit while it has the focus */
#define DESCRIPTION_MAXIMUM_LINES 160
#define DESCRIPTION_BAR 3.0f

static void description_render(struct workshop_browser_item const *item, struct guide_rect box, short lines,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index))
{
	static char wrapped[DESCRIPTION_MAXIMUM_LINES][GUIDE_LAYOUT_LINE_SIZE];
	struct guide_rect track, thumb;
	int count, index;
	float y = box.y, width = box.width - DESCRIPTION_BAR - 4;

	(void)target;
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
	if (workshop_view.description_on)
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
			workshop_view.description_on ? COLOR_VALUE : 0xFFFFFF80);
	}
}

/* whether the description has more lines than show (it can be scrolled) */
static boolean description_scrolls(void)
{
	return workshop_view.page && workshop_view.description_count > workshop_view.description_shown;
}

static void description_scroll(short step)
{
	workshop_view.description_scroll = (short)guide_layout_scroll_clamp(workshop_view.description_scroll + step,
		workshop_view.description_count, workshop_view.description_shown);
}

/* the line over an item's buttons: what is happening, or why the buttons
are what they are (NULL: none); a download's share in *bar (else -1) */
static char const *page_note(struct workshop_browser_item const *item, char *line, int size, float *bar)
{
	*bar = -1;
	if (workshop_view.confirm_remove)
		return "Delete its files?";
	if (workshop_view.confirm)
		return use_steam_note();
	if (item->setup)
		return NULL;
	/* (no line that says what the one button does: "Sign in to steamcmd"
	over SIGN IN, "Workshop maps are off" over WORKSHOP MAPS OFF, "Link
	Steam to download in the game" over LINK STEAM) */
	if (item->steam_error[0])
		return item->steam_error;
	if (item->state == WORKSHOP_ITEM_DOWNLOADING && item->total > 0)
	{
		char done[24], total[24];

		workshop_browser_size_text((long long)item->downloaded, done, sizeof(done));
		workshop_browser_size_text((long long)item->total, total, sizeof(total));
		snprintf(line, (size_t)size, "Installing %d%% - %s of %s", (int)(item->downloaded * 100 / item->total), done,
			total);
		*bar = (float)item->downloaded / (float)item->total;
		*bar = *bar < 0 ? 0 : *bar > 1 ? 1 : *bar;
		return line;
	}
	if (item->state == WORKSHOP_ITEM_WAITING && workshop_browser_link_offered())
		return "Starting download";
	if (item->state == WORKSHOP_ITEM_WAITING && workshop_browser_bridge_status() != STEAM_BRIDGE_READY)
		return "Open Steam to subscribe";
	return NULL;
}

/* the list's heading: its page, and PREVIOUS and NEXT (LT and RT, Page Up
and Page Down) while Steam's list has pages */
static void heading_render(float left, float top, short status,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index))
{
	char heading[48];
	float right = left + LIST_WIDTH;
	short index;

	if (workshop_browser_filter() != WORKSHOP_FILTER_INSTALLED)
		snprintf(heading, sizeof(heading), "MCC WORKSHOP - PAGE %d", workshop_browser_page());
	else
		snprintf(heading, sizeof(heading), "MCC WORKSHOP - INSTALLED");
	ui_style_text(UI_TEXT_HEADING, left, top, UI_ALIGN_LEFT, heading);
	if (status == WORKSHOP_LIST_LOADING)
	{
		ui_style_text(UI_TEXT_LABEL, right, top + 1, UI_ALIGN_RIGHT, "Loading");
		return;
	}
	/* (failed: why (the find row's page has RETRY); a busy Steam's wait
	counted down instead) */
	if (status == WORKSHOP_LIST_FAILED)
	{
		int busy = workshop_browser_busy_seconds();
		char word[40];

		if (busy)
			snprintf(word, sizeof(word), "Steam is busy, %ds", busy);
		else
			snprintf(word, sizeof(word), "Can't reach Steam");
		ui_style_text(UI_TEXT_LABEL, right, top + 1, UI_ALIGN_RIGHT, word);
		return;
	}
	if (!guide_workshop_pages())
		return;
	for (index = 0; index < 2; index++)
	{
		/* (PREVIOUS on the left, NEXT on the right; one that can't, dimmed) */
		boolean can = index ? guide_workshop_page_offered(1) : guide_workshop_page_offered(-1);
		float x = right - (2 - index) * PAGE_BUTTON_WIDTH - (1 - index) * 4;

		ui_overlay_text(UI_FONT_MENU, 8.5f, x + PAGE_BUTTON_WIDTH * 0.5f, top, UI_ALIGN_CENTER,
			can ? COLOR_VALUE : COLOR_VALUE & 0xFFFFFF40, index ? "NEXT \xE2\x80\xBA" : "\xE2\x80\xB9 PREV");
		if (target && can)
			target(x, top - 3, x + PAGE_BUTTON_WIDTH, top + 12, GUIDE_WORKSHOP_TARGET_PAGE, index);
	}
}

void guide_workshop_render(float left, float top, float right, float bottom,
	void (*target)(float x0, float y0, float x1, float y1, short kind, short index))
{
	short count = (short)workshop_browser_count(), index, visible;
	short status = (short)workshop_browser_status();
	struct workshop_browser_item const *item;
	float y = top + HEADING_HEIGHT, x = left + LIST_WIDTH + 12, width = right - x;
	char line[160], size[24], shown[GUIDE_LAYOUT_LINE_SIZE];

	if (workshop_view.row >= count)
		workshop_view.row = (short)MAX(count - 1, 0);
	if (!count)
	{
		ui_style_text(UI_TEXT_LINE, (left + right) * 0.5f, (top + bottom) * 0.5f - 14, UI_ALIGN_CENTER,
			status == WORKSHOP_LIST_LOADING ? "Loading" : status == WORKSHOP_LIST_FAILED ? "Can't reach the site" :
			"No Workshop items");
		return;
	}
	heading_render(left, top, status, target);
	visible = (short)((bottom - y + ROW_GAP) / (ROW_HEIGHT + ROW_GAP));
	if (workshop_view.row < workshop_view.first)
		workshop_view.first = workshop_view.row;
	else if (workshop_view.row >= workshop_view.first + visible)
		workshop_view.first = (short)(workshop_view.row - visible + 1);
	for (index = workshop_view.first; index < count && index < workshop_view.first + visible; index++)
	{
		boolean on = index == workshop_view.row && !workshop_view.page;
		char const *state;
		float state_width;

		item = workshop_browser_item(index);
		state = item_word(item);
		state_width = state[0] ? ui_overlay_text_width(UI_FONT_MENU, 8.0f, state) + 8 : 0;
		ui_style_row(left, y, LIST_WIDTH, ROW_HEIGHT, on);
		if (on)
			text_line_focused(UI_TEXT_NAME, left + 10, y + 3, LIST_WIDTH - 20, UI_ALIGN_LEFT, shown_title(item),
				1 + index);
		else
			text_line(UI_TEXT_NAME, left + 10, y + 3, LIST_WIDTH - 20, UI_ALIGN_LEFT, shown_title(item));
		/* (an author's name of letters the font lacks: left out, as a title's) */
		ui_overlay_text_shown(UI_FONT_MENU, item->author, shown, sizeof(shown));
		if (shown[0])
			snprintf(line, sizeof(line), "%s - %s", kind_text(item), shown);
		else
			snprintf(line, sizeof(line), "%s", kind_text(item));
		text_line(UI_TEXT_STATUS, left + 10, y + 17, LIST_WIDTH - 20 - state_width, UI_ALIGN_LEFT, line);
		ui_overlay_text(UI_FONT_MENU, 8.0f, left + LIST_WIDTH - 10, y + 17, UI_ALIGN_RIGHT, item_color(item), state);
		if (target)
			target(left, y, left + LIST_WIDTH, y + ROW_HEIGHT, GUIDE_WORKSHOP_TARGET_ROW, index);
		y += ROW_HEIGHT + ROW_GAP;
	}
	/* the chosen item's page */
	item = chosen();
	if (!item)
		return;
	ui_style_glass(x, top, width, bottom - top, 8, ui_style_color(UI_COLOR_PANE), ui_style_color(UI_COLOR_ROW_EDGE));
	if (item->setup == WORKSHOP_ROW_FIND)
	{
		short options[MAXIMUM_OPTIONS], option_count = options_of(item, options);
		float option_height = OPTION_HEIGHT - 4, option_gap = OPTION_GAP - 1;

		text_line(UI_TEXT_TITLE, x + 12, top + 12, width - 24, UI_ALIGN_LEFT, item->title);
		text_line(UI_TEXT_LINE, x + 12, top + 32, width - 24, UI_ALIGN_LEFT, kind_text(item));
		y = top + 50;
		if (workshop_view.searching)
		{
			boolean blink = (system_milliseconds() / 500) % 2 == 0;

			snprintf(line, sizeof(line), "%s%s", workshop_view.search, blink ? "_" : " ");
			ui_style_row(x + 12, y + 4, width - 24, OPTION_HEIGHT + 4, TRUE);
			guide_layout_fit(line, width - 44, 11.0f, measure_font, (void *)(intptr_t)UI_FONT_MENU, 1, shown,
				sizeof(shown));
			ui_overlay_text(UI_FONT_MENU, 11.0f, x + 22, y + 11, UI_ALIGN_LEFT, COLOR_VALUE, shown);
			ui_style_text(UI_TEXT_LABEL, x + 12, y + OPTION_HEIGHT + 14, UI_ALIGN_LEFT, "Type, then Enter");
			return;
		}
		/* (the find row's many, smaller, from under its title down, as many as fit) */
		for (index = 0; index < option_count && y + option_height <= bottom - 8; index++)
		{
			boolean on = workshop_view.page && index == workshop_view.option;

			ui_style_row(x + 12, y, width - 24, option_height, on);
			ui_overlay_text(UI_FONT_MENU, 10.0f, x + 22, y + (option_height - 10) * 0.5f, UI_ALIGN_LEFT, COLOR_VALUE,
				option_label(item, options[index]));
			if (target)
				target(x + 12, y, x + width - 12, y + option_height, GUIDE_WORKSHOP_TARGET_OPTION, index);
			y += option_height + option_gap;
		}
		return;
	}
	{
		struct guide_rect pane = { x, top, width, bottom - top };
		struct guide_page_layout layout;
		short options[MAXIMUM_OPTIONS], option_count = (short)MIN(options_of(item, options), GUIDE_PAGE_MAXIMUM_BUTTONS);
		char note_text[96];
		float bar, state_width;
		char const *note = page_note(item, note_text, sizeof(note_text), &bar), *state = item_word(item);
		short lines;

		guide_layout_page(pane, note != NULL, option_count, DESCRIPTION_LINE, &layout);
		/* (its title; its kind and size, and where it stands) */
		if (workshop_view.page)
			text_line_focused(UI_TEXT_TITLE, layout.title.x, layout.title.y, layout.title.width, UI_ALIGN_LEFT,
				shown_title(item), -1 - workshop_view.row);
		else
			text_line(UI_TEXT_TITLE, layout.title.x, layout.title.y, layout.title.width, UI_ALIGN_LEFT,
				shown_title(item));
		workshop_browser_size_text(item->size, size, sizeof(size));
		snprintf(line, sizeof(line), "%s%s%s", kind_text(item), size[0] ? " - " : "", size);
		state_width = state[0] ? ui_overlay_text_width(UI_FONT_MENU, 9.0f, state) + 8 : 0;
		text_line(UI_TEXT_LINE, layout.status.x, layout.status.y, layout.status.width - state_width, UI_ALIGN_LEFT, line);
		ui_overlay_text(UI_FONT_MENU, 9.0f, layout.status.x + layout.status.width, layout.status.y + 1, UI_ALIGN_RIGHT,
			item_color(item), state);
		/* (its picture in its box, the item's own or Steam's; a setup row's
		words take the box's room) */
		if (item->setup)
		{
			float room = layout.description.y + layout.description.height - layout.picture.y;

			layout.description.y = layout.picture.y;
			layout.description_lines = (int)(room / DESCRIPTION_LINE);
		}
		else if (layout.picture.height > 0)
		{
			int picture_width, picture_height, serial;
			unsigned char const *picture = workshop_browser_preview(workshop_view.row, &picture_width, &picture_height,
				&serial);
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
			/* (none to be had: said so, not an empty box) */
			else if (workshop_browser_preview_missing(workshop_view.row))
				ui_style_text(UI_TEXT_LINE, box.x + box.width * 0.5f,
					box.y + (box.height - ui_style_text_size(UI_TEXT_LINE)) * 0.5f, UI_ALIGN_CENTER, "No picture");
		}
		/* (its description: the author's words, then a pack's maps, as many
		lines as the room has, the rest scrolled to) */
		lines = (short)layout.description_lines;
		if (lines > 0)
			description_render(item, layout.description, lines, target);
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
			boolean on = workshop_view.page && index == workshop_view.option && !workshop_view.description_on;

			/* (Workshop maps off: the row is the setting, drawn as SETTINGS'
			rows are, its value between their arrows; pressed, it turns them on) */
			if (options[index] == _option_show_maps)
				ui_style_setting(button.x, button.y, button.width, button.height, 0.9f, "WORKSHOP MAPS", NULL, "OFF", on);
			else
			{
				ui_style_row(button.x, button.y, button.width, button.height, on);
				text_fit(UI_TEXT_NAME, option_label(item, options[index]), button.width - 20, shown, sizeof(shown));
				ui_overlay_text(UI_FONT_MENU, 10.0f, button.x + 10, button.y + (button.height - 10) * 0.5f, UI_ALIGN_LEFT,
					COLOR_VALUE, shown);
			}
			if (target)
				target(button.x, button.y, button.x + button.width, button.y + button.height, GUIDE_WORKSHOP_TARGET_OPTION,
					index);
		}
	}
}

#endif
#endif /* HALO_MCC */
