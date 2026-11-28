/*
H2_MENU.H

Halo 2's own main menu on this engine (dev switch HALO_H2_MENU=1): the
player's MCC mainmenu.map made a level (h2_level.c) whose scripts fly the
camera through its background scene, and the main menu screen's widgets -
read from the same map's user interface tags when the level is made
(h2_menu_make.c) - drawn over it by a small renderer of their own
(h2_menu.c), not this engine's menus. Nothing of Halo 2's is shipped:
everything is read from the player's files.

The menu file (t:\h2\<map>.h2menu, little-endian, one struct h2_menu_file)
keeps the screens as Halo 2's tags have them - the main menu, settings,
the campaign's difficulty and its level select: their bitmap and text
widgets, their lists and the lists' skins, their positions in Halo 2's
units (the screen's centre the origin), the items' words and descriptions,
and the fonts' glyphs, each bitmap and font page made a bitmap tag of the
level ("h2\menu\...").
*/

#ifndef __H2_MENU_H
#define __H2_MENU_H

#include "cseries.h"

/* ---------- constants */

enum
{
	H2_MENU_FILE_SIGNATURE = 0x6E6D3268,   /* 'h2mn' */
	/* the menu part's version (h2_level.h): 19 the pause menu with all of
	Halo 2's fonts (was the builder's 275); a stale menu file is made again
	alone when its level loads */
	H2_MENU_FILE_VERSION = 19,
	H2_MENU_MAXIMUM_BITMAPS = 512,
	H2_MENU_MAXIMUM_TEXTS = 96,
	H2_MENU_MAXIMUM_ITEMS = 32,
	H2_MENU_MAXIMUM_FONTS = 8,
	H2_MENU_MAXIMUM_ANIMATIONS = 8,
	/* the shared UI globals' element animations (wigl 0x120) and their keyframes */
	H2_MENU_MAXIMUM_MOTIONS = 64,
	/* a pane's buttons (the pregame lobby's START GAME, GAME SETUP ...) */
	H2_MENU_MAXIMUM_TABS = 6,
	H2_MENU_MOTION_KEYFRAMES = 6,
	H2_MENU_MAXIMUM_SKINS = 8,
	/* (the glyphs kept: characters 32..127, then the button icons U+E100..
	U+E103 - A, B, X, Y - as the bytes 0x80..0x83) */
	H2_MENU_FIRST_GLYPH = 32,
	H2_MENU_GLYPHS = 100,
	H2_MENU_FIRST_ICON = 0x80,
	/* (Halo 2's medals: the fonts' characters U+E065..U+E091 - the postgame
	string list's medal strings are these -, in the report's font alone) */
	H2_MENU_FIRST_MEDAL_CHARACTER = 0xE065,
	H2_MENU_MEDAL_GLYPHS = 45,
	H2_MENU_WORDS = 64,
	H2_MENU_DESCRIPTION = 256,
	/* the virtual keyboard's keys (the Xbox's: 49) */
	H2_MENU_MAXIMUM_KEYS = 64,

	/* the screens */
	H2_MENU_SCREEN_MAIN = 0,
	H2_MENU_SCREEN_SETTINGS,
	H2_MENU_SCREEN_DIFFICULTY,
	H2_MENU_SCREEN_LEVELS,
	H2_MENU_SCREEN_PLAYER,
	H2_MENU_SCREEN_CONTROLLER,
	H2_MENU_SCREEN_LOOK_SENSITIVITY,
	H2_MENU_SCREEN_INVERT_LOOK,
	H2_MENU_SCREEN_VIDEO,
	H2_MENU_SCREEN_DISPLAY_MODE,
	H2_MENU_SCREEN_AUDIO,
	H2_MENU_SCREEN_VOLUME,
	H2_MENU_SCREEN_LIVE,
	H2_MENU_SCREEN_GAME_SELECT,
	H2_MENU_SCREEN_MAP_SELECT,
	/* (the pregame lobby: drawn over this engine's lobby, h2_menu_lobby_draw) */
	H2_MENU_SCREEN_LOBBY,
	/* (a level's own: Halo 2's pause - a multiplayer game's GAME MENU, the
	campaign's GAME PAUSED -, shared.map's screens, in its level's file) */
	H2_MENU_SCREEN_MP_PAUSE,
	H2_MENU_SCREEN_SP_PAUSE,
	/* (a dialog over the main menu, as Halo 2's: CAMPAIGN OPTIONS) */
	H2_MENU_SCREEN_CAMPAIGN_OPTIONS,
	/* (the profile's Subtitles: on or off - game.h2_subtitles) */
	H2_MENU_SCREEN_SUBTITLES,
	/* (the Xbox's virtual keyboard - Y on Xbox Live: a host's code typed, the
	game joined by it - and its error dialog, a wrong code's) */
	H2_MENU_SCREEN_KEYBOARD,
	H2_MENU_SCREEN_ERROR,
	/* (OptiMatch: Halo 2 Vista's live game browser - internet play's public
	Halo 2 games, this engine's listings, its items made as they come) */
	H2_MENU_SCREEN_BROWSER,
	/* (the attract screen before the main menu: Halo 2 Vista's "PRESS ANY
	KEY TO CONTINUE", start_screen) */
	H2_MENU_SCREEN_START,
	/* (the settings' choice lists this menu reached in round 3 of the
	120menu lane: the controller's vibration, thumbstick and button
	layouts and its restore defaults, the video options' brightness, HUD
	size and restore defaults, the audio options' restore defaults, and
	the ABOUT dialog) */
	H2_MENU_SCREEN_VIBRATION,
	H2_MENU_SCREEN_THUMBSTICKS,
	H2_MENU_SCREEN_BUTTONS,
	H2_MENU_SCREEN_RESTORE_CONTROLLER,
	H2_MENU_SCREEN_BRIGHTNESS,
	H2_MENU_SCREEN_HUD_SIZE,
	H2_MENU_SCREEN_RESTORE_VIDEO,
	H2_MENU_SCREEN_RESTORE_AUDIO,
	H2_MENU_SCREEN_ABOUT,
	H2_MENU_SCREENS,
	/* (the owner of the game shell's background - its frame and its
	scrolling bands -, drawn under every screen but the main menu) */
	H2_MENU_OWNER_BACKGROUND = H2_MENU_SCREENS,

