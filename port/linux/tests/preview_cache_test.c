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

	/* (no picture to be had: remembered, not asked for again) */
	CHECK(!preview_cache_failed(&cache, "3822459106"), "not failed yet");
	preview_cache_fail(&cache, "3822459106");
	preview_cache_fail(&cache, "3822459106");
	CHECK(preview_cache_failed(&cache, "3822459106"), "failed");
	CHECK(!preview_cache_failed(&cache, "382245910"), "only that id");
	for (index = 0; index < PREVIEW_CACHE_FAILED; index++)
	{
		snprintf(id, sizeof(id), "200%d", index);
		preview_cache_fail(&cache, id);
	}
	CHECK(!preview_cache_failed(&cache, "3822459106"), "the oldest failure goes when the list is full");

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
