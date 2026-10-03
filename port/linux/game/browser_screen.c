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

A game on a Custom Edition map (Halo PC's, announced as <file>@ce) is named
as the menus' map list names it (ui_map_list.c: Halo PC's own names, or the
file's made readable), marked HALO PC as the game list's web pages mark it,
and pictured by Halo PC's own picture of it. It is joined only with the map
in maps\ce (and on a 64-bit build, the only one that plays them): else its
details say what is missing, and A says so rather than join.
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
#include "halo_ui_map_list.h"

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
	/* whether the selected game's Custom Edition map is in maps\ce, asked
	again this often */
	CE_MAP_CHECK_INTERVAL = 1000,
};

/* whether a game's map can be played here (ce_map_state) */
enum
{
	/* an Xbox map */
	_ce_map_none,
	/* a Custom Edition map, in maps\ce */
	_ce_map_present,
	/* a Custom Edition map this machine lacks */
	_ce_map_missing,
	/* a Custom Edition map, on a build that plays none (32-bit) */
	_ce_map_unsupported,
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


/* the list's orders (LT and RT, or the shoulders, step through them) */
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
	/* the last game's map asked of maps\ce, and the answer (ce_map_state) */
	char ce_map[BROWSER_MAP_LENGTH];
	short ce_map_answer;
	unsigned long ce_map_time;
} browser_screen;

/* ---------- private code */

static void set_status(
	char const *text)
{
	csstrncpy(browser_screen.status, text, sizeof(browser_screen.status) - 1);
	browser_screen.status[sizeof(browser_screen.status) - 1] = 0;
	browser_screen.status_time = system_milliseconds();
}

static void utf8_text(unsigned short const *name, long length, char *text, long size);

/* a game's map: its file's name (the path's last part, without a Custom
Edition map's @ce), and whether it is a Custom Edition map */
static boolean map_file(
	char const *path,
	char *file,
	long size)
{
	char const *base = path;
	char const *cursor;
	long length;
	boolean ce;

	for (cursor = path; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			base = cursor + 1;
	}
	length = (long)strlen(base);
	ce = length >= 3 && base[length - 3] == '@' && (base[length - 2] | 0x20) == 'c' && (base[length - 1] | 0x20) == 'e';
	if (ce)
		length -= 3;
	snprintf(file, (size_t)size, "%.*s", (int)length, base);
	return ce;
}

static boolean map_is_ce(
	char const *path)
{
	char file[BROWSER_MAP_LENGTH];

	return map_file(path, file, sizeof(file));
}

/* a game's map as players name it: an Xbox map's name in the menus, or a
Custom Edition map's as the menus' map list names it */
static char const *map_display_name(
	char const *path,
	char *text,
	long size)
{
	char file[BROWSER_MAP_LENGTH];
	long index;

	if (map_file(path, file, sizeof(file)))
	{
		wchar_t name[48];
		unsigned short characters[48];
		long length;

		ui_map_list_ce_name(file, name, NUMBEROF(name));
		for (length = 0; length < NUMBEROF(name) && name[length]; length++)
			characters[length] = (unsigned short)name[length];
		utf8_text(characters, length, text, size);
		if (!text[0])
			snprintf(text, (size_t)size, "Unknown map");
		return text;
	}
	for (index = 0; index < NUMBEROF(map_names); index++)
	{
		if (!csstrcmp(file, map_names[index][0]))
			return map_names[index][1];
	}
	/* (the path's last part, as it was) */
	snprintf(text, (size_t)size, "%s", file);
	return text;
}

