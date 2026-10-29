/*
STUBBS_MODE.C

A first look at Stubbs the Zombie's maps on this engine (2005, Wideload, on
the Halo engine), played as a map pack whose folder is maps_stubbs with
game.pack_menus = "stubbs". Bring your own copy: none of its files ship.

Stubbs's Xbox maps are Xbox cache files (version 5, build "400"), compressed
as Halo's are, with Halo's tag header. Their tags are linked for a tag cache
at 0x8038B000, 0x1B000 below Halo's (0x803A6000), so with this mode on the
tag cache is placed there (physical_memory_map.c). The native builds keep
their game state above the tag cache (halo_port_capacity.h), so the room
below it is free.

Everything here is for development builds only (not HALO_RELEASE) and only
with the stubbs pack's own menus: stock Halo, Custom Edition and the other
packs never take these paths.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "errors.h"
#include "halo_map_packs.h"
#include "tag_files/tag_files.h"
#include "tag_schema.h"
#include "structures/structure_bsp_definitions.h"
#include "physics/collision_model_definitions.h"
#include "stubbs_mode.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

boolean stubbs_mode_active(
	void)
{
#if defined(HALO_RELEASE) || !defined(HALO_MAP_LIST)
	return FALSE;
#else
	static int active = -1;

	if (active < 0)
	{
		char const *pack = map_pack_menus();

		active = pack && !_stricmp(pack, STUBBS_MODE_PACK_ID);
	}
	return active ? TRUE : FALSE;
#endif
}

unsigned long stubbs_mode_tag_cache_base(
	unsigned long halo_base)
{
	return stubbs_mode_active() ? STUBBS_XBOX_TAG_CACHE_BASE : halo_base;
}

void stubbs_mode_log(
	char const *format,
	...)
{
	char text[512];
	va_list arguments;

	if (!stubbs_mode_active())
		return;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	error(_error_silent, "stubbs mode: %s", text);
}

/* ---------- Stubbs's units, as Halo's

Stubbs's unit definitions (bipd, vehi) are 0x174 bytes longer than Halo's:
the object part (0x17C bytes) is Halo's, the biped and vehicle parts after
the unit part are Halo's, and the unit part has fields of Stubbs's own
among Halo's. Each is rewritten in place, in Halo's layout (it is longer,
so it fits): Halo's fields copied from where Stubbs has them, as far as
they are known, the rest zero. Not known yet, and left empty: the unit's
HUDs, dialogue, powered seats, initial weapons and seats */

enum
{
	HALO_UNIT_SIZE = 0x2F0,
	STUBBS_UNIT_SIZE = 0x464,
	STUBBS_UNIT_GROWTH = STUBBS_UNIT_SIZE - HALO_UNIT_SIZE,
	HALO_BIPED_SIZE = 0x4F4,
	HALO_VEHICLE_SIZE = 0x3F0,
};

static struct
{
	unsigned long halo_offset;
	unsigned long stubbs_offset;
	unsigned long size;
} const stubbs_unit_fields[] =
{
	/* flags, default team, constant sound, child damage fraction */
	{ 0x17C, 0x17C, 0x0C },
	/* integrated light toggle effect to the camera's tracks (Stubbs has 0x20
	bytes of its own before it) */
	{ 0x188, 0x1A8, 0x78 },
	/* acceleration scale to feign death repeat chance: 0x44 bytes of
	Stubbs's are among these, where is not known yet; taken from the end */
	{ 0x200, 0x264, 0x4C },
	/* spawned actor variant to the melee damage and blip type */
	{ 0x24C, 0x2B0, 0x5C },
};

static unsigned char *stubbs_host(unsigned char *base, unsigned long size, unsigned long address,
	unsigned long length)
{
	unsigned long offset = address - STUBBS_XBOX_TAG_CACHE_BASE;

	if (address < STUBBS_XBOX_TAG_CACHE_BASE || offset > size || length > size - offset)
		return NULL;
	return base + offset;
}

static void stubbs_unit_convert(unsigned char *definition, unsigned long halo_part_size)
{
	static unsigned char stubbs[STUBBS_UNIT_SIZE + 0x400];
	size_t index;

	memcpy(stubbs, definition, STUBBS_UNIT_SIZE + halo_part_size);
	memset(definition + 0x17C, 0, HALO_UNIT_SIZE - 0x17C);
	for (index = 0; index < sizeof(stubbs_unit_fields) / sizeof(*stubbs_unit_fields); index++)
		memcpy(definition + stubbs_unit_fields[index].halo_offset, stubbs + stubbs_unit_fields[index].stubbs_offset,
			stubbs_unit_fields[index].size);
	memcpy(definition + HALO_UNIT_SIZE, stubbs + STUBBS_UNIT_SIZE, halo_part_size);
}

