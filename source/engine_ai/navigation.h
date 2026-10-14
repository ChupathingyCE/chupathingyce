/* Bounded route search over a cooked navigation graph; no game or world dependencies. */
#ifndef ENGINE_AI_NAVIGATION_H
#define ENGINE_AI_NAVIGATION_H

#include <stdbool.h>
#include <stdint.h>

enum {
    ENGINE_AI_NAV_NODES_MAX = 256,
    ENGINE_AI_NAV_LINKS_MAX = 2048,
    ENGINE_AI_NAV_ROUTE_LINKS_MAX = ENGINE_AI_NAV_NODES_MAX - 1
};
#define ENGINE_AI_NAV_INDEX_NONE UINT32_MAX

/* Directed edge. Reverse travel requires a separate edge. Costs must be positive.
 * traversal_id == 0 means ordinary travel; other IDs name game-owned traversal actions.
 * required_capabilities is an all-required bit mask, not an actor/species enum. */
struct engine_ai_nav_link {
    uint32_t source_node;
    uint32_t destination_node;
    uint32_t cost;
    uint32_t required_capabilities;
    uint32_t traversal_id;
    bool enabled;
};

/* Caller-owned immutable snapshot. Node/link indices are local to this revision.
 * Keep the descriptor and link array alive and unchanged throughout a search.
 * Publish a new revision for topology, availability, cost, or traversal changes. */
struct engine_ai_nav_graph {
    const struct engine_ai_nav_link *links;
    uint32_t node_count;
    uint32_t link_count;
    uint64_t revision;
};

enum engine_ai_nav_status {
    ENGINE_AI_NAV_INVALID = 0,
    ENGINE_AI_NAV_RUNNING,
    ENGINE_AI_NAV_FOUND,
    ENGINE_AI_NAV_NO_PATH,
    ENGINE_AI_NAV_STALE
};

/* Link indices preserve special traversal steps; do not smooth them away.
 * An empty FOUND route means the start node is already the goal. */
struct engine_ai_nav_route {
    uint64_t revision;
    uint64_t cost;
    uint32_t link_count;
    uint32_t link_indices[ENGINE_AI_NAV_ROUTE_LINKS_MAX];
};

/* Caller-owned workspace, one per in-flight search; never serialize raw pointers.
 * Treat fields as private. No heap allocation, recursion, callbacks, or globals. */
struct engine_ai_nav_search {
    const struct engine_ai_nav_graph *graph;
    uint64_t revision;
    uint64_t distances[ENGINE_AI_NAV_NODES_MAX];
    uint32_t predecessor_links[ENGINE_AI_NAV_NODES_MAX];
    bool closed[ENGINE_AI_NAV_NODES_MAX];
    uint32_t start_node;
    uint32_t goal_node;
    uint32_t capabilities;
    enum engine_ai_nav_status status;
};

/* Non-null, non-overlapping objects are programmer contracts. Invalid graph data
 * returns INVALID. begin clears workspace; result clears route on every failure.
 * Each step settles at most expansion_budget nodes (1..NODES_MAX). Tie-breaking
 * uses node index, then link storage order. Positive integer costs avoid FP drift.
 * Worst-case step: budget * (NODES_MAX + LINKS_MAX) inspections, no hidden work.
 * Pass the current graph to step/result; replacement snapshots invalidate searches. */
enum engine_ai_nav_status engine_ai_nav_begin(struct engine_ai_nav_search *search,
    const struct engine_ai_nav_graph *graph, uint32_t start_node, uint32_t goal_node,
    uint32_t capabilities);
enum engine_ai_nav_status engine_ai_nav_step(struct engine_ai_nav_search *search,
    const struct engine_ai_nav_graph *graph, uint32_t expansion_budget);
enum engine_ai_nav_status engine_ai_nav_result(const struct engine_ai_nav_search *search,
    const struct engine_ai_nav_graph *graph, struct engine_ai_nav_route *route);

#endif
