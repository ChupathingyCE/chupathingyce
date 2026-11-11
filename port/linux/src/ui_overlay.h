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
	ui\small_ui, fonts.json: Cyborg Interface Gothic) and their titles'
	(as the menus' title pictures: Chupathingy Titles, or Cyborg Interface
	Gothic when display.title_font is "cyborg", drawn at the same capital
	height and baseline). A character they lack is drawn in Noto Sans Bold.
	UI_FONT_REGULAR and UI_FONT_BOLD are Noto Sans itself: no screen draws
	its text in them, which would show the player another face */
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
face buttons by Kenney's Input Prompts, a Steam Deck's, a Steam
Controller's and a Wii U pad's every button by Kenney's, the others' as
dark pills, and the keyboard's as light key caps */
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
/* what is gathered drawn into a framebuffer, the game's picture, in place of
the window */
void ui_overlay_present_to(unsigned int framebuffer, int width, int height);
int ui_overlay_pending(void);
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

/* the Xbox menus' animated plasma (ui_widget.c's draw_bitmap_in_rect: the
interface's plasma noise map, twice, scrolling at two scales) filling a
rounded rectangle, tinted by color, its alpha the surface's: the Guide's
panels and rows. Drawn from the player's ui.map's map when it was given
(ui_overlay_plasma_source), else from a noise of the overlay's own. time:
the milliseconds clock it scrolls by (system_milliseconds) */
void ui_overlay_plasma(float x, float y, float width, float height, float radius, unsigned int color,
	unsigned int time);
/* the plasma's noise map: RGBA (rows from the top), width by height,
copied; serial names it (the same serial is not copied again) */
void ui_overlay_plasma_source(int serial, const unsigned char *pixels, int width, int height);
/* the plasma's noise map read from the game's texture of it (a bitmap's
hardware format, the texture cache's: the interface's plasma map), once:
nonzero if it was read */
int ui_overlay_plasma_from_texture(const void *hardware_format);
/* the color of the plasma's bright threads (0xRRGGBBAA, its alpha unused):
the theme's (ui_style_theme) */
void ui_overlay_plasma_thread(unsigned int color);

/* what is drawn from here on goes over what was drawn before at a lower
layer (0 to 4; each frame starts at 0): text is drawn over the shapes of
its layer, and under those of the layers above, as a dialog over a list.
The Guide alone draws on 4, over every other screen's dialogs */
void ui_overlay_layer(int layer);

/* a button's glyph, size high, at x (its left), y (its top); its width.
color's alpha dims it (a prompt that does nothing now) */
float ui_overlay_button(int button, float size, float x, float y, unsigned int color);
float ui_overlay_button_width(int button, float size);

/* whether the keyboard (or the mouse) was used last: a screen whose keys
are not the game's (the Guide's Q, E and M) draws its own key caps, as the
keyboard's buttons are drawn, with ui_overlay_key (its width) */
int ui_overlay_keyboard(void);
float ui_overlay_key(const char *label, float size, float x, float y, unsigned int color);
float ui_overlay_key_width(const char *label, float size);

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

/* a medal's icon (medal_icons.h's index: the site's medals, as
stats.chupathingy.org draws them), size square, x, y its top left, tinted by
color (its alpha fades it). Nothing in a build without them
(HALO_MEDAL_ICONS, halo_console_features.h); ui_overlay_medals_available
says whether they are drawn */
void ui_overlay_medal(int medal, float x, float y, float size, unsigned int color);
int ui_overlay_medals_available(void);

/* a place the overlay leaves for the game's own drawing (a map's picture),
for this frame (at most 4) */
void ui_overlay_cutout(float x, float y, float width, float height);

/* whether the overlay draws (no window: the dedicated server, a run with
the null renderer) */
int ui_overlay_available(void);

/* ---------- the Guide's look (ui_overlay_style.c), shared by the screens
drawn in it: the Guide (port/linux/game/guide.c) and the setup launcher
(launcher.c). The Chupathingy menus' blues over the Xbox menus' plasma:
translucent panels and rows, a glow on the selected row and the edges. In
the menus' 640x480, as everything here */

