/*
D3D8_BGFX.INL

A test renderer: the Xbox Direct3D 8 device drawn with bgfx instead of
OpenGL, so that each platform can draw with its own API: Metal on macOS,
Vulkan on Windows and Linux, and on Windows Direct3D 11 when display.renderer
= "d3d11" asks for it (for testing). display.renderer = "bgfx" (HALO_RENDERER=bgfx)
chooses it at start-up, in every 64-bit desktop build (configure.py
--without-bgfx leaves it out); when bgfx cannot start, the game draws with
OpenGL as usual (platform_video_reopen_for_gl). It is part of
d3d8_gl.c, included at its end, and shares the device's state: what the game
sets through the XDK's inline functions and the entry points above is read at
each draw, as the OpenGL path reads it. OpenGL is never initialized while it
runs (gl_ready stays FALSE), so the OpenGL path's entry points do nothing.

The idea of drawing the Xbox's Direct3D through bgfx comes from Theseus by
Milenko (UIX Desktop), whose desktop renderer does it for the dashboard. The
game cannot use Theseus's precompiled fixed-function shaders, though: its
vertex shaders are Xbox microcode and its pixel shaders register combiner
states, translated at run time (nv2a_vsh.c, nv2a_psh.c). Their GLSL is
rewritten for Vulkan's rules (bg_vulkan_glsl) and compiled to SPIR-V with
glslang; Vulkan takes the SPIR-V, and on macOS SPIRV-Cross turns it into
Metal's shading language. Either goes to bgfx in its shader container, which
is kept on disk by the GLSL's hash, so a shader is translated once.

The frame is bgfx views, in order: each target change, clear after draws,
viewport move or copy into a texture starts the next view. Render targets
are bgfx textures found by the surface's address, as the OpenGL path's are
(render_target_get), and a texture at a target's address samples it.
Visibility tests are bgfx occlusion queries (counting samples). bgfx has no depth bias, depth range or stencil write
mask of its own: the depth range is applied in the vertex shader, a biased
draw's pixel shader writes its depth offset as polygon offset does, and the
stencil write mask rides in the back stencil's read mask, as bgfx reads it. The port's own pictures
(the high-res text and HUD, menu art, the overlay) are bgfx textures too.
*/

#include <SDL3/SDL.h>
#include <bgfx/c99/bgfx.h>
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#if defined(__APPLE__) || defined(_WIN32)
#include "spirv_cross_c.h"
#endif

#include "hud_hires.h"
#include "menu_files.h"
#include "text_hires.h"
#include "ui_overlay_cellwall.h"

/* a 2D copy of width x height from the source's top left (mip level) to the
destination's (bgfx's texture regions) */
static void bg_blit_2d(bgfx_view_id_t view, bgfx_texture_handle_t destination, uint8_t level,
	bgfx_texture_handle_t source, uint16_t width, uint16_t height)
{
	bgfx_texture_region_t to = { 0 }, from = { 0 };

	if (!BGFX_HANDLE_IS_VALID(destination) || !BGFX_HANDLE_IS_VALID(source))
		return;

	to.handle = destination;
	to.mip = level;
	to.width = width;
	to.height = height;
	to.depth = 1;
	from.handle = source;
	from.width = width;
	from.height = height;
	from.depth = 1;
	bgfx_blit(view, &to, &from);
}

/* a read-back of the texture's top mip level into data */
static uint32_t bg_read_texture(bgfx_texture_handle_t texture, void *data)
{
	bgfx_texture_region_t region = { 0 };

	if (!BGFX_HANDLE_IS_VALID(texture))
		return 0;
	region.handle = texture;
	region.depth = 1;
	return bgfx_read_texture(&region, data);
}

SDL_Window *platform_sdl_window(void);

BOOL xgpu_bgfx;

/* the backend bgfx draws with: Vulkan (Windows, Linux) takes SPIR-V and
its uniforms' table, Metal (macOS) its own language, which it reflects */
static BOOL bg_vulkan;
/* Direct3D 11 (Windows, with display.renderer = "d3d11"): the shaders as Metal's GLSL, through SPIRV-Cross to HLSL and
d3dcompiler_47 to DXBC (bg_hlsl, bg_dxbc), in the container Vulkan's go in */
static BOOL bg_d3d11;

#define BG_SHADER_BUCKETS 1024
#define BG_TEXTURE_BUCKETS 4096
#define BG_TARGET_BUCKETS 256
/* views a frame may use (bgfx allows 4096); the last is the present's */
#define BG_MAXIMUM_VIEWS 1023
/* each staged vertex is all sixteen input registers, four floats each */
#define BG_VERTEX_BYTES (XGPU_VERTEX_ATTRIBUTE_COUNT * 16)
/* the vertex cache: its buckets, the most it holds, how long an unused
range stays and how a range the game keeps rewriting is left to stream
(as the OpenGL path's mirror: rewrites no more than BG_VERTEX_REWRITE_FRAMES
apart, BG_VERTEX_VOLATILE_REWRITES in a row, stream for
BG_VERTEX_VOLATILE_FRAMES) */
#define BG_VERTEX_BUCKETS 4096
#define BG_VERTEX_CACHE_BUDGET (512UL << 20)
#define BG_VERTEX_UNUSED_FRAMES 300
#define BG_VERTEX_REWRITE_FRAMES 2
#define BG_VERTEX_VOLATILE_REWRITES 4
#define BG_VERTEX_VOLATILE_FRAMES 600
/* Metal's uniform space a frame (bgfx built with 32 MB), less a margin:
draws past it are skipped rather than overrun it */
#define BG_UNIFORM_BUDGET ((32UL << 20) - (1UL << 20))

/* the frame's transient vertices the scene leaves for the overlay
(xgpu_bgfx_overlay_draw), drawn after it at the present: 1 MB, in the
scene's vertices. A busy view that used them all up took the overlay's
screens and text with it */
#define BG_OVERLAY_RESERVE_VERTICES ((1UL << 20) / BG_VERTEX_BYTES)
/* occlusion queries, used as a ring (bgfx built with 4096) */
#define BG_QUERY_COUNT 4096
#define BG_COMPOSITE_LEVELS 16
/* frames until a visibility test's queries have their results (bgfx's
frame latency, 3, and the frame that reads them) */
#define BG_QUERY_LATENCY 5
/* a slot's tests waiting for their results (the game tests each again every
frame) */
#define BG_TESTS_PENDING 8

/* ---------- uniforms

The translators' uniforms, each a bgfx uniform of the same name. bgfx's
Metal renderer knows only vec4 (and matrix) members, so a float or a
shorter vector becomes a vec4 named <name>_v4 (bg_vulkan_glsl). */

static struct draw_uniforms bg_uniforms;
/* the viewport's depth range (x MinZ, y MaxZ - MinZ), and a biased draw's
offset (x slope factor, y constant in depth units) */
static float bg_depth_range[4], bg_depth_bias[4];

static const struct
{
	const char *name;
	const float *source;
	unsigned short count;
	unsigned short components;
} bg_uniform_sources[] =
{
	{ "c", &device.constants[0][0], XGPU_VERTEX_CONSTANT_COUNT, 4 },
	{ "viewport_scale", bg_uniforms.viewport_scale, 1, 4 },
	{ "viewport_offset", bg_uniforms.viewport_offset, 1, 4 },
	{ "point_size_v4", &bg_uniforms.point_size, 1, 1 },
	{ "screen_offset_v4", &bg_uniforms.screen_offset, 1, 1 },
	{ "ps_c0", &bg_uniforms.ps_c0[0][0], 8, 4 },
	{ "ps_c1", &bg_uniforms.ps_c1[0][0], 8, 4 },
	{ "ps_final_c0", bg_uniforms.ps_final_c0, 1, 4 },
	{ "ps_final_c1", bg_uniforms.ps_final_c1, 1, 4 },
	{ "fog_color", bg_uniforms.fog_color, 1, 4 },
	{ "fog_parameters", bg_uniforms.fog_parameters, 1, 4 },
	{ "alpha_reference_v4", &bg_uniforms.alpha_reference, 1, 1 },
	{ "bump_matrix", &bg_uniforms.bump_matrix[0][0], 4, 4 },
	{ "bump_luminance", &bg_uniforms.bump_luminance[0][0], 4, 4 },
	{ "texture_scale", &bg_uniforms.texture_scale[0][0], 4, 4 },
	{ "texture_lod_bias", bg_uniforms.texture_lod_bias, 1, 4 },
	{ "depth_range", bg_depth_range, 1, 4 },
	{ "depth_bias", bg_depth_bias, 1, 4 },
};
#define BG_UNIFORM_COUNT (sizeof(bg_uniform_sources) / sizeof(bg_uniform_sources[0]))

/* the varyings, at the same locations in both stages */
static const char *const bg_varyings[] =
{
	"xD0", "xD1", "xB0", "xB1", "xT0", "xT1", "xT2", "xT3", "xFog", "xWorldNormal", "xWorldPosition",
};
/* their types, as the translators declare them */
static const char *const bg_varying_types[] =
{
	"vec4", "vec4", "vec4", "vec4", "vec4", "vec4", "vec4", "vec4", "float", "vec4", "vec3",
};

/* the input registers v0 to v15 as bgfx's attributes, which its Metal
renderer finds by these names */
static const char *const bg_attribute_names[XGPU_VERTEX_ATTRIBUTE_COUNT] =
{
	"a_position", "a_normal", "a_tangent", "a_bitangent", "a_color0", "a_color1", "a_color2", "a_color3",
	"a_indices", "a_weight", "a_texcoord0", "a_texcoord1", "a_texcoord2", "a_texcoord3", "a_texcoord4", "a_texcoord5",
};

/* bg_shader_get's variants */
enum
{
	/* a pixel shader that offsets its depth (bg_vulkan_glsl) */
	_bg_shader_depth_bias = 1,
};

struct bg_shader
{
	struct bg_shader *next;
	Uint64 hash;
	bgfx_shader_handle_t shader;
	/* its uniform block's bytes, which Metal copies at each draw */
	unsigned long uniform_size;
	BOOL fragment, failed;
	/* while a compiler thread makes it (bg_shader_ready): its job, and
	(under bg_jobs.lock) the code it made (Metal source, or Vulkan's
	shader container) and its bytes, or why it failed */
	struct bg_shader_job *job;
	BOOL done;
	char *code;
	unsigned long code_size;
	char *error;
};

struct bg_fragment
{
	struct bg_fragment *next;
	unsigned long hash;
	struct nv2a_pixel_shader_key key;
	int flags;
	struct bg_shader *shader;
};

struct bg_program
{
	struct bg_program *next;
	struct bg_shader *vertex, *fragment;
	bgfx_program_handle_t program;
#ifdef HALO_SHADER_PREP
	/* the map whose shader key list has it (d3d8_shader_prep.inl) */
	unsigned long noted;
#endif
};

struct bg_texture
{
	struct bg_texture *next;
	DWORD data, format_word, size_word;
	unsigned long palette_hash;
	unsigned long address, size, generation;
	struct xgpu_texture_description description;
	bgfx_texture_handle_t texture;
	BOOL created, mipmapped, failed;
	/* the frame it was last bound in: a change after that is a new
	texture, so the frame's earlier draws keep what they drew with */
	unsigned long used_frame;
	/* the high-res HUD texture drawn in its place (hud_hires.h), or -1, as
	of its settings' changes */
	long override;
	unsigned long override_settings;
};

/* a render target: a surface's address, its size and kind, at a scale */
struct bg_target
{
	struct bg_target *next;
	DWORD data;
	unsigned long width, height, pixel_width, pixel_height;
	float scale[2];
	BOOL depth, fresh;
	/* samples a pixel (0: not multisampled; bg_screen_samples) */
	int samples;
	bgfx_texture_handle_t texture;
	unsigned long last_rendered, written;
};

struct bg_framebuffer
{
	struct bg_framebuffer *next;
	uint16_t color, depth;
	bgfx_frame_buffer_handle_t handle;
};

/* a texture the game renders one level at a time (the water's ripples), its
levels' targets copied into one mipmapped texture */
struct bg_composite
{
	struct bg_composite *next;
	DWORD data;
	unsigned long width, height, levels;
	bgfx_texture_handle_t texture;
	uint16_t sources[BG_COMPOSITE_LEVELS];
	unsigned long written[BG_COMPOSITE_LEVELS];
	/* how many levels the game rendered, from the first; the levels past
	them are made of the last one it did (bg_composite_fill), each in a
	target of its own, copied into the texture's level */
	unsigned long rendered;
	bgfx_texture_handle_t scratch[BG_COMPOSITE_LEVELS];
	bgfx_frame_buffer_handle_t scratch_framebuffer[BG_COMPOSITE_LEVELS], level_framebuffer[BG_COMPOSITE_LEVELS];
	/* the frame the levels were last made for, and whether they were */
	unsigned long filled_frame;
	BOOL filled;
};

static struct
{
	BOOL ready;
	SDL_Window *window;
	/* the depth buffers' format, the first of bg_depth_formats the renderer
	draws into, and whether it has a stencil (bg_formats_choose) */
	bgfx_texture_format_t depth_format;
	BOOL depth_stencil;
	bgfx_vertex_layout_t layout;
	bgfx_uniform_handle_t uniforms[BG_UNIFORM_COUNT];
	bgfx_uniform_handle_t samplers[4];
	unsigned long frame;

	/* the window's pixels, as bgfx's back buffer was last reset to */
	int window_width, window_height;
	unsigned long reset_flags;

	/* the bound targets: their frame buffer, pixels a unit and pixels */
	struct bg_target *target_color, *target_depth;
	bgfx_frame_buffer_handle_t framebuffer;
	float target_scale[2];
	unsigned long target_width, target_height;
	int target_samples;
	unsigned long write_serial;

	/* the frame's views: the one being drawn, its frame buffer and
	rectangle, whether it has drawn or cleared yet, and the copies the next
	one starts with */
	unsigned short view;
	bgfx_frame_buffer_handle_t view_framebuffer;
	unsigned short view_rect[4];
	BOOL view_used;
	unsigned short present_view;

	bgfx_texture_handle_t blank, blank_cube, blank_volume;
	/* (1x1 white: a clear of some color channels, bg_clear_channels) */
	bgfx_texture_handle_t white;
	struct bg_shader *shaders[BG_SHADER_BUCKETS];
	struct bg_fragment *fragments[BG_SHADER_BUCKETS];
	struct bg_program *programs[BG_SHADER_BUCKETS];
	struct bg_texture *textures[BG_TEXTURE_BUCKETS];
	struct bg_target *targets[BG_TARGET_BUCKETS];
	struct bg_framebuffer *framebuffers;
	struct bg_composite *composites;
	bgfx_program_handle_t blit, crt, copy;
	bgfx_uniform_handle_t crt_parameters[3];
	/* the quads that are drawn every frame, kept out of the frame's transient
	vertices: when a busy view used those up, the present drew only its
	clear, and the whole window went black (#1047). The present's is the
	picture's letterboxed corners, written again when they change; a
	channel clear's covers its view */
	bgfx_dynamic_vertex_buffer_handle_t present_quad;
	float present_quad_corner[2];
	bgfx_vertex_buffer_handle_t full_quad;

	/* the visibility tests: the queries of the one running, and each slot's
	latest, its count (in the game's pixels) and its target's pixels per
	game pixel */
	bgfx_occlusion_query_handle_t queries[BG_QUERY_COUNT];
	unsigned long query_next;
	BOOL test_active;
	unsigned long test_first, test_count;
	struct
	{
		/* its tests still waiting for their queries, oldest first */
		struct
		{
			unsigned long first, count, frame;
			float area;
			/* the test's number (D3DDevice_EndVisibilityTest) */
			unsigned long number;
		} pending[BG_TESTS_PENDING];
		unsigned char pending_count;
		UINT known;
		/* the number of the test known is of (0: none yet) */
		unsigned long known_number;
	} tests[VISIBILITY_TEST_SLOTS];

	/* the overlay's (xgpu_bgfx_overlay_draw) */
	bgfx_program_handle_t overlay;
	bgfx_vertex_layout_t overlay_layout;
	bgfx_texture_handle_t overlay_atlas, overlay_icons, overlay_picture, overlay_plasma, overlay_medals;
	bgfx_uniform_handle_t overlay_uniforms[5], overlay_samplers[5];
	BOOL overlay_medals_ready;
	int overlay_picture_serial, overlay_plasma_serial;
	BOOL overlay_failed;

	/* vertices converted once and kept on the GPU (bg_vertex_cache_get) */
	struct bg_vertex_entry *vertex_buckets[BG_VERTEX_BUCKETS];
	unsigned long vertex_cache_bytes;
	unsigned long stat_cached_vertices, stat_streamed_vertices;
	/* this frame's shader and vertex work (bg_frame_account) */
	struct
	{
		unsigned long translated, loaded, programs;
		double translate_seconds, load_seconds, vertex_seconds;
		uint32_t libraries, pipelines;
		double library_seconds, pipeline_seconds;
	} account;

	/* debug.screenshot_every's copy of the back buffer, read back */
	bgfx_texture_handle_t capture;
	unsigned long capture_width, capture_height;
	unsigned char *capture_pixels;
	uint32_t capture_ready;
	BOOL capture_pending;
	char capture_path[512];
#ifdef HALO_CAPTURE
	/* a player's screenshot (capture.c): bgfx's screenshot of the window,
the overlay on the picture, asked for at a present (bg_shot_start), cut to
the picture's part of the window when bgfx hands it over (bg_shot_taken),
and passed on after the frame (bg_present) */
	BOOL shot_pending;
	int shot_frames;
	int shot_crop[4];
	unsigned char *shot_pixels;
	long shot_width, shot_height;
	int shot_bgra;
	/* the clips' frames (capture_clips.c): the back buffer read back whole
	into one of a few buffers, each lent to the encoder once bgfx has
	filled it (bgfx's blit cannot scale; the encoder scales) */
	struct
	{
		bgfx_texture_handle_t texture;
		unsigned long width, height;
		unsigned char *pixels;
		uint32_t ready;
		int state;
		volatile int released;
		double time;
	} clip[3];
#endif

	unsigned long uniform_bytes;
	unsigned long drawn, skipped, stat_tests, stat_seen, stat_waited;
	/* draws left out because the frame's transient vertices or indices ran
	out */
	unsigned long stat_starved;
	/* time in bgfx_frame (waiting for the render thread and the GPU) */
	double stat_frame_wait;
} bg;

static struct bg_shader bg_failed_shader = { .failed = TRUE };

static BOOL bg_active(void)
{
	return bg.ready;
}

static void bg_log_once(const char *what)
{
	static const char *logged[32];
	int index;

	for (index = 0; index < 32 && logged[index]; index++)
	{
		if (logged[index] == what)
			return;
	}
	if (index < 32)
		logged[index] = what;
	platform_log("bgfx: %s (not drawn yet)", what);
}

static Uint64 bg_hash(const void *data, unsigned long size, Uint64 hash)
{
	const unsigned char *bytes = data;

	while (size--)
		hash = (hash ^ *bytes++) * 1099511628211ULL;
	return hash;
}

/* ---------- GLSL for Vulkan

The translators write GLSL for OpenGL: loose uniforms, varyings matched by
name. SPIR-V wants the uniforms in a block and the varyings at locations;
bgfx wants the vertex inputs named as its attributes and the block's members
as vec4s. Each line the translators write declares one thing, so the rewrite
goes line by line. The clip-space fix for OpenGL without glClipControl comes
out: Metal's clip space is Direct3D's, as the Xbox's is, and so is Vulkan's
as bgfx sets its viewport (upside down).

The bindings are each backend's: Metal's are given by SPIRV-Cross (bg_msl),
Vulkan's are bgfx's own (its shader compiler's): the vertex shader's
uniform block at 0, the pixel shader's at 1, and a sampler at its stage
plus 2 as a texture and its stage plus 18 as a sampler, since bgfx's
Vulkan renderer binds them apart.

Two things OpenGL does in fixed function are added: a vertex shader maps
its depth into the viewport's range (glDepthRange's), and a biased pixel
shader moves its depth as glPolygonOffset does (its slope from the depth's
derivatives). Both wrap the shader's own main. */

static void bg_replace_all(struct xgpu_text *text, const char *from, const char *to)
{
	struct xgpu_text out = { 0 };
	const char *cursor = text->buffer, *found;
	unsigned long length = (unsigned long)strlen(from);

	if (!cursor)
		return;
	while ((found = strstr(cursor, from)) != NULL)
	{
		/* (whole identifiers only) */
		char after = found[length];
		char before = found > text->buffer ? found[-1] : ' ';
		BOOL inside = (after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z') ||
			(after >= '0' && after <= '9') || after == '_' ||
			(before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z') ||
			(before >= '0' && before <= '9') || before == '_';

		if (inside)
			xgpu_text_append(&out, "%.*s", (int)(found - cursor + length), cursor);
		else
			xgpu_text_append(&out, "%.*s%s", (int)(found - cursor), cursor, to);
		cursor = found + length;
	}
	xgpu_text_append(&out, "%s", cursor);
	free(text->buffer);
	*text = out;
}

/* the vertex constants a shader reads: all of them with relative
addressing, otherwise up to its highest */
static unsigned long bg_constants_used(const char *source)
{
	const char *cursor = source;
	unsigned long highest = 0;

	if (strstr(source, "clamp(a0"))
		return XGPU_VERTEX_CONSTANT_COUNT;
	while ((cursor = strstr(cursor, "c[")) != NULL)
	{
		if (cursor == source || !((cursor[-1] >= 'a' && cursor[-1] <= 'z') || cursor[-1] == '_' ||
			(cursor[-1] >= 'A' && cursor[-1] <= 'Z') || (cursor[-1] >= '0' && cursor[-1] <= '9')))
		{
			unsigned long index = (unsigned long)strtoul(cursor + 2, NULL, 10);

			if (cursor[2] >= '0' && cursor[2] <= '9' && index + 1 > highest)
				highest = index + 1;
		}
		cursor += 2;
	}
	return highest ? (highest > XGPU_VERTEX_CONSTANT_COUNT ? XGPU_VERTEX_CONSTANT_COUNT : highest) : 1;
}

static char *bg_vulkan_glsl(const char *source, BOOL fragment, int flags, unsigned long *uniform_size)
{
	struct xgpu_text block = { 0 }, defines = { 0 }, body = { 0 }, all = { 0 };
	const char *line = source;
	unsigned long index, size = 0;
	int sampler_next = 0;
	BOOL declared[sizeof(bg_varyings) / sizeof(bg_varyings[0])] = { FALSE };

	xgpu_text_append(&block, "#version 450\n");
	xgpu_text_append(&block, "layout(set = 0, binding = %d, std140) uniform Uniforms\n{\n",
		bg_vulkan ? (fragment ? 1 : 0) : 8);
	while (*line)
	{
		const char *end = strchr(line, '\n');
		unsigned long length = end ? (unsigned long)(end - line) : (unsigned long)strlen(line);
		char text[512], type[64], name[64];

		if (length >= sizeof(text))
		{
			xgpu_text_append(&body, "%.*s\n", (int)length, line);
			line += length + (end ? 1 : 0);
			continue;
		}
		memcpy(text, line, length);
		text[length] = 0;
		line += length + (end ? 1 : 0);
		/* (Vulkan keeps the position invariant, as OpenGL does: the game
		draws a surface again in passes that test for the same depth, from
		vertex shaders that would otherwise round it apart) */
		if (strncmp(text, "#version", 8) == 0 || strncmp(text, "precision ", 10) == 0 ||
			(!bg_vulkan && !bg_d3d11 && strcmp(text, "invariant gl_Position;") == 0) ||
			strcmp(text, "\tgl_Position.y = -gl_Position.y;") == 0 ||
			strcmp(text, "\tgl_Position.z = 2.0 * gl_Position.z - gl_Position.w;") == 0)
			continue;
		if (sscanf(text, "uniform %63s %63[A-Za-z0-9_]", type, name) == 2)
		{
			const char *bracket = strchr(text, '[');
			unsigned long count = bracket ? (unsigned long)atol(bracket + 1) : 1;

			if (strncmp(type, "sampler", 7) == 0)
			{
				/* tex0 to tex3 at their stages; others in order */
				int binding = strncmp(name, "tex", 3) == 0 && name[3] >= '0' && name[3] <= '3' && !name[4] ?
					name[3] - '0' : sampler_next;

				sampler_next = binding + 1;
				if (bg_vulkan)
				{
					/* (sampler2D tex0 as a texture2D and a sampler, and the
					name for the two together) */
					xgpu_text_append(&body,
						"layout(set = 0, binding = %d) uniform texture%s %s_texture;\n"
						"layout(set = 0, binding = %d) uniform sampler %s_sampler;\n"
						"#define %s %s(%s_texture, %s_sampler)\n",
						binding + 2, type + 7, name, binding + 18, name, name, type, name, name);
				}
				else
				{
					xgpu_text_append(&body, "layout(set = 0, binding = %d) %s\n", binding, text);
				}
				continue;
			}
			if (!fragment && strcmp(name, "c") == 0)
			{
				count = bg_constants_used(source);
				xgpu_text_append(&block, "\tvec4 c[%lu];\n", count);
			}
			else if (strcmp(type, "float") == 0 || strcmp(type, "vec2") == 0 || strcmp(type, "vec3") == 0)
			{
				xgpu_text_append(&block, "\tvec4 %s_v4;\n", name);
				xgpu_text_append(&defines, "#define %s %s_v4.%s\n", name, name,
					type[0] == 'f' ? "x" : type[3] == '2' ? "xy" : "xyz");
			}
			else
			{
				xgpu_text_append(&block, "\t%s\n", text + 8);
			}
			size += 16 * count;
			continue;
		}
		if ((strncmp(text, "in ", 3) == 0 || strncmp(text, "out ", 4) == 0) &&
			sscanf(text, "%*s %63s %63[A-Za-z0-9_];", type, name) == 2)
		{
			for (index = 0; index < sizeof(bg_varyings) / sizeof(bg_varyings[0]); index++)
			{
				if (strcmp(name, bg_varyings[index]) == 0)
					break;
			}
			if (index < sizeof(bg_varyings) / sizeof(bg_varyings[0]))
			{
				xgpu_text_append(&body, "layout(location = %lu) %s\n", index, text);
				declared[index] = TRUE;
				continue;
			}
		}
		if (strcmp(text, "void main()") == 0)
		{
			xgpu_text_append(&body, "void bg_main()\n");
			continue;
		}
		xgpu_text_append(&body, "%s\n", text);
	}
	/* (Direct3D 11 joins the stages by register, not by name or location:
	both declare every varying, in order, so that a pixel shader that reads
	a few finds them where the vertex shader wrote them) */
	if (bg_d3d11)
	{
		for (index = 0; index < sizeof(bg_varyings) / sizeof(bg_varyings[0]); index++)
		{
			if (!declared[index])
				xgpu_text_append(&body, "layout(location = %lu) %s %s %s;\n", index, fragment ? "in" : "out",
					bg_varying_types[index], bg_varyings[index]);
		}
		/* (and the pixel shader reads each, so that each stays in its
		signature) */
		if (fragment)
		{
			xgpu_text_append(&body, "float bg_kept;\nfloat bg_read_varyings()\n{\n\tfloat all = 0.0;\n");
			for (index = 0; index < sizeof(bg_varyings) / sizeof(bg_varyings[0]); index++)
				xgpu_text_append(&body, "\tall += %s.x;\n", bg_varyings[index]);
			xgpu_text_append(&body, "\treturn all;\n}\n");
		}
	}
	if (!fragment)
	{
		/* (glDepthRange's: the viewport's MinZ to MaxZ) */
		xgpu_text_append(&block, "\tvec4 depth_range;\n");
		xgpu_text_append(&body, "void main()\n{\n");
		for (index = 0; bg_d3d11 && index < sizeof(bg_varyings) / sizeof(bg_varyings[0]); index++)
		{
			if (!declared[index])
				xgpu_text_append(&body, "\t%s = %s(0.0);\n", bg_varyings[index], bg_varying_types[index]);
		}
		xgpu_text_append(&body,
			"\tbg_main();\n"
			"\tgl_Position.z = gl_Position.z * depth_range.y + depth_range.x * gl_Position.w;\n"
			"}\n");
	}
	else if (flags & _bg_shader_depth_bias)
	{
		/* (glPolygonOffset's: the slope's factor and a constant in the
		depth buffer's units) */
		xgpu_text_append(&block, "\tvec4 depth_bias;\n");
		xgpu_text_append(&body,
			"void main()\n"
			"{\n"
			"%s"
			"\tbg_main();\n"
			"\tfloat slope = max(abs(dFdx(gl_FragCoord.z)), abs(dFdy(gl_FragCoord.z)));\n"
			"\tgl_FragDepth = clamp(gl_FragCoord.z + depth_bias.x * slope + depth_bias.y, 0.0, 1.0);\n"
			"}\n", bg_d3d11 ? "\tbg_kept = bg_read_varyings();\n" : "");
	}
	else
	{
		xgpu_text_append(&body, "void main()\n{\n%s\tbg_main();\n}\n",
			bg_d3d11 && fragment ? "\tbg_kept = bg_read_varyings();\n" : "");
	}
	size += 16;
	/* (a block may not be empty) */
	if (fragment && !(flags & _bg_shader_depth_bias) && size == 16)
		xgpu_text_append(&block, "\tvec4 unused_v4;\n");
	xgpu_text_append(&all, "%s};\n%s%s", block.buffer, defines.buffer ? defines.buffer : "",
		body.buffer ? body.buffer : "");
	free(block.buffer);
	free(defines.buffer);
	free(body.buffer);
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		char from[16];

		snprintf(from, sizeof(from), "v%lu_in", index);
		bg_replace_all(&all, from, bg_attribute_names[index]);
	}
	if (uniform_size)
		*uniform_size = size;
	return all.buffer;
}

/* ---------- shader compilation: GLSL, SPIR-V, Metal's language, bgfx's container */

/* SPIR-V for Vulkan's GLSL (malloc'd words), or NULL with why (malloc'd):
glslang, as bgfx's shader compiler has it, with the varyings and outputs
given no location placed by it */
static unsigned int *bg_spirv(const char *source, BOOL fragment, unsigned long *count, char **error)
{
	glslang_input_t input;
	glslang_shader_t *shader;
	glslang_program_t *program = NULL;
	glslang_stage_t stage = fragment ? GLSLANG_STAGE_FRAGMENT : GLSLANG_STAGE_VERTEX;
	int messages = GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT;
	unsigned int *words = NULL;

	memset(&input, 0, sizeof(input));
	input.language = GLSLANG_SOURCE_GLSL;
	input.stage = stage;
	input.client = GLSLANG_CLIENT_VULKAN;
	input.client_version = GLSLANG_TARGET_VULKAN_1_0;
	input.target_language = GLSLANG_TARGET_SPV;
	input.target_language_version = GLSLANG_TARGET_SPV_1_0;
	input.code = source;
	input.default_version = 450;
	input.default_profile = GLSLANG_NO_PROFILE;
	input.messages = (glslang_messages_t)messages;
	input.resource = glslang_default_resource();
	if ((shader = glslang_shader_create(&input)) == NULL)
	{
		*error = strdup("glslang");
		return NULL;
	}
	glslang_shader_set_options(shader, GLSLANG_SHADER_AUTO_MAP_LOCATIONS);
	if (!glslang_shader_preprocess(shader, &input) || !glslang_shader_parse(shader, &input))
	{
		*error = strdup(glslang_shader_get_info_log(shader));
		goto done;
	}
	program = glslang_program_create();
	glslang_program_add_shader(program, shader);
	if (!glslang_program_link(program, messages) || !glslang_program_map_io(program))
	{
		*error = strdup(glslang_program_get_info_log(program));
		goto done;
	}
	glslang_program_SPIRV_generate(program, stage);
	*count = (unsigned long)glslang_program_SPIRV_get_size(program);
	if (*count && (words = malloc(*count * sizeof(*words))) != NULL)
		glslang_program_SPIRV_get(program, words);
	else
		*error = strdup("glslang made no SPIR-V");
done:
	if (program)
		glslang_program_delete(program);
	glslang_shader_delete(shader);
	return words;
}

#ifdef __APPLE__
/* Metal's shading language for the SPIR-V: the uniform block is bgfx's
_mtl_u in buffer 0, each sampler at its binding, the entry point bgfx's */
static char *bg_msl(const unsigned int *words, unsigned long word_count, BOOL fragment, char **error)
{
	spvc_context context = NULL;
	spvc_parsed_ir ir;
	spvc_compiler compiler;
	spvc_compiler_options options;
	spvc_resources resources;
	const spvc_reflected_resource *list;
	size_t count, index;
	const char *source = NULL;
	char *copy = NULL;
	SpvExecutionModel model = fragment ? SpvExecutionModelFragment : SpvExecutionModelVertex;

	*error = NULL;
	if (spvc_context_create(&context) != SPVC_SUCCESS)
		return NULL;
	if (spvc_context_parse_spirv(context, (const SpvId *)words, word_count, &ir) != SPVC_SUCCESS ||
		spvc_context_create_compiler(context, SPVC_BACKEND_MSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler) !=
			SPVC_SUCCESS)
		goto done;
	spvc_compiler_create_compiler_options(compiler, &options);
	spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_MSL_VERSION, SPVC_MAKE_MSL_VERSION(2, 1, 0));
	spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_MSL_PLATFORM, SPVC_MSL_PLATFORM_MACOS);
	spvc_compiler_install_compiler_options(compiler, options);
	spvc_compiler_rename_entry_point(compiler, "main", "xlatMtlMain", model);
	spvc_compiler_create_shader_resources(compiler, &resources);
	spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, &list, &count);
	for (index = 0; index < count; index++)
	{
		spvc_msl_resource_binding binding;

		spvc_compiler_set_name(compiler, list[index].id, "_mtl_u");
		spvc_msl_resource_binding_init(&binding);
		binding.stage = model;
		binding.desc_set = spvc_compiler_get_decoration(compiler, list[index].id, SpvDecorationDescriptorSet);
		binding.binding = spvc_compiler_get_decoration(compiler, list[index].id, SpvDecorationBinding);
		binding.msl_buffer = 0;
		spvc_compiler_msl_add_resource_binding(compiler, &binding);
	}
	spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, &list, &count);
	for (index = 0; index < count; index++)
	{
		spvc_msl_resource_binding binding;
		unsigned stage = spvc_compiler_get_decoration(compiler, list[index].id, SpvDecorationBinding);

		spvc_msl_resource_binding_init(&binding);
		binding.stage = model;
		binding.desc_set = 0;
		binding.binding = stage;
		binding.msl_texture = stage;
		binding.msl_sampler = stage;
		spvc_compiler_msl_add_resource_binding(compiler, &binding);
	}
	if (spvc_compiler_compile(compiler, &source) == SPVC_SUCCESS && source)
		copy = strdup(source);
