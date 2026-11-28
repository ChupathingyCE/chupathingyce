/*
H2_VEHICLE_BOOST.C

port: Halo 2's vehicles' boost on a Halo 2 level. Halo 2's vehicle tag (vehi
0x1D0..0x1E0, its unit's 'has boost' flag) gives a boost: peak power (the
Ghost 1.65: its speed and push that many times), rise power (0.1: seconds to
rise to it), peak time (300000: as long as it is held), fall power (0.2:
seconds to fall back) and dead time (0.1: seconds after a boost before the
next), read with its force model's values (h2_vehicle_physics.h), and its
sound, its looping sound attachments scaled by its boost (the Ghost's
ghost_boost_left and _right: each track's in as it starts, then its loop
again and again while it lasts, on the vehicle). Held with the throttle
forward, the vehicle boosts. The boost
button, while driving a Ghost:
- PC keyboard: Left Shift (Halo 2's PC game's), or Left Ctrl / C (crouch);
- a gamepad: the left trigger (Halo 2's and Halo 3's on the Xbox; a driver
  throws no grenade: player_control.c) or the left stick click (crouch);
- the Vita: L (the left trigger as shipped) or D-pad down (crouch) - held,
  the crouch toggle let go while driving one (vita_pad.c).
Its speed: vehicles.c's speed
seek reaches peak power times its maximum speed, faster; while it boosts the
vehicle cannot fire (Halo 2's). The state is the game's (each unit update,
from the controls every machine has), so a network game's machines agree.
h2_vehicle_boost_value (0 none .. 1 full) is the vehicle's C in (vehicles.c's
mode 13): its glow panels' fade and Halo 2's boost attachments - the plasma
vent's flash and contrail, h2_effects.c - scale by it. HALO_H2_NO_BOOST: none.
*/

#include "cseries.h"
#include "render_epoch.h" /* HALO_ENV_ONCE (perf/grunt-tick-2: the switches read again only when the settings change) */
#include "game/game.h"
#include "game/players.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"
#include "sound/sound_manager.h"
#include "h2_sound.h"
#include "h2_vehicle_physics.h"

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	H2VB_MAXIMUM_VEHICLES = 64,
	H2VB_MAXIMUM_KINDS = 16
};

/* ---------- structures */

struct h2vb_kind
{
	long definition_index;      /* the vehicle's definition */
	boolean boosts;
	real peak_power, rise_seconds, peak_seconds, fall_seconds, dead_seconds;
	/* (its boost's sound: Halo 2's looping sound attachments scaled by its
	boost - the Ghost's ghost_boost_left and _right -, each its track's
	folder: its in and loop, "" none) */
	char sounds[2][96];
};

/* (without Halo 2's force model's values - HALO_H2_STOCK_VEHICLE_PHYSICS -
the Ghost's, its vehi 0x1D0..0x1E0: objects\vehicles\ghost\ghost) */
static struct h2vb_kind const h2vb_ghost =
{
	NONE, TRUE, 1.65f, 0.1f, 300000.0f, 0.2f, 0.1f,
	{ "vehicles\\ghost\\ghost_boost_left\\track1\\", "vehicles\\ghost\\ghost_boost_right\\track1\\" },
};

/* ---------- globals */

static struct
{
	long scenario;
	boolean halo2;
	struct
	{
		long vehicle;
		short kind;
		real power;             /* 0 none .. 1 peak */
		long held_ticks;        /* how long it has boosted */
		long dead_until;        /* (game time) no boost before */
		/* (its boost's sound: playing, the left and right sounds, when
		the next loop starts - game time) */
		boolean sounding;
		long sounds[2];
		long sound_next;
	} vehicles[H2VB_MAXIMUM_VEHICLES];
	/* (each vehicle's hatch: open 0..1, h2vb_hatch_update) */
	struct
	{
		long vehicle;
		real open;
	} hatches[H2VB_MAXIMUM_VEHICLES];
	/* (each vehicle definition's boost, read as it is first seen: Halo 2's
	vehi 0x1D0..0x1E0, through its force model's values,
	h2_vehicle_physics.h) */
	long kind_count;
	struct h2vb_kind kinds[H2VB_MAXIMUM_KINDS];
	volatile int local_driving;     /* (the input's: the local player drives a boosting vehicle) */
	/* the Warthog's "jump" input: ticks it has been braked (h2_vehicle_tail_value) */
	struct { long vehicle; long ticks; } brakes[H2VB_MAXIMUM_VEHICLES];
	/* game_can_use_flashlights: flashlights_allowed's flag, and when it was set */
	boolean flashlights;
	long flashlights_time;
	/* (halo_map_generation when the level's values were taken) */
	unsigned long generation;
} h2vb = { NONE };

