/*
RENDER.C

symbols in this file:
001743B0 0020:
	_render_effects (0000)
001743D0 0010:
	_render_initialize (0000)
001743E0 0010:
	_render_initialize_for_new_map (0000)
001743F0 0010:
	_render_dispose_from_old_map (0000)
00174400 0010:
	_render_dispose (0000)
00174410 0110:
	_render_nonplayer_frame (0000)
00174520 00f0:
	_render_frame_pregame (0000)
00174610 0020:
	_render_frame_present (0000)
00174630 0070:
	_render_location_visible (0000)
001746A0 0050:
	_rendered_cluster_get (0000)
001746F0 03f0:
	_render_window (0000)
00174AE0 03f0:
	_render_player_frame (0000)
00174ED0 00f0:
	_render_frame (0000)
0029F44C 001f:
	??_C@_0BP@JNDFGHKA@c?3?2halo?2SOURCE?2render?2render?4c?$AA@ (0000)
0029F470 0061:
	??_C@_0GB@NHJDHKGO@location?9?$DOcluster_index?$DO?$DN0?5?$CG?$CG?5lo@ (0000)
0029F4D8 0052:
	??_C@_0FC@DLFICDJP@rendered_cluster_index?$DO?$DN0?5?$CG?$CG?5ren@ (0000)
0029F530 0052:
	??_C@_0FC@NBMIPGBN@window?9?$DOrender_camera?4viewport_b@ (0000)
0029F588 0051:
	??_C@_0FB@IDPOBHAJ@window?9?$DOrender_camera?4viewport_b@ (0000)
0029F5DC 002c:
	??_C@_0CM@LKHLGDKF@window?9?$DOrender_camera?4viewport_b@ (0000)
0029F608 002c:
	??_C@_0CM@HBCHLAAA@window?9?$DOrender_camera?4viewport_b@ (0000)
0029F638 006d:
	??_C@_0GN@PKELCNNH@?$CBmemcmp?$CI?$CGwindow?9?$DOrender_camera?4w@ (0000)
0029F6A8 0071:
	??_C@_0HB@BCLIBPGM@?$CBmemcmp?$CI?$CGwindow?9?$DOrender_camera?4v@ (0000)
0029F720 0048:
	??_C@_0EI@NGOLLMDP@?$CD?$CD?$CD?5ERROR?5something?5is?5wrong?5wit@ (0000)
0030D4D2 0004:
	_render_contrails_enabled (0000)
	_render_particles_enabled (0001)
	_render_particle_systems_enabled (0002)
	_render_weather_particle_systems_enabled (0003)
004B8B22 0001:
	_render_invalid_fog_warning_displayed (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "render.h"
#include "render_cameras_internal.h"
#include "render_particles.h"
#include "objects.h"
#include "render_sprite.h"
#include "scenario.h"
#include "structure_bsp_definitions.h"
#include "effects/player_effects.h"
#include "structures/structure_visibility.h"
#include "rasterizer.h"
#include "rasterizer/rasterizer_lights.h"
#include "profile.h"
#include "progress_bar.h"
#include "ui_widget.h"
#include "bink_playback.h"
#include "game.h"
#include "game_engine.h"
#include "interface/first_person_weapons.h"
#include "interface/interface.h"
#include "editor_stubs.h"
#include "render_debug.h"
#include "objects/object_lights_rendering.h"
#include "effects/particle_systems.h"
#include "effects/weather_particle_systems.h"
#include "main/main.h"
#include "structures/structures.h"
#include "halo_perf_log.h" /* port: console.perf_log */

/* ---------- constants */

enum
{
	_render_target_primary = 0,
	_render_target_secondary,
};

enum
{
	_decal_layer_primary = 0,
	_decal_layer_secondary,
	_decal_layer_light,
	_decal_layer_alpha_tested,
	_decal_layer_water,
};

enum
{
	_render_planar_fog_mode_fully_fogged = 2,
};

/* ---------- macros */

#ifndef HALO_XBOX_CONSOLE
#define RASTERIZER_TARGET_RENDER_PRIMARY_WIDTH halo_screen_width()
#else
#define RASTERIZER_TARGET_RENDER_PRIMARY_WIDTH 640
#endif
#define RASTERIZER_TARGET_RENDER_PRIMARY_HEIGHT 480

/* ---------- structures */

/* ---------- prototypes */

static void render_nonplayer_frame(
	const struct render_window *window,
	long window_type);
