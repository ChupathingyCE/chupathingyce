/*
XGPU.H

Internals shared by the OpenGL implementation of the Xbox Direct3D API:
the NV2A shader translators (nv2a_vsh.c, nv2a_psh.c), texture decoding
(xbox_textures.c), guest memory write tracking (memory_watch.c) and the
device itself (d3d8_gl.c).
*/

#ifndef __HALO_LINUX_XGPU_H
#define __HALO_LINUX_XGPU_H

#include "platform.h"
#include "gl.h"

/* ---------- the desktop's two kinds of OpenGL

The desktop renderer is written for OpenGL 4.5, and does without what came
after 3.3 where macOS's OpenGL 4.1 lacks it (OpenGL ES has the same path):
each vertex attribute pointed at on its own (no 4.3 vertex attribute
binding), each texture unit bound on its own (no 4.4 multi-bind), mipmap
levels copied with a blit (no 4.3 glCopyImageSubData), and D3D's clip space
converted by the vertex shaders (no 4.5 glClipControl: nv2a_vsh.c).
Windows, Linux and FreeBSD take that path when the driver has no OpenGL 4.5
(an older integrated GPU's, game issue #78): sdl_platform.c then makes a
3.3 context and sets xgpu_gl_legacy (gl_functions.c), and the entry points
past 3.3 are left out, as on macOS (gl_functions_load).

XGPU_GL_POINTERS: the build has the path without them; XGPU_GL_BINDINGS:
the build has the 4.5 one; XGPU_GL_LEGACY: the path drawing now. */
#if defined(HALO_GL_ES) || defined(__APPLE__)
#define XGPU_GL_POINTERS 1
#define XGPU_GL_BINDINGS 0
#define XGPU_GL_LEGACY 1
#elif defined(HALO_CONSOLE_GL_LAYER)
#define XGPU_GL_POINTERS 0
#define XGPU_GL_BINDINGS 1
#define XGPU_GL_LEGACY 0
#else
#define XGPU_GL_POINTERS 1
#define XGPU_GL_BINDINGS 1
#define XGPU_GL_LEGACY_AT_RUN_TIME 1
extern int xgpu_gl_legacy;
#define XGPU_GL_LEGACY xgpu_gl_legacy
#endif

/* the desktop shaders' #version line: 4.5's, macOS's 4.1, or on an older
driver the newest of 4.1, 4.0 and 3.3 its context has */
#ifdef XGPU_GL_LEGACY_AT_RUN_TIME
extern const char *xgpu_gl_version_line;
#define XGPU_GL_VERSION_LINE xgpu_gl_version_line
/* (GLSL 3.30's has no gl_SampleMask: alpha tested over the samples, nv2a_psh.c) */
extern int xgpu_gl_sample_mask;
#define XGPU_GL_SAMPLE_MASK xgpu_gl_sample_mask
#elif defined(__APPLE__)
#define XGPU_GL_VERSION_LINE "#version 410 core\n"
#define XGPU_GL_SAMPLE_MASK 1
#else
#define XGPU_GL_VERSION_LINE "#version 450 core\n"
#define XGPU_GL_SAMPLE_MASK 1
#endif

#ifdef HALO_GL_ES
/* OpenGL ES features that are optional (d3d8_gl.c gl_initialize) */
struct xgpu_capabilities
{
	BOOL copy_image;
	BOOL border_clamp;
	BOOL anisotropy;
	BOOL s3tc;
	/* ES 3.2, or the EXT/OES_draw_elements_base_vertex extension: glDrawElementsBaseVertex */
	BOOL base_vertex;
	/* ES 3.1 with fragment atomic counters: exact visibility test counts */
	BOOL atomic_counters;
	/* "300 es" or "310 es" */
	const char *shading_language;
};

extern struct xgpu_capabilities xgpu_capabilities;

/* port/android/guest/runtime/guest_host.h */
int host_gl_has_extension(const char *name);
void host_gl_read_buffer(unsigned int buffer, unsigned int offset, unsigned int size, void *data);
void host_gl_buffer_write(unsigned int target, unsigned int offset, unsigned int size, const void *data);
void host_gl_fence_frame(unsigned int slot);
void host_gl_wait_frame(unsigned int slot);
#endif