/* an animation graph's unit seats: Stubbs's are 0x80 bytes, Halo's 0x64 and
the same as the first 0x64 of Stubbs's (Stubbs adds a block and padding) */
enum
{
	ANIMATION_GRAPH_UNITS_OFFSET = 0x0C,
	HALO_ANIMATION_UNIT_SEAT_SIZE = 0x64,
	STUBBS_ANIMATION_UNIT_SEAT_SIZE = 0x80,
	/* its animations: Stubbs's are 0xBC bytes, Halo's 0xB4, Stubbs having
	8 bytes of its own at 0x30 (zero in the maps looked at) */
	ANIMATION_GRAPH_ANIMATIONS_OFFSET = 0x74,
	HALO_ANIMATION_SIZE = 0xB4,
	STUBBS_ANIMATION_SIZE = 0xBC,
	STUBBS_ANIMATION_INSERT_OFFSET = 0x30,
	STUBBS_ANIMATION_INSERT_SIZE = 0x08,
};

/* Stubbs's animations' frame data is in the map file, not its tags (the
Xbox streamed it): each one's data (a tag_data, Halo's at 0xA0) has a size
and a file offset and no address. It is read into one block of the game's
heap (which is in the Xbox address space) and the addresses set */
enum
{
	HALO_ANIMATION_DATA_OFFSET = 0xA0,
	TAG_DATA_SIZE_OFFSET = 0x00,
	TAG_DATA_FILE_OFFSET_OFFSET = 0x08,
	TAG_DATA_ADDRESS_OFFSET = 0x0C,
};

static void *stubbs_animation_data;
static unsigned long stubbs_animation_data_bytes;
static boolean (*stubbs_file_read)(long offset, long size, void *buffer);

static unsigned long stubbs_animation_data_size(unsigned char *animation)
{
	long data_size, address;

	memcpy(&data_size, animation + HALO_ANIMATION_DATA_OFFSET + TAG_DATA_SIZE_OFFSET, 4);
	memcpy(&address, animation + HALO_ANIMATION_DATA_OFFSET + TAG_DATA_ADDRESS_OFFSET, 4);
	return data_size > 0 && !address ? (unsigned long)data_size : 0;
}

static unsigned long stubbs_animation_data_read(unsigned char *animation, unsigned char *into)
{
	unsigned long data_size = stubbs_animation_data_size(animation);
	long file_offset;
	unsigned long address;

	if (!data_size)
		return 0;
	memcpy(&file_offset, animation + HALO_ANIMATION_DATA_OFFSET + TAG_DATA_FILE_OFFSET_OFFSET, 4);
	if (!stubbs_file_read || !stubbs_file_read(file_offset, (long)data_size, into))
		return 0;
	address = (unsigned long)(__UINTPTR_TYPE__)xbox_address(into);
	memcpy(animation + HALO_ANIMATION_DATA_OFFSET + TAG_DATA_ADDRESS_OFFSET, &address, 4);
	return (data_size + 15) & ~15UL;
}

static long stubbs_animation_convert(unsigned char *base, unsigned long size, unsigned long address)
{
	unsigned char *graph = stubbs_host(base, size, address, 0x80);
	unsigned char *seats;
	unsigned long count, seats_address, index;

	if (!graph)
		return 0;
	memcpy(&count, graph + ANIMATION_GRAPH_UNITS_OFFSET, 4);
	memcpy(&seats_address, graph + ANIMATION_GRAPH_UNITS_OFFSET + 4, 4);
	{
		unsigned long animation_count, animations_address;
		unsigned char *animations;

		memcpy(&animation_count, graph + ANIMATION_GRAPH_ANIMATIONS_OFFSET, 4);
		memcpy(&animations_address, graph + ANIMATION_GRAPH_ANIMATIONS_OFFSET + 4, 4);
		animations = animation_count && animation_count <= 0x1000 ?
			stubbs_host(base, size, animations_address, animation_count * STUBBS_ANIMATION_SIZE) : NULL;
		for (index = 0; animations && index < animation_count; index++)
		{
			unsigned char *from = animations + index * STUBBS_ANIMATION_SIZE;
			unsigned char *to = animations + index * HALO_ANIMATION_SIZE;

			memmove(to, from, STUBBS_ANIMATION_INSERT_OFFSET);
			memmove(to + STUBBS_ANIMATION_INSERT_OFFSET,
				from + STUBBS_ANIMATION_INSERT_OFFSET + STUBBS_ANIMATION_INSERT_SIZE,
				HALO_ANIMATION_SIZE - STUBBS_ANIMATION_INSERT_OFFSET);
		}
	}
	if (!count || count > 0x1000)
		return 1;
	seats = stubbs_host(base, size, seats_address, count * STUBBS_ANIMATION_UNIT_SEAT_SIZE);
	if (!seats)
		return 0;
	for (index = 0; index < count; index++)
		memmove(seats + index * HALO_ANIMATION_UNIT_SEAT_SIZE, seats + index * STUBBS_ANIMATION_UNIT_SEAT_SIZE,
			HALO_ANIMATION_UNIT_SEAT_SIZE);
	return 1;
}

