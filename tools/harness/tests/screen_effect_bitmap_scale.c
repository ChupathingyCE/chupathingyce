/*
A screen effect bitmap's scale to its texture's coordinates
(rasterizer_screen_effect_bitmap_scale, rasterizer_xbox_screen_effect.c).
The Xbox addresses a linear texture in texels; a Custom Edition map's linear
bitmap, a scope's zoom mask among them, is sampled from 0 to 1 as Halo PC
samples every texture, so it is scaled as a swizzled one is. The screen's
own copy stays in texels, and a Custom Edition zoom mask at its bitmap
group's high resolution scale is smaller on the screen than its size.
*/

#include "harness.h"

#include <math.h>
#include "under_test.inc"

static boolean close_to(real a, real b)
{
	return fabsf(a - b) <= 1.0e-6f * (fabsf(b) > 1.0f ? fabsf(b) : 1.0f);
}

/* the scale of a bitmap width x height, as the screen effect works it out */
static void scale_of(boolean linear, boolean screen_copy, boolean custom_edition_map, real mask_scale,
	real width, real height, real *size_i, real *size_j, real *scale_i, real *scale_j)
{
	*size_i = width;
	*size_j = height;
	rasterizer_screen_effect_bitmap_scale(linear, screen_copy, custom_edition_map, mask_scale,
		size_i, size_j, scale_i, scale_j);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	real size_i, size_j, scale_i, scale_j;

	/* a Custom Edition map's linear zoom mask, 16 texels of 4 bytes a row (a
	64-byte pitch, as the Xbox's own lays a linear row out): sampled from 0 to 1 */
	CASE("ce-linear-mask")
	{
		scale_of(TRUE, FALSE, TRUE, 1.0f, 16.0f, 8.0f, &size_i, &size_j, &scale_i, &scale_j);
		CHECK(size_i == 16.0f && size_j == 8.0f, "size %gx%g", size_i, size_j);
		CHECK(close_to(scale_i, 1.0f / 16.0f) && close_to(scale_j, 1.0f / 8.0f), "scale %g, %g", scale_i, scale_j);
		return 0;
	}
	/* the same mask at its group's half scale: half its size on the screen,
	still sampled from 0 to 1 */
	CASE("ce-linear-mask-scaled")
	{
		scale_of(TRUE, FALSE, TRUE, 0.5f, 16.0f, 8.0f, &size_i, &size_j, &scale_i, &scale_j);
		CHECK(size_i == 8.0f && size_j == 4.0f, "size %gx%g", size_i, size_j);
		CHECK(close_to(scale_i, 1.0f / 8.0f) && close_to(scale_j, 1.0f / 4.0f), "scale %g, %g", scale_i, scale_j);
		return 0;
	}
	/* a swizzled one, on any map: from 0 to 1 */
	CASE("swizzled")
	{
		scale_of(FALSE, FALSE, TRUE, 0.5f, 64.0f, 32.0f, &size_i, &size_j, &scale_i, &scale_j);
		CHECK(close_to(scale_i, 1.0f / 32.0f) && close_to(scale_j, 1.0f / 16.0f), "CE scale %g, %g", scale_i, scale_j);
		scale_of(FALSE, FALSE, FALSE, 1.0f, 64.0f, 32.0f, &size_i, &size_j, &scale_i, &scale_j);
		CHECK(close_to(scale_i, 1.0f / 64.0f) && close_to(scale_j, 1.0f / 32.0f), "Xbox scale %g, %g", scale_i, scale_j);
		return 0;
	}
	/* the screen's own copy, on a Custom Edition map too: in texels */
	CASE("screen-copy")
	{
		scale_of(TRUE, TRUE, TRUE, 1.0f, 1280.0f, 720.0f, &size_i, &size_j, &scale_i, &scale_j);
		CHECK(scale_i == 1.0f && scale_j == 1.0f, "CE scale %g, %g", scale_i, scale_j);
		scale_of(TRUE, TRUE, FALSE, 1.0f, 1280.0f, 720.0f, &size_i, &size_j, &scale_i, &scale_j);
		CHECK(scale_i == 1.0f && scale_j == 1.0f, "Xbox scale %g, %g", scale_i, scale_j);
		return 0;
	}
	/* an Xbox map's linear bitmap: in texels, as the Xbox addresses it */
	CASE("xbox-linear")
	{
		scale_of(TRUE, FALSE, FALSE, 1.0f, 16.0f, 8.0f, &size_i, &size_j, &scale_i, &scale_j);
		CHECK(scale_i == 1.0f && scale_j == 1.0f, "scale %g, %g", scale_i, scale_j);
		scale_of(TRUE, FALSE, FALSE, 0.5f, 16.0f, 8.0f, &size_i, &size_j, &scale_i, &scale_j);
		CHECK(scale_i == 2.0f && scale_j == 2.0f && size_i == 8.0f, "scaled: scale %g, %g", scale_i, scale_j);
		return 0;
	}
	fprintf(stderr, "unknown case '%s'\n", case_name);
	return 2;
}
