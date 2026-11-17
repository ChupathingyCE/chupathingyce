/*
STUBBS_HORDE.H

Stubbs mode's brains and horde (port/linux/game/stubbs_horde.c): Stubbs eats
a human's brains and heals, and a human he or his zombies kill rises as a
zombie on his side that follows him and fights for him. Each hook does
nothing outside Stubbs mode (stubbs_mode_active).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef STUBBS_HORDE_H
#define STUBBS_HORDE_H

/* (HALO_CONSOLE_NO_STUBBS: which build has Stubbs mode, read here so
every unit sees the same side of the switch) */
#include "halo_console_features.h"

struct damage_data;

#ifndef HALO_CONSOLE_NO_STUBBS

/* a tick of the horde (game.c's game_tick, after the actors): the eats in
progress, the corpses that rise, the zombies that follow */
void stubbs_horde_update(void);

/* a player's action key (players.c, before Halo's own uses of it): TRUE
when it ate, or is eating, a human's brains and Halo's action is skipped */
boolean stubbs_horde_player_action(long player_index);

/* a unit's lethal damage (units.c's unit_damage_aftermath): a human killed
by Stubbs or a zombie is marked to rise */
void stubbs_horde_unit_damaged_to_death(long unit_index, struct damage_data const *damage_data);

/* the script function (unit_finishing_move <unit>): the unit eats the
brains of a human in reach if it can; TRUE when it started */
boolean stubbs_horde_unit_finishing_move(long unit_index);

/* the script function (ai_set_resurrection_group <ai>): where new zombies
go (an encounter, or a squad of one); NONE for the map's own choice */
void stubbs_horde_set_resurrection_group(long ai_reference);

/* whether a unit is one of Stubbs's zombies (risen here) */
boolean stubbs_horde_unit_is_zombie(long unit_index);

/* the map's actor variant for a unit of this kind (the palette entry whose
variant is of the unit's definition): NONE for none. ai_attach uses it
for a Stubbs squad that names no variant of its own */
long stubbs_horde_palette_variant_for_unit(long unit_index);

#else
static __inline void stubbs_horde_update(void) { }
static __inline boolean stubbs_horde_player_action(long player_index) { (void)player_index; return FALSE; }
static __inline void stubbs_horde_unit_damaged_to_death(long unit_index, struct damage_data const *damage_data) { (void)unit_index; (void)damage_data; }
static __inline boolean stubbs_horde_unit_finishing_move(long unit_index) { (void)unit_index; return FALSE; }
static __inline void stubbs_horde_set_resurrection_group(long ai_reference) { (void)ai_reference; }
static __inline boolean stubbs_horde_unit_is_zombie(long unit_index) { (void)unit_index; return FALSE; }
static __inline long stubbs_horde_palette_variant_for_unit(long unit_index) { (void)unit_index; return NONE; }
#endif

#endif
