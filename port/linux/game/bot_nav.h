/*
BOT_NAV.H

The bots' map of the world, the way Quake III's bots have one: an area
awareness system (AAS).

	bot_nav.c: the heightfield. Every collision surface of the map
	rasterized into columns; the floors in each, the room over them, and
	how far every spot is from a wall, a drop, and anything a vehicle
	can't go over.

	bot_aas.c: areas and reachabilities. The walkable floor is cut into
	convex areas (rectangles of the heightfield kept a body's width off
	walls, so anywhere in an area can be walked to in a straight line from
	anywhere else in it), and every way from one area into another is a
	reachability with a travel type (walk, jump up, drop down, jump a gap,
	climb a ladder, teleport), where it starts and ends and how long it
	takes. Vehicles get their own set of areas (a vehicle's width off walls,
	no stairs, no jumps). Routing answers "the quickest way from here to
	there" from a cache of travel times to each goal area.

Worked out once per map and kept in the save folder (u:\navcache).
*/

#ifndef __BOT_NAV_H
#define __BOT_NAV_H

#include "cseries.h"
#include "math/real_math.h"

#define NAVM_STEP 0.35f /* walked up without a jump */
#define NAVM_JUMP 0.75f /* up with a (crouch) jump */

/* ---------- the heightfield */

boolean navm_build(void); /* for the loaded map: from the cache, or worked out (then cached) */
void navm_dispose(void);
boolean navm_ready(void);

real navm_wall_clearance(real_point3d const *point, real *floor_z); /* room around a point (-1: not on the grid) */
real navm_edge_distance(real_point3d const *point); /* how far to a drop */
boolean navm_floor_at(real_point3d const *point, real above, real below, real *floor_z);
boolean navm_line_walkable(real_point3d const *from, real_point3d const *to, real wall_min, real edge_min);
boolean navm_line_passable(real_point3d const *from, real_point3d const *to, real wall_min); /* ...over a hand's width of missing floor, too */

/* ---------- the area awareness system */

enum
{
	AAS_FOOT = 0,
	AAS_VEHICLE,
	NUMBER_OF_AAS_TYPES
};

enum
{
	_aas_travel_walk = 0, /* across the shared edge (the portal) */
	_aas_travel_jump, /* up a ledge: run at it and jump */
	_aas_travel_drop, /* walk off a ledge */
	_aas_travel_gap, /* run and jump across */
	_aas_travel_ladder, /* walk into it facing it, up, and off at the top */
	_aas_travel_teleport, /* step on the pad */
	NUMBER_OF_AAS_TRAVEL_TYPES
};

enum
{
	_aas_area_reachable = 1, /* can be got to from where people start */
	_aas_area_playable = 2, /* and not somewhere odd (the top of the map's walls) */
	_aas_area_escapable = 4, /* and there is a way back from it */
};

struct aas_area
{
	real_point3d center;
	float x0, y0, x1, y1; /* its rectangle */
	float z0, z1;
	float clearance; /* the most room in it */
	long first_reach;
	long reach_count;
	long first_incoming;
	long incoming_count;
	long flags;
};

#define AAS_WALKOVER 2
#define AAS_HOP 3 /* a gap with something knee high in the way (a kerb, a low wall): jumped from further back, to be over it */

struct aas_reach
{
	long from, to;
	short travel; /* _aas_travel_* */
	short careful; /* a drop onto something narrow with a fall beyond it: off the edge at a walk, not a run;
	                  a gap with floor all the way across (AAS_WALKOVER): walked, not jumped */
	real_point3d start; /* where to be to take it (in the area it leaves) */
	real_point3d edge; /* where the move itself happens (the ledge, the gap's edge, the ladder's foot, the pad) */
	real_point3d end; /* where it comes out (in the area it goes to) */
	real_point3d left, right; /* walk: the shared edge's ends */
	real face_i, face_j; /* ladder: out of its face */
	float time; /* seconds, over and above the walk to start */
};

/* baking (bot_bake.c): the navigation is worked out afresh and kept with
   the game data, where every machine reads it before its own */
void navm_set_baking(boolean baking);
/* ... the files it has written so far (two a map: its heights, its areas) */
long navm_baked_files(void);
/* whether the map has baked navigation of this build's kind */
boolean navm_baked_current(char const *map_name);
boolean aas_baked_current(char const *map_name);

boolean aas_build(void);
void aas_dispose(void);
boolean aas_ready(void);

long aas_area_count(long type);
struct aas_area const *aas_area(long type, long area);
struct aas_reach const *aas_reach(long type, long reach);
long aas_reach_count(long type);

real aas_speed(long type); /* world units a second, for travel times */

/* the area a point is in (NONE: none, or not quite: see aas_nearest_area) */
long aas_point_area(long type, real_point3d const *point);
/* the nearest area to a point within radius (over the floor, not through
   walls), and the nearest spot in it */
long aas_nearest_area(long type, real_point3d const *point, real radius, real_point3d *spot);
/* a straight line along the floor, all in areas */
boolean aas_line_in_areas(long type, real_point3d const *from, real_point3d const *to);

/* routing. avoid (optional) says whether a reachability is to be left
   alone just now (one a bot failed at) */
typedef boolean (*aas_avoid_function)(void *context, long reach);
/* routes can take a side's costs for going through each area (the
   danger there, as that side knows it): slot, or NONE for none */
#define AAS_COST_SLOTS 4
void aas_set_area_costs(long type, long slot, float const *costs); /* seconds per area (area_count of them), or NULL */
float aas_goal_time(long type, long reach, long goal_area, long slot); /* from reach's start to the goal area; <0 no way */
long aas_route_reach(long type, long area, real_point3d const *from, long goal_area, aas_avoid_function avoid, void *context, long slot, float *time);
float aas_travel_time(long type, real_point3d const *from, real_point3d const *to); /* seconds; <0 no way */
void aas_reach_failed(long type, long reach);
/* a jumped gap's fall for a jump that misses (a great deal where there is no floor under it) */
real aas_reach_fall(long type, long reach); /* learned: it went wrong; it costs more from now on */

/* places of interest (spawns, items, flags, teleporters) */
long aas_place_count(void);
long aas_spawn_place_count(void); /* the first so many of them: where players start */
real_point3d const *aas_place(long index);
long aas_random_area(long type, unsigned long seed); /* a playable area, bigger ones likelier */

/* the bots' logging switches, read once (getenv is slow on some systems,
   and the bots ask every tick) */
int bot_logging(void); /* HALO_BOT_LOG */
int bot_env_set(char const *name);

#endif
