/*
TAG_IMPORT.C

Tags of another map, brought into the map that is loaded: the Flood of the
campaign's maps, for the multiplayer maps' infection (infection.c), whose
maps have none.

A map's tags are one block of memory, read where the map was built to have
it (the tag cache's address), with every pointer in it already an address:
a tag's name, the elements of its blocks, its data. What is not in that
block is read from the map's file as it is wanted, by its offset there (a
bitmap's pixels and a sound's samples: the texture and sound caches), or is
a table of the map's all of whose models' vertices and indices come before
the tags.

So a tag of another map is brought here with all it refers to (the tags it
names, down to their bitmaps and sounds), each copied to memory the loaded
map does not use (past its tags and before its level's geometry, which a map
loads at the far end of the tag cache), and in each copy:

- every pointer into the tag itself is moved to where the copy is. Nothing
  here knows a tag's fields, but a pointer of a tag is the address of a
  block's elements or of a data's bytes, which have the block's count before
  them or the data's size, and no definition after: an address inside the
  tag with those about it is one. (An animation's frames are the one thing
  of these tags that is not fields: they are left as they are.)
- every tag index is made the index of that tag here: of the map's own tag
  of that name where it has one (a map has many of these already: sounds,
  particles, bitmaps), else of the copy, added at the end of the map's tags.
- a bitmap's pixels, a sound's samples and a model's vertices and indices
  were taken from the other map when it was read: the caches are given the
  first two when they ask the map's file for them (tag_import_read), and
  the last are copied with the tags.

The other map is read (and decompressed) once, when first wanted; what was
taken from it is kept for every later map.

Every machine of a game must bring in the same: the host's objects are told
to its clients by their tags' indices (network_objects.c). The tags brought
in are added in the order of their names, so machines with the same maps
have them at the same indices, whichever campaign map each took them from.
*/

#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "cache/physical_memory_map.h"
#include "memory/zlib/zlib.h"
#include "models/model_animation_definitions.h"
#include "sound/sound_definitions.h"
#include "tag_files/tag_groups.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);

/* cache_files.c's and cache_files_windows.c's */
void *cache_files_tag_data_end(void);
void *cache_files_tag_data_limit(void);
void cache_files_set_tag_instances(void *instances, long count);
void cache_files_register_imported_buffers(void *vertex_buffer, void *vertex_data, void *index_buffer, void *index_data);
char const *cache_files_map_directory(void);

/* infection.c's: whether the game about to be played is one */
boolean infection_game_selected(void);

void tag_import_tags_loaded(void);
void tag_import_tags_unloaded(void);
void *tag_import_end(void);
boolean tag_import_read(long tag_index, long offset, long size, void *buffer);
boolean tag_import_available(void);

/* ---------- constants */

#define TAG_CACHE_ADDRESS 0x803A6000UL
#define TAG_CACHE_SIZE 0x01600000UL
/* where something in the game's memory is, as the tags name it: its address
on the Xbox, which is its pointer but on a 64-bit build (xbox_address.h;
back with xbox_pointer) */
#define ADDRESS_OF(pointer) ((unsigned long)POINTER_BITS(XBOX_ADDRESS(pointer)))

enum
{
	CACHE_HEADER_SIZE = 0x800,
	CACHE_HEADER_SIGNATURE = 'head',
	CACHE_FOOTER_SIGNATURE = 'foot',
	CACHE_TAGS_SIGNATURE = 'tags',
	CACHE_VERSION_XBOX = 5,
	/* (a campaign map is 0x11600000 at most: cache_files_windows.c) */
	CACHE_MAXIMUM_FILE_LENGTH = 0x11600000,

	/* a model's geometries, their parts, and a part's buffers (the model
	tag's layout in a map, which models/model_definitions.h has not) */
	MODEL_GEOMETRIES_OFFSET = 0xD0,
	MODEL_GEOMETRY_SIZE = 48,
	MODEL_GEOMETRY_PARTS_OFFSET = 36,
	MODEL_PART_SIZE = 104,
	MODEL_PART_INDEX_COUNT_OFFSET = 0x48,
	MODEL_PART_INDICES_OFFSET = 0x4C,
	MODEL_PART_INDEX_BUFFER_OFFSET = 0x50,
	MODEL_PART_VERTEX_COUNT_OFFSET = 0x58,
	MODEL_PART_VERTEX_BUFFER_OFFSET = 0x64,
	MODEL_VERTEX_SIZE = 32,
	/* (Direct3D's resource: Common, Data, Lock) */
	BUFFER_RESOURCE_SIZE = 12,

	MAXIMUM_IMPORT_TAGS = 512,
	MAXIMUM_IMPORT_RAWS = 1024,
	READ_CHUNK_SIZE = 0x40000,
};

enum
{
	_raw_bitmap_pixels,
	_raw_sound_samples,
	_raw_model_part,
};

/* ---------- structures */

/* a map's header, the header of its tags and a tag of them
(cache_files.c's, which are its own) */
struct import_cache_header
{
	unsigned long header_signature;
	long version;
	long file_length;
	byte reservedC[4];
	long tag_data_offset;
	long tag_data_size;
	byte reserved18[8];
	char name[0x20];
	char build[0x20];
	byte reserved60[0x79C];
	unsigned long footer_signature;
};

