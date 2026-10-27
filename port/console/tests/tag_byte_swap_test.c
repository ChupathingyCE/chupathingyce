/*
TAG_BYTE_SWAP_TEST.C

tag_byte_swap_tags (port/console/game/tag_byte_swap.c) on a real Xbox map,
read as a big-endian console reads it: after the swap every block it follows
must have been taken, and a tag the swap has already done must be left as it
is. Usage: tag_byte_swap_test <map> (a map of assets/maps), or
tag_byte_swap_test --synthetic (a model made here, no map needed); built by
port/console/tests/run_tag_byte_swap_test.sh.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "errors.h"

void tag_byte_swap_tags(void *base, unsigned long base_address, unsigned long size);
void tag_byte_swap_structure_bsp(void *bsp, unsigned long bsp_address, unsigned long region_address,
	unsigned long region_size);
int cache_file_tags_byte_swap(void *base, unsigned long base_address, unsigned long size);

/* the swap's count of each byte's swaps (tag_byte_swap.c, built with
TAG_BYTE_SWAP_EMULATE_BIG_ENDIAN) */
extern unsigned char *tag_byte_swap_test_base;
extern unsigned long tag_byte_swap_test_size;
extern unsigned char *tag_byte_swap_test_counts;

/* where struct structure_bsp keeps its clusters' visibility (a tag_data) */
#define STRUCTURE_BSP_CLUSTER_DATA 0x140

#define TAG_CACHE_ADDRESS 0x803A6000UL

static int reports, summary_bad = -1;

void error(int type, const char *format, ...)
{
	char line[512];
	va_list arguments;
	unsigned long a, b, c, d, bad;

	(void)type;
	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	if (sscanf(line, "tag byte swap: %lu of %lu tags, %lu blocks, %lu elements; %lu blocks not swapped", &a, &b, &c,
		&d, &bad) == 5)
		summary_bad = (int)bad;
	if (strstr(line, "is not one"))
		reports++;
	printf("%s\n", line);
}

void animation_graph_byte_swap_data(void *graph) { (void)graph; }
unsigned long structure_bsp_cluster_data_offset(void) { return STRUCTURE_BSP_CLUSTER_DATA; }

/* the cache_byte_swap.c routine reads with memcpy: the host being little
endian, the tag header and index are made big-endian here instead */
static void be32(unsigned char *at, unsigned long value)
{
	at[0] = (unsigned char)(value >> 24);
	at[1] = (unsigned char)(value >> 16);
	at[2] = (unsigned char)(value >> 8);
	at[3] = (unsigned char)value;
}

static unsigned long le32(const unsigned char *at)
{
	return at[0] | (at[1] << 8) | (at[2] << 16) | ((unsigned long)at[3] << 24);
}

static void swap_words(unsigned char *base, unsigned long address, unsigned long size, unsigned long at,
	unsigned long words)
{
	unsigned long offset = at - address, index;

	if (offset >= size || words > (size - offset) / 4)
		return;
	for (index = 0; index < words; index++)
		be32(base + offset + index * 4, le32(base + offset + index * 4));
}

/* how many bytes the swap changes in a retail map (a change to the tables
that adds or drops a swapped field shows here: it is to be checked, then the
number updated), and in its structure bsp; 0 for a map not listed */
static const struct { const char *name; unsigned long tags, bsp; } expected[] =
{
	{ "bloodgulch.map", 2144338UL, 940272UL },
	{ "ui.map", 458380UL, 1686UL },
};

