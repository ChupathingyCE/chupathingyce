/*
H2_MATERIALS.C

(port) Halo 2's opaque materials read from its map for the level builder
(h2_level.c), and written into this engine's environment shaders
(h2_materials.h says what the extension carries).

A Halo 2 shader tag (shad) keeps, in its postprocess definition (0x20,
0x7C each), its bitmaps (0x04, 0xC each: the tag's handle first), its pixel
constants (0x0C, 4 each: X8R8G8B8) and its vertex constants (0x14, 0x10
each: a map's scale x y first) in its template's order, which the template
alone knows. The table below gives each template's: which bitmap is the
bump, base, detail and secondary detail map, which vertex constant each
one's scale, which pixel constants the specular colours. They were read off
the H2EK's own shaders and templates (tags\shaders\shader_templates\opaque,
the Zanzibar shaders' parameters against the map's constants):

  tex_bump (no_alpha, shiny, alpha_test...): maps bump, lightmap alpha test,
    base, detail; scales bump, base, detail; colours specular, glancing
  tex_bump_detail_keep: maps bump, base, detail; scales the same
  tex_bump_detail_blend: maps bump, base, detail, secondary; scales detail,
    secondary, base, bump (the beach: sand 128 x 64, weeds 32 x 16, the bump
    64 x 32); colours diffuse, diffuse offset, specular, glancing
  tex_bump_detail_keep_blend: maps and scales bump, base, detail, secondary;
    colours as detail_blend's
  tex_bump_env_combined: maps bump, active camouflage bump, base, detail,
    cube; scales bump, base, detail; colours environment tints (2),
    brightnesses (2), specular, glancing (its cube h2_level.c's own)

The blend templates' detail shows where the base's alpha is 0 and the
secondary where it is 1 (the template's own words); this engine's blended
type shows its primary detail map where the alpha is 1, so the secondary is
written as the primary.
*/

/* ---------- headers */

#include "cseries.h"
#include "h2_materials.h"
#include "h2_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef free

/* (see h2_level.c: this file's SSE moves want a 16 byte stack) */
#define H2_ALIGN __attribute__((force_align_arg_pointer))

/* ---------- constants */

enum
{
	MAXIMUM_BITMAPS = 16,
	MAXIMUM_CONSTANTS = 16,
};

/* ---------- structures */

struct material_template
{
	char const *name;           /* the template's name after its folder */
	signed char map_bump, map_base, map_detail, map_secondary, map_cube;
	signed char scale_bump, scale_detail, scale_secondary;     /* vertex constants */
	signed char specular, glancing;                             /* colours (C_PC, C_VC) */
	signed char env_tint, env_glancing_tint;                    /* colours */
	signed char env_brightness, env_glancing_brightness;        /* values (V_PC, V_VC) */
	unsigned char lightmap_type;
	boolean blended;
	/* (h2/render-accuracy: the base map's own scale - its vertex constant;
	rows without it the rule in h2_material_read) */
	boolean own_base_scale;
	signed char scale_base;
};

/* a constant: a pixel constant (its rgb; a value its alpha) or a vertex
constant (its xyz; a value one component) */
#define C_PC(k) (k)
#define C_VC(k) (64 + (k))
#define V_PC(k) (k)
#define V_VC(k, c) (64 + 4 * (k) + (c))
#define ENV_NONE -1, -1, -1, -1

/* ---------- globals */

/* (h2/render-accuracy) the specular mask of each opaque template's lightmap
pass, its PC implementation (H2EK shader_templates\opaque\*: the lightmap
pass each names; pixel_shaders_dx9\shaders_shader_passes_lightmap_*_0.psh,
$lightmap_type 1-3: T1a = EXPAND(D1a) x T0a, then T1a x T0a again in the
lightmap_tex_detail family, T0a the base's alpha; 0 in lightmap_tex_detail_
overlay; no specular term in lightmap_tex_detail_no_specular) */
static char const *const specular_mask_double_templates[] =
{
	"tex_bump", "tex_bump_active_camo", "tex_bump_detail_keep", "tex_bump_dprs_env", "tex_bump_dprs_env_illum",
	"tex_bump_env_fast_masked", "tex_bump_env_four_change_color", "tex_bump_env_illum_3_channel",
	"tex_bump_env_illum_3_channel_occlusion_combined", "tex_bump_env_illum_four_change_color_no_lod",
	"tex_bump_env_illum_two_change_color", "tex_bump_env_illum_two_change_color_combined", "tex_bump_env_two_change_color",
	"tex_bump_env_two_change_color_combined", "tex_bump_env_two_change_color_indexed", "tex_bump_four_change_color",
	"tex_bump_illum", "tex_bump_illum_bloom", "tex_bump_illum_detail", "tex_bump_meter_illum", "tex_bump_no_alpha",
	"tex_bump_plasma", "tex_bump_plasma_one_channel_illum", "tex_bump_shiny", "tex_bump_two_change_color", "tex_illum",
	"tex_illum_bloom", NULL
};
static char const *const specular_mask_base_templates[] =
{
	"tex_bump_env_no_detail", "tex_bump_illum_3_channel", "tex_env", "tex_env_3_channel_illum", NULL
};
static char const *const specular_masked_out_templates[] =
{
	"tex_bump_detail_overlay", "tex_bump_env_detail_overlay", "tex_bump_env_detail_overlay_combined",
	"tex_bump_illum_no_specular", NULL
};

static boolean template_listed(
	char const *const *list,
	char const *name)
{
	for (; *list; list++)
	{
		if (!strcmp(*list, name))
			return TRUE;
	}
	return FALSE;
}

