/*
OPENSAUCE_MAP.C

OpenSauce's maps, in a module of their own (opensauce.h). OpenSauce (by
Kornner Studios) builds Custom Edition caches with a header of its own in
the stock header's padding at 0x70, and tags of its own groups; this is what
the game makes of them, and only for a map that has the header:

- the header is read as the map loads (opensauce_map_opened);
- its tags are checked against OpenSauce's layout rather than Halo PC's
  (tag_schema_opensauce.c, cache_files.c), so OpenSauce's groups are walked
  and checked like any other, not passed over;
- its project_yellow tag (the scenario's first reference, which OpenSauce
  takes from the scenario's unused one) and project_yellow_globals are read:
  the gravity scale is honored, as OpenSauce honors it; the rest is logged,
  once a map, with what is and is not supported yet.

Whether a map's header may be read at all (its memory upgrades need the
larger tag window) is still SPV3 mode's call (spv3_mode.c). A map without
the header is never touched here.

Formats from OpenSauce's source (GPL-3.0-or-later), read as facts:
cache_files_structures_yelo.hpp, project_yellow_scenario_definitions.hpp,
project_yellow_global_definitions.hpp, and GameState.cpp for what of
project_yellow it honors at run time (the gravity scale, when it is above
0 and not 1). No code of it is here.
*/

#ifdef HALO_CUSTOM_EDITION

#include "cseries.h"
#include "errors.h"
#include "opensauce.h"
#include "spv3_mode.h"
#include "opensauce_scripts.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	OPENSAUCE_TAG_BASE = 0x40440000,
	OPENSAUCE_STOCK_TAG_CACHE_SIZE = 0x01700000,
	OPENSAUCE_VERSION_OFFSET = 0x04,
	OPENSAUCE_FLAGS_OFFSET = 0x06,
	OPENSAUCE_TAG_VERSIONS_OFFSET = 0x08,
	OPENSAUCE_UPGRADE_AMOUNT_OFFSET = 0x0C,
	OPENSAUCE_MOD_NAME_OFFSET = 0x40,

	/* project_yellow's and project_yellow_globals' fields read here
	(tag_schema_opensauce.c's structures, checked by then) */
	YELO_VERSION_OFFSET = 0x00,
	YELO_GLOBALS_OFFSET = 0x04,
	YELO_GAME_GLOBALS_OFFSET = 0x14,
	YELO_SCRIPTED_WIDGETS_OFFSET = 0x68,
	YELO_GRAVITY_SCALE_OFFSET = 0x84,
	YELO_PLAYER_SPEED_SCALE_OFFSET = 0x88,
	YELO_GAMEPLAY_FLAGS_OFFSET = 0xB8,
	YELO_SIZE = 0x138,
	GELO_FLAGS_OFFSET = 0x02,
	GELO_SCRIPTED_WIDGETS_OFFSET = 0x98,
	GELO_PLAYER_UNITS_OFFSET = 0xA4,
	GELO_SCRIPTING_OFFSET = 0xC4,
	GELO_SIZE = 0x120,
	SCRIPTING_FUNCTIONS_OFFSET = 0x00,
	SCRIPTING_GLOBALS_OFFSET = 0x0C,
};

/* the game's gravity (physics.c), and OpenSauce's range for its scale */
#define OPENSAUCE_GRAVITY_SCALE_MAXIMUM 2.0f

/* ---------- structures */

/* a Custom Edition tag instance (cache_files.c's) */
struct opensauce_tag_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	unsigned long tag_index;
	unsigned long name;
	unsigned long address;
	unsigned long indexed;
	unsigned long pad;
};

typedef char verify_opensauce_tag_instance_size[sizeof(struct opensauce_tag_instance) == 0x20 ? 1 : -1];

/* ---------- globals */

extern real global_gravity;

static struct
{
	boolean active;
	struct opensauce_header header;
	char map_name[64];
	/* the gravity as it was, put back when the map unloads */
	boolean gravity_changed;
	real gravity_before;
	/* (camouflage past 1 said once a map) */
	boolean camouflage_logged;
} opensauce_globals;

static char const *const opensauce_flag_names[NUMBER_OF_OPENSAUCE_HEADER_FLAGS] =
{
	"uses_memory_upgrades",
	"uses_mod_data_files",
	"is_protected",
	"uses_game_state_upgrades",
	"has_compression_params",
};

/* ---------- private code */

static unsigned long opensauce_read32(
	byte const *data,
	long offset)
{
	unsigned long value;

	memcpy(&value, data + offset, sizeof(value));
	return value;
}

