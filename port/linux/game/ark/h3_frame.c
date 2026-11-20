/*
H3_FRAME.C

A Halo 3 level's frame as Halo 3 finishes it (the PC; the Vita and the null
build have no halo_h3_frame_pass and draw the level as before).

Halo 3 lights its surfaces in linear light, draws each through its
atmosphere (atmosphere.fx compute_scattering: the surface's light times the
extinction plus the inscatter), times the frame's exposure, into a target
that holds more than white; then, every frame (the kit's own shader text,
read for its rules: downsample_4x4_block_bloom, blur_11_horizontal /
vertical, add_downsampled, exposure_downsample, final_composite):

- the bloom: the frame a quarter its size, each texel's color times
  max(intensity x inherent bloom, intensity - bloom point) / intensity - so
  a share of everything glows and what is past the bloom point glows whole
  -, blurred (the 11-tap kernel 1 10 45 120 210 252 ... / 1024) at three
  sizes, each times its camera fx color (small, medium, large), times the
  bloom intensity, added to the frame;
- the tone curve: min(color, 1.4938) -> 1.0041 c - 0.15 c^3 (white at
  1.4938 of the exposed light, the shoulder under it);
- the exposure: the frame's mean log intensity (and the log of its mean:
  the camera fx's anti-bloom mixes them) moved toward the camera fx's
  "auto-exposure screen brightness", within its least and most stops.

This engine's frame on a Halo 3 level holds the square root of that light
(h3_level.c: its lightmap texel sqrt(light x exposure) / 2 through the
doubling texture pass), made at ONE exposure, the level's (exposure_auto),
with this engine's linear fog standing in for the atmosphere, clamped at
white, no bloom, no tone curve. Here the frame is drawn into a float target
(sun-lit surfaces keep their light past white: up to 4 x), and one pass
(d3d8_gl.c's h3_frame_pass) squares it back to light, draws the level's
atmosphere at each pixel from its depth, moves it from the bake's exposure
to the frame's, adds the bloom, applies the tone curve and encodes it for
the screen; the interface is drawn after it, as Halo 3's is.

The camera fx's values are each map's own (cfxs; h3_level.c reads them).
What is not in any tag or shader text and was measured on halo3_tag_test
(its exposure readout, frames with its bloom / tone curve / atmosphere off):
the display's encode and how far the exposure follows the meter - see
display_power and exposure_follow below.

HALO_H3_FRAME=0            the frame as before (this engine's fog, one exposure, no bloom, no tone curve)
HALO_H3_FRAME_ATMOSPHERE=0 this engine's fog instead of the pass's atmosphere
HALO_H3_FRAME_BLOOM=0      no bloom
HALO_H3_FRAME_TONE=0       no tone curve (the light clamped at white)
HALO_H3_FRAME_AUTO=0       the exposure stays the level's (the bake's)
HALO_H3_EXPOSURE_STOPS=n   the exposure fixed at n stops (a test: the tag test's frozen exposure)
HALO_H3_EXPOSURE_INSTANT=1 the exposure follows its meter at once (tests' still frames)
HALO_H3_FRAME_DEBUG=n      1 the atmosphere's share, 2 the bloom alone, 3 the frame's light before the pass, 4 depth
HALO_H3_FRAME_LOG=1        the exposure and the meter, once a second
*/

#include "cseries/cseries.h"
#include "math/real_math.h"
#include "memory/data.h"
#include "tag_files/tag_groups.h"
#include "scenario/scenario.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "rasterizer/rasterizer_active_camouflage.h"
#include "main/main_runtime.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "h3_frame.h"

void platform_log(char const *format, ...);
boolean h3_level_direct_loaded(void);

void (*halo_h3_frame_pass)(struct halo_h3_frame_parameters const *parameters, float measured[2]);
void (*halo_h3_frame_float)(int on);

static struct h3_frame_level frame_level;

