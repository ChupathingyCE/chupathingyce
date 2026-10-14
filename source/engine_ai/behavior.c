/* Validate before execution: one immutable tree, bounded depth and node count. */
#include "behavior.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

static bool node_valid(const engine_ai_behavior_node *node, uint32_t count)
{
    assert(node != NULL);
    assert(count <= ENGINE_AI_BEHAVIOR_NODE_MAX);
    if (node->kind == ENGINE_AI_BEHAVIOR_LEAF) {
        return node->leaf_id != 0 && node->child_first == 0 && node->child_count == 0;
    }
    if (node->kind != ENGINE_AI_BEHAVIOR_PRIORITY &&
        node->kind != ENGINE_AI_BEHAVIOR_SEQUENCE) {
        return false;
    }
    if (node->leaf_id != 0 || node->child_count == 0 || node->child_first >= count) {
        return false;
    }
    return node->child_count <= count - node->child_first;
}

engine_ai_behavior_status engine_ai_behavior_validate(
    const engine_ai_behavior_definition *definition)
{
    bool seen[ENGINE_AI_BEHAVIOR_NODE_MAX] = {false};
    uint32_t stack[ENGINE_AI_BEHAVIOR_DEPTH_MAX];
    uint32_t next_child[ENGINE_AI_BEHAVIOR_DEPTH_MAX] = {0};
    uint32_t depth = 1;
    uint32_t visited = 1;
    if (definition == NULL || definition->nodes == NULL || definition->node_count == 0 ||
        definition->node_count > ENGINE_AI_BEHAVIOR_NODE_MAX ||
        definition->root >= definition->node_count) {
        return ENGINE_AI_BEHAVIOR_INVALID;
    }
    for (uint32_t index = 0; index < definition->node_count; index++) {
        if (!node_valid(&definition->nodes[index], definition->node_count)) {
            return ENGINE_AI_BEHAVIOR_INVALID;
        }
    }
    stack[0] = definition->root;
    seen[definition->root] = true;
    /* A tree has at most N descents and N ascents. Repeated edges fail early. */
    for (uint32_t work = 0; work < 2u * ENGINE_AI_BEHAVIOR_NODE_MAX; work++) {
        assert(depth > 0);
        assert(depth <= ENGINE_AI_BEHAVIOR_DEPTH_MAX);
        uint32_t frame = depth - 1;
        const engine_ai_behavior_node *node = &definition->nodes[stack[frame]];
        if (next_child[frame] == node->child_count) {
            depth--;
            if (depth == 0) {
                return visited == definition->node_count ? ENGINE_AI_BEHAVIOR_VALID :
                    ENGINE_AI_BEHAVIOR_INVALID;
            }
            continue;
        }
        uint32_t child = node->child_first + next_child[frame];
        next_child[frame]++;
        assert(child < definition->node_count);
        if (seen[child]) return ENGINE_AI_BEHAVIOR_INVALID;
        if (depth == ENGINE_AI_BEHAVIOR_DEPTH_MAX) return ENGINE_AI_BEHAVIOR_TOO_DEEP;
        seen[child] = true;
        visited++;
        stack[depth] = child;
        next_child[depth] = 0;
        depth++;
    }
    assert(false); /* The validated node bound guarantees traversal termination. */
    return ENGINE_AI_BEHAVIOR_INVALID;
}

static void state_reset(engine_ai_behavior_state *state)
{
    *state = (engine_ai_behavior_state){0};
    state->running_node = ENGINE_AI_BEHAVIOR_NODE_MAX;
    state->initialized = 1;
}

engine_ai_behavior_status engine_ai_behavior_begin(
    engine_ai_behavior_state *state, const engine_ai_behavior_definition *definition)
{
    if (state == NULL) return ENGINE_AI_BEHAVIOR_INVALID;
    engine_ai_behavior_status status = engine_ai_behavior_validate(definition);
    if (status == ENGINE_AI_BEHAVIOR_VALID) state_reset(state);
    return status;
}

