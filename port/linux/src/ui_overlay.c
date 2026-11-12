/*
UI_OVERLAY.C

The overlay's drawing (ui_overlay.h): shapes and text gathered while the
game draws a frame, drawn at its Present at the window's resolution, over
the game's picture.

Each shape is a quad whose fragment shader works out a rounded rectangle's
edge (filled, or its outline) from the distance to it, so corners and lines
are smooth at any size; text is glyphs (posix_ui_font.c) packed into one
atlas texture as they are first drawn at a size, and drawn as quads too, as
are the controller's button icons (a texture array, a layer each).
*/

#ifdef HALO_GAME_BROWSER

#include "delta.h"
#include "platform.h"
#include "port_config.h"
#include "text_hires.h"
#include "gl.h"
#include "xgpu.h"
#include "ui_font.h"
#include "ui_overlay.h"
#include "halo_prompt_schemes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the button icons' texture: on ES 2, with no texture arrays, one 2D texture
with the icons one above the other (and no smaller levels: its size may not
be a power of two) */
#ifdef HALO_CONSOLE_GLES2
#define ICONS_TARGET GL_TEXTURE_2D
#define ICONS_BINDING GL_TEXTURE_BINDING_2D
#else
#define ICONS_TARGET GL_TEXTURE_2D_ARRAY
#define ICONS_BINDING GL_TEXTURE_BINDING_2D_ARRAY
#endif

/* (xinput_sdl.c: the device the player last used, halo_prompt_schemes.h's:
0 keyboard, 1 Xbox-like, 2 PlayStation, 3 Nintendo, 4 Steam Deck, 5 Wii U,
6 Steam Controller) */
int platform_prompt_scheme(void);
/* (interface/progress_bar.c: the loading screen is up) */
extern unsigned char progress_bar_is_active(void);

/* the controller's button icons (port/assets/prompts, generated with the
fonts by tools/embed_assets.py --fonts): A, B, X, Y, Start, LT, RT, LB, RB,
Back, each ui_prompt_icon_size square, RGBA */
enum
{
	NUMBER_OF_ICONS = 10,
	/* (a quad's radius that marks it as an icon's, or as the picture's) */
	ICON_MARK = -2,
	PICTURE_MARK = -3,
	/* (the plasma's: ui_overlay_plasma) */
	PLASMA_MARK = -4,
	/* (a medal's: ui_overlay_medal) */
	MEDAL_MARK = -5,
	/* the overlay's own noise for the plasma, when the player's ui.map gave
	none: this many texels square, tiling */
	PLASMA_NOISE_SIZE = 128,
};
extern const int ui_prompt_icon_size;
extern const unsigned int *const ui_prompt_icons[NUMBER_OF_ICONS];
#ifdef HALO_MEDAL_ICONS
/* the medals' icons (port/assets/medals, generated with the fonts): one
atlas, RGBA, a cell of ui_medal_atlas_cell pixels each, MEDAL_ICON_COLUMNS
to a row, in medal_icons.h's order */
extern const int ui_medal_atlas_cell, ui_medal_atlas_width, ui_medal_atlas_height;
extern const unsigned int *const ui_medal_atlas;
#endif

enum
{
	LAYOUT_WIDTH = 640,
	LAYOUT_HEIGHT = 480,
	MAXIMUM_QUADS = 16384,
	ATLAS_SIZE = 2048,
	MAXIMUM_GLYPHS = 4096,
	MAXIMUM_TEXT = 64 * 1024,
	MAXIMUM_TEXTS = 4096,
	MAXIMUM_LAYERS = 6,
};

/* a quad as gathered (layout coordinates) */
struct quad
{
	float x, y, width, height;
	float radius, thickness;
	unsigned int top, bottom;
	/* a glyph's place in the atlas (u0 < 0: a shape) */
	float u0, v0, u1, v1;
	/* (ui_overlay_layer's) */
	int layer;
};

struct vertex
{
	float x, y;
	float u, v;
	float local_x, local_y, half_width, half_height;
	float radius, thickness;
	unsigned char color[4];
};

struct glyph
{
	int font;
	int pixel_size;
	unsigned int codepoint;
	/* its place in the atlas, and from the pen on the baseline (pixels) */
	short atlas_x, atlas_y, width, height, x_offset, y_offset;
	float advance;
};

static struct
{
	int ready, failed;
	GLuint program, vertex_array, vertex_buffer, atlas, icons, picture;
#ifdef HALO_CONSOLE_GLES2
	GLint icon_count_location;
	GLint y_flip_location;
#endif
	GLint scale_location, atlas_location, icons_location, picture_location, cutouts_location, window_height_location;
	/* the picture (ui_overlay_picture): the serial and pixels last given,
	and the serial in the texture */
	int picture_serial, picture_width, picture_height, picture_uploaded;
	unsigned char *picture_pixels;
	/* the plasma's noise map (ui_overlay_plasma_source, else the overlay's
	own noise: serial -1), the serial in its texture, and the second map's
	place from the first's this frame (x, y, then its scale), and whether a
	plasma is drawn this frame */
	GLuint plasma;
	GLint plasma_location, plasma_shift_location, plasma_thread_location;
	int plasma_serial, plasma_width, plasma_height, plasma_uploaded;
	unsigned char *plasma_pixels;
	/* (the second map's place, then its threads' color: ui_overlay_plasma_thread) */
	float plasma_shift[8];
	int plasma_drawn;
	/* the medals' atlas (ui_overlay_medal), premultiplied as the icons are,
	made when a medal is first drawn (the GL texture filled from it, then it
	is let go), and whether one is drawn this frame */
	GLuint medals;
	GLint medals_location;
	unsigned char *medal_texels;
	int medals_uploaded, medals_drawn;
	/* each icon's drawn columns (from its left, of ui_prompt_icon_size) */
	int icon_left[NUMBER_OF_ICONS], icon_right[NUMBER_OF_ICONS];
	int icons_measured;

	/* the places the game draws its own pictures into (layout coordinates) */
	float cutouts[4][4];
	int cutout_count;

	struct quad quads[MAXIMUM_QUADS];
	int quad_count;
	/* (text is laid out at Present, when the window's scale is known) */
	struct
	{
		int font, align, layer;
		float size, x, y;
		/* its glyphs' tops leaning right by this much of their height (a
		button's italic letter) */
		float skew;
		unsigned int color;
		int offset;
	} texts[MAXIMUM_TEXTS];
	int layer;
	int text_count;
	char text[MAXIMUM_TEXT];
	int text_used;

	struct glyph glyphs[MAXIMUM_GLYPHS];
	int glyph_count;
	int shelf_x, shelf_y, shelf_height;
	float last_scale;

	struct vertex vertices[MAXIMUM_QUADS * 6];
#ifdef HALO_BGFX
	/* (the bgfx backend: the atlas's texels here, the rows changed since it
	last had them, and the icons premultiplied; d3d8_bgfx.inl draws) */
	unsigned char *atlas_texels, *icon_texels;
	int dirty_top, dirty_bottom;
#endif
} overlay;

/* ---------- the device's buttons */

static const unsigned int button_glyphs[NUMBER_OF_PROMPT_SCHEMES][NUMBER_OF_UI_BUTTONS] =
{
	/* keyboard: Enter, Backspace, E, Tab, F2, Q, X, Q, C (the game's keys
	for them, xinput_sdl.c; C is Online Games' Link Profile, F2 its My
	Profile: escape is B), Left, Right, F1 */
	{ 0xE05E, 0xE038, 0xE05A, 0xE0D1, 0xE06F, 0xE0B3, 0xE0E3, 0xE0B3, 0xE046, 0xE020, 0xE022, 0xE067 },
	/* Xbox: A, B, X, Y, menu, LT, RT, LB, RB, -, -, view */
	{ 0xE004, 0xE006, 0xE01E, 0xE020, 0xE014, 0xE047, 0xE04D, 0xE043, 0xE049, 0, 0, 0xE01C },
	/* PlayStation: cross, circle, square, triangle, options, L2, R2, L1, R1,
	-, -, share */
	{ 0xE04B, 0xE041, 0xE051, 0xE053, 0xE009, 0xE07D, 0xE085, 0xE078, 0xE080, 0, 0, 0xE00B },
	/* Switch (by position, as SDL gives its buttons: the south is its B,
	the east its A, the west its Y, the north its X): B, A, Y, X, plus, ZL,
	ZR, L, R, d-pad left and right, minus */
	{ 0xE006, 0xE004, 0xE01A, 0xE018, 0xE00E, 0xE01C, 0xE01E, 0xE00A, 0xE010, 0xE037, 0xE03A, 0xE00C },
	/* Steam Deck: A, B, X, Y, menu, L2, R2, L1, R1, d-pad left and right,
	view */
	{ 0xE001, 0xE003, 0xE01D, 0xE01F, 0xE00F, 0xE009, 0xE015, 0xE007, 0xE013, 0xE027, 0xE02A, 0xE01B },
	/* Wii U (by label, as the Wii U port presses them: A is A): A, B, X,
	Y, plus, ZL, ZR, L, R, d-pad left and right, minus */
	{ 0xE005, 0xE007, 0xE017, 0xE019, 0xE00F, 0xE01B, 0xE01D, 0xE00B, 0xE013, 0xE02B, 0xE02E, 0xE00D },
	/* Steam Controller: A, B, X, Y, start, LT, RT, LB, RB, d-pad left and
	right, back */
	{ 0xE022, 0xE024, 0xE036, 0xE038, 0xE034, 0xE04D, 0xE059, 0xE049, 0xE055, 0xE040, 0xE043, 0xE026 },
};

static const int button_fonts[NUMBER_OF_PROMPT_SCHEMES] =
{
	POSIX_UI_FONT_KEYBOARD, POSIX_UI_FONT_XBOX, POSIX_UI_FONT_PLAYSTATION, POSIX_UI_FONT_NINTENDO,
	POSIX_UI_FONT_STEAM_DECK, POSIX_UI_FONT_WIIU, POSIX_UI_FONT_STEAM_CONTROLLER,
};

static int scheme(void)
{
	int value = platform_prompt_scheme();

	return value >= 0 && value < NUMBER_OF_PROMPT_SCHEMES ? value : PROMPT_SCHEME_XBOX;
}

/* ---------- gathering */

void ui_overlay_cutout(float x, float y, float width, float height)
{
	if (overlay.cutout_count >= 4)
		return;
	overlay.cutouts[overlay.cutout_count][0] = x;
	overlay.cutouts[overlay.cutout_count][1] = y;
	overlay.cutouts[overlay.cutout_count][2] = x + width;
	overlay.cutouts[overlay.cutout_count][3] = y + height;
	overlay.cutout_count++;
}

int ui_overlay_available(void)
{
	return !overlay.failed;
}

static struct quad *new_quad(void)
{
	struct quad *quad;

	if (overlay.quad_count >= MAXIMUM_QUADS)
		return NULL;
	quad = &overlay.quads[overlay.quad_count++];
	memset(quad, 0, sizeof(*quad));
	quad->u0 = -1.0f;
	quad->layer = overlay.layer;
	return quad;
}

void ui_overlay_layer(int layer)
{
	overlay.layer = layer < 0 ? 0 : layer >= MAXIMUM_LAYERS ? MAXIMUM_LAYERS - 1 : layer;
}

void ui_overlay_gradient(float x, float y, float width, float height, float radius, unsigned int top,
	unsigned int bottom)
{
	struct quad *quad = new_quad();

	if (!quad)
		return;
	quad->x = x;
	quad->y = y;
	quad->width = width;
	quad->height = height;
	quad->radius = radius;
	quad->top = top;
	quad->bottom = bottom;
}

void ui_overlay_rect(float x, float y, float width, float height, float radius, unsigned int color)
{
	ui_overlay_gradient(x, y, width, height, radius, color, color);
}

