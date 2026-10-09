/*
BOT_FLIGHT.C

The bots in the air: a Banshee, a fighter, anything whose physics are a
flying machine's (vehicles.c: the alien fighter, the human plane).

Such a craft goes where its pilot looks, up and down as well as round,
and as fast as the throttle says. So flying it is three numbers a tick (a
yaw, a pitch, a throttle), and what there is to know is where the ground
is and where the sky stops.

- The air map. The whole map from above, in squares a few world units
  across: in each, the top of whatever stands in it (the ground, a roof, a
  cliff: what a craft must be over) and the lowest of what hangs over that
  (the top of the level's shell: what it must be under). Made from the
  map's collision surfaces, each cut to the squares it lies in. A very
  large map has one as any other does: this is what joins the parts of it
  that the navigation works out apart (bot_nav.c, navm_choose_parts).
- A route. Straight there when every square on the line has room between
  its ground and its sky; else round what has none (a cliff to the top of
  the level), by a search over the squares, and the turns of what it finds
  pulled straight.
- The height to fly at. A craft's own clearance over the highest ground
  in the stretch it will cover in the next two seconds (so it has begun
  to climb before the hill, not at it), and under the sky. A ray along
  the way it is going is the last word: something there that the map does
  not show (a tree, another craft, a bridge) is climbed over or turned
  from.
- A fight. At someone on the ground: a run at them, nose down, firing
  while they are in the sights; off and up before the ground, out to a
  distance, and round for another. At someone in the air: after them.
- A landing, where the craft is only the way there: down by the place,
  and out.
*/

#include "bot_internal.h"
#include "bot_nav_internal.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "physics/collision_bsp_definitions.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "units/unit_definitions.h"
#include "units/units.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ---------- constants */

#define AIR_CELL_FINEST 2.f /* a square's side, world units, at the least */
#define AIR_MAXIMUM_SIDE 448 /* squares along the longer side, at most */
#define AIR_NO_GROUND -1.0e9f /* a square nothing of the map lies in */
#define AIR_NO_SKY 1.0e9f /* a square with nothing over its ground */
#define AIR_SKY_MARGIN 1.5f /* under the sky by this */
#define AIR_ROUTE_EXPANSIONS 60000 /* squares a search looks at, at most */
#define AIR_HOVER_SLOWING 0.45f /* a plane on its jets: how fast it may be going (a second's) for each unit it has left to stop in */
#define AIR_HOVER_LEAN 0.2f /* ... and how far it leans for each unit a second it wants (radians) */
#define AIR_DRAG 0.02f /* what the air takes of a plane's speed each tick, about (found by flying one) */
#define AIR_UP 1.5707963f /* straight up, radians */
#define AIR_SMALL_CRAFT 2.f /* the bounding radius of a craft the size of a Banshee: what is larger wants more room by the difference */
/* On its way a craft keeps the height it has, where that is over the least
   the ground lets it have and under the way down to where it is bound: it
   does not come down to the ground to cross a valley. How far over the
   least it likes to be is its own, drawn afresh now and then: from this
   much, */
#define AIR_CRUISE_LEAST 4.f
/* ... to this much more for a slow craft, and this much more again for a quick one, */
#define AIR_CRUISE_SLOW 16.f
#define AIR_CRUISE_QUICK 50.f
/* ... for this long, */
#define AIR_CRUISE_SECONDS 25.f
/* ... and the way down to a place: one down for this many along */
#define AIR_GLIDE 0.25f
/* ... and a run down at someone is broken off when the ground is nearer
   than the craft sinks in this long, and its own room */
#define AIR_PULL_OUT 1.3f
/* A bomb whose blast hurts this far off or further (a Longsword's: 80) is
   not let go on a run at someone, from a few lengths off: it is flown over
   them with, level, further over them than it hurts by this much, */
#define AIR_BOMB_LARGE 15.f
#define AIR_BOMB_OVER 25.f
/* ... let go where it will fall no further off them than this much of how
   far it kills, */
#define AIR_BOMB_NEAR 0.3f
/* ... and not from lower over them than it hurts and this */
#define AIR_BOMB_CLEAR 8.f
/* ... and for this long after it is on, the way it was going and no lower:
   the bomb is that long falling, and the craft is to be over the blast, not
   round and down into it */
#define AIR_BOMB_LEAVING 11.f

/* ---------- the air map */

static struct
{
	boolean ready;
	real x0, y0;
	real cell;
	long width, height;
	float *ground;
	float *sky;
	/* (what stands on the ground that is no part of the map's shell: the top
	   of a tree, of a great rock. Flown over, as the ground is; but not
	   ground: someone under a tree is out in the open, and a craft down
	   among the trunks is not under a roof) */
	float *over;
	/* the search for a route */
	float *cost;
	long *from;
	unsigned long *seen;
	long *heap;
	float *heap_key;
	long heap_count;
	long heap_capacity;
	unsigned long search;
	long search_tick;
	long searches_this_tick;
} air;

static void air_trails_forget(void);

void bot_air_dispose(void)
{
	air_trails_forget();
	free(air.ground);
	free(air.sky);
	free(air.over);
	free(air.cost);
	free(air.from);
	free(air.seen);
	free(air.heap);
	free(air.heap_key);
	memset(&air, 0, sizeof(air));
}

boolean bot_air_ready(void)
{
	return air.ready;
}

/* a surface's corners and its plane (as bot_nav.c's navm_surface_polygon) */
static long air_surface_polygon(struct collision_bsp *bsp, long s, real_point3d *polygon, real_plane3d *plane)
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

/* a polygon cut to x >= edge (sign 1) or x <= edge (sign -1) on an axis
   (into room for AIR_CLIP_MOST corners: a surface is convex, and each cut
   adds one corner at the most; one that is not is cut short, not let run
   over the end) */
#define AIR_CLIP_MOST 24
static long air_clip(real_point3d *out, real_point3d const *in, long count, long axis, real edge, real sign)
{
	long i;
	long written = 0;
	for (i = 0; i < count && written <= AIR_CLIP_MOST - 2; i++)
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

/* one pass over the map's surfaces: the first for the ground (everything
   but what faces down at all: floors, slopes, upright walls), the second
   for the sky (what faces down, over the ground of its square: the top of
   the level's shell, and its sides where they lean in over the map as a
   dome's do) */
static void air_surfaces(struct collision_bsp *bsp, int sky_pass)
{
	long s;
	for (s = 0; s < bsp->surfaces.count; s++)
	{
		real_plane3d plane;
		real_point3d polygon[16];
		long count = air_surface_polygon(bsp, s, polygon, &plane);
		real minimum_x = 1.0e9f, maximum_x = -1.0e9f, minimum_y = 1.0e9f, maximum_y = -1.0e9f;
		long cx0, cx1, cy0, cy1, cx, cy;
		long i;

		if (count < 3 || (plane.n.k < -0.1f) != (sky_pass != 0))
			continue;
		for (i = 0; i < count; i++)
		{
			minimum_x = MIN(minimum_x, polygon[i].x);
			maximum_x = MAX(maximum_x, polygon[i].x);
			minimum_y = MIN(minimum_y, polygon[i].y);
			maximum_y = MAX(maximum_y, polygon[i].y);
		}
		cx0 = MAX(0, (long)floor((minimum_x - air.x0) / air.cell));
		cx1 = MIN(air.width - 1, (long)floor((maximum_x - air.x0) / air.cell));
		cy0 = MAX(0, (long)floor((minimum_y - air.y0) / air.cell));
		cy1 = MIN(air.height - 1, (long)floor((maximum_y - air.y0) / air.cell));
		for (cy = cy0; cy <= cy1; cy++)
		{
			for (cx = cx0; cx <= cx1; cx++)
			{
				real_point3d a[AIR_CLIP_MOST], b[AIR_CLIP_MOST];
				real x = air.x0 + cx * air.cell;
				real y = air.y0 + cy * air.cell;
				real low = 1.0e9f, high = -1.0e9f;
				long n = count;
				long c = cy * air.width + cx;
				memcpy(a, polygon, count * sizeof(real_point3d));
				n = air_clip(b, a, n, 0, x, 1.f);
				n = air_clip(a, b, n, 0, x + air.cell, -1.f);
				n = air_clip(b, a, n, 1, y, 1.f);
				n = air_clip(a, b, n, 1, y + air.cell, -1.f);
				if (n < 3)
					continue;
				for (i = 0; i < n; i++)
				{
					low = MIN(low, a[i].z);
					high = MAX(high, a[i].z);
				}
				if (!sky_pass)
					air.ground[c] = MAX(air.ground[c], high);
				else if (low > air.ground[c] + 1.f)
					air.sky[c] = MIN(air.sky[c], low);
			}
		}
	}
}

static long air_cell_at(real x, real y)
{
	long cx, cy;
	if (!air.ready)
		return NONE;
	cx = (long)floor((x - air.x0) / air.cell);
	cy = (long)floor((y - air.y0) / air.cell);
	if (cx < 0 || cy < 0 || cx >= air.width || cy >= air.height)
		return NONE;
	return cy * air.width + cx;
}

static void air_scenery(void);

/* how far along a way (a yaw and a pitch) before something in the flags is hit */
static real air_ray_with(unsigned long flags, real_point3d const *from, real yaw, real pitch, real length, long ignore)
{
	struct collision_result collision;
	real_vector3d vector;
	vector.i = (real)(cos(yaw) * cos(pitch)) * length;
	vector.j = (real)(sin(yaw) * cos(pitch)) * length;
	vector.k = (real)sin(pitch) * length;
	if (!collision_test_vector(flags, from, &vector, ignore, &collision))
		return length;
	return collision.t * length;
}

/* testing (HALO_BOT_AIRAT=x,y,z): the squares round a point, and what is
   over and under it by rays */
static void air_report_at(void)
{
	real_point3d at = { 0.f, 0.f, 0.f };
	long cx, cy, dx, dy;
	sscanf(getenv("HALO_BOT_AIRAT"), "%f,%f,%f", &at.x, &at.y, &at.z);
	cx = (long)floor((at.x - air.x0) / air.cell);
	cy = (long)floor((at.y - air.y0) / air.cell);
	for (dy = 6; dy >= -6; dy--)
	{
		char line[512];
		long length = 0;
		for (dx = -8; dx <= 8; dx++)
		{
			long ax = cx + dx, ay = cy + dy;
			if (ax < 0 || ay < 0 || ax >= air.width || ay >= air.height)
				length += snprintf(line + length, sizeof(line) - length, "    .    ");
			else
				length += snprintf(line + length, sizeof(line) - length, " %4.0f/%-4.0f", air.ground[ay * air.width + ax],
					MIN(air.sky[ay * air.width + ax], 9999.f));
		}
		bot_log("airat %s", line);
	}
	{
		struct collision_result collision;
		real_vector3d up = { 0.f, 0.f, 2000.f }, down = { 0.f, 0.f, -2000.f };
		unsigned long flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
			FLAG(_collision_test_structure_bit);
		boolean hit_up = collision_test_vector(flags, &at, &up, NONE, &collision);
		real up_at = hit_up ? collision.point.z : 0.f;
		boolean hit_down = collision_test_vector(flags, &at, &down, NONE, &collision);
		bot_log("airat %.0f %.0f %.0f: something over it at %.1f (%d), under it at %.1f (%d)", at.x, at.y, at.z, up_at, hit_up,
			hit_down ? collision.point.z : 0.f, hit_down);
		{
			long d;
			for (d = 0; d < 16; d++)
			{
				static real const pitches[5] = { 0.f, 0.15f, 0.3f, 0.5f, 0.8f };
				real yaw = d * 0.3926991f;
				real_point3d from = at;
				char line[160];
				long length = 0;
				long k;
				from.z += 1.f;
				for (k = 0; k < 5; k++)
					length += snprintf(line + length, sizeof(line) - length, " %4.0f", air_ray_with(flags, &from, yaw, pitches[k], 400.f, NONE));
				bot_log("airat ray %.2f:%s", yaw, line);
			}
		}
	}
}

void bot_air_build(void)
{
	struct collision_bsp *bsp = global_collision_bsp_get();
	unsigned long long start = bot_time_microseconds();
	real minimum_x = 1.0e9f, maximum_x = -1.0e9f, minimum_y = 1.0e9f, maximum_y = -1.0e9f;
	long cells;
	long i;

	bot_air_dispose();
	if (!bsp || bsp->vertices.count <= 0)
		return;
	for (i = 0; i < bsp->vertices.count; i++)
	{
		struct collision_vertex const *vertex = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, i, struct collision_vertex);
		minimum_x = MIN(minimum_x, vertex->point.x);
		maximum_x = MAX(maximum_x, vertex->point.x);
		minimum_y = MIN(minimum_y, vertex->point.y);
		maximum_y = MAX(maximum_y, vertex->point.y);
	}
	/* (a level's shell can be many times the size of what is played in: an
	   island in a mile of sea. The squares are for where its places are,
	   and a good way round them) */
	if (aas_ready() && aas_place_count() > 1)
	{
		real low_x = 1.0e9f, high_x = -1.0e9f, low_y = 1.0e9f, high_y = -1.0e9f;
		real margin;
		for (i = 0; i < aas_place_count(); i++)
		{
			low_x = MIN(low_x, aas_place(i)->x);
			high_x = MAX(high_x, aas_place(i)->x);
			low_y = MIN(low_y, aas_place(i)->y);
			high_y = MAX(high_y, aas_place(i)->y);
		}
		margin = 80.f + 0.3f * MAX(high_x - low_x, high_y - low_y);
		minimum_x = MAX(minimum_x, low_x - margin);
		maximum_x = MIN(maximum_x, high_x + margin);
		minimum_y = MAX(minimum_y, low_y - margin);
		maximum_y = MIN(maximum_y, high_y + margin);
	}
	air.cell = MAX(AIR_CELL_FINEST, MAX(maximum_x - minimum_x, maximum_y - minimum_y) / AIR_MAXIMUM_SIDE);
	air.x0 = (real)floor(minimum_x) - air.cell;
	air.y0 = (real)floor(minimum_y) - air.cell;
	air.width = (long)((maximum_x - air.x0) / air.cell) + 2;
	air.height = (long)((maximum_y - air.y0) / air.cell) + 2;
	cells = air.width * air.height;
	air.ground = (float *)malloc(cells * sizeof(float));
	air.sky = (float *)malloc(cells * sizeof(float));
	air.over = (float *)malloc(cells * sizeof(float));
	air.cost = (float *)malloc(cells * sizeof(float));
	air.from = (long *)malloc(cells * sizeof(long));
	air.seen = (unsigned long *)calloc(cells, sizeof(unsigned long));
	air.heap_capacity = cells * 4;
	air.heap = (long *)malloc(air.heap_capacity * sizeof(long));
	air.heap_key = (float *)malloc(air.heap_capacity * sizeof(float));
	if (!air.ground || !air.sky || !air.over || !air.cost || !air.from || !air.seen || !air.heap || !air.heap_key)
	{
		bot_log("air: no memory for the air map: the bots do not fly on this map");
		bot_air_dispose();
		return;
	}
	for (i = 0; i < cells; i++)
	{
		air.ground[i] = AIR_NO_GROUND;
		air.sky[i] = AIR_NO_SKY;
	}
	air_surfaces(bsp, FALSE);
	air_surfaces(bsp, TRUE);
	air.ready = TRUE;
	memcpy(air.over, air.ground, cells * sizeof(float));
	air_scenery();
	if (getenv("HALO_BOT_AIRAT"))
		air_report_at();
	if (bot_logging())
	{
		long inside = 0, roofed = 0;
		real lowest = 1.0e9f, highest = -1.0e9f, lowest_sky = 1.0e9f;
		for (i = 0; i < cells; i++)
		{
			if (air.ground[i] <= AIR_NO_GROUND * 0.5f)
				continue;
			inside++;
			lowest = MIN(lowest, air.ground[i]);
			highest = MAX(highest, air.ground[i]);
			if (air.sky[i] < AIR_NO_SKY * 0.5f)
			{
				roofed++;
				lowest_sky = MIN(lowest_sky, air.sky[i]);
			}
		}
		bot_log("air: %ld x %ld squares of %.1f, %ld of the map (ground %.0f to %.0f), %ld under a sky (the lowest at %.0f), %.2f s",
			air.width, air.height, air.cell, inside, lowest, highest, roofed, lowest_sky, (bot_time_microseconds() - start) / 1.0e6);
	}
}

