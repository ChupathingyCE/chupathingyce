/* Bounded per-agent execution of directed navigation route links. */
#include "traversal.h"

#include <assert.h>
#include <string.h>

static bool graph_shape_valid(const struct engine_ai_nav_graph *graph);
static bool graph_valid(const struct engine_ai_nav_graph *graph);
static bool route_valid(const struct engine_ai_nav_graph *graph,
    const struct engine_ai_nav_route *route, uint32_t capabilities);
static bool state_valid(const struct engine_ai_traversal_state *state);
static bool callbacks_complete(const struct engine_ai_traversal_callbacks *callbacks);
static void release_reservation(struct engine_ai_traversal_state *state,
    const struct engine_ai_traversal_callbacks *callbacks, void *context,
    enum engine_ai_traversal_release_reason reason);
static enum engine_ai_traversal_result fail_route(
    struct engine_ai_traversal_state *state,
    const struct engine_ai_traversal_callbacks *callbacks, void *context,
    enum engine_ai_traversal_result result,
    enum engine_ai_traversal_release_reason reason);
static enum engine_ai_traversal_result advance_link(
    struct engine_ai_traversal_state *state);
static enum engine_ai_traversal_result step_special(
    struct engine_ai_traversal_state *state,
    const struct engine_ai_nav_link *link,
    bool target_reached, const struct engine_ai_traversal_callbacks *callbacks,
    void *context);

bool engine_ai_traversal_begin(struct engine_ai_traversal_state *state,
    const struct engine_ai_nav_graph *graph, const struct engine_ai_nav_route *route,
    uint32_t capabilities)
{
    struct engine_ai_traversal_state next;
    uint32_t index;
    assert(state != NULL);
    assert(graph != NULL);
    assert(route != NULL);
    if (!graph_valid(graph) || !route_valid(graph, route, capabilities)) {
        return false;
    }
    memset(&next, 0, sizeof(next));
    next.route.revision = route->revision;
    next.route.cost = route->cost;
    next.route.link_count = route->link_count;
    for (index = 0; index < route->link_count; index++) {
        next.route.link_indices[index] = route->link_indices[index];
    }
    next.phase = route->link_count == 0 ? ENGINE_AI_TRAVERSAL_COMPLETE :
        ENGINE_AI_TRAVERSAL_APPROACH;
    next.terminal_result = route->link_count == 0 ? ENGINE_AI_TRAVERSAL_COMPLETED : 0;
    *state = next;
    assert(state_valid(state));
    return true;
}

bool engine_ai_traversal_target_node(const struct engine_ai_traversal_state *state,
    const struct engine_ai_nav_graph *graph, uint32_t *target_node)
{
    const struct engine_ai_nav_link *link;
    uint32_t target;
    assert(state != NULL);
    assert(graph != NULL);
    assert(target_node != NULL);
    if (!state_valid(state) || state->phase != ENGINE_AI_TRAVERSAL_APPROACH ||
        !graph_shape_valid(graph) || graph->revision != state->route.revision ||
        !route_valid(graph, &state->route, UINT32_MAX)) {
        return false;
    }
    link = &graph->links[state->route.link_indices[state->next_link]];
    target = link->traversal_id == 0 ? link->destination_node : link->source_node;
    *target_node = target;
    return true;
}

