/*
LAUNCHER.H

Chupathingy's own screen before the game (launcher.c): the same window, run
before any map loads, with no map's art or fonts. It shows the player's maps
by family with their state and folders, adds maps from a disc image or an
installer, and sends the logs. Then PLAY hands the window to the game.

It opens on the first start without the Xbox maps (Welcome, in place of the
message boxes), when the maps folder isn't right (Maps), with --setup, with
Shift or a controller's Back (View) held while the game starts, or every
time with launcher.show_at_start = "always". A run nobody watches
(platform_run_is_quiet), the dedicated server and Android never show it.

The desktop builds with the overlay (HALO_GAME_BROWSER) have it; the others
keep the message boxes (sdl_platform.c).
*/

#ifndef __HALO_LAUNCHER_H
#define __HALO_LAUNCHER_H

#if defined(HALO_GAME_BROWSER) && !defined(HALO_ANDROID) && !defined(HALO_SERVER)
#define HALO_LAUNCHER 1
#endif

#ifdef HALO_LAUNCHER
/* the first start without the Xbox maps (platform_offer_game_data): Welcome,
for the game folder destination (size bytes). Nonzero once destination has
the Xbox maps; the player may point at a folder that has them elsewhere
(paths.data is written), which destination then is. Quits if the player
does. */
int launcher_welcome(char *destination, unsigned long size);

/* the maps folder isn't the Xbox maps (data_check_maps): the launcher opens
on Maps when the window opens, rather than the game quitting */
void launcher_open_on_maps(void);

/* the window and its OpenGL context made (platform_video_initialize): the
launcher, when something asked for it; returns when the player picks PLAY */
void launcher_at_start(void);
#endif

#endif
