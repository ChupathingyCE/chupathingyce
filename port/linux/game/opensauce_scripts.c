/*
OPENSAUCE_SCRIPTS.C

The script functions and globals the scripts of OpenSauce's maps call that
the game lacks (opensauce_scripts.h), as a table after Halo's and Stubbs
mode's, there only on a map with an OpenSauce header (opensauce_map.c).
Without them, the game drops every script that calls one (hs_compile.c):
SPV3 lost 8 to 41 percent of each map's scripts so.

What each does is OpenSauce's (Kornner Studios, GPL-3.0-or-later): its
script definitions (ScriptLibrary.Definitions.*.inl) for their names and
signatures, ObjectFieldDefinitions for the data names and the fields they
are, and its evaluators (Objects.Scripting.inl, Players.Scripting.inl) for
what a call does with a name it doesn't know (nothing, and -1 or none
back). Read as facts; the code is this game's:
  - the object, unit, weapon and player data functions read and write the
    named field of this game's object (Halo PC's object, unit and weapon
    layouts past the object header are the Xbox's: lua_twiddlings_objects.c),
    a set refused, as OpenSauce refuses it, for a field it does not let
    a network game change;
  - weapon_data_trigger_set_real changes the weapon's tag, as OpenSauce's
    does, for every weapon of it, until the map loads again;
  - the numbers and bits (abs_real, bit_test, bit_toggle), distances, and
    physics_set_gravity (opensauce_map.c puts the gravity back);
  - post-processing's (pp_*) go to the map's post-processing
    (opensauce_postprocess.c), where the map has some and the build draws
    it; elsewhere an effect instance's active flag and fade are kept, the
    fade done at once, as nothing is drawn;
  - display_scripted_ui_widget opens the widget the map's project_yellow
    (or project_yellow_globals) lists under the name, as OpenSauce's
    UIWidgets.cpp does: a widget of its own, the one open closed (none on
    the dedicated server, as OpenSauce's has none);
  - ai_transform_actor, ai_transform_actors, ai_transform_actors_by_type
    and ai_actor_is_transforming start and ask after the map's actor
    variant transforms (opensauce_transforms.c), on the host;
  - structure_bsp_set_lightmap_set and structure_bsp_set_sky_set choose a
    BSP's lightmap or sky set by name (opensauce_bsp_sets.c);
  - play_bink_movie plays the movie over the game, as OpenSauce's does,
    the map pack's own file first (opensauce_play_bink_movie);
  - what needs a part of OpenSauce the game does not have yet answers as
    that part would with nothing to show, and says so once: faux zones'
    skies, runtime vectors, and the third and fourth grenade counts on a map
    whose globals list only two.
The globals are Halo PC's network globals (vehicle_incremental_rate and the
rest, which SPV3's scripts use as their own variables) and OpenSauce's motion
blur ones, of the types Halo PC's and OpenSauce's scripts give them
(BlamLib's Halo1_CE_Scripting.xml, in OpenSauce's repository).
*/

#include "cseries.h"
#include "errors.h"
#include "opensauce_scripts.h"

#ifdef OPENSAUCE_SCRIPTS

#include "hs/hs.h"
#include "hs/object_lists.h"
#include "game/game.h"
#include "game/players.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "bink/bink_playback.h"
#include "interface/ui_widget.h"
#include "halo_map_packs.h"
#include "opensauce.h"
#include "opensauce_postprocess.h"
#include "opensauce_transforms.h"
#include "opensauce_bsp_sets.h"
#include "stubbs_functions.h"
#include "network_coop.h"
#include "networking/network_game_globals.h"

#include <math.h>
#include <string.h>

/* ---------- constants */

enum
{
	MAXIMUM_OPENSAUCE_PARAMETERS = 7,
	/* (effect_postprocess_collection's instances: tag_schema_opensauce.c) */
	MAXIMUM_OPENSAUCE_EFFECT_INSTANCES = 32,
};

/* the most physics_set_gravity takes */
#define OPENSAUCE_MAXIMUM_GRAVITY_SCALE 10.0f

/* ---------- structures */

struct opensauce_script_function
{
	struct hs_function_definition definition;
	/* (the parameters after the first follow the definition: hs.h) */
	short more_parameter_types[MAXIMUM_OPENSAUCE_PARAMETERS - 1];
	short types[MAXIMUM_OPENSAUCE_PARAMETERS];
	/* (a function not made real yet says so once) */
	boolean logged;
};

struct opensauce_script_global
{
	char const *name;
	short type;
	long initial;
};

/* ---------- prototypes */

boolean hs_macro_function_parse(short function_index, long expression_index);
extern long const hs_function_table_count;
static void opensauce_script_evaluate(short function_index, long thread_index, boolean initialize);

/* ---------- globals */

#define OPENSAUCE_SCRIPT_FUNCTION(name, return_type, count, ...) \
	{ { return_type, 0, name, hs_macro_function_parse, opensauce_script_evaluate, "OpenSauce's", NULL, count, \
		{ 0 } }, { 0 }, __VA_ARGS__, FALSE }

