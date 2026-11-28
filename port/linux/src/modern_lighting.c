/*
MODERN_LIGHTING.C

The OpenGL passes of display.ray_tracing and display.glow
(modern_lighting.h), over a
window's 3D view once its opaque surfaces are drawn (render_window,
source/render/render.c, through port/linux/game/modern_lighting_scene.c).

1. The depth buffer (resolved, with its stencil, whose bit 0 is the
   first-person weapon's: rasterizer_xbox_models.c) is copied, and a pass
   writes each pixel's position from the camera and its normal (from the
   depths around it) into two targets. With display.ray_tracing those two
   are Vulkan's images (GL_EXT_memory_object_fd), which the rays start from.
2. The lighting: the traced occlusion and sun visibility
   (posix_modern_lighting_rt.c), between two semaphores
   (GL_EXT_semaphore_fd), on a grid of every pixel or one of each 2x2 or
   3x3 block (display.ray_tracing_quality), accumulated over frames.
3. A blur that keeps to each surface (two passes weighted by distance).
4. The composite: the view multiplied by the result over the opaque surfaces
   (sky and first-person weapon left out by the stencil and the depth), in
   the game's gamma-encoded color, before the shine, the reflections, the
   transparent surfaces and the fog are drawn over it. In a network game no
   player, vehicle or projectile is made darker than
   display.lighting_unit_floor (the online brightness floor: nobody hides in
   a shadow the other player's machine may not draw).

halo_ml_reflections: with SHADOWS + AO, the traced reflected rays' hits
shaded from the view, where the game's own shine and cube map reflections
lit a surface (the difference between the view before and after them).
halo_ml_bloom: display.glow's bloom, before display.hdr's tone mapping
(d3d8_gl.c), or over an 8-bit view (halo_ml_glow_sdr).

The lab (HALO_ML_LAB, a file of commands read each frame and removed): modes
switched without a restart, debug views, screenshots and frame timings, for
tools/modern_lighting_lab.py.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "xgpu.h"
#include "modern_lighting.h"
#include "port_config.h"
#include "capture.h"
#include "opensauce_postprocess.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* the menus' MODERN LIGHTING row (Video Setup's ADVANCED, the Guide's
SETTINGS): what its page has on, for display.ray_tracing's and
display.glow's words: the ray tracing (SHADOWS, SHADOWS + AO), else GLOW,
else OFF; NOT SUPPORTED's ray tracing is off */
void halo_ml_settings_summary_of(const char *ray_tracing, const char *glow, char *text, int size)
{
	int traced = ray_tracing && (!strcmp(ray_tracing, "shadows") || !strcmp(ray_tracing, "ao") ||
		!strcmp(ray_tracing, "gi")) && halo_ml_ray_tracing_support(0) != 0;
	int glowing = glow && (!strcmp(glow, "on") || !strcmp(glow, "shafts"));

	if (size <= 0)
		return;
	if (traced)
		snprintf(text, (size_t)size, "%s", !strcmp(ray_tracing, "shadows") ? "SHADOWS" : "SHADOWS + AO");
	else
		snprintf(text, (size_t)size, "%s", glowing ? "GLOW" : "OFF");
}

void halo_ml_settings_summary(char *text, int size)
{
	halo_ml_settings_summary_of(config_string("display.ray_tracing"), config_string("display.glow"), text, size);
}

int halo_ml_setting_is_lighting(const char *key)
{
	return !strcmp(key, "display.ray_tracing") || !strcmp(key, "display.ray_tracing_quality") ||
		!strcmp(key, "display.glow") || !strcmp(key, "display.glow_intensity");
}

int halo_ml_unavailable(void)
{
	return halo_ml_renderer_bgfx();
}

const char *halo_ml_setting_note(const char *key)
{
	static char note[96];
	const char *reason;
	int traced = !strcmp(key, "display.ray_tracing") || !strcmp(key, "display.ray_tracing_quality");

	if (!halo_ml_setting_is_lighting(key))
		return NULL;
	/* (bgfx draws: none of them yet; OpenGL's from the next start, when
	Video Setup's RENDERER chooses it) */
	if (halo_ml_unavailable())
		reason = "NEEDS THE OPENGL RENDERER";
	else if (traced)
		reason = halo_ml_ray_tracing_reason();
	/* (OpenGL older than 4.3: the glow's passes can't run) */
	else if (halo_ml_glow_support() == 0)
		reason = "NEEDS OPENGL 4.3";
	/* (a map with its own post-processing: its bloom, not the glow) */
	else
		reason = opensauce_postprocess_drawn() ? "OFF, THE MAP HAS ITS OWN" : NULL;
	if (!reason)
		return NULL;
	snprintf(note, sizeof(note), "%s: %s", traced ? "RAY TRACING" : "GLOW", reason);
	return note;
}

#if !defined(HALO_GL_ES) && !defined(__APPLE__)

#ifndef GL_DEVICE_UUID_EXT
#define GL_DEVICE_UUID_EXT 0x9597
#endif
#ifndef GL_HANDLE_TYPE_OPAQUE_FD_EXT
#define GL_HANDLE_TYPE_OPAQUE_FD_EXT 0x9586
#endif
#ifndef GL_LAYOUT_GENERAL_EXT
#define GL_LAYOUT_GENERAL_EXT 0x958D
#endif
#ifndef GL_DEDICATED_MEMORY_OBJECT_EXT
#define GL_DEDICATED_MEMORY_OBJECT_EXT 0x9581
#endif
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_DEPTH_STENCIL_TEXTURE_MODE
#define GL_DEPTH_STENCIL_TEXTURE_MODE 0x90EA
#endif
#ifndef GL_STENCIL_INDEX
#define GL_STENCIL_INDEX 0x1901
#endif

void *gl_function_extra(const char *name);

/* the entry points past the renderer's own list (gl.h) */
static struct
{
	void (*QueryCounter)(GLuint id, GLenum target);
	void (*GetQueryObjectui64v)(GLuint id, GLenum pname, unsigned long long *params);
	void (*GetQueryObjectiv)(GLuint id, GLenum pname, GLint *params);
	void (*TexStorage2D)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
	void (*TextureView)(GLuint texture, GLenum target, GLuint origtexture, GLenum internalformat, GLuint minlevel,
		GLuint numlevels, GLuint minlayer, GLuint numlayers);
	void (*GetUnsignedBytei_vEXT)(GLenum target, GLuint index, GLubyte *data);
	void (*CreateMemoryObjectsEXT)(GLsizei n, GLuint *memoryObjects);
	void (*MemoryObjectParameterivEXT)(GLuint memoryObject, GLenum pname, const GLint *params);
	void (*ImportMemoryFdEXT)(GLuint memory, unsigned long long size, GLenum handleType, GLint fd);
	void (*TexStorageMem2DEXT)(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height,
		GLuint memory, unsigned long long offset);
	void (*DeleteMemoryObjectsEXT)(GLsizei n, const GLuint *memoryObjects);
	void (*GenSemaphoresEXT)(GLsizei n, GLuint *semaphores);
	void (*ImportSemaphoreFdEXT)(GLuint semaphore, GLenum handleType, GLint fd);
	void (*SignalSemaphoreEXT)(GLuint semaphore, GLuint numBufferBarriers, const GLuint *buffers,
		GLuint numTextureBarriers, const GLuint *textures, const GLenum *dstLayouts);
	void (*WaitSemaphoreEXT)(GLuint semaphore, GLuint numBufferBarriers, const GLuint *buffers,
		GLuint numTextureBarriers, const GLuint *textures, const GLenum *srcLayouts);
	void (*DeleteSemaphoresEXT)(GLsizei n, const GLuint *semaphores);
	/* (GL 4.5 / GL_ARB_texture_barrier: the bloom's combine reads the
	pixel it writes, the view's own texture, without a copy of it) */
	void (*TextureBarrier)(void);
	void (*GetFramebufferAttachmentParameteriv)(GLenum target, GLenum attachment, GLenum pname, GLint *params);
	BOOL loaded, interop;
} ext;

static void ext_load(void)
{
	if (ext.loaded)
		return;
	ext.loaded = TRUE;
#define LOAD(field, name) *(void **)&ext.field = gl_function_extra(name)
	LOAD(QueryCounter, "glQueryCounter");
	LOAD(GetQueryObjectui64v, "glGetQueryObjectui64v");
	LOAD(GetQueryObjectiv, "glGetQueryObjectiv");
	LOAD(TexStorage2D, "glTexStorage2D");
	LOAD(TextureView, "glTextureView");
	LOAD(GetUnsignedBytei_vEXT, "glGetUnsignedBytei_vEXT");
	LOAD(CreateMemoryObjectsEXT, "glCreateMemoryObjectsEXT");
	LOAD(MemoryObjectParameterivEXT, "glMemoryObjectParameterivEXT");
	LOAD(ImportMemoryFdEXT, "glImportMemoryFdEXT");
	LOAD(TexStorageMem2DEXT, "glTexStorageMem2DEXT");
	LOAD(DeleteMemoryObjectsEXT, "glDeleteMemoryObjectsEXT");
	LOAD(GenSemaphoresEXT, "glGenSemaphoresEXT");
	LOAD(ImportSemaphoreFdEXT, "glImportSemaphoreFdEXT");
	LOAD(SignalSemaphoreEXT, "glSignalSemaphoreEXT");
	LOAD(WaitSemaphoreEXT, "glWaitSemaphoreEXT");
	LOAD(DeleteSemaphoresEXT, "glDeleteSemaphoresEXT");
	LOAD(GetFramebufferAttachmentParameteriv, "glGetFramebufferAttachmentParameteriv");
	LOAD(TextureBarrier, "glTextureBarrier");
	if (!ext.TextureBarrier)
		LOAD(TextureBarrier, "glTextureBarrierNV");
#undef LOAD
	ext.interop = ext.GetUnsignedBytei_vEXT && ext.CreateMemoryObjectsEXT && ext.ImportMemoryFdEXT &&
		ext.TexStorageMem2DEXT && ext.GenSemaphoresEXT && ext.ImportSemaphoreFdEXT && ext.SignalSemaphoreEXT &&
		ext.WaitSemaphoreEXT && ext.MemoryObjectParameterivEXT;
}

/* ---------- programs */

enum
{
	_program_prepass,
	_program_blur,
	_program_composite,
	_program_reflections,
	_program_bloom_bright,
	_program_bloom_down,
	_program_bloom_up,
	_program_bloom_combine,
	_program_temporal,
	_program_shafts,
	_program_shine,
	_program_reflect_grid,
	NUMBER_OF_PROGRAMS
};

static const char vertex_source[] =
	"void main()\n"
	"{\n"
	"\tgl_Position = vec4(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0, 0.0, 1.0);\n"
	"}\n";

/* what every program over the view has: the rectangle (corners, in the
target's pixels, rows from the picture's top), clip to world and back */
static const char common_source[] =
	"uniform vec4 rect;\n"
	"uniform vec4 to_world[4];\n"
	"uniform vec4 to_clip[4];\n"
	"uniform vec4 camera;\n"
	"/* the trace's grid: x 1 every pixel traced, 2 one of each 2x2 block, its\n"
	"result at the block's place in the low-resolution corner; yz this frame's\n"
	"pixel in its block */\n"
	"uniform vec4 grid;\n"
	"int grid_scale()\n"
	"{\n"
	"\treturn max(int(grid.x), 1);\n"
	"}\n"
	"/* the low-resolution corner's last texel */\n"
	"ivec2 grid_last()\n"
	"{\n"
	"\tint s = grid_scale();\n"
	"\treturn ivec2(rect.xy) + (ivec2(rect.zw) - ivec2(rect.xy) + s - 1) / s - 1;\n"
	"}\n"
	"/* the pixel a block's texel was traced at (offset: the frame's pixel in its block) */\n"
	"ivec2 grid_pixel(ivec2 q, vec2 offset)\n"
	"{\n"
	"\treturn min(ivec2(rect.xy) + (q - ivec2(rect.xy)) * grid_scale() + ivec2(offset), ivec2(rect.zw) - 1);\n"
	"}\n"
	"/* the block of a pixel */\n"
	"ivec2 grid_block(ivec2 p)\n"
	"{\n"
	"\treturn ivec2(rect.xy) + (p - ivec2(rect.xy)) / grid_scale();\n"
	"}\n"
	"vec3 world_at(vec2 frag, float depth)\n"
	"{\n"
	"\tvec2 uv = (frag - rect.xy) / (rect.zw - rect.xy);\n"
	"\tvec4 ndc = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);\n"
	"\tvec4 w = vec4(dot(to_world[0], ndc), dot(to_world[1], ndc), dot(to_world[2], ndc), dot(to_world[3], ndc));\n"
	"\treturn w.xyz / w.w;\n"
	"}\n"
	"/* the pixel a point is drawn at (xy) and its depth (z); w < 0 behind the camera */\n"
	"vec4 screen_of(vec3 world)\n"
	"{\n"
	"\tvec4 p = vec4(world, 1.0);\n"
	"\tvec4 c = vec4(dot(to_clip[0], p), dot(to_clip[1], p), dot(to_clip[2], p), dot(to_clip[3], p));\n"
	"\tvec3 ndc = c.xyz / c.w;\n"
	"\tvec2 uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);\n"
	"\treturn vec4(rect.xy + uv * (rect.zw - rect.xy), ndc.z, c.w);\n"
	"}\n"
	"/* the view's depth (the opaque surfaces'), and from it a pixel's point\n"
	"from the camera (w its distance, 0 the sky) and its surface's normal: on\n"
	"a grid of blocks the passes over the whole view take them from here, not\n"
	"from full-size images of them (the prepass writes the traced pixels'\n"
	"alone) */\n"
	"uniform sampler2D depth_texture;\n"
	"float depth_at(ivec2 p)\n"
	"{\n"
	"\treturn texelFetch(depth_texture, clamp(p, ivec2(rect.xy), ivec2(rect.zw) - 1), 0).r;\n"
	"}\n"
	"vec3 neighbor(ivec2 p, ivec2 step)\n"
	"{\n"
	"\tivec2 q = clamp(p + step, ivec2(rect.xy), ivec2(rect.zw) - 1);\n"
	"\treturn world_at(vec2(q) + 0.5, depth_at(q));\n"
	"}\n"
	"vec4 point_at(ivec2 p)\n"
	"{\n"
	"\tfloat d = depth_at(p);\n"
	"\tif (d >= 1.0)\n"
	"\t\treturn vec4(0.0);\n"
	"\tvec3 w = world_at(vec2(p) + 0.5, d) - camera.xyz;\n"
	"\treturn vec4(w, length(w));\n"
	"}\n"
	"/* (w: the point, from the camera) */\n"
	"vec3 normal_at(ivec2 p, vec3 w)\n"
	"{\n"
	"\tw += camera.xyz;\n"
	"\tvec3 r = neighbor(p, ivec2(1, 0)) - w, l = w - neighbor(p, ivec2(-1, 0));\n"
	"\tvec3 u = neighbor(p, ivec2(0, 1)) - w, b = w - neighbor(p, ivec2(0, -1));\n"
	"\tvec3 dx = dot(r, r) < dot(l, l) ? r : l;\n"
	"\tvec3 dy = dot(u, u) < dot(b, b) ? u : b;\n"
	"\tvec3 n = normalize(cross(dx, dy));\n"
	"\treturn dot(n, camera.xyz - w) < 0.0 ? -n : n;\n"
	"}\n"
	"/* how like a grid texel's traced point (G, from the camera, and its\n"
	"normal Gn) a pixel's point P is: near in distance, and on its surface\n"
	"(near the plane through G across Gn; no normal of the pixel's needed) */\n"
	"float likeness(vec4 P, vec4 G, vec3 Gn)\n"
	"{\n"
	"\treturn exp(-abs(G.w - P.w) / (0.01 * P.w + 0.02)) * exp(-abs(dot(P.xyz - G.xyz, Gn)) / (0.004 * P.w + 0.01));\n"
	"}\n"
	"bool inside(vec2 frag)\n"
	"{\n"
	"\treturn frag.x >= rect.x && frag.y >= rect.y && frag.x < rect.z && frag.y < rect.w;\n"
	"}\n"
	"float noise(vec2 p)\n"
	"{\n"
	"\treturn fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));\n"
	"}\n";

