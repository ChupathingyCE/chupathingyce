/*
MODERN_LIGHTING.H

Optional lighting passes over the 3D view of the OpenGL renderer (off by
default; bgfx has none of them yet), after the opaque surfaces are drawn and
before the shine, the reflections, the transparent surfaces and the fog
(source/render/render.c). Drawing only: nothing here is read by the game's
simulation.

- display.glow: a bloom over lights and self-lit surfaces (modern_lighting.c).
- display.ray_tracing (Vulkan beside OpenGL): the sun's shadows of
  the objects and ambient occlusion traced with VK_KHR_ray_query against the
  level's render geometry and the objects' render models as they are drawn
  (skinned on the GPU), alpha-tested surfaces tested by their texture's
  alpha (posix_modern_lighting_rt.c; the scene from port/linux/game/
  modern_lighting_scene.c). debug.ray_tracing_geometry "collision" traces
  round 1's collision BSP and collision models instead, for comparing.
  Turned on and off while the game runs: the Vulkan side path comes up and
  goes down with it.

Every type here is the same size in the game's units (rewritten for the
64-bit build, tools/lp64_build.py) and the host's (posix_*.c): ints, floats
and pointers only.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef MODERN_LIGHTING_H
#define MODERN_LIGHTING_H

enum
{
	HALO_RT_OFF = 0,
	HALO_RT_SHADOWS,
	HALO_RT_AO,
	HALO_RT_GI,
};

/* display.ray_tracing_quality: what the trace costs. high: every pixel, 4
rays toward the sun and 4 of occlusion (round 2's); medium: one pixel of
each 2x2 block a frame (a different one each frame, taken up to the view's
size by depth after the accumulation over frames), 2 and 2 rays; low: that,
1 and 1. auto: medium on an integrated GPU (an APU, whose memory is the
system's), high on others */
enum
{
	HALO_RT_QUALITY_AUTO = 0,
	HALO_RT_QUALITY_LOW,
	HALO_RT_QUALITY_MEDIUM,
	HALO_RT_QUALITY_HIGH,
};

/* what the game knows of the view, for one window */
struct halo_ml_frame
{
	/* clip = world_to_clip * (x, y, z, 1), rows first (D3D's clip space:
	depth 0 to 1, y up) */
	float world_to_clip[16];
	float camera[3];
	/* toward the sun, and its color; sun_valid 0 where the sky has none */
	float sun[3];
	float sun_color[3];
	int sun_valid;
	/* the camera's cluster sees a sky */
	int outdoor;
	int network_game;
	/* the window's 3D view, in the game's units of the screen */
	int bounds[4];
	/* the ray tracing's instance of the camera's own unit (and its
	weapon), whose collision model holds the camera: -1 none */
	int viewer_instance;
};

/* modern_lighting_scene.c: the lab's "objects" (vehicles and bipeds, by name and place) */
void modern_lighting_scene_list(char *text, int size);
/* the lab's "encounters" (the scenario's AI encounters, by name and where their first squad starts) */
void modern_lighting_scene_encounters(char *text, int size);

/* d3d8_gl.c: the back buffer's GL objects (modern_lighting.c) */
struct halo_ml_targets
{
	unsigned int color_texture, depth_texture;
	unsigned int read_framebuffer, draw_framebuffer;
	int width, height, samples, hdr;
	int corners[4];
};

int halo_ml_targets_get(short x0, short y0, short x1, short y1, int resolve_color, struct halo_ml_targets *out);
/* OpenGL draws (not bgfx), and bgfx does */
int halo_ml_renderer_ready(void);
int halo_ml_renderer_bgfx(void);
void halo_ml_targets_drawn(int drew);

