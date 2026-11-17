/*
TEXT_HIRES.C

The game's text drawn with fonts at the display's resolution (text_hires.h).

Each font tag the fonts draw (port/assets/fonts/fonts.json) gets its font
sized so that its capitals are as tall as the tag's: the game's layout,
from the tag's character widths, is kept, and the fonts were chosen to fit
the Xbox maps' widths. A tag whose letters are narrower than its font's (a
Custom Edition loc.map's) gets the font narrowed until its letters are, on
the whole, as wide as the tag's, so that they do not run into each other.
The GL texture the game's placeholder bitmap stands for (xbox_textures.c)
is the atlas, its new rows uploaded when the placeholder is bound. It holds
one of two kinds of glyph:

- Distance fields (display.high_res_text_fields, the default): each glyph's
  multi-channel signed distance field (port/third_party/msdfgen/msdf_gen.h)
  is made once, at a fixed size (FIELD_EM texels to the em), and the pixel
  shader draws its edge sharp at whatever size the display gives it
  (nv2a_psh.c, from the key's text_fields: text_hires_atlas_fields). A
  font's ASCII and Latin-1 glyphs are made ahead on a worker thread as the
  font is loaded; any other glyph is made as it is first drawn, within a
  budget of the frame's time (FIELDS_FRAME_BUDGET_US), or else by the
  worker, the glyph drawn rasterized (a stand-in in the same atlas) until
  its field is there. A glyph whose field cannot be made (an outline of
  more edges than the generator holds) keeps its stand-in. A field is
  shared by every font tag drawn with the same font file; a change of the
  display's size changes nothing, and a full atlas starts it over. Based
  on work by goatbutt and the OpenH1 team (their text_hires.c and
  msdf_gen.h).
- Rasterized glyphs (display.high_res_text_fields false, and a console's
  own renderer): a glyph is rasterized when first drawn, at the display's
  pixels per unit of the 480 lines, into an 8-bit atlas. A change of the
  display's scale (a window made fullscreen) or a full atlas starts it over.

Either way the glyph's quad, advance and ink are the font's box for it at
the display's pixels (stbtt_GetGlyphBitmapBox), so the text is laid out and
cut the same.

The console's lines are drawn in a font of their own, the "console" role's
(rasterizer_text_set_console_font, while terminal.c draws them), sized and
fitted to the console's font tag in the same way.

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
#include "halo_delta_mark.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"

/* (a console's own renderer draws the atlas without the fields' decode:
halo_port_features.h) */
#if !defined(HALO_CONSOLE_NO_TEXT_FIELDS) && !defined(HALO_CONSOLE_HOST_ART)
#define TEXT_FIELDS 1
#include "../../third_party/msdfgen/msdf_gen.h"
#endif

/* (the Xbox maps' tags and a Custom Edition loc.map's, which share names,
each at its oversamples) */
#define MAXIMUM_FONTS 16
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
	/* the tag's letters' widths (text_hires_font), which it is narrowed to,
	and their sum */
	short letter_widths[TEXT_HIRES_FIT_LETTERS];
	long letters_width;
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
/* the rows of the atlas (either kind's) written since they were last
uploaded: [dirty_top, dirty_bottom) */
static int dirty_top = ATLAS_SIZE, dirty_bottom;
static unsigned long placeholder_data, placeholder_width, placeholder_height;
static GLuint atlas_texture;
/* whether the texture holds the fields (made again when the kind changes) */
static int atlas_texture_fields;

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

/* whether the glyphs are drawn from distance fields now
(display.high_res_text_fields; never on a console's own renderer) */
static int fields_wanted(void)
{
#ifdef TEXT_FIELDS
	static int wanted;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		wanted = config_boolean("display.high_res_text_fields");
	}
	return wanted;
#else
	return 0;
#endif
}

#ifdef TEXT_FIELDS
static void fields_reset(void);
static void fields_drain(void);
static void fields_prewarm(long font);
#endif

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

/* the slot's font narrowed where its letters are, summed, wider than its
tag's (a Custom Edition loc.map's fonts are narrower than the Xbox maps'),
so that they do not run into each other; returns how wide it is drawn */
static float font_fit(long font)
{
	float tag_width = 0.0f;
	float font_width = 0.0f;
	int letter;

	for (letter = 0; letter < TEXT_HIRES_FIT_LETTERS; letter++)
	{
		int glyph = stbtt_FindGlyphIndex(&fonts[font].info, letter < 26 ? 'A' + letter : 'a' + letter - 26);
		int advance = 0, bearing = 0;

		if (fonts[font].letter_widths[letter] <= 0 || !glyph)
			continue;
		stbtt_GetGlyphHMetrics(&fonts[font].info, glyph, &advance, &bearing);
		tag_width += fonts[font].letter_widths[letter];
		font_width += advance * fonts[font].scale * fonts[font].width;
	}
	if (tag_width > 0.0f && font_width > tag_width)
		fonts[font].width *= tag_width / font_width;
	return fonts[font].width;
}