extern unsigned long halo_map_generation;

/* ---------- private code */

/* (perf/ce-floor-2) h2vb_level's answer and what it was found for, read by
unit_update before its per-unit h2_vehicle_boost_update call (units.c) */
long h2_vehicle_boost_seen_scenario = NONE;
unsigned long h2_vehicle_boost_seen_generation = ~0UL;
boolean h2_vehicle_boost_halo2;

static void h2vb_level(void)
{
	char const *name;
	long i;

	/* (the map's generation too: consecutive maps' scenarios can have the
	same tag index - burial_mounds and coagulation, one after the other, did
	-, and the definitions and vehicles kept then point into the map gone,
	a vehicle of the same object index finding its slot with the last map's
	definition: a segfault in h2vp_antigrav) */
	if (h2vb.scenario == global_scenario_index && h2vb.generation == halo_map_generation)
		return;
	h2vb.scenario = global_scenario_index;
	h2vb.generation = halo_map_generation;
	name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : "";
	h2vb.halo2 = name && !strncmp(name, "h2\\", 3) && !HALO_ENV_ONCE("HALO_H2_NO_BOOST");
	h2_vehicle_boost_seen_scenario = h2vb.scenario;
	h2_vehicle_boost_seen_generation = h2vb.generation;
	h2_vehicle_boost_halo2 = h2vb.halo2;
	for (i = 0; i < H2VB_MAXIMUM_VEHICLES; i++)
	{
		h2vb.vehicles[i].vehicle = NONE;
		h2vb.hatches[i].vehicle = NONE;
		h2vb.brakes[i].vehicle = NONE;
	}
	h2vb.kind_count = 0;
	h2vb.flashlights = FALSE;
	h2vb.flashlights_time = 0;
}

static void h2vb_hatch_update(long unit_index, struct unit_datum *unit);

static short h2vb_kind_of(long definition_index)
{
	struct h2vp_definition const *definition;
	struct h2vb_kind *kind;
	short k;

	for (k = 0; k < (short)h2vb.kind_count; k++)
	{
		if (h2vb.kinds[k].definition_index == definition_index)
			return h2vb.kinds[k].boosts ? k : NONE;
	}
	if (h2vb.kind_count >= H2VB_MAXIMUM_KINDS)
		return NONE;
	kind = &h2vb.kinds[h2vb.kind_count];
	csmemset(kind, 0, sizeof(*kind));
	definition = h2_vehicle_physics_definition(definition_index);
	if (definition && definition->boost_peak_power > 1.0f)
	{
		kind->boosts = TRUE;
		kind->peak_power = definition->boost_peak_power;
		kind->rise_seconds = definition->boost_rise_power;
		kind->peak_seconds = definition->boost_peak_time;
		kind->fall_seconds = definition->boost_fall_power;
		kind->dead_seconds = definition->boost_dead_time;
		memcpy(kind->sounds, definition->boost_sounds, sizeof(kind->sounds));
		kind->sounds[0][sizeof(kind->sounds[0]) - 1] = kind->sounds[1][sizeof(kind->sounds[1]) - 1] = 0;
	}
	else if (!definition && !strcmp(tag_get_name(definition_index), "h2\\vehicles\\ghost"))
		*kind = h2vb_ghost;
	kind->definition_index = definition_index;
	return kind->boosts ? (short)h2vb.kind_count++ : (h2vb.kind_count++, NONE);
}