static unsigned long stubbs_animation_data_measure(unsigned char *animation, unsigned char *into)
{
	(void)into;
	return stubbs_animation_data_size(animation);
}

/* each animation of every graph (in Halo's layout by now), passed to visit;
the sum of what it returns */
static unsigned long stubbs_animations_each(unsigned char *base, unsigned long size, unsigned long instances,
	unsigned long count, unsigned long (*visit)(unsigned char *animation, unsigned char *into), unsigned char *into)
{
	unsigned long index, total = 0;

	for (index = 0; index < count; index++)
	{
		unsigned char *instance = stubbs_host(base, size, instances, count * 0x20) + index * 0x20;
		unsigned long group, address, animation_count, animations_address, animation_index;
		unsigned char *graph, *animations;

		memcpy(&group, instance, 4);
		memcpy(&address, instance + 0x14, 4);
		if (group != 'antr' || !(graph = stubbs_host(base, size, address, 0x80)))
			continue;
		memcpy(&animation_count, graph + ANIMATION_GRAPH_ANIMATIONS_OFFSET, 4);
		memcpy(&animations_address, graph + ANIMATION_GRAPH_ANIMATIONS_OFFSET + 4, 4);
		if (!animation_count || animation_count > 0x1000 ||
			!(animations = stubbs_host(base, size, animations_address, animation_count * HALO_ANIMATION_SIZE)))
		{
			continue;
		}
		for (animation_index = 0; animation_index < animation_count; animation_index++)
		{
			unsigned long used = visit(animations + animation_index * HALO_ANIMATION_SIZE, into ? into + total : NULL);

			total += visit == stubbs_animation_data_measure ? (used + 15) & ~15UL : used;
		}
	}
	return total;
}

/* ---------- sound classes

Stubbs's sounds use classes that are unused slots in Halo's (sound_classes.c:
20, 48 and others; their names are not known), which halt the game. Each is
played as the nearest named class below it */
enum
{
	SOUND_CLASS_OFFSET = 0x04,
};

extern char const *sound_class_names[];

static boolean stubbs_sound_class_convert(unsigned char *field)
{
	short sound_class, halo_class;

	memcpy(&sound_class, field, 2);
	halo_class = sound_class < 0 ? 0 : sound_class > 50 ? 50 : sound_class;
	while (halo_class > 0 && !sound_class_names[halo_class][0])
		halo_class--;
	if (halo_class == sound_class)
		return FALSE;
	memcpy(field, &halo_class, 2);
	return TRUE;
}

/* ---------- blocks whose elements are longer in Stubbs

A block's elements rewritten in place from Stubbs's size to Halo's (never
longer), Halo's fields copied from where Stubbs has them, the rest zero.
Layouts from Sigmmma's reclaimer (GPL-3.0), stubbs/defs, against its
hek/defs for Halo's */

struct stubbs_copy
{
	unsigned short halo_offset;
	unsigned short stubbs_offset;
	unsigned short size;
};

struct stubbs_element_layout
{
	unsigned short halo_size;
	unsigned short stubbs_size;
	struct stubbs_copy const *copies;
	short copy_count;
};

static unsigned char *stubbs_tags_base;
static unsigned long stubbs_tags_size;

