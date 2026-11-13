/*
TITLE_TOOL.C

The title tool (title_tool.h): the menus' titles rebuilt from the player's
own ui.map.

1. The map is inflated and its title bitmaps found by their tags; each one
is decoded (DXT3 or DXT5) and read as tools/title_assets.py reads it: which texels
are text (text_mask), how much of each the text covers (text_coverage),
the text's color and opacity, and its baseline.
2. Every letter of every title is an instance of its character, placed where
titles.json says it starts and on its title's baseline. Its texels near it
are samples of the character's shape at that sub-texel position; all the
instances' samples, put together on a grid GRID times finer than a texel
and smoothed, make the character's shape (its field: how much of a texel
centered there the letter covers). Each instance is then moved, by eighths
of a texel, to where it best matches the shape (registration), and the
shapes made again. The samples a neighboring letter covers more than the
letter itself are left out (the L's foot under a following A), and, for a
character seen three times or more, the samples that differ from the shape
by much (an odd neighbor's ink), and the shapes made a last time.
3. A title is drawn at its scale: each letter's shape where its instance is,
sharpened to an edge about one pixel wide (the field's 0.5 contour, by its
gradient), over the title's plate with its old text filled in from around
it and enlarged; the text in its own color and opacity.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "title_tool.h"

#include "zlib_prefixed.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the Xbox maps' tag addresses (tools/hud_assets.py's XBOX_TAG_BASE) */
#define TAG_BASE 0x803A6000UL
/* the largest map read (ui.map inflates to about 32 MB) */
#define MAXIMUM_MAP_SIZE (192UL << 20)
#define FORMAT_DXT3 15
#define FORMAT_DXT5 16

/* a character's shape: GRID samples a texel, over a box of WIDTH by HEIGHT
texels whose corner is (LEFT, TOP) from where the letter starts on its
baseline */
#define GRID 4
#define WIDTH 40
#define HEIGHT 30
#define LEFT (-4)
#define TOP (-26)
#define FIELD_WIDTH (WIDTH * GRID)
#define FIELD_HEIGHT (HEIGHT * GRID)
/* how much the shapes are smoothed (texels) */
#define SMOOTHING 0.85f
/* how far an instance is moved to match its shape: eighths of a texel */
#define MOVE_X 8
#define MOVE_Y 3
/* a sample a neighbor covers more than this (and more than its own
letter) is the neighbor's; one that differs from the shape by more than
OUTLIER is left out */
#define NEIGHBOR 0.3f
#define OUTLIER 0.45f
/* the plate's blur before it is enlarged (tools/title_assets.py SMOOTHING) */
#define PLATE_SMOOTHING 1.0f

struct instance
{
	long title;
	int character;
	/* where the letter starts and its baseline, and its move */
	float x, y, dx, dy;
	/* its samples: where (texel centers), what they cover, whether used */
	long count;
	float *px, *py, *value;
	unsigned char *keep;
};

struct shape
{
	float *field;
};

struct picture
{
	int present;
	int width, height, scale;
	/* RGBA, as decoded */
	unsigned char *texels;
	/* the old text's texels and coverage */
	unsigned char *mask;
	float *coverage;
	float color[3], opacity, baseline;
	/* its letters: instances first .. first + letters */
	long first, letters;
};

struct title_tool
{
	long count;
	struct picture *pictures;
	struct instance *instances;
	long instance_count;
	struct shape shapes[256];
};

/* ---------- reading the map */

static unsigned long u32(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

static unsigned int u16(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8);
}

/* the map's file, inflated (its 2 KB header stays as it is); NULL if it is
not an Xbox map */
static unsigned char *map_read(const char *path, unsigned long *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *raw = NULL, *data = NULL;
	long length;
	unsigned long total;
	z_stream stream;

	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0x800 || (unsigned long)length > MAXIMUM_MAP_SIZE ||
		fseek(file, 0, SEEK_SET))
	{
		fclose(file);
		return NULL;
	}
	raw = malloc((size_t)length);
	if (!raw || fread(raw, 1, (size_t)length, file) != (size_t)length)
	{
		fclose(file);
		free(raw);
		return NULL;
	}
	fclose(file);
	total = u32(raw + 8);
	if (memcmp(raw, "daeh", 4) || u32(raw + 4) != 5 || total <= 0x800 || total > MAXIMUM_MAP_SIZE)
	{
		free(raw);
		return NULL;
	}
	data = malloc(total);
	if (!data)
	{
		free(raw);
		return NULL;
	}
	memcpy(data, raw, 0x800);
	memset(&stream, 0, sizeof(stream));
	stream.next_in = raw + 0x800;
	stream.avail_in = (uInt)(length - 0x800);
	stream.next_out = data + 0x800;
	stream.avail_out = (uInt)(total - 0x800);
	if (inflateInit(&stream) != Z_OK)
	{
		free(raw);
		free(data);
		return NULL;
	}
	inflate(&stream, Z_FINISH);
	inflateEnd(&stream);
	free(raw);
	if (stream.total_out != total - 0x800)
	{
		free(data);
		return NULL;
	}
	*size = total;
	return data;
}

/* the tag data at a tag address, with size bytes there; NULL if outside */
static const unsigned char *tag_read(const unsigned char *tags, unsigned long tags_size, unsigned long address,
	unsigned long size)
{
	if (address < TAG_BASE || address - TAG_BASE > tags_size || size > tags_size - (address - TAG_BASE))
		return NULL;
	return tags + (address - TAG_BASE);
}