struct import_tag_header
{
	unsigned long tag_instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	unsigned long vertex_buffers;
	long index_buffer_count;
	unsigned long index_buffers;
	unsigned long signature;
};

struct import_tag_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	unsigned long tag_index;
	unsigned long name;
	unsigned long base_address;
	unsigned long unused[2];
};

typedef char verify_import_cache_header_size[sizeof(struct import_cache_header) == CACHE_HEADER_SIZE ? 1 : -1];
typedef char verify_import_tag_instance_size[sizeof(struct import_tag_instance) == 0x20 ? 1 : -1];

/* what a tag has outside the tags' memory */
struct import_raw
{
	short kind;
	/* where in the tag the thing it is of is (a bitmap, a sound's samples,
	a model's part) */
	long field_offset;
	long size;
	byte *data;
	/* (a model part's indices) */
	long size2;
	byte *data2;
};

/* a tag taken from the other map */
struct import_tag
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	/* its index and address in the map it is from */
	unsigned long datum;
	unsigned long address;
	long size;
	byte *data;
	char *name;
	/* a flag for each four bytes of it: not fields (an animation's frames) */
	byte *skip;
	long raw_count;
	struct import_raw *raws;
	/* the tags it names (indices of these) */
	long reference_count;
	short *references;

	/* in the map loaded now: the tag it is there (the map's own of its name,
	or its copy), NONE while it is not wanted */
	long loaded_index;
	boolean loaded_new;
	unsigned long loaded_address;
	unsigned long loaded_name;
};

/* the other map while it is being read */
struct import_source
{
	byte *image;
	long file_length;
	struct import_cache_header *header;
	struct import_tag_header *tag_header;
	struct import_tag_instance *instances;
	unsigned long *ends;
};

/* ---------- globals */

/* the tags wanted, with all they refer to */
static struct
{
	unsigned long group_tag;
	char const *name;
} const import_roots[] =
{
	{ 'bipd', "characters\\floodcombat_human\\floodcombat_human" },
};

/* the maps that have them, the smallest first */
static char const *const import_maps[] = { "c20", "d20", "c40", "c10", "d40" };

static struct
{
	boolean tried;
	boolean built;
	char map_name[32];
	long tag_count;
	struct import_tag tags[MAXIMUM_IMPORT_TAGS];
	/* the map's tags (by their place in its index) that are of these */
	long source_tag_count;
	short *by_source_index;

	/* in the map loaded now */
	long first_new_index;
	long new_count;
	byte *end;
	long live_raw_count;
	struct import_raw const *live_raws[MAXIMUM_IMPORT_RAWS];
} import = { 0 };

/* ---------- private code */

static voidpf import_zalloc(
	voidpf opaque,
	uInt items,
	uInt size)
{
	void *memory = malloc(items * size);

	(void)opaque;
	if (memory)
		csmemset(memory, 0, items * size);
	return memory;
}

static void import_zfree(
	voidpf opaque,
	voidpf address)
{
	(void)opaque;
	free(address);
}

/* the map's file, whole, as the game has it once copied to its cache: a
map on the disc is its header and then all the rest compressed */
static boolean source_read(
	struct import_source *source,
	char const *path)
{
	FILE *file = fopen(path, "rb");
	struct import_cache_header header;
	boolean result = FALSE;

	csmemset(source, 0, sizeof(*source));
	if (!file)
		return FALSE;
	if (fread(&header, 1, sizeof(header), file) == sizeof(header) &&
		header.header_signature == CACHE_HEADER_SIGNATURE && header.footer_signature == CACHE_FOOTER_SIGNATURE &&
		header.version == CACHE_VERSION_XBOX &&
		header.file_length > CACHE_HEADER_SIZE && header.file_length <= CACHE_MAXIMUM_FILE_LENGTH &&
		header.tag_data_offset >= CACHE_HEADER_SIZE && header.tag_data_size >= (long)sizeof(struct import_tag_header) &&
		header.tag_data_size <= (long)TAG_CACHE_SIZE &&
		header.tag_data_offset <= header.file_length - header.tag_data_size)
	{
		long size;

		fseek(file, 0, SEEK_END);
		size = ftell(file);
		fseek(file, CACHE_HEADER_SIZE, SEEK_SET);
		source->image = malloc(header.file_length);
		if (source->image)
		{
			csmemcpy(source->image, &header, sizeof(header));
			if (size >= header.file_length)
			{
				result = fread(source->image + CACHE_HEADER_SIZE, 1, header.file_length - CACHE_HEADER_SIZE, file) ==
					(size_t)(header.file_length - CACHE_HEADER_SIZE);
			}
			else
			{
				byte *chunk = malloc(READ_CHUNK_SIZE);
				z_stream stream;

				csmemset(&stream, 0, sizeof(stream));
				stream.zalloc = import_zalloc;
				stream.zfree = import_zfree;
				if (chunk && inflateInit(&stream) == Z_OK)
				{
					int status = Z_OK;

					stream.next_out = source->image + CACHE_HEADER_SIZE;
					stream.avail_out = header.file_length - CACHE_HEADER_SIZE;
					while (status == Z_OK && stream.avail_out)
					{
						if (!stream.avail_in)
						{
							stream.next_in = chunk;
							stream.avail_in = (uInt)fread(chunk, 1, READ_CHUNK_SIZE, file);
							if (!stream.avail_in)
								break;
						}
						status = inflate(&stream, Z_NO_FLUSH);
					}
					result = !stream.avail_out && (status == Z_OK || status == Z_STREAM_END);
					inflateEnd(&stream);
				}
				free(chunk);
			}
		}
	}
	fclose(file);

	if (result)
	{
		source->file_length = header.file_length;
		source->header = (struct import_cache_header *)source->image;
		source->tag_header = (struct import_tag_header *)(source->image + header.tag_data_offset);
		result = source->tag_header->signature == CACHE_TAGS_SIGNATURE &&
			source->tag_header->tag_count > 0 && source->tag_header->tag_count < 0x8000 &&
			source->tag_header->tag_instances >= TAG_CACHE_ADDRESS &&
			source->tag_header->tag_instances - TAG_CACHE_ADDRESS <=
				(unsigned long)header.tag_data_size - source->tag_header->tag_count * sizeof(struct import_tag_instance);
		if (result)
		{
			source->instances = (struct import_tag_instance *)((byte *)source->tag_header +
				(source->tag_header->tag_instances - TAG_CACHE_ADDRESS));
		}
	}
	if (!result)
	{
		free(source->image);
		csmemset(source, 0, sizeof(*source));
	}
	return result;
}

