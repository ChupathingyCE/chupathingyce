/*
TEAM_CHANGE.C

Changing teams during a team game (docs/delta.md, Team changes): Halo PC's
in-game CHANGE TEAMS. The Xbox let a player choose a team only in the
pregame lobby.

- **The host decides.** A player asks (the pause menu's CHANGE TEAMS): on
  the host, its own rule says at once; on a client, the ask goes to the host
  over Delta Peer (TEAM_REQUEST), and the host answers (TEAM_RESULT). The
  host's rule (team_change_host_set; a player's host reads
  network.team_change): off, balanced (the default: no change that leaves
  one team more than the margin, 1 unless set, bigger than the other) or
  allow; and one change every TEAM_CHANGE_COOLDOWN seconds a player. A
  dedicated server's admin moves anyone (sv_team: team_change_move).
- **Through the game, as Halo PC did.** The host kills the player's unit at
  once, as nobody's kill: no death, suicide or betrayal is counted and no
  respawn penalty is added; their score stays theirs, and the team scores
  stay as they are. The host puts the player on the new team (the player,
  its network record and the host's game record, as the game type's auto
  team balance does: game_engine_player_killed), and they respawn on it
  after the game type's respawn time. Every machine, OpenCE's too, learns
  the new team from the unit the host spawns them with
  (network_player_attach_unit): the game protocol is unchanged, and other
  machines see only a death and a respawn.
- **Round-based game types** (Bomb, Grifball, Open Pitch:
  team_change_waits_for_respawn) let the round play out: the change is made
  at the player's next death, or as the next round starts (the host kills
  the unit then, as nobody's kill, and they start the round on the new
  team's end), whichever comes first. A carrier keeps the bomb for their own
  team until then: their death drops it, and a new round takes it back
  first.

A player already dead (waiting to respawn) changes at once.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "units/units.h"
#include "network_coop.h"
#include "../src/delta_wire.h"
#ifdef HALO_GAME_BROWSER
#include "../src/delta_peer.h"
#include "chat.h"
#endif

#include "team_change.h"

#include <stdio.h>
#include <string.h>

/* the platform layer's (port/linux/src/port_config.c) */
int config_boolean(char const *name);
char const *config_string(char const *name);
long config_integer(char const *name);
void platform_log(char const *format, ...);
/* (network_server_manager_internal.h's) */
struct network_game *network_game_server_get_game(struct network_game_server *server);

enum
{
	/* a player's changes at least this many seconds apart (an admin's moves
	aside) */
	TEAM_CHANGE_COOLDOWN = 10,
	/* the most the margin may be */
	TEAM_CHANGE_MAXIMUM_MARGIN = 16,
	TEAM_CHANGE_PLAYERS = HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
};

/* a player's change, by the player's absolute index */
struct team_change_player
{
	/* a change waits (for the unit the host killed, or for the player's
	next death in a round-based game type), to this team */
	boolean waiting;
	short team;
	/* the host killed the player's unit for it: its death is the change's */
	boolean killing;
	/* ... as a round started: the player respawns at once */
	boolean round_start;
	/* the game time of the last change (NONE: none yet) */
	long last_time;
};

static struct
{
	/* the host's rule, set by a dedicated server (else network.team_change's) */
	boolean set;
	int policy;
	int margin;
	struct team_change_player players[TEAM_CHANGE_PLAYERS];
	/* the game the changes are of (its start's game time, to start afresh) */
	boolean in_game;
	/* the client: an ask sent, and the host's results seen */
	boolean asked;
	unsigned int results_seen;
	boolean team_host_set;
} team_change;

/* ---------- the rule */

static boolean same_word(char const *a, char const *b)
{
	for (;; a++, b++)
	{
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
		char y = *b >= 'A' && *b <= 'Z' ? (char)(*b - 'A' + 'a') : *b;

		if (x != y)
			return FALSE;
		if (!x)
			return TRUE;
	}
}

void team_change_host_set(int policy, int margin)
{
	team_change.set = TRUE;
	team_change.policy = policy < _team_change_off || policy > _team_change_allow ? _team_change_balanced : policy;
	team_change.margin = margin < 0 ? 0 : margin > TEAM_CHANGE_MAXIMUM_MARGIN ? TEAM_CHANGE_MAXIMUM_MARGIN : margin;
}

int team_change_host_policy(void)
{
	char const *value;

	if (team_change.set)
		return team_change.policy;
	value = config_string("network.team_change");
	if (same_word(value, "off"))
		return _team_change_off;
	if (same_word(value, "allow"))
		return _team_change_allow;
	return _team_change_balanced;
}