/* (OpenSauce's signatures, or Halo PC's for the two sound gains: every one
SPV3's scripts call, and the actor variant transforms') */
static struct opensauce_script_function opensauce_script_functions[] =
{
	OPENSAUCE_SCRIPT_FUNCTION("weapon_data_get_real", _hs_type_real, 2, { _hs_type_object, _hs_type_string }),
	OPENSAUCE_SCRIPT_FUNCTION("weapon_data_set_real", _hs_type_void, 3,
		{ _hs_type_object, _hs_type_string, _hs_type_real }),
	OPENSAUCE_SCRIPT_FUNCTION("weapon_data_trigger_set_real", _hs_type_void, 5,
		{ _hs_type_object, _hs_type_long_integer, _hs_type_string, _hs_type_string, _hs_type_real }),
	OPENSAUCE_SCRIPT_FUNCTION("objects_distance_to_object", _hs_type_real, 2, { _hs_type_object_list, _hs_type_object }),
	OPENSAUCE_SCRIPT_FUNCTION("object_data_get_real", _hs_type_real, 3,
		{ _hs_type_object, _hs_type_string, _hs_type_string }),
	OPENSAUCE_SCRIPT_FUNCTION("object_data_set_real", _hs_type_void, 4,
		{ _hs_type_object, _hs_type_string, _hs_type_string, _hs_type_real }),
	OPENSAUCE_SCRIPT_FUNCTION("object_data_set_vector", _hs_type_boolean, 3,
		{ _hs_type_object, _hs_type_string, _hs_type_short_integer }),
	OPENSAUCE_SCRIPT_FUNCTION("unit_data_get_object", _hs_type_object, 2, { _hs_type_unit, _hs_type_string }),
	OPENSAUCE_SCRIPT_FUNCTION("unit_data_get_integer", _hs_type_long_integer, 2, { _hs_type_unit, _hs_type_string }),
	OPENSAUCE_SCRIPT_FUNCTION("unit_data_set_integer", _hs_type_void, 3,
		{ _hs_type_unit, _hs_type_string, _hs_type_long_integer }),
	OPENSAUCE_SCRIPT_FUNCTION("unit_data_get_real", _hs_type_real, 2, { _hs_type_unit, _hs_type_string }),
	OPENSAUCE_SCRIPT_FUNCTION("unit_data_set_real", _hs_type_void, 3, { _hs_type_unit, _hs_type_string, _hs_type_real }),
	OPENSAUCE_SCRIPT_FUNCTION("player_data_get_object", _hs_type_object, 2,
		{ _hs_type_short_integer, _hs_type_string }),
	OPENSAUCE_SCRIPT_FUNCTION("player_data_set_real", _hs_type_void, 3,
		{ _hs_type_short_integer, _hs_type_string, _hs_type_real }),
	OPENSAUCE_SCRIPT_FUNCTION("abs_real", _hs_type_real, 1, { _hs_type_real }),
	OPENSAUCE_SCRIPT_FUNCTION("bit_test", _hs_type_boolean, 2, { _hs_type_long_integer, _hs_type_short_integer }),
	OPENSAUCE_SCRIPT_FUNCTION("bit_toggle", _hs_type_long_integer, 3,
		{ _hs_type_long_integer, _hs_type_short_integer, _hs_type_boolean }),
	OPENSAUCE_SCRIPT_FUNCTION("physics_set_gravity", _hs_type_void, 1, { _hs_type_real }),
	/* (post-processing: kept, not drawn) */
	OPENSAUCE_SCRIPT_FUNCTION("pp_set_effect_instance_active", _hs_type_void, 2,
		{ _hs_type_short_integer, _hs_type_boolean }),
	OPENSAUCE_SCRIPT_FUNCTION("pp_set_effect_instance_fade", _hs_type_void, 4,
		{ _hs_type_short_integer, _hs_type_real, _hs_type_real, _hs_type_real }),
	OPENSAUCE_SCRIPT_FUNCTION("pp_get_effect_instance_current_fade", _hs_type_real, 1, { _hs_type_short_integer }),
	OPENSAUCE_SCRIPT_FUNCTION("pp_set_effect_shader_variable_integer", _hs_type_void, 4,
		{ _hs_type_short_integer, _hs_type_short_integer, _hs_type_long_integer, _hs_type_real }),
	OPENSAUCE_SCRIPT_FUNCTION("pp_set_effect_shader_variable_real", _hs_type_void, 7,
		{ _hs_type_short_integer, _hs_type_short_integer, _hs_type_real, _hs_type_real, _hs_type_real,
			_hs_type_real, _hs_type_real }),
	OPENSAUCE_SCRIPT_FUNCTION("pp_bloom_set_maximum_color", _hs_type_void, 4,
		{ _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_real }),
	OPENSAUCE_SCRIPT_FUNCTION("pp_bloom_set_exposure", _hs_type_void, 2, { _hs_type_real, _hs_type_real }),
	/* (a movie: opensauce_play_bink_movie) */
	OPENSAUCE_SCRIPT_FUNCTION("play_bink_movie", _hs_type_void, 1, { _hs_type_string }),
	/* (BSP lightmap and sky sets: opensauce_bsp_sets.c) */
	OPENSAUCE_SCRIPT_FUNCTION("structure_bsp_set_sky_set", _hs_type_boolean, 2,
		{ _hs_type_short_integer, _hs_type_string }),
	OPENSAUCE_SCRIPT_FUNCTION("structure_bsp_set_lightmap_set", _hs_type_boolean, 2,
		{ _hs_type_short_integer, _hs_type_string }),
	/* (parts the game does not have yet) */
	OPENSAUCE_SCRIPT_FUNCTION("display_scripted_ui_widget", _hs_type_boolean, 1, { _hs_type_string }),
	OPENSAUCE_SCRIPT_FUNCTION("scenario_faux_zone_switch_sky", _hs_type_boolean, 1, { _hs_type_string }),
	OPENSAUCE_SCRIPT_FUNCTION("runtime_vector_set", _hs_type_boolean, 5,
		{ _hs_type_short_integer, _hs_type_string, _hs_type_real, _hs_type_real, _hs_type_real }),
	/* (Halo PC's, which the Xbox's scripts never had: opensauce_map.c's sound
	gains) */
	OPENSAUCE_SCRIPT_FUNCTION("sound_set_music_gain", _hs_type_void, 1, { _hs_type_real }),
	OPENSAUCE_SCRIPT_FUNCTION("sound_set_effects_gain", _hs_type_void, 1, { _hs_type_real }),
	/* (the actor variant transforms': opensauce_transforms.c) */
	OPENSAUCE_SCRIPT_FUNCTION("ai_transform_actor", _hs_type_boolean, 3,
		{ _hs_type_object, _hs_type_string, _hs_type_string }),
	OPENSAUCE_SCRIPT_FUNCTION("ai_transform_actors", _hs_type_boolean, 3,
		{ _hs_type_object_list, _hs_type_string, _hs_type_string }),
	OPENSAUCE_SCRIPT_FUNCTION("ai_transform_actors_by_type", _hs_type_boolean, 4,
		{ _hs_type_object_list, _hs_type_actor_variant, _hs_type_string, _hs_type_string }),
	OPENSAUCE_SCRIPT_FUNCTION("ai_actor_is_transforming", _hs_type_boolean, 1, { _hs_type_object }),
};