/* What stands on the map and is not of its shell: the trees, the great
   rocks (scenery with something to run into, and big enough to be in a
   craft's way: a boulder a man can see over is under where it flies
   anyway). The top of each, by rays down through it half a unit apart (a
   tree is a trunk and a crown, not the ball that would hold it), into the
   squares it stands in. */
#define AIR_SCENERY_SMALLEST 1.5f /* its bounding radius */
#define AIR_SCENERY_RAYS 120000 /* of all of it, at most */

static void air_scenery(void)
{
	unsigned long long start = bot_time_microseconds();
	unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_objects_bit) |
		FLAG(_collision_test_objects_scenery_bit);
	struct object_iterator iterator;
	struct object_datum *object;
	long rays = 0;
	long count = 0;
	long raised = 0;

	object_iterator_new(&iterator, _object_mask_scenery, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		struct object_definition *definition = object_definition_get(object->definition_index);
		real_point3d const *middle = &object->object.bounding_sphere_center;
		real radius = object->object.bounding_sphere_radius;
		real step = MAX(0.5f, radius / 20.f);
		real x, y;
		if (definition->object.collision_model.index == NONE || radius < AIR_SCENERY_SMALLEST)
			continue;
		count++;
		for (y = middle->y - radius; y <= middle->y + radius && rays < AIR_SCENERY_RAYS; y += step)
		{
			for (x = middle->x - radius; x <= middle->x + radius && rays < AIR_SCENERY_RAYS; x += step)
			{
				struct collision_result collision;
				real_point3d from;
				real_vector3d down = { 0.f, 0.f, 0.f };
				long c;
				if ((x - middle->x) * (x - middle->x) + (y - middle->y) * (y - middle->y) > radius * radius)
					continue;
				c = air_cell_at(x, y);
				if (c == NONE)
					continue;
				from.x = x;
				from.y = y;
				from.z = middle->z + radius + 0.2f;
				down.k = -(2.f * radius + 0.4f);
				rays++;
				if (collision_test_vector(flags, &from, &down, NONE, &collision) && collision.point.z > air.over[c])
				{
					air.over[c] = collision.point.z;
					raised++;
				}
			}
		}
	}
	if (bot_logging() && count > 0)
		bot_log("air: %ld trees and rocks to fly over (%ld rays, the squares under them raised %ld times), %.2f s", count, rays, raised,
			(bot_time_microseconds() - start) / 1.0e6);
}

/* the squares within radius of a point, those of them on the map: from
   (x0, y0) to (x1, y1), both ends included (none when the point is off it) */
static void air_window(real x, real y, real radius, long *x0, long *y0, long *x1, long *y1)
{
	long reach = radius <= 0.f ? 0 : (long)(radius / MAX(air.cell, 0.01f)) + 1;
	long cx = (long)floor((x - air.x0) / air.cell);
	long cy = (long)floor((y - air.y0) / air.cell);
	*x0 = MAX(cx - reach, 0);
	*y0 = MAX(cy - reach, 0);
	*x1 = MIN(cx + reach, air.width - 1);
	*y1 = MIN(cy + reach, air.height - 1);
}

/* room for a craft over a square: its ground known, and the sky a craft's
   height and more over it */
static boolean air_flyable(long c, real need)
{
	return air.ground[c] > AIR_NO_GROUND * 0.5f && air.sky[c] - air.ground[c] >= need;
}

/* the highest ground within radius of a point that a craft could be over
   at all (not a square with no room between its ground and its sky: a
   wall to the top of the level is gone round, not climbed over) */
static real air_ground_to_clear(real x, real y, real radius, real need, real limit, real *over)
{
	long ax, ay, x0, y0, x1, y1;
	real highest = AIR_NO_GROUND;
	air_window(x, y, radius, &x0, &y0, &x1, &y1);
	for (ay = y0; ay <= y1; ay++)
	{
		for (ax = x0; ax <= x1; ax++)
		{
			long c = ay * air.width + ax;
			/* (nor what stands far over where it is flying: that is gone
			   round, air_route, and passing by it is not climbing it) */
			if (air_flyable(c, need) && air.over[c] <= limit)
			{
				highest = MAX(highest, air.ground[c]);
				/* (... and the top of what stands on it there: a tree's) */
				*over = MAX(*over, air.over[c]);
			}
		}
	}
	return highest;
}

/* the highest ground within radius of a point (AIR_NO_GROUND: off the map) */
real bot_air_ground(real x, real y, real radius)
{
	long ax, ay, x0, y0, x1, y1;
	real highest = AIR_NO_GROUND;
	if (!air.ready)
		return AIR_NO_GROUND;
	air_window(x, y, radius, &x0, &y0, &x1, &y1);
	for (ay = y0; ay <= y1; ay++)
		for (ax = x0; ax <= x1; ax++)
			highest = MAX(highest, air.ground[ay * air.width + ax]);
	return highest;
}

/* is a point out under the sky: over the ground of its square, or within
   a little of it (the map's squares are coarse, and a slope is in them) */
boolean bot_air_open(real_point3d const *point)
{
	real ground = bot_air_ground(point->x, point->y, 0.f);
	return ground <= AIR_NO_GROUND * 0.5f || point->z > ground - MAX(3.f, air.cell);
}

/* the lowest sky within radius of a point */
static real air_sky(real x, real y, real radius)
{
	long ax, ay, x0, y0, x1, y1;
	real lowest = AIR_NO_SKY;
	if (!air.ready)
		return AIR_NO_SKY;
	air_window(x, y, radius, &x0, &y0, &x1, &y1);
	for (ay = y0; ay <= y1; ay++)
		for (ax = x0; ax <= x1; ax++)
			lowest = MIN(lowest, air.sky[ay * air.width + ax]);
	return lowest;
}

/* every square on the straight line between two points has room */
static boolean air_line_flyable(real ax, real ay, real bx, real by, real need, real limit)
{
	real length = (real)sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
	long steps = (long)(length / (air.cell * 0.5f)) + 1;
	long i;
	for (i = 0; i <= steps; i++)
	{
		real t = (real)i / (real)steps;
		long c = air_cell_at(ax + (bx - ax) * t, ay + (by - ay) * t);
		if (c == NONE || !air_flyable(c, need) || air.over[c] > limit)
			return FALSE;
	}
	return TRUE;
}

static void air_heap_push(long c, float key)
{
	long i;
	if (air.heap_count >= air.heap_capacity)
		return;
	i = air.heap_count++;
	while (i > 0)
	{
		long parent = (i - 1) / 2;
		if (air.heap_key[parent] <= key)
			break;
		air.heap[i] = air.heap[parent];
		air.heap_key[i] = air.heap_key[parent];
		i = parent;
	}
	air.heap[i] = c;
	air.heap_key[i] = key;
}

static long air_heap_pop(void)
{
	long top = air.heap[0];
	long last = air.heap[--air.heap_count];
	float key = air.heap_key[air.heap_count];
	long i = 0;
	for (;;)
	{
		long child = i * 2 + 1;
		if (child >= air.heap_count)
			break;
		if (child + 1 < air.heap_count && air.heap_key[child + 1] < air.heap_key[child])
			child++;
		if (air.heap_key[child] >= key)
			break;
		air.heap[i] = air.heap[child];
		air.heap_key[i] = air.heap_key[child];
		i = child;
	}
	air.heap[i] = last;
	air.heap_key[i] = key;
	return top;
}

/* the square with room nearest a point (itself, if it has): NONE none near */
static long air_nearest_flyable(real x, real y, real need, long rings)
{
	long cx = (long)floor((x - air.x0) / air.cell);
	long cy = (long)floor((y - air.y0) / air.cell);
	long ring;
	for (ring = 0; ring <= rings; ring++)
	{
		long dx, dy;
		long best = NONE;
		real best_distance = 1.0e9f;
		for (dy = -ring; dy <= ring; dy++)
		{
			for (dx = -ring; dx <= ring; dx++)
			{
				long ax = cx + dx, ay = cy + dy;
				real distance;
				if ((dx > -ring && dx < ring && dy > -ring && dy < ring) || ax < 0 || ay < 0 || ax >= air.width || ay >= air.height)
					continue;
				if (!air_flyable(ay * air.width + ax, need))
					continue;
				distance = (real)(dx * dx + dy * dy);
				if (distance < best_distance)
				{
					best_distance = distance;
					best = ay * air.width + ax;
				}
			}
		}
		if (best != NONE)
			return best;
	}
	return NONE;
}

static void air_cell_center(long c, real_point3d *point)
{
	point->x = air.x0 + ((c % air.width) + 0.5f) * air.cell;
	point->y = air.y0 + ((c / air.width) + 0.5f) * air.cell;
	point->z = air.ground[c];
}

/* The search over the squares, from start to goal for a craft that needs
   `need` of room and under `limit`: the squares' costs and where each was
   come from are left in the air map. Whether the goal was found. */