void ui_overlay_outline(float x, float y, float width, float height, float radius, float thickness,
	unsigned int color)
{
	struct quad *quad = new_quad();

	if (!quad)
		return;
	quad->x = x;
	quad->y = y;
	quad->width = width;
	quad->height = height;
	quad->radius = radius;
	quad->thickness = thickness > 0.0f ? thickness : 1.0f;
	quad->top = quad->bottom = color;
}

static unsigned int next_codepoint(const char **cursor)
{
	const unsigned char *bytes = (const unsigned char *)*cursor;
	unsigned int codepoint;
	int extra;

	if (!*bytes)
		return 0;
	if (bytes[0] < 0x80) { codepoint = bytes[0]; extra = 0; }
	else if ((bytes[0] & 0xE0) == 0xC0) { codepoint = bytes[0] & 0x1F; extra = 1; }
	else if ((bytes[0] & 0xF0) == 0xE0) { codepoint = bytes[0] & 0x0F; extra = 2; }
	else { codepoint = bytes[0] & 0x07; extra = 3; }
	*cursor += 1;
	while (extra-- > 0 && (**cursor & 0xC0) == 0x80)
	{
		codepoint = (codepoint << 6) | (unsigned int)(**cursor & 0x3F);
		*cursor += 1;
	}
	return codepoint;
}

static int posix_font(int font)
{
	switch (font)
	{
	case UI_FONT_BOLD: return POSIX_UI_FONT_BOLD;
	case UI_FONT_MENU: return POSIX_UI_FONT_MENU;
	case UI_FONT_MENU_SMALL: return POSIX_UI_FONT_MENU_SMALL;
	case UI_FONT_TITLE: return POSIX_UI_FONT_TITLE;
	default: return POSIX_UI_FONT_REGULAR;
	}
}

/* hud_hires.c's: whether display.title_font is "cyborg" */
int hud_hires_title_font_cyborg(void);

/* a face's capitals' height, and its baseline below a line's top (as
lay_out_text places it), at a size of 1; 0 if it cannot be read */
static int face_lines(int face, float *capitals, float *baseline)
{
	static int read[POSIX_UI_FONT_COUNT];
	static float capitals_of[POSIX_UI_FONT_COUNT], baseline_of[POSIX_UI_FONT_COUNT];

	if (face < 0 || face >= POSIX_UI_FONT_COUNT)
		return 0;
	if (!read[face])
	{
		float ascent, descent;
		int width, height, x, y;
		unsigned char *bitmap = posix_ui_font_glyph(face, 100.0f, 'H', &width, &height, &x, &y);

		read[face] = -1;
		if (bitmap && height > 0 && posix_ui_font_metrics(face, 100.0f, &ascent, &descent))
		{
			capitals_of[face] = (float)height / 100.0f;
			baseline_of[face] = ((100.0f - (ascent + descent)) * 0.5f + ascent) / 100.0f;
			read[face] = 1;
		}
		posix_ui_font_free(bitmap);
	}
	*capitals = capitals_of[face];
	*baseline = baseline_of[face];
	return read[face] > 0;
}

/* the face a font draws with: for the titles, display.title_font's, the
cyborg one (Cyborg Interface Gothic) sized to the classic one's capitals
and set on its baseline, so a screen lays its title out once (size and y
changed to match; y may be NULL) */
static int font_face(int font, float *size, float *y)
{
	int face = posix_font(font);
	float classic_capitals, classic_baseline, capitals, baseline, cyborg_size;

	if (face != POSIX_UI_FONT_TITLE || !hud_hires_title_font_cyborg() ||
		!face_lines(POSIX_UI_FONT_TITLE, &classic_capitals, &classic_baseline) ||
		!face_lines(POSIX_UI_FONT_TITLE_CYBORG, &capitals, &baseline))
	{
		return face;
	}
	cyborg_size = *size * classic_capitals / capitals;
	if (y)
		*y += *size * classic_baseline - cyborg_size * baseline;
	*size = cyborg_size;
	return POSIX_UI_FONT_TITLE_CYBORG;
}

static void forget_glyphs(void);

/* the menus' faces from the player's font files (display.high_res_font,
display.high_res_font_large: text_hires.c), read again when the settings
change; the glyphs drawn before go */
static void player_fonts_check(void)
{
	static unsigned long read_at = (unsigned long)-1;
	static char used[2][1024];
	char paths[2][1024];
	int role;

	if (read_at == config_changes())
		return;
	read_at = config_changes();
	for (role = 0; role < 2; role++)
	{
		int font = role ? POSIX_UI_FONT_MENU : POSIX_UI_FONT_MENU_SMALL;

		text_hires_font_file(role, paths[role], sizeof(paths[role]));
		if (!strcmp(paths[role], used[role]))
			continue;
		snprintf(used[role], sizeof(used[role]), "%s", paths[role]);
		/* (text_hires.c logs a file it cannot read) */
		if (posix_ui_font_use_file(font, paths[role]))
			platform_log("Online Games: %s menu text drawn with %s", role ? "large" : "small", paths[role]);
		forget_glyphs();
	}
}

/* the font that draws a character: the menus' faces lack most scripts
(players' names), which Noto Sans Bold draws */
static int glyph_font(int font, unsigned int codepoint)
{
	player_fonts_check();
	if ((font == POSIX_UI_FONT_MENU || font == POSIX_UI_FONT_MENU_SMALL || font == POSIX_UI_FONT_TITLE ||
		font == POSIX_UI_FONT_TITLE_CYBORG) &&
		codepoint > ' ' && !posix_ui_font_has(font, codepoint))
	{
		return POSIX_UI_FONT_BOLD;
	}
	return font;
}

/* a string's width in the layout at a size: measured at the layout's own
scale (Present lays it out again at the window's) */
static float measure(int font, float size, const char *text)
{
	const char *cursor = text;
	unsigned int codepoint, previous = 0;
	int previous_font = -1;
	float width = 0.0f;

	while ((codepoint = next_codepoint(&cursor)) != 0)
	{
		int face = glyph_font(font, codepoint);

		width += posix_ui_font_advance(face, size, codepoint, face == previous_font ? previous : 0);
		previous = codepoint;
		previous_font = face;
	}
	return width;
}

float ui_overlay_text_width(int font, float size, const char *text)
{
	int face;

	if (!text)
		return 0.0f;
	face = font_face(font, &size, NULL);
	return measure(face, size, text);
}

static float add_skewed_text(int font, float size, float x, float y, int align, unsigned int color, float skew,
	const char *text)
{
	int length = (int)strlen(text);
	float width = measure(font, size, text);

	if (overlay.text_count >= MAXIMUM_TEXTS ||
		overlay.text_used + length + 1 > MAXIMUM_TEXT)
	{
		return width;
	}
	overlay.texts[overlay.text_count].font = font;
	overlay.texts[overlay.text_count].align = align;
	overlay.texts[overlay.text_count].size = size;
	overlay.texts[overlay.text_count].x = x;
	overlay.texts[overlay.text_count].y = y;
	overlay.texts[overlay.text_count].color = color;
	overlay.texts[overlay.text_count].layer = overlay.layer;
	overlay.texts[overlay.text_count].skew = skew;
	overlay.texts[overlay.text_count].offset = overlay.text_used;
	memcpy(overlay.text + overlay.text_used, text, (size_t)length + 1);
	overlay.text_used += length + 1;
	overlay.text_count++;
	return width;
}

static float add_text(int font, float size, float x, float y, int align, unsigned int color, const char *text)
{
	return add_skewed_text(font, size, x, y, align, color, 0.0f, text);
}

float ui_overlay_text(int font, float size, float x, float y, int align, unsigned int color, const char *text)
{
	int face;

	if (!text || !*text)
		return 0.0f;
	face = font_face(font, &size, &y);
	return add_text(face, size, x, y, align, color, text);
}

/* (a color's alpha times a share of it) */
static unsigned int fade(unsigned int color, float share)
{
	float alpha = (float)(color & 0xFF) * share;

	return (color & 0xFFFFFF00) | (unsigned int)(alpha < 0.0f ? 0.0f : alpha > 255.0f ? 255.0f : alpha);
}

float ui_overlay_text_glow(int font, float size, float x, float y, int align, unsigned int color,
	unsigned int glow, const char *text)
{
	/* the glow: the text again, faint, in a ring of offsets around it at two
	distances (quads only, as the menus' glow is a blurred copy) */
	static const float ring[8][2] =
	{
		{ 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f },
		{ 0.7f, 0.7f }, { -0.7f, 0.7f }, { 0.7f, -0.7f }, { -0.7f, -0.7f },
	};
	/* (the glow's spread by the size asked for, whatever the face) */
	float spread = size * 0.06f > 0.6f ? size * 0.06f : 0.6f;
	int face = font_face(font, &size, &y);
	int index;

	if (!text || !*text)
		return 0.0f;
	/* (the drop shadow, a unit down and right) */
	add_text(face, size, x + 0.8f, y + 1.0f, align, fade(0x000000FF, (float)(color & 0xFF) / 255.0f * 0.7f), text);
	/* (no glow: the shadow alone) */
	for (index = 0; index < 8 && (glow & 0xFF); index++)
	{
		add_text(face, size, x + ring[index][0] * spread * 2.0f, y + ring[index][1] * spread * 2.0f, align,
			fade(glow, 0.10f), text);
		add_text(face, size, x + ring[index][0] * spread, y + ring[index][1] * spread, align, fade(glow, 0.16f), text);
	}
	return add_text(face, size, x, y, align, color, text);
}

/* ---------- the Xbox menus' pieces */

void ui_overlay_panel(float x, float y, float width, float height, int lit)
{
	float radius = 7.0f;
	int ring;

	/* (lit: an outer glow, fading out from the edge) */
	if (lit)
	{
		for (ring = 1; ring <= 4; ring++)
		{
			ui_overlay_outline(x - ring, y - ring, width + 2 * ring, height + 2 * ring, radius + ring, 1.0f,
				fade(0x3D8BFFFF, 0.28f / (float)ring));
		}
	}
	ui_overlay_gradient(x, y, width, height, radius, lit ? 0x0B2A5EF0 : 0x0A2350B8, lit ? 0x061740F4 : 0x04102AC8);
	/* the faint inner glow, along the inside of the edge */
	for (ring = 1; ring <= 3; ring++)
	{
		ui_overlay_outline(x + ring, y + ring, width - 2 * ring, height - 2 * ring, radius - ring, 1.0f,
			fade(0x3D8BFFFF, (lit ? 0.16f : 0.10f) / (float)ring));
	}
	ui_overlay_outline(x, y, width, height, radius, 1.0f, lit ? 0x4F9DFFFF : 0x2F6DD0D0);
}

void ui_overlay_band(float left, float right, float top, float bottom)
{
	ui_overlay_gradient(left, top, right - left, bottom - top, 0, 0x0A2A6A70, 0x04153A80);
	/* the rule: bright, with a faint glow above and below */
	ui_overlay_rect(left, top - 2.0f, right - left, 2.0f, 0, 0x3D8BFF30);
	ui_overlay_rect(left, top + 1.0f, right - left, 2.0f, 0, 0x3D8BFF30);
	ui_overlay_rect(left, top - 0.5f, right - left, 1.5f, 0, 0x4F9DFFFF);
}

void ui_overlay_prompt_bar(float left, float right, float top, float bottom)
{
	ui_overlay_gradient(left, top, right - left, bottom - top, 0, 0x000000D0, 0x000000B0);
	ui_overlay_rect(left, top - 2.0f, right - left, 2.0f, 0, 0x3D8BFF30);
	ui_overlay_rect(left, top - 0.5f, right - left, 1.5f, 0, 0x4F9DFFFF);
}

static int utf8(unsigned int codepoint, char *out)
{
	out[0] = (char)(0xE0 | (codepoint >> 12));
	out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
	out[2] = (char)(0x80 | (codepoint & 0x3F));
	out[3] = 0;
	return 3;
}

