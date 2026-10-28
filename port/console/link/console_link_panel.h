/*
CONSOLE_LINK_PANEL.H

The DELTA PROFILE panel over a console's Online Games screen
(HALO_CONSOLE_ONLINE_GAMES; console_link.h is what it shows): Start opens
it, as Start opens Delta Profile on the desktop builds' Online Games. It
shows whether the console is linked and to whom, or the code to enter at
stats.chupathingy.org/connect with its QR code, then asks the player to
confirm. Drawn in the menus' fonts on the 640x480 screen the console's
Online Games draws.
*/

#ifndef __CONSOLE_LINK_PANEL_H
#define __CONSOLE_LINK_PANEL_H

void console_link_panel_open(void);
int console_link_panel_active(void);
/* a button the Online Games screen took while the panel is up
(input.h's _gamepad_* button index) */
void console_link_panel_button(short button);
/* every frame the panel is up, after the screen is drawn */
void console_link_panel_render(long font, long heading_font);
/* the screen's one line about it: "DELTA: @HANDLE" or "DELTA: NOT LINKED" */
void console_link_panel_summary(char *text, int size);

#endif
