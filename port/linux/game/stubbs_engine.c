/*
STUBBS_ENGINE.C

Stubbs the Zombie's engine systems, written for this engine from what the
retail game does (stubbs_engine.h). Stubbs mode only.

The special attacks: Stubbs's unit definition lists them (0x170 bytes
each, its block at 0x33C in his unit layout): flatulence, the gut grenade,
the hand and the head. Each has a cost (how many of its count one use
takes), a ready and a throw effect, a speed (world units a second) and the
object it throws. Halo's unit layout has no room for them, so each unit
definition's are noted here before stubbs_mode.c rewrites it.

The hand: black throws it. It leaves Stubbs's shoulder along his aim,
pitched up ten degrees, at the attack's speed, and the player drives it,
a small biped, while Stubbs's body (the player's origin) stands where it
was. Black again recalls it: the hand goes and the player is back in the
body. A hand that dies sends the player back the same way; the player
doesn't die.

A body the hand rides (possession, its own file: stubbs_engine_hand_rides)
is the player's for as long as it lives; black, or its death, brings the
hand home and the player back to Stubbs.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "real_math.h"
#include "objects.h"
#include "model_definitions.h"
#include "units.h"
#include "biped_definitions.h"
#include "players.h"
#include "effects.h"
#include "tag_files.h"
#include "stubbs_mode.h"
#include "stubbs_engine.h"

#ifndef HALO_CONSOLE_NO_STUBBS

/* ---------- constants */

enum
{
	/* unit_animation_start_action's actions whose slots hold Stubbs's own
	animations (stubbs_mode.c): disarm = the hand thrown, put away = the
	hand regrown */
	STUBBS_UNIT_ACTION_HAND_THROW = 1,
	STUBBS_UNIT_ACTION_HAND_BACK = 4,

	/* Stubbs's unit layout: the special attacks block */
	STUBBS_UNIT_SPECIAL_ATTACKS_OFFSET = 0x33C,
	STUBBS_SPECIAL_ATTACK_SIZE = 0x170,

	/* a special attack's fields */
	STUBBS_SPECIAL_ATTACK_COST_OFFSET = 0x06,
	STUBBS_SPECIAL_ATTACK_MAXIMUM_COUNT_OFFSET = 0x04,
	STUBBS_SPECIAL_ATTACK_INITIAL_COUNT_OFFSET = 0x08,
	STUBBS_SPECIAL_ATTACK_REGENERATION_PERIOD_OFFSET = 0x10,
	STUBBS_SPECIAL_ATTACK_READY_EFFECT_OFFSET = 0x4C,
	STUBBS_SPECIAL_ATTACK_THROW_EFFECT_OFFSET = 0x5C,
	STUBBS_SPECIAL_ATTACK_SPEED_OFFSET = 0xA8,
	STUBBS_SPECIAL_ATTACK_OBJECT_OFFSET = 0xAC,

	/* where a biped's flags are: Stubbs's layout (before stubbs_mode.c's
	rewrite), Halo's (after) */
	STUBBS_BIPED_FLAGS_OFFSET = 0x464 + 0x04,
	HALO_BIPED_FLAGS_OFFSET = 0x2F0 + 0x04,
	/* units a map may have */
	MAXIMUM_STUBBS_UNITS_NOTED = 64,
	/* units with special attacks a map may have (Stubbs, his zombies) */
	MAXIMUM_STUBBS_UNIT_DEFINITIONS = 32,
	/* units whose counts differ from the full ones */
	MAXIMUM_STUBBS_UNIT_COUNTS = 16,
	MAXIMUM_STUBBS_LOCAL_PLAYERS = 4,
};

/* how far the hand reaches for a head (world units) */
#define STUBBS_POSSESSION_REACH 1.6f
/* the hand's throw pitched up (radians: ten degrees) */
#define STUBBS_THROW_PITCH 0.17453292f

/* ---------- structures */


struct stubbs_unit_definition
{
	long definition_index;
	short special_attack_count;
	struct stubbs_special_attack special_attacks[STUBBS_MAXIMUM_SPECIAL_ATTACKS];
};

struct stubbs_unit_count
{
	long unit_index;
	char counts[STUBBS_MAXIMUM_SPECIAL_ATTACKS];
	/* brains credit toward the next one, in seconds */
	real credit[STUBBS_MAXIMUM_SPECIAL_ATTACKS];
};

