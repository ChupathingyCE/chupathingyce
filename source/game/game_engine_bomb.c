/*
GAME_ENGINE_BOMB.C

port: Bomb, the game type Bungie cut (this file was empty in the shipped
game), and Grifball, a preset of it (docs/bomb.md). Two teams, one bomb in
the middle of the map: a team scores by carrying it into the other team's
goal, which ends the round. The goals are the map's CTF flag markers and the
bomb is the map's oddball ball, at its first ball spawn (else halfway
between the goals), so every map with two CTF bases plays it.

Bomb is played only between ChupathingyCE machines that agree on Delta
Peer's bomb capability: a host drops every other machine
(network_server_manager.c), and sends its player traits (everyone's, and
the carrier's on top) over Delta Peer (TRAITS). Nothing of a standard game
type changes.

The host decides pickups, scores and rounds; a client of the distributed
netcode shows them from the host's state (game_engine_bomb_write_network_state).
*/

/* ---------- headers */

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "game_engine.h"
#include "game.h"
#include "players.h"
#include "players_runtime.h"
#include "items/items.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "objects/damage.h"
#include "physics/collision_model_definitions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "cache/cache_files.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/unicode.h"
#include "units/units.h"

#ifdef HALO_GAME_BROWSER

#include "../../port/linux/src/delta_peer.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	NUMBER_OF_BOMB_TEAMS = 2,
	/* the goals the game shows (nav points): the bomb, and each team's goal
	(shown to the team that attacks it) */
	BOMB_GOAL_BOMB = 0,
	BOMB_GOAL_FIRST_TEAM = 1,
	/* "play ball" this long into a round */
	BOMB_ROUND_ANNOUNCEMENT_TICKS = 2 * TICKS_PER_SECOND,
	/* the round's last seconds, counted aloud */
	BOMB_ROUND_WARNING_TICKS = 5 * TICKS_PER_SECOND,
	/* the most weapons a court gives (bomb_courts) */
	MAXIMUM_BOMB_COURT_WEAPONS = 2,
};

/* how near the carrier stands to the goal's marker to arm it (CTF's) */
#define BOMB_GOAL_RADIUS 1.0f

/* the shipped multiplayer sounds Bomb borrows (game_engine_multiplayer_sounds.c) */
enum
{
	_bomb_sound_play_ball = 0,
	_bomb_sound_blue_team_score = 0xA,
	_bomb_sound_red_team_score = 0xD,
	_bomb_sound_hill_move = 0x1E,
	_bomb_sound_countdown = 0x2A,
};

/* the messages (format_message): past the generic ones, as the other game
types' */
enum
{
	_bomb_message_you_scored = 30,
	_bomb_message_ally_scored,
	_bomb_message_enemy_scored,
	_bomb_message_you_have_the_bomb,
	_bomb_message_ally_has_the_bomb,
	_bomb_message_enemy_has_the_bomb,
	_bomb_message_bomb_reset,
	_bomb_message_new_round,
	_bomb_message_round_over,
};

/* ---------- globals */

