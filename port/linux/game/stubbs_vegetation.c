/*
STUBBS_VEGETATION.C

Stubbs mode's vegetation (stubbs_looks.h): b10's corn. Stubbs's scenery may
name a vegetation (vege) tag at 0x1FC, past Halo's scenery; b10_farm_house
places six such scenery (scenery\corn\corn_vegetation and _rotated), each a
patch as wide as its bounding radius (13 world units), and the retail game
fills each with instances of the vegetation's model. Here it is drawn
simply: the model (vege 0x40, scenery\corn\corn) on a grid of the
vegetation's spacing (0x50, 0x54: 4 by 4), each moved within its cell and
turned by a hash of its place (never the game's random numbers), stood on
the ground below it where that ground is the patch's own (the field, not
the road through it), and lit as the patch's scenery is. The two shaders the
vegetation names (0x10, 0x20: a front and a side view, likely the far
impostors) are not used yet. Nothing here is game state: the instances are
only drawn, with the objects (render_objects.c).

The vege layout is read from the maps' own values (Sigmmma's reclaimer has
no definition for it).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "stubbs_looks.h"

#ifndef HALO_CONSOLE_NO_STUBBS

#include "math/real_math.h"
#include "models/model_definitions.h"
#include "models/models.h"
#include "objects/object_definitions.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "rasterizer/rasterizer_model_types.h"
#include "render/render.h"
#include "render/render_cameras_internal.h"
#include "tag_files/tag_groups.h"
#include "stubbs_mode.h"

#include <math.h>
#include <string.h>

/* (cache_files.c) */
boolean tag_index_is_group(long tag_index, long group_tag);

#define VEGETATION_TAG 'vege'

enum
{
	VEGETATION_MODEL_OFFSET = 0x40,
	/* the rows' spacing (0.45 by 0.3 world units; the rotated corn's turned
	the other way, 0.3 by 0.45), and a larger spacing (4 by 4) */
	VEGETATION_ROW_SPACING_OFFSET = 0x04,
	VEGETATION_SPACING_OFFSET = 0x50,
	TAG_REFERENCE_INDEX_OFFSET = 0x0C,

	MAXIMUM_VEGETATED_SCENERY_DEFINITIONS = 16,
	MAXIMUM_PATCHES = 32,
	MAXIMUM_INSTANCES = 32768,
	/* (drawn only this near the camera, in world units) */
	MAXIMUM_DRAWN_DISTANCE = 24,
	/* (past this, every other one; past twice it, one in three) */
	THINNED_DISTANCE = 11,
	MAXIMUM_NODES = 16,
	MAXIMUM_REGIONS = 32,
};

static struct
{
	long scenery;
	long vegetation;
} vegetated_definitions[MAXIMUM_VEGETATED_SCENERY_DEFINITIONS];
static short vegetated_definition_count;

/* the instances, placed the first time each patch is drawn */
struct vegetation_instance
{
	real_point3d position;
	real yaw;
};

static struct
{
	long object_index;
	long model_index;
	long first_instance;
	long instance_count;
} patches[MAXIMUM_PATCHES];
static short patch_count;
static struct vegetation_instance instances[MAXIMUM_INSTANCES];
static long instance_count;
static boolean logged;

void stubbs_vegetation_reset(
	void)
{
	vegetated_definition_count = 0;
	patch_count = 0;
	instance_count = 0;
	logged = FALSE;
}

void stubbs_vegetation_note(
	long scenery_index,
	long vegetation_index)
{
	if (vegetation_index == NONE || vegetated_definition_count >= MAXIMUM_VEGETATED_SCENERY_DEFINITIONS)
		return;
	vegetated_definitions[vegetated_definition_count].scenery = scenery_index;
	vegetated_definitions[vegetated_definition_count].vegetation = vegetation_index;
	vegetated_definition_count++;
}

static long vegetation_of_definition(
	long definition_index)
{
	short index;

	for (index = 0; index < vegetated_definition_count; index++)
	{
		if (vegetated_definitions[index].scenery == definition_index)
			return vegetated_definitions[index].vegetation;
	}
	return NONE;
}

static real vegetation_read_real(
	byte const *address)
{
	real value;

	memcpy(&value, address, 4);
	return value;
}

static long vegetation_read32(
	byte const *address)
{
	int value;

	memcpy(&value, address, 4);
	return value;
}

