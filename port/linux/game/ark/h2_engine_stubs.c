/* H2_ENGINE_STUBS.C
	(port) Stand-ins for the engine functions and globals Project Ark added to its
	own engine (source/, port/linux/src) that Halo 2 mode's units call and this
	engine does not have yet. Each does nothing and answers zero, so the mode
	builds and loads; the ones a Halo 2 level needs to play as Ark plays it are
	ported for real one by one (PLAN-next lists them by subsystem, H1-H9).
	Generated from Ark's definitions (their signatures as Ark has them):
	tools: arkfork-out/tools/stubgen.py. */

#include "cseries.h"
#include <string.h>
#include <stdint.h>
#ifndef BOOL
#define BOOL int
#endif

/* (source/hs/hs_runtime.c) */
long* hs_runtime_arguments_evaluate_all(long thread_index, short maximum, short *count, boolean initialize)
{
	(void)thread_index;
	(void)maximum;
	(void)count;
	(void)initialize;
	return 0;
}

/* (port/linux/src/xbox_kernel.c) */
void platform_heap_usage(unsigned long *in_use, unsigned long *capacity)
{
	(void)in_use;
	(void)capacity;
}

/* (port/linux/game/tag_validate.c) */
void tag_validate_pack_allow_globals(boolean allowed)
{
	(void)allowed;
}

/* (source/scenario/scenario.c) */
unsigned long halo_scenario_generation;

/* (port/linux/game/tick_detail.c) */
int halo_tick_detail_state;

/* (source/cache/xbox_texture_cache.c) */
unsigned long texture_cache_hardware_format_size(void)
{
	return 0;
}

/* (source/cache/cache_files.c) */
long cache_file_tag_data_size(void)
{
	return 0;
}

/* (port/linux/src/xbox_memory.c) */
void platform_memory_log(const char *when, const char *map_name)
{
	(void)when;
	(void)map_name;
}

/* (port/linux/src/xbox_memory.c) */
unsigned long platform_contiguous_largest_free(void)
{
	return 0;
}

/* (source/cache/cache_files.c) */
long halo_loaded_tag_count;

/* (source/main/main.c) */
char halo_screenshot_name[64];

/* (port/linux/game/custom_edition_cache.c) */
boolean custom_edition_cache_tags_loaded(void)
{
	return 0;
}

/* (source/hs/hs_runtime.c) */
long hs_runtime_type_default(short type)
{
	(void)type;
	return 0;
}

/* (port/linux/game/tick_detail.c) */
void halo_tick_detail_end(const char *name, unsigned long long started)
{
	(void)name;
	(void)started;
}

/* (port/linux/game/tick_detail.c) */
unsigned long long halo_tick_detail_begin(void)
{
	return 0;
}

/* (port/linux/game/load_profile.c) */
int halo_game_thread_index(void)
{
	return 0;
}

/* (port/linux/src/d3d8_gl.c) */
void halo_d3d_buffer_in_place(const void *buffer, int in_place)
{
	(void)buffer;
	(void)in_place;
}

/* (source/game/game.c) */
void game_loading_screen_h2_wait(void)
{
}

/* (source/ai/ai.c) */
boolean ai_globals_dialogue_triggers_are_enabled(void)
{
	return 0;
}

/* (source/cache/xbox_texture_cache.c) */
void texture_cache_hardware_format_make(struct bitmap_data *bitmap, void *texture)
{
	(void)bitmap;
	(void)texture;
}

/* (port/linux/src/xbox_kernel.c) */
void platform_heap_census_log(const char *when)
{
	(void)when;
}

/* (port/linux/src/xbox_memory.c) */
void platform_contiguous_side_end(void)
{
}

/* (port/linux/src/network_wire.c) */
int network_wire_crossplay(void)
{
	return 0;
}

/* (source/hs/hs_runtime.c) */
void hs_runtime_thread_end(long thread_index)
{
	(void)thread_index;
}

/* (port/linux/src/stall_trace.c) */
void halo_stall_note(const char *what, unsigned long a, unsigned long b)
{
	(void)what;
	(void)a;
	(void)b;
}

