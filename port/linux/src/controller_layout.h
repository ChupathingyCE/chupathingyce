/*
CONTROLLER_LAYOUT.H

The controller's buttons for the game's actions (controller_layout.c): the
profile's button layouts (its button_preset, player_ui.c), and the custom
layout a player makes on the Controls screen
(port/linux/game/controls_screen.c), kept in config.toml as
controls.controller_layout:

	"jump=A, melee=B, action=X, switch_weapon=Y, switch_grenade=RB, ..."

A layout is the game's own table, game_input_preferences'
game_control_to_xbox_buttons: for each of the game's twelve controls, the
Xbox button that does it (input.h's enumeration). Start and Back stay where
they are (the game asserts so). The ten others are always on ten different
buttons: a custom layout is the default one with each of its entries
swapped in, so that whatever the text says, no button is lost or doubled.

The controller's own names for the buttons (an Xbox pad's RB is a
DualSense's R1 and the Duke's black button) are by its kind, as SDL tells
it (xinput_sdl.c's platform_gamepad_describe).

No SDL or game types, so that the build machine can test it
(port/linux/tests/controller_layout_test.c).
*/

#ifndef CONTROLLER_LAYOUT_H
#define CONTROLLER_LAYOUT_H

#include <stddef.h>

/* the game's controls (input_abstraction.c's _game_control_*) */
enum
{
	CONTROLLER_ACTION_JUMP,
	CONTROLLER_ACTION_SWITCH_GRENADE,
	CONTROLLER_ACTION_ACTION,
	CONTROLLER_ACTION_SWITCH_WEAPON,
	CONTROLLER_ACTION_MELEE,
	CONTROLLER_ACTION_FLASHLIGHT,
	CONTROLLER_ACTION_GRENADE,
	CONTROLLER_ACTION_FIRE,
	CONTROLLER_ACTION_START,
	CONTROLLER_ACTION_BACK,
	CONTROLLER_ACTION_CROUCH,
	CONTROLLER_ACTION_ZOOM,
	CONTROLLER_ACTION_COUNT
};

/* the Xbox's buttons (input.h's _gamepad_analog_button_* and
_gamepad_binary_button_*): white and black are a later pad's left and right
bumpers (xinput_sdl.c) */
enum
{
	CONTROLLER_BUTTON_A = 0,
	CONTROLLER_BUTTON_B,
	CONTROLLER_BUTTON_X,
	CONTROLLER_BUTTON_Y,
	CONTROLLER_BUTTON_BLACK,
	CONTROLLER_BUTTON_WHITE,
	CONTROLLER_BUTTON_LEFT_TRIGGER,
	CONTROLLER_BUTTON_RIGHT_TRIGGER,
	CONTROLLER_BUTTON_DPAD_UP,
	CONTROLLER_BUTTON_DPAD_DOWN,
	CONTROLLER_BUTTON_DPAD_LEFT,
	CONTROLLER_BUTTON_DPAD_RIGHT,
	CONTROLLER_BUTTON_START,
	CONTROLLER_BUTTON_BACK,
	CONTROLLER_BUTTON_LEFT_STICK,
	CONTROLLER_BUTTON_RIGHT_STICK,
	CONTROLLER_BUTTON_COUNT
};

/* the profile's button layouts (player_profile.h's _button_preset_*): the
Xbox's five, Bumper Jumper, and the custom one in config.toml */
enum
{
	CONTROLLER_PRESET_DEFAULT,
	CONTROLLER_PRESET_SWAP_TRIGGERS,
	CONTROLLER_PRESET_SWAP_A_LEFT_TRIGGER,
	CONTROLLER_PRESET_SWAP_B_LEFT_TRIGGER,
	CONTROLLER_PRESET_SWAP_B_RIGHT_STICK,
	CONTROLLER_PRESET_BUMPER_JUMPER,
	CONTROLLER_PRESET_CUSTOM,
	CONTROLLER_PRESET_COUNT
};

/* the kinds of controller, for their buttons' names */
enum
{
	CONTROLLER_KIND_XBOX,
	CONTROLLER_KIND_XBOX_360,
	CONTROLLER_KIND_DUKE,
	CONTROLLER_KIND_CONTROLLER_S,
	CONTROLLER_KIND_DUALSENSE,
	CONTROLLER_KIND_DUALSHOCK,
	CONTROLLER_KIND_SWITCH_PRO,
	CONTROLLER_KIND_WII_U_PRO,
	CONTROLLER_KIND_STEAM_DECK,
	CONTROLLER_KIND_GENERIC,
	CONTROLLER_KIND_COUNT
};

/* the layout a preset gives (an unknown one: the default's, as the game
reads a preset it does not know); the custom preset's is custom_text's
(controller_layout_parse), or the default's without one */
void controller_layout_preset(int preset, const char *custom_text, unsigned char layout[CONTROLLER_ACTION_COUNT]);

/* custom_text read onto the default layout: each "action=button" entry
swapped in (an entry naming no action or button is skipped). Returns how
many entries it took */
int controller_layout_parse(const char *text, unsigned char layout[CONTROLLER_ACTION_COUNT]);
/* the layout as config.toml keeps it: every action that can move, in the
table's order */
void controller_layout_format(const unsigned char layout[CONTROLLER_ACTION_COUNT], char *text, size_t size);

/* the action now on the button, or -1 */
int controller_layout_action_on(const unsigned char layout[CONTROLLER_ACTION_COUNT], int button);
/* the action moved to the button, the action that was on it given the
action's old button (a swap); the action that moved off, or -1. Start and
Back neither move nor are taken */
int controller_layout_assign(unsigned char layout[CONTROLLER_ACTION_COUNT], int action, int button);
/* whether an action (or a button) can be moved: not Start and Back */
int controller_layout_movable_action(int action);
int controller_layout_movable_button(int button);

/* the names: an action's in config.toml ("switch_weapon") and as the
screens show it ("SWITCH WEAPON"); a button's in config.toml ("RB") */
const char *controller_layout_action_key(int action);
const char *controller_layout_action_title(int action);
const char *controller_layout_button_key(int button);
/* a preset's name as the screens show it */
const char *controller_layout_preset_title(int preset);
/* a button as a kind of controller labels it: short ("RB", "R1", "Black")
and long ("Right bumper", "R1", "Black button") */
const char *controller_layout_button_glyph(int kind, int button);
const char *controller_layout_button_name(int kind, int button);
/* a kind's name ("Xbox 360 Controller"), for a pad that names itself
nothing better */
const char *controller_layout_kind_title(int kind);
/* a kind from SDL's gamepad type (xinput_sdl.c passes its own numbers:
0 unknown or standard, 1 Xbox 360, 2 Xbox One, 3 PS3, 4 PS4, 5 PS5, 6 a
Switch's) and the name the pad gives */
int controller_layout_kind(int sdl_type, const char *name);

#endif
