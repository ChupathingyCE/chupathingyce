/*
GPU_UNIFORMS.H

The uniforms the translated shaders declare, in declaration order: name, GLSL
type, array count (1: not an array), stage. The translators emit their
declarations from it (nv2a_uniform_declarations, nv2a_vsh.c), gpu.h builds
struct gpu_uniforms from it, and the GL backend (gpu_gl.c) its uniform
locations, uploads and shadows.

The order is the GLSL declaration order the shaders have always had.
*/

#ifndef __HALO_GPU_UNIFORMS_H
#define __HALO_GPU_UNIFORMS_H

enum
{
	GPU_UNIFORM_VERTEX,
	GPU_UNIFORM_PIXEL,
	/* pixel shaders whose samplers have no LOD bias of their own */
	GPU_UNIFORM_PIXEL_LOD_BIAS,
	/* c: the vertex constants, which live in struct gpu_constant_store */
	GPU_UNIFORM_VERTEX_CONSTANTS,
};

/* screen_offset: columns the menus shift by to center on a wide screen
(d3d8_device.c). ps_c0 through texture_scale: the combiner registers that live in
uniforms rather than in the program (C0/C1 of each stage and the final
combiner) and texture constants. texture_lod_bias: one component per stage. */
#define GPU_UNIFORMS(X) \
	X(c, vec4, 192, GPU_UNIFORM_VERTEX_CONSTANTS) \
	X(viewport_scale, vec4, 1, GPU_UNIFORM_VERTEX) \
	X(viewport_offset, vec4, 1, GPU_UNIFORM_VERTEX) \
	X(point_size, float, 1, GPU_UNIFORM_VERTEX) \
	X(screen_offset, float, 1, GPU_UNIFORM_VERTEX) \
	X(ps_c0, vec4, 8, GPU_UNIFORM_PIXEL) \
	X(ps_c1, vec4, 8, GPU_UNIFORM_PIXEL) \
	X(ps_final_c0, vec4, 1, GPU_UNIFORM_PIXEL) \
	X(ps_final_c1, vec4, 1, GPU_UNIFORM_PIXEL) \
	X(fog_color, vec4, 1, GPU_UNIFORM_PIXEL) \
	X(fog_parameters, vec4, 1, GPU_UNIFORM_PIXEL) \
	X(alpha_reference, float, 1, GPU_UNIFORM_PIXEL) \
	X(bump_matrix, vec4, 4, GPU_UNIFORM_PIXEL) \
	X(bump_luminance, vec4, 4, GPU_UNIFORM_PIXEL) \
	X(texture_scale, vec4, 4, GPU_UNIFORM_PIXEL) \
	X(texture_lod_bias, vec4, 1, GPU_UNIFORM_PIXEL_LOD_BIAS)

/* the fields of struct gpu_uniforms (gpu.h): every row but c, each a vec4
array (a scalar is a vec4 whose x holds it), so every field starts on a
16-byte boundary */
#define GPU_UNIFORM_FIELD_GPU_UNIFORM_VERTEX(name, count) float name[count][4];
#define GPU_UNIFORM_FIELD_GPU_UNIFORM_PIXEL(name, count) float name[count][4];
#define GPU_UNIFORM_FIELD_GPU_UNIFORM_PIXEL_LOD_BIAS(name, count) float name[count][4];
#define GPU_UNIFORM_FIELD_GPU_UNIFORM_VERTEX_CONSTANTS(name, count)
#define GPU_UNIFORM_FIELD(name, glsl_type, count, stage) GPU_UNIFORM_FIELD_##stage(name, count)

/* the GL backend's uniform locations, uploads and shadows (gpu_gl.c): every
row but c, in table order */
#define GPU_UNIFORM_IF_NOT_CONSTANTS_GPU_UNIFORM_VERTEX(x) x
#define GPU_UNIFORM_IF_NOT_CONSTANTS_GPU_UNIFORM_PIXEL(x) x
#define GPU_UNIFORM_IF_NOT_CONSTANTS_GPU_UNIFORM_PIXEL_LOD_BIAS(x) x
#define GPU_UNIFORM_IF_NOT_CONSTANTS_GPU_UNIFORM_VERTEX_CONSTANTS(x)

#endif