/* the first mip level of bitmap index of the bitmap group tag name (DXT3
or DXT5), its size and format; NULL if the map has none */
static const unsigned char *bitmap_find(const unsigned char *data, unsigned long size, const char *name, int index,
	int *width, int *height, int *format)
{
	unsigned long tags_offset = u32(data + 0x10), tags_size = u32(data + 0x14), instances, count, tag;
	const unsigned char *tags, *entry;

	if (tags_offset > size || tags_size > size - tags_offset || tags_size < 0x10)
		return NULL;
	tags = data + tags_offset;
	instances = u32(tags);
	count = u32(tags + 0xC);
	for (tag = 0; tag < count; tag++)
	{
		const unsigned char *tag_name, *header, *bitmap;
		unsigned long name_length = strlen(name) + 1, offset, pixels_size;

		entry = tag_read(tags, tags_size, instances + tag * 0x20, 0x20);
		if (!entry)
			return NULL;
		if (memcmp(entry, "mtib", 4))
			continue;
		tag_name = tag_read(tags, tags_size, u32(entry + 0x10), name_length);
		if (!tag_name || memcmp(tag_name, name, name_length))
			continue;
		header = tag_read(tags, tags_size, u32(entry + 0x14), 0x6C);
		if (!header || index < 0 || (unsigned long)index >= u32(header + 0x60))
			return NULL;
		bitmap = tag_read(tags, tags_size, u32(header + 0x64) + (unsigned long)index * 0x30, 0x30);
		if (!bitmap)
			return NULL;
		*width = (int)u16(bitmap + 4);
		*height = (int)u16(bitmap + 6);
		offset = u32(bitmap + 0x18);
		pixels_size = u32(bitmap + 0x1C);
		*format = (int)u16(bitmap + 0xC);
		if ((*format != FORMAT_DXT3 && *format != FORMAT_DXT5) || *width < 4 || *height < 4 || *width > 2048 || *height > 2048 ||
			(*width & 3) || (*height & 3) || offset > size ||
			pixels_size < (unsigned long)*width * (unsigned long)*height || pixels_size > size - offset)
			return NULL;
		return data + offset;
	}
	return NULL;
}

/* DXT3 or DXT5 blocks (row by row) as RGBA, as tools/hud_assets.py decodes
them */
static unsigned char *dxt_decode(const unsigned char *blocks, int width, int height, int format)
{
	unsigned char *texels = malloc((size_t)width * (size_t)height * 4);
	int bx, by, i;

	if (!texels)
		return NULL;
	for (by = 0; by < height / 4; by++)
	{
		for (bx = 0; bx < width / 4; bx++)
		{
			const unsigned char *block = blocks + ((size_t)by * (size_t)(width / 4) + (size_t)bx) * 16;
			unsigned int c0 = u16(block + 8), c1 = u16(block + 10);
			unsigned long bits = u32(block + 12);
			int palette[4][3], alphas[8];
			unsigned long long codes = 0;

			palette[0][0] = (int)((c0 >> 11) & 31) * 255 / 31;
			palette[0][1] = (int)((c0 >> 5) & 63) * 255 / 63;
			palette[0][2] = (int)(c0 & 31) * 255 / 31;
			palette[1][0] = (int)((c1 >> 11) & 31) * 255 / 31;
			palette[1][1] = (int)((c1 >> 5) & 63) * 255 / 63;
			palette[1][2] = (int)(c1 & 31) * 255 / 31;
			for (i = 0; i < 3; i++)
			{
				palette[2][i] = (2 * palette[0][i] + palette[1][i]) / 3;
				palette[3][i] = (palette[0][i] + 2 * palette[1][i]) / 3;
			}
			if (format == FORMAT_DXT5)
			{
				int a0 = block[0], a1 = block[1];

				alphas[0] = a0;
				alphas[1] = a1;
				for (i = 1; i < 7; i++)
				{
					if (a0 > a1)
						alphas[i + 1] = ((7 - i) * a0 + i * a1) / 7;
					else if (i < 5)
						alphas[i + 1] = ((5 - i) * a0 + i * a1) / 5;
				}
				if (a0 <= a1)
				{
					alphas[6] = 0;
					alphas[7] = 255;
				}
				for (i = 0; i < 6; i++)
					codes |= (unsigned long long)block[2 + i] << (8 * i);
			}
			for (i = 0; i < 16; i++)
			{
				unsigned char *texel = texels + (((size_t)(by * 4 + i / 4) * (size_t)width) + (size_t)(bx * 4 + i % 4)) * 4;
				int code = (int)((bits >> (2 * i)) & 3);

				texel[0] = (unsigned char)palette[code][0];
				texel[1] = (unsigned char)palette[code][1];
				texel[2] = (unsigned char)palette[code][2];
				texel[3] = (unsigned char)(format == FORMAT_DXT5 ? alphas[(codes >> (3 * i)) & 7] :
					((block[i / 2] >> (4 * (i & 1))) & 15) * 17);
			}
		}
	}
	return texels;
}

/* ---------- reading a picture (tools/title_assets.py's text_mask and
text_coverage) */

static int compare_floats(const void *a, const void *b)
{
	float x = *(const float *)a, y = *(const float *)b;

	return x < y ? -1 : x > y;
}