struct stubbs_local_player
{
	long origin_unit_index;
	long hand_unit_index;
	long possessed_unit_index;
	boolean hand_button_down;
};

/* ---------- globals */

static struct stubbs_unit_definition stubbs_unit_definitions[MAXIMUM_STUBBS_UNIT_DEFINITIONS];
static short stubbs_unit_definition_count;
static boolean stubbs_unit_definitions_noted;
static struct stubbs_unit_count stubbs_unit_counts[MAXIMUM_STUBBS_UNIT_COUNTS];
static struct stubbs_local_player stubbs_local_players[MAXIMUM_STUBBS_LOCAL_PLAYERS];
/* every biped definition noted at the map's load (already rewritten in
Halo's layout), and the thrown bipeds a player will drive */
static struct
{
	long definition_index;
	unsigned char *definition;
} stubbs_bipeds_noted[MAXIMUM_STUBBS_UNITS_NOTED];
static short stubbs_biped_noted_count;
static long stubbs_driven_bipeds[MAXIMUM_STUBBS_UNITS_NOTED];
static short stubbs_driven_biped_count;

/* ---------- private code */

static void stubbs_engine_forget_players(
	void)
{
	short index;

	for (index = 0; index < MAXIMUM_STUBBS_LOCAL_PLAYERS; index++)
	{
		stubbs_local_players[index].origin_unit_index = NONE;
		stubbs_local_players[index].hand_unit_index = NONE;
		stubbs_local_players[index].possessed_unit_index = NONE;
	}
	for (index = 0; index < MAXIMUM_STUBBS_UNIT_COUNTS; index++)
		stubbs_unit_counts[index].unit_index = NONE;
}

static long stubbs_read_long(
	unsigned char const *address)
{
	long value;

	csmemcpy(&value, address, 4);
	return value;
}

static struct stubbs_unit_definition const *stubbs_unit_definition_get(
	long definition_index)
{
	short index;

	for (index = 0; index < stubbs_unit_definition_count; index++)
	{
		if (stubbs_unit_definitions[index].definition_index == definition_index)
			return &stubbs_unit_definitions[index];
	}
	return NULL;
}

static struct stubbs_special_attack const *stubbs_unit_special_attack_get(
	long unit_index,
	short special_attack)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct stubbs_unit_definition const *definition;

	if (!unit)
		return NULL;
	definition = stubbs_unit_definition_get(unit->definition_index);
	if (!definition || special_attack < 0 || special_attack >= definition->special_attack_count)
		return NULL;
	return &definition->special_attacks[special_attack];
}

/* the count a unit starts with for one of its special attacks, from its
definition (0 for one it hasn't) */
static short stubbs_unit_initial_count(
	long unit_index,
	short special_attack)
{
	struct stubbs_special_attack const *attack = stubbs_unit_special_attack_get(unit_index, special_attack);

	return attack ? attack->initial_count : 0;
}

static struct stubbs_unit_count *stubbs_unit_count_get(
	long unit_index,
	boolean add)
{
	short index;
	struct stubbs_unit_count *free_slot = NULL;

	for (index = 0; index < MAXIMUM_STUBBS_UNIT_COUNTS; index++)
	{
		struct stubbs_unit_count *count = &stubbs_unit_counts[index];

		if (count->unit_index == unit_index)
			return count;
		if (count->unit_index != NONE && !object_try_and_get(count->unit_index))
			count->unit_index = NONE;
		if (count->unit_index == NONE && !free_slot)
			free_slot = count;
	}
	if (add && free_slot)
	{
		free_slot->unit_index = unit_index;
		/* (a new unit's counts are its definition's starting ones) */
		for (index = 0; index < STUBBS_MAXIMUM_SPECIAL_ATTACKS; index++)
		{
			free_slot->counts[index] = (char)stubbs_unit_initial_count(unit_index, index);
			free_slot->credit[index] = 0.f;
		}
		return free_slot;
	}
	return NULL;
}

static boolean stubbs_unit_alive(
	long unit_index)
{
	struct unit_datum *unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;

	return unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit);
}

static long stubbs_effect_new_on_unit(
	long effect_index,
	long unit_index)
{
	if (effect_index == NONE || !object_try_and_get(unit_index))
		return NONE;
	return effect_new_from_object(effect_index, unit_index, unit_index, NONE, 1.f, 1.f, NULL, NULL);
}

