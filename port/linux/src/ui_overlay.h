/*
UI_OVERLAY.H

The overlay (configure.py --game-browser, HALO_GAME_BROWSER): the screens
this port draws over the game's own (the game list's Online Games and its
Link Profile panel), at the window's resolution, after the game's
640x480 picture is scaled into it. A screen lays itself out in the menus'
640x480; the overlay maps that to the picture's place in the window, so
text and buttons are sharp at any size. ui_overlay.c.

Called from the game's thread while it draws a frame; drawn at that frame's
Present (d3d8_gl.c). Plain types only: the game's units (64-bit macOS
rewrites their long) and the platform's both call it.
*/

#ifndef __UI_OVERLAY_H
#define __UI_OVERLAY_H

enum
{
	UI_FONT_REGULAR,
	UI_FONT_BOLD,
	/* the menus' faces: their text's (the fonts of ui\large_ui and
	ui\small_ui, fonts.json) and their titles' (OpenCE). A character they
	lack is drawn in Noto Sans Bold */
	UI_FONT_MENU,
	UI_FONT_MENU_SMALL,
	UI_FONT_TITLE,
};

enum
{
	UI_ALIGN_LEFT,
	UI_ALIGN_CENTER,
	UI_ALIGN_RIGHT,
};

/* a controller's button, drawn as the device the player last used names
it: an Xbox-like pad's by snapchipper's icons (port/assets/prompts), a
Nintendo pad's as grey buttons with its own letters, a PlayStation pad's
face buttons by Kenney's Input Prompts, the others' as dark pills, and the
keyboard's as light key caps */
enum
{
	UI_BUTTON_A,
	UI_BUTTON_B,
	UI_BUTTON_X,
	UI_BUTTON_Y,
	UI_BUTTON_START,
	UI_BUTTON_LEFT_TRIGGER,
	UI_BUTTON_RIGHT_TRIGGER,
	UI_BUTTON_LEFT_SHOULDER,
	UI_BUTTON_RIGHT_SHOULDER,
	UI_BUTTON_DPAD_LEFT,
	UI_BUTTON_DPAD_RIGHT,
	UI_BUTTON_BACK,

	NUMBER_OF_UI_BUTTONS
};

/* colors are 0xRRGGBBAA */

/* a filled rectangle (radius: rounded corners); with two colors, from the
top's to the bottom's */
void ui_overlay_rect(float x, float y, float width, float height, float radius, unsigned int color);
void ui_overlay_gradient(float x, float y, float width, float height, float radius, unsigned int top,
	unsigned int bottom);
/* a rectangle's outline, thickness wide, inside it */
void ui_overlay_outline(float x, float y, float width, float height, float radius, float thickness,
	unsigned int color);

/* text (UTF-8) on one line, size its height in the 640x480 layout, y its
top; its width */
float ui_overlay_text(int font, float size, float x, float y, int align, unsigned int color, const char *text);
float ui_overlay_text_width(int font, float size, const char *text);
/* text as the Xbox menus draw it: a soft glow of glow's color around it and
a drop shadow under it */
float ui_overlay_text_glow(int font, float size, float x, float y, int align, unsigned int color,
	unsigned int glow, const char *text);

/* the Xbox menus' pieces, drawn by code: a panel (rounded, its edge, a faint
inner glow; lit: brighter, with an outer glow), the band under a screen's
title (a bright rule, then a translucent field down to bottom) and the
prompt bar (a rule, then a dark strip down to bottom), the last two from
left to right */
void ui_overlay_panel(float x, float y, float width, float height, int lit);
void ui_overlay_band(float left, float right, float top, float bottom);
void ui_overlay_prompt_bar(float left, float right, float top, float bottom);

/* what is drawn from here on goes over what was drawn before at a lower
layer (0 to 3; each frame starts at 0): text is drawn over the shapes of
its layer, and under those of the layers above, as a dialog over a list */
void ui_overlay_layer(int layer);

/* a button's glyph, size high, at x (its left), y (its top); its width.
color's alpha dims it (a prompt that does nothing now) */
float ui_overlay_button(int button, float size, float x, float y, unsigned int color);
float ui_overlay_button_width(int button, float size);

/* a player's platform logo (delta.h's enum delta_platform), size high,
centered on x, y its top, with a drop shadow if shadow: nothing for an
unknown platform. Returns whether it drew one */
int ui_overlay_platform(int platform, float size, float x, float y, unsigned int color, int shadow);

/* a picture (RGBA, 4 bytes a pixel, rows from the top; width by height)
drawn in a rectangle, tinted by color (0xFFFFFFFF as it is). One picture a
frame: serial names it (a new serial's pixels are copied; the same
serial's are not read again), 0 none */
void ui_overlay_picture(int serial, const unsigned char *pixels, int width, int height, float x, float y,
	float draw_width, float draw_height, unsigned int color);

/* a place the overlay leaves for the game's own drawing (a map's picture),
for this frame (at most 4) */
void ui_overlay_cutout(float x, float y, float width, float height);

/* whether the overlay draws (no window: the dedicated server, a run with
the null renderer) */
int ui_overlay_available(void);

/* (d3d8_gl.c, at Present: the frame's drawing, in the picture's place in
the window, x and y its lower left corner in GL's window coordinates) */
void ui_overlay_present(int x, int y, int width, int height, int window_width, int window_height);

#endif
