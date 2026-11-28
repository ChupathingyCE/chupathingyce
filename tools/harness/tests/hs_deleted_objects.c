/*
HS_DELETED_OBJECTS.C

Script commands handed an object the scripts kept after it was deleted
(#1861). The commands' real code (test_hs_deleted_objects.py takes it from the
sources) runs over a fake world whose object_get halts, as the game's does, on
an index that is unused or changed. A command given a deleted object, or one
whose slot a new object took since, must take it as none: no halt, and none of
the work it does for a live object. Given a live one it must go on to that
work, as before.

Everything a command calls once past its check is a stub that notes it was
reached; the few calls the commands make before it (debug prints, network
co-op's notes, which only compare the index) are not counted.
*/

#include "harness.h"
#include <math.h>
#include <stdarg.h>

typedef unsigned short word;
typedef unsigned long dword;
typedef char tag_string[32];
#include "config.inc"

#define UNSIGNED_SHORT_MAX 0xFFFF
#define UNSIGNED_CHAR_MAX 0xFF
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(value, low, high) ((value) < (low) ? (low) : (value) > (high) ? (high) : (value))
#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#define BIT_VECTOR_SET_FLAG(vector, bit, value) ((void)(vector), (void)(bit), (void)(value))
#define DEGREES_TO_RADIANS(degrees) ((degrees) * 0.017453292f)
#define TICKS_PER_SECOND 30
#define MAXIMUM_TRIGGER_VOLUMES_PER_SCENARIO 256
#define MAXIMUM_WEAPONS_PER_UNIT 4
#define NUMBER_OF_UNIT_GRENADE_TYPES 2
#define RECORDED_ANIMATION_VERSION 4
#define _error_silent 0
#define _stricmp strcasecmp
#define csstrlen strlen
#define csstrcpy strcpy
#define match_assert_valid_real_point3d(file, line, point) ((void)0)
#define match_assert_valid_real_normal3d(file, line, vector) ((void)0)
int strcasecmp(char const *a, char const *b);

typedef struct { real yaw, pitch; } real_euler_angles2d;
typedef struct { real_vector3d forward, left, up; real_point3d position; real scale; } real_matrix4x3;
struct tag_block { long count; void *address; long definition; };
struct tag_reference { long group_tag; char const *name; long name_length; long index; };
struct tag_data { long size; long flags; long file_offset; void *address; long definition; };

enum { _object_dead_bit, _object_cannot_take_damage_bit, _object_ranged_attack_inhibited_bit,
	_object_melee_attack_inhibited_bit, _object_die_act_of_god_bit, _object_die_act_of_god_silent_bit };
enum { _object_movie_star_bit, _object_no_collisions_bit };
enum { _unit_not_enterable_by_player_bit, _unit_aim_without_turning_bit, _unit_cannot_blink_bit,
	_unit_desired_integrated_light_on_bit, _unit_desired_integrated_light_off_bit, _unit_suspended_bit,
	_unit_integrated_light_on_bit };
enum { _unit_animation_postpone_weapon_ik_until_interpolation_ends_bit };
enum { _unit_state_idle, _unit_state_user_animation = 5, _unit_state_opening = 8, _unit_state_closing };
enum { _animation_base, _animation_overlay };
enum { _biped_airborne_bit };
enum { _device_position_changed_bit };
enum { _recording_thread_finished_bit, _recording_thread_killed_bit, _recording_thread_unit_was_controlled_bit,
	_recording_thread_delete_unit_on_complete_bit, _recording_thread_hover_vehicle_on_complete_bit };
enum { _ai_reference_type_encounter, _ai_reference_type_squad, _ai_reference_type_platoon };
enum { _direction_specification_object = 3 };
enum { _secondary_look_scripted = 1 };
enum { _secondary_look_priority_default = 1 };
enum { _ai_unit_effect_magic_sight = 2 };
enum { _looping_sound_stops_music_bit };
enum { _game_looping_sound_scripted_bit };
enum { _coop_sound_impulse, _coop_sound_looping_start };
enum { _item_belongs_to_player_bit };
enum { _encounter_braindead_bit };
enum { _actor_definition_swarm_actor_bit };

/* ---------- the objects */

struct object_fields
{
	short type;
	unsigned long flags, damage_flags;
	long parent_object_index, next_object_index, first_child_object_index;
	short parent_node_index;
	real_point3d position, bounding_sphere_center;
	real_vector3d forward, up, translational_velocity;
	real scale, body_vitality, shield_vitality, maximum_body_vitality, maximum_shield_vitality;
	struct { long animation_graph_index; struct { short index, frame_index; } state; } animation;
};
struct unit_fields
{
	unsigned long flags;
	long player_index, actor_index, swarm_actor_index, driver_object_index, gunner_object_index;
	short parent_seat_index, magic_seat_index, current_weapon_index, override_emotion_animation_index;
	short fake_encounter_index, fake_squad_index;
	long weapon_object_indices[MAXIMUM_WEAPONS_PER_UNIT];
	byte grenade_counts[NUMBER_OF_UNIT_GRENADE_TYPES];
	real_vector3d desired_facing_vector, desired_aiming_vector, desired_looking_vector;
	struct { short state, emotion_index; unsigned long flags; } animation;
};
struct object_datum { long definition_index; struct object_fields object; };
struct unit_datum { long definition_index; struct object_fields object; struct unit_fields unit; };
struct vehicle_datum
{
	long definition_index; struct object_fields object; struct unit_fields unit;
	struct { unsigned long flags; real_point3d hover_position; } vehicle;
};
struct biped_datum { long definition_index; struct object_fields object; struct unit_fields unit;
	struct { unsigned long flags; } biped; };
struct device_datum
{
	long definition_index; struct object_fields object;
	struct { unsigned long flags; real position, power; short position_group_index, power_group_index; } device;
};
struct scenery_datum { struct object_datum object; unsigned long flags; };
struct item_datum { long definition_index; struct object_fields object; struct { unsigned long flags; } item; };