/* (Halo PC's network globals, and OpenSauce's motion blur's) */
static struct opensauce_script_global const opensauce_script_globals[] =
{
	{ "speed_hack_log_level", _hs_type_long_integer, 0 },
	{ "vehicle_incremental_rate", _hs_type_long_integer, 0 },
	{ "biped_incremental_rate", _hs_type_long_integer, 0 },
	{ "projectile_incremental_rate", _hs_type_long_integer, 0 },
	{ "oddball_baseline_rate", _hs_type_long_integer, 0 },
	{ "local_player_update_rate", _hs_type_long_integer, 0 },
	{ "local_player_log_level", _hs_type_long_integer, 0 },
	{ "net_bandwidth", _hs_type_long_integer, 0 },
	{ "cl_remote_player_action_queue_limit", _hs_type_long_integer, 0 },
	{ "cl_remote_player_action_queue_tick_limit", _hs_type_long_integer, 0 },
	{ "leaf_to_leaf_latency", _hs_type_boolean, 0 },
	{ "transport_dumping", _hs_type_boolean, 0 },
	{ "log_server_player_update_history", _hs_type_boolean, 0 },
	{ "use_new_vehicle_update_scheme", _hs_type_boolean, 0 },
	{ "use_super_remote_players_action_update", _hs_type_boolean, 0 },
	{ "pp_motion_blur_enabled", _hs_type_boolean, 0 },
	{ "pp_motion_blur_amount", _hs_type_real, 0 },
};

static boolean opensauce_script_functions_ready = FALSE;

/* post-processing's effect instances, as scripts set them */
static struct
{
	boolean active;
	real fade;
} opensauce_script_effect_instances[MAXIMUM_OPENSAUCE_EFFECT_INSTANCES];

/* ---------- private code */

static void opensauce_script_log_once(
	struct opensauce_script_function *function,
	char const *what)
{
	if (function->logged)
		return;
	function->logged = TRUE;
	error(_error_silent, "OpenSauce scripts: %s %s", function->definition.name, what);
}

static real opensauce_real_argument(
	long argument)
{
	real value;

	memcpy(&value, &argument, sizeof(value));
	return value;
}

