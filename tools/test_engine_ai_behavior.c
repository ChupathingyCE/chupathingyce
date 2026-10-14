#include "engine_ai/behavior.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static void test_shapes(void)
{
    engine_ai_behavior_node nodes[] = {
        {ENGINE_AI_BEHAVIOR_PRIORITY, 1, 2, 0},
        {ENGINE_AI_BEHAVIOR_SEQUENCE, 3, 2, 0},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 1},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 2},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 3}
    };
    engine_ai_behavior_definition tree = {nodes, 5, 0};
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_VALID);
    nodes[1].child_first = 2; /* A shared child is not a tree instance. */
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_INVALID);
    nodes[1].child_first = 0; /* Cycle back to the root. */
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_INVALID);
    nodes[1].child_first = UINT32_MAX;
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_INVALID);
    nodes[1].child_first = 3;
    nodes[1].child_count = UINT32_MAX;
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_INVALID);
    nodes[1].child_count = 1; /* Node four is unreachable. */
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_INVALID);
    nodes[1].child_count = 2;
    nodes[2].leaf_id = 0;
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_INVALID);
    nodes[2].leaf_id = 1;
    nodes[0].kind = (engine_ai_behavior_kind)99;
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_INVALID);
}

static void test_limits(void)
{
    engine_ai_behavior_node nodes[ENGINE_AI_BEHAVIOR_NODE_MAX] = {0};
    engine_ai_behavior_definition tree = {nodes, ENGINE_AI_BEHAVIOR_NODE_MAX, 0};
    nodes[0] = (engine_ai_behavior_node){ENGINE_AI_BEHAVIOR_PRIORITY, 1,
        ENGINE_AI_BEHAVIOR_NODE_MAX - 1, 0};
    for (uint32_t index = 1; index < tree.node_count; index++) {
        nodes[index] = (engine_ai_behavior_node){ENGINE_AI_BEHAVIOR_LEAF, 0, 0, index};
    }
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_VALID);
    tree.node_count++;
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_INVALID);
    tree.node_count = ENGINE_AI_BEHAVIOR_DEPTH_MAX;
    for (uint32_t index = 0; index < tree.node_count; index++) {
        nodes[index] = (engine_ai_behavior_node){ENGINE_AI_BEHAVIOR_SEQUENCE, index + 1, 1, 0};
    }
    nodes[tree.node_count - 1] = (engine_ai_behavior_node){ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 1};
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_VALID);
    nodes[tree.node_count - 1] = (engine_ai_behavior_node){ENGINE_AI_BEHAVIOR_SEQUENCE,
        tree.node_count, 1, 0};
    tree.node_count++;
    nodes[tree.node_count - 1] = (engine_ai_behavior_node){ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 1};
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_TOO_DEEP);
    tree.root = tree.node_count;
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_INVALID);
    tree.root = 0;
    tree.node_count = 0;
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_INVALID);
    assert(engine_ai_behavior_validate(NULL) == ENGINE_AI_BEHAVIOR_INVALID);
    tree.nodes = NULL;
    tree.node_count = 1;
    assert(engine_ai_behavior_validate(&tree) == ENGINE_AI_BEHAVIOR_INVALID);
}

typedef struct leaf_fixture {
    engine_ai_behavior_result outcomes[4];
    uint32_t calls[4];
    uint32_t cancellations;
    bool relevant[ENGINE_AI_BEHAVIOR_NODE_MAX];
    uint32_t scans;
} leaf_fixture;

static engine_ai_behavior_result leaf_tick(void *context, uint32_t node, uint32_t leaf_id)
{
    leaf_fixture *fixture = context;
    assert(node < ENGINE_AI_BEHAVIOR_NODE_MAX);
    assert(leaf_id < 4);
    fixture->calls[leaf_id]++;
    return fixture->outcomes[leaf_id];
}

