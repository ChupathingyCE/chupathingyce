#include "engine_ai/claims.h"

#include <assert.h>
#include <string.h>

static bool claim_live(const struct engine_ai_claim *claim, uint64_t now_tick)
{
    return claim->expires_tick != 0 && now_tick < claim->expires_tick;
}

void engine_ai_claims_reset(struct engine_ai_claims *table)
{
    assert(table);
    memset(table, 0, sizeof(*table));
}

/* Each scope owns a full fixed-capacity table, independent of other teams. */
bool engine_ai_claims_acquire(struct engine_ai_claims *table, uint32_t scope,
    uint32_t object, uint32_t owner, uint64_t now_tick, uint64_t duration_ticks)
{
    uint32_t index;
    uint32_t free_index = ENGINE_AI_CLAIMS_MAX;

    assert(table);
    if (scope >= ENGINE_AI_CLAIM_SCOPES_MAX) return false;
    if (object == ENGINE_AI_CLAIM_OBJECT_NONE || owner == 0 || duration_ticks == 0 ||
        now_tick > UINT64_MAX - duration_ticks) return false;
    for (index = 0; index < ENGINE_AI_CLAIMS_MAX; index++) {
        struct engine_ai_claim *claim = &table->claims[scope][index];
        if (!claim_live(claim, now_tick)) {
            if (free_index == ENGINE_AI_CLAIMS_MAX) free_index = index;
            continue;
        }
        if (claim->object != object) continue;
        if (claim->owner != owner) return false;
        claim->expires_tick = now_tick + duration_ticks;
        return true;
    }
    if (free_index == ENGINE_AI_CLAIMS_MAX) return false;
    table->claims[scope][free_index].object = object;
    table->claims[scope][free_index].owner = owner;
    table->claims[scope][free_index].expires_tick = now_tick + duration_ticks;
    return true;
}

bool engine_ai_claims_held_by_other(const struct engine_ai_claims *table,
    uint32_t scope, uint32_t object, uint32_t owner, uint64_t now_tick)
{
    uint32_t index;

    assert(table);
    if (scope >= ENGINE_AI_CLAIM_SCOPES_MAX || object == ENGINE_AI_CLAIM_OBJECT_NONE ||
        owner == 0) return false;
    for (index = 0; index < ENGINE_AI_CLAIMS_MAX; index++) {
        const struct engine_ai_claim *claim = &table->claims[scope][index];
        if (claim_live(claim, now_tick) && claim->object == object && claim->owner != owner)
            return true;
    }
    return false;
}

void engine_ai_claims_release(struct engine_ai_claims *table, uint32_t scope,
    uint32_t object, uint32_t owner)
{
    uint32_t index;

    assert(table);
    if (scope >= ENGINE_AI_CLAIM_SCOPES_MAX) return;
    for (index = 0; index < ENGINE_AI_CLAIMS_MAX; index++) {
        struct engine_ai_claim *claim = &table->claims[scope][index];
        if (claim->expires_tick != 0 && claim->object == object && claim->owner == owner)
            memset(claim, 0, sizeof(*claim));
    }
}

void engine_ai_claims_release_owner(struct engine_ai_claims *table,
    uint32_t scope, uint32_t owner)
{
    uint32_t index;

    assert(table);
    if (scope >= ENGINE_AI_CLAIM_SCOPES_MAX) return;
    for (index = 0; index < ENGINE_AI_CLAIMS_MAX; index++) {
        struct engine_ai_claim *claim = &table->claims[scope][index];
        if (claim->expires_tick != 0 && claim->owner == owner)
            memset(claim, 0, sizeof(*claim));
    }
}