static long opensauce_real_result(
	real value)
{
	long bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static char const *opensauce_string_argument(
	long argument)
{
	char const *string = argument ? (char const *)xbox_pointer((unsigned long)argument) : NULL;

	return string ? string : "";
}

/* whether a real a script sets is a number (not NaN or infinite: no field
takes one) */
static boolean opensauce_real_settable(
	long argument)
{
	real value = opensauce_real_argument(argument);

	return value == value && value > -1.0e30f && value < 1.0e30f;
}

/* whether the scripts may set a field (networked: one OpenSauce lets a
network game change). Host-authoritative: in network co-op only the host
runs a map's scripts (game.c) and its sync carries the results to clients
(network_coop.c, network_objects.c), so the host sets any field, as a local
game does; another network game's host only those OpenSauce lets it (its
IsLocal rule); a client never (its scripts don't run, and nothing here may
make its simulation differ from the host's) */
static boolean opensauce_script_may_set(
	boolean networked)
{
	if (game_connection() == _game_connection_local)
		return TRUE;
	if (network_game_distributed_client())
		return FALSE;
	return network_coop_active() || networked;
}

/* a vector's element from its name's first letter (x, y, z), as OpenSauce's
VectorFieldNameToIndex */
static short opensauce_vector_element(
	char const *name)
{
	switch (name[0])
	{
	case 'y': case 'j': return 1;
	case 'z': case 'k': return 2;
	}
	return 0;
}

/* an object's real by OpenSauce's name (object_data_get_real and _set_real):
NULL if it has none of that name. Networked: whether a network game may set
it */
static real *opensauce_object_real(
	long object_index,
	char const *name,
	char const *subname,
	boolean *networked,
	boolean *readonly)
{
	struct object_datum *object = object_try_and_get(object_index);
	real_vector3d *vector = NULL;

	*networked = FALSE;
	/* (OpenSauce's read-only ones: the function outputs, radius, scale and
	center, ObjectFieldDefinitions.inl) */
	*readonly = !strcmp(name, "function_out") || !strcmp(name, "radius") || !strcmp(name, "scale") ||
		!strcmp(name, "center");
	if (!object)
		return NULL;
	if (!strcmp(name, "function_out"))
	{
		short function_index = subname[0] >= 'a' && subname[0] <= 'd' ? (short)(subname[0] - 'a') : NONE;

		if (function_index == NONE || !TEST_FLAG(object->object.functions_active_flags, function_index))
			return NULL;
		return &object->object.outgoing_function_values[function_index];
	}
	if (!strcmp(name, "radius"))
		return &object->object.bounding_sphere_radius;
	if (!strcmp(name, "scale"))
		return &object->object.scale;
	/* (OpenSauce keeps the vitality names for old scripts, and answers them
	with the health and the shield's maximum, as here) */
	if (!strcmp(name, "maximum_vitality") || !strcmp(name, "maximum_health"))
		return &object->object.maximum_body_vitality;
	if (!strcmp(name, "current_vitality") || !strcmp(name, "maximum_shield"))
		return &object->object.maximum_shield_vitality;
	if (!strcmp(name, "current_health"))
		return &object->object.body_vitality;
	if (!strcmp(name, "current_shield"))
		return &object->object.shield_vitality;
	*networked = TRUE;
	if (!strcmp(name, "position"))
		vector = (real_vector3d *)&object->object.position;
	else if (!strcmp(name, "transitional_velocity"))
		vector = &object->object.translational_velocity;
	else if (!strcmp(name, "forward"))
		vector = &object->object.forward;
	else if (!strcmp(name, "up"))
		vector = &object->object.up;
	else if (!strcmp(name, "angular_velocity"))
		vector = &object->object.angular_velocity;
	else if (!strcmp(name, "center"))
	{
		*networked = FALSE;
		vector = (real_vector3d *)&object->object.bounding_sphere_center;
	}
	else if (TEST_FLAG(_object_mask_unit, object->object.type))
	{
		struct unit_datum *unit = (struct unit_datum *)object;

		if (!strcmp(name, "desired_facing"))
			vector = &unit->unit.desired_facing_vector;
		else if (!strcmp(name, "desired_aiming"))
			vector = &unit->unit.desired_aiming_vector;
		else if (!strcmp(name, "aiming"))
			vector = &unit->unit.aiming_vector;
		else if (!strcmp(name, "aiming_velocity"))
			vector = &unit->unit.aiming_velocity;
		else if (!strcmp(name, "looking"))
			vector = &unit->unit.looking_vector;
		else if (!strcmp(name, "looking_velocity"))
			vector = &unit->unit.looking_velocity;
		*networked = !strcmp(name, "desired_facing");
	}
	if (!vector)
		return NULL;
	return &((real *)vector)[opensauce_vector_element(subname)];
}

/* a unit's real by OpenSauce's name (unit_data_get_real and _set_real) */
static real *opensauce_unit_real(
	struct unit_datum *unit,
	char const *name)
{
	if (!strcmp(name, "driver_power"))
		return &unit->unit.seat_power[0];
	if (!strcmp(name, "gunner_power"))
		return &unit->unit.seat_power[1];
	if (!strcmp(name, "integrated_light_power"))
		return &unit->unit.integrated_light_power;
	/* (Halo PC's names for the Xbox's fields in the same places:
	unit_structures.hpp's offsets) */
	if (!strcmp(name, "integrated_light_toggle_power"))
		return &unit->unit.integrated_light_battery;
	if (!strcmp(name, "integrated_night_vision_toggle_power"))
		return &unit->unit.integrated_night_vision_power;
	if (!strcmp(name, "camo_power"))
		return &unit->unit.active_camouflage;
	if (!strcmp(name, "full_spectrum_vision_power"))
		return &unit->unit.active_camouflage_super_amount;
	return NULL;
}

/* a unit's integer by OpenSauce's name: its value into *value, FALSE if it
has none of that name; with set, *value written to it if it may be */
static boolean opensauce_unit_integer(
	struct opensauce_script_function *function,
	struct unit_datum *unit,
	char const *name,
	long *value,
	boolean set)
{
	char *byte_field = NULL;
	short *short_field = NULL;
	boolean writable = FALSE, networked = FALSE;

	if (!strcmp(name, "total_grenade_count[frag]"))
		byte_field = &unit->unit.grenade_counts[0], writable = networked = TRUE;
	else if (!strcmp(name, "total_grenade_count[plasma]"))
		byte_field = &unit->unit.grenade_counts[1], writable = networked = TRUE;
	else if (!strcmp(name, "total_grenade_count[custom2]") || !strcmp(name, "total_grenade_count[custom3]"))
	{
		/* (OpenSauce's third and fourth grenades: a biped's, where the map's
		globals list them, mcc_grenades.c; none elsewhere) */
		if (opensauce_map_four_grenades() && unit->object.type == _object_type_biped)
			byte_field = &((struct biped_datum *)unit)->biped.opensauce_grenade_counts[name[26] == '2' ? 0 : 1],
				writable = networked = TRUE;
		else
		{
			opensauce_script_log_once(function, "names a third or fourth grenade type the map's globals don't list: 0, "
				"and set to nothing");
			*value = 0;
			return TRUE;
		}
	}
	else if (!strcmp(name, "current_grenade_index"))
		byte_field = &unit->unit.current_grenade_index;
	else if (!strcmp(name, "zoom_level"))
		byte_field = &unit->unit.current_zoom_level;
	else if (!strcmp(name, "desired_zoom_level"))
		byte_field = &unit->unit.desired_zoom_level, writable = TRUE;
	else if (!strcmp(name, "vehicle_seat_index"))
		short_field = &unit->unit.parent_seat_index;
	/* (read-only, as OpenSauce has it: ObjectFieldDefinitions.inl) */
	else if (!strcmp(name, "current_weapon_index"))
		short_field = &unit->unit.current_weapon_index;
	else if (!strcmp(name, "feign_death_timer"))
		short_field = &unit->unit.feign_death_timer;
	else if (!strcmp(name, "killing_spree_count"))
		short_field = &unit->unit.killing_spree_count;
	else
		return FALSE;
	if (set)
	{
		/* (a weapon index past the unit's weapons refused: the game indexes
		its weapons by it, units.c's unit_get_weapon) */
		if (short_field == &unit->unit.current_weapon_index &&
			(*value < 0 || *value >= MAXIMUM_WEAPONS_PER_UNIT))
		{
			writable = FALSE;
		}
		if (writable && opensauce_script_may_set(networked))
		{
			if (byte_field)
				*byte_field = (char)*value;
			else
				*short_field = (short)*value;
		}
		return TRUE;
	}
	/* (bytes read unsigned, as OpenSauce reads them) */
	*value = byte_field ? (long)(unsigned char)*byte_field : (long)*short_field;
	return TRUE;
}

/* the player in the players' list at list_index (network_player's), or NULL */
static struct player_datum *opensauce_player_at(
	short list_index)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (list_index < 0 || !player_data)
		return NULL;
	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		if (player->network_player_data.player_list_index == list_index)
			return player;
	}
	return NULL;
}

