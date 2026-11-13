/*
DOOR_PORTALS.C

port: closed doors shut their portals to the dynamic lights.

The game lights the level with a dynamic light only in the clusters its
sphere reaches through the level's portals, and an object only with the
lights of its clusters. A door is an object, so it never closes the portal it
stands in: a light just behind a closed door lit the walls and the objects on
the near side through it.

Each door (a machine of the door type) is paired with the portals it covers,
from geometry alone: the door's collision polygons, where its nodes are,
against each portal near it. A portal is the door's when the door's polygons,
met along the portal's normal within DOOR_PORTALS_DEPTH of it, cover
DOOR_PORTALS_COVERED of the portal (door_portals_coverage). The pairing is
made the first time the door is seen at rest at either end of its travel
(device position 0 or 1), for that end. A door's closed end is whichever end
covers its doorway; an open door covers nothing. While a door rests where it
covers its portals they are shut. A moving door is open, so the light comes
through its gap as soon as it starts to open.

What a shut portal changes, each frame (lights_preprocess_scene): for each
light drawn, the clusters it reaches only through a shut portal are left out
of its passes over the level and out of the lights the objects there take.

This is drawing only. Nothing the game simulates is written (the units'
illumination, which the AI sees by, keeps the game's own clusters), and
nothing goes over the network. display.doors_block_light = false leaves the
portals open, as the Xbox had them, and debug.door_portals_log logs each
door's pairing.

A door is paired at an end only once it has rested there DOOR_PORTALS_REST_TICKS
game ticks, so that its pose (moved once a tick, however many frames are drawn)
has settled. Turning the setting off forgets every door, so turning it on
again pairs each one afresh. The original Xbox, with its CPU and 64
MB, keeps fewer doors, polygons and lights (HALO_CONSOLE_XBOX_MAP_SIZES):
about 20 KB in all, and a door with more polygons than it looks at only
covers less of its doorway, so it may shut nothing.

Based on work by goatbutt and the OpenH1 team (erin.cena's door portals).
*/

/* ---------- headers */

#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "devices/devices.h"
#include "game/game.h"
#include "devices/device_definitions.h"
#include "devices/device_machines.h"
#include "physics/collision_bsp.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collision_model_definitions.h"
#include "physics/collision_models.h"
#include "rasterizer/rasterizer.h"
#include "scenario/scenario.h"
#include "structures/structures.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_groups.h"
#include "door_portals.h"

#include <math.h>
#include <string.h>

/* port/linux/src/port_config.h */
int config_boolean(const char *name);
/* port/linux/src (halo.log) */
void platform_log(const char *format, ...);
/* structures.c */
boolean sphere_intersects_cluster_portal(struct structure_bsp *structure, short portal_index,
	real_point3d const *point, real radius);

/* ---------- constants */

enum
{
	/* (device_machines.c's _machine_type_door) */
	DOOR_PORTALS_MACHINE_TYPE_DOOR = 0,
	/* the doors kept paired at once (no stock map has this many; past it,
	a door shuts nothing) */
#ifdef HALO_CONSOLE_XBOX_MAP_SIZES
	DOOR_PORTALS_MAXIMUM_DOORS = 64,
#else
	DOOR_PORTALS_MAXIMUM_DOORS = 256,
#endif
	/* the portals a door shuts at most, at each end */
	DOOR_PORTALS_PER_DOOR = 4,
	/* the portals that can be shut (more than any stock map has: past
	them, none is) */
	DOOR_PORTALS_MAXIMUM_PORTALS = 4096,
	/* a door's polygons looked at (past them, the rest of the door covers
	nothing) */
#ifdef HALO_CONSOLE_XBOX_MAP_SIZES
	DOOR_PORTALS_MAXIMUM_POLYGONS = 128,
#else
	DOOR_PORTALS_MAXIMUM_POLYGONS = 512,
#endif
	DOOR_PORTALS_MAXIMUM_POINTS = DOOR_PORTALS_MAXIMUM_POLYGONS * MAXIMUM_VERTICES_PER_COLLISION_SURFACE,
	/* each triangle of a portal (from its middle) is cut into this many
	squared, with a sample at the middle of each piece */
	DOOR_PORTALS_SUBDIVISIONS = 6,
	/* the lights of a frame that can have clusters behind a shut door
	(past them, a light lights through the door as before) */
#ifdef HALO_CONSOLE_XBOX_MAP_SIZES
	DOOR_PORTALS_BLOCKED_LIGHTS = 16,
#else
	DOOR_PORTALS_BLOCKED_LIGHTS = 64,
#endif
	/* the game ticks a door rests at an end before it is paired there */
	DOOR_PORTALS_REST_TICKS = 3,
	DOOR_PORTALS_CLUSTER_WORDS = MAXIMUM_CLUSTERS_PER_STRUCTURE / 32,
};

