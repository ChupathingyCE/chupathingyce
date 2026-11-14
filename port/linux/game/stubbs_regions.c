/*
STUBBS_REGIONS.C
Stubbs's characters have more regions than Halo's objects hold: a cop's
legs, skull, taser and torso are its regions 8 to 11, a militia's torso its
region 9, an army soldier's its region 8, and a40's skyline model has 26,
while an object keeps a permutation for 8 regions
(object_datum.region_permutations, MAXIMUM_REGIONS_PER_OBJECT). The
renderer draws the regions the permutations it is given cover, so those
regions were never drawn: cops without legs or a chest, militia and soldiers
without a chest, towers missing from the skyline.

In Stubbs mode an object's permutations live here, sixteen per object, and
the object's own eight are kept a copy of the first eight (saved games, the
network and Halo's code read those). A row belongs to one object by its
datum index: a new object, or a saved one restored into a slot, starts from
the object's eight, the rest at their first permutation. Halo mode never
comes here (stubbs_mode.c).
*/

#include "stubbs_regions.h"

#ifndef HALO_CONSOLE_NO_STUBBS

#include "stubbs_mode.h"
#include "objects/objects.h"
#include "halo_port_capacity.h"

#include <string.h>

struct stubbs_region_row
{
	long object_index;
	byte permutations[STUBBS_MAXIMUM_REGIONS_PER_OBJECT];
};

static struct stubbs_region_row stubbs_region_rows[HALO_PORT_MAXIMUM_OBJECTS_PER_MAP];

static struct stubbs_region_row *stubbs_regions_row(
	long object_index,
	struct object_datum **object_out)
{
	struct object_datum *object;
	struct stubbs_region_row *row;
	unsigned long absolute_index;

	if (object_index == NONE || !stubbs_mode_active())
		return NULL;
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (absolute_index >= HALO_PORT_MAXIMUM_OBJECTS_PER_MAP)
		return NULL;
	object = object_try_and_get(object_index);
	if (!object)
		return NULL;
	row = &stubbs_region_rows[absolute_index];
	if (row->object_index != object_index)
	{
		/* (another object had the slot, or a saved one came back) */
		memset(row->permutations, 0, sizeof(row->permutations));
		memcpy(row->permutations, object->object.region_permutations,
			sizeof(object->object.region_permutations));
		row->object_index = object_index;
	}
	if (object_out)
		*object_out = object;
	return row;
}

byte *stubbs_regions_permutations(
	long object_index)
{
	struct stubbs_region_row *row = stubbs_regions_row(object_index, NULL);

	return row ? row->permutations : NULL;
}

void stubbs_regions_object_new(
	long object_index)
{
	struct stubbs_region_row *row = stubbs_regions_row(object_index, NULL);

	if (row)
	{
		/* (the slot's last object is forgotten: the row starts from this one's) */
		row->object_index = NONE;
		stubbs_regions_row(object_index, NULL);
	}
}

void stubbs_regions_changed(
	long object_index)
{
	struct object_datum *object = NULL;
	struct stubbs_region_row *row = stubbs_regions_row(object_index, &object);

	if (row && object)
	{
		memcpy(object->object.region_permutations, row->permutations,
			sizeof(object->object.region_permutations));
	}
}

#endif
