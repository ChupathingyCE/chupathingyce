/*
LUA_OPENSAUCE.C

OpenSauce, as Lua scripts written for maps built on it see it
(lua_opensauce.h). Only while the loaded map is one built for OpenSauce.

The f0 to f5 script globals: haloce.exe defines them (real globals, as the
Xbox's) but gives them no storage, so on Custom Edition alone they are
nothing. OpenSauce gives them storage at its start: it points them at the
fields of the rasterizer's debug options with their names (OpenSauce's
Rasterizer.cpp, k_rasterizer_debug_table, indexes 0x55 to 0x5A of the
external globals, the options at 0x6249C0 in haloce.exe 1.0.10:
_EngineLayout.Rasterizer.inl, rasterizer.hpp's s_rasterizer_debug_options).
So with OpenSauce, f0 is at 0x624A38 and f5 at 0x624A4C, and a map's
scripts and a Lua script share them through memory. This game has them as
the Xbox had them, its own script globals in its own debug options
(hs_globals_external.c), so Twiddlings answers those addresses with them:
what a map's script set is what a Lua script reads, and the other way.
The fields just before them (0x624A24 to 0x624A37, which some scripts read
as f0 to f4 by mistake) are answered as OpenSauce has them: the profile
options this game has, and nothing for the effects level and the two words
after it, which Halo PC added.

Some of the functions and globals OpenSauce adds to the scripts, for a
script's execute_script to call (never a map's own scripts: they are found
only while execute_script runs, lua_scripting.h):
  - unit_data_get_integer: a unit's integer by OpenSauce's name for it
    (ObjectFieldDefinitions.inl: the grenade counts, the zoom levels, the
    weapon and seat indexes and the rest), from this game's unit;
  - its post processing (pp_*, PostProcessingScripting.cpp and its effect
    instances, c_effect_instance.cpp): the effect instances' active flags
    and fades (fading in seconds, as OpenSauce's do), bloom's values and the
    post-processing globals (pp_motion_blur_enabled and the rest) are kept
    as OpenSauce keeps them, so what a script sets it reads back. None is
    drawn: OpenSauce's effects are Direct3D 9 shaders in its own tags, and
    this game's renderers have no bloom or motion blur; each is logged once
    as kept, not drawn. A name (an effect's, a shader variable's) finds none.
The rest (its weapon and object data, runtime values, physics) are not here
yet: execute_script's use of one is logged once by name.

OpenSauce: Kornner Studios, GPL-3.0-or-later (github.com/HaloMods/OpenSauce).

SPDX-FileCopyrightText: 2005-2012 Kornner Studios
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifdef HALO_LUA

#include "cseries.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "hs/hs.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer_console_vars.h"
#include "units/units.h"

#include "lua_internal.h"
#include "lua_opensauce.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

/* OpenSauce's header in a Custom Edition cache's padding (as
ce_map_checks.c reads it) */
#define OPENSAUCE_HEADER_OFFSET 0x70
#define OPENSAUCE_HEADER_SIGNATURE 'yelo'

/* the rasterizer's debug options in haloce.exe 1.0.10, and the fields of
them Twiddlings answers, at OpenSauce's offsets */
#define OPENSAUCE_DEBUG_OPTIONS 0x006249C0u
#define OPENSAUCE_PROFILE_PRINT_LOCKS 0x64u
#define OPENSAUCE_PROFILE_OBJECTLOCK_TIME 0x68u
#define OPENSAUCE_PAD3_SCALE 0x74u
#define OPENSAUCE_F 0x78u
#define OPENSAUCE_F_COUNT 6

/* effect instances kept (an OpenSauce map has as many as its post-processing
tags list; SPV3's highest index is 33) */
#define OPENSAUCE_PP_INSTANCES 64
#define OPENSAUCE_MAXIMUM_PARAMETERS 6

/* ---------- types */

