/*
H2_FONT.C

The text of a Halo 2 level in Halo 2's own font (h2_font.h).

Every line of in-game text goes through two of the level's font tags: the
HUD's (hud_get_font_index: the pickup and checkpoint messages, kill lines,
the scripts' text, subtitles, the HUD's labels) and the terminal's (the
game engine's lines). Their characters come from the Custom Edition kit the
level is made on. Once a Halo 2 level is loaded, each is remade here from the
player's own h2_fonts file (conduit-13, as the Halo 2 HUD's text is): its
glyphs 32..126 and 160..255 rasterised at the screen's size, in the tag's own
format (a table of 256 character indices, the characters, one byte of alpha a
pixel), pointed to from the font tag, so the engine's text layout, colours,
shadows and justification all go on as they were, with Halo 2's letters. The
memory is let go when the level is.

Layout from Halo 2's HUD in MCC (1080p footage, mcc-h2-campaign-20261025.mkv):
"Checkpoint... done" 272 pixels wide at 1080 high, light blue, 56 pixels from
the left and its middle 190 down.
*/

#include "cseries.h"
#include "h2_font.h"
#include "h2_level.h"
#include "text/font_group.h"
#include "tag_files/tag_groups.h"
#include "render/render.h"
#include "interface/interface.h"
#include "cseries_windows.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HALO_LINUX
#include "render_epoch.h" /* HALO_ENV_ONCE */
#endif

#undef malloc
#undef free
#undef calloc
#undef realloc

#pragma push_macro("fopen")
#undef fopen
#ifdef __linux__
char *realpath(char const *path, char *resolved);
#endif
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);
void platform_log(const char *format, ...);
long hud_get_font_index(void);
long interface_get_tag_index(short interface_tag_index);

#ifndef H2F_SCALE_AT_1080
#define H2F_SCALE_AT_1080 1.27f
#endif

enum
{
	H2F_FIRST_GLYPH = 32,
	H2F_FONT_HEADER = 0x200,
	H2F_FONT_INDEX = 0x400,
	H2F_FONT_GLYPHS = 0x40400,
	H2F_CHARACTER_SIZE = 0x14,
	H2F_MAXIMUM_GLYPHS = 224,
	/* the table of the font file the HUD's text is in (font_table.txt: conduit-13) */
	H2F_HUD_FONT = 4,
	H2F_FONTS = 2,
};

struct h2f_character
{
	word character;
	short character_width, bitmap_width, bitmap_height, bitmap_origin_x, bitmap_origin_y, hardware_character_index;
	word pad;
	long pixels_offset;
};

struct h2f_made
{
	struct font_header *font;
	short *table;
	struct h2f_character *characters;
	unsigned char *pixels;
	real scale;
	void *characters_address;
	long pixels_size;
	short ascent, descent, leading;
	/* (the tag and size a making found no font file for) */
	struct font_header *missing_font;
	real missing_scale;
};

static struct h2f_made h2f_made[H2F_FONTS];
/* (HALO_GPU_STATS on the Vita tier: how many times a font was made, d3d8_gxm.c) */
unsigned long h2_font_remakes, h2_font_repoints, h2_font_missing;

static unsigned long h2f_read32(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned long h2f_read16(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8);
}

static unsigned short h2f_font_pixel(long alpha, long color)
{
	return (unsigned short)((((alpha << 1) | (alpha & 1)) << 12) | color);
}

/* a glyph's run-length coded pixels to A4R4G4B4 (halo2-upstream-work's
function_122610, as h2m_font_decode) */
static long h2f_font_decode(unsigned char const *source, long size, unsigned short *out, long out_count)
{
	long count = 0, color = 0xFFF;

#define H2F_PUT(v) do { if (count < out_count) out[count] = (unsigned short)(v); count++; } while (0)
	while (size > 0)
	{
		unsigned long code = source[0], type = code >> 6;
		long length = 1, i;

		if (type > 1)
		{
			if (type != 2)
			{
				H2F_PUT(h2f_font_pixel((code >> 3) & 7, color));
				H2F_PUT(h2f_font_pixel(code & 7, color));
			}
			else
			{
				static signed char const runs[8] = { 0, 4, 3, 2, 5, 4, 3, 2 };
				static unsigned char const opaque[8] = { 0, 1, 1, 1, 0, 0, 0, 0 };
				long run = runs[code & 7], pixel = opaque[code & 7] ? (color | 0xF000) : color;

				H2F_PUT(h2f_font_pixel((code >> 3) & 7, color));
				for (i = 0; i < run; i++)
					H2F_PUT(pixel);
			}
		}
		else
		{
			long run = (long)(code & 0x3F);

			if (run == 0 && type == 0)
			{
				unsigned short value;

				if (size < 3)
					break;
				value = (unsigned short)((source[1] << 8) | source[2]);
				color = value & 0xFFF;
				H2F_PUT(value);
				length = 3;
			}
			else
			{
				long pixel = h2f_font_pixel(type == 0 ? 0 : 7, color);

				for (i = 0; i < run; i++)
					H2F_PUT(pixel);
			}
		}
		size -= length;
		source += length;
	}
#undef H2F_PUT
	return count;
}

