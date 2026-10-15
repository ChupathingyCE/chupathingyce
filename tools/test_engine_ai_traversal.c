#include "engine_ai/navigation.h"
#include "engine_ai/traversal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct game_policy {
    uint32_t available;
    uint32_t reserve_denials;
    uint32_t reserve_calls;
    uint32_t execute_calls;
    uint32_t release_calls;
    uint32_t next_reservation;
    uint32_t last_release_reason;
    uint64_t last_reservation;
    enum engine_ai_traversal_execution_result execution_result;
};

static enum engine_ai_traversal_availability policy_availability(void *context,
    uint32_t traversal_id, uint32_t source_node, uint32_t destination_node);
static enum engine_ai_traversal_reservation_result policy_reserve(void *context,
    uint32_t traversal_id, uint32_t source_node, uint32_t destination_node,
    uint64_t *reservation_id);
static enum engine_ai_traversal_execution_result policy_execute(void *context,
    uint32_t traversal_id, uint32_t source_node, uint32_t destination_node,
    uint64_t reservation_id);
static void policy_release(void *context, uint32_t traversal_id,
    uint64_t reservation_id, enum engine_ai_traversal_release_reason reason);
static enum engine_ai_nav_status find_route(const struct engine_ai_nav_graph *graph,
    uint32_t start_node, uint32_t goal_node, uint32_t capabilities,
    struct engine_ai_nav_route *route);
static void init_policy(struct game_policy *policy);
static void begin_special(struct engine_ai_traversal_state *state,
    const struct engine_ai_nav_graph *graph, const struct engine_ai_nav_route *route,
    uint32_t capabilities, struct game_policy *policy);
static void test_one_way_drop(void);
static void test_jump_capability_loss(void);
static void test_door_reservation_denial_then_grant(void);
static void test_lift_unavailable_during_execution(void);
static void test_cancel_releases_once(void);
static void test_stale_revision_and_invalid_atomicity(void);
static void test_snapshot_restore_replay(void);

static struct engine_ai_traversal_callbacks callbacks = {
    policy_availability,
    policy_reserve,
    policy_execute,
    policy_release
};

static enum engine_ai_traversal_availability policy_availability(void *context,
    uint32_t traversal_id, uint32_t source_node, uint32_t destination_node)
{
    struct game_policy *policy = context;
    assert(traversal_id != 0);
    assert(source_node != destination_node);
    return policy->available ? ENGINE_AI_TRAVERSAL_AVAILABLE :
        ENGINE_AI_TRAVERSAL_UNAVAILABLE;
}

static enum engine_ai_traversal_reservation_result policy_reserve(void *context,
    uint32_t traversal_id, uint32_t source_node, uint32_t destination_node,
    uint64_t *reservation_id)
{
    struct game_policy *policy = context;
    assert(traversal_id != 0);
    assert(source_node != destination_node);
    assert(reservation_id != NULL);
    policy->reserve_calls++;
    if (policy->reserve_denials != 0) {
        policy->reserve_denials--;
        return ENGINE_AI_TRAVERSAL_RESERVATION_DENIED;
    }
    policy->next_reservation++;
    assert(policy->next_reservation != 0);
    *reservation_id = policy->next_reservation;
    policy->last_reservation = *reservation_id;
    return ENGINE_AI_TRAVERSAL_RESERVATION_GRANTED;
}

static enum engine_ai_traversal_execution_result policy_execute(void *context,
    uint32_t traversal_id, uint32_t source_node, uint32_t destination_node,
    uint64_t reservation_id)
{
    struct game_policy *policy = context;
    assert(traversal_id != 0);
    assert(source_node != destination_node);
    assert(reservation_id == policy->last_reservation);
    policy->execute_calls++;
    return policy->execution_result;
}

