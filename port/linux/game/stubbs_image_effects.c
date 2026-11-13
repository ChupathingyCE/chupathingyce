/*
STUBBS_IMAGE_EFFECTS.C

Stubbs mode's image effects (stubbs_player.h): what the scripts'
image_effect_* functions start and end, drawn over the 3D view. Called only
in Stubbs mode; each body does nothing until it is written.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "stubbs_player.h"

#ifndef HALO_CONSOLE_NO_STUBBS /* (the header's empty ones on a console) */

void stubbs_image_effect_set(
	char const *name)
{
	(void)name;
}

void stubbs_image_effect_start_blend(
	char const *name,
	real seconds)
{
	(void)name;
	(void)seconds;
}

void stubbs_image_effect_end(
	void)
{
}

void stubbs_image_effects_render(
	short local_player_index)
{
	(void)local_player_index;
}

#endif
