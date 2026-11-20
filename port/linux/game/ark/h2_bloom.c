/*
H2_BLOOM.C

Halo 2's bloom on a Halo 2 level (PC; the Vita has no halo_bloom_pass).

The rule, read out of the embedded shaders and code of MCC's halo2.dll and the
H2EK's sapien.exe (their screen effects, rasterizer_dx11_porn_dof.cpp): every
frame, after the 3D and before the interface, whatever brightened past a
threshold is blurred and laid over the frame. Threshold 0.9
(rasterizer_bloom_default_threshold), brightness 0.5 x amount x (brightness + 1)
with the curve's 0.5 and 0.5: 0.375. The sky tag's BLOOM OVERRIDE (amount,
threshold, brightness) pulls both toward its own by its amount: zero on
Zanzibar, Coagulation, Lockout, one on Backwash, 0.6 on Warlock. The pass is
d3d8_gl.c's.

The sky's BLOOM OVERRIDE also carries a gamma power (retail 0x12efb0: each
sky with an amount over 0 weighs in its gamma power, 0 read as 1; 0x12a50:
the frame's gamma moves from 1 toward it by the amount, and goes into the
display's gamma ramp, pow(i / 255, gamma) - the whole frame). Street Sweeper's
and Turf's 1.1, Desolation's 1.1 at 0.25, Tombstone's 0.7 at 0.6; every other
multiplayer sky none. The H2EK's tag test draws it (Street Sweeper and Turf
were 1.08x at the brightest to 1.3x in the dark over its frames - x^-0.1).
Here over the 3D frame, after the bloom and the crossfade, before the
interface.

HALO_H2_BLOOM=0: no bloom; 2: the bloom target alone, over black (a debug view).
HALO_H2_SKY_GAMMA=0: no frame gamma (the look before).
*/

#include "cseries/cseries.h"
#include "math/real_math.h"
#include "memory/data.h"
#include "tag_files/tag_groups.h"
#include "scenario/scenario.h"
#include "scenario/sky_definitions.h"
#include "render/render.h"
#include "rasterizer/rasterizer_active_camouflage.h"
#include "main/main_runtime.h"

#include <stdlib.h>
#include <string.h>

#include "h2_bloom.h"
#ifdef HALO_LINUX
#include "render_epoch.h" /* HALO_ENV_ONCE */
#endif

boolean h2_cinematics_depth_of_field(float *separation, float *near_amount, float *far_amount);
float h2_cinematics_crossfade(void);

void (*halo_bloom_pass)(float threshold, float brightness);
void (*halo_depth_of_field_pass)(float separation, float near_amount, float far_amount, float z_near, float z_far);
void (*halo_crossfade_pass)(float opacity);
void (*halo_gamma_pass)(float gamma);

/* the scripts' override (rasterizer_bloom_override*): -1 unset; each value
set replaces the frame's, after the sky's and the curve (MCC halo2.dll
0x18096fbf0) */
static struct
{
	boolean on;
	float threshold, brightness;
} h2_bloom_override = { FALSE, -1.0f, -1.0f };

void h2_bloom_override_enable(boolean on)
{
	h2_bloom_override.on = on;
}

void h2_bloom_override_reset(void)
{
	h2_bloom_override.threshold = h2_bloom_override.brightness = -1.0f;
}

void h2_bloom_override_threshold(float value)
{
	h2_bloom_override.threshold = value;
}

void h2_bloom_override_brightness(float value)
{
	h2_bloom_override.brightness = value;
}

static float h2_bloom_saturate(float value)
{
	return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

void h2_bloom_render(void)
{
	static long scenario_index = NONE;
	static boolean level;
	char const *setting = HALO_ENV_ONCE("HALO_H2_BLOOM");
	char const *gamma_setting = HALO_ENV_ONCE("HALO_H2_SKY_GAMMA");
	float threshold = 0.9f, amount = 0.5f, brightness = 0.5f, gamma = 1.0f;
	struct sky *sky;
	unsigned char const *override;

	if (!halo_bloom_pass)
		return;
	if (scenario_index != global_scenario_index)
	{
		char const *name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;

		scenario_index = global_scenario_index;
		level = name && !strncmp(name, "h2\\", 3);
	}
	if (!level)
		return;
	/* (one window into the frame: not a mirror, nor split screens yet) */
	if (!rasterizer_distortion_available() || main_get_window_count() > 1)
		return;
	/* (a cinematic's depth of field, first: h2_cinematics.c) */
	{
		float separation, near_amount, far_amount;

		if (halo_depth_of_field_pass && h2_cinematics_depth_of_field(&separation, &near_amount, &far_amount))
			halo_depth_of_field_pass(separation, near_amount, far_amount, render.camera.z_near, render.camera.z_far);
	}
	/* (Halo 2's curve, at its only stop) */
	brightness = h2_bloom_saturate(0.5f * amount * (brightness + 1.0f));
	/* (the sky's override, in the built sky's padding: h2_level.c) */
	sky = scenario_get_sky(render.visible_sky_index != NONE ? render.visible_sky_index : 0);
	override = sky ? (unsigned char const *)sky + H2_BLOOM_SKY_OFFSET : NULL;
	if (override && !memcmp(override, "H2BL", 4))
	{
		float const *values = (float const *)(override + 4);

		if (values[0] > 0.0f)
		{
			float sky_gamma = values[3] != 0.0f ? values[3] : 1.0f;

			threshold += (values[1] - threshold) * values[0];
			brightness += (values[2] - brightness) * values[0];
			gamma += (sky_gamma - gamma) * h2_bloom_saturate(values[0]);
		}
	}
	if (h2_bloom_override.on)
	{
		if (h2_bloom_override.threshold != -1.0f)
			threshold = h2_bloom_override.threshold;
		if (h2_bloom_override.brightness != -1.0f)
			brightness = h2_bloom_override.brightness;
	}
	threshold = h2_bloom_saturate(threshold);
	brightness = h2_bloom_saturate(brightness);
	if (brightness > 0.0f && !(setting && !atoi(setting)))
		halo_bloom_pass(threshold, setting && atoi(setting) == 2 ? -brightness : brightness);
	/* (a cinematic's crossfade, last: the frame before it kept until it is
	asked for) */
	if (halo_crossfade_pass)
	{
		float opacity = h2_cinematics_crossfade();

		if (opacity >= 0.0f)
			halo_crossfade_pass(opacity);
	}
	/* (the frame gamma, last: the Xbox's display ramp) */
	if (halo_gamma_pass && gamma > 0.0f && gamma != 1.0f && !(gamma_setting && !atoi(gamma_setting)))
		halo_gamma_pass(gamma);
}
