/*
BOT_DEBUG.H

The bots' debug view (bot_debug.c): the window, the panel and the cameras.
*/

#ifndef __BOT_DEBUG_H
#define __BOT_DEBUG_H

struct camera_control;
struct observer_command;

enum
{
	_bot_debug_camera_own = 0, /* this player's own view */
	_bot_debug_camera_eyes, /* through the chosen bot's eyes */
	_bot_debug_camera_behind, /* from behind it */
	_bot_debug_camera_fly, /* free: the flying camera (noclip) */
	NUMBER_OF_BOT_DEBUG_CAMERAS
};

/* what the platform's second window does (sdl_platform.c offers it) */
struct bot_debug_window_ops
{
	int (*toggle)(void); /* open or close it: whether it is open now */
	int (*is_open)(void);
	void (*set_text)(char const *text); /* what it shows */
	int (*key_held)(int key); /* a key of the view's held now (_bot_debug_key_...) */
};

enum
{
	_bot_debug_key_f7 = 0,
	_bot_debug_key_f8,
	_bot_debug_key_f10,
	_bot_debug_key_shift,
	_bot_debug_key_control,
};

void bot_debug_window_register(struct bot_debug_window_ops const *ops);

/* each frame, in a game (main.c): the keys, and the text */
void bot_debug_update(void);
/* the camera this local player has from it, and the bot's unit for the two
   that watch one (director.c) */
short bot_debug_camera_mode(short local_player_index, long *unit_index);
/* the free camera, flown without a button held (director.c) */
boolean bot_debug_flying(short local_player_index);
/* the camera through a bot's eyes or behind it (a director camera) */
void bot_debug_camera_update(void *data, struct camera_control const *controls, struct observer_command *result);
/* the panel on the screen (hud.c) */
void bot_debug_draw(short local_player_index);

#endif
