/*
LOBBY_SCREEN.C

The game lobby of the Chupathingy menus (configure.py --game-browser): one
screen for every way of playing together, drawn and driven by code over
the Xbox's pregame screen as Online Games is (browser_screen.c), in place
of the System Link screen's four machine panels. The Xbox menus keep the
Xbox's screen (display.menus "xbox"), and display.lobby turns this one off.

The players on the left, one row each: the team's stripe, their name
(their Delta profile's when they share it: profile_names.c) with the Delta
mark and the host's crown, a line under it (the team, then You, Party,
Friend or OpenCE), a voice icon while they talk, and READY or NOT READY (a
score for a game under way); the count in the list's head, and "+N OPEN"
past the open slots shown. A team game that doesn't fit one column shows
the red team on the left and the blue on the right. On the right the map's
card (its picture in its own shape, the map's name, the game type and the
countdown), the rules (the score to win, the time limit, teams) and the
menu of what this player can do.

Three states, from what the game says:
- hosted: this machine's server. START GAME lets the countdown run
  (CANCEL START holds it again), CHANGE MAP opens the Xbox's map select,
  GAME SETUP the Chupathingy menus' setup, INVITE FRIENDS the Guide's party.
- joined: someone else's game. READY tells the host over Delta (lobby_ready:
  docs/delta/peer.md), CHANGE TEAM is the Xbox's swap, GAME DETAILS the rules.
  A host without Delta's READY (OpenCE's, an older build, the Xbox menus)
  gets the Xbox's START SOONER instead. A machine that never says READY in
  a lobby (one without it, or without this lobby up) counts as ready, and
  each of a split screen's players says READY on their own controller.
- server: a dedicated server (a host machine with no player). As joined
  without READY: the server's own countdown decides.

The preview: a game under way is shown here before it is joined, from
what the game list and the host's advertisement say (the roster, the map,
the game type, the players): JOIN THE GAME joins it, B goes back to the
list (lobby_screen_preview_*: browser_screen.c's join waits for the
answer). ADD PLAYERS, or another controller's START, opens the Xbox's
SELECT PROFILE over it for split screen players, who join with JOIN THE
GAME together.

The retail pregame mechanics stay: B leaves through the Xbox screen's own
handler (netgame_unjoin_player), the team swap is the Xbox's, the countdown
is the server's (START GAME only lets it run: network_game_server_pause_
countdown), and a second controller's START joins split screen and its B
takes that player out, through the Xbox screen as they did (the first
controller's B is the lobby's LEAVE, as is the B of a controller with no
player here while the machine has one player; with more, that B does
nothing). While a gamepad here has no player and the game has room, the
first open slot reads PRESS START TO JOIN.

Input: up and down move in the list or the menu, left and right (and the
shoulders) move between the panes, A acts, B leaves, X changes team, Y opens
the selected player's details (mute, and the host's kick). The pointer
clicks rows, menu rows and the prompts. debug.lobby_preview opens any
state with sample players over the main menu, for screenshots and tests.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/ui_widget.h"
#include "interface/ui_widget_definitions.h"
#include "interface/ui_widget_instance.h"
#include "text/unicode.h"
#include "tag_files/tag_groups.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "../src/browser.h"
#include "../src/social.h"
#include "../src/ui_overlay.h"
#include "../src/lobby_layout.h"
#include "../src/guide_layout.h"
#include "../src/delta_peer.h"
#include "../src/delta_wire.h"
#include "../src/server_badge.h"
#include "delta.h"
#include "network_rules.h"
#include "halo_ui_pointer.h"
#include "halo_profile_screen.h"
#include "halo_port_limits.h"
#include "lobby_screen.h"
#include "halo_map_families.h"
#include "voice_game.h"
#include "chat.h"
#include "guide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int config_boolean(char const *name);
char const *config_string(char const *name);
void platform_log(char const *format, ...);
boolean pc_menus_chupathingy(void);
boolean invite_prompt_active(void);
boolean server_ok_to_countdown(struct network_game_server *server);
boolean network_game_server_lobby_is_open(struct network_game_server *server);
void network_game_server_dedicated_start_countdown(struct network_game_server *server);
void network_game_server_keep_menus_variant(struct network_game_server *server);
boolean ui_widget_port_dispatch_event(struct widget_instance *widget, short event_type, short controller_index,
	boolean *deleted);
struct widget_instance *ui_widget_port_replace(struct widget_instance *widget, long tag_index);
/* (xinput_sdl.c) the keyboard or mouse used last; player 1's controller
(-1: the keyboard and mouse) */
int halo_keyboard_prompts(void);
void platform_gamepad_make_first(int port);
struct widget_instance *ui_widget_port_top_screen(void);
/* (interface/ui_widget.c) where B leaves to: the screen the game came from */
void ui_widget_port_lobby_origin_set(boolean joined_from_online_games);
boolean ui_widget_port_lobby_origin_online_games(void);
void ui_widget_port_lobby_leave_history(void);
void browser_screen_open(void);
boolean ui_widget_port_open_screen(char const *name);
int p2p_hosting_status(char *text, int size);
/* (browser_screen.c) the map's picture in a box, and its name as players
know it */
void browser_screen_map_picture(char const *map, float x, float y, float width, float height);
char const *browser_screen_map_name(char const *map, char *text, long size);
boolean browser_screen_active(void);
#ifndef HALO_CONSOLE_NO_CONTROLS
boolean controls_screen_active(void);
#endif
boolean moderation_screen_active(void);
#ifndef HALO_CONSOLE_NO_DELTA_PROFILE
boolean profile_screen_active(void);
#endif

/* ---------- constants */

/* the Xbox's pregame screen, and the screens the host's menu opens */
/* (the Multiplayer menu, ONLINE GAMES' screen) */
#define MULTIPLAYER_SCREEN "ui\\shell\\main_menu\\multiplayer_type_select\\multiplayer_type_select_screen"
#define PREGAME_SCREEN "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen"
#define MAP_SELECT_SCREEN "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\connected_map_select_postgame_wrapper"
#define HOST_SETUP_SCREEN "pc\\chupathingy\\host\\setup_screen"
/* System Link's SELECT PROFILE (ui.map's four quarters), and a quarter
empty ("press A to join"), choosing its profile, and with it picked */
#define ADD_PLAYERS_SCREEN \
	"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\4way_profile_select\\4way_start2join_screen"
#define ADD_PLAYERS_EMPTY \
	"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\4way_profile_select\\qtr_screen_start2join"
#define ADD_PLAYERS_CHOOSING \
	"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\4way_profile_select\\qtr_screen_profile_select"
#define ADD_PLAYERS_PICKED \
	"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\4way_profile_select\\qtr_screen_profile_selected"
/* the events the pregame screen takes (ui_widget_port_dispatch_event) */
#define EVENT_A 0
#define EVENT_B 1

enum
{
	_lobby_none,
	_lobby_hosted,
	_lobby_joined,
	_lobby_server,
	_lobby_preview,
};

enum
{
	_pane_list,
	_pane_list_right,
	_pane_menu,
	NUMBER_OF_PANES
};

enum
{
	_menu_none,
	_menu_start,
	_menu_cancel_start,
	_menu_change_map,
	_menu_game_setup,
	_menu_ready,
	_menu_start_sooner,
	_menu_change_team,
	_menu_details,
	_menu_invite,
	_menu_join,
	_menu_add_players,
};

enum
{
	_dialog_none,
	_dialog_player,
	_dialog_details,
	_dialog_leave,
};

enum
{
	_player_action_mute,
	_player_action_kick,
	NUMBER_OF_PLAYER_ACTIONS
};

enum
{
	MAXIMUM_ROWS = HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
	/* this machine's players (its controllers) */
	LOCAL_PLAYERS = 4,
	MAXIMUM_MENU = LOBBY_LAYOUT_MAXIMUM_MENU_ROWS,
	MAXIMUM_FACTS = LOBBY_LAYOUT_MAXIMUM_FACTS,
	MAXIMUM_TARGETS = 160,
	/* the game's player list states (network_client_manager.c) */
	CLIENT_STATE_PREGAME = 2,
	/* the pregame screen is this one while its widget was seen this long
	ago at most (ms); a press waits this long after the screen opens */
	SEEN_WINDOW = 100,
	SETTLE = 250,
	/* READY said again while the host's state does not show it (ms) */
	READY_RESEND = 2000,
	LOBBY_EVENT_LEFT_STICK = 1,
	LOBBY_EVENT_BUTTON = 3,
	/* a held direction: the first repeat, then each (ms), and the gap that
	makes a new press */
	STEP_FIRST_REPEAT_DELAY = 400,
	STEP_REPEAT_INTERVAL = 90,
	STEP_RELEASE_GAP = 120,
	/* the names kept of a party, for the rows' lines */
	PARTY_NAMES = SOCIAL_PARTY_LARGEST,
	/* a Delta profile's name (profile_names.c's longest), as menu_functions.c has it */
	PROFILE_NAME_LENGTH = 33,
	/* the band's and the bar's places (lobby_layout.h) */
	TITLE_Y = 45,
	KICKER_Y = 20,
	PROMPTS_RIGHT_EDGE = 32,
};

#define NAME_SIZE 10.0f
#define SUB_SIZE 7.5f
#define RIGHT_SIZE 7.5f
#define HEAD_SIZE 8.0f
#define MENU_SIZE 11.0f
#define NOTE_SIZE 8.0f
#define FACT_SIZE 9.0f
#define STATUS_SIZE 8.5f
#define PROMPT_BUTTON 18.0f
#define PROMPT_TEXT 13.0f
#define PROMPT_GAP 14.0f
#define DIALOG_SIZE 9.5f

/* the screen's colors (0xRRGGBBAA): Online Games' blues */
enum
{
	COLOR_TITLE = 0x3D8BFFFF,
	COLOR_KICKER = 0x7CC0FFFF,
	COLOR_TEXT = 0xFFFFFFFF,
	COLOR_HEAD = 0x8FA6C8FF,
	COLOR_DIM = 0x8FA6C8FF,
	COLOR_LABEL = 0x7FB8FFFF,
	COLOR_ROW_FILL = 0x0A1E5070,
	COLOR_SELECTION_FILL = 0x2A62C850,
	COLOR_SELECTION_EDGE = 0x6FB2FFFF,
	COLOR_OPEN = 0x5F86B3A0,
	COLOR_RED_TEAM = 0xD8423AFF,
	COLOR_BLUE_TEAM = 0x3A7FF0FF,
	COLOR_READY = 0x7CFF9AFF,
	COLOR_NOT_READY = 0x8FA6C8FF,
	COLOR_CROWN = 0xFFD34DFF,
	COLOR_DELTA = 0x7CC0FFFF,
	COLOR_VOICE = 0x7CFF9AFF,
	COLOR_MUTED = 0xB08A6EFF,
	COLOR_MENU = 0x4AA3FFFF,
	COLOR_MENU_OFF = 0x4A5E80FF,
	COLOR_PROMPT = 0x4AA3FFFF,
	COLOR_PROMPT_OFF = 0x4A5E80FF,
	COLOR_BUTTON_OFF = 0xFFFFFF55,
	COLOR_WARN = 0xF0A070FF,
	COLOR_SHADE = 0x02081AA0,
	COLOR_PANEL = 0x061430F0,
	COLOR_PANEL_EDGE = 0x3D8BFFA0,
	COLOR_HALO = 0x000814FF,
	COLOR_CARD_SHADE = 0x000021C8,
	COLOR_BADGE = 0xE8C766FF,
};

/* the players' armor colors (the profile's color index), near the game's */
static unsigned int const armor_colors[] =
{
	0xF0F0F0FF, 0x2A2A2AFF, 0xC83232FF, 0x3264C8FF, 0x8A8A8AFF, 0xE6C832FF, 0x3CA03CFF, 0xE680B4FF, 0x7A3CA0FF,
	0x32C8DCFF, 0x2850A0FF, 0xE68A32FF, 0x2A8A82FF, 0x8AA070FF, 0x7A5030FF, 0xC8B082FF, 0x7A2030FF, 0xE68A7AFF,
};

/* the game's button for each of the overlay's glyphs (UI_BUTTON_A...) */
static short const glyph_buttons[NUMBER_OF_UI_BUTTONS] =
{
	_gamepad_analog_button_a, _gamepad_analog_button_b, _gamepad_analog_button_x, _gamepad_analog_button_y,
	_gamepad_binary_button_start, _gamepad_analog_button_left_trigger, _gamepad_analog_button_right_trigger,
	_gamepad_analog_button_white, _gamepad_analog_button_black, _gamepad_binary_button_dpad_left,
	_gamepad_binary_button_dpad_right, _gamepad_binary_button_back,
};

/* ---------- the screen's state */

struct lobby_row
{
	char name[64];
	char sub[32];
	short player_index;
	short machine_index;
	short controller_index;
	short team;
	short platform;
	short color_index;
	short score;
	boolean has_score;
	boolean verified;
	boolean host;
	boolean you;
	boolean local;
	boolean talking;
	boolean muted;
	boolean ready;
	boolean delta;
};

struct lobby_menu_row
{
	short action;
	char label[32];
	char note[40];
	boolean on;
};

struct lobby_fact
{
	char key[24];
	char value[40];
};

struct lobby_target
{
	float x0, y0, x1, y1;
	/* a button's prompt (the game's button, NONE for none), a row (its
	list order, NONE), or a menu row (its index, NONE) */
	short button;
	short row;
	short menu;
};

