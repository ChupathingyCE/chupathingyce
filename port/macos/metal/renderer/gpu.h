/*
GPU.H

The interface between the Direct3D front end (d3d8_device.c, xbox_textures.c)
and a GPU backend (gpu_gl.c; Metal in Phase 1). It is filled in one
sub-step at a time (renderer split design, step 3).

It compiles on the 64-bit iOS host as well as in the 32-bit guest, so it
includes only <stdint.h>, gpu_uniforms.h (which includes nothing) and
halo_stereo.h (which includes only <stdint.h>), and its structs use
fixed-width fields alone: the two ABIs lay them out the same way.
*/

#ifndef __HALO_GPU_H
#define __HALO_GPU_H

#include <stdint.h>
#include "gpu_uniforms.h"
#include "halo_stereo.h"

/* 0 is none */
typedef uint32_t gpu_texture, gpu_buffer, gpu_shader;

/* how visibility (occlusion) tests count samples */
enum
{
	/* the number of samples that passed */
	GPU_OCCLUSION_EXACT,
	/* only whether any passed (OpenGL ES occlusion queries) */
	GPU_OCCLUSION_ANY_SAMPLE,
	/* the pixel shader counts them (count_samples in the pixel shader key) */
	GPU_OCCLUSION_SHADER_COUNTER,
};

/* gpu_capabilities.shader_language: Metal Shading Language (the Metal
backend), which no GLSL version number can be */
enum { GPU_SHADER_LANGUAGE_MSL = 1 };

struct gpu_capabilities
{
	/* D3DCOLOR vertex attributes can be read as BGRA; otherwise the front
	end swizzles them while streaming */
	uint8_t vertex_bgra;
	/* indexed draws take a base vertex; otherwise indices are rebased */
	uint8_t base_vertex;
	uint8_t triangle_fans;
	uint8_t line_loops;
	/* samplers apply a LOD bias; otherwise the shader does */
	uint8_t sampler_lod_bias;
	/* GPU_OCCLUSION_* */
	uint8_t occlusion_mode;
	/* BC1-3 textures; otherwise the front end decodes DXT */
	uint8_t s3tc;
	/* BORDER addressing; otherwise it becomes CLAMP_TO_EDGE */
	uint8_t border_clamp;
	/* the shading language version: 450, 300 or 310; or
	GPU_SHADER_LANGUAGE_MSL */
	uint16_t shader_language;
	/* OpenGL ES shading language */
	uint8_t shader_es;
	/* clip space is emulated in the vertex shader: rows from the top, and
	depth from 0..1 to -1..1 */
	uint8_t clip_y_flip;
	uint8_t clip_z_remap;
	uint8_t pad[3];
	uint32_t max_texture_size;
};

/* ---------- textures, render targets included */

/* gpu_texture_description.type */
enum { GPU_TEXTURE_2D = 1, GPU_TEXTURE_3D, GPU_TEXTURE_CUBE };

/* gpu_texture_description.format: BGRA8 texels are 32-bit ARGB words in
memory; BC1-3 are DXT1, 3 and 5 */
enum
{
	GPU_FORMAT_BGRA8 = 1,
	GPU_FORMAT_BC1,
	GPU_FORMAT_BC2,
	GPU_FORMAT_BC3,
	GPU_FORMAT_DEPTH_STENCIL,
};

/* gpu_texture_description.usage */
enum
{
	/* filled by gpu_texture_upload */
	GPU_USAGE_UPLOAD = 1,
	/* drawn into, or filled from render targets (mip composites) */
	GPU_USAGE_RENDER_TARGET,
};

struct gpu_texture_description
{
	uint8_t type;
	uint8_t format;
	uint8_t usage;
	/* 0 for an ordinary texture; 1 or 2 for a screen-sized target of the
	left or right eye in a foveated frame (a render target allocated at the
	Compositor's drawable size per view, which the eye's rate map fills only
	in part): the backend binds that eye's rate map on any pass that writes
	it, and its viewports and scissors are in the map's screen coordinates
	(Task 10, d3d8_device.c). With debug.rate_map_test on the Mac, 1 marks
	the mono screen's targets, which a synthetic map fills */
	uint8_t foveated_eye;
	uint32_t width, height, depth, levels;
};

