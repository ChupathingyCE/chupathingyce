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
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "guide.h"

#ifdef HALO_GUIDE

#include "../src/ui_overlay.h"
#include "../src/workshop_browser.h"
#include "../src/steam_client_bridge.h"
#include "../src/browser.h"
#include "../src/settings_rows.h"
#include "guide_workshop.h"
#include "mcc_maps.h"
#include "input/input.h"
#include "interface/virtual_keyboard.h"

/* (a key stroke's control modifier: input_xbox.c's, as browser_screen.c has it) */
#define KEY_MODIFIER_CONTROL_BIT 1

#include <stdio.h>
#include <string.h>

int config_boolean(char const *name);
char const *config_string(char const *name);
boolean main_menu_is_active(void);
void platform_text_field(int typing);
unsigned long system_milliseconds(void);
int platform_clipboard_get(char *text, int size);
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
	_option_search,
	_option_play,
	_option_update,
	_option_remove,
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
	/* REMOVE pressed once: CONFIRM deletes the files */
	boolean confirm_remove;
	/* LINK's QR: Steam's text for it, and its modules (made again when the text changes) */
	char link_text[256];
	short link_qr_size;
	unsigned char link_qr[BROWSER_CONNECT_QR_SIZE * BROWSER_CONNECT_QR_SIZE];
	/* SEARCH's text box: typing into it (enter keeps, escape drops) */
	boolean searching;
	char search[64];
	unsigned long debug_search_time;
	/* a controller's search: the game's on-screen keyboard (as the map
	choosers' Search has it), the Guide closed under it and opened again
	on WORKSHOP once it is done */
	boolean keyboard;
	wchar_t keyboard_text[64];
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
	if (item->state == WORKSHOP_ITEM_INSTALLED)
	{
		if (main_menu_is_active())
			options[count++] = _option_play;
		if (bridge && workshop_browser_update_offered(workshop_view.row))
			options[count++] = _option_update;
		if (workshop_browser_removable(workshop_view.row))
			options[count++] = _option_remove;
	}
	/* (GET; the built-in download shows its progress instead of Steam's page while it works) */
	if (item->state != WORKSHOP_ITEM_INSTALLED && !(bridge && item->steam_subscribed) &&
		!(workshop_browser_link_offered() && item->state != WORKSHOP_ITEM_NOT_INSTALLED))
	{
		options[count++] = _option_steam;
	}
	if (bridge && item->steam_subscribed && steam_bridge_can_unsubscribe())
		options[count++] = _option_unsubscribe;
	/* (steamcmd's sign-in; the built-in download's LINK STEAM is on GET itself) */
	if (bridge && steam_bridge_sign_in_needed() && !workshop_browser_link_offered())
		options[count++] = _option_sign_in;
	if (!steam_bridge_enabled() && workshop_browser_bridge_backend() != STEAM_BRIDGE_BACKEND_NONE)
		options[count++] = _option_use_steam;
	if (!item->setup && !config_boolean("game.workshop_maps"))
		options[count++] = _option_show_maps;
	return count;
}

static boolean link_needed(void);

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
	case _option_update: return link_needed() ? "LINK STEAM" : "UPDATE";
	case _option_remove: return workshop_view.confirm_remove ? "CONFIRM" : "REMOVE";
	case _option_show_maps: return "TURN ON";
	case _option_unsubscribe: return "REMOVE";
	case _option_sign_in: return steam_link_available() ? "LINK STEAM" : "SIGN IN";
	case _option_link: return "LINK";
	case _option_page_next: return "NEXT PAGE";
	case _option_page_previous: return "PREVIOUS";
	case _option_use_steam: return workshop_view.confirm ? "CONFIRM" : "USE STEAM";
	default: break;
	}
	if (item->setup)
		return workshop_browser_setup_button(workshop_view.row);
	/* (installed, or on its way: its page in Steam; GET needs the Steam link) */
	if (item->state == WORKSHOP_ITEM_NOT_INSTALLED && link_needed())
		return "LINK STEAM";
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

