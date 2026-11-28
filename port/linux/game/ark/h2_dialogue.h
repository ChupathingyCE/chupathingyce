/*
H2_DIALOGUE.H

port: Halo 2's AI combat dialogue (h2_dialogue.c) on a Halo 2 level: the
ai dialogue globals' vocalizations and patterns, the characters' dialogue
tags (their voices) and the rules that choose who says what, when. The
unit speech machinery stays this engine's (unit_dialogue.c: one line at a
time a unit, a queue of one, pre/post delays); the Halo 2 side decides the
line, its priority and its delays, and plays the voice's own sound.
*/

#ifndef __H2_DIALOGUE_H
#define __H2_DIALOGUE_H

#include "cseries.h"

/* a Halo 2 vocalization carried in a unit speech item's vocalization type:
this base + the globals' vocalization index (this engine's own types stay
below it) */
enum
{
	H2_DIALOGUE_VOCALIZATION_BASE = 1000
};

/* Halo 2's involuntary vocalization types (the globals' involuntary data
block, one entry each; the unit speaks its voice's line of the
vocalization the entry names) */
enum h2_involuntary_type
{
	_h2_involuntary_death = 0,
	_h2_involuntary_death_falling,
	_h2_involuntary_death_violent,
	_h2_involuntary_death_slow,
	_h2_involuntary_death_headshot,
	_h2_involuntary_death_silent,
	_h2_involuntary_death_6,
	_h2_involuntary_pain,
	_h2_involuntary_pain_medium,
	_h2_involuntary_pain_major,
	_h2_involuntary_pain_shield,
	_h2_involuntary_pain_falling,
	_h2_involuntary_flee,
	_h2_involuntary_cower,
	_h2_involuntary_fall,
	_h2_involuntary_surprise,
	_h2_involuntary_16,
	_h2_involuntary_postcombat,
	_h2_involuntary_18,
	_h2_involuntary_19,
	_h2_involuntary_panic,
	_h2_involuntary_21,
	NUMBER_OF_H2_INVOLUNTARY_TYPES
};

/* Halo 2's dialogue event types the patterns answer to (the globals'
"dialogue data" ranges; the names the H2EK gives them) */
enum h2_dialogue_type
{
	_h2_dialogue_death = 0,
	_h2_dialogue_damage = 3,
	_h2_dialogue_sighted_new = 6,
	_h2_dialogue_sighted_new_major = 7,
	_h2_dialogue_sighted_old = 9,
	_h2_dialogue_sighted_first = 10,
	_h2_dialogue_sighted_special = 11,
	_h2_dialogue_heard_new = 13,
	_h2_dialogue_found_unit = 23,
	_h2_dialogue_found_unit_presearch = 24,
	_h2_dialogue_found_unit_pursuit = 25,
	_h2_dialogue_found_unit_self_preserving = 26,
	_h2_dialogue_throwing_grenade = 28,
	_h2_dialogue_charging = 31,
	_h2_dialogue_suppressing_fire = 32,
	_h2_dialogue_grenade_uncover = 33,
	_h2_dialogue_dive = 36,
	_h2_dialogue_evade = 37,
	_h2_dialogue_avoid = 38,
	_h2_dialogue_surprised = 39,
	_h2_dialogue_presearch = 42,
	_h2_dialogue_investigate_failed = 46,
	_h2_dialogue_pursuit_failed = 48,
	_h2_dialogue_investigate_start = 49,
	_h2_dialogue_abandoned_search_space = 50,
	_h2_dialogue_abandoned_search_time = 51,
	_h2_dialogue_abandoned_search_restricted = 53,
	_h2_dialogue_investigate_pursuit_start = 54,
	_h2_dialogue_postcombat_inspect_body = 55,
	_h2_dialogue_taunt = 59,
	_h2_dialogue_retreat = 61,
	_h2_dialogue_retreat_from_scary_target = 62,
	_h2_dialogue_retreat_from_dead_leader = 63,
	_h2_dialogue_retreat_from_proximity = 64,
	_h2_dialogue_retreat_from_low_shield = 65,
	_h2_dialogue_cowering = 67,
	_h2_dialogue_pursuit_sync_quorum = 79,
	_h2_dialogue_vehicle_woohoo = 85,
	_h2_dialogue_vehicle_crazy = 87,
	_h2_dialogue_postcombat_win = 93,
	_h2_dialogue_postcombat_lose = 94,
	_h2_dialogue_postcombat_neutral = 95,
	_h2_dialogue_shoot_corpse = 96,
	_h2_dialogue_postcombat_start = 97,
	_h2_dialogue_inspect_body_start = 98,
	_h2_dialogue_postcombat_status = 99,
	_h2_dialogue_vehicle_entry_start_driver = 101,
	_h2_dialogue_vehicle_enter = 102,
	_h2_dialogue_vehicle_entry_start_gun = 103,
	_h2_dialogue_vehicle_entry_start_passenger = 104,
	_h2_dialogue_evict_driver = 106,
	_h2_dialogue_evict_gunner = 107,
	_h2_dialogue_new_order_advance = 111,
	_h2_dialogue_new_order_charge = 112,
	_h2_dialogue_new_order_fallback = 113,
	_h2_dialogue_new_order_retreat = 114,
	_h2_dialogue_new_order_moveon = 115,
	_h2_dialogue_new_order_arrival = 116,
	_h2_dialogue_new_order_entervcl = 117,
	_h2_dialogue_new_order_exitvcl = 118,
	_h2_dialogue_new_order_fllplr = 119,
	_h2_dialogue_new_order_leaveplr = 120,
	_h2_dialogue_new_order_support = 121,
	_h2_dialogue_emerge = 134,
	_h2_dialogue_curse = 138,
	_h2_dialogue_threaten = 142,
	_h2_dialogue_cover_friend = 146,
	_h2_dialogue_strike = 150,
	_h2_dialogue_gloat = 159,
	_h2_dialogue_greet = 163,
	_h2_dialogue_player_look = 168,
	_h2_dialogue_player_look_longtime = 169,
	_h2_dialogue_panic_grenade_attached = 174,
	_h2_dialogue_help_response = 179,
	_h2_dialogue_remind = 183,
	_h2_dialogue_weapon_trade_better = 188,
	_h2_dialogue_weapon_trade_worse = 189,
	_h2_dialogue_weapon_trade_equal = 190,
	_h2_dialogue_betray = 194,
	_h2_dialogue_forgive = 196,
	NUMBER_OF_H2_DIALOGUE_TYPES = 200
};