static const char prepass_source[] =
	"uniform usampler2D stencil_texture;\n"
	"/* the last frame's shine (the shine pass's: the game's shine, each\n"
	"texel the most of an 8x8 block's), its matrix, and x 1 when known */\n"
	"uniform sampler2D shine;\n"
	"uniform vec4 previous_clip[4];\n"
	"uniform vec4 shine_params;\n"
	"layout(location = 0) out vec4 out_position;\n"
	"layout(location = 1) out vec4 out_normal;\n"
	"/* 1 where the point shone the frame before (its block or one beside\n"
	"it), or that is not known (not in view then): the trace's reflected\n"
	"rays go only there */\n"
	"float shone(vec3 w)\n"
	"{\n"
	"\tif (shine_params.x < 0.5)\n"
	"\t\treturn 1.0;\n"
	"\tvec4 p = vec4(w, 1.0);\n"
	"\tvec4 c = vec4(dot(previous_clip[0], p), dot(previous_clip[1], p), dot(previous_clip[2], p), dot(previous_clip[3], p));\n"
	"\tif (c.w <= 0.0)\n"
	"\t\treturn 1.0;\n"
	"\tvec2 uv = vec2(c.x / c.w * 0.5 + 0.5, 0.5 - c.y / c.w * 0.5);\n"
	"\tif (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)\n"
	"\t\treturn 1.0;\n"
	"\tvec2 f = uv * (rect.zw - rect.xy) / 8.0 - 0.5;\n"
	"\tivec2 k = ivec2(floor(f));\n"
	"\tivec2 last = (ivec2(rect.zw) - ivec2(rect.xy) + 7) / 8 - 1;\n"
	"\tfloat m = max(max(texelFetch(shine, clamp(k, ivec2(0), last), 0).r,\n"
	"\t\ttexelFetch(shine, clamp(k + ivec2(1, 0), ivec2(0), last), 0).r),\n"
	"\t\tmax(texelFetch(shine, clamp(k + ivec2(0, 1), ivec2(0), last), 0).r,\n"
	"\t\ttexelFetch(shine, clamp(k + ivec2(1, 1), ivec2(0), last), 0).r));\n"
	"\treturn m >= 0.02 ? 1.0 : 0.0;\n"
	"}\n"
	"/* (on a grid of blocks: drawn over the low-resolution corner, each texel\n"
	"its block's pixel traced this frame) */\n"
	"void main()\n"
	"{\n"
	"\tivec2 p = grid_pixel(ivec2(gl_FragCoord.xy), grid.yz);\n"
	"\tfloat d = depth_at(p);\n"
	"\tuint s = texelFetch(stencil_texture, p, 0).r;\n"
	"\tif (d >= 1.0 || (s & 1u) != 0u)\n"
	"\t{\n"
	"\t\tout_position = vec4(0.0);\n"
	"\t\tout_normal = vec4(0.0, 0.0, 1.0, (s & 1u) != 0u ? 1.0 : 0.0);\n"
	"\t\treturn;\n"
	"\t}\n"
	"\tvec3 w = world_at(vec2(p) + 0.5, d);\n"
	"\tout_position = vec4(w - camera.xyz, length(w - camera.xyz));\n"
	"\tout_normal = vec4(normal_at(p, w - camera.xyz), shone(w));\n"
	"}\n";

/* the shine: where the game's shine and reflections lit the view (what the
reflections pass draws over), each texel the most of an 8x8 block of the
view's pixels (16 of them looked at), for the next frame's trace */
static const char shine_source[] =
	"uniform sampler2D scene_before;\n"
	"uniform sampler2D scene_now;\n"
	"out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tivec2 base = ivec2(rect.xy) + ivec2(gl_FragCoord.xy) * 8;\n"
	"\tivec2 last = ivec2(rect.zw) - 1;\n"
	"\tfloat m = 0.0;\n"
	"\tfor (int j = 0; j < 4; j++)\n"
	"\t{\n"
	"\t\tfor (int i = 0; i < 4; i++)\n"
	"\t\t{\n"
	"\t\t\tivec2 q = min(base + ivec2(i * 2 + (j & 1), j * 2), last);\n"
	"\t\t\tvec3 shine = max(texelFetch(scene_now, q, 0).rgb - texelFetch(scene_before, q, 0).rgb, vec3(0.0));\n"
	"\t\t\tm = max(m, dot(shine, vec3(0.299, 0.587, 0.114)) * 8.0);\n"
	"\t\t}\n"
	"\t}\n"
	"\tresult = vec4(clamp(m, 0.0, 1.0));\n"
	"}\n";

/* a blur that keeps to each surface: nine taps along direction, weighted by
how near each one's distance is to the middle's */
static const char blur_source[] =
	"uniform sampler2D source;\n"
	"/* the frame's guide (the traced pixels' positions, on the grid; with\n"
	"its normals, w the frames accumulated) */\n"
	"uniform sampler2D guide;\n"
	"uniform sampler2D guide_normal;\n"
	"uniform vec4 direction;\n"
	"out vec4 result;\n"
	"/* (on the trace's grid: its texels, as wide in the view's pixels) */\n"
	"void main()\n"
	"{\n"
	"\tivec2 p = ivec2(gl_FragCoord.xy);\n"
	"\tvec4 middle = texelFetch(source, p, 0);\n"
	"\tfloat d = texelFetch(guide, p, 0).w;\n"
	"\tif (d <= 0.0)\n"
	"\t{\n"
	"\t\tresult = middle;\n"
	"\t\treturn;\n"
	"\t}\n"
	"\tvec2 sum = vec2(0.0);\n"
	"\tfloat weights = 0.0;\n"
	"\tint s = grid_scale();\n"
	"\tint reach = 4 / s;\n"
	"\tfloat spread = 10.0 / float(s * s);\n"
	"\t/* (on a grid, a texel whose history is new, a point just come into\n"
	"\tview or a moving one's: one sparse sample of few rays, spread over\n"
	"\ttwice the width until the frames add up, never shown alone) */\n"
	"\tif (s > 1)\n"
	"\t{\n"
	"\t\tfloat young = clamp((4.0 - texelFetch(guide_normal, p, 0).w) / 3.0, 0.0, 1.0);\n"
	"\t\tif (young > 0.0)\n"
	"\t\t{\n"
	"\t\t\treach *= 2;\n"
	"\t\t\tspread *= 1.0 + 3.0 * young;\n"
	"\t\t}\n"
	"\t}\n"
	"\tivec2 last = grid_last();\n"
	"\tfor (int k = -reach; k <= reach; k++)\n"
	"\t{\n"
	"\t\tivec2 q = clamp(p + ivec2(direction.xy * float(k)), ivec2(rect.xy), last);\n"
	"\t\tfloat dq = texelFetch(guide, q, 0).w;\n"
	"\t\tif (dq <= 0.0)\n"
	"\t\t\tcontinue;\n"
	"\t\tfloat weight = exp(-float(k * k) / spread) * exp(-abs(dq - d) / (0.01 * d + 0.02));\n"
	"\t\tsum += texelFetch(source, q, 0).rg * weight;\n"
	"\t\tweights += weight;\n"
	"\t}\n"
	"\tresult = vec4(weights > 0.0 ? sum / weights : middle.rg, middle.ba);\n"
	"}\n";

/* the view multiplied by the lighting (blended: destination times source),
or with debug.x a view of the passes in its place */
static const char composite_source[] =
	"uniform sampler2D lighting;\n"
	"uniform sampler2D position_texture;\n"
	"uniform sampler2D normal_texture;\n"
	"/* the frame's guide (the traced pixels' positions and normals, on the grid) */\n"
	"uniform sampler2D guide;\n"
	"uniform sampler2D guide_normal;\n"
	"/* x: the occlusion's strength, y: the shadows', z: the floor (0 none),\n"
	"w: the floor for every pixel (1) or the units' (0) */\n"
	"uniform vec4 strength;\n"
	"uniform vec4 debug;\n"
	"/* x: the shadows fade where the lightmap is dark already (its own shadow) */\n"
	"uniform vec4 fade;\n"
	"uniform sampler2D scene_now;\n"
	"out vec4 result;\n"
	"/* the lighting at a pixel: on a grid of blocks, its four nearest\n"
	"texels', weighted by how near each is (bilinear) and by how near its\n"
	"traced point's distance and normal are to the pixel's own, so that no\n"
	"shade crosses an edge; the object and unit marks the likest one's */\n"
	"vec4 lighting_at(ivec2 p, vec4 P)\n"
	"{\n"
	"\tint s = grid_scale();\n"
	"\tif (s <= 1)\n"
	"\t\treturn texelFetch(lighting, p, 0);\n"
	"\tfloat d = P.w;\n"
	"\tvec2 f = (vec2(p) + 0.5 - rect.xy) / float(s) - 0.5;\n"
	"\tivec2 k = ivec2(floor(f));\n"
	"\tvec2 t = f - vec2(k);\n"
	"\tivec2 last = grid_last();\n"
	"\tvec4 sum = vec4(0.0);\n"
	"\tfloat weights = 0.0, best = -1.0;\n"
	"\tvec4 likest = vec4(1.0, 1.0, 0.0, 0.0);\n"
	"\tfor (int j = 0; j < 2; j++)\n"
	"\t{\n"
	"\t\tfor (int i = 0; i < 2; i++)\n"
	"\t\t{\n"
	"\t\t\tivec2 q = clamp(ivec2(rect.xy) + k + ivec2(i, j), ivec2(rect.xy), last);\n"
	"\t\t\tvec4 G = texelFetch(guide, q, 0);\n"
	"\t\t\tif (G.w <= 0.0)\n"
	"\t\t\t\tcontinue;\n"
	"\t\t\tvec4 value = texelFetch(lighting, q, 0);\n"
	"\t\t\tfloat like = likeness(P, G, texelFetch(guide_normal, q, 0).xyz);\n"
	"\t\t\tfloat weight = (mix(1.0 - t.x, t.x, float(i)) * mix(1.0 - t.y, t.y, float(j)) + 0.01) * like;\n"
	"\t\t\tsum += value * weight;\n"
	"\t\t\tweights += weight;\n"
	"\t\t\tif (like > best)\n"
	"\t\t\t{\n"
	"\t\t\t\tbest = like;\n"
	"\t\t\t\tlikest = value;\n"
	"\t\t\t}\n"
	"\t\t}\n"
	"\t}\n"
	"\tif (weights < 1e-4)\n"
	"\t\treturn likest;\n"
	"\treturn vec4((sum / weights).rg, likest.ba);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"\tivec2 p = ivec2(gl_FragCoord.xy);\n"
	"\t/* (every pixel traced: the prepass's full-size images; on a grid, from\n"
	"\tthe depth) */\n"
	"\tvec4 P = grid_scale() <= 1 ? texelFetch(position_texture, p, 0) : point_at(p);\n"
	"\tif (P.w <= 0.0)\n"
	"\t\tdiscard;\n"
	"\tvec4 L = lighting_at(p, P);\n"
	"\tfloat shadow = strength.y;\n"
	"\tif (fade.x > 0.5)\n"
	"\t\tshadow *= smoothstep(0.06, 0.3, dot(texelFetch(scene_now, p, 0).rgb, vec3(0.299, 0.587, 0.114)));\n"
	"\tfloat f = mix(1.0, L.r, strength.x) * mix(1.0, L.g, shadow);\n"
	"\tif (strength.z > 0.0 && (strength.w > 0.5 || L.a > 0.5))\n"
	"\t\tf = max(f, strength.z);\n"
	"\tif (debug.x > 3.5)\n"
	"\t\tresult = vec4(L.rgb, 1.0);\n"
	"\telse if (debug.x > 2.5)\n"
	"\t\tresult = vec4((grid_scale() <= 1 ? texelFetch(normal_texture, p, 0).xyz : normal_at(p, P.xyz)) * 0.5 + 0.5, 1.0);\n"
	"\telse if (debug.x > 1.5)\n"
	"\t\tresult = vec4(fract(camera.xyz + P.xyz), 1.0);\n"
	"\telse if (debug.x > 0.5)\n"
	"\t\tresult = vec4(vec3(f), 1.0);\n"
	"\telse\n"
	"\t\tresult = vec4(vec3(pow(f, 1.0 / 2.2)), 1.0);\n"
	"}\n";

/* the view at a reflected hit (from the camera) of a ray from the point
P, faded toward the screen's edges and the ray's end; w 0 when it is not
seen (off screen, or something else drawn there). The reflections' passes
have it */
#define ML_SEEN_SOURCE \
	"uniform sampler2D scene_before;\n" \
	"uniform sampler2D position_texture;\n" \
	"uniform sampler2D reflection_hits;\n" \
	"/* x: strength, y: the longest ray, z: the frame's checkerboard (0 or\n" \
	"1), w: debug */\n" \
	"uniform vec4 params;\n" \
	"vec4 seen(vec3 hit, vec3 P)\n" \
	"{\n" \
	"\tvec4 s = screen_of(camera.xyz + hit);\n" \
	"\tif (s.w <= 0.0 || !inside(s.xy))\n" \
	"\t\treturn vec4(0.0);\n" \
	"\tvec4 Q = grid_scale() <= 1 ? texelFetch(position_texture, ivec2(s.xy), 0) : point_at(ivec2(s.xy));\n" \
	"\tif (Q.w <= 0.0 || abs(Q.w - length(hit)) > 0.15 + 0.02 * Q.w)\n" \
	"\t\treturn vec4(0.0);\n" \
	"\tvec2 at = s.xy;\n" \
	"\tfloat t = length(hit - P);\n" \
	"\tvec2 edge = min(at - rect.xy, rect.zw - at) / (rect.zw - rect.xy);\n" \
	"\tfloat fade = smoothstep(0.0, 0.08, min(edge.x, edge.y)) * (1.0 - smoothstep(params.y * 0.6, params.y, t));\n" \
	"\treturn vec4(texelFetch(scene_before, ivec2(at), 0).rgb * fade, 1.0);\n" \
	"}\n"

/* on a grid of blocks, the reflected view at each texel of the grid (its
traced point's; times the fresnel there; alpha: the point's distance, 0
none), complete: the texels traced this frame (rt_trace.comp's
reflections_pass: one of each two, in a checkerboard that turns) from their
own hit; the others from the four traced around them, those on the same
surface (distance and normal), each one's hit distance along the texel's
own reflected ray. Misses and hits out of view count as no reflection. No
texel of another frame is used (its hit is where another view's ray went):
nothing of a pattern shows while the view moves, and the view-size pass
takes this up as the lighting is */
static const char reflect_grid_source[] =
	ML_SEEN_SOURCE
	"/* this frame's guide: each grid texel's traced point and normal */\n"
	"uniform sampler2D guide;\n"
	"uniform sampler2D guide_normal;\n"
	"out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tivec2 q = ivec2(gl_FragCoord.xy);\n"
	"\tvec4 G = texelFetch(guide, q, 0);\n"
	"\tresult = vec4(0.0);\n"
	"\tif (G.w <= 0.0)\n"
	"\t\treturn;\n"
	"\tvec3 n = texelFetch(guide_normal, q, 0).xyz;\n"
	"\tfloat fresnel = mix(0.35, 1.0, pow(1.0 - max(dot(-normalize(G.xyz), n), 0.0), 3.0));\n"
	"\tresult = vec4(0.0, 0.0, 0.0, G.w);\n"
	"\tivec2 k = q - ivec2(rect.xy);\n"
	"\tif (((k.x + k.y) & 1) == int(params.z))\n"
	"\t{\n"
	"\t\tvec4 H = texelFetch(reflection_hits, q, 0);\n"
	"\t\tif (H.w > 0.0)\n"
	"\t\t\tresult.rgb = seen(H.xyz, G.xyz).rgb * fresnel;\n"
	"\t\treturn;\n"
	"\t}\n"
	"\tvec3 r = reflect(normalize(G.xyz), n);\n"
	"\tivec2 last = grid_last();\n"
	"\tvec3 sum = vec3(0.0);\n"
	"\tfloat weights = 0.0;\n"
	"\tfor (int i = 0; i < 4; i++)\n"
	"\t{\n"
	"\t\tivec2 o = i == 0 ? ivec2(1, 0) : i == 1 ? ivec2(-1, 0) : i == 2 ? ivec2(0, 1) : ivec2(0, -1);\n"
	"\t\tivec2 u = q + o;\n"
	"\t\tif (u.x < int(rect.x) || u.y < int(rect.y) || u.x > last.x || u.y > last.y)\n"
	"\t\t\tcontinue;\n"
	"\t\tvec4 U = texelFetch(guide, u, 0);\n"
	"\t\tif (U.w <= 0.0)\n"
	"\t\t\tcontinue;\n"
	"\t\tfloat weight = exp(-abs(U.w - G.w) / (0.01 * G.w + 0.02)) *\n"
	"\t\t\tpow(max(dot(n, texelFetch(guide_normal, u, 0).xyz), 0.0), 8.0);\n"
	"\t\tif (weight < 1e-3)\n"
	"\t\t\tcontinue;\n"
	"\t\tvec4 H = texelFetch(reflection_hits, u, 0);\n"
	"\t\tif (H.w > 0.0)\n"
	"\t\t\tsum += seen(G.xyz + r * length(H.xyz - U.xyz), G.xyz).rgb * weight;\n"
	"\t\tweights += weight;\n"
	"\t}\n"
	"\tif (weights > 0.0)\n"
	"\t\tresult.rgb = sum / weights * fresnel;\n"
	"}\n";

