/*
GUIDE.C

The Guide (guide.h): one panel over whatever is on screen, the same in the
Xbox's, Gearbox's and the Chupathingy menus and over a game in progress.
Its look is the Chupathingy menus' (the Xbox's blues, the plasma under the
menus' items, glow on the selected row and the edges, the prompt bar), its
data social.c's.

The panel: the player's card (their name and what they're doing), then
the sections, switched with LB and RB (Q and E, or a click): FRIENDS,
PARTY, PLAYERS HERE, RECENT and SETTINGS.
- PLAYERS HERE lists the game's players: their Delta Tag when they share
  their profile (else their game name), their platform's logo when the game
  is on Delta, and a local Mute (the chat log's, chat.c). No address is ever
  shown.
- SETTINGS has the Guide's own settings.
- FRIENDS is the site's friends (browser.c polls them, social.h): the
  invites, the friends online, the offline ones (a row that shows them),
  the friend requests. A on a row opens its options: a friend's JOIN (the
  site checks, then the game joins its halo://join link), INVITE TO MY GAME
  (in a game on Delta), REMOVE and BLOCK (each pressed twice); an invite's
  JOIN and DISMISS; a request's ACCEPT and DECLINE. Only Delta profiles,
  never an OpenCE player, never an address.
- PARTY and RECENT are still empty. Without a Delta profile, FRIENDS,
  PARTY and RECENT offer SIGN IN (Delta Profile's Link Profile).

It takes the first player's input while open (guide_input.h): the game
gets a controller at rest, and their player stands still
(guide_takes_player). Network games keep running; a game on this machine
alone pauses as it does under the pause menu. B closes it, back to what was
under it as it was.

It opens with controls.guide (F3), the first player's Guide button
(controls.guide_button, where SDL reads it), Back held in the menus with
guide.hold_back (off by default), or the first player's pause menu's GUIDE
(menu_tags.c, guide.pause_menu); the same key or button, or B, closes it.

Toasts show low in the middle of the screen for a few seconds: a new
invite or friend request, and a friend come online (guide.notify_online,
off by default, menus only). In a game only with guide.notify_in_game. The
Guide's key or button while one shows opens its item. The pause menu's
GUIDE counts what waits ("GUIDE 2": invites and requests).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "guide.h"

#ifdef HALO_GUIDE

#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "interface/event_manager.h"
#include "interface/interface.h"
#include "interface/ui_widget.h"
#include "bitmaps/bitmap_group.h"
#include "cache/texture_cache.h"
#include "networking/network_game_globals.h"
#include "saved games/player_profile.h"
#include "interface/player_ui.h"
#include "main/main.h"
#include "bink/bink_playback.h"
#include "../src/browser.h"
#include "../src/delta_profile.h"
#include "../src/discord_activity.h"
#include "../src/guide_input.h"
#include "../src/social.h"
#include "../src/ui_overlay.h"
#include "halo_ui_pointer.h"
#include "halo_profile_screen.h"
#include "chat.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

int config_boolean(char const *name);
char const *config_string(char const *name);
int config_write(char const *name, char const *value);
int config_write_boolean(char const *name, int value);
void platform_log(char const *format, ...);
/* delta_peer_game.c's */
int delta_peer_game_on_delta(void);
int delta_peer_player_platform(int player_index);
int delta_peer_player_profile(int player_index, unsigned char *player_id);
/* discord_game.c's */
boolean discord_game_get(struct discord_activity_game *game);
/* virtual_keyboard.c's */
boolean virtual_keyboard_active(void);
/* p2p.c's: this copy's game's invite (hosted, else joined), and an invite
link joined */
int p2p_hosting_invite(char *text, int size);
int p2p_joined_invite(char *text, int size);
int p2p_join_invite(char const *text);
/* menu_tags.c's: the pause menu's GUIDE text (NONE: none in this map) */
long pc_menu_pause_guide_label(void);
/* ui_widget.c's: the game paused as a menu pauses it */
void ui_widgets_port_pause_game(boolean pause);

/* ---------- constants */

/* the panel's place, in the menus' 640x480 */
enum
{
	PANEL_X = 48, PANEL_Y = 38, PANEL_WIDTH = 544, PANEL_HEIGHT = 398,
	PANEL_RIGHT = PANEL_X + PANEL_WIDTH,
	INSET = 16,
	CONTENT_LEFT = PANEL_X + INSET,
	CONTENT_RIGHT = PANEL_RIGHT - INSET,
	HEADER_Y = PANEL_Y + 12,
	RULE_Y = PANEL_Y + 54,
	TABS_Y = PANEL_Y + 62,
	TABS_HEIGHT = 22,
	BODY_Y = PANEL_Y + 96,
	PROMPTS_Y = PANEL_Y + PANEL_HEIGHT - 30,
	BODY_BOTTOM = PROMPTS_Y - 8,
	LIST_WIDTH = 262,
	DETAIL_LEFT = CONTENT_LEFT + LIST_WIDTH + 12,
	ROW_HEIGHT = 30,
	ROW_GAP = 4,
	SETTING_HEIGHT = 26,

	HEADING_HEIGHT = 16,
	OPTION_HEIGHT = 24,
	OPTION_GAP = 4,
	/* a refusal's line, shown this long */
	MESSAGE_TIME = 6000,
	/* (as Delta Profile's dialogs: DIALOG_SETTLE) */
	CONFIRM_SETTLE = 400,
	/* the browser thread told where the player is this often */
	WHERE_INTERVAL = 1000,

	/* the toast's place */
	TOAST_WIDTH = 260, TOAST_HEIGHT = 34,
	TOAST_X = 320 - TOAST_WIDTH / 2, TOAST_Y = 404,
	TOAST_TIME = 5000,
	TOAST_FADE = 400,

	/* a d-pad held: again after this long, then this often */
	REPEAT_DELAY = 400,
	REPEAT_INTERVAL = 120,

	/* (the plasma's map from ui.map: looked for this many frames, then the
	overlay's own noise) */
	PLASMA_TRIES = 240,

	MAXIMUM_TARGETS = 48,
};

/* the Chupathingy menus' blues (the mockups': review/guide-mockup) */
enum
{
	COLOR_PANEL = 0x0A2652E0,
	COLOR_PANEL_EDGE = 0x2F7FD6FF,
	COLOR_GLOW = 0x2F8FFFFF,
	COLOR_RULE = 0x50A0FF5A,
	COLOR_TITLE = 0x3D9BFFFF,
	COLOR_NAME = 0xFFFFFFFF,
	COLOR_STATUS = 0x8FC4FFFF,
	COLOR_LABEL = 0x6FA6DDFF,
	COLOR_TAB = 0x7FB6EEFF,
	COLOR_TAB_ON = 0x2F7FD68C,
	COLOR_SELECTED_EDGE = 0x9FD4FFFF,
	COLOR_ROW = 0x0A2246E6,
	COLOR_ROW_EDGE = 0x50A0FF40,
	COLOR_ROW_ON = 0x2F7FD670,
	COLOR_DETAIL = 0x020C1EA0,
	COLOR_VALUE = 0xFFFFFFFF,
	COLOR_SETTING = 0x9FD0FFFF,
	COLOR_PROMPT = 0x3D9BFFFF,
	COLOR_PROMPT_GLOW = 0x3D8BFF80,
	COLOR_DOT_GAME = 0x4FB3FFFF,
	COLOR_DOT_ONLINE = 0x7FE39AFF,
	COLOR_DOT_AWAY = 0xE0A53AFF,
	COLOR_DOT_OFFLINE = 0x4A5A70FF,
	COLOR_MUTED = 0xFF9F9FFF,
};

/* the Guide's own settings, as SETTINGS lists them */
enum
{
	_setting_open_key,
	_setting_guide_button,
	_setting_notify_in_game,
	_setting_notify_online,
	_setting_share_presence,
	NUMBER_OF_SETTINGS
};

static char const *const setting_labels[NUMBER_OF_SETTINGS] =
{
	"OPEN KEY", "GUIDE BUTTON", "NOTIFICATIONS IN GAME", "FRIEND ONLINE ALERTS", "SHOW WHEN I'M PLAYING",
};

/* SETTINGS' rows after the settings, signed in: the devices (Delta
Profile's devices dialog) and Link a device (another device's code) */
enum
{
	_device_row_devices,
	_device_row_link,
	NUMBER_OF_DEVICE_ROWS
};

static char const *const device_row_labels[NUMBER_OF_DEVICE_ROWS] =
{
	"DEVICES", "LINK A DEVICE",
};

static char const *const setting_keys[NUMBER_OF_SETTINGS] =
{
	"controls.guide", "controls.guide_button", "guide.notify_in_game", "guide.notify_online", "network.share_presence",
};

/* controls.guide's choices: the F keys the game leaves free (F1 is Back,
F2 Start, F11 full screen, F12 the mouse) */
static char const *const open_keys[] =
{
	"F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
};

/* what a click or tap lands on */
enum
{
	_target_tab,
	_target_row,
	_target_action,
	_target_setting,
	_target_sign_in,
	_target_close,
	_target_option,
	_target_prompt,
};

/* FRIENDS' items, top to bottom: the invites, the friends online, the
offline friends' row (open: they follow), the requests */
enum
{
	_item_heading,
	_item_invite,
	_item_friend,
	_item_offline,
	_item_request,
};

enum
{
	_heading_invites,
	_heading_online,
	_heading_requests,
};

struct friend_item
{
	short kind;
	/* the invite's, friend's or request's index in the state; a heading's
	_heading_* */
	short index;
};

#define MAXIMUM_FRIEND_ITEMS (4 + SOCIAL_MAXIMUM_INVITES + SOCIAL_MAXIMUM_FRIENDS + SOCIAL_MAXIMUM_REQUESTS)

