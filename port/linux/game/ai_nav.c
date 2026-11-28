/*
AI_NAV.C

(port) A map's navigation for computer-controlled characters, beyond the
engine's own path searches: the knowledge of a level the multiplayer bots
were given (Chupathingy #1000), kept here so any AI can use it (Forge's
characters first; the bots' own searches can move onto it).

The engine's searches (source/ai/path.c) walk the level's walkable surfaces
across the edges they share, and nothing else: a ledge a player jumps onto,
a balcony he drops off, a railing he hops over are not ways to them, and
neither are the teleporters. A character whose goal was on ground its
search never reached walked as near as it came and on in a straight line,
into the wall between.

Here, once a map (a bounded step a tick, the same from run to run):

- The walkable copy: the level's walkable surfaces as path.c reads them,
  copied, less the surfaces under a machine that stands in the way (a door
  that never opens), less those with no room above them (under a ramp, in
  a wall's foot), plus the ramps a biped walks up that the AI's surfaces
  leave out. A second copy for vehicles. ai_nav_structure hands either to
  a path search (path_state's structure); the level itself is untouched.
- The regions: the walkable surfaces joined across their shared edges (a
  union of each with its walkable neighbours), so that whether a search can
  get from one surface to another is known before it is asked.
- The jumps: from each walkable surface's open edges (a wall's foot, a
  ledge's lip, a balcony's rail), a few points along each, the ground a few
  steps out beyond them looked for among the walkable surfaces (a grid of
  their extents; no rays): higher by no more than the biped jumps (its
  tag's jump velocity against the gravity, crouched in the air: the feet
  come up), lower by no more than it falls unharmed (the globals' falling
  damage), or level across a gap. One a place (three units) for each pair
  of regions and kind; those between regions first, then those within one
  (a shortcut). Each is tested with a few rays (room to rise at the edge,
  over the lip at the landing's height, nothing between the edge and the
  ground below, room to stand on the landing; a railing in the way a jump
  clears makes it a jump over the railing), and kept.
- The hops: the jumps, and the teleporters and ladders a user adds
  (ai_nav_add_teleporter); the way from one region to another is a search
  over them (Dijkstra's, straight lines between them): ai_nav_route gives
  the first hop to take, by the kinds of hops the caller takes.

A hop that fails a user (it does not come out where the hop goes) is left be
a while, and for the map after three failures with none taken.

Kept with no caller yet: ai_nav_route, ai_nav_reachable,
ai_nav_add_teleporter, ai_nav_hop_result, ai_nav_next_jump_from,
ai_nav_jump_height and ai_nav_report. Forge's characters (#1809) and the
bots' own searches are meant to move onto them; Forge uses
ai_nav_route_kinds today (forge_ai_move.inc).
*/

#include "halo_port_features.h"

#if HALO_AI_NAV

#include "cseries.h"
#include "game/game.h"
#include "game/game_globals.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "physics/physics.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collisions.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "devices/devices.h"
#include "devices/device_definitions.h"
#include "objects/object_types.h"

#include "ai_nav.h"
#include "ai/path.h"
#include "game/players.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);

/* ---------- constants */

/* a pathfinding surface's flag: walkable (path.c's) */
#define NAV_WALKABLE_BIT 6
/* the corners of a surface looked at (its ring of edges) */
#define NAV_CORNERS 16
/* the walkable surfaces' grid: cells this wide, hashed into these buckets */
#define NAV_CELL 2.0f
#define NAV_BUCKETS 16384
/* the work of a tick: surfaces joined, put in the grid, whose edges are
looked at; jumps tested (a few rays each) */
#define NAV_REGIONS_A_TICK 250
#define NAV_GRID_A_TICK 50
/* (and the work of a tick, whatever the surfaces: the grid's cells filled,
the edges' look-ups) */
#define NAV_GRID_WORK_A_TICK 300
#define NAV_EDGES_WORK_A_TICK 600
#define NAV_EDGES_A_TICK 30
#define NAV_TESTS_A_TICK 1
/* the jumps found, before they are tested: one a place this wide for each
two regions and kind */
#define NAV_CANDIDATES 3072
#define NAV_CANDIDATE_SLOTS 8192
#define NAV_PLACE 3.0f
/* a jump within one region is kept when it goes up or down this much at
least (else the walk is beside it) */
#define NAV_SAME_REGION_RISE 0.8f
/* the start regions whose reach is kept */
#define NAV_REACH_CACHE 16

enum
{
	_nav_phase_idle = 0,
	_nav_phase_regions,
	_nav_phase_grid,
	_nav_phase_edges,
	_nav_phase_tests,
	_nav_phase_done,
	_nav_phase_none,
};

struct nav_candidate
{
	long from_surface;
	long to_surface;
	long from_region;
	long to_region;
	real_point3d edge;
	real_vector3d out;
	real_point3d over;
	real_point3d landing;
	real distance;
	real rise;
	short kind;
};

/* ---------- globals */

static struct
{
	short phase;
	struct structure_bsp const *structure;
	long surface_count;
	long *region;
	long *parent;
	long region_count;
	long walkable_count;
	long largest_region;
	long largest_region_size;
	long cursor;
	/* the grid: a bucket's first entry, and the entries (a surface, the next) */
	long buckets[NAV_BUCKETS];
	long *entry_surface;
	long *entry_next;
	/* each surface's extent across (low x, low y, high x, high y), for the
	grid's look-ups to pass over those not under a point at once */
	real *extents;
	long entry_count;
	long entry_capacity;
	/* the jumps found, those between regions first: a slot's candidate */
	struct nav_candidate *candidates;
	long candidate_count;
	long *slots;
	long tested;
	long kept_by_kind[NUMBER_OF_AI_NAV_HOPS];
	long kept_joining;
	long rejected;
	long dropped;
	/* the hops (the teleporters and ladders added first), and by the region
	they are taken from: the first of each region's, in turn */
	struct ai_nav_hop hops[AI_NAV_MAXIMUM_HOPS];
	long hop_count;
	long hop_from_region[AI_NAV_MAXIMUM_HOPS];
	long hop_to_region[AI_NAV_MAXIMUM_HOPS];
	long *region_first_hop;
	long hop_order[AI_NAV_MAXIMUM_HOPS];
	boolean hops_indexed;
	/* the biped's jump */
	boolean biped_known;
	real jump_height;
	real crouch_gain;
	real safe_drop;
	real up_maximum;
	/* the least up a surface's normal is that the biped walks on (its tag's
	runtime minimum), and the surfaces so joined to the regions: a ramp the
	AI's walkable surfaces leave out (Prisoner's, Wizard's) */
	real minimum_normal_k;
	byte *ramp;
	long ramp_count;
	long ramp_cursor;
	long ramp_joins;
	long roof_cursor;
	long roofed_count;
	/* the regions a start region reaches (a bit each), the latest kept */
	struct
	{
		long start_region;
		long stamp;
		long hop_generation;
		unsigned long *bits;
	} reach[NAV_REACH_CACHE];
	long reach_stamp;
	long hop_generation;
	long started_tick;
	long made_tick;
	/* the work of the tick's step so far (NAV_*_WORK_A_TICK), and the
	surface's edge the edges' step goes on from */
	long work;
	long edge_cursor;

	/* debug.txt's line */
	long routes_asked;
	long routes_found;
	long routes_none;
	long hop_successes[NUMBER_OF_AI_NAV_HOPS];
	long hop_failures[NUMBER_OF_AI_NAV_HOPS];
} nav;

/* ---------- small helpers */

static real nav_distance2d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y;

	return (real)sqrt(dx * dx + dy * dy);
}

static real nav_distance3d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;

	return (real)sqrt(dx * dx + dy * dy + dz * dz);
}

static struct collision_bsp const *nav_bsp(void)
{
	if (!nav.structure || nav.structure->collision_bsp.count <= 0)
		return NULL;
	return TAG_BLOCK_GET_ELEMENT(&nav.structure->collision_bsp, 0, struct collision_bsp);
}

static boolean nav_walkable(long surface_index)
{
	byte const *flags = (byte const *)xbox_pointer(nav.structure->pathfinding_surfaces.address);

	return surface_index >= 0 && surface_index < nav.surface_count && TEST_FLAG(flags[surface_index], NAV_WALKABLE_BIT);
}

/* a surface's corners (its ring of edges) and the surface across each edge
(NONE for none); 0 for one not the bsp's */
static long nav_corners(struct collision_bsp const *bsp, long surface_index, real_point3d *corners, long *neighbours)
{
	struct collision_surface const *surface;
	long edge_index, count = 0;

	if (surface_index < 0 || surface_index >= bsp->surfaces.count)
		return 0;
	surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index, struct collision_surface);
	edge_index = surface->first_edge_index;
	do
	{
		struct collision_edge const *edge;
		boolean right;

		if (edge_index < 0 || edge_index >= bsp->edges.count)
			return 0;
		edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
		right = edge->surface_indices[1] == surface_index;
		if (!VALID_INDEX(edge->vertex_indices[right], bsp->vertices.count))
			return 0;
		corners[count] = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, edge->vertex_indices[right], struct collision_vertex)->point;
		if (neighbours)
			neighbours[count] = edge->surface_indices[!right];
		count++;
		edge_index = edge->edge_indices[right];
	} while (edge_index != surface->first_edge_index && count < NAV_CORNERS);
	return count;
}

