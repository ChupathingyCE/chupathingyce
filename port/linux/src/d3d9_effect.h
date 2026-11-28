/*
D3D9_EFFECT.H

Direct3D 9 effects (compiled "fx_2_0" binaries, as fxc and D3DX make them)
read and turned into GLSL, for OpenSauce's post-processing shaders
(port/linux/game/opensauce_postprocess.c), which keep them in their tags
compiled: the effect's parameters, its techniques and passes with each
pass's vertex and pixel shader, the samplers' states, and each shader's
constant table and preshader. A shader's tokens (vs_1_1 to vs_3_0, ps_2_0 to
ps_3_0) become a GLSL 3.30 stage; the preshader (the part of the shader the
compiler moved onto the processor: it computes constants from parameters)
is run here as the effect would run it before each pass.

The formats are Microsoft's: the shader tokens as the Direct3D 9 driver
documentation describes them (D3DSIO_*, D3DSPR_*), the effect container and
the preshader's instructions as they are laid out in the binaries, read as
facts. The code is this game's. Nothing here calls OpenGL: the caller
compiles the text and sets the uniforms (opensauce_postprocess_gl.c).
*/

#ifndef D3D9_EFFECT_H
#define D3D9_EFFECT_H

#include <stddef.h>
#include <stdint.h>

/* ---------- constants */

enum
{
	D3D9_EFFECT_NAME_SIZE = 64,
	/* a shader's registers the translation keeps: Direct3D 9's own limits */
	D3D9_SHADER_FLOAT_REGISTERS = 256,
	D3D9_SHADER_INT_REGISTERS = 16,
	D3D9_SHADER_BOOL_REGISTERS = 16,
	D3D9_SHADER_SAMPLERS = 16,

	/* a parameter's type (D3DXPARAMETER_TYPE) and class (D3DXPARAMETER_CLASS) */
	_d3d9_type_void = 0,
	_d3d9_type_bool,
	_d3d9_type_int,
	_d3d9_type_float,
	_d3d9_type_string,
	_d3d9_type_texture,
	_d3d9_type_texture1d,
	_d3d9_type_texture2d,
	_d3d9_type_texture3d,
	_d3d9_type_texturecube,
	_d3d9_type_sampler,
	_d3d9_type_sampler1d,
	_d3d9_type_sampler2d,
	_d3d9_type_sampler3d,
	_d3d9_type_samplercube,
	_d3d9_type_pixelshader,
	_d3d9_type_vertexshader,

	_d3d9_class_scalar = 0,
	_d3d9_class_vector,
	_d3d9_class_matrix_rows,
	_d3d9_class_matrix_columns,
	_d3d9_class_object,
	_d3d9_class_struct,

	/* a constant's register set (D3DXREGISTER_SET) */
	_d3d9_regset_bool = 0,
	_d3d9_regset_int4,
	_d3d9_regset_float4,
	_d3d9_regset_sampler,

	/* a sampler's states the effect sets (D3DSAMPLERSTATETYPE) */
	_d3d9_sampler_address_u = 1,
	_d3d9_sampler_address_v = 2,
	_d3d9_sampler_address_w = 3,
	_d3d9_sampler_border_color = 4,
	_d3d9_sampler_mag_filter = 5,
	_d3d9_sampler_min_filter = 6,
	_d3d9_sampler_mip_filter = 7,
	_d3d9_sampler_srgb = 11,
	NUMBER_OF_D3D9_SAMPLER_STATES = 14,
	D3D9_SAMPLER_STATE_UNSET = 0xFFFFFFFFu,
};

/* ---------- structures */

/* a constant of a shader's table (CTAB), or of its preshader's */
struct d3d9_constant
{
	char name[D3D9_EFFECT_NAME_SIZE];
	unsigned short regset, reg, count;
	unsigned short klass, type, rows, columns, elements;
};

/* a preshader: its inputs (in its own registers), literals and code */
struct d3d9_preshader
{
	struct d3d9_constant *inputs;
	unsigned long input_count;
	double *literals;
	unsigned long literal_count;
	const uint32_t *code;
	unsigned long code_size;
	/* registers its temporaries and inputs take, in components */
	unsigned long temporary_components, input_components;
};