/* a hash of two whole numbers to [0, 1) */
static real vegetation_hash(
	unsigned int a,
	unsigned int b)
{
	unsigned int value = a * 0x9E3779B1U ^ (b + 0x7F4A7C15U) * 0x85EBCA77U;

	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	value ^= value >> 16;
	return (real)(value & 0xFFFFFF) / (real)0x1000000;
}

/* the patch around a scenery: its instances placed on a grid over its
bounding radius, each on the ground below it */
static short vegetation_patch(
	long object_index,
	long vegetation_index)
{
	struct object_datum *object = object_get(object_index);
	byte const *vegetation;
	real spacing_x, spacing_y, radius;
	long model_index;
	short i, j, cells;
	short ground_material;
	long elsewhere = 0;
	short patch;

	for (patch = 0; patch < patch_count; patch++)
	{
		if (patches[patch].object_index == object_index)
			return patch;
	}
	if (patch_count >= MAXIMUM_PATCHES || !tag_index_is_group(vegetation_index, VEGETATION_TAG))
		return NONE;
	vegetation = (byte const *)tag_get(VEGETATION_TAG, vegetation_index);
	model_index = vegetation_read32(vegetation + VEGETATION_MODEL_OFFSET + TAG_REFERENCE_INDEX_OFFSET);
	if (model_index == NONE || !tag_index_is_group(model_index, MODELS_GROUP_TAG))
		return NONE;
	spacing_x = vegetation_read_real(vegetation + VEGETATION_ROW_SPACING_OFFSET);
	spacing_y = vegetation_read_real(vegetation + VEGETATION_ROW_SPACING_OFFSET + 4);
	if (!(spacing_x >= 0.1f && spacing_x <= 16.0f) || !(spacing_y >= 0.1f && spacing_y <= 16.0f))
	{
		spacing_x = vegetation_read_real(vegetation + VEGETATION_SPACING_OFFSET);
		spacing_y = vegetation_read_real(vegetation + VEGETATION_SPACING_OFFSET + 4);
		if (!(spacing_x >= 0.5f && spacing_x <= 16.0f) || !(spacing_y >= 0.5f && spacing_y <= 16.0f))
			spacing_x = spacing_y = 4.0f;
	}
	radius = object_definition_get(object->definition_index)->object.bounding_radius;
	if (!(radius > 0.0f) || radius > 64.0f)
		radius = 13.0f;

	patch = patch_count++;
	patches[patch].object_index = object_index;
	patches[patch].model_index = model_index;
	patches[patch].first_instance = instance_count;
	patches[patch].instance_count = 0;
	/* (the ground's material under the scenery: corn grows only on it, not
	on the road or the yard beside it) */
	{
		real_point3d point = object->object.position;
		real_vector3d down = { 0.0f, 0.0f, -12.0f };
		struct collision_result collision;

		point.z += 4.0f;
		ground_material = collision_test_vector(FLAG(_collision_test_structure_bit) |
			FLAG(_collision_test_front_facing_surfaces_bit), &point, &down, NONE, &collision) ?
			collision.material_index : NONE;
	}
	cells = (short)(radius / MIN(spacing_x, spacing_y)) + 1;
	for (i = -cells; i <= cells; i++)
	{
		for (j = -cells; j <= cells; j++)
		{
			unsigned int key = (unsigned int)(object_index & 0xFFFF) * 4099U + (unsigned int)((i + 512) * 1031 + (j + 512));
			real_point3d point;
			real_vector3d down;
			struct collision_result collision;
			real dx = ((real)i + (vegetation_hash(key, 1) - 0.5f) * 0.5f) * spacing_x;
			real dy = ((real)j + (vegetation_hash(key, 2) - 0.5f) * 0.5f) * spacing_y;

			if (dx * dx + dy * dy > radius * radius || instance_count >= MAXIMUM_INSTANCES)
				continue;
			point.x = object->object.position.x + dx;
			point.y = object->object.position.y + dy;
			point.z = object->object.position.z + 4.0f;
			down.i = 0.0f;
			down.j = 0.0f;
			down.k = -12.0f;
			if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
				&point, &down, NONE, &collision))
			{
				if (ground_material != NONE && collision.material_index != ground_material)
				{
					elsewhere++;
					continue;
				}
				point = collision.point;
			}
			else
			{
				point.z = object->object.position.z;
			}
			instances[instance_count].position = point;
			instances[instance_count].yaw = vegetation_hash(key, 3) * 2.0f * 3.14159265f;
			instance_count++;
			patches[patch].instance_count++;
		}
	}
	stubbs_mode_log("vegetation: %ld %s instances around %s (%ld on other ground left out)",
		patches[patch].instance_count, tag_get_name(model_index), tag_get_name(object->definition_index), elsewhere);
	return patch;
}

