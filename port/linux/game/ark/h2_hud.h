/*
H2_HUD.H

Halo 2's HUD on a Halo 2 level (h2_hud.c draws it; h2_hud_make.c makes its
file and bitmaps with the level): the HUD file (t:\h2\<map>.h2hud,
little-endian, the structures below one after another) - its header, then
each HUD (the unit's first, its owner "", then the weapons' and vehicles',
their owners their tags' names) and its widgets, as Halo 2's nhdt lays them
out (h2_hud_make.c says how).
*/

#ifndef __H2_HUD_H
#define __H2_HUD_H

/* ---------- constants */

enum
{
	H2_HUD_FILE_SIGNATURE = 0x64683268,   /* 'h2hd' */
	H2_HUD_FILE_VERSION = 10,
	/* (the postgame strings carried with the file, h2_hud_file_word) */
	H2_HUD_MAXIMUM_WORDS = 96,
	/* (the most marks - a magazine's rounds - a meter's image has, h2_hud_file_cell) */
	H2_HUD_MAXIMUM_CELLS = 2048,
	/* (the waypoints' images: on screen, off screen, occluded) */
	H2_HUD_WAYPOINT_IMAGES = 3,
	H2_HUD_MAXIMUM_WAYPOINTS = 16,

	/* (an ally's emblem over its head: emblem_dummy, 64 units, by the scale
	its scoreboard widgets give it) */
	H2_HUD_ALLY_EMBLEM_SIZE = 32,

	H2_HUD_WIDGET_BITMAP = 0,
	H2_HUD_WIDGET_TEXT,
	/* (a player's emblem on the scoreboard, h2_emblem.h: its special 1 this
	player's, 2 the best other's; justification 1 a team's plain square) */
	H2_HUD_WIDGET_EMBLEM,

	H2_HUD_SHADER_NORMAL = 0,
	H2_HUD_SHADER_DIM,
	H2_HUD_SHADER_RED,
	H2_HUD_SHADER_GREEN,
	H2_HUD_SHADER_METER,
	H2_HUD_SHADER_SHIELD,
	H2_HUD_SHADER_SCOPE,
	H2_HUD_SHADER_LIGHT,
	/* (made, not read: the foreground pass of a panel's image) */
	H2_HUD_SHADER_FOREGROUND,

	/* a text's input beyond hud_input1..4: the scoreboard's */
	H2_HUD_TEXT_PLAYER_SCORE = 5,
	H2_HUD_TEXT_OTHER_SCORE,
	H2_HUD_TEXT_VARIANT_NAME,
	H2_HUD_TEXT_TIME_LEFT,

	/* a Halo 2 weapon's prompts, the weap tag's ten messages in their
	order (h2_hud_weapon_message) */
	H2_HUD_PROMPT_PICKUP = 0,
	H2_HUD_PROMPT_SWAP,
	H2_HUD_PROMPT_PICKUP_OR_DUAL,
	H2_HUD_PROMPT_SWAP_OR_DUAL,
	H2_HUD_PROMPT_DUAL,
	H2_HUD_PROMPT_PICKED_UP,
	H2_HUD_PROMPT_AMMO_SINGULAR,
	H2_HUD_PROMPT_AMMO_PLURAL,
	H2_HUD_PROMPT_SWITCH_TO,
	H2_HUD_PROMPT_SWAP_AI,

	/* Halo 2's anchors */
	H2_HUD_ANCHOR_TOP_RIGHT = 0,
	H2_HUD_ANCHOR_TOP_LEFT,
	H2_HUD_ANCHOR_BOTTOM_LEFT,
	H2_HUD_ANCHOR_BOTTOM_RIGHT,
	H2_HUD_ANCHOR_CENTER,
};

/* ---------- structures */

/* (the Arbiter's unit HUD's owner: drawn in place of the unit's, "", while
the player is an Elite) */
#define H2_HUD_OWNER_ELITE "unit:elite"

struct h2_hud_file_header
{
	unsigned long signature;
	unsigned long version;
	long hud_count;
	/* (the HUD globals' motion sensor: its range, world units, and the
	slowest speed a unit moves at to show on it - a tick's; 0 none read) */
	float motion_sensor_range;
	float motion_sensor_minimum_speed;
	/* (the HUD globals' waypoint arrows - the nav points' looks, after the
	HUDs: h2_hud_file_waypoint each) */
	long waypoint_count;
	/* (the meters' marks, after the waypoints: h2_hud_file_cell each) */
	long cell_count;
	/* (the postgame carnage report's strings, after the cells:
	h2_hud_file_word each - Halo 2's postgame string list, read from the
	player's own main menu map; h2_medals.c, h2_menu.c, carnage_report.c) */
	long word_count;
};

/* a string of Halo 2's postgame string list
(ui\screens\game_shell\postgame_statistics\werds): its string id's name
and its English words, UTF-8 */
struct h2_hud_file_word
{
	char id[32];
	char text[64];
};

/* a mark of a meter's image (a round of a magazine's ammunition meter): its
rectangle in the image's texels (x1, y1 its last), and its place in the
order the meter empties in - Halo 2's meter gradient, the image's blue
channel: lit while the meter's input is at least it (0..255 of 1) */
struct h2_hud_file_cell
{
	unsigned char x0, y0, x1, y1;
	unsigned char threshold;
	unsigned char unused[3];
};