/* what a FRIENDS item's options are */
enum
{
	_option_join,
	_option_invite,
	_option_remove,
	_option_block,
	_option_accept,
	_option_decline,
	_option_dismiss,
	MAXIMUM_OPTIONS = 4
};

/* what a toast leads to */
enum
{
	_toast_none,
	_toast_invite,
	_toast_request,
	_toast_friend,
};

struct guide_target
{
	float x0, y0, x1, y1;
	short kind;
	short index;
};

/* ---------- globals */

static struct
{
	boolean open;
	short section;
	/* the row chosen in each section */
	short row[NUMBER_OF_SOCIAL_SECTIONS];
	/* the first row shown of a long list */
	short first_row;
	unsigned int held;
	unsigned int repeat_held;
	unsigned long repeat_time;
	unsigned long opened_time;
	/* it paused the game (a game on this machine alone) */
	boolean paused_game;

	/* PLAYERS HERE: the rows, and each row's player (their machine and game
	name, for the mute) */
	struct social_list players;
	struct social_list players_fresh;
	short machines[SOCIAL_MAXIMUM_ROWS];
	wchar_t names[SOCIAL_MAXIMUM_ROWS][12];

	/* the card */
	char name[SOCIAL_TAG_SIZE];
	char status[SOCIAL_STATUS_SIZE];
	unsigned int color;
	boolean linked;

	/* the toast, and when it came */
	char toast_title[96];
	char toast_line[96];
	unsigned long toast_time;
	boolean toast_shown;
	boolean debug_toast_done;
	boolean debug_open_done;
	/* the toast's item (an invite's ID, a handle) and its sender's initial */
	short toast_kind;
	char toast_key[SOCIAL_INVITE_ID_SIZE];
	char toast_initial[8];

	/* FRIENDS: the site's state's serial and status (browser.c), and
	whether one loaded this run */
	int friends_serial;
	int friends_status;
	boolean friends_loaded;
	unsigned long where_time;
	/* the items, the offline friends shown, the chosen item (by what it
	is, kept as the list changes) */
	struct friend_item items[MAXIMUM_FRIEND_ITEMS];
	short item_count;
	boolean offline_open;
	short chosen_kind;
	char chosen_key[SOCIAL_INVITE_ID_SIZE];
	/* its options panel: open (the d-pad moves in it), the option chosen,
	one waiting for its second press (REMOVE, BLOCK), NONE for none */
	boolean options;
	short option;
	short armed;
	/* the action out (browser_social_act), and a line about the last */
	boolean acting;
	int action_serial;
	char message[96];
	unsigned long message_time;
	/* the invites dismissed (by ID) */
	char dismissed[SOCIAL_MAXIMUM_INVITES][SOCIAL_INVITE_ID_SIZE];
	short dismissed_next;

	int plasma_tries;

	/* SIGN IN here (Quick Connect, browser.c's Link Profile): its code
asked for, and what it came to */
	boolean signing_in;
	struct browser_connect connect;
	/* when its question first showed: A only after CONFIRM_SETTLE, so a
	press meant for something else doesn't answer it */
	unsigned long confirm_shown;

	struct guide_target targets[MAXIMUM_TARGETS];
	short target_count;
} guide;

/* the site's friends: as shown, and as just read (browser_social_get) */
static struct social_state friends_state;
static struct social_state friends_fresh;

/* ---------- private code */

static void target_add(float x0, float y0, float x1, float y1, short kind, short index)
{
	if (guide.target_count >= MAXIMUM_TARGETS)
		return;
	guide.targets[guide.target_count].x0 = x0;
	guide.targets[guide.target_count].y0 = y0;
	guide.targets[guide.target_count].x1 = x1;
	guide.targets[guide.target_count].y1 = y1;
	guide.targets[guide.target_count].kind = kind;
	guide.targets[guide.target_count].index = index;
	guide.target_count++;
}

/* the game's UTF-16 as UTF-8 */
static void utf8_from_wide(char *text, int size, wchar_t const *wide, int length)
{
	int used = 0, index;

	for (index = 0; index < length && wide[index] && used < size - 1; index++)
	{
		unsigned int character = (unsigned short)wide[index];

		if (character < 0x80)
			text[used++] = (char)character;
		else if (character < 0x800)
		{
			if (used + 2 > size - 1)
				break;
			text[used++] = (char)(0xC0 | (character >> 6));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			if (used + 3 > size - 1)
				break;
			text[used++] = (char)(0xE0 | (character >> 12));
			text[used++] = (char)(0x80 | ((character >> 6) & 0x3F));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
	}
	text[used] = 0;
}

static unsigned int fade(unsigned int color, float share)
{
	float alpha = (float)(color & 0xFF) * share;

	return (color & 0xFFFFFF00) | (unsigned int)(alpha < 0.0f ? 0.0f : alpha > 255.0f ? 255.0f : alpha);
}

/* a game alone on this machine: the Guide pauses it as the pause menu does */
static boolean pauses_game(void)
{
	return !main_menu_is_active() && game_in_progress() && game_connection() == _game_connection_local;
}

static boolean in_game(void)
{
	return !main_menu_is_active() && game_in_progress();
}

/* whether SIGN IN goes anywhere: Delta Profile's Link Profile */
static short sign_in_section(void)
{
	short section, count;

	if (!profile_screen_available())
		return NONE;
	count = profile_screen_section_count();
	for (section = 0; section < count; section++)
	{
		if (!strcmp(profile_screen_section_name(section), "LINK PROFILE"))
			return section;
	}
	return NONE;
}

/* ---------- the card */

static void card_update(void)
{
	struct discord_activity_game game;
	char handle[64];

	guide.linked = browser_profile_linked(handle, sizeof(handle)) != 0;
	guide.name[0] = 0;
	/* (the profile's name as the site shows it, else its handle) */
	if (guide.linked && guide.friends_loaded && friends_state.handle[0])
		social_copy_text(guide.name, sizeof(guide.name), social_name(friends_state.display, friends_state.handle));
	else if (guide.linked && handle[0])
		social_copy_text(guide.name, sizeof(guide.name), handle);
	guide.color = 0x2F6DD0FF;
	if (player_ui_get_active_player_profile_index(0) != NONE)
	{
		struct player_profile profile;
		real_rgb_color color;

		player_ui_get_active_player_profile(0, &profile);
		if (!guide.name[0])
			utf8_from_wide(guide.name, sizeof(guide.name), profile.player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH);
		color = player_profile_get_rgb_color(profile.primary_color_index);
		guide.color = ((unsigned int)(PIN(color.red, 0.0f, 1.0f) * 255.0f) << 24) |
			((unsigned int)(PIN(color.green, 0.0f, 1.0f) * 255.0f) << 16) |
			((unsigned int)(PIN(color.blue, 0.0f, 1.0f) * 255.0f) << 8) | 0xFF;
	}
	/* (no profile chosen, in a game: the first player's name there) */
	if (!guide.name[0] && in_game())
	{
		struct data_iterator iterator;
		struct player_datum *player;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->local_player_index == 0)
			{
				utf8_from_wide(guide.name, sizeof(guide.name), player->name, NUMBEROF(player->name));
				break;
			}
		}
	}
	social_own_status(discord_game_get(&game) ? &game : NULL, guide.status, sizeof(guide.status));
}

/* ---------- PLAYERS HERE: the game's players (none outside a network game) */

static void players_update(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	boolean delta = delta_peer_game_on_delta() != 0;
	boolean teams = game_engine_running() && game_engine_has_teams();
	short count = 0;

	social_list_clear(&guide.players_fresh);
	if (!network_game_get_game() || main_menu_is_active() || !game_in_progress())
	{
		social_list_commit(&guide.players, &guide.players_fresh);
		return;
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL && count < SOCIAL_MAXIMUM_ROWS)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct social_row row;
		wchar_t shown[48];
		boolean you = player->local_player_index != NONE;

		if (player->quit_out_of_game)
			continue;
		csmemset(&row, 0, sizeof(row));
		/* (a shared Delta profile's name, else the game name) */
		if (profile_names_player(absolute, shown, NUMBEROF(shown)))
		{
			utf8_from_wide(row.tag, sizeof(row.tag), shown, NUMBEROF(shown));
			row.verified = TRUE;
		}
		else
			utf8_from_wide(row.tag, sizeof(row.tag), player->name, NUMBEROF(player->name));
		row.platform = delta ? delta_peer_player_platform((int)absolute) : 0;
		if (row.platform < 0)
			row.platform = 0;
		row.state = SOCIAL_STATE_IN_GAME;
		row.key = (int)absolute;
		row.muted = !you && chat_local_muted(player->network_player_data.machine_index, player->name);
		row.actions = you ? 0 : SOCIAL_ACTION_MUTE;
		if (you)
			snprintf(row.status, sizeof(row.status), "You");
		else if (row.muted)
			snprintf(row.status, sizeof(row.status), "Muted");
		else if (teams)
			snprintf(row.status, sizeof(row.status), "%s", player->team_index == 0 ? "Red Team" : "Blue Team");
		if (social_list_add(&guide.players_fresh, &row) >= 0)
		{
			guide.machines[guide.players_fresh.count - 1] = player->network_player_data.machine_index;
			csmemcpy(guide.names[guide.players_fresh.count - 1], player->name, sizeof(guide.names[0]));
			count++;
		}
	}
	social_list_commit(&guide.players, &guide.players_fresh);
	if (guide.row[SOCIAL_SECTION_PLAYERS_HERE] >= guide.players.count)
		guide.row[SOCIAL_SECTION_PLAYERS_HERE] = (short)MAX(0, guide.players.count - 1);
}

