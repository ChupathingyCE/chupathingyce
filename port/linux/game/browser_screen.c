/*
BROWSER_SCREEN.C

The in-game server browser (configure.py
--game-browser): every game on the game list (port/linux/src/browser.c),
on a screen of its own over the menus, as the game's virtual keyboard is
(interface/virtual_keyboard.c): drawn and driven by code, not a widget of
the user interface's tags.

X on the System Link screen opens it (ui_widget.c; the list screen marks
when it is up, ui_widget_game_data_input_functions.c). Up and down pick a
game, left and right turn the page, A joins it through its invite, as a web
page's Join or an invite link would, and B goes back. Once the invite's host
answers, its game shows in the System Link list through the tunnel, to be
picked there as any.

Start opens the player's profile page in the web browser; RB opens Quick
Connect over the list, for where no web browser opens: a code (and a QR
code) to type at the game list's /connect page on another device, then the
profile it was typed for, to confirm with A or refuse with B (browser.c).
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "cutscene/cinematics.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/interface.h"
#include "rasterizer/rasterizer.h"
#include "text/draw_string.h"
#include "bitmaps/bitmap_group.h"
#include "interface/ui_widget.h"
#include "tag_files/tag_groups.h"
#include "game/game.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "networking/network_game_globals.h"
#include "../src/browser.h"
#include "../src/ui_overlay.h"

/* ---------- constants */

enum
{
	/* (event_manager.c's event types, which it keeps to itself) */
	BROWSER_EVENT_LEFT_STICK = 1,
	BROWSER_EVENT_BUTTON = 3,

	ROWS_PER_PAGE = 9,
	ROW_HEIGHT = 26,
	LIST_TOP = 112,
	STATUS_DURATION = 6000,
	/* a picked game's host answers this soon, or it is given up on */
	CONNECT_TIMEOUT = 15000,
	/* the screen takes no A this soon after it opens */
	OPEN_SETTLE = 600,
	/* nor a button this soon after Quick Connect's panel opens or closes (a
	press seen twice would close it, or the screen) */
	CONNECT_SETTLE = 400,
};

/* ui_widget.c owns the same private enum (virtual_keyboard.c keeps a copy) */
enum
{
	_ui_audio_feedback_none,
	_ui_audio_feedback_cursor,
};

/* the game's engines, short (as players say them) to fit the column */
static char const *const engine_names[] =
{
	"", "CTF", "Slayer", "Oddball", "King", "Race",
};

/* the multiplayer maps' names in the menus */
static char const *const map_names[][2] =
{
	{ "beavercreek", "Battle Creek" }, { "bloodgulch", "Blood Gulch" }, { "boardingaction", "Boarding Action" },
	{ "carousel", "Derelict" }, { "chillout", "Chill Out" }, { "damnation", "Damnation" },
	{ "hangemhigh", "Hang 'Em High" }, { "longest", "Longest" }, { "prisoner", "Prisoner" },
	{ "putput", "Chiron TL-34" }, { "ratrace", "Rat Race" }, { "sidewinder", "Sidewinder" }, { "wizard", "Wizard" },
};


/* the list's orders (LT and RT step through them, and LB back; RB is
Quick Connect's) */
enum
{
	SORT_PLAYERS,
	SORT_NAME,
	SORT_MAP,
	SORT_TYPE,

	NUMBER_OF_SORTS
};

/* ---------- globals */

static struct
{
	boolean active;
	short selected;
	short count;
	struct browser_game games[BROWSER_MAXIMUM_GAMES];
	char status[96];
	unsigned long status_time;
	short sort;
	/* a game picked: its invite, while its host's game is waited for */
	boolean connecting;
	char connecting_invite[BROWSER_INVITE_LENGTH + 1];
	char connecting_name[64];
	unsigned long connecting_time;
	/* when the screen opened (the menu's A that opened it picks nothing) */
	unsigned long opened_time;
	/* Quick Connect's panel up, and what it shows (browser.c's, each frame);
	when it last opened, closed or asked for a code */
	boolean connect_open;
	unsigned long connect_changed_time;
	struct browser_connect connect;
} browser_screen;

/* ---------- private code */

static void set_status(
	char const *text)
{
	csstrncpy(browser_screen.status, text, sizeof(browser_screen.status) - 1);
	browser_screen.status[sizeof(browser_screen.status) - 1] = 0;
	browser_screen.status_time = system_milliseconds();
}

static char const *map_display_name(
	char const *path)
{
	char const *base = path;
	char const *cursor;
	long index;

	for (cursor = path; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			base = cursor + 1;
	}
	for (index = 0; index < NUMBEROF(map_names); index++)
	{
		if (!csstrcmp(base, map_names[index][0]))
			return map_names[index][1];
	}
	return base;
}

/* (network_client_manager.c: the game whose host's identifier the invite
starts with, joined once it is advertised) */
long network_game_client_join_invite_host(char const *invite);
boolean create_global_network_game_client(void);
void game_connection_set(short connection);
/* (interface/: the network, as System Link's list starts it, and a game of
this machine's, as its Y makes one) */
boolean ui_online_games_start_network(void);
boolean ui_widget_online_games_create_game(void);

static void utf8_name(unsigned short const *name, char *text, long size);

/* the first player in the game to be joined or made, with the profile
System Link's Start would pick: the one last used, else the first saved */
static void join_first_player(
	void)
{
	long profile_index = player_ui_get_player1_last_used_profile_index();
	struct player_profile profile;

	player_ui_local_player_joined_multiplayer_game(0);
	if (profile_index == NONE)
		profile_index = 0;
	if (player_profile_get(profile_index, &profile))
		player_ui_set_active_player_profile(0, profile_index, &profile);
}

