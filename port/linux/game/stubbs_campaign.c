/*
STUBBS_CAMPAIGN.C

Stubbs mode's campaign (stubbs_campaign.h): its levels, in its own order,
and how one leads to the next. Called only in Stubbs mode; each body gives
Halo's answer back until it is written.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "stubbs_campaign.h"

#ifndef HALO_CONSOLE_NO_STUBBS /* (the header's empty ones on a console) */

char const *stubbs_campaign_level_name(
	short level,
	char const *halo_name)
{
	(void)level;
	return halo_name;
}

short stubbs_campaign_level_from_name(
	char const *name,
	short halo_level)
{
	(void)name;
	return halo_level;
}

boolean stubbs_campaign_map_name_allowed(
	void)
{
	return FALSE;
}

boolean stubbs_campaign_map_completed(
	short difficulty)
{
	(void)difficulty;
	return FALSE;
}

boolean stubbs_campaign_event_function(
	struct widget_instance *widget,
	struct event_record *event,
	word function_index,
	boolean *widget_deleted,
	boolean *result)
{
	(void)widget;
	(void)event;
	(void)function_index;
	(void)widget_deleted;
	(void)result;
	return FALSE;
}

#endif
