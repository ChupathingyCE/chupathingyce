/*
H2_LIGHTING.H

Halo 2's lighting on a converted Halo 2 level (h2_level.c builds the level;
this module decides how its light is carried into this engine's).

What Halo 2 keeps for a level's light (Assembly's Halo 2 MCC plugins,
checked against the owner's zanzibar.map):
- the lightmap (ltmp): one group with the section palettes (0x08, Xbox
  only), the bitmap of pages (0x18: MCC's are 32-bit BGRA, linear 0..1,
  alpha unused), each cluster's and each instance's page (0x28, 0x48), two
  lighting environments (0x38: order-2 spherical harmonics, a mean light
  direction and intensity - Zanzibar has 2, not a grid), geometry buckets
  (0x40: per-vertex incident direction, and colour for instances with no
  page), scenery object buckets;
- each cluster's render geometry: its lightmap coordinates (MCC vertex
  buffer 0x1D) and the incident radiance direction of each vertex (0x2F:
  three floats, toward the light, its length the light's directionality);
  the section's lighting flags (0x26: 1 coordinates, 2 incident radiance);
- the sky (sky ): one light - yaw 168, pitch 36 on Zanzibar, colour 0.906
  0.753 0.588 (radiosity block), power 0.2, a lens flare (the sun) -
  indoor/outdoor ambient (Zanzibar: none), clear colour, bloom overrides
  (Zanzibar: none); no fog blocks;
- the scenario: light fixtures (0xC8: none on Zanzibar), light volumes
  (0xE8: 2).

How Halo 2 (MCC's classic renderer) draws it - its lightmap pass's DX11
pixel shader, shaders\shader_passes\lightmap\lightmap_tex_detail_no_alpha
in zanzibar.map, disassembled (D3DDisassemble):
  light = saturate(L * (1 + |I| (n.i - i.z)))   L the lightmap texel, i
                                                the incident direction in
                                                tangent space (normalised),
                                                n the bump map's normal
  colour = min(1, 2 * light * min(1, 2 * base * detail))
- an unbumped surface (n = (0, 0, 1)) takes 2 L base: no directional term
  (the incident direction only bends a bump map's light around 1);
- the two-detail pass adds a per-shader tint (base * scale + offset) and a
  shiny pass a lightmap specular (L * colour * (n.h)^8 from the incident
  direction) - the materials' (h2engine/materials).
Objects take their light from the lightmap below them, its incident
direction the sun's side (this engine's lights_distant_lighting_at_point
does the same with the lightmap vertex's incident vector - once it finds
the surface: a converted level's collision leaves carry no render
surfaces, h2_object_lighting.c finds it), and a sky light the same in sun
and shade (Halo 2's lighting environments; h2_lighting_object_terms).

How it is carried (this file, by tier: h2_lighting_tier):
- classic: as the preview did (h2_level.c's x3, gamma 1.6, saturation
  1.3, its texel the square root - this engine's lightmap pass is linear,
  measured - the surface normal as the incident direction): flatter and
  paler than Halo 2;
- h2 (the desktop's and the Vita's default): Halo 2's 2 L, the texel
  holding half of it (h2_lighting_encode) and the environment's texture
  pass doubling (h2_lighting_overbright: rasterizer_xbox_environment.c,
  the modulate-2x blend, its alpha once - the GL device's
  halo_blend_alpha_single, the GXM device's blend_alpha_single), objects' sampled light doubled to match
  (object_lights.c); Halo 2's incident directions in the lightmap vertices
  (clusters' 0x2F, instances' from the geometry buckets: both in world
  space) - objects' sun-side light and shadow direction; the atlases'
  16-bit texels dithered (HALO_H2_LIGHTMAP_32BIT=1: 32-bit, 2 MB more).
*/

#ifndef __H2_LIGHTING_H
#define __H2_LIGHTING_H

#include "cseries.h"

/* ---------- constants */

enum
{
	_h2_lighting_tier_classic,
	_h2_lighting_tier_h2,
	NUMBER_OF_H2_LIGHTING_TIERS
};

/* ---------- prototypes */

/* the tier the next level build uses (HALO_H2_LIGHTING, config.toml's
display.h2_lighting: classic or h2; default h2 on the desktop's GL
renderer, the Vita and its null build, classic on Android) */
short h2_lighting_tier(void);

/* a word naming every setting a level build bakes in (h2_level.c keeps it
in the level cache's header: a change rebuilds the level) */
unsigned long h2_lighting_build_key(void);

/* (h2 tier) a lightmap texel: Halo 2's light (0..1 linear, as the page
has it, averaged) to this engine's lightmap texel (linear: measured, the
final colour is proportional to the texel - not its square, as the
classic response assumed); FALSE in the classic tier (the caller's own
response) */
boolean h2_lighting_encode(float const light[3], unsigned char out[3]);

