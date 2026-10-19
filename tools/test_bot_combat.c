/* Recovered native evaluator semantics and real observed-action cooldowns. */
#include "features/bots/bot_combat.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static struct bot_combat_inputs target(void)
{
    struct bot_combat_inputs inputs = {0};
    inputs.tick = 100;
    inputs.enabled_mask = BOT_COMBAT_ALL;
    inputs.distance = 5.f;
    inputs.confidence = 1.f;
    inputs.target_vitality = inputs.target_health = 1.f;
    inputs.target_known = inputs.target_visible = inputs.target_meleeable = true;
    inputs.reaction_ready = inputs.retreat_allowed = true;
    return inputs;
}

static enum bot_combat_action evaluate(struct bot_combat_inputs const *inputs,
    struct bot_combat_state const *state, uint32_t base, uint32_t active, float *scores)
{
    return bot_combat_evaluate(bot_profile_get(base), bot_profile_get(active), state, inputs, scores);
}

static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.00001f);
}

static void firing_and_exclusions(void)
{
    struct bot_combat_inputs inputs = target();
    struct bot_combat_state state;
    float scores[BOT_COMBAT_ACTION_COUNT];
    bot_combat_reset(&state, 0, 0);
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_FIGHT);
    near(scores[BOT_COMBAT_FIGHT - 1], 0.5f);
    inputs.playfighting = true;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_FIGHT);
    near(scores[BOT_COMBAT_FIGHT - 1], 2.f);
    assert(scores[BOT_COMBAT_MELEE - 1] == -2.f);
    assert(scores[BOT_COMBAT_GRENADE - 1] == -2.f);
    assert(scores[BOT_COMBAT_DISENGAGE - 1] == -2.f);
    inputs.playfighting = false;
    inputs.enabled_mask &= ~(1u << (BOT_COMBAT_FIGHT - 1));
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_NONE);
    assert(scores[BOT_COMBAT_FIGHT - 1] == -2.f);
    inputs.enabled_mask = BOT_COMBAT_ALL;
    inputs.distance = 0.5f;
    inputs.passenger_fight = inputs.seated = true;
    assert(evaluate(&inputs, &state, 3, 3, scores) == BOT_COMBAT_FIGHT);
    assert(scores[BOT_COMBAT_MELEE - 1] == -2.f);
    inputs.passenger_fight = inputs.seated = false;
    inputs.reaction_ready = false;
    assert(evaluate(&inputs, &state, 3, 3, scores) == BOT_COMBAT_NONE);
    inputs.reaction_ready = true;
    inputs.target_visible = false;
    assert(evaluate(&inputs, &state, 3, 3, scores) == BOT_COMBAT_SEEK);
    near(scores[BOT_COMBAT_SEEK - 1], 0.1f);
    near(scores[BOT_COMBAT_MELEE - 1], 0.f);
    inputs.target_known = false;
    assert(evaluate(&inputs, &state, 3, 3, scores) == BOT_COMBAT_NONE);
    inputs.danger = true;
    assert(evaluate(&inputs, &state, 3, 3, scores) == BOT_COMBAT_AVOID);
    near(scores[BOT_COMBAT_AVOID - 1], 0.9f);
}

static void melee_formula(void)
{
    struct bot_combat_inputs inputs = target();
    struct bot_combat_state state;
    float scores[BOT_COMBAT_ACTION_COUNT];
    struct bot_skill base = *bot_profile_get(3);
    bot_combat_reset(&state, 0, 0);
    inputs.distance = 1.f;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_MELEE);
    near(scores[BOT_COMBAT_MELEE - 1], 2.f / 3.f);
    inputs.confidence = 0.1f;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_FIGHT);
    near(scores[BOT_COMBAT_MELEE - 1], (2.f / 3.f) * 0.7f);
    inputs.target_health = 0.1f;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_MELEE);
    near(scores[BOT_COMBAT_MELEE - 1], (2.f / 3.f) * 1.2f);
    inputs.confidence = inputs.target_health = 1.f;
    inputs.distance = 0.5f;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_MELEE);
    near(scores[BOT_COMBAT_MELEE - 1], (1.f - 0.5f / 3.f) * 1.6f);
    assert(scores[BOT_COMBAT_MELEE - 1] > 1.f);
    inputs.target_meleeable = false;
    assert(evaluate(&inputs, &state, 3, 3, scores) == BOT_COMBAT_FIGHT);
    inputs.target_meleeable = true;
    inputs.distance = 1.f;
    inputs.target_meleeing = inputs.can_counter = true;
    assert(evaluate(&inputs, &state, 3, 3, scores) == BOT_COMBAT_MELEE);
    near(scores[BOT_COMBAT_MELEE - 1], 1.f);
    inputs.target_meleeing = inputs.can_counter = false;
    inputs.target_vulnerable = true; inputs.target_vitality = 0.1f;
    assert(evaluate(&inputs, &state, 3, 3, scores) == BOT_COMBAT_MELEE);
    near(scores[BOT_COMBAT_MELEE - 1], 1.2f);
    base.combat.lunge_vulnerable = true;
    inputs.lunge_path_clear = true;
    assert(bot_combat_evaluate(&base, &base, &state, &inputs, scores) == BOT_COMBAT_MELEE);
    near(scores[BOT_COMBAT_MELEE - 1], 1.f);
    inputs.target_vulnerable = false; inputs.distance = 10.f; inputs.out_of_ammo = true;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_MELEE);
    near(scores[BOT_COMBAT_MELEE - 1], 0.8f);
    inputs.melee_only = true;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_NONE);
}

