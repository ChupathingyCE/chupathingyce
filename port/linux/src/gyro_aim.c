/*
GYRO_AIM.C

Aiming with a gyroscope (gyro_aim.h). The filter is Lord BirchWood's, from
his PlayStation Vita port's vita_controls.c, with his permission.
*/

#include "gyro_aim.h"

#include <string.h>

#define GYRO_DEGREES_TO_RADIANS 0.017453292519943295

/* ---------- the settings */

static float gyro_clamped(double value, float low, float high, float fallback)
{
	/* (NaN fails every comparison) */
	if (!(value == value))
		return fallback;
	if (value < low)
		return low;
	if (value > high)
		return high;
	return (float)value;
}

void gyro_aim_settings_parse(struct gyro_aim_settings *settings, const char *aim, const char *axis,
	double sensitivity_horizontal, double sensitivity_vertical, int invert_horizontal, int invert_vertical,
	double deadzone_degrees)
{
	memset(settings, 0, sizeof(*settings));
	settings->zoom_only = aim && !strcmp(aim, "zoom");
	settings->enabled = settings->zoom_only || (aim && !strcmp(aim, "true"));
	settings->roll = axis && !strcmp(axis, "roll");
	settings->invert_horizontal = invert_horizontal != 0;
	settings->invert_vertical = invert_vertical != 0;
	settings->sensitivity_horizontal = gyro_clamped(sensitivity_horizontal, GYRO_AIM_SENSITIVITY_MINIMUM,
		GYRO_AIM_SENSITIVITY_MAXIMUM, 1.0f);
	settings->sensitivity_vertical = gyro_clamped(sensitivity_vertical, GYRO_AIM_SENSITIVITY_MINIMUM,
		GYRO_AIM_SENSITIVITY_MAXIMUM, 1.0f);
	settings->deadzone = (float)(gyro_clamped(deadzone_degrees, 0.0f, GYRO_AIM_DEADZONE_MAXIMUM,
		GYRO_AIM_DEADZONE_DEFAULT) * GYRO_DEGREES_TO_RADIANS);
}

/* ---------- the filter */

static float gyro_absolute(float value)
{
	return value < 0.0f ? -value : value;
}

/* (no libm: the consoles' builds and the unit test take this alone) */
static float gyro_square_root(float value)
{
	float guess = value > 1.0f ? value : 1.0f;
	int step;

	if (value <= 0.0f)
		return 0.0f;
	for (step = 0; step < 20; step++)
		guess = 0.5f * (guess + value / guess);
	return guess;
}

void gyro_filter_reset(struct gyro_filter *filter)
{
	memset(filter, 0, sizeof(*filter));
}

static void gyro_window_start(struct gyro_filter *filter, const float rate[3], const float accel[3], float dt)
{
	int axis;

	filter->window_time = dt;
	for (axis = 0; axis < 3; axis++)
	{
		filter->window_sum[axis] = rate[axis] * dt;
		filter->window_low[axis] = filter->window_high[axis] = rate[axis];
		filter->accel_low[axis] = filter->accel_high[axis] = accel ? accel[axis] : 0.0f;
	}
}

/* the bias: learnt from each second the controller lies still (the first
one taken as it is, later ones halfway, and only those within
GYRO_FILTER_STILL_BIAS_CHANGE of the bias learnt) */
static void gyro_calibrate(struct gyro_filter *filter, const float rate[3], const float accel[3], float dt)
{
	int axis, still = 1;

	if (filter->window_time <= 0.0f)
	{
		gyro_window_start(filter, rate, accel, dt);
		return;
	}
	for (axis = 0; axis < 3; axis++)
	{
		float low = rate[axis] < filter->window_low[axis] ? rate[axis] : filter->window_low[axis];
		float high = rate[axis] > filter->window_high[axis] ? rate[axis] : filter->window_high[axis];
		float accel_value = accel ? accel[axis] : 0.0f;
		float accel_low = accel_value < filter->accel_low[axis] ? accel_value : filter->accel_low[axis];
		float accel_high = accel_value > filter->accel_high[axis] ? accel_value : filter->accel_high[axis];

		if (high - low > GYRO_FILTER_STILL_SPREAD || accel_high - accel_low > GYRO_FILTER_STILL_ACCEL ||
			gyro_absolute(rate[axis]) > GYRO_FILTER_STILL_MEAN)
			still = 0;
		filter->window_low[axis] = low;
		filter->window_high[axis] = high;
		filter->accel_low[axis] = accel_low;
		filter->accel_high[axis] = accel_high;
	}
	if (!still)
	{
		/* (moved: a new window from this sample) */
		gyro_window_start(filter, rate, accel, dt);
		return;
	}
	filter->window_time += dt;
	for (axis = 0; axis < 3; axis++)
		filter->window_sum[axis] += rate[axis] * dt;
	if (filter->window_time >= GYRO_FILTER_STILL_TIME)
	{
		for (axis = 0; axis < 3; axis++)
		{
			float mean = filter->window_sum[axis] / filter->window_time;

			/* (once a bias is learnt, a still second must be near it: a
			slow steady pan, as smooth as a controller lying still, is not
			taken for drift) */
			if (gyro_absolute(mean) > GYRO_FILTER_STILL_MEAN ||
				(filter->still_count && gyro_absolute(mean - filter->bias[axis]) > GYRO_FILTER_STILL_BIAS_CHANGE))
				break;
		}
		if (axis == 3)
		{
			for (axis = 0; axis < 3; axis++)
			{
				float mean = filter->window_sum[axis] / filter->window_time;

				filter->bias[axis] = filter->still_count ? 0.5f * (filter->bias[axis] + mean) : mean;
			}
			filter->still_count++;
		}
		filter->window_time = 0.0f;
	}
}

