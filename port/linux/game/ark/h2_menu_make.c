/*
H2_MENU_MAKE.C

Halo 2's main menu screens, made with its level (a part of h2_level.c,
built alone it is empty): when the map is the main menu's (mainmenu.map),
its screen widgets - the main menu, settings, the campaign's difficulty and
level select (wgit ui\screens\game_shell\...) -, the list skins their
lists name (the shared UI globals' skins, wigl 0x138), the words of their
string lists (MCC: kept in the globals' language pack,
h2_cache_localized_string; the items Halo 2's code lists, h2m_items, and
the campaign levels' names from each level's own map), their
bitmaps (each image a bitmap tag of the level, "h2\menu\<bitmap>#<image>",
A8R8G8B8) and its fonts (the player's MCC h2_fonts files, as font_table.txt
lists them: their glyphs a page each, "h2\menu\font <name>") written to the
level's menu file (t:\h2\<map>.h2menu, h2_menu.h), which h2_menu.c draws.

The layouts (MCC's cache, checked against the H2EK's field names for the
same tags):
- wgit: flags 0 (bit 2 no header), screen ID 4, text colour 8 (argb),
  string list 0x18, header string id 0x2C, panes 0x20 (0x4C each: list blocks 0xC, text blocks 0x1C, bitmap blocks
  0x24);
- a list block (0x18): flags, skin index 4, visible items 6, bottom-left 8;
- a text block (0x2C): flags, animation 4, font 0xA, colour 0xC (argb),
  bounds 0x1C (top, left, bottom, right), string id 0x24, depth bias 0x28;
- a bitmap block (0x38): flags, animation 4, blend method 8, initial frame
  0xA, top-left 0xC, texture wraps a second 0x10, bitmap 0x18, depth bias
  0x20, render scale 0x30;
- skin: arrows 4, up and down arrows' offsets 0xC and 0x10, item
  animations 0x14 (0x14 each: flags, period 4, keyframes 8 - 0x14 each, the
  alpha at 4), text blocks 0x1C, bitmap blocks 0x24;
- unic (MCC): each language's first string and count at 0x10 (4 bytes a
  language, English first);
- a font file (h2_fonts): its header at 0x200 (version, ascent, descent,
  leading), each character's glyph's index at 0x400 (4 bytes, 65536 of
  them), the glyphs at 0x40400 (16 bytes: advance, its pixels' size, width,
  height, origin x and y, the pixels' file offset), the pixels run-length
  coded 16-bit A4R4G4B4 (halo2-upstream-work src/font_loading.cpp, the
  decoder function_122610, and src/unknown_1406b0.cpp).
*/

#ifdef H2_LEVEL_INTERNAL

#include "h2_menu.h"

void h2_level_folder_create(void);

enum
{
	H2M_WGIT_BUTTON_KEY = 0x06,
	H2M_WGIT_TEXT_COLOR = 0x08,
	H2M_WGIT_STRINGS = 0x18,
	H2M_WGIT_PANES = 0x20,
	H2M_WGIT_HEADER = 0x2C,
	H2M_PANE_SIZE = 0x4C,
	/* (a pane's button widgets - the virtual keyboard's keys -: 0x3C each, a
	text block's fields to its bounds, 0x1C, then its bitmap 0x24, its
	bitmap's offset 0x2C, its string id 0x30) */
	H2M_PANE_BUTTONS = 0x04,
	H2M_BUTTON_SIZE = 0x3C,
	H2M_PANE_LISTS = 0x0C,
	H2M_PANE_PLAYERS = 0x44,
	H2M_PLAYERS_SIZE = 0x18,
	H2M_PANE_TEXTS = 0x1C,
	H2M_PANE_BITMAPS = 0x24,
	H2M_LIST_SIZE = 0x18,
	H2M_TEXT_SIZE = 0x2C,
	H2M_BITMAP_SIZE = 0x38,
	H2M_SKIN_ARROWS = 0x04,
	H2M_SKIN_UP_ARROW = 0x0C,
	H2M_SKIN_DOWN_ARROW = 0x10,
	H2M_SKIN_ANIMATIONS = 0x14,
	H2M_WIGL_PROJECTION = 0x64,
	H2M_WIGL_MOTIONS = 0x120,
	H2M_MOTION_SIZE = 0x2C,
	H2M_KEYFRAME_SIZE = 0x14,
	H2M_SKIN_TEXTS = 0x1C,
	H2M_SKIN_BITMAPS = 0x24,
	H2M_ANIMATION_SIZE = 0x14,
	H2M_WIGL_SKINS = 0x138,
	H2M_UNIC_LANGUAGES = 0x10,
	/* (a font file's) */
	H2M_FONT_HEADER = 0x200,
	H2M_FONT_INDEX = 0x400,
	H2M_FONT_GLYPHS = 0x40400,
	H2M_FONT_PAGE_WIDTH = 512,
	H2M_MAXIMUM_IMAGE = 2048,
};

/* (the made bitmaps, once each) */
static struct
{
	long count;
	struct h2m_image { long bitmap_tag; short image; long made; unsigned short width, height, canvas_width, canvas_height; } images[256];
	/* (the art the pregame lobby is drawn with over this engine's lobby,
	on ui.map, where the level's bitmaps are not: kept as made, while
	keep is set, and the fonts' pages, for the art file) */
	boolean keep;
	/* (an image of another map's - a multiplayer map's picture -: made
	afresh, not kept for reuse: its tag index is that map's) */
	boolean fresh;
	/* (the menu made at load, h2_menu_make_loaded: no art kept - the build's
	bitmaps themselves are handed over) */
	boolean at_load;
	/* (made at load for the lobby, h2_menu_make_loaded's lobby: only the
	art file's images - halved as it holds them - made textures, the rest
	let go) */
	boolean lobby;
} h2m;

/* (a menu made at load, h2_menu_make_loaded) each bitmap handed to the
caller as it is made - made a texture there - and its pixels let go here at
once: no image is held past its own texture (feat/h2-direct-hud-menu) */
static struct
{
	h2_menu_texture_proc proc;
	h2_menu_lazy_proc lazy;
	void *context;
	/* (what is made: H2_MENU_MADE_... - the bitmaps' textures; the lobby's art's; none, the level's own bitmaps
	drawn) */
	short what;
	long given, failed;
	unsigned long bytes;
} h2m_texture;

H2_ALIGN static long h2m_hand_over(struct build *build, long made)
{
	struct bitmap *b;

	if (!h2m.at_load || made == NONE)
		return made;
	b = &build->bitmaps[made];
	/* (the lobby: its textures are the art's, h2m_art_add; none: the level's
	own bitmaps are drawn) */
	if (h2m_texture.what != H2_MENU_MADE_TEXTURES &&
		!(h2m_texture.what == H2_MENU_MADE_LAZY && !strncmp(b->path, "h2\\menu\\font ", 13)))
		;
	else if (b->format == 11 && b->pixels && b->pixels_size == (unsigned long)b->width * b->height * 4 &&
		h2m_texture.proc(h2m_texture.context, b->path, b->width, b->height, b->pixels))
	{
		h2m_texture.given++;
		h2m_texture.bytes += b->pixels_size;
	}
	else
		h2m_texture.failed++;
	if (b->pixels)
		free(b->pixels);
	b->pixels = NULL;
	return made;
}

/* an image kept for the art file: its canvas (linear A8R8G8B8), halved
while larger than 1024 (the frame's 2048: it is drawn at most ~1700
pixels across) */
H2_ALIGN static unsigned char *h2m_halved(unsigned char const *canvas, unsigned long width, unsigned long height,
	unsigned long *out_width, unsigned long *out_height)
{
	unsigned char *pixels;
	unsigned long x, y, w = width, h = height, step = 1;

	while (w > 1024 || h > 1024)
		w /= 2, h /= 2, step *= 2;
	if (!w || !h || !(pixels = malloc(w * h * 4)))
		return NULL;
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			unsigned long sum[4] = { 0, 0, 0, 0 }, sx, sy;
			int c;

			for (sy = 0; sy < step; sy++)
				for (sx = 0; sx < step; sx++)
					for (c = 0; c < 4; c++)
						sum[c] += canvas[4 * ((y * step + sy) * width + x * step + sx) + c];
			for (c = 0; c < 4; c++)
				pixels[4 * (y * w + x) + c] = (unsigned char)(sum[c] / (step * step));
		}
	}
	*out_width = w;
	*out_height = h;
	return pixels;
}

H2_ALIGN static void h2m_art_add(char const *path, unsigned char const *canvas, unsigned long width, unsigned long height)
{
	unsigned char *pixels;
	unsigned long w, h;

	if (!h2m.at_load || !h2m.lobby || !(pixels = h2m_halved(canvas, width, height, &w, &h)))
		return;
	/* (the lobby's: a texture now, halved as the art file held it) */
	if (h2m_texture.proc && h2m_texture.proc(h2m_texture.context, path, (unsigned short)w, (unsigned short)h, pixels))
	{
		h2m_texture.given++;
		h2m_texture.bytes += w * h * 4;
	}
	else
		h2m_texture.failed++;
	free(pixels);
}

/* an image of a Halo 2 bitmap on a power-of-two canvas, linear A8R8G8B8
(X8R8G8B8 made opaque), its own size the corner of it: its sizes, and
(canvas not NULL) the canvas, calloc'd here; FALSE if it cannot be made */
H2_ALIGN static boolean h2m_canvas(struct build *build, long bitmap_tag, short image, unsigned long *width, unsigned long *height,
	unsigned long *canvas_width, unsigned long *canvas_height, unsigned char **canvas)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	long count;
	unsigned long images, reference, size, got = 0, w, h, cw, ch, y;
	unsigned char const *header;
	unsigned short format;
	unsigned char *pixels, *made;

	if (bitmap_tag == NONE || !h2_cache_tag_get(cache, bitmap_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x44, IMAGE_SIZE(build), &count, &images) || image < 0 || image >= count ||
		!(header = h2_cache_at(cache, images + IMAGE_SIZE(build) * image, IMAGE_SIZE(build))))
	{
		return FALSE;
	}
	w = (unsigned short)read16(header + 4);
	h = (unsigned short)read16(header + 6);
	format = (unsigned short)read16(header + 0xC);
	reference = read32(header + IMAGE_REFERENCE(build));
	size = read32(header + IMAGE_BYTES(build));
	if (!w || !h || w > H2M_MAXIMUM_IMAGE || h > H2M_MAXIMUM_IMAGE || !(format == 10 || format == 11))
	{
		platform_log("h2 menu: %s: image %d %lux%lu format %d not made", h2_cache_tag_name(cache, bitmap_tag), image, w, h, format);
		return FALSE;
	}
	for (cw = 1; cw < w; cw *= 2)
		;
	for (ch = 1; ch < h; ch *= 2)
		;
	*width = w, *height = h, *canvas_width = cw, *canvas_height = ch;
	if (!canvas)
		return TRUE;
	if (!(pixels = h2_cache_texture_read(cache, reference, size, &got)) || got < w * h * 4)
	{
		free(pixels);
		return FALSE;
	}
	if (!(made = calloc(cw * ch * 4, 1)))
	{
		free(pixels);
		return FALSE;
	}
	for (y = 0; y < h; y++)
		memcpy(made + 4 * (y * cw), pixels + 4 * (y * w), 4 * w);
	/* (a test: HALO_H2_DUMP_BITMAPS=1 writes the image, B8G8R8A8, to
	t:\h2\dump\menu_width_height_name.raw) */
	if (getenv("HALO_H2_DUMP_BITMAPS"))
	{
		char dump[512];
		FILE *file;

		h2_level_folder_create(), CreateDirectoryA(H2_LEVEL_FOLDER "\\dump", NULL);
		snprintf(dump, sizeof(dump), "%s\\dump\\menu_%lu_%lu_%s_%d.raw", H2_LEVEL_FOLDER, w, h,
			tag_short_name(h2_cache_tag_name(cache, bitmap_tag)), image);
		if ((file = fopen(dump, "wb")) != NULL)
		{
			fwrite(pixels, 1, (size_t)w * h * 4, file);
			fclose(file);
		}
	}
	free(pixels);
	/* (X8R8G8B8: opaque) */
	if (format == 10)
	{
		unsigned long k;

		for (k = 0; k < cw * ch; k++)
			made[4 * k + 3] = 255;
	}
	*canvas = made;
	return TRUE;
}

