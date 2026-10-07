/*
MODERATION_SCREEN.C

Moderation in the game (configure.py --game-browser; docs/delta.md,
Moderation): on a dedicated server that speaks Delta Peer and offers
moderation (delta_moderation.h), a moderator signs in with the moderator
key made from their player key, and warns, kicks or bans players and ends
the game or skips the map from a screen of its own over the pause menu,
drawn and driven by code as Online Games is (browser_screen.c).

Y (or M on the keyboard) opens it while a menu (the pause menu) is up in
such a server's game. A signs in; then up and down pick a player, left and
right an action (those the server's role for the key allows), A asks to
confirm, A again does it, and B goes back. The server's answer shows below.

Over the game, at the top of the screen: a warning the server sends this
player (a few seconds), and, while the server's panel asks to link an
account to this game, how to answer it (the same screen: A links, B
declines). The player key and the moderator key never leave this machine;
only signatures of the server's challenge and of each action do, and only
when the player acts.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "game/players.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "../src/browser.h"
#include "../src/delta_moderation.h"
#include "../src/ui_overlay.h"

/* ---------- constants */

enum
{
	/* (event_manager.c's event types, which it keeps to itself) */
	MODERATION_EVENT_LEFT_STICK = 1,
	MODERATION_EVENT_BUTTON = 3,

	ROWS = 10,
	/* a notice shows this long, a result this long */
	NOTICE_DURATION = 6000,
	RESULT_DURATION = 8000,
	/* the screen takes no A this soon after it opens */
	OPEN_SETTLE = 400,
	/* an analog button's press, out of 255 */
	PRESS_THRESHOLD = 128,
};

enum
{
	COLOR_SHADE = 0x000000A0,
	COLOR_PANEL = 0x081530F0,
	COLOR_PANEL_EDGE = 0x2F6DD0FF,
	COLOR_TITLE = 0x3D8BFFFF,
	COLOR_TEXT = 0xE6EEFCFF,
	COLOR_DIM = 0x8FA6C8FF,
	COLOR_ROW_SELECTED = 0x2052B0FF,
	COLOR_PROMPT = 0x4AA3FFFF,
	COLOR_OK = 0x7FD67FFF,
	COLOR_BAD = 0xFF6B6BFF,
	COLOR_WARNING = 0xF2C14EFF,
};

/* the actions, as the screen offers them */
struct moderation_choice
{
	char const *name;
	int action;
	int minutes;
	/* the permission it needs (with BAN_TIMED, minutes within the role's) */
	unsigned int permission;
	boolean needs_target;
};

static struct moderation_choice const moderation_choices[] =
{
	{ "Warn", _delta_moderation_action_warn, 0, DELTA_MODERATION_WARN, TRUE },
	{ "Kick", _delta_moderation_action_kick, 0, DELTA_MODERATION_KICK, TRUE },
	{ "Ban 1 hour", _delta_moderation_action_ban, 60, DELTA_MODERATION_BAN_TIMED, TRUE },
	{ "Ban 1 day", _delta_moderation_action_ban, 24 * 60, DELTA_MODERATION_BAN_TIMED, TRUE },
	{ "Ban 7 days", _delta_moderation_action_ban, 7 * 24 * 60, DELTA_MODERATION_BAN_TIMED, TRUE },
	{ "Ban for ever", _delta_moderation_action_ban, 0, DELTA_MODERATION_BAN, TRUE },
	{ "End the game", _delta_moderation_action_end_game, 0, DELTA_MODERATION_MAP, FALSE },
	{ "Next map", _delta_moderation_action_next_map, 0, DELTA_MODERATION_MAP, FALSE },
};

/* a player of the game, as the screen lists them */
struct moderation_player
{
	char name[16];
	short machine_index;
};

/* ---------- globals */

static struct
{
	boolean active;
	unsigned long opened_time;
	/* the open key's and button's state last frame (a press opens it) */
	boolean open_held;
	short selected;
	short choice;
	boolean confirming;
	/* the players and the choices allowed, this frame */
	short player_count;
	struct moderation_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short allowed_count;
	short allowed[NUMBEROF(moderation_choices)];
	struct delta_peer_game_moderation moderation;
	/* the last notice and result seen, and when they came */
	unsigned long notice_seen;
	unsigned long notice_time;
	unsigned long result_seen;
	unsigned long result_time;
	char status[128];
} moderation_screen;

/* ---------- private code */

