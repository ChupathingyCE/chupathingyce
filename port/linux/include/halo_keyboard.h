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
	NUMBER_OF_HALO_KEYBOARD_ACTIONS
};

/* the actions the keyboard and mouse hold for the player on the controller
(a bit for each halo_keyboard_action): theirs is the first's; none while a
menu or the console is up */
unsigned long halo_keyboard_actions(short controller_index);

/* the game's button prompts show the keyboard's keys, not the
controller's buttons (the keyboard or mouse used last, unless
input.keyboard_prompts is false: platform_prompt_scheme), and the key of an action as the menus name it ("E", "Space", "Mouse
Left"): 0 for one with none (xinput_sdl.c) */
int halo_keyboard_prompts(void);
int halo_keyboard_action_shown(int action, char *shown, unsigned long size);

/* text chat's line is being typed, or its log is open (chat.c): the
keyboard's keys go to it alone, as with the console (none drives the
player or the menus) */
void platform_chat_typing(int typing);

/* escape was pressed while text chat's line or log was up (once for each
press): it closes them even if its keystroke never reaches the chat */
int platform_chat_escape(void);

#endif