/* how each device's buttons are drawn: an Xbox-like pad's by its icons
(draw_icon; the d-pad's as a pill), a Nintendo pad's face buttons as grey
balls with its own letters, a PlayStation pad's by Kenney's glyphs, the
other buttons as dark pills with a label, the keyboard's as light key caps;
the Steam Deck's, the Steam Controller's and the Wii U's all by Kenney's
glyphs (kenney_device), as pills and balls where a build has no font for
them */
static const char *const pill_labels[NUMBER_OF_PROMPT_SCHEMES][NUMBER_OF_UI_BUTTONS] =
{
	/* (the keyboard's keys for them: xinput_sdl.c; C is Online Games' Link
	Profile, F1 Back, F2 Start) */
	{ "ENTER", "BKSP", "E", "TAB", "F2", "Q", "X", "Q", "C", "\xE2\x86\x90", "\xE2\x86\x92", "F1" },
	{ "A", "B", "X", "Y", "START", "LT", "RT", "LB", "RB", "\xE2\x80\xB9", "\xE2\x80\xBA", "BACK" },
	{ "", "", "", "", "OPTIONS", "L2", "R2", "L1", "R1", "\xE2\x80\xB9", "\xE2\x80\xBA", "SHARE" },
	/* (Nintendo's south and east are B and A where the Xbox's are A and B,
	its west and north Y and X) */
	{ "B", "A", "Y", "X", "+", "ZL", "ZR", "L", "R", "\xE2\x80\xB9", "\xE2\x80\xBA", "\xE2\x88\x92" },
	{ "A", "B", "X", "Y", "MENU", "L2", "R2", "L1", "R1", "\xE2\x80\xB9", "\xE2\x80\xBA", "VIEW" },
	/* (the Wii U's by label: A is A) */
	{ "A", "B", "X", "Y", "+", "ZL", "ZR", "L", "R", "\xE2\x80\xB9", "\xE2\x80\xBA", "\xE2\x88\x92" },
	{ "A", "B", "X", "Y", "START", "LT", "RT", "LB", "RB", "\xE2\x80\xB9", "\xE2\x80\xBA", "BACK" },
};

/* a pill's or key cap's label: the d-pad's arrows only if a face draws them
(the menus' face, else Noto Sans Bold: glyph_font), else "\u2039" and
"\u203A", else "<" and ">", never a blank cap */
static const char *pill_label(int device, int button)
{
	static const unsigned int arrows[2][3] = { { 0x2190, 0x2039, '<' }, { 0x2192, 0x203A, '>' } };
	static const char *const texts[2][3] = { { "\xE2\x86\x90", "\xE2\x80\xB9", "<" },
		{ "\xE2\x86\x92", "\xE2\x80\xBA", ">" } };
	int side, choice;

	if (button != UI_BUTTON_DPAD_LEFT && button != UI_BUTTON_DPAD_RIGHT)
		return pill_labels[device][button];
	side = button == UI_BUTTON_DPAD_RIGHT;
	/* (the label's own first: the keyboard's arrow keys, the pads' chevrons) */
	choice = device == 0 ? 0 : 1;
	for (; choice < 2; choice++)
	{
		if (posix_ui_font_has(POSIX_UI_FONT_MENU, arrows[side][choice]) ||
			posix_ui_font_has(POSIX_UI_FONT_BOLD, arrows[side][choice]))
		{
			return texts[side][choice];
		}
	}
	return texts[side][2];
}

/* the Xbox's face buttons' colors, lit at the top and deep at the bottom */
static const unsigned int face_colors[4][2] =
{
	{ 0x9BE57AFF, 0x2C8A1CFF }, /* A green */
	{ 0xFF8A7AFF, 0xB0201CFF }, /* B red */
	{ 0x8FB4FFFF, 0x2448B8FF }, /* X blue */
	{ 0xFFE680FF, 0xC48A0CFF }, /* Y yellow */
};

/* (whether the device's Kenney font has the button) */
static int kenney_glyph(int device, int button)
{
	return button_glyphs[device][button] && posix_ui_font_has(button_fonts[device], button_glyphs[device][button]);
}

/* (a device whose every button is Kenney's glyph: the Steam Deck, the
Steam Controller and the Wii U) */
static int kenney_device(int device)
{
	return device == PROMPT_SCHEME_STEAM_DECK || device == PROMPT_SCHEME_STEAM_CONTROLLER ||
		device == PROMPT_SCHEME_WIIU;
}

/* Kenney's glyphs (a face button's, at a font size of 100: stb_truetype's,
by the font's ascent and descent) fill the line from 27 down to its
baseline at 99, and 74 across: drawn at the button's size they were 72
percent of it, from 27 down. Here a glyph fills four fifths of the
button, centered on it, as snapchipper's icons fill theirs (10 to 90):
its font at KENNEY_LINE times size, raised by KENNEY_RAISE times size
(tools/test_prompt_schemes.py measures it) */
#define KENNEY_LINE 1.12f
#define KENNEY_RAISE 0.21f

static float kenney_width(int device, int button, float size)
{
	char text[4];

	utf8(button_glyphs[device][button], text);
	return measure(button_fonts[device], size * KENNEY_LINE, text);
}

static float draw_kenney(int device, int button, float size, float x, float y, unsigned int color)
{
	char text[4];

	utf8(button_glyphs[device][button], text);
	return add_text(button_fonts[device], size * KENNEY_LINE, x, y - size * KENNEY_RAISE, UI_ALIGN_LEFT, color, text);
}

/* the icon of a button (an Xbox-like pad's), or -1: the d-pad has none */
static int button_icon(int button)
{
	return button >= UI_BUTTON_A && button <= UI_BUTTON_RIGHT_SHOULDER ? button :
		button == UI_BUTTON_BACK ? 9 : -1;
}

/* the columns an icon draws (its pixels' alpha), measured once */
static void measure_icons(void)
{
	int icon, size = ui_prompt_icon_size;

	if (overlay.icons_measured)
		return;
	for (icon = 0; icon < NUMBER_OF_ICONS; icon++)
	{
		const unsigned char *pixels = (const unsigned char *)ui_prompt_icons[icon];
		int x, y, left = size, right = 0;

		for (y = 0; y < size; y++)
		{
			for (x = 0; x < size; x++)
			{
				if (pixels[(y * size + x) * 4 + 3] > 8)
				{
					left = x < left ? x : left;
					right = x + 1 > right ? x + 1 : right;
				}
			}
		}
		overlay.icon_left[icon] = right > left ? left : 0;
		overlay.icon_right[icon] = right > left ? right : size;
	}
	overlay.icons_measured = 1;
}

/* an icon's width at a size (the square's height), its drawn columns */
static float icon_width(int icon, float size)
{
	measure_icons();
	return size * (float)(overlay.icon_right[icon] - overlay.icon_left[icon]) / (float)ui_prompt_icon_size;
}

static float draw_icon(int icon, float size, float x, float y, unsigned int color)
{
	struct quad *quad = new_quad();
	float width = icon_width(icon, size);

	if (!quad)
		return width;
	quad->x = x;
	quad->y = y;
	quad->width = width;
	quad->height = size;
	quad->radius = ICON_MARK;
	quad->thickness = (float)icon;
	quad->top = quad->bottom = color;
	quad->u0 = (float)overlay.icon_left[icon] / (float)ui_prompt_icon_size;
	quad->u1 = (float)overlay.icon_right[icon] / (float)ui_prompt_icon_size;
	quad->v0 = 0.0f;
	quad->v1 = 1.0f;
	return width;
}

void ui_overlay_picture(int serial, const unsigned char *pixels, int width, int height, float x, float y,
	float draw_width, float draw_height, unsigned int color)
{
	struct quad *quad;

	if (!serial || !pixels || width <= 0 || height <= 0)
		return;
	if (serial != overlay.picture_serial)
	{
		unsigned char *copy = malloc((size_t)width * (size_t)height * 4);

		if (!copy)
			return;
		memcpy(copy, pixels, (size_t)width * (size_t)height * 4);
		free(overlay.picture_pixels);
		overlay.picture_pixels = copy;
		overlay.picture_width = width;
		overlay.picture_height = height;
		overlay.picture_serial = serial;
	}
	quad = new_quad();
	if (!quad)
		return;
	quad->x = x;
	quad->y = y;
	quad->width = draw_width;
	quad->height = draw_height;
	quad->radius = PICTURE_MARK;
	quad->top = quad->bottom = color;
	quad->u0 = 0.0f;
	quad->v0 = 0.0f;
	quad->u1 = 1.0f;
	quad->v1 = 1.0f;
}

/* ---------- the medals */

/* the atlas's texels, premultiplied (so that its smaller levels keep no
dark fringe), made once; NULL without them */
static const unsigned char *medal_texels(void)
{
#ifdef HALO_MEDAL_ICONS
	if (!overlay.medal_texels && !overlay.medals_uploaded)
	{
		size_t texels = (size_t)ui_medal_atlas_width * (size_t)ui_medal_atlas_height, texel;
		const unsigned char *from = (const unsigned char *)ui_medal_atlas;
		unsigned char *to = malloc(texels * 4);

		if (!to)
			return NULL;
		for (texel = 0; texel < texels; texel++)
		{
			unsigned int alpha = from[texel * 4 + 3];

			to[texel * 4] = (unsigned char)(from[texel * 4] * alpha / 255);
			to[texel * 4 + 1] = (unsigned char)(from[texel * 4 + 1] * alpha / 255);
			to[texel * 4 + 2] = (unsigned char)(from[texel * 4 + 2] * alpha / 255);
			to[texel * 4 + 3] = (unsigned char)alpha;
		}
		overlay.medal_texels = to;
	}
#endif
	return overlay.medal_texels;
}

int ui_overlay_medals_available(void)
{
#ifdef HALO_MEDAL_ICONS
	return ui_overlay_available();
#else
	return 0;
#endif
}

void ui_overlay_medal(int medal, float x, float y, float size, unsigned int color)
{
#ifdef HALO_MEDAL_ICONS
	struct quad *quad;
	float cell_width = (float)ui_medal_atlas_cell / (float)ui_medal_atlas_width;
	float cell_height = (float)ui_medal_atlas_cell / (float)ui_medal_atlas_height;
	int columns = ui_medal_atlas_width / ui_medal_atlas_cell;

	if (medal < 0 || medal >= columns * (ui_medal_atlas_height / ui_medal_atlas_cell) || !(color & 0xFF))
		return;
	quad = new_quad();
	if (!quad)
		return;
	quad->x = x;
	quad->y = y;
	quad->width = quad->height = size;
	quad->radius = MEDAL_MARK;
	quad->thickness = 0.0f;
	quad->top = quad->bottom = color;
	quad->u0 = (float)(medal % columns) * cell_width;
	quad->v0 = (float)(medal / columns) * cell_height;
	quad->u1 = quad->u0 + cell_width;
	quad->v1 = quad->v0 + cell_height;
	overlay.medals_drawn = 1;
#else
	(void)medal;
	(void)x;
	(void)y;
	(void)size;
	(void)color;
#endif
}

/* ---------- the plasma */