static boolean air_search(long start, long goal, real need, real limit)
{
	static long const step_x[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static long const step_y[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	long expansions = 0;
	real goal_x = (real)(goal % air.width), goal_y = (real)(goal / air.width);

	air.search++;
	air.heap_count = 0;
	air.cost[start] = 0.f;
	air.from[start] = NONE;
	air.seen[start] = air.search;
	air_heap_push(start, 0.f);
	while (air.heap_count > 0 && expansions++ < AIR_ROUTE_EXPANSIONS)
	{
		long d;
		long cx, cy;
		long c = air_heap_pop();
		if (c == goal)
			return TRUE;
		cx = c % air.width;
		cy = c / air.width;
		for (d = 0; d < 8; d++)
		{
			long nx = cx + step_x[d], ny = cy + step_y[d];
			long n;
			float cost;
			if (nx < 0 || ny < 0 || nx >= air.width || ny >= air.height)
				continue;
			n = ny * air.width + nx;
			if (!air_flyable(n, need) || air.over[n] > limit)
				continue;
			/* (a diagonal: not past the corner of what has no room) */
			if (d >= 4 && (!air_flyable(cy * air.width + nx, need) || !air_flyable(ny * air.width + cx, need)))
				continue;
			/* (the way over, and a little for the climb: round a hill
			   sooner than over it when it is about as far) */
			cost = air.cost[c] + (d >= 4 ? 1.41421356f : 1.f) + (float)fabs(air.ground[n] - air.ground[c]) * 0.25f / air.cell;
			if (air.seen[n] == air.search && air.cost[n] <= cost)
				continue;
			air.seen[n] = air.search;
			air.cost[n] = cost;
			air.from[n] = c;
			air_heap_push(n, cost + (float)sqrt((nx - goal_x) * (nx - goal_x) + (ny - goal_y) * (ny - goal_y)));
		}
	}
	return FALSE;
}

/* The turns of what air_search found: back from the goal, then from each
   kept square the furthest one along that is in a straight line with room.
   The last of them is goal_point. */
static long air_route_turns(long goal, real_point3d const *from, real_point3d const *goal_point, real need, real limit, real_point3d *points,
	long maximum)
{
	long path[4096];
	long path_count = 0;
	long count = 0;
	long at;
	long c;
	real_point3d here = *from;
	for (c = goal; c != NONE && path_count < NUMBEROF(path); c = air.from[c])
		path[path_count++] = c;
	at = path_count - 1; /* (the start) */
	while (at > 0 && count < maximum)
	{
		long next = at - 1;
		long probe;
		for (probe = 0; probe < at - 1; probe++)
		{
			real_point3d there;
			air_cell_center(path[probe], &there);
			if (air_line_flyable(here.x, here.y, there.x, there.y, need, limit))
			{
				next = probe;
				break;
			}
		}
		air_cell_center(path[next], &here);
		points[count++] = next == 0 ? *goal_point : here;
		at = next;
	}
	return count;
}

/* A route through the air from one place to another for a craft that needs
   `need` of room: the turns of it, the last of them the place itself (or
   the nearest square to it with room). -1: none, no way round; 0: none
   yet, the search's turn this tick taken (fly as before). */
static long air_route(real_point3d const *from, real_point3d const *to, real need, real_point3d *points, long maximum)
{
	long start, goal;
	real_point3d goal_point;
	/* (what stands a long way higher than both ends of the way is gone
	   round, not over: a tower, a pinnacle. Up one side of it and down the
	   other is a long way round too, and looks it.) */
	real limit = MAX(from->z, to->z) + 30.f;

	if (!air.ready || maximum < 1)
		return 0;
	start = air_nearest_flyable(from->x, from->y, need, 12);
	goal = air_nearest_flyable(to->x, to->y, need, 24);
	if (start == NONE || goal == NONE)
		return 0;
	air_cell_center(goal, &goal_point);
	if (air_cell_at(to->x, to->y) == goal)
		goal_point = *to;
	/* straight there */
	if (air.over[start] > limit || air.over[goal] > limit)
		limit = AIR_NO_SKY; /* (one end of it is up there) */
	if (air_line_flyable(from->x, from->y, goal_point.x, goal_point.y, need, limit))
	{
		points[0] = goal_point;
		return 1;
	}
	/* (one search a tick, of all the pilots': the others fly on as they are) */
	if (air.search_tick != game_time_get())
	{
		air.search_tick = game_time_get();
		air.searches_this_tick = 0;
	}
	if (air.searches_this_tick++ >= 1)
		return 0;
	if (!air_search(start, goal, need, limit))
		return -1; /* (looked, and there is none: not "no search to be had this tick") */
	return air_route_turns(goal, from, &goal_point, need, limit, points, maximum);
}

/* ---------- the craft */

/* a vehicle's kind, as its physics are (vehicles.c: its type, a short after
   its flags, after the unit's part of its definition); its speeds follow */
long bot_vehicle_kind(long definition_index)
{
	char const *definition = (char const *)object_definition_get(definition_index);
	short type = *(short const *)(definition + sizeof(struct unit_definition) + 4);
	return type >= 0 && type < NUMBER_OF_BOT_VEHICLE_KINDS ? type : _bot_vehicle_jeep;
}

boolean bot_vehicle_flies(long definition_index)
{
	long kind = bot_vehicle_kind(definition_index);
	return kind == _bot_vehicle_fighter || kind == _bot_vehicle_plane;
}

/* its top speed forward, world units a second */
static real air_top_speed(long definition_index)
{
	char const *definition = (char const *)object_definition_get(definition_index);
	real speed = *(real const *)(definition + sizeof(struct unit_definition) + 8);
	/* (never more than one a tick, whatever the tag says: a vehicle's speed
	   seeks its throttle, and stops at it, vehicles.c) */
	return PIN(speed, 0.01f, 1.f) * BOT_TICKS_PER_SECOND;
}

/* ---------- flying */

enum
{
	_pilot_cruise = 0, /* on the way */
	_pilot_run, /* at them, firing */
	_pilot_break, /* off and up, out to a distance */
	_pilot_land, /* down by the place, to get out */
};

/* (testing, HALO_BOT_REFINE with 32 taken off it: a craft on its way at the
   least height the ground lets it have, and every craft a Banshee's size) */
static boolean air_cruises(void)
{
	return bot_refined_is(32) != 0;
}

/* what a ray along a craft's way is stopped by: the map's shell, scenery,
   machines and other vehicles */
static unsigned long const air_ray_flags =
	FLAG(_collision_test_front_facing_surfaces_bit) |
	FLAG(_collision_test_structure_bit) |
	FLAG(_collision_test_objects_bit) |
	FLAG(_collision_test_objects_scenery_bit) |
	FLAG(_collision_test_objects_machines_bit) |
	FLAG(_collision_test_objects_vehicles_bit);

/* the same, of what stands still: the map's shell, its scenery and machines
   (not someone walking through, nor a vehicle left there) */
static unsigned long const air_ray_fixed_flags =
	FLAG(_collision_test_front_facing_surfaces_bit) |
	FLAG(_collision_test_structure_bit) |
	FLAG(_collision_test_objects_bit) |
	FLAG(_collision_test_objects_scenery_bit) |
	FLAG(_collision_test_objects_machines_bit);

/* ... of anything */
static real air_ray(real_point3d const *from, real yaw, real pitch, real length, long ignore)
{
	return air_ray_with(air_ray_flags, from, yaw, pitch, length, ignore);
}

/* ... of what stands still */
static real air_ray_fixed(real_point3d const *from, real yaw, real pitch, real length, long ignore)
{
	return air_ray_with(air_ray_fixed_flags, from, yaw, pitch, length, ignore);
}

/* how far off a way (a yaw and a pitch) the pilot's look is now, radians */
static real air_look_off(struct bot_flight_order const *order, real yaw, real pitch)
{
	real cosine = (real)(cos(order->look_pitch) * cos(pitch) * cos(bot_angle_difference(order->look_yaw, yaw)) + sin(order->look_pitch) * sin(pitch));
	return (real)acos(PIN(cosine, -1.f, 1.f));
}

/* Where to look for a craft's other gun to land its shot on someone: a
   thing that leaves at the craft's speed and its own together, slows as it
   goes and falls (a Banshee's fuel rod). The shot is followed tick by tick
   as the game will move it (projectiles.c), and they as they are going; by
   how much it passes them, the look is moved, and again. Returns how near
   it comes at the last. */
static real air_lob(real_point3d const *from, real_vector3d const *carried, real_point3d const *them, real_vector3d const *their_velocity,
	struct bot_flight_order const *order, real *yaw, real *pitch)
{
	real fall = order->heavy_gravity * 0.003565f; /* (the world's gravity, a tick's: physics.c) */
	real_point3d aim = *them;
	real nearest = 1.0e9f;
	long pass;

	aim.z += 0.2f;
	for (pass = 0; pass < 4; pass++)
	{
		real_vector3d going;
		real_point3d at = *from;
		real_point3d they = *them;
		real_vector3d by = { 0.f, 0.f, 0.f };
		real length = MAX(distance3d(from, &aim), 0.01f);
		long tick;
		they.z += 0.2f;
		going.i = (aim.x - from->x) / length * order->heavy_speed + carried->i;
		going.j = (aim.y - from->y) / length * order->heavy_speed + carried->j;
		going.k = (aim.z - from->z) / length * order->heavy_speed + carried->k;
		nearest = 1.0e9f;
		for (tick = 0; tick < (order->heavy_bomb ? 600 : 150); tick++)
		{
			real off = distance3d(&at, &they);
			real pace;
			if (off < nearest)
			{
				nearest = off;
				by.i = they.x - at.x;
				by.j = they.y - at.y;
				by.k = they.z - at.z;
			}
			else if (off > nearest + 3.f)
				break;
			pace = magnitude3d(&going);
			if (pace > order->heavy_slowest && pace > 0.f && order->heavy_slowing > 0.f)
			{
				real slower = MAX(pace - order->heavy_slowing, order->heavy_slowest);
				going.i *= slower / pace;
				going.j *= slower / pace;
				going.k *= slower / pace;
			}
			going.k -= fall;
			at.x += going.i;
			at.y += going.j;
			at.z += going.k;
			they.x += their_velocity->i;
			they.y += their_velocity->j;
			they.z += their_velocity->k;
		}
		*yaw = bot_yaw_to(from, &aim);
		*pitch = (real)atan2(aim.z - from->z, MAX(bot_distance2d(from, &aim), 0.5f));
		if (nearest < 0.4f)
			break;
		aim.x += by.i;
		aim.y += by.j;
		aim.z += by.k;
	}
	return nearest;
}

/* Which way to go on from here, at this height, for so far, with nothing
   of the map in the way: the way it is going if that is clear, or the least
   turn from it that is (a base under a cliff, bombed, and the cliff behind
   it). The way it is going when none is. */
static real air_way_on(real_point3d const *position, real yaw, real far, real need, real clearance)
{
	static real const turns[11] = { 0.f, 0.4f, -0.4f, 0.8f, -0.8f, 1.2f, -1.2f, 1.7f, -1.7f, 2.3f, -2.3f };
	long t;
	for (t = 0; t < 11; t++)
	{
		real way = bot_wrap(yaw + turns[t]);
		if (air_line_flyable(position->x, position->y, position->x + (real)cos(way) * far, position->y + (real)sin(way) * far, need, position->z - clearance))
			return way;
	}
	return yaw;
}

/* (testing, HALO_BOT_AIRTRACE: what a plane on its jets is about, twice a second) */
static boolean air_trace(void)
{
	static int asked, set;
	if (!asked)
	{
		asked = TRUE;
		set = getenv("HALO_BOT_AIRTRACE") != NULL;
	}
	return set;
}

static boolean air_sight(real_point3d const *from, real_point3d const *to, long ignore)
{
	struct collision_result collision;
	real_vector3d vector;
	vector.i = to->x - from->x;
	vector.j = to->y - from->y;
	vector.k = to->z - from->z;
	return !collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit),
		from, &vector, ignore, &collision);
}

/* a craft's wing tips, half_span either side of its middle, square to the way it goes */
static void air_wing_tips(real_point3d const *middle, real yaw, real half_span, real_point3d *left, real_point3d *right)
{
	*left = *middle;
	*right = *middle;
	left->x -= (real)sin(yaw) * half_span;
	left->y += (real)cos(yaw) * half_span;
	right->x += (real)sin(yaw) * half_span;
	right->y -= (real)cos(yaw) * half_span;
}

/* The way in to a place under a roof, for a plane that is to come down
   there: a hangar cut into a hillside, with the flag at the back of it. From
   the place, a little over its floor, rays level all round; along each, the
   first of it that is out under the sky, with room for the craft's wings on
   both sides all the way. The shortest of them is the way in, and a place
   out in the open in line with it is where the craft comes to first. */
static boolean air_way_in(real_point3d const *place, long ignore, real_point3d *outside)
{
	real_point3d from = *place;
	real best = 1.0e9f;
	long d;
	from.z += 2.5f;
	for (d = 0; d < 24; d++)
	{
		real way = d * 0.2617994f;
		real open = air_ray_fixed(&from, way, 0.f, 300.f, ignore);
		real along;
		real step = MAX(air.cell * 0.5f, 2.f);
		if (bot_logging() && air_trace())
		{
			real_point3d end = from;
			end.x += (real)cos(way) * (open - 1.f);
			end.y += (real)sin(way) * (open - 1.f);
			bot_log("way in: from %.0f %.0f %.0f way %.2f open %.0f; its end %s, %.0f clear over it", from.x, from.y, from.z, way, open,
				bot_air_open(&end) ? "open" : "roofed", air_ray_fixed(&end, 0.f, AIR_UP, 60.f, ignore));
		}
		if (open < 20.f)
			continue;
		for (along = step; along < MIN(open - 3.f, best); along += step)
		{
			real_point3d at = from;
			at.x += (real)cos(way) * along;
			at.y += (real)sin(way) * along;
			if (bot_air_open(&at) && air_ray_fixed(&at, 0.f, AIR_UP, 60.f, ignore) >= 60.f)
			{
				/* (out; and room for it on the way: a ray along each wing's tip) */
				real_point3d left, right;
				real out_to = MIN(along + 30.f, open - 3.f);
				air_wing_tips(&from, way, 2.5f, &left, &right);
				if (air_ray_fixed(&left, way, 0.f, along, ignore) >= along && air_ray_fixed(&right, way, 0.f, along, ignore) >= along && out_to > along + 4.f)
				{
					best = along;
					*outside = from;
					outside->x += (real)cos(way) * out_to;
					outside->y += (real)sin(way) * out_to;
				}
				break;
			}
		}
	}
	return best < 1.0e8f;
}

/* ---------- the ways out from under roofs

A hangar is cut into a hillside, with a craft standing at the back of it and
the flag beside the craft; its floor has a plinth in the middle, its mouth a
lip over it. No ray from the flag finds the way out. But every craft that is
flown out of one (by a bot or by a person) shows it: where it went, from
where it stood to the open air, is kept, a point every few units; and a
craft that is to come down at a place under a roof goes to the open end of
the way that passes nearest the place, and back along it. */

#define AIR_TRAILS 16
#define AIR_TRAIL_POINTS 32
#define AIR_TRAIL_STEP 6.f

struct air_trail
{
	long vehicle; /* (being made: whose) */
	boolean done; /* out: its last point is in the open, a little way beyond the mouth */
	long tick;
	long count;
	real_point3d points[AIR_TRAIL_POINTS];
};

static struct air_trail air_trails[AIR_TRAILS];

/* (the ways out of one map's roofs are not another's: with the air map) */
static void air_trails_forget(void)
{
	memset(air_trails, 0, sizeof(air_trails));
}

static boolean air_roofed(real_point3d const *position, long ignore)
{
	real ground = bot_air_ground(position->x, position->y, 0.f);
	real up;
	if (ground <= AIR_NO_GROUND * 0.5f || position->z >= ground - 1.f)
		return FALSE;
	up = MIN(ground - position->z + 2.f, 80.f);
	return air_ray_fixed(position, 0.f, AIR_UP, up, ignore) < up;
}

void bot_air_trails_update(void)
{
	extern long unit_scripting_unit_driver(long unit_index);
	struct object_iterator iterator;
	struct object_datum *object;
	long now = game_time_get();
	long i;
	if (!air.ready)
		return;
	if (now < 5)
		memset(air_trails, 0, sizeof(air_trails));
	/* (a way being made by a craft that is gone, or left standing: no way) */
	for (i = 0; i < AIR_TRAILS; i++)
	{
		struct air_trail *trail = &air_trails[i];
		if (trail->count > 0 && !trail->done && (!object_try_and_get(trail->vehicle) || unit_scripting_unit_driver(trail->vehicle) == NONE))
			trail->count = 0;
	}
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		struct air_trail *trail = NULL;
		boolean roofed;
		if (!bot_vehicle_flies(object->definition_index) || unit_scripting_unit_driver(iterator.index) == NONE)
			continue;
		for (i = 0; i < AIR_TRAILS; i++)
			if (air_trails[i].count > 0 && !air_trails[i].done && air_trails[i].vehicle == iterator.index)
				trail = &air_trails[i];
		/* (looked at a few times a second: it is slow under a roof) */
		if ((now + (iterator.index & 7)) % 4 != 0)
			continue;
		roofed = air_roofed(&object->object.position, iterator.index);
		if (!trail)
		{
			struct air_trail *oldest = NULL;
			if (!roofed)
				continue;
			/* (one from here already: that one will do) */
			for (i = 0; i < AIR_TRAILS; i++)
				if (air_trails[i].count > 0 && air_trails[i].done && distance3d(&air_trails[i].points[0], &object->object.position) < 8.f)
					break;
			if (i < AIR_TRAILS)
				continue;
			for (i = 0; i < AIR_TRAILS; i++)
				if (air_trails[i].count == 0 || (air_trails[i].done && (!oldest || (oldest->count > 0 && air_trails[i].tick < oldest->tick))))
				{
					oldest = &air_trails[i];
					if (oldest->count == 0)
						break;
				}
			if (!oldest)
				continue;
			oldest->vehicle = iterator.index;
			oldest->done = FALSE;
			oldest->tick = now;
			oldest->count = 1;
			oldest->points[0] = object->object.position;
			continue;
		}
		if (roofed)
		{
			if (distance3d(&trail->points[trail->count - 1], &object->object.position) < AIR_TRAIL_STEP)
				continue;
			if (trail->count >= AIR_TRAIL_POINTS - 2)
			{
				/* (a long way: every other point of it will do) */
				long kept = 0;
				for (i = 0; i < trail->count; i += 2)
					trail->points[kept++] = trail->points[i];
				trail->count = kept;
			}
			trail->points[trail->count++] = object->object.position;
		}
		else if (trail->count >= 2)
		{
			/* out: the mouth, and a place a little way beyond it the way it came out */
			real_point3d const *before = &trail->points[trail->count - 1];
			real_point3d mouth = object->object.position;
			real_point3d beyond = mouth;
			real along = bot_distance2d(before, &mouth);
			real way = along > 0.5f ? bot_yaw_to(before, &mouth) : (real)atan2(object->object.forward.j, object->object.forward.i);
			real open = air_ray_fixed(&mouth, way, 0.f, 40.f, iterator.index);
			beyond.x += (real)cos(way) * MAX(open - 6.f, 0.f);
			beyond.y += (real)sin(way) * MAX(open - 6.f, 0.f);
			trail->points[trail->count++] = mouth;
			trail->points[trail->count++] = beyond;
			trail->done = TRUE;
			trail->tick = now;
			if (bot_logging())
				bot_log("air: a way out from under a roof kept: from %.0f %.0f %.0f by %ld turns to %.0f %.0f %.0f", trail->points[0].x, trail->points[0].y,
					trail->points[0].z, trail->count, beyond.x, beyond.y, beyond.z);
		}
		else
			trail->count = 0;
	}
}

/* the way out that passes nearest a place, with nothing between the place
   and where it passes: which, and at which of its points */
static boolean air_trail_to(real_point3d const *place, long *trail_index, long *point_index)
{
	real best = 45.f;
	long i, k;
	*trail_index = NONE;
	for (i = 0; i < AIR_TRAILS; i++)
	{
		struct air_trail const *trail = &air_trails[i];
		if (trail->count < 3 || !trail->done)
			continue;
		for (k = 0; k < trail->count - 2; k++)
		{
			real off = distance3d(&trail->points[k], place);
			real_point3d at = *place;
			at.z += 2.5f;
			if (off < best && air_sight(&trail->points[k], &at, NONE))
			{
				best = off;
				*trail_index = i;
				*point_index = k;
			}
		}
	}
	return *trail_index != NONE;
}