/* the height of a surface (its corners' plane) at a point across, FALSE when
the point is not over it (or it stands on edge) */
static boolean nav_surface_height(real_point3d const *corners, long count, real x, real y, real *z)
{
	real_vector3d normal = { 0.0f, 0.0f, 0.0f };
	long index, previous;
	boolean inside = FALSE;

	for (index = 0, previous = count - 1; index < count; previous = index++)
	{
		real_point3d const *a = &corners[previous], *b = &corners[index];

		/* (Newell's normal) */
		normal.i += (a->y - b->y) * (a->z + b->z);
		normal.j += (a->z - b->z) * (a->x + b->x);
		normal.k += (a->x - b->x) * (a->y + b->y);
		if ((b->y > y) != (a->y > y) && x < (a->x - b->x) * (y - b->y) / (a->y - b->y) + b->x)
			inside = !inside;
	}
	if (!inside || fabs(normal.k) < 1e-5f)
		return FALSE;
	*z = corners[0].z - (normal.i * (x - corners[0].x) + normal.j * (y - corners[0].y)) / normal.k;
	return TRUE;
}

/* zeroed memory (the game's malloc, debug_memory.c's: its free takes no
other's - calloc is the C library's) */
static void *nav_zeroed(size_t count, size_t size)
{
	void *memory = malloc(count * size);

	if (memory)
		memset(memory, 0, count * size);
	return memory;
}

static unsigned long nav_bucket(long ix, long iy)
{
	return ((unsigned long)ix * 73856093UL ^ (unsigned long)iy * 19349663UL) & (NAV_BUCKETS - 1);
}

static long nav_cell(real value)
{
	return (long)floor(value / NAV_CELL);
}

/* ---------- the memory */

static void nav_free(void)
{
	long index;

	/* (the game's free, debug_memory.c's, takes no NULL) */
	if (nav.region)
		free(nav.region);
	if (nav.parent)
		free(nav.parent);
	if (nav.entry_surface)
		free(nav.entry_surface);
	if (nav.entry_next)
		free(nav.entry_next);
	if (nav.extents)
		free(nav.extents);
	if (nav.candidates)
		free(nav.candidates);
	if (nav.slots)
		free(nav.slots);
	if (nav.region_first_hop)
		free(nav.region_first_hop);
	if (nav.ramp)
		free(nav.ramp);
	for (index = 0; index < NAV_REACH_CACHE; index++)
	{
		if (nav.reach[index].bits)
			free(nav.reach[index].bits);
	}
	nav.region = NULL;
	nav.parent = NULL;
	nav.entry_surface = NULL;
	nav.entry_next = NULL;
	nav.extents = NULL;
	nav.candidates = NULL;
	nav.slots = NULL;
	nav.region_first_hop = NULL;
	nav.ramp = NULL;
	for (index = 0; index < NAV_REACH_CACHE; index++)
		nav.reach[index].bits = NULL;
}

void ai_nav_reset(void)
{
	nav_free();
	csmemset(&nav, 0, sizeof(nav));
	nav.phase = _nav_phase_idle;
}

void ai_nav_set_biped(long biped_definition_index)
{
	struct biped_definition const *definition;
	struct game_globals *globals = scenario_get_game_globals();
	real velocity, harmful = 3.0f;

	if (nav.biped_known || biped_definition_index == NONE)
		return;
	definition = biped_definition_get(biped_definition_index);
	velocity = definition->biped.jump_velocity;
	nav.jump_height = global_gravity > 0.0f ? velocity * velocity / (2.0f * global_gravity) : 0.0f;
	nav.crouch_gain = MAX(0.0f, definition->biped.collision_height_standing - definition->biped.collision_height_crouching);
	/* (the globals' falling damage: its distance's lower bound, a mirror of
	bipeds.c's block, whose first two words are unused) */
	if (globals && globals->falling_damage.count > 0)
	{
		real const *bounds = (real const *)((byte const *)TAG_BLOCK_GET_ELEMENT(&globals->falling_damage, 0, byte) + 8);

		if (bounds[0] > 0.5f && bounds[0] < 20.0f)
			harmful = bounds[0];
	}
	nav.safe_drop = harmful * 0.9f;
	/* (a jump up kept a little short of the highest: the bot leaves the edge
	running, and lands with its feet tucked) */
	nav.up_maximum = PIN(nav.jump_height * 0.95f + nav.crouch_gain * 0.6f, 0.3f, 2.0f);
	nav.minimum_normal_k = definition->biped.runtime_minimum_normal_k > 0.1f ? definition->biped.runtime_minimum_normal_k :
		(real)cos(definition->biped.maximum_slope_angle);
	if (nav.minimum_normal_k < 0.3f || nav.minimum_normal_k > 0.99f)
		nav.minimum_normal_k = 0.7f;
	nav.biped_known = TRUE;
	platform_log("ai nav: the biped jumps %.2f (velocity %.3f, gravity %.5f), crouched %.2f higher, falls %.2f unharmed; "
		"jumps up %.2f at most, drops %.2f; walks up slopes of normal %.2f", nav.jump_height, velocity, global_gravity,
		nav.crouch_gain, harmful, nav.up_maximum, nav.safe_drop, nav.minimum_normal_k);
}

real ai_nav_jump_height(void)
{
	return nav.biped_known ? nav.jump_height : 0.6f;
}

/* ---------- the regions */

static long nav_find(long surface_index)
{
	while (nav.parent[surface_index] != surface_index)
	{
		nav.parent[surface_index] = nav.parent[nav.parent[surface_index]];
		surface_index = nav.parent[surface_index];
	}
	return surface_index;
}

static boolean nav_begin(void)
{
	struct structure_bsp const *structure = global_structure_bsp_get();
	struct collision_bsp const *bsp;
	long index;

	if (!structure || structure->collision_bsp.count <= 0 || structure->pathfinding_surfaces.count <= 0)
		return FALSE;
	nav.structure = structure;
	bsp = nav_bsp();
	nav.surface_count = MIN((long)bsp->surfaces.count, (long)structure->pathfinding_surfaces.count);
	if (nav.surface_count <= 0)
		return FALSE;
	nav.region = (long *)malloc(sizeof(long) * (size_t)nav.surface_count);
	nav.parent = (long *)malloc(sizeof(long) * (size_t)nav.surface_count);
	nav.ramp = (byte *)nav_zeroed((size_t)nav.surface_count, 1);
	/* (the grid's and the jumps' memory taken as their steps begin, each a
	tick of its own: all at once was the making's longest tick) */
	if (!nav.region || !nav.parent || !nav.ramp)
	{
		platform_log("ai nav: no memory for %ld surfaces", nav.surface_count);
		nav_free();
		return FALSE;
	}
	for (index = 0; index < nav.surface_count; index++)
	{
		nav.parent[index] = index;
		nav.region[index] = NONE;
	}
	nav.cursor = 0;
	return TRUE;
}

/* the grid's memory (its first step), the jumps' (theirs): FALSE for none */
static boolean nav_grid_begin(void)
{
	long index;

	nav.entry_capacity = nav.surface_count * 10 + 64;
	nav.entry_surface = (long *)malloc(sizeof(long) * (size_t)nav.entry_capacity);
	nav.entry_next = (long *)malloc(sizeof(long) * (size_t)nav.entry_capacity);
	nav.extents = (real *)malloc(sizeof(real) * 4 * (size_t)nav.surface_count);
	if (!nav.entry_surface || !nav.entry_next || !nav.extents)
		return FALSE;
	for (index = 0; index < NAV_BUCKETS; index++)
		nav.buckets[index] = NONE;
	return TRUE;
}

static boolean nav_edges_begin(void)
{
	long index;

	nav.candidates = (struct nav_candidate *)malloc(sizeof(struct nav_candidate) * NAV_CANDIDATES);
	nav.slots = (long *)malloc(sizeof(long) * NAV_CANDIDATE_SLOTS);
	if (!nav.candidates || !nav.slots)
		return FALSE;
	for (index = 0; index < NAV_CANDIDATE_SLOTS; index++)
		nav.slots[index] = NONE;
	return TRUE;
}

/* whether a surface is one the biped walks on though not one of the AI's
walkable ones: facing up, no steeper than the biped climbs (a ramp) */
static boolean nav_ramp_surface(struct collision_bsp const *bsp, long surface_index)
{
	real_point3d corners[NAV_CORNERS];
	real_vector3d normal = { 0.0f, 0.0f, 0.0f };
	long count, index, previous;
	real length;

	if (surface_index < 0 || surface_index >= nav.surface_count || nav_walkable(surface_index))
		return FALSE;
	count = nav_corners(bsp, surface_index, corners, NULL);
	if (count < 3)
		return FALSE;
	for (index = 0, previous = count - 1; index < count; previous = index++)
	{
		real_point3d const *a = &corners[previous], *b = &corners[index];

		normal.i += (a->y - b->y) * (a->z + b->z);
		normal.j += (a->z - b->z) * (a->x + b->x);
		normal.k += (a->x - b->x) * (a->y + b->y);
	}
	length = (real)sqrt(normal.i * normal.i + normal.j * normal.j + normal.k * normal.k);
	/* (the ring's turn: a surface's corners go round it one way, seen from
	its front; up-facing either way taken, the steepness the measure) */
	return length > 1e-4f && (real)fabs(normal.k) / length >= nav.minimum_normal_k;
}

/* a surface the regions take: walkable or a ramp, with room above it (the
level half a biped's height over its middle closes it: a floor going on
under a wall, which joined rooms the wall parts - Chiron TL34's) */
static boolean nav_standable(long surface_index)
{
	return surface_index >= 0 && surface_index < nav.surface_count && nav.ramp[surface_index] != 2 &&
		(nav.ramp[surface_index] == 1 || nav_walkable(surface_index));
}

#define NAV_ROOFS_A_TICK 16
static void nav_roof_test(struct collision_bsp const *bsp, long surface_index)
{
	real_point3d corners[NAV_CORNERS], middle = { 0.0f, 0.0f, 0.0f };
	real_vector3d up = { 0.0f, 0.0f, 0.35f };
	struct collision_result collision;
	long count, corner;

	count = nav_corners(bsp, surface_index, corners, NULL);
	if (count < 3)
		return;
	for (corner = 0; corner < count; corner++)
	{
		middle.x += corners[corner].x / (real)count;
		middle.y += corners[corner].y / (real)count;
		middle.z += corners[corner].z / (real)count;
	}
	middle.z += 0.05f;
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_back_facing_surfaces_bit), &middle, &up, NONE, &collision))
	{
		nav.ramp[surface_index] = 2;
		nav.roofed_count++;
	}
}

