/*
GYRO_AIM_TEST.C

Tests of gyro aiming (port/linux/src/gyro_aim.c), for tools/test_gyro_aim.py,
on made-up sensor samples: the bias learnt while still, the smoothing, the
deadzone, the axes and the settings read from config.toml's values.

	gyro_aim_test                      runs the tests
	gyro_aim_test settings <aim> <axis> <horizontal> <vertical> <invert
	    horizontal> <invert vertical> <deadzone>
	                                   prints the settings read from
	                                   config.toml's text for each, as
	                                   xinput_sdl.c reads them
*/

#include "gyro_aim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do { if (!(condition)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)
#define NEAR(value, expected, within) CHECK(fabs((double)(value) - (double)(expected)) <= (within))

#define DEGREES(value) ((float)((value) * 0.017453292519943295))
/* a DualSense's rate: samples 4 ms apart */
#define DT 0.004f

static const float one_g[3] = { 0.0f, 1.0f, 0.0f };

/* seconds of the same rate (plus a little noise on every axis, as a real
sensor has); returns the angle turned */
static void run(struct gyro_filter *filter, const float rate[3], const float *accel, float seconds, float deadzone,
	float angle[3])
{
	int steps = (int)(seconds / DT + 0.5f), step;

	for (step = 0; step < steps; step++)
	{
		/* (+-0.2 degrees a second, alternating) */
		float noise = DEGREES(step & 1 ? 0.2 : -0.2);
		float sample[3];

		sample[0] = rate[0] + noise;
		sample[1] = rate[1] - noise;
		sample[2] = rate[2] + noise;
		gyro_filter_sample(filter, sample, accel, DT, deadzone, angle);
	}
}

static void test_settings(void)
{
	struct gyro_aim_settings settings;

	/* the defaults: off */
	gyro_aim_settings_parse(&settings, "false", "yaw", 1.0, 1.0, 0, 0, 0.75);
	CHECK(!settings.enabled && !settings.zoom_only && !settings.roll);
	NEAR(settings.sensitivity_horizontal, 1.0f, 1e-6);
	NEAR(settings.deadzone, DEGREES(0.75), 1e-6);
	gyro_aim_settings_parse(&settings, "true", "roll", 2.0, 0.5, 1, 1, 0.0);
	CHECK(settings.enabled && !settings.zoom_only && settings.roll);
	CHECK(settings.invert_horizontal && settings.invert_vertical);
	NEAR(settings.sensitivity_horizontal, 2.0f, 1e-6);
	NEAR(settings.sensitivity_vertical, 0.5f, 1e-6);
	NEAR(settings.deadzone, 0.0f, 1e-9);
	gyro_aim_settings_parse(&settings, "zoom", NULL, 1.0, 1.0, 0, 0, 0.75);
	CHECK(settings.enabled && settings.zoom_only && !settings.roll);
	/* words it does not know: off, and yaw */
	gyro_aim_settings_parse(&settings, "yes", "steer", 1.0, 1.0, 0, 0, 0.75);
	CHECK(!settings.enabled && !settings.zoom_only && !settings.roll);
	gyro_aim_settings_parse(&settings, NULL, NULL, 1.0, 1.0, 0, 0, 0.75);
	CHECK(!settings.enabled);
	/* clamped to the ranges */
	gyro_aim_settings_parse(&settings, "true", "yaw", 0.0, 50.0, 0, 0, 99.0);
	NEAR(settings.sensitivity_horizontal, GYRO_AIM_SENSITIVITY_MINIMUM, 1e-6);
	NEAR(settings.sensitivity_vertical, GYRO_AIM_SENSITIVITY_MAXIMUM, 1e-6);
	NEAR(settings.deadzone, DEGREES(GYRO_AIM_DEADZONE_MAXIMUM), 1e-6);
	gyro_aim_settings_parse(&settings, "true", "yaw", -3.0, 1.0, 0, 0, -1.0);
	NEAR(settings.sensitivity_horizontal, GYRO_AIM_SENSITIVITY_MINIMUM, 1e-6);
	NEAR(settings.deadzone, 0.0f, 1e-9);
	/* not a number: the default */
	gyro_aim_settings_parse(&settings, "true", "yaw", NAN, NAN, 0, 0, NAN);
	NEAR(settings.sensitivity_horizontal, 1.0f, 1e-6);
	NEAR(settings.sensitivity_vertical, 1.0f, 1e-6);
	NEAR(settings.deadzone, DEGREES(GYRO_AIM_DEADZONE_DEFAULT), 1e-6);
}