gpu_texture gpu_texture_create(const struct gpu_texture_description *description);
/* one face (0 unless a cube) and level; a refresh uploads every face and
level in order, starting from face 0, level 0 */
void gpu_texture_upload(gpu_texture texture, uint32_t face, uint32_t level, const void *data, uint32_t size);
/* level 0 of source into level of destination (mip composites) */
void gpu_texture_copy_level(gpu_texture source, gpu_texture destination, uint32_t level);
/* the levels after base_level from base_level */
void gpu_texture_generate_mipmaps(gpu_texture texture, uint32_t base_level);
void gpu_texture_destroy(gpu_texture texture);
/* level 0 of a 2D color texture as BGRA8 rows from the top; of a 2D
depth-stencil render target, its depth as one float per texel, rows from the
top, as the game sees it (0 at the near plane, 1 at the far plane and where
nothing drew), for debugging. Returns 0 and writes nothing if size is short of
width * height * 4 or the backend cannot read the texture (ES: block-compressed
textures and depth) */
uint32_t gpu_texture_read(gpu_texture texture, void *pixels, uint32_t size);

/* ---------- buffers: the vertex mirror's segments */

/* gpu_buffer_write flags: no queued draw reads the range (pages uploaded for
the first time), so the backend needn't wait for the GPU */
enum { GPU_WRITE_UNUSED = 1 };

gpu_buffer gpu_buffer_create(uint32_t size);
void gpu_buffer_write(gpu_buffer buffer, uint32_t offset, uint32_t size, const void *data, uint32_t flags);

/* ---------- per-frame transient data */

/* gpu_stream kinds */
enum { GPU_STREAM_KIND_VERTEX = 1, GPU_STREAM_KIND_INDEX };

/* room for what one draw streams, before its first gpu_stream: starting a
new buffer between two of a draw's streams would leave the earlier ones
pointing at discarded storage */
void gpu_stream_reserve(uint32_t vertex_bytes, uint32_t index_bytes);
/* copies data into the frame's stream or index buffer; returns its offset
and sets *buffer */
uint32_t gpu_stream(uint32_t kind, const void *data, uint32_t size, gpu_buffer *buffer);

/* ---------- shaders */

enum { GPU_SHADER_VERTEX = 1, GPU_SHADER_PIXEL };

/* source is in the dialect gpu_capabilities.shader_language names; 0 if it
doesn't compile */
gpu_shader gpu_shader_create(uint32_t stage, const char *source);

/* ---------- vertex constants

The front end's store of the 192 vertex constant registers. Each register's
serial is the value serial took when it last changed, and log holds the
index of the register that changed at each serial, modulo its size: a
backend that saw serial s can upload just what changed since. */

enum { GPU_CONSTANT_COUNT = 192, GPU_CONSTANT_LOG_SIZE = 1024 };
/* texture stages and vertex attributes (D3DTSS_MAXSTAGES, XGPU_VERTEX_ATTRIBUTE_COUNT) */
enum { GPU_STAGE_COUNT = 4, GPU_ATTRIBUTE_COUNT = 16 };

struct gpu_constant_store
{
	float c[GPU_CONSTANT_COUNT][4];
	/* 64-bit, so they never wrap: a skinned model's draw changes up to 132
	registers, and 32 bits wrapped within minutes to an hour at a high frame
	rate, after which a program found none of its registers changed and drew
	with another object's node matrices (upstream's b449c43e) */
	uint64_t serials[GPU_CONSTANT_COUNT];
	uint64_t serial;
	uint8_t log[GPU_CONSTANT_LOG_SIZE];
};

/* ---------- uniforms (gpu_uniforms.h): what the shaders read besides the
vertex constants, and serial, which changes whenever any of them does */

struct gpu_uniforms
{
	GPU_UNIFORMS(GPU_UNIFORM_FIELD)
	uint32_t serial;
	uint32_t pad[3];
};

/* ---------- draw state (the spec's draw packet, filled in sub-step e) */