done:
	/* (the context's error, copied: it goes with the context) */
	if (!copy)
	{
		const char *why = spvc_context_get_last_error_string(context);

		platform_log("bgfx: SPIRV-Cross: %s", why);
		*error = strdup(why && *why ? why : "SPIRV-Cross");
	}
	spvc_context_destroy(context);
	return copy;
}
#endif

#ifdef _WIN32
/* Direct3D 11's semantics for the inputs, by location: bgfx's input layout
names bgfx's attributes so (renderer_d3d11.cpp's s_attrib), in the order
bg_attribute_ids gives them */
static const struct
{
	const char *name;
	unsigned index;
} bg_hlsl_semantics[XGPU_VERTEX_ATTRIBUTE_COUNT] =
{
	{ "POSITION", 0 }, { "NORMAL", 0 }, { "TANGENT", 0 }, { "BITANGENT", 0 }, { "COLOR", 0 }, { "COLOR", 1 },
	{ "COLOR", 2 }, { "COLOR", 3 }, { "BLENDINDICES", 0 }, { "BLENDWEIGHT", 0 }, { "TEXCOORD", 0 },
	{ "TEXCOORD", 1 }, { "TEXCOORD", 2 }, { "TEXCOORD", 3 }, { "TEXCOORD", 4 }, { "TEXCOORD", 5 },
};

/* HLSL (shader model 5.0) for the SPIR-V of Metal's GLSL: the uniform block
is the stage's constant buffer 0, where bgfx puts its uniforms, at the
block's std140 offsets; each sampler a texture and a sampler state at its
stage (t and s); the inputs at bgfx's semantics */
static char *bg_hlsl(const unsigned int *words, unsigned long word_count, BOOL fragment, char **error)
{
	spvc_context context = NULL;
	spvc_parsed_ir ir;
	spvc_compiler compiler;
	spvc_compiler_options options;
	spvc_resources resources;
	const spvc_reflected_resource *list;
	size_t count, index;
	const char *source = NULL;
	char *copy = NULL;

	*error = NULL;
	if (spvc_context_create(&context) != SPVC_SUCCESS)
		return NULL;
	if (spvc_context_parse_spirv(context, (const SpvId *)words, word_count, &ir) != SPVC_SUCCESS ||
		spvc_context_create_compiler(context, SPVC_BACKEND_HLSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler) !=
			SPVC_SUCCESS)
		goto done;
	spvc_compiler_create_compiler_options(compiler, &options);
	spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_HLSL_SHADER_MODEL, 50);
	/* (Direct3D has no point size: the vertex shaders' gl_PointSize is
	left out, as Direct3D 11 draws points one pixel wide) */
	spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_HLSL_POINT_SIZE_COMPAT, SPVC_TRUE);
	spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_HLSL_POINT_COORD_COMPAT, SPVC_TRUE);
	spvc_compiler_install_compiler_options(compiler, options);
	spvc_compiler_create_shader_resources(compiler, &resources);
	spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, &list, &count);
	for (index = 0; index < count; index++)
	{
		spvc_compiler_set_decoration(compiler, list[index].id, SpvDecorationDescriptorSet, 0);
		spvc_compiler_set_decoration(compiler, list[index].id, SpvDecorationBinding, 0);
	}
	if (!fragment)
	{
		for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		{
			spvc_hlsl_vertex_attribute_remap remap;
			char semantic[24];

			snprintf(semantic, sizeof(semantic), "%s%u", bg_hlsl_semantics[index].name,
				bg_hlsl_semantics[index].index);
			remap.location = (unsigned)index;
			remap.semantic = semantic;
			spvc_compiler_hlsl_add_vertex_attribute_remap(compiler, &remap, 1);
		}
	}
	if (spvc_compiler_compile(compiler, &source) == SPVC_SUCCESS && source)
		copy = strdup(source);
done:
	/* (the context's error, copied: it goes with the context) */
	if (!copy)
	{
		const char *why = spvc_context_get_last_error_string(context);

		platform_log("bgfx: SPIRV-Cross (HLSL): %s", why);
		*error = strdup(why && *why ? why : "SPIRV-Cross");
	}
	spvc_context_destroy(context);
	return copy;
}

/* d3dcompiler_47.dll's D3DCompile and its blobs (ID3DBlob), declared here
so that the Xbox's Direct3D 8 headers this file has need no Windows ones */
struct bg_blob;
struct bg_blob_table
{
	long (__stdcall *query_interface)(struct bg_blob *self, const void *id, void **out);
	unsigned long (__stdcall *add_reference)(struct bg_blob *self);
	unsigned long (__stdcall *release)(struct bg_blob *self);
	void *(__stdcall *buffer)(struct bg_blob *self);
	size_t (__stdcall *buffer_size)(struct bg_blob *self);
};
struct bg_blob
{
	const struct bg_blob_table *table;
};
typedef long (__stdcall *bg_d3d_compile)(const void *source, size_t size, const char *name, const void *defines,
	void *include, const char *entry, const char *target, unsigned flags, unsigned effect_flags,
	struct bg_blob **code, struct bg_blob **errors);
/* D3DCOMPILE_OPTIMIZATION_LEVEL3 */
#define BG_D3DCOMPILE_FLAGS (1u << 15)

static bg_d3d_compile bg_d3d_compiler;
/* the compiler's file and version ("10.0.22621.1 native", or Wine's), part
of the shader cache's key with BG_D3DCOMPILE_FLAGS */
static char bg_d3d_compiler_identity[96] = "none";

/* win32_posix.c's: a DLL from System32 only; a loaded module's file version
and whether it is Wine's, as text */
void *win32_system_library(const char *name);
void win32_library_identity(void *module, char *text, unsigned long size);

/* whether Direct3D's shader compiler is there (bg_initialize, before bgfx
starts on Direct3D 11) */
static BOOL bg_dxbc_available(void)
{
	SDL_SharedObject *library;

	if (bg_d3d_compiler)
		return TRUE;
	/* (System32's only: never one in the game's folder or the working
	directory) */
	library = (SDL_SharedObject *)win32_system_library("d3dcompiler_47.dll");
	if (library)
	{
		bg_d3d_compiler = (bg_d3d_compile)SDL_LoadFunction(library, "D3DCompile");
		win32_library_identity(library, bg_d3d_compiler_identity, sizeof(bg_d3d_compiler_identity));
		platform_log("bgfx: d3dcompiler_47.dll %s", bg_d3d_compiler_identity);
	}
	if (!bg_d3d_compiler)
		platform_log("bgfx: Direct3D 11 needs d3dcompiler_47.dll, which is missing");
	return bg_d3d_compiler != NULL;
}

/* the DXBC for the HLSL (malloc'd, *size its bytes), or NULL with why */
static unsigned int *bg_dxbc(const char *hlsl, BOOL fragment, unsigned long *size, char **error)
{
	struct bg_blob *code = NULL, *errors = NULL;
	unsigned int *words = NULL;
	long result;

	*size = 0;
	result = bg_d3d_compiler(hlsl, strlen(hlsl), fragment ? "pixel" : "vertex", NULL, NULL, "main",
		fragment ? "ps_5_0" : "vs_5_0", BG_D3DCOMPILE_FLAGS, 0, &code, &errors);
	if (result >= 0 && code)
	{
		size_t length = code->table->buffer_size(code);

		if ((words = malloc(length + 4)) != NULL)
		{
			memcpy(words, code->table->buffer(code), length);
			*size = (unsigned long)length;
		}
	}
	else
	{
		*error = strdup(errors ? (const char *)errors->table->buffer(errors) : "D3DCompile failed");
	}
	if (code)
		code->table->release(code);
	if (errors)
		errors->table->release(errors);
	return words;
}
#endif

/* bgfx's attributes' numbers in its shader container, in its attributes'
order (bgfx's vertexlayout.cpp, which says they never change) */
/* the shader binary format bgfx reads (its shaderc's BGFX_SHADER_BIN_VERSION:
after the hashes, the raw bindings; a uniform's count in two bytes) */
#define BG_SHADER_BINARY_VERSION 14

static const Uint16 bg_attribute_ids[XGPU_VERTEX_ATTRIBUTE_COUNT] =
{
	0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0018, 0x0019,
	0x000e, 0x000f, 0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015,
};

static void bg_put(unsigned char **bytes, unsigned long *size, unsigned long *capacity, const void *data,
	unsigned long length)
{
	if (*size + length > *capacity)
	{
		unsigned char *grown;

		*capacity = (*size + length) * 2 + 256;
		if ((grown = realloc(*bytes, *capacity)) == NULL)
		{
			free(*bytes);
			*bytes = NULL;
		}
		else
		{
			*bytes = grown;
		}
	}
	if (*bytes)
		memcpy(*bytes + *size, data, length);
	*size += length;
}

/* bgfx's shader container for Vulkan (malloc'd, *size its bytes): the
uniforms' table, the SPIR-V, the vertex inputs and the uniform block's
size. bgfx's Vulkan renderer does not reflect the SPIR-V, so the table is
read off the rewritten GLSL (bg_vulkan_glsl): the block's members, each a
vec4 or an array of them, at their std140 offsets, and the samplers with
their stages and kinds; the inputs are at the locations of bgfx's attributes
(bg_attribute_names), which the list follows. Both hashes are 0 so that any
vertex shader goes with any pixel shader. */
static char *bg_vulkan_container(const void *code, unsigned long code_size, const char *source,
	BOOL fragment, unsigned long *size)
{
	unsigned char *bytes = NULL;
	unsigned long capacity = 0, offset = 0, block = 0, count_at;
	const char *line, *end;
	Uint16 count = 0, highest = 0, index;
	Uint32 value;
	BOOL inside = FALSE, used[XGPU_VERTEX_ATTRIBUTE_COUNT] = { FALSE };
	unsigned char byte;

	*size = 0;
	byte = fragment ? 'F' : 'V';
	bg_put(&bytes, &offset, &capacity, &byte, 1);
	bg_put(&bytes, &offset, &capacity, "SH", 2);
	byte = BG_SHADER_BINARY_VERSION;
	bg_put(&bytes, &offset, &capacity, &byte, 1);
	value = 0;
	bg_put(&bytes, &offset, &capacity, &value, 4); /* hash in */
	bg_put(&bytes, &offset, &capacity, &value, 4); /* hash out */
	bg_put(&bytes, &offset, &capacity, &value, 4); /* raw texture bindings */
	bg_put(&bytes, &offset, &capacity, &value, 4); /* raw storage bindings */
	count_at = offset;
	bg_put(&bytes, &offset, &capacity, &count, 2);
	for (line = source; line && *line; line = end ? end + 1 : NULL)
	{
		char name[64], type[32];
		unsigned long elements = 1;
		int location, binding;

		end = strchr(line, '\n');
		if (strncmp(line, "layout(set = 0, binding = ", 26) == 0 && strstr(line, "uniform Uniforms") &&
			(!end || strstr(line, "uniform Uniforms") < end))
		{
			inside = TRUE;
			continue;
		}
		if (inside && line[0] == '{')
			continue;
		if (inside && line[0] == '}')
		{
			inside = FALSE;
			continue;
		}
		if (inside)
		{
			const char *bracket;
			unsigned char entry[9];

			if (sscanf(line, "\tvec4 %63[A-Za-z0-9_]", name) != 1)
			{
				free(bytes);
				return NULL;
			}
			bracket = strchr(line, '[');
			if (bracket && (!end || bracket < end))
				elements = strtoul(bracket + 1, NULL, 10);
			/* (the block's filler, not a uniform of bgfx's) */
			if (strcmp(name, "unused_v4") != 0)
			{
				byte = (unsigned char)strlen(name);
				bg_put(&bytes, &offset, &capacity, &byte, 1);
				bg_put(&bytes, &offset, &capacity, name, byte);
				/* vec4, the array's length, its offset, its registers, no
				texture's component, kind or format */
				entry[0] = (unsigned char)BGFX_UNIFORM_TYPE_VEC4;
				value = (Uint32)elements;
				memcpy(entry + 1, &value, 2);
				value = (Uint32)block;
				memcpy(entry + 3, &value, 2);
				value = (Uint32)elements;
				memcpy(entry + 5, &value, 2);
				entry[7] = entry[8] = 0;
				bg_put(&bytes, &offset, &capacity, entry, 9);
				value = 0;
				bg_put(&bytes, &offset, &capacity, &value, 2);
				count++;
			}
			block += 16 * elements;
			continue;
		}
		/* (Vulkan's textures, or Direct3D 11's samplers, which are Metal's
		GLSL: sampler2D NAME at its stage) */
		if (sscanf(line, bg_vulkan ? "layout(set = 0, binding = %d) uniform texture%31s %63[A-Za-z0-9_]" :
			"layout(set = 0, binding = %d) uniform sampler%31s %63[A-Za-z0-9_]", &binding, type, name) == 3)
		{
			unsigned char entry[9];
			size_t length = strlen(name);

			/* (NAME_texture: the sampler's uniform is NAME) */
			if (length > 8 && strcmp(name + length - 8, "_texture") == 0)
				name[length - 8] = 0;
			byte = (unsigned char)strlen(name);
			bg_put(&bytes, &offset, &capacity, &byte, 1);
			bg_put(&bytes, &offset, &capacity, name, byte);
			/* a sampler (kUniformSamplerBit), at its texture's binding, of
			its kind (bgfx's shader.cpp TextureDimension ids) */
			entry[0] = (unsigned char)(BGFX_UNIFORM_TYPE_SAMPLER | 0x20 | (fragment ? 0x10 : 0));
			value = 1;
			memcpy(entry + 1, &value, 2);
			value = (Uint32)binding;
			memcpy(entry + 3, &value, 2);
			entry[5] = entry[6] = 0;
			entry[7] = 0;
			entry[8] = strcmp(type, "Cube") == 0 ? 0x04 : strcmp(type, "3D") == 0 ? 0x06 :
				strcmp(type, "2DArray") == 0 ? 0x03 : 0x02;
			bg_put(&bytes, &offset, &capacity, entry, 9);
			value = 0;
			bg_put(&bytes, &offset, &capacity, &value, 2);
			count++;
			continue;
		}
		if (!fragment && sscanf(line, "layout(location = %d) in ", &location) == 1 && location >= 0 &&
			location < XGPU_VERTEX_ATTRIBUTE_COUNT)
		{
			used[location] = TRUE;
			if (location + 1 > highest)
				highest = (Uint16)(location + 1);
		}
	}
	if (bytes)
		memcpy(bytes + count_at, &count, 2);
	value = (Uint32)code_size;
	bg_put(&bytes, &offset, &capacity, &value, 4);
	bg_put(&bytes, &offset, &capacity, code, value);
	byte = 0;
	bg_put(&bytes, &offset, &capacity, &byte, 1);
	/* (the inputs by location: a location not read has no attribute, 0xfffe.
	Not 0xffff, which bgfx's Vulkan renderer reads as instance data and
	gives a fallback input at a location another attribute has, which the
	spec forbids and AMD's drivers may refuse: tools/bgfx-patches/0005) */
	byte = (unsigned char)highest;
	bg_put(&bytes, &offset, &capacity, &byte, 1);
	for (index = 0; index < highest; index++)
	{
		Uint16 id = used[index] ? bg_attribute_ids[index] : 0xfffe;

		bg_put(&bytes, &offset, &capacity, &id, 2);
	}
	value = (Uint32)block;
	bg_put(&bytes, &offset, &capacity, &value, 2);
	*size = offset;
	return (char *)bytes;
}

/* bgfx's shader container for Metal source: its uniforms are found by
reflection, so the table is empty, and both hashes are 0 so that any vertex
shader goes with any pixel shader */
static bgfx_shader_handle_t bg_container(const char *msl, BOOL fragment)
{
	unsigned long length = (unsigned long)strlen(msl);
	unsigned char *bytes = malloc(length + 40);
	unsigned long offset = 0;
	Uint32 value;
	Uint16 count = 0;
	bgfx_shader_handle_t handle;

	bytes[offset++] = fragment ? 'F' : 'V';
	bytes[offset++] = 'S';
	bytes[offset++] = 'H';
	bytes[offset++] = BG_SHADER_BINARY_VERSION;
	value = 0;
	memcpy(bytes + offset, &value, 4), offset += 4; /* hash in */
	memcpy(bytes + offset, &value, 4), offset += 4; /* hash out */
	memcpy(bytes + offset, &value, 4), offset += 4; /* raw texture bindings */
	memcpy(bytes + offset, &value, 4), offset += 4; /* raw storage bindings */
	memcpy(bytes + offset, &count, 2), offset += 2;
	value = (Uint32)length;
	memcpy(bytes + offset, &value, 4), offset += 4;
	memcpy(bytes + offset, msl, length + 1), offset += length + 1;
	handle = bgfx_create_shader(bgfx_copy(bytes, (uint32_t)offset));
	free(bytes);
	return handle;
}

/* ---------- the shader cache and the compiler threads

A draw's shaders are made from the translators' GLSL: SPIR-V (glslang),
then Metal's language (SPIRV-Cross) or Vulkan's container, a few
milliseconds each. That is done
on compiler threads, never in a frame: a draw whose shaders are not made
yet is left out until they are (a frame or two, the first time a shader is
seen). What they make is kept in the cache folder (paths.cache, safe to
delete: bgfx/<backend>/<hash>.shader, by a hash of the shader's GLSL, the
backend and the compilers' versions; an entry that does not match is
ignored and made again), and each map's programs are listed there
(map-<name>.programs) so that the next time the map loads they are all made
during its loading screen (bg_warm). Vulkan's pipelines are kept there
too (<hash>.pipeline: bg_callback_cache_write). Past
BG_SHADER_CACHE_MAXIMUM the oldest entries are removed at start-up. */

/* (a new number when what is cached changes: the Metal source's or the
Vulkan container's layout, the entries' format) */
#define BG_SHADER_CACHE_FORMAT 3
#define BG_SHADER_CACHE_MAGIC "HXBS"
#define BG_SHADER_CACHE_MAXIMUM (64UL << 20)
#define BG_SHADER_CACHE_TRIMMED (48UL << 20)
#define BG_COMPILER_THREADS 2
/* the most of a map's programs made in one frame of its loading screen */
#define BG_WARM_PER_FRAME 48

struct bg_shader_job
{
	struct bg_shader_job *next;
	struct bg_shader *shader;
	char *source;
	char what[32];
};

struct bg_pair
{
	struct bg_pair *next;
	Uint64 vertex, fragment;
};

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t wake, finished;
	struct bg_shader_job *first, *last;
	BOOL started;
	/* the cache's folder and the versions its entries are made with */
	char folder[1024];
	Uint64 versions;
	/* the current map's programs, as listed (bg_pair_record), and the map
	whose loading screen makes them (bg_warm) */
	char map[64];
	struct bg_pair *pairs[BG_SHADER_BUCKETS];
	FILE *pairs_file;
	char warm_map[64];
	BOOL warm_pending;
	Uint64 *warm_list;
	unsigned long warm_count, warm_next;
} bg_jobs = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER };

static void bg_cache_entry_path(char *path, size_t size, Uint64 hash)
{
	snprintf(path, size, "%s/%016llx.shader", bg_jobs.folder, (unsigned long long)hash);
}

/* a cache entry: the magic, the format, the versions, the shader's hash,
its uniform block's bytes, the code's length and hash, the code (Metal
source, or Vulkan's shader container) */
struct bg_cache_header
{
	char magic[4];
	Uint32 format;
	Uint64 versions, hash;
	Uint32 uniform_size, length;
	Uint64 check;
};

/* the cached code of a shader (malloc'd, *size its bytes, followed by a
0), or NULL */
static char *bg_cache_read(Uint64 hash, unsigned long *uniform_size, unsigned long *size)
{
	char path[1100];
	struct bg_cache_header header;
	char *msl = NULL;
	FILE *file;

	if (!*bg_jobs.folder)
		return NULL;
	bg_cache_entry_path(path, sizeof(path), hash);
	file = fopen(path, "rb");
	if (!file)
		return NULL;
	if (fread(&header, sizeof(header), 1, file) == 1 && !memcmp(header.magic, BG_SHADER_CACHE_MAGIC, 4) &&
		header.format == BG_SHADER_CACHE_FORMAT && header.versions == bg_jobs.versions && header.hash == hash &&
		header.length < (16U << 20) && (msl = malloc(header.length + 1)) != NULL)
	{
		if (fread(msl, 1, header.length, file) != header.length ||
			bg_hash(msl, header.length, 14695981039346656037ULL) != header.check)
		{
			free(msl);
			msl = NULL;
		}
		else
		{
			msl[header.length] = 0;
			*size = header.length;
			if (uniform_size)
				*uniform_size = header.uniform_size;
		}
	}
	fclose(file);
	return msl;
}

static void bg_cache_write(Uint64 hash, unsigned long uniform_size, const char *msl, unsigned long size)
{
	char path[1100], temporary[1120];
	struct bg_cache_header header;
	FILE *file;

	if (!*bg_jobs.folder)
		return;
	bg_cache_entry_path(path, sizeof(path), hash);
	snprintf(temporary, sizeof(temporary), "%s.%lu.tmp", path, (unsigned long)(uintptr_t)pthread_self() & 0xffff);
	memcpy(header.magic, BG_SHADER_CACHE_MAGIC, 4);
	header.format = BG_SHADER_CACHE_FORMAT;
	header.versions = bg_jobs.versions;
	header.hash = hash;
	header.uniform_size = (Uint32)uniform_size;
	header.length = (Uint32)size;
	header.check = bg_hash(msl, header.length, 14695981039346656037ULL);
	file = fopen(temporary, "wb");
	if (!file)
		return;
	if (fwrite(&header, sizeof(header), 1, file) == 1 && fwrite(msl, 1, header.length, file) == header.length &&
		fclose(file) == 0)
		rename(temporary, path);
	else
		remove(temporary);
}

/* the backend's code for a shader's Vulkan GLSL (malloc'd, *size its
bytes): Metal source, or the shader container with Vulkan's SPIR-V or
Direct3D 11's DXBC; or NULL with why */
static char *bg_compile(const char *source, BOOL fragment, const char *what, char **error, unsigned long *size)
{
	unsigned long count = 0;
	unsigned int *words = bg_spirv(source, fragment, &count, error);
	char *code = NULL;

	(void)what;
	*size = 0;
	if (!words)
		return NULL;
	if (bg_vulkan)
	{
		if ((code = bg_vulkan_container(words, count * sizeof(*words), source, fragment, size)) == NULL)
			*error = strdup("its uniforms are not vec4s");
	}
#ifdef _WIN32
	else if (bg_d3d11)
	{
		char *why = NULL;
		char *hlsl = bg_hlsl(words, count, fragment, &why);
		unsigned long dxbc_size = 0;
		unsigned int *dxbc;

		if (!hlsl)
			*error = why ? why : strdup("SPIRV-Cross");
		else if ((dxbc = bg_dxbc(hlsl, fragment, &dxbc_size, error)) != NULL)
		{
			if ((code = bg_vulkan_container(dxbc, dxbc_size, source, fragment, size)) == NULL)
				*error = strdup("its uniforms are not vec4s");
			free(dxbc);
		}
		free(hlsl);
	}
#endif
#ifdef __APPLE__
	else
	{
		char *why = NULL;

		if ((code = bg_msl(words, count, fragment, &why)) != NULL)
			*size = (unsigned long)strlen(code);
		else
			*error = why ? why : strdup("SPIRV-Cross");
	}
#endif
	free(words);
	return code;
}

static void *bg_compiler_thread(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&bg_jobs.lock);
	for (;;)
	{
		struct bg_shader_job *job;
		char *msl, *error = NULL;
		unsigned long size = 0;

		while (!bg_jobs.first)
			pthread_cond_wait(&bg_jobs.wake, &bg_jobs.lock);
		job = bg_jobs.first;
		bg_jobs.first = job->next;
		if (!bg_jobs.first)
			bg_jobs.last = NULL;
		pthread_mutex_unlock(&bg_jobs.lock);

		msl = bg_compile(job->source, job->shader->fragment, job->what, &error, &size);
		if (msl)
			bg_cache_write(job->shader->hash, job->shader->uniform_size, msl, size);

		pthread_mutex_lock(&bg_jobs.lock);
		job->shader->code = msl;
		job->shader->code_size = size;
		job->shader->error = error;
		job->shader->done = TRUE;
		pthread_cond_broadcast(&bg_jobs.finished);
	}
	return NULL;
}

