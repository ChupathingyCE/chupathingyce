/*
H2_WEAPON_FIELDS.H

port: the fields of a Halo 2 weapon (port/linux/game/h2_weapons.c reads
them from the player's own MCC maps) that this engine's weapon tags have
no place for and items/weapons.c uses: the barrel's error and damage
scale when dual wielded, and the recoil (the view's angle change a shot).
Only a Halo 2 level's made weapons have them (NULL: none, as every
Halo CE level's).
*/

#ifndef __H2_WEAPON_FIELDS_H
#define __H2_WEAPON_FIELDS_H

struct h2_weapon_fields
{
	long definition_index;
	/* dual wielded (both hands' weapons): the error a tick it rises and falls
	(Halo 2's barrel @48 and @4C), its minimum (@58) and angles at error 0 and 1
	(@5C, @60), the damage scale (@64) */
	float dual_error_rise, dual_error_fall;
	float dual_error_minimum, dual_error_lower, dual_error_upper;
	float dual_damage_scale;
	/* recoil (@B0..@C0): the view's pitch a shot, radians, at its least and
	most as the recoil fraction goes 0 to 1 (a tick it rises while firing and
	falls after), the curve (Halo 2's function: 0 linear, 1 early, 2 very
	early, 3 late, 4 very late, 5 cosine) */
	float recoil_lower, recoil_upper;
	float recoil_rise, recoil_fall;
	short recoil_function;
	/* heat lost a second while overheated (weap @168; 0: the weapon's
	heat loss, as this engine's) - the plasma rifle's and the Sentinel
	Beam's 0.35 against their 0.85 */
	float overheated_heat_loss;
	/* Halo 2's weapon flags (weap @12C: 27 "uses the old dual fire error
	code", dual wielding all or nothing - units/dual_wield.h) */
	unsigned long h2_flags;
	/* the barrel's fire recovery time, seconds (@20), and whether its shots are paced by rounds per second (@4) rather
	than by that recovery alone: Halo 2's barrel goes back to idle that long after its trigger is let go (weapons.cpp
	0x103e60 / 0x103f60 / 0x103dd0), the rounds per second pace only a held trigger */
	float fire_recovery_time;
	boolean paced_by_rounds_per_second;
};

/* (port/linux/game/h2_weapons.c) a weapon tag's fields; NULL if it has none
or no Halo 2 level is being played */
struct h2_weapon_fields const *h2_weapon_fields_get(long definition_index);

/* (cache_files.c, as a map's tags load) the loaded Halo 2 level's weapons'
fields read from its MCC map (port/linux/game/h2_weapons.c); without it
(HALO_H2_WEAPON_FIELDS_READER=0) only a level made in this session has them */
void h2_weapon_fields_map_loaded(char const *map_name);

#endif