/* A plane bound for the ground at a place under a roof: to the open air
   outside the way in to it, and in along that (the landing, air_hover_landing,
   does the rest). Returns whether the order is to be replaced by *inbound. */
static boolean air_way_in_order(struct bot_pilot *pilot, struct bot_body const *body, struct bot_flight_order const *order, boolean plane, long now,
	struct bot_flight_order *inbound)
{
	real_point3d const *position = &body->position;
	if (plane && order->land && pilot->vehicle == body->vehicle_index && !bot_air_open(&order->destination))
	{
		if (pilot->way_in == 0 || distance3d(&pilot->way_in_for, &order->destination) > 6.f)
		{
			if (now - pilot->way_in_tick > BOT_SECONDS(3.f) || now < pilot->way_in_tick)
			{
				long trail_index = NONE, point_index = 0;
				pilot->way_in_tick = now;
				pilot->way_in = 0;
				pilot->way_in_trail = NONE;
				pilot->way_in_for = order->destination;
				if (air_trail_to(&order->destination, &trail_index, &point_index))
				{
					pilot->way_in = 1;
					pilot->way_in_trail = trail_index;
					pilot->way_in_last = point_index;
					pilot->way_in_next = air_trails[trail_index].count - 2;
					pilot->way_in_outside = air_trails[trail_index].points[air_trails[trail_index].count - 1];
				}
				else if (air_way_in(&order->destination, body->vehicle_index, &pilot->way_in_outside))
					pilot->way_in = 1;
				if (bot_logging())
					bot_log("pilot %ld t%ld the way in to %.0f %.0f %.0f: %s %.0f %.0f %.0f%s", body->unit_index & 0xffff, now, order->destination.x,
						order->destination.y, order->destination.z, pilot->way_in ? "from" : "none", pilot->way_in_outside.x, pilot->way_in_outside.y,
						pilot->way_in_outside.z, pilot->way_in_trail != NONE ? " (the way a craft came out)" : "");
			}
		}
		/* (in already, as it happens: under the same roof as the place, and
		   the place in sight) */
		if (pilot->way_in == 1 && air_roofed(position, body->vehicle_index) && distance3d(position, &order->destination) < 60.f)
		{
			real_point3d at = order->destination;
			at.z += 2.5f;
			if (air_sight(position, &at, body->vehicle_index))
				pilot->way_in = 3;
		}
		if (pilot->way_in == 1 || pilot->way_in == 2)
		{
			*inbound = *order;
			inbound->destination = pilot->way_in == 1 ? pilot->way_in_outside : air_trails[pilot->way_in_trail].points[pilot->way_in_next];
			inbound->short_of = 0.f;
			return TRUE;
		}
	}
	else
		pilot->way_in = 0;
	return FALSE;
}

/* A craft this pilot has not flown before (or none): everything afresh, and
   where and how it stands noted. */
static void air_pilot_take(struct bot_pilot *pilot, struct bot_body const *body, struct object_datum const *craft, real top_speed, long now)
{
	real_point3d const *position = &body->position;
	real_point3d under = *position;
	real ground_z;
	memset(pilot, 0, sizeof(*pilot));
	pilot->vehicle = body->vehicle_index;
	pilot->hovering = TRUE;
	pilot->rest_z = position->z;
	pilot->stuck_point = *position;
	pilot->stuck_tick = now;
	pilot->hover_yaw = body->facing_yaw;
	pilot->gate = NONE;
	pilot->way_in_trail = NONE;
	pilot->taken_tick = now;
	if (bot_logging())
		bot_log("pilot %ld t%ld is %s (radius %.1f, top speed %.0f)", body->unit_index & 0xffff, now, tag_get_name(craft->definition_index),
			craft->object.bounding_sphere_radius, top_speed);
	/* (standing on the ground, as like as not: its middle this far over it) */
	pilot->rest_over = bot_ground(&under, 6.f, body->vehicle_index, &ground_z) ? PIN(position->z - ground_z, 0.2f, 5.f) : 1.5f;
}

/* The route to the place and where on it to steer: a new one when the place
   has moved a good way, or every two seconds (three turns a second of a fast
   craft are a mile). */
/* (... but one that found no way is not looked for again the tick after,
   and every tick for four seconds: each is the whole search, and the one
   search a tick is every pilot's) */
static void air_route_step(struct bot_pilot *pilot, real_point3d const *position, struct bot_flight_order const *order, real need, real speed, real to_place,
	long now, real_point3d *steer_to)
{
	if ((pilot->route_count <= 0 || now - pilot->route_tick > BOT_SECONDS(2.f) ||
		bot_distance2d(&pilot->route_to, &order->destination) > MAX(12.f, to_place * 0.2f)) &&
		!(pilot->route_count > 0 && pilot->route_none_tick > 0 && now - pilot->route_none_tick < BOT_SECONDS(1.f)))
	{
		real_point3d turns[BOT_PILOT_TURNS];
		long count = air_route(position, &order->destination, need, turns, BOT_PILOT_TURNS);
		if (count > 0)
		{
			memcpy(pilot->route, turns, count * sizeof(real_point3d));
			pilot->route_count = count;
			pilot->route_next = 0;
			pilot->route_tick = now;
			pilot->route_to = order->destination;
		}
		else if (count < 0 && pilot->route_count > 0 && now - pilot->route_tick <= BOT_SECONDS(6.f))
			pilot->route_none_tick = now;
		else if (pilot->route_count <= 0 || now - pilot->route_tick > BOT_SECONDS(6.f))
		{
			/* (no way found, or no search to be had this tick: straight at it,
			   and the height to fly at and the ray do what they can) */
			pilot->route[0] = order->destination;
			pilot->route_count = 1;
			pilot->route_next = 0;
			pilot->route_tick = now;
			pilot->route_to = order->destination;
		}
	}
	while (pilot->route_next < pilot->route_count - 1 &&
		bot_distance2d(position, &pilot->route[pilot->route_next]) < MAX(10.f, speed))
	{
		pilot->route_next++;
	}
	*steer_to = pilot->route[pilot->route_next];
	/* (the last of it is the place itself, wherever it has got to) */
	if (pilot->route_next == pilot->route_count - 1 && bot_distance2d(steer_to, &order->destination) < MAX(12.f, to_place * 0.2f))
		*steer_to = order->destination;
}

/* what bot_fly has worked out about the craft this tick, and the look it is
   building (a yaw, a pitch, a throttle, and the height they are for), for
   the parts of flying it hands to the functions below */
struct air_flight
{
	struct bot_pilot *pilot;
	struct bot_body const *body;
	struct bot_flight_order const *order;
	struct bot_flight_result *result;
	struct object_datum *craft;
	long now;
	long phase_before;
	real top_speed, speed;
	real quick, large, need, clearance, ahead;
	real to_place;
	real ground_here, over_ground;
	boolean plane, under_roof;
	boolean bombing;
	real bomb_lands_z, bomb_clear;
	real_point3d steer_to;
	real yaw, pitch, throttle;
	real height;
	real least; /* the lowest the ground lets it be (its jets go by this: a plane rises to it, and no further, before it is on its wings) */
	char const *why; /* (testing: what it changed its mind for) */
};

/* what bot_fly works out first, of the craft and the order it is given
   (before the order is changed to one for the way in to a place) */
static void air_flight_begin(struct air_flight *f, struct bot_pilot *pilot, struct bot_body const *body, struct bot_flight_order const *order,
	struct bot_flight_result *result)
{
	struct object_datum *craft = object_get(body->vehicle_index);
	real_point3d const *position = &body->position;

	f->pilot = pilot;
	f->body = body;
	f->order = order;
	f->result = result;
	f->craft = craft;
	f->now = game_time_get();
	f->phase_before = pilot->phase;
	f->top_speed = air_top_speed(craft->definition_index);
	f->speed = (real)sqrt(body->velocity.i * body->velocity.i + body->velocity.j * body->velocity.j + body->velocity.k * body->velocity.k) *
		BOT_TICKS_PER_SECOND;
	/* a slow craft (a Banshee, 7 a second) and a fast one (a jet, 120): the
	   room each needs, how high over the ground it keeps, how far off it
	   starts a run and breaks from one, all by how fast it is */
	f->quick = PIN((f->top_speed - 6.f) / 24.f, 0.f, 1.f);
	/* (... and by how large it is: a Pelican is 11 units across and a
	   Longsword 24, and what a Banshee clears by a length they do not clear
	   at all) */
	f->large = air_cruises() ? MAX(craft->object.bounding_sphere_radius - AIR_SMALL_CRAFT, 0.f) : 0.f;
	f->need = MAX(3.f + 3.f * f->quick, f->large > 0.f ? MIN(craft->object.bounding_sphere_radius, 10.f) * 0.75f : 0.f);
	f->clearance = 4.5f + 8.f * f->quick + f->large * 0.6f;
	f->ahead = MAX(14.f, f->speed * 2.f); /* the stretch it looks over */
	f->to_place = bot_distance2d(position, &order->destination);
	f->ground_here = bot_air_ground(position->x, position->y, 0.f);
	f->over_ground = f->ground_here > AIR_NO_GROUND * 0.5f ? MAX(position->z - f->ground_here, -1.f) : 0.f;
	/* (under a roof: the map's square has its ground over us, and there is
	   something over us in fact; beside a tower that fills half the square,
	   there is not) */
	f->under_roof = f->ground_here > AIR_NO_GROUND * 0.5f && position->z < f->ground_here - 1.f &&
		air_ray(position, 0.f, AIR_UP, MIN(f->ground_here - position->z + 2.f, 80.f), body->vehicle_index) <
			MIN(f->ground_here - position->z + 2.f, 80.f);
	f->plane = bot_vehicle_kind(craft->definition_index) == _bot_vehicle_plane;
	/* (where a bomb let fall on the place goes off: on the top of whatever
	   stands over it, a roof or a tree, which is where the blast is) */
	f->bomb_lands_z = order->bomb ? MAX(order->destination.z, bot_air_ground(order->destination.x, order->destination.y, air.cell)) : order->destination.z;
	/* (how far from that the craft is to be when it does: further than it
	   hurts, by the craft's own size, which the blast reaches the nearest
	   part of) */
	f->bomb_clear = order->heavy_hurts + craft->object.bounding_sphere_radius + AIR_BOMB_CLEAR;
	/* (a bombing pass, while the bomb is in its rack and the sky over the
	   place is high enough to be out of its blast under) */
	f->bombing = order->bomb && order->heavy && order->heavy_bomb && order->heavy_ready && !order->airborne &&
		order->heavy_hurts >= AIR_BOMB_LARGE &&
		air_sky(order->destination.x, order->destination.y, 0.f) - AIR_SKY_MARGIN > f->bomb_lands_z + f->bomb_clear;
	f->throttle = 1.f;
	f->why = "";
}

/* off from them, or from a bomb let go: the break phase begun */
static void air_begin_break(struct bot_pilot *pilot, long now, boolean away)
{
	pilot->phase = _pilot_break;
	pilot->phase_tick = now;
	pilot->away = away;
}

/* its bomb let go: on, the way it is going, to where there is room, and no lower */
static void air_leave_bomb(struct air_flight const *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	air_begin_break(pilot, f->now, FALSE);
	pilot->bomb_pass = TRUE;
	pilot->break_yaw = air_way_on(&body->position, f->speed > 2.f ? (real)atan2(body->velocity.j, body->velocity.i) : body->facing_yaw,
		MAX(f->speed, 10.f) * AIR_BOMB_LEAVING, f->need, f->clearance);
}

/* Which way to go to get behind something, if there is anything to get
   behind: of the ways round, the one that soonest puts a hill or a building
   between the craft and them, and is least of a turn. The pilot's break_yaw
   is left as it was when there is none. */
static void air_hide_yaw(struct air_flight const *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct bot_flight_order const *order = f->order;
	real_point3d const *position = &body->position;
	real_point3d them = order->destination;
	real best = -1.0e9f;
	long d;
	them.z += 0.6f;
	for (d = 0; d < 12; d++)
	{
		real way = d * 0.5235988f;
		real turn = (real)fabs(bot_angle_difference(way, body->facing_yaw));
		real from_them = (real)cos(bot_angle_difference(way, bot_yaw_to(&order->destination, position)));
		real score = from_them * 2.f - turn * 1.2f;
		real reach;
		for (reach = 14.f; reach <= 30.f; reach += 8.f)
		{
			real_point3d there;
			real ground;
			there.x = position->x + (real)cos(way) * reach;
			there.y = position->y + (real)sin(way) * reach;
			ground = bot_air_ground(there.x, there.y, 0.f);
			if (ground <= AIR_NO_GROUND * 0.5f)
				break;
			there.z = ground + f->clearance;
			if (!air_sight(&them, &there, body->vehicle_index))
			{
				score += 5.f - (reach - 14.f) * 0.12f;
				break;
			}
		}
		if (from_them > -0.3f && score > best)
		{
			best = score;
			pilot->break_yaw = way;
		}
	}
}

/* A run at someone, in its phase: whether it is time to stop (the bomb to
   drop, the ground near, them near or out of sight). distance, break_at,
   steep and sight are air_attack's. */
static void air_run_ends(struct air_flight *f, real distance, real break_at, boolean steep, boolean sight)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct bot_flight_order const *order = f->order;
	long now = f->now;
	/* (a craft slow enough to hang in the air does, at the end of
	   its run: a few seconds over them with everything it has, then
	   off. One that is not goes by at the distance it must.) */
	if (f->bombing)
	{
		pilot->phase = _pilot_cruise;
		f->why = " (to bomb them)";
	}
	/* (coming down on them from high up, as a craft that keeps its
	   height on the way does: off while there is still the height to
	   pull out in. A Pelican that left it to how near them it was
	   went into the ground beside them.) */
	else if (air_cruises() && !order->airborne && body->velocity.k * BOT_TICKS_PER_SECOND < -2.f && f->ground_here > AIR_NO_GROUND * 0.5f &&
		f->over_ground < -body->velocity.k * BOT_TICKS_PER_SECOND * AIR_PULL_OUT + f->need + f->large)
	{
		air_begin_break(pilot, now, FALSE);
		f->why = " (pulling out)";
		pilot->break_yaw = body->facing_yaw;
	}
	else if (order->airborne || f->quick > 0.3f ? distance < break_at || (steep && f->to_place < break_at * 1.5f) :
		distance < 5.f || (steep && f->to_place < 4.f) || (pilot->over_tick != 0 && now - pilot->over_tick > BOT_SECONDS(5.5f)))
	{
		air_begin_break(pilot, now, FALSE);
		f->why = pilot->over_tick != 0 && now - pilot->over_tick > BOT_SECONDS(5.5f) ? " (long enough)" : distance < break_at ? " (near)" : " (steep)";
		/* (on past them, a little to one side: not the same line back) */
		/* (from another craft: always to the same hand, as ships pass,
		   so two that meet do not turn into each other) */
		pilot->break_yaw = bot_wrap(body->facing_yaw + (order->airborne ? -0.7f : (now / 7) % 2 ? 0.4f : -0.4f));
	}
	else if (!sight && now - pilot->sight_tick > BOT_SECONDS(1.5f))
	{
		pilot->phase = _pilot_cruise;
		f->why = " (out of sight)";
	}
}

