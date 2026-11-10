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

/* the palette's named colors (ui_style_color, by UI_COLOR_*), a table a
theme: Halo Blue, the Chupathingy menus' blues (the Guide's mockups), and
Chupathingy Red, the same with red for blue (the site's reds). The colors
that are not blue (white, the warning's yellow, the states' green, the
friend's gold) are the same in both, but for the red's bad state, orange
to stand apart from its labels */
static const unsigned int palettes[NUMBER_OF_UI_THEMES][NUMBER_OF_UI_COLORS] =
{
	[UI_THEME_BLUE] =
	{
		[UI_COLOR_NAME] = 0xFFFFFFFF,
		[UI_COLOR_STATUS] = 0x8FC4FFFF,
		[UI_COLOR_LABEL] = 0x6FB6FFFF,
		[UI_COLOR_HEAD] = 0x6FB6FFFF,
		[UI_COLOR_HEAD_GLOW] = 0x3D8BFF40,
		[UI_COLOR_TEXT] = 0xFFFFFFFF,
		[UI_COLOR_WARNING] = 0xFFCF6BFF,
		[UI_COLOR_SHADE] = 0x01040CC8,
		[UI_COLOR_GLASS_TOP] = 0x0C2A58F6,
		[UI_COLOR_GLASS_BOTTOM] = 0x061430FA,
		[UI_COLOR_PANE] = 0x03102AE0,
		[UI_COLOR_EMBLEM_FRIEND] = 0xC9A227FF,
		[UI_COLOR_EMBLEM_LETTER] = 0x071A33FF,
		[UI_COLOR_TITLE] = 0x3D9BFFFF,
		[UI_COLOR_TITLE_GLOW] = 0x3D9BFFC0,
		[UI_COLOR_NAV] = 0x4FA6FFFF,
		[UI_COLOR_NAV_GLOW] = 0x3D9BFF70,
		[UI_COLOR_LABEL_DIM] = 0x4A6A8FFF,
		[UI_COLOR_LABEL_GLOW] = 0x3D8BFF60,
		[UI_COLOR_SELECTED_GLOW] = 0x9FD4FF8C,
		[UI_COLOR_TEXT_DIM] = 0x9AA9BBFF,
		[UI_COLOR_FAINT] = 0x7F9CBDFF,
		[UI_COLOR_GOOD] = 0x7FE39AFF,
		[UI_COLOR_BAD] = 0xFF8A80FF,
		[UI_COLOR_TAG] = 0x9FC9F2FF,
		[UI_COLOR_PROMPT] = 0x3D9BFFFF,
		[UI_COLOR_PROMPT_GLOW] = 0x3D8BFF80,
		[UI_COLOR_LINE] = 0x6EB4FF8C,
		[UI_COLOR_LINE_GLOW] = 0x3D9BFF30,
		[UI_COLOR_SEPARATOR] = 0x6EB4FF5A,
		[UI_COLOR_FIELD] = 0x02081A80,
		[UI_COLOR_FIELD_EDGE] = 0x50A0FF40,
		[UI_COLOR_PROGRESS_TOP] = 0x5AAEFFFF,
		[UI_COLOR_PROGRESS_BOTTOM] = 0x2F7FD6FF,
		[UI_COLOR_BACKDROP_TOP] = 0x030A1AFF,
		[UI_COLOR_BACKDROP_MIDDLE] = 0x061430FF,
		[UI_COLOR_BACKDROP_BOTTOM] = 0x030915FF,
		[UI_COLOR_STAR] = 0xC8E1FFFF,
		[UI_COLOR_CLEAR] = 0x020611FF,
		[UI_COLOR_PANEL_EDGE] = 0x2F7FD6FF,
		[UI_COLOR_GLOW] = 0x2F8FFFFF,
		[UI_COLOR_RULE] = 0x50A0FF5A,
		[UI_COLOR_TAB] = 0x7FB6EEFF,
		[UI_COLOR_TAB_ON] = 0x2F7FD68C,
		[UI_COLOR_SELECTED_EDGE] = 0x9FD4FFFF,
		[UI_COLOR_ROW] = 0x0A2246E6,
		[UI_COLOR_ROW_EDGE] = 0x50A0FF40,
		[UI_COLOR_ROW_ON] = 0x2F7FD670,
		[UI_COLOR_PLASMA_THREAD] = 0x3373FFFF,
		[UI_COLOR_LABEL_SOFT] = 0x6FA6DDFF,
		[UI_COLOR_DOT_GAME] = 0x4FB3FFFF,
		[UI_COLOR_DOT_OFFLINE] = 0x4A5A70FF,
	},
	[UI_THEME_RED] =
	{
		[UI_COLOR_NAME] = 0xFFFFFFFF,
		[UI_COLOR_STATUS] = 0xFF9C9FFF,
		[UI_COLOR_LABEL] = 0xFF8184FF,
		[UI_COLOR_HEAD] = 0xFF8184FF,
		[UI_COLOR_HEAD_GLOW] = 0xFF505440,
		[UI_COLOR_TEXT] = 0xFFFFFFFF,
		[UI_COLOR_WARNING] = 0xFFCF6BFF,
		[UI_COLOR_SHADE] = 0x0C0202C8,
		[UI_COLOR_GLASS_TOP] = 0x581415F6,
		[UI_COLOR_GLASS_BOTTOM] = 0x300A0AFA,
		[UI_COLOR_PANE] = 0x2A0607E0,
		[UI_COLOR_EMBLEM_FRIEND] = 0xC9A227FF,
		[UI_COLOR_EMBLEM_LETTER] = 0x330C0DFF,
		[UI_COLOR_TITLE] = 0xFF5459FF,
		[UI_COLOR_TITLE_GLOW] = 0xFF5459C0,
		[UI_COLOR_NAV] = 0xFF6569FF,
		[UI_COLOR_NAV_GLOW] = 0xFF545970,
		[UI_COLOR_LABEL_DIM] = 0x8F5254FF,
		[UI_COLOR_LABEL_GLOW] = 0xFF505460,
		[UI_COLOR_SELECTED_GLOW] = 0xFFACAF8C,
		[UI_COLOR_TEXT_DIM] = 0xBB9E9EFF,
		[UI_COLOR_FAINT] = 0xBD8688FF,
		[UI_COLOR_GOOD] = 0x7FE39AFF,
		[UI_COLOR_BAD] = 0xFFA060FF,
		[UI_COLOR_TAG] = 0xF2AAACFF,
		[UI_COLOR_PROMPT] = 0xFF5459FF,
		[UI_COLOR_PROMPT_GLOW] = 0xFF505480,
		[UI_COLOR_LINE] = 0xFF80838C,
		[UI_COLOR_LINE_GLOW] = 0xFF545930,
		[UI_COLOR_SEPARATOR] = 0xFF80835A,
		[UI_COLOR_FIELD] = 0x1A040480,
		[UI_COLOR_FIELD_EDGE] = 0xFF646840,
		[UI_COLOR_PROGRESS_TOP] = 0xFF6F73FF,
		[UI_COLOR_PROGRESS_BOTTOM] = 0xD64347FF,
		[UI_COLOR_BACKDROP_TOP] = 0x1A0505FF,
		[UI_COLOR_BACKDROP_MIDDLE] = 0x300A0AFF,
		[UI_COLOR_BACKDROP_BOTTOM] = 0x150405FF,
		[UI_COLOR_STAR] = 0xFFCED0FF,
		[UI_COLOR_CLEAR] = 0x110303FF,
		[UI_COLOR_PANEL_EDGE] = 0xD64347FF,
		[UI_COLOR_GLOW] = 0xFF474CFF,
		[UI_COLOR_RULE] = 0xFF64685A,
		[UI_COLOR_TAB] = 0xEE8D90FF,
		[UI_COLOR_TAB_ON] = 0xD643478C,
		[UI_COLOR_SELECTED_EDGE] = 0xFFACAFFF,
		[UI_COLOR_ROW] = 0x461011E6,
		[UI_COLOR_ROW_EDGE] = 0xFF646840,
		[UI_COLOR_ROW_ON] = 0xD6434770,
		[UI_COLOR_PLASMA_THREAD] = 0xFF4346FF,
		[UI_COLOR_LABEL_SOFT] = 0xDD7D80FF,
		[UI_COLOR_DOT_GAME] = 0xFF686DFF,
		[UI_COLOR_DOT_OFFLINE] = 0x704E4FFF,
	},
};

