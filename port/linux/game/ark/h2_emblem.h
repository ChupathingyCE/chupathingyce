/*
H2_EMBLEM.H

Halo 2's player emblems (h2_emblem.c): read from the player's own MCC files -
shared.map's ui\global_bitmaps\emblems\foreground and background (64 and 32
images, 256 a side), its globals' 18 profile colours, multiplayer globals'
team colours; mainmenu.map's names of them - into one file of this machine's
(t:\h2\emblems.h2emblem, made once; nothing of Bungie's ships), and made
textures as Halo 2's emblem passes colour them:

- the armour's: emblem_overlay_simple, the mc_emblem and elite_emblem shaders'
  template (shaders\shader_passes\texture_mapping\emblem_overlay_simple), the
  foreground alone over the armour, a tertiary and b quaternary colour;
- a whole emblem's, the HUD's and the menus': hud\waypoint_emblem, the
  background in the player's primary and secondary colours - its map scaled
  1.2 about the centre, so inset within a border of nothing (the shaders'
  own vertex constants) - and the foreground over it.

The pass's sums are done here on the CPU, once for an emblem and colours,
and the result is an ordinary alpha blended texture: no new shader program
(the Vita's shaders.pak is unchanged).

A player's emblem is the profile's (h2_emblem_protocol.h says what it is,
player_profile.c's extension keeps it, after the stock checksum), or what its
machine told the host (emblem_net.c); a bot's is made from its name.
Halo 2 draws emblems only in multiplayer (function_bc970: emblem_active is
0 on a campaign scenario): the campaign's armour has no emblem pass.
*/

#ifndef __H2_EMBLEM_H
#define __H2_EMBLEM_H

#include "cseries.h"
#include "h2_emblem_protocol.h"

struct bitmap_data;

enum
{
	/* a texture's kind */
	H2_EMBLEM_ARMOUR = 0,
	H2_EMBLEM_WHOLE,

	/* the names kept in the file (Halo 2's own, per language kept) */
	H2_EMBLEM_NAME_SIZE = 32,
	H2_EMBLEM_TEAMS = 8,

	/* what a picker edits, Halo 2's Appearance list's rows after the model
	(c_multiplayer_settings_edit_list::v20, src/unknown_2c4e9c.cpp) */
	H2_EMBLEM_ROW_PRIMARY = 0,
	H2_EMBLEM_ROW_SECONDARY,
	H2_EMBLEM_ROW_EMBLEM_PRIMARY,
	H2_EMBLEM_ROW_EMBLEM_SECONDARY,
	H2_EMBLEM_ROW_FOREGROUND,
	H2_EMBLEM_ROW_BACKGROUND,
	/* (X on Choose Emblem: Halo 2 has no row of it) */
	H2_EMBLEM_ROW_TOGGLE,
	/* (Halo 2's first row; the settings panel's "Halo 2 species") */
	H2_EMBLEM_ROW_MODEL,
	/* (the check mark, "Use model for all games": index 0 on, 1 off) */
	H2_EMBLEM_ROW_MODEL_FOR_ALL,
	H2_EMBLEM_ROWS,
};

/* the stand-in bitmap of the armour's added shader (h2_biped.c): nothing
seen, unless the object drawn has an emblem */
#define H2_EMBLEM_STAND_IN "h2\\characters\\bitmaps\\emblem"

/* the file made, if this machine has MCC's Halo 2 and it is not made yet:
FALSE if there is none to be had (no Halo 2: no emblem rows anywhere) */
boolean h2_emblem_available(void);
/* the file is there (made by the first Halo 2 level or the first look at
the emblem's own screens; never made here) */
boolean h2_emblem_ready(void);
/* the same, maps_directory given (the level builder's) */
void h2_emblem_pack_make(char const *maps_directory);

