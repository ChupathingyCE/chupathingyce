/*
DUAL_WIELD.H

port: Halo 2's dual wielding (port/linux/game/dual_wield.c). A unit
holding a weapon that can be dual wielded (_weapon_can_be_dual_wielded_bit)
can hold a second one in its left hand: the left trigger (the grenade
button) fires it, grenades are not thrown, the weapon switch drops it.
*/

#ifndef __DUAL_WIELD_H
#define __DUAL_WIELD_H

/* ---------- prototypes/DUAL_WIELD.C */

/* (first_person_weapons.c) a new map: no unit holds a left weapon */
void dual_wield_reset(void);

/* the weapon in the unit's left hand (NONE: none) */
long dual_wield_left_weapon(long unit_index);

/* whether the weapon can be held as either of a pair */
boolean dual_wield_weapon_pairs(long weapon_index);

/* whether the weapon is one of a pair a unit holds (the left one, or the
right one of a unit with a left one): Halo 2's barrels are dual wielded
then (error, damage scale) */
boolean dual_wield_weapon_is_dual(long weapon_index);

/* (items/weapons.c, a weapon's shot) a local player's hand fired (Halo 2's
first-person fire states, unknown_0a76b0.cpp function_ee680: unit +0x1fa
the right hand's, +0x1fb the left's, 0.3 seconds) */
void dual_wield_note_fired(long weapon_index);

/* how dual wielded a weapon's barrel is, 0 (its single error, damage) to 1
(its dual ones): Halo 2's function_d1490 - the other hand's last shot's 0.3
seconds running out; with Halo 2's weapon flag 27 ("uses the old dual fire
error code", h2_flags) 1 when dual wielded, 0 when not */
real dual_wield_error_scale(long weapon_index, unsigned long h2_flags);

/* whether the unit could take the weapon (on the ground) in its left hand */
boolean dual_wield_can_pick_up(long unit_index, long weapon_index);

/* the weapon (on the ground) taken in the unit's left hand: TRUE if it was */
boolean dual_wield_pick_up(long unit_index, long weapon_index);

/* (the menus' preview: a character that holds a pair) the weapon, made by
the caller for the unit, put in its left hand with none of the pick-up's
rules: TRUE if it was */
boolean dual_wield_give_left(long unit_index, long weapon_index);

/* the unit's left weapon dropped (thrown down as a dropped weapon) */
void dual_wield_drop_left(long unit_index);

/* the unit's left weapon deleted */
void dual_wield_delete_left(long unit_index);

/* (units.c, after the weapon in its right hand is told its controls) the
left weapon's controls: its trigger the unit's grenade trigger */
void dual_wield_unit_update(long unit_index);
/* (perf/ce-floor-2) nonzero once a left weapon was ever given (dual_wield.c):
until then dual_wield_unit_update has no unit to update (unit_update asks) */
extern int dual_wield_units_ever;

#ifndef DUAL_WIELD_C
/* (perf/ce-floor-3) dual_wield_left_weapon's answer until a left weapon was
ever given - every unit's entry NONE: NONE - without the call (~85 asks a
tick in Halo CE's fights: unit updates, animation labels, weapon state) */
static __inline__ long dual_wield_left_weapon_fast(long unit_index)
{
	return dual_wield_units_ever ? (dual_wield_left_weapon)(unit_index) : NONE;
}
#define dual_wield_left_weapon(unit_index) dual_wield_left_weapon_fast(unit_index)
#endif

/* (network_objects.c, a client) the unit's left weapon made the host's
word on it: the weapon (NONE: none) attached without the rules */
void dual_wield_network_set(long unit_index, long weapon_index);

/* (players.c) the weapon the player could take in its left hand this tick
(player_examine_nearby_item), and the one it could */
void dual_wield_set_candidate(long player_index, long weapon_index);
long dual_wield_candidate(long player_index);

/* (player_control.c) the weapon switch button held `ticks` ticks (0 when
up): whether it switches weapons this tick (*rotate) or takes the weapon
nearby in the left hand (*pick_up). Held near a weapon that can be taken,
the switch waits for the button's release */
void dual_wield_switch_button(long player_index, short local_player_index, long ticks, long hold_ticks,
	boolean *rotate, boolean *pick_up);

#endif
