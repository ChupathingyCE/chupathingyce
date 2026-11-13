/*
STUBBS_CAMPAIGN.H

Stubbs mode's campaign (port/linux/game/stubbs_campaign.c): its levels, in
its own order, and how one leads to the next. Each hook is called only in
Stubbs mode (stubbs_mode_active), so Halo's campaign never reaches it.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef STUBBS_CAMPAIGN_H
#define STUBBS_CAMPAIGN_H

/* (HALO_CONSOLE_NO_STUBBS: which build has Stubbs mode, read here so
every unit sees the same side of the switch) */
#include "halo_console_features.h"

struct widget_instance;
struct event_record;

/* Stubbs's levels, a10_plaza to c50_end (Halo has ten) */
#define STUBBS_CAMPAIGN_LEVEL_COUNT 12

/* Stubbs's ui.map's Load Level strings: a level's name, picture and
description by its index, then the locked card's and the saved game's
description */
#define STUBBS_CAMPAIGN_STRING_LOCKED STUBBS_CAMPAIGN_LEVEL_COUNT
#define STUBBS_CAMPAIGN_STRING_SAVED_GAME (STUBBS_CAMPAIGN_LEVEL_COUNT + 1)

#ifndef HALO_CONSOLE_NO_STUBBS
/* as the game starts (main.c's main_loop): Stubbs's profiles and saved games
in a folder of their own */
void stubbs_campaign_initialize(void);

/* the map a campaign level plays (main.c's main_get_solo_level_name, the
menus' level list in ui_widget_event_handler_functions.c): NULL past the
last */
char const *stubbs_campaign_level_name(short level, char const *halo_name);

/* the campaign level a map's name is (main.c's
main_get_solo_level_from_name), whatever its path or pack: NONE for a map
not in Stubbs's campaign */
short stubbs_campaign_level_from_name(char const *name, short halo_level);

/* whether the running map's scripts may call map_name (hs.c refuses it to
every map but a pack's ui.map): a Stubbs level's may */
boolean stubbs_campaign_map_name_allowed(void);

/* the script function (map_completed <difficulty>) (stubbs_functions.c):
whether player 1's profile has finished this level on that difficulty or a
harder one */
boolean stubbs_campaign_map_completed(short difficulty);

/* Load Level (ui_widget_event_handler_functions.c): a local player's
profile's finished difficulties of a level (a flag each), and whether the
level may be picked (finished, the one after the last finished, or the
first) */
unsigned long stubbs_campaign_level_flags(short local_player_index, short level);
boolean stubbs_campaign_level_available(short local_player_index, short level);

/* a level won (main.c's main_won_map_private): the level is kept as
finished for each local player's profile, and the next level's map is
given (NULL after the last) */
char const *stubbs_campaign_level_won(char const *map_name, short local_player_count);

/* a menu widget's event handler function Halo's table lacks (Stubbs's
ui.map): TRUE once it has run, its result in *result; FALSE for now, and
Halo's "invalid event_handler_function" follows */
boolean stubbs_campaign_event_function(struct widget_instance *widget, struct event_record *event,
	word function_index, boolean *widget_deleted, boolean *result);

#else
/* (a console leaves Stubbs mode out, HALO_CONSOLE_NO_STUBBS in
halo_console_features.h, and may not build this part's file: each hook
is an empty one here) */
static __inline void stubbs_campaign_initialize(void) { }
static __inline unsigned long stubbs_campaign_level_flags(short local_player_index, short level)
{ (void)local_player_index; (void)level; return 0; }
static __inline boolean stubbs_campaign_level_available(short local_player_index, short level)
{ (void)local_player_index; (void)level; return FALSE; }
static __inline char const *stubbs_campaign_level_won(char const *map_name, short local_player_count)
{ (void)map_name; (void)local_player_count; return NULL; }
static __inline char const *stubbs_campaign_level_name(short level, char const *halo_name) { (void)level; return halo_name; }
static __inline short stubbs_campaign_level_from_name(char const *name, short halo_level) { (void)name; return halo_level; }
static __inline boolean stubbs_campaign_map_name_allowed(void) { return FALSE; }
static __inline boolean stubbs_campaign_map_completed(short difficulty) { (void)difficulty; return FALSE; }
static __inline boolean stubbs_campaign_event_function(struct widget_instance *widget, struct event_record *event,
	word function_index, boolean *widget_deleted, boolean *result)
{ (void)widget; (void)event; (void)function_index; (void)widget_deleted; (void)result; return FALSE; }
#endif

#endif
