/*
NV2A_VSH.C

Translation of NV2A vertex programs (Xbox vertex shader microcode) into
GLSL.

Each instruction is four little-endian words. Word 1 to 3 hold a MAC
(vector) operation and an ILU (scalar) operation that execute in parallel
on the same three operands A, B and C, and the destinations of both; the
bit positions of every field are spelled out in operand() and in
nv2a_vertex_shader_translate(). When both units run, the ILU result goes to
temporary r1, whatever the instruction's temporary register field says.

Xbox vertex programs finish by converting their clip-space position to
screen space with the viewport constants c[-38] and c[-37], which Direct3D
maintains. The generated shader inverts that transform to hand OpenGL a
clip-space position again.
*/

#include "xgpu.h"
#include "gpu_uniforms.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- instruction fields */

static unsigned long field(const DWORD *instruction, int word, int low_bit, int bit_count)
{
	return (instruction[word] >> low_bit) & ((1UL << bit_count) - 1);
}

enum
{
	_mac_nop, _mac_mov, _mac_mul, _mac_add, _mac_mad, _mac_dp3, _mac_dph, _mac_dp4,
	_mac_dst, _mac_min, _mac_max, _mac_slt, _mac_sge, _mac_arl,
};

enum
{
	_ilu_nop, _ilu_mov, _ilu_rcp, _ilu_rcc, _ilu_rsq, _ilu_exp, _ilu_log, _ilu_lit,
};

enum
{
	_mux_unknown, _mux_temporary, _mux_input, _mux_constant,
};

/* output register addresses (o[]) */
static const char *output_name(unsigned long address)
{
	switch (address)
	{
	case 0: return "oPos";
	case 3: return "oD0";
	case 4: return "oD1";
	case 5: return "oFog";
	case 6: return "oPts";
	case 7: return "oB0";
	case 8: return "oB1";
	case 9: return "oT0";
	case 10: return "oT1";
	case 11: return "oT2";
	case 12: return "oT3";
	default: return "oUnused";
	}
}

static void write_mask(unsigned long mask, char *out)
{
	/* bit 3 is x */
	int count = 0;

	if (mask & 8) out[count++] = 'x';
	if (mask & 4) out[count++] = 'y';
	if (mask & 2) out[count++] = 'z';
	if (mask & 1) out[count++] = 'w';
	out[count] = 0;
}

static void operand(struct xgpu_text *text, const DWORD *instruction, char which, int relative)
{
	static const char swizzle_names[] = "xyzw";
	unsigned long negate, swizzle[4], index, mux;

	switch (which)
	{
	case 'A':
		negate = field(instruction, 1, 8, 1);
		swizzle[0] = field(instruction, 1, 6, 2);
		swizzle[1] = field(instruction, 1, 4, 2);
		swizzle[2] = field(instruction, 1, 2, 2);
		swizzle[3] = field(instruction, 1, 0, 2);
		index = field(instruction, 2, 28, 4);
		mux = field(instruction, 2, 26, 2);
		break;
	case 'B':
		negate = field(instruction, 2, 25, 1);
		swizzle[0] = field(instruction, 2, 23, 2);
		swizzle[1] = field(instruction, 2, 21, 2);
		swizzle[2] = field(instruction, 2, 19, 2);
		swizzle[3] = field(instruction, 2, 17, 2);
		index = field(instruction, 2, 13, 4);
		mux = field(instruction, 2, 11, 2);
		break;
	default:
		negate = field(instruction, 2, 10, 1);
		swizzle[0] = field(instruction, 2, 8, 2);
		swizzle[1] = field(instruction, 2, 6, 2);
		swizzle[2] = field(instruction, 2, 4, 2);
		swizzle[3] = field(instruction, 2, 2, 2);
		index = (field(instruction, 2, 0, 2) << 2) | field(instruction, 3, 30, 2);
		mux = field(instruction, 3, 28, 2);
		break;
	}

	xgpu_text_append(text, "%s", negate ? "-" : "");
	switch (mux)
	{
	case _mux_temporary:
		/* r12 reads back the position output */
		if (index == 12)
			xgpu_text_append(text, "oPos");
		else
			xgpu_text_append(text, "r%lu", index);
		break;
	case _mux_input:
		xgpu_text_append(text, "v%lu", field(instruction, 1, 9, 4));
		break;
	case _mux_constant:
		if (relative)
			xgpu_text_append(text, "c[clamp(a0 + %lu, 0, %d)]", field(instruction, 1, 13, 8), XGPU_VERTEX_CONSTANT_COUNT - 1);
		else
			xgpu_text_append(text, "c[%lu]", field(instruction, 1, 13, 8));
		break;
	default:
		xgpu_text_append(text, "vec4(0.0)");
		break;
	}
	xgpu_text_append(text, ".%c%c%c%c",
		swizzle_names[swizzle[0]], swizzle_names[swizzle[1]], swizzle_names[swizzle[2]], swizzle_names[swizzle[3]]);
}

