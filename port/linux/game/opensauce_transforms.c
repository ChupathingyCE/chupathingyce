/*
OPENSAUCE_TRANSFORMS.C

OpenSauce's actor variant transforms (opensauce_transforms.h): on a map with
an OpenSauce header, its actor_variant_transform_collection tag lists actor
variants that change into others mid-fight, each with a transform_out (what
starts it: damage from a kind of unit, the actor's vitality, action or
state, and the animation and attachments of the old actor's last moments)
and a transform_in (the actors it may become, picked by chance, and what
they keep of the old one: team, encounter and squad, states, vitality).
SPV3 uses them on 22 of its 23 maps: the Flood taking over Marines, Elites,
Grunts, Jackals and Brutes, Elites drawing a sword when hurt, Grunts
setting off their grenades as they flee.

How a transform goes is OpenSauce's (Kornner Studios, GPL-3.0-or-later): its
c_actor_variant_transform_manager.cpp and AI.Transform.inl, ported to this
game's own functions. The stages are its: the old actor is frozen and plays
its transform_out animation (attachments made, key frame actions done);
when that ends, the new unit (or the old one, reused) gets its new actor,
frozen and unhurt while it plays its transform_in animation; then it is
let go. What differs here:

  - the animations are the game's custom animations (units.c's
    unit_start_user_animation, which network co-op already carries to the
    clients), not a state of their own; a stage ends when its animation
    ends, or stops, as OpenSauce's would at its final key frame;
  - the transforms under way are kept here, not in the game state, so a
    checkpoint's revert (or a saved game) ends any it finds where it
    stands and lets its actor go, rather than going on from the middle;
  - the random picks are this module's own seed, not the game's, so the
    game's own random numbers run as they would without transforms;
  - an attachment is made on the marker it was found on (OpenSauce looked
    for "marker0" and the rest, then attached to "marker");
  - a script naming a target gets that target (OpenSauce's lookup counted
    from the list's end).

Only where the actors think: a local game or a network game's host. A
client plays what its host sends (the new actor and unit, the animations,
the deleted unit), so co-op can follow the host's transforms.
*/

#include "cseries.h"
#include "errors.h"
#include "opensauce_transforms.h"

#ifdef OPENSAUCE_TRANSFORMS

#include "opensauce.h"
#include "opensauce_transform_tags.h"
#include "ai/actor_definitions.h"
#include "ai/actors.h"
#include "ai/ai_globals.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/game_globals.h"
#include "hs/hs_library_external.h"
#include "hs/object_lists.h"
#include "math/real_math.h"
#include "models/model_animation_definitions.h"
#include "models/models.h"
#include "networking/network_game_globals.h"
#include "objects/damage.h"
#include "objects/damage_effect_definitions.h"
#include "objects/objects.h"
#include "units/unit_definitions.h"
#include "units/units.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* OpenSauce's k_max_concurrent_transforms */
	MAXIMUM_TRANSFORMS_UNDER_WAY = 20,
	/* the most of a collection's transforms one actor variant has
	(tag_schema_opensauce.c's MAXIMUM_TRANSFORM_COLLECTION_TRANSFORMS) */
	MAXIMUM_CANDIDATE_TRANSFORMS = 32,
	/* the most riders and children a transform handles at once */
	MAXIMUM_TRANSFORM_CHILDREN = 32,
	/* (transform starts logged a map, then a count) */
	MAXIMUM_TRANSFORMS_LOGGED = 64,

	/* a Custom Edition tag instance (cache_files.c's) */
	TAG_INSTANCE_SIZE = 0x20,

	/* the object's flags while it transforms (OpenSauce takes two of its
	spare object flags, 28 and 29; 28 is Forge's here) */
	_object_transforming_out_bit = 30,
	_object_transforming_in_bit = 31,

	/* the tags' flags and enums (unit_transform_definition.hpp) */
	_transform_scripted_only_bit = 0,

	_target_try_to_use_existing_unit_bit = 0,
	_target_drop_weapon_bit,
	_target_inherit_seated_units_bit,
	_target_delete_attached_actors_bit,

	_out_attachments_destroyed_on_death_bit = 0,
	_out_invincible_during_transform_out_bit = 0,

	_criteria_on_damage_only_bit = 0,
	_criteria_ignore_friendly_fire_bit,
	_criteria_on_actor_action_bit,
	_criteria_on_actor_state_bit,
	_criteria_on_shield_range_bit,
	_criteria_on_health_range_bit,

	_keyframe_primary = 0,
	_keyframe_secondary,
	_keyframe_final,

	_rider_handling_none = 0,
	_rider_handling_kill,
	_rider_handling_eject,

	_effect_target_self = 0,
	_effect_target_riders,

	_team_handling_inherit_from_old_actor = 0,
	_team_handling_inherit_from_attacker,
	_team_handling_override,

	_state_handling_inherit = 0,
	_state_handling_override,
	_state_handling_actor_default,

	_squad_handling_inherit_from_old_actor = 0,
	_squad_handling_inherit_from_attacker,
	_squad_handling_free_actor,

	_vitality_handling_neither = 0,
	_vitality_handling_both,
	_vitality_handling_shield_only,
	_vitality_handling_health_only,

	_damage_type_both = 0,
	_damage_type_weapon_only,
	_damage_type_melee_only,

	/* a damage effect's melee category, and the team none (Halo's) */
	DAMAGE_CATEGORY_MELEE = 6,
	GAME_TEAM_NONE = 0,

	/* a transform's stages */
	_stage_begin = 0,
	_stage_transform_out_begun,
	_stage_transform_out_ended,
	_stage_transform_in_begun,
	_stage_transform_in_ended,
};

typedef char verify_transform_bits_are_spare[NUMBER_OF_OBJECT_DATA_FLAGS <= _object_transforming_out_bit ? 1 : -1];

#define TRANSFORMING_MASK (FLAG(_object_transforming_out_bit) | FLAG(_object_transforming_in_bit))

/* ---------- structures */

struct transform_state
{
	/* NONE: free */
	long unit_index;
	short instigator_team;
	short instigator_squad;
	long instigator_encounter;
	short entry_index;
	short transform_index;
	short target_index;
	short stage;
};

/* ---------- globals */

static struct
{
	void *tag_instances;
	long tag_count;
	struct opensauce_transform_collection const *collection;
	struct transform_state states[MAXIMUM_TRANSFORMS_UNDER_WAY];
	unsigned long seed;
	long logged;
	boolean full_logged;
} transforms;

/* ---------- private code */

