/*
STUBBS_FUNCTIONS.C

Stubbs the Zombie's script functions that Halo lacks, as a table of their own
(Stubbs mode only: stubbs_mode.c). The game looks them up
by name after Halo's (hs.c): their indices follow Halo's table, which is
unchanged, and outside Stubbs mode there are none, so Halo's console never
lists them.

Their signatures are the ones the calls in Stubbs's maps were compiled with.
A few are Halo's functions under another name and run as those; some
belong to another part of the mode and call its hook (stubbs_campaign.h,
stubbs_player.h); the rest do nothing yet (each says so once in debug.txt)
and return none, 0 or false.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "errors.h"
#include "hs/hs.h"
#include "hs/object_lists.h"
#include "objects/damage.h"
#include "objects/objects.h"
#include "ai/ai_script.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "units/unit_definitions.h"
#include "game/players.h"
#include "units/units.h"
#include "models/model_definitions.h"
#include "objects/object_types.h"
#include "game/game.h"
#include "stubbs_mode.h"
#include "stubbs_functions.h"
#include "stubbs_campaign.h"
#include "stubbs_player.h"
#include "stubbs_horde.h"
#include "stubbs_engine.h"

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
	STUBBS_FUNCTION("objects_set_current_vitality", _hs_type_void, 3, "units_set_current_vitality", { _hs_type_object_list, _hs_type_real, _hs_type_real }),
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
	STUBBS_FUNCTION("object_set_current_vitality", _hs_type_void, 3, "unit_set_current_vitality", { _hs_type_object, _hs_type_real, _hs_type_real }),
	STUBBS_FUNCTION("image_effect_start_blend", _hs_type_void, 2, NULL, { _hs_type_string, _hs_type_real }),
	STUBBS_FUNCTION("player_move_origin", _hs_type_void, 2, NULL, { _hs_type_unit, _hs_type_unit }),
	STUBBS_FUNCTION("unit_special_attack_set_count", _hs_type_void, 3, NULL, { _hs_type_unit, _hs_type_short_integer, _hs_type_short_integer }),
	STUBBS_FUNCTION("object_region_destroyed", _hs_type_boolean, 2, NULL, { _hs_type_object, _hs_type_string }),
	STUBBS_FUNCTION("player_action_test_flatulence", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("player_action_test_abandon", _hs_type_boolean, 0, NULL, { 0 }),
	STUBBS_FUNCTION("player_enable_respawn", _hs_type_void, 1, NULL, { _hs_type_boolean }),
	STUBBS_FUNCTION("object_set_maximum_vitality", _hs_type_void, 3, "unit_set_maximum_vitality", { _hs_type_object, _hs_type_real, _hs_type_real }),
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
	/* (Halo's, but a team may be one of the two of Stubbs's that Halo has
	no room for, which read as none: hs_compile.c) */
	STUBBS_FUNCTION("ai_set_team", _hs_type_void, 2, NULL, { _hs_type_ai, _hs_type_enum_team }),
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

