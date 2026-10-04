/*
BOT_AAS.C

The area awareness system, after Quake III's (be_aas_*): the bots' map of
where they can go and how to get there.

AREAS
The heightfield's walkable floor (bot_nav.c), kept a body's radius off
every wall, is cut into rectangles: grown from the roomiest spots out,
a column or a row at a time, for as long as every cell of the new column
(row) is roomy, not in an area yet, and joined to its neighbours at about
the same height. A rectangle of such cells is convex: from anywhere in it
to anywhere else in it is a straight walk that keeps off the walls. That
is what makes following a route simple and safe.

Vehicles get their own areas from the same heightfield: kept further off
walls (a vehicle's half width), with room for a vehicle over them, and
joined only where a vehicle rolls across (slopes, not steps).

REACHABILITIES
Every way from one area into another, typed by how it's travelled, as in
Quake III:
	walk      across the edge two areas share (the portal: both its ends
	          are kept, so a route can be pulled straight through a string
	          of them)
	jump      up a ledge too high to step up (a barrier jump)
	drop      off a ledge (walk off it)
	gap       a running jump across a gap
	ladder    walk into a climbable face, up it, and off at the top
	teleport  step on a teleporter's pad
Each has a start (where to be in the area it leaves), where the move itself
happens (the ledge, the gap's edge, the ladder's foot, the pad), an end (in
the area it reaches) and a travel time.

ROUTING
The travel time from every reachability to a goal area is worked out in
one go (Dijkstra backwards from the goal over the reachabilities: from the
end of one to the start of the next is a straight walk across an area) and
kept in a cache of the most recently wanted goals. A bot in an area takes
the reachability out of it with the least walk to its start plus time from
there. Reachabilities that bots keep failing at cost more (learned).
*/

#include "bot_port.h"
#include "bot_nav_internal.h"

#include "objects/objects.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* hundredths: a Spartan's 0.2 radius and a little. A floor's distance from
   a wall is to the middle of its column, the wall somewhere in the next
   column out: on the coarsest grid (0.2) that's the second column from
   the wall (0.3), on the finest (0.1) the third (0.25) */
#define AAS_FOOT_CLEARANCE 22
#define AAS_VEHICLE_CLEARANCE 60 /* a warthog's half width and a little */
#define AAS_MAXIMUM_SIDE NAVM_CELLS(8.f) /* cells: areas no bigger than 8x8 units */
#define AAS_FOOT_DROP 4.5f /* further down hurts */
#define AAS_VEHICLE_DROP 1.6f
#define AAS_FOOT_SPEED 2.25f
#define AAS_VEHICLE_SPEED 6.f
#define AAS_CACHES 96
#define AAS_CACHE_VERSION 28
#define AAS_DEAD_FAILURES 4 /* failed this often: not a way */

/* what a way that has failed costs, in seconds over its time: a jump or a
   gap has no cost of its own for being one (it is as good as the floor
   beside it), so the first failure counts at once (a little: a slip), and
   each one after doubles it, until it is not a way at all */
static float aas_failure_penalty(long failures)
{
	return failures < 1 ? 0.f : failures >= AAS_DEAD_FAILURES ? 1000.f : (float)(1 << MIN(7, failures));
}
#define AAS_CACHE_MAGIC 0x53414148UL /* HAAS */
#define AAS_MAXIMUM_PLACES 256

/* a Spartan's jump: up at 2.1 units a second, 3.2 units a second a second
   down again, running at 2.25 */
#define AAS_GAP_RISE 0.5f /* a running jump lands this much higher at most (it tops out at 0.69) */
#define AAS_JUMP_VELOCITY 2.1f
#define AAS_GRAVITY 3.21f

struct aas_cache
{
	long goal;
	long slot; /* whose area costs (NONE: none) */
	unsigned long epoch; /* of those costs */
	unsigned long penalty_epoch; /* of the learned penalties */
	unsigned long used;
	float *time; /* per reachability: seconds from its start to the goal */
	/* the search is run only as far as it is asked about (aas_route_advance):
	   a time no more than frontier is final, the others are the best found
	   so far, or not reached yet (1.0e30); complete once all are final. Until
	   then it holds the penalties and the area costs it started with, which
	   it carries on with whatever has changed since. */
	boolean complete;
	float frontier;
	float *penalty;
	float *cost;
	/* the reachabilities reached and not final, as the search left them:
	   what it carries on from (not every reachability looked through for
	   them each time, which on a big map was most of a short search) */
	long *open;
	long open_count;
	long open_capacity;
};

struct aas_world
{
	long area_count;
	long reach_count;
	struct aas_area *areas;
	struct aas_reach *reaches;
	long *incoming; /* reachability indices, grouped by the area they go to */
	long *span_area; /* per heightfield span */
	long *attach_span; /* per span: the nearest span that's in an area, over the floor (aas_find_attachments) */
	unsigned char *attach_depth; /* ...and how many steps off (AAS_ATTACH_NONE: further than AAS_ATTACH_STEPS) */
	boolean attach_reachable; /* ...of the areas there's a way to only (once that's known: aas_find_flags) */
	float *penalty; /* per reachability: learned extra seconds (shared: aas_shared_new) */
	unsigned char *failures;
	float *fall; /* per reachability: a gap's fall for a jump that misses (aas_reach_fall) */
	/* for the routes' search (aas_finish_world): each reachability's area and
	   time again, by the place it has among the ones into its area; and for
	   every area, the seconds from where each way in comes out to where
	   each way out is taken */
	long *reach_from;
	float *incoming_time;
	long *pair_base;
	float *pair_time;
	float *random_weight; /* cumulative, over the areas, for aas_random_area */
	long *region; /* per area: the part of the map it is joined to, some way or other (aas_find_regions) */
	long *region_size; /* per region: how many areas */
	long *region_first; /* per region: where its areas begin in region_areas */
	long *region_areas; /* the areas, a region at a time */
	long region_count;
	struct aas_cache caches[AAS_CACHES];
	unsigned long cache_clock;
	float *area_cost[AAS_COST_SLOTS]; /* extra seconds to go through an area, per side (danger; shared) */
	unsigned long cost_epoch[AAS_COST_SLOTS];
	unsigned long penalty_epoch; /* raised when a way is found wanting (aas_reach_failed) */
};

/* routes worked out afresh this tick, of all the bots': one out of date
   (the danger moved, a way failed) is still a good way, and is used as it
   is while this tick's budget is spent, so that a whole team doesn't redo
   its routes in the same tick (a hitch) */
#define AAS_ROUTES_PER_TICK 1
static long aas_budget_tick = -1;
static long aas_budget_used;
/* ... and routes begun to somewhere there was none to, of all the bots', by
   those that only want to know how far a thing is to weigh it (the goals'
   candidates: aas_travel_time_soon). One with no route yet has nothing out
   of date to go on with, so it is told "not yet" (never "no way") and
   weighs the thing by how far it is as the crow flies until a later tick
   has room for its route. What walks a route always gets one at once. */
#define AAS_NEW_ROUTES_PER_TICK 3
static long aas_new_used;
static boolean aas_route_may_wait; /* (the query under way is one of those) */
static boolean aas_route_waits; /* (... and was told not yet) */
long game_time_get(void);

/* the routing's heap, kept between routes (it's big: a fresh one each time
   cost more than the routing on some systems) */
static float *aas_check_time; /* testing: HALO_BOT_ROUTE_CHECK */
static long aas_check_capacity;
static long *aas_heap_items;
static float *aas_heap_keys;
static long aas_heap_capacity;

static struct aas_world aas_worlds[NUMBER_OF_AAS_TYPES];
static boolean aas_is_ready;
static real_point3d aas_places[AAS_MAXIMUM_PLACES];
static long aas_places_count;
static long aas_spawn_count; /* the first so many of the places: where players start */

/* scratch for searches over the heightfield */
static unsigned long *aas_stamp;
static unsigned long aas_stamp_value;
static long *aas_queue;
static long *aas_depth;

/* ---------- small things */

/* ---------- values several routes share

A route that has not searched the whole map yet goes on with the penalties
and the area costs it began with (aas_route_advance). So those are counted
references: a change while a route still holds the old ones is made in a
copy (aas_shared_writable), and the old go when the last route is done with
them. */

static float *aas_shared_new(long count, boolean zero)
{
	size_t size = 2 * sizeof(long) + (size_t)MAX(1, count) * sizeof(float);
	long *block = (long *)(zero ? calloc(1, size) : malloc(size));
	if (!block)
		return NULL;
	block[0] = 1; /* references */
	block[1] = count;
	return (float *)(block + 2);
}

static float *aas_shared_retain(float *values)
{
	if (values)
		((long *)values - 2)[0]++;
	return values;
}

static void aas_shared_release(float *values)
{
	if (values && --((long *)values - 2)[0] <= 0)
		free((long *)values - 2);
}

/* the values to change: these, or a copy when a route holds them too (the
   world's reference moves to the copy); NULL when there's no memory */
static float *aas_shared_writable(float **values)
{
	long *block = (long *)*values - 2;
	float *copy;
	if (block[0] <= 1)
		return *values;
	copy = aas_shared_new(block[1], FALSE);
	if (!copy)
		return NULL;
	memcpy(copy, *values, (size_t)MAX(1, block[1]) * sizeof(float));
	block[0]--;
	*values = copy;
	return copy;
}

static real aas_distance(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x;
	real dy = a->y - b->y;
	real dz = a->z - b->z;
	return (real)sqrt(dx * dx + dy * dy + dz * dz);
}

static real aas_distance2d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x;
	real dy = a->y - b->y;
	return (real)sqrt(dx * dx + dy * dy);
}

real aas_speed(long type)
{
	return type == AAS_VEHICLE ? AAS_VEHICLE_SPEED : AAS_FOOT_SPEED;
}

static boolean aas_eligible(long type, long s)
{
	struct navm_span const *span;
	if (s == NONE)
		return FALSE;
	span = &navm.spans[s];
	if (!(span->flags & _navm_span_walkable) || (span->flags & _navm_span_blocked))
		return FALSE;
	return type == AAS_VEHICLE ? span->vehicle_wall >= AAS_VEHICLE_CLEARANCE : span->wall >= AAS_FOOT_CLEARANCE;
}

static long aas_neighbor(long type, long s, long direction)
{
	return type == AAS_VEHICLE ? navm_vehicle_neighbor(s, direction) : navm_walk_neighbor(s, direction);
}

static long aas_cell_x(long s)
{
	return navm.spans[s].column % navm.width;
}

static long aas_cell_y(long s)
{
	return navm.spans[s].column / navm.width;
}

static void aas_new_search(void)
{
	if (++aas_stamp_value == 0)
	{
		memset(aas_stamp, 0, navm.span_count * sizeof(unsigned long));
		aas_stamp_value = 1;
	}
}

/* the area a span is in, or the nearest within steps (over the floor):
   where someone at a ledge, a ladder or a pad (all by walls, so not in an
   area themselves) gets on and off the areas */
#define AAS_ATTACH_STEPS 40
#define AAS_ATTACH_NONE 255

/* The nearest area to a point (aas_nearest_area) is asked for the same
   points over and over: every bot weighing what to go for asks it of every
   weapon and powerup lying about, each time, and for something not right on
   a floor's areas the answer is a search of the floors around. It depends
   on the point and on what the areas are, nothing else: the last answers
   are kept, by the point, until the areas (or which of them count) change. */
#define AAS_NEAREST_MEMOS 2048 /* (a power of two) */

struct aas_nearest_memo
{
	unsigned long generation; /* 0: empty */
	long type;
	real_point3d point;
	real radius;
	long area;
	long found; /* the span it was found by */
};

static struct aas_nearest_memo aas_nearest_memos[AAS_NEAREST_MEMOS];
/* (testing: every kept answer against the search) */
static boolean aas_route_check(void);
static unsigned long aas_nearest_generation = 1;

static void aas_nearest_forget(void)
{
	if (!++aas_nearest_generation)
	{
		memset(aas_nearest_memos, 0, sizeof(aas_nearest_memos));
		aas_nearest_generation = 1;
	}
}

static struct aas_nearest_memo *aas_nearest_memo_for(long type, real_point3d const *point, real radius)
{
	unsigned long bits[4];
	unsigned long hash;

	memcpy(bits, point, 3 * sizeof(float));
	memcpy(&bits[3], &radius, sizeof(float));
	hash = (bits[0] * 0x9e3779b1UL) ^ (bits[1] * 0x85ebca6bUL) ^ (bits[2] * 0xc2b2ae35UL) ^ (bits[3] * 0x27d4eb2fUL) ^ (unsigned long)type;
	hash ^= hash >> 15;
	return &aas_nearest_memos[hash & (AAS_NEAREST_MEMOS - 1)];
}

/* ...worked out for every span at once, as far as AAS_ATTACH_STEPS (out
   from the areas, a step at a time): asked for all the time, of a great
   many spans */
/* A build that ran out of memory. What it has by then is not all of the
   map's areas or the ways between them. It is not used and not kept
   (aas_build). */
static boolean aas_build_failed;

static void aas_no_memory(char const *what)
{
	if (!aas_build_failed)
		bot_log("aas: no memory for %s: this map has no navigation", what);
	aas_build_failed = TRUE;
}

static void aas_find_attachments(long type, boolean reachable_only)
{
	struct aas_world *world = &aas_worlds[type];
	long head = 0, tail = 0;
	long s;
	aas_nearest_forget();
	free(world->attach_span);
	free(world->attach_depth);
	world->attach_span = (long *)malloc(MAX(1, navm.span_count) * sizeof(long));
	world->attach_depth = (unsigned char *)malloc(MAX(1, navm.span_count));
	if (!world->attach_span || !world->attach_depth || !world->span_area)
	{
		aas_no_memory("the floors' nearest areas");
		free(world->attach_span);
		free(world->attach_depth);
		world->attach_span = NULL;
		world->attach_depth = NULL;
		world->attach_reachable = FALSE;
		return;
	}
	/* (the areas there's a way to, when that's known: somewhere near a
	   sliver of floor on top of a wall, which is an area nobody gets to, is
	   near the floor at the wall's foot, not at the sliver) */
	world->attach_reachable = reachable_only;
	for (s = 0; s < navm.span_count; s++)
	{
		world->attach_span[s] = NONE;
		world->attach_depth[s] = AAS_ATTACH_NONE;
		if (world->span_area[s] != NONE && (!reachable_only || (world->areas[world->span_area[s]].flags & _aas_area_reachable)))
		{
			world->attach_span[s] = s;
			world->attach_depth[s] = 0;
			aas_queue[tail++] = s;
		}
	}
	while (head < tail)
	{
		long u = aas_queue[head++];
		long d;
		if (world->attach_depth[u] >= AAS_ATTACH_STEPS)
			continue;
		for (d = 0; d < 8; d++)
		{
			long t = navm_walk_neighbor(u, d);
			if (t == NONE || world->attach_depth[t] != AAS_ATTACH_NONE)
				continue;
			world->attach_span[t] = world->attach_span[u];
			world->attach_depth[t] = (unsigned char)(world->attach_depth[u] + 1);
			aas_queue[tail++] = t;
		}
	}
}

static long aas_attach(long type, long s, long steps, long *found)
{
	struct aas_world *world = &aas_worlds[type];
	long head = 0, tail = 0;
	if (s == NONE)
		return NONE;
	if (world->span_area[s] != NONE && (!world->attach_reachable || (world->areas[world->span_area[s]].flags & _aas_area_reachable)))
	{
		if (found)
			*found = s;
		return world->span_area[s];
	}
	if (world->attach_depth)
	{
		if (world->attach_depth[s] != AAS_ATTACH_NONE && world->attach_depth[s] <= steps)
		{
			if (found)
				*found = world->attach_span[s];
			return world->span_area[world->attach_span[s]];
		}
		/* (in an area there's no way to, and none there is near: that one) */
		if (world->span_area[s] != NONE)
		{
			if (found)
				*found = s;
			return world->span_area[s];
		}
		if (world->attach_depth[s] != AAS_ATTACH_NONE || steps <= AAS_ATTACH_STEPS)
			return NONE;
	}
	aas_new_search();
	aas_stamp[s] = aas_stamp_value;
	aas_queue[tail] = s;
	aas_depth[tail++] = 0;
	while (head < tail)
	{
		long u = aas_queue[head];
		long depth = aas_depth[head++];
		long d;
		if (world->span_area[u] != NONE)
		{
			if (found)
				*found = u;
			return world->span_area[u];
		}
		if (depth >= steps)
			continue;
		for (d = 0; d < 8; d++)
		{
			/* (on foot's terms either way: a vehicle gets to its areas over
			   ground a body could cross) */
			long t = navm_walk_neighbor(u, d);
			if (t == NONE || aas_stamp[t] == aas_stamp_value || tail >= navm.span_count)
				continue;
			aas_stamp[t] = aas_stamp_value;
			aas_queue[tail] = t;
			aas_depth[tail++] = depth + 1;
		}
	}
	return NONE;
}

/* ---------- areas */

#define AAS_BUCKETS 600
#define AAS_GRID 193 /* the rectangle being grown, around its seed at the middle (twice the most cells a side, and some) */

