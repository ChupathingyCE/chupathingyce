/*
H2_MATERIALS_RENDER.C

(port) The renderer's side of Halo 2's materials (h2_materials.h): the
quality tier, a shader's extension, and the register combiner programs of
Halo 2's own lightmap passes (H2EK tags\shaders\shader_passes\lightmap\
lightmap_tex_detail.shader_pass, its per-pixel implementation) written for
this engine's environment passes (rasterizer_xbox_environment.c calls
them). They are NV2A combiner programs as the engine's own are, so the
desktop's GLSL and the Vita's translation (nv2a_psh.c, nv2a_psh_cg.c) run
them as they run Halo CE's.

The lightmap pass (its textures: t0 the bump map, t2 the lightmap, t3 the
normalization cube of the incident direction in tangent space; v0.a the
incident direction's squared length, written as Halo 2's accuracy a):

    c' = c + c * a(N.L - Lz)        (Halo 2: R0 = T3 + T3*SIGNED(D1a))

The specular lightmap pass (t0 the bump map, t1 the lightmap, t2 the eye
vector's cube, t3 the incident direction's), added over the diffuse pass
times its alpha (the specular mask):

    R = 2(N.L)(N.E) - L.E
    s = 8 * c * specular colour * R^8 (dull R^4, shiny 2R^8 - 1) * brightness
    (4 * c here: this engine's lightmaps are twice Halo 2's)

Costs (each pixel of a Halo 2 surface, the desktop's tier): the lightmap
pass 4 combiner stages instead of 2, one more texture read (the bump map);
the specular pass a fifth full-screen-ish pass of 7 stages, 4 reads.
*/

/* ---------- headers */

#include "cseries.h"
#ifdef HALO_LINUX
#include "render_epoch.h"
#endif
#include "h2_materials.h"
#include "rasterizer/xbox/rasterizer_xbox_pixel_shader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *config_string(const char *name);
void platform_log(char const *format, ...);

/* ---------- constants */

/* combiner inputs: a register and a mapping */
enum
{
	IN_ZERO = 0x00,
	IN_C0 = 0x01,
	IN_V0 = 0x04,
	IN_T0 = 0x08,
	IN_T1 = 0x09,
	IN_T2 = 0x0A,
	IN_T3 = 0x0B,
	IN_R0 = 0x0C,
	IN_R1 = 0x0D,
	IN_ALPHA = 0x10,
	MAP_UNSIGNED = 0x00,
	MAP_INVERT = 0x20,
	MAP_EXPAND = 0x40,
	MAP_HALF_BIAS_NEGATE = 0xA0,
	MAP_SIGNED = 0xC0,
	MAP_SIGNED_NEGATE = 0xE0,
	IN_ONE = IN_ZERO | MAP_INVERT,
	IN_MINUS_ONE = IN_ZERO | MAP_EXPAND,
	IN_HALF = IN_ZERO | MAP_HALF_BIAS_NEGATE,
};

/* combiner outputs */
enum
{
	OUT_AB_DOT = 0x2000,
	OUT_CD_DOT = 0x1000,
	OUT_SCALE_2 = 0x10000,
	OUT_SCALE_4 = 0x20000,
};

#define INPUTS(a, b, c, d) (((unsigned long)(a) << 24) | ((unsigned long)(b) << 16) | ((unsigned long)(c) << 8) | (unsigned long)(d))
#define OUT_AB(r) ((unsigned long)(r) << 4)
#define OUT_CD(r) ((unsigned long)(r))
#define OUT_SUM(r) ((unsigned long)(r) << 8)

/* Halo 2's $lightmap_specular_brightness (an extern of its passes, not in
any tag the H2EK has): chosen against MCC's Zanzibar (HALO_H2_SPECULAR) */
#define H2_SPECULAR_BRIGHTNESS 0.12f

/* the models' specular: Halo 2's object light pass (H2EK shaders\
shader_passes\light_response\default_light_combined_tex, read for its
field names and combiner program) is the light's specular colour times the
highlight, scaled by the light's overbrightness (1, 2 or 4; the lightmap
pass's doubling "to emulate stupid bug that the artists like"); the
objects' light here is already doubled (the lighting's, h2_lighting.c), the
light's specular colour and overbrightness are not in any tag the H2EK has
- their product the one factor chosen against the owner's MCC frames with
the pass drawn as Halo 2's: 1.5 (6 was chosen before, when the pass's tint
ran to the glancing colour over the whole gun; the first-person SMG and
battle rifle were chrome, and at 2.5 their flat tops still a white band;
MCC's are matte with a faint line along the top edge, its Warthog a dark
green with the hood's highlights alone); and their environment maps' gain
(HALO_H2_MODEL_SPECULAR, HALO_H2_MODEL_ENV) */
/* the level's lightmap relief (its bump map's directional term): Halo 2's
term as disassembled (h2_lighting.h), scale 1. It was 2 from Oct, chosen by
eye against the owner's MCC Zanzibar frames; measured against the real
engine (the H2EK's halo2_tag_test, the same cameras, 26 views on 6 maps,
local render runs) scale 1 is the closer on every one: the
mean pixel difference 11.6 -> 10.3 grey levels, the surfaces' fine contrast
(a 9-pixel high pass) 1.2-1.4x the real engine's at 2, 0.9-1.1x at 1 (the
doubled term darkened every bump's slopes: Burial Mounds' rock 0.87 of the
real brightness, Zanzibar's base floor plates' studs). The full tier only:
the cheap tier's pass has no relief. HALO_H2_LEVEL_RELIEF=2 the old look */
#define H2_LEVEL_RELIEF 1

