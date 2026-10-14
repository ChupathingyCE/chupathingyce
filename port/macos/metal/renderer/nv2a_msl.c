/*
NV2A_MSL.C

The Metal Shading Language frame around the translators' shader bodies
(nv2a_vsh.c, nv2a_psh.c), for struct nv2a_dialect's msl. The bodies are the
GLSL dialect's text, unchanged: a prelude gives GLSL's names their Metal
meanings, and each entry point begins by binding, as locals, every name the
body reads (uniforms, attributes, varyings), so nothing between the two
differs from GLSL.

Bindings, which the Metal backend (port/ios/host/gpu_metal.m) follows:
- vertex: buffer 0 the vertex constants c[192], buffer 1 struct Uniforms,
  buffer 2 struct AttributeTable, buffers 10-25 the vertex buffer each of the
  16 attributes reads (see fetch_attribute);
- fragment: buffer 0 struct Uniforms, textures and samplers 0-3 the stages;
  with a foveated stage (_xgpu_sampler_2d_foveated), buffer 1 the eye's rate
  map's parameter data and buffer 2 each stage's sizes; compiled with
  EXACT_BORDERS (exact border colors, nv2a_msl_fragment_main), also buffer 3
  the stages' border colors and samplers 4-7 opaque white borders.
*/

#include "xgpu.h"

#include <string.h>

/* GLSL's names in Metal terms; the macros that use Metal's own attribute
names (texture) are defined inside the entry point instead, after its
[[texture(n)]] parameters */
static const char msl_prelude[] =
	"#include <metal_stdlib>\n"
	"using namespace metal;\n"
	/* the translators declare every register and the prelude every reader;
	the ones a shader never uses are not worth a warning per shader in the log.
	The compiler runs on the game's thread, whose stack (host_main.m) is not
	the one the system knows about, so it always thinks it is about to run out */
	"#pragma clang diagnostic ignored \"-Wunused-variable\"\n"
	"#pragma clang diagnostic ignored \"-Wunused-function\"\n"
	"#pragma clang diagnostic ignored \"-Wstack-exhausted\"\n"
	"typedef float2 vec2;\n"
	"typedef float3 vec3;\n"
	"typedef float4 vec4;\n"
	"typedef int2 ivec2;\n"
	"typedef int3 ivec3;\n"
	"typedef int4 ivec4;\n"
	"typedef uint2 uvec2;\n"
	"typedef uint3 uvec3;\n"
	"typedef uint4 uvec4;\n"
	"typedef bool2 bvec2;\n"
	"typedef bool3 bvec3;\n"
	"typedef bool4 bvec4;\n"
	"#define inversesqrt rsqrt\n"
	"#define lessThan(a, b) ((a) < (b))\n"
	"#define greaterThanEqual(a, b) ((a) >= (b))\n"
	"#define discard discard_fragment()\n";

