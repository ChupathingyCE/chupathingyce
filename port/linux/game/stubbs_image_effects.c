/*
STUBBS_IMAGE_EFFECTS.C

Stubbs mode's image effects (stubbs_player.h): what the scripts'
image_effect_* functions start and end, and the one a unit's HUD names (the
zombie's colour while you play as Stubbs), drawn over the 3D view. Called
only in Stubbs mode.

An image effect (imef, 0x1AC bytes; its layout from Sigmmma's reclaimer,
GPL-3.0, stubbs/defs/imef.py, the meanings from the maps' own values) grew
out of Halo's cinematic screen effect, so it is drawn with Halo's: its
desaturation and tint by rasterizer_screen_effect, then each of its stages
(a bitmap laid over the screen: a vignette, film noise and streaks, a TV's
lines) as a quad of its own through the same Direct3D 8 device, which both
renderers draw (OpenGL and bgfx). What each field does is read from the
values the effects hold, not from the retail game's code:
- 0x24: a colour that multiplies the screen (zombie_color's 0.9 grey;
  technicolor's warm one, taken at a third: its mode, 0x3C = 1, reads as a
  grade rather than a filter);
- 0x50: a pair of weights that sum to 1, the picture's and its grey's, and
  the grey's tint: Halo's desaturation (movietone: all grey, tinted blue);
  a negative grey weight (a saturation boost) is left out; a second pair
  at 0x94 boosting the colour back (zombie_color's 2 and -1) leaves the
  picture's weight times it;
- 0x1A0: the stages, each 0xA4 bytes: a type at 0x20 (0: a multiplied
  picture such as projector_highlight's vignette, stretched over the view;
  4: one laid over at twice, a noise, streak or TV lines, centered on grey
  and tiled at its texels to the game's pixels), 2 at 0x24 read as the
  bitmap inverted (head's blob), its strength at 0x28, its scale (0x2C,
  0x30) and offset (0x34, 0x38), the bitmap
  at 0x3C and its u, v animations at 0x6C and 0x7C (Halo's periodic
  functions: cosine, noise, spark). Types 1 (a frame's jitter, no bitmap)
  and 3 (the hand's warp) are not drawn yet.

The state is the scripts' and is drawn only (no game state): a new map
(stubbs_looks.c, as its tags load), or a revert past the effect's start,
clears it. A blend between two
effects draws both, the old one fading out.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "stubbs_player.h"

#ifndef HALO_CONSOLE_NO_STUBBS /* (the header's empty ones on a console) */

#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmap_color_conversion.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "game/players.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_cinematics.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"
#include <xtl.h>
#include "rasterizer/xbox/rasterizer_xbox.h"
#include "rasterizer/xbox/rasterizer_xbox_pixel_shader.h"
#include "stubbs_looks.h"
#include "stubbs_mode.h"

#include <math.h>
#include <string.h>

/* (cache_files.c) */
boolean tag_index_is_group(long tag_index, long group_tag);

/* ---------- constants */

#define IMAGE_EFFECT_TAG 'imef'

enum
{
	IMAGE_EFFECT_MULTIPLY_COLOR_OFFSET = 0x24,
	IMAGE_EFFECT_MODE_OFFSET = 0x3C,
	IMAGE_EFFECT_WEIGHTS_OFFSET = 0x50,
	IMAGE_EFFECT_GREY_TINT_OFFSET = 0x58,
	IMAGE_EFFECT_SECOND_WEIGHTS_OFFSET = 0x94,
	IMAGE_EFFECT_STAGES_OFFSET = 0x1A0,

	IMAGE_EFFECT_STAGE_SIZE = 0xA4,
	STAGE_TYPE_OFFSET = 0x20,
	STAGE_MODE_OFFSET = 0x24,
	STAGE_INTENSITY_OFFSET = 0x28,
	STAGE_SCALE_OFFSET = 0x2C,
	STAGE_OFFSET_OFFSET = 0x34,
	STAGE_BITMAP_OFFSET = 0x3C,
	STAGE_U_ANIMATION_OFFSET = 0x6C,
	STAGE_V_ANIMATION_OFFSET = 0x7C,
	MAXIMUM_STAGES = 8,

	TAG_REFERENCE_INDEX_OFFSET = 0x0C,
	TAG_BLOCK_ADDRESS_OFFSET = 0x04,

	_image_effect_mode_grade = 1,

	_stage_type_multiply = 0,
	_stage_type_overlay = 4,
	_stage_mode_inverted = 2,

