/*
H2_PLASMA_SHIELD.H

port: Halo 2's plasma_shield pass (H2EK shader_templates\transparent\
plasma_shield, plasma_shield_change_color, their passes shader_passes\plasma\
plasma_shield, plasma_shield_change_color; the overshield pass is the same
shape): the Elite's and Chief's shields when hit, the powerups' orbs. The
level builder (h2_biped.c, h2_weapons.c) makes each one this engine's plasma
shader (spla: a copy of the kit's shield hit, two 3D noise volumes) and
writes Halo 2's pass beside the CE fields, in the tag's padding
(h2_plasma_shield_write); on the desktop rasterizer_xbox_plasma_energy.c
runs the pass's combiner text over the CE noise volumes as it is:

  R0a = T0a - HALF_BIAS(T1a); R0 = T1a - HALF_BIAS(T0a)
  R0a = 4 (R0a^2 mux R0b^2)                             basic plasma
  R1a = 0 mux EXPAND(R0a)^2                             sharpened
  R0 = (R0a color_wide + R1a color_sharp) D0; R1 = R1a^2 center_line
  SRCCOLOR = R1 D0 + R0, SRCALPHA = 1, added
  (change_color: color_wide = color_sharp = the object's primary change
  colour, center_line white)

D0 is the vertex shader's (transparent_generic_offset): color at a vertex
facing the camera to glancing_color at its rim (by the normal's dot with
the eye), the vertex pushed along its normal by the offset. The shaders'
parameters run by functions of the object's shield_hit input (MCC
shared.map masterchief_plasma_shield_change_color: color black to fff5ae,
glancing_color black to c13f00, the offset 0 to 0.05, the noise
translations 0 to 2 along x; elite_plasma_shield: wide to 66abf7, sharp to
77bbff, center_line to adf4fa, glancing to 9800d9; color white) or of time
(the powerups' translations: a repeat each period). The Vita keeps the CE
stand-in (the kit's plasma over Halo 2's tint).

Tag layout (offsets in the spla tag of 0x14C bytes; the CE definition's
padding: 0x40..0x60, 0x82..0xC0, 0xE4..0x108):
  0x40 'H2PS'; 0x44 mode (short); 0x46 colour_by_input[5]; 0x4B
  translation_by_input[2][3]; 0x51 offset_by_input; 0x54 offset_high;
  0x58 colour_shape[5]; 0x5D offset_shape
A transition with no input (materchief_plasma_shield's wide, sharp and
center_line: black to faf1b4, e07912, 452603) is taken at its end - Halo
2's input for one is not known; at its start every colour would be black.
  0x84 colour_low[5] ARGB; 0x98 colour_high[5]
  0xE4 translation_high[2][3]
The CE fields carry the rest: each noise's scale (0xD0, 0x118), its period
and direction (0xC0.., 0x108..: a translation by time), the intensity source
(0x2C: the shield_hit function) and the offset's source, amount and
exponent (0x34, 0x38, 0x3C).
*/

#ifndef __H2_PLASMA_SHIELD_H
#define __H2_PLASMA_SHIELD_H

#include "cseries.h"

struct h2_cache;

/* ---------- constants */

enum
{
	H2_PLASMA_SHIELD_MAGIC = 0x48325053UL,  /* 'H2PS' */
	H2_PLASMA_SHIELD_TAG_SIZE = 0x14C,

	_h2_plasma_shield_mode_shield = 0,          /* color_wide, color_sharp, center_line the shader's */
	_h2_plasma_shield_mode_change_color,        /* the object's primary change colour, center_line white */
	_h2_plasma_shield_mode_overshield,

	/* the colours: the pass's order */
	_h2_plasma_shield_colour_wide = 0,
	_h2_plasma_shield_colour_sharp,
	_h2_plasma_shield_colour_center_line,
	_h2_plasma_shield_colour_color,
	_h2_plasma_shield_colour_glancing,
	NUMBER_OF_H2_PLASMA_SHIELD_COLOURS,

	_h2_plasma_shield_offset_magic = 0x40,
	_h2_plasma_shield_offset_mode = 0x44,
	_h2_plasma_shield_offset_colour_by_input = 0x46,
	_h2_plasma_shield_offset_translation_by_input = 0x4B,
	_h2_plasma_shield_offset_offset_by_input = 0x51,
	_h2_plasma_shield_offset_offset_high = 0x54,
	_h2_plasma_shield_offset_colour_shape = 0x58,    /* 5 bytes: each colour's transition function (periodic_functions.h) */
	_h2_plasma_shield_offset_offset_shape = 0x5D,
	_h2_plasma_shield_offset_colour_low = 0x84,
	_h2_plasma_shield_offset_colour_high = 0x98,
	_h2_plasma_shield_offset_translation_high = 0xE4,
};

/* ---------- structures */

struct h2_plasma_shield_pass
{
	short mode;
	/* each colour at the input's 0 and 1 (ARGB); a constant one has both
	the same and by_input 0 */
	unsigned long colour_low[NUMBER_OF_H2_PLASMA_SHIELD_COLOURS], colour_high[NUMBER_OF_H2_PLASMA_SHIELD_COLOURS];
	unsigned char colour_by_input[NUMBER_OF_H2_PLASMA_SHIELD_COLOURS];
	/* (by the input) its transition function: linear, early, very early,
	late, very late, cosine - Halo 2's order, this engine's
	periodic_functions.h */
	unsigned char colour_shape[NUMBER_OF_H2_PLASMA_SHIELD_COLOURS];
	/* the two noises: their scales; each translation along x, y, z a repeat
	each period seconds (by time; 0: still), or 0..high by the input */
	float scale[2];
	float translation_period[2][3], translation_high[2][3];
	unsigned char translation_by_input[2][3];
	/* the vertex offset along the normal: 0..high by the input (constant
	ones - the powerups' throb - are left out) */
	float offset_high;
	unsigned char offset_by_input, offset_shape;
	/* (the build's log) the noise bitmaps' names, the input's name */
	char input_name[32];
};

/* ---------- prototypes */

/* Halo 2's plasma_shield pass read off a shader tag (its postprocess
definition: bitmaps, pixel and vertex constants, overlays and their
references, animated parameters and their references); FALSE if the shader
is not one */
boolean h2_plasma_shield_read(
	struct h2_cache *cache,
	long shader_tag,
	char const *template_name,
	struct h2_plasma_shield_pass *pass);

/* the pass written into a spla tag's padding (tag: its 0x14C bytes), and
the CE fields it drives - the offset's source, amount and exponent when the
offset is by the input (intensity_source: the CE function the input is,
0 none) */
void h2_plasma_shield_write(
	unsigned char *tag,
	struct h2_plasma_shield_pass const *pass,
	short intensity_source);

#endif
