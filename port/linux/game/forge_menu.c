/*
FORGE_MENU.C

The Forge menu (forge_menu.h): Forge's screens, drawn as the Delta Guide's
(guide.c) are - the same panel in the same place, the card's title, the
section tabs, the list's rows with the detail pane beside them, the setting
rows with their arrows, and the prompt bar, all of ui_overlay_style.c - and
driven the same way: while one is up it takes the first player's controller
and keyboard (guide_input.h; Q and E or the bumpers the tabs, the d-pad or
the arrows and WASD the rows, Enter, Space or A to choose, Escape,
Backspace or B to go back, Tab or Y, M or X), and the menus' pointer (a
click chooses, a right click goes back, the wheel moves). The menus'
sounds play as they do in Halo's menus (cursor, forward, back, error).

The lobby (FORGE in the Multiplayer menu): the multiplayer maps installed,
each with its layouts counted, on the left; for the one chosen, the pane:
- FORGE: NEW LAYOUT, then the map's saved layouts; A starts a Forge game of
  the map (forge_game_start), with the layout placed;
- PLAY: GAME TYPE (left and right: slayer, team slayer, CTF, king of the
  hill, oddball, race), then the layouts; A plays one as a game of that
  type (forge_play_start).
Y deletes the layout chosen, pressed twice (as the Guide's REMOVE).

In a Forge game, the monitor's X (controls.forge_menu on the keyboard)
opens it as a narrow column at the right of the view, the map in view at
its left (its title and the budget, the tabs, one list, the chosen row's
box, the prompts):
- OBJECTS (OBJECT BROWSER): the categories, A on one its objects (each
  with how many the layout has); A places one in the monitor's hand;
- PROPERTIES: what is held (an object or marker the layout placed), or
  with nothing held what the crosshair was on as X opened the menu, its
  rows as SETTINGS' (left and right change a value);
- LAYOUTS: SAVE (under the layout's title), SAVE AS NEW, then the map's
  layouts: A loads one (pressed twice: what isn't saved is lost), Y
  deletes one (pressed twice);
- SKY AND FOG: SKY, FOG, FOG COLOR, FOG DENSITY, FOG START, FOG END (the
  layout's sky and fog: forge_environment.inc), A or left and right steps
  one.
- OPTIONS: SNAP, MOVE SPEED, ROTATION SNAP, DELETE, GRAB RANGE, HELD
  FACING, UNDO, REDO, NEW ROUND, CLEAR DEAD AI (the layout's dead AI
  characters), CLEAR ALL AI (every one, pressed twice), PLAY AS SPARTAN, and
  CONTROLS, the monitor's buttons and keys.
X or B closes it. What it asks of the session is done by forge.c before the
next tick (forge_menu_ask); what it shows is read from forge.c each frame
(forge_menu_view_get) as it is input, and drawn from that.

The game goes on under it (a Forge game is local: nothing waits on it); the
monitor's camera stays.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "halo_console_features.h"
#include "halo_port_features.h"

#include "forge_menu.h"

#if defined(HALO_GUIDE) && !defined(HALO_CONSOLE_NO_FORGE)

#include "forge.h"
#include "guide.h"
#include "halo_keyboard.h"
#include "halo_ui_pointer.h"
#include "../src/forge_store.h"
#include "../src/guide_input.h"
#include "../src/ui_overlay.h"

#include <stdio.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);
char const *config_string(char const *name);
int config_write(char const *name, char const *value);
boolean main_menu_is_active(void);
boolean cache_files_map_installed(char const *map_name);
/* ui_widget.c's: the menus' sounds (its _ui_audio_feedback_*) */
void ui_play_audio_feedback_sound(short audio_feedback);

/* ---------- constants */

/* the panel's place, in the menus' 640x480: the Guide's */
enum
{
	PANEL_X = 48, PANEL_Y = 38, PANEL_WIDTH = 544, PANEL_HEIGHT = 398,
	PANEL_RIGHT = PANEL_X + PANEL_WIDTH,
	INSET = 16,
	CONTENT_LEFT = PANEL_X + INSET,
	CONTENT_RIGHT = PANEL_RIGHT - INSET,
	HEADER_Y = PANEL_Y + 12,
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
	OPTION_HEIGHT = 24,
	OPTION_GAP = 4,
	HEADING_HEIGHT = 16,

	/* a d-pad held: again after this long, then this often (the Guide's) */
	REPEAT_DELAY = 400,
	REPEAT_INTERVAL = 120,
	/* a status line, shown this long */
	MESSAGE_TIME = 6000,
	/* the most of the pointer's targets */
	MAXIMUM_TARGETS = 64,
	/* the most maps in the lobby */
	MAXIMUM_MAPS = 32,
	/* (the layer it draws on: the other screens' dialogs' 3, under the Guide's 4) */
	FORGE_MENU_LAYER = 3,
};

/* the menus' sounds (ui_widget.c's enum ui_audio_feedback_sound) */
enum
{
	SOUND_CURSOR = 1,
	SOUND_FORWARD = 2,
	SOUND_BACK = 3,
	SOUND_ERROR = 4,
};

enum
{
	_screen_none = 0,
	_screen_lobby,
	_screen_game,
};

enum
{
	_lobby_tab_forge = 0,
	_lobby_tab_play,
	NUMBER_OF_LOBBY_TABS
};

enum
{
	_game_tab_objects = 0,
	_game_tab_properties,
	_game_tab_layouts,
	_game_tab_sky,
	_game_tab_options,
	NUMBER_OF_GAME_TABS
};

/* SKY AND FOG's rows: forge.h's _forge_environment_* (SKY, FOG, FOG COLOR,
FOG DENSITY, FOG START, FOG END), the layout's sky and fog */
static char const *const sky_labels[NUMBER_OF_FORGE_ENVIRONMENT_ROWS] =
{
	"SKY", "FOG", "FOG COLOR", "FOG DENSITY", "FOG START", "FOG END",
};

/* OPTIONS' rows */
enum
{
	_option_snap = 0,
	_option_speed,
	_option_rotate_snap,
	_option_delete,
	_option_grab_range,
	_option_held_facing,
	_option_undo,
	_option_redo,
	_option_round,
	_option_clear_dead_ai,
	_option_clear_all_ai,
	_option_spartan,
	_option_controls,
	NUMBER_OF_OPTIONS
};

/* LAYOUTS' first rows, before the layouts */
enum
{
	_layouts_save = 0,
	_layouts_save_new,
	NUMBER_OF_LAYOUTS_ACTIONS
};

/* what a click lands on */
enum
{
	_target_tab,
	_target_list,
	_target_pane,
	_target_prompt,
};

static char const *const lobby_tab_names[NUMBER_OF_LOBBY_TABS] = { "FORGE", "PLAY" };
static char const *const game_tab_names[NUMBER_OF_GAME_TABS] = { "OBJECTS", "PROPERTIES", "LAYOUTS", "SKY AND FOG", "OPTIONS" };

/* the game types a layout is played as (forge_play_start's words) */
static struct
{
	char const *word;
	char const *name;
} const game_types[] =
{
	{ "slayer", "SLAYER" },
	{ "teamslayer", "TEAM SLAYER" },
	{ "ctf", "CAPTURE THE FLAG" },
	{ "king", "KING OF THE HILL" },
	{ "oddball", "ODDBALL" },
	{ "race", "RACE" },
};

static char const *const option_labels[NUMBER_OF_OPTIONS] =
{
	"SNAP", "MOVE SPEED", "ROTATION SNAP", "DELETE", "GRAB RANGE", "HELD FACING", "UNDO", "REDO", "NEW ROUND", "CLEAR DEAD AI",
	"CLEAR ALL AI", "PLAY AS SPARTAN", "CONTROLS",
};


/* CONTROLS: each of the monitor's controls, the controller's (its words),
and its key (halo_forge_key, NONE: the movement keys or the mouse) */
static struct
{
	char const *what;
	char const *button;
	short key;
	char const *keys;
} const controls[] =
{
	{ "FLY", "LEFT STICK", NONE, "MOVEMENT KEYS" },
	{ "LOOK", "RIGHT STICK", NONE, "MOUSE" },
	{ "UP / DOWN", "A / LEFT STICK CLICK", HALO_FORGE_KEY_UP, NULL },
	{ "FAST", "LT", HALO_FORGE_KEY_FAST, NULL },
	{ "GRAB / LET GO", "RT", HALO_FORGE_KEY_GRAB, NULL },
	{ "ROTATE (HOLDING)", "LT HELD", HALO_FORGE_KEY_ROTATE, NULL },
	{ "NEARER / FARTHER", "LEFT STICK (LT HELD)", HALO_FORGE_KEY_FARTHER, NULL },
	{ "COPY", "B", HALO_FORGE_KEY_COPY, NULL },
	{ "DELETE", "Y", HALO_FORGE_KEY_DELETE, NULL },
	{ "UPRIGHT", "RB", HALO_FORGE_KEY_UPRIGHT, NULL },
	{ "SPEED / ROTATION SNAP", "LB", HALO_FORGE_KEY_SPEED, NULL },
	{ "NUDGE MODE", "LT + RIGHT STICK CLICK", HALO_FORGE_KEY_NUDGE, NULL },
	{ "FORGE MENU", "X", HALO_FORGE_KEY_MENU, NULL },
	{ "MONITOR / SPARTAN", "BACK + RIGHT STICK CLICK", HALO_FORGE_KEY_MONITOR, NULL },
};

/* ---------- globals */

static struct
{
	short screen;
	unsigned long opened_time;
	/* what was held last frame (all of it as it opens: not pressed in it),
	and a d-pad held */
	unsigned int held;
	unsigned int repeat_held;
	unsigned long repeat_time;
	/* the tab, the list's row and the first shown, whether the pane has the
	focus, its row and its first shown */
	short tab;
	short list_row[NUMBER_OF_GAME_TABS];
	short list_first;
	boolean pane;
	short pane_row;
	short pane_first;
	/* a row pressed once of two (DELETE, LOAD): its tab and row, NONE */
	short armed_tab;
	short armed_row;
	short armed_button;
	/* the status line */
	char message[160];
	boolean message_bad;
	unsigned long message_time;
	/* the lobby: its maps (forge_maps' indices), the layouts of all of
	them, the game type PLAY plays */
	short maps[MAXIMUM_MAPS];
	short map_count;
	struct forge_store_entry entries[FORGE_STORE_MAXIMUM_LISTED];
	short entry_count;
	short game_type;
	short map_row;
	/* the game's: the session as it is, the layouts listed as of which
	status line, OPTIONS' CONTROLS shown */
	struct forge_menu_view view;
	unsigned long entries_status_time;
	boolean controls_shown;
	/* the pointer's targets, drawn this frame */
	struct
	{
		float x0, y0, x1, y1;
		short kind;
		short index;
	} targets[MAXIMUM_TARGETS];
	short target_count;
} forge_menu = { _screen_none };

