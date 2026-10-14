/*
XGPU.H

Internals shared by the Direct3D front end of the renderer: the NV2A shader
translators (nv2a_vsh.c, nv2a_psh.c), texture decoding (xbox_textures.c),
guest memory write tracking (memory_watch.c) and the device itself
(d3d8_device.c).
*/

#ifndef __HALO_LINUX_XGPU_H
#define __HALO_LINUX_XGPU_H

#include "platform.h"
#include "gpu.h"

/* what the GPU backend can do (gpu_initialize, d3d8_device.c) */
extern struct gpu_capabilities device_capabilities;

/* ---------- generated source text */

struct xgpu_text
{
	char *buffer;
	unsigned long length;
	unsigned long capacity;
};

void xgpu_text_append(struct xgpu_text *text, const char *format, ...) __attribute__((format(printf, 2, 3)));

/* ---------- shader dialects

What the translators (nv2a_vsh.c, nv2a_psh.c) emit, filled from the
context's capabilities (d3d8_device.c): GLSL, or with msl the Metal Shading
Language (nv2a_msl.c). */

struct nv2a_dialect
{
	/* OpenGL ES: precision statements and an "es" #version */
	unsigned char es;
	/* the #version number: 450, 300 or 310; GPU_SHADER_LANGUAGE_MSL (which
	is never 0) with msl */
	unsigned short version;
	/* Metal Shading Language: the GLSL dialect's shader bodies in nv2a_msl.c's
	frame */
	unsigned char msl;
	/* emulate glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE): rows from the top
	(which also flips the winding, d3d8_device.c), depth from 0..1 to -1..1 */
	unsigned char clip_y_flip;
	unsigned char clip_z_remap;
	/* keep the clip-space position the screen-space conversion divides, and
	undo the conversion without dividing (the mobile precision workaround,
	nv2a_vsh.c) */
	unsigned char clip_capture;
	/* samplers have no LOD bias of their own: pass texture_lod_bias to each
	lookup */
	unsigned char shader_lod_bias;
	/* debug.gpu_debug_expression, _texture0 and _flat (port_config.c) */
	const char *debug_expression;
	unsigned char debug_texture0;
	unsigned char debug_flat;
};

/* the uniform declarations of one stage (GPU_UNIFORM_VERTEX or
GPU_UNIFORM_PIXEL, gpu_uniforms.h) for a dialect */
void nv2a_uniform_declarations(struct xgpu_text *text, const struct nv2a_dialect *dialect, int stage);

/* ---------- vertex shaders */

#define XGPU_VERTEX_ATTRIBUTE_COUNT 16
#define XGPU_VERTEX_CONSTANT_COUNT 192
/* D3D constant register -96 is hardware register 0 */
#define XGPU_VERTEX_CONSTANT_BIAS 96
_Static_assert(GPU_STAGE_COUNT == D3DTSS_MAXSTAGES && GPU_ATTRIBUTE_COUNT == XGPU_VERTEX_ATTRIBUTE_COUNT &&
	GPU_CONSTANT_COUNT == XGPU_VERTEX_CONSTANT_COUNT, "gpu.h's counts disagree with the front end's");

/* GLSL for an NV2A vertex program (the instruction words after the program
header). Attributes whose bit is set in packed_attribute_mask are fed as
NORMPACKED3 32-bit integers and unpacked in the shader. Returns a malloc'd
string. */
char *nv2a_vertex_shader_translate(const struct nv2a_dialect *dialect, const DWORD *instructions,
	unsigned long instruction_count, unsigned long packed_attribute_mask);
/* The same program run on the CPU for one vertex, for its position alone
(oPos: the screen position the program ends with, before the translated
shader undoes the screen-space conversion). inputs are the vertex's
attributes as float4s (v0 to v15), constants the c[] registers
(XGPU_VERTEX_CONSTANT_COUNT of them, biased as the shaders read them).
d3d8_device.c measures the HUD's draws with it (stereo's HUD groups) */
void nv2a_vertex_program_position(const DWORD *instructions, unsigned long instruction_count,
	const float (*constants)[4], const float (*inputs)[4], float position[4]);

/* ---------- pixel shaders */

enum
{
	_xgpu_sampler_none = 0,
	_xgpu_sampler_2d,
	_xgpu_sampler_3d,
	_xgpu_sampler_cube,
	/* a 2D screen-sized target of a foveated eye (gpu_texture_description's
	foveated_eye): its texels are in the eye's rate map's physical layout, so
	the lookup maps its screen coordinates through the map (nv2a_msl.c; Metal
	only) */
	_xgpu_sampler_2d_foveated,
};