struct opensauce_field
{
	unsigned int offset;
	unsigned int size;
	boolean writable;
	void *variable;
};

/* an effect instance's state (c_effect_instance's flags and fade) */
struct opensauce_pp_instance
{
	boolean active;
	real fade_start;
	real fade_end;
	real fade_current;
	/* seconds the fade takes, and has taken */
	real fade_time;
	real fade_elapsed;
};

/* a value bloom changes to over a time */
struct opensauce_pp_value
{
	real start;
	real end;
	real current;
	real time;
	real elapsed;
};

/* one of OpenSauce's script functions here: its definition, its other
parameters following it (hs.h), its types as given */
struct opensauce_function
{
	struct hs_function_definition definition;
	short more_parameter_types[OPENSAUCE_MAXIMUM_PARAMETERS - 1];
	short types[OPENSAUCE_MAXIMUM_PARAMETERS];
};

/* a post-processing global (project_yellow_scripting.xml) */
struct opensauce_pp_global
{
	char const *name;
	boolean is_real;
	real value;
};

enum
{
	_opensauce_bloom_size,
	_opensauce_bloom_exposure,
	_opensauce_bloom_mix_amount,
	_opensauce_bloom_minimum_red,
	_opensauce_bloom_minimum_green,
	_opensauce_bloom_minimum_blue,
	_opensauce_bloom_maximum_red,
	_opensauce_bloom_maximum_green,
	_opensauce_bloom_maximum_blue,
	NUMBER_OF_OPENSAUCE_BLOOM_VALUES
};

/* ---------- prototypes */

static void opensauce_unit_data_get_integer_evaluate(short function_index, long thread_index, boolean initialize);
static void opensauce_pp_evaluate(short function_index, long thread_index, boolean initialize);
static struct opensauce_pp_global *opensauce_pp_global_named(char const *name);
static void opensauce_pp_global_command(char const *script, struct opensauce_pp_global *global, char const *rest);

/* (hs_compile.c) */
boolean hs_macro_function_parse(short function_index, long expression_index);

/* ---------- globals */

#define OPENSAUCE_FUNCTION(name, return_type, count, evaluate, ...) \
	{ { return_type, 0, name, hs_macro_function_parse, evaluate, "OpenSauce's", NULL, count, { 0 } }, { 0 }, \
		__VA_ARGS__ }