/* "full"'s reflections: where the game's shine and reflections lit a
surface, the view before them at the traced reflected ray's hit. Every
pixel traced: its own hit. On a grid of blocks: the grid's reflected view
(reflect_grid_source's), from its four nearest texels, weighted by how near
each is (bilinear) and how near its point's distance is to the pixel's (one
fetch a texel: the pass covers every shiny pixel of the view) */
static const char reflections_source[] =
	ML_SEEN_SOURCE
	"uniform sampler2D scene_now;\n"
	"uniform sampler2D normal_texture;\n"
	"uniform sampler2D guide;\n"
	"uniform sampler2D guide_normal;\n"
	"/* the grid's reflected view (rgb, with the fresnel; a the texel's\n"
	"distance, 0 none) */\n"
	"uniform sampler2D reflected_grid;\n"
	"out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tivec2 p = ivec2(gl_FragCoord.xy);\n"
	"\tvec3 now = texelFetch(scene_now, p, 0).rgb;\n"
	"\tvec3 shine = max(now - texelFetch(scene_before, p, 0).rgb, vec3(0.0));\n"
	"\tfloat m = clamp(dot(shine, vec3(0.299, 0.587, 0.114)) * 8.0, 0.0, 1.0);\n"
	"\t/* (debug: where the game's shine and reflections lit the view) */\n"
	"\tif (params.w > 0.5)\n"
	"\t{\n"
	"\t\tresult = vec4(m, m, m, 1.0);\n"
	"\t\treturn;\n"
	"\t}\n"
	"\tif (m < 0.03)\n"
	"\t\tdiscard;\n"
	"\tint scale = grid_scale();\n"
	"\tvec4 P = scale <= 1 ? texelFetch(position_texture, p, 0) : point_at(p);\n"
	"\tif (P.w <= 0.0)\n"
	"\t\tdiscard;\n"
	"\tvec3 n = scale <= 1 ? texelFetch(normal_texture, p, 0).xyz : vec3(0.0);\n"
	"\tvec3 v = normalize(P.xyz);\n"
	"\tvec3 reflected;\n"
	"\tif (scale <= 1)\n"
	"\t{\n"
	"\t\t/* the traced ray's hit, shaded from the screen where it is seen */\n"
	"\t\tvec4 H = texelFetch(reflection_hits, p, 0);\n"
	"\t\tif (H.w <= 0.0)\n"
	"\t\t\tdiscard;\n"
	"\t\tvec4 c = seen(H.xyz, P.xyz);\n"
	"\t\tif (c.w <= 0.0)\n"
	"\t\t\tdiscard;\n"
	"\t\treflected = c.rgb;\n"
	"\t}\n"
	"\telse\n"
	"\t{\n"
	"\t\tvec2 f = (vec2(p) + 0.5 - rect.xy) / float(scale) - 0.5;\n"
	"\t\tivec2 k = ivec2(floor(f));\n"
	"\t\tvec2 t = f - vec2(k);\n"
	"\t\tivec2 last = grid_last();\n"
	"\t\tvec3 sum = vec3(0.0);\n"
	"\t\tfloat weights = 0.0;\n"
	"\t\tfor (int j = 0; j < 2; j++)\n"
	"\t\t{\n"
	"\t\t\tfor (int i = 0; i < 2; i++)\n"
	"\t\t\t{\n"
	"\t\t\t\tivec2 q = clamp(ivec2(rect.xy) + k + ivec2(i, j), ivec2(rect.xy), last);\n"
	"\t\t\t\tvec4 R = texelFetch(reflected_grid, q, 0);\n"
	"\t\t\t\tif (R.a <= 0.0)\n"
	"\t\t\t\t\tcontinue;\n"
	"\t\t\t\tfloat weight = (mix(1.0 - t.x, t.x, float(i)) * mix(1.0 - t.y, t.y, float(j)) + 0.01) *\n"
	"\t\t\t\t\texp(-abs(R.a - P.w) / (0.01 * P.w + 0.02));\n"
	"\t\t\t\tsum += R.rgb * weight;\n"
	"\t\t\t\tweights += weight;\n"
	"\t\t\t}\n"
	"\t\t}\n"
	"\t\tif (weights < 1e-4)\n"
	"\t\t\tdiscard;\n"
	"\t\t/* (the fresnel the grid's) */\n"
	"\t\tresult = vec4(now + sum / weights * (m * params.x), 1.0);\n"
	"\t\treturn;\n"
	"\t}\n"
	"\tfloat fresnel = mix(0.35, 1.0, pow(1.0 - max(dot(-v, n), 0.0), 3.0));\n"
	"\tresult = vec4(now + reflected * (m * params.x * fresnel), 1.0);\n"
	"}\n";

/* the bloom: the light past white (linear), halved six times and added back
up; the game's colors are gamma 2.2 */
static const char bloom_bright_source[] =
	"uniform sampler2D source;\n"
	"uniform vec4 params;\n"
	"out vec4 result;\n"
	"vec3 past_white(vec2 uv)\n"
	"{\n"
	"\tvec3 c = pow(max(textureLod(source, uv, 0.0).rgb, vec3(0.0)), vec3(2.2));\n"
	"\tfloat l = max(c.r, max(c.g, c.b));\n"
	"\tfloat knee = clamp(l - params.x + params.y, 0.0, 2.0 * params.y);\n"
	"\tknee = knee * knee / (4.0 * params.y + 1e-4);\n"
	"\treturn c * max(knee, l - params.x) / max(l, 1e-4);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"\tvec2 texel = 1.0 / vec2(textureSize(source, 0));\n"
	"\tvec2 uv = (floor(gl_FragCoord.xy) * 2.0 + 1.0) * texel;\n"
	"\tresult = vec4((past_white(uv + texel * vec2(-0.5, -0.5)) + past_white(uv + texel * vec2(0.5, -0.5)) +\n"
	"\t\tpast_white(uv + texel * vec2(-0.5, 0.5)) + past_white(uv + texel * vec2(0.5, 0.5))) * 0.25, 1.0);\n"
	"}\n";

static const char bloom_down_source[] =
	"uniform sampler2D source;\n"
	"out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tvec2 texel = 1.0 / vec2(textureSize(source, 0));\n"
	"\tvec2 uv = (floor(gl_FragCoord.xy) * 2.0 + 1.0) * texel;\n"
	"\tvec3 c = textureLod(source, uv, 0.0).rgb * 0.5;\n"
	"\tc += (textureLod(source, uv + texel * vec2(-1.0, -1.0), 0.0).rgb + textureLod(source, uv + texel * vec2(1.0, -1.0), 0.0).rgb +\n"
	"\t\ttextureLod(source, uv + texel * vec2(-1.0, 1.0), 0.0).rgb + textureLod(source, uv + texel * vec2(1.0, 1.0), 0.0).rgb) * 0.125;\n"
	"\tresult = vec4(c, 1.0);\n"
	"}\n";

static const char bloom_up_source[] =
	"uniform sampler2D source;\n"
	"out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tvec2 texel = 1.0 / vec2(textureSize(source, 0));\n"
	"\tvec2 uv = (gl_FragCoord.xy * 0.5) * texel;\n"
	"\tvec3 c = textureLod(source, uv, 0.0).rgb * 4.0;\n"
	"\tc += (textureLod(source, uv + texel * vec2(-1.0, 0.0), 0.0).rgb + textureLod(source, uv + texel * vec2(1.0, 0.0), 0.0).rgb +\n"
	"\t\ttextureLod(source, uv + texel * vec2(0.0, -1.0), 0.0).rgb + textureLod(source, uv + texel * vec2(0.0, 1.0), 0.0).rgb) * 2.0;\n"
	"\tc += textureLod(source, uv + texel * vec2(-1.0, -1.0), 0.0).rgb + textureLod(source, uv + texel * vec2(1.0, -1.0), 0.0).rgb +\n"
	"\t\ttextureLod(source, uv + texel * vec2(-1.0, 1.0), 0.0).rgb + textureLod(source, uv + texel * vec2(1.0, 1.0), 0.0).rgb;\n"
	"\tresult = vec4(c / 16.0, 1.0);\n"
	"}\n";

static const char bloom_combine_source[] =
	"uniform sampler2D source;\n"
	"uniform sampler2D bloom;\n"
	"uniform vec4 params;\n"
	"out vec4 result;\n"
	"void main()\n"
	"{\n"
	"\tivec2 p = ivec2(gl_FragCoord.xy);\n"
	"\tvec3 c = pow(max(texelFetch(source, p, 0).rgb, vec3(0.0)), vec3(2.2));\n"
	"\tvec2 uv = gl_FragCoord.xy / vec2(textureSize(source, 0));\n"
	"\tc += textureLod(bloom, uv, 0.0).rgb * params.z;\n"
	"\tresult = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);\n"
	"}\n";

/* the lighting accumulated over frames: each pixel's last value found where
its point was drawn the frame before (the positions are world points: the
camera's motion needs no motion vectors), kept unless the point there is
another one (a disocclusion, or an object that moved: its point is not
where it was), held to the range of this frame's samples around it (a
moving object's shadow left behind on the ground goes at once), and blended
with this frame's: faster while the history is short (the guide normal's w
counts its frames), so that a point come into view is not stuck on one
sparse sample */
static const char temporal_source[] =
	"uniform sampler2D source;\n"
	"uniform sampler2D position_texture;\n"
	"uniform sampler2D normal_texture;\n"
	"uniform sampler2D history;\n"
	"/* the last frame's guide: each texel's traced point then, and (w) its\n"
	"history's frames */\n"
	"uniform sampler2D previous_position;\n"
	"uniform sampler2D previous_guide_normal;\n"
	"uniform vec4 previous_clip[4];\n"
	"uniform vec4 previous_camera;\n"
	"/* x: this frame's weight, y: a history to use */\n"
	"uniform vec4 params;\n"
	"layout(location = 0) out vec4 result;\n"
	"/* this frame's guide: the traced pixel's position and normal (w: the\n"
	"frames accumulated) */\n"
	"layout(location = 1) out vec4 out_guide;\n"
	"layout(location = 2) out vec4 out_guide_normal;\n"
	"/* (on the trace's grid: a texel is its block's, the point the pixel traced\n"
	"this frame; the last frame's found at the block its point was drawn in,\n"
	"whose pixel then is a neighbor of that point's: the tolerance as wide) */\n"
	"void main()\n"
	"{\n"
	"\tivec2 p = ivec2(gl_FragCoord.xy);\n"
	"\tvec4 now = texelFetch(source, p, 0);\n"
	"\t/* (the prepass wrote the traced pixel's at its texel) */\n"
	"\tvec4 P = texelFetch(position_texture, p, 0);\n"
	"\tout_guide = P;\n"
	"\tout_guide_normal = vec4(texelFetch(normal_texture, p, 0).xyz, 1.0);\n"
	"\tresult = now;\n"
	"\tif (P.w <= 0.0 || params.y < 0.5)\n"
	"\t\treturn;\n"
	"\tvec4 w = vec4(camera.xyz + P.xyz, 1.0);\n"
	"\tvec4 c = vec4(dot(previous_clip[0], w), dot(previous_clip[1], w), dot(previous_clip[2], w), dot(previous_clip[3], w));\n"
	"\tif (c.w <= 0.0)\n"
	"\t\treturn;\n"
	"\tvec2 uv = vec2(c.x / c.w * 0.5 + 0.5, 0.5 - c.y / c.w * 0.5);\n"
	"\tvec2 at = rect.xy + uv * (rect.zw - rect.xy);\n"
	"\tif (!inside(at))\n"
	"\t\treturn;\n"
	"\tivec2 block = grid_block(ivec2(at));\n"
	"\tvec4 Q = texelFetch(previous_position, block, 0);\n"
	"\tif (Q.w <= 0.0 || distance(previous_camera.xyz + Q.xyz, w.xyz) > (0.03 + 0.01 * P.w) * float(grid_scale()))\n"
	"\t\treturn;\n"
	"\tvec4 before = texelFetch(history, block, 0);\n"
	"\tfloat frames = min(max(texelFetch(previous_guide_normal, block, 0).w, 1.0) + 1.0, 32.0);\n"
	"\tvec2 kept = before.rg;\n"
	"\tif (grid_scale() > 1)\n"
	"\t{\n"
	"\t\tvec2 low = now.rg, high = now.rg;\n"
	"\t\tivec2 last = grid_last();\n"
	"\t\tfor (int j = -1; j <= 1; j++)\n"
	"\t\t{\n"
	"\t\t\tfor (int i = -1; i <= 1; i++)\n"
	"\t\t\t{\n"
	"\t\t\t\tvec2 value = texelFetch(source, clamp(p + ivec2(i, j), ivec2(rect.xy), last), 0).rg;\n"
	"\t\t\t\tlow = min(low, value);\n"
	"\t\t\t\thigh = max(high, value);\n"
	"\t\t\t}\n"
	"\t\t}\n"
	"\t\tkept = clamp(kept, low - 0.1, high + 0.1);\n"
	"\t}\n"
	"\tout_guide_normal.w = frames;\n"
	"\tresult = vec4(mix(kept, now.rg, max(params.x, 1.0 / frames)), now.ba);\n"
	"}\n";

/* light shafts: the sky's light (the view's pixels with no depth) blurred
along the lines to the sun, added to the bloom's first level */
static const char shafts_source[] =
	"uniform sampler2D source;\n"
	"/* xy: the sun on the screen (0 to 1), z: strength, w: decay */\n"
	"uniform vec4 params;\n"
	"out vec4 result;\n"
	"const int SAMPLES = 48;\n"
	"void main()\n"
	"{\n"
	"\tvec2 size = vec2(textureSize(source, 0));\n"
	"\tvec2 uv = gl_FragCoord.xy * 2.0 / size;\n"
	"\tvec2 step = (params.xy - uv) / float(SAMPLES) * 0.9;\n"
	"\tvec3 sum = vec3(0.0);\n"
	"\tfloat weight = 1.0;\n"
	"\tvec2 at = uv + step * noise(gl_FragCoord.xy);\n"
	"\tfor (int i = 0; i < SAMPLES; i++)\n"
	"\t{\n"
	"\t\tfloat sky = textureLod(depth_texture, at, 0.0).r >= 1.0 ? 1.0 : 0.0;\n"
	"\t\tvec3 c = pow(max(textureLod(source, at, 0.0).rgb, vec3(0.0)), vec3(2.2));\n"
	"\t\tsum += c * sky * smoothstep(0.35, 1.0, dot(c, vec3(0.333))) * weight;\n"
	"\t\tweight *= params.w;\n"
	"\t\tat += step;\n"
	"\t}\n"
	"\tresult = vec4(sum / float(SAMPLES) * params.z, 1.0);\n"
	"}\n";

#define BLOOM_LEVELS 7

/* why display.ray_tracing is off here (ml.rt_reason) */
enum
{
	RT_WHY_NONE,
	/* no Vulkan side path in this build (only 64-bit Linux has one) */
	RT_WHY_PLATFORM,
	/* bgfx draws: the trace is OpenGL's */
	RT_WHY_RENDERER,
	/* OpenGL has no GL_EXT_memory_object_fd / GL_EXT_semaphore_fd */
	RT_WHY_SHARING,
	RT_WHY_NO_VULKAN,
	RT_WHY_OTHER_GPU,
	RT_WHY_NO_RAY_QUERIES,
	/* the probe passed, the Vulkan side path's start failed */
	RT_WHY_START,
};

