/*
GAME_ENGINE_BOMB.C

port: Bomb, the game type Bungie cut (this file was empty in the shipped
game), and Grifball, a preset of it (docs/bomb.md). Two teams, one bomb in
the middle of the map: a team scores by carrying it into the other team's
goal, which ends the round. The goals are the map's CTF flag markers and the
bomb is the map's oddball ball, on the ground halfway between the goals (or
where the host's game.bomb_spawns puts it on that map), so every map with
two CTF bases plays it. A known Grifball court (bomb_courts) has its own
ball's spot, goal volumes and out of bounds instead.

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
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "cache/cache_files.h"
#include "effects/effects.h"
#include "game/game_globals.h"
#include "items/projectile_definitions.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/unicode.h"
#include "units/units.h"
#include "game/game_engine_king.h"
#include "interface/hud.h"
#include "interface/hud_messaging.h"
#include "interface/hud_unit.h"
#include "rasterizer/rasterizer_model_types.h"
#include "render/render.h"

#ifdef HALO_GAME_BROWSER

#include "../../port/linux/src/delta_peer.h"
#include "halo_map_families.h"
#include "main/main.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* port_config.c's */
int config_boolean(char const *name);
double config_real(char const *name);
long config_integer(char const *name);
char const *config_string(char const *name);

/* ---------- constants */

enum
{
	NUMBER_OF_BOMB_TEAMS = 2,
	/* the goals the game shows (nav points, as CTF's and Oddball's): the
	bomb lying loose (to everyone), each team's goal (to the team that
	attacks it), and its carrier (a target to the other team, one to defend
	to its own) */
	BOMB_GOAL_BOMB = 0,
	BOMB_GOAL_FIRST_TEAM = 1,
	BOMB_GOAL_CARRIER_ENEMIES = 3,
	BOMB_GOAL_CARRIER_ALLIES = 4,
	/* the goal's outline's sides (the hill's, King of the Hill's) */
	BOMB_GOAL_OUTLINE_SIDES = 16,
	/* "play ball" this long into a round */
	BOMB_ROUND_ANNOUNCEMENT_TICKS = 2 * TICKS_PER_SECOND,
	/* the round's last seconds, counted aloud */
	BOMB_ROUND_WARNING_TICKS = 5 * TICKS_PER_SECOND,
};

/* how near the carrier stands to the goal's marker to arm it (CTF's) */
#define BOMB_GOAL_RADIUS 1.0f
/* the goal's outline's band: its height, and how near the view (from the
goal's center, across the ground) it starts to sink and is gone */
#define BOMB_GOAL_OUTLINE_HEIGHT 0.3f
#define BOMB_GOAL_OUTLINE_FADE_FAR 4.0f
#define BOMB_GOAL_OUTLINE_FADE_NEAR 1.5f

/* the planted bomb's save (game.bomb_arm_time): a defender this near the
bomb while it arms saves it (world units: about 2 meters) */
#define BOMB_SAVE_RADIUS 0.6f
/* the longest it arms (seconds) */
#define BOMB_ARM_TIME_MAXIMUM 10.0

/* the bomb's blast at the goal it is scored in: everyone this near the
goal dies (world units: about 15 meters, a Blood Gulch base and around it),
and the bodies are thrown, harder nearer */
#define BOMB_BLAST_RADIUS 5.0f
#define BOMB_BLAST_THROW_UP 0.3f
#define BOMB_BLAST_THROW_OUT 0.45f

/* the bomb's spawn: the ground found straight down from this high over the
goals (world units, about 30 meters: over Blood Gulch's middle, under an
indoor map's next floor up), or over a point game.bomb_spawns gives with its
height, through this far, and the bomb dropped from this high over it */
#define BOMB_SPAWN_TEST_HEIGHT 10.0f
#define BOMB_SPAWN_POINT_TEST_HEIGHT 1.0f
#define BOMB_SPAWN_TEST_DEPTH 20.0f
#define BOMB_SPAWN_DROP_HEIGHT 0.25f

/* Open Pitch's carrier, on top of everyone's (percent), no overshield: a
melee blow, the only damage of a game without weapons, takes 93% of its
health (everyone's: three times theirs), so it lives through the first and
fumbles the bomb, and the second kills it (docs/bomb.md) */
#define BOMB_OPEN_PITCH_CARRIER_HEALTH 400
#define BOMB_OPEN_PITCH_CARRIER_RESISTANCE 400

enum
{
	/* after a goal: the bomb planted in the goal counts down this long
	(everyone free to run), goes off, and the blast is seen this long before
	the next round (the two a byte in the network state: 255 ticks at most) */
	BOMB_COUNTDOWN_TICKS = 3 * TICKS_PER_SECOND,
	BOMB_BLAST_PAUSE_TICKS = 5 * TICKS_PER_SECOND / 2,
	/* the globals' grenades the blast is drawn with */
	MAXIMUM_BOMB_BLAST_EFFECTS = 2,
	/* the most weapons a court gives (bomb_courts) */
	MAXIMUM_BOMB_COURT_WEAPONS = 2,
	/* the most out of bounds volumes a court names (bomb_courts) */
	MAXIMUM_BOMB_COURT_OUT_OF_BOUNDS = 2,
};

/* the shipped multiplayer sounds Bomb borrows (game_engine_multiplayer_sounds.c):
no announcer line of another game type's */
enum
{
	_bomb_sound_play_ball = 0,
	_bomb_sound_blue_team_score = 0xA,
	_bomb_sound_red_team_score = 0xD,
	_bomb_sound_countdown = 0x1A,
	_bomb_sound_hill_move = 0x1E,
	_bomb_sound_countdown_end = 0x2A,
	/* (not every map's globals have it: hill_move's tone where not) */
	_bomb_sound_ting = 0x2B,
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
	/* the carrier's state message (the HUD's line that CTF's and Oddball's
	carriers have) */
	_bomb_message_carrying,
	_bomb_message_arming,
};

/* ---------- globals */