/* the game's players but this machine's (the client's view) */
static void read_players(
	void)
{
	struct network_game_client *client = global_network_game_client_get();
	struct network_game *game = client ? network_game_client_get_game(client) : NULL;
	short local_machine = network_game_client_get_local_machine_index();
	long index;

	moderation_screen.player_count = 0;
	if (!game)
		return;
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		struct network_player *player = &game->players[index];
		struct moderation_player *out;
		long character;

		if (!network_player_is_valid(player) || player->machine_index == local_machine)
			continue;
		out = &moderation_screen.players[moderation_screen.player_count++];
		for (character = 0; character < (long)NUMBEROF(player->name) && player->name[character] &&
			character < (long)sizeof(out->name) - 1; character++)
		{
			out->name[character] = player_name_character_ascii(player->name[character]);
		}
		out->name[character] = 0;
		out->machine_index = player->machine_index;
	}
}

/* the choices the role allows */
static void read_choices(
	void)
{
	struct delta_peer_game_moderation const *moderation = &moderation_screen.moderation;
	short index;

	moderation_screen.allowed_count = 0;
	for (index = 0; index < (short)NUMBEROF(moderation_choices); index++)
	{
		struct moderation_choice const *choice = &moderation_choices[index];
		boolean allowed = (moderation->permissions & choice->permission) != 0;

		/* (a timed ban the role's own limit allows, or any ban) */
		if (choice->permission == DELTA_MODERATION_BAN_TIMED)
		{
			allowed = (moderation->permissions & DELTA_MODERATION_BAN) ||
				(allowed && (unsigned int)choice->minutes <= moderation->ban_minutes);
		}
		if (allowed)
			moderation_screen.allowed[moderation_screen.allowed_count++] = index;
	}
	if (moderation_screen.choice >= moderation_screen.allowed_count)
		moderation_screen.choice = 0;
}

static void set_status(
	char const *text)
{
	csstrncpy(moderation_screen.status, text, sizeof(moderation_screen.status) - 1);
	moderation_screen.status[sizeof(moderation_screen.status) - 1] = 0;
}

/* the confirmed choice, sent */
static void act(
	void)
{
	struct moderation_choice const *choice;
	short target = DELTA_WIRE_NO_MACHINE;
	char text[128];

	if (!moderation_screen.allowed_count)
		return;
	choice = &moderation_choices[moderation_screen.allowed[moderation_screen.choice]];
	if (choice->needs_target)
	{
		if (!moderation_screen.player_count)
			return;
		target = moderation_screen.players[moderation_screen.selected].machine_index;
	}
	if (!delta_peer_game_moderation_action(choice->action, target, choice->minutes, ""))
	{
		set_status("Could not send that to the server.");
		return;
	}
	if (choice->needs_target)
		snprintf(text, sizeof(text), "%s: %s, sent", choice->name, moderation_screen.players[moderation_screen.selected].name);
	else
		snprintf(text, sizeof(text), "%s, sent", choice->name);
	set_status(text);
}

static void press(
	short button)
{
	struct delta_peer_game_moderation const *moderation = &moderation_screen.moderation;
	boolean settled = system_milliseconds() - moderation_screen.opened_time > OPEN_SETTLE;

	if (button == _gamepad_analog_button_b)
	{
		if (moderation->bind_waiting)
			delta_peer_game_moderation_bind_answer(FALSE);
		else if (moderation_screen.confirming)
			moderation_screen.confirming = FALSE;
		else
			moderation_screen.active = FALSE;
		return;
	}
	/* a link the server's panel asks for comes first */
	if (moderation->bind_waiting)
	{
		if (button == _gamepad_analog_button_a && settled)
		{
			set_status(delta_peer_game_moderation_bind_answer(TRUE) ? "Linked." : "Could not answer the server.");
		}
		return;
	}
	if (!moderation->signed_in)
	{
		if (button == _gamepad_analog_button_a && settled)
		{
			set_status(delta_peer_game_moderation_sign_in() ? "Signing in..." :
				"Could not sign in (no player key, or this is not the server you joined).");
		}
		return;
	}
	switch (button)
	{
	case _gamepad_binary_button_dpad_up:
		if (!moderation_screen.confirming && moderation_screen.selected > 0)
			moderation_screen.selected--;
		break;
	case _gamepad_binary_button_dpad_down:
		if (!moderation_screen.confirming && moderation_screen.selected + 1 < moderation_screen.player_count)
			moderation_screen.selected++;
		break;
	case _gamepad_binary_button_dpad_left:
		if (!moderation_screen.confirming && moderation_screen.allowed_count)
		{
			moderation_screen.choice = (short)((moderation_screen.choice + moderation_screen.allowed_count - 1) %
				moderation_screen.allowed_count);
		}
		break;
	case _gamepad_binary_button_dpad_right:
		if (!moderation_screen.confirming && moderation_screen.allowed_count)
			moderation_screen.choice = (short)((moderation_screen.choice + 1) % moderation_screen.allowed_count);
		break;
	case _gamepad_analog_button_a:
		if (!settled || !moderation_screen.allowed_count)
			break;
		if (moderation_screen.confirming)
		{
			moderation_screen.confirming = FALSE;
			act();
		}
		else if (!moderation_choices[moderation_screen.allowed[moderation_screen.choice]].needs_target ||
			moderation_screen.player_count)
		{
			moderation_screen.confirming = TRUE;
		}
		break;
	default:
		break;
	}
}