static struct
{
	/* each team's goal (its CTF flag marker): a team scores at the other's */
	boolean has_goal[NUMBER_OF_BOMB_TEAMS];
	real_point3d goals[NUMBER_OF_BOMB_TEAMS];
	/* where the bomb starts and resets */
	real_point3d spawn;
	/* the bomb (the host's object, at the same index on every machine) */
	long weapon_index;
	/* its carrier now (NONE: none) */
	long carrier;
	long scores[NUMBER_OF_BOMB_TEAMS];
	long score_to_win;
	/* the round (from 1), its ticks left (NONE: no limit), when it began */
	long round;
	long round_ticks;
	long round_start_time;
	/* the carrier's ticks in the goal */
	long arm_ticks;
	/* the map's energy sword players can use (NONE: none: fists alone) */
	long sword_definition_index;
	/* a court's (bomb_courts): its scripts off, and its weapons players
	get in place of the sword (NONE: a weapon the map doesn't have) */
	boolean court;
	long court_weapons[MAXIMUM_BOMB_COURT_WEAPONS];
	/* the players drawn in the carrier's color (by absolute index) */
	boolean highlighted[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	/* each player's (by absolute index), for the game's report: ticks with
	the bomb, carriers killed, kills while carrying (every machine counts
	them from what it sees: the carrier and the deaths are every
	machine's) */
	long carry_ticks[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short carrier_kills[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short kills_as_carrier[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	/* a client's: its host's traits (TRAITS), by the number Delta Peer
	raises */
	delta_u32 host_traits_generation;
	boolean has_host_traits;
	struct game_traits host_traits;
} bomb_globals;

/* the host's events a client shows (each machine counts them, a client as
it last had them from the host) */
static struct
{
	byte scores;
	byte scorer;
	byte scorer_team;
	byte carries;
	byte carrier;
	byte resets;
	byte rounds;
	byte round_overs;
} bomb_events;

/* the Custom Edition maps that are Grifball courts with a game of their
own, by their version (cache_files_map_version: the first 32 bits of the
whole file's hash): their scripts are off while Bomb plays on them (they
run the map's own ball, goals, score and the game's end), and its players
get the court's own weapons, the first in their hands */
static struct bomb_court
{
	unsigned long version;
	char const *weapons[MAXIMUM_BOMB_COURT_WEAPONS];
} const bomb_courts[] =
{
	/* grifball@ce, HaloNet's Grifball court (166 MB, a CTF map): its
	scripts make its own ball (weapons\grifball\grifball), blow it up at
	a goal with a tank shell's explosion that kills whoever is near, and
	kill a player out of bounds; its energy sword detonates when dropped,
	so bomb_find_sword passes it by */
	{ 0x62b910c6UL, {
		"h3mt\\weapons\\covenant\\gravity_hammer\\gravity_hammer",
		"weapons\\energy_sword\\energy_sword backup",
	} },
};

/* network_game_globals.c's */
boolean network_game_distributed_client(void);
/* port/linux/game/network_distributed.c's */
byte distributed_player_to_byte(long player_index);
long distributed_player_from_byte(byte player_index);

static void bomb_show_score(long player_index, long team_index);
static void bomb_show_carry(long player_index);
static void bomb_show_reset(void);
static void bomb_show_round(boolean timed_out);

static boolean bomb_running(void)
{
	return game_engine && game_engine->type == game_engine_bomb;
}

/* ---------- the playtest's log (debug.txt): every grab, drop, score,
reset, round and traits change, with the tick, the machine's role and a
position; "bomb: DISAGREE" for a client that sees the game otherwise than
its host */

static void bomb_log(char const *format, ...)
{
	char line[256];
	va_list arguments;
	int length;

	length = snprintf(line, sizeof(line), "bomb: %s tick %ld ", network_game_distributed_client() ? "client" : "host",
		(long)game_time_get());
	if (length < 0 || length >= (int)sizeof(line))
		return;
	va_start(arguments, format);
	vsnprintf(line + length, sizeof(line) - (size_t)length, format, arguments);
	va_end(arguments);
	error(_error_log, "%s", line);
}

/* a player's absolute index, team and position, for the log */
static void bomb_log_player(char const *what, long player_index)
{
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
	real_point3d position = *global_origin3d;

	if (player && player->unit_index != NONE)
		position = object_get(player->unit_index)->object.position;
	bomb_log("%s player %ld team %ld at (%.2f %.2f %.2f)", what,
		player_index != NONE ? (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) : -1L, player ? (long)player->team_index : -1L,
		position.x, position.y, position.z);
}

static void bomb_log_traits(char const *whose, struct game_traits const *traits)
{
	bomb_log("traits (%s): everyone health %d damage %d resistance %d speed %d shields %d; carrier health %d damage %d "
		"resistance %d speed %d shields %d highlight %d", whose, traits->everyone.health, traits->everyone.damage,
		traits->everyone.resistance, traits->everyone.speed, traits->everyone.shields, traits->carrier.health,
		traits->carrier.damage, traits->carrier.resistance, traits->carrier.speed, traits->carrier.shields,
		traits->carrier.highlight);
}

/* ---------- player traits */

static short bomb_trait_pin(short value, short minimum, short maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

static void bomb_player_traits_clean(struct player_traits *traits)
{
	traits->health = bomb_trait_pin(traits->health, PLAYER_TRAIT_HEALTH_MINIMUM, PLAYER_TRAIT_HEALTH_MAXIMUM);
	traits->damage = bomb_trait_pin(traits->damage, PLAYER_TRAIT_DAMAGE_MINIMUM, PLAYER_TRAIT_DAMAGE_MAXIMUM);
	traits->resistance = bomb_trait_pin(traits->resistance, PLAYER_TRAIT_RESISTANCE_MINIMUM,
		PLAYER_TRAIT_RESISTANCE_MAXIMUM);
	traits->speed = bomb_trait_pin(traits->speed, PLAYER_TRAIT_SPEED_MINIMUM, PLAYER_TRAIT_SPEED_MAXIMUM);
	if (traits->shields >= NUMBER_OF_TRAIT_SHIELDS)
		traits->shields = _trait_shields_unchanged;
	traits->highlight = traits->highlight ? 1 : 0;
	traits->pad[0] = traits->pad[1] = 0;
}

static void bomb_player_traits_none(struct player_traits *traits)
{
	csmemset(traits, 0, sizeof(*traits));
	traits->health = 100;
	traits->damage = 100;
	traits->resistance = 100;
	traits->speed = 100;
	traits->shields = _trait_shields_unchanged;
}

void game_engine_traits_default(
	struct game_variant const *variant,
	struct game_traits *traits)
{
	bomb_player_traits_none(&traits->everyone);
	bomb_player_traits_none(&traits->carrier);
	if (!variant || variant->game_engine_index != game_engine_bomb)
		return;
	/* (the carrier stands out) */
	traits->carrier.highlight = 1;
	if (variant->game_engine_variant.bomb.traits == _bomb_traits_grifball)
	{
		/* Grifball's rules, as Halo CE has them: a tenth of the health and
		twice the damage (one hit kills), no shields; the carrier fast,
		hard to kill and overshielded (CE's overshield is three times the
		shields: Halo 3's double overshield) */
		traits->everyone.health = 10;
		traits->everyone.damage = 200;
		traits->everyone.shields = _trait_shields_none;
		traits->carrier.speed = 150;
		traits->carrier.resistance = 150;
		traits->carrier.shields = _trait_shields_overshield;
	}
	if (variant->game_engine_variant.bomb.traits == _bomb_traits_open_pitch)
	{
		/* Grifball's, and fast: 175% everyone, the carrier 200% (on top:
		115% of 175%, kept to the speed's most) */
		traits->everyone.health = 10;
		traits->everyone.damage = 200;
		traits->everyone.speed = 175;
		traits->everyone.shields = _trait_shields_none;
		traits->carrier.speed = 115;
		traits->carrier.resistance = 150;
		traits->carrier.shields = _trait_shields_overshield;
	}
}

void game_engine_traits_clean(
	struct game_traits *traits)
{
	bomb_player_traits_clean(&traits->everyone);
	bomb_player_traits_clean(&traits->carrier);
}

/* a client's: its host's traits as TRAITS says them, read when Delta Peer
has new ones */
static void bomb_read_host_traits(void)
{
	struct delta_wire_traits wire;
	delta_u32 generation = delta_peer_game_host_traits(&wire);
	struct player_traits *sets[2];
	struct delta_wire_player_traits const *wires[2];
	short index;

	if (!generation || generation == bomb_globals.host_traits_generation)
		return;
	bomb_globals.host_traits_generation = generation;
	bomb_globals.has_host_traits = wire.kind == DELTA_WIRE_TRAITS_BOMB;
	if (!bomb_globals.has_host_traits)
		return;
	sets[0] = &bomb_globals.host_traits.everyone;
	sets[1] = &bomb_globals.host_traits.carrier;
	wires[0] = &wire.everyone;
	wires[1] = &wire.carrier;
	for (index = 0; index < 2; index++)
	{
		csmemset(sets[index], 0, sizeof(*sets[index]));
		sets[index]->health = (short)wires[index]->health;
		sets[index]->damage = (short)wires[index]->damage;
		sets[index]->resistance = (short)wires[index]->resistance;
		sets[index]->speed = (short)wires[index]->speed;
		sets[index]->shields = wires[index]->shields;
		sets[index]->highlight = wires[index]->highlight;
	}
	game_engine_traits_clean(&bomb_globals.host_traits);
	bomb_log_traits("the host's, by TRAITS", &bomb_globals.host_traits);
}

/* the traits the game plays by: a client's from its host (TRAITS), else
this machine's own (its variant's) */
static struct game_traits const *bomb_traits(void)
{
	static struct game_traits local;

	if (network_game_distributed_client())
	{
		bomb_read_host_traits();
		if (bomb_globals.has_host_traits)
			return &bomb_globals.host_traits;
	}
	game_engine_traits_default(game_engine_get_variant(), &local);
	return &local;
}

boolean game_engine_traits_active(
	void)
{
	return bomb_running();
}

/* a player's traits: everyone's, and the carrier's on top */
static void bomb_player_traits(long player_index, struct player_traits *traits)
{
	struct game_traits const *game_traits = bomb_traits();

	*traits = game_traits->everyone;
	if (player_index != NONE && player_index == bomb_globals.carrier)
	{
		traits->health = (short)(traits->health * game_traits->carrier.health / 100);
		traits->damage = (short)(traits->damage * game_traits->carrier.damage / 100);
		traits->resistance = (short)(traits->resistance * game_traits->carrier.resistance / 100);
		traits->speed = (short)MIN(traits->speed * game_traits->carrier.speed / 100, PLAYER_TRAIT_SPEED_MAXIMUM);
		if (game_traits->carrier.shields != _trait_shields_unchanged)
			traits->shields = game_traits->carrier.shields;
		traits->highlight = game_traits->carrier.highlight;
	}
}

real game_engine_traits_damage_multiplier(
	long damaging_player_index,
	long damaged_player_index)
{
	struct player_traits traits;
	real multiplier = 1.0f;

	if (!bomb_running())
		return 1.0f;
	if (damaging_player_index != NONE)
	{
		bomb_player_traits(damaging_player_index, &traits);
		multiplier *= traits.damage / 100.0f;
	}
	if (damaged_player_index != NONE)
	{
		bomb_player_traits(damaged_player_index, &traits);
		multiplier *= 100.0f / MAX(traits.health, 1);
		multiplier *= 100.0f / MAX(traits.resistance, 1);
	}
	return multiplier;
}

long game_engine_traits_shields(
	long player_index)
{
	struct player_traits traits;

	if (!bomb_running() || player_index == NONE)
		return NONE;
	bomb_player_traits(player_index, &traits);
	switch (traits.shields)
	{
	case _trait_shields_none:
		return FALSE;
	case _trait_shields_normal:
	case _trait_shields_overshield:
		return TRUE;
	}
	return NONE;
}

void game_engine_bomb_host_traits(
	struct game_variant const *variant)
{
	struct game_traits traits;
	struct delta_wire_traits wire;
	struct player_traits const *sets[2];
	struct delta_wire_player_traits *wires[2];
	short index;

	if (!variant || variant->game_engine_index != game_engine_bomb)
	{
		delta_peer_game_set_traits(NULL);
		return;
	}
	game_engine_traits_default(variant, &traits);
	game_engine_traits_clean(&traits);
	csmemset(&wire, 0, sizeof(wire));
	wire.kind = DELTA_WIRE_TRAITS_BOMB;
	sets[0] = &traits.everyone;
	sets[1] = &traits.carrier;
	wires[0] = &wire.everyone;
	wires[1] = &wire.carrier;
	for (index = 0; index < 2; index++)
	{
		wires[index]->health = (unsigned short)sets[index]->health;
		wires[index]->damage = (unsigned short)sets[index]->damage;
		wires[index]->resistance = (unsigned short)sets[index]->resistance;
		wires[index]->speed = (unsigned short)sets[index]->speed;
		wires[index]->shields = sets[index]->shields;
		wires[index]->highlight = sets[index]->highlight;
	}
	delta_peer_game_set_traits(&wire);
	{
		static struct game_traits logged;
		static boolean have_logged = FALSE;

		if (!have_logged || csmemcmp(&logged, &traits, sizeof(traits)))
		{
			logged = traits;
			have_logged = TRUE;
			if (bomb_running())
				bomb_log_traits("hosted", &traits);
			else
				error(_error_log, "bomb: host traits set for the hosted game (%d health, %d speed)",
					traits.everyone.health, traits.everyone.speed);
		}
	}
}

/* ---------- the map */

/* the map's energy sword a player can use: a weapon tag named for a sword,
with a first person model and animations, that does not vanish when dropped
(the Elite's does). NONE for none */
static long bomb_find_sword(void)
{
	struct tag_iterator iterator;
	long tag_index;

	tag_iterator_new(&iterator, WEAPON_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		char const *name = tag_get_name(tag_index);
		struct weapon_definition *definition;
		char lower[256];
		short index;

		if (!name)
			continue;
		for (index = 0; name[index] && index < (short)sizeof(lower) - 1; index++)
			lower[index] = name[index] >= 'A' && name[index] <= 'Z' ? (char)(name[index] - 'A' + 'a') : name[index];
		lower[index] = 0;
		if (!strstr(lower, "sword"))
			continue;
		definition = weapon_definition_get(tag_index);
		if (definition->weapon.interface_definition.first_person_model.index == NONE ||
			definition->weapon.interface_definition.first_person_animations.index == NONE ||
			TEST_FLAG(definition->weapon.flags, _weapon_detonates_when_dropped_bit))
		{
			continue;
		}
		return tag_index;
	}
	return NONE;
}

/* the court the map is (bomb_courts), if it is one: its weapons found */
static void bomb_find_court(void)
{
	unsigned long version;
	long court_index;
	short index;

	bomb_globals.court = FALSE;
	for (index = 0; index < MAXIMUM_BOMB_COURT_WEAPONS; index++)
		bomb_globals.court_weapons[index] = NONE;
	if (!CACHE_FILE_TAGS_ARE_CE() || !cache_file_loaded_map_name())
		return;
	version = cache_files_map_version(cache_file_loaded_map_name());
	for (court_index = 0; court_index < (long)NUMBEROF(bomb_courts); court_index++)
	{
		if (bomb_courts[court_index].version != version)
			continue;
		bomb_globals.court = TRUE;
		for (index = 0; index < MAXIMUM_BOMB_COURT_WEAPONS; index++)
		{
			if (bomb_courts[court_index].weapons[index])
			{
				bomb_globals.court_weapons[index] =
					tag_loaded(WEAPON_DEFINITION_TAG, bomb_courts[court_index].weapons[index]);
			}
		}
		bomb_log("court %s (%08lx): its scripts off, its weapons %lx %lx", cache_file_loaded_map_name(), version,
			(unsigned long)bomb_globals.court_weapons[0], (unsigned long)bomb_globals.court_weapons[1]);
		return;
	}
}

boolean game_engine_bomb_scripts_off(
	void)
{
	return bomb_running() && bomb_globals.court;
}

boolean game_engine_bomb_melee_only(
	void)
{
	return bomb_running() &&
		TEST_FLAG(game_engine_get_variant()->game_engine_variant.bomb.flags, _bomb_melee_only_bit);
}

void game_engine_bomb_give_sword(
	long unit_index)
{
	long definitions[MAXIMUM_BOMB_COURT_WEAPONS];
	boolean added;
	short index;

	if (!game_engine_bomb_melee_only() || unit_index == NONE)
		return;
	/* (a court's own weapons, or the map's sword) */
	for (index = 0; index < MAXIMUM_BOMB_COURT_WEAPONS; index++)
		definitions[index] = bomb_globals.court ? bomb_globals.court_weapons[index] : NONE;
	if (!bomb_globals.court)
		definitions[0] = bomb_globals.sword_definition_index;
	/* (the last first, in place of the loadout's; each one after it into
	the unit's hands, so that the first ends there) */
	added = FALSE;
	for (index = MAXIMUM_BOMB_COURT_WEAPONS - 1; index >= 0; index--)
	{
		struct object_placement_data placement_data;
		long weapon_index;

		if (definitions[index] == NONE)
			continue;
		object_placement_data_new(&placement_data, definitions[index], NONE);
		weapon_index = object_new(&placement_data);
		if (weapon_index != NONE && unit_add_weapon_to_inventory(unit_index, weapon_index,
			added ? _unit_add_weapon_starting : _unit_add_weapon_replace))
		{
			added = TRUE;
		}
		else if (weapon_index != NONE)
			object_delete(weapon_index);
	}
}

static long bomb_create(void)
{
	long definition_index = get_ball_definition_index();
	struct object_placement_data placement_data;
	long weapon_index;

	if (definition_index == NONE)
	{
		error(_error_silent, "bomb: the map has no ball to be the bomb");
		return NONE;
	}
	object_placement_data_new(&placement_data, definition_index, NONE);
	placement_data.position = bomb_globals.spawn;
	weapon_index = object_new(&placement_data);
	if (weapon_index == NONE)
	{
		error(_error_silent, "bomb: failed to create the bomb");
		return NONE;
	}
	/* (ball 0: a flag or ball of no team is no objective weapon to a
	client, network_objects.c; it belongs to no team all the same) */
	weapon_get(weapon_index)->object.owner_team_index = 0;
	object_set_automatic_deactivation(weapon_index, FALSE);
	return weapon_index;
}

/* the bomb back at its spawn, out of any hands (made again if it is gone) */
static void bomb_reset(void)
{
	long weapon_index = bomb_globals.weapon_index;
	struct weapon_datum *weapon = weapon_index != NONE ?
		(struct weapon_datum *)object_try_and_get_and_verify_type(weapon_index, _object_mask_weapon) : NULL;

	bomb_globals.arm_ticks = 0;
	if (!weapon)
	{
		bomb_globals.weapon_index = bomb_create();
		return;
	}
	if (weapon->object.parent_object_index != NONE &&
		object_try_and_get_and_verify_type(weapon->object.parent_object_index, _object_mask_unit))
	{
		unit_drop_current_weapon(weapon->object.parent_object_index, TRUE);
	}
	game_engine_flag_reset(weapon_index, &bomb_globals.spawn);
}

/* ---------- rounds */

/* the host's: a player back at their team's end, as at a round's start: on
one of the team's starting locations nearest its own goal (the count-th of
them, so that a team spreads out), whole again. A player riding something,
or with no starting location, is killed instead (no death counted: it
respawns at its goal) */
static void bomb_send_home(long player_index, short count)
{
	struct player_datum *player = player_get(player_index);
	struct object_datum *unit;
	struct player_starting_location *chosen[8];
	real distances[8];
	short chosen_count = 0;
	short location_count = player_get_starting_location_count();
	short index;

	if (player->unit_index == NONE)
		return;
	unit = object_get(player->unit_index);
	if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return;
	/* the team's eight starting locations nearest its goal, nearest first */
	for (index = 0; index < location_count && VALID_INDEX(player->team_index, NUMBER_OF_BOMB_TEAMS) &&
		bomb_globals.has_goal[player->team_index]; index++)
	{
		struct player_starting_location *location = player_get_starting_location(index);
		real distance;
		short place;

		if (location->team_index != player->team_index ||
			!match_game_type(game_engine_bomb, NUMBEROF(location->game_types), location->game_types))
		{
			continue;
		}
		distance = distance_squared3d(&bomb_globals.goals[player->team_index], &location->position);
		for (place = chosen_count; place > 0 && distances[place - 1] > distance; place--)
		{
			if (place < (short)NUMBEROF(chosen))
			{
				chosen[place] = chosen[place - 1];
				distances[place] = distances[place - 1];
			}
		}
		if (place < (short)NUMBEROF(chosen))
		{
			chosen[place] = location;
			distances[place] = distance;
			if (chosen_count < (short)NUMBEROF(chosen))
				chosen_count++;
		}
	}
	if (!chosen_count || unit->object.parent_object_index != NONE)
	{
		unit_kill_no_statistics(player->unit_index);
		return;
	}
	{
		struct player_starting_location *location = chosen[count % chosen_count];
		real_point3d position = location->position;
		real_vector3d forward;

		vector3d_from_angle(&forward, location->facing);
		position.z += 0.05f;
		object_set_position(player->unit_index, &position, &forward, global_up3d);
		unit->object.translational_velocity = *global_zero_vector3d;
		unit->object.body_vitality = unit->object.maximum_body_vitality > 0.0f ? 1.0f : 0.0f;
		if (unit->object.maximum_shield_vitality > 0.0f && unit->object.shield_vitality < 1.0f)
			unit->object.shield_vitality = 1.0f;
	}
}

static void bomb_start_round(boolean timed_out)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long round_time = game_engine_get_variant()->game_engine_variant.bomb.round_time;
	short counts[NUMBER_OF_BOMB_TEAMS] = { 0, 0 };

	bomb_log("round %ld ends (%s); round %ld starts, red %ld blue %ld", (long)bomb_globals.round,
		timed_out ? "time" : "goal", (long)bomb_globals.round + 1, (long)bomb_globals.scores[0],
		(long)bomb_globals.scores[1]);
	bomb_globals.round++;
	bomb_globals.round_ticks = round_time > 0 ? round_time * TICKS_PER_SECOND : NONE;
	bomb_globals.round_start_time = game_time_get();
	bomb_reset();
	/* every player starts again at their own goal */
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		short team = VALID_INDEX(player->team_index, NUMBER_OF_BOMB_TEAMS) ? (short)player->team_index : 0;

		bomb_send_home(iterator.datum_index, counts[team]++);
	}
	bomb_events.rounds++;
	if (timed_out)
		bomb_events.round_overs++;
	bomb_show_round(timed_out);
}

/* ---------- the engine */

static void bomb_engine_nothing(void)
{
}

static boolean bomb_engine_initialize_for_new_map(void)
{
	struct scenario *scenario = global_scenario_get();
	long team_index;
	long flag_index;
	short index;

	if (!game_engine_get_variant()->universal_variant.teams)
		error(_error_silent, "bomb started up without teams");
	csmemset(&bomb_globals, 0, sizeof(bomb_globals));
	csmemset(&bomb_events, 0, sizeof(bomb_events));
	bomb_globals.weapon_index = NONE;
	bomb_globals.carrier = NONE;
	bomb_globals.score_to_win = game_engine_get_variant()->universal_variant.score_to_win;
	bomb_globals.sword_definition_index = bomb_find_sword();
	bomb_find_court();

	for (team_index = 0; team_index < NUMBER_OF_BOMB_TEAMS; team_index++)
	{
		flag_index = find_netgame_flag(NULL, 0.0f, 0.0f, _netgame_flag_ctf_flag, (short)team_index);
		if (flag_index != NONE)
		{
			bomb_globals.goals[team_index] =
				TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, flag_index, struct scenario_netgame_flag)->position;
			bomb_globals.has_goal[team_index] = TRUE;
		}
		else
			error(_error_silent, "bomb: the map has no CTF flag of team %ld for its goal", team_index);
	}

	/* the bomb's spawn: the map's first ball spawn (of any team), else
	halfway between the goals and a little up (it falls to the ground) */
	flag_index = NONE;
	for (index = 0; index < scenario->netgame_flags.count && flag_index == NONE; index++)
	{
		if (TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag)->type ==
			_netgame_flag_oddball_ball_spawn)
		{
			flag_index = index;
		}
	}
	if (flag_index != NONE)
	{
		bomb_globals.spawn =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, flag_index, struct scenario_netgame_flag)->position;
	}
	else if (bomb_globals.has_goal[0] && bomb_globals.has_goal[1])
	{
		bomb_globals.spawn.x = (bomb_globals.goals[0].x + bomb_globals.goals[1].x) * 0.5f;
		bomb_globals.spawn.y = (bomb_globals.goals[0].y + bomb_globals.goals[1].y) * 0.5f;
		bomb_globals.spawn.z = MAX(bomb_globals.goals[0].z, bomb_globals.goals[1].z) + 1.0f;
	}
	else if (scenario->players.count > 0)
		bomb_globals.spawn = TAG_BLOCK_GET_ELEMENT(&scenario->players, 0, struct player_starting_location)->position;
	else
		bomb_globals.spawn = *global_origin3d;