/* a controller lying still with a drifting sensor turns nothing once the
bias is learnt, and the bias follows a drift that changes */
static void test_drift(void)
{
	struct gyro_filter filter;
	float drift[3] = { DEGREES(0.5), DEGREES(-1.5), DEGREES(1.0) };
	float angle[3] = { 0.0f, 0.0f, 0.0f };

	gyro_filter_reset(&filter);
	/* the first second: learning, nothing out of the deadzone */
	run(&filter, drift, one_g, 1.1f, DEGREES(0.75), angle);
	CHECK(filter.still_count == 1);
	NEAR(filter.bias[0], drift[0], DEGREES(0.05));
	NEAR(filter.bias[1], drift[1], DEGREES(0.05));
	NEAR(filter.bias[2], drift[2], DEGREES(0.05));
	/* a minute more: the view stays where it was */
	memset(angle, 0, sizeof(angle));
	run(&filter, drift, one_g, 60.0f, DEGREES(0.75), angle);
	NEAR(angle[0], 0.0f, DEGREES(0.01));
	NEAR(angle[1], 0.0f, DEGREES(0.01));
	NEAR(angle[2], 0.0f, DEGREES(0.01));
	/* even with no deadzone at all, it drifts less than a degree a minute */
	memset(angle, 0, sizeof(angle));
	run(&filter, drift, one_g, 60.0f, 0.0f, angle);
	NEAR(angle[1], 0.0f, DEGREES(1.0));
	/* the drift changes (the sensor warmed up): learnt again, halfway each
	still second */
	drift[1] = DEGREES(-3.0);
	run(&filter, drift, one_g, 10.0f, DEGREES(0.75), angle);
	NEAR(filter.bias[1], drift[1], DEGREES(0.05));
	/* without the deadzone and without the learnt bias, the same minute
	would have turned the view 3 degrees a second: 180 degrees */
	{
		struct gyro_filter fresh;
		float lost[3] = { 0.0f, 0.0f, 0.0f };
		float moving[3] = { DEGREES(0.0), DEGREES(-3.0), DEGREES(0.0) };
		float shaking[3] = { 0.0f, 1.0f, 0.0f };
		int step;

		gyro_filter_reset(&fresh);
		/* (held in a hand: the accelerometer shakes, so nothing is learnt) */
		for (step = 0; step < 15000; step++)
		{
			shaking[0] = step & 1 ? 0.05f : -0.05f;
			gyro_filter_sample(&fresh, moving, shaking, DT, 0.0f, lost);
		}
		CHECK(fresh.still_count == 0);
		NEAR(lost[1], DEGREES(-180.0), DEGREES(1.0));
	}
}

/* a real turn is never taken for the bias, and comes through whole */
static void test_turn(void)
{
	struct gyro_filter filter;
	float angle[3] = { 0.0f, 0.0f, 0.0f };
	float still[3] = { 0.0f, 0.0f, 0.0f };
	float turn[3] = { 0.0f, DEGREES(90.0), 0.0f };

	gyro_filter_reset(&filter);
	run(&filter, still, one_g, 1.1f, DEGREES(0.75), angle);
	memset(angle, 0, sizeof(angle));
	/* a quarter turn left in a second, then held */
	run(&filter, turn, one_g, 1.0f, DEGREES(0.75), angle);
	run(&filter, still, one_g, 0.5f, DEGREES(0.75), angle);
	/* (the deadzone takes 0.75 of the 90 degrees a second), and the view
	stops when the controller does: no run-on after the turn */
	NEAR(angle[1], DEGREES(90.0 - 0.75), DEGREES(0.2));
	NEAR(angle[0], 0.0f, DEGREES(0.2));
	NEAR(filter.bias[1], 0.0f, DEGREES(0.05));
	/* a slow steady turn in a hand (5 degrees a second, under the still
	mean, but the accelerometer shakes) is not learnt as bias either */
	{
		float slow[3] = { 0.0f, DEGREES(5.0), 0.0f };
		float shaking[3] = { 0.0f, 1.0f, 0.0f };
		int step;

		memset(angle, 0, sizeof(angle));
		for (step = 0; step < 1000; step++)
		{
			shaking[2] = step & 1 ? 0.04f : -0.04f;
			gyro_filter_sample(&filter, slow, shaking, DT, DEGREES(0.75), angle);
		}
		NEAR(filter.bias[1], 0.0f, DEGREES(0.05));
		NEAR(angle[1], DEGREES((5.0 - 0.75) * 4.0), DEGREES(0.5));
	}
}