/* the functions of OpenSauce's here (their arguments and results as its
project_yellow_scripting.xml defines them) */
static struct opensauce_function opensauce_functions[] =
{
	OPENSAUCE_FUNCTION("unit_data_get_integer", _hs_type_long_integer, 2, opensauce_unit_data_get_integer_evaluate,
		{ _hs_type_unit, _hs_type_string }),
	OPENSAUCE_FUNCTION("pp_load", _hs_type_void, 0, opensauce_pp_evaluate, { 0 }),
	OPENSAUCE_FUNCTION("pp_unload", _hs_type_void, 0, opensauce_pp_evaluate, { 0 }),
	OPENSAUCE_FUNCTION("pp_get_effect_instance_index_by_name", _hs_type_short_integer, 1, opensauce_pp_evaluate,
		{ _hs_type_string }),
	OPENSAUCE_FUNCTION("pp_set_effect_instance_active", _hs_type_void, 2, opensauce_pp_evaluate,
		{ _hs_type_short_integer, _hs_type_boolean }),
	OPENSAUCE_FUNCTION("pp_set_effect_instance_fade", _hs_type_void, 4, opensauce_pp_evaluate,
		{ _hs_type_short_integer, _hs_type_real, _hs_type_real, _hs_type_real }),
	OPENSAUCE_FUNCTION("pp_get_effect_instance_current_fade", _hs_type_real, 1, opensauce_pp_evaluate,
		{ _hs_type_short_integer }),
	OPENSAUCE_FUNCTION("pp_get_effect_instance_fade_direction", _hs_type_short_integer, 1, opensauce_pp_evaluate,
		{ _hs_type_short_integer }),
	OPENSAUCE_FUNCTION("pp_get_effect_index_by_name", _hs_type_short_integer, 1, opensauce_pp_evaluate,
		{ _hs_type_string }),
	OPENSAUCE_FUNCTION("pp_get_effect_is_valid", _hs_type_boolean, 1, opensauce_pp_evaluate,
		{ _hs_type_short_integer }),
	OPENSAUCE_FUNCTION("pp_get_effect_shader_variable_index_by_name", _hs_type_short_integer, 2,
		opensauce_pp_evaluate, { _hs_type_short_integer, _hs_type_string }),
	OPENSAUCE_FUNCTION("pp_set_effect_shader_variable_boolean", _hs_type_void, 4, opensauce_pp_evaluate,
		{ _hs_type_short_integer, _hs_type_short_integer, _hs_type_boolean, _hs_type_real }),
	OPENSAUCE_FUNCTION("pp_set_effect_shader_variable_integer", _hs_type_void, 4, opensauce_pp_evaluate,
		{ _hs_type_short_integer, _hs_type_short_integer, _hs_type_long_integer, _hs_type_real }),
	OPENSAUCE_FUNCTION("pp_set_effect_shader_variable_real", _hs_type_void, 6, opensauce_pp_evaluate,
		{ _hs_type_short_integer, _hs_type_short_integer, _hs_type_real, _hs_type_real, _hs_type_real,
		_hs_type_real }),
	OPENSAUCE_FUNCTION("pp_set_effect_shader_instance_active", _hs_type_void, 3, opensauce_pp_evaluate,
		{ _hs_type_short_integer, _hs_type_short_integer, _hs_type_boolean }),
	OPENSAUCE_FUNCTION("pp_bloom_set_size", _hs_type_void, 2, opensauce_pp_evaluate,
		{ _hs_type_real, _hs_type_real }),
	OPENSAUCE_FUNCTION("pp_bloom_set_exposure", _hs_type_void, 2, opensauce_pp_evaluate,
		{ _hs_type_real, _hs_type_real }),
	OPENSAUCE_FUNCTION("pp_bloom_set_mix_amount", _hs_type_void, 2, opensauce_pp_evaluate,
		{ _hs_type_real, _hs_type_real }),
	OPENSAUCE_FUNCTION("pp_bloom_set_minimum_color", _hs_type_void, 4, opensauce_pp_evaluate,
		{ _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real }),
	OPENSAUCE_FUNCTION("pp_bloom_set_maximum_color", _hs_type_void, 4, opensauce_pp_evaluate,
		{ _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real }),
};
static boolean opensauce_functions_ready = FALSE;

/* OpenSauce's post-processing globals, at their defaults (its effects on,
FXAA and motion blur off) */
static struct opensauce_pp_global opensauce_pp_globals[] =
{
	{ "pp_external_post_processes_enabled", FALSE, 1.f },
	{ "pp_internal_post_processes_enabled", FALSE, 1.f },
	{ "pp_fxaa_enabled", FALSE, 0.f },
	{ "pp_motion_blur_enabled", FALSE, 0.f },
	{ "pp_motion_blur_amount", TRUE, 1.f },
};

/* the post processing's state: kept for the map, cleared as another loads */
static struct
{
	unsigned long map_checksum;
	boolean loaded;
	struct opensauce_pp_instance instances[OPENSAUCE_PP_INSTANCES];
	struct opensauce_pp_value bloom[NUMBER_OF_OPENSAUCE_BLOOM_VALUES];
} opensauce_pp;

