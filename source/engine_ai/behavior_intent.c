/* Bridge leaf requests into bounded batches; game phases own validation/execution. */
#include "behavior_intent.h"

#include <assert.h>
#include <stddef.h>

struct engine_ai_behavior_intent_emitter {
    struct engine_ai_intent_batch *batch;
    enum engine_ai_behavior_intent_emit_status status;
    uint32_t active;
};

struct adapter_context {
    const struct engine_ai_behavior_intent_callbacks *callbacks;
    struct engine_ai_behavior_intent_emitter emitter;
};

void engine_ai_behavior_intent_state_init(
    struct engine_ai_behavior_intent_state *state)
{
    assert(state != NULL);
    *state = (struct engine_ai_behavior_intent_state){0};
    state->initialized = 1;
}

enum engine_ai_behavior_intent_emit_status engine_ai_behavior_intent_emit(
    struct engine_ai_behavior_intent_emitter *emitter, uint32_t action_kind,
    uint32_t schema_version, const void *payload, uint32_t payload_size)
{
    enum engine_ai_intent_status status;
    if (emitter == NULL) {
        return ENGINE_AI_BEHAVIOR_INTENT_EMIT_INVALID;
    }
    if (emitter->active == 0) {
        return ENGINE_AI_BEHAVIOR_INTENT_EMIT_DISABLED;
    }
    if (emitter->status != ENGINE_AI_BEHAVIOR_INTENT_EMIT_OK) {
        return emitter->status;
    }
    status = engine_ai_intent_append(emitter->batch, action_kind, schema_version,
        payload, payload_size);
    if (status == ENGINE_AI_INTENT_OK) {
        return ENGINE_AI_BEHAVIOR_INTENT_EMIT_OK;
    }
    emitter->status = status == ENGINE_AI_INTENT_FULL ?
        ENGINE_AI_BEHAVIOR_INTENT_EMIT_FULL : ENGINE_AI_BEHAVIOR_INTENT_EMIT_INVALID;
    return emitter->status;
}

static int batch_valid(const struct engine_ai_intent_batch *batch)
{
    return batch != NULL && batch->capacity > 0 &&
        batch->capacity <= ENGINE_AI_INTENTS_MAX && batch->count <= batch->capacity &&
        batch->consumed == 0 && batch->actor.index != ENGINE_AI_ACTOR_INDEX_INVALID;
}

static int state_valid(const struct engine_ai_behavior_intent_state *state,
    const struct engine_ai_intent_batch *batch)
{
    return state != NULL && state->initialized == 1 && state->branch_active <= 1 &&
        batch_valid(batch) && state->checkpoint_count <= batch->count;
}

static void begin_branch_if_needed(struct engine_ai_behavior_intent_state *state,
    const struct engine_ai_intent_batch *batch)
{
    assert(state_valid(state, batch));
    if (state->branch_active == 0) {
        state->checkpoint_count = batch->count;
        state->branch_active = 1;
    }
    assert(state->checkpoint_count <= batch->count);
}

static enum engine_ai_behavior_result tick_adapter(void *context, uint32_t node,
    uint32_t leaf_id)
{
    struct adapter_context *adapter = context;
    struct engine_ai_behavior_intent_emitter *emitter;
    engine_ai_behavior_result result;
    assert(adapter != NULL);
    emitter = &adapter->emitter;
    assert(emitter->active == 0);
    emitter->active = 1;
    result = adapter->callbacks->tick(adapter->callbacks->context, node, leaf_id, emitter);
    emitter->active = 0;
    if (emitter->status != ENGINE_AI_BEHAVIOR_INTENT_EMIT_OK) {
        return ENGINE_AI_BEHAVIOR_ERROR;
    }
    return result;
}

static void cancel_adapter(void *context, uint32_t node, uint32_t leaf_id)
{
    struct adapter_context *adapter = context;
    assert(adapter != NULL);
    adapter->callbacks->cancel(adapter->callbacks->context, node, leaf_id);
}

static bool relevant_adapter(void *context, uint32_t node)
{
    struct adapter_context *adapter = context;
    assert(adapter != NULL);
    return adapter->callbacks->relevant(adapter->callbacks->context, node);
}

static int callbacks_valid(const struct engine_ai_behavior_intent_callbacks *callbacks)
{
    return callbacks != NULL && callbacks->tick != NULL && callbacks->cancel != NULL &&
        (callbacks->trace == NULL ||
        (callbacks->trace->capacity <= ENGINE_AI_BEHAVIOR_TRACE_MAX &&
        callbacks->trace->count <= callbacks->trace->capacity));
}

