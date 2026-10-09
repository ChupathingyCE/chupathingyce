/*
BOT_NAV.C

Where a body can be, worked out from the map itself: the heightfield the
area awareness system (bot_aas.c) is built on.

Every surface of the map's collision geometry is rasterized into columns
NAVM_CELL square. In each column, the upward-facing surfaces a Spartan can
stand on are floors; everything else that passes through a column (walls,
rocks, ceilings, the underside of a floor above) limits the room over
them. A floor with a body's height of room is walkable. Scenery (trees,
rocks, crates) is marked by testing against it where it stands.

Walkable spots next to each other at about the same height are joined.
From every spot, the distance to the nearest wall, to the nearest drop,
and to the nearest thing a vehicle can't drive over (a wall, a step, a low
ceiling) is worked out over the whole grid (Dijkstra from the borders).
Surfaces the map marks climbable (ladders) are kept for the reachabilities.
*/

#include "bot_port.h"
#include "bot_nav_internal.h"

#include "objects/objects.h"
#include "physics/collisions.h"
#include "physics/collision_bsp_definitions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/files.h"
#include "tag_files/tag_groups.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* a heightfield's file: all its columns, row by row (5), or the blocks of
   them that are worked out (6: a very large map's) */
#define NAVM_CACHE_VERSION 5
#define NAVM_CACHE_VERSION_BLOCKS 6
/* a grid's side, at most, in columns (its columns' numbers fit a long) */
#define NAVM_MAXIMUM_SIDE 40000
#define NAVM_FLOOR_BUDGET 1200000.f /* floors (a column's worth each) a map gets, about: a bigger map, a coarser grid */
#define NAVM_CACHE_MAGIC 0x4d56414eUL /* NAVM */

struct navm_interval
{
	float low, high; /* where a surface passes through a column */
	float floor; /* a floor's height at the column's middle */
	long next;
	unsigned char walkable;
	unsigned char pad[3];
};

struct navm_world navm;

/* A build that ran out of memory. What it has by then is not all of the
   map: a wall whose surfaces are not in it is open floor. It is not used
   and not kept (navm_build). */
static boolean navm_build_failed;

static void navm_no_memory(char const *what)
{
	if (!navm_build_failed)
		bot_log("navm: no memory for %s: this map has no navigation", what);
	navm_build_failed = TRUE;
}

/* ---------- the grid's columns */

static boolean navm_grid_new(struct navm_grid *grid, long width, long height)
{
	grid->across = (width + NAVM_BLOCK - 1) >> NAVM_BLOCK_SHIFT;
	grid->down = (height + NAVM_BLOCK - 1) >> NAVM_BLOCK_SHIFT;
	grid->blocks = (long **)calloc((size_t)(grid->across * grid->down), sizeof(long *));
	return grid->blocks != NULL;
}

static void navm_grid_free(struct navm_grid *grid)
{
	long i;
	if (grid->blocks)
		for (i = 0; i < grid->across * grid->down; i++)
			free(grid->blocks[i]);
	free(grid->blocks);
	memset(grid, 0, sizeof(*grid));
}

/* which block of a grid has a column */
static long navm_block_index(struct navm_grid const *grid, long x, long y)
{
	return (y >> NAVM_BLOCK_SHIFT) * grid->across + (x >> NAVM_BLOCK_SHIFT);
}

long navm_grid_get(struct navm_grid const *grid, long x, long y)
{
	long const *block;
	if (!grid->blocks)
		return NONE;
	block = grid->blocks[navm_block_index(grid, x, y)];
	return block ? block[((y & (NAVM_BLOCK - 1)) << NAVM_BLOCK_SHIFT) | (x & (NAVM_BLOCK - 1))] : NONE;
}

/* a block of the grid (made, every column NONE, if it is not there); NULL
   when there is no memory for it */
static long *navm_grid_block(struct navm_grid *grid, long index)
{
	if (!grid->blocks[index])
	{
		long *block = (long *)malloc(NAVM_BLOCK * NAVM_BLOCK * sizeof(long));
		long i;
		if (!block)
			return NULL;
		for (i = 0; i < NAVM_BLOCK * NAVM_BLOCK; i++)
			block[i] = NONE;
		grid->blocks[index] = block;
	}
	return grid->blocks[index];
}

/* where a column's long is kept, to set it */
static long *navm_grid_slot(struct navm_grid *grid, long x, long y)
{
	long *block = navm_grid_block(grid, navm_block_index(grid, x, y));
	return block ? &block[((y & (NAVM_BLOCK - 1)) << NAVM_BLOCK_SHIFT) | (x & (NAVM_BLOCK - 1))] : NULL;
}

long navm_first_span(long column)
{
	return navm_grid_get(&navm.columns, column % navm.width, column / navm.width);
}

/* whether a column's block is one that is worked out */
static boolean navm_wanted_at(long x, long y)
{
	return !navm.wanted || navm.wanted[navm_block_index(&navm.columns, x, y)];
}

/* ---------- small things */

long navm_column_of(real x, real y)
{
	long cx, cy;
	if (!navm.columns.blocks || navm.cell <= 0.f)
		return NONE;
	cx = (long)floor((x - navm.x0) / NAVM_CELL);
	cy = (long)floor((y - navm.y0) / NAVM_CELL);
	if (cx < 0 || cy < 0 || cx >= navm.width || cy >= navm.height)
		return NONE;
	return cy * navm.width + cx;
}

void navm_column_center(long column, real *x, real *y)
{
	*x = navm.x0 + ((column % navm.width) + 0.5f) * NAVM_CELL;
	*y = navm.y0 + ((column / navm.width) + 0.5f) * NAVM_CELL;
}

void navm_span_point(long span, real_point3d *point)
{
	navm_column_center(navm.spans[span].column, &point->x, &point->y);
	point->z = navm.spans[span].floor;
}

long const navm_dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
long const navm_dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
float const navm_step_length[8] = { 1.f, 1.f, 1.f, 1.f, 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f };

/* the walkable span in the next column over (direction 0..7) that a body
   walks onto from s: about the same height, room for it through the way */
long navm_walk_neighbor(long s, long direction)
{
	struct navm_span const *span = &navm.spans[s];
	long x = span->column % navm.width + navm_dx[direction];
	long y = span->column / navm.width + navm_dy[direction];
	long t;
	long best = NONE;
	real best_rise = 1.0e9f;

	if (x < 0 || y < 0 || x >= navm.width || y >= navm.height)
		return NONE;
	/* something thin in the way (a railing, a post) the heightfield missed */
	if (direction < 4 && (span->pad & (1 << direction)))
		return NONE;
	for (t = navm_first_span_at(x, y); t != NONE; t = navm.spans[t].next)
	{
		struct navm_span const *other = &navm.spans[t];
		real rise = other->floor - span->floor;
		if (!(other->flags & _navm_span_walkable) || (other->flags & _navm_span_blocked) || fabs(rise) > NAVM_STEP)
			continue;
		if (MIN(span->ceiling, other->ceiling) - MAX(span->floor, other->floor) < NAVM_HEIGHT)
			continue;
		if (fabs(rise) < best_rise)
		{
			best_rise = (real)fabs(rise);
			best = t;
		}
	}
	/* a diagonal only where both sides are open too: not through a corner */
	if (best != NONE && direction >= 4)
	{
		long a = navm_walk_neighbor(s, navm_dx[direction] > 0 ? 0 : 1);
		long b = navm_walk_neighbor(s, navm_dy[direction] > 0 ? 2 : 3);
		if (a == NONE || b == NONE)
			return NONE;
	}
	return best;
}

/* the same for a vehicle: no steps (only slopes), a vehicle's room over */
long navm_vehicle_neighbor(long s, long direction)
{
	long t = navm_walk_neighbor(s, direction);
	if (t == NONE)
		return NONE;
	if (fabs(navm.spans[t].floor - navm.spans[s].floor) > NAVM_VEHICLE_STEP * navm_step_length[direction] ||
		navm.spans[t].ceiling - navm.spans[t].floor < NAVM_VEHICLE_HEIGHT || navm.spans[s].ceiling - navm.spans[s].floor < NAVM_VEHICLE_HEIGHT)
	{
		return NONE;
	}
	return t;
}

/* the span a point stands on (floor at or a little under it) */
long navm_span_at(real_point3d const *point, real above, real below)
{
	long column = navm_column_of(point->x, point->y);
	long s;
	long best = NONE;
	real best_distance = 1.0e9f;
	if (column == NONE)
		return NONE;
	for (s = navm_first_span(column); s != NONE; s = navm.spans[s].next)
	{
		real distance = point->z - navm.spans[s].floor;
		if (distance > below || distance < -above)
			continue;
		if (fabs(distance) < best_distance)
		{
			best_distance = (real)fabs(distance);
			best = s;
		}
	}
	return best;
}

/* ---------- building: the heightfield */

/* testing: HALO_BOT_OLDNAV, the heightfield's rules as they were */
static int navm_old_rules(void)
{
	static int old = -1;
	if (old < 0)
		old = getenv("HALO_BOT_OLDNAV") != NULL;
	return old;
}

/* clip a polygon to x >= edge (sign 1) or x <= edge (sign -1) on axis */
static long navm_clip(real_point3d *out, real_point3d const *in, long count, long axis, real edge, real sign)
{
	long i;
	long written = 0;
	for (i = 0; i < count; i++)
	{
		real_point3d const *a = &in[i];
		real_point3d const *b = &in[(i + 1) % count];
		real da = (a->n[axis] - edge) * sign;
		real db = (b->n[axis] - edge) * sign;
		if (da >= 0.f)
			out[written++] = *a;
		if ((da >= 0.f) != (db >= 0.f))
		{
			real t = da / (da - db);
			out[written].x = a->x + (b->x - a->x) * t;
			out[written].y = a->y + (b->y - a->y) * t;
			out[written].z = a->z + (b->z - a->z) * t;
			written++;
		}
	}
	return written;
}

/* the way back, for the four straight directions */
static long const navm_opposite[4] = { 1, 0, 3, 2 };

struct navm_raster
{
	struct navm_grid first; /* per column: first interval */
	struct navm_interval *intervals;
	long count;
	long capacity;
};

static void navm_add_interval(struct navm_raster *raster, long x, long y, real low, real high, real floor_z, int walkable)
{
	struct navm_interval *interval;
	long *first;
	if (navm_build_failed)
		return;
	first = navm_grid_slot(&raster->first, x, y);
	if (!first)
	{
		navm_no_memory("the surfaces' columns");
		return;
	}
	if (raster->count >= raster->capacity)
	{
		long capacity = raster->capacity ? raster->capacity * 2 : (1 << 20);
		struct navm_interval *grown = (struct navm_interval *)realloc(raster->intervals, capacity * sizeof(struct navm_interval));
		if (!grown)
		{
			navm_no_memory("the surfaces");
			return;
		}
		raster->intervals = grown;
		raster->capacity = capacity;
	}
	/* (testing, HALO_BOT_TEST_NOMEMORY=navm: as if there were no more) */
	if (raster->count == 200000 && getenv("HALO_BOT_TEST_NOMEMORY") && !strcmp(getenv("HALO_BOT_TEST_NOMEMORY"), "navm"))
	{
		navm_no_memory("the surfaces (a test)");
		return;
	}
	interval = &raster->intervals[raster->count];
	interval->low = low;
	interval->high = high;
	interval->floor = floor_z;
	interval->walkable = (unsigned char)walkable;
	interval->next = *first;
	*first = raster->count++;
}

/* a surface of the map's collision geometry: its corners (up to 16) and
   its plane, facing out */
static long navm_surface_polygon(struct collision_bsp *bsp, long s, real_point3d *polygon, real_plane3d *plane)
{
	struct collision_surface const *surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, s, struct collision_surface);
	long count = 0;
	long edge_index = surface->first_edge_index;
	*plane = *TAG_BLOCK_GET_ELEMENT(&bsp->bsp3d.planes, surface->plane_designator & 0x7fffffff, real_plane3d);
	if (surface->plane_designator < 0)
	{
		plane->n.i = -plane->n.i;
		plane->n.j = -plane->n.j;
		plane->n.k = -plane->n.k;
		plane->d = -plane->d;
	}
	do
	{
		struct collision_edge const *edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
		int second = edge->surface_indices[1] == s;
		struct collision_vertex const *vertex = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, edge->vertex_indices[second], struct collision_vertex);
		polygon[count++] = vertex->point;
		edge_index = edge->edge_indices[second];
	}
	while (edge_index != surface->first_edge_index && count < 16);
	return count;
}