/* (h2 tier, HALO_H2_LIGHTMAP_32BIT=1) whether the lightmap atlases are
32-bit (a8r8g8b8: the texels hold half the light, 16-bit's 32 levels band;
4 MB an atlas, not 2) */
boolean h2_lighting_atlas_32bit(void);

/* (h2 tier, 16-bit atlases) an atlas's RGB texels dithered to r5g6b5's
levels (the texels hold half the light: 16 levels of red for most of a
level's) */
void h2_lighting_atlas_dither_565(unsigned char *rgb, long size);

/* the lightmap pages' default reduction (h2_level.c, HALO_H2_LIGHTMAP_SHIFT
overrides): 1 (half each way) in both tiers - whole, Zanzibar's are three
32-bit atlases, past the multiplayer map cache */
long h2_lighting_lightmap_shift(void);

/* (h2 tier) whether the converted level carries Halo 2's per-vertex
incident directions in place of the surface normal */
boolean h2_lighting_incident_directions(void);

/* (h2 tier) an instance's incident directions (3 floats a vertex, in the
world's space, its definition's vertices' order; free() them), from the lightmap
group's geometry bucket the instance's bucket reference names; NULL if
there are none */
struct h2_cache;
float *h2_lighting_instance_incident(struct h2_cache *cache, unsigned long lightmap_group, long instance, long vertex_count);

/* (h2_level.c, as a level's cache file is resolved for loading) the
level's name and the key it is built with */
void h2_lighting_level_resolved(char const *name, unsigned long build_key);

/* (the renderer) whether the level now loaded is a Halo 2 level built in
the h2 tier: its lightmap texels hold half the light, the environment
texture pass doubles (rasterizer_xbox_environment.c), and objects' light
sampled from the lightmap is doubled (object_lights.c) */
/* whether a Halo 3 level (h3_level.c) is baked for, and drawn with, the
overbright texture pass: its texel half its light (the tier h2, not
HALO_H3_OVERBRIGHT=0) */
boolean h2_lighting_h3_overbright(void);
boolean h2_lighting_overbright(void);

/* (object_lights.c) a lightmap colour sampled for an object's light, as
the light it stands for */
void h2_lighting_object_lightmap_color(float color[3]);

/* (object_lights.c, a Halo 2 level in the h2 tier) an object's lights
from the (doubled) lightmap light below it, its incident direction's
accuracy (length) and the ground's colour: the ambient, the directed
(sun's) and the bounced light's colours and the shadow it casts; FALSE
elsewhere (this engine's own) */
/* the sky light of the object about to be lit: its cluster's sky's outdoor
ambient colour (NULL or black: none - the fixed sky light, Zanzibar's),
object_lights.c's, read by h2_lighting_object_terms */
void h2_lighting_object_sky(float const rgb[3]);
/* the object being lit's type (objects.h: biped 0, vehicle 1, weapon 2...):
Halo 2's ambient weights its light's brightness by it */
void h2_lighting_object_type(short type);
/* (a test's: HALO_H2_OBJECT_TRACE) the object being lit is traced */
extern boolean h2_lighting_trace_on;
/* whether an object's light sample is left out: a static object's (scenery,
machines, controls...) over a texel all but black (HALO_H2_OBJECT_DARK, the
luminance under which) */
boolean h2_lighting_object_reject(float const light[3]);
struct h2_cache;
/* the level's global lighting tag read (as its level is resolved) */
void h2_lighting_global_lighting_read(struct h2_cache *cache);
/* (after h2_lighting_object_terms) the object's "object env brightness":
Halo 2's reflections take it, not the light under the object */
float h2_lighting_object_env(void);
/* a lightmap group's lighting environment (Halo 2's PRT objects' light: its
samples at 0x38, 0xDC each - position, then 9 spherical harmonic
coefficients of red, green and blue - averaged) as this engine's default
lighting (lighting: ambient, pad, light 0 colour and direction, light 1
colour and direction); without samples Halo 2's own default, a white light
straight down */
void h2_lighting_prt_environment(struct h2_cache *cache, unsigned long lightmap_group, float lighting[16]);
/* (h2_level.c, as a level is resolved - the MCC map open) every structure
BSP's lighting environments kept for h2_lighting_prt_object (HALO_H2_PRT_LIGHT_AVERAGE:
none - the BSP's averaged default lighting as before) */
void h2_lighting_environments_read(struct h2_cache *cache);
/* (object_lights.c) a PRT object's light as Halo 2 gives it: the lighting
environment at a position of the structure BSP - the nearest sample and the
nearest on its far side, blended (function_22cf0) - as this engine's lighting
(h2_lighting_prt_environment's layout); FALSE with no samples kept for the BSP */
boolean h2_lighting_prt_object(short structure_bsp_index, float const position[3], float lighting[16]);
/* the same environment's order-3 coefficients, each colour's nine (D3DX's
order and signs, as the lightmap holds them) */
boolean h2_lighting_prt_object_sh(short structure_bsp_index, float const position[3], float sh[3][9]);
/* (rasterizer_xbox_models.c) a PRT model's vertex shader constants - Halo
2's CPCA (function_023ca0): the environment's coefficients turned into the
node's frame (NULL: left in the world's) and dotted with the model's basis
(h2_level.c's block: order, clusters, PCA vectors, 0, then each cluster its
mean and PCA vectors, each 3 colours x order^2 coefficients), times gain;
four vec4 a cluster (the means' r, g, b; the PCA vectors' r; g; b) into
constants, at most maximum; the number written */
struct real_matrix4x3;
long h2_lighting_prt_cluster_constants(float const sh[3][9], struct real_matrix4x3 const *node, float const *basis, long basis_count,
	float gain, float (*constants)[4], long maximum);
