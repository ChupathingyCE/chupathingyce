/* The client's spiral guard (source/game/game_time.c). A client whose
   machine can't simulate 30 ticks a second (an original Xbox in a game of
   more than about 24 players) ran every tick a frame was due, up to a
   second's, and each frame took longer than the last: one frame a second
   for the rest of the game. From a frame that was due more than the guard's
   few ticks, a frame due more still, and more than the last ran, runs only
   that many.

   The frames here are timed by a model of the machine: a frame takes its
   ticks times a tick's cost plus the drawing (the tester's log: 16 ms
   drawn, ticks of 18.7 ms at one a frame to 27.6 at four, 36 in the
   spiral), with long hitches (a load, a stall) now and then. Each case
   prints the ticks every frame ran; the test compares them with the same
   code without the guard. */
#include <math.h>
#include "harness.h"
#define TICKS_PER_SECOND 30
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define valid_real(x) (!isnan(x) && !isinf(x))
#define HALO_PERF_BEGIN(x) ((void)0)
#define HALO_PERF_END(x) ((void)0)
#define HALO_PERF_COUNT(x, n) ((void)0)
#define HALO_CONSOLE_NO_NETWORK_TEST
#define _error_log 0
static int lines_logged;
static void error(int type, const char *format, ...) { (void)type; (void)format; lines_logged++; }
/* (game.h's connections) */
#include "connections.inc"

struct network_game_client { int unused; };
struct network_game_server { int unused; };

static short connection;
static long ticks_run;
static struct network_game_client client;

static short game_connection(void) { return connection; }
static struct network_game_client *global_network_game_client_get(void) { return &client; }
static struct network_game_server *global_network_game_server_get(void) { return NULL; }
/* (FALSE: the clock waits for the host's first game update) */
static boolean started = TRUE;
static boolean network_game_client_server_has_started_game(struct network_game_client *c) { (void)c; return started; }
static boolean network_game_distributed_client(void) { return connection == _game_connection_network_client; }
static void update_client_local_ticks(long ticks) { (void)ticks; }
static void network_game_server_update_ticks(struct network_game_server *s, short ticks) { (void)s; (void)ticks; }
static long update_client_get_maximum_possible_server_time(void) { return 0x7FFFFFFF; }
static void game_tick(void) { ticks_run++; }
static void render_interpolation_tick(void) {}
static void network_distributed_tick(void) {}
static void render_interpolation_frame_begin(void) {}
static void render_interpolation_frame_end(void) {}
static void game_frame(real seconds) { (void)seconds; }
static real mcc_game_speed_client(real speed) { return speed; }
static void mcc_game_speed_frame(void) {}
/* (debug.freeze_at_tick: never) */
static boolean game_time_frozen_at_tick(void) { return FALSE; }
/* (a bsp is always loaded: no switch fails here) */
struct structure_bsp { int unused; };
static struct structure_bsp bsp;
static struct structure_bsp *global_structure_bsp_try_and_get(void) { return &bsp; }

#include "under_test.inc"

static struct game_time_globals_struct globals;

/* a tick's cost in ms when frame index runs n */
static double tick_cost(const char *model, int index, long n)
{
	/* (a fight the machine can't keep up with for 300 frames, then quiet) */
	if (!strcmp(model, "burst"))
		return index < 300 ? 36.0 : 18.7;
	if (!strcmp(model, "quiet"))
		return 18.7;
	if (!strcmp(model, "heavy"))
		return 18.7 + (27.6 - 18.7) * (n < 1 ? 0 : n > 4 ? 3 : n - 1) / 3.0;
	if (!strcmp(model, "fight"))
		return 30.0;
	if (!strcmp(model, "spiral"))
		return 36.0;
	if (!strcmp(model, "desktop"))
		return 1.0;
	return 0.0;
}

/* frames timed by the model: the ticks each ran, one line; the drawing
   takes render ms; every hitch_every frames one more second (0: none) */
