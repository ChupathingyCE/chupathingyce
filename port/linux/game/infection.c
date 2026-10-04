/*
INFECTION.C

Infection: a game type of the port's, played on the multiplayer maps.

Everyone begins a round alive. A few seconds in, one player picked at random
is infected: a zombie. A zombie has no weapon and no shield, but is quick,
and kills with its hands; whoever dies while a zombie is about comes back as
one. The round is over when nobody is left alive (its winner is the last to
have been), or when its time runs out with someone still alive (they have
won). A game is a few rounds; its winner has the most points: one for each
zombie killed, one for each of the living infected, and three for a round
won.

The zombies are the Flood of the campaign (its combat form), which the
multiplayer maps do not have: tag_import.c brings it into the map as the
game loads, from a campaign map of the maps folder that has it. With none,
a zombie is a player as they are, with nothing in hand: it hits with its
hands.

It is the slayer game type with its own rules for a kill (the variant's
infection flag, struct slayer_variant: "infection" by name,
game_engine_get_variant_by_name), played with teams: the zombies are one
team (red) and the living the other (blue), so that the game's own notion
of a friend holds for each side, and nobody hurts their own. The host runs
all of this; its clients have the host's units with the host's objects
(network_objects.c), which is where a zombie's body comes from: the same
tags are brought into the map on every machine.

On the wire the game is the slayer it always was, so that a build with no
infection can still be in one: what is infection's own goes in two messages
of its own (network_distributed.h), which such a build drops. A client that
has infection says so once it has loaded, and whether its map has the Flood
(the hello); the host tells those the players' teams (the state). A machine
that never says hello plays a game of team slayer whose teams it is not told
of, against zombies it can see: the host makes its zombies of the Flood only
while every machine in the game has it, and of players with no weapon
otherwise.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "items/items.h"
#include "items/weapons.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "units/biped_definitions.h"
#include "network_distributed.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);
long config_integer(char const *name);
double config_real(char const *name);

/* game.c's: the game type chosen for the game that is loading */
extern struct game_variant game_variant_global;
/* cache_files.c's */
boolean cache_files_map_is_multiplayer(void);
boolean tag_index_is_group(long tag_index, long group_tag);
/* players.c's */
void network_player_detach_unit(long player_index);
/* game_engine_slayer.c's */
void slayer_infection_set_team_score(long team_index, long score);
void slayer_infection_adjust_score(long player_index, long score_delta);
/* the console's, the menus' and the game's server's */
void console_printf(boolean clear, const char *format, ...);
struct network_game_server;
struct network_game_server *global_network_game_server_get(void);
word network_game_server_get_state(struct network_game_server *server, short *state_data);
void network_game_server_change_game_variant(struct network_game_server *server, struct game_variant *variant);
void player_ui_set_game_variant(struct game_variant *variant);
/* network_distributed.c's */
boolean network_game_distributed_client(void);
short distributed_client_machines(long *machine_indices, short maximum);

boolean infection_game_selected(void);
boolean infection_active(void);
boolean infection_player_is_zombie(long player_index);
boolean infection_unit_is_flood_zombie(long unit_index);
long infection_player_unit_definition(long player_index, long definition_index);
boolean infection_player_spawned(long player_index);
boolean infection_allow_pick_up(long unit_index, long weapon_index);
real infection_damage_multiplier(long damaging_player_index, long damaged_player_index);
void infection_initialize_for_new_map(void);
void infection_player_added(long player_index);
void infection_player_killed(long killing_player_index, long dead_player_index);
void infection_player_update(long player_index);
void infection_update(void);
long infection_client_definition(long definition_index);
void infection_client_ready_sent(void);
boolean infection_console_command(char const *text);
void infection_machine_ready(long machine_index, boolean new_machine);
void infection_handle_message(long machine_index, long type, long count, void const *data, long size);
long infection_zombie_melee_target(long unit_index);

/* ---------- constants */

