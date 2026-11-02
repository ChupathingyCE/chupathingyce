/*
BOTS_SCREEN.C

The host's BOTS for the Xbox's menus (display.menus = "xbox") and Gearbox's,
which have no GAME SETUP screen of the Chupathingy menus (chupathingy/
host_setup.xml): a screen of its own over the lobby or the pause menu, drawn
and driven by code as Moderation's is (moderation_screen.c).

Y opens it in a lobby of a game this machine hosts (split screen's and
System Link's, and Online Games'); a hint "Y BOTS" under the lobby says so.
In a game this machine hosts, the pause menu's BOTS (menu_tags.c) opens it.
Up and down pick a row, left and right change it, X sets them all as
config.toml has them by default, and A or B is done: the values are kept in
config.toml for the games to come. They are the bots' settings
(port/linux/game/bots.c): the lobby has the new bots at once, a game in
progress gets them one a second (network_server_manager.c), and a new skill
is every bot's at once.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "../src/ui_overlay.h"
#include "bots.h"

int config_text(char const *name, char *text, size_t size);
int config_write(char const *name, char const *value);
int config_default(char const *name, char *text, size_t size);
void platform_log(char const *format, ...);
short pc_menus_set(void);
struct network_game *network_game_server_get_game(void *server);

/* ---------- constants */

enum
{
	/* (event_manager.c's event types, which it keeps to itself) */
	BOTS_EVENT_LEFT_STICK = 1,
	BOTS_EVENT_BUTTON = 3,

	/* the screen takes no A this soon after it opens */
	OPEN_SETTLE = 400,
	/* an analog button's press, out of 255 */
	PRESS_THRESHOLD = 128,
	/* a held direction: the first repeat waits this long, then it repeats
	this often; a gap longer than the last is a new press */
	STEP_FIRST_REPEAT_DELAY = 350,
	STEP_REPEAT_INTERVAL = 110,
	STEP_RELEASE_GAP = 150,
};

enum
{
	COLOR_SHADE = 0x000000A0,
	COLOR_SCREEN_PANEL = 0x081530FF,
	COLOR_PANEL_EDGE = 0x2F6DD0FF,
	COLOR_TITLE = 0x3D8BFFFF,
	COLOR_TEXT = 0xE6EEFCFF,
	COLOR_DIM = 0x8FA6C8FF,
	COLOR_ROW_SELECTED = 0x2052B0FF,
	COLOR_PROMPT = 0x4AA3FFFF,
	COLOR_HINT_BACK = 0x00000090,
};

/* the rows */
enum
{
	_bots_row_count,
	_bots_row_skill,
	_bots_row_fill,
	_bots_row_spread,
	_bots_row_balance,
	NUMBER_OF_BOTS_ROWS
};

static char const *const bots_row_labels[NUMBER_OF_BOTS_ROWS] =
{
	"BOTS", "BOT SKILL", "FILL GAME TO", "SKILL SPREAD", "KEEP GAMES CLOSE",
};

/* ---------- globals */

static struct
{
	boolean active;
	unsigned long opened_time;
	/* the open button's state last frame (a press opens it) */
	boolean open_held;
	/* a lobby of a game this machine hosts is up (its hint is drawn) */
	boolean lobby;
	short selected;
	/* the rows shown, in order */
	short rows[NUMBER_OF_BOTS_ROWS];
	short row_count;
} bots_screen;

/* ---------- private code */

/* whether this machine hosts a game (its lobby, or the game itself) */
static boolean bots_screen_host(
	void)
{
	return global_network_game_server_get() != NULL;
}

/* the most bots: the game's players but the host's own (15 at most) */
static long bots_screen_most(
	void)
{
	void *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	long most = game && game->maximum_players > 0 ? game->maximum_players : 16;

	return PIN(most - 1, 0, 15);
}

static void bots_screen_rows(
	void)
{
	short row;

	bots_screen.row_count = 0;
	for (row = 0; row < NUMBER_OF_BOTS_ROWS; row++)
	{
		/* (keeping the games close: with a spread to keep them close with) */
		if (row == _bots_row_balance && bots_get_skill_spread() <= 0)
			continue;
		bots_screen.rows[bots_screen.row_count++] = row;
	}
	if (bots_screen.selected >= bots_screen.row_count)
		bots_screen.selected = (short)(bots_screen.row_count - 1);
}