int team_change_host_margin(void)
{
	long margin;

	if (team_change.set)
		return team_change.margin;
	margin = config_integer("network.team_change_margin");
	return (int)(margin < 0 ? 0 : margin > TEAM_CHANGE_MAXIMUM_MARGIN ? TEAM_CHANGE_MAXIMUM_MARGIN : margin);
}

/* whether a change waits for the player's next death or the next round,
so a round plays out (a round-based game type's: Bomb's) */
static boolean team_change_waits_for_respawn(void)
{
#ifdef HALO_GAME_BROWSER
	return game_engine_bomb_rounds();
#else
	return FALSE;
#endif
}

/* ---------- the host's */

static struct team_change_player *change_of(long player_index)
{
	long absolute = player_index == NONE ? NONE : DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

	return absolute >= 0 && absolute < TEAM_CHANGE_PLAYERS ? &team_change.players[absolute] : NULL;
}

/* the team a player is on, or is to be on */
static short effective_team(long player_index, struct player_datum const *player)
{
	struct team_change_player const *change = change_of(player_index);

	return change && change->waiting ? change->team : (short)player->team_index;
}

/* the player on the new team: the player, its network record and the
host's game record (as auto team balance does) */
static void team_change_apply(long player_index, short team)
{
	struct player_datum *player = player_get(player_index);
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	long list_index = player->network_player_data.player_list_index;

	player->team_index = team;
	player->network_player_data.team_index = (char)team;
	if (game && VALID_INDEX(list_index, HALO_PORT_MAXIMUM_NETWORK_PLAYERS))
		game->players[list_index].team_index = (char)team;
	platform_log("team change: player %ld is on the %s team", (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index),
		team ? "blue" : "red");
}

/* the host's word on a player's change to a team (NONE: the other one);
forced (an admin's) whatever the rule */
static int team_change_decide(long player_index, short team, boolean forced, short *team_now, long *seconds)
{
	struct player_datum *player = player_index == NONE ? NULL : player_try_and_get(player_index);
	struct team_change_player *change = change_of(player_index);
	struct data_iterator iterator;
	struct player_datum *other;
	short current;
	long count[2] = { 0, 0 };

	*seconds = 0;
	*team_now = player && VALID_INDEX(player->team_index, 2) ? (short)player->team_index : 0;
	if (!global_network_game_server_get() || !game_engine_running() || !game_engine_can_score())
		return _delta_team_refused_not_now;
	if (!game_engine_has_teams() || network_coop_active())
		return _delta_team_refused_no_teams;
	if (!player || !change || player->quit_out_of_game || !VALID_INDEX(player->team_index, 2))
		return _delta_team_refused_player;
	current = effective_team(player_index, player);
	if (team == NONE)
		team = (short)(1 - current);
	*team_now = current;
	if (!VALID_INDEX(team, 2))
		return _delta_team_refused_player;
	if (team == current)
		return _delta_team_refused_same;
	if (!forced && team_change_host_policy() == _team_change_off)
		return _delta_team_refused_off;
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		short other_team = effective_team(iterator.datum_index, other);

		if (!other->quit_out_of_game && VALID_INDEX(other_team, 2))
			count[other_team]++;
	}
	/* (never a team left empty while others play, an admin's move too: a
	team game with every player on one team is over,
	game_engine_should_end_game) */
	if (count[current] == 1 && count[team] > 0)
		return _delta_team_refused_last;
	if (!forced)
	{
		int policy = team_change_host_policy();

		if (change->last_time != NONE && change->last_time <= game_time_get() &&
			game_time_get() - change->last_time < TEAM_CHANGE_COOLDOWN * TICKS_PER_SECOND)
		{
			*seconds = (TEAM_CHANGE_COOLDOWN * TICKS_PER_SECOND - (game_time_get() - change->last_time) +
				TICKS_PER_SECOND - 1) / TICKS_PER_SECOND;
			return _delta_team_refused_too_soon;
		}
		/* (after it: the new team one more, the old one fewer) */
		if (policy == _team_change_balanced && count[team] + 1 - (count[current] - 1) > team_change_host_margin())
			return _delta_team_refused_uneven;
	}
	change->last_time = game_time_get();
	*team_now = team;
	/* (back to the team it is on, before a waiting change is made) */
	if (team == player->team_index)
	{
		change->waiting = FALSE;
		change->killing = FALSE;
		change->round_start = FALSE;
		return _delta_team_changed;
	}
	if (player->unit_index != NONE && team_change_waits_for_respawn())
	{
		change->waiting = TRUE;
		change->team = team;
		change->killing = FALSE;
		platform_log("team change: player %ld changes to the %s team at their next respawn",
			(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index), team ? "blue" : "red");
		return _delta_team_changes_at_respawn;
	}
	if (player->unit_index != NONE)
	{
		/* (its death, at the unit's next update, makes the change) */
		change->waiting = TRUE;
		change->team = team;
		change->killing = TRUE;
		unit_kill_no_statistics(player->unit_index);
		return _delta_team_changed;
	}
	change->waiting = FALSE;
	change->killing = FALSE;
	team_change_apply(player_index, team);
	return _delta_team_changed;
}