/* the font of a slot read (again), from the player's file for its role or
else its built-in one: its scale 0 when it has none */
static void font_load_face(long font)
{
	const struct text_hires_embedded *embedded = embedded_font(fonts[font].tag);
	int large, x0, y0, x1, y1;

	fonts[font].scale = 0.0f;
	if (!embedded)
		return;
	large = embedded->role && !strcmp(embedded->role, "large");
	fonts[font].width = embedded->width > 0.0f ? embedded->width : 1.0f;
	/* (the console's font, role "console", is always the built-in one: the
	player's fonts stand in for the menus' and the HUD's text) */
	if (player_fonts[large].data && !(embedded->role && !strcmp(embedded->role, "console")))
	{
		const unsigned char *data = player_fonts[large].data;

		if (stbtt_InitFont(&fonts[font].info, data, stbtt_GetFontOffsetForIndex(data, 0)) &&
			stbtt_GetCodepointBox(&fonts[font].info, 'H', &x0, &y0, &x1, &y1) && y1 > y0)
		{
			/* (a player's font as it is drawn: its own width) */
			fonts[font].width = 1.0f;
			fonts[font].scale = fonts[font].cap_height / (float)(y1 - y0);
			platform_log("high-res text: %s (capitals %.0f units) drawn with %s, %.0f%% wide", fonts[font].tag,
				fonts[font].cap_height, player_fonts[large].path, font_fit(font) * 100.0f);
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
		platform_log("high-res text: %s (capitals %.0f units) drawn with %s, %.0f%% wide", fonts[font].tag,
			fonts[font].cap_height, embedded->file, font_fit(font) * 100.0f);
	}
}

/* the font of a slot read (again), and its common glyphs' fields made
ahead, on the worker thread */
static void font_load(long font)
{
	font_load_face(font);
#ifdef TEXT_FIELDS
	if (fonts[font].scale > 0.0f)
		fields_prewarm(font);
#endif
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
#ifdef TEXT_FIELDS
	/* (the worker may be reading the files' data) */
	fields_drain();
#endif
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
	/* (every glyph made again: the fields are keyed by the font files'
	data, which the next file read may be given again) */
	atlas_scale = -1.0f;
#ifdef TEXT_FIELDS
	fields_reset();
#endif
}

/* ---------- the distance fields */

#ifdef TEXT_FIELDS

/* the fields' atlas: RGBA texels, 8 MB here and as the texture (half the
rasterized atlas's texture), holding about 800 glyphs' fields */
#define FIELDS_WIDTH 2048
#define FIELDS_HEIGHT 1024
/* texels to the em a field is made at: a field is read at any size, and
48 keeps the curves of the largest text (a title at 4K, 90 pixels to the
em) and the corners of the smallest (small_ui in a 640x480 window, 11) */
#define FIELD_EM 48
/* texels of the field from its lowest value to its highest (the shader
agrees: XGPU_TEXT_FIELD_RANGE), and the clear texels around each glyph's
box that hold the field's reach, half the range and one more */
#define FIELD_RANGE XGPU_TEXT_FIELD_RANGE
#define FIELD_PAD (FIELD_RANGE / 2 + 1)
#define FIELD_COUNT 2048
#define FIELD_TABLE_SIZE (FIELD_COUNT * 2)
/* the verified mark's shape, drawn in units of its own: its height */
#define MARK_UNITS 1000.0
#define MARK_INDEX (-1)
/* a stand-in's key: the glyph rasterized for the display's pixels while
its field is made, or for good when the field cannot be */
#define STAND_IN_BIT 0x40000000
/* the most time a frame spends making fields on its own thread
(microseconds within a frame's worth of time): the rest wait for the worker
and are drawn as stand-ins meanwhile */
#define FIELDS_FRAME_BUDGET_US 1500
#define FIELDS_FRAME_US 16000

/* a glyph's field: the font file it is of (by its data, as stb_truetype
holds it) and its glyph index (STAND_IN_BIT: a stand-in), its texels in the
atlas (width 0: the glyph has no ink, or could not be made: failed), the
atlas's texels to the font unit, and the field's corner in font units (y
up); for a stand-in, the display's pixels to the font unit it was
rasterized at (pixels) */
struct field
{
	const unsigned char *data;
	int index;
	short x, y, width, height;
	float scale;
	float left, top;
	float pixels;
	unsigned char failed;
};

static struct
{
	unsigned char *pixels;
	struct field fields[FIELD_COUNT];
	int count;
	/* (field numbers + 1, by file and glyph) */
	short table[FIELD_TABLE_SIZE];
	int pack_x, pack_y, pack_row_height;
	/* the time this frame's worth of time has spent making fields on its
	own thread (fields_budget) */
	struct timespec window;
	long spent_us;
} fields;
/* (the mark's "font file") */
static const unsigned char mark_data[1];

/* ---------- the worker: fields made ahead, on a thread of their own */

enum
{
	_job_free,
	_job_queued,
	_job_making,
	_job_done
};

/* a glyph the worker makes: its font (a slot's, which stays put), the
field it made (rgba NULL: no ink, or failed) */
struct field_job
{
	const stbtt_fontinfo *info;
	const unsigned char *data;
	int index;
	int state;
	unsigned char *rgba;
	int width, height, failed;
	float left, top, scale;
};

#define FIELD_JOBS 1024

static struct
{
	struct field_job jobs[FIELD_JOBS];
	/* the ring: the next job added, made and collected */
	int add, make, collect;
	pthread_mutex_t lock;
	pthread_cond_t wake, made;
	int running;
	/* what the worker made ahead since it was last reported: fields and
	microseconds */
	long ahead_count, ahead_us;
} work = { { { NULL, NULL, 0, 0, NULL, 0, 0, 0, 0.0f, 0.0f, 0.0f } }, 0, 0, 0,
	PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0 };

static long elapsed_us(const struct timespec *from, const struct timespec *to)
{
	return (long)((to->tv_sec - from->tv_sec) * 1000000L + (to->tv_nsec - from->tv_nsec) / 1000L);
}

static unsigned long field_hash(const unsigned char *data, int index)
{
	return ((unsigned long)(size_t)data * 2654435761UL ^ (unsigned long)(index + 1) * 40503UL) &
		(FIELD_TABLE_SIZE - 1);
}

static void field_table_add(int field)
{
	unsigned long slot = field_hash(fields.fields[field].data, fields.fields[field].index);

	while (fields.table[slot])
		slot = (slot + 1) & (FIELD_TABLE_SIZE - 1);
	fields.table[slot] = (short)(field + 1);
}

static struct field *field_find(const unsigned char *data, int index)
{
	unsigned long slot = field_hash(data, index);

	while (fields.table[slot])
	{
		struct field *field = &fields.fields[fields.table[slot] - 1];

		if (field->data == data && field->index == index)
			return field;
		slot = (slot + 1) & (FIELD_TABLE_SIZE - 1);
	}
	return NULL;
}

/* the job of a glyph queued or made and not yet collected, or NULL (with
the lock held): the ring's live jobs, from the next collected to the last
added, looked through (a thousand at the most, and only while a glyph has
no field yet) */
static struct field_job *job_find(const unsigned char *data, int index)
{
	int job;

	for (job = work.collect; job != work.add; job = (job + 1) % FIELD_JOBS)
	{
		if (work.jobs[job].state != _job_free && work.jobs[job].data == data && work.jobs[job].index == index)
			return &work.jobs[job];
	}
	return NULL;
}

/* the atlas emptied: every field to be made again, and the texture
written whole (nothing until the first field is made: field_place) */
static void fields_reset(void)
{
	if (!fields.pixels)
		return;
	memset(fields.pixels, 0, (size_t)FIELDS_WIDTH * FIELDS_HEIGHT * 4);
	memset(fields.table, 0, sizeof(fields.table));
	fields.count = 0;
	fields.pack_x = fields.pack_y = fields.pack_row_height = 0;
	dirty_top = 0;
	dirty_bottom = FIELDS_HEIGHT;
}

/* a glyph's field made: its outline (in units of the file, y up, as
stb_truetype gives it) within its box at scale texels to the unit, into
*rgba (NULL: no ink); FALSE if it cannot be made (an outline of more edges
than the generator holds, or no memory). On any thread. */
static int field_generate(const struct msdf_vertex *shape, int count, int x0, int y0, int x1, int y1, float scale,
	unsigned char **rgba, int *width, int *height, float *left, float *top)
{
	*rgba = NULL;
	*width = *height = 0;
	*left = *top = 0.0f;
	if (x1 <= x0 || y1 <= y0)
		return 1;
	*width = (int)ceilf((float)(x1 - x0) * scale) + 2 * FIELD_PAD;
	*height = (int)ceilf((float)(y1 - y0) * scale) + 2 * FIELD_PAD;
	*left = (float)x0 - (float)FIELD_PAD / scale;
	*top = (float)y1 + (float)FIELD_PAD / scale;
	if (*width + 1 > FIELDS_WIDTH || *height + 1 > FIELDS_HEIGHT)
		return 0;
	*rgba = malloc((size_t)*width * *height * 4);
	if (!*rgba || !msdf_generate(shape, count, scale, *left, *top, (double)FIELD_RANGE, *width, *height, *rgba))
	{
		free(*rgba);
		*rgba = NULL;
		return 0;
	}
	return 1;
}

/* a font's glyph's field made (field_generate), from its outline */
static int glyph_generate(const stbtt_fontinfo *info, int index, float scale, unsigned char **rgba, int *width,
	int *height, float *left, float *top)
{
	stbtt_vertex *vertices = NULL;
	struct msdf_vertex *shape;
	int count, vertex, x0 = 0, y0 = 0, x1 = 0, y1 = 0, made;

	if (!stbtt_GetGlyphBox(info, index, &x0, &y0, &x1, &y1))
		x0 = y0 = x1 = y1 = 0;
	count = stbtt_GetGlyphShape(info, index, &vertices);
	shape = malloc(sizeof(*shape) * (size_t)(count > 0 ? count : 1));
	if (!shape)
	{
		stbtt_FreeShape(info, vertices);
		*rgba = NULL;
		return 0;
	}
	for (vertex = 0; vertex < count; vertex++)
	{
		shape[vertex].type = vertices[vertex].type;
		shape[vertex].x = vertices[vertex].x;
		shape[vertex].y = vertices[vertex].y;
		shape[vertex].cx = vertices[vertex].cx;
		shape[vertex].cy = vertices[vertex].cy;
		shape[vertex].cx1 = vertices[vertex].cx1;
		shape[vertex].cy1 = vertices[vertex].cy1;
	}
	made = field_generate(shape, count, x0, y0, x1, y1, scale, rgba, width, height, left, top);
	stbtt_FreeShape(info, vertices);
	free(shape);
	return made;
}

/* texels (width by height, RGBA) put into the atlas as the field of a
glyph, keyed by its file and index: NULL rgba for a glyph with no ink, or
one that failed (then kept as failed, never tried again); NULL when there
is no room for it even once the atlas is emptied */
static struct field *field_place(const unsigned char *data, int index, const unsigned char *rgba, int width,
	int height, float left, float top, float scale, float pixels, int failed)
{
	struct field *field;
	int row, attempt;

	if (!fields.pixels)
	{
		fields.pixels = calloc((size_t)FIELDS_WIDTH * FIELDS_HEIGHT, 4);
		if (!fields.pixels)
			return NULL;
		fields_reset();
	}
	if (fields.count >= FIELD_COUNT)
		fields_reset();
	field = &fields.fields[fields.count];
	memset(field, 0, sizeof(*field));
	field->data = data;
	field->index = index;
	field->scale = scale;
	field->pixels = pixels;
	field->failed = (unsigned char)(failed != 0);
	if (!rgba || width <= 0 || height <= 0)
	{
		field_table_add(fields.count++);
		return field;
	}
	if (width + 1 > FIELDS_WIDTH || height + 1 > FIELDS_HEIGHT)
		return NULL;
	for (attempt = 0; attempt < 2; attempt++)
	{
		if (fields.pack_x + width > FIELDS_WIDTH)
		{
			fields.pack_x = 0;
			fields.pack_y += fields.pack_row_height + 1;
			fields.pack_row_height = 0;
		}
		if (fields.pack_y + height <= FIELDS_HEIGHT)
			break;
		fields_reset();
		field = &fields.fields[fields.count];
		memset(field, 0, sizeof(*field));
		field->data = data;
		field->index = index;
		field->scale = scale;
		field->pixels = pixels;
	}
	field->x = (short)fields.pack_x;
	field->y = (short)fields.pack_y;
	field->width = (short)width;
	field->height = (short)height;
	field->left = left;
	field->top = top;
	for (row = 0; row < height; row++)
	{
		memcpy(fields.pixels + ((size_t)(field->y + row) * FIELDS_WIDTH + field->x) * 4,
			rgba + (size_t)row * width * 4, (size_t)width * 4);
	}
	if (field->y < dirty_top)
		dirty_top = field->y;
	if (field->y + height > dirty_bottom)
		dirty_bottom = field->y + height;
	fields.pack_x += width + 1;
	if (height > fields.pack_row_height)
		fields.pack_row_height = height;
	field_table_add(fields.count++);
	return field;
}

static void *fields_worker_main(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&work.lock);
	for (;;)
	{
		struct field_job *job;
		struct timespec from, to;
		unsigned char *rgba = NULL;
		int width = 0, height = 0, failed;
		float left = 0.0f, top = 0.0f;

		while (work.jobs[work.make].state != _job_queued)
			pthread_cond_wait(&work.wake, &work.lock);
		job = &work.jobs[work.make];
		work.make = (work.make + 1) % FIELD_JOBS;
		job->state = _job_making;
		pthread_mutex_unlock(&work.lock);
		clock_gettime(CLOCK_MONOTONIC, &from);
		failed = !glyph_generate(job->info, job->index, job->scale, &rgba, &width, &height, &left, &top);
		clock_gettime(CLOCK_MONOTONIC, &to);
		pthread_mutex_lock(&work.lock);
		job->rgba = rgba;
		job->width = width;
		job->height = height;
		job->left = left;
		job->top = top;
		job->failed = failed;
		job->state = _job_done;
		work.ahead_count++;
		work.ahead_us += elapsed_us(&from, &to);
		pthread_cond_broadcast(&work.made);
	}
	return NULL;
}