static void trace_append(const engine_ai_behavior_callbacks *callbacks,
    engine_ai_behavior_trace_kind kind, uint32_t node, engine_ai_behavior_result result)
{
    engine_ai_behavior_trace *trace = callbacks->trace;
    if (trace == NULL) return;
    assert(trace->count <= trace->capacity);
    assert(trace->capacity <= ENGINE_AI_BEHAVIOR_TRACE_MAX);
    if (trace->count == trace->capacity) {
        trace->truncated = true;
        return;
    }
    trace->events[trace->count++] = (engine_ai_behavior_trace_event){kind, node, result};
}

static bool execution_valid(const engine_ai_behavior_state *state,
    const engine_ai_behavior_definition *definition,
    const engine_ai_behavior_callbacks *callbacks)
{
    if (state == NULL || callbacks == NULL || callbacks->tick == NULL ||
        callbacks->cancel == NULL || state->initialized != 1 ||
        state->depth > ENGINE_AI_BEHAVIOR_DEPTH_MAX || state->pending > 2 ||
        engine_ai_behavior_validate(definition) != ENGINE_AI_BEHAVIOR_VALID) return false;
    if (callbacks->trace != NULL &&
        (callbacks->trace->capacity > ENGINE_AI_BEHAVIOR_TRACE_MAX ||
        callbacks->trace->count > callbacks->trace->capacity)) return false;
    if (state->running_node != ENGINE_AI_BEHAVIOR_NODE_MAX &&
        (state->running_node >= definition->node_count ||
        definition->nodes[state->running_node].kind != ENGINE_AI_BEHAVIOR_LEAF)) return false;
    for (uint32_t index = 0; index < state->depth; index++) {
        if (state->frames[index].node >= definition->node_count) return false;
        const engine_ai_behavior_node *node = &definition->nodes[state->frames[index].node];
        if (node->kind != ENGINE_AI_BEHAVIOR_LEAF &&
            state->frames[index].child >= node->child_count) return false;
        if (index == 0 && state->frames[index].node != definition->root) return false;
        if (index > 0) {
            const engine_ai_behavior_frame *parent = &state->frames[index - 1];
            const engine_ai_behavior_node *parent_node = &definition->nodes[parent->node];
            if (parent_node->kind == ENGINE_AI_BEHAVIOR_LEAF ||
                state->frames[index].node != parent_node->child_first + parent->child) return false;
        }
        if (state->pending != 0 && node->kind == ENGINE_AI_BEHAVIOR_LEAF) return false;
    }
    if (state->running_node != ENGINE_AI_BEHAVIOR_NODE_MAX &&
        (state->depth == 0 || state->pending != 0 ||
        state->frames[state->depth - 1].node != state->running_node)) return false;
    return true;
}

/* Propagate one terminal result, retaining it when the work budget ends. */
static void propagate_result(engine_ai_behavior_state *state,
    const engine_ai_behavior_definition *definition,
    const engine_ai_behavior_callbacks *callbacks)
{
    assert(state->depth > 0);
    assert(state->pending == 1 || state->pending == 2);
    engine_ai_behavior_frame *frame = &state->frames[state->depth - 1];
    const engine_ai_behavior_node *node = &definition->nodes[frame->node];
    bool advance = (node->kind == ENGINE_AI_BEHAVIOR_SEQUENCE && state->pending == 1) ||
        (node->kind == ENGINE_AI_BEHAVIOR_PRIORITY && state->pending == 2);
    if (advance) {
        if (node->kind == ENGINE_AI_BEHAVIOR_PRIORITY) {
            trace_append(callbacks, ENGINE_AI_BEHAVIOR_TRACE_REJECT,
                node->child_first + frame->child, ENGINE_AI_BEHAVIOR_FAILURE);
        }
        frame->child++;
        if (frame->child < node->child_count) {
            state->pending = 0;
            return;
        }
    }
    state->depth--;
}