#ifndef HALO_SERVER
/* play_bink_movie: the named movie played over the game, as OpenSauce plays
it (Halo PC's bink_playback_start, in a local game only), and the script goes
on. Only the file's own name is used, and it must be a .bik: the pack's
movie first (its bink folder, maps_spv3\bink\attract.bik), else the game's
(d:\bink\attract.bik), else it is skipped with one line (halo_map_packs.h,
docs/maps.md). A button press stops it; the menus or the level stay where
they were when it ends. */
boolean main_menu_is_active(void);
int config_boolean(char const *name);

static void opensauce_play_bink_movie(
	struct opensauce_script_function *function,
	char const *name)
{
	char movie[96], path[256];
	char const *file = name ? name : "";
	char const *cursor;
	size_t length;

	for (cursor = file; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			file = cursor + 1;
	}
	length = strlen(file);
	if (length < 5 || length > 64 || _stricmp(file + length - 4, ".bik") || strchr(file, ':'))
	{
		opensauce_script_log_once(function, "was given a name that isn't a movie's file: not played");
		return;
	}
	/* (OpenSauce's: a network client's own game plays none) */
	if (game_connection() != _game_connection_local || bink_playback_in_progress())
		return;
	/* (the menus' movie is the mod's intro, SPV3's attract.bik: the setting
	that skips the game's intro skips it too) */
	if (main_menu_is_active() && config_boolean("display.skip_intro"))
		return;
	snprintf(movie, sizeof(movie), "d:\\bink\\%s", file);
	if (!map_pack_menus_movie_path(movie, path, sizeof(path)))
		snprintf(path, sizeof(path), "%s", movie);
	bink_playback_start(path, FLAG(_bink_playback_button_click_stops_movie_bit) |
		FLAG(_bink_playback_prevent_events_to_ui_bit) | FLAG(_bink_playback_dont_render_ui_bit));
	error(_error_silent, "OpenSauce scripts: play_bink_movie '%s': %s", file,
		bink_playback_active() ? path : "not played");
}
#endif