/* A fight: someone to shoot at, and the craft armed. Which phase it is in
   (a run at them, off from them, or on the way to one), changed by how near
   they are and what is seen of them. */
static void air_attack(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct bot_flight_order const *order = f->order;
	real_point3d const *position = &body->position;
	long now = f->now;
	real reach = MAX(order->range, 30.f);
	real run_in = MIN(reach * 0.9f, 45.f + 100.f * f->quick);
	real break_at = order->airborne ? 6.f + 8.f * f->quick : 8.f + 16.f * f->quick;
	real distance = distance3d(position, &order->destination);
	real below = position->z - order->destination.z;
	/* (a run is made from off to one side of them, not from over their
	   heads: what it fires goes where its pilot looks, as far down as
	   that goes, and the craft comes down a good deal less steeply.
	   From nearly over them it is no run: on past, for room, and round) */
	boolean steep = !order->airborne && below > 1.5f * MAX(f->to_place, 1.f);
	real out_to = MAX(16.f + 85.f * f->quick, below * 0.9f + 6.f);
	boolean sight = air_sight(position, &order->destination, body->vehicle_index);
	/* (its pilot's shields gone: off, straight away from them, and not
	   back until they have come up again, or a good while has gone) */
	boolean flee = order->hurt && distance < 55.f + 100.f * f->quick && pilot->phase != _pilot_break;
	if (order->airborne && distance > 1.f)
	{
		/* (two craft at each other: off while there is still room to
		   miss, a second of how fast the gap is closing) */
		real closing = ((body->velocity.i - order->velocity.i) * (order->destination.x - position->x) +
			(body->velocity.j - order->velocity.j) * (order->destination.y - position->y) +
			(body->velocity.k - order->velocity.k) * (order->destination.z - position->z)) / distance * BOT_TICKS_PER_SECOND;
		break_at = MAX(break_at, closing);
	}
	if (flee)
	{
		air_begin_break(pilot, now, TRUE);
		f->why = " (hurt)";
		pilot->break_yaw = f->to_place > 1.f ? bot_yaw_to(&order->destination, position) : body->facing_yaw;
		/* (not round through a half turn under their guns to do it: on,
		   the way it is going, when that is no nearer them) */
		if (fabs(bot_angle_difference(pilot->break_yaw, body->facing_yaw)) > 1.6f && distance < break_at * 2.f)
			pilot->break_yaw = body->facing_yaw;
		air_hide_yaw(f);
	}
	switch (flee ? NONE : pilot->phase)
	{
	case NONE:
		break;
	case _pilot_run:
		air_run_ends(f, distance, break_at, steep, sight);
		break;
	case _pilot_break:
		if (pilot->away ? (!order->hurt && f->to_place > out_to) || now - pilot->phase_tick > BOT_SECONDS(9.f) :
			f->to_place > out_to || now - pilot->phase_tick > BOT_SECONDS(2.5f + 4.5f * (1.f - f->quick)))
		{
			pilot->phase = _pilot_cruise;
		}
		break;
	default:
		pilot->phase = _pilot_cruise;
		if (sight && distance < run_in && !f->bombing)
		{
			pilot->phase = steep ? _pilot_break : _pilot_run;
			pilot->phase_tick = now;
			pilot->away = FALSE;
			f->why = steep ? " (over them)" : "";
			/* (over them: straight on, the way it is going, and down as
			   the ground lets it) */
			pilot->break_yaw = body->facing_yaw;
		}
		break;
	}
	if (sight)
		pilot->sight_tick = now;
}

/* On its way: the height it has, between what it likes to have over the
   least and the way down to where it is bound (see AIR_CRUISE_LEAST). It
   was the least and nothing else: a Pelican off a base on a cliff went nose
   down into the valley and across it at 10 units over the ground, up and
   down with every rise of it. */
static void air_cruise_height(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	long now = f->now;
	real down_to, liked;
	if (now >= pilot->cruise_until || now < pilot->cruise_until - BOT_SECONDS(AIR_CRUISE_SECONDS * 2.f))
	{
		unsigned long draw = (unsigned long)now * 2654435761UL ^ (unsigned long)(f->body->unit_index & 0xffff) * 40503UL;
		real part = (real)((draw >> 8) & 0xFFFF) / 65535.f;
		pilot->cruise_over = AIR_CRUISE_LEAST + part * (AIR_CRUISE_SLOW + AIR_CRUISE_QUICK * f->quick);
		pilot->cruise_until = now + BOT_SECONDS(AIR_CRUISE_SECONDS * (0.6f + 0.8f * part));
	}
	down_to = MAX(f->least, f->order->destination.z + f->clearance + AIR_GLIDE * f->to_place);
	liked = MIN(f->least + pilot->cruise_over, down_to);
	f->height = MAX(liked, MIN(f->body->position.z, down_to));
}

/* The height to fly at: over the highest ground of the stretch ahead (along
   the way to where it is steering), under the sky. Sets f->least, the
   lowest the ground lets it be, which the height is not when it keeps the
   height it has on its way, or goes over a place to bomb it. */
static void air_height(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_flight_order const *order = f->order;
	real_point3d const *position = &f->body->position;
	real way = pilot->phase == _pilot_break ? pilot->break_yaw : bot_yaw_to(position, &f->steer_to);
	real tops = AIR_NO_GROUND;
	real limit = MAX(position->z, order->destination.z) + 30.f;
	real highest = air_ground_to_clear(position->x, position->y, f->need, f->need, limit, &tops);
	real lowest_sky = air_sky(position->x, position->y, 0.f);
	real along;
	real step = MAX(air.cell * 0.5f, 1.f);
	for (along = step; along <= f->ahead; along += step)
	{
		real x = position->x + (real)cos(way) * along;
		real y = position->y + (real)sin(way) * along;
		highest = MAX(highest, air_ground_to_clear(x, y, f->need * 0.5f, f->need, limit, &tops));
		lowest_sky = MIN(lowest_sky, air_sky(x, y, 0.f));
	}
	if (highest <= AIR_NO_GROUND * 0.5f)
		highest = position->z - f->clearance; /* (off the map's edge: as it is) */
	/* (its clearance over the ground; over the top of a tree, a good deal
	   less will do: there is nothing above that to meet) */
	f->height = MAX(highest + f->clearance, tops + 1.5f + 2.f * f->quick);
	/* (over a chasm, a sea a mile down: no lower than where it is
	   going needs, the ground being nothing to keep by) */
	f->height = MAX(f->height, MIN(position->z, order->destination.z + f->clearance));
	/* (at someone: from a little over them, to come down on them; at
	   another craft, from no lower than it is) */
	if (order->attack && order->armed)
		f->height = MAX(f->height, order->destination.z + (order->airborne ? 0.f : 5.f + 6.f * f->quick));
	/* (to come down at a place: there no lower than it is) */
	if (order->land && f->to_place < 400.f)
		f->height = MAX(f->height, order->destination.z + 4.f);
	f->least = f->height;
	if (pilot->phase == _pilot_cruise && !f->under_roof && air_cruises())
		air_cruise_height(f);
	/* (to bomb a place: over it by more than the bomb hurts, and level:
	   the height it has, if that is enough) */
	if (f->bombing && !pilot->bomb_pass)
	{
		real over_them = f->bomb_lands_z + f->bomb_clear + AIR_BOMB_OVER;
		f->height = MAX(MAX(f->height, over_them), MIN(position->z, over_them + 80.f));
	}
	else if (pilot->bomb_pass)
		f->height = MAX(f->height, position->z);
	if (lowest_sky < AIR_NO_SKY * 0.5f)
	{
		f->height = MIN(f->height, MAX(lowest_sky - AIR_SKY_MARGIN, highest + f->need * 0.5f));
		f->least = MIN(f->least, MAX(lowest_sky - AIR_SKY_MARGIN, highest + f->need * 0.5f));
	}
}

/* how far up or down the craft is going (radians) */
static real air_climb_angle(struct bot_body const *body, real speed)
{
	return (real)asin(PIN(body->velocity.k * BOT_TICKS_PER_SECOND / speed, -1.f, 1.f));
}

/* The look is moved by what the craft is seen to do: the difference between
   the way it is going and the way wanted is made up a little each tick (by
   gain), within these bounds, and added to the look. */
static void air_trim_pitch(struct air_flight *f, real gain, real lowest, real highest)
{
	struct bot_pilot *pilot = f->pilot;
	if (f->speed > 2.f)
		pilot->trim = PIN(pilot->trim + gain * (f->pitch - air_climb_angle(f->body, f->speed)), lowest, highest);
	f->pitch += pilot->trim;
}

/* The look from the height to fly at: round to where it is steering, and up
   or down to the height. */
static void air_look_for_height(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	real_point3d const *position = &f->body->position;
	f->yaw = bot_yaw_to(position, &f->steer_to);
	f->pitch = (real)atan2(f->height - position->z, f->ahead * 0.5f);
	if (f->plane && !pilot->hovering)
	{
		/* A plane on its wings. They carry three quarters of it at the speed
		   it flies, and its nose a little up does the rest: so it goes a
		   little under where its pilot looks, and it is a second coming
		   round to a new look. A steady hand for it: the way up or down to
		   its height over a good stretch ahead, no steeper than it climbs,
		   and the look moved slowly by what it is seen to do. */
		f->pitch = (real)atan2(f->height - position->z, MAX(f->ahead * 1.5f, 40.f));
		f->pitch = PIN(f->pitch, -0.4f, 0.45f);
		air_trim_pitch(f, 0.01f, -0.25f, 0.35f);
	}
	else if (bot_vehicle_kind(f->craft->definition_index) == _bot_vehicle_fighter)
	{
		/* A fighter does not go where its pilot looks, quite: its nose sits
		   a quarter of a radian over the look (vehicles.c: for a person,
		   who sees over it), and it sinks under its own weight; a Banshee
		   whose pilot looks 25 degrees down flies 6 degrees down. So the
		   look is trimmed by what the craft is seen to do: the way up or
		   down that is wanted, against the way it is going, and the
		   difference made up a little each tick. */
		f->pitch = PIN(f->pitch, -0.6f, 0.6f);
		air_trim_pitch(f, 0.04f, -0.7f, 0.4f);
	}
	else
		f->pitch = PIN(f->pitch, -0.45f, 0.6f);
}

/* Looking for the gate the map gives the craft where it stands (a jet put
   down before its gate, or under a roof). */
static void air_gate_look(struct air_flight const *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	real_point3d const *position = &body->position;
	long now = f->now;

	/* (through it: the map's script has put the craft down somewhere else) */
	if (pilot->gate != NONE && distance3d(position, &pilot->gate_entry) > 300.f)
	{
		pilot->gate = NONE;
		pilot->gate_from_rest = FALSE;
		pilot->taken_tick = now - BOT_SECONDS(60.f);
		pilot->exit_valid = FALSE;
	}
	/* (Where it stood when it was taken, too, roof over it or none: a jet
	   is put down before its gate, a few lengths from it and square to it,
	   and that is the way the map means it to leave.) */
	if (!f->under_roof && pilot->phase != _pilot_land && now - pilot->taken_tick < BOT_SECONDS(12.f) &&
		distance3d(position, &pilot->stuck_point) < 60.f && (pilot->gate_tick == 0 || pilot->gate_from_rest))
	{
		real_point3d out;
		long gate = (now - pilot->gate_tick > BOT_SECONDS(1.f) || pilot->gate_tick == 0) ?
			bot_country_gate(body->vehicle_index, position, body->facing_yaw, f->plane ? 260.f : 60.f, pilot->gate, &pilot->gate_entry, &out, &pilot->gate_across) :
			pilot->gate;
		if (now - pilot->gate_tick > BOT_SECONDS(1.f) || pilot->gate_tick == 0)
			pilot->gate_tick = now;
		pilot->gate = gate;
		pilot->gate_from_rest = gate != NONE && fabs(bot_angle_difference(bot_yaw_to(position, &pilot->gate_entry), body->facing_yaw)) < 0.7f;
		if (!pilot->gate_from_rest)
			pilot->gate = NONE;
	}
	else if (!f->under_roof)
		pilot->gate_from_rest = FALSE;
	if (f->under_roof && pilot->phase != _pilot_land && (now - pilot->gate_tick > BOT_SECONDS(1.f) || now < pilot->gate_tick))
	{
		real_point3d out;
		pilot->gate_tick = now;
		pilot->gate = bot_country_gate(body->vehicle_index, position, body->facing_yaw, 260.f, pilot->gate, &pilot->gate_entry, &out, &pilot->gate_across);
		if (pilot->gate != NONE && bot_logging() && f->phase_before == pilot->phase && now % 30 == 0)
			bot_log("pilot %ld t%ld gate %ld at %.0f %.0f %.0f, %.0f off", body->unit_index & 0xffff, now, pilot->gate, pilot->gate_entry.x,
				pilot->gate_entry.y, pilot->gate_entry.z, distance3d(position, &pilot->gate_entry));
	}
	if (!f->under_roof && !pilot->gate_from_rest)
		pilot->gate = NONE;
}

/* Out by the gate the map gives the craft. */
static void air_through_gate(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	real_point3d const *position = &body->position;
	real off = distance3d(position, &pilot->gate_entry);
	real turn;
	pilot->exit_valid = TRUE;
	pilot->exit_point = pilot->gate_entry;
	pilot->exit_open = off;
	f->yaw = bot_yaw_to(position, &pilot->gate_entry);
	turn = (real)fabs(bot_angle_difference(f->yaw, body->facing_yaw));
	/* The gate is a tube in the hangar's wall, hardly wider than the
	   craft and lower than it is wide, with the volume a few lengths
	   down it. The craft goes at its mouth level, along the floor or a
	   little over it as the volume is (not up at it: the mouth's top
	   is a step over a craft's own), round to it slowly while it does
	   not point at it, and fast once it does; and up a little, slowly,
	   when something on the floor has stopped it. */
	f->pitch = (real)atan2(pilot->gate_entry.z - position->z, MAX(bot_distance2d(position, &pilot->gate_entry), 8.f));
	f->pitch = PIN(f->pitch, -0.15f, 0.2f);
	f->throttle = turn > 0.9f ? 0.3f : turn > 0.3f ? 0.5f : 1.f;
	if (!f->plane && f->speed < 1.5f && f->now - pilot->stuck_tick > BOT_SECONDS(1.5f))
	{
		f->pitch = (f->now / 45) % 2 ? 0.45f : -0.1f;
		f->throttle = 0.5f;
	}
	f->height = f->least = pilot->gate_entry.z;
	pilot->indoors = TRUE;
	/* (a mouth many lengths wide, for the large craft: flown into, on
	   its wings, from as far off as it is seen) */
	pilot->wings_out = f->plane && pilot->gate_across > 40.f && turn < 0.6f;
	if (pilot->wings_out)
	{
		f->pitch = (real)atan2(pilot->gate_entry.z - position->z, MAX(bot_distance2d(position, &pilot->gate_entry), 8.f));
		f->pitch = PIN(f->pitch, -0.3f, 0.4f) + 0.08f;
		f->throttle = 1.f;
	}
	/* (At the tube's mouth, square to it, with nothing of the map
	   between the craft's middle and the volume's: it is put in the
	   volume, as a Warthog at a gate's frame is, bot_country_drive,
	   and the map's script does the rest. Flying a jet down a tube a
	   wing's width across is not something the bots do.) */
	if ((off < 4.f + f->speed * 0.15f || (off < 24.f && turn < 0.3f && fabs(pilot->gate_entry.z - position->z) < 1.8f &&
			air_sight(position, &pilot->gate_entry, body->vehicle_index))) &&
		bot_country_gate_enter(body->vehicle_index, pilot->gate) && bot_logging())
		bot_log("pilot %ld t%ld into gate %ld at %.0f %.0f %.0f", body->unit_index & 0xffff, f->now, pilot->gate, pilot->gate_entry.x,
			pilot->gate_entry.y, pilot->gate_entry.z);
}