static void aas_build_areas(long type)
{
	struct aas_world *world = &aas_worlds[type];
	long *grid = (long *)malloc(AAS_GRID * AAS_GRID * sizeof(long));
	long *order = (long *)malloc(MAX(1, navm.span_count) * sizeof(long));
	long *bucket = (long *)calloc(AAS_BUCKETS + 2, sizeof(long));
	long capacity = 4096;
	long eligible = 0;
	long s;
	long i;

	world->span_area = (long *)malloc(MAX(1, navm.span_count) * sizeof(long));
	world->areas = (struct aas_area *)malloc(capacity * sizeof(struct aas_area));
	world->area_count = 0;
	if (!grid || !order || !bucket || !world->span_area || !world->areas)
	{
		aas_no_memory("the areas");
		goto done;
	}
	for (s = 0; s < navm.span_count; s++)
		world->span_area[s] = NONE;

	/* the roomiest first (a counting sort, most room first) */
	for (s = 0; s < navm.span_count; s++)
	{
		if (aas_eligible(type, s))
		{
			long key = type == AAS_VEHICLE ? navm.spans[s].vehicle_wall : navm.spans[s].wall;
			bucket[AAS_BUCKETS - MIN(key, AAS_BUCKETS) + 1]++;
			eligible++;
		}
	}
	for (i = 1; i < AAS_BUCKETS + 2; i++)
		bucket[i] += bucket[i - 1];
	for (s = 0; s < navm.span_count; s++)
	{
		if (aas_eligible(type, s))
		{
			long key = type == AAS_VEHICLE ? navm.spans[s].vehicle_wall : navm.spans[s].wall;
			order[bucket[AAS_BUCKETS - MIN(key, AAS_BUCKETS)]++] = s;
		}
	}

	for (i = 0; i < eligible; i++)
	{
		long seed = order[i];
		long lx = AAS_GRID / 2, hx = AAS_GRID / 2, ly = AAS_GRID / 2, hy = AAS_GRID / 2;
		boolean stopped[4] = { FALSE, FALSE, FALSE, FALSE };
		long fresh[AAS_GRID];
		struct aas_area *area;
		long x, y;

		if (world->span_area[seed] != NONE)
			continue;
		grid[ly * AAS_GRID + lx] = seed;
		while (!stopped[0] || !stopped[1] || !stopped[2] || !stopped[3])
		{
			long direction;
			for (direction = 0; direction < 4; direction++)
			{
				boolean ok = TRUE;
				long count;
				long k;
				if (stopped[direction])
					continue;
				if (direction < 2)
				{
					/* a column: +x (0) or -x (1) */
					long from_x = direction == 0 ? hx : lx;
					if ((direction == 0 ? hx + 1 >= AAS_GRID : lx - 1 < 0) || hx - lx + 1 >= AAS_MAXIMUM_SIDE)
						ok = FALSE;
					count = hy - ly + 1;
					for (k = 0; ok && k < count; k++)
					{
						long t = aas_neighbor(type, grid[(ly + k) * AAS_GRID + from_x], direction);
						if (!aas_eligible(type, t) || world->span_area[t] != NONE)
							ok = FALSE;
						else if (k > 0 && aas_neighbor(type, fresh[k - 1], 2) != t)
							ok = FALSE; /* not joined to the one below it in the new column */
						else
							fresh[k] = t;
					}
					if (ok)
					{
						long to_x = direction == 0 ? ++hx : --lx;
						for (k = 0; k < count; k++)
							grid[(ly + k) * AAS_GRID + to_x] = fresh[k];
					}
				}
				else
				{
					/* a row: +y (2) or -y (3) */
					long from_y = direction == 2 ? hy : ly;
					if ((direction == 2 ? hy + 1 >= AAS_GRID : ly - 1 < 0) || hy - ly + 1 >= AAS_MAXIMUM_SIDE)
						ok = FALSE;
					count = hx - lx + 1;
					for (k = 0; ok && k < count; k++)
					{
						long t = aas_neighbor(type, grid[from_y * AAS_GRID + lx + k], direction);
						if (!aas_eligible(type, t) || world->span_area[t] != NONE)
							ok = FALSE;
						else if (k > 0 && aas_neighbor(type, fresh[k - 1], 0) != t)
							ok = FALSE;
						else
							fresh[k] = t;
					}
					if (ok)
					{
						long to_y = direction == 2 ? ++hy : --ly;
						for (k = 0; k < count; k++)
							grid[to_y * AAS_GRID + lx + k] = fresh[k];
					}
				}
				if (!ok)
					stopped[direction] = TRUE;
			}
		}
		if (world->area_count >= capacity)
		{
			struct aas_area *grown;
			capacity *= 2;
			grown = (struct aas_area *)realloc(world->areas, capacity * sizeof(struct aas_area));
			if (!grown)
			{
				aas_no_memory("the areas");
				break;
			}
			world->areas = grown;
		}
		area = &world->areas[world->area_count];
		memset(area, 0, sizeof(*area));
		area->z0 = 1.0e9f;
		area->z1 = -1.0e9f;
		for (y = ly; y <= hy; y++)
		{
			for (x = lx; x <= hx; x++)
			{
				long u = grid[y * AAS_GRID + x];
				world->span_area[u] = world->area_count;
				area->z0 = MIN(area->z0, navm.spans[u].floor);
				area->z1 = MAX(area->z1, navm.spans[u].floor);
				area->clearance = MAX(area->clearance, (type == AAS_VEHICLE ? navm.spans[u].vehicle_wall : navm.spans[u].wall) / 100.f);
			}
		}
		{
			long corner = grid[ly * AAS_GRID + lx];
			area->x0 = navm.x0 + aas_cell_x(corner) * NAVM_CELL;
			area->y0 = navm.y0 + aas_cell_y(corner) * NAVM_CELL;
			area->x1 = area->x0 + (hx - lx + 1) * NAVM_CELL;
			area->y1 = area->y0 + (hy - ly + 1) * NAVM_CELL;
			navm_span_point(grid[((ly + hy) / 2) * AAS_GRID + (lx + hx) / 2], &area->center);
		}
		world->area_count++;
	}
done:
	free(grid);
	free(order);
	free(bucket);
}

/* ---------- reachabilities */

struct aas_candidate
{
	long from, to;
	long travel;
	long key0, key1, key2; /* walk: the edge's direction, line and place along it */
	float cost;
	long start_span, edge_span, end_span; /* in from's area, the move, in to's area */
	long low_span, high_span; /* walk: the two spans either side of the edge */
	long careful; /* drop: a fall beyond where it lands */
	long walkover; /* floor a step up or down just over a slot: walked */
};

static int aas_candidate_compare(void const *a, void const *b)
{
	struct aas_candidate const *ca = (struct aas_candidate const *)a;
	struct aas_candidate const *cb = (struct aas_candidate const *)b;
	if (ca->from != cb->from)
		return ca->from < cb->from ? -1 : 1;
	if (ca->to != cb->to)
		return ca->to < cb->to ? -1 : 1;
	if (ca->travel != cb->travel)
		return ca->travel < cb->travel ? -1 : 1;
	if (ca->key0 != cb->key0)
		return ca->key0 < cb->key0 ? -1 : 1;
	if (ca->key1 != cb->key1)
		return ca->key1 < cb->key1 ? -1 : 1;
	if (ca->key2 != cb->key2)
		return ca->key2 < cb->key2 ? -1 : 1;
	return ca->cost < cb->cost ? -1 : ca->cost > cb->cost ? 1 : 0;
}

struct aas_candidates
{
	struct aas_candidate *items;
	long count;
	long capacity;
};

static struct aas_candidate *aas_candidate_new(struct aas_candidates *list)
{
	if (list->count >= list->capacity)
	{
		long capacity = list->capacity ? list->capacity * 2 : 65536;
		struct aas_candidate *grown = (struct aas_candidate *)realloc(list->items, capacity * sizeof(struct aas_candidate));
		if (!grown)
		{
			aas_no_memory("the ways between areas");
			return NULL;
		}
		list->items = grown;
		list->capacity = capacity;
	}
	memset(&list->items[list->count], 0, sizeof(struct aas_candidate));
	return &list->items[list->count++];
}

struct aas_reaches
{
	struct aas_reach *items;
	long count;
	long capacity;
};

static struct aas_reach *aas_reach_new(struct aas_reaches *list, long from, long to, long travel)
{
	struct aas_reach *reach;
	if (list->count >= list->capacity)
	{
		long capacity = list->capacity ? list->capacity * 2 : 65536;
		struct aas_reach *grown = (struct aas_reach *)realloc(list->items, capacity * sizeof(struct aas_reach));
		if (!grown)
		{
			aas_no_memory("the ways between areas");
			return NULL;
		}
		list->items = grown;
		list->capacity = capacity;
	}
	/* (testing, HALO_BOT_TEST_NOMEMORY=aas: as if there were no more) */
	if (list->count == 1000 && getenv("HALO_BOT_TEST_NOMEMORY") && !strcmp(getenv("HALO_BOT_TEST_NOMEMORY"), "aas"))
	{
		aas_no_memory("the ways between areas (a test)");
		return NULL;
	}
	reach = &list->items[list->count++];
	memset(reach, 0, sizeof(*reach));
	reach->from = from;
	reach->to = to;
	reach->travel = (short)travel;
	return reach;
}

/* walking from one area into the next: the edges they share, a run of
   cells long each */
static void aas_walk_reaches(long type, struct aas_reaches *reaches)
{
	struct aas_world *world = &aas_worlds[type];
	struct aas_candidates list;
	long s;
	long i;

	memset(&list, 0, sizeof(list));
	for (s = 0; s < navm.span_count; s++)
	{
		long a = world->span_area[s];
		long d;
		if (a == NONE)
			continue;
		for (d = 0; d < 4; d++)
		{
			long t = aas_neighbor(type, s, d);
			struct aas_candidate *candidate;
			if (t == NONE || world->span_area[t] == NONE || world->span_area[t] == a)
				continue;
			candidate = aas_candidate_new(&list);
			if (!candidate)
				break;
			candidate->from = a;
			candidate->to = world->span_area[t];
			candidate->travel = _aas_travel_walk;
			candidate->key0 = d;
			/* the edge's line, and where along it */
			candidate->key1 = d == 0 ? aas_cell_x(s) + 1 : d == 1 ? aas_cell_x(s) : d == 2 ? aas_cell_y(s) + 1 : aas_cell_y(s);
			candidate->key2 = d < 2 ? aas_cell_y(s) : aas_cell_x(s);
			candidate->low_span = s;
			candidate->high_span = t;
		}
	}
	qsort(list.items, list.count, sizeof(struct aas_candidate), aas_candidate_compare);
	for (i = 0; i < list.count;)
	{
		struct aas_candidate const *first = &list.items[i];
		long j = i + 1;
		struct aas_candidate const *last;
		struct aas_reach *reach;
		real_point3d a0, a1, b0, b1;
		while (j < list.count && list.items[j].from == first->from && list.items[j].to == first->to && list.items[j].key0 == first->key0 &&
			list.items[j].key1 == first->key1 && list.items[j].key2 == list.items[j - 1].key2 + 1)
		{
			j++;
		}
		last = &list.items[j - 1];
		reach = aas_reach_new(reaches, first->from, first->to, _aas_travel_walk);
		if (!reach)
			break;
		/* the portal: from the middle of the first cell of the run to the
		   middle of the last, on the line between the two areas */
		navm_span_point(first->low_span, &a0);
		navm_span_point(first->high_span, &b0);
		navm_span_point(last->low_span, &a1);
		navm_span_point(last->high_span, &b1);
		reach->left.x = (a0.x + b0.x) * 0.5f;
		reach->left.y = (a0.y + b0.y) * 0.5f;
		reach->left.z = (a0.z + b0.z) * 0.5f;
		reach->right.x = (a1.x + b1.x) * 0.5f;
		reach->right.y = (a1.y + b1.y) * 0.5f;
		reach->right.z = (a1.z + b1.z) * 0.5f;
		reach->start.x = (reach->left.x + reach->right.x) * 0.5f;
		reach->start.y = (reach->left.y + reach->right.y) * 0.5f;
		reach->start.z = (reach->left.z + reach->right.z) * 0.5f;
		reach->edge = reach->start;
		reach->end = reach->start;
		/* (into a squeeze, nowhere in it more than a hand's width clear of
		   the walls: a body gets through, catching on them as it goes. A
		   way, but not one to take for the sake of a few steps saved) */
		reach->time = type == AAS_FOOT && world->areas[first->to].clearance < 0.32f ? 0.5f : 0.f;
		i = j;
	}
	free(list.items);
}

/* specks: a few cells that happen to be a body's width off everything,
   cut off from the areas round them by floor that isn't quite (in among
   the housing of a teleporter, between two crates). The floor's all one
   to walk on; an area of its own there, with no walk into it or out of
   it, only gets in the way: what's at it (the teleporter's pad, a weapon)
   would be tied to it, not to the real area a few steps off, and there'd
   be no way to it. Such are given up (they stay in the list, empty), and
   the floor there is reached from the areas round it like any other spot
   near a wall. (Not a speck with nothing near it: the top of a pillar, a
   stepping stone, is somewhere of its own, got to by a jump) */
static void aas_dissolve_specks(long type, struct aas_reaches *walks)
{
	struct aas_world *world = &aas_worlds[type];
	unsigned char *linked = (unsigned char *)calloc(MAX(1, world->area_count), 1);
	unsigned char *speck = (unsigned char *)calloc(MAX(1, world->area_count), 1);
	long steps = NAVM_CELLS(1.2f);
	long dissolved = 0;
	long r, a, s;
	if (!linked || !speck)
	{
		aas_no_memory("the specks");
		goto done;
	}
	for (r = 0; r < walks->count; r++)
		linked[walks->items[r].from] = linked[walks->items[r].to] = TRUE;
	for (a = 0; a < world->area_count; a++)
	{
		struct aas_area const *area = &world->areas[a];
		speck[a] = !linked[a] && MAX(area->x1 - area->x0, area->y1 - area->y0) < 0.45f;
	}
	for (s = 0; s < navm.span_count; s++)
	{
		long head = 0, tail = 0;
		long found = NONE;
		a = world->span_area[s];
		if (a == NONE || speck[a] != 1)
			continue;
		/* a real area within a few steps over the floor? */
		aas_new_search();
		aas_stamp[s] = aas_stamp_value;
		aas_queue[tail] = s;
		aas_depth[tail++] = 0;
		while (head < tail && found == NONE)
		{
			long u = aas_queue[head];
			long depth = aas_depth[head++];
			long d;
			if (world->span_area[u] != NONE && !speck[world->span_area[u]])
			{
				found = u;
				break;
			}
			if (depth >= steps)
				continue;
			for (d = 0; d < 8; d++)
			{
				long t = navm_walk_neighbor(u, d);
				if (t == NONE || aas_stamp[t] == aas_stamp_value || tail >= navm.span_count)
					continue;
				aas_stamp[t] = aas_stamp_value;
				aas_queue[tail] = t;
				aas_depth[tail++] = depth + 1;
			}
		}
		speck[a] = found != NONE ? 2 : 3; /* (2: to go; 3: on its own, stays) */
	}
	for (s = 0; s < navm.span_count; s++)
	{
		a = world->span_area[s];
		if (a != NONE && speck[a] == 2)
			world->span_area[s] = NONE;
	}
	for (a = 0; a < world->area_count; a++)
		dissolved += speck[a] == 2;
	if (bot_logging())
		bot_log("aas: type %ld: %ld specks given up", type, dissolved);
done:
	free(linked);
	free(speck);
}