/* the backend's name in the cache: its folder, and part of its versions */
static const char *bg_backend_folder(void)
{
	return bg_vulkan ? "vulkan" : bg_d3d11 ? "d3d11" : "metal";
}

/* the cache's folder, emptied of its oldest entries past its size */
static void bg_cache_start(void)
{
	unsigned int major = 0, minor = 0, patch = 0;
	glslang_version_t glslang;
	char versions[160];
	int index;

	glslang_get_version(&glslang);
#if defined(__APPLE__) || defined(_WIN32)
	spvc_get_version(&major, &minor, &patch);
#endif
	snprintf(versions, sizeof(versions), "%s %d glslang %d.%d.%d%s spirv-cross %u.%u.%u bgfx %d",
		bg_backend_folder(), BG_SHADER_CACHE_FORMAT, glslang.major, glslang.minor, glslang.patch,
		glslang.flavor ? glslang.flavor : "", major, minor, patch, BGFX_API_VERSION);
#ifdef _WIN32
	/* (Direct3D 11's DXBC is the compiler's: its version and flags too) */
	if (bg_d3d11)
	{
		size_t used = strlen(versions);

		snprintf(versions + used, sizeof(versions) - used, " d3dcompiler %s flags %x", bg_d3d_compiler_identity,
			BG_D3DCOMPILE_FLAGS);
	}
#endif
	bg_jobs.versions = bg_hash(versions, strlen(versions), 14695981039346656037ULL);
	snprintf(bg_jobs.folder, sizeof(bg_jobs.folder), "%s/bgfx/%s", platform_folder(_port_folder_cache),
		bg_backend_folder());
	{
		char parent[1100];

		snprintf(parent, sizeof(parent), "%s/bgfx", platform_folder(_port_folder_cache));
		SDL_CreateDirectory(parent);
		if (!SDL_CreateDirectory(bg_jobs.folder))
			*bg_jobs.folder = 0;
	}
	if (*bg_jobs.folder)
	{
		int count = 0;
		char **names = SDL_GlobDirectory(bg_jobs.folder, "*", 0, &count);
		struct { char *name; Uint64 time, size; } *entries = names && count ? calloc((size_t)count, sizeof(*entries)) : NULL;
		Uint64 total = 0;

		for (index = 0; entries && index < count; index++)
		{
			char path[1200];
			SDL_PathInfo info;

			snprintf(path, sizeof(path), "%s/%s", bg_jobs.folder, names[index]);
			entries[index].name = names[index];
			if (SDL_GetPathInfo(path, &info))
			{
				entries[index].time = (Uint64)info.modify_time;
				entries[index].size = info.size;
				total += info.size;
			}
		}
		/* (the oldest first, a few at a time: there are not many) */
		while (entries && total > BG_SHADER_CACHE_MAXIMUM)
		{
			int oldest = -1;

			for (index = 0; index < count; index++)
			{
				if (entries[index].size && (oldest < 0 || entries[index].time < entries[oldest].time))
					oldest = index;
			}
			if (oldest < 0)
				break;
			{
				char path[1200];

				snprintf(path, sizeof(path), "%s/%s", bg_jobs.folder, entries[oldest].name);
				remove(path);
			}
			total -= entries[oldest].size;
			entries[oldest].size = 0;
			if (total <= BG_SHADER_CACHE_TRIMMED)
				break;
		}
		free(entries);
		SDL_free(names);
	}
	for (index = 0; index < BG_COMPILER_THREADS; index++)
	{
		pthread_t thread;

		if (pthread_create(&thread, NULL, bg_compiler_thread, NULL) == 0)
		{
			pthread_detach(thread);
			bg_jobs.started = TRUE;
		}
	}
}

static struct bg_shader *bg_shader_failed(struct bg_shader *shader, const char *what, const char *why)
{
	platform_log("bgfx: the %s shader %016llx fails: %s", what, (unsigned long long)shader->hash, why);
	shader->failed = TRUE;
	return shader;
}

/* a shader made from its code (on this thread: bgfx's) */
static void bg_shader_finish(struct bg_shader *shader, char *msl, unsigned long size, const char *what)
{
	shader->shader = bg_vulkan || bg_d3d11 ? bgfx_create_shader(bgfx_copy(msl, (uint32_t)size)) :
		bg_container(msl, shader->fragment);
	if (!BGFX_HANDLE_IS_VALID(shader->shader))
		bg_shader_failed(shader, what, "bgfx refused it");
}

/* whether the shader is made, finishing it if its thread is done */
static BOOL bg_shader_ready(struct bg_shader *shader)
{
	char *msl, *error;
	unsigned long size;
	struct bg_shader_job *job;

	if (!shader || shader->failed)
		return FALSE;
	if (BGFX_HANDLE_IS_VALID(shader->shader))
		return TRUE;
	if (!shader->job)
		return FALSE;
	pthread_mutex_lock(&bg_jobs.lock);
	if (!shader->done)
	{
		pthread_mutex_unlock(&bg_jobs.lock);
		return FALSE;
	}
	msl = shader->code, shader->code = NULL;
	size = shader->code_size;
	error = shader->error, shader->error = NULL;
	job = shader->job, shader->job = NULL;
	pthread_mutex_unlock(&bg_jobs.lock);
	bg.account.translated++;
	if (msl)
		bg_shader_finish(shader, msl, size, job->what);
	else
		bg_shader_failed(shader, job->what, error ? error : "it does not compile");
	if (!msl && config_boolean("debug.gpu_stats"))
		platform_log("%s", job->source);
	free(msl);
	free(error);
	free(job->source);
	free(job);
	return !shader->failed;
}

static struct bg_shader *bg_shader_find(Uint64 hash)
{
	struct bg_shader *shader;

	for (shader = bg.shaders[hash % BG_SHADER_BUCKETS]; shader; shader = shader->next)
	{
		if (shader->hash == hash)
			return shader;
	}
	return NULL;
}

static struct bg_shader *bg_shader_add(Uint64 hash, BOOL fragment, unsigned long uniform_size)
{
	struct bg_shader *shader = calloc(1, sizeof(*shader));

	shader->hash = hash;
	shader->fragment = fragment;
	shader->uniform_size = uniform_size;
	shader->shader = (bgfx_shader_handle_t)BGFX_INVALID_HANDLE;
	shader->next = bg.shaders[hash % BG_SHADER_BUCKETS];
	bg.shaders[hash % BG_SHADER_BUCKETS] = shader;
	return shader;
}

/* a shader of the translators' GLSL: found, read from the cache folder, or
handed to a compiler thread (bg_shader_ready tells when it is made; wait
waits for it, for the renderer's own shaders) */
static struct bg_shader *bg_shader_get(const char *glsl, BOOL fragment, int flags, const char *what, BOOL wait)
{
	struct bg_shader *shader;
	char *source, *msl;
	unsigned long uniform_size = 0, size = 0;
	double start = stats_seconds();
	Uint64 hash;

	source = bg_vulkan_glsl(glsl, fragment, flags, &uniform_size);
	if (!source)
		return &bg_failed_shader;
	hash = bg_hash(source, strlen(source), fragment ? 14695981039346656037ULL : 1469598103934665603ULL);
	if ((shader = bg_shader_find(hash)) != NULL)
	{
		free(source);
		return shader;
	}
	shader = bg_shader_add(hash, fragment, uniform_size);
	if ((msl = bg_cache_read(hash, NULL, &size)) != NULL)
	{
		bg_shader_finish(shader, msl, size, what);
		free(msl);
		free(source);
		bg.account.loaded++;
		bg.account.load_seconds += stats_seconds() - start;
		return shader;
	}
	if (wait || !bg_jobs.started)
	{
		char *error = NULL;

		msl = bg_compile(source, fragment, what, &error, &size);
		if (msl)
		{
			bg_cache_write(hash, uniform_size, msl, size);
			bg_shader_finish(shader, msl, size, what);
		}
		else
		{
			bg_shader_failed(shader, what, error ? error : "it does not compile");
		}
		free(msl);
		free(error);
		free(source);
		bg.account.translated++;
		bg.account.translate_seconds += stats_seconds() - start;
		return shader;
	}
	{
		struct bg_shader_job *job = calloc(1, sizeof(*job));

		job->shader = shader;
		job->source = source;
		snprintf(job->what, sizeof(job->what), "%s", what);
		shader->job = job;
		pthread_mutex_lock(&bg_jobs.lock);
		if (bg_jobs.last)
			bg_jobs.last->next = job;
		else
			bg_jobs.first = job;
		bg_jobs.last = job;
		pthread_cond_signal(&bg_jobs.wake);
		pthread_mutex_unlock(&bg_jobs.lock);
	}
	return shader;
}

static struct bg_shader *bg_vertex_shader(struct vertex_shader_object *program, BOOL immediate)
{
	void **slot = &program->bg_shader[immediate ? 1 : 0];

	if (!*slot)
	{
		/* (packed normals are unpacked as they are staged: bg_vertex_element) */
		char *glsl = nv2a_vertex_shader_to_glsl(program->instructions, program->instruction_count, 0, NULL, FALSE);

		*slot = bg_shader_get(glsl, FALSE, 0, "vertex", FALSE);
		free(glsl);
	}
	return bg_shader_ready(*slot) ? *slot : NULL;
}

static struct bg_shader *bg_fragment_shader(const struct nv2a_pixel_shader_key *key, int flags)
{
	unsigned long hash = (unsigned long)bg_hash(key, sizeof(*key), 14695981039346656037ULL) ^ (unsigned long)flags;
	struct bg_fragment **bucket = &bg.fragments[hash % BG_SHADER_BUCKETS], *entry;
	char *glsl;

	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->hash == hash && entry->flags == flags && !memcmp(&entry->key, key, sizeof(*key)))
			return bg_shader_ready(entry->shader) ? entry->shader : NULL;
	}
	glsl = nv2a_pixel_shader_to_glsl(key);
	entry = calloc(1, sizeof(*entry));
	entry->hash = hash;
	entry->key = *key;
	entry->flags = flags;
	entry->shader = bg_shader_get(glsl, TRUE, flags, "pixel", FALSE);
	free(glsl);
	entry->next = *bucket;
	*bucket = entry;
	return bg_shader_ready(entry->shader) ? entry->shader : NULL;
}

/* ---------- each map's programs, made at its loading screen */

static FILE *bg_pairs_open(const char *map, const char *mode)
{
	char path[1200];

	if (!*bg_jobs.folder || !*map)
		return NULL;
	snprintf(path, sizeof(path), "%s/map-%s.programs", bg_jobs.folder, map);
	return fopen(path, mode);
}

static BOOL bg_pair_known(Uint64 vertex, Uint64 fragment, BOOL add)
{
	unsigned long bucket = (unsigned long)((vertex * 31 + fragment) % BG_SHADER_BUCKETS);
	struct bg_pair *pair;

	for (pair = bg_jobs.pairs[bucket]; pair; pair = pair->next)
	{
		if (pair->vertex == vertex && pair->fragment == fragment)
			return TRUE;
	}
	if (add && (pair = calloc(1, sizeof(*pair))) != NULL)
	{
		pair->vertex = vertex;
		pair->fragment = fragment;
		pair->next = bg_jobs.pairs[bucket];
		bg_jobs.pairs[bucket] = pair;
	}
	return FALSE;
}

/* a program drawn with on the current map, listed for its next load */
static void bg_pair_record(Uint64 vertex, Uint64 fragment)
{
	Uint64 record[2];

	if (bg_pair_known(vertex, fragment, TRUE))
		return;
	if (!bg_jobs.pairs_file)
		bg_jobs.pairs_file = bg_pairs_open(bg_jobs.map, "ab");
	if (!bg_jobs.pairs_file)
		return;
	record[0] = vertex;
	record[1] = fragment;
	fwrite(record, sizeof(record), 1, bg_jobs.pairs_file);
	fflush(bg_jobs.pairs_file);
}

/* the map whose tags are loaded (cache_files.c, through d3d8_gl.c: on the
game's thread, which may not be bgfx's), its programs made at the next
frames (bg_warm) */
static void bg_map_loaded(const char *map)
{
	pthread_mutex_lock(&bg_jobs.lock);
	snprintf(bg_jobs.warm_map, sizeof(bg_jobs.warm_map), "%s", map ? map : "");
	bg_jobs.warm_pending = TRUE;
	pthread_mutex_unlock(&bg_jobs.lock);
}

static bgfx_program_handle_t bg_program_get(struct bg_shader *vertex, struct bg_shader *fragment);

/* a map has loaded since bgfx started (the main menu's ui.map is the first):
the Vulkan start note's frames are counted from it (bg_frame_out) */
static BOOL bg_map_seen;

/* the current map's listed programs, a few a frame (bg_present): their
shaders read from the cache folder (missing ones are left for the draws) */
static void bg_warm(void)
{
	unsigned long made = 0;

	if (bg_jobs.warm_pending)
	{
		char map[64];
		unsigned long bucket;
		FILE *file;

		pthread_mutex_lock(&bg_jobs.lock);
		snprintf(map, sizeof(map), "%s", bg_jobs.warm_map);
		bg_jobs.warm_pending = FALSE;
		pthread_mutex_unlock(&bg_jobs.lock);
		bg_map_seen = TRUE;
		/* the list of the map now loaded */
		if (bg_jobs.pairs_file)
			fclose(bg_jobs.pairs_file);
		bg_jobs.pairs_file = NULL;
		for (bucket = 0; bucket < BG_SHADER_BUCKETS; bucket++)
		{
			while (bg_jobs.pairs[bucket])
			{
				struct bg_pair *next = bg_jobs.pairs[bucket]->next;

				free(bg_jobs.pairs[bucket]);
				bg_jobs.pairs[bucket] = next;
			}
		}
		snprintf(bg_jobs.map, sizeof(bg_jobs.map), "%s", map);
		free(bg_jobs.warm_list);
		bg_jobs.warm_list = NULL;
		bg_jobs.warm_count = bg_jobs.warm_next = 0;
		if ((file = bg_pairs_open(map, "rb")) != NULL)
		{
			Uint64 record[2];
			unsigned long capacity = 0;

			while (fread(record, sizeof(record), 1, file) == 1)
			{
				if (bg_pair_known(record[0], record[1], TRUE))
					continue;
				if (bg_jobs.warm_count + 2 > capacity)
				{
					Uint64 *grown;

					capacity = capacity ? capacity * 2 : 512;
					if (!(grown = realloc(bg_jobs.warm_list, capacity * sizeof(Uint64))))
						break;
					bg_jobs.warm_list = grown;
				}
				bg_jobs.warm_list[bg_jobs.warm_count++] = record[0];
				bg_jobs.warm_list[bg_jobs.warm_count++] = record[1];
			}
			fclose(file);
			if (bg_jobs.warm_count)
				platform_log("bgfx: making %lu programs of %s", bg_jobs.warm_count / 2, map);
		}
	}
	while (bg_jobs.warm_next < bg_jobs.warm_count && made < BG_WARM_PER_FRAME)
	{
		struct bg_shader *pair[2];
		int which;

		for (which = 0; which < 2; which++)
		{
			Uint64 hash = bg_jobs.warm_list[bg_jobs.warm_next + which];

			if ((pair[which] = bg_shader_find(hash)) == NULL)
			{
				unsigned long uniform_size = 0, size = 0;
				char *msl = bg_cache_read(hash, &uniform_size, &size);

				if (msl)
				{
					pair[which] = bg_shader_add(hash, which == 1, uniform_size);
					bg_shader_finish(pair[which], msl, size, which ? "pixel" : "vertex");
					free(msl);
					bg.account.loaded++;
				}
			}
		}
		if (pair[0] && pair[1] && BGFX_HANDLE_IS_VALID(pair[0]->shader) && BGFX_HANDLE_IS_VALID(pair[1]->shader))
			bg_program_get(pair[0], pair[1]);
		bg_jobs.warm_next += 2;
		made++;
	}
	if (bg_jobs.warm_next && bg_jobs.warm_next >= bg_jobs.warm_count)
	{
		free(bg_jobs.warm_list);
		bg_jobs.warm_list = NULL;
		bg_jobs.warm_count = bg_jobs.warm_next = 0;
	}
}

static struct bg_program *bg_program_entry(struct bg_shader *vertex, struct bg_shader *fragment);

static bgfx_program_handle_t bg_program_get(struct bg_shader *vertex, struct bg_shader *fragment)
{
	return bg_program_entry(vertex, fragment)->program;
}

static struct bg_program *bg_program_entry(struct bg_shader *vertex, struct bg_shader *fragment)
{
	unsigned long hash = (unsigned long)(((uintptr_t)vertex >> 4) ^ ((uintptr_t)fragment >> 2) * 31);
	struct bg_program **bucket = &bg.programs[hash % BG_SHADER_BUCKETS], *entry;

	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->vertex == vertex && entry->fragment == fragment)
			return entry;
	}
	entry = calloc(1, sizeof(*entry));
	entry->vertex = vertex;
	entry->fragment = fragment;
	entry->program = bgfx_create_program(vertex->shader, fragment->shader, false);
	bg.account.programs++;
	entry->next = *bucket;
	*bucket = entry;
	bg_pair_record(vertex->hash, fragment->hash);
	return entry;
}

/* ---------- state (Xbox render states hold OpenGL's enumerants) */

/* GL_NEVER (0x200) to GL_ALWAYS (0x207) as bgfx's test (1 less ... 8 always) */
static unsigned bg_compare(DWORD function)
{
	static const unsigned char tests[8] = { 7, 1, 3, 2, 5, 6, 4, 8 };

	return function >= 0x200 && function <= 0x207 ? tests[function - 0x200] : 8;
}

static unsigned bg_stencil_operation(DWORD operation)
{
	switch (operation)
	{
	case 0x1e00: return 1; /* keep */
	case 0x1e01: return 2; /* replace */
	case 0x1e02: return 4; /* increment, clamped */
	case 0x1e03: return 6; /* decrement, clamped */
	case 0x150a: return 7; /* invert */
	case 0x8507: return 3; /* increment, wrapped */
	case 0x8508: return 5; /* decrement, wrapped */
	default: return 0;
	}
}

static uint64_t bg_blend_factor(DWORD factor)
{
	switch (factor)
	{
	case 0: return BGFX_STATE_BLEND_ZERO;
	case 1: return BGFX_STATE_BLEND_ONE;
	case 0x300: return BGFX_STATE_BLEND_SRC_COLOR;
	case 0x301: return BGFX_STATE_BLEND_INV_SRC_COLOR;
	case 0x302: return BGFX_STATE_BLEND_SRC_ALPHA;
	case 0x303: return BGFX_STATE_BLEND_INV_SRC_ALPHA;
	case 0x304: return BGFX_STATE_BLEND_DST_ALPHA;
	case 0x305: return BGFX_STATE_BLEND_INV_DST_ALPHA;
	case 0x306: return BGFX_STATE_BLEND_DST_COLOR;
	case 0x307: return BGFX_STATE_BLEND_INV_DST_COLOR;
	case 0x308: return BGFX_STATE_BLEND_SRC_ALPHA_SAT;
	/* (bgfx has no constant alpha: the constant color stands in) */
	case 0x8001: case 0x8003: return BGFX_STATE_BLEND_FACTOR;
	case 0x8002: case 0x8004: return BGFX_STATE_BLEND_INV_FACTOR;
	default: return BGFX_STATE_BLEND_ONE;
	}
}

static uint64_t bg_blend_operation(DWORD operation)
{
	switch (operation)
	{
	case D3DBLENDOP_SUBTRACT: return BGFX_STATE_BLEND_EQUATION_SUB;
	case D3DBLENDOP_REVSUBTRACT:
	case D3DBLENDOP_REVSUBTRACTSIGNED: return BGFX_STATE_BLEND_EQUATION_REVSUB;
	case D3DBLENDOP_MIN: return BGFX_STATE_BLEND_EQUATION_MIN;
	case D3DBLENDOP_MAX: return BGFX_STATE_BLEND_EQUATION_MAX;
	default: return BGFX_STATE_BLEND_EQUATION_ADD;
	}
}

/* the draw's state from the render states (apply_raster_state's), and its
stencil (the back's read mask carries the write mask, bgfx's convention); the blend
constant in *rgba */
static uint64_t bg_state(BOOL has_depth, uint32_t *stencil, uint32_t *back_stencil, uint32_t *rgba)
{
	DWORD *rs = D3D__RenderState;
	DWORD write = rs[D3DRS_COLORWRITEENABLE];
	uint64_t state = 0;

	if (write & D3DCOLORWRITEENABLE_RED)
		state |= BGFX_STATE_WRITE_R;
	if (write & D3DCOLORWRITEENABLE_GREEN)
		state |= BGFX_STATE_WRITE_G;
	if (write & D3DCOLORWRITEENABLE_BLUE)
		state |= BGFX_STATE_WRITE_B;
	if (write & D3DCOLORWRITEENABLE_ALPHA)
		state |= BGFX_STATE_WRITE_A;
	if (has_depth && rs[D3DRS_ZENABLE])
	{
		state |= (uint64_t)bg_compare(rs[D3DRS_ZFUNC]) << BGFX_STATE_DEPTH_TEST_SHIFT;
		if (rs[D3DRS_ZWRITEENABLE])
			state |= BGFX_STATE_WRITE_Z;
	}
	*stencil = *back_stencil = BGFX_STENCIL_NONE;
	if (has_depth && bg.depth_stencil && rs[D3DRS_STENCILENABLE])
	{
		DWORD mask = rs[D3DRS_STENCILWRITEMASK] & 0xff;
		unsigned fail = bg_stencil_operation(rs[D3DRS_STENCILFAIL]);
		unsigned depth_fail = bg_stencil_operation(rs[D3DRS_STENCILZFAIL]);
		unsigned pass = bg_stencil_operation(rs[D3DRS_STENCILPASS]);

		/* (writing no bits: every operation keeps) */
		if (!mask)
			fail = depth_fail = pass = 1;
		*stencil = ((uint32_t)bg_compare(rs[D3DRS_STENCILFUNC]) << BGFX_STENCIL_TEST_SHIFT) |
			BGFX_STENCIL_FUNC_REF(rs[D3DRS_STENCILREF] & 0xff) | BGFX_STENCIL_FUNC_RMASK(rs[D3DRS_STENCILMASK] & 0xff) |
			((uint32_t)fail << BGFX_STENCIL_OP_FAIL_S_SHIFT) | ((uint32_t)depth_fail << BGFX_STENCIL_OP_FAIL_Z_SHIFT) |
			((uint32_t)pass << BGFX_STENCIL_OP_PASS_Z_SHIFT);
		if (mask && mask != 0xff)
			*back_stencil = BGFX_STENCIL_FUNC_RMASK(mask);
	}
	if (rs[D3DRS_ALPHABLENDENABLE])
	{
		uint64_t source = bg_blend_factor(rs[D3DRS_SRCBLEND]);
		uint64_t destination = bg_blend_factor(rs[D3DRS_DESTBLEND]);

		state |= BGFX_STATE_BLEND_FUNC(source, destination) | BGFX_STATE_BLEND_EQUATION(bg_blend_operation(rs[D3DRS_BLENDOP]));
	}
	/* (D3DCOLOR's ARGB as bgfx's RGBA) */
	*rgba = (uint32_t)((rs[D3DRS_BLENDCOLOR] << 8) | (rs[D3DRS_BLENDCOLOR] >> 24));
	/* the cull mode names the winding to discard (GL_CW, GL_CCW), in the
	target's rows from the top, as Metal's are */
	if (rs[D3DRS_CULLMODE] == 0x900)
		state |= BGFX_STATE_CULL_CW;
	else if (rs[D3DRS_CULLMODE] == 0x901)
		state |= BGFX_STATE_CULL_CCW;
	/* (Metal rasterizes a multisampled target's samples anyway; Direct3D 11
	and OpenGL only with it) */
	if (bg.target_samples > 1)
		state |= BGFX_STATE_MSAA;
	if (rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME)
		bg_log_once("wireframe");
	return state;
}

/* ---------- views

bgfx draws a frame as views, in order: each has one frame buffer, rectangle
and clear. A view of its own starts at each change of target, at each clear
after draws, when the viewport moves (the hardware viewport must be
Direct3D's, since the vertex shaders compute their positions in it), and
after a draw of the renderer's own (bg_composite_copy). */

