/*
OPENCE_WELCOME_TEST.C

Tests of a host's one line to OpenCE players (port/linux/game/
opence_welcome.c): only a machine without Delta gets it, once a join, a few
seconds after it spawns, and only a line that fits OpenCE's chat is sent.
tools/test_opence_welcome.py builds and runs them.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../game/opence_welcome.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* a machine's frames from start to end, every 33 ms: how many sent it */
static int frames(struct opence_welcome *welcome, int machine, int present, int kind, int spawned,
	unsigned long start, unsigned long end)
{
	unsigned long now;
	int sent = 0;

	for (now = start; now < end; now += 33)
		sent += opence_welcome_due(welcome, machine, present, kind, spawned, now);
	return sent;
}

static void test_opence_only(void)
{
	struct opence_welcome welcome;

	opence_welcome_reset(&welcome);
	/* (a Delta machine: never, spawned or not, however long) */
	CHECK(frames(&welcome, 1, 1, _opence_welcome_delta, 1, 0, 60000) == 0);
	/* (one whose Delta session dropped later is still never told) */
	CHECK(frames(&welcome, 1, 1, _opence_welcome_opence, 1, 60000, 120000) == 0);
	/* (not known yet, or a host without Delta: never) */
	CHECK(frames(&welcome, 2, 1, _opence_welcome_unknown, 1, 0, 60000) == 0);
	/* (an OpenCE machine: once) */
	CHECK(frames(&welcome, 3, 1, _opence_welcome_opence, 1, 0, 60000) == 1);
}

static void test_once_after_spawn(void)
{
	struct opence_welcome welcome;

	opence_welcome_reset(&welcome);
	/* (in the lobby, never spawned: nothing) */
	CHECK(frames(&welcome, 4, 1, _opence_welcome_opence, 0, 0, 30000) == 0);
	/* (spawned at 30 s: nothing for the delay, then once) */
	CHECK(!opence_welcome_due(&welcome, 4, 1, _opence_welcome_opence, 1, 30000));
	CHECK(!opence_welcome_due(&welcome, 4, 1, _opence_welcome_opence, 1, 30000 + OPENCE_WELCOME_DELAY - 1));
	CHECK(opence_welcome_due(&welcome, 4, 1, _opence_welcome_opence, 1, 30000 + OPENCE_WELCOME_DELAY));
	CHECK(!opence_welcome_due(&welcome, 4, 1, _opence_welcome_opence, 1, 30000 + OPENCE_WELCOME_DELAY + 33));
	/* (dead, respawned, a new game: never again while they stay) */
	CHECK(frames(&welcome, 4, 1, _opence_welcome_opence, 0, 40000, 50000) == 0);
	CHECK(frames(&welcome, 4, 1, _opence_welcome_opence, 1, 50000, 200000) == 0);
	/* (dying during the delay still sends it once, on time) */
	CHECK(!opence_welcome_due(&welcome, 5, 1, _opence_welcome_opence, 1, 0));
	CHECK(frames(&welcome, 5, 1, _opence_welcome_opence, 0, 33, OPENCE_WELCOME_DELAY + 33) == 1);
	/* (the delay counts from the first spawn, with the clock wrapping) */
	CHECK(!opence_welcome_due(&welcome, 6, 1, _opence_welcome_opence, 1, 0xFFFFFF00UL));
	CHECK(opence_welcome_due(&welcome, 6, 1, _opence_welcome_opence, 1, 0xFFFFFF00UL + OPENCE_WELCOME_DELAY));
}

static void test_each_join(void)
{
	struct opence_welcome welcome;

	opence_welcome_reset(&welcome);
	CHECK(frames(&welcome, 7, 1, _opence_welcome_opence, 1, 0, 10000) == 1);
	/* (left: forgotten; the machine's next player is told again) */
	CHECK(frames(&welcome, 7, 0, _opence_welcome_opence, 0, 10000, 11000) == 0);
	CHECK(frames(&welcome, 7, 1, _opence_welcome_opence, 1, 11000, 30000) == 1);
	/* (a Delta player on that machine next: never) */
	CHECK(frames(&welcome, 7, 0, _opence_welcome_unknown, 0, 30000, 31000) == 0);
	CHECK(frames(&welcome, 7, 1, _opence_welcome_delta, 1, 31000, 60000) == 0);
	/* (machines are their own: one told, another not yet) */
	CHECK(frames(&welcome, 8, 1, _opence_welcome_opence, 1, 0, 10000) == 1);
	CHECK(frames(&welcome, 9, 1, _opence_welcome_opence, 0, 0, 10000) == 0);
	/* (machines outside the game's: nothing) */
	CHECK(!opence_welcome_due(&welcome, -1, 1, _opence_welcome_opence, 1, 99999));
	CHECK(!opence_welcome_due(&welcome, OPENCE_WELCOME_MACHINES, 1, _opence_welcome_opence, 1, 99999));
}

static void test_text(void)
{
	char text[OPENCE_WELCOME_TEXT_SIZE + 1];

	CHECK(opence_welcome_text_valid(opence_welcome_default_text));
	CHECK(strlen(opence_welcome_default_text) <= OPENCE_WELCOME_TEXT_LENGTH);
	CHECK(!opence_welcome_text_valid(NULL));
	CHECK(!opence_welcome_text_valid(""));
	CHECK(!opence_welcome_text_valid("a|b"));
	CHECK(!opence_welcome_text_valid("caf\xc3\xa9"));
	CHECK(!opence_welcome_text_valid("tab\there"));
	memset(text, 'a', OPENCE_WELCOME_TEXT_LENGTH);
	text[OPENCE_WELCOME_TEXT_LENGTH] = 0;
	CHECK(opence_welcome_text_valid(text));
	text[OPENCE_WELCOME_TEXT_LENGTH] = 'a';
	text[OPENCE_WELCOME_TEXT_LENGTH + 1] = 0;
	CHECK(!opence_welcome_text_valid(text));
}

int main(void)
{
	test_opence_only();
	test_once_after_spawn();
	test_each_join();
	test_text();
	if (failures)
	{
		printf("opence_welcome_test: %d failed\n", failures);
		return 1;
	}
	printf("opence_welcome_test: ok\n");
	return 0;
}
