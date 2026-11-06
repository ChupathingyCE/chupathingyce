/*
BOT_DEBUG.C

The bots' debug view: everything a bot is thinking, to see what it does
and why, and to tell about it.

  F7          the bots' window: a second window beside the game's with every
              bot in a table, and all there is of the bot chosen (its goal,
              who it is after, what it looks at and why, how it moves, its
              route, its vehicle, its character, and its last log lines).
              Its mouse wheel, Page Up and Page Down scroll it; + and -
              size its text. (On a build without a second window, or when
              the game is fullscreen on the one screen: Shift+F7.)
  Shift+F7    the same of the bot chosen on the game's screen, on the left
  Ctrl+F7     all of it, of every bot, to a file in the logs folder
              (bot_debug_<date>_<time>.txt): to send with a report
  F8          the next bot (Shift+F8 the one before; Ctrl+F8 the one in
              the middle of the view)
  F10         the camera: through the chosen bot's eyes, then from behind
              it, then free (flown as a player moves: the movement keys or the
              left stick, the mouse or the right stick to look, fire to rise,
              the grenade button to sink, the d-pad up and down for speed),
              then back to your own
  Shift+F10   back to your own view

The bot chosen has lines drawn in the world while the window or the panel is
open: its goal green, who it is after red (where they were last seen orange),
the motion tracker's dots it keeps yellow, where it drives cyan, the way it
is moving white.

Only on the machine that runs the bots (the host's, or a game of your own).
director.c (the cameras), hud.c (the panel) and main.c call in here; bots.c
writes the text (bots_debug_describe, bots_debug_summary) and keeps each
bot's last tick (bot_debug_snapshot) and log lines.
*/

/* (the bots are the dedicated server's alone: none of this is in a
player's build, bot_client.c) */
#ifdef HALO_SERVER


#include "cseries.h"
#include "camera/observer.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "input/input.h"
#include "interface/interface.h"
#include "main/console.h"
#include "main/main.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "units/units.h"

#include "bot_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* (the file goes by the log's own path on this machine, not an Xbox one: the
   C library's fopen, not the port's, port/linux/include/stdio.h) */
#ifdef fopen
#undef fopen
#endif

/* ---------- the other halves */

/* bots.c */
long bots_debug_next(long after, int direction);
long bots_debug_unit(long index);
long bots_debug_nearest_to_view(real_point3d const *from, real_vector3d const *forward);
void bots_debug_summary(char *buffer, long size, long selected);
void bots_debug_describe(long index, char *buffer, long size);
extern long bot_debug_marked;
extern int bot_debug_capturing;
long bots_configured(void);
long bots_fill(void);

void platform_log(char const *format, ...);
/* xbox_files.c: the log's path, in the logs folder */
extern char platform_log_path[];

/* the window (sdl_platform.c offers it: none on the dedicated server, the
   consoles or Android) */
static struct bot_debug_window_ops const *bot_debug_window;

/* ---------- globals */

static struct
{
	long selected; /* the bot chosen (its absolute index), or NONE */
	short camera; /* _bot_debug_camera_... */
	boolean panel; /* the in-game panel */
	boolean keys_down[8]; /* (last frame's, for the presses) */
	long built_tick; /* (when the text was last made) */
	char summary[24 * 1024];
	char detail[24 * 1024];
	char text[64 * 1024];
} bot_debug = { NONE, _bot_debug_camera_own, FALSE };

/* ---------- private code */

void bot_debug_window_register(struct bot_debug_window_ops const *ops)
{
	bot_debug_window = ops;
}

static boolean bot_debug_window_open(void)
{
	return bot_debug_window && bot_debug_window->is_open();
}

/* a key held now: the keyboard's own, from the platform (the game's keys
   reach the game only while the console or a text field has them); the
   game's, on a build with no platform keys */
static boolean bot_debug_key(short which, short key)
{
	if (bot_debug_window && bot_debug_window->key_held)
		return bot_debug_window->key_held(which) ? TRUE : FALSE;
	return input_key_is_down(key) ? TRUE : FALSE;
}