static struct
{
	/* the lobby covers the pregame screen now; when its widget was last
	seen, and the widget (B leaves through it); when it opened */
	boolean covering;
	unsigned long seen_time;
	struct widget_instance *widget;
	unsigned long opened_time;
	short state;
	/* the game as last read */
	struct lobby_row rows[MAXIMUM_ROWS];
	short count, red, blue;
	short maximum_players;
	boolean teams;
	boolean has_ready;
	char map[128];
	char map_name[64];
	char type_name[40];
	char title[64];
	char kicker[24];
	char host_name[48];
	char chip[48];
	char badge[32];
	char status[160];
	short seconds;
	struct lobby_fact facts[MAXIMUM_FACTS];
	short fact_count;
	struct lobby_menu_row menu[MAXIMUM_MENU];
	short menu_count;
	/* what is selected: the pane, the list's row (its order), the menu's */
	short pane;
	short selected;
	short menu_selected;
	int first[LOBBY_LAYOUT_COLUMNS];
	struct lobby_layout layout;
	struct lobby_layout_inputs inputs;
	/* the host: whether START GAME let the countdown run, who said READY
	(by player list index), and whether its countdown was held when the
	lobby opened; the client: its own READY, when it was last said, and
	the host's state as last heard */
	boolean started;
	boolean held;
	unsigned char ready[MAXIMUM_ROWS];
	/* (the host: which players' machines said anything, READY or not, in
	this lobby; the client: whether it said its state in this lobby) */
	unsigned char said[MAXIMUM_ROWS];
	boolean announced[LOCAL_PLAYERS];
	/* (each of this machine's players: a split screen's guests ready up
	on their own controllers) */
	boolean my_ready[LOCAL_PLAYERS];
	unsigned long ready_said_time[LOCAL_PLAYERS];
	struct delta_wire_lobby_state host_state;
	unsigned int host_state_generation;
	/* a dialog over the screen, its row and its selected line */
	short dialog;
	short dialog_row;
	short dialog_selected;
	/* the pointer's targets, as this frame drew them */
	struct lobby_target targets[MAXIMUM_TARGETS];
	short target_count;
	/* the preview (a game under way, before joining), its answer, and what
	it shows */
	boolean preview;
	int preview_answer;
	struct browser_game preview_game;
	boolean preview_listed;
	/* the preview's SELECT PROFILE is up (the preview waits under it),
	its last A was pressed, and each controller's join as it opened */
	boolean adding;
	boolean adding_done;
	boolean added_joined[LOCAL_PLAYERS];
	long added_profile_index[LOCAL_PLAYERS];
	struct player_profile added_profile[LOCAL_PLAYERS];
	/* the controller driving the preview (its last press or step, not
	another controller's START), and whether that was the keyboard or
	mouse (controller 1's too): the player in front of it */
	short navigator;
	boolean navigator_keyboard;
	/* debug.lobby_preview's sample lobby */
	boolean debug;
	boolean debug_done;
	/* the next lobby is a game joined from the game list; B left for the
	Multiplayer menu's ONLINE GAMES (the list opens again over it), when */
	boolean from_list;
	boolean back_to_list;
	unsigned long back_time;
	/* the party's and the friends' names, for the rows' lines */
	char party_names[PARTY_NAMES][SOCIAL_HANDLE_SIZE];
	short party_count;
	int party_serial;
	struct social_state social;
	int social_serial;
	unsigned long social_time;
	/* a gamepad here may join with START: the first open slot says so */
	boolean join_hint;
} lobby;

/* ---------- small helpers */

/* a platform's name as Online Games' details say it (delta.h's table) */
static char const *delta_platform_label(int platform)
{
#define PLATFORM_LABEL(number, key, tag, name, capitals) case number: return name;
	switch (platform)
	{
	DELTA_PLATFORM_NAMES(PLATFORM_LABEL)
	default: return "";
	}
#undef PLATFORM_LABEL
}

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

static void utf8_from_utf16(char *text, int size, unsigned short const *wide, int length)
{
	wchar_t copy[64];
	int index;

	for (index = 0; index < length && index < (int)NUMBEROF(copy) - 1 && wide[index]; index++)
		copy[index] = (wchar_t)wide[index];
	copy[index] = 0;
	utf8_from_wide(text, size, copy, index);
}

static void copy_text(char *text, int size, char const *source)
{
	snprintf(text, (size_t)size, "%s", source ? source : "");
}

static void upper_text(char *text)
{
	for (; *text; text++)
	{
		if (*text >= 'a' && *text <= 'z')
			*text = (char)(*text - 32);
	}
}

static int same_name(char const *a, char const *b)
{
	if (!a[0] || !b[0])
		return 0;
	for (; *a && *b; a++, b++)
	{
		char x = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a;
		char y = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;

		if (x != y)
			return 0;
	}
	return *a == *b;
}

static float measure(void *context, float size, char const *text)
{
	(void)context;
	return ui_overlay_text_width(UI_FONT_MENU, size, text);
}

/* text on one line in width, cut with "..." when it must */
static char const *fitted(char const *text, float width, float size, char *out, int out_size)
{
	guide_layout_fit(text, width, size, measure, NULL, 0, out, out_size);
	return out;
}

static boolean lobby_enabled(void)
{
	return pc_menus_chupathingy() && config_boolean("display.lobby");
}

/* the controllers joining multiplayer from here (player 1's too) */
static short joining_count(void)
{
	short controller, count = 0;

	for (controller = 0; controller < LOCAL_PLAYERS; controller++)
		count += player_ui_local_player_wants_to_play_multiplayer(controller) ? 1 : 0;
	return count;
}

/* "P1 name, P2 name": the players joining from here, on their profiles */
static void joining_text(char *text, int size)
{
	short controller;
	int used = 0;

	text[0] = 0;
	for (controller = 0; controller < LOCAL_PLAYERS && used < size - 1; controller++)
	{
		struct player_profile profile;
		char name[48];

		if (!player_ui_local_player_wants_to_play_multiplayer(controller))
			continue;
		player_ui_get_active_player_profile(controller, &profile);
		utf8_from_wide(name, sizeof(name), profile.player_name, (int)NUMBEROF(profile.player_name));
		used += snprintf(text + used, (size_t)(size - used), "%sP%d %s", used ? ", " : "", controller + 1,
			name[0] ? name : "(no name)");
	}
}

/* the host's countdown waits for START GAME (network.lobby_start "button",
the default), or runs by itself as the Xbox's does ("auto"; the automated
tests' host too, which nobody presses START for) */
static boolean start_by_button(void)
{
	char const *setting = config_string("network.lobby_start");
	char const *test = config_string("debug.network_test");

	if (test && test[0])
		return FALSE;
	return !setting || strcmp(setting, "auto") != 0;
}

/* whether a screen drawn after this one covers it */
static boolean covered(void)
{
#ifdef HALO_GUIDE
	if (guide_active())
		return TRUE;
#endif
#ifndef HALO_CONSOLE_NO_DELTA_PROFILE
	if (profile_screen_active())
		return TRUE;
#endif
#ifndef HALO_CONSOLE_NO_CONTROLS
	if (controls_screen_active())
		return TRUE;
#endif
	/* (the lobby's own dialogs too) */
	return moderation_screen_active() || invite_prompt_active() || lobby.dialog != _dialog_none;
}

static void add_target(float x0, float y0, float x1, float y1, short button, short row, short menu)
{
	if (lobby.target_count < MAXIMUM_TARGETS)
	{
		struct lobby_target *target = &lobby.targets[lobby.target_count++];

		target->x0 = x0;
		target->y0 = y0;
		target->x1 = x1;
		target->y1 = y1;
		target->button = button;
		target->row = row;
		target->menu = menu;
	}
}

static struct network_game *current_game(void)
{
	struct network_game_client *client = global_network_game_client_get();
	short state_data;

	if (!client || network_game_client_get_state(client, &state_data) != CLIENT_STATE_PREGAME)
		return NULL;
	return network_game_client_get_game(client);
}

/* ---------- the party's and the friends' names (the Guide's: browser.c) */

static void social_read(void)
{
#ifndef HALO_CONSOLE_NO_GUIDE
	unsigned long now = system_milliseconds();
	struct social_party party;
	int serial;

	if (lobby.social_time && now - lobby.social_time < 1000)
		return;
	lobby.social_time = now;
	serial = lobby.party_serial;
	browser_party_get(&party, &serial);
	if (serial != lobby.party_serial)
	{
		short index;

		lobby.party_serial = serial;
		lobby.party_count = 0;
		for (index = 0; party.has && index < party.member_count && index < PARTY_NAMES; index++)
		{
			copy_text(lobby.party_names[lobby.party_count], SOCIAL_HANDLE_SIZE,
				party.members[index].handle[0] ? party.members[index].handle : party.members[index].name);
			lobby.party_count++;
		}
	}
	serial = lobby.social_serial;
	browser_social_get(&lobby.social, &serial);
	lobby.social_serial = serial;
#endif
	/* (a build without the Guide, HALO_CONSOLE_NO_GUIDE, has no party or
	friends to read: browser.c leaves them out) */
}

static boolean in_party(char const *name)
{
	short index;

	for (index = 0; index < lobby.party_count; index++)
	{
		if (same_name(lobby.party_names[index], name))
			return TRUE;
	}
	return FALSE;
}

static boolean is_friend(char const *name)
{
	int index;

	for (index = 0; index < lobby.social.friend_count && index < SOCIAL_MAXIMUM_FRIENDS; index++)
	{
		if (same_name(lobby.social.friends[index].handle, name) || same_name(lobby.social.friends[index].display, name))
			return TRUE;
	}
	return FALSE;
}

/* ---------- the host's READY (Delta: lobby_ready) */

static int host_ready_said(void *context, int machine_index, int player, int ready)
{
	struct network_game *game = network_game_get_game();

	(void)context;
	if (!game || player < 0 || player >= (int)NUMBEROF(game->players) ||
		!network_player_is_valid(&game->players[player]) ||
		(short)game->players[player].machine_index != (short)machine_index)
	{
		return 0;
	}
	lobby.ready[player] = (unsigned char)(ready ? 1 : 0);
	lobby.said[player] = 1;
	return 1;
}

static const struct delta_peer_lobby_host lobby_host = { NULL, host_ready_said };

/* whether a player of the game counts as ready, on the host: said so, on
this machine (the host starts), or on a machine without Delta's READY
(OpenCE's, an older build, a console without it) */
static boolean host_player_ready(struct network_game const *game, short player_index)
{
	short machine = game->players[player_index].machine_index;

	if ((short)machine == network_game_client_get_local_machine_index())
		return TRUE;
	if (lobby.ready[player_index])
		return TRUE;
	/* (a machine that never said anything in this lobby: one without this
	lobby up, the Xbox menus' or display.lobby off, counts as ready, as one
	without Delta's READY does; a lobby says its state as it opens) */
	{
		long index;

		for (index = 0; index < (long)NUMBEROF(game->players); index++)
		{
			if (lobby.said[index] && network_player_is_valid(&game->players[index]) &&
				(short)game->players[index].machine_index == (short)machine)
			{
				return FALSE;
			}
		}
	}
	return TRUE;
}

/* the host's state for the clients, each frame it hosts */
static void host_state_send(struct network_game const *game)
{
	struct delta_wire_lobby_state state;
	long index;

	memset(&state, 0, sizeof(state));
	state.flags = (unsigned char)(DELTA_WIRE_LOBBY_WAITS | (lobby.started ? DELTA_WIRE_LOBBY_STARTED : 0));
	state.generation = 1;
	for (index = 0; game && index < (long)NUMBEROF(game->players) && index < DELTA_WIRE_LOBBY_PLAYERS; index++)
	{
		if (network_player_is_valid(&game->players[index]) && host_player_ready(game, (short)index))
			delta_wire_lobby_state_set(&state, (int)index, 1);
	}
	delta_peer_game_set_lobby_state(&state);
}

/* a client's: whether a player is ready by the host's state (everyone
when the host says nothing) */
static boolean client_player_ready(short player_index)
{
	if (!lobby.host_state_generation)
		return TRUE;
	return delta_wire_lobby_state_ready(&lobby.host_state, player_index) != 0;
}

/* this machine's player of a controller in the game (its player list
index), or NONE */
static short local_player_of(struct network_game const *game, short controller)
{
	short local = network_game_client_get_local_machine_index();
	long index;

	for (index = 0; game && local != NONE && index < (long)NUMBEROF(game->players); index++)
	{
		if (network_player_is_valid(&game->players[index]) && (short)game->players[index].machine_index == local &&
			game->players[index].controller_index == controller)
		{
			return (short)index;
		}
	}
	return NONE;
}

/* this machine's first player */
static short local_player_index(struct network_game const *game)
{
	return local_player_of(game, 0);
}

/* this machine's players (split screen: more than one) */
static short machine_player_count(struct network_game const *game)
{
	short controller, count = 0;

	for (controller = 0; controller < LOCAL_PLAYERS; controller++)
		count += local_player_of(game, controller) != NONE ? 1 : 0;
	return count;
}

/* the controller of this machine's first player (0 with none) */
static short first_local_controller(struct network_game const *game)
{
	short controller;

	for (controller = 0; controller < LOCAL_PLAYERS; controller++)
	{
		if (local_player_of(game, controller) != NONE)
			return controller;
	}
	return 0;
}

