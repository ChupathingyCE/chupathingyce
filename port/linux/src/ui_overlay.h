/*
UI_OVERLAY.H

The overlay (configure.py --game-browser, HALO_GAME_BROWSER): the screens
this port draws over the game's own (the game list's Online Games, its
filters, the profile), at the window's resolution, after the game's
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
};

enum
{
	UI_ALIGN_LEFT,
	UI_ALIGN_CENTER,
	UI_ALIGN_RIGHT,
};

/* a controller's button, drawn as the device the player last used names
it (Kenney's Input Prompts) */
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

/* a button's glyph, size high, at x (its left), y (its top); its width */
float ui_overlay_button(int button, float size, float x, float y, unsigned int color);
float ui_overlay_button_width(int button, float size);

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
