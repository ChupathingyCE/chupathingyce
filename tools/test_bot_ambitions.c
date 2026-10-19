#include "../source/features/bots/bot_ambitions.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static struct bot_ambition_rule rule(uint32_t kind, uint32_t key, uint32_t slots)
{
    struct bot_ambition_rule result = {0};
    result.identity.kind = kind;
    result.identity.object_key = key;
    result.identity.carrier_key = UINT32_MAX;
    result.identity.source_id = kind;
    result.team_mask = 1;
    result.specific_player = BOT_AMBITION_ANY_PLAYER;
    result.slot_limit = slots;
    result.all_bots = true;
    return result;
}

static struct bot_ambition_actor actor(uint32_t number)
{
    struct bot_ambition_actor result = {0};
    result.bot_number = number;
    result.player_key = 100 + number;
    result.has_teams = true;
    return result;
}

static void runner_up_and_transaction(void)
{
    struct bot_ambitions state;
    struct bot_ambition_rule rules[3] = {rule(1, 10, 1), rule(2, 20, 1), rule(3, 30, 1)};
    struct bot_ambition_score scores[3] = {{0.9f, 0.f}, {0.8f, 0.f}, {0.7f, 0.f}};
    struct bot_ambition_actor first = actor(1), second = actor(2), third = actor(3);
    uint32_t selected = 99;
    bot_ambitions_reset(&state);
    assert(bot_ambitions_publish(&state, rules, 3, 0));
    assert(bot_ambitions_select(&state, &first, scores, 0, 10, &selected) && selected == 0);
    assert(bot_ambitions_select(&state, &second, scores, 0, 10, &selected) && selected == 1);
    assert(bot_ambitions_claimant_count(&state, 2, 0) == 0); /* not the last eligible runner-up */
    assert(!bot_ambitions_try_claim(&state, &first, 1, 0, 10));
    assert(bot_ambitions_has_claim(&state, 1, &rules[0].identity, 0)); /* failed switch is atomic */
    assert(bot_ambitions_select(&state, &third, scores, 0, 10, &selected) && selected == 2);
    assert(bot_ambitions_try_claim(&state, &first, 0, 1, 10));
    assert(bot_ambitions_claimant_count(&state, 0, 1) == 1); /* renewal doesn't consume a second slot */
    bot_ambitions_release(&state, 3, NULL);
    assert(bot_ambitions_try_claim(&state, &first, 2, 1, 10));
    assert(bot_ambitions_claimant_count(&state, 0, 1) == 0);
    bot_ambitions_release(&state, 1, &rules[0].identity); /* stale old behavior cancellation */
    assert(bot_ambitions_has_claim(&state, 1, &rules[2].identity, 1));
    bot_ambitions_release(&state, 1, &rules[2].identity);
    assert(!bot_ambitions_has_claim(&state, 1, &rules[2].identity, 1));
}