/* the functions another part of the mode makes real, each through its hook
(stubbs_campaign.c, stubbs_player.c, stubbs_image_effects.c): TRUE once it
has returned the call's value. Each argument is a script value's 32 bits
(a string's an Xbox address, a real's its bits) */
static boolean stubbs_function_routed(
	char const *name,
	short function_index,
	long thread_index,
	boolean initialize)
{
	static long no_arguments[1];
	long *arguments;
	long value = 0;
	real seconds;

	if (_stricmp(name, "map_completed") && _stricmp(name, "hud_show_special_attack") &&
		_stricmp(name, "image_effect_set") && _stricmp(name, "image_effect_start_blend") &&
		_stricmp(name, "image_effect_end") && _stricmp(name, "unit_finishing_move") &&
		_stricmp(name, "ai_set_resurrection_group") && _stricmp(name, "unit_special_attack") &&
		_stricmp(name, "unit_special_attack_get_count") && _stricmp(name, "unit_special_attack_set_count") &&
		_stricmp(name, "player_move_origin"))
	{
		return FALSE;
	}
	/* (none to read for one without arguments: the scripts' stack would
	halt on room for none, image_effect_end's) */
	arguments = hs_function_get(function_index)->parameter_count ?
		hs_macro_function_evaluate(function_index, thread_index, initialize) : no_arguments;
	if (!arguments)
		return TRUE;
	/* (map_completed <difficulty>) */
	if (!_stricmp(name, "map_completed"))
		value = stubbs_campaign_map_completed((short)arguments[0]);
	/* (hud_show_special_attack <short> <boolean>) */
	else if (!_stricmp(name, "hud_show_special_attack"))
		stubbs_player_hud_show_special_attack((short)arguments[0], (boolean)(arguments[1] & 0xFF));
	/* (image_effect_set <name>) */
	else if (!_stricmp(name, "image_effect_set"))
		stubbs_image_effect_set((char const *)xbox_pointer((unsigned long)arguments[0]));
	/* (image_effect_start_blend <name> <seconds>) */
	else if (!_stricmp(name, "image_effect_start_blend"))
	{
		memcpy(&seconds, &arguments[1], sizeof(seconds));
		stubbs_image_effect_start_blend((char const *)xbox_pointer((unsigned long)arguments[0]), seconds);
	}
	/* the special attacks (stubbs_engine.c): (unit_special_attack <unit>
	<index>) throws one at once, (unit_special_attack_get_count <unit>
	<index>) and (unit_special_attack_set_count <unit> <index> <count>) read
	and set how many the unit has, (player_move_origin <from> <to>) moves a
	player's own body to another unit */
	else if (!_stricmp(name, "unit_special_attack"))
		stubbs_engine_special_attack(arguments[0], (short)arguments[1]);
	else if (!_stricmp(name, "unit_special_attack_get_count"))
		value = stubbs_engine_special_attack_get_count(arguments[0], (short)arguments[1]);
	else if (!_stricmp(name, "unit_special_attack_set_count"))
		stubbs_engine_special_attack_set_count(arguments[0], (short)arguments[1], (short)arguments[2]);
	else if (!_stricmp(name, "player_move_origin"))
		stubbs_engine_player_move_origin(arguments[0], arguments[1]);
	/* (unit_finishing_move <unit>) and (ai_set_resurrection_group <ai>)
	(stubbs_horde.c) */
	else if (!_stricmp(name, "unit_finishing_move"))
		stubbs_horde_unit_finishing_move(arguments[0]);
	else if (!_stricmp(name, "ai_set_resurrection_group"))
		stubbs_horde_set_resurrection_group(arguments[0]);
	/* (image_effect_end) */
	else
		stubbs_image_effect_end();
	hs_return(thread_index, value);
	return TRUE;
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

	if (_stricmp(name, "objects_cannot_take_damage") &&
		_stricmp(name, "objects_destroy") && _stricmp(name, "modulo") && _stricmp(name, "ai_set_team"))
	{
		return FALSE;
	}
	arguments = hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (!arguments)
		return TRUE;
	/* (objects_cannot_take_damage <objects> <boolean>) */
	if (!_stricmp(name, "objects_cannot_take_damage"))
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
	/* (ai_set_team <ai> <team>): Halo's, but none for a team Halo has no
	room for (the encounter keeps its own) */
	else if (!_stricmp(name, "ai_set_team"))
	{
		if ((short)arguments[1] != NONE)
			ai_scripting_set_team(arguments[0], (short)arguments[1]);
		else
			stubbs_mode_log("ai_set_team: a team Halo has no room for; the encounter keeps its own");
	}
	hs_return(thread_index, value);
	return TRUE;
}

/* ---------- the player action tests

Stubbs's scripts wait on the player's buttons as Halo's do on
player_action_test_jump: each test is true once any player has pressed it
since the scripts' last (player_action_test_reset). Halo's own tests
(player_control.c) don't know Stubbs's buttons, so the input of each tick
is noted here as well (stubbs_functions_action_note). Stubbs's buttons
are Halo's in the same places, the way the retail game's help text names
them: melee is Halo's melee, its action Halo's action, flatulence Halo's
jump (A), the gut grenade Halo's grenade, the hand Halo's switch grenades
(black) and the head Halo's flashlight (white). The dance-off's
four directions are the left stick's (keyboard: W, S, A, D) */

enum
{
	_stubbs_action_melee_bit = 0,
	_stubbs_action_action_bit,
	_stubbs_action_flatulence_bit,
	_stubbs_action_grenade_bit,
	_stubbs_action_hand_bit,
	_stubbs_action_head_bit,
	_stubbs_action_dance_up_bit,
	_stubbs_action_dance_down_bit,
	_stubbs_action_dance_left_bit,
	_stubbs_action_dance_right_bit,
	_stubbs_action_abandon_bit,
};