/* modern_lighting.c */
/* display.ray_tracing as it is drawn (HALO_RT_*): off where it can't be */
int halo_ml_ray_tracing_mode(void);
/* display.ray_tracing on this machine: 1 it can run (the platform has the
Vulkan side path, the GPU ray queries, the renderer is OpenGL's), 0 not,
-1 not known yet (asked: known on a frame soon, or at once with now where
OpenGL is current) */
int halo_ml_ray_tracing_support(int now);
/* why display.ray_tracing is off here, in the menus' words ("NEEDS THE
OPENGL RENDERER", "VULKAN DIDN'T START"); NULL while it can run or it is
not known yet */
const char *halo_ml_ray_tracing_reason(void);
/* whether the menus show RAY TRACING and RT QUALITY: not where ray tracing
can't run at all (no Vulkan side path on the platform, no ray queries on
the GPU, no GL-Vulkan sharing in the driver); shown, UNAVAILABLE, while
bgfx draws (halo_ml_unavailable) */
int halo_ml_ray_tracing_shown(void);
/* whether the glow can be drawn: OpenGL 4.3's immutable textures and image
copies (glTexStorage2D, glCopyImageSubData), 1; 0 on an older OpenGL (a 3.3
driver), whose GLOW and GLOW STRENGTH rows say so in their help line
(halo_ml_setting_note: GLOW: NEEDS OPENGL 4.3); -1 not known yet (no
renderer) or bgfx's (halo_ml_unavailable says that) */
int halo_ml_glow_support(void);
/* whether the MODERN LIGHTING settings (ray tracing and glow) are
UNAVAILABLE now: bgfx draws, which has none of their passes yet. Their rows
show it and can't be changed; the setting keeps its value for OpenGL */
int halo_ml_unavailable(void);
/* whether key is one of the MODERN LIGHTING settings the menus show */
int halo_ml_setting_is_lighting(const char *key);
/* the menus' line for a MODERN LIGHTING setting that can't be on now
("RAY TRACING: NEEDS THE OPENGL RENDERER"), or NULL */
const char *halo_ml_setting_note(const char *key);
/* the menus' MODERN LIGHTING row's value (config.toml's, or the words
given: a menu's choices not saved yet) */
void halo_ml_settings_summary(char *text, int size);
void halo_ml_settings_summary_of(const char *ray_tracing, const char *glow, char *text, int size);
/* debug.ray_tracing_geometry: 1 the render meshes (the default), 0 the
collision models */
int halo_ml_ray_tracing_meshes(void);
/* display.ray_tracing_quality as in force (auto resolved: HALO_RT_QUALITY_LOW to _HIGH) */
int halo_ml_rt_quality(void);
/* an alpha-tested surface's texture for the trace (its bitmap's hardware
format, the texture cache's; palette: a P8 one's, the game's vector palette):
its number, -3 its alpha all passes (opaque),
-2 none (not readable: the surface left out) */
int halo_ml_rt_texture(const void *key, const void *hardware_format, const unsigned int *palette);
void halo_ml_opaque(const struct halo_ml_frame *frame);
/* the lab's CPU timings of the scene's gathering */
double halo_ml_now_ms(void);
void halo_ml_scene_ms(double ms);
/* the view before the level's shine and reflections (the dynamic lights',
a muzzle flash's, drawn already: no surface counts as shiny for a flash) */
void halo_ml_shine(const struct halo_ml_frame *frame);
void halo_ml_reflections(const struct halo_ml_frame *frame);
/* (from d3d8_gl.c's tone mapping, before its curve: the bloom) */
void halo_ml_bloom(unsigned int texture, unsigned int framebuffer, int width, int height, const int corners[4]);
/* the lab's display.hdr (configured: the setting's), and a number that
changes with the lab's settings */
int halo_ml_lab_hdr(int configured);
unsigned int halo_ml_lab_generation(void);
/* display.glow: on, and its bloom over an 8-bit view (no display.hdr) */
int halo_ml_glow_on(void);
void halo_ml_glow_sdr(short x0, short y0, short x1, short y1);

/* posix_modern_lighting_rt.c (the host's ABI) */
/* whether OpenGL's GPU has what the trace needs (made and taken down again) */
int ml_rt_probe(const unsigned char gl_device_uuid[16]);
/* why the last ml_rt_probe or ml_rt_start found no GPU for the trace */
enum
{
	ML_RT_WHY_NONE,
	/* no libvulkan.so.1, or no Vulkan 1.2 instance */
	ML_RT_WHY_NO_VULKAN,
	/* no Vulkan device is OpenGL's GPU (its UUID) */
	ML_RT_WHY_OTHER_GPU,
	/* OpenGL's GPU's Vulkan device lacks ray queries (or an extension) */
	ML_RT_WHY_NO_RAY_QUERIES,
};
extern int ml_rt_why;
int ml_rt_start(const unsigned char gl_device_uuid[16]);
/* all of it down (the setting off): ml_rt_start brings it up again */
void ml_rt_stop(void);
int ml_rt_ready(void);
/* the Vulkan device is an integrated GPU (display.ray_tracing_quality auto) */
int ml_rt_integrated(void);
/* whether this build has the Vulkan side path (64-bit Linux) */
int ml_rt_platform(void);
/* the world the scene's keys (tag pointers) are of: a new one (another map)
drops the scene made of the last */
void ml_rt_world(unsigned int world);
/* the level's render geometry: each material's vertices (as the map keeps
them; the position the first 12 bytes of each) and triangles; texture: its
alpha-tested texture (-1 opaque), scale: its texture coordinate's */
int ml_rt_level_begin(const void *key);
void ml_rt_level_material(const void *vertices, int vertex_count, int vertex_stride, const unsigned short *triangles,
	int triangle_count, int texture, const float scale[2]);