/* the palette's named colors, for what a screen draws itself (the theme
swaps this one palette): ui_style_color's names (the setup screen's
names where they share one) */
enum
{
	UI_COLOR_NAME,
	UI_COLOR_STATUS,
	UI_COLOR_LABEL,
	UI_COLOR_HEAD,
	UI_COLOR_HEAD_GLOW,
	UI_COLOR_TEXT,
	UI_COLOR_WARNING,
	/* the shade over the screen under the Guide */
	UI_COLOR_SHADE,
	/* glass: a panel's, from its top to its bottom, and a pane's (a detail
	box, a toast) */
	UI_COLOR_GLASS_TOP,
	UI_COLOR_GLASS_BOTTOM,
	UI_COLOR_PANE,
	/* a friend's emblem on a toast, and its letter */
	UI_COLOR_EMBLEM_FRIEND,
	UI_COLOR_EMBLEM_LETTER,
	/* the setup screen's (launcher.c): its title, its sections (lit: their
	glow), a row's label not ready, a lit label's glow, a lit section's glow,
	quieter and fainter text, its states (good, bad), a row's tag, the
	prompts, the full-screen rule and its glow, a separator, a field (a path,
	a progress bar's track) and its edge, a progress bar's fill from its top
	to its bottom, the backdrop's space from its top to its middle and
	bottom, its stars (their alpha their own), and what is cleared behind */
	UI_COLOR_TITLE,
	UI_COLOR_TITLE_GLOW,
	UI_COLOR_NAV,
	UI_COLOR_NAV_GLOW,
	UI_COLOR_LABEL_DIM,
	UI_COLOR_LABEL_GLOW,
	UI_COLOR_SELECTED_GLOW,
	UI_COLOR_TEXT_DIM,
	UI_COLOR_FAINT,
	UI_COLOR_GOOD,
	UI_COLOR_BAD,
	UI_COLOR_TAG,
	UI_COLOR_PROMPT,
	UI_COLOR_PROMPT_GLOW,
	UI_COLOR_LINE,
	UI_COLOR_LINE_GLOW,
	UI_COLOR_SEPARATOR,
	UI_COLOR_FIELD,
	UI_COLOR_FIELD_EDGE,
	UI_COLOR_PROGRESS_TOP,
	UI_COLOR_PROGRESS_BOTTOM,
	UI_COLOR_BACKDROP_TOP,
	UI_COLOR_BACKDROP_MIDDLE,
	UI_COLOR_BACKDROP_BOTTOM,
	UI_COLOR_STAR,
	UI_COLOR_CLEAR,
	/* the look's own pieces (ui_overlay_style.c): a panel's edge and the
	glow around what is lit, the card's rule, a tab, the lit tab, the
	selection's edge, a row's glass, its edge and the lit row's plasma, and
	the plasma's bright threads */
	UI_COLOR_PANEL_EDGE,
	UI_COLOR_GLOW,
	UI_COLOR_RULE,
	UI_COLOR_TAB,
	UI_COLOR_TAB_ON,
	UI_COLOR_SELECTED_EDGE,
	UI_COLOR_ROW,
	UI_COLOR_ROW_EDGE,
	UI_COLOR_ROW_ON,
	UI_COLOR_PLASMA_THREAD,
	/* the Guide's (guide.c): a quieter label, a setting's name, and the
	dots of a friend in a game and offline */
	UI_COLOR_LABEL_SOFT,
	UI_COLOR_DOT_GAME,
	UI_COLOR_DOT_OFFLINE,
	NUMBER_OF_UI_COLORS
};
unsigned int ui_style_color(int name);

