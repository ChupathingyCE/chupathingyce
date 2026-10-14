/*
D3D8_DEVICE.C

The Xbox Direct3D 8 device: the front end of the renderer. It keeps
everything with Xbox or D3D meaning and makes every GPU call through gpu.h
to a backend (gpu_gl.c: OpenGL 4.5 core, or OpenGL ES 3 on iOS).

The game drives the device through the XDK's inline functions, which keep
the "simple" render states in D3D__RenderState and call into this file for
everything else. At each draw the full state is read back from there and
translated: the vertex program into GLSL once per shader (nv2a_vsh.c), the
pixel shader - texture stages and register combiners, 57 render states -
into GLSL once per combination (nv2a_psh.c), and the rest into a gpu_draw
packet the backend applies.

Conventions carried over from the Xbox:
- Clip space is D3D's (depth 0..1, y down in window space). The backend
  makes the GPU agree (glClipControl on desktop GL, the vertex shader's
  epilogue on ES), so viewports, scissors and texture rows line up with
  D3D's top-left origin; the present blit flips the image back for display.
- Render targets and textures are identified by the physical address in
  their Data field. A texture whose data is a render target samples the
  backend's render target directly (render-to-texture).
- Vertex data is read from guest memory at draw time.
*/

#include "xgpu.h"
#include "upload_bounds.h"
#include "sdl_platform.h"
#include "halo_ui_pointer.h"
#include "port_config.h"
#include "halo_display.h"
#include "metal_overlay.h"
#include "halo_stereo.h"
#include "halo_stereo_window.h"
#include "halo_stereo_cutscene.h"
#include "posix.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void d3d8_surface_initialize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);
void d3d8_surface_resize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);
/* port/linux/game/render_interpolation.c */
void render_interpolation_next_frame_due(unsigned long microseconds);
/* input_replay.c */
void input_replay_frame(long width, long height);
/* source/models/models.c: models drawn at each detail level (0 the lowest, 4
the highest) and models the size cull dropped since the last call */
void halo_model_counts_take(unsigned long drawn[5], unsigned long *culled);

struct gpu_capabilities device_capabilities;

/* display.frame_pacing as gpu_initialize flags */
static uint32_t frame_pacing_flags(void)
{
	const char *pacing = config_string("display.frame_pacing");

	if (!strcmp(pacing, "refresh"))
		return GPU_INITIALIZE_PACING_ANY_RATE;
	if (!strcmp(pacing, "tick"))
		return 0;
	if (strcmp(pacing, "off"))
		platform_log("display.frame_pacing: unknown value \"%s\"; using \"off\"", pacing);
	return GPU_INITIALIZE_PACING_OFF;
}

/* ---------- the screen's width

The Xbox screen is 640x480. The native ports can draw a wider one: 480
lines, and as many columns as the display's shape gives. On iOS that is
display.screen_width (port_config.c; 640 keeps 4:3); on the desktop, the
shape of the window (of the display while the game is fullscreen, or of
display.resolution), and 640 where display.resolution_scaling is "original".
The game's camera derives its horizontal field of view from the viewport, so
the 3D view simply widens. The menus and full-screen overlays are laid out
for 640 columns; while they draw (halo_screen_ui_offset), everything shifts
right to center them.

The native ports also draw at the display's resolution: render
targets the size of the screen get that many pixels (screen_scale), and
viewports, clears and visibility counts are scaled to match, so the game
still works in its 480 lines. iOS display.render_height can reduce the render
size; its default of zero uses physical native pixels. The width and scale change only between
frames, after one is presented (halo_screen_commit). */

#define SCREEN_HEIGHT 480
#define SCREEN_MAXIMUM_WIDTH 1920

/* the width the game draws, 0 until first asked, and how many pixels a
render target the size of the screen has per unit of it */
static long screen_width;
static float screen_scale[2] = { 1.0f, 1.0f };
/* head-tracked stereo: the screen's scale for frames and layers without the
Compositor's eyes (menus, loads, the mono layer), the last one a frame
without them took: the theater picture's, which the mono presenter puts on
the screen; 0 until one is known */
static float screen_scale_mono[2];
static long ui_offset;
static int32_t screen_maximum_texture_size = 8192;
#define UI_OFFSET ((int32_t)ui_offset)

/* debug.render_scale_dpad: the render scale changed while the game runs
(halo_render_scale_step), in place of display.render_scale; negative until
first changed */
static double render_scale_live = -1.0;

/* source/render/render_debug.c: text the frame's debug pass draws */
void render_debug_string(unsigned char immediate, const char *string);
/* xbox_kernel.c */
double halo_frame_trace_milliseconds(void);
/* when the render scale last changed, for its readout */
static double render_scale_changed_milliseconds = -1.0;

/* the render scale on screen for two seconds after it changes; called as
each frame begins (render_interpolation.c), so the frame's debug pass draws
it (the console's lines show only while the console is open) */
void halo_render_scale_overlay(void)
{
	char text[64];

	if (render_scale_changed_milliseconds < 0.0 ||
		halo_frame_trace_milliseconds() - render_scale_changed_milliseconds > 2000.0)
		return;
	snprintf(text, sizeof(text), "render scale %.2f", render_scale_live);
	render_debug_string(0, text);
}

/* one step (0.05) up or down of the render scale, 0.25 to 1.0; the next
frame's halo_screen_commit draws at it (xinput_sdl.c: hold Back and press the
D-pad left or right) */
void halo_render_scale_step(int direction)
{
	double scale = render_scale_live >= 0.0 ? render_scale_live : config_real("display.render_scale");

	if (scale <= 0.0 || scale > 1.0)
		scale = 1.0;
	scale = floor(scale * 20.0 + 0.5) / 20.0 + (direction > 0 ? 0.05 : -0.05);
	scale = scale < 0.25 ? 0.25 : scale > 1.0 ? 1.0 : scale;
	render_scale_live = scale;
	render_scale_changed_milliseconds = halo_frame_trace_milliseconds();
	platform_log("render scale %.2f", scale);
}

static int head_eyes_frame(void);

static void screen_mode_choose(long *width, float scale[2])
{
#ifdef HALO_ILP32
	/* display.screen_width, or 0 for the display's shape, which the app
	passes (port/ios/host/host_main.m) */
	const char *display = getenv("HALO_DISPLAY_WIDTH");
	const char *pixels_x = getenv("HALO_DISPLAY_PIXEL_WIDTH");
	const char *pixels_y = getenv("HALO_DISPLAY_PIXEL_HEIGHT");
	long requested_width = config_integer("display.screen_width");
	int drawable_width = 0, drawable_height = 0;
	struct halo_display_size pixels;

	*width = requested_width;
	if (*width <= 0)
		*width = display ? atol(display) : 640;
	if (*width < 640)
		*width = 640;
	if (*width > 1600)
		*width = 1600;
	*width &= ~1L;
	/* The drawable is authoritative once the UIKit window exists. Before
	   then the native host supplies the display's physical pixel dimensions. */
	platform_video_drawable_size(&drawable_width, &drawable_height);
	if (drawable_width <= 0 || drawable_height <= 0) {
		drawable_width = pixels_x ? atoi(pixels_x) : (int)*width;
		drawable_height = pixels_y ? atoi(pixels_y) : SCREEN_HEIGHT;
	}
	{
		/* display.render_scale: a fraction of the display's pixels each way
		(display.upscaler scales the picture back up). Not for foveated eyes:
		the Compositor's rate map sets their size, at the render quality, which
		takes the scale's place */
		double render_scale = render_scale_live >= 0.0 ? render_scale_live : config_real("display.render_scale");

		if (render_scale >= 0.25 && render_scale < 1.0 && !(head_eyes_frame() && halo_stereo_frame()->foveated))
		{
			drawable_width = (int)lround(drawable_width * render_scale);
			drawable_height = (int)lround(drawable_height * render_scale);
		}
	}
	pixels = halo_display_render_size(drawable_width, drawable_height, *width,
		requested_width, config_integer("display.render_height"), screen_maximum_texture_size);
	{
		/* head-tracked stereo: the screen's targets are the eyes' pictures,
		each view's size and shape times display.render_scale, which needn't
		be the screen's shape (the presenter fills each view with its eye's
		picture and draws the HUD at the screen's shape): their pixels aren't
		square. The display's shape is kept to 4:3 and wider, which would
		squeeze a view's height. Only in a frame with the Compositor's eyes;
		the drawable is theirs then (host_stereo_picture_size) */
		if (head_eyes_frame())
		{
			long render_height = config_integer("display.render_height");

			pixels.width = drawable_width;
			pixels.height = drawable_height;
			if (render_height > 0 && pixels.height > render_height)
			{
				pixels.width = pixels.width * render_height / pixels.height;
				pixels.height = render_height;
			}
			if (pixels.width > screen_maximum_texture_size)
				pixels.width = screen_maximum_texture_size;
			if (pixels.height > screen_maximum_texture_size)
				pixels.height = screen_maximum_texture_size;
		}
	}
	scale[0] = (float)pixels.width / (float)*width;
	scale[1] = (float)pixels.height / (float)SCREEN_HEIGHT;
#else
	long display_width, display_height;

	*width = 640;
	scale[0] = scale[1] = 1.0f;
	if (platform_screen_mode(&display_width, &display_height) && display_width > 0 && display_height > 0)
	{
		long wanted = (SCREEN_HEIGHT * display_width + display_height / 2) / display_height;

		*width = wanted < 640 ? 640 : wanted > SCREEN_MAXIMUM_WIDTH ? SCREEN_MAXIMUM_WIDTH : wanted & ~1L;
		scale[0] = (float)display_width / (float)*width;
		scale[1] = (float)display_height / (float)SCREEN_HEIGHT;
		/* a display narrower or wider than the game can be: the picture
		keeps its shape and the display blit letterboxes it */
		if (*width != wanted && *width != (wanted & ~1L))
			scale[0] = scale[1] = scale[0] < scale[1] ? scale[0] : scale[1];
	}
#endif
}

/* a head-tracked frame with the Compositor's eyes, whose screen-sized
targets are the eyes' pictures (screen_mode_choose) */
static int head_eyes_frame(void)
{
	const struct halo_stereo_frame *stereo = halo_stereo_frame();

	return stereo->mode == HALO_STEREO_HEAD && stereo->eye_count == 2 && stereo->eye_width > 0 &&
		stereo->eye_height > 0;
}

static int foveated_eye_allocation(unsigned long *width, unsigned long *height);
static int rate_map_test(void);

/* the scale just taken, remembered for mono frames if this frame has no eyes */
static void screen_scale_taken(void)
{
	if (!head_eyes_frame())
	{
		screen_scale_mono[0] = screen_scale[0];
		screen_scale_mono[1] = screen_scale[1];
	}
}

long halo_screen_width(void)
{
	if (!screen_width)
	{
		screen_mode_choose(&screen_width, screen_scale);
		screen_scale_taken();
		platform_log("screen: %ldx%d drawn at %.0fx%.0f", screen_width, SCREEN_HEIGHT,
			screen_width * screen_scale[0], SCREEN_HEIGHT * screen_scale[1]);
	}
	return screen_width;
}

/* the display's pixels for each of the 480 lines (text_hires.c) */
float halo_screen_pixel_scale(void)
{
	halo_screen_width();
	return screen_scale[1];
}

void halo_screen_ui_offset(unsigned char centered)
{
	ui_offset = centered ? (halo_screen_width() - 640) / 2 : 0;
}

/* render_window calls this once for each view it draws (each player's, and
the mirror's, whose rasterizer_target says which), after the world, its fog
and lens flares, and before interface_draw_screen draws the HUD: a
post-process pass over a player's view (anti-aliasing for the upscaler's
input, an upscale beneath the HUD) belongs here, so that the HUD, the menus
and other views stay as drawn. viewport_bounds is the view's rectangle in the
game's units. Nothing uses it yet. pfista/halo-og runs its FXAA from the same
spot. */
void halo_render_before_hud(short local_player_index, short rasterizer_target,
	union rectangle2d const *viewport_bounds)
{
	(void)local_player_index;
	(void)rasterizer_target;
	(void)viewport_bounds;
}

float halo_screen_scale(void)
{
	halo_screen_width();
	return screen_scale[0] > screen_scale[1] ? screen_scale[0] : screen_scale[1];
}

long halo_shadow_map_scale(void)
{
	long resolution = config_integer("display.shadow_resolution"), scale = 1;
	while (scale < 8 && 128 * scale * 2 <= resolution)
		scale *= 2;
	return scale;
}

/* ---------- state the XDK header's inline functions read and write */

DWORD D3D__RenderState[D3DRS_MAX];
DWORD D3D__TextureState[D3DTSS_MAXSTAGES][D3DTSS_MAX];
WORD *D3D__IndexData;
BYTE D3D__StateBlockDirty[1024];

/* ---------- vertex shaders */

#define VERTEX_SHADER_SIGNATURE 0x76736864UL /* 'vshd' */
#define VERTEX_PROGRAM_SLOTS 136

struct vertex_element
{
	unsigned char reg;
	unsigned char stream;
	unsigned char type;
	unsigned char bytes;
	unsigned short offset;
};

struct vertex_shader_object
{
	unsigned long signature;
	unsigned long id;
	DWORD *instructions;
	unsigned long instruction_count;
	struct vertex_element elements[XGPU_VERTEX_ATTRIBUTE_COUNT];
	unsigned long element_count;
	unsigned long packed_mask;
	/* [0] streams per the declaration, [1] immediate mode (all floats) */
	gpu_shader shader[2];
	/* the packed-attribute mask each was translated with */
	unsigned long shader_packed_mask[2];
	/* debug.gpu_dump_shaders, for each compiled at map load: its source,
	dumped at its first draw (shader_dump_pending) */
	char *dump_source[2];
	/* every object, newest first, and its instructions' hash: its name in
	the shader lists (shader_list_warm) */
	struct vertex_shader_object *next_object;
	uint64_t program_hash;
};

static struct vertex_shader_object *vertex_shader_objects;

/* FNV-1a, 64 bits: the shader lists' names for programs and pixel shader keys */
static uint64_t hash64(const void *data, unsigned long size)
{
	const unsigned char *bytes = data;
	uint64_t hash = 14695981039346656037ULL;

	while (size--)
		hash = (hash ^ *bytes++) * 1099511628211ULL;
	return hash;
}

/* ---------- programs */

struct fragment_entry
{
	struct fragment_entry *next;
	unsigned long hash;
	struct nv2a_pixel_shader_key key;
	gpu_shader shader;
	/* debug.gpu_dump_shaders, for a shader compiled at map load: its source,
	dumped at its first draw (shader_dump_pending) */
	char *dump_source;
};

#define FRAGMENT_BUCKETS 1024

static struct fragment_entry *fragment_buckets[FRAGMENT_BUCKETS];

/* ---------- render targets */

struct render_target_entry
{
	struct render_target_entry *next;
	/* the next with the same address bucket (render_target_bucket) */
	struct render_target_entry *next_in_bucket;
	struct xgpu_render_target target;
	unsigned long last_rendered;
	/* the stereo layer a screen-sized target is for (halo_stereo.h), else
	HALO_STEREO_LAYER_MONO */
	int layer;
};

/* every draw looks up its targets and whether its textures are render
targets, of which there are dozens */
#define RENDER_TARGET_BUCKET_COUNT 256

static struct render_target_entry *render_target_buckets[RENDER_TARGET_BUCKET_COUNT];

static struct render_target_entry **render_target_bucket(unsigned long data)
{
	return &render_target_buckets[((data >> 12) ^ (data >> 20)) % RENDER_TARGET_BUCKET_COUNT];
}


static struct render_target_entry *render_targets;

/* ---------- the device */

#ifdef HALO_ILP32
#define VISIBILITY_ALL_SAMPLES 1000000
#endif

struct gl_device
{
	D3DPRESENT_PARAMETERS presentation;
	D3DSurface back_buffer;
	D3DSurface depth_buffer;
	D3DSurface *render_target;
	D3DSurface *depth_stencil;
	D3DVIEWPORT8 viewport;
	D3DMATRIX transforms[D3DTS_MAX];
	D3DBaseTexture *textures[D3DTSS_MAXSTAGES];
	D3DPalette *palettes[D3DTSS_MAXSTAGES];
	D3DSHADERCONSTANTMODE shader_constant_mode;

	struct vertex_shader_object *vertex_shader;
	struct vertex_shader_object *program_slots[VERTEX_PROGRAM_SLOTS];
	unsigned long program_address;
	float viewport_scale[4];
	float viewport_offset[4];

	struct
	{
		DWORD data;
		UINT stride;
	} streams[16];
	/* SetIndices' base vertex: added to every index of an indexed draw (the
	dynamic vertex buffers keep each buffer's vertices at an offset into one
	vertex buffer, and their triangles count from 0: contrails, lightning) */
	UINT base_vertex_index;

	/* the current value of each input register (SetVertexData) */
	float attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
	BOOL immediate_active;
	D3DPRIMITIVETYPE immediate_type;
	float *immediate_vertices;
	unsigned long immediate_count;
	unsigned long immediate_capacity;

	BOOL query_pending[GPU_VISIBILITY_SLOTS];
	/* the pixels each of the game's pixels covered in the test's target
	(render_target_get), which its count is divided by */
	float query_area[GPU_VISIBILITY_SLOTS];
	BOOL visibility_test_active;

	unsigned long frame;
	unsigned long next_vertex_shader_id;
	BOOL gl_ready;
	BOOL created;
};

static struct gl_device device;

/* debug.gpu_stats prints these once a second */
static struct
{
	unsigned long draws, immediate_draws, clears, presents;
	unsigned long skipped_no_program, skipped_no_target, skipped_link;
	unsigned long target_changes;
	/* vertex and index bytes drawn from the mirror, and streamed */
	unsigned long mirrored_bytes, streamed_bytes;
} stats;

static D3DDevice *device_pointer(void)
{
	return (D3DDevice *)&device;
}

static float dword_to_float(DWORD value)
{
	union { DWORD d; float f; } u;

	u.d = value;
	return u.f;
}

static void color_to_vec4(D3DCOLOR color, float *out)
{
	out[0] = ((color >> 16) & 0xff) / 255.0f;
	out[1] = ((color >> 8) & 0xff) / 255.0f;
	out[2] = (color & 0xff) / 255.0f;
	out[3] = ((color >> 24) & 0xff) / 255.0f;
}

/* ---------- debugging settings, read once (gl_initialize) */

static struct
{
	/* debug.gpu_skip_vertex_shaders "<id>,<id>..." drops draws by vertex
	shader, for finding which pass produces something (port_config.c) */
	const char *skip_vertex_shaders;
	const char *dump_shaders;
	const char *shader_replay;
	BOOL statistics;
} debug_settings;

/* ---------- vertical blank emulation */

#define VERTICAL_BLANK_NANOSECONDS (1000000000L / 60)

static pthread_mutex_t vertical_blank_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t vertical_blank_condition = PTHREAD_COND_INITIALIZER;
static D3DCALLBACK vertical_blank_callback;
static unsigned long vertical_blank_count;
static volatile unsigned int flip_count;
static unsigned long pending_flips;
static BOOL vertical_blank_thread_started = FALSE;

