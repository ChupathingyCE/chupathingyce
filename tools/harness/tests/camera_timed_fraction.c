#include "harness.h"

#include <math.h>
#include "config.inc"
#include "under_test.inc"

/* The camera's timed blend (render_interpolation.c) as the frames of a tick
draw it: the snapshot of the tick's first frame, then each frame's fraction.
Every frame's blend is a number in the range, and it moves on with the
fraction: never stopped for the tick. */
static void tick(real previous_fraction, real latest_fraction, const char *what)
{
	real last = -1.0f;
	int frame;

	for (frame = 0; frame < 8; frame++)
	{
		real t = latest_fraction + (1.0f - latest_fraction) * (real)frame / 8.0f;
		real timed = camera_timed_fraction(t, previous_fraction, latest_fraction);

		CHECK(isfinite(timed), "%s: at %g, the blend is %g", what, t, timed);
		CHECK(timed >= -0.5f && timed <= 1.0f, "%s: at %g, the blend is %g", what, t, timed);
		CHECK(timed > last, "%s: at %g, the blend is %g, no further than the frame before (%g)", what, t, timed,
			last);
		last = timed;
	}
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	/* paused (or the clock stopped): every frame's fraction a whole tick,
	and the camera drawn at the latest, as the objects are */
	CASE("paused")
	{
		real timed = camera_timed_fraction(1.0f, 1.0f, 1.0f);

		CHECK(timed == 1.0f, "paused: the blend is %g, not the latest snapshot (1)", timed);
		return 0;
	}
	/* the tick after a pause: its snapshot taken at the start of the tick,
	or a part of it in, after one taken paused */
	CASE("unpaused")
	{
		CHECK(camera_timed_fraction(1.0f, 1.0f, 0.0f) == 1.0f, "unpaused: a whole tick in is not the latest");
		tick(1.0f, 0.0f, "unpaused at the start of a tick");
		tick(1.0f, 0.3f, "unpaused a part of a tick in");
		tick(1.0f, 0.95f, "unpaused near the end of a tick");
		/* then running again */
		tick(0.0f, 0.0f, "the tick after");
		tick(0.3f, 0.1f, "the tick after that");
		return 0;
	}
	/* snapshots taken nearly a tick apart less a tick (the previous late in
	its tick, the latest at the start of its): too close to tell apart */
	CASE("close-snapshots")
	{
		tick(0.98f, 0.0f, "snapshots 0.02 of a tick apart");
		tick(0.999f, 0.0f, "snapshots 0.001 of a tick apart");
		return 0;
	}
	/* not a number in a snapshot's fraction: the fraction alone */
	CASE("not-a-number")
	{
		CHECK(camera_timed_fraction(0.5f, NAN, 0.2f) == 0.5f, "a previous fraction not a number");
		CHECK(camera_timed_fraction(0.5f, 0.2f, NAN) == 0.5f, "a latest fraction not a number");
		return 0;
	}
	/* while the clock runs, the timed blend as it was */
	CASE("running")
	{
		static const real fractions[][2] = {{0.0f, 0.0f}, {0.4f, 0.1f}, {0.1f, 0.6f}, {0.3f, 0.1f}, {0.9f, 0.85f}};
		int i;

		for (i = 0; i < (int)(sizeof(fractions) / sizeof(fractions[0])); i++)
		{
			real previous = fractions[i][0], latest = fractions[i][1], t = (latest + 1.0f) / 2.0f;
			real expected = PIN((t - previous) / (1.0f + latest - previous), -0.5f, 1.0f);

			CHECK(fabsf(camera_timed_fraction(t, previous, latest) - expected) < 1e-6f, "running %g, %g: %g, not %g", previous,
				latest, camera_timed_fraction(t, previous, latest), expected);
			tick(previous, latest, "running");
		}
		return 0;
	}
	/* a long frame: by the times while the clock runs, by the ticks alone
	when it didn't, never a division by nothing */
	CASE("long-frame")
	{
		real along;

		along = camera_long_frame_along(3.0f, 0.2f, 0.5f);
		CHECK(fabsf(along - 2.3f / 3.3f) < 1e-6f, "running: %g", along);
		along = camera_long_frame_along(3.0f, 1.0f, 0.0f);
		CHECK(fabsf(along - 2.0f / 3.0f) < 1e-6f, "after a pause: %g, not two thirds", along);
		along = camera_long_frame_along(2.0f, 1.0f, 1.0f);
		CHECK(along == 0.5f, "paused: %g, not a half", along);
		along = camera_long_frame_along(2.0f, NAN, 0.5f);
		CHECK(along == 0.5f, "not a number: %g, not a half", along);
		return 0;
	}
	fprintf(stderr, "no such case: %s\n", case_name);
	return 2;
}