/* ---------- private code */

static void target_add(float x0, float y0, float x1, float y1, short kind, short index)
{
	if (forge_menu.target_count >= MAXIMUM_TARGETS)
		return;
	forge_menu.targets[forge_menu.target_count].x0 = x0;
	forge_menu.targets[forge_menu.target_count].y0 = y0;
	forge_menu.targets[forge_menu.target_count].x1 = x1;
	forge_menu.targets[forge_menu.target_count].y1 = y1;
	forge_menu.targets[forge_menu.target_count].kind = kind;
	forge_menu.targets[forge_menu.target_count].index = index;
	forge_menu.target_count++;
}

static void sound(short which)
{
	ui_play_audio_feedback_sound(which);
}

static void message_set(boolean bad, char const *text)
{
	snprintf(forge_menu.message, sizeof(forge_menu.message), "%s", text);
	forge_menu.message_bad = bad;
	forge_menu.message_time = system_milliseconds();
}

static void disarm(void)
{
	forge_menu.armed_tab = NONE;
	forge_menu.armed_row = NONE;
	forge_menu.armed_button = 0;
}

static boolean armed(short row, unsigned int button)
{
	return forge_menu.armed_tab == forge_menu.tab && forge_menu.armed_row == row && forge_menu.armed_button == (short)button;
}

/* a layout of a map: its index among the entries of that map's, NULL past
them */
static struct forge_store_entry const *map_entry(short map_index, short which)
{
	char const *map_name = forge_map_short(map_index);
	short index;

	for (index = 0; index < forge_menu.entry_count; index++)
	{
		if (!strcmp(forge_menu.entries[index].map_name, map_name) && which-- == 0)
			return &forge_menu.entries[index];
	}
	return NULL;
}

static short map_entry_count(short map_index)
{
	char const *map_name = forge_map_short(map_index);
	short index, count = 0;

	for (index = 0; index < forge_menu.entry_count; index++)
	{
		if (!strcmp(forge_menu.entries[index].map_name, map_name))
			count++;
	}
	return count;
}

static void entries_read(short map_index)
{
	int count = forge_store_list(map_index != NONE ? forge_map_short(map_index) : NULL, forge_menu.entries,
		FORGE_STORE_MAXIMUM_LISTED);

	forge_menu.entry_count = (short)(count > 0 ? count : 0);
}

/* SAVE AS NEW's title: one no layout of the session's map has ("<map>
Layout", then "<map> Layout 2"...), from the layouts read */
static void new_title(char *title, int size)
{
	short count = forge_menu.view.map_index != NONE ? map_entry_count(forge_menu.view.map_index) : 0;
	int number;

	for (number = 1; number < 1000; number++)
	{
		short index;

		if (number == 1)
			snprintf(title, (size_t)size, "%s Layout", forge_menu.view.level);
		else
			snprintf(title, (size_t)size, "%s Layout %d", forge_menu.view.level, number);
		for (index = 0; index < count; index++)
		{
			if (!strcmp(map_entry(forge_menu.view.map_index, index)->title, title))
				break;
		}
		if (index == count && strcmp(title, forge_menu.view.title))
			return;
	}
}

/* the input the Guide's way takes it: the first player's controller and
keyboard (guide_input.h) */
static void capture(boolean take)
{
	/* (the Guide over it has the input, and gives it back as it closes) */
	if (guide_active())
		return;
	platform_guide_capture(take);
}

static void opened(short screen)
{
	forge_menu.screen = screen;
	forge_menu.opened_time = system_milliseconds();
	forge_menu.held = 0xFFFFFFFF;
	forge_menu.repeat_held = 0;
	forge_menu.target_count = 0;
	forge_menu.pane = FALSE;
	forge_menu.pane_row = 0;
	forge_menu.pane_first = 0;
	forge_menu.list_first = 0;
	forge_menu.controls_shown = FALSE;
	forge_menu.message[0] = 0;
	disarm();
	capture(TRUE);
}

static void close_screen(void)
{
	if (forge_menu.screen == _screen_none)
		return;
	platform_log("forge: the %s closed", forge_menu.screen == _screen_lobby ? "lobby" : "Forge menu");
	forge_menu.screen = _screen_none;
	capture(FALSE);
}

static unsigned int presses(void)
{
	unsigned int held = platform_guide_held();
	unsigned int pressed = held & ~forge_menu.held;
	unsigned int directions = held & (GUIDE_INPUT_UP | GUIDE_INPUT_DOWN | GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT);
	unsigned long now = system_milliseconds();

	if (directions && directions == forge_menu.repeat_held)
	{
		if (now - forge_menu.repeat_time >= REPEAT_DELAY)
		{
			pressed |= directions;
			forge_menu.repeat_time = now - REPEAT_DELAY + REPEAT_INTERVAL;
		}
	}
	else
	{
		forge_menu.repeat_held = directions;
		forge_menu.repeat_time = now;
	}
	forge_menu.held = held;
	return pressed;
}

/* a row moved to within count (wrapping as the menus' lists do not: held
at the ends, as the Guide's) */
static boolean step_row(short *row, short count, short step)
{
	short was = *row;

	if (count <= 0)
	{
		*row = 0;
		return FALSE;
	}
	*row = (short)PIN(*row + step, 0, count - 1);
	return *row != was;
}

/* ---- the lobby */

static short lobby_map(void)
{
	short row = forge_menu.map_row;

	return row >= 0 && row < forge_menu.map_count ? forge_menu.maps[row] : NONE;
}

/* the pane's rows: FORGE's NEW LAYOUT, PLAY's GAME TYPE, then the layouts */
static short lobby_pane_rows(void)
{
	short map_index = lobby_map();

	return map_index == NONE ? 0 : (short)(1 + map_entry_count(map_index));
}

static void lobby_start(void)
{
	short map_index = lobby_map();
	short row = forge_menu.pane_row;
	struct forge_store_entry const *entry = row > 0 ? map_entry(map_index, (short)(row - 1)) : NULL;
	char why[FORGE_STORE_WHY_SIZE];
	boolean started;

	if (map_index == NONE)
		return;
	if (forge_menu.tab == _lobby_tab_play && row == 0)
	{
		forge_menu.game_type = (short)((forge_menu.game_type + 1) % (short)NUMBEROF(game_types));
		sound(SOUND_CURSOR);
		return;
	}
	if (entry && entry->damaged)
	{
		sound(SOUND_ERROR);
		message_set(TRUE, "This layout is damaged. It can only be deleted.");
		return;
	}
	why[0] = 0;
	if (forge_menu.tab == _lobby_tab_play)
	{
		platform_log("forge: the lobby's PLAY: %s, %s, %s", forge_map_short(map_index), entry ? entry->file_name : "",
			game_types[forge_menu.game_type].word);
		started = entry && forge_play_start(map_index, entry->file_name, game_types[forge_menu.game_type].word, why,
			sizeof(why));
	}
	else
	{
		platform_log("forge: the lobby's FORGE: %s, %s", forge_map_short(map_index), entry ? entry->file_name : "new layout");
		started = forge_game_start(map_index, entry ? entry->file_name : NULL, why, sizeof(why));
	}
	if (!started)
	{
		sound(SOUND_ERROR);
		message_set(TRUE, why[0] ? why : "That can't be started.");
		return;
	}
	sound(SOUND_FORWARD);
	close_screen();
}

static void lobby_delete(void)
{
	short map_index = lobby_map();
	struct forge_store_entry const *entry =
		forge_menu.pane && forge_menu.pane_row > 0 ? map_entry(map_index, (short)(forge_menu.pane_row - 1)) : NULL;

	if (!entry)
		return;
	if (!armed(forge_menu.pane_row, GUIDE_INPUT_Y))
	{
		forge_menu.armed_tab = forge_menu.tab;
		forge_menu.armed_row = forge_menu.pane_row;
		forge_menu.armed_button = GUIDE_INPUT_Y;
		sound(SOUND_CURSOR);
		return;
	}
	disarm();
	if (forge_store_delete(entry->map_name, entry->file_name))
	{
		char text[160];

		platform_log("forge: the lobby's DELETE: %s/%s", entry->map_name, entry->file_name);
		snprintf(text, sizeof(text), "Deleted \"%s\".", entry->title);
		message_set(FALSE, text);
		sound(SOUND_FORWARD);
		entries_read(NONE);
		forge_menu.pane_row = (short)MIN(forge_menu.pane_row, lobby_pane_rows() - 1);
	}
	else
	{
		message_set(TRUE, "That layout can't be deleted.");
		sound(SOUND_ERROR);
	}
}

static void lobby_handle(unsigned int pressed)
{
	short rows = lobby_pane_rows();

	if (pressed & (GUIDE_INPUT_LEFT_BUMPER | GUIDE_INPUT_RIGHT_BUMPER))
	{
		forge_menu.tab = (short)((forge_menu.tab + ((pressed & GUIDE_INPUT_LEFT_BUMPER) ? NUMBER_OF_LOBBY_TABS - 1 : 1)) %
			NUMBER_OF_LOBBY_TABS);
		forge_menu.pane_row = 0;
		forge_menu.pane_first = 0;
		disarm();
		sound(SOUND_CURSOR);
		return;
	}
	if (pressed & GUIDE_INPUT_B)
	{
		sound(SOUND_BACK);
		disarm();
		if (forge_menu.pane)
			forge_menu.pane = FALSE;
		else
			close_screen();
		return;
	}
	if (!forge_menu.pane)
	{
		if (pressed & (GUIDE_INPUT_UP | GUIDE_INPUT_DOWN))
		{
			if (step_row(&forge_menu.map_row, forge_menu.map_count, (short)((pressed & GUIDE_INPUT_UP) ? -1 : 1)))
				sound(SOUND_CURSOR);
			forge_menu.pane_row = 0;
			forge_menu.pane_first = 0;
		}
		if ((pressed & (GUIDE_INPUT_A | GUIDE_INPUT_RIGHT)) && lobby_map() != NONE)
		{
			forge_menu.pane = TRUE;
			sound(SOUND_FORWARD);
		}
		return;
	}
	if (pressed & (GUIDE_INPUT_UP | GUIDE_INPUT_DOWN))
	{
		if (step_row(&forge_menu.pane_row, rows, (short)((pressed & GUIDE_INPUT_UP) ? -1 : 1)))
		{
			sound(SOUND_CURSOR);
			disarm();
		}
	}
	/* (PLAY's GAME TYPE: left and right its values) */
	if (forge_menu.tab == _lobby_tab_play && forge_menu.pane_row == 0 && (pressed & (GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT)))
	{
		short count = (short)NUMBEROF(game_types);

		forge_menu.game_type = (short)((forge_menu.game_type + ((pressed & GUIDE_INPUT_LEFT) ? count - 1 : 1)) % count);
		sound(SOUND_CURSOR);
		return;
	}
	if (pressed & GUIDE_INPUT_LEFT)
	{
		forge_menu.pane = FALSE;
		disarm();
		sound(SOUND_BACK);
		return;
	}
	if (pressed & GUIDE_INPUT_A)
		lobby_start();
	else if (pressed & GUIDE_INPUT_Y)
		lobby_delete();
}