#define H2_MODEL_SPECULAR_BRIGHTNESS 1.5f
#define H2_MODEL_SPECULAR_SKY 1.0f
/* (an environment mapped template's specular share: Halo 2's object light
pass reads the same program for every template, but its mask is the
texaccum's alpha, which the environment mapped templates' own passes write
(their reflection's mask), not the base map's alpha this engine masks by;
whole, the plasma rifle's body blew out white at 1.5 - 0.15 was chosen
with the brightness at 6 (0.9 whole), 0.3 here (0.45): the owner's frames'
plasma rifle, a mild purple sheen) */
#define H2_MODEL_ENV_SPECULAR 0.3f
#define H2_MODEL_ENVIRONMENT_GAIN 1.0f
/* (a template with an environment map pass of its own - tex_bump_dprs_env:
its reflection added whole by Halo 2, here within the lit reflection -
chosen against the Xbox's 04a: the plasma rifle's prongs' violet sheen;
HALO_H2_MODEL_ENV_PASS) */
#define H2_MODEL_ENVIRONMENT_PASS_GAIN 1.0f

/* ---------- private code */

static unsigned long grey_pixel32(float value)
{
	long v = (long)(value * 255.0f + 0.5f);

	if (v < 0)
		v = 0;
	if (v > 255)
		v = 255;
	return ((unsigned long)v << 16) | ((unsigned long)v << 8) | (unsigned long)v | ((unsigned long)v << 24);
}

/* ---------- public code */

enum h2_materials_tier h2_materials_tier(void)
{
	extern volatile unsigned long halo_settings_generation;
	static unsigned long settings_seen;
	static int tier = -1;

	if (tier < 0 || settings_seen != halo_settings_generation)
	{
		char const *setting = getenv("HALO_H2_MATERIALS");

		settings_seen = halo_settings_generation;
		if (!setting || !*setting)
			setting = config_string("graphics.h2_materials");
		if (setting && !strcmp(setting, "full"))
			tier = _h2_materials_full;
		else if (setting && !strcmp(setting, "cheap"))
			tier = _h2_materials_cheap;
		else
		{
			/* (the Vita's too since its GXM device draws the models' per-pixel
			pass - the pair's Cg twins, rasterizer_vita_custom_programs.h) */
			tier = _h2_materials_full;
		}
	}
	return (enum h2_materials_tier)tier;
}

struct h2_material_extension const *h2_material_extension_get(
	void const *shader_environment)
{
	struct h2_material_extension const *extension;

	if (!shader_environment)
		return NULL;
	extension = (struct h2_material_extension const *)
		((unsigned char const *)shader_environment + H2_MATERIAL_EXTENSION_OFFSET);
	if (extension->magic != H2_MATERIAL_MAGIC || extension->version != H2_MATERIAL_VERSION)
		return NULL;
	/* (a test: HALO_H2_MATERIALS_TRACE logs the first shader found with one -
	never on a Halo CE map) */
	{
		static int trace = -1;

		if (trace < 0)
			trace = getenv("HALO_H2_MATERIALS_TRACE") != NULL;
		if (trace == 1)
		{
			platform_log("h2 materials: a shader with the extension drawn (flags %04x)", extension->flags);
			trace = 2;
		}
	}
	return extension;
}

/* the level lightmap relief's scale, 1, 2 or 4 (H2_LEVEL_RELIEF,
HALO_H2_LEVEL_RELIEF): the combiner's output scale - the native renderer's
program (rasterizer_vita_environment.c) bakes the same */
int h2_material_level_relief(void)
{
	static int relief = -1;

	if (relief < 0)
	{
		char const *setting = getenv("HALO_H2_LEVEL_RELIEF");

		relief = setting ? atoi(setting) : H2_LEVEL_RELIEF;
	}
	return relief;
}

/* the lightmap pass's program (lightmap present, no self-illumination) for a
shader with the extension: TRUE written to pixel_shader; material_color its
colour (the final combiner's constant 0) */
boolean h2_material_lightmap_program(
	struct h2_material_extension const *extension,
	void *pixel_shader,
	unsigned long material_color,
	boolean alpha_tested)
{
	struct pixel_shader_definition *ps = (struct pixel_shader_definition *)pixel_shader;

	if (!extension)
		return FALSE;
	memset(ps, 0, sizeof(*ps));
	if (h2_materials_tier() == _h2_materials_cheap || !(extension->flags & _h2_material_relative_bump_bit))
	{
		/* (the lightmap as it is: no bump, the incident direction unused; an
		alpha tested shader's bump map read for its alpha, the test's, as
		this engine's own pass reads it) */
		ps->texture_modes = alpha_tested ? 0x00000401 : 0x00000400;
		ps->combiner_count = 1;
		ps->final_combiner_inputs_abcd = INPUTS(IN_T2, IN_C0, IN_ZERO, IN_ZERO);
		ps->final_combiner_inputs_efg = INPUTS(IN_ZERO, IN_ZERO, alpha_tested ? IN_T0 | IN_ALPHA : IN_ONE, IN_ZERO);
		ps->final_combiner_constant_0 = material_color;
		return TRUE;
	}
	ps->texture_modes = 0x00018401;
	ps->combiner_count = 4;
	/* r1 = N.L; r0.a = Lz */
	ps->rgb_inputs[0] = INPUTS(IN_T0 | MAP_EXPAND, IN_T3 | MAP_EXPAND, IN_ZERO, IN_ZERO);
	ps->rgb_outputs[0] = OUT_AB_DOT | OUT_AB(IN_R1);
	ps->alpha_inputs[0] = INPUTS(IN_T3 | MAP_EXPAND, IN_ONE, IN_ZERO, IN_ZERO);
	ps->alpha_outputs[0] = OUT_AB(IN_R0);
	/* r1.a = N.L - Lz */
	ps->alpha_inputs[1] = INPUTS(IN_R1 | MAP_SIGNED, IN_ONE, IN_R0 | IN_ALPHA | MAP_SIGNED_NEGATE, IN_ONE);
	ps->alpha_outputs[1] = OUT_SUM(IN_R1);
	/* r1.a = a(N.L - Lz) */
	ps->alpha_inputs[2] = INPUTS(IN_V0 | IN_ALPHA, IN_R1 | IN_ALPHA | MAP_SIGNED, IN_ZERO, IN_ZERO);
	ps->alpha_outputs[2] = OUT_AB(IN_R1);
	/* (the relief's strength: times H2_LEVEL_RELIEF, the combiner's output
	scale - HALO_H2_LEVEL_RELIEF 1, 2 or 4) */
	{
		int relief = h2_material_level_relief();

		if (extension->flags & _h2_material_relief_once_bit)
			;
		else if (relief >= 4)
			ps->alpha_outputs[2] |= OUT_SCALE_4;
		else if (relief >= 2)
			ps->alpha_outputs[2] |= OUT_SCALE_2;
	}
	/* r0 = c + c a(N.L - Lz) */
	ps->rgb_inputs[3] = INPUTS(IN_T2, IN_ONE, IN_T2, IN_R1 | IN_ALPHA | MAP_SIGNED);
	ps->rgb_outputs[3] = OUT_SUM(IN_R0);
	ps->final_combiner_inputs_abcd = INPUTS(IN_R0, IN_C0, IN_ZERO, IN_ZERO);
	ps->final_combiner_inputs_efg = INPUTS(IN_ZERO, IN_ZERO, IN_T0 | IN_ALPHA, IN_ZERO);
	ps->final_combiner_constant_0 = material_color;
	return TRUE;
}