/* the ledges: up with a jump, down off them, and across gaps (on foot) */
static void aas_ledge_reaches(long type, struct aas_reaches *reaches)
{
	struct aas_world *world = &aas_worlds[type];
	struct aas_candidates list;
	long attach_steps = NAVM_CELLS(type == AAS_VEHICLE ? 1.6f : 1.2f);
	long const reach_cells = NAVM_CELLS(3.2f); /* how far out from a ledge to look */
	long const jump_cells = NAVM_CELLS(0.6f); /* a ledge to jump up: this close */
	long const gap_cells = NAVM_CELLS(0.4f); /* a gap: at least this wide */
	real maximum_drop = type == AAS_VEHICLE ? AAS_VEHICLE_DROP : AAS_FOOT_DROP;
	long s;
	long i;

	memset(&list, 0, sizeof(list));
	for (s = 0; s < navm.span_count; s++)
	{
		struct navm_span const *span = &navm.spans[s];
		long d;
		if (!(span->flags & _navm_span_walkable) || (span->flags & _navm_span_blocked))
			continue;
		if (type == AAS_VEHICLE && span->ceiling - span->floor < NAVM_VEHICLE_HEIGHT)
			continue;
		if (!(span->flags & (_navm_span_wall_border | _navm_span_edge_border | _navm_span_vehicle_border)))
			continue;
		for (d = 0; d < 8; d++)
		{
			long k;
			long a = NONE;
			long a_span = NONE;
			boolean below = FALSE; /* floor down below on the way: over it only to somewhere at our height (a pit to cross) */
			boolean step_below = FALSE; /* ...only a step down: not even then */
			boolean dropped = FALSE; /* a drop found: on over the pit for a ledge to jump to */
			boolean opening = TRUE; /* room to walk off here */
			boolean crossed_floor = FALSE; /* floor at our height passed over on the way out */
			float dropped_floor = 0.f; /* (the pit's floor: that's no ledge) */
			if (aas_neighbor(type, s, d) != NONE)
				continue;
			/* room to walk off the edge at all: nothing at our level for a
			   body's width beyond it (a slit between two decks, with a floor
			   seen down it, is not a way down) */
			if (type == AAS_FOOT)
			{
				long side_cells = NAVM_CELL < 0.15f ? NAVM_CELLS(0.2f) : 0; /* (a body's width across, too: on the finer grids, which see slots that narrow) */
				long j, w;
				for (j = 1; j <= NAVM_CELLS(0.45f) && opening; j++)
				{
					/* (across: out where the body is as it goes down, not
					   right at the edge, where to one side or the other is
					   the ledge itself when it's left at an angle) */
					long across = j >= NAVM_CELLS(0.3f) ? side_cells : 0;
					for (w = -across; w <= across && opening; w++)
					{
						long bx = aas_cell_x(s) + navm_dx[d] * j - navm_dy[d] * w;
						long by = aas_cell_y(s) + navm_dy[d] * j + navm_dx[d] * w;
						long v;
						if (bx < 0 || by < 0 || bx >= navm.width || by >= navm.height)
							continue;
						for (v = navm_first_span_at(bx, by); v != NONE; v = navm.spans[v].next)
						{
							real rise = navm.spans[v].floor - span->floor;
							if (rise > -NAVM_STEP && rise < NAVM_HEIGHT)
								opening = FALSE;
						}
					}
				}
			}
			for (k = 1; k <= reach_cells; k++)
			{
				long x = aas_cell_x(s) + navm_dx[d] * k;
				long y = aas_cell_y(s) + navm_dy[d] * k;
				long u;
				long found = NONE;
				long travel = NONE;
				boolean level = FALSE;
				boolean level_open = FALSE; /* the level floor is room to stand, not a sliver by a wall */
				boolean column_below = FALSE;
				boolean column_step_below = FALSE;
				long level_span = NONE;
				boolean walkover = FALSE;
				if (x < 0 || y < 0 || x >= navm.width || y >= navm.height)
					break;
				for (u = navm_first_span_at(x, y); u != NONE; u = navm.spans[u].next)
				{
					struct navm_span const *other = &navm.spans[u];
					real rise = other->floor - span->floor;
					/* a floor down below on the way: a drop there, not a gap
					   over it */
					if (k >= 1 && rise < -NAVM_STEP && rise > -2.5f && (other->flags & _navm_span_walkable))
					{
						column_below = TRUE;
						/* (just down a step from us, got back up from with a hop:
						   nothing to jump over) */
						if (rise >= -NAVM_JUMP)
							column_step_below = TRUE;
					}
					if (rise > -NAVM_STEP && rise <= NAVM_STEP && (other->flags & _navm_span_walkable) && !(other->flags & _navm_span_blocked))
					{
						level = TRUE; /* floor at our height: not a clean ledge or gap beyond (the top of a railing,
						                 too narrow to stand on, isn't: jumped over, if the checks below allow) */
						if (other->wall >= AAS_FOOT_CLEARANCE)
							level_open = TRUE;
						level_span = u;
					}
					if (!(other->flags & _navm_span_walkable) || (other->flags & _navm_span_blocked))
						continue;
					if (type == AAS_VEHICLE && other->ceiling - other->floor < NAVM_VEHICLE_HEIGHT)
						continue;
					/* (jumped up: the jump's top is 0.69 up, the legs tucked a bit
					   more; a ledge right at that is a jump that fails as often as
					   not, so a margin) */
					if (d < 4 && k <= jump_cells && type == AAS_FOOT && rise > NAVM_STEP && rise <= NAVM_JUMP - 0.15f &&
						span->ceiling - span->floor >= rise + NAVM_HEIGHT)
					{
						found = u;
						travel = _aas_travel_jump;
					}
					else if (!dropped && k <= NAVM_CELLS(d < 4 ? 1.f : 0.6f) && rise < -(type == AAS_VEHICLE ? NAVM_VEHICLE_STEP : NAVM_STEP) &&
						-rise <= maximum_drop && other->ceiling >= span->floor + NAVM_HEIGHT && found == NONE && opening &&
						(type != AAS_FOOT || (other->wall >= 14 && other->edge >= 14)))
					{
						/* (somewhere a body lands and stays: not a shelf under the
						   ledge too narrow to stand on, which it slides off) */
						found = u;
						travel = _aas_travel_drop;
					}
					else if (type == AAS_FOOT && k >= gap_cells && (!below || (rise >= -NAVM_JUMP && !step_below)) && rise <= AAS_GAP_RISE && rise >= -3.5f &&
						(!dropped || rise >= -NAVM_JUMP || other->floor > dropped_floor + NAVM_JUMP) && found == NONE && other->wall >= 20)
					{
						/* a gap: far enough to need a jump, near enough to make (over
					   to the same height, or across and down onto something
					   lower: a platform, a ledge below) */
						real distance = k * NAVM_CELL * navm_step_length[d];
						real under = AAS_JUMP_VELOCITY * AAS_JUMP_VELOCITY - 2.f * AAS_GRAVITY * rise;
						real air = under >= 0.f ? (AAS_JUMP_VELOCITY + (real)sqrt(under)) / AAS_GRAVITY : 0.f;
						if (distance <= 0.9f * AAS_FOOT_SPEED * air)
						{
							found = u;
							travel = _aas_travel_gap;
						}
					}
				}
				/* floor at about our height (a step up or down at most) just the
				   other side of a slot no wider than a hand: the bevel at the
				   foot of a sill, the crack between two plates. Not walked
				   onto cell by cell, there being no floor in the slot, but a
				   body goes over it without noticing: a way across, walked */
				if (found == NONE && type == AAS_FOOT && level && level_span != NONE && k >= 2 && k <= NAVM_CELLS(0.2f) + 1 && !crossed_floor &&
					NAVM_CELL < 0.15f)
				{
					found = level_span;
					travel = _aas_travel_gap;
					walkover = TRUE;
				}
				if (level)
					crossed_floor = TRUE;
				if (found != NONE)
				{
					struct aas_candidate *candidate;
					long b_span = NONE;
					long b;
					if (a == NONE)
						a = aas_attach(type, s, attach_steps, &a_span);
					b = aas_attach(type, found, attach_steps, &b_span);
					if (a != NONE && b != NONE && a != b && (candidate = aas_candidate_new(&list)) != NULL)
					{
						real_point3d p0, p1;
						candidate->from = a;
						candidate->to = b;
						candidate->travel = travel;
						candidate->walkover = walkover;
						candidate->start_span = a_span;
						candidate->edge_span = s;
						candidate->end_span = b_span;
						candidate->low_span = found;
						navm_span_point(a_span, &p0);
						navm_span_point(s, &p1);
						candidate->cost = aas_distance2d(&p0, &p1);
						navm_span_point(b_span, &p0);
						navm_span_point(found, &p1);
						candidate->cost += aas_distance2d(&p0, &p1) + k * NAVM_CELL * 0.25f;
						/* down onto a ledge with a fall of its own just beyond,
						   the way we're going: run off the one above and it's
						   straight over the one below */
						if (travel == _aas_travel_drop && type == AAS_FOOT)
						{
							long j;
							for (j = k + 1; j <= k + NAVM_CELLS(1.2f) && !candidate->careful; j++)
							{
								long bx = aas_cell_x(s) + navm_dx[d] * j;
								long by = aas_cell_y(s) + navm_dy[d] * j;
								long v;
								boolean floor_there = FALSE;
								if (bx < 0 || by < 0 || bx >= navm.width || by >= navm.height)
									break;
								for (v = navm_first_span_at(bx, by); v != NONE; v = navm.spans[v].next)
								{
									real off = navm.spans[v].floor - navm.spans[found].floor;
									if (off > -1.5f && off < NAVM_HEIGHT)
										floor_there = TRUE; /* (floor on, or a wall: nowhere to fall) */
								}
								if (!floor_there)
									candidate->careful = TRUE;
							}
						}
					}
					/* down into a pit: and across it too, if there's a ledge
					   the far side at about our height (a jump over, as
					   people make for the rockets on a shelf) */
					if (travel == _aas_travel_drop && type == AAS_FOOT)
					{
						dropped = TRUE;
						dropped_floor = navm.spans[found].floor;
						continue;
					}
					break;
				}
				/* (a sliver of floor at our height between us and a step up,
				   too near the wall to stand in: on over it for the step) */
				if (level && (level_open || k > NAVM_CELLS(0.8f) || type != AAS_FOOT))
					break;
				below |= column_below;
				step_below |= column_step_below;
			}
		}
	}
	qsort(list.items, list.count, sizeof(struct aas_candidate), aas_candidate_compare);
	for (i = 0; i < list.count;)
	{
		long j = i;
		long tries = 0;
		boolean made = FALSE;
		while (j < list.count && list.items[j].from == list.items[i].from && list.items[j].to == list.items[i].to &&
			list.items[j].travel == list.items[i].travel)
		{
			struct aas_candidate const *candidate = &list.items[j];
			boolean hop = FALSE; /* a drop with a lip at the edge: jumped, not walked off */
			boolean barrier = FALSE; /* a gap with something knee high standing in it */
			j++;
			if (made || tries >= 6)
				continue;
			/* (sorted by cost among equal keys: only the key2.. ties matter
			   for walks; for these, the cheapest comes first) */
			if (candidate->travel == _aas_travel_gap && !candidate->walkover)
			{
				/* (no jump without a run-up: from the top of a ladder, a ledge
				   a hand's width wide, it falls short. Room to run at it from
				   straight behind the edge) */
				real_point3d edge, land, back;
				real dx, dy, length;
				navm_span_point(candidate->edge_span, &edge);
				navm_span_point(candidate->low_span, &land);
				dx = edge.x - land.x, dy = edge.y - land.y;
				length = (real)sqrt(dx * dx + dy * dy);
				if (length < 0.01f)
					continue;
				back = edge;
				back.x += dx / length * 0.7f;
				back.y += dy / length * 0.7f;
				if (!navm_line_walkable(&edge, &back, 0.f, 0.f))
					continue;
			}
			if (candidate->travel == _aas_travel_gap)
			{
				/* nothing in the way of the jump: at the feet, the waist, and
				   over the top of the arc */
				unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
					FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit);
				struct collision_result collision;
				real_point3d from, to;
				real_vector3d vector;
				real heights[3] = { 0.75f, 1.0f, 1.3f }; /* (in the air: over a railing, a knee-high lip) */
				long h;
				boolean clear = TRUE;
				tries++;
				navm_span_point(candidate->edge_span, &from);
				navm_span_point(candidate->low_span, &to);
				for (h = 0; h < 3 && clear; h++)
				{
					real_point3d a = from, b = to;
					a.z += heights[h];
					b.z += heights[h];
					vector.i = b.x - a.x;
					vector.j = b.y - a.y;
					vector.k = b.z - a.z;
					if (collision_test_vector(flags, &a, &vector, NONE, &collision))
					{
						clear = FALSE;
						if (getenv("HALO_BOT_GAPLOG"))
							bot_log("gap %ld->%ld blocked at h %.2f: %.2f %.2f %.2f -> %.2f %.2f %.2f hit %.2f %.2f %.2f", candidate->from, candidate->to,
								heights[h], a.x, a.y, a.z, b.x, b.y, b.z, collision.point.x, collision.point.y, collision.point.z);
					}
				}
				/* (and room for a body's width, not a line's: either side of
				   the middle, at the waist. A slit in a wall a hand wide lets
				   a line through) */
				if (clear)
				{
					real dx = to.x - from.x, dy = to.y - from.y;
					real length = (real)sqrt(dx * dx + dy * dy);
					long side;
					for (side = -1; side <= 1 && clear && length > 0.01f; side += 2)
					{
						real_point3d a = from, b = to;
						a.x += -dy / length * 0.18f * side, a.y += dx / length * 0.18f * side;
						b.x += -dy / length * 0.18f * side, b.y += dx / length * 0.18f * side;
						a.z += 1.0f;
						b.z += 1.0f;
						vector.i = b.x - a.x;
						vector.j = b.y - a.y;
						vector.k = b.z - a.z;
						if (collision_test_vector(flags, &a, &vector, NONE, &collision))
							clear = FALSE;
					}
				}
				/* across to about the same height, and not a pit between but
				   something standing there (a low wall across a doorway, a
				   kerb): over it only if it's no more than knee high, and then
				   with the jump begun well short of it */
				if (clear && to.z > from.z - 0.3f)
				{
					real lows[2] = { 0.62f, 0.35f };
					for (h = 0; h < 2 && clear; h++)
					{
						real_point3d a = from, b = to;
						a.z += lows[h];
						b.z += lows[h];
						vector.i = b.x - a.x;
						vector.j = b.y - a.y;
						vector.k = b.z - a.z;
						if (collision_test_vector(flags, &a, &vector, NONE, &collision))
						{
							if (h == 0)
								clear = FALSE;
							else
								barrier = TRUE;
						}
					}
				}
				if (!clear)
					continue;
			}
			else if (candidate->travel == _aas_travel_jump || candidate->travel == _aas_travel_drop)
			{
				/* nothing in the way over the ledge (a railing, a lip, a low
				   ceiling): across at the higher floor's knee and waist
				   height, and straight up or down the drop */
				unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
					FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit);
				struct collision_result collision;
				real_point3d low, high;
				real_vector3d vector;
				real heights[2] = { candidate->travel == _aas_travel_drop && type == AAS_FOOT ? 0.5f : 0.4f, 0.75f }; /* (lower down, on a drop on foot: a hop over it, below) */
				long h;
				boolean clear = TRUE;
				tries++;
				if (candidate->travel == _aas_travel_jump)
				{
					navm_span_point(candidate->edge_span, &low);
					navm_span_point(candidate->low_span, &high);
				}
				else
				{
					navm_span_point(candidate->edge_span, &high);
					navm_span_point(candidate->low_span, &low);
				}
				for (h = 0; h < 2 && clear; h++)
				{
					/* (on past the edge onto the high floor a little: a
					   railing, a pane, a clip wall just back from the edge) */
					real_point3d a = low, b = high;
					real dx = high.x - low.x;
					real dy = high.y - low.y;
					real length = (real)sqrt(dx * dx + dy * dy);
					if (length > 0.01f)
					{
						b.x += dx / length * 0.6f;
						b.y += dy / length * 0.6f;
					}
					a.z = b.z = high.z + heights[h];
					vector.i = b.x - a.x;
					vector.j = b.y - a.y;
					vector.k = 0.f;
					if (collision_test_vector(flags, &a, &vector, NONE, &collision))
						clear = FALSE;
					/* (a body's width of it, at the waist, on foot: not a slit
					   that only a line goes through) */
					if (clear && h == 1 && type == AAS_FOOT && length > 0.01f)
					{
						long side;
						for (side = -1; side <= 1 && clear; side += 2)
						{
							real_point3d c = a;
							c.x += -dy / length * 0.18f * side;
							c.y += dx / length * 0.18f * side;
							if (collision_test_vector(flags, &c, &vector, NONE, &collision))
								clear = FALSE;
						}
					}
				}
				if (clear)
				{
					/* up (or down) the face, on the low side */
					real_point3d a = low;
					a.z = low.z + 0.3f;
					vector.i = 0.f;
					vector.j = 0.f;
					vector.k = high.z + 0.75f - a.z;
					if (collision_test_vector(flags, &a, &vector, NONE, &collision))
						clear = FALSE;
				}
				if (clear && candidate->travel == _aas_travel_drop && type == AAS_FOOT) /* (a vehicle can't hop) */
				{
					/* a curb, a railing's foot at the edge (knee high, under
					   the heights above): no walking off, but a hop over it
					   and down is fine, as a gap is jumped */
					real dx = low.x - high.x;
					real dy = low.y - high.y;
					real length = (real)sqrt(dx * dx + dy * dy);
					real lift;
					/* (every few centimetres up to the waist: a bar at the
					   knee, open under, is only a few thick) */
					for (lift = 0.1f; lift < 0.5f && clear; lift += 0.05f)
					{
						real_point3d a = high, b = low;
						a.z = b.z = high.z + lift;
						if (length > 0.01f)
						{
							a.x -= dx / length * 0.3f;
							a.y -= dy / length * 0.3f;
						}
						vector.i = b.x - a.x;
						vector.j = b.y - a.y;
						vector.k = 0.f;
						if (collision_test_vector(flags, &a, &vector, NONE, &collision))
						{
							hop = TRUE;
							/* (shin high at the most: over anything more,
							   with a drop beyond it, a jump lands on it as
							   often as past it) */
							if (lift >= 0.3f)
								clear = FALSE;
						}
					}
				}
				if (!clear)
					continue;
			}
			{
				struct aas_reach *reach = aas_reach_new(reaches, candidate->from, candidate->to, hop ? _aas_travel_gap : candidate->travel);
				real run;
				if (!reach)
					break;
				navm_span_point(candidate->start_span, &reach->start);
				navm_span_point(candidate->edge_span, &reach->edge);
				navm_span_point(candidate->end_span, &reach->end);
				/* (down onto a ledge with a drop of its own close by: run off
				   the edge above and it's straight over the one below) */
				if (candidate->travel == _aas_travel_drop && type == AAS_FOOT && candidate->careful)
					reach->careful = TRUE;
				/* (not a gap at all, but floor all the way over that's too
				   close to a wall or a rock to be in an area: a rough ledge, a
				   squeeze past a boulder. A body gets along it; walked, no
				   run-up and no jump) */
				if (candidate->travel == _aas_travel_gap && !hop)
				{
					real_point3d from, to;
					navm_span_point(candidate->edge_span, &from);
					navm_span_point(candidate->low_span, &to);
					if (fabs(from.z - to.z) <= NAVM_STEP && navm_line_arrives(&from, &to, 0.12f, 0.f, TRUE))
						reach->careful = AAS_WALKOVER;
				}
				if (candidate->walkover)
					reach->careful = AAS_WALKOVER;
				if (candidate->travel == _aas_travel_gap && barrier && reach->careful != AAS_WALKOVER)
					reach->careful = AAS_HOP;
				if (hop)
					reach->careful = AAS_HOP;
				/* a jump's landing: on the ledge itself, not back at its area */
				if (candidate->travel == _aas_travel_jump || candidate->travel == _aas_travel_gap || hop)
				{
					real_point3d landing;
					navm_span_point(candidate->low_span, &landing);
					reach->left = landing;
				}
				run = aas_distance2d(&reach->start, &reach->edge) + aas_distance2d(&reach->edge, &reach->end);
				reach->time = run / aas_speed(type);
				if (hop)
					reach->time += 0.4f; /* (the hop over the lip; the drop's own cost, below, as well) */
				switch (candidate->travel)
				{
				case _aas_travel_jump:
					/* (the rise itself: a jump up is a way like any other,
					   taken whenever it is the quickest, and so is one up
					   and then another across) */
					reach->time += 0.25f;
					break;
				case _aas_travel_drop:
				{
					real height = reach->edge.z - reach->end.z;
					reach->time += 0.2f + height * 0.15f;
					/* hard landings hurt: only when it's much quicker */
					if (type == AAS_FOOT && height > 2.5f)
						reach->time += (height - 2.5f) * 1.5f;
					break;
				}
				case _aas_travel_gap:
					/* a gap jumped is the run across it and no more: as
					   good a way as the floor beside it (one that keeps
					   failing still costs what it has cost: aas_reach_failed) */
					if (reach->careful == AAS_WALKOVER)
						reach->time += 0.3f; /* (slow going, by the wall) */
					break;
				}
				made = TRUE;
			}
		}
		i = j;
	}
	free(list.items);
}

