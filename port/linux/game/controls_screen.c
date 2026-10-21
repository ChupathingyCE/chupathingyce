/*
CONTROLS_SCREEN.C

The Xbox menus' Controls (configure.py --game-browser): the controller's
buttons and the keyboard's keys, for the pause menu's CONTROLS
(menu_tags.c, as display.pause_controls says) while display.menus is the
Xbox's. Those menus are the Xbox's own tags, which have no such screen, so
this one is drawn and driven by code over the pause menu (as Online Games
is: browser_screen.c), laid out as the Xbox's settings screens are: the
title, the band, a row for each setting with its label in blue capitals and
its value in white, the row chosen outlined, the help under the rows and
the prompts at the bottom. The Gearbox and Chupathingy menus have the same
two pages as their own widgets instead, in one list (tools/port_settings.py's
Controls Setup: DEVICE and SHOW choose them; menu_functions.c drives it),
with the same rows and words.

The controller's page: BUTTON LAYOUT (left and right choose one of the
profile's layouts: the Xbox's five, Bumper Jumper, Custom), then each
action's button by the controller's own names for its buttons
(controller_layout.c: an Xbox pad's RB is a DualSense's R1 and the Duke's
black button). A on an action takes the next button pressed (Start or Back
cancel), shows the swap (the action that had that button takes the old
one) in the two rows and the help, and A confirms it (B cancels). That
makes the layout Custom: config.toml keeps it (controls.controller_layout)
and the profile names it (button_preset, saved at once: player_ui.c; a
build without Custom reads it as Default). Y puts the default layout back
(Custom keeps its buttons).

The keyboard's page is Controls Setup's: SHOW chooses the group, each row
an action's two keys (left and right choose which); A takes the next key or
mouse button (Escape cancels, Delete clears it), and a key another action
has is swapped with it, never dropped. Y puts every key back.

X turns the page. Each change is in force and saved at once. The pointer
(the mouse, or a touchscreen's taps) picks a row, and a second click on it
presses A; a click on a prompt presses its button.
*/

#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_CONTROLS)

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/player_ui.h"
#include "networking/network_connection.h"
#include "saved games/player_profile.h"
#include "../src/controller_layout.h"
#include "../src/ui_overlay.h"
#include "halo_ui_pointer.h"

int config_write(char const *name, char const *value);
int config_default(char const *name, char *text, size_t size);
char const *config_string(char const *name);
void platform_log(char const *format, ...);
int platform_input_scheme(void);
int platform_gamepad_count(void);
int platform_gamepad_describe(int index, char *name, size_t size);
void platform_binding_capture_begin(void);
int platform_binding_capture_poll(int *input);
void halo_input_name(int input, char *name, size_t size);
void halo_input_shown_name(char const *binding, char *shown, size_t size);

/* ---------- constants */

enum
{
	/* (event_manager.c's event types, which it keeps to itself) */
	CONTROLS_EVENT_LEFT_STICK = 1,
	CONTROLS_EVENT_BUTTON = 3,

	/* the screen takes no A this soon after it opens */
	OPEN_SETTLE = 400,
	/* the asking for a button gives up after this long */
	CAPTURE_TIMEOUT = 10000,
	STEP_FIRST_REPEAT_DELAY = 350,
	STEP_REPEAT_INTERVAL = 110,
	STEP_RELEASE_GAP = 150,

	MAXIMUM_TARGETS = 24,
	KEY_BINDINGS = 2,
	KEY_NAME_LENGTH = 32,
	KEY_ROWS = 7,
	BUTTON_ROWS = 10,
};

enum
{
	_page_controller,
	_page_keyboard,
};

/* the asking for a button: none; waiting for every button to be let go,
then for one pressed; for it to be let go, then A or B */
enum
{
	_capture_none,
	_capture_release,
	_capture_wait,
	_capture_confirm_release,
	_capture_confirm,
};

/* the look: the Xbox's settings screens' (as the Chupathingy menus have it) */
enum
{
	COLOR_LABEL = 0x2E8BFFFF,
	COLOR_LABEL_GLOW = 0x2E8BFF50,
	COLOR_VALUE = 0xE6EEF8FF,
	COLOR_ROW = 0x06122A90,
	COLOR_ROW_LIT = 0x12346AA0,
	COLOR_ROW_EDGE = 0x6FB8FFFF,
	COLOR_HELP = 0xE6EEF8FF,
	COLOR_SHADE = 0x020814E0,
	COLOR_ARROW = 0x2E8BFFFF,

