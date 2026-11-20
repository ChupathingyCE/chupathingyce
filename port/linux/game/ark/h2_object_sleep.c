/* Halo 2's object sleep, on a Halo 2 level.

Halo 2's tick (the Xbox decompilation, retail 0xb6f10, 0xbc470, 0xb7060,
0xbba20, 0xb7360) updates an object only while a header flag says it is
AWAKE. Its update collects "activity" from the type update (slot 0x40: a
scenery's animation of more than one frame, an item not at rest or at rest
on another object, a weapon's state timers, a machine's device state
changing, a live biped always), the damage update (shields recharging, stun
ticks), the change colours, the animation blending and each child; while a
creation countdown runs (6 ticks when the game is past its sixth, 0 for a
machine) the object stays awake; with the countdown done, no activity and
not WOKEN this tick (0xbba20: something set its velocity, moved it, struck it,
attached to it, its animation runs, its physics body is active) the object
goes to sleep: the pass skips it, and nothing of it is recomputed, until
something wakes it. The node matrices, bounding sphere and lights are
rebuilt only for objects woken this tick.

This engine (Halo CE's lineage) never slept: every active root is updated,
and its matrices rebuilt, every tick. On Cairo Station that is some 80
machines, 70 weapons on racks and 200 scenery props at rest doing nothing
each tick but recomputing what they had.

Kept here, outside the game state (a saved game resumes with every object
awake, as a new one is): an awake/woken pair and the countdown per object
slot, stamped with the slot's identifier. The activity rules are Halo 2's
where this engine's state says the same thing; where it does not (vehicles,
projectiles, units alive, sound scenery, placeholders, objects whose
definition has functions - Halo 2 exports those on demand, this engine
computes them every tick -, scaled change colours, garbage-flagged items,
interpolating objects), the object stays awake: the old behaviour, never
less. A root that was awake when its cluster left the players' PVS is still
awake when it comes back (Halo 2 keeps the flag through deactivation too).
HALO_H2_OBJECT_SLEEP=0: the old behaviour (every root updated). */

#include "cseries.h"
#include "real_math.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "objects/object_types.h"
#include "objects/scenery.h"
#include "models/model_animation_definitions.h"
#include "items/items.h"
#include "items/weapons.h"
#include "units/units.h"
#include "devices/devices.h"
#include "devices/device_definitions.h"
#include "game/game.h"
#include "h2_rules.h"
#include "h2_object_sleep.h"
#include "ptick.h" /* (port) the parallel tick: the slot is tree state */
#include "ragdolls.h"

#include <stdlib.h>
#include <string.h>

enum
{
	_sleep_awake_bit = 0,
	_sleep_woken_bit,
	_sleep_known_bit,

	/* (Halo 2: the countdown at +0xb3 of a new object) */
	SLEEP_CREATION_COUNTDOWN = 6,
	SLEEP_CREATION_COUNTDOWN_FROM_TICK = 6,
	SLEEP_CENSUS_TICKS = 300,
	/* (a dead biped at rest this long with no animation or ragdoll moving
	it: its death animation is over - Halo 2 asks its animation state) */
	SLEEP_CORPSE_REST_TICKS = 30,
};

struct sleep_slot
{
	short identifier;
	byte flags;
	byte countdown;
};

static struct sleep_slot sleep_slots[HALO_PORT_MAXIMUM_OBJECTS_PER_MAP];

static struct
{
	long ticks;
	long roots, awake, asleep;
	long asleep_by_type[NUMBER_OF_OBJECT_TYPES];
	long awake_by_type[NUMBER_OF_OBJECT_TYPES];
	/* what kept a root awake that its type did not */
	long kept_damage, kept_functions, kept_colors, kept_garbage, kept_interpolating, kept_children, kept_countdown, kept_woken;
} sleep_census;

static int sleep_log_enabled = -1;

boolean h2_object_sleep_active(
	void)
{
	extern volatile unsigned long halo_settings_generation;
	static unsigned long settings_seen;
	static int enabled = -1;

	if (enabled < 0 || settings_seen != halo_settings_generation)
	{
		const char *setting = getenv("HALO_H2_OBJECT_SLEEP");

		settings_seen = halo_settings_generation;
		enabled = setting ? atoi(setting) != 0 : 1;
		/* (switched on in a run, @set: every object awake again - while it
		was off nothing noted what moved) */
		if (enabled)
			h2_object_sleep_reset();
	}
	return enabled && h2_rules_level_fast();
}