static void render_window(
	short local_player_index,
	const struct render_camera *camera,
	const struct render_frustum *frustum,
	const struct render_camera *rasterizer_camera,
	const struct render_frustum *rasterizer_frustum,
	short rasterizer_target,
	boolean has_mirror);
static void render_player_frame(
	struct render_window *window,
	const point2d *screenshot_combined_index);

void render_sky(
	void);

/* ---------- globals */

struct render_globals render;

static boolean render_invalid_fog_warning_displayed;

extern short global_screenshot_count;

boolean render_contrails_enabled = TRUE;
boolean render_particles_enabled = TRUE;
boolean render_particle_systems_enabled = TRUE;
boolean render_weather_particle_systems_enabled = TRUE;

/* ---------- public code */

void render_effects(
	boolean enable)
{
	render_weather_particle_systems_enabled = enable;
	render_particle_systems_enabled = enable;
	render_particles_enabled = enable;
	render_contrails_enabled = enable;
}

void render_initialize(
	void)
{
	render_objects_initialize();
}

void render_initialize_for_new_map(
	void)
{
	render_objects_initialize_for_new_map();
}

void render_dispose_from_old_map(
	void)
{
	render_objects_dispose_from_old_map();
}

void render_dispose(
	void)
{
	render_objects_dispose();
}

static void render_nonplayer_frame(
	const struct render_window *window,
	long window_type)
{
	struct rasterizer_window_begin_parameters parameters;

	profile_render_window_start(FALSE);
	memset(&parameters, 0, sizeof(parameters));

	render.camera = window->render_camera;
	render_camera_build_frustum(&render.camera, NULL, &render.frustum, TRUE);

	parameters.camera = window->rasterizer_camera;
	render_camera_build_frustum(&parameters.camera, NULL, &parameters.frustum, TRUE);

	parameters.rasterizer_target = _render_target_primary;
	parameters.suppress_clear = window_type == 0;
	parameters.window_index = NONE;
	rasterizer_window_begin(&parameters);

	switch (window_type)
	{
	case 0:
		/* letterbox bars and split screen dividers: laid out from the
		viewport, so not centered like the menus on a wide screen */
		interface_draw_fullscreen_overlays();
		rasterizer_debug_draw();
		break;

	case 1:
#ifndef HALO_XBOX_CONSOLE
		halo_screen_ui_offset(TRUE);
		game_engine_nonplayer_post_rasterize();
		halo_screen_ui_offset(FALSE);
#else
		game_engine_nonplayer_post_rasterize();
#endif
		break;

	default:
		match_assert("c:\\halo\\SOURCE\\render\\render.c", 287, !"unreachable");
		break;
	}

	rasterizer_window_end();
	profile_render_window_end();

	return;
}

void render_frame_pregame(
	struct render_window const *window,
	struct bitmap_data *bitmap)
{
	struct rasterizer_frame_begin_parameters parameters;
	struct rasterizer_window_begin_parameters rasterizer_parameters;

	render.frame_index++;

	rasterizer_frame_begin(&parameters);
	rasterizer_windows_begin();
	profile_render_window_start(FALSE);

	memset(&rasterizer_parameters, 0, sizeof(rasterizer_parameters));

	render.camera = window->render_camera;
	render_camera_build_frustum(&render.camera, NULL, &render.frustum, TRUE);

	rasterizer_parameters.camera = window->rasterizer_camera;
	render_camera_build_frustum(&rasterizer_parameters.camera, NULL, &rasterizer_parameters.frustum, TRUE);

	rasterizer_parameters.rasterizer_target = 0;
	rasterizer_window_begin(&rasterizer_parameters);

#ifndef HALO_XBOX_CONSOLE
	halo_screen_ui_offset(TRUE);
	render_ui_widgets(0, &window->rasterizer_camera.viewport_bounds);
	halo_screen_ui_offset(FALSE);
#else
	render_ui_widgets(0, &window->rasterizer_camera.viewport_bounds);
#endif
	bink_playback_render();

	{
		real progress;
		if (game_map_loading_in_progress(&progress))
		{
			progress_bar_display(progress);
		}
	}

	rasterizer_window_end();
	profile_render_window_end();
	rasterizer_windows_end();
	rasterizer_frame_end();

	return;
}

void render_frame_present(
	const point2d *screenshot_index,
	struct bitmap_data *bitmap)
{
	rasterizer_present(bitmap, screenshot_index);

	return;
}

