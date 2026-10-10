/*
BOT_STATS.C

How a game of bots went, counted as it is played, for comparing two
builds over a batch of games (tools/bot_games.py). Off unless
HALO_BOT_STATS is set: then every HALO_BOT_STATS seconds of game time (60
when it is not a number) one line of the game so far goes to the log,

	bot stats: map=bloodgulch t=600 bots=12 ...

each figure name=value, every one counted from the start of the game (the
last line of a game is its whole). The figures:

	t               seconds of game time
	bots            bots in the game
	bot_seconds     seconds the bots were alive, all of them together
	caps0, caps1    each team's score in a game of flags (captures)
	pickups         times a flag was taken up (from its stand or the ground)
	drops           times a carried flag fell (its carrier killed, or out of a seat)
	returns         times a flag went home without being captured
	carrier_rides   times a bot got into a vehicle with the flag
	kills, deaths, suicides
	                the bots' own, from the game's statistics
	stuck           times a bot gave up on a way it was stuck on
	route_failed    times a way between two areas was found wanting
	idle            per cent of bot_seconds on foot, standing still, with
	                nobody to shoot and not holding a place on purpose
	driving, gunning, riding
	                per cent of bot_seconds at a wheel, on a gun, in another seat
	escort          per cent of bot_seconds escorting one of ours with the flag
	with_people     per cent of bot_seconds helping a person of their side
	                (on their gun, beside them, following them in)
*/

#include "bot_internal.h"

#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "main/main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);

boolean ctf_bot_flag(long team_index, long *weapon_index, real_point3d *base); /* game_engine_ctf.c */
long ctf_bot_score(long team_index); /* game_engine_ctf.c */
extern struct game_engine ctf_engine;

static struct
{
	int read; /* HALO_BOT_STATS read */
	long every; /* ticks between lines (0: off) */
	long first_tick; /* the game's first tick counted (NONE: none yet) */
	long last_tick;
	long counts[NUMBER_OF_BOT_STATS];
	long alive_ticks;
	long idle_ticks;
	long seat_ticks[4]; /* on foot, driving, gunning, riding */
	long escort_ticks;
	long with_people_ticks;
	/* each flag: who held it last tick (NONE nobody), and whether it was home */
	long flag_holder[2];
	int flag_home[2];
	long scores[2];
	/* where each bot was last tick */
	real_point3d last_position[BOT_MAXIMUM_PLAYERS];
	long last_seen[BOT_MAXIMUM_PLAYERS];
} bot_stats = { 0, 0, NONE };

int bot_stats_enabled(void)
{
	if (!bot_stats.read)
	{
		char const *every = getenv("HALO_BOT_STATS");

		bot_stats.read = TRUE;
		bot_stats.every = every ? (atol(every) > 0 ? atol(every) : 60) * BOT_TICKS_PER_SECOND : 0;
	}
	return bot_stats.every > 0;
}

void bot_stats_reset(void)
{
	int read = bot_stats.read;
	long every = bot_stats.every;

	memset(&bot_stats, 0, sizeof(bot_stats));
	bot_stats.read = read;
	bot_stats.every = every;
	bot_stats.first_tick = NONE;
	bot_stats.flag_holder[0] = bot_stats.flag_holder[1] = NONE;
	bot_stats.flag_home[0] = bot_stats.flag_home[1] = TRUE;
}

void bot_stats_note(int stat)
{
	if (bot_stats_enabled() && stat >= 0 && stat < NUMBER_OF_BOT_STATS)
		bot_stats.counts[stat]++;
}

void bot_stats_bot_tick(long index, real_point3d const *position, int seat_kind, int busy, int escorting, int with_people)
{
	long now = game_time_get();

	if (!bot_stats_enabled() || index < 0 || index >= BOT_MAXIMUM_PLAYERS)
		return;
	bot_stats.alive_ticks++;
	bot_stats.seat_ticks[seat_kind >= 0 && seat_kind < 4 ? seat_kind : 0]++;
	if (escorting)
		bot_stats.escort_ticks++;
	if (with_people)
		bot_stats.with_people_ticks++;
	/* (standing: less than a tenth of a walk's pace since the tick before) */
	if (seat_kind == 0 && !busy && bot_stats.last_seen[index] == now - 1 &&
		distance3d(position, &bot_stats.last_position[index]) < 0.02f)
	{
		bot_stats.idle_ticks++;
	}
	bot_stats.last_position[index] = *position;
	bot_stats.last_seen[index] = now;
}