static void opensauce_log(
	char const *format,
	...)
{
	char text[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	error(_error_silent, "OpenSauce map %s: %s", opensauce_globals.map_name, text);
}

/* the instance of a tag handle, if it is one of a tag of group whose root
is size bytes in the tags */
static struct opensauce_tag_instance const *opensauce_tag_get(
	struct opensauce_tag_instance const *instances,
	long tag_count,
	unsigned long tag_index,
	unsigned long group_tag,
	unsigned long size)
{
	unsigned long index = tag_index & 0xFFFF;
	struct opensauce_tag_instance const *instance;

	if (tag_index == 0xFFFFFFFFUL || index >= (unsigned long)tag_count)
		return NULL;
	instance = &instances[index];
	/* (within the tag cache in use: Custom Edition's, or OpenSauce's where
	SPV3 mode mapped it, spv3_mode.c; a build that checks no tags reads none
	outside it) */
	if (instance->tag_index != tag_index || instance->group_tag != group_tag ||
		instance->address < OPENSAUCE_TAG_BASE ||
		instance->address - OPENSAUCE_TAG_BASE > spv3_mode_tag_cache_size(OPENSAUCE_STOCK_TAG_CACHE_SIZE) - size)
	{
		return NULL;
	}
	return instance;
}

/* a tag reference's handle (struct tag_reference: its index at 0x0C) */
static unsigned long opensauce_reference_index(
	byte const *root,
	long offset)
{
	return opensauce_read32(root, offset + 0x0C);
}

/* a tag block's count (struct tag_block: its count first) */
static long opensauce_block_count(
	byte const *root,
	long offset)
{
	return (long)opensauce_read32(root, offset);
}

static void opensauce_gravity_restore(
	void)
{
	if (opensauce_globals.gravity_changed)
	{
		global_gravity = opensauce_globals.gravity_before;
		opensauce_globals.gravity_changed = FALSE;
	}
}

/* the game's own gravity (physics.c's), whatever a map set it to */
#define OPENSAUCE_STOCK_GRAVITY 0.0035651792f

/* ---------- public code */

boolean opensauce_header_read(
	void const *cache_header,
	struct opensauce_header *header)
{
	byte const *yelo = (byte const *)cache_header + OPENSAUCE_HEADER_OFFSET;
	unsigned short flags;

	if (opensauce_read32(yelo, 0) != (unsigned long)OPENSAUCE_HEADER_SIGNATURE)
		return FALSE;
	if (header)
	{
		memset(header, 0, sizeof(*header));
		memcpy(&header->version, yelo + OPENSAUCE_VERSION_OFFSET, sizeof(header->version));
		memcpy(&flags, yelo + OPENSAUCE_FLAGS_OFFSET, sizeof(flags));
		header->flags = flags;
		header->project_yellow_version = yelo[OPENSAUCE_TAG_VERSIONS_OFFSET];
		header->project_yellow_globals_version = yelo[OPENSAUCE_TAG_VERSIONS_OFFSET + 1];
		memcpy(&header->memory_upgrade_amount, yelo + OPENSAUCE_UPGRADE_AMOUNT_OFFSET,
			sizeof(header->memory_upgrade_amount));
		memcpy(header->mod_name, yelo + OPENSAUCE_MOD_NAME_OFFSET, sizeof(header->mod_name) - 1);
		header->mod_name[sizeof(header->mod_name) - 1] = 0;
	}
	return TRUE;
}

char const *opensauce_header_flag_name(
	short bit)
{
	return bit >= 0 && bit < NUMBER_OF_OPENSAUCE_HEADER_FLAGS ? opensauce_flag_names[bit] : "(unknown)";
}

void opensauce_map_opened(
	void const *cache_header,
	char const *map_name)
{
	opensauce_gravity_restore();
	opensauce_globals.active = opensauce_header_read(cache_header, &opensauce_globals.header);
	snprintf(opensauce_globals.map_name, sizeof(opensauce_globals.map_name), "%s", map_name ? map_name : "");
	if (opensauce_globals.active)
	{
		opensauce_log("OpenSauce header v%d, flags 0x%04x, memory upgrade %.2f, tag versions %u/%u, mod name '%s': "
			"its tags are checked against OpenSauce's layout", (int)opensauce_globals.header.version,
			(unsigned int)opensauce_globals.header.flags, (double)opensauce_globals.header.memory_upgrade_amount,
			(unsigned int)opensauce_globals.header.project_yellow_version,
			(unsigned int)opensauce_globals.header.project_yellow_globals_version, opensauce_globals.header.mod_name);
	}
}

boolean opensauce_map_active(
	void)
{
	return opensauce_globals.active;
}

struct opensauce_header const *opensauce_map_header(
	void)
{
	return opensauce_globals.active ? &opensauce_globals.header : NULL;
}

void opensauce_map_tags_loaded(
	void *tag_instances,
	long tag_count,
	unsigned long scenario_tag_index)
{
	struct opensauce_tag_instance const *instances = tag_instances;
	struct opensauce_tag_instance const *scenario, *yelo, *gelo;
	byte const *yelo_root, *gelo_root;
	real gravity_scale, player_speed_scale;

	if (!opensauce_globals.active || !instances || tag_count <= 0)
		return;
	/* (the scenario's first reference, its unused structure bsp's, is
	OpenSauce's project_yellow: not in the scenario's schema, so checked
	here) */
	scenario = opensauce_tag_get(instances, tag_count, scenario_tag_index, 'scnr', 0x10);
	yelo = scenario ? opensauce_tag_get(instances, tag_count,
		opensauce_reference_index(xbox_pointer(scenario->address), 0), 'yelo', YELO_SIZE) : NULL;
	if (!yelo)
	{
		opensauce_log("no project_yellow tag: nothing of OpenSauce's settings to read");
		return;
	}
	yelo_root = xbox_pointer(yelo->address);
	memcpy(&gravity_scale, yelo_root + YELO_GRAVITY_SCALE_OFFSET, sizeof(gravity_scale));
	memcpy(&player_speed_scale, yelo_root + YELO_PLAYER_SPEED_SCALE_OFFSET, sizeof(player_speed_scale));
	opensauce_log("project_yellow v%d: %ld scripted widgets (not shown yet), gameplay flags 0x%lx, "
		"game globals override %s (built into the map by OpenSauce's tools), player speed scale %.3f%s",
		(int)*(short const *)(yelo_root + YELO_VERSION_OFFSET),
		opensauce_block_count(yelo_root, YELO_SCRIPTED_WIDGETS_OFFSET),
		opensauce_read32(yelo_root, YELO_GAMEPLAY_FLAGS_OFFSET),
		opensauce_reference_index(yelo_root, YELO_GAME_GLOBALS_OFFSET) != 0xFFFFFFFFUL ? "set" : "none",
		(double)player_speed_scale, player_speed_scale != 1.0f ? " (not honored: OpenSauce reads none at run time)" : "");

	/* the gravity scale, as OpenSauce honors it: above 0 and not 1, up to
	its tag's 2 */
	if (gravity_scale > 0.0f && gravity_scale != 1.0f && gravity_scale <= OPENSAUCE_GRAVITY_SCALE_MAXIMUM)
	{
		opensauce_globals.gravity_before = global_gravity;
		opensauce_globals.gravity_changed = TRUE;
		global_gravity *= gravity_scale;
		opensauce_log("gravity scaled by %.3f, as its project_yellow asks", (double)gravity_scale);
	}
	else if (gravity_scale != 1.0f && gravity_scale != 0.0f)
		opensauce_log("gravity scale %.3f is outside OpenSauce's (0, 2]: gravity left as it is", (double)gravity_scale);

	gelo = opensauce_tag_get(instances, tag_count, opensauce_reference_index(yelo_root, YELO_GLOBALS_OFFSET), 'gelo',
		GELO_SIZE);
	if (gelo)
	{
		long scripting_count;

		gelo_root = xbox_pointer(gelo->address);
		scripting_count = opensauce_block_count(gelo_root, GELO_SCRIPTING_OFFSET);
		opensauce_log("project_yellow_globals: flags 0x%04x (not honored yet), %ld scripted widgets, "
			"%ld multiplayer player units (not used), %s OpenSauce script definitions (scripts resolve by name)",
			(unsigned int)*(word const *)(gelo_root + GELO_FLAGS_OFFSET),
			opensauce_block_count(gelo_root, GELO_SCRIPTED_WIDGETS_OFFSET),
			opensauce_block_count(gelo_root, GELO_PLAYER_UNITS_OFFSET),
			scripting_count > 0 ? "has" : "no");
	}
}

void opensauce_map_set_gravity_scale(
	float scale)
{
	if (!opensauce_globals.active)
		return;
	if (!opensauce_globals.gravity_changed)
	{
		opensauce_globals.gravity_before = global_gravity;
		opensauce_globals.gravity_changed = TRUE;
	}
	global_gravity = OPENSAUCE_STOCK_GRAVITY * scale;
	opensauce_log("gravity scaled by %.3f by its scripts (physics_set_gravity)", (double)scale);
}

float opensauce_map_drawn_camouflage(
	float amount)
{
	if (!opensauce_globals.active || (amount >= 0.0f && amount <= 1.0f))
		return amount;
	if (!opensauce_globals.camouflage_logged)
	{
		opensauce_globals.camouflage_logged = TRUE;
		opensauce_log("a unit's active camouflage is %.2f, past 1 (its scripts set it so): drawn fully on, the unit "
			"keeping its value as it fades", (double)amount);
	}
	return amount < 0.0f ? 0.0f : 1.0f;
}

void opensauce_map_unloaded(
	void)
{
	opensauce_gravity_restore();
	if (opensauce_globals.active)
		opensauce_scripts_map_unloaded();
	opensauce_globals.active = FALSE;
	opensauce_globals.camouflage_logged = FALSE;
}

#endif
