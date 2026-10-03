/*
CE_MAP_CHECKS.C

Custom Edition maps checked before they are played (cache_files_windows.c,
ce_map_open). Players drop in any .map file, made by any tool over twenty
years, so nothing in one is trusted: before a map is given its slot, it is
read as it would be loaded (its tags into an image of its tag cache, its
resource maps' tags copied in and relocated after them, its model data and
its structure BSPs into memory of their own) and every offset, count, size
and index the port's loading and converting code reads is checked against
what it points into, with arithmetic that cannot overflow:

  - the header: the file's length, the tag data's range in the file and its
    size against the tag cache;
  - the tag index: the instances within the tag data, each one's handle its
    own index, its name within the tag data and terminated, its data within
    the tag data (a structure BSP's none: it is read when the BSP loads), and
    only bitmaps, sounds, fonts and strings held by the resource maps;
  - the scenario and its structure BSPs: each one's range in the file and its
    place in the tag cache (above the tags, below the cache's end), its
    header and its lightmaps' materials' vertices (ce_bsp.c);
  - the resource maps' tags: each resource within its file and the room left
    in the tag cache, and every block and data in it within it once it is
    relocated (ce_resources.c);
  - every bitmap's format and size (the game's own bitmap_verify) and its
    pixels within the file they are read from, and every sound's pitch
    ranges and permutations, and their samples within theirs (ce_resources.c);
  - every model's geometries, parts, nodes, shaders, strips and vertices
    against the model data and each other, and the room the converted parts
    need (ce_models.c);
  - every shader's type against its group (ce_models.c);
  - the HUD interfaces' blocks the port rescales (ce_hud.c).

A map that fails a check is refused: it is not opened, the reason is logged
to debug.txt and shown on the console, and the game goes on as for a map
that is not there. The check is what the port reads; the game reads the
rest of a map's tags as it reads the Xbox's maps, trusting them.
*/

#ifdef HALO_CUSTOM_EDITION

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "main/console.h"
#include "ce_map_checks.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	CE_HEADER_SIZE = 0x800,
	CE_TAG_INSTANCE_SIZE = 0x20,
	/* (a tag handle's index: 16 bits) */
	CE_MAXIMUM_TAG_COUNT = 0xffff,
	/* the files read: no larger than a signed 32-bit offset reaches */
	CE_MAXIMUM_FILE_SIZE = 0x7fffffff,

	/* the tag header (cache_files.c's cache_file_ce_tag_header) */
	CE_TAG_HEADER_SIZE = 0x28,
	CE_TAG_HEADER_SIGNATURE = 'tags',

	/* the scenario's structure BSPs (scenario_definitions.h) */
	CE_SCENARIO_GROUP = 'scnr',
	CE_SCENARIO_BSPS_OFFSET = 0x5a4,
	CE_SCENARIO_BSP_REFERENCE_SIZE = 0x20,
	CE_MAXIMUM_BSPS = 16, /* (scenario.h's MAXIMUM_STRUCTURE_BSPS_PER_SCENARIO) */
	CE_BSP_GROUP = 'sbsp',
	CE_BSP_HEADER_SIZE = 0x18,
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

struct ce_tag_header
{
	unsigned long tag_instances;
	unsigned long scenario_tag_index;
	unsigned long checksum;
	unsigned long tag_count;
	unsigned long model_part_count;
	unsigned long model_data_file_offset;
	unsigned long model_part_count_again;
	unsigned long vertex_data_size;
	unsigned long model_data_size;
	unsigned long signature;
};

struct ce_bsp_reference
{
	unsigned long file_offset;
	unsigned long file_size;
	unsigned long base_address;
	unsigned long unused;
	unsigned long group_tag;
	unsigned long name;
	unsigned long name_length;
	unsigned long tag_index;
};

/* ---------- prototypes */

boolean ce_resources_check(struct ce_image *image, void *tag_instances, long tag_count, unsigned long end_free,
	unsigned long map_file_size, unsigned long *next_free);
boolean ce_models_check(struct ce_image const *image, void const *tag_instances, long tag_count,
	byte const *model_data, unsigned long model_data_size, unsigned long vertex_data_size, unsigned long *bytes);
