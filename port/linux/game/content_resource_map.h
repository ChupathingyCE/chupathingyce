/*
CONTENT_RESOURCE_MAP.H

A resource map (bitmaps.map, sounds.map, loc.map: Custom Edition's, Halo PC
retail's, HaloMD's, MCC's), read once, header-only: its 16-byte header (its
type: 1 bitmaps, 2 sounds, 3 strings and fonts; the offsets of its paths and
of its table; how many resources) and each 12-byte entry of its table (its
path's offset in the paths, its size, its offset in the file), all
little-endian on every processor. What a loader takes of one (how many, how
large, what it does with a bad entry) stays its own; the checks here are the
ones every reader makes the same way. C89 too.
*/

#ifndef CONTENT_RESOURCE_MAP_H
#define CONTENT_RESOURCE_MAP_H

#include "content_bytes.h"

/* ---------- constants */

#define CONTENT_RESOURCE_HEADER_SIZE 16
#define CONTENT_RESOURCE_ENTRY_SIZE 12

enum
{
	CONTENT_RESOURCE_BITMAPS = 1,
	CONTENT_RESOURCE_SOUNDS = 2,
	CONTENT_RESOURCE_STRINGS = 3
};

/* ---------- types */

struct content_resource_header
{
	uint32_t type;
	uint32_t paths_offset;
	uint32_t table_offset;
	uint32_t count;
};

/* (as it is in the file: a table read whole into an array of these is read
in place by content_resource_table_read) */
struct content_resource_entry
{
	uint32_t path_offset;
	uint32_t size;
	uint32_t offset;
};

typedef char content_resource_entry_size_check[sizeof(struct content_resource_entry) == CONTENT_RESOURCE_ENTRY_SIZE ?
	1 : -1];

/* ---------- code */

/* a resource map's header, from its first CONTENT_RESOURCE_HEADER_SIZE bytes */
CONTENT_BYTES_INLINE void content_resource_header_read(const void *bytes, struct content_resource_header *header)
{
	const unsigned char *at = (const unsigned char *)bytes;

	header->type = content_u32le(at);
	header->paths_offset = content_u32le(at + 4);
	header->table_offset = content_u32le(at + 8);
	header->count = content_u32le(at + 12);
}

/* one entry of its table, from its CONTENT_RESOURCE_ENTRY_SIZE bytes */
CONTENT_BYTES_INLINE void content_resource_entry_read(const void *bytes, struct content_resource_entry *entry)
{
	const unsigned char *at = (const unsigned char *)bytes;

	entry->path_offset = content_u32le(at);
	entry->size = content_u32le(at + 4);
	entry->offset = content_u32le(at + 8);
}

/* count entries as the file has them, read whole into entries: each made
this processor's numbers, in place */
CONTENT_BYTES_INLINE void content_resource_table_read(struct content_resource_entry *entries, uint32_t count)
{
	uint32_t index;

	for (index = 0; index < count; index++)
	{
		struct content_resource_entry entry;

		content_resource_entry_read(&entries[index], &entry);
		entries[index] = entry;
	}
}

/* whether the header's paths come before its table, and its table (count
entries) is all in a file of file_size bytes */
CONTENT_BYTES_INLINE int content_resource_header_within(const struct content_resource_header *header,
	uint32_t file_size)
{
	return header->paths_offset <= header->table_offset && header->table_offset <= file_size &&
		header->count <= (file_size - header->table_offset) / CONTENT_RESOURCE_ENTRY_SIZE;
}

#endif