static void player_mute_toggle(short row)
{
	struct social_row const *player;

	if (row < 0 || row >= guide.players.count)
		return;
	player = &guide.players.rows[row];
	if (!(player->actions & SOCIAL_ACTION_MUTE))
		return;
	chat_local_mute(guide.machines[row], guide.names[row], !player->muted);
	players_update();
}

static void message_set(char const *text);

/* ---------- SETTINGS */

static boolean setting_on(short setting)
{
	return config_boolean(setting_keys[setting]) != 0;
}

static void setting_value(short setting, char *text, int size)
{
	if (setting == _setting_open_key)
	{
		char const *key = config_string(setting_keys[setting]);

		snprintf(text, (size_t)size, "%s", key[0] ? key : "F3");
	}
	else
		snprintf(text, (size_t)size, "%s", setting_on(setting) ? "ON" : "OFF");
}

static void setting_change(short setting, short direction)
{
	if (setting == _setting_open_key)
	{
		char const *key = config_string(setting_keys[setting]);
		short count = (short)NUMBEROF(open_keys), index, found = 0;

		for (index = 0; index < count; index++)
		{
			char const *a = key, *b = open_keys[index];

			while (*a && (*a | 0x20) == (*b | 0x20))
				a++, b++;
			if (!*a && !*b)
				found = index;
		}
		found = (short)((found + (direction < 0 ? count - 1 : 1)) % count);
		config_write(setting_keys[setting], open_keys[found]);
	}
	else if (setting >= 0 && setting < NUMBER_OF_SETTINGS)
		config_write_boolean(setting_keys[setting], !setting_on(setting));
}

/* SETTINGS' rows: the settings, then the devices' rows when signed in */
static short settings_rows(void)
{
	return (short)(NUMBER_OF_SETTINGS + (guide.linked && profile_screen_available() ? NUMBER_OF_DEVICE_ROWS : 0));
}

/* a device row's A: Delta Profile's dialog, over what was under DELTA */
static void device_row_activate(short row)
{
	guide_close();
	if (!(row == _device_row_devices ? profile_screen_open_devices() : profile_screen_open_link_a_device()))
	{
		guide_open();
		message_set(row == _device_row_devices ? "Your devices aren't loaded yet." :
			"Sign in on this device first.");
	}
}

/* ---------- SIGN IN: Quick Connect's code, shown here */

static void sign_in_start(void)
{
	browser_connect_start(NULL);
	browser_connect_get(&guide.connect);
	guide.signing_in = TRUE;
}

static void sign_in_stop(void)
{
	if (guide.signing_in)
		browser_connect_stop();
	guide.signing_in = FALSE;
}

/* the section shows SIGN IN (FRIENDS, PARTY, RECENT) */
static boolean signing_in_here(void)
{
	return guide.section != SOCIAL_SECTION_SETTINGS && guide.section != SOCIAL_SECTION_PLAYERS_HERE;
}

/* SIGN IN's A: a code, the player's yes, or a new code after one ran out */
static void sign_in_activate(void)
{
	struct browser_connect const *connect = &guide.connect;

	if (!guide.signing_in)
		sign_in_start();
	else if (connect->state == BROWSER_CONNECT_CONFIRM && !connect->answered)
	{
		if (system_milliseconds() - guide.confirm_shown >= CONFIRM_SETTLE)
			browser_connect_answer(TRUE);
	}
	else if (connect->state == BROWSER_CONNECT_EXPIRED || connect->state == BROWSER_CONNECT_DECLINED ||
		connect->state == BROWSER_CONNECT_FAILED)
	{
		sign_in_start();
	}
	else if (connect->state == BROWSER_CONNECT_CONNECTED)
	{
		sign_in_stop();
		browser_profile_load(TRUE);
	}
}

/* ---------- FRIENDS: the site's friends (browser.c polls them) */

static boolean invite_dismissed(char const *id)
{
	short index;

	for (index = 0; index < SOCIAL_MAXIMUM_INVITES; index++)
	{
		if (guide.dismissed[index][0] && !strcmp(guide.dismissed[index], id))
			return TRUE;
	}
	return FALSE;
}

static short invites_waiting(void)
{
	short index, count = 0;

	for (index = 0; index < friends_state.invite_count; index++)
		count += !invite_dismissed(friends_state.invites[index].id);
	return count;
}

/* signed in to Delta, as the site says */
static boolean friends_signed_in(void)
{
	return guide.linked && guide.friends_status != BROWSER_SOCIAL_UNLINKED;
}

/* what waits: invites not dismissed, and friend requests */
static short friends_waiting(void)
{
	if (!guide.friends_loaded || guide.friends_status == BROWSER_SOCIAL_UNLINKED ||
		guide.friends_status == BROWSER_SOCIAL_NONE)
	{
		return 0;
	}
	return (short)(invites_waiting() + friends_state.request_count);
}

/* an item's key: an invite's ID, a player's handle */
static char const *item_key(struct friend_item const *item)
{
	switch (item->kind)
	{
	case _item_invite: return friends_state.invites[item->index].id;
	case _item_friend: return friends_state.friends[item->index].handle;
	case _item_request: return friends_state.requests[item->index].handle;
	default: return "";
	}
}

/* the handle an item's actions go to */
static char const *item_handle(struct friend_item const *item)
{
	return item->kind == _item_invite ? friends_state.invites[item->index].from : item_key(item);
}

static boolean item_selectable(short index)
{
	return index >= 0 && index < guide.item_count && guide.items[index].kind != _item_heading;
}

static void item_add(short kind, short index)
{
	if (guide.item_count >= MAXIMUM_FRIEND_ITEMS)
		return;
	guide.items[guide.item_count].kind = kind;
	guide.items[guide.item_count].index = index;
	guide.item_count++;
}

static void chosen_remember(void)
{
	short row = guide.row[SOCIAL_SECTION_FRIENDS];

	if (row < 0 || row >= guide.item_count)
		return;
	guide.chosen_kind = guide.items[row].kind;
	social_copy_text(guide.chosen_key, sizeof(guide.chosen_key), item_key(&guide.items[row]));
}

/* the items from the state, the chosen one kept where it still is */
static void items_build(void)
{
	short index, online = 0, offline = 0, found = NONE;
	short *row = &guide.row[SOCIAL_SECTION_FRIENDS];

	guide.item_count = 0;
	if (invites_waiting())
	{
		item_add(_item_heading, _heading_invites);
		for (index = 0; index < friends_state.invite_count; index++)
		{
			if (!invite_dismissed(friends_state.invites[index].id))
				item_add(_item_invite, index);
		}
	}
	for (index = 0; index < friends_state.friend_count; index++)
	{
		if (friends_state.friends[index].online)
			online++;
		else
			offline++;
	}
	if (online)
		item_add(_item_heading, _heading_online);
	for (index = 0; index < friends_state.friend_count; index++)
	{
		if (friends_state.friends[index].online)
			item_add(_item_friend, index);
	}
	if (offline)
	{
		item_add(_item_offline, offline);
		for (index = 0; guide.offline_open && index < friends_state.friend_count; index++)
		{
			if (!friends_state.friends[index].online)
				item_add(_item_friend, index);
		}
	}
	if (friends_state.request_count)
	{
		item_add(_item_heading, _heading_requests);
		for (index = 0; index < friends_state.request_count; index++)
			item_add(_item_request, index);
	}
	/* (the chosen item, where it is now; else the row where it was) */
	for (index = 0; index < guide.item_count && found == NONE; index++)
	{
		if (item_selectable(index) && guide.items[index].kind == guide.chosen_kind &&
			!strcmp(item_key(&guide.items[index]), guide.chosen_key))
			found = index;
	}
	if (found != NONE)
		*row = found;
	else
	{
		guide.options = FALSE;
		*row = (short)PIN(*row, 0, MAX(0, guide.item_count - 1));
		while (*row > 0 && !item_selectable(*row))
			(*row)--;
		/* (the first row a heading: the row under it) */
		while (*row < guide.item_count - 1 && !item_selectable(*row))
			(*row)++;
		chosen_remember();
	}
}

/* this copy's game can take a friend (in a game on Delta with an invite) */
static boolean can_invite(void)
{
	char invite[160];

	if (!in_game() || !network_game_get_game() || !delta_peer_game_on_delta())
		return FALSE;
	if (!p2p_hosting_invite(invite, sizeof(invite)) && !p2p_joined_invite(invite, sizeof(invite)))
		return FALSE;
	return social_presence_invite_valid(invite) != 0;
}

/* an item's options (_option_*): their count */
static short item_options(struct friend_item const *item, short *options)
{
	short count = 0;

	switch (item->kind)
	{
	case _item_friend:
		if (friends_state.friends[item->index].playing)
			options[count++] = _option_join;
		if (can_invite())
			options[count++] = _option_invite;
		options[count++] = _option_remove;
		options[count++] = _option_block;
		break;
	case _item_invite:
		options[count++] = _option_join;
		options[count++] = _option_dismiss;
		break;
	case _item_request:
		options[count++] = _option_accept;
		options[count++] = _option_decline;
		break;
	}
	return count;
}

static boolean item_has_option(struct friend_item const *item, short option)
{
	short options[MAXIMUM_OPTIONS], count = item_options(item, options), index;

	for (index = 0; index < count; index++)
	{
		if (options[index] == option)
			return TRUE;
	}
	return FALSE;
}

static char const *option_label(short option, boolean armed)
{
	switch (option)
	{
	case _option_join: return "JOIN";
	case _option_invite: return "INVITE TO MY GAME";
	case _option_remove: return armed ? "CONFIRM REMOVE" : "REMOVE";
	case _option_block: return armed ? "CONFIRM BLOCK" : "BLOCK";
	case _option_accept: return "ACCEPT";
	case _option_decline: return "DECLINE";
	default: return "DISMISS";
	}
}

static struct friend_item const *chosen_item(void)
{
	short row = guide.row[SOCIAL_SECTION_FRIENDS];