static char const *bots_screen_skill_name(
	long skill)
{
	switch (skill)
	{
	case 1: return " EASY";
	case 3: return " NORMAL";
	case 5: return " HARD";
	case 8: return " LEGENDARY";
	case 10: return " NEAR PERFECT";
	}
	return "";
}

/* a row's value, and whether left and right change it */
static boolean bots_screen_value(
	short row,
	char *text,
	size_t size)
{
	switch (row)
	{
	case _bots_row_count:
		if (bots_fill() > 0)
		{
			snprintf(text, size, "AUTO");
			return FALSE;
		}
		if (bots_configured() > 0)
			snprintf(text, size, "%ld", bots_configured());
		else
			snprintf(text, size, "NONE");
		return TRUE;
	case _bots_row_skill:
		snprintf(text, size, "%ld%s", bots_get_skill(), bots_screen_skill_name(bots_get_skill()));
		return TRUE;
	case _bots_row_fill:
		if (bots_fill() > 0)
			snprintf(text, size, "%ld PLAYERS", bots_fill());
		else
			snprintf(text, size, "OFF");
		return TRUE;
	case _bots_row_spread:
		if (bots_get_skill_spread() > 0)
			snprintf(text, size, "%ld TO %ld", MAX(1, bots_get_skill() - bots_get_skill_spread()), bots_get_skill());
		else
			snprintf(text, size, "NONE");
		return TRUE;
	case _bots_row_balance:
		snprintf(text, size, "%s", bots_get_balance() ? "ON" : "OFF");
		return TRUE;
	}
	text[0] = 0;
	return FALSE;
}

static char const *bots_screen_help(
	short row)
{
	switch (row)
	{
	case _bots_row_count:
		if (bots_fill() > 0)
			return "FILL GAME TO decides the number of bots. Set it OFF to choose a number.";
		return game_engine_running() ? "Bots in the game. They join or leave one a second." :
			"Bots in the game. They make way for people who join a full game.";
	case _bots_row_skill:
		return "How good the bots are, from 1 (easy) to 10 (near perfect).";
	case _bots_row_fill:
		return "Bots fill the game to this many players, making way for people who join.";
	case _bots_row_spread:
		return "Bots of mixed skill: each gets a skill from this range.";
	case _bots_row_balance:
		return "Bots on a winning team play worse, and on a losing team better.";
	}
	return "";
}

/* left and right on a row */
static void bots_screen_change(
	short row,
	short direction)
{
	switch (row)
	{
	case _bots_row_count:
		if (bots_fill() <= 0)
			bots_set_count(PIN(bots_configured() + direction, 0, bots_screen_most()));
		break;
	case _bots_row_skill:
		bots_set_skill(PIN(bots_get_skill() + direction, 1, 10));
		break;
	case _bots_row_fill:
	{
		/* (off, then 2 players and on: one player and bots is the bots alone) */
		long fill = bots_fill() + direction;

		fill = fill == 1 ? (direction > 0 ? 2 : 0) : fill;
		bots_set_fill(PIN(fill, 0, bots_screen_most() + 1));
		break;
	}
	case _bots_row_spread:
		bots_set_skill_spread(PIN(bots_get_skill_spread() + direction, 0, 9));
		break;
	case _bots_row_balance:
		bots_set_balance(!bots_get_balance());
		break;
	}
}

static void bots_screen_write_one(
	char const *name,
	char const *value)
{
	char text[32];

	if (config_text(name, text, sizeof(text)) && !strcmp(text, value))
		return;
	if (!config_write(name, value))
		platform_log("bots: could not write %s to config.toml", name);
}

/* the values kept for the games to come */
static void bots_screen_write(
	void)
{
	char text[16];

	snprintf(text, sizeof(text), "%ld", bots_configured());
	bots_screen_write_one("bots.count", text);
	snprintf(text, sizeof(text), "%ld", bots_fill());
	bots_screen_write_one("bots.fill", text);
	snprintf(text, sizeof(text), "%ld", bots_get_skill());
	bots_screen_write_one("bots.skill", text);
	snprintf(text, sizeof(text), "%ld", bots_get_skill_spread());
	bots_screen_write_one("bots.skill_spread", text);
	bots_screen_write_one("bots.balance", bots_get_balance() ? "true" : "false");
}