static void leaf_cancel(void *context, uint32_t node, uint32_t leaf_id)
{
    leaf_fixture *fixture = context;
    assert(node < ENGINE_AI_BEHAVIOR_NODE_MAX);
    assert(leaf_id < 4);
    fixture->cancellations++;
}

static void test_execution(void)
{
    engine_ai_behavior_node nodes[] = {
        {ENGINE_AI_BEHAVIOR_PRIORITY, 1, 2, 0},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 1},
        {ENGINE_AI_BEHAVIOR_SEQUENCE, 3, 2, 0},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 2},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 3}
    };
    engine_ai_behavior_definition tree = {nodes, 5, 0};
    engine_ai_behavior_state state;
    leaf_fixture fixture = {.outcomes = {ENGINE_AI_BEHAVIOR_SUCCESS,
        ENGINE_AI_BEHAVIOR_FAILURE, ENGINE_AI_BEHAVIOR_SUCCESS, ENGINE_AI_BEHAVIOR_RUNNING}};
    engine_ai_behavior_callbacks callbacks = {&fixture, leaf_tick, leaf_cancel, NULL};
    assert(engine_ai_behavior_begin(&state, &tree) == ENGINE_AI_BEHAVIOR_VALID);
    assert(engine_ai_behavior_step(&state, &tree, 0, &callbacks) == ENGINE_AI_BEHAVIOR_ERROR);
    for (uint32_t work = 0; work < 12; work++) {
        engine_ai_behavior_result result = engine_ai_behavior_step(&state, &tree, 1, &callbacks);
        if (result == ENGINE_AI_BEHAVIOR_RUNNING) break;
        assert(result == ENGINE_AI_BEHAVIOR_YIELDED);
    }
    assert(state.running_node == 4);
    assert(fixture.calls[1] == 1 && fixture.calls[2] == 1 && fixture.calls[3] == 1);
    engine_ai_behavior_state snapshot = state;
    fixture.outcomes[3] = ENGINE_AI_BEHAVIOR_SUCCESS;
    assert(engine_ai_behavior_step(&state, &tree, 16, &callbacks) == ENGINE_AI_BEHAVIOR_SUCCESS);
    assert(fixture.calls[2] == 1); /* Successful sequence prefix is retained. */
    state = snapshot; /* Pointer-free interpreter state can be restored field-wise. */
    assert(engine_ai_behavior_step(&state, &tree, 16, &callbacks) == ENGINE_AI_BEHAVIOR_SUCCESS);
    state = snapshot;
    assert(engine_ai_behavior_cancel(&state, &tree, &callbacks) == ENGINE_AI_BEHAVIOR_SUCCESS);
    assert(fixture.cancellations == 1);
    assert(engine_ai_behavior_cancel(&state, &tree, &callbacks) == ENGINE_AI_BEHAVIOR_SUCCESS);
    assert(fixture.cancellations == 1);
    fixture.outcomes[1] = ENGINE_AI_BEHAVIOR_SUCCESS;
    assert(engine_ai_behavior_step(&state, &tree, 16, &callbacks) == ENGINE_AI_BEHAVIOR_SUCCESS);
    assert(fixture.calls[2] == 1); /* Higher priority success prevents lower execution. */
    fixture.outcomes[1] = ENGINE_AI_BEHAVIOR_ERROR;
    assert(engine_ai_behavior_step(&state, &tree, 16, &callbacks) == ENGINE_AI_BEHAVIOR_ERROR);
    uint32_t calls = fixture.calls[1];
    assert(engine_ai_behavior_step(&state, &tree, 16, &callbacks) == ENGINE_AI_BEHAVIOR_ERROR);
    assert(fixture.calls[1] == calls);
    assert(engine_ai_behavior_cancel(&state, &tree, &callbacks) == ENGINE_AI_BEHAVIOR_SUCCESS);
    assert(fixture.cancellations == 2);
}

static bool branch_relevant(void *context, uint32_t node)
{
    leaf_fixture *fixture = context;
    assert(node < ENGINE_AI_BEHAVIOR_NODE_MAX);
    fixture->scans++;
    return fixture->relevant[node];
}