/* a step of the unions: each walkable surface with its walkable neighbours,
and a ramp with its walkable or ramp neighbours; then the regions numbered
(the walkable surfaces'), their sizes */
static boolean nav_regions_step(void)
{
	struct collision_bsp const *bsp = nav_bsp();
	long last = MIN(nav.cursor + NAV_REGIONS_A_TICK, nav.surface_count);

	/* (the ramps first, a step a tick: what joins a surface's neighbours
	needs them all) */
	if (nav.ramp_cursor < nav.surface_count)
	{
		long end = MIN(nav.ramp_cursor + NAV_REGIONS_A_TICK, nav.surface_count);

		for (; nav.ramp_cursor < end; nav.ramp_cursor++)
		{
			if (nav_ramp_surface(bsp, nav.ramp_cursor))
			{
				nav.ramp[nav.ramp_cursor] = 1;
				nav.ramp_count++;
			}
		}
		return FALSE;
	}
	/* (then the room above each, a step a tick: a ray each) */
	if (nav.roof_cursor < nav.surface_count)
	{
		long end = nav.roof_cursor, tested = 0;

		for (; nav.roof_cursor < nav.surface_count && tested < NAV_ROOFS_A_TICK; nav.roof_cursor++)
		{
			if (nav.ramp[nav.roof_cursor] == 1 || nav_walkable(nav.roof_cursor))
			{
				nav_roof_test(bsp, nav.roof_cursor);
				tested++;
			}
		}
		(void)end;
		return FALSE;
	}

	for (; nav.cursor < last; nav.cursor++)
	{
		real_point3d corners[NAV_CORNERS];
		long neighbours[NAV_CORNERS];
		long count, corner;

		if (!nav_standable(nav.cursor))
			continue;
		count = nav_corners(bsp, nav.cursor, corners, neighbours);
		for (corner = 0; corner < count; corner++)
		{
			if (nav_standable(neighbours[corner]))
			{
				long a = nav_find(nav.cursor), b = nav_find(neighbours[corner]);

				if (a != b)
				{
					nav.parent[MAX(a, b)] = MIN(a, b);
					if (nav.ramp[nav.cursor] || nav.ramp[neighbours[corner]])
						nav.ramp_joins++;
				}
			}
		}
	}
	if (nav.cursor < nav.surface_count)
		return FALSE;
	{
		long index;
		long *sizes;

		nav.region_count = 0;
		nav.walkable_count = 0;
		for (index = 0; index < nav.surface_count; index++)
		{
			long root;

			if (!nav_walkable(index) || nav.ramp[index] == 2)
				continue;
			nav.walkable_count++;
			root = nav_find(index);
			if (nav.region[root] == NONE)
				nav.region[root] = nav.region_count++;
			nav.region[index] = nav.region[root];
		}
		/* (a ramp joined to walkable ground is in its region: a bot stands
		on one, a goal lies on one; one alone, in none) */
		for (index = 0; index < nav.surface_count; index++)
		{
			long root;

			if (nav.ramp[index] != 1)
				continue;
			root = nav_find(index);
			nav.region[index] = nav.region[root];
		}
		sizes = (long *)nav_zeroed((size_t)MAX(nav.region_count, 1), sizeof(long));
		if (sizes)
		{
			for (index = 0; index < nav.surface_count; index++)
			{
				if (nav.region[index] != NONE)
					sizes[nav.region[index]]++;
			}
			for (index = 0; index < nav.region_count; index++)
			{
				if (sizes[index] > nav.largest_region_size)
				{
					nav.largest_region_size = sizes[index];
					nav.largest_region = index;
				}
			}
			/* (debug: HALO_MPBOT_NAV_LOG, each region's size and extent) */
			if (getenv("HALO_MPBOT_NAV_LOG"))
			{
				long region;

				for (region = 0; region < nav.region_count; region++)
				{
					real_point3d low = { REAL_MAX, REAL_MAX, REAL_MAX }, high = { -REAL_MAX, -REAL_MAX, -REAL_MAX };

					for (index = 0; index < nav.surface_count; index++)
					{
						real_point3d corners[NAV_CORNERS];
						long count, corner;

						if (nav.region[index] != region)
							continue;
						count = nav_corners(bsp, index, corners, NULL);
						for (corner = 0; corner < count; corner++)
						{
							low.x = MIN(low.x, corners[corner].x);
							low.y = MIN(low.y, corners[corner].y);
							low.z = MIN(low.z, corners[corner].z);
							high.x = MAX(high.x, corners[corner].x);
							high.y = MAX(high.y, corners[corner].y);
							high.z = MAX(high.z, corners[corner].z);
						}
					}
					platform_log("ai nav: region %ld: %ld surfaces, (%.1f %.1f %.1f) to (%.1f %.1f %.1f)", region, sizes[region],
						low.x, low.y, low.z, high.x, high.y, high.z);
				}
			}
			free(sizes);
		}
	}
	free(nav.parent);
	nav.parent = NULL;
	nav.cursor = 0;
	return TRUE;
}

/* ---------- the grid */

static boolean nav_grid_step(void)
{
	struct collision_bsp const *bsp = nav_bsp();
	long last = MIN(nav.cursor + NAV_GRID_A_TICK, nav.surface_count);

	for (; nav.cursor < last && nav.work < NAV_GRID_WORK_A_TICK; nav.cursor++)
	{
		real_point3d corners[NAV_CORNERS];
		long count, corner, ix, iy;
		real low_x = REAL_MAX, low_y = REAL_MAX, high_x = -REAL_MAX, high_y = -REAL_MAX;

		if (nav.region[nav.cursor] == NONE)
			continue;
		count = nav_corners(bsp, nav.cursor, corners, NULL);
		if (count < 3)
			continue;
		for (corner = 0; corner < count; corner++)
		{
			low_x = MIN(low_x, corners[corner].x);
			low_y = MIN(low_y, corners[corner].y);
			high_x = MAX(high_x, corners[corner].x);
			high_y = MAX(high_y, corners[corner].y);
		}
		nav.extents[nav.cursor * 4 + 0] = low_x;
		nav.extents[nav.cursor * 4 + 1] = low_y;
		nav.extents[nav.cursor * 4 + 2] = high_x;
		nav.extents[nav.cursor * 4 + 3] = high_y;
		/* (a surface over a great many cells - a level's whole floor - in
		its cells all the same: at most 32 by 32) */
		for (ix = nav_cell(low_x); ix <= nav_cell(high_x) && ix - nav_cell(low_x) < 32; ix++)
		{
			for (iy = nav_cell(low_y); iy <= nav_cell(high_y) && iy - nav_cell(low_y) < 32; iy++)
			{
				unsigned long bucket = nav_bucket(ix, iy);

				if (nav.entry_count >= nav.entry_capacity)
				{
					long capacity = nav.entry_capacity * 2;
					long *surfaces = (long *)realloc(nav.entry_surface, sizeof(long) * (size_t)capacity);
					long *nexts = surfaces ? (long *)realloc(nav.entry_next, sizeof(long) * (size_t)capacity) : NULL;

					if (surfaces)
						nav.entry_surface = surfaces;
					if (!surfaces || !nexts)
						return TRUE;
					nav.entry_next = nexts;
					nav.entry_capacity = capacity;
				}
				nav.work++;
				nav.entry_surface[nav.entry_count] = nav.cursor;
				nav.entry_next[nav.entry_count] = nav.buckets[bucket];
				nav.buckets[bucket] = nav.entry_count++;
			}
		}
	}
	if (nav.cursor < nav.surface_count)
		return FALSE;
	nav.cursor = 0;
	return TRUE;
}

/* the walkable surface under a point across nearest its height (within
`above` over it and `below` under it), NONE for none, its height in *z */
static long nav_ground_at(real x, real y, real z, real above, real below, long except, real *ground_z)
{
	struct collision_bsp const *bsp = nav_bsp();
	unsigned long bucket = nav_bucket(nav_cell(x), nav_cell(y));
	long entry, best = NONE;
	real best_off = REAL_MAX;

	for (entry = nav.buckets[bucket]; entry != NONE; entry = nav.entry_next[entry])
	{
		long surface_index = nav.entry_surface[entry];

		nav.work++;
		real_point3d corners[NAV_CORNERS];
		long count;
		real height;

		if (surface_index == except || x < nav.extents[surface_index * 4 + 0] || y < nav.extents[surface_index * 4 + 1] ||
			x > nav.extents[surface_index * 4 + 2] || y > nav.extents[surface_index * 4 + 3])
		{
			continue;
		}
		count = nav_corners(bsp, surface_index, corners, NULL);
		if (count < 3 || !nav_surface_height(corners, count, x, y, &height))
			continue;
		if (height > z + above || height < z - below)
			continue;
		/* (the highest under the reach above, as a biped lands on the first
		it comes down onto: the nearest above or below, the higher first) */
		if (fabs(height - z) < best_off)
		{
			best_off = (real)fabs(height - z);
			best = surface_index;
			*ground_z = height;
		}
	}
	return best;
}

/* ---------- the jumps found */