/* (port/linux/game/dual_wield.c) */
int dual_wield_units_ever;

/* (source/units/units.c) */
long unit_seat_occupant(long vehicle_index, short seat_index)
{
	(void)vehicle_index;
	(void)seat_index;
	return 0;
}

/* (source/rasterizer/xbox/rasterizer_xbox.c) */
unsigned long rasterizer_stand_in_draws;

/* (port/linux/src/xbox_files.c) */
int platform_mcc_root(char *out, unsigned long out_size)
{
	(void)out;
	(void)out_size;
	return 0;
}

/* (port/linux/src/xbox_memory.c) */
void* platform_contiguous_side_alloc(unsigned long size, unsigned long alignment)
{
	(void)size;
	(void)alignment;
	return 0;
}

/* (source/objects/objects.c) */
void object_region_permutation_set(long object_index, short region_index, byte permutation_index)
{
	(void)object_index;
	(void)region_index;
	(void)permutation_index;
}

/* (source/objects/objects.c) */
unsigned long object_node_compute_census[2];

/* (source/hs/hs_runtime.c) */
boolean hs_runtime_thread_due(long thread_index)
{
	(void)thread_index;
	return 0;
}

/* (source/hs/hs_runtime.c) */
long hs_runtime_executing_thread_index(void)
{
	return 0;
}

/* (source/game/game.c) */
void game_loading_screen_frame(real progress)
{
	(void)progress;
}

/* (port/linux/game/dual_wield.c) */
long dual_wield_left_weapon(long unit_index)
{
	(void)unit_index;
	return 0;
}

/* (port/linux/game/custom_edition_cache.c) */
boolean custom_edition_cache_packs_allowed(unsigned long first_offset)
{
	(void)first_offset;
	return 0;
}

/* (source/cache/cache_files_windows.c) */
void cache_file_request_finish(void *completion_flag, int ok)
{
	(void)completion_flag;
	(void)ok;
}

/* (source/cache/cache_files_windows.c) */
short cache_file_read_bitmap(long tag_index, long offset, long size, void *buffer, boolean *completion_flag_reference, boolean blocking)
{
	(void)tag_index;
	(void)offset;
	(void)size;
	(void)buffer;
	(void)completion_flag_reference;
	(void)blocking;
	return 0;
}

/* (port/linux/game/bots.c) */
int bots_player_is_bot(long player_index)
{
	(void)player_index;
	return 0;
}

/* (port/linux/src/xbox_files.c) */
int xbox_files_read_fallback_directory(const char *host_directory, char *out, unsigned long out_size)
{
	(void)host_directory;
	(void)out;
	(void)out_size;
	return 0;
}

/* (port/linux/game/tag_validate.c) */
void tag_validate_pack_allow_structure(boolean allowed)
{
	(void)allowed;
}

/* (port/linux/game/tag_validate.c) */
void tag_validate_allow_tool_maximums(boolean allowed)
{
	(void)allowed;
}

/* (source/sound/sound_manager.c) */
long sound_steal_fade_milliseconds;

/* (source/sound/sound_manager.c) */
boolean sound_distance_falloff_halo2;

/* (source/cache/xbox_sound_cache.c) */
boolean sound_cache_sound_in_use(struct sound_permutation *sound)
{
	(void)sound;
	return 0;
}

/* (port/linux/game/scene_pack_ce.c) */
char const* scene_pack_custom_edition_builder_version(void)
{
	return 0;
}

/* (port/linux/game/scene_pack_ce.c) */
boolean scene_pack_ce_source_check(struct scene_pack_recipe const *recipe, char const *maps_directory, char *map_name, long map_name_size, unsigned long long *source_hash, char *why, long why_size)
{
	(void)recipe;
	(void)maps_directory;
	(void)map_name;
	(void)map_name_size;
	(void)source_hash;
	(void)why;
	(void)why_size;
	return 0;
}

