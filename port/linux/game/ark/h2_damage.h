/*
H2_DAMAGE.H

port: Halo 2's damage model on a Halo 2 level (h2_damage.c).

The level builder (h2_level.c, h2_biped.c, h2_weapons.c, h2_vehicles.c)
writes Halo 2's values into this engine's tags:
 - a player's collision model (h2_damage_player_read): Halo 2's body and
   shield vitality, stun and recharge times (its hlmt's damage info), its
   materials of the material types this file names for each species
   (h2_damage_player_material_type), their leak and multipliers none;
 - a damage effect made from a Halo 2 one (h2_damage_modifiers_write): its
   material modifiers for those material types Halo 2's damage table's
   (globals: the damage effect's damage groups by the struck material's
   armor; damage.cpp function_d5bc0).
At run time (damage.c on a Halo 2 level, h2_damage_rules) the shield's
overflow reaches the body as raw damage (Halo 2 divides it by the shield's
multiplier) and a player's body regenerates (stun, then recharge, to the
recharge fraction; damage.cpp function_d5de0).
*/

#ifndef __H2_DAMAGE_H
#define __H2_DAMAGE_H

#include "cseries.h"

struct h2_cache;

/* ---------- constants */

enum
{
	_h2_damage_spartan = 0,
	_h2_damage_elite,
	NUMBER_OF_H2_DAMAGE_SPECIES,

	_h2_damage_body = 0,
	_h2_damage_shield,
	NUMBER_OF_H2_DAMAGE_LAYERS,

	/* a damage effect made from Halo 2's keeps its riders' scales (its
	jpt!'s rider direct, maximum and minimum transfer damage scales) in its
	material modifiers past the material types (unused): this magic, then
	the three */
	H2_DAMAGE_RIDER_SLOT = 36,
	/* (past this engine's material types, its table's multiplier on Halo 2's
	metal: a held prop's section, ragdolls_props.c. Thick and thick_hum,
	thin and thin_hum are the same in every damage effect of the table) */
	H2_DAMAGE_THICK_METAL_SLOT = 33,
	H2_DAMAGE_THIN_METAL_SLOT = 34,
	/* a vehicle seat Halo 2's damage info has (its damage seats): unused3[0]
	this, then its direct scale, fall-off radius, maximum and minimum
	transfer scales (reals) */
	H2_DAMAGE_SEAT_MAGIC = 0x48325344UL,
	/* (a seat of the vehicle's child object in Halo 2: its turret's) */
	H2_DAMAGE_SEAT_MAGIC_CHILD = 0x48325354UL,
};

#define H2_DAMAGE_RIDER_MAGIC (-7331.0f)

/* ---------- structures */

/* a Halo 2 model's damage info (its hlmt's "new damage info") */
struct h2_damage_info
{
	boolean found;
	float body_vitality;
	float body_minimum_stun_damage;
	float body_stun_time;
	float body_recharge_time;
	float body_recharge_fraction;
	float shield_vitality;
	float shield_minimum_stun_damage;
	float shield_stun_time;
	float shield_recharge_time;
	unsigned long body_material;    /* string ids: its global materials */
	unsigned long shield_material;
};

/* ---------- prototypes (the builder) */

/* the bipd's name for a species (objects\characters\...\..._mp) */
char const *h2_damage_species_biped(short species);
/* the species of a converted biped's name ("spartan", "elite"); NONE */
short h2_damage_species_named(char const *name);
/* the material type this engine's tags give a species' body or shield */
short h2_damage_player_material_type(short species, short layer);

/* whether a material (its string id) of a Halo 2 object's model is of a damage
section that ignores shields */
boolean h2_damage_material_ignores_shields(struct h2_cache *cache, long object_tag, unsigned long material_name);

/* the flags of the damage section of a material (its string id) of a Halo 2
object's model (0x80 headshotable, 0x100 ignores shields), *section its
index; FALSE when the model has no such material or it no section */
boolean h2_damage_material_section_flags(struct h2_cache *cache, long object_tag, unsigned long material_name, short *section,
	unsigned long *flags);