static void bg_view_set(bgfx_frame_buffer_handle_t framebuffer, const unsigned short rect[4], BOOL new_view)
{
	if (bg.view_used && (new_view || framebuffer.idx != bg.view_framebuffer.idx ||
		memcmp(rect, bg.view_rect, sizeof(bg.view_rect))))
	{
		if (bg.view + 2 < BG_MAXIMUM_VIEWS)
			bg.view++;
		else
			bg_log_once("more views than a frame has");
		bg.view_used = FALSE;
		bgfx_set_view_mode(bg.view, BGFX_VIEW_MODE_SEQUENTIAL);
		bgfx_set_view_clear(bg.view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
	}
	bg.view_framebuffer = framebuffer;
	memcpy(bg.view_rect, rect, sizeof(bg.view_rect));
	bgfx_set_view_frame_buffer(bg.view, framebuffer);
	bgfx_set_view_rect(bg.view, rect[0], rect[1], rect[2], rect[3], 0.0f, 1.0f);
}

/* ---------- render targets (render_target_get's) */

/* display.anti_aliasing's multisampling, as the OpenGL path's (its
render_target_multisample): the samples a pixel of the screen's targets, or
0. Such a target's bgfx texture is made with that many samples: bgfx draws
into a multisampled texture of its own and resolves it into the target's
texture at the end of each view, so what samples or copies the target reads
the resolved pixels. Every target at the screen's size is multisampled, the
back buffer, its depth buffer and what is drawn with them (a frame
buffer's attachments have the same samples). A change of the setting
between frames makes new targets (bg_target_get's key), cleared once as
any new one. FXAA, SMAA and supersampling are the OpenGL path's only. */
static int bg_screen_samples(void)
{
	static int unsupported;
	const bgfx_caps_t *caps;
	int samples;

	if (anti_aliasing() != _anti_aliasing_msaa)
		return 0;
	samples = anti_aliasing_values[anti_aliasing_value].samples;
	caps = bgfx_get_caps();
	/* (the most samples up to the setting's that both the color and the
	depth targets have: bgfx's bit for each count) */
	for (; samples >= 2; samples /= 2)
	{
		uint32_t bit = samples >= 16 ? BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X16 :
			samples >= 8 ? BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X8 :
			samples >= 4 ? BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X4 : BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X2;

		if ((caps->formats[BGFX_TEXTURE_FORMAT_BGRA8] & bit) && (caps->formats[bg.depth_format] & bit))
			break;
	}
	if (samples < 2)
	{
		if (!unsupported++)
			platform_log("anti-aliasing: bgfx's renderer has no multisampled targets, so none");
		return 0;
	}
	if (samples != anti_aliasing_values[anti_aliasing_value].samples && !unsupported++)
		platform_log("anti-aliasing: bgfx's renderer has %dx multisampling at most here", samples);
	return samples;
}

/* a target's texture flags; a multisampled depth buffer is write-only
(nothing samples it, and bgfx makes no frame buffer of one it would have to
resolve) */
static uint64_t bg_samples_flags(int samples, BOOL depth)
{
	uint64_t flags;

	switch (samples)
	{
	case 2: flags = BGFX_TEXTURE_RT_MSAA_X2; break;
	case 4: flags = BGFX_TEXTURE_RT_MSAA_X4; break;
	case 8: flags = BGFX_TEXTURE_RT_MSAA_X8; break;
	default: return BGFX_TEXTURE_RT;
	}
	return depth ? flags | BGFX_TEXTURE_RT_WRITE_ONLY : flags;
}

static struct bg_target **bg_target_bucket(DWORD data)
{
	return &bg.targets[((data >> 12) ^ (data >> 20)) % BG_TARGET_BUCKETS];
}

static struct bg_target *bg_target_get(const D3DSurface *surface)
{
	struct bg_target *entry;
	unsigned long width, height;
	float scale[2] = { 1.0f, 1.0f };
	BOOL depth;
	int samples = 0;

	if (!surface || !surface->Data)
		return NULL;
	surface_dimensions(surface, &width, &height, &depth);
	/* the screen's targets are drawn at the screen's scale, the secondary
	target at the 3D view's pixels with its samples (it is drawn with the
	back buffer's depth buffer, and a frame buffer is one size:
	render_target_scale), and the shadow maps at display.shadow_resolution's */
	if (width == (unsigned long)halo_screen_width() && height == SCREEN_HEIGHT)
	{
		scale[0] = screen_scale[0];
		scale[1] = screen_scale[1];
		samples = bg_screen_samples();
	}
	else if (!depth && width == SECONDARY_TARGET_WIDTH && height == SECONDARY_TARGET_HEIGHT)
	{
		render_target_scale(_render_target_secondary, scale);
		samples = bg_screen_samples();
	}
	else if (!depth && surface_is_shadow_map(surface))
	{
		scale[0] = (float)halo_shadow_map_scale();
		scale[1] = scale[0];
	}
	for (entry = *bg_target_bucket(surface->Data); entry; entry = entry->next)
	{
		/* (one bgfx would not make is kept, so it is asked for once, and is
		no target: what would draw into it is left out) */
		if (entry->data == surface->Data && entry->width == width && entry->height == height && entry->depth == depth &&
			entry->scale[0] == scale[0] && entry->scale[1] == scale[1] && entry->samples == samples)
			return BGFX_HANDLE_IS_VALID(entry->texture) ? entry : NULL;
	}
	entry = calloc(1, sizeof(*entry));
	entry->data = surface->Data;
	entry->width = width;
	entry->height = height;
	entry->depth = depth;
	entry->scale[0] = scale[0];
	entry->scale[1] = scale[1];
	entry->samples = samples;
	entry->pixel_width = (unsigned long)(width * scale[0] + 0.5f);
	entry->pixel_height = (unsigned long)(height * scale[1] + 0.5f);
	entry->texture = bgfx_create_texture_2d((uint16_t)entry->pixel_width, (uint16_t)entry->pixel_height, false, 1,
		depth ? bg.depth_format : BGFX_TEXTURE_FORMAT_BGRA8,
		bg_samples_flags(samples, depth) | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, NULL, 0);
	/* (cleared once: what a new target holds is undefined) */
	entry->fresh = TRUE;
	entry->next = *bg_target_bucket(entry->data);
	*bg_target_bucket(entry->data) = entry;
	if (!BGFX_HANDLE_IS_VALID(entry->texture))
	{
		platform_log("bgfx: a %lux%lu %s target (%d samples) cannot be made, so it is not drawn", entry->pixel_width,
			entry->pixel_height, depth ? "depth" : "color", samples);
		return NULL;
	}
	return entry;
}

/* the back buffer's size in the game's units and in pixels, for the menus'
pointer (ui_point_view_get); FALSE before there is one */
static BOOL bg_back_buffer_size(int *width, int *height, int *pixel_width, int *pixel_height)
{
	struct bg_target *back_buffer = bg.ready ? bg_target_get(&device.back_buffer) : NULL;

	if (!back_buffer)
		return FALSE;
	*width = (int)back_buffer->width;
	*height = (int)back_buffer->height;
	*pixel_width = (int)back_buffer->pixel_width;
	*pixel_height = (int)back_buffer->pixel_height;
	return TRUE;
}

/* the color target last drawn into at this address, or NULL */
static struct bg_target *bg_target_find(DWORD data)
{
	struct bg_target *entry, *best = NULL;

	for (entry = *bg_target_bucket(data); entry; entry = entry->next)
	{
		if (entry->data == data && !entry->depth && entry->last_rendered &&
			(!best || entry->last_rendered > best->last_rendered))
			best = entry;
	}
	return best;
}

static bgfx_frame_buffer_handle_t bg_framebuffer_get(struct bg_target *color, struct bg_target *depth)
{
	uint16_t color_index = color ? color->texture.idx : UINT16_MAX, depth_index = depth ? depth->texture.idx : UINT16_MAX;
	struct bg_framebuffer *entry;
	bgfx_texture_handle_t attachments[2];
	uint8_t count = 0;

	for (entry = bg.framebuffers; entry; entry = entry->next)
	{
		if (entry->color == color_index && entry->depth == depth_index)
			return entry->handle;
	}
	/* (bg_target_get gives only targets with a texture; one without is
	left out all the same, as bgfx reads its texture by the handle) */
	if (color && BGFX_HANDLE_IS_VALID(color->texture))
		attachments[count++] = color->texture;
	if (depth && BGFX_HANDLE_IS_VALID(depth->texture))
		attachments[count++] = depth->texture;
	if (!count)
		return (bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE;
	entry = calloc(1, sizeof(*entry));
	entry->color = color_index;
	entry->depth = depth_index;
	entry->handle = bgfx_create_frame_buffer_from_handles(count, attachments, false);
	if (!BGFX_HANDLE_IS_VALID(entry->handle))
		bg_log_once("a frame buffer bgfx would not make");
	entry->next = bg.framebuffers;
	bg.framebuffers = entry;
	return entry->handle;
}

/* the targets' frame buffer bound (bind_targets'); FALSE if there is
nothing to draw into */
static BOOL bg_bind_targets(BOOL *has_depth)
{
	struct bg_target *color = bg_target_get(device.render_target);
	struct bg_target *depth = bg_target_get(device.depth_stencil);

	if (depth && !depth->depth)
		depth = NULL;
	if (color && depth && (color->pixel_width != depth->pixel_width || color->pixel_height != depth->pixel_height))
	{
		/* (bgfx's frame buffers are one size) */
		bg_log_once("a depth buffer of another size than its target");
		depth = NULL;
	}
	if (color && depth && color->samples != depth->samples)
	{
		/* (nor of other samples) */
		bg_log_once("a depth buffer of other samples than its target");
		depth = NULL;
	}
	if (!color && !depth)
		return FALSE;
	if (color)
	{
		color->last_rendered = device.frame + 1;
		color->written = ++bg.write_serial;
	}
	bg.target_color = color;
	bg.target_depth = depth;
	bg.target_scale[0] = color ? color->scale[0] : depth->scale[0];
	bg.target_scale[1] = color ? color->scale[1] : depth->scale[1];
	bg.target_width = color ? color->pixel_width : depth->pixel_width;
	bg.target_height = color ? color->pixel_height : depth->pixel_height;
	bg.target_samples = color ? color->samples : depth->samples;
	bg.framebuffer = bg_framebuffer_get(color, depth);
	/* (one bgfx would not make: nothing is drawn, rather than into the
	window) */
	if (!BGFX_HANDLE_IS_VALID(bg.framebuffer))
		return FALSE;
	if ((color && color->fresh) || (depth && depth->fresh))
	{
		unsigned short rect[4] = { 0, 0, (unsigned short)bg.target_width, (unsigned short)bg.target_height };
		uint16_t clear = (uint16_t)((color && color->fresh ? BGFX_CLEAR_COLOR : 0) |
			(depth && depth->fresh ? BGFX_CLEAR_DEPTH | (bg.depth_stencil ? BGFX_CLEAR_STENCIL : 0) : 0));

		bg_view_set(bg.framebuffer, rect, TRUE);
		bgfx_set_view_clear(bg.view, clear, 0x000000ff, 1.0f, 0);
		bgfx_touch(bg.view);
		bg.view_used = TRUE;
		if (color)
			color->fresh = FALSE;
		if (depth)
			depth->fresh = FALSE;
	}
	*has_depth = depth != NULL;
	return TRUE;
}

/* a rectangle of the bound targets' units in their pixels, clipped to them */
static BOOL bg_target_rect(float x, float y, float width, float height, unsigned short rect[4])
{
	long x0 = scaled_pixel(x, bg.target_scale[0]);
	long y0 = scaled_pixel(y, bg.target_scale[1]);
	long x1 = scaled_pixel(x + width, bg.target_scale[0]);
	long y1 = scaled_pixel(y + height, bg.target_scale[1]);

	x0 = x0 < 0 ? 0 : x0;
	y0 = y0 < 0 ? 0 : y0;
	x1 = x1 > (long)bg.target_width ? (long)bg.target_width : x1;
	y1 = y1 > (long)bg.target_height ? (long)bg.target_height : y1;
	if (x1 <= x0 || y1 <= y0)
		return FALSE;
	rect[0] = (unsigned short)x0;
	rect[1] = (unsigned short)y0;
	rect[2] = (unsigned short)(x1 - x0);
	rect[3] = (unsigned short)(y1 - y0);
	return TRUE;
}

/* the levels of a texture the game renders one at a time, in one mipmapped
texture (mip_composite_get's), a render target drawn into level by level
before the draw that samples it sets any state (bg_composite_fill). The
levels the game did not render come from the last one it did, each made in
a target of its own first; the OpenGL path has the driver make them. (A
bgfx blit into the texture is not used: on Metal, a draw in the same frame
sampled the texture as it was before the blit.) The target of the first
level alone when the texture cannot be made, or filling it failed. */
static struct bg_composite *bg_composite_find(const struct xgpu_texture_description *description, DWORD data,
	struct bg_target *first, struct bg_target *targets[BG_COMPOSITE_LEVELS], unsigned long *full,
	unsigned long *rendered)
{
	struct bg_composite *composite;
	unsigned long level, size;

	*full = 1;
	*rendered = 0;
	for (size = description->width > description->height ? description->width : description->height; size > 1; size >>= 1)
		(*full)++;
	if (*full > BG_COMPOSITE_LEVELS || description->levels < 2)
		return NULL;
	for (level = 0; level < *full; level++)
	{
		unsigned long width = description->width >> level ? description->width >> level : 1;
		unsigned long height = description->height >> level ? description->height >> level : 1;

		targets[level] = level ? bg_target_find(data + xgpu_texture_level_offset(description, level)) : first;
		if (!targets[level] || targets[level]->width != width || targets[level]->height != height ||
			targets[level]->pixel_width != width || targets[level]->pixel_height != height)
			break;
	}
	*rendered = level;
	if (!*rendered)
		return NULL;
	for (composite = bg.composites; composite; composite = composite->next)
	{
		if (composite->data == data && composite->width == description->width && composite->height == description->height)
			break;
	}
	if (!composite)
	{
		composite = calloc(1, sizeof(*composite));
		composite->data = data;
		composite->width = description->width;
		composite->height = description->height;
		composite->levels = *full;
		composite->texture = bgfx_create_texture_2d((uint16_t)description->width, (uint16_t)description->height, true, 1,
			BGFX_TEXTURE_FORMAT_BGRA8, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, NULL, 0);
		for (level = 0; level < BG_COMPOSITE_LEVELS; level++)
		{
			composite->scratch[level] = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
			composite->scratch_framebuffer[level] = (bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE;
			composite->level_framebuffer[level] = (bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE;
		}
		composite->next = bg.composites;
		bg.composites = composite;
	}
	/* (one bgfx would not make: the top level alone, as a target without
	its levels above) */
	return BGFX_HANDLE_IS_VALID(composite->texture) ? composite : NULL;
}

/* a texture drawn over a frame buffer of width by height (linear samples at
the pixels' centers: at half the source's size, the box filter), in a view
of its own, the draw's state untouched (nothing of it is set yet); FALSE if
bgfx has no room for it */
static BOOL bg_composite_copy(bgfx_frame_buffer_handle_t framebuffer, unsigned long width, unsigned long height,
	bgfx_texture_handle_t source)
{
	if (!BGFX_HANDLE_IS_VALID(bg.copy) || !BGFX_HANDLE_IS_VALID(bg.full_quad) || !BGFX_HANDLE_IS_VALID(framebuffer) ||
		!BGFX_HANDLE_IS_VALID(source))
		return FALSE;
	if (bg.view_used)
	{
		if (bg.view + 2 >= BG_MAXIMUM_VIEWS)
		{
			bg_log_once("more views than a frame has");
			return FALSE;
		}
		bg.view++;
		bg.view_used = FALSE;
	}
	bgfx_set_view_mode(bg.view, BGFX_VIEW_MODE_SEQUENTIAL);
	bgfx_set_view_clear(bg.view, BGFX_CLEAR_NONE, 0, 1.0f, 0);
	bgfx_set_view_frame_buffer(bg.view, framebuffer);
	bgfx_set_view_rect(bg.view, 0, 0, (uint16_t)width, (uint16_t)height, 0.0f, 1.0f);
	/* (the next draw's view is a new one: bg_view_set) */
	bg.view_framebuffer = framebuffer;
	bgfx_set_vertex_buffer(0, bg.full_quad, 0, 4);
	bgfx_set_texture(0, bg.samplers[0], source, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_PT_TRISTRIP, 0);
	bgfx_submit(bg.view, bg.copy, 0, BGFX_DISCARD_ALL);
	bg.view_used = TRUE;
	return TRUE;
}

/* the frame buffer of one level of the composite, made the first time */
static bgfx_frame_buffer_handle_t bg_composite_level_framebuffer(struct bg_composite *composite, unsigned long level)
{
	if (!BGFX_HANDLE_IS_VALID(composite->level_framebuffer[level]))
	{
		bgfx_attachment_t attachment;

		bgfx_attachment_init(&attachment, composite->texture, BGFX_ACCESS_WRITE, 0, 1, (uint16_t)level, 0);
		composite->level_framebuffer[level] = bgfx_create_frame_buffer_from_attachment(1, &attachment, false);
	}
	return composite->level_framebuffer[level];
}

/* a level below the game's made of the one above it, in a target of its own
(the composite's own levels are not sampled while one of them is drawn) */
static bgfx_texture_handle_t bg_composite_scratch(struct bg_composite *composite, unsigned long level,
	bgfx_texture_handle_t above)
{
	unsigned long width = composite->width >> level ? composite->width >> level : 1;
	unsigned long height = composite->height >> level ? composite->height >> level : 1;

	if (!BGFX_HANDLE_IS_VALID(composite->scratch[level]))
	{
		composite->scratch[level] = bgfx_create_texture_2d((uint16_t)width, (uint16_t)height, false, 1,
			BGFX_TEXTURE_FORMAT_BGRA8, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, NULL, 0);
		if (BGFX_HANDLE_IS_VALID(composite->scratch[level]))
			composite->scratch_framebuffer[level] = bgfx_create_frame_buffer_from_handles(1, &composite->scratch[level], false);
	}
	if (!bg_composite_copy(composite->scratch_framebuffer[level], width, height, above))
		return (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
	return composite->scratch[level];
}

/* the composite of a render target texture with levels brought up to date,
before a draw that samples it sets any state: the game's levels copied, the
rest drawn (bg_composite_downsample) and copied */
static void bg_composite_fill(const struct xgpu_texture_description *description, DWORD data, struct bg_target *first)
{
	struct bg_target *targets[BG_COMPOSITE_LEVELS];
	struct bg_composite *composite;
	unsigned long level, full, rendered;
	BOOL changed = FALSE;

	composite = bg_composite_find(description, data, first, targets, &full, &rendered);
	if (!composite)
		return;
	/* (again only once a level was drawn into since) */
	for (level = 0; level < rendered; level++)
	{
		if (composite->sources[level] != targets[level]->texture.idx || composite->written[level] != targets[level]->written)
			changed = TRUE;
	}
	if (!changed && rendered == composite->rendered && composite->filled)
	{
		composite->filled_frame = bg.frame;
		return;
	}
	composite->filled_frame = bg.frame;
	composite->filled = FALSE;
	composite->rendered = rendered;
	for (level = 0; level < full; level++)
	{
		unsigned long width = composite->width >> level ? composite->width >> level : 1;
		unsigned long height = composite->height >> level ? composite->height >> level : 1;
		bgfx_texture_handle_t source;

		if (level < rendered)
		{
			composite->sources[level] = targets[level]->texture.idx;
			composite->written[level] = targets[level]->written;
			source = targets[level]->texture;
		}
		else
		{
			source = bg_composite_scratch(composite, level,
				level - 1 < rendered ? targets[level - 1]->texture : composite->scratch[level - 1]);
		}
		if (!bg_composite_copy(bg_composite_level_framebuffer(composite, level), width, height, source))
			return;
	}
	composite->filled = TRUE;
}

/* the stages' render target textures with levels, filled before the draw
(bg_bind_textures' conditions) */
static void bg_composites_prepare(void)
{
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		D3DBaseTexture *texture = device.textures[stage];
		unsigned long mode = stage_texture_mode(stage);
		struct xgpu_texture_description description;
		struct bg_target *target;

		if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
			continue;
		xgpu_texture_describe(texture->Format, texture->Size, &description);
		if (description.linear || description.cube_map || description.levels < 2)
			continue;
		target = bg_target_find(texture->Data);
		if (target && target->width == description.width && target->height == description.height)
			bg_composite_fill(&description, texture->Data, target);
	}
}

static bgfx_texture_handle_t bg_composite_get(const struct xgpu_texture_description *description, DWORD data,
	struct bg_target *first, BOOL *mipmapped)
{
	struct bg_target *targets[BG_COMPOSITE_LEVELS];
	struct bg_composite *composite;
	unsigned long full, rendered;

	*mipmapped = FALSE;
	composite = bg_composite_find(description, data, first, targets, &full, &rendered);
	if (!composite || !composite->filled || composite->filled_frame != bg.frame)
		return first->texture;
	*mipmapped = TRUE;
	return composite->texture;
}

/* ---------- textures */

static unsigned long bg_palette_hash(const D3DCOLOR *palette)
{
	return palette ? (unsigned long)bg_hash(palette, 256 * sizeof(D3DCOLOR), 14695981039346656037ULL) | 1 : 0;
}

static unsigned long bg_level_dimension(unsigned long base, unsigned long level)
{
	unsigned long value = base >> level;

	return value ? value : 1;
}

/* a level of 32-bit texels box-filtered to half its size (or the source's
smaller dimension kept at 1) */
static void bg_downsample(const unsigned char *source, unsigned long width, unsigned long height, unsigned char *out)
{
	unsigned long out_width = width > 1 ? width / 2 : 1, out_height = height > 1 ? height / 2 : 1, x, y, channel;

	for (y = 0; y < out_height; y++)
	{
		for (x = 0; x < out_width; x++)
		{
			unsigned long x0 = x * 2 < width ? x * 2 : width - 1, x1 = x * 2 + 1 < width ? x * 2 + 1 : x0;
			unsigned long y0 = y * 2 < height ? y * 2 : height - 1, y1 = y * 2 + 1 < height ? y * 2 + 1 : y0;

			for (channel = 0; channel < 4; channel++)
				out[(y * out_width + x) * 4 + channel] = (unsigned char)((source[(y0 * width + x0) * 4 + channel] +
					source[(y0 * width + x1) * 4 + channel] + source[(y1 * width + x0) * 4 + channel] +
					source[(y1 * width + x1) * 4 + channel] + 2) / 4);
		}
	}
}

static void bg_texture_upload(struct bg_texture *entry, const unsigned char *base, const D3DCOLOR *palette)
{
	struct xgpu_texture_description *description = &entry->description;
	int dxt = description->compressed ? xgpu_texture_dxt(description) : 0;
	unsigned long levels = description->levels ? description->levels : 1, level, full = 1, size, face, provided;
	BOOL volume = description->depth > 1;

	if (description->compressed && !dxt)
	{
		bg_log_once("compressed textures other than DXT");
		entry->failed = TRUE;
		return;
	}
	/* bgfx's mipmapped textures have every level down to 1x1 (it has no
	maximum level); the levels a texture lacks are made from its last */
	size = description->width > description->height ? description->width : description->height;
	if (volume && description->depth > size)
		size = description->depth;
	for (; size > 1; size >>= 1)
		full++;
	provided = levels;
	if (levels > 1)
		levels = full;
	if (!entry->created)
	{
		bgfx_texture_format_t format = dxt == 1 ? BGFX_TEXTURE_FORMAT_BC1 : dxt == 3 ? BGFX_TEXTURE_FORMAT_BC2 :
			dxt == 5 ? BGFX_TEXTURE_FORMAT_BC3 : BGFX_TEXTURE_FORMAT_BGRA8;

		if (volume)
			entry->texture = bgfx_create_texture_3d((uint16_t)description->width, (uint16_t)description->height,
				(uint16_t)description->depth, levels > 1, format, BGFX_TEXTURE_NONE, NULL, 0);
		else if (description->cube_map)
			entry->texture = bgfx_create_texture_cube((uint16_t)description->width, levels > 1, 1, format,
				BGFX_TEXTURE_NONE, NULL, 0);
		else
			entry->texture = bgfx_create_texture_2d((uint16_t)description->width, (uint16_t)description->height,
				levels > 1, 1, format, BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE, NULL, 0);
		if (!BGFX_HANDLE_IS_VALID(entry->texture))
		{
			platform_log("bgfx: a %lux%lu texture cannot be made", description->width, description->height);
			entry->failed = TRUE;
			return;
		}
		entry->created = TRUE;
		entry->mipmapped = levels > 1;
	}
	for (face = 0; face < (description->cube_map ? 6UL : 1UL); face++)
	for (level = 0; level < (entry->mipmapped ? levels : 1); level++)
	{
		unsigned long width = bg_level_dimension(description->width, level);
		unsigned long height = bg_level_dimension(description->height, level);
		unsigned long depth = volume ? bg_level_dimension(description->depth, level) : 1;
		unsigned long from = level < provided ? level : provided - 1;
		const unsigned char *source = base + face * xgpu_texture_face_size(description) +
			xgpu_texture_level_offset(description, from);
		const bgfx_memory_t *memory;

		/* (Halo PC's cube map: its levels one after another, each the six
		faces', the second and third swapped; xbox_textures.c's upload) */
		if (description->pc_layout && description->cube_map)
			source = base + xgpu_texture_level_offset(description, from) * 6 +
				(face == 1 ? 2 : face == 2 ? 1 : face) * xgpu_texture_level_bytes(description, from);

		if (dxt)
		{
			/* (a missing level: the last one's first blocks, as many as it has) */
			unsigned long blocks = ((width + 3) / 4) * ((height + 3) / 4) * depth * (dxt == 1 ? 8 : 16);
			unsigned long available = xgpu_texture_level_bytes(description, from);

			memory = bgfx_copy(source, (uint32_t)(level == from ? available : blocks < available ? blocks : available));
		}
		else if (level == from)
		{
			memory = bgfx_alloc((uint32_t)(width * height * depth * 4));
			xgpu_texture_decode_level(description, level, source, palette, (unsigned long *)memory->data);
		}
		else
		{
			/* (a missing level: the last one, point sampled down) */
			unsigned long from_width = bg_level_dimension(description->width, from);
			unsigned long from_height = bg_level_dimension(description->height, from);
			unsigned long from_depth = volume ? bg_level_dimension(description->depth, from) : 1;
			unsigned long *decoded = malloc(from_width * from_height * from_depth * 4), x, y, z;

			memory = bgfx_alloc((uint32_t)(width * height * depth * 4));
			if (decoded && xgpu_texture_decode_level(description, from, source, palette, decoded))
			{
				for (z = 0; z < depth; z++)
				for (y = 0; y < height; y++)
				for (x = 0; x < width; x++)
					((unsigned int *)memory->data)[(z * height + y) * width + x] = (unsigned int)decoded[
						((z * from_depth / depth) * from_height + y * from_height / height) * from_width + x * from_width / width];
			}
			else
			{
				memset(memory->data, 0, width * height * depth * 4);
			}
			free(decoded);
		}
		if (volume)
			bgfx_update_texture_3d(entry->texture, (uint8_t)level, 0, 0, 0, (uint16_t)width, (uint16_t)height,
				(uint16_t)depth, memory);
		else if (description->cube_map)
			bgfx_update_texture_cube(entry->texture, 0, (uint8_t)face, (uint8_t)level, 0, 0, (uint16_t)width,
				(uint16_t)height, memory, UINT16_MAX);
		else
			bgfx_update_texture_2d(entry->texture, 0, (uint8_t)level, 0, 0, (uint16_t)width, (uint16_t)height, memory,
				UINT16_MAX);
	}
}

/* ---------- MCC maps' textures (mcc_texture_bridge.c): their pixels are
immutable allocations outside the Xbox's memory, behind headers whose Data
names nothing, so they are found by the header itself and uploaded once (for
each palette a paletted one is drawn with) */

#if !defined(HALO_MCC) || HALO_MCC
#define BG_MCC_TEXTURE_BUCKETS 512

struct bg_mcc_texture
{
	struct bg_mcc_texture *next;
	const DWORD *resource;
	unsigned long palette_hash;
	struct bg_texture texture;
};

static struct bg_mcc_texture *bg_mcc_textures[BG_MCC_TEXTURE_BUCKETS];

static unsigned long bg_mcc_bucket(const DWORD *resource)
{
	return (unsigned long)((((uintptr_t)resource >> 4) * 2654435761UL) % BG_MCC_TEXTURE_BUCKETS);
}

/* an MCC header's texture, as bg_texture_get's (override: its high-res HUD
stand-in, or -1); -1 if resource is not an MCC texture's header */
static int bg_mcc_texture_get(const DWORD *resource, const D3DCOLOR *palette,
	struct xgpu_texture_description *description, bgfx_texture_handle_t *handle, BOOL *mipmapped, long *override)
{
	struct xgpu_texture_description source;
	const void *pixels;
	unsigned long bytes, hash;
	long hud_asset;
	struct bg_mcc_texture *entry;

	if (!xgpu_mcc_texture_source(resource, &source, &pixels, &bytes, &hud_asset))
		return -1;
	*description = source;
	*override = -1;
	if (hud_asset >= 0 && config_boolean("display.high_res_hud"))
	{
		unsigned long levels;

		if (hud_hires_override_texture(hud_asset, &levels))
		{
			*override = hud_asset;
			return TRUE;
		}
	}
	hash = source.format == 0x0b ? bg_palette_hash(palette) : 0;
	for (entry = bg_mcc_textures[bg_mcc_bucket(resource)]; entry; entry = entry->next)
	{
		if (entry->resource == resource && entry->palette_hash == hash)
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
		{
			xgpu_mcc_texture_failed(&source, "no memory for the bgfx renderer's copy");
			return FALSE;
		}
		entry->resource = resource;
		entry->palette_hash = hash;
		entry->texture.description = source;
		entry->texture.override = -1;
		entry->next = bg_mcc_textures[bg_mcc_bucket(resource)];
		bg_mcc_textures[bg_mcc_bucket(resource)] = entry;
		bg_texture_upload(&entry->texture, (const unsigned char *)pixels, palette);
		if (entry->texture.failed || !entry->texture.created)
		{
			entry->texture.failed = TRUE;
			xgpu_mcc_texture_failed(&source, "the bgfx renderer could not make it");
		}
	}
	if (entry->texture.failed)
		return FALSE;
	*handle = entry->texture.texture;
	*mipmapped = entry->texture.mipmapped;
	return TRUE;
}

/* an MCC header going with its map (xgpu_mcc_texture_unbind): its copies */
static void bg_mcc_texture_forget(const DWORD *resource)
{
	struct bg_mcc_texture **link = &bg_mcc_textures[bg_mcc_bucket(resource)];

	while (*link)
	{
		struct bg_mcc_texture *entry = *link;

		if (entry->resource != resource)
		{
			link = &entry->next;
			continue;
		}
		*link = entry->next;
		if (entry->texture.created && bg.ready)
			bgfx_destroy_texture(entry->texture.texture);
		free(entry);
	}
}
#else
#define bg_mcc_texture_get(a0, a1, a2, a3, a4, a5) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (void)sizeof(a5), -1)
static void bg_mcc_texture_forget(const DWORD *resource)
{
	(void)resource;
}
#endif

/* a stage's texture (xgpu_texture_get's), uploaded again when its pages have
been written; FALSE if it cannot be drawn yet */
static BOOL bg_texture_get(const DWORD *resource, const D3DCOLOR *palette, struct xgpu_texture_description *description,
	bgfx_texture_handle_t *handle, BOOL *mipmapped, long *override)
{
	DWORD data = resource[1], format_word = resource[3], size_word = resource[4];
	BOOL palettized = ((format_word & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == 0x0b;
	unsigned long hash = bg_palette_hash(palette);
	unsigned long bucket = (unsigned long)(bg_hash(&data, sizeof(data), bg_hash(&format_word, sizeof(format_word),
		bg_hash(&size_word, sizeof(size_word), 14695981039346656037ULL))) % BG_TEXTURE_BUCKETS);
	struct bg_texture *entry;
	unsigned long generation;

	*override = -1;
	for (entry = bg.textures[bucket]; entry; entry = entry->next)
	{
		if (entry->data == data && entry->format_word == format_word && entry->size_word == size_word &&
			entry->palette_hash == hash)
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		entry->data = data;
		entry->format_word = format_word;
		entry->size_word = size_word;
		entry->palette_hash = hash;
		entry->override = -1;
		xgpu_texture_describe(format_word, size_word, &entry->description);
#ifdef HALO_64BIT
		entry->address = (unsigned int)data | PLATFORM_CONTIGUOUS_BASE; /* an Xbox address */
#else
		entry->address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
#endif
		entry->size = xgpu_texture_face_size(&entry->description) * (entry->description.cube_map ? 6 : 1);
		entry->next = bg.textures[bucket];
		bg.textures[bucket] = entry;
	}
	*description = entry->description;
	if (entry->failed || !entry->size)
		return FALSE;
	generation = memory_watch_generation(entry->address, entry->size);
	if (!entry->generation || generation > entry->generation || entry->override_settings != hud_hires_settings())
	{
		entry->description.pc_layout = (resource[0] & D3DCOMMON_PORT_PC_LAYOUT) != 0;
		entry->description.pc_meter = (resource[0] & D3DCOMMON_PORT_PC_METER) != 0;
		entry->description.pc_multipurpose = (resource[0] & D3DCOMMON_PORT_PC_MULTIPURPOSE) != 0;
		memory_watch_protect(entry->address, entry->size);
		entry->generation = memory_watch_generation(entry->address, entry->size);
		if (!entry->generation)
			entry->generation = 1;
		/* (which bitmap is here may have changed with the pixels, and
		whether it has a high-res stand-in with the settings) */
		entry->override = -1;
		entry->override_settings = hud_hires_settings();
		if (!palettized && !entry->description.cube_map && entry->description.depth == 1)
		{
			unsigned long levels;

			entry->override = hud_hires_override_find(entry->address, entry->description.width,
				entry->description.height, entry->description.levels > 1 ?
				xgpu_texture_level_offset(&entry->description, 1) : xgpu_texture_face_size(&entry->description));
			if (entry->override >= 0 && !hud_hires_override_texture(entry->override, &levels))
				entry->override = -1;
		}
		if (entry->override < 0)
		{
			if (!platform_is_contiguous(xbox_pointer(entry->address)) ||
				!platform_is_contiguous(xbox_pointer(entry->address + entry->size - 1)))
				return FALSE;
			/* (drawn with already this frame: a new texture, so the draws
			before keep the old pixels, as they did on the Xbox) */
			if (entry->created && entry->used_frame == bg.frame)
			{
				bgfx_destroy_texture(entry->texture);
				entry->created = FALSE;
			}
		{
			double start = stats_seconds();

			bg_texture_upload(entry, (const unsigned char *)xbox_pointer(entry->address), palette);
			stats_frame.texture_seconds += stats_seconds() - start;
		}
		}
		*description = entry->description;
	}
	*override = entry->override;
	if (entry->override >= 0)
		return TRUE;
	if (entry->failed || !entry->created)
		return FALSE;
	entry->used_frame = bg.frame;
	*handle = entry->texture;
	*mipmapped = entry->mipmapped;
	return TRUE;
}

/* flat: a 2D texture (not a cube or 3D one), the only kind given the
anisotropy display.anisotropic_filtering forces, on the same terms as the
OpenGL path's configure_sampler: mipmapped and filtered. bgfx's level is
16x (bg_reset_flags) whatever the setting's number. */
static uint32_t bg_sampler_flags(int stage, BOOL mipmapped, BOOL hires, BOOL flat)
{
	DWORD *state = D3D__TextureState[stage];
	uint32_t flags = 0;
	int axis;

	if (!hires)
	{
		if (state[D3DTSS_MINFILTER] == D3DTEXF_POINT)
			flags |= BGFX_SAMPLER_MIN_POINT;
		else if (state[D3DTSS_MINFILTER] == D3DTEXF_ANISOTROPIC && state[D3DTSS_MAXANISOTROPY] > 1)
			flags |= BGFX_SAMPLER_MIN_ANISOTROPIC;
		else if (flat && mipmapped && state[D3DTSS_MIPFILTER] != D3DTEXF_NONE && (bg.reset_flags & BGFX_RESET_MAXANISOTROPY))
		{
			/* (a point magnification keeps its own flag: the two together
			name no entry of bgfx's filter table) */
			flags |= BGFX_SAMPLER_MIN_ANISOTROPIC;
			if (state[D3DTSS_MAGFILTER] != D3DTEXF_POINT)
				flags |= BGFX_SAMPLER_MAG_ANISOTROPIC;
		}
		if (state[D3DTSS_MAGFILTER] == D3DTEXF_POINT)
			flags |= BGFX_SAMPLER_MAG_POINT;
	}
	if (!mipmapped || (!hires && state[D3DTSS_MIPFILTER] != D3DTEXF_LINEAR))
		flags |= BGFX_SAMPLER_MIP_POINT;
	for (axis = 0; axis < 3; axis++)
	{
		DWORD mode = state[axis == 0 ? D3DTSS_ADDRESSU : axis == 1 ? D3DTSS_ADDRESSV : D3DTSS_ADDRESSW];
		uint32_t address = mode == D3DTADDRESS_MIRROR ? BGFX_SAMPLER_U_MIRROR :
			mode == D3DTADDRESS_WRAP ? 0 : mode == D3DTADDRESS_BORDER ? BGFX_SAMPLER_U_BORDER : BGFX_SAMPLER_U_CLAMP;

		flags |= address << (axis * 2);
	}
	/* the border color: the nearest of the three Metal has (bg_init's
	palette colors 0 to 2 for the other renderers) */
	{
		DWORD border = state[D3DTSS_BORDERCOLOR];
		unsigned long luminance = ((border >> 16) & 0xff) + ((border >> 8) & 0xff) + (border & 0xff);

		flags |= BGFX_SAMPLER_BORDER_COLOR((border >> 24) < 0x80 ? 0 : luminance >= 3 * 0x80 ? 2 : 1);
	}
	return flags;
}

static bgfx_texture_handle_t bg_texture_named(unsigned int name)
{
	bgfx_texture_handle_t handle;

	handle.idx = (uint16_t)(name - 1);
	return handle;
}

/* the stages' textures, and the pixel shader key's part of them
(bind_textures'). They are set on the draw by bg_apply_textures, last thing
before its submit: bgfx keeps what is set only until the next submit, and
binding the targets (a fresh one's clear) or starting a view (its blits)
touches the view, a submit of its own, which dropped them. The draw then
went out with the samplers its shader reads unbound: nothing on NVIDIA,
whose unwritten descriptors read as empty, but a fault on AMD's and Mesa's
drivers, which read whatever the descriptor memory held */
static struct
{
	bgfx_texture_handle_t handle;
	uint32_t flags;
} bg_stage_textures[D3DTSS_MAXSTAGES];

static void bg_apply_textures(void)
{
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		bgfx_set_texture((uint8_t)stage, bg.samplers[stage], bg_stage_textures[stage].handle, bg_stage_textures[stage].flags);
}

static void bg_bind_textures(struct nv2a_pixel_shader_key *key, float texture_scale[4][4])
{
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		D3DBaseTexture *texture = device.textures[stage];
		unsigned long mode = stage_texture_mode(stage);
		struct xgpu_texture_description description;
		bgfx_texture_handle_t handle = bg.blank;
		struct bg_target *target;
		BOOL mipmapped = FALSE, hires = FALSE;

		texture_scale[stage][0] = texture_scale[stage][1] = 1.0f;
		texture_scale[stage][2] = texture_scale[stage][3] = 1.0f;
		if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
		{
			key->sampler_type[stage] = mode == 0x11 ? _xgpu_sampler_2d : _xgpu_sampler_none;
			bg_stage_textures[stage].handle = bg.blank;
			bg_stage_textures[stage].flags = UINT32_MAX;
			continue;
		}
		xgpu_texture_describe(texture->Format, texture->Size, &description);
		key->sampler_type[stage] = description.cube_map ? _xgpu_sampler_cube :
			description.depth > 1 ? _xgpu_sampler_3d : _xgpu_sampler_2d;
		if ((target = bg_target_find(texture->Data)) != NULL)
		{
			/* a render target, as it was last drawn */
			key->sampler_type[stage] = _xgpu_sampler_2d;
			handle = target->texture;
			if (description.linear)
			{
				texture_scale[stage][0] = 1.0f / (float)target->width;
				texture_scale[stage][1] = 1.0f / (float)target->height;
			}
			if (!description.linear && !description.cube_map && description.levels > 1 &&
				target->width == description.width && target->height == description.height)
				handle = bg_composite_get(&description, texture->Data, target, &mipmapped);
		}
		else
		{
			unsigned int name;
			unsigned long levels = 1;

			/* (the high-res text's atlas for its placeholder bitmap, and a
			menu's art: text_hires.h, menu_files.h) */
			if ((name = text_hires_atlas_texture(texture->Data)) != 0)
			{
				handle = bg_texture_named(name);
			}
			else if ((name = menu_art_texture(texture->Data, &levels)) != 0)
			{
				handle = bg_texture_named(name);
				mipmapped = levels > 1;
				hires = TRUE;
			}
			else
			{
				const D3DCOLOR *palette = device.palettes[stage] && device.palettes[stage]->Data ?
					(const D3DCOLOR *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.palettes[stage]->Data) : NULL;
				long override;
				int mcc = bg_mcc_texture_get((const DWORD *)texture, palette, &description, &handle, &mipmapped,
					&override);

				if (mcc >= 0)
				{
					/* (an MCC map's: its kind from its own description, and a
					converted meter's coverage, as d3d8_gl.c's bind_textures) */
					key->sampler_type[stage] = description.cube_map ? _xgpu_sampler_cube :
						description.depth > 1 ? _xgpu_sampler_3d : _xgpu_sampler_2d;
					hires = description.hires;
					if (stage == 0)
					{
						key->coverage_alpha = description.hires_coverage != FALSE;
						key->point_threshold = description.hires_point_threshold != FALSE;
					}
				}
				if (mcc == 0 || (mcc < 0 &&
					!bg_texture_get((const DWORD *)texture, palette, &description, &handle, &mipmapped, &override)))
					handle = description.cube_map ? bg.blank_cube : description.depth > 1 ? bg.blank_volume : bg.blank;
				else if (override >= 0)
				{
					/* (a high-res HUD texture in the bitmap's place: hud_hires.h) */
					name = hud_hires_override_texture(override, &levels);
					handle = name ? bg_texture_named(name) : bg.blank;
					mipmapped = levels > 1;
					hires = TRUE;
					if (stage == 0)
					{
						key->coverage_alpha = hud_hires_override_coverage(override) != 0;
						key->point_threshold = hud_hires_override_point_threshold(override) != 0;
					}
				}
			}
			if (description.linear)
			{
				texture_scale[stage][0] = 1.0f / (float)description.width;
				texture_scale[stage][1] = 1.0f / (float)description.height;
			}
		}
		bg_stage_textures[stage].handle = handle;
		bg_stage_textures[stage].flags = bg_sampler_flags(stage, mipmapped, hires,
			key->sampler_type[stage] == _xgpu_sampler_2d);
	}
}

/* ---------- textures the port's own modules make (xgpu.h) */

unsigned int xgpu_bgfx_texture_create(unsigned long width, unsigned long height, const unsigned char *rgba,
	BOOL mipmapped)
{
	bgfx_texture_handle_t handle;
	unsigned long level_width = width, level_height = height;
	unsigned char *level = NULL;
	int mip = 0;

	if (!bg.ready)
		return 0;
	handle = bgfx_create_texture_2d((uint16_t)width, (uint16_t)height, mipmapped != 0, 1, BGFX_TEXTURE_FORMAT_RGBA8,
		BGFX_TEXTURE_NONE, NULL, 0);
	if (!BGFX_HANDLE_IS_VALID(handle))
		return 0;
	if (!rgba)
		return (unsigned int)handle.idx + 1;
	bgfx_update_texture_2d(handle, 0, 0, 0, 0, (uint16_t)width, (uint16_t)height,
		bgfx_copy(rgba, (uint32_t)(width * height * 4)), UINT16_MAX);
	while (mipmapped && (level_width > 1 || level_height > 1))
	{
		unsigned long next_width = level_width > 1 ? level_width / 2 : 1;
		unsigned long next_height = level_height > 1 ? level_height / 2 : 1;
		unsigned char *next = malloc(next_width * next_height * 4);

		if (!next)
			break;
		bg_downsample(level ? level : rgba, level_width, level_height, next);
		free(level);
		level = next;
		level_width = next_width;
		level_height = next_height;
		bgfx_update_texture_2d(handle, 0, (uint8_t)++mip, 0, 0, (uint16_t)level_width, (uint16_t)level_height,
			bgfx_copy(level, (uint32_t)(level_width * level_height * 4)), UINT16_MAX);
	}
	free(level);
	return (unsigned int)handle.idx + 1;
}

void xgpu_bgfx_texture_rows(unsigned int name, unsigned long top, unsigned long width, unsigned long rows,
	const unsigned char *rgba)
{
	if (bg.ready && name && rows)
		bgfx_update_texture_2d(bg_texture_named(name), 0, 0, 0, (uint16_t)top, (uint16_t)width, (uint16_t)rows,
			bgfx_copy(rgba, (uint32_t)(width * rows * 4)), UINT16_MAX);
}

void xgpu_bgfx_texture_delete(unsigned int name)
{
	if (bg.ready && name)
		bgfx_destroy_texture(bg_texture_named(name));
}

/* ---------- the draw's uniforms (prepare_draw's) */

static void bg_uniforms_update(float texture_scale[4][4])
{
	struct draw_uniforms *converted = &bg_uniforms;
	unsigned long index;
	int stage;

	memcpy(converted->viewport_scale, device.viewport_scale, sizeof(converted->viewport_scale));
	memcpy(converted->viewport_offset, device.viewport_offset, sizeof(converted->viewport_offset));
	memcpy(converted->texture_scale, texture_scale, sizeof(converted->texture_scale));
	converted->point_size = D3D__RenderState[D3DRS_POINTSIZE] ? dword_to_float(D3D__RenderState[D3DRS_POINTSIZE]) : 1.0f;
	for (stage = 0; stage < 8; stage++)
	{
		color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage], converted->ps_c0[stage]);
		color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage], converted->ps_c1[stage]);
	}
	color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0], converted->ps_final_c0);
	color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1], converted->ps_final_c1);
	color_to_vec4(D3D__RenderState[D3DRS_FOGCOLOR], converted->fog_color);
	converted->fog_parameters[0] = dword_to_float(D3D__RenderState[D3DRS_FOGSTART]);
	converted->fog_parameters[1] = dword_to_float(D3D__RenderState[D3DRS_FOGEND]);
	converted->fog_parameters[2] = dword_to_float(D3D__RenderState[D3DRS_FOGDENSITY]);
	converted->fog_parameters[3] = 0.0f;
	converted->alpha_reference = (float)(D3D__RenderState[D3DRS_ALPHAREF] & 0xff);
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		DWORD *state = D3D__TextureState[stage];

		converted->bump_matrix[stage][0] = dword_to_float(state[D3DTSS_BUMPENVMAT00]);
		converted->bump_matrix[stage][1] = dword_to_float(state[D3DTSS_BUMPENVMAT01]);
		converted->bump_matrix[stage][2] = dword_to_float(state[D3DTSS_BUMPENVMAT10]);
		converted->bump_matrix[stage][3] = dword_to_float(state[D3DTSS_BUMPENVMAT11]);
		converted->bump_luminance[stage][0] = dword_to_float(state[D3DTSS_BUMPENVLSCALE]);
		converted->bump_luminance[stage][1] = dword_to_float(state[D3DTSS_BUMPENVLOFFSET]);
		converted->bump_luminance[stage][2] = converted->bump_luminance[stage][3] = 0.0f;
		converted->texture_lod_bias[stage] = dword_to_float(state[D3DTSS_MIPMAPLODBIAS]);
	}
	converted->screen_offset = (float)UI_OFFSET;
	bg_depth_range[0] = device.viewport.MinZ;
	bg_depth_range[1] = device.viewport.MaxZ - device.viewport.MinZ;
	bg_depth_range[2] = bg_depth_range[3] = 0.0f;
	/* (glPolygonOffset's units: the depth buffer's smallest step, 2^-24) */
	/* (the slope's factor scaled by the target's pixels a unit, as the
	OpenGL path's: apply_raster_state) */
	bg_depth_bias[0] = dword_to_float(D3D__RenderState[D3DRS_POLYGONOFFSETZSLOPESCALE]) *
		(bg.target_scale[0] > bg.target_scale[1] ? bg.target_scale[0] : bg.target_scale[1]);
	bg_depth_bias[1] = dword_to_float(D3D__RenderState[D3DRS_POLYGONOFFSETZOFFSET]) / 16777216.0f;
	bg_depth_bias[2] = bg_depth_bias[3] = 0.0f;
	for (index = 0; index < BG_UNIFORM_COUNT; index++)
	{
		if (bg_uniform_sources[index].components == 1)
		{
			float value[4] = { *bg_uniform_sources[index].source, 0.0f, 0.0f, 0.0f };

			bgfx_set_uniform(bg.uniforms[index], value, 1);
		}
		else
		{
			bgfx_set_uniform(bg.uniforms[index], bg_uniform_sources[index].source, bg_uniform_sources[index].count);
		}
	}
}