boolean team_change_death(long dead_player_index)
{
	struct team_change_player *change = change_of(dead_player_index);

	return change && change->waiting && change->killing && global_network_game_server_get() != NULL;
}

void team_change_player_died(long dead_player_index)
{
	struct team_change_player *change = change_of(dead_player_index);
	struct player_datum *player = player_try_and_get(dead_player_index);

	if (!change || !change->waiting || !player || !global_network_game_server_get())
		return;
	if (change->round_start && player->respawn_timer > 1)
		player->respawn_timer = 1;
	change->waiting = FALSE;
	change->killing = FALSE;
	change->round_start = FALSE;
	if (!player->quit_out_of_game && VALID_INDEX(change->team, 2) && game_engine_has_teams())
		team_change_apply(dead_player_index, change->team);
}

boolean team_change_round_started(long player_index)
{
	struct team_change_player *change = change_of(player_index);
	struct player_datum *player = player_try_and_get(player_index);

	if (!change || !change->waiting || change->killing || !player || player->unit_index == NONE ||
		!global_network_game_server_get())
	{
		return FALSE;
	}
	/* (its death, at the unit's next update, makes the change) */
	change->killing = TRUE;
	change->round_start = TRUE;
	unit_kill_no_statistics(player->unit_index);
	platform_log("team change: player %ld changes to the %s team as the round starts",
		(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index), change->team ? "blue" : "red");
	return TRUE;
}

int team_change_move(long list_index, short team, char *text, int size)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	struct network_player const *record;
	struct data_iterator iterator;
	struct player_datum *player;
	long player_index = NONE;
	short team_now;
	long seconds;
	int result;

	if (!game || !VALID_INDEX(list_index, HALO_PORT_MAXIMUM_NETWORK_PLAYERS) || !player_data)
	{
		snprintf(text, (size_t)size, "no game is being played");
		return _delta_team_refused_not_now;
	}
	record = &game->players[list_index];
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (!player->quit_out_of_game && player->network_player_data.machine_index == record->machine_index &&
			player->network_player_data.controller_index == record->controller_index)
		{
			player_index = iterator.datum_index;
		}
	}
	result = team_change_decide(player_index, team, TRUE, &team_now, &seconds);
	switch (result)
	{
	case _delta_team_changed: snprintf(text, (size_t)size, "on the %s team now", team_now ? "blue" : "red"); break;
	case _delta_team_changes_at_respawn:
		snprintf(text, (size_t)size, "on the %s team at their next respawn", team_now ? "blue" : "red");
		break;
	case _delta_team_refused_no_teams: snprintf(text, (size_t)size, "this game has no teams"); break;
	case _delta_team_refused_same: snprintf(text, (size_t)size, "already on the %s team", team_now ? "blue" : "red"); break;
	case _delta_team_refused_last:
		snprintf(text, (size_t)size, "they are the last of their team: the game would end");
		break;
	case _delta_team_refused_not_now: snprintf(text, (size_t)size, "no game is being played"); break;
	default: snprintf(text, (size_t)size, "not in the game"); break;
	}
	return result;
}

/* ---------- the player's */

static char const *team_name(int team)
{
	return team == 1 ? "Blue Team" : "Red Team";
}

/* what the player is shown of a result */
static void team_change_show(int result, int team, int seconds)
{
	char text[96];

	switch (result)
	{
	case _delta_team_changed: snprintf(text, sizeof(text), "Changed to %s", team_name(team)); break;
	case _delta_team_changes_at_respawn:
		snprintf(text, sizeof(text), "You'll join %s when you respawn", team_name(team));
		break;
	case _delta_team_refused_off: snprintf(text, sizeof(text), "This host doesn't allow changing teams"); break;
	case _delta_team_refused_uneven: snprintf(text, sizeof(text), "Can't change teams: the teams would be uneven"); break;
	case _delta_team_refused_no_teams: snprintf(text, sizeof(text), "No teams in this game"); break;
	case _delta_team_refused_too_soon:
		snprintf(text, sizeof(text), "Wait %d second%s to change teams again", seconds, seconds == 1 ? "" : "s");
		break;
	case _delta_team_refused_same: snprintf(text, sizeof(text), "Already on %s", team_name(team)); break;
	case _delta_team_refused_last: snprintf(text, sizeof(text), "Can't change teams: you're the last on yours"); break;
	default: snprintf(text, sizeof(text), "Can't change teams now"); break;
	}
	platform_log("team change: %s", text);
#ifdef HALO_GAME_BROWSER
	chat_note(text);
#endif
}

