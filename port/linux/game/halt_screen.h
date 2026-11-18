/*
HALT_SCREEN.H

The halt screen in the menus' look (halt_screen.c), on the desktop builds
with the overlay and the Guide (HALO_GUIDE): what stopped the game
(halt_facts.h) and the ways out, by controller and keyboard: RESTART (the
setup screen, where the build has one), SEND MY LOGS (log_send.c, as the
setup screen and the Guide send them; the code it returns shown), OPEN LOG
FOLDER (not under gamescope) and QUIT. Called from halt_and_catch_fire
(main.c) only: no tag is looked up, and nothing is allocated until the
player sends their logs.
*/

#ifndef __HALT_SCREEN_H
#define __HALT_SCREEN_H

#include "halt_facts.h"

/* whether it is shown: someone watches the run (platform_run_is_unwatched)
and the overlay draws. Takes the first player's input from here on */
int halt_screen_shown(void);
/* a frame of it, drawn into the overlay, with the player's input since the
last (RESTART and QUIT do not return) */
void halt_screen_frame(struct halt_facts const *facts);

#endif