/* ---- the game's */

static short game_list_rows(void)
{
	switch (forge_menu.tab)
	{
	case _game_tab_objects:
		return NUMBER_OF_FORGE_CATEGORIES;
	case _game_tab_properties:
		return forge_property_count();
	case _game_tab_layouts:
		return (short)(NUMBER_OF_LAYOUTS_ACTIONS + (forge_menu.view.map_index != NONE ?
			map_entry_count(forge_menu.view.map_index) : 0));
	case _game_tab_sky:
		return NUMBER_OF_FORGE_ENVIRONMENT_ROWS;
	case _game_tab_options:
		return forge_menu.controls_shown ? 0 : NUMBER_OF_OPTIONS;
	}
	return 0;
}

static void game_tab_go(short tab)
{
	forge_menu.tab = tab;
	forge_menu.pane = FALSE;
	forge_menu.pane_row = 0;
	forge_menu.pane_first = 0;
	forge_menu.list_first = 0;
	forge_menu.controls_shown = FALSE;
	disarm();
	if (tab == _game_tab_layouts)
		entries_read(forge_menu.view.map_index);
}

/* OPTIONS' DELETE: game.forge_hold_to_delete's values, in the row's order, and
the row's words for them */
static char const *const delete_modes[] = { "off", "controller", "all" };
static char const *const delete_mode_names[] = { "PRESS", "HOLD: CONTROLLER", "HOLD: ALL" };

static short delete_mode(void)
{
	char const *mode = config_string("game.forge_hold_to_delete");
	short index;

	for (index = 0; index < (short)NUMBEROF(delete_modes); index++)
	{
		if (mode && !strcmp(mode, delete_modes[index]))
			return index;
	}
	return 0;
}

/* OPTIONS' GRAB RANGE: game.forge_grab_range's values, in the row's
order, and the row's words for them */
static char const *const grab_ranges[] = { "near", "medium", "far" };
static char const *const grab_range_names[] = { "NEAR", "MEDIUM", "FAR" };

static short grab_range(void)
{
	char const *range = config_string("game.forge_grab_range");
	short index;

	for (index = 0; index < (short)NUMBEROF(grab_ranges); index++)
	{
		if (range && !strcmp(range, grab_ranges[index]))
			return index;
	}
	return (short)NUMBEROF(grab_ranges) - 1;
}

/* OPTIONS' HELD FACING: game.forge_held_facing's values, in the row's
order, and the row's words for them */
static char const *const held_facings[] = { "keep", "view" };
static char const *const held_facing_names[] = { "KEEP", "FOLLOW VIEW" };

static short held_facing(void)
{
	char const *facing = config_string("game.forge_held_facing");

	return (short)(facing && !strcmp(facing, held_facings[1]) ? 1 : 0);
}

static void game_option_step(short row, short step)
{
	switch (row)
	{
	case _option_snap:
		forge_menu_ask(_forge_menu_ask_snap, 0, 0, NULL);
		break;
	case _option_speed:
		forge_menu_ask(_forge_menu_ask_speed, step, 0, NULL);
		break;
	case _option_rotate_snap:
		forge_menu_ask(_forge_menu_ask_rotate_snap, step, 0, NULL);
		break;
	case _option_delete:
	{
		short count = (short)NUMBEROF(delete_modes);

		config_write("game.forge_hold_to_delete", delete_modes[(delete_mode() + count + (step < 0 ? -1 : 1)) % count]);
		break;
	}
	case _option_grab_range:
	{
		short count = (short)NUMBEROF(grab_ranges);

		config_write("game.forge_grab_range", grab_ranges[(grab_range() + count + (step < 0 ? -1 : 1)) % count]);
		break;
	}
	case _option_held_facing:
		config_write("game.forge_held_facing", held_facings[held_facing() ? 0 : 1]);
		break;
	default:
		return;
	}
	sound(SOUND_CURSOR);
}

static void game_option_choose(short row)
{
	switch (row)
	{
	case _option_snap:
	case _option_speed:
	case _option_rotate_snap:
	case _option_delete:
	case _option_grab_range:
	case _option_held_facing:
		game_option_step(row, 1);
		return;
	case _option_undo:
		forge_menu_ask(_forge_menu_ask_undo, 0, 0, NULL);
		break;
	case _option_redo:
		forge_menu_ask(_forge_menu_ask_redo, 0, 0, NULL);
		break;
	case _option_round:
		forge_menu_ask(_forge_menu_ask_round, 0, 0, NULL);
		close_screen();
		break;
	case _option_clear_dead_ai:
		forge_menu_ask(_forge_menu_ask_clear_ai, 0, 0, NULL);
		break;
	case _option_clear_all_ai:
		/* (pressed twice, as a layout's DELETE) */
		if (!armed(row, GUIDE_INPUT_A))
		{
			forge_menu.armed_tab = forge_menu.tab;
			forge_menu.armed_row = row;
			forge_menu.armed_button = GUIDE_INPUT_A;
			sound(SOUND_CURSOR);
			return;
		}
		disarm();
		forge_menu_ask(_forge_menu_ask_clear_ai, 1, 0, NULL);
		break;
	case _option_spartan:
		forge_menu_ask(_forge_menu_ask_spartan, 0, 0, NULL);
		close_screen();
		break;
	case _option_controls:
		forge_menu.controls_shown = TRUE;
		break;
	}
	sound(SOUND_FORWARD);
}

static void game_layouts_choose(short row, unsigned int button)
{
	struct forge_store_entry const *entry = row >= NUMBER_OF_LAYOUTS_ACTIONS ?
		map_entry(forge_menu.view.map_index, (short)(row - NUMBER_OF_LAYOUTS_ACTIONS)) : NULL;

	if (button == GUIDE_INPUT_A && row == _layouts_save)
	{
		forge_menu_ask(_forge_menu_ask_save, 0, 0, NULL);
		sound(SOUND_FORWARD);
		return;
	}
	if (button == GUIDE_INPUT_A && row == _layouts_save_new)
	{
		char title[FORGE_FILE_TITLE_SIZE];

		new_title(title, sizeof(title));
		forge_menu_ask(_forge_menu_ask_save_new, 0, 0, title);
		sound(SOUND_FORWARD);
		return;
	}
	if (!entry)
		return;
	if (button == GUIDE_INPUT_A && entry->damaged)
	{
		sound(SOUND_ERROR);
		message_set(TRUE, "This layout is damaged. It can only be deleted.");
		return;
	}
	if (!armed(row, button))
	{
		forge_menu.armed_tab = forge_menu.tab;
		forge_menu.armed_row = row;
		forge_menu.armed_button = (short)button;
		sound(SOUND_CURSOR);
		return;
	}
	disarm();
	sound(SOUND_FORWARD);
	if (button == GUIDE_INPUT_Y)
	{
		forge_menu_ask(_forge_menu_ask_delete, 0, 0, entry->file_name);
	}
	else
	{
		forge_menu_ask(_forge_menu_ask_load, 0, 0, entry->file_name);
		close_screen();
	}
}

static void game_handle(unsigned int pressed)
{
	short rows = game_list_rows();
	short *row = &forge_menu.list_row[forge_menu.tab];

	if (pressed & (GUIDE_INPUT_LEFT_BUMPER | GUIDE_INPUT_RIGHT_BUMPER))
	{
		game_tab_go((short)((forge_menu.tab + ((pressed & GUIDE_INPUT_LEFT_BUMPER) ? NUMBER_OF_GAME_TABS - 1 : 1)) %
			NUMBER_OF_GAME_TABS));
		sound(SOUND_CURSOR);
		return;
	}
	if (pressed & GUIDE_INPUT_X)
	{
		sound(SOUND_BACK);
		close_screen();
		return;
	}
	if (pressed & GUIDE_INPUT_B)
	{
		sound(SOUND_BACK);
		disarm();
		if (forge_menu.pane)
			forge_menu.pane = FALSE;
		else if (forge_menu.controls_shown)
			forge_menu.controls_shown = FALSE;
		else
			close_screen();
		return;
	}
	*row = (short)(rows > 0 ? PIN(*row, 0, rows - 1) : 0);
	/* (OBJECTS' pane: the category's objects) */
	if (forge_menu.tab == _game_tab_objects && forge_menu.pane)
	{
		short items = forge_category_item_count(*row);

		if (pressed & (GUIDE_INPUT_UP | GUIDE_INPUT_DOWN))
		{
			if (step_row(&forge_menu.pane_row, items, (short)((pressed & GUIDE_INPUT_UP) ? -1 : 1)))
				sound(SOUND_CURSOR);
		}
		if (pressed & GUIDE_INPUT_LEFT)
		{
			forge_menu.pane = FALSE;
			sound(SOUND_BACK);
		}
		else if ((pressed & GUIDE_INPUT_A) && forge_menu.pane_row < items)
		{
			struct forge_item item;

			if (forge_category_item(*row, forge_menu.pane_row, &item))
				platform_log("forge: the menu's OBJECTS: %s, %s", forge_category_name(*row), item.name);
			forge_menu_ask(_forge_menu_ask_place, *row, forge_menu.pane_row, NULL);
			sound(SOUND_FORWARD);
			close_screen();
		}
		return;
	}
	if (pressed & (GUIDE_INPUT_UP | GUIDE_INPUT_DOWN))
	{
		if (step_row(row, rows, (short)((pressed & GUIDE_INPUT_UP) ? -1 : 1)))
		{
			sound(SOUND_CURSOR);
			disarm();
			forge_menu.pane_row = 0;
			forge_menu.pane_first = 0;
		}
	}
	switch (forge_menu.tab)
	{
	case _game_tab_objects:
		if ((pressed & (GUIDE_INPUT_A | GUIDE_INPUT_RIGHT)) && forge_category_item_count(*row) > 0)
		{
			forge_menu.pane = TRUE;
			forge_menu.pane_row = 0;
			forge_menu.pane_first = 0;
			sound(SOUND_FORWARD);
		}
		else if (pressed & GUIDE_INPUT_A)
		{
			sound(SOUND_ERROR);
		}
		break;
	case _game_tab_properties:
		if (rows > 0 && (pressed & (GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT | GUIDE_INPUT_A)))
		{
			forge_menu_ask(_forge_menu_ask_property, *row, (short)((pressed & GUIDE_INPUT_LEFT) ? -1 : 1), NULL);
			sound(SOUND_CURSOR);
		}
		break;
	case _game_tab_layouts:
		if (pressed & GUIDE_INPUT_A)
			game_layouts_choose(*row, GUIDE_INPUT_A);
		else if (pressed & GUIDE_INPUT_Y)
			game_layouts_choose(*row, GUIDE_INPUT_Y);
		break;
	case _game_tab_sky:
		if (pressed & (GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT | GUIDE_INPUT_A))
		{
			forge_menu_ask(_forge_menu_ask_environment, *row, (short)((pressed & GUIDE_INPUT_LEFT) ? -1 : 1), NULL);
			sound(SOUND_CURSOR);
		}
		break;
	case _game_tab_options:
		if (forge_menu.controls_shown)
			break;
		if (pressed & (GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT))
			game_option_step(*row, (short)((pressed & GUIDE_INPUT_LEFT) ? -1 : 1));
		else if (pressed & GUIDE_INPUT_A)
			game_option_choose(*row);
		break;
	}
}