	/* a widget's owner: a screen (its index), or a skin's (this bit and
	the skin's index) */
	H2_MENU_OWNER_SKIN = 0x80,
	/* (drawn by what names it - a skin's arrows -, never by an owner's loop) */
	H2_MENU_OWNER_NONE = 0xFF,

	/* Halo 2's blend methods */
	H2_MENU_BLEND_STANDARD = 0,
	H2_MENU_BLEND_MULTIPLY,
	H2_MENU_BLEND_UNUSED,
};

/* ---------- structures */

struct h2_menu_file_bitmap
{
	char tag[64];               /* this engine's bitmap tag ("" none) */
	char tag_other[64];         /* (an item's) its image for an item not focused ("" none) */
	unsigned char owner;
	unsigned char blend;
	short frame;
	unsigned long flags;        /* Halo 2's */
	short x, y;                 /* Halo 2's "top-left" */
	short bias;                 /* render depth bias */
	short animation;            /* Halo 2's animation index (the file's motions; NONE none) */
	short unused2;
	short unused;               /* (an item's picture - a multiplayer map's -: its item's index and one; 0 none) */
	float scale[2];             /* render scale (0: 1) */
	float wraps[2];             /* texture wraps a second */
	unsigned short width, height;               /* the image's */
	unsigned short canvas_width, canvas_height; /* the bitmap's */
};

struct h2_menu_file_text
{
	unsigned char owner;
	unsigned char font;         /* the menu file's font (0xFF none) */
	short custom_font;          /* Halo 2's font enum */
	unsigned long flags;        /* Halo 2's text flags (bit 0 left justified...) */
	short bounds[4];            /* Halo 2's rectangle: top, left, bottom, right */
	float color[4];             /* alpha, red, green, blue */
	short bias;
	short description;          /* (a screen's) TRUE: it shows the focused item's description */
	short animation;            /* Halo 2's animation index (the file's motions; NONE none) */
	short unused;
	char words[H2_MENU_DESCRIPTION];   /* its string ("" none) */
};

struct h2_menu_file_glyph
{
	unsigned short x, y, width, height;   /* in the font's page */
	short advance;
	short origin_x, origin_y;
	short unused;
};

struct h2_menu_file_font
{
	char tag[64];               /* its page's bitmap tag */
	char name[32];              /* its file's name (h2_fonts) */
	short custom_font;
	short ascent, descent, leading;
	unsigned short page_width, page_height;
	struct h2_menu_file_glyph glyphs[H2_MENU_GLYPHS];
	struct h2_menu_file_glyph medals[H2_MENU_MEDAL_GLYPHS];
};

struct h2_menu_file_animation
{
	long period;                /* milliseconds */
	float alpha[2];             /* first and last keyframes' */
};

/* an element's animations as its screen opens and closes - Halo 2's
transitions in and out: each's keyframes evenly over its period, each an alpha and an offset (x, y
in the screen's units; z away from the viewer, drawn at the projection
plane's distance over its own) */
struct h2_menu_file_motion_keys
{
	long period;                /* milliseconds (0: none) */
	short count, unused;
	float alpha[H2_MENU_MOTION_KEYFRAMES];
	float x[H2_MENU_MOTION_KEYFRAMES], y[H2_MENU_MOTION_KEYFRAMES], z[H2_MENU_MOTION_KEYFRAMES];
};

