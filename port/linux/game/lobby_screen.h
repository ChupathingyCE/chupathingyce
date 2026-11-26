/*
LOBBY_SCREEN.H

The game lobby of the Chupathingy menus (lobby_screen.c): one screen for
a game you host, a game you joined and a dedicated server, drawn by code
over the Xbox's pregame screen, and the preview of a game under way before
it is joined.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef LOBBY_SCREEN_H
#define LOBBY_SCREEN_H

#include "cseries.h"

struct halo_ui_pointer;
struct browser_game;
struct network_invite_advertisement;

/* whether the lobby covers the pregame screen now (the Chupathingy menus
with display.lobby on, the Xbox's pregame screen up), or a preview is up */
boolean lobby_screen_active(void);
/* each frame (interface/ui_widget.c): which screen is up, the host's lobby
for Delta's READY, the debug preview */
void lobby_screen_update(boolean menu_open);
void lobby_screen_process(void);
void lobby_screen_render(void);
void lobby_screen_pointer(struct halo_ui_pointer const *pointer);

/* after a game: whether the host lands in the lobby (its countdown paused,
CHANGE MAP there) instead of the Xbox's map select */
boolean lobby_screen_host_returns_to_lobby(void);
/* GAME SETUP's START GAME, System Link's host: a lobby of this machine's
game opens (its countdown waits for START GAME) */
void lobby_screen_hosted_opened(void);
/* a join from the game list lands in the lobby: its B goes back to the
list (as the Multiplayer menu's ONLINE GAMES opens it) */
void lobby_screen_joined_from_list(void);

/* a game under way, before it is joined (browser_screen.c): the preview
with what the list and the host's advertisement say. FALSE: not shown (the
setting off, the Xbox menus): the join goes on as it did */
boolean lobby_screen_preview_open(struct browser_game const *game,
	struct network_invite_advertisement const *advertisement);
boolean lobby_screen_preview_active(void);
/* the preview's answer: 1 JOIN THE GAME, -1 B (back to the list), 0 none yet */
int lobby_screen_preview_answer(void);
/* the preview's ADD PLAYERS: System Link's SELECT PROFILE is up over it,
and a picked quarter's last A (TRUE: back to the preview; FALSE: a player
is still picking) */
boolean lobby_screen_adding_players(void);
struct widget_instance;
boolean lobby_screen_add_players_finish(struct widget_instance *screen, short controller);

#endif