static void nav_candidate_add(struct nav_candidate const *candidate)
{
	unsigned long key = (unsigned long)candidate->from_region * 2654435761UL ^ (unsigned long)candidate->to_region * 40503UL ^
		(unsigned long)(long)floor(candidate->edge.x / NAV_PLACE) * 73856093UL ^
		(unsigned long)(long)floor(candidate->edge.y / NAV_PLACE) * 19349663UL ^ (unsigned long)candidate->kind * 83492791UL;
	long slot = (long)(key & (NAV_CANDIDATE_SLOTS - 1)), probes;

	for (probes = 0; probes < 32; probes++, slot = (slot + 1) & (NAV_CANDIDATE_SLOTS - 1))
	{
		struct nav_candidate *other;

		if (nav.slots[slot] == NONE)
		{
			if (nav.candidate_count >= NAV_CANDIDATES)
			{
				nav.dropped++;
				return;
			}
			nav.candidates[nav.candidate_count] = *candidate;
			nav.slots[slot] = nav.candidate_count++;
			return;
		}
		other = &nav.candidates[nav.slots[slot]];
		if (other->from_region == candidate->from_region && other->to_region == candidate->to_region &&
			other->kind == candidate->kind && (long)floor(other->edge.x / NAV_PLACE) == (long)floor(candidate->edge.x / NAV_PLACE) &&
			(long)floor(other->edge.y / NAV_PLACE) == (long)floor(candidate->edge.y / NAV_PLACE))
		{
			/* (the shorter way across kept) */
			if (candidate->distance < other->distance)
				*other = *candidate;
			return;
		}
	}
	nav.dropped++;
}

/* a step of the surfaces' open edges: the ground beyond each looked for */
static boolean nav_edges_step(void)
{
	static real const steps[] = { 0.45f, 0.9f, 1.5f, 2.1f };
	struct collision_bsp const *bsp = nav_bsp();
	long last = MIN(nav.cursor + NAV_EDGES_A_TICK, nav.surface_count);

	if (nav.work >= NAV_EDGES_WORK_A_TICK)
		return FALSE;

	for (; nav.cursor < last && nav.work < NAV_EDGES_WORK_A_TICK; nav.cursor++, nav.edge_cursor = 0)
	{
		real_point3d corners[NAV_CORNERS], middle = { 0.0f, 0.0f, 0.0f };
		long neighbours[NAV_CORNERS];
		long count, corner, from_region = nav.region[nav.cursor];

		if (from_region == NONE)
			continue;
		count = nav_corners(bsp, nav.cursor, corners, neighbours);
		if (count < 3)
			continue;
		for (corner = 0; corner < count; corner++)
		{
			middle.x += corners[corner].x / (real)count;
			middle.y += corners[corner].y / (real)count;
			middle.z += corners[corner].z / (real)count;
		}
		/* (an edge at a time, the tick's work allowing: a surface of many
		long open edges over a crowded grid, Sidewinder's, took a ms) */
		for (corner = nav.edge_cursor; corner < count; corner++)
		{
			real_point3d const *a = &corners[corner], *b = &corners[(corner + 1) % count];
			real dx = b->x - a->x, dy = b->y - a->y, length = (real)sqrt(dx * dx + dy * dy);
			real_vector3d out;
			long samples, sample;

			if (nav.work >= NAV_EDGES_WORK_A_TICK)
			{
				nav.edge_cursor = corner;
				return FALSE;
			}

			/* (an edge to another walkable surface, or a ramp, is walked
			across) */
			if (nav_standable(neighbours[corner]) || length < 0.3f)
				continue;
			out.i = dy / length;
			out.j = -dx / length;
			out.k = 0.0f;
			if ((middle.x - a->x) * out.i + (middle.y - a->y) * out.j > 0.0f)
			{
				out.i = -out.i;
				out.j = -out.j;
			}
			samples = PIN((long)(length / 1.5f) + 1, 1, 4);
			for (sample = 0; sample < samples; sample++)
			{
				real along = ((real)sample + 0.5f) / (real)samples;
				struct nav_candidate candidate;
				long step;

				candidate.edge.x = a->x + (b->x - a->x) * along;
				candidate.edge.y = a->y + (b->y - a->y) * along;
				candidate.edge.z = a->z + (b->z - a->z) * along;
				for (step = 0; step < (long)NUMBEROF(steps); step++)
				{
					real x = candidate.edge.x + out.i * steps[step], y = candidate.edge.y + out.j * steps[step], z;
					long surface_index = nav_ground_at(x, y, candidate.edge.z, nav.up_maximum, nav.safe_drop, nav.cursor, &z);
					long to_region;
					real rise;

					if (surface_index == NONE)
						continue;
					to_region = nav.region[surface_index];
					rise = z - candidate.edge.z;
					/* (ground beside the edge, a step's height: walked onto
					already, or the wall's foot's own floor) */
					if (to_region == from_region && fabs(rise) < NAV_SAME_REGION_RISE)
						break;
					/* (up and across far: none; a level gap a short way only) */
					if ((rise > 0.25f && steps[step] > 1.6f) || (fabs(rise) <= 0.25f && steps[step] > 1.6f))
						break;
					candidate.from_surface = nav.cursor;
					candidate.to_surface = surface_index;
					candidate.from_region = from_region;
					candidate.to_region = to_region;
					candidate.out = out;
					candidate.over.x = x;
					candidate.over.y = y;
					candidate.over.z = z;
					candidate.landing = candidate.over;
					candidate.distance = steps[step];
					candidate.rise = rise;
					candidate.kind = rise > 0.25f ? _ai_nav_hop_jump_up : rise < -0.5f ? _ai_nav_hop_drop : _ai_nav_hop_gap;
					/* (the landing a little further on, when that is on it too) */
					{
						real further_z;
						long further = nav_ground_at(x + out.i * 0.35f, y + out.j * 0.35f, z, 0.3f, 0.3f, NONE, &further_z);

						if (further == surface_index || (further != NONE && nav.region[further] == to_region))
						{
							candidate.landing.x = x + out.i * 0.35f;
							candidate.landing.y = y + out.j * 0.35f;
							candidate.landing.z = further_z;
						}
					}
					nav_candidate_add(&candidate);
					break;
				}
			}
		}
	}
	if (nav.cursor < nav.surface_count)
		return FALSE;
	nav.cursor = 0;
	return TRUE;
}

/* ---------- the jumps tested */

static boolean nav_ray(real_point3d const *from, real_point3d const *to)
{
	struct collision_result collision;
	real_vector3d vector = { to->x - from->x, to->y - from->y, to->z - from->z };

	return !collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_back_facing_surfaces_bit) | FLAG(_collision_test_objects_bit) |
		FLAG(_collision_test_objects_scenery_bit) | FLAG(_collision_test_objects_machines_bit), from, &vector, NONE, &collision);
}

/* the ground under a point (rays: the level alone), its walkable region,
NONE for none */
static long nav_region_below(real_point3d const *point, real depth, real *z)
{
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d down = { 0.0f, 0.0f, -depth };

	start.z += 0.3f;
	down.k -= 0.3f;
	if (!collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		&start, &down, NONE, &collision) || collision.type != _collision_result_structure)
	{
		return NONE;
	}
	*z = collision.point.z;
	return collision.surface_index >= 0 && collision.surface_index < nav.surface_count ? nav.region[collision.surface_index] : NONE;
}

/* a jump's rays: where it is taken from (on the ground of its region, back
from the edge), room to rise there, over the lip at the landing's height (a
railing in the way of a walk off it, cleared by a jump), nothing between
the edge and the ground below it, room to stand at the landing; the hop
made of it. FALSE for none */
static boolean nav_candidate_test(struct nav_candidate const *candidate, struct ai_nav_hop *hop)
{
	real_point3d takeoff, a, b;
	real z, top;
	boolean needs_jump = FALSE;
	long region;

	takeoff.x = candidate->edge.x - candidate->out.i * 0.35f;
	takeoff.y = candidate->edge.y - candidate->out.j * 0.35f;
	takeoff.z = candidate->edge.z;
	region = nav_region_below(&takeoff, 0.6f, &z);
	if (region != candidate->from_region || fabs(z - candidate->edge.z) > 0.35f)
	{
		takeoff.x = candidate->edge.x - candidate->out.i * 0.15f;
		takeoff.y = candidate->edge.y - candidate->out.j * 0.15f;
		region = nav_region_below(&takeoff, 0.6f, &z);
		if (region != candidate->from_region || fabs(z - candidate->edge.z) > 0.35f)
			return FALSE;
	}
	takeoff.z = z;
	/* (the landing's ground, and room to stand there: a biped's height) */
	region = nav_region_below(&candidate->landing, 0.6f, &z);
	if (region != candidate->to_region || fabs(z - candidate->landing.z) > 0.35f)
		return FALSE;
	a = candidate->landing;
	a.z = z + 0.1f;
	b = a;
	b.z += 0.65f;
	if (!nav_ray(&a, &b))
		return FALSE;
	if (candidate->kind == _ai_nav_hop_jump_up)
	{
		/* (room to rise at the edge, up to over the landing's floor with
		the body; then across at a knee's height over it) */
		top = candidate->landing.z + 0.75f;
		a = takeoff;
		a.z += 0.3f;
		b = takeoff;
		b.z = top;
		if (!nav_ray(&a, &b))
			return FALSE;
		a = takeoff;
		a.z = candidate->landing.z + 0.3f;
		b = candidate->landing;
		b.z += 0.3f;
		if (!nav_ray(&a, &b))
			return FALSE;
		needs_jump = TRUE;
	}
	else
	{
		/* (off the edge at a knee's height; else over a railing a jump
		clears) */
		a = takeoff;
		a.z += 0.3f;
		b = candidate->over;
		b.z = takeoff.z + 0.3f;
		if (!nav_ray(&a, &b))
		{
			a.z = takeoff.z + MIN(nav.jump_height * 0.8f, 0.8f);
			b.z = a.z;
			if (!nav_ray(&a, &b))
				return FALSE;
			/* (the fall after a hop is the higher: unharmed still) */
			if (-candidate->rise + nav.jump_height > nav.safe_drop)
				return FALSE;
			needs_jump = TRUE;
		}
		/* (down to the ground beyond: nothing between) */
		a = b;
		b = candidate->landing;
		b.z += 0.15f;
		if (candidate->rise < -0.3f && !nav_ray(&a, &b))
			return FALSE;
		if (candidate->kind == _ai_nav_hop_gap && candidate->distance > 0.8f)
			needs_jump = TRUE;
	}
	csmemset(hop, 0, sizeof(*hop));
	hop->kind = candidate->kind;
	hop->needs_jump = needs_jump;
	hop->teleporter_index = NONE;
	hop->entrance = takeoff;
	hop->entrance_surface_index = candidate->from_surface;
	hop->exit = candidate->landing;
	hop->exit_surface_index = candidate->to_surface;
	hop->edge = candidate->edge;
	hop->cost = nav_distance3d(&takeoff, &candidate->landing) + 1.0f;
	return TRUE;
}