static struct data_array *object_header_data;
static union { struct vehicle_datum vehicle; struct biped_datum biped; struct device_datum device;
	struct scenery_datum scenery; char bytes[512]; } objects[8];

/* what a command did: halting gets of live objects, stubs reached past its check */
static int get_count, reached_count;
static char const *reached_name;
static long sound_source = 12345;

static void reached(char const *name)
{
	reached_count++;
	if (!reached_name)
		reached_name = name;
}

static void *datum_try_and_get(struct data_array *data, long index);

/* the game's object_get: halts on an index that is unused or changed */
static void *object_get_and_verify_type(long object_index, unsigned long valid_type_flags)
{
	struct object_header_datum *header =
		object_index == NONE ? NULL : (struct object_header_datum *)datum_try_and_get(object_header_data, object_index);

	CHECK(header, "halt: object index 0x%lx is unused or changed", (unsigned long)object_index);
	CHECK(TEST_FLAG(valid_type_flags, header->type), "halt: object 0x%lx of type %d", (unsigned long)object_index,
		header->type);
	get_count++;
	return header->datum;
}
#define object_header_try_and_get(index) ((struct object_header_datum *)datum_try_and_get(object_header_data, (index)))
static void *object_try_and_get_and_verify_type(long object_index, unsigned long valid_type_flags);
#define object_get(index) ((struct object_datum *)object_get_and_verify_type((index), _object_mask_all))
#define unit_get(index) ((struct unit_datum *)object_get_and_verify_type((index), _object_mask_unit))
#define unit_try_and_get(index) ((struct unit_datum *)object_try_and_get_and_verify_type((index), _object_mask_unit))
#define biped_get(index) ((struct biped_datum *)object_get_and_verify_type((index), _object_mask_biped))
#define vehicle_datum_get(index) ((struct vehicle_datum *)object_get_and_verify_type((index), _object_mask_vehicle))
#define device_get(index) ((struct device_datum *)object_get_and_verify_type((index), _object_mask_device))
#define scenery_get(index) ((struct scenery_datum *)object_get_and_verify_type((index), _object_mask_scenery))
#define weapon_get(index) ((struct object_datum *)object_get_and_verify_type((index), _object_mask_weapon))
static struct item_datum *hs_item_datum_from_object(struct object_datum *object) { return (struct item_datum *)object; }

/* object lists: the one list the command is given, holding the object */
static long listed_object = NONE;
static long object_list_get_first(long object_list_index, long *reference_index)
{
	*reference_index = 1;
	return object_list_index == NONE ? NONE : listed_object;
}
static long object_list_get_next(long object_list_index, long *reference_index)
{
	return NONE;
}

/* ---------- what the commands call: zeroes past their check */

static char zeroes[4096];
#define TAG_BLOCK_GET_ELEMENT(block, index, type) ((type *)tag_block_element((block), (index), sizeof(type)))
static void *tag_block_element(struct tag_block const *block, long index, long size)
{
	return block->address && VALID_INDEX(index, block->count) ? (char *)block->address + index * size : zeroes;
}
#define STUB_POINTER(type, name) (reached(#name), (type *)zeroes)

struct animation { short type, frame_count, runtime_parent_animation_index, private_key_frame_index; };
struct animation_graph { struct tag_block animations; };
struct unit_seat { tag_string label; };
struct unit_definition { struct { struct tag_block seats; struct { long index; } spawned_actor_variant; } unit; };
struct model_region { tag_string name; };
struct model { struct tag_block regions; };
struct object_definition { struct { struct { long index; } model; } object; };
struct object_marker { short node_index; real_matrix4x3 matrix, node_matrix; };
struct object_iterator { long index; };
struct scenario_cutscene_flag { real_point3d position; real_euler_angles2d facing; };
struct starting_weapon { struct tag_reference weapon; };
struct scenario_starting_profile
{
	struct starting_weapon primary_weapon, secondary_weapon;
	real starting_health_modifier, starting_shield_modifier;
	byte grenade_counts[NUMBER_OF_UNIT_GRENADE_TYPES];
};
struct squad_definition { tag_string name; short actor_palette_index, platoon_index, initial_state, default_state; };
struct encounter_definition { tag_string name; unsigned long flags; struct tag_block squads; };
struct scenario
{
	struct tag_block recorded_animations, starting_profiles, ai_encounters, ai_actor_palette, cutscene_flags,
		object_names;
};
struct recorded_animation_definition
{
	tag_string name; short version, unit_control_data_version, length_in_ticks; struct tag_data event_stream;
};
struct animation_controller { long unused; };
struct animation_thread
{
	long unit_index; short animation_index, version; long relative_ticks, ticks_left; unsigned long flags;
	void const *event_stream; long animation_state[4]; struct animation_controller controller;
};
struct animation_thread_debug { boolean active; void const *event_stream_start; long stream_length; short animation_index; };
struct animation_playback
{
	void (*initialize_event_stream)(long *state, struct animation_controller *controller, void const **stream,
		short version, byte const *end);
};
struct actor_datum
{
	struct { long encounter_index; short squad_index; boolean encounterless, force_active; } meta;
	struct { long vehicle_index; } input;
};
struct actor_variant_definition { struct { long index; } actor_reference; };
struct actor_definition { unsigned long flags; };
struct direction_specification { short type; long object_index; };
struct encounter_actor_iterator { long index; };
struct ai_script_actor_reference_iterator { long actor_index; };
struct sound_definition { long scripting_sound_index, scripting_time, longest_permutation_length; };
struct looping_sound_definition { unsigned long flags; long runtime_scripting_sound_index; };
struct game_looping_sound_datum { unsigned long flags; };
struct player_datum { short local_player_index; long unit_index; };
struct damage_data { real_point3d origin, epicenter; long location; };
struct data_iterator { long index; };