static void handle(unsigned int pressed)
{
	if (!pressed)
		return;
	if (forge_menu.screen == _screen_lobby)
		lobby_handle(pressed);
	else if (forge_menu.screen == _screen_game)
		game_handle(pressed);
}

/* ---- drawing */

/* the first row shown of a list of count rows, shown at once, with row
chosen in it */
static short scrolled(short first, short row, short count, short shown)
{
	if (row < first)
		first = row;
	else if (row >= first + shown)
		first = (short)(row - shown + 1);
	if (first > count - shown)
		first = (short)(count - shown);
	return (short)MAX(first, 0);
}

/* a list's row as the Guide's players and friends are: its name, under it
a status line, a tag at its right */
static void list_row(float x, float y, float width, boolean on, char const *name, char const *status, char const *tag,
	boolean warning, short kind, short index)
{
	ui_style_row(x, y, width, ROW_HEIGHT, on);
	if (status && status[0])
	{
		ui_style_text(UI_TEXT_NAME, x + 12, y + 3, UI_ALIGN_LEFT, name);
		ui_style_text(warning ? UI_TEXT_WARNING : UI_TEXT_STATUS, x + 12, y + 17, UI_ALIGN_LEFT, status);
	}
	else
	{
		ui_style_text(UI_TEXT_NAME, x + 12, y + (ROW_HEIGHT - ui_style_text_size(UI_TEXT_NAME)) * 0.5f, UI_ALIGN_LEFT, name);
	}
	if (tag && tag[0])
	{
		ui_overlay_text(UI_FONT_MENU, 9.5f, x + width - 12, y + (ROW_HEIGHT - 9.5f) * 0.5f, UI_ALIGN_RIGHT,
			ui_style_color(UI_COLOR_NAME), tag);
	}
	target_add(x, y, x + width, y + ROW_HEIGHT, kind, index);
}

/* a button of the pane (an option of the Guide's friend's pane) */
static void pane_option(float x, float y, float width, boolean on, char const *label, char const *tag, short index)
{
	ui_style_row(x, y, width, OPTION_HEIGHT, on);
	ui_overlay_text(UI_FONT_MENU, 10.0f, x + 10, y + 7, UI_ALIGN_LEFT, 0xFFFFFFFF, label);
	if (tag && tag[0])
	{
		ui_overlay_text(UI_FONT_MENU, 9.0f, x + width - 10, y + 7.5f, UI_ALIGN_RIGHT, ui_style_color(UI_COLOR_LABEL_SOFT),
			tag);
	}
	target_add(x, y, x + width, y + OPTION_HEIGHT, _target_pane, index);
}

static void render_header(char const *name, char const *status)
{
	ui_style_card(CONTENT_LEFT, CONTENT_RIGHT, HEADER_Y, NULL, NULL, 0, "FORGE");
	/* (the card's name and status line, without a player's emblem) */
	if (name && name[0])
		ui_overlay_text(UI_FONT_MENU, 15.0f, CONTENT_LEFT, HEADER_Y + 1, UI_ALIGN_LEFT, ui_style_color(UI_COLOR_NAME), name);
	if (status && status[0])
	{
		ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, CONTENT_LEFT, HEADER_Y + 20, UI_ALIGN_LEFT,
			ui_style_color(UI_COLOR_STATUS), status);
	}
}

static void render_tabs(char const *const *names, short count)
{
	float starts[NUMBER_OF_GAME_TABS], ends[NUMBER_OF_GAME_TABS];
	short tab;

	ui_style_tabs(CONTENT_LEFT, CONTENT_RIGHT, TABS_Y, TABS_HEIGHT, names, count, forge_menu.tab, "Q", "E", starts, ends);
	for (tab = 0; tab < count; tab++)
		target_add(starts[tab], TABS_Y, ends[tab], TABS_Y + TABS_HEIGHT, _target_tab, tab);
}

static void prompt_add(struct ui_style_prompt *prompts, short *count, int button, char const *key, char const *word)
{
	prompts[*count].button = button;
	prompts[*count].key = key;
	prompts[(*count)++].word = word;
}

static void render_prompts(struct ui_style_prompt const *prompts, short count)
{
	float starts[6], ends[6];
	short index;

	ui_style_prompts(CONTENT_RIGHT, PROMPTS_Y, 15.0f, prompts, count, starts, ends);
	for (index = 0; index < count; index++)
	{
		target_add(starts[index] - 4, PROMPTS_Y - 4, ends[index] + 4, PROMPTS_Y + 19, _target_prompt,
			(short)prompts[index].button);
	}
}

/* the status line, a while, at y */
static void render_message(float x, float y)
{
	if (forge_menu.message[0] && system_milliseconds() - forge_menu.message_time < MESSAGE_TIME)
		ui_style_text(forge_menu.message_bad ? UI_TEXT_WARNING : UI_TEXT_LINE, x, y, UI_ALIGN_LEFT, forge_menu.message);
}

/* the pane's glass, beside the list */
static void render_pane_glass(void)
{
	ui_style_glass(DETAIL_LEFT, BODY_Y, CONTENT_RIGHT - DETAIL_LEFT, BODY_BOTTOM - BODY_Y, 8, ui_style_color(UI_COLOR_PANE),
		ui_style_color(UI_COLOR_ROW_EDGE));
}

/* a layout's status line: its objects and its date */
static void entry_status(struct forge_store_entry const *entry, char *text, int size)
{
	if (entry->damaged)
		snprintf(text, (size_t)size, "%s", "Damaged: can only be deleted");
	else
		snprintf(text, (size_t)size, "%d object%s - %s", entry->objects, entry->objects == 1 ? "" : "s", entry->date);
}

