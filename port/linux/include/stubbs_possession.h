/*
STUBBS_POSSESSION.H

Stubbs the Zombie's possession (port/linux/game/stubbs_possession.c): his
thrown hand lands on a human's head and the player drives the human until
he lets go, the human dies, or the hand is called home. Called only in
Stubbs mode (stubbs_mode_active), so Halo never reaches any of it. The
hand itself (thrown, driven, called home) is stubbs_engine.c's.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef STUBBS_POSSESSION_H
#define STUBBS_POSSESSION_H

/* (HALO_CONSOLE_NO_STUBBS: which build has Stubbs mode, read here so
every unit sees the same side of the switch) */
#include "halo_console_features.h"

#ifndef HALO_CONSOLE_NO_STUBBS

/* a Stubbs unit definition (bipd or vehi) about to be rewritten in Halo's
layout (stubbs_mode.c): whether it can be possessed is noted first, since
Halo's layout keeps none of Stubbs's seats. The first one of a map forgets
the last map's */
void stubbs_possession_note_unit_definition(void const *tag_instance, void const *definition,
	void const *tag_cache, unsigned long tag_data_size);

/* a local player's input of a tick (player_control.c's
handle_one_player_input, after stubbs_engine_player_input): driving the
hand, melee or action by a human's head takes him; while a human is taken,
the hand called home or the human's death gives him back to the game */
void stubbs_possession_player_input(short local_player_index, unsigned long unit_control_flags);

/* a living human the hand may take: his unit definition has Stubbs's
possession seat, nobody drives him, he rides nothing */
boolean stubbs_possession_unit_possessable(long unit_index);

/* the nearest human the hand can reach from where it is (NONE for none) */
long stubbs_possession_find_target(long hand_unit_index);

/* the hand on the human's head and the player driving the human (TRUE when
it happened) */
boolean stubbs_possession_begin(short local_player_index, long hand_unit_index, long human_unit_index);

/* the player back in Stubbs, the hand gone, the human back to his own
mind (for scripts and the console; black goes through stubbs_engine.c) */
void stubbs_possession_release(short local_player_index);

/* the human a local player drives through the hand (NONE for none) */
long stubbs_possession_body_unit(short local_player_index);

#else
/* (a console leaves Stubbs mode out: each hook is an empty one here) */
static __inline void stubbs_possession_note_unit_definition(void const *tag_instance, void const *definition,
	void const *tag_cache, unsigned long tag_data_size)
{ (void)tag_instance; (void)definition; (void)tag_cache; (void)tag_data_size; }
static __inline void stubbs_possession_player_input(short local_player_index, unsigned long unit_control_flags)
{ (void)local_player_index; (void)unit_control_flags; }
static __inline long stubbs_possession_body_unit(short local_player_index) { (void)local_player_index; return NONE; }
#endif

#endif