/* compare functions: depth and stencil tests */
enum
{
	GPU_COMPARE_NEVER, GPU_COMPARE_LESS, GPU_COMPARE_EQUAL, GPU_COMPARE_LESS_EQUAL,
	GPU_COMPARE_GREATER, GPU_COMPARE_NOT_EQUAL, GPU_COMPARE_GREATER_EQUAL, GPU_COMPARE_ALWAYS,
};

enum
{
	GPU_STENCIL_KEEP, GPU_STENCIL_ZERO, GPU_STENCIL_REPLACE, GPU_STENCIL_INCREMENT_CLAMP,
	GPU_STENCIL_DECREMENT_CLAMP, GPU_STENCIL_INVERT, GPU_STENCIL_INCREMENT_WRAP, GPU_STENCIL_DECREMENT_WRAP,
};

enum
{
	GPU_BLEND_ZERO, GPU_BLEND_ONE,
	GPU_BLEND_SOURCE_COLOR, GPU_BLEND_ONE_MINUS_SOURCE_COLOR,
	GPU_BLEND_SOURCE_ALPHA, GPU_BLEND_ONE_MINUS_SOURCE_ALPHA,
	GPU_BLEND_DESTINATION_ALPHA, GPU_BLEND_ONE_MINUS_DESTINATION_ALPHA,
	GPU_BLEND_DESTINATION_COLOR, GPU_BLEND_ONE_MINUS_DESTINATION_COLOR,
	GPU_BLEND_SOURCE_ALPHA_SATURATE,
	GPU_BLEND_CONSTANT_COLOR, GPU_BLEND_ONE_MINUS_CONSTANT_COLOR,
	GPU_BLEND_CONSTANT_ALPHA, GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA,
};

enum { GPU_BLEND_OP_ADD, GPU_BLEND_OP_SUBTRACT, GPU_BLEND_OP_REVERSE_SUBTRACT, GPU_BLEND_OP_MIN, GPU_BLEND_OP_MAX };

/* which faces to discard */
enum { GPU_CULL_NONE, GPU_CULL_FRONT, GPU_CULL_BACK };
enum { GPU_FRONT_CLOCKWISE, GPU_FRONT_COUNTER_CLOCKWISE };
enum { GPU_FILL_SOLID, GPU_FILL_LINE, GPU_FILL_POINT };

/* in target pixels */
struct gpu_viewport
{
	int32_t x, y, width, height;
	float min_z, max_z;
};

/* in target pixels; width or height 0: the scissor test is off */
struct gpu_rect
{
	int32_t x, y, width, height;
};

struct gpu_depth_stencil_state
{
	uint8_t depth_test;
	uint8_t depth_write;
	uint8_t depth_function;
	uint8_t stencil_test;
	uint8_t stencil_function;
	uint8_t stencil_fail, stencil_depth_fail, stencil_pass;
	uint32_t stencil_reference, stencil_read_mask, stencil_write_mask;
};

struct gpu_blend_state
{
	uint8_t enable;
	uint8_t source, destination, operation;
	/* ARGB, as D3DCOLOR */
	uint32_t color;
	/* bit 0 red, 1 green, 2 blue, 3 alpha */
	uint8_t color_write_mask;
	/* nonzero: alpha blends by its own factors (same operation), else by
	source and destination; only stereo's HUD layer sets it (d3d8_device.c,
	hud_layer_blend) */
	uint8_t alpha_separate, alpha_source, alpha_destination;
};

struct gpu_raster_state
{
	uint8_t cull_mode;
	uint8_t front_face;
	uint8_t fill_mode;
	uint8_t depth_bias_enable;
	float depth_bias_slope;
	float depth_bias_constant;
	/* nonzero: depth is clamped to the viewport's range instead of the
	primitive being clipped at the near and far planes (only head-tracked
	stereo's first-person body, halo_first_person_body_depth_clamp) */
	uint8_t depth_clamp;
	uint8_t pad[3];
};