/* ladders: from the floor at the foot of a climbable face to the floor
   over its top */
static void aas_ladder_reaches(struct aas_reaches *reaches)
{
	struct aas_world *world = &aas_worlds[AAS_FOOT];
	long l;
	for (l = 0; l < navm.climbable_count; l++)
	{
		struct navm_climbable const *ladder = &navm.climbables[l];
		real_point3d foot;
		long foot_span;
		long a, a_span = NONE;
		long best = NONE;
		real best_distance = 1.0e9f;
		long x, y;
		long cx, cy;
		if (ladder->top_z - ladder->bottom_z < 0.8f)
			continue;
		foot.x = ladder->center.x + ladder->normal_x * 0.35f;
		foot.y = ladder->center.y + ladder->normal_y * 0.35f;
		foot.z = ladder->bottom_z + 0.3f;
		foot_span = navm_span_at(&foot, 0.5f, 1.2f);
		a = aas_attach(AAS_FOOT, foot_span, NAVM_CELLS(1.6f), &a_span);
		if (a == NONE)
			continue;
		/* the top: the nearest floor there (over it, or to one side where
		   the ladder runs up the side of a ledge) */
		cx = (long)floor((ladder->center.x - navm.x0) / NAVM_CELL);
		cy = (long)floor((ladder->center.y - navm.y0) / NAVM_CELL);
		for (y = cy - NAVM_CELLS(1.4f); y <= cy + NAVM_CELLS(1.4f); y++)
		{
			for (x = cx - NAVM_CELLS(1.4f); x <= cx + NAVM_CELLS(1.4f); x++)
			{
				long u;
				if (x < 0 || y < 0 || x >= navm.width || y >= navm.height)
					continue;
				for (u = navm_first_span_at(x, y); u != NONE; u = navm.spans[u].next)
				{
					real_point3d point;
					real distance;
					real out;
					if (!(navm.spans[u].flags & _navm_span_walkable) || navm.spans[u].floor < ladder->top_z - 1.2f ||
						navm.spans[u].floor > ladder->top_z + 0.5f || navm.spans[u].floor < foot.z + 0.6f)
					{
						continue;
					}
					navm_span_point(u, &point);
					out = (point.x - ladder->center.x) * ladder->normal_x + (point.y - ladder->center.y) * ladder->normal_y;
					distance = aas_distance2d(&point, &ladder->center) + (out > 0.2f ? 1.f : 0.f); /* over the top first */
					if (distance >= 1.5f)
						continue;
					/* (somewhere to stand that leads on: the floor the ladder
					   comes up through before the top of a post by its head,
					   which is a floor too, a hand's width across; and not a
					   shelf with no area near it at all) */
					{
						long top_area = aas_attach(AAS_FOOT, u, NAVM_CELLS(1.6f), NULL);
						struct aas_area const *top;
						if (top_area == NONE || top_area == a)
							continue;
						top = &world->areas[top_area];
						if (MAX(top->x1 - top->x0, top->y1 - top->y0) < 0.45f)
							distance += 4.f;
					}
					/* (and somewhere the climb gets to: nothing over the ladder
					   on the way up to it. A catwalk across the top of the
					   shaft is a floor by the ladder's head, and no way onto it
					   from underneath; the floor to get off at is lower down) */
					if (distance < best_distance)
					{
						unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
							FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit);
						struct collision_result collision;
						real_point3d from = foot;
						real_vector3d up = { 0.f, 0.f, 0.f };
						up.k = navm.spans[u].floor + 0.5f - from.z;
						if (up.k > 0.f && collision_test_vector(flags, &from, &up, NONE, &collision))
							continue;
					}
					if (distance < best_distance)
					{
						best_distance = distance;
						best = u;
					}
				}
			}
		}
		{
			long b_span = NONE;
			long b = aas_attach(AAS_FOOT, best, NAVM_CELLS(1.6f), &b_span);
			struct aas_reach *reach;
			if (b == NONE || b == a || !(reach = aas_reach_new(reaches, a, b, _aas_travel_ladder)))
			{
				if (bot_logging())
					bot_log("aas: ladder at %.1f %.1f %.1f-%.1f: no way off the top", ladder->center.x, ladder->center.y, ladder->bottom_z, ladder->top_z);
				continue;
			}
			navm_span_point(a_span, &reach->start);
			reach->edge = foot;
			reach->edge.z = navm.spans[foot_span].floor;
			navm_span_point(b_span, &reach->end);
			navm_span_point(best, &reach->left); /* the floor at the top */
			reach->face_i = ladder->normal_x;
			reach->face_j = ladder->normal_y;
			reach->time = aas_distance2d(&reach->start, &reach->edge) / AAS_FOOT_SPEED + (ladder->top_z - ladder->bottom_z) / 1.f + 1.5f;
			if (bot_logging())
				bot_log("aas: ladder at %.1f %.1f from %.1f up to %.1f", ladder->center.x, ladder->center.y, reach->edge.z, reach->left.z);
		}
	}
	(void)world;
}

/* teleporters (multiplayer flags: a source pad sends you to the target
   on the same channel) */
static void aas_teleporter_reaches(long type, struct aas_reaches *reaches)
{
	struct scenario *scenario = global_scenario_get();
	long i;
	for (i = 0; i < scenario->netgame_flags.count; i++)
	{
		byte *source = (byte *)xbox_pointer(scenario->netgame_flags.address) + i * 0x94;
		real_point3d const *from = (real_point3d const *)source;
		short channel = *(short *)(source + 0x12);
		long j;
		if (*(short *)(source + 0x10) != _netgame_flag_teleporter_source)
			continue;
		for (j = 0; j < scenario->netgame_flags.count; j++)
		{
			byte *target = (byte *)xbox_pointer(scenario->netgame_flags.address) + j * 0x94;
			real_point3d const *to = (real_point3d const *)target;
			long a_span = NONE, b_span = NONE;
			long a, b;
			struct aas_reach *reach;
			if (*(short *)(target + 0x10) != _netgame_flag_teleporter_target || *(short *)(target + 0x12) != channel)
				continue;
			a = aas_attach(type, navm_span_at(from, 0.6f, 1.5f), NAVM_CELLS(type == AAS_VEHICLE ? 2.4f : 1.6f), &a_span);
			b = aas_attach(type, navm_span_at(to, 0.6f, 1.5f), NAVM_CELLS(type == AAS_VEHICLE ? 2.4f : 1.6f), &b_span);
			if (a == NONE || b == NONE || a == b || !(reach = aas_reach_new(reaches, a, b, _aas_travel_teleport)))
				break;
			navm_span_point(a_span, &reach->start);
			reach->edge = *from;
			navm_span_point(b_span, &reach->end);
			reach->time = aas_distance2d(&reach->start, &reach->edge) / aas_speed(type) + 0.3f;
			if (bot_logging() && type == AAS_FOOT)
				bot_log("aas: teleporter %.1f %.1f %.1f -> %.1f %.1f %.1f", from->x, from->y, from->z, to->x, to->y, to->z);
			break;
		}
	}
}

static int aas_reach_from_compare(void const *a, void const *b)
{
	struct aas_reach const *ra = (struct aas_reach const *)a;
	struct aas_reach const *rb = (struct aas_reach const *)b;
	if (ra->from != rb->from)
		return ra->from < rb->from ? -1 : 1;
	if (ra->to != rb->to)
		return ra->to < rb->to ? -1 : 1;
	return ra->travel - rb->travel;
}

/* ---------- places and flags */

struct player_starting_location *player_get_starting_location(short starting_location_index);

static void aas_collect_places(void)
{
	struct scenario *scenario = global_scenario_get();
	long i;
	aas_places_count = 0;
	for (i = 0; i < scenario->players.count && aas_places_count < AAS_MAXIMUM_PLACES; i++)
	{
		struct player_starting_location *location = player_get_starting_location((short)i);
		if (location)
			aas_places[aas_places_count++] = location->position;
	}
	aas_spawn_count = aas_places_count;
	for (i = 0; i < scenario->netgame_equipment.count && aas_places_count < AAS_MAXIMUM_PLACES; i++)
	{
		/* scenario_netgame_equipment (game_engine.c): position at 0x40 */
		byte *element = (byte *)xbox_pointer(scenario->netgame_equipment.address) + i * 0x90;
		aas_places[aas_places_count++] = *(real_point3d *)(element + 0x40);
	}
	for (i = 0; i < scenario->netgame_flags.count && aas_places_count < AAS_MAXIMUM_PLACES; i++)
	{
		/* scenario_netgame_flag: position first, 0x94 each */
		byte *element = (byte *)xbox_pointer(scenario->netgame_flags.address) + i * 0x94;
		aas_places[aas_places_count++] = *(real_point3d *)element;
	}
}

/* reachable: got to from where people start (a vehicle's areas: under
   areas reachable on foot); playable: near the places people fight over
   (not up the canyon walls) */
static void aas_find_flags(long type)
{
	struct aas_world *world = &aas_worlds[type];
	long *queue = (long *)malloc(MAX(1, world->area_count) * sizeof(long));
	long head = 0, tail = 0;
	aas_nearest_forget();
	real top = -1.0e9f;
	long a;
	long p;

	if (!queue)
	{
		aas_no_memory("the areas' flags");
		return;
	}
	for (a = 0; a < world->area_count; a++)
		world->areas[a].flags = 0;
	if (type == AAS_FOOT)
	{
		/* (from where players start: a weapon lying somewhere says nothing
		   about whether there's a way to it) */
		long seeds = aas_spawn_count > 0 ? aas_spawn_count : aas_places_count;
		for (p = 0; p < seeds; p++)
		{
			long area = aas_nearest_area(AAS_FOOT, &aas_places[p], 3.f, NULL);
			if (area != NONE && !(world->areas[area].flags & _aas_area_reachable))
			{
				world->areas[area].flags |= _aas_area_reachable;
				queue[tail++] = area;
			}
		}
		while (head < tail)
		{
			long area = queue[head++];
			long r;
			for (r = world->areas[area].first_reach; r < world->areas[area].first_reach + world->areas[area].reach_count; r++)
			{
				long to = world->reaches[r].to;
				if (world->failures && world->failures[r] >= AAS_DEAD_FAILURES)
					continue; /* (not a way: bots keep failing at it) */
				if (!(world->areas[to].flags & _aas_area_reachable))
				{
					world->areas[to].flags |= _aas_area_reachable;
					queue[tail++] = to;
				}
			}
		}
		if (tail == 0)
			for (a = 0; a < world->area_count; a++)
				world->areas[a].flags |= _aas_area_reachable | _aas_area_escapable;
		else if (world->incoming)
		{
			/* and a way back from there (not a pit, not a ledge nobody gets
			   off): backwards from the places */
			head = tail = 0;
			for (p = 0; p < seeds; p++)
			{
				long area = aas_nearest_area(AAS_FOOT, &aas_places[p], 3.f, NULL);
				if (area != NONE && !(world->areas[area].flags & _aas_area_escapable))
				{
					world->areas[area].flags |= _aas_area_escapable;
					queue[tail++] = area;
				}
			}
			while (head < tail)
			{
				struct aas_area const *area = &world->areas[queue[head++]];
				long i;
				for (i = area->first_incoming; i < area->first_incoming + area->incoming_count; i++)
				{
					long r = world->incoming[i];
					long from = world->reaches[r].from;
					if (world->failures && world->failures[r] >= AAS_DEAD_FAILURES)
						continue;
					if (!(world->areas[from].flags & _aas_area_escapable))
					{
						world->areas[from].flags |= _aas_area_escapable;
						queue[tail++] = from;
					}
				}
			}
		}
	}
	else
	{
		for (a = 0; a < world->area_count; a++)
		{
			long foot = aas_nearest_area(AAS_FOOT, &world->areas[a].center, 1.5f, NULL);
			if (foot != NONE && (aas_worlds[AAS_FOOT].areas[foot].flags & _aas_area_reachable))
				world->areas[a].flags |= _aas_area_reachable;
		}
	}
	for (p = 0; p < aas_places_count; p++)
		top = MAX(top, aas_places[p].z);
	for (a = 0; a < world->area_count; a++)
	{
		struct aas_area *area = &world->areas[a];
		if (!(area->flags & _aas_area_reachable) || (aas_places_count > 0 && area->center.z > top + 6.f) ||
			(type == AAS_FOOT && !(area->flags & _aas_area_escapable)))
		{
			continue;
		}
		if (aas_places_count == 0)
			area->flags |= _aas_area_playable;
		for (p = 0; p < aas_places_count; p++)
		{
			/* (within a good way of one, and not far up above it: the middle
			   of a big map, the tops of its towers; not up a canyon wall) */
			if (aas_distance2d(&area->center, &aas_places[p]) < 60.f && area->center.z < aas_places[p].z + 6.f)
			{
				area->flags |= _aas_area_playable;
				break;
			}
		}
	}
	free(queue);
}

/* the places that aren't: a marker sunk under the floor (a hill's corner,
   set by eye in the editor) is on the floor over it if that can be got
   to; anything else there's no way to (a flag for a game this map's never
   played, out in the scenery) is no place to go, and is dropped. Where
   players start is always kept */
static void aas_prune_places(void)
{
	struct aas_world *world = &aas_worlds[AAS_FOOT];
	long kept = aas_spawn_count;
	long lifted = 0;
	long p;
	if (world->area_count <= 0)
		return;
	for (p = aas_spawn_count; p < aas_places_count; p++)
	{
		real_point3d place = aas_places[p];
		long area = aas_nearest_area(AAS_FOOT, &place, 3.f, NULL);
		if (area == NONE || !(world->areas[area].flags & _aas_area_reachable))
		{
			real_point3d above = place;
			real_point3d spot;
			above.z += 0.8f;
			area = aas_nearest_area(AAS_FOOT, &above, 1.f, &spot);
			if (area == NONE || !(world->areas[area].flags & _aas_area_reachable) || spot.z < place.z || spot.z > place.z + 1.2f)
				continue;
			place.z = spot.z;
			lifted++;
		}
		aas_places[kept++] = place;
	}
	if (bot_logging() && (kept != aas_places_count || lifted))
		bot_log("aas: places: %ld of %ld kept (no way to the others), %ld up onto the floor over them", kept, aas_places_count, lifted);
	aas_places_count = kept;
}

/* ---------- the cache */

struct aas_cache_header
{
	unsigned long magic;
	unsigned long version;
	unsigned long sizes[2];
	char map[128];
	unsigned long signature[4];
	long span_count;
	unsigned long span_hash; /* the heightfield the areas were made from (it can change with its count the same) */
	long area_count[NUMBER_OF_AAS_TYPES];
	long reach_count[NUMBER_OF_AAS_TYPES];
};

static unsigned long aas_span_hash_now; /* (taken when the cache is looked for, before the build touches the spans) */

static unsigned long aas_span_hash(void)
{
	unsigned long hash = 2166136261UL;
	unsigned char const *bytes = (unsigned char const *)navm.spans;
	size_t i, size = (size_t)navm.span_count * sizeof(struct navm_span);
	for (i = 0; bytes && i < size; i++)
		hash = (hash ^ bytes[i]) * 16777619UL;
	return hash;
}

/* the areas from a cache file of this map, made from the heightfield as it
   is: TRUE when they are whole */