void h2_object_sleep_reset(
	void)
{
	memset(sleep_slots, 0, sizeof(sleep_slots));
	memset(&sleep_census, 0, sizeof(sleep_census));
}

static struct sleep_slot *sleep_slot_get(
	long object_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	short identifier = DATUM_INDEX_TO_IDENTIFIER(object_index);
	struct sleep_slot *slot;

	if (absolute_index < 0 || absolute_index >= HALO_PORT_MAXIMUM_OBJECTS_PER_MAP)
		return NULL;
	slot = &sleep_slots[absolute_index];
	if (!TEST_FLAG(slot->flags, _sleep_known_bit) || slot->identifier != identifier)
	{
		/* a new object: awake, woken (its first update rebuilds everything),
		and Halo 2's countdown unless it is a machine or the game is young */
		struct object_header_datum *header = object_header_try_and_get(object_index);

		slot->identifier = identifier;
		slot->flags = FLAG(_sleep_known_bit) | FLAG(_sleep_awake_bit) | FLAG(_sleep_woken_bit);
		slot->countdown = (game_time_get() >= SLEEP_CREATION_COUNTDOWN_FROM_TICK &&
			(!header || header->type != _object_type_machine)) ? SLEEP_CREATION_COUNTDOWN : 0;
	}
	return slot;
}

static long sleep_root_of(
	long object_index)
{
	long guard = 0;

	for (;;)
	{
		struct object_datum *object = object_try_and_get(object_index);

		if (!object || object->object.parent_object_index == NONE || ++guard > 64)
			return object_index;
		object_index = object->object.parent_object_index;
	}
}

boolean h2_object_sleep_awake(
	long object_index)
{
	struct sleep_slot *slot = sleep_slot_get(object_index);

	return !slot || TEST_FLAG(slot->flags, _sleep_awake_bit);
}

void h2_object_sleep_begin(
	long object_index)
{
	struct sleep_slot *slot = sleep_slot_get(object_index);

	if (slot)
		SET_FLAG(slot->flags, _sleep_woken_bit, FALSE);
}

void h2_object_sleep_end(
	long object_index,
	boolean active)
{
	struct sleep_slot *slot = sleep_slot_get(object_index);
	struct object_header_datum *header = object_header_try_and_get(object_index);

	if (!slot)
		return;
	sleep_census.roots++;
	sleep_census.awake++;
	if (header && header->type >= 0 && header->type < NUMBER_OF_OBJECT_TYPES)
		sleep_census.awake_by_type[header->type]++;
	if (slot->countdown > 0)
	{
		slot->countdown--;
		sleep_census.kept_countdown++;
		return;
	}
	if (!active)
	{
		/* (HALO_H2_SLEEP_MASK=<hex object type mask>: only those types may
		sleep - to find which sleeping kind moves a test's path) */
		static long mask = -1;

		if (mask < 0)
			mask = getenv("HALO_H2_SLEEP_MASK") ? strtol(getenv("HALO_H2_SLEEP_MASK"), NULL, 16) : 0xffff;
		if (TEST_FLAG(slot->flags, _sleep_woken_bit))
			sleep_census.kept_woken++;
		else if (header && !TEST_FLAG(mask, header->type))
			sleep_census.kept_woken++;
		else
		{
			SET_FLAG(slot->flags, _sleep_awake_bit, FALSE);
			/* (HALO_H2_SLEEP_LOG=2: each unit that falls asleep, with why) */
			if (sleep_log_enabled >= 2 && header && TEST_FLAG(_object_mask_unit, header->type))
			{
				struct object_datum *object = object_get(object_index);

				platform_log("h2 sleep: unit %08lx (%s) asleep at tick %ld: damage flags %04x flags %08lx at rest %d idle %d pose %d parent %08lx",
					(unsigned long)object_index, tag_get_name(object->definition_index), game_time_get(),
					(unsigned)object->object.damage_flags, (unsigned long)object->object.flags,
					TEST_FLAG(object->object.flags, _object_at_rest_bit) ? 1 : 0, (int)object->object.idle_ticks,
					ragdolls_object_has_pose(object_index) ? 1 : 0, (unsigned long)object->object.parent_object_index);
			}
		}
	}
}