static void *vertical_blank_thread(void *unused)
{
	struct timespec next;

	(void)unused;
	clock_gettime(CLOCK_MONOTONIC, &next);
	for (;;)
	{
		D3DCALLBACK callback;

		next.tv_nsec += VERTICAL_BLANK_NANOSECONDS;
		if (next.tv_nsec >= 1000000000L)
		{
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		platform_sleep_until(&next);

		pthread_mutex_lock(&vertical_blank_lock);
		vertical_blank_count++;
		/* a presented frame becomes visible at the next vertical blank */
		if (pending_flips)
		{
			pending_flips--;
			flip_count++;
		}
		callback = vertical_blank_callback;
		pthread_cond_broadcast(&vertical_blank_condition);
		pthread_mutex_unlock(&vertical_blank_lock);

		if (callback)
			callback(0);
	}
	return NULL;
}

static void vertical_blank_start(void)
{
	pthread_mutex_lock(&vertical_blank_lock);
	if (!vertical_blank_thread_started)
	{
		pthread_t thread;

		if (pthread_create(&thread, NULL, vertical_blank_thread, NULL) == 0)
		{
			pthread_detach(thread);
			vertical_blank_thread_started = TRUE;
		}
		else
		{
			platform_log("cannot start the vertical blank thread");
		}
	}
	pthread_mutex_unlock(&vertical_blank_lock);
}

/* replaces main/d3d_intimacy.cpp, which reads the counter out of the Xbox
Direct3D runtime's private device structure */
volatile unsigned int *d3d_find_flipcount(void)
{
	return &flip_count;
}

void WINAPI D3DDevice_SetVerticalBlankCallback(D3DCALLBACK callback)
{
	pthread_mutex_lock(&vertical_blank_lock);
	vertical_blank_callback = callback;
	pthread_mutex_unlock(&vertical_blank_lock);
	vertical_blank_start();
}

void WINAPI D3DDevice_BlockUntilVerticalBlank(void)
{
	unsigned long count;

	vertical_blank_start();
	pthread_mutex_lock(&vertical_blank_lock);
	count = vertical_blank_count;
	while (vertical_blank_count == count)
		pthread_cond_wait(&vertical_blank_condition, &vertical_blank_lock);
	pthread_mutex_unlock(&vertical_blank_lock);
}

/* ---------- render targets */

static DWORD surface_dimensions(const D3DSurface *surface, unsigned long *width, unsigned long *height, BOOL *depth)
{
	struct xgpu_texture_description description;
	DWORD format;

	xgpu_texture_describe(surface->Format, surface->Size, &description);
	*width = description.width;
	*height = description.height;
	format = description.format;
	*depth = format == D3DFMT_D24S8 || format == D3DFMT_F24S8 || format == D3DFMT_D16 || format == D3DFMT_F16 ||
		format == D3DFMT_LIN_D24S8 || format == D3DFMT_LIN_F24S8 || format == D3DFMT_LIN_D16 || format == D3DFMT_LIN_F16;
	return format;
}

/* the screen's scale, or with foveated eye passes the eyes' allocated size
over their screen size times it: the density the eyes would have unfoveated,
for the targets that aren't foveated but follow the eyes' size (the HUD
layer, the HUD groups' and the reticle's targets, the zoomed picture and
the effect targets), so that they cost what they did before foveation
rather than growing with the maps' screen size */
static void screen_scale_dense(float scale[2])
{
	const struct halo_stereo_frame *stereo = halo_stereo_frame();
	unsigned long width, height;

	scale[0] = screen_scale[0];
	scale[1] = screen_scale[1];
	if (foveated_eye_allocation(&width, &height))
	{
		scale[0] *= (float)width / (float)stereo->eye_width;
		scale[1] *= (float)height / (float)stereo->eye_height;
	}
}

/* the Xbox-sized offscreen targets drawn larger than the Xbox drew them:
the 128x128 R5G6B5 shadow maps (and their blur) at display.shadow_map_size,
and with display.effect_resolution the 320x240 active-camouflage source (and
its depth) at the screen's scale. Their users address them in normalized or
logical coordinates, so only the pixel count changes. */
static void offscreen_target_scale(unsigned long width, unsigned long height, DWORD format, float scale[2])
{
	static long shadow_size = -1;
	static int effect_resolution = -1;
	static float effect_factor = 1.0f;

	if (shadow_size < 0)
	{
		shadow_size = 128 * halo_shadow_map_scale();
		shadow_size = shadow_size < 128 ? 128 : shadow_size > 2048 ? 2048 : shadow_size;
		effect_resolution = config_boolean("display.effect_resolution");
		/* display.mirror_resolution = "full": the 320x240 target at the
		screen's full resolution, twice its Xbox proportion each way */
		effect_factor = !strcmp(config_string("display.mirror_resolution"), "full") ? 2.0f : 1.0f;
	}
	if (width == 128 && height == 128 && (format == D3DFMT_R5G6B5 || format == D3DFMT_LIN_R5G6B5))
		scale[0] = scale[1] = (float)shadow_size / 128.0f;
	else if (width == 320 && height == 240 && effect_resolution)
	{
		float dense[2];

		/* (foveated eye passes: at the eyes' density without foveation, not
		the rate maps' screen size, which the full-rate center reaches only
		there) */
		screen_scale_dense(dense);
		scale[0] = dense[1] * effect_factor;
		scale[1] = dense[1] * effect_factor;
	}
}

/* the layer a screen-sized target is drawn for now: in a stereo frame each
eye, the HUD and the zoomed picture have their own textures (the one layer
key: a new layer is a change to halo_stereo.h), else the one the game
always had */
static int render_target_layer(void)
{
	return halo_stereo_frame()->eye_count == 2 ? halo_stereo_current_layer() : HALO_STEREO_LAYER_MONO;
}

static struct render_target_entry *render_target_get_layer(const D3DSurface *surface, int stereo_layer)
{
	struct render_target_entry *entry;
	unsigned long width, height;
	BOOL depth;
	DWORD format;
	int layer = HALO_STEREO_LAYER_MONO;
	/* a foveated eye's target: its eye (1 or 2) and allocated size */
	unsigned char foveated_eye = 0;
	unsigned long allocated_width = 0, allocated_height = 0;

	if (!surface || !surface->Data)
		return NULL;
	float scale[2] = { 1.0f, 1.0f };

	format = surface_dimensions(surface, &width, &height, &depth);
	/* the screen's targets are drawn at the screen's scale */
	if (width == (unsigned long)halo_screen_width() && height == SCREEN_HEIGHT)
	{
		scale[0] = screen_scale[0];
		scale[1] = screen_scale[1];
		layer = stereo_layer;
		/* head-tracked stereo: the mono layer (a frame without eyes, which
		presents mono on the theater screen) keeps the theater picture's size
		and shape; the eyes and the HUD take the views' */
		if (layer == HALO_STEREO_LAYER_MONO && screen_scale_mono[0] > 0.0f && head_eyes_frame())
		{
			scale[0] = screen_scale_mono[0];
			scale[1] = screen_scale_mono[1];
		}
		/* the zoomed picture: the whole view at the eyes' density without
		foveation, unfoveated (halo_stereo_zoom_density), in steps of 1/256 of
		an eye's so that a tangent's rounding doesn't make a new target */
		if (layer == HALO_STEREO_LAYER_ZOOM)
		{
			float dense[2], density[2];

			screen_scale_dense(dense);
			halo_stereo_zoom_density(density);
			scale[0] = dense[0] * roundf(density[0] * 256.0f) / 256.0f;
			scale[1] = dense[1] * roundf(density[1] * 256.0f) / 256.0f;
		}
		/* the reticle's layer and the HUD groups' targets are color only:
		their draws share the HUD layer's depth and stencil, as mono's HUD
		shares one */
		if (depth && (layer == HALO_STEREO_LAYER_RETICLE || layer == HALO_STEREO_LAYER_UI ||
			layer >= HALO_STEREO_LAYER_HUD_GROUP))
			layer = HALO_STEREO_LAYER_HUD;
		/* foveated eye passes: an eye's targets are allocated at the
		drawable's size per view and drawn through its rate map, at the
		screen's scale (the map's screen size); the HUD's keep the eyes'
		density without foveation (screen_scale_dense): the presenter reads
		them through the drawable's map anyway, and more pixels gain nothing */
		if ((layer == 0 || layer == 1) && foveated_eye_allocation(&allocated_width, &allocated_height))
			foveated_eye = (unsigned char)(layer + 1);
		/* debug.rate_map_test (the Mac's Metal backend): the mono screen's
		targets through a synthetic map the size of the screen, which the
		backend makes (gpu_metal.m) */
		else if (layer == HALO_STEREO_LAYER_MONO && rate_map_test())
		{
			foveated_eye = 1;
			allocated_width = (unsigned long)(width * scale[0] + 0.5f);
			allocated_height = (unsigned long)(height * scale[1] + 0.5f);
		}
		else if (layer == HALO_STEREO_LAYER_HUD || layer == HALO_STEREO_LAYER_RETICLE ||
			layer == HALO_STEREO_LAYER_UI || layer >= HALO_STEREO_LAYER_HUD_GROUP)
		{
			/* display.hud_resolution: fewer of them, every HUD target alike,
			so the color targets keep the size of the depth they share */
			screen_scale_dense(scale);
			scale[0] *= halo_stereo_hud_resolution();
			scale[1] *= halo_stereo_hud_resolution();
		}
	}
	else
		offscreen_target_scale(width, height, format, scale);
	for (entry = *render_target_bucket(surface->Data); entry; entry = entry->next_in_bucket)
	{
		if (entry->target.data != surface->Data || entry->target.width != width ||
			entry->target.height != height || entry->target.depth != depth || entry->layer != layer ||
			entry->target.foveated_eye != foveated_eye)
			continue;
		/* a foveated eye's target is keyed on its allocation, not its scale:
		the eased render quality changes the map's sizes from frame to frame,
		inside the same allocation */
		if (foveated_eye && entry->target.allocated_width == allocated_width &&
			entry->target.allocated_height == allocated_height)
		{
			entry->target.scale[0] = scale[0];
			entry->target.scale[1] = scale[1];
			entry->target.gl_width = (unsigned long)(width * scale[0] + 0.5f);
			entry->target.gl_height = (unsigned long)(height * scale[1] + 0.5f);
			return entry;
		}
		if (!foveated_eye && entry->target.scale[0] == scale[0] && entry->target.scale[1] == scale[1])
			return entry;
	}
	entry = calloc(1, sizeof(*entry));
	entry->layer = layer;
	entry->target.data = surface->Data;
	entry->target.width = width;
	entry->target.height = height;
	entry->target.depth = depth;
	entry->target.scale[0] = scale[0];
	entry->target.scale[1] = scale[1];
	entry->target.gl_width = (unsigned long)(width * scale[0] + 0.5f);
	entry->target.gl_height = (unsigned long)(height * scale[1] + 0.5f);
	entry->target.foveated_eye = foveated_eye;
	entry->target.allocated_width = foveated_eye ? allocated_width : entry->target.gl_width;
	entry->target.allocated_height = foveated_eye ? allocated_height : entry->target.gl_height;
	{
		struct gpu_texture_description texture = { 0 };

		texture.type = GPU_TEXTURE_2D;
		texture.format = depth ? GPU_FORMAT_DEPTH_STENCIL : GPU_FORMAT_BGRA8;
		texture.usage = GPU_USAGE_RENDER_TARGET;
		texture.foveated_eye = foveated_eye;
		texture.width = (uint32_t)entry->target.allocated_width;
		texture.height = (uint32_t)entry->target.allocated_height;
		texture.depth = 1;
		texture.levels = 1;
		entry->target.texture = gpu_texture_create(&texture);
		/* the zoomed picture: its size against an eye's unfoveated one */
		if (layer == HALO_STEREO_LAYER_ZOOM && !depth)
		{
			float dense[2], view[2];

			screen_scale_dense(dense);
			halo_stereo_zoom_view(view);
			platform_log("stereo: the zoomed picture's target %lux%lu, %.3f by %.3f times an eye's %.0fx%.0f: the "
				"view's half tangents %.3f by %.3f at the eyes' pixels per tangent", entry->target.gl_width,
				entry->target.gl_height, scale[0] / dense[0], scale[1] / dense[1], width * dense[0],
				height * dense[1], view[0], view[1]);
		}
	}
	entry->next = render_targets;
	render_targets = entry;
	entry->next_in_bucket = *render_target_bucket(entry->target.data);
	*render_target_bucket(entry->target.data) = entry;
	return entry;
}

static struct render_target_entry *render_target_get(const D3DSurface *surface)
{
	int layer = render_target_layer();

	/* stereo's HUD layer, keyed by the current HUD group (halo_stereo.h):
	each group draws into a target of its own, so groups that overlap on
	the screen never share pixels; the catch-all's is the layer's own */
	if (layer == HALO_STEREO_LAYER_HUD && halo_stereo_ui_span() && halo_stereo_hud_split())
		layer = HALO_STEREO_LAYER_UI;
	else if (layer == HALO_STEREO_LAYER_HUD && halo_hud_group_current() != HALO_HUD_GROUP_NONE &&
		halo_stereo_hud_split())
		layer = HALO_STEREO_LAYER_HUD_GROUP + halo_hud_group_current();
	return render_target_get_layer(surface, layer);
}

/* a layer whose target the game never clears, so the device clears it to
empty before its first draw each frame: the reticle's, the UI's and the HUD
groups' */
static int layer_cleared_on_first_draw(int layer)
{
	return layer == HALO_STEREO_LAYER_RETICLE || layer == HALO_STEREO_LAYER_UI || layer >= HALO_STEREO_LAYER_HUD_GROUP;
}

/* a layer the presenter puts over the eyes as rgb + eye * alpha
(hud_layer_blend): the HUD's, the reticle's, the UI's and the HUD groups' */
static int layer_is_hud(int layer)
{
	return layer == HALO_STEREO_LAYER_HUD || layer_cleared_on_first_draw(layer);
}

struct xgpu_render_target *xgpu_render_target_find(unsigned long data)
{
	struct render_target_entry *entry, *best = NULL;
	int layer = render_target_layer();

	for (entry = *render_target_bucket(data); entry; entry = entry->next_in_bucket)
	{
		/* a sampled screen-sized target is the current layer's */
		if (entry->layer != HALO_STEREO_LAYER_MONO && entry->layer != layer)
			continue;
		if (entry->target.data == data && !entry->target.depth && (!best || entry->last_rendered > best->last_rendered))
			best = entry;
	}
	return best ? &best->target : NULL;
}

/* counts draws and clears into render targets (xgpu_render_target.written) */
static unsigned long render_target_write_serial;

/* the pixels per unit of the bound targets (render_target_get) */
static float target_scale[2] = { 1.0f, 1.0f };
/* the bound color target is stereo's HUD layer, the reticle's layer or a
HUD group's target (draw_targets) */
static BOOL target_hud_layer;
/* the bound color target is the HUD layer or a HUD group's target, whose
draws' extents go to the group's rectangle (halo_hud_group_extent_in); and
which group's that is (HALO_HUD_GROUP_NONE: the HUD layer's own) */
static BOOL target_hud_measured;
static int target_hud_group;
/* debug.gpu_stats: HUD-layer draws measured on the CPU (immediate mode)
and those taken as the whole viewport, since the last line */
static unsigned long hud_draws_measured, hud_draws_unmeasured;
/* the bound color target is the zoomed picture (draw_targets), whose HUD
draws (halo_stereo_zoom_overlay_on) are fit to the view's shape */
static BOOL target_zoom_layer;
/* something drew into the HUD layer this frame under render.c's UI span
(halo_stereo_set_ui_span) */
static BOOL hud_layer_ui;
/* the widgets' full-screen dims this frame, combined (halo_stereo_ui_dim_add):
0 to 1, the share of the eyes' light they take */
static float ui_dim;

void halo_stereo_ui_dim_add(const void *texture, float alpha)
{
	float texels;

	/* the fill's own alpha. A texture not loaded yet (the panel's first
	frame) or one that can't be read darkens nothing: guessing opaque
	blinked the eyes black for a frame */
	if (!texture || !xgpu_texture_mean_alpha((const DWORD *)texture, &texels))
		return;
	alpha *= texels;
	if (!(alpha > 0.0f))
		return;
	if (alpha > 1.0f)
		alpha = 1.0f;
	/* one dim over another lets through what each lets through */
	ui_dim = 1.0f - (1.0f - ui_dim) * (1.0f - alpha);
}

/* the pixel edge of a coordinate in the bound targets' units */
static int32_t target_pixel(float coordinate, int axis)
{
	return (int32_t)floorf(coordinate * target_scale[axis] + 0.5f);
}

/* The foveation audit (debug.gpu_stats, Task 10): for the first gameplay
frame (not a film) whose eyes render through the rate maps, a line for each
render pass (each change of the draws' targets: their layer, sizes and
whether they're foveated) and for each texture stage that binds a
screen-sized target (and with which sampler), to match against the stereo
spec's audit table: a line without a row is a pass foveation missed */
static int foveation_audit_state; /* 0 waiting, 1 this frame, 2 done */
static unsigned long foveation_audit_frame, foveation_audit_passes, foveation_audit_reads;
static const struct render_target_entry *foveation_audit_color, *foveation_audit_depth;
static const struct xgpu_render_target *foveation_audit_stages[4];

static const char *foveation_audit_layer(int layer)
{
	static char other[24];

	switch (layer)
	{
	case HALO_STEREO_LAYER_MONO: return "mono";
	case 0: return "left eye";
	case 1: return "right eye";
	case HALO_STEREO_LAYER_HUD: return "HUD";
	case HALO_STEREO_LAYER_ZOOM: return "zoom";
	case HALO_STEREO_LAYER_RETICLE: return "reticle";
	case HALO_STEREO_LAYER_UI: return "UI";
	}
	snprintf(other, sizeof(other), "HUD group %d", layer - HALO_STEREO_LAYER_HUD_GROUP);
	return other;
}

static void foveation_audit_target(char *text, size_t size, const char *role, const struct render_target_entry *entry)
{
	if (!entry)
	{
		snprintf(text, size, "%s none", role);
		return;
	}
	snprintf(text, size, "%s %08lx %lux%lu (%s, %s, %lux%lu%s)", role, entry->target.data, entry->target.width,
		entry->target.height, foveation_audit_layer(entry->layer),
		entry->target.width == (unsigned long)halo_screen_width() && entry->target.height == SCREEN_HEIGHT ?
		"screen-sized" : "offscreen", entry->target.gl_width, entry->target.gl_height,
		entry->target.foveated_eye ? ", foveated" : "");
}

/* at each draw's targets (draw_targets) */
static void foveation_audit_pass(const struct render_target_entry *color, const struct render_target_entry *depth)
{
	unsigned long width, height;
	char color_text[160], depth_text[160];

	if (foveation_audit_state == 1 && device.frame != foveation_audit_frame)
	{
		platform_log("foveation audit: frame %lu ends: %lu passes, %lu reads of screen-sized targets",
			foveation_audit_frame, foveation_audit_passes, foveation_audit_reads);
		foveation_audit_state = 2;
	}
	if (foveation_audit_state == 0 && debug_settings.statistics && foveated_eye_allocation(&width, &height) &&
		!halo_stereo_film())
	{
		foveation_audit_state = 1;
		foveation_audit_frame = device.frame;
		platform_log("foveation audit: frame %lu, the first gameplay frame whose eyes render through the rate maps "
			"(allocated %lux%lu)", device.frame, width, height);
	}
	/* (and with the Mac's debug.rate_map_test, frame 1200, 40 seconds in,
	in a30's gameplay with debug.fixed_timestep, to check the audit itself) */
	else if (foveation_audit_state == 0 && debug_settings.statistics && rate_map_test() && device.frame >= 1200)
	{
		foveation_audit_state = 1;
		foveation_audit_frame = device.frame;
		platform_log("foveation audit: frame %lu, with debug.rate_map_test", device.frame);
	}
	if (foveation_audit_state != 1 || (color == foveation_audit_color && depth == foveation_audit_depth))
		return;
	foveation_audit_color = color;
	foveation_audit_depth = depth;
	memset(foveation_audit_stages, 0, sizeof(foveation_audit_stages));
	foveation_audit_passes++;
	foveation_audit_target(color_text, sizeof(color_text), "color", color);
	foveation_audit_target(depth_text, sizeof(depth_text), "depth", depth);
	platform_log("foveation audit: pass %lu: %s; %s", foveation_audit_passes, color_text, depth_text);
}

/* at each stage that binds a render target (stages_fill) */
static void foveation_audit_stage(int stage, const struct xgpu_render_target *target, unsigned char sampler)
{
	const struct render_target_entry *entry;

	if (foveation_audit_state != 1 || !target || target->width != (unsigned long)halo_screen_width() ||
		target->height != SCREEN_HEIGHT || foveation_audit_stages[stage] == target)
		return;
	foveation_audit_stages[stage] = target;
	foveation_audit_reads++;
	entry = (const struct render_target_entry *)((const char *)target - offsetof(struct render_target_entry, target));
	platform_log("foveation audit: pass %lu: stage %d reads %08lx (%s, %lux%lu%s) with the %s sampler",
		foveation_audit_passes, stage, target->data, foveation_audit_layer(entry->layer), target->gl_width,
		target->gl_height, target->foveated_eye ? ", foveated" : "",
		sampler == _xgpu_sampler_2d_foveated ? "foveated" : "plain");
}

/* the textures the current targets render to; FALSE when there are none */
static BOOL draw_targets(gpu_texture *color_texture, gpu_texture *depth_texture)
{
	struct render_target_entry *color = render_target_get(device.render_target);
	struct render_target_entry *depth = render_target_get(device.depth_stencil);

	if (depth && !depth->target.depth)
		depth = NULL;
	if (!color && !depth)
		return FALSE;
	foveation_audit_pass(color, depth);
	/* the reticle's and the HUD groups' targets: empty before their first
	draw of the frame (the HUD layer's own empty, hud_layer_blend: no color,
	the whole picture showing) */
	if (color && layer_cleared_on_first_draw(color->layer) && color->last_rendered != device.frame + 1)
	{
		struct gpu_clear clear;
		struct gpu_rect whole = { 0, 0, (int32_t)color->target.gl_width, (int32_t)color->target.gl_height };

		memset(&clear, 0, sizeof(clear));
		clear.color_target = color->target.texture;
		clear.flags = GPU_CLEAR_COLOR;
		clear.channel_mask = GPU_CHANNEL_RED | GPU_CHANNEL_GREEN | GPU_CHANNEL_BLUE | GPU_CHANNEL_ALPHA;
		clear.color = 0xff000000u;
		gpu_clear(&clear, &whole, 1);
	}
	if (color)
	{
		color->last_rendered = device.frame + 1;
		color->target.written = ++render_target_write_serial;
	}
	if (color && color->layer == HALO_STEREO_LAYER_RETICLE)
		halo_stereo_reticle_drew();
	target_hud_layer = color && layer_is_hud(color->layer);
	target_hud_measured = color && (color->layer == HALO_STEREO_LAYER_HUD ||
		color->layer >= HALO_STEREO_LAYER_HUD_GROUP);
	target_hud_group = color && color->layer >= HALO_STEREO_LAYER_HUD_GROUP ?
		color->layer - HALO_STEREO_LAYER_HUD_GROUP : HALO_HUD_GROUP_NONE;
	target_zoom_layer = color && color->layer == HALO_STEREO_LAYER_ZOOM;
	/* viewports and clears are in the targets' units (render_target_get) */
	target_scale[0] = color ? color->target.scale[0] : depth->target.scale[0];
	target_scale[1] = color ? color->target.scale[1] : depth->target.scale[1];
	*color_texture = color ? color->target.texture : 0;
	*depth_texture = depth ? depth->target.texture : 0;
	return TRUE;
}

/* what the shader translators emit for this context */
static struct nv2a_dialect shader_dialect;

/* debug.rate_map_test, on Metal outside a stereo frame */
static int rate_map_test(void)
{
	static int wanted = -1;

	if (wanted < 0)
	{
		wanted = config_boolean("debug.rate_map_test");
		if (wanted)
			platform_log("debug.rate_map_test: the mono screen's targets render through a synthetic rate map%s",
				shader_dialect.msl ? "" : "; not with this renderer (Metal only)");
	}
	return wanted && shader_dialect.msl && halo_stereo_frame()->eye_count == 0;
}

/* foveated eye passes this frame (halo_stereo_frame's foveated_width, from
host_stereo_foveated_size): the size the eyes' screen-sized targets are
allocated at, which their rate maps fill from the top left. Only on Metal,
whose backend binds the maps (gpu_metal.m) */
static int foveated_eye_allocation(unsigned long *width, unsigned long *height)
{
	const struct halo_stereo_frame *stereo = halo_stereo_frame();

	if (!shader_dialect.msl || !head_eyes_frame() || !stereo->foveated || stereo->foveated_width <= 0 ||
		stereo->foveated_height <= 0)
		return 0;
	*width = (unsigned long)stereo->foveated_width;
	*height = (unsigned long)stereo->foveated_height;
	return 1;
}

static void shader_dialect_initialize(struct nv2a_dialect *dialect, const struct gpu_capabilities *capabilities)
{
	memset(dialect, 0, sizeof(*dialect));
	dialect->es = capabilities->shader_es;
	dialect->version = capabilities->shader_language;
	dialect->msl = capabilities->shader_language == GPU_SHADER_LANGUAGE_MSL;
	dialect->clip_y_flip = capabilities->clip_y_flip;
	dialect->clip_z_remap = capabilities->clip_z_remap;
	/* the mobile GPUs' precision workaround goes with OpenGL ES, and with
	Metal, which runs on the same GPUs */
	dialect->clip_capture = capabilities->shader_es || dialect->msl;
	dialect->shader_lod_bias = !capabilities->sampler_lod_bias;
	dialect->debug_expression = config_string("debug.gpu_debug_expression");
	dialect->debug_texture0 = config_boolean("debug.gpu_debug_texture0") != 0;
	dialect->debug_flat = config_boolean("debug.gpu_debug_flat") != 0;
}

/* the capabilities of desktop OpenGL 4.5, for replaying shaders through its
dialect (debug.gpu_shader_replay_dialect) on a context that isn't */
static void desktop_capabilities(struct gpu_capabilities *capabilities)
{
	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->sampler_lod_bias = 1;
	capabilities->shader_language = 450;
}

/* the dialect fields of the Metal backend's capabilities (gpu_metal.m), for
replaying shaders as MSL on a GL context. sampler_lod_bias is 0: every MSL
lookup takes a bias argument (nv2a_msl.c) */
static void metal_capabilities(struct gpu_capabilities *capabilities)
{
	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->shader_language = GPU_SHADER_LANGUAGE_MSL;
}

/* the file extension of a dialect's shader source */
static const char *shader_extension(const struct nv2a_dialect *dialect)
{
	return dialect->msl ? "metal" : "glsl";
}

/* ---------- shader replay (debug.gpu_shader_replay)

Translates every recorded shader input in a folder into its replay folder,
so the translators' output can be compared byte for byte across changes
without playing to the scenes that first produced them. */

static char *read_whole_file(const char *path, unsigned long *size)
{
	FILE *file = fopen(path, "rb");
	char *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = length > 0 ? malloc((size_t)length) : NULL;
	if (data && fread(data, 1, (size_t)length, file) != (size_t)length)
	{
		free(data);
		data = NULL;
	}
	fclose(file);
	*size = data ? (unsigned long)length : 0;
	return data;
}

static void shader_replay(const char *directory)
{
	void *listing = posix_directory_open(directory);
	char name[256], path[512], output[512];
	unsigned long translated = 0, skipped = 0, compiled = 0;
	struct nv2a_dialect dialect;
	struct gpu_capabilities desktop, metal;
	const char *replay_dialect = config_string("debug.gpu_shader_replay_dialect");
	BOOL compile;

	if (!listing)
	{
		platform_log("shader replay: cannot open %s", directory);
		return;
	}
	snprintf(output, sizeof(output), "%s/replay", directory);
	posix_make_directory(output);
	/* debug.gpu_shader_replay_dialect "450" replays through the desktop
	dialect, which this build doesn't otherwise run, and "msl" through the
	Metal backend's */
	desktop_capabilities(&desktop);
	metal_capabilities(&metal);
	shader_dialect_initialize(&dialect, !strcmp(replay_dialect, "450") ? &desktop :
		!strcmp(replay_dialect, "msl") ? &metal : &device_capabilities);
	/* the Metal backend also compiles what it replays (GL's replay compiles
	nothing, which keeps its GL calls as they were) */
	compile = dialect.msl && shader_dialect.msl;
	while (posix_directory_next(listing, name, sizeof(name)))
	{
		size_t length = strlen(name);
		BOOL vertex = length > 4 && !strcmp(name + length - 4, ".vsh");
		BOOL pixel = length > 4 && !strcmp(name + length - 4, ".key");
		unsigned long size = 0;
		char *data, *source = NULL;
		FILE *file;

		if (!vertex && !pixel)
			continue;
		snprintf(path, sizeof(path), "%s/%s", directory, name);
		data = read_whole_file(path, &size);
		if (vertex && data && size >= 2 * sizeof(DWORD))
		{
			const DWORD *header = (const DWORD *)data;

			/* (divided, not multiplied: a corrupt count must not wrap around) */
			if ((size - 2 * sizeof(DWORD)) % (4 * sizeof(DWORD)) == 0 &&
				(size - 2 * sizeof(DWORD)) / (4 * sizeof(DWORD)) == header[0])
				source = nv2a_vertex_shader_translate(&dialect, header + 2, header[0], header[1]);
		}
		else if (pixel && data && size == sizeof(struct nv2a_pixel_shader_key))
		{
			source = nv2a_pixel_shader_translate(&dialect, (const struct nv2a_pixel_shader_key *)data);
		}
		free(data);
		if (!source)
		{
			platform_log("shader replay: skipping %s (%lu bytes, malformed)", name, size);
			skipped++;
			continue;
		}
		snprintf(path, sizeof(path), "%s/%.*s.%s", output, (int)(length - 4), name, shader_extension(&dialect));
		if ((file = fopen(path, "w")) != NULL)
		{
			fputs(source, file);
			fclose(file);
		}
		if (compile && gpu_shader_create(vertex ? GPU_SHADER_VERTEX : GPU_SHADER_PIXEL, source))
			compiled++;
		free(source);
		translated++;
	}
	posix_directory_close(listing);
	platform_log("shader replay: %lu translated, %lu skipped", translated, skipped);
	if (compile)
		platform_log("shader replay: %lu compiled, %lu failed", compiled, translated - compiled);
}

/* ---------- device creation */

static void gl_initialize(void)
{
	int index;

	gpu_initialize((config_boolean("debug.gl_debug") ? GPU_INITIALIZE_DEBUG : 0) |
		(platform_renderer_metal() ? GPU_INITIALIZE_METAL : 0) |
		(config_boolean("debug.frame_counter") ? GPU_INITIALIZE_FRAME_COUNTER : 0) |
		(platform_fixed_timestep() ? GPU_INITIALIZE_FIXED_TIMESTEP : 0) | frame_pacing_flags() |
		(config_boolean("display.compressed_textures") ? GPU_INITIALIZE_COMPRESSED_TEXTURES : 0) |
		(!strcmp(config_string("display.upscaler"), "metalfx") ? GPU_INITIALIZE_METALFX : 0) |
		(config_boolean("display.immersive") ? GPU_INITIALIZE_IMMERSIVE : 0) |
		(config_boolean("debug.metal_state_cache") ? 0 : GPU_INITIALIZE_NO_STATE_CACHE) |
		(config_boolean("debug.metal_specialize") ? 0 : GPU_INITIALIZE_NO_SPECIALIZE) |
		(config_boolean("debug.metal_pipeline_archive") ? 0 : GPU_INITIALIZE_NO_PIPELINE_ARCHIVE), &device_capabilities);
	screen_maximum_texture_size = (int32_t)device_capabilities.max_texture_size;
#ifdef HALO_ILP32
	/* Select the real Retina drawable before allocating any screen targets. */
	(void)halo_screen_width();
	screen_mode_choose(&screen_width, screen_scale);
	platform_log("iOS render target: %.0fx%.0f (logical %ldx%d)",
		screen_width * screen_scale[0], SCREEN_HEIGHT * screen_scale[1], screen_width, SCREEN_HEIGHT);
#endif
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		device.attributes[index][3] = 1.0f;
	memory_watch_initialize();
	debug_settings.skip_vertex_shaders = config_string("debug.gpu_skip_vertex_shaders");
	debug_settings.dump_shaders = *config_string("debug.gpu_dump_shaders") ?
		config_string("debug.gpu_dump_shaders") : NULL;
	debug_settings.statistics = config_boolean("debug.gpu_stats");
	shader_dialect_initialize(&shader_dialect, &device_capabilities);
	debug_settings.shader_replay = *config_string("debug.gpu_shader_replay") ?
		config_string("debug.gpu_shader_replay") : NULL;
	if (debug_settings.shader_replay)
		shader_replay(debug_settings.shader_replay);
	device.gl_ready = TRUE;
}

Direct3D *WINAPI Direct3DCreate8(UINT sdk_version)
{
	(void)sdk_version;
	return (Direct3D *)1;
}

void WINAPI Direct3D_SetPushBufferSize(DWORD push_buffer_size, DWORD segment_count)
{
	(void)push_buffer_size;
	(void)segment_count;
}

/* the vertex constants, each register's serial (the value serial took when
the register last changed; a program's registers are current up to the
serial it recorded when it last uploaded them) and the register each of the
latest serials changed, so a program that is only a little behind finds its
changed registers without a full scan */
static struct gpu_constant_store constant_store;

static void constants_store(unsigned long first, const void *data, unsigned long count)
{
	const float (*values)[4] = data;
	unsigned long index;

	for (index = 0; index < count; index++)
	{
		if (memcmp(constant_store.c[first + index], values[index], sizeof(constant_store.c[0])))
		{
			memcpy(constant_store.c[first + index], values[index], sizeof(constant_store.c[0]));
			constant_store.serials[first + index] = ++constant_store.serial;
			constant_store.log[constant_store.serial % GPU_CONSTANT_LOG_SIZE] = (unsigned char)(first + index);
		}
	}
}

static void viewport_update_constants(void)
{
	/* Direct3D's reserved constants c[-38] and c[-37] map clip space to
	the screen; zscale is the depth buffer's range */
	float zscale = 16777215.0f;
	unsigned long width, height;
	BOOL depth;

	if (device.depth_stencil)
	{
		struct xgpu_texture_description description;

		xgpu_texture_describe(device.depth_stencil->Format, device.depth_stencil->Size, &description);
		if (description.format == D3DFMT_D16 || description.format == D3DFMT_LIN_D16 ||
			description.format == D3DFMT_F16 || description.format == D3DFMT_LIN_F16)
		{
			zscale = 65535.0f;
		}
	}
	(void)width; (void)height; (void)depth;
	device.viewport_scale[0] = device.viewport.Width * 0.5f;
	device.viewport_scale[1] = -(float)device.viewport.Height * 0.5f;
	device.viewport_scale[2] = zscale * (device.viewport.MaxZ - device.viewport.MinZ);
	device.viewport_scale[3] = 0.0f;
	device.viewport_offset[0] = device.viewport.X + device.viewport.Width * 0.5f;
	device.viewport_offset[1] = device.viewport.Y + device.viewport.Height * 0.5f;
	device.viewport_offset[2] = zscale * device.viewport.MinZ;
	device.viewport_offset[3] = 0.0f;
	if (!(device.shader_constant_mode & D3DSCM_NORESERVEDCONSTANTS))
	{
		constants_store(XGPU_VERTEX_CONSTANT_BIAS - 38, device.viewport_scale, 1);
		constants_store(XGPU_VERTEX_CONSTANT_BIAS - 37, device.viewport_offset, 1);
	}
}

HRESULT WINAPI Direct3D_CreateDevice(UINT adapter, D3DDEVTYPE device_type, void *unused, DWORD behavior_flags,
	D3DPRESENT_PARAMETERS *presentation_parameters, D3DDevice **returned_device)
{
	unsigned long width, height;
	int index;

	(void)adapter;
	(void)device_type;
	(void)unused;
	(void)behavior_flags;
	if (!device.created)
	{
		memset(&device, 0, sizeof(device));
		if (presentation_parameters)
			device.presentation = *presentation_parameters;
		width = device.presentation.BackBufferWidth ? device.presentation.BackBufferWidth : 640;
		height = device.presentation.BackBufferHeight ? device.presentation.BackBufferHeight : 480;
#ifdef HALO_ILP32
		d3d8_surface_initialize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, width, height);
		d3d8_surface_initialize(&device.depth_buffer, D3DFMT_LIN_D24S8, width, height);
#else
		/* room for the widest screen, which F11 can switch to (the screen's
		width, above) */
		d3d8_surface_initialize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, SCREEN_MAXIMUM_WIDTH, height);
		d3d8_surface_initialize(&device.depth_buffer, D3DFMT_LIN_D24S8, SCREEN_MAXIMUM_WIDTH, height);
		d3d8_surface_resize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, width, height);
		d3d8_surface_resize(&device.depth_buffer, D3DFMT_LIN_D24S8, width, height);