/* the map's memory at an address of its tags, if all of the size is of them */
static void *source_address(
	struct import_source const *source,
	unsigned long address,
	unsigned long size)
{
	unsigned long tag_data_size = (unsigned long)source->header->tag_data_size;

	if (address < TAG_CACHE_ADDRESS || address - TAG_CACHE_ADDRESS > tag_data_size ||
		size > tag_data_size - (address - TAG_CACHE_ADDRESS))
	{
		return NULL;
	}
	return (byte *)source->tag_header + (address - TAG_CACHE_ADDRESS);
}

static char const *source_string(
	struct import_source const *source,
	unsigned long address)
{
	char const *string = source_address(source, address, 1);
	char const *end = (char const *)source->tag_header + source->header->tag_data_size;
	char const *character;

	for (character = string; character && character < end; character++)
	{
		if (!*character)
			return string;
	}
	return NULL;
}

static int compare_addresses(
	void const *a,
	void const *b)
{
	unsigned long first = *(unsigned long const *)a;
	unsigned long second = *(unsigned long const *)b;

	return first < second ? -1 : first > second;
}

/* where each of the map's tags ends: where the next begins (a map's tags
are one after another) */
static boolean source_measure(
	struct import_source *source)
{
	long count = source->tag_header->tag_count;
	unsigned long *sorted = malloc(count * sizeof(unsigned long));
	unsigned long limit = TAG_CACHE_ADDRESS + source->header->tag_data_size;
	long index;

	source->ends = malloc(count * sizeof(unsigned long));
	if (!sorted || !source->ends)
	{
		free(sorted);
		return FALSE;
	}
	for (index = 0; index < count; index++)
		sorted[index] = source->instances[index].base_address;
	qsort(sorted, count, sizeof(unsigned long), compare_addresses);
	for (index = 0; index < count; index++)
	{
		unsigned long address = source->instances[index].base_address;
		long low = 0;
		long high = count;

		/* (the first address past this one) */
		while (low < high)
		{
			long middle = (low + high) / 2;

			if (sorted[middle] <= address)
				low = middle + 1;
			else
				high = middle;
		}
		source->ends[index] = low < count && sorted[low] < limit ? sorted[low] : limit;
		if (address < TAG_CACHE_ADDRESS || address >= limit)
			source->ends[index] = address;
	}
	free(sorted);
	return TRUE;
}

static long source_find(
	struct import_source const *source,
	unsigned long group_tag,
	char const *name)
{
	long index;

	for (index = 0; index < source->tag_header->tag_count; index++)
	{
		char const *tag_name = source->instances[index].group_tag == group_tag ?
			source_string(source, source->instances[index].name) : NULL;

		if (tag_name && !_stricmp(tag_name, name))
			return index;
	}
	return NONE;
}

/* the map's tag a value is the index of */
static long source_tag_of_datum(
	struct import_source const *source,
	unsigned long value)
{
	long index = (long)(value & 0xFFFF);

	return value != (unsigned long)NONE && index < source->tag_header->tag_count &&
		source->instances[index].tag_index == value && (value >> 16) ? index : NONE;
}

/* an animation graph's frames are not fields */
static void tag_skip_animation_data(
	struct import_tag *tag)
{
	struct animation_graph *graph = (struct animation_graph *)tag->data;
	unsigned long first = (unsigned long)graph->animations.address;
	long count = graph->animations.count;
	long animation_index;

