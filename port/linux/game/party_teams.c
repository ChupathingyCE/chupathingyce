/*
PARTY_TEAMS.C

Keeping a party on one team (docs/delta.md, Party teams): the Guide's
party members each tell the host their party's tag over Delta Peer
(PARTY_TAG, the party_teams capability), and a host that keeps parties
together (network.keep_parties_together, on for a player's host; a dedicated
server's keep_parties_together, off unless its admin turns it on) puts a
split party on one team, where the teams stay within one player of each
other (party_teams_plan.c says which change).

- **In the lobby**, each time the lobby's players or their parties change,
  the host moves a member and sends the lobby again. Players still choose
  their own team afterwards; the host does not undo it until the lobby
  changes again.
- **In a game**, only a member who joined late and has not spawned yet is
  moved (the game's own team change: team_change_party_move), never a
  player who is playing.

A tag is the client's word, so it moves nobody else: only party members
are ever moved, never a player in no party (no trades); a tag one machine
alone has is no party (its own players are not a party of their own); and
Delta Peer keeps a machine's new tag only a while after its last change
(DELTA_PEER_PARTY_TAG_HOLD), so flipping it can't keep a lobby rearranging.

Teams stay the host's: an OpenCE host, or one that says nothing, places a
party as it always did, and nothing here changes what the game protocol
carries. A tag names no one; the host learns only which machines share one.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "network_coop.h"
#include "team_change.h"
#include "party_teams.h"

#ifdef HALO_GAME_BROWSER
#ifndef HALO_CONSOLE_NO_GUIDE
#include "../src/delta_wire.h"
#include "../src/delta_peer.h"
#include "../src/party_teams_plan.h"
#endif
#endif

#include <string.h>

/* the platform layer's (port/linux/src/port_config.c) */
int config_boolean(char const *name);
void platform_log(char const *format, ...);
/* (network_server_manager_internal.h's) */
struct network_game *network_game_server_get_game(struct network_game_server *server);

enum
{
	PARTY_TEAMS_PLAYERS = HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
	/* a game's late joiners looked at this often (milliseconds) */
	PARTY_TEAMS_INTERVAL = 500,
	/* the most changes one look makes */
	PARTY_TEAMS_CHANGES = 8,
};

static struct
{
	/* the host's choice, set by a dedicated server (else
	network.keep_parties_together's) */
	boolean set;
	boolean keep;
	/* the lobby's players and parties at the last arrangement */
	unsigned long lobby_signature;
	/* a game's players who have spawned (by absolute index), and the game
	they are of */
	boolean in_game;
	boolean spawned[PARTY_TEAMS_PLAYERS];
	unsigned long last_look;
} party_teams;

void party_teams_host_set(boolean keep)
{
	party_teams.set = TRUE;
	party_teams.keep = keep ? TRUE : FALSE;
}

boolean party_teams_host_on(void)
{
	if (party_teams.set)
		return party_teams.keep;
	return config_boolean("network.keep_parties_together") ? TRUE : FALSE;
}

#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_GUIDE)

/* the parties' numbers from their machines' tags: each machine's (-1: in no
party), machines that share a tag sharing a number; a tag no other machine
shares is no party (one machine's own players aren't one) */
static void machine_parties(int *parties)
{
	unsigned char tags[DELTA_PEER_MAXIMUM_MACHINES][DELTA_WIRE_PARTY_TAG_SIZE];
	int sharing[DELTA_PEER_MAXIMUM_MACHINES];
	int machine, earlier;

	for (machine = 0; machine < DELTA_PEER_MAXIMUM_MACHINES; machine++)
	{
		parties[machine] = -1;
		if (!delta_peer_game_machine_party_tag(machine, tags[machine]))
			continue;
		parties[machine] = machine;
		for (earlier = 0; earlier < machine; earlier++)
		{
			if (parties[earlier] >= 0 && !memcmp(tags[earlier], tags[machine], DELTA_WIRE_PARTY_TAG_SIZE))
			{
				parties[machine] = parties[earlier];
				break;
			}
		}
	}
	memset(sharing, 0, sizeof(sharing));
	for (machine = 0; machine < DELTA_PEER_MAXIMUM_MACHINES; machine++)
	{
		if (parties[machine] >= 0)
			sharing[parties[machine]]++;
	}
	for (machine = 0; machine < DELTA_PEER_MAXIMUM_MACHINES; machine++)
	{
		if (parties[machine] >= 0 && sharing[parties[machine]] < 2)
			parties[machine] = -1;
	}
}

