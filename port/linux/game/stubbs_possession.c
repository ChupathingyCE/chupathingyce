/*
STUBBS_POSSESSION.C

Stubbs the Zombie's possession, written for this engine from what the
retail game does (stubbs_possession.h). Stubbs mode only.

How the retail game has it: every human Stubbs can take has one seat in
his unit definition, Stubbs's own kind of seat (type 1, labeled "possess",
on the "possess" marker of his head), and the hand, a small biped the
player drives after throwing it, takes him by entering that seat with its
melee. From then on the player drives the human: the hand rides his head,
his AI is gone, he keeps his team (his friends stay his friends), his body
gets the extra vitality his collision model holds for a possessed body,
and his weapon is filled. The player's origin stays Stubbs's body. Black
(the hand called home) or the human's death ends it, and the human is the
game's again.

Here: Halo's layout keeps none of Stubbs's seats, so the possession seat
is noted from each unit definition before stubbs_mode.c rewrites it, and
the hand is attached to the human's "possess" marker (his head when a
model lacks it) instead of seated. The human's actor is remembered and
deleted while the player drives him, and made again, in his encounter and
squad, when the player lets go.

Sigmmma's reclaimer (GPL-3.0, stubbs/defs) was the reference for the
unit layout.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "real_math.h"
#include <math.h>
#include "objects.h"
#include "object_definitions.h"
#include "units.h"
#include "players.h"
#include "actors.h"
#include "weapons.h"
#include "tag_files.h"
#include "physics/collision_model_definitions.h"
#include "stubbs_mode.h"
#include "stubbs_engine.h"
#include "stubbs_possession.h"
#include "stubbs_functions.h"

#ifndef HALO_CONSOLE_NO_STUBBS

/* ---------- constants */

enum
{
	/* Stubbs's unit layout: the seats block, and a seat's fields */
	STUBBS_UNIT_SEATS_OFFSET = 0x3D8,
	STUBBS_UNIT_SEAT_SIZE = 0x11C,
	STUBBS_UNIT_SEAT_TYPE_OFFSET = 0x00,
	STUBBS_UNIT_SEAT_LABEL_OFFSET = 0x04,
	STUBBS_UNIT_SEAT_MARKER_OFFSET = 0x24,
	STUBBS_UNIT_SEAT_POSSESSION = 1,
	MAXIMUM_STUBBS_UNIT_SEATS = 16,

	/* Stubbs's collision model: the vitality a possessed body has */
	STUBBS_COLLISION_POSSESSED_VITALITY_OFFSET = 0x10,

	/* units a map may have that can be possessed */
	MAXIMUM_STUBBS_POSSESSABLE_DEFINITIONS = 64,
	MAXIMUM_STUBBS_LOCAL_PLAYERS = 4,

	/* an actor made again: alert, no default state (as ai_attach_free) */
	STUBBS_POSSESSION_ACTOR_INITIAL_STATE = 2,
	STUBBS_POSSESSION_ACTOR_DEFAULT_STATE = 0,
};

/* how far the hand reaches for a human (world units, from the hand to his
feet, so a human standing over it counts) */
#define STUBBS_POSSESSION_REACH 1.4f

/* ---------- structures */

struct stubbs_possessable_definition
{
	long definition_index;
	char marker_name[TAG_STRING_LENGTH + 1];
};

struct stubbs_possession
{
	/* the human the player drives, NONE for none */
	long body_unit_index;
	/* his mind, to give back: NONE when he had none */
	long actor_variant_definition_index;
	long encounter_index;
	short squad_index;
	boolean melee_button_down;
	boolean action_button_down;
};

/* ---------- globals */

static struct stubbs_possessable_definition stubbs_possessable_definitions[MAXIMUM_STUBBS_POSSESSABLE_DEFINITIONS];
static short stubbs_possessable_definition_count;
static boolean stubbs_possessable_definitions_noted;
static struct stubbs_possession stubbs_possessions[MAXIMUM_STUBBS_LOCAL_PLAYERS];
/* the last search for a human: how many could be taken, the nearest's
distance (for the log when none is in reach) */
static short stubbs_possession_candidate_count;
static real stubbs_possession_nearest_distance;

/* ---------- private code */

static void stubbs_possession_forget(
	short local_player_index)
{
	struct stubbs_possession *possession = &stubbs_possessions[local_player_index];

	possession->body_unit_index = NONE;
	possession->actor_variant_definition_index = NONE;
	possession->encounter_index = NONE;
	possession->squad_index = NONE;
}

static void stubbs_possession_forget_all(
	void)
{
	short index;

	for (index = 0; index < MAXIMUM_STUBBS_LOCAL_PLAYERS; index++)
		stubbs_possession_forget(index);
}