/* how far from a portal's plane a door's polygon may meet a line along the
portal's normal, in world units (a doorway's depth: a door stands in its
frame, and the portal sits at one face of it or in the middle) */
#define DOOR_PORTALS_DEPTH 0.5f
/* how far outside a polygon's edges a point still meets it (the seam
between a door's two halves) */
#define DOOR_PORTALS_SLACK 0.01f
/* the share of a portal a door covers to shut it. A portal is drawn around
its doorway, not cut to it: on the stock maps closed doors cover 75% to 100%
of theirs, a burnt door about half, and an open one little */
#define DOOR_PORTALS_COVERED 0.75f
/* a polygon more nearly edge-on to the portal than this (the cosine of its
normal with the portal's) covers nothing */
#define DOOR_PORTALS_EDGE_ON 0.1f

/* ---------- structures */

/* the level's cluster portal and cluster, as structures.c has them */
struct door_portals_cluster_portal
{
	short cluster_indices[2];
	long plane_index;
	real_point3d centroid;
	real bounding_radius;
	unsigned long flags;
	long unused[6];
	struct tag_block vertices;
};

struct door_portals_cluster
{
	byte unused[0x5C];
	struct tag_block portal_indices;
};

typedef char verify_door_portals_cluster_portal_size[sizeof(struct door_portals_cluster_portal) == 0x40 ? 1 : -1];
typedef char verify_door_portals_cluster_size[sizeof(struct door_portals_cluster) == 0x68 ? 1 : -1];

/* a paired door: the object, where it was and in which level, and for each
end of its travel whether it was paired there and the portals it covers */
struct door_portals_door
{
	long object_index;
	long definition_index;
	real_point3d position;
	void const *structure;
	/* the frame it was last seen in */
	unsigned long frame;
	boolean paired[2];
	/* the end it rests at (NONE: moving) and the game tick it came to rest */
	short rest_end;
	long rest_since;
	short portal_count[2];
	short portals[2][DOOR_PORTALS_PER_DOOR];
};

static struct
{
	struct door_portals_door doors[DOOR_PORTALS_MAXIMUM_DOORS];
	short door_count;
	unsigned long frame;
	/* the portals shut this frame */
	unsigned long shut[DOOR_PORTALS_MAXIMUM_PORTALS / 32];
	short shut_count;
	/* for each rasterizer light this frame, its row of blocked (plus one;
	0: none), and in each row the clusters the light reaches only through a
	shut portal */
	unsigned char light_rows[MAXIMUM_LIGHTS_PER_WINDOW];
	short blocked_count;
	unsigned long blocked[DOOR_PORTALS_BLOCKED_LIGHTS][DOOR_PORTALS_CLUSTER_WORDS];
	/* a door's polygons, while it is paired */
	real_point3d points[DOOR_PORTALS_MAXIMUM_POINTS];
	short point_counts[DOOR_PORTALS_MAXIMUM_POLYGONS];
	/* each polygon's normal (Newell's, not of unit length) and its length,
	made once as the door is paired */
	real_vector3d normals[DOOR_PORTALS_MAXIMUM_POLYGONS];
	real normal_lengths[DOOR_PORTALS_MAXIMUM_POLYGONS];
} door_portals;

/* ---------- the pairing */

/* a polygon's normal by Newell's method (any convex polygon's, either
winding), not of unit length, and its length */
static real door_portals_normal(
	real_point3d const *polygon,
	short count,
	real_vector3d *normal)
{
	short index;

	normal->i = normal->j = normal->k = 0.0f;
	for (index = 0; index < count; index++)
	{
		real_point3d const *a = &polygon[index];
		real_point3d const *b = &polygon[(index + 1) % count];

		normal->i += (a->y - b->y) * (a->z + b->z);
		normal->j += (a->z - b->z) * (a->x + b->x);
		normal->k += (a->x - b->x) * (a->y + b->y);
	}
	return magnitude3d(normal);
}

