/*
MISSING_MAPS_SCREEN.C

A console's screen for missing maps (HALO_CONSOLE_MISSING_MAPS_SCREEN): when
ui.map is not found, the menus' error screen can't be shown, as it is in
ui.map itself, and a console has no setup flow before the game to say so
(the desktops' is sdl_platform.c's). The game then shows this instead: what
is missing, the folder it looked in, and the maps it needs, drawn with
nothing from any map: each letter's pixels are clears of the screen
(IDirect3DDevice8_Clear's rectangles), which every console's renderer has,
from a 5 by 7 letter set of its own below. B or Back quits to the system's
menu as the game's own quit does (xbox_dashboard_launch); the system's own
button (the Wii U's and the Switch's HOME) works as always.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "input/input.h"
#include "interface/missing_maps_screen.h"
#include "interface/marketing_and_strategic_business_development.h"

#ifdef HALO_CONSOLE_MISSING_MAPS_SCREEN

#include "cseries_windows.h"
#include "rasterizer/xbox/rasterizer_xbox.h"
#include "rasterizer/rasterizer.h"

#if !defined(HALO_XBOX_CONSOLE) && (defined(__unix__) || defined(__APPLE__) || defined(__WIIU__) || defined(__SWITCH__))
#include <unistd.h>
#define MISSING_MAPS_WORKING_FOLDER 1
#endif

/* ---------- constants */

/* the maps the game runs (cache_files.c's list of the retail builds) */
#define MISSING_MAPS_VERSION "NTSC 01.10.12.2276"

enum
{
	/* a letter's cell, in its pixels: 5 wide and a column between, 7 high
	with one below the line (g, j, p, q, y) and one between lines */
	MISSING_MAPS_GLYPH_WIDTH = 5,
	MISSING_MAPS_GLYPH_ROWS = 8,
	MISSING_MAPS_CELL_WIDTH = 6,
	MISSING_MAPS_CELL_HEIGHT = 10,
	MISSING_MAPS_FIRST_CHARACTER = 32,
	MISSING_MAPS_CHARACTERS = 95,

	MISSING_MAPS_LINES = 16,
	MISSING_MAPS_LINE_LENGTH = 128,
	/* (the native renderers' most in one clear: d3d8_console.inl) */
	MISSING_MAPS_CLEAR_BATCH = 16,

	/* the game's primary target (rasterizer_xbox.c's _rasterizer_target_render_primary) */
	MISSING_MAPS_TARGET_RENDER_PRIMARY = 0
};

#define MISSING_MAPS_BACKGROUND 0xFF000000
#define MISSING_MAPS_TITLE_COLOR 0xFFFFFFFF
#define MISSING_MAPS_TEXT_COLOR 0xFFC8D6E5
#define MISSING_MAPS_FOLDER_COLOR 0xFF2896FF

/* ---------- structures */

struct missing_maps_line
{
	char text[MISSING_MAPS_LINE_LENGTH];
	pixel32 color;
	short scale;
};

/* ---------- globals */

