/*
H2_SCRIPTS.C

port: Halo 2's scenario scripts on this engine's script interpreter (a
Halo 2 level, h2_level.c). See h2_scripts.h for the two halves.

Halo 2's compiled scripts are laid out as this engine's (both Bungie's):
a data array of 20-byte syntax nodes - salt, a constant's type / a
function's index / a script's index, the value type, flags (bit 0 a
constant, 1 a script's call, 2 a global, 3 kept), the next node, the
offset of its text in the string data, its value (a call's: its first
node, the function's name) - and scripts (0x28: name, type, return type,
root node) and globals (0x28: name, type, its value's node). The string
data keeps every function's name and every constant's text, so a call is
given a function here by its name, whatever Halo 2's own index for it
(MCC's table has functions the Xbox's has not).

The value types are Halo 2's 62 (the H2EK's hs_doc.txt names them in each
function's signature; every call of the campaign's 15 levels has the
argument types the signatures give), renumbered onto this engine's 49:
the shared ones as this engine numbers them, string ids and seat mappings
as strings (a constant's text is its name: a marker's, a seat's, an
animation's - what this engine's functions take as a string), point
references, styles and the bitmap / shader / model / BSP tag types as long
integers, AI command scripts, behaviours, orders, structure BSPs,
model states and network events as short integers.

Constants keep Halo 2's values: indices into the scenario's blocks
(trigger volumes, cutscene flags and camera points, object names, device
groups - written here, or by h2_objanim.c, in Halo 2's order), Halo 2's
tag handles, string ids, AI references, BSP indices. A function of this
engine's is given only calls whose values it can take: none that takes
an AI, a tag, a team or another of the kinds above (its values are Halo
2's), and only with the arguments and types it is defined with.

Upstream behaviour: halo2-upstream-work's hs_evaluation.cpp /
hs_scheduler.cpp (the runtime is the same design as this engine's); the
functions' semantics from their documentation (hs_doc.txt, H2EK, private)
and the levels' script sources (H2EK, private) read for what each call
is used for. No Bungie text is kept here.
*/

/* ---------- headers */

#include "cseries.h"
#include "h2_scripts.h"
#include "game/cheats.h"
#include "units/bipeds.h"
#include "devices/device_definitions.h"
#include "h2_bloom.h"
#include "h2_cache.h"
#include "h2_render_only.h"
#include "h2_objanim.h"
#include "h2_campaign.h"
#include "h2_ai.h"
#include "h2_menu.h"
#include "h2_skulls.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/game_engine.h"
#include "hs/hs.h"
#include "cutscene/cinematics.h"
#include "hs/hs_scenario_definitions.h"
#include "hs/object_lists.h"
#include "math/real_math.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "objects/damage.h"
#include "game/game_globals.h"
#include "objects/damage_effect_definitions.h"
#include "h2_damage.h"
#include "cache/cache_files.h"
#include "items/weapons.h"
#include "scenario/scenario.h"
#include "objects/object_types.h"
#include "game/player_control.h"
#include "sound/game_sound.h"
#include "sound/sound_manager.h"
#include "devices/devices.h"
#include "devices/device_machines.h"
#include "physics/physics.h"
#include "interface/interface.h"
#include "interface/hud_definitions.h"
#include "game/players.h"
#include "units/units.h"
#include "units/vehicle_scripting.h"
#include "sound/sound_definitions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the C library's allocator: h2_level.c's reasons) */
#undef malloc
#undef free
#undef realloc
#undef calloc
/* host paths, not the game's Xbox paths (port/linux/include/stdio.h) */
#undef fopen

#define H2_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);
boolean h2_rules_level(void);
void network_coop_names_log(char const *who);
long game_time_get(void);
void hud_activate_team_nav_point_with_object(short nav_index, short team_index, long object_index, float vertical_offset);
void hud_activate_team_nav_point_with_flag(short nav_index, short team_index, short flag_index, float vertical_offset);
void hud_deactivate_team_nav_point_with_object(short team_index, long object_index);
void hud_deactivate_team_nav_point_with_flag(short team_index, short flag_index);
long player_index_from_unit_index(long unit_index);
void device_set_power(long device_index, real power);
unsigned long long vita_host_time_us(void);
boolean scripted_show_hud(boolean show);
void director_script_camera(boolean scripted);
boolean device_set_desired_position(long device_index, real position);
void scripted_camera_set(short camera_point_index, word transition_time, long relative_object_index);
short scripted_camera_time(void);
void hs_effect_new(long effect_definition_index, short cutscene_flag_index);
void hs_effect_new_from_object_marker(long effect_definition_index, long object_index, char const *marker_name);
short vehicle_scripting_unload(long vehicle_index, char const *seat_name);
short vehicle_scripting_load_magic(long vehicle_index, char const *seat_name, long object_list_index);
void unit_kill(long unit_index);
#include "h2_sound.h"
#include "h2_dialogue.h"
#include "h2_level.h"
#ifdef HALO_LINUX
#include "render_epoch.h"
#endif
boolean player_control_action_test_look_relative_up(void);
boolean player_control_action_test_look_relative_down(void);

/* (hs_runtime.c, port) */
long *hs_runtime_arguments_evaluate_all(long thread_index, short maximum, short *count, boolean initialize);
short hs_runtime_call_type(long thread_index);
long hs_runtime_type_default(short type);
void hs_runtime_sleep_forever(long thread_index, short script_index);
void hs_runtime_frame_rewind(long thread_index);
boolean hs_macro_function_parse(short function_index, long expression_index);

/* ---------- constants */

enum
{
	/* Halo 2's scenario (MCC's cache layout: 8-byte blocks, 4-byte string
	ids) */
	H2_SCENARIO_OBJECT_NAMES = 0x48,          /* 0x24: name[32], type, placement */
	H2_SCENARIO_TRIGGER_VOLUMES = 0x108,      /* 0x44: name (string id), object name, node name, forward 0xC, up 0x18, position 0x24, extents 0x30 */
	H2_SCENARIO_SCRIPT_STRINGS = 0x1B0,       /* data: size, address */
	H2_SCENARIO_SCRIPTS = 0x1B8,              /* 0x28 */
	H2_SCENARIO_GLOBALS = 0x1C0,              /* 0x28 */
	H2_SCENARIO_CUTSCENE_FLAGS = 0x1E0,       /* 0x38: pad, name[32], position, facing (2) */
	H2_SCENARIO_CAMERA_POINTS = 0x1E8,        /* 0x40: flags, type, name[32], position 0x24, orientation 0x30 */
	H2_SCENARIO_SYNTAX = 0x238,               /* 20 */

	H2_OBJECT_NAME_SIZE = 0x24,
	H2_TRIGGER_VOLUME_SIZE = 0x44,
	H2_SCRIPT_SIZE = 0x28,
	H2_GLOBAL_SIZE = 0x28,
	H2_CUTSCENE_FLAG_SIZE = 0x38,
	H2_CAMERA_POINT_SIZE = 0x40,
	H2_NODE_SIZE = 20,

	/* this engine's scenario (scenario_definitions.h; checked below) */
	CE_SCENARIO_OBJECT_NAMES = 0x204,
	CE_SCENARIO_TRIGGER_VOLUMES = 0x360,
	CE_SCENARIO_SYNTAX = 0x474,
	CE_SCENARIO_STRINGS = 0x488,
	CE_SCENARIO_SCRIPTS = 0x49C,
	CE_SCENARIO_GLOBALS = 0x4A8,
	CE_SCENARIO_CUTSCENE_FLAGS = 0x4E4,
	CE_SCENARIO_CAMERA_POINTS = 0x4F0,

	CE_OBJECT_NAME_SIZE = 0x24,
	CE_TRIGGER_VOLUME_SIZE = 0x60,
	CE_SCRIPT_SIZE = 0x5C,
	CE_GLOBAL_SIZE = 0x5C,
	CE_CUTSCENE_FLAG_SIZE = 0x5C,
	CE_CAMERA_POINT_SIZE = 0x68,
	CE_DATA_ARRAY_HEADER = 0x38,
	CE_SHIPPED_NODES = 19001,
	/* the console's expressions are compiled into the strings' last 0x400
	bytes (hs_compile) */
	CE_STRINGS_CONSOLE_ROOM = 0x400,

	/* Halo 2's script types: startup, dormant, continuous, static, stub,
	command script (an AI's: this engine's static, run by nothing) */
	H2_SCRIPT_COMMAND_SCRIPT = 5,
	NUMBER_OF_H2_TYPES = 62,

	/* syntax node flags */
	NODE_PRIMITIVE = 1,
	NODE_SCRIPT = 2,
	NODE_GLOBAL = 4,
	NODE_PERMANENT = 8,
	EXTERNAL_GLOBAL = 0x8000,

	/* how many arguments the functions here keep */
	MAXIMUM_ARGUMENTS = 8,
	/* the counts logged this often (ticks) while they change */
	REPORT_TICKS = 30 * 60,
};

/* what a function given to calls is */
enum
{
	_kind_engine = 0,      /* this engine's own */
	_kind_halo2,           /* one of this file's */
	_kind_no_op,           /* nothing to do here (prediction, lighting, data mining) */
	_kind_ai,              /* AI, command scripts, AI dialogue: stand-ins */
	_kind_cinematic,       /* cutscenes, cameras, custom animations: stand-ins */
	_kind_sound,           /* sounds and dialogue: stand-ins */
	_kind_other,           /* the rest: stand-ins */
	NUMBER_OF_KINDS,
};

static char const *const kind_names[NUMBER_OF_KINDS] =
{
	"engine", "halo2", "no-op", "ai", "cinematic", "sound", "other",
};

/* ---------- Halo 2's value types as this engine's */

static short const h2_type_to_ce[NUMBER_OF_H2_TYPES] =
{
	_hs_unparsed, _hs_special_form, _hs_function_name, _hs_passthrough,
	_hs_type_void, _hs_type_boolean, _hs_type_real, _hs_type_short_integer,
	_hs_type_long_integer, _hs_type_string, _hs_type_script,
	_hs_type_string,                /* string id (its text, the constant's) */
	_hs_type_string,                /* unit seat mapping (the seats' names' substring) */
	_hs_type_trigger_volume,
	_hs_type_cutscene_flag,
	_hs_type_cutscene_camera_point,
	_hs_type_cutscene_title,
	_hs_type_cutscene_recording,
	_hs_type_device_group,
	_hs_type_ai,
	_hs_type_ai_command_list,
	_hs_type_short_integer,         /* ai command script */
	_hs_type_short_integer,         /* ai behavior */
	_hs_type_short_integer,         /* ai orders */
	_hs_type_starting_profile,
	_hs_type_conversation,
	_hs_type_short_integer,         /* structure bsp */
	_hs_type_navpoint,
	_hs_type_long_integer,          /* point reference */
	_hs_type_long_integer,          /* style */
	_hs_type_hud_message,
	_hs_type_object_list,
	_hs_type_sound,
	_hs_type_effect,
	_hs_type_damage,
	_hs_type_looping_sound,
	_hs_type_animation_graph,
	_hs_type_damage_effect,
	_hs_type_object_definition,
	_hs_type_long_integer,          /* bitmap */
	_hs_type_long_integer,          /* shader */
	_hs_type_long_integer,          /* render model */
	_hs_type_long_integer,          /* structure definition */
	_hs_type_long_integer,          /* lightmap definition */
	_hs_type_enum_game_difficulty,
	_hs_type_enum_team,
	_hs_type_enum_actor_type,
	_hs_type_enum_hud_corner,
	_hs_type_short_integer,         /* model state */
	_hs_type_short_integer,         /* network event */
	_hs_type_object, _hs_type_unit, _hs_type_vehicle, _hs_type_weapon, _hs_type_device, _hs_type_scenery,
	_hs_type_object_name, _hs_type_unit_name, _hs_type_vehicle_name, _hs_type_weapon_name,
	_hs_type_device_name, _hs_type_scenery_name,
};

static short type_from_h2(unsigned short type)
{
	return type < NUMBER_OF_H2_TYPES ? h2_type_to_ce[type] : _hs_unparsed;
}

/* the types whose values are Halo 2's and mean nothing to this engine's
functions (an AI reference, a tag handle, a Halo 2 team...) */
static boolean type_is_halo2s(short type)
{
	switch (type)
	{
	case _hs_type_cutscene_title:
	case _hs_type_cutscene_recording:
	case _hs_type_ai:
	case _hs_type_ai_command_list:
	case _hs_type_starting_profile:
	case _hs_type_conversation:
	case _hs_type_navpoint:
	case _hs_type_hud_message:
	case _hs_type_enum_team:
	case _hs_type_enum_ai_default_state:
	case _hs_type_enum_actor_type:
		return TRUE;
	default:
		return HS_TYPE_IS_TAG_REFERENCE(type);
	}
}

/* ---------- little-endian fields */

static unsigned long get32(void const *p)
{
	unsigned long v;

	memcpy(&v, p, 4);
	return v;
}

static unsigned short get16(void const *p)
{
	unsigned short v;

	memcpy(&v, p, 2);
	return v;
}

static void put32(void *p, unsigned long v)
{
	memcpy(p, &v, 4);
}

static void put16(void *p, unsigned short v)
{
	memcpy(p, &v, 2);
}

/* ---------- the level builder */

/* an empty script node array (this engine's shipped maps' size) and its
strings: no scripts */
H2_ALIGN static void scripts_write_empty(struct h2_objanim_host const *host, unsigned long scenario_root)
{
	unsigned long nodes = CE_SHIPPED_NODES, size = CE_DATA_ARRAY_HEADER + nodes * H2_NODE_SIZE;
	unsigned char *syntax = calloc(size, 1);
	unsigned char *strings = calloc(CE_STRINGS_CONSOLE_ROOM, 1);

	if (syntax && strings)
	{
		memcpy(syntax, "script node", 12);
		put16(syntax + 0x20, (unsigned short)nodes);
		put16(syntax + 0x22, H2_NODE_SIZE);
		syntax[0x24] = 1;
		put32(syntax + 0x28, 0x64407440UL);   /* d@t@ */
		put16(syntax + 0x32, 0xE373);
		host->data(host->context, scenario_root + CE_SCENARIO_SYNTAX, syntax, size);
		host->data(host->context, scenario_root + CE_SCENARIO_STRINGS, strings, CE_STRINGS_CONSOLE_ROOM);
	}
	free(syntax);
	free(strings);
}

/* the syntax nodes as this engine's (their types renumbered), and the
array's header; FALSE if they cannot be */
static short stand_in_kind(char const *name);

/* (a level whose scripts have more nodes than an array holds, 04b's 34939)
each old node's new datum index, NONE: gone; NULL: the nodes kept where
they were */
static long *node_remap;
static long node_remap_count;

static long remap_index(long index)
{
	long absolute = index & 0xFFFF;

	if (index == NONE || !node_remap)
		return index;
	return absolute < node_remap_count ? node_remap[absolute] : NONE;
}

/* a node and what it holds (its calls' arguments: the nodes from its first
on) let go */
static long nodes_free(unsigned char *raw, long count, long index)
{
	long absolute = index & 0xFFFF, freed = 1, child;
	unsigned char *n;

	if (index == NONE || absolute >= count)
		return 0;
	n = raw + H2_NODE_SIZE * absolute;
	if (!(get16(n + 6) & NODE_PRIMITIVE))
	{
		for (child = (long)get32(n + 16); child != NONE && (child & 0xFFFF) < count;)
		{
			long next = (long)get32(raw + H2_NODE_SIZE * (child & 0xFFFF) + 8);

			freed += nodes_free(raw, count, child);
			child = next;
		}
	}
	put16(n, 0);
	return freed;
}

/* the calls of nothing to do here (prediction, lighting hints, data mining:
stand_in_kind's no-ops) that are statements of a begin taken out of it,
until the scripts fit an array; how many nodes went */
static long no_op_statements_remove(unsigned char *raw, long count, char const *strings, unsigned long string_bytes, long used)
{
	long *previous = malloc(count * sizeof(long)), *parent = malloc(count * sizeof(long));
	long i, removed = 0;

	if (!previous || !parent)
	{
		free(previous);
		free(parent);
		return 0;
	}
	for (i = 0; i < count; i++)
		previous[i] = parent[i] = NONE;
	for (i = 0; i < count; i++)
	{
		unsigned char const *n = raw + H2_NODE_SIZE * i;
		long name, child, before;

		if (!get16(n) || (get16(n + 6) & NODE_PRIMITIVE))
			continue;
		name = (long)get32(n + 16) & 0xFFFF;
		if (name >= count)
			continue;
		before = name;
		for (child = (long)get32(raw + H2_NODE_SIZE * name + 8); child != NONE && (child & 0xFFFF) < count;
			child = (long)get32(raw + H2_NODE_SIZE * (child & 0xFFFF) + 8))
		{
			parent[child & 0xFFFF] = i;
			previous[child & 0xFFFF] = before;
			before = child & 0xFFFF;
		}
	}
	for (i = 0; i < count && used - removed > H2_SCRIPTS_MAXIMUM_NODES - H2_SCRIPTS_CONSOLE_ROOM; i++)
	{
		unsigned char *n = raw + H2_NODE_SIZE * i;
		unsigned long name_offset, parent_name_offset;
		long next;

		if (!get16(n) || (get16(n + 6) & (NODE_PRIMITIVE | NODE_SCRIPT)) || get16(n + 4) != _hs_type_void || parent[i] == NONE)
			continue;
		name_offset = get32(raw + H2_NODE_SIZE * (get32(n + 16) & 0xFFFF) + 12);
		parent_name_offset = get32(raw + H2_NODE_SIZE * (get32(raw + H2_NODE_SIZE * parent[i] + 16) & 0xFFFF) + 12);
		if (name_offset >= string_bytes || parent_name_offset >= string_bytes ||
			!memchr(strings + name_offset, 0, string_bytes - name_offset) || strcmp(strings + parent_name_offset, "begin") ||
			stand_in_kind(strings + name_offset) != _kind_no_op)
		{
			continue;
		}
		/* (out of its begin's statements) */
		next = (long)get32(n + 8);
		put32(raw + H2_NODE_SIZE * previous[i] + 8, (unsigned long)next);
		if (next != NONE && (next & 0xFFFF) < count)
			previous[next & 0xFFFF] = previous[i];
		put32(n + 8, (unsigned long)NONE);
		removed += nodes_free(raw, count, i);
	}
	free(previous);
	free(parent);
	return removed;
}

/* the syntax nodes as this engine's (their types renumbered), and the
array's header; FALSE if they cannot be. Nodes past the most an array holds
(H2_SCRIPTS_MAXIMUM_NODES) are made room for by taking out no-op
statements and closing up the array (node_remap) */
H2_ALIGN static boolean syntax_write(struct h2_cache *cache, struct h2_objanim_host const *host, unsigned long scenario_root,
	unsigned long scenario, char const *strings, unsigned long string_bytes, char *why, long why_size)
{
	long count, i, used = 0, first_free = NONE, removed = 0;
	unsigned long nodes, maximum, size;
	unsigned char *syntax, *raw;
	unsigned short next_salt = 0;

	free(node_remap);
	node_remap = NULL;
	node_remap_count = 0;
	if (!h2_cache_block(cache, scenario + H2_SCENARIO_SYNTAX, H2_NODE_SIZE, &count, &nodes) || count <= 0)
	{
		snprintf(why, why_size, "no script nodes");
		return FALSE;
	}
	if (count > 0xFFFF)
	{
		snprintf(why, why_size, "%ld script nodes", count);
		return FALSE;
	}
	raw = malloc(count * H2_NODE_SIZE);
	if (!raw)
		return FALSE;
	for (i = 0; i < count; i++)
	{
		unsigned char const *h = h2_cache_at(cache, nodes + H2_NODE_SIZE * i, H2_NODE_SIZE);

		if (!h)
		{
			free(raw);
			snprintf(why, why_size, "script node %ld cannot be read", i);
			return FALSE;
		}
		memcpy(raw + H2_NODE_SIZE * i, h, H2_NODE_SIZE);
		if (get16(h))
			used++;
	}
	/* (room kept for the console's own commands: 04b's scripts filled the
	array and every console command failed, "i couldn't allocate a syntax
	node") */
	if (count > H2_SCRIPTS_MAXIMUM_NODES - H2_SCRIPTS_CONSOLE_ROOM)
	{
		long new_count = 0;

		if (used > H2_SCRIPTS_MAXIMUM_NODES - H2_SCRIPTS_CONSOLE_ROOM)
			removed = no_op_statements_remove(raw, count, strings, string_bytes, used);
		if (used - removed > H2_SCRIPTS_MAXIMUM_NODES)
		{
			free(raw);
			snprintf(why, why_size, "%ld script nodes in use (%ld no-op ones taken out), more than the %d a script node array holds",
				used, removed, H2_SCRIPTS_MAXIMUM_NODES);
			return FALSE;
		}
		/* (closed up: each node's links given the new indices) */
		node_remap = malloc(count * sizeof(long));
		if (!node_remap)
		{
			free(raw);
			return FALSE;
		}
		node_remap_count = count;
		for (i = 0; i < count; i++)
		{
			unsigned short salt = get16(raw + H2_NODE_SIZE * i);

			node_remap[i] = salt ? (long)(((unsigned long)salt << 16) | (unsigned long)new_count++) : NONE;
		}
		for (i = 0; i < count; i++)
		{
			unsigned char *n = raw + H2_NODE_SIZE * i;

			if (!get16(n))
				continue;
			put32(n + 8, (unsigned long)remap_index((long)get32(n + 8)));
			if (!(get16(n + 6) & NODE_PRIMITIVE))
				put32(n + 16, (unsigned long)remap_index((long)get32(n + 16)));
			if (node_remap[i] != NONE)
				memmove(raw + H2_NODE_SIZE * (node_remap[i] & 0xFFFF), n, H2_NODE_SIZE);
		}
		platform_log("h2 scripts: %ld script nodes in use: %ld no-op statements' nodes taken out, closed up to %ld", used, removed, new_count);
		count = new_count;
		used = new_count;
	}
	maximum = count > CE_SHIPPED_NODES ? (unsigned long)count : CE_SHIPPED_NODES;
	if ((long)maximum < used + H2_SCRIPTS_CONSOLE_ROOM)
		maximum = (unsigned long)MIN(used + H2_SCRIPTS_CONSOLE_ROOM, H2_SCRIPTS_MAXIMUM_NODES);
	size = CE_DATA_ARRAY_HEADER + maximum * H2_NODE_SIZE;
	syntax = calloc(size, 1);
	if (!syntax)
	{
		free(raw);
		snprintf(why, why_size, "no memory for %ld script nodes", count);
		return FALSE;
	}
	for (i = 0; i < count; i++)
	{
		unsigned char const *h = raw + H2_NODE_SIZE * i;
		unsigned char *o = syntax + CE_DATA_ARRAY_HEADER + H2_NODE_SIZE * i;
		unsigned short salt, flags, type;

		salt = get16(h);
		if (!salt)
		{
			if (first_free == NONE)
				first_free = i;
			continue;
		}
		if ((unsigned short)(salt + 1) > next_salt || !next_salt)
			next_salt = (unsigned short)(salt + 1);
		memcpy(o, h, H2_NODE_SIZE);
		flags = get16(h + 6);
		type = get16(h + 4);
		put16(o + 4, (unsigned short)type_from_h2(type));
		/* (a constant's own type, in the index's place) */
		if ((flags & NODE_PRIMITIVE) && !(flags & NODE_GLOBAL) && type != _hs_function_name)
			put16(o + 2, (unsigned short)type_from_h2(get16(h + 2)));
	}
	free(raw);
	if (!node_remap)
		used = 0;
	for (i = 0, used = 0; i < count; i++)
		used += get16(syntax + CE_DATA_ARRAY_HEADER + H2_NODE_SIZE * i) != 0;
	memcpy(syntax, "script node", 12);
	put16(syntax + 0x20, (unsigned short)maximum);
	put16(syntax + 0x22, H2_NODE_SIZE);
	syntax[0x24] = 1;
	put32(syntax + 0x28, 0x64407440UL);   /* d@t@ */
	put16(syntax + 0x2C, (unsigned short)(first_free == NONE ? count : first_free));
	put16(syntax + 0x2E, (unsigned short)count);
	put16(syntax + 0x30, (unsigned short)used);
	put16(syntax + 0x32, next_salt ? next_salt : 0xE373);
	host->data(host->context, scenario_root + CE_SCENARIO_SYNTAX, syntax, size);
	free(syntax);

	return !host->failed(host->context);
}

/* the script strings, with the console's room after them (the caller
frees); NULL, why, if they cannot be read */
H2_ALIGN static unsigned char *strings_read(struct h2_cache *cache, unsigned long scenario, unsigned long *out_size, char *why, long why_size)
{
	unsigned long size = h2_cache_read32(cache, scenario + H2_SCENARIO_SCRIPT_STRINGS);
	unsigned long address = h2_cache_read32(cache, scenario + H2_SCENARIO_SCRIPT_STRINGS + 4);
	unsigned char *strings;
	unsigned long offset;

	if (size > 16 * 1024 * 1024)
	{
		snprintf(why, why_size, "script strings of %lu bytes", size);
		return NULL;
	}
	strings = calloc(size + CE_STRINGS_CONSOLE_ROOM, 1);
	if (!strings)
		return NULL;
	/* (read a piece at a time: the map's data is inflated in segments) */
	for (offset = 0; offset < size;)
	{
		unsigned long piece = size - offset > 0x1000 ? 0x1000 : size - offset;
		void const *p = h2_cache_at(cache, address + offset, piece);

		if (!p)
		{
			free(strings);
			snprintf(why, why_size, "script strings cannot be read at %lu", offset);
			return NULL;
		}
		memcpy(strings + offset, p, piece);
		offset += piece;
	}
	*out_size = size;
	return strings;
}