/* whether the specular lightmap pass draws Halo 2's specular for this
shader (the desktop's tier, a specular colour) */
boolean h2_material_specular_wanted(
	struct h2_material_extension const *extension)
{
	return extension && (extension->flags & _h2_material_specular_bit) &&
		extension->lightmap_type != _h2_lightmap_type_none && h2_materials_tier() == _h2_materials_full;
}

/* the specular lightmap pass's program for it */
void h2_material_specular_program(
	struct h2_material_extension const *extension,
	void *pixel_shader)
{
	struct pixel_shader_definition *ps = (struct pixel_shader_definition *)pixel_shader;
	static float brightness = -1.0f;

	if (brightness < 0.0f)
	{
		char const *setting = getenv("HALO_H2_SPECULAR");

		brightness = setting ? (float)atof(setting) : H2_SPECULAR_BRIGHTNESS;
	}
	memset(ps, 0, sizeof(*ps));
	ps->texture_modes = 0x00018C21;
	ps->combiner_count = 7 | 0x1000;
	/* r0 = N.L, r1 = N.E */
	ps->rgb_inputs[0] = INPUTS(IN_T0 | MAP_EXPAND, IN_T3 | MAP_EXPAND, IN_T0 | MAP_EXPAND, IN_T2 | MAP_EXPAND);
	ps->rgb_outputs[0] = OUT_AB_DOT | OUT_CD_DOT | OUT_AB(IN_R0) | OUT_CD(IN_R1);
	/* t3 = L.E; r0.a = (N.L)(N.E) */
	ps->rgb_inputs[1] = INPUTS(IN_T3 | MAP_EXPAND, IN_T2 | MAP_EXPAND, IN_ZERO, IN_ZERO);
	ps->rgb_outputs[1] = OUT_AB_DOT | OUT_AB(IN_T3);
	ps->alpha_inputs[1] = INPUTS(IN_R0 | MAP_SIGNED, IN_R1 | MAP_SIGNED, IN_ZERO, IN_ZERO);
	ps->alpha_outputs[1] = OUT_AB(IN_R0);
	/* r1.a = R = 2((N.L)(N.E) - L.E / 2) */
	ps->alpha_inputs[2] = INPUTS(IN_R0 | IN_ALPHA | MAP_SIGNED, IN_ONE, IN_T3 | MAP_SIGNED_NEGATE, IN_HALF);
	ps->alpha_outputs[2] = OUT_SUM(IN_R1) | OUT_SCALE_2;
	/* r1.a = R^2 (R below 0: none); r0 = 2 c (specular colour): Halo 2's
	4, over a lightmap twice this engine's (Halo 2's diffuse is 2 c T, this
	engine's c T) */
	ps->alpha_inputs[3] = INPUTS(IN_R1 | IN_ALPHA, IN_R1 | IN_ALPHA, IN_ZERO, IN_ZERO);
	ps->alpha_outputs[3] = OUT_AB(IN_R1);
	ps->rgb_inputs[3] = INPUTS(IN_T1, IN_C0, IN_ZERO, IN_ZERO);
	ps->rgb_outputs[3] = OUT_AB(IN_R0) | OUT_SCALE_2;
	ps->constant_0[3] = extension->specular_color;
	/* R^4 */
	ps->alpha_inputs[4] = INPUTS(IN_R1 | IN_ALPHA, IN_R1 | IN_ALPHA, IN_ZERO, IN_ZERO);
	ps->alpha_outputs[4] = OUT_AB(IN_R1);
	/* R^8 (dull: R^4; shiny: 2R^8 - 1) */
	switch (extension->lightmap_type)
	{
	case _h2_lightmap_type_dull:
		break;
	case _h2_lightmap_type_shiny:
		ps->alpha_inputs[5] = INPUTS(IN_R1 | IN_ALPHA, IN_R1 | IN_ALPHA, IN_HALF, IN_MINUS_ONE);
		ps->alpha_outputs[5] = OUT_SUM(IN_R1) | OUT_SCALE_2;
		break;
	default:
		ps->alpha_inputs[5] = INPUTS(IN_R1 | IN_ALPHA, IN_R1 | IN_ALPHA, IN_ZERO, IN_ZERO);
		ps->alpha_outputs[5] = OUT_AB(IN_R1);
		break;
	}
	/* r0 = 2 r0 R^n */
	ps->rgb_inputs[6] = INPUTS(IN_R0, IN_R1 | IN_ALPHA, IN_ZERO, IN_ZERO);
	ps->rgb_outputs[6] = OUT_AB(IN_R0) | OUT_SCALE_2;
	/* (times the brightness; its alpha R^n: nothing drawn where it is 0) */
	ps->final_combiner_inputs_abcd = INPUTS(IN_R0, IN_C0, IN_ZERO, IN_ZERO);
	ps->final_combiner_inputs_efg = INPUTS(IN_ZERO, IN_ZERO, IN_R1 | IN_ALPHA, IN_ZERO);
	ps->final_combiner_constant_0 = grey_pixel32(brightness);
}

