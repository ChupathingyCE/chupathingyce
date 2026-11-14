/*
TEXT_FIELDS_TEST.C

Tests of the high-res text's distance fields: the generator
(port/third_party/msdfgen/msdf_gen.h, as text_hires.c makes a glyph's
field) and the verified mark's outline (port/linux/include/halo_delta_mark.h,
halo_delta_mark_outline). A field's median channel is 128 on the outline,
above it inside and below it outside, its alpha the true distance with the
same sign, a hole is outside, the same shape makes the same bytes twice, and
an outline with more edges than the generator holds makes nothing. The mark's
inner corners sit a stroke inside its edges. Built and run by
tools/test_text_fields.py; exits nonzero on a failure.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../../third_party/msdfgen/msdf_gen.h"
#include "../include/halo_delta_mark.h"

#include <stdio.h>
#include <string.h>

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

/* as text_hires.c makes them: 48 texels to the em of 1000 units, a range of
6 texels, 4 texels of padding around the box */
#define SCALE (48.0 / 1000.0)
#define RANGE 6.0
#define PAD 4

static int vertex(struct msdf_vertex *shape, int index, int type, double x, double y)
{
	memset(&shape[index], 0, sizeof(shape[index]));
	shape[index].type = type;
	shape[index].x = x;
	shape[index].y = y;
	return index + 1;
}

/* a square's outline from (x0, y0) to (x1, y1), clockwise (y up) as a
font's outer contours go, or the other way round for a hole */
static int square(struct msdf_vertex *shape, int index, double x0, double y0, double x1, double y1, int hole)
{
	if (hole)
	{
		index = vertex(shape, index, 1, x0, y1);
		index = vertex(shape, index, 2, x0, y0);
		index = vertex(shape, index, 2, x1, y0);
		index = vertex(shape, index, 2, x1, y1);
	}
	else
	{
		index = vertex(shape, index, 1, x0, y1);
		index = vertex(shape, index, 2, x1, y1);
		index = vertex(shape, index, 2, x1, y0);
		index = vertex(shape, index, 2, x0, y0);
	}
	return index;
}

/* the field of a shape within its box, as text_hires.c lays it out; the
texels and their width and height (free the texels) */
static unsigned char *field(const struct msdf_vertex *shape, int count, double x0, double y0, double x1, double y1,
	int *width, int *height)
{
	double left = x0 - PAD / SCALE, top = y1 + PAD / SCALE;
	unsigned char *rgba;

	*width = (int)((x1 - x0) * SCALE + 0.999999) + 2 * PAD;
	*height = (int)((y1 - y0) * SCALE + 0.999999) + 2 * PAD;
	rgba = malloc((size_t)*width * *height * 4);
	if (!rgba)
		return NULL;
	memset(rgba, 0x55, (size_t)*width * *height * 4);
	if (!msdf_generate(shape, count, SCALE, left, top, RANGE, *width, *height, rgba))
	{
		free(rgba);
		return NULL;
	}
	return rgba;
}

/* the texel of a point in units (y up) */
static const unsigned char *texel_at(const unsigned char *rgba, int width, int height, double x0, double y1,
	double x, double y)
{
	int column = (int)((x - (x0 - PAD / SCALE)) * SCALE), row = (int)(((y1 + PAD / SCALE) - y) * SCALE);

	if (column < 0 || column >= width || row < 0 || row >= height)
		return NULL;
	return rgba + ((size_t)row * width + column) * 4;
}

static int median(const unsigned char *texel)
{
	int a = texel[0], b = texel[1], c = texel[2];
	int low = a < b ? a : b, high = a < b ? b : a;

	return c < low ? low : c > high ? high : c;
}

static void test_square(void)
{
	struct msdf_vertex shape[4];
	unsigned char *rgba, *again;
	int width, height, x, y;
	const unsigned char *texel;

	square(shape, 0, 0.0, 0.0, 500.0, 500.0, 0);
	rgba = field(shape, 4, 0.0, 0.0, 500.0, 500.0, &width, &height);
	CHECK(rgba != NULL);
	if (!rgba)
		return;
	CHECK(width == 24 + 2 * PAD && height == 24 + 2 * PAD);
	/* the middle: inside, as far from the outline as the field reaches */
	texel = texel_at(rgba, width, height, 0.0, 500.0, 250.0, 250.0);
	CHECK(texel && median(texel) == 255 && texel[3] == 255);
	/* a texel just inside an edge, and one just outside it: the median
	rises through 128 at the outline, as does the alpha */
	texel = texel_at(rgba, width, height, 0.0, 500.0, 250.0, 500.0 - 1.5 / SCALE);
	CHECK(texel && median(texel) > 128 && median(texel) < 255 && texel[3] > 128 && texel[3] < 255);
	texel = texel_at(rgba, width, height, 0.0, 500.0, 250.0, 500.0 + 1.5 / SCALE);
	CHECK(texel && median(texel) < 128 && median(texel) > 0 && texel[3] < 128 && texel[3] > 0);
	/* the corners of the field: outside, and the padding keeps them off
	the outline */
	CHECK(median(rgba) < 64 && rgba[3] < 64);
	CHECK(median(rgba + ((size_t)(height - 1) * width + width - 1) * 4) < 64);
	/* every texel: inside where the box says, by the median and the alpha
	alike (none left as the 0x55 it was filled with) */
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			const unsigned char *at = rgba + ((size_t)y * width + x) * 4;
			double ux = (x + 0.5) / SCALE - PAD / SCALE, uy = 500.0 + PAD / SCALE - (y + 0.5) / SCALE;
			int inside = ux > 0.0 + 0.5 / SCALE && ux < 500.0 - 0.5 / SCALE && uy > 0.0 + 0.5 / SCALE &&
				uy < 500.0 - 0.5 / SCALE;
			int outside = ux < 0.0 - 0.5 / SCALE || ux > 500.0 + 0.5 / SCALE || uy < 0.0 - 0.5 / SCALE ||
				uy > 500.0 + 0.5 / SCALE;

			if (inside)
				CHECK(median(at) > 128 && at[3] > 128);
			if (outside)
				CHECK(median(at) < 128 && at[3] < 128);
		}
	}
	/* the same shape makes the same bytes */
	again = field(shape, 4, 0.0, 0.0, 500.0, 500.0, &width, &height);
	CHECK(again && memcmp(rgba, again, (size_t)width * height * 4) == 0);
	free(again);
	free(rgba);
}