/* (port/linux/game/scene_pack_ce.c) */
struct scene_pack_ce* scene_pack_ce_open(char const *maps_directory, char const *map_name, void *scratch, unsigned long scratch_size, int volatile const *cancel, long volatile *progress, char *why, long why_size)
{
	(void)maps_directory;
	(void)map_name;
	(void)scratch;
	(void)scratch_size;
	(void)cancel;
	(void)progress;
	(void)why;
	(void)why_size;
	return 0;
}

/* (port/linux/game/scene_pack_ce.c) */
void scene_pack_ce_close(struct scene_pack_ce *ce)
{
	(void)ce;
}

/* (source/rasterizer/xbox/rasterizer_xbox_active_camouflage.c) */
boolean rasterizer_distortion_available(void)
{
	return 0;
}

/* (port/linux/game/ragdolls.c) */
boolean ragdolls_object_has_pose(long object_index)
{
	(void)object_index;
	return 0;
}

/* (port/linux/game/player_cosmetics.c) */
short player_cosmetics_profile_color(long profile_index, short primary_color_index)
{
	(void)profile_index;
	(void)primary_color_index;
	return 0;
}

/* (port/linux/game/player_cosmetics.c) */
unsigned long player_cosmetics_color_rgb(short color)
{
	(void)color;
	return 0;
}

/* (source/game/player_control.c) */
void player_control_set_movement_disabled(boolean disabled)
{
	(void)disabled;
}

/* (port/linux/src/xbox_memory.c) */
unsigned long platform_contiguous_side_usage(unsigned long *blocks)
{
	(void)blocks;
	return 0;
}

/* (port/linux/src/xbox_memory.c) */
void platform_contiguous_side_begin(void)
{
}

/* (source/objects/objects.c) */
byte object_region_damage_get(long object_index, short region_index)
{
	(void)object_index;
	(void)region_index;
	return 0;
}

/* (port/linux/game/menu_scene_objects.c) */
int menu_scene_spartan_show(char const *camera, char const *placement, short color_index)
{
	(void)camera;
	(void)placement;
	(void)color_index;
	return 0;
}

/* (source/main/main.c) */
long main_test_vehicle;

/* (source/main/main.c) */
boolean main_checkpoint_pending_immediate(void)
{
	return 0;
}

/* (source/hs/hs_runtime.c) */
boolean hs_runtime_thread_alive(long thread_index)
{
	(void)thread_index;
	return 0;
}

/* (source/hs/hs_runtime.c) */
long hs_runtime_command_script_new(short script_index)
{
	(void)script_index;
	return 0;
}

/* (port/linux/src/xbox_memory.c) */
int halo_world_geometry_holds(const void *address, unsigned long size)
{
	(void)address;
	(void)size;
	return 0;
}

/* (port/linux/src/xbox_memory.c) */
unsigned char* halo_world_geometry_acquire(unsigned long bytes)
{
	(void)bytes;
	return 0;
}

/* (port/linux/src/frame_timing.c) */
void halo_progress_heartbeat(void)
{
}

/* (port/linux/src/menu_files.c) */
void halo_menus_file_free(unsigned char *data)
{
	(void)data;
}

/* (source/main/main.c) */
int halo_main_menu_loaded(void)
{
	return 0;
}

/* (port/linux/game/load_profile.c) */
unsigned long long halo_load_profile_now(void)
{
	return 0;
}

/* (port/linux/game/fine_profile.c) */
unsigned long long halo_fine_tick_now(void)
{
	return 0;
}

/* (port/linux/src/posix_files.c) */
int halo_file_identity(const char *xbox_path, unsigned long *size, unsigned long *seconds, unsigned long *nanoseconds)
{
	(void)xbox_path;
	(void)size;
	(void)seconds;
	(void)nanoseconds;
	return 0;
}

/* (tools/map_validate.c) */
void halo_custom_edition_memory_free(void *address)
{
	(void)address;
}

/* (port/linux/game/h2_jolt_hull.c) */
void h2_jolt_hull_push(long vehicle_index, union real_vector3d const *acceleration, union real_vector3d const *turning)
{
	(void)vehicle_index;
	(void)acceleration;
	(void)turning;
}