static void transforms_log(
	char const *format,
	...)
{
	char text[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	error(_error_silent, "OpenSauce transforms: %s", text);
}

static void transforms_clear_states(
	void)
{
	short index;

	for (index = 0; index < MAXIMUM_TRANSFORMS_UNDER_WAY; index++)
		transforms.states[index].unit_index = NONE;
}

static real transforms_random(
	void)
{
	return real_seed_random_range(&transforms.seed, 0.f, 1.f);
}

/* whether this machine decides the actors (a client follows its host) */
static boolean transforms_decide_here(
	void)
{
	return transforms.collection && !network_game_distributed_client();
}

/* a tag's root by its handle, if it is one of group */
static void const *transforms_tag_get(
	long tag_index,
	unsigned long group_tag)
{
	byte const *instance;
	unsigned long instance_group, instance_index, address;

	if (tag_index == NONE || !transforms.tag_instances || (tag_index & 0xFFFF) >= transforms.tag_count)
		return NULL;
	instance = (byte const *)transforms.tag_instances + (tag_index & 0xFFFF) * TAG_INSTANCE_SIZE;
	memcpy(&instance_group, instance, sizeof(instance_group));
	memcpy(&instance_index, instance + 0x0C, sizeof(instance_index));
	memcpy(&address, instance + 0x14, sizeof(address));
	if (instance_group != group_tag || (long)instance_index != tag_index || !address)
		return NULL;
	return xbox_pointer(address);
}

static char const *transforms_tag_name(
	long tag_index)
{
	unsigned long name;

	if (tag_index == NONE || !transforms.tag_instances || (tag_index & 0xFFFF) >= transforms.tag_count)
		return "none";
	memcpy(&name, (byte const *)transforms.tag_instances + (tag_index & 0xFFFF) * TAG_INSTANCE_SIZE + 0x10,
		sizeof(name));
	return name ? tag_name_strip_path((char const *)xbox_pointer(name)) : "none";
}

static void const *transforms_block_element(
	struct tag_block const *block,
	long index,
	unsigned long size)
{
	if (index < 0 || index >= block->count || !block->address)
		return NULL;
	return (byte const *)xbox_pointer((unsigned long)block->address) + index * size;
}

static struct opensauce_transform_collection_entry const *transforms_entry_get(
	short entry_index)
{
	return transforms_block_element(&transforms.collection->actor_variant_transforms, entry_index,
		sizeof(struct opensauce_transform_collection_entry));
}

static struct opensauce_transform_collection_transform const *transforms_transform_get(
	struct opensauce_transform_collection_entry const *entry,
	short transform_index)
{
	return entry ? transforms_block_element(&entry->transforms, transform_index,
		sizeof(struct opensauce_transform_collection_transform)) : NULL;
}

static struct opensauce_transform_out const *transforms_out_get(
	struct opensauce_transform_collection_transform const *transform)
{
	return transform ? transforms_tag_get(transform->transform_out.index, 'avto') : NULL;
}

static struct opensauce_transform_in const *transforms_in_get(
	struct opensauce_transform_collection_transform const *transform)
{
	return transform ? transforms_tag_get(transform->transform_in.index, 'avti') : NULL;
}

/* the collection's entry for an actor variant, NONE if it has none */
static short transforms_find_entry(
	long actor_variant_index)
{
	long index;

	for (index = 0; index < transforms.collection->actor_variant_transforms.count; index++)
	{
		struct opensauce_transform_collection_entry const *entry = transforms_entry_get((short)index);

		if (entry && entry->actor_variant.index == actor_variant_index)
			return (short)index;
	}
	return NONE;
}

static struct transform_state *transforms_find_state(
	long unit_index)
{
	short index;

	for (index = 0; index < MAXIMUM_TRANSFORMS_UNDER_WAY; index++)
	{
		if (transforms.states[index].unit_index != NONE && transforms.states[index].unit_index == unit_index)
			return &transforms.states[index];
	}
	return NULL;
}

static struct transform_state *transforms_allocate_state(
	long unit_index)
{
	short index;

	for (index = 0; index < MAXIMUM_TRANSFORMS_UNDER_WAY; index++)
	{
		struct transform_state *state = &transforms.states[index];

		if (state->unit_index == NONE)
		{
			state->unit_index = unit_index;
			state->instigator_team = GAME_TEAM_NONE;
			state->instigator_squad = NONE;
			state->instigator_encounter = NONE;
			state->entry_index = NONE;
			state->transform_index = NONE;
			state->target_index = NONE;
			state->stage = _stage_begin;
			return state;
		}
	}
	return NULL;
}

static void transforms_free_state(
	struct transform_state *state)
{
	if (state)
		state->unit_index = NONE;
}

/* a unit an actor of which may start a transform (OpenSauce's UnitIsValid):
alive, not seated, not transforming, with an actor */
static struct unit_datum *transforms_unit_valid(
	long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);

	if (!unit || unit->unit.parent_seat_index != NONE ||
		TEST_FLAG(unit->object.damage_flags, _object_dead_bit) || (unit->object.flags & TRANSFORMING_MASK) ||
		unit->unit.actor_index == NONE || !actor_try_and_get(unit->unit.actor_index))
	{
		return NULL;
	}
	return unit;
}

/* the unit's children that are units (riders, or any), up to maximum */
static short transforms_child_units(
	long unit_index,
	boolean seated_only,
	long *children,
	short *seats,
	short maximum)
{
	long child_index = object_get(unit_index)->object.first_child_object_index;
	short count = 0;

	while (child_index != NONE && count < maximum)
	{
		struct object_datum *child = object_get(child_index);
		struct unit_datum *child_unit = unit_try_and_get(child_index);

		if (child_unit && (!seated_only || child_unit->unit.parent_seat_index != NONE))
		{
			children[count] = child_index;
			if (seats)
				seats[count] = child_unit->unit.parent_seat_index;
			count++;
		}
		child_index = child->object.next_object_index;
	}
	return count;
}

/* the unit's children that have an actor: deleted, or detached */
static void transforms_child_actors(
	long unit_index,
	boolean delete_them)
{
	long children[MAXIMUM_TRANSFORM_CHILDREN];
	short count = transforms_child_units(unit_index, FALSE, children, NULL, MAXIMUM_TRANSFORM_CHILDREN);
	short index;

	for (index = 0; index < count; index++)
	{
		struct unit_datum *child = unit_try_and_get(children[index]);

		if (!child || (child->unit.actor_index == NONE && child->unit.swarm_actor_index == NONE))
			continue;
		if (delete_them)
			object_delete(children[index]);
		else
			object_detach(children[index]);
	}
}

