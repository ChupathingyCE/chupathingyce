/*
H2_DIALOGUE.C

port: Halo 2's AI combat dialogue on a Halo 2 level (h2_dialogue.h).

Halo 2 keeps its combat dialogue in two kinds of tag the player's map
carries: the ai dialogue globals ('adlg', one a map) and a dialogue tag
('udlg') a character's model variant names, its voice. The globals hold
the vocalizations (each a name, a parent, a priority, delays, a weight and
skip fractions, and the responses it invites), the patterns (for each
dialogue event type - death, damage, sighted_new, taunt, new_order_*... -
who speaks which vocalization to whom, under what conditions: the cause's
and subject's kinds, the speaker's combat status, visibility, distance),
each type's range of patterns ("dialogue data") and the involuntary table
(the unit's own vocalization types - death, pain, flee, cower, surprise,
panic - to the globals' vocalizations). A voice is a list of the same
vocalization names, each with its sound tag or none.

What the engine does with them is not in the Xbox decomp (only the script
side, ai_play_line, is: unknown_291ea0.cpp); the rules here are what the
tags' fields say and what the H2EK's engine was measured to do (the lane's
notes, dlg10-runs/NOTES.md): an event finds its type's patterns; each
pattern whose conditions hold names a speaker (the subject, the cause, a
friend, an enemy, a peer or squad mate, the leader, the vehicle's crew)
and a vocalization, which the speaker's voice must have (else its parent
vocalization's, up the chain); the most specific pattern wins, equals by the
vocalizations' weights; then the vocalization's skip fraction (the player
skip fraction when the line is for the player), its repeat delay for that
speaker, its priority against what the speaker says now (a higher one
interrupts, one that fits in the allowable queue delay waits), its pre
delay before the sound, its notification delay before the listeners may
respond, its post delay before the speaker speaks again. A unit's
involuntary vocalizations (pain, death: unit_dialogue.c's rules with Halo
2's thresholds) go through the involuntary table. The sounds are the
voice's own, bound to a few reusable permutations as they play
(h2_sound.c: h2_sound_voice_*).

HALO_H2_DIALOGUE=0 leaves this engine's rules; HALO_H2_DIALOGUE_LOG=1 logs
every event and decision.
*/

/* ---------- headers */

#include "cseries.h"
#ifdef HALO_LINUX
#include "render_epoch.h"
#endif
#include "h2_dialogue.h"
#include "h2_ai.h"
#include "h2_biped.h"
#include "h2_cache.h"
#include "h2_sound.h"
#include "ai/ai.h"
#include "ai/actors.h"
#include "cutscene/cinematics.h"
#include "ai/actor_types.h"
#include "ai/ai_communication.h"
#include "ai/props.h"
#include "game/game_allegiance.h"
#include "game/game.h"
#include "game/players.h"
#include "math/real_math.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "units/units.h"
#include "tag_files/tag_groups.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the C heap's own, as h2_sound.c: the engine's debug allocator refuses a NULL free) */
#undef malloc
#undef free
#undef calloc

struct h2_cache *h2_scripts_cache(void);

/* ---------- constants */

enum
{
	MAXIMUM_VOCALIZATIONS = 400,
	MAXIMUM_PATTERNS = 320,
	MAXIMUM_RESPONSES = 128,
	MAXIMUM_VOICES = 24,
	MAXIMUM_MODELS = 48,
	MAXIMUM_MODEL_VARIANTS = 20,
	MAXIMUM_UNITS = 2048,
	/* the vocalizations a unit remembers the time it last spoke */
	UNIT_MEMORY = 12,
	MAXIMUM_OPTIONS = 64,
	MAXIMUM_PENDING = 32,
	NAME_SIZE = 32,
	/* how far a line carries to those who may answer or notice it */
	HEARING_DISTANCE = 20,
	/* the globals' blocks (NOTES.md: the MCC layout) */
	ADLG_VOCALIZATIONS = 0x00,
	ADLG_PATTERNS = 0x08,
	ADLG_DIALOGUE_DATA = 0x1C,
	ADLG_INVOLUNTARY = 0x24,
	VOCALIZATION_SIZE = 0x60,
	PATTERN_SIZE = 0x40,
	RESPONSE_SIZE = 0xC,
	UDLG_VOCALIZATIONS = 0x0C,
	UDLG_VOCALIZATION_SIZE = 0x10
};

/* the vocalizations' priorities */
enum
{
	_h2_priority_none = 0,
	_h2_priority_recall,
	_h2_priority_idle,
	_h2_priority_comment,
	_h2_priority_idle_response,
	_h2_priority_postcombat,
	_h2_priority_combat,
	_h2_priority_status,
	_h2_priority_respond,
	_h2_priority_warn,
	_h2_priority_act,
	_h2_priority_react,
	_h2_priority_involuntary,
	_h2_priority_scream,
	_h2_priority_death
};

/* the patterns' speaker types */
enum
{
	_speaker_subject = 0,
	_speaker_cause = 1,
	_speaker_friend = 2,
	_speaker_enemy = 4,
	_speaker_vehicle = 5,
	_speaker_joint = 6,
	_speaker_squad = 7,
	_speaker_leader = 8,
	_speaker_joint_leader = 9,
	_speaker_clump = 10,
	_speaker_peer = 11
};

/* the patterns' hostility (the cause's to the subject) */
enum
{
	_hostility_none = 0,
	_hostility_self = 1,
	_hostility_neutral = 2,
	_hostility_friend = 3,
	_hostility_enemy = 4,
	_hostility_traitor = 5
};

/* the patterns' cause and subject types */
enum
{
	_unit_kind_none = 0,
	_unit_kind_player = 1,
	_unit_kind_vehicle = 5,
	_unit_kind_projectile = 6,
	_unit_kind_actor_or_player = 7,
	_unit_kind_turret = 8,
	_unit_kind_unit_in_vehicle = 9,
	_unit_kind_unit_in_turret = 10,
	_unit_kind_driver = 11,
	_unit_kind_gunner = 12,
	_unit_kind_postcombat = 14,
	_unit_kind_postcombat_won = 15,
	_unit_kind_postcombat_lost = 16,
	_unit_kind_player_masterchief = 17,
	_unit_kind_player_dervish = 18,
	_unit_kind_heretic = 19,
	_unit_kind_majorly_scary = 20,
	_unit_kind_last_man_in_vehicle = 21,
	_unit_kind_male = 22,
	_unit_kind_female = 23,
	_unit_kind_grenade = 24
};

/* the patterns' flags */
enum
{
	_pattern_subject_visible_bit = 0,
	_pattern_cause_visible_bit,
	_pattern_friends_present_bit,
	_pattern_subject_is_target_bit,
	_pattern_cause_is_target_bit,
	_pattern_cause_player_or_speaker_ally_bit,
	_pattern_speaker_searching_bit,
	_pattern_speaker_following_player_bit,
	_pattern_cause_primary_player_ally_bit
};

/* the patterns' conditions: the speaker's combat status (bits 0-9) and
vehicle state */
enum
{
	_condition_no_vehicle_bit = 10,
	_condition_vehicle_driver_bit = 11,
	_condition_vehicle_passenger_bit = 12
};

/* the responses' kinds */
enum
{
	_response_vocalization = 0,
	_response_event = 2
};

/* Halo 2's actor types (the patterns' cause actor type) */
enum
{
	_h2_actor_none = 0,
	_h2_actor_elite = 1,
	_h2_actor_jackal = 2,
	_h2_actor_grunt = 3,
	_h2_actor_hunter = 4,
	_h2_actor_player = 7,
	_h2_actor_marine = 8,
	_h2_actor_combat_form = 10,
	_h2_actor_brute = 17,
	_h2_actor_bugger = 19
};

/* this engine's (unit_dialogue.c's, actors.c's and ai.c's private enums) */
enum
{
	_unit_play_speech_none = 0,
	_unit_play_speech_queue,
	_unit_play_speech_immediate,
	_unit_play_speech_immediate_dequeue
};

enum
{
	_actor_combat_status_none = 0,
	_actor_combat_status_wary,
	_actor_combat_status_investigate,
	_actor_combat_status_definite,
	_actor_combat_status_certain,
	_actor_combat_status_clear_los,
	_actor_combat_status_dangerous,
	_actor_combat_status_visible,
	NUMBER_OF_ACTOR_COMBAT_STATUS_LEVELS
};

enum
{
	_damage_category_none = 0,
	_damage_category_falling,
	_damage_category_bullet,
	_damage_category_grenade,
	_damage_category_highexplosive,
	_damage_category_sniper,
	_damage_category_melee,
	_damage_category_flame,
	_damage_category_mountedweapon,
	_damage_category_vehicle
};

/* ---------- structures */

struct vocalization
{
	char name[NAME_SIZE];
	short parent;
	short priority;
	short perception_type;
	short max_combat_status;
	short animation_impulse;
	short overlap_priority;
	short speaker_emotion;
	short listener_emotion;
	real sound_repetition_delay;    /* minutes */
	real allowable_queue_delay;
	real pre_delay;
	real notification_delay;
	real post_delay;
	real repeat_delay;
	real weight;
	real speaker_freeze_time;
	real listener_freeze_time;
	real player_skip_fraction;
	real skip_fraction;
	short first_response;
	short response_count;
};

struct response
{
	short kind;
	short vocalization;
	short flags;
	short dialogue_type;
};

struct pattern
{
	short type;
	short vocalization;
	short speaker;
	short listener;
	short hostility;
	short damage_type;
	short danger_level;
	short attitude;
	short subject_actor_type;
	short cause_actor_type;
	short cause_kind;
	short subject_kind;
	short spatial_relation;
	unsigned long flags;
	unsigned long conditions;
	/* the cause's / subject's "ai type name" (a unit's or weapon's: ghost,
	warthog, sniper, sword...), "" any */
	char cause_ai_type[16];
	char subject_ai_type[16];
};

struct voice
{
	char name[48];
	long tag;                               /* the map's 'udlg' */
	boolean female;
	/* a line's sound tag of the map (NONE: the voice has no such line) */
	long sound[MAXIMUM_VOCALIZATIONS];
	/* the permutation each line spoke last and when (its sound repetition delay) */
	short last_permutation[MAXIMUM_VOCALIZATIONS];
	long last_time[MAXIMUM_VOCALIZATIONS];
	long lines;
};

struct model
{
	long tag;                               /* the map's 'hlmt' */
	short default_voice;
	short variant_count;
	struct
	{
		char name[NAME_SIZE];
		short voice;
	} variants[MAXIMUM_MODEL_VARIANTS];
};

struct unit_state
{
	long unit_index;                        /* whose (a datum's slot is reused: another unit starts afresh) */
	short voice;                            /* NONE: not looked up; -2: none */
	short memory_next;
	short memory_vocalization[UNIT_MEMORY];
	long memory_time[UNIT_MEMORY];
	/* this engine's pain timers (unit_dialogue.c's rule) */
	short damage_minor_sounds;
	short damage_minor_timer;
	short damage_minor_decay_timer;
	short damage_major_timer;
	/* what it said last (for the responses) and to whom */
	short speaking;                         /* the vocalization (NONE) */
	long listener_unit_index;
	long speaking_since;
	/* a flee yelp due */
	long flee_time;
	/* when the unit was first seen here, when it last grunted in pain, since
	when it has stood still, when it last cowered */
	long created;
	long pain_time;
	long still_since;
	long cower_time;
	/* its first sighting of a foe is behind it (one known as it was placed
	counts: the H2EK's placed fights call out no foe) */
	boolean first_sighting_done;
	/* since when the player has been looking at it (0: not), and whether the
	long look was remarked on */
	long looked_at_since;
	boolean long_look_done;
	/* when it was last surprised (this engine's surprise comes again and
	again in a long fight; Halo 2's is a behaviour's start) */
	long surprise_time;
};

