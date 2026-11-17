/*
STUBBS_HORDE.C

Stubbs mode's brains and horde (Stubbs mode only: stubbs_mode.c; nothing here
runs outside it). Written from how the retail game plays and from its tags
(Sigmmma's reclaimer, GPL-3.0, names the Halo parts of them):

- Brains. The action key in front of a human in reach, who is stunned, not
  fighting, or facing away, eats him: Stubbs swings, the human dies in his
  grip, Stubbs's health fills, and the body is marked to rise. The scripts'
  (unit_finishing_move <unit>) does the same for a unit.
- Rising. A human killed by Stubbs or by a zombie rises a few seconds later
  as the zombie of his kind: his actor variant names it (the tag reference
  Halo's actor variant keeps unused, right after the major variant; every
  human of the retail maps has one: a cop rises as a zombie cop). The zombie
  is on the zombie team, allied with the players, and joins the squad the
  scripts named with (ai_set_resurrection_group <ai>), else the map's own
  zombie encounter, else no encounter at all.
- The horde. A zombie with nothing to fight walks after the nearest player
  and keeps near him; the actors' own AI does the fighting.

Nothing of this is in the game state: a saved game loaded mid-eat forgets
the eat and the bodies waiting to rise (the zombies already up are objects
and actors, which the state keeps).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "math/real_math.h"
#include "memory/data.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "scenario/scenario_definitions.h"
#include "scenario/scenario.h"
#include "objects/objects.h"
#include "objects/damage.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "units/bipeds.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/players.h"
#include "ai/ai_globals.h"
#include "ai/ai_scenario_definitions.h"
#include "ai/actors.h"
#include "ai/actor_definitions.h"
#include "ai/encounters.h"
#include "ai/ai_script.h"
#include "stubbs_mode.h"
#include "stubbs_horde.h"
#include "stubbs_engine.h"
#include "stubbs_functions.h"

#include <string.h>

boolean tag_index_is_group(long tag_index, long group_tag);

#ifndef HALO_CONSOLE_NO_STUBBS /* (the header's empty ones on a console) */

enum
{
	/* the retail game's teams (its globals' enum): default, player, civilian,
	militia, robot, zombie, security, possessed, scientist, sheep */
	STUBBS_TEAM_PLAYER = 1,
	STUBBS_TEAM_CIVILIAN = 2,
	STUBBS_TEAM_MILITIA = 3,
	STUBBS_TEAM_ROBOT = 4,
	STUBBS_TEAM_ZOMBIE = 5,
	STUBBS_TEAM_SECURITY = 6,
	STUBBS_TEAM_POSSESSED = 7,
	STUBBS_TEAM_SCIENTIST = 8,
	STUBBS_TEAM_SHEEP = 9,

	/* an eat: the swing, the bite that kills and heals, the rest of the
	animation before the player acts again */
	EAT_TICKS = 45,
	EAT_BITE_TICK = 18,
	/* a body rises this long after death */
	RISE_TICKS = 4 * TICKS_PER_SECOND,
	/* a zombie too far from its player walks to him, checked this often */
	FOLLOW_CHECK_TICKS = 10,

	MAXIMUM_EATS = 16,
	MAXIMUM_BODIES = 64,
	MAXIMUM_ZOMBIES = 128,
};

/* reach of the bite past the two bounding spheres, and how far a zombie
may fall behind (world units) */
#define EAT_REACH 0.45f
#define FOLLOW_DISTANCE 3.0f
#define FOLLOW_STOP_DISTANCE 1.8f
/* how much of his health a brain gives Stubbs (1 fills it) */
#define BRAIN_HEALTH 1.0f

