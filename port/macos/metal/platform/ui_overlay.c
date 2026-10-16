/*
UI_OVERLAY.C — macOS-only native Metal adaptation

The overlay's drawing (ui_overlay.h): shapes and text gathered while the
game draws a frame, drawn into its Metal back buffer at Present, over
the game's picture.

Each shape is a quad whose fragment shader works out a rounded rectangle's
edge (filled, or its outline) from the distance to it, so corners and lines
are smooth at any size; text is glyphs (posix_ui_font.c) packed into one
atlas texture as they are first drawn at a size, and drawn as quads too.
*/

#include "metal_overlay.h"

#ifdef HALO_GAME_BROWSER

#include "ui_font.h"
#include "ui_overlay.h"

#include <math.h>
#include <string.h>

/* (xinput_sdl.c: the device the player last used: 0 keyboard, 1 Xbox-like,
2 PlayStation, 3 Nintendo) */
int platform_input_scheme(void);

enum
{
	LAYOUT_WIDTH = 640,
	LAYOUT_HEIGHT = 480,
	MAXIMUM_QUADS = 8192,
	ATLAS_SIZE = 2048,
	MAXIMUM_GLYPHS = 4096,
	MAXIMUM_TEXT = 64 * 1024,
};

/* a quad as gathered (layout coordinates) */
struct quad
{
	float x, y, width, height;
	float radius, thickness;
	unsigned int top, bottom;
	/* a glyph's place in the atlas (u0 < 0: a shape) */
	float u0, v0, u1, v1;
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
	unsigned char atlas[ATLAS_SIZE * ATLAS_SIZE];
	int atlas_dirty;

	/* the places the game draws its own pictures into (layout coordinates) */
	float cutouts[4][4];
	int cutout_count;

	struct quad quads[MAXIMUM_QUADS];
	int quad_count;
	/* (text is laid out at Present, when the window's scale is known) */
	struct
	{
		int font, align;
		float size, x, y;
		unsigned int color;
		int offset;
	} texts[1024];
	int text_count;
	char text[MAXIMUM_TEXT];
	int text_used;

	struct glyph glyphs[MAXIMUM_GLYPHS];
	int glyph_count;
	int shelf_x, shelf_y, shelf_height;
	float last_scale;

	struct metal_overlay_vertex vertices[MAXIMUM_QUADS * 6];
} overlay;

/* ---------- the device's buttons */

static const unsigned int button_glyphs[4][NUMBER_OF_UI_BUTTONS] =
{
	/* keyboard: Enter, Backspace, E, Tab, Esc, Q, X, Q, C (the game's keys
	for them, xinput_sdl.c; C is Online Games' Link Profile), Left, Right,
	F1 */
	{ 0xE05E, 0xE038, 0xE05A, 0xE0D1, 0xE062, 0xE0B3, 0xE0E3, 0xE0B3, 0xE046, 0xE020, 0xE022, 0xE067 },
	/* Xbox: A, B, X, Y, menu, LT, RT, LB, RB, -, -, view */
	{ 0xE004, 0xE006, 0xE01E, 0xE020, 0xE014, 0xE047, 0xE04D, 0xE043, 0xE049, 0, 0, 0xE01C },
	/* PlayStation: cross, circle, square, triangle, options, L2, R2, L1, R1,
	-, -, share */
	{ 0xE04B, 0xE041, 0xE051, 0xE053, 0xE009, 0xE07D, 0xE085, 0xE078, 0xE080, 0, 0, 0xE00B },
	/* Nintendo: its east and south buttons are A and B where Xbox's are B
	and A; L and R for both; plus and minus */
	{ 0xE005, 0xE007, 0xE019, 0xE017, 0xE00F, 0xE00B, 0xE013, 0xE00B, 0xE013, 0, 0, 0xE00D },
};

static const int button_fonts[4] =
{
	POSIX_UI_FONT_KEYBOARD, POSIX_UI_FONT_XBOX, POSIX_UI_FONT_PLAYSTATION, POSIX_UI_FONT_NINTENDO,
};