static int theme = UI_THEME_BLUE;
static const unsigned int *palette = palettes[UI_THEME_BLUE];

#define PALETTE(name) palette[UI_COLOR_##name]

unsigned int ui_style_color(int name)
{
	return name >= 0 && name < NUMBER_OF_UI_COLORS ? palette[name] : PALETTE(NAME);
}

void ui_style_theme(int which)
{
	if (which < 0 || which >= NUMBER_OF_UI_THEMES)
		which = UI_THEME_BLUE;
	theme = which;
	palette = palettes[which];
	ui_overlay_plasma_thread(PALETTE(PLASMA_THREAD));
}

int ui_style_theme_current(void)
{
	return theme;
}

int ui_style_theme_named(const char *name)
{
	const char *red = "red";

	if (!name)
		return UI_THEME_BLUE;
	/* any case (ASCII), by hand: not every build, the Windows server included, has a case-blind compare */
	for (; *red; name++, red++)
	{
		char c = *name;

		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		if (c != *red)
			return UI_THEME_BLUE;
	}
	return *name ? UI_THEME_BLUE : UI_THEME_RED;
}

static unsigned int fade(unsigned int color, float share);

/* the text kinds: their font, size and color (and glow) */
static const struct
{
	int font;
	float size;
	int color;
	int glow;
} text_kinds[] =
{
	[UI_TEXT_HEADING] = { UI_FONT_MENU, 9.0f, UI_COLOR_HEAD, UI_COLOR_HEAD_GLOW },
	[UI_TEXT_NAME] = { UI_FONT_MENU, 11.0f, UI_COLOR_NAME, -1 },
	[UI_TEXT_STATUS] = { UI_FONT_MENU_SMALL, 8.5f, UI_COLOR_STATUS, -1 },
	[UI_TEXT_TITLE] = { UI_FONT_MENU, 14.0f, UI_COLOR_NAME, UI_COLOR_HEAD_GLOW },
	[UI_TEXT_LINE] = { UI_FONT_MENU_SMALL, 10.0f, UI_COLOR_STATUS, -1 },
	[UI_TEXT_LABEL] = { UI_FONT_MENU_SMALL, 9.0f, UI_COLOR_LABEL, -1 },
	[UI_TEXT_WARNING] = { UI_FONT_MENU_SMALL, 9.0f, UI_COLOR_WARNING, -1 },
};

