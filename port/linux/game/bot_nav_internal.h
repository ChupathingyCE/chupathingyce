/*
BOT_NAV_INTERNAL.H

The heightfield (bot_nav.c) as the area awareness system (bot_aas.c)
builds on it.
*/

#ifndef __BOT_NAV_INTERNAL_H
#define __BOT_NAV_INTERNAL_H

#include "bot_nav.h"

#define NAVM_HEIGHT 0.7f /* room a Spartan needs over a floor (it's 0.69) */
#define NAVM_MINIMUM_NORMAL_K 0.7f /* steeper than about 45 degrees: not a floor */
#define NAVM_MAXIMUM_CLEARANCE 6.f
#define NAVM_VEHICLE_HEIGHT 0.9f /* room a vehicle needs */
#define NAVM_VEHICLE_STEP 0.17f /* a vehicle rolls up this much between neighbouring cells (not stairs) */

enum
{
	_navm_span_walkable = 1, /* room over it */
	_navm_span_blocked = 2, /* scenery stands in it */
	_navm_span_wall_border = 4,
	_navm_span_edge_border = 8,
	_navm_span_vehicle_border = 16,
};

struct navm_span
{
	float floor;
	float ceiling; /* the lowest thing over the floor */
	long next; /* the next floor up in its column */
	long column;
	unsigned short wall; /* distance to a wall, hundredths */
	unsigned short edge; /* distance to a drop, hundredths */
	unsigned short vehicle_wall; /* distance to anything a vehicle can't go over, hundredths */
	unsigned char flags;
	unsigned char pad;
};

/* a surface the map marks climbable (a ladder's face) */
struct navm_climbable
{
	real_point3d center;
	real normal_x, normal_y; /* out of the face */
	real bottom_z, top_z;
	real half_width;
};

#define NAVM_MAXIMUM_CLIMBABLES 128

struct navm_world
{
	boolean ready;
	float cell; /* the grid: world units a column (NAVM_CELL) */
	float x0, y0;
	long width, height;
	long *columns; /* first span in each column, NONE */
	struct navm_span *spans;
	long span_count;
	struct navm_climbable climbables[NAVM_MAXIMUM_CLIMBABLES];
	long climbable_count;
};

extern struct navm_world navm;

/* the heightfield's grid, world units: as fine as the map's size allows
   (navm_build): 0.1 for the small maps, up to 0.2 for the biggest */
#define NAVM_CELL (navm.cell)
#define NAVM_CELL_FINEST 0.1f
#define NAVM_CELL_COARSEST 0.2f
/* so many world units, in columns (at least one) */
#define NAVM_CELLS(units) ((long)MAX(1.f, (units) / NAVM_CELL + 0.5f))
extern long const navm_dx[8];
extern long const navm_dy[8];
extern float const navm_step_length[8];

long navm_column_of(real x, real y);
void navm_column_center(long column, real *x, real *y);
void navm_span_point(long span, real_point3d *point);
long navm_walk_neighbor(long span, long direction);
long navm_vehicle_neighbor(long span, long direction);
long navm_span_at(real_point3d const *point, real above, real below);
void navm_signature(char *map, unsigned long *signature);
void navm_cache_path(char const *map, char const *extension, char *path, long size);

struct navm_heap
{
	long *items;
	float *keys;
	long count;
};

void navm_heap_push(struct navm_heap *heap, long item, float key);
long navm_heap_pop(struct navm_heap *heap, float *key);

#endif