/* a line waiting its pre delay, or a response waiting the notification */
struct pending
{
	boolean used;
	long when;
	long unit_index;                        /* who speaks (a line) / who spoke (a response) */
	long listener_unit_index;
	short vocalization;
	short pattern;
	boolean response;
};

struct option
{
	long unit_index;
	short vocalization;
	short pattern;
	short specificity;
	real weight;
	long listener_unit_index;
	boolean for_player;
};

/* ---------- globals */

static struct
{
	boolean loaded;
	boolean off;
	char map_name[64];
	struct vocalization vocalizations[MAXIMUM_VOCALIZATIONS];
	short vocalization_count;
	struct response responses[MAXIMUM_RESPONSES];
	short response_count;
	struct pattern patterns[MAXIMUM_PATTERNS];
	short pattern_count;
	short type_start[NUMBER_OF_H2_DIALOGUE_TYPES];
	short type_length[NUMBER_OF_H2_DIALOGUE_TYPES];
	short involuntary[NUMBER_OF_H2_INVOLUNTARY_TYPES];
	struct voice *voices;
	short voice_count;
	struct model models[MAXIMUM_MODELS];
	short model_count;
	struct unit_state *units;
	struct pending pending[MAXIMUM_PENDING];
	/* (each team's line playing: Halo 2's team-wide dialogue queue, team_queue_busy) */
	struct { long unit_index, until; short priority; } team_line[16];
	long team_dropped;
	/* the lane's tunables (NOTES.md: measured against the H2EK) */
	real minor_damage_chance;
	real strike_chance;
	real death_chance;
	/* the unit whose death line is being spoken (dead already; NONE) */
	long dying_unit_index;
	long events, spoken, skipped, dropped;
} dlg;

/* ---------- private prototypes */

static void speak(long unit_index, short vocalization, short pattern, long listener_unit_index, boolean from_pending);
static boolean vocalize_named(long unit_index, char const *name);

/* ---------- helpers */

static boolean logging(void)
{
	static int state = -1;

	if (state < 0)
		state = HALO_ENV_ONCE("HALO_H2_DIALOGUE_LOG") != NULL;
	return state != 0;
}

/* no combat dialogue in a cinematic, nor with the triggers off (ai_dialogue_enable
false, a command script's cs_suppress_dialogue_global); the scripted lines
are not this file's */
static boolean suppressed(void)
{
	return cinematic_in_progress() || !ai_globals_dialogue_triggers_are_enabled();
}

static real random_unit(void)
{
	return real_seed_random(get_global_random_seed_address());
}

static real read_real(struct h2_cache *cache, unsigned long address)
{
	void const *p = h2_cache_at(cache, address, 4);
	real value;

	if (!p)
		return 0.0f;
	memcpy(&value, p, 4);
	return value;
}

static short read_short(struct h2_cache *cache, unsigned long address)
{
	void const *p = h2_cache_at(cache, address, 2);
	short value;

	if (!p)
		return 0;
	memcpy(&value, p, 2);
	return value;
}

static short vocalization_by_name(char const *name)
{
	short i;

	for (i = 0; i < dlg.vocalization_count; i++)
	{
		if (!strcmp(dlg.vocalizations[i].name, name))
			return i;
	}
	return NONE;
}

static struct unit_state *unit_state(long unit_index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(unit_index);
	struct unit_state *state;

	if (!dlg.units || absolute < 0 || absolute >= MAXIMUM_UNITS)
		return NULL;
	state = &dlg.units[absolute];
	if (state->unit_index != unit_index)
	{
		memset(state, 0, sizeof(*state));
		state->unit_index = unit_index;
		state->voice = NONE;
		state->speaking = NONE;
		state->listener_unit_index = NONE;
	}
	return state;
}

static boolean unit_exists(long unit_index)
{
	struct object_datum *object = unit_index != NONE ? object_try_and_get(unit_index) : NULL;

	return object && (object->object.type == _object_type_biped || object->object.type == _object_type_vehicle);
}

static boolean unit_alive(long unit_index)
{
	return unit_exists(unit_index) && !TEST_FLAG(object_get(unit_index)->object.damage_flags, _object_dead_bit);
}

static long unit_actor(long unit_index)
{
	struct object_datum *object = unit_index != NONE ? object_try_and_get(unit_index) : NULL;

	if (!object || object->object.type != _object_type_biped)
		return NONE;
	return unit_get(unit_index)->unit.actor_index;
}

static boolean unit_is_player(long unit_index)
{
	struct object_datum *object = unit_index != NONE ? object_try_and_get(unit_index) : NULL;

	return object && object->object.type == _object_type_biped && unit_get(unit_index)->unit.player_index != NONE;
}

static long player_unit(void)
{
	long player_index = local_player_get_player_index(0);

	return player_index != NONE ? player_get(player_index)->unit_index : NONE;
}

static short unit_team(long unit_index)
{
	struct object_datum *object = unit_index != NONE ? object_try_and_get(unit_index) : NULL;

	return object ? object->object.owner_team_index : NONE;
}

/* the relation of one unit to another, as the patterns' hostility has it */
static short relation(long of_unit_index, long to_unit_index)
{
	short a = unit_team(of_unit_index), b = unit_team(to_unit_index);

	if (of_unit_index == NONE || to_unit_index == NONE)
		return _hostility_none;
	if (of_unit_index == to_unit_index)
		return _hostility_self;
	if (a == NONE || b == NONE)
		return _hostility_neutral;
	if (game_team_is_enemy(a, b))
		return _hostility_enemy;
	if (game_team_is_ally(a, b) || a == b)
		return _hostility_friend;
	return _hostility_neutral;
}

static real units_distance(long a, long b)
{
	real_point3d pa, pb;

	if (a == NONE || b == NONE || !object_try_and_get(a) || !object_try_and_get(b))
		return 1.0e9f;
	object_get_origin(a, &pa);
	object_get_origin(b, &pb);
	return distance3d(&pa, &pb);
}

/* Halo 2's actor type of a unit, by its character's name */
static short unit_h2_actor_type(long unit_index)
{
	struct object_datum *object = unit_index != NONE ? object_try_and_get(unit_index) : NULL;
	char const *name;

	if (!object || object->object.type != _object_type_biped)
		return _h2_actor_none;
	if (unit_get(unit_index)->unit.player_index != NONE)
		return _h2_actor_player;
	name = tag_get_name(object->definition_index);
	if (strstr(name, "brute") || strstr(name, "tartarus"))
		return _h2_actor_brute;
	if (strstr(name, "flood"))
		return _h2_actor_combat_form;
	if (strstr(name, "marine") || strstr(name, "johnson") || strstr(name, "crewman"))
		return _h2_actor_marine;
	if (strstr(name, "elite") || strstr(name, "heretic"))
		return _h2_actor_elite;
	if (strstr(name, "grunt"))
		return _h2_actor_grunt;
	if (strstr(name, "jackal"))
		return _h2_actor_jackal;
	if (strstr(name, "hunter"))
		return _h2_actor_hunter;
	if (strstr(name, "bugger"))
		return _h2_actor_bugger;
	return _h2_actor_none;
}

/* whether a unit is of a kind the patterns name */
static boolean unit_is_kind(long unit_index, short kind, long other_unit_index)
{
	struct object_datum *object = unit_index != NONE ? object_try_and_get(unit_index) : NULL;
	long vehicle;

	(void)other_unit_index;
	if (kind == _unit_kind_none)
		return TRUE;
	if (!object)
		return FALSE;
	vehicle = object->object.parent_object_index;
	if (vehicle != NONE && (!object_try_and_get(vehicle) || object_get(vehicle)->object.type != _object_type_vehicle))
		vehicle = NONE;
	switch (kind)
	{
	case _unit_kind_player:
		return unit_is_player(unit_index);
	case _unit_kind_vehicle:
		return object->object.type == _object_type_vehicle;
	case _unit_kind_projectile:
		return object->object.type == _object_type_projectile;
	case _unit_kind_actor_or_player:
		return unit_is_player(unit_index) || unit_actor(unit_index) != NONE;
	case _unit_kind_turret:
		return object->object.type == _object_type_vehicle && strstr(tag_get_name(object->definition_index), "turret") != NULL;
	case _unit_kind_unit_in_vehicle:
		return vehicle != NONE;
	case _unit_kind_unit_in_turret:
		return vehicle != NONE && strstr(tag_get_name(object_get(vehicle)->definition_index), "turret") != NULL;
	case _unit_kind_driver:
		return vehicle != NONE && object->object.type == _object_type_biped &&
			unit_seat_is_driver(vehicle, unit_get(unit_index)->unit.parent_seat_index);
	case _unit_kind_gunner:
		return vehicle != NONE && object->object.type == _object_type_biped &&
			unit_seat_is_gunner(vehicle, unit_get(unit_index)->unit.parent_seat_index);
	case _unit_kind_player_masterchief:
		return unit_is_player(unit_index) && !strstr(tag_get_name(object->definition_index), "elite") &&
			!strstr(tag_get_name(object->definition_index), "dervish");
	case _unit_kind_player_dervish:
		return unit_is_player(unit_index) && (strstr(tag_get_name(object->definition_index), "elite") != NULL ||
			strstr(tag_get_name(object->definition_index), "dervish") != NULL);
	case _unit_kind_heretic:
		return strstr(tag_get_name(object->definition_index), "heretic") != NULL;
	case _unit_kind_majorly_scary:
		return object->object.type == _object_type_biped && (strstr(tag_get_name(object->definition_index), "hunter") != NULL ||
			strstr(tag_get_name(object->definition_index), "tartarus") != NULL || strstr(tag_get_name(object->definition_index), "juggernaut") != NULL);
	case _unit_kind_last_man_in_vehicle:
		return vehicle != NONE;
	case _unit_kind_male:
	case _unit_kind_female:
		{
			short voice = h2_dialogue_unit_voice(unit_index);

			if (voice == NONE)
				return kind == _unit_kind_male;
			return (dlg.voices[voice].female != 0) == (kind == _unit_kind_female);
		}
	case _unit_kind_grenade:
		return FALSE;
	default:
		return FALSE;
	}
}

/* whether a unit is of an "ai type name" the patterns name: its own kind
(a vehicle's: ghost, warthog...), the vehicle it rides, or the weapon it
holds (sniper, sword) - by the names of this engine's tags for them */
static boolean name_has_type(char const *tag_name, char const *type)
{
	char const *leaf = tag_name && strrchr(tag_name, '\\') ? strrchr(tag_name, '\\') + 1 : tag_name;

	if (!leaf)
		return FALSE;
	if (!strcmp(type, "sword"))
		return strstr(leaf, "energy_blade") != NULL || strstr(leaf, "energy_sword") != NULL || strstr(leaf, "sword") != NULL;
	if (!strcmp(type, "warthog"))
		return strstr(leaf, "warthog") != NULL || strstr(leaf, "hog") != NULL;
	return strstr(leaf, type) != NULL;
}

static boolean unit_has_ai_type(long unit_index, char const *type)
{
	struct object_datum *object = unit_index != NONE ? object_try_and_get(unit_index) : NULL;
	long vehicle, weapon;

	if (!type[0])
		return TRUE;
	if (!object)
		return FALSE;
	if (name_has_type(tag_get_name(object->definition_index), type))
		return TRUE;
	vehicle = object->object.parent_object_index;
	if (vehicle != NONE && object_try_and_get(vehicle) && name_has_type(tag_get_name(object_get(vehicle)->definition_index), type))
		return TRUE;
	if (object->object.type == _object_type_biped || object->object.type == _object_type_vehicle)
	{
		struct unit_datum *unit = unit_get(unit_index);

		weapon = unit->unit.current_weapon_index >= 0 && unit->unit.current_weapon_index < MAXIMUM_WEAPONS_PER_UNIT ?
			unit->unit.weapon_object_indices[unit->unit.current_weapon_index] : NONE;
		if (weapon != NONE && object_try_and_get(weapon) && name_has_type(tag_get_name(object_get(weapon)->definition_index), type))
			return TRUE;
	}
	return FALSE;
}