static boolean nav_tests_step(void)
{
	long tests;

	for (tests = 0; tests < NAV_TESTS_A_TICK && nav.cursor < nav.candidate_count * 2; nav.cursor++)
	{
		/* (those between regions first, those within one after) */
		long index = nav.cursor % nav.candidate_count;
		boolean joining_pass = nav.cursor < nav.candidate_count;
		struct nav_candidate const *candidate = &nav.candidates[index];
		struct ai_nav_hop hop;

		if ((candidate->from_region != candidate->to_region) != joining_pass)
			continue;
		tests++;
		nav.tested++;
		if (nav.hop_count >= AI_NAV_MAXIMUM_HOPS)
		{
			nav.dropped++;
			continue;
		}
		if (!nav_candidate_test(candidate, &hop))
		{
			nav.rejected++;
			continue;
		}
		nav.hops[nav.hop_count++] = hop;
		nav.kept_by_kind[hop.kind]++;
		if (candidate->from_region != candidate->to_region)
			nav.kept_joining++;
		/* (debug: HALO_MPBOT_NAV_LOG, each jump kept) */
		if (getenv("HALO_MPBOT_NAV_LOG"))
			platform_log("ai nav: hop %ld %s%s from (%.2f %.2f %.2f) r%ld to (%.2f %.2f %.2f) r%ld, %.2f across, %+.2f",
				nav.hop_count, hop.kind == _ai_nav_hop_jump_up ? "up" : hop.kind == _ai_nav_hop_drop ? "drop" : "gap",
				hop.needs_jump ? " (jumped)" : "", hop.entrance.x, hop.entrance.y, hop.entrance.z, candidate->from_region,
				hop.exit.x, hop.exit.y, hop.exit.z, candidate->to_region, candidate->distance, candidate->rise);
	}
	return nav.cursor >= nav.candidate_count * 2;
}

/* ---------- the hops by region */

static void nav_index_hops(void)
{
	long index, region;
	long *counts;

	if (nav.region_first_hop)
		free(nav.region_first_hop);
	nav.region_first_hop = (long *)nav_zeroed((size_t)nav.region_count + 2, sizeof(long));
	if (!nav.region_first_hop)
		return;
	counts = nav.region_first_hop;
	for (index = 0; index < nav.hop_count; index++)
	{
		nav.hop_from_region[index] = ai_nav_region(nav.hops[index].entrance_surface_index);
		nav.hop_to_region[index] = ai_nav_region(nav.hops[index].exit_surface_index);
		if (nav.hop_from_region[index] != NONE)
			counts[nav.hop_from_region[index] + 1]++;
	}
	for (region = 0; region < nav.region_count; region++)
		counts[region + 1] += counts[region];
	/* (a counting sort: each region's hops after the earlier regions') */
	{
		long *next = (long *)nav_zeroed((size_t)nav.region_count + 1, sizeof(long));

		if (!next)
			return;
		for (region = 0; region < nav.region_count; region++)
			next[region] = counts[region];
		for (index = 0; index < nav.hop_count; index++)
		{
			long from = nav.hop_from_region[index];

			if (from != NONE)
				nav.hop_order[next[from]++] = index;
		}
		free(next);
	}
	nav.hops_indexed = TRUE;
	nav.hop_generation++;
}

/* ---------- the tick's step */

void ai_nav_update(void)
{
	long now = game_time_get();

	nav.work = 0;
	switch (nav.phase)
	{
	case _nav_phase_idle:
		nav.started_tick = now;
		if (!nav_begin())
		{
			nav.phase = _nav_phase_none;
			platform_log("ai nav: the level has no walkable surfaces");
			return;
		}
		nav.phase = _nav_phase_regions;
		break;
	case _nav_phase_regions:
		if (nav.biped_known && nav_regions_step())
		{
			nav.phase = _nav_phase_grid;
			/* (the teleporters and ladders join regions already) */
			nav_index_hops();
		}
		break;
	case _nav_phase_grid:
		if (!nav.entry_surface)
		{
			if (!nav_grid_begin())
			{
				platform_log("ai nav: no memory for the grid");
				nav.phase = _nav_phase_none;
			}
			break;
		}
		if (nav_grid_step())
			nav.phase = _nav_phase_edges;
		break;
	case _nav_phase_edges:
		if (!nav.candidates)
		{
			if (!nav_edges_begin())
			{
				platform_log("ai nav: no memory for the jumps");
				nav.phase = _nav_phase_none;
			}
			break;
		}
		/* (the jumps wait for the biped's) */
		if (nav.biped_known && nav_edges_step())
			nav.phase = _nav_phase_tests;
		break;
	case _nav_phase_tests:
		if (nav.candidate_count <= 0 || nav_tests_step())
		{
			nav.phase = _nav_phase_done;
			nav.made_tick = now;
			nav_index_hops();
			/* (the memory of the making let go) */
			free(nav.candidates);
			free(nav.slots);
			free(nav.entry_surface);
			free(nav.entry_next);
			free(nav.extents);
			nav.extents = NULL;
			nav.candidates = NULL;
			nav.slots = NULL;
			nav.entry_surface = NULL;
			nav.entry_next = NULL;
			platform_log("ai nav: %ld walkable surfaces in %ld regions (the largest %ld surfaces; %ld ramps, %ld joins, %ld with no room above); jumps found %ld (%ld more "
				"dropped), tested %ld, kept %ld (up %ld, drops %ld, gaps %ld; %ld joining regions), %ld rejected; %ld hops with "
				"the teleporters and ladders; made in %ld ticks", nav.walkable_count, nav.region_count, nav.largest_region_size,
				MAX(nav.ramp_count, 0), nav.ramp_joins, nav.roofed_count, nav.candidate_count, nav.dropped, nav.tested,
				nav.kept_by_kind[_ai_nav_hop_jump_up] + nav.kept_by_kind[_ai_nav_hop_drop] + nav.kept_by_kind[_ai_nav_hop_gap],
				nav.kept_by_kind[_ai_nav_hop_jump_up], nav.kept_by_kind[_ai_nav_hop_drop], nav.kept_by_kind[_ai_nav_hop_gap],
				nav.kept_joining, nav.rejected, nav.hop_count, now - nav.started_tick);
		}
		break;
	default:
		break;
	}
}

short ai_nav_phase(void)
{
	return nav.phase;
}

boolean ai_nav_ready(void)
{
	return nav.phase >= _nav_phase_grid && nav.phase != _nav_phase_none && nav.region;
}

void ai_nav_add_teleporter(long teleporter_index, boolean ladder, real_point3d const *entrance,
	long entrance_surface_index, real_point3d const *exit, long exit_surface_index)
{
	struct ai_nav_hop *hop;

	if (nav.hop_count >= AI_NAV_MAXIMUM_HOPS || entrance_surface_index == NONE || exit_surface_index == NONE)
		return;
	hop = &nav.hops[nav.hop_count++];
	csmemset(hop, 0, sizeof(*hop));
	hop->kind = ladder ? _ai_nav_hop_ladder : _ai_nav_hop_teleporter;
	hop->teleporter_index = teleporter_index;
	hop->entrance = *entrance;
	hop->entrance_surface_index = entrance_surface_index;
	hop->exit = *exit;
	hop->exit_surface_index = exit_surface_index;
	hop->edge = *entrance;
	/* (a teleporter's a moment; a ladder's climb, a unit a second) */
	hop->cost = ladder ? 2.0f + (real)fabs(exit->z - entrance->z) : 2.0f;
	if (nav.region)
		nav_index_hops();
}

boolean ai_nav_ramp(long surface_index)
{
	return nav.ramp && nav.phase >= _nav_phase_grid && nav.phase != _nav_phase_none && surface_index >= 0 &&
		surface_index < nav.surface_count && nav.ramp[surface_index] == 1;
}

long ai_nav_region(long surface_index)
{
	if (!nav.region || nav.phase < _nav_phase_grid || nav.phase == _nav_phase_none || surface_index < 0 ||
		surface_index >= nav.surface_count)
	{
		return NONE;
	}
	return nav.region[surface_index];
}

long ai_nav_hop_count(void)
{
	return nav.hop_count;
}

struct ai_nav_hop *ai_nav_hop_get(long hop_index)
{
	return hop_index >= 0 && hop_index < nav.hop_count ? &nav.hops[hop_index] : NULL;
}

boolean ai_nav_hop_usable(long hop_index, long now)
{
	struct ai_nav_hop const *hop = ai_nav_hop_get(hop_index);

	return hop && now >= hop->rest_until_tick && !(hop->failures >= 3 && !hop->successes);
}

void ai_nav_hop_result(long hop_index, boolean success, long now)
{
	struct ai_nav_hop *hop = ai_nav_hop_get(hop_index);

	if (!hop)
		return;
	if (success)
	{
		hop->successes++;
		nav.hop_successes[hop->kind]++;
		return;
	}
	hop->failures++;
	nav.hop_failures[hop->kind]++;
	hop->rest_until_tick = now + TICKS_PER_SECOND * 20;
	/* (the reach known changes with it) */
	if (hop->failures >= 3 && !hop->successes)
		nav.hop_generation++;
}