#define FLOOD_BIPED_NAME "characters\\floodcombat_human\\floodcombat_human"
/* how much more a zombie takes to kill while it is the only one */
#define LONE_ZOMBIE_HEALTH 2.0f
/* how far a zombie's blow reaches, from its middle to theirs (world units) */
#define ZOMBIE_REACH 1.3f

enum
{
	_infection_team_zombies = 0,
	_infection_team_humans = 1,

	_round_waiting = 0,
	_round_countdown,
	_round_playing,
	_round_over,

	/* what a machine has said of itself (the hello's count) */
	_machine_unknown = 0,
	_machine_has_infection,
	_machine_has_flood,

	/* how long a machine that has loaded has to say hello, before it is
	taken for a build with no infection */
	HELLO_TICKS = 3 * TICKS_PER_SECOND,
	/* a player's team in the state, for no player */
	NO_TEAM = 0xFF,

	ROUND_OVER_TICKS = 6 * TICKS_PER_SECOND,
	ROUND_WIN_SCORE = 3,
	NAME_SIZE = 16,
};

/* ---------- globals */

static struct
{
	short state;
	short rounds_played;
	long state_ticks;
	/* the last player alive (an absolute index), once there is just one */
	long last_human;
	/* a countdown's second last told */
	long told_seconds;
	/* how many are zombies now */
	long zombie_count;
} infection = { 0 };

/* what goes between the machines: the state's entries (the host's), as they
are on every machine */
struct infection_network_state
{
	/* (bit 0: the zombies are the Flood) */
	byte flags;
	byte teams[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
};

static struct
{
	/* (the host) each client machine: what it said, and when it loaded */
	byte machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	long ready_times[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	/* ... the zombies are the Flood now */
	boolean flood;
	/* ... the state last sent */
	struct infection_network_state sent;
	boolean sent_valid;
	/* (a client) the host's state last had */
	struct infection_network_state received;
	boolean received_valid;
} infection_network = { { 0 } };

/* ---------- private code */

static boolean variant_is_infection(
	struct game_variant const *variant)
{
	return variant->game_engine_index == game_engine_slayer && variant->game_engine_variant.slayer.infection;
}

static void player_name(
	struct player_datum const *player,
	char *name)
{
	long index;

	for (index = 0; index < NAME_SIZE - 1 && index < (long)NUMBEROF(player->name) && player->name[index]; index++)
		name[index] = player->name[index] >= 32 && player->name[index] < 127 ? (char)player->name[index] : '?';
	name[index] = 0;
	if (!index)
		strcpy(name, "A player");
}

static void announce(
	char const *format,
	char const *name)
{
	char text[96];

	snprintf(text, sizeof(text), format, name);
	network_distributed_notice(text);
	platform_log("infection: %s", text);
}

/* how many play, how many of them are alive (not zombies), and the one
alive when there is but one */
static long count_players(
	long *human_count,
	long *lone_human)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long count = 0;

	*human_count = 0;
	*lone_human = NONE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)))
	{
		if (player->quit_out_of_game)
			continue;
		count++;
		if (player->team_index == _infection_team_humans)
		{
			(*human_count)++;
			*lone_human = iterator.datum_index;
		}
	}
	if (*human_count != 1)
		*lone_human = NONE;
	return count;
}

/* the player's body is taken away, and it spawns anew at once: as what it
has become */
static void respawn_player(
	long player_index)
{
	struct player_datum *player = player_get(player_index);
	long unit_index = player->unit_index;

	if (unit_index != NONE)
	{
		network_player_detach_unit(player_index);
		object_delete(unit_index);
	}
	player->respawn_timer = MIN(player->respawn_timer, 1);
	player->respawn_penalty = 0;
}

static void set_state(
	short state)
{
	infection.state = state;
	infection.state_ticks = 0;
	infection.told_seconds = NONE;
}

