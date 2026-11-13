/*
STUBBS_MULTIPLAYER.C

Stubbs mode's two-player game (stubbs_multiplayer.h). Called only in Stubbs
mode; each body does nothing until it is written.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "stubbs_multiplayer.h"

#ifndef HALO_CONSOLE_NO_STUBBS /* (the header's empty ones on a console) */

boolean stubbs_multiplayer_widget_script(
	char const *script)
{
	(void)script;
	return FALSE;
}

boolean stubbs_multiplayer_profile_chosen(
	struct widget_instance *widget,
	struct event_record *event,
	boolean *widget_deleted,
	boolean *result)
{
	(void)widget;
	(void)event;
	(void)widget_deleted;
	(void)result;
	return FALSE;
}

#endif