/* the overlay's own noise: smooth value noise in octaves, tiling (from a
fixed seed: the same each run), white with the noise as its alpha */
static void plasma_noise(void)
{
	enum { CELLS = 4 };
	unsigned char *pixels;
	float lattice[3][32][32];
	unsigned int seed = 0x9E3779B9u;
	int octave, x, y;

	pixels = malloc(PLASMA_NOISE_SIZE * PLASMA_NOISE_SIZE * 4);
	if (!pixels)
		return;
	for (octave = 0; octave < 3; octave++)
	{
		for (y = 0; y < 32; y++)
		{
			for (x = 0; x < 32; x++)
			{
				seed = seed * 1664525u + 1013904223u;
				lattice[octave][y][x] = (float)(seed >> 8) / 16777216.0f;
			}
		}
	}
	for (y = 0; y < PLASMA_NOISE_SIZE; y++)
	{
		for (x = 0; x < PLASMA_NOISE_SIZE; x++)
		{
			float value = 0.0f, weight = 0.6f;

			for (octave = 0; octave < 3; octave++)
			{
				int cells = CELLS << octave;
				float fx = (float)x * cells / PLASMA_NOISE_SIZE, fy = (float)y * cells / PLASMA_NOISE_SIZE;
				int x0 = (int)fx, y0 = (int)fy, x1 = (x0 + 1) % cells, y1 = (y0 + 1) % cells;
				float tx = fx - x0, ty = fy - y0;
				float a, b;

				/* (smoothstep between the lattice's corners) */
				tx = tx * tx * (3.0f - 2.0f * tx);
				ty = ty * ty * (3.0f - 2.0f * ty);
				a = lattice[octave][y0][x0] + (lattice[octave][y0][x1] - lattice[octave][y0][x0]) * tx;
				b = lattice[octave][y1][x0] + (lattice[octave][y1][x1] - lattice[octave][y1][x0]) * tx;
				value += (a + (b - a) * ty) * weight;
				weight *= 0.5f;
			}
			value /= 1.05f;
			pixels[(y * PLASMA_NOISE_SIZE + x) * 4] = 255;
			pixels[(y * PLASMA_NOISE_SIZE + x) * 4 + 1] = 255;
			pixels[(y * PLASMA_NOISE_SIZE + x) * 4 + 2] = 255;
			pixels[(y * PLASMA_NOISE_SIZE + x) * 4 + 3] = (unsigned char)(value < 0.0f ? 0 : value > 1.0f ? 255 :
				value * 255.0f);
		}
	}
	free(overlay.plasma_pixels);
	overlay.plasma_pixels = pixels;
	overlay.plasma_width = overlay.plasma_height = PLASMA_NOISE_SIZE;
	overlay.plasma_serial = -1;
}

void ui_overlay_plasma_source(int serial, const unsigned char *pixels, int width, int height)
{
	unsigned char *copy;

	if (!serial || serial == overlay.plasma_serial || !pixels || width <= 0 || height <= 0)
		return;
	copy = malloc((size_t)width * (size_t)height * 4);
	if (!copy)
		return;
	memcpy(copy, pixels, (size_t)width * (size_t)height * 4);
	free(overlay.plasma_pixels);
	overlay.plasma_pixels = copy;
	overlay.plasma_width = width;
	overlay.plasma_height = height;
	overlay.plasma_serial = serial;
}

int ui_overlay_plasma_from_texture(const void *hardware_format)
{
#ifdef HALO_CONSOLE_NO_GUIDE
	(void)hardware_format;
	return 0;
#else
	unsigned long width = 0, height = 0;
	unsigned char *pixels;

	if (!hardware_format)
		return 0;
	if (overlay.plasma_serial > 0)
		return 1;
	pixels = xgpu_texture_rgba((const DWORD *)hardware_format, &width, &height);
	if (!pixels)
		return 0;
	/* (serial 1: the game's own, never read again) */
	ui_overlay_plasma_source(1, pixels, (int)width, (int)height);
	free(pixels);
	platform_log("DELTA: the plasma is drawn from ui.map's (%lux%lu)", width, height);
	return 1;
#endif
}

/* (the game's compute_offset_coordinate: how far a map has scrolled, a
share of its size) */
static float plasma_scroll(unsigned int time, float per_second)
{
	return (float)fmod((double)per_second * 0.001 * (double)time, 1.0);
}

void ui_overlay_plasma(float x, float y, float width, float height, float radius, unsigned int color,
	unsigned int time)
{
	/* the menus' two maps: one 311 pixels of the screen a repeat, the other
	201, each scrolled at its own pace (ui_widget.c) */
	const float first = 311.0f, second = 201.0f;
	float first_x = plasma_scroll(time, 0.03215434f), first_y = plasma_scroll(time, 0.026795285f);
	float second_x = -plasma_scroll(time, 0.035536603f), second_y = -plasma_scroll(time, 0.031094525f);
	struct quad *quad;

	if (!overlay.plasma_pixels)
		plasma_noise();
	quad = new_quad();
	if (!quad)
		return;
	quad->x = x;
	quad->y = y;
	quad->width = width;
	quad->height = height;
	quad->radius = PLASMA_MARK;
	quad->thickness = radius;
	quad->top = quad->bottom = color;
	/* (the first map's place, anchored to the screen as the menus' is; the
	second's from it, by the shader) */
	quad->u0 = x / first + first_x;
	quad->v0 = y / first + first_y;
	quad->u1 = (x + width) / first + first_x;
	quad->v1 = (y + height) / first + first_y;
	overlay.plasma_shift[0] = second_x - first_x * first / second;
	overlay.plasma_shift[1] = second_y - first_y * first / second;
	overlay.plasma_shift[2] = first / second;
	overlay.plasma_shift[3] = 0.0f;
	/* (no theme set: Halo Blue's) */
	if (overlay.plasma_shift[7] == 0.0f)
		ui_overlay_plasma_thread(0x3373FFFF);
	overlay.plasma_drawn = 1;
}

void ui_overlay_plasma_thread(unsigned int color)
{
	overlay.plasma_shift[4] = (float)((color >> 24) & 0xFF) / 255.0f;
	overlay.plasma_shift[5] = (float)((color >> 16) & 0xFF) / 255.0f;
	overlay.plasma_shift[6] = (float)((color >> 8) & 0xFF) / 255.0f;
	overlay.plasma_shift[7] = 1.0f;
}

static int face_button(int button)
{
	return button >= UI_BUTTON_A && button <= UI_BUTTON_Y;
}

/* (a pill's or a key cap's label size, and its room around the label) */
static float pill_text_size(float size)
{
	return size * 0.52f;
}

static float pill_width(const char *label, float size)
{
	float width = measure(POSIX_UI_FONT_MENU, pill_text_size(size), label) + size * 0.6f;

	return width > size * 1.3f ? width : size * 1.3f;
}

float ui_overlay_button_width(int button, float size)
{
	int device = scheme();

	if (button < 0 || button >= NUMBER_OF_UI_BUTTONS)
		return 0.0f;
	/* (an Xbox-like pad's: its icon) */
	if (device == PROMPT_SCHEME_XBOX && button_icon(button) >= 0)
		return icon_width(button_icon(button), size);
	/* (Kenney's shape, as wide as it draws: a PlayStation pad's face
	buttons, every button of the Steam and Wii U pads) */
	if (((device == PROMPT_SCHEME_PLAYSTATION && face_button(button)) || kenney_device(device)) &&
		kenney_glyph(device, button))
	{
		return kenney_width(device, button, size);
	}
	if (device != PROMPT_SCHEME_KEYBOARD && face_button(button))
		return size;
	return pill_width(pill_label(device, button), size);
}

float ui_overlay_button(int button, float size, float x, float y, unsigned int color)
{
	int device = scheme();
	float share = (float)(color & 0xFF) / 255.0f;
	const char *label;

	if (button < 0 || button >= NUMBER_OF_UI_BUTTONS)
		return 0.0f;
	label = pill_label(device, button);
	/* an Xbox-like pad's buttons: their icons (snapchipper's) */
	if (device == PROMPT_SCHEME_XBOX && button_icon(button) >= 0)
		return draw_icon(button_icon(button), size, x, y, fade(0xFFFFFFFF, share));
	/* Kenney's glyphs: a PlayStation pad's face buttons, and every button
	of the Steam and Wii U pads */
	if (((device == PROMPT_SCHEME_PLAYSTATION && face_button(button)) || kenney_device(device)) &&
		kenney_glyph(device, button))
	{
		return draw_kenney(device, button, size, x, y, fade(0xFFFFFFFF, share));
	}
	if (device != PROMPT_SCHEME_KEYBOARD && face_button(button))
	{
		float diameter = size * 0.96f, inset = (size - diameter) * 0.5f;
		float letter = size * 0.66f;
		int grey = device == PROMPT_SCHEME_NINTENDO || device == PROMPT_SCHEME_WIIU;
		unsigned int top = grey ? 0xD4D8E0FF : face_colors[button][0];
		unsigned int bottom = grey ? 0x50565FFF : face_colors[button][1];

		/* a glossy ball: its color from lit to deep, a dark rim, a sheen on
		its upper half, and its letter in white, leaning as the Xbox's */
		ui_overlay_rect(x + inset, y + inset + size * 0.05f, diameter, diameter, diameter * 0.5f, fade(0x00000060, share));
		ui_overlay_gradient(x + inset, y + inset, diameter, diameter, diameter * 0.5f, fade(top, share), fade(bottom, share));
		ui_overlay_gradient(x + inset + diameter * 0.16f, y + inset + diameter * 0.06f, diameter * 0.68f, diameter * 0.42f,
			diameter * 0.21f, fade(0xFFFFFF70, share), fade(0xFFFFFF08, share));
		ui_overlay_outline(x + inset, y + inset, diameter, diameter, diameter * 0.5f, 0.75f, fade(0x00000070, share));
		add_skewed_text(POSIX_UI_FONT_MENU, letter, x + size * 0.5f + 0.4f, y + (size - letter) * 0.5f + 0.9f,
			UI_ALIGN_CENTER, fade(0x00000080, share), device == 1 ? 0.2f : 0.0f, label);
		add_skewed_text(POSIX_UI_FONT_MENU, letter, x + size * 0.5f, y + (size - letter) * 0.5f + 0.3f, UI_ALIGN_CENTER,
			fade(0xFFFFFFFF, share), device == 1 ? 0.2f : 0.0f, label);
		return size;
	}
	{
		/* a pill (a pad's other buttons) or a key cap (the keyboard's): its
		label in the menus' face */
		float width = pill_width(label, size);
		float height = size * 0.78f, top_y = y + (size - height) * 0.5f;
		float label_size = pill_text_size(size);
		int key = device == 0;

		ui_overlay_rect(x, top_y + size * 0.06f, width, height, height * (key ? 0.22f : 0.45f), fade(0x00000070, share));
		ui_overlay_gradient(x, top_y, width, height, height * (key ? 0.22f : 0.45f),
			fade(key ? 0xF2F5FAFF : 0x5A606BFF, share), fade(key ? 0xA9B3C2FF : 0x16191FFF, share));
		ui_overlay_outline(x, top_y, width, height, height * (key ? 0.22f : 0.45f), 0.75f,
			fade(key ? 0x5F6B7DFF : 0x9AA3B0A0, share));
		add_text(POSIX_UI_FONT_MENU, label_size, x + width * 0.5f, top_y + (height - label_size) * 0.5f + 0.2f,
			UI_ALIGN_CENTER, fade(key ? 0x1A2230FF : 0xFFFFFFFF, share), label);
		return width;
	}
}

int ui_overlay_keyboard(void)
{
	return scheme() == 0;
}

float ui_overlay_key_width(const char *label, float size)
{
	return pill_width(label ? label : "", size);
}

float ui_overlay_key(const char *label, float size, float x, float y, unsigned int color)
{
	float share = (float)(color & 0xFF) / 255.0f;
	float width = pill_width(label ? label : "", size);
	float height = size * 0.78f, top_y = y + (size - height) * 0.5f;
	float label_size = pill_text_size(size);

	ui_overlay_rect(x, top_y + size * 0.06f, width, height, height * 0.22f, fade(0x00000070, share));
	ui_overlay_gradient(x, top_y, width, height, height * 0.22f, fade(0xF2F5FAFF, share), fade(0xA9B3C2FF, share));
	ui_overlay_outline(x, top_y, width, height, height * 0.22f, 0.75f, fade(0x5F6B7DFF, share));
	add_text(POSIX_UI_FONT_MENU, label_size, x + width * 0.5f, top_y + (height - label_size) * 0.5f + 0.2f,
		UI_ALIGN_CENTER, fade(0x1A2230FF, share), label ? label : "");
	return width;
}

/* ---------- the platforms' logos */

/* (platform_icons.ttf: one logo per platform of delta.h's registry, at
U+E100 plus the platform's number; a platform without a logo in the font,
and unknown, draw nothing) */
#define PLATFORM_GLYPH_BASE 0xE100