#endif
		device.render_target = &device.back_buffer;
		device.depth_stencil = &device.depth_buffer;
		for (index = 0; index < D3DTS_MAX; index++)
		{
			device.transforms[index]._11 = 1.0f;
			device.transforms[index]._22 = 1.0f;
			device.transforms[index]._33 = 1.0f;
			device.transforms[index]._44 = 1.0f;
		}
		device.viewport.Width = width;
		device.viewport.Height = height;
		device.viewport.MaxZ = 1.0f;
		device.next_vertex_shader_id = 1;
		D3D__RenderState[D3DRS_ZENABLE] = TRUE;
		D3D__RenderState[D3DRS_ZWRITEENABLE] = TRUE;
		D3D__RenderState[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
		D3D__RenderState[D3DRS_COLORWRITEENABLE] = D3DCOLORWRITEENABLE_ALL;
		D3D__RenderState[D3DRS_SRCBLEND] = D3DBLEND_ONE;
		D3D__RenderState[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
		D3D__RenderState[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
		D3D__RenderState[D3DRS_CULLMODE] = D3DCULL_CCW;
		D3D__RenderState[D3DRS_FRONTFACE] = D3DFRONT_CW;
		D3D__RenderState[D3DRS_FILLMODE] = D3DFILL_SOLID;
		D3D__RenderState[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
		D3D__RenderState[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
		D3D__RenderState[D3DRS_STENCILMASK] = 0xff;
		D3D__RenderState[D3DRS_STENCILWRITEMASK] = 0xff;
		D3D__RenderState[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
		D3D__RenderState[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
		D3D__RenderState[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
		for (index = 0; index < D3DTSS_MAXSTAGES; index++)
		{
			D3D__TextureState[index][D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_ADDRESSW] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_MAGFILTER] = D3DTEXF_POINT;
			D3D__TextureState[index][D3DTSS_MINFILTER] = D3DTEXF_POINT;
			D3D__TextureState[index][D3DTSS_MAXANISOTROPY] = 1;
		}
		viewport_update_constants();

		if (!config_boolean("debug.null_renderer") && platform_video_initialize(width, height))
			gl_initialize();
		else
			platform_log("Direct3D: running without a window (nothing is displayed)");
		device.created = TRUE;
	}
	*returned_device = device_pointer();
	return S_OK;
}

/* ---------- the menus' pointer */

#ifdef HALO_ILP32
int halo_ui_pointer_update(int menus_active, struct halo_ui_pointer *pointer)
{
	(void)pointer;
	platform_menus_set_active(menus_active != 0);
	return 0;
}
#else
/* a point in the window, as SDL reports it, in the menus' coordinates: the
inverse of the letterboxed display blit at presentation, the screen's
width and the menus' centering (halo_screen_ui_offset) */
static void ui_point_from_window(float window_x, float window_y, short *x, short *y)
{
	struct render_target_entry *back_buffer = render_target_get(&device.back_buffer);
	int window_width, window_height, pixel_width, pixel_height, width, height, left, top;
	float screen_x, screen_y;

	*x = *y = -1;
	if (!back_buffer)
		return;
	platform_video_window_size(&window_width, &window_height);
	platform_video_drawable_size(&pixel_width, &pixel_height);
	if (window_width <= 0 || window_height <= 0)
		return;
	width = pixel_width;
	height = (int)((long)pixel_width * back_buffer->target.gl_height / back_buffer->target.gl_width);
	if (height > pixel_height)
	{
		height = pixel_height;
		width = (int)((long)pixel_height * back_buffer->target.gl_width / back_buffer->target.gl_height);
	}
	left = (pixel_width - width) / 2;
	top = (pixel_height - height) / 2;
	screen_x = (window_x * pixel_width / window_width - left) * (float)back_buffer->target.width / (float)width;
	screen_y = (window_y * pixel_height / window_height - top) * (float)back_buffer->target.height / (float)height;
	*x = (short)floorf(screen_x - (float)(halo_screen_width() - 640) / 2.0f);
	*y = (short)floorf(screen_y);
}

int halo_ui_pointer_update(int menus_active, struct halo_ui_pointer *pointer)
{
	struct platform_ui_pointer state;

	platform_menus_set_active(menus_active != 0);
	platform_ui_pointer_set_active(menus_active != 0);
	if (!menus_active || !device.gl_ready || !platform_ui_pointer_read(&state))
		return 0;
	memset(pointer, 0, sizeof(*pointer));
	ui_point_from_window(state.x, state.y, &pointer->x, &pointer->y);
	ui_point_from_window(state.click_x, state.click_y, &pointer->click_x, &pointer->click_y);
	pointer->moved = state.moved != FALSE;
	pointer->left_clicks = (unsigned char)(state.left_clicks < 255 ? state.left_clicks : 255);
	pointer->right_clicks = (unsigned char)(state.right_clicks < 255 ? state.right_clicks : 255);
	pointer->wheel_steps = (signed char)(state.wheel_steps < -8 ? -8 : state.wheel_steps > 8 ? 8 : state.wheel_steps);
	return 1;
}
#endif

/* the open scoreboard's pointer (game_engine.c): not wired to this renderer's
platform layer yet (port/linux/src/sdl_platform.c's platform_scoreboard_pointer
has no counterpart here), so there is none, as on Android: the scoreboard
shows no line for picking players */
int halo_scoreboard_pointer_update(int offered, struct halo_ui_pointer *pointer)
{
	(void)offered;
	if (pointer)
		memset(pointer, 0, sizeof(*pointer));
	return -1;
}

/* takes up the display's shape and resolution, or the window's, if they
have changed; between frames, since the game's layout and the targets must
agree for a whole frame. Returns the width the game draws. */
long halo_screen_commit(void)
{
	long width;
	float scale[2];

	if (!screen_width)
		return halo_screen_width();
	screen_mode_choose(&width, scale);
	if (!head_eyes_frame())
	{
		screen_scale_mono[0] = scale[0];
		screen_scale_mono[1] = scale[1];
	}
	if (width != screen_width || scale[0] != screen_scale[0] || scale[1] != screen_scale[1])
	{
		platform_log("screen: %ldx%d drawn at %.0fx%.0f", width, SCREEN_HEIGHT,
			width * scale[0], SCREEN_HEIGHT * scale[1]);
		screen_width = width;
		screen_scale[0] = scale[0];
		screen_scale[1] = scale[1];
#ifndef HALO_ILP32
		if (device.created)
		{
			device.presentation.BackBufferWidth = (UINT)width;
			d3d8_surface_resize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, (unsigned long)width, SCREEN_HEIGHT);
			d3d8_surface_resize(&device.depth_buffer, D3DFMT_LIN_D24S8, (unsigned long)width, SCREEN_HEIGHT);
		}
#endif
	}
	return screen_width;
}

/* In a stereo frame, at its begin (stereo.c): takes up the scale the
frame's eyes need now, so its first frame with the Compositor's views, or
the first on the theater screen after them, renders at their size rather
than the last frame's. Only the scale: a new width waits for
halo_screen_commit, which the rasterizer follows. */
void halo_screen_commit_stereo_scale(void)
{
	const struct halo_stereo_frame *stereo = halo_stereo_frame();
	long width;
	float scale[2];
	/* what the scale depends on, to skip the work while it doesn't change */
	static int32_t last_mode = -1, last_eye_count, last_width, last_height, last_foveated;
	static double last_render_scale;

	if (!screen_width)
		return;
	if (stereo->mode == last_mode && stereo->eye_count == last_eye_count && stereo->eye_width == last_width &&
		stereo->eye_height == last_height && stereo->foveated == last_foveated && render_scale_live == last_render_scale)
		return;
	last_foveated = stereo->foveated;
	last_mode = stereo->mode;
	last_eye_count = stereo->eye_count;
	last_width = stereo->eye_width;
	last_height = stereo->eye_height;
	last_render_scale = render_scale_live;
	screen_mode_choose(&width, scale);
	if (!head_eyes_frame())
	{
		screen_scale_mono[0] = scale[0];
		screen_scale_mono[1] = scale[1];
	}
	if (width == screen_width && (scale[0] != screen_scale[0] || scale[1] != screen_scale[1]))
	{
		platform_log("screen: %ldx%d drawn at %.0fx%.0f for the stereo frame", width, SCREEN_HEIGHT,
			width * scale[0], SCREEN_HEIGHT * scale[1]);
		screen_scale[0] = scale[0];
		screen_scale[1] = scale[1];
	}
}

ULONG WINAPI D3DDevice_Release(void)
{
	return 1;
}

void WINAPI D3DDevice_GetDeviceCaps(D3DCAPS8 *caps)
{
	memset(caps, 0, sizeof(*caps));
	caps->DeviceType = D3DDEVTYPE_HAL;
	caps->MaxTextureWidth = 4096;
	caps->MaxTextureHeight = 4096;
	caps->MaxVolumeExtent = 512;
	caps->MaxTextureRepeat = 8192;
	caps->MaxTextureAspectRatio = 4096;
	caps->MaxAnisotropy = 4;
	caps->MaxTextureBlendStages = 4;
	caps->MaxSimultaneousTextures = 4;
	caps->MaxActiveLights = 8;
	caps->MaxVertexBlendMatrices = 4;
	caps->MaxPointSize = 64.0f;
	caps->MaxPrimitiveCount = 0xfffff;
	caps->MaxVertexIndex = 0xffff;
	caps->MaxStreams = 16;
	caps->MaxStreamStride = 255;
	caps->VertexShaderVersion = D3DVS_VERSION(1, 1);
	caps->MaxVertexShaderConst = 192;
	caps->PixelShaderVersion = D3DPS_VERSION(1, 1);
	caps->MaxPixelShaderValue = 1.0f;
}

void WINAPI D3DDevice_GetBackBuffer(INT back_buffer, D3DBACKBUFFER_TYPE type, D3DSurface **result)
{
	(void)back_buffer;
	(void)type;
	/* like Direct3D, the caller gets a reference it must release */
	device.back_buffer.Common++;
	*result = &device.back_buffer;
}

HRESULT WINAPI D3DDevice_GetDepthStencilSurface(D3DSurface **result)
{
	*result = device.depth_stencil;
	if (!*result)
		return D3DERR_NOTFOUND;
	(*result)->Common++;
	return S_OK;
}

static BOOL trace_frame(void);

void WINAPI D3DDevice_SetRenderTarget(D3DSurface *render_target, D3DSurface *depth_stencil)
{
	if (trace_frame())
		platform_log("set render target %08lx depth %08lx", render_target ? (unsigned long)render_target->Data : 0,
			depth_stencil ? (unsigned long)depth_stencil->Data : 0);
	stats.target_changes++;
	if (render_target)
		device.render_target = render_target;
	device.depth_stencil = depth_stencil;
	/* like Direct3D, reset the viewport to the whole new target */
	if (device.render_target)
	{
		unsigned long width, height;
		BOOL depth;

		surface_dimensions(device.render_target, &width, &height, &depth);
		device.viewport.X = 0;
		device.viewport.Y = 0;
		device.viewport.Width = width;
		device.viewport.Height = height;
		device.viewport.MinZ = 0.0f;
		device.viewport.MaxZ = 1.0f;
	}
	viewport_update_constants();
}

void WINAPI D3DDevice_SetViewport(CONST D3DVIEWPORT8 *viewport)
{
	device.viewport = *viewport;
	viewport_update_constants();
}

void WINAPI D3DDevice_SetTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		device.transforms[state] = *matrix;
}

void WINAPI D3DDevice_GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		*matrix = device.transforms[state];
}

void WINAPI D3DDevice_SetFlickerFilter(DWORD filter) { (void)filter; }
void WINAPI D3DDevice_SetSoftDisplayFilter(BOOL enable) { (void)enable; }

void WINAPI D3DDevice_SetShaderConstantMode(D3DSHADERCONSTANTMODE mode)
{
	device.shader_constant_mode = mode;
	viewport_update_constants();
}

/* ---------- GPU synchronisation: GL keeps its own ordering */

BOOL WINAPI D3DDevice_IsBusy(void)
{
	return FALSE;
}

void WINAPI D3DDevice_KickPushBuffer(void)
{
	if (device.gl_ready)
		gpu_flush();
}

void WINAPI D3DDevice_InsertCallback(D3DCALLBACKTYPE type, D3DCALLBACK callback, DWORD context)
{
	(void)type;
	/* the "GPU" reaches the callback immediately */
	if (callback)
		callback(context);
}

/* ---------- visibility (occlusion) tests */

void WINAPI D3DDevice_BeginVisibilityTest(void)
{
	if (!device.gl_ready || device.visibility_test_active)
		return;
	/* the slot is chosen when the test ends */
	device.visibility_test_active = TRUE;
	gpu_visibility_begin();
}

HRESULT WINAPI D3DDevice_EndVisibilityTest(DWORD index)
{
	if (!device.gl_ready || !device.visibility_test_active)
		return S_OK;
	device.visibility_test_active = FALSE;
	index %= GPU_VISIBILITY_SLOTS;
	if (!index)
		index = 1;
	/* the target's pixels to a game pixel: the result is a count of the
	game's pixels (visibility_unscaled), which the game divides by its own
	test's area (lens flares, rasterizer_lights.c), a split-screen window's
	or the screen's alike */
	device.query_area[index] = target_scale[0] * target_scale[1];
	device.query_pending[index] = TRUE;
	gpu_visibility_end((uint32_t)index);
	return S_OK;
}

/* a count of samples in the game's pixels */
static uint32_t visibility_unscaled(uint32_t samples, DWORD index)
{
	float area = device.query_area[index];

	return area > 1.0f ? (uint32_t)(samples / area + 0.5f) : samples;
}

HRESULT WINAPI D3DDevice_GetVisibilityTestResult(DWORD index, UINT *result, ULONGLONG *time_stamp)
{
	uint32_t samples = 0;

	if (time_stamp)
		*time_stamp = 0;
	index %= GPU_VISIBILITY_SLOTS;
	if (!index)
		index = 1;
	if (!device.gl_ready || !device.query_pending[index])
	{
		if (result)
			*result = 0;
		return S_OK;
	}
	if (!gpu_visibility_result((uint32_t)index, &samples))
		return D3DERR_TESTINCOMPLETE;
	switch (device_capabilities.occlusion_mode)
	{
	case GPU_OCCLUSION_EXACT:
		samples = visibility_unscaled(samples, index);
		break;
#ifdef HALO_ILP32
	case GPU_OCCLUSION_ANY_SAMPLE:
		/* ES only says whether any sample passed. The game divides the count by
		the test's area (lens flare brightness, rasterizer_lights.c): report
		more than any test covers, well below what would overflow there. */
		if (samples)
			samples = VISIBILITY_ALL_SAMPLES;
		break;
#endif
	default:
		/* the shader counter's count is returned as counted */
		break;
	}
	if (result)
		*result = samples;
	return S_OK;
}

/* ---------- render and texture stage state */

void D3DFASTCALL D3DDevice_SetRenderState_Simple(DWORD method, DWORD value)
{
	/* callers also store the value in D3D__RenderState themselves */
	(void)method;
	(void)value;
}

void D3DFASTCALL D3DDevice_SetRenderState_Deferred(D3DRENDERSTATETYPE state, DWORD value)
{
	if ((unsigned long)state < D3DRS_MAX)
		D3D__RenderState[state] = value;
}

void WINAPI D3DDevice_SetRenderState_ZBias(DWORD value);

void WINAPI D3DDevice_SetRenderStateNotInline(D3DRENDERSTATETYPE state, DWORD value)
{
	if (state == D3DRS_ZBIAS)
		D3DDevice_SetRenderState_ZBias(value);
	else if ((unsigned long)state < D3DRS_MAX)
		D3D__RenderState[state] = value;
}

/* As the Xbox's D3D8 does it: a z bias is a polygon offset of -bias depth
units plus -bias/4 times the polygon's depth slope, enabled for every fill
mode. Without the slope term, decals (biased by 8) fight with the surface
under them wherever it is seen at an angle. */
void WINAPI D3DDevice_SetRenderState_ZBias(DWORD value)
{
	float offset = -(float)value;
	float slope = offset * 0.25f;
	DWORD enable = value != 0;

	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZSLOPESCALE], &slope, sizeof(slope));
	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZOFFSET], &offset, sizeof(offset));
	D3D__RenderState[D3DRS_POINTOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_WIREFRAMEOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_SOLIDOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_ZBIAS] = value;
}

#define COMPLEX_RENDER_STATE(name, state) \
	void WINAPI D3DDevice_SetRenderState_##name(DWORD value) { D3D__RenderState[state] = value; }

COMPLEX_RENDER_STATE(PSTextureModes, D3DRS_PSTEXTUREMODES)
COMPLEX_RENDER_STATE(VertexBlend, D3DRS_VERTEXBLEND)
COMPLEX_RENDER_STATE(FogColor, D3DRS_FOGCOLOR)
COMPLEX_RENDER_STATE(FillMode, D3DRS_FILLMODE)
COMPLEX_RENDER_STATE(BackFillMode, D3DRS_BACKFILLMODE)
COMPLEX_RENDER_STATE(TwoSidedLighting, D3DRS_TWOSIDEDLIGHTING)
COMPLEX_RENDER_STATE(NormalizeNormals, D3DRS_NORMALIZENORMALS)
COMPLEX_RENDER_STATE(ZEnable, D3DRS_ZENABLE)
COMPLEX_RENDER_STATE(StencilEnable, D3DRS_STENCILENABLE)
COMPLEX_RENDER_STATE(StencilFail, D3DRS_STENCILFAIL)
COMPLEX_RENDER_STATE(FrontFace, D3DRS_FRONTFACE)
COMPLEX_RENDER_STATE(CullMode, D3DRS_CULLMODE)
COMPLEX_RENDER_STATE(TextureFactor, D3DRS_TEXTUREFACTOR)
COMPLEX_RENDER_STATE(LogicOp, D3DRS_LOGICOP)
COMPLEX_RENDER_STATE(EdgeAntiAlias, D3DRS_EDGEANTIALIAS)
COMPLEX_RENDER_STATE(MultiSampleAntiAlias, D3DRS_MULTISAMPLEANTIALIAS)
COMPLEX_RENDER_STATE(MultiSampleMask, D3DRS_MULTISAMPLEMASK)
COMPLEX_RENDER_STATE(MultiSampleType, D3DRS_MULTISAMPLETYPE)
COMPLEX_RENDER_STATE(ShadowFunc, D3DRS_SHADOWFUNC)
COMPLEX_RENDER_STATE(LineWidth, D3DRS_LINEWIDTH)
COMPLEX_RENDER_STATE(Dxt1NoiseEnable, D3DRS_DXT1NOISEENABLE)
COMPLEX_RENDER_STATE(YuvEnable, D3DRS_YUVENABLE)
COMPLEX_RENDER_STATE(OcclusionCullEnable, D3DRS_OCCLUSIONCULLENABLE)
COMPLEX_RENDER_STATE(StencilCullEnable, D3DRS_STENCILCULLENABLE)
COMPLEX_RENDER_STATE(RopZCmpAlwaysRead, D3DRS_ROPZCMPALWAYSREAD)
COMPLEX_RENDER_STATE(RopZRead, D3DRS_ROPZREAD)
COMPLEX_RENDER_STATE(DoNotCullUncompressed, D3DRS_DONOTCULLUNCOMPRESSED)

void D3DFASTCALL D3DDevice_SetTextureState_Deferred(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES && (unsigned long)type < D3DTSS_MAX)
		D3D__TextureState[stage][type] = value;
}

void WINAPI D3DDevice_SetTextureState_TexCoordIndex(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_TEXCOORDINDEX] = value;
}

void WINAPI D3DDevice_SetTextureState_BorderColor(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_BORDERCOLOR] = value;
}

void WINAPI D3DDevice_SetTextureState_ColorKeyColor(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_COLORKEYCOLOR] = value;
}

void WINAPI D3DDevice_SetTextureState_BumpEnv(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES && (unsigned long)type < D3DTSS_MAX)
		D3D__TextureState[stage][type] = value;
}

void WINAPI D3DDevice_SetTexture(DWORD stage, D3DBaseTexture *texture)
{
	if (stage < D3DTSS_MAXSTAGES)
		device.textures[stage] = texture;
}

void WINAPI D3DDevice_SetPalette(DWORD stage, D3DPalette *palette)
{
	if (stage < D3DTSS_MAXSTAGES)
		device.palettes[stage] = palette;
}

void WINAPI D3DDevice_SetPixelShaderProgram(D3DPIXELSHADERDEF *definition)
{
	/* the definition's members are the pixel shader render states */
	if (!definition)
		return;
	memcpy(&D3D__RenderState[D3DRS_PSALPHAINPUTS0], definition->PSAlphaInputs, sizeof(definition->PSAlphaInputs));
	D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSABCD] = definition->PSFinalCombinerInputsABCD;
	D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSEFG] = definition->PSFinalCombinerInputsEFG;
	memcpy(&D3D__RenderState[D3DRS_PSCONSTANT0_0], definition->PSConstant0, sizeof(definition->PSConstant0));
	memcpy(&D3D__RenderState[D3DRS_PSCONSTANT1_0], definition->PSConstant1, sizeof(definition->PSConstant1));
	memcpy(&D3D__RenderState[D3DRS_PSALPHAOUTPUTS0], definition->PSAlphaOutputs, sizeof(definition->PSAlphaOutputs));
	memcpy(&D3D__RenderState[D3DRS_PSRGBINPUTS0], definition->PSRGBInputs, sizeof(definition->PSRGBInputs));
	D3D__RenderState[D3DRS_PSCOMPAREMODE] = definition->PSCompareMode;
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0] = definition->PSFinalCombinerConstant0;
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1] = definition->PSFinalCombinerConstant1;
	memcpy(&D3D__RenderState[D3DRS_PSRGBOUTPUTS0], definition->PSRGBOutputs, sizeof(definition->PSRGBOutputs));
	D3D__RenderState[D3DRS_PSCOMBINERCOUNT] = definition->PSCombinerCount;
	D3D__RenderState[D3DRS_PSTEXTUREMODES] = definition->PSTextureModes;
	D3D__RenderState[D3DRS_PSDOTMAPPING] = definition->PSDotMapping;
	D3D__RenderState[D3DRS_PSINPUTTEXTURE] = definition->PSInputTexture;
}

/* ---------- vertex shaders */

static unsigned long vertex_type_bytes(unsigned long type)
{
	switch (type)
	{
	case D3DVSDT_FLOAT1: return 4;
	case D3DVSDT_FLOAT2: return 8;
	case D3DVSDT_FLOAT3: return 12;
	case D3DVSDT_FLOAT4: return 16;
	case D3DVSDT_D3DCOLOR: return 4;
	case D3DVSDT_SHORT1: return 2;
	case D3DVSDT_SHORT2: return 4;
	case D3DVSDT_SHORT3: return 6;
	case D3DVSDT_SHORT4: return 8;
	case D3DVSDT_NORMSHORT1: return 2;
	case D3DVSDT_NORMSHORT2: return 4;
	case D3DVSDT_NORMSHORT3: return 6;
	case D3DVSDT_NORMSHORT4: return 8;
	case D3DVSDT_NORMPACKED3: return 4;
	case D3DVSDT_PBYTE1: return 1;
	case D3DVSDT_PBYTE2: return 2;
	case D3DVSDT_PBYTE3: return 3;
	case D3DVSDT_PBYTE4: return 4;
	case D3DVSDT_FLOAT2H: return 12;
	default: return 0;
	}
}

static void parse_declaration(struct vertex_shader_object *object, const DWORD *declaration)
{
	unsigned long stream = 0;
	unsigned long offsets[16] = { 0 };

	for (; declaration && *declaration != D3DVSD_END(); declaration++)
	{
		DWORD token = *declaration;
		unsigned long token_type = (token & D3DVSD_TOKENTYPEMASK) >> D3DVSD_TOKENTYPESHIFT;

		switch (token_type)
		{
		case D3DVSD_TOKEN_STREAM:
			stream = token & D3DVSD_STREAMNUMBERMASK;
			break;
		case D3DVSD_TOKEN_STREAMDATA:
			if (token & D3DVSD_DATALOADTYPEMASK)
			{
				/* skip: the count is in dwords, or in bytes with bit 27 */
				unsigned long count = (token & D3DVSD_SKIPCOUNTMASK) >> D3DVSD_SKIPCOUNTSHIFT;

				offsets[stream] += (token & 0x08000000) ? count : count * 4;
			}
			else if (object->element_count < XGPU_VERTEX_ATTRIBUTE_COUNT)
			{
				struct vertex_element *element = &object->elements[object->element_count++];

				element->reg = (unsigned char)(token & D3DVSD_VERTEXREGMASK);
				element->stream = (unsigned char)stream;
				element->type = (unsigned char)((token & D3DVSD_DATATYPEMASK) >> D3DVSD_DATATYPESHIFT);
				element->bytes = (unsigned char)vertex_type_bytes(element->type);
				element->offset = (unsigned short)offsets[stream];
				offsets[stream] += element->bytes;
				if (element->type == D3DVSDT_NORMPACKED3)
					object->packed_mask |= 1UL << element->reg;
			}
			break;
		case D3DVSD_TOKEN_CONSTMEM:
			declaration += ((token & D3DVSD_CONSTCOUNTMASK) >> D3DVSD_CONSTCOUNTSHIFT) * 4;
			break;
		case D3DVSD_TOKEN_EXT:
			declaration += (token & D3DVSD_EXTCOUNTMASK) >> D3DVSD_EXTCOUNTSHIFT;
			break;
		default:
			break;
		}
	}
}