/* a glyph queued for the worker (with the lock held): FALSE when the ring
is full, or it is queued already */
static int job_add(const stbtt_fontinfo *info, const unsigned char *data, int index, float scale)
{
	struct field_job *job = &work.jobs[work.add];

	if (job->state != _job_free || job_find(data, index))
		return 0;
	if (!work.running)
	{
		pthread_t thread;

		if (pthread_create(&thread, NULL, fields_worker_main, NULL) != 0)
			return 0;
		pthread_detach(thread);
		work.running = 1;
	}
	memset(job, 0, sizeof(*job));
	job->info = info;
	job->data = data;
	job->index = index;
	job->scale = scale;
	job->state = _job_queued;
	work.add = (work.add + 1) % FIELD_JOBS;
	pthread_cond_signal(&work.wake);
	return 1;
}

/* the fields the worker has made put into the atlas, in order (the ring's
jobs freed as they are) */
static void fields_collect(void)
{
	pthread_mutex_lock(&work.lock);
	while (work.jobs[work.collect].state == _job_done)
	{
		struct field_job *job = &work.jobs[work.collect];
		unsigned char *rgba = job->rgba;

		job->rgba = NULL;
		pthread_mutex_unlock(&work.lock);
		field_place(job->data, job->index, rgba, job->width, job->height, job->left, job->top, job->scale, 0.0f,
			job->failed);
		free(rgba);
		pthread_mutex_lock(&work.lock);
		job->state = _job_free;
		work.collect = (work.collect + 1) % FIELD_JOBS;
	}
	if (work.ahead_count && work.jobs[work.collect].state == _job_free)
	{
		platform_log("high-res text: %ld fields made ahead in %ld ms", work.ahead_count, work.ahead_us / 1000);
		work.ahead_count = 0;
		work.ahead_us = 0;
	}
	pthread_mutex_unlock(&work.lock);
}