/* whether a game's map can be played here: an Xbox map, or a Custom Edition
map in maps\ce, missing, or on a build without them; maps\ce asked again
when fresh, or for another map, or now and then */
static short ce_map_state(
	struct browser_game const *game,
	boolean fresh)
{
	char file[BROWSER_MAP_LENGTH];

	if (!map_file(game->map, file, sizeof(file)))
		return _ce_map_none;
#ifdef HALO_64BIT
	if (fresh || strcmp(browser_screen.ce_map, game->map) ||
		system_milliseconds() - browser_screen.ce_map_time > CE_MAP_CHECK_INTERVAL)
	{
		csstrncpy(browser_screen.ce_map, game->map, sizeof(browser_screen.ce_map) - 1);
		browser_screen.ce_map[sizeof(browser_screen.ce_map) - 1] = 0;
		browser_screen.ce_map_answer = ui_map_list_ce_present(file) ? _ce_map_present : _ce_map_missing;
		browser_screen.ce_map_time = system_milliseconds();
	}
	return browser_screen.ce_map_answer;
#else
	(void)fresh;
	return _ce_map_unsupported;
#endif
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
	/* (a Custom Edition map's game, only with the map: a join without it
	would fail at its loading; the list's footer says what is missing) */
	if (ce_map_state(game, TRUE) >= _ce_map_missing)
		return;
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
	case SORT_MAP:
	{
		char a_name[64], b_name[64];

		order = strcmp(map_display_name(a->map, a_name, sizeof(a_name)), map_display_name(b->map, b_name, sizeof(b_name)));
		/* (an Xbox map before Halo PC's of the same name) */
		if (!order)
			order = (long)map_is_ce(a->map) - (long)map_is_ce(b->map);
		break;
	}
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

/* what this machine lacks to join a game on a Custom Edition map, or NULL
when nothing (ce_map_state) */
static char const *ce_map_blocker(
	struct browser_game const *game,
	char *text,
	long size)
{
	char file[BROWSER_MAP_LENGTH];

	switch (ce_map_state(game, FALSE))
	{
	case _ce_map_missing:
		map_file(game->map, file, sizeof(file));
		snprintf(text, (size_t)size, "Needs maps/ce/%s.map to join", file);
		return text;
	case _ce_map_unsupported:
		return "Halo PC maps need the 64-bit ChupathingyCE.";
	default:
		return NULL;
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
	while (browser_screen.active && get_next_event(&event, NONE))
	{
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
			case _gamepad_analog_button_black:
				browser_screen.sort = (short)((browser_screen.sort + 1) % NUMBER_OF_SORTS);
				fetch_games();
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
	/* a prompt that does nothing for the selected game */
	COLOR_PROMPT_OFF = 0x4A5E80FF,
	COLOR_BUTTON_OFF = 0xFFFFFF55,
	/* Halo PC's maps: their badge, gold as the game list's web pages draw it */
	COLOR_PC = 0xF2C14EFF,
	COLOR_PC_FILL = 0xF2C14E1F,
	COLOR_PC_EDGE = 0xF2C14E99,
};

/* the list's rows, columns and panels, in the 640x480 layout */
enum
{
	LIST_X = 37, LIST_Y = 72, LIST_WIDTH = 566, LIST_HEAD = 20, LIST_ROW = 22, LIST_FOOT = 20,
	DETAIL_Y = 318, DETAIL_HEIGHT = 117,
	/* the roster's places in the details */
	ROSTER_COLUMNS = 2, ROSTER_ROWS = 7,
	COLUMN_NAME = 67, COLUMN_MAP = 275, COLUMN_TYPE = 385, COLUMN_PLAYERS = 531, COLUMN_PING = 596,
	/* the details' lines: the first's top, and each next's */
	DETAIL_FIRST_LINE = 34, DETAIL_LINE_STEP = 16,
};

/* the HALO PC badge: its text's size, and its room before the Type column */
#define BADGE_SIZE 6.5f
enum
{
	BADGE_MARGIN = 12,
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
	utf8_text(name, BROWSER_NAME_LENGTH, text, size);
}

/* UTF-16 text, up to length characters, as UTF-8 */
static void utf8_text(
	unsigned short const *name,
	long length,
	char *text,
	long size)
{
	long used = 0, index;

	for (index = 0; index < length && name[index] && used < size - 4; index++)
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

static void draw_bitmap_picture(struct bitmap_data *bitmap, short art_width, short art_height, short x0, short y0,
	short x1, short y1);

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

	draw_bitmap_picture(bitmap, art_width, art_height, x0, y0, x1, y1);
}

/* a picture (or none: the box alone), in a cut-out of the overlay */
static void draw_bitmap_picture(
	struct bitmap_data *bitmap,
	short art_width,
	short art_height,
	short x0,
	short y0,
	short x1,
	short y1)
{
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

/* a prompt that does nothing for the selected game, greyed */
static float prompt_off(
	int button,
	char const *words,
	float x)
{
	x += ui_overlay_button(button, 15.0f, x, 455.0f, COLOR_BUTTON_OFF) + 3.0f;
	return x + ui_overlay_text(UI_FONT_BOLD, 12.0f, x, 456.5f, UI_ALIGN_LEFT, COLOR_PROMPT_OFF, words) + 20.0f;
}

/* the HALO PC badge of a game on a Custom Edition map, size its text's
height: its width */
static float pc_badge_width(
	float size)
{
	return ui_overlay_text_width(UI_FONT_BOLD, size, "HALO PC") + size;
}

/* the badge at x (its left), centred on the capitals of text whose top is
text_y and height text_size (as ui_overlay_text draws them: their middle
half the size down) */
static void draw_pc_badge(
	float size,
	float x,
	float text_y,
	float text_size)
{
	float padding = size * 0.5f;
	float height = size + padding;
	float y = text_y + text_size * 0.5f - height * 0.5f;

	ui_overlay_rect(x, y, pc_badge_width(size), height, 2, COLOR_PC_FILL);
	ui_overlay_outline(x, y, pc_badge_width(size), height, 2, 0.75f, COLOR_PC_EDGE);
	ui_overlay_text(UI_FONT_BOLD, size, x + padding, y + padding * 0.55f, UI_ALIGN_LEFT, COLOR_PC, "HALO PC");
}

static float prompt_width(
	int button,
	char const *words)
{
	return ui_overlay_button_width(button, 15.0f) + 3.0f + ui_overlay_text_width(UI_FONT_BOLD, 12.0f, words) + 20.0f;
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
	x -= ui_overlay_text(UI_FONT_BOLD, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_TEXT, "HALO.MILENKO.ORG") + 4;
	x -= ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_DIM, "MASTER") + 14;
	snprintf(text, sizeof(text), "%ld", players);
	x -= ui_overlay_text(UI_FONT_BOLD, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_TEXT, text) + 4;
	x -= ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_DIM, "PLAYERS") + 14;
	snprintf(text, sizeof(text), "%d", browser_screen.count);
	x -= ui_overlay_text(UI_FONT_BOLD, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_TEXT, text) + 4;
	ui_overlay_text(UI_FONT_REGULAR, 9.0f, x, 39, UI_ALIGN_RIGHT, COLOR_DIM, "SERVERS");

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
		char const *map_name;
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
		map_name = map_display_name(game->map, text, sizeof(text));
		if (map_is_ce(game->map))
		{
			/* (the badges in a column at the Map column's right; a name too long
			for the room left of it smaller, its middle where the others' is) */
			float badge_x = COLUMN_TYPE - BADGE_MARGIN - pc_badge_width(BADGE_SIZE);
			float size = 10.0f;

			while (size > 7.0f && ui_overlay_text_width(UI_FONT_BOLD, size, map_name) > badge_x - 4 - COLUMN_MAP)
				size -= 0.5f;
			ui_overlay_text(UI_FONT_BOLD, size, COLUMN_MAP, y + 5 + (10.0f - size) / 2, UI_ALIGN_LEFT, color, map_name);
			draw_pc_badge(BADGE_SIZE, badge_x, y + 5, 10.0f);
		}
		else
			ui_overlay_text(UI_FONT_BOLD, 10.0f, COLUMN_MAP, y + 5, UI_ALIGN_LEFT, color, map_name);
		ui_overlay_text(UI_FONT_BOLD, 10.0f, COLUMN_TYPE, y + 5, UI_ALIGN_LEFT, color, type_name(game, text, sizeof(text)));
		snprintf(text, sizeof(text), "%d/%d", game->players, game->maximum_players);
		ui_overlay_text(UI_FONT_BOLD, 10.0f, COLUMN_PLAYERS, y + 5, UI_ALIGN_RIGHT, game->open ? color : COLOR_CLOSED, text);
		/* (no ping yet: the probe is to come) */
		ui_overlay_text(UI_FONT_REGULAR, 10.0f, COLUMN_PING, y + 5, UI_ALIGN_RIGHT, COLOR_DIM, "\xE2\x80\x93");
	}
	{
		float y = (float)(LIST_Y + LIST_HEAD + ROWS_PER_PAGE * LIST_ROW);
		char blocker_text[BROWSER_MAP_LENGTH + 32];
		char const *blocker = selected ? ce_map_blocker(selected, blocker_text, sizeof(blocker_text)) : NULL;

		ui_overlay_rect(LIST_X + 1, y, LIST_WIDTH - 2, 0.75f, 0, COLOR_PANEL_EDGE);
		/* (in the sort's place: a status message while it shows, else what the
		selected game's Custom Edition map lacks to be joined) */
		if (!browser_screen.connecting && browser_screen.status[0] &&
			system_milliseconds() - browser_screen.status_time < STATUS_DURATION)
		{
			ui_overlay_text(UI_FONT_BOLD, 8.0f, LIST_X + 10, y + 5.5f, UI_ALIGN_LEFT, COLOR_CLOSED, browser_screen.status);
		}
		else if (blocker)
			ui_overlay_text(UI_FONT_BOLD, 8.0f, LIST_X + 10, y + 5.5f, UI_ALIGN_LEFT, COLOR_CLOSED, blocker);
		else
		{
			snprintf(text, sizeof(text), "SORTED BY %s  \xC2\xB7  CLOSED GAMES LAST", sort_names[browser_screen.sort]);
			ui_overlay_text(UI_FONT_BOLD, 7.5f, LIST_X + 10, y + 6, UI_ALIGN_LEFT, COLOR_DIM, text);
		}
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
		char file[BROWSER_MAP_LENGTH], map_name[64];
		short ce_state = ce_map_state(selected, FALSE);
		float y = DETAIL_Y + DETAIL_FIRST_LINE;

		map_file(selected->map, file, sizeof(file));
		if (ce_state != _ce_map_none)
		{
			struct bitmap_data *bitmap = NULL;

#ifdef HALO_64BIT
			/* (Halo PC's picture of it, or of an unknown level: laid out as the
			Xbox's) */
			bitmap = ui_map_list_ce_picture(file);
#endif
			draw_bitmap_picture(bitmap, 140, 116, 46, DETAIL_Y + 9, 171, DETAIL_Y + DETAIL_HEIGHT - 9);
		}
		else
		{
			for (index = 0; index < NUMBEROF(map_picture_order); index++)
			{
				if (!strcmp(file, map_picture_order[index]))
					map_frame = (short)index;
			}
			draw_picture("ui\\shell\\bitmaps\\mp_map_grafix", map_frame, 140, 116, 46, DETAIL_Y + 9, 171,
				DETAIL_Y + DETAIL_HEIGHT - 9);
		}
		ui_overlay_outline(45, DETAIL_Y + 8, 127, DETAIL_HEIGHT - 16, 0, 1.0f, COLOR_ROW_RULE);
		draw_picture("ui\\shell\\bitmaps\\game_type_grafix",
			engine_picture[selected->engine >= 0 && selected->engine < NUMBEROF(engine_picture) ? selected->engine : 0],
			140, 114, 181, DETAIL_Y + 9, 256, DETAIL_Y + DETAIL_HEIGHT - 9);

		utf8_name(selected->name, name, sizeof(name));
		ui_overlay_text(UI_FONT_BOLD, 13.0f, 266, DETAIL_Y + 12, UI_ALIGN_LEFT, 0xFFFFFFFF, name);
#define DETAIL_LINE(label, value) \
		x = 266 + ui_overlay_text(UI_FONT_REGULAR, 10.0f, 266, y, UI_ALIGN_LEFT, COLOR_LABEL, label) + 4; \
		x += ui_overlay_text(UI_FONT_REGULAR, 10.0f, x, y, UI_ALIGN_LEFT, COLOR_TEXT, value); \
		y += DETAIL_LINE_STEP;
		DETAIL_LINE("Status:", selected->open ? "Accepting Players" : "In Progress");
		DETAIL_LINE("Map:", map_display_name(selected->map, map_name, sizeof(map_name)));
		if (ce_state != _ce_map_none)
			draw_pc_badge(BADGE_SIZE, x + 6, y - DETAIL_LINE_STEP, 10.0f);
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
		prompt_width(UI_BUTTON_LEFT_TRIGGER, "") + prompt_width(UI_BUTTON_RIGHT_TRIGGER, "=SORT") - 20 - 3;
	x = 320 - width / 2;
	/* (A greyed for a game on a Custom Edition map that can't be played here) */
	if (selected && ce_map_state(selected, FALSE) >= _ce_map_missing)
		x = prompt_off(UI_BUTTON_A, "=JOIN", x);
	else
		x = prompt(UI_BUTTON_A, "=JOIN", x);
	x = prompt(UI_BUTTON_B, "=BACK", x);
	x = prompt(UI_BUTTON_X, "=REFRESH", x);
	x = prompt(UI_BUTTON_Y, "=CREATE GAME", x);
	x = prompt(UI_BUTTON_BACK, "=FILTERS", x);
	x += ui_overlay_button(UI_BUTTON_LEFT_TRIGGER, 15.0f, x, 455.0f, 0xFFFFFFFF);
	prompt(UI_BUTTON_RIGHT_TRIGGER, "=SORT", x);

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
}

#endif