	bomb_globals.round = 1;
	bomb_globals.round_ticks = game_engine_get_variant()->game_engine_variant.bomb.round_time > 0 ?
		game_engine_get_variant()->game_engine_variant.bomb.round_time * TICKS_PER_SECOND : NONE;
	/* (a client has the host's bomb: game_engine_bomb_read_network_state) */
	if (!network_game_distributed_client())
		bomb_globals.weapon_index = bomb_create();
	return TRUE;
}

static void bomb_engine_player_added(long player_index)
{
	(void)player_index;
}

static void bomb_engine_statistics_append(long statistic)
{
	(void)statistic;
}

static void bomb_engine_handle_message(void *message)
{
	(void)message;
}

/* the carrier's color: bright, between gold and white, pulsing; else its
team's again */
static void bomb_highlight_unit(long player_index, boolean highlight)
{
	struct player_datum *player = player_get(player_index);
	struct object_datum *object;
	struct object_definition *definition;
	real_rgb_color color;
	short index;

	if (player->unit_index == NONE)
		return;
	object = object_get(player->unit_index);
	definition = object_definition_get(object->definition_index);
	if (highlight)
	{
		real pulse = 0.5f + 0.5f * (real)sin(game_time_get() * 0.25f);

		color.red = 1.0f;
		color.green = 0.75f + 0.25f * pulse;
		color.blue = 0.1f + 0.6f * pulse;
	}
	else
		game_engine_player_get_change_color(&color, player_index);
	for (index = 0; index < definition->object.change_colors.count && index < NUMBER_OF_OBJECT_CHANGE_COLORS; index++)
	{
		struct object_change_color_definition *change_color = TAG_BLOCK_GET_ELEMENT(
			&definition->object.change_colors, index, struct object_change_color_definition);

		/* (the colors a function drives, the shields' glow, kept) */
		if (change_color->permutations.count > 0 && !change_color->scaled_by && !change_color->darken_by)
		{
			object->object.base_change_colors[index] = color;
			object->object.outgoing_change_colors[index] = color;
		}
	}
}

