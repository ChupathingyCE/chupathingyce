/*
H2_RUN.C

port, test only (not in a release build): the Halo 2 campaign auto-run
harness's event log (tools/h2run.sh). With HALO_H2_RUN set, a Halo 2 level's
script events are logged as they happen, one line each:

	h2run <tick> <ms> <kind> <text>

tick: the game time (30 a second), ms: the host's clock since the first
line (wall time: on a fixed tick it runs ahead of the game's), kind:

	script    a startup or dormant script's thread began (start) or ended (end)
	wake      a script woke another
	squad     ai_place / ai_place_in_vehicle / ai_kill / ai_erase / ai_migrate / ai_disposable / ai_renew / ai_vehicle_exit
	placed    the actors ai_place made (h2_ai.c)
	drop      vehicle_unload / vehicle_load_magic / a dropship made
	cine      cinematic_start / _stop / skip start and stop, camera control,
	          fade_in / fade_out, cinematic_set_title
	bsp       switch_bsp (asked); bsp_loaded (main.c's poll: done)
	save      game_save* asked; checkpoint lines in the log are what was taken
	revert    game_revert asked; revert (main.c's poll: the game time went back)
	music     sound_looping_start / _stop / _set_alternate
	dialogue  sound_impulse_start, ai_play_line*, cs_play_line
	objective objectives_show_up_to / objectives_finish_up_to
	segment   data_mine_set_mission_segment
	won       game_won, game_lost
	death     a player's unit died (main.c's poll)
	harness   what the harness itself did (a step, a skip, an assist)

h2_run_wait_text: an event whose line holds this text is noted (main.c's
"@waitlog text"), h2_run_wait_seen.

Shots (HALO_H2_RUN_SHOTS=1, tools/h2run.sh's default): every beat (the
watched calls that move a mission on), every harness line (a step, a fight,
a skip, an input), a BSP loaded, a death, a revert, a cinematic's start and
end asks for a shot: h2_run_shot_pending holds the first event's text, the
count since, and the tick it is due (a step's a third of a second after, so
the shot shows where the player went); main.c takes it at most every
h2_run_shot_gap ticks, events in between merged into it (none lost: the
contact sheet lists the timeline's events between shots, tools/h2run/sheet.py).

Halo CE levels (tools/cerun.sh): the same log and harness on a Halo CE solo
level's scripts (h2_run_level), the event kinds the same (Halo CE's own
functions of the same names: ai_place, game_save, switch_bsp, cinematic_*,
sound_looping_*, game_won...).
*/

#include "cseries.h"
#include "game/game.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);
unsigned long system_milliseconds(void);

#if !defined(HALO_RELEASE)

static int h2_run_state = -1;
static unsigned long h2_run_started;
/* (the beats logged: kinds that move a mission on, for main.c's real-time
mode - nothing new for a while is when it steps in) */
unsigned long h2_run_beats;
/* (any script event - dialogue, a wake, a script's start or end - for the
harness's Halo CE steps: a tutorial's talk is the mission going on) */
unsigned long h2_run_activity;
char h2_run_wait_text[96];
boolean h2_run_wait_seen;
/* (a beat's shot asked for: main.c takes it, rate-limited) */
char h2_run_shot_kind[16];
/* (between cinematic_skip_start_internal and _stop_internal: a skip lands
on the skip's save, not an older checkpoint - main.c skips only then) */
boolean h2_run_skippable;
/* (shots at every beat and step: main.c reads and clears these) */
boolean h2_run_shots_every;
char h2_run_shot_pending[120];
long h2_run_shot_pending_count;
long h2_run_shot_due;
static boolean h2_run_last_beat;

boolean h2_run_enabled(
	void)
{
	if (h2_run_state < 0)
	{
		char const *setting = getenv("HALO_H2_RUN");

		h2_run_state = setting && *setting && strcmp(setting, "0") != 0;
		h2_run_started = system_milliseconds();
		setting = getenv("HALO_H2_RUN_SHOTS");
		h2_run_shots_every = setting && *setting && strcmp(setting, "0") != 0;
	}
	return h2_run_state > 0;
}

