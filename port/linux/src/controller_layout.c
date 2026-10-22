/*
CONTROLLER_LAYOUT.C

The controller's buttons for the game's actions (controller_layout.h).
*/

#include "controller_layout.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ---------- the layouts */

#define A CONTROLLER_BUTTON_A
#define B CONTROLLER_BUTTON_B
#define X CONTROLLER_BUTTON_X
#define Y CONTROLLER_BUTTON_Y
#define BLACK CONTROLLER_BUTTON_BLACK
#define WHITE CONTROLLER_BUTTON_WHITE
#define LT CONTROLLER_BUTTON_LEFT_TRIGGER
#define RT CONTROLLER_BUTTON_RIGHT_TRIGGER
#define START CONTROLLER_BUTTON_START
#define BACK CONTROLLER_BUTTON_BACK
#define LS CONTROLLER_BUTTON_LEFT_STICK
#define RS CONTROLLER_BUTTON_RIGHT_STICK

/* the presets' layouts, as player_ui.c's set_local_player_controls_from_player_profile
has them (tools/test_controller_layout.py checks the two agree) */
static const unsigned char preset_layouts[CONTROLLER_PRESET_CUSTOM][CONTROLLER_ACTION_COUNT] =
{
	/* jump, switch grenade, action, switch weapon, melee, flashlight, grenade, fire, start, back, crouch, zoom */
	{ A, BLACK, X, Y, B, WHITE, LT, RT, START, BACK, LS, RS },
	{ A, BLACK, X, Y, B, WHITE, RT, LT, START, BACK, LS, RS },
	{ LT, BLACK, X, Y, B, WHITE, A, RT, START, BACK, LS, RS },
	{ A, BLACK, X, Y, LT, WHITE, B, RT, START, BACK, LS, RS },
	{ A, BLACK, X, Y, RS, WHITE, LT, RT, START, BACK, LS, B },
	/* (Bumper Jumper, as Halo: The Master Chief Collection's) */
	{ WHITE, A, B, Y, BLACK, X, LT, RT, START, BACK, LS, RS },
};

static const char *const preset_titles[CONTROLLER_PRESET_COUNT] =
{
	"DEFAULT", "SWAP TRIGGERS", "SWAP A, L TRIGGER", "SWAP B, L TRIGGER", "SWAP B, R STICK", "BUMPER JUMPER",
	"CUSTOM",
};

static const char *const action_keys[CONTROLLER_ACTION_COUNT] =
{
	"jump", "switch_grenade", "action", "switch_weapon", "melee", "flashlight", "throw_grenade", "fire", "pause",
	"scoreboard", "crouch", "zoom",
};

static const char *const action_titles[CONTROLLER_ACTION_COUNT] =
{
	"JUMP", "SWITCH GRENADE", "ACTION / RELOAD", "SWITCH WEAPON", "MELEE", "FLASHLIGHT", "GRENADE", "FIRE", "PAUSE",
	"SCOREBOARD", "CROUCH", "ZOOM",
};

/* the buttons' names in config.toml (and the other names they are read by) */
static const char *const button_keys[CONTROLLER_BUTTON_COUNT] =
{
	"A", "B", "X", "Y", "RB", "LB", "LT", "RT", "Up", "Down", "Left", "Right", "Start", "Back", "LS", "RS",
};

static const struct
{
	const char *name;
	int button;
} button_aliases[] =
{
	{ "Black", BLACK }, { "White", WHITE }, { "R1", BLACK }, { "L1", WHITE }, { "L2", LT }, { "R2", RT },
	{ "L3", LS }, { "R3", RS }, { "Right Bumper", BLACK }, { "Left Bumper", WHITE },
	{ "Left Trigger", LT }, { "Right Trigger", RT }, { "Left Stick", LS }, { "Right Stick", RS },
};