/* an image of a Halo 2 bitmap made a bitmap tag of the level (A8R8G8B8 on
a power-of-two canvas, swizzled; its own size the corner of it): its index
among the build's bitmaps (NONE: not), its tag's name in path */
H2_ALIGN static long h2m_image(struct build *build, long bitmap_tag, short image, char *path, long path_size,
	unsigned short *width, unsigned short *height, unsigned short *canvas_width, unsigned short *canvas_height)
{
	struct h2_cache *cache = build->cache;
	long i, made;
	unsigned long w, h, cw, ch;
	unsigned char *canvas = NULL, *out;
	boolean lazy;

	/* (named by its folder and its own name: the list skins' "arrows" are
	several bitmaps) */
	{
		char const *name = bitmap_tag != NONE ? h2_cache_tag_name(cache, bitmap_tag) : "";
		char const *last = strrchr(name, '\\');
		long end = last ? (long)(last - name) : 0, start = end;

		while (start > 0 && name[start - 1] != '\\')
			start--;
		snprintf(path, (size_t)path_size, "h2\\menu\\%.*s%s%.30s#%d", (int)MIN(16, end - start), name + start,
			end > start ? "_" : "", last ? last + 1 : name, image);
	}
	for (i = 0; i < h2m.count && !h2m.fresh; i++)
	{
		if (h2m.images[i].bitmap_tag == bitmap_tag && h2m.images[i].image == image)
		{
			*width = h2m.images[i].width, *height = h2m.images[i].height;
			*canvas_width = h2m.images[i].canvas_width, *canvas_height = h2m.images[i].canvas_height;
			return h2m.images[i].made;
		}
	}
	if (h2m.count >= (long)NUMBEROF(h2m.images))
		return NONE;
	/* (made at load with no texture of it now - the main menu scene's and
	the lobby's, made when first drawn (h2_menu_image_make); the lobby's not
	kept for its art, not at all -: its place and size alone, no pixels
	read) */
	lazy = h2m.at_load && (h2m_texture.what == H2_MENU_MADE_LAZY || (h2m_texture.what == H2_MENU_MADE_ART && !h2m.fresh));
	if (!h2m_canvas(build, bitmap_tag, image, &w, &h, &cw, &ch, lazy ? NULL : &canvas))
		return NONE;
	if (lazy)
	{
		if (h2m_texture.lazy && (h2m_texture.what == H2_MENU_MADE_LAZY || h2m.keep))
			h2m_texture.lazy(h2m_texture.context, path, h2_cache_name(cache), bitmap_tag, image, h2m_texture.what == H2_MENU_MADE_ART);
		made = bitmap_made(build, path, (unsigned short)cw, (unsigned short)ch, 11, 0, 0, NULL, cw * ch * 4);
		goto recorded;
	}
	if (!(out = malloc(cw * ch * 4)))
	{
		free(canvas);
		return NONE;
	}
	if (h2m.keep)
		h2m_art_add(path, canvas, cw, ch);
	/* (made at load: linear, as the texture takes it - swizzled as the
	level's tags hold it otherwise) */
	if (h2m.at_load)
		memcpy(out, canvas, cw * ch * 4);
	else
		swizzle_into(out, canvas, cw, ch, 4);
	free(canvas);
	made = h2m_hand_over(build, bitmap_made(build, path, (unsigned short)cw, (unsigned short)ch, 11, 0, 0, out, cw * ch * 4));
recorded:
	if (made == NONE)
		return NONE;
	if (h2m.fresh)
	{
		*width = (unsigned short)w, *height = (unsigned short)h;
		*canvas_width = (unsigned short)cw, *canvas_height = (unsigned short)ch;
		return made;
	}
	h2m.images[h2m.count].bitmap_tag = bitmap_tag;
	h2m.images[h2m.count].image = image;
	h2m.images[h2m.count].made = made;
	*width = h2m.images[h2m.count].width = (unsigned short)w;
	*height = h2m.images[h2m.count].height = (unsigned short)h;
	*canvas_width = h2m.images[h2m.count].canvas_width = (unsigned short)cw;
	*canvas_height = h2m.images[h2m.count].canvas_height = (unsigned short)ch;
	h2m.count++;
	return made;
}

/* ---- fonts */

/* (the collection's files are the host's) */
#pragma push_macro("fopen")
#undef fopen
#ifdef __linux__
char *realpath(char const *path, char *resolved);
#endif
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);

/* the player's h2_fonts folder (HALO_H2_FONTS, Halo 2's own folder's, else beside the maps folder
the main menu's map is in - the file it links to, a test's maps folder's
links): FALSE if not found */
H2_ALIGN static boolean h2m_fonts_directory(struct build *build, char *out, long out_size)
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
	/* (Halo 2's own folder's first, d:\\halo2\\h2_fonts, as h2_font.c) */
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
	snprintf(guest, sizeof(guest), "%s%s.map", h2_level_maps_directory(), h2_cache_name(build->cache));
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

/* the font file font_table.txt names for a font (its line, the first 0) */
H2_ALIGN static boolean h2m_font_file(char const *directory, short custom_font, char *name, long name_size)
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

/* a glyph's run-length coded pixels to A4R4G4B4 (count of them; out NULL:
counted only) - halo2-upstream-work's function_122610 */
static unsigned short h2m_font_pixel(long alpha, long color)
{
	return (unsigned short)((((alpha << 1) | (alpha & 1)) << 12) | color);
}

H2_ALIGN static long h2m_font_decode(unsigned char const *source, long size, unsigned short *out, long out_count)
{
	long count = 0, color = 0xFFF;

#define H2M_PUT(v) do { if (count < out_count) out[count] = (unsigned short)(v); count++; } while (0)
	while (size > 0)
	{
		unsigned long code = source[0], type = code >> 6;
		long length = 1, i;

		if (type > 1)
		{
			if (type != 2)
			{
				H2M_PUT(h2m_font_pixel((code >> 3) & 7, color));
				H2M_PUT(h2m_font_pixel(code & 7, color));
			}
			else
			{
				static signed char const runs[8] = { 0, 4, 3, 2, 5, 4, 3, 2 };
				static unsigned char const opaque[8] = { 0, 1, 1, 1, 0, 0, 0, 0 };
				long run = runs[code & 7], pixel = opaque[code & 7] ? (color | 0xF000) : color;

				H2M_PUT(h2m_font_pixel((code >> 3) & 7, color));
				for (i = 0; i < run; i++)
					H2M_PUT(pixel);
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
				H2M_PUT(value);
				length = 3;
			}
			else
			{
				long pixel = h2m_font_pixel(type == 0 ? 0 : 7, color);

				for (i = 0; i < run; i++)
					H2M_PUT(pixel);
			}
		}
		size -= length;
		source += length;
	}
#undef H2M_PUT
	return count;
}

/* a glyph slot's character: 32..127, then the button icons U+E100.., then
Halo 2's medals */
static long h2m_glyph_character(long slot)
{
	return slot < 96 ? H2_MENU_FIRST_GLYPH + slot : slot < H2_MENU_GLYPHS ? 0xE100 + (slot - 96) :
		H2_MENU_FIRST_MEDAL_CHARACTER + (slot - H2_MENU_GLYPHS);
}

static struct h2_menu_file_glyph *h2m_glyph_slot(struct h2_menu_file_font *font, long slot)
{
	return slot < H2_MENU_GLYPHS ? &font->glyphs[slot] : &font->medals[slot - H2_MENU_GLYPHS];
}

/* a font's glyphs (characters 32..127, the button icons; the report's font,
conduit-13, Halo 2's medals) made a page (a bitmap tag of the level) and
kept in font: FALSE if not */
H2_ALIGN static boolean h2m_font_make(struct build *build, char const *directory, short custom_font,
	struct h2_menu_file_font *font)
{
	char name[64], path[1200];
	FILE *file;
	unsigned char *data = NULL;
	long size = 0, c, x = 1, y = 1, row = 0, page_height, slots;
	unsigned char *page, *out;

	memset(font, 0, sizeof(*font));
	if (!h2m_font_file(directory, custom_font, name, sizeof(name)))
		return FALSE;
	snprintf(path, sizeof(path), "%s/%s", directory, name);
	if (!(file = fopen(path, "rb")))
		return FALSE;
	if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > H2M_FONT_GLYPHS && size < 64L * 1024 * 1024 &&
		fseek(file, 0, SEEK_SET) == 0 && (data = malloc((size_t)size)) != NULL &&
		fread(data, 1, (size_t)size, file) != (size_t)size)
	{
		free(data);
		data = NULL;
	}
	fclose(file);
	if (!data)
		return FALSE;
	snprintf(font->name, sizeof(font->name), "%s", name);
	font->custom_font = custom_font;
	font->ascent = read16(data + H2M_FONT_HEADER + 4);
	font->descent = read16(data + H2M_FONT_HEADER + 6);
	font->leading = read16(data + H2M_FONT_HEADER + 8);
	slots = H2_MENU_GLYPHS + (strcmp(name, "conduit-13") ? 0 : H2_MENU_MEDAL_GLYPHS);
	/* (placed on the page in rows: where each goes, then the page's height) */
	for (c = 0; c < slots; c++)
	{
		struct h2_menu_file_glyph *glyph = h2m_glyph_slot(font, c);
		unsigned long index = read32(data + H2M_FONT_INDEX + 4 * h2m_glyph_character(c));
		unsigned char const *g = index < (unsigned long)(size - H2M_FONT_GLYPHS) / 16 ? data + H2M_FONT_GLYPHS + 16 * index : NULL;
		long w = g ? read16(g + 4) : 0, h = g ? read16(g + 6) : 0;

		if (!g || w < 0 || h < 0 || w > 256 || h > 256)
			continue;
		glyph->advance = read16(g);
		glyph->origin_x = (short)read16(g + 8);
		glyph->origin_y = (short)read16(g + 10);
		glyph->width = (unsigned short)w;
		glyph->height = (unsigned short)h;
		if (x + w + 1 > H2M_FONT_PAGE_WIDTH)
		{
			x = 1;
			y += row + 1;
			row = 0;
		}
		glyph->x = (unsigned short)x;
		glyph->y = (unsigned short)y;
		x += w + 1;
		row = h > row ? h : row;
	}
	for (page_height = 1; page_height < y + row + 1; page_height *= 2)
		;
	page = calloc((size_t)H2M_FONT_PAGE_WIDTH * page_height * 4, 1);
	out = malloc((size_t)H2M_FONT_PAGE_WIDTH * page_height * 4);
	if (!page || !out || page_height > 2048)
	{
		free(page);
		free(out);
		free(data);
		return FALSE;
	}
	for (c = 0; c < slots; c++)
	{
		struct h2_menu_file_glyph const *glyph = h2m_glyph_slot(font, c);
		unsigned long index = read32(data + H2M_FONT_INDEX + 4 * h2m_glyph_character(c));
		unsigned char const *g;
		unsigned long pixels_size, offset;
		long count = (long)glyph->width * glyph->height, k;
		unsigned short *texels;

		if (!count || index >= (unsigned long)(size - H2M_FONT_GLYPHS) / 16)
			continue;
		g = data + H2M_FONT_GLYPHS + 16 * index;
		pixels_size = (unsigned short)read16(g + 2);
		offset = read32(g + 12);
		if (offset > (unsigned long)size || pixels_size > (unsigned long)size - offset || !(texels = calloc((size_t)count, 2)))
			continue;
		h2m_font_decode(data + offset, (long)pixels_size, texels, count);
		for (k = 0; k < count; k++)
		{
			unsigned char *p = page + 4 * ((glyph->y + k / glyph->width) * H2M_FONT_PAGE_WIDTH + glyph->x + k % glyph->width);
			unsigned short v = texels[k];

			p[0] = (unsigned char)((v & 0xF) * 17);
			p[1] = (unsigned char)(((v >> 4) & 0xF) * 17);
			p[2] = (unsigned char)(((v >> 8) & 0xF) * 17);
			p[3] = (unsigned char)(((v >> 12) & 0xF) * 17);
		}
		free(texels);
	}
	free(data);
	if (h2m.at_load)
		memcpy(out, page, (size_t)H2M_FONT_PAGE_WIDTH * page_height * 4);
	else
		swizzle_into(out, page, H2M_FONT_PAGE_WIDTH, (unsigned long)page_height, 4);
	snprintf(font->tag, sizeof(font->tag), "h2\\menu\\font %s", name);
	h2m_art_add(font->tag, page, H2M_FONT_PAGE_WIDTH, (unsigned long)page_height);
	free(page);
	font->page_width = H2M_FONT_PAGE_WIDTH;
	font->page_height = (unsigned short)page_height;
	return h2m_hand_over(build, bitmap_made(build, font->tag, H2M_FONT_PAGE_WIDTH, (unsigned short)page_height, 11, 0, 0, out,
		(unsigned long)H2M_FONT_PAGE_WIDTH * page_height * 4)) != NONE;
}

#pragma pop_macro("fopen")

/* the menu file's font for a Halo 2 font (made the first time): its index,
0xFF none */
H2_ALIGN static unsigned char h2m_font(struct build *build, struct h2_menu_file *menu, char const *directory, short custom_font)
{
	long i;

	for (i = 0; i < menu->font_count; i++)
	{
		if (menu->fonts[i].custom_font == custom_font)
			return (unsigned char)i;
	}
	if (!directory || menu->font_count >= H2_MENU_MAXIMUM_FONTS ||
		!h2m_font_make(build, directory, custom_font, &menu->fonts[menu->font_count]))
	{
		return 0xFF;
	}
	platform_log("h2 menu: font %d: %s (ascent %d, descent %d, page %dx%d)", custom_font, menu->fonts[menu->font_count].name,
		menu->fonts[menu->font_count].ascent, menu->fonts[menu->font_count].descent, menu->fonts[menu->font_count].page_width,
		menu->fonts[menu->font_count].page_height);
	return (unsigned char)menu->font_count++;
}