/* texture filters (D3DTSS_MINFILTER, MAGFILTER, MIPFILTER); the GL backend
treats every one but POINT as linear, and ANISOTROPIC also enables
anisotropy. They keep D3D's distinctions so that a change between two of
them is still a change of state (the backend's sampler cache) */
enum
{
	GPU_FILTER_NONE, GPU_FILTER_POINT, GPU_FILTER_LINEAR, GPU_FILTER_ANISOTROPIC,
	GPU_FILTER_QUINCUNX, GPU_FILTER_GAUSSIAN_CUBIC,
};

/* texture addressing (D3DTSS_ADDRESSU, V, W); CLAMP and CLAMP_TO_EDGE sample
alike but stay distinct, as the filters do */
enum { GPU_ADDRESS_WRAP, GPU_ADDRESS_MIRROR, GPU_ADDRESS_CLAMP, GPU_ADDRESS_BORDER, GPU_ADDRESS_CLAMP_TO_EDGE };

struct gpu_sampler_state
{
	uint8_t min_filter, mag_filter, mip_filter;
	uint8_t address_u, address_v, address_w;
	uint8_t pad[2];
	/* the D3D values: the first level sampled, and the anisotropy an
	ANISOTROPIC min filter uses */
	uint32_t max_mip_level;
	uint32_t max_anisotropy;
	/* the D3D bias; the shader applies it without sampler_lod_bias */
	float lod_bias;
	/* ARGB, as D3DCOLOR; BORDER addressing without border_clamp becomes
	CLAMP_TO_EDGE in the backend */
	uint32_t border_color;
};

struct gpu_stage
{
	gpu_texture texture;
	/* 0: the stage is off; else GPU_TEXTURE_* */
	uint8_t type;
	uint8_t pad[3];
	struct gpu_sampler_state sampler;
};

/* vertex attribute formats: the GL backend reads UBYTE and NORMSHORT
normalized, SHORT as plain integers converted to float, and NORMPACKED3 as
one unsigned integer the shader unpacks; BGRA8 only with vertex_bgra */
enum
{
	GPU_ATTRIBUTE_FLOAT1 = 1, GPU_ATTRIBUTE_FLOAT2, GPU_ATTRIBUTE_FLOAT3, GPU_ATTRIBUTE_FLOAT4,
	GPU_ATTRIBUTE_BGRA8, GPU_ATTRIBUTE_RGBA8,
	GPU_ATTRIBUTE_SHORT1, GPU_ATTRIBUTE_SHORT2, GPU_ATTRIBUTE_SHORT3, GPU_ATTRIBUTE_SHORT4,
	GPU_ATTRIBUTE_NORMSHORT1, GPU_ATTRIBUTE_NORMSHORT2, GPU_ATTRIBUTE_NORMSHORT3, GPU_ATTRIBUTE_NORMSHORT4,
	GPU_ATTRIBUTE_UBYTE1, GPU_ATTRIBUTE_UBYTE2, GPU_ATTRIBUTE_UBYTE3, GPU_ATTRIBUTE_UBYTE4,
	GPU_ATTRIBUTE_NORMPACKED3,
};

/* an attribute's source besides streams 0-15: a constant value, or nothing */
enum { GPU_STREAM_CONSTANT = 16, GPU_STREAM_NONE = 255 };

struct gpu_vertex_stream
{
	gpu_buffer buffer;
	uint32_t offset;
	uint32_t stride;
};

struct gpu_vertex_attribute
{
	uint8_t format;
	uint8_t stream;
	/* bytes from the start of the stream's vertex */
	uint16_t offset;
};

/* primitives; TRIANGLE_FAN and LINE_LOOP only with triangle_fans and line_loops */
enum
{
	GPU_PRIMITIVE_POINTS, GPU_PRIMITIVE_LINES, GPU_PRIMITIVE_LINE_LOOP, GPU_PRIMITIVE_LINE_STRIP,
	GPU_PRIMITIVE_TRIANGLES, GPU_PRIMITIVE_TRIANGLE_STRIP, GPU_PRIMITIVE_TRIANGLE_FAN,
};