	if (tag->size < (long)sizeof(*graph) || count <= 0 || first < tag->address ||
		first - tag->address > (unsigned long)tag->size ||
		(unsigned long)count > (tag->size - (first - tag->address)) / sizeof(struct animation))
	{
		return;
	}
	tag->skip = malloc(tag->size / 4 + 1);
	if (!tag->skip)
		return;
	csmemset(tag->skip, 0, tag->size / 4 + 1);
	for (animation_index = 0; animation_index < count; animation_index++)
	{
		struct animation *animation = (struct animation *)(tag->data + (first - tag->address)) + animation_index;
		struct tag_data const *datas[3];
		long data_index;

		datas[0] = &animation->frame_info;
		datas[1] = &animation->default_data;
		datas[2] = &animation->data;
		for (data_index = 0; data_index < 3; data_index++)
		{
			unsigned long address = (unsigned long)datas[data_index]->address;
			long size = datas[data_index]->size;

			if (size > 0 && address >= tag->address && address - tag->address <= (unsigned long)tag->size &&
				(unsigned long)size <= tag->size - (address - tag->address))
			{
				long begin = (long)(address - tag->address + 3) / 4;
				long end = (long)(address - tag->address + size) / 4;

				if (end > begin)
					csmemset(tag->skip + begin, 1, end - begin);
			}
		}
	}
}

static struct import_raw *tag_raw_new(
	struct import_tag *tag,
	short kind,
	long field_offset)
{
	struct import_raw *raws = realloc(tag->raws, (tag->raw_count + 1) * sizeof(struct import_raw));
	struct import_raw *raw;

	if (!raws)
		return NULL;
	tag->raws = raws;
	raw = &raws[tag->raw_count++];
	csmemset(raw, 0, sizeof(*raw));
	raw->kind = kind;
	raw->field_offset = field_offset;
	return raw;
}

static byte *copy_bytes(
	void const *bytes,
	long size)
{
	byte *copy = bytes && size > 0 ? malloc(size) : NULL;

	if (copy)
		csmemcpy(copy, bytes, size);
	return copy;
}

/* the elements of a block of a tag taken, where they are in its bytes */
static void *tag_block_elements(
	struct import_tag const *tag,
	struct tag_block const *block,
	long element_size)
{
	unsigned long address = (unsigned long)block->address;

	if (block->count <= 0 || block->count > 0xFFFF || address < tag->address ||
		address - tag->address > (unsigned long)tag->size ||
		(unsigned long)block->count * element_size > tag->size - (address - tag->address))
	{
		return NULL;
	}
	return tag->data + (address - tag->address);
}

/* what the tag has in the map's file past its tags, and a model's buffers */
static boolean tag_take_raws(
	struct import_tag *tag,
	struct import_source const *source)
{
	if (tag->group_tag == 'bitm' && tag->size >= (long)sizeof(struct bitmap_group))
	{
		struct bitmap_group *group = (struct bitmap_group *)tag->data;
		struct bitmap_data *bitmaps = tag_block_elements(tag, &group->bitmaps, sizeof(struct bitmap_data));
		long index;

		for (index = 0; bitmaps && index < group->bitmaps.count; index++)
		{
			struct bitmap_data *bitmap = &bitmaps[index];
			struct import_raw *raw;

			if (bitmap->pixels_size <= 0 || bitmap->pixels_offset < CACHE_HEADER_SIZE ||
				bitmap->pixels_offset > source->file_length - bitmap->pixels_size)
			{
				return FALSE;
			}
			raw = tag_raw_new(tag, _raw_bitmap_pixels, (long)((byte *)bitmap - tag->data));
			if (!raw)
				return FALSE;
			raw->size = bitmap->pixels_size;
			raw->data = copy_bytes(source->image + bitmap->pixels_offset, raw->size);
			if (!raw->data)
				return FALSE;
		}
	}
	else if (tag->group_tag == 'snd!' && tag->size >= (long)sizeof(struct sound_definition))
	{
		struct sound_definition *sound = (struct sound_definition *)tag->data;
		struct sound_pitch_range *ranges = tag_block_elements(tag, &sound->pitch_ranges, sizeof(struct sound_pitch_range));
		long range_index;

		for (range_index = 0; ranges && range_index < sound->pitch_ranges.count; range_index++)
		{
			struct sound_permutation *permutations = tag_block_elements(tag, &ranges[range_index].permutations,
				sizeof(struct sound_permutation));
			long index;

			for (index = 0; permutations && index < ranges[range_index].permutations.count; index++)
			{
				struct tag_data *samples = &permutations[index].samples;
				struct import_raw *raw;

				if (samples->size <= 0)
					continue;
				if (samples->file_offset < CACHE_HEADER_SIZE || samples->file_offset > source->file_length - samples->size)
					return FALSE;
				raw = tag_raw_new(tag, _raw_sound_samples, (long)((byte *)samples - tag->data));
				if (!raw)
					return FALSE;
				raw->size = samples->size;
				raw->data = copy_bytes(source->image + samples->file_offset, raw->size);
				if (!raw->data)
					return FALSE;
			}
		}
	}
	else if (tag->group_tag == 'mode' && tag->size >= MODEL_GEOMETRIES_OFFSET + (long)sizeof(struct tag_block))
	{
		struct tag_block *geometries = (struct tag_block *)(tag->data + MODEL_GEOMETRIES_OFFSET);
		byte *geometry = tag_block_elements(tag, geometries, MODEL_GEOMETRY_SIZE);
		long geometry_index;

		for (geometry_index = 0; geometry && geometry_index < geometries->count; geometry_index++, geometry += MODEL_GEOMETRY_SIZE)
		{
			struct tag_block *parts = (struct tag_block *)(geometry + MODEL_GEOMETRY_PARTS_OFFSET);
			byte *part = tag_block_elements(tag, parts, MODEL_PART_SIZE);
			long part_index;

			for (part_index = 0; part && part_index < parts->count; part_index++, part += MODEL_PART_SIZE)
			{
				long index_count = *(long *)(part + MODEL_PART_INDEX_COUNT_OFFSET);
				long vertex_count = *(long *)(part + MODEL_PART_VERTEX_COUNT_OFFSET);
				unsigned long indices = *(unsigned long *)(part + MODEL_PART_INDICES_OFFSET);
				/* (a buffer of the map's table: Direct3D's resource, whose data is
				the address of the vertices) */
				unsigned long *vertex_buffer = source_address(source,
					*(unsigned long *)(part + MODEL_PART_VERTEX_BUFFER_OFFSET), BUFFER_RESOURCE_SIZE);
				struct import_raw *raw;
				void *bytes;

				if (index_count <= 0 || index_count > 0xFFFF || vertex_count <= 0 || vertex_count > 0xFFFF || !vertex_buffer)
					return FALSE;
				raw = tag_raw_new(tag, _raw_model_part, (long)(part - tag->data));
				if (!raw)
					return FALSE;
				raw->size = vertex_count * MODEL_VERTEX_SIZE;
				bytes = source_address(source, vertex_buffer[1], raw->size);
				raw->data = copy_bytes(bytes, raw->size);
				/* (a strip of triangles: two indices more than triangles) */
				raw->size2 = (index_count + 2) * (long)sizeof(word);
				bytes = source_address(source, indices, raw->size2);
				raw->data2 = copy_bytes(bytes, raw->size2);
				if (!raw->data || !raw->data2)
					return FALSE;
			}
		}
	}
	return TRUE;
}

