/* ai_nav.h: (port) a map's navigation for computer-controlled characters
(port/linux/game/ai_nav.c), beyond the engine's own path searches: the
walkable copy of the level's surfaces (ramps opened, the ground under
machines and roofs closed) handed to path.c's searches, the regions those
surfaces make, and the jumps, drops, teleporters and ladders that join the
regions. Nothing here is game state: it is made again from the level each
map, a bounded step a tick, the same from run to run. HALO_AI_NAV
(halo_port_features.h) is 1 on the desktop builds and Android. */
#ifndef HALO_AI_NAV_H
#define HALO_AI_NAV_H

/* what a hop is: a teleporter or a ladder (the user's, by its index there), a
jump up onto higher ground, a drop down (stepping off, or a hop over a
railing first), a jump across a gap */
enum
{
	_ai_nav_hop_teleporter = 0,
	_ai_nav_hop_ladder,
	_ai_nav_hop_jump_up,
	_ai_nav_hop_drop,
	_ai_nav_hop_gap,
	NUMBER_OF_AI_NAV_HOPS,
};

#define AI_NAV_MAXIMUM_HOPS 640

struct ai_nav_hop
{
	short kind;
	/* the jump is pressed at the edge (an up jump, a gap, a railing first) */
	boolean needs_jump;
	/* the user's teleporter (or ladder) index, NONE for a jump */
	long teleporter_index;
	/* where it is taken from (on its walkable surface) and where it comes out
	(the landing, on its own), and the edge between, for a jump */
	real_point3d entrance;
	long entrance_surface_index;
	real_point3d exit;
	long exit_surface_index;
	real_point3d edge;
	/* the walk it stands for (its own length, and a little for the time a
	jump or a teleporter takes) */
	real cost;
	/* the times users took it and came out where it goes, and failed to; when
	it is gone for again after a failure */
	long successes;
	long failures;
	long rest_until_tick;
};

/* a map's start (all forgotten, the memory let go) and its end */
void ai_nav_reset(void);
/* the users' biped: how high its jump goes (the tag's jump velocity against
the gravity), what crouching in the air adds, and the fall it takes no harm
from (the globals' falling damage); given by the user */
void ai_nav_set_biped(long biped_definition_index);
/* (ai_nav_map_update's) a bounded step of the regions, the jumps found and
tested (no step once done) */
void ai_nav_update(void);
boolean ai_nav_ready(void);
/* (HALO_TICK_PROFILE) the step being made: 0 none yet ... 5 done */
short ai_nav_phase(void);
/* a user's teleporters and ladders: hops too */
void ai_nav_add_teleporter(long teleporter_index, boolean ladder, real_point3d const *entrance,
	long entrance_surface_index, real_point3d const *exit, long exit_surface_index);
/* whether a surface is a ramp: not one of the AI's walkable surfaces, but
one the biped walks on (facing up, no steeper than it climbs), joined to the
regions (opened in the walkable copy) */
boolean ai_nav_ramp(long surface_index);
/* the region of a walkable surface (NONE: not walkable, or not known yet) */
long ai_nav_region(long surface_index);
/* the way from a point on one walkable surface to a point on another: NONE
when both are in one region (a search walks there) or either is not known;
-2 when no way is known (no hop joins them); else the first hop to take, its
walk's estimate in *cost */
#define AI_NAV_NO_WAY (-2L)
long ai_nav_route(long start_surface_index, real_point3d const *start, long goal_surface_index,
	real_point3d const *goal, long now, real *cost);
/* the same, by the hops of the kinds given alone (a bit each, 1 <<
_ai_nav_hop_*): other AI that takes no teleporter or ladder (Forge's
characters: the game sends players alone through a teleporter) */
#define AI_NAV_ALL_HOPS ((1UL << NUMBER_OF_AI_NAV_HOPS) - 1)
long ai_nav_route_kinds(long start_surface_index, real_point3d const *start, long goal_surface_index,
	real_point3d const *goal, long now, real *cost, unsigned long kinds);
/* whether a goal's surface is one a user on start's can get to (TRUE when
not known) */
boolean ai_nav_reachable(long start_surface_index, long goal_surface_index, long now);
/* the map's hops */
long ai_nav_hop_count(void);
struct ai_nav_hop *ai_nav_hop_get(long hop_index);
/* a hop taken: came out where it goes, or not (left be a while; for the map
after three failures with no success) */
void ai_nav_hop_result(long hop_index, boolean success, long now);
boolean ai_nav_hop_usable(long hop_index, long now);
/* the jumps and drops near a walk, for a shortcut: those whose entrance is
in the region given, in turn (*cursor from 0; NONE past the last) */
long ai_nav_next_jump_from(long region, long *cursor);
/* the jump a biped makes (world units): its height, crouched */
real ai_nav_jump_height(void);
/* debug.txt's nav line */
void ai_nav_report(long now);

/* ---- the map's navigation, for any user (port/linux/game/ai_nav.c) */

/* once a tick while a user needs it: the walkable copy and the regions and
hops made, a bounded step a tick (a second call in a tick does nothing). A
new structure, or a game started again, starts it over. biped: the biped
whose jumps the hops are made for (NONE: as already set) */
void ai_nav_map_update(long biped_definition_index);
/* the walkable copy, the regions and the hops all made */
boolean ai_nav_map_ready(void);
/* everything let go (a map's end) */
void ai_nav_map_reset(void);
/* the walkable copy a search walks (a vehicle's: closed for vehicles too),
NULL while there is none for the loaded structure: given to path.c as a
search's structure (path_state's) */
struct structure_bsp;
struct structure_bsp *ai_nav_structure(boolean vehicle);
/* the level's surface under a point, and the ground there (NONE: none
within a few units) */
long ai_nav_ground_surface(real_point3d const *point, real_point3d *ground);
/* (debug.ai_nav_test, network_test.c) the map's navigation made in a
network test's game and a path searched between its two starting
locations furthest apart: the log says what was found */
void ai_nav_test_update(void);

#endif