boolean render_location_visible(
	struct location *location)
{
	match_assert("c:\\halo\\SOURCE\\render\\render.c", 584, location->cluster_index>=0 && location->cluster_index<global_structure_bsp_get()->clusters.count);
	return TEST_FLAG(render.visible_cluster_flags[location->cluster_index>>5], location->cluster_index&31);
}

struct rendered_cluster *rendered_cluster_get(
	short rendered_cluster_index)
{
	match_assert("c:\\halo\\SOURCE\\render\\render.c", 592, rendered_cluster_index>=0 && rendered_cluster_index<render.rendered_cluster_count);
	return &render.rendered_clusters[rendered_cluster_index];
}

#ifdef HALO_PS3
#include "ps3_timers.h"

/* port: the timer build: each split adds the time since the last one to a part of the window, and stamps the
RSX's time there, which times the RSX's part (ps3_timers_rsx_wait) */
static uint64_t ps3_timer_window;
/* port: and the game thread's Direct3D draws and constants since then (d3d8_rsx.c) to its share (ps3_render_d3d) */
static uint64_t ps3_timer_window_d3d;
#define PS3_TIMER_D3D() (ps3_timers[_ps3_timer_front_draws] + ps3_timers[_ps3_timer_front_constants])
#define PS3_TIMER_D3D_ADD(timer) ((void)(ps3_render_d3d[(timer)] += PS3_TIMER_D3D() - ps3_timer_window_d3d, \
	ps3_timer_window_d3d = PS3_TIMER_D3D()))
#define PS3_TIMER_WINDOW_BEGIN() \
	((void)(ps3_timer_window = ps3_timers_rsx_wait(_ps3_timer_rsx_before), ps3_timer_window_d3d = PS3_TIMER_D3D()))
#define PS3_TIMER_SPLIT(timer) ((void)ps3_timer_add((timer), ps3_timer_window), PS3_TIMER_D3D_ADD(timer), \
	ps3_timer_window = ps3_timers_rsx_wait(timer))
/* port: a part of a split, timed on the CPU alone (the RSX's stamp stays the split's) */
#define PS3_TIMER_PART(timer) ((void)(ps3_timer_window = ps3_timer_add((timer), ps3_timer_window)), \
	PS3_TIMER_D3D_ADD(timer))
/* port: the draws stamped on the RSX by pixel shader from here in a part (_ps3_stamp_*) or not (-1); the part of the
transparent groups made from here (0: their shader's type) */
#define PS3_TIMER_DRAW_STAMPS(part) ps3_timers_draw_stamps(part)
#define PS3_TRANSPARENT_ORIGIN(part) ((void)(ps3_transparent_origin = (part)))
/* port: the interface pass's share of the game thread's Direct3D draws and constants (d3d8_rsx.c's frame timers) */
static uint64_t ps3_interface_draws, ps3_interface_constants;
#define PS3_TIMER_INTERFACE_BEGIN() ((void)(ps3_interface_draws = ps3_timers[_ps3_timer_front_draws], \
	ps3_interface_constants = ps3_timers[_ps3_timer_front_constants]))
#define PS3_TIMER_INTERFACE_END() \
	((void)(ps3_timers[_ps3_timer_interface_draws] += ps3_timers[_ps3_timer_front_draws] - ps3_interface_draws, \
	ps3_timers[_ps3_timer_interface_constants] += ps3_timers[_ps3_timer_front_constants] - ps3_interface_constants))
#else
#define PS3_TIMER_WINDOW_BEGIN() ((void)0)
#define PS3_TIMER_SPLIT(timer) ((void)0)
#define PS3_TIMER_PART(timer) ((void)0)
#define PS3_TIMER_DRAW_STAMPS(part) ((void)0)
#define PS3_TRANSPARENT_ORIGIN(part) ((void)0)
#define PS3_TIMER_INTERFACE_BEGIN() ((void)0)
#define PS3_TIMER_INTERFACE_END() ((void)0)
#endif

#ifdef HALO_PS3
/* port: the main loop's place, for the PS3's heartbeat (main.c) */
extern volatile long ps3_main_loop_step;
#define PS3_MAIN_STEP(step) (ps3_main_loop_step = (step))
#else
#define PS3_MAIN_STEP(step)
#endif
/* port: render_window's place: 600 + n in the primary target's window, 700 + n in the mirror's */
#define PS3_WINDOW_STEP(n) PS3_MAIN_STEP((rasterizer_target == _render_target_primary ? 600 : 700) + (n))