/* what a victim's brains are worth: the retail game keeps it in the
victim's collision model, in bytes Halo's layout leaves unused (after the
shield recharge fields, which stubbs_mode.c's rewrite keeps as they are):
which special attack the body feeds (1 to 4, 0 none) and how many, the
health it gives (points: a civilian's 30 of Stubbs's hundred) and a
credit toward the attacks that regenerate (a civilian 2, a cop 1:
stubbs_engine_special_attack_credit) */
enum
{
	STUBBS_COLLISION_BRAINS_ATTACK_OFFSET = 0x1C4,
	STUBBS_COLLISION_BRAINS_COUNT_OFFSET = 0x1C6,
	STUBBS_COLLISION_BRAINS_HEALTH_OFFSET = 0x1C8,
	STUBBS_COLLISION_BRAINS_CREDIT_OFFSET = 0x1CC,
	STUBBS_COLLISION_BRAINS_SIZE = 0x1D0,
};

struct horde_brains
{
	short attack; /* (0-based, NONE for none) */
	short count;
	real health; /* (points) */
	real credit;
};

static boolean horde_victim_brains(
	long victim_index,
	struct horde_brains *brains)
{
	struct unit_datum *victim = unit_try_and_get(victim_index);
	struct object_definition *definition;
	unsigned char const *collision;
	short attack, count;

	brains->attack = NONE;
	brains->count = 0;
	brains->health = 0.f;
	brains->credit = 0.f;
	if (!victim)
		return FALSE;
	definition = object_definition_get(victim->definition_index);
	if (definition->object.collision_model.index == NONE)
		return FALSE;
	collision = (unsigned char const *)tag_get('coll', definition->object.collision_model.index);
	if (!collision)
		return FALSE;
	memcpy(&attack, collision + STUBBS_COLLISION_BRAINS_ATTACK_OFFSET, sizeof(attack));
	memcpy(&count, collision + STUBBS_COLLISION_BRAINS_COUNT_OFFSET, sizeof(count));
	memcpy(&brains->health, collision + STUBBS_COLLISION_BRAINS_HEALTH_OFFSET, sizeof(brains->health));
	memcpy(&brains->credit, collision + STUBBS_COLLISION_BRAINS_CREDIT_OFFSET, sizeof(brains->credit));
	if (attack >= 1 && attack <= STUBBS_MAXIMUM_SPECIAL_ATTACKS && count > 0)
	{
		brains->attack = (short)(attack - 1);
		brains->count = count;
	}
	/* (values that aren't these fields) */
	if (!(brains->health >= 0.f && brains->health <= 1000.f))
		brains->health = 0.f;
	if (!(brains->credit >= 0.f && brains->credit <= 1000.f))
		brains->credit = 0.f;
	return TRUE;
}

struct horde_eat
{
	long player_index; /* NONE when free */
	long eater_unit_index;
	long victim_unit_index;
	long started_tick;
	boolean bitten;
};

struct horde_body
{
	long unit_index; /* NONE when free */
	long zombie_variant_index; /* the actor variant it rises as */
	long rise_tick;
};

static struct horde_eat eats[MAXIMUM_EATS];
static struct horde_body bodies[MAXIMUM_BODIES];
static long zombies[MAXIMUM_ZOMBIES];
static long resurrection_ai_reference = NONE;
static boolean horde_ready;
static struct scenario *horde_scenario;
static long horde_last_tick;
static boolean logged_no_zombie_encounter;

/* ---------- housekeeping */

static void horde_reset(
	void)
{
	long index;

	for (index = 0; index < MAXIMUM_EATS; index++)
		eats[index].player_index = NONE;
	for (index = 0; index < MAXIMUM_BODIES; index++)
		bodies[index].unit_index = NONE;
	for (index = 0; index < MAXIMUM_ZOMBIES; index++)
		zombies[index] = NONE;
	resurrection_ai_reference = NONE;
	logged_no_zombie_encounter = FALSE;
	horde_ready = TRUE;
}

