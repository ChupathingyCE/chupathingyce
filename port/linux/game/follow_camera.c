/*
FOLLOW_CAMERA.C

The multiplayer follow camera. A dead player's camera watches their body
(dead_camera.c), as the original game's does. When the wait to respawn is
long (a game type's long respawn time, out of lives, odd man out, or a game
type that holds everyone's spawn until its next round), the camera then
follows a living player from behind, as Halo 2 and 3 do: a teammate in a
team game, the killer (or the leader) in a game without teams. LB and RB
(Q and E, or the mouse wheel, on the keyboard) switch who. The death camera
of a short respawn stays the original game's.

display.follow_camera picks when ("long_waits", "always" or "off") and
display.follow_camera_after how long a wait counts as long. It is this
machine's view only: nothing is sent, so it works in any host's game.
director.c and hud.c call in here; the camera itself is the network co-op
spectator's (coop_spectate_camera).
*/

#include "cseries.h"
#include "camera/dead_camera.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "input/input.h"
#include "memory/data.h"
#include "interface/hud_messaging.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"

#include "halo_port_limits.h"
#include "follow_camera.h"

/* ---------- constants */

/* how long the death camera watches the body before the follow camera
takes over (dead_camera.c's dead timer) */
#define FOLLOW_CAMERA_DEATH_TICKS (3 * TICKS_PER_SECOND)

enum
{
	_follow_camera_long_waits = 0,
	_follow_camera_always,
	_follow_camera_off,
};

/* ---------- globals */

static struct
{
	/* the game time this local player was first seen dead, or NONE alive */
	long dead_since;
	/* whether the camera has started following (until the respawn) */
	boolean following;
	/* the player followed, or NONE */
	long watched;
} follow_camera_locals[MAXIMUM_LOCAL_PLAYERS] =
{
	{ NONE, FALSE, NONE }, { NONE, FALSE, NONE }, { NONE, FALSE, NONE }, { NONE, FALSE, NONE },
};

/* who last killed each player (by absolute index), or NONE */
static long follow_camera_killers[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
static boolean follow_camera_killers_cleared = FALSE;

/* the direction each controller's switch was held last frame */
static short follow_camera_held[MAXIMUM_GAMEPADS];

/* port_config.c */
char const *config_string(char const *name);
double config_real(char const *name);
unsigned long config_changes(void);

/* input_abstraction.c: Q back, E or the mouse wheel on, held this tick */
short input_abstraction_port_follow_cycle(short controller_index);

/* ---------- private code */

static short follow_camera_setting(
	void)
{
	static short setting = _follow_camera_long_waits;
	static unsigned long read_at = (unsigned long)-1;

	/* (read again when Settings changes it) */
	if (read_at != config_changes())
	{
		char const *value = config_string("display.follow_camera");

		read_at = config_changes();
		setting = _follow_camera_long_waits;
		if (!csstrcmp(value, "always"))
			setting = _follow_camera_always;
		else if (!csstrcmp(value, "off"))
			setting = _follow_camera_off;
	}

	return setting;
}

/* display.follow_camera_after: a wait longer than this many seconds, left
once the death camera is done, is a long one */
static long follow_camera_after_ticks(
	void)
{
	static long ticks = 5 * TICKS_PER_SECOND;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		ticks = (long)(PIN(config_real("display.follow_camera_after"), 0.0, 600.0) * TICKS_PER_SECOND);
	}

	return ticks;
}

/* Whether the game type holds every spawn until its next round, however
short the respawn timer reads. None on this line yet.

Bomb (game_engine_bomb.c, on the grifball line) is the first: when it
reaches main, return game_engine_bomb_holds_spawns() here (its blast's pause
before the next round; a scorer's silent death already waits
game_engine_bomb_silent_respawn_ticks on the respawn timer, which
follow_camera_long_wait reads). Sabriel's idea for it, the scorer's camera
following their own thrown body through the blast, would also start here:
follow_camera_unit returning the scorer's dead unit (player->dead_unit_index)
during the pause instead of a teammate. */
static boolean follow_camera_engine_holds_spawns(
	void)
{
	return FALSE;
}