static void test_reevaluation(void)
{
    engine_ai_behavior_node nodes[] = {
        {ENGINE_AI_BEHAVIOR_PRIORITY, 1, 2, 0},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 1},
        {ENGINE_AI_BEHAVIOR_SEQUENCE, 3, 2, 0},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 2},
        {ENGINE_AI_BEHAVIOR_PRIORITY, 5, 2, 0},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 1},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 3}
    };
    engine_ai_behavior_definition tree = {nodes, 7, 0};
    engine_ai_behavior_state state;
    leaf_fixture fixture = {.outcomes = {ENGINE_AI_BEHAVIOR_SUCCESS,
        ENGINE_AI_BEHAVIOR_FAILURE, ENGINE_AI_BEHAVIOR_SUCCESS, ENGINE_AI_BEHAVIOR_RUNNING}};
    engine_ai_behavior_callbacks callbacks = {&fixture, leaf_tick, leaf_cancel, NULL};
    assert(engine_ai_behavior_begin(&state, &tree) == ENGINE_AI_BEHAVIOR_VALID);
    assert(engine_ai_behavior_step(&state, &tree, 32, &callbacks) == ENGINE_AI_BEHAVIOR_RUNNING);
    assert(state.running_node == 6);
    assert(engine_ai_behavior_reevaluate(&state, &tree, 1, branch_relevant,
        &callbacks) == ENGINE_AI_BEHAVIOR_YIELDED);
    assert(state.running_node == 6 && fixture.scans == 0 && fixture.cancellations == 0);
    assert(engine_ai_behavior_reevaluate(&state, &tree, 2, branch_relevant,
        &callbacks) == ENGINE_AI_BEHAVIOR_RUNNING);
    assert(fixture.scans == 2 && fixture.cancellations == 0);
    fixture.relevant[5] = true;
    assert(engine_ai_behavior_reevaluate(&state, &tree, 2, branch_relevant,
        &callbacks) == ENGINE_AI_BEHAVIOR_SUCCESS);
    assert(fixture.cancellations == 1 && fixture.calls[1] == 2);
    assert(state.depth == 3 && state.frames[2].child == 0);
    fixture.outcomes[1] = ENGINE_AI_BEHAVIOR_RUNNING;
    assert(engine_ai_behavior_step(&state, &tree, 32, &callbacks) == ENGINE_AI_BEHAVIOR_RUNNING);
    assert(state.running_node == 5 && fixture.calls[2] == 1);
    fixture.relevant[1] = true;
    assert(engine_ai_behavior_reevaluate(&state, &tree, 2, branch_relevant,
        &callbacks) == ENGINE_AI_BEHAVIOR_SUCCESS);
    assert(fixture.cancellations == 2 && state.depth == 1);
    assert(engine_ai_behavior_step(&state, &tree, 32, &callbacks) == ENGINE_AI_BEHAVIOR_RUNNING);
    assert(state.running_node == 1);
    assert(engine_ai_behavior_reevaluate(&state, &tree, 0, branch_relevant,
        &callbacks) == ENGINE_AI_BEHAVIOR_ERROR);
    assert(engine_ai_behavior_cancel(&state, &tree, &callbacks) == ENGINE_AI_BEHAVIOR_SUCCESS);
    assert(fixture.cancellations == 3);
    assert(engine_ai_behavior_reevaluate(&state, &tree, 2, branch_relevant,
        &callbacks) == ENGINE_AI_BEHAVIOR_ERROR);
}