/* a key's press (not its holding), by its slot in keys_down */
static boolean bot_debug_pressed(short which, short key)
{
	boolean down = bot_debug_key(which, key);
	boolean pressed = down && !bot_debug.keys_down[which];

	bot_debug.keys_down[which] = down;
	return pressed;
}

static boolean bot_debug_shift(void)
{
	return bot_debug_key(_bot_debug_key_shift, _key_shift);
}

static boolean bot_debug_control(void)
{
	return bot_debug_key(_bot_debug_key_control, _key_control);
}

static void bot_debug_build(void)
{
	char const *mode_names[] = { "your own view", "the bot's eyes", "behind the bot", "free (noclip)" };
	long length;

	bots_debug_summary(bot_debug.summary, sizeof(bot_debug.summary), bot_debug.selected);
	bots_debug_describe(bot_debug.selected, bot_debug.detail, sizeof(bot_debug.detail));
	length = snprintf(bot_debug.text, sizeof(bot_debug.text),
		"#BOTS  game time %.1f s   camera: %s\n"
		"~F8 next bot (Shift back, Ctrl the one in view)  F10 camera  Shift+F10 own view  Ctrl+F7 save all to a file  Shift+F7 panel\n"
		"%s\n%s",
		game_time_get() / (real)TICKS_PER_SECOND, mode_names[bot_debug.camera], bot_debug.summary, bot_debug.detail);
	(void)length;
	bot_debug.built_tick = game_time_get();
}

/* text to a file, without the marks at the starts of its lines that colour
   them in the window */
static void bot_debug_write_lines(FILE *file, char const *text)
{
	char const *at = text;

	while (*at)
	{
		char const *end = strchr(at, '\n');
		size_t length = end ? (size_t)(end - at) : strlen(at);

		if (length && (at[0] == '#' || at[0] == '!' || at[0] == '>' || at[0] == '~'))
		{
			/* (a heading and the bot's own line stand out with a rule) */
			if (at[0] == '#' || at[0] == '>')
				fputs(at[0] == '>' ? "\n==== " : "---- ", file);
			at++, length--;
		}
		fwrite(at, 1, length, file);
		fputc('\n', file);
		at = end ? end + 1 : at + length;
	}
}

/* every bot, all of it, to a file in the logs folder */
static void bot_debug_save(void)
{
	char folder[512], path[640], stamp[32];
	char *slash;
	FILE *file;
	time_t now = time(NULL);
	long index, first;

	snprintf(folder, sizeof(folder), "%s", platform_log_path[0] ? platform_log_path : "debug.txt");
	slash = strrchr(folder, '/');
	if (!slash)
		slash = strrchr(folder, '\\');
	if (slash)
		slash[1] = 0;
	else
		folder[0] = 0;
	strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", localtime(&now));
	snprintf(path, sizeof(path), "%sbot_debug_%s.txt", folder, stamp);
	file = fopen(path, "w");
	if (!file)
	{
		console_printf(FALSE, "bot debug: could not write %s", path);
		platform_log("bot debug: could not write %s", path);
		return;
	}
	bots_debug_summary(bot_debug.summary, sizeof(bot_debug.summary), bot_debug.selected);
	fprintf(file, "ChupathingyCE bots, game time %.1f s, map %s\nchosen: bot %ld\n\n", game_time_get() / (real)TICKS_PER_SECOND,
		main_get_multiplayer_map_name() ? main_get_multiplayer_map_name() : "?", bot_debug.selected);
	bot_debug_write_lines(file, bot_debug.summary);
	first = bots_debug_next(NONE, 1);
	for (index = first; index != NONE; )
	{
		bots_debug_describe(index, bot_debug.detail, sizeof(bot_debug.detail));
		bot_debug_write_lines(file, bot_debug.detail);
		index = bots_debug_next(index, 1);
		if (index == first || index <= NONE)
			break;
	}
	fclose(file);
	console_printf(FALSE, "bot debug: saved %s", path);
	platform_log("bot debug: saved %s", path);
}