/* the scripts (a command script as a static one) and globals */
H2_ALIGN static boolean scripts_globals_write(struct h2_cache *cache, struct h2_objanim_host const *host, unsigned long scenario_root,
	unsigned long scenario, long *script_count, long *global_count)
{
	long count, i;
	unsigned long elements, out;

	*script_count = *global_count = 0;
	if (h2_cache_block(cache, scenario + H2_SCENARIO_SCRIPTS, H2_SCRIPT_SIZE, &count, &elements) && count > 0)
	{
		out = host->block(host->context, scenario_root + CE_SCENARIO_SCRIPTS, count, CE_SCRIPT_SIZE);
		for (i = 0; i < count && !host->failed(host->context); i++)
		{
			unsigned char const *h = h2_cache_at(cache, elements + H2_SCRIPT_SIZE * i, H2_SCRIPT_SIZE);
			unsigned char *o = host->at(host->context, out + CE_SCRIPT_SIZE * i);
			unsigned short type;

			if (!h)
				return FALSE;
			memcpy(o, h, 32);
			o[31] = 0;
			type = get16(h + 0x20);
			put16(o + 0x20, type == H2_SCRIPT_COMMAND_SCRIPT ? _hs_script_static : type);
			put16(o + 0x22, (unsigned short)type_from_h2(get16(h + 0x22)));
			put32(o + 0x24, (unsigned long)remap_index((long)get32(h + 0x24)));
		}
		*script_count = count;
	}
	if (h2_cache_block(cache, scenario + H2_SCENARIO_GLOBALS, H2_GLOBAL_SIZE, &count, &elements) && count > 0)
	{
		out = host->block(host->context, scenario_root + CE_SCENARIO_GLOBALS, count, CE_GLOBAL_SIZE);
		for (i = 0; i < count && !host->failed(host->context); i++)
		{
			unsigned char const *h = h2_cache_at(cache, elements + H2_GLOBAL_SIZE * i, H2_GLOBAL_SIZE);
			unsigned char *o = host->at(host->context, out + CE_GLOBAL_SIZE * i);

			if (!h)
				return FALSE;
			memcpy(o, h, 32);
			o[31] = 0;
			put16(o + 0x20, (unsigned short)type_from_h2(get16(h + 0x20)));
			put32(o + 0x28, (unsigned long)remap_index((long)get32(h + 0x24)));
		}
		*global_count = count;
	}

	return !host->failed(host->context);
}

/* the blocks the scripts name things in by index, in Halo 2's order */
H2_ALIGN static void named_blocks_write(struct h2_cache *cache, struct h2_objanim_host const *host, unsigned long scenario_root,
	unsigned long scenario, long counts[4])
{
	long count, i;
	unsigned long elements, out;

	counts[0] = counts[1] = counts[2] = counts[3] = 0;
	/* (the object names and trigger volumes, which the scripts name by
	index too, are h2_level.c's: a campaign level's, in Halo 2's order -
	its placements name the objects. One writer: they were written twice) */
	/* cutscene flags: the same first 0x38 bytes */
	if (h2_cache_block(cache, scenario + H2_SCENARIO_CUTSCENE_FLAGS, H2_CUTSCENE_FLAG_SIZE, &count, &elements) && count > 0)
	{
		out = host->block(host->context, scenario_root + CE_SCENARIO_CUTSCENE_FLAGS, count, CE_CUTSCENE_FLAG_SIZE);
		for (i = 0; i < count && !host->failed(host->context); i++)
		{
			unsigned char const *h = h2_cache_at(cache, elements + H2_CUTSCENE_FLAG_SIZE * i, H2_CUTSCENE_FLAG_SIZE);
			unsigned char *o = host->at(host->context, out + CE_CUTSCENE_FLAG_SIZE * i);

			if (h)
				memcpy(o, h, H2_CUTSCENE_FLAG_SIZE);
			o[4 + 31] = 0;
		}
		counts[2] = count;
	}
	/* cutscene camera points: name, position, orientation; Halo 2's have no
	field of view (camera_set_field_of_view sets it): 70 degrees */
	if (h2_cache_block(cache, scenario + H2_SCENARIO_CAMERA_POINTS, H2_CAMERA_POINT_SIZE, &count, &elements) && count > 0)
	{
		out = host->block(host->context, scenario_root + CE_SCENARIO_CAMERA_POINTS, count, CE_CAMERA_POINT_SIZE);
		for (i = 0; i < count && !host->failed(host->context); i++)
		{
			unsigned char const *h = h2_cache_at(cache, elements + H2_CAMERA_POINT_SIZE * i, H2_CAMERA_POINT_SIZE);
			unsigned char *o = host->at(host->context, out + CE_CAMERA_POINT_SIZE * i);
			float field_of_view = 70.0f * 3.14159265f / 180.0f;

			if (!h)
				continue;
			memcpy(o + 4, h + 4, 32);
			o[4 + 31] = 0;
			/* (two layouts in MCC's maps: position at 0x24 and orientation at
			0x30, the last word 0 - 03a's cinematic_test points, the main
			menu's scene points -, or a word of 0 first, position at 0x28
			and orientation (yaw, pitch, roll) at 0x34 - 03a's prediction
			points, the main menu's orbit, its roll at 0x3C) */
			{
				unsigned long first, last;

				memcpy(&first, h + 0x24, 4);
				memcpy(&last, h + 0x3C, 4);
				memcpy(o + 0x28, h + (first == 0 && last != 0 ? 0x28 : 0x24), 24);
			}
			memcpy(o + 0x40, &field_of_view, 4);
		}
		counts[3] = count;
	}
}

H2_ALIGN long h2_scripts_effect_tags(struct h2_cache *cache, long *tags, long maximum)
{
	/* (a constant of Halo 2's type effect, 33) */
	return h2_scripts_constant_tags(cache, 33, tags, maximum);
}

H2_ALIGN long h2_scripts_constant_tags(struct h2_cache *cache, short h2_type, long *tags, long maximum)
{
	struct h2_cache_tag scenario;
	long count, i, found = 0;
	unsigned long nodes;

	if (getenv("HALO_H2_NO_SCRIPTS") || !h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + H2_SCENARIO_SYNTAX, H2_NODE_SIZE, &count, &nodes))
	{
		return 0;
	}
	for (i = 0; i < count && found < maximum; i++)
	{
		unsigned char const *h = h2_cache_at(cache, nodes + H2_NODE_SIZE * i, H2_NODE_SIZE);
		long tag, k;

		/* (a constant of the type: its value the tag's handle) */
		if (!h || !get16(h) || (get16(h + 6) & (NODE_PRIMITIVE | NODE_GLOBAL)) != NODE_PRIMITIVE || (short)get16(h + 2) != h2_type)
			continue;
		tag = h2_cache_tag_from_handle(cache, get32(h + 16));
		if (tag == NONE)
			continue;
		for (k = 0; k < found && tags[k] != tag; k++)
			;
		if (k == found)
			tags[found++] = tag;
	}
	return found;
}

boolean h2_scripts_call(struct h2_cache *cache, char const *function_name)
{
	static char scenario_name[256];
	static boolean called;
	struct h2_cache_tag scenario;
	char why[128];
	unsigned long string_bytes = 0, at;
	unsigned char *strings;
	size_t length = strlen(function_name);

	if (!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario))
		return FALSE;
	/* (asked for each of a level's vehicles' functions: the last answer kept) */
	if (!strcmp(scenario_name, h2_cache_tag_name(cache, h2_cache_scenario_tag(cache))))
		return called;
	snprintf(scenario_name, sizeof(scenario_name), "%s", h2_cache_tag_name(cache, h2_cache_scenario_tag(cache)));
	called = FALSE;
	if ((strings = strings_read(cache, scenario.address, &string_bytes, why, sizeof(why))) != NULL)
	{
		for (at = 0; at + length < string_bytes && !called; at += strlen((char const *)strings + at) + 1)
			called = !strcmp((char const *)strings + at, function_name);
		free(strings);
	}
	return called;
}

H2_ALIGN boolean h2_scripts_write(
	struct h2_cache *cache,
	struct h2_objanim_host const *host,
	unsigned long scenario_root,
	boolean scripts_wanted,
	char *why,
	long why_size)
{
	struct h2_cache_tag scenario;
	long scripts = 0, globals = 0, counts[4];
	unsigned char *strings = NULL;
	unsigned long string_bytes = 0;

	*why = 0;
	if (!scripts_wanted || getenv("HALO_H2_NO_SCRIPTS"))
	{
		snprintf(why, why_size, scripts_wanted ? "HALO_H2_NO_SCRIPTS" : "not run on a multiplayer level");
		scripts_write_empty(host, scenario_root);
		return FALSE;
	}
	if (!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!(strings = strings_read(cache, scenario.address, &string_bytes, why, why_size)) ||
		!syntax_write(cache, host, scenario_root, scenario.address, (char const *)strings, string_bytes, why, why_size))
	{
		free(strings);
		if (!*why)
			snprintf(why, why_size, "the scenario cannot be read");
		scripts_write_empty(host, scenario_root);
		return FALSE;
	}
	host->data(host->context, scenario_root + CE_SCENARIO_STRINGS, strings, string_bytes + CE_STRINGS_CONSOLE_ROOM);
	free(strings);
	if (host->failed(host->context) ||
		!scripts_globals_write(cache, host, scenario_root, scenario.address, &scripts, &globals))
	{
		if (!*why)
			snprintf(why, why_size, "the scripts cannot be read");
		/* (no scripts or globals run on the nodes: blocks left empty) */
		host->block(host->context, scenario_root + CE_SCENARIO_SCRIPTS, 0, CE_SCRIPT_SIZE);
		host->block(host->context, scenario_root + CE_SCENARIO_GLOBALS, 0, CE_GLOBAL_SIZE);
		return FALSE;
	}
	named_blocks_write(cache, host, scenario_root, scenario.address, counts);
	platform_log("h2 scripts: %s: %ld scripts, %ld globals; %ld cutscene flags, %ld camera points",
		h2_cache_name(cache), scripts, globals, counts[2], counts[3]);

	return !host->failed(host->context);
}

/* ---------- the runtime: functions */

struct h2_function
{
	/* (first: hs_function_get gives its address) */
	struct hs_function_definition definition;
	short more_parameter_types[MAXIMUM_ARGUMENTS - 1];
	char const *name;
	short kind;
	short engine_index;   /* (_kind_engine) */
	long call_sites;
	long calls;
};

typedef char h2_function_parameters_follow[
	offsetof(struct h2_function, more_parameter_types) ==
		offsetof(struct h2_function, definition) + offsetof(struct hs_function_definition, parameter_types) + sizeof(short) ? 1 : -1];

static struct
{
	struct h2_function *functions;
	long count;
	long capacity;
	/* the BSP Halo 2's scripts asked for last (switch_bsp), until a switcher
	says */
	short requested_bsp;
	boolean (*bsp_switcher)(short h2_bsp_index);
	short (*bsp_current)(void);
	long last_report_time;
	long calls_at_last_report;
	long total_calls;
	char map_name[64];
	/* the scripts' sounds: Halo 2's tag handle, its name (the maps' without
	"sound\\"), the pool sound carrying it (h2_sound.c; NONE: none), and
	impulses played and not (no pool sound) */
	struct { unsigned long handle; char const *name; } sounds[512];
	long sound_count;
	/* the scripts' looping sounds: Halo 2's tag handle and its name (the
	maps'), the looping sound tag carrying it (h2_sound.c; NONE: none), and
	the starts played and not */
	struct { unsigned long handle; char const *name; } loops[128];
	long loop_count;
	long loops_started;
	long loops_not_carried;
	long sounds_played;
	long sounds_not_carried;
	/* the scripts' effects: Halo 2's handle, this engine's effect tag made of
	it (h2_effects.c, "h2\\" and its name), NONE: none */
	struct { unsigned long handle; long tag_index; } effects[256];
	long effect_count;
	long effects_played;
	long effects_missing;
	/* object_hide's hidden objects; the enabled kill volumes */
	long hidden[256];
	long hidden_count;
	real standard_gravity;    /* (physics_set_gravity: this engine's before; 0 not changed) */
	/* (cinematic lighting: the objects lit by it, and its lights - Halo 2's
	primary and secondary (pitch, yaw degrees; colour) and ambient) */
	long cinematic_lit[256];
	long cinematic_lit_count;
	float cinematic_primary[5], cinematic_secondary[5], cinematic_ambient[3];
	short kill_volumes[64];
	long kill_volume_count;
	long kills;
	/* object_set_function_variable's values */
	struct h2_object_variable { long object_index; char name[32]; real value, target, rate; boolean set; } variables[256];
	long variable_count;
	/* hud_cinematic_fade's change, when it is due */
	boolean hud_fade_pending, hud_fade_show;
	long hud_fade_at;
} h2_scripts_globals;
/* (perf/ce-floor-2) object_set_function_variable's values' count, read by
object_export_function_values (objects.c) before its per-object walk: with
none - a Halo CE level - no function value is ever found */
long const *const h2_scripts_variable_count = &h2_scripts_globals.variable_count;

static struct h2_function *function_from_index(short function_index)
{
	long i = (long)function_index - H2_SCRIPTS_FUNCTION_BASE;

	return i >= 0 && i < h2_scripts_globals.count ? &h2_scripts_globals.functions[i] : NULL;
}

boolean h2_scripts_function_index_valid(
	short function_index)
{
	return function_from_index(function_index) != NULL;
}

struct hs_function_definition *h2_scripts_function_get(
	short function_index)
{
	struct h2_function *function = function_from_index(function_index);

	return function ? &function->definition : NULL;
}

/* a call counted (the first logged) */
static struct h2_function *function_called(short function_index, long thread_index)
{
	struct h2_function *function = function_from_index(function_index);

	if (!function)
		return NULL;
	if (!function->calls++ && function->kind != _kind_no_op && function->kind != _kind_halo2)
	{
		char const *thread = hs_runtime_get_executing_thread_name();

		platform_log("h2 scripts: first call of %s (%s stand-in) in %s", function->name, kind_names[function->kind],
			thread ? thread : "?");
	}
	h2_scripts_globals.total_calls++;
	(void)thread_index;

	return function;
}

/* a stand-in: its arguments evaluated (their calls made), its type's
default given */
H2_ALIGN static void stand_in_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;

	if (!hs_runtime_arguments_evaluate_all(thread_index, 0, &count, initialize))
		return;
	function_called(function_index, thread_index);
	hs_return(thread_index, hs_runtime_type_default(hs_runtime_call_type(thread_index)));
}

/* ---------- the runtime: Halo 2's functions of this file's */

union value
{
	long l;
	short s;
	boolean b;
	real r;
};

static long return_real(real r)
{
	union value v;

	v.l = 0;
	v.r = r;
	return v.l;
}

static long return_short(short s)
{
	union value v;

	v.l = 0;
	v.s = s;
	return v.l;
}

static long return_boolean(boolean b)
{
	union value v;

	v.l = 0;
	v.b = b;
	return v.l;
}

static real argument_real(long const *a, int i)
{
	union value v;

	v.l = a[i];
	return v.r;
}

static short argument_short(long const *a, int i)
{
	union value v;

	v.l = a[i];
	return v.s;
}

static boolean argument_boolean(long const *a, int i)
{
	union value v;

	v.l = a[i];
	return v.b;
}

/* (sleep_forever [<script>]) */
H2_ALIGN static void sleep_forever_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *arguments = hs_runtime_arguments_evaluate_all(thread_index, 1, &count, initialize);

	if (!arguments)
		return;
	function_called(function_index, thread_index);
	hs_runtime_sleep_forever(thread_index, count > 0 ? argument_short(arguments, 0) : NONE);
	hs_return(thread_index, 0);
}

/* the arguments of a call of a function of fixed parameters */
/* (a function of any argument types - parameter count NONE: native_call_fits -
the scripts' weapon, damage and object names as they are, evaluated) */
static long *arguments_any(short function_index, long thread_index, short maximum, boolean initialize)
{
	short count;
	long *a = hs_runtime_arguments_evaluate_all(thread_index, maximum, &count, initialize);

	if (a)
		function_called(function_index, thread_index);
	return a;
}

static long *arguments_get(short function_index, long thread_index, boolean initialize)
{
	static long none[1];
	struct h2_function *function = function_from_index(function_index);
	/* (none to evaluate: the stack has no room of 0 bytes to give) */
	long *arguments = function && !function->definition.parameter_count ? none :
		hs_macro_function_evaluate(function_index, thread_index, initialize);

	if (arguments)
		function_called(function_index, thread_index);
	return arguments;
}

H2_ALIGN static void print_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	/* (the tick and the script, for a timeline of a level's scripted events
	against Halo 2's scripts) */
	{
		char const *thread = hs_runtime_get_executing_thread_name();

		platform_log("h2 script print: %s [tick %ld %s]", a[0] ? (char const *)a[0] : "", (long)game_time_get(), thread ? thread : "?");
	}
	hs_return(thread_index, 0);
}

H2_ALIGN static void pin_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	real value, low, high;

	if (!a)
		return;
	value = argument_real(a, 0), low = argument_real(a, 1), high = argument_real(a, 2);
	hs_return(thread_index, return_real(value < low ? low : value > high ? high : value));
}

/* a campaign level's BSPs are Halo 2's, in its order (h2_level.c, every
structure BSP of the level); a level of one BSP (a multiplayer one, or a
build without them) remembers the index asked for */
static short bsp_count(void)
{
	return global_scenario_index != NONE ? (short)global_scenario_get()->structure_bsp_references.count : 0;
}

static short bsp_current(void)
{
	if (h2_scripts_globals.bsp_current)
		return h2_scripts_globals.bsp_current();
	return bsp_count() > 1 ? global_structure_bsp_index_get() : h2_scripts_globals.requested_bsp;
}

static void bsp_switch(short index)
{
	boolean switched = FALSE;

	boolean other = index >= 0 && index < bsp_count() && index != global_structure_bsp_index_get();

	if (h2_scripts_globals.bsp_switcher)
		switched = h2_scripts_globals.bsp_switcher(index);
	else if (index >= 0 && index < bsp_count() && bsp_count() > 1)
		switched = index == global_structure_bsp_index_get() || scenario_switch_structure_bsp(index);
	/* (another BSP loaded: "Loading... done", as Halo 2 says at an area's
	switch - this engine's HUD's loading message, as its own switch says it) */
	if (switched && other && index == global_structure_bsp_index_get())
	{
		void hud_load(boolean load);

		hud_load(FALSE);
	}
	if (!switched)
		platform_log("h2 scripts: switch_bsp %d asked for, not switched (%d BSPs): remembered for structure_bsp_index", index, bsp_count());
	h2_scripts_globals.requested_bsp = index;
}

H2_ALIGN static void switch_bsp_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	bsp_switch(argument_short(a, 0));
	hs_return(thread_index, 0);
}

extern void h2_vehicle_flashlights_set(boolean allowed);

H2_ALIGN static void game_can_use_flashlights_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	h2_vehicle_flashlights_set(argument_boolean(a, 0));
	hs_return(thread_index, 0);
}

H2_ALIGN static void structure_bsp_index_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	hs_return(thread_index, return_short(bsp_current()));
}

static boolean object_dead(struct object_datum const *object)
{
	return TEST_FLAG(object->object.damage_flags, _object_dead_bit);
}

H2_ALIGN static void list_count_not_dead_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	long reference, object_index;
	short count = 0;

	if (!a)
		return;
	for (object_index = a[0] != NONE ? object_list_get_first(a[0], &reference) : NONE; object_index != NONE;
		object_index = object_list_get_next(a[0], &reference))
	{
		struct object_datum *object = object_try_and_get(object_index);

		if (object && !object_dead(object))
			count++;
	}
	hs_return(thread_index, return_short(count));
}

/* object_get_health, object_get_shield: [0,1], -1 for no object */
H2_ALIGN static void object_get_vitality_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);
	struct object_datum *object;
	real value = -1.0f;

	if (!a)
		return;
	object = a[0] != NONE ? object_try_and_get(a[0]) : NULL;
	if (object && function)
		value = !strcmp(function->name, "object_get_shield") ? object->object.shield_vitality : object->object.body_vitality;
	hs_return(thread_index, return_real(value));
}

/* the least distance from a list's objects to a point; -1 for none */
static real objects_distance_to_point(long list_index, real_point3d const *point)
{
	long reference, object_index;
	real best = -1.0f;

	for (object_index = list_index != NONE ? object_list_get_first(list_index, &reference) : NONE; object_index != NONE;
		object_index = object_list_get_next(list_index, &reference))
	{
		real_point3d origin;
		real d;

		if (!object_try_and_get(object_index))
			continue;
		object_get_origin(object_index, &origin);
		d = sqrtf((origin.x - point->x) * (origin.x - point->x) + (origin.y - point->y) * (origin.y - point->y) +
			(origin.z - point->z) * (origin.z - point->z));
		if (best < 0.0f || d < best)
			best = d;
	}
	return best;
}

H2_ALIGN static void objects_distance_to_object_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	real_point3d point;
	real d = -1.0f;

	if (!a)
		return;
	if (a[1] != NONE && object_try_and_get(a[1]))
	{
		object_get_origin(a[1], &point);
		d = objects_distance_to_point(a[0], &point);
	}
	hs_return(thread_index, return_real(d));
}

H2_ALIGN static void objects_distance_to_flag_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct scenario *scenario = global_scenario_get();
	short flag;
	real d = -1.0f;

	if (!a)
		return;
	flag = argument_short(a, 1);
	if (flag >= 0 && flag < scenario->cutscene_flags.count)
	{
		real_point3d point;
		unsigned char const *f = (unsigned char const *)scenario->cutscene_flags.address + CE_CUTSCENE_FLAG_SIZE * flag;

		memcpy(&point, f + 0x24, sizeof(point));
		d = objects_distance_to_point(a[0], &point);
	}
	hs_return(thread_index, return_real(d));
}

H2_ALIGN static void objects_distance_to_position_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	real_point3d point;

	if (!a)
		return;
	point.x = argument_real(a, 1), point.y = argument_real(a, 2), point.z = argument_real(a, 3);
	hs_return(thread_index, return_real(objects_distance_to_point(a[0], &point)));
}

/* volume_return_objects, volume_return_objects_by_type: the objects in a
trigger volume (128 at most), of the type mask's types (Halo 2's object
types are this engine's for bipeds to controls) */
H2_ALIGN static void volume_return_objects_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);
	short volume;
	unsigned long mask = _object_mask_all;
	long list = NONE;

	if (!a)
		return;
	volume = argument_short(a, 0);
	if (function && function->definition.parameter_count > 1)
		mask = (unsigned long)a[1];
	if (volume >= 0 && volume < global_scenario_get()->trigger_volumes.count)
	{
		struct object_iterator iterator;
		struct object_datum *object;
		long added = 0;

		list = object_list_new();
		object_iterator_new(&iterator, mask ? mask : _object_mask_all, 0);
		while (list != NONE && added < 128 && (object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
		{
			if (object->object.parent_object_index == NONE && scenario_trigger_volume_test_object(volume, iterator.index))
			{
				object_list_add(list, iterator.index);
				added++;
			}
		}
	}
	hs_return(thread_index, list);
}

H2_ALIGN static void object_get_parent_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct object_datum *object;

	if (!a)
		return;
	object = a[0] != NONE ? object_try_and_get(a[0]) : NULL;
	hs_return(thread_index, object ? object->object.parent_object_index : NONE);
}

H2_ALIGN static void unit_in_vehicle_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct object_datum *object, *parent = NULL;

	if (!a)
		return;
	object = a[0] != NONE ? object_try_and_get(a[0]) : NULL;
	if (object && object->object.parent_object_index != NONE)
		parent = (struct object_datum *)object_try_and_get_and_verify_type(object->object.parent_object_index, _object_mask_unit);
	hs_return(thread_index, return_boolean(parent != NULL));
}

/* game_tick_rate_scalar: ticks a second over 30 (this engine's 30) */
H2_ALIGN static void one_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	hs_return(thread_index, 1);
}

/* game_is_playtest, unit_is_emitting: false */
H2_ALIGN static void false_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	hs_return(thread_index, return_boolean(FALSE));
}

/* game_tick_rate: ticks a second */
H2_ALIGN static void thirty_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	hs_return(thread_index, 30);
}

/* player0_looking_up, player0_looking_down (the training's look test): the
first player's view pitched past 20 degrees up or down (Halo 2's own test
is not in the decompilation yet: an approximation) */
H2_ALIGN static void player0_looking_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);
	struct player_control *control = player_control_get(0);
	real pitch = control ? control->desired_angles.pitch : 0.0f;
	real const threshold = 20.0f * 3.14159265f / 180.0f;

	if (!a)
		return;
	hs_return(thread_index, return_boolean(function && strstr(function->name, "_up") ? pitch > threshold : pitch < -threshold));
}

/* player_action_test_lookstick_forward / _backward: the look stick pushed
up or down since the action tests were reset (this engine's test) */
H2_ALIGN static void lookstick_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);

	if (!a)
		return;
	hs_return(thread_index, return_boolean(function && strstr(function->name, "forward") ?
		player_control_action_test_look_relative_up() : player_control_action_test_look_relative_down()));
}

/* the look test's begin and end: nothing to set up here */
H2_ALIGN static void nothing_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	hs_return(thread_index, 0);
}

/* a script's sound (Halo 2's handle) as the pool sound carrying it, made
to play its own permutation next (h2_sound.c), or NONE; its length */
static long sound_carried(unsigned long handle, real *seconds)
{
	long i, tag_index = NONE;

	*seconds = 0.f;
	for (i = 0; i < h2_scripts_globals.sound_count; i++)
	{
		if (h2_scripts_globals.sounds[i].handle == handle)
		{
			if (!h2_sound_script_sound(h2_scripts_globals.sounds[i].name, &tag_index, seconds))
				tag_index = NONE;
			break;
		}
	}
	return tag_index;
}

/* a script's sound played as scripted_sound_new does; on a dead unit
(Halo 2's scripts play a line or a cry on a body: 00a's council) attached
to it without its speech, which a dead unit refuses - this engine's
unit_notify_impulse_sound asserts that the speech took it */
static void script_sound_play(long tag_index, long object_index, real scale)
{
	struct object_datum *object = object_index != NONE ?
		(struct object_datum *)object_try_and_get_and_verify_type(object_index, _object_mask_unit) : NULL;
	struct sound_definition *definition;
	struct object_marker marker;
	real_point3d position = *global_origin3d;
	real_vector3d forward = *global_forward3d;
	short node_index = 0;

	if (!object || !TEST_FLAG(object->object.damage_flags, _object_dead_bit))
	{
		scripted_sound_new(tag_index, object_index, scale);
		return;
	}
	definition = sound_definition_get(tag_index);
	sound_stop_impulse(definition->scripting_sound_index);
	definition->scripting_time = game_time_get() + (long)definition->longest_permutation_length * 30 / 1000;
	if (object_get_marker_by_name(object_index, "head", &marker, 1))
	{
		node_index = marker.node_index;
		position = marker.node_matrix.position;
		forward = marker.node_matrix.forward;
	}
	definition->scripting_sound_index = object_impulse_sound_new(object_index, tag_index, node_index, &position, &forward,
		PIN(scale, 0.f, 1.f));
}

