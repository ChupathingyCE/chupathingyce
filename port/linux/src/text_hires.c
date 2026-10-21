/*
TEXT_HIRES.C

The game's text drawn with fonts at the display's resolution (text_hires.h).

Each font tag the fonts draw (port/assets/fonts/fonts.json) gets its font
sized so that its capitals are as tall as the tag's: the game's layout,
from the tag's character widths, is kept, and the fonts were chosen to fit
those widths. A glyph is rasterized when first drawn, at the display's
pixels per unit of the 480 lines, into an 8-bit atlas; the GL texture the
game's placeholder bitmap stands for (xbox_textures.c) is the atlas, its new
rows uploaded when the placeholder is bound. A change of the display's
scale (a window made fullscreen) or a full atlas starts it over.

The player's own fonts (display.high_res_font, display.high_res_font_large)
are read from their files when the settings name them, and each font tag's
font is read again, in its place (rasterizer_text.c keeps the fonts' numbers),
when the settings change while the game runs.
*/

#include "text_hires.h"
#include "platform.h"
#include "port_config.h"
#include "port_folders.h"
#include "posix.h"
#include "xgpu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"

#define MAXIMUM_FONTS 8
#define ATLAS_SIZE 2048
/* clear texels around each glyph, so that filtering never reaches the next */
#define ATLAS_PADDING 2
#define GLYPH_CACHE_SIZE 4096
/* a player's font file, at most */
#define MAXIMUM_FONT_FILE_SIZE (32 * 1024 * 1024)

static struct
{
	char tag[64];
	stbtt_fontinfo info;
	/* font units to units of the 480 lines */
	float scale;
	/* how wide its glyphs are drawn (fonts.json) */
	float width;
	/* how many times the display's pixels its glyphs are rasterized with */
	float oversample;
	/* the height of the tag's capitals, which it is sized to */
	float cap_height;
} fonts[MAXIMUM_FONTS];
static long font_count;

/* the player's font files (0 small, 1 large), as the settings were when
last read: their paths ("" for the built-in fonts) and data (NULL when
missing or not a font) */
static struct
{
	char path[1024];
	unsigned char *data;
} player_fonts[2];
static unsigned long player_fonts_read_at = (unsigned long)-1;

static struct
{
	long font;          /* -1: free */
	unsigned long code;
	short x, y, width, height;
	short left, top;    /* the glyph's corner, in pixels from the pen on the baseline */
	float advance;      /* in units of the 480 lines */
} glyphs[GLYPH_CACHE_SIZE];

static unsigned char *atlas;
static float atlas_scale;   /* the pixels per unit its glyphs have */
static int pack_x, pack_y, pack_row_height;
static int dirty_top = ATLAS_SIZE, dirty_bottom;
static unsigned long placeholder_data, placeholder_width, placeholder_height;
static GLuint atlas_texture;

/* tag names compared as the game does, ignoring case (and without the POSIX
strcasecmp, which the Windows build has not) */
static int names_differ(const char *a, const char *b)
{
	while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b))
	{
		a++;
		b++;
	}
	return tolower((unsigned char)*a) != tolower((unsigned char)*b);
}

int text_hires_enabled(void)
{
	static int enabled;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.high_res_text");
	}
	return enabled;
}

/* the atlas emptied: every glyph to be rasterized again */
static void atlas_reset(float scale)
{
	long index;

	if (!atlas)
		atlas = malloc(ATLAS_SIZE * ATLAS_SIZE);
	if (atlas)
		memset(atlas, 0, ATLAS_SIZE * ATLAS_SIZE);
	for (index = 0; index < GLYPH_CACHE_SIZE; index++)
		glyphs[index].font = -1;
	atlas_scale = scale;
	pack_x = pack_y = pack_row_height = 0;
	dirty_top = 0;
	dirty_bottom = ATLAS_SIZE;
}

int text_hires_font_file(int large, char *path, unsigned long size)
{
	const char *value = large && *config_string("display.high_res_font_large") ?
		config_string("display.high_res_font_large") : config_string("display.high_res_font");
	char folder[1024];

	if (size)
		path[0] = '\0';
	if (!*value)
		return 0;
	/* (the folder, made the first time a setting names a file, for the
	player to find) */
	snprintf(folder, sizeof(folder), "%s/fonts", platform_folder(_port_folder_data));
	posix_make_directory(folder);
	if (!port_folder_file(folder, value, path, size))
	{
		platform_log("high-res text: \"%s\" is not a file in %s; drawing the built-in font", value, folder);
		return 0;
	}
	return 1;
}