/* no byte of what was swapped swapped twice (a block walked with the wrong
stride, or reached from two tags: it is back in the map's order, which a
big-endian machine reads as a count of 16777216 for 1, or vertices as
spikes), and the total as expected */
static int check_counts(const char *what, const unsigned char *counts, unsigned long size, unsigned long want)
{
	unsigned long index, once = 0, twice = 0, first = 0;

	for (index = 0; index < size; index++)
	{
		if (counts[index] == 1)
			once++;
		else if (counts[index] > 1 && !twice++)
			first = index;
	}
	printf("%s: %lu bytes swapped, %lu swapped more than once\n", what, once, twice);
	if (twice)
	{
		printf("FAIL: %s: swapped twice from +0x%lx\n", what, first);
		return 1;
	}
	if (want && once != want)
	{
		printf("FAIL: %s: %lu bytes swapped, expected %lu\n", what, once, want);
		return 1;
	}
	return 0;
}

static unsigned long be32_at(const unsigned char *at)
{
	return ((unsigned long)at[0] << 24) | ((unsigned long)at[1] << 16) | ((unsigned long)at[2] << 8) | at[3];
}

/* a structure bsp as scenario_structure_bsp_load swaps it: the header and
its buffers' descriptions (cache_file_structure_bsp_byte_swap's), then the
bsp by its layout */
static int swap_bsp(const unsigned char *decoded, unsigned long file_offset, unsigned long file_size,
	unsigned long address, unsigned long want)
{
	unsigned char *bsp = malloc(file_size);
	unsigned char *counts = calloc(1, file_size);
	unsigned long bsp_address;
	int failed;

	memcpy(bsp, decoded + file_offset, file_size);
	swap_words(bsp, address, file_size, address, 6);
	swap_words(bsp, address, file_size, be32_at(bsp + 8), be32_at(bsp + 4) * 3);
	swap_words(bsp, address, file_size, be32_at(bsp + 0x10), be32_at(bsp + 0xC) * 3);
	bsp_address = be32_at(bsp);
	reports = 0;
	summary_bad = -1;
	tag_byte_swap_test_base = bsp;
	tag_byte_swap_test_size = file_size;
	tag_byte_swap_test_counts = counts;
	tag_byte_swap_structure_bsp(bsp + (bsp_address - address), bsp_address, address, file_size);
	tag_byte_swap_test_counts = NULL;
	failed = check_counts("structure bsp", counts, file_size, want);
	/* (its header's words are the machine's: the bsp's own are what is counted) */
	if (reports)
	{
		printf("FAIL: the structure bsp has %d blocks not swapped\n", reports);
		failed = 1;
	}
	free(counts);
	free(bsp);
	return failed;
}

static void le16_put(unsigned char *at, unsigned short value)
{
	at[0] = (unsigned char)value;
	at[1] = (unsigned char)(value >> 8);
}

static void le32_put(unsigned char *at, unsigned long value)
{
	at[0] = (unsigned char)value;
	at[1] = (unsigned char)(value >> 8);
	at[2] = (unsigned char)(value >> 16);
	at[3] = (unsigned char)(value >> 24);
}

/* a model made here, as Halo CE+X maps have them: parts that share one
index range, overlap another, and overlap a triangles block (whose words
its layout swaps). Every index word must be swapped once, and read as the
map wrote it */
#define SYNTHETIC_SIZE 0x2000
#define SYNTHETIC_INSTANCES 0x40
#define SYNTHETIC_MODEL 0x100
#define SYNTHETIC_GEOMETRY 0x200
#define SYNTHETIC_PARTS 0x300
#define SYNTHETIC_INDICES 0x1000

