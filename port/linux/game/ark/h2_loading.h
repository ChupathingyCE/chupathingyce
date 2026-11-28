/*
H2_LOADING.H

Halo 2's own loading screen for a Halo 2 level (h2_loading.c), as Halo 2
Vista shows it: black, the HALO 2 logo centred, a beam of light sweeping
across it lighting the blue circuit plate and rings behind, a fade. Shown in
place of Halo CE's while a level built from the player's MCC maps loads (a
campaign mission, a multiplayer map, the next mission after a win: the same
load). Its art is the player's own, read when it comes up and let go when
the level is in; the sweep follows the load's progress. It draws with the
engine's screen quad, adding, so the Vita needs no new shader.
*/

#ifndef __H2_LOADING_H
#define __H2_LOADING_H

#include "cseries.h"

/* custom_edition_cache_tags_load, before it asks for the loading screen:
the map about to load (its scenario's path or file name); anything but a
Halo 2 level's makes the next loading screen Halo CE's */
void h2_loading_prepare(char const *map_name);

/* game_loading_screen_begin: TRUE when the screen about to come up is
Halo 2's (its art read, its level named); FALSE leaves Halo CE's */
boolean h2_loading_begin(void);

/* whether Halo 2's loading screen is up */
boolean h2_loading_active(void);

/* render_frame_pregame, after the widgets: the screen at this progress
(0..1) over the viewport */
void h2_loading_draw(real progress);

/* game_loading_screen_end: the art let go */
void h2_loading_end(void);

/* a level's first-time build (h2_level.c: 5 to 13 s on a PC, more on the
Vita, once for each builder version) shown on the same screen, its sweep going
with the bitmaps made: on the game thread alone and only when no loading
screen is up; its end takes the screen down (the load after brings it up
again) */
void h2_loading_build_begin(char const *maps_directory, char const *name);
void h2_loading_build_tick(void);
void h2_loading_build_end(void);

#endif