static void bots_screen_defaults(
	void)
{
	char text[32];

	bots_set_count(config_default("bots.count", text, sizeof(text)) ? atol(text) : 0);
	bots_set_fill(config_default("bots.fill", text, sizeof(text)) ? atol(text) : 0);
	bots_set_skill(config_default("bots.skill", text, sizeof(text)) ? atol(text) : 5);
	bots_set_skill_spread(config_default("bots.skill_spread", text, sizeof(text)) ? atol(text) : 0);
	bots_set_balance(config_default("bots.balance", text, sizeof(text)) && !strcmp(text, "true"));
}

static void bots_screen_close(
	void)
{
	bots_screen_write();
	bots_screen.active = FALSE;
	/* (the button that closed it is not the menu's) */
	event_manager_flush();
}

static void press(
	short button)
{
	boolean settled = system_milliseconds() - bots_screen.opened_time > OPEN_SETTLE;

	bots_screen_rows();
	switch (button)
	{
	case _gamepad_analog_button_b:
	case _gamepad_binary_button_back:
		bots_screen_close();
		break;
	case _gamepad_analog_button_a:
	case _gamepad_binary_button_start:
		if (settled)
			bots_screen_close();
		break;
	case _gamepad_analog_button_x:
		if (settled)
			bots_screen_defaults();
		break;
	case _gamepad_binary_button_dpad_up:
		if (bots_screen.selected > 0)
			bots_screen.selected--;
		break;
	case _gamepad_binary_button_dpad_down:
		if (bots_screen.selected + 1 < bots_screen.row_count)
			bots_screen.selected++;
		break;
	case _gamepad_binary_button_dpad_left:
	case _gamepad_binary_button_dpad_right:
		if (bots_screen.row_count)
			bots_screen_change(bots_screen.rows[bots_screen.selected], button == _gamepad_binary_button_dpad_left ? -1 : 1);
		break;
	default:
		break;
	}
}

/* a direction held down: the first press moves at once; held, it waits,
then repeats at a steady pace */
static void held_press(
	short button)
{
	static unsigned long last_event_time, last_press_time;
	static short last_button;
	static boolean repeating;
	unsigned long now = system_milliseconds();

	if (button != last_button || now - last_event_time > STEP_RELEASE_GAP)
	{
		press(button);
		last_press_time = now;
		repeating = FALSE;
	}
	else if (now - last_press_time >= (repeating ? STEP_REPEAT_INTERVAL : STEP_FIRST_REPEAT_DELAY))
	{
		press(button);
		last_press_time = now;
		repeating = TRUE;
	}
	last_button = button;
	last_event_time = now;
}

/* whether a screen is a lobby (the Xbox's split screen and System Link
pregame screens, Gearbox's lobby) */
static boolean bots_screen_is_lobby(
	char const *name)
{
	return name && (strstr(name, "pregame") || !strcmp(name, "lobby_screen"));
}

/* ---------- public code */

boolean bots_screen_active(
	void)
{
	return bots_screen.active;
}

/* the screen opened (the pause menu's BOTS, the lobby's Y): FALSE if this
machine hosts no game */
boolean bots_screen_open(
	void)
{
	if (!bots_screen_host())
		return FALSE;
	if (!bots_screen.active)
	{
		bots_screen.active = TRUE;
		bots_screen.opened_time = system_milliseconds();
		bots_screen.selected = 0;
		bots_screen_rows();
		/* (the button that opened it is not the menu's) */
		event_manager_flush();
	}
	return TRUE;
}

/* each frame (ui_widget.c's process_ui_widgets), before the menus: Y opens
the screen in a lobby of a game this machine hosts, with the Xbox's or
Gearbox's menus (the Chupathingy menus' lobby has its own: menu_tags.c);
closed if the game is no longer this machine's */
void bots_screen_update(
	char const *screen_name)
{
	boolean held = FALSE;
	short index;

	if (!bots_screen_host())
	{
		bots_screen.active = FALSE;
		bots_screen.lobby = FALSE;
		bots_screen.open_held = FALSE;
		return;
	}
	bots_screen.lobby = pc_menus_set() != 2 && !game_engine_running() && bots_screen_is_lobby(screen_name);
	for (index = 0; index < MAXIMUM_GAMEPADS; index++)
	{
		struct gamepad_state const *gamepad = input_has_gamepad(index) ? input_get_gamepad_state(index) : NULL;

		if (gamepad && gamepad->analog_buttons[_gamepad_analog_button_y] >= PRESS_THRESHOLD)
			held = TRUE;
	}
	if (!bots_screen.active && bots_screen.lobby && held && !bots_screen.open_held)
		bots_screen_open();
	bots_screen.open_held = held;
}

