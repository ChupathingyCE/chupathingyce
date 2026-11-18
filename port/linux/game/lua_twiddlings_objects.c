/*
LUA_TWIDDLINGS_OBJECTS.C

Twiddlings' objects and players (lua_twiddlings.h): what get_object,
get_player and get_dynamic_player give a Chimera script, and its reads and
writes at those addresses, moved from Halo PC's layout of an object or a
player onto this game's (the Xbox's).

A script gets a stand-in address for an object or a player, never one of
this game's: 0xD0000000 plus the object's slot (its index in the object
table) times 0x1000, and 0xD1000000 plus the player's slot times 0x400. Each
read or write there finds what is in that slot now (as a Halo PC pointer to an
object's memory does) and moves the Halo PC offset onto this game's:
  - Halo PC added network state to every object: 0x0C bytes after the
    definition and 0x44 more before the position, so the rest of an object
    sits 0x50 bytes later than the Xbox's; a unit has 0x58 more at its end
    (more network state), its biped or vehicle part after that; a weapon's
    triggers are 4 bytes longer each; items, projectiles, garbage, devices,
    machines, controls and light fixtures are the Xbox's, 0x50 later;
  - the object's blocks (its nodes' orientations and matrices) come after
    its part on both, so they move by the difference of the two sizes, and
    the block offsets an object holds are given as Halo PC's;
  - a player is the Xbox's up to its statistics, which Halo PC has 4 bytes
    later.
A Halo PC field this game does not have (the network state, Halo PC's own
additions) reads 0 and is not written, logged once by object type and offset.

Halo PC's layouts are those of Chimera's halo_data structures (object.hpp,
player.hpp: github.com/SnowyMouse/chimera, by Snowy Mouse, its current
developers Aerocatia and Cambidd, and its contributors) and of OpenSauce's
blamlib object structures (objects, units, items: Kornner Studios), both
GPL-3.0; Halo PC's object sizes are OpenSauce's (object_structures.hpp).

Halo PC's object table, which scripts walk from the address of its pointer
(0x401194 in haloce.exe's code, the operand of a mov from 0x7FB710), is
answered here too: its header is this game's, its elements' object
addresses the stand-ins above.

SPDX-FileCopyrightText: Snowy Mouse and the Chimera contributors
SPDX-FileCopyrightText: Kornner Studios (OpenSauce)
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-only
*/

#ifdef HALO_LUA

#include "cseries.h"
#include "devices/device_controls.h"
#include "devices/device_light_fixtures.h"
#include "devices/devices.h"
#include "game/game.h"
#include "game/players.h"
#include "items/garbage.h"
#include "items/items.h"
#include "items/projectiles.h"
#include "items/weapons.h"
#include "memory/data.h"
#include "objects/object_types.h"
#include "objects/objects.h"
#include "units/bipeds.h"
#include "units/units.h"
#include "units/vehicle_datum.h"

#include "lua_internal.h"
#include "lua_twiddlings.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ---------- constants */

/* a data array's elements (in a 64-bit build its data field is their Xbox
address; in the others, their pointer) */
#ifdef DATA_ARRAY_DATA
#define TWIDDLINGS_ARRAY_DATA(array) DATA_ARRAY_DATA(array)
#else
#define TWIDDLINGS_ARRAY_DATA(array) ((void *)(array)->data)
#endif

/* the stand-in addresses (none of them Halo PC's or the tag cache's) */
#define TWIDDLINGS_OBJECTS_BASE 0xD0000000u
#define TWIDDLINGS_OBJECT_STRIDE 0x1000u
#define TWIDDLINGS_PLAYERS_BASE 0xD1000000u
#define TWIDDLINGS_PLAYER_STRIDE 0x400u
#define TWIDDLINGS_OBJECT_TABLE 0xD2000000u
#define TWIDDLINGS_OBJECT_TABLE_ELEMENTS (TWIDDLINGS_OBJECT_TABLE + 0x40u)

