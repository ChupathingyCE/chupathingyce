/*
H2_HUD_MAKE.C

Halo 2's HUD of a Halo 2 level, made with the level (a part of h2_level.c,
through h2_weapons.c; built alone it is empty): the Master Chief's HUD
(ui\hud\masterchief: grenades, shield meter, motion sensor) and each made
weapon's and vehicle's (its new HUD interface, nhdt: its panel, ammunition
meter and count, reticles, scope), as Halo 2 lays them out. Each widget's
bitmap (the image its sequence names) is made a bitmap tag of the level
("h2\hud\<bitmap>#<image>", DXT5 on a power-of-two canvas, its own size the
corner of it); the widgets are written to the level's HUD file (t:\h2\
<map>.h2hud, h2_hud.h), which the game's Halo 2 HUD (h2_hud.c) draws.

An nhdt (0x28 bytes): its bitmap widgets at 0x8 (0x64 each: name, four
inputs, the "draw if" and "don't draw if" flags of the unit, extras, weapon
and game engine at 0x8..0x15, cutoffs, anchor at 0x1C - 0 top right, 1 top
left, 2 bottom left, 3 bottom right, 4 the centre -, flags, bitmap at 0x20,
shader at 0x28, sequence at 0x30, offset at 0x34, registration point at
0x40, special type at 0x60), its text widgets at 0x10 (0x54: the same to
0x1F, shader at 0x20, string at 0x28, justification, offset at 0x40), its
low clip and low ammo cutoffs at 0x18 (Assembly's Halo 2 nhdt plugin,
checked against the owner's shared.map). A bitmap's sequences at 0x3C
(0x3C each: first image at 0x20, count at 0x22), images at 0x44.
*/

#ifdef H2_LEVEL_INTERNAL

#include "h2_hud.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"

void h2_level_folder_create(void);

enum
{
	/* (Relic's 25 HUDs: 257 images; 06b's and 07b's - 23 and 16 HUDs - over
	256; one of h2_hud.c's H2_HUD_LOADED_MAXIMUM is the white texel's) */
	H2H_MAXIMUM_IMAGES = 511,
	H2H_MAXIMUM_HUDS = 32,
	/* (a meter image's marks at most: the SMG's 60 rounds) */
	H2H_MAXIMUM_IMAGE_CELLS = 96,
};

static struct
{
	long count;
	struct h2h_image { long bitmap_tag; short image; unsigned char kind; unsigned char shift; long made; unsigned short width, height, canvas_width, canvas_height;
		short cell_first, cell_count; } images[H2H_MAXIMUM_IMAGES];
	/* the meters' marks (h2h_meter_cells), and the last image's (h2h_image) */
	long cell_count;
	struct h2_hud_file_cell cells[H2_HUD_MAXIMUM_CELLS];
	short last_cell_first, last_cell_count;
	long hud_count;
	struct h2h_made_hud { long nhdt; char owner[64]; } huds[H2H_MAXIMUM_HUDS];
} h2h;

/* the HUD file's bytes as they are made (h2_hud.h's structures one after
another): in memory - a level being made writes them to its .h2hud file
after, a level loaded (h2_hud_make_loaded) reads them as they are, no file
(feat/h2-direct-hud-menu) */
struct h2h_sink
{
	unsigned char *data;
	unsigned long size, capacity;
	boolean failed;
};

H2_ALIGN static void h2h_put(struct h2h_sink *sink, void const *bytes, unsigned long count)
{
	if (!sink || sink->failed)
		return;
	if (sink->size + count > sink->capacity)
	{
		unsigned long capacity = sink->capacity ? sink->capacity : 16384;
		unsigned char *grown;

		while (capacity < sink->size + count)
			capacity *= 2;
		if (!(grown = realloc(sink->data, capacity)))
		{
			sink->failed = TRUE;
			return;
		}
		sink->data = grown;
		sink->capacity = capacity;
	}
	memcpy(sink->data + sink->size, bytes, count);
	sink->size += count;
}

H2_ALIGN static void h2h_sink_free(struct h2h_sink *sink)
{
	if (sink->data)
		free(sink->data);
	memset(sink, 0, sizeof(*sink));
}

/* (a HUD made at load, h2_hud_make_loaded) each image handed to the caller as
it is made - made a texture there - and its pixels let go here at once: no
sheet is held past its own texture (feat/h2-direct-hud-menu) */
static struct
{
	h2_hud_texture_proc proc;
	void *context;
	long given, failed;
} h2h_texture;

H2_ALIGN static void h2h_hand_over(struct build *build, long made)
{
	struct bitmap *b;
	boolean full;

	if (!h2h_texture.proc || made == NONE)
		return;
	b = &build->bitmaps[made];
	full = b->sidecar_pixels != NULL;
	if (b->format == 16 && (full ? b->sidecar_pixels : b->pixels) &&
		h2h_texture.proc(h2h_texture.context, b->path, full ? b->sidecar_width : b->width, full ? b->sidecar_height : b->height,
			full ? b->sidecar_pixels : b->pixels, full ? b->sidecar_size : b->pixels_size))
	{
		h2h_texture.given++;
	}
	else
		h2h_texture.failed++;
	if (b->pixels)
		free(b->pixels);
	if (b->sidecar_pixels)
		free(b->sidecar_pixels);
	b->pixels = b->sidecar_pixels = NULL;
}

/* a texel block (16 B8G8R8A8 texels) as DXT5: its alpha (8 levels between
its lowest and highest) and its colour (DXT1's four) */
H2_ALIGN static void h2h_dxt5_block(unsigned char const *texels, unsigned char *out)
{
	int lo = 255, hi = 0, k;
	unsigned long long bits = 0;

	for (k = 0; k < 16; k++)
	{
		int a = texels[4 * k + 3];

		lo = a < lo ? a : lo;
		hi = a > hi ? a : hi;
	}
	out[0] = (unsigned char)hi;
	out[1] = (unsigned char)lo;
	for (k = 0; k < 16; k++)
	{
		int a = texels[4 * k + 3], code;

		if (hi == lo)
			code = 0;
		else
		{
			/* (palette: 0 hi, 1 lo, 2..7 hi to lo) */
			int step = (int)(((float)(hi - a) / (float)(hi - lo)) * 7.0f + 0.5f);

			code = step == 0 ? 0 : step == 7 ? 1 : step + 1;
		}
		bits |= (unsigned long long)code << (3 * k);
	}
	for (k = 0; k < 6; k++)
		out[2 + k] = (unsigned char)(bits >> (8 * k));
	h2w_dxt1_block(texels, out + 8);
}

