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

The resource maps, like the map, may be anything a player put there: each
resource is checked to lie in its file, and each block and data in it to lie
in it, as it is relocated; a map whose resources do not is refused before it
is opened (ce_map_checks.c, which copies them in as loading does, into an
image of the tag cache, and checks every bitmap and sound: ce_resources_check).

HaloMD's maps (Halo PC retail's) are read with Custom Edition's resource
maps too: their stock bitmaps' pixels and sounds' samples are found in them
by their tags' paths (below).
*/

#ifdef HALO_CUSTOM_EDITION

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "tag_files/tag_groups.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "ce_map_checks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	_ce_resource_bitmaps,
	_ce_resource_sounds,
	_ce_resource_strings,
	NUMBER_OF_CE_RESOURCE_MAPS,

	CE_TAG_INSTANCE_SIZE = 0x20,
	/* (cache_files.c's: where a Custom Edition map's tags are) */
	CE_TAG_CACHE_BASE = 0x40440000,
	CE_TAG_CACHE_SIZE = 0x01700000,
	/* a bitmap's flags the Xbox's tags keep (bitmap_utilities.c: power of
	two, compressed, palettized, swizzled, linear, v16u16) */
	CE_BITMAP_XBOX_FORMAT_FLAGS = 0x3f,
	CE_BITMAP_LINEAR_FLAG = 0x10,
	/* Halo PC's: the bitmap's pixels are in bitmaps.map, though its tag is
	in the map */
	CE_BITMAP_EXTERNAL_FLAG = 0x100,
	/* (xbox_texture_cache.c's _bitmap_cached_bit) */
	CE_BITMAP_CACHED_FLAG = 0x80,

	/* ce_indexed_tags' mark of a sound whose samples are decoded (in
	ce_sounds.pcm) */
	CE_DECODED_SOUND = 0xff,
	/* (sound_manager.c's sound_compression) */
	CE_SOUND_COMPRESSION_NONE = 0,
	CE_SOUND_COMPRESSION_OGG = 3,
	/* (sound_definitions.h's sound_definition) */
	CE_SOUND_HEADER_SIZE = 0xa4,
	CE_SOUND_SAMPLE_RATE_OFFSET = 0x06,
	CE_SOUND_ENCODING_OFFSET = 0x6c,
	CE_SOUND_COMPRESSION_OFFSET = 0x6e,
	CE_SOUND_LONGEST_PERMUTATION_OFFSET = 0x84,
	CE_SOUND_PITCH_RANGES_OFFSET = 0x98,
	/* (sound_definitions.h's sound_pitch_range and sound_permutation) */
	CE_SOUND_PITCH_RANGE_SIZE = 0x48,
	CE_PITCH_RANGE_ACTUAL_PERMUTATION_COUNT_OFFSET = 0x2c,
	CE_PITCH_RANGE_PERMUTATIONS_OFFSET = 0x3c,
	CE_SOUND_PERMUTATION_SIZE = 0x7c,
	CE_PERMUTATION_COMPRESSION_OFFSET = 0x28,
	CE_PERMUTATION_NEXT_OFFSET = 0x2a,
	CE_PERMUTATION_SAMPLES_OFFSET = 0x40,
	CE_PERMUTATION_MOUTH_DATA_OFFSET = 0x54,
	CE_PERMUTATION_SUBTITLE_DATA_OFFSET = 0x68,
	/* (the played permutations are a 32-bit mask: sound_definitions.c) */
	CE_MAXIMUM_PLAYED_PERMUTATIONS = 32,

	/* the bitmap group's header and its bitmaps (bitmap_group.h) */
	CE_BITMAP_GROUP_SIZE = 0x6c,
	CE_BITMAP_GROUP_BITMAPS_OFFSET = 0x60,
	CE_BITMAP_DATA_SIZE = 0x30,

	/* (a tag block's or tag data's count or size: larger ones would not
	fit in a resource or a tag cache in any case) */
	CE_MAXIMUM_ELEMENTS = 0x10000,
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
	unsigned long file_size;
	unsigned long count;
	struct ce_resource *resources;
	char *paths;
	unsigned long paths_size;
};

typedef char verify_ce_bitmap_data_size[sizeof(struct bitmap_data) == CE_BITMAP_DATA_SIZE ? 1 : -1];

/* ---------- prototypes */

char const *cache_files_map_directory(void);
HANDLE cache_files_ce_map_file(void);
int ce_vorbis_decode(const unsigned char *data, int size, int *channels, int *sample_rate, short **samples);
void ce_vorbis_free(short *samples);
static void ce_sounds_decode(void *tag_instances, long tag_count);

/* ---------- globals */

static struct ce_resource_map ce_resource_maps[NUMBER_OF_CE_RESOURCE_MAPS];
static char const *const ce_resource_map_names[NUMBER_OF_CE_RESOURCE_MAPS] = { "bitmaps", "sounds", "loc" };
/* the indexed tags of the map loaded: their resource map, by tag index (+1;
0 none) */
static byte *ce_indexed_tags;
static long ce_indexed_tag_count;
/* the map's Ogg Vorbis sounds' samples, decoded to 16-bit PCM (maps\ce\ce_sounds.pcm) */
static HANDLE ce_decoded_sounds_file;
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
	unsigned long file_size, file_size_high = 0;
	HANDLE file;

	if (map->file)
		return TRUE;
	sprintf(path, "%sce\\%s.map", cache_files_map_directory(), ce_resource_map_names[type]);
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return ce_refuse("there is no %s", path);
	file_size = GetFileSize(file, &file_size_high);
	/* (its type, its paths before its resources' table, the table in the
	file) */
	if (file_size == INVALID_FILE_SIZE || file_size_high || file_size > 0x7fffffff ||
		!ce_read(file, 0, header, sizeof(header)) || header[0] != (unsigned long)(type + 1) ||
		header[3] > CE_MAXIMUM_ELEMENTS || header[1] > header[2] ||
		!ce_range_within(header[2], header[3] * sizeof(struct ce_resource), file_size))
	{
		CloseHandle(file);
		return ce_refuse("%s is not a resource map", path);
	}
	map->resources = system_malloc(header[3] * sizeof(struct ce_resource) + 1);
	if (!map->resources || !ce_read(file, header[2], map->resources, header[3] * sizeof(struct ce_resource)))
	{
		system_free(map->resources);
		map->resources = NULL;
		CloseHandle(file);
		return ce_refuse("%s's resources could not be read", path);
	}
	/* (the paths, to find a sound's resource by its tag's name) */
	map->paths_size = header[2] - header[1];
	map->paths = map->paths_size ? system_malloc(map->paths_size + 1) : NULL;
	if (map->paths && ce_read(file, header[1], map->paths, map->paths_size))
		map->paths[map->paths_size] = 0;
	else
	{
		system_free(map->paths);
		map->paths = NULL;
	}
	map->count = header[3];
	map->file_size = file_size;
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

		/* (the paths end with the terminator ce_resource_map_open adds) */
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

