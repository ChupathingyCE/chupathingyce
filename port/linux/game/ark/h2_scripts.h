/*
H2_SCRIPTS.H

Halo 2's scenario scripts run by this engine's script interpreter (hs_*.c)
on a Halo 2 level (h2_level.c, h2_rules_level()).

Two halves:
- the level builder (h2_level.c) copies the player's map's compiled
  scripts into the level's scenario as this engine's: its syntax nodes
  (their value types renumbered, Halo 2's 62 types onto this engine's 49),
  its script strings, scripts and globals, and the blocks the scripts name
  things in by index - trigger volumes, cutscene flags, cutscene camera
  points and object names - in Halo 2's order, so every index the
  compiled scripts hold names the same thing here;
- at map load (hs.c's hs_scenario_postprocess, for a Halo 2 level, in
  place of hs_compile_postprocess) each call is given a function by its
  name (the string data keeps every name): this engine's own where it is
  Halo 2's too and takes the same arguments, a Halo 2 one of this file's
  (h2_scripts_function_get, indices from H2_SCRIPTS_FUNCTION_BASE), or a
  logged stand-in that evaluates its arguments and gives its type's
  default. Engine globals Halo 2 has and this engine has not are made
  constants.

What runs, what is a stand-in and what is counted when a level plays:
a local status file (private); HALO_H2_SCRIPT_STATUS=
path writes the counts there when the map is left.
*/

#ifndef __H2_SCRIPTS_H
#define __H2_SCRIPTS_H

#include "cseries.h"

/* ---------- constants */

enum
{
	/* a call's function index from here on is one of h2_scripts.c's */
	H2_SCRIPTS_FUNCTION_BASE = 0x4000,
	/* the most syntax nodes a Halo 2 level's scripts may have (a data
	array's count is a short): 00a 9025, 01a 7221, 03a 22185; 04b's 37509
	and 05a's 29942+ are over or near it */
	H2_SCRIPTS_MAXIMUM_NODES = 0x7FFF,
	/* (the nodes kept free for the console's commands) */
	H2_SCRIPTS_CONSOLE_ROOM = 1024,
};

/* ---------- structures */

/* a function of Halo 2's played here (h2_scripts.c's own, h2_ai.c's): its
name, return type, evaluate and parameters (NONE: any, evaluated in turn) */
struct h2_scripts_native
{
	char const *name;
	short return_type;
	void (*evaluate)(short function_index, long thread_index, boolean initialize);
	short parameter_count;
	short parameter_types[8];
};

/* the scripts' weather (weather_start, weather_stop,
weather_change_intensity): how much falls, on times its intensity, now and
where each is going, in how many ticks - the host's, for its clients */
struct h2_scripts_weather
{
	real on;
	real on_target;
	real intensity;
	real intensity_target;
	short on_ticks;
	short intensity_ticks;
};

/* ---------- prototypes/H2_SCRIPTS.C */

struct h2_cache;
struct h2_objanim_host;
struct scenario;
struct hs_function_definition;

/* (the level builder) the scenario's scripts and the blocks they name
things in, written into the scenario being made at scenario_root (host:
h2_objanim.h's, the builder's tag region). An empty script node array when
not wanted (a multiplayer level), the map has none or they cannot be
copied (FALSE, why) */
/* whether a level's scripts name a script function (its string table) */
boolean h2_scripts_call(struct h2_cache *cache, char const *function_name);

boolean h2_scripts_write(
	struct h2_cache *cache,
	struct h2_objanim_host const *host,
	unsigned long scenario_root,
	boolean scripts_wanted,
	char *why,
	long why_size);

/* (the level builder, h2_effects.c) the effect tags the map's scripts
name, each once (made into this engine's, "h2\\" and their names, for
effect_new and effect_new_on_object_marker); how many */
long h2_scripts_effect_tags(
	struct h2_cache *cache,
	long *tags,
	long maximum);

/* (the level builder) the tags the map's scripts name as constants of a
Halo 2 value type (33 effect, 34 damage: h2_vehicles.c makes the damage
effects, "h2\\" and their names, for damage_new and damage_players), each
once; how many */
long h2_scripts_constant_tags(
	struct h2_cache *cache,
	short h2_type,
	long *tags,
	long maximum);

/* whether the loaded scenario's scripts are a Halo 2 level's */
boolean h2_scripts_level(void);

/* (hs.c, hs_scenario_postprocess) the loaded scenario's Halo 2 scripts made
ready to run: FALSE if they cannot (none run) */
boolean h2_scripts_postprocess(
	struct scenario *scenario);

/* (hs.c, hs_function_get; hs_runtime.c) one of this file's functions, by a
call's index (H2_SCRIPTS_FUNCTION_BASE and on), and whether it is one */
struct hs_function_definition *h2_scripts_function_get(
	short function_index);
boolean h2_scripts_function_index_valid(
	short function_index);

/* (hs_runtime.c) each tick: the counts logged now and then; and when the
map is left */
void h2_scripts_update(void);
/* (object_set_function_variable) an object's script variable's value now
(FALSE: the scripts set none so named); an object definition's incoming
function slot (A..D) given a variable's name; the slots' values filled
(objects.c, object_export_function_values) */
boolean h2_scripts_object_function_value(long object_index, char const *name, real *value);
void h2_scripts_object_function_slot_set(long definition_index, short slot, char const *name);
void h2_scripts_object_function_values(long object_index, long definition_index, real *incoming);
void h2_script_text_objectives_clear(void);