/* how far the stick goes before a dance step counts (and it must be the
stick's larger direction, so a diagonal is one step, never two) */
#define STUBBS_DANCE_STEP_THRESHOLD 0.5f

static unsigned long stubbs_action_flags;
/* each local player's grenade, hand and head buttons last tick (the abandon
test wants a new press, not the first one still held) */
static unsigned long stubbs_action_held[4];
/* (player_action_test_enable <local player> <boolean>): a local player
whose input the tests don't count */
static unsigned long stubbs_action_players_disabled;

#ifndef HALO_CONSOLE_NO_STUBBS /* (the header's empty ones on a console) */
void stubbs_functions_action_note(
	short local_player_index,
	unsigned long unit_control_flags,
	boolean switch_grenades,
	real throttle_forward,
	real throttle_left)
{
	unsigned long flags = 0;
	real forward = throttle_forward < 0.f ? -throttle_forward : throttle_forward;
	real left = throttle_left < 0.f ? -throttle_left : throttle_left;

	if (local_player_index >= 0 && local_player_index < 32 &&
		TEST_FLAG(stubbs_action_players_disabled, local_player_index))
	{
		return;
	}
	if (TEST_FLAG(unit_control_flags, _unit_control_use_equipment_bit))
		flags |= FLAG(_stubbs_action_melee_bit);
	if (TEST_FLAG(unit_control_flags, _unit_control_action_bit))
		flags |= FLAG(_stubbs_action_action_bit);
	if (TEST_FLAG(unit_control_flags, _unit_control_jump_bit))
		flags |= FLAG(_stubbs_action_flatulence_bit);
	if (TEST_FLAG(unit_control_flags, _unit_control_throw_grenade_bit))
		flags |= FLAG(_stubbs_action_grenade_bit);
	if (switch_grenades)
		flags |= FLAG(_stubbs_action_hand_bit);
	if (TEST_FLAG(unit_control_flags, _unit_control_integrated_light_bit))
		flags |= FLAG(_stubbs_action_head_bit);
	if (forward >= STUBBS_DANCE_STEP_THRESHOLD && forward > left)
		flags |= FLAG(throttle_forward > 0.f ? _stubbs_action_dance_up_bit : _stubbs_action_dance_down_bit);
	else if (left >= STUBBS_DANCE_STEP_THRESHOLD && left > forward)
		flags |= FLAG(throttle_left > 0.f ? _stubbs_action_dance_left_bit : _stubbs_action_dance_right_bit);
	if (local_player_index >= 0 && local_player_index < (short)NUMBEROF(stubbs_action_held))
	{
		unsigned long specials = flags &
			(FLAG(_stubbs_action_grenade_bit) | FLAG(_stubbs_action_hand_bit) | FLAG(_stubbs_action_head_bit));

		if (specials & ~stubbs_action_held[local_player_index])
			flags |= FLAG(_stubbs_action_abandon_bit);
		stubbs_action_held[local_player_index] = specials;
	}
	stubbs_action_flags |= flags;
}

void stubbs_functions_action_reset(
	void)
{
	stubbs_action_flags = 0;
}
#endif

/* the action tests by name, and what each is true for (the retail game's
own descriptions of them): its button pressed since the last reset. The
ones of Stubbs's context actions (whistle, shove, possess, entering a
vehicle) are "the action key while it offers that"; with none of those
offers worked out yet, every press of the action key counts, and the
"_enabled" ones (the offer was made) are true: a tutorial that waits on
them moves on when the player does what it says */
static struct
{
	char const *name;
	short bit;
} const stubbs_action_tests[] =
{
	{ "player_action_test_melee", _stubbs_action_melee_bit },
	{ "player_action_test_flatulence", _stubbs_action_flatulence_bit },
	{ "player_action_test_grenade", _stubbs_action_grenade_bit },
	{ "player_action_test_hand", _stubbs_action_hand_bit },
	{ "player_action_test_head", _stubbs_action_head_bit },
	/* ("the abandon head/hand/grenade key": the help text has the grenade
	"pulled again to make it detonate" and black abort the hand, so the
	gut grenade's, the hand's or the head's button pressed anew; a30's
	tutorial resets between the two presses, and a button still held from
	the first doesn't count) */
	{ "player_action_test_abandon", _stubbs_action_abandon_bit },
	{ "player_action_test_dance_up", _stubbs_action_dance_up_bit },
	{ "player_action_test_dance_down", _stubbs_action_dance_down_bit },
	{ "player_action_test_dance_left", _stubbs_action_dance_left_bit },
	{ "player_action_test_dance_right", _stubbs_action_dance_right_bit },
	{ "player_action_test_whistle", _stubbs_action_action_bit },
	{ "player_action_test_shove", _stubbs_action_action_bit },
	{ "player_action_test_possess", _stubbs_action_action_bit },
	{ "player_action_test_vehicle_enter", _stubbs_action_action_bit },
	{ "player_action_test_whistle_enabled", NONE },
	{ "player_action_test_shove_enabled", NONE },
};