/* ---------- models: the per-pixel pass */

void (*h2_materials_custom_program)(const char *vertex_source, const char *fragment_source,
	const float *uniforms, int vec4_count) = NULL;

/* (the Vita's GXM device's: the pair's Cg bodies, rasterizer_vita_custom
_programs.h; NULL elsewhere) */
void (*h2_materials_custom_program_cg)(const char *vertex_body, const char *fragment_body,
	const float *uniforms, int vec4_count) = NULL;

#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
#include "rasterizer/vita/rasterizer_vita_custom_programs.h"
#else
#define vita_h2_model_pass_vertex_body NULL
#define vita_h2_model_pass_fragment_body NULL
#define vita_h2_model_plasma_illum_fragment_body NULL
#endif

/* whether a pass's program pair can be set: the desktop's GLSL, or the
Vita's Cg twins */
static boolean materials_program_available(void)
{
	return h2_materials_custom_program != NULL || h2_materials_custom_program_cg != NULL;
}

/* a pass's program pair set (the GLSL, its Cg twins; NULLs: the device's own
again) */
static void materials_program(char const *vertex_source, char const *fragment_source, char const *vertex_body,
	char const *fragment_body, float const *uniforms, int vec4_count)
{
	if (h2_materials_custom_program)
		h2_materials_custom_program(vertex_source, fragment_source, uniforms, vec4_count);
	else if (h2_materials_custom_program_cg)
		h2_materials_custom_program_cg(vertex_body, fragment_body, uniforms, vec4_count);
}

/* the pass's vertex program: compressed model vertices (their position,
normal, binormal, tangent, texture coordinate, two nodes and a weight)
skinned by the node matrices the model's own vertex programs read (c[60 +
3 node], the node index scale c[7].w), placed as they place it (c[0-3], the
screen's c[58], c[59], the viewport undone as nv2a_vsh.c does); out: the
base map's coordinates, and the lights' and eye's directions in the
surface's tangent space (x the tangent, y the binormal, z the normal - the
environment lightmap program's) */
static char const model_pass_vertex_source[] =
	"#version 450 core\n"
	"uniform vec4 c[192];\n"
	"uniform vec4 viewport_scale;\n"
	"uniform vec4 viewport_offset;\n"
	"uniform float screen_offset;\n"
	"uniform vec4 h2u[10];\n"
	"out vec4 xT0;\n"
	"out vec3 xL0;\n"
	"out vec3 xL1;\n"
	"out vec3 xE;\n"
	"invariant gl_Position;\n"
	"layout(location = 0) in vec4 v0_in;\n"
	"layout(location = 1) in uint v1_packed;\n"
	"layout(location = 2) in uint v2_packed;\n"
	"layout(location = 3) in uint v3_packed;\n"
	"layout(location = 4) in vec4 v4_in;\n"
	"layout(location = 5) in vec4 v5_in;\n"
	"layout(location = 6) in vec4 v6_in;\n"
	"vec3 unpack_normpacked3(uint p)\n"
	"{\n"
	"	int x = int(p << 21) >> 21;\n"
	"	int y = int(p << 10) >> 21;\n"
	"	int z = int(p) >> 22;\n"
	"	return vec3(float(x) / 1023.0, float(y) / 1023.0, float(z) / 511.0);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"	int i0 = int(floor(v5_in.x * c[7].w + 0.001));\n"
	"	int i1 = int(floor(v5_in.y * c[7].w + 0.001));\n"
	"	float w0 = v6_in.x, w1 = v6_in.w - v6_in.x;\n"
	"	vec4 r4 = w0 * c[clamp(i0 + 60, 0, 191)] + w1 * c[clamp(i1 + 60, 0, 191)];\n"
	"	vec4 r5 = w0 * c[clamp(i0 + 61, 0, 191)] + w1 * c[clamp(i1 + 61, 0, 191)];\n"
	"	vec4 r6 = w0 * c[clamp(i0 + 62, 0, 191)] + w1 * c[clamp(i1 + 62, 0, 191)];\n"
	"	vec3 p = vec3(dot(v0_in.xyz, r4.xyz) + r4.w, dot(v0_in.xyz, r5.xyz) + r5.w, dot(v0_in.xyz, r6.xyz) + r6.w);\n"
	"	vec3 n = unpack_normpacked3(v1_packed), b = unpack_normpacked3(v2_packed), t = unpack_normpacked3(v3_packed);\n"
	"	vec3 N = normalize(vec3(dot(n, r4.xyz), dot(n, r5.xyz), dot(n, r6.xyz)));\n"
	"	vec3 B = normalize(vec3(dot(b, r4.xyz), dot(b, r5.xyz), dot(b, r6.xyz)));\n"
	"	vec3 T = normalize(vec3(dot(t, r4.xyz), dot(t, r5.xyz), dot(t, r6.xyz)));\n"
	"	vec3 l0 = -h2u[0].xyz, l1 = -h2u[2].xyz, e = h2u[5].xyz - p;\n"
	"	xL0 = vec3(dot(l0, T), dot(l0, B), dot(l0, N));\n"
	"	xL1 = vec3(dot(l1, T), dot(l1, B), dot(l1, N));\n"
	"	xE = vec3(dot(e, T), dot(e, B), dot(e, N));\n"
	/* (the base map's coordinates as the model's own pass makes them: its
	texture transform, registers 13 and 14 - 12 is the detail map's scale) */
	"	vec4 uv = vec4(v4_in.xy, 0.0, 1.0);\n"
	"	xT0 = vec4(dot(uv, c[13]), dot(uv, c[14]), 0.0, 1.0);\n"
	"	vec4 o = vec4(dot(p, c[0].xyz) + c[0].w, dot(p, c[1].xyz) + c[1].w, dot(p, c[2].xyz) + c[2].w, dot(p, c[3].xyz) + c[3].w);\n"
	"	float rw = 1.0 / o.w;\n"
	"	o.xyz = o.xyz * c[58].xyz * rw + c[59].xyz;\n"
	"	vec3 scale = vec3(viewport_scale.x != 0.0 ? viewport_scale.x : 1.0,\n"
	"		viewport_scale.y != 0.0 ? viewport_scale.y : 1.0,\n"
	"		viewport_scale.z != 0.0 ? viewport_scale.z : 1.0);\n"
	"	vec3 ndc = (vec3(o.xy + vec2(0.5 + screen_offset, 0.5), o.z) - viewport_offset.xyz) / scale;\n"
	"	gl_Position = vec4(ndc * o.w, o.w);\n"
	"}\n";