static boolean aas_cache_read(FILE *file, char const *map, unsigned long const *signature)
{
	struct aas_cache_header header;
	boolean ok;
	long type;

	ok = fread(&header, sizeof(header), 1, file) == 1 && header.magic == AAS_CACHE_MAGIC && header.version == AAS_CACHE_VERSION &&
		header.sizes[0] == sizeof(struct aas_area) && header.sizes[1] == sizeof(struct aas_reach) && !strcmp(header.map, map) &&
		!memcmp(header.signature, signature, 4 * sizeof(unsigned long)) && header.span_count == navm.span_count && header.span_hash == aas_span_hash_now;
	for (type = 0; ok && type < NUMBER_OF_AAS_TYPES; type++)
		ok = header.area_count[type] >= 0 && header.area_count[type] < 16 * 1024 * 1024 &&
			header.reach_count[type] >= 0 && header.reach_count[type] < 64 * 1024 * 1024; /* (before anything's sized by them) */
	for (type = 0; ok && type < NUMBER_OF_AAS_TYPES; type++)
	{
		struct aas_world *world = &aas_worlds[type];
		world->area_count = header.area_count[type];
		world->reach_count = header.reach_count[type];
		world->areas = (struct aas_area *)malloc(MAX(1, world->area_count) * sizeof(struct aas_area));
		world->reaches = (struct aas_reach *)malloc(MAX(1, world->reach_count) * sizeof(struct aas_reach));
		world->span_area = (long *)malloc(MAX(1, navm.span_count) * sizeof(long));
		ok = world->areas && world->reaches && world->span_area &&
			fread(world->areas, sizeof(struct aas_area), (size_t)world->area_count, file) == (size_t)world->area_count &&
			fread(world->reaches, sizeof(struct aas_reach), (size_t)world->reach_count, file) == (size_t)world->reach_count &&
			fread(world->span_area, sizeof(long), (size_t)navm.span_count, file) == (size_t)navm.span_count;
		/* (a damaged file: links between areas that exist, or build afresh) */
		if (ok)
		{
			long r;
			ok = world->area_count >= 0 && world->reach_count >= 0;
			/* (and in the order of the area each one leaves, as they are
			   written: each area's are found by it, aas_finish_world) */
			for (r = 0; ok && r < world->reach_count; r++)
				ok = world->reaches[r].from >= 0 && world->reaches[r].from < world->area_count &&
					world->reaches[r].to >= 0 && world->reaches[r].to < world->area_count &&
					(r == 0 || world->reaches[r].from >= world->reaches[r - 1].from);
			for (r = 0; ok && r < navm.span_count; r++)
				ok = world->span_area[r] == NONE || (world->span_area[r] >= 0 && world->span_area[r] < world->area_count);
		}
	}
	if (!ok)
	{
		/* (nothing of a file that was not whole) */
		for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
		{
			struct aas_world *world = &aas_worlds[type];
			free(world->areas);
			free(world->reaches);
			free(world->span_area);
			world->areas = NULL;
			world->reaches = NULL;
			world->span_area = NULL;
			world->area_count = 0;
			world->reach_count = 0;
		}
	}
	return ok;
}

static boolean aas_cache_load(void)
{
	char map[128];
	unsigned long signature[4];
	long attempt;

	aas_span_hash_now = aas_span_hash();
	/* (baking: worked out afresh) */
	if (getenv("HALO_BOT_NOCACHE") || navm_is_baking())
		return FALSE;
	navm_signature(map, signature);
	/* the baked file, then this machine's own */
	for (attempt = 0; attempt < 2; attempt++)
	{
		char path[512];
		FILE *file;

		navm_cache_path(map, "aas", attempt == 0, path, sizeof(path));
		file = fopen(path, "rb");
		if (!file)
			continue;
		if (aas_cache_read(file, map, signature))
		{
			fclose(file);
			if (bot_logging())
				bot_log("aas: read %s", path);
			return TRUE;
		}
		fclose(file);
	}
	return FALSE;
}

/* whether the map has baked areas of this build's kind (the file's header
   only: bot_nav.c, navm_baked_current) */