/* the worker's queue emptied and the worker idle (the font files' data
about to be freed): what it made is let go */
static void fields_drain(void)
{
	int job;

	pthread_mutex_lock(&work.lock);
	for (job = 0; job < FIELD_JOBS; job++)
	{
		if (work.jobs[job].state == _job_queued)
			work.jobs[job].state = _job_done;
	}
	for (job = 0; job < FIELD_JOBS; job++)
	{
		while (work.jobs[job].state == _job_making)
			pthread_cond_wait(&work.made, &work.lock);
	}
	for (job = 0; job < FIELD_JOBS; job++)
	{
		free(work.jobs[job].rgba);
		work.jobs[job].rgba = NULL;
		work.jobs[job].state = _job_free;
	}
	work.add = work.make = work.collect = 0;
	work.ahead_count = 0;
	work.ahead_us = 0;
	pthread_mutex_unlock(&work.lock);
}

/* a font's common glyphs (ASCII and Latin-1) queued for the worker, as
the font is loaded, so that the text's first draw finds them made */
static void fields_prewarm(long font)
{
	const stbtt_fontinfo *info = &fonts[font].info;
	float scale = stbtt_ScaleForMappingEmToPixels(info, (float)FIELD_EM);
	unsigned long code;

	if (!fields_wanted())
		return;
	pthread_mutex_lock(&work.lock);
	for (code = 33; code < 256; code++)
	{
		int index;

		if (code >= 127 && code < 161)
			continue;
		index = stbtt_FindGlyphIndex(info, (int)code);
		if (index && !field_find(info->data, index))
			job_add(info, info->data, index, scale);
	}
	pthread_mutex_unlock(&work.lock);
}