/* ---------- the level */

/* the level loaded (after its AI and sounds): the globals and the voices
read from the player's map; nothing on a level that is not Halo 2's */
void h2_dialogue_map_loaded(char const *map_name);
void h2_dialogue_dispose(void);
/* whether this level speaks Halo 2's dialogue */
boolean h2_dialogue_active(void);
/* a tick: the delays, the fleeing's yelps */
void h2_dialogue_update(void);

/* ---------- the units */

/* the voice of a unit (its model variant's dialogue tag), NONE: none - the
unit speaks as this engine has it */
short h2_dialogue_unit_voice(long unit_index);
/* the name of a voice */
char const *h2_dialogue_voice_name(short voice);

/* (ai_communication.c) one of this engine's communication events on a Halo
2 level: TRUE when Halo 2's dialogue took it (this engine's rules skip it) */
boolean h2_dialogue_communication(
	short communication_type,
	long subject_unit_index,
	long cause_unit_index,
	short hostility,
	short damage_category);
/* (unit_dialogue.c) a unit hurt or killed: its involuntary line; TRUE when
Halo 2's dialogue took it (*spoke: whether a line was spoken) */
boolean h2_dialogue_damage(
	long unit_index,
	boolean died,
	boolean died_instantly,
	short damage_category,
	long responsible_unit_index,
	boolean *spoke);
/* a Halo 2 dialogue event raised by the AI itself */
void h2_dialogue_event(
	short dialogue_type,
	long subject_unit_index,
	long cause_unit_index,
	short damage_category);
/* (h2_ai.c) a squad's order ended with a dialogue type (1 advance, 2 charge,
3 fall back, 4 retreat, 5 move on, 6 arrival, 7 enter vehicle, 8 exit
vehicle, 9 follow player, 10 leave player, 11 support): Halo 2's new order
event on the squad (one of its units) */
void h2_dialogue_squad_order(long unit_index, short ending_dialogue_type);
/* an involuntary vocalization of a unit (flee, cower, surprise, panic...) */
boolean h2_dialogue_involuntary(long unit_index, short involuntary_type);
/* (h2_ai.c h2_ai_behaviour_voice) a vocalization a Halo 2 behaviour makes
by name (the cower behaviour's "cower", retail 0x1adff0): the unit's
involuntary vocalization when the involuntary table names it (Halo 2's cower
update speaks involuntary type 13), else the named vocalization - either way
through this file's rules (priority, repeat delay, skip fraction); TRUE: spoken */
boolean h2_dialogue_behaviour_voice(long unit_index, char const *vocalization_name);

/* (unit_dialogue.c) a Halo 2 speech item's sound started on its unit: the
sound index (NONE: not heard) and the line's length */
long h2_dialogue_speech_play(long unit_index, short vocalization_type, real *seconds);
/* (unit_dialogue.c) the AI told of a line (its notification delay): the
listeners' responses */
void h2_dialogue_speech_notified(long unit_index, short vocalization_type);
/* the name of a Halo 2 vocalization carried in a speech item */
char const *h2_dialogue_vocalization_name(short vocalization_type);

#endif /* __H2_DIALOGUE_H */
