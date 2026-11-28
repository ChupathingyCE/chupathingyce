/*
H2_LOADING.C

Halo 2's loading screen for a Halo 2 level (h2_loading.h), as the Halo 2
Vista build shows it (the owner's reference footage, JWavBAGGbXk.webm; the
Xbox's, ovuLn9VR9OE.mkv, is the same with a wider beam over a bluer plate):
black, the HALO 2 logo centred, and a vertical beam of light sweeping from the
logo's left end to its right. Where the beam passes it lights the blue circuit
plate and the rings behind the logo (they are dark again once it is gone); its
own brightness rises to a peak in the middle of the sweep and falls again, and
the whole screen ends in a fade. From the Vista footage (30 frames a second,
14 s): the logo alone for the first second, the beam at the logo's left end at
1 s and its right end at 11 s (about 80 pixels a second at 1280 wide), a
peak at 6.4 s, then the fade to black by 13.8 s.

The sweep's place follows the load's progress; the screen is let go the moment
the load is done (nothing is held for the animation to finish). A load that
runs long holds the beam at the right end for a moment, fades, and sweeps
again, as the video loops.

The art is the player's own (globals\loading_screen in mainmenu.map through
textures.dat: the plate, the logo, the two spinner rings, h2_cache.c), made
textures when the screen comes up and let go when the level is in; the beam is
made here. It draws additively with the screen quad the menus and the HUD use
(alpha blend and add, as hud_draw.c uses them), so it needs no shader the
Vita's shaders.pak lacks.
*/

#include "cseries.h"
#include "h2_loading.h"
#include "h2_level.h"
#include "h2_cache.h"
#include "math/real_math.h"
#include "h2_campaign.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmaps_internal.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "cseries_windows.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (this file's pixels are the C library's, as h2_map_picture.c's are) */
#undef malloc
#undef free
#undef calloc

void platform_log(const char *format, ...);

/* ---------- constants */

enum
{
#ifdef HALO_VITA
	/* (the plate, rings and logo at a half and a quarter of their texels) */
	H2L_PLATE_REDUCE = 2,
	H2L_RING_REDUCE = 4,
	H2L_LOGO_REDUCE = 2,
	H2L_STRIPS = 12,
#else
	H2L_PLATE_REDUCE = 1,
	H2L_RING_REDUCE = 2,
	H2L_LOGO_REDUCE = 1,
	H2L_STRIPS = 28,
#endif
	H2L_BEAM_SIZE = 128,
	H2L_BLEND_ALPHA = 0,
	H2L_BLEND_ADD = 3,   /* _shader_framebuffer_blend_function_add */
	/* the Vista footage's timeline, milliseconds: the logo comes up in the
	first second, the end of the sweep to the fade */
	H2L_INTRO_MS = 800,
	H2L_HOLD_MS = 1500,
	H2L_FADE_MS = 2400,
};

/* the beam's travel, as fractions of the width: the logo's left and right ends */
#define H2L_BEAM_LEFT 0.17f
#define H2L_BEAM_RIGHT 0.80f
/* the sweep's top speed, of its length a second (the video's: 10 s for it) */
#define H2L_SWEEP_SPEED 0.30f

/* ---------- globals */

static struct h2l_screen
{
	unsigned long started;
	struct bitmap_data *plate, *logo, *rings[2], *beam, *white;
	long plate_w, plate_h, logo_w, logo_h, ring_w;
	/* the sweep as drawn (0..1), when it reached its end, and the time of the last frame */
	real shown;
	unsigned long arrived, last;
	/* the progress last given (the reveal goes on from it) */
	real given;
	boolean looping;
	/* tests */
	real shot_progress;
	unsigned long shot_since;
	long shots, snaps;
} h2l;

static struct
{
	boolean prepared, wanted;
	char name[64];
	boolean active;
} h2c;

/* (the reveal: below) */
static struct
{
	boolean covering, watching, no_player;
	unsigned long began, unit_ms, settled_ms, frames, quiet;
	/* (the screen's progress when the level came in) */
	real from;
	/* (the screen's frames drawn over the level's: the longest gap) */
	unsigned long cover_last, cover_longest;
	long busy_ms;
	/* (the last frame each kind held up: the texture cache's stand-ins,
	background decodes, detail reads, derived bitmaps, emblems) */
	long kind_ms[5];
	long frame_seen;
	unsigned long at_begin[10], last[10];
} h2r;

static void h2_loading_reveal_now(char const *why);
static void vita_texture_settling_set(int on);


/* ---------- private code: pixels */

static struct bitmap_data *h2l_texture(unsigned char *pixels, long width, long height)
{
	struct bitmap_data *bitmap = calloc(1, sizeof(*bitmap));

