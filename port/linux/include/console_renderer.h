/*
CONSOLE_RENDERER.H

A console's own renderer under the Xbox Direct3D 8 device
(HALO_CONSOLE_NATIVE_RENDERER): port/linux/src/d3d8_console.inl reads the
device's state as the OpenGL path does and hands each draw, clear and
present to the console through these calls, which a port defines on its
graphics API (the Xbox 360's on its Direct3D 9: warthog-360's
port/xbox360/src/d3d8_x360.c). There is no OpenGL in between, and the
OpenGL path stays the fallback (display.renderer = "gl").

Only plain C types cross this line: the console's graphics headers and the
Xbox's Direct3D 8 declarations the game sees have types of the same names.
The enumerations are this interface's own (the Xbox's Direct3D 8 spells its
values as OpenGL's, and each console's API has its own), so a console can
put another graphics layer under the same calls.

What the device side gives the console:
- textures as 32-bit ARGB texels (words in the processor's order) or DXT
  blocks (as the maps have them), each
  texture made once and given its levels; the console keeps them in its
  GPU's own layout (tiled), with no copy of its own;
- render targets by size, which the console keeps in its tile memory and
  copies out (resolves) when a draw samples one;
- shaders as GLSL text from the translators (nv2a_vsh.c, nv2a_psh.c), made
  once each and kept by the console (compiled and cached on its drive);
- each draw's state, constants, streams and indices. Vertex streams and
  index data in the Xbox window are read where they are; the device side
  copies only what the game rewrites (dynamic data) or what the GPU cannot
  read in place.
*/

#ifndef __CONSOLE_RENDERER_H
#define __CONSOLE_RENDERER_H

/* ---------- start-up */

/* the console's device made; nonzero if it can draw */
int console_renderer_initialize(void);

/* ---------- textures */

enum
{
	_console_texture_2d,
	_console_texture_cube,
	_console_texture_volume,
};

enum
{
	_console_format_argb8,
	_console_format_dxt1,
	_console_format_dxt3,
	_console_format_dxt5,
};

struct console_texture;

/* a texture of this kind, format and size with `levels` mip levels; NULL if
there is no memory for it */
struct console_texture *console_texture_create(int kind, int format, unsigned long width, unsigned long height,
	unsigned long depth, unsigned long levels);
/* a level of a face (0 but for cube maps): rows of texels (ARGB words in
the processor's order) or of DXT blocks, packed, slices one after another
for a volume */
void console_texture_level(struct console_texture *texture, unsigned long face, unsigned long level, const void *data);
/* freed once the GPU is done with it */
void console_texture_delete(struct console_texture *texture);

/* ---------- render targets */

struct console_target;

/* a color (depth 0) or depth and stencil target of this many pixels */
struct console_target *console_target_create(unsigned long width, unsigned long height, int depth);
/* a color target's pixels as a texture to sample, copied out of the tile
memory first if it has been drawn into since */
struct console_texture *console_target_texture(struct console_target *target);
/* the targets the draws and clears that follow go to (either may be NULL,
not both) */
void console_targets_set(struct console_target *color, struct console_target *depth);

/* ---------- shaders */

struct console_shader;

/* a shader from the translators' GLSL (vertex nonzero for a vertex
shader): made once, kept by the console; NULL if it does not compile */
struct console_shader *console_shader_create(int vertex, const char *glsl);

/* ---------- uniforms

Every shader's uniforms are at the same registers, so the device side sets
each only when it changes, whatever shader draws. */

enum
{
	/* the vertex shader's: c[192], then the translators' own */
	_console_uniform_constants,
	_console_uniform_viewport_scale,
	_console_uniform_viewport_offset,
	_console_uniform_point_size,
	_console_uniform_screen_offset,
	NUMBER_OF_CONSOLE_VERTEX_UNIFORMS,

	/* the pixel shader's */
	_console_uniform_ps_c0 = NUMBER_OF_CONSOLE_VERTEX_UNIFORMS,
	_console_uniform_ps_c1,
	_console_uniform_ps_final_c0,
	_console_uniform_ps_final_c1,
	_console_uniform_fog_color,
	_console_uniform_fog_parameters,
	_console_uniform_alpha_reference,
	_console_uniform_bump_matrix,
	_console_uniform_bump_luminance,
	_console_uniform_texture_scale,
	_console_uniform_texture_lod_bias,
	_console_uniform_model_lights,
	NUMBER_OF_CONSOLE_UNIFORMS
};

/* vectors of four floats from `first` of a uniform (its registers in
order) */
void console_uniform_set(int uniform, unsigned long first, const float *vectors, unsigned long count);

/* ---------- state */

/* compare functions: never, less, equal, less or equal, greater, not equal,
greater or equal, always (0 to 7) */

enum
{
	_console_blend_zero,
	_console_blend_one,
	_console_blend_source_color,
	_console_blend_inverse_source_color,
	_console_blend_source_alpha,
	_console_blend_inverse_source_alpha,
	_console_blend_destination_alpha,
	_console_blend_inverse_destination_alpha,
	_console_blend_destination_color,
	_console_blend_inverse_destination_color,
	_console_blend_source_alpha_saturate,
	_console_blend_constant_color,
	_console_blend_inverse_constant_color,
	_console_blend_constant_alpha,
	_console_blend_inverse_constant_alpha,
};