/* Halo PC's object table (haloce.exe 1.0.10): the operand that names its
pointer, and the pointer */
#define PC_OBJECT_TABLE_POINTER_OPERAND 0x00401194u
#define PC_OBJECT_TABLE_POINTER 0x007FB710u
/* its header (a data array, as this game's) and its elements: 0x0C bytes,
the object's address at 0x8 */
#define PC_DATA_ARRAY_HEADER_SIZE 0x38u
#define PC_DATA_ARRAY_DATA_OFFSET 0x34u
#define PC_OBJECT_HEADER_SIZE 0x0Cu
#define PC_OBJECT_HEADER_DATUM_OFFSET 0x08u

/* Halo PC's object part (OpenSauce's k_object_size_object) and its block
references (the nodes' orientations and matrices), each a size and an
offset */
#define PC_OBJECT_SIZE 0x1F4u
#define PC_OBJECT_BLOCK_REFERENCES 0x1E8u
#define PC_OBJECT_BLOCK_REFERENCE_COUNT 3

/* ---------- types */

/* a span of a Halo PC object, for the object types in types, and where it
is in this game's */
struct twiddlings_object_range
{
	unsigned long types;
	unsigned short pc_offset;
	unsigned short offset;
	unsigned short size;
};

/* ---------- globals */

static struct twiddlings_object_range const twiddlings_object_ranges[] =
{
	/* every object: its definition, its flags and magic number, then all
	from its position on */
	{ _object_mask_all, 0x000, offsetof(struct object_datum, definition_index), sizeof(long) },
	{ _object_mask_all, 0x010, offsetof(struct object_datum, object.flags), 2 * sizeof(long) },
	{ _object_mask_all, 0x05C, offsetof(struct object_datum, object.position),
		sizeof(struct object_datum) - offsetof(struct object_datum, object.position) },
	/* units, and the biped's and vehicle's parts after Halo PC's longer unit */
	{ _object_mask_unit, 0x1F4, offsetof(struct biped_datum, unit), sizeof(struct _unit_datum) },
	{ _object_mask_biped, 0x4CC, offsetof(struct biped_datum, biped), 0x5A },
	{ _object_mask_vehicle, 0x4CC, offsetof(struct vehicle_datum, vehicle), sizeof(struct _vehicle_datum) },
	/* items (a projectile is one too) */
	{ _object_mask_item | _object_mask_projectile, 0x1F4, offsetof(struct item_datum, item),
		sizeof(struct _item_datum) },
	/* a weapon: to its first trigger's end, its second trigger, its
	magazines on (Halo PC's triggers are 4 bytes longer) */
	{ _object_mask_weapon, 0x22C, offsetof(struct weapon_datum, weapon),
		offsetof(struct _weapon_datum, triggers) + sizeof(struct weapon_trigger) },
	{ _object_mask_weapon, 0x288, offsetof(struct weapon_datum, weapon.triggers[1]), sizeof(struct weapon_trigger) },
	{ _object_mask_weapon, 0x2B0, offsetof(struct weapon_datum, weapon.magazines),
		sizeof(struct _weapon_datum) - offsetof(struct _weapon_datum, magazines) },
	{ _object_mask_garbage, 0x22C, offsetof(struct garbage_datum, lifetime_ticks), sizeof(short) },
	{ _object_mask_projectile, 0x22C, offsetof(struct projectile_datum, projectile), sizeof(struct _projectile_datum) },
	/* devices */
	{ _object_mask_device, 0x1F4, offsetof(struct device_datum, device), sizeof(struct _device_datum) },
	{ _object_mask_machine, 0x214, offsetof(struct machine_datum, machine), sizeof(struct _machine_datum) },
	{ _object_mask_control, 0x214, offsetof(struct control_datum, control), sizeof(struct _control_datum) },
	{ _object_mask_light_fixture, 0x214, offsetof(struct light_fixture_datum, light_fixture),
		sizeof(struct _light_fixture_datum) },
};

/* (the spans above hold only if this game's parts are the sizes Halo PC's
are, less what Halo PC added) */
typedef char twiddlings_object_position_assert[
	offsetof(struct object_datum, object.position) == 0x0C ? 1 : -1];