static void pack_dispose(
	void)
{
	long tag_index;

	for (tag_index = 0; tag_index < import.tag_count; tag_index++)
	{
		struct import_tag *tag = &import.tags[tag_index];
		long raw_index;

		for (raw_index = 0; raw_index < tag->raw_count; raw_index++)
		{
			free(tag->raws[raw_index].data);
			free(tag->raws[raw_index].data2);
		}
		free(tag->raws);
		free(tag->data);
		free(tag->name);
		free(tag->skip);
		free(tag->references);
	}
	free(import.by_source_index);
	import.by_source_index = NULL;
	import.tag_count = 0;
	import.built = FALSE;
}

/* the wanted tags of a map and all they refer to, taken from it */
static boolean pack_build_from(
	struct import_source *source)
{
	long count = source->tag_header->tag_count;
	long *stack = malloc(count * sizeof(long));
	long stack_count = 0;
	long root_index;
	long tag_index;
	boolean result = TRUE;

	import.source_tag_count = count;
	import.by_source_index = malloc(count * sizeof(short));
	if (!stack || !import.by_source_index)
	{
		free(stack);
		return FALSE;
	}
	for (tag_index = 0; tag_index < count; tag_index++)
		import.by_source_index[tag_index] = NONE;

	for (root_index = 0; root_index < (long)NUMBEROF(import_roots); root_index++)
	{
		long found = source_find(source, import_roots[root_index].group_tag, import_roots[root_index].name);

		if (found == NONE)
		{
			free(stack);
			return FALSE;
		}
		stack[stack_count++] = found;
	}

	/* each tag reached is taken, and what it names is reached */
	while (result && stack_count)
	{
		long source_index = stack[--stack_count];
		struct import_tag_instance const *instance = &source->instances[source_index];
		long size = (long)(source->ends[source_index] - instance->base_address);
		byte const *bytes = source_address(source, instance->base_address, size);
		char const *name = source_string(source, instance->name);
		struct import_tag *tag;
		long offset;

		if (import.by_source_index[source_index] != NONE)
			continue;
		if (!bytes || size <= 0 || !name || import.tag_count >= MAXIMUM_IMPORT_TAGS)
		{
			result = FALSE;
			break;
		}
		tag = &import.tags[import.tag_count];
		csmemset(tag, 0, sizeof(*tag));
		import.by_source_index[source_index] = (short)import.tag_count++;
		tag->group_tag = instance->group_tag;
		tag->parent_group_tags[0] = instance->parent_group_tags[0];
		tag->parent_group_tags[1] = instance->parent_group_tags[1];
		tag->datum = instance->tag_index;
		tag->address = instance->base_address;
		tag->size = size & ~3;
		tag->data = copy_bytes(bytes, size);
		tag->name = copy_bytes(name, (long)strlen(name) + 1);
		tag->loaded_index = NONE;
		if (!tag->data || !tag->name)
		{
			result = FALSE;
			break;
		}
		if (tag->group_tag == 'antr')
			tag_skip_animation_data(tag);
		if (!tag_take_raws(tag, source))
		{
			result = FALSE;
			break;
		}
		for (offset = 0; offset + 4 <= tag->size; offset += 4)
		{
			long named = tag->skip && tag->skip[offset / 4] ? NONE :
				source_tag_of_datum(source, *(unsigned long *)(tag->data + offset));

			if (named != NONE && import.by_source_index[named] == NONE && stack_count < count)
				stack[stack_count++] = named;
		}
	}
	free(stack);

