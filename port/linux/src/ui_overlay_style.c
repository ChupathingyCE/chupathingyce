/*
UI_OVERLAY_STYLE.C

The Guide's look as pieces any screen of the overlay can draw (ui_overlay.h,
"the Guide's look"): the Xbox menus' plasma under translucent panels and
rows, a glow on the selected row and on the edges, the section tabs with
their bumpers, the prompt bar and the player's card. The Guide
(port/linux/game/guide.c) draws with them, and the setup launcher
(launcher.c) is to. Built only on ui_overlay.h's own drawing, so it is the
same over every back end (ui_overlay.c's OpenGL and bgfx, the Switch's
deko3d).
*/

#ifdef HALO_GAME_BROWSER

#include "ui_overlay.h"

#include <string.h>

/* the Chupathingy menus' blues (the Guide's mockups) */
enum
{
	COLOR_PANEL = 0x0A2652E0,
	COLOR_PANEL_EDGE = 0x2F7FD6FF,
	COLOR_GLOW = 0x2F8FFFFF,
	COLOR_RULE = 0x50A0FF5A,
	COLOR_TITLE = 0x3D9BFFFF,
	COLOR_NAME = 0xFFFFFFFF,
	COLOR_STATUS = 0x8FC4FFFF,
	COLOR_TAB = 0x7FB6EEFF,
	COLOR_TAB_ON = 0x2F7FD68C,
	COLOR_SELECTED_EDGE = 0x9FD4FFFF,
	COLOR_ROW = 0x0A2246E6,
	COLOR_ROW_EDGE = 0x50A0FF40,
	COLOR_ROW_ON = 0x2F7FD670,
	COLOR_PROMPT = 0x3D9BFFFF,
	COLOR_PROMPT_GLOW = 0x3D8BFF80,
};

static unsigned int style_clock;

static unsigned int fade(unsigned int color, float share)
{
	float alpha = (float)(color & 0xFF) * share;

	return (color & 0xFFFFFF00) | (unsigned int)(alpha < 0.0f ? 0.0f : alpha > 255.0f ? 255.0f : alpha);
}

void ui_style_clock(unsigned int milliseconds)
{
	style_clock = milliseconds;
}

void ui_style_surface(float x, float y, float width, float height, float radius, unsigned int color,
	unsigned int edge, int lit)
{
	int ring;

	if (lit)
	{
		for (ring = 1; ring <= 4; ring++)
		{
			ui_overlay_outline(x - ring, y - ring, width + 2 * ring, height + 2 * ring, radius + ring, 1.0f,
				fade(COLOR_GLOW, (float)(edge & 0xFF) / 255.0f * 0.30f / (float)ring));
		}
	}
	ui_overlay_plasma(x, y, width, height, radius, color, style_clock);
	if (edge & 0xFF)
		ui_overlay_outline(x, y, width, height, radius, 1.0f, edge);
}

void ui_style_panel(float x, float y, float width, float height, float share)
{
	ui_style_surface(x, y, width, height, 12, fade(COLOR_PANEL, share), fade(COLOR_PANEL_EDGE, share), 1);
}

void ui_style_row(float x, float y, float width, float height, int selected)
{
	ui_style_surface(x, y, width, height, 7, selected ? COLOR_ROW_ON : COLOR_ROW,
		selected ? COLOR_SELECTED_EDGE : COLOR_ROW_EDGE, selected);
}

/* a bumper's glyph: the device's, or (the keyboard) the screen's own key */
static float bumper(int button, const char *key, float size, float x, float y, int measure_only)
{
	if (ui_overlay_keyboard() && key)
		return measure_only ? ui_overlay_key_width(key, size) : ui_overlay_key(key, size, x, y, 0xFFFFFFFF);
	return measure_only ? ui_overlay_button_width(button, size) : ui_overlay_button(button, size, x, y, 0xFFFFFFFF);
}