/* a new map, or a game state put back, forgets the eats and the bodies */
static boolean horde_begin(
	void)
{
	struct scenario *scenario;
	long tick;

	if (!stubbs_mode_active())
		return FALSE;
	scenario = global_scenario_try_and_get();
	if (!scenario || !ai_globals || !ai_globals->ai_initialized_for_map)
		return FALSE;
	tick = game_time_get();
	if (!horde_ready || scenario != horde_scenario || tick < horde_last_tick)
	{
		horde_reset();
		horde_scenario = scenario;
	}
	horde_last_tick = tick;
	return TRUE;
}

static struct biped_datum *horde_living_biped(
	long unit_index)
{
	struct biped_datum *biped = unit_index == NONE ? NULL : biped_try_and_get(unit_index);

	if (biped && TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
		return NULL;
	return biped;
}

static boolean horde_team_is_human(
	short team_index)
{
	return team_index == STUBBS_TEAM_CIVILIAN || team_index == STUBBS_TEAM_MILITIA ||
		team_index == STUBBS_TEAM_SECURITY || team_index == STUBBS_TEAM_SCIENTIST;
}

/* the zombie an actor variant's human rises as: the tag reference the
retail game keeps where Halo's actor variant has unused bytes */
static long horde_zombie_variant_of_variant(
	long actor_variant_index)
{
	struct actor_variant_definition const *variant;
	struct tag_reference const *zombie;

	if (actor_variant_index == NONE)
		return NONE;
	variant = actor_variant_definition_get(actor_variant_index);
	if (!variant)
		return NONE;
	zombie = (struct tag_reference const *)variant->unused;
	if (zombie->group_tag != ACTOR_VARIANT_DEFINITION_TAG || zombie->index == NONE ||
		!actor_variant_definition_get(zombie->index))
	{
		return NONE;
	}
	return zombie->index;
}

/* the zombie a unit rises as: by its actor's variant, else by the first
actor variant of the map's palette that is of its kind */
long stubbs_horde_palette_variant_for_unit(
	long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct scenario *scenario = global_scenario_try_and_get();
	long index;

	long same_team = NONE;
	short team;

	if (!unit || !scenario)
		return NONE;
	team = unit_definition_get(unit->definition_index)->unit.default_team;
	for (index = 0; index < scenario->ai_actor_palette.count; index++)
	{
		struct tag_reference *entry = TAG_BLOCK_GET_ELEMENT(
			&scenario->ai_actor_palette, index, struct tag_reference);
		struct actor_variant_definition const *variant;

		if (!entry || entry->index == NONE)
			continue;
		variant = actor_variant_definition_get(entry->index);
		if (!variant || variant->unit_reference.index == NONE)
			continue;
		if (variant->unit_reference.index == unit->definition_index)
			return entry->index;
		/* (none of its own kind: one of its team, a zombie's for a zombie) */
		if (same_team == NONE && tag_index_is_group(variant->unit_reference.index, UNIT_DEFINITION_TAG) &&
			unit_definition_get(variant->unit_reference.index)->unit.default_team == team)
		{
			same_team = entry->index;
		}
	}
	return same_team;
}

static long horde_zombie_variant_of_unit(
	long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct scenario *scenario = global_scenario_try_and_get();
	long zombie = NONE;
	long index;

	if (!unit)
		return NONE;
	if (unit->unit.actor_index != NONE)
	{
		struct actor_datum *actor = actor_try_and_get(unit->unit.actor_index);

		if (actor)
			zombie = horde_zombie_variant_of_variant(actor->meta.variant_definition_index);
	}
	for (index = 0; zombie == NONE && scenario && index < scenario->ai_actor_palette.count; index++)
	{
		struct tag_reference *entry = TAG_BLOCK_GET_ELEMENT(
			&scenario->ai_actor_palette, index, struct tag_reference);
		struct actor_variant_definition const *variant;

		if (!entry || entry->index == NONE)
			continue;
		variant = actor_variant_definition_get(entry->index);
		if (variant && variant->unit_reference.index == unit->definition_index)
			zombie = horde_zombie_variant_of_variant(entry->index);
	}
	return zombie;
}

static boolean horde_unit_is_player(
	long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);

	return unit && unit->unit.player_index != NONE;
}

