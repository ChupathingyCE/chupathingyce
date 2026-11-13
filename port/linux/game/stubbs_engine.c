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

Possession: the hand's action key next to a living human takes him. The
hand rides on his head and the player drives the human (his AI let go).
Black lets go: the hand comes home and the human drops dead, as the retail
game's possessed do.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "real_math.h"
#include "objects.h"
#include "units.h"
#include "players.h"
#include "effects.h"
#include "actors.h"
#include "tag_files.h"
#include "stubbs_mode.h"
#include "stubbs_engine.h"

#ifndef HALO_CONSOLE_NO_STUBBS

/* ---------- constants */

enum
{
	/* Stubbs's unit layout: the special attacks block */
	STUBBS_UNIT_SPECIAL_ATTACKS_OFFSET = 0x33C,
	STUBBS_SPECIAL_ATTACK_SIZE = 0x170,
	MAXIMUM_STUBBS_SPECIAL_ATTACKS = 8,

	/* a special attack's fields */
	STUBBS_SPECIAL_ATTACK_COST_OFFSET = 0x06,
	STUBBS_SPECIAL_ATTACK_READY_EFFECT_OFFSET = 0x4C,
	STUBBS_SPECIAL_ATTACK_THROW_EFFECT_OFFSET = 0x5C,
	STUBBS_SPECIAL_ATTACK_SPEED_OFFSET = 0xA8,
	STUBBS_SPECIAL_ATTACK_OBJECT_OFFSET = 0xAC,

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

struct stubbs_special_attack
{
	long object_definition_index;
	unsigned long object_group;
	long ready_effect_index;
	long throw_effect_index;
	real speed;
	short cost;
};

struct stubbs_unit_definition
{
	long definition_index;
	short special_attack_count;
	struct stubbs_special_attack special_attacks[MAXIMUM_STUBBS_SPECIAL_ATTACKS];
};

struct stubbs_unit_count
{
	long unit_index;
	char counts[MAXIMUM_STUBBS_SPECIAL_ATTACKS];
};

struct stubbs_local_player
{
	long origin_unit_index;
	long hand_unit_index;
	long possessed_unit_index;
	boolean hand_button_down;
	boolean action_button_down;
};

/* ---------- globals */

static struct stubbs_unit_definition stubbs_unit_definitions[MAXIMUM_STUBBS_UNIT_DEFINITIONS];
static short stubbs_unit_definition_count;
static boolean stubbs_unit_definitions_noted;
static struct stubbs_unit_count stubbs_unit_counts[MAXIMUM_STUBBS_UNIT_COUNTS];
static struct stubbs_local_player stubbs_local_players[MAXIMUM_STUBBS_LOCAL_PLAYERS];

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
		csmemset(free_slot->counts, 1, sizeof(free_slot->counts));
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
	local->origin_unit_index = unit_index;
	local->hand_unit_index = hand_unit_index;
	local->possessed_unit_index = NONE;
	players_set_local_player_unit(local_player_index, hand_unit_index);
	stubbs_mode_log("the hand thrown (%s)", tag_get_name(attack->object_definition_index));
	return TRUE;
}

/* a human the hand may take: a living biped of another kind (not a hand,
not Stubbs, not one of his zombies) */
static boolean stubbs_unit_possessable(
	long unit_index,
	struct stubbs_local_player const *local)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	char const *name;

	if (!unit || unit->object.type != _object_type_biped || !stubbs_unit_alive(unit_index))
		return FALSE;
	if (unit_index == local->hand_unit_index || unit_index == local->origin_unit_index)
		return FALSE;
	if (unit->unit.player_index != NONE || unit->object.parent_object_index != NONE)
		return FALSE;
	if (unit->definition_index == object_get(local->hand_unit_index)->definition_index ||
		unit->definition_index == object_get(local->origin_unit_index)->definition_index)
	{
		return FALSE;
	}
	name = tag_get_name(unit->definition_index);
	if (name && (strstr(name, "zombie") || strstr(name, "stubbs") || strstr(name, "\\hand")))
		return FALSE;
	return TRUE;
}