static int scheme(void)
{
	int value = platform_input_scheme();

	return value >= 0 && value < 4 ? value : 1;
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
	return quad;
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
	return font == UI_FONT_BOLD ? POSIX_UI_FONT_BOLD : POSIX_UI_FONT_REGULAR;
}

/* a string's width in the layout at a size: measured at the layout's own
scale (Present lays it out again at the window's) */
static float measure(int font, float size, const char *text)
{
	const char *cursor = text;
	unsigned int codepoint, previous = 0;
	float width = 0.0f;

	for (int count = 0; count < MAXIMUM_TEXT && (codepoint = next_codepoint(&cursor)) != 0; count++)
	{
		width += posix_ui_font_advance(font, size, codepoint, previous);
		previous = codepoint;
	}
	return width;
}

float ui_overlay_text_width(int font, float size, const char *text)
{
	return text ? measure(posix_font(font), size, text) : 0.0f;
}

static float add_text(int font, float size, float x, float y, int align, unsigned int color, const char *text)
{
	int length = (int)strnlen(text, MAXIMUM_TEXT);
	float width = measure(font, size, text);

	if (overlay.text_count >= (int)(sizeof(overlay.texts) / sizeof(overlay.texts[0])) ||
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
	overlay.texts[overlay.text_count].offset = overlay.text_used;
	memcpy(overlay.text + overlay.text_used, text, (size_t)length + 1);
	overlay.text_used += length + 1;
	overlay.text_count++;
	return width;
}

float ui_overlay_text(int font, float size, float x, float y, int align, unsigned int color, const char *text)
{
	return text && *text ? add_text(posix_font(font), size, x, y, align, color, text) : 0.0f;
}

static int utf8(unsigned int codepoint, char *out)
{
	out[0] = (char)(0xE0 | (codepoint >> 12));
	out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
	out[2] = (char)(0x80 | (codepoint & 0x3F));
	out[3] = 0;
	return 3;
}

static const char *const button_words[NUMBER_OF_UI_BUTTONS] =
{
	"A", "B", "X", "Y", "Start", "LT", "RT", "LB", "RB", "<", ">", "Back"
};

float ui_overlay_button_width(int button, float size)
{
	int device = scheme();
	char text[4];

	if (button < 0 || button >= NUMBER_OF_UI_BUTTONS)
		return 0.0f;
	if (!button_glyphs[device][button])
		return measure(POSIX_UI_FONT_BOLD, size, button_words[button]);
	utf8(button_glyphs[device][button], text);
	return measure(button_fonts[device], size, text);
}

float ui_overlay_button(int button, float size, float x, float y, unsigned int color)
{
	int device = scheme();
	char text[4];

	if (button < 0 || button >= NUMBER_OF_UI_BUTTONS)
		return 0.0f;
	/* (a button the device's font lacks: its name) */
	if (!button_glyphs[device][button] || !posix_ui_font_has(button_fonts[device], button_glyphs[device][button]))
		return add_text(POSIX_UI_FONT_BOLD, size, x, y, UI_ALIGN_LEFT, color, button_words[button]);
	utf8(button_glyphs[device][button], text);
	return add_text(button_fonts[device], size, x, y, UI_ALIGN_LEFT, color, text);
}

/* ---------- bounded CPU atlas and layout (uploaded by native Metal) */
static int set_up(void)
{
    if (!overlay.ready)
    {
        overlay.shelf_x = overlay.shelf_y = 2;
        overlay.atlas_dirty = 1;
        overlay.ready = 1;
    }
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
	if (width < 0 || height < 0 || width > ATLAS_SIZE - 4 || height > ATLAS_SIZE - 4)
    {
        if (bitmap) posix_ui_font_free(bitmap);
        *atlas_full = 1;
        return NULL;
    }
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
        for (int row = 0; row < height; row++)
            memcpy(overlay.atlas + (overlay.shelf_y + row) * ATLAS_SIZE + overlay.shelf_x,
                bitmap + row * width, (size_t)width);
        overlay.atlas_dirty = 1;
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
	float pixels = floorf(overlay.texts[index].size * scale + 0.5f);
    int pixel_size;
    if (!isfinite(pixels) || pixels < 4 || pixels > 1024)
        return;
    pixel_size = (int)pixels;
	float ascent, descent, pen_x, pen_y, width;
	unsigned int codepoint, previous = 0;

	if (pixel_size < 4 || !posix_ui_font_metrics(font, (float)pixel_size, &ascent, &descent))
		return;
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

		if (previous)
			pen_x += posix_ui_font_advance(font, (float)pixel_size, codepoint, previous) -
				posix_ui_font_advance(font, (float)pixel_size, codepoint, 0);
		entry = glyph(font, pixel_size, codepoint, atlas_full);
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
		}
		pen_x += entry->advance;
		previous = codepoint;
	}
}