typedef char twiddlings_object_size_assert[sizeof(struct object_datum) == 0x1A4 ? 1 : -1];
typedef char twiddlings_unit_size_assert[sizeof(struct _unit_datum) == 0x280 ? 1 : -1];
typedef char twiddlings_vehicle_size_assert[sizeof(struct _vehicle_datum) == 0x58 ? 1 : -1];
typedef char twiddlings_item_size_assert[sizeof(struct _item_datum) == 0x38 ? 1 : -1];
typedef char twiddlings_weapon_trigger_size_assert[sizeof(struct weapon_trigger) == 0x24 ? 1 : -1];
typedef char twiddlings_projectile_size_assert[sizeof(struct _projectile_datum) == 0x4C ? 1 : -1];
typedef char twiddlings_device_size_assert[sizeof(struct _device_datum) == 0x20 ? 1 : -1];
typedef char twiddlings_light_fixture_size_assert[sizeof(struct _light_fixture_datum) == 0x18 ? 1 : -1];

/* Halo PC's object sizes by type (OpenSauce's k_object_size_*): its blocks
start there */
static unsigned short const twiddlings_pc_object_sizes[NUMBER_OF_OBJECT_TYPES] =
{
	0x550, /* biped */
	0x5C0, /* vehicle */
	0x340, /* weapon */
	0x294, /* equipment */
	0x244, /* garbage */
	0x2B0, /* projectile */
	0x1F8, /* scenery */
	0x228, /* machine */
	0x21C, /* control */
	0x22C, /* light fixture */
	0x1FC, /* placeholder */
	0x1F8, /* sound scenery */
};

static char const *const twiddlings_object_type_names[NUMBER_OF_OBJECT_TYPES] =
{
	"biped", "vehicle", "weapon", "equipment", "garbage", "projectile", "scenery", "machine", "control",
	"light fixture", "placeholder", "sound scenery",
};

/* a Halo PC player: the Xbox's to its statistics, which are 4 bytes later */
static struct twiddlings_object_range const twiddlings_player_ranges[] =
{
	{ 0, 0x00, 0, offsetof(struct player_datum, statistics) },
	{ 0, 0x90, offsetof(struct player_datum, statistics),
		offsetof(struct player_datum, is_blocking_teleporter) - offsetof(struct player_datum, statistics) },
};

typedef char twiddlings_player_statistics_assert[
	offsetof(struct player_datum, statistics) == 0x8C ? 1 : -1];

/* ---------- private code */

/* the element at a data array's absolute index, if one is there */
static byte *twiddlings_datum(
	struct data_array *array,
	unsigned int absolute_index,
	long *handle)
{
	byte *element;
	short identifier;

	if (!array || !array->valid || absolute_index >= (unsigned int)array->maximum_count)
		return NULL;
	element = (byte *)TWIDDLINGS_ARRAY_DATA(array) + absolute_index * (unsigned int)array->size;
	memcpy(&identifier, element, sizeof(identifier));
	if (!identifier)
		return NULL;
	if (handle)
		*handle = (long)(((unsigned long)(unsigned short)identifier << 16) | absolute_index);
	return element;
}

static struct object_datum *twiddlings_object_at(
	unsigned int absolute_index)
{
	long handle;

	if (!game_time_initialized() || !twiddlings_datum(object_header_data, absolute_index, &handle))
		return NULL;
	return (struct object_datum *)object_try_and_get(handle);
}

static void twiddlings_object_refused(
	boolean write,
	char const *what,
	unsigned int offset)
{
	char key[64];

	snprintf(key, sizeof(key), "twiddlings %c %s %04x", write ? 'w' : 'r', what, offset);
	if (write)
		lua_log_once(key, "twiddlings: write refused at a Halo PC %s's +0x%x", what, offset);
	else
		lua_log_once(key, "twiddlings: unknown read at a Halo PC %s's +0x%x", what, offset);
}

