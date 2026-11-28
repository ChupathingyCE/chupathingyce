/*
H2_PLAYER_PHYSICS.C

port: Halo 2's gravity on a Halo 2 level. Halo 2 sets its gravity in the engine
(its physics settings, reset for each level: 4.1712594 world units a second
squared, where Halo CE's is 3.2087), not in a tag, so a converted level gets it
here, when it is loaded, and any other map gets Halo CE's back. This one value
(HALO_H2_STOCK_PLAYER_PHYSICS: none of this player physics - this, the biped's
jump, landings and slopes and the globals' look and falls - for a comparison)
is what the biped's jump height, the fall of a grenade or a projectile, an
item's drop and the falling damage's speeds all follow (global_gravity, a
distance a tick squared): a Halo 2 jump of 3.08 world units a second rises
3.08^2 / (2 * 4.17) = 1.14 where Halo CE's 2.10 rises 0.69.
*/

#include "cseries/cseries.h"
#include "physics/physics.h"
#include "h2_player_physics.h"

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

/* Halo CE's (physics.c) */
#define CE_GRAVITY 0.0035651792f

/* ---------- globals */

/* (perf/ce-floor-2: not static - h2_level_fast.h reads it inline) */
boolean h2_player_physics_is_level;

/* ---------- public code */

void h2_player_physics_map_loaded(
	char const *scenario_name)
{
	h2_player_physics_is_level = scenario_name && !strncmp(scenario_name, "h2\\", 3) && !getenv("HALO_H2_STOCK_PLAYER_PHYSICS");
	global_gravity = h2_player_physics_is_level ?
		H2_GRAVITY_PER_SECOND / (real)(TICKS_PER_SECOND * TICKS_PER_SECOND) : CE_GRAVITY;
}

boolean h2_player_physics_level(
	void)
{
	return h2_player_physics_is_level;
}
