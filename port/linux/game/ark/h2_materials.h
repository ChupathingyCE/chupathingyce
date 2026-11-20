/*
H2_MATERIALS.H

(port) Halo 2's materials on this engine's environment shaders: how a Halo 2
level's surfaces answer the light as Halo 2's do.

A Halo 2 level (h2_level.c) is made into this engine's own cache file, its
structure's shaders environment shaders (shader_environment). Most of a
Halo 2 material fits one: its base map, its detail map (two of them blended by
the base map's alpha: CE's "blended" type), its bump map. What does not fit,
the converter (h2_materials.c) writes into an extension block in the
environment shader's diffuse properties' reserved bytes (0x18 bytes at the
shader's 0x70, zero in every Halo CE shader), marked 'h2mt'; the renderer
(h2_materials_render.c, called from rasterizer_xbox_environment.c) reads it:

- the lightmap pass with Halo 2's bump response, the lightmap times
  1 + a(N.L - Lz) (H2EK shaders\shader_passes\lightmap\lightmap_tex_detail:
  a flat surface keeps its lightmap whatever the light's direction; CE's own
  pass darkens it by 1 - a + a(N.L));
- the specular lightmap pass as Halo 2's: the lightmap tinted by the
  material's specular colour, Halo 2's 2(N.L)(N.E) - L.E to the 8th (4th
  dull, the shiny curve), masked by the diffuse pass's alpha;
- the detail maps' scales along v apart from u (Halo 2's scales are two:
  the beach's sand 128 x 64).

Nothing of this reaches a Halo CE map: no Halo CE shader carries the mark.
The quality tier (HALO_H2_MATERIALS, config.toml's graphics.h2_materials:
"full" the default everywhere - the Vita's GXM device draws the models'
per-pixel pass by its Cg twins, rasterizer_vita_custom_programs.h - "cheap"
a choice: the Vita's settings panel's Halo 2 materials row) chooses at draw
time; the
file made is the same on every machine (the tier changes no tag).
*/

#ifndef __H2_MATERIALS_H
#define __H2_MATERIALS_H
#pragma once

#include "cseries/cseries.h"

/* ---------- constants */

enum
{
	H2_MATERIAL_MAGIC = 0x68326D74, /* 'h2mt' */
	H2_MATERIAL_VERSION = 1,
	/* the extension's place in a shader_environment tag (its diffuse
	properties' reserved04), its size */
	H2_MATERIAL_EXTENSION_OFFSET = 0x70,
	H2_MATERIAL_EXTENSION_SIZE = 0x18,
	/* the materials' part of the level builder's version (the level file's
	header 0x710): a file made by another is made again */
	H2_MATERIALS_BUILDER_VERSION = 15,   /* (15: model shaders carry their own specular type; 14 (r03a r16): detail blend terrains, env clamped combined; 13 (r03a r7): prt templates marked; 12 (r03a r4): tex_bump_dprs_env's own environment map pass flagged) */
	/* a model shader's (shader_model) extension: in its reserved bytes after
	the change colour source (0x4E-0x6B, zero in every Halo CE shader) */
	H2_MODEL_MATERIAL_MAGIC = 0x68326D6F, /* 'h2mo' */
	H2_MODEL_MATERIAL_EXTENSION_OFFSET = 0x50,
	H2_MODEL_MATERIAL_EXTENSION_SIZE = 0x1C,
	/* (an smod's padding: Halo 2's plasma self-illumination, 'H2PI', then
	channels a, b and c's colours, X8R8G8B8) */
	H2_MODEL_PLASMA_ILLUM_MAGIC_OFFSET = 0xB4,
	H2_MODEL_PLASMA_ILLUM_COLOUR_OFFSET = 0x90,
	/* ('H2TC' for the magic: a three-channel glow that flashes - the colours
	its low ones, each channel's other colour and the flash's seconds here) */
	H2_MODEL_THREE_FLASH_HIGH_OFFSET = 0xF0,
	H2_MODEL_THREE_FLASH_PERIOD_OFFSET = 0xB8,
};