static struct { boolean print_scripting; } ai_debug;
static struct { boolean ai_initialized_for_map; } ai_globals_storage = { TRUE }, *ai_globals = &ai_globals_storage;
static struct data_array *animation_threads;
static struct animation_playback *playback_codec[RECORDED_ANIMATION_VERSION];
static unsigned long hs_debug_data[8];
static real_vector3d const zero_vector3d;
static real_vector3d const *global_zero_vector3d = &zero_vector3d;

/* (the calls before a command's check: not counted) */
static void error(int priority, char const *format, ...) { }
static void console_warning(char const *format, ...) { }
static char const *hs_runtime_get_executing_thread_name(void) { return "test"; }
static void ai_index_to_string(long ai_reference, struct scenario *scenario, char *name, long size) { name[0] = 0; }
static void coop_scripts_suspend_followers(long unit_index, boolean suspended) { }
static boolean network_coop_set_players_vitality(long unit_index, boolean maximum, real body, real shield) { return FALSE; }
static boolean coop_scripts_any_player_will_do(long object_list_index) { return FALSE; }
static void coop_scripts_teleport_followers(long unit_index, real_point3d const *destination) { }
static void network_coop_note_unit_open(long unit_index, boolean open) { }
static void network_coop_note_object_effect(long effect, long object_index, char const *marker) { }
static long player_index_from_unit_index(long unit_index) { return NONE; }
static boolean stubbs_mode_active(void) { return TRUE; }
/* the map: a recording, a starting profile, an encounter with a squad that
names no actor variant (Stubbs mode gives one) */
static struct recorded_animation_definition recording = { "recording", 1 };
static struct scenario_starting_profile starting_profile = { .secondary_weapon.weapon.index = NONE };
static struct squad_definition squad = { "squad", 0 };
static struct tag_reference palette = { 0, NULL, 0, NONE };
static struct encounter_definition encounter = { "encounter", 0, { 1, &squad } };
static struct scenario_cutscene_flag cutscene_flag;
static struct scenario scenario =
{
	{ 1, &recording }, { 1, &starting_profile }, { 1, &encounter }, { 1, &palette }, { 1, &cutscene_flag }
};
static struct scenario *global_scenario_get(void) { return &scenario; }
static char const *tag_get_name(long tag_index) { return "tag"; }
static char const *tag_name_strip_path(char const *name) { return name; }
static long game_time_get(void) { return 0; }
static struct data_iterator *data_iterator_new(struct data_iterator *iterator, struct data_array *data) { return iterator; }
static void *data_iterator_next(struct data_iterator *iterator) { return NULL; }
static struct data_array *player_data;
static void network_coop_note_sound(short kind, long definition_index, long object_index, real scale)
{
	sound_source = object_index;
}

