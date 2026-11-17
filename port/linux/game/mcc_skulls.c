/* MCC's skulls on MCC maps (mcc_skulls.h).

What MCC Halo CE does, from its HEK's hs_doc, the Firefight CE mod's
documentation and Halopedia's skull pages (CEA:Skulls, MCC:Skulls):
- every skull is a boolean engine global, debug_ice_cream_flavor_status_<name>
  (and debug_ice_cream_flavor_force_coop), which a map's scripts and the
  console read and set; the skull menu sets them before the level starts;
- the skulls' effects, as their menu text documents them, which is what the
  hooks in the shared code implement here. Where MCC's exact number isn't
  documented (Anger's rate, Catch's chance, That's Just... Wrong's reach)
  the factor is this port's; the direction is MCC's.

Nothing applies outside an MCC map: mcc_skull_active is FALSE on Xbox and
Custom Edition maps whatever the globals hold, so stock Halo is unchanged. */
#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h, above): a console builds this to nothing */
#include "mcc_skulls.h"
#include "mcc_cache.h"
#include "errors.h"
#include "hs/hs.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "items/weapons.h"
#include "objects/objects.h"
#include "units/units.h"
#include <string.h>

char const *config_string(char const *name);

static char const *const mcc_skull_names[MCC_SKULL_COUNT] = {
    "grunt_birthday_party", "assassins", "famine", "i_would_have_been_your_daddy",
    "blind", "ghost", "black_eye", "catch", "sputnik", "mythic", "anger",
    "thunderstorm", "iron", "thats_just_wrong", "bandanna", "boom", "eye_patch",
    "fog", "foreign", "grunt_funeral", "pinata", "recession", "malfunction",
    "streaking", "styx", "feather", "bonded_pair", "masterblaster", "force_coop"
};

/* The globals' storage: what the host's scripts set, or what the host sent. */
static boolean mcc_skull_values[MCC_SKULL_COUNT];
static boolean mcc_skulls_owned;

#define MCC_SKULL_GLOBAL(index, name) \
    { "debug_ice_cream_flavor_status_" name, _hs_type_boolean, 0, &mcc_skull_values[index] }
struct hs_external_global_definition mcc_skull_definitions[MCC_SKULL_COUNT] = {
    MCC_SKULL_GLOBAL(MCC_SKULL_GRUNT_BIRTHDAY_PARTY, "grunt_birthday_party"),
    MCC_SKULL_GLOBAL(MCC_SKULL_ASSASSINS, "assassins"),
    MCC_SKULL_GLOBAL(MCC_SKULL_FAMINE, "famine"),
    MCC_SKULL_GLOBAL(MCC_SKULL_I_WOULD_HAVE_BEEN_YOUR_DADDY, "i_would_have_been_your_daddy"),
    MCC_SKULL_GLOBAL(MCC_SKULL_BLIND, "blind"),
    MCC_SKULL_GLOBAL(MCC_SKULL_GHOST, "ghost"),
    MCC_SKULL_GLOBAL(MCC_SKULL_BLACK_EYE, "black_eye"),
    MCC_SKULL_GLOBAL(MCC_SKULL_CATCH, "catch"),
    MCC_SKULL_GLOBAL(MCC_SKULL_SPUTNIK, "sputnik"),
    MCC_SKULL_GLOBAL(MCC_SKULL_MYTHIC, "mythic"),
    MCC_SKULL_GLOBAL(MCC_SKULL_ANGER, "anger"),
    MCC_SKULL_GLOBAL(MCC_SKULL_THUNDERSTORM, "thunderstorm"),
    MCC_SKULL_GLOBAL(MCC_SKULL_IRON, "iron"),
    MCC_SKULL_GLOBAL(MCC_SKULL_THATS_JUST_WRONG, "thats_just_wrong"),
    MCC_SKULL_GLOBAL(MCC_SKULL_BANDANNA, "bandanna"),
    MCC_SKULL_GLOBAL(MCC_SKULL_BOOM, "boom"),
    MCC_SKULL_GLOBAL(MCC_SKULL_EYE_PATCH, "eye_patch"),
    MCC_SKULL_GLOBAL(MCC_SKULL_FOG, "fog"),
    MCC_SKULL_GLOBAL(MCC_SKULL_FOREIGN, "foreign"),
    MCC_SKULL_GLOBAL(MCC_SKULL_GRUNT_FUNERAL, "grunt_funeral"),
    MCC_SKULL_GLOBAL(MCC_SKULL_PINATA, "pinata"),
    MCC_SKULL_GLOBAL(MCC_SKULL_RECESSION, "recession"),
    MCC_SKULL_GLOBAL(MCC_SKULL_MALFUNCTION, "malfunction"),
    MCC_SKULL_GLOBAL(MCC_SKULL_STREAKING, "streaking"),
    MCC_SKULL_GLOBAL(MCC_SKULL_STYX, "styx"),
    MCC_SKULL_GLOBAL(MCC_SKULL_FEATHER, "feather"),
    MCC_SKULL_GLOBAL(MCC_SKULL_BONDED_PAIR, "bonded_pair"),
    MCC_SKULL_GLOBAL(MCC_SKULL_MASTERBLASTER, "masterblaster"),
    /* (the one global without "status_" in its name) */
    { "debug_ice_cream_flavor_force_coop", _hs_type_boolean, 0, &mcc_skull_values[MCC_SKULL_FORCE_COOP] }
};