boolean aas_baked_current(char const *map_name, long bsp_index)
{
	struct aas_cache_header header;
	char path[512];
	FILE *file;
	boolean ok;

	if (!navm_baked_current(map_name, bsp_index))
		return FALSE;
	navm_cache_file(map_name, bsp_index, "aas", TRUE, path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return FALSE;
	ok = fread(&header, sizeof(header), 1, file) == 1 && header.magic == AAS_CACHE_MAGIC && header.version == AAS_CACHE_VERSION &&
		header.sizes[0] == sizeof(struct aas_area) && header.sizes[1] == sizeof(struct aas_reach);
	/* (and all of it there) */
	if (ok)
	{
		double size = sizeof(header);
		long type;

		for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
			size += (double)header.area_count[type] * sizeof(struct aas_area) + (double)header.reach_count[type] * sizeof(struct aas_reach) +
				(double)header.span_count * sizeof(long);
		ok = fseek(file, 0, SEEK_END) == 0 && (double)ftell(file) == size;
	}
	fclose(file);
	return ok;
}

static void aas_cache_save(void)
{
	struct aas_cache_header header;
	char path[512];
	FILE *file;
	long type;
	boolean whole;
	memset(&header, 0, sizeof(header));
	header.magic = AAS_CACHE_MAGIC;
	header.version = AAS_CACHE_VERSION;
	header.sizes[0] = sizeof(struct aas_area);
	header.sizes[1] = sizeof(struct aas_reach);
	navm_signature(header.map, header.signature);
	header.span_count = navm.span_count;
	header.span_hash = aas_span_hash_now;
	for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
	{
		header.area_count[type] = aas_worlds[type].area_count;
		header.reach_count[type] = aas_worlds[type].reach_count;
	}
	navm_cache_path(header.map, "aas", navm_is_baking(), path, sizeof(path));
	file = fopen(path, "wb");
	if (!file)
	{
		navm_baked_file(FALSE, path);
		return;
	}
	whole = fwrite(&header, sizeof(header), 1, file) == 1;
	for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
	{
		struct aas_world *world = &aas_worlds[type];
		whole = fwrite(world->areas, sizeof(struct aas_area), (size_t)world->area_count, file) == (size_t)world->area_count && whole;
		whole = fwrite(world->reaches, sizeof(struct aas_reach), (size_t)world->reach_count, file) == (size_t)world->reach_count && whole;
		whole = fwrite(world->span_area, sizeof(long), (size_t)navm.span_count, file) == (size_t)navm.span_count && whole;
	}
	whole = fclose(file) == 0 && whole;
	navm_baked_file(whole, path);
}

/* ---------- building */

static void aas_world_free(struct aas_world *world)
{
	long i;
	free(world->areas);
	free(world->reaches);
	free(world->incoming);
	free(world->span_area);
	free(world->attach_span);
	free(world->attach_depth);
	free(world->failures);
	free(world->fall);
	free(world->random_weight);
	free(world->region);
	free(world->region_size);
	free(world->region_first);
	free(world->region_areas);
	free(world->reach_from);
	free(world->incoming_time);
	free(world->pair_base);
	free(world->pair_time);
	for (i = 0; i < AAS_CACHES; i++)
	{
		free(world->caches[i].time);
		free(world->caches[i].open);
		aas_shared_release(world->caches[i].penalty);
		aas_shared_release(world->caches[i].cost);
	}
	aas_shared_release(world->penalty);
	for (i = 0; i < AAS_COST_SLOTS; i++)
		aas_shared_release(world->area_cost[i]);
	memset(world, 0, sizeof(*world));
}

/* what bots learned about the map (the ways they kept failing at), kept
   from game to game. Each time it's read back the counts come down one, so
   a way stays given up on only while bots keep failing at it */
#define AAS_FAIL_MAGIC 0x46534141UL /* 'AASF' */
/* which lessons these are: with the areas' version, so that what the bots
   learned while they moved another way is not held against a way they can
   now take (2: gaps jumped with a late tuck, and timed from their run) */
#define AAS_FAIL_VERSION (AAS_CACHE_VERSION + (2UL << 16))

static void aas_failures_path(char *path, long size, unsigned long *signature)
{
	char map[128];
	navm_signature(map, signature);
	/* (what this machine's bots learned: its own, never baked) */
	navm_cache_path(map, "aasfail", FALSE, path, size);
}

/* where the areas in use came from (the file their failures go back to:
   by the time they're saved, the map may be on another BSP) */
static char aas_failures_saved_path[512];
static unsigned long aas_failures_saved_signature[4];

static void aas_failures_save(void)
{
	char path[512];
	unsigned long signature[4];
	unsigned long header[8];
	FILE *file;
	long type;
	/* (baking: one bot's moment in a map teaches nothing worth keeping) */
	if (!aas_is_ready || getenv("HALO_BOT_NOCACHE") || navm_is_baking() || !aas_failures_saved_path[0])
		return;
	memcpy(path, aas_failures_saved_path, sizeof(path));
	memcpy(signature, aas_failures_saved_signature, sizeof(signature));
	file = fopen(path, "wb");
	if (!file)
		return;
	header[0] = AAS_FAIL_MAGIC;
	header[1] = AAS_FAIL_VERSION;
	header[2] = (unsigned long)aas_worlds[AAS_FOOT].reach_count;
	header[3] = (unsigned long)aas_worlds[AAS_VEHICLE].reach_count;
	memcpy(&header[4], signature, sizeof(signature));
	fwrite(header, sizeof(header), 1, file);
	for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
		if (aas_worlds[type].failures && aas_worlds[type].reach_count > 0)
			fwrite(aas_worlds[type].failures, 1, (size_t)aas_worlds[type].reach_count, file);
	fclose(file);
}

static void aas_failures_load(void)
{
	char path[512];
	unsigned long signature[4];
	unsigned long header[8];
	FILE *file;
	long type;
	if (getenv("HALO_BOT_NOCACHE"))
		return;
	aas_failures_path(path, sizeof(path), signature);
	memcpy(aas_failures_saved_path, path, sizeof(aas_failures_saved_path));
	memcpy(aas_failures_saved_signature, signature, sizeof(aas_failures_saved_signature));
	file = fopen(path, "rb");
	if (!file)
		return;
	if (fread(header, sizeof(header), 1, file) == 1 && header[0] == AAS_FAIL_MAGIC && header[1] == AAS_FAIL_VERSION &&
		header[2] == (unsigned long)aas_worlds[AAS_FOOT].reach_count && header[3] == (unsigned long)aas_worlds[AAS_VEHICLE].reach_count &&
		!memcmp(&header[4], signature, sizeof(signature)))
	{
		for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
		{
			struct aas_world *world = &aas_worlds[type];
			long r;
			long dead = 0;
			if (!world->failures || world->reach_count <= 0 ||
				fread(world->failures, 1, (size_t)world->reach_count, file) != (size_t)world->reach_count)
			{
				break;
			}
			if (!aas_shared_writable(&world->penalty))
				break;
			for (r = 0; r < world->reach_count; r++)
			{
				if (world->failures[r] > 0)
					world->failures[r]--;
				world->penalty[r] = aas_failure_penalty(world->failures[r]);
				dead += world->failures[r] >= 2;
			}
			if (bot_logging() && dead)
				bot_log("aas: %s: %ld ways bots have been failing at, from before", type == AAS_VEHICLE ? "vehicle" : "foot", dead);
		}
	}
	fclose(file);
}

void aas_dispose(void)
{
	long type;
	aas_nearest_forget();
	aas_failures_save();
	for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
		aas_world_free(&aas_worlds[type]);
	free(aas_stamp);
	free(aas_queue);
	free(aas_depth);
	free(aas_check_time);
	aas_check_time = NULL;
	aas_check_capacity = 0;
	aas_stamp = NULL;
	aas_queue = NULL;
	aas_depth = NULL;
	aas_is_ready = FALSE;
	navm_dispose();
}

boolean aas_ready(void)
{
	return aas_is_ready;
}

/* reachabilities in order of the area they leave; the index of those
   into each area; the rest */
/* how far down the floor is under a gap: what a jump that comes up short
   falls (a great deal where there is none) */
static float aas_gap_fall(struct aas_reach const *reach)
{
	float fall = 0.f;
	long i;
	for (i = 1; i <= 3; i++)
	{
		real_point3d point;
		real floor_z;
		point.x = reach->edge.x + (reach->left.x - reach->edge.x) * i / 4.f;
		point.y = reach->edge.y + (reach->left.y - reach->edge.y) * i / 4.f;
		point.z = MIN(reach->edge.z, reach->left.z) - 0.4f;
		if (!navm_floor_at(&point, 0.f, 100.f, &floor_z))
			return 1000.f;
		fall = MAX(fall, reach->edge.z - floor_z);
	}
	return fall;
}

static void aas_finish_world(long type)
{
	struct aas_world *world = &aas_worlds[type];
	long *count;
	long r, a;

	world->incoming = (long *)malloc(MAX(1, world->reach_count) * sizeof(long));
	world->penalty = aas_shared_new(world->reach_count, TRUE);
	world->failures = (unsigned char *)calloc(MAX(1, world->reach_count), 1);
	world->fall = (float *)calloc(MAX(1, world->reach_count), sizeof(float));
	if (world->fall)
		for (r = 0; r < world->reach_count; r++)
			if (world->reaches[r].travel == _aas_travel_gap && world->reaches[r].careful != AAS_WALKOVER)
				world->fall[r] = aas_gap_fall(&world->reaches[r]);
	world->random_weight = (float *)malloc(MAX(1, world->area_count) * sizeof(float));
	count = (long *)calloc(MAX(1, world->area_count) + 1, sizeof(long));
	if (!world->incoming || !world->penalty || !world->failures || !world->fall || !world->random_weight || !count)
	{
		aas_no_memory("the ways into each area");
		free(count);
		return;
	}
	for (a = 0; a < world->area_count; a++)
	{
		world->areas[a].first_reach = 0;
		world->areas[a].reach_count = 0;
	}
	for (r = world->reach_count - 1; r >= 0; r--)
	{
		world->areas[world->reaches[r].from].first_reach = r;
		world->areas[world->reaches[r].from].reach_count++;
		count[world->reaches[r].to + 1]++;
	}
	for (a = 0; a < world->area_count; a++)
	{
		count[a + 1] += count[a];
		world->areas[a].first_incoming = count[a];
		world->areas[a].incoming_count = 0;
	}
	for (r = 0; r < world->reach_count; r++)
	{
		struct aas_area *to = &world->areas[world->reaches[r].to];
		world->incoming[to->first_incoming + to->incoming_count++] = r;
	}
	free(count);

	/* what the routes' search reads, side by side (aas_route_advance): the
	   very numbers it would work out from the reachabilities as it goes */
	{
		real speed = aas_speed(type);
		long pairs = 0;
		long i, j;

		world->reach_from = (long *)malloc(MAX(1, world->reach_count) * sizeof(long));
		world->incoming_time = (float *)malloc(MAX(1, world->reach_count) * sizeof(float));
		world->pair_base = (long *)malloc(MAX(1, world->area_count) * sizeof(long));
		for (a = 0; a < world->area_count; a++)
			pairs += world->areas[a].reach_count * world->areas[a].incoming_count;
		world->pair_time = (float *)malloc(MAX(1, pairs) * sizeof(float));
		if (!world->reach_from || !world->incoming_time || !world->pair_base || !world->pair_time)
		{
			aas_no_memory("the routes' times");
			free(world->pair_time);
			world->pair_time = NULL;
			return;
		}
		for (r = 0; r < world->reach_count; r++)
		{
			world->reach_from[r] = world->reaches[r].from;
			world->incoming_time[r] = world->reaches[world->incoming[r]].time;
		}
		pairs = 0;
		for (a = 0; a < world->area_count; a++)
		{
			struct aas_area const *area = &world->areas[a];
			world->pair_base[a] = pairs;
			for (i = 0; i < area->reach_count; i++)
			{
				struct aas_reach const *out = &world->reaches[area->first_reach + i];
				for (j = 0; j < area->incoming_count; j++)
				{
					struct aas_reach const *in = &world->reaches[world->incoming[area->first_incoming + j]];
					world->pair_time[pairs++] = aas_distance(&in->end, &out->start) / speed;
				}
			}
		}
	}
}

/* The regions: areas joined by any way at all, either way (a drop joins as
   a walk does), share one. Somewhere in another region cannot be got to on
   foot whatever the route: a very large map's parts, each worked out apart
   (bot_nav.c, navm_choose_parts), a ledge nothing leads to. Asked before a
   route is looked for (aas_route_reach, aas_goal_time, and by the bots of
   what they weigh): a route that cannot be searches the whole of the
   region its goal is in to find that out. */
static void aas_find_regions(long type)
{
	struct aas_world *world = &aas_worlds[type];
	long head = 0, tail = 0;
	long a;

	free(world->region);
	free(world->region_size);
	free(world->region_first);
	free(world->region_areas);
	world->region_count = 0;
	world->region = (long *)malloc(MAX(1, world->area_count) * sizeof(long));
	world->region_size = (long *)calloc(MAX(1, world->area_count), sizeof(long));
	world->region_first = (long *)calloc(MAX(1, world->area_count), sizeof(long));
	world->region_areas = (long *)malloc(MAX(1, world->area_count) * sizeof(long));
	if (!world->region || !world->region_size || !world->region_first || !world->region_areas || !world->incoming)
	{
		free(world->region);
		free(world->region_size);
		free(world->region_first);
		free(world->region_areas);
		world->region = NULL;
		world->region_size = NULL;
		world->region_first = NULL;
		world->region_areas = NULL;
		return;
	}
	for (a = 0; a < world->area_count; a++)
		world->region[a] = NONE;
	/* (the areas in the order they are come to, a region at a time: the
	   list of each region's own) */
	for (a = 0; a < world->area_count; a++)
	{
		long region;
		if (world->region[a] != NONE)
			continue;
		region = world->region_count++;
		world->region[a] = region;
		world->region_first[region] = tail;
		world->region_areas[tail++] = a;
		while (head < tail)
		{
			struct aas_area const *area = &world->areas[world->region_areas[head++]];
			long i;
			world->region_size[region]++;
			for (i = 0; i < area->reach_count; i++)
			{
				long to = world->reaches[area->first_reach + i].to;
				if (world->region[to] == NONE)
					world->region[to] = region, world->region_areas[tail++] = to;
			}
			for (i = 0; i < area->incoming_count; i++)
			{
				long from = world->reaches[world->incoming[area->first_incoming + i]].from;
				if (world->region[from] == NONE)
					world->region[from] = region, world->region_areas[tail++] = from;
			}
		}
	}
}

long aas_area_region(long type, long area)
{
	struct aas_world const *world = &aas_worlds[type];
	return world->region && area >= 0 && area < world->area_count ? world->region[area] : NONE;
}

/* how many areas a region has (a ledge on its own: one or two) */
long aas_region_size(long type, long region)
{
	struct aas_world const *world = &aas_worlds[type];
	return world->region_size && region >= 0 && region < world->region_count ? world->region_size[region] : 0;
}

/* one of a region's areas (index: 0 to its size less one) */
long aas_region_area(long type, long region, long index)
{
	struct aas_world const *world = &aas_worlds[type];
	return world->region_areas && region >= 0 && region < world->region_count && index >= 0 && index < world->region_size[region] ?
		world->region_areas[world->region_first[region] + index] : NONE;
}

static void aas_find_random_weights(long type)
{
	struct aas_world *world = &aas_worlds[type];
	float total = 0.f;
	long a;
	for (a = 0; a < world->area_count; a++)
	{
		struct aas_area const *area = &world->areas[a];
		if (area->flags & _aas_area_playable)
			total += (area->x1 - area->x0) * (area->y1 - area->y0);
		world->random_weight[a] = total;
	}
}

boolean aas_build(void)
{
	unsigned long long start = bot_time_microseconds();
	long type;

	aas_dispose();
	aas_build_failed = FALSE;
	if (!navm_build())
		return FALSE;
	aas_stamp = (unsigned long *)calloc(navm.span_count, sizeof(unsigned long));
	aas_queue = (long *)malloc(navm.span_count * sizeof(long));
	aas_depth = (long *)malloc(navm.span_count * sizeof(long));
	aas_stamp_value = 0;
	if (!aas_stamp || !aas_queue || !aas_depth)
	{
		aas_dispose();
		return FALSE;
	}
	aas_collect_places();
	if (!aas_cache_load())
	{
		for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
		{
			struct aas_reaches reaches;
			aas_world_free(&aas_worlds[type]);
			memset(&reaches, 0, sizeof(reaches));
			{
				unsigned long long t0 = bot_time_microseconds(), t1, t2, t3;
				aas_build_areas(type);
				t1 = bot_time_microseconds();
				/* (each stage reads what the one before made: none of them
				   after one that could not finish) */
				if (!aas_build_failed)
					aas_walk_reaches(type, &reaches);
				if (!aas_build_failed && !getenv("HALO_BOT_OLDNAV"))
					aas_dissolve_specks(type, &reaches);
				if (!aas_build_failed)
					aas_find_attachments(type, FALSE);
				t2 = bot_time_microseconds();
				if (!aas_build_failed)
					aas_ledge_reaches(type, &reaches);
				t3 = bot_time_microseconds();
				if (bot_logging())
					bot_log("aas: type %ld: areas %.2f s, walks %.2f s, ledges %.2f s", type, (t1 - t0) / 1.0e6, (t2 - t1) / 1.0e6, (t3 - t2) / 1.0e6);
			}
			if (type == AAS_FOOT && !aas_build_failed)
				aas_ladder_reaches(&reaches);
			if (!aas_build_failed)
				aas_teleporter_reaches(type, &reaches);
			if (reaches.count > 0)
				qsort(reaches.items, reaches.count, sizeof(struct aas_reach), aas_reach_from_compare);
			aas_worlds[type].reaches = reaches.items;
			aas_worlds[type].reach_count = reaches.count;
			if (aas_build_failed)
				break;
		}
		/* (only what is whole is kept for the next game) */
		if (!aas_build_failed)
			aas_cache_save();
	}
	else
	{
		for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
			aas_find_attachments(type, FALSE);
	}
	for (type = 0; type < NUMBER_OF_AAS_TYPES && !aas_build_failed; type++)
	{
		aas_finish_world(type);
		if (!aas_build_failed)
			aas_find_regions(type);
	}
	if (aas_build_failed)
	{
		/* no navigation at all, which the bots play without (straight to
		   each place), sooner than one with parts of the map missing */
		aas_dispose();
		return FALSE;
	}
	aas_failures_load();
	for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
	{
		aas_find_flags(type);
		aas_find_random_weights(type);
	}
	aas_is_ready = aas_worlds[AAS_FOOT].area_count > 0;
	aas_nearest_forget();
	if (aas_is_ready)
	{
		long before = aas_places_count;
		aas_prune_places();
		/* (playable goes by the places: again, without the ones dropped) */
		if (aas_places_count != before)
			for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
				aas_find_flags(type);
		for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
			aas_find_attachments(type, TRUE);
	}
	if (aas_build_failed)
	{
		aas_dispose();
		return FALSE;
	}
	if (bot_logging())
	{
		long counts[NUMBER_OF_AAS_TYPES][NUMBER_OF_AAS_TRAVEL_TYPES];
		long reachable[NUMBER_OF_AAS_TYPES] = { 0, 0 };
		long r, a;
		memset(counts, 0, sizeof(counts));
		for (type = 0; type < NUMBER_OF_AAS_TYPES; type++)
		{
			for (r = 0; r < aas_worlds[type].reach_count; r++)
				counts[type][aas_worlds[type].reaches[r].travel]++;
			for (a = 0; a < aas_worlds[type].area_count; a++)
				reachable[type] += (aas_worlds[type].areas[a].flags & _aas_area_reachable) != 0;
		}
		bot_log("aas: foot %ld areas (%ld reachable), reachabilities walk %ld jump %ld drop %ld gap %ld ladder %ld teleport %ld",
			aas_worlds[AAS_FOOT].area_count, reachable[AAS_FOOT], counts[0][0], counts[0][1], counts[0][2], counts[0][3], counts[0][4], counts[0][5]);
		bot_log("aas: vehicle %ld areas (%ld reachable), reachabilities walk %ld drop %ld teleport %ld; %.2f s",
			aas_worlds[AAS_VEHICLE].area_count, reachable[AAS_VEHICLE], counts[1][0], counts[1][2], counts[1][5],
			(bot_time_microseconds() - start) / 1.0e6);
	}
	/* testing (HALO_BOT_REACHNEAR=x,y): the reachabilities (other than
	   walks) near a point */
	if (aas_is_ready && getenv("HALO_BOT_REACHNEAR"))
	{
		real_point3d near_point = { 0.f, 0.f, 0.f };
		long r;
		sscanf(getenv("HALO_BOT_REACHNEAR"), "%f,%f", &near_point.x, &near_point.y);
		for (r = 0; r < aas_worlds[AAS_FOOT].reach_count; r++)
		{
			struct aas_reach const *reach = &aas_worlds[AAS_FOOT].reaches[r];
			if (reach->travel != _aas_travel_walk && aas_distance2d(&reach->edge, &near_point) < 3.f)
				bot_log("aas: near reach %ld travel %d start %.2f %.2f %.2f edge %.2f %.2f %.2f end %.2f %.2f %.2f land %.2f %.2f %.2f",
					r, reach->travel, reach->start.x, reach->start.y, reach->start.z, reach->edge.x, reach->edge.y, reach->edge.z,
					reach->end.x, reach->end.y, reach->end.z, reach->left.x, reach->left.y, reach->left.z);
		}
	}
	/* testing (HALO_BOT_NAVDUMP=file): the whole thing as text, for looking
	   at with a script: every floor, every area, every reachability */
	if (aas_is_ready && getenv("HALO_BOT_NAVDUMP"))
	{
		FILE *file = fopen(getenv("HALO_BOT_NAVDUMP"), "w");
		if (file)
		{
			long u, a, r;
			fprintf(file, "grid %f %f %ld %ld %f\n", navm.x0, navm.y0, navm.width, navm.height, (double)NAVM_CELL);
			for (u = 0; u < navm.span_count; u++)
				fprintf(file, "s %ld %ld %.3f %.3f %d %d %d %ld\n", navm.spans[u].column % navm.width, navm.spans[u].column / navm.width,
					navm.spans[u].floor, navm.spans[u].ceiling, navm.spans[u].flags, navm.spans[u].wall, navm.spans[u].edge,
					aas_worlds[AAS_FOOT].span_area[u]);
			for (a = 0; a < aas_worlds[AAS_FOOT].area_count; a++)
			{
				struct aas_area const *area = &aas_worlds[AAS_FOOT].areas[a];
				fprintf(file, "a %ld %.2f %.2f %.2f %.2f %.3f %.3f %ld\n", a, area->x0, area->y0, area->x1, area->y1, area->z0, area->z1, (long)area->flags);
			}
			for (r = 0; r < aas_worlds[AAS_FOOT].reach_count; r++)
			{
				struct aas_reach const *reach = &aas_worlds[AAS_FOOT].reaches[r];
				fprintf(file, "r %ld %d %ld %ld %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %d %d\n", r, reach->travel, reach->from, reach->to,
					reach->start.x, reach->start.y, reach->start.z, reach->edge.x, reach->edge.y, reach->edge.z,
					reach->end.x, reach->end.y, reach->end.z, reach->time, aas_worlds[AAS_FOOT].failures ? aas_worlds[AAS_FOOT].failures[r] : 0,
					(int)reach->careful);
			}
			for (r = 0; r < navm.climbable_count; r++)
				fprintf(file, "l %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", navm.climbables[r].center.x, navm.climbables[r].center.y, navm.climbables[r].center.z,
					navm.climbables[r].normal_x, navm.climbables[r].normal_y, navm.climbables[r].bottom_z, navm.climbables[r].top_z, navm.climbables[r].half_width);
			fclose(file);
		}
	}
	/* testing (HALO_BOT_NAVIMAGE=u:\x.ppm): a picture, top floor of each
	   column: grey no area, areas in colours (vehicle areas brighter),
	   white the starts of reachabilities other than walks */
	if (aas_is_ready && getenv("HALO_BOT_NAVIMAGE"))
	{
		FILE *file = fopen(getenv("HALO_BOT_NAVIMAGE"), "wb");
		if (file)
		{
			long y;
			unsigned char *image = (unsigned char *)calloc((size_t)(navm.width * navm.height), 3);
			fprintf(file, "P6 %ld %ld 255\n", navm.width, navm.height);
			for (y = 0; y < navm.height && image; y++)
			{
				long x;
				for (x = 0; x < navm.width; x++)
				{
					long u;
					long top = NONE;
					unsigned char *rgb = &image[((navm.height - 1 - y) * navm.width + x) * 3];
					for (u = navm_first_span_at(x, y); u != NONE; u = navm.spans[u].next)
						if (aas_worlds[AAS_FOOT].span_area[u] != NONE && (aas_worlds[AAS_FOOT].areas[aas_worlds[AAS_FOOT].span_area[u]].flags & _aas_area_reachable))
							top = u;
					if (top == NONE)
					{
						/* the top floor here: walkable (dark yellow), scenery
						   (dark red), no room (grey) */
						long last = NONE;
						for (u = navm_first_span_at(x, y); u != NONE; u = navm.spans[u].next)
							last = u;
						if (last != NONE)
						{
							if (navm.spans[last].flags & _navm_span_blocked)
								rgb[0] = 110;
							else if (navm.spans[last].flags & _navm_span_walkable)
								rgb[0] = rgb[1] = 90;
							else
								rgb[0] = rgb[1] = rgb[2] = 50;
						}
						continue;
					}
					{
						unsigned long h = (unsigned long)aas_worlds[AAS_FOOT].span_area[top] * 2654435761UL;
						int vehicle = aas_worlds[AAS_VEHICLE].span_area[top] != NONE;
						rgb[0] = (unsigned char)(40 + (h >> 8) % 120 + (vehicle ? 80 : 0));
						rgb[1] = (unsigned char)(40 + (h >> 16) % 120 + (vehicle ? 80 : 0));
						rgb[2] = (unsigned char)(40 + (h >> 24) % 120);
					}
				}
			}
			if (image)
			{
				long r;
				for (r = 0; r < aas_worlds[AAS_FOOT].reach_count; r++)
				{
					struct aas_reach const *reach = &aas_worlds[AAS_FOOT].reaches[r];
					long column;
					if (reach->travel == _aas_travel_walk)
						continue;
					column = navm_column_of(reach->edge.x, reach->edge.y);
					if (column != NONE)
					{
						unsigned char *rgb = &image[((navm.height - 1 - column / navm.width) * navm.width + column % navm.width) * 3];
						rgb[0] = rgb[1] = rgb[2] = 255;
					}
				}
				fwrite(image, 3, (size_t)(navm.width * navm.height), file);
				free(image);
			}
			fclose(file);
		}
	}
	return aas_is_ready;
}

/* ---------- queries */

long aas_area_count(long type)
{
	return aas_worlds[type].area_count;
}

struct aas_area const *aas_area(long type, long area)
{
	return &aas_worlds[type].areas[area];
}

struct aas_reach const *aas_reach(long type, long reach)
{
	return &aas_worlds[type].reaches[reach];
}

long aas_reach_count(long type)
{
	return aas_worlds[type].reach_count;
}

long aas_point_area(long type, real_point3d const *point)
{
	long s;
	if (!aas_is_ready)
		return NONE;
	s = navm_span_at(point, 0.3f, 1.2f);
	return s == NONE ? NONE : aas_worlds[type].span_area[s];
}

/* the search itself: the area, and the span it was found by */
static long aas_nearest_area_search(long type, real_point3d const *point, real radius, long *found_out)
{
	struct aas_world *world = &aas_worlds[type];
	long s;
	long found = NONE;
	long area;
	s = navm_span_at(point, 0.3f, 1.5f);
	area = NONE;
	if (s != NONE)
		area = aas_attach(type, s, (long)(radius / NAVM_CELL) + 1, &found);
	if (area == NONE)
	{
		/* not over a floor (in the air, on a vehicle, on something that
		   isn't the map): the nearest floor around at about this height */
		long cx = (long)floor((point->x - navm.x0) / NAVM_CELL);
		long cy = (long)floor((point->y - navm.y0) / NAVM_CELL);
		long reach = (long)(radius / NAVM_CELL) + 1;
		real best = 1.0e9f;
		long x, y;
		found = NONE;
		for (y = cy - reach; y <= cy + reach; y++)
		{
			for (x = cx - reach; x <= cx + reach; x++)
			{
				long u;
				if (x < 0 || y < 0 || x >= navm.width || y >= navm.height)
					continue;
				for (u = navm_first_span_at(x, y); u != NONE; u = navm.spans[u].next)
				{
					real_point3d there;
					real distance;
					if (world->span_area[u] == NONE)
						continue;
					navm_span_point(u, &there);
					if (there.z > point->z + 0.5f || there.z < point->z - 3.f)
						continue;
					distance = aas_distance(&there, point);
					if (distance > radius + 1.f)
						continue;
					/* (one there's a way to, before one there isn't) */
					if (world->attach_reachable && !(world->areas[world->span_area[u]].flags & _aas_area_reachable))
						distance += 2.f;
					if (distance < best)
					{
						best = distance;
						found = u;
					}
				}
			}
		}
		if (found == NONE)
			return NONE;
		area = world->span_area[found];
		if (area == NONE)
			return NONE;
	}
	*found_out = found;
	return area;
}

long aas_nearest_area(long type, real_point3d const *point, real radius, real_point3d *spot)
{
	struct aas_world *world = &aas_worlds[type];
	struct aas_nearest_memo *memo;
	long found = NONE;
	long area;
	if (!aas_is_ready && !world->span_area)
		return NONE;
	/* asked before, of the areas as they are (not while they are being
	   built: they change under it) */
	memo = aas_is_ready ? aas_nearest_memo_for(type, point, radius) : NULL;
	if (memo && memo->generation == aas_nearest_generation && memo->type == type && memo->radius == radius &&
		!memcmp(&memo->point, point, sizeof(memo->point)))
	{
		area = memo->area;
		found = memo->found;
		/* (testing, HALO_BOT_ROUTE_CHECK: against the search) */
		if (aas_route_check())
		{
			long found_now = NONE;
			long area_now = aas_nearest_area_search(type, point, radius, &found_now);
			static long wrong;
			if ((area_now != area || (area != NONE && found_now != found)) && wrong++ < 20)
				bot_log("aas: NEAREST CHECK: area %ld by span %ld kept, %ld by %ld searched", area, found, area_now, found_now);
		}
	}
	else
	{
		area = aas_nearest_area_search(type, point, radius, &found);
		if (memo)
		{
			memo->generation = aas_nearest_generation;
			memo->type = type;
			memo->point = *point;
			memo->radius = radius;
			memo->area = area;
			memo->found = found;
		}
	}
	if (area == NONE)
		return NONE;
	if (spot)
	{
		/* the nearest point of the area's rectangle (a little in) */
		struct aas_area const *a = &world->areas[area];
		real_point3d clamped;
		long c;
		clamped.x = PIN(point->x, a->x0 + NAVM_CELL * 0.5f, a->x1 - NAVM_CELL * 0.5f);
		clamped.y = PIN(point->y, a->y0 + NAVM_CELL * 0.5f, a->y1 - NAVM_CELL * 0.5f);
		clamped.z = navm.spans[found].floor + 0.2f;
		c = navm_span_at(&clamped, 1.f, 1.5f);
		if (c != NONE && world->span_area[c] == area)
			navm_span_point(c, spot);
		else
			navm_span_point(found, spot);
	}
	return area;
}

boolean aas_line_in_areas(long type, real_point3d const *from, real_point3d const *to)
{
	struct aas_world *world = &aas_worlds[type];
	real dx = to->x - from->x;
	real dy = to->y - from->y;
	real length = (real)sqrt(dx * dx + dy * dy);
	long steps = (long)(length / (NAVM_CELL * 0.5f)) + 1;
	real z;
	long i;
	long s;

	if (!aas_is_ready)
		return FALSE;
	s = navm_span_at(from, 0.3f, 1.2f);
	if (s == NONE || world->span_area[s] == NONE)
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
		if (best == NONE || world->span_area[best] == NONE)
			return FALSE;
		z = navm.spans[best].floor;
	}
	return fabs(z - to->z) < 1.2f;
}

/* ---------- routing

The times from every reachability to a goal are a search backwards from the
goal over the reachabilities (Dijkstra). It is run only as far as it is
asked about: a bot wants the way from where it stands, which is settled
once the search has come that far from the goal, and a goal a few rooms off
(someone it is after) never costs a search of the whole map. A later
question from further off carries the search on from where it stopped. What
comes out is what the whole search would give: a time is used only once it
is final.

Where it stops: reachabilities come off the heap in order of their time, so
once the least time on the heap is more than some bound, every time no more
than the bound is final (frontier), and every other is more than it. The
heap itself is not kept: carrying on, it is every reachability reached and
not final. */

