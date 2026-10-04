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

#define NAVM_CACHE_VERSION 5
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

/* ---------- small things */

long navm_column_of(real x, real y)
{
	long cx, cy;
	if (!navm.columns || navm.cell <= 0.f)
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
	for (t = navm.columns[y * navm.width + x]; t != NONE; t = navm.spans[t].next)
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
	for (s = navm.columns[column]; s != NONE; s = navm.spans[s].next)
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

struct navm_raster
{
	long *first; /* per column: first interval */
	struct navm_interval *intervals;
	long count;
	long capacity;
};

static void navm_add_interval(struct navm_raster *raster, long column, real low, real high, real floor_z, int walkable)
{
	struct navm_interval *interval;
	if (raster->count >= raster->capacity)
	{
		long capacity = raster->capacity ? raster->capacity * 2 : (1 << 20);
		struct navm_interval *grown = (struct navm_interval *)realloc(raster->intervals, capacity * sizeof(struct navm_interval));
		if (!grown)
			return;
		raster->intervals = grown;
		raster->capacity = capacity;
	}
	interval = &raster->intervals[raster->count];
	interval->low = low;
	interval->high = high;
	interval->floor = floor_z;
	interval->walkable = (unsigned char)walkable;
	interval->next = raster->first[column];
	raster->first[column] = raster->count++;
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
		/* scenario_netgame_equipment (game_engine.c): position at 0x40 */
		real_point3d const *point = (real_point3d const *)((byte *)xbox_pointer(scenario->netgame_equipment.address) + i * 0x90 + 0x40);
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
		/* a ladder's face (climbable, upright): kept, the faces of one
		   ladder together */
		if ((surface->flags & (1 << 2)) && sqrt(plane.n.i * plane.n.i + plane.n.j * plane.n.j) > 0.6f)
		{
			real horizontal = (real)sqrt(plane.n.i * plane.n.i + plane.n.j * plane.n.j);
			real bottom = 1.0e9f, top = -1.0e9f, sx = 0.f, sy = 0.f, width = 0.f;
			long l;
			for (cx = 0; cx < count; cx++)
			{
				bottom = MIN(bottom, polygon[cx].z);
				top = MAX(top, polygon[cx].z);
				sx += polygon[cx].x / count;
				sy += polygon[cx].y / count;
			}
			for (cx = 0; cx < count; cx++)
			{
				/* along the face */
				real along = (real)fabs((polygon[cx].x - sx) * -plane.n.j / horizontal + (polygon[cx].y - sy) * plane.n.i / horizontal);
				width = MAX(width, along);
			}
			for (l = 0; l < navm.climbable_count; l++)
			{
				struct navm_climbable *climbable = &navm.climbables[l];
				real dx = sx - climbable->center.x;
				real dy = sy - climbable->center.y;
				if (climbable->normal_x * plane.n.i / horizontal + climbable->normal_y * plane.n.j / horizontal > 0.9f &&
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
				climbable->normal_x = plane.n.i / horizontal;
				climbable->normal_y = plane.n.j / horizontal;
				climbable->bottom_z = bottom;
				climbable->top_z = top;
				climbable->half_width = width;
			}
		}
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
				real y0 = navm.y0 + cy * NAVM_CELL;
				long n = count;
				long i;
				real low = 1.0e9f, high = -1.0e9f;
				real floor_z = 0.f;
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
				navm_add_interval(raster, cy * navm.width + cx, low, high, floor_z, walkable);
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
	long column;
	long capacity = 1 << 20;
	navm.spans = (struct navm_span *)malloc(capacity * sizeof(struct navm_span));
	navm.span_count = 0;
	/* (every column empty to start with: a build cut short by memory
	   leaves no garbage) */
	for (column = 0; column < navm.width * navm.height; column++)
		navm.columns[column] = NONE;
	if (!navm.spans)
		return;
	for (column = 0; column < navm.width * navm.height; column++)
	{
		float floors[64];
		long floor_count = 0;
		long i;
		long previous = NONE;
		navm.columns[column] = NONE;
		for (i = raster->first[column]; i != NONE && floor_count < 64; i = raster->intervals[i].next)
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
			for (j = raster->first[column]; j != NONE; j = raster->intervals[j].next)
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
					return;
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
				navm.columns[column] = navm.span_count;
			else
				navm.spans[previous].next = navm.span_count;
			previous = navm.span_count++;
		}
	}
}

/* scenery (trees, rocks, crates): where a body can't stand for it */
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
	static long const opposite[4] = { 1, 0, 3, 2 };
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
					navm.spans[t].pad |= (unsigned char)(1 << opposite[d]);
					marked++;
					break;
				}
			}
		}
	}
	if (bot_logging())
		bot_log("navm: %ld steps blocked by thin things (railings, posts)", marked);
}

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
				for (s = navm.columns[cy * navm.width + cx]; s != NONE; s = navm.spans[s].next)
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
	if (direction < 4 && (span->pad & (1 << direction)))
		return TRUE; /* (a railing, a post) */
	/* something passing through at body height (a wall, a rock, a step
	   face too high to walk) */
	for (i = raster->first[y * navm.width + x]; i != NONE; i = raster->intervals[i].next)
	{
		struct navm_interval const *interval = &raster->intervals[i];
		if (interval->high > span->floor + NAVM_STEP && interval->low < span->floor + NAVM_HEIGHT)
			return TRUE;
	}
	/* a floor there at about our height with scenery standing on it: in
	   the way too. (One with no room over it for anything else has that
	   something at body height, found above, or it's low down and no wall:
	   a ridge's shoulder is a drop, not a wall to keep a body's width off) */
	for (t = navm.columns[y * navm.width + x]; t != NONE; t = navm.spans[t].next)
		if (fabs(navm.spans[t].floor - span->floor) <= NAVM_STEP && ((navm.spans[t].flags & _navm_span_blocked) || navm_old_rules()))
			return TRUE;
	return FALSE;
}