/* a unit first seen here: when, and whether it was placed within sight of
its foes (40 wu) - then it knows of them from the start, as the H2EK's
placed squads do, whose fights call out no foe; one placed out of their
sight calls the first it sees */
static void state_first_seen(struct unit_state *state, long unit_index)
{
	struct data_iterator others;
	struct actor_datum *other;

	if (state->created)
		return;
	state->created = game_time_get();
	data_iterator_new(&others, actor_data);
	while (!state->first_sighting_done && (other = (struct actor_datum *)data_iterator_next(&others)) != NULL)
	{
		if (other->meta.unit_index != NONE && other->meta.unit_index != unit_index && unit_alive(other->meta.unit_index) &&
			relation(unit_index, other->meta.unit_index) == _hostility_enemy && units_distance(unit_index, other->meta.unit_index) < 40.0f)
		{
			state->first_sighting_done = TRUE;
		}
	}
}

/* whether an actor sees (or saw within the last two seconds) a unit */
static boolean actor_sees(long actor_index, long unit_index)
{
	long prop_index;
	struct prop_datum *prop;

	if (actor_index == NONE || unit_index == NONE)
		return FALSE;
	prop_index = prop_get_active_by_unit_index(actor_index, unit_index);
	if (prop_index == NONE)
		return FALSE;
	prop = prop_get(prop_index);
	return prop->visible_ticks > 0 || (prop->last_visible_time != NONE && game_time_get() - prop->last_visible_time < 2 * TICKS_PER_SECOND);
}

/* whether an actor knows of a unit at all (has a prop of it) */
static boolean actor_knows(long actor_index, long unit_index)
{
	return actor_index != NONE && unit_index != NONE && prop_get_active_by_unit_index(actor_index, unit_index) != NONE;
}

/* an actor's combat status on Halo 2's scale (asleep 0 .. dangerous 9) */
static short actor_h2_combat_status(struct actor_datum const *actor)
{
	static short const table[NUMBER_OF_ACTOR_COMBAT_STATUS_LEVELS] = { 1, 2, 4, 5, 6, 8, 9, 7 };
	short s = actor->state.combat_status;

	if (actor->meta.dormant)
		return 0;
	if (s < 0 || s >= NUMBER_OF_ACTOR_COMBAT_STATUS_LEVELS)
		return 1;
	return table[s];
}

/* ---------- the level */

static void voices_free(void)
{
	if (dlg.voices)
		free(dlg.voices);
	dlg.voices = NULL;
	if (dlg.units)
		free(dlg.units);
	dlg.units = NULL;
}

static boolean globals_read(struct h2_cache *cache)
{
	long tag, count = h2_cache_tag_count(cache), n, i, j;
	struct h2_cache_tag t;
	unsigned long elements, e;

	for (tag = 0; tag < count; tag++)
	{
		if (h2_cache_tag_get(cache, tag, &t) && t.group_tag == 'adlg')
			break;
	}
	if (tag >= count)
		return FALSE;
	/* the vocalizations */
	if (!h2_cache_block(cache, t.address + ADLG_VOCALIZATIONS, VOCALIZATION_SIZE, &n, &elements) || n < 1)
		return FALSE;
	for (i = 0; i < n && i < MAXIMUM_VOCALIZATIONS; i++)
	{
		struct vocalization *v = &dlg.vocalizations[i];
		long response_count;
		unsigned long responses;

		e = elements + (unsigned long)i * VOCALIZATION_SIZE;
		memset(v, 0, sizeof(*v));
		h2_cache_string(cache, h2_cache_read32(cache, e + 0x00), v->name, sizeof(v->name));
		v->parent = read_short(cache, e + 0x08);
		v->priority = read_short(cache, e + 0x0A);
		v->perception_type = read_short(cache, e + 0x14);
		v->max_combat_status = read_short(cache, e + 0x16);
		v->animation_impulse = read_short(cache, e + 0x18);
		v->overlap_priority = read_short(cache, e + 0x1A);
		v->sound_repetition_delay = read_real(cache, e + 0x1C);
		v->allowable_queue_delay = read_real(cache, e + 0x20);
		v->pre_delay = read_real(cache, e + 0x24);
		v->notification_delay = read_real(cache, e + 0x28);
		v->post_delay = read_real(cache, e + 0x2C);
		v->repeat_delay = read_real(cache, e + 0x30);
		v->weight = read_real(cache, e + 0x34);
		v->speaker_freeze_time = read_real(cache, e + 0x38);
		v->listener_freeze_time = read_real(cache, e + 0x3C);
		v->speaker_emotion = read_short(cache, e + 0x40);
		v->listener_emotion = read_short(cache, e + 0x42);
		v->player_skip_fraction = read_real(cache, e + 0x44);
		v->skip_fraction = read_real(cache, e + 0x48);
		v->first_response = dlg.response_count;
		if (h2_cache_block(cache, e + 0x50, RESPONSE_SIZE, &response_count, &responses))
		{
			for (j = 0; j < response_count && dlg.response_count < MAXIMUM_RESPONSES; j++)
			{
				struct response *r = &dlg.responses[dlg.response_count++];
				unsigned long re = responses + (unsigned long)j * RESPONSE_SIZE;

				r->kind = read_short(cache, re + 4);
				r->vocalization = read_short(cache, re + 6);
				r->flags = read_short(cache, re + 8);
				r->dialogue_type = read_short(cache, re + 0xA);
				v->response_count++;
			}
		}
		/* (a parent's index past the block, or itself: none) */
		if (v->parent >= n || v->parent == i)
			v->parent = NONE;
	}
	dlg.vocalization_count = (short)MIN(n, MAXIMUM_VOCALIZATIONS);
	/* (a vocalization's parent index that is 0 but whose name is not the
	first's child: the globals' root entries have 0 there; a real parent has
	a parent name) */
	for (i = 0; i < dlg.vocalization_count; i++)
	{
		unsigned long parent_name = h2_cache_read32(cache, elements + (unsigned long)i * VOCALIZATION_SIZE + 4);

		if (!parent_name || parent_name == 0xFFFFFFFFUL)
			dlg.vocalizations[i].parent = NONE;
	}
	/* the patterns */
	if (!h2_cache_block(cache, t.address + ADLG_PATTERNS, PATTERN_SIZE, &n, &elements))
		return FALSE;
	for (i = 0; i < n && i < MAXIMUM_PATTERNS; i++)
	{
		struct pattern *p = &dlg.patterns[i];
		char name[NAME_SIZE];

		e = elements + (unsigned long)i * PATTERN_SIZE;
		p->type = read_short(cache, e + 0x00);
		p->vocalization = read_short(cache, e + 0x02);
		/* (the name names the vocalization where the index does not) */
		if ((p->vocalization < 0 || p->vocalization >= dlg.vocalization_count) &&
			h2_cache_string(cache, h2_cache_read32(cache, e + 0x04), name, sizeof(name)))
		{
			p->vocalization = vocalization_by_name(name);
		}
		p->speaker = read_short(cache, e + 0x08);
		p->flags = (unsigned long)(unsigned short)read_short(cache, e + 0x0A);
		p->listener = read_short(cache, e + 0x0C);
		p->hostility = read_short(cache, e + 0x14);
		p->damage_type = read_short(cache, e + 0x16);
		p->danger_level = read_short(cache, e + 0x18);
		p->attitude = read_short(cache, e + 0x1A);
		p->subject_actor_type = read_short(cache, e + 0x1C);
		p->cause_actor_type = read_short(cache, e + 0x22);
		p->cause_kind = read_short(cache, e + 0x24);
		p->subject_kind = read_short(cache, e + 0x26);
		p->spatial_relation = read_short(cache, e + 0x2C);
		p->conditions = h2_cache_read32(cache, e + 0x3C);
		h2_cache_string(cache, h2_cache_read32(cache, e + 0x28), p->cause_ai_type, sizeof(p->cause_ai_type));
		h2_cache_string(cache, h2_cache_read32(cache, e + 0x30), p->subject_ai_type, sizeof(p->subject_ai_type));
	}
	dlg.pattern_count = (short)MIN(n, MAXIMUM_PATTERNS);
	/* the types' ranges */
	for (i = 0; i < NUMBER_OF_H2_DIALOGUE_TYPES; i++)
	{
		dlg.type_start[i] = NONE;
		dlg.type_length[i] = 0;
	}
	if (h2_cache_block(cache, t.address + ADLG_DIALOGUE_DATA, 4, &n, &elements))
	{
		for (i = 0; i < n && i < NUMBER_OF_H2_DIALOGUE_TYPES; i++)
		{
			dlg.type_start[i] = read_short(cache, elements + (unsigned long)i * 4);
			dlg.type_length[i] = read_short(cache, elements + (unsigned long)i * 4 + 2);
			if (dlg.type_start[i] < 0 || dlg.type_start[i] >= dlg.pattern_count)
				dlg.type_start[i] = NONE, dlg.type_length[i] = 0;
		}
	}
	else
	{
		/* (no ranges: by the patterns' own types) */
		for (i = 0; i < dlg.pattern_count; i++)
		{
			short ty = dlg.patterns[i].type;

			if (ty < 0 || ty >= NUMBER_OF_H2_DIALOGUE_TYPES)
				continue;
			if (dlg.type_start[ty] == NONE)
				dlg.type_start[ty] = (short)i;
			dlg.type_length[ty] = (short)(i + 1 - dlg.type_start[ty]);
		}
	}
	/* the involuntary table */
	for (i = 0; i < NUMBER_OF_H2_INVOLUNTARY_TYPES; i++)
		dlg.involuntary[i] = NONE;
	if (h2_cache_block(cache, t.address + ADLG_INVOLUNTARY, 4, &n, &elements))
	{
		for (i = 0; i < n && i < NUMBER_OF_H2_INVOLUNTARY_TYPES; i++)
		{
			short v = read_short(cache, elements + (unsigned long)i * 4);

			dlg.involuntary[i] = v >= 0 && v < dlg.vocalization_count ? v : NONE;
		}
	}
	return TRUE;
}

static short voice_add(struct h2_cache *cache, long tag)
{
	struct h2_cache_tag t;
	struct voice *v;
	long n, i;
	unsigned long elements;
	char const *name;
	short k;

	for (k = 0; k < dlg.voice_count; k++)
	{
		if (dlg.voices[k].tag == tag)
			return k;
	}
	if (tag == NONE || !h2_cache_tag_get(cache, tag, &t) || t.group_tag != 'udlg' || dlg.voice_count >= MAXIMUM_VOICES)
		return NONE;
	v = &dlg.voices[dlg.voice_count];
	memset(v, 0, sizeof(*v));
	v->tag = tag;
	name = h2_cache_tag_name(cache, tag);
	snprintf(v->name, sizeof(v->name), "%s", strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name);
	v->female = (h2_cache_read32(cache, t.address + 8) & 1) != 0;
	for (i = 0; i < MAXIMUM_VOCALIZATIONS; i++)
	{
		v->sound[i] = NONE;
		v->last_permutation[i] = NONE;
		v->last_time[i] = NONE;
	}
	if (h2_cache_block(cache, t.address + UDLG_VOCALIZATIONS, UDLG_VOCALIZATION_SIZE, &n, &elements))
	{
		for (i = 0; i < n; i++)
		{
			unsigned long e = elements + (unsigned long)i * UDLG_VOCALIZATION_SIZE;
			char line[NAME_SIZE];
			long sound = h2_cache_reference(cache, e + 8);
			short voc;

			if (sound == NONE || !h2_cache_string(cache, h2_cache_read32(cache, e + 4), line, sizeof(line)))
				continue;
			voc = vocalization_by_name(line);
			if (voc == NONE)
				continue;
			v->sound[voc] = sound;
			v->lines++;
		}
	}
	return dlg.voice_count++;
}