/* a meter image's marks (B8G8R8A8 texels, w by h): Halo 2's ammunition
meters draw each round in the green channel and number the order they empty
in in the blue one (each round one value, the first to go the highest - the
SMG's top row from the left); its shader lights a texel while the meter's
input is at least its blue, the rest drawn dim. The rounds found - the
green's separate shapes, each one value of blue - are added to the marks
(their rectangles halved shift times); none when the image is one gradient
(the shield's and the plasma weapons' bars, filled from the left) */
H2_ALIGN static void h2h_meter_cells(unsigned char const *pixels, unsigned long w, unsigned long h, unsigned char shift,
	short *first, short *count)
{
	short *label = w && h && w * h <= 1024UL * 1024UL ? malloc(w * h * sizeof(short)) : NULL;
	long *stack = label ? malloc(w * h * sizeof(long)) : NULL;
	struct h2_hud_file_cell found[H2H_MAXIMUM_IMAGE_CELLS];
	long n = 0, k, i;
	boolean ok = label && stack;

	*first = 0;
	*count = 0;
	if (ok)
	{
		for (k = 0; k < (long)(w * h); k++)
			label[k] = -1;
	}
	for (k = 0; ok && k < (long)(w * h); k++)
	{
		long top = 0, x0 = w, y0 = h, x1 = 0, y1 = 0, core = -1, lo = 255, hi = 0;

		if (label[k] >= 0 || pixels[4 * k + 1] < 64)
			continue;
		if (n >= H2H_MAXIMUM_IMAGE_CELLS)
		{
			ok = FALSE;
			break;
		}
		label[k] = (short)n;
		stack[top++] = k;
		while (top > 0)
		{
			long at = stack[--top], x = at % (long)w, y = at / (long)w, d;
			int blue = pixels[4 * at];

			x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1;
			y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
			if (core < 0 || pixels[4 * at + 1] > pixels[4 * core + 1])
				core = at;
			lo = blue < lo ? blue : lo, hi = blue > hi ? blue : hi;
			for (d = 0; d < 4; d++)
			{
				long nx = x + (d == 0) - (d == 1), ny = y + (d == 2) - (d == 3), next = ny * (long)w + nx;

				if (nx < 0 || ny < 0 || nx >= (long)w || ny >= (long)h || label[next] >= 0 || pixels[4 * next + 1] < 64)
					continue;
				label[next] = (short)n;
				stack[top++] = next;
			}
		}
		/* (a gradient, not a round: no marks) */
		if (hi - lo > 12)
		{
			ok = FALSE;
			break;
		}
		memset(&found[n], 0, sizeof(found[n]));
		found[n].x0 = (unsigned char)((x0 >> shift) > 255 ? 255 : x0 >> shift);
		found[n].y0 = (unsigned char)((y0 >> shift) > 255 ? 255 : y0 >> shift);
		found[n].x1 = (unsigned char)((x1 >> shift) > 255 ? 255 : x1 >> shift);
		found[n].y1 = (unsigned char)((y1 >> shift) > 255 ? 255 : y1 >> shift);
		found[n].threshold = pixels[4 * core];
		n++;
	}
	free(label);
	free(stack);
	if (!ok || n < 2 || h2h.cell_count + n > H2_HUD_MAXIMUM_CELLS)
		return;
	/* (the rounds' rectangles may not overlap: each drawn lit or dim once) */
	for (k = 0; k < n; k++)
	{
		for (i = k + 1; i < n; i++)
		{
			if (found[k].x0 <= found[i].x1 && found[i].x0 <= found[k].x1 && found[k].y0 <= found[i].y1 && found[i].y0 <= found[k].y1)
				return;
		}
	}
	/* (each grown a texel round where it meets no other: the rounds' soft
	edges, under the shape's half, drawn too) */
	for (k = 0; k < n; k++)
	{
		int side;

		for (side = 0; side < 4; side++)
		{
			struct h2_hud_file_cell grown = found[k];
			boolean clear = TRUE;

			if (side == 0 && grown.x0 > 0)
				grown.x0--;
			else if (side == 1 && grown.x1 < 255 && (unsigned long)grown.x1 + 1 < (w >> shift))
				grown.x1++;
			else if (side == 2 && grown.y0 > 0)
				grown.y0--;
			else if (side == 3 && grown.y1 < 255 && (unsigned long)grown.y1 + 1 < (h >> shift))
				grown.y1++;
			else
				continue;
			for (i = 0; i < n && clear; i++)
			{
				clear = i == k || !(grown.x0 <= found[i].x1 && found[i].x0 <= grown.x1 && grown.y0 <= found[i].y1 && found[i].y0 <= grown.y1);
			}
			if (clear)
				found[k] = grown;
		}
	}
	*first = (short)h2h.cell_count;
	*count = (short)n;
	memcpy(&h2h.cells[h2h.cell_count], found, n * sizeof(found[0]));
	h2h.cell_count += n;
}