static short transforms_team(
	short handling,
	short old_team,
	short attacker_team,
	short override_team)
{
	switch (handling)
	{
	case _team_handling_inherit_from_old_actor:
		return old_team;
	case _team_handling_inherit_from_attacker:
		return attacker_team;
	case _team_handling_override:
		return override_team;
	}
	return GAME_TEAM_NONE;
}

static void transforms_vitality(
	short handling,
	struct unit_datum *unit,
	real health,
	real shield)
{
	switch (handling)
	{
	case _vitality_handling_both:
		unit->object.body_vitality = health;
		unit->object.shield_vitality = shield;
		break;
	case _vitality_handling_shield_only:
		unit->object.shield_vitality = shield;
		break;
	case _vitality_handling_health_only:
		unit->object.body_vitality = health;
		break;
	}
}

/* ---------- private code: finding a transform */

static boolean transforms_vitality_criteria(
	struct unit_datum const *unit,
	struct opensauce_transform_out const *out)
{
	boolean health_within = unit->object.body_vitality >= out->health_range[0] &&
		unit->object.body_vitality <= out->health_range[1];
	boolean shield_within = unit->object.shield_vitality >= out->shield_range[0] &&
		unit->object.shield_vitality <= out->shield_range[1];

	if (TEST_FLAG(out->criteria_flags, _criteria_on_health_range_bit) && !health_within)
		return FALSE;
	if (TEST_FLAG(out->criteria_flags, _criteria_on_shield_range_bit) && !shield_within)
		return FALSE;
	return TRUE;
}

/* what a damage transform's choice tests it with */
struct transforms_damage_test
{
	struct unit_datum const *unit;
	struct unit_datum const *instigator;
	boolean melee;
};

typedef boolean (*transforms_select_proc)(
	struct opensauce_transform_collection_transform const *transform,
	struct opensauce_transform_out const *out,
	void const *data);

/* one of an entry's transforms (OpenSauce's FindTransform): each not only
for scripts is a candidate by its chance at this difficulty, then by the
test; one of those picked at random, NONE if none */
static short transforms_find(
	struct opensauce_transform_collection_entry const *entry,
	transforms_select_proc select,
	void const *data)
{
	short candidates[MAXIMUM_CANDIDATE_TRANSFORMS];
	short count = 0;
	short difficulty = game_difficulty_level_get();
	long index;

	if (difficulty < 0 || difficulty >= 4)
		difficulty = _game_difficulty_level_normal;
	for (index = 0; index < entry->transforms.count && count < MAXIMUM_CANDIDATE_TRANSFORMS; index++)
	{
		struct opensauce_transform_collection_transform const *transform =
			transforms_transform_get(entry, (short)index);
		struct opensauce_transform_out const *out = transforms_out_get(transform);

		if (!transform || !out || !transforms_in_get(transform) ||
			TEST_FLAG(transform->flags, _transform_scripted_only_bit))
		{
			continue;
		}
		if (transforms_random() > transform->selection_chance[difficulty])
			continue;
		if (select(transform, out, data))
			candidates[count++] = (short)index;
	}
	return count ? candidates[seed_random_range(&transforms.seed, 0, count)] : NONE;
}

static boolean transforms_damage_criteria(
	struct opensauce_transform_out const *out,
	struct transforms_damage_test const *test)
{
	if (!TEST_FLAG(out->criteria_flags, _criteria_on_damage_only_bit))
		return FALSE;
	if (test->instigator && TEST_FLAG(out->criteria_flags, _criteria_ignore_friendly_fire_bit) &&
		!game_team_is_enemy(test->unit->object.owner_team_index, test->instigator->object.owner_team_index))
	{
		return FALSE;
	}
	if (!TEST_FLAG(out->criteria_flags, _criteria_on_health_range_bit) &&
		!TEST_FLAG(out->criteria_flags, _criteria_on_shield_range_bit))
	{
		return FALSE;
	}
	return transforms_vitality_criteria(test->unit, out);
}

/* damage by a unit: a transform whose instigators name the unit's type (and
the damage's kind, as OpenSauce reads its list: the first whose kind is the
other's turns the transform down) */
static boolean transforms_select_instigated(
	struct opensauce_transform_collection_transform const *transform,
	struct opensauce_transform_out const *out,
	void const *data)
{
	struct transforms_damage_test const *test = data;
	long index;

	(void)transform;
	if (!transforms_damage_criteria(out, test))
		return FALSE;
	for (index = 0; index < out->instigators.count; index++)
	{
		struct opensauce_transform_out_instigator const *instigator = transforms_block_element(&out->instigators,
			index, sizeof(struct opensauce_transform_out_instigator));

		if (!instigator)
			continue;
		if ((instigator->damage_type == _damage_type_weapon_only && test->melee) ||
			(instigator->damage_type == _damage_type_melee_only && !test->melee))
		{
			return FALSE;
		}
		if (instigator->unit.index == test->instigator->definition_index)
			return TRUE;
	}
	return FALSE;
}

/* damage by no one in particular: a transform with no instigators */
static boolean transforms_select_uninstigated(
	struct opensauce_transform_collection_transform const *transform,
	struct opensauce_transform_out const *out,
	void const *data)
{
	(void)transform;
	return out->instigators.count == 0 && transforms_damage_criteria(out, data);
}

/* each tick: a transform not on damage nor by an instigator, whose action,
state and vitality the actor meets */
static boolean transforms_select_update(
	struct opensauce_transform_collection_transform const *transform,
	struct opensauce_transform_out const *out,
	void const *data)
{
	struct unit_datum const *unit = data;
	struct actor_datum const *actor = actor_get(unit->unit.actor_index);

	(void)transform;
	if (TEST_FLAG(out->criteria_flags, _criteria_on_damage_only_bit) || out->instigators.count)
		return FALSE;
	if (TEST_FLAG(out->criteria_flags, _criteria_on_actor_action_bit) &&
		(actor->state.action < 0 || actor->state.action >= 16 || !TEST_FLAG(out->actor_action, actor->state.action)))
	{
		return FALSE;
	}
	/* (the actor's field OpenSauce reads for its state: its mode here) */
	if (TEST_FLAG(out->criteria_flags, _criteria_on_actor_state_bit) &&
		(actor->state.mode < 0 || actor->state.mode >= 16 || !TEST_FLAG(out->actor_state, actor->state.mode)))
	{
		return FALSE;
	}
	return transforms_vitality_criteria(unit, out);
}

/* ---------- private code: attachments */

