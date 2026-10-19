/* Recovered evaluator flow: BehaviorEvaluators.cpp (halo-rea readable tree).
 * Fixed native utilities are recovered; profile values remain CE defaults. */
#include "bot_combat.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>

static int64_t bot_combat_deadline(int64_t tick, uint32_t delay)
{
    assert(tick >= 0);
    return (int64_t)delay > INT64_MAX - tick ? INT64_MAX : tick + (int64_t)delay;
}

static float bot_combat_melee(struct bot_combat_profile const *profile,
    struct bot_combat_inputs const *inputs)
{
    float range = profile->melee_range;
    float multiplier = 1.f;
    float floor = inputs->melee_weapon || inputs->out_of_ammo ? 0.8f : 0.f;
    float utility;
    if (!inputs->target_meleeable ||
        (inputs->melee_only && inputs->distance > profile->melee_hard_max_range)) {
        return 0.f;
    }
    if (profile->advanced_melee) {
        if (inputs->target_meleeing && inputs->can_counter) {
            return 1.f;
        }
        if (inputs->target_vulnerable) {
            if (profile->lunge_vulnerable && inputs->lunge_path_clear &&
                inputs->distance <= profile->melee_lunge_range) {
                return 1.f;
            }
            if (inputs->target_vitality <= profile->vulnerable_vitality) {
                range = profile->melee_lunge_range;
                multiplier = 1.8f;
            }
        }
    }
    if (inputs->confidence < profile->melee_confidence_min) {
        multiplier -= 0.3f;
    }
    if (inputs->target_health <= profile->melee_target_health) {
        multiplier += 0.5f;
    }
    if (inputs->distance <= profile->melee_close_range) {
        multiplier += 0.6f;
    }
    utility = range > 0.f && inputs->distance <= range ?
        (1.f - inputs->distance / range) * multiplier : 0.f;
    return utility < floor ? floor : utility;
}

void bot_combat_reset(struct bot_combat_state *state, int64_t tick,
    uint32_t initial_grenade_delay)
{
    assert(state != NULL);
    assert(tick >= 0);
    *state = (struct bot_combat_state){0};
    state->initialized = true;
    state->last_tick = tick;
    state->melee_ready_tick = tick;
    state->grenade_ready_tick = bot_combat_deadline(tick, initial_grenade_delay);
}

void bot_combat_observe(struct bot_combat_state *state, int64_t tick,
    bool melee_active, bool grenade_active, uint32_t melee_delay, uint32_t grenade_delay)
{
    assert(state != NULL);
    assert(tick >= 0);
    if (!state->initialized || tick < state->last_tick) {
        bot_combat_reset(state, tick, 0);
    }
    if (state->was_melee && !melee_active) {
        state->melee_ready_tick = bot_combat_deadline(tick, melee_delay);
    }
    if (state->was_grenade && !grenade_active) {
        state->grenade_ready_tick = bot_combat_deadline(tick, grenade_delay);
    }
    state->was_melee = melee_active;
    state->was_grenade = grenade_active;
    state->last_tick = tick;
}

void bot_combat_retry(struct bot_combat_state *state, enum bot_combat_action action,
    int64_t tick, uint32_t delay)
{
    int64_t deadline = bot_combat_deadline(tick, delay);
    int64_t *retry;
    assert(state != NULL);
    if (action == BOT_COMBAT_MELEE) {
        retry = &state->melee_retry_tick;
    } else if (action == BOT_COMBAT_GRENADE) {
        retry = &state->grenade_retry_tick;
    } else {
        return;
    }
    if (deadline > *retry) {
        *retry = deadline;
    }
}

static bool bot_combat_excluded(enum bot_combat_action action,
    struct bot_combat_inputs const *inputs)
{
    if (!(inputs->enabled_mask & (1u << (action - 1)))) {
        return true;
    }
    return (inputs->playfighting || inputs->passenger_fight) &&
        (action == BOT_COMBAT_MELEE || action == BOT_COMBAT_GRENADE ||
         action == BOT_COMBAT_DISENGAGE);
}