/* the bitmap of an image of a Halo 2 bitmap (made once): its index among
the build's bitmaps, NONE */
H2_ALIGN static long h2h_image(struct build *build, long bitmap_tag, short image, unsigned char kind, unsigned short *width,
	unsigned short *height, unsigned short *canvas_width, unsigned short *canvas_height, unsigned char *shift)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long count, i;
	unsigned long images;
	unsigned char const *header;
	unsigned long reference, size, got = 0, w, h, cw, ch, x, y;
	unsigned short format;
	unsigned char *pixels, *canvas, *out, *full_pixels = NULL;
	unsigned long full_w = 0, full_h = 0;
	char path[256];
	long made;

	for (i = 0; i < h2h.count; i++)
	{
		if (h2h.images[i].bitmap_tag == bitmap_tag && h2h.images[i].image == image && h2h.images[i].kind == kind)
		{
			*width = h2h.images[i].width, *height = h2h.images[i].height;
			*canvas_width = h2h.images[i].canvas_width, *canvas_height = h2h.images[i].canvas_height;
			*shift = h2h.images[i].shift;
			h2h.last_cell_first = h2h.images[i].cell_first;
			h2h.last_cell_count = h2h.images[i].cell_count;
			return h2h.images[i].made;
		}
	}
	if (h2h.count >= H2H_MAXIMUM_IMAGES || !h2_cache_tag_get(cache, bitmap_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x44, IMAGE_SIZE(build), &count, &images) || image < 0 || image >= count ||
		!(header = h2_cache_at(cache, images + IMAGE_SIZE(build) * image, IMAGE_SIZE(build))))
	{
		return NONE;
	}
	w = (unsigned short)read16(header + 4);
	h = (unsigned short)read16(header + 6);
	format = (unsigned short)read16(header + 0xC);
	reference = read32(header + IMAGE_REFERENCE(build));
	size = read32(header + IMAGE_BYTES(build));
	if (!w || !h || w > 1024 || h > 1024 || !(format == 10 || format == 11) ||
		!(pixels = h2_cache_texture_read(cache, reference, size, &got)) || got < w * h * 4)
	{
		return NONE;
	}
	/* (a meter's rounds, before its channels are made the colour drawn;
	its halvings as below) */
	h2h.last_cell_first = h2h.last_cell_count = 0;
	if (kind == H2_HUD_SHADER_METER)
	{
		unsigned long tw = w, th = h;
		unsigned char halvings = 0;

		while (tw > 256UL || th > 256UL)
			halvings++, tw /= 2, th /= 2;
		h2h_meter_cells(pixels, w, h, halvings, &h2h.last_cell_first, &h2h.last_cell_count);
	}
	/* its channels made the colour drawn (Halo 2's HUD shaders' work,
	approximated: they pick a bitmap's channels - a panel's fill its alpha,
	its outline and icon blue, a meter's lit marks green, the motion
	sensor's disc its alpha and its ring and sweep blue - and tint them the
	HUD's blue) */
	{
		unsigned long k;
		boolean motion_sensor = strstr(tag_short_name(h2_cache_tag_name(cache, bitmap_tag)), "motion_sensor") != NULL;

		for (k = 0; k < w * h; k++)
		{
			unsigned char *p = pixels + 4 * k;   /* B, G, R, A */
			int r = p[2], g = p[1], b = p[0], a = format == 10 ? 255 : p[3];
			int alpha, light;

			if (kind == H2_HUD_SHADER_SCOPE)
			{
				p[0] = p[1] = p[2] = 0;
				p[3] = (unsigned char)a;
				continue;
			}
			/* (straight colour, alpha blended: the colour a species' tint
			multiplies, h2_hud.c; a panel's image in two passes - its fill,
			the green channel's gradient under the alpha channel's shape,
			then its foreground, the blue channel: outline and icons) */
			if (kind == H2_HUD_SHADER_FOREGROUND)
			{
				alpha = b;
				light = 255;
			}
			else if (kind == H2_HUD_SHADER_NORMAL)
			{
				/* (the motion sensor's disc: its alpha channel's 0.14 is
				drawn 0.23 over the view, one colour - MCC's frames, over a
				dark ground and a light one; its green the ping's gradient,
				h2_hud.c draws the ping) */
				alpha = motion_sensor ? (a * 13 / 8 > 255 ? 255 : a * 13 / 8) : a;
				light = motion_sensor ? 255 : 158 + (97 * g) / 255;
			}
			else if (kind == H2_HUD_SHADER_LIGHT)
			{
				alpha = a > b ? a : b;
				alpha = g > alpha ? g : alpha;
				light = 255;
			}
			else if (kind == H2_HUD_SHADER_METER)
			{
				alpha = g ? g : a;
				light = 255;
			}
			else if (kind == H2_HUD_SHADER_SHIELD)
			{
				alpha = a;
				light = 255;
			}
			else
			{
				alpha = a;
				light = 255;
			}
			(void)r;
			p[0] = p[1] = p[2] = (unsigned char)light;
			p[3] = (unsigned char)alpha;
		}
	}
	/* (halved past 128 texels a side, a scope's mask - drawn stretched over
	the screen - past 256: the HUD is drawn at 0.6 of Halo 2's 640x480
	size, and the cache file's room; drawn twice as big for each halving,
	*shift) */
	*shift = 0;
	/* (the PC tier: the image as it is, for the level's sidecar - its
	canvas the file's times the halvings', so its frames' fractions hold) */
	full_w = w;
	full_h = h;
	full_pixels = h2_texture_sidecar_enabled() ? malloc(w * h * 4) : NULL;
	if (full_pixels)
		memcpy(full_pixels, pixels, w * h * 4);
	/* (a meter's marks - a magazine's rounds - are a texel or two apart:
	kept to 256) */
	while (w > (kind == H2_HUD_SHADER_SCOPE || kind == H2_HUD_SHADER_METER ? 256UL : 128UL) ||
		h > (kind == H2_HUD_SHADER_SCOPE || kind == H2_HUD_SHADER_METER ? 256UL : 128UL))
	{
		(*shift)++;
		unsigned long nw = w / 2, nh = h / 2, x2, y2, c;

		for (y2 = 0; y2 < nh; y2++)
		{
			for (x2 = 0; x2 < nw; x2++)
			{
				for (c = 0; c < 4; c++)
					pixels[4 * (y2 * nw + x2) + c] = (unsigned char)((pixels[4 * ((2 * y2) * w + 2 * x2) + c] +
						pixels[4 * ((2 * y2) * w + 2 * x2 + 1) + c] + pixels[4 * ((2 * y2 + 1) * w + 2 * x2) + c] +
						pixels[4 * ((2 * y2 + 1) * w + 2 * x2 + 1) + c] + 2) / 4);
			}
		}
		w = nw;
		h = nh;
	}
	for (cw = 4; cw < w; cw *= 2)
		;
	for (ch = 4; ch < h; ch *= 2)
		;
	canvas = calloc(cw * ch * 4, 1);
	out = malloc(cw * ch);   /* (DXT5: a byte a texel) */
	if (!canvas || !out)
	{
		free(pixels);
		free(canvas);
		free(out);
		free(full_pixels);
		return NONE;
	}
	for (y = 0; y < h; y++)
	{
		memcpy(canvas + 4 * (y * cw), pixels + 4 * (y * w), 4 * w);

	}
	free(pixels);
	for (y = 0; y < ch; y += 4)
	{
		for (x = 0; x < cw; x += 4)
		{
			unsigned char texels[64];
			int k;

			for (k = 0; k < 16; k++)
				memcpy(texels + 4 * k, canvas + 4 * ((y + (k >> 2)) * cw + x + (k & 3)), 4);
			h2h_dxt5_block(texels, out + (y / 4) * (cw / 4) * 16 + (x / 4) * 16);
		}
	}
	free(canvas);
	snprintf(path, sizeof(path), "h2\\hud\\%s#%d.%d", tag_short_name(h2_cache_tag_name(cache, bitmap_tag)), image, kind);
	made = bitmap_made(build, path, (unsigned short)cw, (unsigned short)ch, 16, 0, 0, out, cw * ch);
	if (made == NONE)
	{
		free(full_pixels);
		return NONE;
	}
	if (full_pixels && *shift > 0)
	{
		unsigned long fcw = cw << *shift, fch = ch << *shift, fx, fy;
		unsigned char *full_canvas = calloc(fcw * fch * 4, 1), *full_out = malloc(fcw * fch);

		if (full_canvas && full_out)
		{
			for (fy = 0; fy < full_h && fy < fch; fy++)
				memcpy(full_canvas + 4 * (fy * fcw), full_pixels + 4 * (fy * full_w), 4 * (full_w < fcw ? full_w : fcw));
			for (fy = 0; fy < fch; fy += 4)
			{
				for (fx = 0; fx < fcw; fx += 4)
				{
					unsigned char texels[64];
					int k;

					for (k = 0; k < 16; k++)
						memcpy(texels + 4 * k, full_canvas + 4 * ((fy + (k >> 2)) * fcw + fx + (k & 3)), 4);
					h2h_dxt5_block(texels, full_out + (fy / 4) * (fcw / 4) * 16 + (fx / 4) * 16);
				}
			}
			build->bitmaps[made].sidecar_pixels = full_out;
			build->bitmaps[made].sidecar_size = fcw * fch;
			build->bitmaps[made].sidecar_width = (unsigned short)fcw;
			build->bitmaps[made].sidecar_height = (unsigned short)fch;
			full_out = NULL;
		}
		free(full_canvas);
		free(full_out);
	}
	free(full_pixels);
	h2h_hand_over(build, made);
	h2h.images[h2h.count].bitmap_tag = bitmap_tag;
	h2h.images[h2h.count].image = image;
	h2h.images[h2h.count].kind = kind;
	h2h.images[h2h.count].made = made;
	h2h.images[h2h.count].shift = *shift;
	h2h.images[h2h.count].cell_first = h2h.last_cell_first;
	h2h.images[h2h.count].cell_count = h2h.last_cell_count;
	*width = h2h.images[h2h.count].width = (unsigned short)w;
	*height = h2h.images[h2h.count].height = (unsigned short)h;
	*canvas_width = h2h.images[h2h.count].canvas_width = (unsigned short)cw;
	*canvas_height = h2h.images[h2h.count].canvas_height = (unsigned short)ch;
	h2h.count++;
	return made;
}

