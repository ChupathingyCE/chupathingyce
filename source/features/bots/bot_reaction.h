/* Acquisition and visibility reaction timers with identity-scoped memory.
 * Recovered roles: halo-rea/readable/bots/BehaviorEvaluators.cpp section 2.
 * Caller supplies observed target identity, tick and profile-sampled delays. */
#ifndef HALO_BOT_REACTION_H
#define HALO_BOT_REACTION_H

#include <stdbool.h>
#include <stdint.h>

struct bot_reaction
{
    int64_t target_id; /* salted unit handle, not a player slot */
    int64_t last_tick;
    int64_t last_known_tick;
    int64_t last_visible_tick;
    int64_t acquire_ready_tick;
    int64_t visible_ready_tick;
    int64_t tracking_since_tick;
    bool initialized;
    bool known;
    bool visible;
    bool seen;
};

struct bot_reaction_observation
{
    int64_t tick; /* nonnegative simulation tick */
    int64_t target_id;
    uint32_t acquire_delay_ticks;
    uint32_t visible_delay_ticks;
    uint32_t memory_ticks;
    bool known;
    bool visible; /* ignored unless known */
};

/* A new life/map must reset. Rewound time also resets on observation. */
void bot_reaction_reset(struct bot_reaction *state);

/* O(1), no allocation or RNG. Timers are never shortened by brief occlusion.
 * Memory is strictly younger than memory_ticks, and only for the same unit.
 * A newly visible identity always gets its own visibility timer, even if a
 * shared sighting or damage report had already acquired that target. */
void bot_reaction_observe(struct bot_reaction *state,
    struct bot_reaction_observation const *observation);

bool bot_reaction_can_fire(struct bot_reaction const *state, int64_t tick);

#endif
