/*
H2_MAP_PICTURE.C

The pictures of the player's own Halo 2 maps for this engine's menus: see
h2_map_picture.h.
*/

#include "cseries.h"
#include "h2_map_picture.h"
#include "h2_level.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmaps_internal.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the pixels and canvases are the C library's, as h2_level.c's and
h2_cache.c's are - h2_level_map_picture_read's buffer is freed here) */
#undef malloc
#undef free

/* ---------- constants */

enum
{
	/* each picture resampled to one square texture (MCC's are 440x414:
	the widgets draw them whole, scaled): 256x256 B8G8R8A8, 256 KB, on the
	PC; 128x128, 64 KB, on the Vita; and the textures kept at once - the
	ElDewrito picker's rows scrolled past and the lobby's card -: the PC's
	24 (6 MB), the Vita's 12 (768 KB), the least recently shown let go */
#ifdef HALO_VITA
	H2_MAP_PICTURE_KEPT = 12,
	H2_MAP_PICTURE_SIZE = 128,
#else
	H2_MAP_PICTURE_KEPT = 24,
	H2_MAP_PICTURE_SIZE = 256,
#endif
	H2_MAP_PICTURE_NAME = 64,
	/* (the bitmap fields' values, as menu_tags.c names them) */
	H2_MAP_PICTURE_TYPE_2D = 0,
	H2_MAP_PICTURE_FORMAT_A8R8G8B8 = 11,
	H2_MAP_PICTURE_POWER_OF_TWO_BIT = 0,
};

/* ---------- globals */

static struct
{
	long count;
	unsigned long clock;
	struct h2_map_picture_slot
	{
		char name[H2_MAP_PICTURE_NAME];
		boolean campaign;
		/* (read once: a map with no picture is not read again) - the
		reader's thread reads it (queued, reading), the asking thread makes
		its texture (read) */
		volatile int state;
		char directory[256];        /* (the maps folder it is in, found when asked) */
		unsigned char *canvas;      /* (read: the resampled pixels, NULL none) */
		long width, height;         /* (read: the picture's own size) */
		unsigned long read_ms, reads, read_kb, inflates;
		char why[192];              /* (read, none: why) */
		struct bitmap_data *bitmap; /* (NULL: none, or let go) */
		real u1, v1;
		long bytes;
		unsigned long used;         /* (the clock when last asked for) */
	} slots[H2_MAP_PICTURE_SLOTS];
} h2_map_picture;

/* (a slot's state) */
enum
{
	PICTURE_NEW,      /* never asked, or let go: read when asked */
	PICTURE_QUEUED,   /* asked: for the reader */
	PICTURE_READING,  /* the reader's */
	PICTURE_READ,     /* read (canvas, or none and why): its texture made when next asked */
	PICTURE_DONE,     /* its texture made, or none */
};

/* (the reader: a thread of its own reads each picture from its map - the
map opened, its picture's chunks read and inflated, resampled -, which on a
Vita took a frame of the lobby's well past a second each time the map shown
changed; the thread asking makes the texture, a copy) */
static pthread_mutex_t h2_map_picture_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t h2_map_picture_wake = PTHREAD_COND_INITIALIZER;
static int h2_map_picture_reader = 0;   /* 1 running, -1 none to be had: read on the asking thread */

void h2_cache_io_counts(unsigned long *reads, unsigned long *read_bytes, unsigned long *read_us, unsigned long *inflates,
	unsigned long *inflate_us);
unsigned long system_milliseconds(void);

/* ---------- private code */

static struct h2_map_picture_slot *h2_map_picture_slot(char const *name, boolean campaign)
{
	long k;

	for (k = 0; k < h2_map_picture.count; k++)
	{
		if (h2_map_picture.slots[k].campaign == campaign && !strcmp(h2_map_picture.slots[k].name, name))
			return &h2_map_picture.slots[k];
	}
	if (h2_map_picture.count >= H2_MAP_PICTURE_SLOTS)
		return NULL;
	/* (the reader looks over the slots: one added with the lock held) */
	pthread_mutex_lock(&h2_map_picture_lock);
	memset(&h2_map_picture.slots[h2_map_picture.count], 0, sizeof(h2_map_picture.slots[0]));
	snprintf(h2_map_picture.slots[h2_map_picture.count].name, H2_MAP_PICTURE_NAME, "%s", name);
	h2_map_picture.slots[h2_map_picture.count].campaign = campaign;
	k = h2_map_picture.count++;
	pthread_mutex_unlock(&h2_map_picture_lock);
	return &h2_map_picture.slots[k];
}