/* a widget's shader's kind, by its name */
H2_ALIGN static unsigned char h2h_shader_kind(char const *name)
{
	if (strstr(name, "scope"))
		return H2_HUD_SHADER_SCOPE;
	if (strstr(name, "shield"))
		return H2_HUD_SHADER_SHIELD;
	if (strstr(name, "meter"))
		return H2_HUD_SHADER_METER;
	if (strstr(name, "dim"))
		return H2_HUD_SHADER_DIM;
	if (strstr(name, "red"))
		return H2_HUD_SHADER_RED;
	if (strstr(name, "green"))
		return H2_HUD_SHADER_GREEN;
	return H2_HUD_SHADER_NORMAL;
}

/* an nhdt's widgets written (owner: whose HUD it is - "" the unit's, else
a made weapon's or vehicle's tag name) */
H2_ALIGN static void h2h_write_hud(struct build *build, struct h2h_sink *sink, long nhdt, char const *owner)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long bitmap_count = 0, text_count = 0, i;
	unsigned long bitmaps = 0, texts = 0;
	struct h2_hud_file_hud hud;

	if (!h2_cache_tag_get(cache, nhdt, &t))
		return;
	h2_cache_block(cache, t.address + 0x8, 0x64, &bitmap_count, &bitmaps);
	h2_cache_block(cache, t.address + 0x10, 0x54, &text_count, &texts);
	memset(&hud, 0, sizeof(hud));
	snprintf(hud.owner, sizeof(hud.owner), "%s", owner);
	hud.low_clip = h2w_read_short(cache, t.address + 0x18);
	hud.low_total = h2w_read_short(cache, t.address + 0x1A);
	hud.widget_count = (short)(bitmap_count + text_count);
	h2h_put(sink, &hud, sizeof(hud));
	for (i = 0; i < bitmap_count + text_count; i++)
	{
		boolean text = i >= bitmap_count;
		unsigned long b = text ? texts + 0x54 * (i - bitmap_count) : bitmaps + 0x64 * i;
		unsigned char const *c = h2_cache_at(cache, b, text ? 0x54 : 0x64);
		struct h2_hud_file_widget wd;
		long shader;
		char name[64];

		memset(&wd, 0, sizeof(wd));
		if (!c)
		{
			h2h_put(sink, &wd, sizeof(wd));
			continue;
		}
		h2_cache_string(cache, read32(c), wd.name, sizeof(wd.name));
		wd.type = text ? H2_HUD_WIDGET_TEXT : H2_HUD_WIDGET_BITMAP;
		memcpy(wd.inputs, c + 4, 4);
		memcpy(wd.draw_flags, c + 8, sizeof(wd.draw_flags));
		/* (the widgets of what this build has not: binoculars, the weak-spot and friendly reticles, voice and
		text chat - not drawn, their bitmaps not carried; the bomb's arming meter and the Territories bars are
		carried: h2_hud.c draws the territories' bars and a territory's taking) */
		/* (the scoreboard's emblems, h2_emblem.h: the shader's waypoint_emblem
		over emblem_dummy, half its 64 units a side by the widget's effect;
		the team's rows widget_simple_custom, the dummy plain white) */
		if (!text && strstr(wd.name, "player_emblem"))
		{
			wd.type = H2_HUD_WIDGET_EMBLEM;
			wd.anchor = (unsigned char)read16(c + 0x1C);
			wd.flags = (unsigned char)read16(c + 0x1E);
			wd.offset[0] = read16(c + 0x34);
			wd.offset[1] = read16(c + 0x36);
			wd.special = (long)read32(c + 0x60);
			wd.width = wd.height = 32;
			wd.justification = strstr(wd.name, "_teams") != NULL;
			h2h_put(sink, &wd, sizeof(wd));
			continue;
		}
		if (strstr(wd.name, "binocular") ||
			strstr(wd.name, "emblem") ||
			strstr(wd.name, "talking") || strstr(wd.name, "text_chat") ||
			strstr(wd.name, "weakspot") || strstr(wd.name, "headshot") || strstr(wd.name, "friend") ||
			strstr(wd.name, "plasma_tracking"))
		{
			wd.type = 0xFF;
			h2h_put(sink, &wd, sizeof(wd));
			continue;
		}
		wd.cutoffs[0] = read16(c + 0x16);
		wd.cutoffs[1] = read16(c + 0x18);
		wd.cutoffs[2] = read16(c + 0x1A);
		wd.anchor = (unsigned char)read16(c + 0x1C);
		wd.flags = (unsigned char)read16(c + 0x1E);
		shader = h2_cache_reference(cache, b + (text ? 0x20 : 0x28));
		snprintf(name, sizeof(name), "%s", shader != NONE ? tag_short_name(h2_cache_tag_name(cache, shader)) : "");
		wd.shader = h2h_shader_kind(name);
		/* (a scope's marks - its shader the scope's, not stretched over the
		screen - drawn light) */
		if (wd.shader == H2_HUD_SHADER_SCOPE && !(read16(c + 0x1E) & 0x10))
			wd.shader = H2_HUD_SHADER_LIGHT;
		if (text)
		{
			char string[64];

			h2_cache_string(cache, read32(c + 0x28), string, sizeof(string));
			/* (hud_input1..4: which input's value it shows) */
			wd.text_input = !strncmp(string, "hud_input", 9) ? (unsigned char)(string[9] - '0') : 0;
			/* (the scoreboard's: h2_hud.h's H2_HUD_TEXT_...) */
			if (!strcmp(string, "scoreboard_playerscore"))
				wd.text_input = H2_HUD_TEXT_PLAYER_SCORE;
			else if (!strcmp(string, "scoreboard_otherplayerscore"))
				wd.text_input = H2_HUD_TEXT_OTHER_SCORE;
			else if (!strcmp(string, "scoreboard_variantname"))
				wd.text_input = H2_HUD_TEXT_VARIANT_NAME;
			else if (!strcmp(string, "scoreboard_timeleft"))
				wd.text_input = H2_HUD_TEXT_TIME_LEFT;
			wd.justification = (unsigned char)read16(c + 0x2C);
			wd.offset[0] = read16(c + 0x40);
			wd.offset[1] = read16(c + 0x42);
		}
		else
		{
			long bitmap = h2_cache_reference(cache, b + 0x20);
			unsigned char sequence = c[0x30];
			long sequence_count;
			unsigned long sequences;
			struct h2_cache_tag bt;
			short image = sequence, image_count = 1;

			wd.offset[0] = read16(c + 0x34);
			wd.offset[1] = read16(c + 0x36);
			memcpy(wd.registration, c + 0x40, 8);
			wd.special = (long)read32(c + 0x60);
			/* the sequence's first image */
			if (bitmap != NONE && h2_cache_tag_get(cache, bitmap, &bt) &&
				h2_cache_block(cache, bt.address + 0x3C, 0x3C, &sequence_count, &sequences) && sequence < sequence_count)
			{
				image = h2w_read_short(cache, sequences + 0x3C * sequence + 0x20);
				image_count = h2w_read_short(cache, sequences + 0x3C * sequence + 0x22);
			}
			if (bitmap != NONE && h2h_image(build, bitmap, image, wd.shader, &wd.width, &wd.height, &wd.canvas_width, &wd.canvas_height, &wd.unused[0]) != NONE)
			{
				/* (a meter's rounds) */
				if (wd.shader == H2_HUD_SHADER_METER)
				{
					wd.cell_first = h2h.last_cell_first;
					wd.cell_count = (unsigned char)h2h.last_cell_count;
				}
				snprintf(wd.bitmap, sizeof(wd.bitmap), "h2\\hud\\%s#%d.%d", tag_short_name(h2_cache_tag_name(cache, bitmap)), image, wd.shader);
				if (wd.shader == H2_HUD_SHADER_NORMAL)
				{
					unsigned short fw, fh, fcw, fch;
					unsigned char fshift;

					if (h2h_image(build, bitmap, image, H2_HUD_SHADER_FOREGROUND, &fw, &fh, &fcw, &fch, &fshift) != NONE)
						snprintf(wd.bitmap_fg, sizeof(wd.bitmap_fg), "h2\\hud\\%s#%d.%d", tag_short_name(h2_cache_tag_name(cache, bitmap)), image, H2_HUD_SHADER_FOREGROUND);
				}
				/* (the Elite's: the sequence's second image) */
				if (image_count == 2)
				{
					unsigned char alt_shift;

					if (h2h_image(build, bitmap, image + 1, wd.shader, &wd.alt_width, &wd.alt_height, &wd.alt_canvas_width, &wd.alt_canvas_height, &alt_shift) != NONE)
					{
						wd.alt_shift = alt_shift;
						snprintf(wd.bitmap_alt, sizeof(wd.bitmap_alt), "h2\\hud\\%s#%d.%d", tag_short_name(h2_cache_tag_name(cache, bitmap)), image + 1, wd.shader);
						if (wd.shader == H2_HUD_SHADER_NORMAL)
						{
							unsigned short fw, fh, fcw, fch;
							unsigned char fshift;

							if (h2h_image(build, bitmap, image + 1, H2_HUD_SHADER_FOREGROUND, &fw, &fh, &fcw, &fch, &fshift) != NONE)
								snprintf(wd.bitmap_alt_fg, sizeof(wd.bitmap_alt_fg), "h2\\hud\\%s#%d.%d", tag_short_name(h2_cache_tag_name(cache, bitmap)), image + 1, H2_HUD_SHADER_FOREGROUND);
						}
					}
				}
			}
		}
		h2h_put(sink, &wd, sizeof(wd));
	}
}

