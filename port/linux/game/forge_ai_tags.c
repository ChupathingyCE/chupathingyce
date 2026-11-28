/*
FORGE_AI_TAGS.C

AI characters on the multiplayer maps in Forge: their tags brought in from
the player's own campaign maps once a Forge game (or a layout played) of a
multiplayer map has loaded, beside the map's own. A multiplayer map has no
actor variants; a mission has them, with everything they need (their
units, their weapons, models, animations, sounds, effects, actors). Halo CE
Vita did this with "scene packs" built on the machine into pack files; this
is the smallest version of that idea, read again at each such load: no tag
file is written, and nothing is kept past the map.

Which characters (forge_ai_factions): a faction's actor variants, from the
first of its missions in the maps folder: the Covenant from The Silent
Cartographer, Truth and Reconciliation, Assault on the Control Room or
Keyes; the Marines from Halo, Assault on the Control Room or The Silent
Cartographer; the Flood and the Sentinels from The Library, Keyes, 343
Guilty Spark or The Maw. A faction none of whose missions is there is not
offered (the Forge menu says which). Only the Xbox's maps (cache version 5)
are read.

How (forge_ai_tags_loaded notes the map at the end of scenario_tags_load;
forge_ai_tags_update brings them in at the first update after it, before
the Forge session starts - a map's load leaves a 32-bit build no memory to
spare until it is done, and if there is still none it is tried again each
second for a while):
1. the mission's file is read and expanded (zlib) up to the end of its tag
   data, which is read into one room (the largest mission's size, made
   first and used for each mission in turn);
2. every pointer of its tags is moved from the tag cache's address to that
   room's (the schema walk: tag_schema.h; the tag table, the vertex and
   index buffers, the models' parts' buffers), each one checked to lie in
   the tags, else the mission is refused;
3. the tags are checked by the game's own validator as a map's are
   (tag_validate_other_tags, the loaded map's check state kept): anything
   it would refuse a map for, the mission is refused for;
4. the faction's actor variants and every tag they reach (references and
   tag indices, by the schema) are taken; a tag the multiplayer map has of
   the same group and name is the map's (a weapon both have), a dialogue
   (udlg) none;
5. the bytes those tags take (their roots, blocks, data, names, their
   models' buffers, vertices and indices) are copied end to end into the
   tag cache's room past the map's tags, which its structure bsps' loads
   leave alone (cache_files.c), or the heap's if there is no room; every
   pointer is made the copy's, every reference and tag index the game's
   table's;
6. those tags are added to the map's tag table after its own (and the
   menus'), their models' buffers registered;
7. the mission's file is read again for the bitmaps' pixels and the sounds'
   samples those tags name, into a data file in the cache folder: the
   texture and sound caches' reads of them (cache_file_read) are served
   from it (forge_ai_tags_read).
forge_ai_tags_map_unloaded lets all of it go (the data files deleted)
before the map's tags go, after the texture and sound caches; data files a
crash left behind are deleted at the next start (the first map's load).

Crafted input: a mission's file is anyone's. Its header's sizes are bounded,
every pointer the walks follow must lie in its tags, every tag index must
name a tag of its table, and the validator checks the rest before a tag of
it is used.

Off unless Forge is on and this is a Forge game (or a layout played) of a
multiplayer map (forge_ai_tags_wanted): nothing of this runs otherwise.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "halo_port_features.h"

#ifndef HALO_CONSOLE_NO_FORGE

#include "tag_files/tag_groups.h"
#include "cache/cache_files.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "sound/sound_definitions.h"
#include "rasterizer/rasterizer_geometry.h"
#include "models/model_definitions.h"
#include "tag_schema.h"
#include "forge_ai_tags.h"
#include "../../third_party/zlib/zlib_prefixed.h"

#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);
char const *cache_files_map_directory(void);
void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);
/* forge.c's: whether this map's load is a Forge game's of a multiplayer
map, and a mission's name as Forge says it */
boolean forge_ai_tags_wanted(char const *scenario_name);
char const *forge_mission_title(char const *short_name);

/* ---------- constants */

enum
{
	/* (the Xbox's maps) */
	FORGE_AI_CACHE_VERSION = 5,
	FORGE_AI_HEADER_SIZE = 0x800,
	/* a mission's expanded size and tag data at most */
	FORGE_AI_MAXIMUM_FILE = 512 * 1024 * 1024,
	FORGE_AI_MAXIMUM_TAG_DATA = 0x01600000,
	FORGE_AI_MAXIMUM_SOURCES = 6,
	FORGE_AI_MAXIMUM_ROOTS = 16,
	FORGE_AI_MAXIMUM_MAPS = 4,
	/* the tags brought in, all factions together */
	FORGE_AI_MAXIMUM_TAGS = 4096,
	FORGE_AI_MAXIMUM_RANGES = 32768,
	/* the runs of a mission's tag bytes the characters take, the buffers */
	FORGE_AI_MAXIMUM_EXTENTS = 1 << 17,
	FORGE_AI_MAXIMUM_BUFFERS = 8192,
	FORGE_AI_READ_CHUNK = 1 << 20,
	/* (the first updates after a map's load) how many times the room for a
	mission's tags is looked for, a second apart */
	FORGE_AI_MAXIMUM_TRIES = 30,
};

/* the Xbox's tag cache, where a map's tags are linked */
#define FORGE_AI_TAG_CACHE_BASE 0x803A6000UL
#define FORGE_AI_HEADER_SIGNATURE 'head'
#define FORGE_AI_TAGS_SIGNATURE 'tags'
#define FORGE_AI_DIALOGUE_TAG 'udlg'
#define FORGE_AI_ACTOR_VARIANT_TAG 'actv'
#define FORGE_AI_MODEL_TAG 'mode'
#define FORGE_AI_BITMAP_TAG 'bitm'
#define FORGE_AI_SOUND_TAG 'snd!'
/* (xbox_texture_cache.c's _bitmap_cached_bit) */
#define FORGE_AI_BITMAP_CACHED_BIT 7

/* ---------- structures */

/* the factions: their name, their missions (the first in the maps folder
used), their characters (those of them the mission has) */
static struct forge_ai_faction_definition
{
	char const *name;
	/* FORGE_AI_PACK_* (forge_ai_tags.h): the Forge menu's category */
	short category;
	char const *maps[FORGE_AI_MAXIMUM_MAPS];
	/* each an actor variant's path, or "<group> <path>" */
	char const *roots[FORGE_AI_MAXIMUM_ROOTS];
} const forge_ai_factions[] =
{
	{ "Covenant", FORGE_AI_PACK_AI, { "b30", "a50", "b40", "d20" },
	{
		"characters\\grunt\\grunt minor plasma pistol",
		"characters\\grunt\\grunt major plasma pistol",
		"characters\\grunt\\grunt minor needler",
		"characters\\grunt\\grunt major needler",
		"characters\\jackal\\jackal minor plasma pistol",
		"characters\\jackal\\jackal major plasma pistol",
		"characters\\elite\\elite minor\\elite minor plasma rifle",
		"characters\\elite\\elite major\\elite major plasma rifle",
		"characters\\elite\\elite minor\\elite minor needler",
		"characters\\elite\\elite stealth\\stealth elite plasma rifle",
		"characters\\elite\\elite commander\\elite commander energy sword",
		"characters\\hunter\\hunter",
	} },
	{ "Marines", FORGE_AI_PACK_AI, { "a30", "b40", "b30" },
	{
		"characters\\marine\\marine assault rifle",
		"characters\\marine\\marine assault rifle major",
		"characters\\marine_armored\\marine_armored assault rifle",
		"characters\\marine_armored\\marine_armored assault rifle major",
		"characters\\marine_armored\\marine_armored sniper rifle",
	} },
	{ "Flood", FORGE_AI_PACK_AI, { "c20", "d20", "c10", "d40" },
	{
		"characters\\flood_infection\\flood_infection",
		"characters\\floodcarrier\\floodcarrier",
		"characters\\floodcombat_human\\floodcombat_human assault rifle",
		"characters\\floodcombat_human\\floodcombat_human shotgun",
		"characters\\floodcombat elite\\floodcombat elite plasma rifle",
		"characters\\floodcombat elite\\floodcombat elite shotgun",
	} },
	{ "Sentinels", FORGE_AI_PACK_AI, { "c20", "c10", "d40" },
	{
		"characters\\sentinel\\sentinel",
		"characters\\sentinel\\sentinel major",
	} },
	/* (the campaign's vehicles the multiplayer maps lack, and the campaign's
	own Warthog and Scorpion and Ghost; no rocket Warthog: the campaign has
	none) */
	{ "Covenant vehicles", FORGE_AI_PACK_VEHICLES, { "b40", "c40", "a50", "d20" },
	{
		"vehi vehicles\\banshee\\banshee",
		"vehi vehicles\\wraith\\wraith",
		"vehi vehicles\\c gun turret\\c gun turret",
		"vehi vehicles\\ghost\\ghost",
	} },
	{ "UNSC vehicles", FORGE_AI_PACK_VEHICLES, { "b40", "a10" },
	{
		"vehi vehicles\\warthog\\warthog",
		"vehi vehicles\\scorpion\\scorpion",
	} },
	{ "Dropships", FORGE_AI_PACK_VEHICLES, { "b30", "a50", "b40" },
	{
		"vehi vehicles\\pelican\\pelican",
		"vehi vehicles\\c_dropship\\c_dropship",
	} },
	/* (decorations and objects) */
	{ "Covenant objects", FORGE_AI_PACK_OBJECTS, { "c10", "b30" },
	{
		"scen scenery\\c_storage\\c_storage",
		"scen scenery\\c_storage_large\\c_storage_large",
		"scen scenery\\c_field_generator\\c_field_generator",
		"scen scenery\\c_uplink\\c_uplink",
		"scen scenery\\landing beacon\\landing beacon",
	} },
	{ "Forerunner objects", FORGE_AI_PACK_OBJECTS, { "b30", "c10" },
	{
		"lifi levels\\b30\\devices\\light fixtures\\light fixture ceiling1\\light fixture ceiling1",
		"lifi levels\\b30\\devices\\light fixtures\\light fixture floor1\\light fixture floor1",
		"lifi levels\\b30\\devices\\light fixtures\\light fixture floor2\\light fixture floor2",
		"lifi levels\\b30\\devices\\light fixtures\\light fixture wall1\\light fixture wall1",
		"lifi levels\\b30\\devices\\light fixtures\\light fixture wall3\\light fixture wall3",
		"mach levels\\b30\\devices\\interior tech objects\\holo display square\\holo display square",
		"mach levels\\b30\\devices\\interior tech objects\\holo display tall\\holo display tall",
		"mach levels\\b30\\devices\\interior tech objects\\holo display wide\\holo display wide",
		"mach levels\\b30\\devices\\interior tech objects\\holo control ring\\holo control ring",
		"mach levels\\b30\\devices\\doors\\door small\\door small",
		"ctrl levels\\b30\\devices\\interior tech objects\\holo control\\holo control",
	} },
	{ "Wreckage and barricades", FORGE_AI_PACK_OBJECTS, { "c10" },
	{
		"scen scenery\\vehicles\\pelican_crashed\\pelican_crashed",
		"scen scenery\\vehicles\\c_dropship_crashed\\c_dropship_crashed",
		"scen levels\\b40\\scenery\\b40_metal_small\\metal_small",
		"scen levels\\b40\\scenery\\b40_metal_tall\\metal_tall",
		"scen levels\\b40\\scenery\\b40_metal_wide\\metal_wide",
	} },
	{ "Plants and trees", FORGE_AI_PACK_OBJECTS, { "b30" },
	{
		"scen scenery\\trees\\tree_leafy\\tree_leafy",
		"scen scenery\\trees\\tree_leafy_medium\\tree_leafy_medium",
		"scen scenery\\trees\\tree_leafy_doublewide\\tree_leafy_doublewide",
		"scen scenery\\trees\\tree_leafy_fallentrunk\\tree_leafy_fallentrunk",
		"scen scenery\\trees\\tree_leafy_stump\\tree_leafy_stump",
		"scen scenery\\plants\\vines_hanging\\vines_hanging",
	} },
};
#define NUMBER_OF_FORGE_AI_FACTIONS ((short)NUMBEROF(forge_ai_factions))