boolean stubbs_horde_unit_is_zombie(
	long unit_index)
{
	long index;

	if (unit_index == NONE)
		return FALSE;
	for (index = 0; index < MAXIMUM_ZOMBIES; index++)
	{
		if (zombies[index] == unit_index)
			return TRUE;
	}
	return FALSE;
}

/* Stubbs's side: a player's unit, or a zombie */
static boolean horde_unit_is_ours(
	long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);

	if (!unit)
		return FALSE;
	return unit->unit.player_index != NONE ||
		unit->object.owner_team_index == STUBBS_TEAM_ZOMBIE ||
		stubbs_horde_unit_is_zombie(unit_index);
}

/* ---------- bodies */

static void horde_body_add(
	long unit_index,
	long zombie_variant_index)
{
	long index, free_index = NONE;

	for (index = 0; index < MAXIMUM_BODIES; index++)
	{
		if (bodies[index].unit_index == unit_index)
			return;
		if (bodies[index].unit_index == NONE && free_index == NONE)
			free_index = index;
	}
	if (free_index == NONE)
	{
		stubbs_mode_log("horde: no room for another body to rise");
		return;
	}
	bodies[free_index].unit_index = unit_index;
	bodies[free_index].zombie_variant_index = zombie_variant_index;
	bodies[free_index].rise_tick = game_time_get() + RISE_TICKS;
}

/* the encounter and squad a new zombie joins: the scripts' resurrection
group, else none (a map's own zombie encounter would send him to its
firing positions instead of after the player) */
static void horde_choose_squad(
	long *encounter_index,
	short *squad_index)
{
	struct scenario *scenario = global_scenario_try_and_get();

	*encounter_index = NONE;
	*squad_index = 0;
	if (!scenario)
		return;
	if (resurrection_ai_reference != NONE)
	{
		long encounter = resurrection_ai_reference & UNSIGNED_SHORT_MAX;

		if (VALID_INDEX(encounter, scenario->ai_encounters.count))
		{
			*encounter_index = encounter;
			/* (a squad reference: type 2 in the top two bits, ai_script.c) */
			if (((unsigned long)resurrection_ai_reference >> 30) == 2)
				*squad_index = (short)((resurrection_ai_reference >> 16) & UNSIGNED_CHAR_MAX);
			return;
		}
	}
	if (!logged_no_zombie_encounter)
	{
		logged_no_zombie_encounter = TRUE;
		stubbs_mode_log("horde: no resurrection group set; risen zombies follow the player on their own");
	}
}

static void horde_zombie_add(
	long unit_index)
{
	long index;

	for (index = 0; index < MAXIMUM_ZOMBIES; index++)
	{
		if (zombies[index] == NONE || !horde_living_biped(zombies[index]))
		{
			zombies[index] = unit_index;
			return;
		}
	}
}

/* a body rises: the zombie of its kind stands where it lay, facing as it
did, an actor on the zombie team */
static void horde_body_rise(
	struct horde_body *body)
{
	struct object_datum *corpse = object_try_and_get(body->unit_index);
	struct actor_variant_definition const *variant;
	struct object_placement_data placement;
	real_point3d position;
	real_vector3d forward;
	long encounter_index, zombie_index, actor_index;
	short squad_index;

	if (!corpse || body->zombie_variant_index == NONE)
		return;
	variant = actor_variant_definition_get(body->zombie_variant_index);
	if (!variant || variant->unit_reference.index == NONE || variant->actor_reference.index == NONE)
		return;
	position = corpse->object.position;
	forward = corpse->object.forward;
	forward.k = 0.f;
	if (normalize3d(&forward) < 0.01f)
		vector3d_from_angle(&forward, 0.f);