static void transforms_destroy_attachments(
	long unit_index,
	struct opensauce_transform_out const *out)
{
	long index;

	for (index = 0; index < out->attachments.count; index++)
	{
		struct opensauce_transform_out_attachment const *attachment = transforms_block_element(&out->attachments,
			index, sizeof(struct opensauce_transform_out_attachment));
		long children[MAXIMUM_TRANSFORM_CHILDREN];
		long child_index;
		short count = 0, child;

		if (!attachment || attachment->object_type.index == NONE)
			continue;
		for (child_index = object_get(unit_index)->object.first_child_object_index;
			child_index != NONE && count < MAXIMUM_TRANSFORM_CHILDREN;
			child_index = object_get(child_index)->object.next_object_index)
		{
			if (object_get(child_index)->definition_index == attachment->object_type.index)
				children[count++] = child_index;
		}
		for (child = 0; child < count; child++)
			object_delete(children[child]);
	}
}

static void transforms_attach_object(
	long unit_index,
	struct opensauce_transform_out_attachment const *attachment,
	short marker_index,
	short team,
	real scale)
{
	struct unit_datum *unit = unit_get(unit_index);
	long model_index = unit_definition_get(unit->definition_index)->object.model.index;
	char marker[64];
	struct object_placement_data placement;
	long object_index;

	if (marker_index == NONE)
		snprintf(marker, sizeof(marker), "%.32s", attachment->destination_marker);
	else
		snprintf(marker, sizeof(marker), "%.32s%d", attachment->destination_marker, (int)marker_index);
	if (model_index == NONE || model_find_marker(model_index, marker) == NONE)
		return;
	object_placement_data_new(&placement, attachment->object_type.index, NONE);
	placement.position = unit->object.position;
	placement.owner_team_index = team;
	object_index = object_new(&placement);
	if (object_index == NONE)
		return;
	object_get(object_index)->object.scale = scale;
	{
		char object_marker[TAG_STRING_LENGTH + 1];

		snprintf(object_marker, sizeof(object_marker), "%.32s", attachment->object_marker);
		object_attach_to_marker(unit_index, marker, object_index, object_marker);
	}
}

static void transforms_attach_objects(
	long unit_index,
	short instigator_team,
	struct opensauce_transform_out const *out)
{
	long index;

	for (index = 0; index < out->attachments.count; index++)
	{
		struct opensauce_transform_out_attachment const *attachment = transforms_block_element(&out->attachments,
			index, sizeof(struct opensauce_transform_out_attachment));
		short team;
		real scale;
		short marker;

		if (!attachment || attachment->object_type.index == NONE)
			continue;
		team = transforms_team(attachment->team_handling, unit_get(unit_index)->object.owner_team_index,
			instigator_team, attachment->team_override);
		scale = real_seed_random_range(&transforms.seed, attachment->attachment_scale[0],
			attachment->attachment_scale[1]);
		if (attachment->destination_marker_count == 1)
			transforms_attach_object(unit_index, attachment, NONE, team, scale);
		else
		{
			for (marker = 0; marker < attachment->destination_marker_count && marker < MAXIMUM_TRANSFORM_CHILDREN;
				marker++)
			{
				transforms_attach_object(unit_index, attachment, marker, team, scale);
			}
		}
	}
}

/* ---------- private code: key frame actions */

static void transforms_keyframe_action(
	long unit_index,
	struct opensauce_transform_keyframe_action const *action)
{
	struct unit_datum *unit = unit_get(unit_index);
	long riders[MAXIMUM_TRANSFORM_CHILDREN];
	short rider_count = 0, index;

	if (action->target == _effect_target_riders || action->rider_handling != _rider_handling_none)
		rider_count = transforms_child_units(unit_index, TRUE, riders, NULL, MAXIMUM_TRANSFORM_CHILDREN);

	/* damage to the unit or its riders */
	if (action->damage_effect.index != NONE)
	{
		struct damage_data damage;
		long targets[MAXIMUM_TRANSFORM_CHILDREN];
		short target_count = 0;

		if (action->target == _effect_target_self)
			targets[target_count++] = unit_index;
		else
		{
			for (index = 0; index < rider_count; index++)
				targets[target_count++] = riders[index];
		}
		for (index = 0; index < target_count; index++)
		{
			struct object_datum *target = object_try_and_get(targets[index]);

			if (!target)
				continue;
			damage_data_new(&damage, action->damage_effect.index);
			damage.owner_player_index = unit->unit.player_index;
			damage.owner_object_index = unit_index;
			damage.owner_team_index = unit->object.owner_team_index;
			damage.location = target->object.location;
			damage.origin = target->object.position;
			damage.epicenter = target->object.position;
			object_cause_damage(&damage, targets[index], NONE, NONE, NONE, NULL);
		}
	}

	/* an effect on the unit or its riders */
	if (action->effect.index != NONE)
	{
		char marker[TAG_STRING_LENGTH + 1];

		snprintf(marker, sizeof(marker), "%.32s", action->effect_marker);
		if (action->target == _effect_target_self)
			hs_effect_new_from_object_marker(action->effect.index, unit_index, marker);
		else
		{
			for (index = 0; index < rider_count; index++)
				hs_effect_new_from_object_marker(action->effect.index, riders[index], marker);
		}
	}

	/* the riders killed or put out */
	for (index = 0; index < rider_count && action->rider_handling != _rider_handling_none; index++)
	{
		if (!unit_try_and_get(riders[index]))
			continue;
		if (action->rider_handling == _rider_handling_kill)
			unit_kill(riders[index]);
		else if (action->rider_handling == _rider_handling_eject)
			unit_exit_seat_end(riders[index]);
	}
}

static void transforms_keyframe_actions(
	long unit_index,
	struct tag_block const *actions,
	short keyframe)
{
	long index;

	for (index = 0; index < actions->count; index++)
	{
		struct opensauce_transform_keyframe_action const *action = transforms_block_element(actions, index,
			sizeof(struct opensauce_transform_keyframe_action));

		if (action && action->keyframe == keyframe && unit_try_and_get(unit_index))
			transforms_keyframe_action(unit_index, action);
	}
}

/* ---------- private code: the stages */