char const *mcc_skull_name(short skull)
{
    return skull >= 0 && skull < MCC_SKULL_COUNT ? mcc_skull_names[skull] : "";
}

boolean mcc_skull_global(char const *name)
{
    short i;
    if (!name) return FALSE;
    for (i = 0; i < MCC_SKULL_COUNT; i++)
        if (!csstrcasecmp(name, mcc_skull_definitions[i].name)) return TRUE;
    return FALSE;
}

static short mcc_skull_from_word(char const *word, size_t length)
{
    short i;
    for (i = 0; i < MCC_SKULL_COUNT; i++) {
        if (strlen(mcc_skull_names[i]) == length && !_strnicmp(word, mcc_skull_names[i], length))
            return i;
    }
    /* the menu's spellings too */
    if (length == 6 && !_strnicmp(word, "iwhbyd", 6)) return MCC_SKULL_I_WOULD_HAVE_BEEN_YOUR_DADDY;
    if (length == 7 && !_strnicmp(word, "bandana", 7)) return MCC_SKULL_BANDANNA;
    if (length == 6 && !_strnicmp(word, "pi\xc3\xb1" "ata", 6)) return MCC_SKULL_PINATA;
    return NONE;
}

/* game.mcc_skulls: the skulls MCC's menu would have on, "fog, black_eye". */
static void mcc_skulls_from_config(void)
{
    char const *text = config_string("game.mcc_skulls");
    char line[256];
    size_t at = 0, used = 0;
    if (!text || !text[0]) return;
    while (text[at]) {
        size_t start;
        while (text[at] == ' ' || text[at] == ',' || text[at] == ';' || text[at] == '\t') at++;
        start = at;
        while (text[at] && text[at] != ' ' && text[at] != ',' && text[at] != ';' && text[at] != '\t') at++;
        if (at > start) {
            short skull = mcc_skull_from_word(text + start, at - start);
            if (skull == NONE) {
                error(_error_log, "mcc: game.mcc_skulls names no skull: %.*s", (int)(at - start), text + start);
                continue;
            }
            mcc_skull_values[skull] = TRUE;
            if (used < sizeof(line) - 2) used += (size_t)snprintf(line + used, sizeof(line) - used, "%s%s",
                used ? ", " : "", mcc_skull_names[skull]);
        }
    }
    if (used) error(_error_log, "mcc: skulls on from game.mcc_skulls: %s", line);
}

