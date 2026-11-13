/*
STUBBS_PLAYER.C

Stubbs mode's player (stubbs_player.h): the camera behind Stubbs and his
HUD. Called only in Stubbs mode; each body does nothing until it is
written.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "stubbs_player.h"

#ifndef HALO_CONSOLE_NO_STUBBS /* (the header's empty ones on a console) */

boolean stubbs_player_camera_following(
	long unit_index,
	short *perspective)
{
	(void)unit_index;
	(void)perspective;
	return FALSE;
}

void stubbs_player_hud_draw(
	short local_player_index,
	long unit_index)
{
	(void)local_player_index;
	(void)unit_index;
}

void stubbs_player_hud_show_special_attack(
	short special_attack,
	boolean show)
{
	(void)special_attack;
	(void)show;
}

#endif