/* how much floor the map has, seen from above (world units squared): what
   the heightfield's size goes by */
static real navm_floor_extent(struct collision_bsp *bsp, real minimum_x, real minimum_y, real maximum_x, real maximum_y)
{
	double total = 0.;
	long s;
	for (s = 0; s < bsp->surfaces.count; s++)
	{
		real_point3d polygon[16];
		real_plane3d plane;
		long count = navm_surface_polygon(bsp, s, polygon, &plane);
		double twice = 0.;
		long i;
		if (count < 3 || plane.n.k < NAVM_MINIMUM_NORMAL_K)
			continue;
		/* (within the part that's kept, going by a corner of it) */
		if (polygon[0].x < minimum_x || polygon[0].x > maximum_x || polygon[0].y < minimum_y || polygon[0].y > maximum_y)
			continue;
		for (i = 0; i < count; i++)
		{
			real_point3d const *a = &polygon[i];
			real_point3d const *b = &polygon[(i + 1) % count];
			twice += (double)a->x * b->y - (double)b->x * a->y;
		}
		total += fabs(twice) * 0.5;
	}
	return (real)total;
}

/* where a multiplayer map is played: round where players start, the
   weapons and the flags, and a good way out from them. (A campaign level
   is played all over: left as it is) */
#define NAVM_PLAY_MARGIN 30.f

struct player_starting_location *player_get_starting_location(short starting_location_index);

/* scenario_netgame_equipment (game_engine.c): 0x90 each, the position at 0x40 */
static real_point3d const *navm_netgame_equipment_position(struct scenario *scenario, long i)
{
	return (real_point3d const *)((byte *)xbox_pointer(scenario->netgame_equipment.address) + i * 0x90 + 0x40);
}

static void navm_play_bounds(real *minimum_x, real *minimum_y, real *maximum_x, real *maximum_y)
{
	struct scenario *scenario = global_scenario_get();
	real x0 = 1.0e9f, y0 = 1.0e9f, x1 = -1.0e9f, y1 = -1.0e9f;
	long count = 0;
	long i;
	if (!scenario || scenario->type != _scenario_type_multiplayer || getenv("HALO_BOT_OLDNAV"))
		return;
	for (i = 0; i < scenario->players.count; i++)
	{
		struct player_starting_location *location = player_get_starting_location((short)i);
		if (location)
		{
			x0 = MIN(x0, location->position.x), x1 = MAX(x1, location->position.x);
			y0 = MIN(y0, location->position.y), y1 = MAX(y1, location->position.y);
			count++;
		}
	}
	for (i = 0; i < scenario->netgame_equipment.count; i++)
	{
		real_point3d const *point = navm_netgame_equipment_position(scenario, i);
		x0 = MIN(x0, point->x), x1 = MAX(x1, point->x);
		y0 = MIN(y0, point->y), y1 = MAX(y1, point->y);
		count++;
	}
	for (i = 0; i < scenario->netgame_flags.count; i++)
	{
		/* scenario_netgame_flag: position first, 0x94 each */
		real_point3d const *point = (real_point3d const *)((byte *)xbox_pointer(scenario->netgame_flags.address) + i * 0x94);
		x0 = MIN(x0, point->x), x1 = MAX(x1, point->x);
		y0 = MIN(y0, point->y), y1 = MAX(y1, point->y);
		count++;
	}
	if (count < 4)
		return;
	*minimum_x = MAX(*minimum_x, x0 - NAVM_PLAY_MARGIN);
	*minimum_y = MAX(*minimum_y, y0 - NAVM_PLAY_MARGIN);
	*maximum_x = MIN(*maximum_x, x1 + NAVM_PLAY_MARGIN);
	*maximum_y = MIN(*maximum_y, y1 + NAVM_PLAY_MARGIN);
}

/* The parts of a very large map that are worked out: the blocks within a
   way of where it is played. Players' starting places, the weapons and
   powerups, the vehicles and the flags, teleporters and the ball count in
   full; a race's checkpoints and the hills (strung out across the open
   ground) for half. If that is more floor than a machine should work out
   and keep, the way round each is shortened until it fits. FALSE when the
   map marks nowhere. */
#define NAVM_PART_RADIUS 56.f
#define NAVM_PART_RADIUS_LEAST 14.f
#define NAVM_PARTS_COLUMNS (3 * 1024 * 1024 + 512 * 1024)

struct navm_part_point
{
	real x, y;
	real weight; /* of the radius */
};