/* a root's group (an actor variant's if it names none) and path */
static unsigned long root_group(
	char const *root)
{
	if (strlen(root) > 5 && root[4] == ' ')
		return ((unsigned long)(byte)root[0] << 24) | ((unsigned long)(byte)root[1] << 16) |
			((unsigned long)(byte)root[2] << 8) | (unsigned long)(byte)root[3];
	return FORGE_AI_ACTOR_VARIANT_TAG;
}

static char const *root_path(
	char const *root)
{
	return strlen(root) > 5 && root[4] == ' ' ? root + 5 : root;
}

/* the Xbox's tag header (cache_files.c's) */
struct forge_ai_tag_header
{
	XPTR(struct cache_file_tag_instance) tag_instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	XPTR(void) vertex_buffers;
	long index_buffer_count;
	XPTR(void) index_buffers;
	unsigned long signature;
};

/* (tag_schema_models.c's: a model's geometry and its parts) */
struct forge_ai_model_geometry
{
	byte reserved[0x24];
	struct tag_block parts;
};

struct forge_ai_model_geometry_part
{
	unsigned long flags;
	short shader_index;
	char previous_part_index;
	char next_part_index;
	short centroid_primary_node_index;
	short centroid_secondary_node_index;
	real centroid_primary_node_weight;
	real centroid_secondary_node_weight;
	real_point3d centroid;
	struct tag_block uncompressed_vertices;
	struct tag_block compressed_vertices;
	struct tag_block triangles;
	struct triangle_buffer triangle_buffer;
	struct vertex_buffer vertex_buffer;
};


/* (tag_schema_models.c's) a model vertex's bytes by its type */
static short const forge_ai_vertex_sizes[] =
{
	56, 32, 20, 8, 68, 32, 24, 36, 20, 16, 16, 8
};

/* the file's bytes [offset, offset + size) of pixels and samples, at
position in the data file */
struct forge_ai_range
{
	unsigned long offset;
	unsigned long size;
	unsigned long position;
};

/* a run of a mission's tag bytes the characters take, and where it is in
the bytes kept */
struct forge_ai_extent
{
	unsigned long address;
	unsigned long size;
	unsigned long kept;
};

/* a mission read */
struct forge_ai_source
{
	char map[16];
	/* (while it is read) its tags: tag_data_size bytes, its header first */
	byte *tags;
	unsigned long tag_data_size;
	unsigned long file_length;
	unsigned long tag_data_offset;
	long tag_count;
	/* (while it is read) each of its tags' index in the game's table (NONE:
	none; the map's own: its index), whether a character reaches it, the
	runs of its bytes the characters take */
	long *new_index;
	boolean *wanted;
	struct forge_ai_extent *extents;
	long extent_count;
	/* the bytes kept: the characters' tags' runs, end to end (in the tag
	cache's room past the map's tags, or the heap's) */
	byte *kept;
	boolean kept_in_room;
	unsigned long kept_size;
	unsigned long tags_brought;
	/* its pixels and samples: in a data file of the cache folder (made as
	the map loads, deleted as it goes), the ranges of the mission's file it
	holds */
	HANDLE data_file;
	char data_path[64];
	struct forge_ai_range *ranges;
	long range_count;
	unsigned long data_bytes;
	/* its D3D buffers registered (their Xbox addresses, an index buffer's
	with bit 0 set) */
	unsigned long *buffers;
	long buffer_count;
	boolean registered;
};

/* the schema walk */
struct forge_ai_walk
{
	struct forge_ai_source *source;
	/* (moving: where the tags were linked, and are now) */
	unsigned long from;
	unsigned long to;
	/* each byte of the tags an element of the walk started at already */
	unsigned long *visited;
	unsigned long visited_size;
	boolean failed;
	char why[128];
};

/* ---------- globals */

static struct
{
	boolean loaded;
	struct forge_ai_source sources[FORGE_AI_MAXIMUM_SOURCES];
	short source_count;
	/* each faction's source (NONE: none of its missions is there), and how
	many of its characters it brought */
	short faction_source[NUMBER_OF_FORGE_AI_FACTIONS];
	short faction_roots[NUMBER_OF_FORGE_AI_FACTIONS];
	/* the table before ours, put back as the map goes; ours, its first
	added tag's absolute index and how many */
	void *original_instances;
	long original_count;
	struct cache_file_tag_instance *instances;
	long first_index;
	long count;
	/* each added tag's source (by absolute index - first_index) */
	short *tag_source;
	unsigned long milliseconds;
} forge_ai_tags;

/* the tag cache's room past the map's tags the bytes kept go in, and how
much of it they take */
static byte *forge_ai_tag_room;
static unsigned long forge_ai_tag_room_size;
static unsigned long forge_ai_tag_room_used;
void *cache_files_tag_room(unsigned long *size);
extern unsigned long cache_files_reserved_after_tags;

/* ---------- private code */

static void walk_fail(
	struct forge_ai_walk *walk,
	char const *why,
	unsigned long value)
{
	if (!walk->failed)
		snprintf(walk->why, sizeof(walk->why), "%s (%08lx)", why, value);
	walk->failed = TRUE;
}

/* whether [address, address + size) of the tags linked at base lies in
them */
static boolean source_contains(
	struct forge_ai_source const *source,
	unsigned long base,
	unsigned long address,
	unsigned long size)
{
	return address >= base && address - base <= source->tag_data_size && size <= source->tag_data_size - (address - base);
}

/* (the walk's) a pointer of the tags moved to where they are, if it is in
them; FALSE (the walk failed) if it is not and is no null */
static boolean walk_move(
	struct forge_ai_walk *walk,
	XPTR(void) *field,
	unsigned long size,
	char const *what)
{
	unsigned long address = (unsigned long)*field;

	if (!address)
		return TRUE;
	if (!source_contains(walk->source, walk->from, address, size))
	{
		walk_fail(walk, what, address);
		return FALSE;
	}
	*field = (XPTR(void))(address - walk->from + walk->to);
	return TRUE;
}

/* (the game's free, which takes no null) */
static void release(
	void *block)
{
	if (block)
		free(block);
}

/* (the game's malloc, zeroed: its allocator has no calloc) */
static void *zeroed(
	size_t count,
	size_t size)
{
	void *block = count && size <= (size_t)-1 / count ? malloc(count * size) : NULL;

	if (block)
		memset(block, 0, count * size);
	return block;
}

/* a host pointer's Xbox address, as a number */
static unsigned long address_of(
	void const *pointer)
{
	return (unsigned long)(__UINTPTR_TYPE__)XBOX_ADDRESS(pointer);
}

/* the host address of an Xbox address of the tags where they are now */
static byte *source_at(
	struct forge_ai_source const *source,
	unsigned long address)
{
	return source->tags + (address - address_of(source->tags));
}

static boolean walk_visit(
	struct forge_ai_walk *walk,
	byte const *element)
{
	unsigned long offset = (unsigned long)(element - walk->source->tags);
	unsigned long bit = 1UL << (offset % 32);

	if (offset >= walk->source->tag_data_size || (walk->visited[offset / 32] & bit))
		return FALSE;
	walk->visited[offset / 32] |= bit;
	return TRUE;
}

static struct tag_schema_definition const *schema_root(
	unsigned long group_tag)
{
	struct tag_schema_group const *const *list;

	for (list = tag_schema_xbox_layout.group_lists; *list; list++)
	{
		struct tag_schema_group const *group;

		for (group = *list; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group->definition;
		}
	}
	return NULL;
}

/* (the move) an element's pointers, and its blocks' elements', moved */
static void move_element(
	struct forge_ai_walk *walk,
	byte *element,
	struct tag_schema_definition const *definition,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth > 32)
	{
		walk_fail(walk, "nested too deep", 0);
		return;
	}
	for (field = definition->fields; !walk->failed && field->type != _tag_schema_terminator; field++)
	{
		short item;

		for (item = 0; item < field->count && !walk->failed; item++)
		{
			byte *at = element + field->offset + (long)item * field->size;

			switch (field->type)
			{
			case _tag_schema_block:
			{
				struct tag_block *block = (struct tag_block *)at;
				struct tag_schema_definition const *child = field->definition;
				unsigned long size;
				long index;
				byte *elements;

				if (block->count <= 0)
				{
					block->count = 0;
					block->address = XBOX_NULL;
					break;
				}
				if ((unsigned long)block->count > walk->source->tag_data_size / (unsigned long)MAX(child->size, 1))
				{
					walk_fail(walk, "a block's count", (unsigned long)block->count);
					break;
				}
				size = (unsigned long)block->count * (unsigned long)child->size;
				if (!walk_move(walk, (XPTR(void) *)&block->address, size, "a block's elements"))
					break;
				elements = source_at(walk->source, (unsigned long)block->address);
				for (index = 0; index < block->count && !walk->failed; index++)
				{
					byte *child_element = elements + index * child->size;

					if (walk_visit(walk, child_element))
						move_element(walk, child_element, child, (short)(depth + 1));
				}
				break;
			}
			case _tag_schema_data:
			{
				struct tag_data *data = (struct tag_data *)at;

				if (data->size <= 0)
				{
					data->size = 0;
					data->address = XBOX_NULL;
					break;
				}
				walk_move(walk, (XPTR(void) *)&data->address, (unsigned long)data->size, "data");
				break;
			}
			case _tag_schema_file_data:
				/* (in the file: its address is the game's to set) */
				((struct tag_data *)at)->address = XBOX_NULL;
				break;
			case _tag_schema_reference:
				walk_move(walk, (XPTR(void) *)&((struct tag_reference *)at)->name, 1, "a reference's name");
				break;
			case _tag_schema_struct:
				move_element(walk, at, field->definition, (short)(depth + 1));
				break;
			}
		}
	}
}