	if (!bitmap)
		return NULL;
	bitmap->signature = BITMAP_GROUP_TAG;
	bitmap->width = (short)width;
	bitmap->height = (short)height;
	bitmap->depth = 1;
	bitmap->type = 0;
	bitmap->format = 11;   /* (A8R8G8B8) */
	bitmap->flags = FLAG(0);
	bitmap->tag_index = NONE;
	bitmap->cache_block_index = NONE;
	bitmap->base_address = pixels;
	if (!rasterizer_bitmap_new(bitmap) || !bitmap->hardware_format)
	{
		bitmap->base_address = NULL;
		free(bitmap);
		return NULL;
	}
	rasterizer_bitmap_changed(bitmap);
	bitmap->base_address = NULL;
	return bitmap;
}

static void h2l_texture_free(struct bitmap_data **bitmap)
{
	if (*bitmap)
	{
		rasterizer_bitmap_delete(*bitmap);
		free(*bitmap);
		*bitmap = NULL;
	}
}

static unsigned long h2l_read32(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned long h2l_read16(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8);
}

static void h2l_565(unsigned long c, unsigned char *out)
{
	/* (B, G, R) */
	out[2] = (unsigned char)(((c >> 11) & 31) * 255 / 31);
	out[1] = (unsigned char)(((c >> 5) & 63) * 255 / 63);
	out[0] = (unsigned char)((c & 31) * 255 / 31);
}

/* a DXT1 (kind 1) or DXT3 (kind 3) image decoded to B G R A, `reduce` times
smaller a side (box filtered): the caller frees */
static unsigned char *h2l_dxt(unsigned char const *data, unsigned long size, long w, long h, int kind, long reduce)
{
	long block = kind == 1 ? 8 : 16, bx, by, x, y, c, ow = w / reduce, oh = h / reduce;
	unsigned char *full, *out;

	if (w < 4 || h < 4 || (w & 3) || (h & 3) || size < (unsigned long)((w / 4) * (h / 4) * block))
		return NULL;
	full = malloc((size_t)(w * h * 4));
	if (!full)
		return NULL;
	for (by = 0; by < h / 4; by++)
	{
		for (bx = 0; bx < w / 4; bx++)
		{
			unsigned char const *b = data + (by * (w / 4) + bx) * block;
			unsigned char const *cb = b + (kind == 1 ? 0 : 8);
			unsigned long c0 = h2l_read16(cb), c1 = h2l_read16(cb + 2), bits = h2l_read32(cb + 4);
			unsigned char col[4][4];
			long i;

			h2l_565(c0, col[0]);
			h2l_565(c1, col[1]);
			for (c = 0; c < 3; c++)
			{
				if (c0 > c1 || kind == 3)
				{
					col[2][c] = (unsigned char)((2 * col[0][c] + col[1][c]) / 3);
					col[3][c] = (unsigned char)((col[0][c] + 2 * col[1][c]) / 3);
				}
				else
				{
					col[2][c] = (unsigned char)((col[0][c] + col[1][c]) / 2);
					col[3][c] = 0;
				}
			}
			for (i = 0; i < 16; i++)
			{
				unsigned char *p = full + 4 * ((by * 4 + i / 4) * w + bx * 4 + i % 4);
				unsigned char const *pick = col[(bits >> (2 * i)) & 3];
				unsigned alpha = 255;

				if (kind == 3)
					alpha = ((b[i / 2] >> (4 * (i & 1))) & 15) * 17;
				p[0] = pick[0];
				p[1] = pick[1];
				p[2] = pick[2];
				p[3] = (unsigned char)alpha;
			}
		}
	}
	if (reduce <= 1)
		return full;
	out = malloc((size_t)(ow * oh * 4));
	if (!out)
	{
		free(full);
		return NULL;
	}
	for (y = 0; y < oh; y++)
	{
		for (x = 0; x < ow; x++)
		{
			for (c = 0; c < 4; c++)
			{
				long sum = 0, i, j;

				for (j = 0; j < reduce; j++)
					for (i = 0; i < reduce; i++)
						sum += full[4 * ((y * reduce + j) * w + x * reduce + i) + c];
				out[4 * (y * ow + x) + c] = (unsigned char)(sum / (reduce * reduce));
			}
		}
	}
	free(full);
	return out;
}

/* a bitmap tag's image of the cache, decoded (DXT1 or DXT3): its size in
*w, *h (before the reduction); NULL if it is not there */
static unsigned char *h2l_image(struct h2_cache *cache, char const *name, long image, long reduce, long *w, long *h)
{
	long tag = h2_cache_tag_find(cache, 'bitm', name), count;
	struct h2_cache_tag t;
	unsigned long images, reference, size, got = 0, format;
	unsigned char const *header;
	unsigned char *pixels, *decoded;

	if (tag == NONE || !h2_cache_is_mcc(cache) || !h2_cache_tag_get(cache, tag, &t) ||
		!h2_cache_block(cache, t.address + 0x44, 0xA8, &count, &images) || image >= count ||
		!(header = h2_cache_at(cache, images + 0xA8 * (unsigned long)image, 0xA8)))
	{
		return NULL;
	}
	*w = (long)h2l_read16(header + 4);
	*h = (long)h2l_read16(header + 6);
	format = h2l_read16(header + 0xC);
	reference = h2l_read32(header + 0x38);
	size = h2l_read32(header + 0x50);
	if (format != 14 && format != 15)
		return NULL;
	pixels = h2_cache_texture_read(cache, reference, size, &got);
	if (!pixels)
		return NULL;
	decoded = h2l_dxt(pixels, got, *w, *h, format == 14 ? 1 : 3, reduce);
	free(pixels);
	return decoded;
}