/* ASCII from the space to the tilde, a row a byte from the top, its five
pixels in the low bits, the leftmost highest */
static byte const missing_maps_glyphs[MISSING_MAPS_CHARACTERS][MISSING_MAPS_GLYPH_ROWS] =
{
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /*   */
	{ 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00 }, /* ! */
	{ 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* " */
	{ 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A, 0x00 }, /* # */
	{ 0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04, 0x00 }, /* $ */
	{ 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03, 0x00 }, /* % */
	{ 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D, 0x00 }, /* & */
	{ 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* ' */
	{ 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, 0x00 }, /* ( */
	{ 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00 }, /* ) */
	{ 0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00, 0x00 }, /* * */
	{ 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00, 0x00 }, /* + */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08 }, /* , */
	{ 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00 }, /* - */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00 }, /* . */
	{ 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00 }, /* / */
	{ 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E, 0x00 }, /* 0 */
	{ 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 }, /* 1 */
	{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F, 0x00 }, /* 2 */
	{ 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E, 0x00 }, /* 3 */
	{ 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x00 }, /* 4 */
	{ 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E, 0x00 }, /* 5 */
	{ 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E, 0x00 }, /* 6 */
	{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08, 0x00 }, /* 7 */
	{ 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00 }, /* 8 */
	{ 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00 }, /* 9 */
	{ 0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00 }, /* : */
	{ 0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x08 }, /* ; */
	{ 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00 }, /* < */
	{ 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00 }, /* = */
	{ 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00 }, /* > */
	{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00 }, /* ? */
	{ 0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E, 0x00 }, /* @ */
	{ 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00 }, /* A */
	{ 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00 }, /* B */
	{ 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E, 0x00 }, /* C */
	{ 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E, 0x00 }, /* D */
	{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00 }, /* E */
	{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00 }, /* F */
	{ 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F, 0x00 }, /* G */
	{ 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00 }, /* H */
	{ 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 }, /* I */
	{ 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C, 0x00 }, /* J */
	{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00 }, /* K */
	{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00 }, /* L */
	{ 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11, 0x00 }, /* M */
	{ 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00 }, /* N */
	{ 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00 }, /* O */
	{ 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00 }, /* P */
	{ 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00 }, /* Q */
	{ 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11, 0x00 }, /* R */
	{ 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E, 0x00 }, /* S */
	{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 }, /* T */
	{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00 }, /* U */
	{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00 }, /* V */
	{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A, 0x00 }, /* W */
	{ 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x00 }, /* X */
	{ 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00 }, /* Y */
	{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00 }, /* Z */
	{ 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E, 0x00 }, /* [ */
	{ 0x00, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00 }, /* backslash */
	{ 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E, 0x00 }, /* ] */
	{ 0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* ^ */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00 }, /* _ */
	{ 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* ` */
	{ 0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F, 0x00 }, /* a */
	{ 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E, 0x00 }, /* b */
	{ 0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E, 0x00 }, /* c */
	{ 0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F, 0x00 }, /* d */
	{ 0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x00 }, /* e */
	{ 0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x00 }, /* f */
	{ 0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E }, /* g */
	{ 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00 }, /* h */
	{ 0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x00 }, /* i */
	{ 0x02, 0x00, 0x06, 0x02, 0x02, 0x02, 0x12, 0x0C }, /* j */
	{ 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00 }, /* k */
	{ 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 }, /* l */
	{ 0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15, 0x00 }, /* m */
	{ 0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00 }, /* n */
	{ 0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00 }, /* o */
	{ 0x00, 0x00, 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10 }, /* p */
	{ 0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01 }, /* q */
	{ 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00 }, /* r */
	{ 0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E, 0x00 }, /* s */
	{ 0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06, 0x00 }, /* t */
	{ 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D, 0x00 }, /* u */
	{ 0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00 }, /* v */
	{ 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00 }, /* w */
	{ 0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00 }, /* x */
	{ 0x00, 0x00, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x0E }, /* y */
	{ 0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00 }, /* z */
	{ 0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02, 0x00 }, /* { */
	{ 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 }, /* | */
	{ 0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08, 0x00 }, /* } */
	{ 0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00 }, /* ~ */
};

static struct missing_maps_line missing_maps_lines[MISSING_MAPS_LINES];
static short missing_maps_line_count;

/* ---------- private code */

/* the folder the game looked for the maps in, as the player finds it */
static void missing_maps_folder(
	char *folder,
	long size)
{
#ifdef HALO_XBOX_CONSOLE
	/* (the title's own folder: d:\ is where its executable is) */
	csstrncpy(folder, "D:\\maps", size - 1);
	folder[size - 1] = 0;
#else
	extern void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);
	char path[MISSING_MAPS_LINE_LENGTH * 2];

	platform_translate_path("d:\\maps", path, sizeof(path));
	/* ("./maps" in the game's working folder: the folder itself) */
	if (path[0] == '.' && (path[1] == '/' || path[1] == '\\'))
	{
		char path_whole[NUMBEROF(path)];
		boolean whole = FALSE;

#ifdef MISSING_MAPS_WORKING_FOLDER
		char working[NUMBEROF(path)];

		if (getcwd(working, sizeof(working)))
		{
			unsigned long length = (unsigned long)strlen(working);

			while (length > 1 && (working[length - 1] == '/' || working[length - 1] == '\\'))
				working[--length] = 0;
			_snprintf(path_whole, sizeof(path_whole) - 1, "%s%s", working, path + 1);
			path_whole[sizeof(path_whole) - 1] = 0;
			whole = TRUE;
		}
#endif
		if (whole)
			csstrncpy(path, path_whole, sizeof(path) - 1);
		path[sizeof(path) - 1] = 0;
	}
	if (console_path_shown)
		console_path_shown(path, folder, (unsigned long)size);
	else
	{
		csstrncpy(folder, path, size - 1);
		folder[size - 1] = 0;
	}
#endif

	return;
}

static void missing_maps_add_line(
	char const *text,
	pixel32 color,
	short scale)
{
	struct missing_maps_line *line;

	if (missing_maps_line_count >= MISSING_MAPS_LINES)
		return;
	line = &missing_maps_lines[missing_maps_line_count++];
	csstrncpy(line->text, text, sizeof(line->text) - 1);
	line->text[sizeof(line->text) - 1] = 0;
	line->color = color;
	line->scale = scale;
	error(_error_silent, "missing maps screen: %s", text);

	return;
}

/* a line too long for the screen broken after a separator (a folder's / or
\), else where it runs out, into lines of at most columns letters */
static void missing_maps_add_wrapped(
	char const *text,
	pixel32 color,
	short scale,
	short columns)
{
	char piece[MISSING_MAPS_LINE_LENGTH];
	long length = (long)strlen(text);

	while (length > columns && missing_maps_line_count < MISSING_MAPS_LINES)
	{
		long cut = columns;
		long index;

		for (index = columns; index > columns / 2; index--)
		{
			if (text[index - 1] == '/' || text[index - 1] == '\\' || text[index - 1] == ' ')
			{
				cut = index;
				break;
			}
		}
		csmemcpy(piece, text, cut);
		piece[cut] = 0;
		missing_maps_add_line(piece, color, scale);
		text += cut;
		length -= cut;
	}
	missing_maps_add_line(text, color, scale);

	return;
}

/* the clears waiting to be sent, in one call per MISSING_MAPS_CLEAR_BATCH */
static D3DRECT missing_maps_rectangles[MISSING_MAPS_CLEAR_BATCH];
static long missing_maps_rectangle_count;
static pixel32 missing_maps_rectangle_color;

static void missing_maps_flush(
	void)
{
	if (missing_maps_rectangle_count)
	{
		IDirect3DDevice8_Clear(
			global_d3d_device,
			missing_maps_rectangle_count,
			missing_maps_rectangles,
			D3DCLEAR_TARGET,
			missing_maps_rectangle_color,
			1.0f,
			0);
		missing_maps_rectangle_count = 0;
	}

	return;
}

static void missing_maps_rectangle(
	long x0,
	long y0,
	long x1,
	long y1,
	pixel32 color)
{
	D3DRECT *rectangle;

	if (missing_maps_rectangle_count &&
		(missing_maps_rectangle_color != color || missing_maps_rectangle_count == MISSING_MAPS_CLEAR_BATCH))
	{
		missing_maps_flush();
	}
	missing_maps_rectangle_color = color;
	rectangle = &missing_maps_rectangles[missing_maps_rectangle_count++];
	rectangle->x1 = x0;
	rectangle->y1 = y0;
	rectangle->x2 = x1;
	rectangle->y2 = y1;

	return;
}

/* a line's letters, each row's runs of pixels a clear each, from x, y (its
top left) */
static void missing_maps_draw_text(
	char const *text,
	long x,
	long y,
	short scale,
	pixel32 color)
{
	for (; *text; text++, x += MISSING_MAPS_CELL_WIDTH * scale)
	{
		unsigned char character = (unsigned char)*text;
		byte const *glyph;
		short row;

		if (character < MISSING_MAPS_FIRST_CHARACTER ||
			character >= MISSING_MAPS_FIRST_CHARACTER + MISSING_MAPS_CHARACTERS)
		{
			character = '?';
		}
		glyph = missing_maps_glyphs[character - MISSING_MAPS_FIRST_CHARACTER];
		for (row = 0; row < MISSING_MAPS_GLYPH_ROWS; row++)
		{
			short column = 0;

			while (column < MISSING_MAPS_GLYPH_WIDTH)
			{
				short run_start;

				if (!(glyph[row] & (1 << (MISSING_MAPS_GLYPH_WIDTH - 1 - column))))
				{
					column++;
					continue;
				}
				run_start = column;
				while (column < MISSING_MAPS_GLYPH_WIDTH &&
					(glyph[row] & (1 << (MISSING_MAPS_GLYPH_WIDTH - 1 - column))))
				{
					column++;
				}
				missing_maps_rectangle(
					x + run_start * scale,
					y + row * scale,
					x + column * scale,
					y + (row + 1) * scale,
					color);
			}
		}
	}

	return;
}

/* the screen's surface (the game's primary target, which it presents), its
size in the renderer's units, the whole of it the viewport */
static boolean missing_maps_frame_begin(
	long *width,
	long *height)
{
	D3DSurface *surface = rasterizer_get_target(MISSING_MAPS_TARGET_RENDER_PRIMARY, 0);
	D3DSURFACE_DESC description;
	D3DVIEWPORT8 viewport;

	if (!global_d3d_device || !surface)
		return FALSE;
	IDirect3DDevice8_SetRenderTarget(global_d3d_device, surface, NULL);
	IDirect3DSurface8_GetDesc(surface, &description);
	*width = description.Width ? (long)description.Width : 640;
	*height = description.Height ? (long)description.Height : 480;
	viewport.X = 0;
	viewport.Y = 0;
	viewport.Width = *width;
	viewport.Height = *height;
	viewport.MinZ = 0.0f;
	viewport.MaxZ = 1.0f;
	IDirect3DDevice8_SetViewport(global_d3d_device, &viewport);
	IDirect3DDevice8_Clear(global_d3d_device, 0, NULL, D3DCLEAR_TARGET, MISSING_MAPS_BACKGROUND, 1.0f, 0);

	return TRUE;
}

static void missing_maps_draw(
	void)
{
	long width, height;
	long block_height = 0;
	long block_width = 0;
	long x, y;
	short line_index;

	if (!missing_maps_frame_begin(&width, &height))
		return;
	for (line_index = 0; line_index < missing_maps_line_count; line_index++)
	{
		struct missing_maps_line const *line = &missing_maps_lines[line_index];
		long line_width = (long)strlen(line->text) * MISSING_MAPS_CELL_WIDTH * line->scale;

		block_height += MISSING_MAPS_CELL_HEIGHT * line->scale;
		block_width = MAX(block_width, line_width);
	}
	/* (left aligned, the block in the middle of the screen) */
	x = MAX(0, (width - block_width) / 2);
	y = MAX(0, (height - block_height) / 2);
	for (line_index = 0; line_index < missing_maps_line_count; line_index++)
	{
		struct missing_maps_line const *line = &missing_maps_lines[line_index];

		missing_maps_draw_text(line->text, x, y, line->scale, line->color);
		y += MISSING_MAPS_CELL_HEIGHT * line->scale;
	}
	missing_maps_flush();
	rasterizer_present(NULL, NULL);

	return;
}

/* B or Back pressed (on any pad, or Escape) since the last frame */
static boolean missing_maps_quit_pressed(
	void)
{
	short gamepad_index;

	if (input_key_is_down(_key_escape))
		return TRUE;
	for (gamepad_index = 0; gamepad_index < MAXIMUM_GAMEPADS; gamepad_index++)
	{
		struct gamepad_state const *gamepad = input_get_gamepad_state(gamepad_index);

		if (gamepad &&
			(gamepad->buttons[_gamepad_analog_button_b] == 1 || gamepad->buttons[_gamepad_binary_button_back] == 1))
		{
			return TRUE;
		}
	}

	return FALSE;
}

/* ---------- public code */

void missing_maps_screen(
	char const *map_name)
{
	char folder[MISSING_MAPS_LINE_LENGTH * 2];
	char line[MISSING_MAPS_LINE_LENGTH];
	/* (the body's letters at twice their size: 46 to a line of 640) */
	short const columns = (short)((640 - 2 * 24) / (MISSING_MAPS_CELL_WIDTH * 2));

	missing_maps_folder(folder, sizeof(folder));
	error(_error_silent, "missing maps screen: '%s' not found in %s", map_name ? map_name : "ui", folder);
	missing_maps_line_count = 0;
	missing_maps_add_line("Maps missing", MISSING_MAPS_TITLE_COLOR, 4);
	missing_maps_add_line("", MISSING_MAPS_TEXT_COLOR, 2);
	missing_maps_add_line("No ui.map in this folder:", MISSING_MAPS_TEXT_COLOR, 2);
	missing_maps_add_wrapped(folder, MISSING_MAPS_FOLDER_COLOR, 2, columns);
	missing_maps_add_line("", MISSING_MAPS_TEXT_COLOR, 2);
	missing_maps_add_line("Put the Xbox maps from your", MISSING_MAPS_TEXT_COLOR, 2);
	missing_maps_add_line("Halo disc in that folder.", MISSING_MAPS_TEXT_COLOR, 2);
	_snprintf(line, sizeof(line) - 1, "Version: %s", MISSING_MAPS_VERSION);
	line[sizeof(line) - 1] = 0;
	missing_maps_add_line(line, MISSING_MAPS_TEXT_COLOR, 2);
	missing_maps_add_line("", MISSING_MAPS_TEXT_COLOR, 2);
	missing_maps_add_line("Press B to quit.", MISSING_MAPS_TITLE_COLOR, 2);
#ifdef HALO_CONSOLE_LOG_BUFFER
	{
		/* (the lines above in debug.txt before the wait: errors.c) */
		extern void error_log_flush(void);

		error_log_flush();
	}
#endif

	while (TRUE)
	{
		input_frame_begin();
		input_update();
		if (missing_maps_quit_pressed())
		{
			error(_error_silent, "missing maps screen: quitting to the system's menu");
			xbox_dashboard_launch();
		}
		missing_maps_draw();
		input_frame_end();
		Sleep(16);
	}
}

#else

void missing_maps_screen(
	char const *map_name)
{
	(void)map_name;

	return;
}

#endif /* HALO_CONSOLE_MISSING_MAPS_SCREEN */