/* the median of count values (which it sorts), as NumPy's */
static float median(float *values, long count)
{
	if (count <= 0)
		return 0;
	qsort(values, (size_t)count, sizeof(*values), compare_floats);
	return count & 1 ? values[count / 2] : (values[count / 2 - 1] + values[count / 2]) / 2;
}

/* mask eroded or dilated by one texel, by its four neighbors (SciPy's
default; outside counts as not set) */
static void morphology(const unsigned char *mask, unsigned char *out, int width, int height, int grow)
{
	int x, y;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			int here = mask[y * width + x];
			int left = x > 0 && mask[y * width + x - 1], right = x + 1 < width && mask[y * width + x + 1];
			int up = y > 0 && mask[(y - 1) * width + x], down = y + 1 < height && mask[(y + 1) * width + x];

			out[y * width + x] = (unsigned char)(grow ? (here || left || right || up || down) :
				(here && left && right && up && down));
		}
	}
}

/* the text's texels, its coverage, color and opacity, and its baseline */
static int picture_read(struct picture *picture)
{
	int width = picture->width, height = picture->height, x, y, c;
	long texels = (long)width * height, i, white = 0, dark = 0, solid_count = 0, count;
	const unsigned char *rgba = picture->texels;
	unsigned char *eroded = malloc((size_t)texels), *solid;
	float *values = malloc((size_t)texels * sizeof(float)), maximum_alpha = 0;
	int style;

	picture->mask = malloc((size_t)texels);
	picture->coverage = calloc((size_t)texels, sizeof(float));
	if (!eroded || !values || !picture->mask || !picture->coverage)
	{
		free(eroded);
		free(values);
		return 0;
	}
	for (i = 0; i < texels; i++)
	{
		const unsigned char *t = rgba + i * 4;
		int low = t[0] < t[1] ? t[0] : t[1], high = t[0] > t[1] ? t[0] : t[1];

		low = low < t[2] ? low : t[2];
		high = high > t[2] ? high : t[2];
		white += low > 200 && t[3] > 128;
		dark += t[3] > 0 && high < 64;
		if (t[3] > maximum_alpha)
			maximum_alpha = t[3];
	}
	/* (white text in a glow, a header's bright text on a dark plate, or text
	alone, flat and translucent) */
	style = white > 40 ? 0 : dark > 40 ? 1 : 2;
	for (i = 0; i < texels; i++)
	{
		const unsigned char *t = rgba + i * 4;
		int low = t[0] < t[1] ? t[0] : t[1], high = t[0] > t[1] ? t[0] : t[1];

		low = low < t[2] ? low : t[2];
		high = high > t[2] ? high : t[2];
		picture->mask[i] = (unsigned char)(style == 0 ? low > 200 && t[3] > 128 :
			style == 1 ? t[3] > 0 && high >= 96 : t[3] >= 0.5f * maximum_alpha);
	}
	morphology(picture->mask, eroded, width, height, 0);
	for (i = 0; i < texels; i++)
		solid_count += eroded[i];
	solid = solid_count > 20 ? eroded : picture->mask;
	for (c = 0; c < 4; c++)
	{
		count = 0;
		for (i = 0; i < texels; i++)
		{
			if (solid[i])
				values[count++] = rgba[i * 4 + c];
		}
		if (c < 3)
			picture->color[c] = median(values, count);
		else
			picture->opacity = median(values, count) / 255;
	}
	if (style == 1)
	{
		/* (by green, between the plate's and the text's) */
		float text, plate;

		count = 0;
		for (i = 0; i < texels; i++)
		{
			if (solid[i])
				values[count++] = rgba[i * 4 + 1];
		}
		text = median(values, count);
		count = 0;
		for (i = 0; i < texels; i++)
		{
			const unsigned char *t = rgba + i * 4;

			if (t[3] > 0 && t[0] < 64 && t[1] < 64 && t[2] < 64)
				values[count++] = t[1];
		}
		plate = median(values, count);
		for (i = 0; i < texels; i++)
		{
			float v = text != plate ? (rgba[i * 4 + 1] - plate) / (text - plate) : 0;

			picture->coverage[i] = rgba[i * 4 + 3] ? (v < 0 ? 0 : v > 1 ? 1 : v) : 0;
		}
	}
	else
	{
		/* (white text and flat text: by alpha, the solid texels' being whole;
		the tool makes the headers, style 1, so these are read simply) */
		float full;

		count = 0;
		for (i = 0; i < texels; i++)
		{
			if (solid[i])
				values[count++] = rgba[i * 4 + 3];
		}
		full = median(values, count);
		for (i = 0; i < texels; i++)
		{
			float v = full > 0 ? rgba[i * 4 + 3] / full : 0;

			picture->coverage[i] = picture->mask[i] ? (v > 1 ? 1 : v) : 0;
		}
	}
	free(eroded);
	free(values);
	/* the baseline: under the last row with solid text */
	picture->baseline = -1;
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			if (picture->coverage[y * width + x] >= 0.5f)
			{
				picture->baseline = (float)y + 1;
				break;
			}
		}
	}
	return picture->baseline > 0;
}

/* ---------- the letters' shapes */

