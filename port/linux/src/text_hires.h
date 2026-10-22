/*
TEXT_HIRES.H

The game's text drawn with fonts at the display's resolution, in place of
the bitmap fonts of the maps (port/assets/fonts, embedded by
tools/embed_assets.py). The game lays its text out as before, from its font
tags' character widths; each character is drawn from a glyph of the font
that stands for its tag (fonts.json), rasterized with stb_truetype at as
many pixels as the display gives the 480 lines, into an atlas that the game
binds as a placeholder bitmap (source/rasterizer/rasterizer_text.c).

A player's own font file can stand in for the built-in ones:
display.high_res_font for every tag (and the overlay's menu faces), and
display.high_res_font_large for the large ones only (role "large" in
fonts.json); a relative path is in data/fonts/ in the data root
(port_folders.h). A file that is missing or cannot be read is logged, and the
built-in font drawn.
*/

#ifndef TEXT_HIRES_H
#define TEXT_HIRES_H

/* an embedded font, and the font tag it draws (by name): its role ("large"
or "small": which of the player's fonts stands in for it) and how wide its
glyphs are drawn (1: as the font has them), from fonts.json */
struct text_hires_embedded
{
	const char *tag;
	const char *file;
	const unsigned int *data;
	unsigned int size;
	const char *role;
	float width;
};

extern const struct text_hires_embedded text_hires_embedded[];
extern const unsigned int text_hires_embedded_count;

/* a glyph: its quad, in units of the 480 lines from the pen on the baseline,
its texels in the atlas, in texels of the atlas's placeholder bitmap, and
the font's advance for it, in units */
struct text_hires_glyph
{
	float left, top, right, bottom;
	float u0, v0, u1, v1;
	float advance;
};

/* the font standing for a font tag, sized so that its capitals are
cap_height units tall, its glyphs rasterized with oversample times the
display's pixels (for text drawn scaled up); -1 if none, or
display.high_res_text is off */
long text_hires_font(char const *tag_name, float cap_height, float oversample);
/* the player's font file for the large role (or the small: every other),
whole, into path: display.high_res_font_large, else display.high_res_font,
a relative one in data/fonts/; 0 when neither is set (the built-in fonts),
or the setting's path is refused (logged) */
int text_hires_font_file(int large, char *path, unsigned long size);
/* whether display.high_res_text is on, as it is now */
int text_hires_enabled(void);
/* whether the font has a glyph for the character (it draws a string only
if it has all of its characters) */
int text_hires_covers(long font, unsigned long code);
/* the glyph of a character (rasterized into the atlas if it is not yet);
0 if it has none */
int text_hires_glyph(long font, unsigned long code, struct text_hires_glyph *glyph);
/* the atlas's placeholder: the D3D texture of the bitmap the game binds,
and its size; NULL forgets it */
void text_hires_register_atlas(const unsigned long *texture, unsigned long width, unsigned long height);

/* the GL texture of the atlas when data (a D3D texture's Data) is its
placeholder's, its rasterized glyphs uploaded; 0 otherwise */
unsigned int text_hires_atlas_texture(unsigned long data);

/* d3d8_gl.c: the display's pixels for each of the 480 lines */
float halo_screen_pixel_scale(void);

#ifdef HALO_CONSOLE_HOST_ART
/* text_hires_atlas_texture for a console's own renderer (the Switch's deko3d
renderer): the atlas's coverage (*size texels square, a byte each) if data
is its placeholder bitmap's, else NULL, and the rows written since the last
call, [*top, *bottom) (empty if *top >= *bottom; all of them if all), which
it then counts as sent */
const unsigned char *text_hires_atlas_rows(unsigned long data, long *size, int all, long *top, long *bottom);
#endif

#endif