	/* (the human's mind, kept braindead through the eat so his squad
	counted him dead: gone before his body is, else an actor without a
	unit looks about and halts) */
	{
		struct unit_datum *human = unit_try_and_get(body->unit_index);

		if (human && human->unit.actor_index != NONE)
		{
			actor_delete(human->unit.actor_index, FALSE);
			human->unit.actor_index = NONE;
		}
	}
	object_delete(body->unit_index);

	object_placement_data_new(&placement, variant->unit_reference.index, NONE);
	placement.position = position;
	placement.forward = forward;
	placement.owner_team_index = STUBBS_TEAM_ZOMBIE;
	zombie_index = object_new(&placement);
	if (zombie_index == NONE)
	{
		stubbs_mode_log("horde: no object for a zombie to rise as");
		return;
	}
	unit_get(zombie_index)->object.owner_team_index = STUBBS_TEAM_ZOMBIE;

	horde_choose_squad(&encounter_index, &squad_index);
	actor_index = actor_create_for_unit(
		FALSE,
		zombie_index,
		body->zombie_variant_index,
		encounter_index,
		squad_index,
		FALSE,
		NONE,
		FALSE,
		2, /* (actor_default_state_alert, actors.c) */
		0, /* (actor_default_state_none) */
		NONE,
		0);
	if (actor_index == NONE)
	{
		stubbs_mode_log("horde: no actor for a risen zombie; it stands there");
	}
	else
	{
		/* (an encounter's team is the map's; ours is the zombies') */
		unit_get(zombie_index)->object.owner_team_index = STUBBS_TEAM_ZOMBIE;
		actor_get(actor_index)->meta.team_index = STUBBS_TEAM_ZOMBIE;
	}
	/* the zombies and the players are one side */
	game_allegiance_create(STUBBS_TEAM_PLAYER, FALSE, STUBBS_TEAM_ZOMBIE, FALSE, NONE, NONE, FALSE);
	horde_zombie_add(zombie_index);
	stubbs_mode_log("horde: unit 0x%04lX rose as 0x%04lX (%s), actor 0x%04lX, encounter %ld squad %d",
		body->unit_index & UNSIGNED_SHORT_MAX, zombie_index & UNSIGNED_SHORT_MAX,
		tag_name_strip_path(tag_get_name(body->zombie_variant_index)),
		actor_index & UNSIGNED_SHORT_MAX, encounter_index, squad_index);
}

static void horde_bodies_update(
	long tick)
{
	long index;

	for (index = 0; index < MAXIMUM_BODIES; index++)
	{
		struct horde_body *body = &bodies[index];

		if (body->unit_index == NONE)
			continue;
		if (!object_try_and_get(body->unit_index))
		{
			stubbs_mode_log("horde: the body of unit 0x%04lX is gone before it could rise", body->unit_index & UNSIGNED_SHORT_MAX);
			body->unit_index = NONE;
			continue;
		}
		if (tick >= body->rise_tick)
		{
			horde_body_rise(body);
			body->unit_index = NONE;
		}
	}
}

/* ---------- brains */

