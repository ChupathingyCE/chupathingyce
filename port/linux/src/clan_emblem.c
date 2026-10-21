/*
CLAN_EMBLEM.C

A clan's emblem drawn into pixels (clan_emblem.h): each part of its pieces
filled even-odd, scanline by scanline, four rows and columns of samples a
pixel for smooth edges. The later parts cover the earlier: the background
shape, then the foreground's.
*/

#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_DELTA_PROFILE)

#include "clan_emblem.h"

#include <stdlib.h>
#include <string.h>

struct clan_emblem_polygon
{
	unsigned short first;
	unsigned short count;
};

struct clan_emblem_part
{
	unsigned char layer;
	unsigned char first_polygon;
	unsigned char polygon_count;
};

struct clan_emblem_piece
{
	const char *name;
	unsigned char first_part;
	unsigned char part_count;
};

#include "clan_emblem_table.h"

enum
{
	SAMPLES = 4,
	MAXIMUM_CROSSINGS = 128,
	FIELD = 100,
};

static int compare_floats(const void *a, const void *b)
{
	float x = *(const float *)a, y = *(const float *)b;

	return x < y ? -1 : x > y;
}

static int color_index(int index)
{
	return index >= 0 && index < CLAN_EMBLEM_COLORS ? index : 0;
}

/* a piece's parts over the row of samples at field height y: each sample
covered takes the part's color (its index, plus 1) */
static void fill_piece(const struct clan_emblem_piece *piece, const struct clan_emblem *emblem, float y, int width,
	unsigned char *row)
{
	int part_index;

	for (part_index = 0; part_index < piece->part_count; part_index++)
	{
		const struct clan_emblem_part *part = &clan_emblem_parts[piece->first_part + part_index];
		float crossings[MAXIMUM_CROSSINGS];
		int count = 0, polygon_index, at;
		int color = color_index(part->layer == 1 ? emblem->primary : part->layer == 2 ? emblem->secondary :
			emblem->background_color);

		for (polygon_index = 0; polygon_index < part->polygon_count; polygon_index++)
		{
			const struct clan_emblem_polygon *polygon = &clan_emblem_polygons[part->first_polygon + polygon_index];
			int point;

			for (point = 0; point < polygon->count; point++)
			{
				const float *a = clan_emblem_points[polygon->first + point];
				const float *b = clan_emblem_points[polygon->first + (point + 1) % polygon->count];

				if (((a[1] <= y && b[1] > y) || (b[1] <= y && a[1] > y)) && count < MAXIMUM_CROSSINGS)
					crossings[count++] = a[0] + (y - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
			}
		}
		qsort(crossings, (size_t)count, sizeof(crossings[0]), compare_floats);
		for (at = 0; at + 1 < count; at += 2)
		{
			/* (the samples whose middles are in the span) */
			int x0 = (int)(crossings[at] * width / FIELD + 0.5f);
			int x1 = (int)(crossings[at + 1] * width / FIELD + 0.5f);
			int x;

			if (x0 < 0)
				x0 = 0;
			if (x1 > width)
				x1 = width;
			for (x = x0; x < x1; x++)
				row[x] = (unsigned char)(color + 1);
		}
	}
}

void clan_emblem_render(const struct clan_emblem *emblem, int size, unsigned char *pixels)
{
	unsigned int (*sums)[4];
	unsigned char row[CLAN_EMBLEM_MAXIMUM_SIZE * SAMPLES];
	const struct clan_emblem_piece *background = NULL, *foreground = NULL;
	int width, sample_row, index;

	if (size <= 0)
		return;
	if (size > CLAN_EMBLEM_MAXIMUM_SIZE)
		size = CLAN_EMBLEM_MAXIMUM_SIZE;
	width = size * SAMPLES;
	if (emblem->background >= 0 && emblem->background < CLAN_EMBLEM_BACKGROUNDS)
		background = &clan_emblem_backgrounds[emblem->background];
	if (emblem->foreground >= 0 && emblem->foreground < CLAN_EMBLEM_FOREGROUNDS)
		foreground = &clan_emblem_foregrounds[emblem->foreground];
	/* (in memory only while it is drawn) */
	sums = calloc((size_t)size * (size_t)size, sizeof(sums[0]));
	if (!sums)
	{
		memset(pixels, 0, (size_t)size * (size_t)size * 4);
		return;
	}
	for (sample_row = 0; sample_row < width; sample_row++)
	{
		float y = (sample_row + 0.5f) * FIELD / width;
		int x;

		memset(row, 0, (size_t)width);
		if (background)
			fill_piece(background, emblem, y, width, row);
		if (foreground)
			fill_piece(foreground, emblem, y, width, row);
		for (x = 0; x < width; x++)
		{
			unsigned int *sum;
			unsigned int color;

			if (!row[x])
				continue;
			color = clan_emblem_colors[row[x] - 1];
			sum = sums[(sample_row / SAMPLES) * size + x / SAMPLES];
			sum[0] += (color >> 16) & 0xFF;
			sum[1] += (color >> 8) & 0xFF;
			sum[2] += color & 0xFF;
			sum[3]++;
		}
	}
	for (index = 0; index < size * size; index++)
	{
		unsigned int *sum = sums[index];
		unsigned char *pixel = pixels + index * 4;

		if (!sum[3])
		{
			memset(pixel, 0, 4);
			continue;
		}
		pixel[0] = (unsigned char)(sum[0] / sum[3]);
		pixel[1] = (unsigned char)(sum[1] / sum[3]);
		pixel[2] = (unsigned char)(sum[2] / sum[3]);
		pixel[3] = (unsigned char)(sum[3] * 255 / (SAMPLES * SAMPLES));
	}
	free(sums);
}

int clan_emblem_serial(const struct clan_emblem *emblem)
{
	/* (each field under 32: five bits each, and a mark so it is never 0) */
	return 0x40000000 | ((emblem->foreground & 31) << 20) | ((emblem->background & 31) << 15) |
		((emblem->primary & 31) << 10) | ((emblem->secondary & 31) << 5) | (emblem->background_color & 31);
}

#endif