struct d3d9_shader
{
	/* the tokens, from the version token to the end token */
	const uint32_t *tokens;
	unsigned long token_count;
	int pixel;
	int major, minor;
	struct d3d9_constant *constants;
	unsigned long constant_count;
	struct d3d9_preshader preshader;
	int has_preshader;
};

struct d3d9_parameter
{
	char name[D3D9_EFFECT_NAME_SIZE];
	char semantic[D3D9_EFFECT_NAME_SIZE];
	unsigned long type, klass, rows, columns, elements;
	/* its default value, in 32-bit words (floats, ints or bools) */
	const uint32_t *defaults;
	unsigned long default_count;
	/* a sampler's: the texture parameter its Texture state names (-1 for
	none) and its states (D3D9_SAMPLER_STATE_UNSET where it sets none) */
	long texture;
	unsigned long sampler_states[NUMBER_OF_D3D9_SAMPLER_STATES];
};

struct d3d9_pass
{
	char name[D3D9_EFFECT_NAME_SIZE];
	/* the shaders' indices (-1 for none) */
	long vertex_shader, pixel_shader;
};

struct d3d9_technique
{
	char name[D3D9_EFFECT_NAME_SIZE];
	struct d3d9_pass *passes;
	unsigned long pass_count;
};

struct d3d9_effect
{
	struct d3d9_parameter *parameters;
	unsigned long parameter_count;
	struct d3d9_technique *techniques;
	unsigned long technique_count;
	struct d3d9_shader *shaders;
	unsigned long shader_count;
};

/* what a translation needs to know of the other stage */
struct d3d9_glsl_varyings
{
	/* the varyings' names ("TEXCOORD0", "COLOR1"), as many as count */
	char names[16][24];
	unsigned long count;
};

/* ---------- prototypes */

/* an effect read from its binary (kept by the caller while the effect is
used: the shaders point into it); 0 if it is not one this reads, with why
in error */
int d3d9_effect_read(const void *data, unsigned long size, struct d3d9_effect *effect, char *error,
	size_t error_size);
void d3d9_effect_dispose(struct d3d9_effect *effect);

/* a parameter's index by name or semantic (case not minded for the
semantic, as D3DX doesn't), -1 if it has none */
long d3d9_effect_parameter_find(const struct d3d9_effect *effect, const char *name);
long d3d9_effect_parameter_by_semantic(const struct d3d9_effect *effect, const char *semantic);
long d3d9_effect_technique_find(const struct d3d9_effect *effect, const char *name);

/* GLSL for a shader, without the version line: the float constants are
"uniform vec4 vc[n]" (n in *float_count; "pc" in a pixel shader), the
integer ones "uniform vec4 vci[16]" ("pci", whole numbers as floats), the bools "uniform int
vcb[16]" ("pcb"), the samplers "s0".."s15" (the
kinds used in sampler_kinds: 2 2D, 3 cube, 4 volume). A vertex shader takes
its attributes at locations 0 (POSITION) and 1 to 8 (TEXCOORD0 to 7), and
writes every varying in want (a pixel shader's inputs) as well as its own;
it adds "uniform vec4 d3d9_pixel_fix" (the half pixel Direct3D 9's
rasterizer puts its pixel centers by: 1 / width, 1 / height, and 1 or -1
as y is turned over). A pixel shader lists its inputs in inputs. NULL if
it does not translate, with why in error. The caller frees it. */
char *d3d9_shader_glsl(const struct d3d9_shader *shader, const struct d3d9_glsl_varyings *want,
	struct d3d9_glsl_varyings *inputs, unsigned long *float_count, unsigned char sampler_kinds[D3D9_SHADER_SAMPLERS],
	char *error, size_t error_size);

/* a preshader run: inputs are its input registers (input_components
floats, as its constants put them), outputs the shader's float constants
(D3D9_SHADER_FLOAT_REGISTERS * 4) it writes some of; 0 if it has an
instruction this doesn't run */
int d3d9_preshader_run(const struct d3d9_preshader *preshader, const float *inputs, float *outputs);

#endif
