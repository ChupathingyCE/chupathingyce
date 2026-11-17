/*
HUD_BUTTONS.C

A game's prompts' buttons in HD (input.hud_buttons = "hd"; "classic", the
default, is retail's: the map's own pictures). The HUD's messages ("Hold X
to pick up the Shotgun") draw each button where the map's picture would
be, at its size, and the words around it keep their places: this draws
the button the prompts' style names (input.button_style: the controller
used last, or the Duke's, an Xbox pad's, a PlayStation's or a Switch's)
there instead, in DKrumpp's flat look (he drew the gems for menus), with
the overlay (ui_overlay_hud_button) on GL and bgfx alike.

Only the buttons that have one: A, B, X, Y, the black and white buttons
(RB and LB), the triggers, Start and Back. The d-pad and the sticks keep
the map's pictures. The builds without DKrumpp's art (HALO_HD_BUTTONS,
halo_console_features.h) keep them all.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"

#include "hud_buttons.h"

#ifdef HALO_HD_BUTTONS

#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "../src/ui_overlay.h"

#include <string.h>

char const *config_string(char const *name);

/* how much of the map's picture's box a button fills */
#define HUD_BUTTONS_FILL 0.86f

/* the button (UI_BUTTON_*) of a HUD message's button icon (the HUD globals'
button icons: A, B, X, Y, black, white, LT, RT, the d-pad's four, Start,
Back, the sticks pressed, the sticks), or -1 for none */
static int hud_buttons_ui_button(
	short icon_index)
{
	static int const buttons[] =
	{
		UI_BUTTON_A, UI_BUTTON_B, UI_BUTTON_X, UI_BUTTON_Y, UI_BUTTON_RIGHT_SHOULDER, UI_BUTTON_LEFT_SHOULDER,
		UI_BUTTON_LEFT_TRIGGER, UI_BUTTON_RIGHT_TRIGGER, -1, -1, -1, -1, UI_BUTTON_START, UI_BUTTON_BACK,
	};

	return icon_index >= 0 && icon_index < (short)NUMBEROF(buttons) ? buttons[icon_index] : -1;
}

boolean hud_buttons_draw(
	short icon_index,
	real x,
	real top,
	real width,
	real height,
	real alpha)
{
	struct rasterizer_globals_reserved04 const *screen = &rasterizer_globals.reserved04;
	real screen_width = (real)(screen->screen_bounds.x1 - screen->screen_bounds.x0);
	real screen_height = (real)(screen->screen_bounds.y1 - screen->screen_bounds.y0);
	char const *setting = config_string("input.hud_buttons");
	int button = hud_buttons_ui_button(icon_index);
	real scale, size, drawn, layout_x, layout_y;
	unsigned int shade;

	if (button < 0 || !setting || strcmp(setting, "hd") || screen_width <= 0.0f || screen_height <= 0.0f ||
		height <= 0.0f)
	{
		return FALSE;
	}
	/* (the HUD's coordinates are its view's; the overlay's, the picture's
	480 lines, its middle the layout's: medal_hud.c's) */
	scale = 480.0f / screen_height;
	/* (a little inside the map's picture's box, as its button is drawn in
	it, so the words either side keep their gap) */
	size = height * scale * HUD_BUTTONS_FILL;
	drawn = ui_overlay_hud_button_width(button, size);
	layout_x = 320.0f + ((real)render.camera.viewport_bounds.x0 + x - screen_width * 0.5f) * scale +
		(width * scale - drawn) * 0.5f;
	layout_y = ((real)render.camera.viewport_bounds.y0 + top) * scale + (height * scale - size) * 0.5f;
	shade = (unsigned int)(PIN(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
	ui_overlay_layer(0);
	ui_overlay_hud_button(button, size, layout_x, layout_y, 0xFFFFFF00U | shade);
	return TRUE;
}

#else

boolean hud_buttons_draw(
	short icon_index,
	real x,
	real top,
	real width,
	real height,
	real alpha)
{
	(void)icon_index;
	(void)x;
	(void)top;
	(void)width;
	(void)height;
	(void)alpha;
	return FALSE;
}

#endif