static long navm_part_points(struct navm_part_point *points, long maximum)
{
	struct scenario *scenario = global_scenario_get();
	long count = 0;
	long i;
	if (!scenario)
		return 0;
	for (i = 0; i < scenario->players.count && count < maximum; i++)
	{
		struct player_starting_location *location = player_get_starting_location((short)i);
		if (location)
			points[count].x = location->position.x, points[count].y = location->position.y, points[count++].weight = 1.f;
	}
	for (i = 0; i < scenario->netgame_equipment.count && count < maximum; i++)
	{
		real_point3d const *point = navm_netgame_equipment_position(scenario, i);
		points[count].x = point->x, points[count].y = point->y, points[count++].weight = 1.f;
	}
	for (i = 0; i < scenario->netgame_flags.count && count < maximum; i++)
	{
		struct scenario_netgame_flag const *flag = (struct scenario_netgame_flag const *)((byte *)xbox_pointer(scenario->netgame_flags.address) + i * 0x94);
		points[count].x = flag->position.x, points[count].y = flag->position.y;
		points[count++].weight = flag->type == _netgame_flag_race_track || flag->type == _netgame_flag_race_vehicle ||
			flag->type == _netgame_flag_hill ? 0.5f : 1.f;
	}
	for (i = 0; i < scenario->vehicles.count && count < maximum; i++)
	{
		/* a vehicle's placement (0x78 each): a scenario_object_datum first */
		struct scenario_object_datum const *vehicle = (struct scenario_object_datum const *)((byte *)xbox_pointer(scenario->vehicles.address) + i * 0x78);
		points[count].x = vehicle->position.x, points[count].y = vehicle->position.y, points[count++].weight = 1.f;
	}
	return count;
}

static boolean navm_choose_parts(void)
{
	enum { MAXIMUM_POINTS = 2048 };
	static struct navm_part_point points[MAXIMUM_POINTS];
	long count = navm_part_points(points, MAXIMUM_POINTS);
	long total = navm.columns.across * navm.columns.down;
	real side = NAVM_BLOCK * NAVM_CELL; /* a block's, world units */
	real radius;
	long wanted = 0;

	if (count < 1 || total <= 0)
		return FALSE;
	free(navm.wanted);
	navm.wanted = (unsigned char *)malloc((size_t)total);
	if (!navm.wanted)
		return FALSE;
	for (radius = NAVM_PART_RADIUS; ; radius *= 0.85f)
	{
		long p;
		memset(navm.wanted, 0, (size_t)total);
		wanted = 0;
		for (p = 0; p < count; p++)
		{
			real reach = radius * points[p].weight;
			long bx0 = MAX(0, (long)floor((points[p].x - reach - navm.x0) / side));
			long bx1 = MIN(navm.columns.across - 1, (long)floor((points[p].x + reach - navm.x0) / side));
			long by0 = MAX(0, (long)floor((points[p].y - reach - navm.y0) / side));
			long by1 = MIN(navm.columns.down - 1, (long)floor((points[p].y + reach - navm.y0) / side));
			long bx, by;
			for (by = by0; by <= by1; by++)
			{
				for (bx = bx0; bx <= bx1; bx++)
				{
					/* (the block's nearest point to it) */
					real nx = PIN(points[p].x, navm.x0 + bx * side, navm.x0 + (bx + 1) * side);
					real ny = PIN(points[p].y, navm.y0 + by * side, navm.y0 + (by + 1) * side);
					unsigned char *block = &navm.wanted[by * navm.columns.across + bx];
					if ((nx - points[p].x) * (nx - points[p].x) + (ny - points[p].y) * (ny - points[p].y) <= reach * reach && !*block)
					{
						*block = TRUE;
						wanted++;
					}
				}
			}
		}
		if (wanted * NAVM_BLOCK * NAVM_BLOCK <= NAVM_PARTS_COLUMNS || radius * 0.85f < NAVM_PART_RADIUS_LEAST)
			break;
	}
	bot_log("navm: a very large map (%.0f by %.0f world units): its floors are worked out within %.0f world units of where it is played, %ld of %ld blocks (%.0f%% of the map)",
		navm.width * NAVM_CELL, navm.height * NAVM_CELL, radius, wanted, total, 100. * wanted / total);
	return wanted > 0;
}

/* a ladder's face (climbable, upright): kept, the faces of one ladder
   together */
static void navm_keep_ladder_face(real_point3d const *polygon, long count, real_plane3d const *plane)
{
	real horizontal = (real)sqrt(plane->n.i * plane->n.i + plane->n.j * plane->n.j);
	real bottom = 1.0e9f, top = -1.0e9f, sx = 0.f, sy = 0.f, width = 0.f;
	long l;
	long c;
	for (c = 0; c < count; c++)
	{
		bottom = MIN(bottom, polygon[c].z);
		top = MAX(top, polygon[c].z);
		sx += polygon[c].x / count;
		sy += polygon[c].y / count;
	}
	for (c = 0; c < count; c++)
	{
		/* along the face */
		real along = (real)fabs((polygon[c].x - sx) * -plane->n.j / horizontal + (polygon[c].y - sy) * plane->n.i / horizontal);
		width = MAX(width, along);
	}
	for (l = 0; l < navm.climbable_count; l++)
	{
		struct navm_climbable *climbable = &navm.climbables[l];
		real dx = sx - climbable->center.x;
		real dy = sy - climbable->center.y;
		if (climbable->normal_x * plane->n.i / horizontal + climbable->normal_y * plane->n.j / horizontal > 0.9f &&
			dx * dx + dy * dy < 1.2f * 1.2f && bottom < climbable->top_z + 0.3f && top > climbable->bottom_z - 0.3f)
		{
			climbable->bottom_z = MIN(climbable->bottom_z, bottom);
			climbable->top_z = MAX(climbable->top_z, top);
			climbable->center.z = climbable->bottom_z;
			climbable->half_width = MAX(climbable->half_width, width);
			break;
		}
	}
	if (l == navm.climbable_count && navm.climbable_count < NAVM_MAXIMUM_CLIMBABLES)
	{
		struct navm_climbable *climbable = &navm.climbables[navm.climbable_count++];
		climbable->center.x = sx;
		climbable->center.y = sy;
		climbable->center.z = bottom;
		climbable->normal_x = plane->n.i / horizontal;
		climbable->normal_y = plane->n.j / horizontal;
		climbable->bottom_z = bottom;
		climbable->top_z = top;
		climbable->half_width = width;
	}
}

static void navm_rasterize_bsp(struct navm_raster *raster, struct collision_bsp *bsp)
{
	long s;
	for (s = 0; s < bsp->surfaces.count; s++)
	{
		struct collision_surface const *surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, s, struct collision_surface);
		real_plane3d plane;
		real_point3d polygon[16];
		long count = navm_surface_polygon(bsp, s, polygon, &plane);
		real minimum_x = 1.0e9f, maximum_x = -1.0e9f, minimum_y = 1.0e9f, maximum_y = -1.0e9f;
		long cx0, cx1, cy0, cy1, cx, cy;
		int walkable;

		if (count < 3)
			continue;
		for (cx = 0; cx < count; cx++)
		{
			minimum_x = MIN(minimum_x, polygon[cx].x);
			maximum_x = MAX(maximum_x, polygon[cx].x);
			minimum_y = MIN(minimum_y, polygon[cx].y);
			maximum_y = MAX(maximum_y, polygon[cx].y);
		}
		if ((surface->flags & (1 << 2)) && sqrt(plane.n.i * plane.n.i + plane.n.j * plane.n.j) > 0.6f)
			navm_keep_ladder_face(polygon, count, &plane);
		walkable = plane.n.k >= NAVM_MINIMUM_NORMAL_K;
		cx0 = MAX(0, (long)floor((minimum_x - navm.x0) / NAVM_CELL));
		cx1 = MIN(navm.width - 1, (long)floor((maximum_x - navm.x0) / NAVM_CELL));
		cy0 = MAX(0, (long)floor((minimum_y - navm.y0) / NAVM_CELL));
		cy1 = MIN(navm.height - 1, (long)floor((maximum_y - navm.y0) / NAVM_CELL));
		for (cy = cy0; cy <= cy1; cy++)
		{
			for (cx = cx0; cx <= cx1; cx++)
			{
				real_point3d a[24], b[24];
				real x0 = navm.x0 + cx * NAVM_CELL;
				real y0;
				long n = count;
				long i;
				real low = 1.0e9f, high = -1.0e9f;
				real floor_z = 0.f;
				/* (a part of the map that is not worked out: on to the
				   next block of the row) */
				if (!navm_wanted_at(cx, cy))
				{
					cx |= NAVM_BLOCK - 1;
					continue;
				}
				y0 = navm.y0 + cy * NAVM_CELL;
				memcpy(a, polygon, count * sizeof(real_point3d));
				n = navm_clip(b, a, n, 0, x0, 1.f);
				n = navm_clip(a, b, n, 0, x0 + NAVM_CELL, -1.f);
				n = navm_clip(b, a, n, 1, y0, 1.f);
				n = navm_clip(a, b, n, 1, y0 + NAVM_CELL, -1.f);
				if (n < 3)
					continue;
				for (i = 0; i < n; i++)
				{
					low = MIN(low, a[i].z);
					high = MAX(high, a[i].z);
				}
				if (walkable)
				{
					real mx = x0 + NAVM_CELL * 0.5f;
					real my = y0 + NAVM_CELL * 0.5f;
					floor_z = (plane.d - plane.n.i * mx - plane.n.j * my) / plane.n.k;
					floor_z = PIN(floor_z, low, high);
				}
				navm_add_interval(raster, cx, cy, low, high, floor_z, walkable);
			}
		}
	}
}

