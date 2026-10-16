/*
CE_ASSETS.C

A Custom Edition map's weapons, played on an Xbox map: an asset pack
(tools/ce_asset_pack.py) holds a few weapons of a Custom Edition map and
every tag they use, and is added to the Xbox map's tags as it loads, when
the game asks for it (ce_assets_wanted: Grifball, its sword, hammer and
ball, from grifball_assets.map in maps_ce).

The pack's tags are where the Custom Edition map had them, in Custom
Edition's tag cache, which an Xbox map leaves unused: their bytes are copied
back there, so nothing in them moves. They are then made the engine's as a
Custom Edition map's are (cache_files.c): their resources copied in from the
resource maps, gbxmodels made models, shaders, functions and HUD
interfaces made the Xbox's. The tags the pack keeps are added after the
map's (and the menus': menu_tags.c), each given the next index, and every
reference to one in them given its new index (tag_schema_remap_tags), with
each bitmap's and sound's own. The map's own tags keep theirs.

Their bitmaps' pixels and their sounds' samples are read from the pack, or
from the resource maps for those it names there (ce_assets_file_for_tag,
cache_files_windows.c), and drawn as a Custom Edition map's are (their
bitmaps are in Custom Edition's tag cache: ce_assets_contains).

Every machine in a game adds the same pack, so the same tags have the same
indices on each; which pack, and that it is the same, is not checked yet.
*/

#ifdef HALO_CUSTOM_EDITION

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "ce_map_checks.h"
#include "tag_schema.h"
#include "halo_map_families.h"

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

#define CE_ASSETS_SIGNATURE 'kpac'
#define CE_ASSETS_NAME "grifball_assets"

enum
{
	CE_ASSETS_VERSION = 1,
	CE_ASSETS_CACHE_VERSION = 609,
	CE_ASSETS_TAG_CACHE_BASE = 0x40440000,
	CE_ASSETS_TAG_CACHE_SIZE = 0x01700000,
	CE_ASSETS_MAXIMUM_CHUNKS = 4096,
	CE_ASSETS_MAXIMUM_TAGS = 0x8000,
	CE_ASSETS_MAXIMUM_ROLES = 8,
	/* (a bitmap group's bitmaps, a bitmap's tag index: bitmap_group.h) */
	BITMAP_GROUP_BITMAPS_OFFSET = 0x60,
	BITMAP_DATA_SIZE = 0x30,
	BITMAP_DATA_TAG_INDEX_OFFSET = 0x20,
	/* (a sound's pitch ranges, a range's permutations, a permutation's
	tag indices: sound_definitions.h) */
	SOUND_PITCH_RANGES_OFFSET = 0x98,
	PITCH_RANGE_SIZE = 0x48,
	PITCH_RANGE_PERMUTATIONS_OFFSET = 0x3c,
	PERMUTATION_SIZE = 0x7c,
	PERMUTATION_TAG_INDICES_OFFSET = 0x34,
};

/* ---------- structures */

struct ce_assets_header
{
	unsigned long signature;
	unsigned long version;
	unsigned long cache_version;
	unsigned long tag_cache_base;
	unsigned long tag_data_size;
	unsigned long tag_instances;
	unsigned long tag_count;
	unsigned long chunk_count;
	unsigned long chunks_offset;
	unsigned long model_data_offset;
	unsigned long model_data_size;
	unsigned long vertex_data_size;
	unsigned long role_count;
	unsigned long roles_offset;
	unsigned long checksum;
	unsigned long file_size;
};

struct ce_assets_chunk
{
	unsigned long address;
	unsigned long size;
	unsigned long offset;
};

struct ce_assets_role
{
	unsigned long role;
	unsigned long tag_index;
};

typedef char verify_ce_assets_header_size[sizeof(struct ce_assets_header) == 64 ? 1 : -1];

/* ---------- prototypes */

void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);
boolean ce_resources_tags_loaded(void *tag_instances, long tag_count, unsigned long first_free,
	unsigned long end_free);