/* ---------- private code: drawing */

static pixel32 h2l_color(real alpha, real red, real green, real blue)
{
	real const channels[4] = { alpha, red, green, blue };
	unsigned long value = 0;
	int k;

	for (k = 0; k < 4; k++)
	{
		real c = channels[k] < 0.0f ? 0.0f : channels[k] > 1.0f ? 1.0f : channels[k];

		value = (value << 8) | (unsigned long)(c * 255.0f + 0.5f);
	}
	return (pixel32)value;
}

/* a bitmap over an upright rectangle with a colour at each corner (top
left, top right, bottom right, bottom left) */
static void h2l_rect4(struct bitmap_data *bitmap, real x0, real y0, real x1, real y1, real u0, real v0, real u1, real v1,
	pixel32 const color[4], short blend)
{
	struct dynamic_screen_vertex vertices[4];
	struct rasterizer_dynamic_screen_geometry_parameters parameters;
	short k;

	for (k = 0; k < 4; k++)
	{
		vertices[k].position.x = (k == 0 || k == 3) ? x0 : x1;
		vertices[k].position.y = k < 2 ? y0 : y1;
		vertices[k].texture_coordinates.x = (k == 0 || k == 3) ? u0 : u1;
		vertices[k].texture_coordinates.y = k < 2 ? v0 : v1;
		vertices[k].color = color[k];
	}
	csmemset(&parameters, 0, sizeof(parameters));
	parameters.map_texture_scale[0].i = parameters.map_texture_scale[0].j = 1.0f;
	parameters.map_scale[0].i = parameters.map_scale[0].j = 1.0f;
	parameters.framebuffer_blend_function = blend;
	parameters.map[0] = bitmap;
	rasterizer_psuedo_dynamic_screen_quad_draw(&parameters, vertices);
}

static void h2l_rect(struct bitmap_data *bitmap, real x0, real y0, real x1, real y1, pixel32 color, short blend)
{
	pixel32 c[4] = { color, color, color, color };

	h2l_rect4(bitmap, x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, c, blend);
}

/* the light the beam throws at x: a bell about it */
static real h2l_lit(real x, real beam_x, real reach)
{
	real d = (x - beam_x) / reach;

	return (real)exp(-d * d);
}

/* a bitmap over a rectangle lit only about the beam (the colour fading with
the distance from it, in strips so that the corners carry it), added */
static void h2l_lit_rect(struct bitmap_data *bitmap, real x0, real y0, real x1, real y1, real beam_x, real reach, real level, real red,
	real green, real blue)
{
	long k;

	/* (only the strips the light reaches) */
	for (k = 0; k < H2L_STRIPS; k++)
	{
		real xa = x0 + (x1 - x0) * (real)k / (real)H2L_STRIPS, xb = x0 + (x1 - x0) * (real)(k + 1) / (real)H2L_STRIPS;
		real la = level * h2l_lit(xa, beam_x, reach), lb = level * h2l_lit(xb, beam_x, reach);
		pixel32 c[4];

		if (la < 0.004f && lb < 0.004f)
			continue;
		c[0] = c[3] = h2l_color(la, red * la, green * la, blue * la);
		c[1] = c[2] = h2l_color(lb, red * lb, green * lb, blue * lb);
		h2l_rect4(bitmap, xa, y0, xb, y1, (real)k / (real)H2L_STRIPS, 0.0f, (real)(k + 1) / (real)H2L_STRIPS, 1.0f, c, H2L_BLEND_ADD);
	}
}

/* the beam's picture: a bright narrow core and a wide soft halo across, a
streak of light along the middle, all fading toward the top and bottom */
static struct bitmap_data *h2l_beam_make(void)
{
	unsigned char *pixels = malloc((size_t)(H2L_BEAM_SIZE * H2L_BEAM_SIZE * 4));
	struct bitmap_data *bitmap;
	long x, y;