/* A DWORD cannot hold a native pointer. Bound the handle registry. */
#define VERTEX_SHADER_HANDLE_LIMIT 4096
static struct vertex_shader_object *vertex_shader_handles[VERTEX_SHADER_HANDLE_LIMIT];
static unsigned int vertex_shader_handle_count;

HRESULT WINAPI D3DDevice_CreateVertexShader(CONST DWORD *declaration, CONST DWORD *function, DWORD *handle, DWORD usage)
{
	struct vertex_shader_object *object = calloc(1, sizeof(*object));

	(void)usage;
	if (!object || vertex_shader_handle_count == VERTEX_SHADER_HANDLE_LIMIT)
	{
		free(object);
		return E_OUTOFMEMORY;
	}
	object->signature = VERTEX_SHADER_SIGNATURE;
	object->id = device.next_vertex_shader_id++;
	if (function)
	{
		/* header: program type in the low word, instruction count in the high */
		object->instruction_count = function[0] >> 16;
		if (!object->instruction_count || object->instruction_count > VERTEX_PROGRAM_SLOTS)
		{
			free(object);
			return E_INVALIDARG;
		}
		object->instructions = malloc(object->instruction_count * 4 * sizeof(DWORD));
		if (!object->instructions)
		{
			free(object);
			return E_OUTOFMEMORY;
		}
		memcpy(object->instructions, function + 1, object->instruction_count * 4 * sizeof(DWORD));
		object->program_hash = hash64(object->instructions, object->instruction_count * 4 * sizeof(DWORD));
	}
	parse_declaration(object, declaration);
	object->next_object = vertex_shader_objects;
	vertex_shader_objects = object;
	/* odd values are FVF codes; programmable shader handles are even */
	vertex_shader_handles[vertex_shader_handle_count++] = object;
	*handle = vertex_shader_handle_count * 2;
	return S_OK;
}

static struct vertex_shader_object *vertex_shader_from_handle(DWORD handle)
{
	if (!handle || (handle & 1) || handle / 2 > vertex_shader_handle_count)
		return NULL;
	return vertex_shader_handles[handle / 2 - 1];
}

void WINAPI D3DDevice_DeleteVertexShader(DWORD handle)
{
	/* programs stay cached; the object is small */
	(void)handle;
}

void WINAPI D3DDevice_SetVertexShader(DWORD handle)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	if (object)
	{
		device.vertex_shader = object;
		device.program_address = 0;
		device.program_slots[0] = object;
	}
}

void WINAPI D3DDevice_LoadVertexShader(DWORD handle, DWORD address)
{
	if (address < VERTEX_PROGRAM_SLOTS)
		device.program_slots[address] = vertex_shader_from_handle(handle);
}

void WINAPI D3DDevice_SelectVertexShader(DWORD handle, DWORD address)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	if (object)
		device.vertex_shader = object;
	if (address < VERTEX_PROGRAM_SLOTS)
		device.program_address = address;
}

void WINAPI D3DDevice_GetVertexShaderSize(DWORD handle, UINT *size)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	*size = object ? object->instruction_count : 0;
}

void WINAPI D3DDevice_SetVertexShaderConstant(INT reg, CONST void *constant_data, DWORD constant_count)
{
	long first = reg + XGPU_VERTEX_CONSTANT_BIAS;

	if (first < 0 || first >= XGPU_VERTEX_CONSTANT_COUNT)
		return;
	if (first + (long)constant_count > XGPU_VERTEX_CONSTANT_COUNT)
		constant_count = XGPU_VERTEX_CONSTANT_COUNT - first;
	constants_store((unsigned long)first, constant_data, constant_count);
}

/* the program that runs: the one loaded at the selected address, else the
current shader's own */
static struct vertex_shader_object *current_program(void)
{
	struct vertex_shader_object *program = device.program_slots[device.program_address];

	return program ? program : device.vertex_shader;
}

/* ---------- program cache */

/* size is a multiple of 4 */
static unsigned long hash_words(const void *data, unsigned long size)
{
	const DWORD *words = data;
	unsigned long hash = 2166136261UL;

	for (size /= 4; size; size--)
		hash = (hash ^ *words++) * 16777619UL;
	return hash;
}

/* writes the bytes of data, then those of more, to directory/name
(debug.gpu_dump_shaders) */
static void dump_file(const char *directory, const char *name, const void *data, unsigned long size,
	const void *more, unsigned long more_size)
{
	char path[512];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", directory, name);
	if ((file = fopen(path, "wb")) == NULL)
		return;
	fwrite(data, 1, size, file);
	if (more)
		fwrite(more, 1, more_size, file);
	fclose(file);
}

static void shader_list_vertex_made(struct vertex_shader_object *program, int variant);
static void shader_list_pixel_made(const struct nv2a_pixel_shader_key *key, gpu_shader shader);

/* compiling a map's shader list (halo_shader_list_warm) */
static BOOL shader_list_warming;

/* debug.gpu_dump_shaders: a vertex shader's source and inputs */
static void vertex_shader_dump(const struct vertex_shader_object *program, int variant, const char *source)
{
	char path[512];
	FILE *file;
	DWORD header[2];

	snprintf(path, sizeof(path), "%s/vs%03lu_%d.%s", debug_settings.dump_shaders, program->id, variant,
		shader_extension(&shader_dialect));
	if ((file = fopen(path, "w")) != NULL)
	{
		fputs(source, file);
		fclose(file);
	}
	header[0] = (DWORD)program->instruction_count;
	header[1] = (DWORD)program->shader_packed_mask[variant];
	snprintf(path, sizeof(path), "vs%03lu_%d.vsh", program->id, variant);
	dump_file(debug_settings.dump_shaders, path, header, sizeof(header),
		program->instructions, program->instruction_count * 4 * sizeof(DWORD));
}

/* debug.gpu_dump_shaders: a pixel shader's source and key */
static void fragment_shader_dump(const struct fragment_entry *entry, const char *source)
{
	char path[512];
	FILE *file;

	snprintf(path, sizeof(path), "%s/ps_%08lx.%s", debug_settings.dump_shaders, entry->hash, shader_extension(&shader_dialect));
	if ((file = fopen(path, "w")) != NULL)
	{
		fputs(source, file);
		fclose(file);
	}
	snprintf(path, sizeof(path), "ps_%08lx.key", entry->hash);
	dump_file(debug_settings.dump_shaders, path, &entry->key, sizeof(entry->key), NULL, 0);
}

/* debug.gpu_dump_shaders dumps a shader when it is compiled, except at map
load, which compiles shaders no draw may use: the dump then waits for the
shader's first draw, so the dumps name the shaders the run drew, with or
without the lists. Returns the copy to keep for that, or NULL to dump now
(or not at all). */
static char *shader_dump_pending(const char *source)
{
	return source && debug_settings.dump_shaders && shader_list_warming ? strdup(source) : NULL;
}

/* a program's shader for a variant, translated and compiled with
packed_mask the first time */
static gpu_shader vertex_shader_compile(struct vertex_shader_object *program, int variant, unsigned long packed_mask)
{
	if (!program->shader[variant])
	{
		char *source = nv2a_vertex_shader_translate(&shader_dialect, program->instructions, program->instruction_count, packed_mask);

		program->shader[variant] = source ? gpu_shader_create(GPU_SHADER_VERTEX, source) : 0;
		program->shader_packed_mask[variant] = packed_mask;
		program->dump_source[variant] = shader_dump_pending(source);
		if (source && debug_settings.dump_shaders && !program->dump_source[variant])
			vertex_shader_dump(program, variant, source);
		free(source);
		shader_list_vertex_made(program, variant);
	}
	return program->shader[variant];
}

static gpu_shader vertex_shader_get(struct vertex_shader_object *program, BOOL immediate)
{
	int variant = immediate ? 1 : 0;

	if (program->shader[variant])
	{
		/* (compiled at map load: dumped now, at its first draw) */
		if (program->dump_source[variant])
		{
			vertex_shader_dump(program, variant, program->dump_source[variant]);
			free(program->dump_source[variant]);
			program->dump_source[variant] = NULL;
		}
		return program->shader[variant];
	}
	return vertex_shader_compile(program, variant, immediate ? 0 : device.vertex_shader->packed_mask);
}

/* a pixel shader compiled at map load, at its first draw: dumped */
static gpu_shader fragment_shader_first_draw(struct fragment_entry *entry)
{
	fragment_shader_dump(entry, entry->dump_source);
	free(entry->dump_source);
	entry->dump_source = NULL;
	return entry->shader;
}

typedef char pixel_shader_key_size_assert[sizeof(struct nv2a_pixel_shader_key) % 4 == 0 ? 1 : -1];

static gpu_shader fragment_shader_get(const struct nv2a_pixel_shader_key *key)
{
	/* consecutive draws mostly use one of a few pixel shaders (an object's
	parts take turns) */
#define RECENT_FRAGMENT_COUNT 4
	static struct fragment_entry *recent[RECENT_FRAGMENT_COUNT];
	static unsigned long recent_next;
	unsigned long hash, index;
	struct fragment_entry **bucket;
	struct fragment_entry *entry;
	char *source;

	for (index = 0; index < RECENT_FRAGMENT_COUNT; index++)
	{
		if (recent[index] && !memcmp(&recent[index]->key, key, sizeof(*key)))
			return recent[index]->dump_source ? fragment_shader_first_draw(recent[index]) : recent[index]->shader;
	}
	hash = hash_words(key, sizeof(*key));
	bucket = &fragment_buckets[hash % FRAGMENT_BUCKETS];
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->hash == hash && !memcmp(&entry->key, key, sizeof(*key)))
		{
			recent[recent_next++ % RECENT_FRAGMENT_COUNT] = entry;
			return entry->dump_source ? fragment_shader_first_draw(entry) : entry->shader;
		}
	}
	entry = calloc(1, sizeof(*entry));
	entry->hash = hash;
	entry->key = *key;
	source = nv2a_pixel_shader_translate(&shader_dialect, key);
	entry->shader = source ? gpu_shader_create(GPU_SHADER_PIXEL, source) : 0;
	entry->dump_source = shader_dump_pending(source);
	if (source && debug_settings.dump_shaders && !entry->dump_source)
		fragment_shader_dump(entry, source);
	free(source);
	entry->next = *bucket;
	*bucket = entry;
	recent[recent_next++ % RECENT_FRAGMENT_COUNT] = entry;
	shader_list_pixel_made(key, entry->shader);
	return entry->shader;
}


/* ---------- shader lists: compiling at map load

The Metal backend compiles a shader when the front end first asks for it and
a pipeline when a draw first needs it, and a frame waits for both: tens of
milliseconds each on a cold start, seconds at a map's first frames. So each
map carries a list (port/shader-lists/MAP.txt, in the app as
shader-lists/MAP.txt) of the vertex shaders, pixel shaders and pipelines its
draws made in earlier runs, and halo_shader_list_warm compiles all of them
after the map loads, before its first frame, under the loading screen.
Lines, one each:

  vs PROGRAM VARIANT MASK      a vertex program (the 64-bit FNV-1a of its
                               instructions, hex), 0 for streams or 1 for
                               immediate mode, and the packed-attribute mask
                               it was translated with (hex)
  ps KEY HEX                   a pixel shader: its key's 64-bit hash and the
                               key's bytes (struct nv2a_pixel_shader_key)
  pipeline VS PS BLEND SOURCE DESTINATION OPERATION MASK EXACT DEPTH KINDS ALPHA_SOURCE ALPHA_DESTINATION
                               a pipeline (gpu_pipeline_description): VS is
                               PROGRAM.VARIANT.MASK, PS a KEY, the rest
                               decimal, KINDS the 16 attribute kinds in hex;
                               a line without the alpha factors (from
                               before they were listed) blends alpha as
                               color

Anything still made while drawing, by the Metal backend, is logged once,
with its line, and appended to FOLDER/MAP.txt, FOLDER being
debug.shader_list_record (shader-lists-missed in the data folder unless set;
"" for none), each line once however many sessions make it: the record of
what the lists missed, gathered from devices and runners for
tools/shader_lists.py to merge into the repository's lists. */

/* each shader handle's name in the lists, by handle */
struct shader_list_name
{
	unsigned char kind;      /* 0 none, 1 vertex, 2 pixel */
	unsigned char variant;
	unsigned long packed_mask;
	uint64_t hash;
};

static struct
{
	char map[64];
	struct shader_list_name *names;
	unsigned long name_count;
	const char *record_directory;
	int record_checked;
	/* the hashes of the lines in record_map's file, so none goes in twice */
	char record_map[64];
	uint64_t *recorded;
	unsigned long recorded_count, recorded_capacity;
} shader_list;

static struct shader_list_name *shader_list_name(gpu_shader shader)
{
	if (!shader)
		return NULL;
	if (shader >= shader_list.name_count)
	{
		unsigned long count = shader_list.name_count ? shader_list.name_count : 1024;

		while (count <= shader)
			count *= 2;
		shader_list.names = realloc(shader_list.names, count * sizeof(*shader_list.names));
		memset(shader_list.names + shader_list.name_count, 0, (count - shader_list.name_count) * sizeof(*shader_list.names));
		shader_list.name_count = count;
	}
	return &shader_list.names[shader];
}

static BOOL shader_list_recorded(uint64_t hash)
{
	unsigned long index;

	for (index = 0; index < shader_list.recorded_count; index++)
	{
		if (shader_list.recorded[index] == hash)
			return TRUE;
	}
	return FALSE;
}

static void shader_list_recorded_add(uint64_t hash)
{
	if (shader_list.recorded_count == shader_list.recorded_capacity)
	{
		unsigned long capacity = shader_list.recorded_capacity ? 2 * shader_list.recorded_capacity : 256;
		uint64_t *grown = realloc(shader_list.recorded, capacity * sizeof(*grown));

		if (!grown)
			return;
		shader_list.recorded = grown;
		shader_list.recorded_capacity = capacity;
	}
	shader_list.recorded[shader_list.recorded_count++] = hash;
}

/* the record file's lines for the map in hand, read the first time one of
its lines is recorded */
static void shader_list_recorded_load(const char *path)
{
	char line[1024];
	FILE *file;

	if (!strcmp(shader_list.record_map, shader_list.map))
		return;
	snprintf(shader_list.record_map, sizeof(shader_list.record_map), "%s", shader_list.map);
	shader_list.recorded_count = 0;
	if ((file = fopen(path, "r")) == NULL)
		return;
	while (fgets(line, sizeof(line), file))
	{
		line[strcspn(line, "\r\n")] = 0;
		if (line[0])
			shader_list_recorded_add(hash64(line, strlen(line)));
	}
	fclose(file);
}

/* a line for the map's list, made while drawing: logged, and recorded
under debug.shader_list_record (MSL backends only: the lists are Metal's) */
static void shader_list_made(const char *line)
{
	char path[512];
	uint64_t hash;
	FILE *file;

	if (shader_list_warming || !shader_list.map[0] || !shader_dialect.msl)
		return;
	platform_log("shader list %s: made while drawing: %s", shader_list.map, line);
	if (!shader_list.record_checked)
	{
		shader_list.record_directory = *config_string("debug.shader_list_record") ?
			config_string("debug.shader_list_record") : NULL;
		shader_list.record_checked = 1;
		if (shader_list.record_directory)
			posix_make_directory(shader_list.record_directory);
	}
	if (!shader_list.record_directory)
		return;
	snprintf(path, sizeof(path), "%s/%s.txt", shader_list.record_directory, shader_list.map);
	shader_list_recorded_load(path);
	hash = hash64(line, strlen(line));
	if (shader_list_recorded(hash))
		return;
	if ((file = fopen(path, "a")) != NULL)
	{
		fprintf(file, "%s\n", line);
		fclose(file);
		shader_list_recorded_add(hash);
	}
}

static void shader_list_vertex_made(struct vertex_shader_object *program, int variant)
{
	struct shader_list_name *name = shader_list_name(program->shader[variant]);
	char line[96];

	if (!name)
		return;
	name->kind = 1;
	name->variant = (unsigned char)variant;
	name->packed_mask = program->shader_packed_mask[variant];
	name->hash = program->program_hash;
	snprintf(line, sizeof(line), "vs %016llx %d %lx", (unsigned long long)name->hash, variant, name->packed_mask);
	shader_list_made(line);
}

static void shader_list_pixel_made(const struct nv2a_pixel_shader_key *key, gpu_shader shader)
{
	struct shader_list_name *name = shader_list_name(shader);
	char line[64 + 2 * sizeof(*key)];
	const unsigned char *bytes = (const unsigned char *)key;
	unsigned long index, length;

	if (!name)
		return;
	name->kind = 2;
	name->hash = hash64(key, sizeof(*key));
	length = (unsigned long)snprintf(line, sizeof(line), "ps %016llx ", (unsigned long long)name->hash);
	for (index = 0; index < sizeof(*key); index++)
		length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%02x", bytes[index]);
	shader_list_made(line);
}

/* the pipelines draws made since the last frame, as list lines (called at
Present) */
static void shader_list_take_pipelines(void)
{
	struct gpu_pipeline_description built;

	while (gpu_pipeline_built_take(&built))
	{
		struct shader_list_name *vertex = shader_list_name(built.vertex_shader);
		struct shader_list_name *pixel = shader_list_name(built.pixel_shader);
		char line[192], kinds[2 * GPU_ATTRIBUTE_COUNT + 1];
		int index;

		if (!vertex || !pixel || vertex->kind != 1 || pixel->kind != 2)
			continue;
		for (index = 0; index < GPU_ATTRIBUTE_COUNT; index++)
			snprintf(kinds + 2 * index, 3, "%02x", built.attribute_kinds[index]);
		snprintf(line, sizeof(line), "pipeline %016llx.%u.%lx %016llx %u %u %u %u %u %u %u %s %u %u",
			(unsigned long long)vertex->hash, vertex->variant, vertex->packed_mask, (unsigned long long)pixel->hash,
			built.blend, built.source, built.destination, built.operation, built.write_mask, built.exact_borders,
			built.depth, kinds, built.alpha_source, built.alpha_destination);
		shader_list_made(line);
	}
}

static int hex_digit(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* count bytes from 2 * count hex digits; FALSE, with bytes undefined, if
any isn't one (a damaged line, which is skipped) */
static BOOL hex_bytes(const char *hex, unsigned char *bytes, unsigned long count)
{
	unsigned long index;

	for (index = 0; index < count; index++, hex += 2)
	{
		int high = hex_digit(hex[0]), low = high < 0 ? -1 : hex_digit(hex[1]);

		if (low < 0)
			return FALSE;
		bytes[index] = (unsigned char)(high * 16 + low);
	}
	return TRUE;
}

/* the pixel shader a list's KEY names, among those made so far */
static gpu_shader shader_list_pixel(uint64_t hash)
{
	unsigned long index;

	for (index = 1; index < shader_list.name_count; index++)
	{
		if (shader_list.names[index].kind == 2 && shader_list.names[index].hash == hash)
			return (gpu_shader)index;
	}
	return 0;
}

/* the vertex program objects a list's line names, the next one following
`after` (NULL for the first): those whose instructions hash to hash and, for a vs line
(compiled FALSE), whose draws would translate that variant with mask (the
object's own declaration's mask for streams, 0 for immediate mode), or, for
a pipeline line, whose variant was compiled with mask. The game can create a
program more than once with the same instructions (one object for each
declaration), and a line stands for all of them; an object whose shader
would be translated with another mask is never warmed with this one. */
static struct vertex_shader_object *shader_list_program(struct vertex_shader_object *after, uint64_t hash, int variant,
	unsigned long mask, BOOL compiled)
{
	struct vertex_shader_object *object;

	for (object = after ? after->next_object : vertex_shader_objects; object; object = object->next_object)
	{
		if (!object->instructions || object->program_hash != hash)
			continue;
		if (compiled ? object->shader[variant] && object->shader_packed_mask[variant] == mask :
			(variant ? mask == 0 : object->packed_mask == mask))
			return object;
	}
	return NULL;
}

void halo_shader_list_warm(char const *map_name)
{
	const char *base = map_name;
	const char *cursor;
	char *text, *line, *next;
	uint32_t size;
	unsigned long vertex_count = 0, pixel_count = 0, pipeline_count = 0, missing = 0;
	double started;
	int pass;

	for (cursor = map_name; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			base = cursor + 1;
	}
	snprintf(shader_list.map, sizeof(shader_list.map), "%s", base);
	if (!device.gl_ready || !config_boolean("debug.shader_list_warm") ||
		!(size = gpu_warm_list_read(shader_list.map, NULL, 0)))
		return;
	text = malloc(size + 1);
	if (!text || gpu_warm_list_read(shader_list.map, text, size) != size)
	{
		free(text);
		return;
	}
	text[size] = 0;
	started = halo_frame_trace_milliseconds();
	shader_list_warming = TRUE;
	gpu_warm_begin();
	/* the shaders, then the pipelines, which name them */
	for (pass = 0; pass < 2; pass++)
	{
		for (line = text; line && *line; line = next)
		{
			char kind[16], first[600], second[64];
			unsigned int values[9];

			next = strchr(line, '\n');
			if (next)
				*next++ = 0;
			if (sscanf(line, "%15s", kind) != 1 || kind[0] == '#')
				continue;
			if (pass == 0 && !strcmp(kind, "vs"))
			{
				unsigned long long hash;
				int variant;
				unsigned long mask;
				struct vertex_shader_object *program = NULL;
				BOOL used = FALSE;

				if (sscanf(line, "vs %llx %d %lx", &hash, &variant, &mask) != 3 || variant < 0 || variant > 1)
				{
					missing++;
					continue;
				}
				while ((program = shader_list_program(program, (uint64_t)hash, variant, mask, FALSE)))
				{
					if (!program->shader[variant])
					{
						vertex_shader_compile(program, variant, mask);
						vertex_count++;
					}
					used = TRUE;
				}
				missing += !used;
			}
			else if (pass == 0 && !strcmp(kind, "ps"))
			{
				struct nv2a_pixel_shader_key key;
				unsigned char *bytes = (unsigned char *)&key;

				if (sscanf(line, "ps %63s %599s", second, first) != 2 || strlen(first) != 2 * sizeof(key) ||
					!hex_bytes(first, bytes, sizeof(key)))
				{
					missing++;
					continue;
				}
				fragment_shader_get(&key);
				pixel_count++;
			}
			else if (pass == 1 && !strcmp(kind, "pipeline"))
			{
				struct gpu_pipeline_description description;
				unsigned long long vertex_hash, pixel_hash;
				unsigned int variant;
				unsigned long mask;
				char kinds[64];
				struct vertex_shader_object *program = NULL;
				BOOL used = FALSE;
				int fields;

				memset(&description, 0, sizeof(description));
				fields = sscanf(line, "pipeline %llx.%u.%lx %llx %u %u %u %u %u %u %u %63s %u %u", &vertex_hash, &variant,
					&mask, &pixel_hash, &values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6],
					kinds, &values[7], &values[8]);
				/* (a line from before the alpha factors were listed blends
				alpha as color) */
				if (fields == 12)
				{
					values[7] = values[1];
					values[8] = values[2];
				}
				if ((fields != 12 && fields != 14) || variant > 1 || strlen(kinds) != 2 * GPU_ATTRIBUTE_COUNT ||
					!hex_bytes(kinds, description.attribute_kinds, GPU_ATTRIBUTE_COUNT) ||
					!(description.pixel_shader = shader_list_pixel((uint64_t)pixel_hash)))
				{
					missing++;
					continue;
				}
				description.blend = (uint8_t)values[0];
				description.source = (uint8_t)values[1];
				description.destination = (uint8_t)values[2];
				description.operation = (uint8_t)values[3];
				description.write_mask = (uint8_t)values[4];
				description.exact_borders = (uint8_t)values[5];
				description.depth = (uint8_t)values[6];
				description.alpha_source = (uint8_t)values[7];
				description.alpha_destination = (uint8_t)values[8];
				/* (one for each object compiled with the line's variant and mask) */
				while ((program = shader_list_program(program, (uint64_t)vertex_hash, (int)variant, mask, TRUE)))
				{
					description.vertex_shader = program->shader[variant];
					gpu_pipeline_warm(&description);
					pipeline_count++;
					used = TRUE;
				}
				missing += !used;
			}
		}
		/* (the first pass cut the text into lines; the second reads them) */
		if (pass == 0)
		{
			for (line = text; line < text + size; line++)
			{
				if (!*line)
					*line = '\n';
			}
		}
	}
	gpu_warm_end();
	shader_list_warming = FALSE;
	free(text);
	platform_log("shader list %s: %lu vertex shaders, %lu pixel shaders and %lu pipelines compiled at load in %.0f ms; "
		"%lu lines not used", shader_list.map, vertex_count, pixel_count, pipeline_count,
		halo_frame_trace_milliseconds() - started, missing);
}

/* ---------- per-draw state */