/* everything a translated pixel shader depends on; the GLSL program cache
is keyed by these bytes */
struct nv2a_pixel_shader_key
{
	DWORD combiner_state[D3DRS_PS_MAX];
	/* D3DRS_PSTEXTUREMODES lies past D3DRS_PS_MAX */
	DWORD texture_modes;
	unsigned char sampler_type[4];
	unsigned char alpha_kill[4];
	/* D3DTSS_COLORSIGN: channels (bit 0 alpha ... bit 3 blue, as
	D3DTSIGN_*) that hold signed data in an unsigned texture format */
	unsigned char color_sign[4];
	/* D3DCMP_* function for the alpha test, or 0 when disabled */
	unsigned long alpha_test_function;
	unsigned char fog_enable;
	unsigned char fog_table_mode;
	/* inside a visibility test: count the samples that pass (iOS) */
	unsigned char count_samples;
	/* a high-res HUD meter (hud_hires.h) drawn with the meter's blend (the
	destination kept by the source's alpha): that alpha is eased to 1 by the
	coverage texture 0's green holds, so that the meter darkens what is
	behind it only where it covers it (the Xbox's point-sampled meters stop
	at their texels' edges; filtered ones have a fringe of faint texels).
	XGPU_COVERAGE_POINT_THRESHOLD also set: the meter's discrete thresholds
	in texture 0's red are read at level zero, without filtering, while
	coverage and brightness still use the filtered lookup. (A flag here rather
	than a field of its own keeps the key's size, so recorded shader lists
	stay valid.) */
	unsigned char coverage_alpha;
};
#define XGPU_COVERAGE_ALPHA 1
#define XGPU_COVERAGE_POINT_THRESHOLD 2

char *nv2a_pixel_shader_translate(const struct nv2a_dialect *dialect, const struct nv2a_pixel_shader_key *key);

/* the Metal Shading Language frame around the shader bodies (nv2a_msl.c),
in the order the translators emit it: the prelude (stage GPU_UNIFORM_VERTEX
or GPU_UNIFORM_PIXEL); the vertex shader's outputs, its entry point's
opening and its return; the pixel shader's inputs and its entry point's
opening */
void nv2a_msl_prelude(struct xgpu_text *text, int stage);
void nv2a_msl_vertex_outputs(struct xgpu_text *text);
void nv2a_msl_vertex_main(struct xgpu_text *text, const struct nv2a_dialect *dialect,
	unsigned long packed_attribute_mask);
void nv2a_msl_vertex_return(struct xgpu_text *text);
void nv2a_msl_fragment_inputs(struct xgpu_text *text);
void nv2a_msl_fragment_main(struct xgpu_text *text, const struct nv2a_dialect *dialect,
	const struct nv2a_pixel_shader_key *key);

/* ---------- textures */

struct xgpu_texture_description
{
	DWORD format;       /* D3DFMT_* */
	unsigned long width, height, depth, levels;
	BOOL cube_map;
	BOOL linear;        /* not swizzled; addressed with texel coordinates */
	BOOL compressed;
	unsigned long pitch; /* linear textures */
	BOOL hires;         /* a high-res HUD texture drawn in the texture's place (hud_hires.h) */
	BOOL hires_coverage; /* ... whose green is its coverage (a meter's) */
	BOOL hires_point_threshold; /* ... whose red holds discrete segment data */
};

void xgpu_texture_describe(DWORD format_word, DWORD size_word, struct xgpu_texture_description *description);
/* bytes of one face, mip levels included (cube faces are padded) */
unsigned long xgpu_texture_face_size(const struct xgpu_texture_description *description);
unsigned long xgpu_texture_level_offset(const struct xgpu_texture_description *description, unsigned long level);
unsigned long xgpu_texture_level_pitch(const struct xgpu_texture_description *description, unsigned long level);

/* the texture for an Xbox texture header, uploading or refreshing it from
guest memory as needed; *type receives a GPU_TEXTURE_* */
gpu_texture xgpu_texture_get(const DWORD *resource, const D3DCOLOR *palette, uint32_t *type,
	struct xgpu_texture_description *description);
void xgpu_texture_cache_begin_frame(void);
/* a small 2D texture's mean alpha (0 to 1), decoded from guest memory: 1 and
*alpha set for one of at most 4096 texels (palettized ones excepted), else 0 */
int xgpu_texture_mean_alpha(const DWORD *resource, float *alpha);

/* ---------- render targets */

struct xgpu_render_target
{
	unsigned long data;  /* physical address */
	unsigned long width, height;
	BOOL depth;
	gpu_texture texture;
	/* pixels per unit of width and height: more than 1 for the screen's
	targets when the game draws at the display's resolution (d3d8_device.c) */
	float scale[2];
	/* the logical size: width and height times scale */
	unsigned long gl_width, gl_height;
	/* changes whenever the target is drawn into or cleared (d3d8_device.c,
	bind_targets) */
	unsigned long written;
	/* a foveated eye's target (gpu_texture_description.foveated_eye): the
	eye, 1 or 2, and the texture's allocated size, of which the eye's rate
	map fills the top left; gl_width by gl_height is its screen size. 0 for
	any other target, allocated at gl_width by gl_height */
	unsigned char foveated_eye;
	unsigned long allocated_width, allocated_height;
};

/* the GL texture holding a render target with this physical address, or 0 */
struct xgpu_render_target *xgpu_render_target_find(unsigned long data);

#endif
