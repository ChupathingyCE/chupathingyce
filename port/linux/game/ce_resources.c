/*
CE_RESOURCES.C

Custom Edition maps' indexed tags (cache_files.c, Custom Edition maps). A
Custom Edition map keeps most of its bitmaps, sounds, fonts and strings in
the resource maps beside it (maps\ce\bitmaps.map, sounds.map, loc.map): such
a tag's instance is marked indexed, and its base address is the index of its
resource. When the map's tags load, each indexed tag's resource is copied
into the map's tag cache, in the space between its tags and its structure
BSPs, and its pointers, which the resource keeps relative to its start, made
Xbox addresses there; the tag's base address is then the copy's.

The bitmaps' pixels and the sounds' samples stay in the resource maps, at the
offsets their tags give: cache_files_windows.c reads them from there for the
tags this file names (ce_resources_file_for_tag).

A resource map: its type (1 bitmaps, 2 sounds, 3 strings and fonts), the
offsets of its paths and of its resources, and how many resources; each
resource: its path's offset, its size and its offset. A bitmap's resource
follows the one of its pixels (<path>__pixels).
*/

#ifdef HALO_64BIT

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "tag_files/tag_groups.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	_ce_resource_bitmaps,
	_ce_resource_sounds,
	_ce_resource_strings,
	NUMBER_OF_CE_RESOURCE_MAPS,

	CE_TAG_INSTANCE_SIZE = 0x20,
	/* a bitmap's flags the Xbox's tags keep (bitmap_utilities.c: power of
	two, compressed, palettized, swizzled, linear, v16u16) */
	CE_BITMAP_XBOX_FORMAT_FLAGS = 0x3f,
	/* Halo PC's: the bitmap's pixels are in bitmaps.map, though its tag is
	in the map */
	CE_BITMAP_EXTERNAL_FLAG = 0x100,
	/* (xbox_texture_cache.c's _bitmap_cached_bit) */
	CE_BITMAP_CACHED_FLAG = 0x80,
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

struct ce_resource
{
	unsigned long path_offset;
	unsigned long size;
	unsigned long offset;
};

struct ce_resource_map
{
	HANDLE file;
	unsigned long count;
	struct ce_resource *resources;
	char *paths;
	unsigned long paths_size;
};

/* ---------- prototypes */

char const *cache_files_map_directory(void);

/* ---------- globals */

static struct ce_resource_map ce_resource_maps[NUMBER_OF_CE_RESOURCE_MAPS];
static char const *const ce_resource_map_names[NUMBER_OF_CE_RESOURCE_MAPS] = { "bitmaps", "sounds", "loc" };
/* the indexed tags of the map loaded: their resource map, by tag index (+1;
0 none) */
static byte *ce_indexed_tags;
static long ce_indexed_tag_count;
/* the space left in the map's tag cache (ce_resources_allocate) */
static unsigned long ce_free_next, ce_free_end;

/* ---------- private code */

static boolean ce_read(
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

static boolean ce_resource_map_open(
	short type)
{
	struct ce_resource_map *map = &ce_resource_maps[type];
	char path[256];
	unsigned long header[4];
	HANDLE file;

	if (map->file)
		return TRUE;
	sprintf(path, "%sce\\%s.map", cache_files_map_directory(), ce_resource_map_names[type]);
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		error(_error_silent, "Custom Edition maps: no %s", path);
		return FALSE;
	}
	if (!ce_read(file, 0, header, sizeof(header)) || header[0] != (unsigned long)(type + 1) || header[3] > 0x10000)
	{
		error(_error_silent, "Custom Edition maps: %s is not a resource map", path);
		CloseHandle(file);
		return FALSE;
	}
	map->resources = system_malloc(header[3] * sizeof(struct ce_resource));
	if (!map->resources || !ce_read(file, header[2], map->resources, header[3] * sizeof(struct ce_resource)))
	{
		CloseHandle(file);
		return FALSE;
	}
	/* (the paths, to find a sound's resource by its tag's name) */
	map->paths_size = header[2] > header[1] ? header[2] - header[1] : 0;
	map->paths = map->paths_size ? system_malloc(map->paths_size + 1) : NULL;
	if (map->paths && ce_read(file, header[1], map->paths, map->paths_size))
		map->paths[map->paths_size] = 0;
	else
	{
		system_free(map->paths);
		map->paths = NULL;
	}
	map->count = header[3];
	map->file = file;
	return TRUE;
}

/* the resource whose path is name, or NONE */
static long ce_resource_by_path(
	struct ce_resource_map const *map,
	char const *name)
{
	unsigned long index;

	if (!map->paths)
		return NONE;
	for (index = 0; index < map->count; index++)
	{
		unsigned long offset = map->resources[index].path_offset;

		if (offset < map->paths_size && !_stricmp(map->paths + offset, name))
			return (long)index;
	}
	return NONE;
}