/* the vehicle's slot; NONE if it has no boost (made when create) */
static long h2vb_slot(long vehicle_index, boolean create)
{
	struct object_datum *object = object_try_and_get(vehicle_index);
	long i, free_slot = NONE;
	short kind;

	if (!object)
		return NONE;
	for (i = 0; i < H2VB_MAXIMUM_VEHICLES; i++)
	{
		if (h2vb.vehicles[i].vehicle == vehicle_index)
			return i;
		if (free_slot == NONE && (h2vb.vehicles[i].vehicle == NONE || !object_try_and_get(h2vb.vehicles[i].vehicle)))
			free_slot = i;
	}
	if (!create || free_slot == NONE || (kind = h2vb_kind_of(object->definition_index)) == NONE)
		return NONE;
	csmemset(&h2vb.vehicles[free_slot], 0, sizeof(h2vb.vehicles[free_slot]));
	h2vb.vehicles[free_slot].vehicle = vehicle_index;
	h2vb.vehicles[free_slot].kind = kind;
	h2vb.vehicles[free_slot].dead_until = NONE;
	h2vb.vehicles[free_slot].sounds[0] = h2vb.vehicles[free_slot].sounds[1] = NONE;
	return free_slot;
}

/* the boost's sound (Halo 2's looping sounds scaled by the boost: started
as it begins, its "in" then its "loop" again and again, stopped as it ends)
of a vehicle's slot; boosting whether it boosts now */
static void h2vb_sound_update(long slot, long vehicle_index, boolean boosting)
{
	struct h2vb_kind const *kind = &h2vb.kinds[h2vb.vehicles[slot].kind];
	long now = game_time_get();
	int side;

	if (HALO_ENV_ONCE("HALO_H2_NO_BOOST_SOUND"))
		return;
	if (!boosting)
	{
		if (h2vb.vehicles[slot].sounding)
		{
			for (side = 0; side < 2; side++)
			{
				if (h2vb.vehicles[slot].sounds[side] != NONE)
					sound_stop_impulse(h2vb.vehicles[slot].sounds[side]);
				h2vb.vehicles[slot].sounds[side] = NONE;
			}
			h2vb.vehicles[slot].sounding = FALSE;
		}
		return;
	}
	if (h2vb.vehicles[slot].sounding && now < h2vb.vehicles[slot].sound_next)
		return;
	{
		char const *part = h2vb.vehicles[slot].sounding ? "loop" : "in";
		real longest = 0.0f;

		for (side = 0; side < 2; side++)
		{
			char name[160];
			real seconds = 0.0f;

			h2vb.vehicles[slot].sounds[side] = NONE;
			if (!kind->sounds[side][0])
				continue;
			snprintf(name, sizeof(name), "%s%s", kind->sounds[side], part);
			h2vb.vehicles[slot].sounds[side] = h2_sound_play_on_object(name, vehicle_index, &seconds);
			if (seconds > longest)
				longest = seconds;
		}
		if (HALO_ENV_ONCE("HALO_H2_BOOST_LOG"))
			platform_log("h2 boost: %08lx sound %s%s: %08lx %08lx, %.2f s", (unsigned long)vehicle_index, kind->sounds[0], part,
				(unsigned long)h2vb.vehicles[slot].sounds[0], (unsigned long)h2vb.vehicles[slot].sounds[1], longest);
		h2vb.vehicles[slot].sounding = TRUE;
		/* (the next loop as this one ends; none heard: tried again soon) */
		h2vb.vehicles[slot].sound_next = now + (longest > 0.1f ? (long)(longest * TICKS_PER_SECOND) - 1 : 15);
	}
}

/* ---------- public code */

/* the ticks a Warthog has been braked, vehicles.cpp's function_f5bf0: its
handbrake (the jump button) held, or at over 1 wu/s its throttle against the way
it moves. A vehicle with neither has no slot */
static void h2vb_brake_update(long unit_index, struct unit_datum *unit)
{
	boolean braking = TEST_FLAG(unit->unit.control_flags, _unit_control_jump_bit);
	long i, slot = NONE;

	if (!braking && unit->unit.throttle.i != 0.0f && 30.0f * magnitude3d(&unit->object.translational_velocity) > 1.0f)
	{
		real along = dot_product3d(&unit->object.translational_velocity, &unit->object.forward);

		braking = (unit->unit.throttle.i < 0.0f && along > 0.0f) || (unit->unit.throttle.i > 0.0f && along < 0.0f);
	}
	for (i = 0; i < H2VB_MAXIMUM_VEHICLES && slot == NONE; i++)
	{
		if (h2vb.brakes[i].vehicle == unit_index)
			slot = i;
	}
	if (!braking)
	{
		if (slot != NONE)
			h2vb.brakes[slot].vehicle = NONE;
		return;
	}
	for (i = 0; i < H2VB_MAXIMUM_VEHICLES && slot == NONE; i++)
	{
		if (h2vb.brakes[i].vehicle == NONE)
		{
			slot = i;
			h2vb.brakes[i].vehicle = unit_index;
			h2vb.brakes[i].ticks = 0;
		}
	}
	if (slot != NONE && h2vb.brakes[slot].ticks < 0xFE)
		h2vb.brakes[slot].ticks++;
}