void ui_style_tabs(float left, float right, float y, float height, const char *const *names, int count,
	int selected, const char *left_key, const char *right_key, float *starts, float *ends)
{
	float size = height * 0.45f, pad = height * 0.4f, gap = 3.0f, total = 0.0f, x;
	float glyph = height * 0.64f;
	int index;

	for (index = 0; index < count; index++)
		total += ui_overlay_text_width(UI_FONT_MENU, size, names[index]) + 2 * pad + gap;
	total -= gap;
	bumper(UI_BUTTON_LEFT_SHOULDER, left_key, glyph, left, y + (height - glyph) * 0.5f, 0);
	bumper(UI_BUTTON_RIGHT_SHOULDER, right_key, glyph,
		right - bumper(UI_BUTTON_RIGHT_SHOULDER, right_key, glyph, 0, 0, 1), y + (height - glyph) * 0.5f, 0);
	x = (left + right) * 0.5f - total * 0.5f;
	for (index = 0; index < count; index++)
	{
		float width = ui_overlay_text_width(UI_FONT_MENU, size, names[index]) + 2 * pad;

		if (index == selected)
		{
			ui_style_surface(x, y, width, height, 6, COLOR_TAB_ON, COLOR_SELECTED_EDGE, 1);
			ui_overlay_text_glow(UI_FONT_MENU, size, x + width * 0.5f, y + (height - size) * 0.5f, UI_ALIGN_CENTER,
				0xFFFFFFFF, COLOR_PROMPT_GLOW, names[index]);
		}
		else
		{
			ui_overlay_text(UI_FONT_MENU, size, x + width * 0.5f, y + (height - size) * 0.5f, UI_ALIGN_CENTER,
				COLOR_TAB, names[index]);
		}
		if (starts)
			starts[index] = x;
		if (ends)
			ends[index] = x + width;
		x += width + gap;
	}
}

static float prompt_glyph(const struct ui_style_prompt *prompt, float size, float x, float y, int measure_only)
{
	if (prompt->key && ui_overlay_keyboard())
		return measure_only ? ui_overlay_key_width(prompt->key, size) : ui_overlay_key(prompt->key, size, x, y, 0xFFFFFFFF);
	return measure_only ? ui_overlay_button_width(prompt->button, size) :
		ui_overlay_button(prompt->button, size, x, y, 0xFFFFFFFF);
}

void ui_style_prompts(float right, float y, float size, const struct ui_style_prompt *prompts, int count,
	float *starts, float *ends)
{
	float gap = size, total = 0.0f, x;
	int index;

	for (index = 0; index < count; index++)
	{
		total += prompt_glyph(&prompts[index], size, 0, 0, 1) + 4.0f +
			ui_overlay_text_width(UI_FONT_MENU, size * 0.72f, prompts[index].word);
		if (index)
			total += gap;
	}
	x = right - total;
	for (index = 0; index < count; index++)
	{
		if (starts)
			starts[index] = x;
		x += prompt_glyph(&prompts[index], size, x, y, 0) + 4.0f;
		x += ui_overlay_text_glow(UI_FONT_MENU, size * 0.72f, x, y + size * 0.14f, UI_ALIGN_LEFT, COLOR_PROMPT,
			COLOR_PROMPT_GLOW, prompts[index].word);
		if (ends)
			ends[index] = x;
		x += gap;
	}
}

void ui_style_card(float left, float right, float y, const char *name, const char *status, unsigned int color,
	const char *title)
{
	char initial[2];

	if (title && title[0])
		ui_overlay_text_glow(UI_FONT_TITLE, 17.0f, right, y + 6, UI_ALIGN_RIGHT, COLOR_TITLE, COLOR_TITLE, title);
	ui_overlay_rect(left - 4, y + 42, right - left + 8, 1, 0, COLOR_RULE);
	if (!name || !name[0])
		return;
	/* the emblem: the player's color, their name's first letter */
	ui_overlay_rect(left, y, 32, 32, 5, color);
	ui_overlay_outline(left, y, 32, 32, 5, 1.0f, 0xFFFFFF90);
	initial[0] = name[0] >= 'a' && name[0] <= 'z' ? (char)(name[0] - 32) : name[0];
	initial[1] = 0;
	if ((unsigned char)initial[0] < 0x80)
		ui_overlay_text(UI_FONT_BOLD, 17.0f, left + 16, y + 6, UI_ALIGN_CENTER, 0xFFFFFFFF, initial);
	ui_overlay_text(UI_FONT_BOLD, 15.0f, left + 44, y + 1, UI_ALIGN_LEFT, COLOR_NAME, name);
	if (status && status[0])
		ui_overlay_text(UI_FONT_REGULAR, 9.5f, left + 44, y + 20, UI_ALIGN_LEFT, COLOR_STATUS, status);
}

#endif