/* a font file's data whole (NULL, logged, when it cannot be read or is not
a font); free() it */
static unsigned char *read_font_file(const char *path)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data = NULL;
	long size = -1;
	stbtt_fontinfo info;
	int x0, y0, x1, y1;

	if (!file)
	{
		platform_log("high-res text: cannot open %s; drawing the built-in font", path);
		return NULL;
	}
	if (fseek(file, 0, SEEK_END) == 0)
		size = ftell(file);
	if (size <= 0 || size > MAXIMUM_FONT_FILE_SIZE || fseek(file, 0, SEEK_SET) != 0 ||
		!(data = malloc((size_t)size)) || fread(data, 1, (size_t)size, file) != (size_t)size)
	{
		platform_log("high-res text: cannot read %s (empty, or larger than %d MB); drawing the built-in font", path,
			MAXIMUM_FONT_FILE_SIZE / (1024 * 1024));
		free(data);
		fclose(file);
		return NULL;
	}
	fclose(file);
	if (stbtt_GetFontOffsetForIndex(data, 0) < 0 ||
		!stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0)) ||
		!stbtt_GetCodepointBox(&info, 'H', &x0, &y0, &x1, &y1) || y1 <= y0)
	{
		platform_log("high-res text: %s is not a TrueType or OpenType font with an H; drawing the built-in font", path);
		free(data);
		return NULL;
	}
	platform_log("high-res text: read %s", path);
	return data;
}

/* the font tag's built-in font (fonts.json), or NULL */
static const struct text_hires_embedded *embedded_font(const char *tag_name)
{
	unsigned int index;

	for (index = 0; index < text_hires_embedded_count; index++)
	{
		if (text_hires_embedded[index].tag && !names_differ(text_hires_embedded[index].tag, tag_name))
			return &text_hires_embedded[index];
	}
	return NULL;
}

/* the font of a slot read (again), from the player's file for its role or
else its built-in one: its scale 0 when it has none */
static void font_load(long font)
{
	const struct text_hires_embedded *embedded = embedded_font(fonts[font].tag);
	int large, x0, y0, x1, y1;

	fonts[font].scale = 0.0f;
	if (!embedded)
		return;
	large = embedded->role && !strcmp(embedded->role, "large");
	fonts[font].width = embedded->width > 0.0f ? embedded->width : 1.0f;
	if (player_fonts[large].data)
	{
		const unsigned char *data = player_fonts[large].data;

		if (stbtt_InitFont(&fonts[font].info, data, stbtt_GetFontOffsetForIndex(data, 0)) &&
			stbtt_GetCodepointBox(&fonts[font].info, 'H', &x0, &y0, &x1, &y1) && y1 > y0)
		{
			/* (a player's font as it is drawn: its own width) */
			fonts[font].width = 1.0f;
			fonts[font].scale = fonts[font].cap_height / (float)(y1 - y0);
			platform_log("high-res text: %s drawn with %s", fonts[font].tag, player_fonts[large].path);
			return;
		}
	}
	{
		const unsigned char *data = (const unsigned char *)embedded->data;

		if (!stbtt_InitFont(&fonts[font].info, data, stbtt_GetFontOffsetForIndex(data, 0)) ||
			!stbtt_GetCodepointBox(&fonts[font].info, 'H', &x0, &y0, &x1, &y1) || y1 <= y0)
		{
			platform_log("high-res text: could not read %s for %s", embedded->file, fonts[font].tag);
			return;
		}
		fonts[font].scale = fonts[font].cap_height / (float)(y1 - y0);
		platform_log("high-res text: %s drawn with %s", fonts[font].tag, embedded->file);
	}
}