/* a new round: everyone alive again, each in a new body */
static void round_begin(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)))
	{
		boolean was_zombie = player->team_index != _infection_team_humans;

		player->team_index = _infection_team_humans;
		player->speed_multiplier = 1.0f;
		if (was_zombie || infection.rounds_played)
			respawn_player(iterator.datum_index);
	}
	infection.last_human = NONE;
	set_state(_round_countdown);
}

/* one of the living, picked at random, is the first zombie */
static void infect_someone(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long human_count;
	long lone_human;
	long chosen;

	count_players(&human_count, &lone_human);
	if (human_count <= 0)
		return;
	chosen = seed_random_range(get_global_random_seed_address(), 0, (short)human_count);
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)))
	{
		if (player->quit_out_of_game || player->team_index != _infection_team_humans)
			continue;
		if (!chosen--)
		{
			char name[NAME_SIZE];

			player->team_index = _infection_team_zombies;
			respawn_player(iterator.datum_index);
			player_name(player, name);
			announce("%s is infected", name);
			break;
		}
	}
}

static void round_end(
	long winner_player_index,
	boolean time_up)
{
	infection.rounds_played++;
	if (time_up)
	{
		struct data_iterator iterator;
		struct player_datum *player;

		/* (everyone still alive has won it) */
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)))
		{
			if (!player->quit_out_of_game && player->team_index == _infection_team_humans)
				slayer_infection_adjust_score(iterator.datum_index, ROUND_WIN_SCORE);
		}
		announce("Time is up: the %s", "living have won the round");
	}
	else if (winner_player_index != NONE)
	{
		char name[NAME_SIZE];

		slayer_infection_adjust_score(winner_player_index, ROUND_WIN_SCORE);
		player_name(player_get(winner_player_index), name);
		announce("Everyone is infected: %s was the last alive, and wins the round", name);
	}
	else
	{
		announce("Everyone is %s", "infected");
	}
	set_state(_round_over);
}

/* (the host) whether the zombies are the Flood: in a map that has it, and
with no machine in the game that has not */
static boolean flood_for_everyone(
	void)
{
	long machine_indices[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	short machine_count;
	short index;

	if (tag_loaded('bipd', FLOOD_BIPED_NAME) == NONE)
		return FALSE;
	if (game_connection() != _game_connection_network_server)
		return TRUE;
	machine_count = distributed_client_machines(machine_indices, HALO_PORT_MAXIMUM_NETWORK_MACHINES);
	for (index = 0; index < machine_count; index++)
	{
		long machine_index = machine_indices[index];

		switch (infection_network.machines[machine_index])
		{
		case _machine_has_flood:
			break;
		case _machine_has_infection:
			return FALSE;
		default:
			/* (still loading: it has none of the host's objects yet. Loaded,
			and silent: a build with no infection) */
			if (infection_network.ready_times[machine_index] != NONE &&
				game_time_get() - infection_network.ready_times[machine_index] >= HELLO_TICKS)
			{
				return FALSE;
			}
			break;
		}
	}
	return TRUE;
}

static void network_state_new(
	struct infection_network_state *state)
{
	struct data_iterator iterator;
	struct player_datum *player;

	csmemset(state, 0, sizeof(*state));
	csmemset(state->teams, NO_TEAM, sizeof(state->teams));
	state->flags = infection_network.flood ? 1 : 0;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)))
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

		if (absolute_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
			state->teams[absolute_index] = (byte)player->team_index;
	}
}

/* (the host) the state, to one client machine or (NONE) to all of them */
static void network_state_send(
	long machine_index)
{
	struct
	{
		struct distributed_message_header header;
		struct infection_network_state state;
	} message;

	csmemset(&message, 0, sizeof(message));
	network_state_new(&message.state);
	if (machine_index != NONE)
	{
		distributed_send_to_machine_reliably(machine_index, &message, _distributed_message_infection_state, 1,
			(word)sizeof(message));
	}
	else
	{
		distributed_send(&message, _distributed_message_infection_state, 1, (word)sizeof(message),
			_distributed_to_clients_reliably);
	}
}