/* the smallest a resource of a tag of its kind is: its fields this file reads */
static unsigned long ce_resource_minimum_size(
	unsigned long group_tag)
{
	switch (group_tag)
	{
	case 'bitm': return CE_BITMAP_GROUP_SIZE;
	case 'snd!': return CE_SOUND_HEADER_SIZE;
	case 'font': return 0x9c;
	case 'ustr': return 0xc;
	case 'hmt ': return 0x2c;
	default: return 0;
	}
}

static unsigned long ce_read_long(
	byte const *at)
{
	unsigned long value;

	memcpy(&value, at, sizeof(value));
	return value;
}

static void ce_write_long(
	byte *at,
	unsigned long value)
{
	memcpy(at, &value, sizeof(value));
}

/* a tag block at field of a resource (copy, size bytes, to be at base): its
elements, each element_size bytes, all in the resource, made an Xbox address
there; their count and offset in the resource; FALSE if they are not in it */
static boolean ce_relocate_block(
	byte *copy,
	unsigned long size,
	unsigned long field,
	unsigned long element_size,
	unsigned long base,
	unsigned long *count,
	unsigned long *elements)
{
	unsigned long block_count, address;

	*count = 0;
	*elements = 0;
	if (!ce_range_within(field, 12, size))
		return FALSE;
	block_count = ce_read_long(copy + field);
	address = ce_read_long(copy + field + 4);
	if (!block_count)
		return TRUE;
	if (block_count > CE_MAXIMUM_ELEMENTS || !ce_range_within(address, block_count * element_size, size))
		return FALSE;
	ce_write_long(copy + field + 4, base + address);
	*count = block_count;
	*elements = address;
	return TRUE;
}

/* a tag data at field: its bytes, all in the resource, made an Xbox address */
static boolean ce_relocate_data(
	byte *copy,
	unsigned long size,
	unsigned long field,
	unsigned long base)
{
	unsigned long data_size, address;

	if (!ce_range_within(field, 0x14, size))
		return FALSE;
	data_size = ce_read_long(copy + field);
	address = ce_read_long(copy + field + 12);
	if (!data_size)
		return TRUE;
	if (data_size > 0x7fffffff || !ce_range_within(address, data_size, size))
		return FALSE;
	ce_write_long(copy + field + 12, base + address);
	return TRUE;
}

/* a tag data the game never reads (an editor's: a bitmap's color plate and
pixels, which Halo PC's resources keep the size of but not the bytes):
relocated if it is in the resource, else made empty */
static void ce_relocate_editor_data(
	byte *copy,
	unsigned long size,
	unsigned long field,
	unsigned long base)
{
	if (ce_range_within(field, 0x14, size) && !ce_relocate_data(copy, size, field, base))
	{
		ce_write_long(copy + field, 0);
		ce_write_long(copy + field + 12, 0);
	}
}

/* (a + b, FALSE if that passes limit) */
static boolean ce_advance(
	unsigned long *cursor,
	unsigned long count,
	unsigned long element_size,
	unsigned long limit)
{
	if (count > CE_MAXIMUM_ELEMENTS || !ce_range_within(*cursor, count * element_size, limit))
		return FALSE;
	*cursor += count * element_size;
	return TRUE;
}

/* a sound's resource (sound_definitions.h) keeps the pointers of the
machine that built it, not offsets: its parts follow each other, the sound
(0xa4 bytes), its pitch ranges, each pitch range's permutations, then each
permutation's mouth data and subtitles. Its samples are in sounds.map, where
their offsets say. FALSE if its parts pass its end */
static boolean ce_relocate_sound(
	byte *copy,
	unsigned long size,
	unsigned long base,
	unsigned long tag_index)
{
	unsigned long cursor = CE_SOUND_HEADER_SIZE;
	unsigned long range_count = ce_read_long(copy + CE_SOUND_PITCH_RANGES_OFFSET);
	unsigned long ranges = cursor;
	unsigned long index;

	if (!ce_advance(&cursor, range_count, CE_SOUND_PITCH_RANGE_SIZE, size))
		return FALSE;
	ce_write_long(copy + CE_SOUND_PITCH_RANGES_OFFSET + 4, range_count ? base + ranges : 0);
	for (index = 0; index < range_count; index++)
	{
		byte *range = copy + ranges + index * CE_SOUND_PITCH_RANGE_SIZE;
		unsigned long permutation_count = ce_read_long(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET);

		ce_write_long(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET + 4, permutation_count ? base + cursor : 0);
		ce_write_long(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET + 8, 0);
		if (!ce_advance(&cursor, permutation_count, CE_SOUND_PERMUTATION_SIZE, size))
			return FALSE;
	}
	for (index = 0; index < range_count; index++)
	{
		byte *range = copy + ranges + index * CE_SOUND_PITCH_RANGE_SIZE;
		unsigned long permutation_count = ce_read_long(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET);
		unsigned long permutations = ce_read_long(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET + 4) - base;
		unsigned long permutation;

		for (permutation = 0; permutation < permutation_count; permutation++)
		{
			byte *at = copy + permutations + permutation * CE_SOUND_PERMUTATION_SIZE;
			unsigned long data;

			/* (the sound cache's, as the Xbox's tools leave them: no block
			or address yet, and the tag the samples are read for, which
			routes the read: ce_resources_file_for_tag; Halo PC's resource
			has the tag index of the map it was built in) */
			ce_write_long(at + 0x2c, 0xffffffff);
			ce_write_long(at + 0x30, 0);
			ce_write_long(at + 0x34, tag_index);
			ce_write_long(at + 0x3c, tag_index);
			/* (samples: in sounds.map; mouth data and subtitles: here) */
			ce_write_long(at + CE_PERMUTATION_SAMPLES_OFFSET + 12, 0);
			for (data = CE_PERMUTATION_MOUTH_DATA_OFFSET; data <= CE_PERMUTATION_SUBTITLE_DATA_OFFSET; data += 0x14)
			{
				unsigned long data_size = ce_read_long(at + data);

				ce_write_long(at + data + 12, data_size ? base + cursor : 0);
				if (!ce_advance(&cursor, 1, data_size, size) || data_size > 0x7fffffff)
					return FALSE;
			}
		}
	}
	return TRUE;
}