/* (past it: reached) */
static void object_start_interpolation(long object_index, short frames) { reached("object_start_interpolation"); }
static void network_coop_note_attach(long a, char const *b, long c, char const *d) { reached("network_coop_note_attach"); }
static void object_attach_to_marker(long a, char const *b, long c, char const *d) { reached("object_attach_to_marker"); }
static void network_coop_note_detach(long parent, long child) { reached("network_coop_note_detach"); }
static void object_detach(long object_index) { reached("object_detach"); }
static struct animation_graph *animation_graph_definition_get(long index) { return STUB_POINTER(struct animation_graph, animation_graph_definition_get); }
static short animation_graph_get_animation_by_name(long graph, char const *name) { reached("animation_graph_get_animation_by_name"); return NONE; }
static void network_coop_note_scenery_animation(long a, long b, short c, short d) { reached("network_coop_note_scenery_animation"); }
static boolean scenario_trigger_volume_test_point(short volume, real_point3d const *point) { reached("scenario_trigger_volume_test_point"); return FALSE; }
static boolean device_group_set_desired_value(short group, real value) { reached("device_group_set_desired_value"); return FALSE; }
static void device_group_set_actual_value(short group, real value) { reached("device_group_set_actual_value"); }
static struct animation_thread *get_controlling_thread(long unit_index, long *thread_index) { reached("get_controlling_thread"); return NULL; }
static boolean recorded_animation_playable(struct recorded_animation_definition const *animation) { reached("recorded_animation_playable"); return FALSE; }
static boolean recorded_animation_controlling_unit(long unit_index) { reached("recorded_animation_controlling_unit"); return FALSE; }
static void const *tag_data_get_pointer(struct tag_data const *data, long offset, long size) { reached("tag_data_get_pointer"); return NULL; }
static struct animation_thread_debug *animation_threads_debug_get(long index) { return STUB_POINTER(struct animation_thread_debug, animation_threads_debug_get); }
static void unit_set_actively_controlled(long unit_index, boolean value) { reached("unit_set_actively_controlled"); }
static void mcc_recording_control_begin(long unit_index, struct recorded_animation_definition const *animation, struct animation_controller *controller) { reached("mcc_recording_control_begin"); }
static boolean unit_controllable(long unit_index) { reached("unit_controllable"); return FALSE; }
static void unit_set_controllable(long unit_index, boolean value) { reached("unit_set_controllable"); }
static void unit_set_possessed(long unit_index, boolean value) { reached("unit_set_possessed"); }
static void object_set_automatic_deactivation(long object_index, boolean value) { reached("object_set_automatic_deactivation"); }
static void object_get_origin(long object_index, real_point3d *origin) { reached("object_get_origin"); }
static void unit_delete_all_weapons(long unit_index) { reached("unit_delete_all_weapons"); }
static long create_weapon(struct starting_weapon *weapon, long unit_index) { reached("create_weapon"); return NONE; }
static boolean unit_add_weapon_to_inventory(long unit_index, long weapon_index, short mode) { reached("unit_add_weapon_to_inventory"); return TRUE; }
static void object_delete(long object_index) { reached("object_delete"); }
static void mcc_player_profile_grenades(long unit_index, struct scenario_starting_profile *profile, boolean reset) { reached("mcc_player_profile_grenades"); }
static long object_list_new(void) { reached("object_list_new"); return NONE; }
static void object_list_add(long list, long object_index) { reached("object_list_add"); }
static struct unit_definition *unit_definition_get(long index) { return STUB_POINTER(struct unit_definition, unit_definition_get); }
static void object_iterator_new(struct object_iterator *iterator, unsigned long mask, byte flags) { reached("object_iterator_new"); }
static void *object_iterator_next(struct object_iterator *iterator) { return NULL; }
static short seat_label_to_base_seat_index(char const *label) { reached("seat_label_to_base_seat_index"); return NONE; }
static short animation_choose_random_permutation_internal(boolean a, long graph, short animation) { reached("animation_choose_random_permutation_internal"); return 0; }
static void unit_set_animation(long unit_index, long graph, short animation) { reached("unit_set_animation"); }
static void object_compute_node_matrices_recursive(long object_index) { reached("object_compute_node_matrices_recursive"); }
static void network_coop_note_unit_animation(long a, long b, short c, boolean d) { reached("network_coop_note_unit_animation"); }
static void network_actors_note_user_animation(long a, long b, short c, boolean d) { reached("network_actors_note_user_animation"); }
static void network_coop_note_unit_animation_frame(long a, short b) { reached("network_coop_note_unit_animation_frame"); }
static boolean unit_animation_set_state(long unit_index, short state) { reached("unit_animation_set_state"); return TRUE; }
static void unit_exit_seat_end(long unit_index) { reached("unit_exit_seat_end"); }
static boolean unit_seat_filled(long vehicle_index, short seat_index) { reached("unit_seat_filled"); return TRUE; }
static boolean unit_set_or_test_seat_and_weapon_label(long a, char const *b, void *c, boolean d) { reached("unit_set_or_test_seat_and_weapon_label"); return FALSE; }
static boolean unit_enter_seat(long unit_index, long vehicle_index, short seat_index) { reached("unit_enter_seat"); return FALSE; }
static void coop_scripts_board_followers(long unit_index, long vehicle_index, char const *seat) { reached("coop_scripts_board_followers"); }
static char *strlwr(char *string) { return string; }
static boolean unit_try_and_exit_seat(long unit_index) { reached("unit_try_and_exit_seat"); return FALSE; }
static void coop_scripts_exit_followers(long unit_index, long vehicle_index) { reached("coop_scripts_exit_followers"); }
static short vehicle_scripting_find_available_seats(long vehicle, char const *seat, short a, short *seats, short count) { reached("vehicle_scripting_find_available_seats"); return 0; }
static void coop_enemies_rider_seated(long vehicle_index, long unit_index) { reached("coop_enemies_rider_seated"); }
static boolean coop_enemies_rider_unseated(long vehicle_index, long unit_index) { reached("coop_enemies_rider_unseated"); return FALSE; }
static void actor_erase(long actor_index, boolean a) { reached("actor_erase"); }
static void object_initialize_vitality(long object_index, real *body, real *shield) { reached("object_initialize_vitality"); }
static void object_deplete_shield(long object_index) { reached("object_deplete_shield"); }
static void object_deplete_body(long object_index) { reached("object_deplete_body"); }
static long unit_inventory_get_weapon(long unit_index, short index) { reached("unit_inventory_get_weapon"); return NONE; }
static void actor_delete(long actor_index, boolean a) { reached("actor_delete"); }
static void actor_look_secondary_stop(long actor_index) { reached("actor_look_secondary_stop"); }
static struct actor_datum *actor_get(long actor_index) { return STUB_POINTER(struct actor_datum, actor_get); }
static void actor_look_secondary(long actor_index, short a, short b, struct direction_specification *direction) { reached("actor_look_secondary"); }
static void encounter_actor_iterator_new(struct encounter_actor_iterator *iterator, long encounter) { reached("encounter_actor_iterator_new"); }
static struct actor_datum *encounter_actor_iterator_next(struct encounter_actor_iterator *iterator) { return NULL; }
static void actor_change_encounter(long actor_index, short encounter, short squad) { reached("actor_change_encounter"); }
static void ai_index_actor_iterator_new(long ai_reference, struct ai_script_actor_reference_iterator *iterator) { reached("ai_index_actor_iterator_new"); }
static struct actor_datum *ai_index_actor_iterator_next(struct ai_script_actor_reference_iterator *iterator) { return NULL; }
static void encounter_force_activate(long encounter_index) { reached("encounter_force_activate"); }
static long prop_get_base_by_unit_index(long actor_index, long unit_index, boolean a, boolean b) { reached("prop_get_base_by_unit_index"); return NONE; }
static void actor_handle_unit_effect(long actor_index, long prop_index, short effect) { reached("actor_handle_unit_effect"); }
static struct actor_variant_definition *actor_variant_definition_get(long index) { return STUB_POINTER(struct actor_variant_definition, actor_variant_definition_get); }
static struct actor_definition *actor_definition_get(long index) { return STUB_POINTER(struct actor_definition, actor_definition_get); }
static long actor_create_for_unit(boolean a, long unit_index, long b, long c, short d, boolean e, long f, boolean g, short h, short i, short j, short k) { reached("actor_create_for_unit"); return NONE; }
static void encounters_update_dirty_status(void) { reached("encounters_update_dirty_status"); }
static long stubbs_horde_palette_variant_for_unit(long unit_index) { reached("stubbs_horde_palette_variant_for_unit"); return NONE; }
static struct sound_definition *sound_definition_get(long index) { return (struct sound_definition *)zeroes; }
static void sound_stop_impulse(long index) { }
static short object_get_marker_by_name(long object_index, char const *name, struct object_marker *markers, short count) { reached("object_get_marker_by_name"); return 0; }
static long object_impulse_sound_new(long object_index, long definition, short node, real_point3d const *position, real_vector3d const *forward, real scale) { reached("object_impulse_sound_new"); return NONE; }
static void object_type_notify_impulse_sound(long object_index, long definition, long sound) { reached("object_type_notify_impulse_sound"); }
static long unspatialized_impulse_sound_new(long definition, real scale) { return NONE; }
static struct looping_sound_definition *looping_sound_definition_get(long index) { return (struct looping_sound_definition *)zeroes; }
static void scripted_looping_sound_stop(long index) { }
static void scripted_music_stop_all(void) { }
static long unattached_looping_sound_start(long definition, long source_object_index, real gain)
{
	sound_source = source_object_index;
	return NONE;
}
static struct game_looping_sound_datum *game_looping_sound_get(long index) { return (struct game_looping_sound_datum *)zeroes; }
static real_point3d const origin3d, *global_origin3d = &origin3d;
static real_vector3d const forward3d = { 1.f, 0.f, 0.f }, *global_forward3d = &forward3d;
static boolean hs_unit_can_see_flag(long unit_index, short flag, real degrees) { reached("hs_unit_can_see_flag"); return FALSE; }
static boolean unit_can_see_point(long unit_index, real_point3d const *point, real angle) { reached("unit_can_see_point"); return FALSE; }
static void unit_get_head_position(long unit_index, real_point3d *point) { reached("unit_get_head_position"); }
static boolean object_delete_by_script_allowed(long object_index) { return TRUE; }
static struct object_definition *object_definition_get(long index) { return STUB_POINTER(struct object_definition, object_definition_get); }
static struct model *model_definition_get(long index) { return STUB_POINTER(struct model, model_definition_get); }
static void object_permute_region(long object_index, char const *permutation, short region, boolean a) { reached("object_permute_region"); }
static void object_predict(long object_index) { reached("object_predict"); }
static void effect_new_attached_from_markers(long a, long b, long c, short d, short e, char const **f, real_point3d const *g, real_vector3d const *h, real i, real j, void *k, void *l) { reached("effect_new_attached_from_markers"); }
static void damage_data_new(struct damage_data *damage, long definition) { }
static void scenario_location_from_point(long *location, real_point3d const *point) { reached("scenario_location_from_point"); }
static void object_cause_damage(struct damage_data *damage, long object_index, long a, long b, long c, void *d) { reached("object_cause_damage"); }
static boolean unit_release_item(long object_index) { reached("unit_release_item"); return FALSE; }
static void vector3d_from_euler_angles2d(real_vector3d *vector, real_euler_angles2d const *angles) { memset(vector, 0, sizeof(*vector)); }
static void object_reset(long object_index) { reached("object_reset"); }
static real_matrix4x3 *object_get_node_matrix(long object_index, short node) { return STUB_POINTER(real_matrix4x3, object_get_node_matrix); }
static void matrix4x3_inverse(real_matrix4x3 const *matrix, real_matrix4x3 *inverse) { }
static void matrix4x3_transform_normal(real_matrix4x3 const *matrix, real_vector3d const *in, real_vector3d *out) { }
static struct player_datum *player_get(long index) { return STUB_POINTER(struct player_datum, player_get); }
static void player_teleport(long player_index, long a, real_point3d const *position) { reached("player_teleport"); }
static void player_control_set_facing(short local_player_index, real_vector3d const *forward) { reached("player_control_set_facing"); }
static void object_set_position(long object_index, real_point3d const *position, real_vector3d const *forward, real_vector3d const *up) { reached("object_set_position"); }