	return row >= 0 && row < guide.item_count ? &guide.items[row] : NULL;
}

static void message_set(char const *text)
{
	social_copy_text(guide.message, sizeof(guide.message), text);
	guide.message_time = system_milliseconds();
}

static void friend_act(short action, char const *handle)
{
	struct browser_social_result result;

	if (guide.acting)
		return;
	if (!browser_social_act(action, handle))
		return;
	browser_social_result(&result);
	guide.acting = TRUE;
	guide.action_serial = result.serial;
	guide.message[0] = 0;
}

static void invite_dismiss(char const *id)
{
	social_copy_text(guide.dismissed[guide.dismissed_next], SOCIAL_INVITE_ID_SIZE, id);
	guide.dismissed_next = (short)((guide.dismissed_next + 1) % SOCIAL_MAXIMUM_INVITES);
	if (guide.toast_kind == _toast_invite && !strcmp(guide.toast_key, id))
		guide.toast_shown = FALSE;
	guide.options = FALSE;
	items_build();
}

/* an option of the chosen item */
static void option_run(short option)
{
	struct friend_item const *item = chosen_item();

	if (!item || !item_has_option(item, option))
		return;
	/* (REMOVE and BLOCK take a second press) */
	if ((option == _option_remove || option == _option_block) && guide.armed != option)
	{
		guide.armed = option;
		return;
	}
	guide.armed = NONE;
	switch (option)
	{
	case _option_join: friend_act(BROWSER_SOCIAL_JOIN, item_handle(item)); break;
	case _option_invite: friend_act(BROWSER_SOCIAL_INVITE, item_handle(item)); break;
	case _option_remove: friend_act(BROWSER_SOCIAL_REMOVE, item_handle(item)); break;
	case _option_block: friend_act(BROWSER_SOCIAL_BLOCK, item_handle(item)); break;
	case _option_accept: friend_act(BROWSER_SOCIAL_ACCEPT, item_handle(item)); break;
	case _option_decline: friend_act(BROWSER_SOCIAL_DECLINE, item_handle(item)); break;
	case _option_dismiss: invite_dismiss(item_key(item)); break;
	}
}

/* A on FRIENDS: the chosen item's options, or the offline friends shown */
static void friends_activate(void)
{
	struct friend_item const *item = chosen_item();
	short options[MAXIMUM_OPTIONS], count;

	if (!item)
		return;
	if (guide.options)
	{
		count = item_options(item, options);
		if (guide.option >= 0 && guide.option < count)
			option_run(options[guide.option]);
		return;
	}
	if (item->kind == _item_offline)
	{
		guide.offline_open = !guide.offline_open;
		items_build();
		return;
	}
	if (item_options(item, options) > 0)
	{
		guide.options = TRUE;
		guide.option = 0;
		guide.armed = NONE;
	}
}

static void friends_move(short step)
{
	short *row = &guide.row[SOCIAL_SECTION_FRIENDS];
	short next = *row;
	struct friend_item const *item = chosen_item();

	if (guide.options && item)
	{
		short options[MAXIMUM_OPTIONS], count = item_options(item, options);

		guide.option = (short)PIN(guide.option + step, 0, MAX(0, count - 1));
		guide.armed = NONE;
		return;
	}
	do
		next = (short)(next + step);
	while (next > 0 && next < guide.item_count && !item_selectable(next));
	if (item_selectable(next))
	{
		*row = next;
		guide.armed = NONE;
		guide.message[0] = 0;
		chosen_remember();
	}
}

/* the friends' name a toast shows (none in streamer mode) */
static void toast_from(short kind, char const *key, char const *title, char const *line, char const *name)
{
	social_copy_text(guide.toast_title, sizeof(guide.toast_title), title);
	social_copy_text(guide.toast_line, sizeof(guide.toast_line), line);
	guide.toast_initial[0] = 0;
	if (name && name[0] && !(name[0] & 0x80))
	{
		guide.toast_initial[0] = (char)(name[0] >= 'a' && name[0] <= 'z' ? name[0] - 'a' + 'A' : name[0]);
		guide.toast_initial[1] = 0;
	}
	guide.toast_kind = kind;
	social_copy_text(guide.toast_key, sizeof(guide.toast_key), key);
	guide.toast_time = system_milliseconds();
	guide.toast_shown = TRUE;
}

/* the toasts for what is new: an invite, a request (not those already
waiting at the first state), a friend online (guide.notify_online, menus) */
static void friends_news(boolean first)
{
	struct social_news news;
	boolean streamer = config_boolean("chat.streamer_mode") != 0;
	char title[96], line[96];

	social_news(&friends_state, &friends_fresh, &news);
	if (news.invite >= 0 && !invite_dismissed(friends_fresh.invites[news.invite].id))
	{
		struct social_invite const *invite = &friends_fresh.invites[news.invite];
		char const *name = social_name(invite->display, invite->from);

		if (streamer)
			snprintf(title, sizeof(title), "Game invite");
		else
			snprintf(title, sizeof(title), "%s invited you", name);
		if (social_game_line(&invite->game, line, sizeof(line)) < 0)
			snprintf(line, sizeof(line), "Game invite");
		toast_from(_toast_invite, invite->id, title, line, streamer ? "" : name);
		platform_log("Guide: a new invite");
	}
	else if (news.request >= 0 && !first)
	{
		struct social_request const *request = &friends_fresh.requests[news.request];
		char const *name = social_name(request->display, request->handle);

		toast_from(_toast_request, request->handle, streamer ? "Friend request" : name, streamer ? "" : "Friend request",
			streamer ? "" : name);
		platform_log("Guide: a new friend request");
	}
	else if (news.online >= 0 && !first && !in_game() && config_boolean("guide.notify_online"))
	{
		struct social_friend const *buddy = &friends_fresh.friends[news.online];
		char const *name = social_name(buddy->display, buddy->handle);

		social_friend_status(buddy, line, sizeof(line));
		toast_from(_toast_friend, buddy->handle, streamer ? "Friend online" : name, line, streamer ? "" : name);
	}
}

/* each frame: where the player is, told to the browser thread; a new
state read, its toasts; an action's answer */
static void friends_update(void)
{
	unsigned long now = system_milliseconds();
	int serial = guide.friends_serial;
	struct browser_social_result result;

	if (!guide.where_time || now - guide.where_time >= WHERE_INTERVAL)
	{
		guide.where_time = now ? now : 1;
		browser_social_where(in_game(), guide.open);
	}
	guide.friends_status = browser_social_get(&friends_fresh, &serial);
	if (serial != guide.friends_serial)
	{
		boolean first = !guide.friends_loaded;

		guide.friends_serial = serial;
		if (guide.friends_status == BROWSER_SOCIAL_LOADED)
		{
			friends_news(first);
			guide.friends_loaded = TRUE;
		}
		else if (guide.friends_status == BROWSER_SOCIAL_NONE || guide.friends_status == BROWSER_SOCIAL_UNLINKED)
			guide.friends_loaded = FALSE;
		friends_state = friends_fresh;
		items_build();
	}
	if (!guide.acting)
		return;
	browser_social_result(&result);
	if (result.serial != guide.action_serial || result.state == BROWSER_SOCIAL_BUSY)
		return;
	guide.acting = FALSE;
	if (result.state == BROWSER_SOCIAL_REFUSED)
	{
		message_set(result.message);
		return;
	}
	if (result.action == BROWSER_SOCIAL_JOIN)
	{
		char link[192];

		if (browser_social_take_join(link, sizeof(link)))
		{
			/* (as any invite link: Internet play joins it) */
			guide_close();
			platform_log("Guide: joining a friend's game");
			p2p_join_invite(link);
		}
		return;
	}
	if (result.action == BROWSER_SOCIAL_INVITE)
		message_set("Invite sent");
	else
		guide.options = FALSE;
}

/* the toast's item chosen on FRIENDS, its options open */
static boolean toast_go(void)
{
	short index;
	short kind = guide.toast_kind == _toast_invite ? _item_invite : guide.toast_kind == _toast_request ? _item_request :
		guide.toast_kind == _toast_friend ? _item_friend : NONE;

	if (kind == NONE)
		return FALSE;
	guide.section = SOCIAL_SECTION_FRIENDS;
	guide.first_row = 0;
	for (index = 0; index < guide.item_count; index++)
	{
		if (guide.items[index].kind == kind && !strcmp(item_key(&guide.items[index]), guide.toast_key))
		{
			guide.row[SOCIAL_SECTION_FRIENDS] = index;
			chosen_remember();
			guide.options = kind != _item_friend;
			guide.option = 0;
			guide.armed = NONE;
			break;
		}
	}
	guide.toast_shown = FALSE;
	return TRUE;
}

/* ---------- the sections' rows */

static short section_rows(short section)
{
	switch (section)
	{
	case SOCIAL_SECTION_PLAYERS_HERE:
		return (short)guide.players.count;
	case SOCIAL_SECTION_SETTINGS:
		return settings_rows();
	case SOCIAL_SECTION_FRIENDS:
		if (friends_signed_in())
			return guide.friends_loaded ? guide.item_count : 0;
		return sign_in_section() != NONE ? 1 : 0;
	default:
		/* (SIGN IN, without a Delta profile) */
		return !guide.linked && sign_in_section() != NONE ? 1 : 0;
	}
}

static void section_go(short section)
{
	guide.section = (short)((section + NUMBER_OF_SOCIAL_SECTIONS) % NUMBER_OF_SOCIAL_SECTIONS);
	guide.first_row = 0;
	guide.options = FALSE;
	guide.armed = NONE;
}