/* (port/linux/src/fsb_celt.c) */
size_t fsb_celt_decoder_bytes(int channels)
{
	(void)channels;
	return 0;
}

/* (port/linux/game/emblem_net.c) */
struct h2_emblem const* emblem_net_player(int machine, int controller)
{
	(void)machine;
	(void)controller;
	return 0;
}

/* (port/linux/src/dsound_sdl.c) */
int dsound_sdl_falloff_halo2;

/* (port/linux/game/custom_edition_geometry.c) */
void custom_edition_part_palette_add(struct vertex_buffer const *vertex_buffer, byte const *nodes, short node_count)
{
	(void)vertex_buffer;
	(void)nodes;
	(void)node_count;
}

/* (port/linux/game/custom_edition_maps.c) */
boolean custom_edition_maps_turned_off(char const *name)
{
	(void)name;
	return 0;
}

/* (source/cache/cache_files_windows.c) */
boolean cache_files_cached_copy_unchanged(unsigned long token)
{
	(void)token;
	return 0;
}

/* (source/cache/cache_files_windows.c) */
FILE* cache_files_cached_copy_open(char const *map, unsigned long *token)
{
	(void)map;
	(void)token;
	return 0;
}

/* (source/items/weapons.c) */
real weapon_illumination_get(long weapon_index)
{
	(void)weapon_index;
	return 0.0f;
}

/* (source/sound/game_sound.c) */
long unspatialized_impulse_sound_new_gain(long definition_index, real scale, real gain)
{
	(void)definition_index;
	(void)scale;
	(void)gain;
	return 0;
}

/* (source/sound/game_sound.c) */
long unattached_impulse_sound_new_gain(long definition_index, struct sound_location const *location, real scale, real gain)
{
	(void)definition_index;
	(void)location;
	(void)scale;
	(void)gain;
	return 0;
}

/* (port/linux/game/tag_validate.c) */
void tag_validate_reserve_after_tags(unsigned long bytes)
{
	(void)bytes;
}

/* (port/linux/game/tag_validate.c) */
void tag_validate_reserve_after_bsps(unsigned long offset)
{
	(void)offset;
}

/* (port/linux/game/tag_validate.c) */
boolean tag_validate_pack_tags(void *tag_header, long size, unsigned long file_offset, unsigned long file_size, char const *name, long *corrections)
{
	(void)tag_header;
	(void)size;
	(void)file_offset;
	(void)file_size;
	(void)name;
	(void)corrections;
	return 0;
}

/* (port/linux/game/tag_validate.c) */
void tag_validate_extra_buffers_clear(void)
{
}

/* (port/linux/game/tag_validate.c) */
boolean tag_validate_extra_buffers_add(void *vertex_buffers, long vertex_buffer_count, void *index_buffers, long index_buffer_count)
{
	(void)vertex_buffers;
	(void)vertex_buffer_count;
	(void)index_buffers;
	(void)index_buffer_count;
	return 0;
}

/* (port/linux/game/scene_pack_ce.c) */
unsigned char* scene_pack_ce_tags(struct scene_pack_ce *ce, unsigned long *size)
{
	(void)ce;
	(void)size;
	return 0;
}

/* (port/linux/game/scene_pack_ce.c) */
boolean scene_pack_ce_structure_bsp(struct scene_pack_ce *ce, unsigned long file_offset, unsigned long size, unsigned long address, unsigned char *bytes, char *why, long why_size)
{
	(void)ce;
	(void)file_offset;
	(void)size;
	(void)address;
	(void)bytes;
	(void)why;
	(void)why_size;
	return 0;
}

/* (port/linux/game/scene_pack_ce.c) */
boolean scene_pack_ce_permutation_write(struct scene_pack_ce *ce, void *permutation_bytes, void const *sound_bytes, FILE *data, unsigned long *data_size, unsigned long *data_crc, char *why, long why_size)
{
	(void)ce;
	(void)permutation_bytes;
	(void)sound_bytes;
	(void)data;
	(void)data_size;
	(void)data_crc;
	(void)why;
	(void)why_size;
	return 0;
}