/* whether a point meets a polygon (its normal and that normal's length made
before) along a direction (a unit vector), within DOOR_PORTALS_DEPTH of it
and DOOR_PORTALS_SLACK of its edges */
static boolean door_portals_meets(
	real_point3d const *point,
	real_vector3d const *direction,
	real_point3d const *polygon,
	short count,
	real_vector3d const *normal_vector,
	real length)
{
	real_vector3d normal = *normal_vector;
	real_vector3d edge, offset, cross;
	real_point3d met;
	real along, distance;
	short index;

	if (count < 3 || !(length > 0.0f))
		return FALSE;
	along = dot_product3d(&normal, direction);
	if (!(fabsf(along) > DOOR_PORTALS_EDGE_ON * length))
		return FALSE;
	distance = dot_product3d(&normal, vector_from_points3d(point, &polygon[0], &offset)) / along;
	if (!(fabsf(distance) <= DOOR_PORTALS_DEPTH))
		return FALSE;
	point_from_line3d(point, direction, distance, &met);
	for (index = 0; index < count; index++)
	{
		real edge_length;

		vector_from_points3d(&polygon[index], &polygon[(index + 1) % count], &edge);
		vector_from_points3d(&polygon[index], &met, &offset);
		edge_length = magnitude3d(&edge);
		if (!(edge_length > 0.0f))
			continue;
		/* (how far inside this edge it is, in the polygon's plane) */
		if (dot_product3d(cross_product3d(&edge, &offset, &cross), &normal) / (length * edge_length) <
			-DOOR_PORTALS_SLACK)
		{
			return FALSE;
		}
	}
	return TRUE;
}

/* the share of a portal (count points, its plane's normal) that a door's
polygons cover: of samples spread evenly over it, the share whose line along
the normal meets one of them (door_portals_meets) */
static real door_portals_coverage(
	real_point3d const *portal,
	short count,
	real_vector3d const *normal,
	short polygon_count)
{
	real_point3d middle = { 0.0f, 0.0f, 0.0f };
	real_vector3d unit = *normal;
	real total = 0.0f, covered = 0.0f;
	real length = magnitude3d(normal);
	short index;

	if (count < 3 || !(length > 0.0f))
		return 0.0f;
	scale_vector3d(&unit, 1.0f / length, &unit);
	for (index = 0; index < count; index++)
	{
		middle.x += portal[index].x / count;
		middle.y += portal[index].y / count;
		middle.z += portal[index].z / count;
	}
	/* (each triangle of the middle and an edge, cut into N squared pieces
	with a sample at each one's middle, weighed by the triangle's area) */
	for (index = 0; index < count; index++)
	{
		real_vector3d a, b, cross;
		real area;
		short i, j, half, met = 0;

		vector_from_points3d(&middle, &portal[index], &a);
		vector_from_points3d(&middle, &portal[(index + 1) % count], &b);
		area = magnitude3d(cross_product3d(&a, &b, &cross));
		for (i = 0; i < DOOR_PORTALS_SUBDIVISIONS; i++)
		{
			for (j = 0; i + j < DOOR_PORTALS_SUBDIVISIONS; j++)
			{
				for (half = 0; half < 2 && (half == 0 || i + j + 1 < DOOR_PORTALS_SUBDIVISIONS); half++)
				{
					real s = ((real)i + (half ? 2.0f : 1.0f) / 3.0f) / DOOR_PORTALS_SUBDIVISIONS;
					real t = ((real)j + (half ? 2.0f : 1.0f) / 3.0f) / DOOR_PORTALS_SUBDIVISIONS;
					real_point3d sample;
					short polygon, first = 0;

					sample.x = middle.x + a.i * s + b.i * t;
					sample.y = middle.y + a.j * s + b.j * t;
					sample.z = middle.z + a.k * s + b.k * t;
					for (polygon = 0; polygon < polygon_count; first += door_portals.point_counts[polygon], polygon++)
					{
						if (door_portals_meets(&sample, &unit, &door_portals.points[first],
							door_portals.point_counts[polygon], &door_portals.normals[polygon],
							door_portals.normal_lengths[polygon]))
						{
							met++;
							break;
						}
					}
				}
			}
		}
		total += area;
		covered += area * (real)met / (real)(DOOR_PORTALS_SUBDIVISIONS * DOOR_PORTALS_SUBDIVISIONS);
	}
	return total > 0.0f ? covered / total : 0.0f;
}