/* the functions made real: TRUE once the call's value is returned */
static boolean opensauce_script_data(
	struct opensauce_script_function *function,
	long *arguments,
	long *value)
{
	char const *name = function->definition.name;

	if (!strcmp(name, "objects_distance_to_object"))
	{
		real minimum = -1.f;

		if (arguments[1] != NONE && object_try_and_get(arguments[1]))
		{
			real_point3d destination, origin;
			long reference, object_index;

			object_get_origin(arguments[1], &destination);
			for (object_index = object_list_get_first(arguments[0], &reference); object_index != NONE;
				object_index = object_list_get_next(arguments[0], &reference))
			{
				real distance;

				/* (a listed object deleted since is passed over: #1861) */
				if (!object_try_and_get(object_index))
					continue;
				distance = distance3d(object_get_origin(object_index, &origin), &destination);
				if (minimum < 0.f || distance < minimum)
					minimum = distance;
			}
		}
		*value = opensauce_real_result(minimum);
	}
	else if (!strcmp(name, "object_data_get_real") || !strcmp(name, "object_data_set_real"))
	{
		boolean networked, readonly;
		real *field = arguments[0] != NONE ? opensauce_object_real(arguments[0], opensauce_string_argument(arguments[1]),
			opensauce_string_argument(arguments[2]), &networked, &readonly) : NULL;

		if (name[12] == 'g')
			*value = opensauce_real_result(field ? *field : -1.f);
		else if (field && !readonly && opensauce_real_settable(arguments[3]) && opensauce_script_may_set(networked))
			*field = opensauce_real_argument(arguments[3]);
	}
	else if (!strcmp(name, "weapon_data_get_real") || !strcmp(name, "weapon_data_set_real"))
	{
		struct weapon_datum *weapon = arguments[0] != NONE ?
			object_try_and_get_and_verify_type(arguments[0], _object_mask_weapon) : NULL;
		char const *data_name = opensauce_string_argument(arguments[1]);
		real *field = NULL;

		if (weapon)
		{
			if (!strcmp(data_name, "heat"))
				field = &weapon->weapon.heat;
			else if (!strcmp(data_name, "age"))
				field = &weapon->weapon.age;
			else if (!strcmp(data_name, "light_power"))
				field = &weapon->weapon.integrated_light_power;
		}
		if (name[12] == 'g')
			*value = opensauce_real_result(field ? *field : -1.f);
		else if (field && opensauce_real_settable(arguments[2]) && opensauce_script_may_set(FALSE))
			*field = opensauce_real_argument(arguments[2]);
	}
	else if (!strcmp(name, "weapon_data_trigger_set_real"))
	{
		struct weapon_datum *weapon = arguments[0] != NONE ?
			object_try_and_get_and_verify_type(arguments[0], _object_mask_weapon) : NULL;

		if (weapon && opensauce_script_may_set(FALSE))
		{
			struct weapon_definition *definition = weapon_definition_get(weapon->definition_index);
			char const *data_name = opensauce_string_argument(arguments[2]);
			char const *subname = opensauce_string_argument(arguments[3]);

			if (arguments[1] >= 0 && arguments[1] < definition->weapon.triggers.count)
			{
				struct weapon_trigger_definition *trigger = TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers,
					arguments[1], struct weapon_trigger_definition);
				real *field = NULL;

				if (!strcmp(data_name, "spew_time"))
					field = &trigger->spew_time;
				else if (!strcmp(data_name, "rounds_per_second") && !strcmp(subname, "lower"))
					field = &trigger->initial_rate_of_fire;
				else if (!strcmp(data_name, "rounds_per_second") && !strcmp(subname, "upper"))
					field = &trigger->final_rate_of_fire;
				if (field && opensauce_real_settable(arguments[4]))
					*field = opensauce_real_argument(arguments[4]);
			}
		}
	}
	else if (!strncmp(name, "unit_data_", 10))
	{
		struct unit_datum *unit = arguments[0] != NONE ?
			object_try_and_get_and_verify_type(arguments[0], _object_mask_unit) : NULL;
		char const *data_name = opensauce_string_argument(arguments[1]);

		if (!strcmp(name, "unit_data_get_object"))
		{
			*value = NONE;
			if (unit && !strncmp(data_name, "weapon", 6))
			{
				char last = data_name[0] ? data_name[strlen(data_name) - 1] : 0;
				short weapon_index = last >= '0' && last <= '9' ? (short)(last - '0') : 0;

				if (weapon_index < MAXIMUM_WEAPONS_PER_UNIT)
					*value = unit->unit.weapon_object_indices[weapon_index];
			}
			else if (unit && !strcmp(data_name, "equipment"))
				*value = unit->unit.equipment_object_index;
			else if (unit && !strcmp(data_name, "throwing_grenade_projectile"))
				*value = unit->unit.grenade_object_index;
		}
		else if (!strcmp(name, "unit_data_get_integer") || !strcmp(name, "unit_data_set_integer"))
		{
			long integer = name[10] == 's' ? arguments[2] : 0;

			if (!unit || !opensauce_unit_integer(function, unit, data_name, &integer, name[10] == 's'))
				integer = NONE;
			if (name[10] == 'g')
				*value = integer;
		}
		else
		{
			real *field = unit ? opensauce_unit_real(unit, data_name) : NULL;

			if (name[10] == 'g')
				*value = opensauce_real_result(field ? *field : -1.f);
			else if (field && opensauce_real_settable(arguments[2]) && opensauce_script_may_set(FALSE))
				*field = opensauce_real_argument(arguments[2]);
		}
	}
	else if (!strcmp(name, "player_data_get_object"))
	{
		struct player_datum *player = opensauce_player_at((short)arguments[0]);
		char const *data_name = opensauce_string_argument(arguments[1]);

		*value = NONE;
		if (player && player->unit_index != NONE && !strcmp(data_name, "slave_unit"))
			*value = player->unit_index;
	}
	else if (!strcmp(name, "player_data_set_real"))
	{
		struct player_datum *player = opensauce_player_at((short)arguments[0]);

		if (player && !strcmp(opensauce_string_argument(arguments[1]), "speed") &&
			opensauce_real_settable(arguments[2]) && opensauce_script_may_set(FALSE))
			player->speed_multiplier = opensauce_real_argument(arguments[2]);
	}
	else if (!strcmp(name, "abs_real"))
		*value = opensauce_real_result((real)fabs(opensauce_real_argument(arguments[0])));
	else if (!strcmp(name, "bit_test"))
		*value = (short)arguments[1] >= 0 && (short)arguments[1] < 32 ?
			(arguments[0] & (1L << (short)arguments[1])) != 0 : FALSE;
	else if (!strcmp(name, "bit_toggle"))
	{
		short bit = (short)arguments[1];

		*value = arguments[0];
		if (bit >= 0 && bit < 32)
			*value = (arguments[2] & 0xFF) ? (arguments[0] | (1L << bit)) : (arguments[0] & ~(1L << bit));
	}
