/*
PREVIEW_CACHE_TEST.C

Tests of the Workshop's pictures kept for showing
(port/linux/src/preview_cache.c). tools/test_preview_cache.py runs them.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../src/preview_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...) \
	do \
	{ \
		if (!(condition)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

static unsigned char *pixels(unsigned char value)
{
	unsigned char *made = malloc(4 * 4 * 4);

	if (made)
		memset(made, value, 4 * 4 * 4);
	return made;
}

int main(void)
{
	static struct preview_cache cache;
	const unsigned char *got;
	int width = 0, height = 0, serial = 0, first_serial, index;
	char id[PREVIEW_CACHE_ID_SIZE];

	CHECK(!preview_cache_get(&cache, "3806571482", &width, &height, &serial), "an empty cache has none");

	/* (an item left and gone back to: its picture still there, the regression) */
	CHECK(preview_cache_put(&cache, "3806571482", pixels(1), 4, 4), "kept");
	CHECK(preview_cache_put(&cache, "3811976343", pixels(2), 4, 4), "another kept");
	got = preview_cache_get(&cache, "3806571482", &width, &height, &serial);
	CHECK(got && got[0] == 1 && width == 4 && height == 4 && serial > 0, "the first item's picture is still shown");
	first_serial = serial;
	got = preview_cache_get(&cache, "3811976343", &width, &height, &serial);
	CHECK(got && got[0] == 2 && serial > 0 && serial != first_serial, "each picture has its own serial");

	/* (the same item's again: in its place, a new serial) */
	CHECK(preview_cache_put(&cache, "3806571482", pixels(3), 4, 4), "kept again");
	got = preview_cache_get(&cache, "3806571482", &width, &height, &serial);
	CHECK(got && got[0] == 3 && serial != first_serial, "the new picture replaces the old one");

	/* (full: the least recently shown goes; the one just shown stays) */
	for (index = 0; index < PREVIEW_CACHE_KEPT; index++)
	{
		snprintf(id, sizeof(id), "100%d", index);
		preview_cache_get(&cache, "3806571482", &width, &height, &serial);
		CHECK(preview_cache_put(&cache, id, pixels((unsigned char)(10 + index)), 4, 4), "kept %s", id);
	}
	CHECK(preview_cache_get(&cache, "3806571482", &width, &height, &serial) != NULL, "the one shown each time stays");
	CHECK(!preview_cache_get(&cache, "3811976343", &width, &height, &serial), "the least recently shown went");
	got = preview_cache_get(&cache, "1007", &width, &height, &serial);
	CHECK(got && got[0] == 17, "the last kept is there");

	/* (for good: a 404, too large, not a picture: never asked again this run) */
	CHECK(!preview_cache_failed(&cache, "3822459106") && preview_cache_ready(&cache, "3822459106", 0),
		"not failed yet");
	preview_cache_fail(&cache, "3822459106", 1000, 1, 404, 0);
	preview_cache_fail(&cache, "3822459106", 1000, 1, 404, 0);
	CHECK(preview_cache_failed(&cache, "3822459106"), "failed");
	CHECK(!preview_cache_ready(&cache, "3822459106", 1000 + 24UL * 3600000), "for good is for the run");
	CHECK(!preview_cache_failed(&cache, "382245910"), "only that id");
	for (index = 0; index < PREVIEW_CACHE_FAILED; index++)
	{
		snprintf(id, sizeof(id), "200%d", index);
		preview_cache_fail(&cache, id, 1000, 1, 404, 0);
	}
	CHECK(!preview_cache_failed(&cache, "3822459106"), "the oldest failure goes when the list is full");
	preview_cache_clear(&cache);

	/* (a hiccup: no answer, a busy host: asked again after 2 s, doubling to a
	minute, never each frame; had at last, forgotten) */
	{
		unsigned long now = 5000, waits[] = { 2000, 4000, 8000, 16000, 32000, 60000, 60000 };
		int step;

		for (step = 0; step < (int)(sizeof(waits) / sizeof(waits[0])); step++)
		{
			preview_cache_fail(&cache, "3806571482", now, 0, step % 2 ? 503 : 0, 0);
			CHECK(preview_cache_failed(&cache, "3806571482"), "failing shows so");
			CHECK(!preview_cache_ready(&cache, "3806571482", now + 16), "not asked again the next frame");
			CHECK(!preview_cache_ready(&cache, "3806571482", now + waits[step] - 1), "still waiting (%d)", step);
			CHECK(preview_cache_ready(&cache, "3806571482", now + waits[step]), "asked again after %lu ms",
				waits[step]);
			now += waits[step];
		}
		CHECK(preview_cache_put(&cache, "3806571482", pixels(5), 4, 4), "had at last");
		CHECK(!preview_cache_failed(&cache, "3806571482") && preview_cache_ready(&cache, "3806571482", now),
			"its failures forgotten");

		/* (a 429's Retry-After: its two minutes, not 2 s) */
		preview_cache_fail(&cache, "3659420567", now, 0, 429, 120);
		CHECK(!preview_cache_ready(&cache, "3659420567", now + 119999), "Retry-After honored");
		CHECK(preview_cache_ready(&cache, "3659420567", now + 120000), "asked again after Retry-After");
	}
	CHECK(preview_cache_transient(0) && preview_cache_transient(408) && preview_cache_transient(429) &&
		preview_cache_transient(500) && preview_cache_transient(503), "no answer, 408, 429, 5xx: asked again");
	CHECK(!preview_cache_transient(404) && !preview_cache_transient(403) && !preview_cache_transient(410),
		"404, 403, 410: for good");

	/* (a download's end: cut off by the deadline, a failed read, short of its
	length or empty is a hiccup, asked again after a wait; too large or not
	a picture is for good) */
	CHECK(preview_cache_download(961574, 961574, 0, 8 << 20, 1) == PREVIEW_DOWNLOAD_WHOLE, "whole");
	CHECK(preview_cache_download(-1, 961574, 0, 8 << 20, 1) == PREVIEW_DOWNLOAD_WHOLE, "whole, no length said");
	CHECK(preview_cache_download(961574, 400000, 1, 8 << 20, 1) == PREVIEW_DOWNLOAD_CUT, "the deadline cut it");
	CHECK(preview_cache_download(961574, 400000, 0, 8 << 20, 1) == PREVIEW_DOWNLOAD_CUT, "short of its length");
	CHECK(preview_cache_download(-1, 400000, 1, 8 << 20, 1) == PREVIEW_DOWNLOAD_CUT, "a failed read, no length");
	CHECK(preview_cache_download(-1, 0, 0, 8 << 20, 0) == PREVIEW_DOWNLOAD_CUT, "nothing came");
	CHECK(preview_cache_download(961574, 400000, 1, 8 << 20, 0) == PREVIEW_DOWNLOAD_CUT,
		"a cut-off picture is never \"not a picture\"");
	CHECK(preview_cache_download(9 << 20, 0, 0, 8 << 20, 0) == PREVIEW_DOWNLOAD_TOO_LARGE, "too large said");
	CHECK(preview_cache_download(-1, (8 << 20) + 1, 0, 8 << 20, 1) == PREVIEW_DOWNLOAD_TOO_LARGE, "too large read");
	CHECK(preview_cache_download(2048, 2048, 0, 8 << 20, 0) == PREVIEW_DOWNLOAD_NOT_PICTURE, "whole, not a picture");
	/* (cut off: through retry_backoff, not failed for the run) */
	preview_cache_fail(&cache, "3811976343", 9000, 0, 0, 0);
	CHECK(preview_cache_failed(&cache, "3811976343") && !preview_cache_ready(&cache, "3811976343", 9000) &&
		preview_cache_ready(&cache, "3811976343", 11000), "a cut-off download is asked again after 2 s");

	/* (waiting on Steam's details for an address: looked at again only once
	something changed, not each frame) */
	preview_cache_wait(&cache, "2894901917", 7);
	CHECK(preview_cache_waiting(&cache, "2894901917", 7), "nothing changed: still waiting");
	CHECK(!preview_cache_waiting(&cache, "2894901917", 8), "the details changed: looked at again");
	CHECK(!preview_cache_waiting(&cache, "3806571482", 7), "another item isn't waiting");
	preview_cache_wait(&cache, "3806571482", 8);
	CHECK(!preview_cache_waiting(&cache, "2894901917", 8), "one item waits at a time");

	/* (bad arguments: refused, the pixels freed, nothing kept) */
	CHECK(!preview_cache_put(&cache, "", pixels(4), 4, 4), "no id");
	CHECK(!preview_cache_put(&cache, "1234567890123456789012345", pixels(4), 4, 4), "an id too long");
	CHECK(!preview_cache_put(&cache, "300", pixels(4), 0, 4), "no width");
	CHECK(!preview_cache_put(&cache, "300", NULL, 4, 4), "no pixels");
	CHECK(!preview_cache_get(&cache, "300", &width, &height, &serial), "nothing kept for them");
	CHECK(!preview_cache_get(&cache, NULL, &width, &height, &serial), "no id, no picture");

	preview_cache_clear(&cache);
	CHECK(!preview_cache_get(&cache, "3806571482", &width, &height, &serial), "cleared");
	if (failures)
		return 1;
	printf("preview_cache: all passed\n");
	return 0;
}
