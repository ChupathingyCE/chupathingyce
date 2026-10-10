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
    given the shader's own;
  - the scenario the map's header names with a group other than the
    scenario's (a map protector renames it: h2_ascension's, Headlong_PB2's
    and pitfall's is 'prot'), which Halo PC's engine took as the scenario
    whatever its group said: given the scenario's group, before the map's
    scenario is checked (ce_map_checks.c);
  - a tag whose parent groups are not its group's (Invader leaves a
    vehicle's and a water shader's none: beavercreek_rev_beta's), which
    the game's tag_get checks a vehicle against as an object: given its
    group's;
  - a model's or animation graph's nodes linked into a loop (a node's
    sibling or child one reached already: [h3]_sandtrap's cyborg graph has
    its spine's next sibling the pelvis, the first node), which the game
    walks without end, past its arrays of nodes: that link cut; and a
    node's link past the nodes (h2_ascension's has its parents a byte too
    high, 256 for 1): a sibling or child cut, a parent made the one the
    nodes' tree gives it;
  - a bitmap in MCC's high-quality compression (format 18: BC7, which
    Chimera draws on Halo PC and Halo PC's engine refused): made DXT5's
    format, whose 4x4 blocks are its size, and marked so that its blocks are
    decoded as BC7's when it is drawn (ce_bitmap_is_bc7: xbox_texture_cache.c,
    xbox_textures.c).

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
	/* a bitmap (bitmap_group.h's bitmap_data): its format and flags; MCC's
	high-quality compression (BC7), DXT5 and the compressed flag
	(bitmaps.h); the port's mark of a BC7 bitmap made DXT5's format, a flag
	none of Halo's tools set */
	BITMAP_DATA_SIZE = 0x30,
	BITMAP_DATA_FORMAT_OFFSET = 0x0c,
	BITMAP_DATA_FLAGS_OFFSET = 0x0e,
	BITMAP_FORMAT_DXT5 = 16,
	BITMAP_FORMAT_BC7 = 18,
	BITMAP_COMPRESSED_FLAG = 0x02,
	BITMAP_PORT_BC7_FLAG = 0x8000,

	/* a model's shaders (model_definitions.h): each a tag reference and
	a permutation */
	MODEL_SHADERS_OFFSET = 0xdc,
	MODEL_SHADER_SIZE = 0x20,
	MODEL_HEADER_SIZE = 0xe8,
	SHADER_GROUP = 'shdr',
	SCENARIO_GROUP = 'scnr',

	/* a model's and an animation graph's nodes (model_definitions.h,
	model_animation_definitions.h): each its next sibling, first child and
	parent, at the same place; the most either has (ce_models.c) */
	MODEL_NODES_OFFSET = 0xb8,
	MODEL_NODE_SIZE = 0x9c,
	ANIMATION_GRAPH_NODES_OFFSET = 0x68,
	ANIMATION_GRAPH_NODE_SIZE = 0x40,
	NODE_LINKS_OFFSET = 0x20,
	CE_MAXIMUM_NODES = 64,
};

/* ---------- structures */

/* what was repaired, for the log */
struct ce_repair_counts
{
	long predicted_resources_dropped;
	long predicted_resources_salted;
	long object_types;
	long modifier_shaders;
	long model_shaders;
	long node_links;
	long parent_groups;
	long bc7_bitmaps;
};

/* ---------- globals */

/* the object types' groups, by type (object_definitions.h, objects.c) */
static unsigned long const ce_object_type_groups[] =
{
	'bipd', 'vehi', 'weap', 'eqip', 'garb', 'proj', 'scen', 'mach', 'ctrl', 'lifi', 'plac', 'ssce',
};