/* (units.c, each unit's update) a boosting vehicle's power from its
driver's controls; its triggers let go while it boosts */
void h2_vehicle_boost_update(long unit_index)
{
	struct unit_datum *unit;
	struct h2vb_kind const *kind;
	long slot, now = game_time_get();
	boolean held;
	real step;

	h2vb_level();
	if (!h2vb.halo2)
		return;
	unit = (struct unit_datum *)object_try_and_get(unit_index);
	if (unit && unit->object.type == _object_type_vehicle)
	{
		h2vb_hatch_update(unit_index, unit);
		h2vb_brake_update(unit_index, unit);
	}
	if (!unit || unit->object.type != _object_type_vehicle || (slot = h2vb_slot(unit_index, TRUE)) == NONE)
		return;
	kind = &h2vb.kinds[h2vb.vehicles[slot].kind];
	held = unit->unit.driver_object_index != NONE && TEST_FLAG(unit->unit.control_flags, _unit_control_crouch_modifier_bit) &&
		unit->unit.throttle.i > 0.1f && unit->object.body_vitality > 0.0f &&
		(h2vb.vehicles[slot].dead_until == NONE || now >= h2vb.vehicles[slot].dead_until) &&
		h2vb.vehicles[slot].held_ticks < (long)(kind->peak_seconds * TICKS_PER_SECOND);
	if (held)
	{
		step = kind->rise_seconds > 0.0f ? 1.0f / (kind->rise_seconds * TICKS_PER_SECOND) : 1.0f;
		h2vb.vehicles[slot].power = MIN(1.0f, h2vb.vehicles[slot].power + step);
		h2vb.vehicles[slot].held_ticks++;
	}
	else
	{
		/* (let go: falling, then its dead time) */
		if (h2vb.vehicles[slot].held_ticks > 0)
			h2vb.vehicles[slot].dead_until = now + (long)(kind->dead_seconds * TICKS_PER_SECOND + 0.5f);
		h2vb.vehicles[slot].held_ticks = 0;
		step = kind->fall_seconds > 0.0f ? 1.0f / (kind->fall_seconds * TICKS_PER_SECOND) : 1.0f;
		h2vb.vehicles[slot].power = MAX(0.0f, h2vb.vehicles[slot].power - step);
	}
	h2vb_sound_update(slot, unit_index, held);
	/* (no firing while it boosts) */
	if (h2vb.vehicles[slot].power > 0.0f)
	{
		SET_FLAG(unit->unit.control_flags, _unit_control_weapon_primary_trigger_bit, FALSE);
		SET_FLAG(unit->unit.control_flags, _unit_control_weapon_secondary_trigger_bit, FALSE);
	}
	if (HALO_ENV_ONCE("HALO_H2_BOOST_LOG") && (now % 15) == 0 && unit->unit.driver_object_index != NONE)
		platform_log("h2 boost: %08lx power %.2f speed %.4f (a tick's), controls %08lx throttle %.2f, driver's %08lx",
			(unsigned long)unit_index, h2vb.vehicles[slot].power, magnitude3d(&unit->object.translational_velocity),
			(unsigned long)unit->unit.control_flags, unit->unit.throttle.i,
			object_try_and_get(unit->unit.driver_object_index) ?
			(unsigned long)((struct unit_datum *)object_get(unit->unit.driver_object_index))->unit.control_flags : 0UL);
}

/* (vehicles.c's speed seek) how many times its maximum speed and push the
vehicle has: 1 not boosting, the peak power at full boost */
real h2_vehicle_boost_scale(long vehicle_index)
{
	long slot;

	if (!h2vb.halo2 || h2vb.scenario != global_scenario_index || (slot = h2vb_slot(vehicle_index, FALSE)) == NONE)
		return 1.0f;
	return 1.0f + (h2vb.kinds[h2vb.vehicles[slot].kind].peak_power - 1.0f) * h2vb.vehicles[slot].power;
}