/* the models' voices: every 'hlmt' of the map that names a dialogue (its
default's or a variant's) */
static void models_read(struct h2_cache *cache)
{
	long tag, count = h2_cache_tag_count(cache);

	for (tag = 0; tag < count && dlg.model_count < MAXIMUM_MODELS; tag++)
	{
		struct h2_cache_tag t;
		struct model m;
		long variant_count, i;
		unsigned long variants;
		boolean any = FALSE;

		if (!h2_cache_tag_get(cache, tag, &t) || t.group_tag != 'hlmt')
			continue;
		memset(&m, 0, sizeof(m));
		m.tag = tag;
		m.default_voice = voice_add(cache, h2_cache_reference(cache, t.address + 0x8C));
		any = m.default_voice != NONE;
		if (h2_cache_block(cache, t.address + 0x50, 0x38, &variant_count, &variants))
		{
			for (i = 0; i < variant_count && m.variant_count < MAXIMUM_MODEL_VARIANTS; i++)
			{
				unsigned long e = variants + (unsigned long)i * 0x38;
				short voice = voice_add(cache, h2_cache_reference(cache, e + 0x30));

				if (!h2_cache_string(cache, h2_cache_read32(cache, e), m.variants[m.variant_count].name, NAME_SIZE))
					continue;
				m.variants[m.variant_count].voice = voice;
				m.variant_count++;
				any = any || voice != NONE;
			}
		}
		if (any)
			dlg.models[dlg.model_count++] = m;
	}
}

void h2_dialogue_dispose(void)
{
	voices_free();
	memset(&dlg, 0, sizeof(dlg));
}

void h2_dialogue_map_loaded(char const *map_name)
{
	struct h2_cache *cache = h2_scripts_cache();
	char const *env;
	short i;

	h2_dialogue_dispose();
	if (!cache || !h2_ai_running())
		return;
	env = HALO_ENV_ONCE("HALO_H2_DIALOGUE");
	if (env && !strcmp(env, "0"))
	{
		dlg.off = TRUE;
		return;
	}
	snprintf(dlg.map_name, sizeof(dlg.map_name), "%s", map_name ? map_name : "");
	if (!globals_read(cache))
	{
		platform_log("h2 dialogue: %s has no ai dialogue globals; this engine's rules stay", dlg.map_name);
		return;
	}
	dlg.voices = (struct voice *)calloc(MAXIMUM_VOICES, sizeof(struct voice));
	dlg.units = (struct unit_state *)calloc(MAXIMUM_UNITS, sizeof(struct unit_state));
	if (!dlg.voices || !dlg.units)
	{
		voices_free();
		return;
	}
	for (i = 0; i < MAXIMUM_UNITS; i++)
	{
		dlg.units[i].unit_index = NONE;
		dlg.units[i].voice = NONE;
		dlg.units[i].speaking = NONE;
		dlg.units[i].listener_unit_index = NONE;
	}
	models_read(cache);
	dlg.minor_damage_chance = HALO_ENV_ONCE("HALO_H2_DIALOGUE_MINOR_CHANCE") ? (real)atof(HALO_ENV_ONCE("HALO_H2_DIALOGUE_MINOR_CHANCE")) : 0.45f;
	dlg.dying_unit_index = NONE;
	dlg.death_chance = HALO_ENV_ONCE("HALO_H2_DIALOGUE_DEATH_CHANCE") ? (real)atof(HALO_ENV_ONCE("HALO_H2_DIALOGUE_DEATH_CHANCE")) : 0.08f;
	dlg.strike_chance = HALO_ENV_ONCE("HALO_H2_DIALOGUE_STRIKE_CHANCE") ? (real)atof(HALO_ENV_ONCE("HALO_H2_DIALOGUE_STRIKE_CHANCE")) : 0.012f;
	dlg.loaded = TRUE;
	{
		long lines = 0;

		for (i = 0; i < dlg.voice_count; i++)
			lines += dlg.voices[i].lines;
		platform_log("h2 dialogue: %s: %d vocalizations (%d responses), %d patterns, %d voices (%ld lines), %d models",
			dlg.map_name, (int)dlg.vocalization_count, (int)dlg.response_count, (int)dlg.pattern_count, (int)dlg.voice_count, lines,
			(int)dlg.model_count);
		if (logging())
		{
			for (i = 0; i < dlg.voice_count; i++)
				platform_log("h2 dialogue: voice %d %s: %ld lines%s", (int)i, dlg.voices[i].name, dlg.voices[i].lines, dlg.voices[i].female ? " (female)" : "");
		}
	}
}

boolean h2_dialogue_active(void)
{
	return dlg.loaded;
}

char const *h2_dialogue_voice_name(short voice)
{
	return dlg.loaded && voice >= 0 && voice < dlg.voice_count ? dlg.voices[voice].name : "";
}

/* ---------- the units' voices */

short h2_dialogue_unit_voice(long unit_index)
{
	struct unit_state *state;
	struct object_datum *object;
	char path[128], variant[NAME_SIZE];
	struct h2_cache *cache;
	long bipd, model_tag;
	short m, i, voice = NONE;

	if (!dlg.loaded || unit_index == NONE || !(object = object_try_and_get(unit_index)) || object->object.type != _object_type_biped ||
		!(state = unit_state(unit_index)))
	{
		return NONE;
	}
	if (state->voice != NONE)
		return state->voice >= 0 ? state->voice : NONE;
	state->voice = -2;
	if (unit_get(unit_index)->unit.player_index != NONE || !(cache = h2_scripts_cache()))
		return NONE;
	if (!h2_ai_object_character(unit_index, path, sizeof(path), variant, sizeof(variant)))
		return NONE;
	bipd = h2_cache_tag_find(cache, 'bipd', path);
	if (bipd == NONE)
		return NONE;
	{
		struct h2_cache_tag t;

		if (!h2_cache_tag_get(cache, bipd, &t))
			return NONE;
		model_tag = h2_cache_reference(cache, t.address + 0x34);
	}
	for (m = 0; m < dlg.model_count && dlg.models[m].tag != model_tag; m++)
		;
	if (m == dlg.model_count)
		return NONE;
	for (i = 0; i < dlg.models[m].variant_count && voice == NONE; i++)
	{
		if (variant[0] && !strcmp(dlg.models[m].variants[i].name, variant))
			voice = dlg.models[m].variants[i].voice;
	}
	if (voice == NONE)
		voice = dlg.models[m].default_voice;
	/* (no variant chosen and no default: the first variant's) */
	if (voice == NONE && !variant[0] && dlg.models[m].variant_count)
		voice = dlg.models[m].variants[0].voice;
	state->voice = voice == NONE ? -2 : voice;
	if (logging())
		platform_log("h2 dialogue: %s (%s) speaks with %s", tag_get_name(object->definition_index), variant[0] ? variant : "no variant",
			voice == NONE ? "no voice" : dlg.voices[voice].name);
	return voice;
}

/* the vocalization a voice speaks for one: it or its first ancestor the
voice has a line of (NONE: none) */
static short voice_line(short voice, short vocalization)
{
	short guard;

	if (voice == NONE)
		return NONE;
	for (guard = 0; vocalization != NONE && guard < 8; guard++)
	{
		if (dlg.voices[voice].sound[vocalization] != NONE)
			return vocalization;
		vocalization = dlg.vocalizations[vocalization].parent;
	}
	return NONE;
}

/* ---------- the speaker's state */

static long unit_last_spoke(struct unit_state const *state, short vocalization)
{
	short i;
	long last = NONE;

	for (i = 0; i < UNIT_MEMORY; i++)
	{
		if (state->memory_vocalization[i] == vocalization && state->memory_time[i] != NONE && (last == NONE || state->memory_time[i] > last))
			last = state->memory_time[i];
	}
	return last;
}

static void unit_remember(struct unit_state *state, short vocalization)
{
	state->memory_vocalization[state->memory_next] = vocalization;
	state->memory_time[state->memory_next] = game_time_get();
	state->memory_next = (short)((state->memory_next + 1) % UNIT_MEMORY);
}

/* this engine's speech priority a Halo 2 priority plays as (unit_dialogue.c's
tables are not consulted: the decision is made here) */
static short ce_priority(short h2_priority)
{
	switch (h2_priority)
	{
	case _h2_priority_death:
		return _unit_speech_death;
	case _h2_priority_scream:
		return _unit_speech_scream;
	case _h2_priority_involuntary:
		return _unit_speech_involuntary;
	case _h2_priority_react:
		return _unit_speech_exclamation;
	case _h2_priority_act:
	case _h2_priority_warn:
		return _unit_speech_shout;
	case _h2_priority_respond:
		return _unit_speech_communicate;
	case _h2_priority_status:
	case _h2_priority_combat:
		return _unit_speech_talk;
	default:
		return _unit_speech_idle;
	}
}

/* the Halo 2 priority of what a unit says now (none) */
static short unit_current_priority(long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	short type = unit->unit.speech.current.vocalization_type;

	if (unit->unit.speech.current.priority == _unit_speech_none)
		return _h2_priority_none;
	if (type >= H2_DIALOGUE_VOCALIZATION_BASE && type - H2_DIALOGUE_VOCALIZATION_BASE < dlg.vocalization_count)
		return dlg.vocalizations[type - H2_DIALOGUE_VOCALIZATION_BASE].priority;
	/* (a scripted line: above all but death) */
	return _h2_priority_scream;
}

/* ---------- speaking */

/* the length of a voice's line, seconds (the sound's longest permutation) */
static real line_seconds(short voice, short vocalization)
{
	real seconds = h2_sound_voice_seconds(dlg.voices[voice].tag, dlg.voices[voice].sound[vocalization]);

	return seconds > 0.0f ? seconds : 1.5f;
}