static struct adapter_context adapter_initialize(
    const struct engine_ai_behavior_intent_callbacks *callbacks,
    struct engine_ai_intent_batch *batch)
{
    struct adapter_context adapter = {0};
    adapter.callbacks = callbacks;
    adapter.emitter.batch = batch;
    adapter.emitter.status = ENGINE_AI_BEHAVIOR_INTENT_EMIT_OK;
    return adapter;
}

static struct engine_ai_behavior_callbacks behavior_callbacks(
    struct adapter_context *adapter)
{
    struct engine_ai_behavior_callbacks callbacks = {
        adapter, tick_adapter, cancel_adapter, adapter->callbacks->trace
    };
    return callbacks;
}

static enum engine_ai_behavior_intent_status rollback_branch(
    struct engine_ai_behavior_intent_state *state,
    struct engine_ai_intent_batch *batch)
{
    enum engine_ai_intent_status status = engine_ai_intent_rollback(
        batch, state->checkpoint_count);
    if (status != ENGINE_AI_INTENT_OK) {
        return ENGINE_AI_BEHAVIOR_INTENT_INVALID;
    }
    assert(batch->count == state->checkpoint_count);
    return ENGINE_AI_BEHAVIOR_INTENT_OK;
}

static enum engine_ai_behavior_intent_status adapter_failure(
    const struct adapter_context *adapter)
{
    if (adapter->emitter.status == ENGINE_AI_BEHAVIOR_INTENT_EMIT_FULL) {
        return ENGINE_AI_BEHAVIOR_INTENT_FULL;
    }
    if (adapter->emitter.status == ENGINE_AI_BEHAVIOR_INTENT_EMIT_DISABLED) {
        return ENGINE_AI_BEHAVIOR_INTENT_EMISSION_DISABLED;
    }
    if (adapter->emitter.status == ENGINE_AI_BEHAVIOR_INTENT_EMIT_INVALID) {
        return ENGINE_AI_BEHAVIOR_INTENT_INVALID;
    }
    return ENGINE_AI_BEHAVIOR_INTENT_OK;
}

enum engine_ai_behavior_intent_status engine_ai_behavior_intent_step(
    struct engine_ai_behavior_intent_state *bridge_state,
    engine_ai_behavior_state *behavior_state,
    const engine_ai_behavior_definition *definition, uint32_t work_budget,
    const struct engine_ai_behavior_intent_callbacks *callbacks,
    struct engine_ai_intent_batch *batch, engine_ai_behavior_result *result)
{
    struct adapter_context adapter;
    struct engine_ai_behavior_callbacks wrapped;
    engine_ai_behavior_result outcome;
    enum engine_ai_behavior_intent_status failure;
    if (result == NULL || !state_valid(bridge_state, batch) || !callbacks_valid(callbacks)) {
        return ENGINE_AI_BEHAVIOR_INTENT_INVALID;
    }
    begin_branch_if_needed(bridge_state, batch);
    adapter = adapter_initialize(callbacks, batch);
    wrapped = behavior_callbacks(&adapter);
    outcome = engine_ai_behavior_step(behavior_state, definition, work_budget, &wrapped);
    failure = adapter_failure(&adapter);
    if (failure != ENGINE_AI_BEHAVIOR_INTENT_OK || outcome == ENGINE_AI_BEHAVIOR_ERROR) {
        enum engine_ai_behavior_intent_status rollback_status =
            rollback_branch(bridge_state, batch);
        if (rollback_status != ENGINE_AI_BEHAVIOR_INTENT_OK) {
            return rollback_status;
        }
        if (failure != ENGINE_AI_BEHAVIOR_INTENT_OK) {
            return failure;
        }
        return ENGINE_AI_BEHAVIOR_INTENT_BEHAVIOR_ERROR;
    }
    if (outcome == ENGINE_AI_BEHAVIOR_SUCCESS || outcome == ENGINE_AI_BEHAVIOR_FAILURE) {
        bridge_state->branch_active = 0;
        bridge_state->checkpoint_count = batch->count;
    }
    *result = outcome;
    return ENGINE_AI_BEHAVIOR_INTENT_OK;
}