void h2_object_sleep_wake(
	long object_index)
{
	struct sleep_slot *slot;
	long root;

	if (object_index == NONE || !h2_object_sleep_active())
		return;
	root = sleep_root_of(object_index);
	/* (ptick.h) the root's slot is its tree's state: a speculating thread may
	wake its own root only (a wake of another is a write into that tree); the
	serial side's wake of a root with a pending speculation drops that
	speculation first (the read hook's lazy flush), as any write into it */
	if (halo_ptick_speculating)
	{
		if (HALO_PTICK_SPECULATING())
		{
			if (!halo_ptick_in_tree(root))
				halo_ptick_guard(_ptick_abort_other, "sleep wake other");
		}
		else
			halo_ptick_read_note(root);
	}
	slot = sleep_slot_get(root);
	if (slot)
		slot->flags |= FLAG(_sleep_awake_bit) | FLAG(_sleep_woken_bit);
}

/* (ptick.c) a root's sleep slot, for the tree's snapshot: NULL when the rule
is off or the object has none */
void *halo_ptick_sleep_slot(long object_index, long *size)
{
	struct sleep_slot *slot;

	*size = 0;
	if (object_index == NONE || !h2_object_sleep_active())
		return NULL;
	slot = sleep_slot_get(object_index);
	if (slot)
		*size = sizeof(*slot);
	return slot;
}

/* (ptick.c) the serial loop's bracket before a root's update, done again
before a speculation's run and before a serial rerun after a restore */
void halo_ptick_sleep_begin(long object_index)
{
	if (object_index != NONE && h2_object_sleep_active())
		h2_object_sleep_begin(object_index);
}

boolean h2_object_sleep_woken(
	long object_index)
{
	struct sleep_slot *slot = sleep_slot_get(sleep_root_of(object_index));

	return !slot || TEST_FLAG(slot->flags, _sleep_woken_bit);
}

void h2_object_sleep_note_asleep(
	long object_index,
	short type)
{
	(void)object_index;
	sleep_census.roots++;
	sleep_census.asleep++;
	if (type >= 0 && type < NUMBER_OF_OBJECT_TYPES)
		sleep_census.asleep_by_type[type]++;
}

void h2_object_sleep_type_before(
	long object_index,
	short type,
	struct h2_object_sleep_snapshot *snapshot)
{
	snapshot->position = snapshot->power = 0.f;
	if (TEST_FLAG(_object_mask_device, type))
	{
		struct device_datum *device = (struct device_datum *)object_get_and_verify_type(object_index, _object_mask_device);

		snapshot->position = device->device.position;
		snapshot->power = device->device.power;
	}
}

/* Halo 2's weapon update (retail 0xfdfb0) stays active while any state
timer, trigger, magazine or barrel is doing something */
static boolean sleep_weapon_busy(
	struct weapon_datum const *weapon)
{
	short i;

	if (weapon->weapon.state != _weapon_state_idle || weapon->weapon.state_timer != 0 ||
		weapon->weapon.heat > 0.f || weapon->weapon.overcharged > 0.f ||
		weapon->weapon.integrated_light_delay_ticks != 0 || weapon->weapon.recoil_recovery_time != 0 ||
		weapon->weapon.recoil_angular_velocity != 0.f || weapon->weapon.primary_trigger != 0.f)
		return TRUE;
	for (i = 0; i < 2; i++)
	{
		/* (idle_ticks counts up to 127 since the last shot and stays: not activity) */
		if (weapon->weapon.triggers[i].state != 0 || weapon->weapon.triggers[i].state_timer != 0 ||
			weapon->weapon.magazines[i].state != 0 || weapon->weapon.magazines[i].state_timer != 0)
			return TRUE;
	}
	return FALSE;
}

boolean ragdolls_object_awake(long object_index);

static boolean sleep_corpse_rule(
	void)
{
	extern volatile unsigned long halo_settings_generation;
	static unsigned long settings_seen;
	static int enabled = -1;

	if (enabled < 0 || settings_seen != halo_settings_generation)
	{
		char const *setting = getenv("HALO_H2_CORPSE_SLEEP");
		int was = enabled;

		settings_seen = halo_settings_generation;
		enabled = setting ? atoi(setting) != 0 : 1;
		/* (switched off in a run, @set: every object awake again, the
		corpses asleep on a kept pose among them) */
		if (was == 1 && !enabled)
			h2_object_sleep_reset();
	}
	return enabled;
}