static int navm_float_compare(void const *a, void const *b)
{
	float fa = *(float const *)a;
	float fb = *(float const *)b;
	return fa < fb ? -1 : fa > fb ? 1 : 0;
}

/* the floors in each column, and the room over them */
static void navm_build_spans(struct navm_raster *raster)
{
	long cx, cy;
	long capacity = 1 << 20;
	navm.spans = (struct navm_span *)malloc(capacity * sizeof(struct navm_span));
	navm.span_count = 0;
	if (!navm.spans)
	{
		navm_no_memory("the floors");
		return;
	}
	/* (the columns in their order, row by row, whatever blocks they are in:
	   the spans are numbered as they always were) */
	for (cy = 0; cy < navm.height; cy++)
	for (cx = 0; cx < navm.width; cx++)
	{
		float floors[64];
		long floor_count = 0;
		long i;
		long previous = NONE;
		long column = cy * navm.width + cx;
		long first_interval;
		/* (a block nothing passes through: on to the next of the row) */
		if (!raster->first.blocks[navm_block_index(&raster->first, cx, cy)])
		{
			cx |= NAVM_BLOCK - 1;
			continue;
		}
		first_interval = navm_grid_get(&raster->first, cx, cy);
		for (i = first_interval; i != NONE && floor_count < 64; i = raster->intervals[i].next)
			if (raster->intervals[i].walkable)
				floors[floor_count++] = raster->intervals[i].floor;
		if (!floor_count)
			continue;
		qsort(floors, floor_count, sizeof(float), navm_float_compare);
		for (i = 0; i < floor_count; i++)
		{
			float f = floors[i];
			float ceiling = 1.0e9f;
			long j;
			struct navm_span *span;
			/* one floor for several surfaces meeting in the column: the top */
			if (i + 1 < floor_count && floors[i + 1] - f < 0.2f)
				continue;
			for (j = first_interval; j != NONE; j = raster->intervals[j].next)
			{
				struct navm_interval const *interval = &raster->intervals[j];
				if (interval->high <= f + 0.1f)
					continue; /* at or under the floor */
				if (interval->walkable && fabs(interval->floor - f) < 0.25f)
					continue; /* the floor itself, sloping through the column */
				/* something steep that comes no higher than a step over the
				   floor (a kerb, a bump in the rock, the shoulder of a ridge,
				   the foot of a bank): stepped over, it doesn't take the room
				   (a wall's foot has the rest of the wall over it, which does) */
				if (!interval->walkable && interval->high <= f + NAVM_STEP && !navm_old_rules())
					continue;
				ceiling = MIN(ceiling, MAX(interval->low, f));
			}
			if (navm.span_count >= capacity)
			{
				struct navm_span *grown;
				capacity *= 2;
				grown = (struct navm_span *)realloc(navm.spans, capacity * sizeof(struct navm_span));
				if (!grown)
				{
					navm_no_memory("the floors");
					return;
				}
				navm.spans = grown;
			}
			span = &navm.spans[navm.span_count];
			memset(span, 0, sizeof(*span));
			span->floor = f;
			span->ceiling = ceiling;
			span->next = NONE;
			span->column = column;
			if (ceiling - f >= NAVM_HEIGHT)
				span->flags |= _navm_span_walkable;
			if (previous == NONE)
			{
				long *first = navm_grid_slot(&navm.columns, cx, cy);
				if (!first)
				{
					navm_no_memory("the floors' columns");
					return;
				}
				*first = navm.span_count;
			}
			else
				navm.spans[previous].next = navm.span_count;
			previous = navm.span_count++;
		}
	}
}

/* thin things the heightfield misses (it samples each column at its
   middle): railings on posts, open underneath, a bar at chest height, a
   thin post. Every step from one column to the next is tried at knee,
   waist and chest height; one that hits something is no step (a bit per
   direction in the span's pad, both ways) */
static void navm_mark_thin_obstacles(void)
{
	unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
		FLAG(_collision_test_structure_bit); /* (scenery: navm_mark_scenery) */
	static real const heights[3] = { 0.25f, 0.45f, 0.65f };
	long s;
	long marked = 0;
	for (s = 0; s < navm.span_count; s++)
		navm.spans[s].pad = 0;
	for (s = 0; s < navm.span_count; s++)
	{
		struct navm_span *span = &navm.spans[s];
		long d;
		if (!(span->flags & _navm_span_walkable) || (span->flags & _navm_span_blocked))
			continue;
		/* (+x and +y: each link once) */
		for (d = 0; d < 4; d += 2)
		{
			long t = navm_walk_neighbor(s, d);
			real ax, ay, bx, by;
			long h;
			if (t == NONE)
				continue;
			navm_column_center(span->column, &ax, &ay);
			navm_column_center(navm.spans[t].column, &bx, &by);
			for (h = 0; h < 3; h++)
			{
				struct collision_result collision;
				real_point3d from;
				real_vector3d vector;
				from.x = ax;
				from.y = ay;
				from.z = MAX(span->floor, navm.spans[t].floor) + heights[h];
				vector.i = bx - ax;
				vector.j = by - ay;
				vector.k = 0.f;
				if (collision_test_vector(flags, &from, &vector, NONE, &collision))
				{
					span->pad |= (unsigned char)(1 << d);
					navm.spans[t].pad |= (unsigned char)(1 << navm_opposite[d]);
					marked++;
					break;
				}
			}
		}
	}
	if (bot_logging())
		bot_log("navm: %ld steps blocked by thin things (railings, posts)", marked);
}

/* scenery (trees, rocks, crates): where a body can't stand for it */
static void navm_mark_scenery(void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_objects_bit) |
		FLAG(_collision_test_objects_scenery_bit);
	long marked = 0;

	object_iterator_new(&iterator, _object_mask_scenery, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		real_point3d center = object->object.bounding_sphere_center;
		real radius = object->object.bounding_sphere_radius + 0.3f;
		long cx0 = MAX(0, (long)floor((center.x - radius - navm.x0) / NAVM_CELL));
		long cx1 = MIN(navm.width - 1, (long)floor((center.x + radius - navm.x0) / NAVM_CELL));
		long cy0 = MAX(0, (long)floor((center.y - radius - navm.y0) / NAVM_CELL));
		long cy1 = MIN(navm.height - 1, (long)floor((center.y + radius - navm.y0) / NAVM_CELL));
		long cx, cy;
		for (cy = cy0; cy <= cy1; cy++)
		{
			for (cx = cx0; cx <= cx1; cx++)
			{
				long s;
				for (s = navm_first_span_at(cx, cy); s != NONE; s = navm.spans[s].next)
				{
					struct navm_span *span = &navm.spans[s];
					real x, y;
					long h;
					if (!(span->flags & _navm_span_walkable) || span->floor > center.z + radius || span->floor + NAVM_HEIGHT < center.z - radius)
						continue;
					navm_column_center(span->column, &x, &y);
					/* across the column both ways at knee and chest height */
					for (h = 0; h < 2 && !(span->flags & _navm_span_blocked); h++)
					{
						long axis;
						for (axis = 0; axis < 2; axis++)
						{
							struct collision_result collision;
							real_point3d from;
							real_vector3d vector = { 0.f, 0.f, 0.f };
							long way;
							for (way = -1; way <= 1; way += 2)
							{
								from.x = x - (axis == 0 ? way * NAVM_CELL * 0.5f : 0.f);
								from.y = y - (axis == 1 ? way * NAVM_CELL * 0.5f : 0.f);
								from.z = span->floor + (h ? 0.6f : 0.3f);
								vector.i = axis == 0 ? way * NAVM_CELL : 0.f;
								vector.j = axis == 1 ? way * NAVM_CELL : 0.f;
								if (collision_test_vector(flags, &from, &vector, NONE, &collision))
									span->flags |= _navm_span_blocked;
							}
						}
					}
					if (span->flags & _navm_span_blocked)
						marked++;
				}
			}
		}
	}
	if (bot_logging())
		bot_log("navm: %ld spots under scenery", marked);
}

/* which kind of border a span has toward a direction it can't walk: a
   wall (something in the way at body height) or a drop */