/* (the desktop builds' Stubbs mode, on: ai_attach gives a squad with no actor
variant the unit's own) */
#define HALO_MAP_LIST
/* (the commands unoptimized: a NULL they read must fault, as in the game,
which is built with -fno-delete-null-pointer-checks, rather than be taken
for a path that can't happen) */
#if defined(__clang__)
#pragma clang optimize off
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize ("O0")
#endif
#include "under_test.inc"
#if defined(__clang__)
#pragma clang optimize on
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif

/* ---------- the cases */

static long spawn(short type)
{
	long index = datum_new(object_header_data);
	struct object_header_datum *header = datum_get(object_header_data, index);
	struct object_datum *object = (struct object_datum *)&objects[DATUM_INDEX_TO_ABSOLUTE_INDEX(index)];

	memset(object, 0, sizeof(objects[0]));
	header->type = (byte)type;
	header->datum = object;
	object->object.type = type;
	object->object.parent_object_index = NONE;
	object->object.next_object_index = NONE;
	object->object.first_child_object_index = NONE;
	object->object.animation.animation_graph_index = NONE;
	if (TEST_FLAG(_object_mask_unit, type))
	{
		struct unit_datum *unit = (struct unit_datum *)object;
		short weapon_index;

		unit->unit.player_index = NONE;
		unit->unit.actor_index = NONE;
		unit->unit.swarm_actor_index = NONE;
		unit->unit.parent_seat_index = NONE;
		for (weapon_index = 0; weapon_index < MAXIMUM_WEAPONS_PER_UNIT; weapon_index++)
			unit->unit.weapon_object_indices[weapon_index] = NONE;
	}
	return index;
}

