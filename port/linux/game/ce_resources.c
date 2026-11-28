/*
CE_RESOURCES.C

Custom Edition maps' indexed tags (cache_files.c, Custom Edition maps). A
Custom Edition map keeps most of its bitmaps, sounds, fonts and strings in
the resource maps beside it (maps_ce\bitmaps.map, sounds.map, loc.map): such
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

HaloMD's maps (Halo PC retail's) read their stock bitmaps' pixels and
sounds' samples from Halo PC retail's own bitmaps.map and sounds.map, at the
offsets their tags give, when the player has put them in maps_pc; else (or
for one not there) they are found in Custom Edition's by their tags' paths
(below).
*/

#ifdef HALO_CUSTOM_EDITION

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "main/console.h"
#include "game/game.h"
#include "game/players.h"
#include "interface/hud_messaging.h"
#include "tag_files/tag_groups.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "ce_map_checks.h"
#include "content_resource_map.h"
#include "content_sha256.h"
#include "halo_map_families.h"
#include "halopc_bitmap_offsets.h"
#include "opensauce_survey.h"
#ifndef HALO_CONSOLE
#include "ce_sound_pool.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	_ce_resource_bitmaps,
	_ce_resource_sounds,
	_ce_resource_strings,
	/* Halo PC retail's own bitmaps.map and sounds.map (maps_pc), which a
	retail map's stock bitmaps and sounds are read from when they are
	there: the player's own copies, never shipped */
	_ce_resource_halo_pc_bitmaps,
	_ce_resource_halo_pc_sounds,
	NUMBER_OF_CE_RESOURCE_MAPS,

	/* a bitmap's flags the Xbox's tags keep (bitmap_utilities.c: power of
	two, compressed, palettized, swizzled, linear, v16u16) */
	CE_BITMAP_XBOX_FORMAT_FLAGS = 0x3f,
	CE_BITMAP_LINEAR_FLAG = 0x10,
	/* Halo PC's: the bitmap's pixels are in bitmaps.map, though its tag is
	in the map */
	CE_BITMAP_EXTERNAL_FLAG = 0x100,
	/* (xbox_texture_cache.c's _bitmap_cached_bit) */
	CE_BITMAP_CACHED_FLAG = 0x80,
	/* MCC's high-quality compression (BC7), drawn as DXT5's format with
	its blocks decoded as BC7's (ce_repairs.c), and DXT5 (bitmaps.h) */
	CE_BITMAP_FORMAT_BC7 = 18,
	CE_BITMAP_FORMAT_DXT5 = 16,

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
	/* Halo PC's flag on a permutation's samples (on their tag data's
	flags, after its size): they are in sounds.map, at the offset the tag
	data gives, not in the map */
	CE_SAMPLES_EXTERNAL_FLAG = 1,
	CE_PERMUTATION_MOUTH_DATA_OFFSET = 0x54,
	CE_PERMUTATION_SUBTITLE_DATA_OFFSET = 0x68,
	/* the most a map's decoded sounds take on disk (z:\ce_sounds.pcm), 512
	MB: a map's Ogg Vorbis streams may decode to any length (silence takes
	almost nothing compressed); the shipped maps' take a few megabytes */
	CE_MAXIMUM_DECODED_SOUNDS_SIZE = 0x20000000,

	/* the bitmap group's header and its bitmaps (bitmap_group.h) */
	CE_BITMAP_GROUP_SIZE = 0x6c,
	CE_BITMAP_GROUP_BITMAPS_OFFSET = 0x60,
	CE_BITMAP_DATA_SIZE = 0x30,

	/* (a tag block's or tag data's count or size: larger ones would not
	fit in a resource or a tag cache in any case) */
	CE_MAXIMUM_ELEMENTS = 0x10000,
};

/* ---------- structures */

struct ce_resource_map
{
	HANDLE file;
	unsigned long file_size;
	unsigned long count;
	struct content_resource_entry *resources;
	char *paths;
	unsigned long paths_size;
};

typedef char verify_ce_bitmap_data_size[sizeof(struct bitmap_data) == CE_BITMAP_DATA_SIZE ? 1 : -1];

/* ---------- prototypes */

char const *cache_files_map_directory(void);
HANDLE cache_files_ce_map_file(void);
int ce_vorbis_decode(const unsigned char *data, int size, int *channels, int *sample_rate, short **samples);
int ce_vorbis_resample(short **samples, int frames, int channels, int rate, int wanted);
int ce_vorbis_edges(const unsigned char *data, int size, int count, int whole, int *channels, int *sample_rate,
	short *head, int *head_frames, short *tail, int *tail_frames);
int ce_vorbis_resample_piece(short **samples, int frames, int channels, int rate, int wanted,
	const short *before, int before_frames, const short *after, int after_frames);
void ce_vorbis_free(short *samples);
void *ce_vorbis_stream_open(const unsigned char *data, int size, int *channels, int *sample_rate);
int ce_vorbis_stream_read(void *stream, short *buffer, int frames);
void ce_vorbis_stream_close(void *stream);
short *ce_vorbis_samples_new(int frames, int channels);
unsigned long ce_resources_allocate_tags(unsigned long size);
#ifndef HALO_CONSOLE
static unsigned long ce_resources_allocate_split(unsigned long size);
#endif
static void ce_sounds_decode(void *tag_instances, long tag_count);
static boolean ce_samples_external(byte const *permutation);

/* ---------- globals */

static struct ce_resource_map ce_resource_maps[NUMBER_OF_CE_RESOURCE_MAPS];
static char const *const ce_resource_map_names[NUMBER_OF_CE_RESOURCE_MAPS] =
	{ "bitmaps", "sounds", "loc", "bitmaps", "sounds" };
/* the indexed tags of the map loaded: their resource map, by tag index (+1;
0 none) */
static byte *ce_indexed_tags;
static long ce_indexed_tag_count;
/* the map's Ogg Vorbis sounds' samples, decoded to 16-bit PCM (z:\ce_sounds.pcm),
and its path */
static HANDLE ce_decoded_sounds_file;
static char ce_decoded_sounds_path[256];
#ifndef HALO_CONSOLE
/* the decoded sounds (desktop): each sound's job (what decodes it again),
its piece of the file (ce_sound_pool.h: where it is, while it is in it),
and the offset its tags read it at (its piece's at the map's load; past
the file's budget for one that did not fit then, decoded when it is first
read: ce_resources_sound_read). By tag index: its entry, or NONE */
static struct ce_sound_job *ce_sound_jobs;
static struct ce_sound_pool_entry *ce_sound_pool;
static unsigned long *ce_sound_bases;
static long ce_sound_entry_count, ce_sound_entry_capacity;
static long *ce_sound_entry_by_tag;
static long ce_sound_entry_tag_count;
/* (said once a map: the file full at load, sounds moved out, one that could
not be put in) */
static boolean ce_sound_full_told, ce_sound_evicted_told, ce_sound_no_room_told;
static long ce_sound_reloads, ce_sound_evictions;
/* the decoded sounds' file's bytes, and which map's they are (a read is
told finished for its own map's sounds alone) */
static unsigned long ce_decoded_sounds_size;
static long ce_sound_generation;
/* (ce_read at the place given, not the file's position) */
static boolean ce_read_positioned;
#endif
/* the space left in the map's tag cache (ce_resources_allocate) */
static unsigned long ce_free_next, ce_free_end;
/* and in the room above it (platform.h), once that runs out: the next free
address there, 0 while the tag cache's room is used. Allocations only ever
move up, so a mark (ce_resources_free_mark) is the lowest address of all
that is allocated after it */
static unsigned long ce_room_next;
/* the bytes the tag validator was given right after the tags
(ce_resources_allocate_tags) */
static unsigned long ce_tags_room_used;
/* what the placing of a map's resources wrote in its tags
(ce_resources_place), hashed as it is written: NULL when not placing */
static struct content_sha256 *ce_resources_written;

/* ---------- private code */

static boolean ce_read(
	HANDLE file,
	unsigned long offset,
	void *buffer,
	unsigned long size)
{
	unsigned long bytes_read = 0;

#ifndef HALO_CONSOLE
	/* (while a sound is decoded again in play, at its place: the cache's
	thread reads the same files then, ce_sound_entry_load) */
	if (ce_read_positioned)
	{
		OVERLAPPED overlapped;

		memset(&overlapped, 0, sizeof(overlapped));
		overlapped.Offset = offset;
		return ReadFile(file, buffer, size, &bytes_read, &overlapped) && bytes_read == size;
	}
#endif
	if (SetFilePointer(file, (long)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return FALSE;
	return ReadFile(file, buffer, size, &bytes_read, NULL) && bytes_read == size;
}

/* bytes of the image the placing of a map's resources may have written
(a retail map's bitmaps and sounds, ce_retail_*): hashed, with where they
are, while it places them (ce_resources_place) */
static void ce_resources_placed(
	struct ce_image const *image,
	void const *at,
	unsigned long size)
{
	unsigned long where[2];

	if (!ce_resources_written || !at)
		return;
	where[0] = image->base + (unsigned long)((byte const *)at - image->data);
	where[1] = size;
	content_sha256_add(ce_resources_written, where, sizeof(where));
	content_sha256_add(ce_resources_written, at, size);
}

static boolean ce_resource_map_open(
	short type)
{
	struct ce_resource_map *map = &ce_resource_maps[type];
	char path[256];
	byte header_bytes[CONTENT_RESOURCE_HEADER_SIZE];
	struct content_resource_header header;
	unsigned long file_size, file_size_high = 0;
	HANDLE file;

	if (map->file)
		return TRUE;
	/* (in maps_ce, or its older places: halo_map_families.h) */
	map_family_resource(ce_resource_map_names[type], path, sizeof(path));
	file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		ce_refusal_explain("It needs Custom Edition's %s.map: copy it into maps_ce.", ce_resource_map_names[type]);
		return ce_refuse("there is no %s", path);
	}
	file_size = GetFileSize(file, &file_size_high);
	/* (its type, its paths before its resources' table, the table in the
	file) */
	if (file_size == INVALID_FILE_SIZE || file_size_high || file_size > 0x7fffffff ||
		!ce_read(file, 0, header_bytes, sizeof(header_bytes)))
	{
		CloseHandle(file);
		return ce_refuse("%s is not a resource map", path);
	}
	content_resource_header_read(header_bytes, &header);
	if (header.type != (unsigned long)(type + 1) || header.count > CE_MAXIMUM_ELEMENTS ||
		!content_resource_header_within(&header, file_size))
	{
		CloseHandle(file);
		return ce_refuse("%s is not a resource map", path);
	}
	map->resources = system_malloc(header.count * sizeof(struct content_resource_entry) + 1);
	if (!map->resources ||
		!ce_read(file, header.table_offset, map->resources, header.count * sizeof(struct content_resource_entry)))
	{
		system_free(map->resources);
		map->resources = NULL;
		CloseHandle(file);
		return ce_refuse("%s's resources could not be read", path);
	}
	content_resource_table_read(map->resources, header.count);
	/* (the paths, to find a sound's resource by its tag's name) */
	map->paths_size = header.table_offset - header.paths_offset;
	map->paths = map->paths_size ? system_malloc(map->paths_size + 1) : NULL;
	if (map->paths && ce_read(file, header.paths_offset, map->paths, map->paths_size))
		map->paths[map->paths_size] = 0;
	else
	{
		system_free(map->paths);
		map->paths = NULL;
	}
	map->count = header.count;
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

/* (*cursor moved past count elements of element_size, FALSE if that passes
limit) */
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
same size. A bitmap is found there first by where its pixels are in Halo
PC's bitmaps.map (halopc_bitmap_offsets.h: which tag's bitmap each offset
holds), as Halo PC and HaloMD found it, and only then by its tag's path: the
tools that made some of HaloMD's maps left stock bitmaps renamed (Halo 2
anniversary maps' copy of the HUD's reticles is named after the Warthog's
tires, the fuel rod gun's after a hunter's), which drew another bitmap's
pixels. A bitmap with no namesake borrows the pixels of one of the same
type (and format, if there is one) there; a sound with none is made
silent (no pitch ranges: sound_manager.c does not play it). */

/* every bitmap in Custom Edition's bitmaps.map (CE_BITMAP_DATA_SIZE each),
read once, for stand-ins */
#ifdef HALO_CONSOLE_CE_SHARED_TAG_CACHE
void *platform_ce_heap_realloc(void *address, size_t size);
#endif
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
	struct content_resource_entry const *resource = &map->resources[resource_index];
	byte *copy;

	if (resource->size < minimum_size || resource->size > maximum_size ||
		!ce_range_within(resource->offset, resource->size, map->file_size))
	{
		return NULL;
	}
	copy = malloc(resource->size);
	if (copy && !ce_read(map->file, resource->offset, copy, resource->size))
	{
		if (copy)
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
		if (copy)
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
#ifdef HALO_CONSOLE_CE_SHARED_TAG_CACHE
		/* (kept for good: the heap's, not what a 64 MB console's load borrows,
		port/xbox's xbox_ce_files.c) */
		grown = platform_ce_heap_realloc(ce_retail_stand_ins, (ce_retail_stand_in_count + count) * CE_BITMAP_DATA_SIZE);
#else
		grown = realloc(ce_retail_stand_ins, (ce_retail_stand_in_count + count) * CE_BITMAP_DATA_SIZE);
#endif
		if (grown)
		{
			memcpy(grown + ce_retail_stand_in_count * CE_BITMAP_DATA_SIZE, bitmaps, count * CE_BITMAP_DATA_SIZE);
			ce_retail_stand_ins = grown;
			ce_retail_stand_in_count += count;
		}
		if (bitmaps)
			free(bitmaps);
	}
}