void stubbs_vegetation_render(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	static char region_permutations[MAXIMUM_REGIONS];
	static real_rgb_color change_colors[4];
	static real function_values[4];
	real_matrix4x3 nodes[MAXIMUM_NODES];

	if (!stubbs_mode_active() || vegetated_definition_count == 0)
		return;
	object_iterator_new(&iterator, _object_mask_scenery, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		long vegetation_index = vegetation_of_definition(object->definition_index);
		struct render_lighting lighting;
		struct render_model_effect model_effect;
		struct model *model;
		short patch, node_count, node;
		long instance;
		real model_radius;

		if (vegetation_index == NONE)
			continue;
		patch = vegetation_patch(iterator.index, vegetation_index);
		if (patch == NONE)
			continue;
		/* (the whole patch, before any of it) */
		if (!render_frustum_sphere_visible(&render.frustum, &object->object.position,
			object_definition_get(object->definition_index)->object.bounding_radius + 4.0f))
			continue;
		model = model_definition_get(patches[patch].model_index);
		node_count = (short)MIN(MAX(model->nodes.count, 1), MAXIMUM_NODES);
		model_radius = 2.0f;
		lighting = *object_get_cached_render_lighting(iterator.index, 100.0f);
		csmemset(&model_effect, 0, sizeof(model_effect));
		model_effect.type = 0;
		for (instance = patches[patch].first_instance;
			instance < patches[patch].first_instance + patches[patch].instance_count;
			instance++)
		{
			struct vegetation_instance const *placed = &instances[instance];
			real_point3d centroid = placed->position;
			real pixels;
			real c = (real)cos(placed->yaw);
			real s = (real)sin(placed->yaw);

			centroid.z += 1.0f;
			{
				real dx = centroid.x - render.camera.position.x;
				real dy = centroid.y - render.camera.position.y;
				real dz = centroid.z - render.camera.position.z;

				real distance_squared = dx * dx + dy * dy + dz * dz;

				if (distance_squared > MAXIMUM_DRAWN_DISTANCE * MAXIMUM_DRAWN_DISTANCE)
					continue;
				if (distance_squared > 4 * THINNED_DISTANCE * THINNED_DISTANCE ? instance % 3 :
					distance_squared > THINNED_DISTANCE * THINNED_DISTANCE ? instance % 2 : 0)
					continue;
			}
			if (!render_frustum_sphere_visible(&render.frustum, &centroid, model_radius))
				continue;
			pixels = render_frustum_sphere_diameter_in_pixels(&render.frustum, &centroid, model_radius);
			if (pixels < 3.0f)
				continue;
			{
				real_matrix4x3 placement;

				placement.scale = 1.0f;
				placement.forward.i = c;
				placement.forward.j = s;
				placement.forward.k = 0.0f;
				placement.left.i = -s;
				placement.left.j = c;
				placement.left.k = 0.0f;
				placement.up.i = 0.0f;
				placement.up.j = 0.0f;
				placement.up.k = 1.0f;
				placement.position = placed->position;
				/* each node where the model's default pose has it, from the
				placement (the corn's one node is turned up from lying on its
				side) */
				for (node = 0; node < node_count; node++)
				{
					struct model_node const *definition = TAG_BLOCK_GET_ELEMENT(&model->nodes, node, struct model_node);
					real_matrix4x3 local;
					short parent = definition->parent_node_index;

					matrix4x3_from_point_and_quaternion(&local, &definition->default_translation,
						&definition->default_rotation);
					matrix4x3_multiply(parent >= 0 && parent < node ? &nodes[parent] : &placement, &local, &nodes[node]);
				}
			}
			render_model(
				patches[patch].model_index,
				pixels,
				nodes,
				region_permutations,
				change_colors,
				function_values,
				&lighting,
				&centroid,
				model_radius,
				&model_effect,
				iterator.index,
				NONE,
				0);
		}
	}
	if (!logged)
	{
		logged = TRUE;
		stubbs_mode_log("vegetation: %d patches, %ld instances", patch_count, instance_count);
	}
}

#endif