struct h2_menu_file_motion
{
	struct h2_menu_file_motion_keys in, out;  /* (as its screen opens; as it closes) */
};

struct h2_menu_file_skin
{
	char name[128];
	short item_height;          /* (its words' bounds') */
	/* its scroll arrows: the file's bitmaps (up, down; NONE none - their
	owner H2_MENU_OWNER_NONE) and their offsets from the list's point */
	short arrows[2];
	short arrow_offsets[2][2];
	short animation_count;
	struct h2_menu_file_animation animations[H2_MENU_MAXIMUM_ANIMATIONS];
};

struct h2_menu_file_screen
{
	char name[128];
	char header[H2_MENU_WORDS];  /* its header's words ("" none) */
	float text_color[4];         /* (argb) */
	/* the list: Halo 2's flags, skin (the file's), visible items, its point */
	unsigned long list_flags;
	short list_skin, list_visible, list_x, list_y;
	short list_animation, list_delay;  /* (its animation index, its intro's delay in milliseconds) */
	long item_count;
	char items[H2_MENU_MAXIMUM_ITEMS][H2_MENU_WORDS];
	/* an item's id (its string id's name; a level's: its scenario's path; a
	multiplayer map's: its file's name) */
	char item_ids[H2_MENU_MAXIMUM_ITEMS][96];
	char descriptions[H2_MENU_MAXIMUM_ITEMS][H2_MENU_DESCRIPTION];
	char buttons[H2_MENU_WORDS];  /* its button key's words, the icons 0x80.. ("" none) */
	/* its first pane's buttons (Halo 2's button widgets, 0x3C each: their
	words in bounds 0x1C, their bitmap 0x24 at an offset 0x2C from the
	bounds' top left, their string id 0x30): the file's bitmaps of their
	images 0 and 1 (NONE none) */
	/* its player list (a pane's player block, 0x44: its skin 4, its point
	0xC, its rows' step 0x14 - the pregame lobby's party): the file's skin
	(NONE none) */
	short player_skin, player_x, player_y, player_step;
	short tab_count, tab_unused;
	struct h2_menu_file_tab
	{
		short bounds[4];        /* top, left, bottom, right */
		short offset[2];
		short bitmap[2];
		unsigned char font, unused[3];
		char words[H2_MENU_WORDS];
	} tabs[H2_MENU_MAXIMUM_TABS];
};

/* a key of the virtual keyboard: its button widget's bounds (Halo 2's
rectangle: top, left, bottom, right), its image (the file's bitmap: owner
H2_MENU_OWNER_NONE; NONE none - its focused frame the bitmap's tag_other),
its string id's name ("lowercase_a", "numeral_one", "backspace", "done")
and its words ("a", "1") */
struct h2_menu_file_key
{
	short bounds[4];
	short bitmap;
	short font;                 /* (the file's: its custom font's) */
	char id[32];
	char words[16];
};

struct h2_menu_file
{
	unsigned long signature;
	unsigned long version;
	long bitmap_count, text_count, font_count, skin_count;
	char music[128];            /* the menu's music (a looping sound's tag name, Halo 2's) */
	float grade[4];             /* the scene behind it desaturated to this tint (its screen effect
				       effects\screen_effects\desaturate's shader's colour): red, green,
				       blue, and 1 (0: none) */
	float projection;           /* the UI's projection plane's distance (wigl 0x64; 0 none) */
	long motion_count;
	struct h2_menu_file_motion motions[H2_MENU_MAXIMUM_MOTIONS];
	struct h2_menu_file_screen screens[H2_MENU_SCREENS];
	struct h2_menu_file_skin skins[H2_MENU_MAXIMUM_SKINS];
	struct h2_menu_file_bitmap bitmaps[H2_MENU_MAXIMUM_BITMAPS];
	struct h2_menu_file_text texts[H2_MENU_MAXIMUM_TEXTS];
	struct h2_menu_file_font fonts[H2_MENU_MAXIMUM_FONTS];
	long key_count;
	struct h2_menu_file_key keys[H2_MENU_MAXIMUM_KEYS];
};

/* ---------- prototypes/H2_MENU_MAKE.C (HALO_H2_MENU_AT_LOAD) */

/* a loaded level's menu made from its MCC map, in memory - the menu as its
file would hold it, and its bitmaps (A8R8G8B8 on power-of-two canvases,
linear), each handed to `texture` as it is made (its pixels let go when it
returns: TRUE, made a texture under path): see there */
typedef boolean (*h2_menu_texture_proc)(void *context, char const *path, unsigned short width, unsigned short height,
	unsigned char const *pixels);