	if (!pixels)
		return NULL;
	for (y = 0; y < H2L_BEAM_SIZE; y++)
	{
		for (x = 0; x < H2L_BEAM_SIZE; x++)
		{
			real u = ((real)x + 0.5f) / (real)H2L_BEAM_SIZE * 2.0f - 1.0f, v = ((real)y + 0.5f) / (real)H2L_BEAM_SIZE * 2.0f - 1.0f;
			real core = (real)exp(-(u / 0.12f) * (u / 0.12f)), halo = 0.34f * (real)exp(-(u / 0.34f) * (u / 0.34f));
			real taper = (real)exp(-(v / 0.62f) * (v / 0.62f) * (v / 0.62f) * (v / 0.62f));
			real streak = 0.55f * (real)exp(-(v / 0.045f) * (v / 0.045f)) * (real)exp(-(u / 0.8f) * (u / 0.8f));
			real value = (core + halo) * taper + streak;
			unsigned char *p = pixels + 4 * (y * H2L_BEAM_SIZE + x);

			value = value > 1.0f ? 1.0f : value;
			p[0] = 255;
			p[1] = (unsigned char)(value * 235.0f);
			p[2] = (unsigned char)(value * 215.0f);
			p[3] = (unsigned char)(value * 255.0f);
			/* (white at its heart, blue-violet in the halo) */
			p[0] = (unsigned char)(value * 255.0f);
			p[1] = (unsigned char)(value * (205.0f + 40.0f * core));
			p[2] = (unsigned char)(value * (170.0f + 80.0f * core));
		}
	}
	bitmap = h2l_texture(pixels, H2L_BEAM_SIZE, H2L_BEAM_SIZE);
	free(pixels);
	return bitmap;
}

/* ---------- private code: the art */

static void h2l_let_go(void)
{
	h2l_texture_free(&h2l.plate);
	h2l_texture_free(&h2l.logo);
	h2l_texture_free(&h2l.rings[0]);
	h2l_texture_free(&h2l.rings[1]);
	h2l_texture_free(&h2l.beam);
	h2l_texture_free(&h2l.white);
}

/* mainmenu.map's loading screen art made textures: FALSE if not all there */
static boolean h2l_art(void)
{
	char why[H2_CACHE_WHY_SIZE];
	struct h2_cache *menu = h2_cache_open(h2_level_maps_directory(), "mainmenu", why, sizeof(why));
	long w, h, k;
	unsigned char *pixels;
	boolean ok = FALSE;

	if (!menu)
	{
		platform_log("h2 loading: mainmenu.map: %s", why);
		return FALSE;
	}
	if (!(pixels = h2l_image(menu, "globals\\loading_screen\\loading_screen", 0, H2L_PLATE_REDUCE, &w, &h)))
		goto done;
	h2l.plate_w = w / H2L_PLATE_REDUCE;
	h2l.plate_h = h / H2L_PLATE_REDUCE;
	h2l.plate = h2l_texture(pixels, h2l.plate_w, h2l.plate_h);
	free(pixels);
	if (!h2l.plate || !(pixels = h2l_image(menu, "globals\\loading_screen\\loading_screen_logos", 0, H2L_LOGO_REDUCE, &w, &h)))
		goto done;
	h2l.logo_w = w / H2L_LOGO_REDUCE;
	h2l.logo_h = h / H2L_LOGO_REDUCE;
	h2l.logo = h2l_texture(pixels, h2l.logo_w, h2l.logo_h);
	free(pixels);
	for (k = 0; k < 2; k++)
	{
		if (!(pixels = h2l_image(menu, "globals\\loading_screen\\loading_screen_spinners", k, H2L_RING_REDUCE, &w, &h)))
			goto done;
		h2l.ring_w = w / H2L_RING_REDUCE;
		h2l.rings[k] = h2l_texture(pixels, h2l.ring_w, h / H2L_RING_REDUCE);
		free(pixels);
		if (!h2l.rings[k])
			goto done;
	}
	{
		unsigned char white[4 * 4 * 4];

		memset(white, 255, sizeof(white));
		h2l.white = h2l_texture(white, 4, 4);
	}
	h2l.beam = h2l_beam_make();
	ok = h2l.logo && h2l.white && h2l.beam;
done:
	h2_cache_close(menu);
	if (!ok)
		platform_log("h2 loading: the loading screen's art is not in mainmenu.map (the plate, logo or rings)");
	return ok;
}

/* ---------- public code */

void h2_loading_prepare(char const *map_name)
{
	char const *base, *p;
	size_t length;

	h2c.prepared = TRUE;
	h2c.wanted = FALSE;
	h2c.name[0] = 0;
	if (!map_name || !map_name[0] || (getenv("HALO_H2_LOADING") && !strcmp(getenv("HALO_H2_LOADING"), "0")))
		return;
	/* (a Halo 2 level reaches the engine as levels\test\<name>\<name>, the
	scenario of the level built from the maps folder's <name>.map: a Halo 2
	map there, rather than a Halo CE map of that name) */
	base = map_name;
	for (p = map_name; *p; p++)
	{
		if (*p == '\\' || *p == '/')
			base = p + 1;
	}
	length = strlen(base);
	if (length > 4 && !strcmp(base + length - 4, ".map"))
		length -= 4;
	if (!length || length >= sizeof(h2c.name) || !h2_level_maps_directory()[0])
		return;
	memcpy(h2c.name, base, length);
	h2c.name[length] = 0;
	if (!h2_level_is_halo2_map(h2_level_maps_directory(), h2c.name))
	{
		h2c.name[0] = 0;
		return;
	}
	h2c.wanted = TRUE;
}

