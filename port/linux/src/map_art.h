/*
MAP_ART.H

The site's pictures of the stock multiplayer maps (configure.py
--game-browser, HALO_GAME_BROWSER), for Online Games' details when the
game's own ui.map has no picture of a listed game's map: Halo PC's maps
without Halo PC's ui.map, for one. The site has one of each of the Xbox's
maps (art/maps/<map>.jpg) and Halo PC's picture of each of Halo PC's
(art/maps/ce/<map>.jpg), the pictures the Discord activity names
(discord_activity.c). map_art.c. Plain types only: the game's units call it.
*/

#ifndef __MAP_ART_H
#define __MAP_ART_H

enum
{
	/* the site has no picture of it, or it could not be had */
	MAP_ART_NONE,
	/* read from the cache or downloaded on a thread of its own */
	MAP_ART_LOADING,
	MAP_ART_READY,
};

/* the site's picture of a stock map by its file's name ("icefields"): of
the Xbox's set (xbox_set nonzero), "maps/<file>", else Halo PC's,
"maps/ce/<file>"; 0 if the site has none of it */
int map_art_site_path(int xbox_set, const char *file, char *path, int size);

/* the picture at a site path (map_art_site_path's): MAP_ART_READY with its
pixels (RGBA, rows from the top; kept until a few other pictures are asked
for), its size and a serial of its own (ui_overlay_picture's); else
MAP_ART_LOADING or MAP_ART_NONE. Asking starts the loading: from the save
root's map_art folder, else downloaded from the site (and kept there) while
online play is on (network.online). Never waits. */
int map_art_get(const char *path, const unsigned char **pixels, int *width, int *height, int *serial);

#endif