/* (port/linux/game/scene_pack_ce.c) */
boolean scene_pack_ce_part_buffers(struct scene_pack_ce *ce, unsigned long part_address, unsigned char **vertices, unsigned long *vertex_bytes, unsigned char **indices, unsigned long *index_bytes, char *why, long why_size)
{
	(void)ce;
	(void)part_address;
	(void)vertices;
	(void)vertex_bytes;
	(void)indices;
	(void)index_bytes;
	(void)why;
	(void)why_size;
	return 0;
}

/* (port/linux/game/scene_pack_ce.c) */
boolean scene_pack_ce_multiplayer_unit(char const *maps_directory, char const *map_name, struct scene_pack_ce_unit *unit, char *why, long why_size)
{
	(void)maps_directory;
	(void)map_name;
	(void)unit;
	(void)why;
	(void)why_size;
	return 0;
}

/* (port/linux/game/scene_pack_ce.c) */
boolean scene_pack_ce_model_convert(struct scene_pack_ce *ce, unsigned long model_address, long source_index, char *why, long why_size)
{
	(void)ce;
	(void)model_address;
	(void)source_index;
	(void)why;
	(void)why_size;
	return 0;
}

/* (port/linux/game/scene_pack_ce.c) */
void scene_pack_ce_channels(struct scene_pack_ce *ce, unsigned long const *groups, unsigned long const *roots, long count)
{
	(void)ce;
	(void)groups;
	(void)roots;
	(void)count;
}

/* (port/linux/game/scene_pack_ce.c) */
boolean scene_pack_ce_bsp_material_vertices(struct scene_pack_ce *ce, unsigned char const *material, unsigned char **vertices, unsigned long *vertex_bytes, unsigned char **lightmap_vertices, unsigned long *lightmap_vertex_bytes, char *why, long why_size)
{
	(void)ce;
	(void)material;
	(void)vertices;
	(void)vertex_bytes;
	(void)lightmap_vertices;
	(void)lightmap_vertex_bytes;
	(void)why;
	(void)why_size;
	return 0;
}

/* (port/linux/game/scene_pack_ce.c) */
boolean scene_pack_ce_bitmap_write(struct scene_pack_ce *ce, void *bitmap_bytes, long source_index, void const *group_root, FILE *data, unsigned long *data_size, unsigned long *data_crc, char *why, long why_size)
{
	(void)ce;
	(void)bitmap_bytes;
	(void)source_index;
	(void)group_root;
	(void)data;
	(void)data_size;
	(void)data_crc;
	(void)why;
	(void)why_size;
	return 0;
}

/* (port/linux/game/render_interpolation.c) */
unsigned long render_interpolation_frame_number(void)
{
	return 0;
}

/* (port/linux/game/ragdolls.c) */
boolean ragdolls_object_ragdolled(long object_index)
{
	(void)object_index;
	return 0;
}

/* (port/linux/game/ragdolls.c) */
boolean ragdolls_object_awake(long object_index)
{
	(void)object_index;
	return 0;
}

/* (source/items/projectiles.c) */
void projectile_attach_at(long projectile_index, long object_index, short node_index, union real_point3d const *position)
{
	(void)projectile_index;
	(void)object_index;
	(void)node_index;
	(void)position;
}

/* (port/linux/game/player_looks.c) */
boolean player_looks_command(char const *arguments)
{
	(void)arguments;
	return 0;
}

/* (port/linux/game/player_cosmetics.c) */
short player_cosmetics_icon_color(short icon_index)
{
	(void)icon_index;
	return 0;
}

/* (source/game/player_control.c) */
boolean player_control_action_test_vision_trigger(void)
{
	return 0;
}

/* (source/game/player_control.c) */
boolean player_control_action_test_melee(void)
{
	return 0;
}

/* (port/linux/src/xbox_files.c) */
int platform_file_foreground_busy(void)
{
	return 0;
}

/* (port/linux/src/xbox_files.c) */
void platform_file_card_hold(unsigned long long bytes, int write)
{
	(void)bytes;
	(void)write;
}