/* a pipeline as the Metal backend builds it for a draw, in terms the front
end can name across runs (shader_list.c): its shaders, blending (factors
only while it is on; alpha's are color's unless the draw blends alpha on its
own, gpu_blend_state.alpha_separate), the color write mask (0 without a
color target), the stages that rebuild their border colors, whether it has a
depth-stencil target, and each attribute's kind (0 for a constant, else its
GPU_ATTRIBUTE_* format). Fixed-width fields only: it crosses from the guest
to the host as it is. */
struct gpu_pipeline_description
{
	gpu_shader vertex_shader, pixel_shader;
	uint8_t blend, source, destination, operation;
	uint8_t write_mask, exact_borders, depth, alpha_source;
	uint8_t alpha_destination, pad[3];
	uint8_t attribute_kinds[GPU_ATTRIBUTE_COUNT];
};

/* one draw, complete: the backend applies the whole packet without reference
to any draw before it */
struct gpu_draw
{
	gpu_texture color_target;     /* 0: depth-only draw */
	gpu_texture depth_target;     /* 0: none */
	gpu_shader vertex_shader;
	gpu_shader pixel_shader;
	struct gpu_viewport viewport;
	struct gpu_rect scissor;
	struct gpu_depth_stencil_state depth_stencil;
	struct gpu_blend_state blend;
	struct gpu_raster_state raster;
	struct gpu_stage stages[4];
	struct gpu_vertex_stream streams[16];
	struct gpu_vertex_attribute attributes[16];
	float constant_values[16][4]; /* for GPU_STREAM_CONSTANT attributes */
	gpu_buffer index_buffer;      /* 0: not indexed; 16-bit indices */
	uint32_t index_offset;
	uint32_t primitive;
	uint32_t count;
	int32_t base_vertex;
};

/* ---------- drawing */

/* gpu_clear.flags: the buffers cleared */
enum { GPU_CLEAR_COLOR = 1, GPU_CLEAR_DEPTH = 2, GPU_CLEAR_STENCIL = 4 };
/* gpu_clear.channel_mask: the color channels cleared */
enum { GPU_CHANNEL_RED = 1, GPU_CHANNEL_GREEN = 2, GPU_CHANNEL_BLUE = 4, GPU_CHANNEL_ALPHA = 8 };

struct gpu_clear
{
	gpu_texture color_target;   /* 0: none */
	gpu_texture depth_target;   /* 0: none; then no depth or stencil flags */
	uint32_t flags;
	uint32_t channel_mask;
	uint32_t color;             /* ARGB */
	float depth;
	uint32_t stencil;
};

/* clears each rectangle (in target pixels, as gpu_rect is everywhere) of the
requested buffers; depth and stencil are written whatever the draw-time write
masks are */
void gpu_clear(const struct gpu_clear *clear, const struct gpu_rect *rectangles, uint32_t count);

/* draws; returns 0 if the draw was skipped because its program failed to link */
uint32_t gpu_draw(const struct gpu_draw *draw, const struct gpu_constant_store *constants,
	const struct gpu_uniforms *uniforms);

/* ---------- visibility (occlusion) tests */

/* slots a test's result is kept in; slot 0 is the backend's */
enum { GPU_VISIBILITY_SLOTS = 4096 };

/* the draws until gpu_visibility_end count their samples */
void gpu_visibility_begin(void);
/* the count goes to slot (1 to GPU_VISIBILITY_SLOTS - 1) */
void gpu_visibility_end(uint32_t slot);
/* 1 and the raw count (as occlusion_mode counts) once it is known, else 0 */
uint32_t gpu_visibility_result(uint32_t slot, uint32_t *samples);

/* ---------- frames */

/* submits the work queued so far */
void gpu_flush(void);
/* letterboxes the back buffer into the drawable, swaps, and starts the next
frame's transient buffers. Returns how long from now, in microseconds, until
the next frame is due on the display, when the backend schedules its frames
(Metal), else 0: render_interpolation.c blends that frame for the moment it
is shown. */
uint32_t gpu_present(gpu_texture back_buffer);

