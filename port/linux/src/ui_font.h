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

#endif