static void render_lobby(void)
{
	struct ui_style_prompt prompts[5];
	short prompt_count = 0;
	short map_index = lobby_map();
	short row, shown, first, rows = lobby_pane_rows();
	float y, x = DETAIL_LEFT + 12, width = CONTENT_RIGHT - DETAIL_LEFT - 24;
	char heading[64], status[96];

	render_header("CHOOSE A MAP", NULL);
	render_tabs(lobby_tab_names, NUMBER_OF_LOBBY_TABS);
	/* the maps */
	snprintf(heading, sizeof(heading), "MAPS (%d)", forge_menu.map_count);
	ui_style_text(UI_TEXT_HEADING, CONTENT_LEFT, BODY_Y, UI_ALIGN_LEFT, heading);
	shown = (short)((BODY_BOTTOM - (BODY_Y + HEADING_HEIGHT) + ROW_GAP) / (ROW_HEIGHT + ROW_GAP));
	first = forge_menu.list_first = scrolled(forge_menu.list_first, forge_menu.map_row, forge_menu.map_count, shown);
	y = BODY_Y + HEADING_HEIGHT;
	for (row = first; row < forge_menu.map_count && row < first + shown; row++)
	{
		short count = map_entry_count(forge_menu.maps[row]);
		boolean chosen = row == forge_menu.map_row;

		if (count)
			snprintf(status, sizeof(status), "%d layout%s", count, count == 1 ? "" : "s");
		else
			snprintf(status, sizeof(status), "%s", "No layouts yet");
		if (chosen && forge_menu.pane)
		{
			ui_style_row_chosen(CONTENT_LEFT, y, LIST_WIDTH, ROW_HEIGHT);
			ui_style_text(UI_TEXT_NAME, CONTENT_LEFT + 12, y + 3, UI_ALIGN_LEFT, forge_map_name(forge_menu.maps[row]));
			ui_style_text(UI_TEXT_STATUS, CONTENT_LEFT + 12, y + 17, UI_ALIGN_LEFT, status);
			target_add(CONTENT_LEFT, y, CONTENT_LEFT + LIST_WIDTH, y + ROW_HEIGHT, _target_list, row);
		}
		else
		{
			list_row(CONTENT_LEFT, y, LIST_WIDTH, chosen, forge_map_name(forge_menu.maps[row]), status, NULL, FALSE,
				_target_list, row);
		}
		y += ROW_HEIGHT + ROW_GAP;
	}
	if (!forge_menu.map_count)
	{
		ui_style_text(UI_TEXT_LINE, CONTENT_LEFT + LIST_WIDTH * 0.5f, BODY_Y + 40, UI_ALIGN_CENTER,
			"No multiplayer maps installed");
	}
	/* the pane: the map's rows */
	render_pane_glass();
	if (map_index != NONE)
	{
		ui_style_text(UI_TEXT_TITLE, x, BODY_Y + 12, UI_ALIGN_LEFT, forge_map_name(map_index));
		y = BODY_Y + 36;
		shown = (short)((BODY_BOTTOM - 22 - y + OPTION_GAP) / (OPTION_HEIGHT + OPTION_GAP));
		first = forge_menu.pane_first = scrolled(forge_menu.pane_first, forge_menu.pane_row, rows, shown);
		for (row = first; row < rows && row < first + shown; row++)
		{
			boolean on = forge_menu.pane && row == forge_menu.pane_row;

			if (row == 0 && forge_menu.tab == _lobby_tab_play)
			{
				ui_style_setting(x, y, width, OPTION_HEIGHT, 0.9f, "GAME TYPE", NULL, game_types[forge_menu.game_type].name,
					on);
				target_add(x, y, x + width, y + OPTION_HEIGHT, _target_pane, row);
			}
			else if (row == 0)
			{
				pane_option(x, y, width, on, "NEW LAYOUT", NULL, row);
			}
			else
			{
				struct forge_store_entry const *entry = map_entry(map_index, (short)(row - 1));
				char tag[32];

				snprintf(tag, sizeof(tag), "%d", entry ? entry->objects : 0);
				if (armed(row, GUIDE_INPUT_Y))
					pane_option(x, y, width, on, "CONFIRM DELETE", NULL, row);
				else
					pane_option(x, y, width, on, entry ? entry->title : "", entry && entry->damaged ? "DAMAGED" : tag, row);
			}
			y += OPTION_HEIGHT + OPTION_GAP;
		}
		/* (the layout chosen: its date and author) */
		if (forge_menu.pane && forge_menu.pane_row > 0)
		{
			struct forge_store_entry const *entry = map_entry(map_index, (short)(forge_menu.pane_row - 1));

			if (entry)
			{
				entry_status(entry, status, sizeof(status));
				ui_style_text(UI_TEXT_LABEL, x, BODY_BOTTOM - 18, UI_ALIGN_LEFT, status);
			}
		}
		else if (rows == 1 && forge_menu.tab == _lobby_tab_play)
		{
			ui_style_text(UI_TEXT_LABEL, x, y + 4, UI_ALIGN_LEFT, "No layouts yet");
		}
	}
	render_message(CONTENT_LEFT, BODY_BOTTOM - 12);
	if (forge_menu.pane)
	{
		if (forge_menu.tab == _lobby_tab_play && forge_menu.pane_row == 0)
			prompt_add(prompts, &prompt_count, UI_BUTTON_A, "ENTER", "CHANGE");
		else
			prompt_add(prompts, &prompt_count, UI_BUTTON_A, "ENTER", forge_menu.tab == _lobby_tab_play ? "PLAY" : "FORGE");
		if (forge_menu.pane_row > 0)
			prompt_add(prompts, &prompt_count, UI_BUTTON_Y, "TAB", armed(forge_menu.pane_row, GUIDE_INPUT_Y) ?
				"CONFIRM" : "DELETE");
		prompt_add(prompts, &prompt_count, UI_BUTTON_B, "ESC", "BACK");
	}
	else
	{
		prompt_add(prompts, &prompt_count, UI_BUTTON_A, "ENTER", "SELECT");
		prompt_add(prompts, &prompt_count, UI_BUTTON_B, "ESC", "BACK");
	}
	render_prompts(prompts, prompt_count);
}

/* ---- the game's: the narrow panel at the right of the view

The Forge menu in a game is a column at the right, the map in view at its
left so the monitor sees where it builds: its title and the budget at the
top, the tabs, one list, the chosen row's box at the bottom and the prompts
at its foot, in the Guide's fonts, colors, rows and prompts. */

enum
{
	SIDE_X = 380, SIDE_Y = 24, SIDE_WIDTH = 236, SIDE_HEIGHT = 432,
	SIDE_LEFT = SIDE_X + 12,
	SIDE_RIGHT = SIDE_X + SIDE_WIDTH - 12,
	SIDE_TITLE_Y = SIDE_Y + 10,
	SIDE_RULE_Y = SIDE_Y + 34,
	SIDE_TABS_Y = SIDE_Y + 42,
	SIDE_TABS_HEIGHT = 18,
	SIDE_HEADING_Y = SIDE_Y + 68,
	SIDE_LIST_Y = SIDE_Y + 82,
	SIDE_ROW_HEIGHT = 22,
	SIDE_ROW_GAP = 3,
	SIDE_PROMPTS_Y = SIDE_Y + SIDE_HEIGHT - 24,
	SIDE_BOX_BOTTOM = SIDE_PROMPTS_Y - 6,
	SIDE_BOX_Y = SIDE_BOX_BOTTOM - 76,
	SIDE_LIST_BOTTOM = SIDE_BOX_Y - 6,
};

/* how many rows the list has room for */
static short side_rows_shown(void)
{
	return (short)((SIDE_LIST_BOTTOM - SIDE_LIST_Y + SIDE_ROW_GAP) / (SIDE_ROW_HEIGHT + SIDE_ROW_GAP));
}

/* a row of the list: its name, a count or tag at its right */
static void side_row(float y, boolean on, char const *name, char const *tag, boolean warning, short kind, short index)
{
	float size = ui_style_text_size(UI_TEXT_NAME);

	ui_style_row(SIDE_LEFT, y, SIDE_RIGHT - SIDE_LEFT, SIDE_ROW_HEIGHT, on);
	ui_style_text(warning ? UI_TEXT_WARNING : UI_TEXT_NAME, SIDE_LEFT + 10, y + (SIDE_ROW_HEIGHT - size) * 0.5f,
		UI_ALIGN_LEFT, name);
	if (tag && tag[0])
	{
		ui_overlay_text(UI_FONT_MENU, 9.5f, SIDE_RIGHT - 10, y + (SIDE_ROW_HEIGHT - 9.5f) * 0.5f, UI_ALIGN_RIGHT,
			ui_style_color(UI_COLOR_NAME), tag);
	}
	target_add(SIDE_LEFT, y, SIDE_RIGHT, y + SIDE_ROW_HEIGHT, kind, index);
}

/* a setting's row of the list: its label, its value between the arrows
(NULL: an action) */
static void side_setting(float y, boolean on, char const *label, char const *value, short index)
{
	ui_style_setting(SIDE_LEFT, y, SIDE_RIGHT - SIDE_LEFT, SIDE_ROW_HEIGHT, 0.9f, label, NULL, value, on);
	target_add(SIDE_LEFT, y, SIDE_RIGHT, y + SIDE_ROW_HEIGHT, _target_list, index);
}

/* the box at the bottom: a title and up to three lines */
static void side_box(char const *title, char const *first, char const *second, char const *third)
{
	ui_style_glass(SIDE_LEFT, SIDE_BOX_Y, SIDE_RIGHT - SIDE_LEFT, SIDE_BOX_BOTTOM - SIDE_BOX_Y, 7,
		ui_style_color(UI_COLOR_PANE), ui_style_color(UI_COLOR_ROW_EDGE));
	if (title && title[0])
		ui_style_text(UI_TEXT_TITLE, SIDE_LEFT + 10, SIDE_BOX_Y + 8, UI_ALIGN_LEFT, title);
	if (first && first[0])
		ui_style_text(UI_TEXT_LINE, SIDE_LEFT + 10, SIDE_BOX_Y + 28, UI_ALIGN_LEFT, first);
	if (second && second[0])
		ui_style_text(UI_TEXT_LABEL, SIDE_LEFT + 10, SIDE_BOX_Y + 43, UI_ALIGN_LEFT, second);
	if (third && third[0])
		ui_style_text(UI_TEXT_LABEL, SIDE_LEFT + 10, SIDE_BOX_Y + 57, UI_ALIGN_LEFT, third);
}

static void side_heading(char const *text)
{
	ui_style_text(UI_TEXT_HEADING, SIDE_LEFT, SIDE_HEADING_Y, UI_ALIGN_LEFT, text);
}

/* the session's last line (a save, a delete, an undo), a while */
static char const *side_status(void)
{
	return forge_menu.view.status[0] && system_milliseconds() - forge_menu.view.status_time < MESSAGE_TIME ?
		forge_menu.view.status : NULL;
}

static void upper(char *text)
{
	for (; *text; text++)
	{
		if (*text >= 'a' && *text <= 'z')
			*text = (char)(*text - 'a' + 'A');
	}
}

/* (AI on a multiplayer map) where its characters came from, in a line:
"Missing: Flood, Sentinels", or "From your campaign maps" */
static void ai_sources_text(short category, char *text, int size)
{
	short faction;
	boolean any_missing = FALSE;

	snprintf(text, (size_t)size, "%s", "Missing: ");
	for (faction = 0; faction < forge_ai_faction_lines(category); faction++)
	{
		char line[64];
		boolean missing;

		if (!forge_ai_faction_line(category, faction, line, sizeof(line), &missing) || !missing)
			continue;
		snprintf(text + strlen(text), (size_t)size - strlen(text), "%s%s", any_missing ? ", " : "", line);
		any_missing = TRUE;
	}
	if (!any_missing)
		snprintf(text, (size_t)size, "%s", "From your campaign maps");
}