boolean h2_loading_begin(void)
{
	unsigned long began = system_milliseconds();

	/* (a load begun while the last level's first frames were still under
	the screen: that screen taken down first) */
	if (h2r.covering)
		h2_loading_reveal_now("another load began");
	if (!h2c.prepared || !h2c.wanted || !h2_level_maps_directory()[0])
		return FALSE;
	h2c.wanted = FALSE;
	memset(&h2l, 0, sizeof(h2l));
	if (!h2l_art())
	{
		h2l_let_go();
		return FALSE;
	}
	h2c.active = TRUE;
	h2l.started = h2l.last = system_milliseconds();
	h2l.shot_progress = -1.0f;
	platform_log("h2 loading: %s: art in %lu ms", h2c.name, h2l.started - began);
	return TRUE;
}

boolean h2_loading_active(void)
{
	return h2c.active;
}

void h2_loading_draw(real progress)
{
	extern char halo_screenshot_name[64];
	unsigned long now = system_milliseconds(), since = now - h2l.started;
	real vx0 = (real)render.camera.viewport_bounds.x0, vx1 = (real)render.camera.viewport_bounds.x1;
	real vy0 = (real)render.camera.viewport_bounds.y0, vy1 = (real)render.camera.viewport_bounds.y1;
	real w = vx1 - vx0, h = vy1 - vy0, cx = 0.5f * (vx0 + vx1), cy = 0.5f * (vy0 + vy1);
	real s, intro, fade = 1.0f, env, beam_x, dt = (real)(now - h2l.last) / 1000.0f;
	char const *shots = getenv("HALO_H2_LOADING_SHOTS");
	boolean test = shots && shots[0];
	pixel32 white = h2l_color(1.0f, 1.0f, 1.0f, 1.0f);

	if (!h2c.active)
		return;
	h2l.last = now;
	/* the sweep: where the load is, no faster than the video's, held at
	the end and then, as the video loops, faded and begun again */
	if (test)
	{
		/* (tests: the progress given is the sweep's place, over 1 the fade) */
		s = progress > 1.0f ? 1.0f : progress;
		if (progress > 1.0f)
			fade = 1.0f - (progress - 1.0f) * 2.0f;
		h2l.shown = s;
	}
	else
	{
		real target = progress / 0.97f > 1.0f ? 1.0f : progress / 0.97f;

		h2l.given = progress;
		real step = H2L_SWEEP_SPEED * dt;

		if (h2l.looping)
		{
			real f = (real)(now - h2l.arrived) / (real)(H2L_FADE_MS - H2L_HOLD_MS);

			fade = 1.0f - f;
			if (f >= 1.0f)
			{
				h2l.looping = FALSE;
				h2l.shown = 0.0f;
				h2l.arrived = 0;
			}
		}
		else if (target > h2l.shown)
			h2l.shown = h2l.shown + step < target ? h2l.shown + step : target;
		if (!h2l.looping && h2l.shown >= 0.999f)
		{
			if (!h2l.arrived)
				h2l.arrived = now;
			else if (now - h2l.arrived > H2L_HOLD_MS)
				h2l.looping = TRUE, h2l.arrived = now;
		}
		s = h2l.shown;
	}
	fade = fade < 0.0f ? 0.0f : fade;
	intro = since >= H2L_INTRO_MS ? 1.0f : (real)since / (real)H2L_INTRO_MS;
	/* (the Vista footage's brightness, measured: a bell peaking at 0.55 of the sweep, 0.18 wide) */
	env = 0.14f + 0.86f * (real)exp(-((s - 0.55f) / 0.18f) * ((s - 0.55f) / 0.18f));
	beam_x = vx0 + w * (H2L_BEAM_LEFT + (H2L_BEAM_RIGHT - H2L_BEAM_LEFT) * s);

	h2l_rect(h2l.white, vx0, vy0, vx1, vy1, h2l_color(1.0f, 0.0f, 0.0f, 0.0f), H2L_BLEND_ALPHA);
	{
		/* the plate: its burst's centre on the logo's, lit blue about the beam */
		real ps = h / 560.0f, reach = 0.26f * h + 0.12f * w;
		real pw = (real)h2l.plate_w * (real)H2L_PLATE_REDUCE * ps, ph = (real)h2l.plate_h * (real)H2L_PLATE_REDUCE * ps;
		real px = cx - 380.0f * ps, py = cy - 256.0f * ps;
		real rs = h * 0.74f;
		long k;

		h2l_lit_rect(h2l.plate, px, py, px + pw, py + ph, beam_x, reach, env * fade * 4.6f, 0.26f, 0.36f, 1.0f);
		/* the rings about the logo's centre */
		for (k = 0; k < 2; k++)
			h2l_lit_rect(h2l.rings[k], cx - 0.5f * rs, cy - 0.5f * rs, cx + 0.5f * rs, cy + 0.5f * rs, beam_x, reach * 0.9f,
				env * fade * (k ? 0.12f : 0.20f), 0.25f, 0.35f, 1.0f);
		/* the beam */
		{
			real bw = 1.1f * h, a = env * fade * 1.35f;
			pixel32 c = h2l_color(a, 0.82f * a, 0.86f * a, a);

			h2l_rect(h2l.beam, beam_x - 0.5f * bw, vy0, beam_x + 0.5f * bw, vy1, c, H2L_BLEND_ADD);
		}
	}
	{
		/* the logo, 2 pixels a texel at 720 high, centred; it comes up in the first moments and goes in the fade */
		real ls = h / 360.0f * (real)H2L_LOGO_REDUCE, a = intro * fade;
		real lw = (real)h2l.logo_w * ls, lh = (real)h2l.logo_h * ls;

		h2l_rect(h2l.logo, cx - 0.5f * lw, cy - 0.5f * lh, cx + 0.5f * lw, cy + 0.5f * lh, h2l_color(a, 1.0f, 1.0f, 1.0f), H2L_BLEND_ALPHA);
	}
	(void)white;
	/* (tests: HALO_H2_LOADING_SHOTS=name saves a frame 500 ms after the progress
	last changed, name_0 ..) */
	{
		/* (tests: HALO_H2_LOADING_SNAP=name saves a frame every 2 seconds the screen is up, name_0 ..) */
		char const *snap = getenv("HALO_H2_LOADING_SNAP");

		if (snap && snap[0] && since >= 2000 * (unsigned long)(h2l.snaps + 1))
			snprintf(halo_screenshot_name, 64, "%s_%ld", snap, h2l.snaps++);
	}
	if (test)
	{
		if (progress != h2l.shot_progress)
		{
			h2l.shot_progress = progress;
			h2l.shot_since = now;
		}
		else if (h2l.shot_since && now - h2l.shot_since >= 500)
		{
			h2l.shot_since = 0;
			snprintf(halo_screenshot_name, 64, "%s_%ld", shots, h2l.shots++);
		}
	}
}

