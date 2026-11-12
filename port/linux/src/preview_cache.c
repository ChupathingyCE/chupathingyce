/*
PREVIEW_CACHE.C

The Workshop's pictures kept for showing (preview_cache.h).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "preview_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int id_usable(const char *id)
{
	return id && id[0] && strlen(id) < PREVIEW_CACHE_ID_SIZE;
}

static struct preview_cache_picture *find(struct preview_cache *cache, const char *id)
{
	int index;

	for (index = 0; index < PREVIEW_CACHE_KEPT; index++)
	{
		if (cache->kept[index].pixels && !strcmp(cache->kept[index].id, id))
			return &cache->kept[index];
	}
	return NULL;
}

const unsigned char *preview_cache_get(struct preview_cache *cache, const char *id, int *width, int *height,
	int *serial)
{
	struct preview_cache_picture *picture = id_usable(id) ? find(cache, id) : NULL;

	if (!picture)
		return NULL;
	picture->used = ++cache->tick;
	*width = picture->width;
	*height = picture->height;
	*serial = picture->serial;
	return picture->pixels;
}

int preview_cache_put(struct preview_cache *cache, const char *id, unsigned char *pixels, int width, int height)
{
	struct preview_cache_picture *picture;
	int index;

	if (!id_usable(id) || !pixels || width <= 0 || height <= 0)
	{
		free(pixels);
		return 0;
	}
	/* (the same item's again, else an empty place, else the least recently got) */
	picture = find(cache, id);
	for (index = 0; !picture && index < PREVIEW_CACHE_KEPT; index++)
	{
		if (!cache->kept[index].pixels)
			picture = &cache->kept[index];
	}
	if (!picture)
	{
		picture = &cache->kept[0];
		for (index = 1; index < PREVIEW_CACHE_KEPT; index++)
		{
			if (cache->kept[index].used < picture->used)
				picture = &cache->kept[index];
		}
	}
	if (picture->pixels != pixels)
		free(picture->pixels);
	snprintf(picture->id, sizeof(picture->id), "%s", id);
	picture->pixels = pixels;
	picture->width = width;
	picture->height = height;
	/* (a serial of its own: the overlay uploads a picture when it changes) */
	if (++cache->serial <= 0)
		cache->serial = 1;
	picture->serial = cache->serial;
	picture->used = ++cache->tick;
	return 1;
}

void preview_cache_fail(struct preview_cache *cache, const char *id)
{
	if (!id_usable(id) || preview_cache_failed(cache, id))
		return;
	snprintf(cache->failed[cache->failed_next], PREVIEW_CACHE_ID_SIZE, "%s", id);
	cache->failed_next = (cache->failed_next + 1) % PREVIEW_CACHE_FAILED;
}

int preview_cache_failed(const struct preview_cache *cache, const char *id)
{
	int index;

	if (!id_usable(id))
		return 0;
	for (index = 0; index < PREVIEW_CACHE_FAILED; index++)
	{
		if (!strcmp(cache->failed[index], id))
			return 1;
	}
	return 0;
}

void preview_cache_clear(struct preview_cache *cache)
{
	int index;

	for (index = 0; index < PREVIEW_CACHE_KEPT; index++)
		free(cache->kept[index].pixels);
	memset(cache, 0, sizeof(*cache));
}