int ui_overlay_platform(int platform, float size, float x, float y, unsigned int color, int shadow)
{
	char text[4];
	unsigned int glyph = PLATFORM_GLYPH_BASE + (unsigned int)platform;

	if (platform <= 0 || platform >= NUMBER_OF_DELTA_PLATFORMS || !posix_ui_font_has(POSIX_UI_FONT_PLATFORMS, glyph))
		return 0;
	utf8(glyph, text);
	if (shadow)
		add_text(POSIX_UI_FONT_PLATFORMS, size, x + 0.8f, y + 1.0f, UI_ALIGN_CENTER,
			fade(0x000000FF, (float)(color & 0xFF) / 255.0f * 0.7f), text);
	add_text(POSIX_UI_FONT_PLATFORMS, size, x, y, UI_ALIGN_CENTER, color, text);
	return 1;
}

/* ---------- GL */

static const char vertex_source[] =
	"in vec2 position;\n"
	"in vec2 texture_coordinate;\n"
	"in vec4 shape;\n"
	"in vec2 edge;\n"
	"in vec4 color;\n"
	"uniform vec2 scale;\n"
#ifdef HALO_CONSOLE_GLES2
	"uniform float y_flip;\n"
#endif
	"out vec2 v_texture_coordinate;\n"
	"out vec4 v_shape;\n"
	"out vec2 v_edge;\n"
	"out vec4 v_color;\n"
	"void main()\n"
	"{\n"
	"\tv_texture_coordinate = texture_coordinate;\n"
	"\tv_shape = shape;\n"
	"\tv_edge = edge;\n"
	"\tv_color = color;\n"
	"\tgl_Position = vec4(position * scale - vec2(1.0, -1.0), 0.0, 1.0);\n"
#ifdef HALO_CONSOLE_GLES2
	"\tgl_Position.y *= y_flip;\n"
#endif
	"}\n";

static const char fragment_source[] =
	"in vec2 v_texture_coordinate;\n"
	"in vec4 v_shape;\n"
	"in vec2 v_edge;\n"
	"in vec4 v_color;\n"
	"uniform sampler2D atlas;\n"
#ifdef HALO_CONSOLE_GLES2
	"uniform sampler2D icons;\n"
	"uniform float icon_count;\n"
#else
	"uniform sampler2DArray icons;\n"
#endif
	"uniform sampler2D picture;\n"
	"uniform sampler2D plasma;\n"
	"uniform sampler2D medals;\n"
	"uniform vec4 plasma_shift;\n"
	"uniform vec4 plasma_thread;\n"
	"uniform vec4 cutouts[4];\n"
	"uniform float window_height;\n"
#ifdef HALO_CONSOLE_GLES2
	"uniform float y_flip;\n"
#endif
	"out vec4 fragment;\n"
	"void main()\n"
	"{\n"
	"\tfloat alpha;\n"
	/* (the game's own pictures show through: x0, y0, x1, y1 in window pixels
	from the top) */
#ifdef HALO_CONSOLE_GLES2
	"\tvec2 p = vec2(gl_FragCoord.x, y_flip < 0.0 ? gl_FragCoord.y : window_height - gl_FragCoord.y);\n"
#else
	"\tvec2 p = vec2(gl_FragCoord.x, window_height - gl_FragCoord.y);\n"
#endif
	"\tfor (int i = 0; i < 4; i++)\n"
	"\t\tif (p.x >= cutouts[i].x && p.y >= cutouts[i].y && p.x < cutouts[i].z && p.y < cutouts[i].w)\n"
	"\t\t\tdiscard;\n"
	/* a medal (premultiplied, as an icon) */
	"\tif (v_edge.x < -4.5)\n"
	"\t{\n"
	"\t\tvec4 texel = texture(medals, v_texture_coordinate);\n"
	"\t\tfragment = vec4(texel.rgb / max(texel.a, 0.001), texel.a) * v_color;\n"
	"\t\treturn;\n"
	"\t}\n"
	/* the plasma: the noise map twice, at the menus' two scales, its bright
	threads where the two meet, in a rounded rectangle (edge.y its corners'
	radius) */
	"\tif (v_edge.x < -3.5)\n"
	"\t{\n"
	"\t\tvec2 q = abs(v_shape.xy) - v_shape.zw + vec2(v_edge.y);\n"
	"\t\tfloat d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - v_edge.y;\n"
	"\t\tvec4 t0 = texture(plasma, v_texture_coordinate);\n"
	"\t\tvec4 t1 = texture(plasma, v_texture_coordinate * plasma_shift.z + plasma_shift.xy);\n"
	"\t\tfloat a0 = t0.a * dot(t0.rgb, vec3(0.3333));\n"
	"\t\tfloat a1 = t1.a * dot(t1.rgb, vec3(0.3333));\n"
	"\t\tfloat thread = pow(clamp(1.0 - abs(a0 - a1) * 3.0, 0.0, 1.0), 8.0);\n"
	"\t\tvec3 color = v_color.rgb * (0.8 + 0.6 * a0 * a1) + plasma_thread.rgb * thread * 0.18;\n"
	"\t\tfragment = vec4(color, v_color.a * clamp(0.5 - d, 0.0, 1.0) * (0.92 + 0.08 * thread));\n"
	"\t\treturn;\n"
	"\t}\n"
	/* the picture */
	"\tif (v_edge.x < -2.5)\n"
	"\t{\n"
	"\t\tfragment = texture(picture, v_texture_coordinate) * v_color;\n"
	"\t\treturn;\n"
	"\t}\n"
	/* an icon (premultiplied: its color undone after filtering) */
	"\tif (v_edge.x < -1.5)\n"
	"\t{\n"
#ifdef HALO_CONSOLE_GLES2
	"\t\tvec4 texel = texture(icons, vec2(v_texture_coordinate.x, (v_texture_coordinate.y + v_shape.x) / icon_count));\n"
#else
	"\t\tvec4 texel = texture(icons, vec3(v_texture_coordinate, v_shape.x));\n"
#endif
	"\t\tfragment = vec4(texel.rgb / max(texel.a, 0.001), texel.a) * v_color;\n"
	"\t\treturn;\n"
	"\t}\n"
	"\tif (v_texture_coordinate.x >= 0.0)\n"
	"\t\talpha = texture(atlas, v_texture_coordinate).r;\n"
	"\telse\n"
	"\t{\n"
	/* the rounded rectangle's distance: shape.xy the point from its middle,
	shape.zw its half size, edge.x the corners' radius, edge.y the outline's
	thickness (0: filled) */
	"\t\tvec2 q = abs(v_shape.xy) - v_shape.zw + vec2(v_edge.x);\n"
	"\t\tfloat d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - v_edge.x;\n"
	"\t\talpha = clamp(0.5 - d, 0.0, 1.0);\n"
	"\t\tif (v_edge.y > 0.0)\n"
	"\t\t\talpha *= clamp(d + v_edge.y + 0.5, 0.0, 1.0);\n"
	"\t}\n"
	"\tfragment = vec4(v_color.rgb, v_color.a * alpha);\n"
	"}\n";

static GLuint compile(GLenum type, const char *body)
{
	char source[8192];
	GLuint shader = glCreateShader(type);
	const char *pointer = source;
	GLint status = 0;

#ifdef HALO_GL_ES
	snprintf(source, sizeof(source), "#version %s\nprecision highp float;\nprecision highp sampler2DArray;\n%s",
		xgpu_capabilities.shading_language, body);
#else
	snprintf(source, sizeof(source), "%s%s", XGPU_GL_VERSION_LINE, body);
#endif
	glShaderSource(shader, 1, &pointer, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status)
	{
		char log[2048];

		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		platform_log("overlay: cannot compile a shader: %s", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static int set_up(void)
{
	GLuint vertex_shader, fragment_shader;
	GLint status = 0;
	static const unsigned char zero = 0;
	static const unsigned char zero_texel[4] = { 0, 0, 0, 0 };

	if (overlay.ready || overlay.failed)
		return overlay.ready;
#ifdef HALO_BGFX
	if (xgpu_bgfx)
	{
		int size = ui_prompt_icon_size, icon, texel;

		(void)vertex_shader;
		(void)fragment_shader;
		(void)status;
		(void)zero;
		(void)zero_texel;
		overlay.atlas_texels = calloc(ATLAS_SIZE, ATLAS_SIZE);
		overlay.icon_texels = malloc((size_t)size * size * 4 * NUMBER_OF_ICONS);
		if (!overlay.atlas_texels || !overlay.icon_texels)
		{
			overlay.failed = 1;
			return 0;
		}
		for (icon = 0; icon < NUMBER_OF_ICONS; icon++)
		{
			const unsigned char *from = (const unsigned char *)ui_prompt_icons[icon];
			unsigned char *to = overlay.icon_texels + (size_t)icon * size * size * 4;

			for (texel = 0; texel < size * size; texel++)
			{
				unsigned int alpha = from[texel * 4 + 3];

				to[texel * 4] = (unsigned char)(from[texel * 4] * alpha / 255);
				to[texel * 4 + 1] = (unsigned char)(from[texel * 4 + 1] * alpha / 255);
				to[texel * 4 + 2] = (unsigned char)(from[texel * 4 + 2] * alpha / 255);
				to[texel * 4 + 3] = (unsigned char)alpha;
			}
		}
		overlay.dirty_top = 0;
		overlay.dirty_bottom = ATLAS_SIZE;
		overlay.shelf_x = overlay.shelf_y = 2;
		overlay.ready = 1;
		return 1;
	}
#endif
	vertex_shader = compile(GL_VERTEX_SHADER, vertex_source);
	fragment_shader = compile(GL_FRAGMENT_SHADER, fragment_source);
	if (!vertex_shader || !fragment_shader)
	{
		overlay.failed = 1;
		return 0;
	}
	overlay.program = glCreateProgram();
	glAttachShader(overlay.program, vertex_shader);
	glAttachShader(overlay.program, fragment_shader);
	glBindAttribLocation(overlay.program, 0, "position");
	glBindAttribLocation(overlay.program, 1, "texture_coordinate");
	glBindAttribLocation(overlay.program, 2, "shape");
	glBindAttribLocation(overlay.program, 3, "edge");
	glBindAttribLocation(overlay.program, 4, "color");
	glLinkProgram(overlay.program);
	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);
	glGetProgramiv(overlay.program, GL_LINK_STATUS, &status);
	if (!status)
	{
		platform_log("overlay: cannot link its program");
		overlay.failed = 1;
		return 0;
	}
	overlay.scale_location = glGetUniformLocation(overlay.program, "scale");
	overlay.atlas_location = glGetUniformLocation(overlay.program, "atlas");
	overlay.icons_location = glGetUniformLocation(overlay.program, "icons");
#ifdef HALO_CONSOLE_GLES2
	overlay.icon_count_location = glGetUniformLocation(overlay.program, "icon_count");
	overlay.y_flip_location = glGetUniformLocation(overlay.program, "y_flip");
#endif
	overlay.picture_location = glGetUniformLocation(overlay.program, "picture");
	overlay.plasma_location = glGetUniformLocation(overlay.program, "plasma");
	overlay.medals_location = glGetUniformLocation(overlay.program, "medals");
	overlay.plasma_shift_location = glGetUniformLocation(overlay.program, "plasma_shift");
	overlay.plasma_thread_location = glGetUniformLocation(overlay.program, "plasma_thread");
	overlay.cutouts_location = glGetUniformLocation(overlay.program, "cutouts");
	overlay.window_height_location = glGetUniformLocation(overlay.program, "window_height");

	{
	GLint saved_vertex_array = 0, saved_array_buffer = 0, saved_texture = 0;

	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved_vertex_array);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &saved_array_buffer);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture);
	glGenVertexArrays(1, &overlay.vertex_array);
	glGenBuffers(1, &overlay.vertex_buffer);
	glBindVertexArray(overlay.vertex_array);
	glBindBuffer(GL_ARRAY_BUFFER, overlay.vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(overlay.vertices), NULL, GL_STREAM_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (const void *)offsetof(struct vertex, x));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (const void *)offsetof(struct vertex, u));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (const void *)offsetof(struct vertex, local_x));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (const void *)offsetof(struct vertex, radius));
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct vertex), (const void *)offsetof(struct vertex, color));
	glBindVertexArray(0);

	glGenTextures(1, &overlay.atlas);
	glBindTexture(GL_TEXTURE_2D, overlay.atlas);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_SIZE, ATLAS_SIZE, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	/* (an empty texel for space: glyphs keep away from 0, 0) */
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RED, GL_UNSIGNED_BYTE, &zero);
	/* the icons, a layer each, premultiplied (so that their smaller levels
	keep no dark fringe), with smaller levels for small prompts */
	{
		GLint saved_array = 0;
		int size = ui_prompt_icon_size, icon, texel;
		unsigned char *pixels = malloc((size_t)size * size * 4 * NUMBER_OF_ICONS);

		glGetIntegerv(ICONS_BINDING, &saved_array);
		glGenTextures(1, &overlay.icons);
		glBindTexture(ICONS_TARGET, overlay.icons);
		if (pixels)
		{
			for (icon = 0; icon < NUMBER_OF_ICONS; icon++)
			{
				const unsigned char *from = (const unsigned char *)ui_prompt_icons[icon];
				unsigned char *to = pixels + (size_t)icon * size * size * 4;

				for (texel = 0; texel < size * size; texel++)
				{
					unsigned int alpha = from[texel * 4 + 3];

					to[texel * 4] = (unsigned char)(from[texel * 4] * alpha / 255);
					to[texel * 4 + 1] = (unsigned char)(from[texel * 4 + 1] * alpha / 255);
					to[texel * 4 + 2] = (unsigned char)(from[texel * 4 + 2] * alpha / 255);
					to[texel * 4 + 3] = (unsigned char)alpha;
				}
			}
		}
#ifdef HALO_CONSOLE_GLES2
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size * NUMBER_OF_ICONS, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		free(pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
#else
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, size, size, NUMBER_OF_ICONS, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		free(pixels);
		glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
#endif
		glTexParameteri(ICONS_TARGET, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(ICONS_TARGET, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(ICONS_TARGET, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(ICONS_TARGET, (GLuint)saved_array);
	}
	/* the picture's texture (filled when a picture is given) */
	glGenTextures(1, &overlay.picture);
	glBindTexture(GL_TEXTURE_2D, overlay.picture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &zero_texel);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	/* the plasma's (filled when a plasma is first drawn), tiling */
	glGenTextures(1, &overlay.plasma);
	glBindTexture(GL_TEXTURE_2D, overlay.plasma);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &zero_texel);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	overlay.plasma_uploaded = 0;
	/* the medals' (filled when a medal is first drawn) */
	glGenTextures(1, &overlay.medals);
	glBindTexture(GL_TEXTURE_2D, overlay.medals);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &zero_texel);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindVertexArray((GLuint)saved_vertex_array);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)saved_array_buffer);
	glBindTexture(GL_TEXTURE_2D, (GLuint)saved_texture);
	}
	overlay.shelf_x = overlay.shelf_y = 2;
	overlay.ready = 1;
	return 1;
}