/* (port/linux/src/xbox_memory.c) */
void platform_contiguous_side_mark(unsigned long address, unsigned long size, unsigned long serial)
{
	(void)address;
	(void)size;
	(void)serial;
}

/* (port/linux/src/xbox_memory.c) */
void* platform_contiguous_grow_down(void *address, unsigned long size)
{
	(void)address;
	(void)size;
	return 0;
}

/* (port/linux/src/xbox_memory.c) */
void platform_contiguous_census(const char *when)
{
	(void)when;
}

/* (port/linux/src/xbox_memory.c) */
unsigned long platform_contiguous_block_size(const void *address)
{
	(void)address;
	return 0;
}

/* (source/physics/physics.c) */
void physics_update_with_forces(long object_index, struct mass_point_datum *mass_points, unsigned long passive_mass_points, unsigned long touching_mass_points, real hull_friction, union real_vector3d const *force, union real_vector3d const *torque)
{
	(void)object_index;
	(void)mass_points;
	(void)passive_mass_points;
	(void)touching_mass_points;
	(void)hull_friction;
	(void)force;
	(void)torque;
}

/* (source/physics/physics.c) */
void physics_hull_state(long object_index, struct mass_point_datum *mass_points, unsigned long passive_mass_points, unsigned long touching_mass_points, union real_vector3d const *last_velocity, union real_vector3d const *last_angular_velocity, union real_vector3d *force, union real_vector3d *torque)
{
	(void)object_index;
	(void)mass_points;
	(void)passive_mass_points;
	(void)touching_mass_points;
	(void)last_velocity;
	(void)last_angular_velocity;
	(void)force;
	(void)torque;
}

/* (source/objects/objects.c) */
byte object_region_permutation_get(long object_index, short region_index)
{
	(void)object_index;
	(void)region_index;
	return 0;
}

/* (source/sound/game_sound.c) */
long object_impulse_sound_new_gain(long object_index, long definition_index, short node_index, union real_point3d const *position, union real_vector3d const *forward, real scale, real gain)
{
	(void)object_index;
	(void)definition_index;
	(void)node_index;
	(void)position;
	(void)forward;
	(void)scale;
	(void)gain;
	return 0;
}

/* (port/linux/game/network_coop.c) */
void network_coop_names_log(char const *who)
{
	(void)who;
}

/* (port/linux/game/menu_scene_objects.c) */
void menu_scene_spartan_hide(void)
{
}

/* (port/linux/game/menu_scene_objects.c) */
void menu_scene_spartan_color(short color_index)
{
	(void)color_index;
}

/* (port/linux/game/menu_scene_objects.c) */
boolean menu_scene_objects_frozen(long object_index, real *suspension)
{
	(void)object_index;
	(void)suspension;
	return 0;
}

/* (source/main/main.c) */
boolean main_checkpoint_pending(void)
{
	return 0;
}

/* (source/interface/hud_nav_points.c) */
void hud_port_deactivate_nav_point_with_position(long position_index)
{
	(void)position_index;
}

/* (source/interface/hud_nav_points.c) */
void hud_port_activate_nav_point_with_position(short nav_index, long position_index, float vertical_offset)
{
	(void)nav_index;
	(void)position_index;
	(void)vertical_offset;
}

/* (source/hs/hs_runtime.c) */
boolean hs_runtime_thread_waits_on_command_script(long thread_index)
{
	(void)thread_index;
	return 0;
}

/* (source/hs/hs_runtime.c) */
void hs_runtime_thread_sleep_command_script(long thread_index)
{
	(void)thread_index;
}

/* (source/hs/hs_runtime.c) */
void hs_runtime_thread_sleep(long thread_index, long ticks)
{
	(void)thread_index;
	(void)ticks;
}

/* (source/hs/hs_runtime.c) */
short hs_runtime_thread_run_if_due(long thread_index)
{
	(void)thread_index;
	return 0;
}

/* (source/hs/hs_runtime.c) */
void* hs_runtime_stack_allocate(long thread_index, long size)
{
	(void)thread_index;
	(void)size;
	return 0;
}

