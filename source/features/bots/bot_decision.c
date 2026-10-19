/* CE utility policy behind an engine-independent input/output seam.
 * Architecture reference: halo-rea/readable/bots/DecisionLoop.cpp, section 4.
 * Formulas and defaults below preserve our CE policy, not recovered Lua. */
#include "bot_decision.h"

#include <assert.h>
#include <stddef.h>

static struct bot_utility_tuning const bot_slayer_tuning =
{
    0.95f, /* vehicle */
    0.15f, /* patrol */
    0.9f, 0.85f, 0.55f, 0.28f, /* seen, attacker, remembered, shared cap */
    { ENGINE_AI_UTILITY_LINEAR, 50.f, 0.f, 0.6f, 1.f, 0 },
    { ENGINE_AI_UTILITY_LINEAR, 0.f, 1.f, 0.5f, 1.f, 0 },
    { ENGINE_AI_UTILITY_POWER, 0.5f, 0.f, 0.f, 1.f, 2 },
    0.85f, /* retreat cap */
    0.6f, 0.8f, 0.75f, 0.95f, /* weapon, powerup, vehicle, in reach */
    { ENGINE_AI_UTILITY_LINEAR, 30.f, 0.f, 0.45f, 1.f, 0 },
    { ENGINE_AI_UTILITY_LINEAR, 35.f, 0.f, 0.2f, 1.f, 0 },
    { 3.f, 2.f, 1.f, 1.f },
    15.f /* teammate radius */
};

static void bot_decision_combat(struct bot_utility_tuning const *tuning,
    struct bot_decision_inputs const *inputs, float *scores);
static void bot_decision_scavenge(struct bot_utility_tuning const *tuning,
    struct bot_decision_inputs const *inputs, float *scores);

struct bot_utility_tuning const *bot_decision_default_tuning(void)
{
    return &bot_slayer_tuning;
}

uint32_t bot_decision_objective_leaf(enum bot_ambition_kind kind)
{
    switch (kind) {
    case _bot_ambition_take: return _bot_leaf_take;
    case _bot_ambition_deliver: return _bot_leaf_deliver;
    case _bot_ambition_return: return _bot_leaf_return;
    case _bot_ambition_guard: return _bot_leaf_guard;
    case _bot_ambition_hunt: return _bot_leaf_hunt;
    case _bot_ambition_hold: return _bot_leaf_hold;
    case _bot_ambition_reach: return _bot_leaf_reach;
    case _bot_ambition_hide: return _bot_leaf_hide;
    default: return _bot_leaf_none;
    }
}

uint32_t bot_decision_pickup_leaf(bool vehicle, bool equipment)
{
    return vehicle ? _bot_leaf_enter_vehicle :
        (equipment ? _bot_leaf_acquire_equipment : _bot_leaf_acquire_weapon);
}

bool bot_decision_is_objective_leaf(uint32_t leaf)
{
    return leaf == _bot_leaf_take || (leaf >= _bot_leaf_deliver && leaf <= _bot_leaf_hide);
}

bool bot_decision_is_pickup_leaf(uint32_t leaf)
{
    return leaf == _bot_leaf_acquire_weapon || leaf == _bot_leaf_acquire_equipment ||
        leaf == _bot_leaf_enter_vehicle;
}