static void policy_release(void *context, uint32_t traversal_id,
    uint64_t reservation_id, enum engine_ai_traversal_release_reason reason)
{
    struct game_policy *policy = context;
    assert(traversal_id != 0);
    assert(reservation_id != 0);
    assert(reservation_id == policy->last_reservation);
    policy->release_calls++;
    policy->last_release_reason = (uint32_t)reason;
}

static enum engine_ai_nav_status find_route(const struct engine_ai_nav_graph *graph,
    uint32_t start_node, uint32_t goal_node, uint32_t capabilities,
    struct engine_ai_nav_route *route)
{
    struct engine_ai_nav_search search;
    enum engine_ai_nav_status status = engine_ai_nav_begin(&search, graph,
        start_node, goal_node, capabilities);
    uint32_t step;
    for (step = 0; step <= ENGINE_AI_NAV_NODES_MAX &&
        status == ENGINE_AI_NAV_RUNNING; step++) {
        status = engine_ai_nav_step(&search, graph, ENGINE_AI_NAV_NODES_MAX);
    }
    assert(status != ENGINE_AI_NAV_RUNNING);
    assert(engine_ai_nav_result(&search, graph, route) == status);
    return status;
}

static void init_policy(struct game_policy *policy)
{
    memset(policy, 0, sizeof(*policy));
    policy->available = 1;
    policy->execution_result = ENGINE_AI_TRAVERSAL_EXECUTION_COMPLETE;
}

static void begin_special(struct engine_ai_traversal_state *state,
    const struct engine_ai_nav_graph *graph, const struct engine_ai_nav_route *route,
    uint32_t capabilities, struct game_policy *policy)
{
    assert(engine_ai_traversal_begin(state, graph, route, capabilities));
    assert(engine_ai_traversal_step(state, graph, graph->revision, capabilities,
        true, &callbacks, policy) == ENGINE_AI_TRAVERSAL_EXECUTING);
}

static void test_one_way_drop(void)
{
    struct engine_ai_nav_link links[] = {{0, 1, 1, 0, 11, true}};
    struct engine_ai_nav_graph graph = {links, 2, 1, 41};
    struct engine_ai_nav_route route;
    struct engine_ai_traversal_state state;
    struct game_policy policy;
    uint32_t target = UINT32_MAX;
    assert(find_route(&graph, 0, 1, 0, &route) == ENGINE_AI_NAV_FOUND);
    init_policy(&policy);
    assert(engine_ai_traversal_begin(&state, &graph, &route, 0));
    assert(engine_ai_traversal_target_node(&state, &graph, &target));
    assert(target == 0); /* Approach the drop lip, not its landing anchor. */
    assert(engine_ai_traversal_step(&state, &graph, graph.revision, 0, true,
        &callbacks, &policy) == ENGINE_AI_TRAVERSAL_EXECUTING);
    assert(engine_ai_traversal_step(&state, &graph, graph.revision, 0, false,
        &callbacks, &policy) == ENGINE_AI_TRAVERSAL_COMPLETED);
    assert(policy.release_calls == 1);
    assert(policy.last_release_reason == ENGINE_AI_TRAVERSAL_RELEASE_COMPLETE);
    assert(find_route(&graph, 1, 0, 0, &route) == ENGINE_AI_NAV_NO_PATH);
}

static void test_jump_capability_loss(void)
{
    struct engine_ai_nav_link links[] = {{0, 1, 1, 1, 12, true}};
    struct engine_ai_nav_graph graph = {links, 2, 1, 42};
    struct engine_ai_nav_route route;
    struct engine_ai_traversal_state state;
    struct game_policy policy;
    assert(find_route(&graph, 0, 1, 1, &route) == ENGINE_AI_NAV_FOUND);
    init_policy(&policy);
    assert(engine_ai_traversal_begin(&state, &graph, &route, 1));
    assert(engine_ai_traversal_step(&state, &graph, graph.revision, 0, true,
        &callbacks, &policy) == ENGINE_AI_TRAVERSAL_REPLAN);
    assert(policy.reserve_calls == 0 && policy.release_calls == 0);
}

