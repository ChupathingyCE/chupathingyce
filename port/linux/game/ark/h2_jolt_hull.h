/*
H2_JOLT_HULL.H

port: a Halo 2 Warthog's, Gauss Warthog's or Scorpion's hull as a Jolt rigid
body under h2_vehicle_physics.c's force model; h2_jolt_hull.c.
*/

#ifndef __H2_JOLT_HULL_H
#define __H2_JOLT_HULL_H

struct h2vp_definition;
struct physics_definition;

/* (h2_vehicle_physics.c) the vehicle's hull, the last step's result put on
its object, its contacts noted; FALSE: not on a hull (the engine's physics) */
boolean h2_jolt_hull_begin(long vehicle_index, struct h2vp_definition const *definition, struct physics_definition const *physics);
/* its centre of mass in the world */
void h2_jolt_hull_center(long vehicle_index, real_point3d *center);
/* whether it touches anything but its wheels' ground */
boolean h2_jolt_hull_touching(long vehicle_index);
/* acceleration (world units a second squared) and turning (r x that) on its
whole mass, given to the body before the next step */
void h2_jolt_hull_push(long vehicle_index, real_vector3d const *acceleration, real_vector3d const *turning);

/* (physics.c, ragdolls_props.c) whether the object has a hull */
void h2_jolt_hull_kick(long vehicle_index, real_vector3d const *acceleration, real_vector3d const *turning);
boolean h2_jolt_hull_of(long object_index);
/* (game_time.c) a tick's step, if nothing else stepped the world this tick */
void h2_jolt_hulls_tick(void);
/* (ragdolls_props.c) around the world's step: what the game did to the
hulls' objects put on their bodies; what the step did noted */
void h2_jolt_hulls_before_step(void);
void h2_jolt_hulls_after_step(void);

#endif