/* sound_impulse_start (sound object scale), _start_effect (... effect
name), _trigger (... flags): played as this engine's scripted sound when a
pool sound carries it (h2_sound.c gives the scripts' sounds the pool sounds
the scenery leaves), else counted */
H2_ALIGN static void sound_impulse_start_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	long tag_index;
	real seconds;

	if (!a)
		return;
	tag_index = a[0] != NONE ? sound_carried((unsigned long)a[0], &seconds) : NONE;
	if (HALO_ENV_ONCE("HALO_H2_CINEMATIC_LOG") && a[0] != NONE)
	{
		long i;

		for (i = 0; i < h2_scripts_globals.sound_count; i++)
		{
			if (h2_scripts_globals.sounds[i].handle == (unsigned long)a[0])
				platform_log("h2 cine sound: tick %ld %s on %08lx scale %.2f: %s %.2f s", (long)game_time_get(), h2_scripts_globals.sounds[i].name,
					(unsigned long)a[1], argument_real(a, 2), tag_index != NONE ? "played" : "NOT CARRIED", seconds);
		}
	}
	if (tag_index != NONE)
	{
		/* (sound_impulse_start_effect radio_*: Halo 2's radio filter - radio_3d
		the speaker's own voice near it, as h2_ai.c's lines) */
		struct h2_function *function = function_from_index(function_index);

		if (function && function->definition.parameter_count > 3 && a[3] && (char const *)a[3] != (char const *)NONE &&
			!strncmp((char const *)a[3], "radio_default", 13))
		{
			long i;

			for (i = 0; i < h2_scripts_globals.sound_count; i++)
			{
				if (h2_scripts_globals.sounds[i].handle == (unsigned long)a[0])
					h2_sound_radio_mark(h2_scripts_globals.sounds[i].name, tag_index);
			}
		}
		script_sound_play(tag_index, a[1], argument_real(a, 2));
		h2_scripts_globals.sounds_played++;
	}
	else if (a[0] != NONE)
		h2_scripts_globals.sounds_not_carried++;
	/* (a character's line: its lip-sync on its face, h2_cinematics.c -
	whether or not a pool sound carries it) */
	if (a[0] != NONE && a[1] != NONE)
	{
		long i;

		for (i = 0; i < h2_scripts_globals.sound_count; i++)
		{
			if (h2_scripts_globals.sounds[i].handle == (unsigned long)a[0])
			{
				h2_cinematics_face_start(a[1], h2_scripts_globals.sounds[i].name);
				break;
			}
		}
	}
	hs_return(thread_index, 0);
}

/* sound_impulse_stop (sound): the carried sound's scripted play stopped */
H2_ALIGN static void sound_impulse_stop_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	long i;

	if (!a)
		return;
	for (i = 0; a[0] != NONE && i < h2_scripts_globals.sound_count; i++)
	{
		if (h2_scripts_globals.sounds[i].handle == (unsigned long)a[0])
		{
			long tag_index = h2_sound_vessel(h2_scripts_globals.sounds[i].name);

			if (tag_index != NONE)
				scripted_sound_stop(tag_index);
			break;
		}
	}
	hs_return(thread_index, 0);
}

/* a script's looping sound (Halo 2's handle) as the level's looping sound
tag carrying it (h2_sound.c), or NONE */
static long looping_carried(unsigned long handle)
{
	long i;

	for (i = 0; i < h2_scripts_globals.loop_count; i++)
	{
		if (h2_scripts_globals.loops[i].handle == handle)
			return h2_sound_looping(h2_scripts_globals.loops[i].name);
	}
	return NONE;
}

/* sound_looping_start (looping_sound object scale), sound_looping_stop
(looping_sound), sound_looping_set_alternate (looping_sound boolean): this
engine's scripted looping sounds (game_sound.c) - the music, a cutscene's
foley, a horn on its vehicle - their tracks Halo 2's (h2_sound.c). HALO_H2_LOOP_LOG=1
logs each, with the tick */
H2_ALIGN static void sound_looping_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);
	char const *name = function ? function->name : "";
	long tag_index;

	if (!a)
		return;
	tag_index = a[0] != NONE ? looping_carried((unsigned long)a[0]) : NONE;
	if (getenv("HALO_H2_LOOP_LOG"))
	{
		long i;
		char const *loop_name = "?";

		for (i = 0; i < h2_scripts_globals.loop_count; i++)
		{
			if (h2_scripts_globals.loops[i].handle == (unsigned long)a[0])
				loop_name = h2_scripts_globals.loops[i].name;
		}
		platform_log("h2 script loop: tick %ld (%.2f s) %s %s%s", (long)game_time_get(), (double)vita_host_time_us() / 1e6, name, loop_name,
			tag_index == NONE ? " (not carried)" : "");
	}
	if (tag_index != NONE)
	{
		if (!strcmp(name, "sound_looping_start"))
		{
			long object = a[1] != NONE && object_try_and_get(a[1]) ? a[1] : NONE;

			scripted_looping_sound_start(tag_index, object, PIN(argument_real(a, 2), 0.f, 1.f));
			h2_scripts_globals.loops_started++;
		}
		else if (!strcmp(name, "sound_looping_stop"))
			scripted_looping_sound_stop(tag_index);
		else if (!strcmp(name, "sound_looping_set_alternate"))
			scripted_looping_sound_set_alternate(tag_index, argument_boolean(a, 1));
	}
	else if (a[0] != NONE && !strcmp(name, "sound_looping_start"))
		h2_scripts_globals.loops_not_carried++;
	hs_return(thread_index, 0);
}

/* sound_impulse_language_time: the carried sound's longest permutation in
ticks; 0 for one not carried */
H2_ALIGN static void sound_impulse_language_time_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	long tag_index, ticks = 0;
	real seconds;

	if (!a)
		return;
	tag_index = a[0] != NONE ? sound_carried((unsigned long)a[0], &seconds) : NONE;
	if (tag_index != NONE)
		ticks = (long)(seconds * 30.0f + 0.5f);
	/* (a sound no pool sound carries: its length from the level's gestalt
	(h2_sound_length_ticks), as Halo 2 reads every sound's from its tag
	(function_18a1c0); one not even known: three seconds, a dialogue line's
	length - 0 made `(sleep (- (sound_impulse_language_time line) 60))`
	a sleep of negative ticks, forever: 01b's 1st_malta never docked the
	boarding craft) */
	else if (a[0] != NONE)
	{
		long i;

		ticks = 90;
		for (i = 0; i < h2_scripts_globals.sound_count; i++)
		{
			if (h2_scripts_globals.sounds[i].handle == (unsigned long)a[0])
			{
				long known = h2_sound_length_ticks(h2_scripts_globals.sounds[i].name);

				if (known > 0)
					ticks = known;
				break;
			}
		}
	}
	hs_return(thread_index, ticks);
}

/* the devices device_animate_position moves, in time as Halo 2 does:
from where each was to the position over the seconds given (also a device
in no position group - 03b's Scarab -, which device_set_desired_position
cannot move) */
enum { H2_DEVICE_ANIMATIONS = 64 };
static struct
{
	long device_index;
	real from, to;
	long start, ticks;
} h2_device_animations[H2_DEVICE_ANIMATIONS];

/* a device's position set now: through its group, or (a device in none:
03b's Scarab) its own */
static void device_position_set(long device_index, real position)
{
	struct device_datum *device = device_get(device_index);

	if (device->device.position_group_index != NONE)
		device_set_actual_position(device_index, position);
	else
	{
		device->device.position = position;
		device->device.position_velocity = 0.0f;
		device->device.flags |= FLAG(2); /* (devices.c's position changed bit) */
	}
}

static void device_animations_update(void)
{
	long now = game_time_get();
	short k;

	for (k = 0; k < H2_DEVICE_ANIMATIONS; k++)
	{
		real t;

		if (h2_device_animations[k].device_index == NONE || !h2_device_animations[k].ticks)
			continue;
		if (!object_try_and_get(h2_device_animations[k].device_index) ||
			!TEST_FLAG(_object_mask_device, object_get(h2_device_animations[k].device_index)->object.type))
		{
			h2_device_animations[k].ticks = 0;
			continue;
		}
		t = (real)(now - h2_device_animations[k].start) / (real)h2_device_animations[k].ticks;
		if (t >= 1.0f || t < 0.0f)
		{
			t = 1.0f;
			h2_device_animations[k].ticks = 0;
		}
		/* (at the end exactly the position asked for: scripts test it with
		=, 05b's go1to2 waits for (= (device_get_position elev_under) .103)) */
		device_position_set(h2_device_animations[k].device_index, t >= 1.0f ? h2_device_animations[k].to :
			h2_device_animations[k].from + (h2_device_animations[k].to - h2_device_animations[k].from) * t);
	}
}

static void device_animation_start(long device_index, real to, real seconds)
{
	short k, free_slot = NONE;
	long ticks = (long)(seconds * 30.0f + 0.5f);

	for (k = 0; k < H2_DEVICE_ANIMATIONS; k++)
	{
		if (h2_device_animations[k].ticks && h2_device_animations[k].device_index == device_index)
			break;
		if (free_slot == NONE && !h2_device_animations[k].ticks)
			free_slot = k;
	}
	if (k == H2_DEVICE_ANIMATIONS)
		k = free_slot;
	if (getenv("HALO_H2_DEVICE_LOG"))
		platform_log("h2 scripts: device %lx (%s) from %.3f to %.3f over %ld ticks (slot %d)", device_index,
			tag_get_name(object_get(device_index)->definition_index), (double)device_get_position(device_index), (double)to,
			ticks, (int)k);
	if (ticks <= 0 || k == NONE)
	{
		if (k != NONE)
			h2_device_animations[k].ticks = 0;
		device_position_set(device_index, to);
		return;
	}
	h2_device_animations[k].device_index = device_index;
	h2_device_animations[k].from = device_get_position(device_index);
	h2_device_animations[k].to = to;
	h2_device_animations[k].start = game_time_get();
	h2_device_animations[k].ticks = ticks;
}

/* (devices.c, device_set_desired_position) a device in no position group on
a Halo 2 level moved to the position at its own speed (its powered
position transition time a whole turn): 03a's insertion pods' inserters,
which device_set_position moves; FALSE: not one of those */
boolean h2_scripts_device_move(long device_index, real position)
{
	struct device_datum *device;
	struct device_definition *definition;
	real seconds;

	if (!h2_scripts_globals.count || device_index == NONE || !object_try_and_get(device_index))
		return FALSE;
	device = device_get(device_index);
	if (device->device.position_group_index != NONE)
		return FALSE;
	definition = device_definition_get(device->definition_index);
	seconds = definition->device.powered_position_transition_time;
	if (seconds <= 0.0f)
		seconds = 1.0f;
	object_set_automatic_deactivation(device_index, FALSE);
	device_animation_start(device_index, position, seconds * fabsf(position - device_get_position(device_index)));
	return TRUE;
}

/* device_animate_position (device position seconds acceleration
deceleration interpolate): the device moved to the position over the
seconds (its group, if it has one, told too, so it stays there) */
H2_ALIGN static void device_animate_position_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	if (a[0] != NONE && object_try_and_get(a[0]))
	{
		device_animation_start(a[0], argument_real(a, 1), argument_real(a, 2));
		/* (a device a script animates keeps updating outside the players'
		view and the loaded BSP: 03b's Scarab, teleported to where it waits
		on the next BSP's street, was deactivated and never moved, its
		script waiting on its position for ever - Halo 2's pvs_set_object
		keeps it going) */
		object_set_automatic_deactivation(a[0], FALSE);
		device_set_desired_position(a[0], argument_real(a, 1));
	}
	hs_return(thread_index, 0);
}

/* device_set_position_track (device track interpolation): a new track
starts at its beginning (the position goes back to 0, so a script's
track-by-track walk - set track, animate to 1, wait for 1 - waits on each
in turn); whether the device's graph has the track (halo2-decompiled
devices.cpp 0x107ed0) */
H2_ALIGN static void device_set_position_track_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	boolean played = FALSE;

	if (!a)
		return;

	if (a[0] != NONE && object_try_and_get(a[0]))
	{
		object_set_automatic_deactivation(a[0], FALSE);
		/* (its track, an animation of its graph by name, played at its
		position - the Scarabs' walks, idles and turns - its last track's
		motion kept: h2_cinematics.c. HALO_H2_NO_DEVICE_TRACKS: as before,
		the device standing still) */
		played = !getenv("HALO_H2_NO_DEVICE_TRACKS") &&
			h2_cinematics_device_track(a[0], a[1] ? (char const *)a[1] : "", argument_real(a, 2));
		device_animation_start(a[0], 0.0f, 0.0f);
		/* (the device placed at the new track's start: Halo 2 sets its
		motion's position to 0 before it re-places the device (devices.cpp
		0x107ed0); h2_cinematics_device_track posed its nodes at the last
		track's position - the new track's end - and a device at rest is not
		posed again until it moves: 06b's Key stood at the top of its shaft
		for the riders (its collision there) while the overlay turned and the
		players fell to the shaft's floor) */
		if (played && !getenv("HALO_H2_OLD_TRACK_SWITCH"))
		{
			struct device_datum *device = device_get(a[0]);

			device->device.position = 0.0f;
			device->device.position_velocity = 0.0f;
			object_set_position(a[0], NULL, NULL, NULL);
		}
	}
	hs_return(thread_index, return_boolean(played));
}

/* device_set_overlay_track (device track): the device's overlay by name,
at its position 0, over the track it plays (h2_cinematics.c); whether the
graph has it */
H2_ALIGN static void device_set_overlay_track_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	boolean played = FALSE;

	if (!a)
		return;
	if (a[0] != NONE && object_try_and_get(a[0]) && !getenv("HALO_H2_NO_DEVICE_TRACKS"))
		played = h2_cinematics_device_overlay(a[0], a[1] ? (char const *)a[1] : "");
	hs_return(thread_index, return_boolean(played));
}

/* device_animate_overlay (device position seconds acceleration
deceleration): the overlay's position moved there over the seconds */
H2_ALIGN static void device_animate_overlay_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	if (a[0] != NONE && object_try_and_get(a[0]))
		h2_cinematics_device_overlay_animate(a[0], argument_real(a, 1), argument_real(a, 2));
	hs_return(thread_index, 0);
}

/* (the other functions that only return true) */
H2_ALIGN static void true_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	hs_return(thread_index, return_boolean(TRUE));
}

/* vehicle_unload (object seats), vehicle_load_magic (object seats units):
this engine's, for an object that is a unit; the seats a substring of their
names (Halo 2's seat names: a stand-in vehicle's seats match where its
names do) */
H2_ALIGN static void vehicle_unload_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	short count = 0;

	if (!a)
		return;
	if (a[0] != NONE && object_try_and_get_and_verify_type(a[0], _object_mask_unit))
	{
		/* (a carrier's cargo - a Ghost in a Phantom or a Shadow: h2_ai.c) */
		count = h2_ai_vehicle_unload_cargo(a[0], (char const *)a[1]);
		count += vehicle_scripting_unload(a[0], (char const *)a[1]);
	}
	hs_return(thread_index, return_short(count));
}

/* (vehicle_load_magic's arguments evaluated as their own types: its units a
list, or one object passed as itself - h2_scripts_postprocess -, which a
cast to a list here would have lost) */
H2_ALIGN static void vehicle_load_magic_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count = 0, given;
	long *a = hs_runtime_arguments_evaluate_all(thread_index, 3, &given, initialize);

	if (!a)
		return;
	function_called(function_index, thread_index);
	if (given == 3 && a[0] != NONE && object_try_and_get_and_verify_type(a[0], _object_mask_unit))
	{
		/* (vehicles carried as cargo, h2_ai.c; this engine seats no vehicle
		in a vehicle) */
		count = h2_ai_vehicle_load_cargo(a[0], (char const *)a[1], a[2]);
		if (!count && !object_try_and_get(a[2]))
			count = vehicle_scripting_load_magic(a[0], (char const *)a[1], a[2]);
		/* (one unit passed as itself - 04b's player into the falling Banshee,
		whose banshee_killer kills a player not in banshee_d; the Scarab's
		gunners -: the seat named, else its first free one) */
		else if (!count && object_try_and_get_and_verify_type(a[2], _object_mask_biped))
		{
			short seats[16], seat_count, k;

			if (object_get(a[2])->object.parent_object_index != NONE)
				object_detach(a[2]);
			seat_count = vehicle_scripting_find_available_seats(a[0], (char const *)a[1], NONE, seats, NUMBEROF(seats));
			if (seat_count <= 0)
				seat_count = vehicle_scripting_find_available_seats(a[0], "", NONE, seats, NUMBEROF(seats));
			for (k = 0; k < seat_count && !count; k++)
			{
				if (unit_enter_seat(a[2], a[0], seats[k]))
					count = 1;
			}
			platform_log("h2 scripts: vehicle_load_magic: %s into %s seat '%s': %s (%d free)", tag_get_name(object_get(a[2])->definition_index),
				tag_get_name(object_get(a[0])->definition_index), a[1] ? (char const *)a[1] : "", count ? "seated" : "not seated", (int)seat_count);
		}
	}
	hs_return(thread_index, return_short(count));
}

/* unit_exit_vehicle (unit how): out of its vehicle (this engine's exit,
whatever Halo 2's way: normal, ejection, to the ground) */
H2_ALIGN static void unit_exit_vehicle_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	if (a[0] != NONE)
		unit_scripting_exit_vehicle(a[0]);
	hs_return(thread_index, 0);
}

/* object_hide (object hide): the object (and what it carries) not drawn
while hidden (render_objects.c asks h2_scripts_object_hidden) */
H2_ALIGN static void object_hide_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	long i;

	if (!a)
		return;
	if (a[0] != NONE)
	{
		for (i = 0; i < h2_scripts_globals.hidden_count && h2_scripts_globals.hidden[i] != a[0]; i++)
			;
		if (argument_boolean(a, 1) && i == h2_scripts_globals.hidden_count &&
			h2_scripts_globals.hidden_count < (long)NUMBEROF(h2_scripts_globals.hidden))
		{
			h2_scripts_globals.hidden[h2_scripts_globals.hidden_count++] = a[0];
		}
		else if (!argument_boolean(a, 1) && i < h2_scripts_globals.hidden_count)
			h2_scripts_globals.hidden[i] = h2_scripts_globals.hidden[--h2_scripts_globals.hidden_count];
	}
	hs_return(thread_index, 0);
}

/* Halo 2's cinematic lighting: cinematic_lighting_set_primary_light /
_secondary_light (pitch, yaw, r, g, b), _ambient_light (r, g, b), and
object_uses_cinematic_lighting (object, on) - the objects lit by those
lights, not the level's (h2_scripts_cinematic_lighting, object_lights.c) */
H2_ALIGN static void cinematic_lighting_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);
	int k;

	if (!a || !function)
		return;
	if (!strcmp(function->name, "cinematic_lighting_set_primary_light"))
		for (k = 0; k < 5; k++)
			h2_scripts_globals.cinematic_primary[k] = argument_real(a, k);
	else if (!strcmp(function->name, "cinematic_lighting_set_secondary_light"))
		for (k = 0; k < 5; k++)
			h2_scripts_globals.cinematic_secondary[k] = argument_real(a, k);
	else if (!strcmp(function->name, "cinematic_lighting_set_ambient_light"))
		for (k = 0; k < 3; k++)
			h2_scripts_globals.cinematic_ambient[k] = argument_real(a, k);
	else if (a[0] != NONE)
	{
		long i;

		for (i = 0; i < h2_scripts_globals.cinematic_lit_count && h2_scripts_globals.cinematic_lit[i] != a[0]; i++)
			;
		if (argument_boolean(a, 1) && i == h2_scripts_globals.cinematic_lit_count &&
			h2_scripts_globals.cinematic_lit_count < (long)NUMBEROF(h2_scripts_globals.cinematic_lit))
		{
			h2_scripts_globals.cinematic_lit[h2_scripts_globals.cinematic_lit_count++] = a[0];
		}
		else if (!argument_boolean(a, 1) && i < h2_scripts_globals.cinematic_lit_count)
			h2_scripts_globals.cinematic_lit[i] = h2_scripts_globals.cinematic_lit[--h2_scripts_globals.cinematic_lit_count];
	}
	hs_return(thread_index, 0);
}

/* an argument that names an object: an object name's index made the
object, else the object itself */
static long object_argument(long value)
{
	struct scenario *scenario = global_scenario_get();

	if (value >= 0 && value < scenario->object_names.count)
		return object_index_from_name_index((short)value);
	return value != NONE && object_try_and_get(value) ? value : NONE;
}

/* a string argument: one of the scripts' string constants (a marker's
name), else "" (no marker: the object's origin) */
static char const *script_string(long value)
{
	struct scenario *scenario = global_scenario_get();
	char const *base = (char const *)scenario->hs_string_constants.address;

	if (!value || !base || (char const *)value < base || (char const *)value >= base + scenario->hs_string_constants.size)
		return "";
	return (char const *)value;
}

/* objects_attach (parent marker child marker) / objects_detach (parent
child): the engine's attach (a Halo 2 level's item on a rack left free,
objects.c) and its detach - 03a's insertion pods (the Elite in the pod, the
pod on its inserter, let go at the impact) */
H2_ALIGN static void objects_attach_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct h2_function *function = function_from_index(function_index);
	short count;
	long *a = hs_runtime_arguments_evaluate_all(thread_index, 4, &count, initialize);

	if (!a)
		return;
	function_called(function_index, thread_index);
	if (function && strstr(function->definition.name, "detach"))
	{
		long parent = count > 0 ? object_argument(a[0]) : NONE, child = count > 1 ? object_argument(a[1]) : NONE;

		if (child != NONE && object_get(child)->object.parent_object_index != NONE &&
			(parent == NONE || object_get(child)->object.parent_object_index == parent))
		{
			object_detach(child);
		}
		/* (HALO_H2_ATTACH_LOG: 03b's Scarab pilots, hooked to its seats and
		let go as the player comes near) */
		if (getenv("HALO_H2_ATTACH_LOG"))
			platform_log("h2 scripts: objects_detach %08lx from %08lx at tick %ld", (unsigned long)child, (unsigned long)parent,
				(long)game_time_get());
	}
	else if (count >= 4)
	{
		long parent = object_argument(a[0]), child = object_argument(a[2]);

		if (parent != NONE && child != NONE && parent != child)
			objects_scripting_attach(parent, script_string(a[1]), child, script_string(a[3]));
		if (getenv("HALO_H2_ATTACH_LOG"))
			platform_log("h2 scripts: objects_attach %08lx to %08lx at '%s' at tick %ld: parent %08lx", (unsigned long)child,
				(unsigned long)parent, script_string(a[1]) ? script_string(a[1]) : "", (long)game_time_get(),
				child != NONE ? (unsigned long)object_get(child)->object.parent_object_index : 0UL);
	}
	hs_return(thread_index, 0);
}

static void kill_volumes_forget(void);

/* kill_volume_enable / kill_volume_disable (trigger volume): the bipeds
in an enabled one die, the objects in it go (kill_volumes_update, each
tick); the scenario's kill triggers start enabled (kill_volumes_defaults) */
H2_ALIGN static void kill_volume_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);
	short volume;
	long i;

	if (!a)
		return;
	volume = argument_short(a, 0);
	if (function && volume >= 0 && volume < global_scenario_get()->trigger_volumes.count)
	{
		boolean enable = strstr(function->name, "enable") != NULL;

		for (i = 0; i < h2_scripts_globals.kill_volume_count && h2_scripts_globals.kill_volumes[i] != volume; i++)
			;
		if (enable && i == h2_scripts_globals.kill_volume_count &&
			h2_scripts_globals.kill_volume_count < (long)NUMBEROF(h2_scripts_globals.kill_volumes))
		{
			h2_scripts_globals.kill_volumes[h2_scripts_globals.kill_volume_count++] = volume;
		}
		else if (!enable && i < h2_scripts_globals.kill_volume_count)
			h2_scripts_globals.kill_volumes[i] = h2_scripts_globals.kill_volumes[--h2_scripts_globals.kill_volume_count];
	}
	hs_return(thread_index, 0);
}

/* ---------- the weather (weather_start, weather_stop,
weather_change_intensity: Backwash's rain)

How much of the level's weather (its BSP's weather particle systems,
h2_effects.c) falls: on (0..1, weather_start / weather_stop) times its
intensity (weather_change_intensity), each eased over the seconds the
script gives. A level whose scripts never touch it keeps all of it, as
before. The scripts run on the machine that runs the game; its clients are
sent the state (network_coop.c, h2_scripts_weather_get / _set) */

struct h2_weather_change
{
	real from;
	real to;
	long start_time;
	long ticks;
};

static struct
{
	boolean scripted;
	long changes;
	struct h2_weather_change on;
	struct h2_weather_change intensity;
} h2_weather;

static real weather_change_value(struct h2_weather_change const *change, long now)
{
	if (change->ticks <= 0 || now >= change->start_time + change->ticks)
		return change->to;
	if (now <= change->start_time)
		return change->from;
	return change->from + (change->to - change->from) * (real)(now - change->start_time) / (real)change->ticks;
}

static void weather_change_begin(struct h2_weather_change *change, real to, real seconds, long now)
{
	change->from = weather_change_value(change, now);
	change->to = PIN(to, 0.0f, 1.0f);
	change->start_time = now;
	change->ticks = seconds > 0.0f && seconds < 3600.0f ? (long)(seconds * (real)TICKS_PER_SECOND + 0.5f) : 0;
}

static void weather_reset(void)
{
	memset(&h2_weather, 0, sizeof(h2_weather));
	h2_weather.on.from = h2_weather.on.to = 1.0f;
	h2_weather.intensity.from = h2_weather.intensity.to = 1.0f;
}

/* weather_start (seconds), weather_stop (seconds), weather_change_intensity
(seconds, intensity) */
H2_ALIGN static void weather_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);
	long now = game_time_get();

	if (!a)
		return;
	if (function && !strcmp(function->name, "weather_change_intensity"))
		weather_change_begin(&h2_weather.intensity, argument_real(a, 1), argument_real(a, 0), now);
	else if (function)
		weather_change_begin(&h2_weather.on, !strcmp(function->name, "weather_start") ? 1.0f : 0.0f, argument_real(a, 0), now);
	h2_weather.scripted = TRUE;
	h2_weather.changes++;
	platform_log("h2 scripts: tick %ld %s: weather on %.2f to %.2f, intensity %.2f to %.2f, over %ld and %ld ticks", now,
		function ? function->name : "weather", (double)h2_weather.on.from, (double)h2_weather.on.to,
		(double)h2_weather.intensity.from, (double)h2_weather.intensity.to, h2_weather.on.ticks, h2_weather.intensity.ticks);
	hs_return(thread_index, 0);
}

real h2_scripts_weather_scale(void)
{
	long now;

	if (!h2_weather.scripted || !h2_rules_level())
		return 1.0f;
	now = game_time_get();
	return weather_change_value(&h2_weather.on, now) * weather_change_value(&h2_weather.intensity, now);
}