boolean h2_object_sleep_type_activity(
	long object_index,
	short type,
	struct h2_object_sleep_snapshot const *snapshot)
{
	struct object_datum *object = object_get(object_index);

	switch (type)
	{
	case _object_type_biped:
		/* 0xdd360: a live biped is woken every tick; a dead one while its
		animation runs (0x113e40) or its body moves - its havok bodies active
		(0xbc5e0: physics_disabled clear) = here its ragdoll awake; a kept
		ragdoll asleep sleeps with it (HALO_H2_CORPSE_SLEEP=0: awake while it
		has any ragdoll pose, as before) */
		if (!TEST_FLAG(object->object.damage_flags, _object_dead_bit) ||
			!TEST_FLAG(object->object.flags, _object_at_rest_bit) ||
			object->object.idle_ticks < SLEEP_CORPSE_REST_TICKS ||
			(sleep_corpse_rule() ? ragdolls_object_awake(object_index) : ragdolls_object_has_pose(object_index)))
		{
			h2_object_sleep_wake(object_index);
			return TRUE;
		}
		return FALSE;

	case _object_type_weapon:
	case _object_type_equipment:
	case _object_type_garbage:
		{
			struct item_datum *item = (struct item_datum *)object;

			/* 0x10b4d0: an item not at rest is woken every tick; at rest on
			another object it stays awake (watching that object) */
			if (!TEST_FLAG(object->object.flags, _object_at_rest_bit))
			{
				h2_object_sleep_wake(object_index);
				return TRUE;
			}
			if (item->item.detonation_ticks > 0 ||
				TEST_FLAG(item->item.flags, _item_on_object_bit) ||
				TEST_FLAG(item->item.flags, _item_attached_to_unit_bit))
				return TRUE;
			if (type == _object_type_weapon && sleep_weapon_busy((struct weapon_datum *)object))
				return TRUE;
			if (type == _object_type_garbage)
				return TRUE;
			return FALSE;
		}

	case _object_type_scenery:
		{
			struct scenery_datum *scenery = (struct scenery_datum *)object;

			/* 0x10a2f0: animating with more than one frame; (a Halo 2 prop
			whose physics body moves, ragdolls_props.c) */
			if (TEST_FLAG(scenery->flags, 0))
			{
				boolean animating = TRUE;

				if (object->object.animation.animation_graph_index != NONE)
				{
					struct animation_graph *graph = animation_graph_definition_get(object->object.animation.animation_graph_index);
					short index = object->object.animation.state.index;

					if (graph && index >= 0 && index < graph->animations.count)
						animating = TAG_BLOCK_GET_ELEMENT(&graph->animations, index, struct animation)->frame_count > 1;
				}
				if (animating)
				{
					h2_object_sleep_wake(object_index);
					return TRUE;
				}
			}
			if (ragdolls_object_has_pose(object_index))
			{
				h2_object_sleep_wake(object_index);
				return TRUE;
			}
			return FALSE;
		}

	case _object_type_machine:
	case _object_type_control:
	case _object_type_light_fixture:
		{
			struct device_datum *device = (struct device_datum *)object;

			/* 0x1152e0: moved -> woken; the device state changing -> active */
			if (device->device.position != snapshot->position || device->device.power != snapshot->power)
			{
				h2_object_sleep_wake(object_index);
				return TRUE;
			}
			if (device->device.position_velocity != 0.f || device->device.power_velocity != 0.f)
				return TRUE;
			/* 0x1152e0: a door machine operating automatically is active every
			update (it looks for units in its sphere every fourth tick), and one
			fully open that closes by itself is active while it counts its open
			time - a sleeping door never opened for a player at it (06a qz_door) */
			if (type == _object_type_machine)
			{
				struct machine_datum *machine = (struct machine_datum *)object;
				struct machine_definition *definition = machine_definition_get(object->definition_index);

				if (definition->machine.type == 0 &&
					(!TEST_FLAG(machine->machine.flags, _machine_does_not_operate_automatically_bit) ||
						(device->device.position == 1.f && !TEST_FLAG(machine->machine.flags, _machine_does_not_close_automatically_bit))))
					return TRUE;
			}
			/* (its group's value not reached yet: the next update moves it) */
			if (device->device.position_group_index != NONE &&
				device_group_get_value(device->device.position_group_index) != device->device.position)
				return TRUE;
			if (device->device.power_group_index != NONE &&
				device_group_get_value(device->device.power_group_index) != device->device.power)
				return TRUE;
			return FALSE;
		}

	case _object_type_vehicle:
	case _object_type_projectile:
	case _object_type_placeholder:
	case _object_type_sound_scenery:
	default:
		/* (not decided from this engine's state: awake, as before) */
		h2_object_sleep_wake(object_index);
		return TRUE;
	}
}