/* a glyph at a pixel size, packed into the atlas the first time (NULL: the
atlas is full; it starts over at the next frame) */
static struct glyph *glyph(int font, int pixel_size, unsigned int codepoint, int *atlas_full)
{
	struct glyph *entry;
	unsigned char *bitmap;
	int index, width, height, x_offset, y_offset;

	for (index = 0; index < overlay.glyph_count; index++)
	{
		entry = &overlay.glyphs[index];
		if (entry->codepoint == codepoint && entry->pixel_size == pixel_size && entry->font == font)
			return entry;
	}
	if (overlay.glyph_count >= MAXIMUM_GLYPHS)
	{
		*atlas_full = 1;
		return NULL;
	}
	bitmap = posix_ui_font_glyph(font, (float)pixel_size, codepoint, &width, &height, &x_offset, &y_offset);
	if (width > 0 && height > 0)
	{
		if (overlay.shelf_x + width + 2 > ATLAS_SIZE)
		{
			overlay.shelf_x = 2;
			overlay.shelf_y += overlay.shelf_height + 2;
			overlay.shelf_height = 0;
		}
		if (overlay.shelf_y + height + 2 > ATLAS_SIZE)
		{
			posix_ui_font_free(bitmap);
			*atlas_full = 1;
			return NULL;
		}
	}
	entry = &overlay.glyphs[overlay.glyph_count++];
	entry->font = font;
	entry->pixel_size = pixel_size;
	entry->codepoint = codepoint;
	entry->width = (short)width;
	entry->height = (short)height;
	entry->x_offset = (short)x_offset;
	entry->y_offset = (short)y_offset;
	entry->advance = posix_ui_font_advance(font, (float)pixel_size, codepoint, 0);
	entry->atlas_x = (short)overlay.shelf_x;
	entry->atlas_y = (short)overlay.shelf_y;
	if (bitmap && width > 0 && height > 0)
	{
		/* the glyph with a clear texel all around: a quad at a fraction of a
		pixel filters in the texels beside its glyph, which hold whatever was
		there before (a glyph from before the atlas started over, or a new
		texture's undefined texels), drawn as lines at the glyph's sides */
		int bordered_width = width + 2, bordered_height = height + 2, row;
		unsigned char *bordered = calloc((size_t)bordered_width * (size_t)bordered_height, 1);

		if (bordered)
		{
			for (row = 0; row < height; row++)
				memcpy(bordered + (row + 1) * bordered_width + 1, bitmap + row * width, (size_t)width);
#ifdef HALO_BGFX
			if (xgpu_bgfx)
			{
				for (row = 0; row < bordered_height; row++)
					memcpy(overlay.atlas_texels + (overlay.shelf_y - 1 + row) * ATLAS_SIZE + overlay.shelf_x - 1,
						bordered + row * bordered_width, (size_t)bordered_width);
				if (overlay.shelf_y - 1 < overlay.dirty_top)
					overlay.dirty_top = overlay.shelf_y - 1;
				if (overlay.shelf_y + height + 1 > overlay.dirty_bottom)
					overlay.dirty_bottom = overlay.shelf_y + height + 1;
			}
			else
#endif
			{
				glBindTexture(GL_TEXTURE_2D, overlay.atlas);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				glTexSubImage2D(GL_TEXTURE_2D, 0, overlay.shelf_x - 1, overlay.shelf_y - 1, bordered_width,
					bordered_height, GL_RED, GL_UNSIGNED_BYTE, bordered);
			}
			free(bordered);
		}
		overlay.shelf_x += width + 2;
		if (height > overlay.shelf_height)
			overlay.shelf_height = height;
	}
	if (bitmap)
		posix_ui_font_free(bitmap);
	return entry;
}

static void forget_glyphs(void)
{
	overlay.glyph_count = 0;
	overlay.shelf_x = overlay.shelf_y = 2;
	overlay.shelf_height = 0;
}

/* a text's glyphs as quads, in window pixels (scale: window pixels a layout
pixel) */
static void lay_out_text(int index, float scale, float origin_x, float origin_y, int *atlas_full)
{
	const char *text = overlay.text + overlay.texts[index].offset;
	const char *cursor = text;
	int font = overlay.texts[index].font;
	int pixel_size = (int)floorf(overlay.texts[index].size * scale + 0.5f);
	float ascent, descent, pen_x, pen_y, width;
	unsigned int codepoint, previous = 0;

	if (pixel_size < 4 || !posix_ui_font_metrics(font, (float)pixel_size, &ascent, &descent))
		return;
	int previous_font = -1;

	width = measure(font, (float)pixel_size, text);
	pen_x = origin_x + overlay.texts[index].x * scale;
	if (overlay.texts[index].align == UI_ALIGN_CENTER)
		pen_x -= width * 0.5f;
	else if (overlay.texts[index].align == UI_ALIGN_RIGHT)
		pen_x -= width;
	/* (y the line's top: the baseline an ascent below, centred in the size) */
	pen_y = origin_y + overlay.texts[index].y * scale + ((float)pixel_size - (ascent + descent)) * 0.5f + ascent;
	pen_x = floorf(pen_x + 0.5f);
	pen_y = floorf(pen_y + 0.5f);
	while ((codepoint = next_codepoint(&cursor)) != 0)
	{
		struct glyph *entry;
		struct quad *quad;
		int face = glyph_font(font, codepoint);

		if (previous && face == previous_font)
			pen_x += posix_ui_font_advance(face, (float)pixel_size, codepoint, previous) -
				posix_ui_font_advance(face, (float)pixel_size, codepoint, 0);
		entry = glyph(face, pixel_size, codepoint, atlas_full);
		if (!entry)
			return;
		if (entry->width > 0 && (quad = new_quad()) != NULL)
		{
			/* (in window pixels: the caller knows) */
			quad->x = pen_x + entry->x_offset;
			quad->y = pen_y + entry->y_offset;
			quad->width = entry->width;
			quad->height = entry->height;
			quad->top = quad->bottom = overlay.texts[index].color;
			quad->u0 = (float)entry->atlas_x / ATLAS_SIZE;
			quad->v0 = (float)entry->atlas_y / ATLAS_SIZE;
			quad->u1 = (float)(entry->atlas_x + entry->width) / ATLAS_SIZE;
			quad->v1 = (float)(entry->atlas_y + entry->height) / ATLAS_SIZE;
			quad->radius = -1.0f; /* (marks it as in window pixels) */
			quad->thickness = overlay.texts[index].skew;
			quad->layer = overlay.texts[index].layer;
		}
		pen_x += entry->advance;
		previous = codepoint;
		previous_font = face;
	}
}

static void put_vertex(struct vertex *vertex, float x, float y, float u, float v, float local_x, float local_y,
	float half_width, float half_height, float radius, float thickness, unsigned int color)
{
	vertex->x = x;
	vertex->y = y;
	vertex->u = u;
	vertex->v = v;
	vertex->local_x = local_x;
	vertex->local_y = local_y;
	vertex->half_width = half_width;
	vertex->half_height = half_height;
	vertex->radius = radius;
	vertex->thickness = thickness;
	vertex->color[0] = (unsigned char)(color >> 24);
	vertex->color[1] = (unsigned char)(color >> 16);
	vertex->color[2] = (unsigned char)(color >> 8);
	vertex->color[3] = (unsigned char)color;
}

