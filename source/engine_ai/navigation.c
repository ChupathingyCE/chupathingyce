/* Deterministic Dijkstra baseline for small cooked region/portal graphs.
 * Positive edge costs bound predecessor chains to node_count - 1 edges.
 * Large polygon meshes need a separately profiled solver/cooker, not larger caps. */
#include "navigation.h"

#include <assert.h>
#include <string.h>

_Static_assert(ENGINE_AI_NAV_NODES_MAX > 1, "Routes need at least two nodes");
_Static_assert(ENGINE_AI_NAV_NODES_MAX < UINT32_MAX, "Indices fit uint32_t");

static bool graph_valid(const struct engine_ai_nav_graph *graph);
static uint32_t nearest_open_node(const struct engine_ai_nav_search *search);
static void relax_links(struct engine_ai_nav_search *search, uint32_t node);
static bool snapshot_matches(const struct engine_ai_nav_search *search,
    const struct engine_ai_nav_graph *graph);

enum engine_ai_nav_status engine_ai_nav_begin(struct engine_ai_nav_search *search,
    const struct engine_ai_nav_graph *graph, uint32_t start_node, uint32_t goal_node,
    uint32_t capabilities)
{
    uint32_t index;
    assert(search != NULL);
    assert(graph != NULL);
    memset(search, 0, sizeof(*search));
    search->status = ENGINE_AI_NAV_INVALID;
    if (!graph_valid(graph)) {
        return search->status;
    }
    if (start_node >= graph->node_count || goal_node >= graph->node_count) {
        return search->status;
    }
    search->graph = graph;
    search->revision = graph->revision;
    search->start_node = start_node;
    search->goal_node = goal_node;
    search->capabilities = capabilities;
    for (index = 0; index < graph->node_count; index++) {
        search->distances[index] = UINT64_MAX;
        search->predecessor_links[index] = ENGINE_AI_NAV_INDEX_NONE;
    }
    search->distances[start_node] = 0;
    search->status = ENGINE_AI_NAV_RUNNING;
    assert(search->distances[start_node] == 0);
    return search->status;
}

enum engine_ai_nav_status engine_ai_nav_step(struct engine_ai_nav_search *search,
    const struct engine_ai_nav_graph *graph, uint32_t expansion_budget)
{
    uint32_t expansion;
    assert(search != NULL);
    assert(graph != NULL);
    if (search->status == ENGINE_AI_NAV_INVALID) {
        return search->status;
    }
    if (!snapshot_matches(search, graph)) {
        search->status = ENGINE_AI_NAV_STALE;
        return search->status;
    }
    if (expansion_budget == 0 || expansion_budget > ENGINE_AI_NAV_NODES_MAX) {
        return ENGINE_AI_NAV_INVALID;
    }
    if (search->status != ENGINE_AI_NAV_RUNNING) {
        return search->status;
    }
    for (expansion = 0; expansion < expansion_budget; expansion++) {
        uint32_t node = nearest_open_node(search);
        if (node == ENGINE_AI_NAV_INDEX_NONE) {
            search->status = ENGINE_AI_NAV_NO_PATH;
            break;
        }
        assert(node < graph->node_count);
        assert(!search->closed[node]);
        search->closed[node] = true;
        if (node == search->goal_node) {
            search->status = ENGINE_AI_NAV_FOUND;
            break;
        }
        relax_links(search, node);
    }
    return search->status;
}

enum engine_ai_nav_status engine_ai_nav_result(const struct engine_ai_nav_search *search,
    const struct engine_ai_nav_graph *graph, struct engine_ai_nav_route *route)
{
    uint32_t node;
    uint32_t index;
    assert(search != NULL);
    assert(graph != NULL);
    assert(route != NULL);
    memset(route, 0, sizeof(*route));
    if (search->status == ENGINE_AI_NAV_INVALID) {
        return search->status;
    }
    if (!snapshot_matches(search, graph)) {
        return ENGINE_AI_NAV_STALE;
    }
    if (search->status != ENGINE_AI_NAV_FOUND) {
        return search->status;
    }
    node = search->goal_node;
    for (index = 0; index < ENGINE_AI_NAV_ROUTE_LINKS_MAX; index++) {
        uint32_t link_index;
        if (node == search->start_node) {
            break;
        }
        link_index = search->predecessor_links[node];
        assert(link_index < graph->link_count);
        route->link_indices[route->link_count++] = link_index;
        node = graph->links[link_index].source_node;
    }
    assert(node == search->start_node);
    for (index = 0; index < route->link_count / 2; index++) {
        uint32_t reverse = route->link_count - 1 - index;
        uint32_t saved = route->link_indices[index];
        route->link_indices[index] = route->link_indices[reverse];
        route->link_indices[reverse] = saved;
    }
    route->revision = search->revision;
    route->cost = search->distances[search->goal_node];
    return ENGINE_AI_NAV_FOUND;
}

static bool graph_valid(const struct engine_ai_nav_graph *graph)
{
    uint32_t index;
    assert(graph != NULL);
    if (graph->node_count == 0 || graph->node_count > ENGINE_AI_NAV_NODES_MAX) {
        return false;
    }
    if (graph->link_count > ENGINE_AI_NAV_LINKS_MAX) {
        return false;
    }
    if (graph->link_count != 0 && graph->links == NULL) {
        return false;
    }
    for (index = 0; index < graph->link_count; index++) {
        const struct engine_ai_nav_link *link = &graph->links[index];
        if (link->source_node >= graph->node_count) {
            return false;
        }
        if (link->destination_node >= graph->node_count || link->cost == 0) {
            return false;
        }
    }
    return true;
}

static bool snapshot_matches(const struct engine_ai_nav_search *search,
    const struct engine_ai_nav_graph *graph)
{
    assert(search != NULL);
    assert(graph != NULL);
    return search->graph == graph && search->revision == graph->revision;
}

static uint32_t nearest_open_node(const struct engine_ai_nav_search *search)
{
    uint32_t index;
    uint32_t best = ENGINE_AI_NAV_INDEX_NONE;
    uint64_t distance = UINT64_MAX;
    assert(search != NULL);
    assert(search->graph->node_count <= ENGINE_AI_NAV_NODES_MAX);
    for (index = 0; index < search->graph->node_count; index++) {
        if (!search->closed[index] && search->distances[index] < distance) {
            best = index;
            distance = search->distances[index];
        }
    }
    return best;
}

static void relax_links(struct engine_ai_nav_search *search, uint32_t node)
{
    uint32_t index;
    const struct engine_ai_nav_graph *graph = search->graph;
    assert(node < graph->node_count);
    assert(search->distances[node] != UINT64_MAX);
    for (index = 0; index < graph->link_count; index++) {
        const struct engine_ai_nav_link *link = &graph->links[index];
        uint64_t distance;
        if (link->source_node != node || !link->enabled) {
            continue;
        }
        if ((link->required_capabilities & search->capabilities) !=
            link->required_capabilities || search->closed[link->destination_node]) {
            continue;
        }
        /* A simple path has < NODES_MAX edges, each <= UINT32_MAX. */
        assert(search->distances[node] <= UINT64_MAX - link->cost);
        distance = search->distances[node] + link->cost;
        if (distance < search->distances[link->destination_node]) {
            search->distances[link->destination_node] = distance;
            search->predecessor_links[link->destination_node] = index;
        }
    }
}