/* A look round from under a roof: sixteen ways about and four upward, for
   the one that is open furthest, the way to where we are going sooner than
   its opposite (a hangar's mouth, a cave's). Sets the pilot's exit. */
static void air_find_exit(struct air_flight const *f)
{
	static real const pitches[4] = { 0.f, 0.15f, 0.3f, 0.5f };
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	real_point3d const *position = &body->position;
	real to_there = bot_yaw_to(position, &f->order->destination);
	real best = -1.f;
	long d, k;
	pilot->exit_tick = f->now;
	pilot->exit_valid = FALSE;
	for (d = 0; d < 16; d++)
	{
		real way = d * 0.3926991f;
		for (k = 0; k < 4; k++)
		{
			real open = air_ray(position, way, pitches[k], 300.f, body->vehicle_index);
			real score = MIN(open, 300.f) * (1.f + 0.15f * (real)cos(bot_angle_difference(way, to_there))) - pitches[k] * 20.f;
			if (open > 25.f && score > best)
			{
				best = score;
				pilot->exit_valid = TRUE;
				pilot->exit_open = open;
				pilot->exit_point.x = position->x + (real)(cos(way) * cos(pitches[k])) * MIN(open, 60.f);
				pilot->exit_point.y = position->y + (real)(sin(way) * cos(pitches[k])) * MIN(open, 60.f);
				pilot->exit_point.z = position->z + (real)sin(pitches[k]) * MIN(open, 60.f);
			}
		}
	}
}

/* Out of a roof by the nearest way out found, level, then up. */
static void air_out_of_roof(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	real_point3d const *position = &body->position;
	if (f->now - pilot->exit_tick > BOT_SECONDS(0.5f) || !pilot->exit_valid)
		air_find_exit(f);
	if (pilot->exit_valid)
	{
		f->yaw = bot_yaw_to(position, &pilot->exit_point);
		f->pitch = (real)atan2(pilot->exit_point.z - position->z, MAX(bot_distance2d(position, &pilot->exit_point), 2.f));
		f->pitch = PIN(f->pitch, -0.2f, 0.55f);
		/* (out of it as fast as it goes, with a long clear way out ahead
		   and its nose on it: a jet guns it out of its hangar. Slowly
		   while it is still coming round to the way, or the way is short.) */
		if (!(pilot->exit_open > 70.f && fabs(bot_angle_difference(f->yaw, body->facing_yaw)) < 0.3f && f->quick > 0.5f))
			f->throttle = MIN(f->throttle, 0.3f + 0.4f * (1.f - f->quick));
		f->height = pilot->exit_point.z;
		/* (a plane: off the floor first, a little, and along over it) */
		if (f->plane)
			f->height = MAX(f->height, pilot->rest_z + 2.5f);
		f->least = f->height;
		pilot->indoors = TRUE;
		/* (a plane with two hundred units of nothing before it is
		   under no roof that matters: the top of a mountain with a
		   ledge over it, a cavern the size of a valley. On its wings.) */
		pilot->wings_out = f->plane && pilot->exit_open > 200.f && fabs(bot_angle_difference(f->yaw, body->facing_yaw)) < 0.5f;
		if (pilot->wings_out)
		{
			f->pitch = PIN(f->pitch, -0.2f, 0.4f) + 0.08f;
			f->throttle = 1.f;
		}
	}
}

/* Under a roof (a hangar, a cave: the ground of this square is over us): out
   first, level, to the nearest square that is open to the sky at this height
   and has nothing between us and it; then up. Or by the gate the map gives
   the craft, where it has one: a jet in a hangar with no mouth, and a frame
   in its wall that the map's script carries it from to the sky outside,
   bot_country.c. */
static void air_leave_roof(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	air_gate_look(f);
	if ((f->under_roof || pilot->gate_from_rest) && pilot->phase != _pilot_land && pilot->gate != NONE)
		air_through_gate(f);
	else if (f->under_roof && pilot->phase != _pilot_land)
		air_out_of_roof(f);
	else
	{
		pilot->indoors = FALSE;
		pilot->wings_out = FALSE;
	}
}

/* Where the look is to be on a run: its shots go where its pilot looks and
   the craft follows the look, so it is at where they will be when a shot
   gets there: from the craft's own seat, which a shot leaves at the craft's
   speed and its own together. */
static void air_lead_aim(struct air_flight const *f, real_point3d *aim)
{
	struct bot_body const *body = f->body;
	struct bot_flight_order const *order = f->order;
	real_point3d const *position = &body->position;
	real_vector3d drift;
	real shot = MAX(order->shot_speed, 10.f) / BOT_TICKS_PER_SECOND;
	long pass;
	*aim = order->destination;
	drift.i = order->velocity.i - body->velocity.i;
	drift.j = order->velocity.j - body->velocity.j;
	drift.k = order->velocity.k - body->velocity.k;
	for (pass = 0; pass < 2; pass++)
	{
		real flight = MIN(distance3d(position, aim) / shot, 60.f);
		aim->x = order->destination.x + drift.i * flight;
		aim->y = order->destination.y + drift.j * flight;
		aim->z = order->destination.z + drift.k * flight + (order->airborne ? 0.f : 0.3f);
	}
}

/* Its other gun: slow, and falling as it goes (a Banshee's fuel rod is a
   second getting thirty units, and a unit lower by then). At something on
   the ground and near enough: the look goes to where that will land on
   them, the trigger is pulled when it is there, and let go (it fires once
   a pull). */
static void air_run_heavy(struct air_flight *f, real distance, boolean guns_bear)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct bot_flight_order const *order = f->order;
	struct bot_flight_result *result = f->result;
	real_point3d const *position = &body->position;
	long now = f->now;
	if (pilot->heavy_press > 0)
	{
		result->fire_heavy = TRUE;
		if (--pilot->heavy_press == 0)
			pilot->heavy_tick = now;
	}
	/* (a great blast, though: not from here. On a pass, or not at all) */
	else if (order->heavy_ready && (now - pilot->heavy_tick > BOT_SECONDS(0.8f) || now < pilot->heavy_tick) &&
		!(order->heavy_bomb && order->heavy_hurts >= AIR_BOMB_LARGE))
	{
		real lob_yaw = f->yaw, lob_pitch = f->pitch;
		real lands = air_lob(position, &body->velocity, &order->destination, &order->velocity, order, &lob_yaw, &lob_pitch);
		if (order->heavy_bomb)
		{
			/* (not from so near that it is under it when it goes off) */
			if (lands < 5.f && position->z > order->destination.z + 12.f && distance > 40.f)
			{
				pilot->heavy_press = 3;
				result->fire_heavy = TRUE;
				if (bot_logging())
					bot_log("pilot %ld t%ld bomb away, %.0f off and %.0f up", body->unit_index & 0xffff, now, f->to_place, position->z - order->destination.z);
			}
		}
		else if (lands < 1.f)
		{
			f->yaw = lob_yaw;
			f->pitch = lob_pitch;
			/* (steadier, the moment it takes to put it on them) */
			f->throttle = MIN(f->throttle, 0.6f);
			if (guns_bear && air_look_off(order, f->yaw, f->pitch) < 0.02f + 0.4f / MAX(distance, 4.f))
			{
				pilot->heavy_press = 3;
				result->fire_heavy = TRUE;
				if (bot_logging())
					bot_log("pilot %ld t%ld heavy pulled, %.0f off", body->unit_index & 0xffff, now, distance);
			}
		}
	}
}

/* A run at them: at them as fast as it goes, firing while they are in the
   sights, and the other gun (a fuel rod, a bomb) when that is of use. */
static void air_run(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct bot_flight_order const *order = f->order;
	struct bot_flight_result *result = f->result;
	real_point3d const *position = &body->position;
	real_point3d aim;
	real distance = distance3d(position, &order->destination);
	real off;
	boolean guns_bear;

	air_lead_aim(f, &aim);
	f->yaw = bot_yaw_to(position, &aim);
	f->pitch = (real)atan2(aim.z - position->z, MAX(bot_distance2d(position, &aim), 0.5f));
	f->throttle = 1.f;
	off = air_look_off(order, f->yaw, f->pitch);
	/* (What it fires leaves along its pilot's look only while the craft
	   itself points within fifty degrees of that: its guns are fixed
	   to it, and the game bends a shot toward the look by so much and
	   no more, aim_assist.c. Coming round onto someone, the look is on
	   them long before the craft is.) */
	{
		real_vector3d const *nose = &f->craft->object.forward;
		real swung = (real)acos(PIN(nose->i * cos(f->pitch) * cos(f->yaw) + nose->j * cos(f->pitch) * sin(f->yaw) + nose->k * sin(f->pitch), -1.f, 1.f));
		guns_bear = swung < 0.8f;
		/* (coming round onto them: slowly, in its own length, not in a
		   great sweep out over the sea) */
		if (swung > 1.2f && !order->airborne && f->quick <= 0.5f)
			f->throttle = 0.35f;
		/* (and at the end of it, a craft that can: hanging there a little
		   way off and over them, its guns on them, creeping in while it
		   has height to lose and still when it has not; the heavy gun
		   is ready again in the time it stays) */
		else if (!order->airborne && f->quick <= 0.3f && distance < 15.f)
		{
			f->throttle = guns_bear && f->over_ground > 4.f && position->z > order->destination.z + 3.5f && distance > 9.f ? 0.15f : 0.f;
			if (pilot->over_tick == 0)
				pilot->over_tick = f->now;
		}
	}
	/* (a quick craft opens up from as far as its gun carries: it is on
	   them and past in a second or two) */
	if (guns_bear && off < 0.03f + 0.5f / MAX(distance, 4.f) && distance < MIN(MAX(order->range, 30.f), f->quick > 0.5f ? 150.f : 75.f))
		result->fire = TRUE;
	/* (A slow craft that has come at them from high up, off a hill or
	   over the trees: down first, more steeply than its guns would
	   have it, to come in on them from a little over their heads and
	   not from the sky; it fires when its look is back on them.) */
	if (!order->airborne && f->quick <= 0.3f && f->to_place > 10.f && f->over_ground > 5.f &&
		position->z > order->destination.z + 7.f + 0.3f * MAX(f->to_place - 12.f, 0.f))
	{
		f->pitch = MAX(f->pitch - 0.45f, -1.1f);
		result->fire = FALSE;
	}
	/* (A bomb is let go from wherever it will fall on them from, a long
	   way up and off as that may be: the same reckoning, with no aim
	   to it, the craft's own way being the bomb's.) */
	/* (... and a missile that flies level, from further off than one that falls) */
	/* (... and at another craft too, which one that falls is no use against) */
	else if (order->heavy && (!order->airborne || (order->heavy_gravity <= 0.f && !order->heavy_bomb)) &&
		distance < (order->heavy_bomb ? 400.f : 17.f + 40.f * f->quick + (order->heavy_gravity <= 0.f ? 60.f * f->quick : 0.f)) && distance > 6.f)
		air_run_heavy(f, distance, guns_bear);
}

/* A bombing pass. Its bomb leaves with the craft's own speed and falls: from
   105 units over them it is eight seconds coming down, and 250 units further
   on by then. So it is flown at them level and high, what the bomb would do
   if it were let go now is reckoned every tick (air_lob), and it is let go
   when that is on them, or as near them as this pass will bring it. The craft
   is over the blast when it goes off, further up than it reaches. Then on,
   the way it is going, out to where there is room to come round for the
   next. */
static void air_bomb_pass(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct bot_flight_order const *order = f->order;
	struct bot_flight_result *result = f->result;
	real_point3d const *position = &body->position;
	long now = f->now;

	if (f->bombing && !pilot->indoors && pilot->phase == _pilot_cruise)
	{
		real over_them = position->z - f->bomb_lands_z;
		real lob_yaw = f->yaw, lob_pitch = f->pitch;
		real_point3d lands_at = order->destination;
		real lands;
		real near_enough = MAX(4.f, order->heavy_kills * AIR_BOMB_NEAR);
		boolean again = now - pilot->bomb_tick == 1;
		lands_at.z = f->bomb_lands_z;
		lands = air_lob(position, &body->velocity, &lands_at, &order->velocity, order, &lob_yaw, &lob_pitch);
		if (pilot->heavy_press > 0)
		{
			result->fire_heavy = TRUE;
			if (--pilot->heavy_press == 0)
			{
				pilot->heavy_tick = now;
				air_leave_bomb(f);
				f->why = " (bomb gone)";
			}
		}
		else if (over_them >= f->bomb_clear && lands < near_enough && again &&
			(lands > pilot->bomb_before - 0.02f || lands < near_enough * 0.25f) &&
			(now - pilot->heavy_tick > BOT_SECONDS(2.f) || now < pilot->heavy_tick))
		{
			pilot->heavy_press = 3;
			result->fire_heavy = TRUE;
			if (bot_logging())
				bot_log("pilot %ld t%ld bomb away, %.0f off and %.0f up, to land %.0f off them (it kills within %.0f)", body->unit_index & 0xffff, now, f->to_place,
					over_them, lands, order->heavy_kills);
		}
		/* (over them and nothing let go, the line of it wrong or the height:
		   on, and round for another) */
		else if (f->to_place < MAX(20.f, f->speed * 0.7f) && again && lands > pilot->bomb_before)
		{
			air_leave_bomb(f);
			f->why = " (over them, no bomb)";
		}
		if (bot_logging() && now % 30 == 0)
			bot_log("pilot %ld t%ld bombing: %.0f off, %.0f over them, a bomb now would land %.0f off (near enough: %.0f)", body->unit_index & 0xffff, now, f->to_place,
				over_them, lands, near_enough);
		pilot->bomb_before = lands;
		pilot->bomb_tick = now;
	}
	/* (the bomb let go and the gun not ready for another, which ends the
	   bombing above before its press is out: the pass is over all the same.
	   It was flown on at the place it had just bombed.) */
	else if (pilot->phase == _pilot_cruise && pilot->heavy_press > 0 && order->heavy_bomb)
	{
		pilot->heavy_press = 0;
		pilot->heavy_tick = now;
		air_leave_bomb(f);
		f->why = " (bomb gone)";
	}
	else if (pilot->phase != _pilot_run && pilot->heavy_press > 0 && order->heavy_bomb)
		pilot->heavy_press = 0;
}