/* the HUD globals' waypoint arrows (the nav points' looks: a script's
navpoint names one), their images made; written to the file (NULL: only
counted). Halo 2's hud globals keep this engine's layout of them - the
arrow bitmap, then a block of arrows, each a name, colour, opacity,
translucency, the on screen, off screen and occluded sequences and flags -
at an offset found here: the reference to the waypoints' bitmap followed by
a block whose first arrow is named "default"; the element's size by where
its second's name is, its colour by the first value after its name that
the sequences follow (0..its bitmap's count) */
H2_ALIGN static long h2h_waypoints(struct build *build, struct h2h_sink *sink)
{
	struct h2_cache *cache = build->cache;
	long hudg = h2v_tag_named(cache, 'hudg', "ui\\hud\\default"), count = 0, bitmap = NONE, sequence_count = 0, i;
	struct h2_cache_tag globals, bt;
	unsigned long o, elements = 0, stride = 0, color_at = 0, sequences = 0;

	if (hudg == NONE || !h2_cache_tag_get(cache, hudg, &globals))
		return 0;
	for (o = 0; o < 0x400 && !stride; o += 4)
	{
		long reference = h2_cache_reference(cache, globals.address + o);
		char const *name = reference != NONE ? h2_cache_tag_name(cache, reference) : NULL;
		unsigned char const *first;
		unsigned long s;

		if (!name || !strstr(name, "hud_waypoints") ||
			!h2_cache_block(cache, globals.address + o + 8, 32, &count, &elements) || count < 1 ||
			!(first = h2_cache_at(cache, elements, 32)) || strcmp((char const *)first, "default"))
		{
			continue;
		}
		bitmap = reference;
		stride = 32;
		/* (one arrow: its size as large as can be read; several: the
		second's name) */
		for (s = 36; count > 1 && s <= 0x100; s += 4)
		{
			unsigned char const *second = h2_cache_at(cache, elements + s, 32);

			if (second && second[0] >= 'a' && second[0] <= 'z' && memchr(second, 0, 32) && h2_cache_at(cache, elements, s * count))
			{
				stride = s;
				break;
			}
		}
		if (stride == 32)
			count = 0;
	}
	if (!stride || !count || bitmap == NONE || !h2_cache_tag_get(cache, bitmap, &bt) ||
		!h2_cache_block(cache, bt.address + 0x3C, 0x3C, &sequence_count, &sequences))
	{
		return 0;
	}
	/* (the colour: the first word after the name followed, three words
	on, by three sequences within the bitmap's) */
	for (o = 32; o + 18 <= stride && !color_at; o += 4)
	{
		unsigned char const *e = h2_cache_at(cache, elements + o, 18);
		short k;

		if (!e || !read32(e))
			continue;
		for (k = 0; k < 3; k++)
		{
			short sequence = (short)read16(e + 12 + 2 * k);

			if (sequence < -1 || sequence >= sequence_count)
				break;
		}
		if (k == 3 && readf(e + 4) > 0.0f && readf(e + 4) <= 1.0f)
			color_at = o;
	}
	if (!color_at)
		return 0;
	if (count > H2_HUD_MAXIMUM_WAYPOINTS)
		count = H2_HUD_MAXIMUM_WAYPOINTS;
	for (i = 0; sink && i < count; i++)
	{
		unsigned char const *e = h2_cache_at(cache, elements + stride * (unsigned long)i, stride);
		struct h2_hud_file_waypoint wp;
		short k;

		memset(&wp, 0, sizeof(wp));
		if (e)
		{
			memcpy(wp.name, e, 31);
			wp.color = read32(e + color_at);
			wp.opacity = readf(e + color_at + 4);
			/* (the flags: after the sequences, a word on) */
			wp.flags = color_at + 20 + 4 <= stride ? read32(e + color_at + 20) : 0;
			for (k = 0; k < H2_HUD_WAYPOINT_IMAGES; k++)
			{
				short sequence = (short)read16(e + color_at + 12 + 2 * k);
				short image;

				if (sequence < 0 || sequence >= sequence_count)
					continue;
				image = h2w_read_short(cache, sequences + 0x3C * sequence + 0x20);
				if (h2h_image(build, bitmap, image, H2_HUD_SHADER_LIGHT, &wp.width[k], &wp.height[k], &wp.canvas_width[k],
					&wp.canvas_height[k], &wp.shift[k]) != NONE)
				{
					snprintf(wp.bitmap[k], sizeof(wp.bitmap[k]), "h2\\hud\\%s#%d.%d", tag_short_name(h2_cache_tag_name(cache, bitmap)),
						image, H2_HUD_SHADER_LIGHT);
				}
			}
		}
		h2h_put(sink, &wp, sizeof(wp));
		if (getenv("HALO_H2_HUD_LOG"))
			platform_log("h2 hud: waypoint %ld '%s' colour %08lx opacity %.2f flags %lx: %s %s %s", i, wp.name, wp.color, wp.opacity,
				wp.flags, wp.bitmap[0], wp.bitmap[1], wp.bitmap[2]);
	}
	return count;
}

