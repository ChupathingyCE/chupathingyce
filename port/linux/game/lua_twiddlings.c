/*
LUA_TWIDDLINGS.C

Twiddlings (lua_twiddlings.h): Chimera Lua scripts' read_* and write_*,
which name Halo PC's addresses, translated to this game's memory. This
game's memory is not Halo PC's: its objects and players are laid out as the
Xbox's, the 64-bit builds keep the Xbox's memory in a window of their own,
and nothing lives at haloce.exe's addresses. So no address a script gives is
used as it is. Each is resolved by the twiddlings table to one of:
  - the tag cache: a Custom Edition map's tags are read to Halo PC's
    addresses (0x40440000 up, cache_files.c), and Halo PC's tags are laid
    out as the Xbox's, so a tag's field is at the address a script expects.
    Reads and writes, as on Halo PC (until the map loads again). The tag
    index is the exception: this game's loader keeps the instances
    elsewhere, so Halo PC's (right after the header) are read from them,
    and get_tag gives Halo PC's addresses. Three
    things are not as Halo PC had them: gbxmodels made Xbox models
    (ce_models.c), the HUD's high-resolution scale flags (ce_hud.c) and the
    BSPs' vertices (ce_bsp.c);
  - a named Halo PC global the game has as a variable of its own (the
    difficulty, the players' look rates): read and written through it;
  - anything else: reads give 0 and writes are refused, each address logged
    once.
The objects and the players (get_object, get_player) come next: their
Halo PC fields mapped onto the Xbox's by name.

The setting lua.twiddlings turns it all off (every read 0, every write
refused), to try a script without its memory access.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifdef HALO_LUA

#include "cseries.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "game/players.h"

#include "lua_internal.h"
#include "lua_twiddlings.h"

#include <stdio.h>
#include <string.h>

/* (port/linux/src/port_config.c) */
int config_boolean(char const *name);

/* ---------- constants */

/* Halo PC's tag index (as Halo PC and this game read a Custom Edition
map): the header at the tag cache's base, its instances 0x20 bytes each */
#define TAG_HEADER_INSTANCES_OFFSET 0x0
#define TAG_HEADER_COUNT_OFFSET 0xC
#define TAG_INSTANCE_SIZE 0x20
#define TAG_INSTANCE_GROUP_OFFSET 0x0
#define TAG_INSTANCE_INDEX_OFFSET 0xC
#define TAG_INSTANCE_NAME_OFFSET 0x10
/* where Halo PC has the tag index: right after the header. This game's
loader keeps the instances elsewhere (the header says where), so a script's
address in Halo PC's index is moved onto them */
#define PC_TAG_INSTANCES_OFFSET 0x28

/* ---------- types */

enum twiddlings_access
{
	_twiddlings_read,
	_twiddlings_write
};

/* a named Halo PC global, or count of them stride bytes apart (one a local
player): the access is handed the element's index and its offset in it */
struct twiddlings_global
{
	unsigned int address;
	unsigned int stride;
	unsigned short count;
	unsigned short size;
	char const *name;
	boolean (*access)(enum twiddlings_access access, short index, unsigned int offset, void *value,
		unsigned int size);
};

/* ---------- prototypes */

static boolean twiddlings_difficulty(enum twiddlings_access access, short index, unsigned int offset, void *value,
	unsigned int size);
static boolean twiddlings_look_yaw_rate(enum twiddlings_access access, short index, unsigned int offset,
	void *value, unsigned int size);
static boolean twiddlings_look_pitch_rate(enum twiddlings_access access, short index, unsigned int offset,
	void *value, unsigned int size);

/* ---------- globals */

/* the twiddlings table's named globals: Halo CE 1.0.10's haloce.exe
addresses scripts name, and what they are here. See the design notes for
how each was identified */
static struct twiddlings_global const twiddlings_globals[] =
{
	/* the game's difficulty (a short in the game's options) */
	{ 0x006C7980 + 0x1A6, 0, 1, sizeof(short), "game difficulty", twiddlings_difficulty },
	/* each local player's look rates (player0_look_yaw_rate and the rest) */
	{ 0x006AAACC, 0x85C, MAXIMUM_NUMBER_OF_LOCAL_PLAYERS, sizeof(real), "look yaw rate", twiddlings_look_yaw_rate },
	{ 0x006AAAD0, 0x85C, MAXIMUM_NUMBER_OF_LOCAL_PLAYERS, sizeof(real), "look pitch rate", twiddlings_look_pitch_rate },
};

/* ---------- private code */

static boolean twiddlings_enabled(
	void)
{
	return config_boolean("lua.twiddlings");
}

/* the Xbox address of a pointer into the game's memory, as a script sees
addresses: 32 bits */
static unsigned int twiddlings_address_of(
	void const *pointer)
{
	return (unsigned int)POINTER_BITS(XBOX_ADDRESS(pointer));
}

static void *twiddlings_pointer(
	unsigned int address)
{
	return XBOX_POINTER(void, address);
}