static struct
{
	boolean read;
	boolean on, atmosphere, bloom, tone, automatic, instant, log;
	boolean stops_set;
	float stops_fixed;
	float display_power, exposure_follow, meter_center, bloom_scale;
	int debug;
	/* the atmosphere drawn now (the eye's cluster's setting, moved to) */
	int atmosphere_setting;
	boolean atmosphere_valid;
	struct halo_h3_frame_atmosphere atmosphere_now;
	double atmosphere_time;
	/* the running exposure */
	unsigned long generation;
	boolean started;
	float stops;
	double last_time;
	double log_time;
} frame_state;

static float setting_float(char const *name, float fallback)
{
	char const *text = getenv(name);

	return text && *text ? (float)atof(text) : fallback;
}

static boolean setting_on(char const *name, boolean fallback)
{
	char const *text = getenv(name);

	return text && *text ? atoi(text) != 0 : fallback;
}

static void frame_settings_read(void)
{
	if (frame_state.read)
		return;
	frame_state.read = TRUE;
	frame_state.on = setting_on("HALO_H3_FRAME", TRUE);
	frame_state.atmosphere = setting_on("HALO_H3_FRAME_ATMOSPHERE", TRUE);
	frame_state.bloom = setting_on("HALO_H3_FRAME_BLOOM", TRUE);
	frame_state.tone = setting_on("HALO_H3_FRAME_TONE", TRUE);
	frame_state.automatic = setting_on("HALO_H3_FRAME_AUTO", TRUE);
	frame_state.instant = setting_on("HALO_H3_EXPOSURE_INSTANT", FALSE);
	frame_state.log = setting_on("HALO_H3_FRAME_LOG", FALSE);
	frame_state.stops_set = getenv("HALO_H3_EXPOSURE_STOPS") != NULL;
	frame_state.stops_fixed = setting_float("HALO_H3_EXPOSURE_STOPS", 0.0f);
	/* (the display's encode, measured: the tag test's frame one stop apart is
	1.51 x at 0.05..0.15 of white, 1.43 at 0.15..0.3, 1.39 at 0.3..0.5, 1.37
	at 0.5..0.7 - the sRGB curve's 1.56 1.44 1.39 1.37, not a power's one
	ratio. 0: sRGB; a power otherwise - 0.5 the square root this engine's
	frame holds) */
	frame_state.display_power = setting_float("HALO_H3_DISPLAY_POWER", 0.0f);
	frame_state.exposure_follow = setting_float("HALO_H3_EXPOSURE_FOLLOW", 1.0f);
	frame_state.meter_center = setting_float("HALO_H3_METER_CENTER", 0.0f);
	frame_state.bloom_scale = setting_float("HALO_H3_BLOOM_SCALE", 1.0f);
	frame_state.debug = (int)setting_float("HALO_H3_FRAME_DEBUG", 0.0f);
}

struct h3_frame_level *h3_frame_level(void)
{
	return &frame_level;
}

int h3_frame_enabled(void)
{
	frame_settings_read();
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	return FALSE;
#else
	return frame_state.on;
#endif
}

int h3_frame_atmosphere_enabled(void)
{
	return h3_frame_enabled() && frame_state.atmosphere;
}

static double frame_seconds(void)
{
	return (double)system_milliseconds() * 0.001;
}