/* the HUDs made: the unit's, the made weapons' and vehicles', their file
written (its bitmaps made now: before the build's bitmaps are written) */
/* the postgame carnage report's strings (ui\\screens\\game_shell\\
postgame_statistics\\werds, a game shell string list: the main menu map's,
mainmenu.map beside the level's; the level's own looked in first): the
medals' names, the pages' titles and columns, the teams' names, written
after the cells as h2_hud_file_word each; the count written */
static char const *const h2h_word_ids[] =
{
	"postgame_header", "team", "place", "score", "player", "avg_life", "best_spree", "juggs_killed", "kills_as_jugg",
	"flag_saves", "flag_assists", "bomb_kills", "terrs_taken", "terrs_lost", "kills", "assists", "deaths", "suicides",
	"killed_most", "died_most", "total_medals", "best_medal", "team_stats", "player_stats", "kill_stats", "pvp_stats",
	"medal_stats", "damage_stats", "time_format", "multiple_kill_2", "multiple_kill_3", "multiple_kill_4",
	"multiple_kill_5", "multiple_kill_6", "multiple_kill_7", "5_kills_in_a_row", "10_kills_in_a_row",
	"15_kills_in_a_row", "20_kills_in_a_row", "25_kills_in_a_row", "sniper_kill", "collision_kill", "bash_kill",
	"stealth_kill", "killed_vehicle", "boarded_vehicle", "plasma_stick", "plase_stick", "flag_grab", "flag_carrier_kill",
	"flag_return", "bomb_plant", "bomb_carrier_kill", "bomb_diffused", "team0", "team1", "team2", "team3", "team4",
	"team5", "team6", "team7", "percentage_format", "shots_hit", "shots_fired", "hit_percentage", "head_shots",
	"kills_on_ball", "kills_on_hill", "bomb_grabs", "kills_from_ball", "kills_from_hill", "bomb_grab",
	"ball_carrier_kill", "flag_scored", "medals", "medals_earned", "pvp_killed", "pvp_killed_by", "b_done",
	"b_leave_party", "a_select_player_b_done",
};

H2_ALIGN static long h2h_words_from(struct h2_cache *cache, struct h2_hud_file_word *words, long count)
{
	long unic = h2_cache_tag_find(cache, 'unic', "ui\\screens\\game_shell\\postgame_statistics\\werds");
	struct h2_cache_tag list;
	unsigned char const *p;
	long first, strings, k, found = 0;

	if (unic == NONE || !h2_cache_tag_get(cache, unic, &list) || list.group_tag != 'unic' ||
		(p = h2_cache_at(cache, list.address + 0x10, 4)) == NULL)
	{
		return 0;
	}
	/* (the list's first string of the globals' language pack and its count:
	h2_menu_make.c's H2M_UNIC_LANGUAGES, the English language's) */
	first = (unsigned short)read16(p);
	strings = (unsigned short)read16(p + 2);
	for (k = 0; k < strings; k++)
	{
		unsigned long id = 0;
		char text[256], name[64];
		long w;

		if (!h2_cache_localized_string(cache, 0, first + k, &id, text, sizeof(text)) ||
			!h2_cache_string(cache, id, name, sizeof(name)))
		{
			continue;
		}
		for (w = 0; w < (long)NUMBEROF(h2h_word_ids) && count < H2_HUD_MAXIMUM_WORDS; w++)
		{
			if (strcmp(name, h2h_word_ids[w]))
				continue;
			memset(&words[count], 0, sizeof(words[count]));
			snprintf(words[count].id, sizeof(words[count].id), "%s", name);
			snprintf(words[count].text, sizeof(words[count].text), "%s", text);
			count++;
			found++;
			break;
		}
	}
	return found;
}