static short stubbs_local_player_from_unit(
	long unit_index)
{
	short index;

	for (index = 0; index < MAXIMUM_STUBBS_LOCAL_PLAYERS; index++)
	{
		struct stubbs_local_player *local = &stubbs_local_players[index];

		if (local->origin_unit_index == unit_index || local->hand_unit_index == unit_index ||
			local->possessed_unit_index == unit_index)
		{
			return index;
		}
	}
	return NONE;
}

/* the hand gone (recalled, killed) or come home: the player back in the
body, the hand deleted (when it's still there) */
/* Stubbs's left arm with or without its hand: his model's armleft region
has a "~damaged" permutation, the stump, shown while the hand is away */
static void stubbs_hand_on_arm(
	long unit_index,
	boolean on_arm)
{
	struct object_datum *object = object_try_and_get(unit_index);
	struct object_definition *definition;
	struct model *model;
	short region_index;

	if (!object)
		return;
	definition = object_definition_get(object->definition_index);
	if (definition->object.model.index == NONE)
		return;
	model = model_definition_get(definition->object.model.index);
	for (region_index = 0; region_index < model->regions.count; region_index++)
	{
		struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);

		if (!_stricmp(region->name, "armleft"))
			object_permute_region(unit_index, "~damaged", region_index, !on_arm);
	}
}

static void stubbs_hand_return(
	short local_player_index,
	boolean delete_hand)
{
	struct stubbs_local_player *local = &stubbs_local_players[local_player_index];
	long hand_unit_index = local->hand_unit_index;

	local->hand_unit_index = NONE;
	local->possessed_unit_index = NONE;
	if (stubbs_unit_alive(local->origin_unit_index))
	{
		struct stubbs_special_attack const *attack = stubbs_unit_special_attack_get(local->origin_unit_index,
			_stubbs_special_attack_hand);

		players_set_local_player_unit(local_player_index, local->origin_unit_index);
		player_control_set_facing(local_player_index, &object_get(local->origin_unit_index)->object.forward);
		stubbs_hand_on_arm(local->origin_unit_index, TRUE);
		/* (the hand regrown: his regrow-2 animation, in Halo's put-away slot) */
		unit_animation_start_action(local->origin_unit_index, STUBBS_UNIT_ACTION_HAND_BACK);
		/* (the hand back on his arm can be thrown again) */
		if (attack && stubbs_engine_special_attack_get_count(local->origin_unit_index,
			_stubbs_special_attack_hand) < attack->cost)
		{
			stubbs_engine_special_attack_set_count(local->origin_unit_index, _stubbs_special_attack_hand,
				attack->cost);
		}
	}
	if (delete_hand && hand_unit_index != NONE && object_try_and_get(hand_unit_index))
	{
		if (object_get(hand_unit_index)->object.parent_object_index != NONE)
			object_detach(hand_unit_index);
		object_delete(hand_unit_index);
	}
	stubbs_mode_log("the hand is back on Stubbs");
}