/* a door's collision polygons where its nodes are now, into door_portals'
points; how many */
static short door_portals_polygons(
	long object_index)
{
	struct collision_model_instance instance;
	short polygon_count = 0;
	long point_count = 0;
	short node_index;

	if (!collision_model_instance_new(&instance, object_index))
		return 0;
	for (node_index = 0; node_index < instance.node_count; node_index++)
	{
		struct collision_node const *node =
			TAG_BLOCK_GET_ELEMENT(&instance.model->nodes, node_index, struct collision_node);
		struct collision_bsp const *bsp;
		long surface_index;
		short permutation;

		if (node->region_index < 0 || node->region_index >= MAXIMUM_REGIONS_PER_OBJECT || node->bsps.count <= 0)
			continue;
		/* (the permutation the collision tests take) */
		permutation = (short)PIN((short)instance.region_permutation_indices[node->region_index], 0,
			node->bsps.count - 1);
		bsp = TAG_BLOCK_GET_ELEMENT(&node->bsps, permutation, struct collision_bsp);
		for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
		{
			real_point3d local[MAXIMUM_VERTICES_PER_COLLISION_SURFACE];
			short count, index;

			if (polygon_count >= DOOR_PORTALS_MAXIMUM_POLYGONS ||
				point_count + MAXIMUM_VERTICES_PER_COLLISION_SURFACE > DOOR_PORTALS_MAXIMUM_POINTS)
			{
				return polygon_count;
			}
			count = collision_surface_polygon(bsp, surface_index, local);
			if (count < 3)
				continue;
			for (index = 0; index < count; index++)
				matrix4x3_transform_point(&instance.matrices[node_index], &local[index],
					&door_portals.points[point_count + index]);
			door_portals.normal_lengths[polygon_count] = door_portals_normal(&door_portals.points[point_count], count,
				&door_portals.normals[polygon_count]);
			door_portals.point_counts[polygon_count++] = count;
			point_count += count;
		}
	}
	return polygon_count;
}

/* the portals a door covers, resting at an end of its travel */
static void door_portals_pair(
	long object_index,
	struct door_portals_door *door,
	short end)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	struct bsp3d *bsp = global_bsp3d_get();
	struct object_datum *object = object_get(object_index);
	short polygon_count = door_portals_polygons(object_index);
	long portal_index;

	door->portal_count[end] = 0;
	if (!polygon_count || !structure || !bsp)
		return;
	for (portal_index = 0; portal_index < MIN(structure->cluster_portals.count, DOOR_PORTALS_MAXIMUM_PORTALS);
		portal_index++)
	{
		struct door_portals_cluster_portal const *portal = TAG_BLOCK_GET_ELEMENT(&structure->cluster_portals,
			portal_index, struct door_portals_cluster_portal);
		real reach = object->object.bounding_sphere_radius + portal->bounding_radius;
		real coverage;

		if (portal->plane_index < 0 || portal->plane_index >= bsp->planes.count ||
			portal->vertices.count < 3 || portal->vertices.count > MAXIMUM_VERTICES_PER_CLUSTER_PORTAL ||
			!(distance_squared3d(&object->object.bounding_sphere_center, &portal->centroid) <= reach * reach))
		{
			continue;
		}
		coverage = door_portals_coverage(TAG_BLOCK_GET_ELEMENT(&portal->vertices, 0, real_point3d),
			(short)portal->vertices.count, &TAG_BLOCK_GET_ELEMENT(&bsp->planes, portal->plane_index, real_plane3d)->n,
			polygon_count);
		if (coverage > 0.0f && config_boolean("debug.door_portals_log"))
		{
			platform_log("door portals: %s at (%.2f %.2f %.2f), position %d: portal %ld (clusters %d %d) %.0f%% covered",
				tag_get_name(object->definition_index), object->object.position.x, object->object.position.y,
				object->object.position.z, end, portal_index, portal->cluster_indices[0], portal->cluster_indices[1],
				coverage * 100.0f);
		}
		if (coverage >= DOOR_PORTALS_COVERED && door->portal_count[end] < DOOR_PORTALS_PER_DOOR)
			door->portals[end][door->portal_count[end]++] = (short)portal_index;
	}
}