static struct twiddlings_object_range const *twiddlings_range_at(
	struct twiddlings_object_range const *ranges,
	short count,
	unsigned long type_flag,
	unsigned int pc_offset,
	unsigned int size)
{
	short index;

	for (index = 0; index < count; index++)
	{
		struct twiddlings_object_range const *range = &ranges[index];

		if (type_flag && !(range->types & type_flag))
			continue;
		if (pc_offset >= range->pc_offset && pc_offset + size <= (unsigned int)range->pc_offset + range->size)
			return range;
	}
	return NULL;
}

/* a read or write at a Halo PC offset in the object in a slot */
static boolean twiddlings_object_access(
	boolean write,
	unsigned int absolute_index,
	unsigned int pc_offset,
	void *value,
	unsigned int size)
{
	struct object_datum *object = twiddlings_object_at(absolute_index);
	struct object_header_datum const *header;
	struct twiddlings_object_range const *range;
	short type;
	unsigned int pc_size, size_here, offset;
	long handle;

	if (!object)
	{
		/* (no object there now: as a freed object's memory, nothing) */
		if (write)
			return FALSE;
		memset(value, 0, size);
		return TRUE;
	}
	type = object->object.type;
	if (type < 0 || type >= NUMBER_OF_OBJECT_TYPES)
		return FALSE;
	range = twiddlings_range_at(twiddlings_object_ranges, (short)NUMBEROF(twiddlings_object_ranges), FLAG(type),
		pc_offset, size);
	pc_size = twiddlings_pc_object_sizes[type];
	size_here = (unsigned int)object_type_definition_get(type)->game_datum_size;
	if (range)
	{
		offset = range->offset + (pc_offset - range->pc_offset);
	}
	else if (pc_offset >= pc_size)
	{
		/* its blocks: after its part on both */
		twiddlings_datum(object_header_data, absolute_index, &handle);
		header = (struct object_header_datum const *)datum_get(object_header_data, handle);
		offset = pc_offset - pc_size + size_here;
		if (offset + size > (unsigned int)(unsigned short)header->data_size)
		{
			twiddlings_object_refused(write, twiddlings_object_type_names[type], pc_offset);
			return FALSE;
		}
	}
	else
	{
		twiddlings_object_refused(write, twiddlings_object_type_names[type], pc_offset);
		return FALSE;
	}
	/* the block references' offsets, as Halo PC's */
	if (pc_offset < PC_OBJECT_BLOCK_REFERENCES + PC_OBJECT_BLOCK_REFERENCE_COUNT * 4 &&
		pc_offset + size > PC_OBJECT_BLOCK_REFERENCES)
	{
		byte bytes[PC_OBJECT_BLOCK_REFERENCE_COUNT * 4];
		unsigned int start = PC_OBJECT_BLOCK_REFERENCES;
		short reference;

		if (write || pc_offset < start || pc_offset + size > start + sizeof(bytes))
		{
			twiddlings_object_refused(write, twiddlings_object_type_names[type], pc_offset);
			return FALSE;
		}
		memcpy(bytes, (byte const *)object + (offset - (pc_offset - start)), sizeof(bytes));
		for (reference = 0; reference < PC_OBJECT_BLOCK_REFERENCE_COUNT; reference++)
		{
			short block_offset;

			memcpy(&block_offset, bytes + reference * 4 + 2, sizeof(block_offset));
			if (block_offset >= (short)size_here)
				block_offset = (short)(block_offset - (short)size_here + (short)pc_size);
			memcpy(bytes + reference * 4 + 2, &block_offset, sizeof(block_offset));
		}
		memcpy(value, bytes + (pc_offset - start), size);
		return TRUE;
	}
	if (write)
	{
		struct object_header_block_reference const *nodes = &object->object.node_matrices;

		memcpy((byte *)object + offset, value, size);
		/* (a script that poses an object, as SPV3's first-person legs: the
		frame draws the pose it set, not the ticks': lua_scripting.h) */
		if (nodes->size > 0 && offset >= (unsigned int)(unsigned short)nodes->offset &&
			offset < (unsigned int)(unsigned short)nodes->offset + (unsigned int)(unsigned short)nodes->size)
		{
			twiddlings_datum(object_header_data, absolute_index, &handle);
			lua_scripting_note_posed(handle);
		}
	}
	else
	{
		memcpy(value, (byte const *)object + offset, size);
	}
	return TRUE;
}

