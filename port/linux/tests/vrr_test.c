/*
VRR_TEST.C

Tests of display.vrr's rules (port/linux/src/vrr.c): off unless chosen or
the system says VRR is active, the frames held 3 under the refresh rate or
at display.max_fps where it is lower. tools/test_vrr.py runs them.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../src/vrr.h"

#include <math.h>
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

static int near(float a, float b)
{
	return fabsf(a - b) < 0.001f;
}

int main(void)
{
	char why[160];
	int system;

	/* (words: anything unknown is off, the default) */
	CHECK(vrr_mode("off") == VRR_MODE_OFF, "off");
	CHECK(vrr_mode("on") == VRR_MODE_ON, "on");
	CHECK(vrr_mode("auto") == VRR_MODE_AUTO, "auto");
	CHECK(vrr_mode("") == VRR_MODE_OFF && vrr_mode(NULL) == VRR_MODE_OFF && vrr_mode("yes") == VRR_MODE_OFF,
		"unknown words are off");

	/* (off never, on always, whatever the system says) */
	for (system = VRR_SYSTEM_NONE; system <= VRR_SYSTEM_DRM_OFF; system++)
	{
		CHECK(!vrr_decide(VRR_MODE_OFF, system, why, sizeof(why)), "off with system %d", system);
		CHECK(vrr_decide(VRR_MODE_ON, system, why, sizeof(why)), "on with system %d", system);
	}
	/* (auto: only where the system says VRR is active) */
	CHECK(!vrr_decide(VRR_MODE_AUTO, VRR_SYSTEM_NONE, why, sizeof(why)), "auto, nothing said");
	CHECK(strstr(why, "says nothing") != NULL, "auto's reason: %s", why);
	CHECK(vrr_decide(VRR_MODE_AUTO, VRR_SYSTEM_GAMESCOPE_ON, why, sizeof(why)), "auto, gamescope on");
	CHECK(!vrr_decide(VRR_MODE_AUTO, VRR_SYSTEM_GAMESCOPE_OFF, why, sizeof(why)), "auto, gamescope off");
	CHECK(vrr_decide(VRR_MODE_AUTO, VRR_SYSTEM_DRM_ON, why, sizeof(why)), "auto, DRM on");
	CHECK(!vrr_decide(VRR_MODE_AUTO, VRR_SYSTEM_DRM_OFF, why, sizeof(why)), "auto, DRM off");
	CHECK(!vrr_decide(VRR_MODE_AUTO, VRR_SYSTEM_DRM_OFF, NULL, 0), "no reason asked for");

	/* (3 under the refresh rate; display.max_fps where lower; 0 unknown) */
	CHECK(near(vrr_frame_rate(120.0f, 0), 117.0f), "120 Hz: %f", (double)vrr_frame_rate(120.0f, 0));
	CHECK(near(vrr_frame_rate(144.0f, 0), 141.0f), "144 Hz");
	CHECK(near(vrr_frame_rate(143.67f, -1), 140.67f), "143.67 Hz, no limit");
	CHECK(near(vrr_frame_rate(144.0f, 60), 60.0f), "a lower max_fps wins");
	CHECK(near(vrr_frame_rate(144.0f, 240), 141.0f), "a higher max_fps does not");
	CHECK(near(vrr_frame_rate(0.0f, 0), 0.0f), "refresh not known: vsync alone");
	CHECK(near(vrr_frame_rate(0.0f, 90), 90.0f), "refresh not known: max_fps");
	CHECK(near(vrr_frame_rate(2.0f, 0), 0.0f), "a refresh under the headroom");

	/* (the system: anything it says is one of the answers) */
	system = vrr_system();
	CHECK(system >= VRR_SYSTEM_NONE && system <= VRR_SYSTEM_DRM_OFF, "system %d", system);

	if (failures)
		printf("%d failed\n", failures);
	else
		printf("vrr: all passed\n");
	return failures ? 1 : 0;
}
