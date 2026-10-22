/*
TAG_BYTE_SWAP.C

Applies the generated layouts (tag_byte_swap.h) to a map's tags as they
load: each tag's fields by its group's layout, then each of its blocks'
elements by theirs, recursively. Everything is checked against the data the
tags were read into, so a layout that does not match a map's data stops at
the bad block instead of writing past it, and says so.
*/

#include "cseries.h"
#include "errors.h"

#include "tag_byte_swap.h"

#define MAXIMUM_DEPTH 16
#define MAXIMUM_REPORTS 32

struct swap_region
{
	unsigned char *base;
	unsigned long address;
	unsigned long size;
	/* what was done, for the log */
	unsigned long tags;
	unsigned long blocks;
	unsigned long elements;
	unsigned long bad_blocks;
	unsigned long reports;
};

static unsigned short swap16(unsigned short value)
{
	return (unsigned short)((value << 8) | (value >> 8));
}

static unsigned long swap32(unsigned long value)
{
	return ((value & 0xFFUL) << 24) | ((value & 0xFF00UL) << 8) | ((value >> 8) & 0xFF00UL) |
		((value >> 24) & 0xFFUL);
}

#ifdef TAG_BYTE_SWAP_EMULATE_BIG_ENDIAN
/* host-side test: a little-endian machine reading and writing words as the
big-endian console does (port/console/tests/tag_byte_swap_test.c) */
static unsigned long read32(const unsigned char *at)
{
	return ((unsigned long)at[0] << 24) | ((unsigned long)at[1] << 16) | ((unsigned long)at[2] << 8) | at[3];
}

static void write32(unsigned char *at, unsigned long value)
{
	at[0] = (unsigned char)(value >> 24);
	at[1] = (unsigned char)(value >> 16);
	at[2] = (unsigned char)(value >> 8);
	at[3] = (unsigned char)value;
}

static unsigned short read16(const unsigned char *at)
{
	return (unsigned short)((at[0] << 8) | at[1]);
}

static void write16(unsigned char *at, unsigned short value)
{
	at[0] = (unsigned char)(value >> 8);
	at[1] = (unsigned char)value;
}

/* (and a count of the swaps each byte of the region gets, for the test to
find one swapped twice, which returns to the map's order: the test sets
these around a call) */
unsigned char *tag_byte_swap_test_base;
unsigned long tag_byte_swap_test_size;
unsigned char *tag_byte_swap_test_counts;

static void test_count(const unsigned char *at, unsigned long bytes)
{
	unsigned long offset, index;

	if (!tag_byte_swap_test_counts || at < tag_byte_swap_test_base)
		return;
	offset = (unsigned long)(at - tag_byte_swap_test_base);
	for (index = 0; index < bytes && offset + index < tag_byte_swap_test_size; index++)
	{
		if (tag_byte_swap_test_counts[offset + index] < 255)
			tag_byte_swap_test_counts[offset + index]++;
	}
}
#define TEST_COUNT(at, bytes) test_count((at), (bytes))
#else
static unsigned long read32(const unsigned char *at)
{
	unsigned long value;

	memcpy(&value, at, 4);
	return value;
}

static void write32(unsigned char *at, unsigned long value)
{
	memcpy(at, &value, 4);
}

static unsigned short read16(const unsigned char *at)
{
	unsigned short value;

	memcpy(&value, at, 2);
	return value;
}

static void write16(unsigned char *at, unsigned short value)
{
	memcpy(at, &value, 2);
}
#define TEST_COUNT(at, bytes) ((void)0)
#endif

static void swap_fields(unsigned char *at, unsigned long op, unsigned long count)
{
	unsigned long index;

	for (index = 0; index < count; index++, at += op)
	{
		TEST_COUNT(at, op);
		switch (op)
		{
		case 2:
		{
			write16(at, swap16(read16(at)));
			break;
		}
		case 4:
		{
			write32(at, swap32(read32(at)));
			break;
		}
		case 8:
		{
			unsigned long low = swap32(read32(at)), high = swap32(read32(at + 4));

			write32(at, high);
			write32(at + 4, low);
			break;
		}
		}
	}
}