static engine_ai_behavior_result tick_leaf(engine_ai_behavior_state *state,
    const engine_ai_behavior_node *node, uint32_t index,
    const engine_ai_behavior_callbacks *callbacks)
{
    engine_ai_behavior_result result = callbacks->tick(callbacks->context, index, node->leaf_id);
    engine_ai_behavior_result normalized = result;
    if (result != ENGINE_AI_BEHAVIOR_SUCCESS && result != ENGINE_AI_BEHAVIOR_FAILURE &&
        result != ENGINE_AI_BEHAVIOR_RUNNING) normalized = ENGINE_AI_BEHAVIOR_ERROR;
    trace_append(callbacks, ENGINE_AI_BEHAVIOR_TRACE_LEAF, index, normalized);
    if (result == ENGINE_AI_BEHAVIOR_RUNNING) {
        state->running_node = index;
        return result;
    }
    if (result != ENGINE_AI_BEHAVIOR_SUCCESS && result != ENGINE_AI_BEHAVIOR_FAILURE) {
        /* Preserve ownership even when a faulty callback acquired work. */
        state->running_node = index;
        state->faulted = 1;
        return ENGINE_AI_BEHAVIOR_ERROR;
    }
    state->running_node = ENGINE_AI_BEHAVIOR_NODE_MAX;
    state->depth--;
    state->pending = (uint32_t)result + 1;
    return result;
}

engine_ai_behavior_result engine_ai_behavior_step(
    engine_ai_behavior_state *state, const engine_ai_behavior_definition *definition,
    uint32_t work_budget, const engine_ai_behavior_callbacks *callbacks)
{
    if (!execution_valid(state, definition, callbacks) || work_budget == 0 ||
        work_budget > 2u * ENGINE_AI_BEHAVIOR_NODE_MAX || state->faulted != 0) {
        return ENGINE_AI_BEHAVIOR_ERROR;
    }
    if (state->depth == 0 && state->pending == 0) {
        state->frames[0] = (engine_ai_behavior_frame){definition->root, 0};
        state->depth = 1;
    }
    for (uint32_t work = 0; work < work_budget; work++) {
        if (state->depth == 0) {
            engine_ai_behavior_result result = (engine_ai_behavior_result)(state->pending - 1);
            state->pending = 0;
            return result;
        }
        if (state->pending != 0) {
            propagate_result(state, definition, callbacks);
            continue;
        }
        engine_ai_behavior_frame *frame = &state->frames[state->depth - 1];
        const engine_ai_behavior_node *node = &definition->nodes[frame->node];
        if (node->kind == ENGINE_AI_BEHAVIOR_LEAF) {
            engine_ai_behavior_result result = tick_leaf(state, node, frame->node, callbacks);
            if (result == ENGINE_AI_BEHAVIOR_RUNNING || result == ENGINE_AI_BEHAVIOR_ERROR) {
                return result;
            }
        } else {
            assert(frame->child < node->child_count);
            assert(state->depth < ENGINE_AI_BEHAVIOR_DEPTH_MAX);
            trace_append(callbacks, ENGINE_AI_BEHAVIOR_TRACE_SELECT,
                node->child_first + frame->child, ENGINE_AI_BEHAVIOR_RUNNING);
            state->frames[state->depth] = (engine_ai_behavior_frame){
                node->child_first + frame->child, 0};
            state->depth++;
        }
    }
    if (state->depth == 0) {
        engine_ai_behavior_result result = (engine_ai_behavior_result)(state->pending - 1);
        state->pending = 0;
        return result;
    }
    trace_append(callbacks, ENGINE_AI_BEHAVIOR_TRACE_YIELD,
        ENGINE_AI_BEHAVIOR_NODE_MAX, ENGINE_AI_BEHAVIOR_YIELDED);
    return ENGINE_AI_BEHAVIOR_YIELDED;
}