/* the door entry of an object: its own, or a new one (one not seen for a
frame, or one past the doors kept), or NULL when none is free */
static struct door_portals_door *door_portals_door_get(
	long object_index)
{
	short index;
	short free_index = NONE;

	for (index = 0; index < door_portals.door_count; index++)
	{
		struct door_portals_door *door = &door_portals.doors[index];

		if (door->object_index == object_index)
			return door;
		if (free_index == NONE && door->frame + 1 < door_portals.frame)
			free_index = index;
	}
	if (free_index == NONE && door_portals.door_count < DOOR_PORTALS_MAXIMUM_DOORS)
		free_index = door_portals.door_count++;
	if (free_index == NONE)
		return NULL;
	memset(&door_portals.doors[free_index], 0, sizeof(door_portals.doors[free_index]));
	door_portals.doors[free_index].object_index = NONE;
	return &door_portals.doors[free_index];
}

/* ---------- each frame */

void door_portals_new_map(
	void)
{
	door_portals.door_count = 0;
	door_portals.shut_count = 0;
	door_portals.blocked_count = 0;
	memset(door_portals.shut, 0, sizeof(door_portals.shut));
	memset(door_portals.light_rows, 0, sizeof(door_portals.light_rows));
}

void door_portals_update(
	void)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	struct object_iterator iterator;
	struct machine_datum *machine;

	memset(door_portals.shut, 0, sizeof(door_portals.shut));
	memset(door_portals.light_rows, 0, sizeof(door_portals.light_rows));
	door_portals.shut_count = 0;
	door_portals.blocked_count = 0;
	door_portals.frame++;
	if (!structure || !config_boolean("display.doors_block_light"))
	{
		/* (forgotten, so that when the setting is on again no door pairs
		from a rest counted before) */
		door_portals.door_count = 0;
		return;
	}
	object_iterator_new(&iterator, _object_mask_machine, 0);
	while ((machine = (struct machine_datum *)object_iterator_next(&iterator)) != NULL)
	{
		struct door_portals_door *door;
		short end, index;

		if (machine_definition_get(machine->definition_index)->machine.type != DOOR_PORTALS_MACHINE_TYPE_DOOR)
			continue;
		door = door_portals_door_get(iterator.index);
		if (!door)
			continue;
		/* (another object, or this one moved or in another level, is paired
		again) */
		if (door->object_index != iterator.index || door->definition_index != machine->definition_index ||
			door->structure != structure || memcmp(&door->position, &machine->object.position, sizeof(door->position)))
		{
			memset(door, 0, sizeof(*door));
			door->object_index = iterator.index;
			door->definition_index = machine->definition_index;
			door->position = machine->object.position;
			door->structure = structure;
			door->rest_end = NONE;
		}
		door->frame = door_portals.frame;
		/* (a moving door is open) */
		end = machine->device.position == 0.0f ? 0 : machine->device.position == 1.0f ? 1 : NONE;
		if (end != door->rest_end)
		{
			door->rest_end = end;
			door->rest_since = game_time_get();
		}
		if (end == NONE)
			continue;
		/* (paired once it has rested there long enough for its pose to
		settle; it shuts nothing there before) */
		if (!door->paired[end])
		{
			if (game_time_get() - door->rest_since < DOOR_PORTALS_REST_TICKS)
				continue;
			door_portals_pair(iterator.index, door, end);
			door->paired[end] = TRUE;
		}
		for (index = 0; index < door->portal_count[end]; index++)
		{
			short portal_index = door->portals[end][index];

			if (!(door_portals.shut[portal_index >> 5] & (1UL << (portal_index & 31))))
			{
				door_portals.shut[portal_index >> 5] |= 1UL << (portal_index & 31);
				door_portals.shut_count++;
			}
		}
	}
}

static boolean door_portals_shut(
	short portal_index)
{
	return portal_index >= 0 && portal_index < DOOR_PORTALS_MAXIMUM_PORTALS &&
		(door_portals.shut[portal_index >> 5] & (1UL << (portal_index & 31))) != 0;
}

