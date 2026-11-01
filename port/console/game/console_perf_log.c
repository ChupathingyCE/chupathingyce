/*
CONSOLE_PERF_LOG.C

The frame timing log (port/linux/include/halo_perf_log.h) for the consoles
that draw through the shared GL layer (HALO_CONSOLE_GL_LAYER: the Xbox
360, the Wii U), on with console.perf_log = true in config.toml. It is the
original Xbox's log (port/xbox/game/xbox_perf_log.c in warthog-xbox) with
the same lines, so a run on any console reads the same way: once a second,
the frame time (average and worst), the game ticks, the render's parts,
sound, network, present (on these consoles it holds the wait for the
screen and for the graphics chip) and the wait before it, the draw calls
and objects drawn; a second line splits the render finer. Times come from
the performance counter. Counters and timers only between the lines.

With debug.gpu_stats as well, the GL layer adds its own line (draws,
target changes, data streamed), and the Wii U's its frame split.
*/

#include "cseries.h"
#include "halo_perf_log.h"

#if defined(HALO_PERF_LOG) && !defined(HALO_XBOX_CONSOLE)

#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/players.h"
#include "units/units.h"

int config_boolean(const char *name);

int halo_perf_log_on = FALSE;
unsigned long halo_perf_counters[NUMBER_OF_HALO_PERF_COUNTERS];

/* ---------- the clock */

typedef long long perf_time;

static perf_time perf_now(void)
{
	LARGE_INTEGER now;

	QueryPerformanceCounter(&now);
	return (perf_time)now.QuadPart;
}

/* ---------- the second being measured */

static struct
{
	boolean started;
	perf_time second_start;
	perf_time last_frame;
	/* (counter ticks a millisecond) */
	double ticks_per_millisecond;

	long frames;
	perf_time frame_total;
	perf_time frame_maximum;
	long slow_frames;

	perf_time phase_start[NUMBER_OF_HALO_PERF_PHASES];
	perf_time phase_total[NUMBER_OF_HALO_PERF_PHASES];
	int render_part;
	perf_time render_part_start;

	short zoom_level;
} perf;

void halo_perf_begin(int phase)
{
	perf.phase_start[phase] = perf_now();
}

void halo_perf_end(int phase)
{
	perf.phase_total[phase] += perf_now() - perf.phase_start[phase];
}

void halo_perf_render_part(int phase)
{
	perf_time now = perf_now();

	if (perf.render_part != NONE)
		perf.phase_total[perf.render_part] += now - perf.render_part_start;
	perf.render_part = phase;
	perf.render_part_start = now;
}

/* the local player's zoom: a line when it changes */
static void perf_zoom_update(void)
{
	short zoom_level = NONE;

	if (game_in_progress())
	{
		long player_index = local_player_get_player_index(0);
		struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

		if (player && player->unit_index != NONE)
			zoom_level = unit_get_zoom_level(player->unit_index);
	}

	if (zoom_level != perf.zoom_level)
	{
		if (perf.zoom_level == NONE)
			error(_error_log, "perf: zoom in (level %d)", zoom_level + 1);
		else if (zoom_level == NONE)
			error(_error_log, "perf: zoom out");
		else
			error(_error_log, "perf: zoom level %d", zoom_level + 1);
		perf.zoom_level = zoom_level;
	}
}

static double perf_milliseconds(perf_time ticks, long frames)
{
	return frames > 0 ? (double)ticks / perf.ticks_per_millisecond / (double)frames : 0.0;
}

