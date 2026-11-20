/*
H2_PLASMA_SHIELD.C

port: Halo 2's plasma_shield pass read off a shader tag and written into
this engine's plasma shader's padding (h2_plasma_shield.h).

The shader's postprocess definition (0x20, 0x7C bytes): its bitmaps at 4
(0xC each), pixel constants at 0xC (ARGB), vertex constants at 0x14 (4
reals each: 0..2 noyze0's rows - the scale on the diagonal -, 3..5
noyze1's, 6 color, 7 glancing_color, 8 the offset in w), its overlays at
0x3C (0x14 each: the input's string id at 0, period at 8, the function's
data at 0x10, its size at 0xC), their references at 0x44 (component << 16 |
overlay: components 1..3 a map's scale rows, 4..6 its translation x y z,
0xFFFF a constant's), its animated parameters at 0x4C (2 bytes: the index
of the group's first overlay reference, a kind) and their references at 0x54
(a register at 0..1, the animated parameter at 3): a register 0x02xx or
0x06xx a noise's rows (the first group noyze0, the second noyze1), 0x00 0x82
+ k the k-th pixel constant, 0x1C 0x80 + n the vertex colour n (10 color,
11 glancing_color), 0x1C 0x70 + n the vertex value n (8 the offset) - read
off MCC shared.map's elite_plasma_shield, masterchief_plasma_shield_change_
color, over_shield and active_camouflage.

A function's data: type at 0 (1 a constant, 2 a transition by its input -
or by time when the input is none: 0 to 1 a period -, 3 periodic), flags at
1 (0x20 a colour function), then for a value low and high at 4 and 8, for a
colour four ARGB at 4..20 (the first and the last the ends).
*/

#include "cseries.h"
#include "h2_plasma_shield.h"
#include "h2_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define H2_ALIGN __attribute__((force_align_arg_pointer))

/* ---------- private prototypes */

H2_ALIGN static unsigned long argb_of_reals(struct h2_cache *cache, unsigned long address);
H2_ALIGN static float real_at(struct h2_cache *cache, unsigned long address);
H2_ALIGN static void write_32(unsigned char *at, unsigned long value);
H2_ALIGN static void write_real(unsigned char *at, float value);

/* ---------- public code */

H2_ALIGN boolean h2_plasma_shield_read(
	struct h2_cache *cache,
	long shader_tag,
	char const *template_name,
	struct h2_plasma_shield_pass *pass)
{
	struct h2_cache_tag t;
	long definition_count, pixel_count, vertex_count, overlay_count, reference_count, animated_count, animated_reference_count, k, j;
	unsigned long definitions, pixels, vertices, overlays, references, animated, animated_references;
	int texture_groups = 0;
	long texture_group_param[2] = { -1, -1 };