static void trace(const char *model, double render, int hitch_every, int frames)
{
	double ms = 1000.0 / 30;
	int index;

	for (index = 0; index < frames; index++)
	{
		long before = ticks_run;
		long ran;

		game_time_update((real)(ms / 1000.0));
		ran = ticks_run - before;
		printf("%ld ", ran);
		ms = ran * tick_cost(model, index, ran) + render;
		if (hitch_every && index % hitch_every == hitch_every - 1)
			ms += 1000.0;
		/* (main_update_time pins a frame at a second) */
		if (ms > 1000.0)
			ms = 1000.0;
	}
	printf("\nlogged %d catch-up %ld\n", lines_logged, game_time_client_catch_up_ticks());
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	char model[16];
	char who[16];
	int render, hitch;
	float speed;

	game_time_globals = &globals;
	game_time_initialize_for_new_map();
	globals.active = TRUE;
	globals.speed = 1.0f;
	/* "<connection>:<model>:<render ms>:<hitch every>[:<game speed>]" */
	if (sscanf(case_name, "%15[a-z]:%15[a-z]:%d:%d:%f", who, model, &render, &hitch, &speed) >= 4)
	{
		/* (an MCC map's game speed: more ticks due a frame) */
		if (sscanf(case_name, "%*[a-z]:%*[a-z]:%*d:%*d:%f", &speed) == 1)
			globals.speed = speed;
		connection = !strcmp(who, "client") ? _game_connection_network_client :
			!strcmp(who, "host") ? _game_connection_network_server :
			!strcmp(who, "film") ? _game_connection_film_playback : _game_connection_local;
		trace(model, render, hitch, 3000);
		return 0;
	}
	CASE("held-load")
	{
		/* (a client loading while its clock waits for the host: frames ever
		longer, no tick run; no line, no ticks counted left unrun) */
		int index;

		connection = _game_connection_network_client;
		started = FALSE;
		for (index = 0; index < 10; index++)
			game_time_update((real)(0.1 * (index + 1)));
		CHECK(ticks_run == 0, "%ld ticks run while held", ticks_run);
		CHECK(lines_logged == 0, "%d lines written while held", lines_logged);
		CHECK(game_time_client_catch_up_ticks() == TICKS_PER_SECOND, "catch-up %ld after a held load",
			game_time_client_catch_up_ticks());
		return 0;
	}
	CASE("spiral-capped")
	{
		/* (a client in the spiral runs at most the guard's few once it is
		on, writes its line once, and takes the host's clock sooner) */
		int index;
		double ms = 1000.0 / 30;
		long most = 0;

		connection = _game_connection_network_client;
		for (index = 0; index < 600; index++)
		{
			long before = ticks_run;
			long ran;

			game_time_update((real)(ms / 1000.0));
			ran = ticks_run - before;
			if (index > 60 && ran > most)
				most = ran;
			ms = MIN(ran * 36.0 + 16.0, 1000.0);
		}
		CHECK(most <= GAME_TIME_CLIENT_GUARD_TICKS, "a frame in the spiral ran %ld ticks", most);
		CHECK(ms < 300.0, "a frame in the spiral took %.0f ms", ms);
		CHECK(lines_logged == 1, "%d lines written for one map", lines_logged);
		CHECK(game_time_client_catch_up_ticks() == GAME_TIME_CLIENT_GUARD_TICKS,
			"the host's clock taken %ld ticks ahead", game_time_client_catch_up_ticks());
		/* (the host's clock taken: a second again until it falls behind) */
		game_time_set_distributed(globals.local_time + 40);
		CHECK(game_time_client_catch_up_ticks() == TICKS_PER_SECOND,
			"after the host's clock: %ld", game_time_client_catch_up_ticks());
		/* (a new map writes the line again) */
		game_time_initialize_for_new_map();
		CHECK(game_time_client_catch_up_ticks() == TICKS_PER_SECOND, "a new map: %ld",
			game_time_client_catch_up_ticks());
		return 0;
	}
	CHECK(FALSE, "no such case: %s", case_name);
	return 0;
}