enum engine_ai_traversal_result engine_ai_traversal_step(
    struct engine_ai_traversal_state *state,
    const struct engine_ai_nav_graph *graph, uint64_t resource_revision,
    uint32_t capabilities, bool target_reached,
    const struct engine_ai_traversal_callbacks *callbacks, void *context)
{
    const struct engine_ai_nav_link *link;
    assert(state != NULL);
    assert(graph != NULL);
    if (!state_valid(state)) {
        return ENGINE_AI_TRAVERSAL_INVALID;
    }
    if (state->phase == ENGINE_AI_TRAVERSAL_COMPLETE ||
        state->phase == ENGINE_AI_TRAVERSAL_FAILED ||
        state->phase == ENGINE_AI_TRAVERSAL_CANCELLED) {
        return (enum engine_ai_traversal_result)state->terminal_result;
    }
    if (state->phase != ENGINE_AI_TRAVERSAL_APPROACH &&
        state->phase != ENGINE_AI_TRAVERSAL_EXECUTE) {
        return ENGINE_AI_TRAVERSAL_INVALID;
    }
    if (state->reservation_held != 0 &&
        (callbacks == NULL || callbacks->release == NULL)) {
        return ENGINE_AI_TRAVERSAL_INVALID;
    }
    if (!graph_shape_valid(graph)) {
        return ENGINE_AI_TRAVERSAL_INVALID;
    }
    if (graph->revision != state->route.revision ||
        resource_revision != state->route.revision) {
        return fail_route(state, callbacks, context, ENGINE_AI_TRAVERSAL_REPLAN,
            ENGINE_AI_TRAVERSAL_RELEASE_FAILED);
    }
    if (!route_valid(graph, &state->route, UINT32_MAX)) {
        return ENGINE_AI_TRAVERSAL_INVALID;
    }
    link = &graph->links[state->route.link_indices[state->next_link]];
    if (!link->enabled || (link->required_capabilities & capabilities) !=
        link->required_capabilities) {
        return fail_route(state, callbacks, context, ENGINE_AI_TRAVERSAL_REPLAN,
            ENGINE_AI_TRAVERSAL_RELEASE_FAILED);
    }
    if (link->traversal_id == 0) {
        if (state->phase != ENGINE_AI_TRAVERSAL_APPROACH) {
            return ENGINE_AI_TRAVERSAL_INVALID;
        }
        if (!target_reached) {
            return ENGINE_AI_TRAVERSAL_APPROACHING;
        }
        return advance_link(state);
    }
    if (!callbacks_complete(callbacks)) {
        return ENGINE_AI_TRAVERSAL_INVALID;
    }
    return step_special(state, link, target_reached, callbacks, context);
}

enum engine_ai_traversal_result engine_ai_traversal_cancel(
    struct engine_ai_traversal_state *state,
    const struct engine_ai_traversal_callbacks *callbacks, void *context)
{
    assert(state != NULL);
    if (!state_valid(state) || state->phase == ENGINE_AI_TRAVERSAL_IDLE) {
        return ENGINE_AI_TRAVERSAL_INVALID;
    }
    if (state->phase == ENGINE_AI_TRAVERSAL_COMPLETE ||
        state->phase == ENGINE_AI_TRAVERSAL_FAILED ||
        state->phase == ENGINE_AI_TRAVERSAL_CANCELLED) {
        return (enum engine_ai_traversal_result)state->terminal_result;
    }
    if (state->reservation_held != 0 &&
        (callbacks == NULL || callbacks->release == NULL)) {
        return ENGINE_AI_TRAVERSAL_INVALID;
    }
    release_reservation(state, callbacks, context,
        ENGINE_AI_TRAVERSAL_RELEASE_CANCELLED);
    state->phase = ENGINE_AI_TRAVERSAL_CANCELLED;
    state->terminal_result = ENGINE_AI_TRAVERSAL_CANCELLED_RESULT;
    assert(state_valid(state));
    return ENGINE_AI_TRAVERSAL_CANCELLED_RESULT;
}

static bool graph_shape_valid(const struct engine_ai_nav_graph *graph)
{
    assert(graph != NULL);
    if (graph->node_count == 0 || graph->node_count > ENGINE_AI_NAV_NODES_MAX) {
        return false;
    }
    if (graph->link_count > ENGINE_AI_NAV_LINKS_MAX) {
        return false;
    }
    return graph->link_count == 0 || graph->links != NULL;
}

static bool graph_valid(const struct engine_ai_nav_graph *graph)
{
    uint32_t index;
    if (!graph_shape_valid(graph)) {
        return false;
    }
    for (index = 0; index < graph->link_count; index++) {
        const struct engine_ai_nav_link *link = &graph->links[index];
        if (link->source_node >= graph->node_count ||
            link->destination_node >= graph->node_count || link->cost == 0) {
            return false;
        }
    }
    return true;
}

static bool route_valid(const struct engine_ai_nav_graph *graph,
    const struct engine_ai_nav_route *route, uint32_t capabilities)
{
    uint64_t cost = 0;
    uint32_t expected_source = 0;
    uint32_t index;
    assert(graph != NULL);
    assert(route != NULL);
    if (!graph_shape_valid(graph) || route->revision != graph->revision ||
        route->link_count > ENGINE_AI_NAV_ROUTE_LINKS_MAX) {
        return false;
    }
    for (index = 0; index < route->link_count; index++) {
        const struct engine_ai_nav_link *link;
        uint32_t link_index = route->link_indices[index];
        if (link_index >= graph->link_count) {
            return false;
        }
        link = &graph->links[link_index];
        if (link->source_node >= graph->node_count ||
            link->destination_node >= graph->node_count || link->cost == 0) {
            return false;
        }
        if (index != 0 && link->source_node != expected_source) {
            return false;
        }
        if (!link->enabled || (link->required_capabilities & capabilities) !=
            link->required_capabilities) {
            return false;
        }
        cost += link->cost;
        expected_source = link->destination_node;
    }
    if (cost != route->cost || (route->link_count == 0 && route->cost != 0)) {
        return false;
    }
    return true;
}

