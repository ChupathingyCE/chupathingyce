/* Exclusive, expiring claims on world objects: one owner per item in each
 * scope. Each scope has independent fixed capacity. A claim says nothing about
 * whether another scope or a human may take the object. */
#ifndef ENGINE_AI_CLAIMS_H
#define ENGINE_AI_CLAIMS_H

#include <stdbool.h>
#include <stdint.h>

/* FFA uses scope 0; Halo's two teams use scopes 0 and 1. */
enum { ENGINE_AI_CLAIMS_MAX = 32, ENGINE_AI_CLAIM_SCOPES_MAX = 2 };
#define ENGINE_AI_CLAIM_OBJECT_NONE UINT32_MAX

struct engine_ai_claim {
    uint32_t object;
    uint32_t owner;
    uint64_t expires_tick; /* 0: free */
};

struct engine_ai_claims {
    struct engine_ai_claim claims[ENGINE_AI_CLAIM_SCOPES_MAX][ENGINE_AI_CLAIMS_MAX];
};

void engine_ai_claims_reset(struct engine_ai_claims *table);

/* True if the object is unclaimed, its claim has expired, or owner already
 * holds it (the claim is renewed). False if another owner holds it, the
 * duration is zero, or every slot holds a live claim. */
bool engine_ai_claims_acquire(struct engine_ai_claims *table, uint32_t scope,
    uint32_t object, uint32_t owner, uint64_t now_tick, uint64_t duration_ticks);

/* True if a live claim on object belongs to someone other than owner. */
bool engine_ai_claims_held_by_other(const struct engine_ai_claims *table,
    uint32_t scope, uint32_t object, uint32_t owner, uint64_t now_tick);

void engine_ai_claims_release(struct engine_ai_claims *table, uint32_t scope,
    uint32_t object, uint32_t owner);
/* Releases every claim the owner holds, live or expired. */
void engine_ai_claims_release_owner(struct engine_ai_claims *table,
    uint32_t scope, uint32_t owner);

#endif