static void grenades_and_disengage(void)
{
    struct bot_combat_inputs inputs = target();
    struct bot_combat_state state;
    float scores[BOT_COMBAT_ACTION_COUNT];
    bot_combat_reset(&state, 0, 0);
    inputs.has_grenade = inputs.grenade_path_clear = true;
    inputs.distance = 8.f;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_GRENADE);
    near(scores[BOT_COMBAT_GRENADE - 1], 0.85f);
    inputs.distance = 20.f;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_GRENADE);
    inputs.distance = 20.01f;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_FIGHT);
    inputs.distance = 7.99f;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_FIGHT);
    inputs.distance = 10.f; inputs.height_difference = 5.01f;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_FIGHT);
    inputs.height_difference = -5.f;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_GRENADE);
    assert(evaluate(&inputs, &state, 0, 3, scores) == BOT_COMBAT_FIGHT); /* base ability */
    inputs.grenade_path_clear = false;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_FIGHT);
    inputs.confidence = bot_profile_get(3)->combat.disengage_confidence;
    assert(evaluate(&inputs, &state, 1, 3, scores) == BOT_COMBAT_DISENGAGE); /* active threshold */
    near(scores[BOT_COMBAT_DISENGAGE - 1], 0.7f);
    assert(evaluate(&inputs, &state, 3, 1, scores) == BOT_COMBAT_FIGHT);
    inputs.retreat_allowed = false;
    assert(evaluate(&inputs, &state, 3, 3, scores) == BOT_COMBAT_FIGHT);
    inputs.busy = true;
    assert(evaluate(&inputs, &state, 3, 3, scores) == BOT_COMBAT_NONE);
}

static void cooldown_lifecycle(void)
{
    struct bot_combat_inputs inputs = target();
    struct bot_combat_state state;
    float scores[BOT_COMBAT_ACTION_COUNT];
    bot_combat_reset(&state, 0, 90);
    inputs.distance = 1.f;
    bot_combat_retry(&state, BOT_COMBAT_MELEE, 100, 5);
    assert(state.melee_ready_tick == 0 && state.melee_retry_tick == 105);
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_FIGHT);
    inputs.tick = 105; /* a rejected control never started an animation/cooldown */
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_MELEE);
    bot_combat_observe(&state, 106, true, false, 30, 240);
    assert(state.melee_ready_tick == 0);
    inputs.tick = 106;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_FIGHT);
    bot_combat_observe(&state, 110, false, false, 30, 240);
    assert(state.melee_ready_tick == 140);
    bot_combat_observe(&state, 111, false, true, 30, 240);
    assert(state.melee_ready_tick == 140 && state.grenade_ready_tick == 90);
    bot_combat_observe(&state, 120, false, false, 30, 240);
    assert(state.grenade_ready_tick == 360);
    bot_combat_retry(&state, BOT_COMBAT_GRENADE, 120, 30);
    bot_combat_retry(&state, BOT_COMBAT_GRENADE, 120, 5);
    assert(state.grenade_retry_tick == 150 && state.grenade_ready_tick == 360);
    bot_combat_observe(&state, 121, false, false, 30, 240);
    assert(state.grenade_ready_tick == 360); /* no repeated completion */
    inputs.tick = 120;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_NONE); /* stale observation */
    bot_combat_observe(&state, 0, false, false, 30, 240); /* rewind */
    assert(state.melee_ready_tick == 0 && state.grenade_ready_tick == 0);
    bot_combat_reset(&state, INT64_MAX - 1, 90);
    assert(state.grenade_ready_tick == INT64_MAX);
    bot_combat_retry(&state, BOT_COMBAT_MELEE, INT64_MAX - 1, 90);
    assert(state.melee_retry_tick == INT64_MAX);
    inputs.tick = INT64_MAX; inputs.distance = NAN;
    assert(evaluate(&inputs, &state, 1, 1, scores) == BOT_COMBAT_NONE);
}

int main(void)
{
    firing_and_exclusions();
    melee_formula();
    grenades_and_disengage();
    cooldown_lifecycle();
    puts("bot combat: evaluator formulas, exclusions and observed cooldowns passed");
    return 0;
}