/* ---------- public code */

/* HALO_BOT_DEBUG: the view as the game starts, any of "window", "panel",
   "eyes", "behind", "fly", "bot=<n>" (the bot chosen), "save=<seconds>"
   (a file of every bot after that many seconds of the game) */
static void bot_debug_from_environment(void)
{
	static boolean read;
	static long save_at = NONE;
	static boolean window_wanted;
	char const *text;

	if (!read)
	{
		read = TRUE;
		text = getenv("HALO_BOT_DEBUG");
		if (text)
		{
			char const *bot = strstr(text, "bot=");
			char const *save = strstr(text, "save=");

			bot_debug.panel = strstr(text, "panel") != NULL;
			window_wanted = strstr(text, "window") != NULL;
			bot_debug.camera = strstr(text, "eyes") ? _bot_debug_camera_eyes : strstr(text, "behind") ? _bot_debug_camera_behind :
				strstr(text, "fly") ? _bot_debug_camera_fly : _bot_debug_camera_own;
			if (bot)
				bot_debug.selected = atol(bot + 4);
			if (save)
				save_at = (long)(atof(save + 5) * TICKS_PER_SECOND);
		}
	}
	/* (the window once there is one to open: the platform offers it with its
	   first events) */
	if (window_wanted && bot_debug_window)
	{
		window_wanted = FALSE;
		if (!bot_debug_window->is_open())
			bot_debug_window->toggle();
	}
	if (save_at != NONE && game_time_get() >= save_at)
	{
		save_at = NONE;
		bot_debug_save();
	}
}

void bot_debug_update(void)
{
	boolean window_key, next_key, camera_key;
	boolean shift = bot_debug_shift(), control = bot_debug_control();

	bot_debug_from_environment();

	window_key = bot_debug_pressed(_bot_debug_key_f7, _key_f7);
	next_key = bot_debug_pressed(_bot_debug_key_f8, _key_f8);
	camera_key = bot_debug_pressed(_bot_debug_key_f10, _key_f10);
	if (window_key)
	{
		if (control)
			bot_debug_save();
		else if (shift || !bot_debug_window)
			bot_debug.panel = !bot_debug.panel;
		else if (!bot_debug_window->toggle())
			bot_debug.panel = TRUE; /* (no window to be had: the panel) */
	}
	if (next_key)
	{
		if (control && local_player_get_player_index(0) != NONE)
		{
			/* (the one nearest the middle of what this machine's first player sees) */
			long chosen = bots_debug_nearest_to_view(&render.camera.position, &render.camera.forward);

			if (chosen != NONE)
				bot_debug.selected = chosen;
		}
		else
			bot_debug.selected = bots_debug_next(bot_debug.selected, shift ? -1 : 1);
	}
	if (camera_key)
	{
		if (shift)
			bot_debug.camera = _bot_debug_camera_own;
		else
		{
			bot_debug.camera = (short)((bot_debug.camera + 1) % NUMBER_OF_BOT_DEBUG_CAMERAS);
			if (bot_debug.camera != _bot_debug_camera_own && bot_debug.camera != _bot_debug_camera_fly && bot_debug.selected == NONE)
				bot_debug.selected = bots_debug_next(NONE, 1);
		}
	}
	/* (the bots keep their log lines, and the chosen one has its lines in
	   the world, while there is something to show them on) */
	bot_debug_capturing = bot_debug_window_open() || bot_debug.panel;
	bot_debug_marked = bot_debug_capturing ? bot_debug.selected : NONE;
	if (bot_debug_capturing && bot_debug.selected == NONE)
		bot_debug.selected = bots_debug_next(NONE, 1);
	if (bot_debug_capturing && (game_time_get() != bot_debug.built_tick || bot_debug.built_tick == 0))
	{
		bot_debug_build();
		if (bot_debug_window_open())
			bot_debug_window->set_text(bot_debug.text);
	}
}