static void h2_map_picture_let_go(struct h2_map_picture_slot *slot)
{
	if (!slot->bitmap)
		return;
	rasterizer_bitmap_delete(slot->bitmap);
	free(slot->bitmap);
	slot->bitmap = NULL;
	slot->bytes = 0;
	/* (read again when asked for) */
	pthread_mutex_lock(&h2_map_picture_lock);
	slot->state = PICTURE_NEW;
	pthread_mutex_unlock(&h2_map_picture_lock);
}

/* the textures kept down to the bound: the least recently shown let go */
static void h2_map_picture_bound(void)
{
	for (;;)
	{
		struct h2_map_picture_slot *oldest = NULL;
		long k, kept = 0;

		for (k = 0; k < h2_map_picture.count; k++)
		{
			if (!h2_map_picture.slots[k].bitmap)
				continue;
			kept++;
			if (!oldest || h2_map_picture.slots[k].used < oldest->used)
				oldest = &h2_map_picture.slots[k];
		}
		if (kept <= H2_MAP_PICTURE_KEPT || !oldest)
			return;
		platform_log("h2 map picture: %s let go (%ld kept)", oldest->name, kept - 1);
		h2_map_picture_let_go(oldest);
	}
}

/* the picture read from its map and resampled to the square (the reader's
thread, or the asking one with no reader): into the slot's canvas, or why
there is none */
static void h2_map_picture_read(struct h2_map_picture_slot *slot)
{
	unsigned long started = system_milliseconds(), reads0, kb0, us0, inflates0, iu0, reads1, kb1, us1, inflates1, iu1;
	long width = 0, height = 0, cw, ch, y;
	unsigned char *pixels, *canvas;

	h2_cache_io_counts(&reads0, &kb0, &us0, &inflates0, &iu0);
	pixels = h2_level_map_picture_read(slot->directory, slot->name, slot->campaign, &width, &height, slot->why, sizeof(slot->why));
	slot->canvas = NULL;
	slot->width = width;
	slot->height = height;
	if (pixels)
	{
		/* (resampled to the square, bilinear) */
		cw = ch = H2_MAP_PICTURE_SIZE;
		canvas = malloc((size_t)(cw * ch * 4));
		if (!canvas)
			snprintf(slot->why, sizeof(slot->why), "out of memory");
		for (y = 0; canvas && y < ch; y++)
		{
			real sy = ((real)y + 0.5f) * (real)height / (real)ch - 0.5f;
			long y0 = sy < 0.0f ? 0 : (long)sy, y1 = y0 + 1 < height ? y0 + 1 : y0, x;
			real fy = sy < 0.0f ? 0.0f : sy - (real)y0;

			for (x = 0; x < cw; x++)
			{
				real sx = ((real)x + 0.5f) * (real)width / (real)cw - 0.5f;
				long x0 = sx < 0.0f ? 0 : (long)sx, x1 = x0 + 1 < width ? x0 + 1 : x0, c;
				real fx = sx < 0.0f ? 0.0f : sx - (real)x0;

				for (c = 0; c < 4; c++)
				{
					real top = (real)pixels[4 * (y0 * width + x0) + c] * (1.0f - fx) + (real)pixels[4 * (y0 * width + x1) + c] * fx;
					real bottom = (real)pixels[4 * (y1 * width + x0) + c] * (1.0f - fx) + (real)pixels[4 * (y1 * width + x1) + c] * fx;

					canvas[4 * (y * cw + x) + c] = (unsigned char)(top * (1.0f - fy) + bottom * fy + 0.5f);
				}
			}
		}
		slot->canvas = canvas;
		free(pixels);
	}
	/* (the reads and inflates every thread made meanwhile: the reader's
	alone when the game is idle in the lobby) */
	h2_cache_io_counts(&reads1, &kb1, &us1, &inflates1, &iu1);
	slot->read_ms = system_milliseconds() - started;
	slot->reads = reads1 - reads0;
	slot->read_kb = (kb1 - kb0) / 1024;
	slot->inflates = inflates1 - inflates0;
}