static void transforms_log_start(
	long unit_index,
	struct opensauce_transform_collection_transform const *transform,
	char const *cause)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct actor_datum *actor = actor_try_and_get(unit->unit.actor_index);

	if (transforms.logged < MAXIMUM_TRANSFORMS_LOGGED)
	{
		transforms.logged++;
		transforms_log("%s (%s) starts '%.32s' (%s), health %.2f shield %.2f", transforms_tag_name(
			actor ? actor->meta.variant_definition_index : NONE), transforms_tag_name(unit->definition_index),
			transform->transform_name, cause, (double)unit->object.body_vitality, (double)unit->object.shield_vitality);
	}
	else if (!transforms.full_logged)
	{
		transforms.full_logged = TRUE;
		transforms_log("(more transforms this map: not logged)");
	}
}

/* the old actor frozen, its transform_out animation started (OpenSauce's
TransformOut) */
static void transforms_out(
	long unit_index,
	struct opensauce_transform_out const *out,
	struct transform_state *state)
{
	struct unit_datum *unit = unit_get(unit_index);
	char animation[TAG_STRING_LENGTH + 1];

	actor_braindead(unit->unit.actor_index, TRUE);
	snprintf(animation, sizeof(animation), "%.32s", out->transform_out_animation);
	unit_start_user_animation(unit_index, unit->object.animation.animation_graph_index, animation, FALSE);
	transforms_attach_objects(unit_index, state->instigator_team, out);
	unit = unit_get(unit_index);
	if (TEST_FLAG(out->flags, _out_invincible_during_transform_out_bit))
		SET_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit, TRUE);
	unit->object.flags |= FLAG(_object_transforming_out_bit);
	state->stage = _stage_transform_out_begun;
}

/* the transform ended where it stands: its unit let go (a unit dead or
without its actor, a state that can't go on) */
static void transforms_abandon(
	long unit_index,
	struct transform_state *state)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);

	if (unit)
	{
		struct opensauce_transform_collection_transform const *transform = state ?
			transforms_transform_get(transforms_entry_get(state->entry_index), state->transform_index) : NULL;
		struct opensauce_transform_out const *out = transforms_out_get(transform);

		if (out && TEST_FLAG(out->attachment_flags, _out_attachments_destroyed_on_death_bit))
			transforms_destroy_attachments(unit_index, out);
		unit = unit_get(unit_index);
		unit->object.flags &= ~TRANSFORMING_MASK;
		if (!TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		{
			SET_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit, FALSE);
			if (unit->unit.actor_index != NONE && actor_try_and_get(unit->unit.actor_index))
				actor_braindead(unit->unit.actor_index, FALSE);
		}
		unit_stop_custom_animation(unit_index);
	}
	transforms_free_state(state);
}

/* the new actor's (OpenSauce's CreateUnitActor): its unit's inventory, and
an actor made for it as the target says */
static void transforms_create_actor(
	long unit_index,
	struct opensauce_transform_in_target const *target,
	long old_actor_index,
	long instigator_encounter,
	short instigator_squad)
{
	struct actor_datum *old_actor = old_actor_index != NONE ? actor_try_and_get(old_actor_index) : NULL;
	long encounter_index = NONE;
	short squad_index = NONE;
	short initial_state = 0;
	short return_state = NONE;

	actor_customize_unit(target->actor_variant.index, unit_index);
	switch (target->encounter_squad_handling)
	{
	case _squad_handling_inherit_from_old_actor:
		if (old_actor)
		{
			encounter_index = old_actor->meta.encounter_index;
			squad_index = old_actor->meta.squad_index;
		}
		break;
	case _squad_handling_inherit_from_attacker:
		encounter_index = instigator_encounter;
		squad_index = instigator_squad;
		break;
	}
	switch (target->initial_state_handling)
	{
	case _state_handling_inherit:
		if (old_actor)
			initial_state = old_actor->state.initial_state;
		break;
	case _state_handling_override:
		initial_state = target->initial_state_override;
		break;
	}
	/* (NONE: the actor's default, actor_create_for_unit's) */
	switch (target->return_state_handling)
	{
	case _state_handling_inherit:
		if (old_actor)
			return_state = old_actor->state.default_state;
		break;
	case _state_handling_override:
		return_state = target->return_state_override;
		break;
	}
	actor_create_for_unit(FALSE, unit_index, target->actor_variant.index, encounter_index, squad_index, FALSE, NONE,
		FALSE, initial_state, return_state, NONE, 0);
}

/* a new unit where the old one stands, as it moves (OpenSauce's
CreateUnitNew); NONE if there is no room */
static long transforms_new_unit(
	long unit_index,
	long unit_definition_index,
	short team)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct object_placement_data placement;

	object_placement_data_new(&placement, unit_definition_index, NONE);
	placement.owner_player_index = unit->object.owner_player_index;
	placement.owner_object_index = unit->object.owner_object_index;
	placement.position = unit->object.position;
	placement.translational_velocity = unit->object.translational_velocity;
	placement.forward = unit->object.forward;
	placement.up = unit->object.up;
	placement.angular_velocity = unit->object.angular_velocity;
	placement.owner_team_index = team;
	return object_new(&placement);
}

/* the target picked (by chance at this difficulty, where a script didn't
name one), the new actor made, its transform_in animation started
(OpenSauce's TransformIn) */
static void transforms_in(
	long unit_index,
	struct transform_state *state)
{
	struct opensauce_transform_collection_transform const *transform =
		transforms_transform_get(transforms_entry_get(state->entry_index), state->transform_index);
	struct opensauce_transform_out const *out = transforms_out_get(transform);
	struct opensauce_transform_in const *in = transforms_in_get(transform);
	struct opensauce_transform_in_target const *target;
	struct actor_variant_definition const *variant;
	struct unit_datum *unit, *new_unit;
	long new_unit_index, old_actor_index;
	boolean delete_unit = FALSE;
	real old_health, old_shield;
	char animation[TAG_STRING_LENGTH + 1];

	if (!out || !in)
	{
		transforms_abandon(unit_index, state);
		return;
	}
	if (state->target_index == NONE)
	{
		short difficulty = game_difficulty_level_get();
		real test = transforms_random();
		real lower = 0.f;
		long index;

		if (difficulty < 0 || difficulty >= 4)
			difficulty = _game_difficulty_level_normal;
		for (index = 0; index < in->targets.count; index++)
		{
			struct opensauce_transform_in_target const *candidate = transforms_block_element(&in->targets, index,
				sizeof(struct opensauce_transform_in_target));

			if (!candidate)
				continue;
			if (lower + candidate->selection_chance[difficulty] > test - 1.0e-6f)
			{
				state->target_index = (short)index;
				break;
			}
			lower += candidate->selection_chance[difficulty];
		}
	}
	target = transforms_block_element(&in->targets, state->target_index, sizeof(struct opensauce_transform_in_target));
	variant = target && target->actor_variant.index != NONE ?
		actor_variant_definition_get(target->actor_variant.index) : NULL;
	if (!target || !variant || variant->unit_reference.index == NONE)
	{
		transforms_abandon(unit_index, state);
		return;
	}

