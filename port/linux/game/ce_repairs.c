/*
CE_REPAIRS.C

Custom Edition and HaloMD maps' tags repaired where Halo PC's engine let
them be (cache_files.c, the maps past the Xbox's). Such maps were built by
Halo PC's tools and by others that edited the tags afterwards, and some have
things Halo PC's engine read without checking them, where this one asserts
or reads past a tag's end:

  - predicted resources (what the game reads ahead when an object, a
    first-person weapon, the UI or a BSP's cluster comes into view) naming a
    tag that is not a bitmap or sound, or a bitmap past the bitmap tag's:
    dropped (they are only read ahead);
  - predicted resources naming their tag with the salt of another build of
    the map's tags: given the tag's own (Halo PC's engine took the index
    alone, as cache_files.c does for these maps);
  - an object tag whose type is not its group's (a HaloMD map's scenery
    typed a light fixture, which Halo PC's engine made a light fixture of,
    reading the rest of a light fixture past the scenery's end): given its
    group's type;
  - an object's modifier shader that is not a shader, or of a type that
    cannot modify an object (an environment or model shader: Halo PC's
    engine, and this one, draw the object without it, this one logging
    each frame it is drawn): none;
  - a model's shader naming a tag that is not a shader (a HaloMD map's
    model has a light for one, which Halo PC's engine drew with whatever it
    found there): given the model's first shader that is one, or else the
    map's first shader; one naming a shader with the salt of another build:
    given the shader's own.

The repairs are made before the map is opened, to the image it is checked
in (ce_map_checks.c, so that its checks see the map as it will play), and
again when its tags load and when each of its BSPs loads. Nothing here
touches an Xbox map: only the maps in the slot past the Xbox's are read this
way (cache_files_windows.c).
*/

#ifdef HALO_CUSTOM_EDITION

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "structures/structure_bsp_definitions.h"
#include "ce_map_checks.h"

#include <string.h>

/* ---------- constants */

enum
{
	CE_TAG_INSTANCE_SIZE = 0x20,
	/* (a tag block's or tag data's count or size: larger ones would not
	fit in a tag cache in any case) */
	CE_MAXIMUM_ELEMENTS = 0x10000,

	/* predicted resources (predicted_resources.h): their type, resource
	index and tag index */
	PREDICTED_RESOURCE_SIZE = 0x08,
	PREDICTED_RESOURCE_BITMAP = 0,
	PREDICTED_RESOURCE_SOUND = 1,
	/* their blocks: an object's (object_definitions.h), a weapon's first
	person's (weapon_definitions.h) and the scenario's UI's
	(scenario_definitions.h) */
	OBJECT_PREDICTED_RESOURCES_OFFSET = 0x170,
	WEAPON_PREDICTED_RESOURCES_OFFSET = 0x4e4,
	SCENARIO_PREDICTED_RESOURCES_OFFSET = 0xec,
	/* an object's modifier shader (object_definitions.h: a tag reference,
	its tag index last) */
	OBJECT_MODIFIER_SHADER_OFFSET = 0x90,
	TAG_REFERENCE_INDEX_OFFSET = 0x0c,
	/* a shader's type (shader_definitions.h), Halo PC's: those that can be
	modifiers (shaders.c, shader_type_is_valid_for_modifier: 1, and
	transparent to plasma, with Halo PC's extended chicago at 7) */
	SHADER_TYPE_OFFSET = 0x24,
	SHADER_TYPE_FIRST_MODIFIER = 5,
	SHADER_TYPE_LAST_MODIFIER = 11,
	/* (bitmap_group.h: its bitmaps block) */
	BITMAP_GROUP_BITMAPS_OFFSET = 0x60,

	/* a model's shaders (model_definitions.h): each a tag reference and
	a permutation */
	MODEL_SHADERS_OFFSET = 0xdc,
	MODEL_SHADER_SIZE = 0x20,
	MODEL_HEADER_SIZE = 0xe8,
	SHADER_GROUP = 'shdr',
};

/* ---------- structures */

/* (cache_files.c's) */
struct ce_tag_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	unsigned long tag_index;
	unsigned long name;
	unsigned long base_address;
	unsigned long indexed;
	unsigned long unused;
};

/* what was repaired, for the log */
struct ce_repair_counts
{
	long predicted_resources_dropped;
	long predicted_resources_salted;
	long object_types;
	long modifier_shaders;
	long model_shaders;
};

/* ---------- globals */