enum engine_ai_behavior_intent_status engine_ai_behavior_intent_reevaluate(
    struct engine_ai_behavior_intent_state *bridge_state,
    engine_ai_behavior_state *behavior_state,
    const engine_ai_behavior_definition *definition, uint32_t scan_budget,
    const struct engine_ai_behavior_intent_callbacks *callbacks,
    struct engine_ai_intent_batch *batch, engine_ai_behavior_result *result)
{
    struct adapter_context adapter;
    struct engine_ai_behavior_callbacks wrapped;
    engine_ai_behavior_result outcome;
    if (result == NULL || !state_valid(bridge_state, batch) ||
        !callbacks_valid(callbacks) || callbacks->relevant == NULL) {
        return ENGINE_AI_BEHAVIOR_INTENT_INVALID;
    }
    begin_branch_if_needed(bridge_state, batch);
    adapter = adapter_initialize(callbacks, batch);
    wrapped = behavior_callbacks(&adapter);
    outcome = engine_ai_behavior_reevaluate(behavior_state, definition, scan_budget,
        relevant_adapter, &wrapped);
    if (outcome == ENGINE_AI_BEHAVIOR_ERROR) {
        return ENGINE_AI_BEHAVIOR_INTENT_BEHAVIOR_ERROR;
    }
    if (outcome == ENGINE_AI_BEHAVIOR_SUCCESS) {
        enum engine_ai_behavior_intent_status status =
            rollback_branch(bridge_state, batch);
        if (status != ENGINE_AI_BEHAVIOR_INTENT_OK) {
            return status;
        }
        bridge_state->branch_active = 1;
    }
    *result = outcome;
    return ENGINE_AI_BEHAVIOR_INTENT_OK;
}

enum engine_ai_behavior_intent_status engine_ai_behavior_intent_cancel(
    struct engine_ai_behavior_intent_state *bridge_state,
    engine_ai_behavior_state *behavior_state,
    const engine_ai_behavior_definition *definition,
    const struct engine_ai_behavior_intent_callbacks *callbacks,
    struct engine_ai_intent_batch *batch)
{
    struct adapter_context adapter;
    struct engine_ai_behavior_callbacks wrapped;
    engine_ai_behavior_result outcome;
    if (!state_valid(bridge_state, batch) || !callbacks_valid(callbacks)) {
        return ENGINE_AI_BEHAVIOR_INTENT_INVALID;
    }
    begin_branch_if_needed(bridge_state, batch);
    adapter = adapter_initialize(callbacks, batch);
    wrapped = behavior_callbacks(&adapter);
    outcome = engine_ai_behavior_cancel(behavior_state, definition, &wrapped);
    if (outcome != ENGINE_AI_BEHAVIOR_SUCCESS) {
        return ENGINE_AI_BEHAVIOR_INTENT_BEHAVIOR_ERROR;
    }
    if (rollback_branch(bridge_state, batch) != ENGINE_AI_BEHAVIOR_INTENT_OK) {
        return ENGINE_AI_BEHAVIOR_INTENT_INVALID;
    }
    bridge_state->branch_active = 0;
    bridge_state->checkpoint_count = batch->count;
    return ENGINE_AI_BEHAVIOR_INTENT_OK;
}

static uint32_t rng32_next_unchecked(struct engine_ai_rng32_state *state)
{
    uint64_t previous = state->state;
    uint32_t value;
    uint32_t rotation;
    state->state = previous * UINT64_C(6364136223846793005) + state->increment;
    value = (uint32_t)(((previous >> 18) ^ previous) >> 27);
    rotation = (uint32_t)(previous >> 59);
    return (value >> rotation) | (value << ((0u - rotation) & 31u));
}

enum engine_ai_rng32_status engine_ai_rng32_seed(
    struct engine_ai_rng32_state *state, uint64_t seed, uint64_t stream)
{
    if (state == NULL) {
        return ENGINE_AI_RNG32_INVALID;
    }
    state->state = 0;
    state->increment = (stream << 1) | UINT64_C(1);
    (void)rng32_next_unchecked(state);
    state->state += seed;
    (void)rng32_next_unchecked(state);
    assert((state->increment & UINT64_C(1)) == UINT64_C(1));
    return ENGINE_AI_RNG32_OK;
}

enum engine_ai_rng32_status engine_ai_rng32_next(
    struct engine_ai_rng32_state *state, uint32_t *value)
{
    uint32_t next;
    if (state == NULL || value == NULL ||
        (state->increment & UINT64_C(1)) == UINT64_C(0)) {
        return ENGINE_AI_RNG32_INVALID;
    }
    next = rng32_next_unchecked(state);
    *value = next;
    return ENGINE_AI_RNG32_OK;
}

enum engine_ai_rng32_status engine_ai_rng32_bounded(
    struct engine_ai_rng32_state *state, uint32_t bound, uint32_t *value)
{
    uint32_t next;
    if (bound == 0 || value == NULL ||
        engine_ai_rng32_next(state, &next) != ENGINE_AI_RNG32_OK) {
        return ENGINE_AI_RNG32_INVALID;
    }
    *value = next % bound;
    return ENGINE_AI_RNG32_OK;
}
