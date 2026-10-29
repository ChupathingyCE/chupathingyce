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

void stubbs_mode_convert_tags(
	void *tag_cache,
	unsigned long tag_data_size)
{
	unsigned char *base = tag_cache;
	unsigned long instances, count, index;
	long converted[2] = { 0, 0 };
	long converted_animations = 0;

	if (!stubbs_mode_active() || tag_data_size < 0x28)
		return;
	memcpy(&instances, base, 4);
	memcpy(&count, base + 0x0C, 4);
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
	stubbs_mode_log("%ld animation graphs' unit seats rewritten in Halo's layout", converted_animations);
}
