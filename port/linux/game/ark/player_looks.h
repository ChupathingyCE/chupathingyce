/*
PLAYER_LOOKS.H

What a player wears in a multiplayer game (player_looks.c), whichever game's
level it is: one of five models - Halo 1, 2 and 3's Spartans, Halo 2 and 3's
Elites -, Halo 3's armour when it is a Halo 3 model, and a visor colour.

A look is drawn, never played: the player's biped - its animations, its
collision, its seat in a vehicle, its place on the wire - stays the level's own,
and render_objects.c draws another model on its skeleton, bone by bone by name.
Nothing a look does can reach the game's state, so two machines that show a
player differently are still in the same game. The one exception is Halo 2's
own Elite, a biped of its own in Halo 2's levels (emblem.model, as before).

The campaign keeps each game's own character: a look is worn in a multiplayer
scenario only.

The check mark "Use this model for all games": on, one look; off, a look for
each game, chosen with the Game row. All of them stay saved in
t:\player_looks.txt, a line a look of each profile (blam.sav has no room).
*/

#ifndef __PLAYER_LOOKS_H
#define __PLAYER_LOOKS_H

#include "cseries.h"
#include "math/real_math.h"
#include "player_look_protocol.h"

/* the packs that bring Halo 2's models to a Halo CE level or the menus: their
bipeds are only drawn, never the level's (h2_player_models_biped) */
#define PLAYER_LOOK_PACK_H2_SPARTAN "h2_spartan_look"
#define PLAYER_LOOK_PACK_H2_ELITE "h2_elite_look"

enum
{
	/* the games a look is kept for; the first, every game (the check mark) */
	PLAYER_LOOK_SLOT_ALL = 0,
	PLAYER_LOOK_SLOT_HALO1,
	PLAYER_LOOK_SLOT_HALO2,
	PLAYER_LOOK_SLOT_HALO3,
	PLAYER_LOOK_SLOTS,

	PLAYER_LOOK_PART_NAME_SIZE = 32,
};

/* a look as a profile keeps it: its parts by name, so that a game update's new
armour does not change what was chosen */
struct player_look_saved
{
	unsigned char model;
	unsigned char visor;
	char parts[PLAYER_LOOK_PARTS][PLAYER_LOOK_PART_NAME_SIZE];
};

struct player_looks_profile
{
	boolean separate;
	struct player_look_saved looks[PLAYER_LOOK_SLOTS];
};

/* ---------- the choices offered */

/* whether a model can be chosen here: Halo 1's always; Halo 2's with Halo 2's
files; Halo 3's with Halo 3's, on a PC */
boolean player_looks_model_offered(short model);
char const *player_looks_model_name(short model);
/* a list's next model offered from model, step 1 or -1, wrapping */
short player_looks_model_step(short model, short step);
/* whether a look has the second armour colour to choose: Halo CE's Spartan has
one change colour, so a look in it has none (the saved choice is left alone: it
is worn again with a Halo 2 or Halo 3 model) */
boolean player_looks_secondary_locked(struct player_look_saved const *look);
/* the same for the look the settings panel is editing (its own thread) */
int player_looks_panel_secondary_locked(void);
/* whether a model has armour rows */
boolean player_looks_model_has_armour(short model);
/* a part's choices (the model's own is the first: 0 means no such row); its
name for a choice (0 "DEFAULT"), in capitals, plain: "mp_eva": "EVA" */
short player_looks_part_count(short model, short part);
void player_looks_part_text(short model, short part, short choice, char *text, long text_size);
/* the permutation's own name of a choice ("": the model's own); the choice of a
saved name (0: none of the list) */
char const *player_looks_part_permutation(short model, short part, short choice);
short player_looks_part_choice(short model, short part, char const *permutation);

/* ---------- a profile's */

void player_looks_profile_get(long profile_index, struct player_looks_profile *profile);
boolean player_looks_profile_save(long profile_index, struct player_looks_profile const *profile);
/* the look a level of this kind wears for it (slot: _HALO1, _HALO2) */
struct player_look_saved const *player_looks_profile_look(struct player_looks_profile const *profile, short slot);

/* ---------- the game */

/* (main.c, the top of a frame) the settings panel's choices taken; each look a
player wears wanted, its pack built and loaded when it can be */
void player_looks_update(void);
/* (cache_files.c, a map let go) nothing of its packs kept */
void player_looks_map_unloaded(void);
/* a local player's look in the level loaded, as sent to the others */
void player_looks_local(short local_player_index, struct player_look *look);
/* (players of the game) what a machine's player wears: FALSE, the level's own */
boolean player_looks_of_player(long player_index, struct player_look *look);
/* the visor a player wears in a multiplayer game; NONE, the profile's as ever */
short player_looks_game_visor(long player_index);

/* (render_objects.c) what to draw for object: model_index, the nodes, the
region permutations and the change colours given are changed to the look's; the
preview's and a player's secondary colour among them. The nodes, permutations
and colours are good until the next call */
void player_looks_draw(
	long object_index,
	long *model_index,
	real_matrix4x3 const **node_matrices,
	char const **region_permutations,
	real_rgb_color const **change_colors);

/* the menus' preview: a look's pack, built the first time it is shown (NULL: the
stock Spartan's, Halo 2's are not in a pack yet), and its armour shown; the second
colour shown */
char const *player_looks_preview_pack(struct player_look_saved const *look);
void player_looks_preview_primary(short color);
void player_looks_preview_secondary(short color);
/* a Halo 3 pack's bitmaps that its armour shown draws, the preview's wait: how many
(NONE: not such a pack) */
short player_looks_preview_bitmaps(char const *pack, long *bitmaps, short most);
/* (CUSTOMIZATION's opening) every model's pack not built yet built behind the menus,
each of them the once: a pick is then only a load */
void player_looks_prebuild(void);
/* (CUSTOMIZATION's opening) the looks' packs on the disk held to a few */
void player_looks_packs_prune(void);

/* a console command that starts with "looks" (tests): "show" */
boolean player_looks_command(char const *arguments);

/* (the settings panel, its own thread: Game's Appearance rows) */
enum
{
	PLAYER_LOOKS_PANEL_MODEL = 0,
	PLAYER_LOOKS_PANEL_ALL_GAMES,
	PLAYER_LOOKS_PANEL_GAME,
	PLAYER_LOOKS_PANEL_VISOR,
	PLAYER_LOOKS_PANEL_HELMET,
	PLAYER_LOOKS_PANEL_LEFT_SHOULDER,
	PLAYER_LOOKS_PANEL_RIGHT_SHOULDER,
	PLAYER_LOOKS_PANEL_CHEST,
	PLAYER_LOOKS_PANEL_ARMS,
	PLAYER_LOOKS_PANEL_LEGS,
	PLAYER_LOOKS_PANEL_BODY,
	PLAYER_LOOKS_PANEL_ROWS,
};
int player_looks_panel_offered(int row);
int player_looks_panel_count(int row);
char const *player_looks_panel_name(int row, int index);
int player_looks_panel_chosen(int row);
void player_looks_panel_choose(int row, int index);

#endif
