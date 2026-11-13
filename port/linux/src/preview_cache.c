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
	/* (had at last: its failures forgotten) */
	for (index = 0; index < PREVIEW_CACHE_FAILED; index++)
	{
		if (!strcmp(cache->failed[index].id, id))
			memset(&cache->failed[index], 0, sizeof(cache->failed[index]));
	}
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

static struct preview_cache_failure *failure_find(const struct preview_cache *cache, const char *id)
{
	int index;

	if (!id_usable(id))
		return NULL;
	for (index = 0; index < PREVIEW_CACHE_FAILED; index++)
	{
		if (!strcmp(cache->failed[index].id, id))
			return (struct preview_cache_failure *)&cache->failed[index];
	}
	return NULL;
}

void preview_cache_fail(struct preview_cache *cache, const char *id, unsigned long now, int permanent, int status,
	long retry_after)
{
	struct preview_cache_failure *failure;

	if (!id_usable(id))
		return;
	/* (one already failing keeps its wait, which grows; else the oldest place) */
	if (!(failure = failure_find(cache, id)))
	{
		failure = &cache->failed[cache->failed_next];
		cache->failed_next = (cache->failed_next + 1) % PREVIEW_CACHE_FAILED;
		memset(failure, 0, sizeof(*failure));
		snprintf(failure->id, sizeof(failure->id), "%s", id);
	}
	if (permanent)
		failure->permanent = 1;
	else
		retry_backoff_failed(&failure->backoff, now, status, retry_after);
}

int preview_cache_failed(const struct preview_cache *cache, const char *id)
{
	return failure_find(cache, id) != NULL;
}

int preview_cache_ready(const struct preview_cache *cache, const char *id, unsigned long now)
{
	const struct preview_cache_failure *failure = failure_find(cache, id);

	return !failure || (!failure->permanent && retry_backoff_ready(&failure->backoff, now));
}

int preview_cache_transient(int status)
{
	return status == 0 || status == 408 || status == 429 || status >= 500;
}

int preview_cache_download(long long length, long long got, int ended_badly, long long largest, int picture)
{
	if (length > largest || got > largest)
		return PREVIEW_DOWNLOAD_TOO_LARGE;
	/* (the deadline or a failed read before its end: a half picture is a
	hiccup, never "not a picture") */
	if (ended_badly || got <= 0 || (length >= 0 && got != length))
		return PREVIEW_DOWNLOAD_CUT;
	return picture ? PREVIEW_DOWNLOAD_WHOLE : PREVIEW_DOWNLOAD_NOT_PICTURE;
}

void preview_cache_wait(struct preview_cache *cache, const char *id, unsigned long changes)
{
	if (!id_usable(id))
		return;
	snprintf(cache->waiting, sizeof(cache->waiting), "%s", id);
	cache->waiting_changes = changes;
}

int preview_cache_waiting(const struct preview_cache *cache, const char *id, unsigned long changes)
{
	return id_usable(id) && !strcmp(cache->waiting, id) && cache->waiting_changes == changes;
}

void preview_cache_clear(struct preview_cache *cache)
{
	int index;

	for (index = 0; index < PREVIEW_CACHE_KEPT; index++)
		free(cache->kept[index].pixels);
	memset(cache, 0, sizeof(*cache));
}
