#include "../source/features/bots/bot_profile.h"
#include "../source/features/bots/bot_reaction.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static struct bot_reaction_observation observation(int64_t tick, int64_t target,
    bool known, bool visible)
{
    struct bot_reaction_observation value = {0};
    value.tick = tick;
    value.target_id = target;
    value.known = known;
    value.visible = visible;
    value.acquire_delay_ticks = 10;
    value.visible_delay_ticks = 15;
    value.memory_ticks = 30;
    return value;
}

static void observe(struct bot_reaction *state, int64_t tick, int64_t target,
    bool known, bool visible)
{
    struct bot_reaction_observation value = observation(tick, target, known, visible);
    bot_reaction_observe(state, &value);
}

static void profile_values(void)
{
    struct bot_delay_range range = {0.5f, 1.f};
    assert(bot_profile_get(BOT_PROFILE_MARINE) == &bot_skills[BOT_PROFILE_MARINE]);
    assert(bot_profile_get(BOT_PROFILE_COUNT) == NULL);
    assert(bot_profile_get(UINT32_MAX) == NULL);
    assert(bot_profile_delay_ticks(&range, 0.f, 30) == 15);
    assert(bot_profile_delay_ticks(&range, 1.f, 30) == 30);
    assert(bot_profile_delay_ticks(&range, 0.5f, 30) == 23);
    assert(bot_profile_delay_ticks(&range, NAN, 30) == 0);
    assert(bot_profile_delay_ticks(&range, -0.1f, 30) == 0);
    assert(bot_profile_delay_ticks(&range, 1.1f, 30) == 0);
    assert(bot_profile_delay_ticks(&range, 1.f, 0) == 0);
    range.minimum_seconds = 2.f;
    assert(bot_profile_delay_ticks(&range, 0.f, 30) == 0);
    range.minimum_seconds = -1.f;
    assert(bot_profile_delay_ticks(&range, 0.f, 30) == 0);
    range.minimum_seconds = range.maximum_seconds = 1e20f;
    assert(bot_profile_delay_ticks(&range, 0.f, 30) == UINT32_MAX);
    for (uint32_t index = 0; index < BOT_PROFILE_COUNT; index++) {
        struct bot_skill const *profile = bot_profile_get(index);
        assert(profile != NULL);
        assert(profile->reaction_memory_seconds > 0.f);
        assert(profile->acquire_delay.maximum_seconds >= profile->acquire_delay.minimum_seconds);
        assert(profile->visible_delay.maximum_seconds >= profile->visible_delay.minimum_seconds);
        assert(profile->hysteresis_bonus >= 0.f);
    }
}

static void weapon_rows(void)
{
    struct bot_weapon_attributes rows[BOT_WEAPON_ATTRIBUTES_MAXIMUM] = {0};
    bot_profile_reset_weapon_attributes();
    assert(bot_profile_weapon_attribute(1, 10, BOT_WEAPON_DESIRABILITY) == 1.f);
    assert(bot_profile_weapon_attribute(1, 10, BOT_WEAPON_CAN_SWAP) == 1.f);
    assert(bot_profile_weapon_attribute(1, 10, BOT_WEAPON_RANGE_MAX) == 0.f);
    for (uint32_t index = 0; index < BOT_WEAPON_ATTRIBUTES_MAXIMUM; index++) {
        rows[index].definition_key = 0x80000000u + index;
        rows[index].values[0] = 2.f;
        rows[index].values[1] = rows[index].values[2] = rows[index].values[3] = 1.f;
        rows[index].values[4] = 6.f; rows[index].values[5] = 30.f;
        rows[index].flags = 0x7fu;
    }
    assert(bot_profile_set_weapon_attributes(1, rows, BOT_WEAPON_ATTRIBUTES_MAXIMUM));
    assert(bot_profile_weapon_attribute(1, 0x80000000u, BOT_WEAPON_DESIRABILITY) == 2.f);
    assert(bot_profile_weapon_attribute(2, 0x80000000u, BOT_WEAPON_DESIRABILITY) == 1.f);
    assert(bot_profile_weapon_attribute(1, 0x8000001fu, BOT_WEAPON_RANGE_MAX) == 30.f);
    for (int attribute = BOT_WEAPON_RESERVED_6; attribute < BOT_WEAPON_ATTRIBUTE_COUNT; attribute++) {
        assert(bot_profile_weapon_attribute(1, 0x80000000u, (enum bot_weapon_attribute)attribute) == 1.f);
    }
    rows[0].values[0] = 3.f; /* caller memory is not retained */
    assert(bot_profile_weapon_attribute(1, 0x80000000u, BOT_WEAPON_DESIRABILITY) == 2.f);
    assert(!bot_profile_set_weapon_attributes(BOT_PROFILE_COUNT, rows, 1));
    assert(!bot_profile_set_weapon_attributes(1, rows, BOT_WEAPON_ATTRIBUTES_MAXIMUM + 1));
    assert(!bot_profile_set_weapon_attributes(1, NULL, 1));
    rows[1].definition_key = rows[0].definition_key;
    assert(!bot_profile_set_weapon_attributes(1, rows, 2));
    rows[0].definition_key = UINT32_MAX;
    assert(!bot_profile_set_weapon_attributes(1, rows, 1));
    rows[0].definition_key = 0x80000000u;
    rows[0].values[0] = NAN;
    assert(!bot_profile_set_weapon_attributes(1, rows, 1));
    rows[0].values[0] = 3.f; rows[0].values[1] = 0.f;
    assert(!bot_profile_set_weapon_attributes(1, rows, 1));
    rows[0].values[1] = 1.f; rows[0].values[4] = 31.f;
    assert(!bot_profile_set_weapon_attributes(1, rows, 1));
    rows[0].values[4] = 6.f; rows[0].flags = 0x80u;
    assert(!bot_profile_set_weapon_attributes(1, rows, 1));
    assert(bot_profile_weapon_attribute(1, 0x80000000u, BOT_WEAPON_DESIRABILITY) == 2.f);
    assert(bot_profile_weapon_attribute(1, 0x80000000u, (enum bot_weapon_attribute)-1) == 0.f);
    assert(bot_profile_weapon_attribute(1, 0x80000000u, BOT_WEAPON_ATTRIBUTE_COUNT) == 0.f);
    assert(bot_profile_set_weapon_attributes(1, NULL, 0));
    assert(bot_profile_weapon_attribute(1, 0x80000000u, BOT_WEAPON_DESIRABILITY) == 1.f);
    assert(bot_profile_set_weapon_attributes(2, rows + 1, 1));
    bot_profile_reset_weapon_attributes();
    assert(bot_profile_weapon_attribute(2, 0x80000000u, BOT_WEAPON_IS_MELEE) == 0.f);
}