long ai_nav_next_jump_from(long region, long *cursor)
{
	if (!nav.hops_indexed || !nav.region_first_hop || region < 0 || region >= nav.region_count)
		return NONE;
	while (nav.region_first_hop[region] + *cursor < nav.region_first_hop[region + 1])
	{
		long hop_index = nav.hop_order[nav.region_first_hop[region] + (*cursor)++];

		if (nav.hops[hop_index].kind >= _ai_nav_hop_jump_up)
			return hop_index;
	}
	return NONE;
}

/* ---------- the way between regions */

struct nav_heap_item
{
	real key;
	long hop;
};

static void nav_heap_push(struct nav_heap_item *heap, long *count, real key, long hop)
{
	long position = (*count)++;

	while (position > 0)
	{
		long parent = (position - 1) / 2;

		if (heap[parent].key <= key)
			break;
		heap[position] = heap[parent];
		position = parent;
	}
	heap[position].key = key;
	heap[position].hop = hop;
}

static struct nav_heap_item nav_heap_pop(struct nav_heap_item *heap, long *count)
{
	struct nav_heap_item top = heap[0], last = heap[--(*count)];
	long position = 0;

	for (;;)
	{
		long child = position * 2 + 1;

		if (child >= *count)
			break;
		if (child + 1 < *count && heap[child + 1].key < heap[child].key)
			child++;
		if (heap[child].key >= last.key)
			break;
		heap[position] = heap[child];
		position = child;
	}
	if (*count > 0)
		heap[position] = last;
	return top;
}

long ai_nav_route(long start_surface_index, real_point3d const *start, long goal_surface_index,
	real_point3d const *goal, long now, real *cost)
{
	return ai_nav_route_kinds(start_surface_index, start, goal_surface_index, goal, now, cost, AI_NAV_ALL_HOPS);
}

long ai_nav_route_kinds(long start_surface_index, real_point3d const *start, long goal_surface_index,
	real_point3d const *goal, long now, real *cost, unsigned long kinds)
{
	/* (each hop, and the goal, pushed at most a few times) */
	static struct nav_heap_item heap[AI_NAV_MAXIMUM_HOPS * 4 + 8];
	real arrive[AI_NAV_MAXIMUM_HOPS];
	long first[AI_NAV_MAXIMUM_HOPS];
	long heap_count = 0, index;
	long start_region = ai_nav_region(start_surface_index), goal_region = ai_nav_region(goal_surface_index);
	real best = REAL_MAX;
	long best_first = AI_NAV_NO_WAY;

	if (start_region == NONE || goal_region == NONE || start_region == goal_region || !nav.hops_indexed ||
		!nav.region_first_hop)
	{
		return NONE;
	}
	nav.routes_asked++;
	for (index = 0; index < nav.hop_count; index++)
	{
		arrive[index] = REAL_MAX;
		first[index] = NONE;
	}
	for (index = nav.region_first_hop[start_region]; index < nav.region_first_hop[start_region + 1]; index++)
	{
		long hop_index = nav.hop_order[index];
		real key;

		if (!ai_nav_hop_usable(hop_index, now) || nav.hop_to_region[hop_index] == NONE ||
			!(kinds & (1UL << nav.hops[hop_index].kind)))
			continue;
		key = nav_distance3d(start, &nav.hops[hop_index].entrance) + nav.hops[hop_index].cost;
		if (key < arrive[hop_index])
		{
			arrive[hop_index] = key;
			first[hop_index] = hop_index;
			nav_heap_push(heap, &heap_count, key, hop_index);
		}
	}
	while (heap_count > 0)
	{
		struct nav_heap_item item = nav_heap_pop(heap, &heap_count);
		long to_region;

		if (item.key >= best)
			break;
		/* (the goal reached: the way's first hop) */
		if (item.hop < 0)
		{
			best = item.key;
			best_first = first[-item.hop - 1];
			break;
		}
		if (item.key > arrive[item.hop])
			continue;
		to_region = nav.hop_to_region[item.hop];
		if (to_region == goal_region)
		{
			real total = item.key + nav_distance3d(&nav.hops[item.hop].exit, goal);

			if (total < best && heap_count < (long)NUMBEROF(heap))
				nav_heap_push(heap, &heap_count, total, -item.hop - 1);
			continue;
		}
		for (index = nav.region_first_hop[to_region]; index < nav.region_first_hop[to_region + 1]; index++)
		{
			long hop_index = nav.hop_order[index];
			real key;

			if (!ai_nav_hop_usable(hop_index, now) || nav.hop_to_region[hop_index] == NONE ||
			!(kinds & (1UL << nav.hops[hop_index].kind)))
				continue;
			key = item.key + nav_distance3d(&nav.hops[item.hop].exit, &nav.hops[hop_index].entrance) + nav.hops[hop_index].cost;
			if (key < arrive[hop_index] && heap_count < (long)NUMBEROF(heap))
			{
				arrive[hop_index] = key;
				first[hop_index] = first[item.hop];
				nav_heap_push(heap, &heap_count, key, hop_index);
			}
		}
	}
	if (best_first == AI_NAV_NO_WAY)
		nav.routes_none++;
	else
		nav.routes_found++;
	if (cost)
		*cost = best;
	return best_first;
}

/* the regions a start region reaches, a bit each (kept for the latest few) */
static unsigned long const *nav_reach(long start_region, long now)
{
	long slot, oldest = 0, words = (nav.region_count + 31) / 32;
	long *queue;
	unsigned long *bits;
	long head = 0, tail = 0;

	for (slot = 0; slot < NAV_REACH_CACHE; slot++)
	{
		if (nav.reach[slot].bits && nav.reach[slot].start_region == start_region &&
			nav.reach[slot].hop_generation == nav.hop_generation)
		{
			nav.reach[slot].stamp = ++nav.reach_stamp;
			return nav.reach[slot].bits;
		}
		if (nav.reach[slot].stamp < nav.reach[oldest].stamp)
			oldest = slot;
	}
	if (!nav.reach[oldest].bits)
		nav.reach[oldest].bits = (unsigned long *)malloc(sizeof(unsigned long) * (size_t)MAX(words, 1));
	queue = (long *)malloc(sizeof(long) * (size_t)MAX(nav.region_count, 1));
	if (!nav.reach[oldest].bits || !queue)
	{
		if (queue)
			free(queue);
		return NULL;
	}
	bits = nav.reach[oldest].bits;
	csmemset(bits, 0, sizeof(unsigned long) * (size_t)MAX(words, 1));
	bits[start_region / 32] |= 1UL << (start_region % 32);
	queue[tail++] = start_region;
	while (head < tail)
	{
		long region = queue[head++], index;

		for (index = nav.region_first_hop[region]; index < nav.region_first_hop[region + 1]; index++)
		{
			long hop_index = nav.hop_order[index];
			long to = nav.hop_to_region[hop_index];

			/* (a hop resting now is one still: the reach is the map's) */
			if (to == NONE || (nav.hops[hop_index].failures >= 3 && !nav.hops[hop_index].successes))
				continue;
			if (!(bits[to / 32] & (1UL << (to % 32))))
			{
				bits[to / 32] |= 1UL << (to % 32);
				queue[tail++] = to;
			}
		}
	}
	free(queue);
	(void)now;
	nav.reach[oldest].start_region = start_region;
	nav.reach[oldest].hop_generation = nav.hop_generation;
	nav.reach[oldest].stamp = ++nav.reach_stamp;
	return bits;
}

boolean ai_nav_reachable(long start_surface_index, long goal_surface_index, long now)
{
	long start_region = ai_nav_region(start_surface_index), goal_region = ai_nav_region(goal_surface_index);
	unsigned long const *bits;

	if (start_region == NONE || goal_region == NONE || start_region == goal_region || !nav.hops_indexed ||
		!nav.region_first_hop)
	{
		return TRUE;
	}
	bits = nav_reach(start_region, now);
	return !bits || (bits[goal_region / 32] & (1UL << (goal_region % 32))) != 0;
}

void ai_nav_report(long now)
{
	if (nav.phase != _nav_phase_done)
		return;
	platform_log("ai nav at tick %ld: routes asked %ld, found %ld, none %ld; hops taken/failed: teleporter %ld/%ld, ladder %ld/%ld, "
		"up %ld/%ld, drop %ld/%ld, gap %ld/%ld", now, nav.routes_asked, nav.routes_found, nav.routes_none,
		nav.hop_successes[_ai_nav_hop_teleporter], nav.hop_failures[_ai_nav_hop_teleporter],
		nav.hop_successes[_ai_nav_hop_ladder], nav.hop_failures[_ai_nav_hop_ladder],
		nav.hop_successes[_ai_nav_hop_jump_up], nav.hop_failures[_ai_nav_hop_jump_up],
		nav.hop_successes[_ai_nav_hop_drop], nav.hop_failures[_ai_nav_hop_drop],
		nav.hop_successes[_ai_nav_hop_gap], nav.hop_failures[_ai_nav_hop_gap]);
}


/* ---------- the walkable copy */

/* a pathfinding surface's flag: walkable (path.c's) */
#define AI_NAV_WALKABLE_FLAG_BIT 6
#define AI_NAV_SURFACE_CORNERS 16
/* the surfaces looked at for room above them a tick */
#define AI_NAV_SCAN_A_TICK 32
/* the machines closed at most */
#define AI_NAV_MAXIMUM_CLOSED_OBJECTS 32

static struct
{
	struct structure_bsp *source;
	struct structure_bsp structure;
	byte *walkable;
	struct structure_bsp vehicle_structure;
	byte *vehicle_walkable;
	boolean machines_closed;
	long closed_count;
	long scan_index;
	boolean scan_done;
	long roofed_count;
	boolean ramps_opened;
	long ramps_open_count;
	long ramps_cursor;
	/* the tick the knowledge was last stepped at (once a tick) */
	long stepped_tick;
	boolean ready_logged;
} ai_nav_paths;

static real ai_nav_paths_distance3d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;

	return (real)sqrt(dx * dx + dy * dy + dz * dz);
}