static void bot_combat_score_actions(struct bot_skill const *base,
    struct bot_combat_state const *state, struct bot_combat_inputs const *inputs,
    float *scores)
{
    if (!inputs->target_visible || !inputs->reaction_ready || inputs->busy) {
        return;
    }
    if (scores[BOT_COMBAT_FIGHT - 1] == 0.f && !inputs->melee_only) {
        scores[BOT_COMBAT_FIGHT - 1] = inputs->playfighting ? 2.f : 0.5f;
    }
    if (inputs->seated) {
        return;
    }
    if (scores[BOT_COMBAT_MELEE - 1] == 0.f && !state->was_melee &&
        inputs->tick >= state->melee_ready_tick && inputs->tick >= state->melee_retry_tick) {
        scores[BOT_COMBAT_MELEE - 1] = bot_combat_melee(&base->combat, inputs);
    }
    if (scores[BOT_COMBAT_GRENADE - 1] == 0.f && base->throws_grenades &&
        !inputs->melee_only && !state->was_grenade && inputs->has_grenade &&
        inputs->grenade_path_clear && inputs->tick >= state->grenade_ready_tick &&
        inputs->tick >= state->grenade_retry_tick &&
        inputs->distance >= base->combat.grenade_min_distance &&
        inputs->distance <= base->combat.grenade_max_distance &&
        fabsf(inputs->height_difference) <= base->combat.grenade_max_height) {
        scores[BOT_COMBAT_GRENADE - 1] = 0.85f;
    }
}

enum bot_combat_action bot_combat_evaluate(struct bot_skill const *base,
    struct bot_skill const *active, struct bot_combat_state const *state,
    struct bot_combat_inputs const *inputs, float scores[BOT_COMBAT_ACTION_COUNT])
{
    enum bot_combat_action best = BOT_COMBAT_NONE;
    float best_score = 0.f;
    uint32_t index;
    assert(base != NULL);
    assert(active != NULL);
    assert(state != NULL);
    assert(inputs != NULL);
    assert(scores != NULL);
    for (index = 0; index < BOT_COMBAT_ACTION_COUNT; index++) {
        scores[index] = bot_combat_excluded((enum bot_combat_action)(index + 1), inputs) ?
            (float)BOT_COMBAT_EXCLUDED : 0.f;
    }
    if (!state->initialized || inputs->tick < state->last_tick || inputs->tick < 0 ||
        !isfinite(inputs->distance) || inputs->distance < 0.f ||
        !isfinite(inputs->confidence) || !isfinite(inputs->height_difference) ||
        !isfinite(inputs->target_vitality) || !isfinite(inputs->target_health)) {
        return best;
    }
    if (inputs->danger && !inputs->seated && scores[BOT_COMBAT_AVOID - 1] == 0.f) {
        scores[BOT_COMBAT_AVOID - 1] = 0.9f;
    }
    if (inputs->target_known) {
        if (!inputs->target_visible && scores[BOT_COMBAT_SEEK - 1] == 0.f) {
            scores[BOT_COMBAT_SEEK - 1] = 0.1f;
        }
        if (!inputs->seated && inputs->retreat_allowed && active->retreats &&
            inputs->confidence <= active->combat.disengage_confidence &&
            scores[BOT_COMBAT_DISENGAGE - 1] == 0.f) {
            scores[BOT_COMBAT_DISENGAGE - 1] = 0.7f;
        }
        bot_combat_score_actions(base, state, inputs, scores);
    }
    for (index = 0; index < BOT_COMBAT_ACTION_COUNT; index++) {
        if (scores[index] > best_score) {
            best_score = scores[index];
            best = (enum bot_combat_action)(index + 1);
        }
    }
    return best;
}