static boolean twiddlings_player_access(
	boolean write,
	unsigned int absolute_index,
	unsigned int pc_offset,
	void *value,
	unsigned int size)
{
	byte *player = game_time_initialized() ? twiddlings_datum(player_data, absolute_index, NULL) : NULL;
	struct twiddlings_object_range const *range;

	if (!player)
	{
		/* (no player there yet: nothing, its object and the like none) */
		unsigned int offset;

		if (write)
			return FALSE;
		memset(value, 0, size);
		for (offset = pc_offset; offset < pc_offset + size; offset++)
		{
			if (offset >= offsetof(struct player_datum, action_object_index) &&
				offset < offsetof(struct player_datum, cluster_index))
			{
				((byte *)value)[offset - pc_offset] = 0xFF;
			}
		}
		return TRUE;
	}
	range = twiddlings_range_at(twiddlings_player_ranges, (short)NUMBEROF(twiddlings_player_ranges), 0, pc_offset,
		size);
	if (!range)
	{
		twiddlings_object_refused(write, "player", pc_offset);
		return FALSE;
	}
	player += range->offset + (pc_offset - range->pc_offset);
	if (write)
		memcpy(player, value, size);
	else
		memcpy(value, player, size);
	return TRUE;
}

/* Halo PC's object table: its header, then its elements */
static boolean twiddlings_object_table_read(
	unsigned int address,
	void *value,
	unsigned int size)
{
	struct data_array *array = object_header_data;
	byte bytes[PC_OBJECT_HEADER_SIZE > PC_DATA_ARRAY_HEADER_SIZE ? PC_OBJECT_HEADER_SIZE : PC_DATA_ARRAY_HEADER_SIZE];
	unsigned int base, extent, index;

	if (!array || !game_time_initialized())
		return FALSE;
	if (address >= TWIDDLINGS_OBJECT_TABLE && address + size <= TWIDDLINGS_OBJECT_TABLE + PC_DATA_ARRAY_HEADER_SIZE)
	{
		unsigned int elements = TWIDDLINGS_OBJECT_TABLE_ELEMENTS;

		memcpy(bytes, array, PC_DATA_ARRAY_HEADER_SIZE);
		memcpy(bytes + PC_DATA_ARRAY_DATA_OFFSET, &elements, sizeof(elements));
		memcpy(value, bytes + (address - TWIDDLINGS_OBJECT_TABLE), size);
		return TRUE;
	}
	extent = (unsigned int)array->maximum_count * PC_OBJECT_HEADER_SIZE;
	if (address < TWIDDLINGS_OBJECT_TABLE_ELEMENTS || address + size > TWIDDLINGS_OBJECT_TABLE_ELEMENTS + extent)
		return FALSE;
	index = (address - TWIDDLINGS_OBJECT_TABLE_ELEMENTS) / PC_OBJECT_HEADER_SIZE;
	base = TWIDDLINGS_OBJECT_TABLE_ELEMENTS + index * PC_OBJECT_HEADER_SIZE;
	if (address + size > base + PC_OBJECT_HEADER_SIZE)
		return FALSE;
	memset(bytes, 0, sizeof(bytes));
	{
		byte const *element = (byte const *)TWIDDLINGS_ARRAY_DATA(array) + index * (unsigned int)array->size;
		unsigned int object = twiddlings_object_address((long)index);

		/* (its identifier, flags, type, cluster and size are this game's) */
		memcpy(bytes, element, PC_OBJECT_HEADER_DATUM_OFFSET);
		memcpy(bytes + PC_OBJECT_HEADER_DATUM_OFFSET, &object, sizeof(object));
	}
	memcpy(value, bytes + (address - base), size);
	return TRUE;
}

/* ---------- public code */