/* the player's h2_fonts folder: HALO_H2_FONTS, Halo 2's own folder's
(d:\\halo2\\h2_fonts), else beside the folder of the maps (as h2_menu_make.c
finds it): FALSE if not there */
static boolean h2f_fonts_directory(char *out, long out_size)
{
	char const *override = getenv("HALO_H2_FONTS");
	char guest[600], host[1024], resolved[1024];
	char *slash;
	FILE *file;

	if (override && *override)
	{
		snprintf(out, (size_t)out_size, "%s", override);
		return TRUE;
	}
	/* (Halo 2's own folder first, where Setup sends the font: d:\halo2\
	h2_fonts; then beside the maps folder that holds Halo 2's maps - a card
	set up before, its maps in maps\, has it beside that if anywhere) */
	host[0] = 0;
	platform_translate_path("d:\\halo2\\h2_fonts", host, sizeof(host));
	if (host[0])
	{
		snprintf(resolved, sizeof(resolved), "%s/font_table.txt", host);
		if ((file = fopen(resolved, "rb")) != NULL)
		{
			fclose(file);
			snprintf(out, (size_t)out_size, "%s", host);
			return TRUE;
		}
		/* (the Vita's insider app: its own folder has no font, the stable app's - where
		Setup sends it - is only read, HALO_READ_FALLBACK) */
		{
			extern int xbox_files_read_fallback_directory(const char *host_directory, char *out, unsigned long out_size);
			char other[1024];

			if (xbox_files_read_fallback_directory(host, other, sizeof(other)))
			{
				snprintf(resolved, sizeof(resolved), "%s/font_table.txt", other);
				if ((file = fopen(resolved, "rb")) != NULL)
				{
					fclose(file);
					snprintf(out, (size_t)out_size, "%s", other);
					return TRUE;
				}
			}
		}
	}
	snprintf(guest, sizeof(guest), "%smainmenu.map", h2_level_maps_directory());
	host[0] = 0;
	platform_translate_path(guest, host, sizeof(host));
	if (!host[0])
		return FALSE;
	snprintf(resolved, sizeof(resolved), "%s", host);
#ifdef __linux__
	{
		char real[1024];

		if (realpath(host, real))
			snprintf(resolved, sizeof(resolved), "%s", real);
	}
#endif
	if (!(slash = strrchr(resolved, '/')) && !(slash = strrchr(resolved, '\\')))
		return FALSE;
	*slash = 0;
	if (!(slash = strrchr(resolved, '/')) && !(slash = strrchr(resolved, '\\')))
		return FALSE;
	*slash = 0;
	snprintf(out, (size_t)out_size, "%s/h2_fonts", resolved);
	snprintf(host, sizeof(host), "%s/font_table.txt", out);
	file = fopen(host, "rb");
	if (!file)
		return FALSE;
	fclose(file);
	return TRUE;
}

/* the font file the table names for a font index */
static boolean h2f_font_file(char const *directory, long custom_font, char *name, long name_size)
{
	char path[1100], line[128];
	FILE *file;
	long k = 0;
	boolean found = FALSE;

	snprintf(path, sizeof(path), "%s/font_table.txt", directory);
	if (!(file = fopen(path, "rb")))
		return FALSE;
	while (fgets(line, sizeof(line), file))
	{
		size_t n = strlen(line);

		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' '))
			line[--n] = 0;
		if (k++ == custom_font && n)
		{
			snprintf(name, (size_t)name_size, "%s", line);
			found = strchr(line, '/') == NULL && strchr(line, '\\') == NULL && strstr(line, "..") == NULL;
			break;
		}
	}
	fclose(file);
	return found;
}