/* testing: HALO_BOT_ROUTE_CHECK, every answer against the whole search's */
static int aas_route_checking = -1;

static boolean aas_route_check(void)
{
	if (aas_route_checking < 0)
	{
		/* ("fail": with ways failing all the while, for the penalties) */
		char const *check = getenv("HALO_BOT_ROUTE_CHECK");
		aas_route_checking = !check ? 0 : !strcmp(check, "fail") ? 2 : 1;
	}
	return aas_route_checking;
}

static boolean aas_heap_reserve(struct aas_world const *world)
{
	if (aas_heap_capacity < world->reach_count * 4 + 16)
	{
		free(aas_heap_items);
		free(aas_heap_keys);
		aas_heap_capacity = world->reach_count * 4 + 16;
		aas_heap_items = (long *)malloc(aas_heap_capacity * sizeof(long));
		aas_heap_keys = (float *)malloc(aas_heap_capacity * sizeof(float));
		if (!aas_heap_items || !aas_heap_keys)
		{
			free(aas_heap_items);
			free(aas_heap_keys);
			aas_heap_items = NULL;
			aas_heap_keys = NULL;
			aas_heap_capacity = 0;
			return FALSE;
		}
	}
	return TRUE;
}

/* a route is done with what it held to search with */
static void aas_route_release(struct aas_cache *cache)
{
	aas_shared_release(cache->penalty);
	aas_shared_release(cache->cost);
	cache->penalty = NULL;
	cache->cost = NULL;
}

/* room for so many of the reached and not final */
static boolean aas_route_open_reserve(struct aas_cache *cache, long count)
{
	if (cache->open_capacity < count)
	{
		long capacity = MAX(count, 256);
		long *open = (long *)realloc(cache->open, capacity * sizeof(long));
		if (!open)
			return FALSE;
		cache->open = open;
		cache->open_capacity = capacity;
	}
	return TRUE;
}

/* the route to goal by the side's area costs: found among the ones worked
   out, or begun (the reachabilities into the goal's area have their times;
   nothing is final yet) */
static struct aas_cache *aas_route(long type, long goal, long slot)
{
	struct aas_world *world = &aas_worlds[type];
	struct aas_cache *cache = NULL;
	real speed = aas_speed(type);
	float *time;
	unsigned long epoch;
	long i;

	if (!world->pair_time)
		return NULL;
	if (slot < 0 || slot >= AAS_COST_SLOTS || !world->area_cost[slot])
		slot = NONE;
	epoch = slot != NONE ? world->cost_epoch[slot] : 0;
	if (aas_budget_tick != game_time_get())
	{
		aas_budget_tick = game_time_get();
		aas_budget_used = 0;
		aas_new_used = 0;
	}
	for (i = 0; i < AAS_CACHES; i++)
	{
		struct aas_cache *entry = &world->caches[i];
		if (entry->time && entry->goal == goal && entry->slot == slot)
		{
			entry->used = ++world->cache_clock;
			if ((entry->epoch == epoch && entry->penalty_epoch == world->penalty_epoch) ||
				aas_budget_used >= AAS_ROUTES_PER_TICK)
			{
				return entry; /* (current, or good enough for now) */
			}
			cache = entry; /* out of date: redo it in place */
			if (bot_profiling)
				bot_profile_add(entry->epoch != epoch ? "route: costs changed" : "route: penalties changed", 0);
			break;
		}
	}
	if (!cache)
	{
		if (aas_route_may_wait && aas_new_used >= AAS_NEW_ROUTES_PER_TICK)
		{
			aas_route_waits = TRUE;
			return NULL;
		}
		aas_new_used++;
	}
	aas_budget_used++;
	if (bot_profiling && !cache)
		bot_profile_add(slot != NONE ? "route: new goal (costs)" : "route: new goal", 0);
	/* the least recently used */
	if (!cache)
		for (i = 0; i < AAS_CACHES; i++)
			if (!cache || world->caches[i].used < cache->used)
				cache = &world->caches[i];
	if (!cache->time)
		cache->time = (float *)malloc(MAX(1, world->reach_count) * sizeof(float));
	if (!cache->time)
		return NULL;
	aas_route_release(cache);
	cache->goal = goal;
	cache->slot = slot;
	cache->epoch = epoch;
	cache->penalty_epoch = world->penalty_epoch;
	cache->used = ++world->cache_clock;
	cache->complete = FALSE;
	cache->frontier = -1.f;
	cache->penalty = aas_shared_retain(world->penalty);
	cache->cost = slot != NONE ? aas_shared_retain(world->area_cost[slot]) : NULL;
	time = cache->time;
	for (i = 0; i < world->reach_count; i++)
		time[i] = 1.0e30f;
	cache->open_count = 0;
	{
		struct aas_area const *area = &world->areas[goal];
		if (!aas_route_open_reserve(cache, area->incoming_count))
		{
			cache->goal = NONE; /* (not a route) */
			return NULL;
		}
		for (i = area->first_incoming; i < area->first_incoming + area->incoming_count; i++)
		{
			long r = world->incoming[i];
			struct aas_reach const *reach = &world->reaches[r];
			float t = reach->time + cache->penalty[r] + aas_distance(&reach->end, &area->center) / speed;
			/* (each of them once: a reachability comes into one area) */
			if (time[r] >= 1.0e29f)
				cache->open[cache->open_count++] = r;
			if (t < time[r])
				time[r] = t;
		}
	}
	return cache;
}

/* The search's queue. A way on to the reachability before costs 0.02
   seconds at the least (the hop), so the reachabilities whose times fall
   in one 64th of a second cannot better one another: they are taken in
   any order, bucket by bucket, with no heap to keep (Dial's queue). A
   time is final once its bucket has been gone through. The buckets are a
   ring of AAS_BUCKET_RING from the one being gone through; a time further
   off than that waits in a list of its own. */
#define AAS_BUCKETS_PER_SECOND 64.0f
#define AAS_BUCKET_RING 16384 /* (a power of two) */
#define AAS_BUCKET(time) ((long)((time) * AAS_BUCKETS_PER_SECOND))

static long *aas_bucket_heads; /* [AAS_BUCKET_RING]: the first entry of each, NONE for none */
static long *aas_entry_reach;
static long *aas_entry_next;
static long aas_entry_capacity;
static unsigned long *aas_route_done; /* per reachability: the search that went through it last */
static unsigned long *aas_route_kept; /* ... and the search that kept it as reached and not final */
static long aas_route_done_capacity;
static unsigned long aas_route_search;

static boolean aas_queue_reserve(struct aas_world const *world)
{
	long capacity = world->reach_count * 4 + 16;
	long i;
	if (!aas_bucket_heads)
	{
		aas_bucket_heads = (long *)malloc(AAS_BUCKET_RING * sizeof(long));
		if (!aas_bucket_heads)
			return FALSE;
		for (i = 0; i < AAS_BUCKET_RING; i++)
			aas_bucket_heads[i] = NONE;
	}
	if (aas_entry_capacity < capacity)
	{
		free(aas_entry_reach);
		free(aas_entry_next);
		aas_entry_reach = (long *)malloc(capacity * sizeof(long));
		aas_entry_next = (long *)malloc(capacity * sizeof(long));
		aas_entry_capacity = aas_entry_reach && aas_entry_next ? capacity : 0;
		if (!aas_entry_capacity)
		{
			free(aas_entry_reach);
			free(aas_entry_next);
			aas_entry_reach = NULL;
			aas_entry_next = NULL;
			return FALSE;
		}
	}
	if (aas_route_done_capacity < world->reach_count)
	{
		free(aas_route_done);
		free(aas_route_kept);
		aas_route_done = (unsigned long *)calloc(MAX(1, world->reach_count), sizeof(unsigned long));
		aas_route_kept = (unsigned long *)calloc(MAX(1, world->reach_count), sizeof(unsigned long));
		aas_route_done_capacity = aas_route_done && aas_route_kept ? world->reach_count : 0;
		if (!aas_route_done_capacity)
		{
			free(aas_route_done);
			free(aas_route_kept);
			aas_route_done = NULL;
			aas_route_kept = NULL;
			return FALSE;
		}
	}
	return TRUE;
}

/* the greatest time of a bucket (the float before the next bucket's first) */
static float aas_bucket_last_time(long bucket)
{
	union
	{
		float value;
		unsigned int bits;
	} next;
	next.value = (float)(bucket + 1) / AAS_BUCKETS_PER_SECOND;
	next.bits--;
	return next.value;
}

/* carries the search on until the least of the watched reachabilities'
   times (each with its offset) is final, or there is nothing left to
   search: then every time in its bucket and the ones before is final too
   (the frontier) */
static void aas_route_advance(struct aas_world *world, struct aas_cache *cache, long const *watched, float const *offsets,
	long watched_count)
{
	float *time = cache->time;
	float const *penalty = cache->penalty;
	float const *cost = cache->cost;
	float const *incoming_time = world->incoming_time;
	long const *incoming = world->incoming;
	long const *reach_from = world->reach_from;
	long *heads;
	long *entry_reach;
	long *entry_next;
	unsigned long *done;
	unsigned long search;
	long capacity;
	long used = 0; /* entries taken from the pool so far */
	long free_entry = NONE; /* ...and given back */
	long live = 0; /* entries waiting: in the ring, and far */
	long ring_live = 0;
	long far = NONE; /* the entries further off than the ring */
	long far_bucket = 0; /* the least bucket among them */
	long current; /* the bucket being gone through */
	long ring_last; /* the last bucket of the ring with anything in it */
	long bound_bucket;
	float bound = 1.0e30f;
	float least = 1.0e30f;
	long watched_area = watched_count > 0 ? reach_from[watched[0]] : NONE;
	long r, i;
	BOT_PROFILE_BEGIN(route);

	if (cache->complete || !aas_queue_reserve(world))
		return;
	heads = aas_bucket_heads;
	entry_reach = aas_entry_reach;
	entry_next = aas_entry_next;
	done = aas_route_done;
	capacity = world->reach_count * 4 + 16;
	search = ++aas_route_search;
	if (!search)
	{
		/* (round again: no old mark is this search's) */
		memset(done, 0, (size_t)aas_route_done_capacity * sizeof(unsigned long));
		memset(aas_route_kept, 0, (size_t)aas_route_done_capacity * sizeof(unsigned long));
		search = ++aas_route_search;
	}
	/* (the watched are all of one area, or the one reachability) */
	for (i = 0; i < watched_count; i++)
	{
		float t = offsets[i] + time[watched[i]];
		if (time[watched[i]] < 1.0e29f && t < bound)
			bound = t;
	}
	bound_bucket = bound < 1.0e29f ? AAS_BUCKET(bound) : LONG_MAX;
	/* the queue: every reachability reached and not final (as the search
	   left them), from the least */
	for (i = 0; i < cache->open_count; i++)
		if (time[cache->open[i]] < least)
			least = time[cache->open[i]];
	if (least >= 1.0e29f)
	{
		cache->complete = TRUE;
		if (!aas_route_check())
			aas_route_release(cache);
		return;
	}
	current = AAS_BUCKET(least);

/* (an entry for the reachability, whose time has just been set) */
#define AAS_QUEUE_ADD(reach) \
	{ \
		long entry_ = free_entry; \
		long bucket_ = AAS_BUCKET(time[reach]); \
		if (entry_ != NONE) \
			free_entry = entry_next[entry_]; \
		else \
			entry_ = used++; \
		entry_reach[entry_] = (reach); \
		live++; \
		if (bucket_ - current < AAS_BUCKET_RING) \
		{ \
			entry_next[entry_] = heads[bucket_ & (AAS_BUCKET_RING - 1)]; \
			heads[bucket_ & (AAS_BUCKET_RING - 1)] = entry_; \
			ring_live++; \
			if (bucket_ > ring_last) \
				ring_last = bucket_; \
		} \
		else \
		{ \
			if (far == NONE || bucket_ < far_bucket) \
				far_bucket = bucket_; \
			entry_next[entry_] = far; \
			far = entry_; \
		} \
	}

	ring_last = current;
	for (i = 0; i < cache->open_count; i++)
	{
		r = cache->open[i];
		AAS_QUEUE_ADD(r);
	}
	while (live > 0)
	{
		long entry;

		/* the far ones that the ring has come within reach of (all of them
		   when it is empty: on to the first of them) */
		if (far != NONE && (!ring_live || far_bucket - current < AAS_BUCKET_RING / 2))
		{
			long list = far;

			if (!ring_live && far_bucket > current)
				current = far_bucket;
			far = NONE;
			while (list != NONE)
			{
				long next = entry_next[list];
				long bucket;

				r = entry_reach[list];
				bucket = AAS_BUCKET(time[r]);
				if (done[r] == search || bucket < current)
				{
					/* (bettered since, and gone through already) */
					entry_next[list] = free_entry;
					free_entry = list;
					live--;
				}
				else if (bucket - current < AAS_BUCKET_RING)
				{
					entry_next[list] = heads[bucket & (AAS_BUCKET_RING - 1)];
					heads[bucket & (AAS_BUCKET_RING - 1)] = list;
					ring_live++;
					if (bucket > ring_last)
						ring_last = bucket;
				}
				else
				{
					if (far == NONE || bucket < far_bucket)
						far_bucket = bucket;
					entry_next[list] = far;
					far = list;
				}
				list = next;
			}
			continue;
		}
		if (current > bound_bucket)
			break;
		entry = heads[current & (AAS_BUCKET_RING - 1)];
		heads[current & (AAS_BUCKET_RING - 1)] = NONE;
		while (entry != NONE)
		{
			long next = entry_next[entry];
			struct aas_area const *area;
			float const *pair;
			float key;
			long first;
			long n;
			boolean watched_changed = FALSE;

			r = entry_reach[entry];
			entry_next[entry] = free_entry;
			free_entry = entry;
			entry = next;
			live--;
			ring_live--;
			/* (one entry for each time it was bettered: the first to come up
			   is the one with its time) */
			if (done[r] == search)
				continue;
			done[r] = search;
			key = time[r];
			area = &world->areas[reach_from[r]];
			first = area->first_incoming;
			n = area->incoming_count;
			pair = &world->pair_time[world->pair_base[reach_from[r]] + (r - area->first_reach) * n];
			if (cost)
			{
				float through = cost[reach_from[r]]; /* crossing the area q leads into (the danger there) */
				for (i = 0; i < n; i++)
				{
					long q = incoming[first + i];
					float t = key + incoming_time[first + i] + penalty[q] + pair[i] + 0.02f; /* (a hop costs a little: no ties going back and forth) */
					t += through;
					if (t < time[q] && live < capacity)
					{
						time[q] = t;
						AAS_QUEUE_ADD(q);
						watched_changed |= reach_from[q] == watched_area;
					}
				}
			}
			else
			{
				for (i = 0; i < n; i++)
				{
					long q = incoming[first + i];
					float t = key + incoming_time[first + i] + penalty[q];
					/* (no less with the rest added: nothing to gain here) */
					if (t >= time[q])
						continue;
					t = t + pair[i] + 0.02f;
					if (t < time[q] && live < capacity)
					{
						time[q] = t;
						AAS_QUEUE_ADD(q);
						watched_changed |= reach_from[q] == watched_area;
					}
				}
			}
			if (watched_changed)
			{
				for (i = 0; i < watched_count; i++)
				{
					float t = offsets[i] + time[watched[i]];
					if (time[watched[i]] < 1.0e29f && t < bound)
						bound = t;
				}
				/* (a way out it avoids is not watched: maybe none yet) */
				bound_bucket = bound < 1.0e29f ? AAS_BUCKET(bound) : LONG_MAX;
			}
		}
		current++;
	}
#undef AAS_QUEUE_ADD
	cache->open_count = 0;
	if (live <= 0)
	{
		cache->complete = TRUE;
		/* (testing: kept, for the whole search to be made again with) */
		if (!aas_route_check())
			aas_route_release(cache);
	}
	else
	{
		/* what is left in the queue is where it carries on from: each
		   reachability not gone through, once (it has an entry for each
		   time it was bettered). The ring is left as it was found, empty. */
		unsigned long *kept = aas_route_kept;
		long bucket;
		boolean room = aas_route_open_reserve(cache, live);

		cache->frontier = aas_bucket_last_time(bound_bucket);
		for (bucket = current; bucket <= ring_last && bucket - current < AAS_BUCKET_RING; bucket++)
		{
			long entry = heads[bucket & (AAS_BUCKET_RING - 1)];

			heads[bucket & (AAS_BUCKET_RING - 1)] = NONE;
			for (; entry != NONE; entry = entry_next[entry])
			{
				r = entry_reach[entry];
				if (done[r] != search && kept[r] != search && room)
				{
					kept[r] = search;
					cache->open[cache->open_count++] = r;
				}
			}
		}
		for (; far != NONE; far = entry_next[far])
		{
			r = entry_reach[far];
			if (done[r] != search && kept[r] != search && room)
			{
				kept[r] = search;
				cache->open[cache->open_count++] = r;
			}
		}
		if (!room)
		{
			/* (no memory for them: found again the long way) */
			if (aas_route_open_reserve(cache, world->reach_count))
				for (r = 0; r < world->reach_count; r++)
					if (time[r] > cache->frontier && time[r] < 1.0e29f)
						cache->open[cache->open_count++] = r;
		}
	}
	BOT_PROFILE_END(route, "route search");
}

/* testing: the whole search, as it was before it was run in parts, with
   what the route began with (which it keeps while this is on) */