/* the vertex shader reads its own attributes (vertex pulling): per draw the
backend describes each of the 16 in an AttributeTable entry, the C struct
gpu_metal_attribute (gpu_metal.m), and binds the buffer it reads at offset 0,
since the front end's offsets need not be aligned. The formats are gpu.h's
GPU_ATTRIBUTE_*, converted as OpenGL ES converts them; missing components are
(0, 0, 0, 1).

Each attribute's kind may also come as function constant n (attribute_kind_in
n): 0 for a constant attribute, else its GPU_ATTRIBUTE_* format. The Metal
backend specializes each pipeline's vertex function with the kinds of the
draws that use it (pipeline_key), so the compiler folds the format switch
away; without them (the shader replays, xcrun metal) the kind is read from
the table per draw, as before. Both read the same bytes the same way.
Function constant indices 0-15 are the attribute kinds; any other constant a
vertex shader takes must start at 16. */
static const char msl_vertex_fetch[] =
	"struct AttributeEntry\n"
	"{\n"
	"\tuint format;\n"
	"\tuint stream;\n"
	"\tuint offset;\n"
	"\tuint stride;\n"
	"};\n"
	"struct AttributeTable\n"
	"{\n"
	"\tAttributeEntry entries[16];\n"
	"\tfloat4 constants[16];\n"
	"};\n"
	"#define ATTRIBUTE_KIND(n) \\\n"
	"\tconstant uint attribute_kind_in##n [[function_constant(n)]]; \\\n"
	"\tconstant uint attribute_kind##n = is_function_constant_defined(attribute_kind_in##n) ? attribute_kind_in##n : 0xffffffffu;\n"
	"ATTRIBUTE_KIND(0) ATTRIBUTE_KIND(1) ATTRIBUTE_KIND(2) ATTRIBUTE_KIND(3)\n"
	"ATTRIBUTE_KIND(4) ATTRIBUTE_KIND(5) ATTRIBUTE_KIND(6) ATTRIBUTE_KIND(7)\n"
	"ATTRIBUTE_KIND(8) ATTRIBUTE_KIND(9) ATTRIBUTE_KIND(10) ATTRIBUTE_KIND(11)\n"
	"ATTRIBUTE_KIND(12) ATTRIBUTE_KIND(13) ATTRIBUTE_KIND(14) ATTRIBUTE_KIND(15)\n"
	"static inline uint read_u16(device const uchar *p) { return uint(p[0]) | (uint(p[1]) << 8); }\n"
	"static inline uint read_u32(device const uchar *p) { return read_u16(p) | (read_u16(p + 2) << 16); }\n"
	"static inline float read_f32(device const uchar *p) { return as_type<float>(read_u32(p)); }\n"
	"static inline float read_s16(device const uchar *p) { return float(short(ushort(read_u16(p)))); }\n"
	"static inline float read_n16(device const uchar *p) { return max(read_s16(p) / 32767.0, -1.0); }\n"
	"static inline float4 fetch_attribute(uint index, uint vid, constant AttributeTable &table, device const uchar *stream,\n"
	"\tuint kind)\n"
	"{\n"
	"\tAttributeEntry e = table.entries[index];\n"
	"\tif (kind == 0xffffffffu ? e.stream >= 16 : kind == 0u)\n"
	"\t\treturn table.constants[index];\n"
	"\tdevice const uchar *p = stream + e.offset + vid * e.stride;\n"
	"\tfloat4 v = float4(0.0, 0.0, 0.0, 1.0);\n"
	"\tswitch (kind == 0xffffffffu ? e.format : kind)\n"
	"\t{\n"
	"\tcase 1: v.x = read_f32(p); break;\n"
	"\tcase 2: v.xy = float2(read_f32(p), read_f32(p + 4)); break;\n"
	"\tcase 3: v.xyz = float3(read_f32(p), read_f32(p + 4), read_f32(p + 8)); break;\n"
	"\tcase 4: v = float4(read_f32(p), read_f32(p + 4), read_f32(p + 8), read_f32(p + 12)); break;\n"
	"\tcase 5: v = float4(p[2], p[1], p[0], p[3]) / 255.0; break;\n"
	"\tcase 6: v = float4(p[0], p[1], p[2], p[3]) / 255.0; break;\n"
	"\tcase 7: v.x = read_s16(p); break;\n"
	"\tcase 8: v.xy = float2(read_s16(p), read_s16(p + 2)); break;\n"
	"\tcase 9: v.xyz = float3(read_s16(p), read_s16(p + 2), read_s16(p + 4)); break;\n"
	"\tcase 10: v = float4(read_s16(p), read_s16(p + 2), read_s16(p + 4), read_s16(p + 6)); break;\n"
	"\tcase 11: v.x = read_n16(p); break;\n"
	"\tcase 12: v.xy = float2(read_n16(p), read_n16(p + 2)); break;\n"
	"\tcase 13: v.xyz = float3(read_n16(p), read_n16(p + 2), read_n16(p + 4)); break;\n"
	"\tcase 14: v = float4(read_n16(p), read_n16(p + 2), read_n16(p + 4), read_n16(p + 6)); break;\n"
	"\tcase 15: v.x = p[0] / 255.0; break;\n"
	"\tcase 16: v.xy = float2(p[0], p[1]) / 255.0; break;\n"
	"\tcase 17: v.xyz = float3(p[0], p[1], p[2]) / 255.0; break;\n"
	"\tcase 18: v = float4(p[0], p[1], p[2], p[3]) / 255.0; break;\n"
	"\tdefault: break;\n"
	"\t}\n"
	"\treturn v;\n"
	"}\n"
	/* NORMPACKED3: the raw word unpack_normpacked3 expects; a constant
	packed attribute is 0, which unpacks to (0, 0, 0, 1), as GL's
	glVertexAttribI4ui(0, 0, 0, 0) (gpu_gl.c) */
	"static inline uint fetch_packed(uint index, uint vid, constant AttributeTable &table, device const uchar *stream,\n"
	"\tuint kind)\n"
	"{\n"
	"\tAttributeEntry e = table.entries[index];\n"
	"\tif (kind == 0xffffffffu ? e.stream >= 16 : kind == 0u)\n"
	"\t\treturn 0u;\n"
	"\treturn read_u32(stream + e.offset + vid * e.stride);\n"
	"}\n";