/* (the move) a model's parts' buffers, which no schema field names */
static void move_model_buffers(
	struct forge_ai_walk *walk,
	byte *root)
{
	struct model *model = (struct model *)root;
	long geometry_index;

	if (model->geometries.count <= 0 || !model->geometries.address)
		return;
	for (geometry_index = 0; geometry_index < model->geometries.count && !walk->failed; geometry_index++)
	{
		struct forge_ai_model_geometry *geometry =
			(struct forge_ai_model_geometry *)source_at(walk->source, (unsigned long)model->geometries.address) +
			geometry_index;
		long part_index;

		if (geometry->parts.count <= 0 || !geometry->parts.address)
			continue;
		for (part_index = 0; part_index < geometry->parts.count && !walk->failed; part_index++)
		{
			struct forge_ai_model_geometry_part *part =
				(struct forge_ai_model_geometry_part *)source_at(walk->source, (unsigned long)geometry->parts.address) +
				part_index;

			walk_move(walk, (XPTR(void) *)&part->vertex_buffer.hardware_format, 12, "a part's vertex buffer");
			walk_move(walk, (XPTR(void) *)&part->triangle_buffer.hardware_format, 12, "a part's index buffer");
			part->vertex_buffer.base_address = XBOX_NULL;
			part->triangle_buffer.base_address = XBOX_NULL;
		}
	}
}

/* every pointer of a mission's tags moved from the tag cache's address to
where they are: FALSE, with why, if one does not lie in them */
static boolean source_move(
	struct forge_ai_source *source,
	char *why,
	int why_size)
{
	struct forge_ai_tag_header *header = (struct forge_ai_tag_header *)source->tags;
	struct forge_ai_walk walk;
	struct cache_file_tag_instance *instances;
	long index;

	csmemset(&walk, 0, sizeof(walk));
	walk.source = source;
	walk.from = FORGE_AI_TAG_CACHE_BASE;
	walk.to = address_of(source->tags);
	walk.visited = zeroed(source->tag_data_size / 32 + 1, sizeof(unsigned long));
	if (!walk.visited)
	{
		snprintf(why, (size_t)why_size, "%s", "out of memory");
		return FALSE;
	}
	if (header->signature != FORGE_AI_TAGS_SIGNATURE || header->tag_count <= 0 || header->tag_count > UNSIGNED_SHORT_MAX ||
		header->vertex_buffer_count < 0 || header->index_buffer_count < 0 ||
		header->vertex_buffer_count > (long)(source->tag_data_size / 12) ||
		header->index_buffer_count > (long)(source->tag_data_size / 12))
	{
		walk_fail(&walk, "a damaged tag header", header->signature);
	}
	/* (a list the header counts must be somewhere: walk_move lets a null
	through, which the loops below would read from) */
	if (!walk.failed && (!header->tag_instances || (header->vertex_buffer_count && !header->vertex_buffers) ||
		(header->index_buffer_count && !header->index_buffers)))
	{
		walk_fail(&walk, "a tag header's list with no address", (unsigned long)header->tag_count);
	}
	if (!walk.failed)
	{
		walk_move(&walk, (XPTR(void) *)&header->tag_instances,
			(unsigned long)header->tag_count * sizeof(struct cache_file_tag_instance), "the tag table");
		walk_move(&walk, (XPTR(void) *)&header->vertex_buffers, (unsigned long)header->vertex_buffer_count * 12,
			"the vertex buffers");
		walk_move(&walk, (XPTR(void) *)&header->index_buffers, (unsigned long)header->index_buffer_count * 12,
			"the index buffers");
	}
	/* (each buffer's data: Common, Data, Lock) */
	for (index = 0; !walk.failed && index < header->vertex_buffer_count + header->index_buffer_count; index++)
	{
		unsigned long *buffer = index < header->vertex_buffer_count ?
			(unsigned long *)source_at(source, (unsigned long)header->vertex_buffers) + index * 3 :
			(unsigned long *)source_at(source, (unsigned long)header->index_buffers) +
				(index - header->vertex_buffer_count) * 3;

		walk_move(&walk, (XPTR(void) *)&buffer[1], 1, "a buffer's data");
	}
	instances = walk.failed ? NULL : (struct cache_file_tag_instance *)source_at(source, (unsigned long)header->tag_instances);
	for (index = 0; instances && !walk.failed && index < header->tag_count; index++)
	{
		struct cache_file_tag_instance *instance = &instances[index];
		struct tag_schema_definition const *definition;
		byte *root;

		/* (every tag has a name: source_tag_name reads it) */
		if (!instance->name)
			walk_fail(&walk, "a tag with no name", (unsigned long)index);
		walk_move(&walk, (XPTR(void) *)&instance->name, 1, "a tag's name");
		/* (a structure bsp's: none yet, in another part of the file) */
		if (!instance->base_address || (unsigned long)instance->base_address == (unsigned long)NONE)
			continue;
		definition = schema_root((unsigned long)instance->group_tag);
		if (!definition)
		{
			/* (a group the checks do not know - a meter -: its root moved, its
			own pointers not; never brought in: source_add) */
			walk_move(&walk, (XPTR(void) *)&instance->base_address, 1, "a tag's root");
			continue;
		}
		if (!walk_move(&walk, (XPTR(void) *)&instance->base_address, (unsigned long)definition->size, "a tag's root"))
			break;
		root = source_at(source, (unsigned long)instance->base_address);
		if (!walk_visit(&walk, root))
			continue;
		move_element(&walk, root, definition, 0);
		if ((unsigned long)instance->group_tag == FORGE_AI_MODEL_TAG && !walk.failed)
			move_model_buffers(&walk, root);
	}
	release(walk.visited);
	if (walk.failed)
		snprintf(why, (size_t)why_size, "%s", walk.why);
	return !walk.failed;
}

/* the source's tag of an index (a reference's or a tag index field's): its
absolute index, NONE for none or one not in its table */
static long source_tag(
	struct forge_ai_source const *source,
	long tag_index)
{
	struct forge_ai_tag_header const *header = (struct forge_ai_tag_header const *)source->tags;
	struct cache_file_tag_instance const *instances =
		(struct cache_file_tag_instance const *)source_at(source, (unsigned long)header->tag_instances);
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index);

	if (tag_index == NONE || absolute < 0 || absolute >= source->tag_count || instances[absolute].tag_index != tag_index)
		return NONE;
	return absolute;
}

static struct cache_file_tag_instance *source_instance(
	struct forge_ai_source const *source,
	long absolute)
{
	struct forge_ai_tag_header const *header = (struct forge_ai_tag_header const *)source->tags;

	return (struct cache_file_tag_instance *)source_at(source, (unsigned long)header->tag_instances) + absolute;
}

static char const *source_tag_name(
	struct forge_ai_source const *source,
	long absolute)
{
	return (char const *)source_at(source, (unsigned long)source_instance(source, absolute)->name);
}

/* (the reach, or the renumbering when renumber) an element's references
and tag indices: each tag reached marked wanted (pending), or each made the
game's index */
static void reach_element(
	struct forge_ai_walk *walk,
	byte *element,
	struct tag_schema_definition const *definition,
	boolean renumber,
	long *pending,
	long *pending_count,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth > 32)
		return;
	for (field = definition->fields; field->type != _tag_schema_terminator; field++)
	{
		short item;

		for (item = 0; item < field->count; item++)
		{
			byte *at = element + field->offset + (long)item * field->size;
			long *index_field = NULL;

			switch (field->type)
			{
			case _tag_schema_block:
			{
				struct tag_block *block = (struct tag_block *)at;
				struct tag_schema_definition const *child = field->definition;
				long index;

				if (block->count <= 0 || !block->address ||
					!source_contains(walk->source, address_of(walk->source->tags), (unsigned long)block->address,
						(unsigned long)block->count * (unsigned long)child->size))
				{
					break;
				}
				for (index = 0; index < block->count; index++)
				{
					byte *child_element = source_at(walk->source, (unsigned long)block->address) + index * child->size;

					if (walk_visit(walk, child_element))
						reach_element(walk, child_element, child, renumber, pending, pending_count, (short)(depth + 1));
				}
				break;
			}
			case _tag_schema_struct:
				reach_element(walk, at, field->definition, renumber, pending, pending_count, (short)(depth + 1));
				break;
			case _tag_schema_reference:
				index_field = &((struct tag_reference *)at)->index;
				break;
			case _tag_schema_tag_index:
				index_field = (long *)at;
				break;
			}
			if (!index_field)
				continue;
			{
				long absolute = source_tag(walk->source, *index_field);

				if (renumber)
				{
					*index_field = absolute == NONE ? NONE : walk->source->new_index[absolute];
				}
				else if (absolute != NONE && !walk->source->wanted[absolute] && *pending_count < walk->source->tag_count)
				{
					walk->source->wanted[absolute] = TRUE;
					pending[(*pending_count)++] = absolute;
				}
			}
		}
	}
}