static void ai_nav_paths_free(void)
{
	/* (the port's free halts on NULL) */
	if (ai_nav_paths.walkable)
		free(ai_nav_paths.walkable);
	if (ai_nav_paths.vehicle_walkable)
		free(ai_nav_paths.vehicle_walkable);
	csmemset(&ai_nav_paths, 0, sizeof(ai_nav_paths));
	ai_nav_paths.stepped_tick = NONE;
}

/* the copies made from this map's structure (FALSE: none to make) */
static boolean ai_nav_paths_copy(void)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	long count;

	if (!structure || structure->pathfinding_surfaces.count <= 0 || structure->collision_bsp.count <= 0 ||
		(ai_nav_paths.source && ai_nav_paths.source != structure))
	{
		return FALSE;
	}
	if (ai_nav_paths.walkable)
		return TRUE;
	count = structure->pathfinding_surfaces.count;
	/* (the game's malloc, cseries.h's debug_malloc: system_malloc, the
	Xbox heap (xbox_heap.c), so in the 64-bit builds the copies are inside
	the Xbox window that xbox_address below needs) */
	ai_nav_paths.walkable = (byte *)malloc((size_t)count);
	ai_nav_paths.vehicle_walkable = (byte *)malloc((size_t)count);
	if (!ai_nav_paths.walkable || !ai_nav_paths.vehicle_walkable)
	{
		if (ai_nav_paths.walkable)
			free(ai_nav_paths.walkable);
		if (ai_nav_paths.vehicle_walkable)
			free(ai_nav_paths.vehicle_walkable);
		ai_nav_paths.walkable = ai_nav_paths.vehicle_walkable = NULL;
		return FALSE;
	}
	csmemcpy(ai_nav_paths.walkable, xbox_pointer(structure->pathfinding_surfaces.address), (size_t)count);
	csmemcpy(ai_nav_paths.vehicle_walkable, xbox_pointer(structure->pathfinding_surfaces.address), (size_t)count);
	ai_nav_paths.structure = *structure;
	ai_nav_paths.structure.pathfinding_surfaces.address = xbox_address(ai_nav_paths.walkable);
	ai_nav_paths.vehicle_structure = *structure;
	ai_nav_paths.vehicle_structure.pathfinding_surfaces.address = xbox_address(ai_nav_paths.vehicle_walkable);
	ai_nav_paths.source = structure;
	return TRUE;
}

/* a surface's corners (at most AI_NAV_SURFACE_CORNERS), from its ring of
edges; 0 for one not the bsp's */
static long ai_nav_paths_corners(struct collision_bsp const *bsp, long surface_index, real_point3d *corners)
{
	struct collision_surface const *surface;
	long edge_index, count = 0;

	if (surface_index < 0 || surface_index >= bsp->surfaces.count)
		return 0;
	surface = TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index, struct collision_surface);
	edge_index = surface->first_edge_index;
	do
	{
		struct collision_edge const *edge;
		boolean right;

		if (edge_index < 0 || edge_index >= bsp->edges.count)
			return 0;
		edge = TAG_BLOCK_GET_ELEMENT(&bsp->edges, edge_index, struct collision_edge);
		right = edge->surface_indices[1] == surface_index;
		if (!VALID_INDEX(edge->vertex_indices[right], bsp->vertices.count))
			return 0;
		corners[count++] = TAG_BLOCK_GET_ELEMENT(&bsp->vertices, edge->vertex_indices[right], struct collision_vertex)->point;
		edge_index = edge->edge_indices[right];
	} while (edge_index != surface->first_edge_index && count < AI_NAV_SURFACE_CORNERS);
	return count;
}

/* a walkable surface of the copy: its corners, middle and how far its
corners are from it; FALSE for one not walkable */
static boolean ai_nav_paths_walkable_surface(struct collision_bsp const *bsp, long surface_index,
	real_point3d *corners, long *corner_count, real_point3d *middle, real *reach)
{
	long corner;

	if (surface_index >= ai_nav_paths.structure.pathfinding_surfaces.count ||
		!TEST_FLAG(ai_nav_paths.walkable[surface_index], AI_NAV_WALKABLE_FLAG_BIT))
	{
		return FALSE;
	}
	*corner_count = ai_nav_paths_corners(bsp, surface_index, corners);
	if (*corner_count < 3)
		return FALSE;
	middle->x = middle->y = middle->z = 0.0f;
	for (corner = 0; corner < *corner_count; corner++)
	{
		middle->x += corners[corner].x / (real)*corner_count;
		middle->y += corners[corner].y / (real)*corner_count;
		middle->z += corners[corner].z / (real)*corner_count;
	}
	*reach = 0.0f;
	for (corner = 0; corner < *corner_count; corner++)
		*reach = MAX(*reach, ai_nav_paths_distance3d(middle, &corners[corner]));
	return TRUE;
}

/* whether the object stands across the surface: a line over it at a biped's
knees or chest, from its middle to a little past an edge's, meets it, or
its middle is under it */
static boolean ai_nav_paths_surface_blocked(long object_index, real_point3d const *corners, long corner_count,
	real_point3d const *middle)
{
	static real const heights[] = { 0.35f, 1.0f };
	unsigned long flags = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit);
	struct collision_result collision;
	real_point3d start = *middle;
	real_vector3d up = { 0.0f, 0.0f, 1.6f };
	long height, corner;

	start.z += 0.05f;
	if (collision_test_vector(flags, &start, &up, NONE, &collision) && collision.type == _collision_result_object &&
		collision.object_index == object_index)
	{
		return TRUE;
	}
	for (height = 0; height < (long)NUMBEROF(heights); height++)
	{
		start.z = middle->z + heights[height];
		for (corner = 0; corner < corner_count; corner++)
		{
			real_point3d const *a = &corners[corner], *b = &corners[(corner + 1) % corner_count];
			real_vector3d vector;
			real length, past;

			vector.i = 0.5f * (a->x + b->x) - start.x;
			vector.j = 0.5f * (a->y + b->y) - start.y;
			vector.k = 0.5f * (a->z + b->z) + heights[height] - start.z;
			/* (on a little past the edge: a door may stand on it) */
			length = (real)sqrt(vector.i * vector.i + vector.j * vector.j + vector.k * vector.k);
			if (length < 0.01f)
				continue;
			past = (length + 0.4f) / length;
			vector.i *= past;
			vector.j *= past;
			vector.k *= past;
			if (collision_test_vector(flags, &start, &vector, NONE, &collision) &&
				collision.type == _collision_result_object && collision.object_index == object_index)
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* the walkable surfaces an object stands across closed, in both copies */
static void ai_nav_paths_close_object(long object_index)
{
	struct object_datum *object = object_get(object_index);
	struct collision_bsp const *bsp = TAG_BLOCK_GET_ELEMENT(&ai_nav_paths.structure.collision_bsp, 0, struct collision_bsp);
	long surface_index, closed = 0;

	for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
	{
		real_point3d corners[AI_NAV_SURFACE_CORNERS], middle;
		real reach;
		long corner_count;

		if (ai_nav_paths_walkable_surface(bsp, surface_index, corners, &corner_count, &middle, &reach) &&
			ai_nav_paths_distance3d(&middle, &object->object.bounding_sphere_center) <
				object->object.bounding_sphere_radius + reach + 0.5f &&
			ai_nav_paths_surface_blocked(object_index, corners, corner_count, &middle))
		{
			ai_nav_paths.walkable[surface_index] &= (byte)~FLAG(AI_NAV_WALKABLE_FLAG_BIT);
			ai_nav_paths.vehicle_walkable[surface_index] &= (byte)~FLAG(AI_NAV_WALKABLE_FLAG_BIT);
			closed++;
		}
	}
	ai_nav_paths.closed_count += closed;
	platform_log("ai nav: a machine in the way: %s at (%.1f %.1f %.1f): %ld walkable surfaces closed",
		tag_get_name(object->definition_index), object->object.bounding_sphere_center.x,
		object->object.bounding_sphere_center.y, object->object.bounding_sphere_center.z, closed);
}

/* the machines that stand in the way (pathfinding obstacles, not elevators,
not open doors that let a path through) */
static void ai_nav_paths_close_machines(void)
{
	struct object_iterator iterator;
	long closed = 0;

	ai_nav_paths.machines_closed = TRUE;
	object_iterator_new(&iterator, _object_mask_machine, 0);
	while (object_iterator_next(&iterator) && closed < AI_NAV_MAXIMUM_CLOSED_OBJECTS)
	{
		struct object_datum *object = object_get(iterator.index);
		struct machine_definition *definition = machine_definition_get(object->definition_index);

		if (!TEST_FLAG(definition->machine.flags, _machine_is_pathfinding_obstacle_bit) ||
			TEST_FLAG(definition->machine.flags, _machine_is_elevator_bit) ||
			(device_get_position(iterator.index) > 0.5f &&
				TEST_FLAG(definition->machine.flags, _machine_is_not_pathfinding_obstacle_when_open_bit)))
		{
			continue;
		}
		ai_nav_paths_close_object(iterator.index);
		closed++;
	}
}

/* a bounded step of the surfaces with no room above them, closed */
static void ai_nav_paths_scan(void)
{
	struct collision_bsp const *bsp;
	long scanned;

	if (ai_nav_paths.scan_done)
		return;
	bsp = TAG_BLOCK_GET_ELEMENT(&ai_nav_paths.structure.collision_bsp, 0, struct collision_bsp);
	for (scanned = 0; scanned < AI_NAV_SCAN_A_TICK; scanned++)
	{
		long surface_index = ai_nav_paths.scan_index++;
		real_point3d corners[AI_NAV_SURFACE_CORNERS], middle;
		real reach;
		long corner_count, sample, covered = 0;

		if (surface_index >= bsp->surfaces.count || surface_index >= ai_nav_paths.structure.pathfinding_surfaces.count)
		{
			ai_nav_paths.scan_done = TRUE;
			platform_log("ai nav: %ld walkable surfaces with no room above closed", ai_nav_paths.roofed_count);
			return;
		}
		if (!ai_nav_paths_walkable_surface(bsp, surface_index, corners, &corner_count, &middle, &reach))
			continue;
		/* (the middle first: covered, the surface is; else most of the
		points between it and the corners) */
		for (sample = -1; sample < corner_count; sample++)
		{
			unsigned long flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
				FLAG(_collision_test_back_facing_surfaces_bit);
			real_vector3d up = { 0.0f, 0.0f, 0.35f };
			struct collision_result collision;
			real_point3d point = middle;

			if (sample >= 0)
			{
				point.x = 0.5f * (middle.x + corners[sample].x);
				point.y = 0.5f * (middle.y + corners[sample].y);
				point.z = 0.5f * (middle.z + corners[sample].z);
			}
			point.z += 0.05f;
			if (collision_test_vector(flags, &point, &up, NONE, &collision))
			{
				if (sample < 0)
				{
					covered = corner_count;
					break;
				}
				covered++;
			}
		}
		if (covered * 2 > corner_count)
		{
			ai_nav_paths.walkable[surface_index] &= (byte)~FLAG(AI_NAV_WALKABLE_FLAG_BIT);
			ai_nav_paths.vehicle_walkable[surface_index] &= (byte)~FLAG(AI_NAV_WALKABLE_FLAG_BIT);
			ai_nav_paths.roofed_count++;
		}
	}
}

/* the ramps a biped walks up (the regions join them), opened to the
bipeds' copy (not the vehicles'), 500 surfaces a tick; then the surfaces
with no room above looked at again with them */
static void ai_nav_paths_open_ramps(void)
{
	long surface_index, count;

	if (ai_nav_paths.ramps_opened || !ai_nav_ready())
		return;
	count = ai_nav_paths.structure.pathfinding_surfaces.count;
	for (surface_index = ai_nav_paths.ramps_cursor; surface_index < count && surface_index < ai_nav_paths.ramps_cursor + 500;
		surface_index++)
	{
		if (ai_nav_ramp(surface_index) && ai_nav_region(surface_index) != NONE &&
			!TEST_FLAG(ai_nav_paths.walkable[surface_index], AI_NAV_WALKABLE_FLAG_BIT))
		{
			ai_nav_paths.walkable[surface_index] |= (byte)FLAG(AI_NAV_WALKABLE_FLAG_BIT);
			ai_nav_paths.ramps_open_count++;
		}
	}
	ai_nav_paths.ramps_cursor = surface_index;
	if (surface_index < count)
		return;
	ai_nav_paths.ramps_opened = TRUE;
	if (ai_nav_paths.ramps_open_count)
	{
		ai_nav_paths.scan_index = 0;
		ai_nav_paths.scan_done = FALSE;
	}
	platform_log("ai nav: %ld ramps opened to the searches", ai_nav_paths.ramps_open_count);
}

/* ---------- the walkable copy: public code */

void ai_nav_map_reset(void)
{
	ai_nav_paths_free();
	ai_nav_reset();
}

void ai_nav_map_update(long biped_definition_index)
{
	long now = game_time_get();

	/* (once a tick; a new structure or a game started again: made again) */
	if (ai_nav_paths.source && ai_nav_paths.source != global_structure_bsp_get())
		ai_nav_map_reset();
	if (ai_nav_paths.stepped_tick != NONE && now < ai_nav_paths.stepped_tick)
		ai_nav_map_reset();
	if (now == ai_nav_paths.stepped_tick || !ai_nav_paths_copy())
		return;
	ai_nav_paths.stepped_tick = now;
	if (!ai_nav_paths.machines_closed)
		ai_nav_paths_close_machines();
	ai_nav_set_biped(biped_definition_index);
	ai_nav_paths_scan();
	ai_nav_update();
	ai_nav_paths_open_ramps();
	if (ai_nav_map_ready() && !ai_nav_paths.ready_logged)
	{
		ai_nav_paths.ready_logged = TRUE;
		platform_log("ai nav: the map's navigation is made: %ld hops", ai_nav_hop_count());
	}
}

boolean ai_nav_map_ready(void)
{
	return ai_nav_paths.walkable && ai_nav_phase() >= 5 && ai_nav_paths.ramps_opened;
}

struct structure_bsp *ai_nav_structure(boolean vehicle)
{
	if (!ai_nav_paths.walkable || ai_nav_paths.source != global_structure_bsp_get())
		return NULL;
	return vehicle ? &ai_nav_paths.vehicle_structure : &ai_nav_paths.structure;
}

long ai_nav_ground_surface(real_point3d const *point, real_point3d *ground)
{
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d down = { 0.0f, 0.0f, -6.0f };

	start.z += 0.4f;
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		&start, &down, NONE, &collision) && collision.type == _collision_result_structure)
	{
		if (ground)
			*ground = collision.point;
		return collision.surface_index;
	}
	return NONE;
}