H2_ALIGN static long h2h_words(struct build *build, struct h2h_sink *sink)
{
	static struct h2_hud_file_word words[H2_HUD_MAXIMUM_WORDS];
	long count = h2h_words_from(build->cache, words, 0);

	if (!count && h2_level_maps_directory()[0])
	{
		char why[160];
		struct h2_cache *menu = h2_cache_open(h2_level_maps_directory(), "mainmenu", why, sizeof(why));

		if (menu)
		{
			count = h2h_words_from(menu, words, 0);
			h2_cache_close(menu);
		}
		else
			platform_log("h2 hud: no postgame words: mainmenu.map: %s", why);
	}
	if (count)
		h2h_put(sink, words, sizeof(words[0]) * (unsigned long)count);
	return count;
}

/* the weapons whose HUDs are made: a level being made has them in h2w; a level loaded (h2_hud_make_loaded)
finds them as its tags "h2\\weapons\\<name>", in the order they were made */
static struct
{
	long count;
	struct { long h2_tag; char name[64]; } weapons[H2W_MAXIMUM_WEAPONS];
} h2h_list;

/* the HUDs made into `sink` (the HUD file's bytes) and their bitmaps made the build's: nothing when the map has no
Halo 2 HUD (the sink empty) */
H2_ALIGN static void h2h_make(struct build *build, struct h2h_sink *sink)
{
	struct h2_cache *cache = build->cache;
	struct h2_hud_file_header header;
	long i, unit_hud;

	h2h.count = 0;
	h2h.hud_count = 0;
	h2h.cell_count = 0;
	if (getenv("HALO_H2_NO_HUD") || !h2_cache_is_mcc(cache))
		return;
	unit_hud = h2v_tag_named(cache, 'nhdt', "ui\\hud\\masterchief");
	if (unit_hud == NONE)
		return;
	h2h.huds[h2h.hud_count].nhdt = unit_hud;
	h2h.huds[h2h.hud_count++].owner[0] = 0;
	/* (the Arbiter's own unit HUD, where the level has it - ui\hud\dervish:
	its grenades, shields and motion sensor, and its camouflage's
	indicator by the grenades; drawn in the Chief's place while the
	player is an Elite) */
	{
		long dervish = h2v_tag_named(cache, 'nhdt', "ui\\hud\\dervish");

		if (dervish != NONE && h2h.hud_count < H2H_MAXIMUM_HUDS)
		{
			h2h.huds[h2h.hud_count].nhdt = dervish;
			snprintf(h2h.huds[h2h.hud_count++].owner, sizeof(h2h.huds[0].owner), "%s", H2_HUD_OWNER_ELITE);
		}
	}
	/* (a multiplayer game's scoreboard: the unit's too) */
	{
		long scoreboard = h2v_tag_named(cache, 'nhdt', "ui\\hud\\scoreboard");

		if (scoreboard != NONE && build->multiplayer)
		{
			h2h.huds[h2h.hud_count].nhdt = scoreboard;
			h2h.huds[h2h.hud_count++].owner[0] = 0;
		}
	}
	for (i = 0; i < h2h_list.count && h2h.hud_count < H2H_MAXIMUM_HUDS; i++)
	{
		struct h2_cache_tag t;
		long nhdt;

		if (!h2_cache_tag_get(cache, h2h_list.weapons[i].h2_tag, &t) || (nhdt = h2_cache_reference(cache, t.address + 0x2B0)) == NONE)
			continue;
		h2h.huds[h2h.hud_count].nhdt = nhdt;
		snprintf(h2h.huds[h2h.hud_count++].owner, 64, "h2\\weapons\\%s", h2h_list.weapons[i].name);
	}
	/* the vehicles' guns' HUDs (their reticles and meters), owned "vehicle:<kind>":
	h2_hud.c draws one for the unit that aims the gun */
	{
		static struct { char const *kind, *weapon; } const guns[] =
		{
			{ "ghost", "objects\\vehicles\\ghost\\weapon\\ghost_gun" },
			{ "chaingun", "objects\\vehicles\\warthog\\turrets\\chaingun\\weapon\\chaingun_turret" },
			{ "gauss", "objects\\vehicles\\warthog\\turrets\\gauss\\weapon\\gauss_turret" },
			{ "turret", "objects\\vehicles\\h_turret_ap\\weapon\\h_turret_ap" },
			/* (the rest of Halo 2's vehicle guns with a HUD: the Banshee's,
			the Scorpion's cannon - the multiplayer one's too -, the Wraith's
			mortar, the Covenant turret's, the Shadow's turret) */
			{ "banshee", "objects\\vehicles\\banshee\\weapon\\banshee_gun" },
			{ "scorpion", "objects\\vehicles\\scorpion\\turrets\\cannon\\weapon\\cannon_turret_mp" },
			{ "scorpion", "objects\\vehicles\\scorpion\\turrets\\cannon\\weapon\\cannon_turret" },
			{ "wraith", "objects\\vehicles\\wraith\\turrets\\mortar\\mortar_turret_mp" },
			{ "wraith", "objects\\vehicles\\wraith\\turrets\\mortar\\mortar_turret" },
			{ "c_turret", "objects\\vehicles\\c_turret_ap\\weapon\\big_needler" },
			{ "creep", "objects\\vehicles\\creep\\plasma_turret\\weapons\\creep_turret" },
		};
		long g, made_count = 0;

		for (g = 0; g < (long)(sizeof(guns) / sizeof(guns[0])) && h2h.hud_count < H2H_MAXIMUM_HUDS; g++)
		{
			long weapon = h2v_tag_named(cache, 'weap', guns[g].weapon), nhdt;
			struct h2_cache_tag t;

			long k;

			if (weapon == NONE || !h2_cache_tag_get(cache, weapon, &t) || (nhdt = h2_cache_reference(cache, t.address + 0x2B0)) == NONE)
				continue;
			/* (one HUD a kind: the first of its weapons the level has) */
			for (k = 0; k < h2h.hud_count; k++)
			{
				if (!strncmp(h2h.huds[k].owner, "vehicle:", 8) && !strcmp(h2h.huds[k].owner + 8, guns[g].kind))
					break;
			}
			if (k < h2h.hud_count)
				continue;
			h2h.huds[h2h.hud_count].nhdt = nhdt;
			snprintf(h2h.huds[h2h.hud_count++].owner, 64, "vehicle:%s", guns[g].kind);
			made_count++;
		}
		(void)made_count;
	}
	/* (a white texel's bitmap: the scoreboard's rows' backgrounds, h2_hud.c) */
	{
		unsigned char texels[64], *out = malloc(16);

		memset(texels, 255, sizeof(texels));
		if (out)
		{
			h2h_dxt5_block(texels, out);
			h2h_hand_over(build, bitmap_made(build, "h2\\hud\\white", 4, 4, 16, 0, 0, out, 16));
		}
	}
	memset(&header, 0, sizeof(header));
	header.signature = H2_HUD_FILE_SIGNATURE;
	header.version = H2_HUD_FILE_VERSION;
	header.hud_count = h2h.hud_count;
	/* (Halo 2's HUD globals' motion sensor, 0x290: its range and the slowest
	speed that shows a unit on it) */
	{
		long hudg = h2v_tag_named(cache, 'hudg', "ui\\hud\\default");
		struct h2_cache_tag globals;

		if (hudg != NONE && h2_cache_tag_get(cache, hudg, &globals))
		{
			float range = h2w_read_real(cache, globals.address + 0x290), minimum = h2w_read_real(cache, globals.address + 0x294);

			if (range > 1.0f && range < 100.0f && minimum >= 0.0f && minimum < 1.0f)
			{
				header.motion_sensor_range = range;
				header.motion_sensor_minimum_speed = minimum;
			}
		}
	}
	header.waypoint_count = h2h_waypoints(build, NULL);
	/* (the meters' rounds: found as their images are made, by the HUDs
	written - so written after them, the header again) */
	h2h_put(sink, &header, sizeof(header));
	for (i = 0; i < h2h.hud_count; i++)
		h2h_write_hud(build, sink, h2h.huds[i].nhdt, h2h.huds[i].owner);
	h2h_waypoints(build, sink);
	header.cell_count = h2h.cell_count;
	h2h_put(sink, h2h.cells, sizeof(h2h.cells[0]) * (unsigned long)h2h.cell_count);
	header.word_count = h2h_words(build, sink);
	if (!sink->failed && sink->size >= sizeof(header))
		memcpy(sink->data, &header, sizeof(header));
	platform_log("h2 hud: %ld HUDs, %ld bitmaps made, %ld meter rounds, %ld postgame words (%lu bytes%s)", h2h.hud_count, h2h.count,
		h2h.cell_count, header.word_count, sink->size, sink->failed ? ", NOT all kept: no memory" : "");
}