/* the canvas read made a texture (the thread asking for the picture: the
game's, which draws it) */
static void h2_map_picture_made(struct h2_map_picture_slot *slot)
{
	long cw = H2_MAP_PICTURE_SIZE, ch = H2_MAP_PICTURE_SIZE;
	unsigned char *canvas = slot->canvas;
	struct bitmap_data *bitmap;

	slot->canvas = NULL;
	slot->state = PICTURE_DONE;
	if (!canvas)
	{
		platform_log("h2 map picture: %s: none (%s)", slot->name, slot->why);
		return;
	}
	bitmap = calloc(1, sizeof(*bitmap));
	if (!bitmap)
	{
		free(canvas);
		platform_log("h2 map picture: %s: out of memory", slot->name);
		return;
	}
	bitmap->signature = BITMAP_GROUP_TAG;
	bitmap->width = (short)cw;
	bitmap->height = (short)ch;
	bitmap->depth = 1;
	bitmap->type = H2_MAP_PICTURE_TYPE_2D;
	bitmap->format = H2_MAP_PICTURE_FORMAT_A8R8G8B8;
	bitmap->flags = FLAG(H2_MAP_PICTURE_POWER_OF_TWO_BIT);
	bitmap->tag_index = NONE;
	bitmap->cache_block_index = NONE;
	bitmap->base_address = canvas;
	if (!rasterizer_bitmap_new(bitmap) || !bitmap->hardware_format)
	{
		bitmap->base_address = NULL;
		free(canvas);
		free(bitmap);
		platform_log("h2 map picture: %s: no texture made", slot->name);
		return;
	}
	rasterizer_bitmap_changed(bitmap);
	bitmap->base_address = NULL;
	free(canvas);
	slot->bitmap = bitmap;
	slot->u1 = 1.0f;
	slot->v1 = 1.0f;
	slot->bytes = cw * ch * 4;
	platform_log("h2 map picture: %s: %ldx%ld made %ldx%ld, %ld KB; read in %lu ms%s (reads %lu, %lu KB, inflates %lu)", slot->name,
		slot->width, slot->height, cw, ch, slot->bytes / 1024, slot->read_ms, h2_map_picture_reader > 0 ? " off the game's thread" : "",
		slot->reads, slot->read_kb, slot->inflates);
	h2_map_picture_bound();
}

/* the reader: the queued slot shown last first */
static void *h2_map_picture_reader_main(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&h2_map_picture_lock);
	for (;;)
	{
		struct h2_map_picture_slot *next = NULL;
		long k;

		for (k = 0; k < h2_map_picture.count; k++)
		{
			if (h2_map_picture.slots[k].state == PICTURE_QUEUED && (!next || h2_map_picture.slots[k].used > next->used))
				next = &h2_map_picture.slots[k];
		}
		if (!next)
		{
			pthread_cond_wait(&h2_map_picture_wake, &h2_map_picture_lock);
			continue;
		}
		next->state = PICTURE_READING;
		pthread_mutex_unlock(&h2_map_picture_lock);
		h2_map_picture_read(next);
		pthread_mutex_lock(&h2_map_picture_lock);
		next->state = PICTURE_READ;
	}
	return NULL;
}

/* (the lock held) the reader started once; FALSE none */
static boolean h2_map_picture_reader_start(void)
{
	if (!h2_map_picture_reader)
	{
		pthread_t thread;
		pthread_attr_t attributes;
		char const *setting = getenv("HALO_H2_PICTURE_READER");

		h2_map_picture_reader = -1;
		/* (Halo 2's maps in a menu: the kit's data checked meanwhile, h2_level.c) */
		{
			void h2_level_kit_check_ahead(void);

			h2_level_kit_check_ahead();
		}
		/* (a map's open and its chunks' inflates: a stack of its own, as the
		texture source's workers have) */
		if (!(setting && !strcmp(setting, "0")) && pthread_attr_init(&attributes) == 0)
		{
			pthread_attr_setstacksize(&attributes, 512 * 1024);
			pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
			if (pthread_create(&thread, &attributes, h2_map_picture_reader_main, NULL) == 0)
				h2_map_picture_reader = 1;
			pthread_attr_destroy(&attributes);
		}
	}
	return h2_map_picture_reader > 0;
}

/* ---------- public code */

struct bitmap_data *h2_map_picture_get(char const *name, boolean campaign, real *u1, real *v1)
{
	struct h2_map_picture_slot *slot = name && name[0] ? h2_map_picture_slot(name, campaign) : NULL;

	if (u1)
		*u1 = 1.0f;
	if (v1)
		*v1 = 1.0f;
	if (!slot)
		return NULL;
	slot->used = ++h2_map_picture.clock;
	pthread_mutex_lock(&h2_map_picture_lock);
	if (slot->state == PICTURE_NEW)
	{
		snprintf(slot->directory, sizeof(slot->directory), "%s", h2_maps_directory_of(slot->name));
		slot->state = PICTURE_QUEUED;
		if (h2_map_picture_reader_start())
			pthread_cond_signal(&h2_map_picture_wake);
		else
		{
			/* (no reader: read here, as before) */
			slot->state = PICTURE_READING;
			pthread_mutex_unlock(&h2_map_picture_lock);
			h2_map_picture_read(slot);
			pthread_mutex_lock(&h2_map_picture_lock);
			slot->state = PICTURE_READ;
		}
	}
	if (slot->state == PICTURE_READ)
	{
		pthread_mutex_unlock(&h2_map_picture_lock);
		h2_map_picture_made(slot);
	}
	else
		pthread_mutex_unlock(&h2_map_picture_lock);
	if (slot->bitmap)
	{
		if (u1)
			*u1 = slot->u1;
		if (v1)
			*v1 = slot->v1;
	}
	return slot->bitmap;
}