static unsigned long stage_texture_mode(int stage)
{
	return (D3D__RenderState[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f;
}

static uint8_t gpu_filter(DWORD filter)
{
	switch (filter)
	{
	case D3DTEXF_POINT: return GPU_FILTER_POINT;
	case D3DTEXF_LINEAR: return GPU_FILTER_LINEAR;
	case D3DTEXF_ANISOTROPIC: return GPU_FILTER_ANISOTROPIC;
	case D3DTEXF_QUINCUNX: return GPU_FILTER_QUINCUNX;
	case D3DTEXF_GAUSSIANCUBIC: return GPU_FILTER_GAUSSIAN_CUBIC;
	default: return GPU_FILTER_NONE;
	}
}

static uint8_t gpu_address(DWORD mode)
{
	switch (mode)
	{
	case D3DTADDRESS_MIRROR: return GPU_ADDRESS_MIRROR;
	case D3DTADDRESS_CLAMP: return GPU_ADDRESS_CLAMP;
	case D3DTADDRESS_BORDER: return GPU_ADDRESS_BORDER;
	case D3DTADDRESS_CLAMPTOEDGE: return GPU_ADDRESS_CLAMP_TO_EDGE;
	default: return GPU_ADDRESS_WRAP;
	}
}

/* the sampler state of a stage; without mipmaps the mip filter is none */
/* hires: a high-res HUD texture (hud_hires.h), drawn smaller than it is, so
filtered and from its mip levels whatever the game asks: the HUD's meters are
point sampled for one player, to keep the Xbox bitmaps' texels sharp */
static void sampler_state_fill(int stage, BOOL mipmapped, BOOL hires, struct gpu_sampler_state *sampler)
{
	DWORD *state = D3D__TextureState[stage];

	memset(sampler, 0, sizeof(*sampler));
	sampler->min_filter = hires ? GPU_FILTER_LINEAR : gpu_filter(state[D3DTSS_MINFILTER]);
	sampler->mip_filter = hires ? GPU_FILTER_LINEAR : mipmapped ? gpu_filter(state[D3DTSS_MIPFILTER]) : GPU_FILTER_NONE;
	sampler->mag_filter = hires ? GPU_FILTER_LINEAR : gpu_filter(state[D3DTSS_MAGFILTER]);
	sampler->address_u = gpu_address(state[D3DTSS_ADDRESSU]);
	sampler->address_v = gpu_address(state[D3DTSS_ADDRESSV]);
	sampler->address_w = gpu_address(state[D3DTSS_ADDRESSW]);
	sampler->max_mip_level = hires ? 0 : (uint32_t)state[D3DTSS_MAXMIPLEVEL];
	sampler->max_anisotropy = (uint32_t)state[D3DTSS_MAXANISOTROPY];
	sampler->lod_bias = hires ? 0.0f : dword_to_float(state[D3DTSS_MIPMAPLODBIAS]);
	sampler->border_color = (uint32_t)state[D3DTSS_BORDERCOLOR];
	/* display.anisotropic_filtering: the game never asks for anisotropic
	filtering (the Xbox ran 1x), so every linear, mipmapped stage gets it.
	Point-sampled and unmipmapped stages (the HUD, text, render targets)
	stay as they are. */
	{
		static long anisotropy = -1;

		if (anisotropy < 0)
		{
			anisotropy = config_integer("display.anisotropic_filtering");
			anisotropy = anisotropy < 1 ? 1 : anisotropy > 16 ? 16 : anisotropy;
		}
		if (anisotropy > 1 && sampler->min_filter == GPU_FILTER_LINEAR && sampler->mip_filter != GPU_FILTER_NONE)
		{
			sampler->min_filter = GPU_FILTER_ANISOTROPIC;
			sampler->max_anisotropy = (uint32_t)anisotropy;
		}
	}
}

/* ---------- render targets sampled with their mip chain

The game renders some textures one mip level at a time, each level being a
surface of its own (the water's ripple map). Sampling such a texture needs
every level in one texture, so the levels' render targets are copied into a
mipmapped composite. Each draw of the water binds it, some maps (a30) more
than once a frame, so the copy (and the mipmaps of the levels the game did not
render) is redone only once a level's target has been drawn into since the
last one. */

#define MIP_COMPOSITE_LEVELS 16

struct mip_composite
{
	struct mip_composite *next;
	unsigned long data, width, height, levels;
	gpu_texture texture;
	/* the levels last copied, and each one's target's texture and written
	serial then */
	unsigned long rendered_levels;
	gpu_texture level_sources[MIP_COMPOSITE_LEVELS];
	unsigned long level_written[MIP_COMPOSITE_LEVELS];
};

static struct mip_composite *mip_composites;

static gpu_texture mip_composite_get(const struct xgpu_texture_description *description, unsigned long data)
{
	struct mip_composite *composite;
	struct xgpu_render_target *targets[MIP_COMPOSITE_LEVELS];
	unsigned long level, rendered_levels = 0;
	BOOL changed;

	for (composite = mip_composites; composite; composite = composite->next)
	{
		if (composite->data == data && composite->width == description->width &&
			composite->height == description->height && composite->levels == description->levels)
		{
			break;
		}
	}
	if (!composite)
	{
		composite = calloc(1, sizeof(*composite));
		composite->data = data;
		composite->width = description->width;
		composite->height = description->height;
		composite->levels = description->levels;
		{
			struct gpu_texture_description texture = { 0 };

			texture.type = GPU_TEXTURE_2D;
			texture.format = GPU_FORMAT_BGRA8;
			texture.usage = GPU_USAGE_RENDER_TARGET;
			texture.width = (uint32_t)description->width;
			texture.height = (uint32_t)description->height;
			texture.depth = 1;
			texture.levels = (uint32_t)description->levels;
			composite->texture = gpu_texture_create(&texture);
		}
		composite->rendered_levels = ~0UL;
		composite->next = mip_composites;
		mip_composites = composite;
	}
	for (level = 0; level < description->levels && level < MIP_COMPOSITE_LEVELS; level++)
	{
		unsigned long width = description->width >> level ? description->width >> level : 1;
		unsigned long height = description->height >> level ? description->height >> level : 1;
		struct xgpu_render_target *target =
			xgpu_render_target_find(data + xgpu_texture_level_offset(description, level));

		if (!target || target->width != width || target->height != height ||
			target->gl_width != width || target->gl_height != height)
			break;
		targets[level] = target;
		rendered_levels++;
	}
	changed = rendered_levels != composite->rendered_levels;
	for (level = 0; level < rendered_levels && !changed; level++)
	{
		changed = targets[level]->texture != composite->level_sources[level] ||
			targets[level]->written != composite->level_written[level];
	}
	if (!changed)
		return composite->texture;
	composite->rendered_levels = rendered_levels;
	for (level = 0; level < rendered_levels; level++)
	{
		composite->level_sources[level] = targets[level]->texture;
		composite->level_written[level] = targets[level]->written;
		gpu_texture_copy_level(targets[level]->texture, composite->texture, (uint32_t)level);
	}
	/* levels the game did not render come from the ones it did */
	if (rendered_levels < description->levels)
		gpu_texture_generate_mipmaps(composite->texture, rendered_levels ? (uint32_t)rendered_levels - 1 : 0);
	return composite->texture;
}

/* fills the packet's stages from the texture stages, uploading textures and
composing mips as it goes, and the key's sampler types */
static void stages_fill(struct nv2a_pixel_shader_key *key, float texture_scale[4][4], struct gpu_stage stages[4])
{
	int stage;

	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		D3DBaseTexture *texture = device.textures[stage];
		unsigned long mode = stage_texture_mode(stage);
		struct gpu_stage *packet_stage = &stages[stage];

		texture_scale[stage][0] = texture_scale[stage][1] = 1.0f;
		texture_scale[stage][2] = texture_scale[stage][3] = 1.0f;
		memset(packet_stage, 0, sizeof(*packet_stage));
		if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
		{
			key->sampler_type[stage] = mode == 0x11 ? _xgpu_sampler_2d : _xgpu_sampler_none;
			continue;
		}
		{
			struct xgpu_render_target *target = xgpu_render_target_find(texture->Data);
			struct xgpu_texture_description description = { 0 };
			uint32_t type;
			gpu_texture handle;

			if (target)
			{
				xgpu_texture_describe(texture->Format, texture->Size, &description);
				handle = target->texture;
				type = GPU_TEXTURE_2D;
				if (description.linear)
				{
					texture_scale[stage][0] = 1.0f / (float)target->width;
					texture_scale[stage][1] = 1.0f / (float)target->height;
				}
				if (!description.linear && !description.cube_map && description.levels > 1 &&
					target->width == description.width && target->height == description.height)
					handle = mip_composite_get(&description, texture->Data);
				else
					description.levels = 1;
			}
			else
			{
				const D3DCOLOR *palette = device.palettes[stage] && device.palettes[stage]->Data ?
					(const D3DCOLOR *)PLATFORM_PHYSICAL_TO_VIRTUAL(device.palettes[stage]->Data) : NULL;

				handle = xgpu_texture_get((const DWORD *)texture, palette, &type, &description);
				if (description.linear)
				{
					texture_scale[stage][0] = 1.0f / (float)description.width;
					texture_scale[stage][1] = 1.0f / (float)description.height;
				}
			}
			packet_stage->texture = handle;
			packet_stage->type = (uint8_t)type;
			sampler_state_fill(stage, description.levels > 1, description.hires, &packet_stage->sampler);
			if (stage == 0)
				key->coverage_alpha = (description.hires_coverage ? XGPU_COVERAGE_ALPHA : 0) |
					(description.hires_point_threshold ? XGPU_COVERAGE_POINT_THRESHOLD : 0);
			key->sampler_type[stage] = type == GPU_TEXTURE_CUBE ? _xgpu_sampler_cube :
				type == GPU_TEXTURE_3D ? _xgpu_sampler_3d : _xgpu_sampler_2d;
			/* a foveated eye's screen-sized target: its texels are in the
			eye's rate map's layout, so the lookup goes through the map
			(texture_scale still takes the coordinates to the screen's) */
			if (target && target->foveated_eye && handle == target->texture)
				key->sampler_type[stage] = _xgpu_sampler_2d_foveated;
			foveation_audit_stage(stage, target, key->sampler_type[stage]);
		}
	}
}

/* ---------- raster state: D3D render states as the draw packet's structs */

static uint8_t gpu_compare(DWORD function)
{
	switch (function)
	{
	case D3DCMP_LESS: return GPU_COMPARE_LESS;
	case D3DCMP_EQUAL: return GPU_COMPARE_EQUAL;
	case D3DCMP_LESSEQUAL: return GPU_COMPARE_LESS_EQUAL;
	case D3DCMP_GREATER: return GPU_COMPARE_GREATER;
	case D3DCMP_NOTEQUAL: return GPU_COMPARE_NOT_EQUAL;
	case D3DCMP_GREATEREQUAL: return GPU_COMPARE_GREATER_EQUAL;
	case D3DCMP_ALWAYS: return GPU_COMPARE_ALWAYS;
	/* D3DCMP_NEVER, and 0 (an unset render state) */
	default: return GPU_COMPARE_NEVER;
	}
}

static uint8_t gpu_stencil_operation(DWORD operation)
{
	switch (operation)
	{
	case D3DSTENCILOP_KEEP: return GPU_STENCIL_KEEP;
	case D3DSTENCILOP_REPLACE: return GPU_STENCIL_REPLACE;
	case D3DSTENCILOP_INCRSAT: return GPU_STENCIL_INCREMENT_CLAMP;
	case D3DSTENCILOP_DECRSAT: return GPU_STENCIL_DECREMENT_CLAMP;
	case D3DSTENCILOP_INVERT: return GPU_STENCIL_INVERT;
	case D3DSTENCILOP_INCR: return GPU_STENCIL_INCREMENT_WRAP;
	case D3DSTENCILOP_DECR: return GPU_STENCIL_DECREMENT_WRAP;
	/* D3DSTENCILOP_ZERO is 0 */
	default: return GPU_STENCIL_ZERO;
	}
}

static uint8_t gpu_blend_factor(DWORD factor)
{
	switch (factor)
	{
	case D3DBLEND_ONE: return GPU_BLEND_ONE;
	case D3DBLEND_SRCCOLOR: return GPU_BLEND_SOURCE_COLOR;
	case D3DBLEND_INVSRCCOLOR: return GPU_BLEND_ONE_MINUS_SOURCE_COLOR;
	case D3DBLEND_SRCALPHA: return GPU_BLEND_SOURCE_ALPHA;
	case D3DBLEND_INVSRCALPHA: return GPU_BLEND_ONE_MINUS_SOURCE_ALPHA;
	case D3DBLEND_DESTALPHA: return GPU_BLEND_DESTINATION_ALPHA;
	case D3DBLEND_INVDESTALPHA: return GPU_BLEND_ONE_MINUS_DESTINATION_ALPHA;
	case D3DBLEND_DESTCOLOR: return GPU_BLEND_DESTINATION_COLOR;
	case D3DBLEND_INVDESTCOLOR: return GPU_BLEND_ONE_MINUS_DESTINATION_COLOR;
	case D3DBLEND_SRCALPHASAT: return GPU_BLEND_SOURCE_ALPHA_SATURATE;
	case D3DBLEND_CONSTANTCOLOR: return GPU_BLEND_CONSTANT_COLOR;
	case D3DBLEND_INVCONSTANTCOLOR: return GPU_BLEND_ONE_MINUS_CONSTANT_COLOR;
	case D3DBLEND_CONSTANTALPHA: return GPU_BLEND_CONSTANT_ALPHA;
	case D3DBLEND_INVCONSTANTALPHA: return GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA;
	/* D3DBLEND_ZERO is 0 */
	default: return GPU_BLEND_ZERO;
	}
}

static uint8_t gpu_blend_operation(DWORD operation)
{
	switch (operation)
	{
	case D3DBLENDOP_SUBTRACT: return GPU_BLEND_OP_SUBTRACT;
	case D3DBLENDOP_REVSUBTRACT:
	case D3DBLENDOP_REVSUBTRACTSIGNED: return GPU_BLEND_OP_REVERSE_SUBTRACT;
	case D3DBLENDOP_MIN: return GPU_BLEND_OP_MIN;
	case D3DBLENDOP_MAX: return GPU_BLEND_OP_MAX;
	/* D3DBLENDOP_ADD, and ADDSIGNED (the NV2A's signed add, whose bias is
	dropped, as before) */
	default: return GPU_BLEND_OP_ADD;
	}
}

/* draws into stereo's HUD layer since the last debug.gpu_stats line: those
whose transmittance is exact, and those whose blend has none (the picture
under them is left as it was) */
static unsigned long hud_layer_exact, hud_layer_inexact;

/* Stereo's HUD layer (render.c's HUD pass) is drawn once and put over each
eye's picture by the presenters, so it holds what the game draws over the
picture: premultiplied color, and in alpha the picture's transmittance, how
much of the picture still shows (1 where nothing drew). The presenters
composite rgb + picture * alpha, and the layer clears to color 0, alpha 1
(D3DDevice_Clear).

Mono blends each draw into the picture: P' = src * S + P * D, for the
operation's sign. With the layer standing for C + P * T, the same draw makes
C' = src * S + C * D (the game's own blend, on the color) and T' = T * D,
exact for ADD whenever D is the same for every channel and S doesn't read
the destination: alpha then blends by ZERO and D. Blending off is D = ZERO.
That covers the HUD's alpha-blended draws (INVSRCALPHA), its additive ones
(ONE: the picture still shows) and the meters (SRCALPHA, hud_hires.h). The
game's own alpha writes there (scratch, in mono) are replaced. Other blends
(a destination factor per channel; SUBTRACT and REVERSE_SUBTRACT, which the
layer's color, starting at 0, clamps away where mono would subtract from
the picture; MIN and MAX) keep the game's write mask: they change the color
alone, as before, and debug.gpu_stats counts them. The
port's screen flash in the layer reads the transmittance itself
(rasterizer_xbox_screen_effect.c, D3DBLEND_INVDESTALPHA) and writes color
only: it is left alone. Mono never draws here */
static void hud_layer_blend(struct gpu_blend_state *blend)
{
	uint8_t destination = blend->enable ? blend->destination : GPU_BLEND_ZERO;
	uint8_t source = blend->source;
	BOOL source_reads_destination = blend->enable && (source == GPU_BLEND_DESTINATION_ALPHA ||
		source == GPU_BLEND_ONE_MINUS_DESTINATION_ALPHA || source == GPU_BLEND_DESTINATION_COLOR ||
		source == GPU_BLEND_ONE_MINUS_DESTINATION_COLOR || source == GPU_BLEND_SOURCE_ALPHA_SATURATE);
	BOOL scalar = destination == GPU_BLEND_ZERO || destination == GPU_BLEND_ONE ||
		destination == GPU_BLEND_SOURCE_ALPHA || destination == GPU_BLEND_ONE_MINUS_SOURCE_ALPHA ||
		destination == GPU_BLEND_CONSTANT_ALPHA || destination == GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA;
	/* only ADD: the layer's color starts at 0 and can't go below it, so a
	subtraction (SUBTRACT, REVERSE_SUBTRACT) is clamped away where little has
	drawn, while mono subtracts from the picture; MIN and MAX take no
	factors */
	BOOL operation = !blend->enable || blend->operation == GPU_BLEND_OP_ADD;

	/* a draw that writes no color (depth or stencil only) leaves it */
	if (!(blend->color_write_mask & 7))
		return;
	if (source_reads_destination || !scalar || !operation)
	{
		/* (the flash reads the transmittance by design: not counted) */
		if (source != GPU_BLEND_ONE_MINUS_DESTINATION_ALPHA)
			hud_layer_inexact++;
		return;
	}
	if (!blend->enable)
	{
		blend->enable = 1;
		blend->source = GPU_BLEND_ONE;
		blend->destination = GPU_BLEND_ZERO;
		blend->operation = GPU_BLEND_OP_ADD;
	}
	blend->alpha_separate = 1;
	blend->alpha_source = GPU_BLEND_ZERO;
	blend->alpha_destination = destination;
	blend->color_write_mask |= 8;
	hud_layer_exact++;
}

/* fills the packet's raster state from the render states and the viewport */
static void raster_state_fill(BOOL has_depth, struct gpu_viewport *viewport, struct gpu_rect *scissor,
	struct gpu_depth_stencil_state *depth_stencil, struct gpu_blend_state *blend, struct gpu_raster_state *raster)
{
	DWORD *rs = D3D__RenderState;
	DWORD write = rs[D3DRS_COLORWRITEENABLE];

	memset(viewport, 0, sizeof(*viewport));
	memset(scissor, 0, sizeof(*scissor));
	memset(depth_stencil, 0, sizeof(*depth_stencil));
	memset(blend, 0, sizeof(*blend));
	memset(raster, 0, sizeof(*raster));

	viewport->x = target_pixel((float)device.viewport.X, 0);
	viewport->y = target_pixel((float)device.viewport.Y, 1);
	viewport->width = target_pixel((float)(device.viewport.X + device.viewport.Width), 0) - viewport->x;
	viewport->height = target_pixel((float)(device.viewport.Y + device.viewport.Height), 1) - viewport->y;
	viewport->min_z = device.viewport.MinZ;
	viewport->max_z = device.viewport.MaxZ;
	/* the game never issues a scissor rectangle, and the NV2A scissor register
	defaults to the viewport, so fragment clipping follows the viewport: this is
	what keeps a split-screen window's geometry from bleeding across the divider */
	scissor->x = viewport->x;
	scissor->y = viewport->y;
	scissor->width = viewport->width;
	scissor->height = viewport->height;
	/* port: the zoomed view's HUD elements, laid out on the game's screen,
	are shown over the view's shape (halo_stereo_zoom_fit): the viewport
	narrows or widens across about its center so they keep their shape, fit
	to the view's height as the scope mask is. The scissor stays the
	viewport's own */
	if (target_zoom_layer && halo_stereo_zoom_overlay_on())
	{
		float fit = halo_stereo_zoom_fit((float)halo_screen_width() / (float)SCREEN_HEIGHT);
		float center = (float)viewport->x + (float)viewport->width / 2.0f;
		float width = (float)viewport->width * fit;

		viewport->x = (int32_t)lroundf(center - width / 2.0f);
		viewport->width = (int32_t)lroundf(width);
	}

	depth_stencil->depth_test = has_depth && rs[D3DRS_ZENABLE];
	depth_stencil->depth_write = depth_stencil->depth_test && rs[D3DRS_ZWRITEENABLE];
	depth_stencil->depth_function = gpu_compare(rs[D3DRS_ZFUNC]);
	depth_stencil->stencil_test = has_depth && rs[D3DRS_STENCILENABLE];
	depth_stencil->stencil_function = gpu_compare(rs[D3DRS_STENCILFUNC]);
	depth_stencil->stencil_fail = gpu_stencil_operation(rs[D3DRS_STENCILFAIL]);
	depth_stencil->stencil_depth_fail = gpu_stencil_operation(rs[D3DRS_STENCILZFAIL]);
	depth_stencil->stencil_pass = gpu_stencil_operation(rs[D3DRS_STENCILPASS]);
	depth_stencil->stencil_reference = (uint32_t)rs[D3DRS_STENCILREF];
	depth_stencil->stencil_read_mask = (uint32_t)rs[D3DRS_STENCILMASK];
	depth_stencil->stencil_write_mask = (uint32_t)rs[D3DRS_STENCILWRITEMASK];

	blend->enable = rs[D3DRS_ALPHABLENDENABLE] != 0;
	blend->source = gpu_blend_factor(rs[D3DRS_SRCBLEND]);
	blend->destination = gpu_blend_factor(rs[D3DRS_DESTBLEND]);
	blend->operation = gpu_blend_operation(rs[D3DRS_BLENDOP]);
	blend->color = (uint32_t)rs[D3DRS_BLENDCOLOR];
	blend->color_write_mask = (uint8_t)(((write & D3DCOLORWRITEENABLE_RED) ? 1 : 0) |
		((write & D3DCOLORWRITEENABLE_GREEN) ? 2 : 0) | ((write & D3DCOLORWRITEENABLE_BLUE) ? 4 : 0) |
		((write & D3DCOLORWRITEENABLE_ALPHA) ? 8 : 0));
	if (target_hud_layer)
	{
		hud_layer_blend(blend);
		if (halo_stereo_ui_span())
			hud_layer_ui = TRUE;
	}

	/* the cull mode names the winding to discard; FRONTFACE names the
	front winding */
	raster->cull_mode = rs[D3DRS_CULLMODE] == D3DCULL_NONE ? GPU_CULL_NONE :
		rs[D3DRS_CULLMODE] == rs[D3DRS_FRONTFACE] ? GPU_CULL_FRONT : GPU_CULL_BACK;
	/* a vertex shader that flips y in clip space (clip_y_flip, unlike
	desktop GL's upper-left clip origin) also flips the winding */
	raster->front_face = (rs[D3DRS_FRONTFACE] == D3DFRONT_CCW) != (shader_dialect.clip_y_flip != 0) ?
		GPU_FRONT_COUNTER_CLOCKWISE : GPU_FRONT_CLOCKWISE;
	raster->fill_mode = rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME ? GPU_FILL_LINE :
		rs[D3DRS_FILLMODE] == D3DFILL_POINT ? GPU_FILL_POINT : GPU_FILL_SOLID;
	/* D3DRS_ZBIAS is expressed in these states (D3DDevice_SetRenderState_ZBias) */
	raster->depth_bias_enable = rs[D3DRS_SOLIDOFFSETENABLE] != 0;
	raster->depth_bias_slope = dword_to_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]);
	raster->depth_bias_constant = dword_to_float(rs[D3DRS_POLYGONOFFSETZOFFSET]);
	/* head-tracked stereo's first-person body: its shell nearer than the
	near plane draws at the nearest depth instead of being cut open */
	raster->depth_clamp = halo_first_person_body_depth_clamp() != 0;
}

/* the uniforms of the latest draws, converted from these inputs; the serial
counts the conversions */
#define DRAW_UNIFORM_INPUT_COUNT (4 + 4 + 16 + 1 + 16 + 2 + 4 + 1 + 1 + 7 * D3DTSS_MAXSTAGES)

static DWORD draw_uniform_inputs[DRAW_UNIFORM_INPUT_COUNT];
static struct gpu_uniforms draw_uniforms;

/* fills a draw's packet but for its streams, attributes, indices and
primitive, and brings the draw uniforms up to date; FALSE when the draw is
skipped */
static BOOL prepare_draw(struct gpu_draw *draw, BOOL immediate)
{
	struct vertex_shader_object *program = current_program();
	struct nv2a_pixel_shader_key key;
	float texture_scale[4][4];
	BOOL has_depth = FALSE;
	int stage;

	if (!device.gl_ready || !program || !device.vertex_shader || !program->instructions)
	{
		stats.skipped_no_program++;
		return FALSE;
	}
	{
		const char *skip = debug_settings.skip_vertex_shaders;

		while (skip && *skip)
		{
			if ((unsigned long)atol(skip) == program->id)
				return FALSE;
			skip = strchr(skip, ',');
			if (skip)
				skip++;
		}
	}
	memset(draw, 0, sizeof(*draw));
	if (!draw_targets(&draw->color_target, &draw->depth_target))
	{
		stats.skipped_no_target++;
		return FALSE;
	}
	has_depth = draw->depth_target != 0;
	raster_state_fill(has_depth, &draw->viewport, &draw->scissor, &draw->depth_stencil, &draw->blend, &draw->raster);

	memset(&key, 0, sizeof(key));
	memcpy(key.combiner_state, D3D__RenderState, sizeof(key.combiner_state));
	/* constants are uniforms, not part of the program */
	memset(&key.combiner_state[D3DRS_PSCONSTANT0_0], 0, 16 * sizeof(DWORD));
	key.combiner_state[D3DRS_PSFINALCOMBINERCONSTANT0] = 0;
	key.combiner_state[D3DRS_PSFINALCOMBINERCONSTANT1] = 0;
	key.texture_modes = D3D__RenderState[D3DRS_PSTEXTUREMODES];
	stages_fill(&key, texture_scale, draw->stages);
	for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
	{
		key.alpha_kill[stage] = D3D__TextureState[stage][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key.color_sign[stage] = (unsigned char)((D3D__TextureState[stage][D3DTSS_COLORSIGN] >> 28) & 0xf);
	}
	/* (only with the meter's blend: hud_hires.h, nv2a_pixel_shader_key) */
	if (!((key.coverage_alpha & XGPU_COVERAGE_ALPHA) && D3D__RenderState[D3DRS_ALPHABLENDENABLE] &&
		D3D__RenderState[D3DRS_SRCBLEND] == D3DBLEND_CONSTANTCOLOR &&
		D3D__RenderState[D3DRS_DESTBLEND] == D3DBLEND_SRCALPHA))
	{
		key.coverage_alpha = 0;
	}
	key.alpha_test_function = D3D__RenderState[D3DRS_ALPHATESTENABLE] ? D3D__RenderState[D3DRS_ALPHAFUNC] : 0;
	key.fog_enable = D3D__RenderState[D3DRS_FOGENABLE] != 0;
	key.fog_table_mode = (unsigned char)D3D__RenderState[D3DRS_FOGTABLEMODE];
	key.count_samples = device.visibility_test_active &&
		device_capabilities.occlusion_mode == GPU_OCCLUSION_SHADER_COUNTER;

	draw->vertex_shader = vertex_shader_get(program, immediate);
	draw->pixel_shader = fragment_shader_get(&key);

	/* the state the other uniforms come from: most draws share it with the
	draw before them, and so share its uniforms */
	{
		DWORD inputs[DRAW_UNIFORM_INPUT_COUNT];
		unsigned long count = 0;

		memcpy(&inputs[count], device.viewport_scale, sizeof(device.viewport_scale));
		count += 4;
		memcpy(&inputs[count], device.viewport_offset, sizeof(device.viewport_offset));
		count += 4;
		memcpy(&inputs[count], texture_scale, sizeof(texture_scale));
		count += 16;
		inputs[count++] = D3D__RenderState[D3DRS_POINTSIZE];
		for (stage = 0; stage < 8; stage++)
		{
			inputs[count++] = D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage];
			inputs[count++] = D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage];
		}
		inputs[count++] = D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0];
		inputs[count++] = D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1];
		inputs[count++] = D3D__RenderState[D3DRS_FOGCOLOR];
		inputs[count++] = D3D__RenderState[D3DRS_FOGSTART];
		inputs[count++] = D3D__RenderState[D3DRS_FOGEND];
		inputs[count++] = D3D__RenderState[D3DRS_FOGDENSITY];
		inputs[count++] = D3D__RenderState[D3DRS_ALPHAREF];
		inputs[count++] = (DWORD)UI_OFFSET;
		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		{
			DWORD *state = D3D__TextureState[stage];

			inputs[count++] = state[D3DTSS_BUMPENVMAT00];
			inputs[count++] = state[D3DTSS_BUMPENVMAT01];
			inputs[count++] = state[D3DTSS_BUMPENVMAT10];
			inputs[count++] = state[D3DTSS_BUMPENVMAT11];
			inputs[count++] = state[D3DTSS_BUMPENVLSCALE];
			inputs[count++] = state[D3DTSS_BUMPENVLOFFSET];
			inputs[count++] = state[D3DTSS_MIPMAPLODBIAS];
		}
		if (!draw_uniforms.serial || memcmp(inputs, draw_uniform_inputs, sizeof(inputs)))
		{
			struct gpu_uniforms *converted = &draw_uniforms;

			memcpy(draw_uniform_inputs, inputs, sizeof(inputs));
			draw_uniforms.serial++;
			memcpy(converted->viewport_scale[0], device.viewport_scale, sizeof(converted->viewport_scale));
			memcpy(converted->viewport_offset[0], device.viewport_offset, sizeof(converted->viewport_offset));
			memcpy(converted->texture_scale, texture_scale, sizeof(converted->texture_scale));
			converted->point_size[0][0] = D3D__RenderState[D3DRS_POINTSIZE] ?
				dword_to_float(D3D__RenderState[D3DRS_POINTSIZE]) : 1.0f;
			for (stage = 0; stage < 8; stage++)
			{
				color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage], converted->ps_c0[stage]);
				color_to_vec4(D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage], converted->ps_c1[stage]);
			}
			color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0], converted->ps_final_c0[0]);
			color_to_vec4(D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1], converted->ps_final_c1[0]);
			color_to_vec4(D3D__RenderState[D3DRS_FOGCOLOR], converted->fog_color[0]);
			converted->fog_parameters[0][0] = dword_to_float(D3D__RenderState[D3DRS_FOGSTART]);
			converted->fog_parameters[0][1] = dword_to_float(D3D__RenderState[D3DRS_FOGEND]);
			converted->fog_parameters[0][2] = dword_to_float(D3D__RenderState[D3DRS_FOGDENSITY]);
			converted->fog_parameters[0][3] = 0.0f;
			converted->alpha_reference[0][0] = (float)(D3D__RenderState[D3DRS_ALPHAREF] & 0xff);
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
				converted->texture_lod_bias[0][stage] = dword_to_float(state[D3DTSS_MIPMAPLODBIAS]);
			}
			converted->screen_offset[0][0] = (float)UI_OFFSET;
		}
	}
	return TRUE;
}

