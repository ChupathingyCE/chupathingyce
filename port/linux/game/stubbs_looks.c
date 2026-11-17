/*
STUBBS_LOOKS.C

Stubbs mode's looks (stubbs_looks.h): what its maps' tags need, beyond their
layouts (stubbs_mode.c), to be drawn as the retail game drew them. Layouts
from Sigmmma's reclaimer (GPL-3.0, stubbs/defs) and the maps' own data.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "stubbs_mode.h"
#include "stubbs_looks.h"
#include "objects/objects.h"

#include <string.h>

#ifndef HALO_CONSOLE_NO_STUBBS

enum
{
	TAG_INSTANCE_SIZE = 0x20,
	TAG_INSTANCE_GROUP_OFFSET = 0x00,
	TAG_INSTANCE_INDEX_OFFSET = 0x0C,
	TAG_INSTANCE_ADDRESS_OFFSET = 0x14,
	TAG_REFERENCE_INDEX_OFFSET = 0x0C,
	/* a unit HUD interface's image effect (Stubbs's own field; the maps'
	unhi tags, e.g. ui\hud\stubbs: image_effects\zombie_color) */
	STUBBS_UNIT_HUD_IMAGE_EFFECT_OFFSET = 0x3D8,
	MAXIMUM_UNIT_HUD_IMAGE_EFFECTS = 32,
	/* a unit's HUDs (Stubbs's unit block at 0x378, read before the unit is
	rewritten in Halo's layout: the first one's interface, ui\hud\stubbs
	for Stubbs) */
	STUBBS_UNIT_HUDS_OFFSET = 0x378,
	MAXIMUM_UNIT_HUDS = 64,
	/* a scenery's vegetation (Stubbs's own field, past Halo's scenery: b10's
	scenery\corn\corn_vegetation names scenery\corn\corn) */
	STUBBS_SCENERY_VEGETATION_OFFSET = 0x1FC,
};

static unsigned char *looks_base;
static unsigned long looks_size;

static unsigned char *looks_host(unsigned long address, unsigned long length)
{
	unsigned long offset = address - STUBBS_XBOX_TAG_CACHE_BASE;

	if (address < STUBBS_XBOX_TAG_CACHE_BASE || offset > looks_size || length > looks_size - offset)
		return NULL;
	return looks_base + offset;
}

static unsigned long looks_read32(unsigned char const *address)
{
	unsigned long value = 0;

	memcpy(&value, address, 4);
	return value;
}

/* a tag index (a datum index: NONE is all ones) */
static long looks_read_index(unsigned char const *address)
{
	int value;

	memcpy(&value, address, 4);
	return value;
}

static struct
{
	long unit_hud_interface;
	long image_effect;
} unit_hud_image_effects[MAXIMUM_UNIT_HUD_IMAGE_EFFECTS];
static short unit_hud_image_effect_count;
static struct
{
	long unit;
	long unit_hud_interface;
} unit_huds[MAXIMUM_UNIT_HUDS];
static short unit_hud_count;

long stubbs_looks_unit_image_effect(
	long unit_definition_index)
{
	short index;
	long unit_hud_interface_index = NONE;

	if (unit_definition_index == NONE)
		return NONE;
	for (index = 0; index < unit_hud_count; index++)
	{
		if (unit_huds[index].unit == unit_definition_index)
			unit_hud_interface_index = unit_huds[index].unit_hud_interface;
	}
	if (unit_hud_interface_index == NONE)
		return NONE;
	for (index = 0; index < unit_hud_image_effect_count; index++)
	{
		if (unit_hud_image_effects[index].unit_hud_interface == unit_hud_interface_index)
			return unit_hud_image_effects[index].image_effect;
	}
	return NONE;
}

boolean stubbs_looks_object_lit_at_body(
	long object_index)
{
	struct object_datum *object;

	if (!stubbs_mode_active() || object_index == NONE)
		return FALSE;
	object = object_get(object_index);
	return TEST_FLAG(_object_mask_unit, object->object.type) ? TRUE : FALSE;
}