#ifdef HALO_BGFX
/* the quads as triangles in window pixels, each layer's shapes, then its
text, then the next layer's (shape_count of them shapes, the rest glyphs);
how many vertices */
static int build_vertices(int shape_count, float scale, float origin_x, float origin_y)
{
	int index, count = 0, layer, pass;

	for (layer = 0; layer < MAXIMUM_LAYERS; layer++)
	for (pass = 0; pass < 2; pass++)
	for (index = pass ? shape_count : 0; index < (pass ? overlay.quad_count : shape_count) &&
		count + 6 <= MAXIMUM_QUADS * 6; index++)
	{
		const struct quad *quad = &overlay.quads[index];
		struct vertex *v = &overlay.vertices[count];
		float left, top, right, bottom, half_width, half_height, radius, thickness, grow;
		unsigned int top_color = quad->top, bottom_color = quad->bottom;

		if (quad->layer != layer)
			continue;
		if (index < shape_count && (quad->radius == ICON_MARK || quad->radius == PICTURE_MARK ||
			quad->radius == MEDAL_MARK))
		{
			/* an icon: layout to window pixels, its layer in shape.x (the
			picture: the same, marked as the picture's) */
			float layer_index = quad->thickness;
			float mark = quad->radius;

			left = origin_x + quad->x * scale;
			top = origin_y + quad->y * scale;
			right = left + quad->width * scale;
			bottom = top + quad->height * scale;
			put_vertex(&v[0], left, top, quad->u0, quad->v0, layer_index, 0, 0, 0, mark, 0, top_color);
			put_vertex(&v[1], right, top, quad->u1, quad->v0, layer_index, 0, 0, 0, mark, 0, top_color);
			put_vertex(&v[2], left, bottom, quad->u0, quad->v1, layer_index, 0, 0, 0, mark, 0, top_color);
			put_vertex(&v[3], right, top, quad->u1, quad->v0, layer_index, 0, 0, 0, mark, 0, top_color);
			put_vertex(&v[4], right, bottom, quad->u1, quad->v1, layer_index, 0, 0, 0, mark, 0, top_color);
			put_vertex(&v[5], left, bottom, quad->u0, quad->v1, layer_index, 0, 0, 0, mark, 0, top_color);
		}
		else if (index < shape_count && quad->radius == PLASMA_MARK)
		{
			/* the plasma: a shape (its corners' radius in edge.y), with the
			first map's place as its texture coordinates */
			left = origin_x + quad->x * scale;
			top = origin_y + quad->y * scale;
			right = left + quad->width * scale;
			bottom = top + quad->height * scale;
			half_width = (right - left) * 0.5f;
			half_height = (bottom - top) * 0.5f;
			radius = quad->thickness * scale;
			put_vertex(&v[0], left, top, quad->u0, quad->v0, -half_width, -half_height, half_width, half_height, PLASMA_MARK, radius, top_color);
			put_vertex(&v[1], right, top, quad->u1, quad->v0, half_width, -half_height, half_width, half_height, PLASMA_MARK, radius, top_color);
			put_vertex(&v[2], left, bottom, quad->u0, quad->v1, -half_width, half_height, half_width, half_height, PLASMA_MARK, radius, bottom_color);
			put_vertex(&v[3], right, top, quad->u1, quad->v0, half_width, -half_height, half_width, half_height, PLASMA_MARK, radius, top_color);
			put_vertex(&v[4], right, bottom, quad->u1, quad->v1, half_width, half_height, half_width, half_height, PLASMA_MARK, radius, bottom_color);
			put_vertex(&v[5], left, bottom, quad->u0, quad->v1, -half_width, half_height, half_width, half_height, PLASMA_MARK, radius, bottom_color);
		}
		else if (index < shape_count)
		{
			/* a shape: layout to window pixels, a pixel larger all round for
			the smooth edge */
			left = origin_x + quad->x * scale;
			top = origin_y + quad->y * scale;
			right = left + quad->width * scale;
			bottom = top + quad->height * scale;
			half_width = (right - left) * 0.5f;
			half_height = (bottom - top) * 0.5f;
			radius = quad->radius * scale;
			thickness = quad->thickness > 0.0f ? fmaxf(1.0f, quad->thickness * scale) : 0.0f;
			grow = 1.0f;
			put_vertex(&v[0], left - grow, top - grow, -1.0f, 0.0f, -half_width - grow, -half_height - grow, half_width, half_height, radius, thickness, top_color);
			put_vertex(&v[1], right + grow, top - grow, -1.0f, 0.0f, half_width + grow, -half_height - grow, half_width, half_height, radius, thickness, top_color);
			put_vertex(&v[2], left - grow, bottom + grow, -1.0f, 0.0f, -half_width - grow, half_height + grow, half_width, half_height, radius, thickness, bottom_color);
			put_vertex(&v[3], right + grow, top - grow, -1.0f, 0.0f, half_width + grow, -half_height - grow, half_width, half_height, radius, thickness, top_color);
			put_vertex(&v[4], right + grow, bottom + grow, -1.0f, 0.0f, half_width + grow, half_height + grow, half_width, half_height, radius, thickness, bottom_color);
			put_vertex(&v[5], left - grow, bottom + grow, -1.0f, 0.0f, -half_width - grow, half_height + grow, half_width, half_height, radius, thickness, bottom_color);
		}
		else
		{
			/* a glyph, already in window pixels (its top leaning right by
			its skew) */
			float lean = quad->thickness * quad->height;

			left = quad->x;
			top = quad->y;
			right = left + quad->width;
			bottom = top + quad->height;
			put_vertex(&v[0], left + lean, top, quad->u0, quad->v0, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[1], right + lean, top, quad->u1, quad->v0, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[2], left, bottom, quad->u0, quad->v1, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[3], right + lean, top, quad->u1, quad->v0, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[4], right, bottom, quad->u1, quad->v1, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[5], left, bottom, quad->u0, quad->v1, 0, 0, 0, 0, 0, 0, top_color);
		}
		count += 6;
	}
	return count;
}
#endif
#ifdef HALO_CONSOLE_GLES2
/* where the overlay draws: the window, or the game's picture, to be scaled up
with it, on an ES 2 console whose picture is drawn small (d3d8_gl.c) */
static GLuint overlay_target_framebuffer;

/* whether anything is gathered to draw */
int ui_overlay_pending(void)
{
	return overlay.quad_count || overlay.text_count;
}

void ui_overlay_present_to(unsigned int framebuffer, int width, int height)
{
	overlay_target_framebuffer = framebuffer;
	ui_overlay_present(0, 0, width, height, width, height);
	overlay_target_framebuffer = 0;
}
#else
/* (the window's: iOS's is SDL's view's own, 0 elsewhere) */
#define overlay_target_framebuffer platform_window_framebuffer()
#endif

void ui_overlay_present(int x, int y, int width, int height, int window_width, int window_height)
{
	float scale, origin_x, origin_y;
	int shape_count, index, count = 0, atlas_full = 0;

	/* (a frame's cutouts go with it, drawn or not: a minimized window's
	would fill the four and leave old ones cut out once it is shown) */
	overlay.layer = 0;
	if (!overlay.quad_count && !overlay.text_count)
	{
		overlay.cutout_count = 0;
		return;
	}
	/* (the loading screen is drawn over the menus under it, as the game's
	picture: the screens and button prompts those menus gathered here, Online
	Games' and the Chupathingy menus', are not drawn over it) */
	if (progress_bar_is_active() || !set_up() || width <= 0 || height <= 0)
	{
		overlay.quad_count = overlay.text_count = overlay.text_used = 0;
		overlay.cutout_count = 0;
		return;
	}
	/* (the layout's 480 lines fill the picture's height; a wider picture
	has margins either side of its 640 columns, which a screen may reach
	with x below 0 or past 640) */
	scale = (float)height / LAYOUT_HEIGHT;
	if (scale != overlay.last_scale)
	{
		/* (other sizes: the glyphs packed again) */
		forget_glyphs();
		overlay.last_scale = scale;
	}
	/* the picture's top left, in window pixels from the window's top */
	origin_x = (float)x + ((float)width - LAYOUT_WIDTH * scale) * 0.5f;
	origin_y = (float)(window_height - (y + height));
#ifdef HALO_BGFX
	if (xgpu_bgfx)
	{
		/* (the bgfx backend draws: d3d8_bgfx.inl) */
		float cutouts[4][4];
		int cutout;

		shape_count = overlay.quad_count;
		for (index = 0; index < overlay.text_count; index++)
			lay_out_text(index, scale, origin_x, origin_y, &atlas_full);
		count = build_vertices(shape_count, scale, origin_x, origin_y);
		memset(cutouts, 0, sizeof(cutouts));
		for (cutout = 0; cutout < overlay.cutout_count; cutout++)
		{
			cutouts[cutout][0] = origin_x + overlay.cutouts[cutout][0] * scale;
			cutouts[cutout][1] = origin_y + overlay.cutouts[cutout][1] * scale;
			cutouts[cutout][2] = origin_x + overlay.cutouts[cutout][2] * scale;
			cutouts[cutout][3] = origin_y + overlay.cutouts[cutout][3] * scale;
		}
		/* (the medals' atlas let go once bgfx has it) */
		if (xgpu_bgfx_overlay_draw(overlay.vertices, count, (int)sizeof(struct vertex), overlay.atlas_texels, ATLAS_SIZE,
			overlay.dirty_top, overlay.dirty_bottom, overlay.icon_texels, ui_prompt_icon_size, NUMBER_OF_ICONS,
			overlay.picture_pixels, overlay.picture_width, overlay.picture_height, overlay.picture_serial,
			overlay.plasma_drawn ? overlay.plasma_pixels : NULL, overlay.plasma_width, overlay.plasma_height,
			overlay.plasma_serial, overlay.plasma_shift,
#ifdef HALO_MEDAL_ICONS
			overlay.medals_drawn ? medal_texels() : NULL, ui_medal_atlas_width, ui_medal_atlas_height,
#else
			NULL, 0, 0,
#endif
			(const float (*)[4])cutouts, window_width, window_height) && overlay.medal_texels)
		{
			free(overlay.medal_texels);
			overlay.medal_texels = NULL;
			overlay.medals_uploaded = 1;
		}
		overlay.dirty_top = ATLAS_SIZE;
		overlay.dirty_bottom = 0;
		overlay.quad_count = overlay.text_count = overlay.text_used = 0;
		overlay.cutout_count = 0;
		overlay.layer = 0;
		overlay.plasma_drawn = 0;
		overlay.medals_drawn = 0;
		if (atlas_full)
			forget_glyphs();
		return;
	}
#endif

	/* (the game's renderer keeps its bindings across frames: they are put
	back after) */
	GLint saved_vertex_array = 0, saved_array_buffer = 0, saved_program = 0, saved_texture = 0, saved_active = 0;
	/* (the game's renderer binds a sampler to each unit, which would filter
	the atlas as it filters its textures) */
	GLint saved_sampler = 0, saved_icons = 0, saved_icons_sampler = 0, saved_picture = 0, saved_picture_sampler = 0;
	GLint saved_plasma = 0, saved_plasma_sampler = 0, saved_medals = 0, saved_medals_sampler = 0;
	GLint saved_viewport[4] = { 0, 0, 0, 0 };
	GLboolean saved_blend = glIsEnabled(GL_BLEND), saved_depth = glIsEnabled(GL_DEPTH_TEST),
		saved_stencil = glIsEnabled(GL_STENCIL_TEST), saved_cull = glIsEnabled(GL_CULL_FACE),
		saved_scissor = glIsEnabled(GL_SCISSOR_TEST);
	GLint saved_blend_rgb_source = 0, saved_blend_rgb_destination = 0, saved_blend_alpha_source = 0,
		saved_blend_alpha_destination = 0, saved_blend_equation = 0;

	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved_vertex_array);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &saved_array_buffer);
	glGetIntegerv(GL_CURRENT_PROGRAM, &saved_program);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &saved_active);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture);
	glGetIntegerv(GL_SAMPLER_BINDING, &saved_sampler);
	glBindSampler(0, 0);
	glGetIntegerv(GL_VIEWPORT, saved_viewport);
	glGetIntegerv(GL_BLEND_SRC_RGB, &saved_blend_rgb_source);
	glGetIntegerv(GL_BLEND_DST_RGB, &saved_blend_rgb_destination);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &saved_blend_alpha_source);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &saved_blend_alpha_destination);
	glGetIntegerv(GL_BLEND_EQUATION_RGB, &saved_blend_equation);
#ifndef HALO_GL_NO_CLIP_CONTROL
	/* (the renderer's clip space is D3D's, y down (d3d8_gl.c's
	glClipControl); the overlay's is GL's, as on macOS and Android, which have
	no glClipControl) */
	if (glClipControl)
		glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