static void render_window(
	short local_player_index,
	const struct render_camera *camera,
	const struct render_frustum *frustum,
	const struct render_camera *rasterizer_camera,
	const struct render_frustum *rasterizer_frustum,
	short rasterizer_target,
	boolean has_mirror)
{
	struct rasterizer_window_begin_parameters parameters;
	short rendered_cluster_index;

	/* port: PS3_* (the PS3's heartbeat and timer build) through the window's parts below */
	PS3_WINDOW_STEP(0);
	PS3_TIMER_WINDOW_BEGIN();
	profile_render_window_start(TRUE);
	render.scene_index++;
	memset(&parameters, 0, sizeof(parameters));

	render.local_player_index = local_player_index;
	render.camera = *camera;
	render.frustum = *frustum;
	parameters.camera = *rasterizer_camera;
	parameters.frustum = *rasterizer_frustum;
	parameters.rasterizer_target = rasterizer_target;
	parameters.has_mirror = has_mirror;
	parameters.window_index = render.window_index;
	parameters.fog = render.fog;

	HALO_PERF_RENDER_PART(_halo_perf_render_world);
	PS3_WINDOW_STEP(1);
	structure_visibility_compute();
	PS3_WINDOW_STEP(2);
	player_effect_get_screen_flash(local_player_index, &parameters.screen_flash);
	PS3_WINDOW_STEP(3);
	rasterizer_window_begin(&parameters);
	PS3_TIMER_SPLIT(_ps3_timer_window_visibility);

	if (!bink_playback_in_progress())
	{
		PS3_WINDOW_STEP(4);
		build_sprite_prepare_for_window();
#ifdef HALO_PS3
		/* port: the weather on an SPU while the window draws (weather_particle_systems_render takes it) */
		weather_particle_systems_render_begin();
		PS3_TIMER_PART(_ps3_timer_effects_weather);
#endif
		render_sky();
		PS3_TIMER_PART(_ps3_timer_render_sky);
		HALO_PERF_RENDER_PART(_halo_perf_render_objects);
		PS3_WINDOW_STEP(5);
		first_person_weapon_render_update();
		PS3_TIMER_PART(_ps3_timer_render_first_person);
		HALO_PERF_RENDER_PART(_halo_perf_render_world);
		PS3_WINDOW_STEP(6);
		lights_preprocess_scene();
		PS3_TIMER_PART(_ps3_timer_render_lights);
#ifdef HALO_PS3
		/* port: the dynamic lights' surfaces on the render SPU until the lights' passes (in the lights' part) */
		lights_render_spu_begin();
		PS3_TIMER_PART(_ps3_timer_lights_diffuse);
#endif
		HALO_PERF_RENDER_PART(_halo_perf_render_objects);
		PS3_WINDOW_STEP(7);
		render_objects();
		PS3_TIMER_SPLIT(_ps3_timer_window_objects);
		HALO_PERF_RENDER_PART(_halo_perf_render_world);
		PS3_WINDOW_STEP(8);
		structure_render_preprocess();
		PS3_WINDOW_STEP(9);
		structure_render_lightmaps();
		PS3_TIMER_SPLIT(_ps3_timer_window_lightmaps);
		HALO_PERF_RENDER_PART(_halo_perf_render_lens_flares);
		PS3_WINDOW_STEP(10);
		rasterizer_lens_flares_submit_occlusion_tests();
		PS3_TIMER_PART(_ps3_timer_lights_flares);
		HALO_PERF_RENDER_PART(_halo_perf_render_objects);
		PS3_WINDOW_STEP(11);
		render_object_shadows();
		PS3_TIMER_PART(_ps3_timer_lights_shadows);
		HALO_PERF_RENDER_PART(_halo_perf_render_world);
		PS3_WINDOW_STEP(12);
		lights_render_diffuse();
		PS3_TIMER_PART(_ps3_timer_lights_diffuse);

		PS3_WINDOW_STEP(13);
		rasterizer_decals_begin(_decal_layer_light);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();

		rasterizer_decals_begin(_decal_layer_alpha_tested);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();
		PS3_TIMER_PART(_ps3_timer_lights_decals);
		PS3_TIMER_SPLIT(_ps3_timer_window_lights);
#ifdef HALO_PS3
		/* port: the particles' sprites and the contrails' strips on the render SPU until the effects (in their parts) */
		render_particles_spu_begin();
		PS3_TIMER_PART(_ps3_timer_effects_particles);
		render_contrails_spu_begin();
		PS3_TIMER_PART(_ps3_timer_effects_contrails);
#endif

		PS3_WINDOW_STEP(14);
		structure_render_diffuse_texture();
		PS3_TIMER_PART(_ps3_timer_diffuse_structure);

		PS3_WINDOW_STEP(15);
		rasterizer_decals_begin(_decal_layer_primary);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();

		rasterizer_decals_begin(_decal_layer_secondary);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();
		PS3_TIMER_PART(_ps3_timer_diffuse_decals);
		PS3_TIMER_SPLIT(_ps3_timer_window_diffuse);

		PS3_TIMER_DRAW_STAMPS(_ps3_stamp_specular_lights);
		PS3_WINDOW_STEP(16);
		lights_render_specular();
		PS3_TIMER_SPLIT(_ps3_timer_specular_lights);
		PS3_WINDOW_STEP(17);
		structure_render_specular_lightmaps();
		PS3_TIMER_SPLIT(_ps3_timer_specular_lightmaps);
		PS3_WINDOW_STEP(18);
		structure_render_reflection_lightmap_masks();
		PS3_TIMER_SPLIT(_ps3_timer_specular_masks);
		PS3_WINDOW_STEP(19);
		structure_render_reflection_mirrors();
		PS3_TIMER_SPLIT(_ps3_timer_specular_mirrors);
		PS3_WINDOW_STEP(20);
		structure_render_reflections();
		PS3_TIMER_SPLIT(_ps3_timer_specular_reflections);
		HALO_PERF_RENDER_PART(_halo_perf_render_transparent);
		PS3_WINDOW_STEP(21);
		structure_render_transparent_geometry();
		PS3_TIMER_SPLIT(_ps3_timer_specular_transparent);
		HALO_PERF_RENDER_PART(_halo_perf_render_world);
		PS3_WINDOW_STEP(22);
		structure_render_fog();
		PS3_TIMER_SPLIT(_ps3_timer_specular_fog);
		PS3_TIMER_DRAW_STAMPS(_ps3_stamp_effects_rest);
		HALO_PERF_RENDER_PART(_halo_perf_render_objects);
		PS3_WINDOW_STEP(23);
		game_engine_post_rasterize_objects();
		HALO_PERF_RENDER_PART(_halo_perf_render_effects);
		PS3_WINDOW_STEP(24);
		PS3_TIMER_DRAW_STAMPS(_ps3_stamp_weather);
		weather_particle_systems_render();
		PS3_TIMER_SPLIT(_ps3_timer_effects_weather);
		PS3_TIMER_DRAW_STAMPS(_ps3_stamp_effects_rest);
		PS3_WINDOW_STEP(25);
		PS3_TRANSPARENT_ORIGIN(_ps3_stamp_particles);
		render_particles();
		PS3_TRANSPARENT_ORIGIN(_ps3_stamp_particle_systems);
		particle_systems_render();
		PS3_TRANSPARENT_ORIGIN(0);
		PS3_TIMER_SPLIT(_ps3_timer_effects_particles);
		PS3_WINDOW_STEP(26);
		PS3_TRANSPARENT_ORIGIN(_ps3_stamp_contrails);
		render_contrails_normal();
		PS3_TRANSPARENT_ORIGIN(0);
		PS3_TIMER_SPLIT(_ps3_timer_effects_contrails);
		HALO_PERF_RENDER_PART(_halo_perf_render_transparent);
		PS3_WINDOW_STEP(27);
		rasterizer_transparent_geometry_draw(TRUE);
		PS3_TIMER_SPLIT(_ps3_timer_effects_transparent);
		HALO_PERF_RENDER_PART(_halo_perf_render_world);

		PS3_WINDOW_STEP(28);
		PS3_TIMER_DRAW_STAMPS(_ps3_stamp_decals);
		rasterizer_decals_begin(_decal_layer_water);
		for (rendered_cluster_index = 0;
			rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			rasterizer_decals_draw(rendered_cluster_get(rendered_cluster_index)->cluster_index);
		}
		rasterizer_decals_end();
		PS3_TIMER_SPLIT(_ps3_timer_effects_decals);

		PS3_WINDOW_STEP(29);
		PS3_TIMER_DRAW_STAMPS(_ps3_stamp_detail);
		structure_render_detail_objects();
		PS3_TIMER_SPLIT(_ps3_timer_effects_detail);
		PS3_TIMER_DRAW_STAMPS(_ps3_stamp_effects_rest);
		HALO_PERF_RENDER_PART(_halo_perf_render_transparent);
		PS3_WINDOW_STEP(30);
		rasterizer_transparent_geometry_draw(FALSE);
		PS3_WINDOW_STEP(31);
		rasterizer_transparent_geometry_stop();
		PS3_TIMER_SPLIT(_ps3_timer_effects_transparent);
		PS3_TIMER_SPLIT(_ps3_timer_window_effects);
		PS3_TIMER_DRAW_STAMPS(-1);
		PS3_TIMER_INTERFACE_BEGIN();
		HALO_PERF_RENDER_PART(_halo_perf_render_world);
		PS3_WINDOW_STEP(32);
		structure_render_fog_screen();
		HALO_PERF_RENDER_PART(_halo_perf_render_lens_flares);
		rasterizer_lens_flares_draw();
		HALO_PERF_RENDER_PART(_halo_perf_render_world);
		/* port: the 3D view antialiased (display.anti_aliasing), before the
		HUD and menus are drawn over it */
		if (rasterizer_target == _render_target_primary)
		{
			halo_screen_anti_alias(
				rasterizer_camera->viewport_bounds.x0,
				rasterizer_camera->viewport_bounds.y0,
				rasterizer_camera->viewport_bounds.x1,
				rasterizer_camera->viewport_bounds.y1);
		}
		PS3_TIMER_PART(_ps3_timer_hud_fog_flares);
		HALO_PERF_RENDER_PART(_halo_perf_render_hud);
		PS3_WINDOW_STEP(33);
		interface_draw_screen();
		PS3_TIMER_PART(_ps3_timer_hud_screen);
		PS3_WINDOW_STEP(34);
		rasterizer_screen_flash();
		PS3_TIMER_PART(_ps3_timer_hud_flash);
		PS3_WINDOW_STEP(35);
#ifndef HALO_XBOX_CONSOLE
		halo_screen_ui_offset(TRUE);
		render_ui_widgets(local_player_index, &rasterizer_camera->viewport_bounds);
		halo_screen_ui_offset(FALSE);
#else
		render_ui_widgets(local_player_index, &rasterizer_camera->viewport_bounds);
#endif
		PS3_TIMER_PART(_ps3_timer_hud_widgets);
		PS3_TIMER_INTERFACE_END();
		PS3_TIMER_SPLIT(_ps3_timer_window_interface);
	}

	HALO_PERF_RENDER_PART(_halo_perf_render_world);
	PS3_WINDOW_STEP(36);
	bink_playback_render();
	render_camera_debug_frustum(&render.camera, &render.frustum);
	render_debug();
	editor_render();
	rasterizer_debug_draw();
	PS3_WINDOW_STEP(37);
	rasterizer_window_end();
	HALO_PERF_RENDER_PART(_halo_perf_render_setup);
	profile_render_window_end();
	PS3_TIMER_SPLIT(_ps3_timer_window_end);

	return;
}