void ce_resources_set_own_file(HANDLE file);
HANDLE ce_resources_file_for_tag(long tag_index);
unsigned long ce_resources_allocate(unsigned long size);
void ce_resources_tags_unloaded(void);
void ce_repairs_tags_loaded(void *tag_instances, long tag_count, unsigned long scenario_tag_index);
boolean ce_models_tags_loaded(void *tag_instances, long tag_count, byte const *model_data,
	unsigned long vertex_data_size);
void ce_models_tags_unloaded(void);
void ce_functions_tags_loaded(void *tag_instances, long tag_count);
void ce_shaders_tags_loaded(void *tag_instances, long tag_count);
void ce_hud_tags_loaded(void *tag_instances, long tag_count);
void ce_hud_tags_unloaded(void);
extern long ce_map_cache_version;
extern int platform_ce_tag_cache_ready;

boolean ce_assets_wanted(void);

/* ---------- globals */

static struct
{
	boolean loaded;
	HANDLE file;
	unsigned long checksum;
	/* the pack's tag table (its own indices) */
	struct ce_tag_instance *instances;
	long tag_count;
	/* each of its tags' index in the map's table (NONE: not kept), and
	each kept one's in the pack, in the table's order */
	long *absolute_indices;
	long *pack_indices;
	/* the first of them, and how many */
	long first_index;
	long count;
	/* its roles' tags (their new indices) */
	long role_count;
	struct ce_assets_role roles[CE_ASSETS_MAXIMUM_ROLES];
} ce_assets;

/* ---------- private code */