/* (weather_particle_systems.c) how much of the level's weather falls (1:
all of it, as a level whose scripts never touch it); (network_coop.c) the
host's state, TRUE when its scripts have set it (changes: counted, to send
it as it changes), and a client's made the host's */
real h2_scripts_weather_scale(void);
boolean h2_scripts_weather_get(struct h2_scripts_weather *weather, long *changes);
void h2_scripts_weather_set(struct h2_scripts_weather const *weather);

/* (render_objects.c) whether object_hide hid the object or what carries it */
boolean h2_scripts_object_hidden(long object_index);
void h2_scripts_object_hide(long object_index, boolean hide);
/* whether an object is lit by Halo 2's cinematic lighting (it or what it is
attached to: object_uses_cinematic_lighting), and its lights: the ambient,
the primary's and secondary's travel directions and colours */
boolean h2_scripts_cinematic_lighting(long object_index, float ambient[3], float primary_direction[3], float primary[3],
	float secondary_direction[3], float secondary[3]);
void h2_scripts_dispose_from_old_map(void);

/* the level's own map, open while it is played (NULL: none) */
struct h2_cache *h2_scripts_cache(void);

/* ---------- prototypes/H2_SCRIPT_TEXT.C: the scripts' text */

void h2_script_text_load(struct h2_cache *cache);
void h2_script_text_unload(void);
boolean h2_script_text_training(char const *name);
void h2_script_text_training_show(boolean shown);
boolean h2_script_text_objective(long n, boolean finished);
/* (the pause) the objectives shown so far, a line each, ASCII */
long h2_script_text_objectives_list(char *out, long out_size);
boolean h2_script_text_subtitle(char const *name, real seconds);
boolean h2_script_text_title(short index);
/* (hud.c) drawn on the HUD */
void h2_script_text_draw(short local_player_index);

/* ---------- prototypes/H2_CINEMATICS.C: the cinematics' animations */

void h2_cinematics_dispose(void);
/* (a checkpoint reverted to) */
void h2_cinematics_reset(void);
void h2_scripts_game_state_reverted(void);
long h2_cinematics_camera_animation(long graph_handle, char const *name, long unit_index, short flag_index);
void h2_cinematics_camera_stop(void);
void h2_cinematics_field_of_view(real degrees, short ticks);
/* texture_camera_set_object_marker (object, marker, field of view) and
texture_camera_off: the texture camera the texture camera screens show
(render.c draws its view, h2_level.c's texture_camera_shader_fill) */
void h2_texture_camera_set(long object_index, char const *marker, real field_of_view_degrees);
void h2_texture_camera_off(void);
/* (render.c) the texture camera's view now: FALSE if off (or its object or marker gone) */
boolean h2_texture_camera_view(union real_point3d *position, union real_vector3d *forward, union real_vector3d *up,
	real *field_of_view_degrees);
void h2_cinematics_depth_of_field_set(real separation, real near_lower, real near_upper, real near_time, real far_lower, real far_upper,
	real far_time);
void h2_cinematics_depth_of_field_clear(void);
void h2_cinematics_crossfade_start(real seconds, real exponent);
long h2_cinematics_camera_time(void);
/* (anchor_flag: a relative animation whose anchor object is not there -
the camera animation's anchor flag stands in) */
#define H2_CINEMATICS_CAMERA_ANCHOR (-2)
long h2_cinematics_object_animation(long object_index, long graph_handle, char const *name, long anchor_object, short anchor_flag,
	boolean loop);
long h2_cinematics_object_time(long object_index);
void h2_cinematics_object_stop(long object_index);
/* (device_set_position_track) a device's track by name played at its position, its last track's motion kept */
boolean h2_cinematics_device_track(long device_index, char const *name, real interpolation_seconds);
/* (device_set_overlay_track, device_animate_overlay) a device's overlay by name over its track, and its position */
boolean h2_cinematics_device_overlay(long device_index, char const *name);
void h2_cinematics_device_overlay_animate(long device_index, real position, real seconds);
/* (sound_impulse_start on a character) its line's lip-sync on its face */
boolean h2_cinematics_face_start(long object_index, char const *sound_name);
/* (unit_set_emotional_state) the unit's emotion on its face */
boolean h2_cinematics_emotion(long object_index, char const *name, real weight, long ticks);
/* (h2_scripts_update) each tick */
void h2_cinematics_update(void);
void h2_cinematics_counts(long *decoded, long *failed, long *objects_missing);

/* ---------- engine entry points the scripts call (kept small: other
branches fill them) */

/* switch_bsp and switch_bsp_by_name: the Halo 2 BSP index asked for. A
level that can switch its BSPs (h2_level.c's BSP switching, not yet on
this branch) sets the switcher, which gives TRUE when it switched; until
then the request is logged and remembered, and structure_bsp_index gives
the last one asked for (scripts that wait on it go on) */
void h2_scripts_set_bsp_switcher(
	boolean (*switcher)(short h2_bsp_index),
	short (*current)(void));


/* (perf/h2-tick-gap, HALO_TICK_PROFILE) hs_update's parts, timed:
hs_runtime.c marks the script threads' walk and the lists' collection,
h2_scripts_update the rest and reports ("hs-split") */
enum
{
	_hs_split_threads,
	_hs_split_list_gc,
	_hs_split_h2_ai,
	_hs_split_dialogue,
	_hs_split_kill_volumes,
	_hs_split_devices,
	_hs_split_out_of_world,
	_hs_split_triggers,
	_hs_split_skulls,
	_hs_split_cinematics,
	_hs_split_variables,
	_hs_split_rest,
	NUMBER_OF_HS_SPLITS
};
void h2_scripts_split_mark(int step);

#endif
