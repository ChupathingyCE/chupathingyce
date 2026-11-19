/*
HALO_KEYBOARD.H

The keyboard and mouse's own controls (port/linux/src/xinput_sdl.c): in the
game they do not drive the first controller, as they do in the menus, but
the player's actions themselves, by the bindings in config.toml's
[controls] (Settings > Controls Setup changes them). The game takes them
with the first controller's (source/input/input_abstraction.c), so the
profile's button layout is the controller's alone, and reloading has a key
of its own (source/game/player_control.c).
*/

#ifndef HALO_KEYBOARD_H
#define HALO_KEYBOARD_H

enum halo_keyboard_action
{
	HALO_KEYBOARD_MOVE_FORWARD,
	HALO_KEYBOARD_MOVE_BACKWARD,
	HALO_KEYBOARD_STRAFE_LEFT,
	HALO_KEYBOARD_STRAFE_RIGHT,
	HALO_KEYBOARD_JUMP,
	HALO_KEYBOARD_CROUCH,
	HALO_KEYBOARD_FIRE,
	HALO_KEYBOARD_THROW_GRENADE,
	HALO_KEYBOARD_MELEE,
	HALO_KEYBOARD_RELOAD,
	HALO_KEYBOARD_ZOOM,
	HALO_KEYBOARD_SWITCH_WEAPON,
	HALO_KEYBOARD_SWITCH_GRENADE,
	HALO_KEYBOARD_ACTION,
	HALO_KEYBOARD_FLASHLIGHT,
	/* (these two press the controller's Back and Start, which the menus
	read from it) */
	HALO_KEYBOARD_SCOREBOARD,
	HALO_KEYBOARD_PAUSE,
	/* text chat's (port/linux/game/chat.c reads them; the game does not):
	a line to everyone, to the team, and the recent lines */
	HALO_KEYBOARD_CHAT_ALL,
	HALO_KEYBOARD_CHAT_TEAM,
	HALO_KEYBOARD_CHAT_LOG,
	/* voice chat's (port/linux/game/voice_game.c reads them): talk while
	held, and the next channel */
	HALO_KEYBOARD_PUSH_TO_TALK,
	HALO_KEYBOARD_VOICE_CHANNEL,
	/* a vehicle's secondary fire (source/input/input_abstraction.c): in a
	vehicle's seat, when it has a key, it is the grenade control's in place
	of the grenade key's (with none, the grenade key's, as retail) */
	HALO_KEYBOARD_VEHICLE_SECONDARY_FIRE,
	/* walk (source/input/input_abstraction.c): while held, the keyboard
	moves at half speed, as a stick pushed halfway does (with no key, the
	keyboard runs, as retail) */
	HALO_KEYBOARD_WALK,
	NUMBER_OF_HALO_KEYBOARD_ACTIONS
};

/* the actions the keyboard and mouse hold for the player on the controller
(a bit for each halo_keyboard_action): theirs is the first's; none while a
menu or the console is up */
unsigned long halo_keyboard_actions(short controller_index);

/* whether an action has a key or button bound to it */
int halo_keyboard_action_bound(int action);

/* the game's button prompts show the keyboard's keys, not the
controller's buttons (the keyboard or mouse used last, unless
input.keyboard_prompts is false: platform_prompt_scheme), and the key of an action as the menus name it ("E", "Space", "Mouse
Left"): 0 for one with none (xinput_sdl.c) */
int halo_keyboard_prompts(void);
int halo_keyboard_action_shown(int action, char *shown, unsigned long size);

/* Forge's monitor's keys (port/linux/game/forge.c): config.toml's
controls.forge_*, each up to two keys, mouse buttons or wheel turns named as
the actions' are, read with them. Only Forge reads them, and only while its
player is the monitor, so they may share keys with the actions (Space flies
up and jumps). Held while the game has the keyboard, as the actions are;
the monitor flies with the movement keys (controls.move_*) and looks with
the mouse */
enum halo_forge_key
{
	HALO_FORGE_KEY_UP,
	HALO_FORGE_KEY_DOWN,
	HALO_FORGE_KEY_FAST,
	HALO_FORGE_KEY_GRAB,
	HALO_FORGE_KEY_ROTATE,
	HALO_FORGE_KEY_NEARER,
	HALO_FORGE_KEY_FARTHER,
	HALO_FORGE_KEY_COPY,
	HALO_FORGE_KEY_DELETE,
	HALO_FORGE_KEY_UPRIGHT,
	HALO_FORGE_KEY_SPEED,
	HALO_FORGE_KEY_NUDGE,
	HALO_FORGE_KEY_MENU,
	HALO_FORGE_KEY_MONITOR,
	NUMBER_OF_HALO_FORGE_KEYS
};

/* the Forge keys held (a bit for each halo_forge_key), and a key's first
binding as the prompts show it ("Mouse Left", "E"; 0 for none) */
unsigned long halo_forge_keys(void);
int halo_forge_key_shown(int key, char *shown, unsigned long size);

/* text chat's line is being typed, or its log is open (chat.c): the
keyboard's keys go to it alone, as with the console (none drives the
player or the menus) */
void platform_chat_typing(int typing);

/* escape was pressed while text chat's line or log was up (once for each
press): it closes them even if its keystroke never reaches the chat */
int platform_chat_escape(void);

#endif