/* The last word: what is in the way. Rays the way it is going, as far as it
   gets in a second and a bit, one from its middle and one from each wing:
   something ahead (a tree the map's squares do not show whole, another
   craft, a bridge) and it is up and over; that way shut too, and it is
   round; something off one wing only, and it leans away from that. */
static void air_in_the_way(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct bot_flight_result *result = f->result;
	real_point3d const *position = &body->position;
	long now = f->now;

	/* (nor on its way into a gate: that is a frame in a wall, and the wall is
	   where it is going) */
	if (pilot->phase != _pilot_land && !(pilot->phase == _pilot_run && pilot->over_tick != 0 && f->speed < 3.f) &&
		!((f->under_roof || pilot->gate_from_rest) && pilot->gate != NONE))
	{
		/* (a large craft out in the open: from its own wings' ends, and as
		   much further ahead as it is long. Under a roof and on its jets it
		   is among walls it means to be among, and looks as a small one) */
		boolean wide = f->large > 0.f && !pilot->indoors && !(f->plane && pilot->hovering) && air_cruises();
		real look = MAX(8.f, f->speed * 1.2f) + (wide ? f->large : 0.f);
		real way_yaw = f->speed > 1.5f ? (real)atan2(body->velocity.j, body->velocity.i) : body->facing_yaw;
		real way_pitch = f->speed > 1.5f ? air_climb_angle(body, f->speed) : 0.f;
		real span = MAX(0.9f + 1.2f * f->quick, wide ? f->craft->object.bounding_sphere_radius * 0.75f : 0.f); /* (half of it) */
		real_point3d left_wing, right_wing;
		real free_way = air_ray(position, way_yaw, way_pitch, look, body->vehicle_index);
		real free_left, free_right;
		air_wing_tips(position, way_yaw, span, &left_wing, &right_wing);
		free_left = air_ray(&left_wing, way_yaw, way_pitch, look, body->vehicle_index);
		free_right = air_ray(&right_wing, way_yaw, way_pitch, look, body->vehicle_index);
		if (free_way < look * 0.95f || (free_left < look * 0.95f && free_right < look * 0.95f))
		{
			real nearest = MIN(free_way, MIN(free_left, free_right));
			real up = air_ray(position, way_yaw, 0.65f, look, body->vehicle_index);
			f->pitch = MAX(f->pitch, nearest < look * 0.5f ? 0.8f : 0.55f);
			if (up < look * 0.7f)
			{
				/* (up is shut as well: the side with more room) */
				real left = air_ray(position, way_yaw + 0.9f, 0.1f, look, body->vehicle_index);
				real right = air_ray(position, way_yaw - 0.9f, 0.1f, look, body->vehicle_index);
				f->yaw = bot_wrap(way_yaw + (left > right ? 1.2f : -1.2f));
				f->throttle = MIN(f->throttle, 0.5f);
			}
			/* (not while it is still standing where it was left, with its
			   neighbour or the wall of its bay before it) */
			if (pilot->phase == _pilot_run && f->speed > 2.f)
			{
				air_begin_break(pilot, now, FALSE);
				pilot->break_yaw = f->yaw;
				f->why = " (something in the way)";
			}
			result->fire = FALSE;
			result->fire_heavy = FALSE;
			pilot->heavy_press = 0;
		}
		else if (free_left < look * 0.95f || free_right < look * 0.95f)
		{
			/* (and for a moment after it is clear: not back into it) */
			pilot->swerve = free_left < free_right ? -0.5f : 0.5f;
			pilot->swerve_until = now + BOT_SECONDS(0.4f);
		}
		if (now < pilot->swerve_until && now + BOT_SECONDS(2.f) > pilot->swerve_until)
		{
			f->yaw = bot_wrap(way_yaw + pilot->swerve);
			if (pilot->phase == _pilot_run)
			{
				result->fire = FALSE;
				result->fire_heavy = FALSE;
			}
		}
		/* (too low for the ground it is over: up, whatever else) */
		if (f->ground_here > AIR_NO_GROUND * 0.5f && f->over_ground < f->need * 0.5f && f->over_ground > -1.f && pilot->phase != _pilot_run)
			f->pitch = MAX(f->pitch, 0.4f);
	}
}

/* a plane on its jets: where it is making for */
struct air_hover
{
	real_point3d to;
	real want_z;
	real limit; /* how fast along its nose, at the most */
	real more; /* (further to go beyond the point it is making for) */
	real face; /* the way it faces */
	boolean settle; /* straight down, over the place */
};

/* The throttle for a speed along its nose: Leaning gets a plane on its jets
   two units a second and no more, the air being as thick as it is. Faster
   than that is its engine's to do: a little throttle, which drives it at a
   little speed and leaves most of its jets. */
static real air_hover_throttle(real top_speed, real want, real forward)
{
	real full = top_speed / BOT_TICKS_PER_SECOND; /* (the throttle that is all of it) */
	real low = 0.f, high = 1.f;
	long pass;
	for (pass = 0; pass < 12; pass++)
	{
		/* (vehicles.c: driven at 0.05 of what it lacks of the
		   throttle's speed, by the square of how far open that
		   is; the air takes about 0.02 of what it has) */
		real part = (low + high) * 0.5f;
		real goes = part * full * 0.05f * part * part / (AIR_DRAG + 0.05f * part * part) * BOT_TICKS_PER_SECOND;
		if (goes < want)
			low = part;
		else
			high = part;
	}
	return PIN(low * full + 0.02f * (want - forward), 0.f, full);
}

/* Down at a place on its jets: in over it at the height the ground on the
   way wants, then straight down. */
static void air_hover_landing(struct air_flight const *f, real level, struct air_hover *h)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct bot_flight_order const *order = f->order;
	struct bot_flight_result *result = f->result;
	real_point3d const *position = &body->position;
	long now = f->now;
	real ground_z = order->destination.z;
	real_point3d under = *position;
	boolean low = bot_ground(&under, 12.f, body->vehicle_index, &ground_z);
	real over = low ? position->z - ground_z : 12.f;
	real left = MAX(f->to_place - order->short_of, 0.f);
	h->to = order->destination;
	if (f->to_place > 5.f)
		pilot->hover_yaw = bot_yaw_to(position, &h->to);
	h->face = pilot->hover_yaw;
	/* (as far short of it by what the order says) */
	h->to.x -= (real)cos(h->face) * MIN(order->short_of, f->to_place);
	h->to.y -= (real)sin(h->face) * MIN(order->short_of, f->to_place);
	h->limit = f->top_speed;
	if (pilot->way_in == 1)
	{
		/* (to the open air outside the way in, at its height; and
		   there, round to face it, and in) */
		h->want_z = MAX(order->destination.z, f->to_place > 20.f ? f->least - f->clearance + 5.f : order->destination.z);
		if ((f->to_place < 6.f && level < 3.f) || now - pilot->land_tick > BOT_SECONDS(40.f))
		{
			pilot->way_in = pilot->way_in_trail != NONE ? 2 : 3;
			pilot->land_tick = now;
		}
	}
	else if (pilot->way_in == 2)
	{
		/* (in along the way a craft came out by, turn by turn, at
		   the height it had) */
		h->limit = 8.f;
		h->want_z = order->destination.z;
		h->more = (pilot->way_in_next - pilot->way_in_last) * AIR_TRAIL_STEP + 10.f;
		if (f->to_place < 6.f || now - pilot->land_tick > BOT_SECONDS(25.f))
		{
			pilot->land_tick = now;
			if (--pilot->way_in_next < pilot->way_in_last)
				pilot->way_in = 3;
		}
	}
	else if (!pilot->down)
	{
		/* (in over the place at the height the ground on the way
		   wants, then down the last of it as it comes up to it) */
		h->want_z = f->to_place > 14.f ? MAX(f->least - f->clearance + 5.f, order->destination.z + pilot->rest_over + 5.f) :
			order->destination.z + pilot->rest_over + 1.5f + left * 0.4f + (level > 4.f ? 2.f : 0.f);
		if (pilot->way_in == 3)
		{
			/* (under the roof: along over its floor, no faster than
			   it can stop; and no further than what stands in it) */
			h->limit = 8.f;
			h->want_z = order->destination.z + pilot->rest_over + (left > 6.f ? 2.f : 1.f);
			if (left > 4.f && air_ray(position, body->facing_yaw, 0.f, 9.f, body->vehicle_index) < 9.f &&
				fabs(bot_angle_difference(h->face, body->facing_yaw)) < 0.3f)
			{
				left = 0.f;
			}
		}
		if ((left < 3.f && level < 2.5f) || (left < 8.f && level < 1.f) || now - pilot->land_tick > BOT_SECONDS(40.f))
		{
			pilot->down = TRUE;
			pilot->down_tick = now;
			pilot->still_ticks = 0;
		}
	}
	else
	{
		/* (straight down, slower as the ground comes up; round first,
		   where it is to stand tail to the place: a Pelican's hold opens
		   at its tail, and whoever gets out of one that stands nose to
		   the flag has its hull between them and it) */
		boolean round = order->tail_to && now - pilot->down_tick < BOT_SECONDS(7.f) &&
			fabs(bot_angle_difference(bot_wrap(pilot->hover_yaw + 3.1415927f), body->facing_yaw)) > 0.4f;
		h->settle = TRUE;
		h->to = *position;
		if (order->tail_to)
			h->face = bot_wrap(pilot->hover_yaw + 3.1415927f);
		h->want_z = round ? position->z : position->z - MIN(4.f, 0.6f + 0.8f * MAX(over - pilot->rest_over, 0.f)) / 1.2f;
		pilot->still_ticks = f->speed < 0.6f ? pilot->still_ticks + 1 : 0;
		if ((low && over < pilot->rest_over + 0.5f && f->speed < 1.5f) || pilot->still_ticks > BOT_SECONDS(1.f))
		{
			result->landed = TRUE;
			pilot->rest_z = position->z;
			if (!order->stay)
				result->get_out = TRUE;
		}
	}
}

/* Under a roof, its own height over the floor it is over and the floor a
   length ahead, whatever that floor does: a plinth in the middle of a
   hangar, a ramp; and a little more while something holds it. */
static void air_hover_floor(struct air_flight const *f, struct air_hover *h)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	real_point3d const *position = &body->position;
	long now = f->now;
	real_point3d under = *position, before = *position;
	real floor_z;
	real floor = position->z - 20.f;
	before.x += (real)cos(h->face) * 7.f;
	before.y += (real)sin(h->face) * 7.f;
	if (bot_ground(&under, 10.f, body->vehicle_index, &floor_z))
		floor = floor_z;
	if (bot_ground(&before, 10.f, body->vehicle_index, &floor_z))
		floor = MAX(floor, floor_z);
	if (!(pilot->way_in == 3 && f->to_place < 14.f))
		h->want_z = MAX(h->want_z, floor + pilot->rest_over + 1.6f);
	if (now - pilot->stuck_tick > BOT_SECONDS(1.5f) && h->limit > 0.f)
		h->want_z += MIN((now - pilot->stuck_tick - BOT_SECONDS(1.5f)) / (real)BOT_TICKS_PER_SECOND, 3.f);
}

/* What a plane on its jets makes for: the place it is to land at, the way
   out of a roof, or the height it is to rise to from where it stands. */
static void air_hover_target(struct air_flight const *f, real level, boolean landing, struct air_hover *h)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	real_point3d const *position = &body->position;
	h->to = *position;
	h->want_z = f->least;
	h->limit = 0.f;
	h->more = 0.f;
	h->face = f->yaw;
	h->settle = FALSE;
	if (landing)
		air_hover_landing(f, level, h);
	else if (pilot->indoors)
	{
		h->to = pilot->exit_point;
		h->limit = 9.f;
		h->face = bot_yaw_to(position, &h->to);
	}
	else
	{
		/* (up from where it stands, round to the way it is going; and
		   on its wings from there. A little way up, no more: it climbs
		   faster by its wings than by its jets, and what is over it
		   here, the lip of a hangar's mouth, is not over it a length
		   further on) */
		h->to = pilot->stuck_point;
		h->want_z = MIN(f->least, pilot->rest_z + 3.f);
		/* (something over it that it cannot rise through, the
		   underside of a bridge it came down beneath: no higher
		   than there is room, and off on its wings from where it
		   is) */
		if (h->want_z > position->z + 1.f)
		{
			real room = air_ray(position, f->yaw, AIR_UP, h->want_z - position->z + 6.f, body->vehicle_index);
			if (room < h->want_z - position->z + 5.f)
				h->want_z = MIN(h->want_z, MAX(position->z + room - 5.f, position->z - 1.f));
		}
		if (position->z > h->want_z - 1.f && fabs(bot_angle_difference(f->yaw, body->facing_yaw)) < 0.8f)
			pilot->hovering = FALSE;
	}
	if (pilot->hovering && (pilot->indoors || pilot->way_in >= 2) && !h->settle)
		air_hover_floor(f, h);
}

/* On its jets: lean toward the place, with a little throttle if it is to go
   faster than leaning gets it, and the jet or crouch button for the height. */
static void air_hover_fly(struct air_flight *f, struct air_hover const *h, boolean landing, real climbing)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct bot_flight_result *result = f->result;
	real_point3d const *position = &body->position;
	/* how fast along its nose: toward the place while that lies
	   ahead, and no faster than it can stop from in what is left */
	real nose_i = f->craft->object.forward.i, nose_j = f->craft->object.forward.j;
	real flat = (real)sqrt(nose_i * nose_i + nose_j * nose_j);
	real forward, ahead_of_it, want, wanted_climb;
	if (flat < 0.05f)
	{
		nose_i = (real)cos(body->facing_yaw);
		nose_j = (real)sin(body->facing_yaw);
		flat = 1.f;
	}
	nose_i /= flat;
	nose_j /= flat;
	forward = (body->velocity.i * nose_i + body->velocity.j * nose_j) * BOT_TICKS_PER_SECOND;
	ahead_of_it = (h->to.x - position->x) * nose_i + (h->to.y - position->y) * nose_j;
	/* (the air stops it in a second or two with its engine shut:
	   as fast as it is far, about) */
	want = AIR_HOVER_SLOWING * MAX((real)fabs(ahead_of_it) + (ahead_of_it > 0.f ? h->more : 0.f) - 1.f, 0.f);
	want = ahead_of_it < 0.f ? -MIN(want, 2.f) : MIN(want, h->limit);
	/* (under a roof it goes along its nose only when its nose is
	   on the way out: a Longsword stands on its pad with its
	   tail to the hangar's mouth, and driven off while it was
	   still coming round it went into the wall beside it) */
	if (pilot->indoors && !landing && want > 0.f)
		want *= PIN((0.5f - (real)fabs(bot_angle_difference(h->face, body->facing_yaw))) / 0.25f, 0.f, 1.f);
	f->yaw = h->face;
	f->pitch = PIN(-AIR_HOVER_LEAN * (want - forward), -0.35f, 0.6f);
	/* (Leaning gets it two units a second and no more, the air
	   being as thick as it is. Faster than that is its engine's
	   to do: a little throttle, which drives it at a little
	   speed and leaves most of its jets.) */
	f->throttle = 0.f;
	if (want > 2.5f)
	{
		f->throttle = air_hover_throttle(f->top_speed, want, forward);
		f->pitch = PIN(f->pitch, -0.2f, 0.3f);
	}
	wanted_climb = PIN(1.2f * (h->want_z - position->z), h->settle ? -4.f : -5.f, 6.f);
	result->climb = climbing < wanted_climb;
	result->descend = !result->climb && climbing > wanted_climb + 0.4f;
	result->fire = FALSE;
	result->fire_heavy = FALSE;
	if (result->landed)
	{
		/* (down: nothing, and it stands) */
		result->climb = FALSE;
		result->descend = FALSE;
		f->pitch = 0.f;
	}
	if (bot_logging() && f->now % 15 == 0 && air_trace())
		bot_log("plane %ld t%ld %s at %.1f %.1f %.1f to %.1f %.1f (%.1f off, %.1f ahead) want z %.1f; forward %.1f want %.1f, climbing %.1f want %.1f: pitch %.2f throttle %.2f%s%s%s",
			body->unit_index & 0xffff, f->now, landing ? pilot->down ? "down" : "landing" : pilot->indoors ? "indoors" : "up",
			position->x, position->y, position->z, h->to.x, h->to.y, bot_distance2d(position, &h->to), ahead_of_it, h->want_z, forward, want,
			climbing, wanted_climb, f->pitch, f->throttle, result->climb ? " jets" : "", result->descend ? " sink" : "", result->landed ? " LANDED" : "");
}