/* whether Halo 2's own object light is used (HALO_H2_OBJECT_LIGHT_OLD: the
tuned shares): its secondary light then comes from the side away from its
primary - object_lights.c turns it */
boolean h2_lighting_object_halo2(void);
boolean h2_lighting_object_terms(float const light[3], float accuracy, float const diffuse[3],
	float ambient[3], float direct[3], float bounce[3], float shadow[3]);

/* (structures.c, a leaf with no surface references on a Halo 2 level in
the h2 tier: h2_object_lighting.c) the lightmapped render surface under a
point - its lightmap, material, surface and its s, t in it; FALSE if none */
union real_point3d;
boolean h2_lighting_surface_at_point(union real_point3d const *point, short *lightmap_index, short *material_index,
	long *surface_index, float *s, float *t);
boolean h2_lighting_surface_below_point(union real_point3d const *point, float depth, float above, short *lightmap_index,
	short *material_index, long *surface_index, float *s, float *t, float *height);

/* (the GL renderer, d3d8_gl.c) the modulate-2x blend's alpha multiplied
once (set around the overbright texture pass) */
extern unsigned char halo_blend_alpha_single;

/* (h2/render-accuracy) Halo 2's baked scenery light - the lightmap's scenery
object info and its buckets' per-vertex colours (h2_lighting.c's notes):
whether it is used (HALO_H2_BAKED_OBJECTS, default on the desktop's GL
renderer in the h2 tier, off on the Vita and its null build) */
boolean h2_lighting_baked_objects(void);
/* (h2_level.c, as a level is resolved - the MCC map open) every structure
BSP's baked scenery objects and their buckets kept */
void h2_lighting_baked_read(struct h2_cache *cache);
/* (scenery.c) a scenery made from a placement with Halo 2's unique id */
void h2_lighting_baked_object_placed(long object_index, unsigned long uid);
/* whether the object has its baked light in that BSP */
boolean h2_lighting_baked_object(long object_index, short bsp);
/* (rasterizer_xbox_models.c) a part's vertices' baked light - each its
colour times gain and its incident direction's length - from the model's
'h2bk' words (model_block, model_block_bytes) for its geometry's
part_index-th part; FALSE none (drawn as before) */
boolean h2_lighting_baked_part(long object_index, short bsp, void const *model_block, long model_block_bytes, short geometry,
	short part_index, long vertex_count, float (*out)[4], float gain);
/* (object_lights.c) the object's mean baked colour and incident direction
(their mean: its length the light's directionality); FALSE none */
boolean h2_lighting_baked_summary(long object_index, short bsp, void const *model_block, long model_block_bytes,
	float color[3], float incident[3]);

/* (h2/render-accuracy) a Halo 2 light's brightness animation (its light
tag's 0xC4: a function of the game's time in seconds - Cairo's emergency
lights flash, a cosine clipped to on and off), written by h2_effects.c into
the made light tag's free bytes: 'H2BA', type, flags, shapes, the
function's range, four floats a curve (two curves), the brightness the
light's colour was made at. Halo 2's periodic type alone (the others: kept
whole) */
enum
{
	H2_LIGHT_ANIMATION_MAGIC = 0x48324241, /* 'H2BA' */
	H2_LIGHT_ANIMATION_OFFSET = 0x120,
};
/* (object_lights.c) the light colour's scale at the game's time seconds -
the animation's brightness over the light's own, 0..1 -; 1 for a light
without one or with HALO_H2_LIGHT_ANIMATION=0 (the old look; the
desktop's alone by default: =1 forces it on the Vita and its null build) */
float h2_light_animation_scale(void const *light_definition, float seconds);
/* (h2_level.c) an instance Halo 2 lights per vertex (no lightmap page): its
baked colours' mean (0..1, a lightmap texel's light), FALSE none */
boolean h2_lighting_instance_baked_mean(struct h2_cache *cache, unsigned long lightmap_group, long instance, long vertex_count,
	float rgb[3]);
/* (h2_level.c) its baked colours themselves, one a vertex (R G B A bytes, R
the low byte; malloc'd), NULL none */
unsigned long *h2_lighting_instance_baked_colors(struct h2_cache *cache, unsigned long lightmap_group, long instance, long vertex_count);

#endif