/* OpenSauce's script functions and globals (its CheApe definitions,
project_yellow_scripting.xml) */
static char const *const opensauce_script_words[] =
{
	"volume_test_player_team", "volume_test_player_team_all", "player_team_teleport", "player_team_players",
	"player_data_get_integer", "player_team_data_get_integer", "player_data_get_object", "player_data_get_real",
	"player_data_set_real", "player_local_get", "objects_distance_to_object", "object_data_get_real",
	"object_data_set_real", "weapon_data_get_real", "weapon_data_set_real", "weapon_data_magazine_get_integer",
	"weapon_data_magazine_set_integer", "weapon_data_trigger_set_real", "unit_data_get_object",
	"unit_data_get_integer", "unit_data_set_integer", "unit_data_get_real", "unit_data_set_real",
	"physics_get_gravity", "physics_set_gravity", "physics_constants_reset", "runtime_integers_reset",
	"runtime_integer_get", "runtime_integer_set", "runtime_integer_inc", "runtime_integer_dec",
	"game_change_version_id", "game_engine_data_get_integer", "machine_is_host", "machine_is_dedi", "abs_integer",
	"abs_real", "pp_load", "pp_unload", "pp_get_effect_instance_index_by_name", "pp_set_effect_instance_active",
	"pp_set_effect_instance_fade", "pp_get_effect_instance_current_fade", "pp_get_effect_instance_fade_direction",
	"pp_get_effect_index_by_name", "pp_get_effect_is_valid", "pp_get_effect_shader_variable_index_by_name",
	"pp_set_effect_shader_variable_boolean", "pp_set_effect_shader_variable_integer",
	"pp_set_effect_shader_variable_real", "pp_set_effect_shader_instance_active", "pp_bloom_set_size",
	"pp_bloom_set_exposure", "pp_bloom_set_mix_amount", "pp_bloom_set_minimum_color", "pp_bloom_set_maximum_color",
	"bitwise_and", "bitwise_or", "bitwise_xor", "bitwise_lhs", "bitwise_rhs", "bit_test", "bit_toggle",
	"bit_flags_test", "bit_flags_toggle", "hex_string_to_long", "runtime_integer_operation", "runtime_vectors_reset",
	"runtime_vector_get_element", "runtime_vector_set_element", "runtime_vector_set", "runtime_vector_operation",
	"runtime_vector_to_string", "object_data_set_vector", "object_data_save_vector", "display_scripted_ui_widget",
	"play_bink_movie", "scenario_faux_zones_reset", "scenario_faux_zone_current_switch_variant",
	"scenario_faux_zone_switch_variant", "scenario_faux_zone_switch_sky", "structure_bsp_set_lightmap_set",
	"structure_bsp_set_sky_set", "ai_transform_actor", "ai_transform_actors", "ai_transform_actors_by_type",
	"ai_actor_is_transforming",
	/* (its globals) */
	"rasterizer_rt_display", "rasterizer_gbuffer_enabled", "pp_external_post_processes_enabled",
	"pp_internal_post_processes_enabled", "pp_fxaa_enabled", "pp_motion_blur_enabled", "pp_motion_blur_amount",
	"ai_transforms_enabled",
};

/* ---------- private code */

/* the debug options' fields Twiddlings answers, OpenSauce's offset first */
static struct opensauce_field const *opensauce_field_at(
	unsigned int offset,
	unsigned int size)
{
	static struct opensauce_field fields[3 + OPENSAUCE_F_COUNT];
	static boolean filled = FALSE;
	short index;

	if (!filled)
	{
		fields[0].offset = OPENSAUCE_PROFILE_PRINT_LOCKS;
		/* (with the 3 bytes after it, as on Halo PC) */
		fields[0].size = 4;
		fields[0].variable = &rasterizer_debug_options.profile_print_locks;
		fields[1].offset = OPENSAUCE_PROFILE_OBJECTLOCK_TIME;
		fields[1].size = sizeof(rasterizer_debug_options.profile_objectlock_time);
		fields[1].variable = &rasterizer_debug_options.profile_objectlock_time;
		fields[2].offset = OPENSAUCE_PAD3_SCALE;
		fields[2].size = sizeof(rasterizer_debug_options.pad3_scale);
		fields[2].variable = &rasterizer_debug_options.pad3_scale;
		for (index = 0; index < OPENSAUCE_F_COUNT; index++)
		{
			fields[3 + index].offset = OPENSAUCE_F + index * sizeof(real);
			fields[3 + index].size = sizeof(real);
			fields[3 + index].writable = TRUE;
			fields[3 + index].variable = &rasterizer_debug_options.f[index];
		}
		filled = TRUE;
	}
	for (index = 0; index < (short)NUMBEROF(fields); index++)
	{
		if (offset >= fields[index].offset && offset + size <= fields[index].offset + fields[index].size)
			return &fields[index];
	}
	return NULL;
}