/* a stereo frame's pictures (halo_stereo.h): each eye's color and depth, and
the HUD drawn once for both. Fixed-width fields only: it crosses the guest/host
boundary */
struct gpu_stereo_present
{
	gpu_texture eye_color[2], eye_depth[2], hud;   /* hud: 0 if nothing drew it this frame */
	float near_meters, far_meters;     /* the frame's depth range, for the Compositor */
	int32_t mode;                      /* enum halo_stereo_mode */
	int32_t cinematic;                 /* 1 while the cutscene screen is up */
	float fade[4];                     /* RGB and intensity of the script fade */
	float hud_aspect;                  /* the HUD's width over its height as laid out */
	float vignette;                    /* 0 to 1: how much HEAD mode darkens the eyes' edges */
	int32_t cut_covered;               /* 1 while the script fade covers a cut (no fade through black) */
	/* 1 while a menu, a help panel, the console or a progress bar drew:
	into the UI layer (ui) in HEAD mode's full view, where the HUD is split;
	into the HUD layer elsewhere (the film, SCREEN gameplay) */
	int32_t hud_ui;
	float reticle[3];                  /* where the HUD's center points in the eyes' frame (halo_stereo_reticle) */
	float hud_tangents[2];             /* the HUD pass's half tangents across and up (halo_stereo_hud_tangents) */
	/* the zoomed picture (halo_stereo.h), in place of the eyes' pictures,
	over the whole view; 0 if the frame isn't zoomed. Its half tangents
	across and up (halo_stereo_zoom_view) size the presenter's quad */
	gpu_texture zoom;
	float zoom_tangents[2];
	/* the crosshairs' layer (HALO_STEREO_LAYER_RETICLE), laid out as the
	HUD; 0 if none drew this frame */
	gpu_texture reticle_layer;
	/* each HUD group's target (enum halo_hud_group), laid out as the HUD,
	and the rectangle its draws cover: x0, y0, x1, y1 in layout lines (x
	across hud_aspect * 480, y down 480); 0 and an empty rectangle for a
	group nothing drew this frame. The catch-all is hud */
	gpu_texture hud_group[HALO_HUD_GROUP_COUNT];
	float hud_group_extent[HALO_HUD_GROUP_COUNT][4];
	/* the UI layer (HALO_STEREO_LAYER_UI), laid out as the HUD; 0 if
	nothing drew it this frame */
	gpu_texture ui;
	/* 0 to 1: the widgets' full-screen dim this frame, which isn't drawn in
	the UI layer (halo_stereo_ui_dim_active): the presenter multiplies the
	eyes' color by 1 - ui_dim */
	float ui_dim;
	/* HEAD mode's cutscene window (halo_stereo_window.h, halo_stereo_expansion):
	1 while the film's rectangle expands out to the full view, the eyes
	showing inside it, with its progress (0 to 1) and the bars (0 to 1 of
	the letterbox's) it still carries */
	int32_t expanding;
	float expansion;
	float expansion_bars;
	/* Task 12k's spike, the immersive cutscene (halo_stereo_cutscene.h): 1
	on a frame whose eyes show a cutscene's camera turned by the head; the
	director's frame's forward and up in the eyes' frame (the head's at the
	render: x right, y up, z back), its half tangents across and up, and how
	much the blurred outside is darkened and desaturated (0 to 1). The HUD
	layer goes whole on the frame, as on the film's screen */
	int32_t cutscene;
	float cutscene_forward[3];
	float cutscene_up[3];
	float cutscene_tangents[2];
	float cutscene_dim;
	/* 1 while HEAD mode's full view is held black to cover a seat's exit
	glide (halo_stereo_cut_requested); when it drops, the view comes up as
	after any cut */
	int32_t cut_requested;
};

/* presents a stereo frame as gpu_present does a mono one (and returns the
same). The GL backend's debug view puts eye 0 in the left half of the window,
eye 1 in the right (or, zoomed, the zoomed picture in both), and the HUD over
each half. */
uint32_t gpu_present_stereo(const struct gpu_stereo_present *present);
/* the GL calls (a backend's commands) issued since the last call, for
debug.gpu_stats */
uint32_t gpu_call_count_take(void);

/* ---------- compiling at map load (shader_list.c)

A map's list names the shaders and pipelines its draws use, recorded from
earlier runs, so that they are compiled while it loads rather than when a
frame first draws with them. */