/* whether a unit may eat another's brains now: a living human in reach and
in front, who is stunned, not fighting, or looking away */
static boolean horde_can_eat(
	long eater_index,
	long victim_index)
{
	struct biped_datum *eater = horde_living_biped(eater_index);
	struct biped_datum *victim = horde_living_biped(victim_index);
	real_vector3d delta;
	real distance, reach;

	if (!eater || !victim || eater_index == victim_index)
		return FALSE;
	/* (the hand takes heads, it doesn't eat: stubbs_possession.c) */
	if (stubbs_engine_unit_is_thrown(eater_index))
		return FALSE;
	if (!horde_team_is_human(victim->object.owner_team_index) || victim->unit.player_index != NONE)
		return FALSE;
	if (victim->object.parent_object_index != NONE || eater->object.parent_object_index != NONE)
		return FALSE;
	/* (the scripts' objects_cannot_be_finished: stubbs_functions.c) */
	if (stubbs_functions_object_cannot(victim_index, _stubbs_object_cannot_be_finished_bit))
		return FALSE;
	vector_from_points3d(&eater->object.bounding_sphere_center, &victim->object.bounding_sphere_center, &delta);
	distance = normalize3d(&delta);
	reach = eater->object.bounding_sphere_radius + victim->object.bounding_sphere_radius + EAT_REACH;
	if (distance > reach)
		return FALSE;
	/* (a player eats where he looks: his aim, which his body follows only
	once it has turned far enough; a zombie where it faces) */
	{
		real_vector3d facing = eater->object.forward;

		if (eater->unit.player_index != NONE)
		{
			facing = eater->unit.aiming_vector;
			facing.k = 0.f;
			if (normalize3d(&facing) == 0.f)
				facing = eater->object.forward;
		}
		if (dot_product3d(&facing, &delta) < 0.5f)
			return FALSE;
	}
	if (victim->unit.body_stun_ticks > 0)
		return TRUE;
	if (dot_product3d(&victim->object.forward, &delta) > 0.3f)
		return TRUE; /* (looking away from the eater) */
	if (victim->unit.actor_index == NONE || !actor_in_combat(victim->unit.actor_index))
		return TRUE;
	return FALSE;
}

/* the human in reach to eat (NONE) */
static long horde_find_victim(
	long eater_index)
{
	struct unit_datum *eater = unit_try_and_get(eater_index);
	long object_indices[16];
	long best = NONE;
	real best_distance = 0.f;
	short count, number;

	if (!eater)
		return NONE;
	count = objects_in_sphere(
		0,
		_object_mask_biped,
		&eater->object.location,
		&eater->object.bounding_sphere_center,
		eater->object.bounding_sphere_radius + 1.5f,
		object_indices,
		NUMBEROF(object_indices));
	for (number = 0; number < count; number++)
	{
		struct object_datum *object;
		real distance;

		if (!horde_can_eat(eater_index, object_indices[number]))
			continue;
		object = object_get(object_indices[number]);
		distance = distance_squared3d(&eater->object.bounding_sphere_center, &object->object.bounding_sphere_center);
		if (best == NONE || distance < best_distance)
		{
			best = object_indices[number];
			best_distance = distance;
		}
	}
	return best;
}

static struct horde_eat *horde_eat_of_unit(
	long unit_index)
{
	long index;

	for (index = 0; index < MAXIMUM_EATS; index++)
	{
		if (eats[index].player_index != NONE && eats[index].eater_unit_index == unit_index)
			return &eats[index];
	}
	return NULL;
}

/* an eat begins: the eater swings; the victim, grabbed, stops thinking */
static boolean horde_eat_start(
	long player_index,
	long eater_index,
	long victim_index)
{
	struct horde_eat *eat = NULL;
	struct unit_datum *eater = unit_try_and_get(eater_index);
	struct unit_datum *victim = unit_try_and_get(victim_index);
	long index;

	if (!eater || !victim)
		return FALSE;
	for (index = 0; index < MAXIMUM_EATS && !eat; index++)
	{
		if (eats[index].player_index == NONE)
			eat = &eats[index];
	}
	if (!eat)
		return FALSE;
	eat->player_index = player_index == NONE ? eater_index : player_index;
	eat->eater_unit_index = eater_index;
	eat->victim_unit_index = victim_index;
	eat->started_tick = game_time_get();
	eat->bitten = FALSE;
	/* (Halo's melee is its "use equipment" control) */
	SET_FLAG(eater->unit.control_flags, _unit_control_use_equipment_bit, TRUE);
	/* (the victim's mind goes blank while he is eaten; his actor stays
	until the bite kills him, so the encounter counts him as one of its
	dead: a10's stealth-kill tutorial waits on ai_living_fraction, which a
	deleted actor leaves at 1) */
	if (victim->unit.actor_index != NONE)
		actor_braindead(victim->unit.actor_index, TRUE);
	victim->unit.desired_facing_vector = eater->object.forward;
	scale_vector3d(&victim->unit.desired_facing_vector, -1.f, &victim->unit.desired_facing_vector);
	stubbs_mode_log("horde: unit 0x%04lX eats the brains of unit 0x%04lX",
		eater_index & UNSIGNED_SHORT_MAX, victim_index & UNSIGNED_SHORT_MAX);
	return TRUE;
}