/* ---- words */

/* a string list's English strings: the first's index in the language pack
and their count (MCC) */
H2_ALIGN static void h2m_string_list(struct h2_cache *cache, long unic, long *first, long *count)
{
	struct h2_cache_tag list;
	unsigned char const *p;

	*first = *count = 0;
	if (unic != NONE && h2_cache_tag_get(cache, unic, &list) && list.group_tag == 'unic' &&
		(p = h2_cache_at(cache, list.address + H2M_UNIC_LANGUAGES, 4)) != NULL)
	{
		*first = (unsigned short)read16(p);
		*count = (unsigned short)read16(p + 2);
	}
}

/* the words of a string list's string, by its string id or (id NULL) by
its string id's name: FALSE if it has none */
H2_ALIGN static boolean h2m_words(struct h2_cache *cache, long first, long count, unsigned long string_id, char const *id,
	char *out, long out_size)
{
	long k;

	out[0] = 0;
	for (k = 0; k < count; k++)
	{
		unsigned long found = 0;
		char words[H2_MENU_DESCRIPTION], name[96];

		if (!h2_cache_localized_string(cache, 0, first + k, &found, words, sizeof(words)))
			continue;
		if (id ? (h2_cache_string(cache, found, name, sizeof(name)) && !strcmp(name, id)) : found == string_id)
		{
			snprintf(out, (size_t)out_size, "%s", words);
			return TRUE;
		}
	}
	return FALSE;
}

/* ---- widgets */

H2_ALIGN static void h2m_bitmaps(struct build *build, struct h2_menu_file *menu, unsigned long blocks, long count, unsigned char owner)
{
	struct h2_cache *cache = build->cache;
	long k;

	for (k = 0; k < count && menu->bitmap_count < H2_MENU_MAXIMUM_BITMAPS; k++)
	{
		unsigned char const *b = h2_cache_at(cache, blocks + H2M_BITMAP_SIZE * k, H2M_BITMAP_SIZE);
		struct h2_menu_file_bitmap *bitmap = &menu->bitmaps[menu->bitmap_count];
		long tag;

		if (!b)
			continue;
		memset(bitmap, 0, sizeof(*bitmap));
		tag = h2_cache_reference(cache, blocks + H2M_BITMAP_SIZE * k + 0x18);
		bitmap->owner = owner;
		bitmap->flags = read32(b);
		bitmap->blend = (unsigned char)read16(b + 8);
		bitmap->frame = read16(b + 0xA);
		bitmap->x = read16(b + 0xC);
		bitmap->y = read16(b + 0xE);
		bitmap->wraps[0] = readf(b + 0x10);
		bitmap->wraps[1] = readf(b + 0x14);
		bitmap->bias = read16(b + 0x20);
		bitmap->animation = read16(b + 4);
		bitmap->scale[0] = readf(b + 0x30);
		bitmap->scale[1] = readf(b + 0x34);
		if (tag == NONE || h2m_image(build, tag, bitmap->frame, bitmap->tag, sizeof(bitmap->tag), &bitmap->width, &bitmap->height,
				&bitmap->canvas_width, &bitmap->canvas_height) == NONE)
		{
			bitmap->tag[0] = 0;
		}
		/* (an item's image for the items not focused: its next one - the
		"swap on relative list position" flag, bit 1) */
		if ((owner & H2_MENU_OWNER_SKIN) && (bitmap->flags & 2) && tag != NONE)
		{
			unsigned short w, h, cw, ch;

			if (h2m_image(build, tag, (short)(bitmap->frame + 1), bitmap->tag_other, sizeof(bitmap->tag_other), &w, &h, &cw, &ch) == NONE)
				bitmap->tag_other[0] = 0;
		}
		if (bitmap->tag[0] && getenv("HALO_H2_DEBUG"))
			platform_log("h2 menu: bitmap %d %s frame %d at %d %d, %dx%d of %dx%d, scale %.3f, blend %d, bias %d", owner, bitmap->tag,
				bitmap->frame, bitmap->x, bitmap->y, bitmap->width, bitmap->height, bitmap->canvas_width, bitmap->canvas_height,
				bitmap->scale[0], bitmap->blend, bitmap->bias);
		if (bitmap->tag[0])
			menu->bitmap_count++;
	}
}

/* text blocks: a screen's its string list's words (first, count), the one
whose string id is a description's ("..._description") the focused
item's description; a skin's the item's words */
H2_ALIGN static void h2m_texts(struct build *build, struct h2_menu_file *menu, char const *fonts, unsigned long blocks, long count,
	unsigned char owner, long first, long strings)
{
	struct h2_cache *cache = build->cache;
	long k, j;

	for (k = 0; k < count && menu->text_count < H2_MENU_MAXIMUM_TEXTS; k++)
	{
		unsigned char const *b = h2_cache_at(cache, blocks + H2M_TEXT_SIZE * k, H2M_TEXT_SIZE);
		struct h2_menu_file_text *text = &menu->texts[menu->text_count];
		unsigned long string_id;
		char name[96];

		if (!b)
			continue;
		memset(text, 0, sizeof(*text));
		text->owner = owner;
		text->flags = read32(b);
		text->custom_font = read16(b + 0xA);
		for (j = 0; j < 4; j++)
		{
			text->color[j] = readf(b + 0xC + 4 * j);
			text->bounds[j] = read16(b + 0x1C + 2 * j);
		}
		string_id = read32(b + 0x24);
		text->bias = read16(b + 0x28);
		text->animation = read16(b + 4);
		if (!(owner & H2_MENU_OWNER_SKIN))
		{
			size_t n;

			/* (none: left out - but the campaign pause's, its objectives'
			place, kept empty) */
			/* (the map select's variant tab - its text has no string id,
			Halo 2's code fills it -: the list's "default map" header, the
			tab's word for the game's own variant, the one this menu starts) */
			if (!string_id && owner == H2_MENU_SCREEN_MAP_SELECT)
			{
				if (!h2m_words(cache, first, strings, 0, "mp_base_map_select_default_map_header", text->words, sizeof(text->words)))
					continue;
				text->font = h2m_font(build, menu, fonts, text->custom_font);
				menu->text_count++;
				continue;
			}
			if (!string_id && owner != H2_MENU_SCREEN_SP_PAUSE && owner != H2_MENU_SCREEN_KEYBOARD && owner != H2_MENU_SCREEN_ERROR)
				continue;
			name[0] = 0;
			h2_cache_string(cache, string_id, name, sizeof(name));
			n = strlen(name);
			/* (the focused item's description: "..._description", or a
			dialog's "..._help") */
			text->description = (n > 12 && !strcmp(name + n - 12, "_description")) || (n > 5 && !strcmp(name + n - 5, "_help"));
			/* (the map select's "no maps found" text, the level select's "not
			yet unlocked": the slot under the picture where the focused map's
			or level's description goes) */
			if ((owner == H2_MENU_SCREEN_MAP_SELECT && !strcmp(name, "mp_map_select_no_maps_found")) ||
				(owner == H2_MENU_SCREEN_LEVELS && !strcmp(name, "solo_level_select_unavailable")))
			{
				text->description = TRUE;
			}
			if (string_id && !text->description && !h2m_words(cache, first, strings, string_id, NULL, text->words, sizeof(text->words)))
				continue;
		}
		text->font = h2m_font(build, menu, fonts, text->custom_font);
		if (getenv("HALO_H2_DEBUG") && atoi(getenv("HALO_H2_DEBUG")) >= 2)
		{
			platform_log("h2 menu: owner %d text %ld: flags %lx font %d/%d bounds %d %d %d %d animation %d %s\"%s\"", owner, k, text->flags,
				text->custom_font, text->font, text->bounds[0], text->bounds[1], text->bounds[2], text->bounds[3], text->animation,
				text->description ? "(description) " : "", text->words);
		}
		menu->text_count++;
	}
}

/* the shared UI globals' element animations (wigl 0x120, 0x2C each: flags,
the transition in's period 4 and keyframes 8, the transition out's 0x10
and 0x14; a keyframe 0x14: alpha 4, x 8, y 0xC, z 0x10) - the screens'
texts, bitmaps and lists name them by index - and the
projection plane's distance (wigl 0x64), into the menu */
H2_ALIGN static void h2m_motions(struct build *build, struct h2_menu_file *menu)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag globals;
	unsigned long motions;
	long wigl = NONE, count, k;

	for (k = 0; k < h2_cache_tag_count(cache) && wigl == NONE; k++)
	{
		struct h2_cache_tag c;

		if (h2_cache_tag_get(cache, k, &c) && c.group_tag == 'wigl')
			wigl = k;
	}
	if (wigl == NONE || !h2_cache_tag_get(cache, wigl, &globals))
		return;
	{
		unsigned char const *p = h2_cache_at(cache, globals.address + H2M_WIGL_PROJECTION, 4);

		menu->projection = p ? readf(p) : 0.0f;
	}
	if (!h2_cache_block(cache, globals.address + H2M_WIGL_MOTIONS, H2M_MOTION_SIZE, &count, &motions))
		return;
	for (k = 0; k < count && menu->motion_count < H2_MENU_MAXIMUM_MOTIONS; k++)
	{
		struct h2_menu_file_motion *motion = &menu->motions[menu->motion_count++];
		unsigned long base = motions + H2M_MOTION_SIZE * (unsigned long)k;
		int which;

		memset(motion, 0, sizeof(*motion));
		for (which = 0; which < 2; which++)
		{
			struct h2_menu_file_motion_keys *keys = which ? &motion->out : &motion->in;
			unsigned long at = base + (which ? 0x10 : 4);
			unsigned char const *a = h2_cache_at(cache, at, 4);
			unsigned long keyframes;
			long frames, f;

			if (!a || !h2_cache_block(cache, at + 4, H2M_KEYFRAME_SIZE, &frames, &keyframes) || frames < 1)
				continue;
			keys->period = (long)read32(a);
			for (f = 0; f < frames && f < H2_MENU_MOTION_KEYFRAMES; f++)
			{
				unsigned char const *q = h2_cache_at(cache, keyframes + H2M_KEYFRAME_SIZE * (unsigned long)f, H2M_KEYFRAME_SIZE);

				if (!q)
					break;
				keys->alpha[f] = readf(q + 4);
				keys->x[f] = readf(q + 8);
				keys->y[f] = readf(q + 0xC);
				keys->z[f] = readf(q + 0x10);
				keys->count = (short)(f + 1);
			}
		}
	}
	platform_log("h2 menu: %ld element animations, projection plane %.0f", menu->motion_count, menu->projection);
}

/* a list skin (the shared UI globals' skins, wigl 0x138, by index) into the
menu, once: its index among the file's (NONE: none) */
H2_ALIGN static long h2m_skin_tag(struct build *build, struct h2_menu_file *menu, char const *fonts, long skin);

H2_ALIGN static long h2m_skin(struct build *build, struct h2_menu_file *menu, char const *fonts, short index)
{
	struct h2_cache *cache = build->cache;
	long wigl = NONE, skin = NONE, skin_count, k;
	struct h2_cache_tag globals;
	unsigned long skins;

	for (k = 0; k < h2_cache_tag_count(cache) && wigl == NONE; k++)
	{
		struct h2_cache_tag c;

		if (h2_cache_tag_get(cache, k, &c) && c.group_tag == 'wigl')
			wigl = k;
	}
	if (wigl != NONE && h2_cache_tag_get(cache, wigl, &globals) &&
		h2_cache_block(cache, globals.address + H2M_WIGL_SKINS, 8, &skin_count, &skins) && index >= 0 && index < skin_count)
	{
		skin = h2_cache_reference(cache, skins + 8 * index);
	}
	return h2m_skin_tag(build, menu, fonts, skin);
}