/* a 1D Gaussian kernel of sigma (truncated at four sigma, as SciPy's) */
static float *gaussian_kernel(float sigma, int *radius)
{
	int r = (int)(4 * sigma + 0.5f), i;
	float *kernel = malloc((size_t)(2 * r + 1) * sizeof(float)), sum = 0;

	if (!kernel)
		return NULL;
	for (i = -r; i <= r; i++)
		sum += kernel[i + r] = expf(-0.5f * (float)(i * i) / (sigma * sigma));
	for (i = 0; i <= 2 * r; i++)
		kernel[i] /= sum;
	*radius = r;
	return kernel;
}

/* image (width by height, channels interleaved) blurred by the kernel, edges
taken as their nearest value */
static void blur(float *image, int width, int height, int channels, const float *kernel, int radius)
{
	float *line = malloc((size_t)(width > height ? width : height) * (size_t)channels * sizeof(float));
	int x, y, c, k;

	if (!line)
		return;
	for (y = 0; y < height; y++)
	{
		float *row = image + (size_t)y * (size_t)width * (size_t)channels;

		for (x = 0; x < width; x++)
		{
			for (c = 0; c < channels; c++)
			{
				float sum = 0;

				for (k = -radius; k <= radius; k++)
				{
					int at = x + k < 0 ? 0 : x + k >= width ? width - 1 : x + k;

					sum += kernel[k + radius] * row[at * channels + c];
				}
				line[x * channels + c] = sum;
			}
		}
		memcpy(row, line, (size_t)width * (size_t)channels * sizeof(float));
	}
	for (x = 0; x < width; x++)
	{
		for (y = 0; y < height; y++)
		{
			for (c = 0; c < channels; c++)
			{
				float sum = 0;

				for (k = -radius; k <= radius; k++)
				{
					int at = y + k < 0 ? 0 : y + k >= height ? height - 1 : y + k;

					sum += kernel[k + radius] * image[((size_t)at * (size_t)width + (size_t)x) * (size_t)channels + c];
				}
				line[y * channels + c] = sum;
			}
		}
		for (y = 0; y < height; y++)
		{
			for (c = 0; c < channels; c++)
				image[((size_t)y * (size_t)width + (size_t)x) * (size_t)channels + c] = line[y * channels + c];
		}
	}
	free(line);
}

/* a shape's field at grid position (gx, gy), linearly; zero outside */
static float field_linear(const float *field, float gx, float gy)
{
	int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
	float ax = gx - (float)x0, ay = gy - (float)y0, sum = 0;
	int dx, dy;

	for (dy = 0; dy < 2; dy++)
	{
		for (dx = 0; dx < 2; dx++)
		{
			int x = x0 + dx, y = y0 + dy;

			if (x >= 0 && x < FIELD_WIDTH && y >= 0 && y < FIELD_HEIGHT)
				sum += (dx ? ax : 1 - ax) * (dy ? ay : 1 - ay) * field[y * FIELD_WIDTH + x];
		}
	}
	return sum;
}

/* the Catmull-Rom weights at t (0 to 1) */
static void cubic_weights(float t, float weights[4])
{
	float t2 = t * t, t3 = t2 * t;

	weights[0] = 0.5f * (-t3 + 2 * t2 - t);
	weights[1] = 0.5f * (3 * t3 - 5 * t2 + 2);
	weights[2] = 0.5f * (-3 * t3 + 4 * t2 + t);
	weights[3] = 0.5f * (t3 - t2);
}

/* the field at (gx, gy), cubically; zero outside */
static float field_cubic(const float *field, float gx, float gy)
{
	int x0 = (int)floorf(gx), y0 = (int)floorf(gy), i, j;
	float wx[4], wy[4], sum = 0;

	cubic_weights(gx - (float)x0, wx);
	cubic_weights(gy - (float)y0, wy);
	for (j = 0; j < 4; j++)
	{
		int y = y0 - 1 + j;

		if (y < 0 || y >= FIELD_HEIGHT)
			continue;
		for (i = 0; i < 4; i++)
		{
			int x = x0 - 1 + i;

			if (x >= 0 && x < FIELD_WIDTH)
				sum += wx[i] * wy[j] * field[y * FIELD_WIDTH + x];
		}
	}
	return sum;
}

/* where sample s of an instance falls in its shape's grid (moved by its
move and (mx, my) more) */
static void sample_position(const struct instance *instance, long s, float mx, float my, float *gx, float *gy)
{
	*gx = (instance->px[s] - instance->x - instance->dx - mx - LEFT) * GRID;
	*gy = (instance->py[s] - instance->y - instance->dy - my - TOP) * GRID;
}