static void render_player_frame(
	struct render_window *window,
	const point2d *screenshot_combined_index)
{
	struct render_camera *camera;
	boolean has_mirror;
	real_rectangle2d frustum_bounds;
	struct render_frustum frustum;
	struct render_frustum rasterizer_frustum;
	struct render_mirror mirror;

	camera = &window->render_camera;
	has_mirror = FALSE;

	HALO_PERF_RENDER_PART(_halo_perf_render_setup);
	PS3_MAIN_STEP(580); /* port: (the PS3's heartbeat, here and below) */
	structure_visibility_find_camera(camera);
	PS3_MAIN_STEP(581);
	render.fog.runtime_flags = 0;
	scenario_get_atmospheric_fog(
		window->local_player_index,
		(word)render.visible_sky_index,
		&camera->position,
		&render.fog);
	structure_get_planar_fog((short)render.cluster_index, &render.fog);

	if (render.fog.atmospheric_maximum_distance != 0.0f &&
		render.visible_sky_index == NONE &&
		render.fog.planar_maximum_distance > render.fog.atmospheric_maximum_distance)
	{
		render.fog.planar_maximum_distance = render.fog.atmospheric_maximum_distance;
	}

	if (render.fog.atmospheric_maximum_density == 1.0f &&
		render.fog.atmospheric_maximum_distance != 0.0f)
	{
		window->render_camera.z_far = MIN(
			window->render_camera.z_far,
			render.fog.atmospheric_maximum_distance);
	}

	if (render.fog.planar_mode == _render_planar_fog_mode_fully_fogged &&
		render.fog.planar_maximum_distance != 0.0f)
	{
		window->render_camera.z_far = MIN(
			window->render_camera.z_far,
			render.fog.planar_maximum_distance);
	}

	if (window->render_camera.z_far <= window->render_camera.z_near)
	{
		if (!render_invalid_fog_warning_displayed)
		{
			error(2, "### ERROR something is wrong with the fog in the sky tag or the fog tag");
			render_invalid_fog_warning_displayed = TRUE;
		}

		window->render_camera.z_far = window->render_camera.z_near + 0.01f;
	}

	/* port: a map whose sky or fog gives the camera a near plane that is not
	a number, or so far that adding to it changes nothing (a damaged or
	foreign map's), drew nothing the retail game could have shown and halted
	the debug build at render_camera_build_frustum: the camera takes the
	game's own near and far clip distances instead */
	if (!(window->render_camera.z_near >= 0.0f) ||
		!(window->render_camera.z_far > window->render_camera.z_near))
	{
		if (!render_invalid_fog_warning_displayed)
		{
			error(2, "### ERROR the camera's near and far planes are not numbers; drawn without the fog's");
			render_invalid_fog_warning_displayed = TRUE;
		}
		window->render_camera.z_near = rasterizer_globals.near_clip_distance;
		window->render_camera.z_far = rasterizer_globals.far_clip_distance;
		if (!(window->render_camera.z_near >= 0.0f) ||
			!(window->render_camera.z_far > window->render_camera.z_near))
		{
			/* (the game's own clip distances bad too: the Xbox game's) */
			window->render_camera.z_near = 0.0625f;
			window->render_camera.z_far = 1024.0f;
		}
	}

	/* port: and the rasterizer's camera, a copy of its own (render_frame
	builds its frustum too) with the same planes: its are the clip
	distances, as they are before the fog narrows the render camera's, and
	are not allowed to stay bad when the render camera's were */
	if (!(window->rasterizer_camera.z_near >= 0.0f) ||
		!(window->rasterizer_camera.z_far > window->rasterizer_camera.z_near))
	{
		window->rasterizer_camera.z_near = window->render_camera.z_near;
		window->rasterizer_camera.z_far = window->render_camera.z_far;
	}

	match_assert(
		"c:\\halo\\SOURCE\\render\\render.c",
		187,
		!memcmp(&window->render_camera.viewport_bounds,
			&window->rasterizer_camera.viewport_bounds,
			sizeof(rectangle2d)));
	match_assert(
		"c:\\halo\\SOURCE\\render\\render.c",
		188,
		!memcmp(&window->render_camera.window_bounds,
			&window->rasterizer_camera.window_bounds,
			sizeof(rectangle2d)));

	PS3_MAIN_STEP(582);
	render_camera_build_frustum_bounds(camera, &frustum_bounds);

	if (screenshot_combined_index != NULL)
	{
		long tile_count;

		tile_count = global_screenshot_count * global_screenshot_size;
		if (tile_count > 0)
		{
			real tile_width;
			real tile_height;
			real_rectangle2d adjusted_bounds;

			tile_width = (frustum_bounds.x1 - frustum_bounds.x0) / (real)tile_count;
			tile_height = (frustum_bounds.y1 - frustum_bounds.y0) / (real)tile_count;
			adjusted_bounds.x0 =
				(real)screenshot_combined_index->x * tile_width + frustum_bounds.x0;
			adjusted_bounds.y0 =
				(real)(tile_count - screenshot_combined_index->y - 1) * tile_height + frustum_bounds.y0;
			adjusted_bounds.x1 = adjusted_bounds.x0 + tile_width;
			adjusted_bounds.y1 = adjusted_bounds.y0 + tile_height;
			frustum_bounds = adjusted_bounds;
		}
	}

	render_camera_build_frustum(camera, &frustum_bounds, &frustum, TRUE);
	render_camera_build_frustum(
		&window->rasterizer_camera,
		&frustum_bounds,
		&rasterizer_frustum,
		TRUE);

	PS3_MAIN_STEP(583);
	if (main_get_window_count() == 1)
	{
		if (structure_visibility_find_mirror(camera, &frustum, &mirror))
		{
			short saved_cluster_index;
			struct render_camera mirror_camera;
			struct render_frustum mirror_frustum;

			saved_cluster_index = (short)render.cluster_index;
			match_assert("c:\\halo\\SOURCE\\render\\render.c", 225, window->render_camera.viewport_bounds.x0==0);
			match_assert("c:\\halo\\SOURCE\\render\\render.c", 226, window->render_camera.viewport_bounds.y0==0);
			match_assert("c:\\halo\\SOURCE\\render\\render.c", 227, window->render_camera.viewport_bounds.x1==RASTERIZER_TARGET_RENDER_PRIMARY_WIDTH);
			match_assert("c:\\halo\\SOURCE\\render\\render.c", 228, window->render_camera.viewport_bounds.y1==RASTERIZER_TARGET_RENDER_PRIMARY_HEIGHT);

			PS3_MAIN_STEP(584);
			render_camera_mirror(camera, &mirror, &mirror_camera);
			render_camera_build_frustum(
				&mirror_camera,
				&frustum_bounds,
				&mirror_frustum,
				TRUE);

			rasterizer_profile_enable(FALSE);
			render.cluster_index = mirror.cluster_index;
			render_window(
				NONE,
				&mirror_camera,
				&mirror_frustum,
				&mirror_camera,
				&mirror_frustum,
				_render_target_secondary,
				FALSE);
			render.cluster_index = saved_cluster_index;
			rasterizer_profile_enable(TRUE);
			has_mirror = TRUE;
		}
	}

	PS3_MAIN_STEP(585);
	render_window(
		window->local_player_index,
		camera,
		&frustum,
		&window->rasterizer_camera,
		&rasterizer_frustum,
		_render_target_primary,
		has_mirror);

	return;
}