/* (the host) each tick: what the zombies are, and the state to its clients
when it has changed */
static void network_update(
	void)
{
	boolean flood;
	struct infection_network_state state;

	/* (a machine that has left the game: nothing is known of the next at its
	index, which may be in the game before it has loaded) */
	if (game_connection() == _game_connection_network_server)
	{
		long machine_indices[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
		boolean present[HALO_PORT_MAXIMUM_NETWORK_MACHINES] = { FALSE };
		short machine_count = distributed_client_machines(machine_indices, HALO_PORT_MAXIMUM_NETWORK_MACHINES);
		short index;

		for (index = 0; index < machine_count; index++)
			present[machine_indices[index]] = TRUE;
		for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; index++)
		{
			if (!present[index])
			{
				infection_network.machines[index] = _machine_unknown;
				infection_network.ready_times[index] = NONE;
			}
		}
	}
	flood = flood_for_everyone();

	if (infection_network.flood && !flood)
	{
		struct data_iterator iterator;
		struct player_datum *player;
		long flood_index = tag_loaded('bipd', FLOOD_BIPED_NAME);

		/* a machine with no Flood has come: the zombies come back as players
		are, which it can see */
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)))
		{
			struct object_datum *object = player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;

			if (object && object->definition_index == flood_index)
				respawn_player(iterator.datum_index);
		}
		platform_log("infection: a machine in the game has no Flood: the zombies are players");
	}
	infection_network.flood = flood;
	if (game_connection() != _game_connection_network_server)
		return;
	network_state_new(&state);
	if (!infection_network.sent_valid || csmemcmp(&state, &infection_network.sent, sizeof(state)))
	{
		infection_network.sent = state;
		infection_network.sent_valid = TRUE;
		network_state_send(NONE);
	}
}

/* (a client) the host's word on a player's team */
static void client_player_team(
	long player_index,
	long team_index)
{
	struct player_datum *player = player_get(player_index);

	if (team_index < 0 || team_index > 1 || player->team_index == team_index)
		return;
	player->team_index = team_index;
	if (player->unit_index != NONE)
	{
		struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit);

		if (unit)
			unit->object.owner_team_index = (short)team_index;
	}
}

/* ---------- public code */

/* whether the game that is loading is infection (the Flood is wanted in its
map: tag_import.c) */
boolean infection_game_selected(
	void)
{
	return variant_is_infection(&game_variant_global) && cache_files_map_is_multiplayer();
}

boolean infection_active(
	void)
{
	return game_engine_running() && variant_is_infection(game_engine_get_variant());
}

boolean infection_player_is_zombie(
	long player_index)
{
	/* (a player who has left the game is nobody's: its slot is another's) */
	struct player_datum *player = player_index != NONE && infection_active() ? player_try_and_get(player_index) : NULL;

	return player && player->team_index == _infection_team_zombies;
}

/* whether a unit is a zombie in the Flood's body (biped_update): it hits as
the Flood does, its unit's own attack, where a player with no weapon hits as
players do */
boolean infection_unit_is_flood_zombie(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);

	return infection_player_is_zombie(unit->unit.player_index) &&
		unit->definition_index == tag_loaded('bipd', FLOOD_BIPED_NAME) &&
		unit_definition_get(unit->definition_index)->unit.melee_damage.index != NONE;
}

/* the unit a player spawns as (player_spawn): a zombie is the Flood, in a
map that has it, while every machine in the game has it too */
long infection_player_unit_definition(
	long player_index,
	long definition_index)
{
	if (infection_player_is_zombie(player_index) && infection_network.flood)
	{
		long flood_index = tag_loaded('bipd', FLOOD_BIPED_NAME);

		if (flood_index != NONE)
			definition_index = flood_index;
	}
	return definition_index;
}