/* the shape of character from its instances' kept samples */
static void shape_build(struct title_tool *tool, int character, const float *kernel, int radius)
{
	float *field = tool->shapes[character].field;
	float *weight = calloc(FIELD_WIDTH * FIELD_HEIGHT, sizeof(float));
	long i, s;

	if (!weight)
		return;
	memset(field, 0, FIELD_WIDTH * FIELD_HEIGHT * sizeof(float));
	for (i = 0; i < tool->instance_count; i++)
	{
		const struct instance *instance = &tool->instances[i];

		if (instance->character != character)
			continue;
		for (s = 0; s < instance->count; s++)
		{
			float gx, gy, ax, ay;
			int x0, y0, dx, dy;

			if (!instance->keep[s])
				continue;
			sample_position(instance, s, 0, 0, &gx, &gy);
			x0 = (int)floorf(gx);
			y0 = (int)floorf(gy);
			ax = gx - (float)x0;
			ay = gy - (float)y0;
			for (dy = 0; dy < 2; dy++)
			{
				for (dx = 0; dx < 2; dx++)
				{
					int x = x0 + dx, y = y0 + dy;
					float w = (dx ? ax : 1 - ax) * (dy ? ay : 1 - ay);

					if (x >= 0 && x < FIELD_WIDTH && y >= 0 && y < FIELD_HEIGHT)
					{
						field[y * FIELD_WIDTH + x] += w * instance->value[s];
						weight[y * FIELD_WIDTH + x] += w;
					}
				}
			}
		}
	}
	blur(field, FIELD_WIDTH, FIELD_HEIGHT, 1, kernel, radius);
	blur(weight, FIELD_WIDTH, FIELD_HEIGHT, 1, kernel, radius);
	for (i = 0; i < FIELD_WIDTH * FIELD_HEIGHT; i++)
		field[i] = weight[i] > 1e-3f ? field[i] / weight[i] : 0;
	free(weight);
}

/* the instance moved to where its samples best match its shape */
static void instance_register(struct instance *instance, const float *field)
{
	float best = -1, best_x = 0, best_y = 0;
	int mx, my;
	long s;

	for (my = -MOVE_Y; my <= MOVE_Y; my++)
	{
		for (mx = -MOVE_X; mx <= MOVE_X; mx++)
		{
			float error = 0;

			for (s = 0; s < instance->count; s++)
			{
				float gx, gy, d;

				sample_position(instance, s, 0, 0, &gx, &gy);
				d = field_linear(field, gx - mx * GRID / 8.0f - 0.5f, gy - my * GRID / 8.0f - 0.5f) - instance->value[s];
				error += d * d;
			}
			if (best < 0 || error < best)
			{
				best = error;
				best_x = mx / 8.0f;
				best_y = my / 8.0f;
			}
		}
	}
	instance->dx = best_x;
	instance->dy = best_y;
}

/* an instance's shape at sample s of another instance (or its own) */
static float instance_cover(const struct title_tool *tool, const struct instance *shape_of,
	const struct instance *at, long s)
{
	float gx = (at->px[s] - shape_of->x - shape_of->dx - LEFT) * GRID - 0.5f;
	float gy = (at->py[s] - shape_of->y - shape_of->dy - TOP) * GRID - 0.5f;

	return field_linear(tool->shapes[shape_of->character].field, gx, gy);
}

static int shapes_build(struct title_tool *tool)
{
	int radius, character, counts[256] = { 0 };
	float *kernel = gaussian_kernel(SMOOTHING * GRID, &radius);
	long i, s;

	if (!kernel)
		return 0;
	for (i = 0; i < tool->instance_count; i++)
		counts[tool->instances[i].character]++;
	for (character = 0; character < 256; character++)
	{
		if (!counts[character])
			continue;
		tool->shapes[character].field = malloc(FIELD_WIDTH * FIELD_HEIGHT * sizeof(float));
		if (!tool->shapes[character].field)
		{
			free(kernel);
			return 0;
		}
		shape_build(tool, character, kernel, radius);
		if (counts[character] > 1)
		{
			for (i = 0; i < tool->instance_count; i++)
			{
				if (tool->instances[i].character == character)
					instance_register(&tool->instances[i], tool->shapes[character].field);
			}
			shape_build(tool, character, kernel, radius);
		}
	}
	/* (the samples a neighbor covers more than the letter itself) */
	for (i = 0; i < tool->instance_count; i++)
	{
		struct instance *instance = &tool->instances[i];

		for (s = 0; s < instance->count; s++)
		{
			float neighbor = 0, own = instance_cover(tool, instance, instance, s);
			int side;

			for (side = -1; side <= 1; side += 2)
			{
				long other = i + side;

				if (other >= 0 && other < tool->instance_count && tool->instances[other].title == instance->title)
				{
					float cover = instance_cover(tool, &tool->instances[other], instance, s);

					neighbor = cover > neighbor ? cover : neighbor;
				}
			}
			instance->keep[s] = !(neighbor > NEIGHBOR && neighbor > own);
		}
	}
	for (character = 0; character < 256; character++)
	{
		if (counts[character])
			shape_build(tool, character, kernel, radius);
	}
	/* (and, where there are enough instances to tell, those far from the
	shape) */
	for (character = 0; character < 256; character++)
	{
		if (counts[character] < 3)
			continue;
		for (i = 0; i < tool->instance_count; i++)
		{
			struct instance *instance = &tool->instances[i];

			if (instance->character != character)
				continue;
			for (s = 0; s < instance->count; s++)
			{
				if (fabsf(instance_cover(tool, instance, instance, s) - instance->value[s]) >= OUTLIER)
					instance->keep[s] = 0;
			}
		}
		shape_build(tool, character, kernel, radius);
	}
	free(kernel);
	return 1;
}

/* ---------- the tool */