	/* Halo's periodic functions (an animation's function) */
	_function_one = 0,
	_function_zero,
	_function_cosine,
	_function_cosine_variable_period,
	_function_diagonal_wave,
	_function_diagonal_wave_variable_period,
	_function_slide,
	_function_slide_variable_period,

	/* the screen effect's vertex shader: texture coordinates from the
	quad's pixel (rasterizer_xbox_screen_effect.c) */
	_vertex_shader_screen_effect = 38,
	_vertex_type_screen = 8,
	_vertex_register_pixel = 4,
};

/* ---------- globals */

static struct
{
	long current;
	long previous;
	/* game seconds: when the current one started, and its blend's length */
	real start;
	real blend;
} image_effect = { NONE, NONE, 0.0f, 0.0f };

/* ---------- private code */

static real image_effect_time(
	void)
{
	return (real)game_time_get() / (real)TICKS_PER_SECOND;
}

static long image_effect_read32(
	byte const *address)
{
	int value;

	memcpy(&value, address, 4);
	return value;
}

static real image_effect_read_real(
	byte const *address)
{
	real value;

	memcpy(&value, address, 4);
	return value;
}

static short image_effect_read16(
	byte const *address)
{
	short value;

	memcpy(&value, address, 2);
	return value;
}

static long image_effect_find(
	char const *name)
{
	long index = name && *name ? tag_loaded(IMAGE_EFFECT_TAG, name) : NONE;

	if (name && *name && index == NONE)
		stubbs_mode_log("no image effect '%s' in this map", name);
	return index;
}

static byte const *image_effect_definition(
	long index)
{
	if (index == NONE || !tag_index_is_group(index, IMAGE_EFFECT_TAG))
		return NULL;
	return (byte const *)tag_get(IMAGE_EFFECT_TAG, index);
}

static struct bitmap_data const *image_effect_bitmap(
	byte const *reference)
{
	long index = image_effect_read32(reference + TAG_REFERENCE_INDEX_OFFSET);
	struct bitmap_group *group;

	if (index == NONE || !tag_index_is_group(index, BITMAP_GROUP_TAG))
		return NULL;
	group = bitmap_group_get(index);
	if (!group || group->bitmaps.count < 1)
		return NULL;
	return TAG_BLOCK_GET_ELEMENT(&group->bitmaps, 0, struct bitmap_data);
}

/* a hash of a whole number to [0, 1): the noise functions' values, which
never touch the game's random numbers */
static real image_effect_hash(
	unsigned int value)
{
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	value ^= value >> 16;
	return (real)(value & 0xFFFFFF) / (real)0x1000000;
}

/* an animation (source, function, period, phase, scale) at a time */
static real image_effect_animation(
	byte const *animation,
	real time,
	unsigned int salt)
{
	short function = image_effect_read16(animation + 2);
	real period = image_effect_read_real(animation + 4);
	real phase = image_effect_read_real(animation + 8);
	real scale = image_effect_read_real(animation + 12);
	real x;
	real value;

	if (period <= 0.0f)
		period = 1.0f;
	x = time / period + phase;
	switch (function)
	{
	case _function_one:
		value = 1.0f;
		break;
	case _function_zero:
		value = 0.0f;
		break;
	case _function_cosine:
	case _function_cosine_variable_period:
		value = 0.5f - 0.5f * (real)cos(x * 2.0f * 3.14159265f);
		break;
	case _function_slide:
	case _function_slide_variable_period:
	case _function_diagonal_wave:
	case _function_diagonal_wave_variable_period:
		value = x - (real)floor(x);
		break;
	default:
		/* noise, jitter, wander, spark: a new value each tick of the game
		(30 a second, as the Xbox drew) */
		value = image_effect_hash((unsigned int)(time * TICKS_PER_SECOND) * 2654435761U + salt);
		break;
	}
	return value * scale;
}

static void image_effect_quad(
	short width,
	short height)
{
	IDirect3DDevice8_Begin(global_d3d_device, D3DPT_TRIANGLEFAN);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, _vertex_register_pixel, 0, height);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, D3DVSDE_POSITION, -1.0f, -1.0f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, _vertex_register_pixel, width, height);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, D3DVSDE_POSITION, 1.0f, -1.0f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, _vertex_register_pixel, width, 0);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, D3DVSDE_POSITION, 1.0f, 1.0f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, _vertex_register_pixel, 0, 0);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, D3DVSDE_POSITION, -1.0f, 1.0f);
	IDirect3DDevice8_End(global_d3d_device);
}