/* a Halo 2 object's (bipd, vehi...: its model at +0x34) damage info */
boolean h2_damage_info_read(struct h2_cache *cache, long object_tag, struct h2_damage_info *info);

/* Halo 2's damage table's multiplier for a damage effect (jpt!) on a global
material (its armor) */
float h2_damage_multiplier(struct h2_cache *cache, long damage_tag, unsigned long global_material);

/* a damage effect's material modifiers (this engine's damage effect's
material_modifiers, NUMBER_OF_MATERIAL_TYPES floats at modifiers) for the
players' material types made Halo 2's, damage_tag Halo 2's damage effect;
writef writes a float into the tag. FALSE: none (no damage effect, no
table) */
boolean h2_damage_modifiers_write(struct h2_cache *cache, long damage_tag, void *modifiers,
	void (*write_real)(void *at, float value));

/* a damage effect of this engine's (its tag's bytes at definition) made
Halo 2's damage_tag's: its damage (minimum Halo 2's lower bound - the
damage at no scale -, lower and upper bound its upper bound's range) and
its material modifiers
(h2_damage_modifiers_write). FALSE: not (no damage effect) */
boolean h2_damage_effect_write(struct h2_cache *cache, long damage_tag, void *definition,
	void (*write_real)(void *at, float value));

/* (the game) a damage effect's Halo 2 multiplier on a prop's thick or thin
metal; 1: not made from a Halo 2 one */
real h2_damage_metal_multiplier(long definition_index, boolean thin);

/* the armor class (0..23) of Halo 2's global material (a string id) that a
character's collision material takes (h2_damage.c); NONE: none */
short h2_damage_armor_class(struct h2_cache *cache, unsigned long global_material);
/* the material type its hit effects take (the Hunter's armor, skin); NONE: the stock model's */
short h2_damage_class_effect_type(short armor_class);

/* the material type a converted vehicle's collision materials take (by its
netgame name's beginning: "warthog", "ghost", "h_turret_ap"), its armor's
in the damage effects h2_damage_modifiers_write writes; NONE: none */
short h2_damage_vehicle_material_type(char const *name);

/* ---------- prototypes (the game) */

/* whether a Halo 2 level is being played (its rules apply) */
boolean h2_damage_rules(void);
/* (damage.c) an object's body took damage (stuns its regeneration) */
void h2_damage_body_damaged(long object_index, real damage);
/* (damage.c, each tick) an object's body regenerates */
void h2_damage_body_update(long object_index);
/* (damage.c) Halo 2's riders (damage.cpp function_dc230, function_db210):
a rider hit takes its seat's direct scale times the damage effect's rider
direct scale (1: no Halo 2 seat entry, a gunner, or not a Halo 2 level); a
vehicle hit (not by an explosion) passes its damage to each rider with a
seat entry, times its seat's transfer scale (the maximum within its fall-off
radius of the damage's origin, else the minimum) times the effect's (0:
none passed). h2_damage_transferring brackets the passing on */
real h2_damage_rider_direct_scale(long definition_index, long rider_index);
union real_point3d;
real h2_damage_rider_transfer_scale(long definition_index, long rider_index, union real_point3d const *origin);
void h2_damage_transferring(boolean transferring);

/* (damage.c, HALO_H2_DAMAGE_LOG) one damage's log line */
void h2_damage_log(long object_index, long definition_index, real shield_before, real body_before);

/* (damage.c, a Halo 2 level's vehicle whose body would run out) Halo 2's
riders rule (damage.cpp function_d8cb0): a vehicle with a living rider -
in its seats or its turret's - does not die; its body held at none, it
dies when its last rider dies in it, and is whole again to a quarter
when its last rider gets out alive (function_d6800, function_db670;
h2_damage_body_update watches its riders). TRUE: held.
HALO_H2_VEHICLE_DIES_WITH_RIDERS: none of it. */
boolean h2_damage_vehicle_hold(long vehicle_index);

/* (damage.c) whether a rider's seat is its vehicle's child object's in
Halo 2 (the Warthog's gunner's: the chaingun's) */
boolean h2_damage_rider_child_seat(long rider_index);

#endif