/* (vehicles.c, player_control.c) whether the vehicle boosts (its driver's
crouch its boost: not its hover lowered; the Xbox's crouch at full stick
ignored, not this one's) */
boolean h2_vehicle_boost_capable(long vehicle_index)
{
	struct object_datum *object;

	h2vb_level();
	return h2vb.halo2 && vehicle_index != NONE && (object = object_try_and_get(vehicle_index)) != NULL &&
		object->object.type == _object_type_vehicle && h2vb_kind_of(object->definition_index) != NONE;
}

/* (player_control.c) whether the unit's crouch - its left trigger too - is
a button of the vehicle it drives: a boost (the Ghost's), or an e-brake
(the Warthog's, Halo 2's vehicle flag 17; its driver's alone - a
passenger's left trigger throws its grenade) */
boolean h2_vehicle_crouch_button(long vehicle_index, long unit_index)
{
	struct object_datum *object;
	struct h2vp_definition const *definition;

	if (h2_vehicle_boost_capable(vehicle_index))
		return TRUE;
	if (!h2vb.halo2 || vehicle_index == NONE || (object = object_try_and_get(vehicle_index)) == NULL ||
		object->object.type != _object_type_vehicle || ((struct unit_datum *)object)->unit.driver_object_index != unit_index)
	{
		return FALSE;
	}
	definition = h2_vehicle_physics_definition(object->definition_index);
	return definition && TEST_FLAG(definition->vehicle_flags, 17);
}

/* a vehicle's hatch slot (NONE: none; made when create) */
static long h2vb_hatch_slot(long vehicle_index, boolean create)
{
	long i, free_slot = NONE;

	for (i = 0; i < H2VB_MAXIMUM_VEHICLES; i++)
	{
		if (h2vb.hatches[i].vehicle == vehicle_index)
			return i;
		if (free_slot == NONE && (h2vb.hatches[i].vehicle == NONE || !object_try_and_get(h2vb.hatches[i].vehicle)))
			free_slot = i;
	}
	if (!create || free_slot == NONE)
		return NONE;
	h2vb.hatches[free_slot].vehicle = vehicle_index;
	h2vb.hatches[free_slot].open = 0.0f;
	return free_slot;
}

/* (each vehicle's update) its hatch or canopy: Halo 2 plays "combat:opening"
as a rider gets in or out and "combat:closing" after, each held at its last
frame. The overlay is the one of the two that leaves the rest pose
(h2_vehicles.c h2v_hatch_make), its value toward 1 over that one's length,
back to 0 over the other's. has_hatch 1 (the Scorpion's, the Spectre's: shut
at rest): toward 1 while any rider is entering or leaving its seat. has_hatch
2 (the Banshee's canopy: open at rest): toward 1 - shut - once a rider is
seated, back open while one gets in or out and while it is empty */
static void h2vb_hatch_update(long unit_index, struct unit_datum *unit)
{
	struct h2vp_definition const *definition = h2_vehicle_physics_definition(unit->definition_index);
	long rider, slot;
	boolean opening = FALSE, seated = FALSE;
	real rate;

	if (!definition || !definition->has_hatch || (slot = h2vb_hatch_slot(unit_index, TRUE)) == NONE)
		return;
	for (rider = unit->object.first_child_object_index; rider != NONE; )
	{
		struct object_datum *child = object_get(rider);

		if (TEST_FLAG(_object_mask_unit, child->object.type))
		{
			struct unit_datum *child_unit = (struct unit_datum *)child;

			if (child_unit->unit.animation.state == _unit_state_entering_seat || child_unit->unit.animation.state == _unit_state_exiting_seat)
				opening = TRUE;
			else
				seated = TRUE;
		}
		rider = child->object.next_object_index;
	}
	/* (the canopy's: "toward 1" is shut, when someone sits and no one gets in or out) */
	if (definition->has_hatch == 2)
		opening = seated && !opening;
	rate = definition->hatch_seconds[opening ? 0 : 1];
	rate = rate > 0.01f ? 1.0f / (rate * TICKS_PER_SECOND) : 1.0f;
	{
		real was = h2vb.hatches[slot].open;

		h2vb.hatches[slot].open = opening ? MIN(1.0f, was + rate) : MAX(0.0f, was - rate);
		if (h2vb.hatches[slot].open != was && HALO_ENV_ONCE("HALO_H2_VEHICLE_LOG"))
			platform_log("h2 hatch: vehicle %08lx %s %.2f", (unsigned long)unit_index, (definition->has_hatch == 2) == opening ? "closing" : "opening",
				h2vb.hatches[slot].open);
	}
}