/* a bitmap given another's shape, format and pixels (in bitmaps.map) */
static void ce_retail_bitmap_take(
	byte *data,
	byte const *other)
{
	unsigned short flags, other_flags;

	memcpy(&flags, data + 0x0e, 2);
	memcpy(&other_flags, other + 0x0e, 2);
	flags = (unsigned short)((flags & ~CE_BITMAP_XBOX_FORMAT_FLAGS) | (other_flags & CE_BITMAP_XBOX_FORMAT_FLAGS) |
		CE_BITMAP_EXTERNAL_FLAG);
	memcpy(data + 0x04, other + 0x04, 10);
	memcpy(data + 0x0e, &flags, 2);
	memcpy(data + 0x14, other + 0x14, 2);
	memcpy(data + 0x18, other + 0x18, 8);
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
				ce_retail_bitmap_take(data, stand_in);
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* a tag's path's hash, as halopc_bitmap_offsets.h has them (FNV-1a, 32
bits, of the path in lowercase) */
static unsigned long ce_path_hash(
	char const *path)
{
	unsigned long hash = 0x811c9dc5UL;

	for (; *path; path++)
	{
		unsigned char character = (unsigned char)*path;

		if (character >= 'A' && character <= 'Z')
			character = (unsigned char)(character - 'A' + 'a');
		hash = ((hash ^ character) * 0x01000193UL) & 0xffffffffUL;
	}
	return hash;
}

/* the bitmap whose pixels are at `offset` in Halo PC's bitmaps.map: its
tag's resource in Custom Edition's bitmaps.map (NONE if the offset is none of
Halo PC's stock bitmaps', or its tag is not there) and its index in the tag */
static long ce_retail_resource_at(
	struct ce_resource_map const *map,
	unsigned long offset,
	long *bitmap_index)
{
	long low = 0, high = (long)NUMBEROF(halopc_bitmap_offsets) - 1;
	unsigned long index;

	while (low <= high)
	{
		long middle = low + (high - low) / 2;
		unsigned long here = halopc_bitmap_offsets[middle].offset;

		if (here == offset)
		{
			unsigned long hash = halopc_bitmap_offsets[middle].path_hash;

			if (!map->paths)
				return NONE;
			for (index = 0; index < map->count; index++)
			{
				unsigned long path_offset = map->resources[index].path_offset;

				if (path_offset < map->paths_size && ce_path_hash(map->paths + path_offset) == hash)
				{
					*bitmap_index = halopc_bitmap_offsets[middle].index;
					return (long)index;
				}
			}
			return NONE;
		}
		if (here < offset)
			low = middle + 1;
		else
			high = middle - 1;
	}
	return NONE;
}

/* ---------- Halo PC retail's own resource maps (maps_pc)

Halo PC retail's bitmaps.map and sounds.map are laid out as Custom Edition's
are (a type, the offsets of the paths and of the table, a count; each
resource its path's offset, its size and its offset), but hold only pixels
(<path>_<n>, a tag's bitmap n) and samples (<path>__<range>__<permutation>):
the tags are in the maps. A retail map's tag gives each stock bitmap's pixels
and each stock permutation's samples as an offset and a size in them, which
Halo PC read as they were. So, with the player's own copy in maps_pc, a tag
whose every external bitmap or permutation is one of its resources (from its
start, and no longer) keeps its offsets and is read from there; any
other (a file that is not Halo PC's, a tag of a map built with other resource
maps) is found in Custom Edition's as before. Nothing in the file is trusted:
its header, its table and every resource are checked to lie in it, a file
whose resources are named as Custom Edition's is not taken, and the pixels
and samples are checked again with the map (ce_bitmaps_check, ce_sound_check).
*/

enum
{
	_ce_halo_pc_ready,
	_ce_halo_pc_missing,
	/* (not a resource map of its kind, or Custom Edition's) */
	_ce_halo_pc_unusable,
};

/* each Halo PC resource map's state as the placing of a map's resources
last found it (ce_resources_place_read looks again each time: a player may
add one between maps), and whether it was looked for yet this time */
static short ce_halo_pc_states[2] = { _ce_halo_pc_missing, _ce_halo_pc_missing };
static boolean ce_halo_pc_looked[2];
/* the lines for the player about them, once the game is under way
(ce_resources_update): "" for none */
static char ce_halo_pc_note[2][63];

static int ce_resource_offset_compare(
	void const *a,
	void const *b)
{
	struct content_resource_entry const *first = a, *second = b;

	if (first->offset != second->offset)
		return first->offset < second->offset ? -1 : 1;
	if (first->size != second->size)
		return first->size < second->size ? -1 : 1;
	return 0;
}

/* Halo PC's own bitmaps.map or sounds.map (type: _ce_resource_halo_pc_*),
opened once it is found and checked: FALSE (and its state kept) if it is
not in maps_pc or is not Halo PC's. Never a refusal: without it, a retail
map's stock bitmaps and sounds are found in Custom Edition's */
static boolean ce_halo_pc_resource_map_open(
	short type)
{
	struct ce_resource_map *map = &ce_resource_maps[type];
	short which = (short)(type - _ce_resource_halo_pc_bitmaps);
	unsigned long kind = type == _ce_resource_halo_pc_bitmaps ? 1 : 2;
	char const *custom_ending = type == _ce_resource_halo_pc_bitmaps ? "__pixels" : "__permutations";
	size_t custom_ending_length = strlen(custom_ending);
	char path[256];
	byte header_bytes[CONTENT_RESOURCE_HEADER_SIZE];
	struct content_resource_header header;
	unsigned long file_size, file_size_high = 0, paths_size, index, kept;
	struct content_resource_entry *resources;
	char *paths;
	HANDLE file;

	if (map->file)
		return TRUE;
	if (ce_halo_pc_looked[which])
		return FALSE;
	ce_halo_pc_looked[which] = TRUE;
	ce_halo_pc_states[which] = _ce_halo_pc_missing;
	if (!map_family_halo_pc_resource(ce_resource_map_names[type], path, sizeof(path)))
		return FALSE;
	file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return FALSE;
	ce_halo_pc_states[which] = _ce_halo_pc_unusable;
	file_size = GetFileSize(file, &file_size_high);
	/* (its type, its paths after its header and before its table, the
	table in the file, and no more than 4 MB of paths: Halo PC's are under
	200 KB) */
	memset(&header, 0, sizeof(header));
	if (file_size != INVALID_FILE_SIZE && !file_size_high && file_size <= 0x7fffffff &&
		ce_read(file, 0, header_bytes, sizeof(header_bytes)))
	{
		content_resource_header_read(header_bytes, &header);
	}
	if (header.type != kind || !header.count || header.count > CE_MAXIMUM_ELEMENTS ||
		header.paths_offset < CONTENT_RESOURCE_HEADER_SIZE || !content_resource_header_within(&header, file_size) ||
		header.table_offset - header.paths_offset > 0x400000)
	{
		error(_error_silent, "HaloMD maps: %s is not a resource map of its kind; it is not read", path);
		CloseHandle(file);
		return FALSE;
	}
	paths_size = header.table_offset - header.paths_offset;
	resources = malloc(header.count * sizeof(struct content_resource_entry));
	paths = malloc(paths_size + 1);
	if (!resources || !paths ||
		!ce_read(file, header.table_offset, resources, header.count * sizeof(struct content_resource_entry)) ||
		!ce_read(file, header.paths_offset, paths, paths_size))
	{
		error(_error_silent, "HaloMD maps: %s could not be read; it is not read", path);
		if (resources)
			free(resources);
		if (paths)
			free(paths);
		CloseHandle(file);
		return FALSE;
	}
	paths[paths_size] = 0;
	content_resource_table_read(resources, header.count);
	/* (each resource in the file, with a path, and none named as Custom
	Edition's: a Custom Edition resource map put in maps_pc is not Halo
	PC's, whose offsets the retail maps give) */
	for (index = 0, kept = 0; index < header.count; index++)
	{
		struct content_resource_entry const *resource = &resources[index];
		size_t length;

		if (resource->path_offset >= paths_size)
			continue;
		length = strlen(paths + resource->path_offset);
		if (length >= custom_ending_length &&
			!strcmp(paths + resource->path_offset + length - custom_ending_length, custom_ending))
		{
			error(_error_silent, "HaloMD maps: %s is Custom Edition's (its resources are named <path>%s), not Halo "
				"PC's own; it is not read", path, custom_ending);
			free(resources);
			free(paths);
			CloseHandle(file);
			return FALSE;
		}
		if (!resource->size || !ce_range_within(resource->offset, resource->size, file_size))
			continue;
		resources[kept++] = *resource;
	}
	free(paths);
	if (!kept)
	{
		error(_error_silent, "HaloMD maps: %s has no resources in it; it is not read", path);
		free(resources);
		CloseHandle(file);
		return FALSE;
	}
	/* (by offset, for ce_halo_pc_resource_is) */
	qsort(resources, kept, sizeof(struct content_resource_entry), ce_resource_offset_compare);
	map->resources = resources;
	map->count = kept;
	map->paths = NULL;
	map->paths_size = 0;
	map->file_size = file_size;
	map->file = file;
	ce_halo_pc_states[which] = _ce_halo_pc_ready;
	error(_error_silent, "HaloMD maps: Halo PC's own %s.map found (%s, %lu of its %lu resources in it)",
		ce_resource_map_names[type], path, kept, (unsigned long)header.count);
	return TRUE;
}

/* whether size bytes at offset are one of a Halo PC resource map's
resources, from its start, or the first size bytes of one (HaloMD's maps cut
some stock sounds short: the pistol's fire, the shield's alarm). Its
resources are by offset (ce_halo_pc_resource_map_open) */
static boolean ce_halo_pc_resource_is(
	struct ce_resource_map const *map,
	unsigned long offset,
	unsigned long size)
{
	long low = 0, high = (long)map->count;

	/* (the first at offset or past it) */
	while (low < high)
	{
		long middle = low + (high - low) / 2;

		if (map->resources[middle].offset < offset)
			low = middle + 1;
		else
			high = middle;
	}
	for (; low < (long)map->count && map->resources[low].offset == offset; low++)
	{
		if (size <= map->resources[low].size)
			return TRUE;
	}
	return FALSE;
}

/* what ce_bitmap_drawable finds of a bitmap */
enum
{
	_ce_bitmap_drawn,
	/* (not one the game's bitmap_verify takes) */
	_ce_bitmap_not_drawn,
	/* (a format with no Direct3D one) */
	_ce_bitmap_no_format,
	/* (linear, of a format that cannot be) */
	_ce_bitmap_linear,
	/* (not of its type's shape) */
	_ce_bitmap_misshapen,
};

/* a bitmap (data, CE_BITMAP_DATA_SIZE bytes) as the game's renderer would
make a texture of it, into *bitmap: one the game's own bitmap_verify takes
(of the Xbox's flags), of a format with a Direct3D one, of its type's shape
(ce_bitmaps_check's refusals, by what this gives) */
static short ce_bitmap_drawable(
	byte const *data,
	struct bitmap_data *bitmap)
{
	memcpy(bitmap, data, sizeof(*bitmap));
	bitmap->flags &= CE_BITMAP_XBOX_FORMAT_FLAGS;
	/* (a BC7 bitmap is checked as the DXT5 one it is made: its blocks are
	the same size) */
	if (bitmap->format == CE_BITMAP_FORMAT_BC7)
		bitmap->format = CE_BITMAP_FORMAT_DXT5;
	if (!bitmap_verify(bitmap, FALSE))
		return _ce_bitmap_not_drawn;
	/* (the formats with no Direct3D format, none linear but the
	uncompressed: xbox_texture_cache.c's tables) */
	switch (bitmap->format)
	{
	case 4: case 5: case 7: case 12: case 13:
		return _ce_bitmap_no_format;
	case 14: case 15: case 16: case 17:
		if (bitmap->flags & CE_BITMAP_LINEAR_FLAG)
			return _ce_bitmap_linear;
		break;
	}
	if ((bitmap->type == 2 && bitmap->width != bitmap->height) || (bitmap->type != 1 && bitmap->depth != 1))
		return _ce_bitmap_misshapen;
	return _ce_bitmap_drawn;
}

/* a retail map's bitmap tag whose bitmaps (count of them, all external)
are each a resource of Halo PC's own bitmaps.map, and each one the game
draws as the tag gives it: TRUE if they all are (their offsets are then
kept, and read from there). A protected map's tags may give a bitmap's shape
or format scrambled, which Custom Edition's bitmaps.map's namesake mends as
before (ce_retail_bitmap_take) */
static boolean ce_halo_pc_bitmaps(
	byte const *bitmaps,
	long count)
{
	struct ce_resource_map const *map = &ce_resource_maps[_ce_resource_halo_pc_bitmaps];
	long index;

	if (!count || !ce_halo_pc_resource_map_open(_ce_resource_halo_pc_bitmaps))
		return FALSE;
	for (index = 0; index < count; index++)
	{
		byte const *data = bitmaps + index * CE_BITMAP_DATA_SIZE;
		long offset = (long)ce_read_long(data + 0x18);
		long size = (long)ce_read_long(data + 0x1c);
		struct bitmap_data bitmap;

		/* (a tag's bitmaps are all read from one file: ce_resources_file_for_tag) */
		if (!(*(unsigned short const *)(data + 0x0e) & CE_BITMAP_EXTERNAL_FLAG) || offset < 0 || size <= 0 ||
			!ce_halo_pc_resource_is(map, (unsigned long)offset, (unsigned long)size) ||
			ce_bitmap_drawable(data, &bitmap) != _ce_bitmap_drawn)
		{
			return FALSE;
		}
	}
	return TRUE;
}

/* a retail map's sound (its header, and its pitch ranges, range_count of
them, in the image) of an encoding and compression there are, whose every
permutation with samples is of a compression there is and has them external
and a resource of Halo PC's own sounds.map: TRUE if so (their offsets are
then kept, and read from there). A protected map's tags may give a sound's
encoding or compression scrambled, which Custom Edition's sounds.map's
namesake is checked against as before (ce_retail_sound) */
static boolean ce_halo_pc_sound(
	struct ce_image *image,
	byte const *sound,
	byte *ranges,
	long range_count,
	char const *name)
{
	struct ce_resource_map const *map = &ce_resource_maps[_ce_resource_halo_pc_sounds];
	long range_index, external = 0;
	short encoding, compression;

	memcpy(&encoding, sound + CE_SOUND_ENCODING_OFFSET, sizeof(encoding));
	memcpy(&compression, sound + CE_SOUND_COMPRESSION_OFFSET, sizeof(compression));
	/* (mono or stereo; none, Xbox ADPCM, IMA ADPCM or Ogg Vorbis:
	sound_manager.c's) */
	if (encoding < 0 || encoding > 1 || compression < CE_SOUND_COMPRESSION_NONE ||
		compression > CE_SOUND_COMPRESSION_OGG || !ce_halo_pc_resource_map_open(_ce_resource_halo_pc_sounds))
	{
		return FALSE;
	}
	for (range_index = 0; range_index < range_count; range_index++)
	{
		byte *permutations;
		long permutation_count, permutation_index;

		if (!ce_image_block(image, ranges + range_index * CE_SOUND_PITCH_RANGE_SIZE + CE_PITCH_RANGE_PERMUTATIONS_OFFSET,
			CE_SOUND_PERMUTATION_SIZE, CE_MAXIMUM_ELEMENTS, name, &permutation_count, &permutations))
		{
			return FALSE;
		}
		for (permutation_index = 0; permutation_index < permutation_count; permutation_index++)
		{
			byte const *permutation = permutations + permutation_index * CE_SOUND_PERMUTATION_SIZE;
			long size = (long)ce_read_long(permutation + CE_PERMUTATION_SAMPLES_OFFSET);
			long offset = (long)ce_read_long(permutation + CE_PERMUTATION_SAMPLES_OFFSET + 8);
			short permutation_compression;

			if (!size)
				continue;
			memcpy(&permutation_compression, permutation + CE_PERMUTATION_COMPRESSION_OFFSET,
				sizeof(permutation_compression));
			if (permutation_compression < CE_SOUND_COMPRESSION_NONE || permutation_compression > CE_SOUND_COMPRESSION_OGG)
				return FALSE;
			/* (a sound's samples are all read from one file: one with some in
			the map is found in Custom Edition's as before) */
			if (!ce_samples_external(permutation) || size < 0 || offset < 0 ||
				!ce_halo_pc_resource_is(map, (unsigned long)offset, (unsigned long)size))
			{
				return FALSE;
			}
			external++;
		}
	}
	return external != 0;
}

/* a retail map loaded with stock bitmaps stood in for or sounds silent
(ce_resources_place_read's counts) while Halo PC's own bitmaps.map or
sounds.map, which would have them, is missing from maps_pc or not Halo PC's:
one line logged, and two for the player saying which file to add, shown on
the HUD (and the console) a second into the game: ce_resources_update */
static void ce_halo_pc_missing_tell(
	long stood_in,
	long silenced)
{
	boolean bitmaps = stood_in && ce_halo_pc_states[0] != _ce_halo_pc_ready;
	boolean sounds = silenced && ce_halo_pc_states[1] != _ce_halo_pc_ready;
	boolean unusable = (bitmaps && ce_halo_pc_states[0] == _ce_halo_pc_unusable) ||
		(sounds && ce_halo_pc_states[1] == _ce_halo_pc_unusable);
	char const *files = bitmaps && sounds ? "bitmaps.map and sounds.map" : bitmaps ? "bitmaps.map" : "sounds.map";

	ce_halo_pc_note[0][0] = ce_halo_pc_note[1][0] = 0;
	if (!bitmaps && !sounds)
		return;
	/* (two lines, each short enough for one of the HUD's: 62 characters) */
	snprintf(ce_halo_pc_note[0], sizeof(ce_halo_pc_note[0]), "%s", bitmaps && sounds ?
		"Some textures and sounds in this map are missing." : bitmaps ? "Some textures in this map are stand-ins." :
		"Some sounds in this map are silent.");
	if (unusable)
	{
		error(_error_silent, "HaloMD map: the %s in maps_pc isn't Halo PC's own: %ld bitmaps stood in for, %ld sounds "
			"silent", files, stood_in, silenced);
		snprintf(ce_halo_pc_note[1], sizeof(ce_halo_pc_note[1]), "The %s in maps_pc %s Halo PC's.", files,
			bitmaps && sounds ? "aren't" : "isn't");
	}
	else
	{
		error(_error_silent, "HaloMD map: Halo PC's own %s missing from maps_pc: %ld bitmaps stood in for, %ld sounds "
			"silent", files, stood_in, silenced);
		snprintf(ce_halo_pc_note[1], sizeof(ce_halo_pc_note[1]), "Copy Halo PC's %s to maps_pc.", files);
	}
}

/* a retail map's bitmap tag: its external bitmaps' pixels read from Halo
PC's own bitmaps.map, if it is there and has them all (*from_halo_pc counts
them), else found in Custom Edition's bitmaps.map, and the tag's pixels read
from there (*type). A bitmap
of its path there of another size or format (a bitmaps.map not as Custom
Edition shipped it: an HD or repacked one) is drawn as that one is (*resized);
one not there at all is stood in for (*stood_in) */
static boolean ce_retail_bitmaps(
	struct ce_image *image,
	struct ce_tag_instance const *instance,
	byte *type,
	long *stood_in,
	long *resized,
	long *renamed,
	long *from_halo_pc)
{
	byte *group = ce_image_pointer(image, instance->base_address, CE_BITMAP_GROUP_SIZE);
	char const *name = ce_image_tag_name(image, instance);
	struct ce_resource_map *map = &ce_resource_maps[_ce_resource_bitmaps];
	byte *bitmaps, *theirs, *at_offset = NULL;
	long count, their_count = 0, index, found, at_offset_resource = NONE, at_offset_count = 0;
	boolean external = FALSE;

	/* (ce_bitmaps_check refuses a bitmap whose blocks are not in the tags;
	one that lies over the tag header or tag index is let be, as it is when
	the map loads: ce_map_checks.h) */
	if (!group || !ce_image_block(image, group + CE_BITMAP_GROUP_BITMAPS_OFFSET, CE_BITMAP_DATA_SIZE,
		CE_MAXIMUM_ELEMENTS, name, &count, &bitmaps) || !ce_tags_writable(group + 0x06, 2) ||
		!ce_tags_writable(bitmaps, (unsigned long)count * CE_BITMAP_DATA_SIZE))
	{
		return TRUE;
	}
	for (index = 0; index < count; index++)
		external |= (*(unsigned short *)(bitmaps + index * CE_BITMAP_DATA_SIZE + 0x0e) & CE_BITMAP_EXTERNAL_FLAG) != 0;
	if (!external)
		return TRUE;
	/* (as Halo PC read them, from its own bitmaps.map: nothing in the tag
	changed) */
	if (ce_halo_pc_bitmaps(bitmaps, count))
	{
		*from_halo_pc += count;
		*type = (byte)(_ce_resource_halo_pc_bitmaps + 1);
		return TRUE;
	}
	if (!ce_resource_map_open(_ce_resource_bitmaps))
		return FALSE;
	found = ce_retail_resource(map, name);
	theirs = found != NONE ? ce_retail_resource_bitmaps(map, found, &their_count) : NULL;
	for (index = 0; index < count; index++)
	{
		byte *data = bitmaps + index * CE_BITMAP_DATA_SIZE;
		byte const *their = theirs && index < their_count ? theirs + index * CE_BITMAP_DATA_SIZE : NULL;
		long resource, their_index = 0;

		if (!(*(unsigned short *)(data + 0x0e) & CE_BITMAP_EXTERNAL_FLAG))
			continue;
		/* (the bitmap its pixels' offset in Halo PC's bitmaps.map names,
		before its tag's path's: a renamed stock tag's) */
		resource = ce_retail_resource_at(map, ce_read_long(data + 0x18), &their_index);
		if (resource != NONE)
		{
			if (resource != at_offset_resource)
			{
				if (at_offset)
					free(at_offset);
				at_offset = ce_retail_resource_bitmaps(map, resource, &at_offset_count);
				at_offset_resource = resource;
			}
			if (at_offset && their_index < at_offset_count)
			{
				if (resource != found)
					(*renamed)++;
				their = at_offset + their_index * CE_BITMAP_DATA_SIZE;
			}
		}
		if (their && !memcmp(data + 0x04, their + 0x04, 10) && !memcmp(data + 0x14, their + 0x14, 2) &&
			!memcmp(data + 0x1c, their + 0x1c, 4))
		{
			memcpy(data + 0x18, their + 0x18, 4);
		}
		/* (the same bitmap, of its type, at another size or format: its own
		pixels rather than another bitmap's) */
		else if (their && !memcmp(data + 0x0a, their + 0x0a, 2))
		{
			if (!ce_map_checking() && *resized < 8)
			{
				error(_error_silent, "HaloMD map: bitmap %ld of %s is another size or format in bitmaps.map "
					"(%dx%d format %d there, %dx%d format %d in the map)", index, name, *(short const *)(their + 0x04),
					*(short const *)(their + 0x06), *(short const *)(their + 0x0c), *(short const *)(data + 0x04),
					*(short const *)(data + 0x06), *(short const *)(data + 0x0c));
			}
			ce_retail_bitmap_take(data, their);
			(*resized)++;
		}
		else if (ce_retail_bitmap_stand_in(map, data))
		{
			/* (another bitmap's pixels: what the weather leaves out) */
			*(unsigned short *)(group + 0x06) |= (unsigned short)FLAG(_bitmap_group_port_stood_in_bit);
			if (!ce_map_checking())
			{
				if (*stood_in < 16)
					error(_error_silent, "HaloMD map: bitmap %ld of %s is not in bitmaps.map: stood in for", index, name);
				/* (an OpenSauce map's log counts every one: opensauce_survey.c) */
				opensauce_survey_note_resource_miss("bitmap (not in bitmaps.map)", name);
			}
			(*stood_in)++;
		}
		else
		{
			if (theirs)
				free(theirs);
			if (at_offset)
				free(at_offset);
			return ce_refuse("bitmap %ld of %s is in Halo PC's bitmaps.map, not in Custom Edition's", index, name);
		}
	}
	if (theirs)
		free(theirs);
	if (at_offset)
		free(at_offset);
	/* (its header and bitmaps, as they are now: ce_resources_place) */
	ce_resources_placed(image, group, CE_BITMAP_GROUP_SIZE);
	ce_resources_placed(image, bitmaps, (unsigned long)count * CE_BITMAP_DATA_SIZE);
	*type = (byte)(_ce_resource_bitmaps + 1);
	return TRUE;
}

/* a permutation's samples in sounds.map (Halo PC's flag on them), not in
the map: Halo PC read each permutation's samples from the file its flag
names, whatever the map's version or whether its tag is indexed */
static boolean ce_samples_external(
	byte const *permutation)
{
	return (ce_read_long(permutation + CE_PERMUTATION_SAMPLES_OFFSET + 4) & CE_SAMPLES_EXTERNAL_FLAG) != 0;
}

/* a retail map's sound tag: its external permutations' samples read from
Halo PC's own sounds.map, if it is there and has them all (*from_halo_pc
counts the sound), else found in Custom Edition's sounds.map, and the tag's
samples read from there (*type); or, if they are not all there, of the same
sizes, the sound made silent */
static boolean ce_retail_sound(
	struct ce_image *image,
	struct ce_tag_instance const *instance,
	byte *type,
	long *silenced,
	long *from_halo_pc)
{
	byte *sound = ce_image_pointer(image, instance->base_address, CE_SOUND_HEADER_SIZE);
	char const *name = ce_image_tag_name(image, instance);
	struct ce_resource_map *map = &ce_resource_maps[_ce_resource_sounds];
	byte *ranges, *copy = NULL;
	long range_count, range_index, found;
	unsigned long size = 0, cursor, their_range_count = 0;
	boolean external = FALSE, matches;

	/* (one that lies over the tag header or tag index is let be, as it is
	when the map loads: ce_map_checks.h) */
	if (!sound || !ce_image_block(image, sound + CE_SOUND_PITCH_RANGES_OFFSET, CE_SOUND_PITCH_RANGE_SIZE,
		CE_MAXIMUM_ELEMENTS, name, &range_count, &ranges) || !ce_tags_writable(sound + CE_SOUND_PITCH_RANGES_OFFSET, 8))
	{
		return TRUE;
	}
	for (range_index = 0; range_index < range_count; range_index++)
	{
		byte *permutations;
		long permutation_count, permutation_index;

		if (!ce_image_block(image, ranges + range_index * CE_SOUND_PITCH_RANGE_SIZE + CE_PITCH_RANGE_PERMUTATIONS_OFFSET,
			CE_SOUND_PERMUTATION_SIZE, CE_MAXIMUM_ELEMENTS, name, &permutation_count, &permutations) ||
			!ce_tags_writable(permutations, (unsigned long)permutation_count * CE_SOUND_PERMUTATION_SIZE))
		{
			return TRUE;
		}
		for (permutation_index = 0; permutation_index < permutation_count; permutation_index++)
			external |= ce_samples_external(permutations + permutation_index * CE_SOUND_PERMUTATION_SIZE);
	}
	if (!external)
		return TRUE;
	/* (as Halo PC read them, from its own sounds.map: nothing in the tag
	changed) */
	if (ce_halo_pc_sound(image, sound, ranges, range_count, name))
	{
		(*from_halo_pc)++;
		*type = (byte)(_ce_resource_halo_pc_sounds + 1);
		return TRUE;
	}
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
	/* (its header, pitch ranges and permutations, as they are now:
	ce_resources_place) */
	ce_resources_placed(image, sound, CE_SOUND_HEADER_SIZE);
	if (ce_image_block(image, sound + CE_SOUND_PITCH_RANGES_OFFSET, CE_SOUND_PITCH_RANGE_SIZE, CE_MAXIMUM_ELEMENTS,
		name, &range_count, &ranges))
	{
		ce_resources_placed(image, ranges, (unsigned long)range_count * CE_SOUND_PITCH_RANGE_SIZE);
		for (range_index = 0; range_index < range_count; range_index++)
		{
			byte *permutations;
			long permutation_count;

			if (ce_image_block(image, ranges + range_index * CE_SOUND_PITCH_RANGE_SIZE +
				CE_PITCH_RANGE_PERMUTATIONS_OFFSET, CE_SOUND_PERMUTATION_SIZE, CE_MAXIMUM_ELEMENTS, name,
				&permutation_count, &permutations))
			{
				ce_resources_placed(image, permutations, (unsigned long)permutation_count * CE_SOUND_PERMUTATION_SIZE);
			}
		}
	}
	return TRUE;
}

/* a Custom Edition map's own sound tag (not indexed): its permutations'
samples in the map, or, those flagged so, in sounds.map at the offsets they
give (ce_samples_external): the tools that rebuilt maps pointed a sound's
permutations at sounds.map when it already held the same samples (Super
Campaign Refined's c10: 912 permutations of 250 of its sounds, ADPCM and Ogg
Vorbis, which were read from the map at sounds.map's offsets and played as
loud noise). A sound whose samples are
all in sounds.map is read from there (*type), as an indexed sound's are; one
with some in each file is copied whole into the decoded sounds' file, each
permutation from its own (ce_sounds_decode). FALSE (refused, saying why) if
sounds.map is needed and cannot be opened */
static boolean ce_custom_sound(
	struct ce_image *image,
	struct ce_tag_instance const *instance,
	byte *type)
{
	byte *sound = ce_image_pointer(image, instance->base_address, CE_SOUND_HEADER_SIZE);
	char const *name = ce_image_tag_name(image, instance);
	byte *ranges;
	long range_count, range_index;
	long external = 0, internal = 0;

	/* (a sound whose blocks are not in the tags is refused when it is
	checked: ce_sound_check) */
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
		{
			byte const *permutation = permutations + permutation_index * CE_SOUND_PERMUTATION_SIZE;

			if (ce_samples_external(permutation))
				external++;
			else if ((long)ce_read_long(permutation + CE_PERMUTATION_SAMPLES_OFFSET) > 0)
				internal++;
		}
	}
	if (!external)
		return TRUE;
	if (!ce_resource_map_open(_ce_resource_sounds))
		return FALSE;
	if (!internal)
		*type = (byte)(_ce_resource_sounds + 1);
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
static boolean ce_resources_place_read(
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
	long stood_in = 0, silenced = 0, resized = 0, reindexed = 0, renamed = 0;
	long halo_pc_bitmaps = 0, halo_pc_sounds = 0;

	*copied = 0;
	csmemset(types, 0, tag_count);
	/* (Halo PC's own resource maps looked for again, if they weren't found:
	a player may have added them since) */
	ce_halo_pc_looked[0] = ce_halo_pc_looked[1] = FALSE;
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		short type = ce_resource_type(instance->group_tag);
		char const *name = ce_image_tag_name(image, instance);
		struct ce_resource_map *map;
		struct content_resource_entry *resource;
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
					found = ce_retail_bitmaps(image, instance, &types[index], &stood_in, &resized, &renamed,
						&halo_pc_bitmaps);
				else if (instance->group_tag == 'snd!')
					found = ce_retail_sound(image, instance, &types[index], &silenced, &halo_pc_sounds);
				if (!found)
					return FALSE;
			}
			/* (a Custom Edition map's own sounds whose samples are in
			sounds.map) */
			else if (instance->group_tag == 'snd!' && !ce_custom_sound(image, instance, &types[index]))
				return FALSE;
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
		same: the resource after their pixels. A resource map whose
		resources are in another order than the one the map was built
		with, a repacked one, has the tag's resource by its path) */
		else if (map->paths && instance->base_address < map->count &&
			(map->resources[instance->base_address].path_offset >= map->paths_size ||
			_stricmp(map->paths + map->resources[instance->base_address].path_offset, name)))
		{
			long found = ce_resource_by_path(map, name);

			if (found == NONE)
			{
				ce_refusal_explain("Its %s.map doesn't match: copy Custom Edition's own into maps_ce.",
					ce_resource_map_names[type]);
				return ce_refuse("%s is not in %s.map (its resource %lu there is another's)", name,
					ce_resource_map_names[type], instance->base_address);
			}
			if (!ce_map_checking() && reindexed < 8)
			{
				error(_error_silent, "Custom Edition map: %s's resource %lu in %s.map is another's: %ld, by its path", name,
					instance->base_address, ce_resource_map_names[type], found);
			}
			reindexed++;
			instance->base_address = (unsigned long)found;
		}
		if (instance->base_address >= map->count)
		{
			return ce_refuse("%s's resource %lu is not in %s.map (%lu)", name, instance->base_address,
				ce_resource_map_names[type], map->count);
		}
		resource = &map->resources[instance->base_address];
		if (resource->size < ce_resource_minimum_size(instance->group_tag) ||
			!ce_range_within(resource->offset, resource->size, map->file_size))
		{
			return ce_refuse("%s's resource (%lu bytes at %lu) is not in %s.map", name, (unsigned long)resource->size,
				(unsigned long)resource->offset, ce_resource_map_names[type]);
		}
		if (next > end_free || resource->size > end_free - next)
		{
			ce_refusal_explain_room();
			return ce_refuse("there is no room in the tag cache for %s's resource", name);
		}
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
		error(_error_silent, "HaloMD map: %ld bitmaps and %ld sounds read from Halo PC's own resource maps; "
			"%ld bitmaps stood in for, %ld sounds silent (not in Custom Edition's resource maps), %ld bitmaps of "
			"another size or format in bitmaps.map, %ld found by their pixels' offset under another tag's name",
			halo_pc_bitmaps, halo_pc_sounds, stood_in, silenced, resized, renamed);
		ce_halo_pc_missing_tell(stood_in, silenced);
		/* (Custom Edition's own bitmaps.map has every stock bitmap at the
		size Halo PC's maps expect: one that doesn't is another file) */
		if (resized)
		{
			char path[256];

			map_family_resource("bitmaps", path, sizeof(path));
			error(_error_silent, "HaloMD map: %s isn't the bitmaps.map Custom Edition shipped (an HD or repacked one?): "
				"its %ld bitmaps of another size are drawn as they are there", path, resized);
		}
		/* (a map's own few bitmaps aren't in it, as HaloMD's own bitmaps.map
		had them; many is a bitmaps.map without Custom Edition's stock ones) */
		if (stood_in > 16)
		{
			char path[256];

			map_family_resource("bitmaps", path, sizeof(path));
			error(_error_silent, "HaloMD map: %s lacks %ld of the map's bitmaps: is it Custom Edition's own?", path,
				stood_in);
		}
	}
	if (reindexed && !ce_map_checking())
	{
		error(_error_silent, "Custom Edition map: %ld resources found by their paths: a resource map isn't in the order "
			"Custom Edition's own is (a repacked one?)", reindexed);
	}
	*next_free = next;
	return TRUE;
}

