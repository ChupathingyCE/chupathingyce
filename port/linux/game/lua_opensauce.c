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

The functions and globals OpenSauce adds to the scripts (its post
processing, its unit and weapon data, its runtime values) are not here yet:
execute_script's use of one is logged once by name.

OpenSauce: Kornner Studios, GPL-3.0-or-later (github.com/HaloMods/OpenSauce).

SPDX-FileCopyrightText: 2005-2012 Kornner Studios
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifdef HALO_LUA

#include "cseries.h"
#include "cache/cache_files.h"
#include "rasterizer/rasterizer_console_vars.h"

#include "lua_internal.h"
#include "lua_opensauce.h"

#include <stdio.h>
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

/* ---------- types */

struct opensauce_field
{
	unsigned int offset;
	unsigned int size;
	boolean writable;
	void *variable;
};

/* ---------- globals */

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
	char const *word)
{
	short index;

	if (!lua_opensauce_map())
		return FALSE;
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

#endif
