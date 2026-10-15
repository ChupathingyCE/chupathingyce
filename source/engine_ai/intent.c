/* Bounded per-actor output, consumed independently of game action success. */
#include "intent.h"

#include <assert.h>
#include <string.h>

static int batch_valid(const struct engine_ai_intent_batch *batch)
{
    return batch->capacity > 0 && batch->capacity <= ENGINE_AI_INTENTS_MAX &&
        batch->count <= batch->capacity && batch->consumed <= 1 &&
        batch->actor.index != ENGINE_AI_ACTOR_INDEX_INVALID;
}

enum engine_ai_intent_status engine_ai_intent_begin(struct engine_ai_intent_batch *batch,
    struct engine_ai_actor_id actor, const struct engine_ai_tick_context *tick,
    uint32_t capacity)
{
    assert(batch != NULL);
    memset(batch, 0, sizeof(*batch));
    if (tick == NULL || actor.index == ENGINE_AI_ACTOR_INDEX_INVALID || capacity == 0 ||
        capacity > ENGINE_AI_INTENTS_MAX) {
        return ENGINE_AI_INTENT_INVALID;
    }
    batch->tick = tick->id;
    batch->revision_id = tick->revision_id;
    batch->actor = actor;
    batch->capacity = capacity;
    assert(batch_valid(batch));
    return ENGINE_AI_INTENT_OK;
}

enum engine_ai_intent_status engine_ai_intent_append(struct engine_ai_intent_batch *batch,
    uint32_t action_kind, uint32_t schema_version, const void *payload, uint32_t payload_size)
{
    assert(batch != NULL);
    if (!batch_valid(batch) || action_kind == 0 || schema_version == 0 ||
        payload_size > ENGINE_AI_INTENT_PAYLOAD_BYTES_MAX ||
        (payload_size != 0 && payload == NULL)) {
        return ENGINE_AI_INTENT_INVALID;
    }
    if (batch->consumed != 0) {
        return ENGINE_AI_INTENT_CONSUMED;
    }
    if (batch->count == batch->capacity) {
        return ENGINE_AI_INTENT_FULL;
    }
    assert(batch->count < ENGINE_AI_INTENTS_MAX);
    struct engine_ai_intent *intent = &batch->intents[batch->count];
    memset(intent, 0, sizeof(*intent));
    intent->id.tick = batch->tick;
    intent->id.actor = batch->actor;
    intent->id.ordinal = batch->count;
    intent->action_kind = action_kind;
    intent->schema_version = schema_version;
    intent->payload_size = payload_size;
    if (payload_size != 0) {
        memcpy(intent->payload, payload, payload_size);
    }
    batch->count++;
    assert(batch->count <= batch->capacity);
    return ENGINE_AI_INTENT_OK;
}

enum engine_ai_intent_status engine_ai_intent_rollback(
    struct engine_ai_intent_batch *batch, uint32_t retained_count)
{
    assert(batch != NULL);
    if (!batch_valid(batch) || retained_count > batch->count) {
        return ENGINE_AI_INTENT_INVALID;
    }
    if (batch->consumed != 0) {
        return ENGINE_AI_INTENT_CONSUMED;
    }
    for (uint32_t index = retained_count; index < batch->count; index++) {
        memset(&batch->intents[index], 0, sizeof(batch->intents[index]));
    }
    batch->count = retained_count;
    assert(batch->count <= batch->capacity);
    return ENGINE_AI_INTENT_OK;
}

enum engine_ai_intent_status engine_ai_intent_execute(struct engine_ai_intent_batch *batch,
    const struct engine_ai_tick_context *tick, struct engine_ai_intent_report *report,
    void *context, engine_ai_intent_execute_fn execute)
{
    assert(batch != NULL);
    assert(report != NULL);
    memset(report, 0, sizeof(*report));
    if (!batch_valid(batch) || tick == NULL || execute == NULL) {
        return ENGINE_AI_INTENT_INVALID;
    }
    if (batch->consumed != 0) {
        return ENGINE_AI_INTENT_CONSUMED;
    }
    batch->consumed = 1;
    if (batch->tick.epoch != tick->id.epoch || batch->tick.tick != tick->id.tick ||
        batch->revision_id != tick->revision_id) {
        return ENGINE_AI_INTENT_STALE;
    }
    assert(batch->count <= ENGINE_AI_INTENTS_MAX);
    for (uint32_t index = 0; index < batch->count; index++) {
        struct engine_ai_intent_result result = execute(context, &batch->intents[index]);
        if ((result.outcome != ENGINE_AI_INTENT_ACCEPTED &&
             result.outcome != ENGINE_AI_INTENT_REJECTED) ||
            (result.outcome == ENGINE_AI_INTENT_ACCEPTED && result.reason != 0)) {
            return ENGINE_AI_INTENT_EXECUTOR_FAILED;
        }
        report->results[index] = result;
        report->count++;
    }
    assert(report->count == batch->count);
    return ENGINE_AI_INTENT_OK;
}