/* returns TRUE once it has returned the call's value */
static boolean stubbs_function_action_test(
	char const *name,
	short function_index,
	long thread_index,
	boolean initialize)
{
	long *arguments;
	short index;

	/* (player_action_test_enable <local player> <boolean>) */
	if (!_stricmp(name, "player_action_test_enable"))
	{
		arguments = hs_macro_function_evaluate(function_index, thread_index, initialize);
		if (arguments)
		{
			short local_player_index = (short)arguments[0];

			if (local_player_index >= 0 && local_player_index < 32)
				SET_FLAG(stubbs_action_players_disabled, local_player_index, !(arguments[1] & 0xFF));
			hs_return(thread_index, 0);
		}
		return TRUE;
	}
	for (index = 0; index < (short)NUMBEROF(stubbs_action_tests); index++)
	{
		if (!_stricmp(name, stubbs_action_tests[index].name))
		{
			short bit = stubbs_action_tests[index].bit;

			hs_return(thread_index, bit == NONE ? TRUE : TEST_FLAG(stubbs_action_flags, bit) ? TRUE : FALSE);
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------- what the scripts say an object cannot do

(objects_cannot_die <objects> <boolean>): takes damage down to no health,
but doesn't die (a60's boss: the scripts watch his health reach zero, then
fill it again). (objects_cannot_lose_regions <objects> <boolean>): his
regions (an arm) stay on however hurt. (objects_cannot_be_finished
<objects> <boolean>) and (objects_cannot_be_possessed <objects> <boolean>):
no brains eaten, no hand on his head (stubbs_horde.c, stubbs_possession.c).
Kept here by object index, outside the game state: a saved game loaded
later forgets them, as it does the scripts' other asides. A new map (its
time starting over) clears them. */

enum
{
	MAXIMUM_STUBBS_OBJECT_FLAGS = 64,
};

static struct
{
	long object_index;
	unsigned long flags;
} stubbs_object_flags[MAXIMUM_STUBBS_OBJECT_FLAGS];
static long stubbs_object_flags_time = -1;

static void stubbs_object_flags_forget_old(
	void)
{
	long time = game_time_get();

	if (time < stubbs_object_flags_time)
		memset(stubbs_object_flags, 0, sizeof(stubbs_object_flags));
	stubbs_object_flags_time = time;
}

static void stubbs_object_flag_set(
	long object_index,
	short bit,
	boolean set)
{
	long index, free_index = NONE;

	stubbs_object_flags_forget_old();
	for (index = 0; index < MAXIMUM_STUBBS_OBJECT_FLAGS; index++)
	{
		if (stubbs_object_flags[index].flags && stubbs_object_flags[index].object_index == object_index)
		{
			SET_FLAG(stubbs_object_flags[index].flags, bit, set);
			return;
		}
		if (!stubbs_object_flags[index].flags && free_index == NONE)
			free_index = index;
	}
	if (set && free_index != NONE)
	{
		stubbs_object_flags[free_index].object_index = object_index;
		stubbs_object_flags[free_index].flags = FLAG(bit);
	}
}

boolean stubbs_functions_object_cannot(
	long object_index,
	short what)
{
	long index;

	if (object_index == NONE || !game_in_progress())
		return FALSE;
	stubbs_object_flags_forget_old();
	for (index = 0; index < MAXIMUM_STUBBS_OBJECT_FLAGS; index++)
	{
		if (stubbs_object_flags[index].flags && stubbs_object_flags[index].object_index == object_index)
			return TEST_FLAG(stubbs_object_flags[index].flags, what) ? TRUE : FALSE;
	}
	return FALSE;
}

/* (a region's index in the object's model, by name: NONE for none) */
static short stubbs_object_region_index(
	long object_index,
	char const *name)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct object_definition *definition;
	struct model *model;
	short region_index;

	if (!object || !name)
		return NONE;
	definition = object_definition_get(object->definition_index);
	if (definition->object.model.index == NONE)
		return NONE;
	model = model_definition_get(definition->object.model.index);
	for (region_index = 0; region_index < model->regions.count && region_index < MAXIMUM_REGIONS_PER_OBJECT; region_index++)
	{
		struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);

		if (!_stricmp(region->name, name))
			return region_index;
	}
	return NONE;
}