static void test_trace(void)
{
    engine_ai_behavior_node nodes[] = {
        {ENGINE_AI_BEHAVIOR_PRIORITY, 1, 2, 0},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 1},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 2}
    };
    engine_ai_behavior_definition tree = {nodes, 3, 0};
    engine_ai_behavior_state state;
    leaf_fixture fixture = {.outcomes = {ENGINE_AI_BEHAVIOR_SUCCESS,
        ENGINE_AI_BEHAVIOR_FAILURE, ENGINE_AI_BEHAVIOR_RUNNING}};
    engine_ai_behavior_trace trace = {.capacity = ENGINE_AI_BEHAVIOR_TRACE_MAX};
    engine_ai_behavior_callbacks callbacks = {&fixture, leaf_tick, leaf_cancel, &trace};
    assert(engine_ai_behavior_begin(&state, &tree) == ENGINE_AI_BEHAVIOR_VALID);
    assert(engine_ai_behavior_step(&state, &tree, 16, &callbacks) == ENGINE_AI_BEHAVIOR_RUNNING);
    assert(trace.count == 5 && !trace.truncated);
    const engine_ai_behavior_trace_kind kinds[] = {
        ENGINE_AI_BEHAVIOR_TRACE_SELECT, ENGINE_AI_BEHAVIOR_TRACE_LEAF,
        ENGINE_AI_BEHAVIOR_TRACE_REJECT, ENGINE_AI_BEHAVIOR_TRACE_SELECT,
        ENGINE_AI_BEHAVIOR_TRACE_LEAF
    };
    for (uint32_t i = 0; i < trace.count; i++) {
        assert(trace.events[i].kind == kinds[i]);
        assert(trace.events[i].node == (i < 3 ? 1u : 2u));
    }
    assert(trace.events[1].result == ENGINE_AI_BEHAVIOR_FAILURE);
    assert(trace.events[4].result == ENGINE_AI_BEHAVIOR_RUNNING);
    assert(engine_ai_behavior_reevaluate(&state, &tree, 1, branch_relevant,
        &callbacks) == ENGINE_AI_BEHAVIOR_RUNNING);
    assert(trace.events[5].kind == ENGINE_AI_BEHAVIOR_TRACE_RELEVANCY);
    assert(trace.events[5].result == ENGINE_AI_BEHAVIOR_FAILURE);
    fixture.relevant[1] = true;
    assert(engine_ai_behavior_reevaluate(&state, &tree, 1, branch_relevant,
        &callbacks) == ENGINE_AI_BEHAVIOR_SUCCESS);
    assert(trace.events[6].result == ENGINE_AI_BEHAVIOR_SUCCESS);
    assert(trace.events[7].kind == ENGINE_AI_BEHAVIOR_TRACE_CANCEL);
    assert(trace.events[7].node == 2 && fixture.cancellations == 1);
    assert(trace.count == 8);
    trace.capacity = ENGINE_AI_BEHAVIOR_TRACE_MAX + 1;
    assert(engine_ai_behavior_step(&state, &tree, 16, &callbacks) == ENGINE_AI_BEHAVIOR_ERROR);
    assert(trace.count == 8); /* Invalid sink fails before callbacks or state changes. */
}

static void test_trace_overflow(void)
{
    engine_ai_behavior_node node = {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 1};
    engine_ai_behavior_definition tree = {&node, 1, 0};
    leaf_fixture fixture = {.outcomes = {ENGINE_AI_BEHAVIOR_SUCCESS,
        ENGINE_AI_BEHAVIOR_RUNNING}};
    engine_ai_behavior_trace trace = {.capacity = 1};
    engine_ai_behavior_callbacks callbacks = {&fixture, leaf_tick, leaf_cancel, &trace};
    engine_ai_behavior_state state;
    assert(engine_ai_behavior_begin(&state, &tree) == ENGINE_AI_BEHAVIOR_VALID);
    for (uint32_t i = 0; i < 3; i++) {
        assert(engine_ai_behavior_step(&state, &tree, 1, &callbacks) ==
            ENGINE_AI_BEHAVIOR_RUNNING);
    }
    assert(trace.count == 1 && trace.truncated && fixture.calls[1] == 3);
    assert(trace.events[0].kind == ENGINE_AI_BEHAVIOR_TRACE_LEAF);
    callbacks.trace = NULL;
    assert(engine_ai_behavior_step(&state, &tree, 1, &callbacks) == ENGINE_AI_BEHAVIOR_RUNNING);
    assert(trace.count == 1 && fixture.calls[1] == 4);
    callbacks.trace = &trace;
    trace = (engine_ai_behavior_trace){0}; /* Zero-capacity sink still executes. */
    assert(engine_ai_behavior_cancel(&state, &tree, &callbacks) == ENGINE_AI_BEHAVIOR_SUCCESS);
    assert(trace.count == 0 && trace.truncated && fixture.cancellations == 1);
    trace = (engine_ai_behavior_trace){.capacity = 1, .count = 2};
    assert(engine_ai_behavior_step(&state, &tree, 1, &callbacks) == ENGINE_AI_BEHAVIOR_ERROR);
    assert(fixture.calls[1] == 4);
}