/* ---------- public code */

boolean lua_opensauce_map(
	void)
{
	byte const *header = (byte const *)cache_file_loaded_header();
	unsigned int signature;

	if (!header)
		return FALSE;
	memcpy(&signature, header + OPENSAUCE_HEADER_OFFSET, sizeof(signature));
	return signature == (unsigned int)OPENSAUCE_HEADER_SIGNATURE;
}

int twiddlings_opensauce_access(
	boolean write,
	unsigned int address,
	void *value,
	unsigned int size)
{
	struct opensauce_field const *field;
	unsigned int offset;

	if (address < OPENSAUCE_DEBUG_OPTIONS + OPENSAUCE_PROFILE_PRINT_LOCKS ||
		address + size > OPENSAUCE_DEBUG_OPTIONS + OPENSAUCE_F + OPENSAUCE_F_COUNT * sizeof(real) ||
		!lua_opensauce_map())
	{
		return -1;
	}
	offset = address - OPENSAUCE_DEBUG_OPTIONS;
	field = opensauce_field_at(offset, size);
	if (!field)
		return -1;
	/* (the profile options are the game's debugging: read alone) */
	if (write && !field->writable)
		return FALSE;
	if (write)
		memcpy((byte *)field->variable + (offset - field->offset), value, size);
	else
		memcpy(value, (byte const *)field->variable + (offset - field->offset), size);
	return TRUE;
}