/* ---------- uniforms */

void nv2a_uniform_declarations(struct xgpu_text *text, const struct nv2a_dialect *dialect, int stage)
{
#define DECLARE_UNIFORM(name, glsl_type, count, uniform_stage) \
	if ((uniform_stage) == stage || \
		((uniform_stage) == GPU_UNIFORM_VERTEX_CONSTANTS && stage == GPU_UNIFORM_VERTEX) || \
		((uniform_stage) == GPU_UNIFORM_PIXEL_LOD_BIAS && stage == GPU_UNIFORM_PIXEL && dialect->shader_lod_bias)) \
	{ \
		if ((count) > 1) \
			xgpu_text_append(text, "uniform " #glsl_type " " #name "[%d];\n", (int)(count)); \
		else \
			xgpu_text_append(text, "uniform " #glsl_type " " #name ";\n"); \
	}
	GPU_UNIFORMS(DECLARE_UNIFORM)
#undef DECLARE_UNIFORM
}
static const char shader_outputs[] =
	"out vec4 xD0;\n"
	"out vec4 xD1;\n"
	"out vec4 xB0;\n"
	"out vec4 xB1;\n"
	"out vec4 xT0;\n"
	"out vec4 xT1;\n"
	"out vec4 xT2;\n"
	"out vec4 xT3;\n"
	"out float xFog;\n"
	"invariant gl_Position;\n";
/* GLSL and MSL alike */
static const char shader_helpers[] =
	"vec4 unpack_normpacked3(uint p)\n"
	"{\n"
	"	int x = int(p << 21) >> 21;\n"
	"	int y = int(p << 10) >> 21;\n"
	"	int z = int(p) >> 22;\n"
	"	return vec4(float(x) / 1023.0, float(y) / 1023.0, float(z) / 511.0, 1.0);\n"
	"}\n"
	"vec4 nv2a_rcc(float x)\n"
	"{\n"
	"	float r = 1.0 / x;\n"
	"	if (r > 0.0) r = clamp(r, 5.42101e-20, 1.884467e+19);\n"
	"	else r = clamp(r, -1.884467e+19, -5.42101e-20);\n"
	"	return vec4(r);\n"
	"}\n"
	"vec4 nv2a_exp(float x)\n"
	"{\n"
	"	return vec4(exp2(floor(x)), fract(x), exp2(x), 1.0);\n"
	"}\n"
	"vec4 nv2a_log(float x)\n"
	"{\n"
	"	x = abs(x);\n"
	"	if (x == 0.0) return vec4(-1.0e30, 1.0, -1.0e30, 1.0);\n"
	"	float e = floor(log2(x));\n"
	"	return vec4(e, x / exp2(e), log2(x), 1.0);\n"
	"}\n"
	"vec4 nv2a_lit(vec4 s)\n"
	"{\n"
	"	float specular = s.x > 0.0 ? pow(max(s.y, 0.0), clamp(s.w, -127.9961, 127.9961)) : 0.0;\n"
	"	return vec4(1.0, max(s.x, 0.0), specular, 1.0);\n"
	"}\n";

char *nv2a_vertex_shader_translate(const struct nv2a_dialect *dialect, const DWORD *instructions,
	unsigned long instruction_count, unsigned long packed_attribute_mask)
{
	struct xgpu_text text = { 0 };
	unsigned long index;


	if (!dialect->version)
	{
		platform_log("vertex shader translated before the dialect was set");
		return NULL;
	}
	if (dialect->msl)
	{
		nv2a_msl_prelude(&text, GPU_UNIFORM_VERTEX);
		nv2a_msl_vertex_outputs(&text);
		xgpu_text_append(&text, "%s", shader_helpers);
		nv2a_msl_vertex_main(&text, dialect, packed_attribute_mask);
	}
	else
	{
		if (dialect->es)
			xgpu_text_append(&text, "#version %u es\nprecision highp float;\nprecision highp int;\n", (unsigned)dialect->version);
		else
			xgpu_text_append(&text, "#version %u core\n", (unsigned)dialect->version);
		nv2a_uniform_declarations(&text, dialect, GPU_UNIFORM_VERTEX);
		xgpu_text_append(&text, "%s%s", shader_outputs, shader_helpers);
		for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		{
			if (packed_attribute_mask & (1UL << index))
				xgpu_text_append(&text, "layout(location = %lu) in uint v%lu_packed;\n", index, index);
			else
				xgpu_text_append(&text, "layout(location = %lu) in vec4 v%lu_in;\n", index, index);
		}

		xgpu_text_append(&text, "void main()\n{\n");
	}
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (packed_attribute_mask & (1UL << index))
			xgpu_text_append(&text, "\tvec4 v%lu = unpack_normpacked3(v%lu_packed);\n", index, index);
		else
			xgpu_text_append(&text, "\tvec4 v%lu = v%lu_in;\n", index, index);
	}
	xgpu_text_append(&text,
		"\tvec4 r0 = vec4(0.0), r1 = vec4(0.0), r2 = vec4(0.0), r3 = vec4(0.0);\n"
		"\tvec4 r4 = vec4(0.0), r5 = vec4(0.0), r6 = vec4(0.0), r7 = vec4(0.0);\n"
		"\tvec4 r8 = vec4(0.0), r9 = vec4(0.0), r10 = vec4(0.0), r11 = vec4(0.0);\n"
		"\tvec4 oPos = vec4(0.0, 0.0, 0.0, 1.0);\n"
		"\tvec4 oD0 = vec4(0.0, 0.0, 0.0, 1.0), oD1 = vec4(0.0, 0.0, 0.0, 1.0);\n"
		"\tvec4 oB0 = vec4(0.0, 0.0, 0.0, 1.0), oB1 = vec4(0.0, 0.0, 0.0, 1.0);\n"
		"\tvec4 oT0 = vec4(0.0, 0.0, 0.0, 1.0), oT1 = vec4(0.0, 0.0, 0.0, 1.0);\n"
		"\tvec4 oT2 = vec4(0.0, 0.0, 0.0, 1.0), oT3 = vec4(0.0, 0.0, 0.0, 1.0);\n"
		"\tvec4 oFog = vec4(1.0), oPts = vec4(point_size), oUnused = vec4(0.0);\n"
		"\tint a0 = 0;\n"
		"\tvec4 A, B, C, mac, ilu;\n");
	if (dialect->clip_capture)
		xgpu_text_append(&text, "\tvec4 clip_position = vec4(0.0);\n\tbool clip_captured = false;\n");

	for (index = 0; index < instruction_count; index++)
	{
		const DWORD *instruction = instructions + index * 4;
		unsigned long mac = field(instruction, 1, 21, 4);
		unsigned long ilu = field(instruction, 1, 25, 3);
		unsigned long mac_mask = field(instruction, 3, 24, 4);
		unsigned long temporary = field(instruction, 3, 20, 4);
		unsigned long ilu_mask = field(instruction, 3, 16, 4);
		unsigned long output_mask = field(instruction, 3, 12, 4);
		unsigned long output_is_register = field(instruction, 3, 11, 1);
		unsigned long output_address = field(instruction, 3, 3, 8);
		unsigned long output_from_ilu = field(instruction, 3, 2, 1);
		int relative = (int)field(instruction, 3, 1, 1);
		char mask[5];

		xgpu_text_append(&text, "\t/* %lu */\n", index);
		xgpu_text_append(&text, "\tA = "); operand(&text, instruction, 'A', relative); xgpu_text_append(&text, ";\n");
		xgpu_text_append(&text, "\tB = "); operand(&text, instruction, 'B', relative); xgpu_text_append(&text, ";\n");
		xgpu_text_append(&text, "\tC = "); operand(&text, instruction, 'C', relative); xgpu_text_append(&text, ";\n");

		switch (mac)
		{
		case _mac_nop: break;
		case _mac_mov: xgpu_text_append(&text, "\tmac = A;\n"); break;
		case _mac_mul: xgpu_text_append(&text, "\tmac = A * B;\n"); break;
		case _mac_add: xgpu_text_append(&text, "\tmac = A + C;\n"); break;
		case _mac_mad: xgpu_text_append(&text, "\tmac = A * B + C;\n"); break;
		case _mac_dp3: xgpu_text_append(&text, "\tmac = vec4(dot(A.xyz, B.xyz));\n"); break;
		case _mac_dph: xgpu_text_append(&text, "\tmac = vec4(dot(A.xyz, B.xyz) + B.w);\n"); break;
		case _mac_dp4: xgpu_text_append(&text, "\tmac = vec4(dot(A, B));\n"); break;
		case _mac_dst: xgpu_text_append(&text, "\tmac = vec4(1.0, A.y * B.y, A.z, B.w);\n"); break;
		case _mac_min: xgpu_text_append(&text, "\tmac = min(A, B);\n"); break;
		case _mac_max: xgpu_text_append(&text, "\tmac = max(A, B);\n"); break;
		case _mac_slt: xgpu_text_append(&text, "\tmac = vec4(lessThan(A, B));\n"); break;
		case _mac_sge: xgpu_text_append(&text, "\tmac = vec4(greaterThanEqual(A, B));\n"); break;
		case _mac_arl: xgpu_text_append(&text, "\tmac = A;\n"); break;
		default: xgpu_text_append(&text, "\tmac = vec4(0.0);\n"); break;
		}
		switch (ilu)
		{
		case _ilu_nop: break;
		case _ilu_mov: xgpu_text_append(&text, "\tilu = C;\n"); break;
		case _ilu_rcp: xgpu_text_append(&text, "\tilu = vec4(1.0 / C.x);\n"); break;
		case _ilu_rcc: xgpu_text_append(&text, "\tilu = nv2a_rcc(C.x);\n"); break;
		case _ilu_rsq: xgpu_text_append(&text, "\tilu = vec4(inversesqrt(abs(C.x)));\n"); break;
		case _ilu_exp: xgpu_text_append(&text, "\tilu = nv2a_exp(C.x);\n"); break;
		case _ilu_log: xgpu_text_append(&text, "\tilu = nv2a_log(C.x);\n"); break;
		case _ilu_lit: xgpu_text_append(&text, "\tilu = nv2a_lit(C);\n"); break;
		default: xgpu_text_append(&text, "\tilu = vec4(0.0);\n"); break;
		}
		/* the screen-space conversion takes the reciprocal of the clip-space
		position's w (rcc of r12.w); keep the position it converts */
		if (dialect->clip_capture && ilu == _ilu_rcc && field(instruction, 3, 28, 2) == _mux_temporary &&
			((field(instruction, 2, 0, 2) << 2) | field(instruction, 3, 30, 2)) == 12)
		{
			xgpu_text_append(&text, "\tclip_position = oPos;\n\tclip_captured = true;\n");
		}

		/* results are written only after both units have read their inputs */
		if (mac == _mac_arl)
		{
			xgpu_text_append(&text, "\ta0 = int(floor(mac.x + 0.001));\n");
		}
		else if (mac != _mac_nop && mac_mask)
		{
			write_mask(mac_mask, mask);
			if (temporary == 12)
				xgpu_text_append(&text, "\toPos.%s = mac.%s;\n", mask, mask);
			else
				xgpu_text_append(&text, "\tr%lu.%s = mac.%s;\n", temporary, mask, mask);
		}
		if (ilu != _ilu_nop && ilu_mask)
		{
			unsigned long ilu_temporary = mac != _mac_nop ? 1 : temporary;

			write_mask(ilu_mask, mask);
			if (ilu_temporary == 12)
				xgpu_text_append(&text, "\toPos.%s = ilu.%s;\n", mask, mask);
			else
				xgpu_text_append(&text, "\tr%lu.%s = ilu.%s;\n", ilu_temporary, mask, mask);
		}
		if (output_mask && (output_from_ilu ? ilu : mac) != 0)
		{
			const char *source = output_from_ilu ? "ilu" : "mac";

			write_mask(output_mask, mask);
			if (output_is_register)
				xgpu_text_append(&text, "\t%s.%s = %s.%s;\n", output_name(output_address), mask, source, mask);
			/* writes to constant memory are not used by Halo's shaders */
		}
		if (field(instruction, 3, 0, 1))
			break;
	}

	xgpu_text_append(&text,
		"\t/* undo the screen-space conversion done with c[-38] and c[-37] */\n"
		"\tvec3 scale = vec3(viewport_scale.x != 0.0 ? viewport_scale.x : 1.0,\n"
		"\t\tviewport_scale.y != 0.0 ? viewport_scale.y : 1.0,\n"
		"\t\tviewport_scale.z != 0.0 ? viewport_scale.z : 1.0);\n");
	/* Direct3D 8 puts pixel centers on integer screen coordinates (the game
	offsets its screen-space quads by -0.5 to match), OpenGL on half-integers */
	if (dialect->clip_capture)
	{
		/* The conversion is screen = clip * c[-38] * rcc(w) + c[-37]; undoing
		it by multiplying by w again is lossy near the camera plane, where rcc
		clamps and 1/w rounds differently on each GPU (Mali put vertices of
		the first-person weapon at the vanishing point). Where the clip
		position was kept, the same result is computed without dividing. */
		xgpu_text_append(&text,
			"\tvec4 position;\n"
			"\tif (clip_captured)\n"
			"\t\tposition = vec4((clip_position.xyz * c[%d].xyz + (c[%d].xyz + vec3(0.5 + screen_offset, 0.5, 0.0)\n"
			"\t\t\t- viewport_offset.xyz) * clip_position.w) / scale, clip_position.w);\n"
			"\telse\n"
			"\t\tposition = vec4((vec3(oPos.xy + vec2(0.5 + screen_offset, 0.5), oPos.z) - viewport_offset.xyz) / scale * oPos.w, oPos.w);\n",
			XGPU_VERTEX_CONSTANT_BIAS - 38, XGPU_VERTEX_CONSTANT_BIAS - 37);
	}
	else
	{
		xgpu_text_append(&text,
			"\tvec3 ndc = (vec3(oPos.xy + vec2(0.5 + screen_offset, 0.5), oPos.z) - viewport_offset.xyz) / scale;\n"
			"\tvec4 position = vec4(ndc * oPos.w, oPos.w);\n");
	}
	/* A position whose w is zero, or is not a number, is the clip-space
	origin: the screen conversion's reciprocal is clamped rather than
	infinite, so a large position times a w of zero is exactly zero, and the
	origin is inside the frustum. Nothing then clips the triangle away and
	the divide by w puts the vertex in the middle of the screen: the triangle
	is drawn out to it from the first-person weapon, whose pose follows the
	camera and so reaches the camera plane. The divide on the Xbox sends such
	a vertex to infinity and the clipper takes the triangle; put it behind the
	camera instead, which the clipper also takes (upstream's 3d2c04d6). */
	xgpu_text_append(&text,
		"\tif (!(abs(position.w) > 0.0))\n"
		"\t\tposition = vec4(0.0, 0.0, 0.0, -1.0);\n"
		"\tgl_Position = position;\n");
	/* what glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE) does on desktop GL:
	rows from the top, depth 0..1 */
	if (dialect->clip_y_flip)
		xgpu_text_append(&text, "\tgl_Position.y = -gl_Position.y;\n");
	if (dialect->clip_z_remap)
		xgpu_text_append(&text, "\tgl_Position.z = 2.0 * gl_Position.z - gl_Position.w;\n");
	/* Metal draws reversed-Z (gpu_metal.m): depth 1 at the near plane, 0 at
	the far, so a float depth buffer keeps its precision at a distance */
	if (dialect->msl)
		xgpu_text_append(&text, "\tgl_Position.z = gl_Position.w - gl_Position.z;\n");
	xgpu_text_append(&text,
		"\tgl_PointSize = oPts.x;\n"
		"\txD0 = clamp(oD0, 0.0, 1.0);\n"
		"\txD1 = clamp(oD1, 0.0, 1.0);\n"
		"\txB0 = clamp(oB0, 0.0, 1.0);\n"
		"\txB1 = clamp(oB1, 0.0, 1.0);\n"
		"\txT0 = oT0;\n"
		"\txT1 = oT1;\n"
		"\txT2 = oT2;\n"
		"\txT3 = oT3;\n"
		"\txFog = oFog.x;\n");
	if (dialect->msl)
		nv2a_msl_vertex_return(&text);
	xgpu_text_append(&text, "}\n");
	return text.buffer;
}

/* ---------- the program on the CPU (nv2a_vertex_program_position) */

/* an operand as operand() reads it, with its swizzle and negation */
static void operand_value(const DWORD *instruction, char which, int relative, int a0, const float (*constants)[4],
	const float (*inputs)[4], const float (*temporaries)[4], const float position[4], float value[4])
{
	unsigned long negate, swizzle[4], index, mux;
	const float *from = NULL;
	static const float zero[4];
	int axis;

	switch (which)
	{
	case 'A':
		negate = field(instruction, 1, 8, 1);
		swizzle[0] = field(instruction, 1, 6, 2);
		swizzle[1] = field(instruction, 1, 4, 2);
		swizzle[2] = field(instruction, 1, 2, 2);
		swizzle[3] = field(instruction, 1, 0, 2);
		index = field(instruction, 2, 28, 4);
		mux = field(instruction, 2, 26, 2);
		break;
	case 'B':
		negate = field(instruction, 2, 25, 1);
		swizzle[0] = field(instruction, 2, 23, 2);
		swizzle[1] = field(instruction, 2, 21, 2);
		swizzle[2] = field(instruction, 2, 19, 2);
		swizzle[3] = field(instruction, 2, 17, 2);
		index = field(instruction, 2, 13, 4);
		mux = field(instruction, 2, 11, 2);
		break;
	default:
		negate = field(instruction, 2, 10, 1);
		swizzle[0] = field(instruction, 2, 8, 2);
		swizzle[1] = field(instruction, 2, 6, 2);
		swizzle[2] = field(instruction, 2, 4, 2);
		swizzle[3] = field(instruction, 2, 2, 2);
		index = (field(instruction, 2, 0, 2) << 2) | field(instruction, 3, 30, 2);
		mux = field(instruction, 3, 28, 2);
		break;
	}
	switch (mux)
	{
	case _mux_temporary:
		from = index == 12 ? position : temporaries[index < 12 ? index : 0];
		break;
	case _mux_input:
		from = inputs[field(instruction, 1, 9, 4)];
		break;
	case _mux_constant:
		{
			long constant = (long)field(instruction, 1, 13, 8) + (relative ? a0 : 0);

			constant = constant < 0 ? 0 : constant > XGPU_VERTEX_CONSTANT_COUNT - 1 ? XGPU_VERTEX_CONSTANT_COUNT - 1 :
				constant;
			from = constants[constant];
		}
		break;
	default:
		from = zero;
		break;
	}
	for (axis = 0; axis < 4; axis++)
		value[axis] = negate ? -from[swizzle[axis]] : from[swizzle[axis]];
}

static void masked_write(float *to, const float *from, unsigned long mask)
{
	/* bit 3 is x */
	if (mask & 8) to[0] = from[0];
	if (mask & 4) to[1] = from[1];
	if (mask & 2) to[2] = from[2];
	if (mask & 1) to[3] = from[3];
}

void nv2a_vertex_program_position(const DWORD *instructions, unsigned long instruction_count,
	const float (*constants)[4], const float (*inputs)[4], float position[4])
{
	float temporaries[12][4];
	unsigned long index;
	int a0 = 0, axis;

	memset(temporaries, 0, sizeof(temporaries));
	/* oPos, which r12 also names */
	position[0] = position[1] = position[2] = 0.0f;
	position[3] = 1.0f;
	for (index = 0; index < instruction_count; index++)
	{
		const DWORD *instruction = instructions + index * 4;
		unsigned long mac = field(instruction, 1, 21, 4);
		unsigned long ilu = field(instruction, 1, 25, 3);
		unsigned long mac_mask = field(instruction, 3, 24, 4);
		unsigned long temporary = field(instruction, 3, 20, 4);
		unsigned long ilu_mask = field(instruction, 3, 16, 4);
		unsigned long output_mask = field(instruction, 3, 12, 4);
		unsigned long output_is_register = field(instruction, 3, 11, 1);
		unsigned long output_address = field(instruction, 3, 3, 8);
		unsigned long output_from_ilu = field(instruction, 3, 2, 1);
		int relative = (int)field(instruction, 3, 1, 1);
		float a[4], b[4], c[4], mac_value[4] = { 0 }, ilu_value[4] = { 0 };

		operand_value(instruction, 'A', relative, a0, constants, inputs, (const float (*)[4])temporaries, position, a);
		operand_value(instruction, 'B', relative, a0, constants, inputs, (const float (*)[4])temporaries, position, b);
		operand_value(instruction, 'C', relative, a0, constants, inputs, (const float (*)[4])temporaries, position, c);
		for (axis = 0; axis < 4; axis++)
		{
			switch (mac)
			{
			case _mac_mov: case _mac_arl: mac_value[axis] = a[axis]; break;
			case _mac_mul: mac_value[axis] = a[axis] * b[axis]; break;
			case _mac_add: mac_value[axis] = a[axis] + c[axis]; break;
			case _mac_mad: mac_value[axis] = a[axis] * b[axis] + c[axis]; break;
			case _mac_dp3: mac_value[axis] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; break;
			case _mac_dph: mac_value[axis] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + b[3]; break;
			case _mac_dp4: mac_value[axis] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; break;
			case _mac_min: mac_value[axis] = a[axis] < b[axis] ? a[axis] : b[axis]; break;
			case _mac_max: mac_value[axis] = a[axis] > b[axis] ? a[axis] : b[axis]; break;
			case _mac_slt: mac_value[axis] = a[axis] < b[axis] ? 1.0f : 0.0f; break;
			case _mac_sge: mac_value[axis] = a[axis] >= b[axis] ? 1.0f : 0.0f; break;
			default: break;
			}
		}
		if (mac == _mac_dst)
		{
			mac_value[0] = 1.0f;
			mac_value[1] = a[1] * b[1];
			mac_value[2] = a[2];
			mac_value[3] = b[3];
		}
		switch (ilu)
		{
		case _ilu_mov:
			memcpy(ilu_value, c, sizeof(ilu_value));
			break;
		case _ilu_rcp:
			ilu_value[0] = 1.0f / c[0];
			break;
		case _ilu_rcc:
			{
				float r = 1.0f / c[0];

				if (r > 0.0f)
					r = r < 5.42101e-20f ? 5.42101e-20f : r > 1.884467e+19f ? 1.884467e+19f : r;
				else
					r = r < -1.884467e+19f ? -1.884467e+19f : r > -5.42101e-20f ? -5.42101e-20f : r;
				ilu_value[0] = r;
			}
			break;
		case _ilu_rsq:
			ilu_value[0] = 1.0f / sqrtf(fabsf(c[0]));
			break;
		case _ilu_exp:
			ilu_value[0] = exp2f(floorf(c[0]));
			ilu_value[1] = c[0] - floorf(c[0]);
			ilu_value[2] = exp2f(c[0]);
			ilu_value[3] = 1.0f;
			break;
		case _ilu_log:
			{
				float x = fabsf(c[0]);

				if (x == 0.0f)
				{
					ilu_value[0] = ilu_value[2] = -1.0e30f;
					ilu_value[1] = ilu_value[3] = 1.0f;
				}
				else
				{
					float e = floorf(log2f(x));

					ilu_value[0] = e;
					ilu_value[1] = x / exp2f(e);
					ilu_value[2] = log2f(x);
					ilu_value[3] = 1.0f;
				}
			}
			break;
		case _ilu_lit:
			ilu_value[0] = 1.0f;
			ilu_value[1] = c[0] > 0.0f ? c[0] : 0.0f;
			ilu_value[2] = c[0] > 0.0f ? powf(c[1] > 0.0f ? c[1] : 0.0f,
				c[3] < -127.9961f ? -127.9961f : c[3] > 127.9961f ? 127.9961f : c[3]) : 0.0f;
			ilu_value[3] = 1.0f;
			break;
		default:
			break;
		}
		/* the scalar results fill every component, as the shaders' vec4() */
		if (ilu == _ilu_rcp || ilu == _ilu_rcc || ilu == _ilu_rsq)
			ilu_value[1] = ilu_value[2] = ilu_value[3] = ilu_value[0];

		/* results are written only after both units have read their inputs */
		if (mac == _mac_arl)
			a0 = (int)floorf(mac_value[0] + 0.001f);
		else if (mac != _mac_nop && mac_mask)
			masked_write(temporary == 12 ? position : temporary < 12 ? temporaries[temporary] : temporaries[0],
				mac_value, mac_mask);
		if (ilu != _ilu_nop && ilu_mask)
		{
			unsigned long ilu_temporary = mac != _mac_nop ? 1 : temporary;

			masked_write(ilu_temporary == 12 ? position : ilu_temporary < 12 ? temporaries[ilu_temporary] :
				temporaries[0], ilu_value, ilu_mask);
		}
		if (output_mask && (output_from_ilu ? ilu : mac) != 0 && output_is_register && output_address == 0)
			masked_write(position, output_from_ilu ? ilu_value : mac_value, output_mask);
		if (field(instruction, 3, 0, 1))
			break;
	}
}