boolean h2_scripts_weather_get(struct h2_scripts_weather *weather, long *changes)
{
	long now = game_time_get();
	long left;

	if (!h2_weather.scripted || !h2_rules_level())
		return FALSE;
	weather->on = weather_change_value(&h2_weather.on, now);
	weather->on_target = h2_weather.on.to;
	left = h2_weather.on.start_time + h2_weather.on.ticks - now;
	weather->on_ticks = (short)PIN(left, 0, SHORT_MAX);
	weather->intensity = weather_change_value(&h2_weather.intensity, now);
	weather->intensity_target = h2_weather.intensity.to;
	left = h2_weather.intensity.start_time + h2_weather.intensity.ticks - now;
	weather->intensity_ticks = (short)PIN(left, 0, SHORT_MAX);
	*changes = h2_weather.changes;
	return TRUE;
}

void h2_scripts_weather_set(struct h2_scripts_weather const *weather)
{
	long now = game_time_get();

	if (!h2_rules_level())
		return;
	h2_weather.scripted = TRUE;
	h2_weather.on.from = PIN(weather->on, 0.0f, 1.0f);
	h2_weather.on.to = PIN(weather->on_target, 0.0f, 1.0f);
	h2_weather.on.start_time = now;
	h2_weather.on.ticks = PIN(weather->on_ticks, 0, SHORT_MAX);
	h2_weather.intensity.from = PIN(weather->intensity, 0.0f, 1.0f);
	h2_weather.intensity.to = PIN(weather->intensity_target, 0.0f, 1.0f);
	h2_weather.intensity.start_time = now;
	h2_weather.intensity.ticks = PIN(weather->intensity_ticks, 0, SHORT_MAX);
}

/* hud_cinematic_fade (opacity, seconds): this engine's HUD shown or not
(it has no opacity) once the fade is half done */
H2_ALIGN static void hud_cinematic_fade_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	h2_scripts_globals.hud_fade_show = argument_real(a, 0) >= 0.5f;
	h2_scripts_globals.hud_fade_at = game_time_get() + (long)(argument_real(a, 1) * 15.0f);
	h2_scripts_globals.hud_fade_pending = TRUE;
	hs_return(thread_index, 0);
}

/* the scripts' text (h2_script_text.c: Halo 2's own strings, on the HUD):
hud_set_training_text (name), hud_show_training_text (shown),
objectives_show_up_to / _finish_up_to (n), cinematic_subtitle (name,
seconds); one with no string is logged */
H2_ALIGN static void text_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);
	boolean shown = TRUE;

	if (!a || !function)
		return;
	if (!strcmp(function->name, "hud_set_training_text"))
		shown = h2_script_text_training(a[0] ? (char const *)a[0] : "");
	else if (!strcmp(function->name, "hud_show_training_text"))
		h2_script_text_training_show(argument_boolean(a, 0));
	else if (!strcmp(function->name, "cinematic_subtitle"))
		shown = h2_script_text_subtitle(a[0] ? (char const *)a[0] : "", argument_real(a, 1));
	else if (!strcmp(function->name, "cinematic_set_title"))
		shown = h2_script_text_title(argument_short(a, 0));
	else
		shown = h2_script_text_objective(a[0], strstr(function->name, "finish") != NULL);
	if (!shown)
	{
		if (function->definition.parameter_types[0] == _hs_type_string)
			platform_log("h2 scripts: %s %s: no such string", function->name, a[0] ? (char const *)a[0] : "");
		else
			platform_log("h2 scripts: %s %ld: no such objective", function->name, a[0]);
	}
	hs_return(thread_index, 0);
}

/* a function of this engine's that takes objects, called only with
objects that are there: Halo 2's take none (a stand-in's NONE, a deleted
object) as nothing to do; this engine's assert on them. Its arguments are
evaluated here, as its own evaluator lays them out, and it is then called
on them (hs_runtime_frame_rewind) */
H2_ALIGN static void engine_guard_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct h2_function *function = function_from_index(function_index);
	long *a = function ? hs_macro_function_evaluate(function_index, thread_index, initialize) : NULL;
	short p;

	if (!a)
		return;
	for (p = 0; p < function->definition.parameter_count; p++)
	{
		if (HS_TYPE_IS_OBJECT(function->definition.parameter_types[p]) && (a[p] == NONE || !object_try_and_get(a[p])))
		{
			function->calls++;
			hs_return(thread_index, hs_runtime_type_default(function->definition.return_type));
			return;
		}
	}
	hs_runtime_frame_rewind(thread_index);
	hs_function_get(function->engine_index)->evaluate(function->engine_index, thread_index, FALSE);
}

/* a script's effect (Halo 2's handle) as this engine's effect tag, NONE */
static long effect_made(long handle)
{
	long i;

	for (i = 0; handle != NONE && i < h2_scripts_globals.effect_count; i++)
	{
		if (h2_scripts_globals.effects[i].handle == (unsigned long)handle)
			return h2_scripts_globals.effects[i].tag_index;
	}
	return NONE;
}

/* effect_new (effect flag), effect_new_on_object_marker (effect object
marker): this engine's, with the effect h2_effects.c made of Halo 2's
(those it did not make are counted) */
H2_ALIGN static void effect_new_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);
	long effect;

	if (!a)
		return;
	effect = effect_made(a[0]);
	if (effect == NONE)
		h2_scripts_globals.effects_missing++;
	else if (function && function->definition.parameter_count == 3)
	{
		if (a[1] != NONE && object_try_and_get(a[1]))
		{
			hs_effect_new_from_object_marker(effect, a[1], a[2] ? (char const *)a[2] : "");
			h2_scripts_globals.effects_played++;
		}
	}
	else if (argument_short(a, 1) >= 0 && argument_short(a, 1) < global_scenario_get()->cutscene_flags.count)
	{
		hs_effect_new(effect, argument_short(a, 1));
		h2_scripts_globals.effects_played++;
	}
	hs_return(thread_index, 0);
}

/* (damage_object <damage> <object>): the damage effect dealt to the object -
Halo 2's damage at full scale, no one's: its amount (lower bound at no scale,
the upper bounds' range at full), its multipliers on the object's materials
(h2_damage.c), category, side effect, flags and stun, against the object's
shield and body as any damage is (object_cause_damage). This engine's damage
reads a damage effect tag: a jpt! of the level's lends its definition for the
call, written with Halo 2's values and put back after. The uses: 06a's door
absorbers (60 plasma against a 50 shield and a body of 1: broken), 07a's flyby
(no damage: its camera shake is not played here). The real engine's Hunters
live through damage_object of the sniper round (03a, the audit's runner) */
static void h2_scripts_write_real(void *at, float value)
{
	memcpy(at, &value, sizeof(value));
}

static float h2_scripts_cache_real(struct h2_cache *cache, unsigned long address)
{
	unsigned long bits = h2_cache_read32(cache, address);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value == value ? value : 0.0f;
}

H2_ALIGN static void damage_object_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_any(function_index, thread_index, 2, initialize);
	struct h2_cache *cache = h2_scripts_cache();
	struct h2_cache_tag tag;
	static long lender = NONE - 1;

	if (!a)
		return;
	if (lender == NONE - 1 || (lender != NONE && !tag_get('jpt!', lender)))
	{
		struct tag_iterator iterator;

		tag_iterator_new(&iterator, 'jpt!');
		lender = tag_iterator_next(&iterator);
	}
	if (a[0] != NONE && a[1] != NONE && object_try_and_get(a[1]) && cache && lender != NONE &&
		h2_cache_tag_get(cache, h2_cache_tag_from_handle(cache, (unsigned long)a[0]), &tag))
	{
		struct damage_effect_definition *definition = (struct damage_effect_definition *)tag_get('jpt!', lender);
		struct damage_effect_definition kept = *definition;
		struct damage_data damage;
		long m;
		short side_effect = (short)(h2_cache_read32(cache, tag.address + 0x10) & 0xFFFF);

		memset(definition, 0, sizeof(*definition));
		for (m = 0; m < MAXIMUM_NUMBER_OF_MATERIAL_TYPES; m++)
			definition->damage.material_modifiers[m] = 1.0f;
		h2_damage_effect_write(cache, h2_cache_tag_from_handle(cache, (unsigned long)a[0]), definition, h2_scripts_write_real);
		/* (Halo 2's category - its enum this engine's -, its first ten flags, its side effect when "lethal to the
		unsuspecting" (the others harmless here), its stun) */
		definition->damage.category = (short)(h2_cache_read32(cache, tag.address + 0x10) >> 16);
		if (definition->damage.category < 0 || definition->damage.category >= 13)
			definition->damage.category = 0;
		definition->damage.flags = h2_cache_read32(cache, tag.address + 0x14) & 0x3FF;
		definition->damage.side_effect = side_effect == 2 ? 2 : 0;
		h2_scripts_write_real(&definition->damage.stun, h2_scripts_cache_real(cache, tag.address + 0x34));
		h2_scripts_write_real(&definition->damage.maximum_stun, h2_scripts_cache_real(cache, tag.address + 0x38));
		h2_scripts_write_real(&definition->damage.stun_time, h2_scripts_cache_real(cache, tag.address + 0x3C));
		damage_data_new(&damage, lender);
		object_get_origin(a[1], &damage.epicenter);
		damage.origin = damage.epicenter;
		damage.direction.i = damage.direction.j = 0.0f, damage.direction.k = -1.0f;
		{
			struct object_datum *object = object_get(a[1]);
			real shield = object->object.shield_vitality, body = object->object.body_vitality;

			object_cause_damage(&damage, a[1], NONE, NONE, NONE, NULL);
			object = object_try_and_get(a[1]);
			platform_log("h2 scripts: damage_object %s on %lx: shield %.2f -> %.2f, body %.2f -> %.2f%s",
				h2_cache_tag_name(cache, h2_cache_tag_from_handle(cache, (unsigned long)a[0])), (unsigned long)a[1], (double)shield,
				object ? (double)object->object.shield_vitality : -1.0, (double)body, object ? (double)object->object.body_vitality : -1.0,
				object && TEST_FLAG(object->object.damage_flags, _object_dead_bit) ? " (dead)" : "");
		}
		*definition = kept;
	}
	hs_return(thread_index, 0);
}

/* (weapon_hold_trigger <weapon> <long trigger> <boolean>): a placed weapon's
trigger held by the script, hs_doc "turns the trigger for a weapon on/off":
03a's and 07a's scripted gunfire (43 sites, trigger 0 all). Held until
released or the weapon goes */
enum { H2_HELD_TRIGGERS = 16 };
static long held_triggers[H2_HELD_TRIGGERS];
static short held_trigger_count;

H2_ALIGN static void weapon_hold_trigger_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_any(function_index, thread_index, 3, initialize);
	short k;

	if (!a)
		return;
	for (k = 0; k < held_trigger_count && held_triggers[k] != a[0]; k++)
		;
	if (((union value *)&a[2])->b && a[0] != NONE && k == held_trigger_count && held_trigger_count < H2_HELD_TRIGGERS)
		held_triggers[held_trigger_count++] = a[0];
	else if (!((union value *)&a[2])->b && k < held_trigger_count)
		held_triggers[k] = held_triggers[--held_trigger_count];
	hs_return(thread_index, 0);
}

static void held_triggers_update(void)
{
	void weapon_owner_update(long weapon_index, word control_flags, real primary_trigger);
	short k;

	for (k = 0; k < held_trigger_count; k++)
	{
		struct object_datum *weapon = object_try_and_get(held_triggers[k]);

		if (!weapon || weapon->object.type != _object_type_weapon)
			held_triggers[k--] = held_triggers[--held_trigger_count];
		else
		{
			if (HALO_ENV_ONCE("HALO_H2_CINEMATIC_LOG") && game_time_get() % 30 == 0)
				platform_log("h2 cine: tick %ld weapon held %08lx %s at %.1f %.1f %.1f parent %08lx", (long)game_time_get(), (unsigned long)held_triggers[k],
					tag_get_name(weapon->definition_index), weapon->object.position.x, weapon->object.position.y, weapon->object.position.z,
					(unsigned long)weapon->object.parent_object_index);
			/* (a script's trigger counts no rounds: 03a's Scarab turret, a
			stand-in magazine empty, fired none of its tracers) */
			if (weapon_get(held_triggers[k])->weapon.magazines[0].rounds_loaded < 8)
				weapon_get(held_triggers[k])->weapon.magazines[0].rounds_loaded = 8;
			/* (a weapon at rest is not updated: its trigger would never fire) */
			if (weapon->object.parent_object_index == NONE)
				object_activate(held_triggers[k]);
			weapon_owner_update(held_triggers[k], FLAG(_weapon_control_primary_trigger_bit), 1.0f);
			/* (on a scenery - the Scarab, which is not updated, nor are its
			children -: its update here) */
			if (weapon->object.parent_object_index != NONE &&
				!object_try_and_get_and_verify_type(weapon->object.parent_object_index, _object_mask_unit))
			{
				boolean weapon_update(long weapon_index);

				weapon_update(held_triggers[k]);
			}
		}
	}
}

/* the loaded damage effect of a Halo 2 damage tag (NONE): the one made of it
for the scripts (h2_vehicles.c), else one by its leaf name - this engine holds
a weapon's by its chain, not by the tag */
static long damage_tag_loaded(long handle)
{
	struct h2_cache *cache = h2_scripts_cache();
	long tag = cache && handle != NONE ? h2_cache_tag_from_handle(cache, (unsigned long)handle) : NONE;
	char const *name = tag != NONE ? h2_cache_tag_name(cache, tag) : NULL;
	char const *leaf;
	size_t length;
	struct tag_iterator iterator;
	long tag_index;

	if (!name)
		return NONE;
	/* (the level's own made of it - a script's damage effect, h2_vehicles.c:
	"h2\\" and its Halo 2 name) */
	{
		char made[300];

		snprintf(made, sizeof(made), "h2\\%s", name);
		tag_iterator_new(&iterator, 'jpt!');
		while ((tag_index = tag_iterator_next(&iterator)) != NONE)
		{
			if (!strcmp(tag_get_name(tag_index), made))
				return tag_index;
		}
	}
	leaf = strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name;
	length = strcspn(leaf, ".");
	tag_iterator_new(&iterator, 'jpt!');
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		char const *candidate = tag_get_name(tag_index);
		char const *candidate_leaf = strrchr(candidate, '\\') ? strrchr(candidate, '\\') + 1 : candidate;

		if (!strncmp(candidate_leaf, leaf, length) && !candidate_leaf[length])
			return tag_index;
	}
	/* (a grenade's explosion - 01b's scripted blasts, damage_new
	frag_grenade_explosion at a flag -: the kit's grenade explosion, which
	h2_weapons.c gives Halo 2's grenade's values) */
	{
		static char const *const grenades[][2] =
		{
			{ "frag_grenade_explosion", "weapons\\frag grenade\\explosion" },
			{ "plasma_grenade_explosion", "weapons\\plasma grenade\\explosion" },
		};
		int g;

		for (g = 0; g < (int)(sizeof(grenades) / sizeof(grenades[0])); g++)
		{
			if (strncmp(leaf, grenades[g][0], length) || grenades[g][0][length])
				continue;
			tag_iterator_new(&iterator, 'jpt!');
			while ((tag_index = tag_iterator_next(&iterator)) != NONE)
			{
				if (!strcmp(tag_get_name(tag_index), grenades[g][1]))
					return tag_index;
			}
		}
	}
	platform_log("h2 scripts: no loaded damage effect %s", name);
	return NONE;
}

/* (damage_new <damage> <cutscene_flag>): hs_doc "causes the specified damage at
the specified flag" (01b's grenade blasts); none loaded, nothing hurt */
H2_ALIGN static void damage_new_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_any(function_index, thread_index, 2, initialize);
	long found;

	if (!a)
		return;
	found = damage_tag_loaded(a[0]);
	if (found != NONE && argument_short(a, 1) >= 0 && argument_short(a, 1) < global_scenario_get()->cutscene_flags.count)
	{
		unsigned char const *flag = (unsigned char const *)global_scenario_get()->cutscene_flags.address +
			CE_CUTSCENE_FLAG_SIZE * argument_short(a, 1);
		struct damage_data damage;

		damage_data_new(&damage, found);
		memcpy(&damage.origin, flag + 0x24, sizeof(damage.origin));
		damage.epicenter = damage.origin;
		scenario_location_from_point(&damage.location, &damage.origin);
		area_of_effect_cause_damage(&damage, NONE);
		platform_log("h2 scripts: damage_new %s at flag %d (%.1f %.1f %.1f)", tag_get_name(found), (int)argument_short(a, 1),
			damage.origin.x, damage.origin.y, damage.origin.z);
	}
	hs_return(thread_index, 0);
}

/* (damage_players <damage>): hs_doc "damages all players with the given damage
effect" (01b's flybys, shakes); none loaded, nothing */
H2_ALIGN static void damage_players_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_any(function_index, thread_index, 1, initialize);
	long found, player_index;

	if (!a)
		return;
	found = damage_tag_loaded(a[0]);
	if (found != NONE)
		platform_log("h2 scripts: damage_players %s", tag_get_name(found));
	for (player_index = found != NONE ? data_next_index(player_data, NONE) : NONE; player_index != NONE;
		player_index = data_next_index(player_data, player_index))
	{
		long unit_index = player_get(player_index)->unit_index;

		if (unit_index != NONE && object_try_and_get(unit_index))
		{
			struct damage_data damage;

			damage_data_new(&damage, found);
			object_get_origin(unit_index, &damage.origin);
			damage.epicenter = damage.origin;
			object_cause_damage(&damage, unit_index, NONE, NONE, NONE, NULL);
		}
	}
	hs_return(thread_index, 0);
}

/* (ice_cream_flavor_stock <long>), (ice_cream_flavor_available <long>): a skull
found; whether one is on (h2_skulls.c) */
H2_ALIGN static void ice_cream_flavor_stock_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	h2_skull_found((short)a[0]);
	hs_return(thread_index, 0);
}

H2_ALIGN static void ice_cream_flavor_available_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	hs_return(thread_index, h2_skull_available((short)a[0]));
}

/* (h2_skulls.c) the globals' "ice cream" sound, when the level holds it */
void h2_scripts_skull_sound(void)
{
	long tag_index = tag_loaded('snd!', "sound\\visual_effects\\ice_cream_flavor\\ice_cream_flavor2");

	if (tag_index != NONE)
		scripted_sound_new(tag_index, NONE, 1.0f);
	else
		platform_log("h2 skulls: the ice cream sound is not in this level's tags");
}

/* (biped_ragdoll <unit>): hs_doc "given a dead biped, turns on ragdoll" */
H2_ALIGN static void biped_ragdoll_evaluate(short function_index, long thread_index, boolean initialize)
{
	void ragdolls_unit_died(long unit_index);
	long *a = arguments_any(function_index, thread_index, 1, initialize);
	struct object_datum *object;

	if (!a)
		return;
	object = a[0] != NONE ? object_try_and_get(a[0]) : NULL;
	if (object && object->object.type == _object_type_biped && TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		ragdolls_unit_died(a[0]);
	hs_return(thread_index, 0);
}

/* (object_dynamic_simulation_disable <object> <boolean>): hs_doc "makes it
fixed". A device is fixed already (46 sites, gondolas and plugs); anything else
stopped where it is */
H2_ALIGN static void object_dynamic_simulation_disable_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_any(function_index, thread_index, 2, initialize);
	struct object_datum *object;

	if (!a)
		return;
	object = a[0] != NONE ? object_try_and_get(a[0]) : NULL;
	if (object && ((union value *)&a[1])->b && !TEST_FLAG(_object_mask_device, object->object.type))
	{
		memset(&object->object.translational_velocity, 0, sizeof(object->object.translational_velocity));
		memset(&object->object.angular_velocity, 0, sizeof(object->object.angular_velocity));
		SET_FLAG(object->object.flags, _object_at_rest_bit, TRUE);
	}
	hs_return(thread_index, 0);
}

/* (unit_only_takes_damage_from_players_team <unit> <boolean>): hs_doc "used for
the tartarus boss fight", 08b: damage from anyone but the players' team passes
him by */
enum { H2_PLAYER_TEAM_ONLY_UNITS = 4 };
static long player_team_only_units[H2_PLAYER_TEAM_ONLY_UNITS];
static short player_team_only_count;

H2_ALIGN static void unit_only_takes_damage_from_players_team_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_any(function_index, thread_index, 2, initialize);
	short k;

	if (!a)
		return;
	for (k = 0; k < player_team_only_count && player_team_only_units[k] != a[0]; k++)
		;
	if (((union value *)&a[1])->b && a[0] != NONE && k == player_team_only_count && k < H2_PLAYER_TEAM_ONLY_UNITS)
		player_team_only_units[player_team_only_count++] = a[0];
	else if (!((union value *)&a[1])->b && k < player_team_only_count)
		player_team_only_units[k] = player_team_only_units[--player_team_only_count];
	hs_return(thread_index, 0);
}

/* (damage.c) the blow does not reach this unit */
boolean h2_scripts_damage_passes_by(long object_index, short owner_team_index)
{
	short k;

	for (k = 0; k < player_team_only_count; k++)
	{
		if (player_team_only_units[k] == object_index)
		{
			long player_index = local_player_get_player_index(0);
			short player_team = player_index != NONE ? (short)player_get(player_index)->team_index : NONE;

			/* (the players' team and its allies: 08b's fight is Johnson's
			beam rifle taking Tartarus's shield down - his shots, the human
			team's (ai_allegiance player human), passed him by and the shield
			never fell, the fight's loop never went on: a softlock) */
			boolean passes = player_index != NONE && owner_team_index != player_team &&
				game_team_is_enemy(player_team, owner_team_index);

			if (HALO_ENV_ONCE("HALO_H2_DAMAGE_LOG"))
				platform_log("h2 damage: %08lx (players' team only) hit by team %d, the players' %d: %s", (unsigned long)object_index,
					(int)owner_team_index, (int)player_team, passes ? "passes by" : "taken");
			return passes;
		}
	}
	return FALSE;
}

/* (map_reset): starts the map from the beginning */
H2_ALIGN static void map_reset_evaluate(short function_index, long thread_index, boolean initialize)
{
	void main_reset_map(void);

	if (!arguments_get(function_index, thread_index, initialize))
		return;
	main_reset_map();
	hs_return(thread_index, 0);
}

/* (object_at_marker <object> <marker>): the object attached at that marker of
the parent (hs_doc): nothing here records which marker a child took, so the
child nearest the marker, within 1.5 wu - 03b's and 08b's Scarab turrets, for
vehicle_load_magic */
H2_ALIGN static void object_at_marker_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	long result = NONE;

	if (!a)
		return;
	if (a[0] != NONE && object_try_and_get(a[0]) && a[1])
	{
		struct object_marker marker;

		if (object_get_marker_by_name(a[0], script_string(a[1]), &marker, 1) > 0)
		{
			long child = object_get(a[0])->object.first_child_object_index;
			real best = 1.5f * 1.5f;

			for (; child != NONE; child = object_get(child)->object.next_object_index)
			{
				real_point3d origin;
				real d;

				object_get_origin(child, &origin);
				d = distance_squared3d(&origin, &marker.matrix.position);
				if (d < best)
					best = d, result = child;
			}
		}
	}
	hs_return(thread_index, result);
}

/* (texture_camera_set_object_marker <object> <marker> <field of view>),
(texture_camera_off): the texture camera the texture camera screens show
(h2_cinematics.c) */
H2_ALIGN static void texture_camera_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);

	if (!a || !function)
		return;
	if (function->definition.parameter_count >= 3)
		h2_texture_camera_set(a[0] != NONE && object_try_and_get(a[0]) ? a[0] : NONE, script_string(a[1]), argument_real(a, 2));
	else
		h2_texture_camera_off();
	hs_return(thread_index, 0);
}

/* (player_disable_movement <boolean>): the look stick works, nothing else
(01a's training holds the player until each step) */
H2_ALIGN static void player_disable_movement_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	player_control_set_movement_disabled(argument_boolean(a, 0));
	hs_return(thread_index, 0);
}

/* player_action_test_melee, _vision_trigger (the Arbiter's stealth), _cancel
(Halo 2's back button): since the last player_action_test_reset */
H2_ALIGN static void player_action_test_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);
	boolean tested;

	if (!a || !function)
		return;
	tested = strstr(function->name, "melee") ? player_control_action_test_melee() :
		strstr(function->name, "vision") ? player_control_action_test_vision_trigger() : player_control_action_test_back();
	hs_return(thread_index, return_boolean(tested));
}

/* (player_flashlight_on): any player's */
H2_ALIGN static void player_flashlight_on_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	short i;
	boolean on = FALSE;

	if (!a)
		return;
	for (i = 0; i < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS && !on; i++)
	{
		long player_index = local_player_get_player_index(i);

		if (player_index != NONE && player_get(player_index)->unit_index != NONE)
			on = unit_get_current_flashlight_state(player_get(player_index)->unit_index);
	}
	hs_return(thread_index, return_boolean(on));
}

/* (physics_set_gravity <real>): relative to Halo's standard gravity - 01b's
and 04b's airless and reversed gravity scenes */
H2_ALIGN static void physics_set_gravity_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	if (h2_scripts_globals.standard_gravity == 0.0f)
		h2_scripts_globals.standard_gravity = global_gravity;
	global_gravity = h2_scripts_globals.standard_gravity * argument_real(a, 0);
	hs_return(thread_index, 0);
}

/* (device_closes_automatically_set <device> <boolean>): FALSE, a door stays
open once opened (06b's, 07a's council exit, ...) */
H2_ALIGN static void device_closes_automatically_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	if (a[0] != NONE && machine_try_and_get(a[0]))
		SET_FLAG(machine_get(a[0])->machine.flags, _machine_does_not_close_automatically_bit, !argument_boolean(a, 1));
	hs_return(thread_index, 0);
}

/* (cinematic_clone_players_weapon <object> <marker> <marker>): the first
player's weapon in hand, cloned and attached to the object's marker - the
cutscene's Chief or Arbiter holds what the player held */
H2_ALIGN static void cinematic_clone_players_weapon_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	long player_index = local_player_get_player_index(0);
	long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;

	if (!a)
		return;
	if (a[0] != NONE && object_try_and_get(a[0]) && unit_index != NONE && object_try_and_get(unit_index))
	{
		long weapon = unit_inventory_get_weapon(unit_index, unit_get(unit_index)->unit.current_weapon_index);

		if (weapon != NONE && object_try_and_get(weapon))
		{
			struct object_placement_data placement;
			long clone;

			object_placement_data_new(&placement, object_get(weapon)->definition_index, NONE);
			object_get_origin(a[0], &placement.position);
			clone = object_new(&placement);
			if (clone != NONE)
				objects_scripting_attach(a[0], script_string(a[1]), clone, script_string(a[2]));
		}
	}
	hs_return(thread_index, 0);
}

/* ---- cinematics (h2_cinematics.c) */