static void render_objects(void)
{
	short chosen = forge_menu.list_row[_game_tab_objects], index, shown = side_rows_shown(), first;
	float y = SIDE_LIST_Y;
	char tag[16], line[64], name[FORGE_NAME_SIZE];

	if (!forge_menu.pane)
	{
		side_heading("OBJECTS");
		first = forge_menu.list_first = scrolled(forge_menu.list_first, chosen, NUMBER_OF_FORGE_CATEGORIES, shown);
		for (index = first; index < NUMBER_OF_FORGE_CATEGORIES && index < first + shown; index++)
		{
			snprintf(tag, sizeof(tag), "%d", forge_category_item_count(index));
			side_row(y, index == chosen, forge_category_name(index), tag, FALSE, _target_list, index);
			y += SIDE_ROW_HEIGHT + SIDE_ROW_GAP;
		}
		snprintf(line, sizeof(line), "%d to place", forge_category_item_count(chosen));
		snprintf(tag, sizeof(tag), "Placed: %d", forge_group_placed_count(chosen));
		/* (AI on a multiplayer map: brought from the campaign maps, and the
		factions none of whose campaign maps is there) */
		if (forge_ai_faction_lines(chosen) > 0)
		{
			char sources[96];

			ai_sources_text(chosen, sources, sizeof(sources));
			side_box(forge_category_name(chosen), line, tag, sources);
			return;
		}
		side_box(forge_category_name(chosen), line, tag, NULL);
		return;
	}
	/* (a category's objects, each with how many the layout has) */
	{
		short items = forge_category_item_count(chosen);
		struct forge_item item;

		short notes = 0;

		/* (AI on a multiplayer map: the factions with no campaign map, a row
		each after the characters, shown with the last of them) */
		{
			short faction;

			for (faction = 0; faction < forge_ai_faction_lines(chosen); faction++)
			{
				char text[64];
				boolean missing;

				if (forge_ai_faction_line(chosen, faction, text, sizeof(text), &missing) && missing)
					notes++;
			}
		}
		side_heading(forge_category_name(chosen));
		first = forge_menu.pane_first = scrolled(forge_menu.pane_first, forge_menu.pane_row, items, shown);
		if (notes && forge_menu.pane_row >= items - 1)
			first = forge_menu.pane_first = (short)MAX(0, items + notes - shown);
		for (index = first; index < items && index < first + shown; index++)
		{
			if (!forge_category_item(chosen, index, &item))
				break;
			tag[0] = 0;
			if (item.placed)
				snprintf(tag, sizeof(tag), "%d", item.placed);
			side_row(y, index == forge_menu.pane_row, item.name, tag, FALSE, _target_pane, index);
			y += SIDE_ROW_HEIGHT + SIDE_ROW_GAP;
		}
		/* (AI on a multiplayer map: a row for each faction none of whose
		campaign maps is there, after the characters) */
		if (forge_ai_faction_lines(chosen) > 0)
		{
			short faction;

			for (faction = 0; faction < forge_ai_faction_lines(chosen) && index >= items && index < first + shown; faction++)
			{
				char text[64];
				boolean missing;

				if (!forge_ai_faction_line(chosen, faction, text, sizeof(text), &missing) || !missing)
					continue;
				snprintf(line, sizeof(line), "%s", text);
				upper(line);
				side_row(y, FALSE, line, "NO MAP", TRUE, _target_pane, NONE);
				y += SIDE_ROW_HEIGHT + SIDE_ROW_GAP;
				index++;
			}
		}
		else if (!items)
			ui_style_text(UI_TEXT_LABEL, SIDE_LEFT + 10, y + 4, UI_ALIGN_LEFT, "None on this map");
		if (forge_category_item(chosen, forge_menu.pane_row, &item))
		{
			char from[96];

			snprintf(name, sizeof(name), "%s", item.name);
			upper(name);
			snprintf(line, sizeof(line), "%s", forge_category_name(chosen));
			snprintf(tag, sizeof(tag), "Placed: %d", item.placed);
			from[0] = 0;
			if (item.from[0])
				snprintf(from, sizeof(from), "From %s", item.from);
			side_box(name, tag, item.faction, from);
		}
	}
}

static void render_properties(void)
{
	struct forge_properties properties;
	short count = forge_property_count(), row, shown = side_rows_shown();
	short chosen = forge_menu.list_row[_game_tab_properties];
	short first;
	float y = SIDE_LIST_Y;
	char name[FORGE_NAME_SIZE], line[64];

	if (!count || !forge_properties_get(&properties))
	{
		side_heading("PROPERTIES");
		ui_style_text(UI_TEXT_LINE, SIDE_LEFT + 10, SIDE_LIST_Y + 4, UI_ALIGN_LEFT, "Nothing held");
		side_box("NOTHING HELD", NULL, NULL, NULL);
		return;
	}
	snprintf(name, sizeof(name), "%s", properties.name);
	upper(name);
	side_heading(name);
	first = forge_menu.list_first = scrolled(forge_menu.list_first, chosen, count, shown);
	for (row = first; row < count && row < first + shown; row++)
	{
		char label[64], value[64];

		forge_property_text(row, label, sizeof(label), value, sizeof(value));
		side_setting(y, row == chosen, label, value[0] ? value : NULL, row);
		y += SIDE_ROW_HEIGHT + SIDE_ROW_GAP;
	}
	line[0] = 0;
	if (properties.has_position)
		snprintf(line, sizeof(line), "At %.1f, %.1f, %.1f m", properties.position[0], properties.position[1],
			properties.position[2]);
	side_box(name, line, side_status(), NULL);
}

static void render_layouts(void)
{
	short map_index = forge_menu.view.map_index;
	short count = map_index != NONE ? map_entry_count(map_index) : 0;
	short rows = (short)(NUMBER_OF_LAYOUTS_ACTIONS + count), chosen = forge_menu.list_row[_game_tab_layouts];
	short shown = side_rows_shown(), first, row;
	float y = SIDE_LIST_Y;
	char title[FORGE_FILE_TITLE_SIZE], line[96], date[96];

	snprintf(line, sizeof(line), "LAYOUTS (%d)", count);
	side_heading(line);
	first = forge_menu.list_first = scrolled(forge_menu.list_first, chosen, rows, shown);
	for (row = first; row < rows && row < first + shown; row++)
	{
		if (row == _layouts_save)
			side_row(y, row == chosen, "SAVE", NULL, FALSE, _target_list, row);
		else if (row == _layouts_save_new)
			side_row(y, row == chosen, "SAVE AS NEW", NULL, FALSE, _target_list, row);
		else
		{
			struct forge_store_entry const *entry = map_entry(map_index, (short)(row - NUMBER_OF_LAYOUTS_ACTIONS));
			boolean confirm = armed(row, GUIDE_INPUT_Y) || armed(row, GUIDE_INPUT_A);

			if (!entry)
				break;
			side_row(y, row == chosen, armed(row, GUIDE_INPUT_Y) ? "CONFIRM DELETE" : armed(row, GUIDE_INPUT_A) ?
				"CONFIRM LOAD" : entry->title, forge_menu.view.file[0] && !strcmp(entry->file_name, forge_menu.view.file) ?
				"CURRENT" : NULL, confirm, _target_list, row);
		}
		y += SIDE_ROW_HEIGHT + SIDE_ROW_GAP;
	}
	if (chosen < NUMBER_OF_LAYOUTS_ACTIONS)
	{
		if (chosen == _layouts_save)
			snprintf(title, sizeof(title), "%s", forge_menu.view.save_title);
		else
			new_title(title, sizeof(title));
		snprintf(line, sizeof(line), "Placed: %d of %d", forge_menu.view.objects, forge_objects_budget());
		side_box(title, line, side_status(), NULL);
	}
	else
	{
		struct forge_store_entry const *entry = map_entry(map_index, (short)(chosen - NUMBER_OF_LAYOUTS_ACTIONS));

		if (entry)
		{
			entry_status(entry, line, sizeof(line));
			date[0] = 0;
			if (entry->author[0])
				snprintf(date, sizeof(date), "By %s", entry->author);
			side_box(entry->title, line, side_status() ? side_status() : date, NULL);
		}
	}
}

static void option_row_text(short row, char *label, int label_size, char *value, int value_size)
{
	struct forge_menu_view const *view = &forge_menu.view;

	snprintf(label, (size_t)label_size, "%s", option_labels[row]);
	value[0] = 0;
	switch (row)
	{
	case _option_snap:
		snprintf(value, (size_t)value_size, "%s", view->snap ? "ON" : "OFF");
		break;
	case _option_speed:
		snprintf(value, (size_t)value_size, "%s", forge_speed_name(view->speed));
		break;
	case _option_rotate_snap:
		snprintf(value, (size_t)value_size, "%s", view->rotate_snap);
		break;
	case _option_delete:
		snprintf(value, (size_t)value_size, "%s", delete_mode_names[delete_mode()]);
		break;
	case _option_grab_range:
		snprintf(value, (size_t)value_size, "%s", grab_range_names[grab_range()]);
		break;
	case _option_held_facing:
		snprintf(value, (size_t)value_size, "%s", held_facing_names[held_facing()]);
		break;
	case _option_undo:
		snprintf(label, (size_t)label_size, "UNDO (%d)", view->undo);
		break;
	case _option_redo:
		snprintf(label, (size_t)label_size, "REDO (%d)", view->redo);
		break;
	case _option_clear_dead_ai:
		snprintf(label, (size_t)label_size, "CLEAR DEAD AI (%d)", view->ai_dead);
		break;
	case _option_clear_all_ai:
		if (armed(row, GUIDE_INPUT_A))
			snprintf(label, (size_t)label_size, "%s", "CONFIRM CLEAR ALL AI");
		else
			snprintf(label, (size_t)label_size, "CLEAR ALL AI (%d)", view->ai_alive + view->ai_dead + view->ai_waiting);
		break;
	}
}

/* CONTROLS: each control and what does it on the device used last */
static void render_controls(void)
{
	boolean keyboard = ui_overlay_keyboard() != 0;
	float y = SIDE_LIST_Y;
	short index;

	side_heading(keyboard ? "CONTROLS - KEYBOARD" : "CONTROLS - CONTROLLER");
	for (index = 0; index < (short)NUMBEROF(controls); index++)
	{
		char key[64], second[64];

		key[0] = 0;
		if (!keyboard)
			snprintf(key, sizeof(key), "%s", controls[index].button);
		else if (controls[index].key == NONE)
			snprintf(key, sizeof(key), "%s", controls[index].keys);
		else if (controls[index].key == HALO_FORGE_KEY_UP || controls[index].key == HALO_FORGE_KEY_FARTHER)
		{
			halo_forge_key_shown(controls[index].key, key, sizeof(key));
			halo_forge_key_shown(controls[index].key == HALO_FORGE_KEY_UP ? HALO_FORGE_KEY_DOWN : HALO_FORGE_KEY_NEARER,
				second, sizeof(second));
			snprintf(key + strlen(key), sizeof(key) - strlen(key), " / %s", second);
		}
		else if (!halo_forge_key_shown(controls[index].key, key, sizeof(key)))
			snprintf(key, sizeof(key), "%s", "-");
		ui_overlay_text(UI_FONT_MENU, 8.5f, SIDE_LEFT + 4, y, UI_ALIGN_LEFT, ui_style_color(UI_COLOR_LABEL_SOFT),
			controls[index].what);
		ui_overlay_text(UI_FONT_MENU, 8.5f, SIDE_RIGHT - 4, y, UI_ALIGN_RIGHT, 0xFFFFFFFF, key);
		y += 17.5f;
	}
}