/* a skin - a list's or a player list's - into the menu, once, by its tag:
its index among the file's (NONE: none) */
H2_ALIGN static long h2m_skin_tag(struct build *build, struct h2_menu_file *menu, char const *fonts, long skin)
{
	struct h2_cache *cache = build->cache;
	long count, k, first_text = menu->text_count;
	struct h2_cache_tag skin_tag;
	unsigned long blocks, animations;
	struct h2_menu_file_skin *made;

	if (skin == NONE || !h2_cache_tag_get(cache, skin, &skin_tag) || skin_tag.group_tag != 'skin')
		return NONE;
	for (k = 0; k < menu->skin_count; k++)
	{
		if (!strcmp(menu->skins[k].name, h2_cache_tag_name(cache, skin)))
			return k;
	}
	if (menu->skin_count >= H2_MENU_MAXIMUM_SKINS)
		return NONE;
	made = &menu->skins[menu->skin_count];
	memset(made, 0, sizeof(*made));
	snprintf(made->name, sizeof(made->name), "%s", h2_cache_tag_name(cache, skin));
	if (h2_cache_block(cache, skin_tag.address + H2M_SKIN_BITMAPS, H2M_BITMAP_SIZE, &count, &blocks))
		h2m_bitmaps(build, menu, blocks, count, (unsigned char)(H2_MENU_OWNER_SKIN | menu->skin_count));
	if (h2_cache_block(cache, skin_tag.address + H2M_SKIN_TEXTS, H2M_TEXT_SIZE, &count, &blocks))
		h2m_texts(build, menu, fonts, blocks, count, (unsigned char)(H2_MENU_OWNER_SKIN | menu->skin_count), 0, 0);
	/* (an item's height: its texts' bounds') */
	for (k = first_text; k < menu->text_count; k++)
	{
		short height = (short)abs(menu->texts[k].bounds[0] - menu->texts[k].bounds[2]);

		made->item_height = height > made->item_height ? height : made->item_height;
	}
	/* (its scroll arrows: the arrows bitmap's first image up, its second
	down - or the first again -, at the up and down offsets) */
	{
		long arrows = h2_cache_reference(cache, skin_tag.address + H2M_SKIN_ARROWS);
		int a;

		for (a = 0; a < 2; a++)
		{
			unsigned char const *o = h2_cache_at(cache, skin_tag.address + (a ? H2M_SKIN_DOWN_ARROW : H2M_SKIN_UP_ARROW), 4);
			struct h2_menu_file_bitmap *bitmap = &menu->bitmaps[menu->bitmap_count];

			made->arrows[a] = NONE;
			if (arrows == NONE || !o || menu->bitmap_count >= H2_MENU_MAXIMUM_BITMAPS)
				continue;
			memset(bitmap, 0, sizeof(*bitmap));
			bitmap->owner = H2_MENU_OWNER_NONE;
			bitmap->animation = NONE;
			bitmap->frame = (short)a;
			if (h2m_image(build, arrows, bitmap->frame, bitmap->tag, sizeof(bitmap->tag), &bitmap->width, &bitmap->height,
				&bitmap->canvas_width, &bitmap->canvas_height) == NONE)
			{
				bitmap->frame = 0;
				if (h2m_image(build, arrows, 0, bitmap->tag, sizeof(bitmap->tag), &bitmap->width, &bitmap->height,
					&bitmap->canvas_width, &bitmap->canvas_height) == NONE)
				{
					continue;
				}
			}
			made->arrow_offsets[a][0] = read16(o);
			made->arrow_offsets[a][1] = read16(o + 2);
			made->arrows[a] = (short)menu->bitmap_count++;
		}
	}
	if (h2_cache_block(cache, skin_tag.address + H2M_SKIN_ANIMATIONS, H2M_ANIMATION_SIZE, &count, &animations))
	{
		for (k = 0; k < count && made->animation_count < H2_MENU_MAXIMUM_ANIMATIONS; k++)
		{
			unsigned char const *a = h2_cache_at(cache, animations + H2M_ANIMATION_SIZE * k, H2M_ANIMATION_SIZE);
			long frames;
			unsigned long keyframes;
			struct h2_menu_file_animation *animation = &made->animations[made->animation_count++];

			animation->alpha[0] = animation->alpha[1] = 1.0f;
			if (!a)
				continue;
			animation->period = (long)read32(a + 4);
			if (h2_cache_block(cache, animations + H2M_ANIMATION_SIZE * k + 8, 0x14, &frames, &keyframes) && frames > 0)
			{
				unsigned char const *f0 = h2_cache_at(cache, keyframes + 4, 4);
				unsigned char const *f1 = h2_cache_at(cache, keyframes + 0x14 * (unsigned long)(frames - 1) + 4, 4);

				if (f0)
					animation->alpha[0] = readf(f0);
				if (f1)
					animation->alpha[1] = readf(f1);
			}
		}
	}
	platform_log("h2 menu: list skin %s: item height %d, %d animations", made->name, made->item_height, made->animation_count);
	return menu->skin_count++;
}

/* the items a screen lists (Halo 2's code fills its lists, not its tags):
their string ids' names in its string list, and their descriptions' */
static struct
{
	short screen;
	char const *id;
	char const *description;
} const h2m_items[] =
{
	/* (the PC's main menu: no sign out, a quit) */
	/* (the pauses: Halo 2's items this engine does; B or Start resumes) */
	{ H2_MENU_SCREEN_MP_PAUSE, "settings", NULL },
	{ H2_MENU_SCREEN_MP_PAUSE, "change_teams", NULL },
	{ H2_MENU_SCREEN_MP_PAUSE, "end_game", NULL },
	{ H2_MENU_SCREEN_SP_PAUSE, "continue", NULL },
	{ H2_MENU_SCREEN_SP_PAUSE, "revert_to_last_save", NULL },
	{ H2_MENU_SCREEN_SP_PAUSE, "restart_level", NULL },
	{ H2_MENU_SCREEN_SP_PAUSE, "controller_settings", NULL },
	{ H2_MENU_SCREEN_SP_PAUSE, "save_and_quit", NULL },
	/* (the lobby: its tabs, then the words h2_menu.c's lobby shows) */
	{ H2_MENU_SCREEN_LOBBY, "start_game", NULL },
	{ H2_MENU_SCREEN_LOBBY, "game_setup", NULL },
	{ H2_MENU_SCREEN_LOBBY, "game_start_when_you_get_off_your_ass", NULL },
	{ H2_MENU_SCREEN_LOBBY, "not_squad_leader", NULL },
	{ H2_MENU_SCREEN_LOBBY, "game_start_countdown_in_progress", NULL },
	{ H2_MENU_SCREEN_LOBBY, "game_start_countdown_in_progress_not_leader", NULL },
	{ H2_MENU_SCREEN_LOBBY, "open", NULL },
	{ H2_MENU_SCREEN_LOBBY, "gametype_options_format", NULL },
	{ H2_MENU_SCREEN_LOBBY, "party_leader", NULL },
	{ H2_MENU_SCREEN_LOBBY, "button_key_squad_leader_teams_disabled", NULL },
	{ H2_MENU_SCREEN_LOBBY, "button_key_peon_teams_disabled", NULL },
	{ H2_MENU_SCREEN_LOBBY, "button_key_countdown_in_progress", NULL },
	{ H2_MENU_SCREEN_LOBBY, "button_key_countdown_in_progress_cancel", NULL },
	{ H2_MENU_SCREEN_CAMPAIGN_OPTIONS, "resume_solo_campaign", "resume_solo_help" },
	{ H2_MENU_SCREEN_CAMPAIGN_OPTIONS, "solo", "solo_help" },
	{ H2_MENU_SCREEN_CAMPAIGN_OPTIONS, "restart_solo_campaign", "restart_solo_help" },
	{ H2_MENU_SCREEN_CAMPAIGN_OPTIONS, "coop", "coop_help" },
	{ H2_MENU_SCREEN_MAIN, "campaign", NULL },
	{ H2_MENU_SCREEN_MAIN, "xbox_live", NULL },
	{ H2_MENU_SCREEN_MAIN, "splitscreen", NULL },
	{ H2_MENU_SCREEN_MAIN, "systemlink", NULL },
	{ H2_MENU_SCREEN_MAIN, "settings", NULL },
	{ H2_MENU_SCREEN_MAIN, "quit", NULL },
	{ H2_MENU_SCREEN_SETTINGS, "settings_player_profile", "settings_screen_edit_player_profile_description" },
	{ H2_MENU_SCREEN_SETTINGS, "settings_variant", "settings_screen_edit_game_variant_description" },
	{ H2_MENU_SCREEN_SETTINGS, "settings_playlist", "settings_screen_edit_playlist_description" },
	{ H2_MENU_SCREEN_SETTINGS, "settings_video", "settings_screen_video_description" },
	{ H2_MENU_SCREEN_SETTINGS, "settings_audio", "settings_screen_audio_description" },
	{ H2_MENU_SCREEN_SETTINGS, "settings_network", "settings_screen_network_description" },
	{ H2_MENU_SCREEN_SETTINGS, "settings_about", "settings_screen_about_description" },
	{ H2_MENU_SCREEN_DIFFICULTY, "easy", "easy_description" },
	{ H2_MENU_SCREEN_DIFFICULTY, "normal", "normal_description" },
	{ H2_MENU_SCREEN_DIFFICULTY, "heroic", "heroic_description" },
	{ H2_MENU_SCREEN_DIFFICULTY, "legendary", "legendary_description" },
	{ H2_MENU_SCREEN_PLAYER, "rename_profile", NULL },
	{ H2_MENU_SCREEN_PLAYER, "controller_settings", "controller_settings_description" },
	{ H2_MENU_SCREEN_PLAYER, "keyboard_menu_settings", "keyboard_menu_settings_description" },
	{ H2_MENU_SCREEN_PLAYER, "appearance_settings", "appearance_settings_description" },
	{ H2_MENU_SCREEN_PLAYER, "subtitles_settings", "subtitle_description" },
	{ H2_MENU_SCREEN_CONTROLLER, "look_sensitivity", "look_sensitivity_description" },
	{ H2_MENU_SCREEN_CONTROLLER, "invert_look", "invert_look_description" },
	{ H2_MENU_SCREEN_CONTROLLER, "vibration", "vibration_description" },
	{ H2_MENU_SCREEN_CONTROLLER, "thumbstick_settings", "thumbstick_settings_description" },
	{ H2_MENU_SCREEN_CONTROLLER, "button_settings", "button_settings_description" },
	{ H2_MENU_SCREEN_CONTROLLER, "restore_controller_defaults", NULL },
	{ H2_MENU_SCREEN_LOOK_SENSITIVITY, "look_sensitivity_1", NULL },
	{ H2_MENU_SCREEN_LOOK_SENSITIVITY, "look_sensitivity_2", NULL },
	{ H2_MENU_SCREEN_LOOK_SENSITIVITY, "look_sensitivity_3", NULL },
	{ H2_MENU_SCREEN_LOOK_SENSITIVITY, "look_sensitivity_4", NULL },
	{ H2_MENU_SCREEN_LOOK_SENSITIVITY, "look_sensitivity_5", NULL },
	{ H2_MENU_SCREEN_LOOK_SENSITIVITY, "look_sensitivity_6", NULL },
	{ H2_MENU_SCREEN_LOOK_SENSITIVITY, "look_sensitivity_7", NULL },
	{ H2_MENU_SCREEN_LOOK_SENSITIVITY, "look_sensitivity_8", NULL },
	{ H2_MENU_SCREEN_LOOK_SENSITIVITY, "look_sensitivity_9", NULL },
	{ H2_MENU_SCREEN_LOOK_SENSITIVITY, "look_sensitivity_10", NULL },
	{ H2_MENU_SCREEN_INVERT_LOOK, "enable", NULL },
	{ H2_MENU_SCREEN_INVERT_LOOK, "disable", NULL },
	{ H2_MENU_SCREEN_SUBTITLES, "default", NULL },
	{ H2_MENU_SCREEN_SUBTITLES, "on", NULL },
	{ H2_MENU_SCREEN_SUBTITLES, "off", NULL },
	{ H2_MENU_SCREEN_VIDEO, "resolution", NULL },
	{ H2_MENU_SCREEN_VIDEO, "aspect_ratio", NULL },
	{ H2_MENU_SCREEN_VIDEO, "display_mode", NULL },
	{ H2_MENU_SCREEN_VIDEO, "brightness_level", NULL },
	{ H2_MENU_SCREEN_VIDEO, "gamma_setting", NULL },
	{ H2_MENU_SCREEN_VIDEO, "anti_aliasing", NULL },
	{ H2_MENU_SCREEN_VIDEO, "resize_hud", NULL },
	{ H2_MENU_SCREEN_VIDEO, "level_of_detail", NULL },
	{ H2_MENU_SCREEN_VIDEO, "restore_video_defaults", NULL },
	{ H2_MENU_SCREEN_DISPLAY_MODE, "display_mode_full", NULL },
	{ H2_MENU_SCREEN_DISPLAY_MODE, "display_mode_window", NULL },
	{ H2_MENU_SCREEN_AUDIO, "volume", NULL },
	{ H2_MENU_SCREEN_AUDIO, "speaker_config", NULL },
	{ H2_MENU_SCREEN_AUDIO, "subtitles_settings", NULL },
	{ H2_MENU_SCREEN_AUDIO, "restore_audio_defaults", NULL },
	{ H2_MENU_SCREEN_VOLUME, "volume_0", NULL },
	{ H2_MENU_SCREEN_VOLUME, "volume_1", NULL },
	{ H2_MENU_SCREEN_VOLUME, "volume_2", NULL },
	{ H2_MENU_SCREEN_VOLUME, "volume_3", NULL },
	{ H2_MENU_SCREEN_VOLUME, "volume_4", NULL },
	{ H2_MENU_SCREEN_VOLUME, "volume_5", NULL },
	{ H2_MENU_SCREEN_VOLUME, "volume_6", NULL },
	{ H2_MENU_SCREEN_VOLUME, "volume_7", NULL },
	{ H2_MENU_SCREEN_VOLUME, "volume_8", NULL },
	{ H2_MENU_SCREEN_VOLUME, "volume_9", NULL },
	{ H2_MENU_SCREEN_VOLUME, "volume_10", NULL },
	{ H2_MENU_SCREEN_LIVE, "quickmatch", "quickmatch_help" },
	{ H2_MENU_SCREEN_LIVE, "optimatch", "optimatch_help" },
	{ H2_MENU_SCREEN_LIVE, "custom_game", "arranged_game" },
	{ H2_MENU_SCREEN_LIVE, "view_live_profile", "player_details" },
	{ H2_MENU_SCREEN_LIVE, "clan", "clan_details" },
	{ H2_MENU_SCREEN_GAME_SELECT, "slayer", "mp_game_select_slayer_description" },
	{ H2_MENU_SCREEN_GAME_SELECT, "koth", "mp_game_select_koth_description" },
	{ H2_MENU_SCREEN_GAME_SELECT, "oddball", "mp_game_select_oddball_description" },
	{ H2_MENU_SCREEN_GAME_SELECT, "juggernaut", "mp_game_select_juggernaut_description" },
	{ H2_MENU_SCREEN_GAME_SELECT, "ctf", "mp_game_select_ctf_description" },
	{ H2_MENU_SCREEN_GAME_SELECT, "assault", "mp_game_select_assault_description" },
	{ H2_MENU_SCREEN_GAME_SELECT, "territories", "mp_game_select_territories_description" },
	{ H2_MENU_SCREEN_VIBRATION, "on", NULL },
	{ H2_MENU_SCREEN_VIBRATION, "off", NULL },
	{ H2_MENU_SCREEN_THUMBSTICKS, "default", "default_description" },
	{ H2_MENU_SCREEN_THUMBSTICKS, "southpaw", "southpaw_description" },
	{ H2_MENU_SCREEN_THUMBSTICKS, "legacy", "legacy_description" },
	{ H2_MENU_SCREEN_THUMBSTICKS, "legacy_southpaw", "legacy_southpaw_description" },
	{ H2_MENU_SCREEN_BUTTONS, "default", "standard_help" },
	{ H2_MENU_SCREEN_BUTTONS, "south_paw", "southpaw_help" },
	{ H2_MENU_SCREEN_BUTTONS, "jumpy", "jumpy_help" },
	{ H2_MENU_SCREEN_BUTTONS, "boxer", "boxer_help" },
	{ H2_MENU_SCREEN_BUTTONS, "green_thumb", "greenthumb_help" },
	{ H2_MENU_SCREEN_RESTORE_CONTROLLER, "use_default_controller_settings", NULL },
	{ H2_MENU_SCREEN_RESTORE_CONTROLLER, "use_current_controller_settings", NULL },
	{ H2_MENU_SCREEN_BRIGHTNESS, "brightness_level_1", NULL },
	{ H2_MENU_SCREEN_BRIGHTNESS, "brightness_level_2", NULL },
	{ H2_MENU_SCREEN_BRIGHTNESS, "brightness_level_3", NULL },
	{ H2_MENU_SCREEN_BRIGHTNESS, "brightness_level_4", NULL },
	{ H2_MENU_SCREEN_BRIGHTNESS, "brightness_level_5", NULL },
	{ H2_MENU_SCREEN_BRIGHTNESS, "brightness_level_6", NULL },
	{ H2_MENU_SCREEN_BRIGHTNESS, "brightness_level_7", NULL },
	{ H2_MENU_SCREEN_BRIGHTNESS, "brightness_level_8", NULL },
	{ H2_MENU_SCREEN_BRIGHTNESS, "brightness_level_9", NULL },
	{ H2_MENU_SCREEN_BRIGHTNESS, "brightness_level_10", NULL },
	{ H2_MENU_SCREEN_HUD_SIZE, "resize_hud_large", NULL },
	{ H2_MENU_SCREEN_HUD_SIZE, "resize_hud_medium", NULL },
	{ H2_MENU_SCREEN_HUD_SIZE, "resize_hud_small", NULL },
	{ H2_MENU_SCREEN_RESTORE_VIDEO, "use_default_video_settings", NULL },
	{ H2_MENU_SCREEN_RESTORE_VIDEO, "use_current_video_settings", NULL },
	{ H2_MENU_SCREEN_RESTORE_AUDIO, "use_default_audio_settings", NULL },
	{ H2_MENU_SCREEN_RESTORE_AUDIO, "use_current_audio_settings", NULL },
};

