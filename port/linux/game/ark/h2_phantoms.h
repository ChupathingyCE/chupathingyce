/*
H2_PHANTOMS.H

port: Halo 2's phantoms - gravity lifts, man cannons - on a Halo 2 level
(h2_phantoms.c).
*/

#ifndef __H2_PHANTOMS_H
#define __H2_PHANTOMS_H

#include "cseries.h"

/* (cache_files.c) a map loaded: a Halo 2 level's phantoms read from its
Halo 2 map (nothing for any other) */
void h2_phantoms_map_loaded(char const *map_name);
/* (game_time.c) each game tick: what is in a phantom pushed */
void h2_phantoms_tick(void);
/* (bots.c) whether the object was in a phantom (pushed by it) the last
tick */
boolean h2_phantoms_riding(long object_index);
/* (bots.c) the lifts (a phantom that pushes a player up): each shape of
each phantom object a slot; for one that is a lift, the lowest and highest
points of its column (its marker's line inside it) */
long h2_phantoms_lift_slots(void);
boolean h2_phantoms_lift_get(long slot, real_point3d *bottom, real_point3d *top);

#endif