void bots_screen_process(
	void)
{
	struct event_record event;

	while (bots_screen.active && get_next_event(&event, NONE))
	{
		if (event.type == BOTS_EVENT_BUTTON)
		{
			short button = event.data.button.index;

			if (button >= _gamepad_binary_button_dpad_up && button <= _gamepad_binary_button_dpad_right)
				held_press(button);
			else if (event.data.button.value == 1)
				press(button);
		}
		else if (event.type == BOTS_EVENT_LEFT_STICK)
		{
			if (event.data.stick.y == SHORT_MAX)
				held_press(_gamepad_binary_button_dpad_up);
			else if (event.data.stick.y == SHORT_MIN)
				held_press(_gamepad_binary_button_dpad_down);
			else if (event.data.stick.x == SHORT_MIN)
				held_press(_gamepad_binary_button_dpad_left);
			else if (event.data.stick.x == SHORT_MAX)
				held_press(_gamepad_binary_button_dpad_right);
		}
	}
	/* (the menu behind takes nothing while the screen is up) */
	event_manager_flush();
}

static float bots_screen_prompt(
	int button,
	char const *words,
	float x,
	float y)
{
	x += ui_overlay_button(button, 15.0f, x, y, 0xFFFFFFFF) + 3.0f;
	x += ui_overlay_text(UI_FONT_BOLD, 12.0f, x, y + 1.5f, UI_ALIGN_LEFT, COLOR_PROMPT, words);
	return x + 18.0f;
}

void bots_screen_render(
	void)
{
	char text[64];
	float x, y;
	short slot;

	if (!ui_overlay_available() || !bots_screen_host())
		return;
	if (!bots_screen.active)
	{
		/* (the lobby's hint, under its button key) */
		if (bots_screen.lobby)
		{
			ui_overlay_rect(80, 452, 104, 22, 4, COLOR_HINT_BACK);
			bots_screen_prompt(UI_BUTTON_Y, "BOTS", 86, 455);
		}
		return;
	}
	bots_screen_rows();
	ui_overlay_rect(-200, 0, 1040, 480, 0, COLOR_SHADE);
	ui_overlay_rect(120, 90, 400, 300, 8, COLOR_SCREEN_PANEL);
	ui_overlay_outline(120, 90, 400, 300, 8, 1.0f, COLOR_PANEL_EDGE);
	ui_overlay_text(UI_FONT_BOLD, 22.0f, 140, 102, UI_ALIGN_LEFT, COLOR_TITLE, "BOTS");
	y = 146;
	for (slot = 0; slot < bots_screen.row_count; slot++)
	{
		short row = bots_screen.rows[slot];
		boolean arrows = bots_screen_value(row, text, sizeof(text));
		char shown[80];

		if (slot == bots_screen.selected)
			ui_overlay_rect(136, y - 3, 368, 24, 3, COLOR_ROW_SELECTED);
		ui_overlay_text(UI_FONT_BOLD, 14.0f, 146, y, UI_ALIGN_LEFT, COLOR_TEXT, bots_row_labels[row]);
		snprintf(shown, sizeof(shown), arrows ? "< %s >" : "%s", text);
		ui_overlay_text(UI_FONT_BOLD, 14.0f, 494, y, UI_ALIGN_RIGHT, arrows ? COLOR_TEXT : COLOR_DIM, shown);
		y += 30;
	}
	if (bots_screen.row_count)
	{
		ui_overlay_text(UI_FONT_REGULAR, 12.0f, 140, 318, UI_ALIGN_LEFT, COLOR_DIM,
			bots_screen_help(bots_screen.rows[bots_screen.selected]));
	}
	x = bots_screen_prompt(UI_BUTTON_A, "Done", 140, 356);
	x = bots_screen_prompt(UI_BUTTON_X, "Defaults", x, 356);
	x += ui_overlay_button(UI_BUTTON_DPAD_LEFT, 15.0f, x, 356, 0xFFFFFFFF) + 2.0f;
	x += ui_overlay_button(UI_BUTTON_DPAD_RIGHT, 15.0f, x, 356, 0xFFFFFFFF) + 3.0f;
	ui_overlay_text(UI_FONT_BOLD, 12.0f, x, 357.5f, UI_ALIGN_LEFT, COLOR_PROMPT, "Change");
}

#endif