static struct
{
	char const *name;
	char const *kind;
	boolean prefix;
	boolean beat;
	boolean shot;
} const h2_run_watched[] =
{
	{ "ai_place_in_vehicle", "squad", FALSE, TRUE, FALSE },
	{ "ai_place", "squad", FALSE, TRUE, FALSE },
	{ "ai_kill", "squad", TRUE, FALSE, FALSE },
	{ "ai_erase", "squad", TRUE, FALSE, FALSE },
	{ "ai_migrate", "squad", FALSE, FALSE, FALSE },
	/* (the H2EK reference's timeline has them: ai_print_disposal / ai_print_scripting) */
	{ "ai_disposable", "squad", FALSE, FALSE, FALSE },
	{ "ai_renew", "squad", FALSE, FALSE, FALSE },
	{ "ai_vehicle_exit", "squad", FALSE, FALSE, FALSE },
	{ "vehicle_unload", "drop", FALSE, TRUE, FALSE },
	{ "vehicle_load_magic", "drop", FALSE, FALSE, FALSE },
	{ "cinematic_start", "cine", FALSE, TRUE, TRUE },
	{ "cinematic_stop", "cine", FALSE, TRUE, FALSE },
	{ "cinematic_skip_start", "cine", TRUE, FALSE, FALSE },
	{ "cinematic_skip_stop", "cine", TRUE, FALSE, FALSE },
	{ "cinematic_set_title", "cine", FALSE, TRUE, TRUE },
	{ "camera_control", "cine", FALSE, FALSE, FALSE },
	{ "fade_in", "cine", FALSE, FALSE, FALSE },
	{ "fade_out", "cine", FALSE, FALSE, FALSE },
	{ "switch_bsp", "bsp", TRUE, TRUE, FALSE },
	{ "game_save", "save", TRUE, TRUE, TRUE },
	{ "game_revert", "revert", FALSE, TRUE, FALSE },
	{ "sound_looping_start", "music", FALSE, TRUE, FALSE },
	{ "sound_looping_stop", "music", FALSE, TRUE, FALSE },
	{ "sound_looping_set_alternate", "music", FALSE, TRUE, FALSE },
	{ "sound_impulse_start", "dialogue", FALSE, FALSE, FALSE },
	{ "ai_play_line", "dialogue", TRUE, FALSE, FALSE },
	{ "cs_play_line", "dialogue", FALSE, FALSE, FALSE },
	{ "objectives_show_up_to", "objective", FALSE, TRUE, FALSE },
	{ "objectives_finish_up_to", "objective", FALSE, TRUE, FALSE },
	{ "data_mine_set_mission_segment", "segment", FALSE, TRUE, TRUE },
	{ "game_won", "won", FALSE, TRUE, TRUE },
	{ "game_lost", "won", FALSE, TRUE, TRUE },
	{ "wake", "wake", FALSE, FALSE, FALSE },
};

/* the kind a watched function's call is logged as (NULL: not watched) */
char const *h2_run_watched_kind(
	char const *function_name)
{
	int i;

	if (!function_name)
		return NULL;
	for (i = 0; i < (int)NUMBEROF(h2_run_watched); i++)
	{
		if (h2_run_watched[i].prefix ? !strncmp(function_name, h2_run_watched[i].name, strlen(h2_run_watched[i].name)) :
			!strcmp(function_name, h2_run_watched[i].name))
		{
			if (h2_run_watched[i].beat)
				h2_run_beats++;
			h2_run_last_beat = h2_run_watched[i].beat;
			/* (a skip point set: the cinematic may be skipped - main.c) */
			if (!strcmp(function_name, "cinematic_skip_start_internal"))
				h2_run_skippable = TRUE;
			else if (!strcmp(function_name, "cinematic_skip_stop_internal"))
				h2_run_skippable = FALSE;
			if (h2_run_watched[i].shot && !h2_run_shot_kind[0])
				csstrncpy(h2_run_shot_kind, h2_run_watched[i].kind, sizeof(h2_run_shot_kind) - 1);
			return h2_run_watched[i].kind;
		}
	}
	return NULL;
}