/* (the pixels of an earlier making let go; the characters and the table are kept, one for good
for each font: the text cache points to its characters until it is flushed, and a pointer to
let-go memory would be written) */
static void h2f_let_go(struct h2f_made *made)
{
	free(made->pixels);
	made->pixels = NULL;
	made->font = NULL;
	made->characters_address = NULL;
}

/* the glyph of a character: its index in the font file (0xFFFF: none) */
static long h2f_glyph_char(long k)
{
	return k < 95 ? H2F_FIRST_GLYPH + k : 160 + (k - 95);
}

static boolean h2f_point(struct h2f_made *made, struct font_header *font);

/* font `font` remade from Halo 2's font file at `scale` times its size */
static boolean h2f_make(struct h2f_made *made, struct font_header *font, real scale)
{
	char directory[1024], name[64], path[1200];
	FILE *file;
	unsigned char head[12], record[16];
	unsigned long file_size;
	long k, total = 0, glyphs = 95 + 96;
	struct h2f_character *characters;
	short *table;
	unsigned char *pixels = NULL;
	long capacity = 0;
	short ascent, descent, leading;

	if (!h2f_fonts_directory(directory, sizeof(directory)) || !h2f_font_file(directory, H2F_HUD_FONT, name, sizeof(name)))
		return FALSE;
	snprintf(path, sizeof(path), "%s/%s", directory, name);
	if (!(file = fopen(path, "rb")))
		return FALSE;
	platform_log("h2 font: Halo 2's font file %s", path);
	fseek(file, 0, SEEK_END);
	file_size = (unsigned long)ftell(file);
	if (file_size <= H2F_FONT_GLYPHS || fseek(file, H2F_FONT_HEADER, SEEK_SET) != 0 || fread(head, 1, sizeof(head), file) != sizeof(head))
	{
		fclose(file);
		return FALSE;
	}
	ascent = (short)h2f_read16(head + 4);
	descent = (short)h2f_read16(head + 6);
	leading = (short)h2f_read16(head + 8);
	if (!made->characters)
		made->characters = calloc((size_t)glyphs, sizeof(*characters));
	if (!made->table)
		made->table = calloc(256, sizeof(*table));
	characters = made->characters;
	table = made->table;
	if (!characters || !table)
	{
		fclose(file);
		return FALSE;
	}
	memset(characters, 0, (size_t)glyphs * sizeof(*characters));
	for (k = 0; k < 256; k++)
		table[k] = -1;
	for (k = 0; k < glyphs; k++)
	{
		long c = h2f_glyph_char(k), w, h;
		unsigned char raw[4];
		unsigned long index, offset, size;
		unsigned short *texels;
		unsigned char *source, *alpha, *out;
		long sw, sh, x, y;
		struct h2f_character *ch = &characters[k];

		if (fseek(file, (long)(H2F_FONT_INDEX + 4 * c), SEEK_SET) != 0 || fread(raw, 1, 4, file) != 4)
			continue;
		index = h2f_read32(raw);
		if (H2F_FONT_GLYPHS + 16UL * index + 16 > file_size || fseek(file, (long)(H2F_FONT_GLYPHS + 16UL * index), SEEK_SET) != 0 ||
			fread(record, 1, 16, file) != 16)
		{
			continue;
		}
		w = (long)h2f_read16(record + 4);
		h = (long)h2f_read16(record + 6);
		size = h2f_read16(record + 2);
		offset = h2f_read32(record + 12);
		if (w > 128 || h > 128 || offset + size > file_size)
			continue;
		ch->character = (word)c;
		ch->hardware_character_index = NONE;
		ch->character_width = (short)floor(scale * (real)(short)h2f_read16(record) + 0.5f);
		table[c] = (short)k;
		if (!w || !h || !size)
			continue;
		texels = calloc((size_t)(w * h), 2);
		source = malloc(size);
		if (!texels || !source || fseek(file, (long)offset, SEEK_SET) != 0 || fread(source, 1, size, file) != size)
		{
			free(texels);
			free(source);
			continue;
		}
		h2f_font_decode(source, (long)size, texels, w * h);
		free(source);
		sw = (long)floor(scale * (real)w + 0.5f);
		sh = (long)floor(scale * (real)h + 0.5f);
		sw = sw < 1 ? 1 : sw;
		sh = sh < 1 ? 1 : sh;
		alpha = malloc((size_t)(w * h));
		out = malloc((size_t)(sw * sh));
		if (!alpha || !out)
		{
			free(texels);
			free(alpha);
			free(out);
			continue;
		}
		for (x = 0; x < w * h; x++)
			alpha[x] = (unsigned char)(((texels[x] >> 12) & 0xF) * 17);
		free(texels);
		/* (bilinear, to the glyph's size at this screen's) */
		for (y = 0; y < sh; y++)
		{
			real fy = ((real)y + 0.5f) / scale - 0.5f;
			long y0 = fy < 0 ? 0 : (long)fy, y1 = y0 + 1 < h ? y0 + 1 : y0;
			real ty = fy < 0 ? 0 : fy - (real)y0;

			if (y0 >= h)
				y0 = y1 = h - 1;
			for (x = 0; x < sw; x++)
			{
				real fx = ((real)x + 0.5f) / scale - 0.5f;
				long x0 = fx < 0 ? 0 : (long)fx, x1 = x0 + 1 < w ? x0 + 1 : x0;
				real tx = fx < 0 ? 0 : fx - (real)x0, v;

				if (x0 >= w)
					x0 = x1 = w - 1;
				v = ((real)alpha[y0 * w + x0] * (1 - tx) + (real)alpha[y0 * w + x1] * tx) * (1 - ty) +
					((real)alpha[y1 * w + x0] * (1 - tx) + (real)alpha[y1 * w + x1] * tx) * ty;
				out[y * sw + x] = (unsigned char)(v + 0.5f);
			}
		}
		free(alpha);
		if (total + sw * sh > capacity)
		{
			unsigned char *grown;

			capacity = (total + sw * sh) * 2 + 4096;
			grown = realloc(pixels, (size_t)capacity);
			if (!grown)
			{
				free(out);
				continue;
			}
			pixels = grown;
		}
		memcpy(pixels + total, out, (size_t)(sw * sh));
		free(out);
		ch->bitmap_width = (short)sw;
		ch->bitmap_height = (short)sh;
		ch->bitmap_origin_x = (short)floor(-scale * (real)(short)h2f_read16(record + 8) - 0.5f + 0.5f);
		ch->bitmap_origin_y = (short)floor(scale * (real)(short)h2f_read16(record + 10) + 0.5f);
		ch->pixels_offset = total;
		total += sw * sh;
	}
	fclose(file);
	made->pixels = pixels;
	made->pixels_size = total;
	made->scale = scale;
	made->ascent = (short)floor(scale * (real)ascent + 0.5f);
	made->descent = (short)floor(scale * (real)descent + 0.5f);
	made->leading = (short)floor(scale * (real)leading + 0.5f);
	return h2f_point(made, font);
}