/* the flags since the tick before: taken, dropped, home again */
static void bot_stats_flags(void)
{
	long scores[2];
	long team;

	if (game_engine != &ctf_engine)
		return;
	scores[0] = ctf_bot_score(0);
	scores[1] = ctf_bot_score(1);
	for (team = 0; team < 2; team++)
	{
		long flag = NONE;
		real_point3d base;
		struct object_datum *object;
		long holder;
		int home;

		if (!ctf_bot_flag(team, &flag, &base) || flag == NONE || !(object = object_try_and_get(flag)))
			continue;
		holder = object->object.parent_object_index != NONE ? object_get_ultimate_parent(flag) : NONE;
		home = holder == NONE && distance3d(&object->object.position, &base) < 1.f;
		if (holder != NONE && bot_stats.flag_holder[team] == NONE)
			bot_stats.counts[_bot_stat_flag_pickup]++;
		else if (holder == NONE && bot_stats.flag_holder[team] != NONE && !home)
			bot_stats.counts[_bot_stat_flag_drop]++;
		/* (home again, and not because the other side scored with it) */
		if (home && !bot_stats.flag_home[team] && scores[1 - team] == bot_stats.scores[1 - team])
			bot_stats.counts[_bot_stat_flag_return]++;
		bot_stats.flag_holder[team] = holder;
		bot_stats.flag_home[team] = home;
	}
	bot_stats.scores[0] = scores[0];
	bot_stats.scores[1] = scores[1];
}

static real bot_stats_share(long ticks)
{
	return bot_stats.alive_ticks > 0 ? 100.f * (real)ticks / (real)bot_stats.alive_ticks : 0.f;
}

static void bot_stats_report(long now)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long bots = 0, kills = 0, deaths = 0, suicides = 0;
	char const *map = main_get_multiplayer_map_name();
	char const *name = map ? strrchr(map, '\\') : NULL;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (!bot_player_is_bot(iterator.datum_index))
			continue;
		bots++;
		kills += player->statistics.kills[0];
		deaths += player->statistics.deaths;
		suicides += player->statistics.suicides;
	}
	platform_log("bot stats: map=%s t=%ld bots=%ld bot_seconds=%ld caps0=%ld caps1=%ld pickups=%ld drops=%ld returns=%ld "
		"carrier_rides=%ld kills=%ld deaths=%ld suicides=%ld stuck=%ld route_failed=%ld "
		"idle=%.1f driving=%.1f gunning=%.1f riding=%.1f escort=%.1f with_people=%.1f",
		name ? name + 1 : map ? map : "?", (now - bot_stats.first_tick) / BOT_TICKS_PER_SECOND, bots,
		bot_stats.alive_ticks / BOT_TICKS_PER_SECOND,
		game_engine == &ctf_engine ? ctf_bot_score(0) : 0L, game_engine == &ctf_engine ? ctf_bot_score(1) : 0L,
		bot_stats.counts[_bot_stat_flag_pickup], bot_stats.counts[_bot_stat_flag_drop], bot_stats.counts[_bot_stat_flag_return],
		bot_stats.counts[_bot_stat_carrier_ride], kills, deaths, suicides,
		bot_stats.counts[_bot_stat_stuck], bot_stats.counts[_bot_stat_route_failed],
		bot_stats_share(bot_stats.idle_ticks), bot_stats_share(bot_stats.seat_ticks[1]), bot_stats_share(bot_stats.seat_ticks[2]),
		bot_stats_share(bot_stats.seat_ticks[3]), bot_stats_share(bot_stats.escort_ticks), bot_stats_share(bot_stats.with_people_ticks));
}

void bot_stats_world_tick(long now)
{
	if (!bot_stats_enabled())
		return;
	if (bot_stats.first_tick == NONE || now < bot_stats.last_tick)
	{
		bot_stats_reset();
		bot_stats.first_tick = now;
	}
	bot_stats.last_tick = now;
	bot_stats_flags();
	if (now > bot_stats.first_tick && (now - bot_stats.first_tick) % bot_stats.every == 0)
		bot_stats_report(now);
}