void controller_layout_preset(int preset, const char *custom_text, unsigned char layout[CONTROLLER_ACTION_COUNT])
{
	if (preset == CONTROLLER_PRESET_CUSTOM)
	{
		controller_layout_parse(custom_text ? custom_text : "", layout);
		return;
	}
	if (preset < 0 || preset >= CONTROLLER_PRESET_CUSTOM)
		preset = CONTROLLER_PRESET_DEFAULT;
	memcpy(layout, preset_layouts[preset], CONTROLLER_ACTION_COUNT);
}

int controller_layout_movable_action(int action)
{
	return action >= 0 && action < CONTROLLER_ACTION_COUNT && action != CONTROLLER_ACTION_START &&
		action != CONTROLLER_ACTION_BACK;
}

int controller_layout_movable_button(int button)
{
	return button >= 0 && button < CONTROLLER_BUTTON_COUNT && button != START && button != BACK &&
		(button < CONTROLLER_BUTTON_DPAD_UP || button > CONTROLLER_BUTTON_DPAD_RIGHT);
}

int controller_layout_action_on(const unsigned char layout[CONTROLLER_ACTION_COUNT], int button)
{
	int action;

	for (action = 0; action < CONTROLLER_ACTION_COUNT; action++)
	{
		if (layout[action] == button)
			return action;
	}
	return -1;
}

int controller_layout_assign(unsigned char layout[CONTROLLER_ACTION_COUNT], int action, int button)
{
	int other;

	if (!controller_layout_movable_action(action) || !controller_layout_movable_button(button) ||
		layout[action] == button)
	{
		return -1;
	}
	other = controller_layout_action_on(layout, button);
	if (other >= 0)
		layout[other] = layout[action];
	layout[action] = (unsigned char)button;
	return other;
}

/* a name, ignoring case and the spaces and underscores around and in it */
static int same_name(const char *text, size_t length, const char *name)
{
	size_t index = 0;

	for (; *name; name++)
	{
		if (*name == ' ' || *name == '_')
			continue;
		while (index < length && (text[index] == ' ' || text[index] == '_'))
			index++;
		if (index >= length || tolower((unsigned char)text[index]) != tolower((unsigned char)*name))
			return 0;
		index++;
	}
	while (index < length && (text[index] == ' ' || text[index] == '_'))
		index++;
	return index == length;
}

static int button_named(const char *text, size_t length)
{
	size_t index;

	for (index = 0; index < CONTROLLER_BUTTON_COUNT; index++)
	{
		if (same_name(text, length, button_keys[index]))
			return (int)index;
	}
	for (index = 0; index < sizeof(button_aliases) / sizeof(button_aliases[0]); index++)
	{
		if (same_name(text, length, button_aliases[index].name))
			return button_aliases[index].button;
	}
	return -1;
}

int controller_layout_parse(const char *text, unsigned char layout[CONTROLLER_ACTION_COUNT])
{
	int taken = 0;

	memcpy(layout, preset_layouts[CONTROLLER_PRESET_DEFAULT], CONTROLLER_ACTION_COUNT);
	while (text && *text)
	{
		size_t length = strcspn(text, ",;");
		const char *equals = memchr(text, '=', length);

		if (equals)
		{
			int action, button = button_named(equals + 1, length - (size_t)(equals + 1 - text));

			for (action = 0; action < CONTROLLER_ACTION_COUNT; action++)
			{
				if (same_name(text, (size_t)(equals - text), action_keys[action]))
					break;
			}
			if (action < CONTROLLER_ACTION_COUNT && controller_layout_movable_action(action) &&
				controller_layout_movable_button(button))
			{
				controller_layout_assign(layout, action, button);
				taken++;
			}
		}
		text += length;
		if (*text)
			text++;
	}
	return taken;
}