/* ---------- vertices */

/* an element of a stream's vertex as four floats, missing components
(0, 0, 0, 1) as OpenGL's */
static void bg_vertex_element(const struct vertex_element *element, const unsigned char *source, float *out)
{
	unsigned long count = 0, index;

	out[0] = out[1] = out[2] = 0.0f;
	out[3] = 1.0f;
	switch (element->type)
	{
	case D3DVSDT_FLOAT1: count = 1; break;
	case D3DVSDT_FLOAT2: count = 2; break;
	case D3DVSDT_FLOAT3: case D3DVSDT_FLOAT2H: count = 3; break;
	case D3DVSDT_FLOAT4: count = 4; break;
	case D3DVSDT_NORMPACKED3:
	{
		/* (nv2a_vsh.c's unpack_normpacked3) */
		unsigned int packed;
		int x, y, z;

		memcpy(&packed, source, 4);
		x = (int)(packed << 21) >> 21;
		y = (int)(packed << 10) >> 21;
		z = (int)packed >> 22;
		out[0] = (float)x / 1023.0f;
		out[1] = (float)y / 1023.0f;
		out[2] = (float)z / 511.0f;
		return;
	}
	case D3DVSDT_D3DCOLOR:
		out[0] = source[2] / 255.0f;
		out[1] = source[1] / 255.0f;
		out[2] = source[0] / 255.0f;
		out[3] = source[3] / 255.0f;
		return;
	case D3DVSDT_SHORT1: case D3DVSDT_SHORT2: case D3DVSDT_SHORT3: case D3DVSDT_SHORT4:
	case D3DVSDT_NORMSHORT1: case D3DVSDT_NORMSHORT2: case D3DVSDT_NORMSHORT3: case D3DVSDT_NORMSHORT4:
	{
		BOOL normalized = element->type == D3DVSDT_NORMSHORT1 || element->type == D3DVSDT_NORMSHORT2 ||
			element->type == D3DVSDT_NORMSHORT3 || element->type == D3DVSDT_NORMSHORT4;

		count = element->bytes / 2;
		for (index = 0; index < count; index++)
		{
			short value;

			memcpy(&value, source + index * 2, 2);
			out[index] = normalized ? (value < -32767 ? -1.0f : value / 32767.0f) : (float)value;
		}
		return;
	}
	case D3DVSDT_PBYTE1: case D3DVSDT_PBYTE2: case D3DVSDT_PBYTE3: case D3DVSDT_PBYTE4:
		for (index = 0; index < element->bytes; index++)
			out[index] = source[index] / 255.0f;
		return;
	default:
		return;
	}
	memcpy(out, source, count * sizeof(float));
}

static uint64_t bg_topology(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
	case D3DPT_POINTLIST: return BGFX_STATE_PT_POINTS;
	case D3DPT_LINELIST: return BGFX_STATE_PT_LINES;
	case D3DPT_LINELOOP:
	case D3DPT_LINESTRIP: return BGFX_STATE_PT_LINESTRIP;
	case D3DPT_TRIANGLESTRIP:
	case D3DPT_QUADSTRIP: return BGFX_STATE_PT_TRISTRIP;
	default: return 0;
	}
}

static unsigned long bg_index_count(D3DPRIMITIVETYPE type, unsigned long count)
{
	switch (type)
	{
	case D3DPT_QUADLIST: return count / 4 * 6;
	case D3DPT_TRIANGLEFAN:
	case D3DPT_POLYGON: return count >= 3 ? (count - 2) * 3 : 0;
	case D3DPT_LINELOOP: return count + 1;
	default: return count;
	}
}

static void bg_indices(D3DPRIMITIVETYPE type, const WORD *source, unsigned long count, unsigned long rebase, WORD *out)
{
	unsigned long index;

#define BG_INDEX(i) ((WORD)((source ? source[i] : (i)) - rebase))
	switch (type)
	{
	case D3DPT_QUADLIST:
		for (index = 0; index + 3 < count; index += 4)
		{
			*out++ = BG_INDEX(index);
			*out++ = BG_INDEX(index + 1);
			*out++ = BG_INDEX(index + 2);
			*out++ = BG_INDEX(index);
			*out++ = BG_INDEX(index + 2);
			*out++ = BG_INDEX(index + 3);
		}
		break;
	case D3DPT_TRIANGLEFAN:
	case D3DPT_POLYGON:
		for (index = 1; index + 1 < count; index++)
		{
			*out++ = BG_INDEX(0);
			*out++ = BG_INDEX(index);
			*out++ = BG_INDEX(index + 1);
		}
		break;
	default:
		for (index = 0; index < count; index++)
			*out++ = BG_INDEX(index);
		if (type == D3DPT_LINELOOP)
			*out++ = BG_INDEX(0);
		break;
	}
#undef BG_INDEX
}

/* ---------- vertices

Every draw's vertices are converted into one layout, all sixteen input
registers as floats. Most never change once loaded (the level, models), so
a range drawn from is converted once and kept in a vertex buffer, its pages
write-protected as cached textures' are (memory_watch.c): the game writing
one converts it again, and ranges it rewrites frame after frame stream
instead, as the OpenGL path's mirror does. */

struct bg_vertex_entry
{
	struct bg_vertex_entry *next;
	const struct vertex_shader_object *declaration;
	struct vertex_element elements[XGPU_VERTEX_ATTRIBUTE_COUNT];
	unsigned long element_count;
	/* each stream's first vertex (an Xbox address) and stride; 0 unused */
	unsigned long address[16], stride[16];
	unsigned long vertex_count;
	/* the current values standing for the registers the streams leave out */
	unsigned short provided;
	float attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
	bgfx_vertex_buffer_handle_t buffer;
	unsigned long generation;
	unsigned long used_frame, rewritten_frame, volatile_frame;
	unsigned char rewrites;
};

/* the declaration's registers that come from streams this draw */
static unsigned short bg_vertex_provided(const struct vertex_shader_object *declaration)
{
	unsigned short provided = 0;
	unsigned long index;

	for (index = 0; index < declaration->element_count; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];

		if (device.streams[element->stream].data && element->type != D3DVSDT_NONE &&
			element->reg < XGPU_VERTEX_ATTRIBUTE_COUNT)
			provided |= (unsigned short)(1U << element->reg);
	}
	return provided;
}

static void bg_vertices_convert(unsigned char *data, const struct vertex_shader_object *declaration,
	unsigned long first, unsigned long vertex_count)
{
	unsigned long vertex, index;

	for (vertex = 0; vertex < vertex_count; vertex++)
	{
		float *out = (float *)(data + vertex * BG_VERTEX_BYTES);
		unsigned short provided = 0;

		for (index = 0; index < declaration->element_count; index++)
		{
			const struct vertex_element *element = &declaration->elements[index];
			const unsigned char *source;

			if (!device.streams[element->stream].data || element->type == D3DVSDT_NONE ||
				element->reg >= XGPU_VERTEX_ATTRIBUTE_COUNT || (provided & (1U << element->reg)))
				continue;
			provided |= (unsigned short)(1U << element->reg);
			source = (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[element->stream].data) +
				(first + vertex) * device.streams[element->stream].stride + element->offset;
			bg_vertex_element(element, source, out + element->reg * 4);
		}
		for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		{
			if (!(provided & (1U << index)))
				memcpy(out + index * 4, device.attributes[index], 4 * sizeof(float));
		}
	}
}

static void bg_vertex_entry_free(struct bg_vertex_entry *entry)
{
	if (BGFX_HANDLE_IS_VALID(entry->buffer))
	{
		bgfx_destroy_vertex_buffer(entry->buffer);
		bg.vertex_cache_bytes -= entry->vertex_count * BG_VERTEX_BYTES;
	}
	free(entry);
}

/* drops the ranges not drawn from for a while (bg_present) */
static void bg_vertex_cache_trim(void)
{
	unsigned long bucket;

	for (bucket = 0; bucket < BG_VERTEX_BUCKETS; bucket++)
	{
		struct bg_vertex_entry **link = &bg.vertex_buckets[bucket];

		while (*link)
		{
			struct bg_vertex_entry *entry = *link;

			if (bg.frame - entry->used_frame > BG_VERTEX_UNUSED_FRAMES)
			{
				*link = entry->next;
				bg_vertex_entry_free(entry);
			}
			else
			{
				link = &entry->next;
			}
		}
	}
}

/* the draw's vertices kept on the GPU, converted again if the game wrote
them; invalid when they are to stream (outside the contiguous window,
rewritten often, or no room) */
static bgfx_vertex_buffer_handle_t bg_vertex_cache_get(const struct vertex_shader_object *declaration,
	unsigned long first, unsigned long vertex_count)
{
	bgfx_vertex_buffer_handle_t none = BGFX_INVALID_HANDLE;
	unsigned long address[16], stride[16], stream, index, hash, generation = 0;
	unsigned short provided = bg_vertex_provided(declaration);
	struct bg_vertex_entry *entry;
	const bgfx_memory_t *memory;

	memset(address, 0, sizeof(address));
	memset(stride, 0, sizeof(stride));
	for (index = 0; index < declaration->element_count; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];
		unsigned long start;

		stream = element->stream;
		if (!device.streams[stream].data || element->type == D3DVSDT_NONE || address[stream])
			continue;
		stride[stream] = device.streams[stream].stride;
		address[stream] = (unsigned long)XBOX_ADDRESS(PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data)) +
			first * stride[stream];
		start = address[stream] - PLATFORM_CONTIGUOUS_BASE;
		if (address[stream] < PLATFORM_CONTIGUOUS_BASE ||
			start + (stride[stream] ? stride[stream] * vertex_count : 64) > PLATFORM_CONTIGUOUS_SIZE)
			return none;
	}
	hash = (unsigned long)(uintptr_t)declaration * 31 + vertex_count;
	for (stream = 0; stream < 16; stream++)
		hash = hash * 33 + address[stream];
	hash = (hash ^ (hash >> 13) ^ (hash >> 23)) % BG_VERTEX_BUCKETS;
	for (entry = bg.vertex_buckets[hash]; entry; entry = entry->next)
	{
		if (entry->declaration == declaration && entry->vertex_count == vertex_count &&
			entry->provided == provided && entry->element_count == declaration->element_count &&
			!memcmp(entry->address, address, sizeof(address)) && !memcmp(entry->stride, stride, sizeof(stride)) &&
			!memcmp(entry->elements, declaration->elements, declaration->element_count * sizeof(entry->elements[0])))
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return none;
		entry->declaration = declaration;
		memcpy(entry->elements, declaration->elements, sizeof(entry->elements));
		entry->element_count = declaration->element_count;
		memcpy(entry->address, address, sizeof(address));
		memcpy(entry->stride, stride, sizeof(stride));
		entry->vertex_count = vertex_count;
		entry->provided = provided;
		entry->buffer = none;
		entry->next = bg.vertex_buckets[hash];
		bg.vertex_buckets[hash] = entry;
	}
	entry->used_frame = bg.frame;
	if (entry->volatile_frame && bg.frame - entry->volatile_frame < BG_VERTEX_VOLATILE_FRAMES)
		return none;
	for (stream = 0; stream < 16; stream++)
	{
		if (address[stream])
		{
			unsigned long newest = memory_watch_generation(address[stream],
				stride[stream] ? stride[stream] * vertex_count : 64);

			if (newest > generation)
				generation = newest;
		}
	}
	/* (the registers the streams leave out are part of the vertices) */
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (!(provided & (1U << index)) && memcmp(entry->attributes[index], device.attributes[index], 4 * sizeof(float)))
		{
			memcpy(entry->attributes[index], device.attributes[index], 4 * sizeof(float));
			generation = ~0UL;
		}
	}
	if (BGFX_HANDLE_IS_VALID(entry->buffer) && generation <= entry->generation)
		return entry->buffer;
	if (BGFX_HANDLE_IS_VALID(entry->buffer))
	{
		/* rewritten */
		if (bg.frame - entry->rewritten_frame <= BG_VERTEX_REWRITE_FRAMES)
			entry->rewrites++;
		else
			entry->rewrites = 1;
		entry->rewritten_frame = bg.frame;
		/* (earlier draws keep the old one until the frame is done) */
		bgfx_destroy_vertex_buffer(entry->buffer);
		bg.vertex_cache_bytes -= entry->vertex_count * BG_VERTEX_BYTES;
		entry->buffer = none;
		if (entry->rewrites >= BG_VERTEX_VOLATILE_REWRITES)
		{
			entry->volatile_frame = bg.frame;
			entry->rewrites = 0;
			return none;
		}
	}
	if (bg.vertex_cache_bytes + vertex_count * BG_VERTEX_BYTES > BG_VERTEX_CACHE_BUDGET)
		return none;
	/* protect first, so a write racing with the conversion is noticed */
	generation = 0;
	for (stream = 0; stream < 16; stream++)
	{
		if (address[stream])
		{
			unsigned long size = stride[stream] ? stride[stream] * vertex_count : 64;
			unsigned long newest;

			memory_watch_protect(address[stream], size);
			newest = memory_watch_generation(address[stream], size);
			if (newest > generation)
				generation = newest;
		}
	}
	memory = bgfx_alloc((uint32_t)(vertex_count * BG_VERTEX_BYTES));
	{
		double start = stats_seconds();

		bg_vertices_convert(memory->data, declaration, first, vertex_count);
		bg.account.vertex_seconds += stats_seconds() - start;
	}
	entry->buffer = bgfx_create_vertex_buffer(memory, &bg.layout, BGFX_BUFFER_NONE);
	entry->generation = generation;
	if (BGFX_HANDLE_IS_VALID(entry->buffer))
		bg.vertex_cache_bytes += vertex_count * BG_VERTEX_BYTES;
	return entry->buffer;
}

/* ---------- drawing */

#ifdef HALO_SHADER_PREP
/* (port/linux/game/render_enumerate.c: a map's first frame is being drawn
through every shader, unseen) */
int render_enumerate_active(void);

/* whether a draw of the pass through every shader leaves the frame that is
shown half of the uniform space and of the transient buffers there were
when the pass started */
static BOOL bg_enumerate_room(unsigned long uniform_bytes, unsigned long vertices, unsigned long indices)
{
	static unsigned long frame = ~0UL;
	static uint32_t vertices_at_start, indices_at_start;

	if (frame != bg.frame)
	{
		frame = bg.frame;
		vertices_at_start = bgfx_get_avail_transient_vertex_buffer(0x7fffffffU, &bg.layout);
		indices_at_start = bgfx_get_avail_transient_index_buffer(0x7fffffffU, false);
	}
	if (bg.uniform_bytes + uniform_bytes > BG_UNIFORM_BUDGET / 2)
		return FALSE;
	if (vertices && bgfx_get_avail_transient_vertex_buffer(0x7fffffffU, &bg.layout) < vertices + vertices_at_start / 2)
		return FALSE;
	if (indices && bgfx_get_avail_transient_index_buffer(0x7fffffffU, false) < indices + indices_at_start / 2)
		return FALSE;
	return TRUE;
}
#endif

