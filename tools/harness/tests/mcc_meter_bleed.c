/*
MCC_METER_BLEED.C (test)

The real mcc_meter_bleed (mcc_bitmaps.c) over small meter sheets: a texel
beside a meter's shape takes that shape's fill order, so a filtered meter
can't light the edge of an empty segment; a shape's own texels, the alpha
and a clone's coverage (green) are left as they are, and texels far from
any shape keep their fill order. test_mcc_meter_bleed.py takes the code
(under_test.inc).
*/

#include "harness.h"
#include <stdint.h>

#include "under_test.inc"

#define W 8
#define H 6

static uint32_t texel(unsigned alpha, unsigned red, unsigned green, unsigned blue)
{
	return blue | (green << 8) | (red << 16) | ((uint32_t)alpha << 24);
}

/* one 2x2 shape at columns 3-4, rows 2-3: fill order 120 (in red, green
and blue, as an A8Y8 sheet decodes), the rest uncovered with fill order 0 */
static void sheet(uint32_t *texels, int clone)
{
	unsigned x, y;
	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++)
			texels[y * W + x] = 0;
	for (y = 2; y < 4; y++)
		for (x = 3; x < 5; x++)
			texels[y * W + x] = clone ? texel(200, 120, 90, 120) : texel(200, 120, 120, 120);
}

int main(int argc, char **argv)
{
	uint32_t texels[W * H];
	unsigned char state[W * H];
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("edges-take-the-fill")
	{
		sheet(texels, 0);
		mcc_meter_bleed(texels, W, H, 1, state);
		/* (beside the shape, and two texels away) */
		CHECK(texels[2 * W + 2] == texel(0, 120, 120, 120), "left neighbour %08x", (unsigned)texels[2 * W + 2]);
		CHECK(texels[1 * W + 3] == texel(0, 120, 120, 120), "upper neighbour %08x", (unsigned)texels[1 * W + 3]);
		CHECK(texels[4 * W + 4] == texel(0, 120, 120, 120), "lower neighbour %08x", (unsigned)texels[4 * W + 4]);
		CHECK(texels[2 * W + 1] == texel(0, 120, 120, 120), "two away %08x", (unsigned)texels[2 * W + 1]);
		CHECK(texels[0 * W + 3] == texel(0, 120, 120, 120), "two above %08x", (unsigned)texels[0 * W + 3]);
		return 0;
	}
	CASE("shape-and-far-texels-unchanged")
	{
		sheet(texels, 0);
		mcc_meter_bleed(texels, W, H, 1, state);
		CHECK(texels[2 * W + 3] == texel(200, 120, 120, 120), "a shape texel changed: %08x", (unsigned)texels[2 * W + 3]);
		/* (three texels from the shape: past the passes) */
		CHECK(texels[2 * W + 0] == 0, "a far texel changed: %08x", (unsigned)texels[2 * W + 0]);
		CHECK(texels[2 * W + 7] == 0, "a far texel changed: %08x", (unsigned)texels[2 * W + 7]);
		return 0;
	}
	CASE("clone-keeps-coverage")
	{
		sheet(texels, 1);
		mcc_meter_bleed(texels, W, H, 4, state);
		/* (a Gearbox meter's clone: green is its coverage, kept at 0 outside) */
		CHECK(texels[2 * W + 2] == texel(0, 120, 0, 120), "clone neighbour %08x", (unsigned)texels[2 * W + 2]);
		CHECK(texels[2 * W + 4] == texel(200, 120, 90, 120), "clone shape changed %08x", (unsigned)texels[2 * W + 4]);
		return 0;
	}
	CASE("no-state-no-change")
	{
		sheet(texels, 0);
		mcc_meter_bleed(texels, W, H, 1, NULL);
		CHECK(texels[2 * W + 2] == 0, "changed without its buffer");
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
