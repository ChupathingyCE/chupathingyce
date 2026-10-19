/* Test the same scoring seam used by bots.c, without Halo assets. */
#include "../source/features/bots/bot_decision.h"

#include <assert.h>
#include <stdio.h>

static void near(float actual, float expected)
{
    float difference = actual - expected;
    assert(difference > -0.00001f && difference < 0.00001f);
}

static void only_scores(float const *scores, unsigned int first, unsigned int second)
{
    for (unsigned int index = 0; index < BOT_BEHAVIOR_COUNT; index++) {
        if (index != first - 1 && index != second - 1) {
            near(scores[index], 0.f);
        }
    }
}

static void duties(void)
{
    struct bot_utility_tuning const *t = bot_decision_default_tuning();
    struct bot_decision_inputs in = {0};
    float scores[BOT_BEHAVIOR_COUNT];
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_roam - 1], 0.15f);
    only_scores(scores, _bot_leaf_roam, _bot_leaf_roam);
    in.target_known = true;
    in.target_source = _bot_target_seen;
    in.confidence = 1.f;
    in.objective_valid = true;
    in.objective_kind = _bot_ambition_take;
    in.objective_value = 0.98f;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_objective - 1], 0.98f);
    only_scores(scores, _bot_leaf_roam, _bot_leaf_objective);
    in.opportunity_vehicle = true;
    in.opportunity_relevant = true;
    /* The seat's distance need not be known to score the objective's ride. */
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_enter_vehicle - 1], 0.98f);
    only_scores(scores, _bot_leaf_roam, _bot_leaf_enter_vehicle);
    in.offered_ride = true;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_enter_vehicle - 1], 1.f);
    in.seated = true;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_vehicle - 1], 0.95f);
    only_scores(scores, _bot_leaf_vehicle, _bot_leaf_vehicle);
    in.seated = false;
    in.offered_ride = false;
    in.opportunity_relevant = false;
    in.objective_kind = _bot_ambition_guard;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_fight - 1], 0.9f); /* guards may fight */
    near(scores[_bot_leaf_guard - 1], 0.98f);
    in.navigation_only = true;
    bot_decision_score(t, &in, scores);
    only_scores(scores, _bot_leaf_roam, _bot_leaf_guard);
}

static void combat(void)
{
    struct bot_utility_tuning const *t = bot_decision_default_tuning();
    struct bot_decision_inputs in = {0};
    float scores[BOT_BEHAVIOR_COUNT];
    float const known[] = {0.9f, 0.85f, 0.55f, 0.28f};
    in.target_known = true;
    in.confidence = 1.f;
    for (unsigned int source = 1; source <= 4; source++) {
        in.target_source = (enum bot_target_source)source;
        for (unsigned int metres = 0; metres <= 60; metres++) {
            float distance_scale = metres >= 50 ? 0.6f : 1.f - 0.4f * (float)metres / 50.f;
            in.target_distance = (float)metres;
            bot_decision_score(t, &in, scores);
            near(scores[_bot_leaf_fight - 1], known[source - 1] * distance_scale);
        }
    }
    in.retreat_allowed = true;
    in.confidence = 0.f;
    in.shields = 0.f;
    in.target_source = _bot_target_seen;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_retreat - 1], 0.85f);
    in.shields = 0.25f;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_retreat - 1], 0.25f);
    in.shields = 0.5f;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_retreat - 1], 0.f);
    in.shields = 0.f;
    in.target_source = _bot_target_shared;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_retreat - 1], 0.f);
    in.target_source = _bot_target_seen;
    in.retreat_allowed = false;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_retreat - 1], 0.f);
    in.target_known = false;
    bot_decision_score(t, &in, scores);
    only_scores(scores, _bot_leaf_roam, _bot_leaf_roam);
}

static void opportunities(void)
{
    struct bot_utility_tuning const *t = bot_decision_default_tuning();
    struct bot_decision_inputs in = {0};
    float scores[BOT_BEHAVIOR_COUNT];
    in.opportunity_relevant = true;
    in.opportunity_distance_known = true;
    in.opportunity_distance = 30.f;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_scavenge - 1], 0.6f * 0.45f);
    in.opportunity_powerup = true;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_acquire_equipment - 1], 0.8f * 0.45f);
    in.opportunity_vehicle = true;
    in.opportunity_distance = 35.f;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_enter_vehicle - 1], 0.75f * 0.2f);
    in.opportunity_in_reach = true;
    in.target_known = true;
    in.target_source = _bot_target_seen;
    in.confidence = 1.f;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_enter_vehicle - 1], 0.95f);
    near(scores[_bot_leaf_fight - 1], 0.9f);
    in.opportunity_distance_known = false;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_enter_vehicle - 1], 0.f);
    near(scores[_bot_leaf_fight - 1], 0.9f); /* bad seat doesn't erase combat */
    in.opportunity_distance_known = true;
    in.opportunity_relevant = false;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_enter_vehicle - 1], 0.f);
}

static void distinct_goals(void)
{
    struct bot_utility_tuning const *t = bot_decision_default_tuning();
    struct bot_decision_inputs in = {0};
    float scores[BOT_BEHAVIOR_COUNT];
    in.objective_valid = true;
    in.objective_value = 1.f;
    for (int kind = _bot_ambition_take; kind < NUMBER_OF_BOT_AMBITION_KINDS; kind++) {
        uint32_t leaf = bot_decision_objective_leaf((enum bot_ambition_kind)kind);
        in.objective_kind = (enum bot_ambition_kind)kind;
        bot_decision_score(t, &in, scores);
        assert(leaf > 0 && leaf <= BOT_BEHAVIOR_COUNT && bot_decision_is_objective_leaf(leaf));
        near(scores[leaf - 1], 1.f);
        only_scores(scores, leaf, _bot_leaf_roam);
    }
    in.objective_kind = _bot_ambition_deliver;
    in.opportunity_vehicle = in.opportunity_relevant = in.offered_ride = true;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_deliver - 1], 1.f);
    near(scores[_bot_leaf_enter_vehicle - 1], 0.f);
    assert(bot_decision_is_pickup_leaf(bot_decision_pickup_leaf(false, false)));
    assert(bot_decision_pickup_leaf(false, true) == _bot_leaf_acquire_equipment);
    assert(bot_decision_pickup_leaf(true, true) == _bot_leaf_enter_vehicle);
    assert(bot_decision_objective_leaf((enum bot_ambition_kind)99) == _bot_leaf_none);
}

static void danger_priority(void)
{
    struct bot_decision_inputs in = {0};
    float scores[BOT_BEHAVIOR_COUNT];
    struct bot_utility_tuning const *t = bot_decision_default_tuning();
    in.danger = true;
    in.target_known = true; in.target_source = _bot_target_seen; in.confidence = 1.f;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_avoid - 1], 0.9f);
    near(scores[_bot_leaf_fight - 1], 0.5f);
    in.objective_valid = true; in.objective_kind = _bot_ambition_deliver; in.objective_value = 0.98f;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_deliver - 1], 0.98f); /* utility, not a carrier-route override */
    near(scores[_bot_leaf_avoid - 1], 0.9f);
    in.navigation_only = true;
    bot_decision_score(t, &in, scores);
    near(scores[_bot_leaf_avoid - 1], 0.f);
    in.seated = true;
    bot_decision_score(t, &in, scores);
    only_scores(scores, _bot_leaf_vehicle, _bot_leaf_vehicle);
}

int main(void)
{
    duties();
    distinct_goals();
    danger_priority();
    combat();
    opportunities();
    puts("bot decision policy: duties, combat and opportunities passed");
    return 0;
}