/* hand tremor is smoothed; a fast turn is not */
static void test_smoothing(void)
{
	struct gyro_filter filter;
	float angle[3] = { 0.0f, 0.0f, 0.0f };
	float sample[3] = { 0.0f, 0.0f, 0.0f };
	float largest = 0.0f;
	int step;

	gyro_filter_reset(&filter);
	/* a 3 degree a second tremor, flipping every sample: the smoothed rate
	stays well under it */
	for (step = 0; step < 500; step++)
	{
		sample[1] = DEGREES(step & 1 ? 3.0 : -3.0);
		gyro_filter_sample(&filter, sample, NULL, DT, 0.0f, angle);
		if (step > 10 && fabsf(filter.smoothed[1]) > largest)
			largest = fabsf(filter.smoothed[1]);
	}
	CHECK(largest < DEGREES(0.5));
	NEAR(angle[1], 0.0f, DEGREES(0.05));
	/* a fast flick (60 degrees a second) is passed on at once */
	gyro_filter_reset(&filter);
	sample[1] = DEGREES(60.0);
	gyro_filter_sample(&filter, sample, NULL, DT, 0.0f, angle);
	NEAR(filter.rate[1], DEGREES(60.0), DEGREES(0.01));
	/* between the two, partly smoothed: a step to 10 degrees a second
	reaches it within a few time constants, and not at once */
	gyro_filter_reset(&filter);
	sample[1] = DEGREES(10.0);
	gyro_filter_sample(&filter, sample, NULL, DT, 0.0f, angle);
	CHECK(filter.rate[1] > 0.0f && filter.rate[1] < DEGREES(10.0));
	for (step = 0; step < 100; step++)
		gyro_filter_sample(&filter, sample, NULL, DT, 0.0f, angle);
	NEAR(filter.rate[1], DEGREES(10.0), DEGREES(0.01));
}

static void test_deadzone(void)
{
	struct gyro_filter filter;
	float angle[3] = { 0.0f, 0.0f, 0.0f };
	float sample[3] = { 0.0f, 0.0f, 0.0f };
	int step;

	/* under the deadzone: nothing */
	gyro_filter_reset(&filter);
	sample[0] = DEGREES(1.5);
	for (step = 0; step < 500; step++)
		gyro_filter_sample(&filter, sample, NULL, DT, DEGREES(2.0), angle);
	CHECK(angle[0] == 0.0f && angle[1] == 0.0f && angle[2] == 0.0f);
	/* over it: reduced by it, with no jump at its edge */
	gyro_filter_reset(&filter);
	sample[0] = DEGREES(40.0);
	gyro_filter_sample(&filter, sample, NULL, DT, DEGREES(2.0), angle);
	NEAR(filter.rate[0], DEGREES(38.0), DEGREES(0.01));
	/* on the size of the rate, not each axis: the direction is kept */
	gyro_filter_reset(&filter);
	sample[0] = DEGREES(30.0);
	sample[1] = DEGREES(40.0);
	gyro_filter_sample(&filter, sample, NULL, DT, DEGREES(5.0), angle);
	NEAR(filter.rate[0] / filter.rate[1], 0.75f, 1e-4);
	NEAR(filter.rate[0], DEGREES(30.0 * 45.0 / 50.0), DEGREES(0.01));
}

