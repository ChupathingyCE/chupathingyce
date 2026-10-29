/*
TAG_BYTE_SWAP.C

Applies the generated layouts (tag_byte_swap.h) to a map's tags as they
load: each tag's fields by its group's layout, then each of its blocks'
elements by theirs, recursively. Everything is checked against the data the
tags were read into, so a layout that does not match a map's data stops at
the bad block instead of writing past it, and says so.
*/

#include <stdlib.h>

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

/* a bit for each byte of the tags being swapped that has been (NULL when
there is no memory for it: every field is then swapped as it is reached).
Halo CE+'s maps store bytes that are another block's or tag's once, and
point both at them: they are swapped once, as the first to reach them */
static unsigned char *swap_done_base;
static unsigned long swap_done_size;
static unsigned long *swap_done;

/* whether the op bytes at at were swapped already; marks them if not */
static int swap_done_mark(const unsigned char *at, unsigned long op)
{
	unsigned long offset, index;

	if (!swap_done || at < swap_done_base || (unsigned long)(at - swap_done_base) >= swap_done_size)
		return 0;
	offset = (unsigned long)(at - swap_done_base);
	for (index = 0; index < op && offset + index < swap_done_size; index++)
	{
		if (swap_done[(offset + index) / 32] & (1UL << ((offset + index) % 32)))
			return 1;
	}
	for (index = 0; index < op && offset + index < swap_done_size; index++)
		swap_done[(offset + index) / 32] |= 1UL << ((offset + index) % 32);
	return 0;
}

static void swap_fields(unsigned char *at, unsigned long op, unsigned long count)
{
	unsigned long index;

	for (index = 0; index < count; index++, at += op)
	{
		if (swap_done_mark(at, op))
			continue;
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
#define MODEL_TRIANGLE_SIZE 6 /* three shorts */
#define TRIANGLE_BUFFER_STRIP 1

/* the parts' index ranges, gathered over every model before any is swapped:
parts may share their indices or overlap (Halo CE+X maps do), and a word
swapped twice is back in the map's order. The words of a triangles block
are kept too, as done: its layout swapped them with the block. Sorted and
merged as the table fills; a map with more ranges than this, merged, has
the rest swapped part by part */
#define MAXIMUM_INDEX_RANGES 4096

struct index_range
{
	unsigned long address;
	unsigned long end;
	unsigned long done; /* a triangles block's: already swapped */
};

static struct index_range index_ranges[MAXIMUM_INDEX_RANGES];
static unsigned long index_range_count;

static int index_range_compare(const void *a, const void *b)
{
	const struct index_range *first = a, *second = b;

	if (first->done != second->done)
		return first->done < second->done ? -1 : 1;
	if (first->address != second->address)
		return first->address < second->address ? -1 : 1;
	return 0;
}

/* sorted (the ranges to swap, then the done ones), each kind's overlapping
ranges made one */
static void index_ranges_merge(void)
{
	unsigned long index, count = 0;

	if (!index_range_count)
		return;
	qsort(index_ranges, index_range_count, sizeof(index_ranges[0]), index_range_compare);
	for (index = 1; index < index_range_count; index++)
	{
		struct index_range *last = &index_ranges[count];

		if (index_ranges[index].done == last->done && index_ranges[index].address < last->end)
		{
			if (index_ranges[index].end > last->end)
				last->end = index_ranges[index].end;
		}
		else
		{
			index_ranges[++count] = index_ranges[index];
		}
	}
	index_range_count = count + 1;
}

/* whether the range was kept (or there was no room for it) */
static int index_range_add(unsigned long address, unsigned long size, unsigned long done)
{
	if (index_range_count == MAXIMUM_INDEX_RANGES)
		index_ranges_merge();
	if (index_range_count == MAXIMUM_INDEX_RANGES)
		return 0;
	index_ranges[index_range_count].address = address;
	index_ranges[index_range_count].end = address + size;
	index_ranges[index_range_count].done = done;
	index_range_count++;
	return 1;
}

static void swap_model_indices(struct swap_region *region, unsigned long address, unsigned long *parts_seen,
	unsigned long *overflow)
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
			long triangle_count = (long)read32(part + MODEL_PART_TRIANGLES);
			unsigned long indices = read32(buffer + 8), triangles = read32(part + MODEL_PART_TRIANGLES + 4);
			unsigned long index_count;

			if (triangle_count > 0 &&
				region_holds(region, triangles, (unsigned long)triangle_count * MODEL_TRIANGLE_SIZE))
			{
				index_range_add(triangles, (unsigned long)triangle_count * MODEL_TRIANGLE_SIZE, 1);
			}
			type = read16(buffer);
			if (count <= 0)
				continue;
			index_count = type == TRIANGLE_BUFFER_STRIP ? (unsigned long)count + 2 : (unsigned long)count * 3;
			/* (the triangles block's own words were swapped with the block) */
			if (triangle_count > 0 && triangles == indices)
				continue;
			if (!region_holds(region, indices, index_count * 2))
				continue;
			(*parts_seen)++;
			if (!index_range_add(indices, index_count * 2, 0))
			{
				swap_fields(region->base + (indices - region->address), 2, index_count);
				(*overflow)++;
			}
		}
	}
}