#ifndef HALO_SERVER
	else if (!strcmp(name, "display_scripted_ui_widget"))
	{
		/* the widget the map lists under that name opened, as OpenSauce
		opens it: on its own, over whatever was open (TRUE if it was) */
		char const *widget_name = opensauce_string_argument(arguments[0]);
		long tag_index = opensauce_map_scripted_widget(widget_name);

		*value = FALSE;
		if (tag_index != NONE)
		{
			*value = ui_widget_load_by_name_or_tag(NULL, tag_index, NULL, NONE, NONE, NONE, NONE) != NULL;
			error(_error_silent, "OpenSauce scripts: scripted widget '%s' %s", widget_name,
				*value ? "shown" : "could not be made");
		}
		else
			error(_error_silent, "OpenSauce scripts: scripted widget '%s' isn't in the map's lists", widget_name);
	}
	else if (!strcmp(name, "play_bink_movie"))
		opensauce_play_bink_movie(function, opensauce_string_argument(arguments[0]));
#endif
#ifdef OPENSAUCE_BSP_SETS
	/* (what the map draws: each machine's that runs the map's scripts) */
	else if (!strcmp(name, "structure_bsp_set_lightmap_set"))
		*value = opensauce_bsp_sets_set_lightmap_set((short)arguments[0], opensauce_string_argument(arguments[1]));
	else if (!strcmp(name, "structure_bsp_set_sky_set"))
		*value = opensauce_bsp_sets_set_sky_set((short)arguments[0], opensauce_string_argument(arguments[1]));
#endif
#ifdef OPENSAUCE_TRANSFORMS
	/* (host-authoritative: a client has no transforms of its own,
	opensauce_transforms.c, and its scripts don't run) */
	else if (!strcmp(name, "ai_transform_actor"))
	{
		*value = opensauce_transforms_transform_actor(arguments[0], opensauce_string_argument(arguments[1]),
			opensauce_string_argument(arguments[2]));
	}
	else if (!strcmp(name, "ai_transform_actors"))
	{
		*value = opensauce_transforms_transform_actors(arguments[0], NONE, opensauce_string_argument(arguments[1]),
			opensauce_string_argument(arguments[2]));
	}
	else if (!strcmp(name, "ai_transform_actors_by_type"))
	{
		*value = arguments[1] != NONE && opensauce_transforms_transform_actors(arguments[0], arguments[1],
			opensauce_string_argument(arguments[2]), opensauce_string_argument(arguments[3]));
	}
	else if (!strcmp(name, "ai_actor_is_transforming"))
		*value = opensauce_transforms_actor_is_transforming(arguments[0]);
#endif
	/* (Halo PC's: the music's and the other sounds' gain for this map, each
	machine's own: opensauce_map.c) */
	else if (!strcmp(name, "sound_set_music_gain") || !strcmp(name, "sound_set_effects_gain"))
		opensauce_map_set_sound_gain(name[10] == 'm', opensauce_real_argument(arguments[0]));
	else if (!strcmp(name, "physics_set_gravity"))
	{
		real scale = opensauce_real_argument(arguments[0]);

		/* (a scale the physics can't take, NaN, negative or huge, refused
		and said: OpenSauce takes any) */
		if (!opensauce_real_settable(arguments[0]) || scale < 0.f || scale > OPENSAUCE_MAXIMUM_GRAVITY_SCALE)
			opensauce_script_log_once(function, "was given a gravity scale it can't take (not 0 to 10): ignored");
		else if (opensauce_script_may_set(FALSE))
			opensauce_map_set_gravity_scale(scale);
	}
	else
		return FALSE;
	return TRUE;
}