	memset(pass, 0, sizeof(*pass));
	if (!template_name || !strstr(template_name, "\\plasma_shield"))
		return FALSE;
	pass->mode = strstr(template_name, "plasma_shield_change_color") ? _h2_plasma_shield_mode_change_color : _h2_plasma_shield_mode_shield;
	if (!h2_cache_tag_get(cache, shader_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x20, 0x7C, &definition_count, &definitions) || definition_count < 1 ||
		!h2_cache_block(cache, definitions + 0x14, 0x10, &vertex_count, &vertices) || vertex_count < 8)
	{
		return FALSE;
	}
	if (!h2_cache_block(cache, definitions + 0xC, 4, &pixel_count, &pixels))
		pixel_count = 0;
	/* the constants: the colours as the shader has them (black where a
	function drives them), the scales */
	for (k = 0; k < NUMBER_OF_H2_PLASMA_SHIELD_COLOURS; k++)
		pass->colour_low[k] = pass->colour_high[k] = 0xFFFFFFUL;
	if (pass->mode == _h2_plasma_shield_mode_shield)
	{
		for (k = 0; k < 3 && k < pixel_count; k++)
			pass->colour_low[k] = pass->colour_high[k] = h2_cache_read32(cache, pixels + 4 * k) & 0xFFFFFFUL;
	}
	pass->colour_low[_h2_plasma_shield_colour_color] = pass->colour_high[_h2_plasma_shield_colour_color] = argb_of_reals(cache, vertices + 0x10 * 6);
	pass->colour_low[_h2_plasma_shield_colour_glancing] = pass->colour_high[_h2_plasma_shield_colour_glancing] = argb_of_reals(cache, vertices + 0x10 * 7);
	pass->scale[0] = real_at(cache, vertices);
	pass->scale[1] = real_at(cache, vertices + 0x10 * 3);
	for (k = 0; k < 2; k++)
	{
		if (!(pass->scale[k] > 0.0f))
			pass->scale[k] = 1.0f;
	}
	/* the functions */
	if (!h2_cache_block(cache, definitions + 0x3C, 0x14, &overlay_count, &overlays) ||
		!h2_cache_block(cache, definitions + 0x44, 4, &reference_count, &references) ||
		!h2_cache_block(cache, definitions + 0x4C, 2, &animated_count, &animated) ||
		!h2_cache_block(cache, definitions + 0x54, 4, &animated_reference_count, &animated_references))
	{
		return TRUE;
	}
	for (k = 0; k < animated_reference_count; k++)
	{
		unsigned char const *r = h2_cache_at(cache, animated_references + 4 * k, 4);
		unsigned char const *a;
		unsigned long reg;
		long param, start;

		if (!r)
			continue;
		reg = (unsigned long)r[0] | ((unsigned long)r[1] << 8);
		param = r[3];
		if (param >= animated_count || !(a = h2_cache_at(cache, animated + 2 * param, 2)))
			continue;
		start = a[0];
		if (start >= reference_count)
			continue;
		if ((reg >> 8) == 0x02 || (reg >> 8) == 0x06)
		{
			/* a noise's group: the first seen noyze0, the second noyze1; its
			overlays by its map's components until one repeats */
			int noise = -1;
			unsigned short seen = 0;

			for (j = 0; j < texture_groups; j++)
			{
				if (texture_group_param[j] == param)
					noise = j;
			}
			if (noise < 0)
			{
				if (texture_groups >= 2)
					continue;
				texture_group_param[texture_groups] = param;
				noise = texture_groups++;
			}
			else
				continue;
			for (j = start; j < reference_count; j++)
			{
				unsigned long reference = h2_cache_read32(cache, references + 4 * j);
				long component = (long)(reference >> 16), overlay = (long)(reference & 0xFFFF);
				unsigned char const *o, *data;
				unsigned long input;
				long size;

				if (component < 0 || component > 9 || (seen & (1 << component)) || overlay >= overlay_count)
					break;
				seen |= (unsigned short)(1 << component);
				if (component < 4 || component > 6 || !(o = h2_cache_at(cache, overlays + 0x14 * overlay, 0x14)))
					continue;
				input = (unsigned long)o[0] | ((unsigned long)o[1] << 8);
				size = (long)h2_cache_read32(cache, overlays + 0x14 * overlay + 0xC);
				data = size >= 12 ? h2_cache_at(cache, h2_cache_read32(cache, overlays + 0x14 * overlay + 0x10), (unsigned long)size) : NULL;
				if (!data || data[0] != 2)
					continue;
				if (input)
				{
					float high;

					memcpy(&high, data + 8, sizeof(high));
					pass->translation_by_input[noise][component - 4] = 1;
					pass->translation_high[noise][component - 4] = high;
					if (!pass->input_name[0])
						h2_cache_string(cache, input, pass->input_name, sizeof(pass->input_name));
				}
				else
				{
					float period;

					memcpy(&period, o + 8, sizeof(period));
					pass->translation_period[noise][component - 4] = period > 0.0f ? period : 0.0f;
				}
			}
		}
		else
		{
			unsigned long reference = h2_cache_read32(cache, references + 4 * start);
			long overlay = (long)(reference & 0xFFFF);
			unsigned char const *o, *data;
			unsigned long input;
			long size, colour = -1;
			boolean offset = FALSE;

			if (overlay >= overlay_count || !(o = h2_cache_at(cache, overlays + 0x14 * overlay, 0x14)))
				continue;
			if ((reg >> 8) == 0x1C && (reg & 0xF0) == 0x80)
				colour = (reg & 0xF) == 10 ? _h2_plasma_shield_colour_color : (reg & 0xF) == 11 ? _h2_plasma_shield_colour_glancing : -1;
			else if ((reg >> 8) == 0x1C && (reg & 0xF0) == 0x70)
				offset = (reg & 0xF) == 8;
			else if ((reg >> 8) == 0 && (reg & 0xF0) == 0x80 && pass->mode == _h2_plasma_shield_mode_shield)
				colour = (long)(reg & 0xFF) - 0x82;
			if ((colour < 0 || colour > 2) && colour != _h2_plasma_shield_colour_color && colour != _h2_plasma_shield_colour_glancing && !offset)
				continue;
			input = (unsigned long)o[0] | ((unsigned long)o[1] << 8);
			size = (long)h2_cache_read32(cache, overlays + 0x14 * overlay + 0xC);
			data = size >= 12 ? h2_cache_at(cache, h2_cache_read32(cache, overlays + 0x14 * overlay + 0x10), (unsigned long)size) : NULL;
			/* (a transition by the input: its ends; a constant, or one by
			time or periodic - none of these shaders' -, its first) */
			if (!data)
				continue;
			if (offset)
			{
				float low, high;

				memcpy(&low, data + 4, sizeof(low));
				memcpy(&high, data + 8, sizeof(high));
				if (data[0] == 2 && input)
				{
					pass->offset_by_input = 1;
					pass->offset_high = high;
					pass->offset_shape = data[2] <= 5 ? data[2] : 0;
				}
			}
			else if ((data[1] & 0x20) && size >= 20)
			{
				/* (ARGB little-endian: b g r a) */
				unsigned long low = ((unsigned long)data[6] << 16) | ((unsigned long)data[5] << 8) | data[4];
				unsigned long high = ((unsigned long)data[18] << 16) | ((unsigned long)data[17] << 8) | data[16];
				if (data[0] == 2 && input)
				{
					pass->colour_by_input[colour] = 1;
					pass->colour_shape[colour] = data[2] <= 5 ? data[2] : 0;
					pass->colour_low[colour] = low;
					pass->colour_high[colour] = high;
					if (!pass->input_name[0])
						h2_cache_string(cache, input, pass->input_name, sizeof(pass->input_name));
				}
				else if (data[0] == 2)
				{
					/* (a transition with no input: at its end - see the header) */
					pass->colour_low[colour] = pass->colour_high[colour] = high;
				}
				else
					pass->colour_low[colour] = pass->colour_high[colour] = low;
			}
		}
	}
	return TRUE;
}

