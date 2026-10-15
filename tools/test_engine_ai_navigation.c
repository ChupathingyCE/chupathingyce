#include "engine_ai/navigation.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct engine_ai_nav_search search;
static struct engine_ai_nav_route route;

static enum engine_ai_nav_status solve(const struct engine_ai_nav_graph *graph,
    uint32_t start, uint32_t goal, uint32_t capabilities, uint32_t budget)
{
    enum engine_ai_nav_status status;
    uint32_t tick;
    status = engine_ai_nav_begin(&search, graph, start, goal, capabilities);
    for (tick = 0; tick <= ENGINE_AI_NAV_NODES_MAX; tick++) {
        if (status != ENGINE_AI_NAV_RUNNING) {
            break;
        }
        status = engine_ai_nav_step(&search, graph, budget);
    }
    assert(status != ENGINE_AI_NAV_RUNNING);
    assert(engine_ai_nav_result(&search, graph, &route) == status);
    return status;
}

static void test_smart_links(void)
{
    struct engine_ai_nav_link links[] = {
        {0, 1, 5, 0, 0, true}, {1, 3, 5, 0, 0, true},
        {0, 2, 1, 1, 42, true}, {2, 3, 1, 0, 0, true},
        {0, 3, 1, 0, 99, false}
    };
    struct engine_ai_nav_graph graph = {links, 4, 5, 7};
    struct engine_ai_nav_route saved;
    assert(solve(&graph, 0, 3, 0, 1) == ENGINE_AI_NAV_FOUND);
    assert(route.cost == 10 && route.link_count == 2);
    assert(route.link_indices[0] == 0 && route.link_indices[1] == 1);
    assert(solve(&graph, 0, 3, 1, 1) == ENGINE_AI_NAV_FOUND);
    assert(route.cost == 2 && route.revision == 7);
    assert(route.link_indices[0] == 2);
    assert(links[route.link_indices[0]].traversal_id == 42);
    saved = route;
    assert(solve(&graph, 0, 3, 1, ENGINE_AI_NAV_NODES_MAX) == ENGINE_AI_NAV_FOUND);
    assert(memcmp(&saved, &route, sizeof(route)) == 0);
    assert(solve(&graph, 3, 0, 1, 1) == ENGINE_AI_NAV_NO_PATH);
    assert(route.link_count == 0 && route.cost == 0);
    assert(solve(&graph, 2, 2, 0, 1) == ENGINE_AI_NAV_FOUND);
    assert(route.link_count == 0 && route.cost == 0 && route.revision == 7);
}

static void test_errors_and_revisions(void)
{
    struct engine_ai_nav_link link = {0, 1, 1, 0, 1, true};
    struct engine_ai_nav_graph graph = {&link, 2, 1, 1};
    struct engine_ai_nav_graph replacement = graph;
    assert(engine_ai_nav_begin(&search, &graph, 0, 1, 0) == ENGINE_AI_NAV_RUNNING);
    assert(engine_ai_nav_step(&search, &graph, 0) == ENGINE_AI_NAV_INVALID);
    assert(engine_ai_nav_step(&search, &graph, ENGINE_AI_NAV_NODES_MAX + 1) ==
        ENGINE_AI_NAV_INVALID);
    assert(engine_ai_nav_step(&search, &replacement, 1) == ENGINE_AI_NAV_STALE);
    assert(engine_ai_nav_result(&search, &replacement, &route) == ENGINE_AI_NAV_STALE);
    assert(route.link_count == 0);
    assert(engine_ai_nav_begin(&search, &graph, 0, 1, 0) == ENGINE_AI_NAV_RUNNING);
    assert(engine_ai_nav_result(&search, &graph, &route) == ENGINE_AI_NAV_RUNNING);
    graph.revision++;
    assert(engine_ai_nav_step(&search, &graph, 1) == ENGINE_AI_NAV_STALE);
    assert(solve(&graph, 0, 1, 0, 1) == ENGINE_AI_NAV_FOUND);
    graph.revision++;
    assert(engine_ai_nav_result(&search, &graph, &route) == ENGINE_AI_NAV_STALE);
    assert(route.link_count == 0);
    assert(engine_ai_nav_begin(&search, &graph, 2, 1, 0) == ENGINE_AI_NAV_INVALID);
    link.cost = 0;
    assert(engine_ai_nav_begin(&search, &graph, 0, 1, 0) == ENGINE_AI_NAV_INVALID);
    link.cost = 1;
    link.destination_node = 2;
    assert(engine_ai_nav_begin(&search, &graph, 0, 1, 0) == ENGINE_AI_NAV_INVALID);
    link.destination_node = 1;
    link.source_node = 2;
    assert(engine_ai_nav_begin(&search, &graph, 0, 1, 0) == ENGINE_AI_NAV_INVALID);
    graph.links = NULL;
    assert(engine_ai_nav_begin(&search, &graph, 0, 1, 0) == ENGINE_AI_NAV_INVALID);
    graph.link_count = ENGINE_AI_NAV_LINKS_MAX + 1;
    assert(engine_ai_nav_begin(&search, &graph, 0, 1, 0) == ENGINE_AI_NAV_INVALID);
    graph.link_count = 0;
    graph.node_count = 0;
    assert(engine_ai_nav_begin(&search, &graph, 0, 0, 0) == ENGINE_AI_NAV_INVALID);
    graph.node_count = ENGINE_AI_NAV_NODES_MAX + 1;
    assert(engine_ai_nav_begin(&search, &graph, 0, 0, 0) == ENGINE_AI_NAV_INVALID);
    graph.node_count = 1;
    assert(solve(&graph, 0, 0, 0, 1) == ENGINE_AI_NAV_FOUND);
}