/* the themes the palette comes in: Halo Blue (the default) and
Chupathingy Red (display.theme) */
enum
{
	UI_THEME_BLUE,
	UI_THEME_RED,
	NUMBER_OF_UI_THEMES
};
/* the palette from here on (live: the next frame draws in it) */
void ui_style_theme(int theme);
int ui_style_theme_current(void);
/* display.theme's value ("blue", "red") as a theme: blue for anything else */
int ui_style_theme_named(const char *name);

/* the look's text, each kind in the menus' fonts (ui_style_text): a list's
headings ("ONLINE (2 OF 3)"), a row's name and its status line, a pane's
name, its lines, a quieter line, and a warning */
enum
{
	UI_TEXT_HEADING,
	UI_TEXT_NAME,
	UI_TEXT_STATUS,
	UI_TEXT_TITLE,
	UI_TEXT_LINE,
	UI_TEXT_LABEL,
	UI_TEXT_WARNING,
};
/* text of a kind (UTF-8), y its top; its width. Its height:
ui_style_text_size */
float ui_style_text(int kind, float x, float y, int align, const char *text);
/* the same faded by share (0 to 1: a toast coming and going) */
float ui_style_text_faded(int kind, float share, float x, float y, int align, const char *text);
float ui_style_text_size(int kind);
/* the same at scale times its size (a larger screen's rows: the setup
screen's) */
float ui_style_text_scaled(int kind, float scale, float x, float y, int align, const char *text);

/* the clock the plasma scrolls by, in milliseconds: set once a frame
before drawing (the game's system_milliseconds, the launcher's ticks) */
void ui_style_clock(unsigned int milliseconds);
/* a plasma surface of a color, its edge (alpha 0: none) and, lit, a glow
around it */
void ui_style_surface(float x, float y, float width, float height, float radius, unsigned int color,
	unsigned int edge, int lit);
/* the large glass panel a screen sits on (no plasma: the selection has it),
faded by share (0 to 1) */
void ui_style_panel(float x, float y, float width, float height, float share);
/* calm glass (a pane: a detail box, a toast), its edge (alpha 0: none) */
void ui_style_glass(float x, float y, float width, float height, float radius, unsigned int color,
	unsigned int edge);
/* a row's capsule: glass, the selected one lit with the plasma */
void ui_style_row(float x, float y, float width, float height, int selected);
/* a setting's row (settings_rows.h's, the Guide's and the setup screen's):
its capsule (ui_style_row), its label in ui_style_text's UI_TEXT_NAME at
scale times its size, a tag after it (badge, as "NEEDS A RESTART"; NULL:
none), and its value at the right between the arrows that change it */
void ui_style_setting(float x, float y, float width, float height, float scale, const char *label, const char *badge,
	const char *value, int selected);
/* section tabs between left and right, height tall: the bumpers' glyphs at
the ends (the keyboard's: left_key and right_key, as "Q" and "E", or the
device's LB and RB when NULL), the names centered, the selected one lit.
Each tab's left and right in starts and ends (NULL: not wanted), for the
pointer */
void ui_style_tabs(float left, float right, float y, float height, const char *const *names, int count,
	int selected, const char *left_key, const char *right_key, float *starts, float *ends);
/* the prompt bar's prompts, right-aligned at right: each a button's glyph
(the keyboard's: key, as "ESC", or the game's own key when NULL), then its
word; their places in starts and ends (NULL: not wanted) */
struct ui_style_prompt
{
	int button;
	const char *key;
	const char *word;
};
void ui_style_prompts(float right, float y, float size, const struct ui_style_prompt *prompts, int count,
	float *starts, float *ends);
/* the player's card across the top: their emblem (color, their name's
first letter), name and status line, the screen's title on the right
(NULL: none), and the rule under it all, 42 below y. With no name, the
title and the rule alone */
void ui_style_card(float left, float right, float y, const char *name, const char *status, unsigned int color,
	const char *title);

/* (d3d8_gl.c, at Present: the frame's drawing, in the picture's place in
the window, x and y its lower left corner in GL's window coordinates) */
void ui_overlay_present(int x, int y, int width, int height, int window_width, int window_height);

#endif