	/* the tags each names, now that all are known */
	for (tag_index = 0; result && tag_index < import.tag_count; tag_index++)
	{
		struct import_tag *tag = &import.tags[tag_index];
		long offset;

		for (offset = 0; offset + 4 <= tag->size; offset += 4)
		{
			long named = tag->skip && tag->skip[offset / 4] ? NONE :
				source_tag_of_datum(source, *(unsigned long *)(tag->data + offset));

			if (named != NONE && import.by_source_index[named] != NONE && import.by_source_index[named] != tag_index)
			{
				short *references = realloc(tag->references, (tag->reference_count + 1) * sizeof(short));

				if (!references)
				{
					result = FALSE;
					break;
				}
				tag->references = references;
				references[tag->reference_count++] = import.by_source_index[named];
			}
		}
	}
	return result;
}

static void pack_build(
	void)
{
	long map_index;

	import.tried = TRUE;
	for (map_index = 0; !import.built && map_index < (long)NUMBEROF(import_maps); map_index++)
	{
		struct import_source source;
		char path[256];

		snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), import_maps[map_index]);
		if (!source_read(&source, path))
			continue;
		if (source_measure(&source) && pack_build_from(&source))
		{
			long bytes = 0;
			long raw_bytes = 0;
			long tag_index;

			import.built = TRUE;
			csstrncpy(import.map_name, import_maps[map_index], sizeof(import.map_name) - 1);
			for (tag_index = 0; tag_index < import.tag_count; tag_index++)
			{
				long raw_index;

				bytes += import.tags[tag_index].size;
				for (raw_index = 0; raw_index < import.tags[tag_index].raw_count; raw_index++)
					raw_bytes += import.tags[tag_index].raws[raw_index].size + import.tags[tag_index].raws[raw_index].size2;
			}
			platform_log("flood: %ld tags taken from %s.map (%ld KB, and %ld KB of textures, sounds and models)",
				import.tag_count, import.map_name, bytes / 1024, raw_bytes / 1024);
		}
		else
		{
			pack_dispose();
		}
		free(source.ends);
		free(source.image);
	}
	if (!import.built)
	{
		platform_log("flood: no campaign map with the Flood in the maps folder (c20, d20, c40, c10 or d40): "
			"infection's zombies are players as they are");
	}
}

static int compare_new_tags(
	void const *a,
	void const *b)
{
	struct import_tag const *first = *(struct import_tag const *const *)a;
	struct import_tag const *second = *(struct import_tag const *const *)b;

	if (first->group_tag != second->group_tag)
		return first->group_tag < second->group_tag ? -1 : 1;
	return _stricmp(first->name, second->name);
}

/* the tag of those taken that a value is the other map's index of */
static struct import_tag *pack_tag_of_datum(
	unsigned long value)
{
	long index = (long)(value & 0xFFFF);
	short pack_index = index < import.source_tag_count ? import.by_source_index[index] : NONE;

	return pack_index != NONE && import.tags[pack_index].datum == value ? &import.tags[pack_index] : NULL;
}

/* a tag's copy here made this map's: its pointers, the tags it names */
static long tag_relocate(
	struct import_tag const *tag,
	byte *copy)
{
	long unknown_count = 0;
	long offset;

	for (offset = 0; offset + 4 <= tag->size; offset += 4)
	{
		unsigned long *value = (unsigned long *)(copy + offset);

		if (tag->skip && tag->skip[offset / 4])
			continue;
		if (*value >= tag->address && *value - tag->address < (unsigned long)tag->size)
		{
			/* a block's elements (a count before) or a data's bytes (its size,
			flags and file offset before), with no definition after */
			unsigned long before = offset >= 4 ? value[-1] : 0;
			unsigned long after = offset + 8 <= tag->size ? value[1] : 1;
			unsigned long data_size = offset >= 12 ? value[-3] : 0;

			if (!after && ((before > 0 && before <= 0xFFFF) ||
				(data_size > 0 && data_size <= (unsigned long)tag->size - (*value - tag->address))))
			{
				*value = *value - tag->address + tag->loaded_address;
			}
			else
			{
				unknown_count++;
			}
		}
		else
		{
			struct import_tag const *named = pack_tag_of_datum(*value);

			if (named)
			{
				*value = named->loaded_index;
				/* (a reference: the group, the name, its length, the index) */
				if (offset >= 12 && named->loaded_index != NONE &&
					(value[-3] == named->group_tag || value[-3] == named->parent_group_tags[0] ||
						value[-3] == named->parent_group_tags[1]))
				{
					value[-2] = named->loaded_name;
				}
			}
		}
	}
	return unknown_count;
}

