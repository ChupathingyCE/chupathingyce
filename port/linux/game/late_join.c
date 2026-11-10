/*
LATE_JOIN.C

Late joiners waiting for the next game (docs/delta.md, Host rules). A host
option, off by default: Halo spawns a player who joins a game in progress
at once. With it on (a player's host's network.late_join_wait, a dedicated
server's late_join_wait), they wait until the next game starts and watch
through the follow camera (follow_camera.c) until then.

- The host holds the spawn (late_join_holds, in
  game_engine_should_spawn_player), so it holds for every joiner, an OpenCE
  player's too, and nobody else's game changes: the held player has no unit,
  as a player waiting to respawn has none. A joiner spawns at once when no
  one else is playing, and the next game's players are all new.
- A Chupathingy client knows its host's rule from HOST_RULES
  (host_rules_late_join_wait), so its waiting player watches a living one
  whatever their own display.follow_camera says, with a line saying so.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"

#include "halo_port_limits.h"
#include "host_rules.h"
#include "late_join.h"

/* ---------- constants */

enum
{
	/* a player made this long into a game joined it in progress (the
	game's own players are made as it starts) */
	LATE_JOIN_GRACE_TICKS = 5 * TICKS_PER_SECOND,
	/* a local player with no unit and no death for this long, past the
	grace, is waiting (a joiner the host lets in spawns before it) */
	LATE_JOIN_WAITING_TICKS = 2 * TICKS_PER_SECOND,
};

/* ---------- globals */

static struct
{
	/* (the host's) each player held, by absolute index: their datum index,
	so a new game's player in the same slot is not */
	long held[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	boolean cleared;
	/* (this machine's) when each local player was first seen with no unit
	and no death, NONE if not */
	long unspawned_since[MAXIMUM_LOCAL_PLAYERS];
	long unspawned_player[MAXIMUM_LOCAL_PLAYERS];
} late_join;

/* ---------- private code */

static void late_join_clear(
	void)
{
	long index;

	if (late_join.cleared)
		return;
	for (index = 0; index < (long)NUMBEROF(late_join.held); index++)
		late_join.held[index] = NONE;
	for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
	{
		late_join.unspawned_since[index] = NONE;
		late_join.unspawned_player[index] = NONE;
	}
	late_join.cleared = TRUE;
}

static boolean late_join_marked(
	long player_index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

	late_join_clear();
	return absolute >= 0 && absolute < (long)NUMBEROF(late_join.held) && late_join.held[absolute] == player_index;
}

/* whether anyone not held plays this game (a player in it, not quit) */
static boolean late_join_others_playing(
	long self)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (iterator.datum_index != self && !player->quit_out_of_game && !late_join_marked(iterator.datum_index))
			return TRUE;
	}
	return FALSE;
}

/* ---------- public code */

void late_join_player_added(
	long player_index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

	late_join_clear();
	if (player_index == NONE || absolute < 0 || absolute >= (long)NUMBEROF(late_join.held))
		return;
	/* (a slot's earlier player is forgotten whatever this one is) */
	late_join.held[absolute] = NONE;
	if (global_network_game_server_get() == NULL || !game_engine_running() ||
		game_time_get() < LATE_JOIN_GRACE_TICKS || game_engine_showing_postgame())
	{
		return;
	}
	late_join.held[absolute] = player_index;
}

boolean late_join_holds(
	long player_index)
{
	struct player_datum *player;

	if (!late_join_marked(player_index) || global_network_game_server_get() == NULL ||
		!host_rules_host_late_join_wait())
	{
		return FALSE;
	}
	player = player_get(player_index);
	/* (once spawned, never held again: a death waits the usual way) */
	if (player->statistics.deaths > 0 || !late_join_others_playing(player_index))
	{
		late_join.held[DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index)] = NONE;
		return FALSE;
	}
	return TRUE;
}

boolean late_join_waiting(
	long player_index)
{
	struct player_datum *player;
	short local;
	long now;

	late_join_clear();
	if (player_index == NONE || !game_engine_running() || game_engine_showing_postgame())
		return FALSE;
	player = player_try_and_get(player_index);
	if (!player)
		return FALSE;
	local = player->local_player_index;
	if (local < 0 || local >= MAXIMUM_LOCAL_PLAYERS)
		return FALSE;
	now = game_time_get();
	if (player->unit_index != NONE || player->statistics.deaths > 0 || now < LATE_JOIN_GRACE_TICKS ||
		!host_rules_late_join_wait())
	{
		late_join.unspawned_since[local] = NONE;
		return FALSE;
	}
	if (late_join.unspawned_since[local] == NONE || late_join.unspawned_player[local] != player_index ||
		late_join.unspawned_since[local] > now)
	{
		late_join.unspawned_since[local] = now;
		late_join.unspawned_player[local] = player_index;
	}
	return now - late_join.unspawned_since[local] >= LATE_JOIN_WAITING_TICKS;
}
