/* Bot scoring policy, independent of engine objects and action execution.
 * The engine supplies an observed snapshot; policy returns utility outputs.
 * This is CE policy, not Infinite's unrecovered Lua or tag tuning. */
#ifndef HALO_BOT_DECISION_H
#define HALO_BOT_DECISION_H

#include <stdbool.h>
#include "../../engine_ai/utility.h"
#include "bot_ambitions.h"

/* A leaf's utility option is its id less one. Keep existing log/tree IDs. */
enum
{
    _bot_leaf_none = 0,
    _bot_leaf_retreat,
    _bot_leaf_fight,
    _bot_leaf_roam,
    _bot_leaf_acquire_weapon,
    _bot_leaf_vehicle,
    _bot_leaf_take,
    _bot_leaf_acquire_equipment,
    _bot_leaf_enter_vehicle,
    _bot_leaf_deliver,
    _bot_leaf_return,
    _bot_leaf_guard,
    _bot_leaf_hunt,
    _bot_leaf_hold,
    _bot_leaf_reach,
    _bot_leaf_hide,
    _bot_leaf_avoid,
    BOT_BEHAVIOR_COUNT = _bot_leaf_avoid
};

/* Legacy numeric aliases for old score readers; no generic selected leaves. */
#define _bot_leaf_scavenge _bot_leaf_acquire_weapon
#define _bot_leaf_objective _bot_leaf_take

uint32_t bot_decision_objective_leaf(enum bot_ambition_kind kind);
uint32_t bot_decision_pickup_leaf(bool vehicle, bool equipment);
bool bot_decision_is_objective_leaf(uint32_t leaf);
bool bot_decision_is_pickup_leaf(uint32_t leaf);

enum bot_target_source
{
    _bot_target_none = 0,
    _bot_target_seen,
    _bot_target_attacker,
    _bot_target_remembered,
    _bot_target_shared
};

struct bot_utility_tuning
{
    float vehicle;
    float roam;
    float fight_seen;
    float fight_attacker;
    float fight_remembered;
    float fight_shared_cap;
    engine_ai_utility_curve fight_distance;
    engine_ai_utility_curve fight_confidence;
    engine_ai_utility_curve retreat_shields;
    float retreat_cap;
    float scavenge_weapon;
    float scavenge_powerup;
    float scavenge_vehicle;
    float scavenge_in_reach;
    engine_ai_utility_curve scavenge_distance;
    engine_ai_utility_curve scavenge_vehicle_distance;
    float confidence_weights[4];
    float teammate_radius;
};

/* Caller-owned, read-only during scoring. No engine handles or hidden target
 * state. Distances are metres; confidence, shields and objective value are
 * finite 0..1 values. Irrelevant fields may be zero. A relevant opportunity
 * can have no usable entrance; it may support an objective ride, but ordinary
 * scavenging must not score it without a known distance. */
struct bot_decision_inputs
{
    float confidence;
    float shields;
    bool seated;
    bool navigation_only;
    bool danger; /* observed, expiring hazard; never a hidden enemy query */
    bool objective_valid;
    enum bot_ambition_kind objective_kind;
    float objective_value;
    bool target_known;
    enum bot_target_source target_source;
    float target_distance;
    bool retreat_allowed; /* difficulty permits it and cooldown has expired */
    bool opportunity_relevant;
    bool opportunity_vehicle;
    bool opportunity_powerup;
    bool opportunity_in_reach;
    bool opportunity_distance_known;
    float opportunity_distance;
    bool offered_ride; /* relevant offered seat, with no visible enemy */
};

/* Existing CE defaults. Returned storage is immutable and process-lifetime. */
struct bot_utility_tuning const *bot_decision_default_tuning(void);

/* Overwrites all BOT_BEHAVIOR_COUNT outputs, 0 for unavailable behaviors.
 * No allocation, engine access, random draws, claims, or selector mutation.
 * Inputs/tuning/outputs must be non-null and must not overlap. */
void bot_decision_score(struct bot_utility_tuning const *tuning,
    struct bot_decision_inputs const *inputs, float scores[BOT_BEHAVIOR_COUNT]);

#endif