/* the screens' widget tags */
static char const *const h2m_screen_names[H2_MENU_SCREENS] =
{
	"ui\\screens\\game_shell\\main_menu_screen\\main_menu",
	"ui\\screens\\game_shell\\settings_screen\\settings",
	"ui\\screens\\game_shell\\difficulty\\difficulty",
	"ui\\screens\\game_shell\\load_level\\load_level",
	"ui\\screens\\game_shell\\settings_screen\\player_profile\\edit_profile_menu",
	"ui\\screens\\game_shell\\settings_screen\\player_profile\\controller_settings",
	"ui\\screens\\game_shell\\settings_screen\\player_profile\\look_sensitivity_settings",
	"ui\\screens\\game_shell\\settings_screen\\player_profile\\invert_look",
	"ui\\screens\\game_shell\\settings_screen\\video_settings\\video_settings",
	"ui\\screens\\game_shell\\settings_screen\\video_settings\\display_mode",
	"ui\\screens\\game_shell\\settings_screen\\audio_settings\\audio_settings",
	"ui\\screens\\game_shell\\settings_screen\\audio_settings\\volume_settings",
	"ui\\screens\\game_shell\\xbox_live\\xbox_live_main_menu\\xbox_live_main_menu",
	"ui\\screens\\game_shell\\mp_game_select\\game_engine_category_listing_lobby",
	"ui\\screens\\game_shell\\mp_map_select\\mp_map_select_lobby",
	"ui\\screens\\game_shell\\pregame_lobby\\pregame_lobby",
	"ui\\screens\\multiplayer\\mp_menu\\mp_menu",
	"ui\\screens\\singleplayer\\dialog_pause\\dialog_pause",
	"ui\\screens\\game_shell\\campaign_options\\campaign_options_dialog",
	"ui\\screens\\game_shell\\settings_screen\\player_profile\\subtitles",
	"ui\\screens\\game_shell\\virtual_keyboard\\virtual_keyboard",
	"ui\\screens\\misc\\error_dialog_ok",
	"ui\\screens\\game_shell\\live_game_browser\\live_game_browser",
	"ui\\screens\\game_shell\\start_screen\\start_screen",
	"ui\\screens\\game_shell\\settings_screen\\player_profile\\vibration",
	"ui\\screens\\game_shell\\settings_screen\\player_profile\\thumbstick_settings",
	"ui\\screens\\game_shell\\settings_screen\\player_profile\\button_settings",
	"ui\\screens\\game_shell\\settings_screen\\player_profile\\restore_default_controller_settings",
	"ui\\screens\\game_shell\\settings_screen\\video_settings\\brightness_level",
	"ui\\screens\\game_shell\\settings_screen\\video_settings\\resize_hud",
	"ui\\screens\\game_shell\\settings_screen\\video_settings\\restore_video_defaults",
	"ui\\screens\\game_shell\\settings_screen\\audio_settings\\restore_audio_defaults",
	"ui\\screens\\game_shell\\settings_screen\\about\\about_dialog",
};

/* the campaign's levels the maps folder has (the player's own maps, as MCC
names their files): their scenarios' paths, and their names - each
level's description string list's "name" (scenarios\descriptions\...) */
static char const *const h2m_levels[] =
{
	"00a_introduction", "01a_tutorial", "01b_spacestation", "03a_oldmombasa", "03b_newmombasa", "04a_gasgiant",
	"04b_floodlab", "05a_deltaapproach", "05b_deltatowers", "06a_sentinelwalls", "06b_floodzone", "07a_highcharity",
	"08a_deltacliffs", "07b_forerunnership", "08b_deltacontrol",
};

/* (and the multiplayer maps: each its file's name the item's id) */
/* (the maps looked for; listed by their own sort order, h2m_level_items) */
static char const *const h2m_multiplayer_maps[] =
{
	"ascension", "backwash", "beavercreek", "burial_mounds", "coagulation", "colossus", "containment", "cyclotron",
	"deltatap", "derelict", "dune", "elongation", "foundation", "gemini", "headlong", "highplains", "lockout", "midship",
	"needle", "street_sweeper", "triplicate", "turf", "warlock", "waterworks", "zanzibar",
};

/* a multiplayer map's own name and description: its scenario's level data
(scenario +0x398, a block) holds a multiplayer block (+0x10) whose element
has the map's id, its bitmap and then each language's name (wchar[32], English
first, +0x0C) and each language's description (wchar[256], English first,
+0x24C) - the multiplayer maps' description string lists have none */
enum
{
	H2M_SCNR_LEVEL_DATA = 0x398,
	H2M_LEVEL_DATA_SIZE = 0x18,
	H2M_LEVEL_MULTIPLAYER = 0x10,
	H2M_MULTIPLAYER_NAME = 0x0C,
	H2M_MULTIPLAYER_DESCRIPTION = 0x24C,
	/* (after the nine languages' descriptions (wchar[128] each) the map's
	path (char[256]) and its sort order, the map select's (MCC's files:
	Lockout 10, Ascension 20 ... - the Xbox's order; 0 none) */
	H2M_MULTIPLAYER_SORT_ORDER = 0xC4C,
	H2M_MULTIPLAYER_SIZE = H2M_MULTIPLAYER_SORT_ORDER + 4,
};