/* a mission's file opened, its header read and checked */
static HANDLE source_open(
	char const *map,
	struct forge_ai_source *source,
	char *why,
	int why_size)
{
	char path[256];
	byte header[FORGE_AI_HEADER_SIZE];
	DWORD read = 0;
	HANDLE file;
	long version;
	long file_length;
	long tag_data_offset;
	long tag_data_size;

	snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), map);
	file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		snprintf(why, (size_t)why_size, "%s", "not in the maps folder");
		return NULL;
	}
	if (!ReadFile(file, header, sizeof(header), &read, NULL) || read != sizeof(header))
	{
		CloseHandle(file);
		snprintf(why, (size_t)why_size, "%s", "too short");
		return NULL;
	}
	csmemcpy(&version, header + 4, 4);
	csmemcpy(&file_length, header + 8, 4);
	csmemcpy(&tag_data_offset, header + 16, 4);
	csmemcpy(&tag_data_size, header + 20, 4);
	if (*(unsigned long *)header != FORGE_AI_HEADER_SIGNATURE || version != FORGE_AI_CACHE_VERSION ||
		file_length <= FORGE_AI_HEADER_SIZE || file_length > FORGE_AI_MAXIMUM_FILE ||
		tag_data_size < (long)sizeof(struct forge_ai_tag_header) || tag_data_size > FORGE_AI_MAXIMUM_TAG_DATA ||
		tag_data_offset < FORGE_AI_HEADER_SIZE || tag_data_offset > file_length - tag_data_size)
	{
		CloseHandle(file);
		snprintf(why, (size_t)why_size, "not an Xbox map this can read (version %ld)", version);
		return NULL;
	}
	source->file_length = (unsigned long)file_length;
	source->tag_data_offset = (unsigned long)tag_data_offset;
	source->tag_data_size = (unsigned long)tag_data_size;
	return file;
}

/* where the expanded bytes go: each range's (sorted by offset), into
memory (bytes) or a file at the range's position (file) */
struct forge_ai_sink
{
	struct forge_ai_range const *ranges;
	long range_count;
	byte *bytes;
	HANDLE file;
};

/* a mission's file expanded from its start, each expanded byte in one of
the sink's ranges put there, up to the end of the last: FALSE if it could
not be */
static boolean source_expand(
	HANDLE file,
	struct forge_ai_sink const *sink,
	unsigned long file_length,
	char *why,
	int why_size)
{
	z_stream stream;
	byte *in = malloc(FORGE_AI_READ_CHUNK);
	byte *out = malloc(FORGE_AI_READ_CHUNK);
	unsigned long position = FORGE_AI_HEADER_SIZE;
	unsigned long end = sink->ranges[sink->range_count - 1].offset + sink->ranges[sink->range_count - 1].size;
	long range = 0;
	boolean done = FALSE;
	boolean ok = FALSE;
	boolean written = TRUE;
	int result = Z_OK;

	csmemset(&stream, 0, sizeof(stream));
	if (!in || !out || inflateInit(&stream) != Z_OK)
	{
		release(in);
		release(out);
		snprintf(why, (size_t)why_size, "%s", "out of memory");
		return FALSE;
	}
	SetFilePointer(file, FORGE_AI_HEADER_SIZE, NULL, FILE_BEGIN);
	while (!done && written)
	{
		DWORD read = 0;

		if (!stream.avail_in)
		{
			if (!ReadFile(file, in, FORGE_AI_READ_CHUNK, &read, NULL) || !read)
				break;
			stream.next_in = in;
			stream.avail_in = read;
		}
		stream.next_out = out;
		stream.avail_out = FORGE_AI_READ_CHUNK;
		result = inflate(&stream, Z_NO_FLUSH);
		if (result != Z_OK && result != Z_STREAM_END)
			break;
		{
			unsigned long produced = FORGE_AI_READ_CHUNK - stream.avail_out;
			unsigned long chunk_end = position + produced;
			long scan;

			if (chunk_end > file_length)
				break;
			while (range < sink->range_count && sink->ranges[range].offset + sink->ranges[range].size <= position)
				range++;
			for (scan = range; scan < sink->range_count && sink->ranges[scan].offset < chunk_end; scan++)
			{
				struct forge_ai_range const *entry = &sink->ranges[scan];
				unsigned long from = MAX(entry->offset, position);
				unsigned long to = MIN(entry->offset + entry->size, chunk_end);

				if (to <= from)
					continue;
				if (sink->bytes)
				{
					csmemcpy(sink->bytes + entry->position + (from - entry->offset), out + (from - position), to - from);
				}
				else
				{
					DWORD put = 0;

					SetFilePointer(sink->file, (LONG)(entry->position + (from - entry->offset)), NULL, FILE_BEGIN);
					written = WriteFile(sink->file, out + (from - position), (DWORD)(to - from), &put, NULL) &&
						put == to - from;
				}
			}
			position = chunk_end;
		}
		if (position >= end)
		{
			ok = TRUE;
			done = TRUE;
		}
		else if (result == Z_STREAM_END)
		{
			done = TRUE;
		}
	}
	inflateEnd(&stream);
	release(in);
	release(out);
	if (!written)
		snprintf(why, (size_t)why_size, "%s", "its data file could not be written");
	else if (!ok)
		snprintf(why, (size_t)why_size, "its data is damaged or cut off (at %lu of %lu)", position, end);
	return ok && written;
}

static int range_compare(
	void const *a,
	void const *b)
{
	struct forge_ai_range const *left = a;
	struct forge_ai_range const *right = b;

	return left->offset < right->offset ? -1 : left->offset > right->offset ? 1 : 0;
}

static int extent_compare(
	void const *a,
	void const *b)
{
	struct forge_ai_extent const *left = a;
	struct forge_ai_extent const *right = b;

	return left->address < right->address ? -1 : left->address > right->address ? 1 : 0;
}

/* (the runs) bytes of the tags the characters take */
static void extent_add(
	struct forge_ai_walk *walk,
	unsigned long address,
	unsigned long size)
{
	struct forge_ai_source *source = walk->source;

	if (!size || !address)
		return;
	/* (one the checks made the game's own - an empty name -: not the
	tags', left as it is: kept_move) */
	if (!source_contains(source, address_of(source->tags), address, 1))
		return;
	if (!source_contains(source, address_of(source->tags), address, size))
	{
		walk_fail(walk, "a run past the tags' end", address);
		return;
	}
	if (source->extent_count >= FORGE_AI_MAXIMUM_EXTENTS)
	{
		walk_fail(walk, "too many runs", (unsigned long)source->extent_count);
		return;
	}
	source->extents[source->extent_count].address = address;
	source->extents[source->extent_count].size = size;
	source->extent_count++;
}

/* (the runs) an element's: its blocks' elements, its data, its references'
names */
static void extent_element(
	struct forge_ai_walk *walk,
	byte *element,
	struct tag_schema_definition const *definition,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth > 32)
		return;
	for (field = definition->fields; !walk->failed && field->type != _tag_schema_terminator; field++)
	{
		short item;

		for (item = 0; item < field->count; item++)
		{
			byte *at = element + field->offset + (long)item * field->size;

			switch (field->type)
			{
			case _tag_schema_block:
			{
				struct tag_block *block = (struct tag_block *)at;
				struct tag_schema_definition const *child = field->definition;
				long index;

				if (block->count <= 0 || !block->address ||
					!source_contains(walk->source, address_of(walk->source->tags), (unsigned long)block->address,
						(unsigned long)block->count * (unsigned long)child->size))
				{
					break;
				}
				extent_add(walk, (unsigned long)block->address, (unsigned long)block->count * (unsigned long)child->size);
				for (index = 0; index < block->count && !walk->failed; index++)
				{
					byte *child_element = source_at(walk->source, (unsigned long)block->address) + index * child->size;

					if (walk_visit(walk, child_element))
						extent_element(walk, child_element, child, (short)(depth + 1));
				}
				break;
			}
			case _tag_schema_data:
			{
				struct tag_data *data = (struct tag_data *)at;

				if (data->size > 0 && data->address)
					extent_add(walk, (unsigned long)data->address, (unsigned long)data->size);
				break;
			}
			case _tag_schema_reference:
			{
				struct tag_reference *reference = (struct tag_reference *)at;

				if (reference->name &&
					source_contains(walk->source, address_of(walk->source->tags), (unsigned long)reference->name, 1))
				{
					char const *name = (char const *)source_at(walk->source, (unsigned long)reference->name);
					unsigned long room = walk->source->tag_data_size - (unsigned long)((byte const *)name - walk->source->tags);

					extent_add(walk, (unsigned long)reference->name, (unsigned long)strnlen(name, room - 1) + 1);
				}
				break;
			}
			case _tag_schema_struct:
				extent_element(walk, at, field->definition, (short)(depth + 1));
				break;
			}
		}
	}
}

/* a model's parts' Direct3D buffers and their vertices and indices */
static void extent_model(
	struct forge_ai_walk *walk,
	byte *root)
{
	struct model *model = (struct model *)root;
	long geometry_index;

	for (geometry_index = 0; geometry_index < model->geometries.count && model->geometries.address; geometry_index++)
	{
		struct forge_ai_model_geometry *geometry =
			(struct forge_ai_model_geometry *)source_at(walk->source, (unsigned long)model->geometries.address) +
			geometry_index;
		long part_index;

		for (part_index = 0; part_index < geometry->parts.count && geometry->parts.address; part_index++)
		{
			struct forge_ai_model_geometry_part *part =
				(struct forge_ai_model_geometry_part *)source_at(walk->source, (unsigned long)geometry->parts.address) +
				part_index;
			struct vertex_buffer *vertices = &part->vertex_buffer;
			struct triangle_buffer *triangles = &part->triangle_buffer;
			long vertex_size = VALID_INDEX(vertices->type, (long)NUMBEROF(forge_ai_vertex_sizes)) ?
				forge_ai_vertex_sizes[vertices->type] : 0;
			long index_count = triangles->type == 0 ? 3 * triangles->count :
				triangles->type == 1 && triangles->count > 0 ? triangles->count + 2 : 0;

			if (vertices->hardware_format && vertex_size && vertices->count > 0 &&
				source_contains(walk->source, address_of(walk->source->tags), (unsigned long)vertices->hardware_format, 12))
			{
				unsigned long *buffer = (unsigned long *)source_at(walk->source, (unsigned long)vertices->hardware_format);

				extent_add(walk, (unsigned long)vertices->hardware_format, 12);
				extent_add(walk, buffer[1], (unsigned long)vertices->count * (unsigned long)vertex_size);
			}
			if (triangles->hardware_format && index_count > 0 &&
				source_contains(walk->source, address_of(walk->source->tags), (unsigned long)triangles->hardware_format, 12))
			{
				unsigned long *buffer = (unsigned long *)source_at(walk->source, (unsigned long)triangles->hardware_format);

				extent_add(walk, (unsigned long)triangles->hardware_format, 12);
				extent_add(walk, buffer[1], (unsigned long)index_count * 2);
			}
		}
	}
}

