/*
H2_FONT.H

Halo 2's own font for the in-game text of a Halo 2 level (h2_font.c): the
HUD's font tag and the terminal's remade from the player's h2_fonts files.
*/

#ifndef __H2_FONT_H
#define __H2_FONT_H

#include "cseries.h"

/* (hud.c, a Halo 2 level's HUD each frame) the font tags remade when they
are not yet, or the screen's size has changed; nothing without the player's
Halo 2 font files */
void h2_font_ensure(void);
/* (hud_messaging.c) whether the font tag draws in Halo 2's font now (remade
here): its spaces are Halo 2's, narrow - the Halo CE font's 3-pixel tuck
after each run of a message ate them ("toopen the gate") */
boolean h2_font_is_halo2(long font_index);

#endif