/* a player has spawned (game_engine_postspawn_player_update): TRUE for a
zombie, which has none of what a player starts with */
boolean infection_player_spawned(
	long player_index)
{
	struct player_datum *player;
	struct unit_datum *unit;

	if (!infection_player_is_zombie(player_index))
		return FALSE;
	player = player_get(player_index);
	unit = player->unit_index != NONE ? unit_get(player->unit_index) : NULL;
	/* (where its body cannot be the Flood, a player as they are, with
	nothing in hand: a player with no weapon hits with their hands in every
	build of this network version, the gametypes' loadout of none) */
	if (unit && unit->definition_index != tag_loaded('bipd', FLOOD_BIPED_NAME))
	{
		unit_delete_all_weapons(player->unit_index);
		csmemset(unit->unit.grenade_counts, 0, sizeof(unit->unit.grenade_counts));
	}
	return TRUE;
}

/* whether a unit may pick a weapon up, or hold one it has (the slayer game
type's allow_pick_up): a zombie nothing but what it spawned with */
boolean infection_allow_pick_up(
	long unit_index,
	long weapon_index)
{
	struct unit_datum *unit = unit_get(unit_index);

	if (infection_active() && infection_player_is_zombie(unit->unit.player_index))
	{
		short slot;

		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			if (weapon_index != NONE && unit->unit.weapon_object_indices[slot] == weapon_index)
				return TRUE;
		}
		return FALSE;
	}
	return TRUE;
}

/* how much of one player's damage another takes
(game_engine_get_damage_multiplier): none from their own side, and a
zombie's hands kill in two blows */
real infection_damage_multiplier(
	long damaging_player_index,
	long damaged_player_index)
{
	struct player_datum *damaging;
	struct player_datum *damaged;

	if (damaging_player_index == NONE || damaged_player_index == NONE || damaging_player_index == damaged_player_index ||
		!infection_active())
	{
		return 1.0f;
	}
	damaging = player_try_and_get(damaging_player_index);
	damaged = player_try_and_get(damaged_player_index);
	if (!damaging || !damaged)
		return 1.0f;
	if (damaging->team_index == damaged->team_index)
		return 0.0f;
	if (damaging->team_index == _infection_team_zombies)
		return (real)config_real("infection.zombie_damage");
	/* (a zombie on its own is hard to put down: it has a round to start) */
	return 1.0f / (MAX(0.1f, (real)config_real("infection.zombie_health")) * (infection.zombie_count == 1 ? LONE_ZOMBIE_HEALTH : 1.0f));
}

/* whom a zombie's blow lands on (unit_cause_melee_damage): the nearest of
the living within its reach and before it, NONE for nobody (or for a unit
that is no zombie) */
long infection_zombie_melee_target(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct data_iterator iterator;
	struct player_datum *player;
	long target_index = NONE;
	real nearest = ZOMBIE_REACH;

	if (!infection_player_is_zombie(unit->unit.player_index))
		return NONE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)))
	{
		struct unit_datum *other;
		real_vector3d offset;
		real distance;

		if (player->team_index != _infection_team_humans || player->unit_index == NONE)
			continue;
		other = (struct unit_datum *)object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit);
		if (!other || TEST_FLAG(other->object.damage_flags, _object_dead_bit))
			continue;
		/* (in a vehicle: the vehicle is what is before the zombie) */
		if (other->object.parent_object_index != NONE)
			continue;
		offset.i = other->object.bounding_sphere_center.x - unit->object.bounding_sphere_center.x;
		offset.j = other->object.bounding_sphere_center.y - unit->object.bounding_sphere_center.y;
		offset.k = other->object.bounding_sphere_center.z - unit->object.bounding_sphere_center.z;
		distance = (real)sqrt(offset.i * offset.i + offset.j * offset.j + offset.k * offset.k);
		/* (before it: within a quarter turn or so of where it faces, a
		little more when they are right against it) */
		if (distance < nearest &&
			(distance < 0.3f || (offset.i * unit->object.forward.i + offset.j * unit->object.forward.j) / distance > 0.25f))
		{
			nearest = distance;
			target_index = player->unit_index;
		}
	}
	return target_index;
}