/* a game picked: its invite joined (the tunnel to its host), then its game
joined once advertised through it (browser_screen_process) */
static void join_selected(
	void)
{
	struct browser_game const *game;

	if (browser_screen.selected < 0 || browser_screen.selected >= browser_screen.count)
		return;
	game = &browser_screen.games[browser_screen.selected];
	if (!game->open)
	{
		set_status("That game is not accepting players.");
		return;
	}
	/* a network client searching, as System Link's (the advertisement comes
	to it: browser_screen_open started it) */
	if (!global_network_game_client_get())
	{
		if (!create_global_network_game_client())
		{
			set_status("Could not start the network.");
			return;
		}
		game_connection_set(_game_connection_network_client);
	}
	join_first_player();
	if (!browser_join(game->invite))
	{
		set_status("Internet play is off (network.online in config.toml).");
		return;
	}
	browser_screen.connecting = TRUE;
	csstrncpy(browser_screen.connecting_invite, game->invite, sizeof(browser_screen.connecting_invite) - 1);
	browser_screen.connecting_invite[sizeof(browser_screen.connecting_invite) - 1] = 0;
	utf8_name(game->name, browser_screen.connecting_name, sizeof(browser_screen.connecting_name));
	browser_screen.connecting_time = system_milliseconds();
}

/* the picked game's host: its game joined once it is advertised, and its
lobby opened */
static void wait_for_host(
	void)
{
	long joined = network_game_client_join_invite_host(browser_screen.connecting_invite);

	if (joined > 0)
	{
		browser_screen.connecting = FALSE;
		browser_screen.active = FALSE;
		ui_widgets_close_all();
		ui_widget_load_by_name_or_tag(
			"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen",
			NONE, NULL, NONE, NONE, NONE, NONE);
	}
	else if (joined < 0)
	{
		browser_screen.connecting = FALSE;
		set_status("That game can't be joined from this version.");
	}
	else if (system_milliseconds() - browser_screen.connecting_time > CONNECT_TIMEOUT)
	{
		browser_screen.connecting = FALSE;
		set_status("The host did not answer.");
	}
}

static long compare_names(
	unsigned short const *a,
	unsigned short const *b)
{
	long index;

	for (index = 0; index < BROWSER_NAME_LENGTH; index++)
	{
		unsigned short x = a[index] >= 'a' && a[index] <= 'z' ? (unsigned short)(a[index] - 32) : a[index];
		unsigned short y = b[index] >= 'a' && b[index] <= 'z' ? (unsigned short)(b[index] - 32) : b[index];

		if (x != y || !x)
			return (long)x - (long)y;
	}
	return 0;
}

static long compare_games(
	struct browser_game const *a,
	struct browser_game const *b)
{
	long order;

	/* (closed games last, whatever the order) */
	if (a->open != b->open)
		return a->open ? -1 : 1;
	switch (browser_screen.sort)
	{
	case SORT_NAME: order = compare_names(a->name, b->name); break;
	case SORT_MAP: order = strcmp(map_display_name(a->map), map_display_name(b->map)); break;
	case SORT_TYPE: order = (long)a->engine * 2 + a->teams - ((long)b->engine * 2 + b->teams); break;
	default: order = (long)b->players - (long)a->players; break;
	}
	return order ? order : compare_names(a->name, b->name);
}

/* the game list's games, in the screen's order; the selection stays on its
game */
static void fetch_games(
	void)
{
	char invite[BROWSER_INVITE_LENGTH + 1];
	short index, other;

	invite[0] = 0;
	if (browser_screen.selected >= 0 && browser_screen.selected < browser_screen.count)
		csstrncpy(invite, browser_screen.games[browser_screen.selected].invite, sizeof(invite) - 1);
	invite[sizeof(invite) - 1] = 0;
	browser_screen.count = (short)browser_get_games(browser_screen.games, BROWSER_MAXIMUM_GAMES);
	/* (insertion: a few dozen games) */
	for (index = 1; index < browser_screen.count; index++)
	{
		struct browser_game game = browser_screen.games[index];

		for (other = index; other > 0 && compare_games(&browser_screen.games[other - 1], &game) > 0; other--)
			browser_screen.games[other] = browser_screen.games[other - 1];
		browser_screen.games[other] = game;
	}
	for (index = 0; invite[0] && index < browser_screen.count; index++)
	{
		if (!strcmp(browser_screen.games[index].invite, invite))
			browser_screen.selected = index;
	}
}

/* RB: a Quick Connect code, for the profile the screen's games are joined
with (its name goes to the page, to say who it links) */
static void quick_connect(
	void)
{
	long profile_index = player_ui_get_player1_last_used_profile_index();
	struct player_profile profile;
	unsigned short name[12];
	long index;

	csmemset(name, 0, sizeof(name));
	if (profile_index == NONE)
		profile_index = 0;
	if (player_profile_get(profile_index, &profile))
	{
		for (index = 0; index < NUMBEROF(name) - 1 && index < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH &&
			profile.player_name[index]; index++)
		{
			name[index] = (unsigned short)profile.player_name[index];
		}
	}
	browser_screen.connect_open = TRUE;
	browser_screen.connect_changed_time = system_milliseconds();
	browser_connect_start(name);
	browser_connect_get(&browser_screen.connect);
}

static void close_quick_connect(
	void)
{
	browser_screen.connect_open = FALSE;
	browser_screen.connect_changed_time = system_milliseconds();
	browser_connect_stop();
}

/* the panel's buttons: A and B answer its question while it asks one, RB
asks for a new code once the last is done with, B closes it */
static void quick_connect_button(
	short button)
{
	struct browser_connect const *connect = &browser_screen.connect;
	boolean asking = connect->state == BROWSER_CONNECT_CONFIRM;
	boolean done = connect->state == BROWSER_CONNECT_EXPIRED || connect->state == BROWSER_CONNECT_DECLINED ||
		connect->state == BROWSER_CONNECT_FAILED;

	switch (button)
	{
	case _gamepad_analog_button_a:
		if (asking && !connect->answered)
			browser_connect_answer(TRUE);
		break;
	case _gamepad_analog_button_b:
		if (asking)
		{
			if (!connect->answered)
				browser_connect_answer(FALSE);
		}
		else
			close_quick_connect();
		break;
	case _gamepad_analog_button_black:
		if (done)
			quick_connect();
		break;
	default: break;
	}
}