/* camera_control (on): this engine's scripted camera; off stops the
camera's animation */
H2_ALIGN static void camera_control_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	if (!argument_boolean(a, 0))
		h2_cinematics_camera_stop();
	director_script_camera(argument_boolean(a, 0));
	hs_return(thread_index, 0);
}

/* camera_set (camera point, ticks): this engine's, the animation stopped */
H2_ALIGN static void camera_set_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	h2_cinematics_camera_stop();
	if (argument_short(a, 0) >= 0 && argument_short(a, 0) < global_scenario_get()->cutscene_camera_points.count)
		scripted_camera_set(argument_short(a, 0), (word)argument_short(a, 1), NONE);
	hs_return(thread_index, 0);
}

/* camera_set_animation_relative (graph, animation, unit, flag) */
H2_ALIGN static void camera_set_animation_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	h2_cinematics_camera_animation(a[0], a[1] ? (char const *)a[1] : "", a[2], argument_short(a, 3));
	hs_return(thread_index, 0);
}

/* camera_set_field_of_view (degrees, ticks) */
H2_ALIGN static void camera_field_of_view_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	h2_cinematics_field_of_view(argument_real(a, 0), argument_short(a, 1));
	hs_return(thread_index, 0);
}

/* cinematic_show_letterbox_immediate (boolean): the bars at once
(halo2-decompiled unknown_29f5b0.cpp 0x2a9810) */
H2_ALIGN static void letterbox_immediate_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	cinematic_show_letterbox_immediate(argument_boolean(a, 0));
	platform_log("h2 scripts: letterbox %s at once (tick %ld)", argument_boolean(a, 0) ? "shown" : "gone", game_time_get());
	hs_return(thread_index, 0);
}

/* camera_time: the camera animation's ticks left, else this engine's */
H2_ALIGN static void camera_time_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	long ticks;

	if (!a)
		return;
	ticks = h2_cinematics_camera_time();
	hs_return(thread_index, return_short((short)(ticks != NONE ? ticks : scripted_camera_time())));
}

/* custom_animation (unit graph animation interpolate),
custom_animation_relative (... object), their _loop forms,
scenery_animation_start (scenery graph animation), _relative (... object),
_loop: the object played as the animation's root moves */
H2_ALIGN static void object_animation_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);
	long anchor = NONE;
	boolean loop, scenery, played;

	if (!a || !function)
		return;
	loop = strstr(function->name, "_loop") != NULL;
	scenery = !strncmp(function->name, "scenery_", 8);
	if (strstr(function->name, "_relative"))
		anchor = a[scenery ? 3 : 4];
	/* (a relative one whose anchor object is not there yet - 01b's outro
	scene 07, asked a tick after its switch_bsp, which this engine makes
	between ticks: the BSP's anchor_outro2 not made yet -: the camera's
	anchor flag, where Halo 2's cinematic anchors stand) */
	played = h2_cinematics_object_animation(a[0], a[1], a[2] ? (char const *)a[2] : "", anchor,
		strstr(function->name, "_relative") && anchor == NONE ? H2_CINEMATICS_CAMERA_ANCHOR : NONE, loop) != 0;
	/* (custom_animation gives whether it started) */
	hs_return(thread_index, scenery ? 0 : return_boolean(played));
}

/* unit_get_custom_animation_time, scenery_get_animation_time: its ticks
left (0: none plays) */
H2_ALIGN static void object_animation_time_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	long ticks;

	if (!a)
		return;
	ticks = h2_cinematics_object_time(a[0]);
	hs_return(thread_index, return_short((short)(ticks == NONE ? 0 : ticks)));
}

/* unit_stop_custom_animation */
H2_ALIGN static void object_animation_stop_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	h2_cinematics_object_stop(a[0]);
	hs_return(thread_index, 0);
}

/* ---- navigation points: this engine's HUD nav points (a navpoint is its
HUD globals' arrow, found by name as the scripts are made ready; a Halo 2
team is this engine's of the same number) */

/* activate_team_nav_point_object, breadcrumbs_activate_team_nav_point_object
(navpoint team object offset) and their _flag forms (... flag offset) */
H2_ALIGN static void nav_point_activate_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);

	if (!a || !function)
		return;
	if (function->definition.parameter_types[2] == _hs_type_object)
	{
		if (a[2] != NONE && object_try_and_get(a[2]))
			hud_activate_team_nav_point_with_object(argument_short(a, 0), argument_short(a, 1), a[2], argument_real(a, 3));
	}
	else if (argument_short(a, 2) >= 0 && argument_short(a, 2) < global_scenario_get()->cutscene_flags.count)
		hud_activate_team_nav_point_with_flag(argument_short(a, 0), argument_short(a, 1), argument_short(a, 2), argument_real(a, 3));
	hs_return(thread_index, 0);
}

/* deactivate_team_nav_point_object / _flag, and the breadcrumbs_ forms */
H2_ALIGN static void nav_point_deactivate_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);

	if (!a || !function)
		return;
	if (function->definition.parameter_types[1] == _hs_type_object)
		hud_deactivate_team_nav_point_with_object(argument_short(a, 0), a[1]);
	else if (argument_short(a, 1) >= 0 && argument_short(a, 1) < global_scenario_get()->cutscene_flags.count)
		hud_deactivate_team_nav_point_with_flag(argument_short(a, 0), argument_short(a, 1));
	hs_return(thread_index, 0);
}

/* ---- objects */

/* object_set_velocity (object forward) / (object forward left up): its
velocity, in its own axes */
H2_ALIGN static void object_set_velocity_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);
	struct object_datum *object;

	if (!a || !function)
		return;
	if ((object = a[0] != NONE ? object_try_and_get(a[0]) : NULL) != NULL)
	{
		real f = argument_real(a, 1), l = function->definition.parameter_count > 2 ? argument_real(a, 2) : 0.0f;
		real u = function->definition.parameter_count > 3 ? argument_real(a, 3) : 0.0f;
		real_vector3d left;

		left.i = object->object.up.j * object->object.forward.k - object->object.up.k * object->object.forward.j;
		left.j = object->object.up.k * object->object.forward.i - object->object.up.i * object->object.forward.k;
		left.k = object->object.up.i * object->object.forward.j - object->object.up.j * object->object.forward.i;
		/* (Halo 2's velocities are per second - MCC's sets it as given, a Warthog's
		11 its top speed -, this engine's per tick) */
		f /= 30.0f, l /= 30.0f, u /= 30.0f;
		object->object.translational_velocity.i = f * object->object.forward.i + l * left.i + u * object->object.up.i;
		object->object.translational_velocity.j = f * object->object.forward.j + l * left.j + u * object->object.up.j;
		object->object.translational_velocity.k = f * object->object.forward.k + l * left.k + u * object->object.up.k;
	}
	hs_return(thread_index, 0);
}

/* object_set_deleted_when_deactivated (object) */
H2_ALIGN static void object_deleted_when_deactivated_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct object_datum *object;

	if (!a)
		return;
	if ((object = a[0] != NONE ? object_try_and_get(a[0]) : NULL) != NULL)
		SET_FLAG(object->object.flags, _object_deleted_when_deactivated_bit, TRUE);
	hs_return(thread_index, 0);
}

/* object_set_shield_stun_infinite (object): its shield never recharges */
H2_ALIGN static void object_shield_stun_infinite_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct object_datum *object;

	if (!a)
		return;
	if ((object = a[0] != NONE ? object_try_and_get(a[0]) : NULL) != NULL)
		object->object.shield_stun_ticks = SHORT_MAX;
	hs_return(thread_index, 0);
}

/* object_destroy_type_mask (mask): every object of the mask's types (Halo
2's type bits: this engine's for bipeds to light fixtures) deleted, the
players' units and what they carry kept */
H2_ALIGN static void object_destroy_type_mask_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	{
		struct object_iterator iterator;
		struct object_datum *object;
		unsigned long mask = (unsigned long)a[0] & 0x3FF;

		object_iterator_new(&iterator, mask, 0);
		while (mask && (object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
		{
			long top = iterator.index, depth;

			for (depth = 0; depth < 4 && object_try_and_get(top) && object_get(top)->object.parent_object_index != NONE; depth++)
				top = object_get(top)->object.parent_object_index;
			if (!object_try_and_get_and_verify_type(top, _object_mask_unit) || player_index_from_unit_index(top) == NONE)
				object_delete(iterator.index);
		}
	}
	hs_return(thread_index, 0);
}

/* unit_set_active_camo (unit on seconds), cheat_active_camouflage_by_player
(player on): this engine's active camouflage */
static void unit_camouflage(long unit_index, boolean on)
{
	struct unit_datum *unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;

	if (!unit)
		return;
	unit->unit.active_camouflage = on ? 1.0f : 0.0f;
	SET_FLAG(unit->unit.flags, _unit_active_camouflaged_bit, on);
}

H2_ALIGN static void unit_set_active_camo_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	unit_camouflage(a[0], argument_boolean(a, 1));
	hs_return(thread_index, 0);
}

void rasterizer_screen_effect_start(boolean clear);
void rasterizer_screen_effect_stop(void);

/* cinematic_screen_effect_start (clear), _stop: the engine's, and Halo 2's
depth of field ended; _set_depth_of_field, _set_crossfade, _set_crossfade2:
h2_cinematics.c, drawn on the PC (h2_bloom.c) */
H2_ALIGN static void screen_effect_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);

	if (!a || !function)
		return;
	if (!strcmp(function->name, "cinematic_screen_effect_start"))
	{
		rasterizer_screen_effect_start(argument_boolean(a, 0));
		if (argument_boolean(a, 0))
			h2_cinematics_depth_of_field_clear();
	}
	else if (!strcmp(function->name, "cinematic_screen_effect_stop"))
	{
		rasterizer_screen_effect_stop();
		h2_cinematics_depth_of_field_clear();
	}
	else if (!strcmp(function->name, "cinematic_screen_effect_set_depth_of_field"))
		h2_cinematics_depth_of_field_set(argument_real(a, 0), argument_real(a, 1), argument_real(a, 2), argument_real(a, 3), argument_real(a, 4),
			argument_real(a, 5), argument_real(a, 6));
	else if (!strcmp(function->name, "cinematic_screen_effect_set_crossfade"))
		h2_cinematics_crossfade_start(argument_real(a, 0), 1.0f);
	else
		h2_cinematics_crossfade_start(argument_real(a, 0), argument_real(a, 1));
	hs_return(thread_index, 0);
}

/* object_cinematic_lod (object on): the object at its highest detail for the
level's cutscenes (hs_doc; this engine's own flag for it) */
H2_ALIGN static void object_cinematic_lod_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_any(function_index, thread_index, 2, initialize);
	struct object_datum *object;

	if (!a)
		return;
	if (a[0] != NONE && (object = object_try_and_get(a[0])) != NULL)
		SET_FLAG(object->object.flags, _object_movie_star_bit, ((union value *)&a[1])->b);
	hs_return(thread_index, 0);
}

/* pvs_set_object (object), pvs_clear: hs_doc, "the special place that activates
everything it sees", the engine's own (objects.c). 03a's intro has it on a
Pelican: the Warthogs on the plaza below, out of the player's view, stayed
inactive and their drivers dormant */
H2_ALIGN static void pvs_set_object_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_any(function_index, thread_index, 1, initialize);

	if (!a)
		return;
	object_pvs_set_object(a[0] != NONE && object_try_and_get(a[0]) ? a[0] : NONE);
	hs_return(thread_index, 0);
}

H2_ALIGN static void pvs_clear_evaluate(short function_index, long thread_index, boolean initialize)
{
	(void)function_index, (void)initialize;
	object_pvs_clear();
	hs_return(thread_index, 0);
}

/* rasterizer_bloom_override (on), _reset (on), _threshold, _brightness
(value), the rest logged: h2_bloom.c's override of the frame's bloom (MCC
halo2.dll 0x18096fbf0: each value set replaces the frame's, after the sky's
and its curve, unset -1). 01b's, 00a's and most scenes' lights rest on it */
H2_ALIGN static void bloom_override_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);

	if (!a || !function)
		return;
	if (!strcmp(function->name, "rasterizer_bloom_override"))
		h2_bloom_override_enable(argument_boolean(a, 0));
	else if (!strcmp(function->name, "rasterizer_bloom_override_reset"))
	{
		if (argument_boolean(a, 0))
			h2_bloom_override_reset();
	}
	else if (!strcmp(function->name, "rasterizer_bloom_override_threshold"))
		h2_bloom_override_threshold(argument_real(a, 0));
	else if (!strcmp(function->name, "rasterizer_bloom_override_brightness"))
		h2_bloom_override_brightness(argument_real(a, 0));
	hs_return(thread_index, 0);
}

/* interpolator_start (name value seconds): Halo 2's scenario interpolators
drive its screen effects - 03a's, 04a's and 07a's "blurry_vision", their
effects\screen_effects\blur (a convolution of 8 at half resolution): this
engine's cinematic screen effect's blur, from where it is to the value over
the seconds. HALO_H2_NO_SCREEN_BLUR: none (as before) */
void rasterizer_screen_effect_set_convolution(short convolution_extra_passes, short convolution_type, real convolution_radius_lower_bound,
	real convolution_radius_upper_bound, real convolution_time);

H2_ALIGN static void interpolator_start_evaluate(short function_index, long thread_index, boolean initialize)
{
	static real blur;
	long *a = arguments_get(function_index, thread_index, initialize);
	char const *name;

	if (!a)
		return;
	name = a[0] ? (char const *)a[0] : "";
	if (!getenv("HALO_H2_NO_SCREEN_BLUR") && (strstr(name, "blur") || strstr(name, "vision")))
	{
		real target = PIN(argument_real(a, 1), 0.f, 1.f) * 4.f;
		real seconds = argument_real(a, 2);

		rasterizer_screen_effect_start(FALSE);
		rasterizer_screen_effect_set_convolution(1, 1, blur, target, seconds > 0.f ? seconds : 0.f);
		blur = target;
		platform_log("h2 scripts: interpolator %s to %.2f over %.2f s: the screen's blur %.2f", name, argument_real(a, 1), seconds, target);
	}
	hs_return(thread_index, 0);
}

H2_ALIGN static void cheat_camouflage_by_player_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	{
		long player_index = data_next_index(player_data, NONE), n;

		for (n = 0; n < argument_short(a, 0) && player_index != NONE; n++)
			player_index = data_next_index(player_data, player_index);
		if (player_index != NONE)
			unit_camouflage(player_get(player_index)->unit_index, argument_boolean(a, 1));
	}
	hs_return(thread_index, 0);
}

/* unit_has_weapon (unit, Halo 2's weapon tag): whether one of its weapons
is the tag's (this engine's of the same last name: h2_weapons.c's
"h2\weapons\<name>") */
H2_ALIGN static void unit_has_weapon_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	boolean has = FALSE;

	if (!a)
		return;
	{
		struct unit_datum *unit = a[0] != NONE ? unit_try_and_get(a[0]) : NULL;
		struct h2_cache *cache = h2_scripts_cache();
		long tag = cache && a[1] != NONE ? h2_cache_tag_from_handle(cache, (unsigned long)a[1]) : NONE;
		char const *name = tag != NONE ? h2_cache_tag_name(cache, tag) : NULL;
		char const *short_name = name ? strrchr(name, '\\') : NULL;
		long w;

		short_name = short_name ? short_name + 1 : name;
		for (w = 0; unit && short_name && w < MAXIMUM_WEAPONS_PER_UNIT && !has; w++)
		{
			long weapon = unit->unit.weapon_object_indices[w];
			char const *weapon_name = weapon != NONE && object_try_and_get(weapon) ? tag_get_name(object_get(weapon)->definition_index) : NULL;
			char const *weapon_short = weapon_name ? strrchr(weapon_name, '\\') : NULL;

			weapon_short = weapon_short ? weapon_short + 1 : weapon_name;
			has = weapon_short && !strcmp(weapon_short, short_name);
		}
	}
	hs_return(thread_index, return_boolean(has));
}

/* device_group_set (device group value): this engine's group set (the
device it names is Halo 2's bookkeeping) */
H2_ALIGN static void device_group_set_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	hs_return(thread_index, return_boolean(argument_short(a, 1) != NONE &&
		device_group_set_desired_value(argument_short(a, 1), argument_real(a, 2))));
}

/* ---- per-object script values (object_set_function_variable). Halo 2's
objects' functions read script variables by name ("engine_audio",
"engine_hack", "hover_audio", "holo_power", "turned_on"...). Each object's
values are kept here, eased to their targets over the times the scripts
give (h2_scripts_update), for:
- the code that draws and sounds the objects
  (h2_scripts_object_function_value: a vehicle's engine effects and
  sounds, a shader's input);
- an object definition's incoming function slot (A..D) given a variable's
  name (h2_scripts_object_function_slot_set, by whoever makes the object):
  its value is then the variable's (objects.c, object_export_function_values);
- what this engine has of its own: a device's power follows a variable whose
  name says power or on. */

static struct
{
	long definition_index;
	short slot;
	char name[32];
} object_function_slots[64];
static long object_function_slot_count;

static void h2_scripts_object_function_slots_clear(void)
{
	object_function_slot_count = 0;
}

void h2_scripts_object_function_slot_set(long definition_index, short slot, char const *name)
{
	if (object_function_slot_count < (long)NUMBEROF(object_function_slots) && slot >= 0 && slot < 4 && name)
	{
		object_function_slots[object_function_slot_count].definition_index = definition_index;
		object_function_slots[object_function_slot_count].slot = slot;
		snprintf(object_function_slots[object_function_slot_count].name, 32, "%s", name);
		object_function_slot_count++;
	}
}

static struct h2_object_variable *object_variable_find(long object_index, char const *name, boolean make)
{
	long i;

	for (i = 0; i < h2_scripts_globals.variable_count; i++)
	{
		struct h2_object_variable *v = &h2_scripts_globals.variables[i];

		if (v->object_index == object_index && !strcmp(v->name, name))
			return v;
	}
	if (!make || h2_scripts_globals.variable_count >= (long)NUMBEROF(h2_scripts_globals.variables))
		return NULL;
	{
		struct h2_object_variable *v = &h2_scripts_globals.variables[h2_scripts_globals.variable_count++];

		memset(v, 0, sizeof(*v));
		v->object_index = object_index;
		snprintf(v->name, sizeof(v->name), "%s", name);
		return v;
	}
}

static void object_variable_remove(long object_index, char const *name)
{
	long i;

	for (i = 0; i < h2_scripts_globals.variable_count;)
	{
		struct h2_object_variable *v = &h2_scripts_globals.variables[i];

		if (v->object_index == object_index && (!name || !strcmp(v->name, name)))
			*v = h2_scripts_globals.variables[--h2_scripts_globals.variable_count];
		else
			i++;
	}
}

boolean h2_scripts_object_function_value(long object_index, char const *name, real *value)
{
	struct h2_object_variable *v = name ? object_variable_find(object_index, name, FALSE) : NULL;

	if (v)
		*value = v->value;
	return v != NULL;
}

void h2_scripts_object_function_values(long object_index, long definition_index, real *incoming)
{
	long i;
	real value;

	for (i = 0; i < object_function_slot_count; i++)
	{
		if (object_function_slots[i].definition_index == definition_index &&
			h2_scripts_object_function_value(object_index, object_function_slots[i].name, &value))
		{
			incoming[object_function_slots[i].slot] = value;
		}
	}
}

/* a value's own effect where this engine has one: a device's power */
static void object_variable_apply(struct h2_object_variable const *v)
{
	if ((strstr(v->name, "power") || strstr(v->name, "_on")) &&
		object_try_and_get_and_verify_type(v->object_index, _object_mask_device))
	{
		device_set_power(v->object_index, v->value);
	}
}

/* (h2_scripts_update) eased to their targets; those of objects gone let go */
static void object_variables_update(void)
{
	long i;

	for (i = 0; i < h2_scripts_globals.variable_count;)
	{
		struct h2_object_variable *v = &h2_scripts_globals.variables[i];

		if (!object_try_and_get(v->object_index))
		{
			*v = h2_scripts_globals.variables[--h2_scripts_globals.variable_count];
			continue;
		}
		if (v->value != v->target)
		{
			real step = v->rate > 0.0f ? v->rate : 1e9f;

			v->value = v->value < v->target ? MIN(v->target, v->value + step) : MAX(v->target, v->value - step);
			object_variable_apply(v);
		}
		i++;
	}
}

/* object_set_function_variable (object name value seconds) */
H2_ALIGN static void object_set_function_variable_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_object_variable *v;

	if (!a)
		return;
	if (a[0] != NONE && object_try_and_get(a[0]) && a[1] && (v = object_variable_find(a[0], (char const *)a[1], TRUE)) != NULL)
	{
		real seconds = argument_real(a, 3);
		boolean fresh = !v->set;

		v->target = argument_real(a, 2);
		if (fresh || seconds <= 0.0f)
			v->value = v->target;
		v->rate = seconds > 0.0f ? fabsf(v->target - v->value) / (seconds * 30.0f) : 0.0f;
		v->set = TRUE;
		object_variable_apply(v);
	}
	hs_return(thread_index, 0);
}

/* object_clear_function_variable (object name), object_clear_all_function_variables (object) */
H2_ALIGN static void object_clear_function_variable_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);
	struct h2_function *function = function_from_index(function_index);

	if (!a || !function)
		return;
	if (a[0] != NONE)
		object_variable_remove(a[0], function->definition.parameter_count > 1 && a[1] ? (char const *)a[1] : NULL);
	hs_return(thread_index, 0);
}

/* objectives_clear */
H2_ALIGN static void objectives_clear_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	h2_script_text_objectives_clear();
	hs_return(thread_index, 0);
}

/* object_model_targets_destroyed (object target): none (this engine's
objects keep no Halo 2 model targets) */
H2_ALIGN static void zero_evaluate(short function_index, long thread_index, boolean initialize)
{
	long *a = arguments_get(function_index, thread_index, initialize);

	if (!a)
		return;
	hs_return(thread_index, 0);
}

/* (h2_scripts.h) */
#define native_function h2_scripts_native

