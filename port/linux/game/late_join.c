/*
LATE_JOIN.C

Late joiners waiting for the next game (docs/delta.md, Host rules). A host
option, off by default: Halo spawns a player who joins a game in progress
at once. With it on (a player's host's network.late_join_hold, a dedicated
server's late_join_hold), they wait until the next game starts and watch
through the follow camera (follow_camera.c) until then.

- The host holds the spawn (late_join_holds, in
  game_engine_should_spawn_player), so it holds for every joiner, an OpenCE
  player's too, and nobody else's game changes: the held player has no unit,
  as a player waiting to respawn has none. A joiner spawns at once when no
  one else is playing, and the next game's players are all new.
- A Chupathingy client knows its host's rule from HOST_RULES
  (host_rules_late_join_hold), so its waiting player watches a living one
  whatever their own display.follow_camera says, with a line saying so. An
  OpenCE player held is told once in their chat (the host's notice).
- A hold never lasts more than LATE_JOIN_HOLD_MOST (a game nobody ends, an
  idle player alone in it), and a player who was in this game, dropped and
  came back (the same name) is never held.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"

#include "halo_port_features.h"
#include "halo_port_limits.h"
#include "host_rules.h"
#include "late_join.h"
#include "chat.h"

#ifdef HALO_GAME_BROWSER

/* ---------- constants */

enum
{
	/* a player made this long into a game joined it in progress (the
	game's own players are made as it starts) */
	LATE_JOIN_GRACE_TICKS = 5 * TICKS_PER_SECOND,
	/* a local player with no unit and no death for this long, past the
	grace, is waiting (a joiner the host lets in spawns before it) */
	LATE_JOIN_WAITING_TICKS = 2 * TICKS_PER_SECOND,
	/* the longest a hold lasts: then the player spawns, as Halo would */
	LATE_JOIN_HOLD_MOST = 5 * 60 * TICKS_PER_SECOND,
	/* the names of the players this game has had, remembered (a player back
	after dropping is not a late joiner) */
	LATE_JOIN_NAMES_KEPT = 2 * HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
	LATE_JOIN_NAME_LENGTH = 12,
};

/* ---------- globals */

static struct
{
	/* (the host's) each player held, by absolute index: their datum index,
	so a new game's player in the same slot is not */
	long held[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	/* (the host's) when each was held (game time), and whether an OpenCE
	player held was told so */
	long held_at[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	boolean told[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	/* (the host's) the names of this game's players so far, and the game
	time last seen (it goes back: a new game) */
	wchar_t names[LATE_JOIN_NAMES_KEPT][LATE_JOIN_NAME_LENGTH];
	long name_count;
	long names_time;
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

/* (the host's) whether a name was this game's player's already; then it
is (a new game forgets them) */
static boolean late_join_name_seen(
	wchar_t const *name)
{
	long now = game_time_get();
	long index;

	if (now < late_join.names_time)
		late_join.name_count = 0;
	late_join.names_time = now;
	if (!name[0])
		return FALSE;
	for (index = 0; index < late_join.name_count; index++)
	{
		if (!csmemcmp(late_join.names[index], name, sizeof(late_join.names[index])))
			return TRUE;
	}
	if (late_join.name_count < LATE_JOIN_NAMES_KEPT)
		csmemcpy(late_join.names[late_join.name_count++], name, sizeof(late_join.names[0]));
	return FALSE;
}

void late_join_player_added(
	long player_index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	struct player_datum *player;
	wchar_t name[LATE_JOIN_NAME_LENGTH];
	boolean seen;

	late_join_clear();
	if (player_index == NONE || absolute < 0 || absolute >= (long)NUMBEROF(late_join.held))
		return;
	/* (a slot's earlier player is forgotten whatever this one is) */
	late_join.held[absolute] = NONE;
	late_join.told[absolute] = FALSE;
	if (global_network_game_server_get() == NULL || !game_engine_running() || game_engine_showing_postgame())
		return;
	player = player_get(player_index);
	csmemset(name, 0, sizeof(name));
	csmemcpy(name, player->name, MIN(sizeof(name), sizeof(player->name)));
	name[LATE_JOIN_NAME_LENGTH - 1] = 0;
	seen = late_join_name_seen(name);
	/* (a game's own players, and one back after dropping out of it, play) */
	if (game_time_get() < LATE_JOIN_GRACE_TICKS || seen)
		return;
	late_join.held[absolute] = player_index;
	late_join.held_at[absolute] = game_time_get();
}

boolean late_join_holds(
	long player_index)
{
	struct player_datum *player;
	long absolute;

	if (!late_join_marked(player_index) || global_network_game_server_get() == NULL ||
		!host_rules_host_late_join_hold())
	{
		return FALSE;
	}
	player = player_get(player_index);
	absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	/* (once spawned, never held again: a death waits the usual way; and
	never held longer than LATE_JOIN_HOLD_MOST, nor alone) */
	if (player->statistics.deaths > 0 || !late_join_others_playing(player_index) ||
		game_time_get() - late_join.held_at[absolute] >= LATE_JOIN_HOLD_MOST ||
		game_time_get() < late_join.held_at[absolute])
	{
		late_join.held[absolute] = NONE;
		return FALSE;
	}
	/* (an OpenCE player sees Halo's own death view: told once in their
	chat why; a Chupathingy player's game says so itself) */
	if (!late_join.told[absolute])
	{
		late_join.told[absolute] = TRUE;
#ifdef HALO_PORT_CHAT
		chat_host_notice_to_machine((short)player->network_player_data.machine_index,
			"You joined a game in progress: you play from the next game.");
#endif
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
		!host_rules_late_join_hold())
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

#endif