/* ---------- public code */

/* whether the tags can be had at all (the maps folder has a map with them) */
boolean tag_import_available(
	void)
{
	if (!import.tried)
		pack_build();
	return import.built;
}

/* the map's tags are loaded (scenario_tags_load): the Flood is brought in
if the game is to be infection */
void tag_import_tags_loaded(
	void)
{
	struct import_tag_header *header = physical_memory_get_tag_cache_base_address();
	struct import_tag_instance *instances = (struct import_tag_instance *)xbox_pointer(header->tag_instances);
	struct import_tag *new_tags[MAXIMUM_IMPORT_TAGS];
	short stack[MAXIMUM_IMPORT_TAGS];
	long stack_count = 0;
	long new_count = 0;
	long old_count = header->tag_count;
	long unknown_count = 0;
	long tag_index;
	long root_index;
	long new_index;
	unsigned long cursor;
	unsigned long limit;
	unsigned long instances_address;
	unsigned long names_address;
	unsigned long buffers_address;

	import.first_new_index = NONE;
	import.new_count = 0;
	import.end = NULL;
	import.live_raw_count = 0;
	if (!infection_game_selected() || !tag_import_available())
		return;

	/* the tags wanted and what they name, down to tags the map has itself */
	for (tag_index = 0; tag_index < import.tag_count; tag_index++)
	{
		import.tags[tag_index].loaded_index = NONE;
		import.tags[tag_index].loaded_new = FALSE;
	}
	for (root_index = 0; root_index < (long)NUMBEROF(import_roots); root_index++)
	{
		for (tag_index = 0; tag_index < import.tag_count; tag_index++)
		{
			if (import.tags[tag_index].group_tag == import_roots[root_index].group_tag &&
				!_stricmp(import.tags[tag_index].name, import_roots[root_index].name))
			{
				stack[stack_count++] = (short)tag_index;
			}
		}
	}
	while (stack_count)
	{
		struct import_tag *tag = &import.tags[stack[--stack_count]];
		long reference_index;

		if (tag->loaded_index != NONE || tag->loaded_new)
			continue;
		tag->loaded_index = tag_loaded(tag->group_tag, tag->name);
		if (tag->loaded_index != NONE)
		{
			tag->loaded_name = ADDRESS_OF(tag_get_name(tag->loaded_index));
			continue;
		}
		tag->loaded_new = TRUE;
		new_tags[new_count++] = tag;
		for (reference_index = 0; reference_index < tag->reference_count; reference_index++)
		{
			struct import_tag const *named = &import.tags[tag->references[reference_index]];

			if (named->loaded_index == NONE && !named->loaded_new && stack_count < MAXIMUM_IMPORT_TAGS)
				stack[stack_count++] = tag->references[reference_index];
		}
	}
	if (!new_count)
		return;
	qsort(new_tags, new_count, sizeof(new_tags[0]), compare_new_tags);

	/* where all of it goes: the map's tags and these, their names, these
	tags, their models' buffers */
	limit = ADDRESS_OF(cache_files_tag_data_limit());
	cursor = (ADDRESS_OF(cache_files_tag_data_end()) + 31) & ~31UL;
	instances_address = cursor;
	cursor += (old_count + new_count) * sizeof(struct import_tag_instance);
	names_address = cursor;
	for (new_index = 0; new_index < new_count; new_index++)
		cursor += (unsigned long)strlen(new_tags[new_index]->name) + 1;
	cursor = (cursor + 3) & ~3UL;
	for (new_index = 0; new_index < new_count; new_index++)
	{
		struct import_tag *tag = new_tags[new_index];

		tag->loaded_address = cursor;
		cursor += (tag->size + 3) & ~3UL;
	}
	buffers_address = cursor;
	for (new_index = 0; new_index < new_count; new_index++)
	{
		struct import_tag const *tag = new_tags[new_index];
		long raw_index;

		for (raw_index = 0; raw_index < tag->raw_count; raw_index++)
		{
			if (tag->raws[raw_index].kind == _raw_model_part)
			{
				cursor += 2 * BUFFER_RESOURCE_SIZE + ((tag->raws[raw_index].size + 3) & ~3UL) +
					((tag->raws[raw_index].size2 + 3) & ~3UL);
			}
		}
	}
	if (cursor > limit || old_count + new_count > 0x7FFF)
	{
		platform_log("flood: no room in this map's tags for %ld more (%lu KB short)", new_count, (cursor - limit) / 1024 + 1);
		for (new_index = 0; new_index < new_count; new_index++)
			new_tags[new_index]->loaded_new = FALSE;
		return;
	}

	/* the index: the map's tags, then these */
	csmemset(xbox_pointer(instances_address), 0, cursor - instances_address);
	csmemcpy(xbox_pointer(instances_address), instances, old_count * sizeof(struct import_tag_instance));
	instances = (struct import_tag_instance *)xbox_pointer(instances_address);
	cursor = names_address;
	for (new_index = 0; new_index < new_count; new_index++)
	{
		struct import_tag *tag = new_tags[new_index];
		struct import_tag_instance *instance = &instances[old_count + new_index];
		long index = old_count + new_index;
		long length = (long)strlen(tag->name) + 1;

		/* (a tag's index is its place and, above that, a number that goes up
		by one each tag from the first's: as the map's own are) */
		tag->loaded_index = (long)((((instances[0].tag_index >> 16) + index) & 0xFFFF) << 16 | index);
		tag->loaded_name = cursor;
		csmemcpy(xbox_pointer(cursor), tag->name, length);
		cursor += length;
		instance->group_tag = tag->group_tag;
		instance->parent_group_tags[0] = tag->parent_group_tags[0];
		instance->parent_group_tags[1] = tag->parent_group_tags[1];
		instance->tag_index = tag->loaded_index;
		instance->name = tag->loaded_name;
		instance->base_address = tag->loaded_address;
	}

	/* the tags */
	cursor = buffers_address;
	for (new_index = 0; new_index < new_count; new_index++)
	{
		struct import_tag *tag = new_tags[new_index];
		byte *copy = (byte *)xbox_pointer(tag->loaded_address);
		long raw_index;

		csmemcpy(copy, tag->data, tag->size);
		unknown_count += tag_relocate(tag, copy);
		for (raw_index = 0; raw_index < tag->raw_count; raw_index++)
		{
			struct import_raw const *raw = &tag->raws[raw_index];
			byte *field = copy + raw->field_offset;

			if (raw->kind == _raw_model_part)
			{
				unsigned long vertex_buffer = cursor;
				unsigned long index_buffer = cursor + BUFFER_RESOURCE_SIZE;
				unsigned long vertices = cursor + 2 * BUFFER_RESOURCE_SIZE;
				unsigned long indices = vertices + ((raw->size + 3) & ~3UL);

				cursor = indices + ((raw->size2 + 3) & ~3UL);
				csmemcpy(xbox_pointer(vertices), raw->data, raw->size);
				csmemset(xbox_pointer(indices), 0xFF, (raw->size2 + 3) & ~3UL);
				csmemcpy(xbox_pointer(indices), raw->data2, raw->size2);
				cache_files_register_imported_buffers(xbox_pointer(vertex_buffer), xbox_pointer(vertices),
					xbox_pointer(index_buffer), xbox_pointer(indices));
				*(unsigned long *)(field + MODEL_PART_INDICES_OFFSET) = indices;
				*(unsigned long *)(field + MODEL_PART_INDEX_BUFFER_OFFSET) = index_buffer;
				*(unsigned long *)(field + MODEL_PART_VERTEX_BUFFER_OFFSET) = vertex_buffer;
			}
			else if (import.live_raw_count < MAXIMUM_IMPORT_RAWS)
			{
				/* (the caches ask the map's file for these at an offset: the
				offset is which of these it is, tag_import_read) */
				if (raw->kind == _raw_bitmap_pixels)
				{
					struct bitmap_data *bitmap = (struct bitmap_data *)field;

					bitmap->pixels_offset = import.live_raw_count;
					bitmap->cache_block_index = NONE;
					bitmap->hardware_format = XBOX_NULL;
					bitmap->base_address = XBOX_NULL;
				}
				else
				{
					((struct tag_data *)field)->file_offset = import.live_raw_count;
				}
				import.live_raws[import.live_raw_count++] = raw;
			}
		}
	}

	cache_files_set_tag_instances(instances, old_count + new_count);
	import.first_new_index = old_count;
	import.new_count = new_count;
	import.end = (byte *)xbox_pointer(cursor);
	platform_log("flood: %ld tags brought into this map from %s.map (%lu KB)%s", new_count, import.map_name,
		(cursor - instances_address) / 1024, unknown_count ? ", with pointers not understood" : "");
}

void tag_import_tags_unloaded(
	void)
{
	import.first_new_index = NONE;
	import.new_count = 0;
	import.end = NULL;
	import.live_raw_count = 0;
}

/* where what was brought in ends, NULL when nothing was: a map's level
geometry is loaded past it (scenario_structure_bsp_load clears what is
before the geometry) */
void *tag_import_end(
	void)
{
	return import.end;
}

/* the texture and sound caches' read of a tag's pixels or samples from the
map's file (cache_file_read): those of a tag brought in are here */
boolean tag_import_read(
	long tag_index,
	long offset,
	long size,
	void *buffer)
{
	long index = (long)(tag_index & 0xFFFF);

	if (tag_index == NONE || import.first_new_index == NONE || index < import.first_new_index ||
		index >= import.first_new_index + import.new_count)
	{
		return FALSE;
	}
	if (offset >= 0 && offset < import.live_raw_count)
	{
		struct import_raw const *raw = import.live_raws[offset];

		csmemcpy(buffer, raw->data, MIN(size, raw->size));
	}
	return TRUE;
}