static void put_vertex(struct metal_overlay_vertex *vertex, float x, float y, float u, float v, float local_x, float local_y,
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

void metal_overlay_prepare(int width, int height, struct metal_overlay_frame *frame)
{
	float scale, origin_x, origin_y;
	int shape_count, index, count = 0, atlas_full = 0;

    memset(frame, 0, sizeof(*frame));

	/* (a frame's cutouts go with it, drawn or not: a minimized window's
	would fill the four and leave old ones cut out once it is shown) */
	if (!overlay.quad_count && !overlay.text_count)
	{
		overlay.cutout_count = 0;
		return;
	}
	if (width <= 0 || height <= 0 || !set_up())
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
	origin_x = ((float)width - LAYOUT_WIDTH * scale) * 0.5f;
	origin_y = 0.0f;

	/* Pack the text's glyphs before building its vertices. */
	shape_count = overlay.quad_count;
	for (index = 0; index < overlay.text_count; index++)
		lay_out_text(index, scale, origin_x, origin_y, &atlas_full);

	for (index = 0; index < overlay.quad_count && count + 6 <= MAXIMUM_QUADS * 6; index++)
	{
		const struct quad *quad = &overlay.quads[index];
		struct metal_overlay_vertex *v = &overlay.vertices[count];
		float left, top, right, bottom, half_width, half_height, radius, thickness, grow;
		unsigned int top_color = quad->top, bottom_color = quad->bottom;

		if (index < shape_count)
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
			/* a glyph, already in window pixels */
			left = quad->x;
			top = quad->y;
			right = left + quad->width;
			bottom = top + quad->height;
			put_vertex(&v[0], left, top, quad->u0, quad->v0, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[1], right, top, quad->u1, quad->v0, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[2], left, bottom, quad->u0, quad->v1, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[3], right, top, quad->u1, quad->v0, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[4], right, bottom, quad->u1, quad->v1, 0, 0, 0, 0, 0, 0, top_color);
			put_vertex(&v[5], left, bottom, quad->u0, quad->v1, 0, 0, 0, 0, 0, 0, top_color);
		}
		count += 6;
	}

    frame->vertices = overlay.vertices;
    frame->vertex_count = (uint32_t)count;
    frame->atlas = overlay.atlas;
    frame->atlas_dirty = (uint32_t)overlay.atlas_dirty;
    overlay.atlas_dirty = 0;
    for (int cutout = 0; cutout < overlay.cutout_count; cutout++)
    {
        frame->cutouts[cutout][0] = origin_x + overlay.cutouts[cutout][0] * scale;
        frame->cutouts[cutout][1] = origin_y + overlay.cutouts[cutout][1] * scale;
        frame->cutouts[cutout][2] = origin_x + overlay.cutouts[cutout][2] * scale;
        frame->cutouts[cutout][3] = origin_y + overlay.cutouts[cutout][3] * scale;
    }
	overlay.quad_count = overlay.text_count = overlay.text_used = 0;
	overlay.cutout_count = 0;
	if (atlas_full)
		forget_glyphs();
}

#else
void metal_overlay_prepare(int width, int height, struct metal_overlay_frame *frame)
{
    (void)width; (void)height;
    *frame = (struct metal_overlay_frame){0};
}
#endif
