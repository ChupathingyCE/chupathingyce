/*
UI_OVERLAY.C

The overlay's drawing (ui_overlay.h): shapes and text gathered while the
game draws a frame, drawn at its Present at the window's resolution, over
the game's picture.

Each shape is a quad whose fragment shader works out a rounded rectangle's
edge (filled, or its outline) from the distance to it, so corners and lines
are smooth at any size; text is glyphs (posix_ui_font.c) packed into one
atlas texture as they are first drawn at a size, and drawn as quads too.
*/

#ifdef HALO_GAME_BROWSER

#include "platform.h"
#include "gl.h"
#include "xgpu.h"
#include "ui_font.h"
#include "ui_overlay.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
	GLuint program, vertex_array, vertex_buffer, atlas;
	GLint scale_location, atlas_location, cutouts_location, window_height_location;

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

	struct vertex vertices[MAXIMUM_QUADS * 6];
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

	while ((codepoint = next_codepoint(&cursor)) != 0)
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
	int length = (int)strlen(text);
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
	"A", "B", "X", "Y", "Start", "LT", "RT", "LB", "RB", "<", ">",
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

/* ---------- GL */

static const char vertex_source[] =
	"in vec2 position;\n"
	"in vec2 texture_coordinate;\n"
	"in vec4 shape;\n"
	"in vec2 edge;\n"
	"in vec4 color;\n"
	"uniform vec2 scale;\n"
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
	"}\n";

static const char fragment_source[] =
	"in vec2 v_texture_coordinate;\n"
	"in vec4 v_shape;\n"
	"in vec2 v_edge;\n"
	"in vec4 v_color;\n"
	"uniform sampler2D atlas;\n"
	"uniform vec4 cutouts[4];\n"
	"uniform float window_height;\n"
	"out vec4 fragment;\n"
	"void main()\n"
	"{\n"
	"\tfloat alpha;\n"
	/* (the game's own pictures show through: x0, y0, x1, y1 in window pixels
	from the top) */
	"\tvec2 p = vec2(gl_FragCoord.x, window_height - gl_FragCoord.y);\n"
	"\tfor (int i = 0; i < 4; i++)\n"
	"\t\tif (p.x >= cutouts[i].x && p.y >= cutouts[i].y && p.x < cutouts[i].z && p.y < cutouts[i].w)\n"
	"\t\t\tdiscard;\n"
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
	char source[4096];
	GLuint shader = glCreateShader(type);
	const char *pointer = source;
	GLint status = 0;

#ifdef HALO_ANDROID
	snprintf(source, sizeof(source), "#version %s\nprecision highp float;\n%s", xgpu_capabilities.shading_language,
		body);
#elif defined(__APPLE__)
	snprintf(source, sizeof(source), "#version 410 core\n%s", body);
#else
	snprintf(source, sizeof(source), "#version 450 core\n%s", body);
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

	if (overlay.ready || overlay.failed)
		return overlay.ready;
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
		glBindTexture(GL_TEXTURE_2D, overlay.atlas);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, overlay.shelf_x, overlay.shelf_y, width, height, GL_RED, GL_UNSIGNED_BYTE,
			bitmap);
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

static void put_vertex(struct vertex *vertex, float x, float y, float u, float v, float local_x, float local_y,
	const struct quad *quad, float half_width, float half_height, float radius, float thickness, unsigned int color)
{
	(void)quad;
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

void ui_overlay_present(int x, int y, int width, int height, int window_width, int window_height)
{
	float scale, origin_x, origin_y;
	int shape_count, index, count = 0, atlas_full = 0;

	if (!overlay.quad_count && !overlay.text_count)
		return;
	if (!set_up() || width <= 0 || height <= 0)
	{
		overlay.quad_count = overlay.text_count = overlay.text_used = 0;
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

	/* (the game's renderer keeps its bindings across frames: they are put
	back after) */
	GLint saved_vertex_array = 0, saved_array_buffer = 0, saved_program = 0, saved_texture = 0, saved_active = 0;
	/* (the game's renderer binds a sampler to each unit, which would filter
	the atlas as it filters its textures) */
	GLint saved_sampler = 0;
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

	for (index = 0; index < overlay.quad_count && count + 6 <= MAXIMUM_QUADS * 6; index++)
	{
		const struct quad *quad = &overlay.quads[index];
		struct vertex *v = &overlay.vertices[count];
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
			put_vertex(&v[0], left - grow, top - grow, -1.0f, 0.0f, -half_width - grow, -half_height - grow, quad, half_width, half_height, radius, thickness, top_color);
			put_vertex(&v[1], right + grow, top - grow, -1.0f, 0.0f, half_width + grow, -half_height - grow, quad, half_width, half_height, radius, thickness, top_color);
			put_vertex(&v[2], left - grow, bottom + grow, -1.0f, 0.0f, -half_width - grow, half_height + grow, quad, half_width, half_height, radius, thickness, bottom_color);
			put_vertex(&v[3], right + grow, top - grow, -1.0f, 0.0f, half_width + grow, -half_height - grow, quad, half_width, half_height, radius, thickness, top_color);
			put_vertex(&v[4], right + grow, bottom + grow, -1.0f, 0.0f, half_width + grow, half_height + grow, quad, half_width, half_height, radius, thickness, bottom_color);
			put_vertex(&v[5], left - grow, bottom + grow, -1.0f, 0.0f, -half_width - grow, half_height + grow, quad, half_width, half_height, radius, thickness, bottom_color);
		}
		else
		{
			/* a glyph, already in window pixels */
			left = quad->x;
			top = quad->y;
			right = left + quad->width;
			bottom = top + quad->height;
			put_vertex(&v[0], left, top, quad->u0, quad->v0, 0, 0, quad, 0, 0, 0, 0, top_color);
			put_vertex(&v[1], right, top, quad->u1, quad->v0, 0, 0, quad, 0, 0, 0, 0, top_color);
			put_vertex(&v[2], left, bottom, quad->u0, quad->v1, 0, 0, quad, 0, 0, 0, 0, top_color);
			put_vertex(&v[3], right, top, quad->u1, quad->v0, 0, 0, quad, 0, 0, 0, 0, top_color);
			put_vertex(&v[4], right, bottom, quad->u1, quad->v1, 0, 0, quad, 0, 0, 0, 0, top_color);
			put_vertex(&v[5], left, bottom, quad->u0, quad->v1, 0, 0, quad, 0, 0, 0, 0, top_color);
		}
		count += 6;
	}


	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
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
	if (atlas_full)
		forget_glyphs();
}

#endif
