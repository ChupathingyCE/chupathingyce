/*
STUBBS_CAMPAIGN.H

Stubbs mode's campaign (port/linux/game/stubbs_campaign.c): its levels, in
its own order, and how one leads to the next. Each hook is called only in
Stubbs mode (stubbs_mode_active), so Halo's campaign never reaches it; until
a body is written, each gives Halo's answer back.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef STUBBS_CAMPAIGN_H
#define STUBBS_CAMPAIGN_H

struct widget_instance;
struct event_record;

/* the map a campaign level plays (main.c's main_get_solo_level_name, the
menus' level list in ui_widget_event_handler_functions.c): halo_name for
now */
char const *stubbs_campaign_level_name(short level, char const *halo_name);

/* the campaign level a map's name is (main.c's
main_get_solo_level_from_name): halo_level for now */
short stubbs_campaign_level_from_name(char const *name, short halo_level);

/* whether the running map's scripts may call map_name (hs.c refuses it to
every map but a pack's ui.map): FALSE for now */
boolean stubbs_campaign_map_name_allowed(void);

/* the script function (map_completed <difficulty>) (stubbs_functions.c):
FALSE for now */
boolean stubbs_campaign_map_completed(short difficulty);

/* a menu widget's event handler function Halo's table lacks (Stubbs's
ui.map): TRUE once it has run, its result in *result; FALSE for now, and
Halo's "invalid event_handler_function" follows */
boolean stubbs_campaign_event_function(struct widget_instance *widget, struct event_record *event,
	word function_index, boolean *widget_deleted, boolean *result);

#endif
