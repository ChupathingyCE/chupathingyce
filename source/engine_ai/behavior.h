/* Immutable behavior trees. Game-owned leaf IDs carry no engine policy. */
#ifndef ENGINE_AI_BEHAVIOR_H
#define ENGINE_AI_BEHAVIOR_H

#include <stdbool.h>
#include <stdint.h>

enum {
    ENGINE_AI_BEHAVIOR_NODE_MAX = 128,
    ENGINE_AI_BEHAVIOR_DEPTH_MAX = 16
};

typedef enum engine_ai_behavior_kind {
    ENGINE_AI_BEHAVIOR_LEAF = 0,
    ENGINE_AI_BEHAVIOR_PRIORITY,
    ENGINE_AI_BEHAVIOR_SEQUENCE
} engine_ai_behavior_kind;

typedef struct engine_ai_behavior_node {
    engine_ai_behavior_kind kind;
    uint32_t child_first;
    uint32_t child_count;
    uint32_t leaf_id;
} engine_ai_behavior_node;

typedef struct engine_ai_behavior_definition {
    const engine_ai_behavior_node *nodes;
    uint32_t node_count;
    uint32_t root;
} engine_ai_behavior_definition;

typedef enum engine_ai_behavior_status {
    ENGINE_AI_BEHAVIOR_VALID = 0,
    ENGINE_AI_BEHAVIOR_INVALID,
    ENGINE_AI_BEHAVIOR_TOO_DEEP
} engine_ai_behavior_status;

/* Children are contiguous node indices in authored priority/sequence order.
 * Leaves have a nonzero game-owned ID and zero child fields. Composites have
 * at least one child and a zero leaf ID. Every node must occur exactly once
 * below the root: DAG sharing, cycles, and disconnected nodes are rejected.
 * Depth includes the root. Validation uses bounded stack storage, no heap or
 * recursion, and O(node_count) work. Definition/storage must remain immutable
 * during validation and future execution. Validation does not execute leaves,
 * establish leaf availability, or make caller mutations safe.
 */
engine_ai_behavior_status engine_ai_behavior_validate(
    const engine_ai_behavior_definition *definition);

typedef enum engine_ai_behavior_result {
    ENGINE_AI_BEHAVIOR_SUCCESS = 0,
    ENGINE_AI_BEHAVIOR_FAILURE,
    ENGINE_AI_BEHAVIOR_RUNNING,
    ENGINE_AI_BEHAVIOR_YIELDED,
    ENGINE_AI_BEHAVIOR_ERROR
} engine_ai_behavior_result;

typedef struct engine_ai_behavior_frame {
    uint32_t node;
    uint32_t child;
} engine_ai_behavior_frame;

typedef struct engine_ai_behavior_state {
    engine_ai_behavior_frame frames[ENGINE_AI_BEHAVIOR_DEPTH_MAX];
    uint32_t depth;
    uint32_t running_node; /* NODE_MAX means no running leaf. */
    uint32_t initialized;
    uint32_t faulted;
    uint32_t pending; /* Zero, or terminal result + 1 awaiting propagation. */
} engine_ai_behavior_state;

typedef engine_ai_behavior_result (*engine_ai_behavior_tick_leaf)(
    void *context, uint32_t node, uint32_t leaf_id);
typedef void (*engine_ai_behavior_cancel_leaf)(
    void *context, uint32_t node, uint32_t leaf_id);

enum { ENGINE_AI_BEHAVIOR_TRACE_MAX = 256 };

typedef enum engine_ai_behavior_trace_kind {
    ENGINE_AI_BEHAVIOR_TRACE_SELECT = 0,
    ENGINE_AI_BEHAVIOR_TRACE_REJECT,
    ENGINE_AI_BEHAVIOR_TRACE_LEAF,
    ENGINE_AI_BEHAVIOR_TRACE_RELEVANCY,
    ENGINE_AI_BEHAVIOR_TRACE_CANCEL,
    ENGINE_AI_BEHAVIOR_TRACE_YIELD
} engine_ai_behavior_trace_kind;

typedef struct engine_ai_behavior_trace_event {
    engine_ai_behavior_trace_kind kind;
    uint32_t node;
    engine_ai_behavior_result result;
} engine_ai_behavior_trace_event;