enum
{
	H2_MENU_MADE_TEXTURES,   /* (a level's pause menu: every bitmap made a texture) */
	H2_MENU_MADE_ART,        /* (the lobby: the art file's images alone) */
	H2_MENU_MADE_LAZY,       /* (the main menu scene: its fonts textures, its images made when first drawn) */
};
/* an image to be made when first drawn: its name, the MCC map it is of,
its bitmap tag there and image; halve: as the lobby's art file holds it */
typedef void (*h2_menu_lazy_proc)(void *context, char const *path, char const *map_name, long bitmap_tag, short image,
	boolean halve);
boolean h2_menu_make_loaded(char const *map_name, short what, struct h2_menu_file *menu, h2_menu_texture_proc texture,
	h2_menu_lazy_proc lazy, void *context, long *image_count);
/* (an image made when first drawn) its map opened, on the game thread, and
closed; the image made and handed to texture */
struct h2_cache;
struct h2_cache *h2_menu_image_cache_open(char const *map_name);
void h2_menu_image_cache_close(struct h2_cache *cache);
boolean h2_menu_image_make(struct h2_cache *cache, long bitmap_tag, short image, boolean halve, char const *path,
	h2_menu_texture_proc texture, void *context);

/* ---------- prototypes/H2_MENU.C */

/* whether the loaded level is Halo 2's main menu scene */
boolean h2_menu_scene(void);

/* whether the dev switch asks for it (HALO_H2_MENU=1), or the game hub did */
boolean h2_menu_wanted(void);

/* whether the dev switch alone does (HALO_H2_MENU=1: no hub, as before) */
boolean h2_menu_dev_switch(void);

/* (Project Ark's game hub, menu_functions.c) whether the player's files
hold Halo 2's main menu (the maps folder's mainmenu.map and shared.map) */
boolean h2_menu_available(void);

/* (the hub) HALO 2 picked: this menu is asked for from this engine's main
menu at once (h2_menu_main_menu_update); its B then wants the hub again
(h2_menu_hub_return_take: TRUE once). HALO CE picked: let go */
void h2_menu_hub_request(void);
void h2_menu_hub_release(void);
boolean h2_menu_hub_return_take(void);
/* (1.2.0 menu round 3) the screen the main menu's MEDIA or EXTRAS
dialog left Halo 2's menu for, once ("forge", "command", "firefight",
"fileshare", "friends", "mods"; NULL none): menu_functions.c opens it over
this engine's main menu, and Halo 2's menu again once it is closed */
char const *h2_menu_target_take(void);
/* (the settings panel's Open the game hub, menu_functions.c) this menu
left for the hub, as its main menu's B leaves it: this engine's main menu
with the hub over it */
void h2_menu_hub_leave(void);

/* (hud.c) the menu drawn over the scene in place of the HUD when the level
is Halo 2's main menu: TRUE then */
int h2_menu_draw(void);

/* (ui_widget.c, Start pressed in a game) Halo 2's own pause opened over a
Halo 2 level, when its file has one: TRUE then (HALO_H2_PAUSE=0: never -
this engine's pause screen) */
boolean h2_menu_pause_open(void);

/* (game_engine.c game_engine_post_rasterize_post_game) Halo 2's own
postgame carnage report drawn over a Halo 2 level's game end, when this
menu is the game's (HALO_H2_MENU or the hub's Halo 2; HALO_H2_PCR=1 alone,
0 never): TRUE then - the game's own report is not drawn, nor the Halo 3
one (menu_functions.c h3_carnage_wanted) */
int h2_menu_postgame_draw(void);
boolean h2_menu_postgame_wanted(void);
boolean h2_menu_postgame_up(void);

/* (player_control.c) whether Halo 2's pause has the local player's
controller */
boolean h2_menu_pause_has_controller(short local_player_index);

/* (main.c, each frame) the main menu asks for Halo 2's once, when the dev
switch is on */
void h2_menu_main_menu_update(void);
/* (cache_files.c) a Halo 2 level's own menu textures let go as its map unloads */
void h2_menu_map_unloaded(void);

/* (h2_script_text.c) a text drawn on a Halo 2 level's HUD in one of
Halo 2's own fonts (its index in the font table: 2 the titles' Handel
Gothic, 9 Conduit), from its level's menu file: its lines ('\n') ending
on `bottom` (pixels, down), between x0 and x1, justified (0 left, 1
right, 2 centred), `scale` pixels a font unit; FALSE when the level's
file has no such font */
boolean h2_menu_hud_text(short custom_font, char const *text, real x0, real x1, real bottom, short justification, real red,
	real green, real blue, real alpha, real scale);

#endif
