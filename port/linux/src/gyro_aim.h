/*
GYRO_AIM.H

Aiming with a gyroscope (gyro_aim.c): the settings (config.toml's
input.gyro_*), the filter each sensor's samples go through, and the turn
the game gets from it.

The filter is Lord BirchWood's from his PlayStation Vita port (its
vita_controls.c), used here with his permission: the sensor's bias learnt
while the controller lies still, small motion (hand tremor) smoothed, and a
deadzone on what is left. Thanks, BirchWood! One change: only the small
share of the motion goes through the smoothing (gyro_filter_sample).

The axes are SDL's for a controller held in front of you, which are the
Vita's too: x to the right, y up, z towards you; angular velocity in
radians a second, positive counter-clockwise looking down the axis. So
turning the controller left (yaw) is +y, tilting its top edge towards you
(the view looking up) is +x, and turning it like a steering wheel to the
left (roll) is +z.

Who reads the sensor: xinput_sdl.c with SDL's sensors (desktop controllers,
the Steam Deck, Android's controllers), port/android/host/host_touch.c for
the phone's own gyroscope with the touch controls, and the Switch's host,
which gives xinput_sdl.c the turn already added up (it has a filter of its
own, and keeps it). What the game does with the turn: player_control.c adds
it to the view as it adds the mouse's.

No SDL or game types, so that the build machine can test it
(port/linux/tests/gyro_aim_test.c).
*/

#ifndef GYRO_AIM_H
#define GYRO_AIM_H

/* ---------- the settings */

/* input.gyro_sensitivity_*: how far the view turns per radian the
controller turns, from GYRO_AIM_SENSITIVITY_MINIMUM to _MAXIMUM */
#define GYRO_AIM_SENSITIVITY_MINIMUM 0.25f
#define GYRO_AIM_SENSITIVITY_MAXIMUM 10.0f
/* input.gyro_deadzone: degrees a second, from 0 to this */
#define GYRO_AIM_DEADZONE_MAXIMUM 10.0f
/* (BirchWood's: 0.75 degrees a second) */
#define GYRO_AIM_DEADZONE_DEFAULT 0.75f

struct gyro_aim_settings
{
	/* input.gyro_aim: "true", or "zoom" (zoom_only: only while zoomed in) */
	int enabled;
	int zoom_only;
	/* input.gyro_horizontal_axis "roll": left and right by turning the
	controller like a steering wheel; "yaw" (0), by turning it */
	int roll;
	int invert_horizontal;
	int invert_vertical;
	float sensitivity_horizontal;
	float sensitivity_vertical;
	/* radians a second */
	float deadzone;
};

/* the settings from config.toml's values, each out of range clamped to it
and each not understood (NULL, a word it does not know, not a number) at
its default: aim "false", axis "yaw", sensitivities 1, deadzone
GYRO_AIM_DEADZONE_DEFAULT (degrees a second) */
void gyro_aim_settings_parse(struct gyro_aim_settings *settings, const char *aim, const char *axis,
	double sensitivity_horizontal, double sensitivity_vertical, int invert_horizontal, int invert_vertical,
	double deadzone_degrees);

/* ---------- the filter */

/* small motion is smoothed (hand tremor): fully below the first rate, not
at all above the second (radians a second: 5 and 15 degrees), with this
time constant (seconds) */
#define GYRO_FILTER_SMOOTH_LOW 0.08727f
#define GYRO_FILTER_SMOOTH_HIGH 0.26180f
#define GYRO_FILTER_SMOOTH_TIME 0.040f
/* still (the bias learnt): a second in which no axis' rate moves more than
2 degrees a second (from lowest to highest), the accelerometer no more than
0.03 g, and the mean rate is under 6 degrees a second */
#define GYRO_FILTER_STILL_TIME 1.0f
#define GYRO_FILTER_STILL_SPREAD 0.03491f
#define GYRO_FILTER_STILL_ACCEL 0.03f
#define GYRO_FILTER_STILL_MEAN 0.10472f
/* a gap longer than this (seconds) between two samples integrates nothing
(BirchWood's Vita sampled at 60 Hz or more and had 0.05; a phone's and a
Bluetooth controller's samples come further apart now and then) */
#define GYRO_FILTER_MAXIMUM_GAP 0.1f
/* a rate past this (radians a second: about 2900 degrees a second, past
every controller's and phone's range) is a bad sample, as one that is not a
number: an infinite or huge one would leave NaN in the smoothing for good */
#define GYRO_FILTER_MAXIMUM_RATE 50.0f

struct gyro_filter
{
	/* the bias (radians a second per axis), and how many still seconds
	it was learnt from */
	float bias[3];
	int still_count;
	/* the current still window */
	float window_time;
	float window_sum[3];
	float window_low[3], window_high[3];
	float accel_low[3], accel_high[3];
	/* the smoothed share of the rate (after the bias), and the last rate
	given out (after the deadzone) */
	float smoothed[3];
	float rate[3];
};

/* a filter that has learnt nothing */
void gyro_filter_reset(struct gyro_filter *filter);

/* one sample: rate (radians a second), accel (g; NULL for none), dt
(seconds since the previous sample), deadzone (radians a second). Adds the
angle turned (radians per axis) to angle[] */
void gyro_filter_sample(struct gyro_filter *filter, const float rate[3], const float accel[3], float dt,
	float deadzone, float angle[3]);

/* ---------- the turn */

/* the game's yaw (left positive) and pitch (up positive), in radians, for
the angles the controller turned, with the settings' axis, sensitivity and
invert */
void gyro_aim_look(const struct gyro_aim_settings *settings, const float angle[3], float *yaw, float *pitch);

/* the same for a turn already added up into yaw and pitch (the Switch's
host): the settings' sensitivity and invert */
void gyro_aim_turn(const struct gyro_aim_settings *settings, float turn_yaw, float turn_pitch, float *yaw,
	float *pitch);

#endif