void h3_frame_render(void)
{
	static long scenario_index = NONE;
	static boolean level;
	struct halo_h3_frame_parameters parameters;
	struct h3_frame_level const *f = &frame_level;
	float measured[2] = { 0.0f, 0.0f }, bake_stops, least, most;
	boolean on;
	int i, k;

	if (!halo_h3_frame_pass || !halo_h3_frame_float)
		return;
	if (scenario_index != global_scenario_index)
	{
		char const *name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;

		scenario_index = global_scenario_index;
		level = name && !strncmp(name, "h3\\", 3);
	}
	on = level && h3_frame_enabled() && f->valid && h3_level_direct_loaded();
	halo_h3_frame_float(on);
	if (!on)
		return;
	/* (one window into the frame: not a mirror, nor split screens yet) */
	if (!rasterizer_distortion_available() || main_get_window_count() > 1 || !render.frustum.projection_valid)
		return;
	bake_stops = log2f(f->bake_exposure > 0.0f ? f->bake_exposure : 1.0f);
	least = f->exposure_minimum;
	most = f->exposure_maximum;
	if (frame_state.generation != halo_map_generation || !frame_state.started)
	{
		frame_state.generation = halo_map_generation;
		frame_state.started = TRUE;
		/* (the first frame's: the bake's, within the camera fx's range) */
		frame_state.stops = bake_stops < least ? least : bake_stops > most ? most : bake_stops;
		frame_state.last_time = frame_seconds();
		frame_state.atmosphere_setting = -1;
		frame_state.atmosphere_valid = FALSE;
		platform_log("h3 frame: on: the light made at %.2f stops, exposure %.2f..%.2f toward %.2f, atmosphere %s%s, bloom %s, tone curve %s, exposure %s",
			bake_stops, least, most, f->exposure_target, frame_state.atmosphere && f->level.on ? "on" : "off",
			f->sky_inscatter ? " (the sky its inscatter)" : "", frame_state.bloom ? "on" : "off", frame_state.tone ? "on" : "off",
			frame_state.stops_set ? "fixed" : frame_state.automatic ? "automatic" : "the level's");
	}
	if (frame_state.stops_set)
		frame_state.stops = frame_state.stops_fixed;
	else if (!frame_state.automatic)
		frame_state.stops = bake_stops;

	memset(&parameters, 0, sizeof(parameters));
	parameters.viewport[0] = render.camera.viewport_bounds.x0;
	parameters.viewport[1] = render.camera.viewport_bounds.y0;
	parameters.viewport[2] = render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0;
	parameters.viewport[3] = render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0;
	for (k = 0; k < 3; k++)
		parameters.camera[k] = render.camera.position.n[k];
	for (i = 0; i < 4; i++)
	{
		real_point2d screen;
		real_point3d origin;
		real_vector3d vector;

		/* (the screen's y runs down: render_camera_screen_to_view) */
		screen.n[0] = (i & 1) ? 1.0f : -1.0f;
		screen.n[1] = (i & 2) ? -1.0f : 1.0f;
		render_camera_screen_to_world(&render.camera, &render.frustum, &screen, &origin, &vector);
		for (k = 0; k < 3; k++)
			parameters.corner[i][k] = vector.n[k];
	}
	parameters.z_near = render.camera.z_near;
	parameters.z_far = render.camera.z_far;
	parameters.level = f->level;
	parameters.sky = f->sky;
	/* (the atmosphere of the cluster the eye is in, as Halo 3's frame is
	drawn through its camera's cluster's; moved to over a second when the
	eye changes cluster) */
	{
		int setting = render.cluster_index >= 0 && render.cluster_index < H3_FRAME_CLUSTERS ? f->cluster_setting[render.cluster_index] : -1;
		double now = frame_seconds();
		float share;

		if (setting >= 0 && setting < f->setting_count)
		{
			if (frame_state.atmosphere_setting != setting)
			{
				if (frame_state.log)
					platform_log("h3 frame: cluster %ld: atmosphere setting %d %s", (long)render.cluster_index, setting, f->setting_names[setting]);
				frame_state.atmosphere_setting = setting;
				if (!frame_state.atmosphere_valid)
					frame_state.atmosphere_now = f->settings[setting];
				frame_state.atmosphere_valid = TRUE;
			}
			share = (float)(now - frame_state.atmosphere_time) * 2.0f;
			frame_state.atmosphere_time = now;
			if (share > 1.0f || share < 0.0f || frame_state.instant)
				share = 1.0f;
			{
				float *from = (float *)&frame_state.atmosphere_now.sun[0];
				float const *to = (float const *)&f->settings[setting].sun[0];
				int n, count = (int)((sizeof(struct halo_h3_frame_atmosphere) - offsetof(struct halo_h3_frame_atmosphere, sun)) / sizeof(float));

				for (n = 0; n < count; n++)
					from[n] += (to[n] - from[n]) * share;
				frame_state.atmosphere_now.on = f->settings[setting].on;
			}
			parameters.level = frame_state.atmosphere_now;
		}
	}
	if (!frame_state.atmosphere)
		parameters.level.on = FALSE;
	parameters.sky_inscatter = frame_state.atmosphere && f->sky_inscatter && f->sky.on;
	parameters.bake_exposure = f->bake_exposure > 0.0f ? f->bake_exposure : 1.0f;
	parameters.sky_bake_exposure = f->sky_bake_exposure > 0.0f ? f->sky_bake_exposure : parameters.bake_exposure;
	parameters.exposure = exp2f(frame_state.stops);
	parameters.bloom_point = f->bloom_point;
	parameters.bloom_inherent = f->bloom_inherent;
	parameters.bloom_intensity = frame_state.bloom ? f->bloom_intensity * frame_state.bloom_scale : 0.0f;
	memcpy(parameters.bloom_colors, f->bloom_colors, sizeof(parameters.bloom_colors));
	/* (final_composite's defaults: white, linear, quadratic, cubic) */
	parameters.tone[0] = frame_state.tone ? 1.4938015821857215f : 1.0f;
	parameters.tone[1] = frame_state.tone ? 1.0041494251232542f : 1.0f;
	parameters.tone[2] = 0.0f;
	parameters.tone[3] = frame_state.tone ? -0.15f : 0.0f;
	parameters.display_power = frame_state.display_power;
	parameters.meter_center = frame_state.meter_center;
	parameters.debug = frame_state.debug;
	halo_h3_frame_pass(&parameters, measured);

	/* the exposure for the next frame: the meter's reading (anti-bloom:
	the log of the mean instead of the mean log) as the scene's own - less
	this frame's stops -, the stops that bring it to the target, followed
	as far as exposure_follow says, within the camera fx's range; moved
	there by the camera fx's blend speed an update (its delay apart), no
	more than its maximum change */
	{
		double now = frame_seconds();
		float reading = measured[0] + (measured[1] - measured[0]) * (f->anti_bloom < 0.0f ? 0.0f : f->anti_bloom > 1.0f ? 1.0f : f->anti_bloom);
		float scene = reading - frame_state.stops;
		float wanted = frame_state.stops + frame_state.exposure_follow * (f->exposure_target - scene - frame_state.stops);
		float elapsed = (float)(now - frame_state.last_time);

		frame_state.last_time = now;
		if (wanted < least)
			wanted = least;
		if (wanted > most)
			wanted = most;
		if (frame_state.automatic && !frame_state.stops_set && measured[1] > -15.0f)
		{
			if (frame_state.instant)
				frame_state.stops = wanted;
			else
			{
				float interval = f->exposure_delay > 0.001f ? f->exposure_delay : 1.0f / 30.0f;
				float updates = elapsed / interval, share, change;

				if (updates > 30.0f)
					updates = 30.0f;
				share = 1.0f - powf(1.0f - (f->exposure_blend_speed > 0.0f && f->exposure_blend_speed < 1.0f ? f->exposure_blend_speed : 0.05f), updates);
				change = (wanted - frame_state.stops) * share;
				if (f->exposure_maximum_change > 0.0f)
				{
					float limit = f->exposure_maximum_change * updates;

					if (change > limit)
						change = limit;
					if (change < -limit)
						change = -limit;
				}
				frame_state.stops += change;
			}
		}
		if (frame_state.log && now - frame_state.log_time >= 1.0)
		{
			frame_state.log_time = now;
			platform_log("h3 frame: exposure %.3f stops (wanted %.3f), meter mean log %.3f, log mean %.3f, scene %.3f", frame_state.stops, wanted,
				measured[0], measured[1], scene);
		}
	}
}