/* a unit speaks a vocalization now (or queues it): the item given to this
engine's speech, the sound played as it starts (h2_dialogue_speech_play) */
static void speak(long unit_index, short vocalization, short pattern, long listener_unit_index, boolean from_pending)
{
	struct unit_state *state = unit_state(unit_index);
	struct vocalization const *v = &dlg.vocalizations[vocalization];
	struct unit_datum *unit;
	struct unit_speech_item item;
	short current, play_type = _unit_play_speech_none;
	short voice;

	/* (a death line is the dead one's) */
	if (!state || suppressed() || !(unit_index == dlg.dying_unit_index ? unit_exists(unit_index) : unit_alive(unit_index)) ||
		(voice = h2_dialogue_unit_voice(unit_index)) == NONE)
	{
		return;
	}
	unit = unit_get(unit_index);
	/* the pre delay: waited here, then spoken */
	if (!from_pending && v->pre_delay > 0.0f)
	{
		short i;

		for (i = 0; i < MAXIMUM_PENDING; i++)
		{
			if (!dlg.pending[i].used)
			{
				dlg.pending[i].used = TRUE;
				dlg.pending[i].response = FALSE;
				dlg.pending[i].when = game_time_get() + (long)(v->pre_delay * TICKS_PER_SECOND);
				dlg.pending[i].unit_index = unit_index;
				dlg.pending[i].listener_unit_index = listener_unit_index;
				dlg.pending[i].vocalization = vocalization;
				dlg.pending[i].pattern = pattern;
				return;
			}
		}
	}
	current = unit_current_priority(unit_index);
	if (current == _h2_priority_none)
	{
		play_type = _unit_play_speech_immediate;
	}
	else if (v->priority > current && (v->priority >= _h2_priority_involuntary || !unit->unit.speech.played))
	{
		/* (a scream or a death cuts a line; a higher line takes one that has
		not started yet) */
		play_type = _unit_play_speech_immediate_dequeue;
	}
	else if (v->priority > current && unit->unit.speech.sound_timer + unit->unit.speech.post_delay_timer <= (short)(v->allowable_queue_delay * TICKS_PER_SECOND))
	{
		/* (a higher line within the wait: queued, taking any lower one queued) */
		short queued = unit->unit.speech.queued.vocalization_type;
		short queued_priority = unit->unit.speech.queued.priority == _unit_speech_none ? _h2_priority_none :
			(queued >= H2_DIALOGUE_VOCALIZATION_BASE && queued - H2_DIALOGUE_VOCALIZATION_BASE < dlg.vocalization_count ?
				dlg.vocalizations[queued - H2_DIALOGUE_VOCALIZATION_BASE].priority : _h2_priority_scream);

		if (v->priority > queued_priority)
			play_type = _unit_play_speech_queue;
	}
	if (play_type == _unit_play_speech_none)
	{
		dlg.dropped++;
		if (logging())
			platform_log("h2 dialogue: %s keeps quiet: %s (priority %d) while saying one of priority %d", dlg.voices[voice].name, v->name,
				(int)v->priority, (int)current);
		return;
	}
	memset(&item, 0, sizeof(item));
	item.priority = unit_index == dlg.dying_unit_index ? _unit_speech_death : ce_priority(v->priority);
	item.vocalization_type = (short)(H2_DIALOGUE_VOCALIZATION_BASE + vocalization);
	item.sound_definition_index = NONE;
	item.delay_time = 0;
	item.ai_notification_delay = (short)(v->notification_delay * TICKS_PER_SECOND);
	item.pause_time = (short)(v->post_delay * TICKS_PER_SECOND);
	ai_communication_packet_new(&item.ai);
	unit_speak(unit_index, play_type, &item);
	if (play_type >= _unit_play_speech_immediate)
	{
		/* (unit_speak reads the sound's length from a sound tag: none here;
		the line's own) */
		unit->unit.speech.sound_timer = (short)(line_seconds(voice, vocalization) * TICKS_PER_SECOND + 0.5f);
		if (unit->unit.speech.sound_timer < 1)
			unit->unit.speech.sound_timer = 1;
	}
	if (play_type >= _unit_play_speech_immediate && unit_index != dlg.dying_unit_index)
	{
		short team = unit_team(unit_index);

		if (team >= 0 && team < (short)NUMBEROF(dlg.team_line))
		{
			dlg.team_line[team].unit_index = unit_index;
			dlg.team_line[team].until = game_time_get() + unit->unit.speech.sound_timer + (long)(v->post_delay * TICKS_PER_SECOND);
			dlg.team_line[team].priority = v->priority;
		}
	}
	unit_remember(state, vocalization);
	state->speaking = vocalization;
	state->listener_unit_index = listener_unit_index;
	state->speaking_since = game_time_get();
	dlg.spoken++;
	if (logging())
		platform_log("h2 dialogue: %ld: %s says %s%s (priority %d, pattern %d, to %lx)", game_time_get(), dlg.voices[voice].name, v->name,
			play_type == _unit_play_speech_queue ? " (queued)" : "", (int)v->priority, (int)pattern, (unsigned long)listener_unit_index);
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
	{
		extern void h2_range_voice(long unit_index, short vocalization_type, char const *type_name, char const *line);
		char line[96];

		snprintf(line, sizeof(line), "%s/%s", dlg.voices[voice].name, v->name);
		h2_range_voice(unit_index, item.vocalization_type, v->name, line);
	}
#endif
}

/* Halo 2's dialogue queue is per TEAM (the H2EK tag test's ai_render_dialogue_queue_team; the queue code is not
decompiled): an AI line - an event's, a behaviour's - waits its turn behind the team's line playing, and one that may
not wait that long (its allowable queue delay) is not said. The real engine's 05a Grunts with their Elites shot: 13
retreating, 0-1 fear_rtrt_ldrdead and 0-1 flee lines a watch, cower 6-11; with a per-unit repeat delay ours said 4-6
of each and 23 cower. Involuntary lines (pain, death: the unit's own, unit_dialogue) do not queue.
HALO_H2_DIALOGUE_NO_TEAM_QUEUE=1: off */
static boolean team_queue_busy(long unit_index, short vocalization)
{
	short team = unit_team(unit_index);
	struct vocalization const *v = &dlg.vocalizations[vocalization];
	static int off = -1;

	if (off < 0)
		off = HALO_ENV_ONCE("HALO_H2_DIALOGUE_NO_TEAM_QUEUE") != NULL;
	if (off || team < 0 || team >= (short)NUMBEROF(dlg.team_line) || dlg.team_line[team].unit_index == NONE ||
		dlg.team_line[team].unit_index == unit_index || game_time_get() >= dlg.team_line[team].until ||
		!unit_alive(dlg.team_line[team].unit_index) || v->priority > dlg.team_line[team].priority)
	{
		return FALSE;
	}
	if (dlg.team_line[team].until - game_time_get() <= (long)(v->allowable_queue_delay * TICKS_PER_SECOND))
		return FALSE;
	dlg.team_dropped++;
	if (logging())
		platform_log("h2 dialogue: %ld: %s not said: its team is speaking", game_time_get(), v->name);
	return TRUE;
}

/* the skip fraction: whether a vocalization is passed over this time */
static boolean skipped(struct vocalization const *v, boolean for_player)
{
	real fraction = for_player ? v->player_skip_fraction : v->skip_fraction;

	if (fraction <= 0.0f)
		return FALSE;
	return random_unit() < fraction;
}

/* whether a speaker may say a vocalization now: alive, with a voice that has
it (the line found), its repeat delay over, its combat status allowed */
static boolean speaker_may(long unit_index, short vocalization, short *line, unsigned long conditions)
{
	struct unit_state *state = unit_state(unit_index);
	short voice, l;
	long last, actor_index;
	struct vocalization const *v;

	if (!state || !unit_alive(unit_index) || (voice = h2_dialogue_unit_voice(unit_index)) == NONE)
		return FALSE;
	if (!h2_ai_actor_may_speak_unit(unit_index))
		return FALSE;
	l = voice_line(voice, vocalization);
	if (l == NONE)
		return FALSE;
	v = &dlg.vocalizations[vocalization];
	last = unit_last_spoke(state, l);
	if (last != NONE && v->repeat_delay > 0.0f && game_time_get() - last < (long)(v->repeat_delay * TICKS_PER_SECOND))
		return FALSE;
	actor_index = unit_actor(unit_index);
	if (actor_index != NONE)
	{
		struct actor_datum const *actor = actor_get(actor_index);
		short status = actor_h2_combat_status(actor);
		struct object_datum const *object = object_get(unit_index);
		boolean in_vehicle = object->object.parent_object_index != NONE;

		if (status > v->max_combat_status && v->max_combat_status > 0)
			return FALSE;
		/* (unknown_20fe20.cpp 0x20d8c0: an actor in its retreat behaviour (0x38 -
		flee, cower: this engine's flee action) says nothing under scream
		priority (13: flee, cower, fear lines, deaths) - the H2EK's lone e1 Grunt,
		fleeing, says no newordr_fallback when its squad's order falls back.
		HALO_H2_RETREAT_SPEAKS=1: as before) */
		if (actor->state.action == _actor_action_flee && v->priority < _h2_priority_scream && !getenv("HALO_H2_RETREAT_SPEAKS"))
			return FALSE;
		if (conditions && !TEST_FLAG(conditions, status))
			return FALSE;
		if (conditions)
		{
			if (!in_vehicle && !TEST_FLAG(conditions, _condition_no_vehicle_bit))
				return FALSE;
			if (in_vehicle)
			{
				boolean driver = actor->input.vehicle_driver_type != 0;

				if (driver && !TEST_FLAG(conditions, _condition_vehicle_driver_bit))
					return FALSE;
				if (!driver && !TEST_FLAG(conditions, _condition_vehicle_passenger_bit))
					return FALSE;
			}
		}
	}
	*line = l;
	return TRUE;
}

/* ---------- the events */

/* the pattern's conditions that do not depend on the speaker */
static boolean pattern_fits(struct pattern const *p, long subject, long cause, short damage_category, short *specificity)
{
	short s = 0;

	if (p->vocalization < 0 || p->vocalization >= dlg.vocalization_count)
		return FALSE;
	if (p->hostility != _hostility_none)
	{
		short r = relation(cause, subject);

		if (cause == NONE || r != p->hostility)
			return FALSE;
		s++;
	}
	if (p->damage_type != 0)
	{
		if (damage_category != p->damage_type)
			return FALSE;
		s += 2;
	}
	if (p->cause_kind != _unit_kind_none)
	{
		if (cause == NONE || !unit_is_kind(cause, p->cause_kind, subject))
			return FALSE;
		s += p->cause_kind == _unit_kind_actor_or_player ? 1 : 2;
	}
	if (p->subject_kind != _unit_kind_none)
	{
		if (subject == NONE || !unit_is_kind(subject, p->subject_kind, cause))
			return FALSE;
		s += 2;
	}
	if (p->cause_actor_type != _h2_actor_none)
	{
		if (cause == NONE || unit_h2_actor_type(cause) != p->cause_actor_type)
			return FALSE;
		s += 2;
	}
	if (p->subject_actor_type != _h2_actor_none)
	{
		if (subject == NONE || unit_h2_actor_type(subject) != p->subject_actor_type)
			return FALSE;
		s += 2;
	}
	if (p->cause_ai_type[0])
	{
		if (cause == NONE || !unit_has_ai_type(cause, p->cause_ai_type))
			return FALSE;
		s += 3;
	}
	if (p->subject_ai_type[0])
	{
		if (subject == NONE || !unit_has_ai_type(subject, p->subject_ai_type))
			return FALSE;
		s += 3;
	}
	if (p->spatial_relation != 0)
	{
		real d = units_distance(subject, cause);
		real_point3d a, b;
		boolean ok = FALSE;

		if (subject == NONE || cause == NONE || d > 1.0e8f)
			return FALSE;
		object_get_origin(subject, &a);
		object_get_origin(cause, &b);
		switch (p->spatial_relation)
		{
		case 1: ok = d < 1.0f; break;
		case 2: ok = d < 2.5f; break;
		case 3: ok = d < 5.0f; break;
		case 4: ok = d < 10.0f; break;
		case 5: ok = d >= 10.0f; break;
		case 6: case 7: ok = TRUE; break;
		case 8: ok = b.z - a.z > 1.0f; break;
		case 9: ok = a.z - b.z > 1.0f; break;
		default: ok = FALSE; break;
		}
		if (!ok)
			return FALSE;
		s += 2;
	}
	/* (the attitude: a character's aggressive or timid flavour; this port
	has no such flag on its characters - the normal lines) */
	if (p->attitude != 0)
		return FALSE;
	*specificity = s;
	return TRUE;
}