void mcc_skulls_begin(void)
{
    memset(mcc_skull_values, 0, sizeof(mcc_skull_values));
    mcc_skulls_owned = TRUE;
    mcc_skulls_from_config();
}

void mcc_skulls_dispose(void)
{
    memset(mcc_skull_values, 0, sizeof(mcc_skull_values));
    mcc_skulls_owned = FALSE;
}

unsigned long mcc_skulls_snapshot(void)
{
    unsigned long mask = 0;
    short i;
    if (!mcc_skulls_owned) return 0;
    for (i = 0; i < MCC_SKULL_COUNT; i++) if (mcc_skull_values[i]) mask |= 1u << i;
    return mask;
}

void mcc_skulls_restore(unsigned long mask)
{
    short i;
    if (!mcc_skulls_owned || (mask & ~MCC_SKULL_MASK)) return;
    for (i = 0; i < MCC_SKULL_COUNT; i++) mcc_skull_values[i] = (mask & (1u << i)) != 0;
}

boolean mcc_skull_active(short skull)
{
    return skull >= 0 && skull < MCC_SKULL_COUNT && mcc_skulls_owned &&
        mcc_cache_tags_loaded() && mcc_skull_values[skull];
}

/* ---- effects */

static boolean mcc_skulls_enemy_team(short team_index)
{
    return team_index != NONE && game_team_is_enemy(_game_team_player, team_index);
}

real mcc_skulls_difficulty_scale(short value_type)
{
    real scale = 1.0f;
    if (!mcc_skulls_owned || !mcc_cache_tags_loaded()) return scale;
    switch (value_type) {
    /* Mythic: "Enemies have increased health"; CEA doubled Covenant
     * infantry's health, shields with it. */
    case _game_difficulty_value_enemy_vitality:
    case _game_difficulty_value_enemy_shield:
        if (mcc_skull_values[MCC_SKULL_MYTHIC]) scale *= 2.0f;
        break;
    /* Anger: "Enemies fire their weapons faster and more frequently". */
    case _game_difficulty_value_rate_of_fire:
        if (mcc_skull_values[MCC_SKULL_ANGER]) scale *= 2.0f;
        break;
    case _game_difficulty_value_burst_separation:
    case _game_difficulty_value_new_target_delay:
        if (mcc_skull_values[MCC_SKULL_ANGER]) scale *= 0.5f;
        break;
    /* Catch: "Enemies throw and drop more grenades": the throw chance up,
     * the wait between an encounter's throws down. */
    case _game_difficulty_value_grenade_chance_scale:
        if (mcc_skull_values[MCC_SKULL_CATCH]) scale *= 3.0f;
        break;
    case _game_difficulty_value_grenade_timer_scale:
        if (mcc_skull_values[MCC_SKULL_CATCH]) scale *= 0.5f;
        break;
    default:
        break;
    }
    return scale;
}

real mcc_skulls_perception_scale(short team_index)
{
    /* That's Just... Wrong: "Increases enemy awareness of player". */
    return mcc_skull_active(MCC_SKULL_THATS_JUST_WRONG) && mcc_skulls_enemy_team(team_index) ? 2.0f : 1.0f;
}

real mcc_skulls_blast_scale(void)
{
    /* Boom: "Explosions have twice the damage radius". */
    return mcc_skull_active(MCC_SKULL_BOOM) ? 2.0f : 1.0f;
}

real mcc_skulls_impulse_scale(void)
{
    /* Sputnik: "Mass of objects is decreased, making them more easily
     * displaced": what a damage effect's impulse does to them, doubled. */
    return mcc_skull_active(MCC_SKULL_SPUTNIK) ? 2.0f : 1.0f;
}