/* whether [address, address + size) is inside the region */
static int region_holds(const struct swap_region *region, unsigned long address, unsigned long size)
{
	unsigned long offset = address - region->address;

	return address >= region->address && offset <= region->size && size <= region->size - offset;
}

static void swap_element(struct swap_region *region, unsigned long layout_index, unsigned long address,
	int depth);

static void report(struct swap_region *region, unsigned long layout_index, unsigned long offset,
	long count, unsigned long address)
{
	region->bad_blocks++;
	if (region->reports++ < MAXIMUM_REPORTS)
	{
		error(_error_silent, "tag byte swap: layout %lu's block at +0x%lx is not one (count %ld, address 0x%08lx)",
			layout_index, offset, count, address);
	}
}

static void swap_layout(struct swap_region *region, unsigned long layout_index, unsigned long address, int depth)
{
	const struct tag_swap_layout *layout = &tag_swap_layouts[layout_index];
	unsigned char *at = region->base + (address - region->address);
	unsigned long op_index;

	/* the fields first: the blocks' counts and addresses among them */
	for (op_index = 0; op_index < layout->op_count; op_index++)
	{
		const struct tag_swap_op *op = &tag_swap_ops[layout->first_op + op_index];

		if (op->op)
			swap_fields(at + op->offset, op->op, op->argument);
	}
	if (depth >= MAXIMUM_DEPTH)
		return;
	for (op_index = 0; op_index < layout->op_count; op_index++)
	{
		const struct tag_swap_op *op = &tag_swap_ops[layout->first_op + op_index];
		long count;
		unsigned long element_address, element_size, index;

		if (op->op)
			continue;
		count = (long)read32(at + op->offset);
		element_address = read32(at + op->offset + 4);
		element_size = tag_swap_layouts[op->argument].size;
		if (!count)
			continue;
		if (count < 0 || !element_size || (unsigned long)count > region->size / element_size ||
			!region_holds(region, element_address, (unsigned long)count * element_size))
		{
			report(region, layout_index, op->offset, count, element_address);
			continue;
		}
		region->blocks++;
		for (index = 0; index < (unsigned long)count; index++)
			swap_element(region, op->argument, element_address + index * element_size, depth + 1);
	}
}

static void swap_element(struct swap_region *region, unsigned long layout_index, unsigned long address, int depth)
{
	region->elements++;
	swap_layout(region, layout_index, address, depth);
}

static long group_layout(unsigned long group_tag)
{
	int low = 0, high = tag_swap_group_count - 1;

	while (low <= high)
	{
		int middle = (low + high) / 2;

		if (tag_swap_groups[middle].group_tag == group_tag)
			return (long)tag_swap_groups[middle].layout;
		if (tag_swap_groups[middle].group_tag < group_tag)
			low = middle + 1;
		else
			high = middle - 1;
	}
	return NONE;
}

/* the tag header and instances (cache_files.c's layouts) */
#define TAG_HEADER_INSTANCES 0x0
#define TAG_HEADER_COUNT 0xC
#define TAG_INSTANCE_SIZE 0x20
#define TAG_INSTANCE_GROUP 0x0
#define TAG_INSTANCE_ADDRESS 0x14
#define STRUCTURE_BSP_GROUP 0x73627370UL /* 'sbsp' */
#define UNICODE_STRING_LIST_GROUP 0x75737472UL /* 'ustr' */
#define HUD_MESSAGE_TEXT_GROUP 0x686D7420UL /* 'hmt ' */
/* (a tag_data: its size, then its address at +0xC; a string list entry is one) */
#define TAG_DATA_SIZE 0x14
#define TAG_DATA_ADDRESS 0xC

/* a tag_data of UTF-16 text (wchar_t): its characters, which no layout
covers (the data is bytes to the tag system) */
static void swap_text_data(struct swap_region *region, const unsigned char *data)
{
	long size = (long)read32(data);
	unsigned long address = read32(data + TAG_DATA_ADDRESS);

	if (size <= 0 || !region_holds(region, address, (unsigned long)size))
		return;
	swap_fields(region->base + (address - region->address), 2, (unsigned long)size / 2);
}