static struct
{
	GLuint vertex_shader;
	GLuint programs[NUMBER_OF_PROGRAMS];
	BOOL failed;
	GLuint vertex_array, linear_sampler;
	/* the targets, of the back buffer's size */
	int width, height;
	GLuint depth_copy, stencil_view;
	GLuint lighting[3], lighting_framebuffers[3];
	GLuint scene_before, scene_now;
	GLenum scene_format;
	/* the positions and normals the last frame's passes wrote (the images
	shared with Vulkan) */
	GLuint last_position, last_normal;
	/* the accumulation: two histories, matrix and camera; with each history
	its frame's guide, the position (from the camera, w its distance) and
	the normal of the pixel each texel of the trace's grid was traced at
	(the low-resolution corner: what the accumulation, the blur and the
	composite's upsampling read, rather than the full-size images at a
	stride; the last frame's the accumulation's previous positions) */
	GLuint history[2], history_framebuffers[2], guide[2], guide_normal[2];
	/* where the game's shine was drawn (the shine pass's, an eighth of the
	size), for the next frame's reflected rays; the frame it was made in */
	GLuint shine, shine_framebuffer;
	/* the grid's reflected view (reflect_grid_source's), in its corner */
	GLuint reflected_grid, reflected_grid_framebuffer;
	int shine_frame;
	int history_index, history_valid;
	float previous_clip[16], previous_camera[4];
	/* the view before the lighting, for the shadows' fade into the lightmaps' */
	GLuint scene_lit;
	/* the sun on the screen (0 to 1), for the light shafts; z 0 when behind */
	float sun_screen[3];
	/* display.glow */
	int glow;
	BOOL reflections_traced;
	/* the view before the shine copied this frame (halo_ml_shine) */
	BOOL shine_before;
	float glow_intensity, glow_threshold;
	BOOL glow_shafts;
	/* the bloom's, of the size it was made for */
	int bloom_width, bloom_height;
	GLuint bloom[BLOOM_LEVELS], bloom_framebuffers[BLOOM_LEVELS];
	GLuint bloom_source;
	/* Track A's shared images, through GL_EXT_memory_object_fd */
	BOOL rt_tried, rt_ok;
	/* display.ray_tracing on this machine: 1, 0 (NOT SUPPORTED), -1 not
	known yet; asked: known on the next frame */
	int rt_support;
	BOOL rt_support_asked;
	/* why rt_support is 0, or the start failed (halo_ml_ray_tracing_reason) */
	int rt_reason;
	int rt_generation;
	GLuint rt_memory[4], rt_textures[4], rt_framebuffer, rt_semaphores[2];
	/* the settings, read when they change */
	unsigned long read_at;
	BOOL read;
	int ray_tracing;
	float unit_floor;
	/* display.ray_tracing as set (lab's or config.toml's), before a machine
	without it turns it off */
	int ray_tracing_wanted;
	int frame;
	/* the frame's matrices */
	float to_world[16], to_clip[16];
	/* display.ray_tracing_quality (HALO_RT_QUALITY_*, auto resolved by the
	GPU), and the trace's grid this frame and the last (scale, the pixel
	traced in each block: the passes' "grid") */
	int quality;
	float grid[4], previous_grid[4];
} ml = { .rt_support = -1, .grid = { 1.0f, 0.0f, 0.0f, 0.0f }, .previous_grid = { 1.0f, 0.0f, 0.0f, 0.0f } };

/* the lab's commands (HALO_ML_LAB) */
static struct
{
	BOOL checked;
	char path[1024];
	int ray_tracing, debug;
	int hdr;
	/* "geometry render|collision|config": the trace's scene */
	int geometry;
	/* "quality low|medium|high|auto|config": display.ray_tracing_quality's */
	int quality;
	unsigned int generation;
	int glow, glow_shafts;
	float glow_intensity;
	/* a sun of the lab's (0 0 0: the sky's) */
	float sun[3];
	/* as in a network game (the fairness floor) */
	int network;
	/* the GPU time of the bloom (a pass of its own, after the frame's) */
	GLuint bloom_queries[64][2];
	int bloom_query;
	char label[128];
	int frames_wanted, frames;
	double *intervals;
	double last_time;
	/* each timed frame's record (<lab>.frames: what a slow frame did) */
	struct lab_record
	{
		double interval, cpu_ml_ms, cpu_trace_ms, scene_ms;
		struct ml_rt_frame_stats rt;
	} *records;
	double frame_cpu_ml_ms, frame_cpu_trace_ms, frame_scene_ms;
	/* GPU timestamps: each frame's first pass, and the passes' own time */
	GLuint queries[64][2];
	int query_frame;
	double gpu_ms_sum, frame_gpu_ms_sum, rt_ms_sum, vk_build_ms_sum, vk_trace_ms_sum;
	/* the CPU's time in the passes, and in the trace's submission */
	double cpu_ms_sum, cpu_trace_ms_sum, scene_ms_sum;
	int gpu_count, frame_gpu_count, rt_count;
	unsigned long long previous_start;
	/* "trace on": a line a frame in <lab>.trace: the blend equation the game
	left, the view's mean (0-255) before and after the composite and at the
	tone map (before the HUD); "check": the composite only darkens, a frame
	it brightened counted (and said by "say") */
	BOOL trace;
	unsigned long trace_frame;
	GLint trace_equation;
	int trace_traced;
	float trace_before, trace_after;
	int check_failures, check_frames;
	/* "equation max|min|revsub|add": that blend equation left in GL at the
	hook each frame, as a game draw (a decal's "component max") leaves it;
	"equation none" stops */
	GLenum force_equation;
	GLuint probe_texture, probe_framebuffer;
	/* "rtflags N": the trace's parts left out, to time each (ml_rt_trace's flags) */
	int rt_flags;
} lab = { .ray_tracing = -1, .hdr = -1, .geometry = -1, .quality = -1, .glow = -1, .glow_shafts = -1 };

static const struct halo_ml_frame *lab_frame;

/* the lab's GPU time of each pass (GL timestamps between marks: a
segment's time is its first mark's pass), summed while "time" runs */
enum
{
	_mark_depth_copy,
	_mark_prepass,
	_mark_trace,
	_mark_upsample,
	_mark_temporal,
	_mark_blur,
	_mark_scene_copy,
	_mark_composite,
	_mark_end,
	_mark_reflections,
	_mark_reflections_end,
	_mark_bloom,
	_mark_bloom_bright,
	_mark_bloom_shafts,
	_mark_bloom_down,
	_mark_bloom_up,
	_mark_bloom_combine,
	_mark_bloom_end,
	NUMBER_OF_MARKS
};
static const char *const mark_names[NUMBER_OF_MARKS] =
{
	"depth_copy", "prepass", "trace", "upsample", "temporal", "blur", "scene_copy", "composite", "", "reflections", "",
	"bloom", "bloom_bright", "bloom_shafts", "bloom_down", "bloom_up", "bloom_combine", ""
};
#define MARK_FRAMES 8
static struct
{
	GLuint queries[MARK_FRAMES][NUMBER_OF_MARKS];
	BOOL used[MARK_FRAMES][NUMBER_OF_MARKS];
	int frame;
	double sums[NUMBER_OF_MARKS];
	int counts[NUMBER_OF_MARKS];
} marks;

static void mark(int which)
{
	if (!lab.frames_wanted || !ext.QueryCounter)
		return;
	if (!marks.queries[0][0])
	{
		int index;

		for (index = 0; index < MARK_FRAMES; index++)
			glGenQueries(NUMBER_OF_MARKS, marks.queries[index]);
	}
	ext.QueryCounter(marks.queries[marks.frame % MARK_FRAMES][which], GL_TIMESTAMP);
	marks.used[marks.frame % MARK_FRAMES][which] = TRUE;
}

/* once a frame, at its start: the marks of MARK_FRAMES - 1 frames ago read */
static void marks_next(void)
{
	int slot, which;

	if (!marks.queries[0][0])
		return;
	marks.frame++;
	slot = marks.frame % MARK_FRAMES;
	for (which = 0; which < NUMBER_OF_MARKS; which++)
	{
		int next;
		unsigned long long start = 0, end = 0;

		if (!marks.used[slot][which] || !mark_names[which][0])
			continue;
		for (next = which + 1; next < NUMBER_OF_MARKS && !marks.used[slot][next]; next++)
			;
		if (next >= NUMBER_OF_MARKS)
			continue;
		ext.GetQueryObjectui64v(marks.queries[slot][which], GL_QUERY_RESULT, &start);
		ext.GetQueryObjectui64v(marks.queries[slot][next], GL_QUERY_RESULT, &end);
		if (end > start && lab.frames_wanted)
		{
			marks.sums[which] += (double)(end - start) / 1000000.0;
			marks.counts[which]++;
		}
	}
	memset(marks.used[slot], 0, sizeof(marks.used[slot]));
}


static int words_index(const char *value, const char *const *words, int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (!strcmp(value, words[index]))
			return index;
	}
	return 0;
}

static const char *const ray_tracing_words[] = { "off", "shadows", "ao", "gi" };
static const char *const ray_tracing_quality_words[] = { "auto", "low", "medium", "high" };

int halo_ml_ray_tracing_mode(void)
{
	int ray_tracing;

	if (!ml.read || ml.read_at != config_changes())
	{
		double floor_value;

		ml.read = TRUE;
		ml.read_at = config_changes();
		ml.ray_tracing = words_index(config_string("display.ray_tracing"), ray_tracing_words, 4);
		ml.quality = words_index(config_string("display.ray_tracing_quality"), ray_tracing_quality_words, 4);
		/* ("gi" is "ao"'s: no bounce is traced) */
		if (ml.ray_tracing > HALO_RT_AO)
			ml.ray_tracing = HALO_RT_AO;
		{
			const char *glow = config_string("display.glow");
			double intensity = config_real("display.glow_intensity");
			double threshold = config_real("display.glow_threshold");

			ml.glow = !strcmp(glow, "on") || !strcmp(glow, "shafts");
			ml.glow_shafts = !strcmp(glow, "shafts");
			/* (clamped: config.toml may say anything) */
			ml.glow_intensity = (float)(intensity < 0.0 ? 0.0 : intensity > 4.0 ? 4.0 : intensity);
			ml.glow_threshold = (float)(threshold < 0.05 ? 0.05 : threshold > 4.0 ? 4.0 : threshold);
		}
		floor_value = config_real("display.lighting_unit_floor");
		ml.unit_floor = (float)(floor_value < 0.5 ? 0.5 : floor_value > 1.0 ? 1.0 : floor_value);
	}
	ray_tracing = lab.ray_tracing >= 0 ? (lab.ray_tracing > HALO_RT_AO ? HALO_RT_AO : lab.ray_tracing) : ml.ray_tracing;
	ml.ray_tracing_wanted = ray_tracing;
	/* (no Vulkan side path, no ray tracing: a machine without, or a start
	that failed, until the setting is turned off and on again) */
	if (ray_tracing != HALO_RT_OFF && (ml.rt_support == 0 || (ml.rt_tried && !ml.rt_ok)))
		ray_tracing = HALO_RT_OFF;
	return ray_tracing;
}

/* display.ray_tracing_quality (or the lab's), auto resolved: medium on an
integrated GPU, high on others */
static int rt_quality(void)
{
	int quality = lab.quality >= 0 ? lab.quality : ml.quality;

	if (quality == HALO_RT_QUALITY_AUTO)
		quality = ml_rt_integrated() ? HALO_RT_QUALITY_MEDIUM : HALO_RT_QUALITY_HIGH;
	return quality;
}

int halo_ml_rt_quality(void)
{
	return rt_quality();
}

int halo_ml_ray_tracing_meshes(void)
{
	static int environment = -1;

	if (lab.geometry >= 0)
		return lab.geometry;
	if (environment < 0)
	{
		const char *value = getenv("HALO_RT_GEOMETRY");

		environment = !(value && !strcmp(value, "collision"));
	}
	return environment;
}

int halo_ml_lab_hdr(int configured)
{
	return lab.hdr >= 0 ? lab.hdr : configured;
}

unsigned int halo_ml_lab_generation(void)
{
	return lab.generation;
}

/* how many of the trace's samples (a pixel, or a block of the grid) the
occlusion's radius spans where it is drawn in full, and where it is gone.
Fewer across, its shade (a crease of a cliff's facets, a bump of its rock)
is finer than the samples: it shows as specks and lines that crawl and
flicker while the view moves, most on far cliffs and ledges at MEDIUM and
LOW, whose samples are blocks of 3x3 and 4x4 pixels at 1920x1080 */
static const float ao_samples_full = 8.0f, ao_samples_none = 3.0f;

/* the distances over which the occlusion fades out (ml_rt_trace's
ao_fade): from 40 to 90 world units at most, sooner where the radius
spans few samples. to_clip's first row is the projection's x scale (1 /
tan of half the field of view) times a row of the camera's rotation */
static void ao_fade(const float *to_clip, int width, int scale, float radius, float fade[2])
{
	float x_scale = sqrtf(to_clip[0] * to_clip[0] + to_clip[1] * to_clip[1] + to_clip[2] * to_clip[2]);
	/* (a sample's width at a distance of 1) */
	float sample = x_scale > 0.0f && width > 0 ? 2.0f * (float)(scale > 1 ? scale : 1) / (x_scale * (float)width) : 0.0f;

	fade[0] = 40.0f;
	fade[1] = 90.0f;
	if (sample > 0.0f)
	{
		float full = radius / (sample * ao_samples_full), none = radius / (sample * ao_samples_none);

		fade[0] = full < fade[0] ? full : fade[0];
		fade[1] = none < fade[1] ? none : fade[1];
	}
	if (fade[1] < fade[0] + 1.0f)
		fade[1] = fade[0] + 1.0f;
}

static GLuint program_build(int which)
{
	static const char *const sources[NUMBER_OF_PROGRAMS] =
	{
		prepass_source, blur_source, composite_source, reflections_source,
		bloom_bright_source, bloom_down_source, bloom_up_source, bloom_combine_source, temporal_source,
		shafts_source, shine_source, reflect_grid_source
	};
	static const char *const names[NUMBER_OF_PROGRAMS] =
	{
		"ML prepass", "ML blur", "ML composite", "ML reflections",
		"ML bloom bright", "ML bloom down", "ML bloom up", "ML bloom combine", "ML temporal", "ML shafts",
		"ML shine", "ML reflect grid"
	};
	static const char *const units[] =
	{
		"depth_texture", "stencil_texture", "position_texture", "normal_texture", "source", "lighting",
		"scene_before", "scene_now", "bloom", "history", "previous_position", "reflection_hits", "guide",
		"guide_normal", "previous_guide_normal", "shine", "reflected_grid"
	};
	struct xgpu_text text = { 0 };
	GLuint fragment_shader, program;
	unsigned long index;

	if (!ml.vertex_shader)
	{
		xgpu_text_append(&text, "%s%s", XGPU_GL_VERSION_LINE, vertex_source);
		ml.vertex_shader = xgpu_compile_shader(GL_VERTEX_SHADER, text.buffer, "ML vertex");
		text.length = 0;
		if (!ml.vertex_shader)
		{
			free(text.buffer);
			return 0;
		}
	}
	xgpu_text_append(&text, "%s%s%s", XGPU_GL_VERSION_LINE, common_source, sources[which]);
	fragment_shader = xgpu_compile_shader(GL_FRAGMENT_SHADER, text.buffer, names[which]);
	free(text.buffer);
	if (!fragment_shader)
		return 0;
	program = xgpu_link_program(ml.vertex_shader, fragment_shader, names[which]);
	glDeleteShader(fragment_shader);
	if (!program)
		return 0;
	glUseProgram(program);
	/* (each sampler its own unit, in the list's order) */
	for (index = 0; index < sizeof(units) / sizeof(units[0]); index++)
		glUniform1i(glGetUniformLocation(program, units[index]), (GLint)index);
	return program;
}

static BOOL programs_prepare(void)
{
	int which;

	if (ml.failed)
		return FALSE;
	for (which = 0; which < NUMBER_OF_PROGRAMS; which++)
	{
		if (!ml.programs[which] && !(ml.programs[which] = program_build(which)))
		{
			platform_log("modern lighting: a program did not build; off");
			ml.failed = TRUE;
			return FALSE;
		}
	}
	if (!ml.vertex_array)
	{
		glGenVertexArrays(1, &ml.vertex_array);
		glGenSamplers(1, &ml.linear_sampler);
		glSamplerParameteri(ml.linear_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glSamplerParameteri(ml.linear_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(ml.linear_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glSamplerParameteri(ml.linear_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	return TRUE;
}

/* the units in the order program_build gave them */
enum
{
	_unit_depth,
	_unit_stencil,
	_unit_position,
	_unit_normal,
	_unit_source,
	_unit_lighting,
	_unit_scene_before,
	_unit_scene_now,
	_unit_bloom,
	_unit_history,
	_unit_previous_position,
	_unit_reflection_hits,
	_unit_guide,
	_unit_guide_normal,
	_unit_previous_guide_normal,
	_unit_shine,
	_unit_reflected_grid,
};

static void texture_bind(int unit, GLuint texture, BOOL linear)
{
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, texture);
	glBindSampler((GLuint)unit, linear ? ml.linear_sampler : 0);
}

static GLuint texture_make(GLenum format, int width, int height)
{
	GLuint texture;

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	ext.TexStorage2D(GL_TEXTURE_2D, 1, format, width, height);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return texture;
}

static GLuint framebuffer_make3(GLuint color0, GLuint color1, GLuint color2)
{
	GLuint framebuffer;
	GLenum buffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };

	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color0, 0);
	if (color1)
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, color1, 0);
	if (color1 && color2)
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, color2, 0);
	glDrawBuffers(color1 ? (color2 ? 3 : 2) : 1, buffers);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		platform_log("modern lighting: a framebuffer is incomplete");
	return framebuffer;
}