/* (source/hs/hs_runtime.c) */
void hs_runtime_sleep_forever(long thread_index, short script_index)
{
	(void)thread_index;
	(void)script_index;
}

/* (source/hs/hs_runtime.c) */
long hs_runtime_sleep_all_script_threads(void)
{
	return 0;
}

/* (source/hs/hs_runtime.c) */
void hs_runtime_frame_rewind(long thread_index)
{
	(void)thread_index;
}

/* (source/hs/hs_runtime.c) */
void hs_runtime_end_script(char const *name)
{
	(void)name;
}

/* (source/hs/hs_runtime.c) */
long hs_runtime_end_all_threads(void)
{
	return 0;
}

/* (source/hs/hs_runtime.c) */
short hs_runtime_call_type(long thread_index)
{
	(void)thread_index;
	return 0;
}

/* (source/hs/hs.c) */
boolean hs_function_allowed_in_map_scripts(short function_index)
{
	(void)function_index;
	return 0;
}

/* (port/linux/src/xbox_memory.c) */
void halo_world_geometry_release(void *address)
{
	(void)address;
}

/* (port/linux/src/d3d8_gl.c) */
float halo_ui_pixel_scale(void)
{
	return 0.0f;
}

/* (port/linux/game/tick_thread.c) */
void halo_tick_wake_game_for_futures(void)
{
}

/* (port/linux/src/stall_trace.c) */
int halo_stall_trace_on(void)
{
	return 0;
}

/* (port/linux/src/stall_trace.c) */
int halo_stall_trace_enabled;

/* (port/linux/src/menu_files.c) */
unsigned char* halo_packs_file_read(char const *path, unsigned long *size)
{
	(void)path;
	(void)size;
	return 0;
}

/* (port/linux/src/d3d8_resources.c) */
BOOL halo_d3d_index_buffer_place(void *buffer, void *indices)
{
	(void)buffer;
	(void)indices;
	return 0;
}

/* (tools/map_validate.c) */
void* halo_custom_edition_memory_alloc(unsigned long bytes)
{
	(void)bytes;
	return 0;
}

/* (source/main/main.c) */
void h2_range_inject(char const *command, long delay)
{
	(void)command;
	(void)delay;
}

/* (port/linux/game/h2_menu.c) */
long h2_menu_sound_names(char const **names, long maximum)
{
	(void)names;
	(void)maximum;
	return 0;
}

/* (port/linux/game/h2_menu.c) */
boolean h2_menu_scene(void)
{
	return 0;
}

/* (port/linux/game/h2_menu.c) */
boolean h2_menu_level_textures_release(void)
{
	return 0;
}

/* (port/linux/game/h2_menu.c) */
boolean h2_menu_hud_text(short custom_font, char const *text, real x0, real x1, real bottom, short justification, real red, real green, real blue, real alpha, real scale)
{
	(void)custom_font;
	(void)text;
	(void)x0;
	(void)x1;
	(void)bottom;
	(void)justification;
	(void)red;
	(void)green;
	(void)blue;
	(void)alpha;
	(void)scale;
	return 0;
}

/* (port/linux/game/h2_jolt_hull.c) */
boolean h2_jolt_hull_touching(long vehicle_index)
{
	(void)vehicle_index;
	return 0;
}

/* (port/linux/game/h2_jolt_hull.c) */
boolean h2_jolt_hull_of(long object_index)
{
	(void)object_index;
	return 0;
}

/* (port/linux/game/h2_jolt_hull.c) */
void h2_jolt_hull_kick(long vehicle_index, union real_vector3d const *acceleration, union real_vector3d const *turning)
{
	(void)vehicle_index;
	(void)acceleration;
	(void)turning;
}

/* (port/linux/game/h2_jolt_hull.c) */
void h2_jolt_hull_center(long vehicle_index, union real_point3d *center)
{
	(void)vehicle_index;
	(void)center;
}