	TITLE_X = 64,
	ROWS_X = 57,
	ROWS_WIDTH = 506,
	ROWS_TOP = 76,
	ROW_HEIGHT = 25,
	ROW_SPACING = 26,
	VALUE_X = 448,
	HELP_Y = 363,
	BAR_TOP = 409,
};

/* the controller's rows' actions (as menu_functions.c's button_row_actions) */
static short const button_row_actions[BUTTON_ROWS] =
{
	CONTROLLER_ACTION_FIRE, CONTROLLER_ACTION_GRENADE, CONTROLLER_ACTION_MELEE, CONTROLLER_ACTION_JUMP,
	CONTROLLER_ACTION_ACTION, CONTROLLER_ACTION_SWITCH_WEAPON, CONTROLLER_ACTION_SWITCH_GRENADE,
	CONTROLLER_ACTION_FLASHLIGHT, CONTROLLER_ACTION_CROUCH, CONTROLLER_ACTION_ZOOM,
};

/* the keyboard's actions (Controls Setup's, menu_functions.c, in its groups) */
static struct
{
	char const *setting;
	char const *label;
	short group;
} const keys[] =
{
	{ "controls.move_forward", "MOVE FORWARD", 0 },
	{ "controls.move_backward", "MOVE BACKWARD", 0 },
	{ "controls.strafe_left", "STRAFE LEFT", 0 },
	{ "controls.strafe_right", "STRAFE RIGHT", 0 },
	{ "controls.jump", "JUMP", 0 },
	{ "controls.crouch", "CROUCH", 0 },
	{ "controls.fire", "FIRE", 1 },
	{ "controls.throw_grenade", "THROW GRENADE", 1 },
	{ "controls.melee", "MELEE", 1 },
	{ "controls.reload", "RELOAD", 1 },
	{ "controls.zoom", "ZOOM", 1 },
	{ "controls.switch_weapon", "SWITCH WEAPON", 1 },
	{ "controls.switch_grenade", "SWITCH GRENADE", 1 },
	{ "controls.action", "ACTION", 2 },
	{ "controls.flashlight", "FLASHLIGHT", 2 },
	{ "controls.scoreboard", "SHOW SCORES", 2 },
	{ "controls.pause", "PAUSE MENU", 2 },
	{ "controls.chat_all", "CHAT", 2 },
	{ "controls.chat_team", "TEAM CHAT", 2 },
	{ "controls.chat_log", "CHAT LOG", 2 },
};

#define KEY_COUNT ((short)NUMBEROF(keys))

static char const *const key_groups[] = { "MOVEMENT", "WEAPONS", "ACTIONS" };

/* ---------- globals */

static struct
{
	boolean active;
	unsigned long opened_time;
	short local_player;
	short page;
	/* the row chosen on each page (0 BUTTON LAYOUT's, or SHOW's) */
	short row[2];

	/* the controller's */
	short button_preset;
	short joystick_preset;
	unsigned char layout[CONTROLLER_ACTION_COUNT];
	int kind;
	char pad_name[64];
	short capture;
	short capture_action;
	short capture_button;
	unsigned long capture_time;

	/* the keyboard's */
	short group;
	short key_slot;
	boolean key_capturing;
	char bindings[NUMBEROF(keys)][KEY_BINDINGS][KEY_NAME_LENGTH];

	/* where the pointer presses: a row (picked, then A), or a button */
	short target_count;
	struct
	{
		float x0, y0, x1, y1;
		short row;
		short button;
	} targets[MAXIMUM_TARGETS];

	boolean debug_opened;
} controls_screen;

/* ---------- private code */

static void read_controller(
	void)
{
	player_ui_get_local_player_layouts(controls_screen.local_player, &controls_screen.button_preset,
		&controls_screen.joystick_preset);
	controller_layout_preset(controls_screen.button_preset, config_string("controls.controller_layout"),
		controls_screen.layout);
	controls_screen.kind = platform_gamepad_describe(-1, controls_screen.pad_name, sizeof(controls_screen.pad_name));
}

