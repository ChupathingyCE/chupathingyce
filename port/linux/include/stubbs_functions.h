/*
STUBBS_FUNCTIONS.H

Stubbs the Zombie's script functions, a table of their own after Halo's
(port/linux/game/stubbs_functions.c; Stubbs mode only). A function whose
body belongs to another part of the mode calls that part's hook
(stubbs_campaign.h, stubbs_player.h).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef STUBBS_FUNCTIONS_H
#define STUBBS_FUNCTIONS_H

/* (HALO_CONSOLE_NO_STUBBS: which build has Stubbs mode, read here so
every unit sees the same side of the switch) */
#include "halo_console_features.h"

/* how many there are (none outside Stubbs mode), one by its index among
them, and a name's index (NONE) (hs.c) */
struct hs_function_definition;
long stubbs_function_count(void);
struct hs_function_definition *stubbs_function_get(long index);
long stubbs_function_find(char const *name);

/* the player action tests Stubbs's scripts wait on (player_action_test_melee,
the dance-off's, ...): a local player's input of this tick, noted beside
Halo's own tests (player_control.c's get_local_player_input_blob), and the
scripts' (player_action_test_reset), which clears both */
#ifndef HALO_CONSOLE_NO_STUBBS
void stubbs_functions_action_note(short local_player_index, unsigned long unit_control_flags,
	boolean switch_grenades, real throttle_forward, real throttle_left);
void stubbs_functions_action_reset(void);
#else
/* (a console leaves Stubbs mode out, HALO_CONSOLE_NO_STUBBS in
halo_console_features.h: empty ones here) */
static __inline void stubbs_functions_action_note(short local_player_index, unsigned long unit_control_flags,
	boolean switch_grenades, real throttle_forward, real throttle_left)
{ (void)local_player_index; (void)unit_control_flags; (void)switch_grenades; (void)throttle_forward; (void)throttle_left; }
static __inline void stubbs_functions_action_reset(void) { }
#endif

#endif