static int synthetic_test(void)
{
	/* each part: strip or list, count, its indices' offset, its triangles block's (0 for none) */
	static const struct { unsigned short type; unsigned long count, indices, triangles; } parts[] =
	{
		{ 0, 2, 0x1000, 0 }, /* 0x1000-0x100C */
		{ 0, 2, 0x1000, 0 }, /* the same */
		{ 0, 2, 0x1006, 0 }, /* half over the first, to 0x1012 */
		{ 1, 1, 0x1020, 0 }, /* a strip, 0x1020-0x1026 */
		{ 0, 1, 0x1040, 0x1040 }, /* its own triangles block, 0x1040-0x1046 */
		{ 0, 2, 0x103A, 0 }, /* over that triangles block, 0x103A-0x1046 */
	};
	static const struct { unsigned long start, end; } swapped[] =
	{
		{ 0x1000, 0x1012 }, { 0x1020, 0x1026 }, { 0x103A, 0x1046 },
	};
	unsigned char *tags = calloc(1, SYNTHETIC_SIZE);
	unsigned char *counts = calloc(1, SYNTHETIC_SIZE);
	unsigned long index, part_count = sizeof(parts) / sizeof(parts[0]);
	int failed = 0;

	/* the tag header and index, the console's already */
	be32(tags, TAG_CACHE_ADDRESS + SYNTHETIC_INSTANCES);
	be32(tags + 0xC, 1);
	be32(tags + SYNTHETIC_INSTANCES, 0x6D6F6465UL); /* 'mode' */
	be32(tags + SYNTHETIC_INSTANCES + 0x14, TAG_CACHE_ADDRESS + SYNTHETIC_MODEL);
	/* the model, its geometry and parts, the map's */
	le32_put(tags + SYNTHETIC_MODEL + 0xD0, 1);
	le32_put(tags + SYNTHETIC_MODEL + 0xD4, TAG_CACHE_ADDRESS + SYNTHETIC_GEOMETRY);
	le32_put(tags + SYNTHETIC_GEOMETRY + 0x24, part_count);
	le32_put(tags + SYNTHETIC_GEOMETRY + 0x28, TAG_CACHE_ADDRESS + SYNTHETIC_PARTS);
	for (index = 0; index < part_count; index++)
	{
		unsigned char *part = tags + SYNTHETIC_PARTS + index * 0x68;

		if (parts[index].triangles)
		{
			le32_put(part + 0x38, parts[index].count);
			le32_put(part + 0x3C, TAG_CACHE_ADDRESS + parts[index].triangles);
		}
		le16_put(part + 0x44, parts[index].type);
		le32_put(part + 0x48, parts[index].count);
		le32_put(part + 0x4C, TAG_CACHE_ADDRESS + parts[index].indices);
	}
	/* each index word its own offset */
	for (index = SYNTHETIC_INDICES; index < SYNTHETIC_INDICES + 0x100; index += 2)
		le16_put(tags + index, (unsigned short)index);

	tag_byte_swap_test_base = tags;
	tag_byte_swap_test_size = SYNTHETIC_SIZE;
	tag_byte_swap_test_counts = counts;
	tag_byte_swap_tags(tags, TAG_CACHE_ADDRESS, SYNTHETIC_SIZE);
	tag_byte_swap_test_counts = NULL;
	if (check_counts("synthetic", counts, SYNTHETIC_SIZE, 0))
		failed = 1;
	for (index = 0; index < sizeof(swapped) / sizeof(swapped[0]); index++)
	{
		unsigned long at;

		for (at = swapped[index].start; at < swapped[index].end; at += 2)
		{
			unsigned short value = (unsigned short)((tags[at] << 8) | tags[at + 1]);

			if (counts[at] != 1 || counts[at + 1] != 1 || value != at)
			{
				printf("FAIL: synthetic: the index at +0x%lx reads 0x%x (swapped %d times)\n", at, value, counts[at]);
				failed = 1;
				break;
			}
		}
	}
	if (reports || summary_bad != 0)
	{
		printf("FAIL: synthetic: %d blocks reported not swapped (summary %d)\n", reports, summary_bad);
		failed = 1;
	}
	if (!failed)
		printf("PASS: synthetic\n");
	free(counts);
	free(tags);
	return failed;
}

