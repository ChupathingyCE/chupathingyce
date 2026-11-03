/*
STUBBS_FUNCTIONS.C

Stubbs the Zombie's script functions that Halo lacks, as a table of their own
(Stubbs mode only, development builds: stubbs_mode.c). The game looks them up
by name after Halo's (hs.c): their indices follow Halo's table, which is
unchanged, and outside Stubbs mode there are none, so Halo's console never
lists them.

Their signatures are the ones the calls in Stubbs's maps were compiled with.
A few are Halo's functions under another name and run as those; the rest do
nothing yet (each says so once in debug.txt) and return none, 0 or false.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "errors.h"
#include "hs/hs.h"
#include "hs/object_lists.h"
#include "objects/damage.h"
#include "objects/objects.h"
#include "stubbs_mode.h"

#include <string.h>

boolean hs_macro_function_parse(short function_index, long expression_index);
extern long const hs_function_table_count;

enum
{
	MAXIMUM_STUBBS_PARAMETERS = 8,
};

struct stubbs_function
{
	struct hs_function_definition definition;
	/* (the parameters after the first follow the definition: hs.h) */
	short more_parameter_types[MAXIMUM_STUBBS_PARAMETERS - 1];
	char const *halo_name;
	short types[MAXIMUM_STUBBS_PARAMETERS];
	short halo_index;
	boolean logged;
};

static void stubbs_function_evaluate(short function_index, long thread_index, boolean initialize);

#define STUBBS_FUNCTION(name, return_type, count, halo_name, ...) \
	{ { return_type, 0, name, hs_macro_function_parse, stubbs_function_evaluate, \
		"Stubbs the Zombie's", NULL, count, { 0 } }, { 0 }, halo_name, __VA_ARGS__, NONE, FALSE }