/* the tag cache's Halo PC addresses: a Custom Edition map's alone */
static boolean twiddlings_tag_cache(
	unsigned int *base,
	unsigned int *end)
{
#ifdef HALO_CUSTOM_EDITION
	unsigned long size = 0;
	void *cache;

	if (!cache_file_tags_are_ce())
		return FALSE;
	cache = cache_files_tag_cache(&size);
	if (!cache || !size)
		return FALSE;
	*base = twiddlings_address_of(cache);
	*end = *base + (unsigned int)size;
	return TRUE;
#else
	(void)base;
	(void)end;
	return FALSE;
#endif
}

/* the tag index's instances, as this game has them, and their count */
static boolean twiddlings_tag_index(
	unsigned int *instances,
	unsigned int *count)
{
	unsigned int base, end;

	if (!twiddlings_tag_cache(&base, &end))
		return FALSE;
	*instances = *(unsigned int const *)twiddlings_pointer(base + TAG_HEADER_INSTANCES_OFFSET);
	*count = *(unsigned int const *)twiddlings_pointer(base + TAG_HEADER_COUNT_OFFSET);
	return *instances != 0;
}

/* Halo PC's tag index (its header's instances pointer, and the instances
at base + 0x28), moved onto this game's: TRUE when the address is there */
static boolean twiddlings_tag_index_read(
	unsigned int address,
	void *value,
	unsigned int size)
{
	unsigned int base, end, instances, count, first;

	if (!twiddlings_tag_cache(&base, &end) || !twiddlings_tag_index(&instances, &count))
		return FALSE;
	/* the header's instances pointer: Halo PC's */
	if (address == base + TAG_HEADER_INSTANCES_OFFSET && size == sizeof(unsigned int))
	{
		unsigned int pc_instances = base + PC_TAG_INSTANCES_OFFSET;

		memcpy(value, &pc_instances, sizeof(pc_instances));
		return TRUE;
	}
	first = base + PC_TAG_INSTANCES_OFFSET;
	if (instances == first || address < first || (address - first) / TAG_INSTANCE_SIZE >= count ||
		(address - first) % TAG_INSTANCE_SIZE + size > TAG_INSTANCE_SIZE)
	{
		return FALSE;
	}
	memcpy(value, twiddlings_pointer(instances + (address - first)), size);
	return TRUE;
}

/* the host pointer of [address, address + size) when all of it is in the
tag cache, else NULL */
static void *twiddlings_tag_cache_pointer(
	unsigned int address,
	unsigned int size)
{
	unsigned int base, end;

	if (!twiddlings_tag_cache(&base, &end))
		return NULL;
	if (address < base || address >= end || size > end - address)
		return NULL;
	return twiddlings_pointer(address);
}

static struct twiddlings_global const *twiddlings_global_at(
	unsigned int address,
	unsigned int size,
	short *index,
	unsigned int *offset)
{
	short global;

	for (global = 0; global < (short)NUMBEROF(twiddlings_globals); global++)
	{
		struct twiddlings_global const *entry = &twiddlings_globals[global];
		unsigned int stride = entry->stride ? entry->stride : entry->size;
		unsigned int relative;

		if (address < entry->address)
			continue;
		relative = address - entry->address;
		if (relative / stride >= entry->count)
			continue;
		if (relative % stride + size > entry->size)
			continue;
		*index = (short)(relative / stride);
		*offset = relative % stride;
		return entry;
	}
	return NULL;
}

static boolean twiddlings_access_global(
	enum twiddlings_access access,
	unsigned int address,
	void *value,
	unsigned int size)
{
	short index = 0;
	unsigned int offset = 0;
	struct twiddlings_global const *entry = twiddlings_global_at(address, size, &index, &offset);

	if (!entry)
		return FALSE;
	return entry->access(access, index, offset, value, size);
}

static boolean twiddlings_difficulty(
	enum twiddlings_access access,
	short index,
	unsigned int offset,
	void *value,
	unsigned int size)
{
	short difficulty;

	(void)index;
	if (access == _twiddlings_write)
		return FALSE;
	if (!game_time_initialized())
		return FALSE;
	difficulty = game_difficulty_level_get();
	memcpy(value, (byte const *)&difficulty + offset, size);
	return TRUE;
}

static boolean twiddlings_real(
	real *variable,
	enum twiddlings_access access,
	unsigned int offset,
	void *value,
	unsigned int size)
{
	if (offset || size != sizeof(real))
		return FALSE;
	if (access == _twiddlings_read)
		memcpy(value, variable, sizeof(real));
	else
		memcpy(variable, value, sizeof(real));
	return TRUE;
}

static boolean twiddlings_look_yaw_rate(
	enum twiddlings_access access,
	short index,
	unsigned int offset,
	void *value,
	unsigned int size)
{
	return twiddlings_real(&player_look_yaw_rate[index], access, offset, value, size);
}

static boolean twiddlings_look_pitch_rate(
	enum twiddlings_access access,
	short index,
	unsigned int offset,
	void *value,
	unsigned int size)
{
	return twiddlings_real(&player_look_pitch_rate[index], access, offset, value, size);
}