void h2_run_log(
	char const *kind,
	char const *format,
	...)
{
	char text[400];
	va_list arguments;

	if (!h2_run_enabled())
		return;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	/* (not squads: a30's continuous ai_erase of its Banshee pilots, every 3 s) */
	if (strcmp(kind, "shot") && strcmp(kind, "harness") && strcmp(kind, "squad") && strcmp(kind, "placed"))
		h2_run_activity++;
	if (h2_run_wait_text[0] && !h2_run_wait_seen && (strstr(text, h2_run_wait_text) || strstr(kind, h2_run_wait_text)))
		h2_run_wait_seen = TRUE;
	platform_log("h2run %ld %lu %s %s", game_in_progress() ? (long)game_time_get() : -1L,
		system_milliseconds() - h2_run_started, kind, text);
	/* (a shot at every beat and step) */
	if (h2_run_shots_every && game_in_progress())
	{
		boolean beat = h2_run_last_beat;
		long delay = 0;

		h2_run_last_beat = FALSE;
		if (!strcmp(kind, "harness"))
		{
			/* (the harness's own bookkeeping: no new view) */
			static char last_input[96];
			static long last_input_tick;

			if (!strncmp(text, "wait ", 5) || !strncmp(text, "level ", 6) || !strncmp(text, "end:", 4))
				beat = !strncmp(text, "end:", 4);
			/* (an input pretended again and again while a script waits for
			it - looked up, every 2 s -: its first only, and one a minute) */
			else if (!strncmp(text, "input:", 6) && !strncmp(text, last_input, 32) &&
				(long)game_time_get() - last_input_tick < 1800 && (long)game_time_get() >= last_input_tick)
				beat = FALSE;
			else
				beat = TRUE, delay = 10;
			if (!strncmp(text, "input:", 6) && beat)
			{
				csstrncpy(last_input, text, sizeof(last_input) - 1);
				last_input_tick = (long)game_time_get();
			}
		}
		else if (!strcmp(kind, "bsp") || !strcmp(kind, "death") || !strcmp(kind, "revert") || !strcmp(kind, "won") ||
			(!strcmp(kind, "cine") && strstr(text, "cinematic ")))
			beat = TRUE, delay = !strcmp(kind, "revert") || !strcmp(kind, "bsp") ? 5 : 0;
		else if (!strcmp(kind, "shot") || !strcmp(kind, "script") || !strcmp(kind, "dialogue") || !strcmp(kind, "wake"))
			beat = FALSE;
		if (beat)
		{
			long due = (long)game_time_get() + delay;

			if (!h2_run_shot_pending_count)
			{
				snprintf(h2_run_shot_pending, sizeof(h2_run_shot_pending), "%s %s", kind, text);
				h2_run_shot_due = due;
			}
			else if (due > h2_run_shot_due)
				h2_run_shot_due = due;
			h2_run_shot_pending_count++;
		}
	}
}

/* (hs_runtime.c's Halo CE script dump) a host path opened as is: the
engine's own fopen takes paths as Xbox ones, under the data folder */
#undef fopen
FILE *h2_run_host_fopen(
	char const *path,
	char const *mode)
{
	return fopen(path, mode);
}

/* the level the log and harness follow: a Halo 2 level's scripts, or a
Halo CE solo level's (not the main menu's) */
boolean h2_run_level(
	void)
{
	boolean h2_scripts_level(void);
	struct scenario *scenario;

	if (h2_scripts_level())
		return TRUE;
	scenario = game_in_progress() ? global_scenario_try_and_get() : NULL;
	return scenario && scenario->type == _scenario_type_solo;
}

#else

boolean h2_run_level(void) { return FALSE; }
boolean h2_run_enabled(void) { return FALSE; }
char const *h2_run_watched_kind(char const *function_name) { (void)function_name; return NULL; }
void h2_run_log(char const *kind, char const *format, ...) { (void)kind; (void)format; }

#endif