/* a model's indices (each part's triangle buffer: little-endian words of
the map's own, outside any tag block) made the machine's, as the structure
bsp's and the game's own are: the renderer reads one byte order (a part's
vertices stay as they are: the map's, which the vertex formats read) */
#define MODEL_GROUP 0x6D6F6465UL /* 'mode' */
#define ANIMATION_GRAPH_GROUP 0x616E7472UL /* 'antr' */

void animation_graph_byte_swap_data(void *graph);
unsigned long structure_bsp_cluster_data_offset(void);

/* a tag_data of longs: made the machine's */
static void swap_long_data(struct swap_region *region, const unsigned char *data);
#define MODEL_GEOMETRIES 0xD0 /* struct model's geometries block */
#define MODEL_GEOMETRY_SIZE 0x30
#define MODEL_GEOMETRY_PARTS 0x24
#define MODEL_PART_SIZE 0x68
#define MODEL_PART_TRIANGLES 0x38 /* the part's triangles tag block */
#define MODEL_PART_TRIANGLE_BUFFER 0x44 /* type (short), pad, count, base address */
#define TRIANGLE_BUFFER_STRIP 1

static void swap_model_indices(struct swap_region *region, unsigned long address, unsigned long *swapped)
{
	const unsigned char *model = region->base + (address - region->address);
	long geometry_count = (long)read32(model + MODEL_GEOMETRIES);
	unsigned long geometries = read32(model + MODEL_GEOMETRIES + 4), geometry_index;

	if (geometry_count <= 0 || !region_holds(region, geometries, (unsigned long)geometry_count * MODEL_GEOMETRY_SIZE))
		return;
	for (geometry_index = 0; geometry_index < (unsigned long)geometry_count; geometry_index++)
	{
		const unsigned char *geometry = region->base + (geometries - region->address) +
			geometry_index * MODEL_GEOMETRY_SIZE;
		long part_count = (long)read32(geometry + MODEL_GEOMETRY_PARTS);
		unsigned long parts = read32(geometry + MODEL_GEOMETRY_PARTS + 4), part_index;

		if (part_count <= 0 || !region_holds(region, parts, (unsigned long)part_count * MODEL_PART_SIZE))
			continue;
		for (part_index = 0; part_index < (unsigned long)part_count; part_index++)
		{
			const unsigned char *part = region->base + (parts - region->address) + part_index * MODEL_PART_SIZE;
			const unsigned char *buffer = part + MODEL_PART_TRIANGLE_BUFFER;
			unsigned short type;
			long count = (long)read32(buffer + 4);
			unsigned long indices = read32(buffer + 8), index_count;

			type = read16(buffer);
			if (count <= 0)
				continue;
			index_count = type == TRIANGLE_BUFFER_STRIP ? (unsigned long)count + 2 : (unsigned long)count * 3;
			/* (the triangles block's own words were swapped with the block) */
			if ((long)read32(part + MODEL_PART_TRIANGLES) > 0 && read32(part + MODEL_PART_TRIANGLES + 4) == indices)
				continue;
			if (!region_holds(region, indices, index_count * 2))
				continue;
			swap_fields(region->base + (indices - region->address), 2, index_count);
			(*swapped)++;
		}
	}
}

static void swap_long_data(struct swap_region *region, const unsigned char *data)
{
	long size = (long)read32(data);
	unsigned long address = read32(data + TAG_DATA_ADDRESS);

	if (size <= 0 || !region_holds(region, address, (unsigned long)size))
		return;
	swap_fields(region->base + (address - region->address), 4, (unsigned long)size / 4);
}

/* the text of the groups that hold UTF-16 strings, after their fields */
static void swap_tag_text(struct swap_region *region, unsigned long group_tag, unsigned long address)
{
	const unsigned char *at = region->base + (address - region->address);

	if (group_tag == UNICODE_STRING_LIST_GROUP)
	{
		/* (its strings block of tag_data entries) */
		long count = (long)read32(at);
		unsigned long entries = read32(at + 4), index;

		if (count <= 0 || !region_holds(region, entries, (unsigned long)count * TAG_DATA_SIZE))
			return;
		for (index = 0; index < (unsigned long)count; index++)
			swap_text_data(region, region->base + (entries - region->address) + index * TAG_DATA_SIZE);
	}
	else if (group_tag == HUD_MESSAGE_TEXT_GROUP)
	{
		swap_text_data(region, at);
	}
}

