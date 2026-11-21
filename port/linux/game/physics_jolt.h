/*
PHYSICS_JOLT.H

A small C face on Jolt Physics (port/third_party/jolt, MIT; physics_jolt.cpp)
for the ragdolls (ragdolls.c, display.ragdolls). Only the picture: nothing in
this world is the game's. It holds the level (one static mesh), what moves in
the level and a dead body meets (a vehicle's hull, a machine's parts: moved
where the game has them, never moved by a body), and the ragdolls' bodies.

Units are the caller's (ragdolls.c hands it metres); rotations are
quaternions x y z w. One world at a time, stepped on the game's thread.

The game's units are built with the Xbox's ABI (-malign-double,
-freg-struct-return, a 16-bit wchar_t) and this file's with the host's: so
nothing here is a double, a 64-bit integer, a structure returned or a wide
character, and every structure is ints, floats and pointers alone; nor a
long, which the 64-bit builds' game units read as 32 bits
(tools/lp64_rewrite.py) and this file's as 64.
*/

#ifndef __PHYSICS_JOLT_H
#define __PHYSICS_JOLT_H

#ifdef __cplusplus
extern "C" {
#endif

struct physics_jolt_stats
{
	unsigned int bytes_now, bytes_peak, allocations;
	int bodies, active_bodies;
	/* (the steps' time in all, milliseconds, and their number) */
	float step_ms_total;
	unsigned int steps;
};

/* a world for at most this many bodies; 0: none made */
int physics_jolt_create(int maximum_bodies, float const gravity[3]);
void physics_jolt_destroy(void);
int physics_jolt_exists(void);
/* (bumped by each physics_jolt_create: a body's or a ragdoll's number from
an older world is no longer its) */
unsigned int physics_jolt_generation(void);
/* the level mesh kept for the next world made from the same triangles (after
a checkpoint revert) let go: a map left */
void physics_jolt_forget_meshes(void);

/* the level: a static triangle mesh (vertices x y z, the triangles' vertex
indices counter-clockwise seen from the front); the body's number, -1 none */
int physics_jolt_add_mesh(float const *vertices, int vertex_count, int const *triangles, int triangle_count);
/* a part of a machine (a door, a lift): a triangle mesh about its origin at
position, turned by rotation, moved with physics_jolt_move */
int physics_jolt_add_moving_mesh(float const *vertices, int vertex_count, int const *triangles, int triangle_count,
	float const position[3], float const rotation[4]);
/* a vehicle: the convex hull of points about its origin at position, turned
by rotation, moved with physics_jolt_move */
int physics_jolt_add_moving_hull(float const *points, int point_count, float convex_radius,
	float const position[3], float const rotation[4]);
void physics_jolt_remove_body(int body);
/* a moving body in the world or out of it (a vehicle ridden or gone) */
void physics_jolt_set_enabled(int body, int enabled);
/* a moving body to where the game has it now, in dt seconds */
void physics_jolt_move(int body, float const position[3], float const rotation[4], float dt);
/* (after the level and its first moving bodies are in) */
void physics_jolt_optimize(void);

/* the world dt seconds on, in collision_steps steps */
void physics_jolt_step(float dt, int collision_steps);
void physics_jolt_get_stats(struct physics_jolt_stats *stats);
/* the host's steady clock, microseconds (it wraps: only differences mean
anything) */
unsigned int physics_jolt_clock_us(void);

/* ---------- a dead character's ragdoll (ragdolls.c makes its rig)

A body a joint of the body, parents first: its origin and rotation the node's
at the start (metres, the world), moving at velocity (metres a second) and
turning at angular_velocity (radians a second, about the world's axes), its
shape the convex hull of points in its own frame (4 or more), else a box of
half extents at box_centre turned by box_rotation; its mass (kg; the inertia
its shape's at that mass). Its joint to its parent is at the body's origin:
- a hinge: limits[0..1], about axis (parent_axes[0..2] on the parent,
  axes[0..2] on this body), positive turning reference (parent_axes[3..5])
  toward direction (axes[3..5]);
- a swing and twist: limits[0..5], the twist about forward, a swing about
  left, a swing about up, radians, each frame given by its forward
  (parent_axes[0..2], axes[0..2]) and left (parent_axes[3..5], axes[3..5]);
joint_friction a torque, N m. A part meets the level, what moves in it,
other ragdolls' parts and its own parts but those two joints or fewer from
it (or overlapping at the death). */
enum
{
	PHYSICS_JOLT_RIG_JOINT_NONE = 0,
	PHYSICS_JOLT_RIG_JOINT_HINGE,
	PHYSICS_JOLT_RIG_JOINT_RAGDOLL,
};

struct physics_jolt_rig_body
{
	int parent;
	int joint;
	float position[3], rotation[4], velocity[3], angular_velocity[3];
	float const *points;
	int point_count;
	float half[3], box_centre[3], box_rotation[4];
	float mass;
	float friction, restitution, linear_damping, angular_damping;
	float parent_axes[6], axes[6];
	float limits[6], joint_friction;
	/* (continuous collision: a fast limb does not pass through a thin wall) */
	int linear_cast;
};

struct physics_jolt_rig_options
{
	/* (each joint and part solved with at least these many velocity and
	position iterations; 0 the world's) */
	int velocity_steps, position_steps;
};

/* the ragdoll's number, -1 none */
int physics_jolt_rig_create(struct physics_jolt_rig_body const *bodies, int count,
	struct physics_jolt_rig_options const *options);
void physics_jolt_rig_destroy(int ragdoll);
void physics_jolt_rig_get_body(int ragdoll, int body, float position[3], float rotation[4]);
/* a body's velocity changed by velocity (metres a second), the ragdoll woken */
void physics_jolt_rig_add_velocity(int ragdoll, int body, float const velocity[3]);
/* a body pulled toward position and rotation (metres, the world; x y z w):
its velocity and spin turned weight of the way (0 to 1) toward those that
bring it there in dt seconds, the body woken (a death animation's pose the
ragdoll follows for a moment after it takes over) */
void physics_jolt_rig_drive(int ragdoll, int body, float const position[3], float const rotation[4], float dt,
	float weight);
/* whether Jolt has any of its bodies awake */
int physics_jolt_rig_active(int ragdoll);
void physics_jolt_rig_set_active(int ragdoll, int active);

#ifdef __cplusplus
}
#endif

#endif // __PHYSICS_JOLT_H
