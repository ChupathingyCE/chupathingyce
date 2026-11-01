/*
UI_FONT.H

The overlay's fonts (posix_ui_font.c), for ui_overlay.c: plain types only,
as every header between the game's ABI and the host's (and not named
posix_*: the 64-bit build copies every other header for the game's side).
*/

#ifndef __UI_FONT_H
#define __UI_FONT_H

enum
{
	POSIX_UI_FONT_REGULAR,
	POSIX_UI_FONT_BOLD,
	/* Kenney's Input Prompts: a device's buttons, as glyphs from U+E000 */
	POSIX_UI_FONT_XBOX,
	POSIX_UI_FONT_PLAYSTATION,
	POSIX_UI_FONT_NINTENDO,
	POSIX_UI_FONT_KEYBOARD,
	/* the menus' faces (port/assets/fonts): the fonts fonts.json gives the
	large and small roles, as text_hires.c draws ui\large_ui and
	ui\small_ui, and OpenCE, the titles' */
	POSIX_UI_FONT_MENU,
	POSIX_UI_FONT_MENU_SMALL,
	POSIX_UI_FONT_TITLE,
	/* the scoreboard's platform logos (one-color SVGs, Simple Icons and
	Wikimedia Commons: tools/platform_icons_font.py) */
	POSIX_UI_FONT_PLATFORMS,

	POSIX_UI_FONT_COUNT
};

/* a font's ascent and descent (both positive) at a pixel height; 0 if it
cannot be read */
int posix_ui_font_metrics(int font, float pixel_height, float *ascent, float *descent);
int posix_ui_font_has(int font, unsigned int codepoint);
/* a glyph's advance, with the kerning after the previous (0: none) */
float posix_ui_font_advance(int font, float pixel_height, unsigned int codepoint, unsigned int previous);
/* a glyph's coverage (one byte a pixel), and where it sits from the pen on
the baseline; posix_ui_font_free frees it; NULL for an empty glyph */
unsigned char *posix_ui_font_glyph(int font, float pixel_height, unsigned int codepoint, int *width, int *height,
	int *x_offset, int *y_offset);
void posix_ui_font_free(unsigned char *bitmap);
/* a font drawn from a file (a TrueType or OpenType font, whole path) in
place of its built-in face, or from its built-in face again (NULL or ""):
1 when the file is drawn; 0 when it cannot be read or is no font (the
built-in face is drawn) */
int posix_ui_font_use_file(int font, const char *path);

/* a JPEG picture as RGBA (4 bytes a pixel, rows from the top), NULL if it is
not one or is larger than POSIX_UI_IMAGE_LARGEST either way; freed with
posix_ui_image_free */
#define POSIX_UI_IMAGE_LARGEST 1024
unsigned char *posix_ui_image_decode(const unsigned char *data, int size, int *width, int *height);
void posix_ui_image_free(unsigned char *pixels);

#endif