/* A on the chosen row */
static void activate(void)
{
	short row = guide.row[guide.section];

	switch (guide.section)
	{
	case SOCIAL_SECTION_PLAYERS_HERE:
		player_mute_toggle(row);
		return;
	case SOCIAL_SECTION_SETTINGS:
		if (row >= NUMBER_OF_SETTINGS)
			device_row_activate((short)(row - NUMBER_OF_SETTINGS));
		else
			setting_change(row, 1);
		return;
	case SOCIAL_SECTION_FRIENDS:
		if (friends_signed_in())
		{
			if (guide.friends_loaded)
				friends_activate();
			return;
		}
		break;
	}
	/* (Quick Connect's code, shown here) */
	if (!friends_signed_in() && sign_in_section() != NONE)
		sign_in_activate();
}

static void move(short step)
{
	short rows = section_rows(guide.section);
	short *row = &guide.row[guide.section];

	if (rows <= 0)
		return;
	if (guide.section == SOCIAL_SECTION_FRIENDS && friends_signed_in())
	{
		friends_move(step);
		return;
	}
	*row = (short)PIN(*row + step, 0, rows - 1);
}

/* what was just pressed (and a d-pad held, again and again) */
static unsigned int presses(void)
{
	unsigned int held = platform_guide_held();
	unsigned int pressed = held & ~guide.held;
	unsigned int directions = held & (GUIDE_INPUT_UP | GUIDE_INPUT_DOWN | GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT);
	unsigned long now = system_milliseconds();

	if (directions && directions == guide.repeat_held)
	{
		if (now - guide.repeat_time >= REPEAT_DELAY)
		{
			pressed |= directions;
			guide.repeat_time = now - REPEAT_DELAY + REPEAT_INTERVAL;
		}
	}
	else
	{
		guide.repeat_held = directions;
		guide.repeat_time = now;
	}
	guide.held = held;
	return pressed;
}

/* ---------- drawing */

/* the player's dot */
static unsigned int dot_color(int state)
{
	switch (state)
	{
	case SOCIAL_STATE_IN_GAME: return COLOR_DOT_GAME;
	case SOCIAL_STATE_ONLINE: return COLOR_DOT_ONLINE;
	case SOCIAL_STATE_AWAY: return COLOR_DOT_AWAY;
	default: return COLOR_DOT_OFFLINE;
	}
}

/* the ui.map's plasma map, read once it is in the texture cache (none:
the overlay's own noise) */
static void plasma_look(void)
{
	long tag;

	if (guide.plasma_tries >= PLASMA_TRIES || (!main_menu_is_active() && !game_map_loaded()))
		return;
	guide.plasma_tries++;
	tag = interface_get_tag_index(_interface_bitmap_iface_map3);
	if (tag != NONE)
	{
		struct bitmap_group *group = bitmap_group_get(tag);

		if (group && group->bitmaps.count > 0)
		{
			struct bitmap_data *bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, 0, struct bitmap_data);
			void *hardware_format = _texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE);

			if (hardware_format && ui_overlay_plasma_from_texture(hardware_format))
			{
				guide.plasma_tries = PLASMA_TRIES;
				return;
			}
		}
	}
	if (guide.plasma_tries == PLASMA_TRIES)
		platform_log("Guide: no plasma map in ui.map; drawn from the Guide's own");
}

static void render_card(void)
{
	ui_style_card(CONTENT_LEFT, CONTENT_RIGHT, HEADER_Y, guide.name, guide.status, guide.color, "GUIDE");
}

static void render_tabs(void)
{
	char const *names[NUMBER_OF_SOCIAL_SECTIONS];
	float starts[NUMBER_OF_SOCIAL_SECTIONS], ends[NUMBER_OF_SOCIAL_SECTIONS];
	short section;

	for (section = 0; section < NUMBER_OF_SOCIAL_SECTIONS; section++)
		names[section] = social_section_name(section);
	ui_style_tabs(CONTENT_LEFT, CONTENT_RIGHT, TABS_Y, TABS_HEIGHT, names, NUMBER_OF_SOCIAL_SECTIONS, guide.section,
		"Q", "E", starts, ends);
	for (section = 0; section < NUMBER_OF_SOCIAL_SECTIONS; section++)
		target_add(starts[section], TABS_Y, ends[section], TABS_Y + TABS_HEIGHT, _target_tab, section);
}

/* a player's row: their dot, tag, status line and platform's logo */
static void render_row(struct social_row const *row, float x, float y, float width, boolean on, short index)
{
	ui_style_row(x, y, width, ROW_HEIGHT, on);
	if (row->state >= 0)
		ui_overlay_rect(x + 9, y + ROW_HEIGHT * 0.5f - 3, 6, 6, 3, dot_color(row->state));
	if (row->status[0])
	{
		ui_style_text(UI_TEXT_NAME, x + 22, y + 3, UI_ALIGN_LEFT, row->tag);
		ui_style_text(row->muted ? UI_TEXT_WARNING : UI_TEXT_STATUS, x + 22, y + 17, UI_ALIGN_LEFT, row->status);
	}
	else
		ui_style_text(UI_TEXT_NAME, x + 22, y + (ROW_HEIGHT - ui_style_text_size(UI_TEXT_NAME)) * 0.5f, UI_ALIGN_LEFT,
			row->tag);
	if (row->platform > 0)
		ui_overlay_platform(row->platform, 11.0f, x + width - 14, y + (ROW_HEIGHT - 11.0f) * 0.5f, COLOR_LABEL, 0);
	target_add(x, y, x + width, y + ROW_HEIGHT, _target_row, index);
}

/* a button of the detail pane (an action) */
static void render_action(char const *label, float x, float y, float width, boolean on, unsigned int text,
	short index)
{
	ui_style_row(x, y, width, 24, on);
	ui_overlay_text(UI_FONT_MENU, 10.0f, x + 10, y + 7, UI_ALIGN_LEFT, text, label);
	target_add(x, y, x + width, y + 24, _target_action, index);
}

static void render_players(void)
{
	short count = (short)guide.players.count, visible, index;
	short chosen = guide.row[SOCIAL_SECTION_PLAYERS_HERE];
	char heading[48];
	float y = BODY_Y + 16;

	if (!count)
		return;
	snprintf(heading, sizeof(heading), "PLAYERS (%d)", count);
	ui_style_text(UI_TEXT_HEADING, CONTENT_LEFT, BODY_Y, UI_ALIGN_LEFT, heading);
	visible = (short)((BODY_BOTTOM - y + ROW_GAP) / (ROW_HEIGHT + ROW_GAP));
	if (chosen < guide.first_row)
		guide.first_row = chosen;
	else if (chosen >= guide.first_row + visible)
		guide.first_row = (short)(chosen - visible + 1);
	for (index = guide.first_row; index < count && index < guide.first_row + visible; index++)
	{
		render_row(&guide.players.rows[index], CONTENT_LEFT, y, LIST_WIDTH, index == chosen, index);
		y += ROW_HEIGHT + ROW_GAP;
	}
	/* the chosen player, and what can be done */
	if (chosen >= 0 && chosen < count)
	{
		struct social_row const *row = &guide.players.rows[chosen];
		float x = DETAIL_LEFT, width = CONTENT_RIGHT - DETAIL_LEFT;

		ui_style_glass(x, BODY_Y, width, BODY_BOTTOM - BODY_Y, 8, ui_style_color(UI_COLOR_PANE), COLOR_ROW_EDGE);
		ui_style_text(UI_TEXT_TITLE, x + 12, BODY_Y + 12, UI_ALIGN_LEFT, row->tag);
		if (row->platform > 0)
			ui_overlay_platform(row->platform, 13.0f, x + width - 18, BODY_Y + 12, COLOR_LABEL, 0);
		if (row->status[0])
			ui_style_text(row->muted ? UI_TEXT_WARNING : UI_TEXT_LINE, x + 12, BODY_Y + 32, UI_ALIGN_LEFT, row->status);
		if (row->actions & SOCIAL_ACTION_MUTE)
			render_action(row->muted ? "UNMUTE" : "MUTE", x + 12, BODY_Y + 54, width - 24, TRUE, COLOR_VALUE, 0);
	}
}

static float item_height(short kind)
{
	return kind == _item_heading ? HEADING_HEIGHT : ROW_HEIGHT + ROW_GAP;
}

/* the chosen item in view (a heading over it too, where it fits) */
static void friends_scroll(void)
{
	short chosen = guide.row[SOCIAL_SECTION_FRIENDS];
	float room = BODY_BOTTOM - BODY_Y + ROW_GAP;

	if (chosen < 0 || chosen >= guide.item_count)
		return;
	if (guide.first_row > chosen)
		guide.first_row = chosen;
	if (guide.first_row == chosen && chosen > 0 && guide.items[chosen - 1].kind == _item_heading)
		guide.first_row = (short)(chosen - 1);
	for (;;)
	{
		float used = 0;
		short index;

		for (index = guide.first_row; index <= chosen; index++)
			used += item_height(guide.items[index].kind);
		if (used <= room || guide.first_row >= chosen)
			break;
		guide.first_row++;
	}
}

/* an item's row as the list draws it */
static void item_row(struct friend_item const *item, struct social_row *row)
{
	csmemset(row, 0, sizeof(*row));
	switch (item->kind)
	{
	case _item_invite:
	{
		struct social_invite const *invite = &friends_state.invites[item->index];

		social_copy_text(row->tag, sizeof(row->tag), social_name(invite->display, invite->from));
		if (social_game_line(&invite->game, row->status, sizeof(row->status)) < 0)
			social_copy_text(row->status, sizeof(row->status), "Invited you");
		row->state = SOCIAL_STATE_IN_GAME;
		break;
	}
	case _item_friend:
	{
		struct social_friend const *buddy = &friends_state.friends[item->index];

		social_copy_text(row->tag, sizeof(row->tag), social_name(buddy->display, buddy->handle));
		social_friend_status(buddy, row->status, sizeof(row->status));
		row->state = buddy->playing ? SOCIAL_STATE_IN_GAME : buddy->online ? SOCIAL_STATE_ONLINE : SOCIAL_STATE_OFFLINE;
		break;
	}
	case _item_request:
	{
		struct social_request const *request = &friends_state.requests[item->index];

		social_copy_text(row->tag, sizeof(row->tag), social_name(request->display, request->handle));
		social_copy_text(row->status, sizeof(row->status), "Friend request");
		/* (no dot: not a friend yet) */
		row->state = -1;
		break;
	}
	}
}