/* ---------- public code */

boolean moderation_screen_active(
	void)
{
	return moderation_screen.active;
}

/* each frame (ui_widget.c's process_ui_widgets), before the menus: what the
host says; the screen opened by Y (or M) while a menu is up and the host
offers moderation, closed once it does not */
void moderation_screen_update(
	boolean menu_open)
{
	struct gamepad_state const *gamepad;
	boolean held = input_key_is_down(_key_m);
	short index;

	delta_peer_game_client_moderation(&moderation_screen.moderation);
	if (moderation_screen.moderation.notice_count != moderation_screen.notice_seen)
	{
		moderation_screen.notice_seen = moderation_screen.moderation.notice_count;
		moderation_screen.notice_time = system_milliseconds();
	}
	if (moderation_screen.moderation.result_count != moderation_screen.result_seen)
	{
		moderation_screen.result_seen = moderation_screen.moderation.result_count;
		moderation_screen.result_time = system_milliseconds();
		moderation_screen.status[0] = 0;
	}
	for (index = 0; index < MAXIMUM_GAMEPADS; index++)
	{
		gamepad = input_has_gamepad(index) ? input_get_gamepad_state(index) : NULL;
		if (gamepad && gamepad->analog_buttons[_gamepad_analog_button_y] >= PRESS_THRESHOLD)
			held = TRUE;
	}
	if (!moderation_screen.moderation.available)
		moderation_screen.active = FALSE;
	else if (!moderation_screen.active && menu_open && held && !moderation_screen.open_held)
	{
		moderation_screen.active = TRUE;
		moderation_screen.opened_time = system_milliseconds();
		moderation_screen.confirming = FALSE;
		moderation_screen.status[0] = 0;
		/* (the Y that opened it is not the menu's) */
		event_manager_flush();
	}
	moderation_screen.open_held = held;
}

void moderation_screen_process(
	void)
{
	struct event_record event;

	read_players();
	read_choices();
	if (moderation_screen.selected >= moderation_screen.player_count)
		moderation_screen.selected = (short)MAX(0, moderation_screen.player_count - 1);
	while (moderation_screen.active && get_next_event(&event, NONE))
	{
		if (event.type == MODERATION_EVENT_BUTTON)
			press(event.data.button.index);
		else if (event.type == MODERATION_EVENT_LEFT_STICK)
		{
			if (event.data.stick.y == SHORT_MAX)
				press(_gamepad_binary_button_dpad_up);
			else if (event.data.stick.y == SHORT_MIN)
				press(_gamepad_binary_button_dpad_down);
			else if (event.data.stick.x == SHORT_MIN)
				press(_gamepad_binary_button_dpad_left);
			else if (event.data.stick.x == SHORT_MAX)
				press(_gamepad_binary_button_dpad_right);
		}
	}
	/* (the menu behind takes nothing while the screen is up) */
	event_manager_flush();
}

static float prompt(
	int button,
	char const *words,
	float x,
	float y)
{
	x += ui_overlay_button(button, 15.0f, x, y, 0xFFFFFFFF) + 3.0f;
	x += ui_overlay_text(UI_FONT_BOLD, 12.0f, x, y + 1.5f, UI_ALIGN_LEFT, COLOR_PROMPT, words);
	return x + 18.0f;
}

static char const *role_name(
	int role)
{
	switch (role)
	{
	case _delta_moderation_role_moderator: return "moderator";
	case _delta_moderation_role_admin: return "admin";
	case _delta_moderation_role_owner: return "owner";
	default: return "no role";
	}
}

