/*
STUBBS_MULTIPLAYER.C

Stubbs mode's two-player game (stubbs_multiplayer.h). Called only in Stubbs
mode.

TWO PLAYER is Halo's own co-op flow, run from Stubbs's ui.map as it is: a
profile for player 1, one for player 2 (each picked with its own
controller), the level list and the difficulty, then the level with both
players in split screen. Stubbs's levels have co-op of their own (a10's
coop_setup: player 2 starts beside Stubbs and skips the tutorial), so the
engine has nothing to add there. What is left for this file is where
Stubbs's menus name things its maps lack, and keeping Stubbs's games and
Halo's apart online until online two-player Stubbs is made: a Stubbs copy
hosts no one and joins no Halo game, and Halo joins no Stubbs game, each
told why in plain words.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "stubbs_mode.h"
#include "stubbs_multiplayer.h"

#ifndef HALO_CONSOLE_NO_STUBBS /* (the header's empty ones on a console) */

#include "halo_join_reasons.h"
#include "interface/player_ui.h"
#include "saved games/player_profile_list.h"
#include "halo_map_families.h"

#include <string.h>

/* the platform layer's (sdl_platform.c) */
void platform_log(char const *format, ...);
void platform_show_message(char const *title, char const *message);

/* what a player is told when a join is given up */
#define STUBBS_JOIN_FROM_STUBBS_MODE "Stubbs mode doesn't join online games yet. " \
	"Two players can play Stubbs the Zombie on one screen: TWO PLAYER on the main menu."
#define STUBBS_JOIN_FROM_HALO "This is a Stubbs the Zombie game. Halo can't join it."

/* the event scripts Stubbs's main menu names that its ui.map has no script
for: Halo's main menu's ('multiplayer' moves Halo's backdrop camera), kept
on Stubbs's buttons when its menus were made from Halo's. The original game
had nothing to run for them either, so they count as run */
static char const *const stubbs_menu_scripts_from_halo[] =
{
	"multiplayer",
	"load_campaign",
	"settings",
};

boolean stubbs_multiplayer_widget_script(
	char const *script)
{
	size_t index;

	if (!script)
		return FALSE;
	for (index = 0; index < NUMBEROF(stubbs_menu_scripts_from_halo); index++)
	{
		if (!strcmp(script, stubbs_menu_scripts_from_halo[index]))
			return TRUE;
	}
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
	/* an empty slot of a SELECT PROFILE screen: its own list item asks
	whether to make a new profile there (Stubbs's ui.map), so the screen
	stays put, without an error or the error sound over the question */
	*result = FALSE;
	return TRUE;
}

boolean stubbs_multiplayer_join_refused(
	char const *map_name)
{
	char pack[MAP_PACK_ID_LENGTH + 1];
	boolean stubbs_game = map_name && map_family_pack_id(map_name, pack, sizeof(pack)) &&
		!strcmp(pack, STUBBS_MODE_PACK_ID);
	boolean stubbs_mode = stubbs_mode_active();

	if (!stubbs_mode && !stubbs_game)
		return FALSE;
	/* (and in Stubbs mode every game for now, Stubbs's too: no copy hosts
	one, stubbs_multiplayer_remote_machine_refused) */
	platform_log("stubbs mode: not joining the game on %s: %s", map_name ? map_name : "?",
		stubbs_mode ? "Stubbs mode joins no online games yet" : "a Stubbs the Zombie game, and Stubbs mode is off");
	platform_show_message(JOIN_FAILED_TITLE, stubbs_mode ? STUBBS_JOIN_FROM_STUBBS_MODE : STUBBS_JOIN_FROM_HALO);
	return TRUE;
}

boolean stubbs_multiplayer_remote_machine_refused(
	void)
{
	platform_log("stubbs mode: refusing a machine from elsewhere: Stubbs mode hosts no online games yet");
	return TRUE;
}

short stubbs_multiplayer_profile_list_start(
	short local_player_index,
	long const *profile_indices,
	short count)
{
	long taken;
	short index;

	if (!stubbs_mode_active() || local_player_index <= 0 || !profile_indices)
		return -1;
	taken = player_ui_get_active_player_profile_index(0);
	for (index = 0; index < count; index++)
	{
		long profile_index = profile_indices[index];

		if (profile_index != NONE && profile_index != taken &&
			((unsigned long)profile_index & (1UL << PLAYER_PROFILE_LIST_VALID_BIT)) != 0 &&
			!player_profile_list_built_in(profile_index))
		{
			return index;
		}
	}
	for (index = 0; index < count; index++)
	{
		if (profile_indices[index] == NONE)
			return index;
	}
	return -1;
}

#endif
