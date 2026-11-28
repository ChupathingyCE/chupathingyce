/*
H2_MAP_PICTURE.H

The pictures of the player's own Halo 2 maps for this engine's menus: the
ElDewrito-style SELECT MAP, the lobby's map card and the campaign lobby's
mission card (menu_functions.c, through custom_edition_maps.c's picture
hook), and Halo 2's own pregame lobby card (h2_menu.c). Each is read from
the map file itself when first shown (h2_level_map_picture_read: a
multiplayer map's level-data bitmap, a campaign level's map shot) by a
reader thread of this file's own - the map opened, its chunks read and
inflated off the game's thread, so the lobby never stops for it; the
picture shows a frame or two later (HALO_H2_PICTURE_READER=0: read on the
asking thread, as before) -, made a texture and kept, so that scrolling back
to it costs nothing; nothing of them is in the repo. Each is resampled to one square
texture (256x256 on the PC, 128x128 on the Vita: the widgets draw it whole,
scaled), and the textures kept are bounded (H2_MAP_PICTURE_KEPT): the
least recently shown is let go first.
*/

#ifndef __H2_MAP_PICTURE_H
#define __H2_MAP_PICTURE_H

#include "cseries.h"

struct bitmap_data;

enum
{
	/* (the frames the campaign lobby's mission card and the co-op lobby
	give their picture widgets for a Halo 2 mission: this and a slot's
	index; custom_edition_maps_picture answers them) */
	H2_MAP_PICTURE_FIRST_FRAME = 0x7000,
	H2_MAP_PICTURE_SLOTS = 48,
};

/* the picture of the maps folder's Halo 2 map `name` (its file's name, no
extension; `campaign` a campaign level's map shot), read the first time
and kept; NULL when it has none (said once in the log), or while the reader
is still reading it. *u1 and *v1: the
picture's extent on its texture (a canvas of powers of two) */
struct bitmap_data *h2_map_picture_get(char const *name, boolean campaign, real *u1, real *v1);

/* a frame for a bitmap widget of the campaign level pictures showing the
Halo 2 mission `name`'s picture (H2_MAP_PICTURE_FIRST_FRAME + its slot) */
short h2_map_picture_frame(char const *name, boolean campaign);

/* (custom_edition_maps.c) the picture a frame from h2_map_picture_frame
names, NULL when the map has none; FALSE when the frame is not one of these */
boolean h2_map_picture_for_frame(short frame, struct bitmap_data **picture);

/* (tests: "@h2picture name", main.c) the multiplayer map's picture asked
for every frame until shown; the game thread's share logged */
void h2_map_picture_test(char const *name);
void h2_map_picture_test_frame(void);

/* the textures kept: their count and bytes (the memory cost) */
void h2_map_picture_usage(long *count, long *bytes);

#endif