static bool state_valid(const struct engine_ai_traversal_state *state)
{
    assert(state != NULL);
    if (state->route.link_count > ENGINE_AI_NAV_ROUTE_LINKS_MAX ||
        state->next_link > state->route.link_count || state->reservation_held > 1) {
        return false;
    }
    if (state->phase == ENGINE_AI_TRAVERSAL_APPROACH) {
        return state->next_link < state->route.link_count &&
            state->reservation_held == 0 && state->reservation_id == 0 &&
            state->terminal_result == 0;
    }
    if (state->phase == ENGINE_AI_TRAVERSAL_EXECUTE) {
        return state->next_link < state->route.link_count &&
            state->reservation_held == 1 && state->reservation_id != 0 &&
            state->active_traversal_id != 0 && state->terminal_result == 0;
    }
    if (state->phase == ENGINE_AI_TRAVERSAL_COMPLETE) {
        return state->next_link == state->route.link_count &&
            state->reservation_held == 0 && state->reservation_id == 0 &&
            state->terminal_result == ENGINE_AI_TRAVERSAL_COMPLETED;
    }
    if (state->phase == ENGINE_AI_TRAVERSAL_FAILED) {
        return state->reservation_held == 0 && state->reservation_id == 0 &&
            (state->terminal_result == ENGINE_AI_TRAVERSAL_FAILED_RESULT ||
            state->terminal_result == ENGINE_AI_TRAVERSAL_REPLAN);
    }
    if (state->phase == ENGINE_AI_TRAVERSAL_CANCELLED) {
        return state->reservation_held == 0 && state->reservation_id == 0 &&
            state->terminal_result == ENGINE_AI_TRAVERSAL_CANCELLED_RESULT;
    }
    if (state->phase == ENGINE_AI_TRAVERSAL_IDLE) {
        return state->next_link == 0 && state->route.link_count == 0 &&
            state->reservation_held == 0 && state->reservation_id == 0 &&
            state->terminal_result == 0;
    }
    return false;
}

static bool callbacks_complete(const struct engine_ai_traversal_callbacks *callbacks)
{
    return callbacks != NULL && callbacks->availability != NULL &&
        callbacks->reserve != NULL && callbacks->execute != NULL &&
        callbacks->release != NULL;
}

static void release_reservation(struct engine_ai_traversal_state *state,
    const struct engine_ai_traversal_callbacks *callbacks, void *context,
    enum engine_ai_traversal_release_reason reason)
{
    assert(state != NULL);
    if (state->reservation_held == 0) {
        return;
    }
    assert(callbacks != NULL);
    assert(callbacks->release != NULL);
    assert(state->reservation_id != 0);
    callbacks->release(context, state->active_traversal_id, state->reservation_id,
        reason);
    state->reservation_id = 0;
    state->reservation_held = 0;
    state->active_source_node = 0;
    state->active_destination_node = 0;
    state->active_traversal_id = 0;
}

static enum engine_ai_traversal_result fail_route(
    struct engine_ai_traversal_state *state,
    const struct engine_ai_traversal_callbacks *callbacks, void *context,
    enum engine_ai_traversal_result result,
    enum engine_ai_traversal_release_reason reason)
{
    release_reservation(state, callbacks, context, reason);
    state->phase = ENGINE_AI_TRAVERSAL_FAILED;
    state->terminal_result = (uint32_t)result;
    assert(state_valid(state));
    return result;
}

static enum engine_ai_traversal_result advance_link(
    struct engine_ai_traversal_state *state)
{
    assert(state != NULL);
    assert(state->phase == ENGINE_AI_TRAVERSAL_APPROACH ||
        state->phase == ENGINE_AI_TRAVERSAL_EXECUTE);
    assert(state->next_link < state->route.link_count);
    state->next_link++;
    state->phase = state->next_link == state->route.link_count ?
        ENGINE_AI_TRAVERSAL_COMPLETE : ENGINE_AI_TRAVERSAL_APPROACH;
    if (state->phase == ENGINE_AI_TRAVERSAL_COMPLETE) {
        state->terminal_result = ENGINE_AI_TRAVERSAL_COMPLETED;
        assert(state_valid(state));
        return ENGINE_AI_TRAVERSAL_COMPLETED;
    }
    assert(state_valid(state));
    return ENGINE_AI_TRAVERSAL_APPROACHING;
}

