/*
HALO_SERVER_BROWSER.H

The PC menus' Server Browser (Join Game's SERVER BROWSER mode,
port/linux/game/menu_functions.c) and the games on Halo PC (Custom Edition)
maps among those it lists (port/linux/game/server_browser.c).

The browser lists the public games' listings (p2p_lobby.c) and, with the
game list (HALO_GAME_BROWSER), the games of halo.milenko.org's list. A game
on a Halo PC map has its map as <file>@ce (Custom Edition) or <file>@md
(HaloMD) in either (halo_map_families.h). Such a game is named as the menus'
map list names the map (ui_map_list.c), marked HALO PC or HALOMD, and joined
only where it can be played: with the map in its family's folders, on a
build with Halo PC map support (HALO_CUSTOM_EDITION).
*/

#ifndef HALO_SERVER_BROWSER_H
#define HALO_SERVER_BROWSER_H

/* a game's map's family (halo_map_families.h: a scenario's name or path is
an Xbox map's, <file>@ce and <file>@md Halo PC maps'); for a Halo PC map,
its name as players know it in text (an Xbox map's is the menus' to name:
text is left alone) */
short server_browser_map_family(char const *map, wchar_t *text, short length);

/* why a game on this map can't be joined here (its family's folders lack
the map, or the build plays no Halo PC maps), in message; FALSE if nothing
stops it */
boolean server_browser_map_blocked(char const *map, wchar_t *message, short length);

#endif