/* each command as a script calls it, given its object (x) */
/* (_player: the object is a player's unit; _others: the command goes on with
another, live object, so only the missing one's halt is checked) */
enum { _player = 1, _others = 2 };
#define COMMAND(name, type, call, ...) { name, type, call, __VA_ARGS__ + 0 }
static void run_object_destroy(long x) { hs_object_destroy(x); }
static void run_object_teleport(long x) { hs_object_teleport(x, 0); }
static void run_object_set_facing(long x) { hs_object_set_facing(x, 0); }
static void run_object_set_shield(long x) { hs_object_set_shield(x, 1.f); }
static void run_object_set_permutation(long x) { hs_object_set_permutation(x, "", "damaged"); }
static void run_objects_predict(long x) { listed_object = x; hs_objects_predict(0); }
static void run_effect_new_on_object_marker(long x) { hs_effect_new_from_object_marker(0, x, "head"); }
static void run_damage_object(long x) { hs_damage_object(0, x); }
static void run_objects_can_see_object(long x)
{
	listed_object = spawn(_object_type_biped);
	hs_objects_can_see_object(0, x, 45.f);
}
static void run_volume_test_object(long x) { scenario_trigger_volume_test_object(0, x); }
static void run_volume_test_objects(long x) { listed_object = x; hs_trigger_volume_test_objects(0, 0, FALSE); }
static void run_volume_test_objects_all(long x) { listed_object = x; hs_trigger_volume_test_objects(0, 0, TRUE); }
static void run_object_beautify(long x) { object_beautify(x, TRUE); }
static void run_object_set_collideable(long x) { object_scripting_set_collideable(x, FALSE); }
static void run_object_set_scale(long x) { objects_scripting_set_scale(x, 2.f, 0); }
static void run_objects_attach_parent(long x) { objects_scripting_attach(x, "", spawn(_object_type_scenery), ""); }
static void run_objects_attach_child(long x) { objects_scripting_attach(spawn(_object_type_scenery), "", x, ""); }
static void run_objects_detach(long x) { objects_scripting_detach(NONE + 1, x); }
static void run_object_can_take_damage(long x) { listed_object = x; object_can_take_damage(0); }
static void run_object_cannot_take_damage(long x) { listed_object = x; object_cannot_take_damage(0); }
static void run_object_set_ranged_attack_inhibited(long x) { object_set_ranged_attack_inhibited(x, TRUE); }
static void run_object_set_melee_attack_inhibited(long x) { object_set_melee_attack_inhibited(x, TRUE); }
static void run_scenery_get_animation_time(long x) { scenery_get_animation_time(x); }
static void run_scenery_animation_start(long x) { scenery_animation_start(x, 0, "idle"); }
static void run_scenery_animation_start_at_frame(long x) { scenery_animation_start_at_frame(x, 0, "idle", 2); }
static void run_device_get_position(long x) { device_get_position(x); }
static void run_device_get_power(long x) { device_get_power(x); }
static void run_device_set_position_immediate(long x) { device_set_actual_position(x, 1.f); }
static void run_device_set_position(long x) { device_set_desired_position(x, 1.f); }
static void run_device_set_power(long x) { device_set_power(x, 1.f); }
static void run_recording_play(long x) { recorded_animation_play(x, 0); }
static void run_recording_play_and_delete(long x) { recorded_animation_play_and_delete(x, 0); }
static void run_recording_play_and_hover(long x) { recorded_animation_play_and_hover(x, 0); }
static void run_vehicle_hover(long x) { vehicle_hover(x, TRUE); }
static void run_player_add_equipment(long x) { player_add_equipment(x, 0, TRUE); }
static void run_unit_kill(long x) { unit_kill(x); }
static void run_unit_kill_silent(long x) { unit_kill_silent(x); }
static void run_unit_set_enterable_by_player(long x) { unit_set_enterable_by_player(x, FALSE); }
static void run_unit_aim_without_turning(long x) { unit_aim_without_turning(x, TRUE); }
static void run_unit_can_blink(long x) { unit_scripting_can_blink(x, FALSE); }
static void run_unit_set_desired_flashlight_state(long x) { unit_set_desired_flashlight_state(x, TRUE); }
static void run_vehicle_riders(long x) { unit_scripting_unit_riders(x); }
static void run_vehicle_test_seat_list(long x) { unit_scripting_vehicle_test_seat_list(x, "driver", 0); }
static void run_vehicle_test_seat_vehicle(long x) { unit_scripting_vehicle_test_seat(x, "driver", spawn(_object_type_biped)); }
static void run_vehicle_test_seat_unit(long x) { unit_scripting_vehicle_test_seat(spawn(_object_type_vehicle), "driver", x); }
static void run_unit_set_emotion_animation(long x) { unit_scripting_set_emotion_animation(x, "happy"); }
static void run_unit_suspended(long x) { unit_scripting_suspended(x, TRUE); }
static void run_unit_set_seat(long x) { unit_scripting_set_seat(x, "driver"); }
static void run_unit_get_current_flashlight_state(long x) { unit_get_current_flashlight_state(x); }
static void run_unit_set_emotion(long x) { unit_set_emotion(x, 1); }
static void run_unit_is_playing_custom_animation(long x) { unit_is_playing_custom_animation(x); }
static void run_unit_get_custom_animation_time(long x) { unit_get_custom_animation_time(x); }
static void run_custom_animation(long x) { unit_start_user_animation(x, 0, "wave", FALSE); }
static void run_unit_custom_animation_at_frame(long x) { unit_custom_animation_at_frame(x, 0, "wave", FALSE, 2); }
static void run_unit_stop_custom_animation(long x) { unit_stop_custom_animation(x); }
static void run_unit_enter_vehicle_unit(long x) { unit_scripting_enter_vehicle(x, spawn(_object_type_vehicle), "driver"); }
static void run_unit_enter_vehicle_vehicle(long x) { unit_scripting_enter_vehicle(spawn(_object_type_biped), x, "driver"); }
static void run_vehicle_unload(long x) { vehicle_scripting_unload(x, ""); }
static void run_unit_exit_vehicle(long x) { unit_scripting_exit_vehicle(x); }
static void run_vehicle_load_magic_vehicle(long x) { listed_object = spawn(_object_type_biped); vehicle_scripting_load_magic(x, "", 0); }
static void run_vehicle_load_magic_rider(long x) { listed_object = x; vehicle_scripting_load_magic(spawn(_object_type_vehicle), "", 0); }
static void run_unit_open(long x) { unit_open(x); }
static void run_unit_close(long x) { unit_close(x); }
static void run_unit_set_maximum_vitality(long x) { unit_scripting_set_maximum_vitality(x, 100.f, 100.f); }
static void run_units_set_maximum_vitality(long x) { listed_object = x; units_scripting_set_maximum_vitality(0, 100.f, 100.f); }
static void run_unit_set_current_vitality(long x) { unit_scripting_set_current_vitality(x, 1.f, 1.f); }
static void run_units_set_current_vitality(long x) { listed_object = x; units_scripting_set_current_vitality(0, 1.f, 1.f); }
static void run_unit_has_weapon(long x) { unit_scripting_has_weapon(x, 0); }
static void run_unit_has_weapon_readied(long x) { unit_scripting_has_weapon_readied(x, 0); }
static void run_ai_detach(long x) { ai_scripting_detach_unit(x); }
static void run_ai_detach_units(long x) { listed_object = x; ai_scripting_detach_units(0); }
static void run_ai_stop_looking(long x) { ai_scripting_stop_looking(x); }
static void run_ai_force_active_by_unit(long x) { ai_scripting_force_active_by_unit(x, TRUE); }
static void run_ai_look_at_object(long x) { ai_scripting_look_at_object(x, spawn(_object_type_scenery)); }
static void run_ai_vehicle_encounter(long x) { ai_scripting_vehicle_encounter(x, NONE); }
static void run_ai_magically_see_unit(long x) { ai_scripting_magically_see_unit(0, x); }
static void run_ai_magically_see_units(long x) { listed_object = x; ai_scripting_magically_see_units(0, 0); }
static void run_ai_attach(long x) { ai_scripting_attach_unit(x, 0); }
static void run_sound_impulse_start(long x) { scripted_sound_new(0, x, 1.f); }
static void run_sound_looping_start(long x) { scripted_looping_sound_start(0, x, 1.f); }