/* where a byte of the tags is in the bytes kept (its Xbox address), 0 if
the characters took none of it */
static unsigned long kept_address(
	struct forge_ai_source const *source,
	unsigned long address)
{
	long low = 0;
	long high = source->extent_count - 1;

	while (low <= high)
	{
		long middle = (low + high) / 2;
		struct forge_ai_extent const *extent = &source->extents[middle];

		if (address < extent->address)
			high = middle - 1;
		else if (address >= extent->address + extent->size)
			low = middle + 1;
		else
			return address_of(source->kept) + extent->kept + (address - extent->address);
	}
	return 0;
}

static byte *kept_at(
	struct forge_ai_source const *source,
	unsigned long address)
{
	return source->kept + (address - address_of(source->kept));
}

/* (the bytes kept) a pointer of the tags made the bytes kept's; FALSE (the
walk failed) if they do not hold it */
static boolean kept_move(
	struct forge_ai_walk *walk,
	XPTR(void) *field,
	char const *what)
{
	unsigned long address = (unsigned long)*field;
	unsigned long moved;

	if (!address)
		return TRUE;
	/* (not the tags': one the checks gave, the game's own) */
	if (!source_contains(walk->source, address_of(walk->source->tags), address, 1))
		return TRUE;
	moved = kept_address(walk->source, address);
	if (!moved)
	{
		walk_fail(walk, what, address);
		return FALSE;
	}
	*field = (XPTR(void))moved;
	return TRUE;
}

static boolean kept_visit(
	struct forge_ai_walk *walk,
	byte const *element)
{
	unsigned long offset = (unsigned long)(element - walk->source->kept);
	unsigned long bit = 1UL << (offset % 32);

	if (offset >= walk->source->kept_size || (walk->visited[offset / 32] & bit))
		return FALSE;
	walk->visited[offset / 32] |= bit;
	return TRUE;
}

/* (the bytes kept) an element's pointers made the bytes kept's, its
references and tag indices the game's table's */
static void kept_element(
	struct forge_ai_walk *walk,
	byte *element,
	struct tag_schema_definition const *definition,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth > 32)
		return;
	for (field = definition->fields; !walk->failed && field->type != _tag_schema_terminator; field++)
	{
		short item;

		for (item = 0; item < field->count && !walk->failed; item++)
		{
			byte *at = element + field->offset + (long)item * field->size;
			long *index_field = NULL;

			switch (field->type)
			{
			case _tag_schema_block:
			{
				struct tag_block *block = (struct tag_block *)at;
				struct tag_schema_definition const *child = field->definition;
				long index;

				if (block->count <= 0 || !block->address)
					break;
				if (!kept_move(walk, (XPTR(void) *)&block->address, "a block"))
					break;
				for (index = 0; index < block->count && !walk->failed; index++)
				{
					byte *child_element = kept_at(walk->source, (unsigned long)block->address) + index * child->size;

					if (kept_visit(walk, child_element))
						kept_element(walk, child_element, child, (short)(depth + 1));
				}
				break;
			}
			case _tag_schema_data:
				if (((struct tag_data *)at)->size > 0)
					kept_move(walk, (XPTR(void) *)&((struct tag_data *)at)->address, "data");
				break;
			case _tag_schema_reference:
				kept_move(walk, (XPTR(void) *)&((struct tag_reference *)at)->name, "a name");
				index_field = &((struct tag_reference *)at)->index;
				break;
			case _tag_schema_tag_index:
				index_field = (long *)at;
				break;
			case _tag_schema_struct:
				kept_element(walk, at, field->definition, (short)(depth + 1));
				break;
			}
			if (index_field)
			{
				long absolute = source_tag(walk->source, *index_field);

				*index_field = absolute == NONE ? NONE : walk->source->new_index[absolute];
			}
		}
	}
}

/* (the bytes kept) a model's parts' buffers, and the buffers' data */
static void kept_model(
	struct forge_ai_walk *walk,
	byte *root)
{
	struct model *model = (struct model *)root;
	long geometry_index;

	for (geometry_index = 0; geometry_index < model->geometries.count && model->geometries.address; geometry_index++)
	{
		struct forge_ai_model_geometry *geometry =
			(struct forge_ai_model_geometry *)kept_at(walk->source, (unsigned long)model->geometries.address) +
			geometry_index;
		long part_index;

		for (part_index = 0; part_index < geometry->parts.count && geometry->parts.address; part_index++)
		{
			struct forge_ai_model_geometry_part *part =
				(struct forge_ai_model_geometry_part *)kept_at(walk->source, (unsigned long)geometry->parts.address) +
				part_index;
			XPTR(void) *formats[2];
			short which;

			formats[0] = (XPTR(void) *)&part->vertex_buffer.hardware_format;
			formats[1] = (XPTR(void) *)&part->triangle_buffer.hardware_format;
			for (which = 0; which < 2 && !walk->failed; which++)
			{
				unsigned long *buffer;
				unsigned long address;
				long known;

				if (!*formats[which])
					continue;
				/* (a buffer two parts share: moved once) */
				address = kept_address(walk->source, (unsigned long)*formats[which]);
				for (known = 0; known < walk->source->buffer_count && (walk->source->buffers[known] & ~1UL) != address; known++)
					;
				if (!kept_move(walk, formats[which], "a part's buffer"))
					break;
				if (known < walk->source->buffer_count)
					continue;
				buffer = (unsigned long *)kept_at(walk->source, (unsigned long)*formats[which]);
				if (!kept_move(walk, (XPTR(void) *)&buffer[1], "a buffer's data"))
					break;
				if (walk->source->buffer_count < FORGE_AI_MAXIMUM_BUFFERS)
					walk->source->buffers[walk->source->buffer_count++] = address | (unsigned long)which;
			}
		}
	}
}

/* the bytes the characters' tags take kept apart, their pointers and
indices made the game's; the mission's tags let go */
static boolean source_keep(
	struct forge_ai_source *source,
	char *why,
	int why_size)
{
	struct forge_ai_walk walk;
	long absolute;
	long index;
	long merged = 0;
	unsigned long kept = 0;

	csmemset(&walk, 0, sizeof(walk));
	walk.source = source;
	walk.visited = zeroed(source->tag_data_size / 32 + 1, sizeof(unsigned long));
	source->extents = malloc(FORGE_AI_MAXIMUM_EXTENTS * sizeof(*source->extents));
	if (!walk.visited || !source->extents)
	{
		release(walk.visited);
		snprintf(why, (size_t)why_size, "%s", "out of memory");
		return FALSE;
	}
	source->extent_count = 0;
	for (absolute = 0; absolute < source->tag_count && !walk.failed; absolute++)
	{
		struct cache_file_tag_instance *instance = source_instance(source, absolute);
		struct tag_schema_definition const *definition;
		byte *root;

		if (!source->wanted[absolute] || source->new_index[absolute] == NONE)
			continue;
		definition = schema_root((unsigned long)instance->group_tag);
		extent_add(&walk, (unsigned long)instance->base_address, (unsigned long)definition->size);
		{
			char const *name = source_tag_name(source, absolute);

			extent_add(&walk, (unsigned long)instance->name, (unsigned long)strnlen(name, 255) + 1);
		}
		root = source_at(source, (unsigned long)instance->base_address);
		if (walk_visit(&walk, root))
			extent_element(&walk, root, definition, 0);
		if ((unsigned long)instance->group_tag == FORGE_AI_MODEL_TAG)
			extent_model(&walk, root);
	}
	release(walk.visited);
	walk.visited = NULL;
	if (walk.failed)
	{
		snprintf(why, (size_t)why_size, "%s", walk.why);
		return FALSE;
	}
	/* (the runs sorted, those that overlap or touch made one, each given its
	place in the bytes kept) */
	qsort(source->extents, (size_t)source->extent_count, sizeof(*source->extents), extent_compare);
	for (index = 0; index < source->extent_count; index++)
	{
		struct forge_ai_extent *extent = &source->extents[index];

		if (merged && extent->address <= source->extents[merged - 1].address + source->extents[merged - 1].size)
		{
			unsigned long end = MAX(source->extents[merged - 1].address + source->extents[merged - 1].size,
				extent->address + extent->size);

			source->extents[merged - 1].size = end - source->extents[merged - 1].address;
			continue;
		}
		source->extents[merged++] = *extent;
	}
	source->extent_count = merged;
	for (index = 0; index < merged; index++)
	{
		/* (each run where it is in its 16 bytes: the vertices' alignment) */
		kept = (kept + 15) & ~15UL;
		kept += source->extents[index].address & 15;
		source->extents[index].kept = kept;
		kept += source->extents[index].size;
	}
	source->kept_size = kept;
	/* (in the tag cache's room past the map's tags if it is there - a
	structure bsp's load leaves it -, else the heap's) */
	if (forge_ai_tag_room && forge_ai_tag_room_used + kept + 16 <= forge_ai_tag_room_size)
	{
		source->kept = forge_ai_tag_room + forge_ai_tag_room_used;
		source->kept_in_room = TRUE;
		forge_ai_tag_room_used += (kept + 16 + 15) & ~15UL;
	}
	else
	{
		source->kept = malloc(kept + 16);
	}
	source->buffers = malloc(FORGE_AI_MAXIMUM_BUFFERS * sizeof(unsigned long));
	walk.visited = zeroed(kept / 32 + 1, sizeof(unsigned long));
	if (!source->kept || !source->buffers || !walk.visited)
	{
		release(walk.visited);
		snprintf(why, (size_t)why_size, "%s", "out of memory");
		return FALSE;
	}
	/* (16-byte aligned: the runs keep their alignment) */
	if (address_of(source->kept) & 15)
	{
		unsigned long shift = 16 - (address_of(source->kept) & 15);

		for (index = 0; index < merged; index++)
			source->extents[index].kept += shift;
	}
	for (index = 0; index < merged; index++)
	{
		csmemcpy(source->kept + source->extents[index].kept, source_at(source, source->extents[index].address),
			source->extents[index].size);
	}
	/* (every pointer and index the game's) */
	source->buffer_count = 0;
	for (absolute = 0; absolute < source->tag_count && !walk.failed; absolute++)
	{
		struct cache_file_tag_instance *instance = source_instance(source, absolute);
		byte *root;

		if (!source->wanted[absolute] || source->new_index[absolute] == NONE)
			continue;
		root = kept_at(source, kept_address(source, (unsigned long)instance->base_address));
		if (kept_visit(&walk, root))
			kept_element(&walk, root, schema_root((unsigned long)instance->group_tag), 0);
		if ((unsigned long)instance->group_tag == FORGE_AI_MODEL_TAG && !walk.failed)
			kept_model(&walk, root);
	}
	release(walk.visited);
	if (walk.failed)
	{
		snprintf(why, (size_t)why_size, "%s", walk.why);
		return FALSE;
	}
	return TRUE;
}