/* the object types' groups, by type (object_definitions.h, objects.c) */
static unsigned long const ce_object_type_groups[] =
{
	'bipd', 'vehi', 'weap', 'eqip', 'garb', 'proj', 'scen', 'mach', 'ctrl', 'lifi', 'plac', 'ssce',
};

/* the loaded map's tags (ce_repairs_tags_loaded), for its BSPs' */
static struct ce_tag_instance *ce_loaded_instances;
static long ce_loaded_tag_count;

/* ---------- private code */

static unsigned long ce_read_long(
	byte const *at)
{
	unsigned long value;

	memcpy(&value, at, sizeof(value));
	return value;
}

static void ce_write_long(
	byte *at,
	unsigned long value)
{
	memcpy(at, &value, sizeof(value));
}

static short ce_read_short(
	byte const *at)
{
	short value;

	memcpy(&value, at, sizeof(value));
	return value;
}

static struct ce_tag_instance *ce_instance(
	void *tag_instances,
	long index)
{
	return (struct ce_tag_instance *)((byte *)tag_instances + index * CE_TAG_INSTANCE_SIZE);
}

/* the instance a tag index names by its index alone (as Halo PC's engine
took it), or NULL */
static struct ce_tag_instance *ce_instance_by_index(
	void *tag_instances,
	long tag_count,
	unsigned long tag_index)
{
	if (tag_index == 0xffffffff || (tag_index & 0xffff) >= (unsigned long)tag_count)
		return NULL;
	return ce_instance(tag_instances, (long)(tag_index & 0xffff));
}

static boolean ce_is_of_group(
	struct ce_tag_instance const *instance,
	unsigned long group_tag)
{
	return instance->group_tag == group_tag || instance->parent_group_tags[0] == group_tag ||
		instance->parent_group_tags[1] == group_tag;
}

static boolean ce_is_shader(
	struct ce_tag_instance const *instance)
{
	return instance->group_tag == 'scex' || ce_is_of_group(instance, SHADER_GROUP);
}

/* a tag block at field (count, address): its elements, if all of them are
in the image (no refusal: the checks refuse what they read) */
static byte *ce_block(
	struct ce_image const *image,
	byte const *field,
	unsigned long element_size,
	long *count)
{
	long block_count = (long)ce_read_long(field);
	byte *elements;

	*count = 0;
	if (block_count <= 0 || block_count > CE_MAXIMUM_ELEMENTS)
		return NULL;
	elements = ce_image_pointer(image, ce_read_long(field + 4), (unsigned long)block_count * element_size);
	if (elements)
		*count = block_count;
	return elements;
}

/* a predicted resources block: those naming nothing they can be dropped
(the rest moved down over them, the count lessened), those naming their tag
with another salt given its own */
static void ce_predicted_resources_repair(
	struct ce_image const *image,
	byte *field,
	void *tag_instances,
	long tag_count,
	struct ce_repair_counts *counts)
{
	long count, index, kept = 0;
	byte *resources = ce_block(image, field, PREDICTED_RESOURCE_SIZE, &count);

	for (index = 0; index < count; index++)
	{
		byte *resource = resources + index * PREDICTED_RESOURCE_SIZE;
		short type = ce_read_short(resource);
		short resource_index = ce_read_short(resource + 2);
		unsigned long tag_index = ce_read_long(resource + 4);
		struct ce_tag_instance *instance = ce_instance_by_index(tag_instances, tag_count, tag_index);
		boolean valid = TRUE;

		if (type == PREDICTED_RESOURCE_BITMAP)
		{
			byte *group = instance && instance->group_tag == 'bitm' ?
				ce_image_pointer(image, instance->base_address, BITMAP_GROUP_BITMAPS_OFFSET + 4) : NULL;

			valid = group && resource_index >= 0 &&
				(unsigned long)resource_index < ce_read_long(group + BITMAP_GROUP_BITMAPS_OFFSET);
		}
		else if (type == PREDICTED_RESOURCE_SOUND)
			valid = instance && instance->group_tag == 'snd!';
		if (!valid)
		{
			counts->predicted_resources_dropped++;
			continue;
		}
		if (instance && instance->tag_index != tag_index)
		{
			ce_write_long(resource + 4, instance->tag_index);
			counts->predicted_resources_salted++;
		}
		if (kept != index)
			memmove(resources + kept * PREDICTED_RESOURCE_SIZE, resource, PREDICTED_RESOURCE_SIZE);
		kept++;
	}
	if (kept != count)
		ce_write_long(field, (unsigned long)kept);
}