static short ce_resource_type(
	unsigned long group_tag)
{
	switch (group_tag)
	{
	case 'bitm': return _ce_resource_bitmaps;
	case 'snd!': return _ce_resource_sounds;
	case 'font':
	case 'ustr':
	case 'hmt ': return _ce_resource_strings;
	default: return NONE;
	}
}

/* a pointer (an offset in the resource) made an Xbox address in the copy at
base; 0 stays 0 */
static void ce_relocate(
	byte *copy,
	unsigned long size,
	unsigned long field,
	unsigned long base)
{
	unsigned long *pointer = (unsigned long *)(copy + field);

	if (field + 4 <= size && *pointer)
		*pointer += base;
}

/* a tag block at field: its elements' address; returns its count and puts
the elements' offset in the resource in *elements */
static unsigned long ce_relocate_block(
	byte *copy,
	unsigned long size,
	unsigned long field,
	unsigned long base,
	unsigned long *elements)
{
	unsigned long count = *(unsigned long *)(copy + field);
	unsigned long address = *(unsigned long *)(copy + field + 4);

	*elements = address;
	if (!count)
		return 0;
	ce_relocate(copy, size, field + 4, base);
	return count;
}

/* a tag data at field: its address */
static void ce_relocate_data(
	byte *copy,
	unsigned long size,
	unsigned long field,
	unsigned long base)
{
	if (*(unsigned long *)(copy + field))
		ce_relocate(copy, size, field + 12, base);
}

static void ce_relocate_tag(
	unsigned long group_tag,
	byte *copy,
	unsigned long size,
	unsigned long base,
	unsigned long tag_index)
{
	unsigned long elements, count, index;

	switch (group_tag)
	{
	case 'bitm':
		/* (bitmap_group.h: sequences and their sprites, bitmaps) */
		ce_relocate_data(copy, size, 0x1c, base);
		ce_relocate_data(copy, size, 0x30, base);
		count = ce_relocate_block(copy, size, 0x54, base, &elements);
		for (index = 0; index < count; index++)
		{
			unsigned long ignored;

			ce_relocate_block(copy, size, elements + index * 0x40 + 0x34, base, &ignored);
		}
		count = ce_relocate_block(copy, size, 0x60, base, &elements);
		for (index = 0; index < count; index++)
		{
			byte *bitmap = copy + elements + index * 0x30;

			/* (its tag, which reads its pixels: ce_resources_file_for_tag) */
			*(unsigned long *)(bitmap + 0x20) = tag_index;
			*(unsigned long *)(bitmap + 0x24) = 0xffffffff;
			*(unsigned long *)(bitmap + 0x28) = 0;
			*(unsigned long *)(bitmap + 0x2c) = 0;
		}
		break;
	case 'snd!':
		/* (sound_definitions.h: pitch ranges, their permutations' samples,
		mouth data and subtitles) */
		count = ce_relocate_block(copy, size, 0x98, base, &elements);
		for (index = 0; index < count; index++)
		{
			unsigned long permutations;
			unsigned long permutation_count = ce_relocate_block(copy, size, elements + index * 0x48 + 0x3c, base,
				&permutations);
			unsigned long permutation;

			for (permutation = 0; permutation < permutation_count; permutation++)
			{
				unsigned long at = permutations + permutation * 0x7c;

				ce_relocate_data(copy, size, at + 0x40, base);
				ce_relocate_data(copy, size, at + 0x54, base);
				ce_relocate_data(copy, size, at + 0x68, base);
			}
		}
		break;
	case 'font':
		/* (font_group.h: character tables and their indices, characters, pixels) */
		count = ce_relocate_block(copy, size, 0x30, base, &elements);
		for (index = 0; index < count; index++)
		{
			unsigned long ignored;

			ce_relocate_block(copy, size, elements + index * 0xc, base, &ignored);
		}
		ce_relocate_block(copy, size, 0x7c, base, &elements);
		ce_relocate_data(copy, size, 0x88, base);
		break;
	case 'ustr':
		/* (a block of strings, each a tag data) */
		count = ce_relocate_block(copy, size, 0, base, &elements);
		for (index = 0; index < count; index++)
			ce_relocate_data(copy, size, elements + index * 0x14, base);
		break;
	case 'hmt ':
		/* (its text, its message elements and its messages) */
		ce_relocate_data(copy, size, 0, base);
		ce_relocate_block(copy, size, 0x14, base, &elements);
		ce_relocate_block(copy, size, 0x20, base, &elements);
		break;
	}
}

/* ---------- public code */