/* the state every pass shares: no depth, colour only, blended, the screen
effect's vertex shader with texture coordinates 0 from its first two rows
(u and v: the pixel's x and y over the view times scale, plus offset);
FALSE if the view is empty */
static boolean image_effect_pass_begin(
	real u_scale,
	real v_scale,
	real u_offset,
	real v_offset,
	short *width,
	short *height)
{
	real constants[8][4];

	*width = global_window_parameters.camera.viewport_bounds.x1 - global_window_parameters.camera.viewport_bounds.x0;
	*height = global_window_parameters.camera.viewport_bounds.y1 - global_window_parameters.camera.viewport_bounds.y0;
	if (*width <= 0 || *height <= 0)
		return FALSE;
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_NONE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHATESTENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZWRITEENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZBIAS, 0);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHABLENDENABLE, TRUE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_BLENDOP, D3DBLENDOP_ADD);
	rasterizer_set_vertex_shader_permutation(_vertex_shader_screen_effect, _vertex_type_screen, 0);
	csmemset(constants, 0, sizeof(constants));
	constants[0][0] = u_scale / (real)*width;
	constants[0][3] = u_offset;
	constants[1][1] = v_scale / (real)*height;
	constants[1][3] = v_offset;
	IDirect3DDevice8_SetVertexShaderConstant(global_d3d_device, -81, constants, 8);
	return TRUE;
}

static unsigned long image_effect_pixel(
	real alpha,
	real red,
	real green,
	real blue)
{
	real_argb_color color;

	color.alpha = PIN(alpha, 0.0f, 1.0f);
	color.red = PIN(red, 0.0f, 1.0f);
	color.green = PIN(green, 0.0f, 1.0f);
	color.blue = PIN(blue, 0.0f, 1.0f);
	return real_argb_color_to_pixel32(&color);
}

/* r0 (inputs a b c d: a*b + c*d) out as the colour, alpha one */
static void image_effect_set_pixel_shader(
	unsigned long texture_modes,
	unsigned long constant,
	unsigned long inputs)
{
	struct pixel_shader_definition pixel_shader;

	csmemset(&pixel_shader, 0, sizeof(pixel_shader));
	pixel_shader.texture_modes = texture_modes;
	pixel_shader.combiner_count = PS_COMBINERCOUNT(1, 0);
	pixel_shader.constant_0[0] = constant;
	pixel_shader.rgb_inputs[0] = inputs;
	pixel_shader.rgb_outputs[0] = PS_COMBINEROUTPUTS(PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
	pixel_shader.final_combiner_inputs_abcd = PS_COMBINERINPUTS(PS_REGISTER_ZERO, PS_REGISTER_ZERO,
		PS_REGISTER_ZERO, PS_REGISTER_R0);
	pixel_shader.final_combiner_inputs_efg = PS_COMBINERINPUTS(PS_REGISTER_ZERO, PS_REGISTER_ZERO, PS_REGISTER_ONE, 0);
	rasterizer_set_pixel_shader(&pixel_shader);
}

/* the screen times a colour */
static void image_effect_multiply_color(
	real_rgb_color const *color)
{
	short width, height;

	if (!image_effect_pass_begin(0.0f, 0.0f, 0.0f, 0.0f, &width, &height))
		return;
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_DESTBLEND, D3DBLEND_ZERO);
	image_effect_set_pixel_shader(0,
		image_effect_pixel(1.0f, color->red, color->green, color->blue),
		PS_COMBINERINPUTS(PS_REGISTER_C0, PS_REGISTER_ONE, PS_REGISTER_ZERO, PS_REGISTER_ZERO));
	image_effect_quad(width, height);
}