/* every gathered range swapped once, but for the words its triangles
blocks already had; the count of ranges swapped */
static unsigned long swap_index_ranges(struct swap_region *region)
{
	unsigned long index, done, swapped = 0;

	index_ranges_merge();
	for (done = 0; done < index_range_count && !index_ranges[done].done; done++)
		;
	for (index = 0; index < index_range_count && !index_ranges[index].done; index++)
	{
		unsigned long at = index_ranges[index].address, end = index_ranges[index].end;
		unsigned long next;

		while (at < end)
		{
			/* (the done ranges behind this one are behind every later one) */
			while (done < index_range_count && index_ranges[done].end <= at)
				done++;
			next = done < index_range_count && index_ranges[done].address < end ? index_ranges[done].address : end;
			if (next > at)
				swap_fields(region->base + (at - region->address), 2, (next - at) / 2);
			at = next < end ? index_ranges[done].end : end;
		}
		swapped++;
	}
	index_range_count = 0;
	return swapped;
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

/* a tag's root, for finding tags that share one (shared_tags_find) */
struct swap_root
{
	unsigned long address;
	unsigned long group_tag;
	unsigned long index;
};

static int swap_root_compare(const void *a, const void *b)
{
	const struct swap_root *left = a, *right = b;

	if (left->address != right->address)
		return left->address < right->address ? -1 : 1;
	if (left->group_tag != right->group_tag)
		return left->group_tag < right->group_tag ? -1 : 1;
	return left->index < right->index ? -1 : left->index > right->index;
}

/* a byte for each of count tags: 1 for a tag with the same group and root as
one before it in the table. Halo CE+'s maps have such tags (its tools store
a tag that is byte for byte another's once and point both at it); swapping
both would swap their data twice, back to the map's order. NULL if there is
no memory for it (every tag is then swapped, as before) */
static unsigned char *shared_tags_find(const unsigned char *instances, unsigned long count)
{
	struct swap_root *roots = malloc((count ? count : 1) * sizeof(*roots));
	unsigned char *shared = calloc(count ? count : 1, 1);
	unsigned long index;

	if (!roots || !shared)
	{
		free(roots);
		free(shared);
		return NULL;
	}
	for (index = 0; index < count; index++)
	{
		roots[index].address = read32(instances + index * TAG_INSTANCE_SIZE + TAG_INSTANCE_ADDRESS);
		roots[index].group_tag = read32(instances + index * TAG_INSTANCE_SIZE + TAG_INSTANCE_GROUP);
		roots[index].index = index;
	}
	qsort(roots, count, sizeof(*roots), swap_root_compare);
	for (index = 1; index < count; index++)
	{
		if (roots[index].address && roots[index].address == roots[index - 1].address &&
			roots[index].group_tag == roots[index - 1].group_tag)
		{
			shared[roots[index].index] = 1;
		}
	}
	free(roots);
	return shared;
}

void tag_byte_swap_tags(void *base, unsigned long base_address, unsigned long size)
{
	unsigned char *shared;
	unsigned long shared_count = 0;
	struct swap_region region;
	unsigned long instances, count, index;
	unsigned long missing_groups[16];
	unsigned long missing_counts[16];
	int missing = 0, missing_index;
	unsigned long model_parts = 0, model_part_overflow = 0, model_index_ranges;

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
	shared = shared_tags_find(region.base + (instances - base_address), count);
	swap_done = calloc((size + 31) / 32, sizeof(*swap_done));
	swap_done_base = region.base;
	swap_done_size = size;
	for (index = 0; index < count; index++)
	{
		unsigned char *instance = region.base + (instances - base_address) + index * TAG_INSTANCE_SIZE;
		unsigned long group_tag = read32(instance + TAG_INSTANCE_GROUP);
		unsigned long address = read32(instance + TAG_INSTANCE_ADDRESS);
		long layout = group_layout(group_tag);

		/* (a tag that shares an earlier one's data was swapped as it) */
		if (shared && shared[index])
		{
			shared_count++;
			continue;
		}

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
			swap_model_indices(&region, address, &model_parts, &model_part_overflow);
		/* (an animation's frames, in tag data: model_animations.c reads them) */
		if (group_tag == ANIMATION_GRAPH_GROUP)
			animation_graph_byte_swap_data(region.base + (address - region.address));
	}
	free(shared);
	if (shared_count)
		error(_error_silent, "tag byte swap: %lu tags share another's data, swapped once with it", shared_count);
	/* (the models' indices, once all are known) */
	model_index_ranges = swap_index_ranges(&region);
	free(swap_done);
	swap_done = NULL;
	error(_error_silent, "tag byte swap: %lu of %lu tags, %lu blocks, %lu elements; %lu blocks not swapped",
		region.tags, count, region.blocks, region.elements, region.bad_blocks);
	error(_error_silent, "tag byte swap: %lu model parts' indices, in %lu ranges", model_parts, model_index_ranges);
	if (model_part_overflow)
	{
		error(_error_silent, "tag byte swap: %lu model parts' indices swapped on their own (more than %d ranges)",
			model_part_overflow, MAXIMUM_INDEX_RANGES);
	}
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
	swap_done = calloc((region_size + 31) / 32, sizeof(*swap_done));
	swap_done_base = region.base;
	swap_done_size = region_size;
	swap_layout(&region, (unsigned long)layout, bsp_address, 0);
	/* (the clusters' visibility: longs in tag data) */
	swap_long_data(&region, (const unsigned char *)bsp + structure_bsp_cluster_data_offset());
	free(swap_done);
	swap_done = NULL;
	error(_error_silent, "tag byte swap: structure bsp: %lu blocks, %lu elements; %lu blocks not swapped",
		region.blocks, region.elements, region.bad_blocks);
}