/* whether this frame's worth of time has room to make a field on its own
thread, and the time spent charged to it after */
static int fields_budget(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (elapsed_us(&fields.window, &now) > FIELDS_FRAME_US)
	{
		fields.window = now;
		fields.spent_us = 0;
	}
	return fields.spent_us < FIELDS_FRAME_BUDGET_US;
}

static void fields_charge(const struct timespec *from)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	fields.spent_us += elapsed_us(from, &now);
}

/* a font's glyph's field: made already (NULL while the worker is still
making it, or when it cannot be, the field then marked failed), else made
now within the frame's budget, else queued for the worker */
static struct field *glyph_field(long font, int index)
{
	const stbtt_fontinfo *info = &fonts[font].info;
	struct field *field = field_find(info->data, index);
	float scale;
	int queued;

	if (field)
		return field;
	pthread_mutex_lock(&work.lock);
	queued = job_find(info->data, index) != NULL;
	pthread_mutex_unlock(&work.lock);
	if (queued)
		return NULL;
	scale = stbtt_ScaleForMappingEmToPixels(info, (float)FIELD_EM);
	if (fields_budget())
	{
		struct timespec from;
		unsigned char *rgba = NULL;
		int width = 0, height = 0, failed;
		float left = 0.0f, top = 0.0f;

		clock_gettime(CLOCK_MONOTONIC, &from);
		failed = !glyph_generate(info, index, scale, &rgba, &width, &height, &left, &top);
		field = field_place(info->data, index, rgba, width, height, left, top, scale, 0.0f, failed);
		free(rgba);
		fields_charge(&from);
		return field;
	}
	pthread_mutex_lock(&work.lock);
	job_add(info, info->data, index, scale);
	pthread_mutex_unlock(&work.lock);
	return NULL;
}

