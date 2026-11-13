/*
PREVIEW_CACHE.H

The Workshop's pictures kept for showing (workshop_browser.c): a few, by
item id, so that going back to an item shows its picture again rather than
an empty box, and the items whose picture couldn't be had: for the run when
it never will be (404, too large, not a picture), else asked again only as
retry_backoff.h says (2 s doubling to a minute, Retry-After honored).

No game types, so that the build machine can test it
(port/linux/tests/preview_cache_test.c).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef PREVIEW_CACHE_H
#define PREVIEW_CACHE_H

#include "retry_backoff.h"

enum
{
	PREVIEW_CACHE_ID_SIZE = 24,
	/* the pictures kept (each at most 768 by 768 pixels, workshop_browser.c's
	shrink, a third of a 4K or Retina window's height: about 2.3 MB at most,
	19 MB for all of them) */
	PREVIEW_CACHE_KEPT = 8,
	/* the items whose picture failed, remembered */
	PREVIEW_CACHE_FAILED = 64,
};

struct preview_cache_picture
{
	char id[PREVIEW_CACHE_ID_SIZE];
	unsigned char *pixels;
	int width, height, serial;
	unsigned long used;
};

struct preview_cache_failure
{
	char id[PREVIEW_CACHE_ID_SIZE];
	/* never to be had this run; else asked again once backoff allows */
	int permanent;
	struct retry_backoff backoff;
};

struct preview_cache
{
	struct preview_cache_picture kept[PREVIEW_CACHE_KEPT];
	struct preview_cache_failure failed[PREVIEW_CACHE_FAILED];
	int failed_next;
	/* the item waiting on something else (Steam's details) for where to
	get its picture, and the caller's count of changes then */
	char waiting[PREVIEW_CACHE_ID_SIZE];
	unsigned long waiting_changes;
	int serial;
	unsigned long tick;
};

/* an item's picture: RGBA pixels (still the cache's), its size, and a
serial that is new for each picture kept (never 0); NULL when none is kept */
const unsigned char *preview_cache_get(struct preview_cache *cache, const char *id, int *width, int *height,
	int *serial);
/* pixels (from malloc, the cache's from now on) kept as id's picture, in
place of the one least recently got when all are taken; 0 (and the pixels
freed) when they can't be. Its failures are forgotten */
int preview_cache_put(struct preview_cache *cache, const char *id, unsigned char *pixels, int width, int height);
/* id's picture failed at now (milliseconds, the caller's clock): for good
(permanent), else with an HTTP status (0: no answer) and the seconds its
Retry-After said (0: none), asked again after a wait */
void preview_cache_fail(struct preview_cache *cache, const char *id, unsigned long now, int permanent, int status,
	long retry_after);
/* whether id's picture failed (its page says so) */
int preview_cache_failed(const struct preview_cache *cache, const char *id);
/* whether id's picture may be asked for at now: never failed, or failed
for a while and its wait is over */
int preview_cache_ready(const struct preview_cache *cache, const char *id, unsigned long now);
/* whether an HTTP status is worth asking again after (0: no answer, 408,
429, 5xx): the rest are for good */
int preview_cache_transient(int status);
/* how a picture's download ended (preview_cache_download) */
enum
{
	PREVIEW_DOWNLOAD_WHOLE,
	/* cut off (the deadline, a read that failed, fewer bytes than its
	length said, none at all): asked again after a wait, nothing kept */
	PREVIEW_DOWNLOAD_CUT,
	/* for good */
	PREVIEW_DOWNLOAD_TOO_LARGE,
	PREVIEW_DOWNLOAD_NOT_PICTURE,
};

/* a download's end: length its stated size (-1: none said), got the bytes
read, ended_badly 1 when the last read failed or the deadline passed,
largest the most taken, picture 1 when its first bytes are a PNG's or a
JPEG's */
int preview_cache_download(long long length, long long got, int ended_badly, long long largest, int picture);

/* id can't be asked for until something changes: the caller's count of
changes (the list's, Steam's details') is changes now. One item at a time */
void preview_cache_wait(struct preview_cache *cache, const char *id, unsigned long changes);
/* whether id is still waiting: nothing changed since (changes the same) */
int preview_cache_waiting(const struct preview_cache *cache, const char *id, unsigned long changes);
/* everything let go */
void preview_cache_clear(struct preview_cache *cache);

#endif