/* (the data's pass) a range of the file wanted */
static void range_add(
	struct forge_ai_source *source,
	struct forge_ai_range *ranges,
	unsigned long offset,
	unsigned long size)
{
	if (!size || offset < FORGE_AI_HEADER_SIZE || offset > source->file_length || size > source->file_length - offset ||
		source->range_count >= FORGE_AI_MAXIMUM_RANGES)
	{
		return;
	}
	/* (and its 512-byte sector's rest, which the Xbox's cache reads) */
	size = MIN((size + 511) & ~511UL, source->file_length - offset);
	ranges[source->range_count].offset = offset;
	ranges[source->range_count].size = size;
	ranges[source->range_count].position = 0;
	source->range_count++;
}

/* the pixels and samples of the tags brought in, read from the mission's
file into a data file of the cache folder */
static boolean source_data(
	struct forge_ai_source *source,
	HANDLE file,
	char *why,
	int why_size)
{
	struct forge_ai_range *ranges = malloc(FORGE_AI_MAXIMUM_RANGES * sizeof(*ranges));
	struct forge_ai_sink sink;
	long absolute;
	long merged = 0;
	long index;

	if (!ranges)
	{
		snprintf(why, (size_t)why_size, "%s", "out of memory");
		return FALSE;
	}
	source->range_count = 0;
	for (absolute = 0; absolute < source->tag_count; absolute++)
	{
		struct cache_file_tag_instance *instance = source_instance(source, absolute);
		byte *root;

		if (!source->wanted[absolute] || source->new_index[absolute] == NONE)
			continue;
		root = kept_at(source, kept_address(source, (unsigned long)instance->base_address));
		if ((unsigned long)instance->group_tag == FORGE_AI_BITMAP_TAG)
		{
			struct bitmap_group *group = (struct bitmap_group *)root;

			for (index = 0; index < group->bitmaps.count && group->bitmaps.address; index++)
			{
				struct bitmap_data *bitmap = (struct bitmap_data *)kept_at(source, (unsigned long)group->bitmaps.address) +
					index;

				/* (a cache file's bitmap is made the texture cache's as the map is
				built - its cached bit set, its pixels' offset the file's -: as
				the texture cache reads it; one that is not, from its group's
				pixel data, as texture_cache_bitmap_new would make it) */
				unsigned long offset = (unsigned long)bitmap->pixels_offset;

				if (!TEST_FLAG(bitmap->flags, FORGE_AI_BITMAP_CACHED_BIT))
					offset += (unsigned long)group->pixel_data.file_offset;
				range_add(source, ranges, offset, (unsigned long)MAX(bitmap_get_pixel_data_size(bitmap), bitmap->pixels_size));
			}
		}
		else if ((unsigned long)instance->group_tag == FORGE_AI_SOUND_TAG)
		{
			struct sound_definition *sound = (struct sound_definition *)root;
			long range_index;

			for (range_index = 0; range_index < sound->pitch_ranges.count && sound->pitch_ranges.address; range_index++)
			{
				struct sound_pitch_range *pitch = (struct sound_pitch_range *)kept_at(source,
					(unsigned long)sound->pitch_ranges.address) + range_index;
				long permutation;

				for (permutation = 0; permutation < pitch->permutations.count && pitch->permutations.address; permutation++)
				{
					struct sound_permutation *entry = (struct sound_permutation *)kept_at(source,
						(unsigned long)pitch->permutations.address) + permutation;

					range_add(source, ranges, (unsigned long)entry->samples.file_offset, (unsigned long)entry->samples.size);
					/* (the tag the sound cache reads its samples by, its cache_tag_index
					(xbox_sound_cache.c): the map's
					build wrote the mission's index there) */
					entry->unknown2 = (unsigned long)source->new_index[absolute];
				}
			}
		}
	}
	source->ranges = ranges;
	if (!source->range_count)
		return TRUE;
	qsort(ranges, (size_t)source->range_count, sizeof(*ranges), range_compare);
	for (index = 0; index < source->range_count; index++)
	{
		if (merged && ranges[index].offset <= ranges[merged - 1].offset + ranges[merged - 1].size)
		{
			unsigned long end = MAX(ranges[merged - 1].offset + ranges[merged - 1].size, ranges[index].offset + ranges[index].size);

			ranges[merged - 1].size = end - ranges[merged - 1].offset;
			continue;
		}
		ranges[merged++] = ranges[index];
	}
	source->range_count = merged;
	source->data_bytes = 0;
	for (index = 0; index < merged; index++)
	{
		ranges[index].position = source->data_bytes;
		source->data_bytes += ranges[index].size;
	}
	snprintf(source->data_path, sizeof(source->data_path), "z:\\forge_ai_%s.dat", source->map);
	source->data_file = CreateFileA(source->data_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	if (source->data_file == INVALID_HANDLE_VALUE)
	{
		source->data_file = NULL;
		snprintf(why, (size_t)why_size, "%s", "its data file could not be made in the cache folder");
		return FALSE;
	}
	sink.ranges = ranges;
	sink.range_count = merged;
	sink.bytes = NULL;
	sink.file = source->data_file;
	return source_expand(file, &sink, source->file_length, why, why_size);
}

/* the room the missions' tags are read into, one after another */
static byte *forge_ai_room;
static unsigned long forge_ai_room_size;

/* a mission read: its tag data expanded, moved and checked */
static boolean source_read(
	struct forge_ai_source *source,
	char const *map,
	char *why,
	int why_size)
{
	struct forge_ai_range range;
	struct forge_ai_sink sink;
	HANDLE file;
	boolean ok;

	csmemset(source, 0, sizeof(*source));
	snprintf(source->map, sizeof(source->map), "%s", map);
	file = source_open(map, source, why, why_size);
	if (!file)
		return FALSE;
	/* (the one room every mission's tags are read into in turn, made first:
	forge_ai_tags_bring) */
	source->tags = source->tag_data_size <= forge_ai_room_size ? forge_ai_room : NULL;
	if (!source->tags)
	{
		CloseHandle(file);
		snprintf(why, (size_t)why_size, "%s", "no memory for its tags");
		return FALSE;
	}
	range.offset = source->tag_data_offset;
	range.size = source->tag_data_size;
	range.position = 0;
	sink.ranges = &range;
	sink.range_count = 1;
	sink.bytes = source->tags;
	sink.file = NULL;
	ok = source_expand(file, &sink, source->file_length, why, why_size);
	CloseHandle(file);
	if (!ok)
		return FALSE;
	if (!source_move(source, why, why_size))
		return FALSE;
	source->tag_count = ((struct forge_ai_tag_header *)source->tags)->tag_count;
	/* (the game's own checks, as a map's tags get them) */
	if (!tag_validate_other_tags(source->tags, (long)source->tag_data_size, (long)source->file_length, map))
	{
		snprintf(why, (size_t)why_size, "%s", "its tags did not pass the checks (the log says why)");
		return FALSE;
	}
	source->new_index = malloc((size_t)source->tag_count * sizeof(long));
	source->wanted = zeroed((size_t)source->tag_count, sizeof(boolean));
	if (!source->new_index || !source->wanted)
	{
		snprintf(why, (size_t)why_size, "%s", "out of memory");
		return FALSE;
	}
	return TRUE;
}

/* (once its tags are kept apart) what reading a mission took let go */
static void source_done(
	struct forge_ai_source *source)
{
	source->tags = NULL;
	release(source->new_index);
	source->new_index = NULL;
	release(source->wanted);
	source->wanted = NULL;
	release(source->extents);
	source->extents = NULL;
}

static void source_free(
	struct forge_ai_source *source)
{
	source_done(source);
	if (source->data_file)
	{
		CloseHandle(source->data_file);
		DeleteFileA(source->data_path);
	}
	release(source->ranges);
	release(source->buffers);
	if (!source->kept_in_room)
		release(source->kept);
	csmemset(source, 0, sizeof(*source));
}

/* a faction's characters the source has, marked wanted with everything
they reach (pending: the walk's queue). How many it has */
static short source_reach(
	struct forge_ai_source *source,
	struct forge_ai_faction_definition const *faction,
	long *pending)
{
	struct forge_ai_walk walk;
	long pending_count = 0;
	long next = 0;
	short roots = 0;
	short root;

	csmemset(&walk, 0, sizeof(walk));
	walk.source = source;
	walk.visited = zeroed(source->tag_data_size / 32 + 1, sizeof(unsigned long));
	if (!walk.visited)
		return 0;
	for (root = 0; root < FORGE_AI_MAXIMUM_ROOTS && faction->roots[root]; root++)
	{
		long absolute;

		unsigned long group_tag = root_group(faction->roots[root]);
		char const *path = root_path(faction->roots[root]);

		for (absolute = 0; absolute < source->tag_count; absolute++)
		{
			struct cache_file_tag_instance *instance = source_instance(source, absolute);

			if ((unsigned long)instance->group_tag == group_tag && instance->base_address &&
				!_stricmp(source_tag_name(source, absolute), path))
			{
				if (!source->wanted[absolute])
				{
					source->wanted[absolute] = TRUE;
					pending[pending_count++] = absolute;
				}
				roots++;
				break;
			}
		}
	}
	while (next < pending_count)
	{
		long absolute = pending[next++];
		struct cache_file_tag_instance *instance = source_instance(source, absolute);
		struct tag_schema_definition const *definition = schema_root((unsigned long)instance->group_tag);
		byte *root_element;

		/* (one the map has of the same group and name is the map's; a
		dialogue none: no further) */
		if ((unsigned long)instance->group_tag == FORGE_AI_DIALOGUE_TAG ||
			tag_loaded(instance->group_tag, source_tag_name(source, absolute)) != NONE || !definition ||
			!instance->base_address)
		{
			continue;
		}
		root_element = source_at(source, (unsigned long)instance->base_address);
		if (walk_visit(&walk, root_element))
			reach_element(&walk, root_element, definition, FALSE, pending, &pending_count, 0);
	}
	release(walk.visited);
	return roots;
}

/* the tags wanted given their places in the game's table (or the map's own
tag of their group and name, or none) */
static boolean source_number(
	short source_index,
	struct cache_file_tag_instance *instances,
	long *next,
	long salt,
	char *why,
	int why_size)
{
	struct forge_ai_source *source = &forge_ai_tags.sources[source_index];
	long absolute;

	for (absolute = 0; absolute < source->tag_count; absolute++)
	{
		struct cache_file_tag_instance *instance = source_instance(source, absolute);

		source->new_index[absolute] = NONE;
		if (!source->wanted[absolute] || (unsigned long)instance->group_tag == FORGE_AI_DIALOGUE_TAG ||
			!instance->base_address || !schema_root((unsigned long)instance->group_tag))
		{
			source->wanted[absolute] = FALSE;
			continue;
		}
		source->new_index[absolute] = tag_loaded(instance->group_tag, source_tag_name(source, absolute));
		if (source->new_index[absolute] != NONE)
		{
			/* (the map's own: not brought in) */
			source->wanted[absolute] = FALSE;
			continue;
		}
		if (*next - forge_ai_tags.first_index >= FORGE_AI_MAXIMUM_TAGS || *next >= UNSIGNED_SHORT_MAX)
		{
			snprintf(why, (size_t)why_size, "%s", "too many tags");
			return FALSE;
		}
		source->new_index[absolute] = (salt << 16) | *next;
		forge_ai_tags.tag_source[*next - forge_ai_tags.first_index] = source_index;
		(*next)++;
		source->tags_brought++;
	}
	return TRUE;
}

/* the tags brought in put in the game's table (from where they are kept),
their models' buffers registered */
static void source_table(
	struct forge_ai_source *source,
	struct cache_file_tag_instance *instances)
{
	long absolute;
	long index;

	for (absolute = 0; absolute < source->tag_count; absolute++)
	{
		struct cache_file_tag_instance *instance = source_instance(source, absolute);
		long table_index;

		if (!source->wanted[absolute] || source->new_index[absolute] == NONE)
			continue;
		table_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(source->new_index[absolute]);
		instances[table_index] = *instance;
		instances[table_index].tag_index = source->new_index[absolute];
		instances[table_index].base_address = (XPTR(void))kept_address(source, (unsigned long)instance->base_address);
		instances[table_index].name = (XPTR(char))kept_address(source, (unsigned long)instance->name);
	}
	source->registered = TRUE;
	for (index = 0; index < source->buffer_count; index++)
	{
		unsigned long address = source->buffers[index] & ~1UL;

		if (!(source->buffers[index] & 1))
		{
			D3DVertexBuffer *buffer = XBOX_POINTER(D3DVertexBuffer, address);

			buffer->Common = D3DCOMMON_TYPE_VERTEXBUFFER | 1;
			IDirect3DVertexBuffer8_Register(buffer, NULL);
		}
		else
		{
			D3DIndexBuffer *buffer = XBOX_POINTER(D3DIndexBuffer, address);

			buffer->Common = D3DCOMMON_TYPE_INDEXBUFFER | 1;
		}
	}
}

/* ---------- public code */

/* the map whose characters are to be brought in at the next update, when
it is tried next and how often it was */
static char forge_ai_tags_pending_map[256];
static unsigned long forge_ai_tags_next_try;
static short forge_ai_tags_tries;

/* (the first map's load, the menus' as the game starts) the data files a
copy of the game left behind when it stopped with a map's characters in
(a crash: forge_ai_tags_map_unloaded deletes them otherwise), deleted */
static void forge_ai_tags_clear_leftovers(
	void)
{
	static boolean cleared;
	short faction;
	short map;

	if (cleared)
		return;
	cleared = TRUE;
	for (faction = 0; faction < NUMBER_OF_FORGE_AI_FACTIONS; faction++)
	{
		for (map = 0; map < FORGE_AI_MAXIMUM_MAPS && forge_ai_factions[faction].maps[map]; map++)
		{
			char path[64];

			snprintf(path, sizeof(path), "z:\\forge_ai_%s.dat", forge_ai_factions[faction].maps[map]);
			if (DeleteFileA(path))
				platform_log("forge ai: %s left behind by an earlier run: deleted", path);
		}
	}
}

void forge_ai_tags_loaded(
	char const *scenario_name)
{
	short faction;

	forge_ai_tags_clear_leftovers();
	if (forge_ai_tags.loaded || forge_ai_tags.source_count)
		forge_ai_tags_unloaded();
	forge_ai_tags_pending_map[0] = 0;
	if (forge_ai_tags_wanted(scenario_name))
		snprintf(forge_ai_tags_pending_map, sizeof(forge_ai_tags_pending_map), "%s", scenario_name);
	forge_ai_tags_next_try = system_milliseconds();
	forge_ai_tags_tries = 0;
	for (faction = 0; faction < NUMBER_OF_FORGE_AI_FACTIONS; faction++)
		forge_ai_tags.faction_source[faction] = NONE;
}

/* (the first update once the map has loaded: its load leaves a 32-bit
build no room for a mission's tags until it is done) each mission read in
turn, its characters' tags kept apart and the rest let go before the next */
static boolean forge_ai_tags_bring(
	char const *scenario_name)
{
	char const *chosen[NUMBER_OF_FORGE_AI_FACTIONS];
	unsigned long started;
	long existing;
	long index;
	long salt = 0;
	long next;
	short faction;
	struct cache_file_tag_instance *table = NULL;

	csmemset(&forge_ai_tags, 0, sizeof(forge_ai_tags));
	for (faction = 0; faction < NUMBER_OF_FORGE_AI_FACTIONS; faction++)
		forge_ai_tags.faction_source[faction] = NONE;
	started = system_milliseconds();
	/* (each faction's mission: the first of its in the maps folder) */
	for (faction = 0; faction < NUMBER_OF_FORGE_AI_FACTIONS; faction++)
	{
		short map;

		chosen[faction] = NULL;
		for (map = 0; map < FORGE_AI_MAXIMUM_MAPS && forge_ai_factions[faction].maps[map] && !chosen[faction]; map++)
		{
			char path[256];
			HANDLE file;

			snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), forge_ai_factions[faction].maps[map]);
			file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if (file != INVALID_HANDLE_VALUE)
			{
				CloseHandle(file);
				chosen[faction] = forge_ai_factions[faction].maps[map];
			}
		}
		if (!chosen[faction])
			platform_log("forge ai: %s: none of its missions is in the maps folder: not offered",
				forge_ai_factions[faction].name);
	}
	/* (the table: the map's, then ours) */
	forge_ai_tags.original_instances = cache_files_tag_instances(&existing);
	forge_ai_tags.original_count = existing;
	if (!forge_ai_tags.original_instances || existing <= 0 || existing >= UNSIGNED_SHORT_MAX)
		goto refused;
	for (index = 0; index < existing; index++)
	{
		long instance_salt = (long)((unsigned long)
			((struct cache_file_tag_instance *)forge_ai_tags.original_instances)[index].tag_index >> 16);

		salt = MAX(salt, instance_salt);
	}
	salt = (salt + 1) & 0x7FFF;
	table = malloc((size_t)(existing + FORGE_AI_MAXIMUM_TAGS) * sizeof(*table));
	forge_ai_tags.tag_source = malloc(FORGE_AI_MAXIMUM_TAGS * sizeof(short));
	if (!table || !forge_ai_tags.tag_source)
		goto refused;
	csmemcpy(table, forge_ai_tags.original_instances, (size_t)existing * sizeof(*table));
	forge_ai_tags.instances = table;
	forge_ai_tags.first_index = existing;
	next = existing;
	/* (the room for the largest mission's tags, made before anything else
takes memory: a 32-bit build has little left in one piece) */
	for (faction = 0; faction < NUMBER_OF_FORGE_AI_FACTIONS; faction++)
	{
		struct forge_ai_source probe;
		char why[160];
		HANDLE file;

		if (!chosen[faction])
			continue;
		csmemset(&probe, 0, sizeof(probe));
		file = source_open(chosen[faction], &probe, why, sizeof(why));
		if (file)
		{
			CloseHandle(file);
			forge_ai_room_size = MAX(forge_ai_room_size, probe.tag_data_size);
		}
	}
	forge_ai_tag_room = cache_files_tag_room(&forge_ai_tag_room_size);
	forge_ai_tag_room_used = 0;
	/* (the room the checks keep the loaded map's state in while a mission's
	tags are checked, then the room the tags are read into: both made
	first, before anything else takes memory) */
	if (!forge_ai_room_size)
	{
		platform_log("forge ai: none of the campaign maps there can be read");
		goto refused;
	}
	forge_ai_room = tag_validate_other_tags_room(TRUE) ? malloc(forge_ai_room_size) : NULL;
	if (!forge_ai_room)
	{
		tag_validate_other_tags_room(FALSE);
		/* (the load's memory not let go yet - the shaders made at the
		loading screen -: tried again in a moment) */
		platform_log("forge ai: no memory yet to read the campaign maps' tags (%lu KB)", forge_ai_room_size >> 10);
		forge_ai_room_size = 0;
		release(table);
		forge_ai_tags.instances = NULL;
		release(forge_ai_tags.tag_source);
		forge_ai_tags.tag_source = NULL;
		return FALSE;
	}
	/* (each mission once, for every faction of it) */
	for (faction = 0; faction < NUMBER_OF_FORGE_AI_FACTIONS; faction++)
	{
		struct forge_ai_source *source;
		short source_index;
		short other;
		long *pending;
		char why[160];
		HANDLE file;

		if (!chosen[faction])
			continue;
		for (source_index = 0; source_index < forge_ai_tags.source_count &&
			strcmp(forge_ai_tags.sources[source_index].map, chosen[faction]); source_index++)
			;
		if (source_index < forge_ai_tags.source_count || forge_ai_tags.source_count >= FORGE_AI_MAXIMUM_SOURCES)
			continue;
		source = &forge_ai_tags.sources[source_index];
		if (!source_read(source, chosen[faction], why, sizeof(why)))
		{
			platform_log("forge ai: %s.map can't be used: %s", chosen[faction], why);
			source_free(source);
			/* (nor tried again for another faction of it) */
			for (other = faction; other < NUMBER_OF_FORGE_AI_FACTIONS; other++)
			{
				if (other != faction && chosen[other] && !strcmp(chosen[other], chosen[faction]))
					chosen[other] = NULL;
			}
			if (strstr(why, "memory"))
				goto retry;
			continue;
		}
		forge_ai_tags.source_count++;
		pending = malloc((size_t)source->tag_count * sizeof(long));
		for (other = faction; pending && other < NUMBER_OF_FORGE_AI_FACTIONS; other++)
		{
			if (chosen[other] && !strcmp(chosen[other], chosen[faction]))
			{
				forge_ai_tags.faction_roots[other] = source_reach(source, &forge_ai_factions[other], pending);
				if (forge_ai_tags.faction_roots[other] > 0)
					forge_ai_tags.faction_source[other] = source_index;
			}
		}
		release(pending);
		file = NULL;
		if (!source_number(source_index, table, &next, salt, why, sizeof(why)) || !source_keep(source, why, sizeof(why)) ||
			!(file = source_open(source->map, source, why, sizeof(why))) || !source_data(source, file, why, sizeof(why)))
		{
			if (file)
				CloseHandle(file);
			platform_log("forge ai: %s.map: %s", source->map, why);
			if (strstr(why, "memory"))
				goto retry;
			goto refused;
		}
		CloseHandle(file);
		source_table(source, table);
		source_done(source);
	}
	tag_validate_other_tags_room(FALSE);
	release(forge_ai_room);
	forge_ai_room = NULL;
	forge_ai_room_size = 0;
	if (!forge_ai_tags.source_count)
		goto refused;
	forge_ai_tags.count = next - existing;
	cache_files_reserved_after_tags = forge_ai_tag_room_used;
	cache_files_set_tag_instances(table, next);
	forge_ai_tags.loaded = TRUE;
	forge_ai_tags.milliseconds = system_milliseconds() - started;
	for (index = 0; index < forge_ai_tags.source_count; index++)
	{
		struct forge_ai_source const *source = &forge_ai_tags.sources[index];

		platform_log("forge ai: %s.map: %lu tags brought in (%lu KB of tags, in the %s; %lu KB of pixels and samples in its "
			"data file, %ld buffers)", source->map, source->tags_brought, source->kept_size >> 10,
			source->kept_in_room ? "tag cache's room" : "heap", source->data_bytes >> 10, source->buffer_count);
	}
	for (faction = 0; faction < NUMBER_OF_FORGE_AI_FACTIONS; faction++)
	{
		if (forge_ai_tags.faction_source[faction] != NONE)
			platform_log("forge ai: %s: %d from %s.map", forge_ai_factions[faction].name,
				forge_ai_tags.faction_roots[faction], forge_ai_tags.sources[forge_ai_tags.faction_source[faction]].map);
	}
	platform_log("forge ai: %ld tags added to %s's %ld in %lu ms", forge_ai_tags.count, scenario_name, existing,
		forge_ai_tags.milliseconds);
	return TRUE;