/* the glyph rasterized at the display's pixels (as the rasterized atlas
would have it) into the fields' atlas, as a stand-in while its field is
made or for good: its coverage written so that the shader's decode gives it
back where the texels are the screen's pixels (the median 0.5 + (coverage
- 0.5) / range); NULL when there is no room */
static struct field *stand_in_field(long font, int index, float pixels_x, float pixels, int x0, int y0, int x1,
	int y1)
{
	const stbtt_fontinfo *info = &fonts[font].info;
	struct field *field = field_find(info->data, index | STAND_IN_BIT);
	int width = x1 - x0, height = y1 - y0, texel;
	unsigned char *coverage, *rgba;

	if (field && fabsf(field->pixels - pixels) < pixels * 0.01f)
		return field;
	if (width <= 0 || height <= 0)
		return NULL;
	coverage = calloc((size_t)width * height, 1);
	rgba = malloc((size_t)width * height * 4);
	if (!coverage || !rgba)
	{
		free(coverage);
		free(rgba);
		return NULL;
	}
	stbtt_MakeGlyphBitmap(info, coverage, width, height, width, pixels_x, pixels, index);
	for (texel = 0; texel < width * height; texel++)
	{
		int value = 128 + ((int)coverage[texel] - 128) / FIELD_RANGE;

		rgba[texel * 4 + 0] = rgba[texel * 4 + 1] = rgba[texel * 4 + 2] = (unsigned char)value;
		rgba[texel * 4 + 3] = (unsigned char)value;
	}
	field = field_place(info->data, index | STAND_IN_BIT, rgba, width, height, 0.0f, 0.0f, 0.0f, pixels, 0);
	free(coverage);
	free(rgba);
	return field;
}

/* the verified mark's outline (halo_delta_mark_outline: the triangle
halo_delta_mark_draw draws, its strokes a sixth of its height), MARK_UNITS
tall and a little wider, y up from its base, as a font's contours go: the
outer triangle clockwise and the inner one the other way round; 6 vertices */
#define MARK_VERTICES 6
static void mark_shape(struct msdf_vertex *shape)
{
	double corners[6][2];
	int vertex;

	halo_delta_mark_outline(MARK_UNITS * 1.125, MARK_UNITS, corners);
	memset(shape, 0, sizeof(*shape) * MARK_VERTICES);
	for (vertex = 0; vertex < MARK_VERTICES; vertex++)
	{
		shape[vertex].type = vertex % 3 == 0 ? 1 : 2;
		shape[vertex].x = corners[vertex][0];
		shape[vertex].y = corners[vertex][1];
	}
}

/* the mark's field, made now if it is not yet (six edges: a moment), about
a capital's height at FIELD_EM texels to the em */
static struct field *mark_field(void)
{
	struct field *field = field_find(mark_data, MARK_INDEX);
	struct msdf_vertex shape[MARK_VERTICES];
	unsigned char *rgba = NULL;
	int width = 0, height = 0, failed;
	float left = 0.0f, top = 0.0f, scale = (float)(FIELD_EM * 0.72 / MARK_UNITS);

	if (field)
		return field;
	mark_shape(shape);
	failed = !field_generate(shape, MARK_VERTICES, 0, 0, (int)(MARK_UNITS * 1.125 + 0.5), (int)MARK_UNITS, scale,
		&rgba, &width, &height, &left, &top);
	field = field_place(mark_data, MARK_INDEX, rgba, width, height, left, top, scale, 0.0f, failed);
	free(rgba);
	return field;
}