/* distances over the grid from the borders (Dijkstra, eight ways) */

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
		{
			long t = heap->items[parent];
			float k = heap->keys[parent];
			heap->items[parent] = heap->items[i];
			heap->keys[parent] = heap->keys[i];
			heap->items[i] = t;
			heap->keys[i] = k;
		}
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
		{
			long t = heap->items[m];
			float k = heap->keys[m];
			heap->items[m] = heap->items[i];
			heap->keys[m] = heap->keys[i];
			heap->items[i] = t;
			heap->keys[i] = k;
		}
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
		goto done;
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

/* the heightfield from a cache file of this map (its signature): TRUE when
   it is whole */
static boolean navm_cache_read(FILE *file, char const *map, unsigned long const *signature)
{
	struct navm_cache_header header;
	boolean ok;

	ok = fread(&header, sizeof(header), 1, file) == 1 && header.magic == NAVM_CACHE_MAGIC && header.version == NAVM_CACHE_VERSION &&
		header.span_size == sizeof(struct navm_span) && !strcmp(header.map, map) && !memcmp(header.signature, signature, 4 * sizeof(unsigned long)) &&
		header.width > 0 && header.height > 0 && header.width * header.height < 64 * 1024 * 1024 &&
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
		navm.columns = (long *)malloc(navm.width * navm.height * sizeof(long));
		navm.spans = (struct navm_span *)malloc(MAX(1, header.span_count) * sizeof(struct navm_span));
		ok = navm.columns && navm.spans &&
			fread(navm.columns, sizeof(long), (size_t)(navm.width * navm.height), file) == (size_t)(navm.width * navm.height) &&
			fread(navm.spans, sizeof(struct navm_span), (size_t)header.span_count, file) == (size_t)header.span_count &&
			fread(navm.climbables, sizeof(struct navm_climbable), (size_t)header.climbable_count, file) == (size_t)header.climbable_count;
		navm.span_count = header.span_count;
		navm.climbable_count = header.climbable_count;
		/* (a damaged file: every index in range, or build afresh) */
		if (ok)
		{
			long c;
			for (c = 0; ok && c < navm.width * navm.height; c++)
				ok = navm.columns[c] == NONE || (navm.columns[c] >= 0 && navm.columns[c] < navm.span_count);
			for (c = 0; ok && c < navm.span_count; c++)
				ok = (navm.spans[c].next == NONE || (navm.spans[c].next >= 0 && navm.spans[c].next < navm.span_count)) &&
					navm.spans[c].column >= 0 && navm.spans[c].column < navm.width * navm.height;
		}
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
boolean navm_baked_current(char const *map_name)
{
	struct navm_cache_header header;
	char path[512];
	FILE *file;
	boolean ok;

	navm_cache_file(map_name, 0, "height", TRUE, path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return FALSE;
	ok = fread(&header, sizeof(header), 1, file) == 1 && header.magic == NAVM_CACHE_MAGIC && header.version == NAVM_CACHE_VERSION &&
		header.span_size == sizeof(struct navm_span);
	/* (and all of it there) */
	if (ok)
	{
		double size = sizeof(header) + (double)header.width * header.height * sizeof(long) +
			(double)header.span_count * sizeof(struct navm_span) + (double)header.climbable_count * sizeof(struct navm_climbable);
		ok = fseek(file, 0, SEEK_END) == 0 && (double)ftell(file) == size;
	}
	fclose(file);
	return ok;
}

static void navm_cache_save(void)
{
	struct navm_cache_header header;
	char path[512];
	FILE *file;
	boolean whole;
	memset(&header, 0, sizeof(header));
	header.magic = NAVM_CACHE_MAGIC;
	header.version = NAVM_CACHE_VERSION;
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
	whole = fwrite(&header, sizeof(header), 1, file) == 1 &&
		fwrite(navm.columns, sizeof(long), (size_t)(navm.width * navm.height), file) == (size_t)(navm.width * navm.height) &&
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
	free(navm.columns);
	free(navm.spans);
	memset(&navm, 0, sizeof(navm));
	navm.cell = NAVM_CELL_COARSEST; /* (never nought: it's divided by) */
}

boolean navm_ready(void)
{
	return navm.ready;
}

/* the most columns a map's grid may have (a long each, twice over while it
   is worked out) */
#define NAVM_MAXIMUM_COLUMNS (48 * 1024 * 1024)

/* the last map's grid would have had more: it has no navigation */
static boolean navm_too_large;

boolean navm_map_too_large(void)
{
	return navm_too_large;
}

boolean navm_build(void)
{
	struct collision_bsp *bsp = global_collision_bsp_get();
	unsigned long long start = bot_time_microseconds();
	struct navm_raster raster;
	real minimum_x = 1.0e9f, maximum_x = -1.0e9f, minimum_y = 1.0e9f, maximum_y = -1.0e9f;
	long i;
	long s;

	navm_dispose();
	navm_too_large = FALSE;
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
	/* the grid: as fine as keeps the map to a sensible number of floors
	   (the time to work it all out, and the memory, go by that) */
	{
		real extent = navm_floor_extent(bsp, minimum_x, minimum_y, maximum_x, maximum_y);
		navm.cell = (real)sqrt(MAX(extent, 1.f) / NAVM_FLOOR_BUDGET);
		navm.cell = (real)ceil(navm.cell * 200.f - 0.001f) / 200.f; /* (to 0.005) */
		navm.cell = PIN(navm.cell, NAVM_CELL_FINEST, NAVM_CELL_COARSEST);
		if (getenv("HALO_BOT_CELL")) /* (testing) */
			navm.cell = PIN((real)atof(getenv("HALO_BOT_CELL")), NAVM_CELL_FINEST, NAVM_CELL_COARSEST);
	}
	navm.width = (long)((maximum_x - navm.x0) / NAVM_CELL) + 2;
	navm.height = (long)((maximum_y - navm.y0) / NAVM_CELL) + 2;
	if (navm.width * navm.height > NAVM_MAXIMUM_COLUMNS)
	{
		/* (a map a mile or two across, made for aircraft: at the coarsest
		   grid it is still more than a machine should work out and keep) */
		navm_too_large = TRUE;
		bot_log("navm: this map is too large for the bots' navigation (%.0f by %.0f world units: %ld x %ld columns of %.2f): the bots play it without",
			maximum_x - minimum_x, maximum_y - minimum_y, navm.width, navm.height, navm.cell);
		return FALSE;
	}
	navm.columns = (long *)malloc(navm.width * navm.height * sizeof(long));
	memset(&raster, 0, sizeof(raster));
	raster.first = (long *)malloc(navm.width * navm.height * sizeof(long));
	if (!navm.columns || !raster.first)
	{
		free(raster.first);
		navm_dispose();
		return FALSE;
	}
	for (i = 0; i < navm.width * navm.height; i++)
		raster.first[i] = NONE;

	navm_rasterize_bsp(&raster, bsp);
	navm_build_spans(&raster);
	navm_mark_scenery();
	navm_mark_thin_obstacles();
	/* borders: walls and drops; for vehicles, walls and anything they
	   can't roll over (a step, a low ceiling) */
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
				if (navm_is_wall(&raster, s, d))
					navm.spans[s].flags |= _navm_span_wall_border | _navm_span_vehicle_border;
				else
					navm.spans[s].flags |= _navm_span_edge_border;
			}
			else if (navm_vehicle_neighbor(s, d) == NONE)
				navm.spans[s].flags |= _navm_span_vehicle_border;
		}
	}
	free(raster.first);
	free(raster.intervals);
	navm_distances(_navm_span_wall_border, _navm_distance_wall);
	navm_distances(_navm_span_edge_border, _navm_distance_edge);
	navm_distances(_navm_span_vehicle_border, _navm_distance_vehicle);
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