retry:
	/* (out of memory on the way: all of it let go, tried again in a
	moment) */
	tag_validate_other_tags_room(FALSE);
	release(forge_ai_room);
	forge_ai_room = NULL;
	forge_ai_room_size = 0;
	forge_ai_tags_unloaded();
	return FALSE;

refused:
	tag_validate_other_tags_room(FALSE);
	release(forge_ai_room);
	forge_ai_room = NULL;
	forge_ai_room_size = 0;
	platform_log("forge ai: no characters brought in");
	forge_ai_tags_unloaded();
	return TRUE;
}

boolean forge_ai_tags_update(
	void)
{
	unsigned long now = system_milliseconds();

	if (!forge_ai_tags_pending_map[0] || (long)(forge_ai_tags_next_try - now) > 0)
		return FALSE;
	if (!forge_ai_tags_bring(forge_ai_tags_pending_map))
	{
		/* (no room yet: again in a second, for a while) */
		if (++forge_ai_tags_tries < FORGE_AI_MAXIMUM_TRIES)
		{
			forge_ai_tags_next_try = now + 1000;
			return FALSE;
		}
		platform_log("forge ai: no memory for the campaign maps' tags after %d tries: no characters brought in",
			FORGE_AI_MAXIMUM_TRIES);
		forge_ai_tags_unloaded();
	}
	forge_ai_tags_pending_map[0] = 0;
	return TRUE;
}