/* the resources copied in after a map's tags, hashed (size bytes at the
Xbox address start, in the image) */
static void ce_resources_placed_copies(
	struct ce_image const *image,
	struct content_sha256 *written,
	unsigned long start,
	unsigned long size)
{
	byte const *at = image->data + (start - image->base);

	while (size)
	{
		unsigned long chunk = size < 0x40000000UL ? size : 0x40000000UL;

		content_sha256_add(written, at, chunk);
		at += chunk;
		size -= chunk;
	}
}

/* ce_resources_place_read, and what it wrote hashed (digest): the resource
maps' tags copied in after the map's (relocated), each tag's address and
resource map after, and what it changed in the map's own tags (a retail
map's bitmaps and sounds: ce_resources_placed). The map's tags are hashed
once, as they are read (ce_map_checks.c): with them the same, and these,
the tags with the resources copied in are the same, and are not hashed
again whole. With no digest, nothing is hashed (the original Xbox's
check and load: CE_MAP_HASHED) */
static boolean ce_resources_place(
	struct ce_image *image,
	void *tag_instances,
	long tag_count,
	unsigned long end_free,
	byte *types,
	unsigned long *next_free,
	long *copied,
	byte digest[CE_DIGEST_SIZE])
{
	unsigned long copies = ((image->base + image->size) + 15) & ~15UL;
	struct content_sha256 written;
	boolean placed;
	long index;

	if (!digest)
		return ce_resources_place_read(image, tag_instances, tag_count, end_free, types, next_free, copied);
	content_sha256_begin(&written);
	ce_resources_written = &written;
	placed = ce_resources_place_read(image, tag_instances, tag_count, end_free, types, next_free, copied);
	ce_resources_written = NULL;
	if (!placed)
		return FALSE;
	/* (the copies, after the map's tags) */
	if (*next_free > copies)
		ce_resources_placed_copies(image, &written, copies, *next_free - copies);
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance const *instance = (struct ce_tag_instance const *)((byte const *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		unsigned long placement[2];

		placement[0] = instance->base_address;
		placement[1] = types[index];
		content_sha256_add(&written, placement, sizeof(placement));
	}
	{
		unsigned long end[2];

		end[0] = *next_free;
		end[1] = (unsigned long)*copied;
		content_sha256_add(&written, end, sizeof(end));
	}
	content_sha256_end(&written, digest);
	return TRUE;
}

/* a tag's bitmaps: each one a bitmap the game's renderer can make a texture
of (the game's own bitmap_verify, of the Xbox's flags, a format with a
Direct3D one, square cube maps) and its pixels in the file they are read
from (bitmaps.map's size if they are there, Halo PC's own bitmaps.map's if
they are read from that, else the map's; type is the tag's resource map, as
ce_resources_place found it, +1, or 0) */
static boolean ce_bitmaps_check(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	byte type,
	unsigned long map_file_size)
{
	boolean indexed = type != 0;
	short file_type = type == _ce_resource_halo_pc_bitmaps + 1 ? _ce_resource_halo_pc_bitmaps : _ce_resource_bitmaps;
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

		external = indexed || (*(unsigned short const *)(bitmaps + index * CE_BITMAP_DATA_SIZE + 0x0e) &
			CE_BITMAP_EXTERNAL_FLAG);
		switch (ce_bitmap_drawable(bitmaps + index * CE_BITMAP_DATA_SIZE, &bitmap))
		{
		case _ce_bitmap_not_drawn:
			return ce_refuse("bitmap %ld of %s (%dx%dx%d, type %d, format %d) is not one the game draws", index, name,
				bitmap.width, bitmap.height, bitmap.depth, bitmap.type, bitmap.format);
		case _ce_bitmap_no_format:
			return ce_refuse("bitmap %ld of %s has no format the game draws (%d)", index, name, bitmap.format);
		case _ce_bitmap_linear:
			return ce_refuse("bitmap %ld of %s is linear, of a format that cannot be", index, name);
		case _ce_bitmap_misshapen:
			return ce_refuse("bitmap %ld of %s is %dx%dx%d, not of its type's shape", index, name, bitmap.width,
				bitmap.height, bitmap.depth);
		}
		if (external && file_type == _ce_resource_bitmaps && !ce_resource_map_open(_ce_resource_bitmaps))
			return FALSE;
		file_size = external ? ce_resource_maps[file_type].file_size : map_file_size;
		if (bitmap.pixels_offset < 0 || bitmap.pixels_size < 0 ||
			!ce_range_within((unsigned long)bitmap.pixels_offset, (unsigned long)bitmap.pixels_size, file_size))
		{
			return ce_refuse("bitmap %ld of %s: its pixels (%ld bytes at %ld) are not in %s", index, name,
				bitmap.pixels_size, bitmap.pixels_offset, external ? (file_type == _ce_resource_bitmaps ? "bitmaps.map" :
				"Halo PC's bitmaps.map") : "the map");
		}
	}
	return TRUE;
}