/* its pixel program: the bump map's normal (tex1) lit as Halo 2's dynamic
lightmap lights a surface, over the model's own per-vertex light
(_h2_model_pass_relief: their ratio - the ambient light lit by the bump
map's z as Halo 2's is, a floor of a part of the light under both - half of
it: the blend is the 2x
modulate, DESTCOLOR SRCCOLOR); the main light's specular as Halo 2's
object light pass draws it (2(N.L)(N.E) - L.E to the shader's specular
type's curve, its colour from the specular towards the glancing as N.E
falls - linearly -, the mask the base map's alpha (tex0) times 8 N.L; over
first-person geometry its first-person implementation, the eye's light:
(N.E)^2's curve), times the light's specular colour and brightness
(_h2_model_pass_specular, _h2_model_pass_specular_first_person, added) */
static char const model_pass_fragment_source[] =
	"#version 450 core\n"
	"uniform sampler2D tex0;\n"
	"uniform sampler2D tex1;\n"
	"uniform vec4 texture_scale[4];\n"
	"uniform vec4 h2u[10];\n"
	"in vec4 xT0;\n"
	"in vec3 xL0;\n"
	"in vec3 xL1;\n"
	"in vec3 xE;\n"
	"layout(location = 0) out vec4 fragment_color;\n"
	"void main()\n"
	"{\n"
	"	if (h2u[8].z > 3.5)\n"
	"	{\n"
	"		vec3 base = texture(tex0, xT0.xy * texture_scale[0].xy).rgb;\n"
	"		float shade = dot(base, vec3(0.299, 0.587, 0.114));\n"
	"		vec3 want = h2u[6].rgb * shade;\n"
	"		if (h2u[8].z < 4.5)\n"
	"		{\n"
	/* (times 2 by the blend: the drawn visor's own light, shine and reflection kept) */
	"			fragment_color = vec4(clamp(0.5 * want / max(base, vec3(0.03)), 0.0, 1.0), 1.0);\n"
	"			return;\n"
	"		}\n"
	/* (what the multiplication - at most 2x - cannot reach: lit by the lights, added) */
	"		vec3 nv = normalize(texture(tex1, xT0.xy * h2u[8].xy * texture_scale[1].xy).rgb * 2.0 - 1.0);\n"
	"		vec3 m0 = normalize(xL0), m1 = normalize(xL1);\n"
	"		vec3 lit = h2u[4].rgb * max(nv.z, 0.0) + h2u[1].rgb * max(dot(nv, m0), 0.0) + h2u[3].rgb * max(dot(nv, m1), 0.0);\n"
	"		fragment_color = vec4(lit * max(want - 2.0 * base, vec3(0.0)), 1.0);\n"
	"		return;\n"
	"	}\n"
	"	if (h2u[8].z > 2.5)\n"
	"	{\n"
	"		fragment_color = vec4(mix(vec3(1.0), h2u[6].rgb, texture(tex0, xT0.xy * texture_scale[0].xy).a), 1.0);\n"
	"		return;\n"
	"	}\n"
	"	vec3 n = normalize(texture(tex1, xT0.xy * h2u[8].xy * texture_scale[1].xy).rgb * 2.0 - 1.0);\n"
	"	vec3 l0 = normalize(xL0), l1 = normalize(xL1);\n"
	"	if (h2u[8].z < 0.5)\n"
	"	{\n"
	"		vec3 bumped = h2u[4].rgb * max(n.z, 0.0) + h2u[1].rgb * max(dot(n, l0), 0.0) + h2u[3].rgb * max(dot(n, l1), 0.0);\n"
	"		vec3 flat_ = h2u[4].rgb + h2u[1].rgb * max(l0.z, 0.0) + h2u[3].rgb * max(l1.z, 0.0);\n"
	"		vec3 floor_ = vec3(h2u[9].x * dot(h2u[4].rgb + h2u[1].rgb + h2u[3].rgb, vec3(0.299, 0.587, 0.114)) + 0.01);\n"
	"		vec3 relief = clamp(mix(vec3(1.0), (bumped + floor_) / (flat_ + floor_), h2u[9].y), h2u[9].z, h2u[9].w);\n"
	"		vec3 dbg_e = normalize(xE);\n"
	"		float dbg_r = clamp(2.0 * dot(n, l0) * dot(n, dbg_e) - dot(l0, dbg_e), 0.0, 1.0);\n"
	"		fragment_color = h2u[8].w > 4.5 ? vec4(max(dot(n, l0), 0.0), max(dot(n, dbg_e), 0.0), max(-dot(l0, dbg_e), 0.0), 1.0) :\n"
	"			h2u[8].w > 3.5 ? vec4(vec3(pow(dbg_r, 8.0)), 1.0) :\n"
	"			h2u[8].w > 2.5 ? vec4(texture(tex0, xT0.xy * texture_scale[0].xy).aaa, 1.0) :\n"
	"			h2u[8].w > 1.5 ? vec4(texture(tex0, xT0.xy * texture_scale[0].xy).rgb, 1.0) :\n"
	"			h2u[8].w > 0.5 ? vec4(n * 0.5 + 0.5, 1.0) : vec4(relief * 0.5, 1.0);\n"
	"	}\n"
	"	else\n"
	"	{\n"
	"		vec3 e = normalize(xE);\n"
	/* (Halo 2's specular light: the lightmap's dominant incident direction
	and colour - here the sun's and the sky's lights, each by its strength,
	the sky's share h2u[2].w) */
	"		const vec3 luma = vec3(0.299, 0.587, 0.114);\n"
	"		float w0 = dot(h2u[1].rgb, luma), w1 = dot(h2u[3].rgb, luma) * h2u[2].w;\n"
	"		vec3 ls = normalize(l0 * w0 + l1 * w1 + vec3(0.0, 0.0, 1e-4));\n"
	"		vec3 cs = h2u[1].rgb + h2u[3].rgb * h2u[2].w;\n"
	"		float nl = clamp(dot(n, ls), 0.0, 1.0), ne = clamp(dot(n, e), 0.0, 1.0);\n"
	/* Halo 2's object light pass (H2EK shaders\shader_passes\light_response\
	default_light_combined_tex, its combined implementation, read for its
	field names and combiner program): R = 2(N.L)(N.E) - L.E held in 0..1
	as its combiners hold it (lit from behind, the light facing the camera,
	it passes 1 and its eighth power ran away: the Ghosts hot pink in their
	glancing tint); the highlight's curve switched on the shader's specular
	type - none, R^8, R^2 (dull), 4(R^8 - 192/255) (shiny) -; its tint from
	the specular colour towards the glancing colour as N.E falls, linearly;
	the specular mask (the base map's alpha) times the self-shadowing term
	8 N.L; all times the light's specular colour */
	/* its first-person implementation (lightaccum_frustum_tex_first_person:
	"we have L=E in first-person so this simplifies into (2(N.L)^2 - 1)^k" -
	the first-person light is the eye's: 4((N.E)^2 - 192/255) in the
	reflection's place, its curve the same switch over that - the square for
	default -, its tint mixed by N.E; the highlight on what faces the
	camera, never along a gun's top seen edge-on) */
	"		float r, s;\n"
	"		if (h2u[8].z > 1.5)\n"
	"		{\n"
	"			nl = ne;\n"
	"			r = clamp(4.0 * (ne * ne - 0.7529), 0.0, 1.0);\n"
	"			s = h2u[7].a < 0.5 ? 0.0 : h2u[7].a < 1.5 ? r * r : h2u[7].a < 2.5 ? r : clamp(4.0 * (r * r - 0.7529), 0.0, 1.0);\n"
	"		}\n"
	"		else\n"
	"		{\n"
	"			r = clamp(2.0 * dot(n, ls) * dot(n, e) - dot(ls, e), 0.0, 1.0);\n"
	"			float r2 = r * r, r4 = r2 * r2, r8 = r4 * r4;\n"
	"			s = h2u[7].a < 0.5 ? 0.0 : h2u[7].a < 1.5 ? r8 : h2u[7].a < 2.5 ? r2 : clamp(4.0 * (r8 - 0.7529), 0.0, 1.0);\n"
	"		}\n"
	"		vec3 tint = ne * h2u[6].rgb + (1.0 - ne) * h2u[7].rgb;\n"
	"		float mask = texture(tex0, xT0.xy * texture_scale[0].xy).a * clamp(8.0 * nl, 0.0, 1.0);\n"
	"		fragment_color = vec4(cs * tint * (s * mask * h2u[6].a), 1.0);\n"
	"	}\n"
	"}\n";

