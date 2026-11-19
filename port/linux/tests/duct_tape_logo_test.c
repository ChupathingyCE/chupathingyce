/*
DUCT_TAPE_LOGO_TEST.C

Tests of the main menu's duct tape logo (port/linux/game/duct_tape_logo.h):
the Konami code's sequence, from start to finish, wrong and partial ones,
the presses it keeps from the menu, and the swap's timing. Built and run by
tools/test_duct_tape_logo.py; exits nonzero on a failure.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../game/duct_tape_logo.h"

#include <stdio.h>

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

static int const code[KONAMI_CODE_LENGTH] =
{
	_konami_up, _konami_up, _konami_down, _konami_down,
	_konami_left, _konami_right, _konami_left, _konami_right,
	_konami_b, _konami_a
};

/* presses in: how many times they finished the code */
static int feed(struct konami_code *state, int const *presses, int count)
{
	int finished = 0;
	int index;

	for (index = 0; index < count; index++)
		finished += konami_code_feed(state, presses[index]);
	return finished;
}

static void test_whole_code(void)
{
	struct konami_code state;
	int index;

	konami_code_reset(&state);
	for (index = 0; index < KONAMI_CODE_LENGTH - 1; index++)
		CHECK(!konami_code_feed(&state, code[index]));
	CHECK(konami_code_feed(&state, _konami_a));
	/* (it starts over: a second time works the same) */
	CHECK(state.matched == 0);
	CHECK(feed(&state, code, KONAMI_CODE_LENGTH) == 1);
	CHECK(feed(&state, code, KONAMI_CODE_LENGTH) == 1);
}

static void test_wrong_presses(void)
{
	struct konami_code state;
	static int const wrong_middle[] = { _konami_up, _konami_up, _konami_down, _konami_left };
	static int const other[] = { _konami_up, _konami_up, _konami_down, _konami_down, _konami_other };
	static int const no_b[] = {
		_konami_up, _konami_up, _konami_down, _konami_down,
		_konami_left, _konami_right, _konami_left, _konami_right, _konami_a
	};
	static int const backwards[] = {
		_konami_a, _konami_b, _konami_right, _konami_left, _konami_right, _konami_left,
		_konami_down, _konami_down, _konami_up, _konami_up
	};

	konami_code_reset(&state);
	CHECK(feed(&state, wrong_middle, 4) == 0);
	CHECK(state.matched == 0);
	CHECK(feed(&state, other, 5) == 0);
	CHECK(state.matched == 0);
	CHECK(feed(&state, no_b, 9) == 0);
	CHECK(state.matched == 0);
	CHECK(feed(&state, backwards, 10) == 0);
	/* (ends Up Up: that much of the code is in) */
	CHECK(state.matched == 2);
	/* and the whole code after a wrong start still works */
	konami_code_reset(&state);
	CHECK(feed(&state, wrong_middle, 4) == 0);
	CHECK(feed(&state, code, KONAMI_CODE_LENGTH) == 1);
}

static void test_partial_restarts(void)
{
	struct konami_code state;
	static int const three_ups[] = { _konami_up, _konami_up, _konami_up };
	int index;

	/* Up Up Up: still two in, so the code from the third Up's partner on
	finishes it */
	konami_code_reset(&state);
	CHECK(feed(&state, three_ups, 3) == 0);
	CHECK(state.matched == 2);
	CHECK(feed(&state, code + 2, KONAMI_CODE_LENGTH - 2) == 1);

	/* a wrong press late, which is Up: one in */
	konami_code_reset(&state);
	CHECK(feed(&state, code, 6) == 0);
	CHECK(!konami_code_feed(&state, _konami_up));
	CHECK(state.matched == 1);

	/* half the code, then nothing more: nothing happens */
	konami_code_reset(&state);
	for (index = 0; index < 5; index++)
		CHECK(!konami_code_feed(&state, code[index]));
	CHECK(state.matched == 5);
	/* (an out-of-range state is taken as none) */
	state.matched = 99;
	CHECK(!konami_code_feed(&state, _konami_down));
	CHECK(state.matched == 0);
}

static void test_swallows(void)
{
	struct konami_code state;

	konami_code_reset(&state);
	/* the menu's B and A are its own until the eight arrows are in */
	CHECK(!konami_code_swallows(&state, _konami_b));
	CHECK(!konami_code_swallows(&state, _konami_a));
	feed(&state, code, 7);
	CHECK(!konami_code_swallows(&state, _konami_b));
	feed(&state, code + 7, 1);
	/* (arrows never: they move the menu's focus as ever) */
	CHECK(!konami_code_swallows(&state, _konami_up));
	CHECK(!konami_code_swallows(&state, _konami_a));
	CHECK(konami_code_swallows(&state, _konami_b));
	konami_code_feed(&state, _konami_b);
	CHECK(!konami_code_swallows(&state, _konami_b));
	CHECK(konami_code_swallows(&state, _konami_a));
	CHECK(konami_code_feed(&state, _konami_a));
	CHECK(!konami_code_swallows(&state, _konami_a));
	CHECK(!konami_code_swallows(&state, _konami_b));
}