/* (vehicles.c, its hatch function) how open the vehicle's hatch is */
real h2_vehicle_hatch_value(long vehicle_index)
{
	long slot;

	if (!h2vb.halo2 || h2vb.scenario != global_scenario_index || (slot = h2vb_hatch_slot(vehicle_index, FALSE)) == NONE)
		return 0.0f;
	return h2vb.hatches[slot].open;
}

/* (player_control.c) whether the unit rides a vehicle that boosts now */
real h2_vehicle_boost_value(long vehicle_index);
boolean h2_vehicle_rider_boosting(long unit_index)
{
	struct object_datum *unit = object_try_and_get(unit_index);

	return h2vb.halo2 && unit && unit->object.parent_object_index != NONE &&
		h2_vehicle_boost_value(unit->object.parent_object_index) > 0.0f;
}

/* (player_control.c) whether the unit is the gunner of a vehicle whose
gun's second trigger is on the melee button (the Banshee's fuel rod bomb:
force model values' secondary_on_melee) */
boolean h2_vehicle_secondary_on_melee(long unit_index)
{
	struct object_datum *unit = object_try_and_get(unit_index), *parent;
	struct h2vp_definition const *definition;

	h2vb_level();
	if (!h2vb.halo2 || !unit || unit->object.parent_object_index == NONE ||
		(parent = object_try_and_get(unit->object.parent_object_index)) == NULL || parent->object.type != _object_type_vehicle ||
		((struct unit_datum *)parent)->unit.gunner_object_index != unit_index)
	{
		return FALSE;
	}
	definition = h2_vehicle_physics_definition(parent->definition_index);
	return definition && definition->secondary_on_melee;
}

/* (effects, sounds) the vehicle's boost: 0 none .. 1 full */
/* (render_objects.c) the function values a vehicle's model is drawn with: its
own, but for a vehicle with a shader lit by its turret's illumination (the
Wraith's mortar_charge_illum, h2_level.c's fade source 2) B is the
illumination of the gun in it. The stand-in's B out is its engine's, read by
its sound: so a copy for the drawing, kept until transparent geometry is
drawn at the end of the frame */
real const *h2_vehicle_render_values(long object_index, real const *values)
{
	struct object_datum *object = object_get(object_index);
	static long wraith = NONE, scenario = NONE;
	static unsigned long generation;
	static real copies[32][NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS];
	static unsigned long next;
	struct unit_datum *unit;
	long weapon_index;
	real *copy;
	extern real weapon_illumination_get(long weapon_index);

	if (object->object.type != _object_type_vehicle)
		return values;
	h2vb_level();
	if (!h2vb.halo2)
		return values;
	if (scenario != global_scenario_index || generation != halo_map_generation)
	{
		scenario = global_scenario_index;
		generation = halo_map_generation;
		wraith = tag_loaded('vehi', "h2\\vehicles\\wraith");
	}
	if (object->definition_index != wraith)
		return values;
	unit = (struct unit_datum *)object;
	weapon_index = unit->unit.current_weapon_index != NONE ? unit->unit.weapon_object_indices[unit->unit.current_weapon_index] : NONE;
	copy = copies[next++ % 32];
	memcpy(copy, values, sizeof(copies[0]));
	copy[1] = weapon_index != NONE ? weapon_illumination_get(weapon_index) : 0.f;
	return copy;
}

real h2_vehicle_boost_value(long vehicle_index)
{
	long slot;

	/* (a test's: HALO_H2_BOOST_SHOW 0..1 the glow's power whatever the driver does) */
	if (HALO_ENV_ONCE("HALO_H2_BOOST_SHOW"))
		return (real)atof(HALO_ENV_ONCE("HALO_H2_BOOST_SHOW"));
	if (!h2vb.halo2 || h2vb.scenario != global_scenario_index || (slot = h2vb_slot(vehicle_index, FALSE)) == NONE)
		return 0.0f;
	return h2vb.vehicles[slot].power;
}