/* the parts the game does not have yet: what they return with nothing to
show, said once each */
static void opensauce_script_missing(
	struct opensauce_script_function *function,
	long *arguments,
	long *value)
{
	char const *name = function->definition.name;

#ifdef OPENSAUCE_POSTPROCESS
	/* (the map's post-processing, when it has some: opensauce_postprocess.c,
	which draws it; presentation only, each machine's own) */
	if (!strncmp(name, "pp_", 3) && opensauce_postprocess_active())
	{
		short index = (short)arguments[0];

		if (!strcmp(name, "pp_set_effect_instance_active"))
			opensauce_postprocess_set_instance_active(index, (arguments[1] & 0xFF) != 0);
		else if (!strcmp(name, "pp_set_effect_instance_fade"))
		{
			opensauce_postprocess_set_instance_fade(index, opensauce_real_argument(arguments[1]),
				opensauce_real_argument(arguments[2]), opensauce_real_argument(arguments[3]));
		}
		else if (!strcmp(name, "pp_get_effect_instance_current_fade"))
			*value = opensauce_real_result(opensauce_postprocess_instance_current_fade(index));
		else if (!strcmp(name, "pp_set_effect_shader_variable_integer"))
		{
			opensauce_postprocess_set_variable_integer(index, (short)arguments[1], arguments[2],
				opensauce_real_argument(arguments[3]));
		}
		else if (!strcmp(name, "pp_set_effect_shader_variable_real"))
		{
			float values[4];
			short component;

			for (component = 0; component < 4; component++)
				values[component] = opensauce_real_argument(arguments[2 + component]);
			opensauce_postprocess_set_variable_real(index, (short)arguments[1], values,
				opensauce_real_argument(arguments[6]));
		}
		else if (!strcmp(name, "pp_bloom_set_maximum_color"))
		{
			float color[3];

			color[0] = opensauce_real_argument(arguments[0]);
			color[1] = opensauce_real_argument(arguments[1]);
			color[2] = opensauce_real_argument(arguments[2]);
			opensauce_postprocess_bloom_set_maximum_color(color, opensauce_real_argument(arguments[3]));
		}
		else if (!strcmp(name, "pp_bloom_set_exposure"))
		{
			opensauce_postprocess_bloom_set_exposure(opensauce_real_argument(arguments[0]),
				opensauce_real_argument(arguments[1]));
		}
		return;
	}
#endif
	if (!strncmp(name, "pp_", 3))
	{
		short instance = (short)arguments[0];
		boolean valid = instance >= 0 && instance < MAXIMUM_OPENSAUCE_EFFECT_INSTANCES;

		if (!strcmp(name, "pp_set_effect_instance_active") && valid)
			opensauce_script_effect_instances[instance].active = (arguments[1] & 0xFF) != 0;
		else if (!strcmp(name, "pp_set_effect_instance_fade") && valid)
			opensauce_script_effect_instances[instance].fade = opensauce_real_argument(arguments[2]);
		else if (!strcmp(name, "pp_get_effect_instance_current_fade"))
			*value = opensauce_real_result(valid ? opensauce_script_effect_instances[instance].fade : 0.f);
		opensauce_script_log_once(function, "is kept, not drawn: this build or map draws no post-processing (fades end at once)");
		return;
	}
	opensauce_script_log_once(function, "does nothing yet: this game has no part for it (it returns as though "
		"it had nothing to do)");
}

static void opensauce_script_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct opensauce_script_function *function;
	long index = function_index - hs_function_table_count - stubbs_function_count();
	long *arguments;
	long value = 0;

	if (index < 0 || index >= opensauce_script_function_count())
	{
		hs_return(thread_index, 0);
		return;
	}
	function = &opensauce_script_functions[index];
	arguments = hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (!arguments)
		return;
	if (function->definition.return_type == _hs_type_object)
		value = NONE;
	if (!opensauce_script_data(function, arguments, &value))
		opensauce_script_missing(function, arguments, &value);
	hs_return(thread_index, value);
}

static void opensauce_script_functions_prepare(
	void)
{
	size_t index;

	if (opensauce_script_functions_ready)
		return;
	opensauce_script_functions_ready = TRUE;
	for (index = 0; index < NUMBEROF(opensauce_script_functions); index++)
	{
		struct opensauce_script_function *function = &opensauce_script_functions[index];
		short parameter;

		function->definition.parameter_types[0] = function->types[0];
		for (parameter = 1; parameter < function->definition.parameter_count; parameter++)
			function->more_parameter_types[parameter - 1] = function->types[parameter];
	}
}

/* ---------- public code */

long opensauce_script_function_count(
	void)
{
	return opensauce_map_active() ? (long)NUMBEROF(opensauce_script_functions) : 0;
}

struct hs_function_definition *opensauce_script_function_get(
	long index)
{
	if (index < 0 || index >= opensauce_script_function_count())
		return NULL;
	opensauce_script_functions_prepare();
	return &opensauce_script_functions[index].definition;
}

long opensauce_script_function_find(
	char const *name)
{
	long index;

	for (index = 0; name && index < opensauce_script_function_count(); index++)
	{
		if (!_stricmp(opensauce_script_functions[index].definition.name, name))
			return index;
	}
	return NONE;
}

short opensauce_script_global_count(
	void)
{
	return opensauce_map_active() ? (short)NUMBEROF(opensauce_script_globals) : 0;
}

short opensauce_script_global_find(
	char const *name)
{
	short index;

	for (index = 0; name && index < opensauce_script_global_count(); index++)
	{
		if (!_stricmp(opensauce_script_globals[index].name, name))
			return index;
	}
	return NONE;
}

short opensauce_script_global_type(
	short index)
{
	return index >= 0 && index < opensauce_script_global_count() ? opensauce_script_globals[index].type :
		_hs_unparsed;
}

char const *opensauce_script_global_name(
	short index)
{
	return index >= 0 && index < opensauce_script_global_count() ? opensauce_script_globals[index].name : "";
}

long opensauce_script_global_initial(
	short index)
{
	return index >= 0 && index < opensauce_script_global_count() ? opensauce_script_globals[index].initial : 0;
}

void opensauce_scripts_map_unloaded(
	void)
{
	size_t index;

	memset(opensauce_script_effect_instances, 0, sizeof(opensauce_script_effect_instances));
	for (index = 0; index < NUMBEROF(opensauce_script_functions); index++)
		opensauce_script_functions[index].logged = FALSE;
}

#endif