/* black with Stubbs's body: the hand thrown (special attack 2) */
static boolean stubbs_hand_throw(
	short local_player_index,
	long unit_index)
{
	struct stubbs_local_player *local = &stubbs_local_players[local_player_index];
	struct stubbs_special_attack const *attack = stubbs_unit_special_attack_get(unit_index,
		_stubbs_special_attack_hand);
	struct unit_datum *unit = unit_get(unit_index);
	struct object_placement_data placement;

	real_vector3d aim;
	real_vector3d forward;
	real_point3d head;
	real speed_per_tick;
	long hand_unit_index;

	if (!attack || attack->object_group != 'bipd' || attack->object_definition_index == NONE)
	{
		stubbs_mode_log("no hand for unit tag %08lX", (unsigned long)unit->definition_index);
		return FALSE;
	}
	if (unit->object.parent_object_index != NONE || !stubbs_unit_alive(unit_index))
		return FALSE;
	if (stubbs_engine_special_attack_get_count(unit_index, _stubbs_special_attack_hand) < attack->cost)
		return FALSE;

	unit_get_aiming_vector(unit_index, &aim);
	unit_get_camera_position(unit_index, &head);
	forward = aim;
	forward.k = 0.f;
	if (normalize3d(&forward) == 0.f)
		forward = unit->object.forward;
	/* pitched up: the aim turned toward the sky by the throw's angle */
	{
		real horizontal = (real)sqrt(aim.i * aim.i + aim.j * aim.j);
		real pitch = (real)atan2(aim.k, horizontal) + STUBBS_THROW_PITCH;

		aim.i = forward.i * (real)cos(pitch);
		aim.j = forward.j * (real)cos(pitch);
		aim.k = (real)sin(pitch);
	}

	object_placement_data_new(&placement, attack->object_definition_index, unit_index);
	placement.position.x = head.x + forward.i * 0.4f;
	placement.position.y = head.y + forward.j * 0.4f;
	placement.position.z = head.z - 0.15f;
	placement.forward = forward;
	placement.up.i = 0.f;
	placement.up.j = 0.f;
	placement.up.k = 1.f;
	speed_per_tick = attack->speed / (real)TICKS_PER_SECOND;
	placement.translational_velocity.i = unit->object.translational_velocity.i + aim.i * speed_per_tick;
	placement.translational_velocity.j = unit->object.translational_velocity.j + aim.j * speed_per_tick;
	placement.translational_velocity.k = unit->object.translational_velocity.k + aim.k * speed_per_tick;
	hand_unit_index = object_new(&placement);
	if (hand_unit_index == NONE)
	{
		stubbs_mode_log("the hand could not be made");
		return FALSE;
	}

	stubbs_engine_special_attack_set_count(unit_index, _stubbs_special_attack_hand,
		(short)(stubbs_engine_special_attack_get_count(unit_index, _stubbs_special_attack_hand) - attack->cost));
	stubbs_effect_new_on_unit(attack->ready_effect_index, unit_index);
	stubbs_effect_new_on_unit(attack->throw_effect_index, unit_index);
	/* (his throw: Stubbs's attack-2 animation, in Halo's disarm slot,
	stubbs_mode.c; the body plays it on while the player drives the hand) */
	unit_animation_start_action(unit_index, STUBBS_UNIT_ACTION_HAND_THROW);
	local->origin_unit_index = unit_index;
	local->hand_unit_index = hand_unit_index;
	local->possessed_unit_index = NONE;
	players_set_local_player_unit(local_player_index, hand_unit_index);
	stubbs_hand_on_arm(unit_index, FALSE);
	/* (the hand looks where it flies, a little above the ground) */
	{
		real_vector3d look = forward;

		look.k = 0.25f;
		normalize3d(&look);
		player_control_set_facing(local_player_index, &look);
	}
	stubbs_mode_log("the hand thrown (%s)", tag_get_name(attack->object_definition_index));
	return TRUE;
}

/* a thrown biped the player drives (the hand) moves the way a player's
biped does, by the game's player speeds, not by its animations (the
hand's own crawl has no movement Halo's animation code reads): its
definition's "uses player physics" flag set, at its offset in the layout
the definition is in now */
static void stubbs_driven_biped_flag(
	unsigned char *definition,
	unsigned long flags_offset)
{
	unsigned long flags;

	csmemcpy(&flags, definition + flags_offset, 4);
	flags |= FLAG(_biped_uses_player_physics_bit);
	csmemcpy(definition + flags_offset, &flags, 4);
}

static void stubbs_driven_biped_add(
	long definition_index)
{
	short index;

	for (index = 0; index < stubbs_driven_biped_count; index++)
	{
		if (stubbs_driven_bipeds[index] == definition_index)
			return;
	}
	if (stubbs_driven_biped_count >= MAXIMUM_STUBBS_UNITS_NOTED)
		return;
	stubbs_driven_bipeds[stubbs_driven_biped_count++] = definition_index;
	for (index = 0; index < stubbs_biped_noted_count; index++)
	{
		if (stubbs_bipeds_noted[index].definition_index == definition_index)
			stubbs_driven_biped_flag(stubbs_bipeds_noted[index].definition, HALO_BIPED_FLAGS_OFFSET);
	}
}

/* ---------- public code */

struct stubbs_special_attack const *stubbs_special_attacks_get(
	long unit_definition_index,
	short *count)
{
	struct stubbs_unit_definition const *definition = stubbs_unit_definition_get(unit_definition_index);

	*count = definition ? definition->special_attack_count : 0;
	return definition ? definition->special_attacks : NULL;
}

