/*
PAUSE_LEAVE_TEST.C

Tests of whose player a network game's LEAVE GAME takes out
(port/linux/game/pause_leave.h): a gamepad on another port than the
player's (one person playing, any controller driving the pause screen)
leaves as the player, as the keyboard and the mouse do. Built and run by
tools/test_pause_leave.py; exits nonzero on a failure.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../game/pause_leave.h"

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

int main(void)
{
	/* the Xbox's way: the press's controller has the player */
	CHECK(pause_leave_controller(0, 1, 0) == 0);
	/* the keyboard drives port 0, the player's */
	CHECK(pause_leave_controller(0, 1, 0) == 0);
	/* the mouse: no controller, the screen's player */
	CHECK(pause_leave_controller(-1, 0, 0) == 0);
	/* the report: one person, their gamepad on port 1 (another device
	listed first) or 3 (left there by split screen), the player on 0: the
	player leaves (it was port 1's player, none, and nothing happened) */
	CHECK(pause_leave_controller(1, 0, 0) == 0);
	CHECK(pause_leave_controller(3, 0, 0) == 0);
	/* split screen: each player's own pause screen and controller */
	CHECK(pause_leave_controller(2, 1, 2) == 2);
	CHECK(pause_leave_controller(1, 1, 1) == 1);
	/* a press whose controller has a player keeps it, whatever screen */
	CHECK(pause_leave_controller(1, 1, 0) == 1);
	/* a screen with no player: the press's controller, else the first */
	CHECK(pause_leave_controller(2, 0, -1) == 2);
	CHECK(pause_leave_controller(-1, 0, -1) == 0);
	/* out of range on both sides */
	CHECK(pause_leave_controller(7, 0, 9) == 0);
	CHECK(pause_leave_controller(7, 1, 1) == 1);
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("all passed\n");
	return 0;
}