	if (TEST_FLAG(target->flags, _target_drop_weapon_bit))
		unit_drop_current_weapon(unit_index, TRUE);
	if (TEST_FLAG(target->flags, _target_delete_attached_actors_bit))
		transforms_child_actors(unit_index, TRUE);
	transforms_destroy_attachments(unit_index, out);

	unit = unit_get(unit_index);
	if (TEST_FLAG(target->flags, _target_try_to_use_existing_unit_bit) &&
		unit->definition_index == variant->unit_reference.index)
	{
		unit->object.flags &= ~FLAG(_object_transforming_out_bit);
		unit->object.owner_team_index = transforms_team(target->team_handling, unit->object.owner_team_index,
			state->instigator_team, target->team_override);
		new_unit_index = unit_index;
		transforms_child_actors(unit_index, FALSE);
	}
	else
	{
		new_unit_index = transforms_new_unit(unit_index, variant->unit_reference.index,
			transforms_team(target->team_handling, unit->object.owner_team_index, state->instigator_team,
				target->team_override));
		if (new_unit_index == NONE)
		{
			transforms_log("no room for a new %s: the transform is let go",
				transforms_tag_name(variant->unit_reference.index));
			transforms_abandon(unit_index, state);
			return;
		}
		if (TEST_FLAG(target->flags, _target_inherit_seated_units_bit))
		{
			long riders[MAXIMUM_TRANSFORM_CHILDREN];
			short seats[MAXIMUM_TRANSFORM_CHILDREN];
			short count = transforms_child_units(unit_index, TRUE, riders, seats, MAXIMUM_TRANSFORM_CHILDREN);
			short index;

			for (index = 0; index < count; index++)
			{
				unit_exit_seat_end(riders[index]);
				unit_enter_seat(riders[index], new_unit_index, seats[index]);
				unit_port_animation_set_idle(riders[index]);
			}
		}
		delete_unit = TRUE;
	}
	unit = unit_get(unit_index);
	new_unit = unit_get(new_unit_index);
	/* (stand: a unit "flaming" might not get its weapon) */
	new_unit->unit.animation.base_seat_index = _unit_base_seat_stand;

	/* (making the actor sets the unit's vitality: the old one's kept first) */
	old_health = unit->object.body_vitality;
	old_shield = unit->object.shield_vitality;
	old_actor_index = unit->unit.actor_index;
	transforms_create_actor(new_unit_index, target, old_actor_index, state->instigator_encounter,
		state->instigator_squad);
	new_unit = unit_get(new_unit_index);
	if (new_unit->unit.actor_index != NONE)
		actor_braindead(new_unit->unit.actor_index, TRUE);
	/* (so a weapon such as the energy sword is readied, not dropped as it
	would be at once) */
	unit_port_ready_desired_weapon(new_unit_index);
	new_unit = unit_get(new_unit_index);
	transforms_vitality(target->vitality_inheritance, new_unit, old_health, old_shield);
	transforms_vitality(target->vitality_override, new_unit, target->health_override, target->shield_override);
	SET_FLAG(new_unit->object.damage_flags, _object_shield_depleted_bit, new_unit->object.shield_vitality <= 0.f);

	if (delete_unit)
	{
		transforms_child_actors(unit_index, FALSE);
		unit = unit_get(unit_index);
		if (unit->unit.actor_index != NONE && actor_try_and_get(unit->unit.actor_index))
			actor_delete(unit->unit.actor_index, FALSE);
		object_delete(unit_index);
	}

	new_unit = unit_get(new_unit_index);
	snprintf(animation, sizeof(animation), "%.32s", target->transform_in_animation);
	unit_start_user_animation(new_unit_index, new_unit->object.animation.animation_graph_index, animation, FALSE);
	new_unit = unit_get(new_unit_index);
	SET_FLAG(new_unit->object.damage_flags, _object_cannot_take_damage_bit, TRUE);
	new_unit->object.flags |= FLAG(_object_transforming_in_bit);
	state->unit_index = new_unit_index;
	state->stage = _stage_transform_in_begun;
	if (transforms.logged < MAXIMUM_TRANSFORMS_LOGGED)
	{
		transforms.logged++;
		transforms_log("'%.32s' becomes %s (%s), team %d", target->target_name,
			transforms_tag_name(target->actor_variant.index), delete_unit ? "a new unit" : "the same unit",
			(int)new_unit->object.owner_team_index);
	}
}

/* the new actor let go (OpenSauce's TransformEnd) */
static void transforms_end(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);

	if (unit->unit.actor_index != NONE && actor_try_and_get(unit->unit.actor_index))
		actor_braindead(unit->unit.actor_index, FALSE);
	unit = unit_get(unit_index);
	SET_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit, FALSE);
	unit->object.flags &= ~FLAG(_object_transforming_in_bit);
	unit_stop_custom_animation(unit_index);
}

/* a transform started on a unit: its state filled in, its old actor's
transform_out begun. FALSE if none may start (twenty under way) */
static boolean transforms_start(
	long unit_index,
	short entry_index,
	short transform_index,
	short target_index,
	short instigator_team,
	long instigator_encounter,
	short instigator_squad,
	char const *cause)
{
	struct opensauce_transform_collection_transform const *transform =
		transforms_transform_get(transforms_entry_get(entry_index), transform_index);
	struct opensauce_transform_out const *out = transforms_out_get(transform);
	struct transform_state *state;

	if (!out || !transforms_in_get(transform))
		return FALSE;
	state = transforms_allocate_state(unit_index);
	if (!state)
		return FALSE;
	state->instigator_team = instigator_team;
	state->instigator_encounter = instigator_encounter;
	state->instigator_squad = instigator_squad;
	state->entry_index = entry_index;
	state->transform_index = transform_index;
	state->target_index = target_index;
	transforms_log_start(unit_index, transform, cause);
	transforms_out(unit_index, out, state);
	return TRUE;
}