static int party_of(int const *parties, long machine_index)
{
	return machine_index >= 0 && machine_index < DELTA_PEER_MAXIMUM_MACHINES ? parties[machine_index] : -1;
}

static boolean team_game(struct network_game const *game)
{
	return game && game->variant.universal_variant.teams && !network_coop_active();
}

boolean party_teams_pregame(struct network_game *game)
{
	struct party_teams_player players[PARTY_TEAMS_PLAYERS];
	long list[PARTY_TEAMS_PLAYERS];
	int parties[DELTA_PEER_MAXIMUM_MACHINES];
	struct party_teams_change change;
	unsigned long signature = 2166136261UL;
	int count = 0, changes = 0, index;

	if (!global_network_game_server_get() || !team_game(game) || !party_teams_host_on())
		return FALSE;
	machine_parties(parties);
	for (index = 0; index < PARTY_TEAMS_PLAYERS; index++)
	{
		struct network_player *player = &game->players[index];

		if (!network_player_is_valid(player))
			continue;
		list[count] = index;
		players[count].team = player->team_index;
		players[count].party = party_of(parties, player->machine_index);
		players[count].movable = TRUE;
		signature = (signature ^ (unsigned long)(index * 131 + player->machine_index)) * 16777619UL;
		signature = (signature ^ (unsigned long)(players[count].party + 1)) * 16777619UL;
		count++;
	}
	/* (only when the lobby's players or parties changed: a player's own
	choice of team afterwards stands) */
	if (signature == party_teams.lobby_signature)
		return FALSE;
	party_teams.lobby_signature = signature;
	while (changes < PARTY_TEAMS_CHANGES && party_teams_plan(players, count, &change))
	{
		game->players[list[change.player]].team_index = (char)change.team;
		players[change.player].team = change.team;
		changes++;
	}
	if (changes)
		platform_log("party teams: the lobby's parties put together (%d change%s)", changes, changes == 1 ? "" : "s");
	return changes > 0;
}

void party_teams_update(void)
{
	struct party_teams_player players[PARTY_TEAMS_PLAYERS];
	long indexes[PARTY_TEAMS_PLAYERS];
	int parties[DELTA_PEER_MAXIMUM_MACHINES];
	struct party_teams_change change;
	struct network_game_server *server = global_network_game_server_get();
	struct data_iterator iterator;
	struct player_datum *player;
	boolean in_game = game_engine_running() && player_data != NULL && server != NULL;
	unsigned long now = system_milliseconds();
	int count = 0, changes = 0;

	/* (a game's players are its own; a new lobby arranges afresh) */
	if (in_game != party_teams.in_game)
	{
		party_teams.in_game = in_game;
		memset(party_teams.spawned, 0, sizeof(party_teams.spawned));
		if (!in_game)
			party_teams.lobby_signature = 0;
	}
	if (!in_game)
		return;
	/* (who has spawned: never moved) */
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

		if (absolute >= 0 && absolute < PARTY_TEAMS_PLAYERS && player->unit_index != NONE)
			party_teams.spawned[absolute] = TRUE;
	}
	if (now - party_teams.last_look < PARTY_TEAMS_INTERVAL || !game_engine_has_teams() || network_coop_active() ||
		!party_teams_host_on())
	{
		return;
	}
	party_teams.last_look = now;
	machine_parties(parties);
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL && count < PARTY_TEAMS_PLAYERS)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

		if (player->quit_out_of_game)
			continue;
		indexes[count] = iterator.datum_index;
		players[count].team = (int)player->team_index;
		players[count].party = party_of(parties, player->network_player_data.machine_index);
		/* (a late joiner not spawned yet: in a party, so it may move) */
		players[count].movable = players[count].party >= 0 && absolute >= 0 && absolute < PARTY_TEAMS_PLAYERS &&
			!party_teams.spawned[absolute];
		count++;
	}
	while (changes < PARTY_TEAMS_CHANGES && party_teams_plan(players, count, &change))
	{
		if (!team_change_party_move(indexes[change.player], (short)change.team))
			break;
		players[change.player].team = change.team;
		changes++;
	}
}

#else

boolean party_teams_pregame(struct network_game *game)
{
	(void)game;
	return FALSE;
}

void party_teams_update(void)
{
}

#endif