/* the pattern's conditions on the speaker (flags) */
static boolean pattern_fits_speaker(struct pattern const *p, long speaker, long subject, long cause)
{
	long actor_index = unit_actor(speaker);
	long player = player_unit();

	if (TEST_FLAG(p->flags, _pattern_subject_visible_bit) && speaker != subject && !actor_sees(actor_index, subject))
		return FALSE;
	if (TEST_FLAG(p->flags, _pattern_cause_visible_bit) && speaker != cause && !actor_sees(actor_index, cause))
		return FALSE;
	if (TEST_FLAG(p->flags, _pattern_subject_is_target_bit) && (actor_index == NONE || actor_target_unit_index(actor_index) != subject))
		return FALSE;
	if (TEST_FLAG(p->flags, _pattern_cause_is_target_bit) && (actor_index == NONE || actor_target_unit_index(actor_index) != cause))
		return FALSE;
	if (TEST_FLAG(p->flags, _pattern_cause_player_or_speaker_ally_bit))
	{
		boolean cause_player = cause != NONE && unit_is_player(cause);
		boolean speaker_ally = player != NONE && relation(speaker, player) == _hostility_friend;

		if (!cause_player && !speaker_ally)
			return FALSE;
	}
	if (TEST_FLAG(p->flags, _pattern_cause_primary_player_ally_bit) && (player == NONE || relation(speaker, player) != _hostility_friend))
		return FALSE;
	if (TEST_FLAG(p->flags, _pattern_speaker_searching_bit) && (actor_index == NONE || !actor_get(actor_index)->state.searching))
		return FALSE;
	if (TEST_FLAG(p->flags, _pattern_speaker_following_player_bit) && (actor_index == NONE || !h2_ai_actor_follows_player(actor_index)))
		return FALSE;
	if (TEST_FLAG(p->flags, _pattern_friends_present_bit) && (actor_index == NONE || actor_get(actor_index)->situation.visible_friends < 1))
		return FALSE;
	return TRUE;
}

/* the units a speaker type names, around the subject */
static long speaker_candidates(short speaker_type, long subject, long cause, long *out, long maximum)
{
	long count = 0;
	long subject_actor = unit_actor(subject);
	struct data_iterator iterator;
	struct actor_datum *actor;

	switch (speaker_type)
	{
	case _speaker_subject:
		if (subject != NONE)
			out[count++] = subject;
		return count;
	case _speaker_cause:
		if (cause != NONE)
			out[count++] = cause;
		return count;
	case _speaker_squad:
		/* (the squad given the order: the subject among them) */
		if (subject != NONE && unit_actor(subject) != NONE)
			out[count++] = subject;
		break;
	default:
		break;
	}
	if (subject == NONE)
		return 0;
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL && count < maximum)
	{
		long unit = actor->meta.unit_index;
		boolean same_squad;

		if (unit == NONE || unit == subject || !unit_alive(unit) || actor->meta.dormant)
			continue;
		same_squad = subject_actor != NONE && actor_get(subject_actor)->meta.encounter_index == actor->meta.encounter_index &&
			actor_get(subject_actor)->meta.squad_index == actor->meta.squad_index;
		switch (speaker_type)
		{
		case _speaker_friend:
			if (relation(unit, subject) == _hostility_friend && units_distance(unit, subject) < HEARING_DISTANCE &&
				(same_squad || actor_knows(iterator.datum_index, subject) || actor_sees(iterator.datum_index, cause)))
			{
				out[count++] = unit;
			}
			break;
		case _speaker_enemy:
			if (relation(unit, subject) == _hostility_enemy && actor_sees(iterator.datum_index, subject))
				out[count++] = unit;
			break;
		case _speaker_squad:
		case _speaker_peer:
		case _speaker_joint:
		case _speaker_clump:
			if (same_squad)
				out[count++] = unit;
			break;
		case _speaker_leader:
		case _speaker_joint_leader:
			if (same_squad && h2_ai_actor_is_leader(iterator.datum_index))
				out[count++] = unit;
			break;
		case _speaker_vehicle:
			{
				struct object_datum const *a = object_get(unit), *b = object_get(subject);

				if (a->object.parent_object_index != NONE && a->object.parent_object_index == b->object.parent_object_index)
					out[count++] = unit;
			}
			break;
		default:
			break;
		}
	}
	return count;
}

static char const *type_name(short type)
{
	static char text[16];

	switch (type)
	{
	case _h2_dialogue_death: return "death";
	case _h2_dialogue_damage: return "damage";
	case _h2_dialogue_sighted_new: return "sighted_new";
	case _h2_dialogue_sighted_first: return "sighted_first";
	case _h2_dialogue_sighted_old: return "sighted_old";
	case _h2_dialogue_heard_new: return "heard_new";
	case _h2_dialogue_found_unit: return "found_unit";
	case _h2_dialogue_throwing_grenade: return "throwing_grenade";
	case _h2_dialogue_surprised: return "surprised";
	case _h2_dialogue_investigate_start: return "investigate_start";
	case _h2_dialogue_taunt: return "taunt";
	case _h2_dialogue_retreat: return "retreat";
	case _h2_dialogue_retreat_from_dead_leader: return "retreat_from_dead_leader";
	case _h2_dialogue_cowering: return "cowering";
	case _h2_dialogue_curse: return "curse";
	case _h2_dialogue_threaten: return "threaten";
	case _h2_dialogue_strike: return "strike";
	case _h2_dialogue_gloat: return "gloat";
	case _h2_dialogue_greet: return "greet";
	case _h2_dialogue_charging: return "charging";
	case _h2_dialogue_dive: return "dive";
	case _h2_dialogue_evade: return "evade";
	default:
		snprintf(text, sizeof(text), "type %d", (int)type);
		return text;
	}
}

void h2_dialogue_event(short type, long subject, long cause, short damage_category)
{
	struct option options[MAXIMUM_OPTIONS];
	long option_count = 0, i, k;
	short start, length, best = -1;
	real total = 0.0f, pick;
	long chosen = NONE;

	if (!dlg.loaded || type < 0 || type >= NUMBER_OF_H2_DIALOGUE_TYPES || suppressed())
		return;
	dlg.events++;
	start = dlg.type_start[type];
	length = dlg.type_length[type];
	if (start == NONE || length <= 0)
		return;
	for (i = start; i < start + length && i < dlg.pattern_count; i++)
	{
		struct pattern const *p = &dlg.patterns[i];
		long candidates[32], n;
		short specificity;

		if (p->type != type || !pattern_fits(p, subject, cause, damage_category, &specificity))
			continue;
		/* (a lament for the dead player is the player's: the pattern names no
		subject kind, the H2EK's fights never said it for a dead AI) */
		if (type == _h2_dialogue_death && p->vocalization >= 0 && !strcmp(dlg.vocalizations[p->vocalization].name, "lmnt_deadplr") &&
			!unit_is_player(subject))
		{
			continue;
		}
		n = speaker_candidates(p->speaker, subject, cause, candidates, 32);
		for (k = 0; k < n && option_count < MAXIMUM_OPTIONS; k++)
		{
			short line;
			struct option *o;

			if (!pattern_fits_speaker(p, candidates[k], subject, cause) || !speaker_may(candidates[k], p->vocalization, &line, p->conditions))
				continue;
			o = &options[option_count++];
			o->unit_index = candidates[k];
			o->vocalization = line;
			o->pattern = (short)i;
			/* (a line of the voice's own over a parent's) */
			o->specificity = (short)(specificity * 2 + (line == p->vocalization ? 1 : 0));
			o->weight = dlg.vocalizations[line].weight > 0.0f ? dlg.vocalizations[line].weight : 1.0f;
			o->listener_unit_index = p->listener == 1 ? cause : subject;
			o->for_player = o->listener_unit_index != NONE && unit_is_player(o->listener_unit_index);
			if (o->specificity > best)
				best = o->specificity;
		}
	}
	if (!option_count)
	{
		if (logging())
			platform_log("h2 dialogue: %ld: %s (subject %lx, cause %lx): no one to speak", game_time_get(), type_name(type),
				(unsigned long)subject, (unsigned long)cause);
		return;
	}
	for (i = 0; i < option_count; i++)
	{
		if (options[i].specificity == best)
			total += options[i].weight;
	}
	pick = random_unit() * total;
	for (i = 0; i < option_count; i++)
	{
		if (options[i].specificity != best)
			continue;
		pick -= options[i].weight;
		chosen = i;
		if (pick <= 0.0f)
			break;
	}
	if (chosen == NONE)
		return;
	if (skipped(&dlg.vocalizations[options[chosen].vocalization], options[chosen].for_player))
	{
		dlg.skipped++;
		if (logging())
			platform_log("h2 dialogue: %ld: %s: %s skipped by its fraction", game_time_get(), type_name(type),
				dlg.vocalizations[options[chosen].vocalization].name);
		return;
	}
	if (team_queue_busy(options[chosen].unit_index, options[chosen].vocalization))
		return;
	speak(options[chosen].unit_index, options[chosen].vocalization, options[chosen].pattern, options[chosen].listener_unit_index, FALSE);
}

/* ---------- this engine's events, as Halo 2's */

/* the kind of damage, as Halo 2's patterns name them (this engine's damage
categories are Halo 2's: none, falling, bullet, grenade, explosive, sniper,
melee, flame, mounted weapon, vehicle...) */
static short h2_damage_type(short damage_category)
{
	return damage_category >= 0 && damage_category < 16 ? damage_category : 0;
}