static void initial_and_occlusion(void)
{
    struct bot_reaction state;
    bot_reaction_reset(&state);
    assert(!bot_reaction_can_fire(&state, 0));
    observe(&state, 0, 100, true, true);
    assert(!bot_reaction_can_fire(&state, 14));
    assert(bot_reaction_can_fire(&state, 15));
    observe(&state, 5, 100, true, false);
    assert(!bot_reaction_can_fire(&state, 16));
    observe(&state, 6, 100, true, true);
    assert(state.acquire_ready_tick == 10 && state.visible_ready_tick == 15);
    assert(!bot_reaction_can_fire(&state, 14)); /* memory cannot shorten an outstanding delay */
    assert(bot_reaction_can_fire(&state, 15));
    observe(&state, 16, 100, true, false);
    observe(&state, 17, 100, true, true);
    assert(bot_reaction_can_fire(&state, 17));
    assert(state.tracking_since_tick == 17);
    observe(&state, 18, 100, true, false);
    observe(&state, 47, 100, true, true); /* exactly memory boundary */
    assert(!bot_reaction_can_fire(&state, 47));
    assert(state.visible_ready_tick == 62);
}

static void identities_and_shared(void)
{
    struct bot_reaction state;
    bot_reaction_reset(&state);
    observe(&state, 10, 100, true, false); /* a shared or damage contact */
    observe(&state, 30, 100, true, true);
    assert(state.acquire_ready_tick == 20 && state.visible_ready_tick == 45);
    assert(!bot_reaction_can_fire(&state, 30));
    observe(&state, 45, 100, true, true);
    assert(bot_reaction_can_fire(&state, 45));
    observe(&state, 46, 101, true, true); /* different salted unit, same player allowed */
    assert(!bot_reaction_can_fire(&state, 46));
    assert(state.acquire_ready_tick == 56 && state.visible_ready_tick == 61);
    observe(&state, 47, -1, false, false);
    observe(&state, 48, 101, true, true); /* same unit, brief acquisition loss */
    assert(state.acquire_ready_tick == 56 && state.visible_ready_tick == 61);
    observe(&state, 49, -1, false, true); /* invalid observation cannot authorize fire */
    assert(!bot_reaction_can_fire(&state, 100));
    observe(&state, 90, 101, true, true);
    assert(state.acquire_ready_tick == 100 && state.visible_ready_tick == 105);
    observe(&state, 91, 100, true, true); /* returning to an older target isn't instant */
    assert(state.acquire_ready_tick == 101);
}

static void zero_memory_reset_and_overflow(void)
{
    struct bot_reaction state;
    struct bot_reaction_observation value = observation(100, 10, true, true);
    bot_reaction_reset(&state);
    value.memory_ticks = 0;
    bot_reaction_observe(&state, &value);
    value.tick = 101; value.visible = false;
    bot_reaction_observe(&state, &value);
    value.tick = 102; value.visible = true;
    bot_reaction_observe(&state, &value);
    assert(state.visible_ready_tick == 117);
    value.tick = 0; /* restored/rewound simulation discards future timers */
    bot_reaction_observe(&state, &value);
    assert(state.acquire_ready_tick == 10 && state.visible_ready_tick == 15);
    assert(!bot_reaction_can_fire(&state, -1));
    value.tick = INT64_MAX - 1; value.target_id = 11;
    bot_reaction_observe(&state, &value);
    assert(state.acquire_ready_tick == INT64_MAX && state.visible_ready_tick == INT64_MAX);
    bot_reaction_reset(&state); /* respawn */
    assert(!bot_reaction_can_fire(&state, INT64_MAX));
    value.tick = 0; value.acquire_delay_ticks = value.visible_delay_ticks = 0;
    bot_reaction_observe(&state, &value);
    assert(bot_reaction_can_fire(&state, 0));
}

int main(void)
{
    profile_values();
    weapon_rows();
    initial_and_occlusion();
    identities_and_shared();
    zero_memory_reset_and_overflow();
    puts("bot profiles/reaction: timing, identity, memory and reset passed");
    return 0;
}
