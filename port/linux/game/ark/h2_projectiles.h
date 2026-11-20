/*
H2_PROJECTILES.H

port: Halo 2's projectile rules and the rocket launcher's lock-on on a
Halo 2 level (h2_projectiles.c). The level builder (h2_weapons.c,
h2_vehicles.c) marks what it makes from Halo 2's tags: a projectile's
object definition carries struct h2_projectile_port in its unused words, a
vehicle's struct h2_lock_port, a weapon trigger its port_tracking_tag
(items/weapon_definitions.h). Nothing else carries them: a Halo CE map's
projectiles, vehicles and weapons behave as they always did.

Projectiles (items/projectiles.c, the upstream decompilation's
projectiles.cpp the rules): no speed falloff (Halo 2's air damage range is
a distance, not a deceleration); a speed that follows its acceleration
range (the rocket: 8 to 20 world units a second over its first 20); damage
scaled by the distance flown when "damage scales based on distance" (the
shotgun: full at the muzzle, its lower bound at 8 world units -
function_faa60); a guided angular velocity picked between its bounds for
each projectile (function_f8200); the super-combine at the tag's count of
stuck projectiles, the parent then taking the attached super detonation
damage (function_fc330).

Lock-on (items/weapons.c, weapons.cpp's trigger behaviour 5, the
rocket launcher's "latch-rocketlauncher"): a pull with a lockable target in
the reticle starts locking (0.5 s) instead of firing; held on the same
target the lock is made (function_103bd0) and kept while the target stays;
released while locked the rocket is fired at it (its locked barrel), while
locking a rocket is fired unguided; a lost target ends the lock and its
release fires nothing. Lockable (Halo 2's model targets): a vehicle whose
hlmt has a target "locked by human tracking", within its lock-on distance
and the weapon's autoaim angle, in sight. The plasma pistol's tracking
(plasma tracking) follows bipeds only (their targets are "locked by plasma
tracking", the vehicles' are not).
*/

#ifndef __H2_PROJECTILES_H
#define __H2_PROJECTILES_H

#include "cseries.h"

/* ---------- constants */

enum
{
	H2_PROJECTILE_PORT_TAG = 'h2pj',
	H2_LOCK_PORT_TAG = 'h2lk',
	H2_TRACKING_PORT_TAG = 'h2tr',
	/* (the words of an object definition's unused2 the port data starts at) */
	H2_OBJECT_PORT_WORD = 2,
};

enum
{
	_h2_projectile_damage_scales_with_distance_bit = 0,
	_h2_projectile_accelerates_bit,
	_h2_projectile_super_combines_bit,
	/* (Halo 2's "travels instantaneously", its flag 5: the battle rifle's,
	magnum's, sniper rifle's, carbine's, beam rifle's rounds - moved one
	tick's way the moment they are fired, weapons.c) */
	_h2_projectile_travels_instantaneously_bit,
};

enum
{
	_h2_tracking_none = 0,
	_h2_tracking_human,
	_h2_tracking_plasma,
};

enum
{
	_h2_lock_human_bit = 0,
	_h2_lock_plasma_bit,
};

/* ---------- structures */

/* a projectile's (in its object definition's unused2, from
H2_OBJECT_PORT_WORD) */
struct h2_projectile_port
{
	unsigned long tag;          /* H2_PROJECTILE_PORT_TAG */
	unsigned long flags;
	real guided_angular_velocity[2];   /* radians a second, lower and upper */
	real acceleration_range[2];        /* world units flown */
	real damage_range;          /* (scales with distance) no damage scale left at this distance */
	short super_detonation_count;
	short pad;
	long attached_super_detonation_damage;   /* its jpt! (NONE: none) */
};

/* a vehicle's (the same words) */
struct h2_lock_port
{
	unsigned long tag;          /* H2_LOCK_PORT_TAG */
	unsigned long flags;        /* _h2_lock_... */
	real distance;              /* its lock-on distance */
	real size;                  /* its target's size */
};

/* ---------- prototypes (the game) */

struct projectile_definition;
struct weapon_trigger_definition;

/* a projectile definition's Halo 2 data (NULL: none, a Halo CE one) */
struct h2_projectile_port const *h2_projectile_port(struct projectile_definition const *definition);
/* its guided angular velocity for one projectile */
real h2_projectile_guided_angular_velocity(struct h2_projectile_port const *port, long projectile_index);

/* (weapons.c) a trigger's Halo 2 tracking (_h2_tracking_...) */
short h2_trigger_tracking(struct weapon_trigger_definition const *trigger);
/* (weapons.c, each tick, a player's weapon's lock-on trigger) the trigger as
this engine's trigger code is to see it: down only on the tick a shot is to
be fired */
boolean h2_lock_on_trigger_update(long weapon_index, short trigger_index, boolean down);
/* (weapons.c, a shot) the target a projectile fired now follows: the
locked target (human tracking), the aim's target if a biped (plasma
tracking), else the aim's */
long h2_tracking_target(long weapon_index, struct weapon_trigger_definition const *trigger, long aim_target_index);

/* (h2_hud.c) a weapon's lock: TRUE if it has a lock-on trigger; its state
(lockable: a target is in the reticle, locking - fraction done -, locked)
and the target */
boolean h2_lock_on_hud(long weapon_index, boolean *lockable, boolean *locking, boolean *locked, real *fraction, long *target_index);

#endif