/* font tag `font` pointed at what `made` holds (its characters, table and
pixels, made once): cheap, no file read */
static boolean h2f_point(struct h2f_made *made, struct font_header *font)
{
	long glyphs = 95 + 96;

	made->font = font;
	font->ascending_height = made->ascent;
	font->descending_height = made->descent;
	font->leading_height = made->leading;
	font->leading_width = 0;
	{
		struct font_character_table *tables = (struct font_character_table *)tag_block_get_element_with_size(&font->character_tables, 0, sizeof(struct font_character_table));

		if (font->character_tables.count < 1 || !tables)
			return FALSE;
		tables->character_indices.count = 256;
		tables->character_indices.address = made->table;
	}
	/* (the characters keep this alone: tables 1.. point to none, past 255) */
	{
		long t;

		for (t = 1; t < font->character_tables.count; t++)
		{
			struct font_character_table *extra = (struct font_character_table *)tag_block_get_element_with_size(&font->character_tables, t, sizeof(struct font_character_table));

			extra->character_indices.count = 0;
		}
	}
	made->characters_address = made->characters;
	font->characters.count = glyphs;
	font->characters.address = made->characters;
	font->pixels.size = made->pixels_size;
	font->pixels.address = made->pixels ? made->pixels : (void *)made->characters;
	return TRUE;
}