static void render_options(void)
{
	short chosen = forge_menu.list_row[_game_tab_options], row, shown = side_rows_shown(), first;
	float y = SIDE_LIST_Y;

	if (forge_menu.controls_shown)
	{
		render_controls();
		return;
	}
	side_heading("OPTIONS");
	first = forge_menu.list_first = scrolled(forge_menu.list_first, chosen, NUMBER_OF_OPTIONS, shown);
	for (row = first; row < NUMBER_OF_OPTIONS && row < first + shown; row++)
	{
		char label[64], value[64];

		option_row_text(row, label, sizeof(label), value, sizeof(value));
		side_setting(y, row == chosen, label, value[0] ? value : NULL, row);
		y += SIDE_ROW_HEIGHT + SIDE_ROW_GAP;
	}
	{
		char label[64], value[64];

		option_row_text(chosen, label, sizeof(label), value, sizeof(value));
		side_box(label, value, side_status(), NULL);
	}
}

static void render_sky(void)
{
	short chosen = forge_menu.list_row[_game_tab_sky], row, shown = side_rows_shown(), first;
	float y = SIDE_LIST_Y;
	char value[64];

	side_heading("SKY AND FOG");
	first = forge_menu.list_first = scrolled(forge_menu.list_first, chosen, NUMBER_OF_FORGE_ENVIRONMENT_ROWS, shown);
	for (row = first; row < NUMBER_OF_FORGE_ENVIRONMENT_ROWS && row < first + shown; row++)
	{
		value[0] = 0;
		forge_environment_text(row, value, sizeof(value));
		side_setting(y, row == chosen, sky_labels[row], value[0] ? value : NULL, row);
		y += SIDE_ROW_HEIGHT + SIDE_ROW_GAP;
	}
	value[0] = 0;
	forge_environment_text(chosen, value, sizeof(value));
	side_box(sky_labels[chosen], value, side_status(), NULL);
}

static void render_game(void)
{
	static char const *const titles[NUMBER_OF_GAME_TABS] = { "OBJECT BROWSER", "PROPERTIES", "LAYOUTS", "SKY AND FOG", "OPTIONS" };
	struct ui_style_prompt prompts[5];
	float starts[5], ends[5], tab_starts[NUMBER_OF_GAME_TABS], tab_ends[NUMBER_OF_GAME_TABS];
	short prompt_count = 0, index;
	short row = forge_menu.list_row[forge_menu.tab];
	char budget[32];

	ui_style_panel(SIDE_X, SIDE_Y, SIDE_WIDTH, SIDE_HEIGHT, 1.0f);
	/* (the title, as the Guide's card's, and the budget) */
	snprintf(budget, sizeof(budget), "%d / %d", forge_menu.view.objects, forge_objects_budget());
	{
		/* (as large as fits left of the budget) */
		float room = SIDE_RIGHT - SIDE_LEFT - ui_overlay_text_width(UI_FONT_MENU, 10.0f, budget) - 10.0f, size = 12.0f;

		while (size > 7.0f && ui_overlay_text_width(UI_FONT_TITLE, size, titles[forge_menu.tab]) > room)
			size -= 0.5f;
		ui_overlay_text_glow(UI_FONT_TITLE, size, SIDE_LEFT, SIDE_TITLE_Y + (12.0f - size) * 0.5f, UI_ALIGN_LEFT,
			ui_style_color(UI_COLOR_TITLE), ui_style_color(UI_COLOR_TITLE), titles[forge_menu.tab]);
	}
	ui_overlay_text(UI_FONT_MENU, 10.0f, SIDE_RIGHT, SIDE_TITLE_Y + 4, UI_ALIGN_RIGHT, ui_style_color(UI_COLOR_STATUS),
		budget);
	ui_overlay_rect(SIDE_LEFT - 4, SIDE_RULE_Y, SIDE_RIGHT - SIDE_LEFT + 8, 1, 0, ui_style_color(UI_COLOR_RULE));
	ui_style_tabs(SIDE_LEFT, SIDE_RIGHT, SIDE_TABS_Y, SIDE_TABS_HEIGHT, game_tab_names, NUMBER_OF_GAME_TABS, forge_menu.tab,
		"Q", "E", tab_starts, tab_ends);
	for (index = 0; index < NUMBER_OF_GAME_TABS; index++)
		target_add(tab_starts[index], SIDE_TABS_Y, tab_ends[index], SIDE_TABS_Y + SIDE_TABS_HEIGHT, _target_tab, index);
	switch (forge_menu.tab)
	{
	case _game_tab_objects:
		render_objects();
		prompt_add(prompts, &prompt_count, UI_BUTTON_A, "ENTER", forge_menu.pane ? "PLACE" : "SELECT");
		prompt_add(prompts, &prompt_count, UI_BUTTON_B, "ESC", forge_menu.pane ? "BACK" : "CLOSE");
		break;
	case _game_tab_properties:
		render_properties();
		if (forge_property_count() > 0)
			prompt_add(prompts, &prompt_count, UI_BUTTON_A, "ENTER", "CHANGE");
		prompt_add(prompts, &prompt_count, UI_BUTTON_B, "ESC", "CLOSE");
		break;
	case _game_tab_layouts:
		render_layouts();
		if (row < NUMBER_OF_LAYOUTS_ACTIONS)
			prompt_add(prompts, &prompt_count, UI_BUTTON_A, "ENTER", "SAVE");
		else
		{
			prompt_add(prompts, &prompt_count, UI_BUTTON_A, "ENTER", armed(row, GUIDE_INPUT_A) ? "CONFIRM" : "LOAD");
			prompt_add(prompts, &prompt_count, UI_BUTTON_Y, "TAB", armed(row, GUIDE_INPUT_Y) ? "CONFIRM" : "DELETE");
		}
		prompt_add(prompts, &prompt_count, UI_BUTTON_B, "ESC", "CLOSE");
		break;
	case _game_tab_sky:
		render_sky();
		prompt_add(prompts, &prompt_count, UI_BUTTON_A, "ENTER", "CHANGE");
		prompt_add(prompts, &prompt_count, UI_BUTTON_B, "ESC", "CLOSE");
		break;
	case _game_tab_options:
		render_options();
		if (forge_menu.controls_shown)
			prompt_add(prompts, &prompt_count, UI_BUTTON_B, "ESC", "BACK");
		else
		{
			prompt_add(prompts, &prompt_count, UI_BUTTON_A, "ENTER", row <= _option_rotate_snap ? "CHANGE" : "SELECT");
			prompt_add(prompts, &prompt_count, UI_BUTTON_B, "ESC", "CLOSE");
		}
		break;
	}
	ui_style_prompts(SIDE_RIGHT, SIDE_PROMPTS_Y, 13.0f, prompts, prompt_count, starts, ends);
	for (index = 0; index < prompt_count; index++)
	{
		target_add(starts[index] - 4, SIDE_PROMPTS_Y - 4, ends[index] + 4, SIDE_PROMPTS_Y + 17, _target_prompt,
			(short)prompts[index].button);
	}
}

/* behind the panel: the Guide's (delta.guide_background: the shade, or the
dashboard's cellwall) */
static void render_background(void)
{
	char const *background = config_string("delta.guide_background");
	int cellwall = !strcmp(background, "cellwall") ||
		(strcmp(background, "plain") != 0 && ui_style_theme_current() == UI_THEME_GREEN);

	if (!cellwall || !ui_style_cellwall(-200, 0, 1040, 480))
	{
		ui_overlay_rect(-200, 0, 1040, 480, 0, ui_style_color(UI_COLOR_SHADE));
	}
	else
	{
		unsigned int shade = ui_style_color(UI_COLOR_SHADE) & 0xFFFFFF00;
		int ring;

		for (ring = 6; ring >= 1; ring--)
		{
			ui_overlay_rect(PANEL_X - ring * 5, PANEL_Y - ring * 5, PANEL_WIDTH + ring * 10, PANEL_HEIGHT + ring * 10,
				12 + ring * 5, shade | (unsigned int)(0x2E - ring * 5));
		}
	}
}

/* the lobby's maps: Forge's installed */
static void lobby_maps_read(void)
{
	short map_index;

	forge_menu.map_count = 0;
	for (map_index = 0; map_index < forge_map_count() && forge_menu.map_count < MAXIMUM_MAPS; map_index++)
	{
		if (cache_files_map_installed(forge_map_path(map_index)))
			forge_menu.maps[forge_menu.map_count++] = map_index;
	}
}

/* a keyboard key's cap as the prompts show it: the mouse's buttons short */
static void key_cap(char const *shown, char *cap, int size)
{
	static struct
	{
		char const *name;
		char const *cap;
	} const short_names[] =
	{
		{ "Mouse Left", "LMB" }, { "Mouse Right", "RMB" }, { "Mouse Middle", "MMB" }, { "Mouse 4", "M4" },
		{ "Mouse 5", "M5" }, { "Wheel Up", "WHEEL" }, { "Wheel Down", "WHEEL" }, { "Left Ctrl", "CTRL" },
		{ "Left Shift", "SHIFT" }, { "Delete", "DEL" }, { "Space", "SPACE" },
	};
	short index;

	for (index = 0; index < (short)NUMBEROF(short_names); index++)
	{
		if (!strcmp(shown, short_names[index].name))
		{
			snprintf(cap, (size_t)size, "%s", short_names[index].cap);
			return;
		}
	}
	snprintf(cap, (size_t)size, "%s", shown);
	for (index = 0; cap[index]; index++)
	{
		if (cap[index] >= 'a' && cap[index] <= 'z')
			cap[index] = (char)(cap[index] - 'a' + 'A');
	}
}

/* ---------- public code */

void forge_menu_open_lobby(
	void)
{
	if (!forge_enabled() || !ui_overlay_available() || guide_active())
		return;
	lobby_maps_read();
	entries_read(NONE);
	forge_menu.tab = _lobby_tab_forge;
	forge_menu.map_row = (short)PIN(forge_menu.map_row, 0, MAX(forge_menu.map_count - 1, 0));
	opened(_screen_lobby);
	platform_log("forge: the lobby opened (%d maps, %d layouts)", forge_menu.map_count, forge_menu.entry_count);
}