unsigned int twiddlings_object_address(
	long object_index)
{
	unsigned int absolute_index = (unsigned int)object_index & 0xFFFF;
	long handle;

	if (object_index == NONE || !game_time_initialized() ||
		!twiddlings_datum(object_header_data, absolute_index, &handle))
	{
		return 0;
	}
	/* (an index alone, as Chimera takes one, or a handle whose salt is the
	object's now) */
	if (((unsigned long)object_index & 0xFFFF0000) && handle != object_index)
		return 0;
	if (!object_try_and_get(handle))
		return 0;
	return TWIDDLINGS_OBJECTS_BASE + absolute_index * TWIDDLINGS_OBJECT_STRIDE;
}

unsigned int twiddlings_player_address(
	long player_index)
{
	unsigned int absolute_index = (unsigned int)player_index & 0xFFFF;
	long handle;

	if (player_index == NONE || !game_time_initialized() || !twiddlings_datum(player_data, absolute_index, &handle))
		return 0;
	if (((unsigned long)player_index & 0xFFFF0000) && handle != player_index)
		return 0;
	return TWIDDLINGS_PLAYERS_BASE + absolute_index * TWIDDLINGS_PLAYER_STRIDE;
}

unsigned int twiddlings_player_slot_address(
	long list_index)
{
	long player_index = twiddlings_player_by_list_index(list_index);
	unsigned int count = player_data ? (unsigned int)player_data->maximum_count : 0;

	if (player_index != NONE)
		return twiddlings_player_address(player_index);
	if (list_index < 0 || (unsigned int)list_index >= count)
		return 0;
	return TWIDDLINGS_PLAYERS_BASE + (unsigned int)list_index * TWIDDLINGS_PLAYER_STRIDE;
}

long twiddlings_player_by_list_index(
	long list_index)
{
	unsigned int absolute_index;

	if (!game_time_initialized() || !player_data || list_index < 0)
		return NONE;
	for (absolute_index = 0; absolute_index < (unsigned int)player_data->maximum_count; absolute_index++)
	{
		long handle;
		struct player_datum const *player =
			(struct player_datum const *)twiddlings_datum(player_data, absolute_index, &handle);

		if (player && player->network_player_data.player_list_index == list_index)
			return handle;
	}
	return NONE;
}

int twiddlings_objects_access(
	boolean write,
	unsigned int address,
	void *value,
	unsigned int size)
{
	if (address >= TWIDDLINGS_OBJECTS_BASE && address < TWIDDLINGS_PLAYERS_BASE)
	{
		unsigned int relative = address - TWIDDLINGS_OBJECTS_BASE;

		if (relative % TWIDDLINGS_OBJECT_STRIDE + size > TWIDDLINGS_OBJECT_STRIDE)
			return FALSE;
		return twiddlings_object_access(write, relative / TWIDDLINGS_OBJECT_STRIDE,
			relative % TWIDDLINGS_OBJECT_STRIDE, value, size);
	}
	if (address >= TWIDDLINGS_PLAYERS_BASE && address < TWIDDLINGS_OBJECT_TABLE)
	{
		unsigned int relative = address - TWIDDLINGS_PLAYERS_BASE;

		if (relative % TWIDDLINGS_PLAYER_STRIDE + size > TWIDDLINGS_PLAYER_STRIDE)
			return FALSE;
		return twiddlings_player_access(write, relative / TWIDDLINGS_PLAYER_STRIDE,
			relative % TWIDDLINGS_PLAYER_STRIDE, value, size);
	}
	if (address >= TWIDDLINGS_OBJECT_TABLE && address < TWIDDLINGS_OBJECT_TABLE + 0x01000000u)
	{
		if (write)
			return FALSE;
		return twiddlings_object_table_read(address, value, size);
	}
	if (!write && size == sizeof(unsigned int) &&
		(address == PC_OBJECT_TABLE_POINTER_OPERAND || address == PC_OBJECT_TABLE_POINTER))
	{
		unsigned int pointer = address == PC_OBJECT_TABLE_POINTER_OPERAND ? PC_OBJECT_TABLE_POINTER :
			TWIDDLINGS_OBJECT_TABLE;

		memcpy(value, &pointer, sizeof(pointer));
		return TRUE;
	}
	return -1;
}

#endif