boolean h2_object_sleep_object_activity(
	long object_index)
{
	struct object_datum *object = object_get(object_index);
	struct object_definition *definition = object_definition_get(object->definition_index);
	boolean active = FALSE;

	/* the damage update (0xd5de0): shields charging, draining or stunned */
	if (object->object.maximum_shield_vitality > 0.f &&
		(object->object.shield_vitality != 1.f || object->object.shield_stun_ticks != 0 ||
			TEST_FLAG(object->object.damage_flags, _object_shield_charging_bit) ||
			TEST_FLAG(object->object.damage_flags, _object_shield_over_charging_bit)))
	{
		sleep_census.kept_damage++;
		active = TRUE;
	}
	if ((object->object.damage_flags & (FLAG(_object_die_act_of_god_bit) | FLAG(_object_die_act_of_god_silent_bit) |
			FLAG(_object_die_act_of_god_no_statistics_bit))) ||
		object->object.shield_damage_decay_timer != NONE || object->object.body_damage_decay_timer != NONE)
	{
		sleep_census.kept_damage++;
		active = TRUE;
	}
	/* (its object functions, this engine's time-driven values Halo 2 exports
	on demand, and the change colours they scale are kept current while it
	sleeps: objects_asleep_functions_update, objects.c) */
	(void)definition;
	/* (a garbage item sleeps as any other; the tick's pass still counts it
	among the active garbage objects, objects.c) */
	if (object->object.animation.interpolation_frame_count != 0)
	{
		sleep_census.kept_interpolating++;
		active = TRUE;
	}
	return active;
}

void h2_object_sleep_census(
	void)
{
	/* a tick that does not follow the last one (a revert, a saved game
	loaded, a new map): the game state came from elsewhere and the slots
	here did not - every object awake again, as Halo 2's flags would say
	after it restores them with the rest */
	static long last_game_time = NONE;
	long game_time = game_time_get();

	if (last_game_time == NONE || game_time != last_game_time + 1)
		h2_object_sleep_reset();
	last_game_time = game_time;

	if (sleep_log_enabled < 0)
		sleep_log_enabled = getenv("HALO_H2_SLEEP_LOG") ? atoi(getenv("HALO_H2_SLEEP_LOG")) : 0;
	if (!sleep_log_enabled)
		return;
	if (++sleep_census.ticks >= SLEEP_CENSUS_TICKS)
	{
		static const char *names[NUMBER_OF_OBJECT_TYPES] = {"biped", "vehicle", "weapon", "equipment", "garbage",
			"projectile", "scenery", "machine", "control", "light_fixture", "placeholder", "sound_scenery"};
		char line[1024];
		int n = 0, t;
		real ticks = (real)sleep_census.ticks;

		for (t = 0; t < NUMBER_OF_OBJECT_TYPES; t++)
		{
			if (sleep_census.awake_by_type[t] || sleep_census.asleep_by_type[t])
				n += snprintf(line + n, sizeof(line) - n, " %s %.1f/%.1f", names[t],
					sleep_census.awake_by_type[t] / ticks, sleep_census.asleep_by_type[t] / ticks);
		}
		platform_log("h2 sleep census (%ld ticks, tick %ld): roots %.1f, awake %.1f, asleep %.1f; awake/asleep by type:%s; kept by countdown %.1f woken %.1f damage %.1f functions %.1f colors %.1f garbage %.1f interp %.1f",
			sleep_census.ticks, game_time_get(), sleep_census.roots / ticks, sleep_census.awake / ticks,
			sleep_census.asleep / ticks, line, sleep_census.kept_countdown / ticks, sleep_census.kept_woken / ticks,
			sleep_census.kept_damage / ticks, sleep_census.kept_functions / ticks, sleep_census.kept_colors / ticks,
			sleep_census.kept_garbage / ticks, sleep_census.kept_interpolating / ticks);
		memset(&sleep_census, 0, sizeof(sleep_census));
	}
}
