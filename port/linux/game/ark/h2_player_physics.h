/*
H2_PLAYER_PHYSICS.H

port: the physics a Halo 2 level plays by that Halo 2 keeps in the engine, not
in a tag: its gravity (h2_player_physics.c).
*/

#ifndef __H2_PLAYER_PHYSICS_H
#define __H2_PLAYER_PHYSICS_H

#include "cseries/cseries.h"

/* Halo 2's gravity, world units a second squared (its physics settings' first
value); Halo CE's is 3.2087 */
#define H2_GRAVITY_PER_SECOND 4.1712594f

/* a map was loaded (cache_files.c): on a converted Halo 2 level (scenario
h2\...) global_gravity is Halo 2's, on any other map Halo CE's */
void h2_player_physics_map_loaded(char const *scenario_name);

/* whether the loaded level is a converted Halo 2 one */
boolean h2_player_physics_level(void);

#endif