boolean forge_ai_tags_pending(
	void)
{
	return forge_ai_tags_pending_map[0] != 0;
}

void forge_ai_tags_unloaded(
	void)
{
	short index;

	for (index = 0; index < forge_ai_tags.source_count; index++)
	{
		struct forge_ai_source *source = &forge_ai_tags.sources[index];
		long buffer;

		for (buffer = 0; source->registered && source->buffers && buffer < source->buffer_count; buffer++)
		{
			unsigned long address = source->buffers[buffer] & ~1UL;

			if (!(source->buffers[buffer] & 1))
				IDirect3DVertexBuffer8_BlockUntilNotBusy(XBOX_POINTER(D3DVertexBuffer, address));
			else
				IDirect3DIndexBuffer8_BlockUntilNotBusy(XBOX_POINTER(D3DIndexBuffer, address));
		}
	}
	if (forge_ai_tags.loaded && forge_ai_tags.original_instances)
		cache_files_set_tag_instances(forge_ai_tags.original_instances, forge_ai_tags.original_count);
	cache_files_reserved_after_tags = 0;
	forge_ai_tag_room = NULL;
	forge_ai_tag_room_size = 0;
	forge_ai_tag_room_used = 0;
	for (index = 0; index < FORGE_AI_MAXIMUM_SOURCES; index++)
		source_free(&forge_ai_tags.sources[index]);
	release(forge_ai_tags.instances);
	release(forge_ai_tags.tag_source);
	csmemset(&forge_ai_tags, 0, sizeof(forge_ai_tags));
	for (index = 0; index < NUMBER_OF_FORGE_AI_FACTIONS; index++)
		forge_ai_tags.faction_source[index] = NONE;
}

/* (scenario_tags_unload) */
void forge_ai_tags_map_unloaded(
	void)
{
	forge_ai_tags_pending_map[0] = 0;
	forge_ai_tags_unloaded();
}

/* the source of one of our tags, NULL for the map's */
static struct forge_ai_source const *tag_source(
	long tag_index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index);

	if (!forge_ai_tags.loaded || tag_index == NONE || absolute < forge_ai_tags.first_index ||
		absolute >= forge_ai_tags.first_index + forge_ai_tags.count)
	{
		return NULL;
	}
	return &forge_ai_tags.sources[forge_ai_tags.tag_source[absolute - forge_ai_tags.first_index]];
}

boolean forge_ai_tags_read(
	long tag_index,
	long offset,
	long size,
	void *buffer,
	boolean *read)
{
	struct forge_ai_source const *source = tag_source(tag_index);
	long low = 0;
	long high;

	if (!source)
		return FALSE;
	*read = FALSE;
	high = source->range_count - 1;
	while (low <= high && offset >= 0 && size >= 0 && source->data_file)
	{
		long middle = (low + high) / 2;
		struct forge_ai_range const *range = &source->ranges[middle];

		if ((unsigned long)offset < range->offset)
		{
			high = middle - 1;
		}
		else if ((unsigned long)offset >= range->offset + range->size)
		{
			low = middle + 1;
		}
		else
		{
			unsigned long available = range->offset + range->size - (unsigned long)offset;
			unsigned long wanted = (unsigned long)size;
			DWORD got = 0;

			SetFilePointer(source->data_file, (LONG)(range->position + ((unsigned long)offset - range->offset)), NULL,
				FILE_BEGIN);
			*read = ReadFile(source->data_file, buffer, (DWORD)MIN(available, wanted), &got, NULL) &&
				got == MIN(available, wanted);
			if (wanted > available)
				csmemset((byte *)buffer + available, 0, wanted - available);
			break;
		}
	}
	if (!*read)
		platform_log("forge ai: a read of %ld bytes at %ld of %s.map's that was not kept (%s)", size, offset, source->map,
			tag_get_name(tag_index));
	return TRUE;
}

char const *forge_ai_tags_source_map(
	long tag_index)
{
	struct forge_ai_source const *source = tag_source(tag_index);

	return source ? source->map : NULL;
}

boolean forge_ai_tags_active(
	void)
{
	return forge_ai_tags.loaded;
}

short forge_ai_tags_faction_count(
	void)
{
	return NUMBER_OF_FORGE_AI_FACTIONS;
}

boolean forge_ai_tags_faction(
	short faction,
	char const **name,
	char const **map)
{
	if (faction < 0 || faction >= NUMBER_OF_FORGE_AI_FACTIONS)
		return FALSE;
	*name = forge_ai_factions[faction].name;
	*map = forge_ai_tags.faction_source[faction] != NONE ? forge_ai_tags.sources[forge_ai_tags.faction_source[faction]].map :
		NULL;
	return TRUE;
}

short forge_ai_tags_faction_category(
	short faction)
{
	return faction >= 0 && faction < NUMBER_OF_FORGE_AI_FACTIONS ? forge_ai_factions[faction].category : NONE;
}

/* whether the Forge menu offers a tag: the map's own, an actor variant, or
one of a pack's own objects (not a tag they need: their weapons, effects,
the campaign's energy sword) */
boolean forge_ai_tags_offered(
	long tag_index)
{
	return !tag_source(tag_index) || tag_get_group_tag(tag_index) == FORGE_AI_ACTOR_VARIANT_TAG ||
		forge_ai_tags_faction_of(tag_index) != NONE;
}

/* the faction a character's tag is of (its actor variant's name), NONE */
short forge_ai_tags_faction_of(
	long tag_index)
{
	char const *name = tag_index != NONE ? tag_get_name(tag_index) : NULL;
	short faction;
	short root;

	for (faction = 0; name && faction < NUMBER_OF_FORGE_AI_FACTIONS; faction++)
	{
		for (root = 0; root < FORGE_AI_MAXIMUM_ROOTS && forge_ai_factions[faction].roots[root]; root++)
		{
			if (!_stricmp(name, root_path(forge_ai_factions[faction].roots[root])))
				return faction;
		}
	}
	return NONE;
}

unsigned long forge_ai_tags_bytes(
	void)
{
	unsigned long bytes = 0;
	short index;

	for (index = 0; index < forge_ai_tags.source_count; index++)
		bytes += forge_ai_tags.sources[index].kept_size;
	return bytes;
}

unsigned long forge_ai_tags_data_bytes(
	void)
{
	unsigned long bytes = 0;
	short index;

	for (index = 0; index < forge_ai_tags.source_count; index++)
		bytes += forge_ai_tags.sources[index].data_bytes;
	return bytes;
}

unsigned long forge_ai_tags_load_milliseconds(
	void)
{
	return forge_ai_tags.milliseconds;
}

#endif