/* a sound's pitch ranges and permutations: all in the tags, each played
permutation one of its range's, and each permutation's samples in the file
they are read from (sounds.map's for an indexed sound's and for those
flagged in it, ce_samples_external, or Halo PC's own sounds.map's for a
retail map's sound read from that; else the map's), its mouth data and
subtitles in the tags (type: as ce_bitmaps_check's) */
static boolean ce_sound_check(
	struct ce_image const *image,
	struct ce_tag_instance const *instance,
	byte type,
	unsigned long map_file_size)
{
	boolean indexed = type != 0;
	short file_type = type == _ce_resource_halo_pc_sounds + 1 ? _ce_resource_halo_pc_sounds : _ce_resource_sounds;

	byte *sound = ce_image_pointer(image, instance->base_address, CE_SOUND_HEADER_SIZE);
	char const *name = ce_image_tag_name(image, instance);
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
		if (actual_count < 0 || actual_count > permutation_count)
		{
			return ce_refuse("sound %s plays %d of its %ld permutations", name, actual_count, permutation_count);
		}
		for (permutation_index = 0; permutation_index < permutation_count; permutation_index++)
		{
			byte *permutation = permutations + permutation_index * CE_SOUND_PERMUTATION_SIZE;
			long samples_size = (long)ce_read_long(permutation + CE_PERMUTATION_SAMPLES_OFFSET);
			long samples_offset = (long)ce_read_long(permutation + CE_PERMUTATION_SAMPLES_OFFSET + 8);
			boolean external = indexed || ce_samples_external(permutation);
			unsigned long file_size;
			short next;
			unsigned long ignored_size;
			byte *ignored;

			memcpy(&next, permutation + CE_PERMUTATION_NEXT_OFFSET, sizeof(next));
			if (next != NONE && (next < 0 || next >= permutation_count))
				return ce_refuse("sound %s: a permutation follows on with permutation %d", name, next);
			if (external && file_type == _ce_resource_sounds && !ce_resource_map_open(_ce_resource_sounds))
				return FALSE;
			file_size = external ? ce_resource_maps[file_type].file_size : map_file_size;
			if (samples_size < 0 || samples_offset < 0 ||
				!ce_range_within((unsigned long)samples_offset, (unsigned long)samples_size, file_size))
			{
				return ce_refuse("sound %s: samples (%ld bytes at %ld) not in %s", name, samples_size, samples_offset,
					external ? (file_type == _ce_resource_sounds ? "sounds.map" : "Halo PC's sounds.map") : "the map");
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
	byte digest[CE_DIGEST_SIZE];
	long copied, index;
	boolean valid;

	if (!types)
		return ce_refuse("no memory to check its resources");
	valid = ce_resources_place(image, tag_instances, tag_count, end_free, types, next_free, &copied,
		CE_MAP_HASHED ? digest : NULL);
	/* (what the tags had copied in, and from where: as loading must find
	it, ce_resources_tags_loaded) */
	if (valid && CE_MAP_HASHED)
		ce_resources_digest(digest);
	for (index = 0; valid && index < tag_count; index++)
	{
		struct ce_tag_instance const *instance = (struct ce_tag_instance const *)((byte const *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);

		if (instance->group_tag == 'bitm')
			valid = ce_bitmaps_check(image, instance, types[index], map_file_size);
		else if (instance->group_tag == 'snd!')
			valid = ce_sound_check(image, instance, types[index], map_file_size);
	}
	if (types)
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
	byte digest[CE_DIGEST_SIZE];
	unsigned long next;
	long copied = 0;
	long index;

	system_free(ce_indexed_tags);
	ce_indexed_tags = system_malloc(tag_count);
	ce_indexed_tag_count = ce_indexed_tags ? tag_count : 0;
	ce_free_next = ce_free_end = 0;
	ce_room_next = 0;
	ce_tags_room_used = 0;
	/* (the map's own tags, until its resource maps' are copied in after
	them: what the conversions read is in these, ce_loaded_pointer; and no
	blocks placed past them yet, the last map's let go) */
	ce_loaded_tags_end(0);
	ce_loaded_tags_end(first_free);
	if (!ce_indexed_tags)
		return FALSE;
	image.data = xbox_pointer(CE_IMAGE_TAG_CACHE_BASE);
	image.base = CE_IMAGE_TAG_CACHE_BASE;
	image.size = first_free - CE_IMAGE_TAG_CACHE_BASE;
	/* (writing nothing over the tag header or tag index, as when the map
	was checked: ce_map_checks.h) */
	ce_tags_guard_begin_loaded(tag_instances, tag_count);
	/* (and only as they were when the map was checked: a resource map
	changed since then is refused, ce_map_checks.c) */
	if (!ce_map_loaded_resources_unchanged(
		ce_resources_place(&image, tag_instances, tag_count, end_free, ce_indexed_tags, &next, &copied,
			CE_MAP_HASHED ? digest : NULL) ? digest : NULL))
	{
		ce_tags_guard_end();
		return FALSE;
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
		byte *bitmap_group, *elements;
		long count, bitmap;

		if (instance->group_tag != 'bitm')
			continue;
		bitmap_group = ce_image_pointer(&image, instance->base_address, CE_BITMAP_GROUP_SIZE);
		if (!bitmap_group || !ce_image_block(&image, bitmap_group + CE_BITMAP_GROUP_BITMAPS_OFFSET,
			CE_BITMAP_DATA_SIZE, CE_MAXIMUM_ELEMENTS, "bitmaps", &count, &elements) ||
			!ce_tags_writable(elements, (unsigned long)count * CE_BITMAP_DATA_SIZE))
		{
			continue;
		}
		for (bitmap = 0; bitmap < count; bitmap++)
		{
			byte *data = elements + bitmap * CE_BITMAP_DATA_SIZE;

			/* (a tag's bitmaps are all external or none are: its pixels are
			read from bitmaps.map, as an indexed tag's are, or from Halo PC's
			own, as placing them found: ce_retail_bitmaps) */
			if ((*(unsigned short *)(data + 0x0e) & CE_BITMAP_EXTERNAL_FLAG) && !ce_indexed_tags[index])
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
	ce_loaded_tags_end(next);
	ce_sounds_decode(tag_instances, tag_count);
	ce_tags_guard_end();
	return TRUE;
}

/* a loaded sound's pitch ranges, or a pitch range's permutations (the block
at field, each element_size bytes): in the tag cache (ce_sound_blocks_loaded
saw that they are), else none */
static byte *ce_sound_block(
	byte const *field,
	unsigned long element_size,
	unsigned long *count)
{
	byte *elements;

	*count = ce_read_long(field);
	elements = *count && *count <= CE_MAXIMUM_ELEMENTS ?
		ce_loaded_pointer(ce_read_long(field + 4), *count * element_size) : NULL;
	if (!elements)
		*count = 0;
	return elements;
}

/* whether a loaded sound's header, pitch ranges and permutations, which
ce_sounds_decode reads and writes, are all in the tag cache and none over its
tag header or tag index (ce_map_checks.h): they were checked, but another
tag's conversion may have written over them since */
static boolean ce_sound_blocks_loaded(
	byte const *sound)
{
	unsigned long range_count = ce_read_long(sound + CE_SOUND_PITCH_RANGES_OFFSET);
	unsigned long range_address = ce_read_long(sound + CE_SOUND_PITCH_RANGES_OFFSET + 4);
	byte *ranges;
	unsigned long range;

	if (!ce_tags_writable(sound, CE_SOUND_HEADER_SIZE))
		return FALSE;
	if (!range_count)
		return TRUE;
	ranges = range_count <= CE_MAXIMUM_ELEMENTS ?
		ce_loaded_pointer(range_address, range_count * CE_SOUND_PITCH_RANGE_SIZE) : NULL;
	if (!ranges)
	{
		ce_contained("a sound's %lu pitch ranges at %08lx are outside its tag cache", range_count, range_address);
		return FALSE;
	}
	for (range = 0; range < range_count; range++)
	{
		byte const *field = ranges + range * CE_SOUND_PITCH_RANGE_SIZE + CE_PITCH_RANGE_PERMUTATIONS_OFFSET;
		unsigned long count = ce_read_long(field);
		unsigned long address = ce_read_long(field + 4);
		byte *permutations;

		if (!count)
			continue;
		permutations = count <= CE_MAXIMUM_ELEMENTS ?
			ce_loaded_pointer(address, count * CE_SOUND_PERMUTATION_SIZE) : NULL;
		if (!permutations)
		{
			ce_contained("a sound's %lu permutations at %08lx are outside its tag cache", count, address);
			return FALSE;
		}
		if (!ce_tags_writable(permutations, count * CE_SOUND_PERMUTATION_SIZE))
			return FALSE;
	}
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
	unsigned long pitch_range_count;
	byte *ranges = ce_sound_block(sound + CE_SOUND_PITCH_RANGES_OFFSET, CE_SOUND_PITCH_RANGE_SIZE, &pitch_range_count);
	short compression = NONE;
	unsigned long pitch_range;

	for (pitch_range = 0; pitch_range < pitch_range_count; pitch_range++)
	{
		byte *range = ranges + pitch_range * CE_SOUND_PITCH_RANGE_SIZE;
		unsigned long permutation_count;
		byte *permutations = ce_sound_block(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET, CE_SOUND_PERMUTATION_SIZE,
			&permutation_count);
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

/* a permutation decoded, before it is changed to read its samples from them:
its pitch range and index there, and where its samples are from the start
of its sound's */
struct ce_decoded_permutation
{
	unsigned long range;
	unsigned long index;
	unsigned long offset;
	unsigned long size;
};

/* where a sound's decoded samples are written: the decoded sounds' file, at
position (on a console, at the file's own position, as it always was); with
no file, they are only counted */
struct ce_sound_writer
{
	HANDLE file;
	unsigned long position;
};

/* what a sound is decoded from, kept so that it can be decoded again
(desktop: ce_sound_entry_load): its pitch ranges and their permutations as
they were before they were changed to read the decoded samples (copies, on
the desktop; on a console the tags' own blocks, decoded once) */
struct ce_sound_job
{
	struct ce_tag_instance const *instance;
	HANDLE sounds_map;
	/* (an indexed tag's: all its samples in sounds_map) */
	boolean indexed;
	boolean ogg;
	boolean is_looped;
	short encoding;
	int played_rate;
	unsigned long range_count;
	byte *ranges;
	byte **permutations;
	unsigned long *permutation_counts;
};

/* what decoding a map's sounds did, for its log lines */
struct ce_sound_decode_counts
{
	long resampled;
	long resampled_joined;
	long resampled_looped;
	long streamed;
	boolean resampled_told;
};

enum
{
	_ce_sound_decoded,
	/* (no room left for it in the file) */
	_ce_sound_full,
	_ce_sound_failed
};

static boolean ce_sound_write(
	struct ce_sound_writer *writer,
	void const *data,
	unsigned long bytes)
{
	unsigned long bytes_written = 0;

	if (!writer->file)
	{
		writer->position += bytes;
		return TRUE;
	}
#ifdef HALO_CONSOLE
	if (!WriteFile(writer->file, data, bytes, &bytes_written, NULL) || bytes_written != bytes)
		return FALSE;
#else
	{
		/* (at its place: the cache's thread reads the same file while a
		sound is decoded again, ce_sound_entry_load) */
		OVERLAPPED overlapped;

		memset(&overlapped, 0, sizeof(overlapped));
		overlapped.Offset = writer->position;
		if (!WriteFile(writer->file, data, bytes, &bytes_written, &overlapped) || bytes_written != bytes)
			return FALSE;
	}
#endif
	writer->position += bytes;
	return TRUE;
}

/* the frames of its neighbors a permutation brought to another rate is
filtered with (ce_vorbis_resample_piece: 47 halving, 24 doubling) */
#define CE_RESAMPLE_CONTEXT 48

/* long sounds (desktop): an Ogg Vorbis permutation of more compressed bytes
than this is decoded a piece at a time, each piece written out as it is
decoded (ce_sound_stream), not held whole; one whose samples come to more
than the sound cache holds (the Xbox's 4 MB: the most a permutation's
samples may be, tag_schema_effects.c) is split into permutations played
one after another (ce_sound_split), as Halo's tools split a long sound,
each a chunk of no more than CE_SOUND_CHUNK_MOST bytes (at least
CE_SOUND_CHUNK_SIZE, more where a pitch range would have more permutations
than the game's 256). A permutation is decoded to no more than
CE_SOUND_STREAM_MOST bytes (an hour of 44.1 kHz stereo is 635 MB) */
#define CE_SOUND_STREAM_OGG_BYTES 0x40000
#define CE_SOUND_STREAM_PIECE_FRAMES 0x10000
#define CE_SOUND_SPLIT_SIZE HALO_PORT_XBOX_SOUND_CACHE_SIZE /* (halo_port_capacity.h) */
#define CE_SOUND_CHUNK_SIZE 0x60000
#define CE_SOUND_CHUNK_MOST 0x100000
#define CE_SOUND_STREAM_MOST 0x10000000
#define CE_SOUND_MAXIMUM_PERMUTATIONS 256
#define CE_SOUND_LINKED_PERMUTATIONS_FLAG 2
/* (sound_definitions.h's SOUND_DEFINITION_SPLIT_BY_LOADER_FLAG: played
through as an impulse sound too, sound_manager.c) */
#define CE_SOUND_SPLIT_BY_LOADER_FLAG 0x80000000UL
/* (the tag validator's most bytes of copies, MAXIMUM_COPIED_BYTES in
tag_validate.c: kept free of long sounds' permutations,
ce_resources_allocate_split) */
#define CE_TAG_COPIES_RESERVE 0x400000UL
#define CE_PERMUTATION_BUFFER_SIZE_OFFSET 0x38

/* (the silence a sound that is not looped is played from and into) */
static short const ce_resample_silence[CE_RESAMPLE_CONTEXT * 2] = { 0 };
/* a looping sound's (lsnd) tracks: their size, and their loop and
alternate loop sounds (sound_definitions.h, struct looping_sound_track) */
#define CE_LOOPING_SOUND_TRACKS_OFFSET 0x3c
#define CE_LOOPING_SOUND_TRACK_SIZE 0xa0
#define CE_LOOPING_SOUND_TRACK_LOOP_OFFSET 0x40
#define CE_LOOPING_SOUND_TRACK_ALTERNATE_LOOP_OFFSET 0x80
/* a pitch range's count of permutations a sound picks from (the others
are their continuations) */
#define CE_PITCH_RANGE_ACTUAL_PERMUTATION_COUNT_OFFSET 0x2c

/* a permutation's first and last frames as decoded, before it is brought
to its sound's rate: its neighbors' context (a sound split into
permutations played one after another, next_permutation_index). Kept as
each is decoded; a head not decoded yet is read alone (its first frames),
and a tail only where a looped sound's pieces play round in a ring */
struct ce_permutation_edges
{
	/* (its head read, and its tail; frames, once it is known) */
	boolean head_ready;
	boolean tail_ready;
	int frames;
	int channels;
	int rate;
	short head[CE_RESAMPLE_CONTEXT * 2];
	short tail[CE_RESAMPLE_CONTEXT * 2];
	int head_frames;
	int tail_frames;
};

/* the sound tags (their tag indices) that looping sounds loop: malloc'd, or
NULL */
static unsigned long *ce_looped_sounds(void *tag_instances, long tag_count, long *count)
{
	unsigned long *looped = NULL;
	long capacity = 0, index;

	*count = 0;
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		byte *looping, *tracks;
		unsigned long track_count, track;

		if (instance->group_tag != 'lsnd')
			continue;
		looping = ce_loaded_pointer(instance->base_address, CE_LOOPING_SOUND_TRACKS_OFFSET + 12);
		if (!looping)
			continue;
		tracks = ce_sound_block(looping + CE_LOOPING_SOUND_TRACKS_OFFSET, CE_LOOPING_SOUND_TRACK_SIZE, &track_count);
		for (track = 0; tracks && track < track_count; track++)
		{
			static unsigned long const offsets[2] = { CE_LOOPING_SOUND_TRACK_LOOP_OFFSET,
				CE_LOOPING_SOUND_TRACK_ALTERNATE_LOOP_OFFSET };
			int reference;

			for (reference = 0; reference < 2; reference++)
			{
				byte *at = tracks + track * CE_LOOPING_SOUND_TRACK_SIZE + offsets[reference];

				if (ce_read_long(at) != 'snd!' || ce_read_long(at + 12) == 0xffffffffUL)
					continue;
				if (*count == capacity)
				{
					long larger = capacity ? capacity * 2 : 16;
					unsigned long *grown = realloc(looped, (size_t)larger * sizeof(*looped));

					if (!grown)
						return looped;
					looped = grown;
					capacity = larger;
				}
				looped[(*count)++] = ce_read_long(at + 12);
			}
		}
	}
	return looped;
}

/* a permutation's head read alone (and with `whole`, its tail: the whole
permutation decoded, a frame at a time), into `edge` */
static void ce_permutation_edge_read(byte *at, HANDLE sounds_map, HANDLE map_file, boolean from_sounds_map,
	boolean whole, struct ce_permutation_edges *edge)
{
	long size = *(long *)(at + CE_PERMUTATION_SAMPLES_OFFSET);
	unsigned long offset = *(unsigned long *)(at + CE_PERMUTATION_SAMPLES_OFFSET + 8);
	HANDLE source = from_sounds_map || ce_samples_external(at) ? sounds_map : map_file;
	unsigned char *data;
	int frames = -1;

	memset(edge, 0, sizeof(*edge));
	/* (tried once) */
	edge->head_ready = TRUE;
	edge->tail_ready = whole;
	if (size <= 0 || !source || *(short *)(at + CE_PERMUTATION_COMPRESSION_OFFSET) != CE_SOUND_COMPRESSION_OGG)
		return;
	data = malloc((size_t)size);
	if (data && ce_read(source, offset, data, (unsigned long)size))
	{
		frames = ce_vorbis_edges(data, size, CE_RESAMPLE_CONTEXT, whole, &edge->channels, &edge->rate, edge->head,
			&edge->head_frames, edge->tail, &edge->tail_frames);
	}
	if (data)
		free(data);
	if (frames <= 0 || edge->channels < 1 || edge->channels > 2)
	{
		edge->channels = edge->rate = edge->head_frames = edge->tail_frames = 0;
		return;
	}
	if (whole)
		edge->frames = frames;
}

/* the order a pitch range's permutations are decoded in: each after the
one played before it, so its tail is kept for the next (the rest, a ring,
as they come) */
static void ce_permutation_order(byte *permutations, unsigned long permutation_count, unsigned long *order)
{
	unsigned long count = 0, permutation;
	/* (the game's allocator: no calloc, and free takes no NULL) */
	byte *placed = malloc(permutation_count ? permutation_count : 1);

	if (placed)
		memset(placed, 0, permutation_count ? permutation_count : 1);
	if (!placed)
	{
		for (permutation = 0; permutation < permutation_count; permutation++)
			order[permutation] = permutation;
		return;
	}
	for (permutation = 0; permutation < permutation_count; permutation++)
	{
		unsigned long other;
		boolean has_before = FALSE;
		long next;

		for (other = 0; other < permutation_count && !has_before; other++)
		{
			has_before = other != permutation &&
				*(short *)(permutations + other * CE_SOUND_PERMUTATION_SIZE + CE_PERMUTATION_NEXT_OFFSET) ==
					(short)permutation;
		}
		if (has_before || placed[permutation])
			continue;
		/* (a chain's first: it and the ones after it) */
		for (next = (long)permutation; next >= 0 && (unsigned long)next < permutation_count && !placed[next];
			next = *(short *)(permutations + next * CE_SOUND_PERMUTATION_SIZE + CE_PERMUTATION_NEXT_OFFSET))
		{
			placed[next] = 1;
			order[count++] = (unsigned long)next;
		}
	}
	for (permutation = 0; permutation < permutation_count; permutation++)
	{
		if (!placed[permutation])
			order[count++] = permutation;
	}
	free(placed);
}

/* the permutations played before and after `permutation` in its pitch
range (NONE for none): the one whose next permutation it is, and its
next. A looped sound of one permutation (split or not) plays it over and
over: its first piece's is its last, and its last's its first */
static void ce_permutation_neighbors(byte *range, byte *permutations, unsigned long permutation_count,
	unsigned long permutation, boolean looped, long *before, long *after)
{
	unsigned long other;
	short next = *(short *)(permutations + permutation * CE_SOUND_PERMUTATION_SIZE + CE_PERMUTATION_NEXT_OFFSET);

	*before = NONE;
	*after = next >= 0 && (unsigned long)next < permutation_count && (unsigned long)next != permutation ? next : NONE;
	for (other = 0; other < permutation_count && *before == NONE; other++)
	{
		if (other != permutation &&
			*(short *)(permutations + other * CE_SOUND_PERMUTATION_SIZE + CE_PERMUTATION_NEXT_OFFSET) == (short)permutation)
		{
			*before = (long)other;
		}
	}
	if (looped && *(short *)(range + CE_PITCH_RANGE_ACTUAL_PERMUTATION_COUNT_OFFSET) == 1)
	{
		long first = 0, last = 0, steps;

		/* (the chain from the permutation picked, 0, to its end) */
		for (steps = 0; steps < (long)permutation_count; steps++)
		{
			short following = *(short *)(permutations + last * CE_SOUND_PERMUTATION_SIZE + CE_PERMUTATION_NEXT_OFFSET);

			if (following < 0 || (unsigned long)following >= permutation_count || following == last)
				break;
			last = following;
		}
		if ((long)permutation == first && *before == NONE)
			*before = last;
		if ((long)permutation == last && *after == NONE)
			*after = first;
	}
}

/* what a permutation of channels at rate, brought to another rate, is
filtered with at its ends: the tail of the one played before it and the
head of the one after it (read now if they were not decoded yet), at its
rate and channels; or, where it is played from or into silence (a sound
that is not looped, at its start or end), silence. *joined if it has
either neighbor */
static void ce_permutation_context(byte *range, byte *permutations, unsigned long permutation_count,
	unsigned long permutation, boolean is_looped, int channels, int rate, struct ce_permutation_edges *edges,
	HANDLE sounds_map, HANDLE map_file, boolean from_sounds_map, short const **before_samples, int *before_frames,
	short const **after_samples, int *after_frames, boolean *joined)
{
	struct ce_permutation_edges const *previous = NULL, *following = NULL;

	*before_samples = *after_samples = NULL;
	*before_frames = *after_frames = 0;
	*joined = FALSE;
	if (!edges)
		return;
	{
		long before, after;

		ce_permutation_neighbors(range, permutations, permutation_count, permutation, is_looped, &before, &after);
		/* (one not decoded yet: the one after's head read alone; the one
		before's tail only in a ring, read whole) */
		if (before != NONE && !edges[before].tail_ready)
		{
			ce_permutation_edge_read(permutations + before * CE_SOUND_PERMUTATION_SIZE, sounds_map, map_file,
				from_sounds_map, TRUE, &edges[before]);
		}
		if (after != NONE && !edges[after].head_ready)
		{
			ce_permutation_edge_read(permutations + after * CE_SOUND_PERMUTATION_SIZE, sounds_map, map_file,
				from_sounds_map, FALSE, &edges[after]);
		}
		if (before != NONE && edges[before].tail_frames > 0 && edges[before].channels == channels &&
			edges[before].rate == rate)
		{
			previous = &edges[before];
		}
		if (after != NONE && edges[after].head_frames > 0 && edges[after].channels == channels &&
			edges[after].rate == rate)
		{
			following = &edges[after];
		}
	}
	/* (the start of a sound that is not looped, and its end, are played
	from and into silence: the filter reaches into that. Where a looped
	sound of several permutations goes next is picked as it plays, and the
	filter is shortened there: ce_vorbis_resample_piece) */
	*before_samples = previous ? previous->tail : NULL;
	*after_samples = following ? following->head : NULL;
	*before_frames = previous ? previous->tail_frames : 0;
	*after_frames = following ? following->head_frames : 0;
	if (!is_looped && !previous)
	{
		*before_samples = ce_resample_silence;
		*before_frames = CE_RESAMPLE_CONTEXT;
	}
	if (!is_looped && !following)
	{
		*after_samples = ce_resample_silence;
		*after_frames = CE_RESAMPLE_CONTEXT;
	}
	*joined = previous || following;
}

#ifndef HALO_CONSOLE
/* the last CE_RESAMPLE_CONTEXT frames of what came before and the frames
just read: the tail kept, in tail (*tail_frames of them) */
static void ce_sound_stream_tail(short *tail, int *tail_frames, short const *samples, int frames, int channels)
{
	int keep = frames < CE_RESAMPLE_CONTEXT ? frames : CE_RESAMPLE_CONTEXT;
	int drop = *tail_frames + keep > CE_RESAMPLE_CONTEXT ? *tail_frames + keep - CE_RESAMPLE_CONTEXT : 0;

	if (drop)
	{
		memmove(tail, tail + drop * channels, (size_t)(*tail_frames - drop) * (size_t)channels * sizeof(*tail));
		*tail_frames -= drop;
	}
	memcpy(tail + *tail_frames * channels, samples + (frames - keep) * channels,
		(size_t)keep * (size_t)channels * sizeof(*tail));
	*tail_frames += keep;
}

/* a long Ogg Vorbis permutation (of more than CE_SOUND_STREAM_OGG_BYTES)
decoded a piece of CE_SOUND_STREAM_PIECE_FRAMES at a time, each brought to
played_rate (with the frames before and after it, as the whole would be:
ce_vorbis_resample_piece; the permutation's own ends with `before` and
`after`, ce_permutation_context) and written to file as it is: at most two
pieces held at once, and no more than CE_SOUND_STREAM_MOST bytes, or the
room left in the file, written. Its edges, as decoded, kept in edge (if
given). The bytes written, or 0 if it could not be (what was written is
then written over); *resampled if it was brought to another rate. Only
a permutation of `channels` at played_rate, or at half or twice it, is */
static unsigned long ce_sound_stream(struct ce_sound_writer *writer, unsigned long room, void *stream,
	int channels, int rate, int played_rate, short const *before, int before_frames, short const *after,
	int after_frames, struct ce_permutation_edges *edge, boolean *resampled, boolean *full)
{
	short *pieces[2];
	int frames[2];
	short head[CE_RESAMPLE_CONTEXT * 2], tail[CE_RESAMPLE_CONTEXT * 2], last[CE_RESAMPLE_CONTEXT * 2];
	int head_frames = 0, tail_frames = 0, last_frames = 0;
	unsigned long written = 0;
	boolean ok = TRUE, first = TRUE;
	int current = 0;

	*resampled = FALSE;
	if (rate != played_rate && rate != played_rate * 2 && rate * 2 != played_rate)
		return 0;
	pieces[0] = ce_vorbis_samples_new(CE_SOUND_STREAM_PIECE_FRAMES, channels);
	pieces[1] = ce_vorbis_samples_new(CE_SOUND_STREAM_PIECE_FRAMES, channels);
	if (!pieces[0] || !pieces[1])
	{
		ce_vorbis_free(pieces[0]);
		ce_vorbis_free(pieces[1]);
		return 0;
	}
	frames[0] = ce_vorbis_stream_read(stream, pieces[0], CE_SOUND_STREAM_PIECE_FRAMES);
	if (frames[0] <= 0)
		ok = FALSE;
	else
	{
		head_frames = frames[0] < CE_RESAMPLE_CONTEXT ? frames[0] : CE_RESAMPLE_CONTEXT;
		memcpy(head, pieces[0], (size_t)head_frames * (size_t)channels * sizeof(*head));
	}
	while (ok && frames[current] > 0)
	{
		int other = !current;
		int count = frames[current];
		int out_frames = count;
		unsigned long bytes;
		short const *piece_before = first ? before : last;
		int piece_before_frames = first ? before_frames : last_frames;

		/* (the piece after it, read first: the filter reaches into it) */
		if (!pieces[other])
		{
			pieces[other] = ce_vorbis_samples_new(CE_SOUND_STREAM_PIECE_FRAMES, channels);
			if (!pieces[other])
			{
				ok = FALSE;
				break;
			}
		}
		frames[other] = ce_vorbis_stream_read(stream, pieces[other], CE_SOUND_STREAM_PIECE_FRAMES);
		if (frames[other] < 0)
			frames[other] = 0;
		/* (its own tail, as decoded, before it is brought to its rate in
		place: the next piece's context, and the permutation's edge) */
		last_frames = 0;
		ce_sound_stream_tail(last, &last_frames, pieces[current], count, channels);
		ce_sound_stream_tail(tail, &tail_frames, pieces[current], count, channels);
		if (rate != played_rate)
		{
			out_frames = ce_vorbis_resample_piece(&pieces[current], count, channels, rate, played_rate,
				piece_before, piece_before_frames, frames[other] > 0 ? pieces[other] : after,
				frames[other] > 0 ? (frames[other] < CE_RESAMPLE_CONTEXT ? frames[other] : CE_RESAMPLE_CONTEXT) :
					after_frames);
			*resampled = TRUE;
			if (out_frames <= 0 || !pieces[current])
			{
				ok = FALSE;
				break;
			}
			/* (doubled: given back, and a piece of CE_SOUND_STREAM_PIECE_FRAMES
			made for the next read. Doubled from a short read it is smaller
			than that, and a short read can come before the stream's end:
			stb_vorbis gives one where it skips a page it cannot read) */
			if (rate * 2 == played_rate)
			{
				short *piece = pieces[current];

				bytes = (unsigned long)out_frames * (unsigned long)channels * sizeof(short);
				if (bytes > room - written)
					*full = TRUE;
				if (bytes > room - written || written + bytes > CE_SOUND_STREAM_MOST ||
					!ce_sound_write(writer, piece, bytes))
				{
					ok = FALSE;
					break;
				}
				written += bytes;
				ce_vorbis_free(piece);
				pieces[current] = NULL;
				first = FALSE;
				current = other;
				continue;
			}
		}
		bytes = (unsigned long)out_frames * (unsigned long)channels * sizeof(short);
		if (bytes > room - written)
			*full = TRUE;
		if (bytes > room - written || written + bytes > CE_SOUND_STREAM_MOST ||
			!ce_sound_write(writer, pieces[current], bytes))
		{
			ok = FALSE;
			break;
		}
		written += bytes;
		first = FALSE;
		current = other;
	}
	ce_vorbis_free(pieces[0]);
	ce_vorbis_free(pieces[1]);
	if (!ok || !written)
		return 0;
	if (edge)
	{
		edge->head_ready = edge->tail_ready = TRUE;
		edge->channels = channels;
		edge->rate = rate;
		edge->head_frames = head_frames;
		edge->tail_frames = tail_frames;
		memcpy(edge->head, head, (size_t)head_frames * (size_t)channels * sizeof(short));
		memcpy(edge->tail, tail, (size_t)tail_frames * (size_t)channels * sizeof(short));
		/* (its length, as ce_vorbis_edges gives it, is not needed: only a
		ring's tail is read whole) */
		edge->frames = 0;
	}
	return written;
}

/* a decoded sound's permutations of more samples than the sound cache
holds (CE_SOUND_SPLIT_SIZE), which the game would cut there or not play:
each split, in each pitch range, into chunks of the same samples in the
decoded file, played one after another as Halo's tools split a long sound
(next_permutation_index, the sound's linked permutations flag): the first
chunk the permutation itself (what picks it, its mouth and subtitle data,
are its), the rest added after the range's permutations, the last
followed by what followed the permutation. A sound whose permutations were
not linked has each of them end its own chain. The range's permutations
are given a block of their own, after the tags (ce_resources_allocate_tags:
no more than the map's checks left spare), with room for no more than the
game's 256 of them; a range there is no room for is left as it was. How
many permutations were split */
static long ce_sound_split(byte *sound, byte *pitch_ranges, unsigned long pitch_range_count, char const *name)
{
	long split = 0;
	unsigned long pitch_range;
	boolean linked = (*(unsigned long *)sound & CE_SOUND_LINKED_PERMUTATIONS_FLAG) != 0;

	if (!ce_tags_writable(sound, 4))
		return 0;
	for (pitch_range = 0; pitch_range < pitch_range_count; pitch_range++)
	{
		byte *range = pitch_ranges + pitch_range * CE_SOUND_PITCH_RANGE_SIZE;
		unsigned long count, permutation, extra = 0, chunk = CE_SOUND_CHUNK_SIZE;
		byte *permutations = ce_sound_block(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET, CE_SOUND_PERMUTATION_SIZE,
			&count);
		unsigned long address, next_new;
		byte *block;

		if (!permutations || !count || !ce_tags_writable(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET, 8))
			continue;
		/* (the chunk: the least that keeps the range to 256 permutations) */
		for (;;)
		{
			extra = 0;
			for (permutation = 0; permutation < count; permutation++)
			{
				long size = *(long *)(permutations + permutation * CE_SOUND_PERMUTATION_SIZE +
					CE_PERMUTATION_SAMPLES_OFFSET);

				if (size > CE_SOUND_SPLIT_SIZE)
					extra += ((unsigned long)size + chunk - 1) / chunk - 1;
			}
			if (!extra || count + extra <= CE_SOUND_MAXIMUM_PERMUTATIONS || chunk >= CE_SOUND_CHUNK_MOST)
				break;
			chunk *= 2;
		}
		if (!extra)
			continue;
		if (count + extra > CE_SOUND_MAXIMUM_PERMUTATIONS)
		{
			error(_error_silent, "Custom Edition maps: %s: a pitch range's samples would take %lu permutations, "
				"more than the game's %d; left as it was", name, count + extra, CE_SOUND_MAXIMUM_PERMUTATIONS);
			continue;
		}
		address = ce_resources_allocate_split((count + extra) * CE_SOUND_PERMUTATION_SIZE);
		block = address ? ce_loaded_pointer(address, (count + extra) * CE_SOUND_PERMUTATION_SIZE) : NULL;
		if (!block)
		{
			error(_error_silent, "Custom Edition maps: %s: no room in the tag cache for its long samples' %lu "
				"permutations; left as it was", name, extra);
			continue;
		}
		memcpy(block, permutations, count * CE_SOUND_PERMUTATION_SIZE);
		memset(block + count * CE_SOUND_PERMUTATION_SIZE, 0, extra * CE_SOUND_PERMUTATION_SIZE);
		/* (not linked before: each permutation was one sound alone) */
		if (!linked)
		{
			for (permutation = 0; permutation < count; permutation++)
				*(short *)(block + permutation * CE_SOUND_PERMUTATION_SIZE + CE_PERMUTATION_NEXT_OFFSET) = NONE;
		}
		next_new = count;
		for (permutation = 0; permutation < count; permutation++)
		{
			byte *at = block + permutation * CE_SOUND_PERMUTATION_SIZE;
			unsigned long size = (unsigned long)*(long *)(at + CE_PERMUTATION_SAMPLES_OFFSET);
			unsigned long offset = *(unsigned long *)(at + CE_PERMUTATION_SAMPLES_OFFSET + 8);
			short following = *(short *)(at + CE_PERMUTATION_NEXT_OFFSET);
			unsigned long done;
			byte *previous = at;

			if ((long)size <= CE_SOUND_SPLIT_SIZE)
				continue;
			/* (what follows it, if its chain ends: one that comes back round,
			to it or in a ring of its own, would play for ever once the
			chunks join it) */
			{
				long step, at_index = following;

				/* (through the chunks of one split before it too, which end
				where its chain did) */
				for (step = 0; at_index != NONE && step <= (long)next_new; step++)
				{
					if (at_index < 0 || (unsigned long)at_index >= next_new || (unsigned long)at_index == permutation)
						break;
					at_index = *(short *)(block + at_index * CE_SOUND_PERMUTATION_SIZE + CE_PERMUTATION_NEXT_OFFSET);
				}
				if (at_index != NONE)
				{
					error(_error_silent, "Custom Edition maps: %s: a long permutation's chain comes back round; "
						"its chunks end it", name);
					following = NONE;
				}
			}
			*(long *)(at + CE_PERMUTATION_SAMPLES_OFFSET) = (long)chunk;
			*(unsigned long *)(at + CE_PERMUTATION_BUFFER_SIZE_OFFSET) = chunk;
			for (done = chunk; done < size; done += chunk)
			{
				byte *piece = block + next_new * CE_SOUND_PERMUTATION_SIZE;
				unsigned long piece_size = size - done < chunk ? size - done : chunk;

				memcpy(piece, at, CE_SOUND_PERMUTATION_SIZE);
				*(long *)(piece + CE_PERMUTATION_SAMPLES_OFFSET) = (long)piece_size;
				*(unsigned long *)(piece + CE_PERMUTATION_SAMPLES_OFFSET + 8) = offset + done;
				*(unsigned long *)(piece + CE_PERMUTATION_BUFFER_SIZE_OFFSET) = piece_size;
				/* (its mouth and subtitle data the first chunk's) */
				memset(piece + CE_PERMUTATION_MOUTH_DATA_OFFSET, 0, 20);
				memset(piece + CE_PERMUTATION_SUBTITLE_DATA_OFFSET, 0, 20);
				*(short *)(previous + CE_PERMUTATION_NEXT_OFFSET) = (short)next_new;
				previous = piece;
				next_new++;
			}
			*(short *)(previous + CE_PERMUTATION_NEXT_OFFSET) = following;
			error(_error_silent, "Custom Edition maps: %s: %lu bytes of samples played as %lu permutations of %lu "
				"bytes", name, size, (size + chunk - 1) / chunk, chunk);
			split++;
		}
		*(unsigned long *)(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET) = count + extra;
		*(unsigned long *)(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET + 4) = address;
		*(unsigned long *)sound |= CE_SOUND_LINKED_PERMUTATIONS_FLAG | CE_SOUND_SPLIT_BY_LOADER_FLAG;
		linked = TRUE;
	}
	return split;
}
#endif

/* a sound's permutations decoded (or, a sound's with samples in both the map
and sounds.map, copied as they are) to writer, no more than room bytes from
where it starts: listed in *decoded (grown as needed), each where it is from
the sound's start. _ce_sound_full if there was not room for them (said by
the caller), _ce_sound_failed if one could not be (said here). Its
permutations' edges are kept as each is decoded, in the order they are
played (ce_permutation_edges) */
static int ce_sound_job_decode(
	struct ce_sound_job const *job,
	struct ce_sound_writer *writer,
	unsigned long room,
	struct ce_decoded_permutation **decoded,
	unsigned long *decoded_count,
	unsigned long *decoded_capacity,
	struct ce_sound_decode_counts *counts)
{
	HANDLE map_file = cache_files_ce_map_file();
	HANDLE sounds_map = job->sounds_map;
	unsigned long start = writer->position, pitch_range;
	struct ce_permutation_edges *edges = NULL;
	unsigned long *order = NULL;
	int result = _ce_sound_decoded;

	*decoded_count = 0;
	for (pitch_range = 0; result == _ce_sound_decoded && pitch_range < job->range_count; pitch_range++)
	{
		byte *range = job->ranges + pitch_range * CE_SOUND_PITCH_RANGE_SIZE;
		unsigned long permutation_count = job->permutation_counts[pitch_range];
		byte *permutations = job->permutations[pitch_range];
		unsigned long permutation, step;

		/* (an Ogg Vorbis sound's permutations each after the one played
		before it, and their edges kept: ce_permutation_edges) */
		if (job->ogg && permutation_count)
		{
			struct ce_permutation_edges *grown_edges = realloc(edges, permutation_count * sizeof(*edges));
			unsigned long *grown_order = realloc(order, permutation_count * sizeof(*order));

			/* (none, if there is no room for them) */
			if (!grown_edges && edges)
				free(edges);
			if (!grown_order && order)
				free(order);
			edges = grown_edges;
			order = grown_order;
			if (edges)
				memset(edges, 0, permutation_count * sizeof(*edges));
			if (order)
				ce_permutation_order(permutations, permutation_count, order);
		}
		for (step = 0; result == _ce_sound_decoded && step < permutation_count; step++)
		{
			byte *at;
			long size;
			unsigned long offset, used = writer->position - start;
			HANDLE source;
			unsigned char *data;
			short *samples = NULL;
			int channels = 0, rate = 0, frames;
			unsigned long bytes;
			/* (a looped sound's: its pieces play round in a ring) */
			boolean is_looped = edges && job->is_looped;

			permutation = job->ogg && order ? order[step] : step;
			at = permutations + permutation * CE_SOUND_PERMUTATION_SIZE;
			size = *(long *)(at + CE_PERMUTATION_SAMPLES_OFFSET);
			offset = *(unsigned long *)(at + CE_PERMUTATION_SAMPLES_OFFSET + 8);
			source = job->indexed || ce_samples_external(at) ? sounds_map : map_file;
			if (size <= 0)
				continue;
			if (!source)
			{
				result = _ce_sound_failed;
				break;
			}
			/* (another compression's samples stay in the sound's own file,
			which a decoded sound is no longer read from) */
			if (job->ogg && *(short *)(at + CE_PERMUTATION_COMPRESSION_OFFSET) != CE_SOUND_COMPRESSION_OGG)
			{
				error(_error_silent, "Custom Edition maps: %s: an Ogg Vorbis sound with a permutation of "
					"another compression; left as it was", ce_loaded_tag_name(job->instance));
				result = _ce_sound_failed;
				break;
			}
			if (*decoded_count == *decoded_capacity)
			{
				unsigned long capacity = *decoded_capacity ? *decoded_capacity * 2 : 64;
				struct ce_decoded_permutation *grown = realloc(*decoded, capacity * sizeof(**decoded));

				if (!grown)
				{
					result = _ce_sound_failed;
					break;
				}
				*decoded = grown;
				*decoded_capacity = capacity;
			}
			data = malloc((size_t)size);
			if (!job->ogg)
			{
				/* (a sound with samples in both files: copied as they are) */
				bytes = (unsigned long)size;
				if (bytes > room - used)
				{
					if (data)
						free(data);
					result = _ce_sound_full;
					break;
				}
				if (!data || !ce_read(source, offset, data, bytes) || !ce_sound_write(writer, data, bytes))
				{
					error(_error_silent, "Custom Edition maps: %s: its samples in the map and sounds.map could not "
						"be copied", ce_loaded_tag_name(job->instance));
					if (data)
						free(data);
					result = _ce_sound_failed;
					break;
				}
				free(data);
				(*decoded)[*decoded_count].range = pitch_range;
				(*decoded)[*decoded_count].index = permutation;
				(*decoded)[*decoded_count].offset = used;
				(*decoded)[*decoded_count].size = bytes;
				(*decoded_count)++;
				continue;
			}
#ifndef HALO_CONSOLE
			/* (a long one decoded and written a piece at a time, not held
			whole: ce_sound_stream; split once the sound is, if the cache
			could not hold it, ce_sound_split) */
			if (data && size > CE_SOUND_STREAM_OGG_BYTES && ce_read(source, offset, data, (unsigned long)size))
			{
				void *stream = ce_vorbis_stream_open(data, (int)size, &channels, &rate);

				if (stream && channels == (job->encoding ? 2 : 1) &&
					(rate == job->played_rate || rate == job->played_rate * 2 || rate * 2 == job->played_rate))
				{
					short const *before_samples = NULL, *after_samples = NULL;
					int before_frames = 0, after_frames = 0;
					boolean joined = FALSE, is_resampled = FALSE, full = FALSE;

					if (rate != job->played_rate)
					{
						ce_permutation_context(range, permutations, permutation_count, permutation, is_looped,
							channels, rate, edges, sounds_map, map_file, job->indexed,
							&before_samples, &before_frames, &after_samples, &after_frames, &joined);
					}
					bytes = ce_sound_stream(writer, room - used, stream, channels, rate, job->played_rate,
						before_samples, before_frames, after_samples, after_frames,
						edges ? &edges[permutation] : NULL, &is_resampled, &full);
					ce_vorbis_stream_close(stream);
					free(data);
					if (!bytes)
					{
						if (full)
						{
							result = _ce_sound_full;
							break;
						}
						error(_error_silent, "Custom Edition maps: %s: a long Ogg Vorbis permutation could not be "
							"decoded; it does not play", ce_loaded_tag_name(job->instance));
						opensauce_survey_note_resource_miss("sound (Ogg Vorbis permutation that does not play)",
							ce_loaded_tag_name(job->instance));
						result = _ce_sound_failed;
						break;
					}
					if (is_resampled)
					{
						counts->resampled++;
						counts->resampled_joined += joined;
						counts->resampled_looped += joined && is_looped;
					}
					counts->streamed++;
					(*decoded)[*decoded_count].range = pitch_range;
					(*decoded)[*decoded_count].index = permutation;
					(*decoded)[*decoded_count].offset = used;
					(*decoded)[*decoded_count].size = bytes;
					(*decoded_count)++;
					continue;
				}
				ce_vorbis_stream_close(stream);
			}
#endif
			frames = data && ce_read(source, offset, data, (unsigned long)size) ?
				ce_vorbis_decode(data, size, &channels, &rate, &samples) : -1;
			if (data)
				free(data);
			/* (its edges, for its neighbors) */
			if (edges && frames > 0 && samples && channels >= 1 && channels <= 2)
			{
				struct ce_permutation_edges *edge = &edges[permutation];
				int count = frames < CE_RESAMPLE_CONTEXT ? frames : CE_RESAMPLE_CONTEXT;

				edge->head_ready = edge->tail_ready = TRUE;
				edge->frames = frames;
				edge->channels = channels;
				edge->rate = rate;
				edge->head_frames = edge->tail_frames = count;
				memcpy(edge->head, samples, (size_t)count * (size_t)channels * sizeof(short));
				memcpy(edge->tail, samples + (size_t)(frames - count) * (size_t)channels,
					(size_t)count * (size_t)channels * sizeof(short));
			}
			/* (one at twice the rate it is played at, or half it, brought
			to it: ce_vorbis_resample, told once a map) */
			if (frames > 0 && channels == (job->encoding ? 2 : 1) && rate != job->played_rate)
			{
				short const *before_samples, *after_samples;
				int before_frames, after_frames;
				boolean joined;
				int resampled;

				/* (with the permutations played before and after it: a
				long sound is split into several, and the filter reaches
				across the joins) */
				ce_permutation_context(range, permutations, permutation_count, permutation, is_looped, channels,
					rate, edges, sounds_map, map_file, job->indexed, &before_samples, &before_frames,
					&after_samples, &after_frames, &joined);
				resampled = ce_vorbis_resample_piece(&samples, frames, channels, rate, job->played_rate,
					before_samples, before_frames, after_samples, after_frames);
				if (resampled > 0 && joined)
				{
					counts->resampled_joined++;
					counts->resampled_looped += is_looped;
				}

				if (resampled > 0)
				{
					if (!counts->resampled_told)
					{
						error(_error_silent, "Custom Edition maps: %s: Ogg Vorbis samples at %d Hz, played at %d Hz: "
							"resampled (any more like it are too)", ce_loaded_tag_name(job->instance), rate,
							job->played_rate);
						counts->resampled_told = TRUE;
					}
					frames = resampled;
					rate = job->played_rate;
					counts->resampled++;
				}
			}
			/* (and no more than the sound cache could hold of it) */
			if (frames <= 0 || channels != (job->encoding ? 2 : 1) || rate != job->played_rate || frames > 0x1000000)
			{
				error(_error_silent, "Custom Edition maps: %s: an Ogg Vorbis permutation of %d channels at %d Hz "
					"in a sound of %d at %d Hz; it does not play", ce_loaded_tag_name(job->instance),
					channels, rate, job->encoding ? 2 : 1, job->played_rate);
				opensauce_survey_note_resource_miss("sound (Ogg Vorbis permutation that does not play)",
					ce_loaded_tag_name(job->instance));
				ce_vorbis_free(samples);
				result = _ce_sound_failed;
				break;
			}
			bytes = (unsigned long)frames * (unsigned long)channels * sizeof(short);
			if (bytes > room - used)
			{
				ce_vorbis_free(samples);
				result = _ce_sound_full;
				break;
			}
			if (!ce_sound_write(writer, samples, bytes))
			{
				ce_vorbis_free(samples);
				result = _ce_sound_failed;
				break;
			}
			ce_vorbis_free(samples);
			(*decoded)[*decoded_count].range = pitch_range;
			(*decoded)[*decoded_count].index = permutation;
			(*decoded)[*decoded_count].offset = used;
			(*decoded)[*decoded_count].size = bytes;
			(*decoded_count)++;
		}
	}
	if (edges)
		free(edges);
	if (order)
		free(order);
	return result;
}

/* a sound's job's pitch ranges and permutations (struct ce_sound_job): on the
desktop copies, kept to decode it again; on a console its own blocks */
static boolean ce_sound_job_blocks(
	struct ce_sound_job *job,
	byte *pitch_ranges)
{
	unsigned long pitch_range;

	job->ranges = NULL;
	job->permutations = NULL;
	job->permutation_counts = NULL;
	if (!job->range_count)
		return TRUE;
	job->permutations = malloc(job->range_count * sizeof(*job->permutations));
	job->permutation_counts = malloc(job->range_count * sizeof(*job->permutation_counts));
#ifdef HALO_CONSOLE
	job->ranges = pitch_ranges;
#else
	job->ranges = malloc(job->range_count * CE_SOUND_PITCH_RANGE_SIZE);
#endif
	if (!job->permutations || !job->permutation_counts || !job->ranges)
		return FALSE;
#ifndef HALO_CONSOLE
	memcpy(job->ranges, pitch_ranges, job->range_count * CE_SOUND_PITCH_RANGE_SIZE);
	for (pitch_range = 0; pitch_range < job->range_count; pitch_range++)
		job->permutations[pitch_range] = NULL;
#endif
	for (pitch_range = 0; pitch_range < job->range_count; pitch_range++)
	{
		unsigned long count;
		byte *permutations = ce_sound_block(pitch_ranges + pitch_range * CE_SOUND_PITCH_RANGE_SIZE +
			CE_PITCH_RANGE_PERMUTATIONS_OFFSET, CE_SOUND_PERMUTATION_SIZE, &count);

		job->permutation_counts[pitch_range] = count;
#ifdef HALO_CONSOLE
		job->permutations[pitch_range] = permutations;
#else
		job->permutations[pitch_range] = malloc(count ? count * CE_SOUND_PERMUTATION_SIZE : 1);
		if (!job->permutations[pitch_range])
			return FALSE;
		if (count)
			memcpy(job->permutations[pitch_range], permutations, count * CE_SOUND_PERMUTATION_SIZE);
#endif
	}
	return TRUE;
}

static void ce_sound_job_free(
	struct ce_sound_job *job)
{
	/* (the game's free takes no NULL) */
#ifndef HALO_CONSOLE
	unsigned long pitch_range;

	for (pitch_range = 0; job->permutations && pitch_range < job->range_count; pitch_range++)
	{
		if (job->permutations[pitch_range])
			free(job->permutations[pitch_range]);
	}
	if (job->ranges)
		free(job->ranges);
#endif
	if (job->permutations)
		free(job->permutations);
	if (job->permutation_counts)
		free(job->permutation_counts);
	job->ranges = NULL;
	job->permutations = NULL;
	job->permutation_counts = NULL;
}

#ifndef HALO_CONSOLE
/* the most offset a decoded sound's tags read it at: past the file's budget
for one that did not fit at the map's load (its tags' offsets are signed) */
#define CE_DECODED_SOUNDS_PLACED_MOST 0x7fff0000UL

boolean sound_definition_playing(long definition_index);

static void ce_sound_entries_free(
	void)
{
	long entry;

	for (entry = 0; entry < ce_sound_entry_count; entry++)
		ce_sound_job_free(&ce_sound_jobs[entry]);
	if (ce_sound_jobs)
		free(ce_sound_jobs);
	if (ce_sound_pool)
		free(ce_sound_pool);
	if (ce_sound_bases)
		free(ce_sound_bases);
	if (ce_sound_entry_by_tag)
		free(ce_sound_entry_by_tag);
	ce_sound_jobs = NULL;
	ce_sound_pool = NULL;
	ce_sound_bases = NULL;
	ce_sound_entry_by_tag = NULL;
	ce_sound_entry_tag_count = 0;
	ce_sound_entry_count = ce_sound_entry_capacity = 0;
}

/* a decoded sound kept: its entry, or NONE (no room to keep it) */
static long ce_sound_entry_add(
	struct ce_sound_job const *job,
	unsigned long placed,
	unsigned long length,
	boolean resident,
	unsigned long base)
{
	long entry;

	if (ce_sound_entry_count == ce_sound_entry_capacity)
	{
		long capacity = ce_sound_entry_capacity ? ce_sound_entry_capacity * 2 : 256;
		struct ce_sound_job *jobs = realloc(ce_sound_jobs, (size_t)capacity * sizeof(*jobs));
		struct ce_sound_pool_entry *pool;
		unsigned long *bases;

		if (!jobs)
			return NONE;
		ce_sound_jobs = jobs;
		pool = realloc(ce_sound_pool, (size_t)capacity * sizeof(*pool));
		if (!pool)
			return NONE;
		ce_sound_pool = pool;
		bases = realloc(ce_sound_bases, (size_t)capacity * sizeof(*bases));
		if (!bases)
			return NONE;
		ce_sound_bases = bases;
		ce_sound_entry_capacity = capacity;
	}
	entry = ce_sound_entry_count++;
	ce_sound_jobs[entry] = *job;
	ce_sound_pool[entry].base = base;
	ce_sound_pool[entry].length = length;
	ce_sound_pool[entry].last_used = 0;
	ce_sound_pool[entry].resident = resident;
	ce_sound_pool[entry].in_flight = 0;
	ce_sound_pool[entry].failed = FALSE;
	ce_sound_bases[entry] = placed;
	return entry;
}

/* whether a decoded sound may not be moved out of the file now: playing
(one with a read of it in flight never is: ce_sound_pool_place) */
static int ce_sound_entry_busy(
	long entry,
	void *context)
{
	(void)context;
	return sound_definition_playing((long)ce_sound_jobs[entry].instance->tag_index);
}

/* a decoded sound not in the file decoded into it again, where room is made
for it (ce_sound_pool_place: the sounds read longest ago that are not
busy moved out) */
static boolean ce_sound_entry_load(
	long entry)
{
	struct ce_sound_job const *job = &ce_sound_jobs[entry];
	struct ce_sound_decode_counts counts;
	struct ce_decoded_permutation *decoded = NULL;
	unsigned long decoded_count = 0, decoded_capacity = 0;
	struct ce_sound_writer writer;
	long base, evicted = 0;
	int result;

	if (!ce_decoded_sounds_file)
		return FALSE;
	base = ce_sound_pool_place(ce_sound_pool, ce_sound_entry_count, entry, CE_MAXIMUM_DECODED_SOUNDS_SIZE,
		ce_sound_entry_busy, NULL, &evicted);
	ce_sound_evictions += evicted;
	if (base < 0)
	{
		if (!ce_sound_no_room_told)
		{
			error(_error_silent, "Custom Edition maps: %s: no room in the decoded sounds' file (%lu MB) while the "
				"sounds in it play; it does not play now", ce_loaded_tag_name(job->instance),
				(unsigned long)CE_MAXIMUM_DECODED_SOUNDS_SIZE >> 20);
			ce_sound_no_room_told = TRUE;
		}
		return FALSE;
	}
	memset(&counts, 0, sizeof(counts));
	counts.resampled_told = TRUE;
	writer.file = ce_decoded_sounds_file;
	writer.position = (unsigned long)base;
	ce_read_positioned = TRUE;
	result = ce_sound_job_decode(job, &writer, ce_sound_pool[entry].length, &decoded, &decoded_count,
		&decoded_capacity, &counts);
	ce_read_positioned = FALSE;
	if (decoded)
		free(decoded);
	if (result != _ce_sound_decoded || writer.position - (unsigned long)base != ce_sound_pool[entry].length)
	{
		/* (not tried again until the next map: said once) */
		ce_sound_pool[entry].resident = FALSE;
		ce_sound_pool[entry].failed = TRUE;
		error(_error_silent, "Custom Edition maps: %s: its samples could not be decoded again; it does not play "
			"on this map", ce_loaded_tag_name(job->instance));
		return FALSE;
	}
	ce_sound_reloads++;
	if (evicted && !ce_sound_evicted_told)
	{
		error(_error_silent, "Custom Edition maps: the decoded sounds' file is full: the sounds played longest ago "
			"are moved out of it for the ones played now (first %s)", ce_loaded_tag_name(job->instance));
		ce_sound_evicted_told = TRUE;
	}
	return TRUE;
}
#endif

/* where a decoded sound's samples are read from (cache_files_windows.c): the
offset its tags give made its place in the decoded sounds' file, the sound
decoded into it first if it is not there (desktop). FALSE if it cannot be
read: outside its sound's samples, or past the end of the file. *read is
given for the read's end to be told (ce_resources_sound_read_finished), or
NONE: until then the sound stays in the file */
boolean ce_resources_sound_read(
	long tag_index,
	long *offset,
	long size,
	long *read)
{
	*read = NONE;
#ifndef HALO_CONSOLE
	{
		long index = tag_index & 0xffff, entry;
		unsigned long at;

		if (tag_index == NONE || index >= ce_indexed_tag_count || !ce_indexed_tags ||
			ce_indexed_tags[index] != CE_DECODED_SOUND)
		{
			return TRUE;
		}
		entry = ce_sound_entry_by_tag && index < ce_sound_entry_tag_count ? ce_sound_entry_by_tag[index] : NONE;
		/* (one kept with no entry: its samples where its tags say, all in
		the file) */
		if (entry == NONE)
			return ce_sound_pool_read_within(0, ce_decoded_sounds_size, *offset, size, NULL);
		if (ce_sound_pool[entry].failed ||
			!ce_sound_pool_read_within(ce_sound_bases[entry], ce_sound_pool[entry].length, *offset, size, &at))
		{
			return FALSE;
		}
		if (!ce_sound_pool[entry].resident && !ce_sound_entry_load(entry))
			return FALSE;
		ce_sound_pool[entry].last_used = system_milliseconds();
		*offset = (long)(ce_sound_pool[entry].base + at);
		ce_sound_pool_read_issued(&ce_sound_pool[entry]);
		*read = (ce_sound_generation << 16) | entry;
	}
#else
	(void)tag_index;
	(void)offset;
	(void)size;
#endif
	return TRUE;
}

/* a read ce_resources_sound_read gave finished (on the cache's thread),
done or failed: its sound may be moved out of the file again. A read of an
earlier map's is let be */
void ce_resources_sound_read_finished(
	long read)
{
#ifndef HALO_CONSOLE
	long entry = read & 0xffff;

	if (read == NONE || (read >> 16) != __atomic_load_n(&ce_sound_generation, __ATOMIC_SEQ_CST) ||
		entry >= ce_sound_entry_count || !ce_sound_pool)
	{
		return;
	}
	ce_sound_pool_read_finished(&ce_sound_pool[entry]);
#else
	(void)read;
#endif
}

/* Halo PC's Ogg Vorbis sounds made the engine's 16-bit PCM ones (which it
plays on PCM channels: sound_preferences.c), those whose samples are in
sounds.map and those whose samples are in the map itself (kokiriforest's and
rainbow road's ambience, most of mermaids_plaza's sounds, which this engine
played as noise when they were left as they were): each permutation's
samples decoded (content_vorbis.c) into z:\ce_sounds.pcm, where they are then
read from, and the sound and its permutations marked uncompressed. Their
channels and rate stay the sound's (an Ogg of others is left as it was,
which the game does not play). Each permutation's samples are read from
sounds.map or the map as Halo PC read them (ce_samples_external); a map's
own sound of another compression with samples in both files, which the
engine reads from one file, has them copied as they are into the same file
(ce_custom_sound). (The sounds' blocks and samples were checked:
ce_resources_check; their compression first made their permutations':
ce_sound_compression_repair.) The file holds no more than
CE_MAXIMUM_DECODED_SOUNDS_SIZE. On the desktop a sound that does not fit
when the map loads is measured, its tags given offsets past that, and it is
decoded when it is first read, moving out sounds played longest ago that are
not playing (ce_resources_sound_read); on a console it does not play. */
static void ce_sounds_decode(
	void *tag_instances,
	long tag_count)
{
	HANDLE custom_sounds_map = ce_resource_maps[_ce_resource_sounds].file;
	char path[256];
	HANDLE file = NULL;
	unsigned long written = 0;
#ifndef HALO_CONSOLE
	/* (where the next sound's tags read it: written, until one did not fit
	at load) */
	unsigned long placed = 0;
	boolean load_full = FALSE;
	long deferred = 0;
#endif
	long sounds = 0, in_map = 0, permutations_decoded = 0, repaired = 0, copied = 0;
	long split_count = 0, flags_cleared = 0;
	boolean full_told = FALSE;
	struct ce_sound_decode_counts counts;
	int played_rate;
	long index;
	/* (a sound's decoded permutations, changed once all of them are) */
	struct ce_decoded_permutation *decoded = NULL;
	unsigned long decoded_count = 0, decoded_capacity = 0;
	/* (the looped sounds) */
	long looped_count = 0;
	unsigned long *looped = ce_looped_sounds(tag_instances, tag_count, &looped_count);

	memset(&counts, 0, sizeof(counts));
	if (ce_decoded_sounds_file)
	{
		CloseHandle(ce_decoded_sounds_file);
		ce_decoded_sounds_file = NULL;
		DeleteFileA(ce_decoded_sounds_path);
	}
#ifndef HALO_CONSOLE
	__atomic_store_n(&ce_sound_generation, (ce_sound_generation + 1) & 0x7fff, __ATOMIC_SEQ_CST);
	ce_decoded_sounds_size = 0;
	ce_sound_entries_free();
	ce_sound_full_told = ce_sound_evicted_told = ce_sound_no_room_told = FALSE;
	ce_sound_reloads = ce_sound_evictions = 0;
	ce_sound_entry_by_tag = tag_count > 0 ? malloc((size_t)tag_count * sizeof(*ce_sound_entry_by_tag)) : NULL;
	for (index = 0; ce_sound_entry_by_tag && index < tag_count; index++)
		ce_sound_entry_by_tag[index] = NONE;
	ce_sound_entry_tag_count = ce_sound_entry_by_tag ? tag_count : 0;
#endif
	/* (the loader's split flag, sound_definitions.h's
	SOUND_DEFINITION_SPLIT_BY_LOADER_FLAG, is the loader's alone: cleared on
	every sound before any is split, so no map's sound plays a chain of its
	own through as an impulse sound) */
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		byte *sound = instance->group_tag == 'snd!' ?
			ce_loaded_pointer(instance->base_address, CE_SOUND_HEADER_SIZE) : NULL;

		if (sound && (*(unsigned long *)sound & CE_SOUND_SPLIT_BY_LOADER_FLAG) && ce_tags_writable(sound, 4))
		{
			*(unsigned long *)sound &= ~CE_SOUND_SPLIT_BY_LOADER_FLAG;
			flags_cleared++;
		}
	}
	/* (in the save root's cache, z:\, never in a maps folder: a player's
	maps may be read only, or shared by several copies of the game) */
	sprintf(path, "z:\\ce_sounds.pcm");
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		byte *sound, *pitch_ranges;
		unsigned long pitch_range_count, pitch_range;
		unsigned long decoded_index, length;
		struct ce_sound_job job;
		struct ce_sound_writer writer;
		int result;
		long looped_index;
#ifndef HALO_CONSOLE
		boolean resident = FALSE;
#endif
		short encoding, sample_rate;
		boolean ogg, from_map = FALSE, from_sounds_map = FALSE;
		HANDLE sounds_map = custom_sounds_map;

		if (instance->group_tag != 'snd!')
			continue;
		/* (sounds.map's samples for a sound copied in from there, or for a
		retail map's stock sound read from Halo PC's own; a map's own
		sound's from either file, by each permutation's flag) */
		if (ce_indexed_tags[index] == _ce_resource_halo_pc_sounds + 1)
			sounds_map = ce_resource_maps[_ce_resource_halo_pc_sounds].file;
		else if (ce_indexed_tags[index] && ce_indexed_tags[index] != _ce_resource_sounds + 1)
			continue;
		/* (its blocks in the tag cache, and none over its tag header or tag
		index: else it is left as it was) */
		sound = ce_loaded_pointer(instance->base_address, CE_SOUND_HEADER_SIZE);
		if (!sound)
		{
			ce_contained("sound %08lx is outside its tag cache", instance->base_address);
			continue;
		}
		if (!ce_sound_blocks_loaded(sound))
			continue;
		if (ce_sound_compression_repair(sound))
			repaired++;
		pitch_ranges = ce_sound_block(sound + CE_SOUND_PITCH_RANGES_OFFSET, CE_SOUND_PITCH_RANGE_SIZE,
			&pitch_range_count);
		for (pitch_range = 0; !ce_indexed_tags[index] && pitch_range < pitch_range_count; pitch_range++)
		{
			byte *range = pitch_ranges + pitch_range * CE_SOUND_PITCH_RANGE_SIZE;
			unsigned long permutation_count;
			byte *permutations = ce_sound_block(range + CE_PITCH_RANGE_PERMUTATIONS_OFFSET, CE_SOUND_PERMUTATION_SIZE,
				&permutation_count);
			unsigned long permutation;

			for (permutation = 0; permutation < permutation_count; permutation++)
			{
				byte *at = permutations + permutation * CE_SOUND_PERMUTATION_SIZE;

				if (ce_samples_external(at))
					from_sounds_map = TRUE;
				else if (*(long *)(at + CE_PERMUTATION_SAMPLES_OFFSET) > 0)
					from_map = TRUE;
			}
		}
		ogg = *(short *)(sound + CE_SOUND_COMPRESSION_OFFSET) == CE_SOUND_COMPRESSION_OGG;
		if (!ogg && !(from_map && from_sounds_map))
		{
#ifndef HALO_CONSOLE
			/* (a 16-bit PCM sound read from its own file, the map or
			sounds.map, split where its samples lie if one is longer than the
			sound cache holds: the tag checks would cut it to the cache's
			size, ce_sound_split) */
			if (*(short *)(sound + CE_SOUND_COMPRESSION_OFFSET) == CE_SOUND_COMPRESSION_NONE)
				split_count += ce_sound_split(sound, pitch_ranges, pitch_range_count, ce_loaded_tag_name(instance));
#endif
			continue;
		}
		/* (this copy of the game's own file: two copies sharing the save
		root each write theirs. An older one is deleted first; on Windows
		one another copy has open cannot be, and the next name is tried.
		Elsewhere the file is deleted once it is open, read through its
		handle alone, so another copy's new one is another file) */
		if (!file)
		{
			int attempt;

			file = INVALID_HANDLE_VALUE;
			for (attempt = 0; attempt < 8 && file == INVALID_HANDLE_VALUE; attempt++)
			{
				if (attempt)
					sprintf(path, "z:\\ce_sounds%d.pcm", attempt);
				if (!DeleteFileA(path) && GetLastError() != ERROR_FILE_NOT_FOUND &&
					GetLastError() != ERROR_PATH_NOT_FOUND)
				{
					continue;
				}
				file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
			}
			if (file == INVALID_HANDLE_VALUE)
			{
				error(_error_silent, "Custom Edition maps: cannot write %s; Ogg Vorbis sounds will not play", path);
				if (looped)
					free(looped);
				return;
			}
			sprintf(ce_decoded_sounds_path, "%s", path);
			DeleteFileA(path);
		}
		encoding = *(short *)(sound + CE_SOUND_ENCODING_OFFSET);
		sample_rate = *(short *)(sound + CE_SOUND_SAMPLE_RATE_OFFSET);
		/* (the rate the game plays the sound at: a mono sound's always 22 kHz,
		as the Xbox's channels have it; Halo PC's 44 kHz mono sounds are
		decoded to it and made 22 kHz sounds) */
		played_rate = encoding ? (sample_rate ? 44100 : 22050) : 22050;

		/* (what decodes it, kept to decode it again on the desktop) */
		memset(&job, 0, sizeof(job));
		job.instance = instance;
		job.sounds_map = sounds_map;
		job.indexed = ce_indexed_tags[index] != 0;
		job.ogg = ogg;
		job.encoding = encoding;
		job.played_rate = played_rate;
		job.range_count = pitch_range_count;
		for (looped_index = 0; looped_index < looped_count && !job.is_looped; looped_index++)
			job.is_looped = looped[looped_index] == instance->tag_index;
		if (!ce_sound_job_blocks(&job, pitch_ranges))
		{
			error(_error_silent, "Custom Edition maps: %s: no memory to decode it; left as it was",
				ce_loaded_tag_name(instance));
			ce_sound_job_free(&job);
			continue;
		}
		/* (all or nothing: every permutation is decoded and written before
		any is changed. A sound read from the decoded file with a permutation
		left pointing into its own file, or the other way about, would play
		the wrong samples. If one cannot be, the sound is left as it was and
		what was written of it is written over by the next) */
		writer.file = file;
		writer.position = written;
#ifdef HALO_CONSOLE
		result = ce_sound_job_decode(&job, &writer, CE_MAXIMUM_DECODED_SOUNDS_SIZE - written, &decoded,
			&decoded_count, &decoded_capacity, &counts);
#else
		/* (one that does not fit, and every one after it, measured: decoded
		when it is read, ce_resources_sound_read) */
		result = _ce_sound_full;
		if (!load_full)
		{
			result = ce_sound_job_decode(&job, &writer, CE_MAXIMUM_DECODED_SOUNDS_SIZE - written, &decoded,
				&decoded_count, &decoded_capacity, &counts);
			load_full = result == _ce_sound_full;
		}
		resident = result == _ce_sound_decoded;
		if (result == _ce_sound_full && ce_sound_entry_by_tag)
		{
			if (!ce_sound_full_told)
			{
				error(_error_silent, "Custom Edition maps: the decoded sounds' file is full (%lu MB) at %s: it and "
					"the sounds after it are decoded when they play", (unsigned long)CE_MAXIMUM_DECODED_SOUNDS_SIZE >> 20,
					ce_loaded_tag_name(instance));
				ce_sound_full_told = TRUE;
				full_told = TRUE;
			}
			writer.file = NULL;
			writer.position = 0;
			result = ce_sound_job_decode(&job, &writer, CE_DECODED_SOUNDS_PLACED_MOST - placed, &decoded,
				&decoded_count, &decoded_capacity, &counts);
		}
#endif
		length = writer.position - (writer.file ? written : 0);
		if (result != _ce_sound_decoded)
		{
			if (result == _ce_sound_full)
			{
				if (!full_told)
				{
					error(_error_silent, "Custom Edition maps: the decoded sounds' file is full (%lu MB): %s and the "
						"sounds after it that do not fit do not play", (unsigned long)CE_MAXIMUM_DECODED_SOUNDS_SIZE >> 20,
						ce_loaded_tag_name(instance));
					full_told = TRUE;
				}
				opensauce_survey_note_resource_miss("sound (no room in the decoded sounds' file)",
					ce_loaded_tag_name(instance));
			}
			ce_sound_job_free(&job);
#ifdef HALO_CONSOLE
			if (SetFilePointer(file, (long)written, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
			{
				error(_error_silent, "Custom Edition maps: cannot seek %s; no more Ogg Vorbis sounds decoded",
					path);
				break;
			}
#endif
			continue;
		}
#ifndef HALO_CONSOLE
		/* (kept, before its tags are changed: one that cannot be, with no
		memory for it, is left as it was, as one that does not fit on a
		console is; its tags would read past the file's end) */
		if (ce_sound_entry_by_tag)
		{
			long entry = ce_sound_entry_add(&job, placed, length, resident, written);

			if (entry == NONE)
			{
				error(_error_silent, "Custom Edition maps: %s: no memory to keep its decoded samples; it does not "
					"play", ce_loaded_tag_name(instance));
				ce_sound_job_free(&job);
				continue;
			}
			ce_sound_entry_by_tag[index] = entry;
		}
		else
			ce_sound_job_free(&job);
#endif
		for (decoded_index = 0; decoded_index < decoded_count; decoded_index++)
		{
			struct ce_decoded_permutation const *done = &decoded[decoded_index];
			unsigned long count;
			byte *permutations = ce_sound_block(pitch_ranges + done->range * CE_SOUND_PITCH_RANGE_SIZE +
				CE_PITCH_RANGE_PERMUTATIONS_OFFSET, CE_SOUND_PERMUTATION_SIZE, &count);
			byte *at = permutations + done->index * CE_SOUND_PERMUTATION_SIZE;
#ifdef HALO_CONSOLE
			unsigned long base = written;
#else
			unsigned long base = placed;
#endif

			*(long *)(at + CE_PERMUTATION_SAMPLES_OFFSET) = (long)done->size;
			*(unsigned long *)(at + CE_PERMUTATION_SAMPLES_OFFSET + 8) = base + done->offset;
			if (!ogg)
				continue;
			*(unsigned long *)(at + 0x38) = done->size;
			*(short *)(at + CE_PERMUTATION_COMPRESSION_OFFSET) = CE_SOUND_COMPRESSION_NONE;
		}
#ifdef HALO_CONSOLE
		ce_sound_job_free(&job);
		written += length;
#else
		if (!resident)
			deferred++;
		placed += length;
		if (resident)
			written += length;
#endif
		ce_indexed_tags[index] = CE_DECODED_SOUND;
		if (!ogg)
		{
			copied++;
#ifndef HALO_CONSOLE
			/* (and a 16-bit PCM one split there, as a decoded one is) */
			if (*(short *)(sound + CE_SOUND_COMPRESSION_OFFSET) == CE_SOUND_COMPRESSION_NONE)
				split_count += ce_sound_split(sound, pitch_ranges, pitch_range_count, ce_loaded_tag_name(instance));
#endif
			continue;
		}
		permutations_decoded += (long)decoded_count;
		*(short *)(sound + CE_SOUND_COMPRESSION_OFFSET) = CE_SOUND_COMPRESSION_NONE;
#ifndef HALO_CONSOLE
		/* (one longer than the sound cache holds, split: ce_sound_split) */
		split_count += ce_sound_split(sound, pitch_ranges, pitch_range_count, ce_loaded_tag_name(instance));
#endif
		/* (a 44 kHz mono sound, its samples now 22 kHz's) */
		if (!encoding && sample_rate)
			*(short *)(sound + CE_SOUND_SAMPLE_RATE_OFFSET) = 0;
		sounds++;
		if (from_map)
			in_map++;
	}
	/* (the game's free takes no NULL: a map whose sounds needed no decoding
	never made the list) */
	if (decoded)
		free(decoded);
	if (looped)
		free(looped);
	ce_decoded_sounds_file = file;
#ifndef HALO_CONSOLE
	ce_decoded_sounds_size = written;
#endif
	if (flags_cleared)
		error(_error_silent, "Custom Edition maps: %ld sounds had the loader's split flag set: cleared", flags_cleared);
	error(_error_silent, "Custom Edition maps: %ld long Ogg Vorbis permutations decoded a piece at a time, %ld "
		"permutations of more samples than the sound cache holds split into ones played one after another",
		counts.streamed, split_count);
	error(_error_silent, "Custom Edition maps: %ld Ogg Vorbis sounds decoded, %ld of them the map's (%ld permutations, "
		"%lu bytes; %ld permutations resampled to their sounds' rate, %ld of them with the permutations played "
		"next to them, %ld looped); %ld sounds given their permutations' compression; "
		"%ld with samples in both the map and sounds.map copied", sounds, in_map, permutations_decoded, written,
		counts.resampled, counts.resampled_joined, counts.resampled_looped, repaired, copied);
#ifndef HALO_CONSOLE
	if (deferred)
	{
		error(_error_silent, "Custom Edition maps: %ld sounds (%lu bytes) did not fit in the decoded sounds' file at "
			"load: decoded when they play", deferred, placed - written);
	}
#endif
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
resources (ce_models.c), or once that room is used, of the room above the
tag cache (platform.h; Halo PC kept what the port puts here, its models'
and level's buffers, outside its tag cache): their Xbox address, or 0 if
there is no room */
unsigned long ce_resources_allocate(
	unsigned long size)
{
	unsigned long room_base = CE_IMAGE_TAG_CACHE_BASE + CE_IMAGE_TAG_CACHE_SIZE;
	unsigned long room_end = room_base + CE_CONVERSION_ROOM;
	unsigned long address = ce_free_next;

	if (!address)
		return 0;
	if (!ce_room_next && address <= ce_free_end && size <= ce_free_end - address)
	{
		ce_free_next = (address + size + 15) & ~15UL;
		return address;
	}
	address = ce_room_next ? ce_room_next : room_base;
	if (address > room_end || size > room_end - address)
		return 0;
	ce_room_next = (address + size + 15) & ~15UL;
	return address;
}

/* size bytes (16-byte aligned) of the map's tag cache right after its tags,
for the tag validator to copy a root, block or data there whose bytes
another's overlap (tag_validate.c's tag_validate_room_proc), or for a long
sound's permutations (ce_sound_split), which it then checks: only before
anything else is placed past them, below its BSPs, and no more in all than
the map was checked to leave spare past its models' and BSP's buffers
(ce_map_checked_spare_room). The tags then end past it, for the
conversions (ce_loaded_pointer). Its Xbox address, or 0 if there is no room */
unsigned long ce_resources_allocate_tags(
	unsigned long size)
{
	unsigned long address = ce_free_next;
	unsigned long spare = ce_map_checked_spare_room();
	unsigned long aligned = (size + 15) & ~15UL;

	if (!address || ce_room_next || !size || aligned < size || address > ce_free_end ||
		aligned > ce_free_end - address || ce_tags_room_used > spare || aligned > spare - ce_tags_room_used)
	{
		return 0;
	}
	ce_free_next = address + aligned;
	ce_tags_room_used += aligned;
	ce_loaded_tags_end(ce_free_next);
	return address;
}

#ifndef HALO_CONSOLE
/* the same for a long sound's permutations (ce_sound_split), which come
before the tag validator's copies (which it decides alone, the same on
every platform, tag_validate.c's MAXIMUM_COPIED_BYTES) and so leave that
much of the spare room for them: a sound split or not never decides
whether a map's copies fit */
static unsigned long ce_resources_allocate_split(
	unsigned long size)
{
	unsigned long spare = ce_map_checked_spare_room();
	unsigned long aligned = (size + 15) & ~15UL;

	if (aligned < size || ce_tags_room_used > spare || spare - ce_tags_room_used < CE_TAG_COPIES_RESERVE ||
		aligned > spare - ce_tags_room_used - CE_TAG_COPIES_RESERVE)
	{
		return 0;
	}
	return ce_resources_allocate_tags(size);
}

#endif

/* where the next allocation goes (a BSP's: ce_bsp.c gives back what it took
when it unloads, ce_resources_free_to) */
unsigned long ce_resources_free_mark(
	void)
{
	return ce_room_next ? ce_room_next : ce_free_next;
}

void ce_resources_free_to(
	unsigned long mark)
{
	if (!mark)
		return;
	if (ce_room_next && mark >= CE_IMAGE_TAG_CACHE_BASE + CE_IMAGE_TAG_CACHE_SIZE)
	{
		if (mark <= ce_room_next)
			ce_room_next = mark;
	}
	else if (mark <= ce_free_next)
	{
		ce_free_next = mark;
		ce_room_next = 0;
	}
}

/* once a frame (main.c): the lines the map's loading left for the player
(ce_halo_pc_missing_tell), on each local player's HUD and the console a
second into the game */
void ce_resources_update(
	void)
{
	short local_player_index;
	short line;

	if (!ce_halo_pc_note[0][0] || !game_in_progress() || game_time_get() <= 30)
		return;
	console_warning("%s %s", ce_halo_pc_note[0], ce_halo_pc_note[1]);
	for (local_player_index = local_player_get_next(NONE); local_player_index != NONE;
		local_player_index = local_player_get_next(local_player_index))
	{
		/* (the newest line is the HUD's top one: the second first) */
		for (line = 1; line >= 0; line--)
		{
			wchar_t text[64];
			short index;

			for (index = 0; ce_halo_pc_note[line][index] && index < (short)NUMBEROF(text) - 1; index++)
				text[index] = (wchar_t)(unsigned char)ce_halo_pc_note[line][index];
			text[index] = 0;
			hud_print_message(local_player_index, text);
		}
	}
	ce_halo_pc_note[0][0] = ce_halo_pc_note[1][0] = 0;
}

/* the map unloaded (cache_files.c) */
void ce_resources_tags_unloaded(
	void)
{
#ifndef HALO_CONSOLE
	if (ce_sound_reloads || ce_sound_evictions)
	{
		error(_error_silent, "Custom Edition maps: %ld sounds decoded into the decoded sounds' file as they played, "
			"%ld moved out of it", ce_sound_reloads, ce_sound_evictions);
	}
	ce_sound_reloads = ce_sound_evictions = 0;
#endif
	ce_indexed_tag_count = 0;
	ce_free_next = ce_free_end = 0;
	ce_room_next = 0;
	ce_halo_pc_note[0][0] = ce_halo_pc_note[1][0] = 0;
	ce_loaded_tags_end(0);
	ce_loaded_bsps_low(0);
}

#endif