boolean stubbs_engine_unit_is_thrown(
	long unit_index)
{
	struct object_datum *object = unit_index != NONE ? object_try_and_get(unit_index) : NULL;
	short index;

	if (!object)
		return FALSE;
	for (index = 0; index < stubbs_driven_biped_count; index++)
	{
		if (stubbs_driven_bipeds[index] == object->definition_index)
			return TRUE;
	}
	return FALSE;
}

long stubbs_engine_hand_unit(
	short local_player_index)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_STUBBS_LOCAL_PLAYERS)
		return NONE;
	return stubbs_local_players[local_player_index].hand_unit_index;
}

void stubbs_engine_hand_rides(
	short local_player_index,
	long unit_index)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_STUBBS_LOCAL_PLAYERS ||
		stubbs_local_players[local_player_index].hand_unit_index == NONE)
	{
		return;
	}
	stubbs_local_players[local_player_index].possessed_unit_index = unit_index;
}

void stubbs_engine_note_unit_definition(
	void const *tag_instance,
	void const *definition,
	void const *tag_cache,
	unsigned long tag_data_size)
{
	unsigned char const *instance = tag_instance;
	unsigned char const *unit = definition;
	unsigned char const *base = tag_cache;
	struct stubbs_unit_definition *noted;
	long count;
	unsigned long address, offset;
	short index;

	if (!stubbs_unit_definitions_noted)
	{
		/* (stubbs_mode.c converts a map's tags once, at its load: the last
		map's units and players are gone) */
		stubbs_unit_definition_count = 0;
		stubbs_biped_noted_count = 0;
		stubbs_driven_biped_count = 0;
		stubbs_engine_forget_players();
	}
	stubbs_unit_definitions_noted = TRUE;
	{
		long definition_index = stubbs_read_long(instance + 0x0C);
		unsigned long group = (unsigned long)stubbs_read_long(instance);

		if (group == 'bipd' && stubbs_biped_noted_count < MAXIMUM_STUBBS_UNITS_NOTED)
		{
			/* (this one is still in Stubbs's layout: rewritten after this) */
			stubbs_bipeds_noted[stubbs_biped_noted_count].definition_index = definition_index;
			stubbs_bipeds_noted[stubbs_biped_noted_count++].definition = (unsigned char *)definition;
			for (index = 0; index < stubbs_driven_biped_count; index++)
			{
				if (stubbs_driven_bipeds[index] == definition_index)
					stubbs_driven_biped_flag((unsigned char *)definition, STUBBS_BIPED_FLAGS_OFFSET);
			}
		}
	}
	count = stubbs_read_long(unit + STUBBS_UNIT_SPECIAL_ATTACKS_OFFSET);
	address = (unsigned long)stubbs_read_long(unit + STUBBS_UNIT_SPECIAL_ATTACKS_OFFSET + 4);
	if (count <= 0 || stubbs_unit_definition_count >= MAXIMUM_STUBBS_UNIT_DEFINITIONS)
		return;
	if (count > STUBBS_MAXIMUM_SPECIAL_ATTACKS)
		count = STUBBS_MAXIMUM_SPECIAL_ATTACKS;
	offset = address - STUBBS_XBOX_TAG_CACHE_BASE;
	if (address < STUBBS_XBOX_TAG_CACHE_BASE || offset > tag_data_size ||
		(unsigned long)count * STUBBS_SPECIAL_ATTACK_SIZE > tag_data_size - offset)
	{
		return;
	}

	noted = &stubbs_unit_definitions[stubbs_unit_definition_count++];
	noted->definition_index = stubbs_read_long(instance + 0x0C);
	noted->special_attack_count = (short)count;
	for (index = 0; index < count; index++)
	{
		unsigned char const *element = base + offset + index * STUBBS_SPECIAL_ATTACK_SIZE;
		struct stubbs_special_attack *attack = &noted->special_attacks[index];
		short cost;

		/* (a tag reference: its group, name, name length and tag index) */
		attack->object_group = (unsigned long)stubbs_read_long(element + STUBBS_SPECIAL_ATTACK_OBJECT_OFFSET);
		attack->object_definition_index = stubbs_read_long(element + STUBBS_SPECIAL_ATTACK_OBJECT_OFFSET + 0x0C);
		attack->ready_effect_index = stubbs_read_long(element + STUBBS_SPECIAL_ATTACK_READY_EFFECT_OFFSET + 0x0C);
		attack->throw_effect_index = stubbs_read_long(element + STUBBS_SPECIAL_ATTACK_THROW_EFFECT_OFFSET + 0x0C);
		csmemcpy(&attack->speed, element + STUBBS_SPECIAL_ATTACK_SPEED_OFFSET, 4);
		csmemcpy(&cost, element + STUBBS_SPECIAL_ATTACK_COST_OFFSET, 2);
		attack->cost = cost;
		/* (the starting and the most: a byte each) */
		attack->initial_count = element[STUBBS_SPECIAL_ATTACK_INITIAL_COUNT_OFFSET];
		attack->maximum_count = element[STUBBS_SPECIAL_ATTACK_MAXIMUM_COUNT_OFFSET];
		if (attack->initial_count > attack->maximum_count)
			attack->initial_count = attack->maximum_count;
		csmemcpy(&attack->regeneration_period, element + STUBBS_SPECIAL_ATTACK_REGENERATION_PERIOD_OFFSET, 4);
		if (!(attack->regeneration_period > 0.f && attack->regeneration_period < 3600.f))
			attack->regeneration_period = 0.f;
		if (attack->object_group == 'bipd' && attack->object_definition_index != NONE)
			stubbs_driven_biped_add(attack->object_definition_index);
	}
	stubbs_mode_log("unit tag %08lX has %ld special attacks", (unsigned long)noted->definition_index, count);
}