/* A plane (vehicles.c, update_human_plane_physics). It has two ways of
   staying up. Its wings: at full throttle it is driven hard along its
   nose, is at its top speed in a second, and is lifted by that speed;
   it goes where its pilot looks, as a Banshee does. Its jets: with the
   throttle shut they hold it up, all but (the jump button for a little
   more than its weight, the crouch button for a good deal less), along
   its own up: so it is a helicopter then, going the way it leans, nose
   down to get on and nose up to stop. On its jets from where it stands
   up to the height it flies at, under a roof, and down to the ground;
   on its wings between. */
static void air_plane(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct bot_flight_result *result = f->result;
	real_point3d const *position = &body->position;
	real climbing = body->velocity.k * BOT_TICKS_PER_SECOND;
	real level = (real)sqrt(body->velocity.i * body->velocity.i + body->velocity.j * body->velocity.j) * BOT_TICKS_PER_SECOND;
	boolean landing = pilot->phase == _pilot_land;
	if (landing || (pilot->indoors && !pilot->wings_out))
		pilot->hovering = TRUE;
	else if (pilot->indoors && pilot->wings_out && position->z > pilot->rest_z + 1.5f)
		pilot->hovering = FALSE;
	/* (fallen out of the sky, somehow, slow and under its height: jets) */
	else if (!pilot->hovering && position->z < f->least - 4.f && climbing <= 0.f && f->speed < f->top_speed * 0.4f)
		pilot->hovering = TRUE;
	if (!pilot->hovering)
		f->throttle = 1.f;
	else
	{
		struct air_hover h;
		air_hover_target(f, level, landing, &h);
		if (pilot->hovering)
			air_hover_fly(f, &h, landing, climbing);
	}
	result->pitch = PIN(f->pitch, -1.2f, 1.2f);
	result->yaw = f->yaw;
	result->throttle = f->throttle;
}

/* Held up (against a cliff, on its back, wedged): after a while of it, out. */
static void air_held_up(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_flight_result *result = f->result;
	real_point3d const *position = &f->body->position;
	long now = f->now;

	if (distance3d(position, &pilot->stuck_point) > (f->plane && pilot->hovering ? 2.5f : 4.f) || pilot->phase == _pilot_land ||
		(f->throttle < 0.3f && !f->plane))
	{
		/* (... or standing in the air because it means to) */
		pilot->stuck_point = *position;
		pilot->stuck_tick = now;
	}
	else if (f->plane)
	{
		/* (a plane rises on its jets from where it stands, and is a while
		   about it: longer for it) */
		if (now - pilot->stuck_tick > BOT_SECONDS(9.f))
		{
			result->get_out = TRUE;
			result->stuck = TRUE;
		}
	}
	else if (now - pilot->stuck_tick > BOT_SECONDS(3.f))
	{
		/* (first back and up, a second of it; then out) */
		f->throttle = -1.f;
		f->pitch = 0.5f;
		if (now - pilot->stuck_tick > BOT_SECONDS(6.f))
		{
			result->get_out = TRUE;
			result->stuck = TRUE;
		}
	}
}

/* (testing: something flown into: its speed gone from one tick to the next) */
/* (... or knocked off its way: what it is doing changed by more in a tick
   than its engines change it, 4 units a second) */
static void air_log_bump(struct air_flight const *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct object_datum *craft = f->craft;
	real_point3d const *position = &body->position;
	real knock = (real)sqrt((body->velocity.i - pilot->velocity_before.i) * (body->velocity.i - pilot->velocity_before.i) +
		(body->velocity.j - pilot->velocity_before.j) * (body->velocity.j - pilot->velocity_before.j) +
		(body->velocity.k - pilot->velocity_before.k) * (body->velocity.k - pilot->velocity_before.k)) * BOT_TICKS_PER_SECOND;
	if (bot_logging() && pilot->speed_before > 2.f && f->now - pilot->taken_tick > BOT_SECONDS(1.f) &&
		((pilot->speed_before > 3.f && f->speed < pilot->speed_before * 0.55f) || knock > 4.f))
		bot_log("pilot %ld t%ld bump at %.1f %.1f %.1f (%.1f over the ground) speed %.1f from %.1f, knocked %.1f, phase %ld%s, %s (radius %.1f)", body->unit_index & 0xffff, f->now,
			position->x, position->y, position->z, f->over_ground, f->speed, pilot->speed_before, knock, pilot->phase,
			pilot->indoors ? " indoors" : pilot->hovering ? " on its jets" : "", tag_get_name(craft->definition_index), craft->object.bounding_sphere_radius);
}

/* Which phase it is in: not a fight or a landing, on the way. A bomb on its
   way down has the pilot on, the way it is going and no lower, until that
   has gone off, whatever it is told to do next. */
static void air_choose_phase(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_flight_order const *order = f->order;
	real_point3d const *position = &f->body->position;
	long now = f->now;
	if (pilot->bomb_pass && !(pilot->phase == _pilot_break && now - pilot->phase_tick <= BOT_SECONDS(AIR_BOMB_LEAVING) && now >= pilot->phase_tick))
		pilot->bomb_pass = FALSE;
	if (pilot->bomb_pass)
	{
		/* (looked at again each second: what is clear from here now) */
		if ((now - pilot->phase_tick) % BOT_TICKS_PER_SECOND == 0 && now > pilot->phase_tick)
			pilot->break_yaw = air_way_on(position, pilot->break_yaw, MAX(f->speed, 10.f) * 6.f, f->need, f->clearance);
	}
	else if (order->attack && order->armed && !f->under_roof)
		air_attack(f);
	else if (order->land && (f->to_place < (f->plane ? MAX(30.f, f->speed * 2.5f + 20.f) : MAX(14.f, f->speed * 1.5f)) ||
		(f->plane && pilot->phase == _pilot_land && (f->to_place < 90.f || pilot->way_in >= 2))) &&
		!(f->plane && pilot->phase != _pilot_land && position->z < order->destination.z - 10.f))
	{
		/* (a plane that has begun to come down goes on with it: it is on
		   its jets, and slow, and no nearer for having slowed) */
		pilot->phase = _pilot_land;
	}
	else
		pilot->phase = _pilot_cruise;
}

/* Nobody to shoot at where it is bound (their base, with all of them
   indoors; somewhere to look): round and round over the place, a little way
   off it, at full speed: not stopped over it. And a plane that has come to
   where it is to go down a long way under it, a hangar's mouth high in a
   cliff: round and up on its wings, which climb ten times as fast as its
   jets. */
static void air_patrol(struct air_flight *f)
{
	struct bot_flight_order const *order = f->order;
	real_point3d const *position = &f->body->position;
	if ((order->patrol || (f->plane && order->land && position->z < order->destination.z - 10.f)) && f->pilot->phase == _pilot_cruise)
	{
		real round = 16.f + 60.f * f->quick;
		if (f->to_place < round + 12.f)
		{
			real about = (real)atan2(position->y - order->destination.y, position->x - order->destination.x) + 0.6f;
			f->steer_to.x = order->destination.x + (real)cos(about) * round;
			f->steer_to.y = order->destination.y + (real)sin(about) * round;
			f->steer_to.z = order->destination.z;
		}
	}
}

/* The look and the throttle by the phase it is in (not under a roof: that
   has been seen to). */
static void air_steer_by_phase(struct air_flight *f)
{
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct bot_flight_order const *order = f->order;
	struct bot_flight_result *result = f->result;
	real_point3d const *position = &body->position;
	long now = f->now;
	switch (pilot->indoors ? NONE : pilot->phase)
	{
	case NONE:
		break;
	case _pilot_run:
		air_run(f);
		break;
	case _pilot_break:
		/* off: up out of it for a moment, then away at the height the
		   ground lets it have, for room to come round */
		/* (not in a straight line, for whoever is shooting at it: a little
		   to one hand and the other as it goes) */
		f->yaw = bot_wrap(pilot->break_yaw + 0.3f * (real)sin((now + (body->unit_index & 31) * 7) * 0.14f));
		if (now - pilot->phase_tick < BOT_SECONDS(0.6f) || f->over_ground < f->need * 0.6f)
			f->pitch = MAX(f->pitch, 0.35f);
		else
			f->pitch += 0.12f * (real)sin((now + (body->unit_index & 31) * 5) * 0.21f);
		f->throttle = 1.f;
		break;
	case _pilot_land:
	{
		/* down by the place: slowly over it, nose a little down, and out
		   when the ground is a step below */
		real ground_z;
		real_point3d under = *position;
		boolean low = bot_ground(&under, 3.f, body->vehicle_index, &ground_z);
		if (f->plane)
			break; /* (its own way down: air_plane) */
		f->yaw = f->to_place > 3.f ? bot_yaw_to(position, &order->destination) : body->facing_yaw;
		f->throttle = f->to_place > 6.f ? 0.35f : f->speed > 2.f ? -0.3f : 0.1f;
		f->pitch = f->to_place > 10.f ? MIN(f->pitch, -0.1f) : -0.35f;
		if (order->stay)
			result->landed = low && position->z - ground_z < 2.2f && f->speed < 2.f;
		else if ((low && position->z - ground_z < 2.2f && f->speed < 6.f) || now - pilot->land_tick > BOT_SECONDS(7.f))
			result->get_out = TRUE;
		break;
	}
	default:
		/* on the way: slower into a turn, and for the end of it when the
		   place is where it stops */
		/* (a slow craft; a jet keeps its speed, and goes round wide) */
		if (fabs(bot_angle_difference(f->yaw, body->facing_yaw)) > 1.1f && f->quick <= 0.5f)
			f->throttle = 0.55f;
		if (!order->attack && !order->patrol && f->to_place < MAX(20.f, f->speed * 2.f))
			f->throttle = MIN(f->throttle, MAX(0.25f, f->to_place / MAX(20.f, f->speed * 2.f)));
		if (f->to_place < MAX(6.f, f->speed * 0.4f))
			result->arrived = TRUE;
		break;
	}
	if (pilot->phase != _pilot_land)
	{
		pilot->land_tick = now;
		pilot->down = FALSE;
	}
	if (pilot->phase != _pilot_run)
		pilot->over_tick = 0;
}

/* (testing: what the pilot did this tick, when its phase changes and now and
   then) */
static void air_log_flight(struct air_flight const *f)
{
	static char const *const names[] = { "cruise", "run", "break", "land" };
	struct bot_pilot *pilot = f->pilot;
	struct bot_body const *body = f->body;
	struct bot_flight_order const *order = f->order;
	real_point3d const *position = &body->position;
	long now = f->now;
	if (bot_logging() && (pilot->phase != f->phase_before || now % 60 == 0 || (pilot->phase == _pilot_run && now % 10 == 0)))
	{
		bot_log("pilot %ld t%ld %s at %.0f %.0f %.0f (%.0f over the ground) speed %.0f to %.0f %.0f %.0f (%.0f off, turn %ld of %ld) height %.0f yaw %.2f pitch %.2f throttle %.2f%s%s",
			body->unit_index & 0xffff, now, names[pilot->phase], position->x, position->y, position->z, f->over_ground, f->speed,
			order->destination.x, order->destination.y, order->destination.z, f->to_place, pilot->route_next + 1, pilot->route_count,
			f->height, f->yaw, f->pitch, f->throttle, f->result->fire ? " firing" : "", f->why);
		if (pilot->phase != f->phase_before)
			bot_log("pilot %ld t%ld %s -> %s: they stand at %.2f, %.0f off", body->unit_index & 0xffff, now, names[f->phase_before], names[pilot->phase], order->vitality,
				distance3d(position, &order->destination));
	}
}

/* One tick of flying. The order says where to and what for; the result is
   where to look (a craft goes where its pilot looks), how much throttle,
   and whether to fire, to get out, or (a plane, hovering) to rise. */
void bot_fly(struct bot_pilot *pilot, struct bot_body const *body, struct bot_flight_order const *order, struct bot_flight_result *result)
{
	struct air_flight f;
	struct bot_flight_order inbound;
	real_point3d const *position = &body->position;

	air_flight_begin(&f, pilot, body, order, result);
	memset(result, 0, sizeof(*result));
	if (air_way_in_order(pilot, body, order, f.plane, f.now, &inbound))
	{
		f.order = &inbound;
		f.to_place = bot_distance2d(position, &inbound.destination);
	}

	if (pilot->vehicle != body->vehicle_index)
		air_pilot_take(pilot, body, f.craft, f.top_speed, f.now);

	air_route_step(pilot, position, f.order, f.need, f.speed, f.to_place, f.now, &f.steer_to);
	air_choose_phase(&f);
	air_patrol(&f);
	air_height(&f);
	air_look_for_height(&f);
	air_leave_roof(&f);

	if (pilot->indoors && bot_logging() && air_trace() && f.now % 15 == 0)
		bot_log("indoors %ld t%ld at %.1f %.1f %.1f facing %.2f speed %.1f: to %.1f %.1f %.1f (%s %ld, open %.0f) yaw %.2f pitch %.2f throttle %.2f",
			body->unit_index & 0xffff, f.now, position->x, position->y, position->z, body->facing_yaw, f.speed, pilot->exit_point.x, pilot->exit_point.y,
			pilot->exit_point.z, pilot->gate != NONE ? "gate" : "way out", pilot->gate, pilot->exit_open, f.yaw, f.pitch, f.throttle);
	air_steer_by_phase(&f);
	air_bomb_pass(&f);
	air_in_the_way(&f);
	air_held_up(&f);

	result->yaw = f.yaw;
	result->pitch = PIN(f.pitch, -1.2f, 1.2f);
	result->throttle = f.throttle;
	if (f.plane)
		air_plane(&f);
	air_log_bump(&f);
	pilot->velocity_before = body->velocity;
	pilot->speed_before = f.speed;
	air_log_flight(&f);
}