static void test_gaps(void)
{
	struct gyro_filter filter;
	float angle[3] = { 0.0f, 0.0f, 0.0f };
	float sample[3] = { 0.0f, DEGREES(100.0), 0.0f };
	float bad[3] = { 0.0f, NAN, 0.0f };

	gyro_filter_reset(&filter);
	/* a long gap (the app in the background, a controller asleep) and a
	time going backwards turn nothing */
	gyro_filter_sample(&filter, sample, NULL, 2.0f, 0.0f, angle);
	gyro_filter_sample(&filter, sample, NULL, -0.01f, 0.0f, angle);
	CHECK(angle[1] == 0.0f);
	/* a sample that is not a number is ignored, and leaves no NaN behind */
	gyro_filter_sample(&filter, bad, NULL, DT, 0.0f, angle);
	gyro_filter_sample(&filter, sample, one_g, DT, 0.0f, angle);
	CHECK(angle[1] == angle[1] && angle[1] > 0.0f);
	CHECK(filter.bias[1] == filter.bias[1]);
	/* nor is an infinite or huge one: the filter goes on turning */
	{
		float infinite[3] = { 0.0f, INFINITY, 0.0f };
		float huge[3] = { -1.0e30f, 0.0f, 0.0f };
		float before;

		gyro_filter_sample(&filter, infinite, NULL, DT, 0.0f, angle);
		gyro_filter_sample(&filter, huge, one_g, DT, 0.0f, angle);
		before = angle[1];
		gyro_filter_sample(&filter, sample, one_g, DT, 0.0f, angle);
		CHECK(filter.smoothed[0] == filter.smoothed[0] && filter.smoothed[1] == filter.smoothed[1]);
		CHECK(angle[1] == angle[1] && angle[1] > before);
	}
}

static void test_look(void)
{
	struct gyro_aim_settings settings;
	float angle[3] = { 0.1f, 0.2f, 0.3f };
	float yaw, pitch;

	gyro_aim_settings_parse(&settings, "true", "yaw", 2.0, 0.5, 0, 0, 0.75);
	gyro_aim_look(&settings, angle, &yaw, &pitch);
	NEAR(yaw, 0.4f, 1e-6);
	NEAR(pitch, 0.05f, 1e-6);
	gyro_aim_settings_parse(&settings, "true", "roll", 1.0, 1.0, 1, 1, 0.75);
	gyro_aim_look(&settings, angle, &yaw, &pitch);
	NEAR(yaw, -0.3f, 1e-6);
	NEAR(pitch, -0.1f, 1e-6);
	/* the Switch's turn, added up by its host */
	gyro_aim_settings_parse(&settings, "true", "yaw", 1.5, 1.0, 1, 0, 0.75);
	gyro_aim_turn(&settings, 0.2f, -0.1f, &yaw, &pitch);
	NEAR(yaw, -0.3f, 1e-6);
	NEAR(pitch, -0.1f, 1e-6);
}

/* (config_real's and config_boolean's reading of the text) */
static int print_settings(char **values)
{
	struct gyro_aim_settings settings;

	gyro_aim_settings_parse(&settings, values[0], values[1], strtod(values[2], NULL), strtod(values[3], NULL),
		!strcmp(values[4], "true"), !strcmp(values[5], "true"), strtod(values[6], NULL));
	printf("enabled=%d\nzoom_only=%d\nroll=%d\ninvert_horizontal=%d\ninvert_vertical=%d\n"
		"sensitivity_horizontal=%g\nsensitivity_vertical=%g\ndeadzone_degrees=%.4f\n",
		settings.enabled, settings.zoom_only, settings.roll, settings.invert_horizontal,
		settings.invert_vertical, settings.sensitivity_horizontal, settings.sensitivity_vertical,
		settings.deadzone / 0.017453292519943295);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 9 && !strcmp(argv[1], "settings"))
		return print_settings(argv + 2);
	test_settings();
	test_drift();
	test_turn();
	test_smoothing();
	test_deadzone();
	test_gaps();
	test_look();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("gyro aim: all passed\n");
	return 0;
}