/* ---------- public code */

boolean browser_screen_active(
	void)
{
	return browser_screen.active;
}

/* the screen opened (the Multiplayer menu's ONLINE GAMES, interface/ui_widget.c) */
void browser_screen_open(
	void)
{
	browser_screen.active = TRUE;
	browser_screen.selected = 0;
	browser_screen.status[0] = 0;
	browser_screen.connecting = FALSE;
	if (browser_screen.connect_open)
		close_quick_connect();
	browser_screen.opened_time = system_milliseconds();
	/* (the menu's A, still queued, is not a pick) */
	event_manager_flush();
	/* the network searching, as System Link's list starts it: a game left
	behind (a lobby backed out of) ended */
	if (!ui_online_games_start_network())
		set_status("Could not start the network.");
	fetch_games();
}

/* Y: a game of this machine's, as System Link's Y makes one (the new game's
map chosen next; once it starts, the game list lists it) */
static void create_game(
	void)
{
	join_first_player();
	if (ui_widget_online_games_create_game())
		browser_screen.active = FALSE;
	else
		set_status("Could not create a game.");
}

void browser_screen_process(
	void)
{
	struct event_record event;
	short move = 0;

	fetch_games();
	if (browser_screen.connecting)
		wait_for_host();
	if (browser_screen.connect_open)
		browser_connect_get(&browser_screen.connect);
	while (browser_screen.active && get_next_event(&event, NONE))
	{
		if (event.type == BROWSER_EVENT_BUTTON &&
			system_milliseconds() - browser_screen.connect_changed_time < CONNECT_SETTLE)
		{
			continue;
		}
		/* (Quick Connect's panel takes the buttons while it is up) */
		if (browser_screen.connect_open)
		{
			if (event.type == BROWSER_EVENT_BUTTON)
				quick_connect_button(event.data.button.index);
			continue;
		}
		if (event.type == BROWSER_EVENT_LEFT_STICK)
		{
			if (event.data.stick.y == SHORT_MAX)
				move = -1;
			else if (event.data.stick.y == SHORT_MIN)
				move = 1;
			else if (event.data.stick.x == SHORT_MIN)
				move = -ROWS_PER_PAGE;
			else if (event.data.stick.x == SHORT_MAX)
				move = ROWS_PER_PAGE;
		}
		else if (event.type == BROWSER_EVENT_BUTTON)
		{
			switch (event.data.button.index)
			{
			case _gamepad_binary_button_dpad_up: move = -1; break;
			case _gamepad_binary_button_dpad_down: move = 1; break;
			case _gamepad_binary_button_dpad_left: move = -ROWS_PER_PAGE; break;
			case _gamepad_binary_button_dpad_right: move = ROWS_PER_PAGE; break;
			case _gamepad_analog_button_a:
				if (!browser_screen.connecting && system_milliseconds() - browser_screen.opened_time > OPEN_SETTLE)
					join_selected();
				break;
			case _gamepad_binary_button_start:
				browser_open_profile();
				set_status("Opening your profile in the web browser");
				break;
			case _gamepad_analog_button_x:
				fetch_games();
				set_status("Refreshed");
				break;
			case _gamepad_analog_button_y:
				if (!browser_screen.connecting && system_milliseconds() - browser_screen.opened_time > OPEN_SETTLE)
					create_game();
				break;
			case _gamepad_binary_button_back:
				set_status("Filters are next");
				break;
			case _gamepad_analog_button_left_trigger:
			case _gamepad_analog_button_white:
				browser_screen.sort = (short)((browser_screen.sort + NUMBER_OF_SORTS - 1) % NUMBER_OF_SORTS);
				fetch_games();
				break;
			case _gamepad_analog_button_right_trigger:
				browser_screen.sort = (short)((browser_screen.sort + 1) % NUMBER_OF_SORTS);
				fetch_games();
				break;
			case _gamepad_analog_button_black:
				if (!browser_screen.connecting)
					quick_connect();
				break;
			case _gamepad_analog_button_b:
				/* (B while a host is waited for: the wait given up) */
				if (browser_screen.connecting)
					browser_screen.connecting = FALSE;
				else
					browser_screen.active = FALSE;
				break;
			default: break;
			}
		}
		if (move)
		{
			browser_screen.selected = (short)PIN(browser_screen.selected + move, 0,
				MAX(0, browser_screen.count - 1));
			move = 0;
		}
	}
	if (browser_screen.selected >= browser_screen.count)
		browser_screen.selected = (short)MAX(0, browser_screen.count - 1);
	/* (the widgets behind take nothing while the browser is up) */
	event_manager_flush();
}

/* ---------- drawing: the Online Games screen (the overlay, ui_overlay.c) */

/* the screen's colors (0xRRGGBBAA), as the mockups */
enum
{
	COLOR_BACKGROUND_TOP = 0x0B1830FF,
	COLOR_BACKGROUND_BOTTOM = 0x03070FFF,
	COLOR_RULE = 0x2A62C8FF,
	COLOR_TITLE = 0x3D8BFFFF,
	COLOR_PANEL = 0x081530F0,
	COLOR_PANEL_EDGE = 0x2F6DD0FF,
	COLOR_HEAD = 0x7FB0FFFF,
	COLOR_ROW_SELECTED = 0x2052B0FF,
	COLOR_ROW_RULE = 0x16294AFF,
	COLOR_TEXT = 0xE6EEFCFF,
	COLOR_DIM = 0x8FA6C8FF,
	COLOR_LABEL = 0x4AA3FFFF,
	/* the roster's players of each team */
	COLOR_RED_TEAM = 0xFF6B6BFF,
	COLOR_BLUE_TEAM = 0x6BB0FFFF,
	COLOR_CLOSED = 0xF08A4BFF,
	COLOR_PROMPT = 0x4AA3FFFF,
};

