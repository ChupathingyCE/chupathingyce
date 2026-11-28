/*
CLUSTER_OBJECT_LISTS.C (test)

The object bounds cache's lists of a cluster's objects (object_bounds_cache.c)
over a fake world of clusters whose objects move, are deleted and come back
as another type: each list must be the cluster's objects of the types asked
for, bipeds not, in the cluster's order, as collision's object loop would
have walked them. test_cluster_object_lists.py takes the enums (enums.inc)
and the code (under_test.inc) from the sources.

Dynamic Lighting, Dynamic Shadows and shared renderer optimisations by
chestahh (https://github.com/chestahh), originally developed for GulchCE
(https://github.com/chestahh/GulchCE). Original OpenCE port:
https://github.com/chestahh/GulchCE/tree/035f47bb8fc682675456323dbb3095b506502ec6
From GulchCE by chestahh (granted 2026-11-26). Changed for Chupathingy (see the git history).

SPDX-FileCopyrightText: 2026 chestahh (GulchCE)
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "harness.h"
#include "enums.inc"

struct object_datum { struct { short type; } object; };
enum { OBJECTS = 700, CLUSTERS = 24 };
static struct object_datum objects[OBJECTS];
#define OBJECT_INDEX(i) ((long)((((i) * 5 + 3) & 0x7FFF) << 16) | (i))
#define object_get(object_index) (&objects[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)])

/* the clusters' lists, and their changes as the cluster partition counts them */
static long world[CLUSTERS][OBJECTS], world_counts[CLUSTERS];
static unsigned long world_changes[CLUSTERS];
static long cluster_get_first_collideable_object(long *reference, short cluster)
{
	*reference = cluster * OBJECTS;
	return world_counts[cluster] ? world[cluster][0] : NONE;
}
static long cluster_get_next_collideable_object(long *reference)
{
	long cluster = *reference / OBJECTS, place = *reference % OBJECTS + 1;

	*reference = cluster * OBJECTS + place;
	return place < world_counts[cluster] ? world[cluster][place] : NONE;
}
static unsigned long cluster_port_collideable_object_changes(short cluster)
{
	return cluster >= 0 && cluster < CLUSTERS ? world_changes[cluster] : 0;
}

/* (object_bounds_cache.c's, replaced with the game state) */
static long object_bounds_epoch = 1;

#include "under_test.inc"

static unsigned long seed = 12345;
static unsigned long next_random(void)
{
	seed = seed * 1103515245UL + 12345UL;
	return (seed >> 8) & 0xFFFFFF;
}

static short const types[] = { _object_type_biped, _object_type_vehicle, _object_type_weapon, _object_type_equipment,
	_object_type_garbage, _object_type_projectile, _object_type_scenery, _object_type_machine, _object_type_control,
	_object_type_light_fixture, _object_type_placeholder, _object_type_sound_scenery };
#define NUMBEROF_TYPES ((int)(sizeof(types) / sizeof(types[0])))

/* every object in one to three clusters, in a random order; counted: each list
that changed counts a change, as a reconnect or disconnect does */
static void place(boolean counted)
{
	static long before[CLUSTERS][OBJECTS], before_counts[CLUSTERS];
	long index;
	int cluster;

	memcpy(before, world, sizeof(before));
	memcpy(before_counts, world_counts, sizeof(before_counts));
	memset(world_counts, 0, sizeof(world_counts));
	for (index = 0; index < OBJECTS; index++)
	{
		int count = 1 + (int)(next_random() % 3), first = (int)(next_random() % CLUSTERS), n;

		if (objects[index].object.type < 0)
			continue;
		for (n = 0; n < count; n++)
		{
			int at = (first + n) % CLUSTERS;
			long slot = (long)(next_random() % (unsigned long)(world_counts[at] + 1));

			memmove(&world[at][slot + 1], &world[at][slot], (size_t)(world_counts[at] - slot) * sizeof(long));
			world[at][slot] = OBJECT_INDEX(index);
			world_counts[at]++;
		}
	}
	for (cluster = 0; counted && cluster < CLUSTERS; cluster++)
		if (world_counts[cluster] != before_counts[cluster] ||
			memcmp(world[cluster], before[cluster], (size_t)world_counts[cluster] * sizeof(long)))
			world_changes[cluster]++;
}

