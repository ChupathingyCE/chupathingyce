/* Heap-free adapters from bounded behavior leaves to tick-local intent batches. */
#ifndef ENGINE_AI_BEHAVIOR_INTENT_H
#define ENGINE_AI_BEHAVIOR_INTENT_H

#include "behavior.h"
#include "intent.h"

#include <stdbool.h>
#include <stdint.h>

/* This opaque capability is valid only during a leaf tick callback. Relevancy,
 * cancellation, and game action execution receive no emitter. */
struct engine_ai_behavior_intent_emitter;

enum engine_ai_behavior_intent_emit_status {
    ENGINE_AI_BEHAVIOR_INTENT_EMIT_OK = 0,
    ENGINE_AI_BEHAVIOR_INTENT_EMIT_INVALID,
    ENGINE_AI_BEHAVIOR_INTENT_EMIT_FULL,
    ENGINE_AI_BEHAVIOR_INTENT_EMIT_DISABLED
};

enum engine_ai_behavior_intent_status {
    ENGINE_AI_BEHAVIOR_INTENT_OK = 0,
    ENGINE_AI_BEHAVIOR_INTENT_INVALID,
    ENGINE_AI_BEHAVIOR_INTENT_FULL,
    ENGINE_AI_BEHAVIOR_INTENT_EMISSION_DISABLED,
    ENGINE_AI_BEHAVIOR_INTENT_BEHAVIOR_ERROR
};

typedef engine_ai_behavior_result (*engine_ai_behavior_intent_tick_fn)(
    void *context, uint32_t node, uint32_t leaf_id,
    struct engine_ai_behavior_intent_emitter *emitter);
typedef void (*engine_ai_behavior_intent_cancel_fn)(
    void *context, uint32_t node, uint32_t leaf_id);
typedef bool (*engine_ai_behavior_intent_relevant_fn)(void *context, uint32_t node);

struct engine_ai_behavior_intent_callbacks {
    void *context;
    engine_ai_behavior_intent_tick_fn tick;
    engine_ai_behavior_intent_cancel_fn cancel;
    engine_ai_behavior_intent_relevant_fn relevant;
    engine_ai_behavior_trace *trace;
};

/* One ephemeral checkpoint per actor/tick batch; contains no pointers. This is
 * derived output bookkeeping, not authoritative simulation state. Initialize it
 * once after beginning each fresh batch and after rollback/restore. */
struct engine_ai_behavior_intent_state {
    uint32_t checkpoint_count;
    uint32_t initialized;
    uint32_t branch_active;
};

void engine_ai_behavior_intent_state_init(
    struct engine_ai_behavior_intent_state *state);

/* Appends a copied, game-owned action request to this actor's current batch.
 * FULL is explicit; the behavior step faults and retains its running leaf, and
 * the bridge discards all unexecuted intents emitted by that branch. */
enum engine_ai_behavior_intent_emit_status engine_ai_behavior_intent_emit(
    struct engine_ai_behavior_intent_emitter *emitter, uint32_t action_kind,
    uint32_t schema_version, const void *payload, uint32_t payload_size);

/* result is written only when status is OK. Initialize bridge_state after begin
 * for each fresh actor/tick batch; repeated budget-one calls may share it. On
 * branch interruption/cancel, the bridge releases the running leaf first and
 * rolls back that branch's unexecuted intent suffix. Execute only after decision
 * completes. Any non-OK status faults the caller's phase: do not execute batch. */
enum engine_ai_behavior_intent_status engine_ai_behavior_intent_step(
    struct engine_ai_behavior_intent_state *bridge_state,
    engine_ai_behavior_state *behavior_state,
    const engine_ai_behavior_definition *definition, uint32_t work_budget,
    const struct engine_ai_behavior_intent_callbacks *callbacks,
    struct engine_ai_intent_batch *batch, engine_ai_behavior_result *result);
enum engine_ai_behavior_intent_status engine_ai_behavior_intent_reevaluate(
    struct engine_ai_behavior_intent_state *bridge_state,
    engine_ai_behavior_state *behavior_state,
    const engine_ai_behavior_definition *definition, uint32_t scan_budget,
    const struct engine_ai_behavior_intent_callbacks *callbacks,
    struct engine_ai_intent_batch *batch, engine_ai_behavior_result *result);
enum engine_ai_behavior_intent_status engine_ai_behavior_intent_cancel(
    struct engine_ai_behavior_intent_state *bridge_state,
    engine_ai_behavior_state *behavior_state,
    const engine_ai_behavior_definition *definition,
    const struct engine_ai_behavior_intent_callbacks *callbacks,
    struct engine_ai_intent_batch *batch);

/* Small non-cryptographic PCG-XSH-RR generator. Both words are game-owned,
 * pointer-free state; snapshot/restore their fields explicitly. Bounded choices
 * use one draw modulo bound (deterministic, with documented modulo bias). */
struct engine_ai_rng32_state {
    uint64_t state;
    uint64_t increment;
};
enum engine_ai_rng32_status {
    ENGINE_AI_RNG32_OK = 0,
    ENGINE_AI_RNG32_INVALID
};
enum engine_ai_rng32_status engine_ai_rng32_seed(
    struct engine_ai_rng32_state *state, uint64_t seed, uint64_t stream);
enum engine_ai_rng32_status engine_ai_rng32_next(
    struct engine_ai_rng32_state *state, uint32_t *value);
enum engine_ai_rng32_status engine_ai_rng32_bounded(
    struct engine_ai_rng32_state *state, uint32_t bound, uint32_t *value);

#endif