/* a key setting's two bindings ("Left Ctrl, C") */
static void read_key(
	short key,
	char const *text)
{
	short slot;

	for (slot = 0; slot < KEY_BINDINGS; slot++)
	{
		char *binding = controls_screen.bindings[key][slot];
		size_t length;

		while (*text == ' ' || *text == ',')
			text++;
		length = strcspn(text, ",");
		if (length >= KEY_NAME_LENGTH)
			length = KEY_NAME_LENGTH - 1;
		memcpy(binding, text, length);
		binding[length] = 0;
		while (length && binding[length - 1] == ' ')
			binding[--length] = 0;
		text += strcspn(text, ",");
	}
}

static void write_key(
	short key)
{
	char const *first = controls_screen.bindings[key][0];
	char const *second = controls_screen.bindings[key][1];
	char text[2 * KEY_NAME_LENGTH + 4];

	snprintf(text, sizeof(text), "%s%s%s", *first ? first : second, *first && *second ? ", " : "", *first ? second : "");
	if (strcmp(text, config_string(keys[key].setting)) && config_write(keys[key].setting, text))
		platform_log("controls: %s = %s", keys[key].setting, text);
}

/* the keyboard's group's nth action, or NONE */
static short key_of_row(
	short row)
{
	short key;

	for (key = 0; key < KEY_COUNT && row >= 0; key++)
	{
		if (keys[key].group == controls_screen.group && row-- == 0)
			return key;
	}
	return NONE;
}

static short page_rows(
	void)
{
	short rows = 1;

	if (controls_screen.page == _page_controller)
		return 1 + BUTTON_ROWS;
	while (key_of_row((short)(rows - 1)) != NONE)
		rows++;
	return rows;
}

static void set_layouts(
	short button_preset)
{
	player_ui_set_local_player_layouts(controls_screen.local_player, button_preset, controls_screen.joystick_preset);
	read_controller();
}

/* the swap confirmed: the custom layout written and in force */
static void capture_apply(
	void)
{
	unsigned char layout[CONTROLLER_ACTION_COUNT];
	char text[256];

	memcpy(layout, controls_screen.layout, sizeof(layout));
	controller_layout_assign(layout, controls_screen.capture_action, controls_screen.capture_button);
	controller_layout_format(layout, text, sizeof(text));
	if (!config_write("controls.controller_layout", text))
		return;
	platform_log("controls: controls.controller_layout = %s", text);
	set_layouts(_button_preset_custom);
	player_ui_custom_layout_changed();
}