static enum engine_ai_traversal_result step_special(
    struct engine_ai_traversal_state *state,
    const struct engine_ai_nav_link *link, bool target_reached,
    const struct engine_ai_traversal_callbacks *callbacks, void *context)
{
    enum engine_ai_traversal_availability availability;
    assert(state != NULL);
    assert(link != NULL);
    assert(callbacks_complete(callbacks));
    if (state->phase == ENGINE_AI_TRAVERSAL_APPROACH) {
        uint64_t reservation_id = 0;
        enum engine_ai_traversal_reservation_result reserved;
        if (!target_reached) {
            return ENGINE_AI_TRAVERSAL_APPROACHING;
        }
        availability = callbacks->availability(context, link->traversal_id,
            link->source_node, link->destination_node);
        if (availability == ENGINE_AI_TRAVERSAL_UNAVAILABLE) {
            return fail_route(state, callbacks, context, ENGINE_AI_TRAVERSAL_REPLAN,
                ENGINE_AI_TRAVERSAL_RELEASE_FAILED);
        }
        if (availability != ENGINE_AI_TRAVERSAL_AVAILABLE) {
            return fail_route(state, callbacks, context,
                ENGINE_AI_TRAVERSAL_FAILED_RESULT, ENGINE_AI_TRAVERSAL_RELEASE_FAILED);
        }
        reserved = callbacks->reserve(context, link->traversal_id,
            link->source_node, link->destination_node, &reservation_id);
        if (reserved == ENGINE_AI_TRAVERSAL_RESERVATION_DENIED && reservation_id == 0) {
            return ENGINE_AI_TRAVERSAL_WAITING;
        }
        if (reserved != ENGINE_AI_TRAVERSAL_RESERVATION_GRANTED ||
            reservation_id == 0) {
            return fail_route(state, callbacks, context,
                ENGINE_AI_TRAVERSAL_FAILED_RESULT, ENGINE_AI_TRAVERSAL_RELEASE_FAILED);
        }
        state->reservation_id = reservation_id;
        state->reservation_held = 1;
        state->active_source_node = link->source_node;
        state->active_destination_node = link->destination_node;
        state->active_traversal_id = link->traversal_id;
        state->phase = ENGINE_AI_TRAVERSAL_EXECUTE;
        assert(state_valid(state));
        return ENGINE_AI_TRAVERSAL_EXECUTING;
    }
    if (state->phase != ENGINE_AI_TRAVERSAL_EXECUTE ||
        state->active_source_node != link->source_node ||
        state->active_destination_node != link->destination_node ||
        state->active_traversal_id != link->traversal_id) {
        return ENGINE_AI_TRAVERSAL_INVALID;
    }
    availability = callbacks->availability(context, link->traversal_id,
        link->source_node, link->destination_node);
    if (availability == ENGINE_AI_TRAVERSAL_UNAVAILABLE) {
        return fail_route(state, callbacks, context, ENGINE_AI_TRAVERSAL_REPLAN,
            ENGINE_AI_TRAVERSAL_RELEASE_FAILED);
    }
    if (availability != ENGINE_AI_TRAVERSAL_AVAILABLE) {
        return fail_route(state, callbacks, context, ENGINE_AI_TRAVERSAL_FAILED_RESULT,
            ENGINE_AI_TRAVERSAL_RELEASE_FAILED);
    }
    {
        enum engine_ai_traversal_execution_result execution = callbacks->execute(context,
            link->traversal_id, link->source_node, link->destination_node,
            state->reservation_id);
        if (execution == ENGINE_AI_TRAVERSAL_EXECUTION_RUNNING) {
            return ENGINE_AI_TRAVERSAL_EXECUTING;
        }
        if (execution == ENGINE_AI_TRAVERSAL_EXECUTION_COMPLETE) {
            release_reservation(state, callbacks, context,
                ENGINE_AI_TRAVERSAL_RELEASE_COMPLETE);
            return advance_link(state);
        }
        if (execution == ENGINE_AI_TRAVERSAL_EXECUTION_BLOCKED) {
            return fail_route(state, callbacks, context, ENGINE_AI_TRAVERSAL_REPLAN,
                ENGINE_AI_TRAVERSAL_RELEASE_FAILED);
        }
        return fail_route(state, callbacks, context,
            ENGINE_AI_TRAVERSAL_FAILED_RESULT, ENGINE_AI_TRAVERSAL_RELEASE_FAILED);
    }
}