/* the transforms under way, a step each */
static void transforms_step(
	long *stepped,
	short *stepped_count)
{
	short index;

	for (index = 0; index < MAXIMUM_TRANSFORMS_UNDER_WAY; index++)
	{
		struct transform_state *state = &transforms.states[index];
		long unit_index = state->unit_index;
		struct unit_datum *unit;

		if (unit_index == NONE)
			continue;
		unit = unit_try_and_get(unit_index);
		if (!unit)
		{
			transforms_free_state(state);
			continue;
		}
		stepped[(*stepped_count)++] = unit_index;
		if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit) || unit->unit.actor_index == NONE)
		{
			transforms_abandon(unit_index, state);
			continue;
		}
		if (unit->object.flags & FLAG(_object_transforming_out_bit))
		{
			/* (its animation ended, stopped or never began: the stage is
			over, as at OpenSauce's final key frame) */
			if (state->stage == _stage_transform_out_begun &&
				unit->unit.animation.state != _unit_state_user_animation)
			{
				state->stage = _stage_transform_out_ended;
			}
			/* (no new actor while the AI is off) */
			if (state->stage == _stage_transform_out_ended && ai_globals && ai_globals->ai_active)
				transforms_in(unit_index, state);
		}
		else if (unit->object.flags & FLAG(_object_transforming_in_bit))
		{
			if (state->stage == _stage_transform_in_begun &&
				unit->unit.animation.state != _unit_state_user_animation)
			{
				state->stage = _stage_transform_in_ended;
			}
			if (state->stage == _stage_transform_in_ended)
			{
				transforms_end(unit_index);
				transforms_free_state(state);
			}
		}
		else
		{
			transforms_free_state(state);
		}
	}
}

/* ---------- public code */

void opensauce_transforms_tags_loaded(
	void *tag_instances,
	long tag_count)
{
	long index;
	long entries = 0, transform_count = 0;

	opensauce_transforms_map_unloaded();
	if (!opensauce_map_active() || !tag_instances || tag_count <= 0)
		return;
	transforms.tag_instances = tag_instances;
	transforms.tag_count = tag_count;
	/* (the first collection, as OpenSauce takes it: there should be one) */
	for (index = 0; index < tag_count && !transforms.collection; index++)
	{
		byte const *instance = (byte const *)tag_instances + index * TAG_INSTANCE_SIZE;
		unsigned long group, handle;

		memcpy(&group, instance, sizeof(group));
		memcpy(&handle, instance + 0x0C, sizeof(handle));
		if (group == 'avtc')
			transforms.collection = transforms_tag_get((long)handle, 'avtc');
	}
	if (!transforms.collection)
	{
		transforms.tag_instances = NULL;
		transforms.tag_count = 0;
		return;
	}
	for (index = 0; index < transforms.collection->actor_variant_transforms.count; index++)
	{
		struct opensauce_transform_collection_entry const *entry = transforms_entry_get((short)index);

		if (entry)
		{
			entries++;
			transform_count += entry->transforms.count;
		}
	}
	transforms.seed = 0x61767463UL;
	transforms_log("%ld actor variants transform (%ld transforms)", entries, transform_count);
}

void opensauce_transforms_map_unloaded(
	void)
{
	transforms.tag_instances = NULL;
	transforms.tag_count = 0;
	transforms.collection = NULL;
	transforms.logged = 0;
	transforms.full_logged = FALSE;
	transforms_clear_states();
}

void opensauce_transforms_game_state_loaded(
	void)
{
	struct object_iterator iterator;
	struct unit_datum *unit;
	long ended = 0;

	transforms_clear_states();
	if (!transforms.collection)
		return;
	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((unit = object_iterator_next(&iterator)) != NULL)
	{
		if (unit->object.flags & TRANSFORMING_MASK)
		{
			transforms_abandon(iterator.index, NULL);
			ended++;
		}
	}
	if (ended)
		transforms_log("a game state loaded: %ld transforms under way ended where they stood", ended);
}

void opensauce_transforms_update(
	void)
{
	long stepped[MAXIMUM_TRANSFORMS_UNDER_WAY];
	short stepped_count = 0;
	struct object_iterator iterator;
	struct unit_datum *unit;

	if (!transforms_decide_here())
		return;
	transforms_step(stepped, &stepped_count);

	/* (an actor whose action, state or vitality calls for a transform) */
	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((unit = object_iterator_next(&iterator)) != NULL)
	{
		long unit_index = iterator.index;
		struct actor_datum *actor;
		struct opensauce_transform_collection_entry const *entry;
		short entry_index, transform_index, index;
		boolean was_stepped = FALSE;

		for (index = 0; index < stepped_count; index++)
			was_stepped |= stepped[index] == unit_index;
		if (was_stepped || !transforms_unit_valid(unit_index))
			continue;
		actor = actor_get(unit->unit.actor_index);
		entry_index = transforms_find_entry(actor->meta.variant_definition_index);
		entry = entry_index != NONE ? transforms_entry_get(entry_index) : NULL;
		if (!entry)
			continue;
		transform_index = transforms_find(entry, transforms_select_update, unit);
		if (transform_index != NONE)
			transforms_start(unit_index, entry_index, transform_index, NONE, GAME_TEAM_NONE, NONE, NONE, "its state");
	}
}

void opensauce_transforms_unit_damaged(
	long unit_index,
	struct damage_data const *damage)
{
	struct unit_datum *unit;
	struct unit_datum *instigator;
	struct actor_datum *actor;
	struct opensauce_transform_collection_entry const *entry;
	struct transforms_damage_test test;
	short entry_index, transform_index;
	long instigator_encounter = NONE;
	short instigator_squad = NONE;
	short instigator_team;

	if (!transforms_decide_here() || !damage)
		return;
	unit = transforms_unit_valid(unit_index);
	if (!unit)
		return;
	actor = actor_get(unit->unit.actor_index);
	entry_index = transforms_find_entry(actor->meta.variant_definition_index);
	entry = entry_index != NONE ? transforms_entry_get(entry_index) : NULL;
	if (!entry)
		return;
	instigator = damage->owner_object_index != NONE ? unit_try_and_get(damage->owner_object_index) : NULL;
	test.unit = unit;
	test.instigator = instigator;
	test.melee = damage->definition_index != NONE &&
		damage_effect_definition_get(damage->definition_index)->damage.category == DAMAGE_CATEGORY_MELEE;
	/* (one for the attacker's kind first, then one for no one's) */
	transform_index = instigator ? transforms_find(entry, transforms_select_instigated, &test) : NONE;
	if (transform_index == NONE)
	{
		test.instigator = NULL;
		transform_index = transforms_find(entry, transforms_select_uninstigated, &test);
	}
	if (transform_index == NONE)
		return;
	instigator_team = instigator ? instigator->object.owner_team_index : GAME_TEAM_NONE;
	if (instigator)
	{
		long instigator_actor_index = instigator->unit.swarm_actor_index != NONE ?
			instigator->unit.swarm_actor_index : instigator->unit.actor_index;
		struct actor_datum *instigator_actor = instigator_actor_index != NONE ?
			actor_try_and_get(instigator_actor_index) : NULL;

		if (instigator_actor)
		{
			instigator_encounter = instigator_actor->meta.encounter_index;
			instigator_squad = instigator_actor->meta.squad_index;
		}
	}
	transforms_start(unit_index, entry_index, transform_index, NONE, instigator_team, instigator_encounter,
		instigator_squad, instigator ? transforms_tag_name(instigator->definition_index) : "damage");
}

