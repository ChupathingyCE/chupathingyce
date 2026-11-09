/*
CONSOLE_LINK_PANEL.C

The DELTA PROFILE panel over a console's Online Games (console_link_panel.h).
Game code, built by a port that has Online Games of its own (the original
Xbox's port/xbox/game/xbox_browser_screen.c, the Xbox 360's and the Wii U's
copies of it), with console_link.c under it.
*/

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "input/input.h"
#include "interface/ui_widget.h"
#include "rasterizer/rasterizer.h"
#include "text/draw_string.h"
#include "text/font_group.h"

#include <stdio.h>
#include <string.h>

#include "console_link.h"
#include "console_link_panel.h"

/* (ui_widget.c's private enums) */
enum
{
	_justify_left = 0,
	_justify_right,
	_justify_center,
	_panel_sound_cursor = 1,
	_panel_sound_forward = 2,
	_panel_sound_back = 3,

	/* the panel, inside the title-safe area of the 640x480 screen */
	PANEL_LEFT = 64,
	PANEL_RIGHT = 576,
	PANEL_TOP = 84,
	PANEL_BOTTOM = 412,
	PANEL_INSET = 14,
	/* the QR code's module, in pixels, and its quiet zone, in modules */
	QR_MODULE = 4,
	QR_QUIET = 3
};

#define COLOR_SHADE 0xD0000000
#define COLOR_PANEL 0xFF081530
#define COLOR_RULE 0xFF2A62C8
#define COLOR_CODE_BAND 0xFF0C1E40
#define COLOR_QR_LIGHT 0xFFFFFFFF
#define COLOR_QR_DARK 0xFF000000

static struct
{
	boolean active;
} panel;

/* ---------- drawing */

static void set_color(
	real_argb_color *color,
	real red,
	real green,
	real blue)
{
	color->alpha = 1.0f;
	color->red = red;
	color->green = green;
	color->blue = blue;
}

static void quad(
	short x0,
	short y0,
	short x1,
	short y1,
	pixel32 color)
{
	rectangle2d bounds;

	bounds.x0 = x0;
	bounds.y0 = y0;
	bounds.x1 = x1;
	bounds.y1 = y1;
	draw_quad(&bounds, color);
}

static void widen(
	char const *string,
	wchar_t *wide,
	short size)
{
	short index;

	for (index = 0; index < size - 1 && string[index]; index++)
		wide[index] = (wchar_t)(unsigned char)string[index];
	wide[index] = 0;
}

static void text(
	long font,
	short justification,
	real_argb_color const *color,
	short x0,
	short y0,
	short x1,
	short y1,
	char const *string)
{
	wchar_t wide[128];
	rectangle2d bounds;

	if (font == NONE || !string)
		return;
	widen(string, wide, NUMBEROF(wide));
	bounds.x0 = x0;
	bounds.y0 = y0;
	bounds.x1 = x1;
	bounds.y1 = y1;
	draw_string_set_draw_mode(font, NONE, justification, 0, color);
	rasterizer_draw_unicode_string(&bounds, &bounds, NULL, 0, wide);
}

static short line_height(
	long font)
{
	struct font_header *header = font != NONE ? font_definition_get(font) : NULL;

	return header ? (short)(header->ascending_height + header->descending_height) : 16;
}

static short text_width(
	long font,
	char const *string)
{
	wchar_t wide[128];
	rectangle2d bounds, text_bounds, cursor_bounds;
	real_argb_color color;

	if (font == NONE || !string)
		return 0;
	widen(string, wide, NUMBEROF(wide));
	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = 640;
	bounds.y1 = 480;
	set_color(&color, 1.0f, 1.0f, 1.0f);
	draw_string_set_draw_mode(font, NONE, _justify_left, 0, &color);
	draw_unicode_string_compute_bounds(&bounds, wide, &text_bounds, &cursor_bounds);
	return (short)(text_bounds.x1 - text_bounds.x0);
}

/* words, wrapped to x0..x1 from y down: the next line's y */
static short paragraph(
	long font,
	real_argb_color const *color,
	short x0,
	short x1,
	short y,
	char const *words)
{
	short height = (short)(line_height(font) + 2);
	char line[128];

	while (*words)
	{
		short length = 0, fits = 0;

		while (*words == ' ')
			words++;
		/* (the most words that fit; a word too long for a line is cut) */
		while (words[length] && length < (short)sizeof(line) - 1)
		{
			short end = length;

			while (words[end] && words[end] != ' ' && end < (short)sizeof(line) - 1)
				end++;
			memcpy(line, words, (size_t)end);
			line[end] = 0;
			if (fits && text_width(font, line) > x1 - x0)
				break;
			fits = end;
			length = end;
			while (words[length] == ' ')
				length++;
		}
		if (!fits)
			break;
		memcpy(line, words, (size_t)fits);
		line[fits] = 0;
		text(font, _justify_left, color, x0, y, x1, (short)(y + height), line);
		y = (short)(y + height);
		words += length;
	}
	return y;
}