/* the host's: the carrier's overshield, charged past full again whenever it
has drained or been shot down and the shields are not stunned */
static void bomb_overshield_update(long player_index)
{
	struct player_datum *player = player_get(player_index);
	struct object_datum *object;

	if (player->unit_index == NONE)
		return;
	object = object_get(player->unit_index);
	if (TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		return;
	/* (a game without shields took the unit's: its own back) */
	if (object->object.maximum_shield_vitality <= 0.0f)
	{
		struct object_definition *definition = object_definition_get(object->definition_index);

		if (definition->object.collision_model.index != NONE)
		{
			struct collision_model *collision_model =
				collision_model_definition_get(definition->object.collision_model.index);

			if (collision_model)
				object->object.maximum_shield_vitality = collision_model->resistance.maximum_shield_vitality;
		}
		if (object->object.maximum_shield_vitality <= 0.0f)
			return;
	}
	if (object->object.shield_vitality <= 1.0f && object->object.shield_stun_ticks == 0 &&
		!TEST_FLAG(object->object.damage_flags, _object_shield_over_charging_bit))
	{
		object_double_charge_shield(player->unit_index);
	}
}

static void bomb_award_score(long player_index)
{
	struct player_datum *player = player_get(player_index);
	long team_index = player->team_index;

	if (!VALID_INDEX(team_index, NUMBER_OF_BOMB_TEAMS))
		return;
	bomb_globals.scores[team_index]++;
	player->statistics.multiplayer_statistics.ctf_statistics.flag_scores++;
	bomb_events.scores++;
	bomb_events.scorer = distributed_player_to_byte(player_index);
	bomb_events.scorer_team = (byte)team_index;
	bomb_log_player("score", player_index);
	bomb_log("score: red %ld blue %ld", (long)bomb_globals.scores[0], (long)bomb_globals.scores[1]);
	bomb_show_score(player_index, team_index);
	bomb_reset();
	if (bomb_globals.scores[team_index] < bomb_globals.score_to_win &&
		TEST_FLAG(game_engine_get_variant()->game_engine_variant.bomb.flags, _bomb_new_round_on_score_bit))
	{
		bomb_start_round(FALSE);
	}
}

static void bomb_engine_player_update(long player_index)
{
	struct player_datum *player = player_get(player_index);
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	struct player_traits traits;
	boolean carrying = player->unit_index != NONE && bomb_globals.weapon_index != NONE &&
		unit_has_weapon(player->unit_index, bomb_globals.weapon_index);

	if (carrying && bomb_globals.carrier != player_index)
	{
		bomb_globals.carrier = player_index;
		bomb_log_player("grab", player_index);
	}
	else if (!carrying && bomb_globals.carrier == player_index)
	{
		bomb_globals.carrier = NONE;
		bomb_log_player("drop", player_index);
	}
	if (carrying && game_engine_can_score() && VALID_INDEX(absolute_index, HALO_PORT_MAXIMUM_NETWORK_PLAYERS))
		bomb_globals.carry_ticks[absolute_index]++;

	/* the traits (every machine: a client predicts its own player's speed,
	which the host's movement checks scale by) */
	bomb_player_traits(player_index, &traits);
	player->speed_multiplier = traits.speed / 100.0f;
	if (carrying)
		game_engine_player_depower_active_camo(player_index);
	if (VALID_INDEX(absolute_index, HALO_PORT_MAXIMUM_NETWORK_PLAYERS))
	{
		boolean highlight = carrying && traits.highlight;

		if (highlight || bomb_globals.highlighted[absolute_index])
			bomb_highlight_unit(player_index, highlight);
		bomb_globals.highlighted[absolute_index] = highlight;
	}

	/* (a client has the host's shields, scores and rounds) */
	if (network_game_distributed_client())
		return;
	if (carrying && traits.shields == _trait_shields_overshield)
		bomb_overshield_update(player_index);
	if (carrying && game_engine_can_score() && VALID_INDEX(player->team_index, NUMBER_OF_BOMB_TEAMS))
	{
		long goal_team = (player->team_index + 1) % NUMBER_OF_BOMB_TEAMS;
		struct object_datum *unit = object_get(player->unit_index);

		if (bomb_globals.has_goal[goal_team] &&
			distance_squared3d(&bomb_globals.goals[goal_team], &unit->object.position) <
				BOMB_GOAL_RADIUS * BOMB_GOAL_RADIUS)
		{
			bomb_globals.arm_ticks++;
			if (bomb_globals.arm_ticks >=
				game_engine_get_variant()->game_engine_variant.bomb.arm_time * TICKS_PER_SECOND)
			{
				bomb_award_score(player_index);
			}
		}
		else
			bomb_globals.arm_ticks = 0;
	}
	else if (bomb_globals.carrier == NONE)
		bomb_globals.arm_ticks = 0;
}

static void bomb_engine_weapon_update(long weapon_index, struct weapon_datum *weapon)
{
	real_point3d position;
	long reset_time = game_engine_get_variant()->game_engine_variant.bomb.reset_time;

	/* (a client may not have the host's state yet: the bomb is its only
	ball) */
	if (bomb_globals.weapon_index == NONE && network_game_distributed_client())
		bomb_globals.weapon_index = weapon_index;
	if (weapon_index != bomb_globals.weapon_index)
		return;
	item_get_position_even_if_in_inventory(weapon_index, &position);
	game_engine_set_goal_position(BOMB_GOAL_BOMB, &position, 0.0f, "ball_blue", NONE, NONE, bomb_globals.carrier);

	/* (a client has the bomb where the host puts it) */
	if (!network_game_distributed_client() && reset_time > 0 &&
		(unsigned long)(game_time_get() - weapon->item.last_owned_time) > (unsigned long)(reset_time * TICKS_PER_SECOND) &&
		TEST_FLAG(weapon->object.flags, _object_connected_to_map_bit) && weapon->object.parent_object_index == NONE)
	{
		bomb_log("reset: untouched at (%.2f %.2f %.2f)", weapon->object.position.x, weapon->object.position.y,
			weapon->object.position.z);
		bomb_reset();
		bomb_events.resets++;
		bomb_show_reset();
	}
}

static boolean bomb_engine_picking_up(long weapon_index, long player_index)
{
	if (weapon_index != bomb_globals.weapon_index || player_index == NONE)
		return TRUE;
	if (!network_game_distributed_client() && game_engine_can_score())
	{
		player_get(player_index)->statistics.multiplayer_statistics.ctf_statistics.flag_grabs++;
		bomb_events.carries++;
		bomb_events.carrier = distributed_player_to_byte(player_index);
		bomb_show_carry(player_index);
	}
	return TRUE;
}

static void bomb_engine_weapon_dropped(long weapon_index)
{
	(void)weapon_index;
}

static void bomb_engine_update(void)
{
	long team_index;
	boolean game_over = FALSE;

	/* the goals: each team's shown to the team that attacks it */
	for (team_index = 0; team_index < NUMBER_OF_BOMB_TEAMS; team_index++)
	{
		if (bomb_globals.has_goal[team_index])
		{
			game_engine_set_goal_position((short)(BOMB_GOAL_FIRST_TEAM + team_index), &bomb_globals.goals[team_index],
				0.0f, "default", NONE, (short)((team_index + 1) % NUMBER_OF_BOMB_TEAMS), NONE);
		}
	}
	if (game_time_get() - bomb_globals.round_start_time == BOMB_ROUND_ANNOUNCEMENT_TICKS)
		game_engine_play_multiplayer_sound(_bomb_sound_play_ball);

	if (!game_engine_can_score())
		return;
	/* the round's time (a client counts it too; the host's state corrects
	it) */
	if (bomb_globals.round_ticks != NONE && bomb_globals.round_ticks > 0)
	{
		bomb_globals.round_ticks--;
		if (bomb_globals.round_ticks > 0 && bomb_globals.round_ticks <= BOMB_ROUND_WARNING_TICKS &&
			bomb_globals.round_ticks % TICKS_PER_SECOND == 0)
		{
			game_engine_play_multiplayer_sound(_bomb_sound_countdown);
		}
		if (bomb_globals.round_ticks == 0 && !network_game_distributed_client())
			bomb_start_round(TRUE);
	}
	for (team_index = 0; team_index < NUMBER_OF_BOMB_TEAMS; team_index++)
	{
		if (bomb_globals.score_to_win > 0 && bomb_globals.scores[team_index] >= bomb_globals.score_to_win)
			game_over = TRUE;
	}
	/* (a client ends the game when the host has) */
	if (game_over && !network_game_distributed_client())
		game_engine_end_game();
}

static long bomb_get_score(long player_index, enum get_score_type score_type)
{
	struct player_datum *player = player_get(player_index);

	if (score_type == _get_score_individual)
		return player->statistics.multiplayer_statistics.ctf_statistics.flag_scores;
	return VALID_INDEX(player->team_index, NUMBER_OF_BOMB_TEAMS) ? bomb_globals.scores[player->team_index] : 0;
}

static wchar_t *bomb_format_player_score(long player_index, wchar_t *buffer)
{
	usprintf(buffer, L"%d", player_get(player_index)->statistics.multiplayer_statistics.ctf_statistics.flag_scores);
	return buffer;
}

static wchar_t *bomb_format_score_name(wchar_t *buffer)
{
	/* (its callers' buffers hold 256) */
	ustrncpy_terminated(buffer, L"Rounds", 256);
	return buffer;
}

static wchar_t *bomb_format_team_name(long team_index, wchar_t *buffer)
{
	usprintf(buffer, L"%d", VALID_INDEX(team_index, NUMBER_OF_BOMB_TEAMS) ? (int)bomb_globals.scores[team_index] : 0);
	return buffer;
}

static boolean bomb_engine_allow_pick_up(long unit_index, long weapon_index)
{
	(void)unit_index;
	(void)weapon_index;
	return TRUE;
}

static void bomb_engine_player_damaged_player(long damaging_player_index, long dead_player_index, boolean damage_type)
{
	(void)damaging_player_index;
	(void)dead_player_index;
	(void)damage_type;
}

static void bomb_engine_player_killed_player(long killing_player_index, long killing_object_index,
	long dead_player_index, boolean friendly_fire)
{
	long killer = killing_player_index != NONE ? DATUM_INDEX_TO_ABSOLUTE_INDEX(killing_player_index) : NONE;

	(void)killing_object_index;
	if (VALID_INDEX(killer, HALO_PORT_MAXIMUM_NETWORK_PLAYERS) && killing_player_index != dead_player_index &&
		!friendly_fire && game_engine_can_score())
	{
		if (dead_player_index == bomb_globals.carrier)
			bomb_globals.carrier_kills[killer]++;
		if (killing_player_index == bomb_globals.carrier)
			bomb_globals.kills_as_carrier[killer]++;
	}
	if (dead_player_index == bomb_globals.carrier)
	{
		bomb_log_player("drop (the carrier killed)", dead_player_index);
		bomb_globals.carrier = NONE;
	}
}

static boolean bomb_engine_format_message(long player_index, long message, long message_data, wchar_t *buffer,
	long buffer_size)
{
	struct player_datum *player = player_get(player_index);
	long team_index = VALID_INDEX(player->team_index, NUMBER_OF_BOMB_TEAMS) ? player->team_index : 0;
	long other_team_index = (team_index + 1) % NUMBER_OF_BOMB_TEAMS;

	(void)message_data;
	switch (message)
	{
	case _bomb_message_you_scored:
		usnprintf(buffer, buffer_size, L"You scored! %d to %d", (int)bomb_globals.scores[team_index],
			(int)bomb_globals.scores[other_team_index]);
		break;
	case _bomb_message_ally_scored:
		usnprintf(buffer, buffer_size, L"Your team scored! %d to %d", (int)bomb_globals.scores[team_index],
			(int)bomb_globals.scores[other_team_index]);
		break;
	case _bomb_message_enemy_scored:
		usnprintf(buffer, buffer_size, L"The enemy scored. %d to %d", (int)bomb_globals.scores[team_index],
			(int)bomb_globals.scores[other_team_index]);
		break;
	case _bomb_message_you_have_the_bomb:
		usnprintf(buffer, buffer_size, L"You have the bomb!");
		break;
	case _bomb_message_ally_has_the_bomb:
		usnprintf(buffer, buffer_size, L"Your team has the bomb.");
		break;
	case _bomb_message_enemy_has_the_bomb:
		usnprintf(buffer, buffer_size, L"The enemy has the bomb!");
		break;
	case _bomb_message_bomb_reset:
		usnprintf(buffer, buffer_size, L"The bomb was reset.");
		break;
	case _bomb_message_new_round:
		usnprintf(buffer, buffer_size, L"Round %d", (int)bomb_globals.round);
		break;
	case _bomb_message_round_over:
		usnprintf(buffer, buffer_size, L"Time is up. Round %d", (int)bomb_globals.round);
		break;
	default:
		return FALSE;
	}
	buffer[buffer_size - 1] = 0;
	return TRUE;
}

/* a team's starting locations nearest its own goal first */
static real bomb_engine_starting_location_rating(long player_index,
	struct player_starting_location const *starting_location)
{
	struct player_datum *player = player_get(player_index);
	real distance_squared;

	if (!VALID_INDEX(player->team_index, NUMBER_OF_BOMB_TEAMS) || !bomb_globals.has_goal[player->team_index])
		return 1.0f;
	distance_squared = distance_squared3d(&bomb_globals.goals[player->team_index], &starting_location->position);
	distance_squared = PIN(distance_squared, 1.0f, 10000.0f);
	return 1.0f / distance_squared;
}

static void bomb_engine_prespawn_player_update(long player_index)
{
	(void)player_index;
}

static boolean bomb_engine_test_flag(long flag)
{
	/* (0: team starting locations, as CTF's) */
	return flag == 0;
}

/* ---------- what the host's events show every machine's players (the
host's from the event, a client's from the host's state) */

static void bomb_show_score(long player_index, long team_index)
{
	game_engine_play_multiplayer_sound(team_index == _team_red ? _bomb_sound_red_team_score :
		_bomb_sound_blue_team_score);
	if (player_index != NONE)
	{
		game_show_score_you_ally_enemy(player_index, _bomb_message_you_scored, _bomb_message_ally_scored,
			_bomb_message_enemy_scored, player_index);
	}
}

static void bomb_show_carry(long player_index)
{
	/* (nothing more: the HUD's line says who has the bomb,
	game_engine_bomb_hud_text) */
	(void)player_index;
}

static void bomb_show_reset(void)
{
	game_engine_play_multiplayer_sound(_bomb_sound_hill_move);
	game_show_score(NONE, _bomb_message_bomb_reset);
}

static void bomb_show_round(boolean timed_out)
{
	game_show_score(NONE, timed_out ? _bomb_message_round_over : _bomb_message_new_round);
}

/* ---------- the HUD (game_engine.c draws it) */

boolean game_engine_bomb_hud_text(
	long player_index,
	wchar_t *score_line,
	wchar_t *carrier_line,
	long size)
{
	struct player_datum *player;
	long team_index;
	wchar_t time_string[32];

	score_line[0] = 0;
	carrier_line[0] = 0;
	if (!bomb_running() || player_index == NONE || !game_engine_can_score())
		return FALSE;
	player = player_get(player_index);
	team_index = VALID_INDEX(player->team_index, NUMBER_OF_BOMB_TEAMS) ? player->team_index : 0;
	time_string[0] = 0;
	if (bomb_globals.round_ticks != NONE)
		ticks_to_unicode_time_string(MAX(bomb_globals.round_ticks, 0), NUMBEROF(time_string), time_string);
	usnprintf(score_line, size, L"RED %d - %d BLUE     ROUND %d%s%s", (int)bomb_globals.scores[_team_red],
		(int)bomb_globals.scores[_team_blue], (int)bomb_globals.round, time_string[0] ? L"     " : L"", time_string);
	score_line[size - 1] = 0;
	if (bomb_globals.carrier == player_index)
	{
		long arm_time = game_engine_get_variant()->game_engine_variant.bomb.arm_time;

		if (arm_time > 0 && bomb_globals.arm_ticks > 0)
		{
			usnprintf(carrier_line, size, L"ARMING %d",
				(int)((arm_time * TICKS_PER_SECOND - bomb_globals.arm_ticks + TICKS_PER_SECOND - 1) / TICKS_PER_SECOND));
		}
		else
		{
			long goal_team = (team_index + 1) % NUMBER_OF_BOMB_TEAMS;
			long tenths = 0;

			/* (and how far it is, in the nav points' meters: the goal's
			arrow at the screen's edge shows none) */
			if (bomb_globals.has_goal[goal_team] && player->unit_index != NONE)
			{
				tenths = (long)(sqrt(distance_squared3d(&bomb_globals.goals[goal_team],
					&object_get(player->unit_index)->object.position)) * 30.48f);
			}
			usnprintf(carrier_line, size, L"YOU HAVE THE BOMB: TAKE IT TO THE %s GOAL   %d.%dm",
				goal_team == _team_red ? L"RED" : L"BLUE", (int)(tenths / 10), (int)(tenths % 10));
		}
	}
	else if (bomb_globals.carrier != NONE)
	{
		usnprintf(carrier_line, size, player_get(bomb_globals.carrier)->team_index == player->team_index ?
			L"YOUR TEAM HAS THE BOMB" : L"THE ENEMY HAS THE BOMB");
	}
	carrier_line[size - 1] = 0;
	return TRUE;
}

/* ---------- the engine table */

struct game_engine bomb_engine =
{
	"bomb",
	game_engine_bomb,
	bomb_engine_nothing,
	bomb_engine_initialize_for_new_map,
	bomb_engine_nothing,
	bomb_engine_player_added,
	bomb_engine_nothing,
	bomb_engine_nothing,
	bomb_engine_statistics_append,
	bomb_engine_handle_message,
	bomb_engine_handle_message,
	bomb_engine_nothing,
	bomb_engine_nothing,
	bomb_engine_player_update,
	bomb_engine_weapon_update,
	bomb_engine_picking_up,
	bomb_engine_weapon_dropped,
	bomb_engine_update,
	bomb_get_score,
	bomb_format_player_score,
	bomb_format_score_name,
	bomb_format_team_name,
	bomb_engine_allow_pick_up,
	bomb_engine_player_damaged_player,
	bomb_engine_player_killed_player,
	bomb_engine_format_message,
	bomb_engine_starting_location_rating,
	bomb_engine_prespawn_player_update,
	/* (player_update NULL: the default loadout, game_engine.c) */
	NULL,
	NULL,
	NULL,
	bomb_engine_test_flag,
	NULL,
	NULL,
};

/* ---------- the distributed netcode's state (network_distributed.c) */

struct bomb_network_state
{
	long weapon_index;
	long scores[NUMBER_OF_BOMB_TEAMS];
	long round;
	long round_ticks;
	long arm_ticks;
	/* the host's events (bomb_events) */
	byte score_count;
	byte scorer;
	byte scorer_team;
	byte carry_count;
	byte carrier;
	byte reset_count;
	byte round_count;
	byte round_over_count;
	/* the carrier the host sees now (distributed_player_to_byte), for the
	client's check of its own */
	byte carrier_now;
	byte pad[3];
};

typedef char verify_bomb_network_state_size[sizeof(struct bomb_network_state) == 0x24 ? 1 : -1];

long game_engine_bomb_write_network_state(
	byte *buffer,
	long size)
{
	struct bomb_network_state state;

	if (size < (long)sizeof(state))
		return 0;
	csmemset(&state, 0, sizeof(state));
	state.weapon_index = bomb_globals.weapon_index;
	state.scores[0] = bomb_globals.scores[0];
	state.scores[1] = bomb_globals.scores[1];
	state.round = bomb_globals.round;
	state.round_ticks = bomb_globals.round_ticks;
	state.arm_ticks = bomb_globals.arm_ticks;
	state.score_count = bomb_events.scores;
	state.scorer = bomb_events.scorer;
	state.scorer_team = bomb_events.scorer_team;
	state.carry_count = bomb_events.carries;
	state.carrier = bomb_events.carrier;
	state.reset_count = bomb_events.resets;
	state.round_count = bomb_events.rounds;
	state.round_over_count = bomb_events.round_overs;
	state.carrier_now = distributed_player_to_byte(bomb_globals.carrier);
	csmemcpy(buffer, &state, sizeof(state));
	return sizeof(state);
}

boolean game_engine_bomb_read_network_state(
	byte const *buffer,
	long size,
	boolean first)
{
	struct bomb_network_state state;

	if (size != (long)sizeof(state))
		return FALSE;
	csmemcpy(&state, buffer, sizeof(state));
	/* the playtest's checks: what this client had against the host's (its
	carrier from the units' hands, its round clock counted down itself) */
	if (!first)
	{
		static long carrier_disagreements = 0;
		long host_carrier = distributed_player_from_byte(state.carrier_now);

		if (host_carrier != bomb_globals.carrier)
		{
			/* (a pickup a state's interval old is no disagreement: twice
			running is) */
			if (++carrier_disagreements == 2)
			{
				bomb_log("DISAGREE carrier: host %ld, client %ld",
					host_carrier != NONE ? (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(host_carrier) : -1L,
					bomb_globals.carrier != NONE ? (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(bomb_globals.carrier) : -1L);
			}
		}
		else
			carrier_disagreements = 0;
		if (state.scores[0] < bomb_globals.scores[0] || state.scores[1] < bomb_globals.scores[1])
		{
			bomb_log("DISAGREE score: host red %ld blue %ld, client red %ld blue %ld (the host's went down)",
				(long)state.scores[0], (long)state.scores[1], (long)bomb_globals.scores[0], (long)bomb_globals.scores[1]);
		}
		if (state.round < bomb_globals.round)
			bomb_log("DISAGREE round: host %ld, client %ld", (long)state.round, (long)bomb_globals.round);
		if (state.round == bomb_globals.round && state.round_ticks != NONE && bomb_globals.round_ticks != NONE &&
			(state.round_ticks - bomb_globals.round_ticks > 2 * TICKS_PER_SECOND ||
				bomb_globals.round_ticks - state.round_ticks > 2 * TICKS_PER_SECOND))
		{
			bomb_log("DISAGREE round time: host %ld ticks left, client %ld", (long)state.round_ticks,
				(long)bomb_globals.round_ticks);
		}
		if (state.score_count != bomb_events.scores)
		{
			bomb_log("score (the host's): player %d team %d, red %ld blue %ld", (int)state.scorer, (int)state.scorer_team,
				(long)state.scores[0], (long)state.scores[1]);
		}
		if (state.reset_count != bomb_events.resets)
			bomb_log("reset (the host's)");
		if (state.round_count != bomb_events.rounds)
			bomb_log("round %ld starts (the host's; %s)", (long)state.round,
				state.round_over_count != bomb_events.round_overs ? "time" : "goal");
	}
	/* (the bomb: a weapon of the host's, else none yet) */
	bomb_globals.weapon_index = state.weapon_index != NONE &&
		object_try_and_get_and_verify_type(state.weapon_index, _object_mask_weapon) ? state.weapon_index : NONE;
	/* the scores and round first: the messages show them */
	bomb_globals.scores[0] = MAX(state.scores[0], 0);
	bomb_globals.scores[1] = MAX(state.scores[1], 0);
	if (state.round != bomb_globals.round)
		bomb_globals.round_start_time = game_time_get();
	bomb_globals.round = state.round;
	bomb_globals.round_ticks = state.round_ticks;
	bomb_globals.arm_ticks = MAX(state.arm_ticks, 0);
	if (!first)
	{
		if (state.carry_count != bomb_events.carries)
		{
			long player_index = distributed_player_from_byte(state.carrier);

			if (player_index != NONE)
				bomb_show_carry(player_index);
		}
		if (state.score_count != bomb_events.scores)
			bomb_show_score(distributed_player_from_byte(state.scorer), state.scorer_team);
		if (state.reset_count != bomb_events.resets)
			bomb_show_reset();
		if (state.round_count != bomb_events.rounds)
			bomb_show_round(state.round_over_count != bomb_events.round_overs);
	}
	bomb_events.scores = state.score_count;
	bomb_events.scorer = state.scorer;
	bomb_events.scorer_team = state.scorer_team;
	bomb_events.carries = state.carry_count;
	bomb_events.carrier = state.carrier;
	bomb_events.resets = state.reset_count;
	bomb_events.rounds = state.round_count;
	bomb_events.round_overs = state.round_over_count;
	return TRUE;
}

/* the stock "Picked up the odd ball" line, which a Bomb game does not show
(hud.c): the HUD's own line says who has the bomb */
boolean game_engine_bomb_hides_pickup(
	long weapon_definition_index)
{
	return bomb_running() && weapon_definition_index != NONE &&
		weapon_definition_index == get_ball_definition_index();
}

/* ---------- the game's report (game_engine.c, port/linux/game/game_stats.c;
docs/bomb.md, Reports) */

static char const *bomb_preset_name(void)
{
	switch (game_engine_get_variant()->game_engine_variant.bomb.traits)
	{
	case _bomb_traits_grifball: return "grifball";
	case _bomb_traits_open_pitch: return "open_pitch";
	}
	return "bomb";
}

/* a player's line's members: their goals, carries, seconds with the bomb,
carriers killed and kills as the carrier (none in another game type) */
long game_engine_bomb_report_player(
	long player_index,
	char *text,
	long size)
{
	struct player_datum *player;
	long index;
	int written;

	if (size > 0)
		text[0] = 0;
	if (!bomb_running() || player_index == NONE || size <= 0)
		return 0;
	player = player_get(player_index);
	index = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	if (!VALID_INDEX(index, HALO_PORT_MAXIMUM_NETWORK_PLAYERS))
		return 0;
	written = snprintf(text, (size_t)size, "\"bomb_goals\": %d, \"bomb_carries\": %d, \"bomb_carry_time\": %ld, "
		"\"bomb_carrier_kills\": %d, \"bomb_kills_as_carrier\": %d",
		(int)player->statistics.multiplayer_statistics.ctf_statistics.flag_scores,
		(int)player->statistics.multiplayer_statistics.ctf_statistics.flag_grabs,
		(long)(bomb_globals.carry_ticks[index] / TICKS_PER_SECOND), (int)bomb_globals.carrier_kills[index],
		(int)bomb_globals.kills_as_carrier[index]);
	if (written < 0 || written >= size)
	{
		text[0] = 0;
		return 0;
	}
	return written;
}

/* the game's members: Bomb's own object (its preset, the rounds played and
won by each team, its round and arm times) */
long game_engine_bomb_report_game(
	char *text,
	long size)
{
	struct bomb_variant const *bomb;
	int written;

	if (size > 0)
		text[0] = 0;
	if (!bomb_running() || size <= 0)
		return 0;
	bomb = &game_engine_get_variant()->game_engine_variant.bomb;
	written = snprintf(text, (size_t)size, "\"bomb\": {\"preset\": \"%s\", \"rounds\": %ld, \"team_rounds\": [%ld, %ld], "
		"\"round_time\": %ld, \"arm_time\": %ld}", bomb_preset_name(), (long)bomb_globals.round,
		(long)bomb_globals.scores[0], (long)bomb_globals.scores[1], (long)bomb->round_time, (long)bomb->arm_time);
	if (written < 0 || written >= size)
	{
		text[0] = 0;
		return 0;
	}
	return written;
}

/* ---------- the network test's (port/linux/game/network_test.c) */

long game_engine_bomb_weapon(
	void)
{
	return bomb_running() ? bomb_globals.weapon_index : NONE;
}

boolean game_engine_bomb_goal(
	long team_index,
	real_point3d *position)
{
	if (!bomb_running() || !VALID_INDEX(team_index, NUMBER_OF_BOMB_TEAMS) || !bomb_globals.has_goal[team_index])
		return FALSE;
	*position = bomb_globals.goals[team_index];
	return TRUE;
}

long game_engine_bomb_team_score(
	long team_index)
{
	return bomb_running() && VALID_INDEX(team_index, NUMBER_OF_BOMB_TEAMS) ? bomb_globals.scores[team_index] : 0;
}

long game_engine_bomb_round(
	void)
{
	return bomb_running() ? bomb_globals.round : 0;
}

#endif /* HALO_GAME_BROWSER */