static GLuint framebuffer_make(GLuint color0, GLuint color1)
{
	return framebuffer_make3(color0, color1, 0);
}

static void textures_delete(void)
{
	int index;

	if (ml.depth_copy)
	{
		glDeleteTextures(1, &ml.stencil_view);
		glDeleteTextures(1, &ml.depth_copy);
		glDeleteTextures(3, ml.lighting);
		glDeleteFramebuffers(3, ml.lighting_framebuffers);
		glDeleteTextures(1, &ml.scene_before);
		glDeleteTextures(1, &ml.scene_now);
		glDeleteTextures(1, &ml.scene_lit);
		glDeleteTextures(2, ml.history);
		glDeleteFramebuffers(2, ml.history_framebuffers);
		glDeleteTextures(2, ml.guide);
		glDeleteTextures(2, ml.guide_normal);
		glDeleteTextures(1, &ml.shine);
		glDeleteTextures(1, &ml.reflected_grid);
		glDeleteFramebuffers(1, &ml.reflected_grid_framebuffer);
		glDeleteFramebuffers(1, &ml.shine_framebuffer);
	}
	for (index = 0; index < 3; index++)
	{
		ml.lighting[index] = ml.lighting_framebuffers[index] = 0;
	}
	ml.depth_copy = ml.stencil_view = 0;
	ml.scene_before = ml.scene_now = 0;
	ml.shine = ml.shine_framebuffer = 0;
	ml.reflected_grid = ml.reflected_grid_framebuffer = 0;
}

/* Track A's images, made by Vulkan and taken in here; FALSE without them */
static BOOL rt_textures_fit(int width, int height)
{
	struct ml_rt_shared shared;
	static const GLenum formats[4] = { GL_RGBA32F, GL_RGBA16F, GL_RGBA8, GL_RGBA32F };
	int index;

	if (!ml.rt_ok)
		return FALSE;
	if (!ml_rt_resize(width, height, &shared))
		return FALSE;
	if (ml.rt_textures[0] && shared.generation == ml.rt_generation)
		return TRUE;
	if (ml.rt_textures[0])
	{
		glDeleteTextures(4, ml.rt_textures);
		ext.DeleteMemoryObjectsEXT(4, ml.rt_memory);
		glDeleteFramebuffers(1, &ml.rt_framebuffer);
	}
	ml.rt_generation = shared.generation;
	ext.CreateMemoryObjectsEXT(4, ml.rt_memory);
	for (index = 0; index < 4; index++)
	{
		GLint dedicated = GL_TRUE;

		ext.MemoryObjectParameterivEXT(ml.rt_memory[index], GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
		ext.ImportMemoryFdEXT(ml.rt_memory[index], (unsigned long long)shared.memory_size[index],
			GL_HANDLE_TYPE_OPAQUE_FD_EXT, shared.memory_fd[index]);
		glGenTextures(1, &ml.rt_textures[index]);
		glBindTexture(GL_TEXTURE_2D, ml.rt_textures[index]);
		ext.TexStorageMem2DEXT(GL_TEXTURE_2D, 1, formats[index], width, height, ml.rt_memory[index], 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	if (!ml.rt_semaphores[0])
	{
		ext.GenSemaphoresEXT(2, ml.rt_semaphores);
		ext.ImportSemaphoreFdEXT(ml.rt_semaphores[0], GL_HANDLE_TYPE_OPAQUE_FD_EXT, shared.semaphore_fd[0]);
		ext.ImportSemaphoreFdEXT(ml.rt_semaphores[1], GL_HANDLE_TYPE_OPAQUE_FD_EXT, shared.semaphore_fd[1]);
	}
	ml.rt_framebuffer = framebuffer_make(ml.rt_textures[0], ml.rt_textures[1]);
	platform_log("modern lighting: Vulkan's images taken in, %dx%d (GL error %u)", width, height, glGetError());
	return TRUE;
}

static void textures_fit(const struct halo_ml_targets *targets)
{
	int index;

	if (ml.depth_copy && ml.width == targets->width && ml.height == targets->height &&
		ml.scene_format == (targets->hdr ? GL_RGBA16F : GL_RGBA8))
	{
		return;
	}
	textures_delete();
	ml.width = targets->width;
	ml.height = targets->height;
	ml.scene_format = targets->hdr ? GL_RGBA16F : GL_RGBA8;
	ml.depth_copy = texture_make(GL_DEPTH24_STENCIL8, ml.width, ml.height);
	glGenTextures(1, &ml.stencil_view);
	ext.TextureView(ml.stencil_view, GL_TEXTURE_2D, ml.depth_copy, GL_DEPTH24_STENCIL8, 0, 1, 0, 1);
	glBindTexture(GL_TEXTURE_2D, ml.stencil_view);
	glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_STENCIL_INDEX);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	for (index = 0; index < 3; index++)
	{
		ml.lighting[index] = texture_make(GL_RGBA8, ml.width, ml.height);
		ml.lighting_framebuffers[index] = framebuffer_make(ml.lighting[index], 0);
	}
	ml.scene_before = texture_make(ml.scene_format, ml.width, ml.height);
	ml.scene_now = texture_make(ml.scene_format, ml.width, ml.height);
	ml.scene_lit = texture_make(ml.scene_format, ml.width, ml.height);
	for (index = 0; index < 2; index++)
	{
		ml.history[index] = texture_make(GL_RGBA16F, ml.width, ml.height);
		ml.guide[index] = texture_make(GL_RGBA32F, ml.width, ml.height);
		ml.guide_normal[index] = texture_make(GL_RGBA16F, ml.width, ml.height);
		ml.history_framebuffers[index] = framebuffer_make3(ml.history[index], ml.guide[index], ml.guide_normal[index]);
	}
	ml.shine = texture_make(GL_R8, (ml.width + 7) / 8, (ml.height + 7) / 8);
	ml.shine_framebuffer = framebuffer_make(ml.shine, 0);
	ml.reflected_grid = texture_make(GL_RGBA16F, ml.width, ml.height);
	ml.reflected_grid_framebuffer = framebuffer_make(ml.reflected_grid, 0);
	ml.shine_frame = -100;
	ml.history_valid = FALSE;
	platform_log("modern lighting: targets %dx%d (GL error %u)", ml.width, ml.height, glGetError());
}

/* the view (resolved) for a pass over it whose pixels each read only their
own: with multisampling the resolved texture, which the pass does not draw
into; else the one it draws into, behind a texture barrier; without the
barrier a copy (copy): no full-size copy of the view each frame */
static GLuint view_readable(const struct halo_ml_targets *targets, GLuint copy)
{
	if (targets->samples)
		return targets->color_texture;
	if (ext.TextureBarrier)
	{
		ext.TextureBarrier();
		return targets->color_texture;
	}
	glCopyImageSubData(targets->color_texture, GL_TEXTURE_2D, 0, 0, 0, 0, copy, GL_TEXTURE_2D, 0, 0, 0, 0,
		targets->width, targets->height, 1);
	return copy;
}

/* 4x4 inverse (rows first), FALSE if singular */
static BOOL matrix_inverse(const float *m, float *out)
{
	double inv[16], det;
	int i;

	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
		m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
		m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
		m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
		m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
		m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
		m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
		m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
		m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
		m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
		m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
		m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
		m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
		m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
		m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
		m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
		m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
	det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (det == 0.0)
		return FALSE;
	det = 1.0 / det;
	for (i = 0; i < 16; i++)
		out[i] = (float)(inv[i] * det);
	return TRUE;
}

static void common_uniforms(GLuint program, const struct halo_ml_targets *targets, const struct halo_ml_frame *frame)
{
	float rect[4], camera[4];

	rect[0] = (float)targets->corners[0];
	rect[1] = (float)targets->corners[1];
	rect[2] = (float)targets->corners[2];
	rect[3] = (float)targets->corners[3];
	camera[0] = frame->camera[0];
	camera[1] = frame->camera[1];
	camera[2] = frame->camera[2];
	camera[3] = 0.0f;
	glUseProgram(program);
	glUniform4fv(glGetUniformLocation(program, "rect"), 1, rect);
	glUniform4fv(glGetUniformLocation(program, "to_world"), 4, ml.to_world);
	glUniform4fv(glGetUniformLocation(program, "to_clip"), 4, ml.to_clip);
	glUniform4fv(glGetUniformLocation(program, "camera"), 1, camera);
	glUniform4fv(glGetUniformLocation(program, "grid"), 1, ml.grid);
}

/* every pass's GL state, the game's left as it was by the last of its draws
(d3d8_gl.c sets each only when a draw needs it otherwise): the blend
equation too, which the game sets only for a blended draw, and leaves as it
was while blending is off. A decal or particle drawn with "component max"
(D3DBLENDOP_MAX) left GL_MAX in force for the composite, whose blend
(destination times the lighting) then became max(lighting, view): the world
near white wherever the lighting is near 1, for as long as that effect was
in view (the owner's white frames in a10's fights: 143 frames in a row with
GL_MAX at the hook in a replay of his own checkpoint, each one white) */
static void pass_state(void)
{
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glDisable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glBindVertexArray(ml.vertex_array);
}

static void pass_draw(GLuint framebuffer, const int corners[4])
{
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glViewport(corners[0], corners[1], corners[2] - corners[0], corners[3] - corners[1]);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void uniform4(GLuint program, const char *name, float x, float y, float z, float w)
{
	float value[4];

	value[0] = x;
	value[1] = y;
	value[2] = z;
	value[3] = w;
	glUniform4fv(glGetUniformLocation(program, name), 1, value);
}

/* ---------- the lab */

static double now_ms(void)
{
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	return (double)time.tv_sec * 1000.0 + (double)time.tv_nsec / 1000000.0;
}

static int compare_doubles(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return x < y ? -1 : x > y;
}

static void lab_results(void)
{
	char path[1100];
	FILE *file;
	double sum = 0.0, worst_stretch = 0.0;
	int index, count = lab.frames;

	/* the worst stretch: the slowest 30 frames in a row (half a second at
	60 fps), as their average */
	{
		double window = 0.0;

		for (index = 0; index < count; index++)
		{
			window += lab.intervals[index];
			if (index >= 30)
				window -= lab.intervals[index - 30];
			if (index >= 29 && window / 30.0 > worst_stretch)
				worst_stretch = window / 30.0;
		}
	}
	/* each frame's record, in order */
	if (lab.records)
	{
		snprintf(path, sizeof(path), "%s.frames", lab.path);
		file = fopen(path, "a");
		if (file)
		{
			for (index = 0; index < count; index++)
			{
				const struct lab_record *record = &lab.records[index];

				fprintf(file, "%s\t%d\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
					lab.label, index, record->interval, record->cpu_ml_ms, record->cpu_trace_ms, record->scene_ms,
					(double)record->rt.fence_ms, (double)record->rt.stall_ms, record->rt.built, record->rt.refit,
					record->rt.skinned, record->rt.instances, record->rt.new_slots, record->rt.new_parts,
					record->rt.new_textures, record->rt.new_chunks);
			}
			fclose(file);
		}
	}
	qsort(lab.intervals, (size_t)count, sizeof(double), compare_doubles);
	for (index = 0; index < count; index++)
		sum += lab.intervals[index];
	snprintf(path, sizeof(path), "%s.out", lab.path);
	file = fopen(path, "a");
	if (file)
	{
		fprintf(file, "time %s frames=%d avg_ms=%.3f p50_ms=%.3f p99_ms=%.3f worst_ms=%.3f gpu_frame_ms=%.3f "
			"ml_gpu_ms=%.3f rt_gpu_ms=%.3f cpu_ml_ms=%.3f cpu_trace_ms=%.3f cpu_scene_ms=%.3f\n",
			lab.label, count, sum / count, lab.intervals[count / 2], lab.intervals[(count * 99) / 100],
			lab.intervals[count - 1], lab.frame_gpu_count ? lab.frame_gpu_ms_sum / lab.frame_gpu_count : -1.0,
			lab.gpu_count ? lab.gpu_ms_sum / lab.gpu_count : lab.gpu_ms_sum / count, lab.rt_count ? lab.rt_ms_sum / lab.rt_count : 0.0,
			lab.cpu_ms_sum / count, lab.cpu_trace_ms_sum / count, lab.scene_ms_sum / count);
		{
			char passes[1024];
			int used = 0, which;

			passes[0] = 0;
			for (which = 0; which < NUMBER_OF_MARKS; which++)
			{
				if (marks.counts[which] && used < (int)sizeof(passes) - 64)
					used += snprintf(passes + used, sizeof(passes) - (size_t)used, " pass_%s=%.3f", mark_names[which],
						marks.sums[which] / marks.counts[which]);
			}
			double median = lab.intervals[count / 2];
			int over_15 = 0, over_2 = 0, over_33 = 0;

			for (index = 0; index < count; index++)
			{
				over_15 += lab.intervals[index] > median * 1.5;
				over_2 += lab.intervals[index] > median * 2.0;
				over_33 += lab.intervals[index] > 33.4;
			}
			fprintf(file, "passes %s worst_stretch_ms=%.3f p95_ms=%.3f p999_ms=%.3f spikes15=%d spikes2=%d "
				"over33=%d vk_build_ms=%.3f vk_trace_ms=%.3f%s\n", lab.label,
				worst_stretch, lab.intervals[(count * 95) / 100], lab.intervals[(count * 999) / 1000], over_15, over_2,
				over_33, lab.rt_count ? lab.vk_build_ms_sum / lab.rt_count : 0.0,
				lab.rt_count ? lab.vk_trace_ms_sum / lab.rt_count : 0.0, passes);
		}
		fclose(file);
	}
	free(lab.intervals);
	lab.intervals = NULL;
	free(lab.records);
	lab.records = NULL;
	lab.frames_wanted = 0;
}

static void lab_say(const char *line)
{
	char path[1100];
	FILE *file;

	snprintf(path, sizeof(path), "%s.out", lab.path);
	file = fopen(path, "a");
	if (file)
	{
		fprintf(file, "%s\n", line);
		fclose(file);
	}
}

/* the lab's commands, once a frame */
static void lab_poll(void)
{
	char taken[1100], line[256];
	FILE *file;

	if (!lab.checked)
	{
		const char *path = getenv("HALO_ML_LAB");

		lab.checked = TRUE;
		if (path && *path)
			snprintf(lab.path, sizeof(lab.path), "%s", path);
	}
	if (!lab.path[0])
		return;
	snprintf(taken, sizeof(taken), "%s.taken", lab.path);
	if (rename(lab.path, taken) != 0)
		return;
	file = fopen(taken, "r");
	if (!file)
		return;
	while (fgets(line, sizeof(line), file))
	{
		char word[64] = "", value[128] = "";
		int frames = 0;

		if (sscanf(line, "%63s %127s %d", word, value, &frames) < 1)
			continue;
		if (!strcmp(word, "rt"))
			lab.generation++;
		if (!strcmp(word, "rt"))
			lab.ray_tracing = !strcmp(value, "config") ? -1 : words_index(value, ray_tracing_words, 4);
		else if (!strcmp(word, "glow"))
		{
			lab.glow = !strcmp(value, "config") ? -1 : strcmp(value, "off") != 0;
			if (lab.glow > 0)
				ml.glow_shafts = !strcmp(value, "shafts");
			else if (lab.glow < 0)
				ml.read = FALSE;
		}
		else if (!strcmp(word, "network"))
			lab.network = !strcmp(value, "on");
		else if (!strcmp(word, "geometry"))
			lab.geometry = !strcmp(value, "config") ? -1 : !strcmp(value, "render");
		else if (!strcmp(word, "rtflags"))
			lab.rt_flags = atoi(value);
		else if (!strcmp(word, "quality"))
			lab.quality = !strcmp(value, "config") ? -1 : words_index(value, ray_tracing_quality_words, 4);
		else if (!strcmp(word, "sun"))
		{
			float x = 0.0f, y = 0.0f, z = 0.0f;

			sscanf(line, "%*s %f %f %f", &x, &y, &z);
			lab.sun[0] = x;
			lab.sun[1] = y;
			lab.sun[2] = z;
		}
		else if (!strcmp(word, "glow_intensity"))
			lab.glow_intensity = (float)atof(value);
		else if (!strcmp(word, "hdr"))
		{
			lab.hdr = !strcmp(value, "config") ? -1 : !strcmp(value, "on");
			lab.generation++;
		}
		else if (!strcmp(word, "where") && lab_frame)
		{
			char said[300];

			snprintf(said, sizeof(said), "where %.3f %.3f %.3f sun %.3f %.3f %.3f valid=%d outdoor=%d network=%d",
				lab_frame->camera[0], lab_frame->camera[1], lab_frame->camera[2], lab_frame->sun[0], lab_frame->sun[1],
				lab_frame->sun[2], lab_frame->sun_valid, lab_frame->outdoor, lab_frame->network_game);
			lab_say(said);
		}
		else if (!strcmp(word, "objects"))
		{
			static char list[8192], said[8300];

			modern_lighting_scene_list(list, sizeof(list));
			snprintf(said, sizeof(said), "objects %s", list);
			lab_say(said);
		}
		else if (!strcmp(word, "encounters"))
		{
			static char list[16384], said[16500];

			modern_lighting_scene_encounters(list, sizeof(list));
			snprintf(said, sizeof(said), "encounters %s", list);
			lab_say(said);
		}
		else if (!strcmp(word, "trace"))
			lab.trace = !strcmp(value, "on");
		else if (!strcmp(word, "check"))
		{
			lab.check_failures = 0;
			lab.check_frames = 0;
		}
		else if (!strcmp(word, "equation"))
		{
			lab.force_equation = !strcmp(value, "max") ? GL_MAX : !strcmp(value, "min") ? GL_MIN :
				!strcmp(value, "revsub") ? GL_FUNC_REVERSE_SUBTRACT : !strcmp(value, "add") ? GL_FUNC_ADD : 0;
		}
		else if (!strcmp(word, "debug"))
			lab.debug = atoi(value);
#ifdef HALO_CAPTURE
		/* (the builds with screenshots: capture.h) */
		else if (!strcmp(word, "shot"))
			capture_request_screenshot();
#endif
		else if (!strcmp(word, "time") && frames > 0)
		{
			free(lab.intervals);
			lab.intervals = calloc((size_t)frames, sizeof(double));
			free(lab.records);
			lab.records = calloc((size_t)frames, sizeof(*lab.records));
			{
				struct ml_rt_frame_stats discarded;

				ml_rt_frame_stats_take(&discarded);
			}
			snprintf(lab.label, sizeof(lab.label), "%s", value);
			lab.frames_wanted = frames;
			lab.frames = 0;
			lab.gpu_ms_sum = lab.frame_gpu_ms_sum = lab.rt_ms_sum = 0.0;
			lab.cpu_ms_sum = lab.cpu_trace_ms_sum = lab.scene_ms_sum = 0.0;
			lab.vk_build_ms_sum = lab.vk_trace_ms_sum = 0.0;
			memset(marks.sums, 0, sizeof(marks.sums));
			memset(marks.counts, 0, sizeof(marks.counts));
			lab.gpu_count = lab.frame_gpu_count = lab.rt_count = 0;
		}
		else if (!strcmp(word, "say"))
		{
			char said[700];
			char rt[500];

			ml_rt_say(rt, sizeof(rt));
			snprintf(said, sizeof(said), "say %s rt=%d rt_ok=%d check_frames=%d check_failures=%d %s", value,
				lab.ray_tracing, ml.rt_ok, lab.check_frames, lab.check_failures, rt);
			lab_say(said);
		}
	}
	fclose(file);
	remove(taken);
	lab_say("done");
}

/* each frame, at the first pass: the time since the last, and the GPU's
timestamps from some frames ago */
static void lab_frame_begin(void)
{
	double time = now_ms();
	int slot = lab.query_frame % 64;

	marks_next();

	if (lab.frames_wanted && lab.last_time > 0.0)
	{
		if (lab.records)
		{
			struct lab_record *record = &lab.records[lab.frames];

			record->interval = time - lab.last_time;
			record->cpu_ml_ms = lab.frame_cpu_ml_ms;
			record->cpu_trace_ms = lab.frame_cpu_trace_ms;
			record->scene_ms = lab.frame_scene_ms;
			ml_rt_frame_stats_take(&record->rt);
		}
		lab.intervals[lab.frames++] = time - lab.last_time;
		if (lab.frames >= lab.frames_wanted)
			lab_results();
	}
	lab.frame_cpu_ml_ms = lab.frame_cpu_trace_ms = lab.frame_scene_ms = 0.0;
	lab.last_time = time;
	if (!ext.QueryCounter || !ext.GetQueryObjectui64v)
		return;
	if (!lab.queries[0][0])
	{
		int index;

		for (index = 0; index < 64; index++)
			glGenQueries(2, lab.queries[index]);
	}
	if (lab.query_frame >= 64)
	{
		unsigned long long start = 0, end = 0;
		GLint available = 0;

		if (ext.GetQueryObjectiv)
			ext.GetQueryObjectiv(lab.queries[slot][1], GL_QUERY_RESULT_AVAILABLE, &available);
		if (available)
		{
			ext.GetQueryObjectui64v(lab.queries[slot][0], GL_QUERY_RESULT, &start);
			ext.GetQueryObjectui64v(lab.queries[slot][1], GL_QUERY_RESULT, &end);
			if (lab.frames_wanted)
			{
				if (end > start)
				{
					lab.gpu_ms_sum += (double)(end - start) / 1000000.0;
					lab.gpu_count++;
				}
				if (lab.previous_start && start > lab.previous_start)
				{
					lab.frame_gpu_ms_sum += (double)(start - lab.previous_start) / 1000000.0;
					lab.frame_gpu_count++;
				}
			}
			lab.previous_start = start;
		}
	}
	ext.QueryCounter(lab.queries[slot][0], GL_TIMESTAMP);
}

static void lab_frame_end(void)
{
	if (ext.QueryCounter && lab.queries[0][0])
		ext.QueryCounter(lab.queries[lab.query_frame % 64][1], GL_TIMESTAMP);
	lab.query_frame++;
	if (lab.frames_wanted)
	{
		float rt = ml_rt_gpu_ms();

		if (rt >= 0.0f)
		{
			float build = 0.0f, traced = 0.0f;

			ml_rt_gpu_phases(&build, &traced);
			lab.rt_ms_sum += rt;
			lab.vk_build_ms_sum += build;
			lab.vk_trace_ms_sum += traced;
			lab.rt_count++;
		}
	}
}

/* the lab's trace: the mean (0-255, RGB) of a view's rectangle of a
texture, from a 64x36 copy of it; -1 if it could not be read */
static float lab_probe(GLuint texture, const int corners[4])
{
	static unsigned char pixels[64 * 36 * 4];
	GLuint read = 0;
	GLint pack_buffer = 0;
	double sum = 0.0;
	int index;

	if (!texture)
		return -1.0f;
	if (!lab.probe_texture)
	{
		lab.probe_texture = texture_make(GL_RGBA8, 64, 36);
		lab.probe_framebuffer = framebuffer_make(lab.probe_texture, 0);
	}
	glDisable(GL_SCISSOR_TEST);
	glGenFramebuffers(1, &read);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, lab.probe_framebuffer);
	glBlitFramebuffer(corners[0], corners[1], corners[2], corners[3], 0, 0, 64, 36, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, lab.probe_framebuffer);
	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glReadPixels(0, 0, 64, 36, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)pack_buffer);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &read);
	for (index = 0; index < 64 * 36; index++)
		sum += pixels[index * 4] + pixels[index * 4 + 1] + pixels[index * 4 + 2];
	return (float)(sum / (64.0 * 36.0 * 3.0));
}