static void draw_qr(
	short x0,
	short y0)
{
	short size = (short)console_link_qr_size();
	short side = (short)((size + 2 * QR_QUIET) * QR_MODULE);
	short x, y;

	if (!size)
		return;
	quad(x0, y0, (short)(x0 + side), (short)(y0 + side), COLOR_QR_LIGHT);
	for (y = 0; y < size; y++)
	{
		short top = (short)(y0 + (QR_QUIET + y) * QR_MODULE);

		/* (a row's dark runs, a quad each) */
		for (x = 0; x < size; x++)
		{
			short end = x;

			if (!console_link_qr_dark(x, y))
				continue;
			while (end + 1 < size && console_link_qr_dark(end + 1, y))
				end++;
			quad((short)(x0 + (QR_QUIET + x) * QR_MODULE), top, (short)(x0 + (QR_QUIET + end + 1) * QR_MODULE),
				(short)(top + QR_MODULE), COLOR_QR_DARK);
			x = end;
		}
	}
}

static void prompt(
	long font,
	real_argb_color const *color,
	wchar_t const *words)
{
	rectangle2d bounds;

	if (font == NONE)
		return;
	bounds.x0 = PANEL_LEFT + PANEL_INSET;
	bounds.y0 = (short)(PANEL_BOTTOM - PANEL_INSET - line_height(font));
	bounds.x1 = PANEL_RIGHT - PANEL_INSET;
	bounds.y1 = PANEL_BOTTOM - 4;
	draw_string_set_draw_mode(font, NONE, _justify_left, 0, color);
	draw_string_and_hack_in_icons(&bounds, &bounds, NULL, 0, words, FALSE);
}

/* ---------- public code */

void console_link_panel_open(
	void)
{
	panel.active = TRUE;
	/* (what the site says of the profile now) */
	console_link_refresh();
	error(_error_log, "online games: Delta profile opened");
}

int console_link_panel_active(
	void)
{
	return panel.active;
}

void console_link_panel_button(
	short button)
{
	struct console_link_view view;

	console_link_view(&view);
	switch (button)
	{
	case _gamepad_analog_button_a:
		if (view.phase == CONSOLE_LINK_CONFIRM)
		{
			ui_play_audio_feedback_sound(_panel_sound_forward);
			console_link_answer(TRUE);
		}
		else if (view.phase == CONSOLE_LINK_DONE)
		{
			ui_play_audio_feedback_sound(_panel_sound_forward);
			console_link_cancel();
		}
		else if ((view.phase == CONSOLE_LINK_IDLE || view.phase == CONSOLE_LINK_FAILED) && !view.busy)
		{
			ui_play_audio_feedback_sound(_panel_sound_forward);
			console_link_begin();
		}
		break;
	case _gamepad_analog_button_b:
	case _gamepad_binary_button_start:
	case _gamepad_binary_button_back:
		ui_play_audio_feedback_sound(_panel_sound_back);
		if (view.phase == CONSOLE_LINK_CONFIRM)
			console_link_answer(FALSE);
		else if (view.phase != CONSOLE_LINK_CONFIRMING)
		{
			/* (a code shown is let lapse: the site forgets it) */
			console_link_cancel();
			panel.active = FALSE;
		}
		break;
	default:
		break;
	}
}