void infection_initialize_for_new_map(
	void)
{
	long flood_index = tag_loaded('bipd', FLOOD_BIPED_NAME);

	csmemset(&infection, 0, sizeof(infection));
	infection.last_human = NONE;
	infection.told_seconds = NONE;
	csmemset(&infection_network, 0, sizeof(infection_network));
	csmemset(infection_network.ready_times, 0xFF, sizeof(infection_network.ready_times));
	infection_network.flood = flood_index != NONE;
	/* the Flood moves as its animations do, at a creature's pace, and a
	player in its body must move as a player does: by the game's speeds for
	players (the tag here is this map's own copy: tag_import.c) */
	if (flood_index != NONE && variant_is_infection(game_engine_get_variant()))
	{
		struct biped_definition *definition = biped_definition_get(flood_index);

		SET_FLAG(definition->biped.flags, _biped_uses_player_physics_bit, TRUE);
		/* (nor does it play dead: it dies, and comes back as every player
		does) */
		definition->unit.feign_death_chance = 0.0f;
		definition->unit.grenade_count = 0;
		/* (and it jumps as a player does: the Flood's own leap is three
		times as high, which no route of the bots' is made for, and it
		lands hurt) */
		{
			struct game_globals *game_globals = scenario_get_game_globals();
			struct game_globals_multiplayer_information *multiplayer_information = game_globals->multiplayer_information.count > 0 ?
				TAG_BLOCK_GET_ELEMENT(&game_globals->multiplayer_information, 0, struct game_globals_multiplayer_information) : NULL;

			if (multiplayer_information && multiplayer_information->unit.index != NONE)
			{
				struct biped_definition *player_definition = biped_definition_get(multiplayer_information->unit.index);

				definition->biped.jump_velocity = player_definition->biped.jump_velocity;
				definition->biped.minimum_soft_landing_velocity = player_definition->biped.minimum_soft_landing_velocity;
				definition->biped.minimum_hard_landing_velocity = player_definition->biped.minimum_hard_landing_velocity;
				definition->biped.maximum_hard_landing_velocity = player_definition->biped.maximum_hard_landing_velocity;
				definition->biped.death_hard_landing_velocity = player_definition->biped.death_hard_landing_velocity;
				/* (and it sees what a player sees: its health, the motion
				tracker. The Flood has no such display of its own) */
				if (!definition->unit.huds.count)
					definition->unit.huds = player_definition->unit.huds;
			}
		}
	}
}

/* a player has joined: alive, unless the round's zombies are already about */
void infection_player_added(
	long player_index)
{
	if (infection_active())
	{
		player_get(player_index)->team_index = !network_game_distributed_client() && infection.state == _round_playing ?
			_infection_team_zombies : _infection_team_humans;
	}
}

/* a player has died (the slayer game type's player_killed_player, on the
host): one of the living killed with zombies about is a zombie from now, by
whatever hand */
void infection_player_killed(
	long killing_player_index,
	long dead_player_index)
{
	struct player_datum *dead_player = player_get(dead_player_index);
	struct player_datum *killing_player = killing_player_index != NONE && killing_player_index != dead_player_index ?
		player_try_and_get(killing_player_index) : NULL;

	if (dead_player->quit_out_of_game || infection.state != _round_playing)
		return;
	if (killing_player && killing_player->team_index != dead_player->team_index)
		slayer_infection_adjust_score(killing_player_index, 1);
	if (dead_player->team_index == _infection_team_humans)
		dead_player->team_index = _infection_team_zombies;
}

