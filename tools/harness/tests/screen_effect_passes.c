/*
The scopes' zoom and the cutscenes' blurs (a convolution screen effect,
rasterizer_xbox_screen_effect.c) on a screen of several pixels to the
Xbox's one: rasterizer_screen_effect_extra_passes and
rasterizer_screen_effect_pass_radius. Each pass blends three copies of the
screen, one in place and one a radius either way. The passes have to stay
few (each is a full-screen draw), their copies must sit no farther apart in
the screen's pixels than the Xbox's did in its own (or they show as
ghosts), and the blur must stay about as wide as the Xbox's.
*/

#include "harness.h"

#include <stdlib.h>
#include "under_test.inc"

/* a convolution with no extra passes of its own draws two on the Xbox */
enum { GAME_PASSES = 2 };
/* three copies a pass, at most six passes */
enum { MAXIMUM_COPIES = 729 };

static int by_position(const void *a, const void *b)
{
	real x = *(const real *)a, y = *(const real *)b;

	return x < y ? -1 : x > y;
}

/* where the copies land, in radii (sorted), and the blur's variance */
static long copies(short pass_count, real *positions, real *variance)
{
	long count = 1;
	short pass;

	positions[0] = 0.0f;
	*variance = 0.0f;
	for (pass = 0; pass < pass_count; pass++)
	{
		real radius = rasterizer_screen_effect_pass_radius(1.0f, pass, GAME_PASSES);
		long index;

		CHECK(count * 3 <= MAXIMUM_COPIES, "%d passes are too many", pass_count);
		for (index = 0; index < count; index++)
		{
			positions[count + index] = positions[index] - radius;
			positions[2 * count + index] = positions[index] + radius;
		}
		count *= 3;
		*variance += 2.0f / 3.0f * radius * radius;
	}
	qsort(positions, (size_t)count, sizeof(real), by_position);
	return count;
}

static real widest_gap(const real *positions, long count)
{
	real widest = 0.0f;
	long index;

	for (index = 1; index < count; index++)
	{
		if (positions[index] - positions[index - 1] > widest)
			widest = positions[index] - positions[index - 1];
	}
	return widest;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	static real positions[MAXIMUM_COPIES];
	static const real scales[] = { 1.0f, 1.25f, 2.0f, 3.0f, 4.5f, 6.0f, 9.0f, 20.0f, 81.0f, 500.0f };
	real xbox_variance;
	real xbox_gap = widest_gap(positions, copies(GAME_PASSES, positions, &xbox_variance));
	int index;

	/* 640x480 and smaller draw the game's passes only */
	CASE("xbox-size")
	{
		CHECK(rasterizer_screen_effect_extra_passes(1.0f) == 0, "extra passes at 640x480");
		CHECK(rasterizer_screen_effect_extra_passes(0.5f) == 0, "extra passes below 640x480");
		return 0;
	}
	/* the game's passes keep their radius, each extra one a third of the last */
	CASE("radii")
	{
		CHECK(rasterizer_screen_effect_pass_radius(9.0f, 0, GAME_PASSES) == 9.0f, "first pass changed");
		CHECK(rasterizer_screen_effect_pass_radius(9.0f, 1, GAME_PASSES) == 9.0f, "second pass changed");
		CHECK(rasterizer_screen_effect_pass_radius(9.0f, 2, GAME_PASSES) == 3.0f, "third pass %g",
			rasterizer_screen_effect_pass_radius(9.0f, 2, GAME_PASSES));
		CHECK(rasterizer_screen_effect_pass_radius(9.0f, 3, GAME_PASSES) == 1.0f, "fourth pass %g",
			rasterizer_screen_effect_pass_radius(9.0f, 3, GAME_PASSES));
		return 0;
	}
	/* few passes, an even count, and four in all at 2160 lines */
	CASE("few")
	{
		for (index = 0; index < (int)(sizeof(scales) / sizeof(scales[0])); index++)
		{
			short extra = rasterizer_screen_effect_extra_passes(scales[index]);

			CHECK(extra >= 0 && extra <= 4 && !(extra & 1), "%d extra passes at %gx", extra, scales[index]);
		}
		CHECK(GAME_PASSES + rasterizer_screen_effect_extra_passes(4.5f) == 4, "%d passes at 2160 lines",
			GAME_PASSES + rasterizer_screen_effect_extra_passes(4.5f));
		return 0;
	}
	/* no ghosts: copies no farther apart in screen pixels than the Xbox's, up to 81x */
	CASE("no-ghosts")
	{
		for (index = 0; index < (int)(sizeof(scales) / sizeof(scales[0])) && scales[index] <= 81.0f; index++)
		{
			real variance;
			short pass_count = GAME_PASSES + rasterizer_screen_effect_extra_passes(scales[index]);
			real gap = widest_gap(positions, copies(pass_count, positions, &variance)) * scales[index];

			CHECK(gap <= xbox_gap * 1.0001f, "copies %g apart at %gx (the Xbox's %g)", gap, scales[index], xbox_gap);
		}
		return 0;
	}
	/* the blur stays as wide as the Xbox's, within a tenth */
	CASE("as-wide")
	{
		for (index = 0; index < (int)(sizeof(scales) / sizeof(scales[0])); index++)
		{
			real variance;

			copies(GAME_PASSES + rasterizer_screen_effect_extra_passes(scales[index]), positions, &variance);
			CHECK(variance >= xbox_variance * 0.999f && variance <= xbox_variance * 1.1f,
				"blur variance %g of the Xbox's at %gx", variance / xbox_variance, scales[index]);
		}
		return 0;
	}
	fprintf(stderr, "unknown case '%s'\n", case_name);
	return 2;
}