/* over the game: a warning the server sent, and a link waiting */
static void render_banner(
	void)
{
	struct delta_peer_game_moderation const *moderation = &moderation_screen.moderation;
	unsigned long now = system_milliseconds();
	char text[192];

	if (moderation->notice_count && now - moderation_screen.notice_time < NOTICE_DURATION && moderation->notice[0])
	{
		boolean warning = moderation->notice_kind == _delta_moderation_notice_warning;

		ui_overlay_rect(110, 24, 420, 30, 6, COLOR_PANEL);
		ui_overlay_outline(110, 24, 420, 30, 6, 1.0f, warning ? COLOR_WARNING : COLOR_PANEL_EDGE);
		snprintf(text, sizeof(text), "%s%s", warning ? "Server warning: " : "", moderation->notice);
		ui_overlay_text(UI_FONT_BOLD, 13.0f, 320, 32, UI_ALIGN_CENTER, warning ? COLOR_WARNING : COLOR_TEXT, text);
	}
	else if (moderation->bind_waiting && !moderation_screen.active)
	{
		ui_overlay_rect(110, 24, 420, 30, 6, COLOR_PANEL);
		ui_overlay_outline(110, 24, 420, 30, 6, 1.0f, COLOR_PANEL_EDGE);
		ui_overlay_text(UI_FONT_REGULAR, 12.0f, 320, 33, UI_ALIGN_CENTER, COLOR_TEXT,
			"The server asks to link this game to an account: pause, then press Y");
	}
}