float ui_style_text_size(int kind)
{
	return kind >= 0 && kind <= UI_TEXT_WARNING ? text_kinds[kind].size : 10.0f;
}

float ui_style_text_faded(int kind, float share, float x, float y, int align, const char *text)
{
	if (kind < 0 || kind > UI_TEXT_WARNING)
		kind = UI_TEXT_LINE;
	if (text_kinds[kind].glow >= 0)
	{
		return ui_overlay_text_glow(text_kinds[kind].font, text_kinds[kind].size, x, y, align,
			fade(palette[text_kinds[kind].color], share), fade(palette[text_kinds[kind].glow], share), text);
	}
	return ui_overlay_text(text_kinds[kind].font, text_kinds[kind].size, x, y, align,
		fade(palette[text_kinds[kind].color], share), text);
}

float ui_style_text(int kind, float x, float y, int align, const char *text)
{
	return ui_style_text_faded(kind, 1.0f, x, y, align, text);
}

float ui_style_text_scaled(int kind, float scale, float x, float y, int align, const char *text)
{
	if (kind < 0 || kind > UI_TEXT_WARNING)
		kind = UI_TEXT_LINE;
	if (text_kinds[kind].glow >= 0)
	{
		return ui_overlay_text_glow(text_kinds[kind].font, text_kinds[kind].size * scale, x, y, align,
			palette[text_kinds[kind].color], palette[text_kinds[kind].glow], text);
	}
	return ui_overlay_text(text_kinds[kind].font, text_kinds[kind].size * scale, x, y, align,
		palette[text_kinds[kind].color], text);
}

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
				fade(PALETTE(GLOW), (float)(edge & 0xFF) / 255.0f * 0.30f / (float)ring));
		}
	}
	ui_overlay_plasma(x, y, width, height, radius, color, style_clock);
	if (edge & 0xFF)
		ui_overlay_outline(x, y, width, height, radius, 1.0f, edge);
}

void ui_style_glass(float x, float y, float width, float height, float radius, unsigned int color,
	unsigned int edge)
{
	ui_overlay_rect(x, y, width, height, radius, color);
	if (edge & 0xFF)
		ui_overlay_outline(x, y, width, height, radius, 1.0f, edge);
}

void ui_style_panel(float x, float y, float width, float height, float share)
{
	int ring;

	/* (glass, nearly opaque, so that nothing under it reads through; its
	edge's glow around it. The plasma is the selection's alone, as in
	Halo's menus) */
	for (ring = 1; ring <= 4; ring++)
	{
		ui_overlay_outline(x - ring, y - ring, width + 2 * ring, height + 2 * ring, 12 + ring, 1.0f,
			fade(PALETTE(GLOW), share * 0.30f / (float)ring));
	}
	ui_overlay_gradient(x, y, width, height, 12, fade(PALETTE(GLASS_TOP), share), fade(PALETTE(GLASS_BOTTOM), share));
	ui_overlay_outline(x, y, width, height, 12, 1.0f, fade(PALETTE(PANEL_EDGE), share));
}