static boolean buttons_down(
	short *pressed)
{
	boolean down = FALSE;
	short gamepad, button;

	*pressed = NONE;
	for (gamepad = 0; gamepad < MAXIMUM_GAMEPADS; gamepad++)
	{
		struct gamepad_state const *state = input_has_gamepad(gamepad) ? input_get_gamepad_state(gamepad) : NULL;

		for (button = 0; state && button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
		{
			if (!state->buttons[button])
				continue;
			down = TRUE;
			if (*pressed == NONE && (controller_layout_movable_button(button) || button == CONTROLLER_BUTTON_START ||
				button == CONTROLLER_BUTTON_BACK))
			{
				*pressed = button;
			}
		}
	}
	return down;
}

/* the asking for a button, each frame: TRUE while it takes the controllers */
static boolean capture_update(
	void)
{
	short pressed;
	boolean down;

	if (controls_screen.capture == _capture_none)
		return FALSE;
	down = buttons_down(&pressed);
	switch (controls_screen.capture)
	{
	case _capture_release:
		if (!down)
			controls_screen.capture = _capture_wait;
		break;
	case _capture_wait:
		if (system_milliseconds() - controls_screen.capture_time > CAPTURE_TIMEOUT ||
			pressed == CONTROLLER_BUTTON_START || pressed == CONTROLLER_BUTTON_BACK ||
			(pressed != NONE && pressed == controls_screen.layout[controls_screen.capture_action]))
		{
			controls_screen.capture = _capture_none;
		}
		else if (pressed != NONE)
		{
			controls_screen.capture_button = pressed;
			controls_screen.capture = _capture_confirm_release;
		}
		break;
	case _capture_confirm_release:
		if (!down)
			controls_screen.capture = _capture_confirm;
		break;
	case _capture_confirm:
		if (pressed == CONTROLLER_BUTTON_A)
		{
			controls_screen.capture = _capture_none;
			capture_apply();
		}
		else if (pressed == CONTROLLER_BUTTON_B || pressed == CONTROLLER_BUTTON_START ||
			pressed == CONTROLLER_BUTTON_BACK)
		{
			controls_screen.capture = _capture_none;
		}
		break;
	}
	return TRUE;
}

/* the key or button taken: the slot's, and any other slot that had it takes
the slot's old one */
static void key_capture_update(
	void)
{
	short key = key_of_row((short)(controls_screen.row[_page_keyboard] - 1)), slot = controls_screen.key_slot;
	int input = -1, result;

	if (!controls_screen.key_capturing || !(result = platform_binding_capture_poll(&input)))
		return;
	controls_screen.key_capturing = FALSE;
	if (key == NONE)
		return;
	if (result == 1)
	{
		char name[KEY_NAME_LENGTH], old[KEY_NAME_LENGTH];
		short other, other_slot;

		halo_input_name(input, name, sizeof(name));
		if (!*name)
			return;
		snprintf(old, sizeof(old), "%s", controls_screen.bindings[key][slot]);
		for (other = 0; other < KEY_COUNT; other++)
		{
			for (other_slot = 0; other_slot < KEY_BINDINGS; other_slot++)
			{
				if ((other != key || other_slot != slot) && !_stricmp(controls_screen.bindings[other][other_slot], name))
				{
					snprintf(controls_screen.bindings[other][other_slot], KEY_NAME_LENGTH, "%s", old);
					write_key(other);
				}
			}
		}
		snprintf(controls_screen.bindings[key][slot], KEY_NAME_LENGTH, "%s", name);
	}
	else if (result == 2)
		controls_screen.bindings[key][slot][0] = 0;
	write_key(key);
}

static void step_side(
	short direction)
{
	short row = controls_screen.row[controls_screen.page];

	if (controls_screen.page == _page_controller)
	{
		if (row == 0)
			set_layouts((short)((controls_screen.button_preset + direction + CONTROLLER_PRESET_COUNT) %
				CONTROLLER_PRESET_COUNT));
	}
	else if (row == 0)
		controls_screen.group = (short)((controls_screen.group + direction + NUMBEROF(key_groups)) % NUMBEROF(key_groups));
	else
		controls_screen.key_slot = (short)(1 - controls_screen.key_slot);
}

static void row_accept(
	void)
{
	short row = controls_screen.row[controls_screen.page];

	if (row == 0)
	{
		step_side(1);
		return;
	}
	if (controls_screen.page == _page_keyboard)
	{
		controls_screen.key_capturing = TRUE;
		platform_binding_capture_begin();
		return;
	}
	if (platform_gamepad_count() <= 0)
		return;
	controls_screen.capture_action = button_row_actions[row - 1];
	controls_screen.capture_button = NONE;
	controls_screen.capture = _capture_release;
	controls_screen.capture_time = system_milliseconds();
}

static void defaults(
	void)
{
	short key;

	if (controls_screen.page == _page_controller)
	{
		set_layouts(_button_preset_standard);
		return;
	}
	for (key = 0; key < KEY_COUNT; key++)
	{
		char text[128];

		if (config_default(keys[key].setting, text, sizeof(text)))
		{
			read_key(key, text);
			write_key(key);
		}
	}
}

static void press(
	short button);

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

static void press(
	short button)
{
	short rows = page_rows();
	short *row = &controls_screen.row[controls_screen.page];

	if (controls_screen.capture != _capture_none || controls_screen.key_capturing)
		return;
	switch (button)
	{
	case _gamepad_analog_button_b:
		controls_screen.active = FALSE;
		break;
	case _gamepad_analog_button_x:
		controls_screen.page = (short)(1 - controls_screen.page);
		break;
	case _gamepad_analog_button_y:
		defaults();
		break;
	case _gamepad_analog_button_a:
		if (system_milliseconds() - controls_screen.opened_time > OPEN_SETTLE)
			row_accept();
		break;
	case _gamepad_binary_button_dpad_up:
		*row = (short)((*row + rows - 1) % rows);
		break;
	case _gamepad_binary_button_dpad_down:
		*row = (short)((*row + 1) % rows);
		break;
	case _gamepad_binary_button_dpad_left:
		step_side(-1);
		break;
	case _gamepad_binary_button_dpad_right:
		step_side(1);
		break;
	default:
		break;
	}
}

/* ---------- drawing */

static void add_target(
	float x,
	float y,
	float width,
	float height,
	short row,
	short button)
{
	if (controls_screen.target_count >= MAXIMUM_TARGETS)
		return;
	controls_screen.targets[controls_screen.target_count].x0 = x;
	controls_screen.targets[controls_screen.target_count].y0 = y;
	controls_screen.targets[controls_screen.target_count].x1 = x + width;
	controls_screen.targets[controls_screen.target_count].y1 = y + height;
	controls_screen.targets[controls_screen.target_count].row = row;
	controls_screen.targets[controls_screen.target_count].button = button;
	controls_screen.target_count++;
}

/* a row: its capsule (outlined when chosen), its label, and its values (one
centered, or two in columns), with arrows for a choice */
static void render_row(
	short row,
	char const *label,
	char const *first,
	char const *second,
	boolean choice)
{
	boolean lit = row == controls_screen.row[controls_screen.page];
	float y = ROWS_TOP + row * ROW_SPACING;

	ui_overlay_rect(ROWS_X, y, ROWS_WIDTH, ROW_HEIGHT, ROW_HEIGHT * 0.5f, lit ? COLOR_ROW_LIT : COLOR_ROW);
	if (lit)
		ui_overlay_outline(ROWS_X - 1, y - 1, ROWS_WIDTH + 2, ROW_HEIGHT + 2, ROW_HEIGHT * 0.5f + 1, 1.5f, COLOR_ROW_EDGE);
	ui_overlay_text_glow(UI_FONT_MENU, 20.0f, ROWS_X + 14, y + 2.5f, UI_ALIGN_LEFT, COLOR_LABEL, COLOR_LABEL_GLOW, label);
	if (second)
	{
		ui_overlay_text(UI_FONT_MENU, 17.0f, 360, y + 4, UI_ALIGN_CENTER, COLOR_VALUE, first);
		ui_overlay_text(UI_FONT_MENU, 17.0f, 490, y + 4, UI_ALIGN_CENTER, COLOR_VALUE, second);
	}
	else
	{
		ui_overlay_text(UI_FONT_MENU, 20.0f, VALUE_X, y + 2.5f, UI_ALIGN_CENTER, COLOR_VALUE, first);
		if (choice)
		{
			ui_overlay_text(UI_FONT_MENU, 20.0f, VALUE_X - 78, y + 2.5f, UI_ALIGN_CENTER, COLOR_ARROW, "\xE2\x80\xB9");
			ui_overlay_text(UI_FONT_MENU, 20.0f, VALUE_X + 78, y + 2.5f, UI_ALIGN_CENTER, COLOR_ARROW, "\xE2\x80\xBA");
		}
	}
	add_target(ROWS_X, y, ROWS_WIDTH, ROW_HEIGHT, row, NONE);
}

/* a prompt as the Xbox's: the button, then "=WORD"; its width */
static float prompt(
	int glyph,
	short button,
	char const *word,
	float x)
{
	float y = BAR_TOP + 10, width;
	char text[48];

	width = ui_overlay_button(glyph, 13.0f, x, y + 1, 0xFFFFFFFF) + 3;
	snprintf(text, sizeof(text), "=%s", word);
	width += ui_overlay_text_glow(UI_FONT_MENU, 20.0f, x + width, y - 3, UI_ALIGN_LEFT, COLOR_LABEL, COLOR_LABEL_GLOW,
		text);
	add_target(x - 2, y - 4, width + 4, 20, NONE, button);
	return width + 26;
}

/* the help: two lines */
static void render_help(
	char const *first,
	char const *second)
{
	ui_overlay_text(UI_FONT_MENU, 18.0f, ROWS_X + 12, HELP_Y, UI_ALIGN_LEFT, COLOR_HELP, first);
	if (second)
		ui_overlay_text(UI_FONT_MENU, 18.0f, ROWS_X + 12, HELP_Y + 20, UI_ALIGN_LEFT, COLOR_HELP, second);
}

/* a help line that starts with a button, drawn as the device used last
names it (the keyboard's key for it, or the controller's button) */
static void render_help_button(
	int button,
	char const *text)
{
	float width = ui_overlay_button(button, 16.0f, ROWS_X + 12, HELP_Y + 1, 0xFFFFFFFF);

	ui_overlay_text(UI_FONT_MENU, 18.0f, ROWS_X + 12 + width + 5, HELP_Y, UI_ALIGN_LEFT, COLOR_HELP, text);
}

static void render_controller(
	void)
{
	short focused = controls_screen.row[_page_controller];
	short other = NONE, index;
	char text[128], help[128];

	render_row(0, "BUTTON LAYOUT:", controller_layout_preset_title(controls_screen.button_preset), NULL, TRUE);
	if (controls_screen.capture >= _capture_confirm_release)
		other = (short)controller_layout_action_on(controls_screen.layout, controls_screen.capture_button);
	for (index = 0; index < BUTTON_ROWS; index++)
	{
		short action = button_row_actions[index];
		char label[48];

		if (controls_screen.capture != _capture_none && action == controls_screen.capture_action)
		{
			if (controls_screen.capture < _capture_confirm_release)
				snprintf(text, sizeof(text), "PRESS A BUTTON");
			else
				snprintf(text, sizeof(text), "> %s <",
					controller_layout_button_name(controls_screen.kind, controls_screen.capture_button));
		}
		else if (other != NONE && action == other)
			snprintf(text, sizeof(text), "> %s <", controller_layout_button_name(controls_screen.kind,
				controls_screen.layout[controls_screen.capture_action]));
		else
			snprintf(text, sizeof(text), "%s", controller_layout_button_name(controls_screen.kind,
				controls_screen.layout[action]));
		snprintf(label, sizeof(label), "%s:", controller_layout_action_title(action));
		render_row((short)(index + 1), label, text, NULL, FALSE);
	}

	/* the help line */
	if (controls_screen.capture == _capture_release || controls_screen.capture == _capture_wait)
	{
		render_help("Press a button. Start or Back cancels.", NULL);
	}
	else if (controls_screen.capture != _capture_none)
		render_help("A confirms, B cancels.", NULL);
	else if (focused == 0)
	{
		render_help(controls_screen.kind >= 0 ? controls_screen.pad_name : "No controller connected.", NULL);
	}
	else
	{
		render_help(controls_screen.kind < 0 ? "Connect a controller to change buttons." : "A changes the button.",
			NULL);
	}
}

static void render_keyboard(
	void)
{
	short focused = controls_screen.row[_page_keyboard];
	short row;

	render_row(0, "SHOW:", key_groups[controls_screen.group], NULL, TRUE);
	for (row = 1; row <= KEY_ROWS; row++)
	{
		short key = key_of_row((short)(row - 1)), slot;
		char shown[KEY_BINDINGS][48];

		if (key == NONE)
			break;
		for (slot = 0; slot < KEY_BINDINGS; slot++)
		{
			char const *binding = controls_screen.bindings[key][slot];
			char name[KEY_NAME_LENGTH];

			/* (the key as the keyboard's layout labels it: xinput_sdl.c) */
			if (*binding)
				halo_input_shown_name(binding, name, sizeof(name));
			else
				snprintf(name, sizeof(name), "-");
			if (controls_screen.key_capturing && row == focused && controls_screen.key_slot == slot)
				snprintf(shown[slot], sizeof(shown[slot]), "PRESS A KEY");
			else if (row == focused && controls_screen.key_slot == slot)
				snprintf(shown[slot], sizeof(shown[slot]), "> %s <", name);
			else
				snprintf(shown[slot], sizeof(shown[slot]), "%s", name);
		}
		render_row(row, keys[key].label, shown[0], shown[1], FALSE);
	}
	if (controls_screen.key_capturing)
		render_help("Press a key or mouse button. Escape cancels,", "Delete clears.");
	else if (focused == 0)
		render_help("Choose what to show.", NULL);
	else
		render_help_button(UI_BUTTON_A, "sets the key. Left and right pick which one.");
}

/* ---------- public code */

boolean controls_screen_active(
	void)
{
	return controls_screen.active;
}

/* page: 0 the controller's, 1 the keyboard's, -1 that of what the player
used last */
void controls_screen_open(
	short local_player_index,
	short page)
{
	short key;

	controls_screen.active = TRUE;
	controls_screen.opened_time = system_milliseconds();
	controls_screen.local_player = local_player_index >= 0 && local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS ?
		local_player_index : 0;
	controls_screen.page = page >= 0 ? page : platform_input_scheme() == 0 ? _page_keyboard : _page_controller;
	controls_screen.row[0] = controls_screen.row[1] = 0;
	controls_screen.group = 0;
	controls_screen.key_slot = 0;
	controls_screen.capture = _capture_none;
	controls_screen.key_capturing = FALSE;
	read_controller();
	for (key = 0; key < KEY_COUNT; key++)
		read_key(key, config_string(keys[key].setting));
	/* (the A that opened it is not this screen's) */
	event_manager_flush();
}

/* each frame (ui_widget.c's process_ui_widgets), before the menus: the
screen opened by debug.menu_open ("controls", "controls/keyboard",
"controls/rebind"), to look at it */
void controls_screen_update(
	boolean menu_open)
{
	char const *open = config_string("debug.menu_open");

	if (controls_screen.debug_opened || !menu_open || strncmp(open, "controls", 8))
		return;
	controls_screen.debug_opened = TRUE;
	controls_screen_open(0, !strcmp(open, "controls/keyboard") ? _page_keyboard : _page_controller);
	if (!strcmp(open, "controls/rebind"))
	{
		controls_screen.row[_page_controller] = 3;
		controls_screen.capture_action = CONTROLLER_ACTION_MELEE;
		controls_screen.capture_button = CONTROLLER_BUTTON_LEFT_TRIGGER;
		controls_screen.capture = _capture_confirm;
	}
}

void controls_screen_process(
	void)
{
	struct event_record event;

	read_controller();
	key_capture_update();
	if (capture_update())
	{
		event_manager_flush();
		return;
	}
	while (controls_screen.active && get_next_event(&event, NONE))
	{
		if (event.type == CONTROLS_EVENT_BUTTON)
		{
			short button = event.data.button.index;

			if (button >= _gamepad_binary_button_dpad_up && button <= _gamepad_binary_button_dpad_right)
				held_press(button);
			else if (event.data.button.value == 1)
				press(button);
		}
		else if (event.type == CONTROLS_EVENT_LEFT_STICK)
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

void controls_screen_pointer(
	struct halo_ui_pointer const *pointer)
{
	short index;

	if (!controls_screen.active)
		return;
	if (pointer->right_clicks)
		press(_gamepad_analog_button_b);
	if (pointer->wheel_steps)
		press(pointer->wheel_steps > 0 ? _gamepad_binary_button_dpad_up : _gamepad_binary_button_dpad_down);
	if (!pointer->left_clicks || !controls_screen.active)
		return;
	for (index = 0; index < controls_screen.target_count; index++)
	{
		short row = controls_screen.targets[index].row;

		if (pointer->click_x < controls_screen.targets[index].x0 || pointer->click_x >= controls_screen.targets[index].x1 ||
			pointer->click_y < controls_screen.targets[index].y0 || pointer->click_y >= controls_screen.targets[index].y1)
		{
			continue;
		}
		if (controls_screen.targets[index].button != NONE)
			press(controls_screen.targets[index].button);
		else if (controls_screen.capture == _capture_none && !controls_screen.key_capturing)
		{
			/* (the keyboard's: the slot clicked, by its half of the row) */
			short slot = controls_screen.page == _page_keyboard && pointer->click_x >= 425 ? 1 : 0;

			if (row == controls_screen.row[controls_screen.page] &&
				(controls_screen.page == _page_controller || row == 0 || slot == controls_screen.key_slot))
			{
				press(_gamepad_analog_button_a);
			}
			controls_screen.row[controls_screen.page] = row;
			controls_screen.key_slot = slot;
		}
		return;
	}
}

/* each frame (ui_widget.c's render_ui_widgets, the first player's) */
void controls_screen_render(
	void)
{
	float x;

	if (!controls_screen.active || !ui_overlay_available())
		return;
	controls_screen.target_count = 0;
	ui_overlay_rect(-400, -100, 1440, 680, 0, COLOR_SHADE);
	ui_overlay_text_glow(UI_FONT_TITLE, 21.0f, TITLE_X, 33, UI_ALIGN_LEFT, COLOR_LABEL, 0x2E8BFF90,
		controls_screen.page == _page_controller ? "CONTROLLER SETUP" : "CONTROLS SETUP");
	ui_overlay_band(-400, 1040, 68, BAR_TOP - 2);
	ui_overlay_prompt_bar(-400, 1040, BAR_TOP, BAR_TOP + 36);
	if (controls_screen.page == _page_controller)
		render_controller();
	else
		render_keyboard();
	x = 60;
	x += prompt(UI_BUTTON_Y, _gamepad_analog_button_y, "DEFAULTS", x);
	x += prompt(UI_BUTTON_X, _gamepad_analog_button_x, controls_screen.page == _page_controller ? "KEYBOARD" : "BUTTONS", x);
	x += prompt(UI_BUTTON_B, _gamepad_analog_button_b, "BACK", x);
	prompt(UI_BUTTON_A, _gamepad_analog_button_a, "CHANGE", x);
}

#endif