boolean ce_shaders_check(struct ce_image const *image, void const *tag_instances, long tag_count);
boolean ce_hud_check(struct ce_image const *image, void const *tag_instances, long tag_count);
boolean ce_bsp_check(struct ce_image const *image, unsigned long *bytes);

/* ---------- globals */

static char ce_refusal[256];
static boolean ce_checking;

/* ---------- private code */

static boolean ce_file_read(
	HANDLE file,
	unsigned long offset,
	void *buffer,
	unsigned long size)
{
	unsigned long bytes_read = 0;

	if (SetFilePointer(file, (long)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return FALSE;
	return ReadFile(file, buffer, size, &bytes_read, NULL) && bytes_read == size;
}

/* the instance at a tag handle, if it is the handle of one */
static struct ce_tag_instance const *ce_tag_instance_get(
	struct ce_tag_instance const *instances,
	long tag_count,
	unsigned long tag_index)
{
	unsigned long index = tag_index & 0xffff;

	if (tag_index == 0xffffffff || index >= (unsigned long)tag_count || instances[index].tag_index != tag_index)
		return NULL;
	return &instances[index];
}

static boolean ce_tag_index_check(
	struct ce_image const *image,
	struct ce_tag_instance *instances,
	long tag_count)
{
	long index;

	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = &instances[index];
		char const *name = ce_image_pointer(image, instance->name, 1);

		if ((instance->tag_index & 0xffff) != (unsigned long)index)
			return ce_refuse("tag %ld's handle is %08lx", index, instance->tag_index);
		if (!name || !memchr(name, 0, image->base + image->size - instance->name))
			return ce_refuse("tag %ld's name is not in its tag data", index);
		if (instance->indexed)
		{
			switch (instance->group_tag)
			{
			case 'bitm':
			case 'snd!':
			case 'font':
			case 'ustr':
			case 'hmt ':
				break;
			default:
				return ce_refuse("%s is held by a resource map, which keeps no tags of its kind", name);
			}
			/* (its resource: ce_resources_check) */
		}
		else if (instance->group_tag == CE_BSP_GROUP)
		{
			/* (read when the BSP loads: cache_files.c) */
			if (instance->base_address)
				return ce_refuse("structure BSP %s is placed before it is loaded", name);
		}
		else if (!ce_image_pointer(image, instance->base_address, 4))
		{
			return ce_refuse("%s is not in its tag data", name);
		}
	}
	return TRUE;
}

/* the scenario's structure BSPs: each one read and checked (ce_bsp.c); the
lowest's address (the tag cache's room ends there) and the room the most
needy's conversion takes */
static boolean ce_bsps_check(
	HANDLE file,
	unsigned long file_size,
	struct ce_image const *image,
	struct ce_tag_instance const *instances,
	long tag_count,
	byte const *scenario,
	unsigned long *end_free,
	unsigned long *bsp_bytes)
{
	long count, index;
	byte *references;
	unsigned long tags_end = image->base + image->size;

	*end_free = CE_IMAGE_TAG_CACHE_BASE + CE_IMAGE_TAG_CACHE_SIZE;
	*bsp_bytes = 0;
	if (!ce_image_block(image, scenario + CE_SCENARIO_BSPS_OFFSET, CE_SCENARIO_BSP_REFERENCE_SIZE, CE_MAXIMUM_BSPS,
		"the scenario's structure BSPs", &count, &references))
	{
		return FALSE;
	}
	if (!count)
		return ce_refuse("its scenario has no structure BSP");
	for (index = 0; index < count; index++)
	{
		struct ce_bsp_reference reference;
		struct ce_tag_instance const *instance;
		struct ce_image bsp;
		unsigned long bytes = 0;
		boolean valid;

		memcpy(&reference, references + index * CE_SCENARIO_BSP_REFERENCE_SIZE, sizeof(reference));
		instance = ce_tag_instance_get(instances, tag_count, reference.tag_index);
		if (!instance || instance->group_tag != CE_BSP_GROUP)
			return ce_refuse("structure BSP %ld is not a structure BSP's tag", index);
		if (reference.file_size < CE_BSP_HEADER_SIZE || !ce_range_within(reference.file_offset, reference.file_size,
			file_size))
		{
			return ce_refuse("structure BSP %ld (%lu bytes at %lu) is not in the file", index, reference.file_size,
				reference.file_offset);
		}
		/* (above the tags, within the tag cache: cache_files.c reads it
		there; ce_resources.c fills the room below the lowest) */
		if (reference.base_address < tags_end || reference.base_address % 4 ||
			!ce_range_within(reference.base_address - CE_IMAGE_TAG_CACHE_BASE, reference.file_size,
				CE_IMAGE_TAG_CACHE_SIZE))
		{
			return ce_refuse("structure BSP %ld (%lu bytes at %08lx) is not in the tag cache, above the tags", index,
				reference.file_size, reference.base_address);
		}
		if (reference.base_address < *end_free)
			*end_free = reference.base_address;
		bsp.base = reference.base_address;
		bsp.size = reference.file_size;
		bsp.data = malloc(reference.file_size);
		if (!bsp.data)
			return ce_refuse("no memory to check structure BSP %ld (%lu bytes)", index, reference.file_size);
		valid = ce_file_read(file, reference.file_offset, bsp.data, reference.file_size) &&
			ce_bsp_check(&bsp, &bytes);
		free(bsp.data);
		if (!valid)
			return ce_refuse("structure BSP %ld could not be read", index);
		if (bytes > *bsp_bytes)
			*bsp_bytes = bytes;
	}
	return TRUE;
}

static boolean ce_map_check_tags(
	HANDLE file,
	unsigned long file_size,
	unsigned long tag_data_offset,
	unsigned long tag_data_size)
{
	struct ce_image image, tags;
	struct ce_tag_header header;
	struct ce_tag_instance *instances;
	struct ce_tag_instance const *scenario_instance;
	byte *scenario;
	byte *model_data = NULL;
	unsigned long end_free, bsp_bytes, next_free, model_bytes = 0;
	long tag_count;
	boolean valid = FALSE;

	if (tag_data_offset < CE_HEADER_SIZE || tag_data_size < CE_TAG_HEADER_SIZE ||
		!ce_range_within(tag_data_offset, tag_data_size, file_size))
	{
		return ce_refuse("its tag data (%lu bytes at %lu) is not in the file", tag_data_size, tag_data_offset);
	}
	if (tag_data_size > CE_IMAGE_TAG_CACHE_SIZE)
	{
		return ce_refuse("its tag data (%lu bytes) is larger than the tag cache (%lu bytes)", tag_data_size,
			(unsigned long)CE_IMAGE_TAG_CACHE_SIZE);
	}
	/* (an image of the whole tag cache: the resource maps' tags are copied
	in after the tags, as they are when the map loads) */
	image.base = CE_IMAGE_TAG_CACHE_BASE;
	image.size = tag_data_size;
	image.data = malloc(CE_IMAGE_TAG_CACHE_SIZE);
	if (!image.data)
		return ce_refuse("no memory to check it");
	memset(image.data, 0xcd, CE_IMAGE_TAG_CACHE_SIZE);
	if (!ce_file_read(file, tag_data_offset, image.data, tag_data_size))
	{
		ce_refuse("its tag data could not be read");
		goto done;
	}
	memcpy(&header, image.data, sizeof(header));
	if (header.signature != CE_TAG_HEADER_SIGNATURE)
	{
		ce_refuse("its tag header's signature is %08lx", header.signature);
		goto done;
	}
	tag_count = (long)header.tag_count;
	if (tag_count <= 0 || tag_count > CE_MAXIMUM_TAG_COUNT)
	{
		ce_refuse("it has %ld tags", tag_count);
		goto done;
	}
	instances = ce_image_pointer(&image, header.tag_instances, (unsigned long)tag_count * CE_TAG_INSTANCE_SIZE);
	if (!instances || header.tag_instances % 4)
	{
		ce_refuse("its %ld tags' instances (at %08lx) are not in its tag data", tag_count, header.tag_instances);
		goto done;
	}
	if (!ce_tag_index_check(&image, instances, tag_count))
		goto done;
	scenario_instance = ce_tag_instance_get(instances, tag_count, header.scenario_tag_index);
	scenario = scenario_instance && scenario_instance->group_tag == CE_SCENARIO_GROUP ?
		ce_image_pointer(&image, scenario_instance->base_address, CE_SCENARIO_BSPS_OFFSET + 0xc) : NULL;
	if (!scenario)
	{
		ce_refuse("its scenario (tag %08lx) is not a scenario", header.scenario_tag_index);
		goto done;
	}
	if (!ce_bsps_check(file, file_size, &image, instances, tag_count, scenario, &end_free, &bsp_bytes))
		goto done;
	/* the resource maps' tags copied in and relocated, and every bitmap and
	sound checked (ce_resources.c); the image then reaches past them */
	/* (the map's own tags, in the tag data: what follows is the resource
	maps') */
	tags = image;
	if (!ce_resources_check(&image, instances, tag_count, end_free, file_size, &next_free))
		goto done;
	/* the models, from the model data (ce_models.c) */
	if (header.vertex_data_size > header.model_data_size ||
		!ce_range_within(header.model_data_file_offset, header.model_data_size, file_size))
	{
		ce_refuse("its model data (%lu bytes at %lu, %lu of vertices) is not in the file", header.model_data_size,
			header.model_data_file_offset, header.vertex_data_size);
		goto done;
	}
	model_data = malloc(header.model_data_size ? header.model_data_size : 1);
	if (!model_data || !ce_file_read(file, header.model_data_file_offset, model_data, header.model_data_size))
	{
		ce_refuse("its model data (%lu bytes) could not be read", header.model_data_size);
		goto done;
	}
	if (!ce_models_check(&tags, instances, tag_count, model_data, header.model_data_size,
		header.vertex_data_size, &model_bytes))
	{
		goto done;
	}
	/* (the room left: the models' parts, strips and buffers, then the
	loaded BSP's buffers) */
	if (model_bytes > end_free - next_free || bsp_bytes > end_free - next_free - model_bytes)
	{
		ce_refuse("its models and structure BSP need %lu bytes more of the tag cache than its BSP leaves (%lu)",
			model_bytes + bsp_bytes, end_free - next_free);
		goto done;
	}
	valid = ce_shaders_check(&tags, instances, tag_count) && ce_hud_check(&tags, instances, tag_count);

done:
	if (model_data)
		free(model_data);
	free(image.data);
	return valid;
}

/* ---------- public code */

boolean ce_range_within(
	unsigned long offset,
	unsigned long size,
	unsigned long limit)
{
	return offset <= limit && size <= limit - offset;
}

void *ce_image_pointer(
	struct ce_image const *image,
	unsigned long address,
	unsigned long size)
{
	if (address < image->base || !ce_range_within(address - image->base, size, image->size))
		return NULL;
	return image->data + (address - image->base);
}

boolean ce_image_block(
	struct ce_image const *image,
	void const *field,
	unsigned long element_size,
	unsigned long maximum_count,
	char const *what,
	long *count,
	byte **elements)
{
	long block_count;
	unsigned long address;

	memcpy(&block_count, field, sizeof(block_count));
	memcpy(&address, (byte const *)field + 4, sizeof(address));
	*count = 0;
	*elements = NULL;
	if (block_count < 0 || (unsigned long)block_count > maximum_count)
		return ce_refuse("%s: %ld of them (at most %lu)", what, block_count, maximum_count);
	if (!block_count)
		return TRUE;
	/* (maximum_count is small: the product cannot overflow) */
	*elements = ce_image_pointer(image, address, (unsigned long)block_count * element_size);
	if (!*elements)
		return ce_refuse("%s: %ld of them at %08lx, not in the tags", what, block_count, address);
	*count = block_count;
	return TRUE;
}

boolean ce_image_data(
	struct ce_image const *image,
	void const *field,
	char const *what,
	unsigned long *size,
	byte **data)
{
	long data_size;
	unsigned long address;

	memcpy(&data_size, field, sizeof(data_size));
	memcpy(&address, (byte const *)field + 12, sizeof(address));
	*size = 0;
	*data = NULL;
	if (data_size < 0)
		return ce_refuse("%s: %ld bytes", what, data_size);
	if (!data_size)
		return TRUE;
	*data = ce_image_pointer(image, address, (unsigned long)data_size);
	if (!*data)
		return ce_refuse("%s: %ld bytes at %08lx, not in the tags", what, data_size, address);
	*size = (unsigned long)data_size;
	return TRUE;
}

char const *ce_image_tag_name(
	struct ce_image const *image,
	void const *instance)
{
	struct ce_tag_instance const *tag = instance;
	char const *name = ce_image_pointer(image, tag->name, 1);

	/* (checked to be terminated: ce_tag_index_check) */
	return name ? name : "<unnamed>";
}

boolean ce_refuse(
	char const *format,
	...)
{
	if (!ce_refusal[0])
	{
		va_list arguments;

		va_start(arguments, format);
		vsnprintf(ce_refusal, sizeof(ce_refusal), format, arguments);
		va_end(arguments);
	}
	return FALSE;
}

boolean ce_map_checking(
	void)
{
	return ce_checking;
}

/* an element of a tag block or the bytes of a tag data the game reads
(tag_groups.c), at an Xbox address: with a Custom Edition map loaded, whose
tags were checked only as far as the port reads them, not read outside its
tag cache (where its tags, resources, models and BSP are), but from zeroed
bytes of their own, and logged; else, as always, the address */
void *ce_tags_pointer(
	unsigned long address,
	long size)
{
	extern boolean cache_file_tags_are_ce(void);
	static byte scratch[0x40000];
	static long misses;

	if (!size || !cache_file_tags_are_ce() || (address >= CE_IMAGE_TAG_CACHE_BASE && size > 0 &&
		ce_range_within(address - CE_IMAGE_TAG_CACHE_BASE, (unsigned long)size, CE_IMAGE_TAG_CACHE_SIZE)))
	{
		return xbox_pointer(address);
	}
	if (misses < 16)
	{
		misses++;
		error(_error_silent, "Custom Edition map: its tags name %ld bytes at %08lx, outside them (read as zeroes)",
			size, address);
	}
	if (size < 0 || (unsigned long)size > sizeof(scratch))
	{
		error(_error_silent, "Custom Edition map: its tags are damaged; stopping");
		system_exit(-1);
	}
	memset(scratch, 0, (size_t)size);
	return scratch;
}

/* a Custom Edition map, its header read and verified (cache_files_windows.c),
checked as above before it is opened: FALSE, with the reason logged and shown,
if it is refused */
boolean ce_map_check(
	HANDLE file,
	char const *map_name,
	long file_length,
	long tag_data_offset,
	long tag_data_size)
{
	unsigned long file_size_high = 0;
	unsigned long file_size = GetFileSize(file, &file_size_high);
	unsigned long started = system_milliseconds();
	boolean valid;

	ce_refusal[0] = 0;
	ce_checking = TRUE;
	if (file_size == INVALID_FILE_SIZE || file_size_high || file_size > CE_MAXIMUM_FILE_SIZE)
		valid = ce_refuse("its size could not be read, or is over 2 GB");
	else if (file_length < CE_HEADER_SIZE || (unsigned long)file_length > file_size)
		valid = ce_refuse("it is cut short: its header says %ld bytes, the file has %lu", file_length, file_size);
	else
		valid = ce_map_check_tags(file, file_size, (unsigned long)tag_data_offset, (unsigned long)tag_data_size);
	ce_checking = FALSE;
	if (!valid)
	{
		error(_error_silent, "Custom Edition map %s refused: %s", map_name, ce_refusal);
		console_warning("Custom Edition map %s refused: %s", map_name, ce_refusal);
	}
	else
	{
		error(_error_silent, "Custom Edition map %s: checked (%lu ms)", map_name,
			system_milliseconds() - started);
	}
	return valid;
}

#endif