static struct native_function const native_functions[] =
{
	{ "sleep_forever", _hs_type_void, sleep_forever_evaluate, NONE, { 0 } },
	{ "print", _hs_type_void, print_evaluate, 1, { _hs_type_string } },
	{ "pin", _hs_type_real, pin_evaluate, 3, { _hs_type_real, _hs_type_real, _hs_type_real } },
	{ "switch_bsp", _hs_type_void, switch_bsp_evaluate, 1, { _hs_type_short_integer } },
	{ "game_can_use_flashlights", _hs_type_void, game_can_use_flashlights_evaluate, 1, { _hs_type_boolean } },
	{ "switch_bsp_by_name", _hs_type_void, switch_bsp_evaluate, 1, { _hs_type_short_integer } },
	{ "structure_bsp_index", _hs_type_short_integer, structure_bsp_index_evaluate, 0, { 0 } },
	{ "list_count_not_dead", _hs_type_short_integer, list_count_not_dead_evaluate, 1, { _hs_type_object_list } },
	{ "object_get_health", _hs_type_real, object_get_vitality_evaluate, 1, { _hs_type_object } },
	{ "object_get_shield", _hs_type_real, object_get_vitality_evaluate, 1, { _hs_type_object } },
	{ "objects_distance_to_object", _hs_type_real, objects_distance_to_object_evaluate, 2, { _hs_type_object_list, _hs_type_object } },
	{ "objects_distance_to_flag", _hs_type_real, objects_distance_to_flag_evaluate, 2, { _hs_type_object_list, _hs_type_cutscene_flag } },
	{ "objects_distance_to_position", _hs_type_real, objects_distance_to_position_evaluate, 4,
		{ _hs_type_object_list, _hs_type_real, _hs_type_real, _hs_type_real } },
	{ "volume_return_objects", _hs_type_object_list, volume_return_objects_evaluate, 1, { _hs_type_trigger_volume } },
	{ "volume_return_objects_by_type", _hs_type_object_list, volume_return_objects_evaluate, 2,
		{ _hs_type_trigger_volume, _hs_type_long_integer } },
	{ "object_get_parent", _hs_type_object, object_get_parent_evaluate, 1, { _hs_type_object } },
	{ "unit_in_vehicle", _hs_type_boolean, unit_in_vehicle_evaluate, 1, { _hs_type_unit } },
	{ "game_tick_rate_scalar", _hs_type_long_integer, one_evaluate, 0, { 0 } },
	{ "game_is_playtest", _hs_type_boolean, false_evaluate, 0, { 0 } },
	{ "unit_is_emitting", _hs_type_boolean, false_evaluate, 1, { _hs_type_unit } },
	{ "game_tick_rate", _hs_type_long_integer, thirty_evaluate, 0, { 0 } },
	{ "activate_team_nav_point_object", _hs_type_void, nav_point_activate_evaluate, 4,
		{ _hs_type_navpoint, _hs_type_enum_team, _hs_type_object, _hs_type_real } },
	{ "breadcrumbs_activate_team_nav_point_object", _hs_type_void, nav_point_activate_evaluate, 4,
		{ _hs_type_navpoint, _hs_type_enum_team, _hs_type_object, _hs_type_real } },
	{ "activate_team_nav_point_flag", _hs_type_void, nav_point_activate_evaluate, 4,
		{ _hs_type_navpoint, _hs_type_enum_team, _hs_type_cutscene_flag, _hs_type_real } },
	{ "breadcrumbs_activate_team_nav_point_flag", _hs_type_void, nav_point_activate_evaluate, 4,
		{ _hs_type_navpoint, _hs_type_enum_team, _hs_type_cutscene_flag, _hs_type_real } },
	{ "deactivate_team_nav_point_object", _hs_type_void, nav_point_deactivate_evaluate, 2, { _hs_type_enum_team, _hs_type_object } },
	{ "breadcrumbs_deactivate_team_nav_point_object", _hs_type_void, nav_point_deactivate_evaluate, 2, { _hs_type_enum_team, _hs_type_object } },
	{ "deactivate_team_nav_point_flag", _hs_type_void, nav_point_deactivate_evaluate, 2, { _hs_type_enum_team, _hs_type_cutscene_flag } },
	{ "breadcrumbs_deactivate_team_nav_point_flag", _hs_type_void, nav_point_deactivate_evaluate, 2,
		{ _hs_type_enum_team, _hs_type_cutscene_flag } },
	{ "object_set_velocity", _hs_type_void, object_set_velocity_evaluate, 2, { _hs_type_object, _hs_type_real } },
	{ "object_set_velocity", _hs_type_void, object_set_velocity_evaluate, 4, { _hs_type_object, _hs_type_real, _hs_type_real, _hs_type_real } },
	{ "object_set_deleted_when_deactivated", _hs_type_void, object_deleted_when_deactivated_evaluate, 1, { _hs_type_object } },
	{ "object_set_shield_stun_infinite", _hs_type_void, object_shield_stun_infinite_evaluate, 1, { _hs_type_object } },
	{ "object_destroy_type_mask", _hs_type_void, object_destroy_type_mask_evaluate, 1, { _hs_type_long_integer } },
	{ "unit_set_active_camo", _hs_type_void, unit_set_active_camo_evaluate, 3, { _hs_type_unit, _hs_type_boolean, _hs_type_real } },
	{ "interpolator_start", _hs_type_void, interpolator_start_evaluate, 3, { _hs_type_string, _hs_type_real, _hs_type_real } },
	{ "cheat_active_camouflage_by_player", _hs_type_void, cheat_camouflage_by_player_evaluate, 2, { _hs_type_short_integer, _hs_type_boolean } },
	{ "unit_has_weapon", _hs_type_boolean, unit_has_weapon_evaluate, 2, { _hs_type_unit, _hs_type_object_definition } },
	{ "device_group_set", _hs_type_boolean, device_group_set_evaluate, 3, { _hs_type_device, _hs_type_device_group, _hs_type_real } },
	{ "object_set_function_variable", _hs_type_void, object_set_function_variable_evaluate, 4,
		{ _hs_type_object, _hs_type_string, _hs_type_real, _hs_type_real } },
	{ "object_clear_function_variable", _hs_type_void, object_clear_function_variable_evaluate, 2, { _hs_type_object, _hs_type_string } },
	{ "object_clear_all_function_variables", _hs_type_void, object_clear_function_variable_evaluate, 1, { _hs_type_object } },
	{ "objectives_clear", _hs_type_void, objectives_clear_evaluate, 0, { 0 } },
	{ "object_model_targets_destroyed", _hs_type_short_integer, zero_evaluate, 2, { _hs_type_object, _hs_type_string } },
	{ "device_animate_overlay", _hs_type_void, device_animate_overlay_evaluate, 5,
		{ _hs_type_device, _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real } },
	{ "device_set_overlay_track", _hs_type_boolean, device_set_overlay_track_evaluate, 2, { _hs_type_device, _hs_type_string } },
	{ "camera_control", _hs_type_void, camera_control_evaluate, 1, { _hs_type_boolean } },
	{ "camera_set", _hs_type_void, camera_set_evaluate, 2, { _hs_type_cutscene_camera_point, _hs_type_short_integer } },
	{ "camera_set_animation_relative", _hs_type_void, camera_set_animation_evaluate, 4,
		{ _hs_type_animation_graph, _hs_type_string, _hs_type_unit, _hs_type_cutscene_flag } },
	{ "camera_set_field_of_view", _hs_type_void, camera_field_of_view_evaluate, 2, { _hs_type_real, _hs_type_short_integer } },
	{ "camera_time", _hs_type_short_integer, camera_time_evaluate, 0, { 0 } },
	{ "cinematic_show_letterbox_immediate", _hs_type_void, letterbox_immediate_evaluate, 1, { _hs_type_boolean } },
	{ "custom_animation", _hs_type_boolean, object_animation_evaluate, 4,
		{ _hs_type_unit, _hs_type_animation_graph, _hs_type_string, _hs_type_boolean } },
	{ "custom_animation_loop", _hs_type_boolean, object_animation_evaluate, 4,
		{ _hs_type_unit, _hs_type_animation_graph, _hs_type_string, _hs_type_boolean } },
	{ "custom_animation_relative", _hs_type_boolean, object_animation_evaluate, 5,
		{ _hs_type_unit, _hs_type_animation_graph, _hs_type_string, _hs_type_boolean, _hs_type_object } },
	{ "custom_animation_relative_loop", _hs_type_boolean, object_animation_evaluate, 5,
		{ _hs_type_unit, _hs_type_animation_graph, _hs_type_string, _hs_type_boolean, _hs_type_object } },
	{ "scenery_animation_start", _hs_type_void, object_animation_evaluate, 3, { _hs_type_scenery, _hs_type_animation_graph, _hs_type_string } },
	{ "scenery_animation_start_loop", _hs_type_void, object_animation_evaluate, 3, { _hs_type_scenery, _hs_type_animation_graph, _hs_type_string } },
	{ "scenery_animation_start_relative", _hs_type_void, object_animation_evaluate, 4,
		{ _hs_type_scenery, _hs_type_animation_graph, _hs_type_string, _hs_type_object } },
	{ "unit_get_custom_animation_time", _hs_type_short_integer, object_animation_time_evaluate, 1, { _hs_type_unit } },
	{ "scenery_get_animation_time", _hs_type_short_integer, object_animation_time_evaluate, 1, { _hs_type_scenery } },
	{ "unit_stop_custom_animation", _hs_type_void, object_animation_stop_evaluate, 1, { _hs_type_unit } },
	{ "effect_new", _hs_type_void, effect_new_evaluate, 2, { _hs_type_effect, _hs_type_cutscene_flag } },
	{ "effect_new_on_object_marker", _hs_type_void, effect_new_evaluate, 3, { _hs_type_effect, _hs_type_object, _hs_type_string } },
	{ "vehicle_unload", _hs_type_short_integer, vehicle_unload_evaluate, 2, { _hs_type_object, _hs_type_string } },
	{ "vehicle_load_magic", _hs_type_short_integer, vehicle_load_magic_evaluate, 3, { _hs_type_object, _hs_type_string, _hs_type_object_list } },
	{ "unit_exit_vehicle", _hs_type_void, unit_exit_vehicle_evaluate, 2, { _hs_type_unit, _hs_type_short_integer } },
	{ "object_hide", _hs_type_void, object_hide_evaluate, 2, { _hs_type_object, _hs_type_boolean } },
	{ "object_uses_cinematic_lighting", _hs_type_void, cinematic_lighting_evaluate, 2, { _hs_type_object, _hs_type_boolean } },
	{ "cinematic_screen_effect_start", _hs_type_void, screen_effect_evaluate, 1, { _hs_type_boolean } },
	{ "cinematic_screen_effect_stop", _hs_type_void, screen_effect_evaluate, 0, { 0 } },
	{ "cinematic_screen_effect_set_depth_of_field", _hs_type_void, screen_effect_evaluate, 7,
		{ _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real } },
	{ "cinematic_screen_effect_set_crossfade", _hs_type_void, screen_effect_evaluate, 1, { _hs_type_real } },
	{ "cinematic_screen_effect_set_crossfade2", _hs_type_void, screen_effect_evaluate, 2, { _hs_type_real, _hs_type_real } },
	{ "object_cinematic_lod", _hs_type_void, object_cinematic_lod_evaluate, NONE, { 0 } },
	{ "pvs_set_object", _hs_type_void, pvs_set_object_evaluate, NONE, { 0 } },
	{ "pvs_clear", _hs_type_void, pvs_clear_evaluate, 0, { 0 } },
	{ "rasterizer_bloom_override", _hs_type_void, bloom_override_evaluate, 1, { _hs_type_boolean } },
	{ "rasterizer_bloom_override_reset", _hs_type_void, bloom_override_evaluate, 1, { _hs_type_boolean } },
	{ "rasterizer_bloom_override_threshold", _hs_type_void, bloom_override_evaluate, 1, { _hs_type_real } },
	{ "rasterizer_bloom_override_brightness", _hs_type_void, bloom_override_evaluate, 1, { _hs_type_real } },
	{ "cinematic_lighting_set_primary_light", _hs_type_void, cinematic_lighting_evaluate, 5,
		{ _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real } },
	{ "cinematic_lighting_set_secondary_light", _hs_type_void, cinematic_lighting_evaluate, 5,
		{ _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real } },
	{ "cinematic_lighting_set_ambient_light", _hs_type_void, cinematic_lighting_evaluate, 3, { _hs_type_real, _hs_type_real, _hs_type_real } },
	{ "objects_attach", _hs_type_void, objects_attach_evaluate, NONE, { 0 } },
	{ "objects_detach", _hs_type_void, objects_attach_evaluate, NONE, { 0 } },
	{ "weather_start", _hs_type_void, weather_evaluate, 1, { _hs_type_real } },
	{ "weather_stop", _hs_type_void, weather_evaluate, 1, { _hs_type_real } },
	{ "weather_change_intensity", _hs_type_void, weather_evaluate, 2, { _hs_type_real, _hs_type_real } },
	{ "kill_volume_enable", _hs_type_void, kill_volume_evaluate, 1, { _hs_type_trigger_volume } },
	{ "kill_volume_disable", _hs_type_void, kill_volume_evaluate, 1, { _hs_type_trigger_volume } },
	{ "hud_set_training_text", _hs_type_void, text_evaluate, 1, { _hs_type_string } },
	{ "hud_show_training_text", _hs_type_void, text_evaluate, 1, { _hs_type_boolean } },
	{ "objectives_show_up_to", _hs_type_void, text_evaluate, 1, { _hs_type_long_integer } },
	{ "objectives_finish_up_to", _hs_type_void, text_evaluate, 1, { _hs_type_long_integer } },
	{ "cinematic_subtitle", _hs_type_void, text_evaluate, 2, { _hs_type_string, _hs_type_real } },
	{ "cinematic_set_title", _hs_type_void, text_evaluate, 1, { _hs_type_cutscene_title } },
	{ "hud_cinematic_fade", _hs_type_void, hud_cinematic_fade_evaluate, 2, { _hs_type_real, _hs_type_real } },
	{ "hud_enable_training", _hs_type_void, nothing_evaluate, 1, { _hs_type_boolean } },
	{ "device_animate_position", _hs_type_void, device_animate_position_evaluate, 6,
		{ _hs_type_device, _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_boolean } },
	{ "device_set_position_track", _hs_type_boolean, device_set_position_track_evaluate, 3, { _hs_type_device, _hs_type_string, _hs_type_real } },
	{ "sound_impulse_start", _hs_type_void, sound_impulse_start_evaluate, 3, { _hs_type_sound, _hs_type_object, _hs_type_real } },
	{ "sound_impulse_start_effect", _hs_type_void, sound_impulse_start_evaluate, 4,
		{ _hs_type_sound, _hs_type_object, _hs_type_real, _hs_type_string } },
	{ "sound_impulse_trigger", _hs_type_void, sound_impulse_start_evaluate, 4,
		{ _hs_type_sound, _hs_type_object, _hs_type_real, _hs_type_long_integer } },
	{ "sound_impulse_language_time", _hs_type_long_integer, sound_impulse_language_time_evaluate, 1, { _hs_type_sound } },
	{ "sound_impulse_stop", _hs_type_void, sound_impulse_stop_evaluate, 1, { _hs_type_sound } },
	{ "sound_looping_start", _hs_type_void, sound_looping_evaluate, 3, { _hs_type_looping_sound, _hs_type_object, _hs_type_real } },
	{ "sound_looping_stop", _hs_type_void, sound_looping_evaluate, 1, { _hs_type_looping_sound } },
	{ "sound_looping_set_alternate", _hs_type_void, sound_looping_evaluate, 2, { _hs_type_looping_sound, _hs_type_boolean } },
	{ "player0_looking_up", _hs_type_boolean, player0_looking_evaluate, 0, { 0 } },
	{ "player0_looking_down", _hs_type_boolean, player0_looking_evaluate, 0, { 0 } },
	{ "player_action_test_lookstick_forward", _hs_type_boolean, lookstick_evaluate, 0, { 0 } },
	{ "player_action_test_lookstick_backward", _hs_type_boolean, lookstick_evaluate, 0, { 0 } },
	{ "player_action_test_look_up_begin", _hs_type_void, nothing_evaluate, 0, { 0 } },
	{ "cinematic_clone_players_weapon", _hs_type_void, cinematic_clone_players_weapon_evaluate, 3,
		{ _hs_type_object, _hs_type_string, _hs_type_string } },
	{ "damage_object", _hs_type_void, damage_object_evaluate, NONE, { 0 } },
	{ "map_reset", _hs_type_void, map_reset_evaluate, 0, { 0 } },
	{ "weapon_hold_trigger", _hs_type_void, weapon_hold_trigger_evaluate, NONE, { 0 } },
	{ "damage_new", _hs_type_void, damage_new_evaluate, NONE, { 0 } },
	{ "damage_players", _hs_type_void, damage_players_evaluate, NONE, { 0 } },
	{ "biped_ragdoll", _hs_type_void, biped_ragdoll_evaluate, NONE, { 0 } },
	{ "ice_cream_flavor_stock", _hs_type_void, ice_cream_flavor_stock_evaluate, 1, { _hs_type_long_integer } },
	{ "ice_cream_flavor_available", _hs_type_boolean, ice_cream_flavor_available_evaluate, 1, { _hs_type_long_integer } },
	{ "object_dynamic_simulation_disable", _hs_type_void, object_dynamic_simulation_disable_evaluate, NONE, { 0 } },
	{ "unit_only_takes_damage_from_players_team", _hs_type_void, unit_only_takes_damage_from_players_team_evaluate, NONE, { 0 } },
	/* (a local frame of motion for physics that respects it, 04b's plummet: nothing here respects one) */
	{ "physics_set_velocity_frame", _hs_type_void, nothing_evaluate, 3, { _hs_type_real, _hs_type_real, _hs_type_real } },
	{ "object_at_marker", _hs_type_object, object_at_marker_evaluate, 2, { _hs_type_object, _hs_type_string } },
	{ "texture_camera_set_object_marker", _hs_type_void, texture_camera_evaluate, 3, { _hs_type_object, _hs_type_string, _hs_type_real } },
	{ "texture_camera_off", _hs_type_void, texture_camera_evaluate, 0, { 0 } },
	{ "player_disable_movement", _hs_type_void, player_disable_movement_evaluate, 1, { _hs_type_boolean } },
	{ "player_action_test_melee", _hs_type_boolean, player_action_test_evaluate, 0, { 0 } },
	{ "player_action_test_vision_trigger", _hs_type_boolean, player_action_test_evaluate, 0, { 0 } },
	{ "player_action_test_cancel", _hs_type_boolean, player_action_test_evaluate, 0, { 0 } },
	{ "player_flashlight_on", _hs_type_boolean, player_flashlight_on_evaluate, 0, { 0 } },
	{ "physics_set_gravity", _hs_type_void, physics_set_gravity_evaluate, 1, { _hs_type_real } },
	{ "device_closes_automatically_set", _hs_type_void, device_closes_automatically_evaluate, 2, { _hs_type_device, _hs_type_boolean } },
	/* (Halo 2's phantom power latch, a look; no-ops here) */
	{ "object_set_phantom_power", _hs_type_void, nothing_evaluate, 2, { _hs_type_object, _hs_type_boolean } },
	{ "physics_disable_character_ground_adhesion_forces", _hs_type_void, nothing_evaluate, 1, { _hs_type_real } },
	{ "set_global_sound_environment", _hs_type_void, nothing_evaluate, 6,
		{ _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_long_integer, _hs_type_real } },
	{ "player_action_test_look_down_begin", _hs_type_void, nothing_evaluate, 0, { 0 } },
	{ "player_action_test_look_pitch_end", _hs_type_void, nothing_evaluate, 0, { 0 } },
};

/* Halo 2's names for functions of this engine's that do the same */
static struct
{
	char const *halo2;
	char const *engine;
} const engine_aliases[] =
{
	/* (a cinematic's skip reverts to the save made as it starts -
	global_scripts' cinematic_skip_start / _stop, which revert when it ends
	too: the level as it was before the cinematic, its BSP and the player
	where the level starts) */
	/* (Halo 2's game_save_immediate saves now, safe or not: this engine's
	totally unsafe one; game_save waits for a safe moment, 8 s at most, as
	Halo 2's) */
	{ "game_save_immediate", "game_save_totally_unsafe" },
	{ "game_save_cinematic_skip", "game_save_totally_unsafe" },
	{ "object_create_clone", "object_create" },
	{ "player_effect_set_max_vibration", "player_effect_set_max_rumble" },
	{ "controller_get_look_invert", "player0_look_pitch_is_inverted" },
	{ "controller_set_look_invert", "player0_look_invert_pitch" },
};

/* functions of this engine's that Halo 2's scripts call but that are not
given them here: the camera is the player's until cutscenes are played
(camera_control, camera_set); the BSP and printing are this file's; and
those a map may not call */
static char const *const engine_refused[] =
{
	"time_code_show", "time_code_start", "time_code_reset",
	"camera_control", "camera_set", "camera_time", "unit_get_custom_animation_time", "scenery_get_animation_time",
	"unit_stop_custom_animation", "switch_bsp", "structure_bsp_index", "print",
	"cinematic_set_title",
};

/* nothing to do here (no stand-in logged): predicting, lighting and
render hints, data mining, caches */
static char const *const no_op_prefixes[] =
{
	"predict_", "object_cinematic_lod", "cinematic_lighting_update", "sound_impulse_predict",
	"data_mine_", "texture_cache_flush", "geometry_cache_flush", "cache_block_for_one_frame", "object_type_predict",
	"objects_predict", "camera_predict_", "rasterizer_bloom_", "cinematic_lightmap_shadow_", "render_lights_enable_cinematic_shadow",
	"pvs_", "garbage_collect_unsafe", "sound_suppress_ambience_update_on_revert", "cinematic_screen_effect_set_",
	"object_cinematic_visibility", "enable_render_light_suppressor", "disable_render_light_suppressor",
	"loading_screen_fade_to_white", "cinematic_enable_ambience_details", "object_type_predict_high",
	/* (debug: Halo 2's release builds draw no time code) */
	"time_code_",
	/* (flocks, training hints, movies and the outro: none here; the
	warthog's chaingun light, a debug drop, screen interpolators. The
	weather is this file's: weather_evaluate) */
	"flock_", "player_training_activate_", "cinematic_start_movie", "cinematic_outro_start",
	"cinematic_set_far_clip_distance", "weapon_enable_warthog_chaingun_light", "drop",
};

static short stand_in_kind(char const *name)
{
	int i;

	for (i = 0; i < (int)NUMBEROF(no_op_prefixes); i++)
	{
		if (!strncmp(name, no_op_prefixes[i], strlen(no_op_prefixes[i])))
			return _kind_no_op;
	}
	if (!strncmp(name, "ai_play_line", 12) || !strncmp(name, "sound_", 6) || !strcmp(name, "cs_play_line"))
		return _kind_sound;
	if (!strncmp(name, "ai", 2) || !strncmp(name, "cs_", 3) || !strncmp(name, "unit_set_emotional_state", 24))
	{
		return _kind_ai;
	}
	if (!strncmp(name, "cinematic", 9) || !strncmp(name, "camera_", 7) || strstr(name, "custom_animation") ||
		!strncmp(name, "scenery_animation", 17) || !strncmp(name, "hud_cinematic", 13) || !strncmp(name, "fade_", 5) ||
		!strncmp(name, "objects_attach", 14) || !strncmp(name, "objects_detach", 14))
	{
		return _kind_cinematic;
	}
	return _kind_other;
}

/* the function a name is given (index in h2_scripts_globals.functions),
made the first time; NONE: no memory */
static long function_for_name(char const *name, short kind, short engine_index, struct native_function const *native)
{
	struct h2_function *function;
	long i;

	for (i = 0; i < h2_scripts_globals.count; i++)
	{
		/* (a name Halo 2 has twice - object_set_velocity (object forward)
		and (object forward left up) - is two functions: 03a's falling
		Warthog, the second form, ran as the first one's parameters and
		halted the script "corrupted syntax tree" at the tunnel Scarab) */
		if (!strcmp(h2_scripts_globals.functions[i].name, name) && h2_scripts_globals.functions[i].kind == kind &&
			(!native || h2_scripts_globals.functions[i].definition.parameter_count ==
				(native->parameter_count == NONE ? 0 : native->parameter_count)))
		{
			return i;
		}
	}
	if (h2_scripts_globals.count == h2_scripts_globals.capacity)
	{
		long capacity = h2_scripts_globals.capacity ? 2 * h2_scripts_globals.capacity : 256;
		struct h2_function *grown = realloc(h2_scripts_globals.functions, capacity * sizeof(*grown));

		if (!grown || capacity > 0x3FFF)
			return NONE;
		h2_scripts_globals.functions = grown;
		h2_scripts_globals.capacity = capacity;
	}
	function = &h2_scripts_globals.functions[h2_scripts_globals.count];
	memset(function, 0, sizeof(*function));
	function->name = name;
	function->kind = kind;
	function->engine_index = engine_index;
	function->definition.name = name;
	function->definition.help = "";
	if (kind == _kind_engine && engine_index != NONE)
	{
		struct hs_function_definition const *engine = hs_function_get(engine_index);
		int p;

		function->definition.return_type = engine->return_type;
		function->definition.parse = engine->parse;
		function->definition.evaluate = engine_guard_evaluate;
		function->definition.parameter_count = engine->parameter_count;
		for (p = 0; p < engine->parameter_count && p < MAXIMUM_ARGUMENTS; p++)
			function->definition.parameter_types[p] = engine->parameter_types[p];
	}
	else if (native)
	{
		int p;

		function->definition.return_type = native->return_type;
		function->definition.evaluate = native->evaluate;
		function->definition.parameter_count = native->parameter_count == NONE ? 0 : native->parameter_count;
		for (p = 0; p < function->definition.parameter_count && p < MAXIMUM_ARGUMENTS; p++)
			function->definition.parameter_types[p] = native->parameter_types[p];
	}
	else
	{
		function->definition.return_type = _hs_passthrough;
		function->definition.evaluate = stand_in_evaluate;
	}

	return h2_scripts_globals.count++;
}

/* ---------- the runtime: the scripts made ready */

static void report(char const *why);

struct conversion
{
	struct scenario *scenario;
	struct data_array *syntax;
	struct hs_syntax_node *nodes;
	long node_count;
	char const *strings;
	long string_bytes;
	long calls[NUMBER_OF_KINDS];
	long made_inert;
	long casts_inert;
	long objects_as_lists;
};

static struct hs_syntax_node *node_get(struct conversion const *c, long index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(index);

	if (index == NONE || absolute >= c->node_count || !c->nodes[absolute].datum_header)
		return NULL;
	return &c->nodes[absolute];
}

static char const *string_at(struct conversion const *c, long offset)
{
	return offset >= 0 && offset < c->string_bytes && memchr(c->strings + offset, 0, c->string_bytes - offset) ?
		c->strings + offset : NULL;
}

/* a node made a constant of its own type (its type's default) */
static void node_make_inert(struct conversion *c, struct hs_syntax_node *node)
{
	node->flags = NODE_PRIMITIVE | NODE_PERMANENT;
	node->constant_type = node->type;
	node->data = hs_type_valid(node->type) ? hs_runtime_type_default(node->type) : 0;
	c->made_inert++;
}

/* whether a call of this engine's function `index` fits it: the
arguments it takes, of its types; none of Halo 2's own kinds of value */
static boolean engine_call_fits(struct conversion const *c, short index, struct hs_syntax_node const *name_node)
{
	struct hs_function_definition const *definition = hs_function_get(index);
	struct hs_syntax_node const *argument;
	short count = 0;

	if (!definition->evaluate)
		return FALSE;
	/* (the special forms: begin to unit - they take any arguments of the
	language's; begin_random keeps 32) */
	if (index <= _hs_function_object_to_unit)
	{
		if (index == _hs_function_begin_random || index == _hs_function_cond)
		{
			for (argument = node_get(c, name_node->next_node_index); argument; argument = node_get(c, argument->next_node_index))
				count++;
			return index == _hs_function_begin_random && count <= 32;
		}
		return TRUE;
	}
	if (!hs_function_allowed_in_map_scripts(index) || type_is_halo2s(definition->return_type))
		return FALSE;
	for (argument = node_get(c, name_node->next_node_index); argument; argument = node_get(c, argument->next_node_index))
	{
		if (count >= definition->parameter_count || argument->type != definition->parameter_types[count] ||
			type_is_halo2s(argument->type))
		{
			return FALSE;
		}
		count++;
	}
	return count == definition->parameter_count;
}

/* whether a call fits one of this file's functions */
static boolean native_call_fits(struct conversion const *c, struct native_function const *native, struct hs_syntax_node const *name_node)
{
	struct hs_syntax_node const *argument;
	short count = 0;

	if (native->parameter_count == NONE)
		return TRUE;
	for (argument = node_get(c, name_node->next_node_index); argument; argument = node_get(c, argument->next_node_index))
	{
		if (count >= native->parameter_count || argument->type != native->parameter_types[count])
			return FALSE;
		count++;
	}
	return count == native->parameter_count;
}