/* an object tag's type made its group's, if it is not */
static void ce_object_type_repair(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	struct ce_repair_counts *counts)
{
	byte *object = ce_image_pointer(image, instance->base_address, OBJECT_PREDICTED_RESOURCES_OFFSET + 0xc);
	short type, group_type;

	if (!object)
		return;
	for (group_type = 0; group_type < (short)NUMBEROF(ce_object_type_groups); group_type++)
	{
		if (ce_object_type_groups[group_type] == instance->group_tag)
			break;
	}
	type = ce_read_short(object);
	if (group_type >= (short)NUMBEROF(ce_object_type_groups) || type == group_type)
		return;
	error(_error_silent, "%s map: object %s is typed %d, not its group's %d (made its group's)",
		ce_map_cache_version == CE_CACHE_VERSION_RETAIL ? "HaloMD" : "Custom Edition",
		ce_image_tag_name(image, instance), type, group_type);
	memcpy(object, &group_type, sizeof(group_type));
	counts->object_types++;
}

/* an object's modifier shader made none if it is not a shader, or not of
a type that can be one (render_objects.c draws the object without it) */
static void ce_modifier_shader_repair(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	void *tag_instances,
	long tag_count,
	struct ce_repair_counts *counts)
{
	byte *reference = ce_image_pointer(image, instance->base_address + OBJECT_MODIFIER_SHADER_OFFSET, 0x10);
	struct ce_tag_instance *shader;
	byte *data;
	short type;

	if (!reference || ce_read_long(reference + TAG_REFERENCE_INDEX_OFFSET) == 0xffffffff)
		return;
	shader = ce_instance_by_index(tag_instances, tag_count, ce_read_long(reference + TAG_REFERENCE_INDEX_OFFSET));
	data = shader && ce_is_shader(shader) ? ce_image_pointer(image, shader->base_address, SHADER_TYPE_OFFSET + 2) :
		NULL;
	type = data ? ce_read_short(data + SHADER_TYPE_OFFSET) : -1;
	if (type == 1 || (type >= SHADER_TYPE_FIRST_MODIFIER && type <= SHADER_TYPE_LAST_MODIFIER))
	{
		if (shader->tag_index != ce_read_long(reference + TAG_REFERENCE_INDEX_OFFSET))
			ce_write_long(reference + TAG_REFERENCE_INDEX_OFFSET, shader->tag_index);
		return;
	}
	ce_write_long(reference + TAG_REFERENCE_INDEX_OFFSET, 0xffffffff);
	counts->modifier_shaders++;
}

/* a model's shaders that name a tag that is not a shader (or a shader of
another salt) repaired */
static void ce_model_shaders_repair(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	void *tag_instances,
	long tag_count,
	struct ce_repair_counts *counts)
{
	byte *model = ce_image_pointer(image, instance->base_address, MODEL_HEADER_SIZE);
	byte *shaders;
	long count, index;
	struct ce_tag_instance *substitute = NULL;

	if (!model)
		return;
	shaders = ce_block(image, model + MODEL_SHADERS_OFFSET, MODEL_SHADER_SIZE, &count);
	for (index = 0; index < count; index++)
	{
		byte *reference = shaders + index * MODEL_SHADER_SIZE;
		struct ce_tag_instance *shader = ce_instance_by_index(tag_instances, tag_count, ce_read_long(reference + 0xc));

		if (shader && ce_is_shader(shader))
		{
			if (shader->tag_index != ce_read_long(reference + 0xc))
				ce_write_long(reference + 0xc, shader->tag_index);
			continue;
		}
		/* (the model's first shader that is one, else the map's first) */
		if (!substitute)
		{
			long other;

			for (other = 0; other < count && !substitute; other++)
			{
				struct ce_tag_instance *candidate = ce_instance_by_index(tag_instances, tag_count,
					ce_read_long(shaders + other * MODEL_SHADER_SIZE + 0xc));

				if (candidate && ce_is_shader(candidate))
					substitute = candidate;
			}
			for (other = 0; other < tag_count && !substitute; other++)
			{
				if (ce_is_shader(ce_instance(tag_instances, other)))
					substitute = ce_instance(tag_instances, other);
			}
			if (!substitute)
				return;
		}
		ce_write_long(reference, substitute->group_tag);
		ce_write_long(reference + 0xc, substitute->tag_index);
		counts->model_shaders++;
	}
}