boolean lua_opensauce_script_word(
	char const *script,
	char const *word,
	char const *expression)
{
	struct opensauce_pp_global *global;
	short index;

	if (!lua_opensauce_map())
		return FALSE;
	global = opensauce_pp_global_named(word);
	if (global)
	{
		char const *rest = strstr(expression, word);

		opensauce_pp_global_command(script, global, rest ? rest + strlen(word) : "");
		return TRUE;
	}
	for (index = 0; index < (short)NUMBEROF(opensauce_script_words); index++)
	{
		if (!strcmp(word, opensauce_script_words[index]))
		{
			char key[160];

			snprintf(key, sizeof(key), "opensauce %s", word);
			lua_log_once(key, "%s: execute_script: OpenSauce's %s is not here yet: skipped", script, word);
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------- script functions */

/* a unit's integer by OpenSauce's name for it (ObjectFieldDefinitions.inl's
g_unit_integer_fields, names matched without case): FALSE for a name it
does not have, or one this game's unit has not (Custom Edition's third and
fourth grenade counts) */
static boolean opensauce_unit_integer(
	struct unit_datum const *unit,
	char const *name,
	long *value)
{
	struct _unit_datum const *data = &unit->unit;

	if (!_stricmp(name, "ticks_until_flame_to_death"))
		*value = (byte)data->flaming_death_delay;
	else if (!_stricmp(name, "throwing_grenade_state"))
		*value = (byte)data->grenade_throw_state;
	else if (!_stricmp(name, "total_grenade_count[frag]"))
		*value = (byte)data->grenade_counts[0];
	else if (!_stricmp(name, "total_grenade_count[plasma]"))
		*value = (byte)data->grenade_counts[1];
	else if (!_stricmp(name, "current_grenade_index"))
		*value = (byte)data->current_grenade_index;
	/* (bytes as OpenSauce reads them: not zoomed, -1, is 255) */
	else if (!_stricmp(name, "zoom_level"))
		*value = (byte)data->current_zoom_level;
	else if (!_stricmp(name, "desired_zoom_level"))
		*value = (byte)data->desired_zoom_level;
	else if (!_stricmp(name, "vehicle_seat_index"))
		*value = data->parent_seat_index;
	else if (!_stricmp(name, "current_weapon_index"))
		*value = data->current_weapon_index;
	else if (!_stricmp(name, "feign_death_timer"))
		*value = data->feign_death_timer;
	else if (!_stricmp(name, "killing_spree_count"))
		*value = data->killing_spree_count;
	else
		return FALSE;
	return TRUE;
}

/* unit_data_get_integer <unit> <data_name>: -1 when it fails, as OpenSauce's */
static void opensauce_unit_data_get_integer_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	long *arguments = hs_macro_function_evaluate(function_index, thread_index, initialize);
	long value = NONE;

	if (!arguments)
		return;
	{
		char const *name = arguments[1] ? (char const *)xbox_pointer((unsigned int)arguments[1]) : "";
		struct unit_datum const *unit = (long)arguments[0] != NONE ? unit_try_and_get((long)arguments[0]) : NULL;

		if (unit && !opensauce_unit_integer(unit, name, &value))
		{
			char key[160];

			value = NONE;
			snprintf(key, sizeof(key), "opensauce unit integer %.100s", name);
			lua_log_once(key, "execute_script: unit_data_get_integer: no unit integer \"%.100s\" here: -1", name);
		}
	}
	hs_return(thread_index, value);
}

static void opensauce_pp_value_set(
	struct opensauce_pp_value *value,
	real end,
	real time)
{
	value->start = value->current;
	value->end = end;
	value->time = time > 0.f ? time : 0.f;
	value->elapsed = 0.f;
	if (value->time <= 0.f)
		value->current = end;
}

static void opensauce_pp_value_advance(
	struct opensauce_pp_value *value,
	real seconds)
{
	if (value->current == value->end)
		return;
	value->elapsed += seconds;
	if (value->time <= 0.f || value->elapsed >= value->time)
		value->current = value->end;
	else
		value->current = value->start + (value->end - value->start) * (value->elapsed / value->time);
}

/* the effect instance of an index a script gave, or NULL (logged once) */
static struct opensauce_pp_instance *opensauce_pp_instance(
	char const *function,
	long index)
{
	char key[96];

	if (index >= 0 && index < OPENSAUCE_PP_INSTANCES)
		return &opensauce_pp.instances[index];
	snprintf(key, sizeof(key), "opensauce pp instance %ld", index);
	lua_log_once(key, "execute_script: %s: effect instance %ld is past the %d kept: nothing", function, index,
		OPENSAUCE_PP_INSTANCES);
	return NULL;
}

static real opensauce_real_argument(
	long argument)
{
	union { long bits; real value; } converted;

	converted.bits = argument;
	return converted.value;
}

/* OpenSauce's post-processing functions: kept, never drawn */
static void opensauce_pp_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	long *arguments = hs_macro_function_evaluate(function_index, thread_index, initialize);
	struct hs_function_definition const *function = hs_function_get(function_index);
	char const *name = function->name;
	union { real value; long bits; } result;
	struct opensauce_pp_instance *instance;
	char key[128];

	if (!arguments)
		return;
	result.bits = 0;
	snprintf(key, sizeof(key), "opensauce pp drawn %s", name);
	lua_log_once(key, "execute_script: OpenSauce's %s is kept, not drawn: this game has no OpenSauce post "
		"processing", name);
	if (!strcmp(name, "pp_load"))
	{
		opensauce_pp.loaded = TRUE;
	}
	else if (!strcmp(name, "pp_unload"))
	{
		opensauce_pp.loaded = FALSE;
	}
	else if (!strcmp(name, "pp_set_effect_instance_active"))
	{
		if ((instance = opensauce_pp_instance(name, (long)(short)arguments[0])) != NULL)
			instance->active = (boolean)arguments[1];
	}
	else if (!strcmp(name, "pp_set_effect_instance_fade"))
	{
		if ((instance = opensauce_pp_instance(name, (long)(short)arguments[0])) != NULL)
		{
			instance->fade_start = opensauce_real_argument(arguments[1]);
			instance->fade_end = opensauce_real_argument(arguments[2]);
			instance->fade_current = instance->fade_start;
			instance->fade_time = opensauce_real_argument(arguments[3]);
			instance->fade_elapsed = 0.f;
			if (instance->fade_time <= 0.f)
				instance->fade_current = instance->fade_end;
		}
	}
	else if (!strcmp(name, "pp_get_effect_instance_current_fade"))
	{
		if ((instance = opensauce_pp_instance(name, (long)(short)arguments[0])) != NULL)
			result.value = instance->fade_current;
	}
	else if (!strcmp(name, "pp_get_effect_instance_fade_direction"))
	{
		if ((instance = opensauce_pp_instance(name, (long)(short)arguments[0])) != NULL)
		{
			result.bits = instance->fade_current == instance->fade_end ? NONE :
				instance->fade_start < instance->fade_end ? 1 : 0;
		}
	}
	else if (!strcmp(name, "pp_get_effect_instance_index_by_name") || !strcmp(name, "pp_get_effect_index_by_name") ||
		!strcmp(name, "pp_get_effect_shader_variable_index_by_name"))
	{
		/* (no effect is read from the map's tags: no name is found) */
		result.bits = NONE;
	}
	else if (!strcmp(name, "pp_get_effect_is_valid"))
	{
		result.bits = FALSE;
	}
	else if (!strcmp(name, "pp_bloom_set_size"))
	{
		opensauce_pp_value_set(&opensauce_pp.bloom[_opensauce_bloom_size], opensauce_real_argument(arguments[0]),
			opensauce_real_argument(arguments[1]));
	}
	else if (!strcmp(name, "pp_bloom_set_exposure"))
	{
		opensauce_pp_value_set(&opensauce_pp.bloom[_opensauce_bloom_exposure], opensauce_real_argument(arguments[0]),
			opensauce_real_argument(arguments[1]));
	}
	else if (!strcmp(name, "pp_bloom_set_mix_amount"))
	{
		opensauce_pp_value_set(&opensauce_pp.bloom[_opensauce_bloom_mix_amount],
			opensauce_real_argument(arguments[0]), opensauce_real_argument(arguments[1]));
	}
	else if (!strcmp(name, "pp_bloom_set_minimum_color") || !strcmp(name, "pp_bloom_set_maximum_color"))
	{
		short first = !strcmp(name, "pp_bloom_set_minimum_color") ? _opensauce_bloom_minimum_red :
			_opensauce_bloom_maximum_red;
		short component;

		for (component = 0; component < 3; component++)
		{
			opensauce_pp_value_set(&opensauce_pp.bloom[first + component],
				opensauce_real_argument(arguments[component]), opensauce_real_argument(arguments[3]));
		}
	}
	/* (the shader variables and shader instances: nothing to keep them in) */
	hs_return(thread_index, result.bits);
}

static void opensauce_functions_prepare(
	void)
{
	short index;

	if (opensauce_functions_ready)
		return;
	opensauce_functions_ready = TRUE;
	for (index = 0; index < (short)NUMBEROF(opensauce_functions); index++)
	{
		struct opensauce_function *function = &opensauce_functions[index];
		short parameter;

		function->definition.parameter_types[0] = function->types[0];
		for (parameter = 1; parameter < function->definition.parameter_count; parameter++)
			function->more_parameter_types[parameter - 1] = function->types[parameter];
	}
}

/* a post-processing global by name, or NULL */
static struct opensauce_pp_global *opensauce_pp_global_named(
	char const *name)
{
	short index;

	for (index = 0; index < (short)NUMBEROF(opensauce_pp_globals); index++)
	{
		if (!_stricmp(name, opensauce_pp_globals[index].name))
			return &opensauce_pp_globals[index];
	}
	return NULL;
}

/* "<global>" or "<global> <value>", as the console takes a global: the
value printed or set */
static void opensauce_pp_global_command(
	char const *script,
	struct opensauce_pp_global *global,
	char const *rest)
{
	char key[128];

	while (*rest == ' ' || *rest == '\t')
		rest++;
	snprintf(key, sizeof(key), "opensauce pp global %s", global->name);
	lua_log_once(key, "%s: execute_script: OpenSauce's %s is kept, not drawn: this game has no OpenSauce post "
		"processing", script, global->name);
	if (!*rest || *rest == ')')
	{
		real_argb_color const white = { 1.f, 1.f, 1.f, 1.f };
		char line[128];

		if (global->is_real)
			snprintf(line, sizeof(line), "%f", (double)global->value);
		else
			snprintf(line, sizeof(line), "%s", global->value != 0.f ? "true" : "false");
		lua_console_line(&white, line);
		return;
	}
	if (!_strnicmp(rest, "true", 4))
		global->value = 1.f;
	else if (!_strnicmp(rest, "false", 5))
		global->value = 0.f;
	else
		global->value = (real)atof(rest);
	if (!global->is_real)
		global->value = global->value != 0.f ? 1.f : 0.f;
}

/* ---------- public code (script functions) */

struct hs_function_definition *lua_opensauce_hs_function(
	short index)
{
	/* (only on OpenSauce's maps, as they are found: another map's scripts
	that name one by its number name none) */
	if (index < 0 || index >= (short)NUMBEROF(opensauce_functions) || !lua_opensauce_map())
		return NULL;
	opensauce_functions_prepare();
	return &opensauce_functions[index].definition;
}

short lua_opensauce_hs_function_find(
	char const *name)
{
	short index;

	if (!lua_opensauce_map())
		return NONE;
	for (index = 0; index < (short)NUMBEROF(opensauce_functions); index++)
	{
		if (!_stricmp(name, opensauce_functions[index].definition.name))
			return index;
	}
	return NONE;
}

void lua_opensauce_map_load(
	void)
{
	short index;

	memset(&opensauce_pp, 0, sizeof(opensauce_pp));
	/* (bloom at OpenSauce's defaults would be its tags'; none are read) */
	for (index = 0; index < (short)NUMBEROF(opensauce_pp_globals); index++)
	{
		opensauce_pp_globals[index].value = !strcmp(opensauce_pp_globals[index].name, "pp_fxaa_enabled") ||
			!strcmp(opensauce_pp_globals[index].name, "pp_motion_blur_enabled") ? 0.f : 1.f;
	}
}

void lua_opensauce_tick(
	void)
{
	real const seconds = 1.f / (real)TICKS_PER_SECOND;
	short index;

	if (!lua_opensauce_map())
		return;
	for (index = 0; index < OPENSAUCE_PP_INSTANCES; index++)
	{
		struct opensauce_pp_instance *instance = &opensauce_pp.instances[index];
		struct opensauce_pp_value fade;

		if (instance->fade_current == instance->fade_end)
			continue;
		fade.start = instance->fade_start;
		fade.end = instance->fade_end;
		fade.current = instance->fade_current;
		fade.time = instance->fade_time;
		fade.elapsed = instance->fade_elapsed;
		opensauce_pp_value_advance(&fade, seconds);
		instance->fade_current = fade.current;
		instance->fade_elapsed = fade.elapsed;
	}
	for (index = 0; index < NUMBER_OF_OPENSAUCE_BLOOM_VALUES; index++)
		opensauce_pp_value_advance(&opensauce_pp.bloom[index], seconds);
}

#endif