/* the window's framebuffer, which the frame is presented into: 0 but on
iOS, which has no default framebuffer, and draws the window from the one
SDL makes for it (sdl_platform.c) */
#ifdef HALO_IOS
unsigned int platform_window_framebuffer(void);
#else
#define platform_window_framebuffer() 0
#endif

/* ---------- GL state

The device caches the GL state it sets for draws (d3d8_gl.c); code that
changes GL state behind it (binding a texture to upload it, deleting one)
must call this afterwards. */

void xgpu_gl_state_invalidate(void);

/* a compiled shader, or a linked program of two, or 0 with the log
written */
GLuint xgpu_compile_shader(GLenum type, const char *code, const char *what);
GLuint xgpu_link_program(GLuint vertex_shader, GLuint fragment_shader, const char *what);

/* the draws' shaders by their text (xgpu_shader_cache.c): a text compiled
before gives the same shader; program_get tells it each pair it links; and
at start-up the texts and pairs of earlier runs are all compiled and linked,
each program handed to linked() */
GLuint xgpu_shader_cache_compile(GLenum type, const char *source, const char *what);
void xgpu_shader_cache_linked(GLuint vertex_shader, GLuint fragment_shader);
void xgpu_shader_cache_warm(void (*linked)(GLuint vertex_shader, GLuint fragment_shader, GLuint program));
#ifdef HALO_SHADER_PREP
/* the shaders a map's loading screen makes (d3d8_shader_prep.inl): a text's
shader with its compile asked for and not waited on (one made before as it
is), or 0; then whether it is compiled, kept in the file as any other: 1 it
is, 0 it failed (its log written; the shader kept, as the renderer holds its
name, so that a program linked of it fails and its draws are left out, and
never saved, so that the next run compiles it again), -1 not yet (wait FALSE, on a driver that compiles in parallel). A draw that
comes to a shader first waits for it. As for a linked program's. */
extern BOOL xgpu_parallel_compile;
GLuint xgpu_shader_cache_submit(GLenum type, const char *source);
int xgpu_shader_cache_finish(GLuint shader, BOOL wait);
int xgpu_program_finish(GLuint program, BOOL wait);
#endif

/* ---------- generated source text */

struct xgpu_text
{
	char *buffer;
	unsigned long length;
	unsigned long capacity;
};

void xgpu_text_append(struct xgpu_text *text, const char *format, ...) __attribute__((format(printf, 2, 3)));

/* ---------- vertex shaders */

#define XGPU_VERTEX_ATTRIBUTE_COUNT 16
#define XGPU_VERTEX_CONSTANT_COUNT 192
/* D3D constant register -96 is hardware register 0 */
#define XGPU_VERTEX_CONSTANT_BIAS 96

/* where one of the game's model lighting programs (d3d8_gl.c
halo_vertex_shader_lighting) has the normal, and the world position, that it
lights the diffuse color by: the temporary register that holds each before
the instruction given */
struct nv2a_vertex_lighting
{
	/* 1 by the ambient and distant lights, 2 by the point lights too */
	int lights;
	unsigned long normal_instruction, normal_register;
	/* (with the point lights only) */
	unsigned long position_instruction, position_register;
};

/* finds them in a model lighting program, checking that it lights its
diffuse color as nv2a_psh.c does for each pixel; FALSE if it does not */
BOOL nv2a_vertex_shader_lighting(const DWORD *instructions, unsigned long instruction_count,
	struct nv2a_vertex_lighting *lighting);

/* GLSL for an NV2A vertex program (the instruction words after the program
header). Attributes whose bit is set in packed_attribute_mask are fed as
NORMPACKED3 32-bit integers and unpacked in the shader. With lighting (else
NULL), the normal and world position go to the pixel shader too, which
lights the diffuse color for each pixel (nv2a_pixel_shader_key
per_pixel_lighting). Returns a malloc'd string. */
/* OpenGL ES and macOS's OpenGL 4.1 have no glClipControl: vertex shaders
convert D3D's clip space themselves (nv2a_vsh.c) */
#if defined(HALO_GL_ES) || defined(__APPLE__)
#define HALO_GL_NO_CLIP_CONTROL 1
#endif