void stubbs_engine_player_input(
	short local_player_index,
	unsigned long *player_control_flags,
	unsigned long hand_button,
	unsigned long unit_control_flags)
{
	struct stubbs_local_player *local;
	long player_index;
	long unit_index;
	boolean hand_pressed;

	/* (the next map's tags noted afresh) */
	stubbs_unit_definitions_noted = FALSE;
	if (local_player_index < 0 || local_player_index >= MAXIMUM_STUBBS_LOCAL_PLAYERS)
		return;
	local = &stubbs_local_players[local_player_index];
	hand_pressed = (*player_control_flags & hand_button) && !local->hand_button_down;
	local->hand_button_down = (*player_control_flags & hand_button) != 0;
	*player_control_flags &= ~hand_button;
	(void)unit_control_flags;

	player_index = local_player_get_player_index(local_player_index);
	if (player_index == NONE)
		return;
	unit_index = player_get(player_index)->unit_index;

	/* the hand or the human gone from under the player (deleted by a script,
	a revert): back to the body */
	if (local->hand_unit_index != NONE)
	{
		if (!object_try_and_get(local->hand_unit_index) ||
			(local->possessed_unit_index != NONE && !stubbs_unit_alive(local->possessed_unit_index)))
		{
			if (unit_index == local->hand_unit_index || unit_index == local->possessed_unit_index ||
				unit_index == NONE)
			{
				stubbs_hand_return(local_player_index, TRUE);
			}
			else
			{
				local->hand_unit_index = NONE;
				local->possessed_unit_index = NONE;
			}
			return;
		}
	}
	if (unit_index == NONE)
		return;

	if (local->hand_unit_index == NONE)
	{
		if (hand_pressed)
			stubbs_hand_throw(local_player_index, unit_index);
		return;
	}
	/* black with the hand out, or with a body it rides: the hand home */
	if (hand_pressed && (unit_index == local->hand_unit_index || unit_index == local->possessed_unit_index))
		stubbs_hand_return(local_player_index, TRUE);
}

boolean stubbs_engine_player_unit_died(
	long unit_index,
	long player_index)
{
	struct player_datum *player = player_get(player_index);
	short local_player_index = player->local_player_index;
	struct stubbs_local_player *local;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_STUBBS_LOCAL_PLAYERS)
		return FALSE;
	local = &stubbs_local_players[local_player_index];
	if (local->hand_unit_index == NONE || unit_index == local->origin_unit_index)
		return FALSE;
	if (unit_index != local->hand_unit_index && unit_index != local->possessed_unit_index)
		return FALSE;
	if (!stubbs_unit_alive(local->origin_unit_index))
		return FALSE;
	/* (the hand killed stays where it fell; a dead human's hand comes home) */
	stubbs_hand_return(local_player_index, unit_index != local->hand_unit_index);
	stubbs_mode_log("the player's %s died: back in Stubbs's body",
		unit_index == local->possessed_unit_index ? "human" : "hand");
	return TRUE;
}