/* each player each tick (the slayer game type's player_update_each_tick) */
void infection_player_update(
	long player_index)
{
	struct player_datum *player = player_get(player_index);

	if (network_game_distributed_client())
	{
		/* (the host's word, which may have come before the player did) */
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

		if (infection_network.received_valid && absolute_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
			client_player_team(player_index, infection_network.received.teams[absolute_index]);
		return;
	}
	player->speed_multiplier = player->team_index == _infection_team_zombies ?
		(real)config_real("infection.zombie_speed") : 1.0f;
}

/* each tick (the slayer game type's update), on the host: the round */
void infection_update(
	void)
{
	long human_count;
	long lone_human;
	long player_count;

	if (network_game_distributed_client())
		return;
	network_update();
	if (!game_engine_can_score())
		return;
	player_count = count_players(&human_count, &lone_human);
	slayer_infection_set_team_score(_infection_team_humans, human_count);
	slayer_infection_set_team_score(_infection_team_zombies, player_count - human_count);
	infection.zombie_count = player_count - human_count;
	infection.state_ticks++;

	switch (infection.state)
	{
	case _round_waiting:
		/* (a game needs two) */
		if (player_count >= 2)
			round_begin();
		break;
	case _round_countdown:
	{
		long seconds = config_integer("infection.countdown") - infection.state_ticks / TICKS_PER_SECOND;

		if (player_count < 2)
		{
			set_state(_round_waiting);
		}
		else if (seconds <= 0)
		{
			set_state(_round_playing);
			infect_someone();
		}
		else if (seconds != infection.told_seconds && (seconds == config_integer("infection.countdown") || seconds <= 3))
		{
			char text[64];

			infection.told_seconds = seconds;
			snprintf(text, sizeof(text), "Round %d of %ld: the infection begins in %ld", infection.rounds_played + 1,
				MAX(1, config_integer("infection.rounds")), seconds);
			network_distributed_notice(text);
		}
		break;
	}
	case _round_playing:
	{
		long round_ticks = config_integer("infection.round_minutes") * 60 * TICKS_PER_SECOND;

		if (lone_human != NONE && infection.last_human != DATUM_INDEX_TO_ABSOLUTE_INDEX(lone_human))
		{
			char name[NAME_SIZE];

			infection.last_human = DATUM_INDEX_TO_ABSOLUTE_INDEX(lone_human);
			player_name(player_get(lone_human), name);
			if (player_count > 2)
				announce("%s is the last one alive", name);
		}
		if (!human_count)
		{
			long winner = NONE;

			/* (the last alive, if still in the game) */
			if (infection.last_human != NONE)
			{
				struct data_iterator iterator;
				struct player_datum *player;

				data_iterator_new(&iterator, player_data);
				while ((player = (struct player_datum *)data_iterator_next(&iterator)))
				{
					if (DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index) == infection.last_human && !player->quit_out_of_game)
						winner = iterator.datum_index;
				}
			}
			round_end(winner, FALSE);
		}
		else if (human_count == player_count)
		{
			/* (the zombies have all left the game) */
			if (player_count >= 2)
				infect_someone();
			else
				set_state(_round_waiting);
		}
		else if (round_ticks > 0 && infection.state_ticks >= round_ticks)
		{
			round_end(NONE, TRUE);
		}
		break;
	}
	case _round_over:
		if (infection.state_ticks >= ROUND_OVER_TICKS)
		{
			if (infection.rounds_played >= MAX(1, config_integer("infection.rounds")))
				game_engine_end_game();
			else
				round_begin();
		}
		break;
	}
}