/* the view now (resolved), for the trace */
static float lab_probe_view(const struct halo_ml_frame *frame)
{
	struct halo_ml_targets targets;
	float mean;

	if (!halo_ml_targets_get((short)frame->bounds[0], (short)frame->bounds[1], (short)frame->bounds[2],
		(short)frame->bounds[3], TRUE, &targets))
	{
		return -1.0f;
	}
	mean = lab_probe(targets.color_texture, targets.corners);
	pass_state();
	return mean;
}

/* the frame's line, at the tone map (final: the view before the HUD) */
static void lab_trace_line(float final)
{
	char path[1100];
	FILE *file;

	if (!lab.trace || !lab.path[0])
		return;
	snprintf(path, sizeof(path), "%s.trace", lab.path);
	file = fopen(path, "a");
	if (file)
	{
		fprintf(file, "frame=%lu equation=0x%04x traced=%d before=%.1f after=%.1f final=%.1f%s\n", lab.trace_frame,
			(unsigned int)lab.trace_equation, lab.trace_traced, (double)lab.trace_before, (double)lab.trace_after,
			(double)final, lab.trace_after > lab.trace_before + 1.5f && lab.trace_before >= 0.0f ? " BRIGHTER" : "");
		fclose(file);
	}
	lab.trace_before = lab.trace_after = -1.0f;
	lab.trace_traced = -1;
	lab.trace_equation = 0;
}

/* the composite has drawn (or not): the check, the composite having only
darkened the view (it multiplies it by the lighting, at most 1) */
static void lab_check(float before, float after)
{
	if (before < 0.0f || after < 0.0f)
		return;
	lab.check_frames++;
	if (after > before + 1.5f)
	{
		char said[200];

		lab.check_failures++;
		snprintf(said, sizeof(said), "check FAIL frame=%lu equation=0x%04x before=%.1f after=%.1f", lab.trace_frame,
			(unsigned int)lab.trace_equation, (double)before, (double)after);
		if (lab.check_failures <= 20)
			lab_say(said);
	}
}

/* the sun's place on the screen (0 to 1), for the shafts */
static void sun_screen_find(const struct halo_ml_frame *frame)
{
	float point[4], clip[4];
	int row;

	ml.sun_screen[2] = 0.0f;
	if (!frame->sun_valid || !frame->outdoor)
		return;
	point[0] = frame->camera[0] + frame->sun[0] * 1000.0f;
	point[1] = frame->camera[1] + frame->sun[1] * 1000.0f;
	point[2] = frame->camera[2] + frame->sun[2] * 1000.0f;
	point[3] = 1.0f;
	for (row = 0; row < 4; row++)
	{
		clip[row] = frame->world_to_clip[row * 4 + 0] * point[0] + frame->world_to_clip[row * 4 + 1] * point[1] +
			frame->world_to_clip[row * 4 + 2] * point[2] + frame->world_to_clip[row * 4 + 3];
	}
	if (clip[3] <= 0.0f)
		return;
	ml.sun_screen[0] = clip[0] / clip[3] * 0.5f + 0.5f;
	ml.sun_screen[1] = 0.5f - clip[1] / clip[3] * 0.5f;
	/* (fading as it leaves the screen) */
	{
		float dx = ml.sun_screen[0] < 0.0f ? -ml.sun_screen[0] : ml.sun_screen[0] > 1.0f ? ml.sun_screen[0] - 1.0f : 0.0f;
		float dy = ml.sun_screen[1] < 0.0f ? -ml.sun_screen[1] : ml.sun_screen[1] > 1.0f ? ml.sun_screen[1] - 1.0f : 0.0f;
		float away = dx > dy ? dx : dy;

		ml.sun_screen[2] = away >= 0.5f ? 0.0f : 1.0f - away * 2.0f;
	}
}

/* the bloom's GPU time, for the lab */
static void lab_gpu_begin(void)
{
	if (!ext.QueryCounter)
		return;
	if (!lab.bloom_queries[0][0])
	{
		int index;

		for (index = 0; index < 64; index++)
			glGenQueries(2, lab.bloom_queries[index]);
	}
	if (lab.bloom_query >= 64 && ext.GetQueryObjectiv)
	{
		GLint available = 0;
		int slot = lab.bloom_query % 64;

		ext.GetQueryObjectiv(lab.bloom_queries[slot][1], GL_QUERY_RESULT_AVAILABLE, &available);
		if (available && lab.frames_wanted)
		{
			unsigned long long start = 0, end = 0;

			ext.GetQueryObjectui64v(lab.bloom_queries[slot][0], GL_QUERY_RESULT, &start);
			ext.GetQueryObjectui64v(lab.bloom_queries[slot][1], GL_QUERY_RESULT, &end);
			if (end > start)
			{
				lab.gpu_ms_sum += (double)(end - start) / 1000000.0;
			}
		}
	}
	ext.QueryCounter(lab.bloom_queries[lab.bloom_query % 64][0], GL_TIMESTAMP);
}

static void lab_gpu_end(void)
{
	if (!ext.QueryCounter || !lab.bloom_queries[0][0])
		return;
	ext.QueryCounter(lab.bloom_queries[lab.bloom_query % 64][1], GL_TIMESTAMP);
	lab.bloom_query++;
}

/* ---------- the passes */

/* whether this machine can: OpenGL drawing (not bgfx), its interop with
Vulkan, and a Vulkan device on its GPU with ray queries (made and taken down
again) */
static void rt_probe(void)
{
	unsigned char uuid[16];

	if (ml.rt_support >= 0)
		return;
	if (ml.rt_ok)
	{
		ml.rt_support = 1;
		return;
	}
	if (!ml_rt_platform())
	{
		ml.rt_support = 0;
		ml.rt_reason = RT_WHY_PLATFORM;
		platform_log("ray tracing: not in this build (64-bit Linux only)");
		return;
	}
	if (!halo_ml_renderer_ready())
	{
		/* (no renderer yet: asked again later; bgfx's: never) */
		if (halo_ml_renderer_bgfx())
		{
			ml.rt_support = 0;
			ml.rt_reason = RT_WHY_RENDERER;
			platform_log("ray tracing: not supported on this renderer (OpenGL only)");
		}
		return;
	}
	ext_load();
	if (!ext.interop)
	{
		ml.rt_support = 0;
		ml.rt_reason = RT_WHY_SHARING;
		platform_log("ray tracing: not supported: OpenGL has no GL_EXT_memory_object_fd and GL_EXT_semaphore_fd");
		return;
	}
	memset(uuid, 0, sizeof(uuid));
	ext.GetUnsignedBytei_vEXT(GL_DEVICE_UUID_EXT, 0, uuid);
	ml.rt_support = ml_rt_probe(uuid) ? 1 : 0;
	ml.rt_reason = ml.rt_support ? RT_WHY_NONE : ml_rt_why == ML_RT_WHY_NO_VULKAN ? RT_WHY_NO_VULKAN :
		ml_rt_why == ML_RT_WHY_OTHER_GPU ? RT_WHY_OTHER_GPU : RT_WHY_NO_RAY_QUERIES;
	platform_log("ray tracing: %s on this GPU", ml.rt_support ? "supported" :
		ml.rt_reason == RT_WHY_NO_VULKAN ? "not supported (no Vulkan 1.2: libvulkan.so.1 or its driver missing)" :
		ml.rt_reason == RT_WHY_OTHER_GPU ? "not supported (no Vulkan device is OpenGL's GPU)" :
		"not supported (no Vulkan ray queries)");
}

int halo_ml_ray_tracing_support(int now)
{
	if (ml.rt_ok)
		return 1;
	if (ml.rt_support < 0)
	{
		ml.rt_support_asked = TRUE;
		if (now)
			rt_probe();
	}
	return ml.rt_support;
}

const char *halo_ml_ray_tracing_reason(void)
{
	if (ml.rt_ok)
		return NULL;
	if (ml.rt_support == 0)
	{
		switch (ml.rt_reason)
		{
		case RT_WHY_RENDERER:
			return "NEEDS THE OPENGL RENDERER";
		case RT_WHY_PLATFORM:
			return "NOT ON THIS PLATFORM";
		case RT_WHY_SHARING:
			return "DRIVER LACKS GL-VULKAN SHARING";
		case RT_WHY_NO_VULKAN:
			return "NO VULKAN DRIVER";
		case RT_WHY_OTHER_GPU:
			return "VULKAN IS ON ANOTHER GPU";
		default:
			return "NO RAY TRACING ON THIS GPU";
		}
	}
	/* (supported, but its start failed: off until the setting is turned
	off and on again) */
	if (ml.rt_tried && ml.rt_reason == RT_WHY_START && ml.ray_tracing_wanted != HALO_RT_OFF)
		return "VULKAN DIDN'T START";
	return NULL;
}

int halo_ml_ray_tracing_shown(void)
{
	return halo_ml_ray_tracing_support(TRUE) != 0 || ml.rt_reason == RT_WHY_RENDERER;
}

int halo_ml_glow_support(void)
{
	static int logged;
	int support;

	if (!halo_ml_renderer_ready() || halo_ml_renderer_bgfx())
		return -1;
	ext_load();
	/* (halo_ml_bloom's own check) */
	support = ext.TexStorage2D && glCopyImageSubData ? 1 : 0;
	if (!support && !logged)
	{
		logged = 1;
		platform_log("glow: not supported: OpenGL has no glTexStorage2D and glCopyImageSubData (OpenGL 4.3)");
	}
	return support;
}