static void perf_second_log(perf_time now)
{
	long frames = perf.frames;
	double frame_count = frames > 0 ? (double)frames : 1.0;
	unsigned long elapsed = (unsigned long)((double)(now - perf.second_start) / perf.ticks_per_millisecond);

	error(_error_log,
		"perf: %ld frames in %lu ms, frame %.1f avg %.1f max, %ld over 34 ms; "
		"tick %.1f (%.1f ticks), render %.1f (world %.1f objects %.1f transparent %.1f effects %.1f hud %.1f), "
		"sound %.1f, network %.1f, present %.1f, vsync wait %.1f; "
		"%.0f draws, %.0f objects, %.0f models (%.0f high lod) a frame; zoom %d",
		frames, elapsed,
		perf_milliseconds(perf.frame_total, frames),
		perf_milliseconds(perf.frame_maximum, 1),
		perf.slow_frames,
		perf_milliseconds(perf.phase_total[_halo_perf_tick], frames),
		(double)halo_perf_counters[_halo_perf_ticks] / frame_count,
		perf_milliseconds(perf.phase_total[_halo_perf_render], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_render_world], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_render_objects], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_render_transparent], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_render_effects], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_render_hud], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_sound], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_network], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_present], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_vsync_wait], frames),
		(double)halo_perf_counters[_halo_perf_draws] / frame_count,
		(double)halo_perf_counters[_halo_perf_objects] / frame_count,
		(double)halo_perf_counters[_halo_perf_models] / frame_count,
		(double)halo_perf_counters[_halo_perf_models_high_lod] / frame_count,
		perf.zoom_level + 1);

	{
		static int const render_parts[] =
		{
			_halo_perf_render_world,
			_halo_perf_render_objects,
			_halo_perf_render_transparent,
			_halo_perf_render_effects,
			_halo_perf_render_hud,
			_halo_perf_render_begin,
			_halo_perf_render_setup,
			_halo_perf_render_lens_flares,
			_halo_perf_render_screen_effect,
			_halo_perf_render_end
		};
		perf_time other = perf.phase_total[_halo_perf_render] - perf.phase_total[_halo_perf_render_sound];
		int index;

		/* (the render less its sound and its parts: the views' cameras,
		main_game_render's own work) */
		for (index = 0; index < (int)NUMBEROF(render_parts); index++)
			other -= perf.phase_total[render_parts[index]];
		error(_error_log,
			"perf: render detail: begin %.1f setup %.1f lens flares %.1f (%.0f tests) screen effect %.1f (%.1f passes) "
			"end %.1f sound %.1f other %.1f; occlusion wait %.1f, texture wait %.1f (%.1f loads a frame)",
			perf_milliseconds(perf.phase_total[_halo_perf_render_begin], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_render_setup], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_render_lens_flares], frames),
			(double)halo_perf_counters[_halo_perf_visibility_tests] / frame_count,
			perf_milliseconds(perf.phase_total[_halo_perf_render_screen_effect], frames),
			(double)halo_perf_counters[_halo_perf_screen_effect_passes] / frame_count,
			perf_milliseconds(perf.phase_total[_halo_perf_render_end], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_render_sound], frames),
			perf_milliseconds(other, frames),
			perf_milliseconds(perf.phase_total[_halo_perf_occlusion_wait], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_texture_wait], frames),
			(double)halo_perf_counters[_halo_perf_texture_loads] / frame_count);
	}
}

static void perf_second_start(perf_time now)
{
	perf.second_start = now;
	perf.frames = 0;
	perf.frame_total = 0;
	perf.frame_maximum = 0;
	perf.slow_frames = 0;
	csmemset(perf.phase_total, 0, sizeof(perf.phase_total));
	csmemset(halo_perf_counters, 0, sizeof(halo_perf_counters));
}

void halo_perf_frame(void)
{
	static int known = -1;
	perf_time now;

	if (known < 0)
	{
		LARGE_INTEGER frequency;

		known = config_boolean("console.perf_log") ? 1 : 0;
		halo_perf_log_on = known;
		perf.render_part = NONE;
		perf.zoom_level = NONE;
		if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
			frequency.QuadPart = 1000000;
		perf.ticks_per_millisecond = (double)frequency.QuadPart / 1000.0;
		if (known)
			error(_error_log, "perf: timing on, a line a second (counter %.0f kHz): times in ms a frame",
				perf.ticks_per_millisecond);
	}
	if (!halo_perf_log_on)
		return;

	now = perf_now();
	if (!perf.started)
	{
		perf.started = TRUE;
		perf.last_frame = now;
		perf_second_start(now);
		return;
	}

	{
		perf_time frame = now - perf.last_frame;

		perf.last_frame = now;
		perf.frames++;
		perf.frame_total += frame;
		if (frame > perf.frame_maximum)
			perf.frame_maximum = frame;
		if ((double)frame > perf.ticks_per_millisecond * 34.0)
			perf.slow_frames++;
	}

	perf_zoom_update();

	if ((double)(now - perf.second_start) >= perf.ticks_per_millisecond * 1000.0)
	{
		perf_second_log(now);
		perf_second_start(now);
	}
}

#endif