int main(int argc, char **argv)
{
	unsigned long want_tags = 0, want_bsp = 0, bsp_count = 0, bsp_references = 0, scenario_offset = 0;
	unsigned char *counts;
	unsigned char *scenario_bsps;
	size_t name_index;
	FILE *file;
	unsigned char *raw, *tags;
	unsigned char *body, *decoded;
	long length;
	unsigned long tag_offset, tag_size, count, instances, index, decoded_size;
	int failed = 0;

	if (argc < 2)
	{
		printf("usage: tag_byte_swap_test <map> | --synthetic\n");
		return 2;
	}
	if (!strcmp(argv[1], "--synthetic"))
		return synthetic_test();
	file = fopen(argv[1], "rb");
	if (!file)
	{
		printf("cannot open %s\n", argv[1]);
		return 2;
	}
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	raw = malloc(length);
	if (fread(raw, 1, length, file) != (size_t)length)
		return 2;
	fclose(file);
	tag_offset = le32(raw + 0x10);
	tag_size = le32(raw + 0x14);
	/* (maps are a header and a deflated body, or plain) */
	decoded_size = tag_offset + tag_size + 0x800;
	decoded = calloc(1, decoded_size + length);
	memcpy(decoded, raw, 0x800);
	{
		uLongf out = (uLongf)(decoded_size + length - 0x800);

		body = decoded + 0x800;
		if (uncompress(body, &out, raw + 0x800, length - 0x800) != Z_OK)
			memcpy(body, raw + 0x800, length - 0x800);
	}
	tags = malloc(tag_size);
	memcpy(tags, decoded + tag_offset, tag_size);

	for (name_index = 0; name_index < sizeof(expected) / sizeof(expected[0]); name_index++)
	{
		const char *base_name = strrchr(argv[1], '/');

		base_name = base_name ? base_name + 1 : argv[1];
		if (!strcmp(base_name, expected[name_index].name))
		{
			want_tags = expected[name_index].tags;
			want_bsp = expected[name_index].bsp;
		}
	}
	/* the scenario's structure bsps, read before the tags are swapped */
	{
		unsigned long scenario_index = le32(tags + 4) & 0xFFFF;
		unsigned long instances_address = le32(tags);

		scenario_offset = le32(tags + (instances_address - TAG_CACHE_ADDRESS) + scenario_index * 0x20 + 0x14) -
			TAG_CACHE_ADDRESS;
		bsp_count = le32(tags + scenario_offset + 0x5A4);
		bsp_references = le32(tags + scenario_offset + 0x5A8) - TAG_CACHE_ADDRESS;
	}
	scenario_bsps = malloc(bsp_count * 0x20 + 1);
	memcpy(scenario_bsps, tags + bsp_references, bsp_count * 0x20);
	/* the tag header and index, then the tags, as cache_files.c does */
	{
		unsigned long header_instances = le32(tags);

		count = le32(tags + 0xC);
		instances = header_instances;
		swap_words(tags, TAG_CACHE_ADDRESS, tag_size, TAG_CACHE_ADDRESS, 9);
		swap_words(tags, TAG_CACHE_ADDRESS, tag_size, instances, count * 8);
	}
	counts = calloc(1, tag_size);
	tag_byte_swap_test_base = tags;
	tag_byte_swap_test_size = tag_size;
	tag_byte_swap_test_counts = counts;
	tag_byte_swap_tags(tags, TAG_CACHE_ADDRESS, tag_size);
	tag_byte_swap_test_counts = NULL;
	if (check_counts("tags", counts, tag_size, want_tags))
		failed = 1;
	if (reports || summary_bad != 0)
	{
		printf("FAIL: %d blocks reported not swapped (summary %d)\n", reports, summary_bad);
		failed = 1;
	}
	for (index = 0; index < bsp_count; index++)
	{
		const unsigned char *reference = scenario_bsps + index * 0x20;

		if (swap_bsp(decoded, (unsigned long)le32(reference), le32(reference + 4), le32(reference + 8), want_bsp))
			failed = 1;
	}
	/* a second pass over swapped data must not be what a first one needs: the
	instance index is in the swapped order, so every layout's counts are sane
	only if the first pass took them all */
	(void)index;
	if (!failed)
		printf("PASS: %s\n", argv[1]);
	return failed;
}