static BOOL rt_start(void)
{
	unsigned char uuid[16];

	if (ml.rt_tried)
		return ml.rt_ok;
	ml.rt_tried = TRUE;
	rt_probe();
	if (ml.rt_support == 0)
		return FALSE;
	ext_load();
	if (!ext.interop)
		return FALSE;
	memset(uuid, 0, sizeof(uuid));
	ext.GetUnsignedBytei_vEXT(GL_DEVICE_UUID_EXT, 0, uuid);
	ml.rt_ok = ml_rt_start(uuid);
	if (!ml.rt_ok)
	{
		ml.rt_reason = RT_WHY_START;
		platform_log("ray tracing: the Vulkan side path did not start; off");
	}
	else
	{
		ml.rt_support = 1;
		ml.rt_reason = RT_WHY_NONE;
	}
	ml.history_valid = FALSE;
	return ml.rt_ok;
}

/* the setting turned off: OpenGL's copies of the shared images and
semaphores let go (OpenGL done with them first), then the Vulkan side path
taken down; turned on again, it is made again */
static void rt_stop(void)
{
	if (ml.rt_ok || ml.rt_textures[0] || ml.rt_semaphores[0])
	{
		glFinish();
		if (ml.rt_textures[0])
		{
			glDeleteTextures(4, ml.rt_textures);
			ext.DeleteMemoryObjectsEXT(4, ml.rt_memory);
			glDeleteFramebuffers(1, &ml.rt_framebuffer);
		}
		if (ml.rt_semaphores[0] && ext.DeleteSemaphoresEXT)
			ext.DeleteSemaphoresEXT(2, ml.rt_semaphores);
		memset(ml.rt_textures, 0, sizeof(ml.rt_textures));
		memset(ml.rt_memory, 0, sizeof(ml.rt_memory));
		memset(ml.rt_semaphores, 0, sizeof(ml.rt_semaphores));
		ml.rt_framebuffer = 0;
		ml.rt_generation = 0;
		xgpu_gl_state_invalidate();
	}
	ml_rt_stop();
	ml.rt_ok = FALSE;
	ml.rt_tried = FALSE;
	ml.history_valid = FALSE;
}

int halo_ml_rt_texture(const void *key, const void *hardware_format, const unsigned int *palette)
{
	unsigned long width = 0, height = 0;
	unsigned char *rgba;
	int texture;

	if (!ml.rt_ok)
		return -2;
	texture = ml_rt_texture_find(key);
	if (texture != -1)
		return texture;
	rgba = hardware_format ? xgpu_texture_rgba_palette((const DWORD *)hardware_format, (const D3DCOLOR *)palette, &width,
		&height) : NULL;
	if (!rgba && hardware_format)
	{
		const DWORD *resource = (const DWORD *)hardware_format;
		struct xgpu_texture_description description;

		xgpu_texture_describe(resource[3], resource[4], &description);
		platform_log("ray tracing: a texture's alpha not readable: format 0x%lx %lux%lu depth %lu cube %d common 0x%lx",
			(unsigned long)description.format, description.width, description.height, description.depth,
			(int)description.cube_map, (unsigned long)resource[0]);
	}
	texture = ml_rt_texture_add(key, rgba, (int)width, (int)height);
	free(rgba);
	return texture;
}

static void halo_ml_opaque_timed(const struct halo_ml_frame *original);

void halo_ml_scene_ms(double ms)
{
	if (lab.frames_wanted)
	{
		lab.scene_ms_sum += ms;
		lab.frame_scene_ms += ms;
	}
}

double halo_ml_now_ms(void)
{
	return now_ms();
}

void halo_ml_opaque(const struct halo_ml_frame *original)
{
	double before = now_ms();

	halo_ml_opaque_timed(original);
	if (lab.frames_wanted)
	{
		lab.cpu_ms_sum += now_ms() - before;
		lab.frame_cpu_ml_ms += now_ms() - before;
	}
}

