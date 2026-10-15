/* Tick-local decision output. Action schemas and validation remain game-owned. */
#ifndef ENGINE_AI_INTENT_H
#define ENGINE_AI_INTENT_H

#include <stdint.h>

/* Game-owned actor identity: a slot index plus a generation that distinguishes
 * reuse of the slot (in Halo, a datum's absolute index and its identifier). */
#define ENGINE_AI_ACTOR_INDEX_INVALID ((uint32_t)0xFFFFFFFFu)
struct engine_ai_actor_id {
    uint32_t index;
    uint32_t generation;
};

/* The simulation tick an intent belongs to. Epoch distinguishes worlds (a new
 * map or game); revision scopes snapshot provenance, not identity. */
struct engine_ai_tick_id {
    uint64_t epoch;
    uint64_t tick;
};
struct engine_ai_tick_context {
    struct engine_ai_tick_id id;
    uint64_t revision_id;
};

enum {
    ENGINE_AI_INTENTS_MAX = 32,
    ENGINE_AI_INTENT_PAYLOAD_BYTES_MAX = 64
};

/* Field-wise identity, not a hash. Revision is deliberately excluded so replay
 * reproduces IDs. Ordinals follow deterministic per-actor decision order.
 * Actor generations distinguish reused entity slots; epoch distinguishes worlds. */
struct engine_ai_intent_id {
    struct engine_ai_tick_id tick;
    struct engine_ai_actor_id actor;
    uint32_t ordinal;
};

struct engine_ai_intent {
    struct engine_ai_intent_id id;
    uint32_t action_kind;
    uint32_t schema_version;
    uint32_t payload_size;
    uint8_t payload[ENGINE_AI_INTENT_PAYLOAD_BYTES_MAX];
};

enum engine_ai_intent_status {
    ENGINE_AI_INTENT_OK = 0,
    ENGINE_AI_INTENT_INVALID,
    ENGINE_AI_INTENT_FULL,
    ENGINE_AI_INTENT_CONSUMED,
    ENGINE_AI_INTENT_STALE,
    ENGINE_AI_INTENT_EXECUTOR_FAILED
};

enum engine_ai_intent_outcome {
    ENGINE_AI_INTENT_ACCEPTED = 1,
    ENGINE_AI_INTENT_REJECTED = 2
};
struct engine_ai_intent_result {
    enum engine_ai_intent_outcome outcome;
    uint32_t reason; /* Game-owned rejection code; zero on acceptance. */
};
struct engine_ai_intent_report {
    uint32_t count;
    struct engine_ai_intent_result results[ENGINE_AI_INTENTS_MAX];
};

/* One caller-owned batch per actor per tick. Treat fields as private; begin must
 * precede use. No pointers persist in the batch. Payloads are copied bytes, not
 * pointer-bearing objects; decode with memcpy, never cast for alignment.
 * Batch, report, tick context, callback context and payload input must not
 * overlap. No raw-struct serialization/hashing: encode fields explicitly and
 * initialize payload padding if using local struct copies.
 * Single-owner, non-reentrant. No heap, world writes, or scheduling in append. */
struct engine_ai_intent_batch {
    struct engine_ai_tick_id tick;
    uint64_t revision_id;
    struct engine_ai_actor_id actor;
    uint32_t capacity;
    uint32_t count;
    uint32_t consumed;
    struct engine_ai_intent intents[ENGINE_AI_INTENTS_MAX];
};

/* Called synchronously in append order against current world state. The game
 * must validate actor generation, targets, authority, resources and schema,
 * then execute using the same rules as player actions. Rejection must not mutate
 * state. Acceptance means committed simulation state, not an external effect.
 * Intent pointer is borrowed only for the callback; do not retain it.
 * Callback must not touch batch/report or recurse into execution. Invalid return
 * values fault execution: prior accepted actions are NOT rolled back. Caller
 * must stop the simulation tick and restore a known-good world before retrying. */
typedef struct engine_ai_intent_result (*engine_ai_intent_execute_fn)(
    void *context, const struct engine_ai_intent *intent);

/* Non-null batch/report are programmer contracts. begin clears batch even on
 * failure. Actor index must be valid; liveness is checked by the executor.
 * Capacity is 1..INTENTS_MAX. Revision scopes snapshot provenance, not identity. */
enum engine_ai_intent_status engine_ai_intent_begin(struct engine_ai_intent_batch *batch,
    struct engine_ai_actor_id actor, const struct engine_ai_tick_context *tick,
    uint32_t capacity);

/* Nonzero game-owned kind/version, payload_size <= PAYLOAD_BYTES_MAX. NULL payload
 * is valid only for size zero. Failure leaves batch unchanged; FULL is explicit.
 * A producer must abandon/rebuild an incomplete required sequence; execution is
 * per intent, not an atomic transaction. No implicit retry, replacement or expiry. */
enum engine_ai_intent_status engine_ai_intent_append(struct engine_ai_intent_batch *batch,
    uint32_t action_kind, uint32_t schema_version, const void *payload, uint32_t payload_size);

/* Discard an unexecuted suffix back to a saved count. Existing prefix intents and
 * provenance remain unchanged. Consumed batches cannot be rewound. */
enum engine_ai_intent_status engine_ai_intent_rollback(
    struct engine_ai_intent_batch *batch, uint32_t retained_count);

/* Exact epoch/tick/revision match required; stale execution consumes without
 * callbacks. Valid execution consumes even an empty batch, once per instance.
 * Report is cleared on entry; count includes only valid callback results.
 * Rejection is an ordinary result, not a batch failure; subsequent intents run.
 * Copied/rebuilt batches are NOT globally deduplicated. Caller schedules exactly
 * one batch per actor/tick in stable actor order. On rollback, restore world and
 * decision state together, discard derived batches/reports, then regenerate.
 * The executor must buffer external effects through the timeline, not emit them.
 * Work is <= INTENTS_MAX callbacks; callback work must itself be bounded. */
enum engine_ai_intent_status engine_ai_intent_execute(struct engine_ai_intent_batch *batch,
    const struct engine_ai_tick_context *tick, struct engine_ai_intent_report *report,
    void *context, engine_ai_intent_execute_fn execute);

#endif