static void test_door_reservation_denial_then_grant(void)
{
    struct engine_ai_nav_link links[] = {{0, 1, 1, 0, 21, true}};
    struct engine_ai_nav_graph graph = {links, 2, 1, 43};
    struct engine_ai_nav_route route;
    struct engine_ai_traversal_state state;
    struct game_policy policy;
    assert(find_route(&graph, 0, 1, 0, &route) == ENGINE_AI_NAV_FOUND);
    init_policy(&policy);
    policy.reserve_denials = 1;
    assert(engine_ai_traversal_begin(&state, &graph, &route, 0));
    assert(engine_ai_traversal_step(&state, &graph, graph.revision, 0, true,
        &callbacks, &policy) == ENGINE_AI_TRAVERSAL_WAITING);
    assert(state.phase == ENGINE_AI_TRAVERSAL_APPROACH);
    assert(engine_ai_traversal_step(&state, &graph, graph.revision, 0, true,
        &callbacks, &policy) == ENGINE_AI_TRAVERSAL_EXECUTING);
    assert(engine_ai_traversal_step(&state, &graph, graph.revision, 0, false,
        &callbacks, &policy) == ENGINE_AI_TRAVERSAL_COMPLETED);
    assert(policy.reserve_calls == 2 && policy.release_calls == 1);
}

static void test_lift_unavailable_during_execution(void)
{
    struct engine_ai_nav_link links[] = {
        {0, 1, 1, 0, 0, true}, {1, 2, 1, 0, 31, true}
    };
    struct engine_ai_nav_graph graph = {links, 3, 2, 44};
    struct engine_ai_nav_route route;
    struct engine_ai_traversal_state state;
    struct game_policy policy;
    uint32_t target;
    assert(find_route(&graph, 0, 2, 0, &route) == ENGINE_AI_NAV_FOUND);
    init_policy(&policy);
    assert(engine_ai_traversal_begin(&state, &graph, &route, 0));
    assert(engine_ai_traversal_target_node(&state, &graph, &target) && target == 1);
    assert(engine_ai_traversal_step(&state, &graph, graph.revision, 0, true,
        &callbacks, &policy) == ENGINE_AI_TRAVERSAL_APPROACHING);
    assert(engine_ai_traversal_target_node(&state, &graph, &target) && target == 1);
    assert(engine_ai_traversal_step(&state, &graph, graph.revision, 0, true,
        &callbacks, &policy) == ENGINE_AI_TRAVERSAL_EXECUTING);
    policy.available = 0; /* The lift becomes unusable after reservation. */
    assert(engine_ai_traversal_step(&state, &graph, graph.revision, 0, false,
        &callbacks, &policy) == ENGINE_AI_TRAVERSAL_REPLAN);
    assert(policy.execute_calls == 0 && policy.release_calls == 1);
    assert(policy.last_release_reason == ENGINE_AI_TRAVERSAL_RELEASE_FAILED);
}

static void test_cancel_releases_once(void)
{
    struct engine_ai_nav_link links[] = {{0, 1, 1, 0, 41, true}};
    struct engine_ai_nav_graph graph = {links, 2, 1, 45};
    struct engine_ai_nav_route route;
    struct engine_ai_traversal_state state;
    struct game_policy policy;
    assert(find_route(&graph, 0, 1, 0, &route) == ENGINE_AI_NAV_FOUND);
    init_policy(&policy);
    begin_special(&state, &graph, &route, 0, &policy);
    assert(engine_ai_traversal_cancel(&state, &callbacks, &policy) ==
        ENGINE_AI_TRAVERSAL_CANCELLED_RESULT);
    assert(engine_ai_traversal_cancel(&state, &callbacks, &policy) ==
        ENGINE_AI_TRAVERSAL_CANCELLED_RESULT);
    assert(policy.release_calls == 1);
    assert(policy.last_release_reason == ENGINE_AI_TRAVERSAL_RELEASE_CANCELLED);
}

