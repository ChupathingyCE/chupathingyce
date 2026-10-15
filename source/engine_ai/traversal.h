/* Pointer-free per-agent execution state for revision-scoped navigation routes. */
#ifndef ENGINE_AI_TRAVERSAL_H
#define ENGINE_AI_TRAVERSAL_H

#include "navigation.h"

#include <stdbool.h>
#include <stdint.h>

enum engine_ai_traversal_phase {
    ENGINE_AI_TRAVERSAL_IDLE = 0,
    ENGINE_AI_TRAVERSAL_APPROACH,
    ENGINE_AI_TRAVERSAL_EXECUTE,
    ENGINE_AI_TRAVERSAL_COMPLETE,
    ENGINE_AI_TRAVERSAL_FAILED,
    ENGINE_AI_TRAVERSAL_CANCELLED
};

enum engine_ai_traversal_result {
    ENGINE_AI_TRAVERSAL_INVALID = 0,
    ENGINE_AI_TRAVERSAL_APPROACHING,
    ENGINE_AI_TRAVERSAL_WAITING,
    ENGINE_AI_TRAVERSAL_EXECUTING,
    ENGINE_AI_TRAVERSAL_COMPLETED,
    ENGINE_AI_TRAVERSAL_FAILED_RESULT,
    ENGINE_AI_TRAVERSAL_REPLAN,
    ENGINE_AI_TRAVERSAL_CANCELLED_RESULT
};

enum engine_ai_traversal_availability {
    ENGINE_AI_TRAVERSAL_AVAILABILITY_ERROR = 0,
    ENGINE_AI_TRAVERSAL_AVAILABLE,
    ENGINE_AI_TRAVERSAL_UNAVAILABLE
};

enum engine_ai_traversal_reservation_result {
    ENGINE_AI_TRAVERSAL_RESERVATION_ERROR = 0,
    ENGINE_AI_TRAVERSAL_RESERVATION_DENIED,
    ENGINE_AI_TRAVERSAL_RESERVATION_GRANTED
};

enum engine_ai_traversal_execution_result {
    ENGINE_AI_TRAVERSAL_EXECUTION_ERROR = 0,
    ENGINE_AI_TRAVERSAL_EXECUTION_RUNNING,
    ENGINE_AI_TRAVERSAL_EXECUTION_COMPLETE,
    ENGINE_AI_TRAVERSAL_EXECUTION_BLOCKED,
    ENGINE_AI_TRAVERSAL_EXECUTION_FAILED
};

enum engine_ai_traversal_release_reason {
    ENGINE_AI_TRAVERSAL_RELEASE_COMPLETE = 1,
    ENGINE_AI_TRAVERSAL_RELEASE_FAILED,
    ENGINE_AI_TRAVERSAL_RELEASE_CANCELLED
};

/* This state owns a route copy and scalar reservation identity only. It contains
 * no pointers and may be copied by a timeline provider; game-owned reservation
 * state must be snapshotted by that provider as well. Initialize via begin(). */
struct engine_ai_traversal_state {
    struct engine_ai_nav_route route;
    uint64_t reservation_id;
    uint32_t next_link;
    uint32_t active_source_node;
    uint32_t active_destination_node;
    uint32_t active_traversal_id;
    uint32_t phase;
    uint32_t terminal_result;
    uint32_t reservation_held;
};

/* Availability is checked immediately before reserving and on every executing
 * tick. It must report dynamic game policy (e.g. a lift that became unavailable). */
struct engine_ai_traversal_callbacks {
    enum engine_ai_traversal_availability (*availability)(void *context,
        uint32_t traversal_id, uint32_t source_node, uint32_t destination_node);
    /* Denial is retryable and must leave reservation_id zero. Grant must return a
     * nonzero opaque token. Reservation ownership remains with game policy. */
    enum engine_ai_traversal_reservation_result (*reserve)(void *context,
        uint32_t traversal_id, uint32_t source_node, uint32_t destination_node,
        uint64_t *reservation_id);
    enum engine_ai_traversal_execution_result (*execute)(void *context,
        uint32_t traversal_id, uint32_t source_node, uint32_t destination_node,
        uint64_t reservation_id);
    /* Called exactly once for every granted reservation, including cancellation. */
    void (*release)(void *context, uint32_t traversal_id, uint64_t reservation_id,
        enum engine_ai_traversal_release_reason reason);
};

/* Route/link continuity, positive costs, capability admission and revision are
 * checked before state is replaced. On failure, state is byte-for-byte unchanged. */
bool engine_ai_traversal_begin(struct engine_ai_traversal_state *state,
    const struct engine_ai_nav_graph *graph, const struct engine_ai_nav_route *route,
    uint32_t capabilities);

/* Approach targets are graph anchors: destination for ordinary links, source for
 * special links. Each ordinary link is exposed separately, so callers cannot
 * smooth across a special link. Output is unchanged on failure. */
bool engine_ai_traversal_target_node(const struct engine_ai_traversal_state *state,
    const struct engine_ai_nav_graph *graph, uint32_t *target_node);

/* Call once per simulation tick. target_reached advances an ordinary waypoint or
 * starts a special action at its source anchor. resource_revision is the current
 * world resource revision. Stale revisions, lost capability, and blocked links
 * fault the route with REPLAN; phase failures are terminal. Reservation denial
 * returns WAITING and retries on a later call. Invalid inputs do not mutate state. */
enum engine_ai_traversal_result engine_ai_traversal_step(
    struct engine_ai_traversal_state *state,
    const struct engine_ai_nav_graph *graph, uint64_t resource_revision,
    uint32_t capabilities, bool target_reached,
    const struct engine_ai_traversal_callbacks *callbacks, void *context);

/* Idempotent for terminal states. A held reservation requires a release callback;
 * without it cancellation is rejected without changing state. */
enum engine_ai_traversal_result engine_ai_traversal_cancel(
    struct engine_ai_traversal_state *state,
    const struct engine_ai_traversal_callbacks *callbacks, void *context);

#endif