static void populate(void)
{
	long index;

	for (index = 0; index < OBJECTS; index++)
		objects[index].object.type = types[next_random() % NUMBEROF_TYPES];
	place(TRUE);
}

/* each cluster's list against its objects of the types asked for, bipeds not,
in order: for `sets` sets of types in turn (a cluster keeps two lists) */
static long listed;
static boolean lists_match(int sets)
{
	static unsigned long const type_sets[] = {
		FLAG(_collision_test_objects_scenery_bit) | FLAG(_collision_test_objects_machines_bit),
		FLAG(_collision_test_objects_vehicles_bit) | FLAG(_collision_test_objects_scenery_bit) |
			FLAG(_collision_test_objects_machines_bit),
		_collision_test_objects_all_types_flags & ~FLAG(_collision_test_objects_bipeds_bit),
	};
	int set, cluster;

	for (set = 0; set < sets * 2; set++)
	{
		unsigned long flags = FLAG(_collision_test_objects_bit) | type_sets[set % sets];

		for (cluster = 0; cluster < CLUSTERS; cluster++)
		{
			long count = -1, expected = 0, at;
			long const *list = object_bounds_cache_cluster_objects((short)cluster, flags, &count);

			CHECK(list, "no list for cluster %d", cluster);
			for (at = 0; at < world_counts[cluster]; at++)
			{
				short type = object_get(world[cluster][at])->object.type;

				if (type == _object_type_biped || !TEST_FLAG(flags, type + _collision_test_objects_first_type_bit))
					continue;
				if (expected >= count || list[expected] != world[cluster][at])
					return FALSE;
				expected++;
			}
			if (expected != count)
				return FALSE;
			listed += count;
		}
	}
	return TRUE;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	populate();

	/* as objects move between clusters and the types asked for change
	(more sets than a cluster keeps) */
	CASE("matches")
	{
		int round;

		for (round = 0; round < 8; round++)
		{
			CHECK(lists_match(round % 2 ? 2 : 3), "round %d: a cluster's list differs from its objects", round);
			place(TRUE);
		}
		CHECK(listed > 3000, "only %ld objects listed: too few to compare", listed);
		return 0;
	}
	/* objects deleted, and their datums reused by objects of another type */
	CASE("deleted")
	{
		long index;

		CHECK(lists_match(2), "before");
		for (index = 0; index < OBJECTS; index += 7)
			objects[index].object.type = -1;
		place(TRUE);
		CHECK(lists_match(2), "after deletions");
		for (index = 0; index < OBJECTS; index += 7)
			objects[index].object.type = index % 2 ? _object_type_scenery : _object_type_machine;
		place(TRUE);
		CHECK(lists_match(3), "after the datums were reused");
		return 0;
	}
	/* the game state replaced: the lists made anew though no change was counted */
	CASE("invalidated")
	{
		CHECK(lists_match(2), "before");
		place(FALSE);
		object_bounds_epoch++;
		CHECK(lists_match(2), "a list kept across a new game state");
		return 0;
	}
	/* a change not counted is not seen: the counts are what keeps the lists right */
	CASE("detects-stale")
	{
		CHECK(lists_match(2), "before");
		place(FALSE);
		CHECK(!lists_match(2), "the lists changed with no change counted and nothing told");
		return 0;
	}
	/* no list for a vector that tests for bipeds, nor for a cluster out of range */
	CASE("refused")
	{
		long count = 0;

		CHECK(!object_bounds_cache_cluster_objects(0, FLAG(_collision_test_objects_bipeds_bit) |
			FLAG(_collision_test_objects_scenery_bit), &count), "a list for a vector that tests for bipeds");
		CHECK(!object_bounds_cache_cluster_objects(NONE, FLAG(_collision_test_objects_scenery_bit), &count),
			"a list for no cluster");
		CHECK(!object_bounds_cache_cluster_objects(MAXIMUM_CLUSTERS_PER_STRUCTURE,
			FLAG(_collision_test_objects_scenery_bit), &count), "a list past the clusters");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