static short stubbs_object_flag_bit_of_function(
	char const *name)
{
	if (!_stricmp(name, "objects_cannot_die"))
		return _stubbs_object_cannot_die_bit;
	if (!_stricmp(name, "objects_cannot_lose_regions"))
		return _stubbs_object_cannot_lose_regions_bit;
	if (!_stricmp(name, "objects_cannot_be_finished"))
		return _stubbs_object_cannot_be_finished_bit;
	if (!_stricmp(name, "objects_cannot_be_possessed"))
		return _stubbs_object_cannot_be_possessed_bit;
	return NONE;
}

/* returns TRUE once it has returned the call's value */
static boolean stubbs_function_object_flags(
	char const *name,
	short function_index,
	long thread_index,
	boolean initialize)
{
	long *arguments;
	long value = 0;
	short bit = stubbs_object_flag_bit_of_function(name);

	if (bit == NONE && _stricmp(name, "object_region_destroyed") && _stricmp(name, "unit_taking_damage"))
		return FALSE;
	arguments = hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (!arguments)
		return TRUE;
	if (bit != NONE)
	{
		long reference, object_index;

		for (object_index = object_list_get_first(arguments[0], &reference); object_index != NONE;
			object_index = object_list_get_next(arguments[0], &reference))
		{
			stubbs_object_flag_set(object_index, bit, (arguments[1] & 0xFF) ? TRUE : FALSE);
		}
	}
	/* (object_region_destroyed <object> <region name>): "returns true if the
	named region of the object has been destroyed" */
	else if (!_stricmp(name, "object_region_destroyed"))
	{
		struct object_datum *object = arguments[0] != NONE ? object_try_and_get(arguments[0]) : NULL;
		short region_index = object ? stubbs_object_region_index(arguments[0],
			(char const *)xbox_pointer((unsigned long)arguments[1])) : NONE;

		value = region_index != NONE && TEST_FLAG(object->object.regions_destroyed_flags, region_index) ? TRUE : FALSE;
	}
	/* (unit_taking_damage <unit>): hurt just now */
	else
	{
		struct object_datum *object = arguments[0] != NONE ? object_try_and_get(arguments[0]) : NULL;

		value = object && object->object.recent_body_damage > 0.f ? TRUE : FALSE;
	}
	hs_return(thread_index, value);
	return TRUE;
}

/* ---------- the questions the scripts ask about objects

Each as the retail game's own note on it says (Stubbs.exe's function
descriptions); the levels wait on them (b30, c40: how close an object is to
a camera point; a50, b30, c10: who is in a volume; most levels: what kind of
unit the player is) */