void console_link_panel_render(
	long font,
	long heading_font)
{
	struct console_link_view view;
	real_argb_color title_color, head_color, text_color, dim_color, note_color, code_color;
	short x0 = PANEL_LEFT + PANEL_INSET, x1 = PANEL_RIGHT - PANEL_INSET;
	short height = (short)(line_height(font) + 2);
	short y = PANEL_TOP + 10;
	char line[128];

	if (!panel.active)
		return;
	console_link_update();
	console_link_view(&view);
	set_color(&title_color, 0.24f, 0.55f, 1.0f);
	set_color(&head_color, 0.5f, 0.69f, 1.0f);
	set_color(&text_color, 0.9f, 0.93f, 0.99f);
	set_color(&dim_color, 0.56f, 0.65f, 0.78f);
	set_color(&note_color, 0.94f, 0.54f, 0.29f);
	set_color(&code_color, 1.0f, 1.0f, 1.0f);

	quad(0, 0, 640, 480, COLOR_SHADE);
	quad(PANEL_LEFT, PANEL_TOP, PANEL_RIGHT, PANEL_BOTTOM, COLOR_PANEL);
	text(heading_font != NONE ? heading_font : font, _justify_left, &title_color, x0, y, x1,
		(short)(y + line_height(heading_font != NONE ? heading_font : font)), "DELTA PROFILE");
	y = (short)(y + line_height(heading_font != NONE ? heading_font : font) + 4);
	quad(x0, y, x1, (short)(y + 1), COLOR_RULE);
	y = (short)(y + 10);

	switch (view.phase)
	{
	case CONSOLE_LINK_GETTING_CODE:
		paragraph(font, &dim_color, x0, x1, y, "Getting a code...");
		prompt(font, &text_color, L"%b-button Back");
		break;
	case CONSOLE_LINK_CODE_SHOWN:
	{
		short qr_side = (short)((console_link_qr_size() + 2 * QR_QUIET) * QR_MODULE);
		short words_right = (short)(qr_side ? x1 - qr_side - 16 : x1);
		short code_height = (short)(2 * height);

		draw_qr((short)(x1 - qr_side), y);
		y = paragraph(font, &text_color, x0, words_right, y,
			"On your phone or computer, go to " CONSOLE_LINK_PAGE ", sign in, and enter this code:");
		y = (short)(y + 6);
		quad(x0, y, words_right, (short)(y + code_height), COLOR_CODE_BAND);
		text(heading_font != NONE ? heading_font : font, _justify_center, &code_color, x0,
			(short)(y + (code_height - line_height(heading_font != NONE ? heading_font : font)) / 2), words_right,
			(short)(y + code_height), view.code);
		y = (short)(y + code_height + 8);
		if (qr_side)
			y = paragraph(font, &dim_color, x0, words_right, y, "Or scan the QR code with your phone's camera.");
		sprintf(line, "Waiting for the code... %lu:%02lu left.", view.code_left / 60000, view.code_left / 1000 % 60);
		paragraph(font, &dim_color, x0, words_right, (short)(y + 4), line);
		prompt(font, &text_color, L"%b-button Cancel");
		break;
	}
	case CONSOLE_LINK_CONFIRM:
		sprintf(line, "@%s entered the code. Link this console to @%s?", view.asker, view.asker);
		y = paragraph(font, &text_color, x0, x1, y, line);
		if (view.previous[0])
		{
			sprintf(line, "Its games count for @%s now. They will count for @%s instead.", view.previous, view.asker);
			y = paragraph(font, &dim_color, x0, x1, (short)(y + 4), line);
		}
		paragraph(font, &dim_color, x0, x1, (short)(y + 4),
			"If you didn't enter the code yourself, press B.");
		prompt(font, &text_color, L"%a-button Link     %b-button Don't link");
		break;
	case CONSOLE_LINK_CONFIRMING:
		paragraph(font, &dim_color, x0, x1, y, "Linking...");
		break;
	case CONSOLE_LINK_DONE:
		paragraph(font, &text_color, x0, x1, y, view.message);
		prompt(font, &text_color, L"%a-button OK     %b-button Back");
		break;
	case CONSOLE_LINK_FAILED:
		paragraph(font, &note_color, x0, x1, y, view.message);
		prompt(font, &text_color, L"%a-button Try again     %b-button Back");
		break;
	default:
		if (view.linked)
		{
			sprintf(line, "Linked to @%s", view.handle);
			y = paragraph(font, &head_color, x0, x1, y, line);
			if (view.display[0] || view.tag[0])
			{
				sprintf(line, "%s%s%s%s", view.tag[0] ? "[" : "", view.tag, view.tag[0] ? "] " : "", view.display);
				y = paragraph(font, &text_color, x0, x1, y, line);
			}
			y = paragraph(font, &dim_color, x0, x1, (short)(y + 6),
				"Your games on this console count toward your Delta profile, and players see your Delta name.");
			paragraph(font, &dim_color, x0, x1, (short)(y + 6),
				"To unlink it, sign in at " CONSOLE_LINK_SITE " and open your profile's linked copies.");
			prompt(font, &text_color, L"%a-button Link to another profile     %b-button Back");
		}
		else
		{
			y = paragraph(font, &text_color, x0, x1, y, "This console isn't linked to a Delta profile.");
			paragraph(font, &dim_color, x0, x1, (short)(y + 6),
				"Link it to count your games toward your profile on " CONSOLE_LINK_SITE
				" and show your Delta name to other players. You'll get a code to enter on your phone or computer.");
			prompt(font, &text_color, L"%a-button Get a code     %b-button Back");
		}
		break;
	}
}

void console_link_panel_summary(
	char *text,
	int size)
{
	struct console_link_view view;
	char line[48];

	console_link_view(&view);
	if (view.linked)
		sprintf(line, "DELTA: @%s", view.handle);
	else
		strcpy(line, "DELTA: NOT LINKED");
	if (size > 0)
	{
		strncpy(text, line, (size_t)size - 1);
		text[size - 1] = 0;
	}
}