static const char *const varyings[] = { "xD0", "xD1", "xB0", "xB1", "xT0", "xT1", "xT2", "xT3" };
#define VARYING_COUNT (sizeof(varyings) / sizeof(varyings[0]))

/* the uniforms, in gpu_uniforms.h's order and C's layout: every row a
float[count][4] in struct gpu_uniforms, so float4 name[count] here. Both
stages declare the whole struct, since the backend binds the same bytes to
each */
static void msl_uniform_struct(struct xgpu_text *text)
{
	xgpu_text_append(text, "struct Uniforms\n{\n");
#define DECLARE_ROW(name, glsl_type, count, uniform_stage) \
	if ((uniform_stage) != GPU_UNIFORM_VERTEX_CONSTANTS) \
		xgpu_text_append(text, "\tfloat4 " #name "[%d];\n", (int)(count));
	GPU_UNIFORMS(DECLARE_ROW)
#undef DECLARE_ROW
	xgpu_text_append(text, "};\n");
}

void nv2a_msl_prelude(struct xgpu_text *text, int stage)
{
	xgpu_text_append(text, "%s", msl_prelude);
	msl_uniform_struct(text);
	if (stage == GPU_UNIFORM_VERTEX)
		xgpu_text_append(text, "%s", msl_vertex_fetch);
}

/* the names the GLSL dialect declares as uniforms (nv2a_uniform_declarations,
with its stage rule), as locals from struct Uniforms u */
static void msl_uniform_locals(struct xgpu_text *text, const struct nv2a_dialect *dialect, int stage)
{
#define BIND_UNIFORM(name, glsl_type, count, uniform_stage) \
	if ((uniform_stage) == stage || \
		((uniform_stage) == GPU_UNIFORM_PIXEL_LOD_BIAS && stage == GPU_UNIFORM_PIXEL && dialect->shader_lod_bias)) \
	{ \
		if ((count) > 1) \
			xgpu_text_append(text, "\tconstant float4 *" #name " = u." #name ";\n"); \
		else if (!strcmp(#glsl_type, "float")) \
			xgpu_text_append(text, "\tfloat " #name " = u." #name "[0].x;\n"); \
		else \
			xgpu_text_append(text, "\tvec4 " #name " = u." #name "[0];\n"); \
	}
	GPU_UNIFORMS(BIND_UNIFORM)
#undef BIND_UNIFORM
}

void nv2a_msl_vertex_outputs(struct xgpu_text *text)
{
	unsigned long index;

	xgpu_text_append(text,
		"struct VertexOut\n"
		"{\n"
		"\tfloat4 gl_Position [[position, invariant]];\n"
		"\tfloat gl_PointSize [[point_size]];\n");
	for (index = 0; index < VARYING_COUNT; index++)
		xgpu_text_append(text, "\tfloat4 %s [[user(%s)]];\n", varyings[index], varyings[index]);
	xgpu_text_append(text, "\tfloat xFog [[user(xFog)]];\n};\n");
}

void nv2a_msl_vertex_main(struct xgpu_text *text, const struct nv2a_dialect *dialect,
	unsigned long packed_attribute_mask)
{
	unsigned long index;

	xgpu_text_append(text,
		"vertex VertexOut vertex_main(uint vid [[vertex_id]], constant float4 *c [[buffer(0)]],\n"
		"\tconstant Uniforms &u [[buffer(1)]], constant AttributeTable &table [[buffer(2)]]");
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		xgpu_text_append(text, ",\n\tdevice const uchar *stream%lu [[buffer(%lu)]]", index, 10 + index);
	xgpu_text_append(text, ")\n{\n");
	msl_uniform_locals(text, dialect, GPU_UNIFORM_VERTEX);
	/* the inputs the GLSL dialect declares, which its v%lu = v%lu_in copies
	read */
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (packed_attribute_mask & (1UL << index))
			xgpu_text_append(text, "\tuint v%lu_packed = fetch_packed(%lu, vid, table, stream%lu, attribute_kind%lu);\n",
				index, index, index, index);
		else
			xgpu_text_append(text, "\tvec4 v%lu_in = fetch_attribute(%lu, vid, table, stream%lu, attribute_kind%lu);\n",
				index, index, index, index);
	}
	xgpu_text_append(text, "\tfloat4 gl_Position;\n\tfloat gl_PointSize;\n");
	for (index = 0; index < VARYING_COUNT; index++)
		xgpu_text_append(text, "\tvec4 %s;\n", varyings[index]);
	xgpu_text_append(text, "\tfloat xFog;\n");
}

void nv2a_msl_vertex_return(struct xgpu_text *text)
{
	unsigned long index;

	xgpu_text_append(text, "\tVertexOut out;\n\tout.gl_Position = gl_Position;\n\tout.gl_PointSize = gl_PointSize;\n");
	for (index = 0; index < VARYING_COUNT; index++)
		xgpu_text_append(text, "\tout.%s = %s;\n", varyings[index], varyings[index]);
	xgpu_text_append(text, "\tout.xFog = xFog;\n\treturn out;\n");
}

void nv2a_msl_fragment_inputs(struct xgpu_text *text)
{
	unsigned long index;

	xgpu_text_append(text, "struct FragmentIn\n{\n");
	for (index = 0; index < VARYING_COUNT; index++)
		xgpu_text_append(text, "\tfloat4 %s [[user(%s)]];\n", varyings[index], varyings[index]);
	xgpu_text_append(text, "\tfloat xFog [[user(xFog)]];\n};\n");
}

void nv2a_msl_fragment_main(struct xgpu_text *text, const struct nv2a_dialect *dialect,
	const struct nv2a_pixel_shader_key *key)
{
	unsigned long index;
	int stage, foveated = 0;

	for (stage = 0; stage < 4; stage++)
		foveated |= key->sampler_type[stage] == _xgpu_sampler_2d_foveated;
	/* a foveated eye's screen-sized target (_xgpu_sampler_2d_foveated): its
	texels are where the eye's rate map put each screen pixel, at the top
	left of a target allocated larger. The lookup's normalized coordinates
	times the screen size, inside it, go through the map's decoder (the map's
	parameter data in buffer 1), then stay half a texel inside the map's
	physical region (past it the texels are stale) and are divided by the
	allocated size. Buffer 2 holds two float4s a stage: the screen size and
	the physical size, then the allocated size (gpu_metal.m's bind_stages) */
	if (foveated)
		xgpu_text_append(text,
			"static float2 foveated_coordinates(float2 coordinates, constant rasterization_rate_map_data &map,\n"
			"\tfloat4 screen_physical, float4 allocated)\n"
			"{\n"
			"\trasterization_rate_map_decoder decoder(map);\n"
			"\tfloat2 screen = clamp(coordinates * screen_physical.xy, float2(0.0), screen_physical.xy - 1.0 / 256.0);\n"
			"\tfloat2 physical = clamp(decoder.map_screen_to_physical_coordinates(screen), float2(0.5),\n"
			"\t\tscreen_physical.zw - 0.5);\n"
			"\treturn physical / allocated.xy;\n"
			"}\n");
	/* exact border colors: EXACT_BORDERS, which the Metal backend defines
	only when it compiles a shader again for a draw that needs it, has bit n
	set for each stage n whose BORDER addressing has a color Metal's samplers
	lack. Such a stage samples twice, with a transparent black border (its own
	sampler) and an opaque white one (sampler 4 + n): their difference is the
	border's weight in the filtered sample, the same in every channel, so
	black + color * (white - black) is the sample with the game's color
	(borders, buffer 3). Without it, the text compiles as it always has. */
	xgpu_text_append(text,
		"#ifndef EXACT_BORDERS\n"
		"#define EXACT_BORDERS 0\n"
		"#endif\n"
		"#if EXACT_BORDERS\n"
		"struct Borders\n{\n\tfloat4 tex0, tex1, tex2, tex3;\n};\n"
		"template <typename T, typename C>\n"
		"static inline float4 border_sample(T t, sampler black, sampler white, float4 color, C coordinates, bias b)\n"
		"{\n"
		"\tfloat4 plain = t.sample(black, coordinates, b);\n"
		"\treturn plain + color * (t.sample(white, coordinates, b) - plain);\n"
		"}\n"
		"#endif\n");
	xgpu_text_append(text, "fragment float4 fragment_main(FragmentIn in [[stage_in]], constant Uniforms &u [[buffer(0)]]");
	for (stage = 0; stage < 4; stage++)
	{
		const char *type = key->sampler_type[stage] == _xgpu_sampler_3d ? "texture3d<float>" :
			key->sampler_type[stage] == _xgpu_sampler_cube ? "texturecube<float>" : "texture2d<float>";

		xgpu_text_append(text, ",\n\t%s tex%d [[texture(%d)]], sampler tex%d_sampler [[sampler(%d)]]",
			type, stage, stage, stage, stage);
	}
	if (foveated)
		xgpu_text_append(text, ",\n\tconstant rasterization_rate_map_data &rate_map [[buffer(1)]],"
			"\n\tconstant float4 *foveation [[buffer(2)]]");
	xgpu_text_append(text, "\n#if EXACT_BORDERS\n\t, constant Borders &borders [[buffer(3)]]\n#endif\n");
	for (stage = 0; stage < 4; stage++)
		xgpu_text_append(text, "#if EXACT_BORDERS & %d\n\t, sampler tex%d_white [[sampler(%d)]]\n#endif\n",
			1 << stage, stage, 4 + stage);
	xgpu_text_append(text, ")\n{\n");
	/* after the signature: defined before it, this would also replace the
	[[texture(n)]] attributes. Every lookup has a bias (shader_lod_bias). */
	xgpu_text_append(text, "#if EXACT_BORDERS\n");
	for (stage = 0; stage < 4; stage++)
		xgpu_text_append(text,
			"#if EXACT_BORDERS & %d\n"
			"#define tex%d_lookup(coordinates, b) border_sample(tex%d, tex%d_sampler, tex%d_white, borders.tex%d, coordinates, b)\n"
			"#else\n"
			"#define tex%d_lookup(coordinates, b) tex%d.sample(tex%d_sampler, coordinates, b)\n"
			"#endif\n",
			1 << stage, stage, stage, stage, stage, stage, stage, stage, stage);
	xgpu_text_append(text,
		"#define texture(t, coordinates, b) t##_lookup(coordinates, bias(b))\n"
		"#else\n"
		"#define texture(t, coordinates, b) (t).sample(t##_sampler, coordinates, bias(b))\n"
		"#endif\n");
	if (foveated)
		xgpu_text_append(text, "#define texture_foveated(stage, t, coordinates, b) (t).sample(t##_sampler, "
			"foveated_coordinates(coordinates, rate_map, foveation[(stage) * 2], foveation[(stage) * 2 + 1]), bias(b))\n");
	for (index = 0; index < VARYING_COUNT; index++)
		xgpu_text_append(text, "\tvec4 %s = in.%s;\n", varyings[index], varyings[index]);
	xgpu_text_append(text, "\tfloat xFog = in.xFog;\n");
	msl_uniform_locals(text, dialect, GPU_UNIFORM_PIXEL);
	xgpu_text_append(text, "\tfloat4 fragment_color;\n");
}