static void halo_ml_opaque_timed(const struct halo_ml_frame *original)
{
	struct halo_ml_frame copy = *original;
	const struct halo_ml_frame *frame = &copy;
	struct halo_ml_targets targets;
	int ray_tracing;
	GLuint position, normal, position_framebuffer, lighting, lit_view;
	BOOL traced = FALSE;
	float strength[4];
	int quality = HALO_RT_QUALITY_HIGH, scale = 1;
	/* the corners of the trace's grid: the view's, or its low-resolution corner */
	int grid_corners[4];
	/* each 2x2 block's pixel traced, a frame each in turn */
	static const int offsets[4][2] = { { 0, 0 }, { 1, 1 }, { 1, 0 }, { 0, 1 } };
	/* (low's 3x3 blocks: each of the nine in turn, spread out) */
	static const int offsets3[9][2] =
		{ { 0, 0 }, { 1, 1 }, { 2, 2 }, { 2, 0 }, { 0, 2 }, { 1, 0 }, { 2, 1 }, { 0, 1 }, { 1, 2 } };
	/* (4x4 blocks, a large view's: the ordered dither's order) */
	static const int offsets4[16][2] =
		{ { 0, 0 }, { 2, 2 }, { 2, 0 }, { 0, 2 }, { 1, 1 }, { 3, 3 }, { 3, 1 }, { 1, 3 },
		  { 1, 0 }, { 3, 2 }, { 3, 0 }, { 1, 2 }, { 0, 1 }, { 2, 3 }, { 2, 1 }, { 0, 3 } };

	lab_frame = frame;
	lab_poll();
	lab_frame = NULL;
	if (lab.force_equation)
	{
		/* (as a game draw leaves it: the state cache told) */
		glBlendEquation(lab.force_equation);
		xgpu_gl_state_invalidate();
	}
	if (lab.trace)
	{
		lab.trace_frame++;
		glGetIntegerv(GL_BLEND_EQUATION_RGB, &lab.trace_equation);
		lab.trace_traced = 0;
	}
	if (lab.network)
		copy.network_game = TRUE;
	if (lab.sun[0] != 0.0f || lab.sun[1] != 0.0f || lab.sun[2] != 0.0f)
	{
		float length = sqrtf(lab.sun[0] * lab.sun[0] + lab.sun[1] * lab.sun[1] + lab.sun[2] * lab.sun[2]);

		copy.sun[0] = lab.sun[0] / length;
		copy.sun[1] = lab.sun[1] / length;
		copy.sun[2] = lab.sun[2] / length;
		copy.sun_valid = TRUE;
	}
	/* (the menus asked whether this machine can) */
	if (ml.rt_support_asked && ml.rt_support < 0)
		rt_probe();
	ray_tracing = halo_ml_ray_tracing_mode();
	/* (turned off: the Vulkan side path down; turned on: up) */
	if (ml.ray_tracing_wanted == HALO_RT_OFF && (ml.rt_ok || ml.rt_tried))
		rt_stop();
	if (ray_tracing != HALO_RT_OFF && !ml.rt_tried)
	{
		rt_start();
		ray_tracing = halo_ml_ray_tracing_mode();
	}
	if (ray_tracing == HALO_RT_OFF)
	{
		/* (the lab times this mode too) */
		ext_load();
		lab_frame_begin();
		lab_frame_end();
		return;
	}
	ext_load();
	if (!ext.TexStorage2D || !ext.TextureView || !glCopyImageSubData || !programs_prepare())
		return;
	if (!matrix_inverse(frame->world_to_clip, ml.to_world))
		return;
	memcpy(ml.to_clip, frame->world_to_clip, sizeof(ml.to_clip));
	if (!halo_ml_targets_get((short)frame->bounds[0], (short)frame->bounds[1], (short)frame->bounds[2],
		(short)frame->bounds[3], FALSE, &targets))
	{
		return;
	}
	lab_frame_begin();
	ml.frame++;
	textures_fit(&targets);
	sun_screen_find(frame);
	pass_state();

	/* 1. the depths copied (and their stencil), and the positions and normals */
	mark(_mark_depth_copy);
	glCopyImageSubData(targets.depth_texture, GL_TEXTURE_2D, 0, 0, 0, 0, ml.depth_copy, GL_TEXTURE_2D, 0, 0, 0, 0,
		targets.width, targets.height, 1);
	/* (the images shared with Vulkan, where the prepass writes and the
	trace reads: none, nothing drawn) */
	if (!rt_textures_fit(targets.width, targets.height))
	{
		lab_frame_end();
		halo_ml_targets_drawn(FALSE);
		return;
	}
	position = ml.rt_textures[0];
	normal = ml.rt_textures[1];
	position_framebuffer = ml.rt_framebuffer;
	traced = TRUE;
	quality = rt_quality();
	/* (low: one pixel of each 3x3 block, a ninth of the rays) */
	scale = quality == HALO_RT_QUALITY_HIGH ? 1 : quality == HALO_RT_QUALITY_MEDIUM ? 2 : 3;
	/* (below high, the trace's grid as fine as at 1280x800 whatever the
	view's size: its blocks as many as there, each as wide in the view as
	there, so that the work does not grow with the pixels; 1920x1080 traces
	one of each 3x3 at medium, 4x4 at low, and looks as 1280x800 does) */
	if (scale > 1)
	{
		double pixels = (double)(targets.corners[2] - targets.corners[0]) *
			(double)(targets.corners[3] - targets.corners[1]);
		int sized = (int)((double)scale * sqrt(pixels / (1280.0 * 800.0)) + 0.5);

		scale = sized > scale ? (sized > 4 ? 4 : sized) : scale;
	}
	/* the grid: every pixel (high), or one of each 2x2 (medium) or 3x3
	(low) block, a different one each frame; a history of another grid not
	used */
	memcpy(ml.previous_grid, ml.grid, sizeof(ml.grid));
	ml.grid[0] = (float)scale;
	ml.grid[1] = scale == 4 ? (float)offsets4[ml.frame & 15][0] : scale == 3 ? (float)offsets3[ml.frame % 9][0] :
		scale > 1 ? (float)offsets[ml.frame & 3][0] : 0.0f;
	ml.grid[2] = scale == 4 ? (float)offsets4[ml.frame & 15][1] : scale == 3 ? (float)offsets3[ml.frame % 9][1] :
		scale > 1 ? (float)offsets[ml.frame & 3][1] : 0.0f;
	if (ml.previous_grid[0] != ml.grid[0])
		ml.history_valid = FALSE;
	grid_corners[0] = targets.corners[0];
	grid_corners[1] = targets.corners[1];
	grid_corners[2] = targets.corners[0] + (targets.corners[2] - targets.corners[0] + scale - 1) / scale;
	grid_corners[3] = targets.corners[1] + (targets.corners[3] - targets.corners[1] + scale - 1) / scale;
	ml.last_position = position;
	ml.last_normal = normal;
	common_uniforms(ml.programs[_program_prepass], &targets, frame);
	/* (the last frame's shine, where the reflected rays go: known when it
	was made the frame before, else every pixel) */
	glUniform4fv(glGetUniformLocation(ml.programs[_program_prepass], "previous_clip"), 4, ml.previous_clip);
	uniform4(ml.programs[_program_prepass], "shine_params",
		ml.shine_frame == ml.frame - 1 && ml.history_valid ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
	texture_bind(_unit_shine, ml.shine, FALSE);
	texture_bind(_unit_depth, ml.depth_copy, FALSE);
	texture_bind(_unit_stencil, ml.stencil_view, FALSE);
	mark(_mark_prepass);
	/* (on a grid, the traced pixels' alone, at their blocks' texels) */
	pass_draw(position_framebuffer, grid_corners);
	mark(_mark_trace);

	/* 2. the lighting */
	if (traced)
	{
		struct ml_rt_trace trace;
		GLuint shared[4];
		GLenum layouts[4] = { GL_LAYOUT_GENERAL_EXT, GL_LAYOUT_GENERAL_EXT, GL_LAYOUT_GENERAL_EXT,
			GL_LAYOUT_GENERAL_EXT };

		memcpy(shared, ml.rt_textures, sizeof(shared));
		memset(&trace, 0, sizeof(trace));
		memcpy(trace.camera, frame->camera, sizeof(trace.camera));
		memcpy(trace.sun, frame->sun, sizeof(trace.sun));
		if (!frame->sun_valid)
			trace.sun[0] = trace.sun[1] = trace.sun[2] = 0.0f;
		trace.mode = ray_tracing;
		trace.frame = ml.frame;
		trace.viewer_instance = frame->viewer_instance;
		memcpy(trace.corners, targets.corners, sizeof(trace.corners));
		trace.ao_radius = 0.35f;
		/* (wider than the sun's own 0.27 degrees: Halo's sun is a big soft one) */
		trace.sun_cone = 0.018f;
		/* (fewer rays: the accumulation over frames and the blur average them) */
		trace.shadow_rays = quality == HALO_RT_QUALITY_HIGH ? 4 : quality == HALO_RT_QUALITY_MEDIUM ? 2 : 1;
		trace.ao_rays = trace.shadow_rays;
		trace.blue_noise = quality != HALO_RT_QUALITY_HIGH;
		trace.scale = scale;
		trace.offset[0] = (int)ml.grid[1];
		trace.offset[1] = (int)ml.grid[2];
		trace.debug = lab.debug == 5 ? 1 : 0;
		trace.flags = lab.rt_flags;
		ao_fade(ml.to_clip, targets.corners[2] - targets.corners[0], scale, trace.ao_radius, trace.ao_fade);
		int result;

		ext.SignalSemaphoreEXT(ml.rt_semaphores[0], 0, NULL, 4, shared, layouts);
		glFlush();
		{
			double before = now_ms();

			result = ml_rt_trace(&trace);
			if (lab.frames_wanted)
			{
				lab.cpu_trace_ms_sum += now_ms() - before;
				lab.frame_cpu_trace_ms += now_ms() - before;
			}
		}
		/* (-1: nothing submitted, nothing to wait for; 0: submitted, but
		nothing traced yet, the level's structure not made) */
		if (result >= 0)
			ext.WaitSemaphoreEXT(ml.rt_semaphores[1], 0, NULL, 4, shared, layouts);
		if (result <= 0)
		{
			lab_frame_end();
			halo_ml_targets_drawn(FALSE);
			return;
		}
		lighting = ml.rt_textures[2];
	}

	/* 2b. accumulated over frames (where the point was seen before) */
	mark(_mark_temporal);
	{
		GLuint program = ml.programs[_program_temporal];
		int next = ml.history_index ^ 1;

		common_uniforms(program, &targets, frame);
		glUniform4fv(glGetUniformLocation(program, "previous_clip"), 4, ml.previous_clip);
		glUniform4fv(glGetUniformLocation(program, "previous_camera"), 1, ml.previous_camera);
		uniform4(program, "params", traced ? 0.2f : 0.35f, ml.history_valid ? 1.0f : 0.0f, 0.0f, 0.0f);
		texture_bind(_unit_source, lighting, FALSE);
		texture_bind(_unit_position, position, FALSE);
		texture_bind(_unit_normal, normal, FALSE);
		texture_bind(_unit_history, ml.history[ml.history_index], FALSE);
		texture_bind(_unit_previous_position, ml.guide[ml.history_index], FALSE);
		texture_bind(_unit_previous_guide_normal, ml.guide_normal[ml.history_index], FALSE);
		/* (the history, and this frame's guide beside it: the last frame's
		is the other's, no copy of the positions) */
		pass_draw(ml.history_framebuffers[next], grid_corners);
		ml.history_index = next;
		lighting = ml.history[next];
		memcpy(ml.previous_clip, frame->world_to_clip, sizeof(ml.previous_clip));
		ml.previous_camera[0] = frame->camera[0];
		ml.previous_camera[1] = frame->camera[1];
		ml.previous_camera[2] = frame->camera[2];
		ml.history_valid = TRUE;
	}

	/* 3. the blur, across and down */
	mark(_mark_blur);
	{
		GLuint program = ml.programs[_program_blur];

		common_uniforms(program, &targets, frame);
		texture_bind(_unit_guide, ml.guide[ml.history_index], FALSE);
		texture_bind(_unit_guide_normal, ml.guide_normal[ml.history_index], FALSE);
		texture_bind(_unit_source, lighting, FALSE);
		uniform4(program, "direction", 1.0f, 0.0f, 0.0f, 0.0f);
		pass_draw(ml.lighting_framebuffers[1], grid_corners);
		texture_bind(_unit_source, ml.lighting[1], FALSE);
		uniform4(program, "direction", 0.0f, 1.0f, 0.0f, 0.0f);
		pass_draw(ml.lighting_framebuffers[2], grid_corners);
	}

	/* the view as the game lit it, for the traced shadows' fade */
	mark(_mark_scene_copy);
	lit_view = ml.scene_lit;
	if (traced && halo_ml_targets_get((short)frame->bounds[0], (short)frame->bounds[1], (short)frame->bounds[2],
		(short)frame->bounds[3], TRUE, &targets))
	{
		lit_view = view_readable(&targets, ml.scene_lit);
		pass_state();
	}

	if (lab.trace)
	{
		lab.trace_traced = traced;
		lab.trace_before = lab_probe_view(frame);
	}

	/* 4. over the view: the opaque surfaces but the first-person weapon */
	mark(_mark_composite);
	{
		GLuint program = ml.programs[_program_composite];
		BOOL any_pixel_floor = !traced;

		/* (Track A: the sun's shadows of the objects, a share of the
		light: the lightmaps' own are in already) */
		strength[0] = traced ? (ray_tracing >= HALO_RT_AO ? 0.85f : 0.0f) : 0.9f;
		strength[1] = traced ? 0.8f : 0.55f;
		strength[2] = frame->network_game ? ml.unit_floor : 0.0f;
		strength[3] = any_pixel_floor ? 1.0f : 0.0f;
		common_uniforms(program, &targets, frame);
		glUniform4fv(glGetUniformLocation(program, "strength"), 1, strength);
		uniform4(program, "debug", (float)lab.debug, 0.0f, 0.0f, 0.0f);
		texture_bind(_unit_position, position, FALSE);
		texture_bind(_unit_normal, normal, FALSE);
		texture_bind(_unit_lighting, ml.lighting[2], FALSE);
		texture_bind(_unit_guide, ml.guide[ml.history_index], FALSE);
		texture_bind(_unit_guide_normal, ml.guide_normal[ml.history_index], FALSE);
		texture_bind(_unit_scene_now, lit_view, FALSE);
		texture_bind(_unit_depth, ml.depth_copy, FALSE);
		uniform4(program, "fade", traced ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
		glEnable(GL_STENCIL_TEST);
		glStencilFunc(GL_EQUAL, 0, 1);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		glStencilMask(0);
		if (!lab.debug)
		{
			glEnable(GL_BLEND);
			glBlendFunc(GL_DST_COLOR, GL_ZERO);
		}
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		pass_draw(targets.draw_framebuffer, targets.corners);
		glDisable(GL_BLEND);
		glDisable(GL_STENCIL_TEST);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}
	halo_ml_targets_drawn(TRUE);
	if (lab.trace)
	{
		lab.trace_after = lab_probe_view(frame);
		lab_check(lab.trace_before, lab.trace_after);
	}

	/* (the view before the shine and the reflections, for the traced
	reflections: halo_ml_shine's, once the dynamic lights' shine is drawn) */
	ml.reflections_traced = traced && ray_tracing >= HALO_RT_AO;
	ml.shine_before = FALSE;
	mark(_mark_end);
	/* (traced: what follows the wait on the trace sent now, not with the
	rest of the frame, so that the GPU goes on as soon as the trace is done
	rather than idle until the frame's end is sent) */
	if (traced)
		glFlush();
	lab_frame_end();
	halo_ml_targets_drawn(FALSE);
}

/* the view before the level's shine and reflections, after the dynamic
lights' (a muzzle flash's on the floor is not the floor's shine: with it a
flash lit up reflections on every surface near it) */
void halo_ml_shine(const struct halo_ml_frame *frame)
{
	struct halo_ml_targets targets;

	if (ml.failed || !ml.reflections_traced || !ml.scene_before)
		return;
	if (!halo_ml_targets_get((short)frame->bounds[0], (short)frame->bounds[1], (short)frame->bounds[2],
		(short)frame->bounds[3], TRUE, &targets) || targets.width != ml.width || targets.height != ml.height)
	{
		return;
	}
	glCopyImageSubData(targets.color_texture, GL_TEXTURE_2D, 0, 0, 0, 0, ml.scene_before, GL_TEXTURE_2D, 0, 0, 0, 0,
		targets.width, targets.height, 1);
	ml.shine_before = TRUE;
	halo_ml_targets_drawn(FALSE);
}

void halo_ml_reflections(const struct halo_ml_frame *frame)
{
	struct halo_ml_targets targets;
	GLuint program, now_view;

	if (ml.failed || !ml.reflections_traced || !ml.shine_before || !ml.depth_copy || !ml.last_position ||
		!ml.programs[_program_reflections])
	{
		return;
	}
	if (!halo_ml_targets_get((short)frame->bounds[0], (short)frame->bounds[1], (short)frame->bounds[2],
		(short)frame->bounds[3], TRUE, &targets) || targets.width != ml.width || targets.height != ml.height)
	{
		return;
	}
	if (!matrix_inverse(frame->world_to_clip, ml.to_world))
		return;
	memcpy(ml.to_clip, frame->world_to_clip, sizeof(ml.to_clip));
	mark(_mark_reflections);
	now_view = view_readable(&targets, ml.scene_now);
	pass_state();
	texture_bind(_unit_scene_before, ml.scene_before, FALSE);
	texture_bind(_unit_scene_now, now_view, FALSE);
	/* where the shine was drawn, for the next frame's reflected rays (before
	the reflections are drawn over the view it reads) */
	{
		int shine_corners[4];

		shine_corners[0] = shine_corners[1] = 0;
		shine_corners[2] = (targets.corners[2] - targets.corners[0] + 7) / 8;
		shine_corners[3] = (targets.corners[3] - targets.corners[1] + 7) / 8;
		program = ml.programs[_program_shine];
		common_uniforms(program, &targets, frame);
		pass_draw(ml.shine_framebuffer, shine_corners);
		ml.shine_frame = ml.frame;
	}
	texture_bind(_unit_reflection_hits, ml.rt_textures[3], FALSE);
	texture_bind(_unit_guide, ml.guide[ml.history_index], FALSE);
	texture_bind(_unit_guide_normal, ml.guide_normal[ml.history_index], FALSE);
	texture_bind(_unit_position, ml.last_position, FALSE);
	texture_bind(_unit_depth, ml.depth_copy, FALSE);
	texture_bind(_unit_normal, ml.last_normal, FALSE);
	/* (on a grid of blocks: the grid's reflected view first, complete) */
	if (ml.grid[0] > 1.5f)
	{
		int scale = (int)ml.grid[0], grid_corners[4];

		grid_corners[0] = targets.corners[0];
		grid_corners[1] = targets.corners[1];
		grid_corners[2] = targets.corners[0] + (targets.corners[2] - targets.corners[0] + scale - 1) / scale;
		grid_corners[3] = targets.corners[1] + (targets.corners[3] - targets.corners[1] + scale - 1) / scale;
		program = ml.programs[_program_reflect_grid];
		common_uniforms(program, &targets, frame);
		uniform4(program, "params", 0.8f, 60.0f, (float)(ml.frame & 1), 0.0f);
		pass_draw(ml.reflected_grid_framebuffer, grid_corners);
		texture_bind(_unit_reflected_grid, ml.reflected_grid, FALSE);
	}
	program = ml.programs[_program_reflections];
	common_uniforms(program, &targets, frame);
	uniform4(program, "params", 0.8f, 60.0f, (float)(ml.frame & 1), lab.debug == 6 ? 1.0f : 0.0f);
	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_EQUAL, 0, 1);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glStencilMask(0);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	pass_draw(targets.draw_framebuffer, targets.corners);
	glDisable(GL_STENCIL_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	mark(_mark_reflections_end);
	halo_ml_targets_drawn(TRUE);
}

/* the bloom and display.glow: "full" alone gives a faint bloom over the light
past white (display.hdr's); display.glow a stronger one, from a lower
threshold (so that the glows Halo draws at white, its self-lit surfaces,
lights, plasma and muzzle flashes, bloom without display.hdr too), seven
levels wide, and with "shafts" the sky's light streaked toward the sun.
texture: the view's colors (resolved); framebuffer: where the sum goes */
void halo_ml_bloom(unsigned int texture, unsigned int framebuffer, int width, int height, const int corners[4])
{
	int level;
	GLint attached = 0;
	/* the view the bloom is made from: its own texture, or a copy of it */
	GLuint source;
	BOOL feedback;
	int sizes[BLOOM_LEVELS][2];
	int whole[4];
	float threshold, knee, intensity;

	if (!halo_ml_glow_on())
		return;
	ext_load();
	if (!ext.TexStorage2D || !glCopyImageSubData || !programs_prepare())
		return;
	if (halo_ml_glow_on())
	{
		threshold = ml.glow_threshold > 0.05f ? ml.glow_threshold : 0.05f;
		knee = threshold * 0.5f;
		intensity = ml.glow_intensity;
	}
	else
	{
		threshold = 1.0f;
		knee = 0.5f;
		intensity = 0.12f;
	}
	if (lab.glow_intensity > 0.0f)
		intensity = lab.glow_intensity;
	lab_gpu_begin();
	mark(_mark_bloom);
	if (ml.bloom_width != width || ml.bloom_height != height)
	{
		if (ml.bloom[0])
		{
			glDeleteTextures(BLOOM_LEVELS, ml.bloom);
			glDeleteFramebuffers(BLOOM_LEVELS, ml.bloom_framebuffers);
			glDeleteTextures(1, &ml.bloom_source);
		}
		ml.bloom_width = width;
		ml.bloom_height = height;
		for (level = 0; level < BLOOM_LEVELS; level++)
		{
			int w = width >> (level + 1), h = height >> (level + 1);

			ml.bloom[level] = texture_make(GL_RGBA16F, w > 1 ? w : 1, h > 1 ? h : 1);
			ml.bloom_framebuffers[level] = framebuffer_make(ml.bloom[level], 0);
		}
		ml.bloom_source = texture_make(GL_RGBA16F, width, height);
	}
	for (level = 0; level < BLOOM_LEVELS; level++)
	{
		sizes[level][0] = (width >> (level + 1)) > 1 ? width >> (level + 1) : 1;
		sizes[level][1] = (height >> (level + 1)) > 1 ? height >> (level + 1) : 1;
	}
	pass_state();
	/* (the view's own texture read where the combine draws elsewhere: a
	multisampled framebuffer, the resolved texture its copy. Where the
	combine draws into the texture itself, read with a texture barrier,
	each pixel its own; without one, a copy) */
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	if (ext.GetFramebufferAttachmentParameteriv)
		ext.GetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &attached);
	/* (not known: taken as the texture's own, the safe way) */
	feedback = !ext.GetFramebufferAttachmentParameteriv || (GLuint)attached == texture;
	source = feedback && !ext.TextureBarrier ? ml.bloom_source : texture;
	if (source == ml.bloom_source)
	{
		GLuint read = 0, draw = 0;

		glGenFramebuffers(1, &read);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
		glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
		glGenFramebuffers(1, &draw);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ml.bloom_source, 0);
		glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glDeleteFramebuffers(1, &read);
		glDeleteFramebuffers(1, &draw);
	}
	/* the light past the threshold, halved */
	mark(_mark_bloom_bright);
	glUseProgram(ml.programs[_program_bloom_bright]);
	uniform4(ml.programs[_program_bloom_bright], "params", threshold, knee, 0.0f, 0.0f);
	texture_bind(_unit_source, source, TRUE);
	whole[0] = whole[1] = 0;
	whole[2] = sizes[0][0];
	whole[3] = sizes[0][1];
	pass_draw(ml.bloom_framebuffers[0], whole);
	/* the sun's shafts, added to it */
	if (halo_ml_glow_on() && ml.glow_shafts && ml.sun_screen[2] > 0.0f && ml.depth_copy &&
		ml.width == width && ml.height == height)
	{
		GLuint program = ml.programs[_program_shafts];

		mark(_mark_bloom_shafts);
		glUseProgram(program);
		uniform4(program, "params", ml.sun_screen[0], ml.sun_screen[1], 2.5f * ml.sun_screen[2], 0.975f);
		texture_bind(_unit_source, source, TRUE);
		texture_bind(_unit_depth, ml.depth_copy, TRUE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		pass_draw(ml.bloom_framebuffers[0], whole);
		glDisable(GL_BLEND);
	}
	mark(_mark_bloom_down);
	glUseProgram(ml.programs[_program_bloom_down]);
	for (level = 1; level < BLOOM_LEVELS; level++)
	{
		texture_bind(_unit_source, ml.bloom[level - 1], TRUE);
		whole[2] = sizes[level][0];
		whole[3] = sizes[level][1];
		pass_draw(ml.bloom_framebuffers[level], whole);
	}
	mark(_mark_bloom_up);
	glUseProgram(ml.programs[_program_bloom_up]);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	for (level = BLOOM_LEVELS - 1; level > 0; level--)
	{
		texture_bind(_unit_source, ml.bloom[level], TRUE);
		whole[2] = sizes[level - 1][0];
		whole[3] = sizes[level - 1][1];
		pass_draw(ml.bloom_framebuffers[level - 1], whole);
	}
	glDisable(GL_BLEND);
	mark(_mark_bloom_combine);
	glUseProgram(ml.programs[_program_bloom_combine]);
	/* (the levels summed: each upsample adds the one below it) */
	uniform4(ml.programs[_program_bloom_combine], "params", 0.0f, 0.0f, intensity / (float)(BLOOM_LEVELS - 2), 0.0f);
	texture_bind(_unit_source, source, FALSE);
	texture_bind(_unit_bloom, ml.bloom[0], TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	if (feedback && source == texture)
		ext.TextureBarrier();
	pass_draw(framebuffer, corners);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	mark(_mark_bloom_end);
	lab_gpu_end();
	halo_ml_targets_drawn(FALSE);
}

/* display.glow without display.hdr: the bloom over the 8-bit view, from
d3d8_gl.c where the tone mapping would be */
void halo_ml_glow_sdr(short x0, short y0, short x1, short y1)
{
	struct halo_ml_targets targets;

	if (halo_ml_glow_on() && halo_ml_targets_get(x0, y0, x1, y1, TRUE, &targets))
	{
		halo_ml_bloom(targets.color_texture, targets.draw_framebuffer, targets.width, targets.height, targets.corners);
		halo_ml_targets_drawn(TRUE);
	}
	/* (the lab's trace: the view as it is shown, before the HUD) */
	if (lab.trace && halo_ml_targets_get(x0, y0, x1, y1, TRUE, &targets))
	{
		lab_trace_line(lab_probe(targets.color_texture, targets.corners));
		halo_ml_targets_drawn(FALSE);
	}
}

/* (on a map that draws its own post-processing, OpenSauce's, as SPV3's
levels: the glow steps aside, not a second bloom over the map's own) */
int halo_ml_glow_on(void)
{
	(void)halo_ml_ray_tracing_mode();
	if (opensauce_postprocess_drawn())
		return 0;
	return lab.glow >= 0 ? lab.glow > 0 : ml.glow;
}

#else

int halo_ml_ray_tracing_mode(void)
{
	return HALO_RT_OFF;
}

int halo_ml_ray_tracing_support(int now)
{
	(void)now;
	return 0;
}

const char *halo_ml_ray_tracing_reason(void)
{
	return "NOT ON THIS PLATFORM";
}

int halo_ml_ray_tracing_shown(void)
{
	return 0;
}

int halo_ml_glow_support(void)
{
	/* (no MODERN LIGHTING pages here: halo_port_features.h) */
	return -1;
}

int halo_ml_rt_quality(void)
{
	return HALO_RT_QUALITY_HIGH;
}

int halo_ml_ray_tracing_meshes(void)
{
	return 1;
}

int halo_ml_rt_texture(const void *key, const void *hardware_format, const unsigned int *palette)
{
	(void)key;
	(void)hardware_format;
	return -1;
}

void halo_ml_scene_ms(double ms)
{
	(void)ms;
}

double halo_ml_now_ms(void)
{
	return 0.0;
}

void halo_ml_opaque(const struct halo_ml_frame *original)
{
	(void)original;
}

void halo_ml_shine(const struct halo_ml_frame *frame)
{
	(void)frame;
}

void halo_ml_reflections(const struct halo_ml_frame *frame)
{
	(void)frame;
}

void halo_ml_bloom(unsigned int texture, unsigned int framebuffer, int width, int height, const int corners[4])
{
	(void)texture;
	(void)framebuffer;
	(void)width;
	(void)height;
	(void)corners;
}

int halo_ml_lab_hdr(int configured)
{
	return configured;
}

void halo_ml_glow_sdr(short x0, short y0, short x1, short y1)
{
	(void)x0;
	(void)y0;
	(void)x1;
	(void)y1;
}

int halo_ml_glow_on(void)
{
	return 0;
}

unsigned int halo_ml_lab_generation(void)
{
	return 0;
}

#endif