/* a stage's bitmap over the screen: multiplied (the screen times the
bitmap, faded toward white by strength) or laid over (the screen times twice
the bitmap, faded toward grey: a grey bitmap leaves it as it was) */
static void image_effect_draw_stage(
	byte const *stage,
	real strength,
	real time,
	short stage_index)
{
	short type = (short)image_effect_read32(stage + STAGE_TYPE_OFFSET);
	/* (head's blob, dark in its middle, 2 at 0x24: read as its inverse, the
	view kept in the middle and dark around it) */
	boolean inverted = image_effect_read32(stage + STAGE_MODE_OFFSET) == _stage_mode_inverted;
	struct bitmap_data const *bitmap;
	real u_scale, v_scale, u_offset, v_offset;
	short width, height;

	if (type != _stage_type_multiply && type != _stage_type_overlay)
		return;
	strength *= image_effect_read_real(stage + STAGE_INTENSITY_OFFSET);
	if (strength <= 0.0f)
		return;
	bitmap = image_effect_bitmap(stage + STAGE_BITMAP_OFFSET);
	if (!bitmap)
		return;
	u_scale = image_effect_read_real(stage + STAGE_SCALE_OFFSET);
	v_scale = image_effect_read_real(stage + STAGE_SCALE_OFFSET + 4);
	u_offset = image_effect_read_real(stage + STAGE_OFFSET_OFFSET) +
		image_effect_animation(stage + STAGE_U_ANIMATION_OFFSET, time, 0x1000u * (stage_index + 1));
	v_offset = image_effect_read_real(stage + STAGE_OFFSET_OFFSET + 4) +
		image_effect_animation(stage + STAGE_V_ANIMATION_OFFSET, time, 0x2000u * (stage_index + 1) + 7);
	if (u_scale == 0.0f)
		u_scale = 1.0f;
	if (v_scale == 0.0f)
		v_scale = 1.0f;

	/* (a laid-over bitmap is tiled at its texels to the game's pixels times
	its scale: video_interlace's 4 texels at 0.5, two lines each; a
	multiplied one is stretched over the view) */
	if (type == _stage_type_overlay)
	{
		short view_width = global_window_parameters.camera.viewport_bounds.x1 -
			global_window_parameters.camera.viewport_bounds.x0;
		short view_height = global_window_parameters.camera.viewport_bounds.y1 -
			global_window_parameters.camera.viewport_bounds.y0;

		if (bitmap->width > 0 && bitmap->height > 0)
		{
			u_scale *= (real)view_width / (real)bitmap->width;
			v_scale *= (real)view_height / (real)bitmap->height;
		}
	}
	if (!image_effect_pass_begin(u_scale, v_scale, u_offset, v_offset, &width, &height))
		return;
	if (!rasterizer_set_texture_bitmap_data(0, bitmap))
		return;
	IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	IDirect3DDevice8_SetTextureStageState(global_d3d_device, 0, D3DTSS_MIPFILTER, D3DTEXF_POINT);
	/* r0 = t0 * strength + (1 or 1/2) * (1 - strength) */
	image_effect_set_pixel_shader(PS_TEXTUREMODES(PS_TEXTUREMODES_PROJECT2D, 0, 0, 0),
		image_effect_pixel(strength, 0.0f, 0.0f, 0.0f),
		PS_COMBINERINPUTS(
			inverted ? PS_REGISTER_T0 | PS_INPUTMAPPING_UNSIGNED_INVERT : PS_REGISTER_T0,
			PS_REGISTER_C0 | PS_CHANNEL_ALPHA,
			type == _stage_type_multiply ? PS_REGISTER_ONE : PS_REGISTER_ONE_HALF,
			PS_REGISTER_C0 | PS_CHANNEL_ALPHA | PS_INPUTMAPPING_UNSIGNED_INVERT));
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_DESTBLEND,
		type == _stage_type_multiply ? D3DBLEND_ZERO : D3DBLEND_SRCCOLOR);
	image_effect_quad(width, height);
}

