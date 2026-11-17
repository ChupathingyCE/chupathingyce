/* MCC's skulls on MCC maps (port/linux/game/mcc_skulls.c).

MCC Halo CE keeps every skull as an engine global a script can read and set,
debug_ice_cream_flavor_status_<skull> (hs_doc of the MCC HEK; the Firefight
CE mod sets eight of them from its own settings menu and resets the ones the
MCC skull menu may have turned on). This file gives an MCC map the same 29
globals plus debug_ice_cream_flavor_force_coop, appended to the engine's
external global table, and the gameplay each skull is documented to change
(Halopedia's CEA:Skulls and MCC:Skulls, the MCC skull menu's own text).

Nothing here touches Xbox or Custom Edition maps: a skull is only active
while an MCC map is loaded (mcc_skull_active), and every hook in the shared
code asks that first. */
#ifndef MCC_SKULLS_H
#define MCC_SKULLS_H
#include "cseries.h"
#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif

/* In the MCC HEK's order (hs_doc), which is also the table's. */
enum mcc_skull {
    MCC_SKULL_GRUNT_BIRTHDAY_PARTY, MCC_SKULL_ASSASSINS, MCC_SKULL_FAMINE,
    MCC_SKULL_I_WOULD_HAVE_BEEN_YOUR_DADDY, MCC_SKULL_BLIND, MCC_SKULL_GHOST,
    MCC_SKULL_BLACK_EYE, MCC_SKULL_CATCH, MCC_SKULL_SPUTNIK, MCC_SKULL_MYTHIC,
    MCC_SKULL_ANGER, MCC_SKULL_THUNDERSTORM, MCC_SKULL_IRON,
    MCC_SKULL_THATS_JUST_WRONG, MCC_SKULL_BANDANNA, MCC_SKULL_BOOM,
    MCC_SKULL_EYE_PATCH, MCC_SKULL_FOG, MCC_SKULL_FOREIGN, MCC_SKULL_GRUNT_FUNERAL,
    MCC_SKULL_PINATA, MCC_SKULL_RECESSION, MCC_SKULL_MALFUNCTION,
    MCC_SKULL_STREAKING, MCC_SKULL_STYX, MCC_SKULL_FEATHER, MCC_SKULL_BONDED_PAIR,
    MCC_SKULL_MASTERBLASTER, MCC_SKULL_FORCE_COOP,
    MCC_SKULL_COUNT
};
#define MCC_SKULL_MASK 0x1fffffffu

/* The globals themselves, mcc_skull_definitions[MCC_SKULL_COUNT], are
declared where the engine's table lists them (source/hs/hs_globals_external.c). */
/* The skull's short name, "black_eye". */
char const *mcc_skull_name(short skull);
/* TRUE for a skull global's name (mcc_campaign_global_settable). */
boolean mcc_skull_global(char const *name);

/* Level start and end (mcc_campaign_begin, mcc_campaign_dispose): every
skull off, then the ones game.mcc_skulls names on, as MCC's menu would. */
void mcc_skulls_begin(void);
void mcc_skulls_dispose(void);
/* The checkpoint and network state (mcc_campaign_snapshot/restore). */
unsigned long mcc_skulls_snapshot(void);
void mcc_skulls_restore(unsigned long mask);

/* TRUE while an MCC map is loaded and the skull is on. */
boolean mcc_skull_active(short skull);

/* ---- the skulls' effects, asked from the shared code */

/* Mythic, Anger, Catch: what an enemy team's difficulty value is multiplied
by (game_difficulty_get_team_value). */
real mcc_skulls_difficulty_scale(short value_type);
/* That's Just... Wrong: what an enemy actor's vision and hearing distances
are multiplied by (actor_perception.c). */
real mcc_skulls_perception_scale(short team_index);
/* Boom: what an explosion's radii are multiplied by (damage.c). */
real mcc_skulls_blast_scale(void);
/* Sputnik: what a damage effect's impulse is multiplied by (damage.c). */
real mcc_skulls_impulse_scale(void);
/* Catch: how many grenades a placed enemy actor carries instead (actors.c). */
short mcc_skulls_actor_grenades(short team_index, short count);
/* Assassins: TRUE if a placed enemy actor is cloaked (actors.c). */
boolean mcc_skulls_actor_cloaked(short team_index);
/* Famine: halves the ammunition of an AI unit's weapons as it dies, before
they drop (unit_died). */
void mcc_skulls_unit_dropping_weapons(long unit_index);
/* Black Eye: a player's melee hit on a living enemy unit recharges the
player's shields (object_cause_damage); and whether the player's unit may
recharge its shields on its own at all (objects_update_shields' caller). */
void mcc_skulls_melee_hit(long attacker_index, long victim_index);
boolean mcc_skulls_shields_recharge(long object_index);
/* Recession: how many rounds (or how much battery) one of a player's shots
costs instead of one (weapons.c). */
short mcc_skulls_rounds_per_shot(long weapon_index, short rounds);
/* Iron: TRUE if the dead solo player's level restarts instead of reverting
to the last checkpoint (main_lost_map_private). */
boolean mcc_skulls_restart_on_death(void);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_skull_active(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_skulls_difficulty_scale(a0) ((void)sizeof(a0), (real)1)
#define mcc_skulls_perception_scale(a0) ((void)sizeof(a0), (real)1)
#define mcc_skulls_blast_scale() ((real)1)
#define mcc_skulls_impulse_scale() ((real)1)
#define mcc_skulls_actor_grenades(a0, a1) ((void)sizeof(a0), (short)(a1))
#define mcc_skulls_actor_cloaked(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_skulls_unit_dropping_weapons(a0) ((void)sizeof(a0), (void)0)
#define mcc_skulls_melee_hit(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (void)0)
#define mcc_skulls_shields_recharge(a0) ((void)sizeof(a0), (boolean)1)
#define mcc_skulls_rounds_per_shot(a0, a1) ((void)sizeof(a0), (short)(a1))
#define mcc_skulls_restart_on_death() ((boolean)0)
#endif

#endif