/* the groups with parents, and their parents (tag_groups.c's, as Halo PC
has them) */
static struct
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
} const ce_group_parents[] =
{
	{ 'bipd', { 'unit', 'obje' } },
	{ 'vehi', { 'unit', 'obje' } },
	{ 'weap', { 'item', 'obje' } },
	{ 'eqip', { 'item', 'obje' } },
	{ 'garb', { 'item', 'obje' } },
	{ 'mach', { 'devi', 'obje' } },
	{ 'ctrl', { 'devi', 'obje' } },
	{ 'lifi', { 'devi', 'obje' } },
	{ 'proj', { 'obje', 0xffffffff } },
	{ 'scen', { 'obje', 0xffffffff } },
	{ 'plac', { 'obje', 0xffffffff } },
	{ 'ssce', { 'obje', 0xffffffff } },
	{ 'unit', { 'obje', 0xffffffff } },
	{ 'item', { 'obje', 0xffffffff } },
	{ 'devi', { 'obje', 0xffffffff } },
	{ 'senv', { 'shdr', 0xffffffff } },
	{ 'soso', { 'shdr', 0xffffffff } },
	{ 'sotr', { 'shdr', 0xffffffff } },
	{ 'schi', { 'shdr', 0xffffffff } },
	{ 'scex', { 'shdr', 0xffffffff } },
	{ 'swat', { 'shdr', 0xffffffff } },
	{ 'sgla', { 'shdr', 0xffffffff } },
	{ 'smet', { 'shdr', 0xffffffff } },
	{ 'spla', { 'shdr', 0xffffffff } },
};

/* the loaded map's tags (ce_repairs_tags_loaded), for its BSPs' */
static struct ce_tag_instance *ce_loaded_instances;
static long ce_loaded_tag_count;

/* ---------- private code */

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
		ce_map_family_name(), ce_image_tag_name(image, instance), type, group_type);
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
		struct ce_tag_instance *shader = ce_instance_by_index(tag_instances, tag_count,
			ce_read_long(reference + TAG_REFERENCE_INDEX_OFFSET));

		if (shader && ce_is_shader(shader))
		{
			if (shader->tag_index != ce_read_long(reference + TAG_REFERENCE_INDEX_OFFSET))
				ce_write_long(reference + TAG_REFERENCE_INDEX_OFFSET, shader->tag_index);
			continue;
		}
		/* (the model's first shader that is one, else the map's first) */
		if (!substitute)
		{
			long other;

			for (other = 0; other < count && !substitute; other++)
			{
				struct ce_tag_instance *candidate = ce_instance_by_index(tag_instances, tag_count,
					ce_read_long(shaders + other * MODEL_SHADER_SIZE + TAG_REFERENCE_INDEX_OFFSET));

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
		ce_write_long(reference + TAG_REFERENCE_INDEX_OFFSET, substitute->tag_index);
		counts->model_shaders++;
	}
}

/* a model's or an animation graph's nodes (the block at field, each
node_size bytes): a next sibling or first child past the nodes cut, then the
nodes walked from the first, by next siblings and first children, as the
game walks them, and a link to a node reached already cut; a parent past
the nodes made the node's parent in that walk (none if it is not reached) */
static void ce_node_links_repair(
	struct ce_image const *image,
	byte *field,
	unsigned long node_size,
	struct ce_repair_counts *counts)
{
	long node_count;
	byte *nodes = ce_block(image, field, node_size, &node_count);
	boolean reached[CE_MAXIMUM_NODES];
	short parents[CE_MAXIMUM_NODES];
	short queue[CE_MAXIMUM_NODES];
	long read_index = 0, write_index = 0, index;