static void eligibility_and_capacity(void)
{
    struct bot_ambitions state;
    struct bot_ambition_rule value = rule(1, 10, BOT_AMBITION_UNLIMITED);
    struct bot_ambition_actor first = actor(1), last = actor(31), other = actor(2), invalid = actor(32);
    bot_ambitions_reset(&state);
    value.all_bots = false;
    value.include_bots = 1u | (1u << 30);
    value.exclude_bots = 1u << 30;
    assert(bot_ambitions_publish(&state, &value, 1, 0));
    assert(bot_ambitions_can_claim(&state, &first, 0, 0));
    assert(!bot_ambitions_can_claim(&state, &last, 0, 0));
    assert(!bot_ambitions_can_claim(&state, &other, 0, 0));
    assert(!bot_ambitions_can_claim(&state, &invalid, 0, 0));
    invalid.bot_number = 0; assert(!bot_ambitions_can_claim(&state, &invalid, 0, 0));
    value.exclude_bots = 0;
    assert(bot_ambitions_publish(&state, &value, 1, 0));
    last.team_index = 1; assert(!bot_ambitions_can_claim(&state, &last, 0, 0));
    last.has_teams = false; assert(bot_ambitions_can_claim(&state, &last, 0, 0));
    value.specific_player = first.player_key;
    assert(bot_ambitions_publish(&state, &value, 1, 0));
    assert(!bot_ambitions_can_claim(&state, &last, 0, 0));
    assert(bot_ambitions_try_claim(&state, &first, 0, 0, 10));
    value.team_mask = 2;
    assert(bot_ambitions_publish(&state, &value, 1, 1));
    assert(!bot_ambitions_has_claim(&state, 1, &value.identity, 1));
    value = rule(1, 10, BOT_AMBITION_UNLIMITED);
    assert(bot_ambitions_publish(&state, &value, 1, 1));
    for (uint32_t number = 1; number <= BOT_AMBITION_BOTS_MAX; number++) {
        struct bot_ambition_actor current = actor(number);
        assert(bot_ambitions_try_claim(&state, &current, 0, 1, 10));
    }
    assert(bot_ambitions_claimant_count(&state, 0, 1) == 31);
    value.slot_limit = 2;
    assert(bot_ambitions_publish(&state, &value, 1, 2));
    assert(bot_ambitions_claimant_count(&state, 0, 2) == 2);
    assert(bot_ambitions_has_claim(&state, 1, &value.identity, 2));
    assert(bot_ambitions_has_claim(&state, 2, &value.identity, 2));
    assert(!bot_ambitions_has_claim(&state, 31, &value.identity, 2));
    value.slot_limit = 0;
    assert(bot_ambitions_publish(&state, &value, 1, 3));
    assert(bot_ambitions_claimant_count(&state, 0, 3) == 0);
}

static void identity_and_publication(void)
{
    struct bot_ambitions state;
    struct bot_ambition_actor first = actor(1);
    struct bot_ambition_rule rules[2] = {rule(1, 0x80000005u, 1), rule(1, 0x90000005u, 1)};
    struct bot_ambition_rule reordered[2] = {rules[1], rules[0]};
    bot_ambitions_reset(&state);
    assert(bot_ambitions_publish(&state, rules, 2, 0));
    assert(bot_ambitions_try_claim(&state, &first, 0, 0, 10));
    assert(bot_ambitions_publish(&state, reordered, 2, 1));
    assert(bot_ambitions_claimant_count(&state, 1, 1) == 1);
    assert(bot_ambitions_claimant_count(&state, 0, 1) == 0); /* salts aren't truncated */
    assert(bot_ambitions_publish(&state, rules + 1, 1, 2));
    assert(!bot_ambitions_has_claim(&state, 1, &rules[0].identity, 2));
    rules[0] = rule(3, UINT32_MAX, 1); rules[1] = rules[0];
    rules[0].identity.source_id = 1000; rules[1].identity.source_id = 1001;
    assert(bot_ambitions_publish(&state, rules, 2, 2)); /* distinct authored areas */
    assert(bot_ambitions_try_claim(&state, &first, 0, 2, 10));
    rules[0].identity.carrier_key = 900;
    assert(bot_ambitions_publish(&state, rules, 2, 3));
    assert(!bot_ambitions_has_claim(&state, 1, &rules[0].identity, 3));
    assert(bot_ambitions_try_claim(&state, &first, 0, 3, 10));
    assert(bot_ambitions_publish(&state, NULL, 0, 4));
    assert(!bot_ambitions_has_claim(&state, 1, &rules[0].identity, 4));
}