/* names and colours, as the player's files have them ("" before the file is
made); a name in the language chosen where the file has it */
char const *h2_emblem_foreground_name(short index);
char const *h2_emblem_background_name(short index);
char const *h2_emblem_color_name(short index);
/* 0xRRGGBB: a colour's, a team's (the multiplayer globals') */
unsigned long h2_emblem_color_rgb(short index);
unsigned long h2_emblem_team_rgb(short team);
/* the colour of the eighteen nearest 0xRRGGBB: one armour colour for every
game, Halo CE's list's, worn in Halo 2 as the nearest of Halo 2's */
short h2_emblem_nearest_color(unsigned long rgb);

/* a profile's emblem (Halo 2's new profile's, h2_emblem_default, if it has
none), saved */
void h2_emblem_profile_get(long profile_index, struct h2_emblem *emblem);
boolean h2_emblem_profile_save(long profile_index, struct h2_emblem const *emblem);
/* "Use model for all games", the check mark of Customization's ARMORY and the
settings panel. On, the default: one MODEL list, what is chosen is the
profile's model in every game. Off: Halo CE and Halo 2 each keep a model of
their own. A game only ever reads its own: Halo 2 the appearance's model,
Halo CE player_models.c's choice; this says how the choosers fill them. It is
the profile's alone (a bit beside the emblem's flags in the profile's
extension), not a part of struct h2_emblem, so no message can carry it */
boolean h2_emblem_models_separate(long profile_index);
boolean h2_emblem_profile_save_separate(long profile_index, struct h2_emblem const *emblem, boolean separate);
/* (player_models.c) the profile chose a Custom Edition model in the settings
panel: Halo 2 has none, so with the check mark on it wears the Spartan */
void h2_emblem_ce_model_chosen(long profile_index);
/* a profile's emblem as this machine shows it (the Halo 3 menus' nameplate) */
void h2_emblem_profile_shown(long profile_index, struct h2_emblem *emblem);
/* the emblem of a local player (its profile's), and of any player of the
game; FALSE: this game has none for it (the default given) */
void h2_emblem_local(short local_player_index, struct h2_emblem *emblem);
boolean h2_emblem_player(long player_index, struct h2_emblem *emblem);
/* the machine's and controller's, as a lobby's rows have them; FALSE: none */
boolean h2_emblem_network_player(struct network_player const *player, struct h2_emblem *emblem);

/* the emblem's texture, in the appearance's own four colours (Halo 2's menus
and HUD: function_7f790 with no team, function_1392f8, src/unknown_139296.cpp).
NULL while it is not made (at most a few are made a frame) or there is no
file. Good for this frame */
struct bitmap_data *h2_emblem_bitmap(struct h2_emblem const *emblem, short kind);
/* (the renderer) the armour's emblem of the object drawn; NULL none */
struct bitmap_data *h2_emblem_object_bitmap(long object_index);
/* (the renderer) a chicago shader's first map is the emblem stand-in */
boolean h2_emblem_stand_in(long bitmap_tag);

/* (menu_functions.c) a menu widget drawn as the emblem, whole, in place of
its bitmap, until set again (NULL: its bitmap) - the rows of a lobby and a
scoreboard, the Customization preview; name the widget's own, so that a widget
made later at the same address is not taken for it. (ui_widget.c) that
widget's texture; NULL its own */
void h2_emblem_widget_set(void const *widget, char const *name, struct h2_emblem const *emblem);
struct bitmap_data *h2_emblem_widget_bitmap(void const *widget, char const *name);

/* (main.c, each frame) each player's unit given its emblem, the local
players' told to the game (emblem_net.c) */
void h2_emblem_update(void);
/* (cache_files.c, a map let go) the textures go with its texture memory */
void h2_emblem_map_unloaded(void);

/* (the settings panel, port/vita/host/vita_settings.c, its thread: the first
local player's profile's emblem, taken at the game's frame) */
int h2_emblem_panel_offered(void);
int h2_emblem_panel_count(int row);
char const *h2_emblem_panel_name(int row, int index);
int h2_emblem_panel_chosen(int row);
void h2_emblem_panel_choose(int row, int index);

#endif
