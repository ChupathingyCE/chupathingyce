/*
H2_RAGDOLL_RULES.H

port: Halo 2's own rules for a dead biped's ragdoll (bipeds.cpp, Bungie's
code; no Havok code): the push its bodies get when it falls over, and the
loose-then-tight schedule of its joints' limits and friction. Nothing here
knows a solver: ragdolls.c applies them to its particles, and a rigid-body
engine (Jolt: jolt/vita) can apply them to its bodies and constraints as they
are.
*/

#ifndef __H2_RAGDOLL_RULES_H
#define __H2_RAGDOLL_RULES_H

#include "cseries.h"
#include "real_math.h"

/* (bipeds.cpp function_e0e00, unknown_1cec30.cpp function_1d35d0) the scale
of every ragdoll joint's limits - a ragdoll constraint's twist, cone and
plane, a limited hinge's range - and of its friction, `seconds` after the
biped went limp: 1 + (1 - min(0.8 seconds, 1)) x 2, 1, 1.333 or 1.666 (by
the biped's index), so a body falls loose for 1.25 seconds, its joints
reaching their tags' limits and friction then (the Spartan's and Elite's
joints: 0 in their ragdoll constraints, 0.25 in their node edges) */
real h2_ragdoll_settle_scale(
	long biped_index,
	real seconds);

/* the seconds the schedule lasts */
#define H2_RAGDOLL_SETTLE_SECONDS 1.25f

/* (bipeds.cpp function_e0ef0) the push of a body going limp, each body's
velocity change in world units a second in changes[]: with a killing blow
(direction, its body hit) a spin about the mass centre - the blow made
square to the line from the centre to the body hit, random 1.5 to 3.25 wu/s
x min(push scale, 0.7), each body by how far along that line it lies (the
farthest 1, behind the centre x0.65 the other way), the body hit twice - and
a random lift of 0.75 to 1.5 wu/s for all; without one (direction NULL or
no body hit) its root body 2 wu/s down. `random` gives numbers in [0, 1):
the caller's, deterministic or not as its simulation needs. FALSE: none. */
boolean h2_ragdoll_death_push(
	short count,
	real_point3d const *positions,
	real const *masses,
	short root,
	short hit,
	real_vector3d const *direction,
	real push_scale,
	real (*random)(void *context),
	void *context,
	real_vector3d *changes);

/* (the bipd's "head shot acc scale" +0x258, Halo 2's Spartan 1.2 and Elite 1.3: held to 0.7) */
#define H2_RAGDOLL_PUSH_SCALE_MAXIMUM 0.7f

#endif