H2_ALIGN static void h2m_utf16(unsigned char const *p, long characters, char *out, size_t size)
{
	size_t n = 0;
	long k;

	for (k = 0; k < characters && size; k++)
	{
		unsigned c = (unsigned short)read16(p + 2 * k);

		if (!c)
			break;
		if (c < 0x80 && n + 1 < size)
			out[n++] = (char)c;
		else if (c < 0x800 && n + 2 < size)
		{
			out[n++] = (char)(0xC0 | (c >> 6));
			out[n++] = (char)(0x80 | (c & 0x3F));
		}
		else if (c >= 0x800 && n + 3 < size)
		{
			out[n++] = (char)(0xE0 | (c >> 12));
			out[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
			out[n++] = (char)(0x80 | (c & 0x3F));
		}
		else
			break;
	}
	if (size)
		out[n] = 0;
}

H2_ALIGN static boolean h2m_multiplayer_level(struct h2_cache *level, char *name, size_t name_size,
	char *description, size_t description_size, long *picture, long *sort_order)
{
	long scenario = h2_cache_scenario_tag(level), count;
	struct h2_cache_tag t;
	unsigned long data, multiplayer;
	unsigned char const *p;

	if (scenario == NONE || !h2_cache_tag_get(level, scenario, &t) ||
		!h2_cache_block(level, t.address + H2M_SCNR_LEVEL_DATA, H2M_LEVEL_DATA_SIZE, &count, &data) || count < 1 ||
		!h2_cache_block(level, data + H2M_LEVEL_MULTIPLAYER, H2M_MULTIPLAYER_SIZE, &count, &multiplayer) || count < 1 ||
		!(p = h2_cache_at(level, multiplayer, H2M_MULTIPLAYER_SIZE)))
		return FALSE;
	if (picture)
		*picture = h2_cache_reference(level, multiplayer + 4);
	if (sort_order)
		memcpy(sort_order, p + H2M_MULTIPLAYER_SORT_ORDER, 4);
	h2m_utf16(p + H2M_MULTIPLAYER_NAME, 32, name, name_size);
	h2m_utf16(p + H2M_MULTIPLAYER_DESCRIPTION, 256, description, description_size);
	return name[0] != 0;
}

/* a campaign level's map shot: its scenario's level data's campaign
block (level data +8, its elements: campaign 0, map id 4, its bitmap 8, its
names from 0x10) - ui\\global_bitmaps\\map_shots\\<level> (NONE none) */
H2_ALIGN static long h2m_campaign_level_picture(struct h2_cache *level)
{
	long scenario = h2_cache_scenario_tag(level), count;
	struct h2_cache_tag t;
	unsigned long data, campaign;

	if (scenario == NONE || !h2_cache_tag_get(level, scenario, &t) ||
		!h2_cache_block(level, t.address + H2M_SCNR_LEVEL_DATA, H2M_LEVEL_DATA_SIZE, &count, &data) || count < 1 ||
		!h2_cache_block(level, data + 8, 0x10, &count, &campaign) || count < 1)
	{
		return NONE;
	}
	return h2_cache_reference(level, campaign + 8);
}

H2_ALIGN static void h2m_level_items(struct build *build, struct h2_menu_file *menu, short index, struct h2_menu_file_screen *screen,
	boolean multiplayer)
{
	char const *const *names = multiplayer ? h2m_multiplayer_maps : h2m_levels;
	long name_count = multiplayer ? (long)NUMBEROF(h2m_multiplayer_maps) : (long)NUMBEROF(h2m_levels);
	long l, k, first_item = screen->item_count;
	long sort_orders[H2_MENU_MAXIMUM_ITEMS];

	for (l = 0; l < name_count && screen->item_count < H2_MENU_MAXIMUM_ITEMS; l++)
	{
		char why[H2_CACHE_WHY_SIZE];
		struct h2_cache *level;

		{
			char path[700];
			FILE *file;

			/* (a map not in the folder: not opened - quietly) */
			snprintf(path, sizeof(path), "%s%s.map", h2_level_maps_directory(), names[l]);
			if (!(file = fopen(path, "rb")))
				continue;
			fclose(file);
		}
		level = h2_cache_open(h2_level_maps_directory(), names[l], why, sizeof(why));
		char words[H2_MENU_WORDS];

		if (!level)
		{
			if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 menu: level %s: %s", names[l], why);
			continue;
		}
		words[0] = 0;
		{
			long picture = NONE;

			if (multiplayer)
			{
				sort_orders[screen->item_count - first_item] = 0;
				h2m_multiplayer_level(level, words, sizeof(words), screen->descriptions[screen->item_count], H2_MENU_DESCRIPTION, &picture,
					&sort_orders[screen->item_count - first_item]);
			}
			else
				picture = h2m_campaign_level_picture(level);
			/* (its picture - its level data's bitmap, the map's own, a
			campaign level's map shot -: in place of the screen's
			placeholder - the map select's unknown map, the level select's
			load_level -, while it is focused) */
			if (picture != NONE && menu->bitmap_count < H2_MENU_MAXIMUM_BITMAPS)
			{
				long b;

				for (b = 0; b < menu->bitmap_count; b++)
				{
					if (menu->bitmaps[b].owner == index &&
						strstr(menu->bitmaps[b].tag, multiplayer ? "unknown_map" : "load_level_load_level"))
					{
						break;
					}
				}
				if (b < menu->bitmap_count)
				{
					struct h2_menu_file_bitmap *bitmap = &menu->bitmaps[menu->bitmap_count];
					struct h2_cache *saved = build->cache;

					*bitmap = menu->bitmaps[b];
					build->cache = level;
					h2m.fresh = TRUE;
					if (h2m_image(build, picture, 0, bitmap->tag, sizeof(bitmap->tag), &bitmap->width, &bitmap->height,
						&bitmap->canvas_width, &bitmap->canvas_height) != NONE)
					{
						/* (its item's by its id once the items are in order) */
						bitmap->unused = 1;
						snprintf(bitmap->tag_other, sizeof(bitmap->tag_other), "%s", multiplayer ? names[l] : h2_cache_scenario_path(level));
						menu->bitmap_count++;
					}
					h2m.fresh = FALSE;
					build->cache = saved;
				}
			}
		}
		for (k = 0; k < h2_cache_tag_count(level) && !words[0]; k++)
		{
			struct h2_cache_tag t;
			long first, count;

			if (h2_cache_tag_get(level, k, &t) && t.group_tag == 'unic' &&
				!strncmp(h2_cache_tag_name(level, k), "scenarios\\descriptions\\", 23))
			{
				h2m_string_list(level, k, &first, &count);
				h2m_words(level, first, count, 0, "name", words, sizeof(words));
				h2m_words(level, first, count, 0, "description", screen->descriptions[screen->item_count], H2_MENU_DESCRIPTION);
			}
		}
		snprintf(screen->items[screen->item_count], H2_MENU_WORDS, "%s", words[0] ? words : names[l]);
		snprintf(screen->item_ids[screen->item_count], 96, "%s", multiplayer ? names[l] : h2_cache_scenario_path(level));
		screen->item_count++;
		h2_cache_close(level);
	}
	/* (the multiplayer maps in their files' sort order, as Halo 2's map
	select lists them - none (0) last -, not their names' or files') */
	if (multiplayer)
	{
		for (l = first_item + 1; l < screen->item_count; l++)
		{
			for (k = l; k > first_item; k--)
			{
				long a = sort_orders[k - 1 - first_item], b = sort_orders[k - first_item];
				char words[H2_MENU_WORDS], id[96], description[H2_MENU_DESCRIPTION];

				if (!((a <= 0 && b > 0) || (a > 0 && b > 0 && b < a)))
					break;
				sort_orders[k - 1 - first_item] = b, sort_orders[k - first_item] = a;
				memcpy(words, screen->items[k], sizeof(words));
				memcpy(screen->items[k], screen->items[k - 1], sizeof(words));
				memcpy(screen->items[k - 1], words, sizeof(words));
				memcpy(id, screen->item_ids[k], sizeof(id));
				memcpy(screen->item_ids[k], screen->item_ids[k - 1], sizeof(id));
				memcpy(screen->item_ids[k - 1], id, sizeof(id));
				memcpy(description, screen->descriptions[k], sizeof(description));
				memcpy(screen->descriptions[k], screen->descriptions[k - 1], sizeof(description));
				memcpy(screen->descriptions[k - 1], description, sizeof(description));
			}
		}
	}
	/* (the pictures to their items, as they now are) */
	for (l = 0; l < menu->bitmap_count; l++)
	{
		struct h2_menu_file_bitmap *bitmap = &menu->bitmaps[l];

		if (bitmap->owner != index || !bitmap->unused || !bitmap->tag_other[0])
			continue;
		for (k = first_item; k < screen->item_count && strcmp(screen->item_ids[k], bitmap->tag_other); k++)
			;
		bitmap->unused = (short)(k < screen->item_count ? k + 1 : 0);
		bitmap->tag_other[0] = 0;
	}
}

/* a string's button icons (U+E100.. - A, B, X, Y - in UTF-8: EE 84 80..)
made the bytes 0x80..0x83 the menu's fonts keep them as; other characters
past ASCII left out */
H2_ALIGN static void h2m_icons(char *text)
{
	unsigned char *in = (unsigned char *)text, *out = (unsigned char *)text;

	while (*in)
	{
		if (in[0] == 0xEE && in[1] == 0x84 && in[2] >= 0x80 && in[2] <= 0x83)
		{
			*out++ = (unsigned char)(H2_MENU_FIRST_ICON + (in[2] - 0x80));
			in += 3;
		}
		else if (*in < 0x80)
			*out++ = *in++;
		else
			in++;
	}
	*out = 0;
}

/* a screen's button key (its widget's at 6: 0 none, else the button key
types' string that less one - "A SELECT B BACK"...) */
H2_ALIGN static void h2m_buttons(struct h2_cache *cache, unsigned long wgit_address, char *out, long out_size)
{
	long strings = h2_cache_tag_find(cache, 'unic', "ui\\global_strings\\button_key_types"), first = 0, count = 0;
	unsigned char const *p = h2_cache_at(cache, wgit_address + H2M_WGIT_BUTTON_KEY, 2);
	short type = p ? (short)read16(p) : 0;
	unsigned long found = 0;

	out[0] = 0;
	if (strings == NONE || type <= 0)
		return;
	h2m_string_list(cache, strings, &first, &count);
	if (type - 1 < count && h2_cache_localized_string(cache, 0, first + type - 1, &found, out, out_size))
		h2m_icons(out);
}

/* a screen widget's first pane into the menu: its bitmaps and texts, its
list (and the list's skin), its header, its items */
H2_ALIGN static boolean h2m_screen(struct build *build, struct h2_menu_file *menu, char const *fonts, short index)
{
	struct h2_cache *cache = build->cache;
	struct h2_menu_file_screen *screen = &menu->screens[index];
	long wgit = h2_cache_tag_find(cache, 'wgit', h2m_screen_names[index]), count, k, first = 0, strings = 0;
	struct h2_cache_tag t;
	unsigned long panes, blocks;
	unsigned char const *p;

	snprintf(screen->name, sizeof(screen->name), "%s", h2m_screen_names[index]);
	if (wgit == NONE || !h2_cache_tag_get(cache, wgit, &t) ||
		!h2_cache_block(cache, t.address + H2M_WGIT_PANES, H2M_PANE_SIZE, &count, &panes) || count < 1)
	{
		platform_log("h2 menu: no screen %s", h2m_screen_names[index]);
		return FALSE;
	}
	h2m_string_list(cache, h2_cache_reference(cache, t.address + H2M_WGIT_STRINGS), &first, &strings);
	/* (HALO_H2_DEBUG=2: the screen's string list, id by id - what its items
	may be named, for the item table above) */
	if (getenv("HALO_H2_DEBUG") && atoi(getenv("HALO_H2_DEBUG")) >= 2)
	{
		for (k = 0; k < strings; k++)
		{
			unsigned long found = 0;
			char words[H2_MENU_DESCRIPTION], name[96];

			name[0] = 0;
			if (h2_cache_localized_string(cache, 0, first + k, &found, words, sizeof(words)) &&
				h2_cache_string(cache, found, name, sizeof(name)))
			{
				platform_log("h2 menu: %s: string %ld %s = \"%s\"", h2m_screen_names[index], k, name, words);
			}
		}
	}
	if ((p = h2_cache_at(cache, t.address + H2M_WGIT_TEXT_COLOR, 16)) != NULL)
	{
		for (k = 0; k < 4; k++)
			screen->text_color[k] = readf(p + 4 * k);
	}
	/* (its header: unless its flags say none, bit 2) */
	if ((p = h2_cache_at(cache, t.address, 4)) != NULL && !(read32(p) & 4) && (p = h2_cache_at(cache, t.address + H2M_WGIT_HEADER, 4)))
		h2m_words(cache, first, strings, read32(p), NULL, screen->header, sizeof(screen->header));
	h2m_buttons(cache, t.address, screen->buttons, sizeof(screen->buttons));
	/* (its first pane's buttons: words, bounds and bitmap images - each
	image 0 unfocused, 1 focused where the bitmap has it) */
	screen->tab_count = 0;
	/* (not the virtual keyboard's: its keys, below) */
	if (index != H2_MENU_SCREEN_KEYBOARD && h2_cache_block(cache, panes + H2M_PANE_BUTTONS, H2M_BUTTON_SIZE, &count, &blocks))
	{
		for (k = 0; k < count && screen->tab_count < H2_MENU_MAXIMUM_TABS; k++)
		{
			unsigned char const *b = h2_cache_at(cache, blocks + H2M_BUTTON_SIZE * (unsigned long)k, H2M_BUTTON_SIZE);
			struct h2_menu_file_tab *tab = &screen->tabs[screen->tab_count];
			long tag = h2_cache_reference(cache, blocks + H2M_BUTTON_SIZE * (unsigned long)k + 0x24);
			int j, image;

			if (!b)
				continue;
			memset(tab, 0, sizeof(*tab));
			for (j = 0; j < 4; j++)
				tab->bounds[j] = read16(b + 0x1C + 2 * j);
			tab->offset[0] = read16(b + 0x2C);
			tab->offset[1] = read16(b + 0x2E);
			tab->font = fonts ? h2m_font(build, menu, fonts, read16(b + 0xA)) : 0xFF;
			h2m_words(cache, first, strings, read32(b + 0x30), NULL, tab->words, sizeof(tab->words));
			for (image = 0; image < 2; image++)
			{
				struct h2_menu_file_bitmap *bitmap = &menu->bitmaps[menu->bitmap_count];

				tab->bitmap[image] = NONE;
				if (tag == NONE || menu->bitmap_count >= H2_MENU_MAXIMUM_BITMAPS)
					continue;
				memset(bitmap, 0, sizeof(*bitmap));
				bitmap->owner = H2_MENU_OWNER_NONE;
				bitmap->animation = NONE;
				bitmap->frame = (short)image;
				if (h2m_image(build, tag, (short)image, bitmap->tag, sizeof(bitmap->tag), &bitmap->width, &bitmap->height,
					&bitmap->canvas_width, &bitmap->canvas_height) != NONE)
				{
					tab->bitmap[image] = (short)menu->bitmap_count++;
				}
			}
			if (tab->bitmap[1] == NONE)
				tab->bitmap[1] = tab->bitmap[0];
			screen->tab_count++;
		}
	}
	if (h2_cache_block(cache, panes + H2M_PANE_BITMAPS, H2M_BITMAP_SIZE, &count, &blocks))
		h2m_bitmaps(build, menu, blocks, count, (unsigned char)index);
	if (h2_cache_block(cache, panes + H2M_PANE_TEXTS, H2M_TEXT_SIZE, &count, &blocks))
		h2m_texts(build, menu, fonts, blocks, count, (unsigned char)index, first, strings);
	/* (the virtual keyboard's keys: its button widgets) */
	if (index == H2_MENU_SCREEN_KEYBOARD && h2_cache_block(cache, panes + H2M_PANE_BUTTONS, H2M_BUTTON_SIZE, &count, &blocks))
	{
		for (k = 0; k < count && menu->key_count < H2_MENU_MAXIMUM_KEYS; k++)
		{
			unsigned char const *b = h2_cache_at(cache, blocks + H2M_BUTTON_SIZE * k, H2M_BUTTON_SIZE);
			struct h2_menu_file_key *key = &menu->keys[menu->key_count];
			long tag, j;

			if (!b)
				continue;
			memset(key, 0, sizeof(*key));
			for (j = 0; j < 4; j++)
				key->bounds[j] = read16(b + 0x1C + 2 * j);
			h2_cache_string(cache, read32(b + 0x30), key->id, sizeof(key->id));
			if (!key->id[0])
				continue;
			h2m_words(cache, first, strings, read32(b + 0x30), NULL, key->words, sizeof(key->words));
			key->font = h2m_font(build, menu, fonts, read16(b + 0xA));
			key->bitmap = NONE;
			tag = h2_cache_reference(cache, blocks + H2M_BUTTON_SIZE * k + 0x24);
			if (tag != NONE && menu->bitmap_count < H2_MENU_MAXIMUM_BITMAPS)
			{
				struct h2_menu_file_bitmap *bitmap = &menu->bitmaps[menu->bitmap_count];
				unsigned short w, h, cw, ch;

				memset(bitmap, 0, sizeof(*bitmap));
				bitmap->owner = H2_MENU_OWNER_NONE;
				bitmap->x = (short)(key->bounds[1] + read16(b + 0x2C));
				bitmap->y = (short)(key->bounds[0] + read16(b + 0x2E));
				bitmap->animation = NONE;
				if (h2m_image(build, tag, 0, bitmap->tag, sizeof(bitmap->tag), &bitmap->width, &bitmap->height, &bitmap->canvas_width,
					&bitmap->canvas_height) != NONE)
				{
					/* (its focused look: the bitmap's next image, if any) */
					if (h2m_image(build, tag, 1, bitmap->tag_other, sizeof(bitmap->tag_other), &w, &h, &cw, &ch) == NONE)
						bitmap->tag_other[0] = 0;
					key->bitmap = (short)menu->bitmap_count++;
				}
			}
			menu->key_count++;
		}
		platform_log("h2 menu: the virtual keyboard: %ld keys of %ld buttons", menu->key_count, count);
	}
	screen->list_skin = NONE;
	screen->list_animation = NONE;
	screen->player_skin = NONE;
	if (h2_cache_block(cache, panes + H2M_PANE_PLAYERS, H2M_PLAYERS_SIZE, &count, &blocks) && count > 0 &&
		(p = h2_cache_at(cache, blocks, H2M_PLAYERS_SIZE)) != NULL)
	{
		screen->player_skin = (short)h2m_skin_tag(build, menu, fonts, h2_cache_reference(cache, blocks + 4));
		screen->player_x = read16(p + 0xC);
		screen->player_y = read16(p + 0xE);
		screen->player_step = read16(p + 0x14);
		platform_log("h2 menu: %s: its player list skin %d at %d %d, rows %d apart", screen->name, screen->player_skin,
			screen->player_x, screen->player_y, screen->player_step);
	}
	if (h2_cache_block(cache, panes + H2M_PANE_LISTS, H2M_LIST_SIZE, &count, &blocks) && count > 0 &&
		(p = h2_cache_at(cache, blocks, H2M_LIST_SIZE)) != NULL)
	{
		screen->list_flags = read32(p);
		screen->list_visible = read16(p + 6);
		screen->list_x = read16(p + 8);
		screen->list_y = read16(p + 0xA);
		screen->list_animation = read16(p + 0xC);
		screen->list_delay = read16(p + 0xE);
		screen->list_skin = (short)h2m_skin(build, menu, fonts, read16(p + 4));
	}
	if (index == H2_MENU_SCREEN_LEVELS || index == H2_MENU_SCREEN_MAP_SELECT)
		h2m_level_items(build, menu, index, screen, index == H2_MENU_SCREEN_MAP_SELECT);
	for (k = 0; k < (long)NUMBEROF(h2m_items) && screen->item_count < H2_MENU_MAXIMUM_ITEMS; k++)
	{
		if (h2m_items[k].screen != index ||
			!h2m_words(cache, first, strings, 0, h2m_items[k].id, screen->items[screen->item_count], H2_MENU_WORDS))
		{
			continue;
		}
		snprintf(screen->item_ids[screen->item_count], 96, "%s", h2m_items[k].id);
		/* (the lobby's words: their button icons the fonts', the game's
		tokens - a name, a count - left out: h2_menu.c fills them) */
		if (index == H2_MENU_SCREEN_LOBBY)
			h2m_icons(screen->items[screen->item_count]);
		if (h2m_items[k].description)
		{
			h2m_words(cache, first, strings, 0, h2m_items[k].description, screen->descriptions[screen->item_count],
				H2_MENU_DESCRIPTION);
		}
		screen->item_count++;
	}
	/* (HALO_H2_MENU_STRINGS: a screen's string list's names, its items' ids to be) */
	if (getenv("HALO_H2_MENU_STRINGS"))
	{
		long q;

		for (q = 0; q < strings; q++)
		{
			unsigned long id = 0;
			char words[128], name[96];

			if (h2_cache_localized_string(cache, 0, first + q, &id, words, sizeof(words)) && h2_cache_string(cache, id, name, sizeof(name)))
				platform_log("h2 menu: %s string %s: %s", screen->name, name, words);
		}
	}
	platform_log("h2 menu: %s: header \"%s\", %ld items, list at %d %d (%d visible, skin %d)", screen->name, screen->header,
		screen->item_count, screen->list_x, screen->list_y, screen->list_visible, screen->list_skin);
	return TRUE;
}

/* the menu file made: the main menu's map only */
/* whether the build's map is Halo 2's main menu (mainmenu.map) */
H2_ALIGN static boolean h2m_main_menu(struct build *build)
{
	return !strcmp(h2_cache_scenario_path(build->cache), "scenarios\\ui\\mainmenu\\mainmenu");
}

/* the menu made into `menu` from the build's map, its bitmaps the build's
(bitmap_made: "h2\menu\..."): a level's pause screen and the HUD titles'
fonts, or the main menu's screens (the lobby's art among them), for
h2_menu_make_loaded: the menu and its bitmaps in memory, no file */
H2_ALIGN static void h2_menu_make_into(struct build *build, struct h2_menu_file *menu)
{
	struct h2_cache *cache = build->cache;
	char fonts[1024];
	boolean have_fonts, at_load = h2m.at_load, lobby = h2m.lobby;
	long k;

	boolean main_menu = h2m_main_menu(build);

	/* (HALO_H2_HUD_STRINGS: the HUD's messages' strings logged, by id) */
	if (getenv("HALO_H2_HUD_STRINGS"))
	{
		long first, count, q;

		h2m_string_list(cache, h2_cache_tag_find(cache, 'unic', getenv("HALO_H2_HUD_STRINGS")), &first, &count);
		for (q = 0; q < count; q++)
		{
			unsigned long id = 0;
			char words[256], name[96];

			if (h2_cache_localized_string(cache, 0, first + q, &id, words, sizeof(words)) &&
				h2_cache_string(cache, id, name, sizeof(name)))
				platform_log("hud string %s: %s", name, words);
		}
	}

	memset(&h2m, 0, sizeof(h2m));
	h2m.at_load = at_load;
	h2m.lobby = lobby;
	menu->signature = H2_MENU_FILE_SIGNATURE;
	menu->version = H2_MENU_FILE_VERSION;
	have_fonts = h2m_fonts_directory(build, fonts, sizeof(fonts));
	h2m_motions(build, menu);
	if (!have_fonts)
		platform_log("h2 menu: no h2_fonts folder beside the maps (HALO_H2_FONTS names one): no words drawn");
	/* (another level: its pause only - shared.map's screen, as the level's
	tags list it) */
	if (!main_menu)
	{
		short pause = build->multiplayer ? H2_MENU_SCREEN_MP_PAUSE : H2_MENU_SCREEN_SP_PAUSE;

		if (have_fonts)
		{
			h2m_screen(build, menu, fonts, pause);
			/* (the HUD's titles' faces - the location cards' Handel Gothic,
			the chapter titles' Conduit (font table 2 and 9) -, drawn by
			h2_script_text.c through h2_menu_hud_text) */
			h2m_font(build, menu, fonts, 2);
			h2m_font(build, menu, fonts, 9);
		}
		platform_log("h2 menu: the pause %s: %ld items, %ld bitmaps, %ld texts", h2m_screen_names[pause],
			menu->screens[pause].item_count, menu->bitmap_count, menu->text_count);
		return;
	}
	/* (the lobby and the game shell's background first, their images kept
	for the art file as they are made) */
	h2m.keep = TRUE;
	h2m_screen(build, menu, have_fonts ? fonts : NULL, H2_MENU_SCREEN_LOBBY);
	/* (the game shell's background: its frame - the header's bar, the
	panels' edges, the buttons' bar - and its scrolling bands) */
	{
		long wgit = h2_cache_tag_find(cache, 'wgit', "ui\\screens\\game_shell\\main_menu_screen\\game_shell_background");
		struct h2_cache_tag t;
		long count;
		unsigned long panes, blocks;

		if (wgit != NONE && h2_cache_tag_get(cache, wgit, &t) &&
			h2_cache_block(cache, t.address + H2M_WGIT_PANES, H2M_PANE_SIZE, &count, &panes) && count > 0 &&
			h2_cache_block(cache, panes + H2M_PANE_BITMAPS, H2M_BITMAP_SIZE, &count, &blocks))
		{
			h2m_bitmaps(build, menu, blocks, count, H2_MENU_OWNER_BACKGROUND);
		}
	}
	h2m.keep = FALSE;
	for (k = 0; k < H2_MENU_SCREEN_MP_PAUSE; k++)
	{
		if (k != H2_MENU_SCREEN_LOBBY)
			h2m_screen(build, menu, have_fonts ? fonts : NULL, (short)k);
	}
	h2m_screen(build, menu, have_fonts ? fonts : NULL, H2_MENU_SCREEN_CAMPAIGN_OPTIONS);
	h2m_screen(build, menu, have_fonts ? fonts : NULL, H2_MENU_SCREEN_SUBTITLES);
	h2m_screen(build, menu, have_fonts ? fonts : NULL, H2_MENU_SCREEN_KEYBOARD);
	h2m_screen(build, menu, have_fonts ? fonts : NULL, H2_MENU_SCREEN_ERROR);
	h2m_screen(build, menu, have_fonts ? fonts : NULL, H2_MENU_SCREEN_BROWSER);
	h2m_screen(build, menu, have_fonts ? fonts : NULL, H2_MENU_SCREEN_START);
	for (k = H2_MENU_SCREEN_VIBRATION; k <= H2_MENU_SCREEN_ABOUT; k++)
		h2m_screen(build, menu, have_fonts ? fonts : NULL, (short)k);
	/* (its music: the main menu's looping sound) */
	for (k = 0; k < h2_cache_tag_count(cache); k++)
	{
		struct h2_cache_tag c;

		if (h2_cache_tag_get(cache, k, &c) && c.group_tag == 'lsnd' && strstr(h2_cache_tag_name(cache, k), "main_menu_music"))
		{
			snprintf(menu->music, sizeof(menu->music), "%s", h2_cache_tag_name(cache, k));
			break;
		}
	}
	/* (the scene's grade: the desaturate screen effect's shader (the
	effect's at 0x44) - its postprocess definition's (0x20) first pixel
	constant (0x0C, a colour as B, G, R, A bytes) */
	{
		long effect = h2_cache_tag_find(cache, 'egor', "effects\\screen_effects\\desaturate"), shader, count;
		struct h2_cache_tag e, sh;
		unsigned long definitions, constants;
		unsigned char const *p;

		if (effect != NONE && h2_cache_tag_get(cache, effect, &e) &&
			(shader = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, e.address + 0x44))) != NONE &&
			h2_cache_tag_get(cache, shader, &sh) &&
			h2_cache_block(cache, sh.address + 0x20, 0x7C, &count, &definitions) && count > 0 &&
			h2_cache_block(cache, definitions + 0x0C, 4, &count, &constants) && count > 0 &&
			(p = h2_cache_at(cache, constants, 4)) != NULL)
		{
			menu->grade[0] = p[2] / 255.0f;
			menu->grade[1] = p[1] / 255.0f;
			menu->grade[2] = p[0] / 255.0f;
			menu->grade[3] = 1.0f;
			platform_log("h2 menu: the scene graded to %.3f %.3f %.3f (the desaturate screen effect)", menu->grade[0],
				menu->grade[1], menu->grade[2]);
		}
	}
	/* (the screens join by code and OptiMatch would take, if the map has
	them: the Xbox's virtual keyboard, Halo 2's PC game browser) */
	{
		static char const *const wanted[] =
		{
			"ui\\screens\\game_shell\\virtual_keyboard\\virtual_keyboard",
			"ui\\screens\\game_shell\\live_game_browser\\live_game_browser",
		};
		long w;

		for (w = 0; w < (long)NUMBEROF(wanted); w++)
			platform_log("h2 menu: %s %s (%s)", wanted[w], h2_cache_tag_find(cache, 'wgit', wanted[w]) != NONE ? "is there" :
				"is not there", h2_cache_name(cache));
	}
	platform_log("h2 menu: %ld bitmaps (%ld images), %ld texts, %ld fonts, %ld skins", menu->bitmap_count, h2m.count,
		menu->text_count, menu->font_count, menu->skin_count);
}