/* the extension's flags */
enum
{
	/* the lightmap pass as Halo 2's (its bump relative to the flat surface) */
	_h2_material_relative_bump_bit = 0x0001,
	/* Halo 2's specular from the lightmap */
	_h2_material_specular_bit = 0x0002,
	/* the detail maps' v scales: their u scales times detail_aspect */
	_h2_material_detail_aspect_bit = 0x0004,
	/* (an environment mapped template whose PC lightmap pass writes the
	reflection's mask 0 - lightmap_tex_detail_overlay: Halo 2 draws no
	reflection on it; rasterizer_xbox_environment.c h2_env_sheen_mode) */
	_h2_material_env_masked_out_bit = 0x0008,
	/* (tex_bump_three_detail_blend: its three details blended by the base
	map's alpha - detail a to b over 0..0.5, to c over 0.5..1: the PC's
	lightmap_tex_three_detail_blend, its texture stages base, a, b, c - this
	engine's primary, secondary and micro detail maps;
	rasterizer_xbox_environment.c's texture pass) */
	_h2_material_three_detail_bit = 0x0010,
	/* (h2/render-accuracy: the shader's own 'lightmap specular brightness'
	- Halo 2's $lightmap_specular_brightness, the shader tag's 0x44 - in the
	specular colour's top byte, times 64: the desktop's specular pass,
	h2_material_specular_brightness) */
	_h2_material_specular_brightness_bit = 0x0020,
	/* (h2/render-accuracy: the specular's mask as the template's PC
	lightmap pass makes it - h2_material_specular_mask: the lightmap_tex_
	detail family sat((2|I| - 1) base.a) base.a, the base alone ones
	sat((2|I| - 1) base.a), the detail_overlay and no_specular ones none;
	the rest the destination alpha, the details' alphas in it) */
	_h2_material_specular_mask_double_bit = 0x0040,
	_h2_material_specular_mask_base_bit = 0x0080,
	_h2_material_specular_masked_out_bit = 0x0100,
	/* (h2/render-accuracy: a glow Halo 2 adds after the lit pass - its
	self-illumination passes, default_self_illumination(_scroll, _detail),
	"add" T0 x $self_illum_color, and lightmap_tex_detail_illum_no_bump's
	SRCCOLOR = T2 + [R0 + D1] -, unlit and not times the base map; this
	engine's self-illumination is light in the lightmap pass, times the base
	in the texture pass. The desktop's environment passes add it
	(rasterizer_xbox_environment.c h2_glow_added); the Vita as before) */
	_h2_material_glow_added_bit = 0x0200,
	/* (a Halo 3 level's, h3_level.c) the relief as the formula has it, once:
	Halo 3's dominant light's share is its a - not H2_LEVEL_RELIEF's
	doubling, chosen against Halo 2's frames; 0x0008 on h3/vita-mem,
	moved here at the 1.2.0 merge: 0x0008-0x0200 are 1.2.0's) */
	_h2_material_relief_once_bit = 0x0400,
};

/* the model extension's flags */
enum
{
	/* (an environment mapped template's) Halo 2's lightmap_tex_env pass:
	its specular light half the shiny pass's (no 2x on the tinted light) */
	_h2_model_material_env_specular_bit = 0x01,
	/* (an environment map pass of its own - tex_bump_dprs_env, the Covenant
	weapons' purple metal: Halo 2 adds its reflection whole, not within the
	lit lightmap pass - the plasma rifle's prongs' violet sheen) */
	_h2_model_material_env_pass_bit = 0x02,
	/* (a multiplayer player's armour: a pass of Halo 2's secondary change
	colour over the multipurpose map's alpha - its change colour map's green;
	this engine's model shaders tint the primary alone) */
	_h2_model_material_secondary_bit = 0x04,
	/* (a visor: its reflection tinted by the player's visor colour,
	player_cosmetics.c) */
	_h2_model_material_visor_bit = 0x08,
};

