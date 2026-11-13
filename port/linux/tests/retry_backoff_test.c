/*
RETRY_BACKOFF_TEST.C

Tests of when a failed web request is asked again
(port/linux/src/retry_backoff.c). tools/test_retry_backoff.py runs them.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../src/retry_backoff.h"

#include <stdio.h>
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

int main(void)
{
	struct retry_backoff backoff;
	unsigned long now = 1000, frames = 0, asked = 0, expected[] = { 2000, 4000, 8000, 16000, 32000, 60000, 60000 };
	int index;

	memset(&backoff, 0, sizeof(backoff));
	CHECK(retry_backoff_ready(&backoff, now), "a new one waits");
	/* (no network: 2 s, doubling to a minute) */
	for (index = 0; index < (int)(sizeof(expected) / sizeof(expected[0])); index++)
	{
		retry_backoff_failed(&backoff, now, 0, 0);
		CHECK(backoff.delay == expected[index], "wait %d: %lu, not %lu", index, backoff.delay, expected[index]);
		CHECK(!retry_backoff_ready(&backoff, now + backoff.delay - 1), "ready early at %d", index);
		CHECK(retry_backoff_ready(&backoff, now + backoff.delay), "not ready at %d", index);
		now += backoff.delay;
	}
	/* (answered: back to none, the next failure 2 s again) */
	retry_backoff_answered(&backoff);
	CHECK(retry_backoff_ready(&backoff, now), "answered but waiting");
	retry_backoff_failed(&backoff, now, 0, 0);
	CHECK(backoff.delay == RETRY_BACKOFF_FIRST, "not back to the first wait: %lu", backoff.delay);
	/* (busy, with Retry-After: as long as it says; capped at an hour) */
	retry_backoff_answered(&backoff);
	retry_backoff_failed(&backoff, now, 429, 120);
	CHECK(backoff.delay == 120000, "429 Retry-After 120: %lu", backoff.delay);
	retry_backoff_failed(&backoff, now, 503, 999999);
	CHECK(backoff.delay == RETRY_BACKOFF_LONGEST, "Retry-After capped: %lu", backoff.delay);
	/* (a Retry-After on a plain failure is not a server's word) */
	retry_backoff_answered(&backoff);
	retry_backoff_failed(&backoff, now, 404, 500);
	CHECK(backoff.delay == RETRY_BACKOFF_FIRST, "404 took Retry-After: %lu", backoff.delay);
	/* (the clock wrapping past its top) */
	retry_backoff_answered(&backoff);
	retry_backoff_failed(&backoff, (unsigned long)-500, 0, 0);
	CHECK(!retry_backoff_ready(&backoff, 100), "ready across the wrap");
	CHECK(retry_backoff_ready(&backoff, 1600), "not ready after the wrap");
	CHECK(retry_backoff_left(&backoff, 100) == 1400, "left across the wrap: %lu", retry_backoff_left(&backoff, 100));
	/* (the error text's Retry-After) */
	CHECK(retry_backoff_after("HTTP 429, Retry-After: 30") == 30, "Retry-After not read");
	CHECK(retry_backoff_after("no answer") == 0 && retry_backoff_after(NULL) == 0, "Retry-After from nothing");
	CHECK(retry_backoff_after("Retry-After: -5") == 0, "a negative Retry-After");
	/* (a frame loop of 30 s with no network: a handful of requests, not one a frame) */
	retry_backoff_answered(&backoff);
	for (now = 0; now < 30000; now += 16, frames++)
	{
		if (retry_backoff_ready(&backoff, now))
		{
			asked++;
			retry_backoff_failed(&backoff, now, 0, 0);
		}
	}
	CHECK(asked <= 5, "%lu requests in %lu frames", asked, frames);
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("retry backoff: all passed\n");
	return 0;
}