void ui_style_row(float x, float y, float width, float height, int selected)
{
	if (selected)
		ui_style_surface(x, y, width, height, 7, PALETTE(ROW_ON), PALETTE(SELECTED_EDGE), 1);
	else
		ui_style_glass(x, y, width, height, 7, PALETTE(ROW), PALETTE(ROW_EDGE));
}

void ui_style_setting(float x, float y, float width, float height, float scale, const char *label, const char *badge,
	const char *value, int selected)
{
	float size = ui_style_text_size(UI_TEXT_NAME) * scale;
	float text_y = y + (height - size) * 0.5f, right = x + width - size * 1.2f, label_end;
	float value_width = ui_overlay_text_width(UI_FONT_MENU, size, value);

	ui_style_row(x, y, width, height, selected);
	label_end = x + size * 1.1f + ui_style_text_scaled(UI_TEXT_NAME, scale, x + size * 1.1f, text_y, UI_ALIGN_LEFT,
		label);
	if (badge && badge[0])
	{
		float badge_scale = scale * 0.75f, badge_size = ui_style_text_size(UI_TEXT_WARNING) * badge_scale;
		float badge_width = ui_overlay_text_width(UI_FONT_MENU_SMALL, badge_size, badge) + badge_size;
		float badge_x = label_end + size * 0.6f, badge_height = badge_size * 1.6f;

		ui_overlay_outline(badge_x, y + (height - badge_height) * 0.5f, badge_width, badge_height, 3.0f, 0.8f,
			fade(PALETTE(WARNING), 0.7f));
		ui_style_text_scaled(UI_TEXT_WARNING, badge_scale, badge_x + badge_size * 0.5f, y + (height - badge_size) * 0.5f,
			UI_ALIGN_LEFT, badge);
	}
	/* (the value between its arrows, as the menus' settings have it) */
	ui_overlay_text(UI_FONT_MENU, size, right, text_y, UI_ALIGN_RIGHT, PALETTE(TAB), "\xE2\x80\xBA");
	ui_style_text_scaled(UI_TEXT_NAME, scale, right - size * 1.2f, text_y, UI_ALIGN_RIGHT, value);
	ui_overlay_text(UI_FONT_MENU, size, right - size * 1.8f - value_width, text_y, UI_ALIGN_RIGHT, PALETTE(TAB),
		"\xE2\x80\xB9");
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
			ui_style_surface(x, y, width, height, 6, PALETTE(TAB_ON), PALETTE(SELECTED_EDGE), 1);
			ui_overlay_text_glow(UI_FONT_MENU, size, x + width * 0.5f, y + (height - size) * 0.5f, UI_ALIGN_CENTER,
				0xFFFFFFFF, PALETTE(PROMPT_GLOW), names[index]);
		}
		else
		{
			ui_overlay_text(UI_FONT_MENU, size, x + width * 0.5f, y + (height - size) * 0.5f, UI_ALIGN_CENTER,
				PALETTE(TAB), names[index]);
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
		x += ui_overlay_text_glow(UI_FONT_MENU, size * 0.72f, x, y + size * 0.14f, UI_ALIGN_LEFT, PALETTE(PROMPT),
			PALETTE(PROMPT_GLOW), prompts[index].word);
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
		ui_overlay_text_glow(UI_FONT_TITLE, 17.0f, right, y + 6, UI_ALIGN_RIGHT, PALETTE(TITLE), PALETTE(TITLE), title);
	ui_overlay_rect(left - 4, y + 42, right - left + 8, 1, 0, PALETTE(RULE));
	if (!name || !name[0])
		return;
	/* the emblem: the player's color, their name's first letter */
	ui_overlay_rect(left, y, 32, 32, 5, color);
	ui_overlay_outline(left, y, 32, 32, 5, 1.0f, 0xFFFFFF90);
	initial[0] = name[0] >= 'a' && name[0] <= 'z' ? (char)(name[0] - 32) : name[0];
	initial[1] = 0;
	if ((unsigned char)initial[0] < 0x80)
		ui_overlay_text(UI_FONT_BOLD, 17.0f, left + 16, y + 6, UI_ALIGN_CENTER, 0xFFFFFFFF, initial);
	ui_overlay_text(UI_FONT_MENU, 15.0f, left + 44, y + 1, UI_ALIGN_LEFT, PALETTE(NAME), name);
	if (status && status[0])
		ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, left + 44, y + 20, UI_ALIGN_LEFT, PALETTE(STATUS), status);
}

#endif
