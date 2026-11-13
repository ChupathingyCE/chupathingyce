/*
STUBBS_MULTIPLAYER.H

Stubbs mode's two-player game: the co-op menus' Stubbs branches and, later,
how a Stubbs game is told apart online
(port/linux/game/stubbs_multiplayer.c). Each hook is called only in Stubbs
mode (stubbs_mode_active); until a body is written, each does nothing.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef STUBBS_MULTIPLAYER_H
#define STUBBS_MULTIPLAYER_H

struct widget_instance;
struct event_record;

#ifndef HALO_CONSOLE_NO_STUBBS
/* a menu widget's event script the map lacks (ui_widget.c, where Halo
logs "failed to run ui widget event script", such as Stubbs's
'multiplayer'): TRUE once it has run; FALSE for now */
boolean stubbs_multiplayer_widget_script(char const *script);

/* the co-op profile list's choice, where Halo finds no selectable profile
(ui_widget_event_handler_functions.c): TRUE once it has chosen, its result
in *result; FALSE for now, and Halo's "this is not a selectable player
profile" follows */
boolean stubbs_multiplayer_profile_chosen(struct widget_instance *widget, struct event_record *event,
	boolean *widget_deleted, boolean *result);

#else
/* (a console leaves Stubbs mode out, HALO_CONSOLE_NO_STUBBS in
halo_console_features.h, and may not build this part's file: each hook
is an empty one here) */
static __inline boolean stubbs_multiplayer_widget_script(char const *script) { (void)script; return FALSE; }
static __inline boolean stubbs_multiplayer_profile_chosen(struct widget_instance *widget, struct event_record *event,
	boolean *widget_deleted, boolean *result)
{ (void)widget; (void)event; (void)widget_deleted; (void)result; return FALSE; }
#endif

#endif
