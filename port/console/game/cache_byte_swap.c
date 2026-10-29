/*
CACHE_BYTE_SWAP.C

The cache files' headers and tag index in a big-endian console's byte
order (cache_byte_swap.h). Their layouts are the game's
(source/cache/cache_files.c): fixed fields of 4 bytes, swapped by offset.
The tags' own data is swapped by its definitions, group by group, as it is
added (port/console/game/tag_byte_swap.c, once there).
*/

#include <string.h>

#include "cache_byte_swap.h"

#define HEADER_SIGNATURE 0x68656164UL /* 'head' */
#define TAGS_SIGNATURE 0x74616773UL /* 'tags' */
#define STRUCTURE_BSP_SIGNATURE 0x73627370UL /* 'sbsp' */

static unsigned long swap32(unsigned long value)
{
	return ((value & 0xFFUL) << 24) | ((value & 0xFF00UL) << 8) | ((value >> 8) & 0xFF00UL) |
		((value >> 24) & 0xFFUL);
}

static unsigned long read32(const unsigned char *at)
{
	unsigned long value;

	memcpy(&value, at, 4);
	return value;
}

static void swap_at(unsigned char *base, unsigned long offset)
{
	unsigned long value = swap32(read32(base + offset));

	memcpy(base + offset, &value, 4);
}

static void swap_words(unsigned char *base, unsigned long count)
{
	unsigned long index;

	for (index = 0; index < count; index++)
		swap_at(base, index * 4);
}

void cache_file_header_byte_swap(void *header)
{
	unsigned char *bytes = header;
	/* header_signature, version, file_length, tag_data_offset,
	tag_data_size, checksum, footer_signature */
	static const unsigned short offsets[] = { 0x0, 0x4, 0x8, 0x10, 0x14, 0x64, 0x7FC };
	unsigned long index;
	unsigned char byte;

	if (read32(bytes) != swap32(HEADER_SIGNATURE))
		return;
	for (index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++)
		swap_at(bytes, offsets[index]);
	/* scenario_type, a short */
	byte = bytes[0x60];
	bytes[0x60] = bytes[0x61];
	bytes[0x61] = byte;
}

/* the cache file tag header: tag_instances, scenario_tag_index, checksum,
tag_count, vertex_buffer_count, vertex_buffers, index_buffer_count,
index_buffers, signature */
#define TAG_HEADER_WORDS 9
#define TAG_HEADER_SIGNATURE_OFFSET 0x20
/* a tag instance: group_tag, parent_group_tags[2], tag_index, name,
base_address, unused[2] */
#define TAG_INSTANCE_WORDS 8
/* a buffer's description (a D3DResource: Common, Data, Lock) */
#define BUFFER_WORDS 3

/* count words at Xbox address address, if they are all inside the data */
static void swap_part(unsigned char *bytes, unsigned long base_address, unsigned long size, unsigned long address,
	unsigned long count)
{
	unsigned long offset = address - base_address;

	if (!address || offset >= size || count > (size - offset) / 4)
		return;
	swap_words(bytes + offset, count);
}

int cache_file_tags_byte_swap(void *base, unsigned long base_address, unsigned long size)
{
	unsigned char *bytes = base;
	unsigned long tag_count, vertex_buffer_count, index_buffer_count;

	if (size < TAG_HEADER_WORDS * 4 || read32(bytes + TAG_HEADER_SIGNATURE_OFFSET) != swap32(TAGS_SIGNATURE))
		return 0;
	swap_words(bytes, TAG_HEADER_WORDS);
	tag_count = read32(bytes + 0xC);
	vertex_buffer_count = read32(bytes + 0x10);
	index_buffer_count = read32(bytes + 0x18);
	if (tag_count <= 0xFFFF)
		swap_part(bytes, base_address, size, read32(bytes + 0x0), tag_count * TAG_INSTANCE_WORDS);
	if (vertex_buffer_count <= 0x100000)
		swap_part(bytes, base_address, size, read32(bytes + 0x14), vertex_buffer_count * BUFFER_WORDS);
	if (index_buffer_count <= 0x100000)
		swap_part(bytes, base_address, size, read32(bytes + 0x1C), index_buffer_count * BUFFER_WORDS);
	return 1;
}

/* base_address, vertex_buffer_count, vertex_buffers, index_buffer_count,
index_buffers, signature */
int cache_file_structure_bsp_byte_swap(void *base, unsigned long base_address, unsigned long size)
{
	unsigned char *bytes = base;
	unsigned long vertex_buffer_count, index_buffer_count;

	if (size < 0x18 || read32(bytes + 0x14) != swap32(STRUCTURE_BSP_SIGNATURE))
		return 0;
	swap_words(bytes, 6);
	vertex_buffer_count = read32(bytes + 0x4);
	index_buffer_count = read32(bytes + 0xC);
	if (vertex_buffer_count <= 0x100000)
		swap_part(bytes, base_address, size, read32(bytes + 0x8), vertex_buffer_count * BUFFER_WORDS);
	if (index_buffer_count <= 0x100000)
		swap_part(bytes, base_address, size, read32(bytes + 0x10), index_buffer_count * BUFFER_WORDS);
	return 1;
}