boolean h2_dialogue_communication(short communication_type, long subject, long cause, short hostility, short damage_category)
{
	long actor_index;

	if (!dlg.loaded)
		return FALSE;
	/* (a unit with no Halo 2 voice - a player, a turret - as this engine has
	it; a subject that is a Halo 2 character: Halo 2's rules) */
	if (h2_dialogue_unit_voice(subject) == NONE && !unit_is_player(subject))
		return FALSE;
	actor_index = unit_actor(subject);
	(void)hostility;
	{
		struct unit_state *state = unit_state(subject);
		long now = game_time_get();

		if (state)
		{
			state_first_seen(state, subject);
			switch (communication_type)
			{
			case _ai_communication_sighted_enemy:
			case _ai_communication_found_enemy:
			case _ai_communication_unexpected_enemy:
			case _ai_communication_alert_noncombat:
				/* (a unit just placed among its enemies does not call them out:
				the H2EK's placed squads say srprs, never seefoe; nor does one
				whose order forces its combat status - "Combat" on every order of
				05a's landing zone, which is why the real fights there call out
				no foe: its status never rose because it saw one) */
				if (now - state->created < 3 * TICKS_PER_SECOND)
					return TRUE;
				if (actor_index != NONE && h2_ai_actor_forced_combat_status(actor_index) >= 4)
					return TRUE;
				break;
			case _ai_communication_flee_idle:
			case _ai_communication_hiding_finished:
				if (state->cower_time && now - state->cower_time < 3 * TICKS_PER_SECOND)
					return TRUE;
				state->cower_time = now;
				break;
			case _ai_communication_hurt:
			case _ai_communication_damage:
				/* (a grunt of pain just made: no whine on top of it) */
				if (state->pain_time && now - state->pain_time < TICKS_PER_SECOND)
				{
					if (cause != NONE && cause != subject && unit_actor(cause) != NONE && relation(cause, subject) == _hostility_enemy &&
						random_unit() < dlg.strike_chance)
					{
						h2_dialogue_event(_h2_dialogue_strike, cause, subject, h2_damage_type(damage_category));
					}
					return TRUE;
				}
				break;
			default:
				break;
			}
		}
	}
	switch (communication_type)
	{
	case _ai_communication_death:
		/* (the dead one's own line is its involuntary death: unit_dialogue.c;
		the event's lines - the killer's cheer, the praise, the scold, the
		lament - are rare in the H2EK's fights: one chr_kllfoe_blt in three,
		no lament - the lament's conditions keep out a speaker with a dangerous
		foe) */
		if (unit_is_player(subject) || (cause != NONE && unit_is_player(cause)) || random_unit() < dlg.death_chance)
			h2_dialogue_event(_h2_dialogue_death, subject, cause, h2_damage_type(damage_category));
		break;
	case _ai_communication_hurt:
	case _ai_communication_damage:
		/* (Halo 2's damage event is a friend's fire - the whine, the scold of the
		player -: the H2EK's fights, Marines shot throughout, whined 0 times) */
		if (communication_type == _ai_communication_damage || (cause != NONE && relation(cause, subject) == _hostility_friend))
			h2_dialogue_event(_h2_dialogue_damage, subject, cause, h2_damage_type(damage_category));
		/* (and the one who struck: Halo 2's strike, its taunts and curses in answer) */
		if (cause != NONE && cause != subject && unit_actor(cause) != NONE && relation(cause, subject) == _hostility_enemy &&
			random_unit() < dlg.strike_chance)
		{
			h2_dialogue_event(_h2_dialogue_strike, cause, subject, h2_damage_type(damage_category));
		}
		break;
	case _ai_communication_sighted_enemy:
		/* (the first foe seen this combat: sighted_first - seefoe, any status;
		another while fighting: sighted_new - morefoe, which the patterns allow
		only to one with no foe in view: the H2EK's placed fights say neither) */
		{
			struct unit_state *state = unit_state(subject);

			if (actor_index != NONE && state && !state->first_sighting_done && !actor_get(actor_index)->state.had_visible_enemy)
			{
				state->first_sighting_done = TRUE;
				h2_dialogue_event(_h2_dialogue_sighted_first, subject, cause, 0);
			}
			else
				h2_dialogue_event(_h2_dialogue_sighted_new, subject, cause, 0);
		}
		break;
	case _ai_communication_found_enemy:
		/* (this engine finds a known enemy again and again; Halo 2's found_unit
		is a search's end) */
		if (actor_index != NONE && actor_get(actor_index)->state.searching)
			h2_dialogue_event(_h2_dialogue_found_unit_pursuit, subject, cause, 0);
		break;
	case _ai_communication_unexpected_enemy:
		h2_dialogue_event(_h2_dialogue_sighted_first, subject, cause, 0);
		break;
	case _ai_communication_alert_noncombat:
		h2_dialogue_event(_h2_dialogue_heard_new, subject, cause, 0);
		break;
	case _ai_communication_grenade_throwing:
		h2_dialogue_event(_h2_dialogue_throwing_grenade, subject, actor_index != NONE ? actor_target_unit_index(actor_index) : NONE, 0);
		break;
	case _ai_communication_grenade_sighted:
	case _ai_communication_grenade_startle:
	case _ai_communication_grenade_danger:
		/* (Halo 2 warns of a grenade as a sighting of one: no unit is the
		grenade here; its line is the plain warning) */
		h2_dialogue_event(_h2_dialogue_sighted_new, subject, NONE, 0);
		break;
	case _ai_communication_lost_contact:
		h2_dialogue_event(_h2_dialogue_investigate_failed, subject, cause, 0);
		break;
	case _ai_communication_search_start:
		h2_dialogue_event(_h2_dialogue_investigate_start, subject, cause, 0);
		break;
	case _ai_communication_search_query:
		h2_dialogue_event(_h2_dialogue_presearch, subject, cause, 0);
		break;
	case _ai_communication_search_report:
		h2_dialogue_event(_h2_dialogue_pursuit_failed, subject, cause, 0);
		break;
	case _ai_communication_search_abandon:
		h2_dialogue_event(_h2_dialogue_abandoned_search_time, subject, cause, 0);
		break;
	case _ai_communication_search_group_abandon:
		h2_dialogue_event(_h2_dialogue_abandoned_search_space, subject, cause, 0);
		break;
	case _ai_communication_uncover_start:
		h2_dialogue_event(_h2_dialogue_found_unit_self_preserving, subject, cause, 0);
		break;
	case _ai_communication_sighted_friend_player:
		h2_dialogue_event(_h2_dialogue_greet, subject, cause, 0);
		break;
	case _ai_communication_flee:
		/* (this engine's flee as such has no Halo 2 event: the H2EK's fleeing
		Grunts yelp (the involuntary flee, below) and say no fear_rtrt; Halo
		2's retreat_from_* events are its retreat behaviour's own causes) */
		break;
	case _ai_communication_flee_leader_died:
		h2_dialogue_event(_h2_dialogue_retreat_from_dead_leader, subject, cause, 0);
		break;
	case _ai_communication_flee_idle:
	case _ai_communication_hiding_finished:
		h2_dialogue_event(_h2_dialogue_cowering, subject, cause, 0);
		break;
	case _ai_communication_surprise:
		{
			struct unit_state *state = unit_state(subject);
			long now = game_time_get();

			if (state && state->surprise_time && now - state->surprise_time < 30 * TICKS_PER_SECOND)
				break;
			if (state)
				state->surprise_time = now;
			h2_dialogue_event(_h2_dialogue_surprised, subject, cause, 0);
			h2_dialogue_involuntary(subject, _h2_involuntary_surprise);
		}
		break;
	case _ai_communication_berserk:
		if (!vocalize_named(subject, "brsrk"))
			vocalize_named(subject, "charge");
		break;
	case _ai_communication_melee:
		/* (the H2EK's fights: melee 2 - the roar as the blow lands) */
		vocalize_named(subject, "melee");
		break;
	case _ai_communication_leap:
		vocalize_named(subject, "meleeleap");
		break;
	case _ai_communication_dive:
		/* (Halo 2's dive impulse - this engine's dive on a Halo 2 level,
		actions.c - raises its "dive" event (the patterns' dive -> dive); its
		evasion impulse the "evade" event (-> dodge), raised by actions.c's
		evade itself) */
		h2_dialogue_event(_h2_dialogue_dive, subject, cause, 0);
		break;
	case _ai_communication_vehicle_entry:
		h2_dialogue_event(_h2_dialogue_vehicle_enter, subject, cause, 0);
		break;
	case _ai_communication_vehicle_woohoo:
		h2_dialogue_event(_h2_dialogue_vehicle_woohoo, subject, cause, 0);
		break;
	case _ai_communication_vehicle_scared:
		h2_dialogue_event(_h2_dialogue_vehicle_crazy, subject, cause, 0);
		break;
	case _ai_communication_postcombat_check_enemy:
	case _ai_communication_postcombat_shoot_corpse:
		h2_dialogue_event(_h2_dialogue_shoot_corpse, subject, cause, 0);
		break;
	case _ai_communication_postcombat_check_friend:
		h2_dialogue_event(_h2_dialogue_postcombat_inspect_body, subject, cause, 0);
		break;
	case _ai_communication_postcombat_triumph:
	case _ai_communication_postcombat_celebrate:
	case _ai_communication_postcombat_massacre:
		h2_dialogue_event(_h2_dialogue_postcombat_win, subject, cause, 0);
		break;
	case _ai_communication_postcombat_wounded:
	case _ai_communication_postcombat_alone:
		h2_dialogue_event(_h2_dialogue_postcombat_lose, subject, cause, 0);
		break;
	case _ai_communication_postcombat_unscathed:
		h2_dialogue_event(_h2_dialogue_postcombat_neutral, subject, cause, 0);
		break;
	case _ai_communication_falling_to_death:
		h2_dialogue_involuntary(subject, _h2_involuntary_fall);
		break;
	default:
		/* (the shooting chatter, killing sprees, advances, covers, blocked:
		Halo 2 has no lines for them as events) */
		break;
	}
	return TRUE;
}

void h2_dialogue_squad_order(long unit_index, short ending_dialogue_type)
{
	if (!dlg.loaded || ending_dialogue_type < 1 || ending_dialogue_type > 11)
		return;
	h2_dialogue_event((short)(_h2_dialogue_new_order_advance + ending_dialogue_type - 1), unit_index, NONE, 0);
}

/* a vocalization the behaviours speak by name (melee, charge, dive, dodge,
brsrk, roar: no pattern names them) */
static boolean vocalize_named(long unit_index, char const *name)
{
	short vocalization = vocalization_by_name(name), line;

	if (vocalization == NONE || !speaker_may(unit_index, vocalization, &line, 0))
		return FALSE;
	if (skipped(&dlg.vocalizations[line], FALSE))
	{
		dlg.skipped++;
		return FALSE;
	}
	if (team_queue_busy(unit_index, line))
		return FALSE;
	speak(unit_index, line, NONE, NONE, FALSE);
	return TRUE;
}

/* ---------- involuntary vocalizations */

boolean h2_dialogue_involuntary(long unit_index, short involuntary_type)
{
	short vocalization, line, voice;
	struct unit_state *state;

	if (!dlg.loaded || involuntary_type < 0 || involuntary_type >= NUMBER_OF_H2_INVOLUNTARY_TYPES || suppressed())
		return FALSE;
	vocalization = dlg.involuntary[involuntary_type];
	if (vocalization == NONE || (voice = h2_dialogue_unit_voice(unit_index)) == NONE || !(state = unit_state(unit_index)))
		return FALSE;
	if (!speaker_may(unit_index, vocalization, &line, 0))
		return FALSE;
	if (skipped(&dlg.vocalizations[line], FALSE))
	{
		dlg.skipped++;
		return FALSE;
	}
	speak(unit_index, line, NONE, NONE, FALSE);
	return TRUE;
}

boolean h2_dialogue_behaviour_voice(long unit_index, char const *vocalization_name)
{
	short vocalization, type;

	if (!dlg.loaded || !vocalization_name || (vocalization = vocalization_by_name(vocalization_name)) == NONE)
		return FALSE;
	for (type = 0; type < NUMBER_OF_H2_INVOLUNTARY_TYPES; type++)
	{
		if (dlg.involuntary[type] == vocalization)
			return h2_dialogue_involuntary(unit_index, type);
	}
	if (suppressed())
		return FALSE;
	return vocalize_named(unit_index, vocalization_name);
}

boolean h2_dialogue_damage(long unit_index, boolean died, boolean died_instantly, short damage_category, long responsible_unit_index, boolean *spoke)
{
	struct unit_state *state;
	struct unit_datum *unit;
	short type = NONE;
	boolean took_body_damage, took_major_body_damage;

	*spoke = FALSE;
	if (!dlg.loaded || h2_dialogue_unit_voice(unit_index) == NONE || !(state = unit_state(unit_index)))
		return FALSE;
	unit = unit_get(unit_index);
	took_body_damage = unit->object.recent_body_damage > 0.0f;
	took_major_body_damage = unit->object.recent_body_damage >= 0.6f;
	(void)responsible_unit_index;
	if (died)
	{
		long actor_index = unit->unit.actor_index;
		boolean severe = FALSE;

		if (actor_index != NONE)
			severe = actor_get(actor_index)->state.combat_status >= _actor_combat_status_definite;
		/* (the H2EK's fights: dth for every death but a burning one's dth_slw
		and a headshot's dth_hdsht - 28 of 30; no dth_slnt, no dth_mjr) */
		(void)severe;
		(void)died_instantly;
		if (damage_category == _damage_category_falling)
			type = _h2_involuntary_death_falling;
		else if (damage_category == _damage_category_flame)
			type = _h2_involuntary_death_slow;
		else
			type = _h2_involuntary_death;
		/* (the death's line: not passed over, not refused for what the unit
		was saying) */
		{
			short vocalization = dlg.involuntary[type], line, voice = h2_dialogue_unit_voice(unit_index);

			if (vocalization != NONE && (line = voice_line(voice, vocalization)) != NONE)
			{
				dlg.dying_unit_index = unit_index;
				speak(unit_index, line, NONE, NONE, TRUE);
				dlg.dying_unit_index = NONE;
				*spoke = TRUE;
			}
		}
		return TRUE;
	}
	if (state->damage_major_timer == 0)
	{
		if (damage_category == _damage_category_falling)
			type = _h2_involuntary_pain_falling;
		else if (took_major_body_damage)
			type = _h2_involuntary_pain_major;
		else if (state->damage_minor_timer == 0 && state->damage_minor_sounds < 3 && random_unit() < dlg.minor_damage_chance)
		{
			/* (Halo 2's minor damage chance, unit-minor-damage-chance: a minor hurt
			grunts by chance - HALO_H2_DIALOGUE_MINOR_CHANCE, measured against the
			H2EK's fights: NOTES.md) */
			type = took_body_damage ? (unit->object.recent_body_damage >= 0.45f ? _h2_involuntary_pain_medium : _h2_involuntary_pain) :
				_h2_involuntary_pain_shield;
		}
	}
	if (type != NONE && h2_dialogue_involuntary(unit_index, type))
	{
		*spoke = TRUE;
		state->pain_time = game_time_get();
		if (took_major_body_damage)
			state->damage_major_timer = 60;
		else
		{
			state->damage_minor_sounds++;
			state->damage_minor_timer = 30;
			state->damage_minor_decay_timer = 22;
		}
	}
	return TRUE;
}