void h2_loading_end(void)
{
	if (!h2c.active)
		return;
	h2c.active = FALSE;
	h2r.covering = FALSE;
	vita_texture_settling_set(0);
	h2l_let_go();
	platform_log("h2 loading: up %lu ms, art let go", system_milliseconds() - h2l.started);
}

/* ---------- the reveal

The level is in when its load returns, but its first picture is not: the
local player spawns in the first ticks and the frame's textures are asked for
as it is drawn - the bitmaps the texture cache reads (each drawn from a white
stand-in meanwhile: rasterizer_xbox.c), the derived ones the texture source's
workers make (the Spartans' multipurpose maps, whose stand-in left the
armour white until it was in), the levels above each streamed texture's base
(vita_textures.c, a few a frame), the emblems (a few a frame). A Vita took
seconds over them after the screen went, the scene filling in and the
player white before its colour. So the screen stays up over the level's
first frames (the game ticking as it would, the frames drawn as they would be
and this screen drawn over them: h2_loading_cover_draw; its sounds heard as
they would be - the sound system paused mid-level tripped its gain check),
until a frame finds
the local players' units there with their colours given and draws with every
texture in and nothing asked or waited for - then it goes, and the first
picture shown is whole. HALO_H2_REVEAL_MS (default 12000) bounds the wait;
HALO_H2_REVEAL=0 takes the screen down when the load returns, as before, and
still measures when the first view came in (the log's "h2 reveal" line). */

#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
/* (the Vita's texture decoder, port/vita/platform/vita_textures.c; the PC's
OpenGL textures are made whole when first drawn) */
void vita_texture_settle_counts(unsigned long counts[6]);
void vita_texture_settling(int on);
#define H2R_VITA_TEXTURES 1
#else
#define H2R_VITA_TEXTURES 0
#endif
long h2_texture_source_reads_pending(unsigned long *made);
int h2_emblem_local_players_ready(void);
unsigned long h2_emblem_deferred_count(void);
extern unsigned long rasterizer_stand_in_draws;

enum
{
	/* the frames in a row a first view must draw with nothing missing */
	H2R_QUIET_FRAMES = 3,
	H2R_DEFAULT_MS = 12000,
	/* (watching alone, the screen gone: the most it watches) */
	H2R_WATCH_MS = 30000,
	/* (the reveal's sweep: two thirds of the way left to H2R_SWEEP_END in this long) */
	H2R_SWEEP_MS = 3000,
};
#define H2R_SWEEP_END 0.965f

static void vita_texture_settling_set(int on)
{
#if H2R_VITA_TEXTURES
	vita_texture_settling(on);
#else
	(void)on;
#endif
}