real navm_edge_distance(real_point3d const *point)
{
	long s;
	if (!navm.ready)
		return -1.f;
	s = navm_span_at(point, 0.5f, 1.2f);
	if (s == NONE)
		return -1.f;
	return navm.spans[s].edge / 100.f;
}

boolean navm_floor_at(real_point3d const *point, real above, real below, real *floor_z)
{
	long s = navm.ready ? navm_span_at(point, above, below) : NONE;
	if (s == NONE)
		return FALSE;
	if (floor_z)
		*floor_z = navm.spans[s].floor;
	return TRUE;
}

/* walking straight from one point to another over the floor: never up or
   down more than a step at once, at least wall_min from walls and
   edge_min from drops all the way */
boolean navm_line_walkable(real_point3d const *from, real_point3d const *to, real wall_min, real edge_min)
{
	real dx = to->x - from->x;
	real dy = to->y - from->y;
	real length = (real)sqrt(dx * dx + dy * dy);
	long steps = (long)(length / (NAVM_CELL * 0.5f)) + 1;
	unsigned short wall = (unsigned short)(wall_min * 100.f);
	unsigned short edge = (unsigned short)(edge_min * 100.f);
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
		for (u = navm.columns[column]; u != NONE; u = navm.spans[u].next)
		{
			real rise = navm.spans[u].floor - z;
			if (fabs(rise) <= NAVM_STEP && fabs(rise) < best_rise)
			{
				best_rise = (real)fabs(rise);
				best = u;
			}
		}
		if (best == NONE || !(navm.spans[best].flags & _navm_span_walkable) || (navm.spans[best].flags & _navm_span_blocked) ||
			navm.spans[best].wall < wall || navm.spans[best].edge < edge)
		{
			return FALSE;
		}
		z = navm.spans[best].floor;
	}
	return TRUE;
}

/* the same, for getting along on foot where it's tight: over a slot in
   the floor no wider than a hand (the bevel at the foot of a sill, the
   crack between two plates: nothing a body notices), and up or down a
   step at a time */
boolean navm_line_passable(real_point3d const *from, real_point3d const *to, real wall_min)
{
	real dx = to->x - from->x;
	real dy = to->y - from->y;
	real length = (real)sqrt(dx * dx + dy * dy);
	long steps = (long)(length / (NAVM_CELL * 0.5f)) + 1;
	unsigned short wall = (unsigned short)(wall_min * 100.f);
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
		for (u = navm.columns[column]; u != NONE; u = navm.spans[u].next)
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
			missing += length / (real)steps;
			if (missing > 0.16f)
				return FALSE;
			continue;
		}
		missing = 0.f;
		if (!(navm.spans[best].flags & _navm_span_walkable) || (navm.spans[best].flags & _navm_span_blocked) || navm.spans[best].wall < wall)
			return FALSE;
		z = navm.spans[best].floor;
	}
	return missing == 0.f;
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