/* a resource's pointers, relative to its start, made Xbox addresses at
base: FALSE if one points out of it */
static boolean ce_relocate_tag(
	unsigned long group_tag,
	byte *copy,
	unsigned long size,
	unsigned long base,
	unsigned long tag_index)
{
	unsigned long elements, count, index, ignored, ignored_count;

	switch (group_tag)
	{
	case 'bitm':
		/* (bitmap_group.h: sequences and their sprites, bitmaps) */
		ce_relocate_editor_data(copy, size, 0x1c, base);
		ce_relocate_editor_data(copy, size, 0x30, base);
		if (!ce_relocate_block(copy, size, 0x54, 0x40, base, &count, &elements))
		{
			return FALSE;
		}
		for (index = 0; index < count; index++)
		{
			if (!ce_relocate_block(copy, size, elements + index * 0x40 + 0x34, 0x20, base, &ignored_count, &ignored))
				return FALSE;
		}
		if (!ce_relocate_block(copy, size, CE_BITMAP_GROUP_BITMAPS_OFFSET, CE_BITMAP_DATA_SIZE, base, &count,
			&elements))
		{
			return FALSE;
		}
		for (index = 0; index < count; index++)
		{
			byte *bitmap = copy + elements + index * CE_BITMAP_DATA_SIZE;

			/* (its tag, which reads its pixels: ce_resources_file_for_tag) */
			ce_write_long(bitmap + 0x20, tag_index);
			ce_write_long(bitmap + 0x24, 0xffffffff);
			ce_write_long(bitmap + 0x28, 0);
			ce_write_long(bitmap + 0x2c, 0);
		}
		return TRUE;
	case 'snd!':
		return ce_relocate_sound(copy, size, base, tag_index);
	case 'font':
		/* (font_group.h: character tables and their indices, characters, pixels) */
		if (!ce_relocate_block(copy, size, 0x30, 0xc, base, &count, &elements))
			return FALSE;
		for (index = 0; index < count; index++)
		{
			if (!ce_relocate_block(copy, size, elements + index * 0xc, 2, base, &ignored_count, &ignored))
				return FALSE;
		}
		return ce_relocate_block(copy, size, 0x7c, 0x14, base, &ignored_count, &ignored) &&
			ce_relocate_data(copy, size, 0x88, base);
	case 'ustr':
		/* (a block of strings, each a tag data) */
		if (!ce_relocate_block(copy, size, 0, 0x14, base, &count, &elements))
			return FALSE;
		for (index = 0; index < count; index++)
		{
			if (!ce_relocate_data(copy, size, elements + index * 0x14, base))
				return FALSE;
		}
		return TRUE;
	case 'hmt ':
		/* (its text, its message elements and its messages) */
		return ce_relocate_data(copy, size, 0, base) &&
			ce_relocate_block(copy, size, 0x14, 2, base, &ignored_count, &ignored) &&
			ce_relocate_block(copy, size, 0x20, 0x40, base, &ignored_count, &ignored);
	}
	return TRUE;
}

/* ---------- HaloMD's maps (Halo PC retail's, version 7, played as <name>@md)

A retail map has no indexed tags: its bitmaps and sounds are all in it, but
the pixels and samples of the stock ones are at offsets in Halo PC's own
bitmaps.map and sounds.map, which are laid out unlike Custom Edition's. The
same stock tags are in Custom Edition's resource maps, by the same paths
(HaloMD's maps add '+'s to copies of them): each external bitmap or sound
takes the offsets of its namesake there, when its pixels or samples are the
same size. A bitmap with no namesake borrows the pixels of one of the same
type (and format, if there is one) there; a sound with none is made
silent (no pitch ranges: sound_manager.c does not play it). */

/* every bitmap in Custom Edition's bitmaps.map (CE_BITMAP_DATA_SIZE each),
read once, for stand-ins */
static byte *ce_retail_stand_ins;
static long ce_retail_stand_in_count;
static boolean ce_retail_stand_ins_read;

/* the resource of a tag's path, or of the path without its trailing '+'s */
static long ce_retail_resource(
	struct ce_resource_map const *map,
	char const *name)
{
	char stripped[256];
	size_t length = strlen(name), stripped_length = length;
	long found = ce_resource_by_path(map, name);

	if (found != NONE)
		return found;
	while (stripped_length && name[stripped_length - 1] == '+')
		stripped_length--;
	if (stripped_length == length || !stripped_length || stripped_length >= sizeof(stripped))
		return NONE;
	memcpy(stripped, name, stripped_length);
	stripped[stripped_length] = 0;
	return ce_resource_by_path(map, stripped);
}

/* a resource (no larger than maximum_size) read into a buffer to free, or NULL */
static byte *ce_retail_read_resource(
	struct ce_resource_map const *map,
	long resource_index,
	unsigned long minimum_size,
	unsigned long maximum_size,
	unsigned long *size)
{
	struct ce_resource const *resource = &map->resources[resource_index];
	byte *copy;

	if (resource->size < minimum_size || resource->size > maximum_size ||
		!ce_range_within(resource->offset, resource->size, map->file_size))
	{
		return NULL;
	}
	copy = malloc(resource->size);
	if (copy && !ce_read(map->file, resource->offset, copy, resource->size))
	{
		free(copy);
		copy = NULL;
	}
	*size = resource->size;
	return copy;
}

/* a bitmap resource's bitmaps, copied into a buffer to free, or NULL */
static byte *ce_retail_resource_bitmaps(
	struct ce_resource_map const *map,
	long resource_index,
	long *count)
{
	unsigned long size = 0;
	byte *copy = ce_retail_read_resource(map, resource_index, CE_BITMAP_GROUP_SIZE, 0x100000, &size);
	byte *bitmaps = NULL;

	*count = 0;
	if (copy)
	{
		unsigned long bitmap_count = ce_read_long(copy + CE_BITMAP_GROUP_BITMAPS_OFFSET);
		unsigned long at = ce_read_long(copy + CE_BITMAP_GROUP_BITMAPS_OFFSET + 4);

		if (bitmap_count && bitmap_count <= CE_MAXIMUM_ELEMENTS &&
			ce_range_within(at, bitmap_count * CE_BITMAP_DATA_SIZE, size))
		{
			bitmaps = malloc(bitmap_count * CE_BITMAP_DATA_SIZE);
			if (bitmaps)
			{
				memcpy(bitmaps, copy + at, bitmap_count * CE_BITMAP_DATA_SIZE);
				*count = (long)bitmap_count;
			}
		}
		free(copy);
	}
	return bitmaps;
}

static void ce_retail_read_stand_ins(
	struct ce_resource_map const *map)
{
	unsigned long index;

	ce_retail_stand_ins_read = TRUE;
	for (index = 0; index < map->count; index++)
	{
		unsigned long path_offset = map->resources[index].path_offset;
		size_t length;
		long count;
		byte *bitmaps, *grown;

		if (!map->paths || path_offset >= map->paths_size)
			continue;
		length = strlen(map->paths + path_offset);
		if (length >= 8 && !strcmp(map->paths + path_offset + length - 8, "__pixels"))
			continue;
		bitmaps = ce_retail_resource_bitmaps(map, (long)index, &count);
		if (!bitmaps)
			continue;
		grown = realloc(ce_retail_stand_ins, (ce_retail_stand_in_count + count) * CE_BITMAP_DATA_SIZE);
		if (grown)
		{
			memcpy(grown + ce_retail_stand_in_count * CE_BITMAP_DATA_SIZE, bitmaps, count * CE_BITMAP_DATA_SIZE);
			ce_retail_stand_ins = grown;
			ce_retail_stand_in_count += count;
		}
		free(bitmaps);
	}
}