void opensauce_transforms_unit_animation_event(
	long unit_index,
	boolean ended)
{
	struct unit_datum *unit;
	struct transform_state *state;
	struct opensauce_transform_collection_transform const *transform;
	short keyframe;

	if (!transforms_decide_here())
		return;
	unit = unit_try_and_get(unit_index);
	if (!unit || !(unit->object.flags & TRANSFORMING_MASK))
		return;
	state = transforms_find_state(unit_index);
	if (!state)
		return;
	transform = transforms_transform_get(transforms_entry_get(state->entry_index), state->transform_index);
	keyframe = _keyframe_final;
	if (!ended)
	{
		struct animation_graph *graph = animation_graph_definition_get(unit->object.animation.animation_graph_index);
		struct animation *animation = TAG_BLOCK_GET_ELEMENT(&graph->animations, unit->object.animation.state.index,
			struct animation);

		keyframe = unit->object.animation.state.frame_index == animation->private_second_key_frame_index &&
			unit->object.animation.state.frame_index != animation->private_key_frame_index ?
			_keyframe_secondary : _keyframe_primary;
	}
	if ((unit->object.flags & FLAG(_object_transforming_out_bit)) && state->stage == _stage_transform_out_begun)
	{
		struct opensauce_transform_out const *out = transforms_out_get(transform);

		if (out)
			transforms_keyframe_actions(unit_index, &out->keyframe_actions, keyframe);
		if (ended)
			state->stage = _stage_transform_out_ended;
	}
	else if ((unit->object.flags & FLAG(_object_transforming_in_bit)) && state->stage == _stage_transform_in_begun)
	{
		struct opensauce_transform_in const *in = transforms_in_get(transform);
		struct opensauce_transform_in_target const *target = in ?
			transforms_block_element(&in->targets, state->target_index, sizeof(struct opensauce_transform_in_target)) :
			NULL;

		if (target)
			transforms_keyframe_actions(unit_index, &target->keyframe_actions, keyframe);
		if (ended)
			state->stage = _stage_transform_in_ended;
	}
}

boolean opensauce_transforms_transform_actor(
	long unit_index,
	char const *transform_name,
	char const *target_name)
{
	struct unit_datum *unit;
	struct actor_datum *actor;
	struct opensauce_transform_collection_entry const *entry;
	struct opensauce_transform_collection_transform const *transform;
	short entry_index, transform_index = NONE, target_index = NONE;
	long index;

	if (!transforms_decide_here() || unit_index == NONE)
		return FALSE;
	unit = unit_try_and_get(unit_index);
	if (!unit || (unit->object.flags & TRANSFORMING_MASK) || unit->unit.actor_index == NONE)
		return FALSE;
	actor = actor_try_and_get(unit->unit.actor_index);
	if (!actor)
		return FALSE;
	entry_index = transforms_find_entry(actor->meta.variant_definition_index);
	entry = entry_index != NONE ? transforms_entry_get(entry_index) : NULL;
	if (!entry)
		return FALSE;
	if (transform_name && transform_name[0])
	{
		for (index = 0; index < entry->transforms.count; index++)
		{
			transform = transforms_transform_get(entry, (short)index);
			if (transform && !strncmp(transform->transform_name, transform_name, sizeof(transform->transform_name)))
			{
				transform_index = (short)index;
				break;
			}
		}
	}
	else
	{
		/* (one with no instigators, at random) */
		short candidates[MAXIMUM_CANDIDATE_TRANSFORMS];
		short count = 0;

		for (index = 0; index < entry->transforms.count && count < MAXIMUM_CANDIDATE_TRANSFORMS; index++)
		{
			struct opensauce_transform_out const *out = transforms_out_get(transforms_transform_get(entry, (short)index));

			if (out && out->instigators.count == 0)
				candidates[count++] = (short)index;
		}
		if (count)
			transform_index = candidates[seed_random_range(&transforms.seed, 0, count)];
	}
	transform = transforms_transform_get(entry, transform_index);
	if (!transform)
		return FALSE;
	if (target_name && target_name[0])
	{
		struct opensauce_transform_in const *in = transforms_in_get(transform);

		for (index = 0; in && index < in->targets.count; index++)
		{
			struct opensauce_transform_in_target const *target = transforms_block_element(&in->targets, index,
				sizeof(struct opensauce_transform_in_target));

			if (target && !strncmp(target->target_name, target_name, sizeof(target->target_name)))
			{
				target_index = (short)index;
				break;
			}
		}
		if (target_index == NONE)
			return FALSE;
	}
	return transforms_start(unit_index, entry_index, transform_index, target_index, GAME_TEAM_NONE, NONE, NONE,
		"a script");
}

boolean opensauce_transforms_transform_actors(
	long object_list_index,
	long actor_variant_index,
	char const *transform_name,
	char const *target_name)
{
	long reference, object_index;
	long units[MAXIMUM_TRANSFORMS_UNDER_WAY * 4];
	short count = 0, index;
	boolean success = FALSE;

	/* (the list read first: a transform changes no list, but its units are
	read before any starts) */
	for (object_index = object_list_get_first(object_list_index, &reference);
		object_index != NONE && count < (short)NUMBEROF(units);
		object_index = object_list_get_next(object_list_index, &reference))
	{
		units[count++] = object_index;
	}
	for (index = 0; index < count; index++)
	{
		if (actor_variant_index != NONE)
		{
			struct unit_datum *unit = unit_try_and_get(units[index]);
			struct actor_datum *actor = unit && unit->unit.actor_index != NONE ?
				actor_try_and_get(unit->unit.actor_index) : NULL;

			if (!actor || actor->meta.variant_definition_index != actor_variant_index)
				continue;
		}
		success |= opensauce_transforms_transform_actor(units[index], transform_name, target_name);
	}
	return success;
}

boolean opensauce_transforms_actor_is_transforming(
	long unit_index)
{
	struct unit_datum *unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;

	return unit && (unit->object.flags & TRANSFORMING_MASK) != 0;
}

#endif