typedef struct engine_ai_behavior_trace {
    engine_ai_behavior_trace_event events[ENGINE_AI_BEHAVIOR_TRACE_MAX];
    uint32_t capacity; /* 0..TRACE_MAX; zero is a valid drop-all sink. */
    uint32_t count;
    bool truncated;
} engine_ai_behavior_trace;

/* Optional caller-owned append-only diagnostics, not persistent decision state.
 * Initialize count=0, truncated=false, and capacity before use; reset at tick/pass
 * boundaries as desired. Full sinks retain the prefix and set truncated without
 * affecting execution. No allocation, callbacks, or additional interpreter work
 * units. Events are ordered; SELECT is traversal, not proof of subtree relevancy;
 * REJECT is a failed priority child; RELEVANCY records eligibility checks.
 * LEAF records normalized callback results (invalid results become ERROR).
 * CANCEL precedes the cancellation callback. YIELD uses NODE_MAX as its node.
 * Root leaves have no SELECT event. Invalid arguments emit nothing. Trace storage
 * must not overlap state, definitions, or callback context; callbacks may not
 * mutate it. Identity/tick/revision labels and publication are caller-owned.
 * Do not serialize raw structs; traces are derived and discarded on restore.
 */
typedef struct engine_ai_behavior_callbacks {
    void *context;
    engine_ai_behavior_tick_leaf tick;
    engine_ai_behavior_cancel_leaf cancel;
    engine_ai_behavior_trace *trace; /* NULL disables diagnostics. */
} engine_ai_behavior_callbacks;

/* Initialize only fresh/inactive storage: never overwrite an active reservation.
 * State is caller-owned and opaque by contract; definition must stay unchanged
 * and alive until cancellation/completion. Separate agents require separate state.
 * Step spends one work unit per descent, leaf tick, or result propagation; budgets
 * are 1..2*NODE_MAX. YIELDED retains progress, not a behavior failure. RUNNING
 * resumes that leaf on the next step. Priority tries children until non-failure;
 * sequence tries children until non-success. Successful prefixes are not repeated.
 * Completed roots restart on the next step. This is memory traversal, NOT reactive
 * priority reevaluation: callers cancel before changing goals/interrupting work.
 * Cancel notifies exactly the running leaf, then clears all composite progress.
 * Callbacks must not reenter or mutate state/definition. Tick returns only SUCCESS,
 * FAILURE, RUNNING, or ERROR. ERROR faults state until cancel; it may have emitted
 * intents, so caller must discard derived output/restore decision state as needed.
 * Leaf-local state and external effects remain game-owned. Snapshot fields
 * explicitly with leaf-local state; never persist raw pointers or struct padding.
 */
engine_ai_behavior_status engine_ai_behavior_begin(
    engine_ai_behavior_state *state, const engine_ai_behavior_definition *definition);
engine_ai_behavior_result engine_ai_behavior_step(
    engine_ai_behavior_state *state, const engine_ai_behavior_definition *definition,
    uint32_t work_budget, const engine_ai_behavior_callbacks *callbacks);
engine_ai_behavior_result engine_ai_behavior_cancel(
    engine_ai_behavior_state *state, const engine_ai_behavior_definition *definition,
    const engine_ai_behavior_callbacks *callbacks);

/* Reevaluate earlier siblings of active priority ancestors, root first.
 * Relevancy is game-owned, side-effect-free subtree eligibility, not a leaf tick.
 * The first relevant sibling replaces the running branch: cancel runs before any
 * replacement leaf tick, and sequence progress above that selector is retained.
 * Requires a running, non-faulted state. SUCCESS means replacement prepared;
 * RUNNING means no replacement; ERROR means invalid arguments/state.
 * Scan budget is 1..NODE_MAX. All candidate siblings must fit before callbacks
 * run, otherwise YIELDED leaves state/reservations untouched. This atomic scan
 * does not accumulate budget: callers must provide sufficient budget periodically
 * to bound interruption latency. Relevancy and cancel must not mutate/reenter
 * interpreter state or definitions. Definition validation is additional O(N).
 */
typedef bool (*engine_ai_behavior_relevant)(void *context, uint32_t node);
engine_ai_behavior_result engine_ai_behavior_reevaluate(
    engine_ai_behavior_state *state, const engine_ai_behavior_definition *definition,
    uint32_t scan_budget, engine_ai_behavior_relevant relevant,
    const engine_ai_behavior_callbacks *callbacks);

#endif
