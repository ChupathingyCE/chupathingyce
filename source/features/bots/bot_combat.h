/* Native-style combat utility and observed-action cooldowns. Engine access,
 * aim, trajectory/teammate safety and player controls remain in the adapter. */
#ifndef HALO_BOT_COMBAT_H
#define HALO_BOT_COMBAT_H

#include "bot_profile.h"

enum bot_combat_action
{
    BOT_COMBAT_NONE = 0,
    BOT_COMBAT_FIGHT,
    BOT_COMBAT_MELEE,
    BOT_COMBAT_GRENADE,
    BOT_COMBAT_DISENGAGE,
    BOT_COMBAT_SEEK,
    BOT_COMBAT_AVOID,
    BOT_COMBAT_ACTION_COUNT = BOT_COMBAT_AVOID
};

enum
{
    BOT_COMBAT_ALL = (1u << BOT_COMBAT_ACTION_COUNT) - 1u,
    BOT_COMBAT_EXCLUDED = -2
};

struct bot_combat_state
{
    int64_t last_tick;
    int64_t melee_ready_tick;
    int64_t grenade_ready_tick;
    int64_t melee_retry_tick;
    int64_t grenade_retry_tick;
    bool initialized;
    bool was_melee;
    bool was_grenade;
};

struct bot_combat_inputs
{
    int64_t tick;
    uint32_t enabled_mask; /* bit (action - 1) */
    float distance;
    float height_difference;
    float confidence;
    float target_vitality;
    float target_health;
    bool target_known;
    bool target_visible;
    bool target_meleeable;
    bool reaction_ready;
    bool busy;
    bool seated;
    bool passenger_fight;
    bool playfighting;
    bool melee_only;
    bool melee_weapon;
    bool out_of_ammo;
    bool target_meleeing;
    bool can_counter;
    bool target_vulnerable;
    bool lunge_path_clear;
    bool has_grenade;
    bool grenade_path_clear;
    bool retreat_allowed;
    bool danger;
};

/* New life/map. Initial grenade delay is CE spawn policy, not a cooldown. */
void bot_combat_reset(struct bot_combat_state *state, int64_t tick,
    uint32_t initial_grenade_delay);

/* Call every simulation tick, even if no target or another goal is running.
 * Only observed falling edges start full cooldowns. Callers sample the two
 * supplied delays on the corresponding edge; no heap/RNG here. */
void bot_combat_observe(struct bot_combat_state *state, int64_t tick,
    bool melee_active, bool grenade_active, uint32_t melee_delay, uint32_t grenade_delay);

/* Short retry gates after submitted controls or a refused safety check.
 * Neither operation consumes an action cooldown. Repeated calls cannot
 * shorten an already pending retry. Only melee/grenade are accepted. */
void bot_combat_retry(struct bot_combat_state *state, enum bot_combat_action action,
    int64_t tick, uint32_t delay);

/* Writes every output. Disabled/playfighting/passenger exclusions score -2;
 * unavailable actions score 0. Melee can exceed 1 (recovered multipliers).
 * Read-only evaluation; highest positive score wins, deterministic enum ties.
 * No target visibility/reaction means no melee, grenade or firing proposal. */
enum bot_combat_action bot_combat_evaluate(struct bot_skill const *base,
    struct bot_skill const *active, struct bot_combat_state const *state,
    struct bot_combat_inputs const *inputs, float scores[BOT_COMBAT_ACTION_COUNT]);

#endif