/* whether this dead player waits long enough to follow someone */
static boolean follow_camera_long_wait(
	long player_index)
{
	struct player_datum *player = player_get(player_index);

	if (game_engine_man_out(player_index) || follow_camera_engine_holds_spawns())
		return TRUE;
	return player->respawn_timer > follow_camera_after_ticks();
}

/* whether a player can be followed: alive and not this one */
static boolean follow_camera_can_watch(
	long self,
	long player_index)
{
	struct player_datum *player;

	if (player_index == NONE || player_index == self)
		return FALSE;
	player = player_try_and_get(player_index);
	return player && player->unit_index != NONE && !player->quit_out_of_game;
}

/* The next player to follow after `after` (or the first, if it is NONE),
`direction` 1 on or -1 back, wrapping around; in a team game teammates
only, unless none of them is alive. NONE if nobody is alive. */
static long follow_camera_next(
	long self,
	long after,
	short direction)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long candidates[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long count = 0;
	long position = NONE;
	long team_index = player_get(self)->team_index;
	boolean teams = game_engine_has_teams();
	long pass;

	/* teammates first, then (with none alive, or no teams) everyone */
	for (pass = teams ? 0 : 1; pass < 2 && count == 0; pass++)
	{
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (!follow_camera_can_watch(self, iterator.datum_index) ||
				(pass == 0 && player->team_index != team_index))
			{
				continue;
			}
			if (iterator.datum_index == after)
				position = count;
			if (count < (long)NUMBEROF(candidates))
				candidates[count++] = iterator.datum_index;
		}
	}
	if (count == 0)
		return NONE;
	if (position == NONE)
		return candidates[direction < 0 ? count - 1 : 0];
	return candidates[(position + direction + count) % count];
}

/* the first player to follow: in a game without teams, who killed this
one (or `dead` for one who just died), else the leader */
static long follow_camera_first(
	long self,
	long dead)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long best = NONE;
	long best_score = 0;

	if (game_engine_has_teams())
		return follow_camera_next(self, NONE, 1);
	if (dead != NONE && DATUM_INDEX_TO_ABSOLUTE_INDEX(dead) < HALO_PORT_MAXIMUM_NETWORK_PLAYERS &&
		follow_camera_killers_cleared)
	{
		long killer = follow_camera_killers[DATUM_INDEX_TO_ABSOLUTE_INDEX(dead)];

		if (follow_camera_can_watch(self, killer))
			return killer;
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long score;

		if (!follow_camera_can_watch(self, iterator.datum_index))
			continue;
		score = game_engine && game_engine->get_player_score ?
			game_engine->get_player_score(iterator.datum_index, _get_score_individual) : 0;
		if (best == NONE || score > best_score)
		{
			best = iterator.datum_index;
			best_score = score;
		}
	}

	return best;
}

/* 1 or -1 once per press of RB or LB (or the keyboard's), however long it is
held: one tick can span several frames */
static short follow_camera_pressed(
	short controller_index)
{
	struct gamepad_state const *gamepad;
	short direction = 0;
	short pressed;

	if (controller_index < 0 || controller_index >= MAXIMUM_GAMEPADS)
		return 0;
	gamepad = input_get_gamepad_state(controller_index);
	if (gamepad && gamepad->buttons[FIRST_GAMEPAD_ANALOG_BUTTON + _gamepad_analog_button_black] > 0)
		direction = 1;
	else if (gamepad && gamepad->buttons[FIRST_GAMEPAD_ANALOG_BUTTON + _gamepad_analog_button_white] > 0)
		direction = -1;
	else
		direction = input_abstraction_port_follow_cycle(controller_index);
	pressed = direction != follow_camera_held[controller_index] ? direction : 0;
	follow_camera_held[controller_index] = direction;
	return pressed;
}