/* the block (count, address) at block, its elements repacked: their new
address (the same) or NULL if it is not in the tags */
static unsigned char *stubbs_block_repack(unsigned char *block, struct stubbs_element_layout const *layout,
	unsigned long *count_out)
{
	static unsigned char element[0x1000];
	unsigned long count, address, index;
	unsigned char *elements;
	short copy;

	*count_out = 0;
	memcpy(&count, block, 4);
	memcpy(&address, block + 4, 4);
	if (!count || count > 0x10000 ||
		!(elements = stubbs_host(stubbs_tags_base, stubbs_tags_size, address, count * layout->stubbs_size)) ||
		layout->stubbs_size > sizeof(element))
	{
		return NULL;
	}
	for (index = 0; index < count; index++)
	{
		memcpy(element, elements + index * layout->stubbs_size, layout->stubbs_size);
		memset(elements + index * layout->halo_size, 0, layout->halo_size);
		for (copy = 0; copy < layout->copy_count; copy++)
			memcpy(elements + index * layout->halo_size + layout->copies[copy].halo_offset,
				element + layout->copies[copy].stubbs_offset, layout->copies[copy].size);
	}
	*count_out = count;
	return elements;
}

/* a collision model's materials, regions and their permutations (stubbs/defs/coll.py) */
static struct stubbs_copy const stubbs_collision_material_copies[] =
{
	{ 0x00, 0x00, 0x30 }, /* name, flags, type, shield leak and multiplier */
	{ 0x3C, 0x4C, 0x04 }, /* body damage multiplier */
};
static struct stubbs_copy const stubbs_collision_region_copies[] =
{
	{ 0x00, 0x00, 0x24 }, /* name, flags */
	{ 0x28, 0x38, 0x04 }, /* damage threshold */
	{ 0x38, 0x88, 0x10 }, /* destroyed effect (Stubbs adds garbage and a weapon) */
	{ 0x48, 0xD4, 0x0C }, /* permutations */
};
static struct stubbs_copy const stubbs_collision_permutation_copies[] =
{
	{ 0x00, 0x00, 0x20 }, /* name */
};
static struct stubbs_element_layout const stubbs_collision_material = { 0x48, 0x90, stubbs_collision_material_copies, 2 };
static struct stubbs_element_layout const stubbs_collision_region = { 0x54, 0xE0, stubbs_collision_region_copies, 4 };
static struct stubbs_element_layout const stubbs_collision_permutation = { 0x20, 0x80, stubbs_collision_permutation_copies, 1 };

static void stubbs_collision_convert(unsigned char *model)
{
	unsigned char *regions;
	unsigned long count, index;

	stubbs_block_repack(model + offsetof(struct collision_model, resistance.materials), &stubbs_collision_material, &count);
	regions = stubbs_block_repack(model + offsetof(struct collision_model, resistance.regions), &stubbs_collision_region,
		&count);
	for (index = 0; regions && index < count; index++)
	{
		unsigned long permutations;

		stubbs_block_repack(regions + index * stubbs_collision_region.halo_size + 0x48, &stubbs_collision_permutation,
			&permutations);
	}
}

void stubbs_mode_set_file_reader(
	boolean (*read)(long offset, long size, void *buffer))
{
	stubbs_file_read = read;
}