static int instances_add(struct title_tool *tool, long title, const struct title_tool_title *entry)
{
	struct picture *picture = &tool->pictures[title];
	const char *text = entry->text;
	long letters = 0, k, length = (long)strlen(text);
	struct instance *grown;

	for (k = 0; k < length; k++)
		letters += text[k] != ' ';
	grown = realloc(tool->instances, (size_t)(tool->instance_count + letters) * sizeof(*grown));
	if (!grown)
		return 0;
	tool->instances = grown;
	picture->first = tool->instance_count;
	picture->letters = letters;
	for (k = 0; k < letters; k++)
	{
		struct instance *instance = &tool->instances[tool->instance_count];
		float x = entry->lefts[k], low = x - 0.75f;
		float high = k + 1 < letters ? entry->lefts[k + 1] - 0.25f : x + 40;
		int row0 = (int)picture->baseline + TOP, column0 = (int)floorf(low), column1 = (int)ceilf(high);
		int row, column, character = 0;
		long letter = -1, at;

		for (at = 0; at < length; at++)
		{
			if (text[at] != ' ' && ++letter == k)
			{
				character = (unsigned char)text[at];
				break;
			}
		}
		memset(instance, 0, sizeof(*instance));
		instance->title = title;
		instance->character = character;
		instance->x = x;
		instance->y = picture->baseline;
		instance->px = malloc((size_t)HEIGHT * (size_t)(column1 - column0 + 1) * sizeof(float));
		instance->py = malloc((size_t)HEIGHT * (size_t)(column1 - column0 + 1) * sizeof(float));
		instance->value = malloc((size_t)HEIGHT * (size_t)(column1 - column0 + 1) * sizeof(float));
		instance->keep = malloc((size_t)HEIGHT * (size_t)(column1 - column0 + 1));
		tool->instance_count++;
		if (!instance->px || !instance->py || !instance->value || !instance->keep)
			return 0;
		for (row = row0; row < row0 + HEIGHT; row++)
		{
			if (row < 0 || row >= picture->height)
				continue;
			for (column = column0; column <= column1; column++)
			{
				if (column < 0 || column >= picture->width || column + 0.5f < low || column + 0.5f > high)
					continue;
				instance->px[instance->count] = (float)column + 0.5f;
				instance->py[instance->count] = (float)row + 0.5f;
				instance->value[instance->count] = picture->coverage[row * picture->width + column];
				instance->keep[instance->count] = 1;
				instance->count++;
			}
		}
	}
	return 1;
}

struct title_tool *title_tool_new(const char *path, const struct title_tool_title *titles, long count)
{
	unsigned long size = 0;
	unsigned char *data = map_read(path, &size);
	struct title_tool *tool;
	long index, found = 0;

	if (!data)
		return NULL;
	tool = calloc(1, sizeof(*tool));
	if (!tool || !(tool->pictures = calloc((size_t)(count > 0 ? count : 1), sizeof(*tool->pictures))))
	{
		free(tool);
		free(data);
		return NULL;
	}
	tool->count = count;
	for (index = 0; index < count; index++)
	{
		struct picture *picture = &tool->pictures[index];
		int format = 0;
		const unsigned char *blocks = bitmap_find(data, size, titles[index].tag, titles[index].bitmap,
			&picture->width, &picture->height, &format);

		/* (only the pixels its letters were measured on) */
		if (!blocks || crc32(0L, blocks, (uInt)(picture->width * picture->height)) != titles[index].crc ||
			titles[index].scale < 1 || titles[index].scale > 8)
			continue;
		picture->scale = titles[index].scale;
		picture->texels = dxt_decode(blocks, picture->width, picture->height, format);
		if (!picture->texels || !picture_read(picture) || !instances_add(tool, index, &titles[index]))
			continue;
		picture->present = 1;
		found++;
	}
	free(data);
	if (!found || !shapes_build(tool))
	{
		title_tool_free(tool);
		return NULL;
	}
	return tool;
}

int title_tool_has(const struct title_tool *tool, long index)
{
	return tool && index >= 0 && index < tool->count && tool->pictures[index].present;
}

/* ---------- drawing a title */

/* PIL's bicubic resampling (a = -0.5) of one axis: for each output position,
its first input and weights (support 2, as only enlarging) */
static float bicubic(float x)
{
	const float a = -0.5f;

	x = fabsf(x);
	if (x < 1)
		return ((a + 2) * x - (a + 3)) * x * x + 1;
	if (x < 2)
		return (((x - 5) * x + 8) * x - 4) * a;
	return 0;
}