short bot_debug_camera_mode(short local_player_index, long *unit_index)
{
	*unit_index = NONE;
	/* (this machine's first player's view only) */
	if (local_player_index != 0 || bot_debug.camera == _bot_debug_camera_own || !game_engine_running())
		return _bot_debug_camera_own;
	if (bot_debug.camera == _bot_debug_camera_fly)
		return _bot_debug_camera_fly;
	*unit_index = bots_debug_unit(bot_debug.selected);
	/* (the bot dead: your own view until it is back) */
	return *unit_index != NONE ? bot_debug.camera : _bot_debug_camera_own;
}

boolean bot_debug_flying(short local_player_index)
{
	long unit_index;

	return bot_debug_camera_mode(local_player_index, &unit_index) == _bot_debug_camera_fly;
}

/* the camera through the chosen bot's eyes, or behind it (director.c) */
void bot_debug_camera_update(void *data, struct camera_control const *controls, struct observer_command *result)
{
	long unit_index;
	short mode = bot_debug_camera_mode(0, &unit_index);
	struct unit_datum *unit;
	real_vector3d forward;
	real length;
	short parameter;

	(void)data;
	(void)controls;
	csmemset(result, 0, sizeof(*result));
	if (unit_index == NONE || (mode != _bot_debug_camera_eyes && mode != _bot_debug_camera_behind))
		return;
	unit = unit_get(unit_index);
	forward = unit->unit.aiming_vector;
	length = (real)sqrt(forward.i * forward.i + forward.j * forward.j + forward.k * forward.k);
	if (length < 0.01f)
	{
		forward = unit->object.forward;
		length = 1.f;
	}
	forward.i /= length, forward.j /= length, forward.k /= length;
	if (mode == _bot_debug_camera_eyes)
	{
		/* (a little in front of its eyes, out of its helmet: as far as there
		   is room for, short of a wall in front of its face. Pushed on into
		   the wall, the camera saw through it) */
		struct collision_result collision;
		real_vector3d ahead;
		real room = 0.25f;

		unit_get_camera_position(unit_index, &result->focus_position);
		ahead.i = forward.i * 0.45f, ahead.j = forward.j * 0.45f, ahead.k = forward.k * 0.45f;
		if (collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
			FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit), &result->focus_position, &ahead,
			object_get_ultimate_parent(unit_index), &collision))
		{
			room = MAX(0.f, collision.t * 0.45f - 0.2f);
		}
		room = MIN(room, 0.25f);
		result->focus_position.x += forward.i * room;
		result->focus_position.y += forward.j * room;
		result->focus_position.z += forward.k * room;
		result->focus_distance = 0.f;
	}
	else
	{
		/* (behind and a little over it, looking the way it looks; further
		   back from a vehicle, by its size) */
		struct object_datum *root = object_get(object_get_ultimate_parent(unit_index));
		real yaw = (real)atan2(forward.j, forward.i);

		result->focus_position = root->object.bounding_sphere_center;
		/* (a unit vector: the observer asserts on any other) */
		forward.i = (real)cos(yaw) * 0.9578263f;
		forward.j = (real)sin(yaw) * 0.9578263f;
		forward.k = -0.2873479f;
		result->focus_distance = 2.5f + (root != (struct object_datum *)unit ? root->object.bounding_sphere_radius * 1.5f : 0.f);
	}
	result->forward = forward;
	observer_up_from_forward(&result->forward, &result->up);
	result->field_of_view = DEGREES_TO_RADIANS(70.f);
	result->flags = FLAG(_observer_command_valid_bit);
	/* (at once: no easing after it) */
	for (parameter = 0; parameter < NUMBER_OF_OBSERVER_COMMAND_PARAMETERS; parameter++)
	{
		result->parameter_flags[parameter] = FLAG(_observer_time_valid_bit) | FLAG(_observer_time_force_bit);
		result->parameter_timers[parameter] = 0.f;
	}
}