char *nv2a_vertex_shader_to_glsl(const DWORD *instructions, unsigned long instruction_count,
	unsigned long packed_attribute_mask, const struct nv2a_vertex_lighting *lighting, BOOL plasma_model);

/* ---------- pixel shaders */

enum
{
	_xgpu_sampler_none = 0,
	_xgpu_sampler_2d,
	_xgpu_sampler_3d,
	_xgpu_sampler_cube,
};

/* everything a translated pixel shader depends on (nv2a_pixel_key.h) */
#include "nv2a_pixel_key.h"

char *nv2a_pixel_shader_to_glsl(const struct nv2a_pixel_shader_key *key);

/* nv2a_finite, in the vertex and pixel shaders: a component that is not a
number made 0, by its bits (a driver's fast math may drop a float test such as
isnan). The NV2A made none, and a GPU draws one as anything: black, white or
blocks. An infinite one is kept, for the clamps after it to take to the end of
the range, as the NV2A did. A named function, so that a console that writes
the GLSL into another language can give it its own body (the Xbox 360's has no
integer bit operations) */
#define NV2A_FINITE \
	"vec4 nv2a_finite(vec4 v)\n" \
	"{\n" \
	"	return mix(v, vec4(0.0), greaterThan(floatBitsToUint(v) & uvec4(0x7fffffffu), uvec4(0x7f800000u)));\n" \
	"}\n"

#ifdef HALO_GL_ES
/* ES samplers have no LOD bias of their own */
#define XGPU_PIXEL_UNIFORMS_ES "uniform vec4 texture_lod_bias;\n"
#else
#define XGPU_PIXEL_UNIFORMS_ES ""
#endif

/* the combiner registers that live in uniforms rather than in the program:
C0/C1 of each stage and the final combiner, and texture constants */
#define XGPU_PIXEL_UNIFORMS \
	"uniform vec4 ps_c0[8];\n" \
	"uniform vec4 ps_c1[8];\n" \
	"uniform vec4 ps_final_c0;\n" \
	"uniform vec4 ps_final_c1;\n" \
	"uniform vec4 fog_color;\n" \
	"uniform vec4 fog_parameters;\n" \
	"uniform float alpha_reference;\n" \
	"uniform vec4 bump_matrix[4];\n" \
	"uniform vec4 bump_luminance[4];\n" \
	"uniform vec4 texture_scale[4];\n" \
	XGPU_PIXEL_UNIFORMS_ES

/* the vertex constants the per-pixel model lighting reads, in a uniform of
their own (the vertex shader's 192 would pass OpenGL ES's least fragment
uniform space): [0] c[-82] (the translucency in z), [1] to [11] c[-79] to
c[-69] (rasterizer_set_model_lighting's two point lights, two distant
lights and the ambient light) */
#define XGPU_MODEL_LIGHT_COUNT 12

/* ---------- textures */