/* the list for name (a map's file name, "a10"), as the app carries it: its
length, 0 for none; the text, without a terminator, is copied while it fits
size */
uint32_t gpu_warm_list_read(const char *name, char *text, uint32_t size);
/* from warm_begin to warm_end, gpu_shader_create returns at once and the
shader compiles alongside the others, and gpu_pipeline_warm builds a
pipeline as a draw with that description would; warm_end returns when all of
it is done */
void gpu_warm_begin(void);
void gpu_pipeline_warm(const struct gpu_pipeline_description *description);
void gpu_warm_end(void);
/* a pipeline a draw built outside warming, since the last call: 1 and its
description, or 0 when there are no more */
uint32_t gpu_pipeline_built_take(struct gpu_pipeline_description *description);

/* gpu_initialize flags */
enum
{
	GPU_INITIALIZE_DEBUG = 1,        /* debug.gl_debug: report GPU errors */
	/* display.renderer = "metal": the Metal backend (iOS and tvOS hosts) */
	GPU_INITIALIZE_METAL = 2,
	/* debug.frame_counter: the iOS host shows the frame number and game time */
	GPU_INITIALIZE_FRAME_COUNTER = 4,
	/* debug.fixed_timestep: a frame is 1/30 s of game time (halo_virtual_clock.h) */
	GPU_INITIALIZE_FIXED_TIMESTEP = 8,
	/* display.frame_pacing = "refresh": any whole number of refreshes a
	frame, not only rates that are multiples of 30 frames a second (Metal) */
	GPU_INITIALIZE_PACING_ANY_RATE = 16,
	/* display.frame_pacing = "off": present each frame as soon as it's
	drawn (Metal) */
	GPU_INITIALIZE_PACING_OFF = 32,
	/* display.compressed_textures: DXT textures upload as BC1-3 where the
	GPU has them (Metal; GL decides from its extensions) */
	GPU_INITIALIZE_COMPRESSED_TEXTURES = 64,
	/* display.upscaler = "metalfx": a back buffer smaller than its place on
	screen is scaled up by MetalFX (Metal), not bilinearly */
	GPU_INITIALIZE_METALFX = 128,
	/* display.immersive: the picture goes on a screen in an immersive space
	(Metal, visionOS 26 and later) */
	GPU_INITIALIZE_IMMERSIVE = 256,
	/* debug.metal_state_cache = false: the Metal backend makes every encoder
	call a draw has, without skipping the ones that set what the encoder
	already holds (metal_state_cache.h) */
	GPU_INITIALIZE_NO_STATE_CACHE = 512,
	/* debug.metal_specialize = false: the Metal backend draws with each
	vertex shader's unspecialized function (vertex_function), its fallback
	when specializing fails */
	GPU_INITIALIZE_NO_SPECIALIZE = 1024,
	/* debug.metal_pipeline_archive = false: the Metal backend neither loads
	nor saves its pipeline archive (archive_open) */
	GPU_INITIALIZE_NO_PIPELINE_ARCHIVE = 2048,
};
/* probe the context, which must be current, and set it up */
void gpu_initialize(uint32_t flags, struct gpu_capabilities *capabilities);

/* the line every backend logs at the end of gpu_initialize: the capabilities
that change what the front end does, which a GL run and a Metal run of the
same build must agree on (tools/mac_run.py compare --across-backends) */
#define GPU_CAPABILITIES_LOG(log, capabilities) \
	log("GPU capabilities: vertex BGRA %u, base vertex %u, triangle fans %u, line loops %u, sampler LOD bias %u, " \
		"occlusion %u, S3TC %u, border clamp %u, max texture size %u", (unsigned)(capabilities)->vertex_bgra, \
		(unsigned)(capabilities)->base_vertex, (unsigned)(capabilities)->triangle_fans, \
		(unsigned)(capabilities)->line_loops, (unsigned)(capabilities)->sampler_lod_bias, \
		(unsigned)(capabilities)->occlusion_mode, (unsigned)(capabilities)->s3tc, \
		(unsigned)(capabilities)->border_clamp, (unsigned)(capabilities)->max_texture_size)