long stubbs_engine_player_origin_unit(
	long player_index)
{
	struct player_datum *player = player_try_and_get(player_index);
	short local_player_index;

	if (!player)
		return NONE;
	local_player_index = player->local_player_index;
	if (local_player_index >= 0 && local_player_index < MAXIMUM_STUBBS_LOCAL_PLAYERS &&
		stubbs_local_players[local_player_index].hand_unit_index != NONE &&
		object_try_and_get(stubbs_local_players[local_player_index].origin_unit_index))
	{
		return stubbs_local_players[local_player_index].origin_unit_index;
	}
	return player->unit_index;
}

boolean stubbs_engine_special_attack(
	long unit_index,
	short special_attack)
{
	short local_player_index = stubbs_local_player_from_unit(unit_index);
	struct unit_datum *unit = unit_try_and_get(unit_index);
	short index;

	if (!unit)
		return FALSE;
	if (local_player_index == NONE && unit->unit.player_index != NONE)
		local_player_index = player_get(unit->unit.player_index)->local_player_index;
	if (special_attack != _stubbs_special_attack_hand || local_player_index == NONE)
		return FALSE;
	for (index = 0; index < MAXIMUM_STUBBS_LOCAL_PLAYERS; index++)
	{
		if (index == local_player_index && stubbs_local_players[index].hand_unit_index == NONE)
			return stubbs_hand_throw(local_player_index, unit_index);
	}
	return FALSE;
}

short stubbs_engine_special_attack_get_count(
	long unit_index,
	short special_attack)
{
	struct stubbs_unit_count *count = stubbs_unit_count_get(unit_index, FALSE);

	if (special_attack < 0 || special_attack >= STUBBS_MAXIMUM_SPECIAL_ATTACKS)
		return 0;
	/* (until a script or a throw changes them, a unit's counts are its
	definition's starting ones) */
	return count ? count->counts[special_attack] : stubbs_unit_initial_count(unit_index, special_attack);
}

short stubbs_engine_special_attack_get_maximum(
	long unit_index,
	short special_attack)
{
	struct stubbs_special_attack const *attack = stubbs_unit_special_attack_get(unit_index, special_attack);

	return attack ? attack->maximum_count : 0;
}

void stubbs_engine_special_attack_credit(
	long unit_index,
	real credit)
{
	struct stubbs_unit_count *counts = stubbs_unit_count_get(unit_index, TRUE);
	short index;

	if (!counts || !(credit > 0.f))
		return;
	for (index = 0; index < STUBBS_MAXIMUM_SPECIAL_ATTACKS; index++)
	{
		struct stubbs_special_attack const *attack = stubbs_unit_special_attack_get(unit_index, index);

		if (!attack || !(attack->regeneration_period > 0.f))
			continue;
		counts->credit[index] += credit;
		while (counts->credit[index] >= attack->regeneration_period && counts->counts[index] < attack->maximum_count)
		{
			counts->credit[index] -= attack->regeneration_period;
			counts->counts[index]++;
		}
		if (counts->counts[index] >= attack->maximum_count)
			counts->credit[index] = 0.f;
	}
}

void stubbs_engine_special_attack_set_count(
	long unit_index,
	short special_attack,
	short count)
{
	struct stubbs_unit_count *counts = stubbs_unit_count_get(unit_index, TRUE);
	short maximum = stubbs_engine_special_attack_get_maximum(unit_index, special_attack);

	if (!counts || special_attack < 0 || special_attack >= STUBBS_MAXIMUM_SPECIAL_ATTACKS)
		return;
	/* (never more than the unit can hold, as the retail game keeps it) */
	if (count > maximum)
		count = maximum;
	counts->counts[special_attack] = (char)(count < 0 ? 0 : count > 127 ? 127 : count);
}

void stubbs_engine_player_move_origin(
	long from_unit_index,
	long to_unit_index)
{
	short local_player_index = stubbs_local_player_from_unit(from_unit_index);

	if (local_player_index != NONE)
		stubbs_local_players[local_player_index].origin_unit_index = to_unit_index;
}

#endif