/* the packet prepare_draw and the draw function built */
static struct gpu_draw draw_packet;

static void submit_draw(const struct gpu_draw *draw, BOOL immediate)
{
	if (!gpu_draw(draw, &constant_store, &draw_uniforms))
	{
		stats.skipped_link++;
		return;
	}
	if (immediate)
		stats.immediate_draws++;
	else
		stats.draws++;
}

/* ---------- tracing (debug.gpu_trace_frame) */

static BOOL trace_frame(void)
{
	static long frame = -2;

	if (frame == -2)
		frame = config_integer("debug.gpu_trace_frame");
	return frame >= 0 && device.frame == (unsigned long)frame;
}

static void trace_draw(const char *kind, D3DPRIMITIVETYPE type, unsigned long count, const float *first_vertex)
{
	struct vertex_shader_object *program = current_program();
	DWORD *rs = D3D__RenderState;

	if (!trace_frame())
		return;
	platform_log("%s type %d count %lu vs %lu (decl %lu) vp %lu,%lu %lux%lu z%.2f-%.2f zen %lu zw %lu zf %lx blend %lu %lx/%lx cull %lx cw %08lx tm %05lx cc %lx fin %08lx/%08lx at %lu/%lx",
		kind, type, count, program ? program->id : 0, device.vertex_shader ? device.vertex_shader->id : 0,
		device.viewport.X, device.viewport.Y, device.viewport.Width, device.viewport.Height,
		device.viewport.MinZ, device.viewport.MaxZ, rs[D3DRS_ZENABLE], rs[D3DRS_ZWRITEENABLE], rs[D3DRS_ZFUNC],
		rs[D3DRS_ALPHABLENDENABLE], rs[D3DRS_SRCBLEND], rs[D3DRS_DESTBLEND], rs[D3DRS_CULLMODE],
		rs[D3DRS_COLORWRITEENABLE], rs[D3DRS_PSTEXTUREMODES], rs[D3DRS_PSCOMBINERCOUNT],
		rs[D3DRS_PSFINALCOMBINERINPUTSABCD], rs[D3DRS_PSFINALCOMBINERINPUTSEFG],
		rs[D3DRS_ALPHATESTENABLE], rs[D3DRS_ALPHAFUNC]);
	{
		int stage;

		for (stage = 0; stage < D3DTSS_MAXSTAGES; stage++)
		{
			D3DBaseTexture *texture = device.textures[stage];
			struct xgpu_texture_description description;

			if (!texture || !((D3D__RenderState[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f))
				continue;
			xgpu_texture_describe(texture->Format, texture->Size, &description);
			platform_log("    t%d: data %08lx format %08lx size %08lx -> fmt %02lx %lux%lux%lu levels %lu linear %d cube %d rt %d min %lu mip %lu bias %g maxmip %lu",
				stage, texture->Data, texture->Format, texture->Size, description.format, description.width,
				description.height, description.depth, description.levels, description.linear, description.cube_map,
				xgpu_render_target_find(texture->Data) != NULL, D3D__TextureState[stage][D3DTSS_MINFILTER],
				D3D__TextureState[stage][D3DTSS_MIPFILTER], dword_to_float(D3D__TextureState[stage][D3DTSS_MIPMAPLODBIAS]),
				D3D__TextureState[stage][D3DTSS_MAXMIPLEVEL]);
		}
	}
	platform_log("    offset enable %lu slope %g offset %g zbias %ld stencil %lu func %lx ref %lx mask %lx write %lx ops %lx/%lx/%lx",
		rs[D3DRS_SOLIDOFFSETENABLE], dword_to_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]),
		dword_to_float(rs[D3DRS_POLYGONOFFSETZOFFSET]), (long)rs[D3DRS_ZBIAS], rs[D3DRS_STENCILENABLE],
		rs[D3DRS_STENCILFUNC], rs[D3DRS_STENCILREF], rs[D3DRS_STENCILMASK], rs[D3DRS_STENCILWRITEMASK],
		rs[D3DRS_STENCILFAIL], rs[D3DRS_STENCILZFAIL], rs[D3DRS_STENCILPASS]);
	if (config_boolean("debug.gpu_trace_constants"))
	{
		int constant;

		for (constant = 0; constant < XGPU_VERTEX_CONSTANT_COUNT; constant++)
		{
			const float *value = constant_store.c[constant];

			if (value[0] || value[1] || value[2] || value[3])
				platform_log("    c[%d] = %g %g %g %g", constant, value[0], value[1], value[2], value[3]);
		}
	}
	if (device.vertex_shader)
	{
		unsigned long index;

		for (index = 0; index < device.vertex_shader->element_count; index++)
		{
			const struct vertex_element *element = &device.vertex_shader->elements[index];

			platform_log("    decl v%lu: stream %lu offset %lu type %02lx", (unsigned long)element->reg,
				(unsigned long)element->stream, (unsigned long)element->offset, (unsigned long)element->type);
		}
		for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
		{
			const float *value = device.attributes[index];

			if (value[0] || value[1] || value[2] || value[3] != 1.0f)
				platform_log("    current v%lu = %g %g %g %g", index, value[0], value[1], value[2], value[3]);
		}
	}
	if (first_vertex)
	{
		int reg;

		for (reg = 0; reg < XGPU_VERTEX_ATTRIBUTE_COUNT; reg++)
		{
			const float *v = first_vertex + reg * 4;

			if (v[0] || v[1] || v[2] || v[3] != 1.0f)
				platform_log("    v%d = %g %g %g %g", reg, v[0], v[1], v[2], v[3]);
		}
	}
}


/* ---------- the contiguous window in GL buffers

Vertex and index buffers live in the Xbox's contiguous memory, where most
never change once loaded. The mirror keeps a copy of that memory in GL
buffers (one per segment, created when first needed) and uploads a page
only when it is first drawn from or after the game has written it: pages
are write-protected once uploaded, as cached textures are (memory_watch.c).
Pages the game rewrites frame after frame (dynamic vertices) would fault on
every write; after a few such rewrites a page counts as volatile for a
while, and draws that use it stream their data as before. */

#define MIRROR_SEGMENT_SIZE 0x400000UL
#define MIRROR_SEGMENT_COUNT (PLATFORM_CONTIGUOUS_SIZE / MIRROR_SEGMENT_SIZE)
#define MIRROR_PAGE_SIZE 0x1000UL
#define MIRROR_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / MIRROR_PAGE_SIZE)
/* rewrites no more than this many frames apart ... */
#define MIRROR_REWRITE_FRAMES 2
/* ... this many times in a row make a page volatile ... */
#define MIRROR_VOLATILE_REWRITES 4
/* ... for this many frames */
#define MIRROR_VOLATILE_FRAMES 600

enum
{
	_mirror_page_absent,
	_mirror_page_present,
	_mirror_page_volatile
};

static struct
{
	gpu_buffer buffers[MIRROR_SEGMENT_COUNT];
	unsigned char state[MIRROR_PAGE_COUNT];
	unsigned char rewrites[MIRROR_PAGE_COUNT];
	/* the page's memory_watch generation when it was uploaded */
	unsigned long generation[MIRROR_PAGE_COUNT];
	unsigned long rewritten_frame[MIRROR_PAGE_COUNT];
} mirror;

/* uploads the pages of [first, last) that are absent or stale; FALSE if one
of them turns out to be volatile */
static BOOL mirror_refresh(unsigned long first, unsigned long last)
{
	unsigned long page, run;
	BOOL volatile_page = FALSE;
	unsigned char stale[256];
	unsigned long count = last - first;

	if (count > sizeof(stale))
	{
		/* a range this long is refreshed in pieces */
		for (page = first; page < last; page += sizeof(stale))
		{
			if (!mirror_refresh(page, page + sizeof(stale) < last ? page + sizeof(stale) : last))
				return FALSE;
		}
		return TRUE;
	}
	for (page = first; page < last; page++)
	{
		BOOL written = mirror.state[page] == _mirror_page_present &&
			memory_watch_generation(PLATFORM_CONTIGUOUS_BASE + page * MIRROR_PAGE_SIZE, MIRROR_PAGE_SIZE) >
			mirror.generation[page];

		stale[page - first] = mirror.state[page] != _mirror_page_present || written;
		if (written)
		{
			if (device.frame - mirror.rewritten_frame[page] <= MIRROR_REWRITE_FRAMES)
				mirror.rewrites[page]++;
			else
				mirror.rewrites[page] = 1;
			mirror.rewritten_frame[page] = device.frame;
			if (mirror.rewrites[page] >= MIRROR_VOLATILE_REWRITES)
			{
				mirror.state[page] = _mirror_page_volatile;
				volatile_page = TRUE;
			}
		}
	}
	if (volatile_page)
		return FALSE;
	for (page = first; page < last; page = run)
	{
		unsigned long segment = page * MIRROR_PAGE_SIZE / MIRROR_SEGMENT_SIZE;
		unsigned long address, size;
		/* no queued draw can read pages uploaded for the first time */
		BOOL unused = TRUE;

		if (!stale[page - first])
		{
			run = page + 1;
			continue;
		}
		for (run = page; run < last && stale[run - first]; run++)
		{
			if (mirror.state[run] != _mirror_page_present)
			{
				mirror.rewrites[run] = 0;
				mirror.rewritten_frame[run] = device.frame;
			}
			else
			{
				unused = FALSE;
			}
		}
		address = PLATFORM_CONTIGUOUS_BASE + page * MIRROR_PAGE_SIZE;
		size = (run - page) * MIRROR_PAGE_SIZE;
		/* protect first, so a write racing with the upload is noticed */
		memory_watch_protect(address, size);
		for (; page < run; page++)
		{
			mirror.generation[page] = memory_watch_generation(PLATFORM_CONTIGUOUS_BASE + page * MIRROR_PAGE_SIZE,
				MIRROR_PAGE_SIZE);
			mirror.state[page] = _mirror_page_present;
		}
		if (!mirror.buffers[segment])
			mirror.buffers[segment] = gpu_buffer_create(MIRROR_SEGMENT_SIZE);
		gpu_buffer_write(mirror.buffers[segment],
			(uint32_t)(address - PLATFORM_CONTIGUOUS_BASE - segment * MIRROR_SEGMENT_SIZE),
			(uint32_t)size, xbox_pointer(address), unused ? GPU_WRITE_UNUSED : 0);
	}
	return TRUE;
}

/* makes [address, address + size) current in the mirror, giving the buffer
that holds it, the range's offset in that buffer and the newest upload
generation of its pages (which changes whenever its contents do); FALSE if
the range is outside the window, spans two segments or is volatile */
static BOOL mirror_range(unsigned long address, unsigned long size, gpu_buffer *buffer, unsigned long *offset,
	unsigned long *generation)
{
	unsigned long start = address - PLATFORM_CONTIGUOUS_BASE;
	unsigned long segment, first, last, page, oldest = ~0UL, newest = 0;
	BOOL present = TRUE;

	if (!size || address < PLATFORM_CONTIGUOUS_BASE || start > PLATFORM_CONTIGUOUS_SIZE ||
		size > PLATFORM_CONTIGUOUS_SIZE - start)
		return FALSE;
	segment = start / MIRROR_SEGMENT_SIZE;
	if ((start + size - 1) / MIRROR_SEGMENT_SIZE != segment)
		return FALSE;
	first = start / MIRROR_PAGE_SIZE;
	last = (start + size - 1) / MIRROR_PAGE_SIZE + 1;
	for (page = first; page < last; page++)
	{
		if (mirror.state[page] == _mirror_page_volatile)
		{
			if (device.frame - mirror.rewritten_frame[page] < MIRROR_VOLATILE_FRAMES)
				return FALSE;
			mirror.state[page] = _mirror_page_absent;
		}
		if (mirror.state[page] != _mirror_page_present)
		{
			present = FALSE;
		}
		else
		{
			if (mirror.generation[page] < oldest)
				oldest = mirror.generation[page];
			if (mirror.generation[page] > newest)
				newest = mirror.generation[page];
		}
	}
	if (!present || memory_watch_generation(address, size) > oldest)
	{
		if (!mirror_refresh(first, last))
			return FALSE;
		for (newest = 0, page = first; page < last; page++)
		{
			if (mirror.generation[page] > newest)
				newest = mirror.generation[page];
		}
	}
	*buffer = mirror.buffers[segment];
	*offset = start - segment * MIRROR_SEGMENT_SIZE;
	if (generation)
		*generation = newest;
	stats.mirrored_bytes += size;
	return TRUE;
}

/* the smallest and largest index of an index range the mirror holds: the
same ranges are drawn frame after frame */
#define INDEX_RANGE_SLOTS 4096

static struct
{
	unsigned long address;
	unsigned long count;
	unsigned long generation;
	WORD minimum;
	WORD maximum;
} index_ranges[INDEX_RANGE_SLOTS];

static void index_extent(const WORD *indices, unsigned long count, unsigned long generation, BOOL cached,
	unsigned long *minimum, unsigned long *maximum)
{
	unsigned long slot = (((unsigned long)XBOX_ADDRESS(indices) >> 1) ^ (count * 2654435761UL)) % INDEX_RANGE_SLOTS;
	unsigned long index, low = 0xffff, high = 0;

	if (cached && index_ranges[slot].address == (unsigned long)XBOX_ADDRESS(indices) && index_ranges[slot].count == count &&
		index_ranges[slot].generation == generation)
	{
		*minimum = index_ranges[slot].minimum;
		*maximum = index_ranges[slot].maximum;
		return;
	}
	for (index = 0; index < count; index++)
	{
		if (indices[index] < low)
			low = indices[index];
		if (indices[index] > high)
			high = indices[index];
	}
	if (cached)
	{
		index_ranges[slot].address = (unsigned long)XBOX_ADDRESS(indices);
		index_ranges[slot].count = count;
		index_ranges[slot].generation = generation;
		index_ranges[slot].minimum = (WORD)low;
		index_ranges[slot].maximum = (WORD)high;
	}
	*minimum = low;
	*maximum = high;
}

/* ---------- vertex data */

static unsigned long stream_upload(const void *data, unsigned long size, gpu_buffer *buffer)
{
	return gpu_stream(GPU_STREAM_KIND_VERTEX, data, (uint32_t)size, buffer);
}

/* stream_upload, with the D3DCOLOR elements of the stream turned from BGRA
into the RGBA byte order a backend without vertex_bgra reads */
static unsigned long stream_upload_swizzled(const struct vertex_shader_object *declaration, unsigned long stream,
	const unsigned char *data, unsigned long size, unsigned long stride, gpu_buffer *buffer)
{
	static unsigned char *scratch;
	static unsigned long scratch_size;
	unsigned long offsets[XGPU_VERTEX_ATTRIBUTE_COUNT];
	unsigned long count = 0, index, vertex;

	for (index = 0; index < declaration->element_count; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];

		if (element->stream == stream && element->type == D3DVSDT_D3DCOLOR)
			offsets[count++] = element->offset;
	}
	if (!count || !stride)
		return stream_upload(data, size, buffer);
	if (scratch_size < size)
	{
		free(scratch);
		scratch_size = size + 65536;
		scratch = malloc(scratch_size);
	}
	memcpy(scratch, data, size);
	for (vertex = 0; vertex + stride <= size; vertex += stride)
	{
		for (index = 0; index < count; index++)
		{
			unsigned char *color = scratch + vertex + offsets[index];
			unsigned char blue = color[0];

			color[0] = color[2];
			color[2] = blue;
		}
	}
	return stream_upload(scratch, size, buffer);
}

static uint32_t index_upload(const void *data, unsigned long size, gpu_buffer *buffer)
{
	return gpu_stream(GPU_STREAM_KIND_INDEX, data, (uint32_t)size, buffer);
}

/* the GPU_ATTRIBUTE_* format of a declaration element */
static uint8_t attribute_format(const struct vertex_element *element)
{
	switch (element->type)
	{
	case D3DVSDT_FLOAT1: return GPU_ATTRIBUTE_FLOAT1;
	case D3DVSDT_FLOAT2: return GPU_ATTRIBUTE_FLOAT2;
	case D3DVSDT_FLOAT3: case D3DVSDT_FLOAT2H: return GPU_ATTRIBUTE_FLOAT3;
	case D3DVSDT_FLOAT4: return GPU_ATTRIBUTE_FLOAT4;
	/* without BGRA attributes, stream_upload_swizzled swaps the bytes */
	case D3DVSDT_D3DCOLOR: return device_capabilities.vertex_bgra ? GPU_ATTRIBUTE_BGRA8 : GPU_ATTRIBUTE_RGBA8;
	case D3DVSDT_SHORT1: return GPU_ATTRIBUTE_SHORT1;
	case D3DVSDT_SHORT2: return GPU_ATTRIBUTE_SHORT2;
	case D3DVSDT_SHORT3: return GPU_ATTRIBUTE_SHORT3;
	case D3DVSDT_SHORT4: return GPU_ATTRIBUTE_SHORT4;
	case D3DVSDT_NORMSHORT1: return GPU_ATTRIBUTE_NORMSHORT1;
	case D3DVSDT_NORMSHORT2: return GPU_ATTRIBUTE_NORMSHORT2;
	case D3DVSDT_NORMSHORT3: return GPU_ATTRIBUTE_NORMSHORT3;
	case D3DVSDT_NORMSHORT4: return GPU_ATTRIBUTE_NORMSHORT4;
	case D3DVSDT_PBYTE1: return GPU_ATTRIBUTE_UBYTE1;
	case D3DVSDT_PBYTE2: return GPU_ATTRIBUTE_UBYTE2;
	case D3DVSDT_PBYTE3: return GPU_ATTRIBUTE_UBYTE3;
	case D3DVSDT_PBYTE4: return GPU_ATTRIBUTE_UBYTE4;
	case D3DVSDT_NORMPACKED3: return GPU_ATTRIBUTE_NORMPACKED3;
	default: return GPU_ATTRIBUTE_FLOAT4;
	}
}

/* without vertex_bgra a stream with colors is swizzled as it is uploaded
(stream_upload_swizzled) and cannot come from the mirror */
static BOOL stream_has_colors(const struct vertex_shader_object *declaration, unsigned long stream)
{
	unsigned long index;

	for (index = 0; index < declaration->element_count; index++)
	{
		if (declaration->elements[index].stream == stream && declaration->elements[index].type == D3DVSDT_D3DCOLOR)
			return TRUE;
	}
	return FALSE;
}

/* upload vertices [first, first + count) of every stream the declaration
uses into the packet's streams and point its attributes at them; attribute
data then starts at vertex 0 of the uploaded range */
static BOOL setup_streams(struct gpu_draw *draw, unsigned long first, unsigned long count)
{
	struct vertex_shader_object *declaration = device.vertex_shader;
	struct gpu_vertex_stream *streams = draw->streams;
	struct gpu_vertex_attribute attribute;
	BOOL placed[16] = { FALSE };
	BOOL enabled[XGPU_VERTEX_ATTRIBUTE_COUNT] = { FALSE };
	unsigned long index, total = 0;

	/* the mirror first; then one reservation for everything streamed */
	for (index = 0; index < declaration->element_count; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];
		unsigned long stream = element->stream, stride;
		uint32_t bytes, base;
		if (stream >= 16 || element->reg >= XGPU_VERTEX_ATTRIBUTE_COUNT)
			return FALSE;
		stride = device.streams[stream].stride;
		if (element->type != D3DVSDT_NONE &&
			(element->offset > (stride ? stride : 64) ||
			 element->bytes > (stride ? stride : 64) - element->offset))
			return FALSE;
		unsigned long offset;

		if (!device.streams[stream].data || element->type == D3DVSDT_NONE || placed[stream])
			continue;
		placed[stream] = TRUE;
		streams[stream].buffer = 0;
		streams[stream].stride = (uint32_t)stride;
		if (!metal_upload_span((uint32_t)XBOX_ADDRESS(PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data)),
			(uint32_t)first, (uint32_t)count, (uint32_t)stride,
			PLATFORM_CONTIGUOUS_BASE, PLATFORM_CONTIGUOUS_SIZE, &base, &bytes))
			return FALSE;
		if ((device_capabilities.vertex_bgra || !stream_has_colors(declaration, stream)) &&
			mirror_range(base, bytes, &streams[stream].buffer, &offset, NULL))
		{
			streams[stream].offset = (uint32_t)offset;
			continue;
		}
		streams[stream].buffer = 0;
		if (((bytes + 15) & ~15U) > METAL_UPLOAD_BYTES_MAX - total)
			return FALSE;
		total += (bytes + 15) & ~15U;
	}
	gpu_stream_reserve((uint32_t)total, 0);
	for (index = 0; index < declaration->element_count; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];
		unsigned long stream = element->stream;
		unsigned long stride = device.streams[stream].stride;

		if (!device.streams[stream].data || element->type == D3DVSDT_NONE)
			continue;
		if (!streams[stream].buffer)
		{
			const unsigned char *base = PLATFORM_PHYSICAL_TO_VIRTUAL(device.streams[stream].data);
			unsigned long bytes = stride ? stride * count : 64;

			streams[stream].offset = (uint32_t)(device_capabilities.vertex_bgra ?
				stream_upload(base + first * stride, bytes, &streams[stream].buffer) :
				stream_upload_swizzled(declaration, stream, base + first * stride, bytes, stride, &streams[stream].buffer));
			stats.streamed_bytes += bytes;
		}
		attribute.format = attribute_format(element);
		attribute.stream = (uint8_t)stream;
		attribute.offset = element->offset;
		draw->attributes[element->reg] = attribute;
		enabled[element->reg] = TRUE;
	}
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (!enabled[index])
		{
			/* a packed attribute reads the integer zero, any other its current value */
			attribute.format = declaration->packed_mask & (1UL << index) ? GPU_ATTRIBUTE_NORMPACKED3 : GPU_ATTRIBUTE_FLOAT4;
			attribute.stream = GPU_STREAM_CONSTANT;
			attribute.offset = 0;
			draw->attributes[index] = attribute;
			memcpy(draw->constant_values[index], device.attributes[index], sizeof(draw->constant_values[index]));
		}
	}
	return TRUE;
}

/* the packet's primitive for a D3D one (quad lists become triangle lists
with indices of their own, quad_indices) */
static uint32_t packet_primitive(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
	case D3DPT_POINTLIST: return GPU_PRIMITIVE_POINTS;
	case D3DPT_LINELIST: return GPU_PRIMITIVE_LINES;
	case D3DPT_LINELOOP: return GPU_PRIMITIVE_LINE_LOOP;
	case D3DPT_LINESTRIP: return GPU_PRIMITIVE_LINE_STRIP;
	case D3DPT_TRIANGLESTRIP:
	case D3DPT_QUADSTRIP: return GPU_PRIMITIVE_TRIANGLE_STRIP;
	case D3DPT_TRIANGLEFAN:
	case D3DPT_POLYGON: return GPU_PRIMITIVE_TRIANGLE_FAN;
	default: return GPU_PRIMITIVE_TRIANGLES;
	}
}

/* index lists a draw makes for itself (quad_indices, and the rebased copy
in D3DDevice_DrawIndexedVertices), which go to gpu_stream at once: two
buffers that grow as needed and are reused, rather than a malloc and free
for nearly every indexed draw */
enum
{
	_index_scratch_quads,
	_index_scratch_rebased,
	NUMBER_OF_INDEX_SCRATCHES
};

static struct
{
	WORD *indices;
	unsigned long capacity;
} index_scratches[NUMBER_OF_INDEX_SCRATCHES];

/* room for count indices in a scratch, whose contents are then undefined */
static WORD *index_scratch(int scratch, unsigned long count)
{
	if (scratch < 0 || scratch >= NUMBER_OF_INDEX_SCRATCHES ||
		count > METAL_DRAW_VERTICES_MAX * 3 / 2)
		return NULL;
	if (index_scratches[scratch].capacity < count + 1)
	{
		unsigned long capacity = count + 1 > 4096 ? count + 1 : 4096;

		while (capacity < count + 1)
			capacity *= 2;
		WORD *indices = realloc(index_scratches[scratch].indices, capacity * sizeof(WORD));
		if (!indices)
			return NULL;
		index_scratches[scratch].indices = indices;
		index_scratches[scratch].capacity = capacity;
	}
	return index_scratches[scratch].indices;
}

/* quads become two triangles each, in the quads scratch (index_scratch) */
static WORD *quad_indices(const WORD *indices, unsigned long count, unsigned long *out_count)
{
	unsigned long quads = count / 4;
	WORD *result = index_scratch(_index_scratch_quads, quads * 6);
	unsigned long quad;
	if (!result)
	{
		*out_count = 0;
		return NULL;
	}

	for (quad = 0; quad < quads; quad++)
	{
		WORD v0 = indices ? indices[quad * 4] : (WORD)(quad * 4);
		WORD v1 = indices ? indices[quad * 4 + 1] : (WORD)(quad * 4 + 1);
		WORD v2 = indices ? indices[quad * 4 + 2] : (WORD)(quad * 4 + 2);
		WORD v3 = indices ? indices[quad * 4 + 3] : (WORD)(quad * 4 + 3);

		result[quad * 6 + 0] = v0;
		result[quad * 6 + 1] = v1;
		result[quad * 6 + 2] = v2;
		result[quad * 6 + 3] = v0;
		result[quad * 6 + 4] = v2;
		result[quad * 6 + 5] = v3;
	}
	*out_count = quads * 6;
	return result;
}