/* ---------- public code */

long follow_camera_unit(
	short local_player_index)
{
	long self = local_player_get_player_index(local_player_index);
	struct player_datum *player;
	short pressed;
	long now;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return NONE;
	if (self == NONE || !game_engine_running())
	{
		follow_camera_locals[local_player_index].dead_since = NONE;
		follow_camera_locals[local_player_index].following = FALSE;
		return NONE;
	}
	player = player_get(self);
	/* checked every frame so a press is never missed or counted twice */
	pressed = follow_camera_pressed(player->network_player_data.controller_index);
	/* alive: back to the player's own view; keep `watched`, so the next
	death starts on the same teammate */
	if (player->unit_index != NONE || game_engine_showing_postgame() ||
		follow_camera_setting() == _follow_camera_off)
	{
		follow_camera_locals[local_player_index].dead_since = NONE;
		follow_camera_locals[local_player_index].following = FALSE;
		return NONE;
	}
	now = game_time_get();
	if (follow_camera_locals[local_player_index].dead_since == NONE)
		follow_camera_locals[local_player_index].dead_since = now;
	if (!follow_camera_locals[local_player_index].following)
	{
		/* the original death camera first, and all of a short wait */
		if (now - follow_camera_locals[local_player_index].dead_since < FOLLOW_CAMERA_DEATH_TICKS ||
			(follow_camera_setting() == _follow_camera_long_waits && !follow_camera_long_wait(self)))
		{
			return NONE;
		}
		follow_camera_locals[local_player_index].following = TRUE;
		follow_camera_locals[local_player_index].watched = follow_camera_first(self, self);
	}
	{
		long *watched = &follow_camera_locals[local_player_index].watched;

		if (pressed)
			*watched = follow_camera_next(self, *watched, pressed);
		else if (!follow_camera_can_watch(self, *watched))
		{
			/* the one followed died (or left): their killer in a game
			without teams, else the next */
			long dead = *watched;

			*watched = game_engine_has_teams() ? follow_camera_next(self, dead, 1) : follow_camera_first(self, dead);
		}

		return *watched != NONE ? player_get(*watched)->unit_index : NONE;
	}
}

void follow_camera_player_killed(
	long killing_player_index,
	long dead_player_index)
{
	long dead_absolute;

	if (!follow_camera_killers_cleared)
	{
		long index;

		for (index = 0; index < (long)NUMBEROF(follow_camera_killers); index++)
			follow_camera_killers[index] = NONE;
		follow_camera_killers_cleared = TRUE;
	}
	if (dead_player_index == NONE)
		return;
	dead_absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(dead_player_index);
	if (dead_absolute < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
		follow_camera_killers[dead_absolute] = killing_player_index != dead_player_index ? killing_player_index : NONE;
}

void follow_camera_draw(
	short local_player_index)
{
	long watched;
	long font_index = hud_get_font_index();
	struct font_header *font;
	real_argb_color color = *global_real_argb_white;
	rectangle2d bounds;
	wchar_t text[48];
	short height;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		!follow_camera_locals[local_player_index].following || font_index == NONE)
	{
		return;
	}
	watched = follow_camera_locals[local_player_index].watched;
	if (watched == NONE || !player_try_and_get(watched))
		return;
	usnprintf(text, NUMBEROF(text) - 1, L"FOLLOWING %.12s", player_get(watched)->name);
	text[NUMBEROF(text) - 1] = 0;
	/* one line, centered near the bottom of the view, in the HUD's font */
	font = font_definition_get(font_index);
	height = (short)(font->ascending_height + font->descending_height);
	bounds = render.camera.window_bounds;
	bounds.y0 = (short)(bounds.y1 - height - 24);
	bounds.y1 = (short)(bounds.y1 - 24);
	color.alpha = 0.8f;
	/* draw mode 2: centered */
	draw_string_set_draw_mode(font_index, NONE, 2, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
}