void stubbs_mode_convert_tags(
	void *tag_cache,
	unsigned long tag_data_size)
{
	unsigned char *base = tag_cache;
	unsigned long instances, count, index;
	long converted[2] = { 0, 0 };
	long converted_animations = 0;
	long converted_collision = 0;
	long converted_sounds = 0;

	if (!stubbs_mode_active() || tag_data_size < 0x28)
		return;
	memcpy(&instances, base, 4);
	memcpy(&count, base + 0x0C, 4);
	stubbs_tags_base = base;
	stubbs_tags_size = tag_data_size;
	if (!stubbs_host(base, tag_data_size, instances, count * 0x20))
	{
		stubbs_mode_log("the tag header is not Stubbs's: nothing converted");
		return;
	}
	for (index = 0; index < count; index++)
	{
		unsigned char *instance = stubbs_host(base, tag_data_size, instances, count * 0x20) + index * 0x20;
		unsigned long group, address;
		unsigned long halo_part = 0;
		unsigned char *definition;
		int kind;

		memcpy(&group, instance, 4);
		memcpy(&address, instance + 0x14, 4);
		if (group == 'snd!')
		{
			unsigned char *sound = stubbs_host(base, tag_data_size, address, 8);

			if (sound && stubbs_sound_class_convert(sound + SOUND_CLASS_OFFSET))
				converted_sounds++;
			continue;
		}
		if (group == 'coll')
		{
			unsigned char *model = stubbs_host(base, tag_data_size, address, sizeof(struct collision_model));

			if (model)
			{
				stubbs_collision_convert(model);
				converted_collision++;
			}
			continue;
		}
		if (group == 'antr')
		{
			converted_animations += stubbs_animation_convert(base, tag_data_size, address);
			continue;
		}
		if (group == 'bipd')
			halo_part = HALO_BIPED_SIZE - HALO_UNIT_SIZE, kind = 0;
		else if (group == 'vehi')
			halo_part = HALO_VEHICLE_SIZE - HALO_UNIT_SIZE, kind = 1;
		else
			continue;
		definition = stubbs_host(base, tag_data_size, address, STUBBS_UNIT_SIZE + halo_part);
		if (!definition)
			continue;
		stubbs_unit_convert(definition, halo_part);
		converted[kind]++;
	}
	stubbs_mode_log("%ld bipeds and %ld vehicles rewritten in Halo's unit layout", converted[0], converted[1]);
	stubbs_mode_log("%ld animation graphs rewritten in Halo's layout", converted_animations);
	stubbs_mode_log("%ld sounds of classes Halo lacks played as the nearest class before", converted_sounds);
	stubbs_mode_log("%ld collision models' materials and regions rewritten in Halo's layout", converted_collision);
	{
		unsigned long needed = stubbs_animations_each(base, tag_data_size, instances, count,
			stubbs_animation_data_measure, NULL);
		unsigned long read;

		if (stubbs_animation_data)
		{
			system_free(stubbs_animation_data);
			stubbs_animation_data = NULL;
		}
		if (needed)
			stubbs_animation_data = system_calloc(1, (long)needed + 16);
		stubbs_animation_data_bytes = stubbs_animation_data ? needed : 0;
		read = stubbs_animation_data ? stubbs_animations_each(base, tag_data_size, instances, count,
			stubbs_animation_data_read, stubbs_animation_data) : 0;
		stubbs_mode_log("animation frame data: %lu bytes in the map file, %lu read", needed, read);
	}
}

boolean stubbs_mode_owns_data(
	void const *address,
	long size)
{
	unsigned char const *start = stubbs_animation_data;

	return stubbs_mode_active() && start && size >= 0 && (unsigned char const *)address >= start &&
		(unsigned long)((unsigned char const *)address - start) <= stubbs_animation_data_bytes &&
		(unsigned long)size <= stubbs_animation_data_bytes - (unsigned long)((unsigned char const *)address - start);
}

/* ---------- Stubbs's structure BSPs, as Halo's

A cluster's 12 bytes where Halo has its surface indices are something else
in Stubbs (Sigmmma's reclaimer, GPL-3.0, stubbs/defs/sbsp.py: "different
data here than surface indices"): left empty. Stubbs's 24 bytes of floats
where Halo has padding are left as they are */
enum
{
	CLUSTER_SIZE = 0x68,
	CLUSTER_SURFACE_INDICES_OFFSET = 0x44,
};

void stubbs_mode_convert_structure_bsp(
	void *bsp_base,
	unsigned long size)
{
	unsigned char *base = bsp_base;
	unsigned long bsp_address, clusters_count, clusters_address, base_address, index;
	unsigned char *bsp, *clusters;

	if (!stubbs_mode_active() || size < 0x18)
		return;
	base_address = (unsigned long)(__UINTPTR_TYPE__)xbox_address(base);
	memcpy(&bsp_address, base, 4);
	if (bsp_address < base_address || bsp_address - base_address > size - sizeof(struct structure_bsp))
		return;
	bsp = base + (bsp_address - base_address);
	memcpy(&clusters_count, bsp + offsetof(struct structure_bsp, clusters), 4);
	memcpy(&clusters_address, bsp + offsetof(struct structure_bsp, clusters) + 4, 4);
	if (clusters_address < base_address || clusters_count > 8192 ||
		clusters_address - base_address > size || clusters_count * CLUSTER_SIZE > size - (clusters_address - base_address))
	{
		return;
	}
	clusters = base + (clusters_address - base_address);
	for (index = 0; index < clusters_count; index++)
		memset(clusters + index * CLUSTER_SIZE + CLUSTER_SURFACE_INDICES_OFFSET, 0, 12);
	stubbs_mode_log("structure bsp: %lu clusters' surface indices left empty", clusters_count);
}