/* Halo 2's plasma_one_channel_self_illumination (H2EK pass text): the
multichannel map's alpha a noise against a live time - R0 = T0a - HALF_BIAS(t),
R0a = t - HALF_BIAS(T0a); basic = 4 (SIGNED(R0a)^2 mux SIGNED(R0b)^2); two
sharpenings ("0 mux EXPAND^2"); colour = plasma x channel_a x T0r + T0g
channel_b + T0b channel_c, added. h2u[0..2] the channels, h2u[3].x the time */
static char const model_plasma_illum_fragment_source[] =
	"#version 450 core\n"
	"uniform sampler2D tex0;\n"
	"uniform vec4 texture_scale[4];\n"
	"uniform vec4 h2u[10];\n"
	"in vec4 xT0;\n"
	"layout(location = 0) out vec4 fragment_color;\n"
	"void main()\n"
	"{\n"
	"	vec4 t = texture(tex0, xT0.xy * texture_scale[0].xy);\n"
	"	float time = h2u[3].x;\n"
	"	float r0 = clamp(t.a - (time - 0.5), -1.0, 1.0);\n"
	"	float r0a = clamp(time - (t.a - 0.5), -1.0, 1.0);\n"
	"	float basic = clamp(4.0 * (r0a < 0.5 ? r0a * r0a : r0 * r0), -1.0, 1.0);\n"
	"	float e = 2.0 * clamp(basic, 0.0, 1.0) - 1.0;\n"
	"	float s1 = basic < 0.5 ? 0.0 : e * e;\n"
	"	e = 2.0 * s1 - 1.0;\n"
	"	float s2 = s1 < 0.5 ? 0.0 : e * e;\n"
	"	if (time > 1.5) s2 = 1.0;\n"     /* (a three-channel glow's pass: no plasma) */
	"	vec3 c = s2 * h2u[0].rgb * t.r + t.g * h2u[1].rgb + t.b * h2u[2].rgb;\n"
	"	fragment_color = vec4(clamp(c, 0.0, 1.0), 0.0);\n"
	"}\n";