/* the player's fonts read again when their settings changed, and every
font with them (the atlas's glyphs then drawn again) */
static void player_fonts_check(void)
{
	char paths[2][1024];
	int role, changed = 0;
	long font;

	if (player_fonts_read_at == config_changes())
		return;
	player_fonts_read_at = config_changes();
	for (role = 0; role < 2; role++)
	{
		text_hires_font_file(role, paths[role], sizeof(paths[role]));
		if (strcmp(paths[role], player_fonts[role].path))
			changed = 1;
	}
	if (!changed)
		return;
	for (role = 0; role < 2; role++)
	{
		free(player_fonts[role].data);
		player_fonts[role].data = NULL;
		snprintf(player_fonts[role].path, sizeof(player_fonts[role].path), "%s", paths[role]);
		if (paths[role][0])
			player_fonts[role].data = read_font_file(paths[role]);
	}
	for (font = 0; font < font_count; font++)
		font_load(font);
	/* (every glyph rasterized again) */
	atlas_scale = -1.0f;
}

long text_hires_font(char const *tag_name, float cap_height, float oversample)
{
	long font;

	if (!text_hires_enabled() || !tag_name || cap_height <= 0.0f)
		return -1;
	player_fonts_check();
	/* (text is never drawn with fewer pixels than the display's) */
	if (oversample < 1.0f)
		oversample = 1.0f;
	for (font = 0; font < font_count; font++)
	{
		if (!names_differ(fonts[font].tag, tag_name) && fonts[font].oversample == oversample)
			return fonts[font].scale > 0.0f ? font : -1;
	}
	if (font_count >= MAXIMUM_FONTS)
		return -1;
	font = font_count++;
	snprintf(fonts[font].tag, sizeof(fonts[font].tag), "%s", tag_name);
	fonts[font].oversample = oversample;
	fonts[font].cap_height = cap_height;
	font_load(font);
	return fonts[font].scale > 0.0f ? font : -1;
}

int text_hires_covers(long font, unsigned long code)
{
	if (font < 0 || font >= font_count || fonts[font].scale <= 0.0f)
		return 0;
	/* (control characters are never drawn) */
	return code < 32 || code == ' ' || stbtt_FindGlyphIndex(&fonts[font].info, (int)code) != 0;
}

int text_hires_glyph(long font, unsigned long code, struct text_hires_glyph *glyph)
{
	float scale = halo_screen_pixel_scale();
	float pixel_scale;
	unsigned long slot;
	long probe;

	player_fonts_check();
	if (font < 0 || font >= font_count || fonts[font].scale <= 0.0f || !placeholder_data || scale <= 0.0f)
		return 0;
	if (!atlas || scale != atlas_scale)
		atlas_reset(scale);
	if (!atlas)
		return 0;
	slot = ((unsigned long)font * 2654435761UL ^ code * 40503UL) & (GLYPH_CACHE_SIZE - 1);
	for (probe = 0; probe < GLYPH_CACHE_SIZE; probe++, slot = (slot + 1) & (GLYPH_CACHE_SIZE - 1))
	{
		if (glyphs[slot].font == -1 || (glyphs[slot].font == font && glyphs[slot].code == code))
			break;
	}
	if (probe == GLYPH_CACHE_SIZE)
	{
		atlas_reset(scale);
		return text_hires_glyph(font, code, glyph);
	}
	if (glyphs[slot].font == -1)
	{
		/* rasterized now */
		float pixels = fonts[font].scale * scale * fonts[font].oversample;
		int index = stbtt_FindGlyphIndex(&fonts[font].info, (int)code);
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		int width, height, advance = 0, bearing = 0;

		if (index)
		{
			stbtt_GetGlyphBitmapBox(&fonts[font].info, index, pixels * fonts[font].width, pixels, &x0, &y0, &x1, &y1);
			stbtt_GetGlyphHMetrics(&fonts[font].info, index, &advance, &bearing);
		}
		width = x1 - x0;
		height = y1 - y0;
		if (width > 0 && height > 0)
		{
			if (width + 2 * ATLAS_PADDING > ATLAS_SIZE || height + 2 * ATLAS_PADDING > ATLAS_SIZE)
				return 0;
			if (pack_x + width + 2 * ATLAS_PADDING > ATLAS_SIZE)
			{
				pack_x = 0;
				pack_y += pack_row_height;
				pack_row_height = 0;
			}
			if (pack_y + height + 2 * ATLAS_PADDING > ATLAS_SIZE)
			{
				atlas_reset(scale);
				return text_hires_glyph(font, code, glyph);
			}
			stbtt_MakeGlyphBitmap(&fonts[font].info,
				atlas + (pack_y + ATLAS_PADDING) * ATLAS_SIZE + pack_x + ATLAS_PADDING,
				width, height, ATLAS_SIZE, pixels * fonts[font].width, pixels, index);
			if (pack_y < dirty_top)
				dirty_top = pack_y;
			if (pack_y + height + 2 * ATLAS_PADDING > dirty_bottom)
				dirty_bottom = pack_y + height + 2 * ATLAS_PADDING;
		}
		glyphs[slot].font = font;
		glyphs[slot].code = code;
		glyphs[slot].x = (short)(pack_x + ATLAS_PADDING);
		glyphs[slot].y = (short)(pack_y + ATLAS_PADDING);
		glyphs[slot].width = (short)(width > 0 && height > 0 ? width : 0);
		glyphs[slot].height = (short)(width > 0 && height > 0 ? height : 0);
		glyphs[slot].left = (short)x0;
		glyphs[slot].top = (short)y0;
		glyphs[slot].advance = advance * fonts[font].scale * fonts[font].width;
		if (width > 0 && height > 0)
		{
			pack_x += width + 2 * ATLAS_PADDING;
			if (height + 2 * ATLAS_PADDING > pack_row_height)
				pack_row_height = height + 2 * ATLAS_PADDING;
		}
	}
	if (!glyphs[slot].width || !glyphs[slot].height)
		return 0;
	pixel_scale = scale * fonts[font].oversample;
	glyph->left = glyphs[slot].left / pixel_scale;
	glyph->top = glyphs[slot].top / pixel_scale;
	glyph->right = (glyphs[slot].left + glyphs[slot].width) / pixel_scale;
	glyph->bottom = (glyphs[slot].top + glyphs[slot].height) / pixel_scale;
	glyph->advance = glyphs[slot].advance;
	glyph->u0 = (float)glyphs[slot].x * placeholder_width / ATLAS_SIZE;
	glyph->v0 = (float)glyphs[slot].y * placeholder_height / ATLAS_SIZE;
	glyph->u1 = (float)(glyphs[slot].x + glyphs[slot].width) * placeholder_width / ATLAS_SIZE;
	glyph->v1 = (float)(glyphs[slot].y + glyphs[slot].height) * placeholder_height / ATLAS_SIZE;
	return 1;
}