/* image (straight RGBA, floats) enlarged scale times, filtered with its
color premultiplied and blurred first (tools/title_assets.py's enlarge) */
static float *enlarge(const float *image, int width, int height, int scale)
{
	int out_width = width * scale, out_height = height * scale, radius, x, y, c, k;
	float *premultiplied = malloc((size_t)width * (size_t)height * 4 * sizeof(float));
	float *across = malloc((size_t)out_width * (size_t)height * 4 * sizeof(float));
	float *result = malloc((size_t)out_width * (size_t)out_height * 4 * sizeof(float));
	float *kernel = gaussian_kernel(PLATE_SMOOTHING, &radius);
	long i;

	if (!premultiplied || !across || !result || !kernel)
	{
		free(premultiplied);
		free(across);
		free(result);
		free(kernel);
		return NULL;
	}
	for (i = 0; i < (long)width * height; i++)
	{
		float alpha = image[i * 4 + 3] / 255;

		for (c = 0; c < 3; c++)
			premultiplied[i * 4 + c] = image[i * 4 + c] * alpha;
		premultiplied[i * 4 + 3] = image[i * 4 + 3];
	}
	blur(premultiplied, width, height, 4, kernel, radius);
	free(kernel);
	for (x = 0; x < out_width; x++)
	{
		float center = ((float)x + 0.5f) / (float)scale, weights[5], total = 0;
		int first = (int)(center - 2 + 0.5f), last = (int)(center + 2 + 0.5f);

		first = first < 0 ? 0 : first;
		last = last > width ? width : last;
		for (k = first; k < last && k - first < 5; k++)
			total += weights[k - first] = bicubic(((float)k - center + 0.5f));
		for (y = 0; y < height; y++)
		{
			for (c = 0; c < 4; c++)
			{
				float sum = 0;

				for (k = first; k < last && k - first < 5; k++)
					sum += weights[k - first] * premultiplied[((size_t)y * (size_t)width + (size_t)k) * 4 + c];
				across[((size_t)y * (size_t)out_width + (size_t)x) * 4 + c] = total != 0 ? sum / total : 0;
			}
		}
	}
	for (y = 0; y < out_height; y++)
	{
		float center = ((float)y + 0.5f) / (float)scale, weights[5], total = 0;
		int first = (int)(center - 2 + 0.5f), last = (int)(center + 2 + 0.5f);

		first = first < 0 ? 0 : first;
		last = last > height ? height : last;
		for (k = first; k < last && k - first < 5; k++)
			total += weights[k - first] = bicubic(((float)k - center + 0.5f));
		for (x = 0; x < out_width; x++)
		{
			float *out = result + ((size_t)y * (size_t)out_width + (size_t)x) * 4, alpha;

			for (c = 0; c < 4; c++)
			{
				float sum = 0;

				for (k = first; k < last && k - first < 5; k++)
					sum += weights[k - first] * across[((size_t)k * (size_t)out_width + (size_t)x) * 4 + c];
				sum = total != 0 ? sum / total : 0;
				out[c] = sum < 0 ? 0 : sum > 255 ? 255 : sum;
			}
			alpha = out[3] / 255;
			for (c = 0; c < 3; c++)
			{
				float v = alpha > 1e-4f ? out[c] / alpha : 0;

				out[c] = v > 255 ? 255 : v;
			}
		}
	}
	free(premultiplied);
	free(across);
	return result;
}

/* the picture with its old text (and a texel around it) taken from the
nearest texels not in it, then softened (tools/title_assets.py's fill_in) */
static float *plate(const struct picture *picture)
{
	int width = picture->width, height = picture->height, x, y, c, r;
	long texels = (long)width * height, i;
	unsigned char *edge = malloc((size_t)texels);
	float *filled = malloc((size_t)texels * 4 * sizeof(float)), *result = malloc((size_t)texels * 4 * sizeof(float));

	if (!edge || !filled || !result)
	{
		free(edge);
		free(filled);
		free(result);
		return NULL;
	}
	morphology(picture->mask, edge, width, height, 1);
	for (i = 0; i < texels * 4; i++)
		filled[i] = result[i] = picture->texels[i];
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			long best = -1, best_distance = 0;

			if (!edge[y * width + x])
				continue;
			/* (the nearest texel not in it: searched in growing squares,
			then the one found checked against the rest of its circle) */
			for (r = 1; r < 64 && (best < 0 || (long)(r - 1) * (r - 1) <= best_distance); r++)
			{
				int dx, dy;

				for (dy = -r; dy <= r; dy++)
				{
					for (dx = -r; dx <= r; dx++)
					{
						int sx = x + dx, sy = y + dy;
						long distance = (long)dx * dx + (long)dy * dy;

						if ((dx != -r && dx != r && dy != -r && dy != r) || sx < 0 || sy < 0 || sx >= width ||
							sy >= height || edge[sy * width + sx])
							continue;
						if (best < 0 || distance < best_distance)
						{
							best = (long)sy * width + sx;
							best_distance = distance;
						}
					}
				}
			}
			for (c = 0; c < 4; c++)
				filled[((long)y * width + x) * 4 + c] = best >= 0 ? picture->texels[best * 4 + c] : 0;
		}
	}
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			if (!edge[y * width + x])
				continue;
			for (c = 0; c < 4; c++)
			{
				float sum = 0;
				int dx, dy;

				for (dy = -1; dy <= 1; dy++)
				{
					for (dx = -1; dx <= 1; dx++)
					{
						int sx = x + dx < 0 ? 0 : x + dx >= width ? width - 1 : x + dx;
						int sy = y + dy < 0 ? 0 : y + dy >= height ? height - 1 : y + dy;

						sum += filled[((long)sy * width + sx) * 4 + c];
					}
				}
				result[((long)y * width + x) * 4 + c] = sum / 9;
			}
		}
	}
	free(edge);
	free(filled);
	return result;
}