/* (a level being made - the builder's, and the direct make's) nothing of
the menu: it, its bitmaps and the lobby's art are made from the map when
they are wanted (h2_menu_make_loaded), no menu file, art file or bitmaps in
the level */
H2_ALIGN static void h2_menu_make(struct build *build)
{
	(void)build;
}

/* (h2_menu.c, HALO_H2_MENU_AT_LOAD) a loaded level's menu made from its
MCC map as it is wanted, in memory: the menu (as its .h2menu file would
hold it) into `menu`, and each of its bitmaps - the pause menu's dialog
panes and fonts' pages, or the main menu's screens' (mainmenu.map: the main
menu scene's, and the lobby's) - A8R8G8B8 on a power-of-two canvas, linear
(the texture's own order; the level's tags hold them swizzled) - handed to
`texture` as it is made, its pixels let go once it returns (what:
H2_MENU_MADE_TEXTURES every bitmap's; _LAZY the fonts' - the main menu
scene's, whose screens' images are tens of MB: each image handed to `lazy`,
its map, tag and image, to be made when it is first drawn
(h2_menu_image_make); _ART the lobby's: its fonts', and its art's images -
mainmenu.map's lobby screen and the game shell's background, as the art file
holds them, halved past 1024 - to `lazy` as _LAZY's); FALSE when
anything fails (the textures given so far the caller's to let go), and the
level's own file and bitmaps are used */
H2_ALIGN boolean h2_menu_make_loaded(char const *map_name, short what, struct h2_menu_file *menu, h2_menu_texture_proc texture,
	h2_menu_lazy_proc lazy, void *context, long *image_count)
{
	char const *directory = map_name ? h2_maps_directory_of(map_name) : NULL;
	char why[160];
	struct h2_cache *cache;
	struct build *build;
	long i;
	boolean ok;

	*image_count = 0;
	if (!directory || !h2_level_is_halo2_map(directory, map_name) || !(cache = h2_cache_open(directory, map_name, why, sizeof(why))))
		return FALSE;
	if (!(build = calloc(1, sizeof(*build))))
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
	{
		struct h2_cache_tag scenario;
		long netgame = 0;
		unsigned long items;

		build->multiplayer = h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) &&
			h2_cache_block(cache, scenario.address + 0x120, 0x90, &netgame, &items) && netgame > 0;
	}
	memset(menu, 0, sizeof(*menu));
	memset(&h2m, 0, sizeof(h2m));
	memset(&h2m_texture, 0, sizeof(h2m_texture));
	h2m_texture.proc = texture;
	h2m_texture.lazy = lazy;
	h2m_texture.context = context;
	h2m_texture.what = what;
	h2m.at_load = TRUE;
	h2m.lobby = what == H2_MENU_MADE_ART;
	h2_menu_make_into(build, menu);
	h2m.at_load = FALSE;
	h2m.lobby = FALSE;
	ok = build->bitmap_count > 0 && !h2m_texture.failed && (what == H2_MENU_MADE_TEXTURES ? h2m_texture.given == build->bitmap_count :
		h2m_texture.given > 0);
	*image_count = h2m_texture.given;
	if (ok)
		platform_log("h2 menu: %s: %ld bitmaps, %lu KB, made at load from the MCC map (no file)", map_name, h2m_texture.given,
			h2m_texture.bytes / 1024);
	else
		platform_log("h2 menu: %s: NOT made at load (%ld of %ld bitmaps made textures): the level's own file and bitmaps are used",
			map_name, h2m_texture.given, build->bitmap_count);
	memset(&h2m_texture, 0, sizeof(h2m_texture));
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