/* Halo 2's specular types (the shader's "specular type", the object light
passes' $specular_type): the highlight's curve of R = 2(N.L)(N.E) - L.E -
none, R^8, R^2 (dull), 4(R^8 - 0.75) (shiny) */
enum
{
	_h2_specular_type_none = 0,
	_h2_specular_type_default,
	_h2_specular_type_dull,
	_h2_specular_type_shiny,
};

/* Halo 2's lightmap types (the passes' $lightmap_type) */
enum
{
	_h2_lightmap_type_none = 0,
	_h2_lightmap_type_default,
	_h2_lightmap_type_dull,
	_h2_lightmap_type_shiny,
};

enum h2_materials_tier
{
	/* (a choice) base, details, the lightmap without bump; no specular
	pass */
	_h2_materials_cheap = 0,
	/* the default: bump, Halo 2's lightmap and specular passes, the models'
	per-pixel pass */
	_h2_materials_full,
};

/* ---------- structures */

/* in the tag (little endian, as the cache file) */
struct h2_material_extension
{
	unsigned long magic;            /* H2_MATERIAL_MAGIC */
	unsigned char version;
	unsigned char lightmap_type;    /* _h2_lightmap_type_* */
	unsigned short flags;
	float detail_aspect[2];         /* primary, secondary: v scale / u scale */
	unsigned long specular_color;   /* X8R8G8B8, perpendicular */
	unsigned long specular_glancing_color;
};

typedef char h2_material_extension_size_assert[sizeof(struct h2_material_extension) == H2_MATERIAL_EXTENSION_SIZE ? 1 : -1];

/* a model shader's: its bump map (a bitmap tag's datum index) and its
scales, its specular colours - for the per-pixel pass (the desktop's full
tier: h2_materials_render.c, rasterizer_xbox_models.c) */
struct h2_model_material_extension
{
	unsigned long magic;            /* H2_MODEL_MATERIAL_MAGIC */
	unsigned long bump_map;         /* datum index, 0xFFFFFFFF none */
	float bump_scale[2];
	unsigned long specular_color;   /* X8R8G8B8 */
	unsigned long specular_glancing_color;
	/* the shader's own "specular type" (the H2EK's; what Halo 2's object
	light pass switches its highlight's curve on): _h2_specular_type_* */
	unsigned char specular_type;
	unsigned char flags;
	unsigned short detail_alpha;    /* its detail map's mean alpha, a part of the specular mask (65535 whole); 0 unknown, whole */
};

typedef char h2_model_material_extension_size_assert[
	sizeof(struct h2_model_material_extension) == H2_MODEL_MATERIAL_EXTENSION_SIZE ? 1 : -1];

/* a Halo 2 shader as read from its map (h2_material_read) */
struct h2_material
{
	boolean known;              /* its template is one this file reads */
	long bump_tag;              /* Halo 2 bitmap tags, NONE none */
	long base_tag;
	long detail_tag;            /* shows where the base's alpha is 0 */
	long secondary_tag;         /* (blended) where it is 1 */
	float bump_scale[2];
	float detail_scale[2];
	float secondary_scale[2];
	/* (a template with one, its base map's: 1 none) */
	float base_scale[2];
	boolean blended;
	unsigned long specular_color, specular_glancing_color;
	unsigned char lightmap_type;
	/* (the shader's own "lightmap type", 0x40: 0 diffuse - no lightmap
	specular -, 1 default, 2 dull, 3 shiny specular; -1 not read) */
	signed char shader_lightmap_type;
	/* (the shader's own "specular type", 0x3E: _h2_specular_type_*, the
	object light passes' curve; -1 not read) */
	signed char shader_specular_type;
	/* (the shader's own "lightmap specular brightness", 0x44 - the
	lightmap passes' C1a, $lightmap_specular_brightness; -1 not read) */
	float lightmap_specular_brightness;
	/* (its specular mask: the extension's _h2_material_specular_mask_* bits) */
	unsigned short specular_mask;
	/* (environment mapped templates) the cube, its tints and brightnesses,
	looking along the surface (glancing) and into it */
	long cube_tag;
	long change_colour_tag;     /* (change colour templates) its mask map: red the primary colour's, green the secondary's; NONE */
	boolean has_env;
	boolean env_pass;           /* (its template has an environment map pass of its own: tex_bump_dprs_env) */
	boolean env_masked_out;     /* (its template's PC lightmap pass writes the reflection's mask 0: tex_bump_env_detail_overlay) */
	boolean three_detail;       /* (tex_bump_three_detail_blend: detail a detail_tag, b secondary_tag, c overlay_tag) */
	boolean prt;                /* (a prt_ template: lit by the lighting environment, h2_lighting_prt_environment) */
	boolean glow_added;         /* (its glow added after the texture pass: _h2_material_glow_added_bit - h2_level.c sets it as it writes the shader) */
	float env_tint[3], env_glancing_tint[3];
	float env_brightness, env_glancing_brightness;
	/* (alpha test templates) the map whose alpha the template tests - the
	palms' fronds' bump map: their base maps DXT1, no alpha; NONE none */
	long alpha_test_tag;
	/* (tex_bump_detail_blend_detail) its overlay detail map, times twice it
	after the blend (H2EK lightmap_tex_detail_blend_detail: R0 = 2 R0 T6);
	NONE none */
	long overlay_tag;
	float overlay_scale[2];
};

