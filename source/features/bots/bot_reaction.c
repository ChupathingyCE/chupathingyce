#include "bot_reaction.h"

#include <assert.h>
#include <stddef.h>

static int64_t bot_reaction_deadline(int64_t tick, uint32_t delay)
{
    assert(tick >= 0);
    return (int64_t)delay > INT64_MAX - tick ? INT64_MAX : tick + (int64_t)delay;
}

static bool bot_reaction_recent(int64_t tick, int64_t previous, uint32_t memory)
{
    assert(tick >= 0);
    return tick >= previous && tick - previous < (int64_t)memory;
}

void bot_reaction_reset(struct bot_reaction *state)
{
    assert(state != NULL);
    *state = (struct bot_reaction){0};
    state->target_id = -1;
}

void bot_reaction_observe(struct bot_reaction *state,
    struct bot_reaction_observation const *observation)
{
    bool same;
    bool known;
    bool visible;
    int64_t tick;
    assert(state != NULL);
    assert(observation != NULL);
    assert(observation->tick >= 0);
    tick = observation->tick;
    if (state->initialized && tick < state->last_tick) {
        bot_reaction_reset(state);
    }
    same = state->initialized && state->target_id == observation->target_id;
    known = observation->known && observation->target_id >= 0;
    visible = known && observation->visible;
    if (known) {
        if (!same || !state->known) {
            if (!same || !bot_reaction_recent(tick, state->last_known_tick,
                observation->memory_ticks)) {
                state->acquire_ready_tick = bot_reaction_deadline(tick,
                    observation->acquire_delay_ticks);
            }
        }
        if (visible && (!same || !state->visible)) {
            if (!same || !state->seen || !bot_reaction_recent(tick,
                state->last_visible_tick, observation->memory_ticks)) {
                state->visible_ready_tick = bot_reaction_deadline(tick,
                    observation->visible_delay_ticks);
            }
            state->tracking_since_tick = tick;
        }
        if (!same) {
            state->seen = false;
        }
        state->target_id = observation->target_id;
        state->last_known_tick = tick;
        if (visible) {
            state->seen = true;
            state->last_visible_tick = tick;
        }
    }
    state->initialized = true;
    state->last_tick = tick;
    state->known = known;
    state->visible = visible;
}

bool bot_reaction_can_fire(struct bot_reaction const *state, int64_t tick)
{
    assert(state != NULL);
    return state->initialized && state->known && state->visible &&
        tick >= state->last_tick && tick >= state->acquire_ready_tick &&
        tick >= state->visible_ready_tick;
}