void WINAPI D3DDevice_SetStreamSource(UINT stream_number, D3DVertexBuffer *stream_data, UINT stride)
{
	if (stream_number >= 16)
		return;
	device.streams[stream_number].data = stream_data ? stream_data->Data : 0;
	device.streams[stream_number].stride = stride;
}

void WINAPI D3DDevice_SetIndices(D3DIndexBuffer *index_data, UINT base_vertex_index)
{
	device.base_vertex_index = base_vertex_index;
	D3D__IndexData = index_data ? (WORD *)xbox_pointer(index_data->Data) : NULL;
}

/* a draw into stereo's HUD layer or a HUD group's target
(target_hud_measured): its screen extent, in the targets' units, to the
rectangle of the group whose target it drew into (halo_hud_group_extent_in). Immediate-mode draws,
which is how the HUD draws its bitmaps, meters, text and motion sensor, are
measured exactly: the vertex program runs on the CPU for each vertex's
screen position (oPos, as the translated shader reads it before undoing
the screen-space conversion: D3D's pixel centers, half a pixel in, and the
UI's offset). Any other draw (vertices NULL) is taken as the whole
viewport, and counted for debug.gpu_stats. Clipped to the viewport */
static void hud_draw_extent(const float *vertices, unsigned long count)
{
	struct vertex_shader_object *program = current_program();
	float viewport[4] = { (float)device.viewport.X, (float)device.viewport.Y,
		(float)(device.viewport.X + device.viewport.Width), (float)(device.viewport.Y + device.viewport.Height) };
	float extent[4] = { viewport[2], viewport[3], viewport[0], viewport[1] };
	unsigned long floats = XGPU_VERTEX_ATTRIBUTE_COUNT * 4, index;

	if (!vertices || !program || !program->instructions)
	{
		hud_draws_unmeasured++;
		halo_hud_group_extent_in(target_hud_group, viewport[0], viewport[1], viewport[2], viewport[3]);
		return;
	}
	hud_draws_measured++;
	for (index = 0; index < count; index++)
	{
		float position[4], x, y;

		nv2a_vertex_program_position(program->instructions, program->instruction_count,
			(const float (*)[4])constant_store.c, (const float (*)[4])(vertices + index * floats), position);
		x = position[0] + 0.5f + (float)UI_OFFSET;
		y = position[1] + 0.5f;
		if (!isfinite(x) || !isfinite(y))
			continue;
		extent[0] = fminf(extent[0], x);
		extent[1] = fminf(extent[1], y);
		extent[2] = fmaxf(extent[2], x);
		extent[3] = fmaxf(extent[3], y);
	}
	extent[0] = fmaxf(extent[0], viewport[0]);
	extent[1] = fmaxf(extent[1], viewport[1]);
	extent[2] = fminf(extent[2], viewport[2]);
	extent[3] = fminf(extent[3], viewport[3]);
	halo_hud_group_extent_in(target_hud_group, extent[0], extent[1], extent[2], extent[3]);
}

void WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT vertex_count)
{
	struct gpu_draw *draw = &draw_packet;

	if (!vertex_count || vertex_count > METAL_DRAW_VERTICES_MAX || !prepare_draw(draw, FALSE))
		return;
	if (target_hud_measured)
		hud_draw_extent(NULL, 0);
	trace_draw("draw", primitive_type, vertex_count, NULL);
	if (!setup_streams(draw, start_vertex, vertex_count))
		return;
	if (primitive_type == D3DPT_QUADLIST)
	{
		unsigned long count;
		WORD *indices = quad_indices(NULL, vertex_count, &count);
		if (!indices || !count)
			return;

		draw->index_offset = index_upload(indices, count * sizeof(WORD), &draw->index_buffer);
		draw->primitive = GPU_PRIMITIVE_TRIANGLES;
		draw->count = (uint32_t)count;
	}
	else
	{
		draw->primitive = packet_primitive(primitive_type);
		draw->count = vertex_count;
	}
	submit_draw(draw, FALSE);
}

void WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE primitive_type, UINT vertex_count, CONST WORD *index_data)
{
	struct gpu_draw *draw = &draw_packet;
	unsigned long minimum, maximum, index, count, generation = 0, index_offset = 0;
	WORD *indices = NULL;
	const WORD *source = index_data;
	BOOL mirrored;

	if (!vertex_count || vertex_count > METAL_DRAW_VERTICES_MAX || !index_data || !prepare_draw(draw, FALSE))
		return;
	if (target_hud_measured)
		hud_draw_extent(NULL, 0);
	/* quads are drawn as triangles, from indices made for the draw */
	mirrored = primitive_type != D3DPT_QUADLIST && device_capabilities.base_vertex &&
		mirror_range((unsigned long)XBOX_ADDRESS(index_data), vertex_count * sizeof(WORD), &draw->index_buffer, &index_offset, &generation);
	index_extent(index_data, vertex_count, generation, mirrored, &minimum, &maximum);
	trace_draw("indexed", primitive_type, vertex_count, NULL);
	/* (the streams from the base vertex on: index i is vertex base + i) */
	if (device.base_vertex_index > UINT32_MAX - minimum ||
		!setup_streams(draw, device.base_vertex_index + minimum, maximum - minimum + 1))
		return;
	draw->primitive = packet_primitive(primitive_type);
	if (mirrored)
	{
		/* the attributes start at vertex minimum */
		draw->index_offset = (uint32_t)index_offset;
		draw->count = vertex_count;
		draw->base_vertex = -(int32_t)minimum;
		submit_draw(draw, FALSE);
		return;
	}
	stats.streamed_bytes += vertex_count * sizeof(WORD);
	count = vertex_count;
	if (primitive_type == D3DPT_QUADLIST)
	{
		indices = quad_indices(index_data, vertex_count, &count);
		if (!indices || !count)
			return;
		source = indices;
		draw->primitive = GPU_PRIMITIVE_TRIANGLES;
	}
	if (!device_capabilities.base_vertex)
	{
		/* the indices are copied anyway: rebase them */
		WORD *rebased = index_scratch(_index_scratch_rebased, count);
		if (!rebased || !source)
			return;

		for (index = 0; index < count; index++)
			rebased[index] = (WORD)(source[index] - minimum);
		draw->index_offset = index_upload(rebased, count * sizeof(WORD), &draw->index_buffer);
		draw->count = (uint32_t)count;
		submit_draw(draw, FALSE);
		return;
	}
	draw->index_offset = index_upload(source, count * sizeof(WORD), &draw->index_buffer);
	draw->count = (uint32_t)count;
	draw->base_vertex = -(int32_t)minimum;
	submit_draw(draw, FALSE);
}

/* ---------- immediate mode */

void WINAPI D3DDevice_Begin(D3DPRIMITIVETYPE primitive_type)
{
	device.immediate_active = TRUE;
	device.immediate_type = primitive_type;
	device.immediate_count = 0;
}

static void immediate_emit(void)
{
	unsigned long floats = XGPU_VERTEX_ATTRIBUTE_COUNT * 4;

	if (device.immediate_count >= METAL_DRAW_VERTICES_MAX)
		return;
	if (device.immediate_count == device.immediate_capacity)
	{
		unsigned long capacity = device.immediate_capacity ? device.immediate_capacity * 2 : 256;
		float *vertices = realloc(device.immediate_vertices, capacity * floats * sizeof(float));
		if (!vertices)
		{
			device.immediate_count = 0;
			device.immediate_active = FALSE;
			return;
		}
		device.immediate_vertices = vertices;
		device.immediate_capacity = capacity;
	}
	memcpy(device.immediate_vertices + device.immediate_count * floats, device.attributes, floats * sizeof(float));
	device.immediate_count++;
}

void WINAPI D3DDevice_End(void)
{
	struct gpu_draw *draw = &draw_packet;
	unsigned long stride = XGPU_VERTEX_ATTRIBUTE_COUNT * 4 * sizeof(float);
	unsigned long index, count = device.immediate_count;
	D3DPRIMITIVETYPE type = device.immediate_type;

	device.immediate_active = FALSE;
	if (!count || !prepare_draw(draw, TRUE))
		return;
	if (target_hud_measured)
		hud_draw_extent(device.immediate_vertices, count);
	trace_draw("immediate", type, count, device.immediate_vertices);
	/* every attribute, as a float4, from stream 0 */
	draw->streams[0].offset = (uint32_t)stream_upload(device.immediate_vertices, count * stride, &draw->streams[0].buffer);
	draw->streams[0].stride = (uint32_t)stride;
	for (index = 0; index < XGPU_VERTEX_ATTRIBUTE_COUNT; index++)
	{
		draw->attributes[index].format = GPU_ATTRIBUTE_FLOAT4;
		draw->attributes[index].stream = 0;
		draw->attributes[index].offset = (uint16_t)(index * 4 * sizeof(float));
	}
	if (type == D3DPT_QUADLIST)
	{
		unsigned long index_count;
		WORD *indices = quad_indices(NULL, count, &index_count);

		draw->index_offset = index_upload(indices, index_count * sizeof(WORD), &draw->index_buffer);
		draw->primitive = GPU_PRIMITIVE_TRIANGLES;
		draw->count = (uint32_t)index_count;
	}
	else
	{
		draw->primitive = packet_primitive(type);
		draw->count = (uint32_t)count;
	}
	submit_draw(draw, TRUE);
}

static void set_attribute(INT reg, float a, float b, float c, float d)
{
	BOOL emit = FALSE;

	if (reg == D3DVSDE_VERTEX)
	{
		reg = 0;
		emit = TRUE;
	}
	if (reg < 0 || reg >= XGPU_VERTEX_ATTRIBUTE_COUNT)
		return;
	device.attributes[reg][0] = a;
	device.attributes[reg][1] = b;
	device.attributes[reg][2] = c;
	device.attributes[reg][3] = d;
	/* like the hardware, writing register 0 completes a vertex */
	if (device.immediate_active && (emit || reg == 0))
		immediate_emit();
}

void WINAPI D3DDevice_SetVertexData2f(INT reg, FLOAT a, FLOAT b)
{
	set_attribute(reg, a, b, 0.0f, 1.0f);
}

void WINAPI D3DDevice_SetVertexData4f(INT reg, FLOAT a, FLOAT b, FLOAT c, FLOAT d)
{
	set_attribute(reg, a, b, c, d);
}

void WINAPI D3DDevice_SetVertexData2s(INT reg, SHORT a, SHORT b)
{
	set_attribute(reg, (float)a, (float)b, 0.0f, 1.0f);
}

void WINAPI D3DDevice_SetVertexData4ub(INT reg, BYTE a, BYTE b, BYTE c, BYTE d)
{
	set_attribute(reg, a / 255.0f, b / 255.0f, c / 255.0f, d / 255.0f);
}

void WINAPI D3DDevice_SetVertexDataColor(INT reg, D3DCOLOR color)
{
	float value[4];

	color_to_vec4(color, value);
	set_attribute(reg, value[0], value[1], value[2], value[3]);
}

/* ---------- clearing */

void WINAPI D3DDevice_Clear(DWORD count, CONST D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	struct gpu_clear clear;
	struct gpu_rect *list;
	uint32_t listed = 0;
	DWORD index;

	memset(&clear, 0, sizeof(clear));
	if (!device.gl_ready || !draw_targets(&clear.color_target, &clear.depth_target))
		return;
	if (trace_frame())
		platform_log("clear flags %lx color %08lx z %g count %lu target %08lx depth %08lx", (unsigned long)flags,
			(unsigned long)color, z, (unsigned long)count,
			device.render_target ? (unsigned long)device.render_target->Data : 0,
			device.depth_stencil ? (unsigned long)device.depth_stencil->Data : 0);
	stats.clears++;
	if (flags & D3DCLEAR_TARGET)
	{
		/* the Xbox clears the channels named (D3DCLEAR_TARGET_R, _G, _B, _A):
		the fog screen clears only alpha, leaving the picture under the fog */
		clear.flags |= GPU_CLEAR_COLOR;
		clear.channel_mask = ((flags & D3DCLEAR_TARGET_R) ? GPU_CHANNEL_RED : 0) |
			((flags & D3DCLEAR_TARGET_G) ? GPU_CHANNEL_GREEN : 0) |
			((flags & D3DCLEAR_TARGET_B) ? GPU_CHANNEL_BLUE : 0) |
			((flags & D3DCLEAR_TARGET_A) ? GPU_CHANNEL_ALPHA : 0);
		clear.color = (uint32_t)color;
		/* stereo's HUD layer keeps the picture's transmittance in alpha
		(hud_layer_blend): the game's alpha is coverage, so its clear to 0
		leaves the layer empty */
		if (target_hud_layer)
			clear.color = (clear.color & 0x00ffffffu) | ((0xffu - (clear.color >> 24)) << 24);
	}
	if (clear.depth_target && (flags & D3DCLEAR_ZBUFFER))
	{
		clear.flags |= GPU_CLEAR_DEPTH;
		clear.depth = z;
	}
	if (clear.depth_target && (flags & D3DCLEAR_STENCIL))
	{
		clear.flags |= GPU_CLEAR_STENCIL;
		clear.stencil = (uint32_t)stencil;
	}
	list = malloc(sizeof(*list) * (count && rectangles ? count : 1));
	if (!count || !rectangles)
	{
		/* the NV2A clips a viewport-less clear to the viewport, which is what
		keeps a split-screen window's clear from wiping the other window */
		int32_t x0 = target_pixel((float)device.viewport.X, 0);
		int32_t y0 = target_pixel((float)device.viewport.Y, 1);

		list[0].x = x0;
		list[0].y = y0;
		list[0].width = target_pixel((float)(device.viewport.X + device.viewport.Width), 0) - x0;
		list[0].height = target_pixel((float)(device.viewport.Y + device.viewport.Height), 1) - y0;
		listed = 1;
	}
	else
	{
		for (index = 0; index < count; index++)
		{
			INT left = rectangles[index].x1 > device.viewport.X ? rectangles[index].x1 : device.viewport.X;
			INT top = rectangles[index].y1 > device.viewport.Y ? rectangles[index].y1 : device.viewport.Y;
			INT right = rectangles[index].x2 < device.viewport.X + device.viewport.Width ?
				rectangles[index].x2 : device.viewport.X + device.viewport.Width;
			INT bottom = rectangles[index].y2 < device.viewport.Y + device.viewport.Height ?
				rectangles[index].y2 : device.viewport.Y + device.viewport.Height;
			int32_t x0, y0;

			if (left >= right || top >= bottom)
				continue;
			x0 = target_pixel((float)(left + UI_OFFSET), 0);
			y0 = target_pixel((float)top, 1);
			list[listed].x = x0;
			list[listed].y = y0;
			list[listed].width = target_pixel((float)(right + UI_OFFSET), 0) - x0;
			list[listed].height = target_pixel((float)bottom, 1) - y0;
			listed++;
		}
	}
	gpu_clear(&clear, list, listed);
	free(list);
}

/* ---------- presentation */

/* suffix: "" for the usual frameNNNNN.bmp, else e.g. "-left" */
static void write_screenshot_channel(struct render_target_entry *target, const char *suffix, int alpha);

static void write_screenshot(struct render_target_entry *target, const char *suffix)
{
	write_screenshot_channel(target, suffix, 0);
}

/* a screenshot's BGRA pixels, rows from the top, as directory/frameNNNNN<suffix>.bmp */
static void screenshot_file(const char *directory, const char *suffix, const unsigned char *pixels,
	unsigned long width, unsigned long height)
{
	unsigned char header[54] = { 'B', 'M' };
	unsigned long image_size = width * height * 4, row;
	char path[512];
	FILE *file;

	snprintf(path, sizeof(path), "%s/frame%05lu%s.bmp", directory, device.frame, suffix);
	file = fopen(path, "wb");
	if (file)
	{
		*(unsigned int *)(header + 2) = (unsigned int)(54 + image_size);
		*(unsigned int *)(header + 10) = 54;
		*(unsigned int *)(header + 14) = 40;
		*(int *)(header + 18) = (int)width;
		*(int *)(header + 22) = -(int)height; /* rows from the top, as read */
		*(unsigned short *)(header + 26) = 1;
		*(unsigned short *)(header + 28) = 32;
		*(unsigned int *)(header + 34) = (unsigned int)image_size;
		fwrite(header, 1, sizeof(header), file);
		for (row = 0; row < height; row++)
			fwrite(pixels + row * width * 4, 1, width * 4, file);
		fclose(file);
	}
}

/* the target's color, or with alpha its alpha channel as gray (stereo's HUD
layer: the picture's transmittance, hud_layer_blend) */
static void write_screenshot_channel(struct render_target_entry *target, const char *suffix, int alpha)
{
	const char *directory = *config_string("debug.screenshot_directory") ?
		config_string("debug.screenshot_directory") : NULL;
	unsigned long width = target->target.gl_width, height = target->target.gl_height;
	unsigned char *pixels;
	unsigned long row;
	unsigned long image_size = width * height * 4;

	if (!directory)
		return;
	pixels = malloc(image_size);
	if (!gpu_texture_read(target->target.texture, pixels, (uint32_t)image_size))
	{
		free(pixels);
		return;
	}
	/* the display ignores destination alpha, which the game uses as scratch;
	image viewers would show it as transparency */
	for (row = 0; row < width * height; row++)
	{
		if (alpha)
			pixels[row * 4] = pixels[row * 4 + 1] = pixels[row * 4 + 2] = pixels[row * 4 + 3];
		pixels[row * 4 + 3] = 0xff;
	}
	screenshot_file(directory, suffix, pixels, width, height);
	free(pixels);
}

/* debug.screenshot_every in the side-by-side view, around a cutscene's
film: what HEAD mode's presenter would show the left eye
(halo_stereo_side_by_side_window), with the window's own test
(halo_stereo_window.h), over the eye's fixed frustum. The film: its picture
on the theater's default screen. The expansion: the eye's picture inside
the growing window, black on its bars. Gray around, for the room */
static void write_window_screenshot(struct render_target_entry *target)
{
	const char *directory = *config_string("debug.screenshot_directory") ?
		config_string("debug.screenshot_directory") : NULL;
	unsigned long width = target->target.gl_width, height = target->target.gl_height;
	unsigned long image_size = width * height * 4, x, y;
	struct halo_stereo_window_model model;
	unsigned char *picture, *pixels;

	if (!directory || !halo_stereo_side_by_side_window(0, &model))
		return;
	picture = malloc(image_size);
	pixels = malloc(image_size);
	if (!picture || !pixels || !gpu_texture_read(target->target.texture, picture, (uint32_t)image_size))
	{
		free(picture);
		free(pixels);
		return;
	}
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			/* the pixel's direction: the eye looks along -z at the screen */
			float u = ((float)x + 0.5f) / (float)width, v = ((float)y + 0.5f) / (float)height;
			float across = -model.tangents[0] + u * (model.tangents[0] + model.tangents[1]);
			float rise = model.tangents[2] - v * (model.tangents[2] + model.tangents[3]);
			unsigned char *out = pixels + (y * width + x) * 4;
			const unsigned char *in = NULL;
			int shown = 0;

			if (model.kind == 1)
			{
				if (across >= model.screen[0] && across <= model.screen[1] && rise >= model.screen[2] &&
					rise <= model.screen[3])
				{
					unsigned long sx = (unsigned long)((across - model.screen[0]) / (model.screen[1] - model.screen[0]) *
						(float)(width - 1) + 0.5f);
					unsigned long sy = (unsigned long)((model.screen[3] - rise) / (model.screen[3] - model.screen[2]) *
						(float)(height - 1) + 0.5f);

					in = picture + (sy * width + sx) * 4;
					shown = 1;
				}
			}
			else
				shown = halo_stereo_window_test(across, rise, -1.0f, model.window[0], model.window[1], model.window[2],
					model.window[3], model.bars);
			if (shown == 1)
			{
				if (!in)
					in = picture + (y * width + x) * 4;
				out[0] = in[0];
				out[1] = in[1];
				out[2] = in[2];
			}
			else
				out[0] = out[1] = out[2] = shown == 2 ? 0 : 0x60;
			out[3] = 0xff;
		}
	}
	screenshot_file(directory, "-window", pixels, width, height);
	free(picture);
	free(pixels);
}

/* debug.screenshot_every in the side-by-side view, on Task 12k's immersive
cutscene (debug.cutscene_immersive): what HEAD mode's presenter shows the
left eye, with its mask (halo_stereo_cutscene.h) over the eye's fixed
frustum. Inside the director's frame the eye's picture, sharp, with the HUD
layer (titles, bars) whole on the frame; outside, the picture at a quarter
of its size, blurred, desaturated and darkened by debug.cutscene_outside_dim,
eased over the soft edge. As host_stereo.m's shader does it, in C */
static void write_cutscene_screenshot(struct render_target_entry *target, struct render_target_entry *hud)
{
	const char *directory = *config_string("debug.screenshot_directory") ?
		config_string("debug.screenshot_directory") : NULL;
	unsigned long width = target->target.gl_width, height = target->target.gl_height;
	unsigned long small_width = width / HALO_STEREO_CUTSCENE_BLUR_SCALE, small_height = height / HALO_STEREO_CUTSCENE_BLUR_SCALE;
	unsigned long hud_width = hud ? hud->target.gl_width : 0, hud_height = hud ? hud->target.gl_height : 0;
	float tangents[4], forward[3], up[3], right[3], frame[2], dim, weights[HALO_STEREO_CUTSCENE_TAPS + 1], total = 0.0f;
	unsigned char *picture, *pixels, *hud_pixels = NULL;
	float *small, *pass;
	unsigned long x, y;
	int tap, pass_index, channel;

	if (!directory || !halo_stereo_side_by_side_cutscene(0, tangents) ||
		!halo_stereo_cutscene_frame(forward, up, frame, &dim) || small_width < 1 || small_height < 1)
		return;
	right[0] = forward[1] * up[2] - forward[2] * up[1];
	right[1] = forward[2] * up[0] - forward[0] * up[2];
	right[2] = forward[0] * up[1] - forward[1] * up[0];
	picture = malloc(width * height * 4);
	pixels = malloc(width * height * 4);
	small = malloc(small_width * small_height * 3 * sizeof(float));
	pass = malloc(small_width * small_height * 3 * sizeof(float));
	if (hud)
		hud_pixels = malloc(hud_width * hud_height * 4);
	if (!picture || !pixels || !small || !pass || (hud && !hud_pixels) ||
		!gpu_texture_read(target->target.texture, picture, (uint32_t)(width * height * 4)) ||
		(hud && !gpu_texture_read(hud->target.texture, hud_pixels, (uint32_t)(hud_width * hud_height * 4))))
	{
		free(picture);
		free(pixels);
		free(small);
		free(pass);
		free(hud_pixels);
		return;
	}
	/* a quarter of the size each axis, each texel the mean of its 4x4 */
	for (y = 0; y < small_height; y++)
		for (x = 0; x < small_width; x++)
			for (channel = 0; channel < 3; channel++)
			{
				float sum = 0.0f;
				unsigned long dx, dy;

				for (dy = 0; dy < HALO_STEREO_CUTSCENE_BLUR_SCALE; dy++)
					for (dx = 0; dx < HALO_STEREO_CUTSCENE_BLUR_SCALE; dx++)
						sum += picture[((y * HALO_STEREO_CUTSCENE_BLUR_SCALE + dy) * width + x *
							HALO_STEREO_CUTSCENE_BLUR_SCALE + dx) * 4 + channel];
				small[(y * small_width + x) * 3 + channel] = sum / (255.0f * HALO_STEREO_CUTSCENE_BLUR_SCALE *
					HALO_STEREO_CUTSCENE_BLUR_SCALE);
			}
	/* the separable Gaussian, across then down, clamped at the edges */
	for (tap = 0; tap <= HALO_STEREO_CUTSCENE_TAPS; tap++)
	{
		weights[tap] = expf(-(float)(tap * tap) / (2.0f * HALO_STEREO_CUTSCENE_SIGMA * HALO_STEREO_CUTSCENE_SIGMA));
		total += tap ? 2.0f * weights[tap] : weights[tap];
	}
	for (pass_index = 0; pass_index < 2; pass_index++)
	{
		float *from = pass_index == 0 ? small : pass, *to = pass_index == 0 ? pass : small;

		for (y = 0; y < small_height; y++)
			for (x = 0; x < small_width; x++)
				for (channel = 0; channel < 3; channel++)
				{
					float sum = 0.0f;

					for (tap = -HALO_STEREO_CUTSCENE_TAPS; tap <= HALO_STEREO_CUTSCENE_TAPS; tap++)
					{
						long sx = (long)x + (pass_index == 0 ? tap : 0), sy = (long)y + (pass_index == 1 ? tap : 0);

						sx = sx < 0 ? 0 : sx >= (long)small_width ? (long)small_width - 1 : sx;
						sy = sy < 0 ? 0 : sy >= (long)small_height ? (long)small_height - 1 : sy;
						sum += weights[tap < 0 ? -tap : tap] * from[((unsigned long)sy * small_width + (unsigned long)sx) * 3 +
							channel];
					}
					to[(y * small_width + x) * 3 + channel] = sum / total;
				}
	}
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			/* the pixel's direction in the eye's frame (x right, y up, z back),
			then in the director's frame's axes (z forward) */
			float u = ((float)x + 0.5f) / (float)width, v = ((float)y + 0.5f) / (float)height;
			float view[3] = { -tangents[0] + u * (tangents[0] + tangents[1]), tangents[2] - v * (tangents[2] + tangents[3]),
				-1.0f };
			float fx = view[0] * right[0] + view[1] * right[1] + view[2] * right[2];
			float fy = view[0] * up[0] + view[1] * up[1] + view[2] * up[2];
			float fz = view[0] * forward[0] + view[1] * forward[1] + view[2] * forward[2];
			float outside = halo_stereo_cutscene_outside(fx, fy, fz, frame[0], frame[1], HALO_STEREO_CUTSCENE_SOFT_EDGE);
			float blurred[3], rgb[3], dimmed[3], color[3];
			unsigned char *out = pixels + (y * width + x) * 4;
			const unsigned char *in = picture + (y * width + x) * 4;
			/* the blurred picture, bilinear */
			float sx = u * (float)small_width - 0.5f, sy = v * (float)small_height - 0.5f;
			long x0 = (long)floorf(sx), y0 = (long)floorf(sy);
			float ax = sx - (float)x0, ay = sy - (float)y0;

			for (channel = 0; channel < 3; channel++)
			{
				float corners[4];
				int corner;

				for (corner = 0; corner < 4; corner++)
				{
					long cx = x0 + (corner & 1), cy = y0 + (corner >> 1);

					cx = cx < 0 ? 0 : cx >= (long)small_width ? (long)small_width - 1 : cx;
					cy = cy < 0 ? 0 : cy >= (long)small_height ? (long)small_height - 1 : cy;
					corners[corner] = small[((unsigned long)cy * small_width + (unsigned long)cx) * 3 + channel];
				}
				blurred[channel] = (corners[0] * (1.0f - ax) + corners[1] * ax) * (1.0f - ay) +
					(corners[2] * (1.0f - ax) + corners[3] * ax) * ay;
			}
			/* (BGRA: the luma's weights take red, green, blue) */
			rgb[0] = blurred[2];
			rgb[1] = blurred[1];
			rgb[2] = blurred[0];
			halo_stereo_cutscene_dimmed(rgb, dim, dimmed);
			for (channel = 0; channel < 3; channel++)
				color[channel] = (float)in[channel] / 255.0f * (1.0f - outside) + dimmed[2 - channel] * outside;
			/* the HUD layer on the frame: premultiplied color, and in alpha the
			share of the picture that still shows */
			if (hud_pixels && fz > 0.0f && fabsf(fx / fz) <= frame[0] && fabsf(fy / fz) <= frame[1])
			{
				unsigned long hx = (unsigned long)((0.5f + 0.5f * fx / fz / frame[0]) * (float)(hud_width - 1) + 0.5f);
				unsigned long hy = (unsigned long)((0.5f - 0.5f * fy / fz / frame[1]) * (float)(hud_height - 1) + 0.5f);
				const unsigned char *h = hud_pixels + (hy * hud_width + hx) * 4;

				for (channel = 0; channel < 3; channel++)
					color[channel] = (float)h[channel] / 255.0f + color[channel] * (float)h[3] / 255.0f;
			}
			for (channel = 0; channel < 3; channel++)
				out[channel] = (unsigned char)lroundf(fminf(1.0f, fmaxf(0.0f, color[channel])) * 255.0f);
			out[3] = 0xff;
		}
	}
	screenshot_file(directory, "-cutscene", pixels, width, height);
	free(picture);
	free(pixels);
	free(small);
	free(pass);
	free(hud_pixels);
}