static void horde_eats_update(
	long tick)
{
	long index;

	for (index = 0; index < MAXIMUM_EATS; index++)
	{
		struct horde_eat *eat = &eats[index];
		struct unit_datum *eater, *victim;

		if (eat->player_index == NONE)
			continue;
		eater = unit_try_and_get(eat->eater_unit_index);
		victim = unit_try_and_get(eat->victim_unit_index);
		if (!eater || !victim || TEST_FLAG(eater->object.damage_flags, _object_dead_bit))
		{
			eat->player_index = NONE;
			continue;
		}
		if (!eat->bitten && tick >= eat->started_tick + EAT_BITE_TICK)
		{
			long zombie = horde_zombie_variant_of_unit(eat->victim_unit_index);

			eat->bitten = TRUE;
			if (!TEST_FLAG(victim->object.damage_flags, _object_dead_bit))
			{
				SET_FLAG(victim->object.damage_flags, _object_cannot_take_damage_bit, FALSE);
				unit_kill(eat->victim_unit_index);
			}
			/* (the reward is the victim's: his health and his special attack
			count, as his collision model says; one with no such value fills
			Stubbs up. Only a player's own body is fed, as in the retail game) */
			{
				struct horde_brains brains;
				boolean known = horde_victim_brains(eat->victim_unit_index, &brains);
				long origin = eater->unit.player_index != NONE ?
					stubbs_engine_player_origin_unit(eater->unit.player_index) : NONE;

				real maximum = object_get_maximum_body_vitality(eat->eater_unit_index, FALSE);

				eater->object.body_vitality += known && brains.health > 0.f && maximum > 0.f ?
					brains.health / maximum : BRAIN_HEALTH;
				if (eater->object.body_vitality > 1.f)
					eater->object.body_vitality = 1.f;
				if (origin == eat->eater_unit_index)
				{
					if (brains.attack != NONE)
					{
						stubbs_engine_special_attack_set_count(origin, brains.attack,
							(short)(stubbs_engine_special_attack_get_count(origin, brains.attack) + brains.count));
					}
					stubbs_engine_special_attack_credit(origin, brains.credit);
				}
				stubbs_mode_log("horde: the brains of unit 0x%04lX: %.0f health of %.0f, attack %d x%d, credit %.1f",
					eat->victim_unit_index & UNSIGNED_SHORT_MAX, brains.health, maximum, (int)brains.attack,
					(int)brains.count, brains.credit);
			}
			if (zombie == NONE)
				stubbs_mode_log("horde: unit 0x%04lX has no zombie to rise as", eat->victim_unit_index & UNSIGNED_SHORT_MAX);
			horde_body_add(eat->victim_unit_index, zombie);
		}
		if (tick >= eat->started_tick + EAT_TICKS)
			eat->player_index = NONE;
	}
}

/* ---------- the horde following */

static long horde_nearest_player_unit(
	real_point3d const *position,
	real *distance_squared)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long best = NONE;

	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		struct unit_datum *unit;
		real distance;

		if (player->unit_index == NONE || !horde_living_biped(player->unit_index))
			continue;
		unit = unit_get(player->unit_index);
		distance = distance_squared3d(position, &unit->object.position);
		if (best == NONE || distance < *distance_squared)
		{
			best = player->unit_index;
			*distance_squared = distance;
		}
	}
	return best;
}