boolean h2_material_plasma_illum_available(
	void)
{
	return materials_program_available();
}

void h2_material_model_plasma_illum_program(
	unsigned long const *colours,
	float time)
{
	static float uniforms[H2_MODEL_PASS_UNIFORMS][4];
	int k, c;

	if (!materials_program_available())
		return;
	if (!colours)
	{
		materials_program(NULL, NULL, NULL, NULL, NULL, 0);
		return;
	}
	for (k = 0; k < 3; k++)
	{
		for (c = 0; c < 3; c++)
			uniforms[k][c] = ((colours[k] >> (16 - 8 * c)) & 255) / 255.0f;
		uniforms[k][3] = 0.0f;
	}
	uniforms[3][0] = time;
	uniforms[3][1] = uniforms[3][2] = uniforms[3][3] = 0.0f;
	materials_program(model_pass_vertex_source, model_plasma_illum_fragment_source, vita_h2_model_pass_vertex_body,
		vita_h2_model_plasma_illum_fragment_body, &uniforms[0][0], H2_MODEL_PASS_UNIFORMS);
}

struct h2_model_material_extension const *h2_material_model_extension_get(
	void const *shader_model)
{
	struct h2_model_material_extension const *extension;
	unsigned long magic;

	if (!shader_model)
		return NULL;
	extension = (struct h2_model_material_extension const *)
		((unsigned char const *)shader_model + H2_MODEL_MATERIAL_EXTENSION_OFFSET);
	memcpy(&magic, &extension->magic, sizeof(magic));
	return magic == H2_MODEL_MATERIAL_MAGIC ? extension : NULL;
}

boolean h2_material_model_pass_wanted(
	struct h2_model_material_extension const *extension,
	boolean compressed_vertices)
{
	extern volatile unsigned long halo_settings_generation;
	static unsigned long settings_seen;
	static int disabled = -1;

	/* (HALO_H2_NO_MODEL_PASS=1: none - live, @set in tests) */
	if (disabled < 0 || settings_seen != halo_settings_generation)
	{
		char const *setting = getenv("HALO_H2_NO_MODEL_PASS");

		settings_seen = halo_settings_generation;
		disabled = setting && *setting && strcmp(setting, "0");
	}
	return extension && !disabled && materials_program_available() && extension->bump_map != 0xFFFFFFFFUL &&
		compressed_vertices && h2_materials_tier() == _h2_materials_full;
}

boolean h2_material_model_pass_cinematic, h2_material_model_pass_cinematic_hull;