/* a call given its function */
static boolean call_convert(struct conversion *c, struct hs_syntax_node *call)
{
	struct hs_syntax_node *name_node = node_get(c, call->data);
	char const *name = name_node ? string_at(c, name_node->string_offset) : NULL;
	char const *engine_name;
	short engine_index = NONE, kind;
	long entry = NONE;
	int i;

	if (!name || name_node->type != _hs_function_name)
		return FALSE;
	/* (HALO_H2_SCRIPT_INDEX_LOG=1: each function's index in the MCC map's
	own numbering, once - to find it in the Halo 2 decomp's function table,
	whose evaluators are annotated with their index) */
	if (HALO_ENV_ONCE("HALO_H2_SCRIPT_INDEX_LOG"))
	{
		for (i = 0; i < h2_scripts_globals.count && strcmp(h2_scripts_globals.functions[i].name, name); i++)
			;
		if (i == h2_scripts_globals.count)
		{
			char signature[256];
			struct hs_syntax_node const *argument;
			int used = 0;

			/* (the first call's argument types, this engine's names, to tell
			the decomp's entry of the same index from a neighbour's) */
			for (argument = node_get(c, name_node->next_node_index); argument && used < 200; argument = node_get(c, argument->next_node_index))
			{
				used += snprintf(signature + used, sizeof(signature) - used, "%s%s", used ? "," : "",
					hs_type_valid(argument->type) ? hs_type_names[argument->type] : "?");
			}
			platform_log("h2 script index: %d %s (%s)", (int)call->function_index, name, signature);
		}
	}
	/* this engine's, where it does what Halo 2's does with these arguments */
	engine_name = name;
	for (i = 0; i < (int)NUMBEROF(engine_aliases); i++)
	{
		if (!strcmp(name, engine_aliases[i].halo2))
			engine_name = engine_aliases[i].engine;
	}
	for (i = 0; i < (int)NUMBEROF(engine_refused) && strcmp(name, engine_refused[i]); i++)
		;
	/* (Halo 2's main menu scene, h2_menu.c: its flythrough is the camera) */
	if (i < (int)NUMBEROF(engine_refused) && h2_menu_scene() &&
		(!strcmp(name, "camera_control") || !strcmp(name, "camera_set")))
	{
		i = (int)NUMBEROF(engine_refused);
	}
	if (i == (int)NUMBEROF(engine_refused))
		engine_index = hs_find_function_by_name(engine_name);
	if (engine_index != NONE && engine_call_fits(c, engine_index, name_node))
	{
		struct hs_function_definition const *engine = hs_function_get(engine_index);
		boolean guarded = FALSE;
		short p;

		/* (one that takes objects is called through engine_guard_evaluate;
		the others are called as they are, listed for the counts) */
		for (p = 0; engine->parse == hs_macro_function_parse && p < engine->parameter_count; p++)
			guarded = guarded || HS_TYPE_IS_OBJECT(engine->parameter_types[p]);
		if (engine->parameter_count > MAXIMUM_ARGUMENTS)
			guarded = FALSE;
		entry = function_for_name(name, _kind_engine, engine_index, NULL);
		if (entry != NONE)
			h2_scripts_globals.functions[entry].call_sites++;
		call->function_index = guarded && entry != NONE ? (short)(H2_SCRIPTS_FUNCTION_BASE + entry) : engine_index;
		name_node->function_index = call->function_index;
		c->calls[_kind_engine]++;
		return TRUE;
	}
	/* one of this file's, h2_ai.c's (the AI and command scripts), or a
	stand-in */
	for (i = 0; i < (int)NUMBEROF(native_functions); i++)
	{
		if (!strcmp(name, native_functions[i].name) && native_call_fits(c, &native_functions[i], name_node))
			break;
	}
	if (i < (int)NUMBEROF(native_functions))
		entry = function_for_name(name, _kind_halo2, NONE, &native_functions[i]);
	else
	{
		long ai_count = 0, k;
		struct native_function const *ai_natives = h2_ai_natives(&ai_count);

		for (k = 0; k < ai_count; k++)
		{
			if (!strcmp(name, ai_natives[k].name) && native_call_fits(c, &ai_natives[k], name_node))
				break;
		}
		if (k < ai_count)
			entry = function_for_name(name, _kind_halo2, NONE, &ai_natives[k]);
		else
		{
			/* (h2_campaign.c's: starting profiles, nav points) */
			long campaign_count = 0;
			struct native_function const *campaign_natives = h2_campaign_natives(&campaign_count);

			for (k = 0; k < campaign_count; k++)
			{
				if (!strcmp(name, campaign_natives[k].name) && native_call_fits(c, &campaign_natives[k], name_node))
					break;
			}
			if (k < campaign_count)
				entry = function_for_name(name, _kind_halo2, NONE, &campaign_natives[k]);
			else
				entry = function_for_name(name, stand_in_kind(name), NONE, NULL);
		}
	}
	if (entry == NONE)
		return FALSE;
	kind = h2_scripts_globals.functions[entry].kind;
	h2_scripts_globals.functions[entry].call_sites++;
	call->function_index = (short)(H2_SCRIPTS_FUNCTION_BASE + entry);
	name_node->function_index = call->function_index;
	c->calls[kind]++;

	return TRUE;
}

/* the type a node gives (before its parent's cast) */
static short node_actual_type(struct conversion const *c, struct hs_syntax_node const *node)
{
	if (node->flags & NODE_PRIMITIVE)
	{
		if (node->flags & NODE_GLOBAL)
			return hs_global_get_type(node->short_value);
		return node->constant_type;
	}
	if (node->flags & NODE_SCRIPT)
	{
		return node->script_index >= 0 && node->script_index < c->scenario->hs_scripts.count ?
			TAG_BLOCK_GET_ELEMENT(&c->scenario->hs_scripts, node->script_index, struct hs_script)->return_type : _hs_unparsed;
	}
	if (node->function_index >= 0 && node->function_index < H2_SCRIPTS_FUNCTION_BASE)
		return hs_function_get(node->function_index)->return_type;
	return function_from_index(node->function_index) ? function_from_index(node->function_index)->definition.return_type : _hs_unparsed;
}

/* the level's map (the Halo 2 map it was made from), open while it is
played: h2_script_text.c reads its strings, h2_cinematics.c its animations */
static struct h2_cache *runtime_cache;

struct h2_cache *h2_scripts_cache(void)
{
	return runtime_cache;
}

static void h2_scripts_runtime_cache_close(void)
{
	h2_script_text_unload();
	h2_cinematics_dispose();
	if (runtime_cache)
		h2_cache_close(runtime_cache);
	runtime_cache = NULL;
}

/* (scenario.c, a structure BSP switch in the play, before the old BSP is let
go) the pieces the level's map's open holds - up to its 8 MB budget, the C
heap's - let go: the next BSP's world is read from the map with what the
play left, and the dev Vita's Cairo switch found the C heap at 1.3 MB free
(Nov 11). The scripts read them again as they need them. The open is the
tick's own (the scripts', the cinematics', the AI's): a switch runs on the
tick's thread (a script's switch_bsp) or between ticks (the main loop's) */
void h2_scripts_structure_bsp_switching(void)
{
	unsigned long held;

	if (!runtime_cache)
		return;
	held = h2_cache_segment_bytes(runtime_cache);
	h2_cache_trim(runtime_cache);
	platform_log("h2 scripts: the level's map: %lu KB of its pieces let go for the structure bsp switch (%lu KB kept)",
		held / 1024, h2_cache_segment_bytes(runtime_cache) / 1024);
}

H2_ALIGN static void h2_scripts_runtime_cache_open(void)
{
	char const *name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
	char why[H2_CACHE_WHY_SIZE];

	h2_scripts_runtime_cache_close();
	if (!name || strncmp(name, "h2\\", 3) || getenv("HALO_H2_NO_SCRIPT_CACHE"))
		return;
	/* (an open of its own, never the load's kept one: the scripts, the
	cinematics and the AI read it on the tick's thread through the play,
	while the game thread's users of the kept one - the HUD made after the
	load, h2_hud_make.c, its window hooks and 2 MB budget put on it, all its
	segments let go as they changed - read and evict on another thread: the
	03a intro's faces and emotions read pieces the HUD's make had just let
	go (the dev Vita's halts at tick 80: csmemcpy's overlap assert in
	mcc_bytes, a data abort in h2_cinematics_face_start)) */
	runtime_cache = h2_cache_open_own(h2_level_maps_directory(), name + 3, why, sizeof(why));
	if (!runtime_cache)
	{
		platform_log("h2 scripts: %s.map cannot be read (no text or cinematics): %s", name + 3, why);
		return;
	}
	/* (its pieces let go past a few MB: the animations are read as played) */
	h2_cache_segment_budget(runtime_cache, 8UL * 1024UL * 1024UL);
	h2_script_text_load(runtime_cache);
	/* (what the text's load inflated let go: the play reads its pieces as
	it needs them, as the kept one's trim at the load's end left it) */
	h2_cache_trim(runtime_cache);
}

boolean h2_scripts_level(void)
{
	return h2_rules_level();
}

boolean h2_scripts_postprocess(
	struct scenario *scenario)
{
	struct conversion c;
	long i;

	memset(&c, 0, sizeof(c));
	c.scenario = scenario;
	c.syntax = (struct data_array *)scenario->hs_syntax_data.address;
	c.nodes = (struct hs_syntax_node *)((char *)c.syntax + sizeof(struct data_array));
	c.node_count = c.syntax->count;
	c.strings = scenario->hs_string_constants.address;
	c.string_bytes = scenario->hs_string_constants.size;
	/* (once a load: the strings' offsets are made pointers) */
	if (!strcmp(c.syntax->name, "h2 script node"))
		return TRUE;
	/* the object names given their placements, the ones not placed at the
	start too ("not automatically": a script makes them by name). The
	shipped maps' come so from the tool; the builder's names do not */
	object_names_postprocess(scenario, FALSE);
	h2_scripts_globals.count = 0;
	h2_scripts_globals.sound_count = 0;
	h2_scripts_globals.hidden_count = 0;
	h2_scripts_globals.cinematic_lit_count = 0;
	kill_volumes_forget();
	h2_scripts_globals.kills = 0;
	weather_reset();
	h2_scripts_globals.variable_count = 0;
	h2_scripts_object_function_slots_clear();
	h2_scripts_globals.effect_count = 0;
	h2_scripts_globals.effects_played = 0;
	h2_scripts_globals.effects_missing = 0;
	for (i = 0; i < c.node_count && h2_scripts_globals.effect_count < (long)NUMBEROF(h2_scripts_globals.effects); i++)
	{
		struct hs_syntax_node const *node = &c.nodes[i];
		char const *name;
		char tag_name[256];
		long k;

		if (!node->datum_header || !(node->flags & NODE_PRIMITIVE) || (node->flags & NODE_GLOBAL) ||
			node->constant_type != _hs_type_effect || node->data == NONE || !(name = string_at(&c, node->string_offset)))
		{
			continue;
		}
		for (k = 0; k < h2_scripts_globals.effect_count && h2_scripts_globals.effects[k].handle != (unsigned long)node->data; k++)
			;
		if (k < h2_scripts_globals.effect_count)
			continue;
		snprintf(tag_name, sizeof(tag_name), "h2\\%s", name);
		h2_scripts_globals.effects[k].handle = (unsigned long)node->data;
		h2_scripts_globals.effects[k].tag_index = tag_loaded('effe', tag_name);
		h2_scripts_globals.effect_count++;
	}
	h2_scripts_globals.sounds_played = 0;
	h2_scripts_globals.sounds_not_carried = 0;
	for (i = 0; i < c.node_count && h2_scripts_globals.sound_count < (long)NUMBEROF(h2_scripts_globals.sounds); i++)
	{
		struct hs_syntax_node const *node = &c.nodes[i];
		char const *name;
		long k;

		if (!node->datum_header || !(node->flags & NODE_PRIMITIVE) || (node->flags & NODE_GLOBAL) ||
			node->constant_type != _hs_type_sound || node->data == NONE || !(name = string_at(&c, node->string_offset)))
		{
			continue;
		}
		for (k = 0; k < h2_scripts_globals.sound_count && h2_scripts_globals.sounds[k].handle != (unsigned long)node->data; k++)
			;
		if (k < h2_scripts_globals.sound_count)
			continue;
		h2_scripts_globals.sounds[k].handle = (unsigned long)node->data;
		h2_scripts_globals.sounds[k].name = !strncmp(name, "sound\\", 6) ? name + 6 : name;
		h2_scripts_globals.sound_count++;
	}
	/* the navpoints: this engine's HUD globals' arrows, by name (the first
when none is so named) */
	{
		long hud_globals_index = interface_get_tag_index(_interface_hud_globals);
		struct tag_block const *arrows = hud_globals_index != NONE ?
			&((struct hud_globals_definition *)tag_get('hudg', hud_globals_index))->waypoint.arrows : NULL;

		for (i = 0; i < c.node_count; i++)
		{
			struct hs_syntax_node *node = &c.nodes[i];
			char const *name;
			long k;

			if (!node->datum_header || !(node->flags & NODE_PRIMITIVE) || (node->flags & NODE_GLOBAL) ||
				node->constant_type != _hs_type_navpoint)
			{
				continue;
			}
			name = string_at(&c, node->string_offset);
			node->data = arrows && arrows->count > 0 ? 0 : NONE;
			for (k = 0; arrows && name && k < arrows->count; k++)
			{
				if (!strcmp((char const *)arrows->address + 0x68 * k, name))
				{
					node->data = k;
					break;
				}
			}
		}
	}
	h2_scripts_globals.loop_count = 0;
	h2_scripts_globals.loops_started = 0;
	h2_scripts_globals.loops_not_carried = 0;
	for (i = 0; i < c.node_count && h2_scripts_globals.loop_count < (long)NUMBEROF(h2_scripts_globals.loops); i++)
	{
		struct hs_syntax_node const *node = &c.nodes[i];
		char const *name;
		long k;

		if (!node->datum_header || !(node->flags & NODE_PRIMITIVE) || (node->flags & NODE_GLOBAL) ||
			node->constant_type != _hs_type_looping_sound || node->data == NONE || !(name = string_at(&c, node->string_offset)))
		{
			continue;
		}
		for (k = 0; k < h2_scripts_globals.loop_count && h2_scripts_globals.loops[k].handle != (unsigned long)node->data; k++)
			;
		if (k < h2_scripts_globals.loop_count)
			continue;
		h2_scripts_globals.loops[k].handle = (unsigned long)node->data;
		h2_scripts_globals.loops[k].name = name;
		h2_scripts_globals.loop_count++;
	}
	/* (HALO_H2_SCRIPT_DUMP=<script>: its compiled tree logged as text, the
	names its nodes have - the MCC map's own script, for a test) */
	/* (HALO_H2_SCRIPT_DUMP_FILE=<path>, test builds: every script written
	there (when no file is there yet), one a line - name, type, text - with no length
	cut: the campaign auto-run harness's expected timelines,
	tools/h2run/expected.py) */
#ifndef HALO_RELEASE
	/* (the first level loaded only: a file there already is kept - the
	harness's next level, loaded after the win, is not the one run) */
	FILE *dump_file = NULL;

	if (getenv("HALO_H2_SCRIPT_DUMP_FILE"))
	{
		FILE *there = fopen(getenv("HALO_H2_SCRIPT_DUMP_FILE"), "r");

		if (there)
			fclose(there);
		else
			dump_file = fopen(getenv("HALO_H2_SCRIPT_DUMP_FILE"), "w");
	}
	static char *dump_text;
	enum { DUMP_FILE_TEXT = 1 << 20 };

	if (dump_file && !dump_text)
		dump_text = malloc(DUMP_FILE_TEXT);
	if (dump_file && !dump_text)
	{
		fclose(dump_file);
		dump_file = NULL;
	}
#else
	FILE *dump_file = NULL;
	static char *dump_text;
	enum { DUMP_FILE_TEXT = 1 };
#endif
	if (getenv("HALO_H2_SCRIPT_DUMP") || dump_file)
	{
		long script_index;

		/* (the globals: each one's type and initial-value node, raw) */
		for (script_index = 0; script_index < scenario->hs_globals.count; script_index++)
		{
			struct hs_global const *global = TAG_BLOCK_GET_ELEMENT(&scenario->hs_globals, script_index, struct hs_global);
			struct hs_syntax_node const *node = node_get(&c, global->initialization_expression_index);

			/* (only HALO_H2_SCRIPT_DUMP's globals: the dump file alone -
			tools/h2run.sh's - halted every run here on a NULL compare) */
			if (!getenv("HALO_H2_SCRIPT_DUMP") || (strcmp(getenv("HALO_H2_SCRIPT_DUMP"), "*") && strcmp(getenv("HALO_H2_SCRIPT_DUMP"), "globals")))
				break;
			platform_log("h2 script dump: global %ld %s type %d init node %lx: %s", script_index, global->name, (int)global->type,
				(unsigned long)global->initialization_expression_index,
				node ? "" : "(none)");
			if (node)
				platform_log("h2 script dump:   flags %x type %d index/constant_type %d data %lx next %lx", (unsigned)node->flags,
					(int)node->type, (int)node->function_index, (unsigned long)node->data, (unsigned long)node->next_node_index);
		}

		for (script_index = 0; script_index < scenario->hs_scripts.count; script_index++)
		{
			struct hs_script const *script = TAG_BLOCK_GET_ELEMENT(&scenario->hs_scripts, script_index, struct hs_script);
			static char log_text[16384];
			char *text = dump_file ? dump_text : log_text;
			long const text_size = dump_file ? (long)DUMP_FILE_TEXT : (long)sizeof(log_text);
			long length = 0;
			long stack[512], depth = 0, visited = 0;

			if (!dump_file && strcmp(getenv("HALO_H2_SCRIPT_DUMP"), "*") && strcmp(script->name, getenv("HALO_H2_SCRIPT_DUMP")))
				continue;
			/* (a node index, or -2 for a closing parenthesis) */
			stack[depth++] = script->root_expression_index & 0xFFFF;
			while (depth > 0 && visited++ < 65536 && length < text_size - 300)
			{
				long index = stack[--depth];
				struct hs_syntax_node const *node;
				char const *name;

				if (index == -2)
				{
					length += snprintf(text + length, text_size - length, ")");
					continue;
				}
				if (index < 0 || index >= c.node_count)
					continue;
				node = &c.nodes[index];
				if (node->next_node_index != NONE && depth < (long)NUMBEROF(stack))
					stack[depth++] = node->next_node_index & 0xFFFF;
				if (!(node->flags & NODE_PRIMITIVE))
				{
					length += snprintf(text + length, text_size - length, " (");
					if (depth < (long)NUMBEROF(stack))
						stack[depth++] = -2;
					if (node->data != NONE && depth < (long)NUMBEROF(stack))
						stack[depth++] = node->data & 0xFFFF;
				}
				else
				{
					name = string_at(&c, node->string_offset);
					if (name && *name)
						length += snprintf(text + length, text_size - length, " %.120s", name);
					else if (node->type == _hs_type_real)
						length += snprintf(text + length, text_size - length, " %g", (double)*(float const *)&node->data);
					else
						length += snprintf(text + length, text_size - length, " #%ld", (long)node->data);
				}
			}
			text[length] = 0;
			if (dump_file)
				fprintf(dump_file, "%s\t%d\t%s\n", script->name, (int)script->script_type, text);
			else
				platform_log("h2 script dump: %.32s:%s", script->name, text);
		}
	}
	if (dump_file)
		fclose(dump_file);
	/* (HALO_H2_LOOP_LOG: which script names which looping sound, to wake it in a test) */
	if (getenv("HALO_H2_LOOP_LOG"))
	{
		long script_index;

		for (script_index = 0; script_index < scenario->hs_scripts.count; script_index++)
		{
			struct hs_script const *script = TAG_BLOCK_GET_ELEMENT(&scenario->hs_scripts, script_index, struct hs_script);
			long stack[256], depth = 0, visited = 0;

			stack[depth++] = script->root_expression_index & 0xFFFF;
			while (depth > 0 && visited++ < 65536)
			{
				long index = stack[--depth];
				struct hs_syntax_node const *node;
				char const *name;

				if (index < 0 || index >= c.node_count)
					continue;
				node = &c.nodes[index];
				if (node->next_node_index != NONE && depth < (long)NUMBEROF(stack))
					stack[depth++] = node->next_node_index & 0xFFFF;
				if (!(node->flags & NODE_PRIMITIVE))
				{
					if (node->data != NONE && depth < (long)NUMBEROF(stack))
						stack[depth++] = node->data & 0xFFFF;
				}
				else if (!(node->flags & NODE_GLOBAL) && node->constant_type == _hs_type_looping_sound &&
					(name = string_at(&c, node->string_offset)) != NULL && strchr(name, '\\'))
				{
					platform_log("h2 script loop: script %.32s names %s", script->name, name);
				}
			}
		}
	}
	h2_scripts_globals.requested_bsp = 0;
	h2_scripts_globals.total_calls = 0;
	h2_scripts_globals.calls_at_last_report = 0;
	h2_scripts_globals.last_report_time = 0;
	snprintf(h2_scripts_globals.map_name, sizeof(h2_scripts_globals.map_name), "%s",
		global_scenario_index != NONE ? tag_get_name(global_scenario_index) : "?");

	/* the short constants' whole value: the MCC maps' compiled nodes hold a
	short constant in the value's low word with 0xFFFF above it (a 1 arrives
	as 0xFFFF0001), which the readers of a short see through, but the
	engine's readers of the whole slot do not: sleep_until's timeout - its
	third argument, (sleep_until <cond> 30 one_minute) - compared as a long
	was a negative number, so every timed wait ended at its first period (the
	encounters' waves one second apart; Halo 2's own nodes are whole values,
	hs_scheduler reads them as longs). Each short constant, and so each short
	global's initial value, is made the sign-extended whole value */
	for (i = 0; i < c.node_count && !HALO_ENV_ONCE("HALO_H2_SHORT_CONSTANTS_RAW"); i++)
	{
		struct hs_syntax_node *node = &c.nodes[i];

		if (!node->datum_header || !(node->flags & NODE_PRIMITIVE) || (node->flags & NODE_GLOBAL))
			continue;
		if (node->constant_type == _hs_type_short_integer)
			node->data = (long)(short)node->data;
		else if (node->constant_type == _hs_type_boolean)
			node->data = (long)(unsigned char)node->data;
	}
	/* the engine globals: made constants (Halo 2's AI and cutscene ones
	this engine has not), and a set of one sets nothing (first: the set's
	variable is its first argument) */
	for (i = 0; i < c.node_count; i++)
	{
		struct hs_syntax_node *node = &c.nodes[i];
		struct hs_syntax_node *name_node, *variable;
		char const *name;

		if (!node->datum_header || (node->flags & (NODE_PRIMITIVE | NODE_SCRIPT)) ||
			!(name_node = node_get(&c, node->data)) || !(name = string_at(&c, name_node->string_offset)) || strcmp(name, "set"))
		{
			continue;
		}
		variable = node_get(&c, name_node->next_node_index);
		if (variable && (variable->flags & NODE_GLOBAL) && (variable->short_value & EXTERNAL_GLOBAL))
			node_make_inert(&c, node);
	}
	for (i = 0; i < c.node_count; i++)
	{
		struct hs_syntax_node *node = &c.nodes[i];

		if (!node->datum_header || !(node->flags & NODE_PRIMITIVE) || !(node->flags & NODE_GLOBAL))
			continue;
		if (node->short_value & EXTERNAL_GLOBAL)
		{
			char const *name = string_at(&c, node->string_offset);
			long value;

			node_make_inert(&c, node);
			/* (the AI's constants Halo 2 keeps as engine globals: their
			values, h2_ai.c) */
			/* (ai_current_actor and ai_current_squad name an actor only as
			an AI value: cast to an object list - Halo 2 casts AI to lists,
			04a's volume tests - they stay the list's default, none) */
			if (name && h2_ai_external_global_value(name, &value) &&
				(node->type != _hs_type_object_list || strncmp(name, "ai_current_", 11)))
			{
				node->data = value;
			}
		}
		else if (node->short_value < 0 || node->short_value >= scenario->hs_globals.count)
			return FALSE;
	}
	/* the calls */
	for (i = 0; i < c.node_count; i++)
	{
		struct hs_syntax_node *node = &c.nodes[i];

		if (!node->datum_header || (node->flags & (NODE_PRIMITIVE | NODE_SCRIPT)) || !hs_type_valid(node->type))
			continue;
		if (!call_convert(&c, node))
		{
			platform_log("h2 scripts: script node %ld names no function; the scripts won't run", i);
			return FALSE;
		}
	}
	/* (vehicle_load_magic's units given as one object - 03a's Ghosts loaded
	into its Shadows -: the object itself passed, its evaluation, h2_ai.c,
	telling an object from a list) */
	for (i = 0; i < c.node_count; i++)
	{
		struct hs_syntax_node *node = &c.nodes[i], *name_node, *argument;
		char const *name;
		short n;

		if (!node->datum_header || (node->flags & (NODE_PRIMITIVE | NODE_SCRIPT)) ||
			!(name_node = node_get(&c, node->data)) || !(name = string_at(&c, name_node->string_offset)) ||
			strcmp(name, "vehicle_load_magic"))
		{
			continue;
		}
		for (argument = name_node, n = 0; argument && n < 3; n++)
			argument = node_get(&c, argument->next_node_index);
		if (getenv("HALO_H2_AI_DEBUG"))
			platform_log("h2 scripts: vehicle_load_magic's units: node type %d, actual %d", argument ? argument->type : -1,
				argument ? node_actual_type(&c, argument) : -1);
		if (argument && argument->type == _hs_type_object_list && HS_TYPE_IS_OBJECT(node_actual_type(&c, argument)))
		{
			argument->type = node_actual_type(&c, argument);
			c.objects_as_lists++;
		}
	}
	/* the constants: a string's value its text's address; a value a cast
	of this engine's cannot make, or an AI's made an object list (Halo 2
	casts AI to lists), its type's default */
	for (i = 0; i < c.node_count; i++)
	{
		struct hs_syntax_node *node = &c.nodes[i];
		short actual;

		if (!node->datum_header || node->type == _hs_function_name || !hs_type_valid(node->type))
			continue;
		if ((node->flags & NODE_PRIMITIVE) && !(node->flags & NODE_GLOBAL) && node->constant_type == _hs_type_string)
		{
			/* (a string's text, a string id's name: where the node's text is) */
			char const *text = string_at(&c, node->string_offset);

			node->data = (long)(text ? text : "");
			if (node->type != _hs_type_string)
			{
				node_make_inert(&c, node);
				c.casts_inert++;
			}
			continue;
		}
		actual = node_actual_type(&c, node);
		if (actual == _hs_passthrough)
			continue;
		if (!hs_type_valid(actual) || !hs_can_cast(actual, node->type) ||
			(node->type == _hs_type_object_list && actual == _hs_type_ai))
		{
			/* (HALO_H2_SCRIPT_DUMP: each such node) */
			if (getenv("HALO_H2_SCRIPT_DUMP"))
			{
				platform_log("h2 script dump: node %ld made a constant: wanted type %d, actual %d, flags %x, %s %s", i, (int)node->type,
					(int)actual, (unsigned)node->flags,
					(node->flags & NODE_GLOBAL) ? "global" : (node->flags & NODE_PRIMITIVE) ? "constant" : (node->flags & NODE_SCRIPT) ? "script call" : "call",
					(node->flags & NODE_GLOBAL) ? hs_global_get_name((short)node->short_value) :
					(node->flags & (NODE_PRIMITIVE | NODE_SCRIPT)) ? "" :
					(node_get(&c, node->data) && string_at(&c, node_get(&c, node->data)->string_offset) ? string_at(&c, node_get(&c, node->data)->string_offset) : "?"));
			}
			node_make_inert(&c, node);
			c.casts_inert++;
		}
	}
	/* the level's own map, open while it is played (its text, its
	cinematics' animations) */
	h2_scripts_runtime_cache_open();
	/* (its models' render-only nodes: h2_render_only.c) */
	h2_render_only_map_loaded(h2_scripts_cache());
	/* (a campaign level's AI dialogue, read from that map: h2_dialogue.c) */
	h2_dialogue_map_loaded(h2_scripts_globals.map_name);
	/* (and what its walk over every model's and voice's tag inflated let go,
	as the text's: the dialogue copies what it keeps. Those pieces - up to
	the open's 8 MB budget, the C heap's - stayed through the level's start:
	the dev Vita's 01b, C heap 'world done' -> 'loaded' +10.5 MB with the
	dialogue vs +3.2 without, and Jolt's level mesh, made next, found no
	room (Nov 9: 'jolt: no memory for 64912 bytes', Balanced and Quality)) */
	if (runtime_cache)
	{
		unsigned long held = h2_cache_segment_bytes(runtime_cache);

		h2_cache_trim(runtime_cache);
		platform_log("h2 scripts: the level's map: %lu KB of its pieces read for the text and the dialogue let go (%lu KB kept)",
			held / 1024, h2_cache_segment_bytes(runtime_cache) / 1024);
	}
	/* (marked: the nodes are this engine's now) */
	snprintf(c.syntax->name, sizeof(c.syntax->name), "h2 script node");
	platform_log("h2 scripts: %s: %ld nodes; calls given this engine's functions %ld, Halo 2's of h2_scripts.c %ld, "
		"no-ops %ld, stand-ins %ld (ai %ld, cinematic %ld, sound %ld, other %ld); %ld engine globals and %ld casts made constants; "
		"%ld distinct functions named",
		h2_scripts_globals.map_name, c.syntax->actual_count, c.calls[_kind_engine], c.calls[_kind_halo2], c.calls[_kind_no_op],
		c.calls[_kind_ai] + c.calls[_kind_cinematic] + c.calls[_kind_sound] + c.calls[_kind_other],
		c.calls[_kind_ai], c.calls[_kind_cinematic], c.calls[_kind_sound], c.calls[_kind_other],
		c.made_inert - c.casts_inert, c.casts_inert, h2_scripts_globals.count);
	report("made ready");

