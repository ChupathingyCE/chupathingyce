/*
TAG_GROUPS.C
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "tag_files.h"
#include "byte_swapping.h"
#include "tag_groups.h"

#ifdef HALO_CUSTOM_EDITION
void *ce_tags_pointer(unsigned long address, long size);
boolean cache_file_tags_are_ce(void);
boolean tag_index_is_group(long tag_index, long group_tag);
#endif

/* ---------- constants */

enum
{
	/* port: the most bytes of the empty data (tag_empty_data): more than
	any tag's root or any block's element */
	TAG_EMPTY_DATA_SIZE = 0x10000,
};

/* ---------- globals */

/* port: (tag_empty_data; in the Xbox address space on the 64-bit builds,
where a tag's pointers to it are Xbox addresses: xbox_address.h) */
#ifdef HALO_64BIT
static unsigned long *tag_empty_data_bytes;
#else
static unsigned long tag_empty_data_bytes[TAG_EMPTY_DATA_SIZE / sizeof(unsigned long)];
#endif

/* ---------- private code */

/* port: an index past what it indexes, logged once */
static void tag_index_error(
	char const *what,
	long index,
	long count)
{
	static boolean logged = FALSE;

	if (!logged)
	{
		logged = TRUE;
		error(_error_silent, "#%ld is not a %s index in [#0,#%ld): an empty one is used", index, what, count);
	}

	return;
}

/* ---------- public code */

/* port: what an index into a tag block, a tag's data or the tags that is
not one gives (tag_block_get_element_with_size, tag_data_get_pointer,
tag_get): TAG_EMPTY_DATA_SIZE bytes of zeros, zeroed again each time, in
place of whatever lies past the block, the data or the tags. Whatever
reads it reads an element or tag with nothing in it (no elements in its
blocks, no tags referenced, every index 0); whatever writes it writes
nowhere that matters */
void *tag_empty_data(
	void)
{
#ifdef HALO_64BIT
	if (!tag_empty_data_bytes)
		tag_empty_data_bytes = malloc(TAG_EMPTY_DATA_SIZE);
	if (!tag_empty_data_bytes)
		return NULL;
	csmemset(tag_empty_data_bytes, 0, TAG_EMPTY_DATA_SIZE);
#else
	csmemset(tag_empty_data_bytes, 0, sizeof(tag_empty_data_bytes));
#endif

	return tag_empty_data_bytes;
}

long verify_tag_reference(
	const struct tag_reference *reference)
{
	long index;

	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3055, reference);
#ifdef HALO_CUSTOM_EDITION
	/* port: a Custom Edition map's tags are not found by name. A "protected"
	map (made with a map protector) has its tags' names replaced, most with
	the same one, and its references' emptied, so the name finds no tag or
	another; the reference's index, a tag of its group, is the tag
	(cache_files.c's tag_index_is_group) */
	if (cache_file_tags_are_ce())
		return tag_index_is_group(reference->index, reference->group_tag) ? reference->index : NONE;
#endif
#ifdef HALO_64BIT
	index = tag_loaded(reference->group_tag, TAG_REFERENCE_NAME(reference));
#else
	index = tag_loaded(reference->group_tag, reference->name);
#endif
	
	match_vassert(
		"c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3061, reference->index==index,
		csprintf(temporary,
			"tag reference \"%s\" and actual index do not match: is %08lX but should be %08lX",
#ifdef HALO_64BIT
			TAG_REFERENCE_NAME(reference),
#else
			reference->name,
#endif
			reference->index,
			index));

	return index;
}

void* tag_data_get_pointer(
	const struct tag_data *data,
	long offset, 
	long size) 
{
	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3073, size>=0);
	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3074, offset>=0 && offset+size<=data->size);
	/* port: bytes past the data are the empty data's (tag_empty_data), as
	far as they go */
	if (size < 0 || offset < 0 || offset > data->size || size > data->size - offset || (size && !data->address))
	{
		tag_index_error("data", offset, data->size);
		return size <= TAG_EMPTY_DATA_SIZE ? tag_empty_data() : NULL;
	}

#ifdef HALO_CUSTOM_EDITION
	/* port: (a Custom Edition map's, only in its tag cache: ce_map_checks.c) */
	return ce_tags_pointer((unsigned long)data->address + offset, size);
#elif defined(HALO_64BIT)
	return (void *)((byte *)TAG_DATA_ADDRESS(data) + offset);
#else
	return (void *)((byte *)data->address + offset);
#endif
}

#if defined(HALO_CUSTOM_EDITION) && !defined(HALO_64BIT)
extern boolean cache_file_is_ce;
#endif

void *tag_block_get_element_with_size(
	const struct tag_block *block,
	long index, 
	long element_size) 
{
	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3084, block);
	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3085, block->count>=0);
#ifndef HALO_64BIT
#ifdef HALO_CUSTOM_EDITION
	/* port: a Custom Edition map's blocks keep the definitions' addresses in
	Halo PC's executable, not this one's (cache_files.c) */
	if (!cache_file_is_ce)
#endif
	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3086, !block->definition || block->definition->element_size==element_size);
#endif

	match_vassert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3089, index>=0 && index<block->count,
		csprintf(temporary,
			"#%d is not a valid %s index in [#0,#%d)",
			index,
#ifdef HALO_64BIT
			"<unknown>", block->count));
#else
			block->definition ? block->definition->name : "<unknown>", block->count));
#endif
	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3090, block->address);
	/* port: an element past the block (an index a map's data gave, which
	nothing checked) is the empty data (tag_empty_data), not whatever lies
	past the block */
	if (index < 0 || index >= block->count || !block->address)
	{
		tag_index_error("block element", index, block->count);
		return element_size <= TAG_EMPTY_DATA_SIZE ? tag_empty_data() : NULL;
	}

#ifdef HALO_CUSTOM_EDITION
	/* port: (a Custom Edition map's, only in its tag cache: ce_map_checks.c) */
	return ce_tags_pointer((unsigned long)block->address + index * element_size, element_size);
#elif defined(HALO_64BIT)
	return (void *)((byte *)TAG_BLOCK_ADDRESS(block) + (index * element_size));
#else
	return (void *)((byte *)block->address + (index * element_size));
#endif
}