/* (generated from the calls in Stubbs's maps: name, return type, parameters) */
static struct stubbs_function stubbs_functions[] =
{
	/* (Halo's, taking a second argument in Stubbs: run as Halo's) */
	STUBBS_FUNCTION("camera_control", _hs_type_void, 2, "camera_control", { _hs_type_boolean, _hs_type_short_integer }),
	STUBBS_FUNCTION("players_current", _hs_type_object_list, 0, "players", { 0 }),
	STUBBS_FUNCTION("players_origin", _hs_type_object_list, 0, "players", { 0 }),
	STUBBS_FUNCTION("effect_new_on_flag", _hs_type_void, 2, "effect_new", { _hs_type_effect, _hs_type_cutscene_flag }),
	STUBBS_FUNCTION("cinematic_clear_title", _hs_type_void, 1, NULL, { _hs_type_cutscene_title }),
	STUBBS_FUNCTION("object_get_health", _hs_type_real, 1, "unit_get_health", { _hs_type_object }),
	STUBBS_FUNCTION("apply_impulse", _hs_type_void, 5, NULL, { _hs_type_trigger_volume, _hs_type_cutscene_flag, _hs_type_real, _hs_type_real, _hs_type_damage }),
	STUBBS_FUNCTION("object_activation_add_camera", _hs_type_void, 1, NULL, { _hs_type_cutscene_camera_point }),
	STUBBS_FUNCTION("unit_set_weapon", _hs_type_void, 2, NULL, { _hs_type_unit, _hs_type_object_definition }),
	STUBBS_FUNCTION("objects_cannot_take_damage", _hs_type_void, 2, NULL, { _hs_type_object_list, _hs_type_boolean }),
	STUBBS_FUNCTION("object_activation_clear", _hs_type_void, 0, NULL, { 0 }),
	STUBBS_FUNCTION("object_distance_to_camera_point", _hs_type_real, 2, NULL, { _hs_type_cutscene_camera_point, _hs_type_object }),
	STUBBS_FUNCTION("players_nonorigin", _hs_type_object_list, 0, "players", { 0 }),
	STUBBS_FUNCTION("object_is_instance", _hs_type_boolean, 2, NULL, { _hs_type_object, _hs_type_object_definition }),
	STUBBS_FUNCTION("image_effect_set", _hs_type_void, 1, NULL, { _hs_type_string }),
	STUBBS_FUNCTION("effect_new_on_object", _hs_type_void, 2, NULL, { _hs_type_effect, _hs_type_object }),
	STUBBS_FUNCTION("objects_in_volume", _hs_type_object_list, 2, NULL, { _hs_type_object_list, _hs_type_trigger_volume }),
	STUBBS_FUNCTION("unit_enter_vehicle_now", _hs_type_void, 3, "unit_enter_vehicle", { _hs_type_unit, _hs_type_vehicle, _hs_type_string }),
	STUBBS_FUNCTION("player_set_profile", _hs_type_void, 3, "player_add_equipment", { _hs_type_unit, _hs_type_starting_profile, _hs_type_boolean }),
	STUBBS_FUNCTION("objects_cannot_lose_regions", _hs_type_void, 2, NULL, { _hs_type_object_list, _hs_type_boolean }),
	STUBBS_FUNCTION("ai_set_resurrection_group", _hs_type_void, 1, NULL, { _hs_type_ai }),
	STUBBS_FUNCTION("ai_enable_communication", _hs_type_void, 2, NULL, { _hs_type_ai, _hs_type_boolean }),
	STUBBS_FUNCTION("image_effect_end", _hs_type_void, 0, NULL, { 0 }),
	STUBBS_FUNCTION("camera_video_enable", _hs_type_void, 1, NULL, { _hs_type_boolean }),
	STUBBS_FUNCTION("ai_begin_flee", _hs_type_void, 3, NULL, { _hs_type_ai, _hs_type_boolean, _hs_type_short_integer }),
	STUBBS_FUNCTION("ai_set_disguise_known", _hs_type_void, 3, NULL, { _hs_type_ai, _hs_type_string, _hs_type_boolean }),
	STUBBS_FUNCTION("camera_video_set", _hs_type_void, 1, NULL, { _hs_type_cutscene_camera_point }),
	STUBBS_FUNCTION("player_action_test_enable", _hs_type_void, 2, NULL, { _hs_type_short_integer, _hs_type_boolean }),
	STUBBS_FUNCTION("object_set_function_value", _hs_type_void, 3, NULL, { _hs_type_object, _hs_type_string, _hs_type_real }),
	STUBBS_FUNCTION("ai_set_disguise_knowable", _hs_type_void, 3, NULL, { _hs_type_ai, _hs_type_string, _hs_type_boolean }),
	STUBBS_FUNCTION("player_set_weapons", _hs_type_void, 3, "player_add_equipment", { _hs_type_unit, _hs_type_starting_profile, _hs_type_boolean }),
	STUBBS_FUNCTION("map_completed", _hs_type_boolean, 1, NULL, { _hs_type_enum_game_difficulty }),
	STUBBS_FUNCTION("player_force_respawn", _hs_type_void, 0, NULL, { 0 }),
	STUBBS_FUNCTION("show_hud_help_text_for_player", _hs_type_void, 2, NULL, { _hs_type_boolean, _hs_type_unit }),
	STUBBS_FUNCTION("objects_set_current_vitality", _hs_type_void, 3, NULL, { _hs_type_object_list, _hs_type_real, _hs_type_real }),
	STUBBS_FUNCTION("hud_show_special_attack", _hs_type_void, 2, NULL, { _hs_type_short_integer, _hs_type_boolean }),
	STUBBS_FUNCTION("object_activation_add_object", _hs_type_void, 1, NULL, { _hs_type_object }),
	STUBBS_FUNCTION("objects_cannot_be_finished", _hs_type_void, 2, NULL, { _hs_type_object_list, _hs_type_boolean }),
	STUBBS_FUNCTION("objects_cannot_be_possessed", _hs_type_void, 2, NULL, { _hs_type_object_list, _hs_type_boolean }),
	STUBBS_FUNCTION("objects_destroy", _hs_type_void, 1, NULL, { _hs_type_object_list }),
	STUBBS_FUNCTION("player_action_test_dance_down", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("player_action_test_dance_up", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("player_action_test_dance_left", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("player_action_test_dance_right", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("objects_cannot_die", _hs_type_void, 2, NULL, { _hs_type_object_list, _hs_type_boolean }),
	STUBBS_FUNCTION("ai_begin_flee_with_cower_chance", _hs_type_void, 5, NULL, { _hs_type_ai, _hs_type_boolean, _hs_type_short_integer, _hs_type_boolean, _hs_type_real }),
	STUBBS_FUNCTION("unit_special_attack_get_count", _hs_type_short_integer, 2, NULL, { _hs_type_unit, _hs_type_short_integer }),
	STUBBS_FUNCTION("object_set_current_vitality", _hs_type_void, 3, NULL, { _hs_type_object, _hs_type_real, _hs_type_real }),
	STUBBS_FUNCTION("image_effect_start_blend", _hs_type_void, 2, NULL, { _hs_type_string, _hs_type_real }),
	STUBBS_FUNCTION("player_move_origin", _hs_type_void, 2, NULL, { _hs_type_unit, _hs_type_unit }),
	STUBBS_FUNCTION("unit_special_attack_set_count", _hs_type_void, 3, NULL, { _hs_type_unit, _hs_type_short_integer, _hs_type_short_integer }),
	STUBBS_FUNCTION("object_region_destroyed", _hs_type_boolean, 2, NULL, { _hs_type_object, _hs_type_string }),
	STUBBS_FUNCTION("player_action_test_flatulence", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("player_action_test_abandon", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("player_enable_respawn", _hs_type_void, 1, NULL, { _hs_type_boolean }),
	STUBBS_FUNCTION("object_set_maximum_vitality", _hs_type_void, 3, NULL, { _hs_type_object, _hs_type_real, _hs_type_real }),
	STUBBS_FUNCTION("ai_set_all_enemies_unopposable", _hs_type_void, 2, NULL, { _hs_type_ai, _hs_type_boolean }),
	STUBBS_FUNCTION("player_action_test_hand", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("modulo", _hs_type_long_integer, 2, NULL, { _hs_type_long_integer, _hs_type_long_integer }),
	STUBBS_FUNCTION("player_action_test_head", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("ai_vehicle_use_movement_positions", _hs_type_void, 2, NULL, { _hs_type_ai, _hs_type_boolean }),
	STUBBS_FUNCTION("unit_taking_damage", _hs_type_boolean, 1, NULL, { _hs_type_unit }),
	STUBBS_FUNCTION("unit_get_weapon", _hs_type_object, 1, NULL, { _hs_type_unit }),
	STUBBS_FUNCTION("player_action_test_melee", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("unit_finishing_move", _hs_type_void, 1, NULL, { _hs_type_unit }),
	STUBBS_FUNCTION("unit_special_attack_animated", _hs_type_void, 2, NULL, { _hs_type_unit, _hs_type_short_integer }),
	STUBBS_FUNCTION("player_action_test_whistle_enabled", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("player_action_test_whistle", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("player_action_test_shove_enabled", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("player_action_test_shove", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("player_action_test_vehicle_enter", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("player_action_test_grenade", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("unit_special_attack", _hs_type_void, 2, NULL, { _hs_type_unit, _hs_type_short_integer }),
	STUBBS_FUNCTION("ai_disguise_knowledge_count", _hs_type_long_integer, 2, NULL, { _hs_type_ai, _hs_type_string }),
	STUBBS_FUNCTION("object_clone_state", _hs_type_void, 3, NULL, { _hs_type_object, _hs_type_object, _hs_type_boolean }),
	STUBBS_FUNCTION("ai_end_flee", _hs_type_void, 1, NULL, { _hs_type_ai }),
	STUBBS_FUNCTION("objects_distance", _hs_type_real, 2, NULL, { _hs_type_object_list, _hs_type_object }),
	STUBBS_FUNCTION("player_action_test_possess", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("breakable_surfaces_broken", _hs_type_boolean, 1, NULL, { _hs_type_string }),
	STUBBS_FUNCTION("ui_map_first_load", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("main_menu_show", _hs_type_void, 0, NULL, { 0 }),
	STUBBS_FUNCTION("attract_mode_enable", _hs_type_void, 0, NULL, { 0 }),
};

/* a function of Halo's own table by its name (NONE) */
static short stubbs_halo_function_find(
	char const *name)
{
	short index;

	for (index = 0; index < (short)hs_function_table_count; index++)
	{
		if (!_stricmp(hs_function_get(index)->name, name))
			return index;
	}
	return NONE;
}

/* ---------- the ones made real here

Each takes its arguments as the calls in Stubbs's maps pass them, and does
what its name and its uses there say (Sigmmma's reclaimer has no script
docs): */

static long stubbs_objects_each(long object_list, void (*each)(long object_index))
{
	long reference, object_index;

	for (object_index = object_list_get_first(object_list, &reference); object_index != NONE;
		object_index = object_list_get_next(object_list, &reference))
	{
		each(object_index);
	}
	return 0;
}

/* returns TRUE once it has returned the call's value */
static boolean stubbs_function_native(
	char const *name,
	short function_index,
	long thread_index,
	boolean initialize)
{
	long *arguments;
	long value = 0;

	if (_stricmp(name, "objects_cannot_take_damage") && _stricmp(name, "objects_cannot_die") &&
		_stricmp(name, "objects_destroy") && _stricmp(name, "modulo"))
	{
		return FALSE;
	}
	arguments = hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (!arguments)
		return TRUE;
	/* (objects_cannot_take_damage <objects> <boolean>, and objects_cannot_die
	the same: Halo has no "cannot die" apart from it) */
	if (!_stricmp(name, "objects_cannot_take_damage") || !_stricmp(name, "objects_cannot_die"))
	{
		if (arguments[1] & 0xFF)
			object_cannot_take_damage(arguments[0]);
		else
			object_can_take_damage(arguments[0]);
	}
	/* (objects_destroy <objects>) */
	else if (!_stricmp(name, "objects_destroy"))
		stubbs_objects_each(arguments[0], object_delete);
	/* (modulo <long> <long>) */
	else if (!_stricmp(name, "modulo"))
		value = arguments[1] ? arguments[0] % arguments[1] : 0;
	hs_return(thread_index, value);
	return TRUE;
}

static boolean stubbs_functions_ready;

static void stubbs_functions_prepare(
	void)
{
	size_t index;

	if (stubbs_functions_ready)
		return;
	stubbs_functions_ready = TRUE;
	for (index = 0; index < sizeof(stubbs_functions) / sizeof(*stubbs_functions); index++)
	{
		struct stubbs_function *function = &stubbs_functions[index];
		short parameter;

		function->definition.parameter_types[0] = function->types[0];
		for (parameter = 1; parameter < function->definition.parameter_count; parameter++)
			function->more_parameter_types[parameter - 1] = function->types[parameter];
	}
}

long stubbs_function_count(
	void)
{
	return stubbs_mode_active() ? (long)(sizeof(stubbs_functions) / sizeof(*stubbs_functions)) : 0;
}

struct hs_function_definition *stubbs_function_get(
	long index)
{
	if (index < 0 || index >= stubbs_function_count())
		return NULL;
	stubbs_functions_prepare();
	return &stubbs_functions[index].definition;
}

long stubbs_function_find(
	char const *name)
{
	long index;

	for (index = 0; name && index < stubbs_function_count(); index++)
	{
		if (!_stricmp(stubbs_functions[index].definition.name, name))
			return index;
	}
	return NONE;
}

static void stubbs_function_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct stubbs_function *function;
	long index = function_index - hs_function_table_count;
	long value = 0;

	if (index < 0 || index >= stubbs_function_count())
	{
		hs_return(thread_index, 0);
		return;
	}
	function = &stubbs_functions[index];
	if (function->halo_name)
	{
		if (function->halo_index == NONE)
			function->halo_index = stubbs_halo_function_find(function->halo_name);
		if (function->halo_index != NONE && function->halo_index < hs_function_table_count)
		{
			/* (Halo's evaluator, reading the arguments as this function's,
			which are the same) */
			hs_function_get(function->halo_index)->evaluate(function_index, thread_index, initialize);
			return;
		}
	}
	if (stubbs_function_native(function->definition.name, function_index, thread_index, initialize))
		return;
	if (!function->logged)
	{
		function->logged = TRUE;
		stubbs_mode_log("script function %s does nothing yet", function->definition.name);
	}
	switch (function->definition.return_type)
	{
	case _hs_type_object_list:
	case _hs_type_object:
	case _hs_type_unit:
	case _hs_type_vehicle:
		value = NONE;
		break;
	}
	hs_return(thread_index, value);
}