void forge_menu_game_open_now(
	void)
{
	if (!ui_overlay_available() || guide_active() || forge_menu.screen != _screen_none)
		return;
	forge_menu_view_get(&forge_menu.view);
	if (!forge_menu.view.session || !forge_menu.view.monitor)
		return;
	opened(_screen_game);
	game_tab_go(forge_menu.view.holding ? _game_tab_properties :
		forge_menu.tab == _game_tab_properties || forge_menu.tab >= NUMBER_OF_GAME_TABS ? _game_tab_objects : forge_menu.tab);
	entries_read(forge_menu.view.map_index);
	forge_menu.entries_status_time = forge_menu.view.status_time;
	platform_log("forge: the Forge menu opened on %s", game_tab_names[forge_menu.tab]);
}

boolean forge_menu_game_open(
	void)
{
	return forge_menu.screen == _screen_game;
}

void forge_menu_game_close(
	void)
{
	if (forge_menu.screen == _screen_game)
		close_screen();
}

boolean forge_menu_active(
	void)
{
	return forge_menu.screen != _screen_none;
}

void forge_menu_update(
	void)
{
	if (forge_menu.screen == _screen_none)
		return;
	/* (the Guide opened over it: it closes, the input the Guide's) */
	if (guide_active())
	{
		platform_log("forge: the %s closed under DELTA", forge_menu.screen == _screen_lobby ? "lobby" : "Forge menu");
		forge_menu.screen = _screen_none;
		return;
	}
	if (!forge_enabled())
	{
		close_screen();
		return;
	}
	if (forge_menu.screen == _screen_lobby && !main_menu_is_active())
	{
		close_screen();
		return;
	}
	if (forge_menu.screen == _screen_game)
	{
		forge_menu_view_get(&forge_menu.view);
		if (!forge_menu.view.session || !forge_menu.view.monitor)
		{
			close_screen();
			return;
		}
		/* (a save or a delete done: the layouts read again) */
		if (forge_menu.view.status_time != forge_menu.entries_status_time)
		{
			forge_menu.entries_status_time = forge_menu.view.status_time;
			entries_read(forge_menu.view.map_index);
		}
		/* (LAYOUTS' row past a layout deleted) */
		forge_menu.list_row[_game_tab_layouts] = (short)PIN(forge_menu.list_row[_game_tab_layouts], 0,
			NUMBER_OF_LAYOUTS_ACTIONS - 1 + (forge_menu.view.map_index != NONE ? map_entry_count(forge_menu.view.map_index) : 0));
		/* (PROPERTIES of nothing held any more, its rows gone) */
		forge_menu.list_row[_game_tab_properties] = (short)PIN(forge_menu.list_row[_game_tab_properties], 0,
			MAX(forge_property_count() - 1, 0));
	}
	/* (kept: a close of the Guide's elsewhere gives it up) */
	capture(TRUE);
}

void forge_menu_process(
	void)
{
	if (forge_menu.screen == _screen_none)
		return;
	handle(presses());
}

void forge_menu_pointer(
	struct halo_ui_pointer const *pointer)
{
	short index;

	if (forge_menu.screen == _screen_none)
		return;
	if (pointer->right_clicks)
	{
		handle(GUIDE_INPUT_B);
		return;
	}
	if (pointer->wheel_steps)
	{
		short step;

		for (step = 0; step < ABS(pointer->wheel_steps); step++)
			handle(pointer->wheel_steps > 0 ? GUIDE_INPUT_UP : GUIDE_INPUT_DOWN);
		return;
	}
	if (!pointer->left_clicks)
		return;
	for (index = forge_menu.target_count - 1; index >= 0; index--)
	{
		float x = (float)pointer->click_x, y = (float)pointer->click_y;

		if (x < forge_menu.targets[index].x0 || x >= forge_menu.targets[index].x1 || y < forge_menu.targets[index].y0 ||
			y >= forge_menu.targets[index].y1)
		{
			continue;
		}
		switch (forge_menu.targets[index].kind)
		{
		case _target_tab:
			if (forge_menu.screen == _screen_game)
				game_tab_go(forge_menu.targets[index].index);
			else
			{
				forge_menu.tab = forge_menu.targets[index].index;
				forge_menu.pane_row = 0;
				disarm();
			}
			sound(SOUND_CURSOR);
			break;
		case _target_list:
		{
			short row = forge_menu.targets[index].index;
			short *chosen = forge_menu.screen == _screen_lobby ? &forge_menu.map_row : &forge_menu.list_row[forge_menu.tab];

			/* (a click on the row chosen: A; on another, chosen) */
			if (*chosen == row && !forge_menu.pane)
			{
				handle(GUIDE_INPUT_A);
			}
			else
			{
				*chosen = row;
				forge_menu.pane = FALSE;
				forge_menu.pane_row = 0;
				forge_menu.pane_first = 0;
				disarm();
				sound(SOUND_CURSOR);
			}
			break;
		}
		case _target_pane:
			/* (a row that says something - a faction with no campaign map -
			is no choice) */
			if (forge_menu.targets[index].index < 0)
				break;
			if (forge_menu.pane && forge_menu.pane_row == forge_menu.targets[index].index)
			{
				handle(GUIDE_INPUT_A);
			}
			else
			{
				forge_menu.pane = TRUE;
				forge_menu.pane_row = forge_menu.targets[index].index;
				disarm();
				sound(SOUND_CURSOR);
			}
			break;
		case _target_prompt:
		{
			static struct
			{
				int button;
				unsigned int input;
			} const buttons[] =
			{
				{ UI_BUTTON_A, GUIDE_INPUT_A }, { UI_BUTTON_B, GUIDE_INPUT_B }, { UI_BUTTON_X, GUIDE_INPUT_X },
				{ UI_BUTTON_Y, GUIDE_INPUT_Y },
			};
			short button;

			for (button = 0; button < (short)NUMBEROF(buttons); button++)
			{
				if (buttons[button].button == forge_menu.targets[index].index)
					handle(buttons[button].input);
			}
			break;
		}
		}
		return;
	}
}

void forge_menu_render(
	void)
{
	if (forge_menu.screen == _screen_none || !ui_overlay_available() || guide_active())
		return;
	ui_style_theme(ui_style_theme_named(config_string("display.theme")));
	forge_menu.target_count = 0;
	ui_overlay_layer(FORGE_MENU_LAYER);
	ui_style_clock((unsigned int)system_milliseconds());
	/* (the lobby: the Guide's panel over the menus; the game's: a column
	at the right, the map in view) */
	if (forge_menu.screen == _screen_lobby)
	{
		render_background();
		ui_style_panel(PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT, 1.0f);
		render_lobby();
	}
	else
	{
		render_game();
	}
	ui_overlay_layer(0);
}

void forge_menu_draw_prompts(
	struct forge_hud_prompt const *prompts,
	short count)
{
	float size = 14.0f, gap = 12.0f, total = 0.0f, x, y = 444.0f;
	boolean keyboard;
	short index;
	char caps[8][24];
	char const *words[8];

	if (!ui_overlay_available() || count <= 0 || forge_menu.screen != _screen_none)
		return;
	count = (short)MIN(count, (short)NUMBEROF(caps));
	keyboard = ui_overlay_keyboard() != 0;
	ui_style_theme(ui_style_theme_named(config_string("display.theme")));
	for (index = 0; index < count; index++)
	{
		char shown[64];

		caps[index][0] = 0;
		words[index] = prompts[index].word;
		if (keyboard && prompts[index].key != NONE && halo_forge_key_shown(prompts[index].key, shown, sizeof(shown)))
		{
			key_cap(shown, caps[index], sizeof(caps[index]));
			/* (a key of its own for the controller's combination, "+ RS:
			MONITOR" or the stick's "RS: NUDGE": its word alone) */
			if (!strncmp(words[index], "+ RS: ", 6))
				words[index] += 6;
			else if (!strncmp(words[index], "RS: ", 4))
				words[index] += 4;
		}
	}
	for (index = 0; index < count; index++)
	{
		if (caps[index][0])
			total += ui_overlay_key_width(caps[index], size) + 4.0f;
		else if (prompts[index].button != NONE)
			total += ui_overlay_hud_button_width(prompts[index].button, size) + 4.0f;
		total += ui_overlay_text_width(UI_FONT_MENU, size * 0.72f, words[index]);
		if (index)
			total += gap;
	}
	ui_overlay_layer(0);
	x = 320.0f - total * 0.5f;
	/* (on the glass of a toast, as the Guide's lines over a game are) */
	ui_style_glass(x - 10.0f, y - 5.0f, total + 20.0f, size + 10.0f, 7.0f, ui_style_color(UI_COLOR_PANE),
		ui_style_color(UI_COLOR_ROW_EDGE));
	for (index = 0; index < count; index++)
	{
		if (caps[index][0])
			x += ui_overlay_key(caps[index], size, x, y, 0xFFFFFFFF) + 4.0f;
		else if (prompts[index].button != NONE)
			x += ui_overlay_hud_button(prompts[index].button, size, x, y, 0xFFFFFFFF) + 4.0f;
		x += ui_overlay_text_glow(UI_FONT_MENU, size * 0.72f, x, y + size * 0.14f, UI_ALIGN_LEFT,
			ui_style_color(UI_COLOR_PROMPT), ui_style_color(UI_COLOR_PROMPT_GLOW), words[index]);
		x += gap;
	}
}

/* (forge.c's forge_draw) the delete held (game.forge_hold_to_delete): a bar over
the crosshair that fills as it is held */
void forge_menu_draw_hold(
	float fraction)
{
	float width = 56.0f, height = 5.0f, x = 320.0f - width * 0.5f, y = 222.0f;

	if (!ui_overlay_available() || forge_menu.screen != _screen_none)
		return;
	fraction = fraction < 0.0f ? 0.0f : fraction > 1.0f ? 1.0f : fraction;
	ui_overlay_layer(0);
	ui_overlay_rect(x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f, 2.0f, 0x000000A0);
	ui_overlay_rect(x, y, width * fraction, height, 2.0f, 0xE8553DFF);
}

#else

void forge_menu_open_lobby(void) { }
void forge_menu_game_open_now(void) { }
boolean forge_menu_game_open(void) { return FALSE; }
void forge_menu_game_close(void) { }
boolean forge_menu_active(void) { return FALSE; }
void forge_menu_update(void) { }
void forge_menu_process(void) { }
void forge_menu_pointer(struct halo_ui_pointer const *pointer) { (void)pointer; }
void forge_menu_render(void) { }
void forge_menu_draw_prompts(struct forge_hud_prompt const *prompts, short count) { (void)prompts; (void)count; }
void forge_menu_draw_hold(float fraction) { (void)fraction; }

#endif