/* the list's rows, columns and panels, in the 640x480 layout */
enum
{
	LIST_X = 37, LIST_Y = 72, LIST_WIDTH = 566, LIST_HEAD = 20, LIST_ROW = 22, LIST_FOOT = 20,
	DETAIL_Y = 318, DETAIL_HEIGHT = 117,
	/* the roster's places in the details */
	ROSTER_COLUMNS = 2, ROSTER_ROWS = 7,
	COLUMN_NAME = 67, COLUMN_MAP = 275, COLUMN_TYPE = 385, COLUMN_PLAYERS = 531, COLUMN_PING = 596,
};

/* the map pictures (the menus' mp_map_grafix, in their order:
ui_widget_game_data_input_functions.c) and the game types' (game_type_grafix) */
static char const *const map_picture_order[] =
{
	"beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout",
	"carousel", "boardingaction", "bloodgulch", "wizard", "putput", "longest",
};
static short const engine_picture[] = { 5, 0, 2, 3, 1, 4 };

static char const *const sort_names[NUMBER_OF_SORTS] = { "PLAYERS", "NAME", "MAP", "TYPE" };

static void utf8_name(
	unsigned short const *name,
	char *text,
	long size)
{
	long used = 0, index;

	for (index = 0; index < BROWSER_NAME_LENGTH && name[index] && used < size - 4; index++)
	{
		unsigned int character = name[index];

		if (character < 0x80)
			text[used++] = (char)character;
		else if (character < 0x800)
		{
			text[used++] = (char)(0xC0 | (character >> 6));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			text[used++] = (char)(0xE0 | (character >> 12));
			text[used++] = (char)(0x80 | ((character >> 6) & 0x3F));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
	}
	text[used] = 0;
}

static char const *type_name(
	struct browser_game const *game,
	char *text,
	long size)
{
	char const *engine = game->engine >= 0 && game->engine < NUMBEROF(engine_names) && engine_names[game->engine][0] ?
		engine_names[game->engine] : "Game";

	/* (Capture the Flag is played in teams alone) */
	snprintf(text, (size_t)size, "%s%s", game->teams && game->engine != 1 ? "Team " : "", engine);
	return text;
}

/* a game's picture from the game's own bitmaps, in a cut-out of the overlay */
static void draw_picture(
	char const *tag,
	short frame,
	short art_width,
	short art_height,
	short x0,
	short y0,
	short x1,
	short y1)
{
	long bitmap_index = tag_loaded('bitm', tag);
	struct bitmap_data *bitmap = bitmap_index != NONE ? bitmap_group_get_bitmap_from_sequence(bitmap_index, 0, frame) : NULL;
	rectangle2d bounds;

	ui_overlay_cutout(x0, y0, (float)(x1 - x0), (float)(y1 - y0));
	bounds.x0 = x0;
	bounds.y0 = y0;
	bounds.x1 = x1;
	bounds.y1 = y1;
	draw_quad(&bounds, 0xFF0A1A33);
	if (bitmap)
	{
		/* (the picture fills the bitmap's top left; the rest is margin) */
		rectangle2d art;

		art.x0 = 0;
		art.y0 = 0;
		art.x1 = (short)MIN(art_width, bitmap->width);
		art.y1 = (short)MIN(art_height, bitmap->height);
		draw_bitmap_in_rect(bitmap, &bounds, &art, NULL, 0xFFFFFFFF, NULL, FALSE);
	}
}

static float prompt(
	int button,
	char const *words,
	float x)
{
	x += ui_overlay_button(button, 15.0f, x, 455.0f, 0xFFFFFFFF) + 3.0f;
	return x + ui_overlay_text(UI_FONT_BOLD, 12.0f, x, 456.5f, UI_ALIGN_LEFT, COLOR_PROMPT, words) + 20.0f;
}

static float prompt_width(
	int button,
	char const *words)
{
	return ui_overlay_button_width(button, 15.0f) + 3.0f + ui_overlay_text_width(UI_FONT_BOLD, 12.0f, words) + 20.0f;
}

/* ---------- drawing: Quick Connect's panel */

enum
{
	CONNECT_X = 100, CONNECT_Y = 84, CONNECT_WIDTH = 440, CONNECT_HEIGHT = 312,
	CONNECT_QR_X = 382, CONNECT_QR_Y = 150, CONNECT_QR_WIDTH = 138,
	COLOR_CONNECTED = 0x6BE38AFF,
	COLOR_QR_DARK = 0x081020FF,
};

/* a line of text with a button's glyph in it, centred on x */
static void text_with_button(
	float size,
	float x,
	float y,
	unsigned int color,
	char const *before,
	int button,
	char const *after)
{
	float width = ui_overlay_text_width(UI_FONT_BOLD, size, before) + 4 +
		ui_overlay_button_width(button, size + 4) + 4 + ui_overlay_text_width(UI_FONT_BOLD, size, after);

	x -= width / 2;
	x += ui_overlay_text(UI_FONT_BOLD, size, x, y, UI_ALIGN_LEFT, color, before) + 4;
	x += ui_overlay_button(button, size + 4, x, y - 2, 0xFFFFFFFF) + 4;
	ui_overlay_text(UI_FONT_BOLD, size, x, y, UI_ALIGN_LEFT, color, after);
}

/* the panel's buttons, centred along its foot */
static void connect_prompts(
	int first,
	char const *first_words,
	int second,
	char const *second_words)
{
	float width = prompt_width(second, second_words) - 20 - 3;
	float x;

	if (first_words)
		width += prompt_width(first, first_words);
	x = 320 - width / 2;
	if (first_words)
	{
		x += ui_overlay_button(first, 15.0f, x, CONNECT_Y + CONNECT_HEIGHT - 30, 0xFFFFFFFF) + 3;
		x += ui_overlay_text(UI_FONT_BOLD, 12.0f, x, CONNECT_Y + CONNECT_HEIGHT - 28.5f, UI_ALIGN_LEFT, COLOR_PROMPT,
			first_words) + 20;
	}
	x += ui_overlay_button(second, 15.0f, x, CONNECT_Y + CONNECT_HEIGHT - 30, 0xFFFFFFFF) + 3;
	ui_overlay_text(UI_FONT_BOLD, 12.0f, x, CONNECT_Y + CONNECT_HEIGHT - 28.5f, UI_ALIGN_LEFT, COLOR_PROMPT,
		second_words);
}

/* the page with the code as a QR code: dark modules on a light square with
its quiet zone, each row's dark runs one rectangle (overlapping a little,
so that no seam shows between them) */
static void draw_qr(
	struct browser_connect const *connect)
{
	short size = (short)connect->qr_size;
	float module = (float)CONNECT_QR_WIDTH / (size + 8);
	short x, y, run;

	ui_overlay_rect(CONNECT_QR_X, CONNECT_QR_Y, CONNECT_QR_WIDTH, CONNECT_QR_WIDTH, 4, 0xFFFFFFFF);
	for (y = 0; y < size; y++)
	{
		for (x = 0; x < size; x = (short)(x + run))
		{
			for (run = 0; x + run < size && connect->qr[y * size + x + run] == connect->qr[y * size + x]; run++)
				;
			if (connect->qr[y * size + x])
			{
				ui_overlay_rect(CONNECT_QR_X + (x + 4) * module, CONNECT_QR_Y + (y + 4) * module,
					run * module + 0.3f, module + 0.3f, 0, COLOR_QR_DARK);
			}
		}
	}
	ui_overlay_text(UI_FONT_REGULAR, 9.0f, CONNECT_QR_X + CONNECT_QR_WIDTH / 2, CONNECT_QR_Y + CONNECT_QR_WIDTH + 6,
		UI_ALIGN_CENTER, COLOR_DIM, "or scan this with your phone");
}

static void draw_quick_connect(
	void)
{
	struct browser_connect const *connect = &browser_screen.connect;
	char text[192];
	float center = CONNECT_X + CONNECT_WIDTH / 2;

	ui_overlay_rect(CONNECT_X, CONNECT_Y, CONNECT_WIDTH, CONNECT_HEIGHT, 8, 0x0A1A36F8);
	ui_overlay_outline(CONNECT_X, CONNECT_Y, CONNECT_WIDTH, CONNECT_HEIGHT, 8, 1.0f, COLOR_PANEL_EDGE);
	ui_overlay_text(UI_FONT_BOLD, 20.0f, CONNECT_X + 20, CONNECT_Y + 14, UI_ALIGN_LEFT, COLOR_TITLE, "Quick Connect");
	ui_overlay_rect(CONNECT_X + 1, CONNECT_Y + 48, CONNECT_WIDTH - 2, 0.75f, 0, COLOR_ROW_RULE);

	switch (connect->state)
	{
	case BROWSER_CONNECT_WAITING:
	{
		float left = CONNECT_X + 20;
		float y = CONNECT_Y + 66;

		ui_overlay_text(UI_FONT_REGULAR, 10.0f, left, y, UI_ALIGN_LEFT, COLOR_TEXT, "On your phone or computer, go to");
		ui_overlay_text(UI_FONT_BOLD, 13.0f, left, y + 17, UI_ALIGN_LEFT, COLOR_LABEL, connect->page);
		ui_overlay_text(UI_FONT_REGULAR, 10.0f, left, y + 38, UI_ALIGN_LEFT, COLOR_TEXT, "and enter:");
		ui_overlay_rect(left, y + 58, 238, 62, 6, 0x123266FF);
		ui_overlay_outline(left, y + 58, 238, 62, 6, 1.0f, COLOR_PANEL_EDGE);
		ui_overlay_text(UI_FONT_BOLD, 36.0f, left + 119, y + 68, UI_ALIGN_CENTER, 0xFFFFFFFF, connect->code);
		snprintf(text, sizeof(text), "Good for %d:%02d", connect->seconds / 60, connect->seconds % 60);
		ui_overlay_text(UI_FONT_REGULAR, 10.0f, left, y + 132, UI_ALIGN_LEFT, COLOR_DIM, text);
		ui_overlay_text(UI_FONT_BOLD, 10.0f, left, y + 152, UI_ALIGN_LEFT, COLOR_TEXT, "Waiting\xE2\x80\xA6");
		if (connect->qr_size)
			draw_qr(connect);
		connect_prompts(UI_BUTTON_B, NULL, UI_BUTTON_B, "=CLOSE");
		break;
	}
	case BROWSER_CONNECT_CONFIRM:
		/* (the profile the code was typed for: the player says whether it
		is theirs) */
		if (connect->previous[0])
			snprintf(text, sizeof(text), "Move this game from %s to %s?", connect->previous, connect->handle);
		else
			snprintf(text, sizeof(text), "Connect this game to %s?", connect->handle);
		ui_overlay_text(UI_FONT_BOLD, 16.0f, center, CONNECT_Y + 100, UI_ALIGN_CENTER, 0xFFFFFFFF, text);
		snprintf(text, sizeof(text), "Someone signed in as %s entered the code.", connect->handle);
		ui_overlay_text(UI_FONT_REGULAR, 10.0f, center, CONNECT_Y + 132, UI_ALIGN_CENTER, COLOR_DIM, text);
		ui_overlay_text(UI_FONT_REGULAR, 10.0f, center, CONNECT_Y + 148, UI_ALIGN_CENTER, COLOR_DIM,
			"If that isn't you, cancel.");
		if (connect->answered)
		{
			ui_overlay_text(UI_FONT_BOLD, 11.0f, center, CONNECT_Y + 196, UI_ALIGN_CENTER, COLOR_TEXT,
				"Sending your answer\xE2\x80\xA6");
		}
		else
		{
			snprintf(text, sizeof(text), "%d:%02d to answer", connect->seconds / 60, connect->seconds % 60);
			ui_overlay_text(UI_FONT_REGULAR, 10.0f, center, CONNECT_Y + 196, UI_ALIGN_CENTER, COLOR_LABEL, text);
			connect_prompts(UI_BUTTON_A, "=CONNECT", UI_BUTTON_B, "=CANCEL");
		}
		break;
	case BROWSER_CONNECT_CONNECTED:
		snprintf(text, sizeof(text), "Connected as %s", connect->handle);
		ui_overlay_text(UI_FONT_BOLD, 18.0f, center, CONNECT_Y + 110, UI_ALIGN_CENTER, COLOR_CONNECTED, text);
		ui_overlay_text(UI_FONT_REGULAR, 10.0f, center, CONNECT_Y + 144, UI_ALIGN_CENTER, COLOR_DIM,
			"Your games now count toward this profile.");
		connect_prompts(UI_BUTTON_B, NULL, UI_BUTTON_B, "=DONE");
		break;
	case BROWSER_CONNECT_EXPIRED:
	case BROWSER_CONNECT_DECLINED:
	case BROWSER_CONNECT_FAILED:
		if (connect->state == BROWSER_CONNECT_EXPIRED)
			text_with_button(12.0f, center, CONNECT_Y + 120, COLOR_TEXT, "Code expired. Press", UI_BUTTON_RIGHT_SHOULDER,
				"for a new code.");
		else if (connect->state == BROWSER_CONNECT_DECLINED)
			text_with_button(12.0f, center, CONNECT_Y + 120, COLOR_TEXT, "Cancelled. Press", UI_BUTTON_RIGHT_SHOULDER,
				"for a new code.");
		else
		{
			ui_overlay_text(UI_FONT_BOLD, 11.0f, center, CONNECT_Y + 104, UI_ALIGN_CENTER, COLOR_CLOSED, connect->message);
			text_with_button(12.0f, center, CONNECT_Y + 132, COLOR_TEXT, "Press", UI_BUTTON_RIGHT_SHOULDER,
				"to try again.");
		}
		connect_prompts(UI_BUTTON_RIGHT_SHOULDER, "=NEW CODE", UI_BUTTON_B, "=CLOSE");
		break;
	default:
		ui_overlay_text(UI_FONT_BOLD, 11.0f, center, CONNECT_Y + 120, UI_ALIGN_CENTER, COLOR_TEXT,
			"Getting a code\xE2\x80\xA6");
		connect_prompts(UI_BUTTON_B, NULL, UI_BUTTON_B, "=CLOSE");
		break;
	}
}

void browser_screen_render(
	void)
{
	short page_first, page_count, row;
	long players = 0, index;
	char text[160], name[64];
	float x, width, margin = (float)((halo_screen_width() - 640) / 2 + 2);
	struct browser_game const *selected = browser_screen.count ? &browser_screen.games[browser_screen.selected] : NULL;

	if (!ui_overlay_available())
		return;
	for (index = 0; index < browser_screen.count; index++)
		players += browser_screen.games[index].players;

	/* the screen, its widescreen margins too */
	ui_overlay_gradient(-margin, 0, 640 + 2 * margin, 480, 0, COLOR_BACKGROUND_TOP, COLOR_BACKGROUND_BOTTOM);
	ui_overlay_gradient(-margin, 0, 640 + 2 * margin, 62, 0, 0x0A1A36FF, 0x050C1AFF);
	ui_overlay_rect(-margin, 61.5f, 640 + 2 * margin, 1.0f, 0, COLOR_RULE);
	ui_overlay_text(UI_FONT_BOLD, 30.0f, 37, 17, UI_ALIGN_LEFT, COLOR_TITLE, "ONLINE GAMES");

	/* servers, players and the list it comes from, at the right */
	x = 603;
	/* (the game list's address, in capitals as the rest) */
	browser_server_name(text, sizeof(text));
	for (index = 0; text[index]; index++)
	{
		if (text[index] >= 'a' && text[index] <= 'z')
			text[index] = (char)(text[index] - 32);
	}
	x -= ui_overlay_text(UI_FONT_BOLD, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_TEXT, text[0] ? text : "NONE") + 4;
	x -= ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_DIM, "MASTER") + 14;
	snprintf(text, sizeof(text), "%ld", players);
	x -= ui_overlay_text(UI_FONT_BOLD, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_TEXT, text) + 4;
	x -= ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_DIM, "PLAYERS") + 14;
	snprintf(text, sizeof(text), "%d", browser_screen.count);
	x -= ui_overlay_text(UI_FONT_BOLD, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_TEXT, text) + 4;
	ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_DIM, "SERVERS");

	/* Quick Connect's panel in the list's place (the overlay draws all its
	text over all its shapes: none of the list's may lie under the panel) */
	if (browser_screen.connect_open)
	{
		draw_quick_connect();
		return;
	}

	/* the list */
	ui_overlay_rect(LIST_X, LIST_Y, LIST_WIDTH, LIST_HEAD + ROWS_PER_PAGE * LIST_ROW + LIST_FOOT, 6, COLOR_PANEL);
	ui_overlay_gradient(LIST_X, LIST_Y, LIST_WIDTH, LIST_HEAD, 6, 0x123266FF, 0x0C2347FF);
	ui_overlay_text(UI_FONT_BOLD, 8.0f, COLUMN_NAME, LIST_Y + 6, UI_ALIGN_LEFT, COLOR_HEAD, "Server");
	ui_overlay_text(UI_FONT_BOLD, 8.0f, COLUMN_MAP, LIST_Y + 6, UI_ALIGN_LEFT, COLOR_HEAD, "Map");
	ui_overlay_text(UI_FONT_BOLD, 8.0f, COLUMN_TYPE, LIST_Y + 6, UI_ALIGN_LEFT, COLOR_HEAD, "Type");
	ui_overlay_text(UI_FONT_BOLD, 8.0f, COLUMN_PLAYERS, LIST_Y + 6, UI_ALIGN_RIGHT,
		browser_screen.sort == SORT_PLAYERS ? 0xFFFFFFFF : COLOR_HEAD, "Players");
	ui_overlay_text(UI_FONT_BOLD, 8.0f, COLUMN_PING, LIST_Y + 6, UI_ALIGN_RIGHT, COLOR_HEAD, "Ping");

	page_first = (short)(browser_screen.selected - browser_screen.selected % ROWS_PER_PAGE);
	page_count = (short)MAX(1, (browser_screen.count + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
	if (!browser_screen.count)
	{
		ui_overlay_text(UI_FONT_REGULAR, 10.0f, 320, LIST_Y + LIST_HEAD + 80, UI_ALIGN_CENTER, COLOR_DIM,
			"No one is hosting right now. Host a game, and it shows here.");
	}
	for (row = 0; row < ROWS_PER_PAGE; row++)
	{
		float y = (float)(LIST_Y + LIST_HEAD + row * LIST_ROW);
		struct browser_game const *game;
		unsigned int color;

		if (row)
			ui_overlay_rect(LIST_X + 1, y, LIST_WIDTH - 2, 0.5f, 0, COLOR_ROW_RULE);
		if (page_first + row >= browser_screen.count)
			continue;
		game = &browser_screen.games[page_first + row];
		if (page_first + row == browser_screen.selected)
			ui_overlay_rect(LIST_X + 1, y, LIST_WIDTH - 2, LIST_ROW, 0, COLOR_ROW_SELECTED);
		color = game->open ? COLOR_TEXT : COLOR_DIM;
		utf8_name(game->name, name, sizeof(name));
		ui_overlay_text(UI_FONT_BOLD, 10.0f, COLUMN_NAME, y + 5, UI_ALIGN_LEFT, color, name);
		ui_overlay_text(UI_FONT_BOLD, 10.0f, COLUMN_MAP, y + 5, UI_ALIGN_LEFT, color, map_display_name(game->map));
		ui_overlay_text(UI_FONT_BOLD, 10.0f, COLUMN_TYPE, y + 5, UI_ALIGN_LEFT, color, type_name(game, text, sizeof(text)));
		snprintf(text, sizeof(text), "%d/%d", game->players, game->maximum_players);
		ui_overlay_text(UI_FONT_BOLD, 10.0f, COLUMN_PLAYERS, y + 5, UI_ALIGN_RIGHT, game->open ? color : COLOR_CLOSED, text);
		/* (no ping yet: the probe is to come) */
		ui_overlay_text(UI_FONT_REGULAR, 10.0f, COLUMN_PING, y + 5, UI_ALIGN_RIGHT, COLOR_DIM, "\xE2\x80\x93");
	}
	{
		float y = (float)(LIST_Y + LIST_HEAD + ROWS_PER_PAGE * LIST_ROW);

		ui_overlay_rect(LIST_X + 1, y, LIST_WIDTH - 2, 0.75f, 0, COLOR_PANEL_EDGE);
		snprintf(text, sizeof(text), "SORTED BY %s  \xC2\xB7  CLOSED GAMES LAST", sort_names[browser_screen.sort]);
		ui_overlay_text(UI_FONT_BOLD, 7.5f, LIST_X + 10, y + 6, UI_ALIGN_LEFT, COLOR_DIM, text);
		snprintf(text, sizeof(text), "\xE2\x80\xB9   PAGE %d OF %d   \xE2\x80\xBA", page_first / ROWS_PER_PAGE + 1, page_count);
		ui_overlay_text(UI_FONT_BOLD, 8.0f, LIST_X + LIST_WIDTH - 12, y + 5.5f, UI_ALIGN_RIGHT, COLOR_LABEL, text);
	}
	ui_overlay_outline(LIST_X, LIST_Y, LIST_WIDTH, LIST_HEAD + ROWS_PER_PAGE * LIST_ROW + LIST_FOOT, 6, 1.0f,
		COLOR_PANEL_EDGE);

	/* the selected game */
	ui_overlay_rect(LIST_X, DETAIL_Y, LIST_WIDTH, DETAIL_HEIGHT, 6, COLOR_PANEL);
	ui_overlay_outline(LIST_X, DETAIL_Y, LIST_WIDTH, DETAIL_HEIGHT, 6, 1.0f, COLOR_PANEL_EDGE);
	if (selected)
	{
		short map_frame = NUMBEROF(map_picture_order);
		char const *base = selected->map;
		char const *cursor;
		float y = DETAIL_Y + 34;

		for (cursor = selected->map; *cursor; cursor++)
		{
			if (*cursor == '\\' || *cursor == '/')
				base = cursor + 1;
		}
		for (index = 0; index < NUMBEROF(map_picture_order); index++)
		{
			if (!strcmp(base, map_picture_order[index]))
				map_frame = (short)index;
		}
		draw_picture("ui\\shell\\bitmaps\\mp_map_grafix", map_frame, 140, 116, 46, DETAIL_Y + 9, 171, DETAIL_Y + DETAIL_HEIGHT - 9);
		ui_overlay_outline(45, DETAIL_Y + 8, 127, DETAIL_HEIGHT - 16, 0, 1.0f, COLOR_ROW_RULE);
		draw_picture("ui\\shell\\bitmaps\\game_type_grafix",
			engine_picture[selected->engine >= 0 && selected->engine < NUMBEROF(engine_picture) ? selected->engine : 0],
			140, 114, 181, DETAIL_Y + 9, 256, DETAIL_Y + DETAIL_HEIGHT - 9);

		utf8_name(selected->name, name, sizeof(name));
		ui_overlay_text(UI_FONT_BOLD, 13.0f, 266, DETAIL_Y + 12, UI_ALIGN_LEFT, 0xFFFFFFFF, name);
#define DETAIL_LINE(label, value) \
		x = 266 + ui_overlay_text(UI_FONT_REGULAR, 10.0f, 266, y, UI_ALIGN_LEFT, COLOR_LABEL, label) + 4; \
		ui_overlay_text(UI_FONT_REGULAR, 10.0f, x, y, UI_ALIGN_LEFT, COLOR_TEXT, value); \
		y += 16;
		DETAIL_LINE("Status:", selected->open ? "Accepting Players" : "In Progress");
		DETAIL_LINE("Map:", map_display_name(selected->map));
		DETAIL_LINE("Rules:", type_name(selected, text, sizeof(text)));
		if (selected->score_limit)
		{
			snprintf(text, sizeof(text), "%d", selected->score_limit);
			DETAIL_LINE("Score Limit:", text);
		}
		snprintf(text, sizeof(text), "%d of %d", selected->players, selected->maximum_players);
		DETAIL_LINE("Players:", text);
#undef DETAIL_LINE

		/* who is in it (the host's roster, when it sends one: two columns of
		seven, the last place saying how many more) */
		ui_overlay_rect(444, DETAIL_Y + 10, 0.75f, DETAIL_HEIGHT - 20, 0, COLOR_ROW_RULE);
		ui_overlay_text(UI_FONT_BOLD, 8.5f, 453, DETAIL_Y + 10, UI_ALIGN_LEFT, COLOR_LABEL, "IN GAME");
		if (!selected->players && !selected->roster_count)
			ui_overlay_text(UI_FONT_REGULAR, 8.5f, 453, DETAIL_Y + 27, UI_ALIGN_LEFT, COLOR_DIM, "No one yet");
		else if (!selected->roster_count)
			ui_overlay_text(UI_FONT_REGULAR, 8.5f, 453, DETAIL_Y + 27, UI_ALIGN_LEFT, COLOR_DIM,
				"This host doesn't share names");
		else
		{
			long kept = selected->roster_count < BROWSER_LISTED_ROSTER ? selected->roster_count : BROWSER_LISTED_ROSTER;
			long places = ROSTER_COLUMNS * ROSTER_ROWS;
			long shown = selected->roster_count > places ? places - 1 : kept;
			long index;

			for (index = 0; index < shown; index++)
			{
				struct browser_roster_player const *player = &selected->roster[index];
				unsigned long color = !selected->teams || player->team < 0 ? COLOR_TEXT :
					player->team == 0 ? COLOR_RED_TEAM : COLOR_BLUE_TEAM;

				utf8_name(player->name, name, sizeof(name));
				ui_overlay_text(UI_FONT_REGULAR, 8.5f, 453 + (index / ROSTER_ROWS) * 85,
					DETAIL_Y + 27 + (index % ROSTER_ROWS) * 11, UI_ALIGN_LEFT, color, name);
			}
			if (selected->roster_count > shown)
			{
				snprintf(text, sizeof(text), "+%d more", selected->roster_count - (int)shown);
				ui_overlay_text(UI_FONT_REGULAR, 8.5f, 453 + (shown / ROSTER_ROWS) * 85,
					DETAIL_Y + 27 + (shown % ROSTER_ROWS) * 11, UI_ALIGN_LEFT, COLOR_DIM, text);
			}
		}
	}

	/* the buttons */
	ui_overlay_rect(-margin, 444, 640 + 2 * margin, 0.75f, 0, COLOR_RULE);
	width = prompt_width(UI_BUTTON_A, "=JOIN") + prompt_width(UI_BUTTON_B, "=BACK") +
		prompt_width(UI_BUTTON_X, "=REFRESH") + prompt_width(UI_BUTTON_Y, "=CREATE GAME") +
		prompt_width(UI_BUTTON_BACK, "=FILTERS") +
		prompt_width(UI_BUTTON_LEFT_TRIGGER, "") + prompt_width(UI_BUTTON_RIGHT_TRIGGER, "=SORT") +
		prompt_width(UI_BUTTON_RIGHT_SHOULDER, "=QUICK CONNECT") - 20 - 3;
	x = 320 - width / 2;
	x = prompt(UI_BUTTON_A, "=JOIN", x);
	x = prompt(UI_BUTTON_B, "=BACK", x);
	x = prompt(UI_BUTTON_X, "=REFRESH", x);
	x = prompt(UI_BUTTON_Y, "=CREATE GAME", x);
	x = prompt(UI_BUTTON_BACK, "=FILTERS", x);
	x += ui_overlay_button(UI_BUTTON_LEFT_TRIGGER, 15.0f, x, 455.0f, 0xFFFFFFFF);
	x = prompt(UI_BUTTON_RIGHT_TRIGGER, "=SORT", x);
	prompt(UI_BUTTON_RIGHT_SHOULDER, "=QUICK CONNECT", x);

	if (browser_screen.connecting)
	{
		char line[160];
		long dots = (long)((system_milliseconds() - browser_screen.connecting_time) / 400 % 4);

		snprintf(line, sizeof(line), "Connecting to %s%.*s", browser_screen.connecting_name, (int)dots, "...");
		ui_overlay_rect(170, 200, 300, 64, 6, 0x0A1A36F8);
		ui_overlay_outline(170, 200, 300, 64, 6, 1.0f, COLOR_PANEL_EDGE);
		ui_overlay_text(UI_FONT_BOLD, 12.0f, 320, 212, UI_ALIGN_CENTER, 0xFFFFFFFF, line);
		x = 320 - (ui_overlay_button_width(UI_BUTTON_B, 13.0f) + ui_overlay_text_width(UI_FONT_BOLD, 10.0f, "=CANCEL")) / 2;
		x += ui_overlay_button(UI_BUTTON_B, 13.0f, x, 236, 0xFFFFFFFF) + 3;
		ui_overlay_text(UI_FONT_BOLD, 10.0f, x, 237.5f, UI_ALIGN_LEFT, COLOR_PROMPT, "=CANCEL");
	}
	else if (browser_screen.status[0] && system_milliseconds() - browser_screen.status_time < STATUS_DURATION)
		ui_overlay_text(UI_FONT_BOLD, 9.0f, 603, 300, UI_ALIGN_RIGHT, COLOR_CLOSED, browser_screen.status);
}

#endif