	if (!nodes || node_count > CE_MAXIMUM_NODES)
		return;
	for (index = 0; index < node_count; index++)
	{
		byte *links = nodes + index * node_size + NODE_LINKS_OFFSET;
		long link;

		for (link = 0; link < 2; link++)
		{
			short linked = ce_read_short(links + link * sizeof(short));

			if (linked != NONE && (linked < 0 || linked >= node_count))
			{
				ce_write_short(links + link * sizeof(short), NONE);
				counts->node_links++;
			}
		}
	}
	memset(reached, 0, sizeof(reached));
	reached[0] = TRUE;
	parents[0] = NONE;
	queue[write_index++] = 0;
	while (read_index < write_index)
	{
		short node_index = queue[read_index++];
		byte *links = nodes + node_index * node_size + NODE_LINKS_OFFSET;
		long link;

		/* (its next sibling, of its parent, then its first child) */
		for (link = 0; link < 2; link++)
		{
			short linked = ce_read_short(links + link * sizeof(short));

			if (linked == NONE)
				continue;
			if (reached[linked])
			{
				ce_write_short(links + link * sizeof(short), NONE);
				counts->node_links++;
				continue;
			}
			reached[linked] = TRUE;
			parents[linked] = link ? node_index : parents[node_index];
			queue[write_index++] = linked;
		}
	}
	for (index = 0; index < node_count; index++)
	{
		byte *parent = nodes + index * node_size + NODE_LINKS_OFFSET + 2 * sizeof(short);
		short parent_index = ce_read_short(parent);

		if (parent_index != NONE && (parent_index < 0 || parent_index >= node_count))
		{
			ce_write_short(parent, reached[index] ? parents[index] : NONE);
			counts->node_links++;
		}
	}
}

/* a tag given its group's parent groups, if it has others */
static void ce_parent_groups_repair(
	struct ce_tag_instance *instance,
	struct ce_repair_counts *counts)
{
	long index;

	for (index = 0; index < NUMBEROF(ce_group_parents); index++)
	{
		if (ce_group_parents[index].group_tag != instance->group_tag)
			continue;
		if (instance->parent_group_tags[0] != ce_group_parents[index].parent_group_tags[0] ||
			instance->parent_group_tags[1] != ce_group_parents[index].parent_group_tags[1])
		{
			instance->parent_group_tags[0] = ce_group_parents[index].parent_group_tags[0];
			instance->parent_group_tags[1] = ce_group_parents[index].parent_group_tags[1];
			counts->parent_groups++;
		}
		return;
	}
}

/* a bitmap tag's BC7 bitmaps made DXT5's format and marked; the mark taken
from any other */
static void ce_bitmaps_repair(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	struct ce_repair_counts *counts)
{
	byte *group = ce_image_pointer(image, instance->base_address, BITMAP_GROUP_BITMAPS_OFFSET + 0xc);
	byte *bitmaps;
	long count, index;

	if (!group)
		return;
	bitmaps = ce_block(image, group + BITMAP_GROUP_BITMAPS_OFFSET, BITMAP_DATA_SIZE, &count);
	for (index = 0; index < count; index++)
	{
		byte *bitmap = bitmaps + index * BITMAP_DATA_SIZE;
		short format;
		unsigned short flags;

		memcpy(&format, bitmap + BITMAP_DATA_FORMAT_OFFSET, sizeof(format));
		memcpy(&flags, bitmap + BITMAP_DATA_FLAGS_OFFSET, sizeof(flags));
		if (format == BITMAP_FORMAT_BC7)
		{
			format = BITMAP_FORMAT_DXT5;
			flags |= BITMAP_COMPRESSED_FLAG | BITMAP_PORT_BC7_FLAG;
			counts->bc7_bitmaps++;
		}
		else
			flags &= (unsigned short)~BITMAP_PORT_BC7_FLAG;
		memcpy(bitmap + BITMAP_DATA_FORMAT_OFFSET, &format, sizeof(format));
		memcpy(bitmap + BITMAP_DATA_FLAGS_OFFSET, &flags, sizeof(flags));
	}
}