/* split screen: a gamepad here with no player in the game, which joins
with START (the Xbox pregame screen's own), while the game has room */
static boolean controller_can_join(struct network_game const *game)
{
	short controller;

	if (!game || lobby.state == _lobby_preview || lobby.count >= lobby.maximum_players)
		return FALSE;
	for (controller = 1; controller < LOCAL_PLAYERS; controller++)
	{
		if (input_has_gamepad(controller) && local_player_of(game, controller) == NONE &&
			!player_ui_local_player_wants_to_play_multiplayer(controller))
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* the client's READY, said to the host and said again while its state
doesn't show it */
static void client_ready_frame(struct network_game const *game)
{
	unsigned long now = system_milliseconds();
	short controller;

	lobby.host_state_generation = delta_peer_game_lobby_state(&lobby.host_state);
	lobby.has_ready = delta_peer_game_lobby_ready() != 0;
	if (!lobby.has_ready)
		return;
	for (controller = 0; controller < LOCAL_PLAYERS; controller++)
	{
		short player = local_player_of(game, controller);

		if (player == NONE)
			continue;
		/* (said once as the lobby opens, NOT READY too, so the host counts
		this machine's READY and not it as a machine without the lobby) */
		if (!lobby.announced[controller])
		{
			delta_peer_game_lobby_say(player, lobby.my_ready[controller]);
			lobby.announced[controller] = TRUE;
			lobby.ready_said_time[controller] = now;
			continue;
		}
		/* (said again while the host's state doesn't show it: ready and not
		shown, or shown ready after saying not any more) */
		if (lobby.host_state_generation)
		{
			if ((delta_wire_lobby_state_ready(&lobby.host_state, player) != 0) == (lobby.my_ready[controller] != FALSE))
				continue;
		}
		else if (!lobby.my_ready[controller])
			continue;
		if (lobby.ready_said_time[controller] && now - lobby.ready_said_time[controller] < READY_RESEND)
			continue;
		delta_peer_game_lobby_say(player, lobby.my_ready[controller]);
		lobby.ready_said_time[controller] = now;
	}
}

/* ---------- reading the game */

static short compare_rows(struct lobby_row const *a, struct lobby_row const *b)
{
	if (lobby.teams && a->team != b->team)
		return a->team < b->team ? -1 : 1;
	if (a->has_score && b->has_score && a->score != b->score)
		return a->score > b->score ? -1 : 1;
	return a->player_index < b->player_index ? -1 : a->player_index > b->player_index ? 1 : 0;
}

static void sort_rows(void)
{
	short index, other;

	for (index = 1; index < lobby.count; index++)
	{
		struct lobby_row row = lobby.rows[index];

		for (other = index; other > 0 && compare_rows(&lobby.rows[other - 1], &row) > 0; other--)
			lobby.rows[other] = lobby.rows[other - 1];
		lobby.rows[other] = row;
	}
	lobby.red = lobby.blue = 0;
	for (index = 0; lobby.teams && index < lobby.count; index++)
	{
		if (lobby.rows[index].team)
			lobby.blue++;
		else
			lobby.red++;
	}
}

/* the line under a name: the team first, then one tag (the host has its
crown beside the name) */
static void row_sub_line(struct lobby_row *row)
{
	char const *team = lobby.teams ? (row->team ? "Blue team" : "Red team") : "";
	char tag[32] = "";

	if (row->you)
		copy_text(tag, sizeof(tag), "You");
	else if (row->local)
		snprintf(tag, sizeof(tag), "Player %d here", row->controller_index + 1);
	else if (in_party(row->name))
		copy_text(tag, sizeof(tag), "Party");
	else if (is_friend(row->name))
		copy_text(tag, sizeof(tag), "Friend");
	else if (!row->delta && lobby.state != _lobby_preview && !row->host)
		copy_text(tag, sizeof(tag), "Legacy");
	if (team[0] && tag[0])
		snprintf(row->sub, sizeof(row->sub), "%s  /  %s", team, tag);
	else
		snprintf(row->sub, sizeof(row->sub), "%s", team[0] ? team : tag);
}

static void read_type(struct network_game const *game)
{
	char text[64];

	utf8_from_wide(text, sizeof(text), game->variant.human_readable_game_description,
		(int)NUMBEROF(game->variant.human_readable_game_description));
	/* (a built-in game type has no name of its own: the engine's then, not
	the name helper's stand-in for a player's) */
	lobby.type_name[0] = 0;
	if (text[0])
		ui_overlay_name_shown(UI_FONT_MENU, text, lobby.type_name, sizeof(lobby.type_name));
	if (!lobby.type_name[0])
	{
		static char const *const engines[] = { "", "CTF", "Slayer", "Oddball", "King of the Hill", "Race", "Bomb" };
		long engine = game->variant.game_engine_index;

		copy_text(lobby.type_name, sizeof(lobby.type_name), engine > 0 && engine < (long)NUMBEROF(engines) ?
			engines[engine] : !engine ? "Co-op" : "New game type");
		if (engine == 2 && game->variant.universal_variant.teams)
			copy_text(lobby.type_name, sizeof(lobby.type_name), "Team Slayer");
	}
	upper_text(lobby.type_name);
}

static void fact_set(char const *key, char const *value)
{
	struct lobby_fact *fact;

	if (lobby.fact_count >= MAXIMUM_FACTS)
		return;
	fact = &lobby.facts[lobby.fact_count++];
	copy_text(fact->key, sizeof(fact->key), key);
	copy_text(fact->value, sizeof(fact->value), value);
}

/* a co-op game's difficulty (the game's 0 Easy to 3 Legendary), or NULL */
static char const *coop_difficulty_name(long difficulty)
{
	static char const *const difficulties[] = { "Easy", "Normal", "Heroic", "Legendary" };

	return difficulty >= 0 && difficulty < (long)NUMBEROF(difficulties) ? difficulties[difficulty] : NULL;
}

/* a co-op game's facts: its level and difficulty (it has no score, time
limit or teams) */
static void coop_facts(char const *map, long difficulty)
{
	char value[64];

	lobby.fact_count = 0;
	fact_set("Level", browser_screen_map_name(map, value, sizeof(value)));
	if (coop_difficulty_name(difficulty))
		fact_set("Difficulty", coop_difficulty_name(difficulty));
}

static void read_facts(struct network_game const *game)
{
	char value[40];
	long score = game->variant.universal_variant.score_to_win;
	short minutes = game->variant_options.time_limit;

	/* (engine 0: a co-op game, on a campaign level) */
	if (!game->variant.game_engine_index)
	{
		coop_facts(lobby.map, game->difficulty);
		return;
	}
	lobby.fact_count = 0;
	if (score > 0)
	{
		snprintf(value, sizeof(value), "%ld%s", score, game->variant.game_engine_index == 2 ? " kills" :
			game->variant.game_engine_index == 1 ? " captures" : "");
	}
	else
		copy_text(value, sizeof(value), "None");
	fact_set("Score to win", value);
	if (minutes > 0)
		snprintf(value, sizeof(value), "%d minutes", minutes);
	else
		copy_text(value, sizeof(value), "None");
	fact_set("Time limit", value);
	fact_set("Teams", lobby.teams ? "Red vs Blue" : "Free for all");
}

/* the controller of the host's own player, who has the crown: the host
machine's first controller's, not a split-screen guest's; NONE with none */
static short host_player_controller(struct network_game const *game, short host_machine)
{
	short controller = NONE;
	long index;

	for (index = 0; index < (long)NUMBEROF(game->players); index++)
	{
		struct network_player const *player = &game->players[index];

		if (network_player_is_valid(player) && (short)player->machine_index == host_machine &&
			(controller == NONE || (short)player->controller_index < controller))
		{
			controller = (short)player->controller_index;
		}
	}
	return controller;
}

/* the rows from the game's player list */
static void read_players(struct network_game const *game, short local_machine, short host_machine, boolean hosting)
{
	long index;
	short host_controller = host_player_controller(game, host_machine);

	lobby.count = 0;
	for (index = 0; index < (long)NUMBEROF(game->players) && lobby.count < MAXIMUM_ROWS; index++)
	{
		struct network_player const *player = &game->players[index];
		struct lobby_row *row;
		wchar_t name[PROFILE_NAME_LENGTH > NUMBEROF(player->name) + 1 ? PROFILE_NAME_LENGTH : NUMBEROF(player->name) + 1];
		char text[64];

		if (!network_player_is_valid(player))
			continue;
		row = &lobby.rows[lobby.count++];
		memset(row, 0, sizeof(*row));
		row->player_index = (short)index;
		row->machine_index = player->machine_index;
		row->controller_index = player->controller_index;
		row->team = (short)(lobby.teams ? (player->team_index ? 1 : 0) : 0);
		row->color_index = player->primary_color_index;
		ustrncpy(name, player->name, NUMBEROF(player->name));
		name[NUMBEROF(player->name)] = 0;
#ifndef HALO_CONSOLE_NO_DELTA_PROFILE
		if (profile_names_player(index, name, PROFILE_NAME_LENGTH))
			row->verified = profile_names_verified(index);
#endif
		utf8_from_wide(text, sizeof(text), name, (int)ustrlen(name));
		ui_overlay_name_shown(UI_FONT_MENU, text, row->name, sizeof(row->name));
		if (!row->name[0])
		{
			/* (a name the font can't show, or none: the machine's) */
			if ((short)row->machine_index >= 0 && (short)row->machine_index < (short)NUMBEROF(game->machines))
			{
				utf8_from_wide(text, sizeof(text), game->machines[(short)row->machine_index].name,
					(int)NUMBEROF(game->machines[0].name));
				ui_overlay_name_shown(UI_FONT_MENU, text, row->name, sizeof(row->name));
			}
			if (!row->name[0])
				snprintf(row->name, sizeof(row->name), "Player %ld", index + 1);
		}
		row->platform = (short)delta_peer_player_platform((int)index);
		row->delta = row->platform != _delta_platform_unknown ||
			delta_peer_game_player_has((int)index, _delta_capability_platform) != 0;
		row->host = (short)row->machine_index == host_machine && (short)row->controller_index == host_controller;
		row->local = (short)row->machine_index == local_machine;
		row->you = row->local && row->controller_index == 0;
		row->muted = chat_local_muted(row->machine_index, player->name);
#ifdef HALO_PORT_VOICE
		row->talking = !row->muted && voice_game_machine_talking(row->machine_index);
#endif
		row->ready = hosting ? host_player_ready(game, (short)index) : client_player_ready((short)index);
		row_sub_line(row);
	}
	sort_rows();
}

/* the title (the game's name), the host's name, the chip at the right */
static void read_band(struct network_game const *game, boolean hosting)
{
	char text[64];
	short index;

	utf8_from_wide(text, sizeof(text), game->name, (int)NUMBEROF(game->name));
	ui_overlay_name_shown(UI_FONT_MENU, text, lobby.title, sizeof(lobby.title));
	if (!lobby.title[0])
		copy_text(lobby.title, sizeof(lobby.title), "GAME LOBBY");
	upper_text(lobby.title);
	lobby.host_name[0] = 0;
	for (index = 0; index < lobby.count; index++)
	{
		if (lobby.rows[index].host)
		{
			copy_text(lobby.host_name, sizeof(lobby.host_name), lobby.rows[index].name);
			break;
		}
	}
	lobby.badge[0] = 0;
	if (lobby.state == _lobby_server)
	{
		struct server_badge badge;

		copy_text(lobby.kicker, sizeof(lobby.kicker), "DEDICATED SERVER");
		if (delta_peer_game_server_badge(&badge) > 0 && browser_badge_name(badge.tier))
		{
			copy_text(lobby.badge, sizeof(lobby.badge), browser_badge_name(badge.tier));
			upper_text(lobby.badge);
		}
	}
	else
		copy_text(lobby.kicker, sizeof(lobby.kicker), hosting ? "HOSTED GAME" : "JOINED GAME");
	lobby.chip[0] = 0;
}

static void menu_add(short action, char const *label, char const *note, boolean on)
{
	struct lobby_menu_row *row;

	if (lobby.menu_count >= MAXIMUM_MENU)
		return;
	row = &lobby.menu[lobby.menu_count++];
	row->action = action;
	copy_text(row->label, sizeof(row->label), label);
	copy_text(row->note, sizeof(row->note), note);
	row->on = on;
}

static short not_ready_count(void)
{
	short index, count = 0;

	for (index = 0; index < lobby.count; index++)
		count += lobby.rows[index].ready ? 0 : 1;
	return count;
}

static void read_menu(struct network_game_server *server)
{
	char note[40];
	short not_ready = not_ready_count();

	lobby.menu_count = 0;
	note[0] = 0;
	switch (lobby.state)
	{
	case _lobby_hosted:
		if (lobby.started)
		{
			/* (A shortens the countdown, as the Xbox's did; cancelling is
			the row below, and only while the lobby is still open: once
			the game has begun loading, the start can't be taken back) */
			boolean open = !server || network_game_server_lobby_is_open(server);

			menu_add(_menu_start_sooner, "START SOONER", "", open);
			menu_add(_menu_cancel_start, "CANCEL START", "", open);
		}
		else
		{
			if (not_ready)
				snprintf(note, sizeof(note), "%d not ready", not_ready);
			else if (server && !server_ok_to_countdown(server))
				copy_text(note, sizeof(note), lobby.count < 2 ? "Waiting for players" : "Teams");
			menu_add(_menu_start, "START GAME", note, TRUE);
		}
		menu_add(_menu_change_map, "CHANGE MAP", "", !lobby.started);
		menu_add(_menu_game_setup, "GAME SETUP", "", !lobby.started);
		menu_add(_menu_invite, "INVITE FRIENDS", "", TRUE);
		break;
	case _lobby_joined:
		if (lobby.has_ready)
			menu_add(_menu_ready, "READY", lobby.my_ready[0] ? "Ready" : "Not ready", TRUE);
		else
			menu_add(_menu_start_sooner, "START SOONER", "", TRUE);
		if (lobby.teams)
		{
			short me = local_player_index(network_game_get_game());
			struct network_game const *game = network_game_get_game();

			copy_text(note, sizeof(note), me != NONE && game && game->players[me].team_index ? "Blue" : "Red");
			menu_add(_menu_change_team, "CHANGE TEAM", note, TRUE);
		}
		menu_add(_menu_details, "GAME DETAILS", "", TRUE);
		menu_add(_menu_invite, "INVITE FRIENDS", "", TRUE);
		break;
	case _lobby_server:
		if (lobby.teams)
		{
			short me = local_player_index(network_game_get_game());
			struct network_game const *game = network_game_get_game();

			copy_text(note, sizeof(note), me != NONE && game && game->players[me].team_index ? "Blue" : "Red");
			menu_add(_menu_change_team, "CHANGE TEAM", note, TRUE);
		}
		menu_add(_menu_details, "GAME DETAILS", "", TRUE);
		menu_add(_menu_invite, "INVITE FRIENDS", "", TRUE);
		break;
	case _lobby_preview:
		menu_add(_menu_join, "JOIN THE GAME", lobby.preview_game.open ? "" : "Full", lobby.preview_game.open);
		if (joining_count() > 1)
			snprintf(note, sizeof(note), "%d players", joining_count());
		menu_add(_menu_add_players, "ADD PLAYERS", note, lobby.preview_game.open);
		menu_add(_menu_details, "GAME DETAILS", "", TRUE);
		break;
	default:
		break;
	}
	if (lobby.menu_selected >= lobby.menu_count)
		lobby.menu_selected = (short)MAX(0, lobby.menu_count - 1);
}

static void read_status(struct network_game_server *server)
{
	char hosting[192];

	if (lobby.seconds > 0)
	{
		snprintf(lobby.status, sizeof(lobby.status), "Starting on %s", lobby.map_name);
		return;
	}
	switch (lobby.state)
	{
	case _lobby_hosted:
		if (server && p2p_hosting_status(hosting, sizeof(hosting)) && hosting[0])
			copy_text(lobby.status, sizeof(lobby.status), hosting);
		else if (lobby.started)
			copy_text(lobby.status, sizeof(lobby.status), lobby.count < 2 ? "Waiting for players" : "Starting...");
		else if (not_ready_count())
			snprintf(lobby.status, sizeof(lobby.status), "%d not ready", not_ready_count());
		else
			copy_text(lobby.status, sizeof(lobby.status), lobby.count < 2 ? "Waiting for players" : "Everyone is ready");
		break;
	case _lobby_joined:
		copy_text(lobby.status, sizeof(lobby.status), lobby.has_ready && !lobby.my_ready[0] ? "You are not ready" :
			"Waiting for the host");
		break;
	case _lobby_server:
		copy_text(lobby.status, sizeof(lobby.status), "Waiting for the next game");
		break;
	case _lobby_preview:
		if (joining_count() > 1)
		{
			char joining[120];

			joining_text(joining, sizeof(joining));
			snprintf(lobby.status, sizeof(lobby.status), "Joining together: %s", joining);
		}
		else
		{
			copy_text(lobby.status, sizeof(lobby.status), lobby.preview_game.open ?
				"Game in progress: join now, or wait for the next one" : "Game in progress and full");
		}
		break;
	default:
		lobby.status[0] = 0;
		break;
	}
}

/* everything from the game, each frame the lobby covers the pregame screen */
static void read_game(void)
{
	struct network_game_client *client = global_network_game_client_get();
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = current_game();
	short local = network_game_client_get_local_machine_index();
	short host_machine = 0, host_players = 0, index;
	boolean hosting = server != NULL;

	if (!game)
	{
		lobby.count = 0;
		lobby.menu_count = 0;
		return;
	}
	/* (back from a game: the game type the host chose, not none) */
	if (hosting)
		network_game_server_keep_menus_variant(server);
	{
		static short logged_players = NONE;
		short players = machine_player_count(game);

		if (players != logged_players)
		{
			platform_log("split screen lobby: the host has %d player(s) of this machine (P1 %s, P2 %s, P3 %s, P4 %s)",
				players, local_player_of(game, 0) != NONE ? "in" : "out", local_player_of(game, 1) != NONE ? "in" : "out",
				local_player_of(game, 2) != NONE ? "in" : "out", local_player_of(game, 3) != NONE ? "in" : "out");
			logged_players = players;
		}
	}
	social_read();
	lobby.teams = game->variant.universal_variant.teams != 0;
	lobby.maximum_players = (short)game->maximum_players;
	/* (the game's map as the game's protocol names it, custom_maps\name and
	so on, made this port's name, name@ce, for the pictures and names) */
	map_family_from_wire_name(game->map.name, lobby.map, sizeof(lobby.map));
	browser_screen_map_name(lobby.map, lobby.map_name, sizeof(lobby.map_name));
	upper_text(lobby.map_name);
	read_type(game);
	/* (the host is machine 0; one with no player is a dedicated server) */
	for (index = 0; index < (short)NUMBEROF(game->players); index++)
	{
		if (network_player_is_valid(&game->players[index]) && (short)game->players[index].machine_index == host_machine)
			host_players++;
	}
	lobby.state = hosting ? _lobby_hosted : host_players ? _lobby_joined : _lobby_server;
	if (hosting)
		lobby.has_ready = TRUE;
	else
		client_ready_frame(game);
	read_players(game, local, host_machine, hosting);
	lobby.join_hint = controller_can_join(game);
	if (hosting)
		host_state_send(game);
	lobby.seconds = network_game_client_get_seconds_to_game_start(client);
	if (hosting && lobby.seconds > 0)
		lobby.started = TRUE;
	read_band(game, hosting);
	read_facts(game);
	read_menu(server);
	read_status(server);
	if (lobby.selected >= lobby.count)
		lobby.selected = (short)MAX(0, lobby.count - 1);
}

/* the preview's rows and card, from the list's roster and the host's
advertisement */
static void read_preview(void)
{
	struct browser_game const *game = &lobby.preview_game;
	char value[40], text[64];
	short index;

	lobby.teams = game->teams != 0;
	lobby.maximum_players = game->maximum_players;
	copy_text(lobby.map, sizeof(lobby.map), game->map);
	browser_screen_map_name(game->map, lobby.map_name, sizeof(lobby.map_name));
	upper_text(lobby.map_name);
	network_rules_engine_name(game->engine, game->teams, lobby.type_name, sizeof(lobby.type_name));
	if (!lobby.type_name[0])
		copy_text(lobby.type_name, sizeof(lobby.type_name), game->difficulty >= 1 ? "Co-op" : "New game type");
	upper_text(lobby.type_name);
	lobby.count = 0;
	for (index = 0; index < game->roster_count && index < BROWSER_LISTED_ROSTER && lobby.count < MAXIMUM_ROWS; index++)
	{
		struct lobby_row *row = &lobby.rows[lobby.count++];

		memset(row, 0, sizeof(*row));
		row->player_index = index;
		row->machine_index = NONE;
		row->team = (short)(lobby.teams && game->roster[index].team > 0 ? 1 : 0);
		row->color_index = (short)(index % (short)NUMBEROF(armor_colors));
		row->delta = TRUE;
		utf8_from_utf16(text, sizeof(text), game->roster[index].name, BROWSER_PLAYER_NAME_LENGTH);
		ui_overlay_name_shown(UI_FONT_MENU, text, row->name, sizeof(row->name));
		if (!row->name[0])
			snprintf(row->name, sizeof(row->name), "Player %d", index + 1);
		row->ready = TRUE;
		row_sub_line(row);
	}
	sort_rows();
	utf8_from_utf16(text, sizeof(text), game->name, BROWSER_NAME_LENGTH);
	ui_overlay_name_shown(UI_FONT_MENU, text, lobby.title, sizeof(lobby.title));
	if (!lobby.title[0])
		copy_text(lobby.title, sizeof(lobby.title), "GAME IN PROGRESS");
	upper_text(lobby.title);
	copy_text(lobby.kicker, sizeof(lobby.kicker), game->hosting == BROWSER_HOSTING_DEDICATED ||
		game->hosting == BROWSER_HOSTING_OFFICIAL ? "DEDICATED SERVER" : "GAME IN PROGRESS");
	lobby.badge[0] = 0;
	if (game->badge && browser_badge_name(game->badge))
	{
		copy_text(lobby.badge, sizeof(lobby.badge), browser_badge_name(game->badge));
		upper_text(lobby.badge);
	}
	lobby.host_name[0] = 0;
	/* (the count is the list's head's alone) */
	if (game->ping >= 0)
		snprintf(lobby.chip, sizeof(lobby.chip), "PING %s%d", game->ping_estimated ? "~" : "", game->ping);
	else
		lobby.chip[0] = 0;
	lobby.fact_count = 0;
	/* (a co-op game: the list says its difficulty, 1 Easy to 4 Legendary) */
	if (game->difficulty >= 1 && coop_difficulty_name(game->difficulty - 1))
	{
		coop_facts(game->map, game->difficulty - 1);
		if (game->region[0])
			fact_set("Region", game->region);
		lobby.seconds = 0;
		read_menu(NULL);
		read_status(NULL);
		return;
	}
	if (game->score_limit > 0)
	{
		snprintf(value, sizeof(value), "%d", game->score_limit);
		fact_set("Score to win", value);
	}
	fact_set("Teams", lobby.teams ? "Red vs Blue" : "Free for all");
	if (game->region[0])
		fact_set("Region", game->region);
	lobby.seconds = 0;
	read_menu(NULL);
	read_status(NULL);
}

/* ---------- debug.lobby_preview's sample lobby */

static void debug_sample(char const *spec)
{
	static char const *const names[] = { "Sarge", "Simmons", "Grif", "Donut", "Church", "Tucker", "Caboose", "Tex",
		"Washington", "Wyoming", "Sister", "Doc", "Lopez", "Kai", "Grey", "Sheila", "Vic", "Andy", "Junior", "York",
		"North", "South", "Carolina", "Maine", "Florida", "Connecticut", "Wash", "Niner", "Price", "Delta", "Theta", "Sigma" };
	static short const platforms[] = { _delta_platform_pc_macos, _delta_platform_pc_windows, _delta_platform_steam_deck,
		_delta_platform_xbox, _delta_platform_pc_windows, _delta_platform_unknown, _delta_platform_android,
		_delta_platform_xbox360 };
	char kind[16];
	long players = 8;
	boolean long_names = strstr(spec, "long") != NULL, teams = strstr(spec, "ffa") == NULL;
	char const *colon = strchr(spec, ':');
	short index;

	snprintf(kind, sizeof(kind), "%.*s", (int)(colon ? colon - spec : (long)strlen(spec)), spec);
	if (colon)
		players = atol(colon + 1);
	players = PIN(players, 1, MAXIMUM_ROWS);
	lobby.state = !strcmp(kind, "joined") ? _lobby_joined : !strcmp(kind, "server") ? _lobby_server :
		!strcmp(kind, "preview") ? _lobby_preview : _lobby_hosted;
	lobby.teams = teams;
	lobby.maximum_players = (short)(players <= 16 ? 16 : players);
	copy_text(lobby.map, sizeof(lobby.map), "bloodgulch");
	/* (map=<the game's map>, as the game names it: custom_maps\name) */
	if (strstr(spec, "map="))
	{
		char const *start = strstr(spec, "map=") + 4;
		size_t length = strcspn(start, ":");

		char wire[128];

		snprintf(wire, sizeof(wire), "%.*s", (int)length, start);
		map_family_from_wire_name(wire, lobby.map, sizeof(lobby.map));
	}
	browser_screen_map_name(lobby.map, lobby.map_name, sizeof(lobby.map_name));
	upper_text(lobby.map_name);
	copy_text(lobby.type_name, sizeof(lobby.type_name), teams ? "TEAM SLAYER" : "SLAYER");
	lobby.count = 0;
	for (index = 0; index < players; index++)
	{
		struct lobby_row *row = &lobby.rows[lobby.count++];

		memset(row, 0, sizeof(*row));
		row->player_index = index;
		row->machine_index = index;
		row->team = (short)(teams ? index % 2 : 0);
		row->color_index = (short)(index % (short)NUMBEROF(armor_colors));
		row->platform = platforms[index % (short)NUMBEROF(platforms)];
		row->delta = row->platform != _delta_platform_unknown;
		row->verified = row->delta && (index % 3) != 2;
		row->host = index == 0 && lobby.state != _lobby_server && lobby.state != _lobby_preview;
		row->you = index == (lobby.state == _lobby_hosted ? 0 : 2);
		row->local = row->you;
		row->talking = index == 0 || index == 4;
		row->muted = index == 7;
		row->ready = (index % 4) != 2 || lobby.state == _lobby_preview;
		if (lobby.state == _lobby_preview)
		{
			row->has_score = TRUE;
			row->score = (short)((players - index) * 3 % 29);
		}
		if (long_names && index % 2 == 1)
			snprintf(row->name, sizeof(row->name), "%s_the_longest_name_here_%d", names[index % NUMBEROF(names)], index);
		else if (long_names && index == 0)
			copy_text(row->name, sizeof(row->name), "WWWWWWWWWWWWWWWWWWWWWWWW");
		else
			copy_text(row->name, sizeof(row->name), names[index % NUMBEROF(names)]);
		{
			char const *team = teams ? (row->team ? "Blue team" : "Red team") : "";
			char const *tag = row->you ? "You" : index == 1 ? "Party" : index == 3 ? "Friend" :
				!row->delta && !row->host ? "Legacy" : "";

			if (team[0] && tag[0])
				snprintf(row->sub, sizeof(row->sub), "%s  /  %s", team, tag);
			else
				snprintf(row->sub, sizeof(row->sub), "%s", team[0] ? team : tag);
		}
	}
	sort_rows();
	lobby.has_ready = lobby.state == _lobby_joined;
	memset(lobby.my_ready, 0, sizeof(lobby.my_ready));
	lobby.started = FALSE;
	/* (the host's name is its crowned row's) */
	lobby.host_name[0] = 0;
	for (index = 0; index < lobby.count; index++)
	{
		if (lobby.rows[index].host && (lobby.state == _lobby_hosted || lobby.state == _lobby_joined))
			copy_text(lobby.host_name, sizeof(lobby.host_name), lobby.rows[index].name);
	}
	switch (lobby.state)
	{
	case _lobby_hosted:
		copy_text(lobby.kicker, sizeof(lobby.kicker), "HOSTED GAME");
		copy_text(lobby.title, sizeof(lobby.title), "GAME LOBBY");
		break;
	case _lobby_joined:
		copy_text(lobby.kicker, sizeof(lobby.kicker), "JOINED GAME");
		copy_text(lobby.title, sizeof(lobby.title), "GAME LOBBY");
		break;
	case _lobby_server:
	case _lobby_preview:
		copy_text(lobby.kicker, sizeof(lobby.kicker), "DEDICATED SERVER");
		copy_text(lobby.title, sizeof(lobby.title), "[C] CHUPATHINGY");
		copy_text(lobby.badge, sizeof(lobby.badge), "OFFICIAL SERVER");
		break;
	default:
		break;
	}
	copy_text(lobby.chip, sizeof(lobby.chip), lobby.state == _lobby_preview ? "PING 37" : "");
	lobby.fact_count = 0;
	fact_set("Score to win", teams ? "50 kills" : "25 kills");
	fact_set("Time limit", "15 minutes");
	fact_set("Teams", teams ? "Red vs Blue" : "Free for all");
	memset(&lobby.preview_game, 0, sizeof(lobby.preview_game));
	lobby.preview_game.open = 1;
	lobby.seconds = strstr(spec, "countdown") ? 5 : 0;
	lobby.join_hint = strstr(spec, "join") != NULL && lobby.state != _lobby_preview;
	read_menu(NULL);
	read_status(NULL);
	if (lobby.state == _lobby_hosted && strstr(spec, "countdown"))
	{
		lobby.started = TRUE;
		read_menu(NULL);
	}
}

/* ---------- what is up */

boolean lobby_screen_preview_active(void)
{
	return lobby.preview;
}

boolean lobby_screen_active(void)
{
	return (lobby.covering || lobby.preview || lobby.debug) && !lobby.adding;
}

/* whether the widget up is the Xbox's pregame screen */
static boolean pregame_screen_up(void)
{
	struct widget_instance *top = ui_widget_port_top_screen();
	long pregame = tag_loaded(UI_WIDGET_DEFINITION_TAG, PREGAME_SCREEN);

	return top && pregame != NONE && top->definition_tag_index == pregame;
}

static void reset_selection(void)
{
	lobby.pane = _pane_menu;
	lobby.selected = 0;
	lobby.menu_selected = 0;
	memset(lobby.first, 0, sizeof(lobby.first));
	lobby.dialog = _dialog_none;
	lobby.target_count = 0;
}

/* a lobby opened: a new one, so READY and START begin again */
static void lobby_opened(void)
{
	struct network_game_server *server = global_network_game_server_get();

	reset_selection();
	lobby.opened_time = system_milliseconds();
	lobby.started = FALSE;
	lobby.held = FALSE;
	memset(lobby.my_ready, 0, sizeof(lobby.my_ready));
	memset(lobby.ready_said_time, 0, sizeof(lobby.ready_said_time));
	lobby.host_state_generation = 0;
	memset(lobby.ready, 0, sizeof(lobby.ready));
	memset(lobby.said, 0, sizeof(lobby.said));
	memset(lobby.announced, 0, sizeof(lobby.announced));
	/* (the host's countdown waits for START GAME) */
	if (server && start_by_button())
	{
		network_game_server_pause_countdown(server, TRUE);
		lobby.held = TRUE;
	}
}

/* ---------- ADD PLAYERS: the preview's split screen players

ADD PLAYERS, or another controller's START, opens System Link's SELECT
PROFILE (ui.map's four quarters) in place of the Multiplayer menu the game
list is over. The controller that led the way there (the keyboard and mouse
too) is player 1, whichever was connected first (platform_gamepad_make_first),
and each player already joined is shown in their quarter with their profile
picked (add_players_fill). The others press A or START in their quarter,
pick a profile with left and right and press A; B in a joined quarter
empties it, that player no longer joining, and B in an empty one goes back.
A picked quarter's A, which went on to System Link's game list, comes back
to the preview instead (lobby_screen_add_players_finish), and JOIN THE GAME
joins with them all: the pregame screen asks the host for every joined
player at once. While it is up, its "clear multiplayer player joins" and
the Multiplayer menu's "mp type menu initialize" leave the game being
joined alone (ui_widget_event_handler_functions.c). Going back puts
everyone back as they were; its last A joins only the players in its
quarters */

enum
{
	SOUND_ERROR = 4,
};

static void add_players_fill(void);

boolean lobby_screen_adding_players(void)
{
	return lobby.adding;
}

static void add_players_open(short controller)
{
	short index;

	if (lobby.adding || lobby.debug)
		return;
	if (!lobby.preview_game.open)
	{
		platform_log("split screen preview: ADD PLAYERS (P%d) refused: the game can't be joined now", controller + 1);
		ui_play_audio_feedback_sound(SOUND_ERROR);
		return;
	}
	for (index = 0; index < LOCAL_PLAYERS; index++)
	{
		lobby.added_joined[index] = player_ui_local_player_wants_to_play_multiplayer(index);
		lobby.added_profile_index[index] = player_ui_get_active_player_profile_index(index);
		player_ui_get_active_player_profile(index, &lobby.added_profile[index]);
	}
	lobby.adding = TRUE;
	lobby.adding_done = FALSE;
	if (!ui_widget_port_open_screen(ADD_PLAYERS_SCREEN))
	{
		lobby.adding = FALSE;
		platform_log("split screen preview: ADD PLAYERS (P%d): SELECT PROFILE did not open", controller + 1);
		ui_play_audio_feedback_sound(SOUND_ERROR);
		return;
	}
	add_players_fill();
	/* (one person here: the controller that led the way is player 1's, the
	top-left quarter's, whichever was connected first) */
	if (lobby.added_joined[0] && !lobby.added_joined[1] && !lobby.added_joined[2] && !lobby.added_joined[3])
	{
		platform_gamepad_make_first(lobby.navigator_keyboard ? -1 : lobby.navigator);
		platform_log("split screen preview: ADD PLAYERS: player 1 is %s", lobby.navigator_keyboard ?
			"the keyboard and mouse" : lobby.navigator ? "the gamepad that led the way (it takes port 1)" :
			"port 1's gamepad");
	}
	platform_log("split screen preview: ADD PLAYERS (P%d): SELECT PROFILE opened; gamepads P1 %s, P2 %s, P3 %s, P4 %s",
		controller + 1, input_has_gamepad(0) ? "yes" : "no", input_has_gamepad(1) ? "yes" : "no",
		input_has_gamepad(2) ? "yes" : "no", input_has_gamepad(3) ? "yes" : "no");
}

/* the controller's join as SELECT PROFILE found it */
static void add_players_restore(short controller)
{
	if (lobby.added_joined[controller])
	{
		player_ui_set_active_player_profile(controller, lobby.added_profile_index[controller],
			&lobby.added_profile[controller]);
		player_ui_local_player_joined_multiplayer_game(controller);
	}
	else
		player_ui_local_player_left_multiplayer_game(controller);
}

static boolean widget_tree_has(struct widget_instance const *widget, long definition_index)
{
	for (; widget; widget = widget->next)
	{
		if (widget->definition_tag_index == definition_index || widget_tree_has(widget->child, definition_index))
			return TRUE;
	}
	return FALSE;
}

/* the controller's quarter among the widgets and their descendants, when
it is the definition's (NULL: none) */
static struct widget_instance *add_players_quarter(struct widget_instance *widget, long definition_index,
	short controller)
{
	for (; widget; widget = widget->next)
	{
		struct widget_instance *found;

		if (widget->definition_tag_index == definition_index && widget->local_player_index == controller)
			return widget;
		if ((found = add_players_quarter(widget->child, definition_index, controller)) != NULL)
			return found;
	}
	return NULL;
}

/* SELECT PROFILE as it opens, its joins cleared: each controller joined
before it (player 1, whom Online Games joined, on the controller that led
the way here: add_players_open) shown in its quarter with its profile
picked, as its own A twice would leave it (its list moved to its profile
between them: the list starts on player 1's last) */
static void add_players_fill(void)
{
	struct widget_instance *screen = ui_widget_port_top_screen();
	long empty = tag_loaded(UI_WIDGET_DEFINITION_TAG, ADD_PLAYERS_EMPTY);
	long choosing = tag_loaded(UI_WIDGET_DEFINITION_TAG, ADD_PLAYERS_CHOOSING);
	long picked = tag_loaded(UI_WIDGET_DEFINITION_TAG, ADD_PLAYERS_PICKED);
	short controller;

	if (!screen || empty == NONE || choosing == NONE || picked == NONE)
		return;
	for (controller = 0; controller < LOCAL_PLAYERS; controller++)
	{
		struct widget_instance *quarter;
		struct widget_instance *list;
		boolean deleted = FALSE;

		if (!lobby.added_joined[controller])
			continue;
		quarter = add_players_quarter(screen->child, empty, controller);
		if (!quarter)
		{
			platform_log("split screen preview: SELECT PROFILE: P%d has no empty quarter to fill", controller + 1);
			continue;
		}
		ui_widget_port_dispatch_event(quarter, EVENT_A, controller, &deleted);
		quarter = add_players_quarter(screen->child, choosing, controller);
		if (!quarter)
		{
			platform_log("split screen preview: SELECT PROFILE: P%d's quarter did not join", controller + 1);
			continue;
		}
		/* (the spinner list, as player_profile_set_for_game_1wide finds it) */
		for (list = quarter->child; list && list->type != 2; list = list->next)
			;
		if (list && list->parameters.list.list_items)
		{
			long const *items = (long const *)list->parameters.list.list_items;
			short item;

			for (item = 0; item < (short)list->parameters.list.number_of_items; item++)
			{
				if (items[item] == lobby.added_profile_index[controller])
					list->parameters.list.selected_index = item;
			}
		}
		ui_widget_port_dispatch_event(quarter, EVENT_A, controller, &deleted);
		platform_log("split screen preview: SELECT PROFILE: P%d's quarter filled in, %s", controller + 1,
			add_players_quarter(screen->child, picked, controller) ? "profile picked" : "still choosing a profile");
	}
}

/* (interface/ui_widget.c) B on SELECT PROFILE over the preview, which has
no B of its own: TRUE when the controller's quarter had joined (choosing
or picked), which is then empty again and that player won't join; FALSE
when it was empty, and the screen goes back as the Xbox's did */
boolean lobby_screen_add_players_back(struct widget_instance *screen, short controller)
{
	long empty = tag_loaded(UI_WIDGET_DEFINITION_TAG, ADD_PLAYERS_EMPTY);
	struct widget_instance *quarter;

	if (!lobby.adding || !screen || empty == NONE || controller < 0 || controller >= LOCAL_PLAYERS)
		return FALSE;
	quarter = add_players_quarter(screen->child, tag_loaded(UI_WIDGET_DEFINITION_TAG, ADD_PLAYERS_PICKED), controller);
	if (!quarter)
		quarter = add_players_quarter(screen->child, tag_loaded(UI_WIDGET_DEFINITION_TAG, ADD_PLAYERS_CHOOSING),
			controller);
	if (!quarter)
		return FALSE;
	if (!ui_widget_port_replace(quarter, empty))
	{
		platform_log("split screen preview: SELECT PROFILE: P%d's B: the quarter could not be emptied", controller + 1);
		return FALSE;
	}
	player_ui_local_player_left_multiplayer_game(controller);
	platform_log("split screen preview: SELECT PROFILE: P%d's B: their quarter is empty, they won't join", controller + 1);
	return TRUE;
}

/* (interface/ui_widget.c) a picked quarter's A on the preview's SELECT
PROFILE: TRUE to go back to the preview with the players who picked their
profiles, FALSE while a quarter is still picking */
boolean lobby_screen_add_players_finish(struct widget_instance *screen, short controller)
{
	long choosing = tag_loaded(UI_WIDGET_DEFINITION_TAG, ADD_PLAYERS_CHOOSING);

	if (screen && choosing != NONE && widget_tree_has(screen->child, choosing))
	{
		platform_log("split screen preview: SELECT PROFILE: P%d's A waits, a player is still picking a profile",
			controller + 1);
		display_error_text_deferred(L"Waiting for every player\r\nto select a profile.", controller);
		ui_play_audio_feedback_sound(SOUND_ERROR);
		return FALSE;
	}
	lobby.adding_done = TRUE;
	platform_log("split screen preview: SELECT PROFILE done (P%d's A)", controller + 1);
	return TRUE;
}

/* SELECT PROFILE gone (lobby_screen_update): back to the preview */
static void add_players_closed(void)
{
	char joining[160];
	short controller;

	lobby.adding = FALSE;
	if (!lobby.adding_done)
	{
		for (controller = 0; controller < LOCAL_PLAYERS; controller++)
			add_players_restore(controller);
		platform_log("split screen preview: SELECT PROFILE backed out: the players are as before");
	}
	joining_text(joining, sizeof(joining));
	platform_log("split screen preview: back from SELECT PROFILE; joining from here: %d (%s)", joining_count(),
		joining);
	reset_selection();
	lobby.opened_time = system_milliseconds();
	read_menu(NULL);
	read_status(NULL);
	event_manager_flush();
}

void lobby_screen_joined_from_list(void)
{
	lobby.from_list = lobby_enabled();
}

void lobby_screen_hosted_opened(void)
{
	if (lobby_enabled())
		lobby_opened();
}

boolean lobby_screen_host_returns_to_lobby(void)
{
	return lobby_enabled();
}

void lobby_screen_update(boolean menu_open)
{
	struct network_game_server *server = global_network_game_server_get();
	boolean covering;

	/* the host's lobby for Delta's READY, while this machine hosts with
	these menus (clients that join agree to it in their HELLO) */
	{
		static boolean lobby_host_set;
		boolean set = server != NULL && lobby_enabled();

		if (set != lobby_host_set)
		{
			delta_peer_game_set_lobby_host(set ? &lobby_host : NULL);
			lobby_host_set = set;
		}
	}
	if (!server)
		delta_peer_game_set_lobby_state(NULL);

	/* the preview's SELECT PROFILE closed (its last A or its B) */
	if (lobby.adding)
	{
		struct widget_instance *top = ui_widget_port_top_screen();
		long screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, ADD_PLAYERS_SCREEN);
		static char said[160];
		char joining[160];

		/* (who has joined on it so far, said as it changes) */
		joining_text(joining, sizeof(joining));
		if (strcmp(joining, said))
		{
			platform_log("split screen preview: SELECT PROFILE: joining %d (%s)", joining_count(), joining);
			copy_text(said, sizeof(said), joining);
		}
		if (!top || screen == NONE || top->definition_tag_index != screen)
		{
			said[0] = 0;
			add_players_closed();
		}
	}

	covering = menu_open && lobby_enabled() && !browser_screen_active() && pregame_screen_up() && current_game() != NULL;
	if (covering && !lobby.covering)
	{
		lobby.widget = ui_widget_port_top_screen();
		/* (where its B goes: the game list a join came from, the screen
		a new game was made from) */
		ui_widget_port_lobby_origin_set(lobby.from_list);
		lobby.from_list = FALSE;
		lobby_opened();
		event_manager_flush();
	}
	else if (!covering && lobby.covering)
	{
		lobby.widget = NULL;
		lobby.dialog = _dialog_none;
	}
	lobby.covering = covering;
	if (covering)
	{
		lobby.widget = ui_widget_port_top_screen();
		read_game();
	}
	/* (the server's own lobby, not this one: no state sent) */
	if (!covering && server)
		delta_peer_game_set_lobby_state(NULL);

	/* left for ONLINE GAMES: the game list again, once the Multiplayer
	menu is up (it ended the game left behind) */
	if (lobby.back_to_list && !covering)
	{
		struct widget_instance *top = ui_widget_port_top_screen();

		if (system_milliseconds() - lobby.back_time > 3000)
			lobby.back_to_list = FALSE;
		else if (top && main_menu_is_active() && !browser_screen_active() && !invite_prompt_active() &&
			top->definition_tag_index == tag_loaded(UI_WIDGET_DEFINITION_TAG, MULTIPLAYER_SCREEN))
		{
			lobby.back_to_list = FALSE;
			browser_screen_open();
		}
	}

	/* debug.lobby_preview: the sample lobby over the main menu, once */
	if (!lobby.debug_done && !lobby.covering && !lobby.preview && main_menu_is_active())
	{
		char const *preview = config_string("debug.lobby_preview");

		if (preview && preview[0] && lobby_enabled())
		{
			lobby.debug_done = TRUE;
			lobby.debug = TRUE;
			reset_selection();
			lobby.opened_time = system_milliseconds();
			debug_sample(preview);
			event_manager_flush();
		}
	}
}

boolean lobby_screen_preview_open(struct browser_game const *game, struct network_invite_advertisement const *advertisement)
{
	if (!lobby_enabled() || lobby.covering)
		return FALSE;
	memset(&lobby.preview_game, 0, sizeof(lobby.preview_game));
	lobby.preview_listed = game != NULL;
	if (game)
		lobby.preview_game = *game;
	if (advertisement)
	{
		/* (the host's own word on its game: fresher than the list's) */
		if (!game)
		{
			int index;

			for (index = 0; index < (int)NUMBEROF(advertisement->game_name) && index < BROWSER_NAME_LENGTH; index++)
				lobby.preview_game.name[index] = (unsigned short)advertisement->game_name[index];
			copy_text(lobby.preview_game.map, sizeof(lobby.preview_game.map), advertisement->map_name);
			lobby.preview_game.engine = advertisement->engine_type;
			lobby.preview_game.teams = advertisement->has_teams;
			lobby.preview_game.ping = -1;
		}
		lobby.preview_game.players = advertisement->player_count;
		lobby.preview_game.maximum_players = advertisement->maximum_player_count;
		lobby.preview_game.open = advertisement->open;
		lobby.preview_game.score_limit = advertisement->score_limit;
	}
	lobby.preview = TRUE;
	lobby.preview_answer = 0;
	lobby.navigator = 0;
	lobby.navigator_keyboard = halo_keyboard_prompts() != 0;
	lobby.state = _lobby_preview;
	reset_selection();
	lobby.opened_time = system_milliseconds();
	social_read();
	read_preview();
	platform_log("split screen preview: opened (%d of %d in the game, open %d); gamepads P2 %s, P3 %s, P4 %s; "
		"joining from here: %d", lobby.preview_game.players, lobby.preview_game.maximum_players,
		(int)lobby.preview_game.open, input_has_gamepad(1) ? "yes" : "no", input_has_gamepad(2) ? "yes" : "no",
		input_has_gamepad(3) ? "yes" : "no", joining_count());
	event_manager_flush();
	return TRUE;
}

int lobby_screen_preview_answer(void)
{
	int answer = lobby.preview_answer;

	if (answer)
	{
		lobby.preview = FALSE;
		lobby.preview_answer = 0;
	}
	return answer;
}

/* ---------- the actions */

static void leave(short controller)
{
	boolean deleted = FALSE;

	if (lobby.debug)
	{
		lobby.debug = FALSE;
		return;
	}
	if (lobby.preview)
	{
		short guest;

		/* (backed out: its split screen players don't join the next game) */
		for (guest = 1; guest < LOCAL_PLAYERS; guest++)
		{
			if (player_ui_local_player_wants_to_play_multiplayer(guest))
			{
				player_ui_local_player_left_multiplayer_game(guest);
				platform_log("split screen preview: backed out; P%d no longer joins", guest + 1);
			}
		}
		lobby.preview_answer = -1;
		return;
	}
	if (lobby.widget)
	{
		struct widget_instance *leaf = lobby.widget;

		/* (back goes where the game came from, not through the new
		game's setup: ui_widget.c) */
		ui_widget_port_lobby_leave_history();
		lobby.back_to_list = ui_widget_port_lobby_origin_online_games();
		lobby.back_time = system_milliseconds();
		while (leaf->focused_child)
			leaf = leaf->focused_child;
		ui_widget_port_dispatch_event(leaf, EVENT_B, controller, &deleted);
	}
}

static void swap_team(short controller)
{
	struct network_game *game = network_game_get_game();
	struct network_game_client *client = global_network_game_client_get();
	short local = network_game_client_get_local_machine_index();
	long index;

	if (!game || !client || !lobby.teams || local == NONE || lobby.debug || lobby.preview)
		return;
	for (index = 0; index < (long)NUMBEROF(game->players); index++)
	{
		struct network_player player = game->players[index];

		if (network_player_is_valid(&game->players[index]) && (short)player.machine_index == local &&
			player.controller_index == controller)
		{
			player.team_index = (char)!player.team_index;
			network_game_client_update_local_player_data(client, &player);
			return;
		}
	}
}

static void start_game(boolean start)
{
	struct network_game_server *server = global_network_game_server_get();

	lobby.started = start;
	if (lobby.debug)
		return;
	if (server)
	{
		network_game_server_pause_countdown(server, !start);
		/* (the countdown starts on a player's joining, which came while
		it was held: started now, as a dedicated server's is) */
		if (start)
			network_game_server_dedicated_start_countdown(server);
	}
	/* (cancelled: the host's own count goes at once, so the next frame
	doesn't read the old one back as a start before the server's NONE
	reaches its client, #1414) */
	if (!start)
	{
		struct network_game_client *client = global_network_game_client_get();

		if (client)
			network_game_client_countdown_timer_update(client, NONE);
		lobby.seconds = NONE;
	}
}

/* a controller's READY: its own player's (a split screen's guest too) */
static void toggle_ready(short controller)
{
	struct network_game *game = network_game_get_game();
	short player;

	if (controller < 0 || controller >= LOCAL_PLAYERS)
		controller = 0;
	player = local_player_of(game, controller);
	if (!lobby.debug && player == NONE)
		return;
	lobby.my_ready[controller] = !lobby.my_ready[controller];
	if (lobby.debug)
		return;
	delta_peer_game_lobby_say(player, lobby.my_ready[controller]);
	lobby.ready_said_time[controller] = system_milliseconds();
}

static void start_sooner(void)
{
	struct network_game_client *client = global_network_game_client_get();

	if (client && !lobby.debug)
		network_game_client_request_start_time_change(client, TRUE);
}

static void menu_act(short controller)
{
	struct lobby_menu_row const *row;

	if (lobby.menu_selected < 0 || lobby.menu_selected >= lobby.menu_count)
		return;
	row = &lobby.menu[lobby.menu_selected];
	if (!row->on)
		return;
	switch (row->action)
	{
	case _menu_start: start_game(TRUE); break;
	case _menu_cancel_start:
	{
		struct network_game_server *server = global_network_game_server_get();

		/* (not once the start went out: the server would sit in its lobby
		with the start sent, and refuse every change) */
		if (!server || network_game_server_lobby_is_open(server))
			start_game(FALSE);
		break;
	}
	case _menu_change_map:
		if (!lobby.debug)
		{
			ui_widgets_close_all();
			ui_widget_load_by_name_or_tag(MAP_SELECT_SCREEN, NONE, NULL, NONE, NONE, NONE, NONE);
		}
		break;
	case _menu_game_setup:
		if (!lobby.debug)
			ui_widget_port_open_screen(HOST_SETUP_SCREEN);
		break;
	case _menu_ready: toggle_ready(controller); break;
	case _menu_start_sooner: start_sooner(); break;
	case _menu_change_team: swap_team(controller); break;
	case _menu_details:
		lobby.dialog = _dialog_details;
		lobby.dialog_selected = 0;
		break;
	case _menu_invite:
#ifdef HALO_GUIDE
		if (!lobby.debug)
			guide_open_party();
#endif
		break;
	case _menu_join:
		if (lobby.preview)
		{
			char joining[160];

			joining_text(joining, sizeof(joining));
			platform_log("split screen preview: JOIN THE GAME with %d player(s): %s", joining_count(), joining);
			lobby.preview_answer = 1;
		}
		break;
	case _menu_add_players:
		if (lobby.preview)
			add_players_open(controller);
		break;
	default:
		break;
	}
}

/* the selected player's details: a dialog */
static void open_player(void)
{
	if (lobby.selected < 0 || lobby.selected >= lobby.count)
		return;
	lobby.dialog = _dialog_player;
	lobby.dialog_row = lobby.selected;
	lobby.dialog_selected = 0;
}

static short player_actions(struct lobby_row const *row, short *actions)
{
	short count = 0;

	if (!row->local && lobby.state != _lobby_preview)
		actions[count++] = _player_action_mute;
	if (lobby.state == _lobby_hosted && !row->local)
		actions[count++] = _player_action_kick;
	return count;
}

static void player_act(void)
{
	struct lobby_row const *row;
	short actions[NUMBER_OF_PLAYER_ACTIONS], count;
	struct network_game *game = network_game_get_game();

	if (lobby.dialog_row < 0 || lobby.dialog_row >= lobby.count)
		return;
	row = &lobby.rows[lobby.dialog_row];
	count = player_actions(row, actions);
	if (lobby.dialog_selected < 0 || lobby.dialog_selected >= count)
		return;
	switch (actions[lobby.dialog_selected])
	{
	case _player_action_mute:
		if (game && row->player_index >= 0 && row->player_index < (short)NUMBEROF(game->players) && !lobby.debug)
			chat_local_mute(row->machine_index, game->players[row->player_index].name, !row->muted);
		break;
	case _player_action_kick:
		if (!lobby.debug)
			network_game_server_host_kick_machine(row->machine_index, NULL);
		lobby.dialog = _dialog_none;
		break;
	default:
		break;
	}
}

/* ---------- input */

static void step_list(short move)
{
	short column_first, column_count;

	if (!lobby.count)
		return;
	if (lobby.layout.columns == 2)
	{
		boolean right = lobby.pane == _pane_list_right;

		column_first = right ? lobby.red : 0;
		column_count = right ? lobby.blue : lobby.red;
		if (!column_count)
			return;
		lobby.selected = (short)PIN(lobby.selected + move, column_first, column_first + column_count - 1);
	}
	else
		lobby.selected = (short)PIN(lobby.selected + move, 0, lobby.count - 1);
}

static void step(short move)
{
	if (lobby.dialog == _dialog_player)
	{
		struct lobby_row const *row = lobby.dialog_row >= 0 && lobby.dialog_row < lobby.count ? &lobby.rows[lobby.dialog_row] : NULL;
		short actions[NUMBER_OF_PLAYER_ACTIONS], count = row ? player_actions(row, actions) : 0;

		lobby.dialog_selected = (short)PIN(lobby.dialog_selected + move, 0, MAX(0, count - 1));
		return;
	}
	if (lobby.dialog != _dialog_none)
		return;
	if (lobby.pane == _pane_menu)
		lobby.menu_selected = (short)PIN(lobby.menu_selected + move, 0, MAX(0, lobby.menu_count - 1));
	else
		step_list(move);
}

/* the pane to the left or right: the list (its two columns), the menu */
static void step_pane(short move)
{
	short pane = lobby.pane;

	if (lobby.dialog != _dialog_none)
		return;
	for (;;)
	{
		pane = (short)(pane + move);
		if (pane < 0 || pane >= NUMBER_OF_PANES)
			return;
		if (pane == _pane_list_right && lobby.layout.columns != 2)
			continue;
		if (pane == _pane_list && !lobby.count)
			continue;
		if (pane == _pane_list_right && !lobby.blue)
			continue;
		break;
	}
	lobby.pane = pane;
	if (pane == _pane_list_right && lobby.selected < lobby.red)
		lobby.selected = lobby.red;
	else if (pane == _pane_list && lobby.layout.columns == 2 && lobby.selected >= lobby.red)
		lobby.selected = (short)MAX(0, lobby.red - 1);
}

/* a step held down: the first press moves at once; held, it waits, then
repeats at a steady pace, as the menus do */
static void held_step(short button)
{
	static unsigned long last_event_time, last_step_time;
	static short last_button;
	static boolean repeating;
	unsigned long now = system_milliseconds();
	boolean act = FALSE;

	if (button != last_button || now - last_event_time > STEP_RELEASE_GAP)
	{
		act = TRUE;
		repeating = FALSE;
		last_step_time = now;
	}
	else if (now - last_step_time >= (repeating ? STEP_REPEAT_INTERVAL : STEP_FIRST_REPEAT_DELAY))
	{
		act = TRUE;
		repeating = TRUE;
		last_step_time = now;
	}
	last_button = button;
	last_event_time = now;
	if (!act)
		return;
	switch (button)
	{
	case _gamepad_binary_button_dpad_up: step(-1); break;
	case _gamepad_binary_button_dpad_down: step(1); break;
	case _gamepad_binary_button_dpad_left: step_pane(-1); break;
	case _gamepad_binary_button_dpad_right: step_pane(1); break;
	default: break;
	}
}

/* a second controller's START (it joins split screen) and B (its player
leaves, the machine's others staying): the Xbox pregame screen's own */
static void guest_press(short button, short controller)
{
	boolean deleted = FALSE;
	struct widget_instance *leaf = lobby.widget;

	if (!leaf)
		return;
	while (leaf->focused_child)
		leaf = leaf->focused_child;
	ui_widget_port_dispatch_event(leaf, button, controller, &deleted);
}

static void press(short button, short controller)
{
	if (system_milliseconds() - lobby.opened_time < SETTLE)
		return;
	if (lobby.preview && controller >= 0 && controller < LOCAL_PLAYERS &&
		!(button == _gamepad_binary_button_start && controller > 0))
	{
		lobby.navigator = controller;
		lobby.navigator_keyboard = controller == 0 && halo_keyboard_prompts();
	}
	/* (the preview: another controller's START is the Xbox's to join,
	SELECT PROFILE's) */
	if (lobby.preview && controller > 0 && button == _gamepad_binary_button_start && lobby.dialog == _dialog_none)
	{
		platform_log("split screen preview: P%d pressed START", controller + 1);
		add_players_open(controller);
		return;
	}
	if (controller > 0 && !lobby.debug && !lobby.preview)
	{
		struct network_game const *game = network_game_get_game();
		boolean own_player = local_player_of(game, controller) != NONE;

		/* (START joins; B takes out a guest, a player of its own with the
		machine's others staying) */
		if (button == _gamepad_binary_button_start ||
			(button == _gamepad_analog_button_b && own_player && machine_player_count(game) > 1))
		{
			guest_press(button, controller);
			return;
		}
		/* (a controller with no player here, a second gamepad: its B is the
		lobby's LEAVE, as the first controller's, and it leaves as the
		machine's first player. It went to the guest's B, which took nobody
		out, so a pad that wasn't the first could not leave. With split
		screen's players here it is someone else's pad: its B leaves nothing,
		as on the Xbox, so it can't take the others out of the game) */
		if (!own_player)
		{
			if (machine_player_count(game) > 1)
			{
				if (button == _gamepad_analog_button_b && lobby.dialog == _dialog_none)
					return;
			}
			else
				controller = first_local_controller(game);
		}
	}
	if (lobby.dialog != _dialog_none)
	{
		switch (button)
		{
		case _gamepad_analog_button_a:
		case _gamepad_binary_button_start:
			if (lobby.dialog == _dialog_player)
				player_act();
			else if (lobby.dialog == _dialog_leave)
			{
				lobby.dialog = _dialog_none;
				leave(controller);
			}
			else
				lobby.dialog = _dialog_none;
			break;
		case _gamepad_analog_button_b:
			lobby.dialog = _dialog_none;
			break;
		default:
			break;
		}
		return;
	}
	switch (button)
	{
	case _gamepad_binary_button_dpad_up:
	case _gamepad_binary_button_dpad_down:
	case _gamepad_binary_button_dpad_left:
	case _gamepad_binary_button_dpad_right:
		held_step(button);
		break;
	case _gamepad_analog_button_white:
		step_pane(-1);
		break;
	case _gamepad_analog_button_black:
		step_pane(1);
		break;
	case _gamepad_analog_button_a:
		if (lobby.pane == _pane_menu)
			menu_act(controller);
		else
			open_player();
		break;
	case _gamepad_analog_button_b:
		/* (the host with others in: asked first) */
		if (lobby.state == _lobby_hosted && lobby.count > 1 && !lobby.debug)
			lobby.dialog = _dialog_leave;
		else
			leave(controller);
		break;
	case _gamepad_analog_button_x:
		if (lobby.teams && lobby.state != _lobby_preview)
			swap_team(controller);
		break;
	case _gamepad_analog_button_y:
		open_player();
		break;
	default:
		break;
	}
}

void lobby_screen_process(void)
{
	struct event_record event;

	if (!lobby_screen_active())
		return;
	if (lobby.debug && (!main_menu_is_active() || browser_screen_active()))
		lobby.debug = FALSE;
	if (lobby.preview)
		read_preview();
	while (lobby_screen_active() && get_next_event(&event, NONE))
	{
		if (lobby.preview && event.controller_index >= 0 && event.controller_index < LOCAL_PLAYERS &&
			!(event.type == LOBBY_EVENT_BUTTON && event.data.button.index == _gamepad_binary_button_start &&
				event.controller_index > 0))
		{
			lobby.navigator = event.controller_index;
			lobby.navigator_keyboard = event.controller_index == 0 && halo_keyboard_prompts();
		}
		if (event.type == LOBBY_EVENT_BUTTON)
		{
			short button = event.data.button.index;

			if (button >= _gamepad_binary_button_dpad_up && button <= _gamepad_binary_button_dpad_right)
				held_step(button);
			else if (event.data.button.value == 1)
				press(button, event.controller_index);
		}
		else if (event.type == LOBBY_EVENT_LEFT_STICK)
		{
			if (event.data.stick.y == SHORT_MAX)
				held_step(_gamepad_binary_button_dpad_up);
			else if (event.data.stick.y == SHORT_MIN)
				held_step(_gamepad_binary_button_dpad_down);
			else if (event.data.stick.x == SHORT_MIN)
				held_step(_gamepad_binary_button_dpad_left);
			else if (event.data.stick.x == SHORT_MAX)
				held_step(_gamepad_binary_button_dpad_right);
		}
	}
	/* (the widgets behind take nothing while the lobby is up) */
	event_manager_flush();
}

void lobby_screen_pointer(struct halo_ui_pointer const *pointer)
{
	short index;

	if (!lobby_screen_active())
		return;
	if (pointer->right_clicks)
		press(_gamepad_analog_button_b, 0);
	if (pointer->wheel_steps)
		step((short)(pointer->wheel_steps > 0 ? -1 : 1));
	if (!pointer->left_clicks)
		return;
	for (index = 0; index < lobby.target_count; index++)
	{
		struct lobby_target const *target = &lobby.targets[index];

		if (pointer->click_x < target->x0 || pointer->click_x >= target->x1 || pointer->click_y < target->y0 ||
			pointer->click_y >= target->y1)
			continue;
		/* (under a dialog, its prompts alone) */
		if (lobby.dialog != _dialog_none && target->button == NONE)
			continue;
		if (target->button != NONE)
			press(target->button, 0);
		else if (target->menu != NONE)
		{
			lobby.pane = _pane_menu;
			if (lobby.menu_selected == target->menu)
				press(_gamepad_analog_button_a, 0);
			else
				lobby.menu_selected = target->menu;
		}
		else if (target->row != NONE)
		{
			lobby.pane = (short)(lobby.layout.columns == 2 && target->row >= lobby.red ? _pane_list_right : _pane_list);
			if (lobby.selected == target->row)
				press(_gamepad_analog_button_a, 0);
			else
				lobby.selected = target->row;
		}
		return;
	}
}

/* ---------- drawing */

static float prompt_width(int button, char const *words, float button_size, float text_size)
{
	return ui_overlay_button_width(button, button_size) + 3.0f + ui_overlay_text_width(UI_FONT_MENU, text_size, words);
}

static float prompt(int button, char const *words, float x, float y, float button_size, float text_size, boolean on,
	float top, float bottom)
{
	float left = x;

	x += ui_overlay_button(button, button_size, x, y, on ? 0xFFFFFFFF : COLOR_BUTTON_OFF) + 3.0f;
	x += ui_overlay_text_glow(UI_FONT_MENU, text_size, x, y + (button_size - text_size) * 0.5f + 0.5f, UI_ALIGN_LEFT,
		on ? COLOR_PROMPT : COLOR_PROMPT_OFF, on ? 0x3D8BFF80 : 0x00000000, words);
	add_target(left - 6.0f, top, x + 6.0f, bottom, glyph_buttons[button], NONE, NONE);
	return x;
}

/* the crown: a base and three points, in gold */
static void draw_crown(float x, float y, float size, unsigned int color)
{
	float unit = size / 8.0f;

	ui_overlay_rect(x, y + size - unit * 2.5f, size, unit * 2.5f, 0.5f, color);
	ui_overlay_rect(x, y + unit * 2, unit * 2, size - unit * 2, 0, color);
	ui_overlay_rect(x + size * 0.5f - unit, y, unit * 2, size, 0, color);
	ui_overlay_rect(x + size - unit * 2, y + unit * 2, unit * 2, size - unit * 2, 0, color);
}

/* the Delta mark: a hollow triangle, drawn as its outline's steps */
static void draw_delta_mark(float x, float y, float size, unsigned int color)
{
	float thickness = size * 0.16f, step, half = size * 0.5f;
	int steps = 6, index;

	for (index = 0; index < steps; index++)
	{
		float share = (float)index / (float)steps, next = (float)(index + 1) / (float)steps;
		float top = y + size * share, bottom = y + size * next;
		float left = x + half - half * next, right = x + half + half * next - thickness;

		ui_overlay_rect(left, top, thickness, bottom - top + 0.3f, 0, color);
		ui_overlay_rect(right, top, thickness, bottom - top + 0.3f, 0, color);
		step = bottom;
	}
	ui_overlay_rect(x, step - thickness, size, thickness, 0, color);
}

/* the voice icon: a speaker and two arcs' bars */
static void draw_voice(float x, float y, float size, unsigned int color)
{
	float unit = size / 10.0f;

	ui_overlay_rect(x, y + unit * 3, unit * 2.5f, unit * 4, 0.5f, color);
	ui_overlay_rect(x + unit * 2.5f, y + unit * 1.5f, unit * 2.5f, unit * 7, 1.0f, color);
	ui_overlay_rect(x + unit * 6.5f, y + unit * 3.5f, unit, unit * 3, 0.5f, color);
	ui_overlay_rect(x + unit * 8.5f, y + unit * 2, unit, unit * 6, 0.5f, color);
}

static void draw_band(void)
{
	struct lobby_layout const *layout = &lobby.layout;
	float right = 640 + layout->margin - PROMPTS_RIGHT_EDGE, x, y, left;
	char text[80];

	ui_overlay_band(layout->band.x, layout->band.x + layout->band.width, LOBBY_LAYOUT_BAND_BOTTOM, LOBBY_LAYOUT_BAR_TOP);
	/* (the kicker and the title lined up with the players' column, the
	title in the Xbox screens' size, cut to its room) */
	left = layout->list_head.x + 2;
	ui_overlay_text_glow(UI_FONT_MENU, 8.0f, left, KICKER_Y, UI_ALIGN_LEFT, COLOR_KICKER, COLOR_HALO, lobby.kicker);
	fitted(lobby.title, right - left - 220, 13.5f, text, sizeof(text));
	ui_overlay_text_glow(UI_FONT_TITLE, 13.5f, left, TITLE_Y, UI_ALIGN_LEFT, COLOR_TITLE, COLOR_TITLE, text);
	/* the host or the server's badge at the right (the crown is the host's
	row's alone), and the chip (a preview's ping) */
	x = right;
	y = 14;
	if (lobby.chip[0])
	{
		ui_overlay_outline(x - ui_overlay_text_width(UI_FONT_MENU, 8.0f, lobby.chip) - 12, y - 4,
			ui_overlay_text_width(UI_FONT_MENU, 8.0f, lobby.chip) + 12, 16, 2, 1.0f, 0x7CC0FF80);
		ui_overlay_text_glow(UI_FONT_MENU, 8.0f, x - 6, y, UI_ALIGN_RIGHT, COLOR_KICKER, COLOR_HALO, lobby.chip);
	}
	y = 36;
	if (lobby.badge[0])
		ui_overlay_text_glow(UI_FONT_MENU, 8.5f, x, y, UI_ALIGN_RIGHT, COLOR_BADGE, COLOR_HALO, lobby.badge);
	else if (lobby.host_name[0])
	{
		char name[48];

		snprintf(text, sizeof(text), "HOST: %s", fitted(lobby.host_name, 120, 8.5f, name, sizeof(name)));
		upper_text(text);
		ui_overlay_text_glow(UI_FONT_MENU, 8.5f, x, y, UI_ALIGN_RIGHT, COLOR_TEXT, COLOR_HALO, text);
	}
}

static void draw_emblem(struct lobby_row const *row, float x, float y, float size)
{
	unsigned int color = armor_colors[(row->color_index >= 0 ? row->color_index : 0) % (short)NUMBEROF(armor_colors)];

	ui_overlay_rect(x, y, size, size, 2, color);
	ui_overlay_outline(x, y, size, size, 2, 0.75f, 0xFFFFFF40);
}

static void draw_row(struct lobby_row const *row, short order, int column, int visible_row, boolean picked)
{
	struct lobby_layout const *layout = &lobby.layout;
	struct lobby_rect r = lobby_layout_row(layout, column, visible_row);
	float y = r.y + 1, height = r.height - 2, x = r.x;
	float name_y = y + 3, sub_y = y + 3 + NAME_SIZE + 2, text_x, width;
	char text[80];
	unsigned int right_color;
	char const *right_text = NULL;

	ui_overlay_rect(x, y, r.width, height, 3, COLOR_ROW_FILL);
	if (picked)
	{
		ui_overlay_rect(x, y, r.width, height, 3, COLOR_SELECTION_FILL);
		ui_overlay_outline(x, y, r.width, height, 3, 1.25f, COLOR_SELECTION_EDGE);
	}
	add_target(x, y, x + r.width, y + height, NONE, order, NONE);
	/* the team's stripe */
	if (lobby.teams)
		ui_overlay_rect(x, y, layout->stripe_width, height, 1, row->team ? COLOR_BLUE_TEAM : COLOR_RED_TEAM);
	/* the name, with the Delta mark and the crown after it while they fit */
	text_x = x + layout->name_x;
	width = layout->name_width - (row->verified ? 12 : 0) - (row->host ? 13 : 0);
	fitted(row->name, width, NAME_SIZE, text, sizeof(text));
	text_x += ui_overlay_text_glow(UI_FONT_MENU, NAME_SIZE, text_x, name_y, UI_ALIGN_LEFT, COLOR_TEXT,
		picked ? 0x6FB2FF80 : 0, text) + 4;
	if (row->verified)
	{
		draw_delta_mark(text_x, name_y + 1.5f, 7.5f, COLOR_DELTA);
		text_x += 11;
	}
	if (row->host)
		draw_crown(text_x, name_y + 1.5f, 8, COLOR_CROWN);
	if (row->sub[0])
	{
		fitted(row->sub, layout->name_width, SUB_SIZE, text, sizeof(text));
		ui_overlay_text(UI_FONT_MENU, SUB_SIZE, x + layout->name_x, sub_y, UI_ALIGN_LEFT, COLOR_DIM, text);
	}
	/* the voice icon while they talk, a dim one while muted */
	if (row->talking || row->muted)
		draw_voice(x + layout->voice_x, y + (height - 10.0f) * 0.5f, 10.0f, row->muted ? COLOR_MUTED : COLOR_VOICE);
	/* the right column: READY, a score, or a mark in two columns */
	if (row->has_score)
	{
		snprintf(text, sizeof(text), "%d", row->score);
		right_text = text;
		right_color = COLOR_TEXT;
	}
	else if (lobby.state == _lobby_hosted || lobby.state == _lobby_joined)
	{
		right_color = row->ready ? COLOR_READY : COLOR_NOT_READY;
		if (layout->columns == 2)
		{
			if (row->ready)
				ui_overlay_rect(x + layout->right_x - 8, y + (height - 6) * 0.5f, 6, 6, 3, right_color);
			else
				ui_overlay_outline(x + layout->right_x - 8, y + (height - 6) * 0.5f, 6, 6, 3, 1.5f, COLOR_TEXT);
		}
		else
			right_text = row->ready ? "READY" : "NOT READY";
	}
	if (right_text)
	{
		ui_overlay_text_glow(UI_FONT_MENU, RIGHT_SIZE, x + layout->right_x, y + (height - RIGHT_SIZE) * 0.5f + 0.5f,
			UI_ALIGN_RIGHT, right_color, picked ? 0x6FB2FF60 : 0, right_text);
	}
}

/* an open slot; the first, while a gamepad here may join, says how */
static void draw_open_slot(int column, int visible_row, boolean join)
{
	struct lobby_rect r = lobby_layout_row(&lobby.layout, column, visible_row);
	float text_y = r.y + (r.height - 7.0f) * 0.5f + 0.5f;

	ui_overlay_outline(r.x, r.y + 1, r.width, r.height - 2, 3, 0.75f, COLOR_OPEN);
	/* (in words: the key the keyboard last used would name the wrong
	button, and it is a gamepad's START) */
	if (join)
	{
		ui_overlay_text(UI_FONT_MENU, 7.0f, r.x + 10, text_y, UI_ALIGN_LEFT, COLOR_PROMPT, "PRESS START TO JOIN");
		return;
	}
	ui_overlay_text(UI_FONT_MENU, 7.0f, r.x + 10, text_y, UI_ALIGN_LEFT, COLOR_OPEN, "OPEN SLOT");
}

/* a column's open slots past the ones it shows */
static int open_past(int column)
{
	struct lobby_layout const *layout = &lobby.layout;
	int open = layout->columns == 2 ? lobby.maximum_players / 2 - layout->count[column] :
		lobby.maximum_players - lobby.count;

	return lobby.state == _lobby_preview ? 0 : open - layout->open[column];
}

static void draw_list(void)
{
	struct lobby_layout const *layout = &lobby.layout;
	char text[64];
	int column, index;
	boolean join = lobby.join_hint;

	/* the head: PLAYERS and the count; the teams' counts in two columns */
	ui_overlay_text_glow(UI_FONT_MENU, HEAD_SIZE, layout->list_head.x + 2, layout->list_head.y + 2, UI_ALIGN_LEFT,
		COLOR_TITLE, COLOR_HALO, lobby.state == _lobby_preview ? "IN THE GAME" : "PLAYERS");
	if (layout->columns == 2)
	{
		snprintf(text, sizeof(text), "RED %d", lobby.red);
		ui_overlay_text_glow(UI_FONT_MENU, HEAD_SIZE, layout->column[0].x + layout->column[0].width - 2,
			layout->list_head.y + 2, UI_ALIGN_RIGHT, COLOR_RED_TEAM, COLOR_HALO, text);
		snprintf(text, sizeof(text), "BLUE %d", lobby.blue);
		ui_overlay_text_glow(UI_FONT_MENU, HEAD_SIZE, layout->column[1].x + layout->column[1].width - 2,
			layout->list_head.y + 2, UI_ALIGN_RIGHT, COLOR_BLUE_TEAM, COLOR_HALO, text);
	}
	else
	{
		snprintf(text, sizeof(text), lobby.state == _lobby_preview && lobby.count && lobby.rows[0].has_score ? "SCORE" : "%d OF %d",
			lobby.count, lobby.maximum_players);
		ui_overlay_text_glow(UI_FONT_MENU, HEAD_SIZE, layout->list_head.x + layout->list_head.width - 2,
			layout->list_head.y + 2, UI_ALIGN_RIGHT, COLOR_KICKER, COLOR_HALO, text);
	}
	for (index = 0; index < lobby.count; index++)
	{
		int row;

		lobby_layout_place(layout, &lobby.inputs, index, &column, &row);
		if (row >= 0)
			draw_row(&lobby.rows[index], (short)index, column, row, lobby.pane != _pane_menu && index == lobby.selected &&
				lobby.dialog == _dialog_none);
	}
	for (column = 0; column < layout->columns; column++)
	{
		int shown = layout->count[column] - layout->first[column];

		for (index = 0; index < layout->open[column]; index++)
		{
			draw_open_slot(column, shown + index, join);
			join = FALSE;
		}
		/* (more rows than fit: how many are past the end) */
		if (layout->count[column] - layout->first[column] > layout->rows)
		{
			struct lobby_rect c = layout->column[column];

			snprintf(text, sizeof(text), "+ %d MORE", layout->count[column] - layout->first[column] - layout->rows);
			ui_overlay_text_glow(UI_FONT_MENU, 7.0f, c.x + c.width - 6, c.y + c.height + 1, UI_ALIGN_RIGHT, COLOR_KICKER,
				COLOR_HALO, text);
		}
		else if (layout->first[column] > 0)
		{
			struct lobby_rect c = layout->column[column];

			snprintf(text, sizeof(text), "%d ABOVE", layout->first[column]);
			ui_overlay_text_glow(UI_FONT_MENU, 7.0f, c.x + c.width - 6, c.y + c.height + 1, UI_ALIGN_RIGHT, COLOR_KICKER,
				COLOR_HALO, text);
		}
		/* (open slots past the ones shown) */
		else if (open_past(column) > 0)
		{
			struct lobby_rect c = layout->column[column];

			snprintf(text, sizeof(text), "+%d OPEN", open_past(column));
			ui_overlay_text_glow(UI_FONT_MENU, 7.0f, c.x + c.width - 6, c.y + c.height + 1, UI_ALIGN_RIGHT, COLOR_KICKER,
				COLOR_HALO, text);
		}
	}
}

static void draw_card(void)
{
	struct lobby_layout const *layout = &lobby.layout;
	struct lobby_rect c = layout->card;
	/* (the picture in its own shape, the menus' 140 by 116, at the left;
	the map's name, the game type and the countdown beside it) */
	float inset = 4.0f, picture_height = c.height - inset * 2, picture_width = picture_height * 140.0f / 116.0f;
	float text_x, text_width, y;
	char text[80];

	ui_overlay_rect(c.x, c.y, c.width, c.height, 0, COLOR_CARD_SHADE);
	ui_overlay_outline(c.x - 1, c.y - 1, c.width + 2, c.height + 2, 2, 1.0f, 0x4F9DFFC0);
	if (picture_width > c.width * 0.6f)
	{
		picture_width = c.width * 0.6f;
		picture_height = picture_width * 116.0f / 140.0f;
	}
	if (!covered())
		browser_screen_map_picture(lobby.map, c.x + inset, c.y + inset, picture_width, picture_height);
	else
		ui_overlay_rect(c.x + inset, c.y + inset, picture_width, picture_height, 0, 0x0A1A33FF);
	text_x = c.x + inset + picture_width + 10;
	text_width = c.x + c.width - 8 - text_x;
	y = c.y + 8;
	ui_overlay_text(UI_FONT_MENU, 6.5f, text_x, y, UI_ALIGN_LEFT, COLOR_DIM,
		lobby.state == _lobby_preview ? "NOW PLAYING" : lobby.state == _lobby_server ? "NEXT GAME" : "MAP");
	y += 10;
	fitted(lobby.map_name, text_width, 12.0f, text, sizeof(text));
	ui_overlay_text_glow(UI_FONT_MENU, 12.0f, text_x, y, UI_ALIGN_LEFT, COLOR_TEXT, COLOR_HALO, text);
	y += 16;
	fitted(lobby.type_name, text_width, 9.0f, text, sizeof(text));
	ui_overlay_text_glow(UI_FONT_MENU, 9.0f, text_x, y, UI_ALIGN_LEFT, COLOR_TITLE, COLOR_HALO, text);
	/* the countdown, under them */
	if (lobby.seconds > 0)
	{
		y = c.y + c.height - 48;
		ui_overlay_text_glow(UI_FONT_MENU, 7.5f, text_x, y, UI_ALIGN_LEFT, COLOR_KICKER, COLOR_HALO, "GAME STARTS IN");
		snprintf(text, sizeof(text), "%d", lobby.seconds);
		ui_overlay_text_glow(UI_FONT_MENU, 28.0f, text_x, y + 10, UI_ALIGN_LEFT, COLOR_TEXT, COLOR_TITLE, text);
	}
}

static void draw_facts(void)
{
	struct lobby_layout const *layout = &lobby.layout;
	float half = layout->facts.width * 0.5f;
	short index;
	char text[96], value[64];

	for (index = 0; index < lobby.fact_count; index++)
	{
		float x = layout->facts.x + (index % 2) * half;
		float y = layout->facts.y + (index / 2) * layout->fact_line;
		float width;

		snprintf(text, sizeof(text), "%s:", lobby.facts[index].key);
		width = ui_overlay_text(UI_FONT_MENU, FACT_SIZE, x, y, UI_ALIGN_LEFT, COLOR_DIM, text) + FACT_SIZE * 0.4f;
		fitted(lobby.facts[index].value, half - width - 6, FACT_SIZE, value, sizeof(value));
		ui_overlay_text(UI_FONT_MENU, FACT_SIZE, x + width, y, UI_ALIGN_LEFT, COLOR_TEXT, value);
	}
}

static void draw_menu(void)
{
	struct lobby_layout const *layout = &lobby.layout;
	short index;
	char text[64];

	for (index = 0; index < lobby.menu_count; index++)
	{
		struct lobby_menu_row const *row = &lobby.menu[index];
		struct lobby_rect r = lobby_layout_menu_row(layout, index);
		boolean picked = lobby.pane == _pane_menu && index == lobby.menu_selected && lobby.dialog == _dialog_none;
		float y = r.y + 1, height = r.height - 2, note_width = 0;
		unsigned int color = !row->on ? COLOR_MENU_OFF : picked ? COLOR_TEXT : COLOR_MENU;

		ui_overlay_rect(r.x, y, r.width, height, 3, COLOR_ROW_FILL);
		if (picked)
		{
			ui_overlay_rect(r.x, y, r.width, height, 3, COLOR_SELECTION_FILL);
			ui_overlay_outline(r.x, y, r.width, height, 3, 1.25f, COLOR_SELECTION_EDGE);
			ui_overlay_rect(r.x, y, 3, height, 1, COLOR_TITLE);
		}
		add_target(r.x, y, r.x + r.width, y + height, NONE, NONE, index);
		if (row->note[0])
		{
			fitted(row->note, r.width * 0.45f, NOTE_SIZE, text, sizeof(text));
			note_width = ui_overlay_text(UI_FONT_MENU, NOTE_SIZE, r.x + r.width - 8, y + (height - NOTE_SIZE) * 0.5f + 0.5f,
				UI_ALIGN_RIGHT, COLOR_DIM, text) + 8;
		}
		fitted(row->label, r.width - 20 - note_width, MENU_SIZE, text, sizeof(text));
		ui_overlay_text_glow(UI_FONT_MENU, MENU_SIZE, r.x + 10, y + (height - MENU_SIZE) * 0.5f + 0.5f, UI_ALIGN_LEFT,
			color, picked ? 0x6FB2FF80 : 0, text);
	}
}

static void draw_bar(void)
{
	struct lobby_layout const *layout = &lobby.layout;
	float right = 640 + layout->margin - PROMPTS_RIGHT_EDGE;
	float y = LOBBY_LAYOUT_BAR_TOP + (LOBBY_LAYOUT_HEIGHT - LOBBY_LAYOUT_BAR_TOP - PROMPT_BUTTON) / 2;
	int buttons[4];
	char const *words[4];
	boolean on[4];
	short count = 0, index;
	float width = 0, x;
	char text[160];

	/* (the bar to the screen's bottom, its prompts in its middle) */
	ui_overlay_prompt_bar(layout->bar.x, layout->bar.x + layout->bar.width, LOBBY_LAYOUT_BAR_TOP, LOBBY_LAYOUT_HEIGHT);
	buttons[count] = UI_BUTTON_Y;
	words[count] = lobby.state == _lobby_preview ? "=PLAYER" : "=PLAYER DETAILS";
	on[count++] = lobby.count > 0;
	if (lobby.teams && lobby.state != _lobby_preview)
	{
		buttons[count] = UI_BUTTON_X;
		words[count] = "=CHANGE TEAM";
		on[count++] = TRUE;
	}
	buttons[count] = UI_BUTTON_B;
	words[count] = lobby.state == _lobby_preview ? "=BACK" : "=LEAVE";
	on[count++] = TRUE;
	buttons[count] = UI_BUTTON_A;
	words[count] = "=SELECT";
	on[count++] = TRUE;
	for (index = 0; index < count; index++)
		width += prompt_width(buttons[index], words[index], PROMPT_BUTTON, PROMPT_TEXT) + (index ? PROMPT_GAP : 0);
	x = right - width;
	/* the status line, in the room left of the prompts */
	fitted(lobby.status, x - (layout->bar.x + PROMPTS_RIGHT_EDGE) - 16, STATUS_SIZE, text, sizeof(text));
	ui_overlay_text_glow(UI_FONT_MENU, STATUS_SIZE, layout->bar.x + PROMPTS_RIGHT_EDGE,
		LOBBY_LAYOUT_BAR_TOP + (LOBBY_LAYOUT_HEIGHT - LOBBY_LAYOUT_BAR_TOP - STATUS_SIZE) / 2 + 0.5f, UI_ALIGN_LEFT,
		COLOR_DIM, COLOR_HALO, text);
	for (index = 0; index < count; index++)
	{
		x = prompt(buttons[index], words[index], x, y, PROMPT_BUTTON, PROMPT_TEXT, on[index], LOBBY_LAYOUT_BAR_TOP,
			LOBBY_LAYOUT_HEIGHT) + PROMPT_GAP;
	}
}

/* a dialog over the screen: a panel with its lines, A and B at the bottom */
static void draw_dialog(void)
{
	struct lobby_layout const *layout = &lobby.layout;
	float width = 300, height = 150, x = 320 - width * 0.5f, y = 240 - height * 0.5f, line_y;
	char text[160], name[64];
	int buttons[2] = { UI_BUTTON_B, UI_BUTTON_A };
	char const *words[2] = { "=BACK", "=SELECT" };
	float prompts_width = 0, px;
	short index;

	ui_overlay_layer(1);
	ui_overlay_rect(layout->band.x, LOBBY_LAYOUT_BAND_BOTTOM + 1, layout->band.width, LOBBY_LAYOUT_BAR_TOP - LOBBY_LAYOUT_BAND_BOTTOM - 2, 0,
		COLOR_SHADE);
	ui_overlay_rect(x, y, width, height, 6, COLOR_PANEL);
	ui_overlay_outline(x, y, width, height, 6, 1.0f, COLOR_PANEL_EDGE);
	line_y = y + 12;
	switch (lobby.dialog)
	{
	case _dialog_player:
	{
		struct lobby_row const *row = lobby.dialog_row >= 0 && lobby.dialog_row < lobby.count ? &lobby.rows[lobby.dialog_row] : NULL;
		short actions[NUMBER_OF_PLAYER_ACTIONS], count;

		if (!row)
			break;
		fitted(row->name, width - 60, 12.0f, name, sizeof(name));
		ui_overlay_text_glow(UI_FONT_MENU, 12.0f, x + 16, line_y, UI_ALIGN_LEFT, COLOR_TEXT, COLOR_HALO, name);
		draw_emblem(row, x + width - 16 - 16, line_y - 2, 16);
		line_y += 18;
		if (row->verified)
		{
			ui_overlay_text(UI_FONT_MENU, DIALOG_SIZE, x + 16, line_y, UI_ALIGN_LEFT, COLOR_DELTA, "Verified Delta profile");
			line_y += 13;
		}
		if (row->platform)
		{
			ui_overlay_text(UI_FONT_MENU, DIALOG_SIZE, x + 16, line_y, UI_ALIGN_LEFT, COLOR_DIM,
				delta_platform_label(row->platform));
			line_y += 13;
		}
		if (row->sub[0])
		{
			ui_overlay_text(UI_FONT_MENU, DIALOG_SIZE, x + 16, line_y, UI_ALIGN_LEFT, COLOR_DIM, row->sub);
			line_y += 13;
		}
		count = player_actions(row, actions);
		line_y += 4;
		for (index = 0; index < count; index++)
		{
			boolean picked = index == lobby.dialog_selected;
			char const *label = actions[index] == _player_action_mute ? (row->muted ? "UNMUTE" : "MUTE") : "KICK";

			if (picked)
			{
				ui_overlay_rect(x + 12, line_y - 3, width - 24, 16, 3, COLOR_SELECTION_FILL);
				ui_overlay_outline(x + 12, line_y - 3, width - 24, 16, 3, 1.0f, COLOR_SELECTION_EDGE);
			}
			ui_overlay_text_glow(UI_FONT_MENU, 10.0f, x + 20, line_y, UI_ALIGN_LEFT, picked ? COLOR_TEXT : COLOR_MENU,
				picked ? 0x6FB2FF80 : 0, label);
			line_y += 17;
		}
		if (!count)
			ui_overlay_text(UI_FONT_MENU, DIALOG_SIZE, x + 16, line_y, UI_ALIGN_LEFT, COLOR_DIM, "This is you");
		break;
	}
	case _dialog_details:
		snprintf(text, sizeof(text), "%s", lobby.map_name);
		ui_overlay_text_glow(UI_FONT_MENU, 12.0f, x + 16, line_y, UI_ALIGN_LEFT, COLOR_TEXT, COLOR_HALO,
			fitted(text, width - 32, 12.0f, name, sizeof(name)));
		line_y += 16;
		ui_overlay_text(UI_FONT_MENU, DIALOG_SIZE, x + 16, line_y, UI_ALIGN_LEFT, COLOR_TITLE, lobby.type_name);
		line_y += 16;
		for (index = 0; index < lobby.fact_count; index++)
		{
			snprintf(text, sizeof(text), "%s: %s", lobby.facts[index].key, lobby.facts[index].value);
			ui_overlay_text(UI_FONT_MENU, DIALOG_SIZE, x + 16, line_y, UI_ALIGN_LEFT, COLOR_DIM, text);
			line_y += 13;
		}
		if (lobby.host_name[0])
		{
			snprintf(text, sizeof(text), "Host: %s", lobby.host_name);
			ui_overlay_text(UI_FONT_MENU, DIALOG_SIZE, x + 16, line_y, UI_ALIGN_LEFT, COLOR_DIM,
				fitted(text, width - 32, DIALOG_SIZE, name, sizeof(name)));
		}
		words[1] = "=OK";
		break;
	case _dialog_leave:
		ui_overlay_text_glow(UI_FONT_MENU, 12.0f, x + 16, line_y, UI_ALIGN_LEFT, COLOR_TEXT, COLOR_HALO, "END THE GAME?");
		line_y += 18;
		snprintf(text, sizeof(text), "%d players are in your lobby.", lobby.count);
		ui_overlay_text(UI_FONT_MENU, DIALOG_SIZE, x + 16, line_y, UI_ALIGN_LEFT, COLOR_DIM, text);
		words[0] = "=STAY";
		words[1] = "=LEAVE";
		break;
	default:
		break;
	}
	for (index = 0; index < 2; index++)
		prompts_width += prompt_width(buttons[index], words[index], 14.0f, 10.0f) + (index ? PROMPT_GAP : 0);
	px = x + width - 14 - prompts_width;
	for (index = 0; index < 2; index++)
		px = prompt(buttons[index], words[index], px, y + height - 24, 14.0f, 10.0f, TRUE, y + height - 30, y + height) + PROMPT_GAP;
	ui_overlay_layer(0);
}

void lobby_screen_render(void)
{
	struct lobby_layout_inputs *in = &lobby.inputs;

	lobby.target_count = 0;
	if (!lobby_screen_active() || !ui_overlay_available())
		return;
	memset(in, 0, sizeof(*in));
	in->screen_width = (float)halo_screen_width();
	in->players = lobby.count;
	in->maximum_players = lobby.maximum_players;
	in->teams = lobby.teams;
	in->red = lobby.red;
	in->blue = lobby.blue;
	in->selected = lobby.pane == _pane_menu ? -1 : lobby.selected;
	in->first[0] = lobby.first[0];
	in->first[1] = lobby.first[1];
	in->menu_rows = lobby.menu_count;
	in->facts = lobby.fact_count;
	lobby_layout_compute(in, &lobby.layout);
	lobby.first[0] = lobby.layout.first[0];
	lobby.first[1] = lobby.layout.first[1];
	if (lobby.pane == _pane_list_right && lobby.layout.columns != 2)
		lobby.pane = _pane_list;

	draw_band();
	draw_list();
	draw_card();
	draw_facts();
	draw_menu();
	draw_bar();
#ifdef HALO_PORT_CHAT
	/* the chat's lines under the menu, over the lobby, as they show over a
	game's HUD (the game's own are under the overlay: chat.c) */
	{
		struct lobby_layout const *layout = &lobby.layout;
		float top = layout->menu.y;

		if (lobby.menu_count > 0)
		{
			struct lobby_rect last = lobby_layout_menu_row(layout, lobby.menu_count - 1);

			top = last.y + last.height;
		}
		chat_draw_overlay(layout->menu.x, top + 8, layout->menu.x + layout->menu.width, LOBBY_LAYOUT_BAR_TOP - 6);
	}
#endif
	if (lobby.dialog != _dialog_none)
		draw_dialog();
}

#endif