int ml_rt_level_end(void);
/* an alpha-tested texture by key: its number, -1 not made yet, -2 none,
-3 its alpha all passes (traced as opaque) */
int ml_rt_texture_find(const void *key);
int ml_rt_texture_add(const void *key, const unsigned char *rgba, int width, int height);
/* a model part (kind 1 compressed, 2 uncompressed vertices; indices a strip
or a list), copied once by key: its number, -2 none */
int ml_rt_part(const void *key, const void *vertices, int vertex_count, int kind, const unsigned short *indices,
	int index_count, int strip);
struct ml_rt_part_use
{
	int part;
	int texture;
	float scale[2];
};
/* an object this frame (slot: its absolute index, key: its datum index), the
parts it draws and its node matrices (3 rows of 4 each, as rendered: each
node's matrix times its default inverse) */
void ml_rt_object(int slot, int key, const struct ml_rt_part_use *parts, int part_count, const float *matrices,
	int node_count, unsigned int custom, unsigned int mask);
/* the level's triangles (world space), made again when the BSP changes */
int ml_rt_static_mesh(const void *key, const float *positions, int vertex_count, const unsigned int *indices,
	int triangle_count);
/* a mesh in its own space, kept by key (a collision BSP); its number, or -1 */
int ml_rt_mesh_find(const void *key);
int ml_rt_mesh_add(const void *key, const float *positions, int vertex_count, const unsigned int *indices,
	int triangle_count);
void ml_rt_instances_begin(void);
/* transform: 3 rows of 4 (world = rows * (x, y, z, 1)); custom: the
instance's number for the shader; mask: HALO_RT_MASK_* */
void ml_rt_instance(int mesh, const float transform[12], unsigned int custom, unsigned int mask);

#define HALO_RT_MASK_LEVEL 0x01
#define HALO_RT_MASK_OBJECT 0x02
#define HALO_RT_MASK_UNIT 0x04 /* a player's or an AI's unit, a vehicle, a projectile (the fairness cap) */
#define HALO_RT_MASK_SHADOWLESS 0x08 /* an object that casts no shadow */

/* the shared images (made by Vulkan, used by OpenGL through
GL_EXT_memory_object_fd): what OpenGL writes for the tracing (each pixel's
position from the camera and its normal) and what the tracing writes back
(the lighting, and the reflected rays' hits); 0 until made */
struct ml_rt_shared
{
	int width, height;
	int memory_fd[4];
	int memory_size[4];
	int semaphore_fd[2]; /* 0: OpenGL is done, 1: Vulkan is done */
	int generation;
};
int ml_rt_resize(int width, int height, struct ml_rt_shared *shared);
/* the trace, between OpenGL's semaphore 0 and its wait on semaphore 1 */
struct ml_rt_trace
{
	float camera[3];
	float sun[3];
	int mode;
	int frame;
	int viewer_instance;
	int corners[4];
	float ao_radius;
	float sun_cone;
	int shadow_rays;
	int debug;
	/* the occlusion's rays a pixel; 1 the low-discrepancy noise (else white) */
	int ao_rays;
	int blue_noise;
	/* 1 every pixel, 2 one of each 2x2 block (the pixel offset[] of it), its
	result at the block's place in the images' low-resolution corner */
	int scale;
	int offset[2];
	/* the lab's: 1 no reflected rays, 2 no occlusion rays, 4 no sun rays */
	int flags;
	/* the distances (from the camera) over which the occlusion fades out:
	none traced past the second */
	float ao_fade[2];
};
int ml_rt_trace(const struct ml_rt_trace *trace);
/* the last traces' GPU time in milliseconds, -1 if not known */
float ml_rt_gpu_ms(void);
/* the last traces' GPU time of the builds (skinning, bottom- and top-level
structures) and of the trace itself, in milliseconds */
void ml_rt_gpu_phases(float *build_ms, float *trace_ms);
void ml_rt_say(char *line, int size);
/* the work since the last call (then counted again from zero): for the lab's
frame-by-frame record, what a slow frame did */
struct ml_rt_frame_stats
{
	float fence_ms; /* the CPU's wait for a frame slot */
	float stall_ms; /* the CPU's waits for one-time submits and idles, new memory */
	int built, refit, skinned, instances;
	int new_slots, new_parts, new_textures, new_chunks;
};
void ml_rt_frame_stats_take(struct ml_rt_frame_stats *stats);

#endif
