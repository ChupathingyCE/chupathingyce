/*
PAUSE_LEAVE.H

Whose player a network game's pause screen's LEAVE GAME takes out
(ui_widget_event_handler_functions.c's network_game_remove_local_player).
The Xbox took the controller the press came from. With one person playing,
any controller drives the pause screen (ui_widget.c's
widget_takes_any_controller), so the press can come from a gamepad on
another port than the player's: Windows can list another device as a
gamepad before the one in hand, and a split screen game leaves its
controllers on their ports. That controller has no player in the game, and
LEAVE GAME did nothing; the keyboard (port 0) and the mouse (the screen's
player) still left. Nothing here has a dependency;
port/linux/tests/pause_leave_test.c tests it (tools/test_pause_leave.py).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __PAUSE_LEAVE_H
#define __PAUSE_LEAVE_H

/* (the Xbox's four controllers) */
#define PAUSE_LEAVE_CONTROLLERS 4

/* the controller whose player leaves: the press's, if that controller has a
player in the game (event_has_player); else the pause screen's player's;
else the press's, else the first. A press with no controller (the mouse's)
is NONE (-1), as is a screen with no player */
static short pause_leave_controller(short event_controller, int event_has_player, short screen_controller)
{
	int event_valid = event_controller >= 0 && event_controller < PAUSE_LEAVE_CONTROLLERS;

	if (event_valid && event_has_player)
		return event_controller;
	if (screen_controller >= 0 && screen_controller < PAUSE_LEAVE_CONTROLLERS)
		return screen_controller;
	return event_valid ? event_controller : 0;
}

#endif