void controller_layout_format(const unsigned char layout[CONTROLLER_ACTION_COUNT], char *text, size_t size)
{
	size_t used = 0;
	int action;

	if (!size)
		return;
	text[0] = 0;
	for (action = 0; action < CONTROLLER_ACTION_COUNT; action++)
	{
		int written;

		if (!controller_layout_movable_action(action) || layout[action] >= CONTROLLER_BUTTON_COUNT)
			continue;
		written = snprintf(text + used, size - used, "%s%s=%s", used ? ", " : "", action_keys[action],
			button_keys[layout[action]]);
		if (written < 0 || (size_t)written >= size - used)
			break;
		used += (size_t)written;
	}
}

const char *controller_layout_action_key(int action)
{
	return action >= 0 && action < CONTROLLER_ACTION_COUNT ? action_keys[action] : "";
}

const char *controller_layout_action_title(int action)
{
	return action >= 0 && action < CONTROLLER_ACTION_COUNT ? action_titles[action] : "";
}

const char *controller_layout_button_key(int button)
{
	return button >= 0 && button < CONTROLLER_BUTTON_COUNT ? button_keys[button] : "";
}

const char *controller_layout_preset_title(int preset)
{
	return preset >= 0 && preset < CONTROLLER_PRESET_COUNT ? preset_titles[preset] : preset_titles[0];
}

/* ---------- the controllers' names for their buttons */

/* A, B, X, Y, black (RB), white (LB), LT, RT, the d-pad, Start, Back, LS, RS:
each kind's short names, then its long ones */
static const char *const glyphs[CONTROLLER_KIND_COUNT][2][CONTROLLER_BUTTON_COUNT] =
{
	/* Xbox Series and One */
	{ { "A", "B", "X", "Y", "RB", "LB", "LT", "RT", "Up", "Down", "Left", "Right", "Menu", "View", "LS", "RS" },
	  { "A", "B", "X", "Y", "RB", "LB", "LT", "RT", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Menu",
	    "View", "LS", "RS" } },
	/* Xbox 360 */
	{ { "A", "B", "X", "Y", "RB", "LB", "LT", "RT", "Up", "Down", "Left", "Right", "Start", "Back", "LS", "RS" },
	  { "A", "B", "X", "Y", "RB", "LB", "LT", "RT", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Start",
	    "Back", "LS", "RS" } },
	/* the Duke */
	{ { "A", "B", "X", "Y", "Black", "White", "L", "R", "Up", "Down", "Left", "Right", "Start", "Back", "LS", "RS" },
	  { "A", "B", "X", "Y", "Black", "White", "L trigger", "R trigger", "D-pad up", "D-pad down", "D-pad left",
	    "D-pad right", "Start", "Back", "LS", "RS" } },
	/* Controller S */
	{ { "A", "B", "X", "Y", "Black", "White", "L", "R", "Up", "Down", "Left", "Right", "Start", "Back", "LS", "RS" },
	  { "A", "B", "X", "Y", "Black", "White", "L trigger", "R trigger", "D-pad up", "D-pad down", "D-pad left",
	    "D-pad right", "Start", "Back", "LS", "RS" } },
	/* DualSense */
	{ { "Cross", "Circle", "Square", "Triangle", "R1", "L1", "L2", "R2", "Up", "Down", "Left", "Right", "Options",
	    "Create", "L3", "R3" },
	  { "Cross", "Circle", "Square", "Triangle", "R1", "L1", "L2", "R2", "D-pad up", "D-pad down", "D-pad left",
	    "D-pad right", "Options", "Create", "L3", "R3" } },
	/* DualShock */
	{ { "Cross", "Circle", "Square", "Triangle", "R1", "L1", "L2", "R2", "Up", "Down", "Left", "Right", "Options",
	    "Share", "L3", "R3" },
	  { "Cross", "Circle", "Square", "Triangle", "R1", "L1", "L2", "R2", "D-pad up", "D-pad down", "D-pad left",
	    "D-pad right", "Options", "Share", "L3", "R3" } },
	/* a Switch's (by place: the bottom button is B) */
	{ { "B", "A", "Y", "X", "R", "L", "ZL", "ZR", "Up", "Down", "Left", "Right", "+", "-", "LS", "RS" },
	  { "B", "A", "Y", "X", "R", "L", "ZL", "ZR", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Plus",
	    "Minus", "LS", "RS" } },
	/* the Wii U Pro Controller (as a Switch's) */
	{ { "B", "A", "Y", "X", "R", "L", "ZL", "ZR", "Up", "Down", "Left", "Right", "+", "-", "LS", "RS" },
	  { "B", "A", "Y", "X", "R", "L", "ZL", "ZR", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Plus",
	    "Minus", "LS", "RS" } },
	/* the Steam Deck */
	{ { "A", "B", "X", "Y", "R1", "L1", "L2", "R2", "Up", "Down", "Left", "Right", "Menu", "View", "L3", "R3" },
	  { "A", "B", "X", "Y", "R1", "L1", "L2", "R2", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Menu",
	    "View", "L3", "R3" } },
	/* any other (by the Xbox's names, as SDL maps it) */
	{ { "A", "B", "X", "Y", "RB", "LB", "LT", "RT", "Up", "Down", "Left", "Right", "Start", "Back", "LS", "RS" },
	  { "A", "B", "X", "Y", "RB", "LB", "LT", "RT", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Start",
	    "Back", "LS", "RS" } },
};