/* the chosen item's panel: its name, status, a line about its game, then
its options */
static void render_friend_detail(struct friend_item const *item)
{
	struct social_row row;
	float x = DETAIL_LEFT, width = CONTENT_RIGHT - DETAIL_LEFT, y;
	char line[SOCIAL_STATUS_SIZE];
	short options[MAXIMUM_OPTIONS], count, index;
	boolean busy;

	line[0] = 0;
	ui_style_glass(x, BODY_Y, width, BODY_BOTTOM - BODY_Y, 8, ui_style_color(UI_COLOR_PANE), COLOR_ROW_EDGE);
	if (item->kind == _item_offline)
	{
		snprintf(line, sizeof(line), "OFFLINE (%d)", item->index);
		ui_style_text(UI_TEXT_HEADING, x + 12, BODY_Y + 14, UI_ALIGN_LEFT, line);
		return;
	}
	item_row(item, &row);
	ui_style_text(UI_TEXT_TITLE, x + 12, BODY_Y + 12, UI_ALIGN_LEFT, row.tag);
	if (row.status[0])
		ui_style_text(UI_TEXT_LINE, x + 12, BODY_Y + 32, UI_ALIGN_LEFT, row.status);
	switch (item->kind)
	{
	case _item_invite:
		social_copy_text(line, sizeof(line), friends_state.invites[item->index].game.name);
		break;
	case _item_friend:
	{
		struct social_friend const *buddy = &friends_state.friends[item->index];
		time_t since = (time_t)buddy->since;
		struct tm *date = buddy->since > 0 ? localtime(&since) : NULL;

		if (buddy->playing && buddy->game.listed)
			social_copy_text(line, sizeof(line), buddy->game.name);
		else if (date)
			strftime(line, sizeof(line), "Friends since %b %d", date);
		break;
	}
	}
	if (line[0])
		ui_style_text(UI_TEXT_LABEL, x + 12, BODY_Y + 48, UI_ALIGN_LEFT, line);
	count = item_options(item, options);
	busy = guide.acting;
	y = BODY_Y + 68;
	for (index = 0; index < count; index++)
	{
		boolean on = guide.options && index == guide.option;

		ui_style_row(x + 12, y, width - 24, OPTION_HEIGHT, on);
		ui_overlay_text(UI_FONT_MENU, 10.0f, x + 22, y + 7, UI_ALIGN_LEFT, busy ? fade(COLOR_VALUE, 0.5f) : COLOR_VALUE,
			option_label(options[index], guide.armed == options[index]));
		target_add(x + 12, y, x + width - 12, y + OPTION_HEIGHT, _target_option, index);
		y += OPTION_HEIGHT + OPTION_GAP;
	}
	/* (what the last action came to: a refusal's line, "Invite sent") */
	if (busy)
		ui_style_text(UI_TEXT_LINE, x + 12, y + 4, UI_ALIGN_LEFT, "Sending");
	else if (guide.message[0] && system_milliseconds() - guide.message_time < MESSAGE_TIME)
		ui_style_text(UI_TEXT_WARNING, x + 12, y + 4, UI_ALIGN_LEFT, guide.message);
}

static void render_friends(void)
{
	short index, chosen = guide.row[SOCIAL_SECTION_FRIENDS];
	float y = BODY_Y, center = (CONTENT_LEFT + CONTENT_RIGHT) * 0.5f;
	char heading[48];
	int online = social_online_count(&friends_state);

	if (!guide.friends_loaded)
	{
		ui_style_text(UI_TEXT_LINE, center, (BODY_Y + BODY_BOTTOM) * 0.5f - 6, UI_ALIGN_CENTER,
			guide.friends_status == BROWSER_SOCIAL_FAILED ? "Can't reach the site" : "Loading");
		return;
	}
	friends_scroll();
	for (index = guide.first_row; index < guide.item_count; index++)
	{
		struct friend_item const *item = &guide.items[index];
		float height = item_height(item->kind);

		if (y + height - (item->kind == _item_heading ? 0 : ROW_GAP) > BODY_BOTTOM)
			break;
		if (item->kind == _item_heading)
		{
			if (item->index == _heading_invites)
				snprintf(heading, sizeof(heading), "INVITES (%d)", invites_waiting());
			else if (item->index == _heading_online)
				snprintf(heading, sizeof(heading), "ONLINE (%d OF %d)", online, friends_state.friend_count);
			else
				snprintf(heading, sizeof(heading), "REQUESTS (%d)", friends_state.request_count);
			ui_style_text(UI_TEXT_HEADING, CONTENT_LEFT, y + 2, UI_ALIGN_LEFT, heading);
		}
		else if (item->kind == _item_offline)
		{
			ui_style_row(CONTENT_LEFT, y, LIST_WIDTH, ROW_HEIGHT, index == chosen && !guide.options);
			snprintf(heading, sizeof(heading), "OFFLINE (%d)", item->index);
			ui_overlay_text(UI_FONT_MENU, 9.5f, CONTENT_LEFT + 12, y + (ROW_HEIGHT - 9.5f) * 0.5f, UI_ALIGN_LEFT,
				COLOR_LABEL, heading);
			ui_overlay_text(UI_FONT_MENU, 11.0f, CONTENT_LEFT + LIST_WIDTH - 14, y + (ROW_HEIGHT - 11.0f) * 0.5f,
				UI_ALIGN_RIGHT, COLOR_LABEL, guide.offline_open ? "-" : "+");
			target_add(CONTENT_LEFT, y, CONTENT_LEFT + LIST_WIDTH, y + ROW_HEIGHT, _target_row, index);
		}
		else
		{
			struct social_row row;

			item_row(item, &row);
			render_row(&row, CONTENT_LEFT, y, LIST_WIDTH, index == chosen && !guide.options, index);
		}
		y += height;
	}
	if (!guide.item_count)
		ui_style_text(UI_TEXT_LINE, CONTENT_LEFT + LIST_WIDTH * 0.5f, y + 14, UI_ALIGN_CENTER, "No friends yet");
	if (chosen >= 0 && chosen < guide.item_count)
		render_friend_detail(&guide.items[chosen]);
}

static void render_settings(void)
{
	short setting;
	float y = BODY_Y;

	for (setting = 0; setting < NUMBER_OF_SETTINGS; setting++)
	{
		boolean on = setting == guide.row[SOCIAL_SECTION_SETTINGS];
		char value[32];
		float width = CONTENT_RIGHT - CONTENT_LEFT, value_right = CONTENT_RIGHT - 14;

		setting_value(setting, value, sizeof(value));
		ui_style_row(CONTENT_LEFT, y, width, SETTING_HEIGHT, on);
		ui_overlay_text(UI_FONT_MENU, 10.0f, CONTENT_LEFT + 12, y + 8, UI_ALIGN_LEFT, COLOR_SETTING,
			setting_labels[setting]);
		/* (the value between its arrows, as the menus' settings have it) */
		ui_overlay_text(UI_FONT_MENU, 10.0f, value_right, y + 8, UI_ALIGN_RIGHT, COLOR_VALUE, "\xE2\x80\xBA");
		ui_overlay_text(UI_FONT_MENU, 10.0f, value_right - 12, y + 8, UI_ALIGN_RIGHT, COLOR_VALUE, value);
		ui_overlay_text(UI_FONT_MENU, 10.0f, value_right - 18 - ui_overlay_text_width(UI_FONT_MENU, 10.0f, value),
			y + 8, UI_ALIGN_RIGHT, COLOR_VALUE, "\xE2\x80\xB9");
		target_add(CONTENT_LEFT, y, CONTENT_RIGHT, y + SETTING_HEIGHT, _target_setting, setting);
		y += SETTING_HEIGHT + ROW_GAP;
	}
	for (setting = NUMBER_OF_SETTINGS; setting < settings_rows(); setting++)
	{
		boolean on = setting == guide.row[SOCIAL_SECTION_SETTINGS];
		float width = CONTENT_RIGHT - CONTENT_LEFT;

		ui_style_row(CONTENT_LEFT, y, width, SETTING_HEIGHT, on);
		ui_overlay_text(UI_FONT_MENU, 10.0f, CONTENT_LEFT + 12, y + 8, UI_ALIGN_LEFT, COLOR_SETTING,
			device_row_labels[setting - NUMBER_OF_SETTINGS]);
		ui_overlay_text(UI_FONT_MENU, 10.0f, CONTENT_RIGHT - 14, y + 8, UI_ALIGN_RIGHT, COLOR_VALUE, "\xE2\x80\xBA");
		target_add(CONTENT_LEFT, y, CONTENT_RIGHT, y + SETTING_HEIGHT, _target_setting, setting);
		y += SETTING_HEIGHT + ROW_GAP;
	}
	if (guide.message[0] && system_milliseconds() - guide.message_time < MESSAGE_TIME)
		ui_style_text(UI_TEXT_LINE, CONTENT_LEFT + 12, y + 4, UI_ALIGN_LEFT, guide.message);
}

