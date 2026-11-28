/*
H2_MACHINE_RIDERS.H

(port) Bipeds carried by the moving machines of a converted Halo 2 level:
the windmill's blades, the house gate's flaps (h2_machine_riders.c).
*/

#ifndef __H2_MACHINE_RIDERS_H
#define __H2_MACHINE_RIDERS_H

#include "math/real_math.h"

/* (bipeds.c, its physics) the biped stands on the machine at point, its
surface facing normal (the node it rides found there), or (no_contact) on
none this tick */
void h2_machine_rider_contact(long biped_index, long machine_index, real_point3d const *point,
	real_vector3d const *normal);
void h2_machine_rider_no_contact(long biped_index, real_vector3d *velocity);

/* (objects.c) a machine's update begins (its node matrices kept, when a
biped rides it) and ends (its node matrices recomputed: its riders moved
with the nodes they stand on) */
void h2_machine_riders_begin(long machine_index);
void h2_machine_riders_end(long machine_index);

#endif