static void twiddlings_refused(
	enum twiddlings_access access,
	unsigned int address)
{
	char key[48];

	snprintf(key, sizeof(key), "twiddlings %c %08x", access == _twiddlings_read ? 'r' : 'w', address);
	if (access == _twiddlings_read)
		lua_log_once(key, "twiddlings: unknown read at 0x%08x", address);
	else
		lua_log_once(key, "twiddlings: write refused at 0x%08x", address);
}

/* ---------- public code */

boolean twiddlings_read(
	unsigned int address,
	void *value,
	unsigned int size)
{
	void *pointer;

	memset(value, 0, size);
	if (!twiddlings_enabled())
	{
		lua_log_once("twiddlings off", "twiddlings: off (lua.twiddlings), so every read is 0");
		return FALSE;
	}
	if (twiddlings_tag_index_read(address, value, size))
		return TRUE;
	pointer = twiddlings_tag_cache_pointer(address, size);
	if (pointer)
	{
		memcpy(value, pointer, size);
		return TRUE;
	}
	if (twiddlings_access_global(_twiddlings_read, address, value, size))
		return TRUE;
	memset(value, 0, size);
	twiddlings_refused(_twiddlings_read, address);
	return FALSE;
}

boolean twiddlings_write(
	unsigned int address,
	void const *value,
	unsigned int size)
{
	void *pointer;
	byte copy[8];

	if (!twiddlings_enabled())
	{
		lua_log_once("twiddlings off w", "twiddlings: off (lua.twiddlings), so every write is refused");
		return FALSE;
	}
	{
		byte ignored[8];

		/* (the tag index is read alone) */
		if (size <= sizeof(ignored) && twiddlings_tag_index_read(address, ignored, size))
		{
			twiddlings_refused(_twiddlings_write, address);
			return FALSE;
		}
	}
	pointer = twiddlings_tag_cache_pointer(address, size);
	if (pointer)
	{
		memcpy(pointer, value, size);
		return TRUE;
	}
	if (size <= sizeof(copy))
	{
		memcpy(copy, value, size);
		if (twiddlings_access_global(_twiddlings_write, address, copy, size))
			return TRUE;
	}
	twiddlings_refused(_twiddlings_write, address);
	return FALSE;
}

boolean twiddlings_read_string(
	unsigned int address,
	char *text,
	unsigned int size)
{
	unsigned int base, end, length;
	char const *source;

	text[0] = 0;
	if (!size)
		return FALSE;
	if (!twiddlings_enabled() || !twiddlings_tag_cache(&base, &end) || address < base || address >= end)
	{
		twiddlings_refused(_twiddlings_read, address);
		return FALSE;
	}
	source = (char const *)twiddlings_pointer(address);
	for (length = 0; length + 1 < size && address + length < end && source[length]; length++)
		text[length] = source[length];
	text[length] = 0;
	return TRUE;
}

boolean twiddlings_write_string(
	unsigned int address,
	char const *text)
{
	unsigned int length = (unsigned int)strlen(text) + 1;
	void *pointer = twiddlings_enabled() ? twiddlings_tag_cache_pointer(address, length) : NULL;

	if (!pointer)
	{
		twiddlings_refused(_twiddlings_write, address);
		return FALSE;
	}
	memcpy(pointer, text, length);
	return TRUE;
}

unsigned int twiddlings_tag_instance_address(
	long tag_index)
{
	unsigned int base, end, instances, count, absolute_index, instance;

	if (tag_index == NONE || !twiddlings_tag_cache(&base, &end) || !twiddlings_tag_index(&instances, &count))
		return 0;
	absolute_index = (unsigned int)tag_index & 0xFFFF;
	if (absolute_index >= count)
		return 0;
	instance = instances + absolute_index * TAG_INSTANCE_SIZE;
	/* (an index without a salt, as Chimera takes one) */
	if (((unsigned int)tag_index & 0xFFFF0000) &&
		*(unsigned int const *)twiddlings_pointer(instance + TAG_INSTANCE_INDEX_OFFSET) != (unsigned int)tag_index)
	{
		return 0;
	}
	/* (the instance where Halo PC has it: twiddlings_tag_index_read) */
	return base + PC_TAG_INSTANCES_OFFSET + absolute_index * TAG_INSTANCE_SIZE;
}

unsigned int twiddlings_tag_instance_address_by_name(
	unsigned long group_tag,
	char const *path)
{
	unsigned int base, end, instances, count, index;

	if (!twiddlings_tag_cache(&base, &end) || !twiddlings_tag_index(&instances, &count))
		return 0;
	for (index = 0; index < count; index++)
	{
		unsigned int instance = instances + index * TAG_INSTANCE_SIZE;
		unsigned int name = *(unsigned int const *)twiddlings_pointer(instance + TAG_INSTANCE_NAME_OFFSET);
		unsigned int group = *(unsigned int const *)twiddlings_pointer(instance + TAG_INSTANCE_GROUP_OFFSET);
		char text[256];

		if (group != (unsigned int)group_tag || name < base || name >= end)
			continue;
		if (!twiddlings_read_string(name, text, sizeof(text)))
			continue;
		if (!_stricmp(text, path))
			return base + PC_TAG_INSTANCES_OFFSET + index * TAG_INSTANCE_SIZE;
	}
	return 0;
}

#endif
