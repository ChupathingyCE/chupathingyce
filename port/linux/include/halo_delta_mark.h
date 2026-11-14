/*
HALO_DELTA_MARK.H

The mark of a verified Delta player on the scoreboard (display.verified_mark):
the letter Delta, U+0394, drawn as text in the platform logos' column
(source/game/game_engine.c). The maps' fonts have no Greek, so the font tags
draw it as this outline triangle, the size of the font's capitals
(source/text/font_group.c), and so does the high-res text when its font has
no Delta of its own (port/linux/src/text_hires.c). Plain types only: the
game's side and the host's both include it.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef HALO_DELTA_MARK_H
#define HALO_DELTA_MARK_H

#define HALO_DELTA_MARK 0x0394

/* the mark's width for its height (both in pixels): a little wider than
tall, as the letter is */
static inline int halo_delta_mark_width(
	int height)
{
	return height + (height + 4) / 8;
}

/* the mark's room after it, before the name (in pixels, for its height) */
static inline int halo_delta_mark_gap(
	int height)
{
	return height / 4 > 2 ? height / 4 : 2;
}

/* the mark as an outline, for a distance field of it (text_hires.c): a
triangle standing on its base, width by height units (y up from the base:
the apex at the top), its strokes a sixth of its height. corners gets the
outer triangle's corners clockwise (the apex, the base's right, the base's
left) and then the inner triangle's the other way round (its apex, its
left, its right): where the edges before and after each corner, moved a
stroke inward, meet. Nothing with a height of 0 or less. */
static inline void halo_delta_mark_outline(
	double width,
	double height,
	double corners[6][2])
{
	double const stroke = height / 6.0;
	double outer[3][2];
	double normals[3][3];
	int edge, corner;

	if (height <= 0.0 || width <= 0.0)
		return;
	outer[0][0] = 0.5 * width;
	outer[0][1] = height;
	outer[1][0] = width;
	outer[1][1] = 0.0;
	outer[2][0] = 0.0;
	outer[2][1] = 0.0;
	/* (each edge's inward normal, a unit long, and its offset: a point's
	distance inside the edge is n.p + c) */
	for (edge = 0; edge < 3; edge++)
	{
		double const *a = outer[edge], *b = outer[(edge + 1) % 3], *c = outer[(edge + 2) % 3];
		double nx = a[1] - b[1], ny = b[0] - a[0];
		double length = nx * nx + ny * ny, guess;
		int step;

		/* (a square root without math.h: Newton's, from a guess) */
		guess = length > 1.0 ? length * 0.5 : 1.0;
		for (step = 0; step < 40; step++)
			guess = 0.5 * (guess + length / guess);
		nx /= guess;
		ny /= guess;
		if (nx * c[0] + ny * c[1] - (nx * a[0] + ny * a[1]) < 0.0)
		{
			nx = -nx;
			ny = -ny;
		}
		normals[edge][0] = nx;
		normals[edge][1] = ny;
		normals[edge][2] = -(nx * a[0] + ny * a[1]);
	}
	for (corner = 0; corner < 3; corner++)
	{
		double const *before = normals[(corner + 2) % 3], *after = normals[corner];
		double determinant = before[0] * after[1] - before[1] * after[0];
		double rb = stroke - before[2], ra = stroke - after[2];

		corners[corner][0] = outer[corner][0];
		corners[corner][1] = outer[corner][1];
		/* (the inner corners in the order 0, 2, 1: the other way round) */
		corners[3 + (3 - corner) % 3][0] = (rb * after[1] - before[1] * ra) / determinant;
		corners[3 + (3 - corner) % 3][1] = (before[0] * ra - rb * after[0]) / determinant;
	}
}

/* the mark drawn as coverage (0 to 255, a byte a pixel) into width by
height pixels, rows stride bytes apart: the outline of a triangle standing
on its base, its strokes about a sixth of its height (one pixel at least),
each pixel's coverage from 4 by 4 samples */
static inline void halo_delta_mark_draw(
	unsigned char *pixels,
	int width,
	int height,
	int stride)
{
	/* (the corners: the apex, then the base's right and left) */
	float const apex_x = 0.5f * (float)width, base_y = (float)height;
	float const corners[3][2] = { { apex_x, 0.0f }, { (float)width, base_y }, { 0.0f, base_y } };
	float stroke = (float)height / 6.0f;
	float normals[3][3];
	int edge, x, y;

	if (!pixels || width <= 0 || height <= 0)
		return;
	if (stroke < 1.0f)
		stroke = 1.0f;
	/* (each edge's inward normal, a unit long, and its offset: a point's
	distance inside the edge is n.p + c) */
	for (edge = 0; edge < 3; edge++)
	{
		float const *a = corners[edge], *b = corners[(edge + 1) % 3];
		float nx = a[1] - b[1], ny = b[0] - a[0];
		float length = nx * nx + ny * ny, inverse = 1.0f, guess;
		int step;

		/* (a square root without math.h: Newton's, from a guess) */
		guess = length > 1.0f ? length * 0.5f : 1.0f;
		for (step = 0; step < 24; step++)
			guess = 0.5f * (guess + length / guess);
		if (guess > 0.0f)
			inverse = 1.0f / guess;
		nx *= inverse;
		ny *= inverse;
		/* (pointing inward: towards the third corner) */
		if (nx * corners[(edge + 2) % 3][0] + ny * corners[(edge + 2) % 3][1] - (nx * a[0] + ny * a[1]) < 0.0f)
		{
			nx = -nx;
			ny = -ny;
		}
		normals[edge][0] = nx;
		normals[edge][1] = ny;
		normals[edge][2] = -(nx * a[0] + ny * a[1]);
	}
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			int samples = 0, sx, sy;

			for (sy = 0; sy < 4; sy++)
			{
				for (sx = 0; sx < 4; sx++)
				{
					float px = (float)x + (sx + 0.5f) * 0.25f, py = (float)y + (sy + 0.5f) * 0.25f;
					float nearest = 1.0e9f;

					for (edge = 0; edge < 3; edge++)
					{
						float distance = normals[edge][0] * px + normals[edge][1] * py + normals[edge][2];

						if (distance < nearest)
							nearest = distance;
					}
					/* (inside the triangle, and within a stroke of an edge) */
					if (nearest >= 0.0f && nearest < stroke)
						samples++;
				}
			}
			pixels[y * stride + x] = (unsigned char)(samples * 255 / 16);
		}
	}
}

#endif