static void invalid_requests_preserve_state(void)
{
    struct bot_ambitions state;
    struct bot_ambition_actor first = actor(1), invalid = actor(32);
    struct bot_ambition_rule rules[2] = {rule(1, 10, 1), rule(1, 10, 1)};
    struct bot_ambition_score score = {1.f, 0.f};
    uint32_t selected = 99;
    bot_ambitions_reset(&state);
    assert(bot_ambitions_publish(&state, rules, 1, 0));
    assert(bot_ambitions_try_claim(&state, &first, 0, 0, 10));
    assert(!bot_ambitions_publish(&state, rules, 2, 1));
    assert(!bot_ambitions_publish(&state, NULL, 1, 1));
    assert(!bot_ambitions_publish(&state, rules, BOT_AMBITION_CAPACITY + 1, 1));
    rules[0].identity.kind = 0; assert(!bot_ambitions_publish(&state, rules, 1, 1));
    rules[0].identity.kind = 32; assert(!bot_ambitions_publish(&state, rules, 1, 1));
    rules[0].identity.kind = 1; rules[0].slot_limit = 32;
    assert(!bot_ambitions_publish(&state, rules, 1, 1));
    rules[0].slot_limit = 1; rules[0].include_bots = 0x80000000u;
    assert(!bot_ambitions_publish(&state, rules, 1, 1));
    assert(!bot_ambitions_try_claim(&state, &invalid, 0, 0, 10));
    assert(!bot_ambitions_try_claim(&state, &first, 0, 0, 0));
    assert(!bot_ambitions_try_claim(&state, &first, 0, -1, 10));
    assert(!bot_ambitions_try_claim(&state, &first, 0, INT64_MAX, 10));
    score.utility = NAN;
    assert(!bot_ambitions_select(&state, &first, &score, 1, 10, &selected));
    score.utility = 1.f; score.distance = -1.f;
    assert(!bot_ambitions_select(&state, &first, &score, 1, 10, &selected));
    assert(selected == 99 && state.count == 1 && state.last_tick == 0);
    assert(bot_ambitions_has_claim(&state, 1, &state.rules[0].identity, 0));
    score.utility = 0.f; score.distance = 0.f;
    assert(!bot_ambitions_select(&state, &first, &score, 0, 10, &selected));
    assert(!bot_ambitions_has_claim(&state, 1, &state.rules[0].identity, 0));
}

static void distance_expiry_and_rewind(void)
{
    struct bot_ambitions state;
    struct bot_ambition_actor first = actor(1), second = actor(2);
    struct bot_ambition_rule rules[2] = {rule(1, 10, 1), rule(1, 20, 1)};
    struct bot_ambition_score scores[2] = {{1.f, 10.f}, {1.f, 5.f}};
    uint32_t selected = 99;
    bot_ambitions_reset(&state);
    assert(bot_ambitions_publish(&state, rules, 2, 0));
    assert(bot_ambitions_select(&state, &first, scores, 0, 4, &selected) && selected == 1);
    scores[0].distance = 5.f;
    assert(bot_ambitions_select(&state, &first, scores, 2, 4, &selected) && selected == 1);
    assert(bot_ambitions_has_claim(&state, 1, &rules[1].identity, 5));
    assert(!bot_ambitions_has_claim(&state, 1, &rules[1].identity, 6));
    assert(bot_ambitions_try_claim(&state, &second, 1, 6, 4));
    assert(bot_ambitions_publish(&state, rules, 2, 1)); /* rewound simulation */
    assert(bot_ambitions_claimant_count(&state, 1, 1) == 0);
    assert(bot_ambitions_try_claim(&state, &first, 0, INT64_MAX - 2, 100));
    assert(bot_ambitions_has_claim(&state, 1, &rules[0].identity, INT64_MAX - 1));
    assert(!bot_ambitions_has_claim(&state, 1, &rules[0].identity, INT64_MAX));
}

static void full_publication(void)
{
    struct bot_ambitions state;
    struct bot_ambition_rule rules[BOT_AMBITION_CAPACITY];
    bot_ambitions_reset(&state);
    for (uint32_t index = 0; index < BOT_AMBITION_CAPACITY; index++) {
        rules[index] = rule(1, index, 1);
    }
    assert(bot_ambitions_publish(&state, rules, BOT_AMBITION_CAPACITY, 0));
    for (uint32_t index = 0; index < BOT_AMBITION_CAPACITY; index++) {
        struct bot_ambition_actor current = actor(index + 1);
        assert(bot_ambitions_try_claim(&state, &current, index, 0, 10));
    }
    assert(state.count == BOT_AMBITION_CAPACITY);
    assert(bot_ambitions_claimant_count(&state, BOT_AMBITION_CAPACITY, 0) == 0);
    bot_ambitions_release(&state, 0, NULL); bot_ambitions_release(&state, 32, NULL);
    assert(bot_ambitions_claimant_count(&state, 0, 0) == 1);
}

int main(void)
{
    runner_up_and_transaction();
    eligibility_and_capacity();
    identity_and_publication();
    invalid_requests_preserve_state();
    distance_expiry_and_rewind();
    full_publication();
    puts("ambitions: slot limits, runner-up choice, identity, eligibility and lifecycle passed");
    return 0;
}