/* Settings' STEAM ACCOUNT, its QR up at once */
static void link_go(void)
{
	workshop_view.page = FALSE;
	steam_link_start();
	guide_open_setting("game.workshop_steam");
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
		if (workshop_browser_play(workshop_view.row))
			guide_close();
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
		/* (twice: the files are deleted) */
		if (!workshop_view.confirm_remove)
		{
			workshop_view.confirm_remove = TRUE;
			return;
		}
		workshop_view.confirm_remove = FALSE;
		workshop_view.option = 0;
		workshop_browser_remove(workshop_view.row);
		return;
	case _option_show_maps:
		config_write_boolean("game.workshop_maps", 1);
		mcc_maps_rescan();
		return;
	case _option_unsubscribe:
		workshop_browser_unsubscribe(workshop_view.row);
		return;
	case _option_sign_in:
		/* (the built-in download: to the link row, its QR shown) */
		if (workshop_browser_link_offered())
		{
			workshop_view.row = 0;
			workshop_view.page = TRUE;
			workshop_view.option = 0;
			steam_link_start();
			return;
		}
		steam_bridge_sign_in();
		return;
	case _option_link:
		/* (SETTINGS' STEAM ACCOUNT, its QR up at once) */
		workshop_view.page = FALSE;
		steam_link_start();
		guide_open_setting("game.workshop_steam");
		return;
	case _option_page_next:
	case _option_page_previous:
		if (option == _option_page_next ? workshop_browser_page_next() : workshop_browser_page_previous())
		{
			/* (the new page's first item chosen: the row under the find row) */
			short index;

			for (index = 0; index < (short)workshop_browser_count(); index++)
			{
				struct workshop_browser_item const *row = workshop_browser_item(index);

				if (row && row->setup == WORKSHOP_ROW_FIND)
				{
					workshop_view.row = (short)(index + 1);
					workshop_view.first = index;
					break;
				}
			}
			workshop_view.page = FALSE;
			workshop_view.option = 0;
		}
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

/* LINK's QR, while Steam waits for the scan: dark on white with the quiet
edge readers need, and the step in words under it. Its bottom; y if none */
static float link_qr_render(float x, float y, float width, float bottom)
{
	char text[256], error[24];
	int link = steam_link_poll(text, sizeof(text), error, sizeof(error));
	float side, module;
	short qr_x, qr_y, run, size;

	if (link == STEAM_LINK_STARTING)
	{
		ui_style_text(UI_TEXT_LABEL, x, y + 4, UI_ALIGN_LEFT, "Getting a code");
		return y + 20;
	}
	if (link == STEAM_LINK_FAILED)
	{
		ui_style_text(UI_TEXT_LABEL, x, y + 4, UI_ALIGN_LEFT, error[0] ? error : "Failed");
		return y + 20;
	}
	if (link != STEAM_LINK_WAITING && link != STEAM_LINK_SCANNED)
		return y;
	if (strcmp(text, workshop_view.link_text))
	{
		snprintf(workshop_view.link_text, sizeof(workshop_view.link_text), "%s", text);
		workshop_view.link_qr_size = (short)browser_text_qr(text, workshop_view.link_qr);
	}
	size = workshop_view.link_qr_size;
	side = MIN(width, bottom - y - 24);
	if (size <= 0 || side < 60)
		return y;
	module = side / (size + 8);
	ui_overlay_rect(x, y, side, side, 4, 0xFFFFFFFF);
	for (qr_y = 0; qr_y < size; qr_y++)
	{
		for (qr_x = 0; qr_x < size; qr_x = (short)(qr_x + run))
		{
			unsigned char dark = workshop_view.link_qr[qr_y * size + qr_x];

			for (run = 0; qr_x + run < size && workshop_view.link_qr[qr_y * size + qr_x + run] == dark; run++)
				;
			if (dark)
				ui_overlay_rect(x + (qr_x + 4) * module, y + (qr_y + 4) * module, run * module + 0.3f, module + 0.3f, 0,
					0x0A1A33FF);
		}
	}
	ui_style_text(UI_TEXT_LABEL, x, y + side + 6, UI_ALIGN_LEFT,
		link == STEAM_LINK_SCANNED ? "Approve on your phone" : "Scan with the Steam app");
	return y + side + 22;
}

/* SETTINGS' STEAM ACCOUNT pressed: LINK (its QR beside it), CANCEL while
it waits, UNLINK twice when linked */
static void link_settings_press(void)
{
	int link = steam_link_poll(NULL, 0, NULL, 0);

	if (steam_link_linked(NULL, 0))
	{
		if (!steam_link_armed())
		{
			steam_link_set_armed(1);
			return;
		}
		steam_link_set_armed(0);
		steam_link_unlink();
		return;
	}
	if (link == STEAM_LINK_STARTING || link == STEAM_LINK_WAITING || link == STEAM_LINK_SCANNED)
		steam_link_cancel();
	else
		steam_link_start();
}

void guide_workshop_link_panel(float x, float y, float width, float bottom)
{
	int link = steam_link_poll(NULL, 0, NULL, 0);
	float height;

	if (steam_link_armed() && steam_link_linked(NULL, 0))
	{
		ui_style_glass(x, y, width, 70, 8, ui_style_color(UI_COLOR_PANE), ui_style_color(UI_COLOR_ROW_EDGE));
		text_wrapped("Press again to unlink. The game forgets it here and at Steam.", x + 12, y + 14, width - 24, 3);
		return;
	}
	if (link != STEAM_LINK_STARTING && link != STEAM_LINK_WAITING && link != STEAM_LINK_SCANNED &&
		link != STEAM_LINK_FAILED)
	{
		return;
	}
	height = MIN(width + 40, bottom - y);
	ui_style_glass(x, y, width, height, 8, ui_style_color(UI_COLOR_PANE), ui_style_color(UI_COLOR_ROW_EDGE));
	link_qr_render(x + 12, y + 12, width - 24, y + height - 6);
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

	workshop_browser_own_library();
	settings_rows_set_steam_link_action(workshop_browser_link_offered() ? link_settings_press : NULL);
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
	if (workshop_view.searching)
		return;
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
	workshop_view.page = FALSE;
	workshop_view.confirm = FALSE;
	workshop_view.confirm_remove = FALSE;
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
	if (workshop_browser_filter() != WORKSHOP_FILTER_INSTALLED)
		snprintf(heading, sizeof(heading), "MCC WORKSHOP - PAGE %d", workshop_browser_page());
	else
		snprintf(heading, sizeof(heading), "MCC WORKSHOP - INSTALLED");
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
	if (workshop_view.searching && item->setup == WORKSHOP_ROW_FIND)
	{
		char shown[80];
		boolean blink = (system_milliseconds() / 500) % 2 == 0;

		snprintf(shown, sizeof(shown), "%s%s", workshop_view.search, blink ? "_" : " ");
		ui_style_row(x + 12, y + 4, width - 24, OPTION_HEIGHT + 4, TRUE);
		ui_overlay_text(UI_FONT_MENU, 11.0f, x + 22, y + 11, UI_ALIGN_LEFT, COLOR_VALUE, shown);
		ui_style_text(UI_TEXT_LABEL, x + 12, y + OPTION_HEIGHT + 14, UI_ALIGN_LEFT, "Type, then Enter");
		return;
	}
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
		(short)MAX(1, MIN(DESCRIPTION_LINES, (bottom - 12 - 3 * (OPTION_HEIGHT + OPTION_GAP) - 20 - y) /
		(DESCRIPTION_SIZE + 4))));
	{
		short options[MAXIMUM_OPTIONS], option_count = options_of(item, options);
		float option_height = OPTION_HEIGHT, option_gap = OPTION_GAP;

		y = bottom - 12 - option_count * (OPTION_HEIGHT + OPTION_GAP) + OPTION_GAP;
		/* (the find row's many, smaller, from under its title down) */
		if (item->setup == WORKSHOP_ROW_FIND)
		{
			option_height = OPTION_HEIGHT - 4;
			option_gap = OPTION_GAP - 1;
			y = top + 50;
		}
		/* (an action line over the options: what they do, in a few words) */
		if (workshop_view.confirm_remove)
			ui_style_text(UI_TEXT_LABEL, x + 12, y - 16, UI_ALIGN_LEFT, "Delete its files?");
		else if (workshop_view.confirm)
			text_wrapped(use_steam_note(), x + 12, y - 4 - NOTE_LINES * (DESCRIPTION_SIZE + 4), width - 24, NOTE_LINES);
		else if (!item->setup && workshop_browser_bridge_status() == STEAM_BRIDGE_READY && steam_bridge_sign_in_needed() &&
			!workshop_browser_link_offered())
			ui_style_text(UI_TEXT_LABEL, x + 12, y - 16, UI_ALIGN_LEFT,
				workshop_browser_link_offered() ? "Link Steam first" : "Sign in to steamcmd");
		else if (!item->setup && item->steam_error[0] && item->state != WORKSHOP_ITEM_INSTALLED)
			ui_style_text(UI_TEXT_LABEL, x + 12, y - 16, UI_ALIGN_LEFT, item->steam_error);
		else if (item->state == WORKSHOP_ITEM_DOWNLOADING && item->total > 0)
		{
			/* (its words, and a bar under them) */
			float bar_y = option_count ? y - 14 : bottom - 24, fraction = (float)item->downloaded / (float)item->total;
			char done[24], total[24];

			workshop_browser_size_text((long long)item->downloaded, done, sizeof(done));
			workshop_browser_size_text((long long)item->total, total, sizeof(total));
			snprintf(line, sizeof(line), "Downloading %d%% - %s of %s", (int)(item->downloaded * 100 / item->total),
				done, total);
			ui_style_text(UI_TEXT_LABEL, x + 12, bar_y - 18, UI_ALIGN_LEFT, line);
			fraction = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
			ui_overlay_rect(x + 12, bar_y, width - 24, 6, 3, 0xFFFFFF26);
			ui_overlay_rect(x + 12, bar_y, (width - 24) * fraction, 6, 3, COLOR_WAITING);
		}
		else if (item->state == WORKSHOP_ITEM_WAITING && workshop_browser_link_offered())
			ui_style_text(UI_TEXT_LABEL, x + 12, (option_count ? y : bottom - 8) - 16, UI_ALIGN_LEFT, "Starting download");
		else if (item->state == WORKSHOP_ITEM_WAITING && workshop_browser_bridge_status() != STEAM_BRIDGE_READY)
			ui_style_text(UI_TEXT_LABEL, x + 12, y - 16, UI_ALIGN_LEFT, "Open Steam to subscribe");
		else if (!item->setup && !config_boolean("game.workshop_maps"))
			ui_style_text(UI_TEXT_LABEL, x + 12, y - 16, UI_ALIGN_LEFT, "Workshop maps are off");
		for (index = 0; index < option_count; index++)
		{
			boolean on = workshop_view.page && index == workshop_view.option;

			ui_style_row(x + 12, y, width - 24, option_height, on);
			ui_overlay_text(UI_FONT_MENU, 10.0f, x + 22, y + (option_height - 10) * 0.5f, UI_ALIGN_LEFT, COLOR_VALUE,
				option_label(item, options[index]));
			if (target)
				target(x + 12, y, x + width - 12, y + option_height, 1, index);
			y += option_height + option_gap;
		}
	}
}

#endif
#endif /* HALO_MCC */
