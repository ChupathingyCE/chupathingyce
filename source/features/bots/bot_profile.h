/* Difficulty-owned behavior parameters. Field roles follow the recovered
 * Infinite profile; values are provisional CE tuning, NOT recovered tag data. */
#ifndef HALO_BOT_PROFILE_H
#define HALO_BOT_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

enum bot_profile_id
{
    BOT_PROFILE_RECRUIT = 0,
    BOT_PROFILE_MARINE,
    BOT_PROFILE_ODST,
    BOT_PROFILE_SPARTAN,
    BOT_PROFILE_COUNT
};

struct bot_delay_range
{
    float minimum_seconds;
    float maximum_seconds;
};

struct bot_combat_profile
{
    struct bot_delay_range melee_cooldown;
    struct bot_delay_range grenade_cooldown;
    float melee_close_range;
    float melee_range;
    float melee_action_range; /* CE player control reach, not an Infinite lunge */
    float melee_hard_max_range;
    float melee_lunge_range;
    float vulnerable_vitality;
    float melee_target_health;
    float melee_confidence_min;
    float disengage_confidence;
    float grenade_min_distance;
    float grenade_max_distance;
    float grenade_max_height;
    bool advanced_melee;
    bool lunge_vulnerable;
};

/* Retain the existing skill field names while relocating their ownership.
 * Base profile owns actions/aim/timers; active profile owns perception,
 * locomotion and selection. No binary offsets or Infinite enum IDs leak in. */
struct bot_skill
{
    char const *name;
    float aim_error_degrees;
    float turn_degrees_per_second;
    float sight_range;
    float field_of_view_degrees;
    float fire_tolerance_degrees;
    float burst_seconds;
    float burst_pause_seconds;
    bool strafes;
    bool throws_grenades;
    bool retreats;
    bool shares_sightings;
    float fire_range;
    float accurate_range;
    float aim_settle_seconds;
    float moving_aim_penalty;
    float moving_turn_scale;
    bool crouches;
    float orient_seconds;
    struct bot_delay_range acquire_delay;
    struct bot_delay_range visible_delay;
    float reaction_memory_seconds;
    float pursuit_memory_seconds;
    float hysteresis_bonus;
    float hysteresis_seconds;
    float switch_margin;
    struct bot_combat_profile combat;
};

enum
{
    BOT_WEAPON_ATTRIBUTES_MAXIMUM = 32,
    BOT_WEAPON_ATTRIBUTE_VALUES = 6
};

/* Recovered attribute numbering. Roles of 1..3 are aim-related; their use as
 * error/settle/moving scales below is our explicit CE adaptation. */
enum bot_weapon_attribute
{
    BOT_WEAPON_DESIRABILITY = 0,
    BOT_WEAPON_AIM_ERROR_SCALE,
    BOT_WEAPON_AIM_SETTLE_SCALE,
    BOT_WEAPON_MOVING_ERROR_SCALE,
    BOT_WEAPON_RANGE_MIN,
    BOT_WEAPON_RANGE_MAX,
    BOT_WEAPON_RESERVED_6,
    BOT_WEAPON_RESERVED_7,
    BOT_WEAPON_RESERVED_8,
    BOT_WEAPON_RESERVED_9,
    BOT_WEAPON_IS_MELEE,
    BOT_WEAPON_CAN_SWAP,
    BOT_WEAPON_THROWABLE,
    BOT_WEAPON_ATTRIBUTE_COUNT
};

struct bot_weapon_attributes
{
    uint32_t definition_key; /* full salted CE tag handle; UINT32_MAX is invalid */
    float values[BOT_WEAPON_ATTRIBUTE_VALUES];
    uint32_t flags; /* recovered attributes 6..12, bit (attribute - 6) */
};

extern struct bot_skill const bot_skills[BOT_PROFILE_COUNT];

/* C policy supplies missing per-weapon tag rows here. Bounded copies, no heap.
 * Validate the entire replacement before mutation; duplicate/invalid keys,
 * nonfinite/negative values, aim scales outside 0.05..16 and reversed ranges
 * are rejected. The lower bound preserves a representable imperfect-aim floor.
 * Rows are map-local; reset before loading another map's tag identities. */
void bot_profile_reset_weapon_attributes(void);
bool bot_profile_set_weapon_attributes(uint32_t profile_id,
    struct bot_weapon_attributes const *rows, uint32_t count);

/* Unknown key/profile: neutral desirability/aim scales (1), unspecified range
 * (0), swap allowed, no melee/throwable flags. Invalid attribute returns 0. */
float bot_profile_weapon_attribute(uint32_t profile_id, uint32_t definition_key,
    enum bot_weapon_attribute attribute);

/* NULL for an invalid ID; callers must validate configuration on admission. */
struct bot_skill const *bot_profile_get(uint32_t id);

/* Delay rounded up to whole ticks, with bounded conversion. Invalid range,
 * sample or tick rate returns 0 (callers supply validated profile data).
 * sample is a caller-owned uniform draw in [0,1]. */
uint32_t bot_profile_delay_ticks(struct bot_delay_range const *range,
    float sample, uint32_t ticks_per_second);

#endif