H2_ALIGN void h2_plasma_shield_write(
	unsigned char *tag,
	struct h2_plasma_shield_pass const *pass,
	short intensity_source)
{
	int k, n, c;

	write_32(tag + _h2_plasma_shield_offset_magic, H2_PLASMA_SHIELD_MAGIC);
	tag[_h2_plasma_shield_offset_mode] = (unsigned char)pass->mode;
	tag[_h2_plasma_shield_offset_mode + 1] = 0;
	for (k = 0; k < NUMBER_OF_H2_PLASMA_SHIELD_COLOURS; k++)
	{
		tag[_h2_plasma_shield_offset_colour_by_input + k] = pass->colour_by_input[k];
		tag[_h2_plasma_shield_offset_colour_shape + k] = pass->colour_shape[k];
		write_32(tag + _h2_plasma_shield_offset_colour_low + 4 * k, pass->colour_low[k]);
		write_32(tag + _h2_plasma_shield_offset_colour_high + 4 * k, pass->colour_high[k]);
	}
	for (n = 0; n < 2; n++)
	{
		for (c = 0; c < 3; c++)
		{
			tag[_h2_plasma_shield_offset_translation_by_input + 3 * n + c] = pass->translation_by_input[n][c];
			write_real(tag + _h2_plasma_shield_offset_translation_high + 4 * (3 * n + c), pass->translation_high[n][c]);
		}
	}
	tag[_h2_plasma_shield_offset_offset_by_input] = pass->offset_by_input;
	tag[_h2_plasma_shield_offset_offset_shape] = pass->offset_shape;
	write_real(tag + _h2_plasma_shield_offset_offset_high, pass->offset_high);
	/* the CE offset: the input's function to the power 1, times the high */
	if (pass->offset_by_input && intensity_source > 0)
	{
		tag[0x34] = (unsigned char)intensity_source;
		tag[0x35] = 0;
		write_real(tag + 0x38, pass->offset_high);
		write_real(tag + 0x3C, 1.0f);
	}
}

/* ---------- private code */

H2_ALIGN static float real_at(struct h2_cache *cache, unsigned long address)
{
	void const *p = h2_cache_at(cache, address, 4);
	float v = 0.0f;

	if (p)
		memcpy(&v, p, sizeof(v));
	return v;
}

/* (a vertex constant's r g b, 0..1, as ARGB) */
H2_ALIGN static unsigned long argb_of_reals(struct h2_cache *cache, unsigned long address)
{
	unsigned long argb = 0;
	int c;

	for (c = 0; c < 3; c++)
	{
		float v = real_at(cache, address + 4 * c);
		long byte = (long)(v * 255.0f + 0.5f);

		byte = byte < 0 ? 0 : byte > 255 ? 255 : byte;
		argb |= (unsigned long)byte << (16 - 8 * c);
	}
	return argb;
}

H2_ALIGN static void write_32(unsigned char *at, unsigned long value)
{
	at[0] = (unsigned char)value;
	at[1] = (unsigned char)(value >> 8);
	at[2] = (unsigned char)(value >> 16);
	at[3] = (unsigned char)(value >> 24);
}

H2_ALIGN static void write_real(unsigned char *at, float value)
{
	memcpy(at, &value, sizeof(value));
}