/* an image effect over the view, at a strength from 0 (none) to 1 */
static void image_effect_draw(
	long index,
	real strength)
{
	byte const *definition = image_effect_definition(index);
	real grey_weight;
	real_rgb_color multiply;
	real time = image_effect_time();
	long stage_count;
	byte const *stages;
	short stage_index;

	if (!definition || strength <= 0.0f)
		return;
	strength = PIN(strength, 0.0f, 1.0f);

	/* the grey and its tint: Halo's desaturation */
	grey_weight = image_effect_read_real(definition + IMAGE_EFFECT_WEIGHTS_OFFSET + 4);
	/* (a second pair at 0x94 that boosts the colour back, zombie_color's 2
	and -1: the picture's weight times it is what colour is left) */
	{
		real picture_weight = image_effect_read_real(definition + IMAGE_EFFECT_WEIGHTS_OFFSET);
		real boost = image_effect_read_real(definition + IMAGE_EFFECT_SECOND_WEIGHTS_OFFSET);

		if (grey_weight > 0.0f && boost > 1.0f && boost <= 4.0f && picture_weight >= 0.0f)
			grey_weight = 1.0f - MIN(picture_weight * boost, 1.0f);
	}
	if (grey_weight > 0.0f)
	{
		struct rasterizer_cinematic_screen_effect_parameters parameters;

		csmemset(&parameters, 0, sizeof(parameters));
		parameters.filter_desaturation_intensity = PIN(grey_weight, 0.0f, 1.0f) * strength;
		memcpy(&parameters.filter_desaturation_tint, definition + IMAGE_EFFECT_GREY_TINT_OFFSET,
			sizeof(parameters.filter_desaturation_tint));
		rasterizer_screen_effect(&parameters);
	}

	/* the colour it multiplies by */
	memcpy(&multiply, definition + IMAGE_EFFECT_MULTIPLY_COLOR_OFFSET, sizeof(multiply));
	{
		real amount = strength;

		if (image_effect_read32(definition + IMAGE_EFFECT_MODE_OFFSET) == _image_effect_mode_grade)
			amount *= 1.0f / 3.0f;
		multiply.red = 1.0f + (multiply.red - 1.0f) * amount;
		multiply.green = 1.0f + (multiply.green - 1.0f) * amount;
		multiply.blue = 1.0f + (multiply.blue - 1.0f) * amount;
	}
	if (multiply.red < 0.999f || multiply.green < 0.999f || multiply.blue < 0.999f)
		image_effect_multiply_color(&multiply);

	/* its stages */
	stage_count = image_effect_read32(definition + IMAGE_EFFECT_STAGES_OFFSET);
	if (stage_count <= 0 || stage_count > MAXIMUM_STAGES)
		return;
	stages = (byte const *)xbox_pointer(
		(unsigned int)image_effect_read32(definition + IMAGE_EFFECT_STAGES_OFFSET + TAG_BLOCK_ADDRESS_OFFSET));
	if (!stages)
		return;
	for (stage_index = 0; stage_index < (short)stage_count; stage_index++)
		image_effect_draw_stage(stages + stage_index * IMAGE_EFFECT_STAGE_SIZE, strength, time, stage_index);
}

/* the image effect the unit a player controls shows: its HUD's (Stubbs's:
image_effects\zombie_color), outside cinematics */
static long image_effect_of_player_unit(
	short local_player_index)
{
	long player_index = local_player_get_player_index(local_player_index);
	long unit_index;

	if (player_index == NONE || cinematic_in_progress())
		return NONE;
	unit_index = player_get(player_index)->unit_index;
	if (unit_index == NONE)
		return NONE;
	return stubbs_looks_unit_image_effect(object_get(unit_index)->definition_index);
}

/* ---------- public code */

void stubbs_image_effect_set(
	char const *name)
{
	image_effect.previous = NONE;
	image_effect.current = image_effect_find(name);
	image_effect.start = image_effect_time();
	image_effect.blend = 0.0f;
}

void stubbs_image_effect_start_blend(
	char const *name,
	real seconds)
{
	long index = image_effect_find(name);

	if (index == image_effect.current)
		return;
	image_effect.previous = image_effect.current;
	image_effect.current = index;
	image_effect.start = image_effect_time();
	image_effect.blend = seconds > 0.0f ? seconds : 0.0f;
}

void stubbs_image_effect_end(
	void)
{
	image_effect.previous = NONE;
	image_effect.current = NONE;
	image_effect.start = image_effect_time();
	image_effect.blend = 0.0f;
}

void stubbs_image_effects_reset(
	void)
{
	image_effect.previous = NONE;
	image_effect.current = NONE;
	image_effect.start = 0.0f;
	image_effect.blend = 0.0f;
}

void stubbs_image_effects_render(
	short local_player_index)
{
	real time = image_effect_time();
	real fraction;

	/* (a new level, or a revert to before the effect began) */
	if (time < image_effect.start)
		stubbs_image_effect_end();
	/* (the view itself, not a mirror's or a texture's) */
	if (global_window_parameters.rasterizer_target != 0)
		return;
	fraction = image_effect.blend > 0.0f ? PIN((time - image_effect.start) / image_effect.blend, 0.0f, 1.0f) : 1.0f;
	if (image_effect.current == NONE && image_effect.previous == NONE)
	{
		image_effect_draw(image_effect_of_player_unit(local_player_index), 1.0f);
		return;
	}
	if (fraction < 1.0f)
		image_effect_draw(image_effect.previous, 1.0f - fraction);
	image_effect_draw(image_effect.current, fraction);
}

#endif