/* (pcmap.py and scalemap.py, local comparison scripts: each H2EK
shader's parameters matched with the MCC map's constants, every shader of
Zanzibar and shared.map the H2EK has) */
/* (h2/render-accuracy, session 20: the rows with an own base scale (the
last two fields) read their vertex constants as MCC lays them out - in the
order the template's LOD0 passes name them (the H2EK's shader_templates\
opaque\* and shader_passes\*: each implementation's vs constants, first
come first; checked against every MCC level shader of the 106 test views'
maps) - and every map their passes draw: tex_bump_env and its clamped and
illum twins detail, base, bump (the bump's scale was left at 1: 01b's
grav_pad 0.5); tex_bump_alpha_test detail, bump, base (its texaccum pass
draws the detail map, left out); tex_bump_env_illum_3_channel and its
occlusion_combined twin, tex_bump_env_alpha_test_combined, tex_bump_plasma,
tex_bump_alpha_test_clamped and tex_bump_env_illum_two_change_color_combined
bump, base, detail (their lightmap pass lightmap_tex_detail* draws the
detail map, left out: 03a's street_straight's horizontal_brushed);
tex_bump_env_two_change_color_multiply_map_self_illum detail, base, change
colour, multiply, bump (the Elite's jetpack's bump read at the detail's
16x); tex_bump_illum_3_channel's lightmap pass (lightmap_tex) draws no
detail map; and every one its base map's own scale (01b's girders 1 x 2).
Two colour reads fixed by MCC's constants against the H2EK shaders: the
honor guard templates' specular is pixel constant 1, glancing 2 (pike,
elite_honor_armor: 2 was read as both), tex_bump_dprs_env_illum's
reflection tints and brightnesses vertex constants 8-11 (the gravity
throne: its tint was read as both tints, its glancing brightness 1).
The PRT templates' too: prt_simple's vertex constants detail, base, bump
(Zanzibar's grating_prt's bump 35x, read at its base's 1x), prt_scarab's,
prt_lightmap's and prt_simple_lm_emissive's base, bump, -, detail.
The old rows name@old: HALO_H2_TEMPLATE_READS_OLD=1, the Vita and its null
build) */
static struct material_template const material_templates[] =
{
	/* the structure's: bump, lightmap alpha test, base, detail */
	{ "tex_bump", 0, 2, 3, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_no_alpha", 0, 2, 3, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_shiny", 0, 2, 3, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_shiny, FALSE },
	/* (its constant 0 is the detail's - 16, its default, on every one: read
	as the bump's, this engine's cut-out, in the bump map, repeated 16 times:
	Sanctuary's vines, Waterworks' bush_vine, Desolation's, Zanzibar's
	spindle_grate shredded into tiny pieces. Its PC passes are
	default_texture_zwrite_alphatest (vs constants detail, bump, base; its
	texaccum twice the base times the detail) then old_lightmap_tangentspace:
	the detail map 4 at constant 0, the bump 1, the base 2 - session 20) */
	{ "tex_bump_alpha_test", 0, 3, 4, -1, -1, 1, 0, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE, TRUE, 2 },
	{ "tex_bump_alpha_test@old", 0, 3, -1, -1, -1, -1, -1, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_alpha_test_single_pass", 0, 2, 3, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_alpha_test_single_pass@old", 0, 2, 3, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_alpha_test_clamped_single_pass", 0, 2, 3, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_alpha_test_clamped_single_pass@old", 0, 2, 3, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_detail_keep", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	/* blends: bump, base, detail, secondary; colours diffuse, offset,
	specular, glancing */
	{ "tex_bump_detail_blend", 0, 1, 2, 3, -1, 3, 0, 1, C_PC(2), C_PC(3), ENV_NONE, _h2_lightmap_type_default, TRUE },
	{ "tex_bump_detail_keep_blend", 0, 1, 2, 3, -1, 0, 2, 3, C_PC(2), C_PC(3), ENV_NONE, _h2_lightmap_type_default, TRUE },
	/* environment mapped: bump, active camouflage bump, base, detail, cube */
	{ "tex_bump_env", 0, 2, 3, -1, 4, 2, 0, -1, C_PC(5), C_PC(6), C_PC(1), C_PC(2), V_PC(3), V_PC(4), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env@old", 0, 2, 3, -1, 4, -1, 0, -1, C_PC(5), C_PC(6), C_PC(1), C_PC(2), V_PC(3), V_PC(4), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_clamped", 0, 2, 3, -1, 4, 2, 0, -1, C_PC(5), C_PC(6), C_PC(1), C_PC(2), V_PC(3), V_PC(4), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_clamped@old", 0, 2, 3, -1, 4, -1, 0, -1, C_PC(5), C_PC(6), C_PC(1), C_PC(2), V_PC(3), V_PC(4), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_no_detail", 0, 2, -1, -1, 3, 0, -1, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_no_detail@old", 0, 2, -1, -1, 3, -1, -1, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_combined", 0, 2, 3, -1, 4, 0, 2, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE },
	/* (their self-illuminated twins - the same maps and constants first, the
	glow map after: 00a's battle cruiser hull, the Covenant strip lights.
	Read as defaults they drew flat, no bump, no metal reflection) */
	{ "tex_bump_env_illum", 0, 2, 3, -1, 4, 2, 0, -1, C_PC(5), C_PC(6), C_PC(1), C_PC(2), V_PC(3), V_PC(4), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_illum@old", 0, 2, 3, -1, 4, -1, 0, -1, C_PC(5), C_PC(6), C_PC(1), C_PC(2), V_PC(3), V_PC(4), _h2_lightmap_type_default, FALSE },
	/* (h2/render-accuracy) tex_bump_env_illum_combined's constants are not
	tex_bump_env_combined's: its PC passes are default_texture_zwrite (vs
	constants detail_map, base_map) then lightmap_env (bump_map, base_map),
	so MCC's vertex constants run detail, base, bump - and its pixel
	constants have one before the reflection's (env_midrange_tint_color's
	place, white): tints 1 and 2, brightnesses 3 and 4, specular 5 and 6
	(H2EK midship_strip_a_grey_lit: detail x4, bump x1, specular 0.12 0.17
	0.27, env tint 0.42 0.49 0.91 = MCC's vertex constants 0 and 6, pixel
	constants 1 and 5). Read as tex_bump_env_combined's, Midship's platform
	rim drew its bump map four times over (concentric bands where Halo 2
	has one groove to each light), its detail map at a quarter of its
	scale, no specular and a white reflection tint. The desktop's;
	HALO_H2_ILLUM_COMBINED_OLD=1 (and the Vita and its null build) the old
	reading, the @old row */
	{ "tex_bump_env_illum_combined", 0, 2, 3, -1, 4, 2, 0, -1, C_PC(5), C_PC(6), C_PC(1), C_PC(2), V_PC(3), V_PC(4), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_illum_combined@old", 0, 2, 3, -1, 4, 0, 2, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_alpha_test", 0, 3, 4, -1, 5, 0, 2, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_alpha_test@old", 0, 3, 4, -1, 5, -1, 2, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_dprs_env", 0, 2, 3, -1, 4, 0, 2, -1, C_PC(0), C_PC(1), C_VC(7), C_VC(8), V_VC(9, 3), V_VC(10, 3), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_dprs_env@old", 0, 2, 3, -1, 4, -1, 2, -1, C_PC(0), C_PC(1), C_VC(7), C_VC(8), V_VC(9, 3), V_VC(10, 3), _h2_lightmap_type_default, FALSE },
	/* change colours: bump, camouflage bump, base, detail, change colour, cube */
	{ "tex_bump_env_four_change_color", 0, 2, 3, -1, 5, 0, 2, -1, C_PC(0), C_PC(1), C_PC(4), C_PC(5), V_PC(6), V_PC(7), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_four_change_color@old", 0, 2, 3, -1, 5, -1, 2, -1, C_PC(0), C_PC(1), C_PC(4), C_PC(5), V_PC(6), V_PC(7), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_two_change_color", 0, 2, 3, -1, 5, 0, 2, -1, C_PC(0), C_PC(1), C_PC(4), C_PC(5), V_PC(6), V_PC(7), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_two_change_color@old", 0, 2, 3, -1, 5, -1, 2, -1, C_PC(0), C_PC(1), C_PC(4), C_PC(5), V_PC(6), V_PC(7), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_illum_two_change_color", 0, 2, 3, -1, 5, 0, 2, -1, C_PC(0), C_PC(1), C_PC(5), C_PC(6), -1, -1, _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_illum_two_change_color@old", 0, 2, 3, -1, 5, -1, 2, -1, C_PC(0), C_PC(1), C_PC(5), C_PC(6), -1, -1, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_illum_3_channel", 0, 1, 2, -1, 3, 0, 2, -1, C_PC(0), C_PC(1), C_PC(10), C_PC(11), V_PC(12), V_PC(13), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_illum_3_channel@old", 0, 1, -1, -1, 3, -1, -1, -1, C_PC(0), C_PC(1), C_PC(10), C_PC(11), V_PC(12), V_PC(13), _h2_lightmap_type_default, FALSE },
	{ "tex_env", -1, 0, 1, -1, 2, -1, 0, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE },
	/* (the PRT templates - Halo 2's precomputed radiance transfer objects:
	maps bump, base, detail, the specular cube; MCC keeps no env tints or
	brightnesses for them, the cube tinted by the pixel constants alone.
	prt_simple (Zanzibar's flywheels and wheels, 03a's Scarab guns): scales
	detail, bump; colours specular, the cube's tint. prt_scarab and
	prt_lightmap (04b's walls): scales bump, base, -, detail; prt_scarab's
	cube two tints (purple straight on, teal glancing), then specular,
	glancing; prt_lightmap's three whites. Unread, they drew their base map
	flat, the Scarab a dark blue silhouette) */
	{ "prt_simple", 0, 1, 2, -1, 3, 2, 0, -1, C_PC(0), C_PC(0), C_PC(1), C_PC(1), -1, -1, _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "prt_simple@old", 0, 1, 2, -1, 3, 1, 0, -1, C_PC(0), C_PC(0), C_PC(1), C_PC(1), -1, -1, _h2_lightmap_type_default, FALSE },
	{ "prt_scarab", 0, 1, 2, -1, 3, 1, 3, -1, C_PC(2), C_PC(3), C_PC(0), C_PC(1), -1, -1, _h2_lightmap_type_default, FALSE, TRUE, 0 },
	{ "prt_scarab@old", 0, 1, 2, -1, 3, 0, 3, -1, C_PC(2), C_PC(3), C_PC(0), C_PC(1), -1, -1, _h2_lightmap_type_default, FALSE },
	{ "prt_lightmap", 0, 1, 2, -1, 3, 1, 3, -1, C_PC(1), C_PC(2), C_PC(0), C_PC(0), -1, -1, _h2_lightmap_type_default, FALSE, TRUE, 0 },
	{ "prt_lightmap@old", 0, 1, 2, -1, 3, 0, 3, -1, C_PC(1), C_PC(2), C_PC(0), C_PC(0), -1, -1, _h2_lightmap_type_default, FALSE },
	{ "prt_simple_lm_emissive", 0, 1, 2, -1, 4, 1, 3, -1, C_PC(2), C_PC(3), C_PC(0), C_PC(0), -1, -1, _h2_lightmap_type_default, FALSE, TRUE, 0 },
	{ "prt_simple_lm_emissive@old", 0, 1, 2, -1, 4, 0, 3, -1, C_PC(2), C_PC(3), C_PC(0), C_PC(0), -1, -1, _h2_lightmap_type_default, FALSE },
	/* (round 3: every opaque template the MCC maps use, read off their
	constants against the H2EK's parameters over all 40 maps -
	a local comparison script, private. The self-illuminated
	ones' glow map stays h2_level.c's (its illum paths); these give them
	their bump map, detail scale and specular) */
	/* bump, base, detail; glow map 3 */
	{ "tex_bump_illum", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_illum_bloom", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_illum_3_channel", 0, 1, -1, -1, -1, 0, -1, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_illum_3_channel@old", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_plasma_one_channel_illum", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_illum_detail_honor_guard", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(1), C_PC(2), ENV_NONE, _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_illum_detail_honor_guard@old", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(1), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_illum_alpha_test_single_pass", 0, 1, -1, -1, -1, 0, -1, -1, C_PC(0), C_PC(2), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_plasma", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_plasma@old", 0, 1, -1, -1, -1, 0, -1, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	/* change colours (the characters' skins, the Prophets): bump, base, detail, mask */
	{ "tex_bump_one_change_color", 0, 1, -1, -1, -1, 0, -1, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_two_change_color", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_four_change_color", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	/* active camouflage: bump, its camouflage bump, base, detail */
	{ "tex_bump_active_camo", 0, 2, 3, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	/* two details (the second left out: this engine's blend is the base's alpha's) */
	{ "tex_bump_two_detail", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(2), C_PC(3), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_detail_overlay", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(2), C_PC(3), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_tiling_specular", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(1), C_PC(2), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_alpha_test_clamped", 0, 2, 3, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_alpha_test_clamped@old", 0, 2, -1, -1, -1, 0, -1, -1, -1, -1, ENV_NONE, _h2_lightmap_type_default, FALSE },
	/* environment mapped */
	{ "tex_bump_env_illum_3_channel_combined", 0, 1, 2, -1, 3, 0, 2, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_illum_3_channel_combined@old", 0, 1, 2, -1, 3, -1, 2, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_fast_masked", 0, 1, 2, -1, 3, 0, 2, -1, C_PC(0), C_PC(1), C_VC(7), C_VC(8), V_VC(9, 3), V_VC(10, 3), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_fast_masked@old", 0, 1, 2, -1, 3, -1, 2, -1, C_PC(0), C_PC(1), C_VC(7), C_VC(8), V_VC(9, 3), V_VC(10, 3), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_dbl_spec", 0, 2, 3, -1, 4, 0, 2, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_dbl_spec@old", 0, 2, 3, -1, 4, -1, 2, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_two_change_color_indexed", 0, 1, 2, -1, 4, 0, 2, -1, C_PC(0), C_PC(1), C_PC(4), C_PC(5), V_PC(6), V_PC(7), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_two_change_color_indexed@old", 0, 1, 2, -1, 4, -1, 2, -1, C_PC(0), C_PC(1), C_PC(4), C_PC(5), V_PC(6), V_PC(7), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_alpha_test_combined", 0, 2, 3, -1, 4, 0, 2, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_alpha_test_combined@old", 0, 2, -1, -1, 4, 0, -1, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_alpha_test_indexed", 0, 2, 3, -1, 4, 0, 2, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_alpha_test_indexed@old", 0, 2, 3, -1, 4, -1, -1, -1, C_PC(4), C_PC(5), C_PC(0), C_PC(1), V_PC(2), V_PC(3), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_two_detail", 0, 1, 2, -1, 4, 0, 2, -1, C_PC(2), C_PC(3), C_PC(6), C_PC(7), V_PC(8), V_PC(9), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_two_detail_combined", 0, 1, 2, -1, 4, 0, 2, -1, C_PC(2), C_PC(3), C_PC(6), C_PC(7), V_PC(8), V_PC(9), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_detail_overlay", 0, 1, 2, -1, 4, 0, 2, -1, C_PC(2), C_PC(3), C_PC(4), C_PC(5), V_PC(6), V_PC(7), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_detail_overlay@old", 0, 1, 2, -1, 4, -1, 2, -1, C_PC(2), C_PC(3), C_PC(4), C_PC(5), V_PC(6), V_PC(7), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_detail_overlay_combined", 0, 1, 2, -1, 4, 0, 2, -1, C_PC(2), C_PC(3), C_PC(4), C_PC(5), V_PC(6), V_PC(7), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_detail_overlay_combined@old", 0, 1, 2, -1, 4, -1, 2, -1, C_PC(2), C_PC(3), C_PC(4), C_PC(5), V_PC(6), V_PC(7), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_two_change_color_combined", 0, 1, 2, -1, 4, 0, 2, -1, C_PC(0), C_PC(1), C_PC(2), C_PC(3), V_PC(4), V_PC(5), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_two_change_color_combined@old", 0, 1, 2, -1, 4, -1, 2, -1, C_PC(0), C_PC(1), C_PC(2), C_PC(3), V_PC(4), V_PC(5), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_illum_two_change_color_combined", 0, 1, 2, -1, 4, 0, 2, -1, C_PC(0), C_PC(1), C_PC(3), C_PC(4), V_PC(5), V_PC(5), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_illum_two_change_color_combined@old", 0, 1, -1, -1, 4, -1, -1, -1, C_PC(0), C_PC(1), C_PC(3), C_PC(4), V_PC(5), V_PC(5), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_two_change_color_multiply_map_self_illum", 0, 1, 2, -1, 5, 4, 0, -1, C_PC(0), C_PC(1), C_VC(10), C_VC(11), -1, -1, _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_two_change_color_multiply_map_self_illum@old", 0, 1, -1, -1, 5, 0, -1, -1, C_PC(0), C_PC(1), C_VC(10), C_VC(11), -1, -1, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_illum_four_change_color_no_lod", 0, 2, 3, -1, 6, 0, 2, -1, C_PC(0), C_PC(1), C_VC(4), C_VC(5), V_VC(6, 3), V_VC(7, 3), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_illum_detail_honor_guard", 0, 1, 2, -1, 5, 0, 2, -1, C_PC(1), C_PC(2), C_VC(6), C_VC(7), V_VC(8, 3), V_VC(9, 3), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_illum_detail_honor_guard@old", 0, 1, 2, -1, 5, 0, 2, -1, C_PC(2), C_PC(2), C_VC(6), C_VC(7), V_VC(8, 3), V_VC(9, 3), _h2_lightmap_type_default, FALSE },
	{ "tex_bump_env_illum_3_channel_occlusion_combined", 0, 1, 2, -1, 3, 0, 2, -1, C_PC(0), C_PC(1), C_PC(13), C_PC(14), -1, -1, _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_illum_3_channel_occlusion_combined@old", 0, 1, -1, -1, 3, -1, -1, -1, C_PC(0), C_PC(1), C_PC(13), C_PC(14), -1, -1, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_dprs_env_illum", 0, 1, 2, -1, 3, 0, 2, -1, C_PC(0), C_PC(1), C_VC(8), C_VC(9), V_VC(10, 3), V_VC(11, 3), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_dprs_env_illum@old", 0, 1, 2, -1, 3, -1, 2, -1, C_PC(0), C_PC(0), C_PC(5), C_PC(5), -1, -1, _h2_lightmap_type_default, FALSE },
	/* (round 16: the blended terrains the multiplayer maps' ground uses -
	read off their MCC constants against the H2EK's parameters, private
	tools/tmpl_dump.py. tex_bump_detail_blend_detail: bump, base, blend
	details 1 and 2, overlay detail (left out); scales bump 0, base 1,
	details 2, 3, overlay 4.zw; colours -, -, specular, glancing - Beaver
	Creek's, Burial Mounds' ground. tex_bump_three_detail_blend: bump,
	base, details a, b, c (c left out: this engine blends two by the base's
	alpha); scales as it; specular, glancing first - Backwash's, Dune's.
	tex_detail_blend: base, detail, secondary, no bump (map 3 a flat
	vector); scales detail 2, secondary 3. tex_bump_env_clamped_combined:
	bump, base, detail, cube; detail scale 0; env tint, glancing tint 1, 2,
	brightnesses 3, 4, specular 5, 6. Read as generic they drew their base
	map alone) */
	{ "tex_bump_detail_blend_detail", 0, 1, 2, 3, -1, 0, 2, 3, C_PC(2), C_PC(3), ENV_NONE, _h2_lightmap_type_default, TRUE },
	/* (tex_bump_three_detail_blend: its lightmap pass reads base_map and
	detail_map_a alone - H2EK lightmap_tex_three_detail_blend, its textures
	block and "apply detail map", T0 2 base detail_a; b and c are in no pass
	the template lists. Blended by the base's alpha with b, Relic's beach was
	moss where the tag build has sand. HALO_H2_THREE_DETAIL_OLD=1 the
	blend) */
	{ "tex_bump_three_detail_blend", 0, 1, 2, -1, -1, 0, 2, -1, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, FALSE },
	{ "tex_bump_three_detail_blend@old", 0, 1, 2, 3, -1, 0, 2, 3, C_PC(0), C_PC(1), ENV_NONE, _h2_lightmap_type_default, TRUE },
	{ "tex_detail_blend", 3, 0, 1, 2, -1, -1, 2, 3, C_PC(2), C_PC(3), ENV_NONE, _h2_lightmap_type_default, TRUE },
	{ "tex_bump_env_clamped_combined", 0, 1, 2, -1, 3, 2, 0, -1, C_PC(5), C_PC(6), C_PC(1), C_PC(2), V_PC(3), V_PC(4), _h2_lightmap_type_default, FALSE, TRUE, 1 },
	{ "tex_bump_env_clamped_combined@old", 0, 1, 2, -1, 3, -1, 0, -1, C_PC(5), C_PC(6), C_PC(1), C_PC(2), V_PC(3), V_PC(4), _h2_lightmap_type_default, FALSE },
	/* no bump: the cut-out foliage - its alpha test map 0, base 1, detail 2 */
	{ "tex_alpha_test", -1, 1, 2, -1, -1, -1, 1, -1, -1, -1, ENV_NONE, _h2_lightmap_type_default, FALSE },
};

/* ---------- private code */

H2_ALIGN static float read_float(void const *p)
{
	float f;

	memcpy(&f, p, sizeof(f));
	return f;
}

H2_ALIGN static void write_float(unsigned char *p, float f)
{
	memcpy(p, &f, sizeof(f));
}

H2_ALIGN static void write_long(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

/* (tex_bump_env_illum_combined's old reading: the Vita and its null build,
or HALO_H2_ILLUM_COMBINED_OLD - the material_templates rows) */
H2_ALIGN static boolean illum_combined_old(void)
{
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	return TRUE;
#else
	return getenv("HALO_H2_ILLUM_COMBINED_OLD") != NULL;
#endif
}

/* (the templates whose reads session 20 made the H2EK's - their passes'
constant order, the maps their passes draw: the Vita and its null build,
or HALO_H2_TEMPLATE_READS_OLD, keep the old rows) */
H2_ALIGN static boolean template_reads_old(void)
{
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	return TRUE;
#else
	return getenv("HALO_H2_TEMPLATE_READS_OLD") != NULL;
#endif
}

/* whether a template's old row (name@old) is read instead of its own */
H2_ALIGN static boolean template_old_wanted(char const *name)
{
	if (!strcmp(name, "tex_bump_three_detail_blend"))
		return getenv("HALO_H2_THREE_DETAIL_OLD") != NULL;
	if (!strcmp(name, "tex_bump_env_illum_combined"))
		return illum_combined_old();
	return template_reads_old();
}

H2_ALIGN static struct material_template const *template_find(char const *template_name)
{
	char const *slash = strrchr(template_name, '\\');
	char const *name = slash ? slash + 1 : template_name;
	size_t length = strlen(name);
	struct material_template const *found = NULL, *old = NULL;
	long i;

	if (!strstr(template_name, "\\opaque\\"))
		return NULL;
	for (i = 0; i < (long)(sizeof(material_templates) / sizeof(material_templates[0])); i++)
	{
		char const *row = material_templates[i].name;

		if (!strcmp(row, name))
			found = &material_templates[i];
		else if (!strncmp(row, name, length) && !strcmp(row + length, "@old"))
			old = &material_templates[i];
	}
	/* (a row's old reading kept beside it, name@old) */
	if (old && template_old_wanted(name))
		return old;
	return found;
}

/* a bitmap of the shader's, NONE for none or one of the templates' own
defaults (default_detail, default_vector: a flat bump map...) */
H2_ALIGN static long material_bitmap(struct h2_cache *cache, long const *bitmaps, long count, int index)
{
	char const *name;

	if (index < 0 || index >= count || bitmaps[index] == NONE)
		return NONE;
	name = h2_cache_tag_name(cache, bitmaps[index]);
	if (!name || strstr(name, "\\default_") || strstr(name, "gray_50_percent") || strstr(name, "\\color_"))
		return NONE;
	return bitmaps[index];
}

/* a vertex constant as a map's scale: x y positive, z w 0 (else 1 1) */
H2_ALIGN static void material_scale(float const (*constants)[4], long count, int index, float scale[2])
{
	scale[0] = scale[1] = 1.0f;
	if (index >= 0 && index < count && constants[index][0] > 0.0f && constants[index][1] > 0.0f &&
		constants[index][3] == 0.0f)
	{
		scale[0] = constants[index][0];
		scale[1] = constants[index][1];
	}
}

/* a colour constant (C_PC, C_VC; -1 none: black, or white into rgb):
X8R8G8B8, and as floats into rgb when it is given */
H2_ALIGN static unsigned long material_colour(unsigned long const *colours, long colour_count,
	float const (*constants)[4], long constant_count, int which, float *rgb)
{
	unsigned long packed = 0;
	int c;

	if (which >= 0 && which < 64 && which < colour_count)
		packed = colours[which] & 0xFFFFFF;
	else if (which >= 64 && which - 64 < constant_count)
	{
		for (c = 0; c < 3; c++)
		{
			float f = constants[which - 64][c];
			long v = (long)((f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f) * 255.0f + 0.5f);

			packed |= (unsigned long)v << (16 - 8 * c);
		}
	}
	else
		packed = rgb ? 0xFFFFFF : 0;
	if (rgb)
	{
		for (c = 0; c < 3; c++)
			rgb[c] = ((packed >> (16 - 8 * c)) & 255) / 255.0f;
	}
	return packed;
}

/* a value constant (V_PC: a pixel constant's alpha, V_VC: a vertex
constant's component; -1 none: 1) */
H2_ALIGN static float material_value(unsigned long const *colours, long colour_count,
	float const (*constants)[4], long constant_count, int which)
{
	if (which >= 0 && which < 64 && which < colour_count)
		return (colours[which] >> 24) / 255.0f;
	if (which >= 64 && (which - 64) / 4 < constant_count)
		return constants[(which - 64) / 4][(which - 64) % 4];
	return 1.0f;
}

/* ---------- public code */

H2_ALIGN boolean h2_material_read(
	struct h2_cache *cache,
	unsigned long shader_address,
	char const *template_name,
	struct h2_material *material)
{
	struct material_template const *t = template_find(template_name ? template_name : "");
	long definition_count, bitmap_count = 0, constant_count = 0, colour_count = 0, k;
	unsigned long definitions, bitmaps, constants, colours;
	long bitmap_tags[MAXIMUM_BITMAPS];
	float scales[MAXIMUM_CONSTANTS][4];
	unsigned long colour_values[MAXIMUM_CONSTANTS];

	memset(material, 0, sizeof(*material));
	material->bump_tag = material->base_tag = material->detail_tag = material->secondary_tag = material->cube_tag = material->change_colour_tag = NONE;
	material->alpha_test_tag = material->overlay_tag = NONE;
	material->overlay_scale[0] = material->overlay_scale[1] = 1.0f;
	material->bump_scale[0] = material->bump_scale[1] = 1.0f;
	material->detail_scale[0] = material->detail_scale[1] = 1.0f;
	material->secondary_scale[0] = material->secondary_scale[1] = 1.0f;
	material->base_scale[0] = material->base_scale[1] = 1.0f;
	if (!t || !h2_cache_block(cache, shader_address + 0x20, 0x7C, &definition_count, &definitions) ||
		definition_count < 1 || !h2_cache_block(cache, definitions + 4, 0xC, &bitmap_count, &bitmaps))
	{
		return FALSE;
	}
	if (bitmap_count > MAXIMUM_BITMAPS)
		bitmap_count = MAXIMUM_BITMAPS;
	for (k = 0; k < bitmap_count; k++)
		bitmap_tags[k] = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * k));
	if (h2_cache_block(cache, definitions + 0x14, 0x10, &constant_count, &constants))
	{
		if (constant_count > MAXIMUM_CONSTANTS)
			constant_count = MAXIMUM_CONSTANTS;
		for (k = 0; k < constant_count; k++)
		{
			unsigned char const *c = h2_cache_at(cache, constants + 0x10 * k, 16);
			int axis;

			for (axis = 0; axis < 4; axis++)
				scales[k][axis] = c ? read_float(c + 4 * axis) : 0.0f;
		}
	}
	else
		constant_count = 0;
	if (h2_cache_block(cache, definitions + 0x0C, 4, &colour_count, &colours))
	{
		if (colour_count > MAXIMUM_CONSTANTS)
			colour_count = MAXIMUM_CONSTANTS;
		for (k = 0; k < colour_count; k++)
			colour_values[k] = h2_cache_read32(cache, colours + 4 * k);
	}
	else
		colour_count = 0;

	material->known = TRUE;
	material->prt = strstr(template_name, "\\prt_") != NULL;
	material->blended = t->blended;
	material->lightmap_type = t->lightmap_type;
	/* (the shader's own lightmap type - after its light response (0x34),
	shader LOD bias and specular type (0x3C, 0x3E): 03a's rubble's diffuse,
	no specular, drawn shiny gold by the template's white specular) */
	{
		unsigned char const *p = h2_cache_at(cache, shader_address + 0x40, 2);
		short type = p ? (short)(p[0] | p[1] << 8) : -1;

		material->shader_lightmap_type = type >= 0 && type <= 3 ? (signed char)type : -1;
		p = h2_cache_at(cache, shader_address + 0x3E, 2);
		type = p ? (short)(p[0] | p[1] << 8) : -1;
		material->shader_specular_type = type >= 0 && type <= 3 ? (signed char)type : -1;
		/* (h2/render-accuracy: its "lightmap specular brightness" after the
		lightmap type - the H2EK's shader tag 0x44; Lockout's walls 0.1..0.6,
		the objects' 1) */
		p = h2_cache_at(cache, shader_address + 0x44, 4);
		material->lightmap_specular_brightness = p ? read_float(p) : -1.0f;
		if (!(material->lightmap_specular_brightness >= 0.0f && material->lightmap_specular_brightness < 64.0f))
			material->lightmap_specular_brightness = -1.0f;
	}
	{
		char const *leaf = strrchr(template_name, '\\');

		leaf = leaf ? leaf + 1 : template_name;
		material->specular_mask = template_listed(specular_mask_double_templates, leaf) ? _h2_material_specular_mask_double_bit :
			template_listed(specular_mask_base_templates, leaf) ? _h2_material_specular_mask_base_bit :
			template_listed(specular_masked_out_templates, leaf) ? _h2_material_specular_masked_out_bit : 0;
	}
	material->bump_tag = material_bitmap(cache, bitmap_tags, bitmap_count, t->map_bump);
	material->base_tag = material_bitmap(cache, bitmap_tags, bitmap_count, t->map_base);
	material->detail_tag = material_bitmap(cache, bitmap_tags, bitmap_count, t->map_detail);
	material->secondary_tag = material_bitmap(cache, bitmap_tags, bitmap_count, t->map_secondary);
	material->cube_tag = t->map_cube >= 0 && t->map_cube < bitmap_count ? bitmap_tags[t->map_cube] : NONE;
	/* (tex_bump_alpha_test...: their second map the one whose alpha they
	test - the H2EK's "alpha test map"; color_white, a default: none) */
	if (!strncmp(t->name, "tex_bump_alpha_test", 19))
		material->alpha_test_tag = material_bitmap(cache, bitmap_tags, bitmap_count, 1);
	/* (tex_alpha_test: its first) */
	else if (!strcmp(t->name, "tex_alpha_test"))
		material->alpha_test_tag = material_bitmap(cache, bitmap_tags, bitmap_count, 0);
	/* (a change colour template's mask: the bitmap named as one - _cc,
	change_color, color_change - as h2_biped.c finds the armour's) */
	if (strstr(template_name, "change_color"))
	{
		for (k = 0; k < bitmap_count; k++)
		{
			char const *name = bitmap_tags[k] != NONE ? h2_cache_tag_name(cache, bitmap_tags[k]) : NULL;
			size_t length = name ? strlen(name) : 0;

			if (name && (strstr(name, "change_color") || strstr(name, "chg_color") || strstr(name, "color_change") ||
				(length > 3 && !strcmp(name + length - 3, "_cc"))))
			{
				material->change_colour_tag = bitmap_tags[k];
				break;
			}
		}
	}
	material_scale((float const (*)[4])scales, constant_count, t->scale_bump, material->bump_scale);
	material_scale((float const (*)[4])scales, constant_count, t->scale_detail, material->detail_scale);
	material_scale((float const (*)[4])scales, constant_count, t->scale_secondary, material->secondary_scale);
	/* (the templates whose vertex constants are bump, base, detail - H2EK's
	tex_bump* parameters in order: base_map then detail_map, each its own
	scale; the alpha test ones keep another map there) */
	if (t->own_base_scale)
		material_scale((float const (*)[4])scales, constant_count, t->scale_base, material->base_scale);
	else if (t->scale_bump == 0 && t->scale_detail == 2 && !t->blended && !strstr(t->name, "alpha") && !strstr(t->name, "keep"))
		material_scale((float const (*)[4])scales, constant_count, 1, material->base_scale);
	/* (tex_bump_env_illum_combined's: detail, base, bump - its base the second too) */
	else if (!strcmp(t->name, "tex_bump_env_illum_combined"))
		material_scale((float const (*)[4])scales, constant_count, 1, material->base_scale);
	material->specular_color = material_colour(colour_values, colour_count, (float const (*)[4])scales, constant_count,
		t->specular, NULL);
	material->specular_glancing_color = material_colour(colour_values, colour_count, (float const (*)[4])scales,
		constant_count, t->glancing, NULL);
	if (material->cube_tag != NONE)
	{
		material->has_env = TRUE;
		material->env_pass = strstr(template_name, "_dprs_") != NULL;
		/* (the PC's first implementation of tex_bump_env_detail_overlay(_combined):
		lightmap_tex_detail_overlay writes its mask 0 - T0a= 0 -, the
		environment map pass after it adds its reflection times that) */
		material->env_masked_out = strstr(template_name, "tex_bump_env_detail_overlay") != NULL;
		material_colour(colour_values, colour_count, (float const (*)[4])scales, constant_count, t->env_tint,
			material->env_tint);
		material_colour(colour_values, colour_count, (float const (*)[4])scales, constant_count, t->env_glancing_tint,
			material->env_glancing_tint);
		material->env_brightness = material_value(colour_values, colour_count, (float const (*)[4])scales,
			constant_count, t->env_brightness);
		material->env_glancing_brightness = material_value(colour_values, colour_count, (float const (*)[4])scales,
			constant_count, t->env_glancing_brightness);
	}
	/* (tex_bump_detail_blend_detail's overlay detail, map 4: the pass
	(H2EK pixel_shaders_dx9 lightmap_tex_detail_blend_detail) multiplies the
	blended details by twice it - left out before; its scale the vertex
	constant 4's z and w (x and y the second detail's offset), else its x
	and y. HALO_H2_NO_OVERLAY=1: left out, as before) */
	{
		/* (tex_bump_detail_overlay: R0 = 2 detail x secondary detail, the
		secondary map 3 - H2EK lightmap_tex_detail_overlay; its scale the
		constant 3's x and y, else z and w) */
		int overlay_map = -1, overlay_constant = -1;

		if (!strcmp(t->name, "tex_bump_detail_blend_detail"))
			overlay_map = 4, overlay_constant = 4;
		else if (!strcmp(t->name, "tex_bump_detail_overlay"))
			overlay_map = 3, overlay_constant = 3;
		if (overlay_map >= 0 && bitmap_count > overlay_map && !getenv("HALO_H2_NO_OVERLAY"))
		{
			int c = overlay_constant;

			material->overlay_tag = material_bitmap(cache, bitmap_tags, bitmap_count, overlay_map);
			if (constant_count > c && scales[c][0] > 0.0f && scales[c][1] > 0.0f && scales[c][3] == 0.0f)
				material_scale((float const (*)[4])scales, constant_count, c, material->overlay_scale);
			else if (constant_count > c && scales[c][2] > 0.0f && scales[c][3] > 0.0f)
			{
				material->overlay_scale[0] = scales[c][2];
				material->overlay_scale[1] = scales[c][3];
			}
			if (getenv("HALO_H2_DEBUG"))
			{
				int k2;

				fprintf(stderr, "h2 materials: overlay (%s) %s scale %.2f %.2f; constants:", t->name,
					material->overlay_tag != NONE ? h2_cache_tag_name(cache, material->overlay_tag) : "-",
					material->overlay_scale[0], material->overlay_scale[1]);
				for (k2 = 0; k2 < constant_count && k2 < 6; k2++)
					fprintf(stderr, " [%.2f %.2f %.2f %.2f]", scales[k2][0], scales[k2][1], scales[k2][2], scales[k2][3]);
				fprintf(stderr, " maps:");
				for (k2 = 0; k2 < bitmap_count && k2 < 6; k2++)
					fprintf(stderr, " %s", bitmap_tags[k2] != NONE ? h2_cache_tag_name(cache, bitmap_tags[k2]) : "-");
				fprintf(stderr, "\n");
			}
		}
	}
	/* (tex_bump_three_detail_blend: its maps bump, base, detail a, b, c;
	scales bump 0, base 1, a 2, b 3, c 4 (or 4's z and w). Its PC pass
	(H2EK lightmap_tex_three_detail_blend, implementation 0: stages base,
	a, bump, lightmap, b, c - kit pass XML) blends all three by the base's
	alpha: x1 = sat(2 a), x2 = sat(2 a - 1); lerp(a, b, x1) then lerp(., c,
	x2), times twice the base. Before (and HALO_H2_THREE_DETAIL_A_ONLY=1)
	detail a alone - its textures block was read as binding a alone) */
	if (!strcmp(t->name, "tex_bump_three_detail_blend") && !getenv("HALO_H2_THREE_DETAIL_A_ONLY"))
	{
		long b = material_bitmap(cache, bitmap_tags, bitmap_count, 3), c = material_bitmap(cache, bitmap_tags, bitmap_count, 4);

		if (material->detail_tag != NONE && b != NONE && c != NONE)
		{
			material->three_detail = TRUE;
			material->secondary_tag = b;
			material_scale((float const (*)[4])scales, constant_count, 3, material->secondary_scale);
			material->overlay_tag = c;
			if (constant_count > 4 && scales[4][0] > 0.0f && scales[4][1] > 0.0f && scales[4][3] == 0.0f)
				material_scale((float const (*)[4])scales, constant_count, 4, material->overlay_scale);
			else if (constant_count > 4 && scales[4][2] > 0.0f && scales[4][3] > 0.0f)
			{
				material->overlay_scale[0] = scales[4][2];
				material->overlay_scale[1] = scales[4][3];
			}
		}
		if (getenv("HALO_H2_DEBUG"))
		{
			int k2;

			fprintf(stderr, "h2 materials: three detail %s: a %s x%.1f b %s x%.1f c %s x%.1f; constants:", material->three_detail ? "on" : "off",
				material->detail_tag != NONE ? h2_cache_tag_name(cache, material->detail_tag) : "-", material->detail_scale[0],
				b != NONE ? h2_cache_tag_name(cache, b) : "-", material->secondary_scale[0],
				c != NONE ? h2_cache_tag_name(cache, c) : "-", material->overlay_scale[0]);
			for (k2 = 0; k2 < constant_count && k2 < 6; k2++)
				fprintf(stderr, " [%.2f %.2f %.2f %.2f]", scales[k2][0], scales[k2][1], scales[k2][2], scales[k2][3]);
			fprintf(stderr, "\n");
		}
	}
	/* (a blend whose two maps are one: a single detail) */
	if (material->blended && (material->secondary_tag == NONE || material->secondary_tag == material->detail_tag))
	{
		material->blended = FALSE;
		material->secondary_tag = NONE;
	}

	return TRUE;
}

H2_ALIGN void h2_material_shader_write(
	unsigned char *shader,
	struct h2_material const *material,
	boolean bumped)
{
	unsigned char *extension = shader + H2_MATERIAL_EXTENSION_OFFSET;
	unsigned short flags = _h2_material_relative_bump_bit;
	float primary_aspect, secondary_aspect;

	/* (a template this file does not read: the extension still, so its
	lightmap is Halo 2's - the incident directions are Halo 2's, which this
	engine's own pass would darken - its bump map's scale as written) */
	if (!material->known)
	{
		if (bumped)
		{
			memcpy(shader + 0x138, shader + 0x124, 4);
			memcpy(shader + 0x13C, shader + 0x124, 4);
		}
		memset(extension, 0, H2_MATERIAL_EXTENSION_SIZE);
		write_long(extension, H2_MATERIAL_MAGIC);
		extension[4] = H2_MATERIAL_VERSION;
		extension[5] = _h2_lightmap_type_default;
		if (material->glow_added)
			flags |= _h2_material_glow_added_bit;
		extension[6] = (unsigned char)flags;
		extension[7] = (unsigned char)(flags >> 8);
		write_float(extension + 8, 1.0f);
		write_float(extension + 12, 1.0f);
		return;
	}
	/* (type: blended, its primary detail map (where the base's alpha is 1)
	Halo 2's secondary: h2_level.c writes them so) */
	shader[0x2A] = material->blended ? 1 : 0;
	shader[0x2B] = 0;
	/* (the bump map's runtime scale: the base's coordinates times it, as
	Halo 2's two) */
	if (bumped)
	{
		write_float(shader + 0x124, material->bump_scale[0]);
		write_float(shader + 0x138, material->bump_scale[0]);
		write_float(shader + 0x13C, material->bump_scale[1]);
	}
	primary_aspect = material->blended ?
		material->secondary_scale[1] / material->secondary_scale[0] : material->detail_scale[1] / material->detail_scale[0];
	secondary_aspect = material->blended ? material->detail_scale[1] / material->detail_scale[0] :
		material->three_detail ? material->secondary_scale[1] / material->secondary_scale[0] : 1.0f;
	if (primary_aspect != 1.0f || secondary_aspect != 1.0f)
		flags |= _h2_material_detail_aspect_bit;
	/* (the lightmap's specular pass as the shader's lightmap type says: none
	for a diffuse one, its curve for the others - HALO_H2_SHADER_LIGHTMAP_TYPE_OLD
	the template's alone) */
	{
		boolean own = material->shader_lightmap_type >= 0 && !getenv("HALO_H2_SHADER_LIGHTMAP_TYPE_OLD");
		unsigned char lightmap_type = material->lightmap_type;

		if (own && material->shader_lightmap_type > 0)
			lightmap_type = material->shader_lightmap_type == 2 ? _h2_lightmap_type_dull :
				material->shader_lightmap_type == 3 ? _h2_lightmap_type_shiny : _h2_lightmap_type_default;
		if ((material->specular_color & 0xFFFFFF) && !(own && material->shader_lightmap_type == 0))
			flags |= _h2_material_specular_bit;
		memset(extension, 0, H2_MATERIAL_EXTENSION_SIZE);
		write_long(extension, H2_MATERIAL_MAGIC);
		extension[4] = H2_MATERIAL_VERSION;
		extension[5] = lightmap_type;
	}
	if (material->has_env && material->env_masked_out)
		flags |= _h2_material_env_masked_out_bit;
	if (material->three_detail)
		flags |= _h2_material_three_detail_bit;
	if (material->glow_added)
		flags |= _h2_material_glow_added_bit;
	extension[6] = (unsigned char)flags;
	extension[7] = (unsigned char)(flags >> 8);
	write_float(extension + 8, primary_aspect);
	write_float(extension + 12, secondary_aspect);
	/* (h2/render-accuracy: the shader's own lightmap specular brightness in
	the specular colour's top byte, times 64 - HALO_H2_SPECULAR_OLD's pass
	reads the colour's rgb alone) */
	if (material->lightmap_specular_brightness >= 0.0f)
	{
		float brightness = material->lightmap_specular_brightness * 64.0f + 0.5f;
		unsigned long byte = brightness > 255.0f ? 255UL : (unsigned long)brightness;

		flags |= _h2_material_specular_brightness_bit | material->specular_mask;
		extension[6] = (unsigned char)flags;
		extension[7] = (unsigned char)(flags >> 8);
		write_long(extension + 16, (material->specular_color & 0xFFFFFFUL) | (byte << 24));
	}
	else
		write_long(extension + 16, material->specular_color);
	write_long(extension + 20, material->specular_glancing_color);
}

H2_ALIGN unsigned char *h2_material_alpha_mask(
	unsigned char const *pixels,
	unsigned long size,
	unsigned long width,
	unsigned long height,
	unsigned short format,
	unsigned long side)
{
	unsigned long bw = (width + 3) / 4, bh = (height + 3) / 4, x, y;
	unsigned char *blocks, *out;

	if (!pixels || !width || !height || !side)
		return NULL;
	if (format == 10 || format == 11)
	{
		bw = width;
		bh = height;
	}
	else if (format != 15 && format != 16)
		return NULL;
	if ((format == 15 || format == 16) && size < bw * bh * 16)
		return NULL;
	if ((format == 10 || format == 11) && size < width * height * 4)
		return NULL;
	blocks = malloc(bw * bh);
	out = malloc(side * side);
	if (!blocks || !out)
	{
		free(blocks);
		free(out);
		return NULL;
	}
	/* (each 4x4 block's average alpha; a 32-bit map's alpha as it is) */
	for (y = 0; y < bh; y++)
	{
		for (x = 0; x < bw; x++)
		{
			unsigned char const *b = format == 10 || format == 11 ? pixels + 4 * (y * width + x) : pixels + 16 * (y * bw + x);
			unsigned long sum = 0;
			int i;

			if (format == 10)
				sum = 255 * 16;
			else if (format == 11)
				sum = (unsigned long)b[3] * 16;
			else if (format == 15)
			{
				for (i = 0; i < 16; i++)
					sum += ((b[i / 2] >> (4 * (i & 1))) & 15) * 17;
			}
			else
			{
				unsigned long a0 = b[0], a1 = b[1], palette[8];
				unsigned long long bits = 0;

				for (i = 0; i < 6; i++)
					bits |= (unsigned long long)b[2 + i] << (8 * i);
				palette[0] = a0;
				palette[1] = a1;
				for (i = 0; i < 6; i++)
					palette[2 + i] = a0 > a1 ? ((6 - i) * a0 + (1 + i) * a1) / 7 : i < 4 ? ((4 - i) * a0 + (1 + i) * a1) / 5 :
						i == 4 ? 0 : 255;
				for (i = 0; i < 16; i++)
					sum += palette[(bits >> (3 * i)) & 7];
			}
			blocks[y * bw + x] = (unsigned char)(sum / 16);
		}
	}
	/* (the blocks under each texel of the mask, averaged) */
	for (y = 0; y < side; y++)
	{
		for (x = 0; x < side; x++)
		{
			unsigned long x0 = x * bw / side, x1 = (x + 1) * bw / side, y0 = y * bh / side, y1 = (y + 1) * bh / side;
			unsigned long sum = 0, n = 0, sx, sy;

			if (x1 <= x0)
				x1 = x0 + 1;
			if (y1 <= y0)
				y1 = y0 + 1;
			for (sy = y0; sy < y1 && sy < bh; sy++)
			{
				for (sx = x0; sx < x1 && sx < bw; sx++)
				{
					sum += blocks[sy * bw + sx];
					n++;
				}
			}
			out[y * side + x] = (unsigned char)(n ? sum / n : 0);
		}
	}
	free(blocks);
	return out;
}

H2_ALIGN void h2_material_model_shader_write(
	unsigned char *shader,
	struct h2_material const *material,
	unsigned long bump_map)
{
	unsigned char *extension = shader + H2_MODEL_MATERIAL_EXTENSION_OFFSET;

	if (!material->known)
		return;
	memset(extension, 0, H2_MODEL_MATERIAL_EXTENSION_SIZE);
	write_long(extension, H2_MODEL_MATERIAL_MAGIC);
	write_long(extension + 4, bump_map);
	write_float(extension + 8, material->bump_scale[0]);
	write_float(extension + 12, material->bump_scale[1]);
	write_long(extension + 16, material->specular_color);
	write_long(extension + 20, material->specular_glancing_color);
	/* (Halo 2's object light passes switch their highlight's curve on the
	shader's own specular type - the H2EK's "specular type", not its lightmap
	type: a shader with a diffuse lightmap can still have a dull or shiny
	highlight under the dynamic light (the battle rifle's plastic, the sniper
	rifle's grip); one not read: the template's lightmap type's curve) */
	extension[24] = material->shader_specular_type >= 0 ? (unsigned char)material->shader_specular_type :
		material->lightmap_type == _h2_lightmap_type_dull ? _h2_specular_type_dull :
		material->lightmap_type == _h2_lightmap_type_shiny ? _h2_specular_type_shiny : _h2_specular_type_default;
	extension[25] = (material->has_env ? _h2_model_material_env_specular_bit : 0) |
		(material->has_env && material->env_pass ? _h2_model_material_env_pass_bit : 0);
}

H2_ALIGN void h2_material_model_detail_alpha_write(
	unsigned char *shader,
	float alpha)
{
	unsigned char *extension = shader + H2_MODEL_MATERIAL_EXTENSION_OFFSET;
	long value = (long)(alpha * 65535.0f + 0.5f);

	if (value < 1)
		value = 1;
	if (value > 65535)
		value = 65535;
	extension[26] = (unsigned char)(value & 255);
	extension[27] = (unsigned char)(value >> 8);
}

/* the glows Halo 2 colours by an animated function of the object's health
(H2EK: its channel colours' functions of "health" - the map keeps one end):
their colour whole, the amber of the fusion coils' channel B at full health
(the owner's frames: amber H panels; whiter as they are hurt - the object's
change colour A, h2_level.c) */
static struct
{
	char const *name;
	float colour[3];
} const glow_colours[] =
{
	{ "\\fusion_coil_core", { 1.0f, 0.56f, 0.17f } },
	{ "\\fusion_bolt", { 1.0f, 0.56f, 0.17f } },
};

H2_ALIGN boolean h2_material_glow_colour(
	char const *shader_name,
	float *colour)
{
	long i;

	if (!shader_name || getenv("HALO_H2_MATERIALS_OLD"))
		return FALSE;
	for (i = 0; i < (long)(sizeof(glow_colours) / sizeof(glow_colours[0])); i++)
	{
		char const *at = strstr(shader_name, glow_colours[i].name);

		if (at && !at[strlen(glow_colours[i].name)])
		{
			memcpy(colour, glow_colours[i].colour, sizeof(glow_colours[i].colour));
			return TRUE;
		}
	}
	return FALSE;
}

/* (channel_a, b and c_color at the coil's full health: the first stops of
their health functions in fusion_coil_core, little endian as a pixel
constant: c is the H panels' amber the owner's frames show, a and b the
plasma's blue-white) */
H2_ALIGN boolean h2_material_plasma_illum_colours(
	char const *shader_name,
	unsigned long *colours)
{
	if (!shader_name || (!strstr(shader_name, "\\fusion_coil_core") && !strstr(shader_name, "\\fusion_bolt")))
		return FALSE;
	colours[0] = 0xAEFBFFUL;
	colours[1] = 0x2B8FFFUL;
	colours[2] = 0xC67700UL;
	return TRUE;
}

H2_ALIGN boolean h2_material_shell_hidden(
	char const *shader_name)
{
	return shader_name && strstr(shader_name, "\\fusion_coil_bloom") != NULL;
}

/* ---------- change colours */

/* (an H2 object tag: its default model variant 0x30, model 0x34, change
colors 0x10 each at 0xAC - their initial permutations 0x20 each: weight,
lower bound, upper bound, variant name; the hlmt's variants 0x38 each at
0x50, their names first) */
H2_ALIGN static boolean change_colour_name_empty(
	struct h2_cache *cache,
	unsigned long id)
{
	char name[64];

	return !id || id == 0xFFFFFFFFUL || !h2_cache_string(cache, id, name, sizeof(name)) || !name[0];
}

H2_ALIGN boolean h2_change_colours_read(
	struct h2_cache *cache,
	long h2_object_tag,
	struct h2_change_colours *colours)
{
	struct h2_cache_tag t, h;
	long colour_count = 0, variant_count = 0, i, hlmt;
	unsigned long colour_elements, variants = 0, default_variant = 0;
	boolean any = FALSE;
	int k;

	memset(colours, 0, sizeof(*colours));
	if (h2_object_tag == NONE || !h2_cache_tag_get(cache, h2_object_tag, &t) ||
		!h2_cache_block(cache, t.address + 0xAC, 0x10, &colour_count, &colour_elements) || colour_count < 1)
	{
		return FALSE;
	}
	/* (its variants: the default one - "default", else one named
	default..., else the first, as h2_biped.c shows it - and each) */
	hlmt = h2_cache_reference(cache, t.address + 0x34);
	if (hlmt != NONE && h2_cache_tag_get(cache, hlmt, &h) &&
		h2_cache_block(cache, h.address + 0x50, 0x38, &variant_count, &variants) && variant_count > 0)
	{
		long chosen = NONE, pass;

		if (variant_count > 31)
			variant_count = 31;
		for (pass = 0; pass < 2 && chosen == NONE; pass++)
		{
			for (i = 0; i < variant_count && chosen == NONE; i++)
			{
				char name[64];

				if (h2_cache_string(cache, h2_cache_read32(cache, variants + 0x38 * i), name, sizeof(name)) &&
					(pass ? !strncmp(name, "default", 7) : !strcmp(name, "default")))
				{
					chosen = i;
				}
			}
		}
		default_variant = h2_cache_read32(cache, variants + 0x38 * (chosen != NONE ? chosen : 0));
	}
	else
		variant_count = 0;
	/* (Halo 2's primary this engine's C, its secondary D) */
	for (k = 0; k < 2 && k < colour_count; k++)
	{
		long permutation_count = 0, v;
		unsigned long permutations;
		struct h2_change_colour_permutation *out = colours->permutations[2 + k];
		short *count = &colours->count[2 + k];
		long default_count = 0;

		if (!h2_cache_block(cache, colour_elements + 0x10 * k, 0x20, &permutation_count, &permutations) || permutation_count < 1)
			continue;
		for (v = 0; v <= variant_count; v++)
		{
			unsigned long variant = v ? h2_cache_read32(cache, variants + 0x38 * (v - 1)) : default_variant;
			float total = 0.0f, sum = 0.0f;
			long n = 0, first = *count, j;

			for (i = 0; i < permutation_count; i++)
			{
				unsigned char const *p = h2_cache_at(cache, permutations + 0x20 * i, 0x20);
				unsigned long name;
				float weight;

				if (!p)
					continue;
				name = (unsigned long)p[0x1C] | ((unsigned long)p[0x1D] << 8) | ((unsigned long)p[0x1E] << 16) |
					((unsigned long)p[0x1F] << 24);
				if (!(name == variant && variant) && !change_colour_name_empty(cache, name))
					continue;
				if (*count >= H2_CHANGE_COLOUR_PERMUTATIONS)
					break;
				memcpy(&weight, p, 4);
				out[*count].weight = weight > 0.0f ? weight : 0.0f;
				memcpy(out[*count].lower, p + 4, 12);
				memcpy(out[*count].upper, p + 0x10, 12);
				total += out[*count].weight;
				(*count)++;
				n++;
			}
			/* (a variant named by none while the default is: Halo 2 leaves
			it white - 07a's Tartarus, the Brute biped's "tartarus" variant,
			white-furred, not a Brute's brown) */
			if (v && !n && default_count && *count < H2_CHANGE_COLOUR_PERMUTATIONS)
			{
				int c;

				for (c = 0; c < 3; c++)
					out[*count].lower[c] = out[*count].upper[c] = 1.0f;
				out[*count].weight = 1.0f;
				total = 1.0f;
				(*count)++;
				n = 1;
			}
			/* (a variant's the same as the default's: none of its own) */
			if (v == 0)
				default_count = n;
			else if (n && n == default_count)
			{
				for (j = 0; j < n && !memcmp(out[first + j].lower, out[j].lower, sizeof(out[j].lower) * 2); j++)
					;
				if (j == n)
				{
					*count = (short)first;
					n = 0;
				}
			}
			/* (their weights rising to 1 - just under for a variant's
			group, its number added) */
			for (j = 0; j < n; j++)
			{
				sum += total > 0.0f ? out[first + j].weight / total : 1.0f / (float)n;
				out[first + j].weight = v ? (float)v + (j == n - 1 ? 0.999f : sum * 0.999f) : (j == n - 1 ? 1.0f : sum);
			}
			any |= n > 0;
		}
	}
	return any;
}