static boolean ce_assets_read(
	unsigned long offset,
	void *buffer,
	unsigned long size)
{
	unsigned long bytes_read = 0;

	if (SetFilePointer(ce_assets.file, (long)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return FALSE;
	return ReadFile(ce_assets.file, buffer, size, &bytes_read, NULL) && bytes_read == size;
}

static boolean ce_assets_fail(
	char const *why)
{
	error(_error_silent, "assets: %s.map not added: %s", CE_ASSETS_NAME, why);
	if (ce_assets.file)
		CloseHandle(ce_assets.file);
	free(ce_assets.absolute_indices);
	free(ce_assets.pack_indices);
	memset(&ce_assets, 0, sizeof(ce_assets));
	return FALSE;
}

/* (once their tags were made the engine's) */
static void ce_assets_made_fail(
	char const *why)
{
	ce_resources_set_own_file(NULL);
	ce_resources_tags_unloaded();
	ce_models_tags_unloaded();
	ce_hud_tags_unloaded();
	ce_assets_fail(why);
}

/* a tag index in the pack's tags made the one in the map's table (NONE: a
tag the pack does not keep) */
static long ce_assets_remap(
	long tag_index)
{
	long index = tag_index & 0xffff;

	if (tag_index == NONE || index >= ce_assets.tag_count ||
		(long)ce_assets.instances[index].tag_index != tag_index || ce_assets.absolute_indices[index] == NONE)
	{
		return NONE;
	}
	return ce_assets.absolute_indices[index];
}

/* the tag's own index in each bitmap of a bitmap group and each permutation
of a sound */
static void ce_assets_set_own_indices(
	unsigned long group_tag,
	byte *root,
	long tag_index)
{
	if (group_tag == 'bitm')
	{
		long count = *(long *)(root + BITMAP_GROUP_BITMAPS_OFFSET);
		byte *bitmaps = xbox_pointer(*(unsigned long *)(root + BITMAP_GROUP_BITMAPS_OFFSET + 4));
		long index;

		for (index = 0; bitmaps && index < count; index++)
			*(long *)(bitmaps + index * BITMAP_DATA_SIZE + BITMAP_DATA_TAG_INDEX_OFFSET) = tag_index;
	}
	else if (group_tag == 'snd!')
	{
		long range_count = *(long *)(root + SOUND_PITCH_RANGES_OFFSET);
		byte *ranges = xbox_pointer(*(unsigned long *)(root + SOUND_PITCH_RANGES_OFFSET + 4));
		long range;

		for (range = 0; ranges && range < range_count; range++)
		{
			byte *pitch_range = ranges + range * PITCH_RANGE_SIZE;
			long count = *(long *)(pitch_range + PITCH_RANGE_PERMUTATIONS_OFFSET);
			byte *permutations = xbox_pointer(*(unsigned long *)(pitch_range + PITCH_RANGE_PERMUTATIONS_OFFSET + 4));
			long index;

			for (index = 0; permutations && index < count; index++)
			{
				*(long *)(permutations + index * PERMUTATION_SIZE + PERMUTATION_TAG_INDICES_OFFSET) = tag_index;
				*(long *)(permutations + index * PERMUTATION_SIZE + PERMUTATION_TAG_INDICES_OFFSET + 4) = tag_index;
			}
		}
	}
}

/* ---------- public code */

/* the pack added to the map's tags just loaded, if the game wants it */
void ce_assets_tags_loaded(
	void)
{
	struct ce_assets_header header;
	struct ce_assets_chunk *chunks = NULL;
	char path[256];
	byte *tag_cache = xbox_pointer(CE_ASSETS_TAG_CACHE_BASE);
	struct cache_file_tag_instance_copy
	{
		unsigned long group_tag;
		unsigned long parent_group_tags[2];
		long tag_index;
		unsigned long name;
		unsigned long base_address;
		unsigned long unused[2];
	} *existing, *table;
	long existing_count, index, kept, salt;
	unsigned long table_address;
	byte *model_data;

	memset(&ce_assets, 0, sizeof(ce_assets));
	if (!ce_assets_wanted())
		return;
	if (!platform_ce_tag_cache_ready)
	{
		error(_error_silent, "assets: no tag cache for them");
		return;
	}
	if (!map_family_resource(CE_ASSETS_NAME, path, sizeof(path)))
	{
		error(_error_silent, "assets: there is no %s", path);
		return;
	}
	ce_assets.file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (ce_assets.file == INVALID_HANDLE_VALUE)
	{
		ce_assets.file = NULL;
		ce_assets_fail("it cannot be opened");
		return;
	}
	if (!ce_assets_read(0, &header, sizeof(header)) || header.signature != CE_ASSETS_SIGNATURE ||
		header.version != CE_ASSETS_VERSION || header.cache_version != CE_ASSETS_CACHE_VERSION ||
		header.tag_cache_base != CE_ASSETS_TAG_CACHE_BASE || header.tag_data_size > CE_ASSETS_TAG_CACHE_SIZE / 2 ||
		header.tag_count == 0 || header.tag_count > CE_ASSETS_MAXIMUM_TAGS ||
		header.tag_instances < CE_ASSETS_TAG_CACHE_BASE ||
		header.tag_instances - CE_ASSETS_TAG_CACHE_BASE > header.tag_data_size ||
		header.tag_count > (header.tag_data_size - (header.tag_instances - CE_ASSETS_TAG_CACHE_BASE)) / CE_TAG_INSTANCE_SIZE ||
		header.chunk_count == 0 || header.chunk_count > CE_ASSETS_MAXIMUM_CHUNKS ||
		header.role_count > CE_ASSETS_MAXIMUM_ROLES || header.model_data_size > 0x4000000 ||
		header.vertex_data_size > header.model_data_size)
	{
		ce_assets_fail("not an asset pack of this version");
		return;
	}
	ce_assets.checksum = header.checksum;

	/* the tags' bytes, where they were */
	memset(tag_cache, 0, header.tag_data_size);
	chunks = malloc(header.chunk_count * sizeof(*chunks));
	if (!chunks || !ce_assets_read(header.chunks_offset, chunks, header.chunk_count * sizeof(*chunks)))
	{
		free(chunks);
		ce_assets_fail("its chunks cannot be read");
		return;
	}
	for (index = 0; index < (long)header.chunk_count; index++)
	{
		unsigned long offset = chunks[index].address - CE_ASSETS_TAG_CACHE_BASE;

		if (chunks[index].address < CE_ASSETS_TAG_CACHE_BASE || offset > header.tag_data_size ||
			chunks[index].size > header.tag_data_size - offset ||
			!ce_assets_read(chunks[index].offset, tag_cache + offset, chunks[index].size))
		{
			free(chunks);
			ce_assets_fail("a chunk is damaged");
			return;
		}
	}
	free(chunks);
	if (header.role_count && !ce_assets_read(header.roles_offset, ce_assets.roles,
		header.role_count * sizeof(struct ce_assets_role)))
	{
		ce_assets_fail("its roles cannot be read");
		return;
	}
	ce_assets.role_count = (long)header.role_count;
	ce_assets.instances = xbox_pointer(header.tag_instances);
	ce_assets.tag_count = (long)header.tag_count;

	/* made the engine's, as a Custom Edition map's tags are (cache_files.c) */
	ce_map_cache_version = CE_ASSETS_CACHE_VERSION;
	ce_resources_set_own_file(ce_assets.file);
	if (!ce_resources_tags_loaded(ce_assets.instances, ce_assets.tag_count,
		CE_ASSETS_TAG_CACHE_BASE + header.tag_data_size, CE_ASSETS_TAG_CACHE_BASE + CE_ASSETS_TAG_CACHE_SIZE))
	{
		ce_resources_set_own_file(NULL);
		ce_assets_fail("their resources could not be loaded");
		return;
	}
	ce_repairs_tags_loaded(ce_assets.instances, ce_assets.tag_count, NONE);
	model_data = malloc(header.model_data_size ? header.model_data_size : 1);
	if (!model_data || !ce_assets_read(header.model_data_offset, model_data, header.model_data_size) ||
		!ce_models_tags_loaded(ce_assets.instances, ce_assets.tag_count, model_data, header.vertex_data_size))
	{
		error(_error_silent, "assets: their models could not all be made");
	}
	free(model_data);
	ce_functions_tags_loaded(ce_assets.instances, ce_assets.tag_count);
	ce_shaders_tags_loaded(ce_assets.instances, ce_assets.tag_count);
	ce_hud_tags_loaded(ce_assets.instances, ce_assets.tag_count);

	/* the kept tags after the map's, in a table in the tag cache */
	existing = cache_files_tag_instances(&existing_count);
	ce_assets.absolute_indices = malloc(ce_assets.tag_count * sizeof(long));
	ce_assets.pack_indices = malloc(ce_assets.tag_count * sizeof(long));
	if (!existing || !ce_assets.absolute_indices || !ce_assets.pack_indices)
	{
		ce_assets_made_fail("no room for their tags");
		return;
	}
	for (index = 0, kept = 0; index < ce_assets.tag_count; index++)
	{
		struct ce_tag_instance *instance = &ce_assets.instances[index];

		ce_assets.absolute_indices[index] = NONE;
		if (instance->group_tag == (unsigned long)NONE || !instance->base_address ||
			instance->base_address < CE_ASSETS_TAG_CACHE_BASE ||
			instance->base_address >= CE_ASSETS_TAG_CACHE_BASE + CE_ASSETS_TAG_CACHE_SIZE)
		{
			continue;
		}
		kept++;
	}
	if (existing_count + kept > UNSIGNED_SHORT_MAX ||
		!(table_address = ce_resources_allocate((existing_count + kept) * sizeof(*table))))
	{
		ce_assets_made_fail("no room for their tags");
		return;
	}
	table = xbox_pointer(table_address);
	memcpy(table, existing, existing_count * sizeof(*table));
	salt = existing_count ? (existing[0].tag_index >> 16) & 0xffff : 0xe174;
	ce_assets.first_index = existing_count;
	for (index = 0, kept = 0; index < ce_assets.tag_count; index++)
	{
		struct ce_tag_instance *instance = &ce_assets.instances[index];
		long absolute_index = existing_count + kept;
		struct cache_file_tag_instance_copy *copy = &table[absolute_index];

		if (instance->group_tag == (unsigned long)NONE || !instance->base_address ||
			instance->base_address < CE_ASSETS_TAG_CACHE_BASE ||
			instance->base_address >= CE_ASSETS_TAG_CACHE_BASE + CE_ASSETS_TAG_CACHE_SIZE)
		{
			continue;
		}
		copy->group_tag = instance->group_tag;
		copy->parent_group_tags[0] = instance->parent_group_tags[0];
		copy->parent_group_tags[1] = instance->parent_group_tags[1];
		copy->tag_index = (long)((((salt + absolute_index) & 0xffff) << 16) | absolute_index);
		copy->name = instance->name;
		copy->base_address = instance->base_address;
		copy->unused[0] = copy->unused[1] = 0;
		ce_assets.absolute_indices[index] = copy->tag_index;
		ce_assets.pack_indices[kept] = index;
		kept++;
	}
	ce_assets.count = kept;
	/* every reference in them given its new index, and each bitmap and
	sound its own */
	for (index = 0; index < ce_assets.tag_count; index++)
	{
		struct ce_tag_instance *instance = &ce_assets.instances[index];

		if (ce_assets.absolute_indices[index] == NONE)
			continue;
		if (!tag_schema_remap_tags(instance->group_tag, xbox_pointer(instance->base_address), tag_cache,
			CE_ASSETS_TAG_CACHE_SIZE, ce_assets_remap))
		{
			char group[5];

			group[0] = (char)(instance->group_tag >> 24);
			group[1] = (char)(instance->group_tag >> 16);
			group[2] = (char)(instance->group_tag >> 8);
			group[3] = (char)instance->group_tag;
			group[4] = 0;
			error(_error_silent, "assets: %s has no schema ('%s'): its references are not given new indices",
				(char const *)xbox_pointer(instance->name), group);
		}
		ce_assets_set_own_indices(instance->group_tag, xbox_pointer(instance->base_address),
			ce_assets.absolute_indices[index]);
	}
	for (index = 0; index < ce_assets.role_count; index++)
	{
		long tag = (long)ce_assets.roles[index].tag_index;

		ce_assets.roles[index].tag_index = (unsigned long)(tag >= 0 && tag < ce_assets.tag_count ?
			ce_assets.absolute_indices[tag] : NONE);
	}
	ce_resources_set_own_file(NULL);
	cache_files_set_tag_instances(table, existing_count + ce_assets.count);
	ce_assets.loaded = TRUE;
	error(_error_silent, "assets: %s.map (%08lx): %ld tags added after the map's %ld", CE_ASSETS_NAME,
		ce_assets.checksum, ce_assets.count, existing_count);
}

/* the map's tags unloaded (cache_files.c) */
void ce_assets_tags_unloaded(
	void)
{
	if (!ce_assets.loaded)
		return;
	ce_resources_tags_unloaded();
	ce_models_tags_unloaded();
	ce_hud_tags_unloaded();
	if (ce_assets.file)
		CloseHandle(ce_assets.file);
	free(ce_assets.absolute_indices);
	free(ce_assets.pack_indices);
	memset(&ce_assets, 0, sizeof(ce_assets));
}

/* whether a tag is one of the pack's */
boolean ce_assets_tag(
	long tag_index)
{
	long index = tag_index & 0xffff;

	return ce_assets.loaded && tag_index != NONE && index >= ce_assets.first_index &&
		index < ce_assets.first_index + ce_assets.count;
}

/* whether an address is in the pack's tags (a bitmap of theirs, drawn as a
Custom Edition map's: xbox_texture_cache.c) */
boolean ce_assets_contains(
	void const *pointer)
{
	unsigned long address;

	if (!ce_assets.loaded || !pointer)
		return FALSE;
	address = (unsigned long)xbox_address(pointer);
	return address >= CE_ASSETS_TAG_CACHE_BASE && address < CE_ASSETS_TAG_CACHE_BASE + CE_ASSETS_TAG_CACHE_SIZE;
}

/* the file a pack tag's pixels or samples are read from (cache_files_windows.c):
a resource map's, or the pack's */
HANDLE ce_assets_file_for_tag(
	long tag_index)
{
	HANDLE file;

	if (!ce_assets_tag(tag_index))
		return NULL;
	/* (by its index in the pack, which the resource maps' are by) */
	file = ce_resources_file_for_tag(ce_assets.pack_indices[(tag_index & 0xffff) - ce_assets.first_index]);
	return file ? file : ce_assets.file;
}

/* the pack's tag of a role (1: a weapon players get, 2: the ball), the
which'th of that role: NONE if there is none */
long ce_assets_role_tag(
	unsigned long role,
	long which)
{
	long index;

	for (index = 0; ce_assets.loaded && index < ce_assets.role_count; index++)
	{
		if (ce_assets.roles[index].role == role && which-- == 0)
			return (long)ce_assets.roles[index].tag_index;
	}
	return NONE;
}

#endif
