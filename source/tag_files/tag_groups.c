/*
TAG_GROUPS.C
*/

/* ---------- headers */

#include "cseries.h"
#include "tag_files.h"
#include "byte_swapping.h"
#include "tag_groups.h"

#ifdef HALO_CUSTOM_EDITION
void *ce_tags_pointer(unsigned long address, long size);
#endif

/* ---------- public code */

long verify_tag_reference(
	const struct tag_reference *reference)
{
	long index;

	match_assert("c:\\halo\\SOURCE\\tag_files\\tag_groups.c", 3055, reference);
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

#ifdef HALO_CUSTOM_EDITION
	/* port: (a Custom Edition map's, only in its tag cache: ce_map_checks.c) */
	return ce_tags_pointer((unsigned long)block->address + index * element_size, element_size);
#elif defined(HALO_64BIT)
	return (void *)((byte *)TAG_BLOCK_ADDRESS(block) + (index * element_size));
#else
	return (void *)((byte *)block->address + (index * element_size));
#endif
}