/* ---------- backends

Every function above, for building a table of one backend's functions
(struct gpu_backend) and the entry points that call through it: F(return
type, name without gpu_, parameters, arguments) for a function with a
result, P(name, parameters, arguments) for one without. GPU_OPERATIONS is
every one but gpu_initialize, which picks the backend in the iOS host
(port/ios/host/host_gpu_dispatch.c); elsewhere gpu_gl.c's entry points call
its own functions. */

#define GPU_OPERATIONS(F, P) \
	F(gpu_texture, texture_create, (const struct gpu_texture_description *description), (description)) \
	P(texture_upload, (gpu_texture texture, uint32_t face, uint32_t level, const void *data, uint32_t size), \
		(texture, face, level, data, size)) \
	P(texture_copy_level, (gpu_texture source, gpu_texture destination, uint32_t level), (source, destination, level)) \
	P(texture_generate_mipmaps, (gpu_texture texture, uint32_t base_level), (texture, base_level)) \
	P(texture_destroy, (gpu_texture texture), (texture)) \
	F(uint32_t, texture_read, (gpu_texture texture, void *pixels, uint32_t size), (texture, pixels, size)) \
	F(gpu_buffer, buffer_create, (uint32_t size), (size)) \
	P(buffer_write, (gpu_buffer buffer, uint32_t offset, uint32_t size, const void *data, uint32_t flags), \
		(buffer, offset, size, data, flags)) \
	P(stream_reserve, (uint32_t vertex_bytes, uint32_t index_bytes), (vertex_bytes, index_bytes)) \
	F(uint32_t, stream, (uint32_t kind, const void *data, uint32_t size, gpu_buffer *buffer), (kind, data, size, buffer)) \
	F(gpu_shader, shader_create, (uint32_t stage, const char *source), (stage, source)) \
	P(clear, (const struct gpu_clear *clear, const struct gpu_rect *rectangles, uint32_t count), \
		(clear, rectangles, count)) \
	F(uint32_t, draw, (const struct gpu_draw *draw, const struct gpu_constant_store *constants, \
		const struct gpu_uniforms *uniforms), (draw, constants, uniforms)) \
	P(visibility_begin, (void), ()) \
	P(visibility_end, (uint32_t slot), (slot)) \
	F(uint32_t, visibility_result, (uint32_t slot, uint32_t *samples), (slot, samples)) \
	P(flush, (void), ()) \
	F(uint32_t, present, (gpu_texture back_buffer), (back_buffer)) \
	F(uint32_t, present_stereo, (const struct gpu_stereo_present *present), (present)) \
	F(uint32_t, call_count_take, (void), ()) \
	/* compiling at map load (shader_list.c): the map's list as the app \
	carries it (its length; the text is copied while it fits size), and \
	between warm_begin and warm_end, shaders compile in parallel and \
	pipeline_warm builds a pipeline as a draw would, all finished by \
	warm_end; a backend without pipelines (GL) has no list and builds none */ \
	F(uint32_t, warm_list_read, (const char *name, char *text, uint32_t size), (name, text, size)) \
	P(warm_begin, (void), ()) \
	P(pipeline_warm, (const struct gpu_pipeline_description *description), (description)) \
	P(warm_end, (void), ()) \
	/* a pipeline built for a draw, outside warming, since the last call: 1 \
	and its description, or 0 when there are no more */ \
	F(uint32_t, pipeline_built_take, (struct gpu_pipeline_description *description), (description))

#define GPU_FUNCTIONS(F, P) \
	GPU_OPERATIONS(F, P) \
	P(initialize, (uint32_t flags, struct gpu_capabilities *capabilities), (flags, capabilities))

#define GPU_BACKEND_FUNCTION(type, name, parameters, arguments) type (*name) parameters;
#define GPU_BACKEND_PROCEDURE(name, parameters, arguments) void (*name) parameters;
/* one backend's functions; it holds host pointers, so it never crosses to
the guest */
struct gpu_backend
{
	GPU_FUNCTIONS(GPU_BACKEND_FUNCTION, GPU_BACKEND_PROCEDURE)
};
#undef GPU_BACKEND_FUNCTION
#undef GPU_BACKEND_PROCEDURE

#endif
