/*
GUIDE.H

The Guide (port/linux/game/guide.c): a menu of its own, drawn by the port
over whatever is on screen (any of the three menu sets, or a game in
progress), the same in each. The first player opens it and it takes their
input until it closes; the game keeps running under it, and pauses only
where the pause menu would (a game on this machine alone). Desktop and
Android; not on the consoles yet (HALO_CONSOLE_NO_GUIDE).
*/

#ifndef GUIDE_H
#define GUIDE_H

/* (HALO_GUIDE: halo_console_features.h) */

#ifdef HALO_GUIDE

struct halo_ui_pointer;

/* opened (on its first section, or where it was last) or closed; closing
gives the input back to what was under it */
void guide_open(void);
void guide_close(void);
/* opened on the About screen (the menus' ABOUT rows): B closes it */
void guide_about_open(void);
boolean guide_active(void);

/* each frame from the menus (ui_widget.c's process_ui_widgets), whether a
menu is up: what opens or closes it, and its toasts' time */
void guide_update(boolean menu_open);
/* while it is open, instead of the menus: its input */
void guide_process(void);
/* the menus' pointer while it is open */
void guide_pointer(struct halo_ui_pointer const *pointer);
/* each frame, after the menus and the screens of code (the first player's
render): the Guide, or a toast */
void guide_render(void);

/* while it is open: the first player's controls are the Guide's
(player_control.c gives their player none) */
boolean guide_takes_player(void);

/* a toast: a short line at the bottom of the screen for a few seconds,
quiet, never over a game unless delta.notify_in_game is on (the Guide's
own: a new invite or friend request, a friend online with
delta.notify_online) */
void guide_toast(char const *title, char const *line);

/* the pause menu's DELTA with what waits in it (invites, friend
requests): "DELTA 2" for the text of its string list; else NULL */
wchar_t const *guide_pause_label(long string_list_tag);

#endif

#endif