/* the text's coverage at the picture's scale: each letter's shape where its
instance is, sharpened to an edge a pixel wide */
static float *letters_draw(const struct title_tool *tool, const struct picture *picture)
{
	int scale = picture->scale, out_width = picture->width * scale, out_height = picture->height * scale;
	float *coverage = calloc((size_t)out_width * (size_t)out_height, sizeof(float));
	long k;

	if (!coverage)
		return NULL;
	for (k = 0; k < picture->letters; k++)
	{
		const struct instance *instance = &tool->instances[picture->first + k];
		const float *field = tool->shapes[instance->character].field;
		float x = instance->x + instance->dx, y = instance->y + instance->dy;
		int x0 = (int)((x + LEFT) * scale), x1 = (int)((x + LEFT + WIDTH) * scale);
		int y0 = (int)((y + TOP) * scale), y1 = (int)((y + TOP + HEIGHT) * scale), px, py;

		x0 = x0 < 0 ? 0 : x0;
		y0 = y0 < 0 ? 0 : y0;
		x1 = x1 > out_width ? out_width : x1;
		y1 = y1 > out_height ? out_height : y1;
		for (py = y0; py < y1; py++)
		{
			float gy = (((float)py + 0.5f) / (float)scale - y - TOP) * GRID - 0.5f;

			for (px = x0; px < x1; px++)
			{
				float gx = (((float)px + 0.5f) / (float)scale - x - LEFT) * GRID - 0.5f;
				float value = field_cubic(field, gx, gy), gradient, alpha;
				float ddx = (field_linear(field, gx + 1, gy) - field_linear(field, gx - 1, gy)) / 2;
				float ddy = (field_linear(field, gx, gy + 1) - field_linear(field, gx, gy - 1)) / 2;
				float *out = &coverage[(size_t)py * (size_t)out_width + (size_t)px];

				if (value < 0.02f && *out >= 1)
					continue;
				gradient = sqrtf(ddx * ddx + ddy * ddy) * GRID;
				alpha = 0.5f + (value - 0.5f) / (gradient > 0.3f ? gradient : 0.3f) * (float)scale;
				alpha = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
				if (alpha > *out)
					*out = alpha;
			}
		}
	}
	return coverage;
}

/* transparent pixels given the color of the nearest covered one in their
row or column, so that filtering at the edges blends in no black
(tools/title_assets.py's bleed, near enough) */
static void bleed(unsigned char *rgba, int width, int height)
{
	int x, y, pass;

	for (pass = 0; pass < 2; pass++)
	{
		int lines = pass ? width : height, length = pass ? height : width, line;

		for (line = 0; line < lines; line++)
		{
			int last = -1, at;

			for (at = 0; at < length; at++)
			{
				x = pass ? line : at;
				y = pass ? at : line;
				if (rgba[((size_t)y * (size_t)width + (size_t)x) * 4 + 3])
				{
					int fill;

					for (fill = last + 1; fill < at; fill++)
					{
						int fx = pass ? line : fill, fy = pass ? fill : line;
						int nearer = last >= 0 && fill - last < at - fill ? last : at;
						int nx = pass ? line : nearer, ny = pass ? nearer : line;

						memcpy(rgba + ((size_t)fy * (size_t)width + (size_t)fx) * 4,
							rgba + ((size_t)ny * (size_t)width + (size_t)nx) * 4, 3);
					}
					last = at;
				}
			}
			if (last >= 0)
			{
				for (at = last + 1; at < length; at++)
				{
					int fx = pass ? line : at, fy = pass ? at : line, nx = pass ? line : last, ny = pass ? last : line;

					memcpy(rgba + ((size_t)fy * (size_t)width + (size_t)fx) * 4,
						rgba + ((size_t)ny * (size_t)width + (size_t)nx) * 4, 3);
				}
			}
		}
	}
}

unsigned char *title_tool_render(const struct title_tool *tool, long index, unsigned long *width,
	unsigned long *height)
{
	const struct picture *picture;
	float *background, *base, *letters;
	unsigned char *result;
	long pixels, i;
	int c;

	if (!title_tool_has(tool, index))
		return NULL;
	picture = &tool->pictures[index];
	pixels = (long)picture->width * picture->scale * picture->height * picture->scale;
	base = plate(picture);
	background = base ? enlarge(base, picture->width, picture->height, picture->scale) : NULL;
	free(base);
	letters = background ? letters_draw(tool, picture) : NULL;
	result = letters ? malloc((size_t)pixels * 4) : NULL;
	if (!result)
	{
		free(background);
		free(letters);
		return NULL;
	}
	for (i = 0; i < pixels; i++)
	{
		float cover = letters[i] * picture->opacity, back = background[i * 4 + 3] / 255;
		float alpha = cover + back * (1 - cover);

		for (c = 0; c < 3; c++)
		{
			float v = alpha > 1e-4f ?
				(picture->color[c] * cover + background[i * 4 + c] * back * (1 - cover)) / alpha : 0;

			result[i * 4 + c] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
		}
		result[i * 4 + 3] = (unsigned char)(alpha * 255 + 0.5f);
	}
	free(background);
	free(letters);
	bleed(result, picture->width * picture->scale, picture->height * picture->scale);
	*width = (unsigned long)(picture->width * picture->scale);
	*height = (unsigned long)(picture->height * picture->scale);
	return result;
}

void title_tool_free(struct title_tool *tool)
{
	long i;

	if (!tool)
		return;
	for (i = 0; i < tool->count; i++)
	{
		free(tool->pictures[i].texels);
		free(tool->pictures[i].mask);
		free(tool->pictures[i].coverage);
	}
	for (i = 0; i < tool->instance_count; i++)
	{
		free(tool->instances[i].px);
		free(tool->instances[i].py);
		free(tool->instances[i].value);
		free(tool->instances[i].keep);
	}
	for (i = 0; i < 256; i++)
		free(tool->shapes[i].field);
	free(tool->pictures);
	free(tool->instances);
	free(tool);
}
