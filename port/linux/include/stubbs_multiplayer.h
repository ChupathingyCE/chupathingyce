/*
STUBBS_MULTIPLAYER.H

Stubbs mode's two-player game: the co-op menus' Stubbs branches and how a
Stubbs game is told apart online (port/linux/game/stubbs_multiplayer.c).
Each hook is called only in Stubbs mode (stubbs_mode_active), except
stubbs_multiplayer_join_refused, which acts only in Stubbs mode or on a
Stubbs game.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef STUBBS_MULTIPLAYER_H
#define STUBBS_MULTIPLAYER_H

/* (HALO_CONSOLE_NO_STUBBS: which build has Stubbs mode, read here so
every unit sees the same side of the switch) */
#include "halo_console_features.h"

struct widget_instance;
struct event_record;

#ifndef HALO_CONSOLE_NO_STUBBS
/* a menu widget's event script the map lacks (ui_widget.c, where Halo
logs "failed to run ui widget event script", such as Stubbs's
'multiplayer'): TRUE when it is one of Halo's the original game had
nothing for either (it counts as run) */
boolean stubbs_multiplayer_widget_script(char const *script);

/* the co-op profile list's choice, where Halo finds no selectable profile
(ui_widget_event_handler_functions.c): an empty slot, whose own list item
asks about a new profile; TRUE, *result FALSE (the screen stays), in place
of Halo's "this is not a selectable player profile" and its error sound */
boolean stubbs_multiplayer_profile_chosen(struct widget_instance *widget, struct event_record *event,
	boolean *widget_deleted, boolean *result);

/* a network game this machine is joining, its map named as the loader has
it (network_client_manager.c, whatever the mode): TRUE when Stubbs mode is
on and the game is not Stubbs's, or off and the game is Stubbs's, the
player told why in a message; FALSE to go on joining. Online two-player
Stubbs isn't made yet */
boolean stubbs_multiplayer_join_refused(char const *map_name);

/* a machine from elsewhere asking into the game this machine hosts, in
Stubbs mode only (network_server_manager.c): TRUE to refuse it, which it
is told as the game not taking players (the one refusal every copy knows) */
boolean stubbs_multiplayer_remote_machine_refused(void);

/* where TWO PLAYER's profile list for a player after the first starts
(ui_widget_event_handler_functions.c; the list's entries are saved game
file indices, -1 an empty slot): the first profile of his own that isn't
the one player 1 chose, else an empty slot, else -1 to leave the list
where Halo put it (on player 1's), in Stubbs mode only */
short stubbs_multiplayer_profile_list_start(short local_player_index, long const *profile_indices, short count);

#else
/* (a console leaves Stubbs mode out, HALO_CONSOLE_NO_STUBBS in
halo_console_features.h, and may not build this part's file: each hook
is an empty one here) */
static __inline boolean stubbs_multiplayer_widget_script(char const *script) { (void)script; return FALSE; }
static __inline boolean stubbs_multiplayer_profile_chosen(struct widget_instance *widget, struct event_record *event,
	boolean *widget_deleted, boolean *result)
{ (void)widget; (void)event; (void)widget_deleted; (void)result; return FALSE; }
static __inline boolean stubbs_multiplayer_join_refused(char const *map_name) { (void)map_name; return FALSE; }
static __inline boolean stubbs_multiplayer_remote_machine_refused(void) { return FALSE; }
static __inline short stubbs_multiplayer_profile_list_start(short local_player_index, long const *profile_indices,
	short count)
{ (void)local_player_index; (void)profile_indices; (void)count; return -1; }
#endif

#endif