static const char *const kind_titles[CONTROLLER_KIND_COUNT] =
{
	"Xbox Wireless Controller", "Xbox 360 Controller", "Xbox Controller (Duke)", "Xbox Controller S", "DualSense",
	"DualShock 4", "Nintendo Switch Pro Controller", "Wii U Pro Controller", "Steam Deck", "Controller",
};

const char *controller_layout_button_glyph(int kind, int button)
{
	if (kind < 0 || kind >= CONTROLLER_KIND_COUNT)
		kind = CONTROLLER_KIND_GENERIC;
	return button >= 0 && button < CONTROLLER_BUTTON_COUNT ? glyphs[kind][0][button] : "";
}

const char *controller_layout_button_name(int kind, int button)
{
	if (kind < 0 || kind >= CONTROLLER_KIND_COUNT)
		kind = CONTROLLER_KIND_GENERIC;
	return button >= 0 && button < CONTROLLER_BUTTON_COUNT ? glyphs[kind][1][button] : "";
}

const char *controller_layout_kind_title(int kind)
{
	return kind >= 0 && kind < CONTROLLER_KIND_COUNT ? kind_titles[kind] : kind_titles[CONTROLLER_KIND_GENERIC];
}

/* whether the name has the word in it, in any case */
static int name_has(const char *name, const char *word)
{
	size_t length = strlen(word);

	for (; name && *name; name++)
	{
		size_t index;

		for (index = 0; index < length && name[index] &&
			tolower((unsigned char)name[index]) == tolower((unsigned char)word[index]); index++)
			;
		if (index == length)
			return 1;
	}
	return 0;
}

int controller_layout_kind(int sdl_type, const char *name)
{
	/* (the names first: an adapter for the Xbox's own pads, a Wii U Pro
	Controller and the Deck report the type of what they act as) */
	if (name_has(name, "steam deck"))
		return CONTROLLER_KIND_STEAM_DECK;
	if (name_has(name, "wii u"))
		return CONTROLLER_KIND_WII_U_PRO;
	if (name_has(name, "duke"))
		return CONTROLLER_KIND_DUKE;
	if (name_has(name, "controller s") || name_has(name, "x-box pad") || name_has(name, "xbox pad"))
		return CONTROLLER_KIND_CONTROLLER_S;
	switch (sdl_type)
	{
	case 1: return CONTROLLER_KIND_XBOX_360;
	case 2: return CONTROLLER_KIND_XBOX;
	case 3:
	case 4: return CONTROLLER_KIND_DUALSHOCK;
	case 5: return CONTROLLER_KIND_DUALSENSE;
	case 6: return CONTROLLER_KIND_SWITCH_PRO;
	default: return CONTROLLER_KIND_GENERIC;
	}
}