/* FRIENDS, PARTY and RECENT: SIGN IN without a Delta profile; nothing yet
with one (the site's friends come next) */
static void render_coming(void)
{
	float center = (CONTENT_LEFT + CONTENT_RIGHT) * 0.5f, width = 140;
	float y = (BODY_Y + BODY_BOTTOM) * 0.5f - 24;

	if (friends_signed_in() || sign_in_section() == NONE)
		return;
	if (guide.signing_in)
	{
		struct browser_connect const *connect = &guide.connect;
		char text[160];

		switch (connect->state)
		{
		case BROWSER_CONNECT_WAITING:
			snprintf(text, sizeof(text), "Enter this code at %s", connect->page);
			ui_style_text(UI_TEXT_LINE, center, y - 18, UI_ALIGN_CENTER, text);
			ui_style_row(center - 110, y + 2, 220, 50, TRUE);
			ui_overlay_text_glow(UI_FONT_MENU, 26.0f, center, y + 10, UI_ALIGN_CENTER, 0xFFFFFFFF, COLOR_PROMPT_GLOW,
				connect->code);
			snprintf(text, sizeof(text), "Expires in %d:%02d", connect->seconds / 60, connect->seconds % 60);
			ui_style_text(UI_TEXT_LINE, center, y + 62, UI_ALIGN_CENTER, text);
			break;
		case BROWSER_CONNECT_CONFIRM:
			snprintf(text, sizeof(text), connect->previous[0] ? "Sign in as @%s (from @%s)?" : "Sign in as @%s?",
				connect->handle, connect->previous);
			ui_overlay_text_glow(UI_FONT_MENU, 14.0f, center, y + 10, UI_ALIGN_CENTER, 0xFFFFFFFF, COLOR_PROMPT_GLOW, text);
			if (connect->answered)
				ui_style_text(UI_TEXT_LINE, center, y + 40, UI_ALIGN_CENTER, "Sending your answer\xE2\x80\xA6");
			break;
		case BROWSER_CONNECT_CONNECTED:
			snprintf(text, sizeof(text), "Signed in as @%s", connect->handle);
			ui_overlay_text_glow(UI_FONT_MENU, 14.0f, center, y + 10, UI_ALIGN_CENTER, 0xFFFFFFFF, COLOR_PROMPT_GLOW, text);
			break;
		case BROWSER_CONNECT_EXPIRED:
		case BROWSER_CONNECT_DECLINED:
		case BROWSER_CONNECT_FAILED:
			ui_style_text(UI_TEXT_LINE, center, y + 10, UI_ALIGN_CENTER,
				connect->state == BROWSER_CONNECT_EXPIRED ? "The code expired." :
				connect->state == BROWSER_CONNECT_DECLINED ? "Not signed in." : connect->message);
			break;
		default:
			ui_style_text(UI_TEXT_LINE, center, y + 10, UI_ALIGN_CENTER, "Getting a code\xE2\x80\xA6");
			break;
		}
		return;
	}
	ui_style_text(UI_TEXT_LINE, center, y, UI_ALIGN_CENTER, "Sign in to Delta to see friends");
	ui_style_row(center - width * 0.5f, y + 22, width, 26, TRUE);
	ui_overlay_text_glow(UI_FONT_MENU, 11.0f, center, y + 29, UI_ALIGN_CENTER, 0xFFFFFFFF, COLOR_PROMPT_GLOW,
		"SIGN IN");
	target_add(center - width * 0.5f, y + 22, center + width * 0.5f, y + 48, _target_sign_in, 0);
}

/* FRIENDS' shortcuts on the chosen item: Y joins, X invites */
static short shortcut_option(unsigned int button)
{
	struct friend_item const *item = chosen_item();
	short option = button == GUIDE_INPUT_Y ? _option_join : _option_invite;

	if (guide.section != SOCIAL_SECTION_FRIENDS || !friends_signed_in() || !guide.friends_loaded || guide.options ||
		!item || !item_has_option(item, option))
	{
		return NONE;
	}
	return option;
}

/* SIGN IN's A: its word as the sign-in goes (NULL: none now); B goes back
while signing in */
static char const *sign_in_word(boolean *back)
{
	if (!guide.signing_in)
		return "SIGN IN";
	*back = TRUE;
	switch (guide.connect.state)
	{
	case BROWSER_CONNECT_CONFIRM:
		return guide.connect.answered ? NULL : "YES";
	case BROWSER_CONNECT_CONNECTED:
		return "DONE";
	case BROWSER_CONNECT_EXPIRED:
	case BROWSER_CONNECT_DECLINED:
	case BROWSER_CONNECT_FAILED:
		return "NEW CODE";
	default:
		return NULL;
	}
}

static void prompt_add(struct ui_style_prompt *prompts, short *count, int button, char const *key, char const *word)
{
	prompts[*count].button = button;
	prompts[*count].key = key;
	prompts[(*count)++].word = word;
}

static void render_prompts(void)
{
	struct ui_style_prompt prompts[4];
	float starts[4], ends[4];
	char const *word = NULL;
	short count = 0, index;
	boolean back = FALSE;

	switch (guide.section)
	{
	case SOCIAL_SECTION_PLAYERS_HERE:
		if (guide.row[guide.section] < guide.players.count &&
			(guide.players.rows[guide.row[guide.section]].actions & SOCIAL_ACTION_MUTE))
		{
			word = guide.players.rows[guide.row[guide.section]].muted ? "UNMUTE" : "MUTE";
		}
		break;
	case SOCIAL_SECTION_SETTINGS:
		word = guide.row[guide.section] >= NUMBER_OF_SETTINGS ? "OPEN" : "CHANGE";
		break;
	case SOCIAL_SECTION_FRIENDS:
		if (friends_signed_in())
		{
			struct friend_item const *item = guide.friends_loaded ? chosen_item() : NULL;
			short options[MAXIMUM_OPTIONS];

			if (item && guide.options)
				word = "SELECT";
			else if (item && item->kind == _item_offline)
				word = guide.offline_open ? "HIDE" : "SHOW";
			else if (item && item_options(item, options) > 0)
				word = "SELECT";
			back = guide.options;
			break;
		}
		if (section_rows(guide.section))
			word = sign_in_word(&back);
		break;
	default:
		if (section_rows(guide.section))
			word = sign_in_word(&back);
		break;
	}
	if (word)
		prompt_add(prompts, &count, UI_BUTTON_A, "ENTER", word);
	if (shortcut_option(GUIDE_INPUT_Y) != NONE)
		prompt_add(prompts, &count, UI_BUTTON_Y, "TAB", "JOIN");
	if (shortcut_option(GUIDE_INPUT_X) != NONE)
		prompt_add(prompts, &count, UI_BUTTON_X, "M", "INVITE");
	prompt_add(prompts, &count, UI_BUTTON_B, "ESC", guide.signing_in && signing_in_here() &&
		guide.connect.state == BROWSER_CONNECT_CONFIRM && !guide.connect.answered ? "NO" : back ? "BACK" : "CLOSE");
	ui_style_prompts(CONTENT_RIGHT, PROMPTS_Y, 15.0f, prompts, count, starts, ends);
	for (index = 0; index < count; index++)
		target_add(starts[index] - 4, PROMPTS_Y - 4, ends[index] + 4, PROMPTS_Y + 19, _target_prompt, (short)prompts[index].button);
}

/* the toast on screen now: in the menus, in a game only with
guide.notify_in_game, a friend online's in the menus alone */
static boolean toast_visible(void)
{
	if (!guide.toast_shown || system_milliseconds() - guide.toast_time >= TOAST_TIME)
		return FALSE;
	if (in_game() && (!config_boolean("guide.notify_in_game") || guide.toast_kind == _toast_friend))
		return FALSE;
	return TRUE;
}

static void render_toast(void)
{
	unsigned long age = system_milliseconds() - guide.toast_time;
	float share, text_right = TOAST_X + TOAST_WIDTH - 10;

	if (!guide.toast_shown)
		return;
	if (age >= TOAST_TIME)
	{
		guide.toast_shown = FALSE;
		return;
	}
	/* (never over the loading screen) */
	if (!toast_visible())
		return;
	share = age < TOAST_FADE ? (float)age / TOAST_FADE :
		age > TOAST_TIME - TOAST_FADE ? (float)(TOAST_TIME - age) / TOAST_FADE : 1.0f;
	ui_overlay_layer(4);
	ui_style_clock((unsigned int)system_milliseconds());
	ui_style_glass(TOAST_X, TOAST_Y, TOAST_WIDTH, TOAST_HEIGHT, 8, fade(ui_style_color(UI_COLOR_GLASS_TOP), share),
		fade(COLOR_PANEL_EDGE, share));
	/* (a friend's: their initial on gold; else the player's own color) */
	if (guide.toast_kind != _toast_none)
	{
		ui_overlay_rect(TOAST_X + 8, TOAST_Y + 7, 20, 20, 4, fade(ui_style_color(UI_COLOR_EMBLEM_FRIEND), share));
		if (guide.toast_initial[0])
		{
			ui_overlay_text(UI_FONT_BOLD, 11.0f, TOAST_X + 18, TOAST_Y + 11, UI_ALIGN_CENTER, fade(ui_style_color(UI_COLOR_EMBLEM_LETTER), share),
				guide.toast_initial);
		}
		/* (the key that opens it) */
		if (ui_overlay_keyboard())
		{
			char const *key = config_string("controls.guide");

			key = key[0] ? key : "F3";
			text_right -= ui_overlay_key_width(key, 12.0f) + 6;
			ui_overlay_key(key, 12.0f, text_right + 6, TOAST_Y + 11, fade(ui_style_color(UI_COLOR_NAME), share));
		}
	}
	else
		ui_overlay_rect(TOAST_X + 8, TOAST_Y + 7, 20, 20, 4, fade(guide.color ? guide.color : 0x2F6DD0FF, share));
	ui_style_text_faded(UI_TEXT_NAME, share, TOAST_X + 36, TOAST_Y + 5, UI_ALIGN_LEFT, guide.toast_title);
	ui_style_text_faded(UI_TEXT_STATUS, share, TOAST_X + 36, TOAST_Y + 20, UI_ALIGN_LEFT, guide.toast_line);
	ui_overlay_layer(0);
}