void tag_byte_swap_tags(void *base, unsigned long base_address, unsigned long size)
{
	struct swap_region region;
	unsigned long instances, count, index;
	unsigned long missing_groups[16];
	unsigned long missing_counts[16];
	int missing = 0, missing_index;
	unsigned long model_parts = 0;

	csmemset(&region, 0, sizeof(region));
	region.base = base;
	region.address = base_address;
	region.size = size;
	instances = read32(region.base + TAG_HEADER_INSTANCES);
	count = read32(region.base + TAG_HEADER_COUNT);
	if (!region_holds(&region, instances, count * TAG_INSTANCE_SIZE))
	{
		error(_error_silent, "tag byte swap: the tag index is not in the tag data");
		return;
	}
	for (index = 0; index < count; index++)
	{
		unsigned char *instance = region.base + (instances - base_address) + index * TAG_INSTANCE_SIZE;
		unsigned long group_tag = read32(instance + TAG_INSTANCE_GROUP);
		unsigned long address = read32(instance + TAG_INSTANCE_ADDRESS);
		long layout = group_layout(group_tag);

		/* (a structure bsp's tag is read with its bsp) */
		if (group_tag == STRUCTURE_BSP_GROUP || !region_holds(&region, address, 4))
			continue;
		if (layout == NONE)
		{
			for (missing_index = 0; missing_index < missing; missing_index++)
			{
				if (missing_groups[missing_index] == group_tag)
					break;
			}
			if (missing_index == missing && missing < 16)
			{
				missing_groups[missing] = group_tag;
				missing_counts[missing++] = 0;
			}
			if (missing_index < 16)
				missing_counts[missing_index]++;
			continue;
		}
		if (!region_holds(&region, address, tag_swap_layouts[layout].size))
		{
			report(&region, (unsigned long)layout, 0, 1, address);
			continue;
		}
		region.tags++;
		swap_layout(&region, (unsigned long)layout, address, 0);
		swap_tag_text(&region, group_tag, address);
		if (group_tag == MODEL_GROUP)
			swap_model_indices(&region, address, &model_parts);
		/* (an animation's frames, in tag data: model_animations.c reads them) */
		if (group_tag == ANIMATION_GRAPH_GROUP)
			animation_graph_byte_swap_data(region.base + (address - region.address));
	}
	error(_error_silent, "tag byte swap: %lu of %lu tags, %lu blocks, %lu elements; %lu blocks not swapped",
		region.tags, count, region.blocks, region.elements, region.bad_blocks);
	error(_error_silent, "tag byte swap: %lu model parts' indices", model_parts);
	for (missing_index = 0; missing_index < missing; missing_index++)
	{
		error(_error_silent, "tag byte swap: no layout for '%c%c%c%c' (%lu tags, left as they are)",
			(char)(missing_groups[missing_index] >> 24), (char)(missing_groups[missing_index] >> 16),
			(char)(missing_groups[missing_index] >> 8), (char)missing_groups[missing_index],
			missing_counts[missing_index]);
	}
}

void tag_byte_swap_structure_bsp(void *bsp, unsigned long bsp_address, unsigned long region_address,
	unsigned long region_size)
{
	struct swap_region region;
	long layout = group_layout(STRUCTURE_BSP_GROUP);

	csmemset(&region, 0, sizeof(region));
	region.base = (unsigned char *)bsp - (bsp_address - region_address);
	region.address = region_address;
	region.size = region_size;
	if (layout == NONE || !region_holds(&region, bsp_address, tag_swap_layouts[layout].size))
	{
		error(_error_silent, "tag byte swap: the structure bsp cannot be swapped");
		return;
	}
	swap_layout(&region, (unsigned long)layout, bsp_address, 0);
	/* (the clusters' visibility: longs in tag data) */
	swap_long_data(&region, (const unsigned char *)bsp + structure_bsp_cluster_data_offset());
	error(_error_silent, "tag byte swap: structure bsp: %lu blocks, %lu elements; %lu blocks not swapped",
		region.blocks, region.elements, region.bad_blocks);
}