/* (port/linux/game/h2_jolt_hull.c) */
boolean h2_jolt_hull_begin(long vehicle_index, struct h2vp_definition const *d, struct physics_definition const *physics)
{
	(void)vehicle_index;
	(void)d;
	(void)physics;
	return 0;
}

/* (source/units/units.c) */
real h2_arbiter_camo_energy(long player_index)
{
	(void)player_index;
	return 0.0f;
}

/* (source/sound/game_sound.c) */
long game_sound_background_looping_sound_index(void)
{
	return 0;
}

/* (source/sound/game_sound.c) */
short game_looping_sound_state(long looping_sound_index)
{
	(void)looping_sound_index;
	return 0;
}

/* (source/game/game.c) */
real game_loading_screen_h2_origin(void)
{
	return 0.0f;
}

/* (source/game/game.c) */
boolean game_loading_screen_h2_begin(char const *map_name)
{
	(void)map_name;
	return 0;
}

/* (source/game/game.c) */
void game_loading_screen_end(void)
{
}

/* (source/game/game_engine.c) */
long game_engine_time_left_seconds(void)
{
	return 0;
}

/* (source/game/game_engine.c) */
long game_engine_random_item(long item_collection_index)
{
	(void)item_collection_index;
	return 0;
}

/* (source/game/game_engine_multiplayer_sounds.c) */
void game_engine_play_h2_announcement(char const *h2_name)
{
	(void)h2_name;
}

/* (source/game/game_engine.c) */
long game_engine_in_game_ranking(long *player_indices, long *places, boolean *tied, long maximum)
{
	(void)player_indices;
	(void)places;
	(void)tied;
	(void)maximum;
	return 0;
}

/* (port/linux/src/fsb_celt.c) */
int fsb_celt_decoder_start(void *memory, size_t bytes, int channels)
{
	(void)memory;
	(void)bytes;
	(void)channels;
	return 0;
}

/* (port/linux/src/fsb_celt.c) */
int fsb_celt_decode(void *decoder, uint8_t const *payload, uint32_t payload_bytes, int16_t *pcm)
{
	(void)decoder;
	(void)payload;
	(void)payload_bytes;
	(void)pcm;
	return 0;
}

/* (port/linux/game/field_of_view.c) */
boolean field_of_view_level_aspect_enabled(void)
{
	return 0;
}

/* (port/linux/game/field_of_view.c) */
short field_of_view_h3_reticle_pixels(short local_player_index, short half_height)
{
	(void)local_player_index;
	(void)half_height;
	return 0;
}

/* (port/linux/game/custom_edition_sounds.c) */
void custom_edition_sounds_stop(void)
{
}

/* (port/linux/game/custom_edition_sounds.c) */
void custom_edition_sounds_h2_loaded(void)
{
}

/* (port/linux/game/custom_edition_maps.c) */
char const* custom_edition_maps_level_title(char const *level_name)
{
	(void)level_name;
	return 0;
}

/* (port/linux/game/custom_edition_maps.c) */
boolean custom_edition_maps_h2_level(char const *level_name)
{
	(void)level_name;
	return 0;
}

/* (port/linux/game/custom_edition_cache.c) */
void* custom_edition_cache_tag_cache(unsigned long *size)
{
	(void)size;
	return 0;
}

/* (port/linux/src/port_config.c) */
int config_write_integer(const char *name, long value)
{
	(void)name;
	(void)value;
	return 0;
}

/* (source/cutscene/cinematics.c) */
void cinematic_show_letterbox_immediate(boolean show)
{
	(void)show;
}

/* (source/cache/cache_files_windows.c) */
int cache_files_reads_pending(void)
{
	return 0;
}

/* (source/cache/cache_files_windows.c) */
boolean cache_files_precache_copy_progress(char *map_name, long map_name_size, real *progress)
{
	(void)map_name;
	(void)map_name_size;
	(void)progress;
	return 0;
}

/* (source/cache/cache_files.c) */
boolean cache_file_is_halo2_level(void)
{
	return 0;
}

/* (source/ai/ai.c) */
boolean ai_globals_ai_active_get(void)
{
	return 0;
}

