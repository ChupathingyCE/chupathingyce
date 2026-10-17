/*
POWERUP_TIMERS.C

display.powerup_timers: a small line of the HUD's text for each local player
with the seconds left on their timed powerups. The original game shows none.

Active camouflage and full spectrum vision count down in the player's
powerup_durations (players.c's player_update_powerups, one a tick; the
distributed netcode sends them to a client). An overshield has no timer: the
shield above full drains by a fixed step each tick of a multiplayer game
(damage.c), so what is left of it gives the time; in campaign it does not
drain and shows none. hud.c calls in here.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/hud_messaging.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "units/units.h"

#include "powerup_timers.h"

/* ---------- constants */

/* damage.c: an overshield's drain each tick, and the most it charges to */
#define POWERUP_TIMERS_OVERSHIELD_DRAIN 0.00074074074f
#define POWERUP_TIMERS_OVERSHIELD_MAXIMUM 3.0f

/* ---------- prototypes */

/* port_config.c */
int config_boolean(char const *name);
unsigned long config_changes(void);

/* ---------- private code */

static boolean powerup_timers_enabled(
	void)
{
	static boolean enabled = FALSE;
	static unsigned long read_at = (unsigned long)-1;

	/* (read again when Settings changes it) */
	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.powerup_timers") != 0;
	}

	return enabled;
}

/* whole seconds left of `ticks`, rounded up */
static long powerup_timers_seconds(
	long ticks)
{
	return (ticks + TICKS_PER_SECOND - 1) / TICKS_PER_SECOND;
}

/* the ticks left of a unit's overshield, or 0 for none (or none that drains) */
static long powerup_timers_overshield_ticks(
	long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	real above_full;

	if (!unit || !game_engine_running() || unit->object.maximum_shield_vitality <= 0.0f)
		return 0;
	/* (still charging: all of it will drain) */
	if (TEST_FLAG(unit->object.damage_flags, _object_shield_over_charging_bit))
		above_full = POWERUP_TIMERS_OVERSHIELD_MAXIMUM - 1.0f;
	else
		above_full = unit->object.shield_vitality - 1.0f;
	if (above_full <= 0.0f)
		return 0;

	return (long)(above_full / POWERUP_TIMERS_OVERSHIELD_DRAIN);
}

/* adds "<name> <seconds>" to `text`, after three spaces if it has some */
static void powerup_timers_append(
	wchar_t *text,
	unsigned long size,
	wchar_t const *name,
	long ticks)
{
	unsigned long length = ustrlen(text);

	if (length + 1 >= size)
		return;
	usnprintf(text + length, size - length - 1, length ? L"   %s %ld" : L"%s %ld", name, powerup_timers_seconds(ticks));
	text[size - 1] = 0;
}

/* ---------- public code */

void powerup_timers_draw(
	short local_player_index)
{
	long player_index = local_player_get_player_index(local_player_index);
	long font_index = hud_get_font_index();
	struct player_datum *player;
	struct font_header *font;
	real_argb_color color = *global_real_argb_white;
	rectangle2d bounds;
	wchar_t text[96];
	long ticks;
	short height;

	if (!powerup_timers_enabled() || player_index == NONE || font_index == NONE)
		return;
	player = player_get(player_index);
	if (player->unit_index == NONE)
		return;
	text[0] = 0;
	ticks = player->powerup_durations[_player_powerup_active_camouflage];
	if (ticks > 0)
		powerup_timers_append(text, NUMBEROF(text), L"CAMO", ticks);
	ticks = powerup_timers_overshield_ticks(player->unit_index);
	if (ticks > 0)
		powerup_timers_append(text, NUMBEROF(text), L"OVERSHIELD", ticks);
	ticks = player->powerup_durations[_player_powerup_full_spectrum_vision];
	if (ticks > 0)
		powerup_timers_append(text, NUMBEROF(text), L"VISION", ticks);
	if (!text[0])
		return;
	/* one line, centered near the top of the view, in the HUD's font */
	font = font_definition_get(font_index);
	height = (short)(font->ascending_height + font->descending_height);
	bounds = render.camera.window_bounds;
	bounds.y0 = (short)(bounds.y0 + 24);
	bounds.y1 = (short)(bounds.y0 + height);
	color.alpha = 0.7f;
	/* draw mode 2: centered */
	draw_string_set_draw_mode(font_index, NONE, 2, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
}