static struct
{
	/* each team's goal (its CTF flag marker, or a court's trigger volume:
	the volume's floor, in its middle): a team scores at the other's */
	boolean has_goal[NUMBER_OF_BOMB_TEAMS];
	real_point3d goals[NUMBER_OF_BOMB_TEAMS];
	/* a court's goals' trigger volumes (NONE: the marker's radius) */
	short goal_volumes[NUMBER_OF_BOMB_TEAMS];
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
	/* the host's: the bomb planted in a goal, arming (game.bomb_arm_time):
	its ticks left (0: none), the goal's team and the planter. A defender
	who reaches it first saves it; else it scores once */
	long plant_ticks;
	long plant_goal_team;
	long plant_scorer;
	/* the map's energy sword players can use (NONE: none: fists alone) */
	long sword_definition_index;
	/* ... and where it has none, the globals' pistol, given unseen: the
	"fists" (its pistol whip, with first person arms; it can't shoot and
	isn't drawn, dropped or picked up: game_engine_bomb_hides_weapon) */
	long fist_definition_index;
	/* the host giving them (no one picks them up otherwise) */
	boolean giving_fists;
	/* a court's (bomb_courts): its scripts off, and its weapons players
	get in place of the sword (NONE: a weapon the map doesn't have) */
	boolean court;
	long court_weapons[MAXIMUM_BOMB_COURT_WEAPONS];
	/* a court's own ball (NONE: none), which is not placed: the bomb starts
	where it would be */
	long court_ball_definition_index;
	/* a court's out of bounds trigger volumes (NONE: none): a player in one
	is killed, and the bomb lying in one is reset, as its scripts did */
	short out_of_bounds_volumes[MAXIMUM_BOMB_COURT_OUT_OF_BOUNDS];
	/* the players drawn in the carrier's color (by absolute index) */
	boolean highlighted[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	/* each player's (by absolute index), for the game's report: ticks with
	the bomb, carriers killed, kills while carrying (every machine counts
	them from what it sees: the carrier and the deaths are every
	machine's) */
	long carry_ticks[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short carrier_kills[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short kills_as_carrier[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	/* after a goal, the ticks before the next round (the planted bomb's
	countdown, then the blast's pause; 0: playing): the host's, which a
	client has from it and counts down too */
	long hold_ticks;
	/* the countdown's second last shown (0: none) */
	long countdown_shown;
	/* the goal's: the goal the bomb is planted in (its team's), the scorer
	and their team, whether a round follows, and whether its blast has gone
	off (on this machine) */
	long blast_goal_team;
	long blast_scorer;
	long blast_team;
	boolean blast_next_round;
	boolean blast_done;
	/* the blast's effects */
	long blast_effects[MAXIMUM_BOMB_BLAST_EFFECTS];
	/* the host's: the scoring team's players its blast kills, which count
	nothing (by absolute index) */
	boolean blast_silent[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
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
get the court's own weapons, the first in their hands. What the scripts
did that Bomb needs, the court's entry names: its ball (the bomb starts
where the map places it, and it is not placed), its goals' and its out of
bounds trigger volumes */
static struct bomb_court
{
	unsigned long version;
	char const *weapons[MAXIMUM_BOMB_COURT_WEAPONS];
	char const *ball;
	/* each team's goal, red's and blue's (the one it defends) */
	char const *goal_volumes[NUMBER_OF_BOMB_TEAMS];
	char const *out_of_bounds_volumes[MAXIMUM_BOMB_COURT_OUT_OF_BOUNDS];
} const bomb_courts[] =
{
	/* grifball@ce, HaloNet's Grifball court (166 MB, a CTF map): its
	scripts make its own ball (weapons\grifball\grifball, placed on the
	court's middle), score red's when the ball is in "bluegoal" (blue's
	end) and blue's in "redgoal", blow it up there with a tank shell's
	explosion that kills whoever is near, and kill a player in "OOB" (the
	ball in it goes back to its spawn); its CTF flags are off the court,
	and its energy sword detonates when dropped, so bomb_find_sword passes
	it by */
	{ 0x62b910c6UL, {
		"h3mt\\weapons\\covenant\\gravity_hammer\\gravity_hammer",
		"weapons\\energy_sword\\energy_sword backup",
	}, "weapons\\grifball\\grifball", { "redgoal", "bluegoal" }, { "OOB", NULL } },
};

/* network_game_globals.c's */
boolean network_game_distributed_client(void);
/* port/linux/game/network_distributed.c's */
byte distributed_player_to_byte(long player_index);
long distributed_player_from_byte(byte player_index);
boolean network_distributed_death_silent(long dead_player_index);
/* port/linux/game/network_damage.c's */
void network_damage_host_kill_for_player(long object_index, long player_index);
/* damage.c's */
void damage_kill_object_no_statistics(long object_index);
/* port/linux/game/team_change.c's */
boolean team_change_round_started(long player_index);

static void bomb_show_score(long player_index, long team_index);
static void bomb_show_carry(long player_index);
static void bomb_show_reset(void);
static void bomb_show_round(boolean timed_out);
static void bomb_blast_find_effects(void);

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

static void bomb_player_movement_clean(struct player_traits *traits)
{
	traits->jump = bomb_trait_pin(traits->jump, PLAYER_TRAIT_JUMP_MINIMUM, PLAYER_TRAIT_JUMP_MAXIMUM);
	traits->gravity = bomb_trait_pin(traits->gravity, PLAYER_TRAIT_GRAVITY_MINIMUM, PLAYER_TRAIT_GRAVITY_MAXIMUM);
	traits->melee = bomb_trait_pin(traits->melee, PLAYER_TRAIT_MELEE_MINIMUM, PLAYER_TRAIT_MELEE_MAXIMUM);
	traits->pad2 = 0;
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
	bomb_player_movement_clean(traits);
}

static void bomb_player_traits_none(struct player_traits *traits)
{
	csmemset(traits, 0, sizeof(*traits));
	traits->health = 100;
	traits->damage = 100;
	traits->resistance = 100;
	traits->speed = 100;
	traits->shields = _trait_shields_unchanged;
	traits->jump = 100;
	traits->gravity = 100;
	traits->melee = 100;
}

/* Open Pitch's floaty movement: a higher jump and less gravity (in
percent), and a longer melee reach for the defenders (the carrier's back to
100%: 67% of 150%) */
enum
{
	BOMB_OPEN_PITCH_JUMP = 120,
	BOMB_OPEN_PITCH_GRAVITY = 65,
	BOMB_OPEN_PITCH_MELEE = 150,
};

static void bomb_open_pitch_movement(struct game_traits *traits)
{
	traits->everyone.jump = BOMB_OPEN_PITCH_JUMP;
	traits->everyone.gravity = BOMB_OPEN_PITCH_GRAVITY;
	traits->everyone.melee = BOMB_OPEN_PITCH_MELEE;
	traits->carrier.melee = 67;
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
	if (variant->game_engine_variant.bomb.traits == _bomb_traits_open_pitch)
		bomb_open_pitch_movement(traits);
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
		/* Grifball's, and fast: 175% everyone (else the host's
		game.bomb_open_pitch_speed: a client takes the host's, and refuses
		the whole of TRAITS past the speed's most), the carrier 200% (on
		top: 115% of 175%, kept to the speed's most) */
		long speed = config_integer("game.bomb_open_pitch_speed");

		traits->everyone.health = 10;
		traits->everyone.damage = 200;
		traits->everyone.speed = speed > 0 ?
			(short)PIN(speed, PLAYER_TRAIT_SPEED_MINIMUM, PLAYER_TRAIT_SPEED_MAXIMUM) : 175;
		traits->everyone.shields = _trait_shields_none;
		traits->carrier.speed = 115;
		/* (the carrier a little harder to stop, no more: game.bomb_open_pitch_
		carrier_health, _resistance and _shields on the host) */
		{
			long health = config_integer("game.bomb_open_pitch_carrier_health");
			long resistance = config_integer("game.bomb_open_pitch_carrier_resistance");
			char const *shields = config_string("game.bomb_open_pitch_carrier_shields");

			traits->carrier.health = health > 0 ? (short)health : BOMB_OPEN_PITCH_CARRIER_HEALTH;
			traits->carrier.resistance = resistance > 0 ? (short)resistance : BOMB_OPEN_PITCH_CARRIER_RESISTANCE;
			traits->carrier.shields = _trait_shields_unchanged;
			if (shields && !csstrcasecmp(shields, "normal"))
				traits->carrier.shields = _trait_shields_normal;
			else if (shields && !csstrcasecmp(shields, "overshield"))
				traits->carrier.shields = _trait_shields_overshield;
		}
	}
}

void game_engine_traits_clean(
	struct game_traits *traits)
{
	bomb_player_traits_clean(&traits->everyone);
	bomb_player_traits_clean(&traits->carrier);
}

/* the movement traits of TRAITS (0: a host before them; unchanged) */
static void bomb_read_host_movement(struct player_traits *traits, struct delta_wire_player_traits const *wire)
{
	traits->jump = wire->jump ? (short)wire->jump : 100;
	traits->gravity = wire->gravity ? (short)wire->gravity : 100;
	traits->melee = wire->melee ? (short)wire->melee : 100;
}

static void bomb_write_host_movement(struct delta_wire_player_traits *wire, struct player_traits const *traits)
{
	wire->jump = (unsigned short)traits->jump;
	wire->gravity = (unsigned short)traits->gravity;
	wire->melee = (unsigned short)traits->melee;
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
		bomb_read_host_movement(sets[index], wires[index]);
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

static void bomb_carrier_movement(struct player_traits *traits, struct player_traits const *carrier)
{
	traits->jump = (short)PIN(traits->jump * carrier->jump / 100, PLAYER_TRAIT_JUMP_MINIMUM,
		PLAYER_TRAIT_JUMP_MAXIMUM);
	traits->gravity = (short)PIN(traits->gravity * carrier->gravity / 100, PLAYER_TRAIT_GRAVITY_MINIMUM,
		PLAYER_TRAIT_GRAVITY_MAXIMUM);
	traits->melee = (short)PIN(traits->melee * carrier->melee / 100, PLAYER_TRAIT_MELEE_MINIMUM,
		PLAYER_TRAIT_MELEE_MAXIMUM);
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
		bomb_carrier_movement(traits, &game_traits->carrier);
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

/* a player's movement traits, if it plays by them: a Bomb game type's, and
its machine and the host both have them (bomb_movement); else none, as an
earlier build moves it */
static boolean bomb_player_movement(long player_index, struct player_traits *traits)
{
	/* (each player's, as last logged: 0 not yet, 1 none, 2 by them) */
	static byte logged[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long absolute_index = player_index != NONE ? DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) : NONE;
	boolean moves;

	if (!bomb_running() || player_index == NONE)
		return FALSE;
	moves = delta_peer_game_player_has((int)absolute_index, _delta_capability_bomb_movement) != 0;
	if (moves)
		bomb_player_traits(player_index, traits);
	if (VALID_INDEX(absolute_index, HALO_PORT_MAXIMUM_NETWORK_PLAYERS) && logged[absolute_index] != 1 + moves)
	{
		logged[absolute_index] = (byte)(1 + moves);
		if (moves)
			bomb_log("player %ld moves by the traits: jump %d gravity %d melee %d", absolute_index, traits->jump,
				traits->gravity, traits->melee);
		else
			bomb_log("player %ld moves as it always did (its machine or the host lacks bomb_movement)",
				absolute_index);
	}
	return moves;
}

real game_engine_traits_jump_multiplier(
	long player_index)
{
	struct player_traits traits;

	return bomb_player_movement(player_index, &traits) ? traits.jump / 100.0f : 1.0f;
}

real game_engine_traits_gravity_multiplier(
	long player_index)
{
	struct player_traits traits;

	return bomb_player_movement(player_index, &traits) ? traits.gravity / 100.0f : 1.0f;
}

real game_engine_traits_melee_multiplier(
	long player_index)
{
	struct player_traits traits;

	return bomb_player_movement(player_index, &traits) ? traits.melee / 100.0f : 1.0f;
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
		bomb_write_host_movement(wires[index], sets[index]);
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

/* the scenario's trigger volume of that name (NONE: none) */
static short bomb_find_trigger_volume(char const *name)
{
	struct scenario *scenario = global_scenario_get();
	short index;

	for (index = 0; name && index < scenario->trigger_volumes.count; index++)
	{
		struct scenario_trigger_volume *volume =
			TAG_BLOCK_GET_ELEMENT(&scenario->trigger_volumes, index, struct scenario_trigger_volume);

		if (!csstrcasecmp(volume->name, name))
			return index;
	}
	if (name)
		bomb_log("the court has no trigger volume %s", name);
	return NONE;
}

/* the middle of a trigger volume's floor */
static void bomb_trigger_volume_floor(short volume_index, real_point3d *point)
{
	struct scenario_trigger_volume *volume =
		TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->trigger_volumes, volume_index, struct scenario_trigger_volume);
	real_point3d middle;

	if (volume->type == _scenario_trigger_volume_type_oriented)
	{
		real_matrix4x3 matrix;

		middle.x = volume->extents.i * 0.5f;
		middle.y = volume->extents.j * 0.5f;
		middle.z = 0.0f;
		matrix4x3_from_point_and_vectors(&matrix, &volume->position, &volume->forward, &volume->up);
		matrix4x3_transform_point(&matrix, &middle, point);
	}
	else
	{
		point->x = (volume->bounds.x0 + volume->bounds.x1) * 0.5f;
		point->y = (volume->bounds.y0 + volume->bounds.y1) * 0.5f;
		point->z = volume->bounds.z0;
	}
}

/* where the map places its court's ball (FALSE: it doesn't) */
static boolean bomb_find_court_ball(real_point3d *position)
{
	struct scenario *scenario = global_scenario_get();
	short index;

	if (bomb_globals.court_ball_definition_index == NONE)
		return FALSE;
	for (index = 0; index < scenario->weapons.count; index++)
	{
		struct scenario_weapon_datum *weapon = TAG_BLOCK_GET_ELEMENT(&scenario->weapons, index,
			struct scenario_weapon_datum);

		if (VALID_INDEX(weapon->object.palette_entry_index, scenario->weapon_palette.count) &&
			TAG_BLOCK_GET_ELEMENT(&scenario->weapon_palette, weapon->object.palette_entry_index,
				struct scenario_object_palette_entry)->reference.index == bomb_globals.court_ball_definition_index)
		{
			*position = weapon->object.position;
			return TRUE;
		}
	}
	return FALSE;
}

/* the court the map is (bomb_courts), if it is one: its weapons, ball and
trigger volumes found */
static void bomb_find_court(void)
{
	unsigned long version;
	long court_index;
	short index;

	bomb_globals.court = FALSE;
	for (index = 0; index < MAXIMUM_BOMB_COURT_WEAPONS; index++)
		bomb_globals.court_weapons[index] = NONE;
	bomb_globals.court_ball_definition_index = NONE;
	for (index = 0; index < NUMBER_OF_BOMB_TEAMS; index++)
		bomb_globals.goal_volumes[index] = NONE;
	for (index = 0; index < MAXIMUM_BOMB_COURT_OUT_OF_BOUNDS; index++)
		bomb_globals.out_of_bounds_volumes[index] = NONE;
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
		if (bomb_courts[court_index].ball)
		{
			bomb_globals.court_ball_definition_index =
				tag_loaded(WEAPON_DEFINITION_TAG, bomb_courts[court_index].ball);
		}
		for (index = 0; index < NUMBER_OF_BOMB_TEAMS; index++)
			bomb_globals.goal_volumes[index] = bomb_find_trigger_volume(bomb_courts[court_index].goal_volumes[index]);
		for (index = 0; index < MAXIMUM_BOMB_COURT_OUT_OF_BOUNDS; index++)
		{
			bomb_globals.out_of_bounds_volumes[index] =
				bomb_find_trigger_volume(bomb_courts[court_index].out_of_bounds_volumes[index]);
		}
		bomb_log("court %s (%08lx): its scripts off, its weapons %lx %lx, its ball %lx, its goals' volumes %d %d, "
			"out of bounds %d", cache_file_loaded_map_name(), version, (unsigned long)bomb_globals.court_weapons[0],
			(unsigned long)bomb_globals.court_weapons[1], (unsigned long)bomb_globals.court_ball_definition_index,
			bomb_globals.goal_volumes[0], bomb_globals.goal_volumes[1], bomb_globals.out_of_bounds_volumes[0]);
		return;
	}
}

boolean game_engine_bomb_scripts_off(
	void)
{
	return bomb_running() && bomb_globals.court;
}

boolean game_engine_bomb_weapon_placement_allowed(
	struct scenario_object_datum const *placement,
	struct tag_block *palette)
{
	/* (every machine places the same: the court's own ball, on none) */
	return !bomb_running() || !bomb_globals.court || bomb_globals.court_ball_definition_index == NONE ||
		!VALID_INDEX(placement->palette_entry_index, palette->count) ||
		TAG_BLOCK_GET_ELEMENT(palette, placement->palette_entry_index,
			struct scenario_object_palette_entry)->reference.index != bomb_globals.court_ball_definition_index;
}

/* a court's out of bounds volume an object is in (FALSE: none) */
static boolean bomb_out_of_bounds(long object_index)
{
	short index;

	for (index = 0; index < MAXIMUM_BOMB_COURT_OUT_OF_BOUNDS; index++)
	{
		if (bomb_globals.out_of_bounds_volumes[index] != NONE &&
			scenario_trigger_volume_test_object(bomb_globals.out_of_bounds_volumes[index], object_index))
		{
			return TRUE;
		}
	}
	return FALSE;
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
	struct object_placement_data placement_data;
	long weapon_index;

	long definition_index = bomb_globals.sword_definition_index != NONE ? bomb_globals.sword_definition_index :
		bomb_globals.fist_definition_index;

	/* (a client has the host's: network_objects.c) */
	if (!game_engine_bomb_melee_only() || unit_index == NONE || network_game_distributed_client())
		return;
	/* (a court's own weapons: the last first, in place of the loadout's;
	each one after it into the unit's hands, so that the first ends there) */
	if (bomb_globals.court)
	{
		boolean added = FALSE;
		short index;

		for (index = MAXIMUM_BOMB_COURT_WEAPONS - 1; index >= 0; index--)
		{
			if (bomb_globals.court_weapons[index] == NONE)
				continue;
			object_placement_data_new(&placement_data, bomb_globals.court_weapons[index], NONE);
			weapon_index = object_new(&placement_data);
			if (weapon_index != NONE && unit_add_weapon_to_inventory(unit_index, weapon_index,
				added ? _unit_add_weapon_starting : _unit_add_weapon_replace))
			{
				added = TRUE;
			}
			else if (weapon_index != NONE)
				object_delete(weapon_index);
		}
		return;
	}
	if (definition_index == NONE)
		return;
	object_placement_data_new(&placement_data, definition_index, NONE);
	weapon_index = object_new(&placement_data);
	if (weapon_index == NONE)
		return;
	/* (the fists hold no rounds) */
	if (definition_index == bomb_globals.fist_definition_index)
	{
		struct weapon_datum *weapon = weapon_get(weapon_index);
		short magazine;

		for (magazine = 0; magazine < (short)NUMBEROF(weapon->weapon.magazines); magazine++)
		{
			weapon->weapon.magazines[magazine].rounds_total = 0;
			weapon->weapon.magazines[magazine].rounds_loaded = 0;
		}
	}
	bomb_globals.giving_fists = TRUE;
	if (!unit_add_weapon_to_inventory(unit_index, weapon_index, 2))
		object_delete(weapon_index);
	bomb_globals.giving_fists = FALSE;
}

/* whether a weapon is a melee-only game's unseen fists (every machine's
rule: none of it is drawn, its HUD is a bare crosshair, it can't shoot,
zoom or reload, and it is never left lying or picked up) */
boolean game_engine_bomb_hides_weapon(
	long definition_index)
{
	return definition_index != NONE && bomb_running() && definition_index == bomb_globals.fist_definition_index &&
		game_engine_bomb_melee_only();
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
static void bomb_place(real_point3d const *position)
{
	long weapon_index = bomb_globals.weapon_index;
	struct weapon_datum *weapon = weapon_index != NONE ?
		(struct weapon_datum *)object_try_and_get_and_verify_type(weapon_index, _object_mask_weapon) : NULL;

	bomb_globals.arm_ticks = 0;
	bomb_globals.plant_ticks = 0;
	if (!weapon)
	{
		bomb_globals.weapon_index = bomb_create();
		return;
	}
	/* (the bomb itself out of the carrier's inventory: in its hands, or
	holstered, where it has no parent: a pickup made with the trigger down
	leaves the bomb holstered, the fists in hand. Left there, the carrier
	scored again each tick it stood in the goal) */
	if (TEST_FLAG(weapon->item.flags, _item_attached_to_unit_bit) || weapon->object.parent_object_index != NONE)
	{
		long owner_index = NONE;

		if (weapon->object.parent_object_index != NONE &&
			object_try_and_get_and_verify_type(weapon->object.parent_object_index, _object_mask_unit))
		{
			owner_index = weapon->object.parent_object_index;
		}
		else
		{
			struct object_iterator units;

			object_iterator_new(&units, _object_mask_unit, 0);
			while (owner_index == NONE && object_iterator_next(&units))
			{
				if (unit_has_weapon(units.index, weapon_index))
					owner_index = units.index;
			}
		}
		if (owner_index == NONE || !unit_drop_weapon(owner_index, weapon_index))
		{
			error(_error_silent, "bomb: no unit holds the bomb it is attached to; it is made again");
			object_delete(weapon_index);
			bomb_globals.weapon_index = bomb_create();
			bomb_globals.carrier = NONE;
			return;
		}
	}
	bomb_globals.carrier = NONE;
	game_engine_flag_reset(weapon_index, position);
}

static void bomb_reset(void)
{
	bomb_place(&bomb_globals.spawn);
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

		/* (a team change waiting for the round: the player starts it on the
		new team's end instead, the bomb already out of their hands) */
		if (team_change_round_started(iterator.datum_index))
			continue;
		bomb_send_home(iterator.datum_index, counts[team]++);
	}
	bomb_events.rounds++;
	if (timed_out)
		bomb_events.round_overs++;
	bomb_show_round(timed_out);
}

/* the maps whose bomb spawn is not halfway between the goals, as
game.bomb_spawns has them (a point with no height is on the ground there).
Blood Gulch's is halfway between its two teleporters' exits (50.7 world
units from red's goal, 47.3 from blue's): about even, and each team's
teleporter takes it there */
static char const bomb_built_in_spawns[] = "bloodgulch 63.31 -120.38";

/* the point game.bomb_spawns (else the built-in ones) gives this map:
"<map> <x> <y> [<z>]", the map as a playlist names it (bloodgulch,
timberland@ce), entries separated by commas; the host's alone counts (only
its bomb is placed) */
static boolean bomb_spawn_setting(char const *spawns, real_point3d *point, boolean *has_height)
{
	char file[64];
	char map[80];
	char const *entry = spawns;
	short family = map_family_parse(main_get_multiplayer_map_name(), file, sizeof(file));

	snprintf(map, sizeof(map), "%s%s", file, map_family_suffix(family));
	while (entry && *entry)
	{
		char name[64];
		float x, y, z;
		int count = sscanf(entry, " %63[^ ,] %f %f %f", name, &x, &y, &z);

		if (count >= 3 && !csstrcasecmp(name, map))
		{
			point->x = x;
			point->y = y;
			point->z = count == 4 ? z : 0.0f;
			*has_height = count == 4;
			return TRUE;
		}
		entry = strchr(entry, ',');
		if (entry)
			entry++;
	}
	return FALSE;
}

/* the bomb's spawn, where it starts and resets: game.bomb_spawns's point on
this map, else the built-in one, else on the ground halfway between the
goals, so that neither team's run to it is the longer (a map's ball spawns
are oddball's, scattered over the map: Blood Gulch's first is nearly twice
as far from red's base as from blue's). The ground is the structure's (no
objects: none are placed yet), the first floor straight down over the
point. A map without the two goals, or without ground there, has its first
ball spawn (of any team), else halfway between the goals and a little up
(it falls to the ground), else its first starting location */
static void bomb_find_spawn(void)
{
	struct scenario *scenario = global_scenario_get();
	char const *setting = config_string("game.bomb_spawns");
	real_point3d point;
	boolean has_point;
	boolean has_height = FALSE;
	long flag_index = NONE;
	short index;

	/* (a court's own ball's spot) */
	if (bomb_find_court_ball(&bomb_globals.spawn))
	{
		bomb_log("spawn at the court's ball (%.2f %.2f %.2f)", bomb_globals.spawn.x, bomb_globals.spawn.y,
			bomb_globals.spawn.z);
		return;
	}
	has_point = bomb_spawn_setting(setting ? setting : "", &point, &has_height) ||
		bomb_spawn_setting(bomb_built_in_spawns, &point, &has_height);
	if (!has_point && bomb_globals.has_goal[0] && bomb_globals.has_goal[1])
	{
		point.x = (bomb_globals.goals[0].x + bomb_globals.goals[1].x) * 0.5f;
		point.y = (bomb_globals.goals[0].y + bomb_globals.goals[1].y) * 0.5f;
		has_point = TRUE;
	}
	if (has_point)
	{
		struct collision_result collision;
		real_vector3d down;

		if (has_height)
			point.z += BOMB_SPAWN_POINT_TEST_HEIGHT;
		else if (bomb_globals.has_goal[0] && bomb_globals.has_goal[1])
			point.z = MAX(bomb_globals.goals[0].z, bomb_globals.goals[1].z) + BOMB_SPAWN_TEST_HEIGHT;
		else
			point.z = BOMB_SPAWN_TEST_HEIGHT;
		down.i = 0.0f;
		down.j = 0.0f;
		down.k = -BOMB_SPAWN_TEST_DEPTH;
		/* (a floor faces up, at the test going down: a ceiling's underside,
		or a roof seen from over the map, is not one) */
		if (collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) |
				FLAG(_collision_test_ignore_invisible_surfaces_bit) | FLAG(_collision_test_structure_bit),
				&point, &down, NONE, &collision))
		{
			bomb_globals.spawn = collision.point;
			bomb_globals.spawn.z += BOMB_SPAWN_DROP_HEIGHT;
			bomb_log("spawn on the ground at (%.2f %.2f %.2f): %.2f from red's goal, %.2f from blue's",
				bomb_globals.spawn.x, bomb_globals.spawn.y, bomb_globals.spawn.z,
				distance3d(&bomb_globals.spawn, &bomb_globals.goals[0]),
				distance3d(&bomb_globals.spawn, &bomb_globals.goals[1]));
			return;
		}
		error(_error_silent, "bomb: no ground under (%.2f %.2f) for the bomb's spawn", point.x, point.y);
	}

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
	bomb_log("spawn at (%.2f %.2f %.2f)", bomb_globals.spawn.x, bomb_globals.spawn.y, bomb_globals.spawn.z);
}

/* ---------- what every machine shows of the goals */

/* the nav points (game_engine_render_nav_points): each team's goal to the
team that attacks it, in the goal's team's arrow; the carrier a target to
the other team, and one to defend to their own (not to the carrier) */
static void bomb_update_goals(void)
{
	struct player_datum *carrier = bomb_globals.carrier != NONE ? player_try_and_get(bomb_globals.carrier) : NULL;
	long team_index;

	for (team_index = 0; team_index < NUMBER_OF_BOMB_TEAMS; team_index++)
	{
		if (bomb_globals.has_goal[team_index])
		{
			game_engine_set_goal_position((short)(BOMB_GOAL_FIRST_TEAM + team_index), &bomb_globals.goals[team_index],
				0.0f, team_index == _team_red ? "default_red" : "default", NONE,
				(short)((team_index + 1) % NUMBER_OF_BOMB_TEAMS), NONE);
		}
	}
	if (carrier && carrier->unit_index != NONE && VALID_INDEX(carrier->team_index, NUMBER_OF_BOMB_TEAMS))
	{
		real_point3d position = object_get(carrier->unit_index)->object.bounding_sphere_center;

		game_engine_set_goal_position(BOMB_GOAL_CARRIER_ENEMIES, &position, 0.0f, "target_red", NONE,
			(short)((carrier->team_index + 1) % NUMBER_OF_BOMB_TEAMS), NONE);
		game_engine_set_goal_position(BOMB_GOAL_CARRIER_ALLIES, &position, 0.0f, "target_blue", NONE,
			(short)carrier->team_index, bomb_globals.carrier);
	}
	else
	{
		game_engine_clear_goal_position(BOMB_GOAL_CARRIER_ENEMIES);
		game_engine_clear_goal_position(BOMB_GOAL_CARRIER_ALLIES);
	}
}

/* the arming's seconds, counted with the countdown's beeps (the carrier's
arm ticks, which a client has from the host) */
static void bomb_arming_sounds(void)
{
	static long second_heard = 0;
	long arm_ticks = game_engine_get_variant()->game_engine_variant.bomb.arm_time * TICKS_PER_SECOND;
	long second;

	if (arm_ticks <= 0 || bomb_globals.arm_ticks <= 0 || bomb_globals.carrier == NONE)
	{
		second_heard = 0;
		return;
	}
	second = (arm_ticks - bomb_globals.arm_ticks + TICKS_PER_SECOND - 1) / TICKS_PER_SECOND;
	if (second > 0 && (second_heard == 0 || second < second_heard))
		game_engine_play_multiplayer_sound(_bomb_sound_countdown);
	second_heard = second;
}

/* a goal's outline, its points about the ground: a circle of the goal's
radius about its marker (a goal that is a volume would give its own) */
static short bomb_goal_outline(long team_index, real_point3d *points, short maximum_count)
{
	short count = (short)MIN(maximum_count, BOMB_GOAL_OUTLINE_SIDES);
	short index;

	for (index = 0; index < count; index++)
	{
		real angle = (real)index * (real)(2.0 * 3.14159265358979) / (real)count;

		points[index] = bomb_globals.goals[team_index];
		points[index].x += BOMB_GOAL_RADIUS * (real)cos(angle);
		points[index].y += BOMB_GOAL_RADIUS * (real)sin(angle);
	}
	return count;
}

/* each goal outlined as King of the Hill's hill is, in the map's hill
shader: a band of quads about it (king_engine_post_rasterize's), low on the
ground, and lower the nearer the view is, gone at the goal, so it never
covers the screen */
static void bomb_engine_post_rasterize(void)
{
	struct game_globals *game_globals = scenario_get_game_globals();
	struct game_globals_multiplayer_information *multiplayer_information;
	long team_index;

	if (!bomb_running() || !game_globals || game_globals->multiplayer_information.count <= 0)
		return;
	multiplayer_information = TAG_BLOCK_GET_ELEMENT(&game_globals->multiplayer_information, 0,
		struct game_globals_multiplayer_information);
	if (multiplayer_information->hill_shader.index == NONE)
		return;
	for (team_index = 0; team_index < NUMBER_OF_BOMB_TEAMS; team_index++)
	{
		real_point3d points[BOMB_GOAL_OUTLINE_SIDES];
		short count;
		short index;
		real perimeter = 0.0f;
		real segments;
		real u = 0.0f;

		real height;

		if (!bomb_globals.has_goal[team_index])
			continue;
		{
			real i = render.camera.position.x - bomb_globals.goals[team_index].x;
			real j = render.camera.position.y - bomb_globals.goals[team_index].y;
			real fade = PIN(((real)sqrt(i * i + j * j) - BOMB_GOAL_OUTLINE_FADE_NEAR) /
				(BOMB_GOAL_OUTLINE_FADE_FAR - BOMB_GOAL_OUTLINE_FADE_NEAR), 0.0f, 1.0f);

			height = BOMB_GOAL_OUTLINE_HEIGHT * fade;
		}
		if (height < 0.02f)
			continue;
		count = bomb_goal_outline(team_index, points, BOMB_GOAL_OUTLINE_SIDES);
		for (index = 0; index < count; index++)
			perimeter += distance3d(&points[index], &points[(index + 1) % count]);
		segments = (real)MAX(1.0, floor(perimeter + 0.5f));
		for (index = 0; index < count; index++)
		{
			struct model_vertex_uncompressed vertices[4];
			real_vector3d sides[2];
			real_vector3d normal;
			real end_u = u + distance3d(&points[index], &points[(index + 1) % count]) * segments / perimeter;

			csmemset(vertices, 0, sizeof(vertices));
			vertices[0].position = points[index];
			vertices[1].position = points[index];
			vertices[1].position.z += height;
			vertices[2].position = points[(index + 1) % count];
			vertices[2].position.z += height;
			vertices[3].position = points[(index + 1) % count];
			vector_from_points3d(&vertices[0].position, &vertices[1].position, &sides[0]);
			vector_from_points3d(&vertices[1].position, &vertices[2].position, &sides[1]);
			cross_product3d(&sides[0], &sides[1], &normal);
			normalize3d(&normal);
			vertices[0].normal = vertices[1].normal = vertices[2].normal = vertices[3].normal = normal;
			vertices[0].texcoord.x = u / segments;
			vertices[0].texcoord.y = 1.0f;
			vertices[1].texcoord.x = u / segments;
			vertices[1].texcoord.y = 0.2f;
			vertices[2].texcoord.x = end_u / segments;
			vertices[2].texcoord.y = 0.2f;
			vertices[3].texcoord.x = end_u / segments;
			vertices[3].texcoord.y = 1.0f;
			u = end_u;
			render_dynamic_quad(vertices, multiplayer_information->hill_shader.index, NULL, NULL, segments, 1.0f);
		}
	}
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

	if (!game_engine_get_variant()->universal_variant.teams)
		error(_error_silent, "bomb started up without teams");
	csmemset(&bomb_globals, 0, sizeof(bomb_globals));
	csmemset(&bomb_events, 0, sizeof(bomb_events));
	bomb_globals.weapon_index = NONE;
	bomb_globals.carrier = NONE;
	bomb_globals.blast_goal_team = NONE;
	bomb_globals.blast_scorer = NONE;
	bomb_globals.blast_team = NONE;
	bomb_globals.blast_done = TRUE;
	bomb_blast_find_effects();
	bomb_globals.score_to_win = game_engine_get_variant()->universal_variant.score_to_win;
	bomb_globals.sword_definition_index = bomb_find_sword();
	/* (the multiplayer weapon list's pistol: game_engine.c's
	_weapon_list_pistol) */
	bomb_globals.fist_definition_index = bomb_globals.sword_definition_index == NONE ?
		list_index_to_weapon_definition_index(4) : NONE;
	if (bomb_globals.fist_definition_index != NONE &&
		weapon_definition_get(bomb_globals.fist_definition_index)->weapon.interface_definition.first_person_animations.index ==
			NONE)
	{
		bomb_globals.fist_definition_index = NONE;
	}
	bomb_find_court();
	/* (a court gives its own weapons: no fists) */
	if (bomb_globals.court)
		bomb_globals.fist_definition_index = NONE;

	for (team_index = 0; team_index < NUMBER_OF_BOMB_TEAMS; team_index++)
	{
		/* (a court's goal: its trigger volume) */
		if (bomb_globals.goal_volumes[team_index] != NONE)
		{
			bomb_trigger_volume_floor(bomb_globals.goal_volumes[team_index], &bomb_globals.goals[team_index]);
			bomb_globals.has_goal[team_index] = TRUE;
			continue;
		}
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

	bomb_find_spawn();
	bomb_log("goals red (%.2f %.2f %.2f) blue (%.2f %.2f %.2f), the bomb's spawn (%.2f %.2f %.2f)",
		bomb_globals.goals[0].x, bomb_globals.goals[0].y, bomb_globals.goals[0].z, bomb_globals.goals[1].x,
		bomb_globals.goals[1].y, bomb_globals.goals[1].z, bomb_globals.spawn.x, bomb_globals.spawn.y,
		bomb_globals.spawn.z);

	/* (the goals' outlines, drawn as the hill is) */
	render_dynamic_quad_initialize();
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

/* ---------- the blast */

/* the blast's effects: each of the globals' grenades' explosion (the frag's,
the plasma's) */
static void bomb_blast_find_effects(void)
{
	struct game_globals *globals = scenario_get_game_globals();
	short index;

	for (index = 0; index < MAXIMUM_BOMB_BLAST_EFFECTS; index++)
		bomb_globals.blast_effects[index] = NONE;
	for (index = 0; globals && index < globals->grenades.count && index < MAXIMUM_BOMB_BLAST_EFFECTS; index++)
	{
		struct game_globals_grenade *grenade = TAG_BLOCK_GET_ELEMENT(&globals->grenades, index,
			struct game_globals_grenade);

		if (grenade->projectile.index != NONE)
		{
			bomb_globals.blast_effects[index] =
				projectile_definition_get(grenade->projectile.index)->projectile.effect.index;
		}
	}
}

/* every machine's: the blast drawn at the goal, a ring of the grenades'
explosions about a cluster at its middle (the same places on every machine),
without their damage: the blast's is bomb_blast's */
static void bomb_blast_effects(real_point3d const *goal)
{
	/* (world units from the goal: a cluster in the middle, rising; a ring
	about it; one further out, about the base) */
	static real const places[][3] =
	{
		{ 0.0f, 0.0f, 0.3f }, { 0.0f, 0.0f, 1.2f }, { 0.0f, 0.0f, 2.4f }, { 1.8f, 0.0f, 0.3f },
		{ 1.27f, 1.27f, 0.3f }, { 0.0f, 1.8f, 0.3f }, { -1.27f, 1.27f, 0.3f }, { -1.8f, 0.0f, 0.3f },
		{ -1.27f, -1.27f, 0.3f }, { 0.0f, -1.8f, 0.3f }, { 1.27f, -1.27f, 0.3f }, { 3.03f, 1.75f, 0.5f },
		{ 0.0f, 3.5f, 0.5f }, { -3.03f, 1.75f, 0.5f }, { -3.03f, -1.75f, 0.5f }, { 0.0f, -3.5f, 0.5f },
		{ 3.03f, -1.75f, 0.5f }, { 0.64f, 0.64f, 1.4f }, { -0.64f, 0.64f, 1.4f }, { -0.64f, -0.64f, 1.4f },
		{ 0.64f, -0.64f, 1.4f },
	};
	short index;

	for (index = 0; index < (short)NUMBEROF(places); index++)
	{
		/* (the frag's below, the plasma's above, where there are both) */
		long effect_index = bomb_globals.blast_effects[places[index][2] > 1.0f && bomb_globals.blast_effects[1] != NONE];
		char const *marker_names[2] = { "", "gravity" };
		real_point3d points[2];
		real_vector3d forwards[2];

		if (effect_index == NONE)
			effect_index = bomb_globals.blast_effects[0];
		if (effect_index == NONE)
			return;
		points[0].x = goal->x + places[index][0];
		points[0].y = goal->y + places[index][1];
		points[0].z = goal->z + places[index][2];
		points[1] = points[0];
		forwards[0] = *global_up3d;
		forwards[1] = *global_down3d;
		effects_port_harmless = TRUE;
		effect_new_unattached_from_markers(effect_index, NONE, NULL, NUMBEROF(marker_names), marker_names, points,
			forwards, 0.0f, 0.0f, NULL, NULL, TRUE);
		effects_port_harmless = FALSE;
	}
}

/* the host's: everyone near the goal dies, the bodies thrown out and up.
The other team's dead are the scorer's kills; the scoring team's own (the
scorer too) count nothing: no death, no suicide, no betrayal, no penalty.
Everyone dead respawns at their own end as the next round starts. */
static void bomb_blast(long scorer_index, long team_index, real_point3d const *goal)
{
	struct data_iterator iterator;
	struct player_datum *player;
	real radius = BOMB_BLAST_RADIUS;
	struct object_iterator objects;

	/* (a scorer who has left credits no one: their enemies die all the
	same, counting nothing) */
	if (scorer_index != NONE && !player_try_and_get(scorer_index))
		scorer_index = NONE;
	/* the other team first: the scorer is alive to be credited */
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct object_datum *unit;

		if (player->unit_index == NONE || !VALID_INDEX(absolute_index, HALO_PORT_MAXIMUM_NETWORK_PLAYERS))
			continue;
		unit = object_get(player->unit_index);
		if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
			distance_squared3d(goal, &unit->object.position) > radius * radius)
		{
			continue;
		}
		if (player->team_index != team_index)
		{
			bomb_log_player("blast kills", iterator.datum_index);
			if (scorer_index != NONE)
				network_damage_host_kill_for_player(player->unit_index, scorer_index);
			else
				damage_kill_object_no_statistics(player->unit_index);
		}
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct object_datum *unit;

		if (player->unit_index == NONE || !VALID_INDEX(absolute_index, HALO_PORT_MAXIMUM_NETWORK_PLAYERS) ||
			player->team_index != team_index)
		{
			continue;
		}
		unit = object_get(player->unit_index);
		if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
			distance_squared3d(goal, &unit->object.position) > radius * radius)
		{
			continue;
		}
		bomb_log_player("blast takes its own", iterator.datum_index);
		bomb_globals.blast_silent[absolute_index] = TRUE;
		damage_kill_object_no_statistics(player->unit_index);
		bomb_globals.blast_silent[absolute_index] = FALSE;
	}
	/* the bodies thrown (bipeds alone: the bomb is home, at its spawn) */
	object_iterator_new(&objects, _object_mask_biped, 0);
	while (object_iterator_next(&objects))
	{
		struct object_datum *body = object_get(objects.index);
		real_vector3d out;
		real distance, near;

		if (!TEST_FLAG(body->object.damage_flags, _object_dead_bit) || body->object.parent_object_index != NONE)
			continue;
		out.i = body->object.position.x - goal->x;
		out.j = body->object.position.y - goal->y;
		out.k = 0.0f;
		distance = (real)sqrt(distance_squared3d(goal, &body->object.position));
		if (distance > radius)
			continue;
		if (normalize3d(&out) <= 0.0f)
			out = *global_forward3d;
		near = 1.0f - distance / radius;
		body->object.translational_velocity.i = out.i * BOMB_BLAST_THROW_OUT * (0.5f + 0.5f * near);
		body->object.translational_velocity.j = out.j * BOMB_BLAST_THROW_OUT * (0.5f + 0.5f * near);
		body->object.translational_velocity.k = BOMB_BLAST_THROW_UP * (0.6f + 0.4f * near);
		SET_FLAG(body->object.flags, _object_at_rest_bit, FALSE);
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
	{
		long goal_team = (team_index + 1) % NUMBER_OF_BOMB_TEAMS;

		/* the bomb planted in the goal, out of anyone's hands, to go off
		after a countdown (bomb_hold_update) */
		bomb_globals.carrier = NONE;
		bomb_globals.arm_ticks = 0;
		bomb_globals.blast_goal_team = goal_team;
		bomb_globals.blast_scorer = player_index;
		bomb_globals.blast_team = team_index;
		bomb_globals.blast_next_round = bomb_globals.scores[team_index] < bomb_globals.score_to_win &&
			TEST_FLAG(game_engine_get_variant()->game_engine_variant.bomb.flags, _bomb_new_round_on_score_bit);
		bomb_globals.blast_done = FALSE;
		bomb_globals.countdown_shown = 0;
		bomb_place(bomb_globals.has_goal[goal_team] ? &bomb_globals.goals[goal_team] : &bomb_globals.spawn);
		bomb_globals.hold_ticks = BOMB_COUNTDOWN_TICKS + BOMB_BLAST_PAUSE_TICKS;
	}
}

/* the host's: the carrier at the enemy goal plants the bomb there, to
arm for game.bomb_arm_time (0: it scores at once). Out of everyone's hands
until it scores or a defender saves it (bomb_plant_update) */
static void bomb_plant(long player_index)
{
	struct player_datum *player = player_get(player_index);
	long goal_team = (player->team_index + 1) % NUMBER_OF_BOMB_TEAMS;
	double arm_time = config_real("game.bomb_arm_time");
	long ticks = arm_time > 0.0 ?
		(long)(MIN(arm_time, BOMB_ARM_TIME_MAXIMUM) * TICKS_PER_SECOND + 0.5) : 0;

	if (ticks <= 0 || !bomb_globals.has_goal[goal_team])
	{
		bomb_award_score(player_index);
		return;
	}
	bomb_log_player("plant", player_index);
	bomb_place(&bomb_globals.goals[goal_team]);
	bomb_globals.plant_ticks = ticks;
	bomb_globals.plant_goal_team = goal_team;
	bomb_globals.plant_scorer = player_index;
}

/* the host's: the planted bomb saved by a living defender who reaches it
(back to its spawn, as an untouched bomb's reset), else, armed, scored */
static void bomb_plant_update(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *scorer;
	real_point3d position;

	if (bomb_globals.weapon_index == NONE)
	{
		bomb_globals.plant_ticks = 0;
		return;
	}
	item_get_position_even_if_in_inventory(bomb_globals.weapon_index, &position);
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *unit;

		if (player->team_index != bomb_globals.plant_goal_team || player->unit_index == NONE)
			continue;
		unit = object_get(player->unit_index);
		if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
			distance_squared3d(&position, &unit->object.position) >= BOMB_SAVE_RADIUS * BOMB_SAVE_RADIUS)
		{
			continue;
		}
		bomb_log_player("save", iterator.datum_index);
		bomb_reset();
		bomb_events.resets++;
		bomb_show_reset();
		return;
	}
	if (--bomb_globals.plant_ticks > 0)
		return;
	/* (the planter's score, unless it left the game: then the bomb resets) */
	scorer = player_try_and_get(bomb_globals.plant_scorer);
	if (scorer && (scorer->team_index + 1) % NUMBER_OF_BOMB_TEAMS == bomb_globals.plant_goal_team)
		bomb_award_score(bomb_globals.plant_scorer);
	else
	{
		bomb_log("plant: the planter is gone; the bomb resets");
		bomb_reset();
		bomb_events.resets++;
		bomb_show_reset();
	}
}

/* every machine's: the planted bomb's countdown, its blast (the host's
damage; everyone's effects), and the host's next round after the pause */
static void bomb_hold_update(void)
{
	if (bomb_globals.hold_ticks <= 0)
		return;
	bomb_globals.hold_ticks--;
	if (bomb_globals.hold_ticks > BOMB_BLAST_PAUSE_TICKS)
	{
		long seconds = (bomb_globals.hold_ticks - BOMB_BLAST_PAUSE_TICKS + TICKS_PER_SECOND - 1) / TICKS_PER_SECOND;

		if (bomb_globals.countdown_shown == 0 || seconds < bomb_globals.countdown_shown)
		{
			bomb_globals.countdown_shown = seconds;
			game_engine_play_multiplayer_sound(_bomb_sound_countdown);
		}
		return;
	}
	bomb_globals.countdown_shown = 0;
	if (!bomb_globals.blast_done)
	{
		long goal_team = bomb_globals.blast_goal_team;

		bomb_globals.blast_done = TRUE;
		game_engine_play_multiplayer_sound(_bomb_sound_countdown_end);
		if (VALID_INDEX(goal_team, NUMBER_OF_BOMB_TEAMS) && bomb_globals.has_goal[goal_team])
		{
			if (!network_game_distributed_client())
				bomb_blast(bomb_globals.blast_scorer, bomb_globals.blast_team, &bomb_globals.goals[goal_team]);
			bomb_blast_effects(&bomb_globals.goals[goal_team]);
		}
		/* (the bomb gone with it, home again) */
		if (!network_game_distributed_client())
			bomb_reset();
	}
	/* the host's next round: everyone home, the dead with them (no one
	spawned during the pause) */
	if (bomb_globals.hold_ticks == 0 && !network_game_distributed_client() && bomb_globals.blast_next_round)
	{
		struct data_iterator iterator;
		struct player_datum *player;

		bomb_start_round(FALSE);
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->unit_index == NONE)
				player->respawn_timer = MIN(player->respawn_timer, 1);
		}
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
	/* the carrier's line in the HUD's state message, as CTF's and Oddball's
	carriers have (every machine: the carrier and the arming are each
	machine's from the host's) */
	game_engine_state_message(player_index, NONE, NONE);
	if (carrying && game_engine_can_score())
	{
		game_engine_state_message(player_index, bomb_globals.arm_ticks > 0 &&
			game_engine_get_variant()->game_engine_variant.bomb.arm_time > 0 ? _bomb_message_arming :
			_bomb_message_carrying, player_index);
	}

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
	/* (a court's out of bounds: the player killed, as its scripts did) */
	if (player->unit_index != NONE && bomb_out_of_bounds(player->unit_index) &&
		!TEST_FLAG(object_get(player->unit_index)->object.damage_flags, _object_dead_bit))
	{
		bomb_log_player("out of bounds: killed", player_index);
		unit_kill(player->unit_index);
		return;
	}
	if (carrying && traits.shields == _trait_shields_overshield)
		bomb_overshield_update(player_index);
	/* (one score a goal: none from the goal to the next round, the bomb
	planted) */
	if (carrying && game_engine_can_score() && bomb_globals.hold_ticks <= 0 && bomb_globals.plant_ticks <= 0 &&
		VALID_INDEX(player->team_index, NUMBER_OF_BOMB_TEAMS))
	{
		long goal_team = (player->team_index + 1) % NUMBER_OF_BOMB_TEAMS;
		struct object_datum *unit = object_get(player->unit_index);
		boolean in_goal = bomb_globals.goal_volumes[goal_team] != NONE ?
			scenario_trigger_volume_test_object(bomb_globals.goal_volumes[goal_team], player->unit_index) :
			bomb_globals.has_goal[goal_team] &&
				distance_squared3d(&bomb_globals.goals[goal_team], &unit->object.position) <
					BOMB_GOAL_RADIUS * BOMB_GOAL_RADIUS;

		if (in_goal)
		{
			bomb_globals.arm_ticks++;
			if (bomb_globals.arm_ticks >=
				game_engine_get_variant()->game_engine_variant.bomb.arm_time * TICKS_PER_SECOND)
			{
				bomb_plant(player_index);
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
	/* (the bomb's own marker while it lies loose or planted; its carrier's
are bomb_update_goals') */
	if (weapon->object.parent_object_index == NONE)
	{
		item_get_position_even_if_in_inventory(weapon_index, &position);
		game_engine_set_goal_position(BOMB_GOAL_BOMB, &position, 0.0f, "ball_blue", NONE, NONE, NONE);
	}
	else
		game_engine_clear_goal_position(BOMB_GOAL_BOMB);

	/* (a client has the bomb where the host puts it; a court's bomb lying
	out of bounds goes back at once, as its scripts sent their ball) */
	if (!network_game_distributed_client() && weapon->object.parent_object_index == NONE &&
		TEST_FLAG(weapon->object.flags, _object_connected_to_map_bit) && bomb_out_of_bounds(weapon_index))
	{
		bomb_log("reset: out of bounds at (%.2f %.2f %.2f)", weapon->object.position.x, weapon->object.position.y,
			weapon->object.position.z);
		bomb_reset();
		bomb_events.resets++;
		bomb_show_reset();
		return;
	}
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

	bomb_update_goals();
	bomb_arming_sounds();
	/* the fists of the dead (or any dropped) gone: the host's word deletes
	a client's */
	if (bomb_globals.fist_definition_index != NONE && game_engine_bomb_melee_only() &&
		!network_game_distributed_client())
	{
		struct object_iterator objects;

		object_iterator_new(&objects, _object_mask_weapon, 0);
		while (object_iterator_next(&objects))
		{
			struct weapon_datum *weapon = weapon_get(objects.index);

			if (weapon->definition_index == bomb_globals.fist_definition_index &&
				weapon->object.parent_object_index == NONE &&
				TEST_FLAG(weapon->object.flags, _object_connected_to_map_bit) &&
				!TEST_FLAG(weapon->item.flags, _item_attached_to_unit_bit))
			{
				object_delete(objects.index);
			}
		}
	}
	if (game_time_get() - bomb_globals.round_start_time == BOMB_ROUND_ANNOUNCEMENT_TICKS &&
		bomb_globals.hold_ticks == 0)
	{
		game_engine_play_multiplayer_sound(_bomb_sound_play_ball);
	}

	if (!game_engine_can_score())
		return;
	/* (the planted bomb arming: the host's) */
	if (bomb_globals.plant_ticks > 0 && !network_game_distributed_client())
		bomb_plant_update();
	/* (the round's time stands still from the goal to the next round) */
	if (bomb_globals.hold_ticks > 0)
	{
		bomb_hold_update();
		return;
	}
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
		if (bomb_globals.round_ticks == 0)
			game_engine_play_multiplayer_sound(_bomb_sound_countdown_end);
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
	/* (not the fists lying about: one left is gone at the host's next tick;
	those carried are the carrier's to use) */
	{
		struct weapon_datum *weapon = weapon_get(weapon_index);

		if (game_engine_bomb_hides_weapon(weapon->definition_index) && !bomb_globals.giving_fists &&
			TEST_FLAG(weapon->object.flags, _object_connected_to_map_bit) &&
			!TEST_FLAG(weapon->item.flags, _item_attached_to_unit_bit) && weapon->object.parent_object_index == NONE)
		{
			return FALSE;
		}
	}
	/* (not the bomb from the goal to the next round, or while it arms: it
	is planted) */
	return weapon_index != bomb_globals.weapon_index ||
		(bomb_globals.hold_ticks <= 0 && bomb_globals.plant_ticks <= 0);
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
	case _bomb_message_enemy_has_the_bomb:
	{
		struct player_datum *carrier = message_data != NONE ? player_try_and_get(message_data) : NULL;

		if (carrier)
		{
			usnprintf(buffer, buffer_size, message == _bomb_message_ally_has_the_bomb ? L"%s has the bomb." :
				L"%s has the bomb!", carrier->name);
		}
		else
		{
			usnprintf(buffer, buffer_size, message == _bomb_message_ally_has_the_bomb ? L"Your team has the bomb." :
				L"The enemy has the bomb!");
		}
		break;
	}
	case _bomb_message_carrying:
		usnprintf(buffer, buffer_size, L"Take the bomb to the enemy goal");
		break;
	case _bomb_message_arming:
		usnprintf(buffer, buffer_size, L"Arming the bomb");
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
	/* the grab's tone (the "ting", where the map's globals have it), and
	who has it in the game's messages, as Oddball's ball */
	game_engine_play_multiplayer_sound(game_engine_multiplayer_sound_exists(_bomb_sound_ting) ? _bomb_sound_ting :
		_bomb_sound_hill_move);
	if (player_index != NONE && player_try_and_get(player_index))
	{
		game_show_score_you_ally_enemy(player_index, _bomb_message_you_have_the_bomb, _bomb_message_ally_has_the_bomb,
			_bomb_message_enemy_has_the_bomb, player_index);
	}
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

/* ---------- the HUD (game_engine.c draws it after the game's own) */

/* the round, in the HUD's own digits and colors (hud_render_round): its
clock, and each team's rounds beside it in its arrows' colors; the planted
bomb's countdown, large; and the carrier's arming on the shield meter's
likeness. All from the state every machine has from the host */
void game_engine_bomb_rasterize(
	long player_index)
{
	struct player_datum *player;
	unsigned long red_color = 0xFFFF4040;
	unsigned long blue_color = 0xFF4080FF;
	long arm_time;
	long seconds;

	if (!bomb_running() || player_index == NONE || !game_engine_can_score())
		return;
	player = player_get(player_index);
	if (player->local_player_index == NONE)
		return;
	hud_nav_point_color("default_red", &red_color);
	hud_nav_point_color("default", &blue_color);
	hud_render_round(player->local_player_index, bomb_globals.round_ticks, BOMB_ROUND_WARNING_TICKS,
		bomb_globals.scores[_team_red], red_color, bomb_globals.scores[_team_blue], blue_color);
	seconds = game_engine_bomb_countdown();
	if (seconds > 0)
		hud_render_large_count(player->local_player_index, seconds);
	arm_time = game_engine_get_variant()->game_engine_variant.bomb.arm_time;
	if (bomb_globals.carrier == player_index && arm_time > 0 && bomb_globals.arm_ticks > 0)
	{
		hud_render_progress_meter(player->local_player_index,
			(real)bomb_globals.arm_ticks / (real)(arm_time * TICKS_PER_SECOND));
	}
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
	bomb_engine_post_rasterize,
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
	/* after a goal, the ticks before play goes on (bomb_globals.hold_ticks;
	a host before it sent 0 here) */
	byte hold_ticks;
	byte pad[2];
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
	state.hold_ticks = (byte)PIN(bomb_globals.hold_ticks, 0, 255);
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
	bomb_globals.hold_ticks = state.hold_ticks;
	if (!first)
	{
		if (state.carry_count != bomb_events.carries)
		{
			long player_index = distributed_player_from_byte(state.carrier);

			if (player_index != NONE)
				bomb_show_carry(player_index);
		}
		if (state.score_count != bomb_events.scores)
		{
			long goal_team = (state.scorer_team + 1) % NUMBER_OF_BOMB_TEAMS;

			bomb_show_score(distributed_player_from_byte(state.scorer), state.scorer_team);
			/* (the planted bomb's countdown and blast, drawn here as the
			host's hold_ticks run down: bomb_hold_update) */
			bomb_globals.blast_goal_team = goal_team;
			bomb_globals.blast_team = state.scorer_team;
			bomb_globals.blast_done = FALSE;
			bomb_globals.countdown_shown = 0;
		}
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
(hud.c): the game's own message says who has the bomb */
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

/* a player's Bomb counts for the game's event log (port/linux): seconds
carrying the bomb, carriers killed and kills while carrying; FALSE when no
Bomb game is running */
boolean game_engine_bomb_player_statistics(
	long player_index,
	long *carry_seconds,
	long *carrier_kills,
	long *kills_as_carrier)
{
	long index;

	*carry_seconds = *carrier_kills = *kills_as_carrier = 0;
	if (!bomb_running() || player_index == NONE)
		return FALSE;
	index = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	if (!VALID_INDEX(index, HALO_PORT_MAXIMUM_NETWORK_PLAYERS))
		return FALSE;
	*carry_seconds = bomb_globals.carry_ticks[index] / TICKS_PER_SECOND;
	*carrier_kills = bomb_globals.carrier_kills[index];
	*kills_as_carrier = bomb_globals.kills_as_carrier[index];
	return TRUE;
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

/* ---------- the goal's countdown and blast (game_engine.c) */

/* whether a player's death is the blast's taking of the scoring team's own,
which counts nothing (the host's, as its blast marked it; a client's, as the
host's word on the death says: network_distributed_death_silent) */
boolean game_engine_bomb_silent_death(
	long killing_player_index,
	long dead_player_index,
	boolean friendly_fire)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(dead_player_index);

	if (!bomb_running() || !VALID_INDEX(absolute_index, HALO_PORT_MAXIMUM_NETWORK_PLAYERS))
		return FALSE;
	if (!network_game_distributed_client())
		return bomb_globals.blast_silent[absolute_index];
	return killing_player_index == dead_player_index && friendly_fire &&
		network_distributed_death_silent(dead_player_index);
}

/* the respawn of a death that counts nothing: with the next round */
long game_engine_bomb_silent_respawn_ticks(
	void)
{
	return bomb_running() && bomb_globals.hold_ticks > 0 ? bomb_globals.hold_ticks : BOMB_BLAST_PAUSE_TICKS;
}

/* the planted bomb's countdown's second now (0: none) */
long game_engine_bomb_countdown(
	void)
{
	if (!bomb_running() || bomb_globals.hold_ticks <= BOMB_BLAST_PAUSE_TICKS)
		return 0;
	return (bomb_globals.hold_ticks - BOMB_BLAST_PAUSE_TICKS + TICKS_PER_SECOND - 1) / TICKS_PER_SECOND;
}

/* whether no one spawns now (the blast's pause, before the next round) */
boolean game_engine_bomb_rounds(
	void)
{
	return bomb_running();
}

boolean game_engine_bomb_holds_spawns(
	void)
{
	return bomb_running() && bomb_globals.hold_ticks > 0 && bomb_globals.hold_ticks <= BOMB_BLAST_PAUSE_TICKS;
}

/* the host's: a melee blow that doesn't kill the carrier knocks the bomb
loose (Open Pitch, game.bomb_open_pitch_fumble): it falls at the carrier's
feet, thrown a little ahead as a drop is, and the carrier can't take it
back at once (unit_drop_item) */
void game_engine_bomb_unit_struck(
	long unit_index)
{
	long weapon_index = bomb_globals.weapon_index;

	if (!bomb_running() || network_game_distributed_client() || weapon_index == NONE ||
		game_engine_get_variant()->game_engine_variant.bomb.traits != _bomb_traits_open_pitch ||
		!config_boolean("game.bomb_open_pitch_fumble") ||
		!object_try_and_get_and_verify_type(unit_index, _object_mask_unit) || !unit_has_weapon(unit_index, weapon_index))
	{
		return;
	}
	if (unit_drop_weapon(unit_index, weapon_index))
	{
		long player_index = unit_get(unit_index)->unit.player_index;

		if (player_index != NONE)
			bomb_log_player("fumble", player_index);
	}
}

/* the host's alone counts: only it deals a landing's damage
(network_damage_deals) */
boolean game_engine_bomb_landing_damage(
	void)
{
	return !bomb_running() ||
		game_engine_get_variant()->game_engine_variant.bomb.traits == _bomb_traits_none;
}

/* ---------- the network test's (port/linux/game/network_test.c) */

long game_engine_bomb_weapon(
	void)
{
	return bomb_running() ? bomb_globals.weapon_index : NONE;
}

long game_engine_bomb_planted_goal(
	void)
{
	return bomb_running() && bomb_globals.plant_ticks > 0 ? bomb_globals.plant_goal_team : NONE;
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

#if defined(HALO_BIG_ENDIAN) && defined(HALO_GAME_BROWSER)
/* port: (game_engine.h) the state in the wire's byte order, either way */
void game_engine_bomb_network_state_byte_swap(
	byte *buffer,
	long size)
{
	struct bomb_network_state *state = (struct bomb_network_state *)buffer;

	if (size != (long)sizeof(*state))
		return;
	GAME_ENGINE_NETWORK_STATE_SWAP(state, weapon_index, arm_ticks, sizeof(long));
}
#endif