static void test_trace_budget_and_fault(void)
{
    engine_ai_behavior_node nodes[] = {
        {ENGINE_AI_BEHAVIOR_PRIORITY, 1, 2, 0},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 1},
        {ENGINE_AI_BEHAVIOR_LEAF, 0, 0, 2}
    };
    engine_ai_behavior_definition tree = {nodes, 3, 0};
    engine_ai_behavior_state traced, plain;
    leaf_fixture a = {.outcomes = {ENGINE_AI_BEHAVIOR_SUCCESS,
        ENGINE_AI_BEHAVIOR_FAILURE, ENGINE_AI_BEHAVIOR_RUNNING}};
    leaf_fixture b = a;
    engine_ai_behavior_trace trace = {.capacity = ENGINE_AI_BEHAVIOR_TRACE_MAX};
    engine_ai_behavior_callbacks with_trace = {&a, leaf_tick, leaf_cancel, &trace};
    engine_ai_behavior_callbacks without_trace = {&b, leaf_tick, leaf_cancel, NULL};
    assert(engine_ai_behavior_begin(&traced, &tree) == ENGINE_AI_BEHAVIOR_VALID);
    assert(engine_ai_behavior_begin(&plain, &tree) == ENGINE_AI_BEHAVIOR_VALID);
    for (uint32_t i = 0; i < 6; i++) {
        engine_ai_behavior_result result = engine_ai_behavior_step(&traced, &tree, 1, &with_trace);
        assert(result == engine_ai_behavior_step(&plain, &tree, 1, &without_trace));
        assert(traced.depth == plain.depth && traced.pending == plain.pending);
        assert(traced.running_node == plain.running_node && traced.faulted == plain.faulted);
        for (uint32_t j = 0; j < traced.depth; j++) {
            assert(traced.frames[j].node == plain.frames[j].node);
            assert(traced.frames[j].child == plain.frames[j].child);
        }
        assert(a.calls[1] == b.calls[1] && a.calls[2] == b.calls[2]);
    }
    assert(trace.events[1].kind == ENGINE_AI_BEHAVIOR_TRACE_YIELD);
    assert(trace.events[1].node == ENGINE_AI_BEHAVIOR_NODE_MAX);
    a.outcomes[2] = (engine_ai_behavior_result)99;
    assert(engine_ai_behavior_step(&traced, &tree, 1, &with_trace) == ENGINE_AI_BEHAVIOR_ERROR);
    assert(trace.events[trace.count - 1].kind == ENGINE_AI_BEHAVIOR_TRACE_LEAF);
    assert(trace.events[trace.count - 1].result == ENGINE_AI_BEHAVIOR_ERROR);
    uint32_t count = trace.count;
    assert(engine_ai_behavior_step(&traced, &tree, 1, &with_trace) == ENGINE_AI_BEHAVIOR_ERROR);
    assert(trace.count == count);
}

int main(void)
{
    test_shapes();
    test_limits();
    test_execution();
    test_reevaluation();
    test_trace();
    test_trace_overflow();
    test_trace_budget_and_fault();
    puts("engine AI behavior definition tests passed");
    return 0;
}
