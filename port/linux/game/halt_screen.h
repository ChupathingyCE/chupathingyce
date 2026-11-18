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

/* what the halt screen does: HALT_SCREEN_SHOWN, drawn in the overlay (it
takes the first player's input from here on, the console and chat closed);
HALT_SCREEN_EXIT, nothing drawn and the game ends (a run nobody watches,
or a halt on a thread other than the window's, which can neither draw nor
read input); HALT_SCREEN_PLAIN, the game's own plain screen (no overlay) */
enum
{
	HALT_SCREEN_PLAIN,
	HALT_SCREEN_SHOWN,
	HALT_SCREEN_EXIT,
};
int halt_screen_choose(void);
/* a frame of it, drawn into the overlay, with the player's input since the
last (RESTART and QUIT do not return) */
void halt_screen_frame(struct halt_facts const *facts);

#endif