/* (h2_menu.c) an MCC map opened for menu images made when first drawn,
on the game thread: its inflated pieces to a budget in the memory window,
let go at the close (as h2_menu_make_loaded's); NULL if not */
H2_ALIGN struct h2_cache *h2_menu_image_cache_open(char const *map_name)
{
	char const *directory = map_name ? h2_maps_directory_of(map_name) : NULL;
	char why[160];
	struct h2_cache *cache;

	if (!directory || !(cache = h2_cache_open(directory, map_name, why, sizeof(why))))
		return NULL;
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	h2_cache_segment_memory(cache, h2_level_window_alloc, h2_level_window_free);
	h2_cache_segment_budget(cache, 2048UL * 1024UL);
#endif
	return cache;
}

H2_ALIGN void h2_menu_image_cache_close(struct h2_cache *cache)
{
	h2_cache_close(cache);
}

/* (h2_menu.c) one menu image made as h2_menu_make_loaded would have made it
- the canvas h2m_image makes; halve: as the lobby's art file holds it - and
handed to texture under path: FALSE if not */
H2_ALIGN boolean h2_menu_image_make(struct h2_cache *cache, long bitmap_tag, short image, boolean halve, char const *path,
	h2_menu_texture_proc texture, void *context)
{
	struct build build;
	unsigned long w, h, cw, ch, hw, hh;
	unsigned char *canvas = NULL, *halved = NULL;
	boolean ok;

	memset(&build, 0, sizeof(build));
	build.cache = cache;
	if (!cache || !h2m_canvas(&build, bitmap_tag, image, &w, &h, &cw, &ch, &canvas))
		return FALSE;
	if (halve && !(halved = h2m_halved(canvas, cw, ch, &hw, &hh)))
	{
		free(canvas);
		return FALSE;
	}
	ok = halved ? texture(context, path, (unsigned short)hw, (unsigned short)hh, halved) :
		texture(context, path, (unsigned short)cw, (unsigned short)ch, canvas);
	if (halved)
		free(halved);
	free(canvas);
	return ok;
}

/* a Halo 2 level's own name: a multiplayer map's level data's, a campaign
level's description's "name" (NULL: not a Halo 2 map of that folder);
the last few kept */
char const *h2_level_title(char const *maps_directory, char const *name)
{
	static struct
	{
		char directory[256], name[64], title[H2_MENU_WORDS];
	} kept[8];
	static int next;
	char why[H2_CACHE_WHY_SIZE], description[H2_MENU_DESCRIPTION];
	struct h2_cache *level;
	int i;
	long k;

	if (!maps_directory || !maps_directory[0] || !name || !name[0])
		return NULL;
	for (i = 0; i < (int)NUMBEROF(kept); i++)
	{
		if (kept[i].name[0] && !strcmp(kept[i].directory, maps_directory) && !strcmp(kept[i].name, name))
			return kept[i].title[0] ? kept[i].title : NULL;
	}
	i = next++ % (int)NUMBEROF(kept);
	snprintf(kept[i].directory, sizeof(kept[i].directory), "%s", maps_directory);
	snprintf(kept[i].name, sizeof(kept[i].name), "%s", name);
	kept[i].title[0] = 0;
	if (!h2_level_is_halo2_map(maps_directory, name) || !(level = h2_cache_open(maps_directory, name, why, sizeof(why))))
		return NULL;
	if (!h2m_multiplayer_level(level, kept[i].title, sizeof(kept[i].title), description, sizeof(description), NULL, NULL))
	{
		for (k = 0; k < h2_cache_tag_count(level) && !kept[i].title[0]; k++)
		{
			struct h2_cache_tag t;
			long first, count;

			if (h2_cache_tag_get(level, k, &t) && t.group_tag == 'unic' &&
				!strncmp(h2_cache_tag_name(level, k), "scenarios\\descriptions\\", 23))
			{
				h2m_string_list(level, k, &first, &count);
				h2m_words(level, first, count, 0, "name", kept[i].title, sizeof(kept[i].title));
			}
		}
	}
	h2_cache_close(level);
	return kept[i].title[0] ? kept[i].title : NULL;
}

/* a Halo 2 campaign level's own description (its scenarios\descriptions
string list's "description"), the first line or two of the story it tells:
TRUE with the text in `out`; the last few kept */
boolean h2_level_description(char const *maps_directory, char const *name, char *out, long out_size)
{
	static struct
	{
		char directory[256], name[64], text[H2_MENU_DESCRIPTION];
	} kept[24];
	static int next;
	char why[H2_CACHE_WHY_SIZE];
	struct h2_cache *level;
	int i;
	long k;

	if (out_size > 0)
		out[0] = 0;
	if (!maps_directory || !maps_directory[0] || !name || !name[0] || out_size <= 0)
		return FALSE;
	for (i = 0; i < (int)NUMBEROF(kept); i++)
	{
		if (kept[i].name[0] && !strcmp(kept[i].directory, maps_directory) && !strcmp(kept[i].name, name))
		{
			snprintf(out, (size_t)out_size, "%s", kept[i].text);
			return kept[i].text[0] != 0;
		}
	}
	i = next++ % (int)NUMBEROF(kept);
	snprintf(kept[i].directory, sizeof(kept[i].directory), "%s", maps_directory);
	snprintf(kept[i].name, sizeof(kept[i].name), "%s", name);
	kept[i].text[0] = 0;
	if (h2_level_is_halo2_map(maps_directory, name) && (level = h2_cache_open(maps_directory, name, why, sizeof(why))) != NULL)
	{
		for (k = 0; k < h2_cache_tag_count(level) && !kept[i].text[0]; k++)
		{
			struct h2_cache_tag t;
			long first, count;

			if (h2_cache_tag_get(level, k, &t) && t.group_tag == 'unic' &&
				!strncmp(h2_cache_tag_name(level, k), "scenarios\\descriptions\\", 23))
			{
				h2m_string_list(level, k, &first, &count);
				h2m_words(level, first, count, 0, "description", kept[i].text, sizeof(kept[i].text));
			}
		}
		h2_cache_close(level);
	}
	snprintf(out, (size_t)out_size, "%s", kept[i].text);
	return kept[i].text[0] != 0;
}

/* ---- a map's picture for the other menus (h2_map_picture.c) */

/* the picture of the maps folder's Halo 2 map `name` - a multiplayer map's
own, its level data's bitmap; a campaign level's map shot -, decoded as
h2m_image decodes the menu's images (formats 10 and 11, B8G8R8A8), the
caller frees; NULL when the map, its picture or a decodable image is not
there, `why` saying which */
H2_ALIGN unsigned char *h2_level_map_picture_read(char const *maps_directory, char const *name, boolean campaign, long *width,
	long *height, char *why, long why_size)
{
	char opened[H2_CACHE_WHY_SIZE];
	struct h2_cache *level = h2_cache_open(maps_directory, name, opened, sizeof(opened));
	struct h2_cache_tag t;
	long picture = NONE, count;
	unsigned long images, reference, size, got = 0, w, h, image_size, image_reference, image_bytes;
	unsigned char const *header;
	unsigned short format;
	unsigned char *pixels;

	*width = *height = 0;
	if (!level)
	{
		snprintf(why, (size_t)why_size, "%s", opened);
		return NULL;
	}
	if (campaign)
		picture = h2m_campaign_level_picture(level);
	else
	{
		char words[H2_MENU_WORDS], description[H2_MENU_DESCRIPTION];
		long sort_order = 0;

		h2m_multiplayer_level(level, words, sizeof(words), description, sizeof(description), &picture, &sort_order);
	}
	image_size = h2_cache_is_mcc(level) ? 0xA8 : 0x74;
	image_reference = h2_cache_is_mcc(level) ? 0x38 : 0x1C;
	image_bytes = h2_cache_is_mcc(level) ? 0x50 : 0x34;
	if (picture == NONE || !h2_cache_tag_get(level, picture, &t) || !h2_cache_block(level, t.address + 0x44, image_size, &count, &images) ||
		count < 1 || !(header = h2_cache_at(level, images, image_size)))
	{
		snprintf(why, (size_t)why_size, "no picture in the map");
		h2_cache_close(level);
		return NULL;
	}
	w = (unsigned short)read16(header + 4);
	h = (unsigned short)read16(header + 6);
	format = (unsigned short)read16(header + 0xC);
	reference = read32(header + image_reference);
	size = read32(header + image_bytes);
	if (!w || !h || w > H2M_MAXIMUM_IMAGE || h > H2M_MAXIMUM_IMAGE || !(format == 10 || format == 11))
	{
		snprintf(why, (size_t)why_size, "%s: image %lux%lu format %d", h2_cache_tag_name(level, picture), w, h, format);
		h2_cache_close(level);
		return NULL;
	}
	pixels = h2_cache_texture_read(level, reference, size, &got);
	h2_cache_close(level);
	if (!pixels || got < w * h * 4)
	{
		free(pixels);
		snprintf(why, (size_t)why_size, "the picture's pixels not read (%lu of %lu bytes)", got, w * h * 4);
		return NULL;
	}
	*width = (long)w;
	*height = (long)h;
	why[0] = 0;
	return pixels;
}

#else

/* (built alone: nothing - h2_level.c compiles it) */
typedef int h2_menu_make_compiled_in_h2_level;

#endif /* H2_LEVEL_INTERNAL */