static void ce_repairs_log(
	struct ce_repair_counts const *counts)
{
	if (ce_map_checking() || !(counts->predicted_resources_dropped | counts->predicted_resources_salted |
		counts->object_types | counts->modifier_shaders | counts->model_shaders))
	{
		return;
	}
	error(_error_silent, "%s map: %ld predicted resources dropped and %ld given their tags' salts, %ld object "
		"types, %ld modifier shaders and %ld model shaders repaired", ce_map_cache_version == CE_CACHE_VERSION_RETAIL ?
		"HaloMD" : "Custom Edition", counts->predicted_resources_dropped, counts->predicted_resources_salted,
		counts->object_types, counts->modifier_shaders, counts->model_shaders);
}

/* ---------- public code */

/* the map's tags repaired, in the image they are checked or loaded in (its
resource maps' tags already copied in: ce_resources.c) */
void ce_repairs_apply(
	struct ce_image const *image,
	void *tag_instances,
	long tag_count,
	unsigned long scenario_tag_index)
{
	struct ce_repair_counts counts;
	long index;

	memset(&counts, 0, sizeof(counts));
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = ce_instance(tag_instances, index);
		byte *data;

		if (ce_is_of_group(instance, 'obje'))
		{
			ce_object_type_repair(image, instance, &counts);
			ce_modifier_shader_repair(image, instance, tag_instances, tag_count, &counts);
			data = ce_image_pointer(image, instance->base_address, OBJECT_PREDICTED_RESOURCES_OFFSET + 0xc);
			if (data)
				ce_predicted_resources_repair(image, data + OBJECT_PREDICTED_RESOURCES_OFFSET, tag_instances, tag_count,
					&counts);
			data = instance->group_tag == 'weap' ?
				ce_image_pointer(image, instance->base_address, WEAPON_PREDICTED_RESOURCES_OFFSET + 0xc) : NULL;
			if (data)
				ce_predicted_resources_repair(image, data + WEAPON_PREDICTED_RESOURCES_OFFSET, tag_instances, tag_count,
					&counts);
		}
		else if (instance->group_tag == 'mod2')
			ce_model_shaders_repair(image, instance, tag_instances, tag_count, &counts);
	}
	{
		struct ce_tag_instance *scenario = ce_instance_by_index(tag_instances, tag_count, scenario_tag_index);
		byte *data = scenario && scenario->group_tag == 'scnr' ?
			ce_image_pointer(image, scenario->base_address, SCENARIO_PREDICTED_RESOURCES_OFFSET + 0xc) : NULL;

		if (data)
			ce_predicted_resources_repair(image, data + SCENARIO_PREDICTED_RESOURCES_OFFSET, tag_instances, tag_count,
				&counts);
	}
	ce_repairs_log(&counts);
}

/* the map's tags loaded (cache_files.c), its resource maps' tags copied in:
repaired in place, in its tag cache */
void ce_repairs_tags_loaded(
	void *tag_instances,
	long tag_count,
	unsigned long scenario_tag_index)
{
	struct ce_image image;

	image.data = xbox_pointer(CE_IMAGE_TAG_CACHE_BASE);
	image.base = CE_IMAGE_TAG_CACHE_BASE;
	image.size = CE_IMAGE_TAG_CACHE_SIZE;
	ce_loaded_instances = tag_instances;
	ce_loaded_tag_count = tag_count;
	ce_repairs_apply(&image, tag_instances, tag_count, scenario_tag_index);
}

/* one of its structure BSPs loaded (cache_files.c): its clusters'
predicted resources repaired */
void ce_repairs_bsp_loaded(
	struct structure_bsp *structure)
{
	struct ce_image image;
	struct ce_repair_counts counts;
	long index;

	if (!ce_loaded_instances)
		return;
	image.data = xbox_pointer(CE_IMAGE_TAG_CACHE_BASE);
	image.base = CE_IMAGE_TAG_CACHE_BASE;
	image.size = CE_IMAGE_TAG_CACHE_SIZE;
	memset(&counts, 0, sizeof(counts));
	for (index = 0; index < structure->clusters.count; index++)
	{
		struct structure_cluster *cluster = TAG_BLOCK_GET_ELEMENT(&structure->clusters, index, struct structure_cluster);

		ce_predicted_resources_repair(&image, (byte *)&cluster->predicted_resources, ce_loaded_instances,
			ce_loaded_tag_count, &counts);
	}
	ce_repairs_log(&counts);
}

#endif