static void bg_draw(D3DPRIMITIVETYPE type, BOOL immediate, unsigned long first, unsigned long vertex_count,
	const WORD *indices, unsigned long index_count, unsigned long rebase)
{
	struct vertex_shader_object *program = current_program();
	struct vertex_shader_object *declaration = device.vertex_shader;
	struct nv2a_pixel_shader_key key;
	struct bg_shader *vertex_shader, *fragment_shader;
	bgfx_program_handle_t handle;
	bgfx_transient_vertex_buffer_t vertices;
	bgfx_transient_index_buffer_t generated_indices;
	bgfx_vertex_buffer_handle_t cached = BGFX_INVALID_HANDLE;
	float texture_scale[4][4];
	unsigned short rect[4];
	unsigned long generated, uniform_bytes;
	uint32_t stencil, back_stencil, rgba;
	uint64_t state;
	BOOL has_depth, native, starved;
	int stage;

	if (!vertex_count || !program || !declaration || !program->instructions)
		return;
	if (vertex_count > 0xffff)
	{
		bg.skipped++;
		return;
	}
	/* (the mipmapped copies of the render targets it samples, made before
	any of its state is set: they draw) */
	bg_composites_prepare();

	/* the pixel shader's key (prepare_draw's), the textures first: their
	sampler types are part of it (bgfx keeps what is set until the submit) */
	memset(&key, 0, sizeof(key));
	memcpy(key.combiner_state, D3D__RenderState, sizeof(key.combiner_state));
	memset(&key.combiner_state[D3DRS_PSCONSTANT0_0], 0, 16 * sizeof(DWORD));
	key.combiner_state[D3DRS_PSFINALCOMBINERCONSTANT0] = 0;
	key.combiner_state[D3DRS_PSFINALCOMBINERCONSTANT1] = 0;
	key.texture_modes = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	bg_bind_textures(&key, texture_scale);
	if (!bg_bind_targets(&has_depth) ||
		!bg_target_rect((float)device.viewport.X, (float)device.viewport.Y, (float)device.viewport.Width,
			(float)device.viewport.Height, rect))
	{
		bgfx_discard(BGFX_DISCARD_ALL);
		bg.skipped++;
		return;
	}
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		key.alpha_kill[stage] = D3D__TextureState[stage][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key.color_sign[stage] = (unsigned char)((D3D__TextureState[stage][D3DTSS_COLORSIGN] >> 28) & 0xf);
	}
	/* (only with the meter's blend: hud_hires.h, nv2a_pixel_shader_key) */
	key.coverage_alpha = key.coverage_alpha && D3D__RenderState[D3DRS_ALPHABLENDENABLE] &&
		D3D__RenderState[D3DRS_SRCBLEND] == D3DBLEND_CONSTANTCOLOR &&
		D3D__RenderState[D3DRS_DESTBLEND] == D3DBLEND_SRCALPHA;
	key.point_threshold = key.point_threshold && key.coverage_alpha;
	key.alpha_test_function = D3D__RenderState[D3DRS_ALPHATESTENABLE] ? D3D__RenderState[D3DRS_ALPHAFUNC] : 0;
	/* (multisampled: the alpha test covers samples, as the OpenGL path's) */
	if (bg.target_samples > 1 && key.alpha_test_function && !D3D__RenderState[D3DRS_ALPHABLENDENABLE])
		key.alpha_test_samples = (unsigned char)bg.target_samples;
	key.fog_enable = D3D__RenderState[D3DRS_FOGENABLE] != 0;
	key.fog_table_mode = (unsigned char)D3D__RenderState[D3DRS_FOGTABLEMODE];

	vertex_shader = bg_vertex_shader(program, immediate);
	fragment_shader = bg_fragment_shader(&key, has_depth && D3D__RenderState[D3DRS_SOLIDOFFSETENABLE] ?
		_bg_shader_depth_bias : 0);
	{
		struct bg_program *entry = vertex_shader && fragment_shader ? bg_program_entry(vertex_shader, fragment_shader) :
			NULL;

		handle = entry ? entry->program : (bgfx_program_handle_t)BGFX_INVALID_HANDLE;
#ifdef HALO_SHADER_PREP
		if (entry && entry->noted != shader_prep_serial)
		{
			prep_note(&entry->noted, program, immediate, FALSE, FALSE,
				has_depth && D3D__RenderState[D3DRS_SOLIDOFFSETENABLE], declaration->packed_mask, &key);
		}
#endif
	}
	native = !indices && type != D3DPT_QUADLIST && type != D3DPT_TRIANGLEFAN && type != D3DPT_POLYGON &&
		type != D3DPT_LINELOOP;
	generated = native ? 0 : bg_index_count(type, indices ? index_count : vertex_count);
	/* (Metal copies both shaders' uniform blocks for each draw, into a frame's
	space that is not to be overrun) */
	uniform_bytes = vertex_shader && fragment_shader ?
		((vertex_shader->uniform_size + 255) & ~255UL) + ((fragment_shader->uniform_size + 255) & ~255UL) : 0;
	if (bg.uniform_bytes + uniform_bytes > BG_UNIFORM_BUDGET)
		bg_log_once("more draws than a frame's uniform space holds");
	if (!immediate && BGFX_HANDLE_IS_VALID(handle))
		cached = bg_vertex_cache_get(declaration, first, vertex_count);
#ifdef HALO_SHADER_PREP
	/* (a map's first frame drawn first through every shader, unseen, leaves
	half of the frame's uniform space and transient buffers to the frame that
	is shown: past it, its draws are left out) */
	if (BGFX_HANDLE_IS_VALID(handle) && render_enumerate_active() &&
		!bg_enumerate_room(uniform_bytes, BGFX_HANDLE_IS_VALID(cached) ? 0 : vertex_count, generated))
	{
		bgfx_discard(BGFX_DISCARD_ALL);
		bg.skipped++;
		return;
	}
#endif
	starved = (!BGFX_HANDLE_IS_VALID(cached) &&
			bgfx_get_avail_transient_vertex_buffer((uint32_t)(vertex_count + BG_OVERLAY_RESERVE_VERTICES), &bg.layout) <
				vertex_count + BG_OVERLAY_RESERVE_VERTICES) ||
		(generated && bgfx_get_avail_transient_index_buffer((uint32_t)generated, false) < generated);
	if (starved && BGFX_HANDLE_IS_VALID(handle))
	{
		/* (the scene loses this draw; the picture is still presented,
		bg_blit) */
		static BOOL logged;

		if (!logged)
			platform_log("bgfx: a frame ran out of transient vertices or indices; its draws past them are left out");
		logged = TRUE;
		bg.stat_starved++;
	}
	if (!BGFX_HANDLE_IS_VALID(handle) || (!native && !generated) || bg.uniform_bytes + uniform_bytes > BG_UNIFORM_BUDGET ||
		starved)
	{
		/* (drop the textures set for it) */
		bgfx_discard(BGFX_DISCARD_ALL);
		bg.skipped++;
		return;
	}
	bg.uniform_bytes += uniform_bytes;

	/* the vertices: every input register, from the declaration's elements or
	the current values the declaration leaves out (immediate vertices are
	all sixteen registers already) */
	if (BGFX_HANDLE_IS_VALID(cached))
	{
		bgfx_set_vertex_buffer(0, cached, 0, (uint32_t)vertex_count);
		bg.stat_cached_vertices += vertex_count;
	}
	else
	{
		bgfx_alloc_transient_vertex_buffer(&vertices, (uint32_t)vertex_count, &bg.layout);
		if (immediate)
			memcpy(vertices.data, device.immediate_vertices, vertex_count * BG_VERTEX_BYTES);
		else
			bg_vertices_convert(vertices.data, declaration, first, vertex_count);
		bgfx_set_transient_vertex_buffer(0, &vertices, 0, (uint32_t)vertex_count);
		bg.stat_streamed_vertices += vertex_count;
	}
	if (generated)
	{
		bgfx_alloc_transient_index_buffer(&generated_indices, (uint32_t)generated, false);
		bg_indices(type, indices, indices ? index_count : vertex_count, rebase, (WORD *)generated_indices.data);
		bgfx_set_transient_index_buffer(&generated_indices, 0, (uint32_t)generated);
	}

	bg_uniforms_update(texture_scale);
	state = bg_state(has_depth, &stencil, &back_stencil, &rgba) | bg_topology(type);
	bgfx_set_state(state, rgba);
	bgfx_set_stencil(stencil, back_stencil);
	bg_view_set(bg.framebuffer, rect, FALSE);
	/* (after the view is set: its blits touch it, which drops what is set) */
	bg_apply_textures();
	if (bg.test_active)
	{
		/* (inside a visibility test: a query of its own, summed at the end) */
		bgfx_occlusion_query_handle_t query = bg.queries[bg.query_next++ % BG_QUERY_COUNT];

		bg.test_count++;
		bgfx_submit_occlusion_query(bg.view, handle, query, 0, BGFX_DISCARD_ALL);
	}
	else
	{
		bgfx_submit(bg.view, handle, 0, BGFX_DISCARD_ALL);
	}
	bg.view_used = TRUE;
	bg.drawn++;
}

static void bg_draw_vertices(D3DPRIMITIVETYPE type, unsigned long start, unsigned long count)
{
	bg_draw(type, FALSE, start, count, NULL, 0, 0);
}

static void bg_draw_indexed(D3DPRIMITIVETYPE type, unsigned long count, const WORD *indices)
{
	unsigned long minimum = 0xffff, maximum = 0, index;

	if (!count || !indices)
		return;
	for (index = 0; index < count; index++)
	{
		if (indices[index] < minimum)
			minimum = indices[index];
		if (indices[index] > maximum)
			maximum = indices[index];
	}
	/* (the vertices from the base vertex on: index i is vertex base + i;
	the indices are drawn made relative to the first vertex staged) */
	bg_draw(type, FALSE, device.base_vertex_index + minimum, maximum - minimum + 1, indices, count, minimum);
}

static void bg_draw_immediate(D3DPRIMITIVETYPE type, unsigned long count)
{
	bg_draw(type, TRUE, 0, count, NULL, 0, 0);
}

/* a quad's four vertices, a triangle strip from (x0, y0) to (-x0, -y0) in
clip space, its texture coordinates 0 to 1 */
static void bg_quad_corners(unsigned char *vertices, float x0, float y0)
{
	int corner;

	memset(vertices, 0, 4 * BG_VERTEX_BYTES);
	for (corner = 0; corner < 4; corner++)
	{
		float *out = (float *)(vertices + corner * BG_VERTEX_BYTES);

		out[0] = corner & 1 ? -x0 : x0;
		out[1] = corner & 2 ? -y0 : y0;
		out[4] = corner & 1 ? 1.0f : 0.0f;
		out[5] = corner & 2 ? 1.0f : 0.0f;
	}
}

/* a clear of some of the color channels (the Xbox's D3DCLEAR_TARGET_R, _G,
_B, _A: the fog screen clears only alpha, leaving the picture under the
fog), which bgfx's view clear can't mask: a quad over the view that writes
only those channels, the blit's program sampling white times the blend
factor, the clear's color */
static void bg_clear_channels(DWORD channels, D3DCOLOR color)
{
	uint64_t state = BGFX_STATE_PT_TRISTRIP | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_FACTOR, BGFX_STATE_BLEND_ZERO);

	if (!BGFX_HANDLE_IS_VALID(bg.blit) || !BGFX_HANDLE_IS_VALID(bg.full_quad))
		return;
	state |= channels & D3DCLEAR_TARGET_R ? BGFX_STATE_WRITE_R : 0;
	state |= channels & D3DCLEAR_TARGET_G ? BGFX_STATE_WRITE_G : 0;
	state |= channels & D3DCLEAR_TARGET_B ? BGFX_STATE_WRITE_B : 0;
	state |= channels & D3DCLEAR_TARGET_A ? BGFX_STATE_WRITE_A : 0;
	bgfx_set_vertex_buffer(0, bg.full_quad, 0, 4);
	bgfx_set_texture(0, bg.samplers[0], bg.white, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	bgfx_set_state(state, (uint32_t)((color << 8) | (color >> 24)));
	bgfx_submit(bg.view, bg.blit, 0, BGFX_DISCARD_ALL);
}

static void bg_clear(DWORD count, const D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	unsigned short rect[4];
	uint16_t clear = 0;
	DWORD channels = flags & D3DCLEAR_TARGET;
	BOOL has_depth, done;

	if (!bg_bind_targets(&has_depth))
		return;
	if (channels == D3DCLEAR_TARGET)
		clear |= BGFX_CLEAR_COLOR;
	else if (!channels || !bg.target_color)
		channels = 0;
	if (has_depth && (flags & D3DCLEAR_ZBUFFER))
		clear |= BGFX_CLEAR_DEPTH;
	if (has_depth && (flags & D3DCLEAR_STENCIL))
		clear |= BGFX_CLEAR_STENCIL;
	if (!clear && !channels)
		return;
	/* the viewport's, or each rectangle's: a view of its own, which clears
	as it starts */
	for (done = FALSE; !done; )
	{
		BOOL drawn;

		if (count && rectangles)
		{
			drawn = bg_target_rect((float)rectangles->x1, (float)rectangles->y1, (float)(rectangles->x2 - rectangles->x1),
				(float)(rectangles->y2 - rectangles->y1), rect);
			rectangles++;
			done = --count == 0;
		}
		else
		{
			drawn = bg_target_rect((float)device.viewport.X, (float)device.viewport.Y, (float)device.viewport.Width,
				(float)device.viewport.Height, rect);
			done = TRUE;
		}
		if (!drawn)
			continue;
		bg_view_set(bg.framebuffer, rect, TRUE);
		bgfx_set_view_clear(bg.view, clear, (uint32_t)((color << 8) | (color >> 24)), z, (uint8_t)stencil);
		bgfx_touch(bg.view);
		if (channels && channels != D3DCLEAR_TARGET)
			bg_clear_channels(channels, color);
		bg.view_used = TRUE;
	}
}

/* ---------- visibility tests (lens flares, The Library's lights)

Each draw inside a test is submitted with an occlusion query of its own,
which counts its samples (Metal's counting mode); a test's
count is its draws', once bgfx has them all, a frame or two later. Until then
the slot's earlier count stands, as the OpenGL path's does: the game spins on
a result it is told is incomplete. */

static void bg_visibility_begin(void)
{
	if (bg.test_active)
		return;
	bg.test_active = TRUE;
	bg.test_first = bg.query_next;
	bg.test_count = 0;
}

/* number: the test's (D3DDevice_EndVisibilityTest); FALSE when no test
was running */
static BOOL bg_visibility_end(DWORD index, unsigned long number)
{
	unsigned char count;

	if (!bg.test_active)
		return FALSE;
	bg.test_active = FALSE;
	index %= VISIBILITY_TEST_SLOTS;
	count = bg.tests[index].pending_count;
	if (count == BG_TESTS_PENDING)
	{
		/* (the oldest goes) */
		memmove(&bg.tests[index].pending[0], &bg.tests[index].pending[1],
			(BG_TESTS_PENDING - 1) * sizeof(bg.tests[index].pending[0]));
		count--;
	}
	bg.tests[index].pending[count].first = bg.test_first;
	bg.tests[index].pending[count].count = bg.test_count < BG_QUERY_COUNT ? bg.test_count : BG_QUERY_COUNT;
	bg.tests[index].pending[count].frame = bg.frame;
	bg.tests[index].pending[count].number = number;
	/* (the count in the game's pixels: lens flares divide it by their area;
	a multisampled target's queries count its samples) */
	bg.tests[index].pending[count].area = bg.target_scale[0] * bg.target_scale[1] *
		(float)(bg.target_samples > 1 ? bg.target_samples : 1);
	bg.tests[index].pending_count = (unsigned char)(count + 1);
	bg.stat_tests++;
	return TRUE;
}

/* the slot's latest count known: its newest test whose queries have their
results (bgfx's results are a query's latest, this test's once the GPU has
finished its frame, which bgfx reads that many frames later); until then,
the slot's earlier count. number: the test the count is of (0: none yet) */
static UINT bg_visibility_result(DWORD index, unsigned long *number)
{
	int test, done = -1;
	unsigned long query;

	index %= VISIBILITY_TEST_SLOTS;
	*number = bg.tests[index].known_number;
	for (test = 0; test < bg.tests[index].pending_count; test++)
	{
		/* (not one whose queries the ring has given to later tests since:
		their results are those tests') */
		if (bg.query_next - bg.tests[index].pending[test].first > BG_QUERY_COUNT)
			continue;
		if (bg.frame >= bg.tests[index].pending[test].frame + BG_QUERY_LATENCY)
			done = test;
	}
	if (done < 0)
	{
		if (bg.tests[index].pending_count)
			bg.stat_waited++;
		return bg.tests[index].known;
	}
	{
		long long samples = 0;
		BOOL complete = TRUE;

		for (query = 0; query < bg.tests[index].pending[done].count; query++)
		{
			int32_t result = 0;

			if (bgfx_get_result(bg.queries[(bg.tests[index].pending[done].first + query) % BG_QUERY_COUNT], &result) ==
				BGFX_OCCLUSION_QUERY_RESULT_NORESULT)
				complete = FALSE;
			samples += result > 0 ? result : 0;
		}
		if (complete)
		{
			float area = bg.tests[index].pending[done].area;

			bg.tests[index].known = area > 1.0f ? (UINT)(samples / area + 0.5f) : (UINT)samples;
			bg.tests[index].known_number = bg.tests[index].pending[done].number;
			*number = bg.tests[index].known_number;
			if (bg.tests[index].known)
				bg.stat_seen++;
		}
	}
	/* (it and the older ones are done with) */
	memmove(&bg.tests[index].pending[0], &bg.tests[index].pending[done + 1],
		(size_t)(bg.tests[index].pending_count - done - 1) * sizeof(bg.tests[index].pending[0]));
	bg.tests[index].pending_count = (unsigned char)(bg.tests[index].pending_count - done - 1);
	return bg.tests[index].known;
}

/* ---------- targets and presentation */

static const char bg_blit_vertex_source[] =
	"layout(location = 0) in vec4 a_position;\n"
	"layout(location = 1) in vec4 a_normal;\n"
	"out vec4 xT0;\n"
	"void main()\n"
	"{\n"
	"\txT0 = a_normal;\n"
	"\tgl_Position = vec4(a_position.xy, 0.0, 1.0);\n"
	"}\n";

static const char bg_blit_fragment_source[] =
	"in vec4 xT0;\n"
	"uniform sampler2D tex0;\n"
	"layout(location = 0) out vec4 fragment_color;\n"
	"void main()\n"
	"{\n"
	"\tfragment_color = vec4(texture(tex0, xT0.xy).rgb, 1.0);\n"
	"}\n";

/* (a level of a texture made of the one above it, every channel: the water's
ripple maps keep their fade in alpha) */
static const char bg_copy_fragment_source[] =
	"in vec4 xT0;\n"
	"uniform sampler2D tex0;\n"
	"layout(location = 0) out vec4 fragment_color;\n"
	"void main()\n"
	"{\n"
	"\tfragment_color = texture(tex0, xT0.xy);\n"
	"}\n";

/* display.crt_filter: a CRT look over the final picture (scanlines,
curvature, phosphor mask, vignette, bloom, flicker, color bleed), off by
default. The CRT filter is from Theseus by Milenko (fs_crt.sc, with its
"Classic" preset), turned into the translators' GLSL dialect. */
static const char bg_crt_fragment_source[] =
	"in vec4 xT0;\n"
	"uniform sampler2D tex0;\n"
	/* x scanlines, y curvature, z phosphor mask, w vignette */
	"uniform vec4 crt_parameters1;\n"
	/* x bloom, y flicker, z color bleed, w brightness */
	"uniform vec4 crt_parameters2;\n"
	/* x time, yz the picture's size in pixels */
	"uniform vec4 crt_parameters3;\n"
	"layout(location = 0) out vec4 fragment_color;\n"
	"vec2 distort(vec2 uv, float k)\n"
	"{\n"
	"\tvec2 cc = uv - 0.5;\n"
	"\treturn uv + cc * dot(cc, cc) * k;\n"
	"}\n"
	"void main()\n"
	"{\n"
	"\tvec2 uv = xT0.xy;\n"
	"\tvec2 resolution = crt_parameters3.yz;\n"
	"\tvec3 color;\n"
	"\tif (crt_parameters1.y > 0.0)\n"
	"\t{\n"
	"\t\tuv = distort(uv, crt_parameters1.y * 0.3);\n"
	"\t\tif (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)\n"
	"\t\t{\n"
	"\t\t\tfragment_color = vec4(0.0, 0.0, 0.0, 1.0);\n"
	"\t\t\treturn;\n"
	"\t\t}\n"
	"\t}\n"
	"\tif (crt_parameters2.z > 0.0)\n"
	"\t{\n"
	"\t\tfloat offset = crt_parameters2.z / resolution.x;\n"
	"\t\tcolor.r = texture(tex0, vec2(uv.x + offset, uv.y)).r;\n"
	"\t\tcolor.g = texture(tex0, uv).g;\n"
	"\t\tcolor.b = texture(tex0, vec2(uv.x - offset, uv.y)).b;\n"
	"\t}\n"
	"\telse\n"
	"\t\tcolor = texture(tex0, uv).rgb;\n"
	"\tif (crt_parameters2.x > 0.0)\n"
	"\t{\n"
	"\t\tfloat ps = 1.5 / resolution.x;\n"
	"\t\tfloat pt = 1.5 / resolution.y;\n"
	"\t\tvec3 bloom = (texture(tex0, uv + vec2(-ps, -pt)).rgb + texture(tex0, uv + vec2(ps, -pt)).rgb +\n"
	"\t\t\ttexture(tex0, uv + vec2(-ps, pt)).rgb + texture(tex0, uv + vec2(ps, pt)).rgb) * 0.25;\n"
	"\t\tcolor = mix(color, max(color, bloom), crt_parameters2.x);\n"
	"\t}\n"
	"\tif (crt_parameters1.x > 0.0)\n"
	"\t{\n"
	"\t\tfloat scanline = pow(sin(uv.y * resolution.y * 3.14159) * 0.5 + 0.5, 1.5);\n"
	"\t\tcolor *= mix(1.0, scanline, crt_parameters1.x * 0.5);\n"
	"\t}\n"
	"\tif (crt_parameters1.z > 0.0)\n"
	"\t{\n"
	"\t\tint px = int(gl_FragCoord.x) - 3 * (int(gl_FragCoord.x) / 3);\n"
	"\t\tfloat dim = 1.0 - crt_parameters1.z * 0.5;\n"
	"\t\tcolor *= px == 0 ? vec3(1.0, dim, dim) : px == 1 ? vec3(dim, 1.0, dim) : vec3(dim, dim, 1.0);\n"
	"\t}\n"
	"\tif (crt_parameters2.y > 0.0)\n"
	"\t\tcolor *= 1.0 - crt_parameters2.y * 0.03 * sin(crt_parameters3.x * 15.0);\n"
	"\tif (crt_parameters1.w > 0.0)\n"
	"\t{\n"
	"\t\tvec2 vig = uv * (1.0 - uv);\n"
	"\t\tcolor *= pow(vig.x * vig.y * 16.0, crt_parameters1.w * 0.3);\n"
	"\t}\n"
	"\tfragment_color = vec4(color * crt_parameters2.w, 1.0);\n"
	"}\n";

/* bgfx's reset flags: vertical sync, and the most anisotropy (16x) when
display.anisotropic_filtering forces it (bg_sampler_flags; bgfx's samplers
have either that or none) */
static unsigned long bg_reset_flags(void)
{
	unsigned long flags = config_boolean("display.vsync") ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;

	if (config_integer("display.anisotropic_filtering") >= 2)
		flags |= BGFX_RESET_MAXANISOTROPY;
	return flags;
}

static void bg_window_update(void)
{
	int width = 0, height = 0;
	unsigned long flags = bg_reset_flags();

	SDL_GetWindowSizeInPixels(bg.window, &width, &height);
	if (width == bg.window_width && height == bg.window_height && flags == bg.reset_flags)
		return;
	bg.window_width = width;
	bg.window_height = height;
	bg.reset_flags = flags;
	{
		/* (the window's swap chain: its new size; its format and flags as
		they are) */
		bgfx_swap_chain_t swap_chain = { 0 };

		swap_chain.width = (uint32_t)width;
		swap_chain.height = (uint32_t)height;
		swap_chain.formatColor = BGFX_TEXTURE_FORMAT_COUNT;
		swap_chain.formatDepthStencil = BGFX_TEXTURE_FORMAT_COUNT;
		swap_chain.depth = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
		bgfx_reset((uint32_t)flags, &swap_chain);
	}
}