#endif

	/* the text's glyphs (packing new ones binds the atlas) */
	shape_count = overlay.quad_count;
	for (index = 0; index < overlay.text_count; index++)
		lay_out_text(index, scale, origin_x, origin_y, &atlas_full);

	/* each layer's shapes, then its text, then the next layer's */
	{
	int layer, pass;

	for (layer = 0; layer < MAXIMUM_LAYERS; layer++)
	for (pass = 0; pass < 2; pass++)
	for (index = pass ? shape_count : 0; index < (pass ? overlay.quad_count : shape_count) &&
		count + 6 <= MAXIMUM_QUADS * 6; index++)
	{
		const struct quad *quad = &overlay.quads[index];
		struct vertex *v = &overlay.vertices[count];
		float left, top, right, bottom, half_width, half_height, radius, thickness, grow;
		unsigned int top_color = quad->top, bottom_color = quad->bottom;

		if (quad->layer != layer)
			continue;
		if (index < shape_count && (quad->radius == ICON_MARK || quad->radius == PICTURE_MARK ||
			quad->radius == MEDAL_MARK))
		{
			/* an icon: layout to window pixels, its layer in shape.x (the
			picture: the same, marked as the picture's) */
			float layer_index = quad->thickness;
			float mark = quad->radius;

			left = origin_x + quad->x * scale;
			top = origin_y + quad->y * scale;
			right = left + quad->width * scale;
			bottom = top + quad->height * scale;
			put_vertex(&v[0], left, top, quad->u0, quad->v0, layer_index, 0, 0, 0, mark, 0, top_color);
			put_vertex(&v[1], right, top, quad->u1, quad->v0, layer_index, 0, 0, 0, mark, 0, top_color);
			put_vertex(&v[2], left, bottom, quad->u0, quad->v1, layer_index, 0, 0, 0, mark, 0, top_color);
			put_vertex(&v[3], right, top, quad->u1, quad->v0, layer_index, 0, 0, 0, mark, 0, top_color);
			put_vertex(&v[4], right, bottom, quad->u1, quad->v1, layer_index, 0, 0, 0, mark, 0, top_color);
			put_vertex(&v[5], left, bottom, quad->u0, quad->v1, layer_index, 0, 0, 0, mark, 0, top_color);
		}
		else if (index < shape_count && quad->radius == PLASMA_MARK)
		{
			/* the plasma: a shape (its corners' radius in edge.y), with the
			first map's place as its texture coordinates */
			left = origin_x + quad->x * scale;
			top = origin_y + quad->y * scale;
			right = left + quad->width * scale;
			bottom = top + quad->height * scale;
			half_width = (right - left) * 0.5f;
			half_height = (bottom - top) * 0.5f;
			radius = quad->thickness * scale;
			put_vertex(&v[0], left, top, quad->u0, quad->v0, -half_width, -half_height, half_width, half_height, PLASMA_MARK, radius, top_color);
			put_vertex(&v[1], right, top, quad->u1, quad->v0, half_width, -half_height, half_width, half_height, PLASMA_MARK, radius, top_color);
			put_vertex(&v[2], left, bottom, quad->u0, quad->v1, -half_width, half_height, half_width, half_height, PLASMA_MARK, radius, bottom_color);
			put_vertex(&v[3], right, top, quad->u1, quad->v0, half_width, -half_height, half_width, half_height, PLASMA_MARK, radius, top_color);
			put_vertex(&v[4], right, bottom, quad->u1, quad->v1, half_width, half_height, half_width, half_height, PLASMA_MARK, radius, bottom_color);
			put_vertex(&v[5], left, bottom, quad->u0, quad->v1, -half_width, half_height, half_width, half_height, PLASMA_MARK, radius, bottom_color);
		}
		else if (index < shape_count)
		{
			/* a shape: layout to window pixels, a pixel larger all round for
			the smooth edge */
			left = origin_x + quad->x * scale;
			top = origin_y + quad->y * scale;
			right = left + quad->width * scale;
			bottom = top + quad->height * scale;
			half_width = (right - left) * 0.5f;
			half_height = (bottom - top) * 0.5f;
			radius = quad->radius * scale;
			thickness = quad->thickness > 0.0f ? fmaxf(1.0f, quad->thickness * scale) : 0.0f;
			grow = 1.0f;
			put_vertex(&v[0], left - grow, top - grow, -1.0f, 0.0f, -half_width - grow, -half_height - grow, half_width, half_height, radius, thickness, top_color);
			put_vertex(&v[1], right + grow, top - grow, -1.0f, 0.0f, half_width + grow, -half_height - grow, half_width, half_height, radius, thickness, top_color);
			put_vertex(&v[2], left - grow, bottom + grow, -1.0f, 0.0f, -half_width - grow, half_height + grow, half_width, half_height, radius, thickness, bottom_color);
			put_vertex(&v[3], right + grow, top - grow, -1.0f, 0.0f, half_width + grow, -half_height - grow, half_width, half_height, radius, thickness, top_color);
			put_vertex(&v[4], right + grow, bottom + grow, -1.0f, 0.0f, half_width + grow, half_height + grow, half_width, half_height, radius, thickness, bottom_color);
			put_vertex(&v[5], left - grow, bottom + grow, -1.0f, 0.0f, -half_width - grow, half_height + grow, half_width, half_height, radius, thickness, bottom_color);
		}
		else
		{
			/* a glyph, already in window pixels (its top leaning right by
			its skew) */
			float lean = quad->thickness * quad->height;

			left = quad->x;
			top = quad->y;
			right = left + quad->width;
			bottom = top + quad->height;
			put_vertex(&v[0], left + lean, top, quad->u0, quad->v0, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[1], right + lean, top, quad->u1, quad->v0, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[2], left, bottom, quad->u0, quad->v1, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[3], right + lean, top, quad->u1, quad->v0, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[4], right, bottom, quad->u1, quad->v1, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[5], left, bottom, quad->u0, quad->v1, 0, 0, 0, 0, 0, 0, top_color);
		}
		count += 6;
	}
	}

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, overlay_target_framebuffer);
	glViewport(0, 0, window_width, window_height);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CULL_FACE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(overlay.program);
	glUniform2f(overlay.scale_location, 2.0f / (float)window_width, -2.0f / (float)window_height);
#ifdef HALO_CONSOLE_GLES2
	/* (a render target's row 0 is the top of the picture; the window's, its
	bottom) */
	glUniform1f(overlay.y_flip_location, overlay_target_framebuffer ? -1.0f : 1.0f);
#endif
	glActiveTexture(GL_TEXTURE1);
	glGetIntegerv(ICONS_BINDING, &saved_icons);
	glGetIntegerv(GL_SAMPLER_BINDING, &saved_icons_sampler);
	glBindSampler(1, 0);
	glBindTexture(ICONS_TARGET, overlay.icons);
	glUniform1i(overlay.icons_location, 1);
#ifdef HALO_CONSOLE_GLES2
	glUniform1f(overlay.icon_count_location, (float)NUMBER_OF_ICONS);
#endif
	glActiveTexture(GL_TEXTURE2);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_picture);
	glGetIntegerv(GL_SAMPLER_BINDING, &saved_picture_sampler);
	glBindSampler(2, 0);
	glBindTexture(GL_TEXTURE_2D, overlay.picture);
	if (overlay.picture_serial != overlay.picture_uploaded && overlay.picture_pixels)
	{
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, overlay.picture_width, overlay.picture_height, 0, GL_RGBA,
			GL_UNSIGNED_BYTE, overlay.picture_pixels);
		overlay.picture_uploaded = overlay.picture_serial;
	}
	glUniform1i(overlay.picture_location, 2);
	/* the plasma's noise map, on the fourth unit (only while a plasma is
	drawn: the Guide's, nothing when it is closed) */
	glActiveTexture(GL_TEXTURE3);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_plasma);
	glGetIntegerv(GL_SAMPLER_BINDING, &saved_plasma_sampler);
	glBindSampler(3, 0);
	glBindTexture(GL_TEXTURE_2D, overlay.plasma);
	if (overlay.plasma_drawn && overlay.plasma_pixels && overlay.plasma_serial != overlay.plasma_uploaded)
	{
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, overlay.plasma_width, overlay.plasma_height, 0, GL_RGBA,
			GL_UNSIGNED_BYTE, overlay.plasma_pixels);
		overlay.plasma_uploaded = overlay.plasma_serial;
	}
	glUniform1i(overlay.plasma_location, 3);
	/* the medals' atlas, on the fifth (filled when a medal is first drawn,
	with smaller levels but for ES 2's, which may not be a power of two) */
	glActiveTexture(GL_TEXTURE4);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_medals);
	glGetIntegerv(GL_SAMPLER_BINDING, &saved_medals_sampler);
	glBindSampler(4, 0);
	glBindTexture(GL_TEXTURE_2D, overlay.medals);
#ifdef HALO_MEDAL_ICONS
	if (overlay.medals_drawn && !overlay.medals_uploaded && medal_texels())
	{
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ui_medal_atlas_width, ui_medal_atlas_height, 0, GL_RGBA,
			GL_UNSIGNED_BYTE, overlay.medal_texels);
#ifndef HALO_CONSOLE_GLES2
		/* (a few levels: the cells' margins keep them apart that far) */
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
#endif
		free(overlay.medal_texels);
		overlay.medal_texels = NULL;
		overlay.medals_uploaded = 1;
	}
#endif
	glUniform1i(overlay.medals_location, 4);
	glUniform4fv(overlay.plasma_shift_location, 1, overlay.plasma_shift);
	glUniform4fv(overlay.plasma_thread_location, 1, overlay.plasma_shift + 4);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, overlay.atlas);
	glUniform1i(overlay.atlas_location, 0);
	{
		float cutouts[4][4];
		int cutout;

		memset(cutouts, 0, sizeof(cutouts));
		for (cutout = 0; cutout < overlay.cutout_count; cutout++)
		{
			cutouts[cutout][0] = origin_x + overlay.cutouts[cutout][0] * scale;
			cutouts[cutout][1] = origin_y + overlay.cutouts[cutout][1] * scale;
			cutouts[cutout][2] = origin_x + overlay.cutouts[cutout][2] * scale;
			cutouts[cutout][3] = origin_y + overlay.cutouts[cutout][3] * scale;
		}
		glUniform4fv(overlay.cutouts_location, 4, &cutouts[0][0]);
		glUniform1f(overlay.window_height_location, (float)window_height);
	}
	glBindVertexArray(overlay.vertex_array);
	glBindBuffer(GL_ARRAY_BUFFER, overlay.vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(overlay.vertices), NULL, GL_STREAM_DRAW);
	glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)count * (GLsizeiptr)sizeof(struct vertex), overlay.vertices);
	glDrawArrays(GL_TRIANGLES, 0, count);

	glBindVertexArray((GLuint)saved_vertex_array);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)saved_array_buffer);
	glUseProgram((GLuint)saved_program);
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, (GLuint)saved_medals);
	glBindSampler(4, (GLuint)saved_medals_sampler);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, (GLuint)saved_plasma);
	glBindSampler(3, (GLuint)saved_plasma_sampler);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, (GLuint)saved_picture);
	glBindSampler(2, (GLuint)saved_picture_sampler);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(ICONS_TARGET, (GLuint)saved_icons);
	glBindSampler(1, (GLuint)saved_icons_sampler);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, (GLuint)saved_texture);
	glBindSampler(0, (GLuint)saved_sampler);
	glActiveTexture((GLenum)saved_active);
	glViewport(saved_viewport[0], saved_viewport[1], saved_viewport[2], saved_viewport[3]);
#ifndef HALO_GL_NO_CLIP_CONTROL
	if (glClipControl)
		glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
#endif
	glBlendFuncSeparate((GLenum)saved_blend_rgb_source, (GLenum)saved_blend_rgb_destination,
		(GLenum)saved_blend_alpha_source, (GLenum)saved_blend_alpha_destination);
	glBlendEquation((GLenum)saved_blend_equation);
	(saved_blend ? glEnable : glDisable)(GL_BLEND);
	(saved_depth ? glEnable : glDisable)(GL_DEPTH_TEST);
	(saved_stencil ? glEnable : glDisable)(GL_STENCIL_TEST);
	(saved_cull ? glEnable : glDisable)(GL_CULL_FACE);
	(saved_scissor ? glEnable : glDisable)(GL_SCISSOR_TEST);

	overlay.quad_count = overlay.text_count = overlay.text_used = 0;
	overlay.cutout_count = 0;
	overlay.layer = 0;
	overlay.plasma_drawn = 0;
	overlay.medals_drawn = 0;
	if (atlas_full)
		forget_glyphs();
}

#endif
