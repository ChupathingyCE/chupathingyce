#include "harness.h"

#include <math.h>

struct observer_result
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
	real field_of_view;
};

#include "config.inc"

static real distance_squared(real_point3d const *a, real_point3d const *b)
{
	real x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;

	return x * x + y * y + z * z;
}

#include "under_test.inc"

/* A cutscene camera's snapshots (render_interpolation.c's timed views) and
the cuts observer_timed_cut finds between them: a fast camera's flight is
never a cut, however far apart its snapshots fall; a jump to another point
or a turn is; and a fast camera's first move after a cut is not. */
static struct observer_result at(real x, real y)
{
	struct observer_result result = {{x, y, 10.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 1.2f};

	return result;
}

/* frames a tick or a little more apart (uncapped, a long frame now and
then), from a fixed sequence so every system draws the same times */
static double gap(int index)
{
	static const double gaps[] = {1.0, 1.02, 1.3, 1.07, 1.0, 1.18, 1.01, 2.4, 1.0, 1.11};

	return gaps[index % (int)(sizeof(gaps) / sizeof(gaps[0]))];
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	/* a camera flying 3.5 units a tick, further than a player's view could
	move in one (d40's Longsword): never a cut */
	CASE("fast-camera")
	{
		double times[3] = {0.0, 1.0, 2.05};
		int frame;

		for (frame = 0; frame < 40; frame++)
		{
			struct observer_result earlier = at((real)(3.5 * times[0]), 0.0f);
			struct observer_result a = at((real)(3.5 * times[1]), 0.0f);
			struct observer_result b = at((real)(3.5 * times[2]), 0.0f);

			CHECK(!observer_timed_cut(&earlier, times[0], 3.5f, &a, times[1], &b, times[2]),
				"a camera flying 3.5 units a tick was cut between snapshots %g ticks apart", times[2] - times[1]);
			times[0] = times[1];
			times[1] = times[2];
			times[2] += gap(frame);
		}
		return 0;
	}
	/* a camera set to another point, a cut from a moving camera, and a turn */
	CASE("cut")
	{
		struct observer_result still = at(0.0f, 0.0f);
		struct observer_result far_away = at(40.0f, 25.0f);
		struct observer_result moving_before = at(-3.5f, 0.0f);
		struct observer_result turned = at(0.0f, 0.0f);

		CHECK(observer_timed_cut(&still, 0.0, 0.0f, &still, 1.0, &far_away, 2.1),
			"a still camera set 47 units away was not a cut");
		CHECK(observer_timed_cut(&moving_before, 0.0, 3.5f, &still, 1.0, &far_away, 2.0),
			"a moving camera cut 47 units away was not a cut");
		turned.forward.i = 0.0f;
		turned.forward.j = 1.0f;
		CHECK(observer_timed_cut(&still, 0.0, 0.0f, &still, 1.0, &turned, 2.0),
			"a camera turned a quarter of a revolution was not a cut");
		CHECK(observer_timed_cut(NULL, 0.0, 0.0f, &still, 1.0, &far_away, 2.0),
			"a jump with no snapshot before was not a cut");
		return 0;
	}
	/* the first move after a cut: no move since the cut to carry on, but
	the view's last speed (3.5 a tick, accelerating to 3.8) */
	CASE("first-move-after-cut")
	{
		struct observer_result a = at(100.0f, 0.0f);
		struct observer_result b = at(103.8f, 0.0f);

		CHECK(!observer_timed_cut(NULL, 0.0, 3.5f, &a, 1.0, &b, 2.0),
			"a fast camera's first move after a cut (3.8 units, last speed 3.5) was taken for a cut");
		return 0;
	}
	/* not a number in a snapshot: a cut */
	CASE("not-a-number")
	{
		struct observer_result a = at(0.0f, 0.0f);
		struct observer_result b = at(NAN, 0.0f);

		CHECK(observer_timed_cut(&a, 0.0, 0.0f, &a, 1.0, &b, 2.0), "a position not a number was no cut");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
