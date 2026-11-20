/*
H2_COLLISION_DAMAGE.H

port: Halo 2's collision damage of a vehicle - from a wall, the ground, another
vehicle - by Bungie's rule over the contacts' impulses (unknown_1d96d0.cpp,
unknown_1da550.cpp function_1da6d0 and function_1da9e0; the rule, not Havok).
physics.c notes a Halo 2 vehicle's contacts' velocity changes as its physics
run; h2_vehicle_physics.c applies the rule at the end of the vehicle's tick.
*/

#ifndef __H2_COLLISION_DAMAGE_H
#define __H2_COLLISION_DAMAGE_H

/* (h2_vehicle_physics.c, before the vehicle's physics) which of its mass
points are its hull - not its wheels or anti-gravity points, which Halo 2
holds up by raycasts, never by a contact */
void h2_collision_damage_hull(long vehicle_index, unsigned long hull_mask);
/* (physics.c) mass point mass_point_index of object_index found the ground
this tick, moving into it at normal_velocity (a tick's, negative into the
ground): a landing when the hull was off the ground last tick; from_origin the
point from the object's origin, normal the ground's */
void h2_collision_damage_ground(long object_index, short mass_point_index, real normal_velocity, real_vector3d const *from_origin,
	real_vector3d const *normal);
/* (physics.c) object_index's sweep met a surface (of other_object_index, NONE
the world) at point, its normal normal, and lost delta_v (a tick's) of its
velocity into it (point NULL: at its centre of mass) */
void h2_collision_damage_sweep(long object_index, long other_object_index, real delta_v, real_point3d const *point,
	real_vector3d const *normal);
/* (physics.c) two vehicles' mass points overlap, closing at closing_speed (a
tick's, positive toward each other) along their line; masses theirs */
void h2_collision_damage_pair(long object_index, real mass, long other_object_index, real other_mass, real closing_speed);
/* (h2_jolt_hull.c) a rigid hull's contact: the velocity it lost to a new
contact (world units a second), other_object_index NONE for the world */
void h2_collision_damage_contact(long object_index, long other_object_index, real impulse);
/* (h2_vehicle_physics.c) at the end of the vehicle's physics tick: Halo 2's
rule over what was noted this tick; collision the vehicle's obje 0x6C..0x8C
(h2vp_definition.collision); speed_before its speed before its physics ran
(world units a second; negative: its speed now) */
void h2_collision_damage_tick(long vehicle_index, float const collision[9], real speed_before);

#endif
