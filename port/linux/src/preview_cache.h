/*
PREVIEW_CACHE.H

The Workshop's pictures kept for showing (workshop_browser.c): a few, by
item id, so that going back to an item shows its picture again rather than
an empty box, and the items whose picture couldn't be had, so that they are
not asked for again each frame.

No game types, so that the build machine can test it
(port/linux/tests/preview_cache_test.c).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef PREVIEW_CACHE_H
#define PREVIEW_CACHE_H

enum
{
	PREVIEW_CACHE_ID_SIZE = 24,
	/* the pictures kept (each at most 384 pixels wide: about 330 KB) */
	PREVIEW_CACHE_KEPT = 8,
	/* the items remembered as having none */
	PREVIEW_CACHE_FAILED = 64,
};

struct preview_cache_picture
{
	char id[PREVIEW_CACHE_ID_SIZE];
	unsigned char *pixels;
	int width, height, serial;
	unsigned long used;
};

struct preview_cache
{
	struct preview_cache_picture kept[PREVIEW_CACHE_KEPT];
	char failed[PREVIEW_CACHE_FAILED][PREVIEW_CACHE_ID_SIZE];
	int failed_next;
	int serial;
	unsigned long tick;
};

/* an item's picture: RGBA pixels (still the cache's), its size, and a
serial that is new for each picture kept (never 0); NULL when none is kept */
const unsigned char *preview_cache_get(struct preview_cache *cache, const char *id, int *width, int *height,
	int *serial);
/* pixels (from malloc, the cache's from now on) kept as id's picture, in
place of the one least recently got when all are taken; 0 (and the pixels
freed) when they can't be */
int preview_cache_put(struct preview_cache *cache, const char *id, unsigned char *pixels, int width, int height);
/* id has no picture to be had (this run); and whether it was said so */
void preview_cache_fail(struct preview_cache *cache, const char *id);
int preview_cache_failed(const struct preview_cache *cache, const char *id);
/* everything let go */
void preview_cache_clear(struct preview_cache *cache);

#endif