/* a depth target's depth as the game sees it (gpu_texture_read), or NULL if
the backend can't read it back; the caller frees it */
static float *depth_read(struct render_target_entry *target)
{
	uint32_t size = (uint32_t)(target->target.gl_width * target->target.gl_height * 4);
	float *depth = malloc(size);

	if (depth && !gpu_texture_read(target->target.texture, depth, size))
	{
		free(depth);
		depth = NULL;
	}
	return depth;
}

/* the share of a depth target's texels nothing drew (the far plane's
depth), which the Compositor treats as empty: 0..1, or -1 if the backend
can't read the depth back */
static float depth_empty_share(struct render_target_entry *target)
{
	unsigned long count = target->target.gl_width * target->target.gl_height, index, empty = 0;
	float *depth = depth_read(target);

	if (!depth || !count)
	{
		free(depth);
		return -1.0f;
	}
	for (index = 0; index < count; index++)
		empty += depth[index] >= 1.0f;
	free(depth);
	return (float)empty / (float)count;
}

/* debug.screenshot_every in a stereo frame: an eye's depth as a grayscale
frameNNNNN-left-depth.bmp. Reverse-Z, as the Compositor gets it (near is
bright); black only where nothing drew, which the Compositor treats as
empty, and anything drawn at least a dark gray, so the two can't be confused */
static void write_depth_screenshot(struct render_target_entry *target, const char *suffix)
{
	const char *directory = *config_string("debug.screenshot_directory") ?
		config_string("debug.screenshot_directory") : NULL;
	unsigned long width = target->target.gl_width, height = target->target.gl_height;
	unsigned char *pixels;
	float *depth;
	char path[512];
	FILE *file;
	unsigned long index, empty = 0;
	unsigned char header[54] = { 'B', 'M' };
	unsigned long image_size = width * height * 4;

	if (!directory || !(depth = depth_read(target)))
		return;
	pixels = malloc(image_size);
	if (!pixels)
	{
		free(depth);
		return;
	}
	for (index = 0; index < width * height; index++)
	{
		float reversed = 1.0f - depth[index];
		unsigned char gray = reversed <= 0.0f ? 0 :
			(unsigned char)(32.0f + 223.0f * sqrtf(sqrtf(reversed < 1.0f ? reversed : 1.0f)));

		empty += reversed <= 0.0f;
		pixels[index * 4] = pixels[index * 4 + 1] = pixels[index * 4 + 2] = gray;
		pixels[index * 4 + 3] = 0xff;
	}
	free(depth);
	snprintf(path, sizeof(path), "%s/frame%05lu%s.bmp", directory, device.frame, suffix);
	file = fopen(path, "wb");
	if (file)
	{
		*(unsigned int *)(header + 2) = (unsigned int)(54 + image_size);
		*(unsigned int *)(header + 10) = 54;
		*(unsigned int *)(header + 14) = 40;
		*(int *)(header + 18) = (int)width;
		*(int *)(header + 22) = -(int)height; /* rows from the top, as read */
		*(unsigned short *)(header + 26) = 1;
		*(unsigned short *)(header + 28) = 32;
		*(unsigned int *)(header + 34) = (unsigned int)image_size;
		fwrite(header, 1, sizeof(header), file);
		for (index = 0; index < height; index++)
			fwrite(pixels + index * width * 4, 1, width * 4, file);
		fclose(file);
	}
	platform_log("frame %lu: %s: %.1f%% of the depth is empty", device.frame, suffix,
		100.0f * (float)empty / (float)(width * height));
	free(pixels);
}

/* the names of the HUD's groups, for logs and screenshots (halo_stereo.h) */
static const char *hud_group_name(int group)
{
	static const char *const names[HALO_HUD_GROUP_COUNT] = { "weapon", "unit", "tracker", "prompt", "messages",
		"seats" };

	return group >= 0 && group < HALO_HUD_GROUP_COUNT ? names[group] : "none";
}

/* debug.screenshot_every in a stereo frame: a HUD target's pixels checked
against the rectangle its draws were measured to (layout lines; NULL: the
reticle's layer, which has none), logged with the box its drawn pixels
(any color, or alpha under 1) fill and how many fall outside the
rectangle, and saved as frameNNNNN-SUFFIX.bmp: the rectangle and a margin,
or for the reticle the drawn pixels' box and a margin */
static void hud_target_check(struct render_target_entry *target, const char *name, const float *rectangle,
	const char *suffix)
{
	const char *directory = *config_string("debug.screenshot_directory") ?
		config_string("debug.screenshot_directory") : NULL;
	unsigned long width = target->target.gl_width, height = target->target.gl_height, x, y;
	uint32_t size = (uint32_t)(width * height * 4);
	unsigned char *pixels = malloc(size);
	long box[4] = { (long)width, (long)height, -1, -1 }, crop[4];
	unsigned long outside = 0, drawn = 0;
	long margin = (long)ceilf(4.0f * target->target.scale[1]);
	float scale_x = target->target.scale[0], scale_y = target->target.scale[1];

	if (!pixels || !gpu_texture_read(target->target.texture, pixels, size))
	{
		free(pixels);
		return;
	}
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
		{
			const unsigned char *pixel = pixels + (y * width + x) * 4;

			if (!pixel[0] && !pixel[1] && !pixel[2] && pixel[3] == 0xff)
				continue;
			drawn++;
			box[0] = (long)x < box[0] ? (long)x : box[0];
			box[1] = (long)y < box[1] ? (long)y : box[1];
			box[2] = (long)x > box[2] ? (long)x : box[2];
			box[3] = (long)y > box[3] ? (long)y : box[3];
			/* a pixel wholly outside the rectangle (its edges are fractional) */
			if (rectangle && ((float)(x + 1) <= rectangle[0] * scale_x || (float)x >= rectangle[2] * scale_x ||
				(float)(y + 1) <= rectangle[1] * scale_y || (float)y >= rectangle[3] * scale_y))
				outside++;
		}
	if (rectangle)
		platform_log("stereo: frame %lu: HUD group %s: rectangle %.1f, %.1f to %.1f, %.1f lines; its pixels' box "
			"%.1f, %.1f to %.1f, %.1f lines (%lu pixels drawn, %lu outside the rectangle)", device.frame, name,
			rectangle[0], rectangle[1], rectangle[2], rectangle[3], drawn ? (float)box[0] / scale_x : 0.0f,
			drawn ? (float)box[1] / scale_y : 0.0f, drawn ? (float)(box[2] + 1) / scale_x : 0.0f,
			drawn ? (float)(box[3] + 1) / scale_y : 0.0f, drawn, outside);
	else
		platform_log("stereo: frame %lu: the %s layer's pixels' box %.1f, %.1f to %.1f, %.1f lines (%lu pixels drawn)",
			device.frame, name, drawn ? (float)box[0] / scale_x : 0.0f, drawn ? (float)box[1] / scale_y : 0.0f,
			drawn ? (float)(box[2] + 1) / scale_x : 0.0f, drawn ? (float)(box[3] + 1) / scale_y : 0.0f, drawn);
	if (rectangle)
	{
		crop[0] = (long)floorf(rectangle[0] * scale_x);
		crop[1] = (long)floorf(rectangle[1] * scale_y);
		crop[2] = (long)ceilf(rectangle[2] * scale_x) - 1;
		crop[3] = (long)ceilf(rectangle[3] * scale_y) - 1;
	}
	else
		memcpy(crop, box, sizeof(crop));
	crop[0] = crop[0] - margin < 0 ? 0 : crop[0] - margin;
	crop[1] = crop[1] - margin < 0 ? 0 : crop[1] - margin;
	crop[2] = crop[2] + margin >= (long)width ? (long)width - 1 : crop[2] + margin;
	crop[3] = crop[3] + margin >= (long)height ? (long)height - 1 : crop[3] + margin;
	if (directory && crop[2] >= crop[0] && crop[3] >= crop[1])
	{
		unsigned long crop_width = (unsigned long)(crop[2] - crop[0] + 1), crop_height = (unsigned long)(crop[3] - crop[1] + 1);
		unsigned long image_size = crop_width * crop_height * 4;
		unsigned char header[54] = { 'B', 'M' };
		char path[512];
		FILE *file;

		/* the layer's color over gray where its transmittance shows the
		picture, so what it covers stands out */
		for (y = 0; y < height; y++)
			for (x = 0; x < width; x++)
			{
				unsigned char *pixel = pixels + (y * width + x) * 4;
				int channel;

				for (channel = 0; channel < 3; channel++)
				{
					int value = pixel[channel] + (64 * pixel[3]) / 255;

					pixel[channel] = (unsigned char)(value > 255 ? 255 : value);
				}
				pixel[3] = 0xff;
			}
		snprintf(path, sizeof(path), "%s/frame%05lu%s.bmp", directory, device.frame, suffix);
		file = fopen(path, "wb");
		if (file)
		{
			*(unsigned int *)(header + 2) = (unsigned int)(54 + image_size);
			*(unsigned int *)(header + 10) = 54;
			*(unsigned int *)(header + 14) = 40;
			*(int *)(header + 18) = (int)crop_width;
			*(int *)(header + 22) = -(int)crop_height; /* rows from the top, as read */
			*(unsigned short *)(header + 26) = 1;
			*(unsigned short *)(header + 28) = 32;
			*(unsigned int *)(header + 34) = (unsigned int)image_size;
			fwrite(header, 1, sizeof(header), file);
			for (y = (unsigned long)crop[1]; y <= (unsigned long)crop[3]; y++)
				fwrite(pixels + (y * width + (unsigned long)crop[0]) * 4, 1, crop_width * 4, file);
			fclose(file);
		}
	}
	free(pixels);
}

/* frees the screen-sized targets of the stereo layers (the eyes and the
HUD) once stereo has stopped: the immersive space closed, and nothing
reopened it for a few seconds. Mono's stay. */
static void stereo_targets_release(void)
{
	struct render_target_entry **link = &render_targets;
	unsigned long freed = 0, bytes = 0;

	while (*link)
	{
		struct render_target_entry *entry = *link;

		if (entry->layer == HALO_STEREO_LAYER_MONO)
		{
			link = &entry->next;
			continue;
		}
		{
			struct render_target_entry **bucket = render_target_bucket(entry->target.data);

			while (*bucket != entry)
				bucket = &(*bucket)->next_in_bucket;
			*bucket = entry->next_in_bucket;
		}
		*link = entry->next;
		bytes += entry->target.allocated_width * entry->target.allocated_height * (entry->target.depth ? 5 : 4);
		gpu_texture_destroy(entry->target.texture);
		free(entry);
		freed++;
	}
	if (freed)
		platform_log("stereo: stereo stopped; freed %lu eye and HUD targets (%lu MB)", freed, bytes >> 20);
}

/* the back buffer's texture for a stereo layer if something drew it this
frame, else NULL (without creating it) */
static struct render_target_entry *back_buffer_drawn_this_frame(int layer)
{
	struct render_target_entry *entry;

	for (entry = *render_target_bucket(device.back_buffer.Data); entry; entry = entry->next_in_bucket)
	{
		if (entry->target.data == device.back_buffer.Data && entry->layer == layer &&
			entry->last_rendered == device.frame + 1)
			return entry;
	}
	return NULL;
}

void WINAPI D3DDevice_Present(CONST RECT *source_rectangle, CONST RECT *destination_rectangle,
	void *unused, void *unused2)
{
	static long screenshot_every = -1;

	(void)source_rectangle;
	(void)destination_rectangle;
	(void)unused;
	(void)unused2;
	if (screenshot_every < 0)
		screenshot_every = config_integer("debug.screenshot_every");

	if (device.gl_ready)
	{
		const struct halo_stereo_frame *stereo = halo_stereo_frame();
		/* a frame presents in stereo only if its eyes, or the zoomed picture
		in their place (halo_stereo_zoom), were drawn: the menus, loading
		screens and any frame without an eye pass (render_player_frame_stereo)
		draw everything in the mono layer, which gpu_present_stereo never
		reads, so they present mono as before */
		struct render_target_entry *zoom = stereo->eye_count == 2 && halo_stereo_zoom() ?
			back_buffer_drawn_this_frame(HALO_STEREO_LAYER_ZOOM) : NULL;
		int stereo_frame = stereo->eye_count == 2 && (back_buffer_drawn_this_frame(0) || zoom);
		static unsigned long frames_without_eyes;

		/* a few seconds without the Compositor's eyes (the space closed):
		the stereo layers' targets go */
		if (stereo->eye_count == 2)
			frames_without_eyes = 0;
		else if (stereo->mode != HALO_STEREO_OFF && ++frames_without_eyes == 300)
			stereo_targets_release();
		/* a stereo frame's screenshot and trace are of eye 0, or the zoomed
		picture in a zoomed frame */
		struct render_target_entry *back_buffer = zoom ? zoom : render_target_get_layer(&device.back_buffer,
			stereo_frame ? 0 : HALO_STEREO_LAYER_MONO);
		struct render_target_entry *hud = stereo_frame ? back_buffer_drawn_this_frame(HALO_STEREO_LAYER_HUD) : NULL;
		/* the reticle's layer and each HUD group's target, if drawn this
		frame (halo_stereo.h) */
		struct render_target_entry *reticle = stereo_frame ? back_buffer_drawn_this_frame(HALO_STEREO_LAYER_RETICLE) : NULL;
		/* the UI layer (a menu, a help panel, the console, a progress bar in
		HEAD mode's full view), if drawn this frame */
		struct render_target_entry *ui = stereo_frame ? back_buffer_drawn_this_frame(HALO_STEREO_LAYER_UI) : NULL;
		struct render_target_entry *hud_groups[HALO_HUD_GROUP_COUNT];
		float hud_group_rectangles[HALO_HUD_GROUP_COUNT][4];
		int group;

		for (group = 0; group < HALO_HUD_GROUP_COUNT; group++)
		{
			hud_groups[group] = stereo_frame ?
				back_buffer_drawn_this_frame(HALO_STEREO_LAYER_HUD_GROUP + group) : NULL;
			/* a group the HUD pass didn't measure (its target drew only a
			flash, or a later pass's draw) shows nothing */
			if (!halo_hud_group_rectangle(group, hud_group_rectangles[group]))
				hud_groups[group] = NULL;
		}

		/* Custom screens gather shapes/text while the game draws. Composite
		them once per frame; this also makes screenshots include the UI. */
		macos_metal_overlay_draw(back_buffer->target.texture);
		if (trace_frame())
			platform_log("present back buffer %08lx texture %u", (unsigned long)device.back_buffer.Data,
				back_buffer->target.texture);
		if (screenshot_every > 0 && device.frame % (unsigned long)screenshot_every == 0)
		{
			write_screenshot(back_buffer, "");
			/* and each picture of a stereo frame, to free-view or inspect: the
			eyes', or a zoomed frame's one picture for both (its eyes weren't
			drawn) */
			if (stereo_frame)
			{
				if (zoom)
					write_screenshot(zoom, "-zoom");
				else
				{
					write_screenshot(back_buffer, "-left");
					write_screenshot(render_target_get_layer(&device.back_buffer, 1), "-right");
					write_depth_screenshot(render_target_get_layer(&device.depth_buffer, 0), "-left-depth");
					write_window_screenshot(back_buffer);
					write_cutscene_screenshot(back_buffer, hud);
				}
				if (hud)
				{
					write_screenshot(hud, "-hud");
					write_screenshot_channel(hud, "-hud-alpha", 1);
				}
				if (ui)
				{
					write_screenshot(ui, "-ui");
					write_screenshot_channel(ui, "-ui-alpha", 1);
				}
				/* the reticle's layer and each HUD group's target, cropped,
				with their pixels checked against their rectangles */
				if (reticle)
					hud_target_check(reticle, "reticle", NULL, "-reticle");
				for (group = 0; group < HALO_HUD_GROUP_COUNT; group++)
					if (hud_groups[group])
					{
						char suffix[32];

						snprintf(suffix, sizeof(suffix), "-hud-%s", hud_group_name(group));
						hud_target_check(hud_groups[group], hud_group_name(group), hud_group_rectangles[group], suffix);
					}
				if (hud)
				{
					float none[4];

					halo_hud_group_rectangle(HALO_HUD_GROUP_NONE, none);
					hud_target_check(hud, "none", none, "-hud-none");
				}
			}
		}
		if (stereo_frame)
		{
			struct gpu_stereo_present present = { 0 };
			int eye;
			static unsigned long depth_checks;

			/* debug.gpu_stats: how much of the eye depth the game drew is empty
			(before the presenter's floor, host_stereo.m), on the first stereo
			frame and every 900th after: walls and floors should leave none
			outside the sky, so a nonzero share in an enclosed space means
			static geometry lost its depth (headset session 1) */
			if (debug_settings.statistics && !zoom && depth_checks++ % 900 == 0)
			{
				float share = depth_empty_share(render_target_get_layer(&device.depth_buffer, 0));

				if (share >= 0.0f)
					platform_log("stereo: frame %lu: the left eye depth is %.1f%% empty (stereo frame %lu)",
						device.frame, 100.0f * share, depth_checks);
				else
					platform_log("stereo: the eye depth can't be read back");
			}

			for (eye = 0; eye < 2; eye++)
			{
				present.eye_color[eye] = render_target_get_layer(&device.back_buffer, eye)->target.texture;
				present.eye_depth[eye] = render_target_get_layer(&device.depth_buffer, eye)->target.texture;
			}
			/* the HUD's texture exists only once something drew it, and is
			passed only if that was this frame */
			if (hud)
				present.hud = hud->target.texture;
			/* the crosshairs' layer, and the HUD's groups with their
			rectangles (halo_stereo.h) */
			if (reticle)
				present.reticle_layer = reticle->target.texture;
			for (group = 0; group < HALO_HUD_GROUP_COUNT; group++)
				if (hud_groups[group])
				{
					present.hud_group[group] = hud_groups[group]->target.texture;
					memcpy(present.hud_group_extent[group], hud_group_rectangles[group],
						sizeof(present.hud_group_extent[group]));
				}
			/* debug.gpu_stats: each group's rectangle, once a second of game
			time (30 frames) */
			if (debug_settings.statistics && device.frame % 30 == 0)
			{
				char line[512];
				int length = 0;

				for (group = HALO_HUD_GROUP_NONE; group < HALO_HUD_GROUP_COUNT; group++)
				{
					float rectangle[4];

					if (halo_hud_group_rectangle(group, rectangle))
						length += snprintf(line + length, sizeof(line) - (size_t)length, "%s%s %.1f,%.1f-%.1f,%.1f",
							length ? "; " : "", hud_group_name(group), rectangle[0], rectangle[1], rectangle[2],
							rectangle[3]);
					else
						length += snprintf(line + length, sizeof(line) - (size_t)length, "%s%s empty",
							length ? "; " : "", hud_group_name(group));
					if (length >= (int)sizeof(line))
						length = (int)sizeof(line) - 1;
				}
				platform_log("stereo: frame %lu: HUD groups (lines): %s; reticle layer %s; %lu HUD draws measured, "
					"%lu taken as the viewport", device.frame, line, reticle ? "drawn" : "empty", hud_draws_measured,
					hud_draws_unmeasured);
				hud_draws_measured = hud_draws_unmeasured = 0;
			}
			/* the zoomed picture in the eyes' place, only if its pass ran this
			frame (the zoomed view's elements still go to its layer while the
			pass waits under a menu, halo_stereo_zoom_begin), and the view it
			spans */
			if (zoom)
			{
				present.zoom = zoom->target.texture;
				halo_stereo_zoom_view(present.zoom_tangents);
			}
			/* the eyes' planes (the eye loop's, halo_stereo_set_depth_range), in
			meters: one world unit is 3.048 m */
			halo_stereo_depth_range(&present.near_meters, &present.far_meters);
			present.near_meters *= 3.048f;
			present.far_meters *= 3.048f;
			present.mode = stereo->mode;
			/* the HUD's shape as the game lays it out: its texture's pixels
			needn't be square (screen_mode_choose) */
			present.hud_aspect = (float)halo_screen_width() / (float)SCREEN_HEIGHT;
			/* a cutscene: the 3D film on the screen, whatever the mode; the
			script fade the eyes drew (render.c), which tints the space around
			the screen in step with the picture; and whether it covers a cut */
			present.cinematic = halo_stereo_film();
			halo_stereo_fade(present.fade);
			present.cut_covered = halo_stereo_cut_covered();
			/* and whether the full view is held black over a seat's exit glide */
			present.cut_requested = halo_stereo_cut_requested();
			/* input.comfort_vignette, while the stick turns the look */
			present.vignette = halo_stereo_vignette();
			/* HEAD mode's UI: in the full view, the UI layer on the UI's
			quad, the HUD staying in its pieces, and the eyes darkened by the
			widgets' dim, which isn't drawn there; elsewhere the whole HUD
			layer on the UI's quad while a menu, the console or a progress
			bar drew into it, except over a cutscene's film, where the menu
			stays on the screen with the frozen film. Where the crosshair
			points; and the HUD pass's projection, for the catch-all quad */
			if (ui)
				present.ui = ui->target.texture;
			present.ui_dim = ui_dim;
			/* each change of the dim, to the hundredth */
			{
				static int ui_dim_logged = 0;
				int hundredths = (int)lroundf(ui_dim * 100.0f);

				if (hundredths != ui_dim_logged)
				{
					platform_log("stereo: frame %lu: the widgets' dim darkens the eyes by %.2f%s", device.frame,
						ui_dim, ui ? "; the UI layer on the UI's quad" : "");
					ui_dim_logged = hundredths;
				}
			}
			/* HEAD mode's cutscene window, expanding from the film out to
			the full view */
			present.expanding = halo_stereo_expansion(&present.expansion, &present.expansion_bars);
			/* Task 12k's immersive cutscene: the director's frame, for the mask */
			present.cutscene = halo_stereo_cutscene_frame(present.cutscene_forward, present.cutscene_up,
				present.cutscene_tangents, &present.cutscene_dim);
			present.hud_ui = ui ? 1 : hud && hud_layer_ui && !halo_stereo_film_letterbox();
			/* the next frame's zoomed pass waits while a menu holds the layer */
			halo_stereo_set_ui_shown(present.hud_ui);
			halo_stereo_reticle(present.reticle);
			halo_stereo_hud_tangents(present.hud_tangents);
			render_interpolation_next_frame_due(gpu_present_stereo(&present));
		}
		else
			render_interpolation_next_frame_due(gpu_present(back_buffer->target.texture));
		hud_layer_ui = FALSE;
		ui_dim = 0.0f;
		xgpu_texture_cache_begin_frame();
		shader_list_take_pipelines();
	}
	device.frame++;
	input_replay_frame(lroundf(screen_width * screen_scale[0]), lroundf(SCREEN_HEIGHT * screen_scale[1]));
	/* debug.fixed_timestep: a frame ends once the workers are idle */
	platform_quiescence_wait();
	platform_clock_frame();
	stats.presents++;
	if (debug_settings.statistics && device.frame % 60 == 0)
	{
		unsigned long calls = gpu_call_count_take();

		platform_log("frame %lu: %lu draws, %lu immediate, %lu clears, %lu target changes; skipped %lu no program, %lu no target, %lu link; "
			"%lu KB mirrored, %lu KB streamed, %lu GL calls",
			device.frame, stats.draws / stats.presents, stats.immediate_draws / stats.presents, stats.clears / stats.presents,
			stats.target_changes / stats.presents, stats.skipped_no_program, stats.skipped_no_target, stats.skipped_link,
			stats.mirrored_bytes / stats.presents / 1024, stats.streamed_bytes / stats.presents / 1024,
			calls / stats.presents);
		memset(&stats, 0, sizeof(stats));
		/* and how stereo's HUD layer took its draws (hud_layer_blend) */
		if (hud_layer_exact || hud_layer_inexact)
			platform_log("stereo: the HUD layer's last 60 frames: %lu draws with exact transmittance, %lu without",
				hud_layer_exact, hud_layer_inexact);
		hud_layer_exact = hud_layer_inexact = 0;
		{
			unsigned long models_drawn[5], models_culled;

			halo_model_counts_take(models_drawn, &models_culled);
			platform_log("models: the last 60 frames: %lu drawn (detail level 0, the lowest, to 4: %lu %lu %lu %lu %lu), "
				"%lu culled by size", models_drawn[0] + models_drawn[1] + models_drawn[2] + models_drawn[3] +
				models_drawn[4], models_drawn[0], models_drawn[1], models_drawn[2], models_drawn[3], models_drawn[4],
				models_culled);
		}
	}
	platform_pump_events();

	pthread_mutex_lock(&vertical_blank_lock);
	/* the Xbox keeps at most two frames queued behind its 60 Hz display;
	with interpolation, frames come at the real display's rate instead,
	paced by vsync (platform_video_swap); a frame with stereo's eyes is
	paced by the Compositor's frames (host_theater_frame_begin waits for
	each), at 90 Hz or the rate display.frame_repeat sets, so the Xbox's
	60 Hz queue never holds it back, interpolation or not */
	if (halo_interpolation_enabled() || halo_stereo_frame()->eye_count == 2)
	{
		flip_count++;
	}
	else
	{
		while (pending_flips >= 2)
			pthread_cond_wait(&vertical_blank_condition, &vertical_blank_lock);
		pending_flips++;
	}
	pthread_mutex_unlock(&vertical_blank_lock);
}

HRESULT WINAPI D3DDevice_PersistDisplay(void)
{
	return S_OK;
}