static void test_hole(void)
{
	struct msdf_vertex shape[8];
	unsigned char *rgba;
	int width, height, count;
	const unsigned char *texel;

	count = square(shape, 0, 0.0, 0.0, 600.0, 600.0, 0);
	count = square(shape, count, 200.0, 200.0, 400.0, 400.0, 1);
	rgba = field(shape, count, 0.0, 0.0, 600.0, 600.0, &width, &height);
	CHECK(rgba != NULL);
	if (!rgba)
		return;
	/* the ring is inside, the hole outside */
	texel = texel_at(rgba, width, height, 0.0, 600.0, 100.0, 300.0);
	CHECK(texel && median(texel) > 128 && texel[3] > 128);
	texel = texel_at(rgba, width, height, 0.0, 600.0, 300.0, 300.0);
	CHECK(texel && median(texel) < 128 && texel[3] < 128);
	free(rgba);
}

static void test_too_many_edges(void)
{
	static struct msdf_vertex shape[MSDF_MAXIMUM_EDGES + 8];
	unsigned char *rgba;
	int width, height, index, count = 0;

	/* (a circle of more lines than the generator holds) */
	for (index = 0; index < MSDF_MAXIMUM_EDGES + 4; index++)
	{
		double angle = 6.283185307179586 * index / (MSDF_MAXIMUM_EDGES + 4);

		count = vertex(shape, count, index == 0 ? 1 : 2, 500.0 + 400.0 * cos(angle), 500.0 - 400.0 * sin(angle));
	}
	rgba = field(shape, count, 100.0, 100.0, 900.0, 900.0, &width, &height);
	CHECK(rgba == NULL);
	free(rgba);
}

/* a point's distance inside the edge from a to b of a triangle whose third
corner is c (positive inside) */
static double inside(const double a[2], const double b[2], const double c[2], const double p[2])
{
	double nx = a[1] - b[1], ny = b[0] - a[0], length = sqrt(nx * nx + ny * ny);

	nx /= length;
	ny /= length;
	if (nx * (c[0] - a[0]) + ny * (c[1] - a[1]) < 0.0)
	{
		nx = -nx;
		ny = -ny;
	}
	return nx * (p[0] - a[0]) + ny * (p[1] - a[1]);
}

static void test_mark_outline(void)
{
	double corners[6][2];
	const double stroke = 1000.0 / 6.0;
	struct msdf_vertex shape[6];
	unsigned char *rgba;
	int width, height, corner, edge, index;
	const unsigned char *texel;

	halo_delta_mark_outline(1125.0, 1000.0, corners);
	/* the outer triangle: the apex, then the base's right and left */
	CHECK(corners[0][0] == 562.5 && corners[0][1] == 1000.0);
	CHECK(corners[1][0] == 1125.0 && corners[1][1] == 0.0);
	CHECK(corners[2][0] == 0.0 && corners[2][1] == 0.0);
	/* each inner corner a stroke inside the two edges it is between, and
	inside the third; the inner apex under the outer, the inner base a stroke
	up */
	for (corner = 3; corner < 6; corner++)
	{
		double nearest = 1e9, furthest = -1e9;

		for (edge = 0; edge < 3; edge++)
		{
			double distance = inside(corners[edge], corners[(edge + 1) % 3], corners[(edge + 2) % 3], corners[corner]);

			if (distance < nearest)
				nearest = distance;
			if (distance > furthest)
				furthest = distance;
		}
		CHECK(fabs(nearest - stroke) < 1e-6 && furthest > stroke);
	}
	CHECK(fabs(corners[3][0] - 562.5) < 1e-6 && corners[3][1] < 1000.0 - stroke);
	CHECK(fabs(corners[4][1] - stroke) < 1e-6 && fabs(corners[5][1] - stroke) < 1e-6);
	/* (the inner triangle the other way round: its left corner after its
	apex, as the outer's right is after its) */
	CHECK(corners[4][0] < corners[5][0]);
	/* and as a field: ink on the strokes, none in the middle or outside */
	memset(shape, 0, sizeof(shape));
	for (index = 0; index < 6; index++)
	{
		shape[index].type = index % 3 == 0 ? 1 : 2;
		shape[index].x = corners[index][0];
		shape[index].y = corners[index][1];
	}
	rgba = field(shape, 6, 0.0, 0.0, 1125.0, 1000.0, &width, &height);
	CHECK(rgba != NULL);
	if (!rgba)
		return;
	texel = texel_at(rgba, width, height, 0.0, 1000.0, 562.5, stroke * 0.5);
	CHECK(texel && median(texel) > 128);
	texel = texel_at(rgba, width, height, 0.0, 1000.0, 562.5, 400.0);
	CHECK(texel && median(texel) < 128);
	texel = texel_at(rgba, width, height, 0.0, 1000.0, 50.0, 900.0);
	CHECK(texel && median(texel) < 128);
	free(rgba);
}

int main(void)
{
	test_square();
	test_hole();
	test_too_many_edges();
	test_mark_outline();
	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("text fields: ok\n");
	return 0;
}
