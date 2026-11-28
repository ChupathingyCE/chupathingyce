/*
H2_ITEM_PHYSICS.H

port: Halo 2's item motion on a Halo 2 level (h2_item_physics.c): how a
weapon, a grenade lying as equipment or a piece of garbage falls, bounces,
spins and comes to rest (items.cpp function_10b850, function_10cf80), a
unit's drop (unit_object_type.cpp function_ce6b0) and how a thrown
grenade stops (projectiles.cpp function_faa60). Halo CE's items.c and
projectiles.c ask these at the few places the two games differ; a Halo CE
map behaves as it always did.
*/

#ifndef __H2_ITEM_PHYSICS_H
#define __H2_ITEM_PHYSICS_H
#pragma once

#include "cseries.h"

/* ---------- constants */

enum
{
	/* (items.cpp: so many slow bounces in a row and an item rests where it
	is, whatever it met - a slope, a crate, a vehicle) */
	H2_ITEM_SLOW_BOUNCES_TO_REST = 4,
};

/* ---------- prototypes */

/* a Halo 2 level's item motion is Halo 2's (not HALO_H2_STOCK_ITEM_PHYSICS) */
boolean h2_item_physics_level(void);

/* (items.c item_update, a bounce that does not rest) Halo 2's bounce: the
velocity's change along the normal (a world-units-a-tick multiple of it)
for an impact of speed into (along -normal, a tick's), surface_floor (the
structure or a scenery or device met, its slope under 45 degrees),
structure (the structure met, not an object) */
real h2_item_bounce_reflection(real into, boolean surface_floor, boolean structure);

/* (items.c) a slow impact (under 1.5 world units a second into the surface) */
boolean h2_item_impact_slow(real into);

/* (items.c item_accelerate) the spin a tick an item given a push turns at
for Halo 2's radians a second */
real h2_item_spin_per_tick(real radians_per_second);

/* (units.c unit_drop_item) a dropped item's velocity held to Halo 2's 4
world units a second */
void h2_item_drop_velocity_clamp(real_vector3d *velocity);

/* (projectiles.c, a bounce) TRUE when a bouncing projectile that is left
this speed squared (a tick's) stops, Halo 2's 0.3 world units a second */
boolean h2_projectile_bounce_stops(real speed_squared);

/* (main.c) "@toss NAME vx vy vz [dx dy dz]": a new item or projectile of
the first weapon, equipment or projectile tag with NAME in its name, put dx dy dz (world units,
the first player's facing: forward, left, up) from the first player's
unit, given that velocity (world units a second, the same axes) - "NAME
vx vy vz @ x y z": at x y z, the world's axes -, traced:
a line each tick it moves (its place, speed, up vector, bounces) until it
rests and two seconds more */
void h2_item_physics_test_command(char const *arguments);

/* (items.c, projectiles.c: each one's update) the trace's; (items.c) a
bounce */
void h2_item_physics_trace(long object_index);
/* (perf/ce-floor-3) the same, its first test inline: nothing to do until a
trace was ever taken (every item and projectile update asks) */
extern boolean h2_item_traces_ever;
static __inline__ void h2_item_physics_trace_fast(long object_index)
{
	if (h2_item_traces_ever)
		h2_item_physics_trace(object_index);
}
void h2_item_physics_note_bounce(long object_index);

#endif