short h2_map_picture_frame(char const *name, boolean campaign)
{
	struct h2_map_picture_slot *slot = name && name[0] ? h2_map_picture_slot(name, campaign) : NULL;

	return slot ? (short)(H2_MAP_PICTURE_FIRST_FRAME + (slot - h2_map_picture.slots)) : NONE;
}

boolean h2_map_picture_for_frame(short frame, struct bitmap_data **picture)
{
	long index = frame - H2_MAP_PICTURE_FIRST_FRAME;

	*picture = NULL;
	if (index < 0 || index >= h2_map_picture.count)
		return FALSE;
	*picture = h2_map_picture_get(h2_map_picture.slots[index].name, h2_map_picture.slots[index].campaign, NULL, NULL);
	return TRUE;
}

/* (tests: "@h2picture name" - main.c) the picture of the multiplayer map
`name` asked for every frame, as a lobby's card asks, until it is shown or
has none: how long the game's thread spent on it a frame, and when */
static struct
{
	char name[H2_MAP_PICTURE_NAME];
	unsigned long started, frames, longest_us, total_us;
} h2_map_picture_test_state;

void h2_map_picture_test(char const *name)
{
	memset(&h2_map_picture_test_state, 0, sizeof(h2_map_picture_test_state));
	snprintf(h2_map_picture_test_state.name, sizeof(h2_map_picture_test_state.name), "%s", name);
	h2_map_picture_test_state.started = system_milliseconds();
	{
		/* (and its title, as the lobby's card names it: custom_edition_maps.c) */
		char const *h2_level_title(char const *maps_directory, char const *name);
		unsigned long long vita_host_time_us(void);
		unsigned long long before = vita_host_time_us();
		unsigned long r0, k0, u0, i0, iu0, r1, k1, u1, i1, iu1;
		char const *title;

		h2_cache_io_counts(&r0, &k0, &u0, &i0, &iu0);
		title = h2_level_title(h2_maps_directory_of(name), name);
		h2_cache_io_counts(&r1, &k1, &u1, &i1, &iu1);
		platform_log("h2 map picture test: %s's title \"%s\" in %.1f ms on the game's thread (reads %lu, %lu KB, inflates %lu)", name,
			title ? title : "", (vita_host_time_us() - before) / 1000.0, r1 - r0, (k1 - k0) / 1024, i1 - i0);
	}
}

void h2_map_picture_test_frame(void)
{
	unsigned long long vita_host_time_us(void);
	unsigned long long before;
	struct h2_map_picture_slot *slot;
	struct bitmap_data *picture;
	unsigned long us;

	if (!h2_map_picture_test_state.name[0])
		return;
	before = vita_host_time_us();
	picture = h2_map_picture_get(h2_map_picture_test_state.name, FALSE, NULL, NULL);
	us = (unsigned long)(vita_host_time_us() - before);
	h2_map_picture_test_state.frames++;
	h2_map_picture_test_state.total_us += us;
	if (us > h2_map_picture_test_state.longest_us)
		h2_map_picture_test_state.longest_us = us;
	slot = h2_map_picture_slot(h2_map_picture_test_state.name, FALSE);
	if (picture || !slot || slot->state == PICTURE_DONE)
	{
		platform_log("h2 map picture test: %s %s after %lu ms (%lu frames asking); the game's thread spent at most %.1f ms of a frame "
			"on it, %.1f ms in all", h2_map_picture_test_state.name, picture ? "shown" : "has none",
			system_milliseconds() - h2_map_picture_test_state.started, h2_map_picture_test_state.frames,
			h2_map_picture_test_state.longest_us / 1000.0, h2_map_picture_test_state.total_us / 1000.0);
		h2_map_picture_test_state.name[0] = 0;
	}
}

void h2_map_picture_usage(long *count, long *bytes)
{
	long k;

	*count = *bytes = 0;
	for (k = 0; k < h2_map_picture.count; k++)
	{
		if (h2_map_picture.slots[k].bitmap)
		{
			(*count)++;
			*bytes += h2_map_picture.slots[k].bytes;
		}
	}
}