static void test_ties_and_capabilities(void)
{
    struct engine_ai_nav_link links[] = {
        {0, 2, 1, 0, 0, true}, {0, 1, 1, 0, 0, true},
        {2, 3, 1, 0, 0, true}, {1, 3, 1, 0, 0, true},
        {0, 3, 1, 3, 88, true}, {0, 0, 1, 0, 0, true}
    };
    struct engine_ai_nav_graph graph = {links, 4, 6, 1};
    assert(solve(&graph, 0, 3, 1, 1) == ENGINE_AI_NAV_FOUND);
    assert(route.link_count == 2);
    assert(route.link_indices[0] == 1 && route.link_indices[1] == 3);
    assert(solve(&graph, 0, 3, 3, 1) == ENGINE_AI_NAV_FOUND);
    assert(route.link_count == 1 && route.link_indices[0] == 4);
}

static void test_capacity_and_large_costs(void)
{
    static struct engine_ai_nav_link links[ENGINE_AI_NAV_LINKS_MAX];
    struct engine_ai_nav_graph graph = {
        links, ENGINE_AI_NAV_NODES_MAX, ENGINE_AI_NAV_LINKS_MAX, 1
    };
    uint32_t index;
    for (index = 0; index < ENGINE_AI_NAV_ROUTE_LINKS_MAX; index++) {
        links[index] = (struct engine_ai_nav_link){index, index + 1, UINT32_MAX,
            0, 0, true};
    }
    for (; index < ENGINE_AI_NAV_LINKS_MAX; index++) {
        links[index] = (struct engine_ai_nav_link){0, 0, 1, 0, 0, true};
    }
    assert(solve(&graph, 0, ENGINE_AI_NAV_NODES_MAX - 1, 0, 1) == ENGINE_AI_NAV_FOUND);
    assert(route.link_count == ENGINE_AI_NAV_ROUTE_LINKS_MAX);
    assert(route.cost == (uint64_t)UINT32_MAX * ENGINE_AI_NAV_ROUTE_LINKS_MAX);
    for (index = 0; index < route.link_count; index++) {
        assert(route.link_indices[index] == index);
    }
    assert(engine_ai_nav_step(&search, &graph, 1) == ENGINE_AI_NAV_FOUND);
}

static uint32_t next_random(uint32_t *state)
{
    /* Deliberate unsigned wraparound for a repeatable test generator. */
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void check_route(const struct engine_ai_nav_graph *graph,
    uint32_t start, uint32_t goal, uint32_t capabilities, uint64_t expected_cost)
{
    uint32_t index;
    uint32_t node = start;
    uint64_t cost = 0;
    for (index = 0; index < route.link_count; index++) {
        const struct engine_ai_nav_link *link;
        assert(route.link_indices[index] < graph->link_count);
        link = &graph->links[route.link_indices[index]];
        assert(link->source_node == node && link->enabled);
        assert((link->required_capabilities & capabilities) == link->required_capabilities);
        node = link->destination_node;
        cost += link->cost;
    }
    assert(node == goal);
    assert(cost == expected_cost && route.cost == expected_cost);
}

static void test_reference_solver(void)
{
    enum { NODES = 8, LINKS = 40, TRIALS = 64 };
    struct engine_ai_nav_link links[LINKS];
    struct engine_ai_nav_graph graph = {links, NODES, LINKS, 1};
    uint32_t random_state = 19;
    uint32_t trial;
    for (trial = 0; trial < TRIALS; trial++) {
        uint64_t distances[NODES][NODES];
        uint32_t index, source, destination, via;
        uint32_t capabilities = trial % 4;
        for (source = 0; source < NODES; source++) {
            for (destination = 0; destination < NODES; destination++) {
                distances[source][destination] = source == destination ? 0 : UINT64_MAX;
            }
        }
        for (index = 0; index < LINKS; index++) {
            struct engine_ai_nav_link *link = &links[index];
            link->source_node = (next_random(&random_state) >> 8) % NODES;
            link->destination_node = (next_random(&random_state) >> 8) % NODES;
            link->cost = 1 + next_random(&random_state) % 100;
            link->required_capabilities = next_random(&random_state) % 4;
            link->traversal_id = index;
            link->enabled = (next_random(&random_state) >> 8) % 3 != 0;
            if (link->enabled && (link->required_capabilities & capabilities) ==
                link->required_capabilities) {
                uint64_t *distance = &distances[link->source_node][link->destination_node];
                if (link->cost < *distance) {
                    *distance = link->cost;
                }
            }
        }
        /* Independent all-pairs Floyd-Warshall reference on tiny bounded graphs. */
        for (via = 0; via < NODES; via++) {
            for (source = 0; source < NODES; source++) {
                for (destination = 0; destination < NODES; destination++) {
                    uint64_t first = distances[source][via];
                    uint64_t second = distances[via][destination];
                    if (first != UINT64_MAX && second != UINT64_MAX &&
                        first + second < distances[source][destination]) {
                        distances[source][destination] = first + second;
                    }
                }
            }
        }
        for (source = 0; source < NODES; source++) {
            for (destination = 0; destination < NODES; destination++) {
                enum engine_ai_nav_status status = solve(&graph, source, destination,
                    capabilities, 1 + trial % 8);
                if (distances[source][destination] == UINT64_MAX) {
                    assert(status == ENGINE_AI_NAV_NO_PATH);
                } else {
                    assert(status == ENGINE_AI_NAV_FOUND);
                    check_route(&graph, source, destination, capabilities,
                        distances[source][destination]);
                }
            }
        }
    }
}

int main(void)
{
    test_smart_links();
    test_errors_and_revisions();
    test_ties_and_capabilities();
    test_capacity_and_large_costs();
    test_reference_solver();
    puts("engine AI navigation tests passed");
    return 0;
}