/* the texture work so far, as running totals: [0..5] vita_textures.c's
(settle counts), [6] stand-ins drawn by the texture cache, [7] derived
reads pending, [8] derived made, [9] emblems put off */
static void h2r_counts(unsigned long counts[10])
{
	memset(counts, 0, sizeof(unsigned long) * 10);
#if H2R_VITA_TEXTURES
	vita_texture_settle_counts(counts);
#endif
	counts[6] = rasterizer_stand_in_draws;
	counts[7] = (unsigned long)h2_texture_source_reads_pending(&counts[8]);
	counts[9] = h2_emblem_deferred_count();
}

static unsigned long h2r_limit_ms(void)
{
	char const *setting = getenv("HALO_H2_REVEAL_MS");

	return setting ? (unsigned long)atol(setting) : H2R_DEFAULT_MS;
}

boolean h2_loading_covering(void)
{
	return h2r.covering;
}

/* (game_loading_screen_end) the level loaded: TRUE when the screen stays up
over its first frames (taken down by h2_loading_reveal_frame); FALSE to
take it down now. The watch starts either way */
boolean h2_loading_reveal_begin(void)
{
	char const *setting = getenv("HALO_H2_REVEAL");

	memset(&h2r, 0, sizeof(h2r));
	h2r.watching = TRUE;
	h2r.began = system_milliseconds();
	h2r.unit_ms = h2r.settled_ms = (unsigned long)NONE;
	h2r.frame_seen = render.frame_index;
	h2r.from = h2l.given;
	h2r_counts(h2r.at_begin);
	memcpy(h2r.last, h2r.at_begin, sizeof(h2r.last));
	/* (Halo 2's main menu: no player to wait for, its textures alone) */
	h2r.no_player = !strcmp(h2c.name, "mainmenu");
	/* (a multiplayer level's alone: a mission's first ticks may be its
	opening cinematic's, which the screen must not hide - watched only) */
	if (!h2c.active || (setting && !strcmp(setting, "0")) || !h2r_limit_ms() || global_scenario_index == NONE ||
		global_scenario_get()->type != _scenario_type_multiplayer)
	{
		return FALSE;
	}
	h2r.covering = TRUE;
	vita_texture_settling_set(1);
	return TRUE;
}

static void h2r_report(char const *why)
{
	unsigned long now[10], since = system_milliseconds() - h2r.began;

	h2r_counts(now);
	if (h2r.covering)
		platform_log("h2 reveal: the loading screen drawn over %lu of the level's frames, at most %lu ms between two", h2r.frames,
			h2r.cover_longest);
	platform_log("h2 reveal: %s %lu ms after the level loaded (%lu frames%s): the local player's unit %s%ld ms; "
		"since the load - stand-ins drawn %lu, lower levels drawn while decoding %lu, detail reads %lu asked (%lu held back), "
		"%lu in, %lu still out; derived bitmaps %lu made, %lu reads out; emblems put off %lu; last frame wanting a texture at %ld ms",
		why, since, h2r.frames, h2r.covering ? " under the loading screen" : ", the screen gone",
		h2r.unit_ms == (unsigned long)NONE ? "not there, " : "at ", h2r.unit_ms == (unsigned long)NONE ? (long)since : (long)h2r.unit_ms,
		now[6] - h2r.at_begin[6], now[2] - h2r.at_begin[2], now[3] - h2r.at_begin[3], now[4] - h2r.at_begin[4],
		now[5] - h2r.at_begin[5], now[1], now[8] - h2r.at_begin[8], now[7], now[9] - h2r.at_begin[9], (long)h2r.busy_ms);
	platform_log("h2 reveal: the last frame held up by - the texture cache's reads %ld ms, decodes %ld ms, detail reads %ld ms, "
		"derived bitmaps %ld ms, emblems %ld ms", h2r.kind_ms[0], h2r.kind_ms[1], h2r.kind_ms[2], h2r.kind_ms[3], h2r.kind_ms[4]);
}

/* (tests: HALO_H2_REVEAL_SHOTS=name saves the first frame drawn after the
load as name_first - under the screen, or not with HALO_H2_REVEAL=0 - and the
first after the reveal, or the frame the first view came in, as name_reveal) */
static void h2r_shot(char const *what)
{
	extern char halo_screenshot_name[64];
	char const *shots = getenv("HALO_H2_REVEAL_SHOTS");

	if (shots && shots[0])
		snprintf(halo_screenshot_name, 64, "%s_%s", shots, what);
}

static void h2_loading_reveal_now(char const *why)
{
	if (h2r.watching)
	{
		h2r_report(why);
		h2r_shot("reveal");
	}
	h2r.watching = FALSE;
	if (h2r.covering)
	{
		h2r.covering = FALSE;
		h2_loading_end();
	}
}