static long stubbs_real_bits(real value)
{
	long bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

/* returns TRUE once it has returned the call's value */
static boolean stubbs_function_query(
	char const *name,
	short function_index,
	long thread_index,
	boolean initialize)
{
	long *arguments;
	long value = 0;

	if (_stricmp(name, "objects_in_volume") && _stricmp(name, "object_is_instance") &&
		_stricmp(name, "object_distance_to_camera_point") && _stricmp(name, "objects_distance") &&
		_stricmp(name, "unit_get_weapon"))
	{
		return FALSE;
	}
	arguments = hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (!arguments)
		return TRUE;
	/* (objects_in_volume <objects> <volume>): "trims the object list to only
	include objects within the specified volume" */
	if (!_stricmp(name, "objects_in_volume"))
	{
		long reference, object_index;
		short volume = (short)arguments[1];

		value = object_list_new();
		for (object_index = object_list_get_first(arguments[0], &reference); object_index != NONE;
			object_index = object_list_get_next(arguments[0], &reference))
		{
			if (volume != NONE && value != NONE && scenario_trigger_volume_test_object(volume, object_index))
				object_list_add(value, object_index);
		}
	}
	/* (object_is_instance <object> <object definition>): "returns true if
	the object is of the specified type" */
	else if (!_stricmp(name, "object_is_instance"))
	{
		struct object_datum *object = arguments[0] != NONE ? object_try_and_get(arguments[0]) : NULL;

		value = object && arguments[1] != NONE && object->definition_index == arguments[1];
	}
	/* (object_distance_to_camera_point <camera point> <object>): "returns
	the distance from the object to the cutscene flag" (-1 without one) */
	else if (!_stricmp(name, "object_distance_to_camera_point"))
	{
		struct tag_block *points = &global_scenario_get()->cutscene_camera_points;
		short point_index = (short)arguments[0];
		real distance = -1.f;

		if (point_index >= 0 && point_index < points->count && arguments[1] != NONE &&
			object_try_and_get(arguments[1]))
		{
			struct scenario_cutscene_camera_point *point = TAG_BLOCK_GET_ELEMENT(points, point_index,
				struct scenario_cutscene_camera_point);
			real_point3d origin;

			object_get_origin(arguments[1], &origin);
			distance = square_root(distance_squared3d(&origin, &point->position));
		}
		value = stubbs_real_bits(distance);
	}
	/* (objects_distance <objects> <object>): "returns the shortest distance
	between any of the objects in the list and the specified object" (-1
	for none) */
	else if (!_stricmp(name, "objects_distance"))
	{
		long reference, object_index;
		real shortest = -1.f;
		real_point3d origin, other;

		if (arguments[1] != NONE && object_try_and_get(arguments[1]))
		{
			object_get_origin(arguments[1], &origin);
			for (object_index = object_list_get_first(arguments[0], &reference); object_index != NONE;
				object_index = object_list_get_next(arguments[0], &reference))
			{
				real distance;

				object_get_origin(object_index, &other);
				distance = square_root(distance_squared3d(&origin, &other));
				if (shortest < 0.f || distance < shortest)
					shortest = distance;
			}
		}
		value = stubbs_real_bits(shortest);
	}
	/* (unit_get_weapon <unit>): "returns the weapon that the unit is
	currently wielding" */
	else
	{
		struct unit_datum *unit = arguments[0] != NONE ? unit_try_and_get(arguments[0]) : NULL;

		value = unit ? unit_inventory_get_weapon(arguments[0], unit->unit.current_weapon_index) : NONE;
	}
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
	if (stubbs_function_routed(function->definition.name, function_index, thread_index, initialize))
		return;
	if (stubbs_function_native(function->definition.name, function_index, thread_index, initialize))
		return;
	if (stubbs_function_object_flags(function->definition.name, function_index, thread_index, initialize))
		return;
	if (stubbs_function_action_test(function->definition.name, function_index, thread_index, initialize))
		return;
	if (stubbs_function_query(function->definition.name, function_index, thread_index, initialize))
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

/* ---------- a name placed on demand

A Stubbs map places some of its named units "not automatically" and its
scripts hand the name straight to ai_attach (a10's spare zombies for the
whistle lesson): the name has no object until it is used. hs_cast
(hs_runtime.c) creates such a unit as a script casts its name, for a name
whose placement says so and that has no object yet. */

boolean stubbs_functions_name_placed_on_demand(
	short object_name_index)
{
	struct scenario *scenario = global_scenario_get();
	struct scenario_object_name *name;
	struct tag_block *placements;
	struct scenario_object_datum *placement;
	long placement_size;

	if (!scenario || !VALID_INDEX(object_name_index, scenario->object_names.count))
		return FALSE;
	name = TAG_BLOCK_GET_ELEMENT(&scenario->object_names, object_name_index, struct scenario_object_name);
	if (!VALID_INDEX(name->runtime_object_type, NUMBER_OF_OBJECT_TYPES) ||
		object_type_definition_get(name->runtime_object_type)->placement_tag_block_offset == NONE ||
		object_type_definition_get(name->runtime_object_type)->palette_tag_block_offset == NONE)
	{
		return FALSE;
	}
	placements = scenario_get_object_type_scenario_datums(scenario, name->runtime_object_type, &placement_size);
	if (!placements || !VALID_INDEX(name->runtime_scenario_datum_index, placements->count))
		return FALSE;
	placement = (struct scenario_object_datum *)tag_block_get_element_with_size(
		placements, name->runtime_scenario_datum_index, placement_size);
	return TEST_FLAG(placement->placement_flags, 0) != 0; /* (bit 0: not placed automatically) */
}