static float const *aas_route_check_times(long type, struct aas_cache const *cache)
{
	struct aas_world *world = &aas_worlds[type];
	struct navm_heap heap;
	real speed = aas_speed(type);
	float *time;
	float const *cost = cache->cost;
	long i;

	if (!cache->penalty || !aas_heap_reserve(world))
		return NULL;
	if (aas_check_capacity < world->reach_count)
	{
		free(aas_check_time);
		aas_check_capacity = world->reach_count;
		aas_check_time = (float *)malloc(MAX(1, aas_check_capacity) * sizeof(float));
		if (!aas_check_time)
		{
			aas_check_capacity = 0;
			return NULL;
		}
	}
	time = aas_check_time;
	for (i = 0; i < world->reach_count; i++)
		time[i] = 1.0e30f;
	heap.items = aas_heap_items;
	heap.keys = aas_heap_keys;
	heap.count = 0;
	{
		struct aas_area const *area = &world->areas[cache->goal];
		for (i = area->first_incoming; i < area->first_incoming + area->incoming_count; i++)
		{
			long r = world->incoming[i];
			struct aas_reach const *reach = &world->reaches[r];
			float t = reach->time + cache->penalty[r] + aas_distance(&reach->end, &area->center) / speed;
			if (t < time[r])
			{
				time[r] = t;
				navm_heap_push(&heap, r, t);
			}
		}
	}
	while (heap.count > 0)
	{
		float key;
		long r = navm_heap_pop(&heap, &key);
		struct aas_reach const *reach = &world->reaches[r];
		struct aas_area const *area = &world->areas[reach->from];
		if (key > time[r])
			continue;
		for (i = area->first_incoming; i < area->first_incoming + area->incoming_count; i++)
		{
			long q = world->incoming[i];
			struct aas_reach const *before = &world->reaches[q];
			float t = key + before->time + cache->penalty[q] + aas_distance(&before->end, &reach->start) / speed + 0.02f;
			if (cost)
				t += cost[reach->from];
			if (t < time[q] && heap.count < world->reach_count * 4 + 16)
			{
				time[q] = t;
				navm_heap_push(&heap, q, t);
			}
		}
	}
	return time;
}

float aas_goal_time(long type, long reach, long goal_area, long slot)
{
	struct aas_world *world = &aas_worlds[type];
	struct aas_cache *cache;
	float result;
	if (!aas_is_ready || goal_area == NONE || reach == NONE)
		return -1.f;
	if (world->region && world->region[world->reaches[reach].to] != world->region[goal_area])
		return -1.f; /* (as aas_route_reach: another part of the map) */
	cache = aas_route(type, goal_area, slot);
	if (!cache)
		return -1.f;
	if (!cache->complete && !(cache->time[reach] <= cache->frontier))
	{
		float offset = 0.f;
		aas_route_advance(world, cache, &reach, &offset, 1);
	}
	result = cache->time[reach] >= 1.0e29f ? -1.f : cache->time[reach];
	if (aas_route_check())
	{
		static unsigned long checked, wrong;
		float const *whole = aas_route_check_times(type, cache);
		if (whole)
		{
			float expected = whole[reach] >= 1.0e29f ? -1.f : whole[reach];
			checked++;
			if (expected != result && wrong++ < 20)
				bot_log("aas: ROUTE CHECK: goal time of reach %ld to area %ld is %f, the whole search's %f", reach, goal_area, result, expected);
			if (checked % 20000 == 0)
				bot_log("aas: route check: %lu goal times checked, %lu wrong", checked, wrong);
		}
	}
	return result;
}

/* how far to the way into a link: a doorway (a walk) is a whole edge, the
   nearest point on it, not its middle (several doors out of a long room:
   the one beside us, not the one whose middle happens to be nearer) */
static real aas_reach_entry_distance(real_point3d const *from, struct aas_reach const *reach)
{
	if (reach->travel == _aas_travel_walk)
	{
		real ex = reach->right.x - reach->left.x, ey = reach->right.y - reach->left.y, ez = reach->right.z - reach->left.z;
		real length2 = ex * ex + ey * ey + ez * ez;
		real f = length2 > 1.0e-6f ?
			((from->x - reach->left.x) * ex + (from->y - reach->left.y) * ey + (from->z - reach->left.z) * ez) / length2 : 0.f;
		real_point3d nearest;
		f = PIN(f, 0.f, 1.f);
		nearest.x = reach->left.x + ex * f;
		nearest.y = reach->left.y + ey * f;
		nearest.z = reach->left.z + ez * f;
		return aas_distance(from, &nearest);
	}
	return aas_distance(from, &reach->start);
}

/* the ways out of an area a bot may take at once (more, and the route's
   search is simply finished: aas_route_reach) */
#define AAS_WATCHED_REACHES 256

long aas_route_reach(long type, long area, real_point3d const *from, long goal_area, aas_avoid_function avoid, void *context, long slot, float *time_out)
{
	struct aas_world *world = &aas_worlds[type];
	struct aas_cache *cache;
	float const *time;
	long watched[AAS_WATCHED_REACHES];
	float offsets[AAS_WATCHED_REACHES];
	long watched_count = 0;
	long best = NONE;
	float best_time = 1.0e30f;
	long r;
	long i;
	real speed = aas_speed(type);

	if (time_out)
		*time_out = -1.f;
	if (!aas_is_ready || area == NONE || goal_area == NONE)
		return NONE;
	if (area == goal_area)
	{
		if (time_out)
			*time_out = aas_distance(from, &world->areas[goal_area].center) / speed;
		return NONE;
	}
	/* (another part of the map, joined to this one by nothing either way:
	   no way there, known at once, with no route begun to find it out by
	   going through all of the part the goal is in) */
	if (world->region && world->region[area] != world->region[goal_area])
		return NONE;
	cache = aas_route(type, goal_area, slot);
	if (!cache)
		return NONE;
	time = cache->time;
	if (world->areas[area].reach_count > AAS_WATCHED_REACHES)
	{
		/* (never, as the areas are made) the whole search, and every way out */
		if (!cache->complete)
			aas_route_advance(world, cache, NULL, NULL, 0);
		for (r = world->areas[area].first_reach; r < world->areas[area].first_reach + world->areas[area].reach_count; r++)
		{
			float t;
			if (time[r] >= 1.0e29f)
				continue;
			if (avoid && avoid(context, r))
				continue;
			t = aas_reach_entry_distance(from, &world->reaches[r]) / speed + time[r];
			if (t < best_time)
			{
				best_time = t;
				best = r;
			}
		}
	}
	else
	{
		/* the ways out it may take, and how long to each */
		for (r = world->areas[area].first_reach; r < world->areas[area].first_reach + world->areas[area].reach_count; r++)
		{
			if (avoid && avoid(context, r))
				continue;
			watched[watched_count] = r;
			offsets[watched_count] = aas_reach_entry_distance(from, &world->reaches[r]) / speed;
			watched_count++;
		}
		if (!cache->complete && watched_count > 0)
		{
			/* settled already? The quickest of the final ones, when it is
			   no more than the frontier: the others' times are more than
			   that before the way to them is added */
			for (i = 0; i < watched_count; i++)
			{
				float t;
				if (!(time[watched[i]] <= cache->frontier))
					continue;
				t = offsets[i] + time[watched[i]];
				if (t < best_time)
					best_time = t;
			}
			if (!(best_time <= cache->frontier))
				aas_route_advance(world, cache, watched, offsets, watched_count);
			best_time = 1.0e30f;
		}
		for (i = 0; i < watched_count; i++)
		{
			float t;
			if (time[watched[i]] >= 1.0e29f)
				continue;
			t = offsets[i] + time[watched[i]];
			if (t < best_time)
			{
				best_time = t;
				best = watched[i];
			}
		}
	}
	if (time_out && best != NONE)
		*time_out = best_time;
	if (aas_route_check())
	{
		static unsigned long checked, wrong;
		float const *whole = aas_route_check_times(type, cache);
		if (whole)
		{
			long expected = NONE;
			float expected_time = 1.0e30f;
			for (r = world->areas[area].first_reach; r < world->areas[area].first_reach + world->areas[area].reach_count; r++)
			{
				float t;
				if (whole[r] >= 1.0e29f)
					continue;
				if (avoid && avoid(context, r))
					continue;
				t = aas_reach_entry_distance(from, &world->reaches[r]) / speed + whole[r];
				if (t < expected_time)
				{
					expected_time = t;
					expected = r;
				}
			}
			checked++;
			if ((expected != best || (best != NONE && expected_time != best_time)) && wrong++ < 20)
				bot_log("aas: ROUTE CHECK: from area %ld to area %ld by reach %ld in %f, the whole search's %ld in %f (frontier %f complete %d)",
					area, goal_area, best, best_time, expected, expected_time, cache->frontier, (int)cache->complete);
			if (checked % 20000 == 0)
				bot_log("aas: route check: %lu routes checked, %lu wrong", checked, wrong);
			if (aas_route_checking > 1 && checked % 97 == 0 && world->reach_count > 0)
				aas_reach_failed(type, (long)(checked / 97 * 7919 % (unsigned long)MIN(world->reach_count, 400)));
		}
	}
	return best;
}

float aas_travel_time(long type, real_point3d const *from, real_point3d const *to)
{
	real radius = type == AAS_VEHICLE ? 6.f : 2.f;
	long a = aas_nearest_area(type, from, radius, NULL);
	long b = aas_nearest_area(type, to, radius, NULL);
	float time;
	if (a == NONE || b == NONE)
		return -1.f;
	if (a == b)
		return aas_distance(from, to) / aas_speed(type);
	if (aas_route_reach(type, a, from, b, NULL, NULL, NONE, &time) == NONE)
		return -1.f;
	return time;
}

/* the same for something only being weighed: with this tick's new routes
   spent and none to there yet, *soon is set and the time is as no way's
   (below 0): not known yet, to be asked again */
float aas_travel_time_soon(long type, real_point3d const *from, real_point3d const *to, boolean *soon)
{
	float time;
	aas_route_may_wait = TRUE;
	aas_route_waits = FALSE;
	time = aas_travel_time(type, from, to);
	aas_route_may_wait = FALSE;
	*soon = aas_route_waits;
	return aas_route_waits ? -1.f : time;
}

/* a gap's fall for a jump that misses it (0 for what is not a jumped gap) */
real aas_reach_fall(long type, long reach)
{
	struct aas_world const *world = &aas_worlds[type];
	return aas_is_ready && world->fall && reach >= 0 && reach < world->reach_count ? world->fall[reach] : 0.f;
}

void aas_reach_failed(long type, long reach)
{
	struct aas_world *world = &aas_worlds[type];
	long i;
	if (!aas_is_ready || reach < 0 || reach >= world->reach_count)
		return;
	if (world->failures[reach] < 255)
		world->failures[reach]++;
	/* (the routes still searching keep the penalties they began with) */
	if (!aas_shared_writable(&world->penalty))
		return;
	world->penalty[reach] = aas_failure_penalty(world->failures[reach]);
	if (world->failures[reach] == AAS_DEAD_FAILURES)
	{
		/* not a way at all: whatever's only got to by it is off the map as
		   far as goals go */
		aas_find_flags(type);
		aas_find_random_weights(type);
	}
	if (world->failures[reach] >= 1)
	{
		/* routes worked out before are redone as they're wanted (a few a tick) */
		world->penalty_epoch++;
		(void)i;
		if (bot_logging())
		{
			struct aas_reach const *r = &world->reaches[reach];
			bot_log("aas: %s reachability %ld (travel %d) at %.1f %.1f %.1f failed %d times: +%.0f s", type == AAS_VEHICLE ? "vehicle" : "foot",
				reach, r->travel, r->edge.x, r->edge.y, r->edge.z, world->failures[reach], world->penalty[reach]);
		}
	}
}

long aas_spawn_place_count(void)
{
	return aas_spawn_count;
}

long aas_place_count(void)
{
	return aas_places_count;
}

real_point3d const *aas_place(long index)
{
	return &aas_places[index];
}

long aas_random_area(long type, unsigned long seed)
{
	struct aas_world *world = &aas_worlds[type];
	float total;
	float pick;
	long low = 0, high;
	if (!aas_is_ready || world->area_count == 0)
		return NONE;
	total = world->random_weight[world->area_count - 1];
	if (total <= 0.f)
		return NONE;
	pick = (float)(seed % 1000003UL) / 1000003.f * total;
	high = world->area_count - 1;
	while (low < high)
	{
		long middle = (low + high) / 2;
		if (world->random_weight[middle] <= pick) /* (the first whose running total passes the pick: never one weighing nothing) */
			low = middle + 1;
		else
			high = middle;
	}
	return low;
}

void aas_set_area_costs(long type, long slot, float const *costs)
{
	struct aas_world *world = &aas_worlds[type];
	if (!aas_is_ready || slot < 0 || slot >= AAS_COST_SLOTS)
		return;
	if (!costs)
	{
		aas_shared_release(world->area_cost[slot]);
		world->area_cost[slot] = NULL;
		return;
	}
	if (!world->area_cost[slot])
	{
		world->area_cost[slot] = aas_shared_new(world->area_count, FALSE);
		if (!world->area_cost[slot])
			return;
	}
	else
	{
		/* much the same danger as before: the routes worked out stand */
		long a;
		float change = 0.f;
		for (a = 0; a < world->area_count && change < 0.5f; a++)
		{
			float d = costs[a] - world->area_cost[slot][a];
			if (d < 0.f)
				d = -d;
			if (d > change)
				change = d;
		}
		if (change < 0.5f)
			return;
		/* (the routes still searching keep the costs they began with) */
		if (!aas_shared_writable(&world->area_cost[slot]))
			return;
	}
	memcpy(world->area_cost[slot], costs, world->area_count * sizeof(float));
	world->cost_epoch[slot]++;
}

/* testing: HALO_BOT_SCAN=x,y,dx,dy,cells: the heightfield's floors along a line */
/* HALO_BOT_PROBE=x,y,z,x2,y2: what's in the way from one point to
   another, level, at heights 0.1..1.6 over z (debugging) */
static void aas_debug_probe(void)
{
	char const *text = getenv("HALO_BOT_PROBE");
	float x, y, z, x2, y2;
	long h;
	if (!text || sscanf(text, "%f,%f,%f,%f,%f", &x, &y, &z, &x2, &y2) != 5)
		return;
	for (h = 1; h <= 16; h++)
	{
		unsigned long const flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
			FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit);
		struct collision_result collision;
		real_point3d a;
		real_vector3d v;
		a.x = x, a.y = y, a.z = z + h * 0.1f;
		v.i = x2 - x, v.j = y2 - y, v.k = 0.f;
		if (collision_test_vector(flags, &a, &v, NONE, &collision))
			bot_log("probe h %.1f: hit %.2f %.2f %.2f type %d normal %.2f %.2f %.2f", h * 0.1f, collision.point.x, collision.point.y, collision.point.z,
				(int)collision.type, collision.plane.n.i, collision.plane.n.j, collision.plane.n.k);
		else
			bot_log("probe h %.1f: clear", h * 0.1f);
	}
}

/* HALO_BOT_PROBEDOWN=x,y,z: what's under a point (the map, an object) */
static void aas_debug_probe_down(void)
{
	char const *text = getenv("HALO_BOT_PROBEDOWN");
	float x, y, z;
	long pass;
	if (!text || sscanf(text, "%f,%f,%f", &x, &y, &z) != 3)
		return;
	for (pass = 0; pass < 3; pass++)
	{
		unsigned long flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
			(pass == 0 ? FLAG(_collision_test_structure_bit) : FLAG(_collision_test_objects_bit) |
				(pass == 1 ? FLAG(_collision_test_objects_scenery_bit) : FLAG(_collision_test_objects_machines_bit) |
					FLAG(_collision_test_objects_controls_bit) | FLAG(_collision_test_objects_light_fixtures_bit) | FLAG(_collision_test_objects_vehicles_bit)));
		struct collision_result collision;
		real_point3d a;
		real_vector3d v;
		a.x = x, a.y = y, a.z = z;
		v.i = 0.f, v.j = 0.f, v.k = -6.f;
		if (collision_test_vector(flags, &a, &v, NONE, &collision))
			bot_log("probedown %s: hit %.2f %.2f %.2f type %d normal %.2f %.2f %.2f", pass == 0 ? "structure" : pass == 1 ? "scenery" : "machines",
				collision.point.x, collision.point.y, collision.point.z, (int)collision.type, collision.plane.n.i, collision.plane.n.j, collision.plane.n.k);
		else
			bot_log("probedown %s: clear", pass == 0 ? "structure" : pass == 1 ? "scenery" : "machines");
	}
}

void aas_debug_scan(void)
{
	char const *text = getenv("HALO_BOT_SCAN");
	aas_debug_probe();
	aas_debug_probe_down();
	float x, y, dx, dy;
	long cells, k;
	if (!text || sscanf(text, "%f,%f,%f,%f,%ld", &x, &y, &dx, &dy, &cells) != 5)
		return;
	for (k = 0; k <= cells; k++)
	{
		real px = x + dx * k * NAVM_CELL, py = y + dy * k * NAVM_CELL;
		long column = navm_column_of(px, py);
		long u;
		char line[512];
		long used = 0;
		line[0] = 0;
		if (column == NONE)
			continue;
		for (u = navm_first_span(column); u != NONE && used < 440; u = navm.spans[u].next)
			used += snprintf(line + used, sizeof(line) - used, " [f %.2f c %.2f fl %d wall %d edge %d area %ld]", navm.spans[u].floor,
				navm.spans[u].ceiling, navm.spans[u].flags, navm.spans[u].wall, navm.spans[u].edge,
				aas_worlds[AAS_FOOT].span_area ? aas_worlds[AAS_FOOT].span_area[u] : -2L);
		bot_log("scan %.2f %.2f:%s", px, py, line);
	}
}
