/*
DETERMINISM_CHECK.C

An input-replay check of the simulation (debug.determinism in config.toml,
HALO_DETERMINISM), for tools/test_determinism.py:

- "record:<file>" writes each game tick's player actions (every player's,
  as players_update_before_game takes them from the update queue) to <file>;
- "replay:<file>" plays a recording's actions back in place of the queue's,
  tick by tick, so the same game runs again without anyone's input.

Either way the game's random seed is debug.determinism_seed (written in the
recording, and taken from it on replay), the menus' ticks are left out (the
first game past the main menu is the one checked), and once a tick has run
a line of hashes of the simulation's state goes to debug.determinism_hashes:

    <tick> <all> rng=<seed> players=<hash> objects=<hash> scores=<hash> count=<objects>

players: each player's unit, team, respawn timer, kills, deaths and score;
objects: each object's index, tag, parent, position, velocities, facing and
vitality (raw bits: the same build on the same machine must match exactly);
scores: the game type's score of each player and whether it can score.

After debug.determinism_ticks ticks (0: never) the game quits; a replay also
quits when its recording runs out. Off by default; nothing of it runs
without the setting. A console's build leaves it out
(HALO_CONSOLE_NO_NETWORK_TEST, halo_port_features.h).

Called from players_update_before_game (players.c), game_load (game.c) and
the tick loop (game_time.c).
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "interface/ui_widget.h"
#include "math/real_math.h"

#ifndef HALO_CONSOLE_NO_NETWORK_TEST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the check's files are the test's, by host path: not the game's Xbox paths
(port/linux/include/stdio.h sends fopen through xbox_files.c, which puts
every path under the data root) */
#ifdef fopen
#undef fopen
#endif

/* the platform layer's (port/linux/src/port_config.c) */
const char *config_string(char const *name);
long config_integer(char const *name);
void platform_log(char const *format, ...);

#define DETERMINISM_MAGIC "HDET1"

enum
{
	_determinism_off,
	_determinism_record,
	_determinism_replay,
};

/* a tick of a recording: its time, whether the queue had an update, and
the actions of the players there were */
struct determinism_tick_header
{
	long tick;
	long dequeued;
	long action_count;
};

struct determinism_action
{
	long player_absolute_index;
	struct player_action action;
};