	return TRUE;
}

/* ---------- the runtime: counts */

static int function_calls_compare(void const *a, void const *b)
{
	struct h2_function const *x = *(struct h2_function const *const *)a, *y = *(struct h2_function const *const *)b;

	return x->calls != y->calls ? (x->calls < y->calls ? 1 : -1) : strcmp(x->name, y->name);
}

static void report(char const *why)
{
	struct h2_function **sorted;
	char const *path = getenv("HALO_H2_SCRIPT_STATUS");
	FILE *file = NULL;
	long i, kinds[NUMBER_OF_KINDS] = { 0 }, cinematic_counts[3];

	if (!h2_scripts_globals.count)
		return;
	h2_cinematics_counts(&cinematic_counts[0], &cinematic_counts[1], &cinematic_counts[2]);
	sorted = malloc(h2_scripts_globals.count * sizeof(*sorted));
	if (!sorted)
		return;
	for (i = 0; i < h2_scripts_globals.count; i++)
		sorted[i] = &h2_scripts_globals.functions[i];
	qsort(sorted, h2_scripts_globals.count, sizeof(*sorted), function_calls_compare);
	if (path && *path)
		file = fopen(path, "w");
	if (file)
		fprintf(file, "# %s (%s): each function the scripts name: what runs it (engine: this engine's - its calls counted only when skipped for an object that is not there), calls, call sites\n# name\tkind\tcalls\tcall sites\n",
			h2_scripts_globals.map_name, why);
	for (i = 0; i < h2_scripts_globals.count; i++)
	{
		kinds[sorted[i]->kind] += sorted[i]->calls;
		if (file)
			fprintf(file, "%s\t%s\t%ld\t%ld\n", sorted[i]->name, kind_names[sorted[i]->kind], sorted[i]->calls, sorted[i]->call_sites);
	}
	if (file)
		fclose(file);
	platform_log("h2 scripts: %s (%s): calls of Halo 2's functions %ld, no-ops %ld, stand-ins ai %ld cinematic %ld sound %ld other %ld; "
		"script sounds played %ld, not carried by a pool sound %ld; looping sounds started %ld, not carried %ld; effects made %ld, not made here %ld; "
		"cinematic animations played %ld, not found %ld, of objects not there %ld",
		h2_scripts_globals.map_name, why, kinds[_kind_halo2], kinds[_kind_no_op], kinds[_kind_ai], kinds[_kind_cinematic],
		kinds[_kind_sound], kinds[_kind_other], h2_scripts_globals.sounds_played, h2_scripts_globals.sounds_not_carried,
		h2_scripts_globals.loops_started, h2_scripts_globals.loops_not_carried, h2_scripts_globals.effects_played, h2_scripts_globals.effects_missing, cinematic_counts[0], cinematic_counts[1], cinematic_counts[2]);
	{
		char line[1024];
		int used = 0, shown = 0;

		for (i = 0; i < h2_scripts_globals.count && shown < 24; i++)
		{
			if (!sorted[i]->calls || sorted[i]->kind == _kind_no_op || sorted[i]->kind == _kind_halo2 || sorted[i]->kind == _kind_engine)
				continue;
			used += snprintf(line + used, sizeof(line) - used, "%s%s %ld", shown ? ", " : "", sorted[i]->name, sorted[i]->calls);
			if (used >= (int)sizeof(line))
				break;
			shown++;
		}
		if (shown)
			platform_log("h2 scripts: stand-ins called most: %s", line);
	}
	free(sorted);
}

/* the players' units in an enabled kill volume killed */
/* a player's unit outside the loaded structure BSP (fallen out of the
world, or put there) a second: killed, as Halo 2 kills a player who falls
out of its level - or, with cheat_deathless_player, put back where it last
stood in the world. Nothing halts on a unit left outside */
static void players_out_of_world_update(void)
{
	enum { PLAYERS = 4 };
	static real_point3d last_inside[PLAYERS];
	static long outside_ticks[PLAYERS];
	static boolean inside_seen[PLAYERS];
	static void const *scenario_seen;
	long player_index;
	short k = 0;

	if (scenario_seen != global_scenario_get())
	{
		scenario_seen = global_scenario_get();
		memset(outside_ticks, 0, sizeof(outside_ticks));
		memset(inside_seen, 0, sizeof(inside_seen));
	}
	if (cinematic_in_progress())
		return;
	for (player_index = data_next_index(player_data, NONE); player_index != NONE && k < PLAYERS;
		player_index = data_next_index(player_data, player_index), k++)
	{
		struct player_datum *player = player_get(player_index);
		struct object_datum *unit;

		if (player->unit_index == NONE || !(unit = object_try_and_get(player->unit_index)) || object_dead(unit) ||
			unit->object.parent_object_index != NONE)
		{
			outside_ticks[k] = 0;
			continue;
		}
		if (unit->object.location.cluster_index != NONE)
		{
			outside_ticks[k] = 0;
			last_inside[k] = unit->object.position;
			inside_seen[k] = TRUE;
			continue;
		}
		/* (falling: a unit outside the loaded BSP's clusters but on its feet -
		05a's temple door seam - is left alone) */
		if (unit->object.type == _object_type_biped && biped_get(player->unit_index)->biped.airborne_ticks < 30)
		{
			outside_ticks[k] = 0;
			continue;
		}
		if (++outside_ticks[k] < 30)
			continue;
		outside_ticks[k] = 0;
		if (cheat.deathless_player && inside_seen[k])
		{
			platform_log("h2 scripts: player %ld outside the world: put back at %.1f %.1f %.1f", player_index,
				last_inside[k].x, last_inside[k].y, last_inside[k].z);
			object_set_position(player->unit_index, &last_inside[k], NULL, NULL);
		}
		else if (!cheat.deathless_player)
		{
			platform_log("h2 scripts: player %ld outside the world: killed", player_index);
			unit_kill(player->unit_index);
		}
	}
}

/* (the scenario loaded: its kill triggers enabled) Halo 2's scenario kill
triggers (its scenario's block at 0x230, each a trigger volume; the
volume's own kill index at 0x40, which h2_level.c marks in this engine's
volume's unused bytes 0x2C) are all enabled as a level begins (upstream
halo2-decompiled unknown_1eb8a0.cpp 0x1ec480: its disabled bits cleared);
kill_volume_disable sets a volume's bit, kill_volume_enable clears it. A
multiplayer level's - Lockout's pit, Zanzibar's sea, Ascension's and
Warlock's drops - as a campaign level's */
static struct scenario const *kill_volumes_scenario_seen;

static void kill_volumes_defaults(void)
{
	struct scenario const *scenario = global_scenario_get();
	long k, count = 0;

	if (scenario == kill_volumes_scenario_seen)
		return;
	kill_volumes_scenario_seen = scenario;
	h2_scripts_globals.kill_volume_count = 0;
	if (!scenario || !h2_rules_level())
		return;
	for (k = 0; k < scenario->trigger_volumes.count && k < (long)NUMBEROF(h2_scripts_globals.kill_volumes); k++)
	{
		struct scenario_trigger_volume const *volume = TAG_BLOCK_GET_ELEMENT(&scenario->trigger_volumes, k,
			struct scenario_trigger_volume);

		if (*(unsigned short const *)(volume->unused + 8) == 0xB7B8)
		{
			h2_scripts_globals.kill_volumes[h2_scripts_globals.kill_volume_count++] = (short)k;
			count++;
		}
	}
	if (count)
		platform_log("h2 scripts: %ld kill volume%s enabled (the scenario's kill triggers)", count, count == 1 ? "" : "s");
}

/* (h2_scripts_game_state_reverted, a map left) the defaults read again
at the next update */
static void kill_volumes_forget(void)
{
	h2_scripts_globals.kill_volume_count = 0;
	kill_volumes_scenario_seen = NULL;
}

/* Halo 2's kill volumes (upstream halo2-decompiled unknown_1eb8a0.cpp
function_1ec500, bipeds.cpp 0x118b80's biped update, unknown_118e80.cpp
0x118b80's object update): every biped in an enabled one - a player's, a
bot's, an AI's, riding or not - dies (Halo 2 deals it the globals' kill
damage); every other object with physics in one - a vehicle driven off
Zanzibar's sea wall, a weapon dropped into Lockout's pit - is flagged
away and gone. The machine that runs the game alone: the deaths and the
objects' going reach a joiner as every other's */
static void kill_volumes_update(void)
{
	struct object_iterator iterator;
	struct object_datum *object;

	/* (cheat_deathless_player: no kill volume kills the players either) */
	if (cheat.deathless_player || game_connection() == _game_connection_network_client)
		return;

	object_iterator_new(&iterator, _object_mask_unit | _object_mask_item, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		long k;

		if (object->object.type == _object_type_biped)
		{
			if (object_dead(object))
				continue;
		}
		else if (object->object.parent_object_index != NONE)
			continue;
		for (k = 0; k < h2_scripts_globals.kill_volume_count; k++)
		{
			if (!scenario_trigger_volume_test_object(h2_scripts_globals.kill_volumes[k], iterator.index))
				continue;
			if (object->object.type == _object_type_biped)
			{
				long player_index = ((struct unit_datum *)object)->unit.player_index;

				platform_log("h2 scripts: kill volume %d killed %s %08lx%s", h2_scripts_globals.kill_volumes[k],
					tag_get_name(object->definition_index), (unsigned long)iterator.index,
					player_index != NONE ? " (a player's)" : "");
				unit_kill(iterator.index);
				h2_scripts_globals.kills++;
			}
			else
			{
				platform_log("h2 scripts: kill volume %d took %s %08lx", h2_scripts_globals.kill_volumes[k],
					tag_get_name(object->definition_index), (unsigned long)iterator.index);
				object_delete(iterator.index);
			}
			break;
		}
	}
}

/* whether the object is lit by the cinematic's lights (or sits on one that is) */
boolean h2_scripts_object_cinematic_lit(long object_index)
{
	long i, depth;

	if (!h2_scripts_globals.cinematic_lit_count)
		return FALSE;
	for (depth = 0; depth < 4 && object_index != NONE; depth++)
	{
		struct object_datum *object;

		for (i = 0; i < h2_scripts_globals.cinematic_lit_count; i++)
		{
			if (h2_scripts_globals.cinematic_lit[i] == object_index)
				return TRUE;
		}
		object = object_try_and_get(object_index);
		object_index = object ? object->object.parent_object_index : NONE;
	}
	return FALSE;
}

boolean h2_scripts_cinematic_lighting(
	long object_index,
	float ambient[3],
	float primary_direction[3],
	float primary[3],
	float secondary_direction[3],
	float secondary[3])
{
	long i, depth;

	if (HALO_ENV_ONCE("HALO_H2_NO_CINEMATIC_LIGHTING") || !h2_scripts_globals.cinematic_lit_count)
		return FALSE;
	/* (an object attached to a lit one - its weapon - lit with it) */
	for (depth = 0; depth < 4 && object_index != NONE; depth++)
	{
		struct object_datum *object;

		for (i = 0; i < h2_scripts_globals.cinematic_lit_count; i++)
		{
			if (h2_scripts_globals.cinematic_lit[i] == object_index)
			{
				int k;

				for (k = 0; k < 2; k++)
				{
					float const *light = k ? h2_scripts_globals.cinematic_secondary : h2_scripts_globals.cinematic_primary;
					float *direction = k ? secondary_direction : primary_direction, *colour = k ? secondary : primary;
					float pitch = light[0] * 0.017453293f, yaw = light[1] * 0.017453293f;

					/* (the light's travel: from its pitch and yaw towards
					the object - 03a's sun 43 up at yaw 122, its warm primary -:
					MCC halo2.dll 0x1808d0950 negates the vector too) */
					direction[0] = -cosf(pitch) * cosf(yaw);
					direction[1] = -cosf(pitch) * sinf(yaw);
					direction[2] = -sinf(pitch);
					memcpy(colour, light + 2, 3 * sizeof(float));
				}
				memcpy(ambient, h2_scripts_globals.cinematic_ambient, 3 * sizeof(float));
				return TRUE;
			}
		}
		object = object_try_and_get(object_index);
		object_index = object ? object->object.parent_object_index : NONE;
	}
	return FALSE;
}

/* (a test's, main.c "@hideplayer": object_hide's list from the harness -
the tag test's (object_hide (player0) 1) before its shots) */
void h2_scripts_object_hide(
	long object_index,
	boolean hide)
{
	long i;

	if (object_index == NONE)
		return;
	for (i = 0; i < h2_scripts_globals.hidden_count && h2_scripts_globals.hidden[i] != object_index; i++)
		;
	if (hide && i == h2_scripts_globals.hidden_count && h2_scripts_globals.hidden_count < (long)NUMBEROF(h2_scripts_globals.hidden))
		h2_scripts_globals.hidden[h2_scripts_globals.hidden_count++] = object_index;
	else if (!hide && i < h2_scripts_globals.hidden_count)
		h2_scripts_globals.hidden[i] = h2_scripts_globals.hidden[--h2_scripts_globals.hidden_count];
}

boolean h2_scripts_object_hidden(
	long object_index)
{
	long i, depth;

	if (HALO_ENV_ONCE("HALO_H2_NO_OBJECT_HIDE"))
		return FALSE;
	for (depth = 0; depth < 4 && object_index != NONE && h2_scripts_globals.hidden_count; depth++)
	{
		struct object_datum *object;

		for (i = 0; i < h2_scripts_globals.hidden_count; i++)
		{
			if (h2_scripts_globals.hidden[i] == object_index)
				return TRUE;
		}
		object = object_try_and_get(object_index);
		object_index = object ? object->object.parent_object_index : NONE;
	}
	return FALSE;
}

/* (perf/h2-tick-gap) HALO_TICK_PROFILE: hs_update's time by part - the
script threads' walk and the lists' collection (hs_runtime.c), then this
update's Halo 2 systems - every 300 ticks, "hs-split (ms/tick)" */
static const char *const hs_split_names[NUMBER_OF_HS_SPLITS] = { "threads", "list_gc", "h2_ai", "dialogue",
	"kill_volumes", "devices", "out_of_world", "triggers", "skulls", "cinematics", "variables", "rest" };
static unsigned long long hs_split_us[NUMBER_OF_HS_SPLITS], hs_split_last;
static unsigned long hs_split_ticks;
static int hs_split_enabled = -1;

void h2_scripts_split_mark(int step)
{
	unsigned long long now;

	if (hs_split_enabled < 0)
	{
		const char *setting = getenv("HALO_TICK_PROFILE");

		hs_split_enabled = setting && atoi(setting) != 0;
	}
	if (hs_split_enabled <= 0)
		return;
	now = vita_host_time_us();
	if (step >= 0 && step < NUMBER_OF_HS_SPLITS)
		hs_split_us[step] += now - hs_split_last;
	hs_split_last = now;
}

static void hs_split_report(void)
{
	if (hs_split_enabled <= 0)
		return;
	if (++hs_split_ticks >= 300)
	{
		char line[400];
		int n = 0, step;
		unsigned long long total = 0;

		for (step = 0; step < NUMBER_OF_HS_SPLITS; step++)
		{
			n += snprintf(line + n, sizeof(line) - (size_t)n, " %s %.3f", hs_split_names[step], hs_split_us[step] / 1000.0 / hs_split_ticks);
			total += hs_split_us[step];
			hs_split_us[step] = 0;
		}
		platform_log("hs-split (ms/tick): total %.3f |%s", total / 1000.0 / hs_split_ticks, line);
		hs_split_ticks = 0;
	}
}

void h2_scripts_update(void)
{
	long time;

	/* (the AI's orders and command scripts: h2_ai.c; its dialogue's delays, h2_dialogue.c) */
	h2_scripts_split_mark(-1);
	h2_ai_update();
	h2_scripts_split_mark(_hs_split_h2_ai);
	h2_dialogue_update();
	h2_scripts_split_mark(_hs_split_dialogue);
	/* (the scenario's kill triggers: a level with no scripts has them too -
	Lockout's pit) */
	kill_volumes_defaults();
	if (!h2_scripts_globals.count)
	{
		if (h2_scripts_globals.kill_volume_count)
			kill_volumes_update();
		h2_scripts_split_mark(_hs_split_kill_volumes);
		hs_split_report();
		return;
	}
	device_animations_update();
	h2_scripts_split_mark(_hs_split_devices);
	/* (a campaign level's: a multiplayer game's rules keep their own) */
	if (!game_engine_running())
		players_out_of_world_update();
	h2_scripts_split_mark(_hs_split_out_of_world);
	/* (tests, HALO_NAMES_LOG: what the scripts make and destroy, a local
	game's - a network game's host logs it as it sends it) */
	if (game_connection() == _game_connection_local && HALO_ENV_ONCE("HALO_NAMES_LOG") && game_time_get() % 15 == 0)
		network_coop_names_log("local");
	if (h2_scripts_globals.kill_volume_count)
		kill_volumes_update();
	h2_scripts_split_mark(_hs_split_kill_volumes);
	held_triggers_update();
	h2_scripts_split_mark(_hs_split_triggers);
	h2_skulls_update();
	h2_scripts_split_mark(_hs_split_skulls);
	h2_cinematics_update();
	h2_scripts_split_mark(_hs_split_cinematics);
	if (h2_scripts_globals.variable_count)
		object_variables_update();
	h2_scripts_split_mark(_hs_split_variables);
	if (h2_scripts_globals.hud_fade_pending && game_time_get() >= h2_scripts_globals.hud_fade_at)
	{
		h2_scripts_globals.hud_fade_pending = FALSE;
		scripted_show_hud(h2_scripts_globals.hud_fade_show);
	}
	time = game_time_get();
	if (time - h2_scripts_globals.last_report_time >= REPORT_TICKS || time < h2_scripts_globals.last_report_time)
	{
		h2_scripts_globals.last_report_time = time;
		if (h2_scripts_globals.total_calls != h2_scripts_globals.calls_at_last_report)
		{
			h2_scripts_globals.calls_at_last_report = h2_scripts_globals.total_calls;
			report("so far");
		}
	}
	h2_scripts_split_mark(_hs_split_rest);
	hs_split_report();
}

void h2_scripts_dispose_from_old_map(void)
{
	/* (what a level's scripts set outside the game state ends with it) */
	if (h2_scripts_globals.standard_gravity != 0.0f)
		global_gravity = h2_scripts_globals.standard_gravity;
	h2_scripts_globals.standard_gravity = 0.0f;
	player_control_set_movement_disabled(FALSE);
	h2_bloom_override_enable(FALSE);
	h2_bloom_override_reset();
	h2_ai_dispose_from_old_map();
	memset(h2_device_animations, 0, sizeof(h2_device_animations));
	report("map left");
	h2_scripts_runtime_cache_close();
	h2_scripts_globals.count = 0;
	h2_scripts_globals.hidden_count = 0;
	h2_scripts_globals.cinematic_lit_count = 0;
	kill_volumes_forget();
}

/* (h2_sound.c, as a Halo 2 level loads, before its scripts are made
ready) the sounds the loaded scenario's scripts name, each once, in their
order, as the maps name them without "sound\\" */
long h2_scripts_sound_names(char const **names, long maximum);
long h2_scripts_sound_names(char const **names, long maximum)
{
	struct tag_iterator iterator;
	long scenario_index, i, count = 0;
	struct scenario *scenario;
	struct data_array const *syntax;
	struct hs_syntax_node const *nodes;
	char const *strings;
	long string_bytes;

	if (getenv("HALO_H2_NO_SCRIPT_SOUNDS"))
		return 0;
	tag_iterator_new(&iterator, SCENARIO_TAG);
	scenario_index = tag_iterator_next(&iterator);
	if (scenario_index == NONE || strncmp(tag_get_name(scenario_index), "h2\\", 3))
		return 0;
	scenario = (struct scenario *)tag_get(SCENARIO_TAG, scenario_index);
	syntax = (struct data_array const *)scenario->hs_syntax_data.address;
	strings = scenario->hs_string_constants.address;
	string_bytes = scenario->hs_string_constants.size;
	if (!syntax || !strings || scenario->hs_syntax_data.size < (long)sizeof(*syntax) || syntax->size != sizeof(struct hs_syntax_node) ||
		scenario->hs_syntax_data.size < (long)sizeof(*syntax) + syntax->count * (long)sizeof(struct hs_syntax_node))
	{
		return 0;
	}
	nodes = (struct hs_syntax_node const *)(syntax + 1);
	for (i = 0; i < syntax->count && count < maximum; i++)
	{
		char const *name;
		long k;

		if (!nodes[i].datum_header || !(nodes[i].flags & NODE_PRIMITIVE) || (nodes[i].flags & NODE_GLOBAL) ||
			nodes[i].constant_type != _hs_type_sound || nodes[i].string_offset < 0 || nodes[i].string_offset >= string_bytes ||
			!memchr(strings + nodes[i].string_offset, 0, string_bytes - nodes[i].string_offset))
		{
			continue;
		}
		name = strings + nodes[i].string_offset;
		if (!strncmp(name, "sound\\", 6))
			name += 6;
		for (k = 0; k < count && strcmp(names[k], name); k++)
			;
		if (k == count)
			names[count++] = name;
	}
	/* (and the mission dialogue's lines the AI speaks, h2_ai.c: as many as
	there is room for, in the mission's order) */
	{
		extern long h2_ai_dialogue_sound_names(char const **names, long maximum);

		count += h2_ai_dialogue_sound_names(names + count, maximum - count);
	}
	return count;
}

/* (h2_sound.c) the looping sounds the loaded scenario's scripts name (music,
cutscene foley, horns), each once, in their order, as the maps name them */
long h2_scripts_looping_sound_names(char const **names, long maximum);
long h2_scripts_looping_sound_names(char const **names, long maximum)
{
	struct tag_iterator iterator;
	long scenario_index, i, count = 0;
	struct scenario *scenario;
	struct data_array const *syntax;
	struct hs_syntax_node const *nodes;
	char const *strings;
	long string_bytes;

	if (getenv("HALO_H2_NO_SCRIPT_LOOPS"))
		return 0;
	tag_iterator_new(&iterator, SCENARIO_TAG);
	scenario_index = tag_iterator_next(&iterator);
	if (scenario_index == NONE || strncmp(tag_get_name(scenario_index), "h2\\", 3))
		return 0;
	scenario = (struct scenario *)tag_get(SCENARIO_TAG, scenario_index);
	syntax = (struct data_array const *)scenario->hs_syntax_data.address;
	strings = scenario->hs_string_constants.address;
	string_bytes = scenario->hs_string_constants.size;
	if (!syntax || !strings || scenario->hs_syntax_data.size < (long)sizeof(*syntax) || syntax->size != sizeof(struct hs_syntax_node) ||
		scenario->hs_syntax_data.size < (long)sizeof(*syntax) + syntax->count * (long)sizeof(struct hs_syntax_node))
	{
		return 0;
	}
	nodes = (struct hs_syntax_node const *)(syntax + 1);
	for (i = 0; i < syntax->count && count < maximum; i++)
	{
		char const *name;
		long k;

		if (!nodes[i].datum_header || !(nodes[i].flags & NODE_PRIMITIVE) || (nodes[i].flags & NODE_GLOBAL) ||
			nodes[i].constant_type != _hs_type_looping_sound || nodes[i].string_offset < 0 || nodes[i].string_offset >= string_bytes ||
			!memchr(strings + nodes[i].string_offset, 0, string_bytes - nodes[i].string_offset))
		{
			continue;
		}
		name = strings + nodes[i].string_offset;
		/* (a tag's name has a folder: a function's name node is not one) */
		if (!strchr(name, '\\'))
			continue;
		for (k = 0; k < count && strcmp(names[k], name); k++)
			;
		if (k == count)
			names[count++] = name;
	}
	return count;
}

void h2_scripts_game_state_reverted(void)
{
	h2_scripts_globals.variable_count = 0;
	h2_scripts_globals.hidden_count = 0;
	h2_scripts_globals.cinematic_lit_count = 0;
	h2_bloom_override_enable(FALSE);
	h2_bloom_override_reset();
	kill_volumes_forget();
	h2_cinematics_reset();
	/* (the AI's command scripts and orders: h2_ai.c) */
	h2_ai_game_state_reverted();
}

void h2_scripts_set_bsp_switcher(
	boolean (*switcher)(short h2_bsp_index),
	short (*current)(void))
{
	h2_scripts_globals.bsp_switcher = switcher;
	h2_scripts_globals.bsp_current = current;
}

/* this engine's scenario's blocks where this file writes them */
typedef char ce_scenario_offsets[
	offsetof(struct scenario, object_names) == CE_SCENARIO_OBJECT_NAMES &&
	offsetof(struct scenario, trigger_volumes) == CE_SCENARIO_TRIGGER_VOLUMES &&
	offsetof(struct scenario, hs_syntax_data) == CE_SCENARIO_SYNTAX &&
	offsetof(struct scenario, hs_string_constants) == CE_SCENARIO_STRINGS &&
	offsetof(struct scenario, hs_scripts) == CE_SCENARIO_SCRIPTS &&
	offsetof(struct scenario, hs_globals) == CE_SCENARIO_GLOBALS &&
	offsetof(struct scenario, cutscene_flags) == CE_SCENARIO_CUTSCENE_FLAGS &&
	offsetof(struct scenario, cutscene_camera_points) == CE_SCENARIO_CAMERA_POINTS &&
	sizeof(struct hs_script) == CE_SCRIPT_SIZE &&
	sizeof(struct hs_global) == CE_GLOBAL_SIZE &&
	sizeof(struct scenario_trigger_volume) == CE_TRIGGER_VOLUME_SIZE &&
	sizeof(struct scenario_object_name) == CE_OBJECT_NAME_SIZE &&
	sizeof(struct scenario_cutscene_camera_point) == CE_CAMERA_POINT_SIZE &&
	sizeof(struct data_array) == CE_DATA_ARRAY_HEADER ? 1 : -1];