static long stubbs_read_long(
	unsigned char const *address)
{
	long value;

	csmemcpy(&value, address, 4);
	return value;
}

static struct stubbs_possessable_definition const *stubbs_possessable_definition_get(
	long definition_index)
{
	short index;

	for (index = 0; index < stubbs_possessable_definition_count; index++)
	{
		if (stubbs_possessable_definitions[index].definition_index == definition_index)
			return &stubbs_possessable_definitions[index];
	}
	return NULL;
}

static boolean stubbs_unit_alive(
	long unit_index)
{
	struct unit_datum *unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;

	return unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit);
}

static boolean stubbs_body_taken(
	long unit_index)
{
	short index;

	for (index = 0; index < MAXIMUM_STUBBS_LOCAL_PLAYERS; index++)
	{
		if (stubbs_possessions[index].body_unit_index == unit_index)
			return TRUE;
	}
	return FALSE;
}

/* the vitality Stubbs's collision model gives a possessed body (0 for
none: Halo's layout has padding there, which stubbs_mode.c keeps) */
static real stubbs_possessed_vitality(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct object_definition const *definition = object_definition_get(unit->definition_index);
	unsigned char const *collision;
	real vitality;

	if (definition->object.collision_model.index == NONE)
		return 0.f;
	collision = (unsigned char const *)collision_model_definition_get(definition->object.collision_model.index);
	csmemcpy(&vitality, collision + STUBBS_COLLISION_POSSESSED_VITALITY_OFFSET, sizeof(vitality));
	return valid_real(vitality) && vitality > 0.f && vitality < 100000.f ? vitality : 0.f;
}

/* the human's mind back: an actor of his old variant, in his old
encounter and squad */
static void stubbs_possession_give_back(
	short local_player_index)
{
	struct stubbs_possession *possession = &stubbs_possessions[local_player_index];
	long body_unit_index = possession->body_unit_index;
	struct unit_datum *body;

	if (!stubbs_unit_alive(body_unit_index))
	{
		stubbs_mode_log("the possessed body is gone");
		return;
	}
	body = unit_get(body_unit_index);
	if (body->unit.player_index != NONE || body->unit.actor_index != NONE)
		return;
	if (possession->actor_variant_definition_index != NONE)
	{
		actor_create_for_unit(FALSE, body_unit_index, possession->actor_variant_definition_index,
			possession->encounter_index, possession->squad_index, FALSE, NONE, FALSE,
			STUBBS_POSSESSION_ACTOR_INITIAL_STATE, STUBBS_POSSESSION_ACTOR_DEFAULT_STATE, NONE, 0);
	}
	stubbs_mode_log("%s has his own mind back", tag_get_name(body->definition_index));
}

/* ---------- public code */

void stubbs_possession_note_unit_definition(
	void const *tag_instance,
	void const *definition,
	void const *tag_cache,
	unsigned long tag_data_size)
{
	unsigned char const *instance = tag_instance;
	unsigned char const *unit = definition;
	unsigned char const *base = tag_cache;
	long count, index;
	unsigned long address, offset;

	if (!stubbs_possessable_definitions_noted)
	{
		/* (stubbs_mode.c converts a map's tags once, at its load: the last
		map's humans are gone) */
		stubbs_possessable_definition_count = 0;
		stubbs_possession_forget_all();
	}
	stubbs_possessable_definitions_noted = TRUE;
	count = stubbs_read_long(unit + STUBBS_UNIT_SEATS_OFFSET);
	address = (unsigned long)stubbs_read_long(unit + STUBBS_UNIT_SEATS_OFFSET + 4);
	if (count <= 0 || count > MAXIMUM_STUBBS_UNIT_SEATS ||
		stubbs_possessable_definition_count >= MAXIMUM_STUBBS_POSSESSABLE_DEFINITIONS)
	{
		return;
	}
	offset = address - STUBBS_XBOX_TAG_CACHE_BASE;
	if (address < STUBBS_XBOX_TAG_CACHE_BASE || offset > tag_data_size ||
		(unsigned long)count * STUBBS_UNIT_SEAT_SIZE > tag_data_size - offset)
	{
		return;
	}
	for (index = 0; index < count; index++)
	{
		unsigned char const *seat = base + offset + index * STUBBS_UNIT_SEAT_SIZE;
		short type;

		csmemcpy(&type, seat + STUBBS_UNIT_SEAT_TYPE_OFFSET, sizeof(type));
		if (type == STUBBS_UNIT_SEAT_POSSESSION ||
			!strncmp((char const *)seat + STUBBS_UNIT_SEAT_LABEL_OFFSET, "possess", TAG_STRING_LENGTH))
		{
			struct stubbs_possessable_definition *noted =
				&stubbs_possessable_definitions[stubbs_possessable_definition_count++];

			noted->definition_index = stubbs_read_long(instance + 0x0C);
			csmemcpy(noted->marker_name, seat + STUBBS_UNIT_SEAT_MARKER_OFFSET, TAG_STRING_LENGTH);
			noted->marker_name[TAG_STRING_LENGTH] = 0;
			if (!noted->marker_name[0])
				csstrncpy(noted->marker_name, "head", TAG_STRING_LENGTH);
			stubbs_mode_log("unit tag %08lX can be possessed (marker %s)",
				(unsigned long)noted->definition_index, noted->marker_name);
			return;
		}
	}
}