engine_ai_behavior_result engine_ai_behavior_cancel(
    engine_ai_behavior_state *state, const engine_ai_behavior_definition *definition,
    const engine_ai_behavior_callbacks *callbacks)
{
    if (!execution_valid(state, definition, callbacks)) return ENGINE_AI_BEHAVIOR_ERROR;
    if (state->running_node != ENGINE_AI_BEHAVIOR_NODE_MAX) {
        uint32_t index = state->running_node;
        trace_append(callbacks, ENGINE_AI_BEHAVIOR_TRACE_CANCEL,
            index, ENGINE_AI_BEHAVIOR_SUCCESS);
        callbacks->cancel(callbacks->context, index, definition->nodes[index].leaf_id);
    }
    state_reset(state);
    return ENGINE_AI_BEHAVIOR_SUCCESS;
}

engine_ai_behavior_result engine_ai_behavior_reevaluate(
    engine_ai_behavior_state *state, const engine_ai_behavior_definition *definition,
    uint32_t scan_budget, engine_ai_behavior_relevant relevant,
    const engine_ai_behavior_callbacks *callbacks)
{
    if (!execution_valid(state, definition, callbacks) || relevant == NULL ||
        scan_budget == 0 || scan_budget > ENGINE_AI_BEHAVIOR_NODE_MAX ||
        state->faulted != 0 || state->running_node == ENGINE_AI_BEHAVIOR_NODE_MAX) {
        return ENGINE_AI_BEHAVIOR_ERROR;
    }
    uint32_t candidates = 0;
    for (uint32_t level = 0; level < state->depth; level++) {
        const engine_ai_behavior_frame *frame = &state->frames[level];
        if (definition->nodes[frame->node].kind == ENGINE_AI_BEHAVIOR_PRIORITY) {
            candidates += frame->child;
        }
    }
    /* Earlier siblings on a single tree path are disjoint nodes. */
    assert(candidates < definition->node_count);
    if (candidates > scan_budget) {
        trace_append(callbacks, ENGINE_AI_BEHAVIOR_TRACE_YIELD,
            ENGINE_AI_BEHAVIOR_NODE_MAX, ENGINE_AI_BEHAVIOR_YIELDED);
        return ENGINE_AI_BEHAVIOR_YIELDED;
    }
    /* (a distinct name: the Halo build compiles this as gnu89, where a for
     * loop's declaration is in the enclosing scope) */
    for (uint32_t scan_level = 0; scan_level < state->depth; scan_level++) {
        engine_ai_behavior_frame *frame = &state->frames[scan_level];
        const engine_ai_behavior_node *node = &definition->nodes[frame->node];
        if (node->kind != ENGINE_AI_BEHAVIOR_PRIORITY) continue;
        for (uint32_t child = 0; child < frame->child; child++) {
            uint32_t index = node->child_first + child;
            assert(index < definition->node_count);
            bool eligible = relevant(callbacks->context, index);
            trace_append(callbacks, ENGINE_AI_BEHAVIOR_TRACE_RELEVANCY, index,
                eligible ? ENGINE_AI_BEHAVIOR_SUCCESS : ENGINE_AI_BEHAVIOR_FAILURE);
            if (!eligible) continue;
            uint32_t running = state->running_node;
            trace_append(callbacks, ENGINE_AI_BEHAVIOR_TRACE_CANCEL,
                running, ENGINE_AI_BEHAVIOR_SUCCESS);
            callbacks->cancel(callbacks->context, running, definition->nodes[running].leaf_id);
            frame->child = child;
            state->depth = scan_level + 1;
            state->running_node = ENGINE_AI_BEHAVIOR_NODE_MAX;
            state->pending = 0;
            return ENGINE_AI_BEHAVIOR_SUCCESS;
        }
    }
    return ENGINE_AI_BEHAVIOR_RUNNING;
}