void text_hires_register_atlas(const unsigned long *texture, unsigned long width, unsigned long height)
{
	placeholder_data = texture ? texture[1] : 0;
	placeholder_width = width;
	placeholder_height = height;
}

unsigned int text_hires_atlas_texture(unsigned long data)
{
	if (!data || data != placeholder_data || !atlas)
		return 0;
	if (!atlas_texture)
	{
		glGenTextures(1, &atlas_texture);
		glBindTexture(GL_TEXTURE_2D, atlas_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ATLAS_SIZE, ATLAS_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		xgpu_gl_state_invalidate();
		dirty_top = 0;
		dirty_bottom = ATLAS_SIZE;
	}
	if (dirty_top < dirty_bottom)
	{
		/* (white, the glyph's coverage its alpha, as the maps' fonts are) */
		long rows = dirty_bottom - dirty_top;
		unsigned char *texels = malloc((size_t)rows * ATLAS_SIZE * 4);

		if (texels)
		{
			long index;
			const unsigned char *coverage = atlas + (long)dirty_top * ATLAS_SIZE;

			for (index = 0; index < rows * ATLAS_SIZE; index++)
			{
				texels[index * 4 + 0] = texels[index * 4 + 1] = texels[index * 4 + 2] = 255;
				texels[index * 4 + 3] = coverage[index];
			}
			glBindTexture(GL_TEXTURE_2D, atlas_texture);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, dirty_top, ATLAS_SIZE, (GLsizei)rows, GL_RGBA, GL_UNSIGNED_BYTE, texels);
			xgpu_gl_state_invalidate();
			free(texels);
			dirty_top = ATLAS_SIZE;
			dirty_bottom = 0;
		}
	}
	return atlas_texture;
}

#ifdef HALO_CONSOLE_HOST_ART
const unsigned char *text_hires_atlas_rows(unsigned long data, long *size, int all, long *top, long *bottom)
{
	if (!data || data != placeholder_data || !atlas)
		return NULL;
	*size = ATLAS_SIZE;
	*top = all ? 0 : dirty_top;
	*bottom = all ? ATLAS_SIZE : dirty_bottom;
	dirty_top = ATLAS_SIZE;
	dirty_bottom = 0;
	return atlas;
}
#endif
