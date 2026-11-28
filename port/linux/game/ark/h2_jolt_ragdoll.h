/*
H2_JOLT_RAGDOLL.H

port: a dead Halo 2 character's rigid ragdoll on Jolt (h2_jolt_ragdoll.c): its
physics model's bodies, pills, boxes, masses, joints and limits, read from the
player's map. ragdolls.c starts, steps and draws it. On by default where the
build has Jolt, the PC's; HALO_H2_JOLT_RAGDOLLS=0 turns it off (the engine's
ragdolls), on a Vita built with --jolt =1 turns it on. The rigid hulls have
their own switch.
*/

#ifndef __H2_JOLT_RAGDOLL_H
#define __H2_JOLT_RAGDOLL_H

#include "cseries.h"
#include "real_math.h"

#define H2_JOLT_RAGDOLL_MAXIMUM_BODIES 16

struct h2_cache;

/* one body of a physics model, in its node's frame, world units */
struct h2_jolt_ragdoll_body
{
	char node_name[32];
	short parent;				/* a body of the rig, parents first; NONE: the root */
	short joint;				/* PHYSICS_JOLT_RIG_JOINT_... to the parent */
	boolean box;
	real radius;
	real bottom[3], top[3];
	real half[3], box_axes[9], box_centre[3];
	real mass;
	real centre[3];				/* its centre of mass */
	real inertia[9];			/* kg wu^2 about its centre of mass */
	real friction, restitution, linear_damping, angular_damping;
	real parent_axes[6], axes[6];	/* the joint's axes (physics_jolt_rig_body's) */
	real limits[6];				/* radians (physics_jolt_rig_body's) */
	real joint_friction;		/* kg wu^2 / s^2 */
};

struct h2_jolt_ragdoll_rig
{
	char source[32];
	short body_count;
	struct h2_jolt_ragdoll_body bodies[H2_JOLT_RAGDOLL_MAXIMUM_BODIES];
};

/* the rigs of every Halo 2 character the level's maps hold: shared is
shared.map, cache the level's own */
void h2_jolt_ragdoll_map_loaded(struct h2_cache *cache, struct h2_cache *shared);
/* on: a Jolt build, a Halo 2 level, HALO_H2_JOLT_RAGDOLLS (this file's header) */
boolean h2_jolt_ragdolls_enabled(void);
/* the rig of an object's tag (its name: h2\characters\<source>_mp\...), NULL none */
struct h2_jolt_ragdoll_rig const *h2_jolt_ragdoll_rig_of(long definition_index);

#endif