boolean stubbs_possession_unit_possessable(
	long unit_index)
{
	struct unit_datum *unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;

	if (!unit || unit->object.type != _object_type_biped || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return FALSE;
	if (unit->unit.player_index != NONE || unit->object.parent_object_index != NONE || stubbs_body_taken(unit_index))
		return FALSE;
	/* (the scripts' objects_cannot_be_possessed: stubbs_functions.c) */
	if (stubbs_functions_object_cannot(unit_index, _stubbs_object_cannot_be_possessed_bit))
		return FALSE;
	return stubbs_possessable_definition_get(unit->definition_index) != NULL;
}

long stubbs_possession_find_target(
	long hand_unit_index)
{
	struct object_datum *hand = hand_unit_index != NONE ? object_try_and_get(hand_unit_index) : NULL;
	struct object_iterator iterator;
	struct object_datum *object;
	long best_index = NONE;
	real best_distance = STUBBS_POSSESSION_REACH * STUBBS_POSSESSION_REACH;

	stubbs_possession_nearest_distance = -1.f;
	stubbs_possession_candidate_count = 0;
	if (!hand)
		return NONE;
	object_iterator_new(&iterator, FLAG(_object_type_biped), 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		real dx, dy, dz, distance;

		if (iterator.index == hand_unit_index || !stubbs_possession_unit_possessable(iterator.index))
			continue;
		stubbs_possession_candidate_count++;
		dx = object->object.position.x - hand->object.position.x;
		dy = object->object.position.y - hand->object.position.y;
		dz = object->object.position.z - hand->object.position.z;
		/* (a standing human's middle is above a hand on the ground: the
		height counts for less) */
		distance = dx * dx + dy * dy + 0.25f * dz * dz;
		if (stubbs_possession_nearest_distance < 0.f || distance < stubbs_possession_nearest_distance)
			stubbs_possession_nearest_distance = distance;
		if (distance < best_distance)
		{
			best_distance = distance;
			best_index = iterator.index;
		}
	}
	if (stubbs_possession_nearest_distance > 0.f)
		stubbs_possession_nearest_distance = (real)sqrt(stubbs_possession_nearest_distance);
	return best_index;
}

boolean stubbs_possession_begin(
	short local_player_index,
	long hand_unit_index,
	long human_unit_index)
{
	struct stubbs_possession *possession;
	struct stubbs_possessable_definition const *possessable;
	struct unit_datum *human;
	struct object_datum *hand;
	struct object_marker marker;
	char const *marker_name;
	short team_index;
	real vitality;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_STUBBS_LOCAL_PLAYERS)
		return FALSE;
	possession = &stubbs_possessions[local_player_index];
	if (possession->body_unit_index != NONE || !stubbs_possession_unit_possessable(human_unit_index))
		return FALSE;
	hand = hand_unit_index != NONE ? object_try_and_get(hand_unit_index) : NULL;
	if (!hand || hand->object.type != _object_type_biped || hand_unit_index == human_unit_index)
		return FALSE;
	human = unit_get(human_unit_index);
	possessable = stubbs_possessable_definition_get(human->definition_index);

	/* his mind remembered and set aside */
	possession->actor_variant_definition_index = NONE;
	possession->encounter_index = NONE;
	possession->squad_index = NONE;
	if (human->unit.actor_index != NONE)
	{
		struct actor_datum *actor = actor_get(human->unit.actor_index);

		possession->actor_variant_definition_index = actor->meta.variant_definition_index;
		possession->encounter_index = actor->meta.encounter_index;
		possession->squad_index = actor->meta.squad_index;
		actor_delete(human->unit.actor_index, FALSE);
	}

	/* the hand on his head */
	marker_name = possessable->marker_name;
	if (object_get_marker_by_name(human_unit_index, marker_name, &marker, 1) <= 0)
		marker_name = "head";
	if (hand->object.parent_object_index != NONE)
		object_detach(hand_unit_index);
	object_attach_to_marker(human_unit_index, marker_name, hand_unit_index, "");

	/* the player drives him; he keeps his team */
	team_index = human->object.owner_team_index;
	players_set_local_player_unit(local_player_index, human_unit_index);
	human->object.owner_team_index = team_index;

	/* a possessed body's vitality, full */
	vitality = stubbs_possessed_vitality(human_unit_index);
	if (vitality > 0.f)
	{
		human->object.maximum_body_vitality = vitality;
		human->object.body_vitality = 1.f;
	}

	/* his weapon filled */
	if (human->unit.current_weapon_index != NONE &&
		human->unit.weapon_object_indices[human->unit.current_weapon_index] != NONE)
	{
		weapon_set_current_amount(human->unit.weapon_object_indices[human->unit.current_weapon_index], 1.f);
	}

	possession->body_unit_index = human_unit_index;
	stubbs_engine_hand_rides(local_player_index, human_unit_index);
	stubbs_mode_log("the hand took %s", tag_get_name(human->definition_index));
	return TRUE;
}