void gyro_filter_sample(struct gyro_filter *filter, const float rate[3], const float accel[3], float dt,
	float deadzone, float angle[3])
{
	float corrected[3], out[3], magnitude, smoothing, alpha;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		/* (a sample that is not a number, or past any sensor's range, is
		none) */
		if (!(gyro_absolute(rate[axis]) <= GYRO_FILTER_MAXIMUM_RATE) || (accel && !(accel[axis] == accel[axis])))
			return;
	}
	if (!(dt >= 0.0f) || dt > GYRO_FILTER_MAXIMUM_GAP)
		dt = 0.0f;
	if (deadzone < 0.0f)
		deadzone = 0.0f;
	gyro_calibrate(filter, rate, accel, dt);
	for (axis = 0; axis < 3; axis++)
		corrected[axis] = rate[axis] - filter->bias[axis];
	/* small motion smoothed (fully at GYRO_FILTER_SMOOTH_LOW and under, not
	at all from GYRO_FILTER_SMOOTH_HIGH). Only the share of each sample that
	is smoothed goes through the average and the rest goes straight out
	(BirchWood's averaged the whole rate, by the share of the newest
	sample: a fast turn stopped short then ran on for a few degrees) */
	magnitude = gyro_square_root(corrected[0] * corrected[0] + corrected[1] * corrected[1] +
		corrected[2] * corrected[2]);
	smoothing = magnitude <= GYRO_FILTER_SMOOTH_LOW ? 1.0f : magnitude >= GYRO_FILTER_SMOOTH_HIGH ? 0.0f :
		(GYRO_FILTER_SMOOTH_HIGH - magnitude) / (GYRO_FILTER_SMOOTH_HIGH - GYRO_FILTER_SMOOTH_LOW);
	alpha = dt > 0.0f ? dt / (GYRO_FILTER_SMOOTH_TIME + dt) : 0.0f;
	for (axis = 0; axis < 3; axis++)
	{
		filter->smoothed[axis] += (corrected[axis] * smoothing - filter->smoothed[axis]) * alpha;
		out[axis] = corrected[axis] * (1.0f - smoothing) + filter->smoothed[axis];
	}
	/* the deadzone, on the smoothed rate's size */
	magnitude = gyro_square_root(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
	for (axis = 0; axis < 3; axis++)
	{
		filter->rate[axis] = magnitude > deadzone ? out[axis] * (magnitude - deadzone) / magnitude : 0.0f;
		angle[axis] += filter->rate[axis] * dt;
	}
}

/* ---------- the turn */

void gyro_aim_turn(const struct gyro_aim_settings *settings, float turn_yaw, float turn_pitch, float *yaw,
	float *pitch)
{
	*yaw = (settings->invert_horizontal ? -turn_yaw : turn_yaw) * settings->sensitivity_horizontal;
	*pitch = (settings->invert_vertical ? -turn_pitch : turn_pitch) * settings->sensitivity_vertical;
}

void gyro_aim_look(const struct gyro_aim_settings *settings, const float angle[3], float *yaw, float *pitch)
{
	gyro_aim_turn(settings, settings->roll ? angle[2] : angle[1], angle[0], yaw, pitch);
}
