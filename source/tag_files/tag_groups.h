/*
TAG_GROUPS.H

header included in hcex build.
*/

#ifndef __TAG_GROUPS_H
#define __TAG_GROUPS_H
#pragma once

/* ---------- headers */

#include "memory/byte_swapping.h"

/* ---------- constants */

enum tag_field_type
{
	_tag_field_string = 0,
	_tag_field_char_integer = 1,
	_tag_field_short_integer = 2,
	_tag_field_long_integer = 3,
	_tag_field_enum = 6,
	_tag_field_word_flags = 8,
	_tag_field_byte_flags = 9,
	_tag_field_real_point3d = 17,
	_tag_field_real_plane2d = 23,
	_tag_field_real_plane3d = 24,
	_tag_field_tag_reference = 33,
	_tag_field_block = 34,
	_tag_field_data = 37,
	_tag_field_pad = 40,
	_tag_field_terminator = 44,
};

/* ---------- macros */

#ifdef HALO_CONSOLE_INLINE_ACCESSORS
/* port: a console's busiest units', inline (tag_block_get_element_fast, below) */
#define TAG_BLOCK_GET_ELEMENT(block_address, index, type) ((type *)tag_block_get_element_fast((block_address), (index), sizeof(type)))
#else
#define TAG_BLOCK_GET_ELEMENT(block_address, index, type) ((type *)tag_block_get_element_with_size((block_address), (index), sizeof(type)))
#endif

/* ---------- structures */

typedef void (*byte_swap_block_proc)(void *);
typedef boolean (*postprocess_block_proc)(void *, boolean);
typedef byte *(*format_block_proc)(long, struct tag_block *, long, byte *);
typedef void (*delete_block_proc)(struct tag_block *, long);
typedef void (*byte_swap_data_proc)(void *, void *, long);

struct tag_enum_definition
{
	long count;
	char **names;
	void *unused;
};

struct tag_field
{
	short type;
	word pad;
	char *name;
	void *definition;
};

struct tag_data_definition
{
	char *name;
	unsigned long flags;
	long maximum_size;
	byte_swap_data_proc byte_swap_data;
};

struct tag_block_definition
{
	char *name;
	unsigned long flags;
	long maximum_element_count;
	long element_size;
	void *default_element;
	struct tag_field *fields;
	byte_swap_block_proc byte_swap_block;
	postprocess_block_proc postprocess_block;
	format_block_proc format_block;
	delete_block_proc delete_block;
	byte_swap_code *byte_swap_codes;
};

struct tag_block
{
	long count;
	XPTR(void) address;
	XPTR(struct tag_block_definition) definition;
};
#ifdef HALO_64BIT

typedef char tag_block_size_assert[
	sizeof(struct tag_block) == 0xC ? 1 : -1];
#endif

struct tag_reference
{
	unsigned long group_tag;
	XPTR(char) name;
	long name_length;
	long index;
};
#ifdef HALO_64BIT

typedef char tag_reference_size_assert[
	sizeof(struct tag_reference) == 0x10 ? 1 : -1];
#endif

struct tag_reference_definition
{
	unsigned long flags;
	unsigned long group_tag;
	unsigned long *group_tags;
};

#ifndef HALO_64BIT
typedef char tag_reference_definition_size_assert[
	sizeof(struct tag_reference_definition) == 0xC ? 1 : -1];

#endif
struct tag_data
{
	long size;
	unsigned long pad;
	long file_offset;
	XPTR(void) address;
	XPTR(struct tag_data_definition) definition;
};
#ifdef HALO_ADDRESS_TRANSLATED

typedef char tag_data_size_assert[
	sizeof(struct tag_data) == 0x14 ? 1 : -1];

#define TAG_BLOCK_ADDRESS(block) XBOX_POINTER(void, (block)->address)
#define TAG_DATA_ADDRESS(data) XBOX_POINTER(void, (data)->address)
#define TAG_REFERENCE_NAME(reference) XBOX_POINTER(char, (reference)->name)
#endif

/* ---------- prototypes/TAG_GROUPS.C */

long verify_tag_reference(struct tag_reference const *reference);
/* port: zeros in place of an element, data or tag that is not there */
void *tag_empty_data(void);
void *tag_data_get_pointer(struct tag_data const *data, long offset, long size);
void *tag_block_get_element_with_size(struct tag_block const *block, long index, long element_size);

#ifdef HALO_CUSTOM_EDITION
/* port: where a Custom Edition map's tags are (ce_map_checks.h's
CE_IMAGE_TAG_CACHE_BASE and _SIZE, which ce_map_checks.c checks these
against), for the inline TAG_BLOCK_GET_ELEMENT */
#define TAG_GROUPS_CE_TAG_CACHE_BASE 0x40440000UL
#define TAG_GROUPS_CE_TAG_CACHE_SIZE 0x01700000UL
#endif

#ifdef HALO_CONSOLE_INLINE_ACCESSORS
/* port: a console's busiest units' TAG_BLOCK_GET_ELEMENT
(halo_port_features.h): the element tag_block_get_element_with_size gives,
without the call, when the index is in the block, the block has an address
and, in a Custom Edition map, the element is in its tag cache
(ce_tags_pointer's bounds, ce_map_checks.c). Anything else is the
function's, which checks it again */
#ifdef HALO_CUSTOM_EDITION
extern boolean cache_file_is_ce;
#endif

static __inline__ void *tag_block_get_element_fast(
	struct tag_block const *block,
	long index,
	long element_size)
{
	if (index >= 0 && index < block->count && block->address)
	{
#ifdef HALO_CUSTOM_EDITION
		if (cache_file_is_ce)
		{
			unsigned long address = (unsigned long)block->address + index * element_size;

			if (!element_size || (address >= TAG_GROUPS_CE_TAG_CACHE_BASE && element_size > 0 &&
				address - TAG_GROUPS_CE_TAG_CACHE_BASE <= TAG_GROUPS_CE_TAG_CACHE_SIZE &&
				(unsigned long)element_size <= TAG_GROUPS_CE_TAG_CACHE_SIZE - (address - TAG_GROUPS_CE_TAG_CACHE_BASE)))
			{
				return xbox_pointer(address);
			}
		}
		else
#endif
#ifdef HALO_ADDRESS_TRANSLATED
		return (void *)((byte *)TAG_BLOCK_ADDRESS(block) + (index * element_size));
#else
		return (void *)((byte *)block->address + (index * element_size));
#endif
	}

	return tag_block_get_element_with_size(block, index, element_size);
}
#endif

/* ---------- prototypes/CACHE_FILES.C */

long tag_loaded(long group_tag, const char *name);

void *tag_get(long group_tag, long tag_index);

/* ---------- globals */

/* ---------- public code */

#endif // __TAG_GROUPS_H