void render_frame(
	struct render_window *windoze,
	short window_count,
	const point2d *screenshot_page_index,
	const point2d *screenshot_index,
	struct bitmap_data *screenshot_bitmap,
	real time_delta_since_tick_sec)
{
	struct rasterizer_frame_begin_parameters parameters;
	short window_index;
	struct render_window *window;
	point2d screenshot_combined_index;

	render.frame_index++;
	render.time_delta_since_tick_sec = time_delta_since_tick_sec;
	memset(&parameters, 0, sizeof(parameters));
	HALO_PERF_RENDER_PART(_halo_perf_render_begin);
#ifndef HALO_XBOX_CONSOLE
	/* continuous between ticks (render_interpolation.c) */
	parameters.game_time_sec = render_interpolation_game_time_sec(game_time_get());
#else
	parameters.game_time_sec = (real)game_time_get() * (1.0f / TICKS_PER_SECOND);
#endif
	PS3_MAIN_STEP(520); /* port: (the PS3's heartbeat, here and below) */
	rasterizer_frame_begin(&parameters);
	PS3_MAIN_STEP(521);
	rasterizer_windows_begin();
	PS3_MAIN_STEP(522);

	for (window_index = 0; window_index < window_count; window_index++)
	{
		long window_type;

		window = &windoze[window_index];
		render.window_index = window_index;
		if (window->console_window)
		{
			window_type = 0;
		}
		else if (window->local_player_index != NONE)
		{
			if (screenshot_index != NULL && screenshot_page_index != NULL)
			{
				screenshot_combined_index.x =
					screenshot_page_index->x * global_screenshot_size + screenshot_index->x;
				screenshot_combined_index.y =
					screenshot_page_index->y * global_screenshot_size + screenshot_index->y;
			}

			PS3_MAIN_STEP(523);
			render_player_frame(
				window,
				screenshot_index != NULL ? &screenshot_combined_index : NULL);
			continue;
		}
		else
		{
			window_type = 1;
		}

		HALO_PERF_RENDER_PART(_halo_perf_render_end);
		PS3_MAIN_STEP(524 + window_type);
		render_nonplayer_frame(window, window_type);
	}

	HALO_PERF_RENDER_PART(_halo_perf_render_end);

	PS3_MAIN_STEP(527);
#ifndef HALO_XBOX_CONSOLE
	halo_screen_ui_offset(TRUE);
	progress_bar_eachframe();
	halo_screen_ui_offset(FALSE);
#else
	progress_bar_eachframe();
#endif
	PS3_MAIN_STEP(528);
	rasterizer_windows_end();
	PS3_MAIN_STEP(529);
	rasterizer_frame_end();
	PS3_MAIN_STEP(530);
	HALO_PERF_RENDER_PART(NONE);

	return;
}

/* ---------- private code */