/* a nav point's look (Halo 2's HUD globals' waypoint arrow, its name the
scripts' navpoint's): its colour and opacity, its image on screen, off it
(an arrow at the edge, turned to it) and hidden behind something; "" none */
struct h2_hud_file_waypoint
{
	char name[32];
	unsigned long color;
	float opacity;
	unsigned long flags;
	char bitmap[H2_HUD_WAYPOINT_IMAGES][64];
	unsigned short width[H2_HUD_WAYPOINT_IMAGES], height[H2_HUD_WAYPOINT_IMAGES];
	unsigned short canvas_width[H2_HUD_WAYPOINT_IMAGES], canvas_height[H2_HUD_WAYPOINT_IMAGES];
	unsigned char shift[H2_HUD_WAYPOINT_IMAGES], unused;
};

struct h2_hud_file_hud
{
	char owner[64];
	short low_clip;
	short low_total;
	short widget_count;
	short unused;
};

struct h2_hud_file_widget
{
	char name[32];
	unsigned char type;
	unsigned char shader;
	unsigned char anchor;
	unsigned char flags;
	unsigned char inputs[4];
	/* Halo 2's (0x8..0x17): draw if unit, extra, weapon, game engine (2
	bytes each); don't draw if the same */
	unsigned char draw_flags[16];
	short cutoffs[3];           /* age, clip, total */
	short offset[2];
	float registration[2];
	long special;
	/* (a bitmap's) its image's tag's name and size, its canvas's */
	char bitmap[64];
	unsigned short width, height, canvas_width, canvas_height;
	/* (a text's) which input it shows (1..4; H2_HUD_TEXT_...), its
	justification */
	unsigned char text_input;
	unsigned char justification;
	/* (a bitmap's) unused[0]: its image halved that many times (drawn as
	many times twice as big) */
	unsigned char unused[2];
	/* (a bitmap's) its foreground image's tag's name (outline, icons: the
	panel's second pass; "" none) */
	char bitmap_fg[64];
	/* (the Elite's own image of a sequence with two - its panels' corners
	rounded -, the image after the first: its tag's name, foreground's, size,
	canvas and halvings; "" none) */
	char bitmap_alt[64], bitmap_alt_fg[64];
	unsigned short alt_width, alt_height, alt_canvas_width, alt_canvas_height;
	unsigned char alt_shift;
	/* (a meter of marks - rounds -: its marks, the file's cells from
	cell_first; 0 a meter filled from the left) */
	unsigned char cell_count;
	short cell_first;
};

/* (damage.c's damage categories - Halo 2's jpt! category enum is this
engine's, h2_weapons.c; h2_hud.c's kill lines and h2_medals.c's medals
read them off a dead unit's last damage category) */
enum
{
	H2_HUD_DAMAGE_FALLING = 1,
	H2_HUD_DAMAGE_SNIPER = 5,
	H2_HUD_DAMAGE_MELEE = 6,
	H2_HUD_DAMAGE_VEHICLE = 9,
};

/* ---------- prototypes/H2_HUD.C */

/* (hud.c, hud_messaging.c) one of a Halo 2 weapon's prompts from the
player's own map (H2_HUD_PROMPT_...), its glyphs kept: U+E47C/U+E47D the
hold verb, U+E449/U+E46A the switch weapon button, U+E448, U+E45A, U+E45B,
U+E461, U+E467 the action button, U+E1xx the weapon's picture, U+E422 a
count, "\r\n" a new line; NULL none */
wchar_t const *h2_hud_weapon_message(long definition_index, int which);
/* (hud_messaging.c) a weapon object's definition (NONE: no such weapon) */
long h2_hud_weapon_object_definition(long weapon_index);

/* (hud.c) the local player's Halo 2 HUD drawn if the level is a Halo 2
one with its HUD file: TRUE then (this engine's unit and weapon HUD are
not drawn) */
int h2_hud_draw(short local_player_index, long unit_index);

/* (h2_campaign.c, a script's navpoint) the waypoint arrow of the level's
HUD file named name; NONE if none */
short h2_hud_waypoint_index(char const *name);
/* the number of the level's waypoint arrows (0: no Halo 2 HUD) */
short h2_hud_waypoint_count(void);

/* (hud_nav_points.c) a nav point drawn as Halo 2 draws it, the waypoint
arrow nav_index of the level's HUD file: centred at the screen point (the
viewport's pixels), turned theta (off screen), its image the screen type's
(0 on screen, 1 off screen, 2 occluded) and its size by the distance (world
units). FALSE when the level has no Halo 2 HUD (this engine's is drawn) */
int h2_hud_nav_point_draw(short local_player_index, short nav_index, short screen_type, real x, real y, real theta,
	real distance);
/* (game_engine_render_nav_points) the colour (0xRRGGBB) the next nav
points are drawn in, NONE their arrow's own (a Territories territory's: its
holder's) */
void h2_hud_nav_point_tint(long rgb);

/* the white texel's bitmap of the level's HUD (made at load, or the
level's own): NULL none */
struct bitmap_data *h2_hud_white_bitmap(void);

/* (h2_hud_make.c, HALO_H2_HUD_AT_LOAD) a loaded level's HUD made from its
MCC map, in memory: the widgets (the HUD file's bytes, the structures above,
no file) and its images (DXT5 sheets), each handed to `texture` as it is
made (its pixels let go when it returns: TRUE, made a texture under path):
see there */
typedef boolean (*h2_hud_texture_proc)(void *context, char const *path, unsigned short width, unsigned short height,
	unsigned char const *pixels, unsigned long size);
void h2_hud_loaded_free(unsigned char *widgets);
/* (cache_files.c) the level's HUD textures let go as its map unloads */
void h2_hud_map_unloaded(void);
boolean h2_hud_make_loaded(char const *map_name, unsigned char **widgets, unsigned long *widgets_size,
	h2_hud_texture_proc texture, void *context, long *image_count);

#endif