static struct
{
	boolean read;
	long mode;
	FILE *inputs;
	FILE *hashes;
	unsigned long seed;
	long ticks_limit;
	long ticks_done;
	boolean started;
	boolean finished;
	/* a replay's next tick, read ahead */
	boolean have_next;
	struct determinism_tick_header next;
	struct determinism_action next_actions[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
} determinism_globals;

static void determinism_finish(
	char const *reason)
{
	if (determinism_globals.inputs)
		fclose(determinism_globals.inputs);
	if (determinism_globals.hashes)
		fclose(determinism_globals.hashes);
	determinism_globals.inputs = NULL;
	determinism_globals.hashes = NULL;
	determinism_globals.finished = TRUE;
	platform_log("determinism: %s after %ld ticks", reason, determinism_globals.ticks_done);
	exit(EXIT_SUCCESS);
}

static boolean determinism_read_next(
	void)
{
	FILE *file = determinism_globals.inputs;
	long index;

	determinism_globals.have_next = FALSE;
	if (!file ||
		fread(&determinism_globals.next, sizeof(determinism_globals.next), 1, file) != 1 ||
		determinism_globals.next.action_count < 0 ||
		determinism_globals.next.action_count > HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
	{
		return FALSE;
	}
	for (index = 0; index < determinism_globals.next.action_count; index++)
	{
		if (fread(&determinism_globals.next_actions[index], sizeof(determinism_globals.next_actions[index]), 1, file) != 1)
			return FALSE;
	}
	determinism_globals.have_next = TRUE;
	return TRUE;
}

static void determinism_read_settings(
	void)
{
	char const *setting;
	char const *hashes;

	if (determinism_globals.read)
		return;
	determinism_globals.read = TRUE;
	setting = config_string("debug.determinism");
	if (!setting || !setting[0])
		return;
	determinism_globals.seed = (unsigned long)config_integer("debug.determinism_seed");
	determinism_globals.ticks_limit = config_integer("debug.determinism_ticks");
	if (!strncmp(setting, "record:", 7))
	{
		char header[8] = DETERMINISM_MAGIC;
		unsigned long sizes[2] = { (unsigned long)sizeof(struct player_action), HALO_PORT_MAXIMUM_NETWORK_PLAYERS };

		determinism_globals.inputs = fopen(setting + 7, "wb");
		if (!determinism_globals.inputs)
		{
			platform_log("determinism: can't write %s", setting + 7);
			return;
		}
		fwrite(header, sizeof(header), 1, determinism_globals.inputs);
		fwrite(sizes, sizeof(sizes), 1, determinism_globals.inputs);
		fwrite(&determinism_globals.seed, sizeof(determinism_globals.seed), 1, determinism_globals.inputs);
		determinism_globals.mode = _determinism_record;
	}
	else if (!strncmp(setting, "replay:", 7))
	{
		char header[8];
		unsigned long sizes[2];

		determinism_globals.inputs = fopen(setting + 7, "rb");
		if (!determinism_globals.inputs ||
			fread(header, sizeof(header), 1, determinism_globals.inputs) != 1 ||
			strncmp(header, DETERMINISM_MAGIC, sizeof(header)) ||
			fread(sizes, sizeof(sizes), 1, determinism_globals.inputs) != 1 ||
			sizes[0] != sizeof(struct player_action) ||
			sizes[1] != HALO_PORT_MAXIMUM_NETWORK_PLAYERS ||
			fread(&determinism_globals.seed, sizeof(determinism_globals.seed), 1, determinism_globals.inputs) != 1)
		{
			platform_log("determinism: %s is not a recording of this build", setting + 7);
			if (determinism_globals.inputs)
				fclose(determinism_globals.inputs);
			determinism_globals.inputs = NULL;
			return;
		}
		determinism_globals.mode = _determinism_replay;
		determinism_read_next();
	}
	else
	{
		platform_log("determinism: unknown setting \"%s\"", setting);
		return;
	}
	hashes = config_string("debug.determinism_hashes");
	if (hashes && hashes[0])
	{
		determinism_globals.hashes = fopen(hashes, "w");
		if (!determinism_globals.hashes)
			platform_log("determinism: can't write %s", hashes);
	}
	platform_log("determinism: %s, seed %lu", determinism_globals.mode == _determinism_record ? "recording" : "replaying",
		determinism_globals.seed);
}

static boolean determinism_active(
	void)
{
	determinism_read_settings();
	return determinism_globals.mode != _determinism_off && !determinism_globals.finished;
}

/* game_load: every game of the check plays from the same seed */
void determinism_check_game_load(
	unsigned long *random_seed)
{
	if (determinism_active())
		*random_seed = determinism_globals.seed;
}

/* players_update_before_game: the tick's actions recorded, or the
recording's put in their place; returns whether the tick has actions */
boolean determinism_check_actions(
	boolean dequeued,
	struct player_action *actions)
{
	long tick;

	if (!determinism_active() || main_menu_is_active())
		return dequeued;
	determinism_globals.started = TRUE;
	tick = game_time_get();
	if (determinism_globals.mode == _determinism_record)
	{
		struct determinism_tick_header header;
		struct determinism_action recorded[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
		struct data_iterator iterator;
		struct player_datum *player;

		header.tick = tick;
		header.dequeued = dequeued ? 1 : 0;
		header.action_count = 0;
		if (dequeued)
		{
			data_iterator_new(&iterator, player_data);
			while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			{
				long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

				if (absolute_index < 0 || absolute_index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
					continue;
				recorded[header.action_count].player_absolute_index = absolute_index;
				recorded[header.action_count].action = actions[absolute_index];
				header.action_count++;
			}
		}
		fwrite(&header, sizeof(header), 1, determinism_globals.inputs);
		fwrite(recorded, sizeof(recorded[0]), (size_t)header.action_count, determinism_globals.inputs);
		return dequeued;
	}

	/* a replay: the recording's tick, which must be this one */
	if (!determinism_globals.have_next)
	{
		determinism_finish("the recording ran out");
		return dequeued;
	}
	if (determinism_globals.next.tick != tick)
	{
		platform_log("determinism: the recording's tick %ld is not the game's %ld", determinism_globals.next.tick, tick);
		if (determinism_globals.hashes)
			fprintf(determinism_globals.hashes, "# tick mismatch: recording %ld, game %ld\n", determinism_globals.next.tick, tick);
		determinism_finish("stopped");
		return dequeued;
	}
	{
		long index;

		for (index = 0; index < determinism_globals.next.action_count; index++)
		{
			long absolute_index = determinism_globals.next_actions[index].player_absolute_index;

			if (absolute_index >= 0 && absolute_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
				actions[absolute_index] = determinism_globals.next_actions[index].action;
		}
		dequeued = determinism_globals.next.dequeued ? TRUE : FALSE;
	}
	determinism_read_next();
	return dequeued;
}

/* FNV-1a, 64 bits */
static unsigned long long determinism_hash(
	unsigned long long hash,
	void const *data,
	size_t size)
{
	unsigned char const *bytes = (unsigned char const *)data;
	size_t index;

	for (index = 0; index < size; index++)
	{
		hash ^= bytes[index];
		hash *= 0x100000001B3ULL;
	}
	return hash;
}

#define DETERMINISM_HASH_START 0xCBF29CE484222325ULL
#define HASH_FIELD(hash, field) ((hash) = determinism_hash((hash), &(field), sizeof(field)))

/* the tick loop (game_time.c), after each tick */
void determinism_check_tick_end(
	void)
{
	unsigned long long players_hash = DETERMINISM_HASH_START;
	unsigned long long objects_hash = DETERMINISM_HASH_START;
	unsigned long long scores_hash = DETERMINISM_HASH_START;
	unsigned long long all_hash;
	unsigned long seed;
	long object_count = 0;
	long tick;

	if (!determinism_active() || !determinism_globals.started || main_menu_is_active())
		return;
	/* (the tick just run: game_time_get has moved past it) */
	tick = game_time_get() - 1;
	seed = get_random_seed();
	{
		struct data_iterator iterator;
		struct player_datum *player;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			long score = game_engine_running() && game_engine && game_engine->get_player_score ?
				game_engine->get_player_score(iterator.datum_index, _get_score_individual) : 0;

			HASH_FIELD(players_hash, iterator.datum_index);
			HASH_FIELD(players_hash, player->unit_index);
			HASH_FIELD(players_hash, player->team_index);
			HASH_FIELD(players_hash, player->respawn_timer);
			HASH_FIELD(players_hash, player->statistics.kills[0]);
			HASH_FIELD(players_hash, player->statistics.deaths);
			HASH_FIELD(scores_hash, iterator.datum_index);
			HASH_FIELD(scores_hash, score);
		}
	}
	{
		boolean can_score = game_engine_running() ? game_engine_can_score() : FALSE;

		HASH_FIELD(scores_hash, can_score);
	}
	{
		struct object_iterator objects;

		object_iterator_new(&objects, _object_mask_all, 0);
		while (object_iterator_next(&objects))
		{
			struct object_datum *object = object_get(objects.index);

			HASH_FIELD(objects_hash, objects.index);
			HASH_FIELD(objects_hash, object->definition_index);
			HASH_FIELD(objects_hash, object->object.parent_object_index);
			HASH_FIELD(objects_hash, object->object.position);
			HASH_FIELD(objects_hash, object->object.translational_velocity);
			HASH_FIELD(objects_hash, object->object.angular_velocity);
			HASH_FIELD(objects_hash, object->object.forward);
			HASH_FIELD(objects_hash, object->object.up);
			HASH_FIELD(objects_hash, object->object.body_vitality);
			HASH_FIELD(objects_hash, object->object.shield_vitality);
			object_count++;
		}
	}
	all_hash = DETERMINISM_HASH_START;
	HASH_FIELD(all_hash, seed);
	HASH_FIELD(all_hash, players_hash);
	HASH_FIELD(all_hash, objects_hash);
	HASH_FIELD(all_hash, scores_hash);
	if (determinism_globals.hashes)
	{
		fprintf(determinism_globals.hashes, "%ld %016llx rng=%08lx players=%016llx objects=%016llx scores=%016llx count=%ld\n",
			tick, all_hash, seed, players_hash, objects_hash, scores_hash, object_count);
	}
	determinism_globals.ticks_done++;
	if (determinism_globals.ticks_limit > 0 && determinism_globals.ticks_done >= determinism_globals.ticks_limit)
		determinism_finish("done");
}

#else

void determinism_check_game_load(
	unsigned long *random_seed)
{
	(void)random_seed;
}

boolean determinism_check_actions(
	boolean dequeued,
	struct player_action *actions)
{
	(void)actions;
	return dequeued;
}

void determinism_check_tick_end(
	void)
{
}

#endif