/* (a level being made - the builder's, and the direct make's: h2_weapons.c)
nothing of the HUD: its widgets and sheets are made from the map as the
level loads (h2_hud_make_loaded), no HUD file and no sheets in the level */
H2_ALIGN static void h2_hud_make(struct build *build)
{
	(void)build;
}

/* (h2_hud.c, HALO_H2_HUD_AT_LOAD) the loaded Halo 2 level's HUD made from its MCC map as the level loads: the
widgets (the HUD file's bytes, as a level being made writes them - in memory, no file) handed over in `widgets`
(malloc'd here, h2_hud_loaded_free's), and each image of its bitmaps - DXT5, a PC tier's at full size, as its
sidecar gives them - handed to `texture` as it is made, its pixels let go once it returns: FALSE when anything
fails (the textures given so far the caller's to let go), and the level's own file and bitmaps are used */

/* (h2_hud.c) the widgets handed over, freed where they were made (this file's allocator) */
H2_ALIGN void h2_hud_loaded_free(unsigned char *widgets)
{
	if (widgets)
		free(widgets);
}

H2_ALIGN boolean h2_hud_make_loaded(char const *map_name, unsigned char **widgets, unsigned long *widgets_size,
	h2_hud_texture_proc texture, void *context, long *image_count)
{
	char const *directory = map_name ? h2_maps_directory_of(map_name) : NULL;
	char why[160];
	struct h2_cache *cache;
	struct build *build;
	struct tag_iterator iterator;
	long index, i, k;
	boolean ok;
	struct h2h_sink sink;

	*widgets = NULL;
	*widgets_size = 0;
	*image_count = 0;
	if (!directory || !h2_level_is_halo2_map(directory, map_name) || !(cache = h2_cache_open(directory, map_name, why, sizeof(why))))
		return FALSE;
	build = calloc(1, sizeof(*build));
	if (!build)
	{
		h2_cache_close(cache);
		return FALSE;
	}
	build->cache = cache;
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	/* (made during the play: the map's inflated pieces to a budget, in the
	memory window, let go as the cache is closed - the open is the level's
	kept one (h2_cache.h), which would hold them through the play
	otherwise; as h2_hud.c's events' read) */
	h2_cache_segment_memory(cache, h2_level_window_alloc, h2_level_window_free);
	h2_cache_segment_budget(cache, 2048UL * 1024UL);
#endif
	build->multiplayer = global_scenario_get()->netgame_equipment.count > 0;
	h2h_list.count = 0;
	tag_iterator_new(&iterator, WEAPON_DEFINITION_TAG);
	while ((index = tag_iterator_next(&iterator)) != NONE && h2h_list.count < H2W_MAXIMUM_WEAPONS)
	{
		char const *name = tag_get_name(index);

		if (!name || strncmp(name, "h2\\weapons\\", 11))
			continue;
		for (k = 0; k < (long)(sizeof(h2w_kinds) / sizeof(h2w_kinds[0])); k++)
		{
			if (!strcmp(name + 11, tag_short_name(h2w_kinds[k].h2)))
			{
				long tag = h2_cache_tag_find(cache, 'weap', h2w_kinds[k].h2);

				if (tag != NONE)
				{
					h2h_list.weapons[h2h_list.count].h2_tag = tag;
					snprintf(h2h_list.weapons[h2h_list.count++].name, 64, "%s", name + 11);
				}
				break;
			}
		}
	}
	memset(&sink, 0, sizeof(sink));
	memset(&h2h_texture, 0, sizeof(h2h_texture));
	h2h_texture.proc = texture;
	h2h_texture.context = context;
	h2h_make(build, &sink);
	ok = sink.size >= sizeof(struct h2_hud_file_header) && !sink.failed && h2h_texture.given > 0 && !h2h_texture.failed &&
		h2h_texture.given == build->bitmap_count;
	*image_count = h2h_texture.given;
	if (ok)
	{
		*widgets = sink.data;
		*widgets_size = sink.size;
		sink.data = NULL;
	}
	else
		platform_log("h2 hud: %s: %ld of %ld images made textures, %lu bytes of widgets%s", map_name, h2h_texture.given,
			build->bitmap_count, sink.size, sink.failed ? " (no memory)" : "");
	memset(&h2h_texture, 0, sizeof(h2h_texture));
	h2h_sink_free(&sink);
	for (i = 0; i < build->bitmap_count; i++)
	{
		if (build->bitmaps[i].pixels)
			free(build->bitmaps[i].pixels);
		if (build->bitmaps[i].sidecar_pixels)
			free(build->bitmaps[i].sidecar_pixels);
	}
	if (build->bitmaps)
		free(build->bitmaps);
	free(build);
	h2_cache_close(cache);
	return ok;
}

#else

/* (built alone: nothing - h2_level.c compiles it, through h2_weapons.c) */
typedef int h2_hud_make_compiled_in_h2_level;

#endif