/* the hand's action key: the nearest head in reach taken */
static boolean stubbs_hand_possess(
	short local_player_index)
{
	struct stubbs_local_player *local = &stubbs_local_players[local_player_index];
	struct object_datum *hand = object_get(local->hand_unit_index);
	struct object_iterator iterator;
	struct object_datum *object;
	long best_index = NONE;
	real best_distance = STUBBS_POSSESSION_REACH * STUBBS_POSSESSION_REACH;
	struct unit_datum *human;

	object_iterator_new(&iterator, FLAG(_object_type_biped), 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		real_point3d head;
		real dx, dy, dz, distance;

		if (object->object.type != _object_type_biped || !stubbs_unit_possessable(iterator.index, local))
			continue;
		unit_get_head_position(iterator.index, (void *)&head);
		dx = head.x - hand->object.position.x;
		dy = head.y - hand->object.position.y;
		dz = head.z - hand->object.position.z;
		/* (a standing human's head is above the hand on the floor: the
		height counts for less) */
		distance = dx * dx + dy * dy + 0.25f * dz * dz;
		if (distance < best_distance)
		{
			best_distance = distance;
			best_index = iterator.index;
		}
	}
	if (best_index == NONE)
		return FALSE;

	human = unit_get(best_index);
	if (human->unit.actor_index != NONE)
		actor_delete(human->unit.actor_index, FALSE);
	/* the hand rides on his head */
	players_set_local_player_unit(local_player_index, best_index);
	object_attach_to_marker(best_index, "head", local->hand_unit_index, "");
	local->possessed_unit_index = best_index;
	stubbs_mode_log("the hand took %s", tag_get_name(human->definition_index));
	return TRUE;
}

/* black while a human is taken: the hand comes home, the human drops */
static void stubbs_hand_release(
	short local_player_index)
{
	struct stubbs_local_player *local = &stubbs_local_players[local_player_index];
	long possessed_unit_index = local->possessed_unit_index;

	stubbs_hand_return(local_player_index, TRUE);
	if (stubbs_unit_alive(possessed_unit_index))
		unit_kill(possessed_unit_index);
}

/* ---------- public code */

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
		stubbs_engine_forget_players();
	}
	stubbs_unit_definitions_noted = TRUE;
	count = stubbs_read_long(unit + STUBBS_UNIT_SPECIAL_ATTACKS_OFFSET);
	address = (unsigned long)stubbs_read_long(unit + STUBBS_UNIT_SPECIAL_ATTACKS_OFFSET + 4);
	if (count <= 0 || stubbs_unit_definition_count >= MAXIMUM_STUBBS_UNIT_DEFINITIONS)
		return;
	if (count > MAXIMUM_STUBBS_SPECIAL_ATTACKS)
		count = MAXIMUM_STUBBS_SPECIAL_ATTACKS;
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
	boolean hand_pressed, action_pressed;

	/* (the next map's tags noted afresh) */
	stubbs_unit_definitions_noted = FALSE;
	if (local_player_index < 0 || local_player_index >= MAXIMUM_STUBBS_LOCAL_PLAYERS)
		return;
	local = &stubbs_local_players[local_player_index];
	hand_pressed = (*player_control_flags & hand_button) && !local->hand_button_down;
	local->hand_button_down = (*player_control_flags & hand_button) != 0;
	*player_control_flags &= ~hand_button;
	action_pressed = TEST_FLAG(unit_control_flags, _unit_control_action_bit) && !local->action_button_down;
	local->action_button_down = TEST_FLAG(unit_control_flags, _unit_control_action_bit);

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
	if (local->possessed_unit_index != NONE)
	{
		if (hand_pressed)
			stubbs_hand_release(local_player_index);
		return;
	}
	if (unit_index != local->hand_unit_index)
		return;
	if (hand_pressed)
		stubbs_hand_return(local_player_index, TRUE);
	else if (action_pressed)
		stubbs_hand_possess(local_player_index);
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

	if (special_attack < 0 || special_attack >= MAXIMUM_STUBBS_SPECIAL_ATTACKS)
		return 0;
	/* (until a script sets them, each special attack is ready: one use) */
	return count ? count->counts[special_attack] : 1;
}

void stubbs_engine_special_attack_set_count(
	long unit_index,
	short special_attack,
	short count)
{
	struct stubbs_unit_count *counts = stubbs_unit_count_get(unit_index, TRUE);

	if (!counts || special_attack < 0 || special_attack >= MAXIMUM_STUBBS_SPECIAL_ATTACKS)
		return;
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