/* blend operations: add, subtract, reverse subtract, minimum, maximum (0 to
4); stencil operations: keep, zero, replace, increment and decrement
(saturating), invert, increment and decrement (wrapping) (0 to 7) */

enum
{
	_console_cull_none = 1,
	_console_cull_cw,
	_console_cull_ccw,
};

struct console_state
{
	unsigned char blend, depth_test, depth_write, stencil;
	/* the enumerations above */
	unsigned char blend_source, blend_destination, blend_operation;
	unsigned char depth_function;
	unsigned char stencil_function, stencil_fail, stencil_depth_fail, stencil_pass;
	unsigned char stencil_reference, stencil_mask, stencil_write_mask;
	unsigned char cull;
	/* bits: 1 red, 2 green, 4 blue, 8 alpha */
	unsigned char color_write;
	unsigned char wireframe;
	/* ARGB */
	unsigned long blend_color;
	/* in the depth buffer's units of 2^-24, and per unit of slope */
	float depth_bias, slope_bias;
	/* in the targets' pixels, and the depth range */
	unsigned long viewport[4];
	float depth_near, depth_far;
};

/* ---------- samplers */

enum
{
	_console_address_wrap = 1,
	_console_address_mirror,
	_console_address_clamp,
	_console_address_border,
};

struct console_sampler
{
	struct console_texture *texture;
	unsigned char address[3];
	/* filters: 0 point, 1 linear, 2 anisotropic (minification only); mip
	filter 0 none, 1 point, 2 linear */
	unsigned char magnify, minify, mip;
	unsigned char anisotropy;
	/* ARGB */
	unsigned long border;
	float lod_bias;
	unsigned long top_level;
};

/* ---------- draws */

enum
{
	_console_primitive_points = 1,
	_console_primitive_lines,
	_console_primitive_line_strip,
	_console_primitive_triangles,
	_console_primitive_triangle_fan,
	_console_primitive_triangle_strip,
	_console_primitive_quads,
};

/* an input register's format: the OpenGL path's (size, type, normalized)
in one */
enum
{
	_console_vertex_float1 = 1,
	_console_vertex_float2,
	_console_vertex_float3,
	_console_vertex_float4,
	_console_vertex_color,       /* D3DCOLOR: B, G, R, A bytes */
	_console_vertex_short2,
	_console_vertex_short4,
	_console_vertex_short2n,
	_console_vertex_short4n,
	_console_vertex_ubyte4n,
	_console_vertex_normpacked3, /* the Xbox's 11:11:10 normal */
};

#define CONSOLE_VERTEX_STREAMS 16
#define CONSOLE_VERTEX_REGISTERS 16

struct console_stream
{
	/* where the GPU reads the stream's vertex 0 (the draw's first), and its
	stride; little_endian for the map's own vertices, as its cache file has
	them, else the processor's order */
	const void *data;
	unsigned long size;
	unsigned long stride;
	unsigned char little_endian;
};

struct console_element
{
	unsigned char reg, stream, format;
	unsigned short offset;
};

struct console_draw
{
	struct console_shader *vertex_shader, *pixel_shader;
	const struct console_state *state;
	const struct console_sampler *samplers;
	unsigned long sampler_count;
	/* the streams and the registers they feed; the vertex shader's other
	registers hold constant values (for every vertex) */
	const struct console_stream *streams;
	unsigned long stream_count;
	const struct console_element *elements;
	unsigned long element_count;
	const float (*constant_values)[4];
	unsigned short constant_registers;
	/* the stable identity of the elements' layout (the same value, the same
	elements, formats and strides), so the console finds what it made for
	it without comparing them */
	const void *layout_key;
	int primitive;
	/* vertices from 0 (from each stream's data), or indices into them: 16
	bits, the processor's order */
	unsigned long vertex_count;
	/* the lowest vertex the draw reads (its smallest index; 0 for a draw
	without indices) */
	unsigned long vertex_first;
	const unsigned short *indices;
	unsigned long index_count;
	/* the indices are the game's to keep while the GPU reads them (in the
	Xbox window, not rewritten): read where they are */
	unsigned char indices_resident;
	/* inside a visibility test (counted by its query) */
	unsigned char counted;
};

void console_draw(const struct console_draw *draw);

/* the viewport's (state.viewport) or the rectangles' pixels of the targets,
cleared: flags 1 color, 2 depth, 4 stencil */
void console_clear(unsigned long flags, const unsigned long *rectangles, unsigned long rectangle_count,
	unsigned long color, float depth, unsigned long stencil);

/* ---------- visibility tests (lens flares, The Library's lights) */

/* a test's draws begin, and end with the slot its count goes to; counts
come a frame or more later and are never waited for */
void console_query_begin(void);
void console_query_end(unsigned long slot);
/* nonzero, with its count of samples, once the slot's latest finished test
is known (each count is given once); zero, leaving *samples, otherwise */
int console_query_result(unsigned long slot, unsigned long *samples);

/* ---------- the frame */

/* the color target shown, scaled to the screen; the frame ends */
void console_present(struct console_target *picture);

/* size bytes (a multiple of 4) copied for this frame's draws, from data,
or left for the caller to fill when data is NULL: memory the GPU reads,
valid until the frame two on. NULL if the frame's space is used up */
void *console_frame_copy(const void *data, unsigned long size);

/* the CPU's writes to [data, data + size) made visible to the GPU (written
back from its cache), for memory the GPU reads in place */
void console_memory_written(const void *data, unsigned long size);

#endif