/* ---------- prototypes/H2_MATERIALS.C */

struct h2_cache;

/* the shader tag at shader_address (its template's name template_name):
its maps, scales and specular, as its template lays them out; FALSE (and
known FALSE) when the template is not one of the table's */
boolean h2_material_read(
	struct h2_cache *cache,
	unsigned long shader_address,
	char const *template_name,
	struct h2_material *material);

/* the extension and the fields it changes written into a shader_environment
being made (shader its first byte): the type (blended), the bump map's
runtime scale, the extension block */
void h2_material_shader_write(
	unsigned char *shader,
	struct h2_material const *material,
	boolean bumped);

/* a map's alpha (DXT3, DXT5, 32-bit: Halo 2's specular masks) as a
side x side mask, a byte a texel, row by row (the caller frees); NULL for a
format without alpha */
unsigned char *h2_material_alpha_mask(
	unsigned char const *pixels,
	unsigned long size,
	unsigned long width,
	unsigned long height,
	unsigned short format,
	unsigned long side);

/* a model shader's extension written (shader its first byte), bump_map the
bump bitmap's tag datum (0xFFFFFFFF none) */
void h2_material_model_shader_write(
	unsigned char *shader,
	struct h2_material const *material,
	unsigned long bump_map);

/* a model shader's extension given its detail map's mean alpha (0..1):
Halo 2's specular mask is the base map's alpha times the detail map's */
void h2_material_model_detail_alpha_write(
	unsigned char *shader,
	float alpha);

/* (port) a Halo 2 object's change colours (its tag's change colors, 0xAC:
primary, secondary..., their initial permutations: weight, lower and upper
bound, variant name) as this engine's change colours C (the primary, the
colour the model shaders' change colour masks take) and D (the secondary;
A and B left out). Each colour's permutations a group per model variant
the object can be made as: group 0 the model's default variant's - this
engine's own form, weights rising to 1 -, group v (its variant number v,
the hlmt's variant v - 1) with v added to its weights (objects.c picks
within the object's variant's group, else group 0). A permutation is the
variant's if it names it or names none, picked by its weight (all 0: each
alike). FALSE when the tag has no change colour with a permutation */
enum
{
	H2_CHANGE_COLOUR_PERMUTATIONS = 64,
};

struct h2_change_colour_permutation
{
	float weight;
	float lower[3];
	float upper[3];
};

struct h2_change_colours
{
	short count[4];         /* this engine's A B C D */
	struct h2_change_colour_permutation permutations[4][H2_CHANGE_COLOUR_PERMUTATIONS];
};

boolean h2_change_colours_read(
	struct h2_cache *cache,
	long h2_object_tag,
	struct h2_change_colours *colours);

/* a glow Halo 2 colours by the object's health: its colour whole (colour,
3 floats) - TRUE if the shader is one; a bloom shell not drawn */
boolean h2_material_glow_colour(
	char const *shader_name,
	float *colour);