static struct
{
	char const *name;
	short type;
	void (*run)(long object_index);
	unsigned flags;
} const commands[] =
{
	COMMAND("object_destroy", _object_type_scenery, run_object_destroy),
	COMMAND("object_teleport", _object_type_scenery, run_object_teleport),
	COMMAND("object_set_facing", _object_type_scenery, run_object_set_facing),
	COMMAND("object_set_shield", _object_type_scenery, run_object_set_shield),
	COMMAND("object_set_permutation", _object_type_scenery, run_object_set_permutation),
	COMMAND("objects_predict", _object_type_scenery, run_objects_predict),
	COMMAND("effect_new_on_object_marker", _object_type_scenery, run_effect_new_on_object_marker),
	COMMAND("damage_object", _object_type_scenery, run_damage_object),
	COMMAND("objects_can_see_object", _object_type_scenery, run_objects_can_see_object),
	COMMAND("volume_test_object", _object_type_scenery, run_volume_test_object),
	COMMAND("volume_test_objects", _object_type_scenery, run_volume_test_objects),
	COMMAND("volume_test_objects_all", _object_type_scenery, run_volume_test_objects_all),
	COMMAND("object_beautify", _object_type_scenery, run_object_beautify),
	COMMAND("object_set_collideable", _object_type_scenery, run_object_set_collideable),
	COMMAND("object_set_scale", _object_type_scenery, run_object_set_scale),
	COMMAND("objects_attach_parent", _object_type_scenery, run_objects_attach_parent),
	COMMAND("objects_attach_child", _object_type_scenery, run_objects_attach_child),
	COMMAND("objects_detach", _object_type_scenery, run_objects_detach),
	COMMAND("object_can_take_damage", _object_type_scenery, run_object_can_take_damage),
	COMMAND("object_cannot_take_damage", _object_type_scenery, run_object_cannot_take_damage),
	COMMAND("object_set_ranged_attack_inhibited", _object_type_scenery, run_object_set_ranged_attack_inhibited),
	COMMAND("object_set_melee_attack_inhibited", _object_type_scenery, run_object_set_melee_attack_inhibited),
	COMMAND("scenery_get_animation_time", _object_type_scenery, run_scenery_get_animation_time),
	COMMAND("scenery_animation_start", _object_type_scenery, run_scenery_animation_start),
	COMMAND("scenery_animation_start_at_frame", _object_type_scenery, run_scenery_animation_start_at_frame),
	COMMAND("device_get_position", _object_type_machine, run_device_get_position),
	COMMAND("device_get_power", _object_type_machine, run_device_get_power),
	COMMAND("device_set_position_immediate", _object_type_machine, run_device_set_position_immediate),
	COMMAND("device_set_position", _object_type_machine, run_device_set_position),
	COMMAND("device_set_power", _object_type_machine, run_device_set_power),
	COMMAND("recording_play", _object_type_biped, run_recording_play),
	COMMAND("recording_play_and_delete", _object_type_biped, run_recording_play_and_delete),
	COMMAND("recording_play_and_hover", _object_type_vehicle, run_recording_play_and_hover),
	COMMAND("vehicle_hover", _object_type_vehicle, run_vehicle_hover),
	COMMAND("player_add_equipment", _object_type_biped, run_player_add_equipment, _player),
	COMMAND("unit_kill", _object_type_biped, run_unit_kill),
	COMMAND("unit_kill_silent", _object_type_biped, run_unit_kill_silent),
	COMMAND("unit_set_enterable_by_player", _object_type_vehicle, run_unit_set_enterable_by_player),
	COMMAND("unit_aim_without_turning", _object_type_biped, run_unit_aim_without_turning),
	COMMAND("unit_can_blink", _object_type_biped, run_unit_can_blink),
	COMMAND("unit_set_desired_flashlight_state", _object_type_biped, run_unit_set_desired_flashlight_state),
	COMMAND("vehicle_riders", _object_type_vehicle, run_vehicle_riders),
	COMMAND("vehicle_test_seat_list", _object_type_vehicle, run_vehicle_test_seat_list),
	COMMAND("vehicle_test_seat_vehicle", _object_type_vehicle, run_vehicle_test_seat_vehicle),
	COMMAND("vehicle_test_seat_unit", _object_type_biped, run_vehicle_test_seat_unit),
	COMMAND("unit_set_emotion_animation", _object_type_biped, run_unit_set_emotion_animation),
	COMMAND("unit_suspended", _object_type_biped, run_unit_suspended),
	COMMAND("unit_set_seat", _object_type_biped, run_unit_set_seat),
	COMMAND("unit_get_current_flashlight_state", _object_type_biped, run_unit_get_current_flashlight_state),
	COMMAND("unit_set_emotion", _object_type_biped, run_unit_set_emotion),
	COMMAND("unit_is_playing_custom_animation", _object_type_biped, run_unit_is_playing_custom_animation),
	COMMAND("unit_get_custom_animation_time", _object_type_biped, run_unit_get_custom_animation_time),
	COMMAND("custom_animation", _object_type_biped, run_custom_animation),
	COMMAND("unit_custom_animation_at_frame", _object_type_biped, run_unit_custom_animation_at_frame),
	COMMAND("unit_stop_custom_animation", _object_type_biped, run_unit_stop_custom_animation),
	COMMAND("unit_enter_vehicle_unit", _object_type_biped, run_unit_enter_vehicle_unit),
	COMMAND("unit_enter_vehicle_vehicle", _object_type_vehicle, run_unit_enter_vehicle_vehicle),
	COMMAND("vehicle_unload", _object_type_vehicle, run_vehicle_unload),
	COMMAND("unit_exit_vehicle", _object_type_biped, run_unit_exit_vehicle),
	COMMAND("vehicle_load_magic_vehicle", _object_type_vehicle, run_vehicle_load_magic_vehicle),
	COMMAND("vehicle_load_magic_rider", _object_type_biped, run_vehicle_load_magic_rider, _others),
	COMMAND("unit_open", _object_type_vehicle, run_unit_open),
	COMMAND("unit_close", _object_type_vehicle, run_unit_close),
	COMMAND("unit_set_maximum_vitality", _object_type_biped, run_unit_set_maximum_vitality),
	COMMAND("units_set_maximum_vitality", _object_type_biped, run_units_set_maximum_vitality),
	COMMAND("unit_set_current_vitality", _object_type_biped, run_unit_set_current_vitality),
	COMMAND("units_set_current_vitality", _object_type_biped, run_units_set_current_vitality),
	COMMAND("unit_has_weapon", _object_type_biped, run_unit_has_weapon),
	COMMAND("unit_has_weapon_readied", _object_type_biped, run_unit_has_weapon_readied),
	COMMAND("ai_detach", _object_type_biped, run_ai_detach),
	COMMAND("ai_detach_units", _object_type_biped, run_ai_detach_units),
	COMMAND("ai_stop_looking", _object_type_biped, run_ai_stop_looking),
	COMMAND("ai_force_active_by_unit", _object_type_biped, run_ai_force_active_by_unit),
	COMMAND("ai_look_at_object", _object_type_biped, run_ai_look_at_object),
	COMMAND("ai_vehicle_encounter", _object_type_vehicle, run_ai_vehicle_encounter),
	COMMAND("ai_magically_see_unit", _object_type_biped, run_ai_magically_see_unit),
	COMMAND("ai_magically_see_units", _object_type_biped, run_ai_magically_see_units),
	COMMAND("ai_attach", _object_type_biped, run_ai_attach),
	COMMAND("sound_impulse_start", _object_type_scenery, run_sound_impulse_start),
	COMMAND("sound_looping_start", _object_type_scenery, run_sound_looping_start),
};