struct xgpu_texture_description
{
	DWORD format;       /* D3DFMT_* */
	unsigned long width, height, depth, levels;
	BOOL cube_map;
	BOOL linear;        /* not swizzled; addressed with texel coordinates */
	BOOL pc_layout;     /* laid out as Halo PC's bitmaps (D3DCOMMON_PORT_PC_LAYOUT) */
	BOOL pc_meter;      /* a Halo PC HUD meter's channels (D3DCOMMON_PORT_PC_METER) */
	BOOL pc_multipurpose; /* a Halo PC multipurpose map's channels (D3DCOMMON_PORT_PC_MULTIPURPOSE) */
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

/* the GL texture for an Xbox texture header, uploading or refreshing it
from guest memory as needed; *target receives GL_TEXTURE_2D etc. */
GLuint xgpu_texture_get(const DWORD *resource, const D3DCOLOR *palette, GLenum *target,
	struct xgpu_texture_description *description);
void xgpu_texture_cache_begin_frame(void);
/* for a port whose GL can run short of texture memory (the Xbox 360's):
drops the textures not drawn in the last 'frames' frames (at least one; the
next draw that needs one uploads it again), between frames, and returns how
many it dropped */
unsigned long xgpu_texture_cache_drop_idle(unsigned long frames);
/* has the GL texture 'texture' uploaded again the next time it is drawn (the
port could not make it, and has freed memory since) */
void xgpu_texture_cache_reload(GLuint texture);
/* the same for every texture the port says it is missing (more could not be
made than it kept a list of); returns how many */
unsigned long xgpu_texture_cache_reload_missing(int (*missing)(GLuint texture));
/* (not on the consoles: HALO_CONSOLE_NO_GUIDE) a plain 2D texture's first
level, read for the port's own drawing (the Guide's plasma: ui_overlay.h),
as RGBA bytes with rows from the top; NULL for one it does not read (a cube,
a volume, a palette's, larger than 1024 square) or without memory. The
caller frees it */
unsigned char *xgpu_texture_rgba(const DWORD *resource, unsigned long *width, unsigned long *height);

/* MCC-owned immutable resources never enter the guest physical texture cache. */
BOOL xgpu_mcc_texture_get(DWORD const *resource, D3DCOLOR const *palette,
	GLuint *texture, GLenum *target, struct xgpu_texture_description *description);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define xgpu_mcc_texture_get(a0, a1, a2, a3, a4) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (BOOL)0)
#endif
BOOL xgpu_mcc_texture_upload(GLuint texture, GLenum target,
	struct xgpu_texture_description const *description, void const *pixels,
	unsigned long bytes, D3DCOLOR const *palette);
void xgpu_mcc_texture_unbind(DWORD const *resource);
/* an MCC header's description and immutable pixels, for a renderer that
uploads its own copies (the bgfx renderer); FALSE if resource is not one */
BOOL xgpu_mcc_texture_source(DWORD const *resource, struct xgpu_texture_description *description,
	void const **pixels, unsigned long *bytes, long *hud_asset);
/* an MCC texture a renderer could not make, and why: logged (the first few,
then the count when the map's textures go: mcc_texture_bridge.c) */
void xgpu_mcc_texture_failed(struct xgpu_texture_description const *description, char const *reason);
#if defined(HALO_MCC) && !HALO_MCC
#define xgpu_mcc_texture_source(a0, a1, a2, a3, a4) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (BOOL)0)
#define xgpu_mcc_texture_failed(a0, a1) ((void)sizeof(a0), (void)sizeof(a1))
#endif

/* ---------- render targets */

struct xgpu_render_target
{
	unsigned long data;  /* physical address */
	unsigned long width, height;
	BOOL depth;
	GLuint texture;
	/* pixels per unit of width and height: more than 1 for the screen's
	targets when the game draws at the display's resolution (d3d8_gl.c) */
	float scale[2];
	unsigned long gl_width, gl_height;
	/* with multisampling, the multisampled renderbuffer draws go to, its
	samples a pixel (0 when it has none), the samples the GPU gave it (it
	may give more than were asked for), and whether it has been drawn into
	since the texture last had its pixels (d3d8_gl.c,
	render_target_multisample) */
	GLuint multisample;
	int samples;
	int gl_samples;
	BOOL unresolved;
	/* changes whenever the target is drawn into or cleared (d3d8_gl.c,
	bind_targets) */
	unsigned long written;
};

/* the GL texture holding a render target with this physical address, or 0 */
struct xgpu_render_target *xgpu_render_target_find(unsigned long data);

#if defined(HALO_BGFX) || defined(HALO_CONSOLE_NATIVE_RENDERER) || defined(HALO_VULKAN)
/* a level of a texture that is not compressed, as 32-bit ARGB words in the
processor's order (B, G, R, A in memory on a little-endian one); FALSE
without the memory for it */
BOOL xgpu_texture_decode_level(const struct xgpu_texture_description *description, unsigned long level,
	const unsigned char *source, const D3DCOLOR *palette, unsigned long *destination);