/* ---------- the speech's sound and responses */

long h2_dialogue_speech_play(long unit_index, short vocalization_type, real *seconds)
{
	short vocalization = (short)(vocalization_type - H2_DIALOGUE_VOCALIZATION_BASE), voice;
	struct voice *v;
	long sound;

	if (seconds)
		*seconds = 0.0f;
	if (!dlg.loaded || vocalization < 0 || vocalization >= dlg.vocalization_count || (voice = h2_dialogue_unit_voice(unit_index)) == NONE)
		return NONE;
	v = &dlg.voices[voice];
	if (v->sound[vocalization] == NONE)
		return NONE;
	/* (perf/cairo-tick, HALO_TICK_PROFILE=3) the line's parts timed (tick_detail.c) */
	extern unsigned long long halo_tick_detail_begin(void);
extern int halo_tick_detail_state; /* (perf/ce-floor) 0: no detail timing - no call */
	extern void halo_tick_detail_end(const char *name, unsigned long long started);
	unsigned long long detail_at = (halo_tick_detail_state ? halo_tick_detail_begin() : 0ULL);

	sound = h2_sound_voice_play(v->tag, v->sound[vocalization], unit_index, &v->last_permutation[vocalization],
		v->last_time[vocalization], (long)(dlg.vocalizations[vocalization].sound_repetition_delay * 60.0f * TICKS_PER_SECOND), seconds);
	(halo_tick_detail_state ? halo_tick_detail_end("dlg:voice_play", detail_at) : (void)0);
	v->last_time[vocalization] = game_time_get();
	/* (its gesture: the vocalization's animation impulse - a cheer, a taunt,
	a point... -, played by the speaker as the line starts, h2_ai.c) */
	detail_at = (halo_tick_detail_state ? halo_tick_detail_begin() : 0ULL);
	if (dlg.vocalizations[vocalization].animation_impulse > 0)
		h2_ai_vocalization_animation(unit_index, dlg.vocalizations[vocalization].animation_impulse);
	(halo_tick_detail_state ? halo_tick_detail_end("dlg:gesture", detail_at) : (void)0);
	/* (its lips: the face animation of the line's permutation, h2_cinematics.c) */
	if (sound != NONE)
	{
		extern boolean h2_cinematics_face_start(long object_index, char const *sound_name);
		struct h2_cache *cache = h2_scripts_cache();
		char const *name = cache ? h2_cache_tag_name(cache, v->sound[vocalization]) : NULL;

		if (name && name[0])
		{
			char line[160];

			snprintf(line, sizeof(line), "%s#%d", !strncmp(name, "sound\\", 6) ? name + 6 : name, (int)v->last_permutation[vocalization]);
			detail_at = (halo_tick_detail_state ? halo_tick_detail_begin() : 0ULL);
			h2_cinematics_face_start(unit_index, line);
			(halo_tick_detail_state ? halo_tick_detail_end("dlg:face", detail_at) : (void)0);
		}
	}
	return sound;
}

char const *h2_dialogue_vocalization_name(short vocalization_type)
{
	short vocalization = (short)(vocalization_type - H2_DIALOGUE_VOCALIZATION_BASE);

	if (!dlg.loaded || vocalization < 0 || vocalization >= dlg.vocalization_count)
		return "?";
	return dlg.vocalizations[vocalization].name;
}

/* the listeners' answers to a line: a response vocalization spoken by one
who heard it (the one spoken to first, else a friend or an enemy of the
speaker near it with the line), or a dialogue event raised on them */
void h2_dialogue_speech_notified(long unit_index, short vocalization_type)
{
	short vocalization = (short)(vocalization_type - H2_DIALOGUE_VOCALIZATION_BASE);
	struct vocalization const *v;
	struct unit_state *state = unit_state(unit_index);
	long listener;
	short i;

	if (!dlg.loaded || vocalization < 0 || vocalization >= dlg.vocalization_count || !state)
		return;
	v = &dlg.vocalizations[vocalization];
	listener = state->listener_unit_index;
	for (i = 0; i < v->response_count; i++)
	{
		struct response const *r = &dlg.responses[v->first_response + i];

		if (r->kind == _response_event)
		{
			if (r->dialogue_type >= 0 && r->dialogue_type < NUMBER_OF_H2_DIALOGUE_TYPES && listener != NONE && unit_alive(listener))
				h2_dialogue_event(r->dialogue_type, listener, unit_index, 0);
			continue;
		}
		if (r->kind != _response_vocalization || r->vocalization < 0 || r->vocalization >= dlg.vocalization_count)
			continue;
		{
			long candidates[32], n = 0, k, chosen = NONE;
			short line;
			struct data_iterator iterator;
			struct actor_datum *actor;

			if (listener != NONE && listener != unit_index && unit_actor(listener) != NONE)
				candidates[n++] = listener;
			data_iterator_new(&iterator, actor_data);
			while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL && n < 32)
			{
				long unit = actor->meta.unit_index;

				if (unit == NONE || unit == unit_index || unit == listener || !unit_alive(unit) || actor->meta.dormant)
					continue;
				if (units_distance(unit, unit_index) > HEARING_DISTANCE)
					continue;
				if (relation(unit, unit_index) == _hostility_enemy && !actor_sees(iterator.datum_index, unit_index))
					continue;
				candidates[n++] = unit;
			}
			for (k = 0; k < n; k++)
			{
				if (speaker_may(candidates[k], r->vocalization, &line, 0))
				{
					chosen = k;
					break;
				}
			}
			if (chosen == NONE)
			{
				if (logging())
					platform_log("h2 dialogue: %ld: no one answers %s with %s (%ld heard)", game_time_get(), v->name,
						dlg.vocalizations[r->vocalization].name, n);
				continue;
			}
			if (skipped(&dlg.vocalizations[line], unit_is_player(unit_index)))
			{
				dlg.skipped++;
				continue;
			}
			speak(candidates[chosen], line, NONE, unit_index, FALSE);
			/* (one answer a line) */
			break;
		}
	}
}

/* the player's look: an ally the player's aim rests on (within 6 wu, 7
degrees) for a second is looked at - Halo 2's player_look event, its
look_* lines; for eight seconds, player_look_longtime */
static void player_look_update(long now)
{
	long player = player_unit();
	struct unit_datum *unit;
	real_point3d eye;
	real_vector3d aim;
	struct data_iterator iterator;
	struct actor_datum *actor;

	if (player == NONE || !unit_alive(player))
		return;
	unit = unit_get(player);
	aim = unit->unit.aiming_vector;
	unit_get_camera_position(player, &eye);
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long other = actor->meta.unit_index;
		struct unit_state *state = other != NONE ? unit_state(other) : NULL;
		real_point3d head;
		real_vector3d to;
		real distance, along;
		boolean looked = FALSE;

		if (!state || !unit_alive(other) || actor->meta.dormant || relation(player, other) != _hostility_friend)
			continue;
		unit_get_head_position(other, (union real_point3d *)&head);
		to.i = head.x - eye.x;
		to.j = head.y - eye.y;
		to.k = head.z - eye.z;
		distance = (real)sqrt(to.i * to.i + to.j * to.j + to.k * to.k);
		if (distance > 0.1f && distance < 6.0f)
		{
			along = (to.i * aim.i + to.j * aim.j + to.k * aim.k) / distance;
			/* (cos 7 degrees) */
			looked = along > 0.9925f;
		}
		if (!looked)
		{
			state->looked_at_since = 0;
			state->long_look_done = FALSE;
			continue;
		}
		if (!state->looked_at_since)
			state->looked_at_since = now;
		else if (now - state->looked_at_since == TICKS_PER_SECOND)
			h2_dialogue_event(_h2_dialogue_player_look, other, player, 0);
		else if (now - state->looked_at_since >= 8 * TICKS_PER_SECOND && !state->long_look_done)
		{
			state->long_look_done = TRUE;
			h2_dialogue_event(_h2_dialogue_player_look_longtime, other, player, 0);
		}
	}
}

/* ---------- the tick */

void h2_dialogue_update(void)
{
	long now = game_time_get();
	short i;
	struct data_iterator iterator;
	struct actor_datum *actor;

	if (!dlg.loaded)
		return;
	if (now % 3 == 0 && !suppressed())
		player_look_update(now);
	/* the lines waiting their pre delay */
	for (i = 0; i < MAXIMUM_PENDING; i++)
	{
		struct pending *p = &dlg.pending[i];

		if (!p->used || p->when > now)
			continue;
		p->used = FALSE;
		if (unit_alive(p->unit_index))
			speak(p->unit_index, p->vocalization, p->pattern, p->listener_unit_index, TRUE);
	}
	/* the units' pain timers */
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long unit = actor->meta.unit_index;
		struct unit_state *state = unit != NONE ? unit_state(unit) : NULL;

		if (!state || !unit_alive(unit))
			continue;
		state_first_seen(state, unit);
		if (!state->first_sighting_done && (actor->situation.known_enemies > 0 || actor->state.had_visible_enemy) &&
			now - state->created < 3 * TICKS_PER_SECOND)
		{
			state->first_sighting_done = TRUE;
		}
		if (actor->control.moving)
			state->still_since = 0;
		else if (!state->still_since)
			state->still_since = now;
		if (state->damage_minor_decay_timer > 0 && --state->damage_minor_decay_timer == 0 && state->damage_minor_sounds > 0)
		{
			state->damage_minor_sounds--;
			state->damage_minor_decay_timer = 22;
		}
		if (state->damage_minor_timer > 0)
			state->damage_minor_timer--;
		if (state->damage_major_timer > 0)
			state->damage_major_timer--;
		/* a fleeing one yelps as it runs (its flee vocalization's repeat delay
		and skip fraction space them). Not one in a Halo 2 retreat (our flee with the leader-dead or
		peer-dead panic, 8 / 3: h2_ai.c): Halo 2's retreat runs to cover and cowers - the cower behaviour
		voices it (action_guard.c) -; the real engine's 05a Grunts retreating from their shot Elites said
		0-1 flee a watch, ours 6-11 */
		if (actor->state.action == _actor_action_flee && !actor->meta.dormant && state->voice != -2 &&
			actor->state.action_data.flee.panic_type != 8 && actor->state.action_data.flee.panic_type != 3)
		{
			if (state->flee_time == 0 || now - state->flee_time >= TICKS_PER_SECOND)
			{
				state->flee_time = now;
				h2_dialogue_involuntary(unit, state->still_since && now - state->still_since >= 2 * TICKS_PER_SECOND ? _h2_involuntary_cower : _h2_involuntary_flee);
			}
		}
		else
			state->flee_time = 0;
	}
}