void stubbs_looks_object_body_point(
	long object_index,
	real_point3d *point)
{
	struct object_datum *object = object_get(object_index);
	real_matrix4x3 const *nodes = object_get_node_matrices(object_index);

	*point = nodes ? nodes[0].position : object->object.bounding_sphere_center;
}

void stubbs_looks_convert_tags(
	void *tag_cache,
	unsigned long tag_data_size)
{
	unsigned long instances, count, index;
	unsigned char *instance_table;

	if (!stubbs_mode_active() || tag_data_size < 0x28)
		return;
	stubbs_image_effects_reset();
	stubbs_vegetation_reset();
	looks_base = tag_cache;
	looks_size = tag_data_size;
	unit_hud_image_effect_count = 0;
	unit_hud_count = 0;
	instances = looks_read32(looks_base);
	count = looks_read32(looks_base + 0x0C);
	/* (no more instances than the tag data could hold, before the
	multiply: a 32-bit long could wrap on a bad count) */
	if (count > (unsigned long)tag_data_size / TAG_INSTANCE_SIZE)
		return;
	instance_table = looks_host(instances, count * TAG_INSTANCE_SIZE);
	if (!instance_table)
		return;
	for (index = 0; index < count; index++)
	{
		unsigned char *instance = instance_table + index * TAG_INSTANCE_SIZE;
		unsigned long group = looks_read32(instance + TAG_INSTANCE_GROUP_OFFSET);
		unsigned long address = looks_read32(instance + TAG_INSTANCE_ADDRESS_OFFSET);
		unsigned char *definition;

		if (group == 'unhi' && (definition = looks_host(address, STUBBS_UNIT_HUD_IMAGE_EFFECT_OFFSET + 0x10)) != NULL)
		{
			unsigned char *reference = definition + STUBBS_UNIT_HUD_IMAGE_EFFECT_OFFSET;
			long image_effect = looks_read_index(reference + TAG_REFERENCE_INDEX_OFFSET);

			if (looks_read32(reference) == 'imef' && image_effect != NONE &&
				unit_hud_image_effect_count < MAXIMUM_UNIT_HUD_IMAGE_EFFECTS)
			{
				unit_hud_image_effects[unit_hud_image_effect_count].unit_hud_interface =
					looks_read_index(instance + TAG_INSTANCE_INDEX_OFFSET);
				unit_hud_image_effects[unit_hud_image_effect_count].image_effect = image_effect;
				unit_hud_image_effect_count++;
			}
		}
		else if ((group == 'bipd' || group == 'vehi') &&
			(definition = looks_host(address, STUBBS_UNIT_HUDS_OFFSET + 0x0C)) != NULL &&
			looks_read32(definition + STUBBS_UNIT_HUDS_OFFSET) > 0 && unit_hud_count < MAXIMUM_UNIT_HUDS)
		{
			unsigned char const *reference = looks_host(looks_read32(definition + STUBBS_UNIT_HUDS_OFFSET + 4), 0x10);

			if (reference && looks_read32(reference) == 'unhi')
			{
				unit_huds[unit_hud_count].unit = looks_read_index(instance + TAG_INSTANCE_INDEX_OFFSET);
				unit_huds[unit_hud_count].unit_hud_interface = looks_read_index(reference + TAG_REFERENCE_INDEX_OFFSET);
				unit_hud_count++;
			}
		}
		else if (group == 'scen' &&
			(definition = looks_host(address, STUBBS_SCENERY_VEGETATION_OFFSET + 0x10)) != NULL &&
			looks_read32(definition + STUBBS_SCENERY_VEGETATION_OFFSET) == 'vege')
		{
			stubbs_vegetation_note(looks_read_index(instance + TAG_INSTANCE_INDEX_OFFSET),
				looks_read_index(definition + STUBBS_SCENERY_VEGETATION_OFFSET + TAG_REFERENCE_INDEX_OFFSET));
		}
	}
	stubbs_mode_log("%d unit HUD interfaces name an image effect, %d units a HUD", unit_hud_image_effect_count, unit_hud_count);
}

#endif