/* text_hires_glyph, from the fields: the quad as the rasterized glyph's
would be, mapped onto the glyph's field, or onto its stand-in while the
field is made */
static int fields_glyph(long font, unsigned long code, struct text_hires_glyph *glyph)
{
	float scale = halo_screen_pixel_scale();
	float pixel_scale = scale * fonts[font].oversample;
	float pixels = fonts[font].scale * pixel_scale;
	float pixels_x = pixels * fonts[font].width;
	float u_scale = (float)placeholder_width / FIELDS_WIDTH;
	float v_scale = (float)placeholder_height / FIELDS_HEIGHT;
	int index = stbtt_FindGlyphIndex(&fonts[font].info, (int)code);
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0, advance = 0, bearing = 0;
	struct field *field;

	fields_collect();
	if (!index)
	{
		/* (the verified mark, where the font has no Delta: as the
		rasterized glyph is sized, the height of the tag's capitals) */
		int mark_height, mark_width;

		if (code != HALO_DELTA_MARK || !(field = mark_field()) || !field->width)
			return 0;
		mark_height = (int)(fonts[font].cap_height * pixel_scale + 0.5f);
		if (mark_height < 1)
			mark_height = 1;
		mark_width = halo_delta_mark_width(mark_height);
		glyph->left = 0.0f;
		glyph->top = -(float)mark_height / pixel_scale;
		glyph->right = (float)mark_width / pixel_scale;
		glyph->bottom = 0.0f;
		glyph->advance = (float)(mark_width + halo_delta_mark_gap(mark_height)) / pixel_scale;
		glyph->u0 = (field->x + (0.0f - field->left) * field->scale) * u_scale;
		glyph->u1 = (field->x + ((float)(MARK_UNITS * 1.125) - field->left) * field->scale) * u_scale;
		glyph->v0 = (field->y + (field->top - (float)MARK_UNITS) * field->scale) * v_scale;
		glyph->v1 = (field->y + (field->top - 0.0f) * field->scale) * v_scale;
		return 1;
	}
	stbtt_GetGlyphBitmapBox(&fonts[font].info, index, pixels_x, pixels, &x0, &y0, &x1, &y1);
	if (x1 <= x0 || y1 <= y0)
		return 0;
	stbtt_GetGlyphHMetrics(&fonts[font].info, index, &advance, &bearing);
	glyph->left = x0 / pixel_scale;
	glyph->top = y0 / pixel_scale;
	glyph->right = x1 / pixel_scale;
	glyph->bottom = y1 / pixel_scale;
	glyph->advance = advance * fonts[font].scale * fonts[font].width;
	field = glyph_field(font, index);
	if (!field || field->failed)
	{
		/* (its stand-in: the rasterized glyph, its quad the whole of it) */
		field = stand_in_field(font, index, pixels_x, pixels, x0, y0, x1, y1);
		if (!field || !field->width)
			return 0;
		glyph->u0 = field->x * u_scale;
		glyph->u1 = (field->x + field->width) * u_scale;
		glyph->v0 = field->y * v_scale;
		glyph->v1 = (field->y + field->height) * v_scale;
		return 1;
	}
	if (!field->width)
		return 0;
	/* (the quad's corners in the field: the pixels' box, y down, in font
	units, y up) */
	glyph->u0 = (field->x + (x0 / pixels_x - field->left) * field->scale) * u_scale;
	glyph->u1 = (field->x + (x1 / pixels_x - field->left) * field->scale) * u_scale;
	glyph->v0 = (field->y + (field->top + y0 / pixels) * field->scale) * v_scale;
	glyph->v1 = (field->y + (field->top + y1 / pixels) * field->scale) * v_scale;
	return 1;
}

#endif /* TEXT_FIELDS */

long text_hires_font(char const *tag_name, float cap_height, short const *letter_widths, float oversample)
{
	long font;
	long letters_width = 0;
	int letter;

	if (!text_hires_enabled() || !tag_name || cap_height <= 0.0f || !letter_widths)
		return -1;
	player_fonts_check();
	/* (text is never drawn with fewer pixels than the display's) */
	if (oversample < 1.0f)
		oversample = 1.0f;
	for (letter = 0; letter < TEXT_HIRES_FIT_LETTERS; letter++)
		letters_width += letter_widths[letter] > 0 ? letter_widths[letter] : 0;
	/* (by the tag's measures too: the Xbox maps and a Custom Edition loc.map
	have tags of the same names with other measures) */
	for (font = 0; font < font_count; font++)
	{
		if (!names_differ(fonts[font].tag, tag_name) && fonts[font].cap_height == cap_height &&
			fonts[font].letters_width == letters_width && fonts[font].oversample == oversample)
		{
			return fonts[font].scale > 0.0f ? font : -1;
		}
	}
	if (font_count >= MAXIMUM_FONTS)
		return -1;
	font = font_count++;
	snprintf(fonts[font].tag, sizeof(fonts[font].tag), "%s", tag_name);
	fonts[font].oversample = oversample;
	fonts[font].cap_height = cap_height;
	memcpy(fonts[font].letter_widths, letter_widths, sizeof(fonts[font].letter_widths));
	fonts[font].letters_width = letters_width;
	font_load(font);
	return fonts[font].scale > 0.0f ? font : -1;
}