/* a tex_bump_plasma_one_channel_illum shader's channel colours whole, FALSE
if this file has none for it (they run by object functions) */
boolean h2_material_plasma_illum_colours(
	char const *shader_name,
	unsigned long *colours);
/* the renderer's side: its pass's program over a model part (colours as the
smod holds them), NULL to end */
void h2_material_model_plasma_illum_program(
	unsigned long const *colours,
	float time);
boolean h2_material_plasma_illum_available(
	void);

boolean h2_material_shell_hidden(
	char const *shader_name);

/* ---------- prototypes/H2_MATERIALS_RENDER.C */

/* (the desktop's OpenGL device sets it: d3d8_gl.c) the next draws with
these GLSL sources and uniform array h2u, NULL sources: the translated
programs again */
extern void (*h2_materials_custom_program)(
	const char *vertex_source,
	const char *fragment_source,
	const float *uniforms,
	int vec4_count);

/* a shader_model's extension, NULL none (every Halo CE shader) */
struct h2_model_material_extension const *h2_material_model_extension_get(
	void const *shader_model);

/* whether a model part with it gets the per-pixel pass (the full tier, the
device able to, a bump map; compressed model vertices, the pass's own
vertex program's) */
boolean h2_material_model_pass_wanted(
	struct h2_model_material_extension const *extension,
	boolean compressed_vertices);

enum
{
	H2_MODEL_PASS_UNIFORMS = 10,
	_h2_model_pass_relief = 0,
	_h2_model_pass_specular,
	/* (the specular pass over first-person geometry: Halo 2's first-person
	implementation of the light pass) */
	_h2_model_pass_specular_first_person,
	/* (the secondary colour: the armour multiplied by it where the
	multipurpose map's alpha says, tex0 that map, before the others) */
	_h2_model_pass_secondary,
	/* (a visor in a colour of the player's own, over what the material drew
	- see rasterizer_model_visor_pass: the drawn visor times what turns its
	baked hue into the colour, then what that cannot reach added, lit) */
	_h2_model_pass_visor,
	_h2_model_pass_visor_add,
};

/* the gain on a model shader's environment map's brightness (1 for none
or the cheap tier; HALO_H2_MODEL_ENV, 2: Halo 2's envmap pass doubles it) */
float h2_material_model_environment_gain(
	struct h2_model_material_extension const *extension);

/* the pass's uniforms (H2_MODEL_PASS_UNIFORMS vec4s): the lights (distant
ones: their directions, the way the light goes, and colours; up to two),
the ambient colour, the camera's position, the material's specular; and
the program set (h2_materials_custom_program) for that mode, NULL
uniforms: back to the translated programs */
void h2_material_model_pass_program(
	struct h2_model_material_extension const *extension,
	float const (*light_directions)[3],
	float const (*light_colors)[3],
	short light_count,
	float const *ambient_color,
	float const *camera_position,
	int mode,
	float (*uniforms)[4]);


/* the tier chosen (HALO_H2_MATERIALS, graphics.h2_materials; it can change
between frames) */
enum h2_materials_tier h2_materials_tier(void);

/* a shader_environment's extension (shader_environment its tag data,
struct shader first), NULL when it has none: every Halo CE shader */
struct h2_material_extension const *h2_material_extension_get(
	void const *shader_environment);

/* the environment lightmap pass's program for a shader with an extension
(lightmap present, no self-illumination; pixel_shader a struct
pixel_shader_definition), material_color its final constant: TRUE when
written (the cheap tier: the lightmap without bump) */
/* the level lightmap relief's combiner output scale: 1, 2 or 4 */
int h2_material_level_relief(void);
boolean h2_material_lightmap_program(
	struct h2_material_extension const *extension,
	void *pixel_shader,
	unsigned long material_color,
	boolean alpha_tested);

/* whether the specular lightmap pass draws Halo 2's specular for it (the
full tier, a specular colour), and its program */
boolean h2_material_specular_wanted(
	struct h2_material_extension const *extension);
void h2_material_specular_program(
	struct h2_material_extension const *extension,
	void *pixel_shader);

#endif