/* a bitmap given the pixels of one in Custom Edition's bitmaps.map: of its
shape, else of its type and format, else of its type; FALSE if none is */
static boolean ce_retail_bitmap_stand_in(
	struct ce_resource_map const *map,
	byte *data)
{
	long pass, index;

	if (!ce_retail_stand_ins_read)
		ce_retail_read_stand_ins(map);
	for (pass = 0; pass < 3; pass++)
	{
		for (index = 0; index < ce_retail_stand_in_count; index++)
		{
			byte const *stand_in = ce_retail_stand_ins + index * CE_BITMAP_DATA_SIZE;
			boolean fits;

			switch (pass)
			{
			case 0: /* (width, height, depth, type, format; mipmaps; pixels' size) */
				fits = !memcmp(data + 0x04, stand_in + 0x04, 10) && !memcmp(data + 0x14, stand_in + 0x14, 2) &&
					!memcmp(data + 0x1c, stand_in + 0x1c, 4);
				break;
			case 1:
				fits = !memcmp(data + 0x0a, stand_in + 0x0a, 4);
				break;
			default:
				fits = !memcmp(data + 0x0a, stand_in + 0x0a, 2);
				break;
			}
			if (fits)
			{
				unsigned short flags, stand_in_flags;

				memcpy(&flags, data + 0x0e, 2);
				memcpy(&stand_in_flags, stand_in + 0x0e, 2);
				flags = (unsigned short)((flags & ~CE_BITMAP_XBOX_FORMAT_FLAGS) |
					(stand_in_flags & CE_BITMAP_XBOX_FORMAT_FLAGS) | CE_BITMAP_EXTERNAL_FLAG);
				memcpy(data + 0x04, stand_in + 0x04, 10);
				memcpy(data + 0x0e, &flags, 2);
				memcpy(data + 0x14, stand_in + 0x14, 2);
				memcpy(data + 0x18, stand_in + 0x18, 8);
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* a retail map's bitmap tag: its external bitmaps' pixels found in Custom
Edition's bitmaps.map (or stood in for), and the tag's pixels read from
there (*type) */
static boolean ce_retail_bitmaps(
	struct ce_image *image,
	struct ce_tag_instance const *instance,
	byte *type,
	long *stood_in)
{
	byte *group = ce_image_pointer(image, instance->base_address, CE_BITMAP_GROUP_SIZE);
	char const *name = ce_image_tag_name(image, instance);
	struct ce_resource_map *map = &ce_resource_maps[_ce_resource_bitmaps];
	byte *bitmaps, *theirs;
	long count, their_count = 0, index, found;
	boolean external = FALSE;

	/* (ce_bitmaps_check refuses a bitmap whose blocks are not in the tags) */
	if (!group || !ce_image_block(image, group + CE_BITMAP_GROUP_BITMAPS_OFFSET, CE_BITMAP_DATA_SIZE,
		CE_MAXIMUM_ELEMENTS, name, &count, &bitmaps))
	{
		return TRUE;
	}
	for (index = 0; index < count; index++)
		external |= (*(unsigned short *)(bitmaps + index * CE_BITMAP_DATA_SIZE + 0x0e) & CE_BITMAP_EXTERNAL_FLAG) != 0;
	if (!external)
		return TRUE;
	if (!ce_resource_map_open(_ce_resource_bitmaps))
		return FALSE;
	found = ce_retail_resource(map, name);
	theirs = found != NONE ? ce_retail_resource_bitmaps(map, found, &their_count) : NULL;
	for (index = 0; index < count; index++)
	{
		byte *data = bitmaps + index * CE_BITMAP_DATA_SIZE;
		byte const *their = theirs && index < their_count ? theirs + index * CE_BITMAP_DATA_SIZE : NULL;

		if (!(*(unsigned short *)(data + 0x0e) & CE_BITMAP_EXTERNAL_FLAG))
			continue;
		if (their && !memcmp(data + 0x04, their + 0x04, 10) && !memcmp(data + 0x14, their + 0x14, 2) &&
			!memcmp(data + 0x1c, their + 0x1c, 4))
		{
			memcpy(data + 0x18, their + 0x18, 4);
		}
		else if (ce_retail_bitmap_stand_in(map, data))
			(*stood_in)++;
		else
		{
			if (theirs)
				free(theirs);
			return ce_refuse("bitmap %ld of %s is in Halo PC's bitmaps.map, not in Custom Edition's", index, name);
		}
	}
	if (theirs)
		free(theirs);
	*type = (byte)(_ce_resource_bitmaps + 1);
	return TRUE;
}

/* a retail map's sound tag: its external permutations' samples found in
Custom Edition's sounds.map, and the tag's samples read from there (*type);
or, if they are not all there, of the same sizes, the sound made silent */
static boolean ce_retail_sound(
	struct ce_image *image,
	struct ce_tag_instance const *instance,
	byte *type,
	long *silenced)
{
	byte *sound = ce_image_pointer(image, instance->base_address, CE_SOUND_HEADER_SIZE);
	char const *name = ce_image_tag_name(image, instance);
	struct ce_resource_map *map = &ce_resource_maps[_ce_resource_sounds];
	byte *ranges, *copy = NULL;
	long range_count, range_index, found;
	unsigned long size = 0, cursor, their_range_count = 0;
	boolean external = FALSE, matches;

	if (!sound || !ce_image_block(image, sound + CE_SOUND_PITCH_RANGES_OFFSET, CE_SOUND_PITCH_RANGE_SIZE,
		CE_MAXIMUM_ELEMENTS, name, &range_count, &ranges))
	{
		return TRUE;
	}
	for (range_index = 0; range_index < range_count; range_index++)
	{
		byte *permutations;
		long permutation_count, permutation_index;

		if (!ce_image_block(image, ranges + range_index * CE_SOUND_PITCH_RANGE_SIZE + CE_PITCH_RANGE_PERMUTATIONS_OFFSET,
			CE_SOUND_PERMUTATION_SIZE, CE_MAXIMUM_ELEMENTS, name, &permutation_count, &permutations))
		{
			return TRUE;
		}
		for (permutation_index = 0; permutation_index < permutation_count; permutation_index++)
			external |= (ce_read_long(permutations + permutation_index * CE_SOUND_PERMUTATION_SIZE +
				CE_PERMUTATION_SAMPLES_OFFSET + 4) & 1) != 0;
	}
	if (!external)
		return TRUE;
	if (!ce_resource_map_open(_ce_resource_sounds))
		return FALSE;
	found = ce_retail_resource(map, name);
	if (found != NONE)
		copy = ce_retail_read_resource(map, found, CE_SOUND_HEADER_SIZE, 0x1000000, &size);
	/* (the same encoding and compression, pitch ranges, permutations and
	samples' sizes; its resource's parts follow each other: ce_relocate_sound) */
	matches = copy && !memcmp(copy + CE_SOUND_ENCODING_OFFSET, sound + CE_SOUND_ENCODING_OFFSET, 4);
	if (matches)
	{
		their_range_count = ce_read_long(copy + CE_SOUND_PITCH_RANGES_OFFSET);
		cursor = CE_SOUND_HEADER_SIZE;
		matches = their_range_count == (unsigned long)range_count &&
			ce_advance(&cursor, their_range_count, CE_SOUND_PITCH_RANGE_SIZE, size);
	}
	for (range_index = 0; matches && range_index < range_count; range_index++)
	{
		byte *permutations;
		long permutation_count, permutation_index;
		unsigned long their_permutations = cursor;

		ce_image_block(image, ranges + range_index * CE_SOUND_PITCH_RANGE_SIZE + CE_PITCH_RANGE_PERMUTATIONS_OFFSET,
			CE_SOUND_PERMUTATION_SIZE, CE_MAXIMUM_ELEMENTS, name, &permutation_count, &permutations);
		matches = ce_read_long(copy + CE_SOUND_HEADER_SIZE + range_index * CE_SOUND_PITCH_RANGE_SIZE +
			CE_PITCH_RANGE_PERMUTATIONS_OFFSET) == (unsigned long)permutation_count &&
			ce_advance(&cursor, (unsigned long)permutation_count, CE_SOUND_PERMUTATION_SIZE, size);
		for (permutation_index = 0; matches && permutation_index < permutation_count; permutation_index++)
		{
			byte const *their = copy + their_permutations + permutation_index * CE_SOUND_PERMUTATION_SIZE;
			byte const *mine = permutations + permutation_index * CE_SOUND_PERMUTATION_SIZE;

			matches = ce_read_long(mine + CE_PERMUTATION_SAMPLES_OFFSET) ==
				ce_read_long(their + CE_PERMUTATION_SAMPLES_OFFSET);
		}
	}
	/* (then the offsets taken: a second pass, once all of them match) */
	if (matches)
	{
		cursor = CE_SOUND_HEADER_SIZE + their_range_count * CE_SOUND_PITCH_RANGE_SIZE;
		for (range_index = 0; range_index < range_count; range_index++)
		{
			byte *permutations;
			long permutation_count, permutation_index;

			ce_image_block(image, ranges + range_index * CE_SOUND_PITCH_RANGE_SIZE +
				CE_PITCH_RANGE_PERMUTATIONS_OFFSET, CE_SOUND_PERMUTATION_SIZE, CE_MAXIMUM_ELEMENTS, name,
				&permutation_count, &permutations);
			for (permutation_index = 0; permutation_index < permutation_count; permutation_index++)
			{
				memcpy(permutations + permutation_index * CE_SOUND_PERMUTATION_SIZE + CE_PERMUTATION_SAMPLES_OFFSET + 8,
					copy + cursor + permutation_index * CE_SOUND_PERMUTATION_SIZE + CE_PERMUTATION_SAMPLES_OFFSET + 8, 4);
			}
			cursor += (unsigned long)permutation_count * CE_SOUND_PERMUTATION_SIZE;
		}
		*type = (byte)(_ce_resource_sounds + 1);
	}
	else
	{
		ce_write_long(sound + CE_SOUND_PITCH_RANGES_OFFSET, 0);
		ce_write_long(sound + CE_SOUND_PITCH_RANGES_OFFSET + 4, 0);
		(*silenced)++;
	}
	if (copy)
		free(copy);
	return TRUE;
}

/* a sound's header in sounds.map is the one of the map sounds.map was
built with: its promotion sound is a tag index there, not in this map (in
timberland's, a shell casing sound's promotion sound is a Scorpion shader or
bitmap), so the copy takes the map's own header, but for what describes the
samples in sounds.map (rate, encoding, compression, longest permutation)
and its pitch ranges, as relocated (Halo PC's sound cache, the same) */
static void ce_sound_header(
	byte *copy,
	unsigned long size,
	byte const *map_header)
{
	byte entry[CE_SOUND_HEADER_SIZE];

	if (size < CE_SOUND_HEADER_SIZE)
		return;
	memcpy(entry, copy, CE_SOUND_HEADER_SIZE);
	memcpy(copy, map_header, CE_SOUND_HEADER_SIZE);
	memcpy(copy + CE_SOUND_SAMPLE_RATE_OFFSET, entry + CE_SOUND_SAMPLE_RATE_OFFSET, 2);
	memcpy(copy + CE_SOUND_ENCODING_OFFSET, entry + CE_SOUND_ENCODING_OFFSET, 4); /* (and compression) */
	memcpy(copy + CE_SOUND_LONGEST_PERMUTATION_OFFSET, entry + CE_SOUND_LONGEST_PERMUTATION_OFFSET, 4);
	memcpy(copy + CE_SOUND_PITCH_RANGES_OFFSET, entry + CE_SOUND_PITCH_RANGES_OFFSET, 12);
}

/* the indexed tags' resources copied into the image of the tag cache after
its tags (its size, which grows to take them in), below end_free, and
relocated; each tag's resource map put in types (+1; 0 none), and the end
of the copies in *next_free. FALSE (refused, saying why) if one cannot be */
static boolean ce_resources_place(
	struct ce_image *image,
	void *tag_instances,
	long tag_count,
	unsigned long end_free,
	byte *types,
	unsigned long *next_free,
	long *copied)
{
	unsigned long tags_end = image->base + image->size;
	unsigned long next = (tags_end + 15) & ~15UL;
	long index;
	long stood_in = 0, silenced = 0;

	*copied = 0;
	csmemset(types, 0, tag_count);
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		short type = ce_resource_type(instance->group_tag);
		char const *name = ce_image_tag_name(image, instance);
		struct ce_resource_map *map;
		struct ce_resource *resource;
		byte *copy;
		byte const *sound_header = NULL;

		if (type == NONE)
			continue;
		if (!instance->indexed)
		{
			/* (a retail map's stock bitmaps and sounds, from Custom
			Edition's resource maps) */
			if (ce_map_cache_version == CE_CACHE_VERSION_RETAIL)
			{
				boolean found = TRUE;

				if (instance->group_tag == 'bitm')
					found = ce_retail_bitmaps(image, instance, &types[index], &stood_in);
				else if (instance->group_tag == 'snd!')
					found = ce_retail_sound(image, instance, &types[index], &silenced);
				if (!found)
					return FALSE;
			}
			continue;
		}
		if (!ce_resource_map_open(type))
			return FALSE;
		map = &ce_resource_maps[type];
		/* (a sound's tag in the map is only its header, without its pitch
		ranges: those are the resource of its name in sounds.map, after its
		samples, which begins with a header of its own: ce_sound_header) */
		if (type == _ce_resource_sounds)
		{
			long found = ce_resource_by_path(map, name);

			/* (its header, in the map's tags) */
			if (instance->base_address < tags_end)
				sound_header = ce_image_pointer(image, instance->base_address, CE_SOUND_HEADER_SIZE);
			if (found == NONE)
				return ce_refuse("%s is not in sounds.map", name);
			instance->base_address = (unsigned long)found;
		}
		/* (strings and fonts are indexed by their resource, bitmaps the
		same: the resource after their pixels) */
		if (instance->base_address >= map->count)
		{
			return ce_refuse("%s's resource %lu is not in %s.map (%lu)", name, instance->base_address,
				ce_resource_map_names[type], map->count);
		}
		resource = &map->resources[instance->base_address];
		if (resource->size < ce_resource_minimum_size(instance->group_tag) ||
			!ce_range_within(resource->offset, resource->size, map->file_size))
		{
			return ce_refuse("%s's resource (%lu bytes at %lu) is not in %s.map", name, resource->size,
				resource->offset, ce_resource_map_names[type]);
		}
		if (next > end_free || resource->size > end_free - next)
			return ce_refuse("there is no room in the tag cache for %s's resource", name);
		copy = image->data + (next - image->base);
		if (!ce_read(map->file, resource->offset, copy, resource->size))
			return ce_refuse("%s's resource could not be read", name);
		if (!ce_relocate_tag(instance->group_tag, copy, resource->size, next, instance->tag_index))
			return ce_refuse("%s's resource in %s.map points out of itself", name, ce_resource_map_names[type]);
		if (sound_header)
			ce_sound_header(copy, resource->size, sound_header);
		instance->base_address = next;
		types[index] = (byte)(type + 1);
		next = (next + resource->size + 15) & ~15UL;
		image->size = next - image->base;
		(*copied)++;
	}
	if (ce_map_cache_version == CE_CACHE_VERSION_RETAIL && !ce_map_checking())
	{
		error(_error_silent, "HaloMD map: %ld bitmaps stood in for, %ld sounds silent (not in Custom Edition's "
			"resource maps)", stood_in, silenced);
	}
	*next_free = next;
	return TRUE;
}

/* a tag's bitmaps: each one a bitmap the game's renderer can make a texture
of (the game's own bitmap_verify, of the Xbox's flags, a format with a
Direct3D one, square cube maps) and its pixels in the file they are read
from (bitmaps.map's size if they are there, else the map's) */
static boolean ce_bitmaps_check(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	boolean indexed,
	unsigned long map_file_size)
{
	byte *group = ce_image_pointer(image, instance->base_address, CE_BITMAP_GROUP_SIZE);
	char const *name = ce_image_tag_name(image, instance);
	byte *bitmaps;
	long count, index;

	if (!group)
		return ce_refuse("bitmap %s is not in the tags", name);
	if (!ce_image_block(image, group + CE_BITMAP_GROUP_BITMAPS_OFFSET, CE_BITMAP_DATA_SIZE, CE_MAXIMUM_ELEMENTS,
		name, &count, &bitmaps))
	{
		return FALSE;
	}
	for (index = 0; index < count; index++)
	{
		struct bitmap_data bitmap;
		unsigned long file_size;
		boolean external;

		memcpy(&bitmap, bitmaps + index * CE_BITMAP_DATA_SIZE, sizeof(bitmap));
		external = indexed || (bitmap.flags & CE_BITMAP_EXTERNAL_FLAG);
		bitmap.flags &= CE_BITMAP_XBOX_FORMAT_FLAGS;
		if (!bitmap_verify(&bitmap, FALSE))
		{
			return ce_refuse("bitmap %ld of %s (%dx%dx%d, type %d, format %d) is not one the game draws", index, name,
				bitmap.width, bitmap.height, bitmap.depth, bitmap.type, bitmap.format);
		}
		/* (the formats with no Direct3D format, none linear but the
		uncompressed: xbox_texture_cache.c's tables) */
		switch (bitmap.format)
		{
		case 4: case 5: case 7: case 12: case 13:
			return ce_refuse("bitmap %ld of %s has no format the game draws (%d)", index, name, bitmap.format);
		case 14: case 15: case 16: case 17:
			if (bitmap.flags & CE_BITMAP_LINEAR_FLAG)
				return ce_refuse("bitmap %ld of %s is linear, of a format that cannot be", index, name);
			break;
		}
		if ((bitmap.type == 2 && bitmap.width != bitmap.height) || (bitmap.type != 1 && bitmap.depth != 1))
			return ce_refuse("bitmap %ld of %s is %dx%dx%d, not of its type's shape", index, name, bitmap.width,
				bitmap.height, bitmap.depth);
		if (external && !ce_resource_map_open(_ce_resource_bitmaps))
			return FALSE;
		file_size = external ? ce_resource_maps[_ce_resource_bitmaps].file_size : map_file_size;
		if (bitmap.pixels_offset < 0 || bitmap.pixels_size < 0 ||
			!ce_range_within((unsigned long)bitmap.pixels_offset, (unsigned long)bitmap.pixels_size, file_size))
		{
			return ce_refuse("bitmap %ld of %s: its pixels (%ld bytes at %ld) are not in %s", index, name,
				bitmap.pixels_size, bitmap.pixels_offset, external ? "bitmaps.map" : "the map");
		}
	}
	return TRUE;
}

/* a sound's pitch ranges and permutations: all in the tags, each played
permutation one of its range's, and each permutation's samples in the file
they are read from, its mouth data and subtitles in the tags */
static boolean ce_sound_check(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	boolean indexed,
	unsigned long map_file_size)
{
	byte *sound = ce_image_pointer(image, instance->base_address, CE_SOUND_HEADER_SIZE);
	char const *name = ce_image_tag_name(image, instance);
	unsigned long file_size = indexed ? ce_resource_maps[_ce_resource_sounds].file_size : map_file_size;
	byte *ranges;
	long range_count, range_index;

	if (!sound)
		return ce_refuse("sound %s is not in the tags", name);
	if (!ce_image_block(image, sound + CE_SOUND_PITCH_RANGES_OFFSET, CE_SOUND_PITCH_RANGE_SIZE, CE_MAXIMUM_ELEMENTS,
		name, &range_count, &ranges))
	{
		return FALSE;
	}
	for (range_index = 0; range_index < range_count; range_index++)
	{
		byte *range = ranges + range_index * CE_SOUND_PITCH_RANGE_SIZE;
		short actual_count;
		byte *permutations;
		long permutation_count, permutation_index;

		if (!ce_image_block(image, range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET, CE_SOUND_PERMUTATION_SIZE,
			CE_MAXIMUM_ELEMENTS, name, &permutation_count, &permutations))
		{
			return FALSE;
		}
		memcpy(&actual_count, range + CE_PITCH_RANGE_ACTUAL_PERMUTATION_COUNT_OFFSET, sizeof(actual_count));
		if (actual_count < 0 || actual_count > permutation_count || actual_count > CE_MAXIMUM_PLAYED_PERMUTATIONS)
		{
			return ce_refuse("sound %s plays %d of its %ld permutations", name, actual_count, permutation_count);
		}
		for (permutation_index = 0; permutation_index < permutation_count; permutation_index++)
		{
			byte *permutation = permutations + permutation_index * CE_SOUND_PERMUTATION_SIZE;
			long samples_size = (long)ce_read_long(permutation + CE_PERMUTATION_SAMPLES_OFFSET);
			long samples_offset = (long)ce_read_long(permutation + CE_PERMUTATION_SAMPLES_OFFSET + 8);
			short next;
			unsigned long ignored_size;
			byte *ignored;

			memcpy(&next, permutation + CE_PERMUTATION_NEXT_OFFSET, sizeof(next));
			if (next != NONE && (next < 0 || next >= permutation_count))
				return ce_refuse("sound %s: a permutation follows on with permutation %d", name, next);
			if (samples_size < 0 || samples_offset < 0 ||
				!ce_range_within((unsigned long)samples_offset, (unsigned long)samples_size, file_size))
			{
				return ce_refuse("sound %s: samples (%ld bytes at %ld) not in %s", name, samples_size, samples_offset,
					indexed ? "sounds.map" : "the map");
			}
			if (!ce_image_data(image, permutation + CE_PERMUTATION_MOUTH_DATA_OFFSET, name, &ignored_size, &ignored) ||
				!ce_image_data(image, permutation + CE_PERMUTATION_SUBTITLE_DATA_OFFSET, name, &ignored_size,
					&ignored))
			{
				return FALSE;
			}
		}
	}
	return TRUE;
}

/* ---------- public code */

/* a Custom Edition map being checked (ce_map_checks.c): its indexed tags'
resources copied into the image of its tag cache, below end_free, as loading
copies them, and every bitmap and sound checked; the end of the copies in
*next_free */
boolean ce_resources_check(
	struct ce_image *image,
	void *tag_instances,
	long tag_count,
	unsigned long end_free,
	unsigned long map_file_size,
	unsigned long *next_free)
{
	byte *types = malloc(tag_count);
	long copied, index;
	boolean valid;

	if (!types)
		return ce_refuse("no memory to check its resources");
	valid = ce_resources_place(image, tag_instances, tag_count, end_free, types, next_free, &copied);
	for (index = 0; valid && index < tag_count; index++)
	{
		struct ce_tag_instance const *instance = (struct ce_tag_instance const *)((byte const *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);

		if (instance->group_tag == 'bitm')
			valid = ce_bitmaps_check(image, instance, types[index] != 0, map_file_size);
		else if (instance->group_tag == 'snd!')
			valid = ce_sound_check(image, instance, types[index] != 0, map_file_size);
	}
	free(types);
	return valid;
}

/* a Custom Edition map's tags loaded (cache_files.c): its indexed tags'
resources copied in between first_free and end_free (Xbox addresses);
FALSE if one could not be (the map was checked before it was opened, as
it is copied in here: ce_map_checks.c) */
boolean ce_resources_tags_loaded(
	void *tag_instances,
	long tag_count,
	unsigned long first_free,
	unsigned long end_free)
{
	struct ce_image image;
	unsigned long next;
	long copied = 0;
	long index;

	system_free(ce_indexed_tags);
	ce_indexed_tags = system_malloc(tag_count);
	ce_indexed_tag_count = ce_indexed_tags ? tag_count : 0;
	ce_free_next = ce_free_end = 0;
	if (!ce_indexed_tags)
		return FALSE;
	image.data = xbox_pointer(CE_TAG_CACHE_BASE);
	image.base = CE_TAG_CACHE_BASE;
	image.size = first_free - CE_TAG_CACHE_BASE;
	if (!ce_resources_place(&image, tag_instances, tag_count, end_free, ce_indexed_tags, &next, &copied))
		return FALSE;
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
		byte *bitmap_group, *elements;
		long count, bitmap;

		if (instance->group_tag != 'bitm')
			continue;
		bitmap_group = ce_image_pointer(&image, instance->base_address, CE_BITMAP_GROUP_SIZE);
		if (!bitmap_group || !ce_image_block(&image, bitmap_group + CE_BITMAP_GROUP_BITMAPS_OFFSET,
			CE_BITMAP_DATA_SIZE, CE_MAXIMUM_ELEMENTS, "bitmaps", &count, &elements))
		{
			continue;
		}
		for (bitmap = 0; bitmap < count; bitmap++)
		{
			byte *data = elements + bitmap * CE_BITMAP_DATA_SIZE;

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
	ce_sounds_decode(tag_instances, tag_count);
	return TRUE;
}

/* a sound's compression, which the game plays all its permutations by
(sound_manager.c), made its permutations' when they all agree on another
one: Halo PC's engine went by each permutation's, and some maps' tools left
a sound marked Xbox ADPCM over uncompressed samples (mermaids_plaza's
magnum's ready sound, which this engine played as noise). TRUE if it was */
static boolean ce_sound_compression_repair(
	byte *sound)
{
	unsigned long pitch_range_count = *(unsigned long *)(sound + CE_SOUND_PITCH_RANGES_OFFSET);
	byte *ranges = xbox_pointer(*(unsigned long *)(sound + CE_SOUND_PITCH_RANGES_OFFSET + 4));
	short compression = NONE;
	unsigned long pitch_range;

	for (pitch_range = 0; pitch_range < pitch_range_count; pitch_range++)
	{
		byte *range = ranges + pitch_range * CE_SOUND_PITCH_RANGE_SIZE;
		unsigned long permutation_count = *(unsigned long *)(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET);
		byte *permutations = xbox_pointer(*(unsigned long *)(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET + 4));
		unsigned long permutation;

		for (permutation = 0; permutation < permutation_count; permutation++)
		{
			short each = *(short *)(permutations + permutation * CE_SOUND_PERMUTATION_SIZE +
				CE_PERMUTATION_COMPRESSION_OFFSET);

			if (compression == NONE)
				compression = each;
			else if (each != compression)
				return FALSE;
		}
	}
	if (compression == NONE || compression == *(short *)(sound + CE_SOUND_COMPRESSION_OFFSET))
		return FALSE;
	*(short *)(sound + CE_SOUND_COMPRESSION_OFFSET) = compression;
	return TRUE;
}

/* Halo PC's Ogg Vorbis sounds made the engine's 16-bit PCM ones (which it
plays on PCM channels: sound_preferences.c), those whose samples are in
sounds.map and those whose samples are in the map itself (kokiriforest's and
rainbow road's ambience, most of mermaids_plaza's sounds, which this engine
played as noise when they were left as they were): each permutation's
samples decoded (ce_vorbis.c) into maps\ce\ce_sounds.pcm, where they are then
read from, and the sound and its permutations marked uncompressed. Their
channels and rate stay the sound's (an Ogg of others is left as it was,
which the game does not play). (The sounds' blocks and samples were checked:
ce_resources_check; their compression first made their permutations':
ce_sound_compression_repair.) */
static void ce_sounds_decode(
	void *tag_instances,
	long tag_count)
{
	HANDLE sounds_map = ce_resource_maps[_ce_resource_sounds].file;
	HANDLE map_file = cache_files_ce_map_file();
	char path[256];
	HANDLE file = NULL;
	unsigned long written = 0;
	long sounds = 0, in_map = 0, permutations_decoded = 0, repaired = 0;
	long index;

	if (ce_decoded_sounds_file)
	{
		CloseHandle(ce_decoded_sounds_file);
		ce_decoded_sounds_file = NULL;
	}
	sprintf(path, "%sce\\ce_sounds.pcm", cache_files_map_directory());
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		byte *sound;
		HANDLE source;
		unsigned long pitch_range_count, pitch_ranges, pitch_range;
		short encoding, sample_rate;
		boolean all = TRUE;

		if (instance->group_tag != 'snd!')
			continue;
		/* (its samples' file: sounds.map's for a sound copied in from there,
		the map's for one of its own) */
		if (ce_indexed_tags[index] == _ce_resource_sounds + 1)
			source = sounds_map;
		else if (!ce_indexed_tags[index])
			source = map_file;
		else
			continue;
		sound = xbox_pointer(instance->base_address);
		if (ce_sound_compression_repair(sound))
			repaired++;
		if (!source || *(short *)(sound + CE_SOUND_COMPRESSION_OFFSET) != CE_SOUND_COMPRESSION_OGG)
			continue;
		if (!file)
		{
			file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
			if (file == INVALID_HANDLE_VALUE)
			{
				error(_error_silent, "Custom Edition maps: cannot write %s; Ogg Vorbis sounds will not play", path);
				return;
			}
		}
		encoding = *(short *)(sound + CE_SOUND_ENCODING_OFFSET);
		sample_rate = *(short *)(sound + CE_SOUND_SAMPLE_RATE_OFFSET);
		pitch_range_count = *(unsigned long *)(sound + CE_SOUND_PITCH_RANGES_OFFSET);
		pitch_ranges = *(unsigned long *)(sound + CE_SOUND_PITCH_RANGES_OFFSET + 4);
		for (pitch_range = 0; pitch_range < pitch_range_count; pitch_range++)
		{
			byte *range = (byte *)xbox_pointer(pitch_ranges) + pitch_range * CE_SOUND_PITCH_RANGE_SIZE;
			unsigned long permutation_count = *(unsigned long *)(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET);
			unsigned long permutations = *(unsigned long *)(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET + 4);
			unsigned long permutation;

			for (permutation = 0; permutation < permutation_count; permutation++)
			{
				byte *at = (byte *)xbox_pointer(permutations) + permutation * CE_SOUND_PERMUTATION_SIZE;
				long size = *(long *)(at + CE_PERMUTATION_SAMPLES_OFFSET);
				unsigned long offset = *(unsigned long *)(at + CE_PERMUTATION_SAMPLES_OFFSET + 8);
				unsigned char *data;
				short *samples = NULL;
				int channels = 0, rate = 0, frames;
				unsigned long bytes, bytes_written = 0;

				if (*(short *)(at + CE_PERMUTATION_COMPRESSION_OFFSET) != CE_SOUND_COMPRESSION_OGG || size <= 0)
					continue;
				data = malloc((size_t)size);
				frames = data && ce_read(source, offset, data, (unsigned long)size) ?
					ce_vorbis_decode(data, size, &channels, &rate, &samples) : -1;
				free(data);
				/* (and no more than the sound cache could hold of it) */
				if (frames <= 0 || channels != (encoding ? 2 : 1) || rate != (sample_rate ? 44100 : 22050) ||
					frames > 0x1000000)
				{
					error(_error_silent, "Custom Edition maps: %s: an Ogg Vorbis permutation of %d channels at %d Hz",
						(char const *)xbox_pointer(instance->name), channels, rate);
					ce_vorbis_free(samples);
					all = FALSE;
					continue;
				}
				bytes = (unsigned long)frames * (unsigned long)channels * sizeof(short);
				if (written > 0x7fffffff - bytes || !WriteFile(file, samples, bytes, &bytes_written, NULL) ||
					bytes_written != bytes)
				{
					ce_vorbis_free(samples);
					all = FALSE;
					continue;
				}
				ce_vorbis_free(samples);
				*(long *)(at + CE_PERMUTATION_SAMPLES_OFFSET) = (long)bytes;
				*(unsigned long *)(at + CE_PERMUTATION_SAMPLES_OFFSET + 8) = written;
				*(unsigned long *)(at + 0x38) = bytes;
				*(short *)(at + CE_PERMUTATION_COMPRESSION_OFFSET) = CE_SOUND_COMPRESSION_NONE;
				written += bytes;
				permutations_decoded++;
			}
		}
		if (all)
		{
			*(short *)(sound + CE_SOUND_COMPRESSION_OFFSET) = CE_SOUND_COMPRESSION_NONE;
			ce_indexed_tags[index] = CE_DECODED_SOUND;
			sounds++;
			if (source == map_file)
				in_map++;
		}
	}
	ce_decoded_sounds_file = file;
	error(_error_silent, "Custom Edition maps: %ld Ogg Vorbis sounds decoded, %ld of them the map's (%ld permutations, "
		"%lu bytes); %ld sounds given their permutations' compression", sounds, in_map, permutations_decoded, written,
		repaired);
}

/* the resource map a tag's pixels or samples are read from (cache_files_windows.c),
or NULL: the map itself */
HANDLE ce_resources_file_for_tag(
	long tag_index)
{
	long index = tag_index & 0xffff;

	if (tag_index == NONE || index >= ce_indexed_tag_count || !ce_indexed_tags[index])
		return NULL;
	if (ce_indexed_tags[index] == CE_DECODED_SOUND)
		return ce_decoded_sounds_file;
	return ce_resource_maps[ce_indexed_tags[index] - 1].file;
}

/* size bytes (16-byte aligned) of the map's tag cache, after its tags and
resources (ce_models.c): their Xbox address, or 0 if there is no room */
unsigned long ce_resources_allocate(
	unsigned long size)
{
	unsigned long address = ce_free_next;

	if (!address || address > ce_free_end || size > ce_free_end - address)
		return 0;
	ce_free_next = (address + size + 15) & ~15UL;
	return address;
}

/* where the next allocation goes (a BSP's: ce_bsp.c gives back what it took
when it unloads, ce_resources_free_to) */
unsigned long ce_resources_free_mark(
	void)
{
	return ce_free_next;
}

void ce_resources_free_to(
	unsigned long mark)
{
	if (mark && mark <= ce_free_next)
		ce_free_next = mark;
}

/* the map unloaded (cache_files.c) */
void ce_resources_tags_unloaded(
	void)
{
	ce_indexed_tag_count = 0;
	ce_free_next = ce_free_end = 0;
}

#endif