/* the clusters a sphere floods into from a cluster through the portals, as
structure_clusters_in_sphere finds them (each portal the sphere meets, to a
cluster not yet reached), but through no shut portal where shut is TRUE. Each
is marked in reached (cleared by the caller). *met_shut (if asked): whether a
shut portal the sphere meets was passed over */
static void door_portals_flood(
	short cluster_index,
	real_point3d const *position,
	real radius,
	boolean shut,
	unsigned long *reached,
	boolean *met_shut)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	short stack[MAXIMUM_CLUSTERS_PER_STRUCTURE];
	short next[MAXIMUM_CLUSTERS_PER_STRUCTURE];
	short depth = 0;
	long cluster_count = structure ? MIN(structure->clusters.count, MAXIMUM_CLUSTERS_PER_STRUCTURE) : 0;

	if (met_shut)
		*met_shut = FALSE;
	if (cluster_index < 0 || cluster_index >= cluster_count)
		return;
	reached[cluster_index >> 5] |= 1UL << (cluster_index & 31);
	stack[depth] = cluster_index;
	next[depth++] = 0;
	while (depth > 0)
	{
		struct door_portals_cluster const *cluster = TAG_BLOCK_GET_ELEMENT(&structure->clusters, stack[depth - 1],
			struct door_portals_cluster);
		short list_index = next[depth - 1]++;
		struct door_portals_cluster_portal const *portal;
		short portal_index, adjacent;

		if (list_index >= cluster->portal_indices.count)
		{
			depth--;
			continue;
		}
		portal_index = *TAG_BLOCK_GET_ELEMENT(&cluster->portal_indices, list_index, short);
		if (portal_index < 0 || portal_index >= structure->cluster_portals.count)
			continue;
		portal = TAG_BLOCK_GET_ELEMENT(&structure->cluster_portals, portal_index, struct door_portals_cluster_portal);
		adjacent = portal->cluster_indices[0] == stack[depth - 1] ? portal->cluster_indices[1] : portal->cluster_indices[0];
		if (adjacent < 0 || adjacent >= cluster_count || (reached[adjacent >> 5] & (1UL << (adjacent & 31))) ||
			!sphere_intersects_cluster_portal(structure, portal_index, position, radius))
		{
			continue;
		}
		if (shut && door_portals_shut(portal_index))
		{
			if (met_shut)
				*met_shut = TRUE;
			continue;
		}
		reached[adjacent >> 5] |= 1UL << (adjacent & 31);
		stack[depth] = adjacent;
		next[depth++] = 0;
	}
}

void door_portals_light(
	long rasterizer_light_index,
	short cluster_index,
	real_point3d const *center,
	real radius)
{
	unsigned long closed[DOOR_PORTALS_CLUSTER_WORDS];
	unsigned long *open;
	boolean met_shut, any = FALSE;
	short index;

	if (!door_portals.shut_count || rasterizer_light_index < 0 || rasterizer_light_index >= MAXIMUM_LIGHTS_PER_WINDOW ||
		door_portals.blocked_count >= DOOR_PORTALS_BLOCKED_LIGHTS)
	{
		return;
	}
	if (cluster_index == NONE)
		return;
	memset(closed, 0, sizeof(closed));
	door_portals_flood(cluster_index, center, radius, TRUE, closed, &met_shut);
	if (!met_shut)
		return;
	open = door_portals.blocked[door_portals.blocked_count];
	memset(open, 0, sizeof(door_portals.blocked[0]));
	door_portals_flood(cluster_index, center, radius, FALSE, open, NULL);
	for (index = 0; index < DOOR_PORTALS_CLUSTER_WORDS; index++)
	{
		open[index] &= ~closed[index];
		if (open[index])
			any = TRUE;
	}
	if (any)
		door_portals.light_rows[rasterizer_light_index] = (unsigned char)++door_portals.blocked_count;
}

boolean door_portals_light_blocked(
	long rasterizer_light_index,
	short cluster_index)
{
	short row;

	if (rasterizer_light_index < 0 || rasterizer_light_index >= MAXIMUM_LIGHTS_PER_WINDOW ||
		cluster_index < 0 || cluster_index >= MAXIMUM_CLUSTERS_PER_STRUCTURE)
	{
		return FALSE;
	}
	row = door_portals.light_rows[rasterizer_light_index];
	return row && (door_portals.blocked[row - 1][cluster_index >> 5] & (1UL << (cluster_index & 31))) != 0;
}

short door_portals_light_filter(
	long rasterizer_light_index,
	short count,
	short *clusters)
{
	short kept = 0, index;

	if (rasterizer_light_index < 0 || rasterizer_light_index >= MAXIMUM_LIGHTS_PER_WINDOW ||
		!door_portals.light_rows[rasterizer_light_index])
	{
		return count;
	}
	for (index = 0; index < count; index++)
	{
		if (!door_portals_light_blocked(rasterizer_light_index, clusters[index]))
			clusters[kept++] = clusters[index];
	}
	return kept;
}