boolean team_change_pause_offered(void)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game_client *client = global_network_game_client_get();
	struct network_game *game = server ? network_game_server_get_game(server) :
		client ? network_game_client_get_game(client) : NULL;

	return game && game->variant.universal_variant.teams && !network_coop_active() &&
		config_boolean("display.pause_change_teams");
}

boolean team_change_ask(short local_player_index)
{
	long player_index = local_player_index == NONE ? NONE : local_player_get_player_index(local_player_index);
	short team_now;
	long seconds;
	int result;

	if (player_index == NONE || !player_try_and_get(player_index))
		return FALSE;
	if (!game_engine_has_teams())
	{
		team_change_show(_delta_team_refused_no_teams, 0, 0);
		return FALSE;
	}
	if (global_network_game_server_get())
	{
		result = team_change_decide(player_index, NONE, FALSE, &team_now, &seconds);
		team_change_show(result, team_now, (int)seconds);
		return TRUE;
	}
#ifdef HALO_GAME_BROWSER
	if (delta_peer_game_team_ready())
	{
		team_change.results_seen = delta_peer_game_team_result(NULL);
		if (delta_peer_game_team_request((int)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index), DELTA_WIRE_TEAM_OTHER))
		{
			team_change.asked = TRUE;
			platform_log("team change: asked the host");
			return TRUE;
		}
	}
	else
	{
		platform_log("team change: the host doesn't take team changes over Delta");
		chat_note("Changing teams needs a Chupathingy host");
		return FALSE;
	}
#endif
	team_change_show(_delta_team_refused_not_now, 0, 0);
	return FALSE;
}

/* ---------- each frame */

#ifdef HALO_GAME_BROWSER
/* a client's request over Delta Peer: its player's change */
static int team_host_request(void *context, int machine_index, int player, int team, int *team_now, int *seconds)
{
	struct data_iterator iterator;
	struct player_datum *datum;
	long player_index = NONE;
	short now = 0;
	long wait = 0;
	int result;

	(void)context;
	if (!player_data)
		return _delta_team_refused_not_now;
	data_iterator_new(&iterator, player_data);
	while ((datum = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index) == player)
			player_index = iterator.datum_index;
	}
	/* (only a player of the machine that asks) */
	if (player_index == NONE || (long)player_get(player_index)->network_player_data.machine_index != machine_index)
		return _delta_team_refused_player;
	result = team_change_decide(player_index, team == DELTA_WIRE_TEAM_OTHER ? NONE : (short)team, FALSE, &now,
		&wait);
	*team_now = now;
	*seconds = (int)wait;
	platform_log("team change: machine %d asks for player %d: %d", machine_index, player, result);
	return result;
}

static struct delta_peer_team_host const team_host = { NULL, team_host_request };
#endif

void team_change_update(void)
{
	boolean in_game = game_engine_running() && player_data != NULL;

#ifdef HALO_GAME_BROWSER
	if (!team_change.team_host_set)
	{
		delta_peer_game_set_team_host(&team_host);
		team_change.team_host_set = TRUE;
	}
#endif
	/* (a game's changes are its own) */
	if (in_game != team_change.in_game)
	{
		long index;

		team_change.in_game = in_game;
		for (index = 0; index < TEAM_CHANGE_PLAYERS; index++)
		{
			team_change.players[index].waiting = FALSE;
			team_change.players[index].killing = FALSE;
			team_change.players[index].round_start = FALSE;
			team_change.players[index].last_time = NONE;
		}
		team_change.asked = FALSE;
	}
#ifdef HALO_GAME_BROWSER
	if (team_change.asked)
	{
		struct delta_wire_team_result result;
		unsigned int results = delta_peer_game_team_result(&result);

		if (results != team_change.results_seen)
		{
			team_change.results_seen = results;
			team_change.asked = FALSE;
			team_change_show(result.result, result.team, result.seconds);
		}
	}
#endif
}