int text_hires_covers(long font, unsigned long code)
{
	if (font < 0 || font >= font_count || fonts[font].scale <= 0.0f)
		return 0;
	/* (control characters are never drawn; the verified mark is drawn
without the font's Delta: text_hires_glyph) */
	return code < 32 || code == ' ' || code == HALO_DELTA_MARK || stbtt_FindGlyphIndex(&fonts[font].info, (int)code) != 0;
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
#ifdef TEXT_FIELDS
	if (fields_wanted())
		return fields_glyph(font, code, glyph);
#endif
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
		/* (the verified mark, where the font has no Delta: as font_group.c
		draws it for the font tags, the height of the tag's capitals, on the
		baseline, with its room after it) */
		int mark = code == HALO_DELTA_MARK && !index;
		float mark_advance = 0.0f;

		if (index)
		{
			stbtt_GetGlyphBitmapBox(&fonts[font].info, index, pixels * fonts[font].width, pixels, &x0, &y0, &x1, &y1);
			stbtt_GetGlyphHMetrics(&fonts[font].info, index, &advance, &bearing);
		}
		else if (mark)
		{
			float pixel_scale_now = scale * fonts[font].oversample;
			int mark_height = (int)(fonts[font].cap_height * pixel_scale_now + 0.5f);

			if (mark_height < 1)
				mark_height = 1;
			x1 = halo_delta_mark_width(mark_height);
			y0 = -mark_height;
			mark_advance = (float)(x1 + halo_delta_mark_gap(mark_height)) / pixel_scale_now;
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
			if (mark)
			{
				halo_delta_mark_draw(atlas + (pack_y + ATLAS_PADDING) * ATLAS_SIZE + pack_x + ATLAS_PADDING,
					width, height, ATLAS_SIZE);
			}
			else
			{
				stbtt_MakeGlyphBitmap(&fonts[font].info,
					atlas + (pack_y + ATLAS_PADDING) * ATLAS_SIZE + pack_x + ATLAS_PADDING,
					width, height, ATLAS_SIZE, pixels * fonts[font].width, pixels, index);
			}
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
		glyphs[slot].advance = mark ? mark_advance : advance * fonts[font].scale * fonts[font].width;
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

int text_hires_atlas_fields(void)
{
	return fields_wanted();
}

/* the rows [top, top + rows) of the texture written, RGBA */
static void texture_rows(unsigned long top, unsigned long width, unsigned long rows, const unsigned char *rgba)
{
#ifdef HALO_BGFX
	if (xgpu_bgfx)
	{
		xgpu_bgfx_texture_rows(atlas_texture, top, width, rows, rgba);
		return;
	}
#endif
#ifdef HALO_VULKAN
	if (xgpu_vulkan)
	{
		xgpu_vulkan_texture_rows(atlas_texture, top, width, rows, rgba);
		return;
	}
#endif
	glBindTexture(GL_TEXTURE_2D, atlas_texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, (GLint)top, (GLsizei)width, (GLsizei)rows, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	xgpu_gl_state_invalidate();
}

static void texture_delete(void)
{
	if (!atlas_texture)
		return;
#ifdef HALO_BGFX
	if (xgpu_bgfx)
	{
		xgpu_bgfx_texture_delete(atlas_texture);
		atlas_texture = 0;
		return;
	}
#endif
#ifdef HALO_VULKAN
	if (xgpu_vulkan)
	{
		xgpu_vulkan_texture_delete(atlas_texture);
		atlas_texture = 0;
		return;
	}
#endif
	glDeleteTextures(1, &atlas_texture);
	xgpu_gl_state_invalidate();
	atlas_texture = 0;
}

unsigned int text_hires_atlas_texture(unsigned long data)
{
	int kind = fields_wanted();
	unsigned long width = ATLAS_SIZE, height = ATLAS_SIZE;

	if (!data || data != placeholder_data)
		return 0;
#ifdef TEXT_FIELDS
	if (kind)
	{
		if (!fields.pixels)
			return 0;
		width = FIELDS_WIDTH;
		height = FIELDS_HEIGHT;
	}
	else
#endif
	if (!atlas)
	{
		return 0;
	}
	/* (the texture of the other kind of glyph, from before the setting
	changed: made again, whole) */
	if (atlas_texture && atlas_texture_fields != kind)
		texture_delete();
	if (!atlas_texture)
	{
#ifdef HALO_BGFX
		if (xgpu_bgfx)
		{
			/* (the bgfx backend's texture: d3d8_bgfx.inl) */
			atlas_texture = xgpu_bgfx_texture_create(width, height, NULL, FALSE);
		}
		else
#endif
#ifdef HALO_VULKAN
		if (xgpu_vulkan)
		{
			/* (the Vulkan backend's image: d3d8_vk.inl) */
			atlas_texture = xgpu_vulkan_texture_create(width, height, NULL, FALSE);
		}
		else
#endif
		{
			glGenTextures(1, &atlas_texture);
			glBindTexture(GL_TEXTURE_2D, atlas_texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			xgpu_gl_state_invalidate();
		}
		atlas_texture_fields = kind;
		dirty_top = 0;
		dirty_bottom = (int)height;
	}
	if (atlas_texture && dirty_top < dirty_bottom)
	{
		long rows = dirty_bottom - dirty_top;

#ifdef TEXT_FIELDS
		if (kind)
		{
			/* (the fields, as they are) */
			texture_rows((unsigned long)dirty_top, width, (unsigned long)rows,
				fields.pixels + (size_t)dirty_top * FIELDS_WIDTH * 4);
			dirty_top = (int)height;
			dirty_bottom = 0;
		}
		else
#endif
		{
			/* (white, the glyph's coverage its alpha, as the maps' fonts are) */
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
				texture_rows((unsigned long)dirty_top, ATLAS_SIZE, (unsigned long)rows, texels);
				free(texels);
				dirty_top = (int)height;
				dirty_bottom = 0;
			}
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