/* (the main loop, once a frame before it is drawn) the last frame drawn
looked at: the screen taken down once the first view is whole */
void h2_loading_reveal_frame(void)
{
	unsigned long now[10], since;
	int players;
	boolean busy;

	if (!h2r.watching)
		return;
	since = system_milliseconds() - h2r.began;
	players = h2r.no_player ? -1 : h2_emblem_local_players_ready();
	if (players != 0 && h2r.unit_ms == (unsigned long)NONE)
		h2r.unit_ms = since;
	/* (no frame drawn since the last look - the first ticks, before the
	game draws: nothing to tell) */
	if (render.frame_index == h2r.frame_seen)
	{
		if (since > (h2r.covering ? h2r_limit_ms() : (unsigned long)H2R_WATCH_MS))
			h2_loading_reveal_now(h2r.covering ? "TIMED OUT, the screen taken down" : "the watch ended, never settled,");
		return;
	}
	h2r.frame_seen = render.frame_index;
	if (!h2r.frames++)
		h2r_shot("first");
	h2r_counts(now);
	/* (a frame that drew from a stand-in, asked for or held back a detail
	read, put off an emblem, or left a decode or a derived read out) */
	busy = now[0] || now[1] || now[7] || now[6] != h2r.last[6] || now[2] != h2r.last[2] || now[3] != h2r.last[3] ||
		now[4] != h2r.last[4] || now[9] != h2r.last[9];
	/* (what each kind of texture work last held the frame up at, for the log) */
	if (now[6] != h2r.last[6])
		h2r.kind_ms[0] = (long)since;
	if (now[0] > now[1] || now[2] != h2r.last[2])
		h2r.kind_ms[1] = (long)since;
	if (now[1] || now[3] != h2r.last[3] || now[4] != h2r.last[4])
		h2r.kind_ms[2] = (long)since;
	if (now[7])
		h2r.kind_ms[3] = (long)since;
	if (now[9] != h2r.last[9])
		h2r.kind_ms[4] = (long)since;
	memcpy(h2r.last, now, sizeof(h2r.last));
	if (busy || players == 0)
	{
		if (busy)
			h2r.busy_ms = (long)since;
		h2r.quiet = 0;
	}
	else
		h2r.quiet++;
	if (h2r.quiet >= H2R_QUIET_FRAMES)
		h2_loading_reveal_now(h2r.covering ? "the first view whole," : "(the screen gone at the load) the first view whole");
	else if (since > (h2r.covering ? h2r_limit_ms() : (unsigned long)H2R_WATCH_MS))
		h2_loading_reveal_now(h2r.covering ? "TIMED OUT, the screen taken down" : "the watch ended, never settled,");
}

/* (render.c, each player's window after its interface) the loading screen
over the level's first frames while they are not whole */
void h2_loading_cover_draw(void)
{
	if (h2r.covering && h2c.active)
	{
		unsigned long now = system_milliseconds(), last = h2r.cover_last ? h2r.cover_last : h2r.began;

		if (now - last > h2r.cover_longest)
			h2r.cover_longest = now - last;
		h2r.cover_last = now;
		/* (the load done, the first view coming in: the sweep on toward its
		end - never to it, which would hold and loop as a load that ran
		long - until the view is whole and the screen goes) */
		h2_loading_draw(h2r.from + (H2R_SWEEP_END - h2r.from) * (1.0f - (real)exp(-(real)(now - h2r.began) / (real)H2R_SWEEP_MS)));
	}
}

/* ---------- a level's build */

boolean game_loading_screen_h2_begin(char const *map_name);
void game_loading_screen_frame(real progress);
void game_loading_screen_end(void);
boolean game_loading_screen_up_here(void);
int halo_thread_index(void);
int halo_game_thread_index(void);

static struct
{
	boolean on;
	long total, done;
} h2b;

void h2_loading_build_begin(char const *maps_directory, char const *name)
{
	char why[H2_CACHE_WHY_SIZE];
	struct h2_cache *cache;
	long k;

	h2b.on = FALSE;
	if (h2c.active || halo_thread_index() != halo_game_thread_index() || (getenv("HALO_H2_LOADING") && !strcmp(getenv("HALO_H2_LOADING"), "0")))
		return;
	/* (the bitmaps the level has: what its build makes, for the sweep) */
	h2b.total = 0;
	h2b.done = 0;
	if ((cache = h2_cache_open(maps_directory, name, why, sizeof(why))) != NULL)
	{
		struct h2_cache_tag t;

		for (k = 0; k < h2_cache_tag_count(cache); k++)
		{
			if (h2_cache_tag_get(cache, k, &t) && t.group_tag == 'bitm')
				h2b.total++;
		}
		h2_cache_close(cache);
	}
	if (game_loading_screen_h2_begin(name))
		h2b.on = TRUE;
}

void h2_loading_build_tick(void)
{
	if (!h2b.on)
		return;
	h2b.done++;
	game_loading_screen_frame(0.05f + 0.90f * (real)MIN(h2b.done, h2b.total) / (real)MAX(h2b.total, 1));
}

void h2_loading_build_end(void)
{
	if (!h2b.on)
		return;
	h2b.on = FALSE;
	game_loading_screen_end();
}