static void test_swap(void)
{
	struct duct_tape_logo logo;
	int frame, drop, last_drop;
	unsigned long now;

	duct_tape_logo_reset(&logo, 0);
	CHECK(!duct_tape_logo_step(&logo, 1000, &frame, &drop));
	CHECK(frame == DUCT_TAPE_LOGO_HALO_FRAME && drop == 0);

	/* the setting by hand: shown at once, no swap */
	duct_tape_logo_sync(&logo, 1);
	CHECK(!duct_tape_logo_step(&logo, 1000, &frame, &drop));
	CHECK(frame == DUCT_TAPE_LOGO_TAPE_FRAME && drop == 0);
	duct_tape_logo_sync(&logo, 0);
	CHECK(!duct_tape_logo_step(&logo, 1000, &frame, &drop));
	CHECK(frame == DUCT_TAPE_LOGO_HALO_FRAME);

	/* the code: the Halo logo sinks, slowly and never far, the whole
	build-up; then the tape, in one frame, with the click once */
	CHECK(duct_tape_logo_toggle(&logo, 5000) == 1);
	last_drop = 0;
	for (now = 5000; now < 5000 + DUCT_TAPE_LOGO_BUILD_UP; now += 16)
	{
		CHECK(!duct_tape_logo_step(&logo, now, &frame, &drop));
		CHECK(frame == DUCT_TAPE_LOGO_HALO_FRAME);
		CHECK(drop >= last_drop && drop <= DUCT_TAPE_LOGO_DROP);
		last_drop = drop;
		/* (the setting, already written, does not cut it short) */
		duct_tape_logo_sync(&logo, 1);
	}
	CHECK(last_drop >= DUCT_TAPE_LOGO_DROP - 1);
	CHECK(duct_tape_logo_step(&logo, 5000 + DUCT_TAPE_LOGO_BUILD_UP, &frame, &drop));
	CHECK(frame == DUCT_TAPE_LOGO_TAPE_FRAME && drop == 0);
	CHECK(!duct_tape_logo_step(&logo, 5000 + DUCT_TAPE_LOGO_BUILD_UP + 16, &frame, &drop));
	CHECK(frame == DUCT_TAPE_LOGO_TAPE_FRAME && drop == 0);

	/* and back the same way */
	CHECK(duct_tape_logo_toggle(&logo, 20000) == 0);
	CHECK(!duct_tape_logo_step(&logo, 20000 + DUCT_TAPE_LOGO_BUILD_UP / 2, &frame, &drop));
	CHECK(frame == DUCT_TAPE_LOGO_TAPE_FRAME && drop > 0);
	CHECK(duct_tape_logo_step(&logo, 20000 + DUCT_TAPE_LOGO_BUILD_UP + 100, &frame, &drop));
	CHECK(frame == DUCT_TAPE_LOGO_HALO_FRAME && drop == 0);

	/* the code twice in the build-up: it stops where it was, no click */
	CHECK(duct_tape_logo_toggle(&logo, 30000) == 1);
	CHECK(duct_tape_logo_toggle(&logo, 30500) == 0);
	CHECK(!duct_tape_logo_step(&logo, 31000, &frame, &drop));
	CHECK(frame == DUCT_TAPE_LOGO_HALO_FRAME && drop == 0);
	CHECK(!duct_tape_logo_step(&logo, 40000, &frame, &drop));

	/* (the clock wrapping in a build-up: still the right length) */
	CHECK(duct_tape_logo_toggle(&logo, (unsigned long)-100) == 1);
	CHECK(!duct_tape_logo_step(&logo, 100, &frame, &drop));
	CHECK(frame == DUCT_TAPE_LOGO_HALO_FRAME);
	CHECK(duct_tape_logo_step(&logo, DUCT_TAPE_LOGO_BUILD_UP, &frame, &drop));
	CHECK(frame == DUCT_TAPE_LOGO_TAPE_FRAME);
}

int main(void)
{
	test_whole_code();
	test_wrong_presses();
	test_partial_restarts();
	test_swallows();
	test_swap();
	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("duct tape logo: all passed\n");
	return 0;
}