static void horde_zombies_update(
	long tick)
{
	long index;

	for (index = 0; index < MAXIMUM_ZOMBIES; index++)
	{
		struct biped_datum *zombie;
		struct actor_datum *actor;
		long player_unit_index;
		real distance_squared = 0.f;

		if (zombies[index] == NONE)
			continue;
		zombie = horde_living_biped(zombies[index]);
		if (!zombie)
		{
			zombies[index] = NONE;
			continue;
		}
		if ((tick + index) % FOLLOW_CHECK_TICKS != 0)
			continue;
		if (zombie->unit.actor_index == NONE || zombie->object.parent_object_index != NONE)
			continue;
		actor = actor_try_and_get(zombie->unit.actor_index);
		if (!actor)
			continue;
		switch (actor->state.action)
		{
		case _actor_action_fight:
		case _actor_action_charge:
		case _actor_action_obey:
		case _actor_action_vehicle:
		case _actor_action_converse:
			continue;
		}
		if (actor_target_unit_index(zombie->unit.actor_index) != NONE)
			continue;
		player_unit_index = horde_nearest_player_unit(&zombie->object.position, &distance_squared);
		if (player_unit_index == NONE)
			continue;
		if (distance_squared > FOLLOW_DISTANCE * FOLLOW_DISTANCE)
		{
			struct unit_datum *player_unit = unit_get(player_unit_index);

			actor_move_to_point(zombie->unit.actor_index, &player_unit->object.position, NONE, player_unit_index);
		}
		else if (distance_squared < FOLLOW_STOP_DISTANCE * FOLLOW_STOP_DISTANCE &&
			actor->control.path.destination_orders.destination_type == _destination_raw_location)
		{
			actor_move_halt(zombie->unit.actor_index);
		}
	}
}

/* ---------- the hooks */

void stubbs_horde_update(
	void)
{
	long tick;

	if (!horde_begin())
		return;
	tick = game_time_get();
	horde_eats_update(tick);
	horde_bodies_update(tick);
	horde_zombies_update(tick);
}

boolean stubbs_horde_player_action(
	long player_index)
{
	struct player_datum *player;
	long victim;

	if (!horde_begin())
		return FALSE;
	player = player_get(player_index);
	if (player->unit_index == NONE)
		return FALSE;
	if (horde_eat_of_unit(player->unit_index))
		return TRUE;
	victim = horde_find_victim(player->unit_index);
	if (victim == NONE)
		return FALSE;
	return horde_eat_start(player_index, player->unit_index, victim);
}

void stubbs_horde_unit_damaged_to_death(
	long unit_index,
	struct damage_data const *damage_data)
{
	struct unit_datum *unit;
	long zombie;

	if (!horde_begin() || !damage_data)
		return;
	unit = unit_try_and_get(unit_index);
	if (!unit || !horde_team_is_human(unit->object.owner_team_index) || unit->unit.player_index != NONE)
		return;
	if (horde_eat_of_unit(unit_index))
		return;
	if (damage_data->owner_team_index != STUBBS_TEAM_ZOMBIE &&
		damage_data->owner_player_index == NONE &&
		!horde_unit_is_ours(damage_data->owner_object_index))
	{
		return;
	}
	zombie = horde_zombie_variant_of_unit(unit_index);
	if (zombie == NONE)
		return;
	horde_body_add(unit_index, zombie);
}

boolean stubbs_horde_unit_finishing_move(
	long unit_index)
{
	struct unit_datum *unit;
	long victim;

	if (!horde_begin())
		return FALSE;
	unit = unit_try_and_get(unit_index);
	if (!unit || horde_eat_of_unit(unit_index))
		return FALSE;
	victim = horde_find_victim(unit_index);
	if (victim == NONE)
		return FALSE;
	return horde_eat_start(unit->unit.player_index, unit_index, victim);
}

void stubbs_horde_set_resurrection_group(
	long ai_reference)
{
	if (!horde_begin())
		return;
	resurrection_ai_reference = ai_reference;
}

#endif