/* a compressed texture's DXT kind (1, 3 or 5; 0 for any other), and the
bytes of one of its levels */
int xgpu_texture_dxt(const struct xgpu_texture_description *description);
unsigned long xgpu_texture_level_bytes(const struct xgpu_texture_description *description, unsigned long level);
#endif

#ifdef HALO_VULKAN
/* the Android app's Vulkan backend draws (display.renderer = "vulkan";
d3d8_vk.inl): set as the window is made, so that nothing here makes an
OpenGL context or calls OpenGL; cleared when it could not start */
extern BOOL xgpu_vulkan;
/* the textures the port's own modules make (hud_hires.c, text_hires.c,
menu_files.c), as xgpu_bgfx_texture_create's: an image's number */
unsigned int xgpu_vulkan_texture_create(unsigned long width, unsigned long height, const unsigned char *rgba,
	BOOL mipmapped);
void xgpu_vulkan_texture_rows(unsigned int name, unsigned long top, unsigned long width, unsigned long rows,
	const unsigned char *rgba);
void xgpu_vulkan_texture_delete(unsigned int name);
#endif
#ifdef HALO_BGFX
/* display.renderer is "bgfx": the bgfx test backend draws instead of OpenGL
(d3d8_bgfx.inl), from platform_video_initialize on */
extern BOOL xgpu_bgfx;
/* the port's own textures under the bgfx backend (the high-res text's atlas,
HUD sheets, menu art): RGBA texels, made with their smaller levels when
mipmapped; a name (nonzero) as the OpenGL path's texture names are */
unsigned int xgpu_bgfx_texture_create(unsigned long width, unsigned long height, const unsigned char *rgba,
	BOOL mipmapped);
void xgpu_bgfx_texture_rows(unsigned int name, unsigned long top, unsigned long width, unsigned long rows,
	const unsigned char *rgba);
void xgpu_bgfx_texture_delete(unsigned int name);
/* the setup launcher's frame before the game draws (launcher.c): bgfx
started, the window cleared to color (0xRRGGBBAA), the overlay over it in
the picture's place (x, y its lower left corner) and the window saved to
screenshot (a .bmp's path, or NULL); FALSE if bgfx does not start */
BOOL xgpu_bgfx_launcher_frame(unsigned int color, int x, int y, int width, int height, const char *screenshot);
/* the overlay's quads (ui_overlay.c) over the window's picture at Present
(plasma_shift: the plasma's second map's place, then its threads' color;
cellwall: the cellwall's motion, side and front tints, ui_overlay_cellwall;
medals: the medals' atlas, premultiplied, while one is drawn, NULL before);
whether the medals' atlas is the GPU's now (its pixels may be let go) */
int xgpu_bgfx_overlay_draw(const void *vertices, int count, int vertex_bytes, const unsigned char *atlas, int atlas_size,
	int dirty_top, int dirty_bottom, const unsigned char *icons, int icon_size, int icon_count,
	const unsigned char *picture, int picture_width, int picture_height, int picture_serial,
	const unsigned char *plasma, int plasma_width, int plasma_height, int plasma_serial, const float plasma_shift[8],
	const float cellwall[12], const unsigned char *medals, int medals_width, int medals_height,
	const float cutouts[4][4], int window_width, int window_height);
#endif

/* ---------- anti-aliasing

display.anti_aliasing's passes (xgpu_post.c): FXAA or SMAA antialias each
window's 3D view in place before the HUD and menus are drawn over it, so
that their text stays sharp. Supersampling and multisampling are the
device's (d3d8_gl.c). */

/* the programs and textures of FXAA, or of SMAA, made now; FALSE if they
cannot be (once FALSE, it stays so) */
BOOL xgpu_post_prepare(BOOL smaa);

/* FXAA, or SMAA, on the corners x0, y0 to x1, y1 (from row 0) of a render
target's framebuffer, width by height, GL_RGBA8; FALSE if its programs do
not build */
BOOL xgpu_post_anti_alias(BOOL smaa, GLuint framebuffer, unsigned long width, unsigned long height,
	const GLint corners[4]);

#endif