/* the panel on the game's screen (hud.c): the chosen bot, on the left */
void bot_debug_draw(short local_player_index)
{
	long font_index;
	struct font_header *font;
	short line_height;
	rectangle2d bounds;
	real_argb_color color, shadow = { 1.f, 0.f, 0.f, 0.f };
	char const *at;
	long line = 0;

	if (local_player_index != 0)
		return;
	/* (the camera's, while it is not your own: a line at the top) */
	if (bot_debug.camera != _bot_debug_camera_own && !bot_debug.panel)
	{
		char const *names[] = { "", "EYES", "BEHIND", "FREE CAMERA" };
		char top[96];
		long unit_index;

		bot_debug_camera_mode(0, &unit_index);
		font_index = interface_get_tag_index(_interface_font_terminal);
		if (font_index == NONE)
			return;
		snprintf(top, sizeof(top), "%s%s%ld   (F10 next, Shift+F10 own view, F8 next bot)", names[bot_debug.camera],
			bot_debug.camera == _bot_debug_camera_fly ? "" : " OF BOT ", bot_debug.camera == _bot_debug_camera_fly ? 0L : bot_debug.selected);
		if (bot_debug.camera == _bot_debug_camera_fly)
			snprintf(top, sizeof(top), "FREE CAMERA: move and look as you play, fire to rise, grenade to sink   (F10 own view)");
		bounds = render.camera.window_bounds;
		bounds.y0 = (short)(bounds.y0 + 8);
		bounds.y1 = (short)(bounds.y0 + 20);
		color = *global_real_argb_white;
		draw_string_set_draw_mode(font_index, NONE, 2, 0, &color);
		rasterizer_draw_string(&bounds, NULL, NULL, 0, top);
	}
	if (!bot_debug.panel || !bot_debug.text[0])
		return;
	font_index = interface_get_tag_index(_interface_font_terminal);
	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	/* (the chosen bot's part: from its heading) */
	at = strchr(bot_debug.text, '>') ? strstr(bot_debug.text, "\n>") : NULL;
	at = at ? at + 1 : bot_debug.text;
	while (*at && line < 40)
	{
		char row[200];
		char const *end = strchr(at, '\n');
		long length = end ? (long)(end - at) : (long)strlen(at);
		char kind = length ? at[0] : 0;
		long skip = (kind == '#' || kind == '!' || kind == '>' || kind == '~') ? 1 : 0;
		long copy = MIN(length - skip, (long)sizeof(row) - 1);

		memcpy(row, at + skip, (size_t)MAX(copy, 0));
		row[MAX(copy, 0)] = 0;
		bounds = render.camera.window_bounds;
		bounds.x0 = (short)(bounds.x0 + 8);
		bounds.y0 = (short)(bounds.y0 + 40 + line * line_height);
		bounds.y1 = (short)(bounds.y0 + line_height);
		if (bounds.y1 > render.camera.window_bounds.y1 - 40)
			break;
		color = *global_real_argb_white;
		if (kind == '#')
			color.red = 1.f, color.green = 0.8f, color.blue = 0.35f;
		else if (kind == '!')
			color.red = 1.f, color.green = 0.45f, color.blue = 0.4f;
		else if (kind == '>')
			color.red = 0.5f, color.green = 1.f, color.blue = 0.55f;
		else if (kind == '~')
			color.red = color.green = color.blue = 0.6f;
		/* (a shadow, to be read over the bright parts of the world) */
		offset_rectangle2d(&bounds, 1, 1);
		draw_string_set_draw_mode(font_index, NONE, 0, 0, &shadow);
		rasterizer_draw_string(&bounds, NULL, NULL, 0, row);
		offset_rectangle2d(&bounds, -1, -1);
		draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
		rasterizer_draw_string(&bounds, NULL, NULL, 0, row);
		at = end ? end + 1 : at + length;
		line++;
	}
}
#endif /* HALO_SERVER */