/* each frame (ui_widget.c's render_ui_widgets, the first player's) */
void moderation_screen_render(
	void)
{
	struct delta_peer_game_moderation const *moderation = &moderation_screen.moderation;
	char text[192];
	float x, y;
	short row, first;

	if (!ui_overlay_available() || !moderation->available)
		return;
	render_banner();
	if (!moderation_screen.active)
		return;

	ui_overlay_rect(-200, 0, 1040, 480, 0, COLOR_SHADE);
	ui_overlay_rect(100, 60, 440, 360, 8, COLOR_PANEL);
	ui_overlay_outline(100, 60, 440, 360, 8, 1.0f, COLOR_PANEL_EDGE);
	ui_overlay_text(UI_FONT_BOLD, 22.0f, 120, 72, UI_ALIGN_LEFT, COLOR_TITLE, "MODERATION");
	if (moderation->has_state)
	{
		ui_overlay_text(UI_FONT_REGULAR, 12.0f, 520, 80, UI_ALIGN_RIGHT, COLOR_DIM,
			role_name(moderation->role));
	}
	y = 108;
	if (moderation->bind_waiting)
	{
		ui_overlay_text(UI_FONT_BOLD, 14.0f, 120, y, UI_ALIGN_LEFT, COLOR_TEXT, "Link this game to an account?");
		snprintf(text, sizeof(text), "Account: %s", moderation->bind_account);
		ui_overlay_text(UI_FONT_REGULAR, 13.0f, 120, y + 28, UI_ALIGN_LEFT, COLOR_TEXT, text);
		snprintf(text, sizeof(text), "Server: %s", moderation->bind_server);
		ui_overlay_text(UI_FONT_REGULAR, 13.0f, 120, y + 46, UI_ALIGN_LEFT, COLOR_TEXT, text);
		ui_overlay_text(UI_FONT_REGULAR, 11.0f, 120, y + 76, UI_ALIGN_LEFT, COLOR_DIM,
			"Only if you asked for it in the server's control panel. Linking");
		ui_overlay_text(UI_FONT_REGULAR, 11.0f, 120, y + 90, UI_ALIGN_LEFT, COLOR_DIM,
			"signs with your moderator key; your player key stays here.");
		x = prompt(UI_BUTTON_A, "Link", 120, 392);
		prompt(UI_BUTTON_B, "Decline", x, 392);
		return;
	}
	if (!moderation->signed_in)
	{
		char key[80];

		ui_overlay_text(UI_FONT_REGULAR, 13.0f, 120, y, UI_ALIGN_LEFT, COLOR_TEXT,
			"This server has moderators. Sign in if you are one.");
		ui_overlay_text(UI_FONT_REGULAR, 11.0f, 120, y + 24, UI_ALIGN_LEFT, COLOR_DIM,
			"Signing in shows this server your moderator key:");
		if (browser_moderator_key(key, sizeof(key)))
		{
			snprintf(text, sizeof(text), "%.32s", key);
			ui_overlay_text(UI_FONT_REGULAR, 11.0f, 120, y + 40, UI_ALIGN_LEFT, COLOR_TEXT, text);
			snprintf(text, sizeof(text), "%s", key + 32);
			ui_overlay_text(UI_FONT_REGULAR, 11.0f, 120, y + 54, UI_ALIGN_LEFT, COLOR_TEXT, text);
		}
		if (moderation_screen.status[0])
			ui_overlay_text(UI_FONT_REGULAR, 12.0f, 120, 360, UI_ALIGN_LEFT, COLOR_DIM, moderation_screen.status);
		x = prompt(UI_BUTTON_A, "Sign in", 120, 392);
		prompt(UI_BUTTON_B, "Back", x, 392);
		return;
	}
	if (!moderation->has_state)
	{
		ui_overlay_text(UI_FONT_REGULAR, 13.0f, 120, y, UI_ALIGN_LEFT, COLOR_TEXT, "Signing in...");
		prompt(UI_BUTTON_B, "Back", 120, 392);
		return;
	}
	if (!moderation->role || !moderation_screen.allowed_count)
	{
		ui_overlay_text(UI_FONT_REGULAR, 13.0f, 120, y, UI_ALIGN_LEFT, COLOR_TEXT,
			"Your moderator key has no role on this server.");
		ui_overlay_text(UI_FONT_REGULAR, 11.0f, 120, y + 22, UI_ALIGN_LEFT, COLOR_DIM,
			"Its owner can add it: sv_players shows it while you are signed in.");
		prompt(UI_BUTTON_B, "Back", 120, 392);
		return;
	}

	/* the action, then the players */
	snprintf(text, sizeof(text), "< %s >", moderation_choices[moderation_screen.allowed[moderation_screen.choice]].name);
	ui_overlay_text(UI_FONT_BOLD, 14.0f, 320, y, UI_ALIGN_CENTER, COLOR_TEXT, text);
	y += 26;
	if (moderation_choices[moderation_screen.allowed[moderation_screen.choice]].needs_target)
	{
		first = (short)MAX(0, moderation_screen.selected - ROWS + 1);
		for (row = 0; row < ROWS && first + row < moderation_screen.player_count; row++)
		{
			short index = (short)(first + row);

			if (index == moderation_screen.selected)
				ui_overlay_rect(116, y + row * 20 - 2, 408, 19, 3, COLOR_ROW_SELECTED);
			ui_overlay_text(UI_FONT_REGULAR, 13.0f, 124, y + row * 20, UI_ALIGN_LEFT, COLOR_TEXT,
				moderation_screen.players[index].name);
		}
		if (!moderation_screen.player_count)
			ui_overlay_text(UI_FONT_REGULAR, 13.0f, 124, y, UI_ALIGN_LEFT, COLOR_DIM, "No other players.");
	}
	if (moderation_screen.confirming)
	{
		struct moderation_choice const *choice = &moderation_choices[moderation_screen.allowed[moderation_screen.choice]];

		if (choice->needs_target)
		{
			snprintf(text, sizeof(text), "%s %s? Press A again.", choice->name,
				moderation_screen.players[moderation_screen.selected].name);
		}
		else
			snprintf(text, sizeof(text), "%s? Press A again.", choice->name);
		ui_overlay_text(UI_FONT_BOLD, 13.0f, 120, 340, UI_ALIGN_LEFT, COLOR_WARNING, text);
	}
	if (moderation_screen.status[0])
		ui_overlay_text(UI_FONT_REGULAR, 12.0f, 120, 360, UI_ALIGN_LEFT, COLOR_DIM, moderation_screen.status);
	else if (moderation->result_count && system_milliseconds() - moderation_screen.result_time < RESULT_DURATION)
	{
		ui_overlay_text(UI_FONT_REGULAR, 12.0f, 120, 360, UI_ALIGN_LEFT, moderation->result_ok ? COLOR_OK : COLOR_BAD,
			moderation->result);
	}
	x = prompt(UI_BUTTON_A, moderation_screen.confirming ? "Confirm" : "Choose", 120, 392);
	x = prompt(UI_BUTTON_DPAD_LEFT, "Action", x, 392);
	prompt(UI_BUTTON_B, moderation_screen.confirming ? "Cancel" : "Back", x, 392);
}

#endif