/* ---------- public code */

boolean guide_active(void)
{
	return guide.open;
}

boolean guide_takes_player(void)
{
	return guide.open;
}

void guide_open(void)
{
	if (guide.open || !ui_overlay_available())
		return;
	guide.open = TRUE;
	guide.opened_time = system_milliseconds();
	/* (what is held as it opens is not pressed in it) */
	guide.held = 0xFFFFFFFF;
	guide.repeat_held = 0;
	guide.target_count = 0;
	platform_guide_capture(TRUE);
	event_manager_flush();
	card_update();
	players_update();
	browser_profile_load(FALSE);
	/* (its friends asked for soon) */
	guide.options = FALSE;
	guide.armed = NONE;
	guide.where_time = 0;
	friends_update();
	guide.paused_game = pauses_game();
	if (guide.paused_game)
		ui_widgets_port_pause_game(TRUE);
	platform_log("Guide: opened");
}

void guide_close(void)
{
	if (!guide.open)
		return;
	guide.open = FALSE;
	guide.target_count = 0;
	sign_in_stop();
	platform_guide_capture(FALSE);
	event_manager_flush();
	if (guide.paused_game)
		ui_widgets_port_pause_game(FALSE);
	guide.paused_game = FALSE;
}

void guide_toast(char const *title, char const *line)
{
	social_copy_text(guide.toast_title, sizeof(guide.toast_title), title);
	social_copy_text(guide.toast_line, sizeof(guide.toast_line), line);
	guide.toast_kind = _toast_none;
	guide.toast_time = system_milliseconds();
	guide.toast_shown = TRUE;
}

wchar_t const *guide_pause_label(long string_list_tag)
{
	static wchar_t label[16];
	char text[16];
	long tag = pc_menu_pause_guide_label();
	short waiting, index;

	if (tag == NONE || string_list_tag != tag || (waiting = friends_waiting()) <= 0)
		return NULL;
	snprintf(text, sizeof(text), "GUIDE %d", MIN(waiting, 99));
	for (index = 0; text[index] && index < (short)NUMBEROF(label) - 1; index++)
		label[index] = (wchar_t)text[index];
	label[index] = 0;
	return label;
}

void guide_update(boolean menu_open)
{
	friends_update();
	/* (a toast waits out the intro's video and a map's loading: its time
	starts once it can show) */
	if (guide.toast_shown && (bink_playback_active() || (!main_menu_is_active() && !game_map_loaded())))
		guide.toast_time = system_milliseconds();
	/* (debug.guide_open and debug.guide_toast: for screenshots and tests,
	once a run) */
	if (!guide.debug_open_done && config_boolean("debug.guide_open") && (main_menu_is_active() || game_map_loaded()))
	{
		guide.debug_open_done = TRUE;
		guide_open();
	}
	if (!guide.debug_toast_done && config_string("debug.guide_toast")[0] && main_menu_is_active() &&
		menu_open)
	{
		guide.debug_toast_done = TRUE;
		guide_toast(config_string("debug.guide_toast"), "Online \xC2\xB7 In the menus");
	}
	/* (F3, the Guide button, Back held in the menus: open or close; not
	over the virtual keyboard, which takes typing) */
	if (platform_guide_toggles())
	{
		if (guide.open)
			guide_close();
		else if (!virtual_keyboard_active())
		{
			/* (a toast up: its item) */
			boolean toast = toast_visible() && guide.toast_kind != _toast_none;

			guide_open();
			if (toast && guide.open)
				toast_go();
		}
	}
	if (guide.open)
	{
		/* (a game that ended under it lets go of its pause) */
		if (guide.paused_game && !pauses_game())
		{
			ui_widgets_port_pause_game(FALSE);
			guide.paused_game = FALSE;
		}
		card_update();
		players_update();
	}
}

/* what was pressed (or clicked on the prompt bar) */
static void handle(unsigned int pressed)
{
	short option;

	if (pressed & GUIDE_INPUT_B)
	{
		/* (in FRIENDS' options panel: back to the list; signing in: a no, or
		the code dropped) */
		if (guide.options)
		{
			guide.options = FALSE;
			guide.armed = NONE;
		}
		else if (guide.signing_in && signing_in_here() && guide.connect.state == BROWSER_CONNECT_CONFIRM &&
			!guide.connect.answered)
		{
			browser_connect_answer(FALSE);
		}
		else if (guide.signing_in && signing_in_here())
			sign_in_stop();
		else
			guide_close();
		return;
	}
	if (pressed & GUIDE_INPUT_LEFT_BUMPER)
		section_go((short)(guide.section - 1));
	if (pressed & GUIDE_INPUT_RIGHT_BUMPER)
		section_go((short)(guide.section + 1));
	if (pressed & GUIDE_INPUT_UP)
		move(-1);
	if (pressed & GUIDE_INPUT_DOWN)
		move(1);
	if (guide.section == SOCIAL_SECTION_SETTINGS && guide.row[guide.section] < NUMBER_OF_SETTINGS &&
		(pressed & (GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT)))
		setting_change(guide.row[guide.section], (pressed & GUIDE_INPUT_LEFT) ? -1 : 1);
	if (pressed & GUIDE_INPUT_A)
		activate();
	else if ((pressed & GUIDE_INPUT_X) && guide.section == SOCIAL_SECTION_PLAYERS_HERE)
		player_mute_toggle(guide.row[guide.section]);
	else if ((pressed & GUIDE_INPUT_Y) && (option = shortcut_option(GUIDE_INPUT_Y)) != NONE)
		option_run(option);
	else if ((pressed & GUIDE_INPUT_X) && (option = shortcut_option(GUIDE_INPUT_X)) != NONE)
		option_run(option);
}

void guide_process(void)
{
	if (!guide.open)
		return;
	if (guide.signing_in)
	{
		int before = guide.connect.state;

		browser_connect_get(&guide.connect);
		if (guide.connect.state == BROWSER_CONNECT_CONFIRM && before != BROWSER_CONNECT_CONFIRM)
			guide.confirm_shown = system_milliseconds();
		/* (signed in: the card and the friends follow) */
		if (guide.connect.state == BROWSER_CONNECT_CONNECTED && guide.linked)
			guide.signing_in = FALSE;
	}
	handle(presses());
}

void guide_pointer(struct halo_ui_pointer const *pointer)
{
	short index;

	if (!guide.open)
		return;
	if (pointer->right_clicks)
	{
		handle(GUIDE_INPUT_B);
		return;
	}
	if (!pointer->left_clicks)
		return;
	for (index = 0; index < guide.target_count; index++)
	{
		struct guide_target const *target = &guide.targets[index];

		if (pointer->click_x < target->x0 || pointer->click_x >= target->x1 || pointer->click_y < target->y0 ||
			pointer->click_y >= target->y1)
		{
			continue;
		}
		switch (target->kind)
		{
		case _target_tab:
			section_go(target->index);
			break;
		case _target_row:
			/* (a tap chooses the row; on the one chosen, it is its A) */
			if (guide.section == SOCIAL_SECTION_FRIENDS)
			{
				boolean again = guide.row[guide.section] == target->index && !guide.options;

				guide.options = FALSE;
				guide.armed = NONE;
				guide.row[guide.section] = target->index;
				chosen_remember();
				/* (a click on the chosen row, or on the offline friends' row) */
				if (again || guide.items[target->index].kind == _item_offline)
					activate();
				break;
			}
			if (guide.row[guide.section] == target->index && pointer->touch)
				activate();
			guide.row[guide.section] = target->index;
			break;
		case _target_setting:
			guide.row[guide.section] = target->index;
			if (target->index >= NUMBER_OF_SETTINGS)
				device_row_activate((short)(target->index - NUMBER_OF_SETTINGS));
			else
				setting_change(target->index, 1);
			break;
		case _target_option:
			guide.options = TRUE;
			if (guide.option != target->index)
				guide.armed = NONE;
			guide.option = target->index;
			activate();
			break;
		case _target_action:
		case _target_sign_in:
			activate();
			break;
		case _target_prompt:
			handle(target->index == UI_BUTTON_A ? GUIDE_INPUT_A : target->index == UI_BUTTON_X ? GUIDE_INPUT_X :
				target->index == UI_BUTTON_Y ? GUIDE_INPUT_Y : GUIDE_INPUT_B);
			break;
		case _target_close:
			guide_close();
			break;
		}
		return;
	}
	/* (a click outside the panel closes it) */
	if (pointer->click_x < PANEL_X || pointer->click_x >= PANEL_RIGHT || pointer->click_y < PANEL_Y ||
		pointer->click_y >= PANEL_Y + PANEL_HEIGHT)
	{
		guide_close();
	}
}

void guide_render(void)
{
	if (!ui_overlay_available())
		return;
	if (!guide.open)
	{
		render_toast();
		return;
	}
	plasma_look();
	guide.target_count = 0;
	ui_overlay_layer(4);
	ui_overlay_rect(-200, 0, 1040, 480, 0, ui_style_color(UI_COLOR_SHADE));
	ui_style_clock((unsigned int)system_milliseconds());
	ui_style_panel(PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT, 1.0f);
	render_card();
	render_tabs();
	switch (guide.section)
	{
	case SOCIAL_SECTION_PLAYERS_HERE:
		render_players();
		break;
	case SOCIAL_SECTION_SETTINGS:
		render_settings();
		break;
	case SOCIAL_SECTION_FRIENDS:
		if (friends_signed_in())
			render_friends();
		else
			render_coming();
		break;
	default:
		render_coming();
		break;
	}
	render_prompts();
	ui_overlay_layer(0);
}

#endif