void bot_decision_score(struct bot_utility_tuning const *tuning,
    struct bot_decision_inputs const *inputs, float scores[BOT_BEHAVIOR_COUNT])
{
    uint32_t index;
    uint32_t objective;
    uint32_t pickup;
    bool duty;
    assert(tuning != NULL);
    assert(inputs != NULL);
    assert(scores != NULL);
    objective = bot_decision_objective_leaf(inputs->objective_kind);
    pickup = bot_decision_pickup_leaf(inputs->opportunity_vehicle, inputs->opportunity_powerup);
    duty = inputs->objective_valid && objective != _bot_leaf_none;
    for (index = 0; index < BOT_BEHAVIOR_COUNT; index++) {
        scores[index] = 0.f;
    }
    if (inputs->seated) {
        scores[_bot_leaf_vehicle - 1] = tuning->vehicle;
        return;
    }
    if (inputs->danger && !inputs->navigation_only) {
        scores[_bot_leaf_avoid - 1] = 0.9f; /* recovered avoid utility */
    }
    scores[_bot_leaf_roam - 1] = tuning->roam;
    if (inputs->opportunity_relevant && inputs->offered_ride &&
        !(duty && inputs->objective_kind == _bot_ambition_deliver)) {
        scores[_bot_leaf_enter_vehicle - 1] = 1.f;
        return;
    }
    if (duty && inputs->objective_kind != _bot_ambition_guard &&
        inputs->objective_kind != _bot_ambition_deliver &&
        inputs->opportunity_relevant && inputs->opportunity_vehicle) {
        scores[pickup - 1] = inputs->objective_value;
        return;
    }
    if (duty) {
        scores[objective - 1] = inputs->objective_value;
        if (inputs->objective_kind != _bot_ambition_guard) {
            return;
        }
    }
    /* Preserve the existing navigation fixture's priority after duties. */
    if (inputs->navigation_only) {
        return;
    }
    bot_decision_combat(tuning, inputs, scores);
    bot_decision_scavenge(tuning, inputs, scores);
}

static void bot_decision_combat(struct bot_utility_tuning const *tuning,
    struct bot_decision_inputs const *inputs, float *scores)
{
    float known;
    float fight;
    if (!inputs->target_known) {
        return;
    }
    switch (inputs->target_source) {
    case _bot_target_seen: known = tuning->fight_seen; break;
    case _bot_target_attacker: known = tuning->fight_attacker; break;
    case _bot_target_remembered: known = tuning->fight_remembered; break;
    default: known = tuning->fight_shared_cap; break;
    }
    fight = known * engine_ai_utility_curve_evaluate(
        &tuning->fight_distance, inputs->target_distance) *
        engine_ai_utility_curve_evaluate(&tuning->fight_confidence, inputs->confidence);
    if (inputs->target_source == _bot_target_shared && fight > tuning->fight_shared_cap) {
        fight = tuning->fight_shared_cap;
    }
    /* CE adaptation: while a physical hazard is observed, use native fight's
     * 0.5 ceiling so a legacy high-confidence fight cannot tie/stall avoid. */
    if (inputs->danger && fight > 0.5f) {
        fight = 0.5f;
    }
    scores[_bot_leaf_fight - 1] = fight;
    if (inputs->retreat_allowed && inputs->target_source != _bot_target_shared) {
        float retreat = (1.f - inputs->confidence) * engine_ai_utility_curve_evaluate(
            &tuning->retreat_shields, inputs->shields);
        scores[_bot_leaf_retreat - 1] = retreat < tuning->retreat_cap ?
            retreat : tuning->retreat_cap;
    }
}

static void bot_decision_scavenge(struct bot_utility_tuning const *tuning,
    struct bot_decision_inputs const *inputs, float *scores)
{
    float value;
    uint32_t pickup = bot_decision_pickup_leaf(inputs->opportunity_vehicle, inputs->opportunity_powerup);
    engine_ai_utility_curve const *distance_curve;
    if (!inputs->opportunity_relevant || !inputs->opportunity_distance_known) {
        return;
    }
    if (inputs->opportunity_in_reach) {
        scores[pickup - 1] = tuning->scavenge_in_reach;
        return;
    }
    value = inputs->opportunity_powerup ? tuning->scavenge_powerup : tuning->scavenge_weapon;
    distance_curve = &tuning->scavenge_distance;
    if (inputs->opportunity_vehicle) {
        value = tuning->scavenge_vehicle;
        distance_curve = &tuning->scavenge_vehicle_distance;
    }
    scores[pickup - 1] = value *
        engine_ai_utility_curve_evaluate(distance_curve, inputs->opportunity_distance);
}