/* a Custom Edition map's tags loaded (cache_files.c): its indexed tags'
resources copied in between first_free and end_free (Xbox addresses);
FALSE if one could not be */
boolean ce_resources_tags_loaded(
	void *tag_instances,
	long tag_count,
	unsigned long first_free,
	unsigned long end_free)
{
	unsigned long next = (first_free + 15) & ~15UL;
	long copied = 0;
	long index;

	system_free(ce_indexed_tags);
	ce_indexed_tags = system_malloc(tag_count);
	ce_indexed_tag_count = ce_indexed_tags ? tag_count : 0;
	if (!ce_indexed_tags)
		return FALSE;
	csmemset(ce_indexed_tags, 0, tag_count);
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		short type = ce_resource_type(instance->group_tag);
		struct ce_resource_map *map;
		struct ce_resource *resource;
		byte *copy;

		if (!instance->indexed || type == NONE)
			continue;
		if (!ce_resource_map_open(type))
			return FALSE;
		map = &ce_resource_maps[type];
		/* (a sound's tag in the map is only its header, empty: the whole
		tag is the resource of its name in sounds.map, after its samples) */
		if (type == _ce_resource_sounds)
		{
			long found = ce_resource_by_path(map, xbox_pointer(instance->name));

			if (found == NONE)
			{
				error(_error_silent, "Custom Edition maps: %s is not in sounds.map",
					(char const *)xbox_pointer(instance->name));
				return FALSE;
			}
			instance->base_address = (unsigned long)found;
		}
		/* (strings and fonts are indexed by their resource, bitmaps the
		same: the resource after their pixels) */
		if (instance->base_address >= map->count)
		{
			error(_error_silent, "Custom Edition maps: tag %ld's resource %lu is not in %s.map", index,
				instance->base_address, ce_resource_map_names[type]);
			return FALSE;
		}
		resource = &map->resources[instance->base_address];
		if (next + resource->size > end_free)
		{
			error(_error_silent, "Custom Edition maps: no room for tag %ld's resource", index);
			return FALSE;
		}
		copy = xbox_pointer(next);
		if (!ce_read(map->file, resource->offset, copy, resource->size))
			return FALSE;
		ce_relocate_tag(instance->group_tag, copy, resource->size, next, instance->tag_index);
		instance->base_address = next;
		ce_indexed_tags[index] = (byte)(type + 1);
		next = (next + resource->size + 15) & ~15UL;
		copied++;
	}
	/* every bitmap (in the map or copied in): Halo PC's flags past the
	Xbox's format flags (its "external", and what Halo PC's renderer keeps
	there) cleared, and the bitmap made the texture cache's, as the Xbox's
	tools leave every bitmap in a map (texture_cache_bitmap_new, but for
	the pixels' offset, already the file's): its tag named, and no cache
	block, texture or pixels yet */
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		byte *bitmap_group;
		unsigned long count, elements, bitmap;

		if (instance->group_tag != 'bitm')
			continue;
		bitmap_group = xbox_pointer(instance->base_address);
		count = *(unsigned long *)(bitmap_group + 0x60);
		elements = *(unsigned long *)(bitmap_group + 0x64);
		for (bitmap = 0; bitmap < count; bitmap++)
		{
			byte *data = (byte *)xbox_pointer(elements) + bitmap * 0x30;

			/* (a tag's bitmaps are all external or none are: its pixels are
			read from bitmaps.map, as an indexed tag's are) */
			if (*(unsigned short *)(data + 0x0e) & CE_BITMAP_EXTERNAL_FLAG)
				ce_indexed_tags[index] = (byte)(_ce_resource_bitmaps + 1);
			*(unsigned short *)(data + 0x0e) = (*(unsigned short *)(data + 0x0e) & CE_BITMAP_XBOX_FORMAT_FLAGS) |
				CE_BITMAP_CACHED_FLAG;
			*(unsigned long *)(data + 0x20) = instance->tag_index;
			*(long *)(data + 0x24) = -1;
			*(unsigned long *)(data + 0x28) = 0;
			*(unsigned long *)(data + 0x2c) = 0;
		}
	}
	error(_error_silent, "Custom Edition maps: %ld indexed tags copied in (%lu bytes free after them)", copied,
		end_free - next);
	ce_free_next = next;
	ce_free_end = end_free;
	return TRUE;
}

/* the resource map a tag's pixels or samples are read from (cache_files_windows.c),
or NULL: the map itself */
HANDLE ce_resources_file_for_tag(
	long tag_index)
{
	long index = tag_index & 0xffff;

	if (tag_index == NONE || index >= ce_indexed_tag_count || !ce_indexed_tags[index])
		return NULL;
	return ce_resource_maps[ce_indexed_tags[index] - 1].file;
}

/* size bytes (16-byte aligned) of the map's tag cache, after its tags and
resources (ce_models.c): their Xbox address, or 0 if there is no room */
unsigned long ce_resources_allocate(
	unsigned long size)
{
	unsigned long address = ce_free_next;

	if (!address || address + size > ce_free_end)
		return 0;
	ce_free_next = (address + size + 15) & ~15UL;
	return address;
}

/* the map unloaded (cache_files.c) */
void ce_resources_tags_unloaded(
	void)
{
	ce_indexed_tag_count = 0;
}

#endif