static void test_stale_revision_and_invalid_atomicity(void)
{
    struct engine_ai_nav_link links[] = {{0, 1, 1, 0, 51, true}};
    struct engine_ai_nav_graph graph = {links, 2, 1, 46};
    struct engine_ai_nav_graph invalid_graph;
    struct engine_ai_nav_route route;
    struct engine_ai_nav_route invalid_route;
    struct engine_ai_traversal_state state;
    struct engine_ai_traversal_state saved;
    assert(find_route(&graph, 0, 1, 0, &route) == ENGINE_AI_NAV_FOUND);
    memset(&state, 0xA5, sizeof(state));
    saved = state;
    invalid_route = route;
    invalid_route.cost++;
    assert(!engine_ai_traversal_begin(&state, &graph, &invalid_route, 0));
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    links[0].required_capabilities = 1;
    assert(!engine_ai_traversal_begin(&state, &graph, &route, 0));
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    links[0].required_capabilities = 0;
    assert(engine_ai_traversal_begin(&state, &graph, &route, 0));
    saved = state;
    invalid_graph = graph;
    invalid_graph.node_count = 0;
    assert(engine_ai_traversal_step(&state, &invalid_graph, graph.revision, 0,
        true, &callbacks, NULL) == ENGINE_AI_TRAVERSAL_INVALID);
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    assert(engine_ai_traversal_step(&state, &graph, graph.revision, 0, true,
        NULL, NULL) == ENGINE_AI_TRAVERSAL_INVALID);
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    graph.revision++;
    assert(engine_ai_traversal_step(&state, &graph, graph.revision, 0, true,
        &callbacks, NULL) == ENGINE_AI_TRAVERSAL_REPLAN);
    assert(state.phase == ENGINE_AI_TRAVERSAL_FAILED);
}

static void test_snapshot_restore_replay(void)
{
    struct engine_ai_nav_link links[] = {{0, 1, 1, 0, 61, true}};
    struct engine_ai_nav_graph graph = {links, 2, 1, 47};
    struct engine_ai_nav_route route;
    struct engine_ai_traversal_state state;
    struct engine_ai_traversal_state snapshot;
    struct engine_ai_traversal_state first_final;
    struct game_policy policy;
    struct game_policy policy_snapshot;
    struct game_policy first_final_policy;
    enum engine_ai_traversal_result first_result;
    enum engine_ai_traversal_result replay_result;
    assert(find_route(&graph, 0, 1, 0, &route) == ENGINE_AI_NAV_FOUND);
    init_policy(&policy);
    begin_special(&state, &graph, &route, 0, &policy);
    memcpy(&snapshot, &state, sizeof(snapshot));
    memcpy(&policy_snapshot, &policy, sizeof(policy_snapshot));
    first_result = engine_ai_traversal_step(&state, &graph, graph.revision, 0,
        false, &callbacks, &policy);
    memcpy(&first_final, &state, sizeof(first_final));
    memcpy(&first_final_policy, &policy, sizeof(first_final_policy));
    memcpy(&state, &snapshot, sizeof(state));
    memcpy(&policy, &policy_snapshot, sizeof(policy));
    replay_result = engine_ai_traversal_step(&state, &graph, graph.revision, 0,
        false, &callbacks, &policy);
    assert(first_result == replay_result);
    assert(memcmp(&state, &first_final, sizeof(state)) == 0);
    assert(memcmp(&policy, &first_final_policy, sizeof(policy)) == 0);
    assert(first_result == ENGINE_AI_TRAVERSAL_COMPLETED);
}

int main(void)
{
    test_one_way_drop();
    test_jump_capability_loss();
    test_door_reservation_denial_then_grant();
    test_lift_unavailable_during_execution();
    test_cancel_releases_once();
    test_stale_revision_and_invalid_atomicity();
    test_snapshot_restore_replay();
    puts("engine AI traversal tests passed");
    return 0;
}