/* the console's "gametype <name>" (hs.c), in the lobby of a game this
machine hosts: the game type by its name (game_engine_get_variant_by_name's,
"infection" among them, which no menu lists). TRUE when the text was it. */
boolean infection_console_command(
	char const *text)
{
	char name[32];
	long length = 0;
	struct network_game_server *server = global_network_game_server_get();
	struct game_variant variant;
	struct game_variant empty;

	while (*text == ' ' || *text == '\t' || *text == '(')
		text++;
	if (strncmp(text, "gametype", 8) || (text[8] && text[8] != ' ' && text[8] != '\t' && text[8] != ')'))
		return FALSE;
	text += 8;
	while (*text == ' ' || *text == '\t' || *text == '"')
		text++;
	while (*text && *text != ' ' && *text != '\t' && *text != ')' && *text != '"' && length < (long)sizeof(name) - 1)
		name[length++] = *text++;
	name[length] = 0;
	if (!length)
	{
		console_printf(FALSE, "gametype <name>: the game type of the lobby this machine hosts (infection, slayer, team_slayer, ctf, ...)");
		return TRUE;
	}
	if (!server || network_game_server_get_state(server, NULL) != 0)
	{
		console_printf(FALSE, "gametype: only in the lobby of a game this machine hosts");
		return TRUE;
	}
	csmemset(&empty, 0, sizeof(empty));
	game_engine_get_variant_by_name(&variant, name);
	if (!csmemcmp(&variant, &empty, sizeof(variant)))
	{
		console_printf(FALSE, "gametype: no game type %s", name);
		return TRUE;
	}
	player_ui_set_game_variant(&variant);
	network_game_server_change_game_variant(server, &variant);
	console_printf(FALSE, "gametype: %s", name);
	return TRUE;
}

/* (a client) the tag the host's object is made of here: the host's Flood,
on a machine that has not got it (no campaign map with it in its maps
folder), is a player's body */
long infection_client_definition(
	long definition_index)
{
	if (infection_active() && !tag_index_is_group(definition_index, 'obje'))
	{
		struct game_globals *game_globals = scenario_get_game_globals();
		struct game_globals_multiplayer_information *multiplayer_information = game_globals->multiplayer_information.count > 0 ?
			TAG_BLOCK_GET_ELEMENT(&game_globals->multiplayer_information, 0, struct game_globals_multiplayer_information) : NULL;

		if (multiplayer_information && multiplayer_information->unit.index != NONE)
			definition_index = multiplayer_information->unit.index;
	}
	return definition_index;
}

/* (a client) its ready has gone to the host (network_objects_client_tick):
with it, that this build has infection, and whether this map has the Flood */
void infection_client_ready_sent(
	void)
{
	struct distributed_message_header message;

	if (!infection_active())
		return;
	csmemset(&message, 0, sizeof(message));
	distributed_send(&message, _distributed_message_infection_hello,
		tag_loaded('bipd', FLOOD_BIPED_NAME) != NONE ? _machine_has_flood : _machine_has_infection,
		(word)sizeof(message), _distributed_to_host_reliably);
}

/* (the host) a client machine has loaded the game (its ready:
network_distributed_handle_message); new at its index, nothing is known of
it until its hello */
void infection_machine_ready(
	long machine_index,
	boolean new_machine)
{
	if (machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES)
		return;
	if (new_machine)
		infection_network.machines[machine_index] = _machine_unknown;
	if (new_machine || infection_network.ready_times[machine_index] == NONE)
		infection_network.ready_times[machine_index] = game_time_get();
}

/* one of infection's own messages (network_distributed_handle_message);
machine_index is the sender's on the host, NONE on a client */
void infection_handle_message(
	long machine_index,
	long type,
	long count,
	void const *data,
	long size)
{
	if (!infection_active())
		return;
	switch (type)
	{
	case _distributed_message_infection_hello:
		if (machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES ||
			game_connection() != _game_connection_network_server)
		{
			return;
		}
		infection_network.machines[machine_index] = count == _machine_has_flood ? _machine_has_flood : _machine_has_infection;
		/* (and it is told where the game is) */
		network_state_send(machine_index);
		break;
	case _distributed_message_infection_state:
	{
		struct data_iterator iterator;
		struct player_datum *player;

		if (machine_index != NONE || game_connection() != _game_connection_network_client ||
			size < (long)sizeof(infection_network.received))
		{
			return;
		}
		csmemcpy(&infection_network.received, data, sizeof(infection_network.received));
		infection_network.received_valid = TRUE;
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)))
		{
			long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

			if (absolute_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
				client_player_team(iterator.datum_index, infection_network.received.teams[absolute_index]);
		}
		break;
	}
	}
}