short mcc_skulls_actor_grenades(short team_index, short count)
{
    /* Catch: an enemy carries (and so drops) twice its grenades, two at least. */
    if (!mcc_skull_active(MCC_SKULL_CATCH) || !mcc_skulls_enemy_team(team_index)) return count;
    return (short)MAX(count * 2, 2);
}

boolean mcc_skulls_actor_cloaked(short team_index)
{
    /* Assassins: "All enemies in the game are permanently cloaked". */
    return mcc_skull_active(MCC_SKULL_ASSASSINS) && mcc_skulls_enemy_team(team_index);
}

void mcc_skulls_unit_dropping_weapons(long unit_index)
{
    /* Famine: "Weapons dropped by AI have half the ammo they normally would". */
    struct unit_datum *unit = unit_try_and_get(unit_index);
    short slot, i;
    if (!unit || unit->unit.player_index != NONE || !mcc_skull_active(MCC_SKULL_FAMINE)) return;
    for (slot = 0; slot < (short)NUMBEROF(unit->unit.weapon_object_indices); slot++) {
        struct weapon_datum *weapon = (struct weapon_datum *)
            object_try_and_get_and_verify_type(unit->unit.weapon_object_indices[slot], _object_mask_weapon);
        if (!weapon) continue;
        for (i = 0; i < (short)NUMBEROF(weapon->weapon.magazines); i++) {
            weapon->weapon.magazines[i].rounds_total /= 2;
            weapon->weapon.magazines[i].rounds_loaded /= 2;
        }
        /* a battery's charge is 1 - age */
        if (weapon->weapon.age < 1.0f) weapon->weapon.age = 1.0f - (1.0f - weapon->weapon.age) * 0.5f;
    }
}

boolean mcc_skulls_shields_recharge(long object_index)
{
    /* Black Eye: "Your shields only recharge when you melee enemies". */
    struct unit_datum *unit;
    if (!mcc_skull_active(MCC_SKULL_BLACK_EYE)) return TRUE;
    unit = unit_try_and_get(object_index);
    return !unit || unit->unit.player_index == NONE;
}

void mcc_skulls_melee_hit(long attacker_index, long victim_index)
{
    struct unit_datum *attacker;
    struct object_datum *victim;
    if (!mcc_skull_active(MCC_SKULL_BLACK_EYE)) return;
    attacker = unit_try_and_get(attacker_index);
    victim = object_try_and_get(victim_index);
    if (!attacker || !victim || attacker->unit.player_index == NONE ||
        !TEST_FLAG(_object_mask_unit, victim->object.type) ||
        TEST_FLAG(victim->object.damage_flags, _object_dead_bit) ||
        attacker->object.owner_team_index == NONE || victim->object.owner_team_index == NONE ||
        !game_team_is_enemy(attacker->object.owner_team_index, victim->object.owner_team_index) ||
        attacker->object.maximum_shield_vitality <= 0.0f) return;
    if (attacker->object.shield_vitality < 1.0f) attacker->object.shield_vitality = 1.0f;
    attacker->object.shield_stun_ticks = 0;
}

short mcc_skulls_rounds_per_shot(long weapon_index, short rounds)
{
    /* Recession: "Every shot is worth twice the ammo". */
    struct weapon_datum *weapon;
    if (!mcc_skull_active(MCC_SKULL_RECESSION)) return rounds;
    weapon = (struct weapon_datum *)object_try_and_get_and_verify_type(weapon_index, _object_mask_weapon);
    if (!weapon || !TEST_FLAG(weapon->item.flags, _item_belongs_to_player_bit)) return rounds;
    return (short)(rounds * 2);
}

boolean mcc_skulls_restart_on_death(void)
{
    /* Iron: "Dying solo restarts the level" (co-op's checkpoint rule is the
     * engine's own: everyone dead reverts to the last checkpoint). */
    return mcc_skull_active(MCC_SKULL_IRON) && game_connection() == _game_connection_local &&
        players_are_all_dead();
}
#endif /* HALO_MCC */