static real h2f_scale(void)
{
	/* Halo 2's text at the screen's size: its pixels at 1080 high, each a pixel of this screen's 1080 */
	real height = (real)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0);
	real scale = height / 1080.0f * H2F_SCALE_AT_1080;

	/* (in 32nds: a viewport a few pixels taller or shorter is the same
	font, which is made again only when its step changes) */
	return (real)floor(scale * 32.0f + 0.5f) / 32.0f;
}

/* (tests: HALO_H2_FONT_TEST="text" prints that line on the HUD every 3 seconds) */
void hud_print_message(short local_player_index, wchar_t const *message);
static void h2f_test_message(void)
{
	static unsigned long last;
	char const *text = HALO_ENV_ONCE("HALO_H2_FONT_TEST");
	unsigned long now = system_milliseconds();
	wchar_t wide[128];
	long k;

	if (!text || !text[0] || now - last < 3000)
		return;
	last = now;
	for (k = 0; text[k] && k < 127; k++)
		wide[k] = text[k] == '_' ? L' ' : (wchar_t)(unsigned char)text[k];
	wide[k] = 0;
	hud_print_message(0, wide);
}

boolean h2_font_is_halo2(long font_index)
{
	struct font_header *font;
	long k;

	if (font_index == NONE)
		return FALSE;
	font = font_definition_get(font_index);
	for (k = 0; k < H2F_FONTS; k++)
	{
		if (h2f_made[k].characters && h2f_made[k].font == font && font->characters.address == h2f_made[k].characters_address)
			return TRUE;
	}
	return FALSE;
}

void h2_font_ensure(void)
{
	long fonts[H2F_FONTS], k;
	real scale = h2f_scale();

	fonts[0] = hud_get_font_index();
	fonts[1] = interface_get_tag_index(_interface_font_terminal);
	if (HALO_ENV_ONCE("HALO_H2_FONT") && !strcmp(HALO_ENV_ONCE("HALO_H2_FONT"), "0"))
		return;
	for (k = 0; k < H2F_FONTS; k++)
	{
		struct font_header *font;
		boolean same_scale;

		if (fonts[k] == NONE || (k && fonts[k] == fonts[0]))
			continue;
		font = font_definition_get(fonts[k]);
		same_scale = h2f_made[k].characters && fabs((double)(h2f_made[k].scale - scale)) < 0.001;
		/* (already ours, at this size) */
		if (same_scale && h2f_made[k].font == font && font->characters.address == h2f_made[k].characters_address)
			continue;
		/* (made at this size, the tag another's - a new map's, or put back
		as it was: pointed at what is made again, no file read) */
		if (same_scale && h2f_made[k].pixels)
		{
			if (h2f_point(&h2f_made[k], font))
			{
				h2_font_repoints++;
				if (h2_font_repoints <= 4 || !(h2_font_repoints % 1000))
					platform_log("h2 font: tag %ld (%s) pointed at its font again (%s; %lu times)", fonts[k], k ? "terminal" : "HUD",
						h2f_made[k].font != font ? "another tag" : "its characters another's", h2_font_repoints);
				continue;
			}
		}
		/* (tried already for this tag at this size, and Halo 2's font file
		was not there: not tried again until the tag or the size changes -
		the Vita, whose Setup sent no h2_fonts, looked for it every frame,
		20-30 ms of each, Nov 2026) */
		if (h2f_made[k].missing_font == font && fabs((double)(h2f_made[k].missing_scale - scale)) < 0.001)
			continue;
		/* (a new size, or none made yet: made from the font file) */
		if (h2f_made[k].characters)
			platform_log("h2 font: tag %ld (%s) made again: scale %.3f to %.3f", fonts[k], k ? "terminal" : "HUD", h2f_made[k].scale, scale);
		h2f_let_go(&h2f_made[k]);
		if (h2f_make(&h2f_made[k], font, scale))
		{
			h2_font_remakes++;
			platform_log("h2 font: tag %ld (%s) in Halo 2's font at %.2f: ascent %d descent %d", fonts[k], k ? "terminal" : "HUD", scale,
				font->ascending_height, font->descending_height);
		}
		else
		{
			h2f_made[k].missing_font = font;
			h2f_made[k].missing_scale = scale;
			h2_font_missing++;
			platform_log("h2 font: tag %ld not remade (no Halo 2 font file: d:\\halo2\\h2_fonts or h2_fonts beside "
				"the maps folder, font_table.txt and its fonts); not tried again for this map", fonts[k]);
		}
	}
	h2f_test_message();
}

#pragma pop_macro("fopen")