/* (the input, xinput_sdl.c) whether the local player drives a vehicle that
boosts: Left Shift is its boost then */
int h2_vehicle_boost_local_driving(void)
{
	return h2vb.local_driving;
}

/* (units.c, each unit's update, the local player's) whether it drives a
boosting vehicle */
void h2_vehicle_boost_local_update(long unit_index)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get(unit_index);
	struct object_datum *parent;
	long player_index;

	if (!unit || unit->object.type != _object_type_biped || (player_index = unit->unit.player_index) == NONE ||
		local_player_get_player_index(0) != player_index)
	{
		return;
	}
	parent = unit->object.parent_object_index != NONE ? object_try_and_get(unit->object.parent_object_index) : NULL;
	/* (a vehicle with an e-brake - the Warthog -: its crouch the e-brake,
	the same button, h2_vehicle_physics.c) */
	h2vb.local_driving = h2vb.halo2 && parent && parent->object.type == _object_type_vehicle &&
		((struct unit_datum *)parent)->unit.driver_object_index == unit_index && (h2vb_kind_of(parent->definition_index) != NONE ||
		(h2_vehicle_physics_definition(parent->definition_index) &&
		TEST_FLAG(h2_vehicle_physics_definition(parent->definition_index)->vehicle_flags, 17)));
}

/* a vehicle's "jump" (0 or 1, the Warthog's tail lights' sixth function,
object_get_function_value): vehicles.cpp's 0x4000580 and function_f5d70, the
first half second of a brake or while the handbrake is held. The tag test's
Warthog shows its red flares with the jump button held and none after */
real h2_vehicle_tail_value(long vehicle_index)
{
	struct unit_datum *unit = (struct unit_datum *)object_get(vehicle_index);
	long i;

	if (TEST_FLAG(unit->unit.control_flags, _unit_control_jump_bit))
		return 1.0f;
	for (i = 0; i < H2VB_MAXIMUM_VEHICLES; i++)
	{
		if (h2vb.brakes[i].vehicle == vehicle_index)
			return h2vb.brakes[i].ticks < 15 ? 1.0f : 0.0f;
	}
	return 0.0f;
}

void h2_vehicle_flashlights_set(boolean allowed)
{
	h2vb_level();
	if (h2vb.flashlights != allowed)
	{
		h2vb.flashlights = allowed;
		h2vb.flashlights_time = game_time_get();
		if (HALO_ENV_ONCE("HALO_H2_DEBUG"))
			platform_log("h2 vehicles: flashlights %s", allowed ? "allowed" : "not allowed");
	}
}

/* a headlight's (the seventh function): driver_seat_power by flashlights_allowed,
unknown_0bffa0.cpp's function_c18e0's second's fade to the flag */
real h2_vehicle_flashlight_value(long vehicle_index)
{
	struct unit_datum *unit = (struct unit_datum *)object_get(vehicle_index);
	real time, fade;

	h2vb_level();
	time = (real)(game_time_get() - h2vb.flashlights_time) / 30.0f;
	time = PIN(time, 0.0f, 1.0f);
	fade = h2vb.flashlights ? time : 1.0f - time;
	return fade * unit->unit.seat_power[0];
}

/* the Banshee's "wingtips" (0 .. 1, a contrail's fifth function, object_get_function_value): how hard it turns
at speed, its wingtip contrails' scale. Halo 2's input is wingtip_contrail_new,
which the decomp does not name: this is what the tag test shows, a white line off
a tip in a hard banked turn at speed and none flying straight (its turn's
acceleration, 10 wu/s/s full) */
real h2_vehicle_wingtip_value(long vehicle_index)
{
	struct object_datum *object = (struct object_datum *)object_get(vehicle_index);
	real speed = 30.0f * magnitude3d(&object->object.translational_velocity);
	real turn = 30.0f * magnitude3d(&object->object.angular_velocity);
	real value = speed * turn / 10.0f;

	/* (Halo 2's function is off with its driver seat power) */
	if (((struct unit_datum *)object)->unit.driver_object_index == NONE)
		return 0.0f;
	return value > 1.0f ? 1.0f : value;
}