static int navm_is_wall(struct navm_raster *raster, long s, long direction)
{
	struct navm_span const *span = &navm.spans[s];
	long x = span->column % navm.width + navm_dx[direction];
	long y = span->column / navm.width + navm_dy[direction];
	long i;
	long t;
	if (x < 0 || y < 0 || x >= navm.width || y >= navm.height)
		return TRUE;
	/* (the edge of a part of the map that is worked out: as the map's own) */
	if (!navm_wanted_at(x, y))
		return TRUE;
	if (direction < 4 && (span->pad & (1 << direction)))
		return TRUE; /* (a railing, a post) */
	/* something passing through at body height (a wall, a rock, a step
	   face too high to walk) */
	for (i = navm_grid_get(&raster->first, x, y); i != NONE; i = raster->intervals[i].next)
	{
		struct navm_interval const *interval = &raster->intervals[i];
		if (interval->high > span->floor + NAVM_STEP && interval->low < span->floor + NAVM_HEIGHT)
			return TRUE;
	}
	/* a floor there at about our height with scenery standing on it: in
	   the way too. (One with no room over it for anything else has that
	   something at body height, found above, or it's low down and no wall:
	   a ridge's shoulder is a drop, not a wall to keep a body's width off) */
	for (t = navm_first_span_at(x, y); t != NONE; t = navm.spans[t].next)
		if (fabs(navm.spans[t].floor - span->floor) <= NAVM_STEP && ((navm.spans[t].flags & _navm_span_blocked) || navm_old_rules()))
			return TRUE;
	return FALSE;
}

/* distances over the grid from the borders (Dijkstra, eight ways) */

static void navm_heap_swap(struct navm_heap *heap, long a, long b)
{
	long t = heap->items[a];
	float k = heap->keys[a];
	heap->items[a] = heap->items[b];
	heap->keys[a] = heap->keys[b];
	heap->items[b] = t;
	heap->keys[b] = k;
}

void navm_heap_push(struct navm_heap *heap, long item, float key)
{
	long i = heap->count++;
	heap->items[i] = item;
	heap->keys[i] = key;
	while (i > 0)
	{
		long parent = (i - 1) / 2;
		if (heap->keys[parent] <= heap->keys[i])
			break;
		navm_heap_swap(heap, parent, i);
		i = parent;
	}
}

long navm_heap_pop(struct navm_heap *heap, float *key)
{
	long top = heap->items[0];
	long i = 0;
	*key = heap->keys[0];
	heap->count--;
	heap->items[0] = heap->items[heap->count];
	heap->keys[0] = heap->keys[heap->count];
	for (;;)
	{
		long l = i * 2 + 1;
		long r = l + 1;
		long m = i;
		if (l < heap->count && heap->keys[l] < heap->keys[m])
			m = l;
		if (r < heap->count && heap->keys[r] < heap->keys[m])
			m = r;
		if (m == i)
			break;
		navm_heap_swap(heap, m, i);
		i = m;
	}
	return top;
}

enum
{
	_navm_distance_wall = 0,
	_navm_distance_edge,
	_navm_distance_vehicle,
};

static void navm_distances(unsigned char border_flag, long kind)
{
	float *distance = (float *)malloc(navm.span_count * sizeof(float));
	struct navm_heap heap;
	long s;
	heap.items = (long *)malloc(navm.span_count * 8 * sizeof(long));
	heap.keys = (float *)malloc(navm.span_count * 8 * sizeof(float));
	heap.count = 0;
	if (!distance || !heap.items || !heap.keys)
	{
		navm_no_memory("the distances to the walls");
		goto done;
	}
	for (s = 0; s < navm.span_count; s++)
	{
		distance[s] = 1.0e9f;
		if (navm.spans[s].flags & border_flag)
		{
			/* the wall's half a cell off the middle of a border cell */
			distance[s] = NAVM_CELL * 0.5f;
			navm_heap_push(&heap, s, distance[s]);
		}
	}
	while (heap.count > 0)
	{
		float key;
		long d;
		s = navm_heap_pop(&heap, &key);
		if (key > distance[s] || key > NAVM_MAXIMUM_CLEARANCE)
			continue;
		for (d = 0; d < 8; d++)
		{
			long t = kind == _navm_distance_vehicle ? navm_vehicle_neighbor(s, d) : navm_walk_neighbor(s, d);
			float through;
			if (t == NONE)
				continue;
			through = key + navm_step_length[d] * NAVM_CELL;
			if (through < distance[t] && heap.count < navm.span_count * 8)
			{
				distance[t] = through;
				navm_heap_push(&heap, t, through);
			}
		}
	}
	for (s = 0; s < navm.span_count; s++)
	{
		float value = MIN(distance[s], NAVM_MAXIMUM_CLEARANCE);
		if (!(navm.spans[s].flags & _navm_span_walkable) || (navm.spans[s].flags & _navm_span_blocked))
			value = 0.f;
		if (kind == _navm_distance_vehicle && navm.spans[s].ceiling - navm.spans[s].floor < NAVM_VEHICLE_HEIGHT)
			value = 0.f;
		if (kind == _navm_distance_wall)
			navm.spans[s].wall = (unsigned short)(value * 100.f);
		else if (kind == _navm_distance_edge)
			navm.spans[s].edge = (unsigned short)(value * 100.f);
		else
			navm.spans[s].vehicle_wall = (unsigned short)(value * 100.f);
	}
done:
	free(distance);
	free(heap.items);
	free(heap.keys);
}

/* ---------- the cache */

void navm_signature(char *map, unsigned long *signature)
{
	struct collision_bsp *bsp = global_collision_bsp_get();
	unsigned long sum = 0;
	long i;
	snprintf(map, 128, "%s", global_scenario_index != NONE ? tag_get_name(global_scenario_index) : "none");
	/* One of Halo PC's maps (<name>@ce, <name>@md) goes by the name it is
	   played under, its file's: the scenario inside it may be named as one
	   of the Xbox's is (Halo PC's own bloodgulch), or not at all (a
	   protected map's is "<protected>"), and the bake looks for its files
	   by the played name (bot_bake.c). */
	{
		extern char const *game_options_map_name(void);
		char const *played = game_options_map_name();
		char const *last = strrchr(played, '\\');
		if (strchr(played, '@'))
			snprintf(map, 128, "%s", last ? last + 1 : played);
	}
	memset(signature, 0, 4 * sizeof(unsigned long));
	if (!bsp)
		return;
	signature[0] = (unsigned long)bsp->surfaces.count;
	signature[1] = (unsigned long)bsp->edges.count;
	signature[2] = (unsigned long)bsp->vertices.count;
	for (i = 0; i < bsp->vertices.count; i += 5)
	{
		struct collision_vertex const *vertex = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, i, struct collision_vertex);
		unsigned long words[3];
		memcpy(words, &vertex->point, sizeof(words));
		sum = sum * 31 + words[0] + words[1] * 7 + words[2] * 13;
	}
	signature[3] = sum;
}

/* Where a map's navigation is kept. Baked, with the game data
   (d:\navcache, next to maps: made when the game is built, bot_bake.c), or
   worked out by this machine the first time the map had bots (u:\navcache,
   in the save folder). A map's baked files are read before its own. */
static boolean navm_baking;

void navm_set_baking(boolean baking)
{
	navm_baking = baking;
}

boolean navm_is_baking(void)
{
	return navm_baking;
}

/* (the files baking has written whole) */
static long navm_baked_file_count;

void navm_baked_file(boolean whole, char const *path)
{
	if (!navm_baking)
		return;
	if (whole)
		navm_baked_file_count++;
	else
		bot_log("navm: could not write %s", path);
}

long navm_baked_files(void)
{
	return navm_baked_file_count;
}

void navm_cache_file(char const *map, long bsp_index, char const *extension, boolean baked, char *path, long size)
{
	char name[128];
	char const *last = strrchr(map, '\\');
	char const *root = baked ? "d:\\navcache" : "u:\\navcache";
	long i;
	snprintf(name, sizeof(name), "%s", last ? last + 1 : map);
	for (i = 0; name[i]; i++)
		if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= '0' && name[i] <= '9') || name[i] == '_'))
			name[i] = '_';
	/* (the game data's folder is only written to when baking) */
	if (!baked || navm_baking)
	{
		struct file_reference folder;
		file_reference_create_from_path(&folder, root, TRUE);
		if (!file_exists(&folder))
			file_create(&folder);
	}
	/* (a campaign level has a few structure BSPs: one each) */
	snprintf(path, size, "%s\\%s_%d.%s", root, name, (int)bsp_index, extension);
}

/* How many structure BSPs a map has, kept beside its baked navigation when
   every one of them has been baked (<map>.bsps, a number): what "is this
   map baked" needs to know of a map that is not loaded, and its files
   alone do not say (a second BSP's that are not there look the same as a
   map with one BSP). 0: not kept, a bake from before this was. */
static void navm_baked_bsps_path(char const *map, char *path, long size)
{
	char *end;
	navm_cache_file(map, 0, "bsps", TRUE, path, size);
	/* (<map>_0.bsps as the others are named: the map's, not a BSP's) */
	end = strstr(path, "_0.bsps");
	if (end && !end[7])
		snprintf(end, size - (end - path), ".bsps");
}