/* the back buffer into the window, letterboxed to its shape: the rectangle
it fills */
static void bg_blit(unsigned short view, int letterbox[4])
{
	struct bg_target *back_buffer = bg_target_get(&device.back_buffer);
	unsigned long width = (unsigned long)bg.window_width, height;
	float x0, y0;

	bgfx_set_view_mode(view, BGFX_VIEW_MODE_SEQUENTIAL);
	bgfx_set_view_frame_buffer(view, (bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE);
	bgfx_set_view_rect(view, 0, 0, (uint16_t)bg.window_width, (uint16_t)bg.window_height, 0.0f, 1.0f);
	bgfx_set_view_clear(view, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
	bgfx_touch(view);
	if (!back_buffer || !BGFX_HANDLE_IS_VALID(bg.blit) || !BGFX_HANDLE_IS_VALID(bg.present_quad))
		return;
	height = (unsigned long)((unsigned long long)bg.window_width * back_buffer->pixel_height / back_buffer->pixel_width);
	if (height > (unsigned long)bg.window_height)
	{
		height = (unsigned long)bg.window_height;
		width = (unsigned long)((unsigned long long)bg.window_height * back_buffer->pixel_width / back_buffer->pixel_height);
	}
	letterbox[0] = (int)(bg.window_width - (int)width) / 2;
	letterbox[1] = (int)(bg.window_height - (int)height) / 2;
	letterbox[2] = (int)width;
	letterbox[3] = (int)height;
	x0 = -(float)width / bg.window_width;
	y0 = (float)height / bg.window_height;
	if (x0 != bg.present_quad_corner[0] || y0 != bg.present_quad_corner[1])
	{
		unsigned char corners[4 * BG_VERTEX_BYTES];

		bg_quad_corners(corners, x0, y0);
		bgfx_update_dynamic_vertex_buffer(bg.present_quad, 0, bgfx_copy(corners, sizeof(corners)));
		bg.present_quad_corner[0] = x0;
		bg.present_quad_corner[1] = y0;
	}
	bgfx_set_dynamic_vertex_buffer(0, bg.present_quad, 0, 4);
	bgfx_set_texture(0, bg.samplers[0], back_buffer->texture, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_PT_TRISTRIP, 0);
	if (config_boolean("display.crt_filter") && BGFX_HANDLE_IS_VALID(bg.crt))
	{
		/* (Theseus's "Classic" preset) */
		float parameters[3][4] =
		{
			{ 0.5f, 0.4f, 0.3f, 0.3f },
			{ 0.15f, 0.2f, 1.0f, 1.05f },
			{ (float)SDL_GetTicks() / 1000.0f, (float)width, (float)height, 0.0f },
		};
		int index;

		for (index = 0; index < 3; index++)
			bgfx_set_uniform(bg.crt_parameters[index], parameters[index], 1);
		bgfx_submit(view, bg.crt, 0, BGFX_DISCARD_ALL);
		return;
	}
	bgfx_submit(view, bg.blit, 0, BGFX_DISCARD_ALL);
}

/* ---------- the overlay (ui_overlay.c), over the picture at the window's
resolution: its vertices and atlas as ui_overlay.c makes them, its shaders
in the translators' dialect (in window pixels from the top, as Metal's
fragment coordinates are) */

/* (its inputs at locations 0 to 4, with no gap: bgfx's Vulkan renderer takes
an unused location in the container's list (bg_vulkan_container's 0xffff)
for instance data, and gives each such one a stand-in input after the
layout's own, at locations 5 on, which landed on the texture coordinates'
10 and 11 when they were there: every shape and glyph lost its texture
coordinates, and the setup screen on bgfx showed only its shapes' left
edges. The texture coordinates are bgfx's tangent and bitangent slots, as
xgpu_bgfx_overlay_draw's layout gives them) */
static const char bg_overlay_vertex_source[] =
	"layout(location = 0) in vec2 a_position;\n"
	"layout(location = 2) in vec2 a_tangent;\n"
	"layout(location = 1) in vec4 a_normal;\n"
	"layout(location = 3) in vec2 a_bitangent;\n"
	"layout(location = 4) in vec4 a_color0;\n"
	"uniform vec2 scale;\n"
	"out vec4 xT0;\n"
	"out vec4 xT1;\n"
	"out vec4 xT2;\n"
	"out vec4 xD0;\n"
	"void main()\n"
	"{\n"
	"\txT0 = vec4(a_tangent, 0.0, 0.0);\n"
	"\txT1 = a_normal;\n"
	"\txT2 = vec4(a_bitangent, 0.0, 0.0);\n"
	"\txD0 = a_color0;\n"
	"\tgl_Position = vec4(a_position * scale - vec2(1.0, -1.0), 0.0, 1.0);\n"
	"}\n";

static const char bg_overlay_fragment_source[] =
	"in vec4 xT0;\n"
	"in vec4 xT1;\n"
	"in vec4 xT2;\n"
	"in vec4 xD0;\n"
	"uniform sampler2D atlas;\n"
	"uniform sampler2DArray icons;\n"
	"uniform sampler2D picture;\n"
	"uniform sampler2D plasma;\n"
	"uniform sampler2D medals;\n"
	"uniform vec4 plasma_shift;\n"
	"uniform vec4 plasma_thread;\n"
	"uniform vec4 cutouts[4];\n"
	"uniform vec4 cellwall_parameters[3];\n"
	"layout(location = 0) out vec4 fragment_color;\n"
	UI_OVERLAY_CELLWALL_GLSL
	"void main()\n"
	"{\n"
	"\tfloat alpha;\n"
	"\tvec2 p = gl_FragCoord.xy;\n"
	"\tfor (int i = 0; i < 4; i++)\n"
	"\t\tif (p.x >= cutouts[i].x && p.y >= cutouts[i].y && p.x < cutouts[i].z && p.y < cutouts[i].w)\n"
	"\t\t\tdiscard;\n"
	/* (the cellwall: ui_overlay.c's) */
	"\tif (xT2.x < -5.5)\n"
	"\t{\n"
	"\t\tfragment_color = cellwall(xT0.xy, cellwall_parameters[0], cellwall_parameters[1], cellwall_parameters[2],\n"
	"\t\t\txD0);\n"
	"\t\treturn;\n"
	"\t}\n"
	/* (a medal: ui_overlay.c's) */
	"\tif (xT2.x < -4.5)\n"
	"\t{\n"
	"\t\tvec4 texel = texture(medals, xT0.xy);\n"
	"\t\tfragment_color = vec4(texel.rgb / max(texel.a, 0.001), texel.a) * xD0;\n"
	"\t\treturn;\n"
	"\t}\n"
	/* (the plasma: ui_overlay.c's) */
	"\tif (xT2.x < -3.5)\n"
	"\t{\n"
	"\t\tvec2 q = abs(xT1.xy) - xT1.zw + vec2(xT2.y);\n"
	"\t\tfloat d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - xT2.y;\n"
	"\t\tvec4 t0 = texture(plasma, xT0.xy);\n"
	"\t\tvec4 t1 = texture(plasma, xT0.xy * plasma_shift.z + plasma_shift.xy);\n"
	"\t\tfloat a0 = t0.a * dot(t0.rgb, vec3(0.3333));\n"
	"\t\tfloat a1 = t1.a * dot(t1.rgb, vec3(0.3333));\n"
	"\t\tfloat thread = pow(clamp(1.0 - abs(a0 - a1) * 3.0, 0.0, 1.0), 8.0);\n"
	"\t\tvec3 color = xD0.rgb * (0.8 + 0.6 * a0 * a1) + plasma_thread.rgb * thread * 0.18;\n"
	"\t\tfragment_color = vec4(color, xD0.a * clamp(0.5 - d, 0.0, 1.0) * (0.92 + 0.08 * thread));\n"
	"\t\treturn;\n"
	"\t}\n"
	"\tif (xT2.x < -2.5)\n"
	"\t{\n"
	"\t\tfragment_color = texture(picture, xT0.xy) * xD0;\n"
	"\t\treturn;\n"
	"\t}\n"
	"\tif (xT2.x < -1.5)\n"
	"\t{\n"
	"\t\tvec4 texel = texture(icons, vec3(xT0.xy, xT1.x));\n"
	"\t\tfragment_color = vec4(texel.rgb / max(texel.a, 0.001), texel.a) * xD0;\n"
	"\t\treturn;\n"
	"\t}\n"
	"\tif (xT0.x >= 0.0)\n"
	"\t\talpha = texture(atlas, xT0.xy).r;\n"
	"\telse\n"
	"\t{\n"
	"\t\tvec2 q = abs(xT1.xy) - xT1.zw + vec2(xT2.x);\n"
	"\t\tfloat d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - xT2.x;\n"
	"\t\talpha = clamp(0.5 - d, 0.0, 1.0);\n"
	"\t\tif (xT2.y > 0.0)\n"
	"\t\t\talpha *= clamp(d + xT2.y + 0.5, 0.0, 1.0);\n"
	"\t}\n"
	"\tfragment_color = vec4(xD0.rgb, xD0.a * alpha);\n"
	"}\n";

/* the overlay's textures and program let go (what bg_overlay_prepare made
before one of them failed, #1081: nothing of a half-made overlay is kept or
bound) */
static void bg_overlay_release(void)
{
	bgfx_texture_handle_t *textures[5] = { &bg.overlay_atlas, &bg.overlay_icons, &bg.overlay_picture,
		&bg.overlay_plasma, &bg.overlay_medals };
	int index;

	for (index = 0; index < 5; index++)
	{
		if (BGFX_HANDLE_IS_VALID(*textures[index]))
			bgfx_destroy_texture(*textures[index]);
		*textures[index] = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
	}
	if (BGFX_HANDLE_IS_VALID(bg.overlay))
		bgfx_destroy_program(bg.overlay);
	bg.overlay = (bgfx_program_handle_t)BGFX_INVALID_HANDLE;
	bg.overlay_medals_ready = FALSE;
}

/* a texture of the overlay's replaced by a new one: the old one stays,
and is still what the overlay binds, when the new one cannot be made
(#1081: never an invalid handle bound) */
static BOOL bg_overlay_replace(bgfx_texture_handle_t *texture, bgfx_texture_handle_t replacement)
{
	if (!BGFX_HANDLE_IS_VALID(replacement))
		return FALSE;
	if (BGFX_HANDLE_IS_VALID(*texture))
		bgfx_destroy_texture(*texture);
	*texture = replacement;
	return TRUE;
}

static BOOL bg_overlay_prepare(int atlas_size, const unsigned char *icons, int icon_size, int icon_count)
{
	struct bg_shader *vertex, *fragment;
	static const char *const samplers[5] = { "atlas", "icons", "picture", "plasma", "medals" };
	int index;

	if (BGFX_HANDLE_IS_VALID(bg.overlay) || bg.overlay_failed)
		return !bg.overlay_failed;
	bg.overlay_failed = TRUE;
	bg.overlay_atlas = bg.overlay_icons = bg.overlay_picture = bg.overlay_plasma = bg.overlay_medals =
		(bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
	/* (its uniforms before its shaders, which Vulkan's finds them by) */
	bg.overlay_uniforms[0] = bgfx_create_uniform("scale_v4", BGFX_UNIFORM_TYPE_VEC4, 1);
	bg.overlay_uniforms[1] = bgfx_create_uniform("cutouts", BGFX_UNIFORM_TYPE_VEC4, 4);
	bg.overlay_uniforms[2] = bgfx_create_uniform("plasma_shift", BGFX_UNIFORM_TYPE_VEC4, 1);
	bg.overlay_uniforms[3] = bgfx_create_uniform("plasma_thread", BGFX_UNIFORM_TYPE_VEC4, 1);
	bg.overlay_uniforms[4] = bgfx_create_uniform("cellwall_parameters", BGFX_UNIFORM_TYPE_VEC4, 3);
	for (index = 0; index < 5; index++)
		bg.overlay_samplers[index] = bgfx_create_uniform(samplers[index], BGFX_UNIFORM_TYPE_SAMPLER, 1);
	vertex = bg_shader_get(bg_overlay_vertex_source, FALSE, 0, "overlay vertex", TRUE);
	fragment = bg_shader_get(bg_overlay_fragment_source, TRUE, 0, "overlay pixel", TRUE);
	if (vertex->failed || fragment->failed)
		return FALSE;
	bg.overlay = bgfx_create_program(vertex->shader, fragment->shader, false);
	bgfx_vertex_layout_begin(&bg.overlay_layout, BGFX_RENDERER_TYPE_NOOP);
	bgfx_vertex_layout_add(&bg.overlay_layout, BGFX_ATTRIB_POSITION, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
	/* (the texture coordinates in the tangent and bitangent slots: locations
	2 and 3, bg_overlay_vertex_source) */
	bgfx_vertex_layout_add(&bg.overlay_layout, BGFX_ATTRIB_TANGENT, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
	bgfx_vertex_layout_add(&bg.overlay_layout, BGFX_ATTRIB_NORMAL, 4, BGFX_ATTRIB_TYPE_FLOAT, false, false);
	bgfx_vertex_layout_add(&bg.overlay_layout, BGFX_ATTRIB_BITANGENT, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
	bgfx_vertex_layout_add(&bg.overlay_layout, BGFX_ATTRIB_COLOR0, 4, BGFX_ATTRIB_TYPE_UINT8, true, false);
	bgfx_vertex_layout_end(&bg.overlay_layout);
	bg.overlay_atlas = bgfx_create_texture_2d((uint16_t)atlas_size, (uint16_t)atlas_size, false, 1,
		BGFX_TEXTURE_FORMAT_R8, BGFX_TEXTURE_NONE, NULL, 0);
	/* (the icons, a layer each, premultiplied, with smaller levels for
	small prompts) */
	bg.overlay_icons = bgfx_create_texture_2d((uint16_t)icon_size, (uint16_t)icon_size, true, (uint16_t)icon_count,
		BGFX_TEXTURE_FORMAT_RGBA8, BGFX_TEXTURE_NONE, NULL, 0);
	if (!BGFX_HANDLE_IS_VALID(bg.overlay) || !BGFX_HANDLE_IS_VALID(bg.overlay_atlas) ||
		!BGFX_HANDLE_IS_VALID(bg.overlay_icons))
	{
		platform_log("bgfx: the overlay's textures cannot be made, so it is not drawn");
		bg_overlay_release();
		return FALSE;
	}
	for (index = 0; index < icon_count && icons; index++)
	{
		unsigned long width = (unsigned long)icon_size, height = (unsigned long)icon_size;
		unsigned char *level = malloc(width * height * 4);
		int mip = 0;

		if (!level)
			break;
		memcpy(level, icons + (size_t)index * icon_size * icon_size * 4, width * height * 4);
		for (;;)
		{
			unsigned char *next;

			bgfx_update_texture_2d(bg.overlay_icons, (uint16_t)index, (uint8_t)mip, 0, 0, (uint16_t)width,
				(uint16_t)height, bgfx_copy(level, (uint32_t)(width * height * 4)), UINT16_MAX);
			if (width == 1 && height == 1)
				break;
			next = malloc((width > 1 ? width / 2 : 1) * (height > 1 ? height / 2 : 1) * 4);
			if (!next)
				break;
			bg_downsample(level, width, height, next);
			free(level);
			level = next;
			width = width > 1 ? width / 2 : 1;
			height = height > 1 ? height / 2 : 1;
			mip++;
		}
		free(level);
	}
	{
		static const unsigned char zero[4] = { 0, 0, 0, 0 };

		bg.overlay_picture = bgfx_create_texture_2d(1, 1, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, BGFX_TEXTURE_NONE,
			bgfx_copy(zero, 4), 0);
		bg.overlay_plasma = bgfx_create_texture_2d(1, 1, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, BGFX_TEXTURE_NONE,
			bgfx_copy(zero, 4), 0);
		bg.overlay_medals = bgfx_create_texture_2d(1, 1, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, BGFX_TEXTURE_NONE,
			bgfx_copy(zero, 4), 0);
		/* (every texture the overlay binds, or none of it: the stand-ins too) */
		if (!BGFX_HANDLE_IS_VALID(bg.overlay_picture) || !BGFX_HANDLE_IS_VALID(bg.overlay_plasma) ||
			!BGFX_HANDLE_IS_VALID(bg.overlay_medals))
		{
			platform_log("bgfx: the overlay's textures cannot be made, so it is not drawn");
			bg_overlay_release();
			return FALSE;
		}
	}
	bg.overlay_failed = FALSE;
	return TRUE;
}

int xgpu_bgfx_overlay_draw(const void *vertices, int count, int vertex_bytes, const unsigned char *atlas, int atlas_size,
	int dirty_top, int dirty_bottom, const unsigned char *icons, int icon_size, int icon_count,
	const unsigned char *picture, int picture_width, int picture_height, int picture_serial,
	const unsigned char *plasma, int plasma_width, int plasma_height, int plasma_serial, const float plasma_shift[8],
	const float cellwall[12], const unsigned char *medals, int medals_width, int medals_height,
	const float cutouts[4][4], int window_width, int window_height)
{
	bgfx_transient_vertex_buffer_t buffer;
	float scale[4];

	if (!bg.ready || !count || !atlas || !bg_overlay_prepare(atlas_size, icons, icon_size, icon_count) ||
		vertex_bytes != (int)bg.overlay_layout.stride ||
		bgfx_get_avail_transient_vertex_buffer((uint32_t)count, &bg.overlay_layout) < (uint32_t)count)
		return bg.overlay_medals_ready;
	if (dirty_bottom > dirty_top)
		bgfx_update_texture_2d(bg.overlay_atlas, 0, 0, 0, (uint16_t)dirty_top, (uint16_t)atlas_size,
			(uint16_t)(dirty_bottom - dirty_top),
			bgfx_copy(atlas + (size_t)dirty_top * atlas_size, (uint32_t)((dirty_bottom - dirty_top) * atlas_size)),
			UINT16_MAX);
	if (picture && picture_serial != bg.overlay_picture_serial)
	{
		bg_overlay_replace(&bg.overlay_picture, bgfx_create_texture_2d((uint16_t)picture_width,
			(uint16_t)picture_height, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, BGFX_TEXTURE_NONE,
			bgfx_copy(picture, (uint32_t)(picture_width * picture_height * 4)), 0));
		bg.overlay_picture_serial = picture_serial;
	}
	/* (the plasma's noise map, while a plasma is drawn: the Guide's) */
	if (plasma && plasma_serial != bg.overlay_plasma_serial)
	{
		bg_overlay_replace(&bg.overlay_plasma, bgfx_create_texture_2d((uint16_t)plasma_width,
			(uint16_t)plasma_height, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, BGFX_TEXTURE_NONE,
			bgfx_copy(plasma, (uint32_t)(plasma_width * plasma_height * 4)), 0));
		bg.overlay_plasma_serial = plasma_serial;
	}
	/* (the medals' atlas, once a medal is drawn: with smaller levels, as
	the icons have) */
	if (medals && !bg.overlay_medals_ready && medals_width > 0 && medals_height > 0)
	{
		unsigned long width = (unsigned long)medals_width, height = (unsigned long)medals_height;
		unsigned char *level = malloc(width * height * 4);
		int mip = 0;

		bg.overlay_medals_ready = TRUE;
		/* (the stand-in stays bound if the atlas cannot be made: the medals
		then draw blank, and nothing binds an invalid handle) */
		if (!bg_overlay_replace(&bg.overlay_medals, bgfx_create_texture_2d((uint16_t)medals_width,
			(uint16_t)medals_height, true, 1, BGFX_TEXTURE_FORMAT_RGBA8, BGFX_TEXTURE_NONE, NULL, 0)))
		{
			platform_log("bgfx: the medals' atlas cannot be made; medals are not drawn");
			free(level);
			level = NULL;
		}
		if (level)
			memcpy(level, medals, width * height * 4);
		while (level)
		{
			unsigned char *next;

			bgfx_update_texture_2d(bg.overlay_medals, 0, (uint8_t)mip, 0, 0, (uint16_t)width, (uint16_t)height,
				bgfx_copy(level, (uint32_t)(width * height * 4)), UINT16_MAX);
			if (width == 1 && height == 1)
				break;
			next = malloc((width > 1 ? width / 2 : 1) * (height > 1 ? height / 2 : 1) * 4);
			if (next)
				bg_downsample(level, width, height, next);
			free(level);
			level = next;
			width = width > 1 ? width / 2 : 1;
			height = height > 1 ? height / 2 : 1;
			mip++;
		}
		free(level);
	}
	bgfx_alloc_transient_vertex_buffer(&buffer, (uint32_t)count, &bg.overlay_layout);
	memcpy(buffer.data, vertices, (size_t)count * (size_t)vertex_bytes);
	scale[0] = 2.0f / (float)window_width;
	scale[1] = -2.0f / (float)window_height;
	scale[2] = scale[3] = 0.0f;
	bgfx_set_uniform(bg.overlay_uniforms[0], scale, 1);
	bgfx_set_uniform(bg.overlay_uniforms[1], cutouts, 4);
	bgfx_set_uniform(bg.overlay_uniforms[2], plasma_shift, 1);
	bgfx_set_uniform(bg.overlay_uniforms[3], plasma_shift + 4, 1);
	bgfx_set_uniform(bg.overlay_uniforms[4], cellwall, 3);
	bgfx_set_texture(0, bg.overlay_samplers[0], bg.overlay_atlas, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	bgfx_set_texture(1, bg.overlay_samplers[1], bg.overlay_icons, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	bgfx_set_texture(2, bg.overlay_samplers[2], bg.overlay_picture, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	bgfx_set_texture(3, bg.overlay_samplers[3], bg.overlay_plasma, BGFX_SAMPLER_NONE);
	bgfx_set_texture(4, bg.overlay_samplers[4], bg.overlay_medals, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	bgfx_set_transient_vertex_buffer(0, &buffer, 0, (uint32_t)count);
	bgfx_set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
		BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA), 0);
	bgfx_submit(bg.present_view, bg.overlay, 0, BGFX_DISCARD_ALL);
	return bg.overlay_medals_ready;
}

static void bg_callback_screen_shot(bgfx_callback_interface_t *self, const char *path, uint32_t width,
	uint32_t height, uint32_t pitch, bgfx_texture_format_t format, const void *data, uint32_t size, bool flip);

/* ---------- a Vulkan start that did not get going

bgfx's Vulkan is started with a note in the save root
(bgfx-vulkan.starting) that the game's first frames out after its first map
loads remove (bg_frame_out): the main menu's, or the map's a command line
starts on. The setup screen's frames never count, so a start that closes
loading that first map still leaves it. A note still there at the next start
says the last one closed, crashed or hung before them: a driver that refused a pipeline, a GPU fault, a swap chain
that never presented (game issue #1046, AMD's drivers and RADV). That start
draws with OpenGL, and the player is asked whether to stay on OpenGL for
good (platform_offer_opengl): display.renderer changes only on a yes, and a
no asks again the next time Vulkan does not get going.
HALO_BGFX_START_NOTE=off leaves the note out (a test of the renderer that
quits early). */
#define BG_START_FRAMES 60

const char *platform_save_root(void);
void platform_offer_opengl(void);

static BOOL bg_start_note_wanted(void)
{
	const char *setting = getenv("HALO_BGFX_START_NOTE");

	return !(setting && strcmp(setting, "off") == 0);
}

static void bg_start_note_path(char *path, size_t size)
{
	snprintf(path, size, "%s/bgfx-vulkan.starting", platform_save_root());
}

/* whether the last Vulkan start left its note (which is taken away: the
next start tries Vulkan again if the player asks for it) */
static BOOL bg_last_start_failed(void)
{
	char path[1024];
	FILE *file;

	bg_start_note_path(path, sizeof(path));
	if ((file = fopen(path, "rb")) == NULL)
		return FALSE;
	fclose(file);
	remove(path);
	return TRUE;
}

static void bg_start_note(void)
{
	char path[1024];
	FILE *file;

	bg_start_note_path(path, sizeof(path));
	if ((file = fopen(path, "wb")) != NULL)
	{
		fputs("bgfx's Vulkan renderer is starting; the game's first frames remove this file.\n"
			"Found at a start, it means the last start closed before them, and that start draws with OpenGL.\n", file);
		fclose(file);
	}
}

/* a frame of the game's went out to the window: the note goes at the
BG_START_FRAMES'th after the first map loaded */
static void bg_frame_out(void)
{
	static unsigned long frames;

	if (!bg_vulkan || !bg_map_seen || frames > BG_START_FRAMES)
		return;
	if (++frames == BG_START_FRAMES)
	{
		char path[1024];

		bg_start_note_path(path, sizeof(path));
		remove(path);
	}
}

/* the setup launcher's frame (launcher.c), before the game draws: bgfx
started (once; the device's start then finds it so), the window cleared to
color (0xRRGGBBAA) and the overlay's quads over it, in the picture's place
(x and y its lower left corner, as OpenGL's), and the window saved to
screenshot (a .bmp's path, or NULL). FALSE if bgfx does not start */
BOOL xgpu_bgfx_launcher_frame(unsigned int color, int x, int y, int width, int height, const char *screenshot)
{
	if (!bg.ready && !bg_initialize())
		return FALSE;
	bg_window_update();
	bgfx_set_view_frame_buffer(0, (bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE);
	bgfx_set_view_rect(0, 0, 0, (uint16_t)bg.window_width, (uint16_t)bg.window_height, 0.0f, 1.0f);
	bgfx_set_view_clear(0, BGFX_CLEAR_COLOR, color, 1.0f, 0);
	bgfx_touch(0);
	bg.present_view = 0;
#ifdef HALO_GAME_BROWSER
	ui_overlay_present(x, y, width, height, bg.window_width, bg.window_height);
#endif
	if (screenshot && *screenshot)
		bgfx_request_screen_shot((bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE, screenshot);
	bgfx_frame(BGFX_FRAME_NONE);
	/* (the game's first frame starts its views as bg_present leaves them) */
	bgfx_set_view_clear(0, BGFX_CLEAR_NONE, 0, 1.0f, 0);
	return TRUE;
}

#ifdef __APPLE__
/* Metal's compiles (the bgfx patch, tools/bgfx-patches) */
struct bgfx_mtl_stats_t
{
	uint32_t libraries, pipelines;
	double library_seconds, pipeline_seconds;
};
extern struct bgfx_mtl_stats_t bgfx_mtl_stats;
#endif

/* the GPU bgfx draws with, as its renderer names it (the bgfx patch,
tools/bgfx-patches) */
extern char bgfx_gpu_name[256];
/* whether bgfx's Vulkan renderer times each frame on the GPU (the bgfx patch
0004): only for debug.gpu_stats, which logs it. NVIDIA's driver crashed in
the frame's first timestamp on Windows (game issue #939), and nothing else
reads it */
extern int bgfx_vk_gpu_timer;

/* this frame's shader and vertex work, for debug.gpu_stats's slow frames
(D3DDevice_Present, after the frame); the next frame's starts */
static void bg_frame_account(char *text, size_t size)
{
	snprintf(text, size, "; bgfx: %lu shaders translated %.1f ms, %lu from disk %.1f ms, %lu programs, "
		"Metal %u libraries %.1f ms, %u pipelines %.1f ms; vertices converted %.1f ms",
		bg.account.translated, bg.account.translate_seconds * 1000.0, bg.account.loaded,
		bg.account.load_seconds * 1000.0, bg.account.programs, bg.account.libraries,
		bg.account.library_seconds * 1000.0, bg.account.pipelines, bg.account.pipeline_seconds * 1000.0,
		bg.account.vertex_seconds * 1000.0);
	memset(&bg.account, 0, sizeof(bg.account));
}

/* with vsync off, whether this frame goes to the window: a window's
picture waits for the display (Metal hands out the next one only once the
display has taken one, about 3 behind), so frames faster than the display
draw the game but go to the window only as often as the display shows them,
leaving the game to run faster as the OpenGL path does */
static double bg_window_next, bg_window_interval = -1.0;

/* the display's frame interval, in seconds */
static double bg_display_interval(void)
{
	if (bg_window_interval < 0.0)
	{
		const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(platform_sdl_window()));

		bg_window_interval = mode && mode->refresh_rate > 0.0f ? 1.0 / mode->refresh_rate : 1.0 / 60.0;
	}
	return bg_window_interval;
}

static BOOL bg_window_due(void)
{
	double now, next = bg_window_next, interval;

	if (bg.reset_flags & BGFX_RESET_VSYNC)
		return TRUE;
	interval = bg_display_interval();
	now = stats_seconds();
	if (now < next)
		return FALSE;
	/* (a frame more than an interval late starts the count again) */
	bg_window_next = now - next > interval ? now + interval : next + interval;
	return TRUE;
}

/* after a frame went to the window: when it waited for the display, the
next goes an interval after the display took one (the display's own
timing, which the count above drifts from) */
static void bg_window_presented(double start, double end)
{
	if (!(bg.reset_flags & BGFX_RESET_VSYNC) && end - start > bg_window_interval * 0.25)
		bg_window_next = end + bg_window_interval;
}

/* whether the window can't be seen (bg_present's pacing): SDL's flags, and
on macOS the window's occlusion as bgfx's Metal reads it, which SDL does
not always report */
/* (a renderer measurement: debug.frame_times is set, so a covered window's
frames are not paced to the display, as the OpenGL path's are not) */
static BOOL bg_measuring(void)
{
	static int measuring = -1;

	if (measuring < 0)
		measuring = *config_string("debug.frame_times") != 0;
	return measuring != 0;
}

static BOOL bg_window_hidden(void)
{
#ifdef __APPLE__
	int macos_window_occluded(void *window);

	if (macos_window_occluded(SDL_GetPointerProperty(SDL_GetWindowProperties(bg.window),
		SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL)))
		return TRUE;
#endif
	return (SDL_GetWindowFlags(bg.window) & (SDL_WINDOW_OCCLUDED | SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0;
}

#ifdef HALO_CAPTURE
/* (the path bgfx is asked for a player's screenshot by, which its callback
knows as no file's) */
static const char bg_shot_path[] = "capture:screenshot";

/* a player's screenshot: the window as this frame shows it, the overlay's
screens, DELTA and the toasts on the picture, asked of bgfx (letterbox: the
picture's part of the window, from its top left, never empty: bg_present
keeps the ask until a frame has one) */
static void bg_shot_start(const int letterbox[4])
{
	memcpy(bg.shot_crop, letterbox, sizeof(bg.shot_crop));
	bgfx_request_screen_shot((bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE, bg_shot_path);
	bg.shot_pending = TRUE;
	bg.shot_frames = 0;
}

/* bgfx's screenshot of the window (bg_callback_screen_shot, as bgfx_frame
draws): the picture's part copied out, its rows from the top */
static void bg_shot_taken(uint32_t width, uint32_t height, uint32_t pitch, bgfx_texture_format_t format,
	const void *data, bool flip)
{
	long x = bg.shot_crop[0], y = bg.shot_crop[1], crop_width = bg.shot_crop[2], crop_height = bg.shot_crop[3];
	long row;
	unsigned char *pixels;

	if (!bg.shot_pending || bg.shot_pixels)
		return;
	if (format != BGFX_TEXTURE_FORMAT_BGRA8 && format != BGFX_TEXTURE_FORMAT_RGBA8)
	{
		platform_log("screenshot: the window's format (%d) is not one a screenshot can be made of", (int)format);
		return;
	}
	/* (the window may have changed size since the ask) */
	if (x < 0 || y < 0 || x + crop_width > (long)width || y + crop_height > (long)height)
	{
		x = y = 0;
		crop_width = (long)width;
		crop_height = (long)height;
	}
	pixels = malloc((size_t)crop_width * (size_t)crop_height * 4);
	if (!pixels)
		return;
	for (row = 0; row < crop_height; row++)
	{
		long source = flip ? (long)height - 1 - (y + row) : y + row;

		memcpy(pixels + row * crop_width * 4, (const unsigned char *)data + source * (long)pitch + x * 4,
			(size_t)crop_width * 4);
	}
	bg.shot_pixels = pixels;
	bg.shot_width = crop_width;
	bg.shot_height = crop_height;
	bg.shot_bgra = format == BGFX_TEXTURE_FORMAT_BGRA8;
}

#endif
#ifdef HALO_CAPTURE
/* a clip's frame, when one is due and a buffer is free (0 free, 1 being
read, 2 lent) */
static void bg_clip_start(void)
{
	struct bg_target *back_buffer = bg_target_get(&device.back_buffer);
	long width, height;
	double time;
	int index;

	for (index = 0; index < 3; index++)
	{
		if (bg.clip[index].state == 2 && __atomic_load_n(&bg.clip[index].released, __ATOMIC_ACQUIRE))
			bg.clip[index].state = 0;
	}
	if (!back_buffer || !capture_clip_frame_due((long)back_buffer->pixel_width, (long)back_buffer->pixel_height,
		&width, &height, &time))
	{
		return;
	}
	for (index = 0; index < 3 && bg.clip[index].state; index++)
		;
	if (index == 3)
		return;
	if (!BGFX_HANDLE_IS_VALID(bg.clip[index].texture) || bg.clip[index].width != back_buffer->pixel_width ||
		bg.clip[index].height != back_buffer->pixel_height)
	{
		if (BGFX_HANDLE_IS_VALID(bg.clip[index].texture))
			bgfx_destroy_texture(bg.clip[index].texture);
		bg.clip[index].width = back_buffer->pixel_width;
		bg.clip[index].height = back_buffer->pixel_height;
		bg.clip[index].texture = bgfx_create_texture_2d((uint16_t)bg.clip[index].width,
			(uint16_t)bg.clip[index].height, false, 1, BGFX_TEXTURE_FORMAT_BGRA8,
			BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK, NULL, 0);
		free(bg.clip[index].pixels);
		bg.clip[index].pixels = malloc(bg.clip[index].width * bg.clip[index].height * 4);
	}
	if (!bg.clip[index].pixels || !BGFX_HANDLE_IS_VALID(bg.clip[index].texture))
		return;
	bg_blit_2d(bg.present_view, bg.clip[index].texture, 0, back_buffer->texture, (uint16_t)bg.clip[index].width,
		(uint16_t)bg.clip[index].height);
	bg.clip[index].ready = bg_read_texture(bg.clip[index].texture, bg.clip[index].pixels);
	bg.clip[index].time = time;
	bg.clip[index].state = 1;
}

/* the frames bgfx has filled, oldest first, lent to the encoder */
static void bg_clip_finish(uint32_t frame_number)
{
	for (;;)
	{
		int index, oldest = -1;

		for (index = 0; index < 3; index++)
		{
			if (bg.clip[index].state == 1 && (oldest < 0 || bg.clip[index].time < bg.clip[oldest].time))
				oldest = index;
		}
		if (oldest < 0 || frame_number < bg.clip[oldest].ready)
			return;
		bg.clip[oldest].state = capture_clip_frame_lend(bg.clip[oldest].pixels, (long)bg.clip[oldest].width,
			(long)bg.clip[oldest].height, (long)bg.clip[oldest].width * 4, TRUE, bg.clip[oldest].time,
			&bg.clip[oldest].released) ? 2 : 0;
	}
}

#endif

static void bg_present(void)
{
	double frame_start, frame_end;
	uint32_t frame_number;
	BOOL window;
	static long screenshot_every = -1;
	int letterbox[4] = { 0, 0, 0, 0 };

	if (screenshot_every < 0)
		screenshot_every = *config_string("debug.screenshot_directory") ? config_integer("debug.screenshot_every") : 0;
	bg_warm();
	bg.present_view = (unsigned short)(bg.view + 1);
	if ((window = bg_window_due()) != FALSE)
	{
		bg_blit(bg.present_view, letterbox);
#ifdef HALO_GAME_BROWSER
		/* the overlay's screens, over the picture at the window's resolution
		(ui_overlay.c; its y from the window's bottom, as OpenGL's) */
		ui_overlay_present(letterbox[0], bg.window_height - (letterbox[1] + letterbox[3]), letterbox[2], letterbox[3],
			bg.window_width, bg.window_height);
#endif
	}
	/* (debug.test_input's shot too, as the window: xinput_sdl.c) */
	if (test_screenshot_requested && *config_string("debug.screenshot_directory"))
	{
		char path[512];

		test_screenshot_requested = FALSE;
		snprintf(path, sizeof(path), "%s/window%05lu.bmp", config_string("debug.screenshot_directory"), device.frame);
		bgfx_request_screen_shot((bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE, path);
	}
	if (screenshot_every > 0 && device.frame % (unsigned long)screenshot_every == 0)
	{
		char path[512];

		/* the game's frame, as the OpenGL path's (frameNNNNN.bmp: copied from
		the back buffer and read back once the GPU has it), or with
		debug.screenshot_window the window as shown (windowNNNNN.bmp) */
		struct bg_target *back_buffer = bg_target_get(&device.back_buffer);

		if (config_boolean("debug.screenshot_window") || !back_buffer || bg.capture_pending)
		{
			snprintf(path, sizeof(path), "%s/window%05lu.bmp", config_string("debug.screenshot_directory"), device.frame);
			bgfx_request_screen_shot((bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE, path);
		}
		else
		{
			if (!BGFX_HANDLE_IS_VALID(bg.capture) || bg.capture_width != back_buffer->pixel_width ||
				bg.capture_height != back_buffer->pixel_height)
			{
				if (BGFX_HANDLE_IS_VALID(bg.capture))
					bgfx_destroy_texture(bg.capture);
				bg.capture_width = back_buffer->pixel_width;
				bg.capture_height = back_buffer->pixel_height;
				bg.capture = bgfx_create_texture_2d((uint16_t)bg.capture_width, (uint16_t)bg.capture_height, false, 1,
					BGFX_TEXTURE_FORMAT_BGRA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK, NULL, 0);
				free(bg.capture_pixels);
				bg.capture_pixels = malloc(bg.capture_width * bg.capture_height * 4);
			}
			if (bg.capture_pixels && BGFX_HANDLE_IS_VALID(bg.capture))
			{
				bg_blit_2d(bg.present_view, bg.capture, 0, back_buffer->texture, (uint16_t)bg.capture_width,
					(uint16_t)bg.capture_height);
				bg.capture_ready = bg_read_texture(bg.capture, bg.capture_pixels);
				snprintf(bg.capture_path, sizeof(bg.capture_path), "%s/frame%05lu.bmp",
					config_string("debug.screenshot_directory"), device.frame);
				bg.capture_pending = TRUE;
			}
		}
	}
#ifdef HALO_CAPTURE
	/* (a player's screenshot: the window as shown, so only a frame that
	goes to it with a picture, which bg_blit leaves out of a minimized
	window's; the ask waits for one that does) */
	if (window && letterbox[2] > 0 && letterbox[3] > 0 && !bg.shot_pending && capture_screenshot_take())
		bg_shot_start(letterbox);
	bg_clip_start();
#endif
	frame_start = stats_seconds();
	{
#ifdef __APPLE__
		struct bgfx_mtl_stats_t before = bgfx_mtl_stats;
#endif

		frame_number = bgfx_frame(BGFX_FRAME_NONE);
		bg_frame_out();
#ifdef __APPLE__
		bg.account.libraries += bgfx_mtl_stats.libraries - before.libraries;
		bg.account.pipelines += bgfx_mtl_stats.pipelines - before.pipelines;
		bg.account.library_seconds += bgfx_mtl_stats.library_seconds - before.library_seconds;
		bg.account.pipeline_seconds += bgfx_mtl_stats.pipeline_seconds - before.pipeline_seconds;
#endif
	}
	frame_end = stats_seconds();
	bg.stat_frame_wait += frame_end - frame_start;
	if (window)
		bg_window_presented(frame_start, frame_end);
	/* (a covered or minimized window: bgfx shows nothing then, so nothing
	waits for the display, and the game would run as fast as it can; the
	frames are paced to the display's rate here instead) */
	if (bg_window_hidden() && !bg_measuring())
	{
		static double hidden_next;
		double interval = bg_display_interval(), now = stats_seconds();

		if (hidden_next > now && hidden_next - now <= interval)
			SDL_DelayNS((Uint64)((hidden_next - now) * 1e9));
		hidden_next = (hidden_next > now ? hidden_next : now) + interval;
	}
	if (frame_number >= bg.capture_ready && bg.capture_pending)
	{
		bg_callback_screen_shot(NULL, bg.capture_path, (uint32_t)bg.capture_width, (uint32_t)bg.capture_height,
			(uint32_t)(bg.capture_width * 4), BGFX_TEXTURE_FORMAT_BGRA8, bg.capture_pixels,
			(uint32_t)(bg.capture_width * bg.capture_height * 4), false);
		bg.capture_pending = FALSE;
	}
#ifdef HALO_CAPTURE
	if (bg.shot_pending && bg.shot_pixels)
	{
		/* (the pixels are capture.c's now: the next gets new ones) */
		capture_screenshot_submit(bg.shot_pixels, bg.shot_width, bg.shot_height, bg.shot_width * 4, bg.shot_bgra,
			FALSE);
		bg.shot_pixels = NULL;
		bg.shot_pending = FALSE;
	}
	else if (bg.shot_pending && ++bg.shot_frames > 8)
	{
		/* (bgfx draws as each frame is submitted, so it comes with the
		frame; a window it could not read never does) */
		platform_log("screenshot: the renderer gave no picture");
		capture_notice("Screenshot not saved", "The renderer gave no picture");
		bg.shot_pending = FALSE;
	}
	bg_clip_finish(frame_number);
#endif
	if (debug_settings.statistics && device.frame % 60 == 0)
	{
		const bgfx_stats_t *stats = bgfx_get_stats();
		double gpu = stats->gpuTimerFreq ? (double)(stats->gpuTimeEnd - stats->gpuTimeBegin) * 1000.0 /
			(double)stats->gpuTimerFreq : 0.0;
		double cpu = stats->cpuTimerFreq ? (double)stats->cpuTimeFrame * 1000.0 / (double)stats->cpuTimerFreq : 0.0;

		platform_log("bgfx frame %lu: %lu draws, %lu skipped (%lu for transient room), %u views, %lu KB uniforms; "
			"GPU %.2f ms, frame %.2f ms; %lu visibility tests, %lu seen, %lu waited; in bgfx_frame %.2f ms; vertices %lu cached, %lu streamed, "
			"cache %lu MB", device.frame, bg.drawn / 60, bg.skipped / 60, bg.stat_starved / 60,
			(unsigned)bg.present_view + 1, bg.uniform_bytes / 1024, gpu, cpu, bg.stat_tests / 60, bg.stat_seen / 60,
			bg.stat_waited / 60, bg.stat_frame_wait * 1000.0 / 60.0, bg.stat_cached_vertices / 60,
			bg.stat_streamed_vertices / 60, bg.vertex_cache_bytes >> 20);
		bg.stat_frame_wait = 0.0;
		bg.stat_cached_vertices = bg.stat_streamed_vertices = 0;
		bg.drawn = bg.skipped = bg.stat_starved = bg.stat_tests = bg.stat_seen = bg.stat_waited = 0;
	}
	platform_video_swap();
	bg_window_update();
	if (bg.frame % 60 == 0)
		bg_vertex_cache_trim();
	/* the next frame's */
	bg.frame++;
	bg.uniform_bytes = 0;
	bg.view = 0;
	bg.view_used = FALSE;
	bgfx_set_view_mode(0, BGFX_VIEW_MODE_SEQUENTIAL);
	bgfx_set_view_clear(0, BGFX_CLEAR_NONE, 0, 1.0f, 0);
}

/* ---------- bgfx's callbacks: its errors to the log, screenshots as BMP */

static void bg_callback_fatal(bgfx_callback_interface_t *self, const char *file, uint16_t line, bgfx_fatal_t code,
	const char *text)
{
	(void)self;
	platform_log("bgfx: fatal %d at %s:%u: %s", (int)code, file, (unsigned)line, text);
	if (code != BGFX_FATAL_DEBUG_CHECK)
		abort();
}

static void bg_callback_trace(bgfx_callback_interface_t *self, const char *file, uint16_t line, const char *format,
	va_list arguments)
{
	(void)self;
	(void)file;
	(void)line;
	(void)format;
	(void)arguments;
}

static void bg_callback_profiler_begin(bgfx_callback_interface_t *self, const char *name, uint32_t abgr,
	const char *file, uint16_t line)
{
	(void)self, (void)name, (void)abgr, (void)file, (void)line;
}

static void bg_callback_profiler_end(bgfx_callback_interface_t *self)
{
	(void)self;
}

/* bgfx's own cache: Vulkan's pipelines (each draw state's, made the first
time it is drawn, a hitch of tens of milliseconds on some drivers), kept
beside the shaders as <hash>.pipeline so that the next run makes them from
what the driver made the last time. The driver checks the data is its own
(the device's and the driver's versions are in it) and makes the pipeline
from scratch when it is not. */
static void bg_pipeline_path(char *path, size_t size, uint64_t id)
{
	snprintf(path, size, "%s/%016llx.pipeline", bg_jobs.folder, (unsigned long long)id);
}

static uint32_t bg_callback_cache_read_size(bgfx_callback_interface_t *self, uint64_t id)
{
	char path[1100];
	SDL_PathInfo info;

	(void)self;
	if (!*bg_jobs.folder)
		return 0;
	bg_pipeline_path(path, sizeof(path), id);
	return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE && info.size < (64U << 20) ?
		(uint32_t)info.size : 0;
}

static bool bg_callback_cache_read(bgfx_callback_interface_t *self, uint64_t id, void *data, uint32_t size)
{
	char path[1100];
	FILE *file;
	bool read;

	(void)self;
	if (!*bg_jobs.folder)
		return false;
	bg_pipeline_path(path, sizeof(path), id);
	if (!(file = fopen(path, "rb")))
		return false;
	read = fread(data, 1, size, file) == size;
	fclose(file);
	return read;
}

static void bg_callback_cache_write(bgfx_callback_interface_t *self, uint64_t id, const void *data, uint32_t size)
{
	char path[1100], temporary[1120];
	FILE *file;

	(void)self;
	if (!*bg_jobs.folder || !size)
		return;
	bg_pipeline_path(path, sizeof(path), id);
	snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	if (!(file = fopen(temporary, "wb")))
		return;
	{
		BOOL written = fwrite(data, 1, size, file) == size;

		if (fclose(file) == 0 && written)
			rename(temporary, path);
		else
			remove(temporary);
	}
}

static void bg_callback_screen_shot(bgfx_callback_interface_t *self, const char *path, uint32_t width,
	uint32_t height, uint32_t pitch, bgfx_texture_format_t format, const void *data, uint32_t size, bool flip)
{
	unsigned char header[54] = { 'B', 'M' };
	unsigned long image_size = (unsigned long)width * height * 4, row, column;
	FILE *file;

	(void)self, (void)format, (void)size;
#ifdef HALO_CAPTURE
	if (path && !strcmp(path, bg_shot_path))
	{
		bg_shot_taken(width, height, pitch, format, data, flip);
		return;
	}
#endif
	file = fopen(path, "wb");
	if (!file)
		return;
	*(unsigned int *)(header + 2) = (unsigned int)(54 + image_size);
	*(unsigned int *)(header + 10) = 54;
	*(unsigned int *)(header + 14) = 40;
	*(int *)(header + 18) = (int)width;
	*(int *)(header + 22) = flip ? (int)height : -(int)height;
	*(unsigned short *)(header + 26) = 1;
	*(unsigned short *)(header + 28) = 32;
	*(unsigned int *)(header + 34) = (unsigned int)image_size;
	fwrite(header, 1, sizeof(header), file);
	for (row = 0; row < height; row++)
	{
		unsigned char line[4 * 8192];
		const unsigned char *source = (const unsigned char *)data + row * pitch;

		for (column = 0; column < width && column < 8192; column++)
		{
			line[column * 4] = source[column * 4];
			line[column * 4 + 1] = source[column * 4 + 1];
			line[column * 4 + 2] = source[column * 4 + 2];
			line[column * 4 + 3] = 0xff;
		}
		fwrite(line, 4, width, file);
	}
	fclose(file);
	platform_log("bgfx: screenshot %s", path);
}

static void bg_callback_capture_begin(bgfx_callback_interface_t *self, uint32_t width, uint32_t height,
	uint32_t pitch, bgfx_texture_format_t format, bool flip)
{
	(void)self, (void)width, (void)height, (void)pitch, (void)format, (void)flip;
}

static void bg_callback_capture_end(bgfx_callback_interface_t *self)
{
	(void)self;
}

static void bg_callback_capture_frame(bgfx_callback_interface_t *self, const void *data, uint32_t size)
{
	(void)self, (void)data, (void)size;
}

static const bgfx_callback_vtbl_t bg_callback_table =
{
	bg_callback_fatal, bg_callback_trace, bg_callback_profiler_begin, bg_callback_profiler_begin,
	bg_callback_profiler_end, bg_callback_cache_read_size, bg_callback_cache_read, bg_callback_cache_write,
	bg_callback_screen_shot, bg_callback_capture_begin, bg_callback_capture_end, bg_callback_capture_frame,
};
static bgfx_callback_interface_t bg_callback = { &bg_callback_table };

/* ---------- start-up */

/* the renderer=... name of what draws (d3d8_gl.c's device creation) */
static const char *bg_renderer_name(void)
{
	switch (bgfx_get_renderer_type())
	{
	case BGFX_RENDERER_TYPE_METAL: return "bgfx-metal";
	case BGFX_RENDERER_TYPE_VULKAN: return "bgfx-vulkan";
	case BGFX_RENDERER_TYPE_DIRECT3D11: return "bgfx-d3d11";
	case BGFX_RENDERER_TYPE_DIRECT3D12: return "bgfx-d3d12";
	default: return "bgfx";
	}
}

#ifndef __APPLE__
#include "bgfx_vulkan_check.inl"
#endif

/* bgfx started on the renderer of type, in the window (width by height
pixels): whether it did */
/* the depth buffers' formats, best first: the game's stencil needs D24S8
(bgfx makes it D32_SFLOAT_S8_UINT on a Vulkan device without
D24_UNORM_S8_UINT, AMD's: tools/bgfx-patches/0004), and the others draw
without one (the stencil's tests are left out, bg_state) */
static const struct
{
	bgfx_texture_format_t format;
	const char *name;
	BOOL stencil;
} bg_depth_formats[] = {
	{ BGFX_TEXTURE_FORMAT_D24S8, "D24S8", TRUE },
	{ BGFX_TEXTURE_FORMAT_D32F, "D32F", FALSE },
	{ BGFX_TEXTURE_FORMAT_D24, "D24", FALSE },
	{ BGFX_TEXTURE_FORMAT_D32, "D32", FALSE },
	{ BGFX_TEXTURE_FORMAT_D24F, "D24F", FALSE },
	{ BGFX_TEXTURE_FORMAT_D16, "D16", FALSE },
};

/* whether bgfx's renderer draws into a texture of the format: its caps, less
those HALO_BGFX_UNSUPPORTED names (a test's, "D24S8,D32F" say, to try the
fallbacks on a GPU that has them) */
static BOOL bg_format_drawable(const bgfx_caps_t *caps, bgfx_texture_format_t format, const char *name)
{
	const char *unsupported = getenv("HALO_BGFX_UNSUPPORTED");

	if (unsupported && *unsupported)
	{
		size_t length = strlen(name);
		const char *at;

		for (at = unsupported; (at = strstr(at, name)) != NULL; at += length)
		{
			if ((at == unsupported || at[-1] == ',') && (at[length] == ',' || at[length] == '\0'))
				return FALSE;
		}
	}
	return (caps->formats[format] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
}

/* the targets' formats from the renderer's caps (bg.depth_format); FALSE
when it draws into none of them, which leaves the game to OpenGL */
static BOOL bg_formats_choose(void)
{
	const bgfx_caps_t *caps = bgfx_get_caps();
	size_t index;

	if (!bg_format_drawable(caps, BGFX_TEXTURE_FORMAT_BGRA8, "BGRA8"))
	{
		platform_log("bgfx: %s draws into no BGRA8 targets, so OpenGL", bgfx_get_renderer_name(bgfx_get_renderer_type()));
		return FALSE;
	}
	for (index = 0; index < sizeof(bg_depth_formats) / sizeof(bg_depth_formats[0]); index++)
	{
		if (!bg_format_drawable(caps, bg_depth_formats[index].format, bg_depth_formats[index].name))
			continue;
		bg.depth_format = bg_depth_formats[index].format;
		bg.depth_stencil = bg_depth_formats[index].stencil;
		if (index)
			platform_log("bgfx: depth buffers are %s here (no D24S8)%s", bg_depth_formats[index].name,
				bg.depth_stencil ? "" : ", without a stencil");
		return TRUE;
	}
	platform_log("bgfx: %s draws into no depth buffer format, so OpenGL",
		bgfx_get_renderer_name(bgfx_get_renderer_type()));
	return FALSE;
}

static BOOL bg_start(bgfx_renderer_type_t type, int width, int height)
{
	bgfx_init_t init;

	/* (drawn on this thread: bgfx renders as each frame is submitted) */
	bgfx_render_frame(-1);
	bgfx_init_ctor(&init);
	init.type = type;
	{
		SDL_PropertiesID properties = SDL_GetWindowProperties(bg.window);

#if defined(__APPLE__)
		init.swapChain.nwh = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
#elif defined(_WIN32)
		init.swapChain.nwh = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#else
		if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0)
		{
			init.platformData.type = BGFX_NATIVE_WINDOW_HANDLE_TYPE_WAYLAND;
			init.swapChain.ndt = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
			init.swapChain.nwh = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
		}
		else
		{
			init.swapChain.ndt = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
			init.swapChain.nwh = (void *)(uintptr_t)SDL_GetNumberProperty(properties,
				SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
		}
#endif
	}
	bg_vulkan = type == BGFX_RENDERER_TYPE_VULKAN;
	bg_d3d11 = type == BGFX_RENDERER_TYPE_DIRECT3D11;
	init.swapChain.width = (uint32_t)width;
	init.swapChain.height = (uint32_t)height;
	init.reset = (uint32_t)bg_reset_flags();
	/* (only the renderer asked for: the shaders here are only its) */
	init.fallback = false;
	init.limits.maxTransientVbSize = 64u << 20;
	init.limits.maxTransientIbSize = 8u << 20;
	init.callback = &bg_callback;
	if (!init.swapChain.nwh)
	{
		platform_log("bgfx: the window has no handle for %s", bgfx_get_renderer_name(init.type));
		return FALSE;
	}
#ifndef __APPLE__
	/* (a computer without a working Vulkan, or with a driver the blocklist
	names, draws with OpenGL: bgfx_vulkan_check.inl) */
	if (bg_vulkan && !bg_vulkan_check(&init.vendorId, &init.deviceId))
		return FALSE;
	if (bg_vulkan && bg_start_note_wanted())
	{
		if (bg_last_start_failed())
		{
			platform_log("bgfx: the last Vulkan start closed before its first map drew %d frames, so OpenGL for this "
				"start", BG_START_FRAMES);
			platform_offer_opengl();
			return FALSE;
		}
		bg_start_note();
	}
#endif
#ifdef _WIN32
	/* (Direct3D 11's shaders are compiled at run time, by Windows' own
	compiler) */
	if (bg_d3d11 && !bg_dxbc_available())
		return FALSE;
#endif
	bgfx_vk_gpu_timer = config_boolean("debug.gpu_stats");
	if (!bgfx_init(&init))
	{
		platform_log("bgfx: %s does not start", bgfx_get_renderer_name(init.type));
		return FALSE;
	}
	/* (bgfx tries its other renderers when the one asked for fails: the
	shaders here are only the one's) */
	if (bgfx_get_renderer_type() != init.type)
	{
		platform_log("bgfx: %s does not start (bgfx offered %s)", bgfx_get_renderer_name(init.type),
			bgfx_get_renderer_name(bgfx_get_renderer_type()));
		bgfx_shutdown();
		return FALSE;
	}
	/* (Direct3D 11 falls back to Windows' software renderer, WARP, on a GPU
	it can't use, a feature level 10 laptop say: OpenGL is faster there.
	bgfx names it by Microsoft's IDs, 1414 and 008c, or the software
	rasterizer's) */
	if (bg_d3d11 && (bgfx_get_caps()->vendorId == BGFX_PCI_ID_SOFTWARE_RASTERIZER ||
		(bgfx_get_caps()->vendorId == 0x1414 && bgfx_get_caps()->deviceId == 0x008c) ||
		strstr(bgfx_gpu_name, "Basic Render Driver")))
	{
		platform_log("bgfx: Direct3D 11 would draw in software (%s), so OpenGL",
			*bgfx_gpu_name ? bgfx_gpu_name : "WARP");
		bgfx_shutdown();
		return FALSE;
	}
	if (!bg_formats_choose())
	{
		bgfx_shutdown();
		return FALSE;
	}
	return TRUE;
}

static BOOL bg_initialize(void)
{
	unsigned long index;
	int width = 0, height = 0;
	static const unsigned int black = 0xff000000;

	/* (started already: the setup launcher draws with it before the
	device starts, xgpu_bgfx_launcher_frame) */
	if (bg.ready)
		return TRUE;

	bg.window = platform_sdl_window();
	bg.view = 0;
	bg.blit = (bgfx_program_handle_t)BGFX_INVALID_HANDLE;
	bg.copy = (bgfx_program_handle_t)BGFX_INVALID_HANDLE;
	bg.crt = (bgfx_program_handle_t)BGFX_INVALID_HANDLE;
	bg.present_quad = (bgfx_dynamic_vertex_buffer_handle_t)BGFX_INVALID_HANDLE;
	bg.full_quad = (bgfx_vertex_buffer_handle_t)BGFX_INVALID_HANDLE;
	bg.overlay = (bgfx_program_handle_t)BGFX_INVALID_HANDLE;
	bg.capture = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
#ifdef HALO_CAPTURE
	for (index = 0; index < 3; index++)
		bg.clip[index].texture = (bgfx_texture_handle_t)BGFX_INVALID_HANDLE;
#endif
	bg.framebuffer = (bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE;
	bg.view_framebuffer = (bgfx_frame_buffer_handle_t)BGFX_INVALID_HANDLE;
	bg.frame = 1;
	SDL_GetWindowSizeInPixels(bg.window, &width, &height);

	{
		/* the renderer: Metal on macOS, Vulkan elsewhere; Direct3D 11 on
		Windows only with display.renderer = "d3d11", which is for testing.
		One that does not start leaves the game to OpenGL
		(platform_video_reopen_for_gl), with a fresh window */
		bgfx_renderer_type_t type;

#if defined(__APPLE__)
		type = BGFX_RENDERER_TYPE_METAL;
#elif defined(_WIN32)
		type = strcmp(config_string("display.renderer"), "d3d11") == 0 ? BGFX_RENDERER_TYPE_DIRECT3D11 :
			BGFX_RENDERER_TYPE_VULKAN;
#else
		type = BGFX_RENDERER_TYPE_VULKAN;
#endif
		if (!bg_start(type, width, height))
			return FALSE;
	}
	bg.window_width = width;
	bg.window_height = height;
	bg.reset_flags = bg_reset_flags();
	/* the border colors (bg_sampler_flags) */
	{
		static const float colors[3][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 1 }, { 1, 1, 1, 1 } };
		int color;

		for (color = 0; color < 3; color++)
			bgfx_set_palette_color(color, colors[color]);
	}
	{
		const bgfx_caps_t *caps = bgfx_get_caps();

		platform_log("bgfx: %s on %s (vendor %04x, device %04x), window %dx%d",
			bgfx_get_renderer_name(bgfx_get_renderer_type()), *bgfx_gpu_name ? bgfx_gpu_name : "an unnamed GPU",
			(unsigned)caps->vendorId, (unsigned)caps->deviceId, width, height);
	}

	glslang_initialize_process();
	bg_cache_start();
	{
		extern void (*cache_file_map_loaded_hook)(char const *map_name);

		cache_file_map_loaded_hook = bg_map_loaded;
	}

	bgfx_vertex_layout_begin(&bg.layout, BGFX_RENDERER_TYPE_NOOP);
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		bgfx_vertex_layout_add(&bg.layout, (bgfx_attrib_t)index, 4, BGFX_ATTRIB_TYPE_FLOAT, false, false);
	bgfx_vertex_layout_end(&bg.layout);
	{
		unsigned char corners[4 * BG_VERTEX_BYTES];

		bg_quad_corners(corners, -1.0f, 1.0f);
		bg.full_quad = bgfx_create_vertex_buffer(bgfx_copy(corners, sizeof(corners)), &bg.layout, BGFX_BUFFER_NONE);
		bg.present_quad = bgfx_create_dynamic_vertex_buffer(4, &bg.layout, BGFX_BUFFER_NONE);
		/* (none written yet: bg_blit writes the first) */
		bg.present_quad_corner[0] = bg.present_quad_corner[1] = 0.0f;
	}
	for (index = 0; index < BG_UNIFORM_COUNT; index++)
		bg.uniforms[index] = bgfx_create_uniform(bg_uniform_sources[index].name, BGFX_UNIFORM_TYPE_VEC4,
			bg_uniform_sources[index].components == 1 ? 1 : bg_uniform_sources[index].count);
	for (index = 0; index < 4; index++)
	{
		char name[8];

		snprintf(name, sizeof(name), "tex%lu", index);
		bg.samplers[index] = bgfx_create_uniform(name, BGFX_UNIFORM_TYPE_SAMPLER, 1);
	}
	for (index = 0; index < BG_QUERY_COUNT; index++)
		bg.queries[index] = bgfx_create_occlusion_query();
	/* what a stage without a texture samples: black, as OpenGL's unit
	without one */
	bg.blank = bgfx_create_texture_2d(1, 1, false, 1, BGFX_TEXTURE_FORMAT_BGRA8, BGFX_TEXTURE_NONE,
		bgfx_copy(&black, 4), 0);
	{
		static const unsigned int white = 0xffffffff;

		bg.white = bgfx_create_texture_2d(1, 1, false, 1, BGFX_TEXTURE_FORMAT_BGRA8, BGFX_TEXTURE_NONE,
			bgfx_copy(&white, 4), 0);
	}
	{
		unsigned int faces[6] = { black, black, black, black, black, black };

		bg.blank_cube = bgfx_create_texture_cube(1, false, 1, BGFX_TEXTURE_FORMAT_BGRA8, BGFX_TEXTURE_NONE,
			bgfx_copy(faces, sizeof(faces)), 0);
		bg.blank_volume = bgfx_create_texture_3d(1, 1, 1, false, BGFX_TEXTURE_FORMAT_BGRA8, BGFX_TEXTURE_NONE,
			bgfx_copy(&black, 4), 0);
	}
	{
		struct bg_shader *vertex = bg_shader_get(bg_blit_vertex_source, FALSE, 0, "blit vertex", TRUE);
		struct bg_shader *fragment = bg_shader_get(bg_blit_fragment_source, TRUE, 0, "blit pixel", TRUE);

		if (!vertex->failed && !fragment->failed)
			bg.blit = bgfx_create_program(vertex->shader, fragment->shader, false);
		fragment = bg_shader_get(bg_copy_fragment_source, TRUE, 0, "copy pixel", TRUE);
		if (!vertex->failed && !fragment->failed)
			bg.copy = bgfx_create_program(vertex->shader, fragment->shader, false);
		for (index = 0; index < 3; index++)
		{
			char name[24];

			snprintf(name, sizeof(name), "crt_parameters%lu", index + 1);
			bg.crt_parameters[index] = bgfx_create_uniform(name, BGFX_UNIFORM_TYPE_VEC4, 1);
		}
		fragment = bg_shader_get(bg_crt_fragment_source, TRUE, 0, "CRT filter pixel", TRUE);
		if (!vertex->failed && !fragment->failed)
			bg.crt = bgfx_create_program(vertex->shader, fragment->shader, false);
	}

	/* (gl_initialize's: the guest memory writes that make textures stale,
	the attributes' w, and the debug settings) */
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		device.attributes[index][3] = 1.0f;
	memory_watch_initialize();
	debug_settings.statistics = config_boolean("debug.gpu_stats");
	halo_screen_width();
	bg.ready = TRUE;
	bgfx_set_view_mode(0, BGFX_VIEW_MODE_SEQUENTIAL);
	return TRUE;
}