void h2_material_model_pass_program(
	struct h2_model_material_extension const *extension,
	float const (*light_directions)[3],
	float const (*light_colors)[3],
	short light_count,
	float const *ambient_color,
	float const *camera_position,
	int mode,
	float (*uniforms)[4])
{
	static float brightness = -1.0f, env_specular = -1.0f;
	int k, c;

	if (!materials_program_available())
		return;
	if (!uniforms)
	{
		materials_program(NULL, NULL, NULL, NULL, NULL, 0);
		return;
	}
	{
		extern volatile unsigned long halo_settings_generation;
		static unsigned long settings_seen;

		if (brightness < 0.0f || settings_seen != halo_settings_generation)
		{
			char const *setting = getenv("HALO_H2_MODEL_SPECULAR");

			settings_seen = halo_settings_generation;
			brightness = setting ? (float)atof(setting) : H2_MODEL_SPECULAR_BRIGHTNESS;
		}
	}
	/* (an environment mapped template's specular: Halo 2's lightmap_tex_env
	pass tints the light without the shiny pass's 2x - HALO_H2_MODEL_ENV_SPECULAR,
	live, its share) */
	{
		extern volatile unsigned long halo_settings_generation;
		static unsigned long env_seen;

		if (env_specular < 0.0f || env_seen != halo_settings_generation)
		{
			char const *setting = getenv("HALO_H2_MODEL_ENV_SPECULAR");

			env_seen = halo_settings_generation;
			env_specular = setting ? (float)atof(setting) : H2_MODEL_ENV_SPECULAR;
		}
	}
	memset(uniforms, 0, sizeof(float) * 4 * H2_MODEL_PASS_UNIFORMS);
	for (k = 0; k < 2; k++)
	{
		for (c = 0; c < 3; c++)
		{
			uniforms[2 * k][c] = k < light_count ? light_directions[k][c] : 0.0f;
			uniforms[2 * k + 1][c] = k < light_count ? light_colors[k][c] : 0.0f;
		}
	}
	for (c = 0; c < 3; c++)
	{
		uniforms[4][c] = ambient_color[c];
		uniforms[5][c] = camera_position[c];
		uniforms[6][c] = ((extension->specular_color >> (16 - 8 * c)) & 255) / 255.0f;
		uniforms[7][c] = ((extension->specular_glancing_color >> (16 - 8 * c)) & 255) / 255.0f;
	}
	uniforms[6][3] = brightness * (extension->detail_alpha ? extension->detail_alpha / 65535.0f : 1.0f) *
		((extension->flags & _h2_model_material_env_specular_bit) ? env_specular : 1.0f);
	/* (the sky light's share of the specular light - HALO_H2_MODEL_SPECULAR_SKY,
	live) */
	{
		extern volatile unsigned long halo_settings_generation;
		static unsigned long sky_seen;
		static float sky = -1.0f;

		if (sky < 0.0f || sky_seen != halo_settings_generation)
		{
			char const *setting = getenv("HALO_H2_MODEL_SPECULAR_SKY");

			sky_seen = halo_settings_generation;
			sky = setting ? (float)atof(setting) : H2_MODEL_SPECULAR_SKY;
		}
		uniforms[2][3] = sky;
	}
	/* (a cinematic's lights: its primary alone is specular - hs_doc: "shadowing
	diffuse and specular", the secondary "non-shadowing diffuse" - warm in 03a,
	MCC's gold Pelican; with the secondary's blue it was silver. Their strength
	a fit to a hull, HALO_H2_CINEMATIC_SPECULAR: f0085 and f0109 of the Outskirts
	footage - on a body it blew a Jackal's arm white: as it is, MCC's Marines the
	same either way) */
	if (h2_material_model_pass_cinematic)
	{
		static float cinematic = -1.0f;

		if (cinematic < 0.0f)
			cinematic = getenv("HALO_H2_CINEMATIC_SPECULAR") ? (float)atof(getenv("HALO_H2_CINEMATIC_SPECULAR")) : 2.33f;
		uniforms[2][3] = 0.0f;
		if (h2_material_model_pass_cinematic_hull)
			uniforms[6][3] *= cinematic;
	}
	/* (the shader's specular type: the highlight's curve) */
	uniforms[7][3] = (float)extension->specular_type;
	memcpy(&uniforms[8][0], &extension->bump_scale[0], sizeof(float));
	memcpy(&uniforms[8][1], &extension->bump_scale[1], sizeof(float));
	uniforms[8][2] = (float)mode;
	/* (tests: HALO_H2_MODEL_DEBUG=1 draws the bump map's normal; the relief's
	floor, a part of the light: HALO_H2_MODEL_RELIEF_FLOOR) */
	{
		static float debug = -1.0f, relief_floor = -1.0f;

		if (debug < 0.0f)
		{
			debug = HALO_ENV_ONCE("HALO_H2_MODEL_DEBUG") ? (atoi(HALO_ENV_ONCE("HALO_H2_MODEL_DEBUG")) >= 2 ? (float)atoi(HALO_ENV_ONCE("HALO_H2_MODEL_DEBUG")) : 1.0f) : 0.0f;
			relief_floor = getenv("HALO_H2_MODEL_RELIEF_FLOOR") ? (float)atof(getenv("HALO_H2_MODEL_RELIEF_FLOOR")) : 0.08f;
		}
		uniforms[8][3] = debug;
		uniforms[9][0] = relief_floor;
		/* (the relief's strength and its bounds - its brightening most: a
		bump's side turned to a grazing light would double, crinkled tinfoil
		on the gate's bars; HALO_H2_MODEL_RELIEF="strength low high") */
		{
			static float relief[3] = { -1.0f, 0.0f, 0.0f };

			if (relief[0] < 0.0f)
			{
				char const *setting = getenv("HALO_H2_MODEL_RELIEF");

				relief[0] = 0.6f, relief[1] = 0.55f, relief[2] = 1.2f;
				if (setting)
					sscanf(setting, "%f %f %f", &relief[0], &relief[1], &relief[2]);
			}
			uniforms[9][1] = relief[0];
			uniforms[9][2] = relief[1];
			uniforms[9][3] = relief[2];
		}
	}
	/* (the secondary colour's pass: its colour for the specular's) */
	if (mode == _h2_model_pass_secondary)
	{
		for (c = 0; c < 3; c++)
			uniforms[6][c] = ambient_color[c];
	}
	/* (a visor's: its colour comes in the camera's place) */
	if (mode == _h2_model_pass_visor || mode == _h2_model_pass_visor_add)
	{
		for (c = 0; c < 3; c++)
			uniforms[6][c] = camera_position[c];
	}
	materials_program(model_pass_vertex_source, model_pass_fragment_source, vita_h2_model_pass_vertex_body,
		vita_h2_model_pass_fragment_body, &uniforms[0][0], H2_MODEL_PASS_UNIFORMS);
}

float h2_material_model_environment_gain(
	struct h2_model_material_extension const *extension)
{
	extern volatile unsigned long halo_settings_generation;
	static unsigned long settings_seen;
	static float gain = -1.0f, pass_gain = 1.0f;

	if (!extension || h2_materials_tier() != _h2_materials_full)
		return 1.0f;
	if (gain < 0.0f || settings_seen != halo_settings_generation)
	{
		char const *setting = getenv("HALO_H2_MODEL_ENV");

		settings_seen = halo_settings_generation;
		gain = setting ? (float)atof(setting) : H2_MODEL_ENVIRONMENT_GAIN;
		setting = getenv("HALO_H2_MODEL_ENV_PASS");
		pass_gain = setting ? (float)atof(setting) : H2_MODEL_ENVIRONMENT_PASS_GAIN;
	}
	return (extension->flags & _h2_model_material_env_pass_bit) ? gain * pass_gain : gain;
}