long navm_baked_bsps(char const *map_name)
{
	char path[512];
	FILE *file;
	long count = 0;

	navm_baked_bsps_path(map_name, path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return 0;
	if (fscanf(file, "%ld", &count) != 1 || count < 1 || count > 32)
		count = 0;
	fclose(file);
	return count;
}

boolean navm_baked_bsps_write(char const *map_name, long count)
{
	char path[512];
	FILE *file;
	boolean whole;

	navm_baked_bsps_path(map_name, path, sizeof(path));
	file = fopen(path, "wb");
	if (!file)
		return FALSE;
	whole = fprintf(file, "%ld\n", count) > 0;
	return fclose(file) == 0 && whole;
}

/* is there a baked file of a map's for this structure BSP at all, good or not */
boolean navm_baked_exists(char const *map_name, long bsp_index)
{
	char path[512];
	FILE *file;

	navm_cache_file(map_name, bsp_index, "height", TRUE, path, sizeof(path));
	file = fopen(path, "rb");
	if (file)
		fclose(file);
	return file != NULL;
}

void navm_cache_path(char const *map, char const *extension, boolean baked, char *path, long size)
{
	navm_cache_file(map, global_structure_bsp_index, extension, baked, path, size);
}

struct navm_cache_header
{
	unsigned long magic;
	unsigned long version;
	unsigned long span_size;
	char map[128];
	unsigned long signature[4];
	float x0, y0;
	long width, height;
	long span_count;
	long climbable_count;
	float cell;
};

/* whether a header is of a file of this build's kind */
static boolean navm_cache_header_current(struct navm_cache_header const *header)
{
	return header->magic == NAVM_CACHE_MAGIC &&
		(header->version == NAVM_CACHE_VERSION || header->version == NAVM_CACHE_VERSION_BLOCKS) &&
		header->span_size == sizeof(struct navm_span);
}

/* every column, a row at a time */
static boolean navm_cache_read_columns(FILE *file)
{
	long *row = (long *)malloc(navm.width * sizeof(long));
	long x, y;
	boolean ok = row != NULL;
	for (y = 0; ok && y < navm.height; y++)
	{
		ok = fread(row, sizeof(long), (size_t)navm.width, file) == (size_t)navm.width;
		for (x = 0; ok && x < navm.width; x++)
		{
			if (row[x] != NONE)
			{
				long *first = navm_grid_slot(&navm.columns, x, y);
				ok = first && row[x] >= 0 && row[x] < navm.span_count;
				if (ok)
					*first = row[x];
			}
		}
	}
	free(row);
	return ok;
}

/* the blocks that are worked out: how many, then each one's place and its
   columns */
static boolean navm_cache_read_blocks(FILE *file)
{
	long block_count = 0;
	long total = navm.columns.across * navm.columns.down;
	long b;
	boolean ok = fread(&block_count, sizeof(block_count), 1, file) == 1 && block_count >= 0 && block_count <= total;
	navm.wanted = ok ? (unsigned char *)calloc((size_t)total, 1) : NULL;
	ok = ok && navm.wanted != NULL;
	for (b = 0; ok && b < block_count; b++)
	{
		long index = NONE;
		long *block;
		long c;
		ok = fread(&index, sizeof(index), 1, file) == 1 && index >= 0 && index < total && !navm.wanted[index];
		block = ok ? navm_grid_block(&navm.columns, index) : NULL;
		ok = block && fread(block, sizeof(long), NAVM_BLOCK * NAVM_BLOCK, file) == NAVM_BLOCK * NAVM_BLOCK;
		for (c = 0; ok && c < NAVM_BLOCK * NAVM_BLOCK; c++)
			ok = block[c] == NONE || (block[c] >= 0 && block[c] < navm.span_count);
		if (ok)
			navm.wanted[index] = TRUE;
	}
	return ok;
}

/* A damaged file is not used at all. Each floor is in a column of the grid,
   and the next floor of its column comes after it in the file and is in the
   same column (as they are written: a chain that came back on itself would
   be walked for ever). */
static boolean navm_cache_check_spans(void)
{
	boolean ok = TRUE;
	long c;
	long b;
	for (c = 0; ok && c < navm.span_count; c++)
	{
		struct navm_span const *span = &navm.spans[c];
		ok = span->column >= 0 && span->column / navm.width < navm.height &&
			(span->next == NONE || (span->next > c && span->next < navm.span_count && navm.spans[span->next].column == span->column));
	}
	/* ... and each column's first floor is a floor of that column */
	for (b = 0; ok && b < navm.columns.across * navm.columns.down; b++)
	{
		long const *block = navm.columns.blocks[b];
		long x0 = (b % navm.columns.across) << NAVM_BLOCK_SHIFT;
		long y0 = (b / navm.columns.across) << NAVM_BLOCK_SHIFT;
		long i;
		if (!block)
			continue;
		for (i = 0; ok && i < NAVM_BLOCK * NAVM_BLOCK; i++)
		{
			long x = x0 + (i & (NAVM_BLOCK - 1));
			long y = y0 + (i >> NAVM_BLOCK_SHIFT);
			if (block[i] != NONE)
				ok = x < navm.width && y < navm.height && navm.spans[block[i]].column == y * navm.width + x;
		}
	}
	return ok;
}

/* the heightfield from a cache file of this map (its signature): TRUE when
   it is whole */
static boolean navm_cache_read(FILE *file, char const *map, unsigned long const *signature)
{
	struct navm_cache_header header;
	boolean ok;

	ok = fread(&header, sizeof(header), 1, file) == 1 && navm_cache_header_current(&header) &&
		!strncmp(header.map, map, sizeof(header.map)) &&
		memchr(header.map, 0, sizeof(header.map)) && !memcmp(header.signature, signature, 4 * sizeof(unsigned long)) &&
		header.width > 0 && header.height > 0 && header.width <= NAVM_MAXIMUM_SIDE && header.height <= NAVM_MAXIMUM_SIDE &&
		/* (the two sides are in range by now, and their product is worked
		   out in 64 bits: never one that has wrapped round to a small number) */
		(header.version == NAVM_CACHE_VERSION_BLOCKS ||
			(unsigned long long)header.width * (unsigned long long)header.height < 64ULL * 1024ULL * 1024ULL) &&
		header.climbable_count >= 0 && header.climbable_count <= NAVM_MAXIMUM_CLIMBABLES &&
		header.span_count >= 0 && header.span_count < 64 * 1024 * 1024 && /* (before anything's sized by it) */
		header.cell >= NAVM_CELL_FINEST * 0.99f && header.cell <= NAVM_CELL_COARSEST * 1.01f &&
		(!getenv("HALO_BOT_CELL") || fabs(header.cell - atof(getenv("HALO_BOT_CELL"))) < 0.001f);
	if (ok)
	{
		navm.cell = header.cell;
		navm.x0 = header.x0;
		navm.y0 = header.y0;
		navm.width = header.width;
		navm.height = header.height;
		navm.span_count = header.span_count;
		navm.climbable_count = header.climbable_count;
		navm.spans = (struct navm_span *)malloc(MAX(1, header.span_count) * sizeof(struct navm_span));
		ok = navm_grid_new(&navm.columns, navm.width, navm.height) && navm.spans != NULL;
		/* (a damaged file: every index in range, or build afresh) */
		if (ok)
			ok = header.version == NAVM_CACHE_VERSION ? navm_cache_read_columns(file) : navm_cache_read_blocks(file);
		ok = ok &&
			fread(navm.spans, sizeof(struct navm_span), (size_t)header.span_count, file) == (size_t)header.span_count &&
			fread(navm.climbables, sizeof(struct navm_climbable), (size_t)header.climbable_count, file) == (size_t)header.climbable_count;
		if (ok)
			ok = navm_cache_check_spans();
	}
	return ok;
}

static boolean navm_cache_load(void)
{
	char map[128];
	unsigned long signature[4];
	long attempt;

	/* (baking: worked out afresh) */
	if (getenv("HALO_BOT_NOCACHE") || navm_baking)
		return FALSE;
	navm_signature(map, signature);
	/* the baked file, then this machine's own */
	for (attempt = 0; attempt < 2; attempt++)
	{
		char path[512];
		FILE *file;

		navm_cache_path(map, "height", attempt == 0, path, sizeof(path));
		file = fopen(path, "rb");
		if (!file)
			continue;
		if (navm_cache_read(file, map, signature))
		{
			fclose(file);
			if (bot_logging())
				bot_log("navm: read %s", path);
			return TRUE;
		}
		fclose(file);
		navm_dispose();
	}
	return FALSE;
}

/* whether the map has a baked heightfield of this build's kind (its header
   only: whether it is this very map's is found when the map is loaded) */
boolean navm_baked_current(char const *map_name, long bsp_index)
{
	struct navm_cache_header header;
	char path[512];
	FILE *file;
	boolean ok;

	navm_cache_file(map_name, bsp_index, "height", TRUE, path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return FALSE;
	ok = fread(&header, sizeof(header), 1, file) == 1 && navm_cache_header_current(&header);
	/* (and all of it there) */
	if (ok)
	{
		double size = sizeof(header) + (double)header.span_count * sizeof(struct navm_span) +
			(double)header.climbable_count * sizeof(struct navm_climbable);
		if (header.version == NAVM_CACHE_VERSION)
			size += (double)header.width * header.height * sizeof(long);
		else
		{
			long block_count = 0;
			ok = fread(&block_count, sizeof(block_count), 1, file) == 1 && block_count >= 0;
			size += sizeof(block_count) + (double)block_count * (sizeof(long) + NAVM_BLOCK * NAVM_BLOCK * sizeof(long));
		}
		ok = ok && fseek(file, 0, SEEK_END) == 0 && (double)ftell(file) == size;
	}
	fclose(file);
	return ok;
}

/* every column, a row at a time (as the file always was) */
static boolean navm_cache_write_columns(FILE *file)
{
	long *row = (long *)malloc(navm.width * sizeof(long));
	long x, y;
	boolean whole = row != NULL;
	for (y = 0; whole && y < navm.height; y++)
	{
		for (x = 0; x < navm.width; x++)
			row[x] = navm_first_span_at(x, y);
		whole = fwrite(row, sizeof(long), (size_t)navm.width, file) == (size_t)navm.width;
	}
	free(row);
	return whole;
}

/* the blocks that are worked out */
static boolean navm_cache_write_blocks(FILE *file)
{
	long total = navm.columns.across * navm.columns.down;
	long block_count = 0;
	long b;
	boolean whole;
	for (b = 0; b < total; b++)
		block_count += navm.wanted[b] != 0;
	whole = fwrite(&block_count, sizeof(block_count), 1, file) == 1;
	for (b = 0; whole && b < total; b++)
	{
		long *block;
		if (!navm.wanted[b])
			continue;
		block = navm_grid_block(&navm.columns, b);
		whole = block && fwrite(&b, sizeof(b), 1, file) == 1 &&
			fwrite(block, sizeof(long), NAVM_BLOCK * NAVM_BLOCK, file) == NAVM_BLOCK * NAVM_BLOCK;
	}
	return whole;
}

static void navm_cache_save(void)
{
	struct navm_cache_header header;
	char path[512];
	FILE *file;
	boolean whole;
	memset(&header, 0, sizeof(header));
	header.magic = NAVM_CACHE_MAGIC;
	header.version = navm.wanted ? NAVM_CACHE_VERSION_BLOCKS : NAVM_CACHE_VERSION;
	header.span_size = sizeof(struct navm_span);
	navm_signature(header.map, header.signature);
	header.x0 = navm.x0;
	header.y0 = navm.y0;
	header.width = navm.width;
	header.height = navm.height;
	header.span_count = navm.span_count;
	header.climbable_count = navm.climbable_count;
	header.cell = navm.cell;
	navm_cache_path(header.map, "height", navm_baking, path, sizeof(path));
	file = fopen(path, "wb");
	if (!file)
	{
		navm_baked_file(FALSE, path);
		return;
	}
	whole = fwrite(&header, sizeof(header), 1, file) == 1;
	if (whole)
		whole = navm.wanted ? navm_cache_write_blocks(file) : navm_cache_write_columns(file);
	whole = whole &&
		fwrite(navm.spans, sizeof(struct navm_span), (size_t)navm.span_count, file) == (size_t)navm.span_count &&
		fwrite(navm.climbables, sizeof(struct navm_climbable), (size_t)navm.climbable_count, file) == (size_t)navm.climbable_count;
	whole = fclose(file) == 0 && whole;
	navm_baked_file(whole, path);
	if (bot_logging())
		bot_log("navm: saved %s", path);
}

/* ---------- public */

void navm_dispose(void)
{
	navm_grid_free(&navm.columns);
	free(navm.wanted);
	free(navm.spans);
	memset(&navm, 0, sizeof(navm));
	navm.cell = NAVM_CELL_COARSEST; /* (never nought: it's divided by) */
}

/* the most columns a map's grid may have with all of them worked out (a
   long each, twice over meanwhile); a larger map has parts of it worked out
   (navm_choose_parts) */
#define NAVM_MAXIMUM_COLUMNS (48 * 1024 * 1024)

/* A map so large that its floors are worked out only in parts (or not at
   all): what goes between the parts on the ground goes across country
   (bot_country.c). */
boolean bot_nav_in_parts(void)
{
	return navm_map_too_large() || (navm.width > 0 && (double)navm.width * (double)navm.height > (double)NAVM_MAXIMUM_COLUMNS);
}

/* the last map's grid would have had more: it has no navigation */
static boolean navm_too_large;

boolean navm_map_too_large(void)
{
	return navm_too_large;
}

boolean navm_in_parts(void)
{
	return navm.ready && navm.wanted != NULL;
}

/* borders: walls and drops; for vehicles, walls and anything they can't
   roll over (a step, a low ceiling) */
static void navm_mark_borders(struct navm_raster *raster)
{
	long s;
	for (s = 0; s < navm.span_count; s++)
	{
		long d;
		if (!(navm.spans[s].flags & _navm_span_walkable) || (navm.spans[s].flags & _navm_span_blocked))
			continue;
		for (d = 0; d < 8; d++)
		{
			long t = navm_walk_neighbor(s, d);
			if (t == NONE)
			{
				if (navm_is_wall(raster, s, d))
					navm.spans[s].flags |= _navm_span_wall_border | _navm_span_vehicle_border;
				else
					navm.spans[s].flags |= _navm_span_edge_border;
			}
			else if (navm_vehicle_neighbor(s, d) == NONE)
				navm.spans[s].flags |= _navm_span_vehicle_border;
		}
	}
}

/* the grid: as fine as keeps the map to a sensible number of floors (the
   time to work it all out, and the memory, go by that) */
static void navm_choose_cell(struct collision_bsp *bsp, real minimum_x, real minimum_y, real maximum_x, real maximum_y)
{
	real extent = navm_floor_extent(bsp, minimum_x, minimum_y, maximum_x, maximum_y);
	navm.cell = (real)sqrt(MAX(extent, 1.f) / NAVM_FLOOR_BUDGET);
	navm.cell = (real)ceil(navm.cell * 200.f - 0.001f) / 200.f; /* (to 0.005) */
	navm.cell = PIN(navm.cell, NAVM_CELL_FINEST, NAVM_CELL_COARSEST);
	if (getenv("HALO_BOT_CELL")) /* (testing) */
		navm.cell = PIN((real)atof(getenv("HALO_BOT_CELL")), NAVM_CELL_FINEST, NAVM_CELL_COARSEST);
}

boolean navm_build(void)
{
	struct collision_bsp *bsp = global_collision_bsp_get();
	unsigned long long start = bot_time_microseconds();
	struct navm_raster raster;
	real minimum_x = 1.0e9f, maximum_x = -1.0e9f, minimum_y = 1.0e9f, maximum_y = -1.0e9f;
	long i;

	navm_dispose();
	navm_too_large = FALSE;
	navm_build_failed = FALSE;
	if (!bsp)
		return FALSE;
	if (navm_cache_load())
	{
		navm.ready = TRUE;
		return TRUE;
	}
	for (i = 0; i < bsp->vertices.count; i++)
	{
		struct collision_vertex const *vertex = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, i, struct collision_vertex);
		minimum_x = MIN(minimum_x, vertex->point.x);
		maximum_x = MAX(maximum_x, vertex->point.x);
		minimum_y = MIN(minimum_y, vertex->point.y);
		maximum_y = MAX(maximum_y, vertex->point.y);
	}
	/* a multiplayer map: only as far as people go, which is round where
	   they start and what's there to fight over. (Boarding Action's two
	   ships are five times as long as the part of them that's played in.)
	   What's left out needn't be worked out, and what's kept gets a finer
	   grid for it */
	navm_play_bounds(&minimum_x, &minimum_y, &maximum_x, &maximum_y);
	navm.x0 = (float)floor(minimum_x) - 1.f;
	navm.y0 = (float)floor(minimum_y) - 1.f;
	navm_choose_cell(bsp, minimum_x, minimum_y, maximum_x, maximum_y);
	navm.width = (long)((maximum_x - navm.x0) / NAVM_CELL) + 2;
	navm.height = (long)((maximum_y - navm.y0) / NAVM_CELL) + 2;
	if (navm.width > NAVM_MAXIMUM_SIDE || navm.height > NAVM_MAXIMUM_SIDE)
	{
		/* (beyond anything sensible: ten miles a side) */
		navm_too_large = TRUE;
		bot_log("navm: this map is too large for the bots' navigation (%.0f by %.0f world units): the bots play it without",
			maximum_x - minimum_x, maximum_y - minimum_y);
		return FALSE;
	}
	memset(&raster, 0, sizeof(raster));
	if (!navm_grid_new(&navm.columns, navm.width, navm.height) || !navm_grid_new(&raster.first, navm.width, navm.height))
	{
		navm_grid_free(&raster.first);
		navm_dispose();
		return FALSE;
	}
	/* A map a mile or two across (made for aircraft: Coldsnap, Hugeass) is
	   eighty times Blood Gulch, nearly all of it open ground nobody walks:
	   ten minutes on foot from one end to the other. Its floors are worked
	   out only round where it is played (where players start, the weapons,
	   the vehicles, the flags), each such part as fine as a small map's;
	   the rest is left out, and crossed by air (bot_flight.c). */
	if (navm.width * navm.height > NAVM_MAXIMUM_COLUMNS && !navm_choose_parts())
	{
		navm_too_large = TRUE;
		bot_log("navm: this map is too large for the bots' navigation (%.0f by %.0f world units) and has nowhere marked to play in: the bots play it without",
			maximum_x - minimum_x, maximum_y - minimum_y);
		navm_grid_free(&raster.first);
		navm_dispose();
		return FALSE;
	}

	navm_rasterize_bsp(&raster, bsp);
	if (!navm_build_failed)
		navm_build_spans(&raster);
	if (navm_build_failed)
	{
		/* (not half a map: none of it) */
		navm_grid_free(&raster.first);
		free(raster.intervals);
		navm_dispose();
		return FALSE;
	}
	navm_mark_scenery();
	navm_mark_thin_obstacles();
	navm_mark_borders(&raster);
	navm_grid_free(&raster.first);
	free(raster.intervals);
	navm_distances(_navm_span_wall_border, _navm_distance_wall);
	navm_distances(_navm_span_edge_border, _navm_distance_edge);
	navm_distances(_navm_span_vehicle_border, _navm_distance_vehicle);
	if (navm_build_failed)
	{
		navm_dispose();
		return FALSE;
	}
	navm.ready = navm.span_count > 0;
	if (bot_logging())
		bot_log("navm: %ld x %ld columns of %.3f, %ld floors, %ld climbable faces, %.1f s", navm.width, navm.height, navm.cell, navm.span_count,
			navm.climbable_count, (bot_time_microseconds() - start) / 1.0e6);
	if (navm.ready)
		navm_cache_save();
	return navm.ready;
}

real navm_wall_clearance(real_point3d const *point, real *floor_z)
{
	long s;
	if (!navm.ready)
		return -1.f;
	s = navm_span_at(point, 0.5f, 1.2f);
	if (s == NONE || !(navm.spans[s].flags & _navm_span_walkable))
		return -1.f;
	if (floor_z)
		*floor_z = navm.spans[s].floor;
	return navm.spans[s].wall / 100.f;
}

/* Whether a body gets from one floor to the next column's, which the
   neighbours (navm_walk_neighbor) and the straight lines below both go by:
   room for it through the way, and nothing thin between the two (a
   railing, a post: navm_mark_thin_obstacles). Floors not side by side (the
   same column, or one further off over a slot in the floor) have nothing
   known between them. */
static boolean navm_transition_open(long from_span, long to_span)
{
	struct navm_span const *from = &navm.spans[from_span];
	struct navm_span const *to = &navm.spans[to_span];
	long dx = to->column % navm.width - from->column % navm.width;
	long dy = to->column / navm.width - from->column / navm.width;
	long x_side = dx > 0 ? 0 : 1;
	long y_side = dy > 0 ? 2 : 3;

	if ((dx == 0 && dy == 0) || dx < -1 || dx > 1 || dy < -1 || dy > 1)
		return TRUE;
	if (MIN(from->ceiling, to->ceiling) - MAX(from->floor, to->floor) < NAVM_HEIGHT)
		return FALSE;
	if (dx != 0 && ((from->pad & (1 << x_side)) || (to->pad & (1 << navm_opposite[x_side]))))
		return FALSE;
	if (dy != 0 && ((from->pad & (1 << y_side)) || (to->pad & (1 << navm_opposite[y_side]))))
		return FALSE;
	/* (Cornerwise, the neighbours' rule, navm_walk_neighbor, is that both
	   sides be open too. A line does not take that rule whole, and does
	   not need to. A side shut by a wall makes the floor stepped onto a
	   wall's border, 0.05 from it, which any wall_min of a hand's width
	   refuses as it is; a side with no floor, by a drop, is the corner of
	   a ledge, which a body does cut across and every jump's run-up
	   starts on, and how near a drop a line may go is its edge_min. Only
	   a line asked for with no clearance at all goes past a wall's corner:
	   the areas' check that a jump has floor behind it to run on, which is
	   asking no more than that. With the neighbours' rule there, a map
	   loses a few hundred jumps from ledges that have a wall anywhere by
	   them: 241 of Wizard's.) */
	return TRUE;
}

/* Along the floor from one point toward another, a floor at a time. The
   line is the one on the map from `from` to `to`: the floor it starts on
   is the one under `from`, and each floor after is the one a step or less
   from the last. The height of `to` is not read (most callers ask "is
   there floor that way", with a point that has no height of its own); the
   floor the line ends on is given back for those that mean a place
   (navm_line_arrives). over_slots: a hand's width of missing floor is
   stepped over. */
static boolean navm_line_follow(real_point3d const *from, real_point3d const *to, real wall_min, real edge_min, boolean over_slots, long *end_span)
{
	real dx = to->x - from->x;
	real dy = to->y - from->y;
	real length = (real)sqrt(dx * dx + dy * dy);
	long steps = (long)(length / (NAVM_CELL * 0.5f)) + 1;
	unsigned short wall = (unsigned short)(wall_min * 100.f);
	unsigned short edge = (unsigned short)(edge_min * 100.f);
	real missing = 0.f;
	real z;
	long i;
	long s;

	if (!navm.ready)
		return FALSE;
	s = navm_span_at(from, 0.3f, 1.2f);
	if (s == NONE)
		return FALSE;
	z = navm.spans[s].floor;
	for (i = 1; i <= steps; i++)
	{
		real t = (real)i / (real)steps;
		long column = navm_column_of(from->x + dx * t, from->y + dy * t);
		long u;
		long best = NONE;
		real best_rise = 1.0e9f;
		if (column == NONE)
			return FALSE;
		for (u = navm_first_span(column); u != NONE; u = navm.spans[u].next)
		{
			real rise = navm.spans[u].floor - z;
			if (fabs(rise) <= NAVM_STEP && fabs(rise) < best_rise)
			{
				best_rise = (real)fabs(rise);
				best = u;
			}
		}
		if (best == NONE)
		{
			if (!over_slots)
				return FALSE;
			missing += length / (real)steps;
			if (missing > 0.16f)
				return FALSE;
			continue;
		}
		missing = 0.f;
		if (!(navm.spans[best].flags & _navm_span_walkable) || (navm.spans[best].flags & _navm_span_blocked) ||
			navm.spans[best].wall < wall || navm.spans[best].edge < edge)
		{
			return FALSE;
		}
		if (best != s && !navm_transition_open(s, best))
			return FALSE;
		s = best;
		z = navm.spans[best].floor;
	}
	if (end_span)
		*end_span = s;
	return missing == 0.f;
}

/* walking straight from one point toward another over the floor: never up
   or down more than a step at once, at least wall_min from walls and
   edge_min from drops all the way, through nothing a neighbour's way is
   shut by */
boolean navm_line_walkable(real_point3d const *from, real_point3d const *to, real wall_min, real edge_min)
{
	return navm_line_follow(from, to, wall_min, edge_min, FALSE, NULL);
}

/* ... to a place: the floor the line ends on is the very one `to` is on
   or just over (the floor navm_span_at finds for the point), not another
   under or over it that happens to be about as high */
boolean navm_line_arrives(real_point3d const *from, real_point3d const *to, real wall_min, real edge_min, boolean over_slots)
{
	long end_span = NONE;
	if (!navm_line_follow(from, to, wall_min, edge_min, over_slots, &end_span))
		return FALSE;
	return end_span != NONE && end_span == navm_span_at(to, 0.3f, 1.2f);
}

int bot_logging(void)
{
	static int logging = -1;
	if (logging < 0)
		logging = getenv("HALO_BOT_LOG") != NULL;
	return logging;
}

int bot_env_set(char const *name)
{
	/* (the few debugging switches: read once each) */
	static char const *names[8];
	static int values[8];
	int i;
	for (i = 0; i < 8 && names[i]; i++)
		if (!strcmp(names[i], name))
			return values[i];
	if (i == 8)
		return getenv(name) != NULL;
	names[i] = name;
	values[i] = getenv(name) != NULL;
	return values[i];
}