void stubbs_possession_release(
	short local_player_index)
{
	struct stubbs_possession *possession;
	long player_index, body_unit_index, hand_unit_index;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_STUBBS_LOCAL_PLAYERS)
		return;
	possession = &stubbs_possessions[local_player_index];
	body_unit_index = possession->body_unit_index;
	if (body_unit_index == NONE)
		return;
	player_index = local_player_get_player_index(local_player_index);
	hand_unit_index = stubbs_engine_hand_unit(local_player_index);
	if (player_index != NONE && player_get(player_index)->unit_index == body_unit_index)
	{
		long origin_unit_index = stubbs_engine_player_origin_unit(player_index);

		if (origin_unit_index != NONE && origin_unit_index != body_unit_index)
			players_set_local_player_unit(local_player_index, origin_unit_index);
	}
	/* (the hand gone from his head: stubbs_engine.c sees it go and has the
	hand back on Stubbs's arm) */
	if (hand_unit_index != NONE && object_try_and_get(hand_unit_index))
	{
		if (object_get(hand_unit_index)->object.parent_object_index != NONE)
			object_detach(hand_unit_index);
		object_delete(hand_unit_index);
	}
	stubbs_possession_give_back(local_player_index);
	stubbs_possession_forget(local_player_index);
}

long stubbs_possession_body_unit(
	short local_player_index)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_STUBBS_LOCAL_PLAYERS)
		return NONE;
	return stubbs_possessions[local_player_index].body_unit_index;
}

void stubbs_possession_player_input(
	short local_player_index,
	unsigned long unit_control_flags)
{
	struct stubbs_possession *possession;
	long player_index, unit_index, hand_unit_index;
	boolean melee_pressed, action_pressed;

	/* (the next map's tags noted afresh) */
	stubbs_possessable_definitions_noted = FALSE;
	if (local_player_index < 0 || local_player_index >= MAXIMUM_STUBBS_LOCAL_PLAYERS)
		return;
	possession = &stubbs_possessions[local_player_index];
	melee_pressed = TEST_FLAG(unit_control_flags, _unit_control_use_equipment_bit) && !possession->melee_button_down;
	possession->melee_button_down = TEST_FLAG(unit_control_flags, _unit_control_use_equipment_bit) != 0;
	action_pressed = TEST_FLAG(unit_control_flags, _unit_control_action_bit) && !possession->action_button_down;
	possession->action_button_down = TEST_FLAG(unit_control_flags, _unit_control_action_bit) != 0;

	player_index = local_player_get_player_index(local_player_index);
	if (player_index == NONE)
		return;
	unit_index = player_get(player_index)->unit_index;

	if (possession->body_unit_index != NONE)
	{
		if (!object_try_and_get(possession->body_unit_index))
		{
			/* gone (a script, a revert): nothing to give back */
			stubbs_possession_forget(local_player_index);
		}
		else if (!stubbs_unit_alive(possession->body_unit_index))
		{
			/* dead under the player: stubbs_engine.c sends him home */
			stubbs_mode_log("the possessed body died");
			stubbs_possession_forget(local_player_index);
		}
		else if (unit_index != possession->body_unit_index)
		{
			/* black: the hand went home (stubbs_engine.c), the human is
			the game's again */
			stubbs_possession_give_back(local_player_index);
			stubbs_possession_forget(local_player_index);
		}
		return;
	}

	/* driving the hand, melee or action by a human: he is taken */
	hand_unit_index = stubbs_engine_hand_unit(local_player_index);
	if (hand_unit_index == NONE || unit_index != hand_unit_index || !(melee_pressed || action_pressed))
		return;
	if (!stubbs_possession_begin(local_player_index, hand_unit_index, stubbs_possession_find_target(hand_unit_index)))
	{
		stubbs_mode_log("no human within the hand's reach (%.1f world units): %d could be taken, the nearest %.1f away",
			STUBBS_POSSESSION_REACH, stubbs_possession_candidate_count, stubbs_possession_nearest_distance);
	}
}

#endif