int main(int argc, char **argv)
{
	char name[64];
	char *state;
	long index, object_index;
	boolean sound;
	static char before[sizeof(objects)];

	CHECK(argc == 2 && strchr(argv[1], ':') && strlen(argv[1]) < sizeof(name), "usage: <command>:<state>");
	strcpy(name, argv[1]);
	state = strchr(name, ':') + 1;
	state[-1] = 0;
	object_header_data = game_state_data_new("object", NUMBEROF(objects), sizeof(struct object_header_datum));
	for (index = 0; index < NUMBEROF(commands) && strcmp(commands[index].name, name); index++)
		;
	CHECK(index < NUMBEROF(commands), "no command %s", name);
	sound = !strcmp(name, "sound_impulse_start") || !strcmp(name, "sound_looping_start");

	if (!strcmp(state, "none"))
	{
		object_index = NONE;
	}
	else
	{
		object_index = spawn(commands[index].type);
		if (!strcmp(state, "deleted") || !strcmp(state, "reused"))
			datum_delete(object_header_data, object_index);
		/* (the slot taken by a new object since: the old index is still gone) */
		if (!strcmp(state, "reused"))
			CHECK(DATUM_INDEX_TO_ABSOLUTE_INDEX(spawn(commands[index].type)) == DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index),
				"slot not reused");
		else
			CHECK(!strcmp(state, "live") || !strcmp(state, "deleted"), "no state %s", state);
	}

	if (TEST_FLAG(commands[index].flags, 0) && !strcmp(state, "live"))
		((struct unit_datum *)&objects[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)])->unit.player_index = 0;
	/* (one that can take damage again: so that it shows) */
	if (!strcmp(name, "object_can_take_damage") && !strcmp(state, "live"))
		((struct object_datum *)&objects[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)])->object.damage_flags =
			FLAG(_object_cannot_take_damage_bit);
	memcpy(before, objects, sizeof(objects));
	commands[index].run(object_index);

	if (!strcmp(state, "live") && sound)
	{
		CHECK(sound_source == object_index, "the sound was given object 0x%lx", (unsigned long)sound_source);
	}
	else if (!strcmp(state, "live"))
	{
		/* (past its check: it gets the object, does its work or changes it) */
		CHECK(get_count + reached_count > 0 || memcmp(before, objects, sizeof(objects)),
			"a live object was taken as none");
	}
	else if (sound)
	{
		/* (a sound from no object: played as from none) */
		CHECK(sound_source == NONE, "the sound was given object 0x%lx", (unsigned long)sound_source);
		CHECK(!reached_count, "reached %s", reached_name);
	}
	else if (!TEST_FLAG(commands[index].flags, 1))
	{
		CHECK(!reached_count, "a missing object reached %s", reached_name);
	}
	return 0;
}