/* ---------- the test (debug: HALO_AI_NAV_TEST, in a network test's game) */

static struct
{
	boolean done;
	long biped;
} ai_nav_test;

void ai_nav_test_update(void)
{
	struct scenario *scenario;
	struct player_starting_location const *from = NULL, *to = NULL;
	real best = 0.0f;
	long index, other;

	if (ai_nav_test.done || !getenv("HALO_AI_NAV_TEST") || !(scenario = global_scenario_get()))
		return;
	/* (the biped: the first player's unit's) */
	if (ai_nav_test.biped == 0)
	{
		struct data_iterator iterator;
		struct player_datum *player;

		ai_nav_test.biped = NONE;
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (object_try_and_get(player->unit_index))
			{
				ai_nav_test.biped = object_get(player->unit_index)->definition_index;
				break;
			}
		}
		if (ai_nav_test.biped == NONE)
		{
			ai_nav_test.biped = 0;
			return;
		}
	}
	ai_nav_map_update(ai_nav_test.biped);
	if (!ai_nav_map_ready())
		return;
	ai_nav_test.done = TRUE;
	/* (the two starting locations furthest apart) */
	for (index = 0; index < scenario->players.count; index++)
	{
		for (other = index + 1; other < scenario->players.count; other++)
		{
			struct player_starting_location const *a =
				TAG_BLOCK_GET_ELEMENT(&scenario->players, index, struct player_starting_location);
			struct player_starting_location const *b =
				TAG_BLOCK_GET_ELEMENT(&scenario->players, other, struct player_starting_location);
			real distance = ai_nav_paths_distance3d(&a->position, &b->position);

			if (distance > best)
			{
				best = distance;
				from = a;
				to = b;
			}
		}
	}
	if (!from)
	{
		platform_log("ai nav test: failed: the map has fewer than two starting locations");
		return;
	}
	{
		static struct path_state state;
		struct path_input input;
		real_point3d start, goal;
		long start_surface = ai_nav_ground_surface(&from->position, &start);
		long goal_surface = ai_nav_ground_surface(&to->position, &goal);
		long start_region = ai_nav_region(start_surface), goal_region = ai_nav_region(goal_surface);
		boolean found = FALSE;
		real cost = 0.0f;
		long hop = NONE;

		platform_log("ai nav test: %ld hops; from (%.1f %.1f %.1f) region %ld to (%.1f %.1f %.1f) region %ld, %.0f units apart",
			ai_nav_hop_count(), start.x, start.y, start.z, start_region, goal.x, goal.y, goal.z, goal_region, (double)best);
		if (start_surface == NONE || goal_surface == NONE || start_region == NONE || goal_region == NONE)
		{
			platform_log("ai nav test: failed: a starting location on no walkable ground");
			return;
		}
		if (start_region != goal_region)
		{
			/* (another region: the first hop of the way, by jumps, drops
			and gaps alone, then the search to its entrance) */
			hop = ai_nav_route_kinds(start_surface, &start, goal_surface, &goal, game_time_get(), &cost,
				(1UL << _ai_nav_hop_jump_up) | (1UL << _ai_nav_hop_drop) | (1UL << _ai_nav_hop_gap));
			if (hop < 0)
			{
				platform_log("ai nav test: failed: no way between the regions");
				return;
			}
			goal = ai_nav_hop_get(hop)->entrance;
			goal_surface = ai_nav_hop_get(hop)->entrance_surface_index;
		}
		/* (a long way across a large level is more surfaces than a search
		holds: each search goes as near as it reaches, and the next from
		there, as the bots search; 16 legs at most) */
		{
			long leg;
			real left = ai_nav_paths_distance3d(&start, &goal);

			for (leg = 0; leg < 16 && !found; leg++)
			{
				real now_left;

				path_input_new(&input, 0.25f, FALSE, NONE);
				path_input_set_start(&input, &start, start_surface);
				path_state_new(&input, &state, NULL);
				state.structure = ai_nav_structure(FALSE);
				path_state_destination(&state, &goal, goal_surface, 1000.0f);
				if (!state.structure || !path_state_find(&state))
					break;
				now_left = ai_nav_paths_distance3d(&state.closest_point, &goal);
				platform_log("ai nav test: leg %ld from (%.1f %.1f %.1f) reaches (%.1f %.1f %.1f), %.1f units from the goal",
					leg + 1, start.x, start.y, start.z, state.closest_point.x, state.closest_point.y, state.closest_point.z,
					(double)now_left);
				if (now_left < 1.5f)
					found = TRUE;
				else if (now_left > left - 0.5f)
					break;
				left = now_left;
				start_surface = ai_nav_ground_surface(&state.closest_point, &start);
				if (start_surface == NONE)
					break;
			}
		}
		platform_log("ai nav test: path %s%s%s", found ? "found" : "not found",
			hop != NONE ? " to the first hop" : " to the goal", hop != NONE ? ", the rest by hops" : "");
		platform_log("ai nav test: %s", found ? "passed" : "failed");
	}
}

#endif
