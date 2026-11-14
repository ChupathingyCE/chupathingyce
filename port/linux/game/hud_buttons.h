/*
HUD_BUTTONS.H

A game's prompts' buttons in HD (input.hud_buttons: hud_buttons.c).
*/

#ifndef HUD_BUTTONS_H
#define HUD_BUTTONS_H

/* a HUD message's button icon (the HUD globals' button icons' index) drawn
in HD in the box the map's picture takes (the HUD's pixels: x, top, width,
height), at alpha; FALSE where it is not (input.hud_buttons is "classic",
or the button has no HD picture): the map's picture is drawn then */
boolean hud_buttons_draw(short icon_index, real x, real top, real width, real height, real alpha);

#endif