static void ce_repairs_log(
	struct ce_repair_counts const *counts)
{
	if (ce_map_checking() || !(counts->predicted_resources_dropped | counts->predicted_resources_salted |
		counts->object_types | counts->modifier_shaders | counts->model_shaders | counts->node_links |
		counts->parent_groups | counts->bc7_bitmaps))
	{
		return;
	}
	error(_error_silent, "%s map: %ld predicted resources dropped and %ld given their tags' salts, %ld object "
		"types, %ld modifier shaders and %ld model shaders repaired, %ld node links looping back or out of the nodes, "
		"%ld tags' parent groups, %ld BC7 bitmaps", ce_map_family_name(),
		counts->predicted_resources_dropped, counts->predicted_resources_salted, counts->object_types,
		counts->modifier_shaders, counts->model_shaders, counts->node_links, counts->parent_groups,
		counts->bc7_bitmaps);
}

/* ---------- public code */

/* the scenario the map's header names (scenario_tag_index, which must be a
tag's handle) given the scenario's group if it has another; TRUE if it was */
boolean ce_repairs_scenario_group(
	void *tag_instances,
	long tag_count,
	unsigned long scenario_tag_index)
{
	struct ce_tag_instance *scenario = ce_instance_by_index(tag_instances, tag_count, scenario_tag_index);

	if (!scenario || scenario->tag_index != scenario_tag_index || scenario->group_tag == SCENARIO_GROUP)
		return FALSE;
	scenario->group_tag = SCENARIO_GROUP;
	scenario->parent_group_tags[0] = 0xffffffff;
	scenario->parent_group_tags[1] = 0xffffffff;
	return TRUE;
}

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
	/* (first: the repairs below find objects and shaders by their parents) */
	for (index = 0; index < tag_count; index++)
		ce_parent_groups_repair(ce_instance(tag_instances, index), &counts);
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
		{
			ce_model_shaders_repair(image, instance, tag_instances, tag_count, &counts);
			data = ce_image_pointer(image, instance->base_address, MODEL_NODES_OFFSET + 0xc);
			if (data)
				ce_node_links_repair(image, data + MODEL_NODES_OFFSET, MODEL_NODE_SIZE, &counts);
		}
		else if (instance->group_tag == 'bitm')
			ce_bitmaps_repair(image, instance, &counts);
		else if (instance->group_tag == 'antr')
		{
			data = ce_image_pointer(image, instance->base_address, ANIMATION_GRAPH_NODES_OFFSET + 0xc);
			if (data)
				ce_node_links_repair(image, data + ANIMATION_GRAPH_NODES_OFFSET, ANIMATION_GRAPH_NODE_SIZE, &counts);
		}
	}
	{
		struct ce_tag_instance *scenario = ce_instance_by_index(tag_instances, tag_count, scenario_tag_index);
		byte *data = scenario && scenario->group_tag == SCENARIO_GROUP ?
			ce_image_pointer(image, scenario->base_address, SCENARIO_PREDICTED_RESOURCES_OFFSET + 0xc) : NULL;

		if (data)
			ce_predicted_resources_repair(image, data + SCENARIO_PREDICTED_RESOURCES_OFFSET, tag_instances, tag_count,
				&counts);
	}
	ce_repairs_log(&counts);
}

/* whether a bitmap (bitmap_group.h's bitmap_data, in the loaded map's
tags) is one of MCC's BC7 bitmaps, made DXT5's format: its blocks are BC7's
(xbox_texture_cache.c) */
boolean ce_bitmap_is_bc7(
	void const *bitmap)
{
	unsigned short flags;
	short format;

	memcpy(&format, (byte const *)bitmap + BITMAP_DATA_FORMAT_OFFSET, sizeof(format));
	memcpy(&flags, (byte const *)bitmap + BITMAP_DATA_FLAGS_OFFSET, sizeof(flags));
	return format == BITMAP_FORMAT_DXT5 && (flags & BITMAP_PORT_BC7_FLAG);
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
	if (ce_repairs_scenario_group(tag_instances, tag_count, scenario_tag_index))
		error(_error_silent, "Custom Edition map: its scenario given the scenario's group");
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
