/*
STUBBS_ENGINE.H

Stubbs the Zombie's engine systems (port/linux/game/stubbs_engine.c): his
special attacks (the hand first) and the two units a Stubbs player has, his
own body (the origin) and the one he drives now (his hand, or a human the
hand has taken). Called only in Stubbs mode (stubbs_mode_active), so Halo
never reaches any of it.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef STUBBS_ENGINE_H
#define STUBBS_ENGINE_H

/* (HALO_CONSOLE_NO_STUBBS: which build has Stubbs mode, read here so
every unit sees the same side of the switch) */
#include "halo_console_features.h"

/* Stubbs's special attacks, as his unit definition lists them */
enum
{
	_stubbs_special_attack_flatulence = 0,
	_stubbs_special_attack_gut_grenade,
	_stubbs_special_attack_hand,
	_stubbs_special_attack_head,
};

/* the most special attacks a unit definition has read here (Stubbs: 4) */
#define STUBBS_MAXIMUM_SPECIAL_ATTACKS 8

/* a special attack as a unit definition has it (Stubbs's layout: 0x170
bytes each, its block at 0x33C): the object it throws (NONE for none: the
flatulence), that object's group ('bipd' the hand, 'vehi' the head, 'proj'
the gut grenade), its ready and throw effects, its speed in world units a
second, how many of the unit's count one use takes, and the count a new
unit starts with (Stubbs's hand 1, his head 0: the scripts set the rest
with unit_special_attack_set_count) */
struct stubbs_special_attack
{
	long object_definition_index;
	unsigned long object_group;
	long ready_effect_index;
	long throw_effect_index;
	real speed;
	short cost;
	short initial_count;
	/* the most a unit can hold (0: the unit never has this attack, and
	its HUD never shows it) */
	short maximum_count;
	/* seconds of brains credit for one more (0: none this way) */
	real regeneration_period;
};

#ifndef HALO_CONSOLE_NO_STUBBS

/* a unit definition's special attacks (NULL, *count 0, for a unit with
none), as noted at the map's load */
struct stubbs_special_attack const *stubbs_special_attacks_get(long unit_definition_index, short *count);

/* a local player's thrown hand (NONE when it is on Stubbs's arm) */
long stubbs_engine_hand_unit(short local_player_index);

/* whether a unit is one of the thrown bipeds a player drives (the hand):
it has no mouth to eat with, only a head to take (stubbs_horde.c) */
boolean stubbs_engine_unit_is_thrown(long unit_index);

/* possession (its own file) tells the hand system the hand now rides
unit_index, the body the player was given: it is his until it dies or
black is pressed, and then the hand comes home and the player is back in
Stubbs (what becomes of the body is possession's own business) */
void stubbs_engine_hand_rides(short local_player_index, long unit_index);

/* a Stubbs unit definition (bipd or vehi) about to be rewritten in Halo's
layout (stubbs_mode.c): its special attacks noted first, since Halo's
layout has no room for them. The first one of a map forgets the last map's
hands and bodies */
void stubbs_engine_note_unit_definition(void const *tag_instance, void const *definition,
	void const *tag_cache, unsigned long tag_data_size);

/* a local player's input of a tick (player_control.c's
handle_one_player_input): the hand thrown, recalled or put on a human's
head. hand_button is the hand's bit among the player control flags (Halo's
switch grenades, black), taken out of them */
void stubbs_engine_player_input(short local_player_index, unsigned long *player_control_flags,
	unsigned long hand_button, unsigned long unit_control_flags);

/* a player's unit died (units.c): TRUE when it was the hand or a body the
hand rides, and the player is back in Stubbs's body (the player doesn't
die) */
boolean stubbs_engine_player_unit_died(long unit_index, long player_index);

/* for the script functions (stubbs_functions.c): a player's body
(player_origin), (unit_special_attack <unit> <index>), the special
attack counts and (player_move_origin <from> <to>) */
long stubbs_engine_player_origin_unit(long player_index);
boolean stubbs_engine_special_attack(long unit_index, short special_attack);
short stubbs_engine_special_attack_get_count(long unit_index, short special_attack);

/* the most of one a unit can hold (0 for an attack it never has) */
short stubbs_engine_special_attack_get_maximum(long unit_index, short special_attack);

/* brains eaten (stubbs_horde.c): the victim's credit goes toward every
attack that regenerates, one more each time it adds up to the attack's
period */
void stubbs_engine_special_attack_credit(long unit_index, real credit);
void stubbs_engine_special_attack_set_count(long unit_index, short special_attack, short count);
void stubbs_engine_player_move_origin(long from_unit_index, long to_unit_index);

#else
/* (a console leaves Stubbs mode out: each hook is an empty one here) */
static __inline struct stubbs_special_attack const *stubbs_special_attacks_get(long unit_definition_index, short *count)
{ (void)unit_definition_index; *count = 0; return NULL; }
static __inline long stubbs_engine_hand_unit(short local_player_index) { (void)local_player_index; return NONE; }
static __inline boolean stubbs_engine_unit_is_thrown(long unit_index) { (void)unit_index; return FALSE; }
static __inline void stubbs_engine_special_attack_credit(long unit_index, real credit) { (void)unit_index; (void)credit; }
static __inline void stubbs_engine_hand_rides(short local_player_index, long unit_index)
{ (void)local_player_index; (void)unit_index; }
static __inline void stubbs_engine_note_unit_definition(void const *tag_instance, void const *definition,
	void const *tag_cache, unsigned long tag_data_size)
{ (void)tag_instance; (void)definition; (void)tag_cache; (void)tag_data_size; }
static __inline void stubbs_engine_player_input(short local_player_index, unsigned long *player_control_flags,
	unsigned long hand_button, unsigned long unit_control_flags)
{ (void)local_player_index; (void)player_control_flags; (void)hand_button; (void)unit_control_flags; }
static __inline boolean stubbs_engine_player_unit_died(long unit_index, long player_index)
{ (void)unit_index; (void)player_index; return FALSE; }
static __inline boolean stubbs_engine_special_attack(long unit_index, short special_attack)
{ (void)unit_index; (void)special_attack; return FALSE; }
static __inline short stubbs_engine_special_attack_get_count(long unit_index, short special_attack)
{ (void)unit_index; (void)special_attack; return 0; }
static __inline void stubbs_engine_special_attack_set_count(long unit_index, short special_attack, short count)
{ (void)unit_index; (void)special_attack; (void)count; }
static __inline void stubbs_engine_player_move_origin(long from_unit_index, long to_unit_index)
{ (void)from_unit_index; (void)to_unit_index; }
#endif

#endif
