/* port: fixed-memory, single-threaded scheduling for BSP surface searches.
   Up to 32 clients each hold at most one request; four shared A* workspaces
   serve them in order. Per tick the work is bounded whatever the number of
   clients: at most four queries begin (each placing its two endpoints), 64
   polygons are expanded, and one found route is string-pulled. */
#ifndef ENGINE_AI_SURFACE_NAVIGATION_JOBS_H
#define ENGINE_AI_SURFACE_NAVIGATION_JOBS_H

#include "surface_navigation.h"

enum {
    SN_JOBS_CLIENTS_MAX = 32,
    SN_JOBS_WORKSPACES_MAX = 4,
    SN_JOBS_EXPANSIONS_PER_WORKSPACE = 16,
    SN_JOBS_EXPANSIONS_PER_TICK = 64,
    /* each at most SN_STRAIGHTEN_CHECKS_MAX collision checks */
    SN_JOBS_STRAIGHTENS_PER_TICK = 1,
    /* enough slices to expand every polygon of the largest resource once */
    SN_JOBS_QUERY_STEPS_MAX = (SN_POLYGONS_MAX + SN_JOBS_EXPANSIONS_PER_WORKSPACE - 1) /
        SN_JOBS_EXPANSIONS_PER_WORKSPACE
};

/* A client's request. Its points are snapshots; nothing is borrowed. */
struct sn_jobs_request {
    struct sn_point start, goal;
    /* When its query begins, each endpoint is placed with sn_project_near and
       these limits (0 < depth <= 100, 0 < horizontal <= 10); a depth of 0
       uses the endpoints as given (sn_query_begin projects them). */
    float ground_depth, ground_horizontal;
    uint64_t obstacle_revision;
    bool check_goal_segment; /* sn_query.check_goal_segment */
    /* String-pull a found route before it is returned: checked with the
       step's callback (sn_route_straighten_checked), unchecked without one. */
    bool straighten;
};

struct sn_jobs_diagnostics {
    struct sn_point start_point, goal_point;
    uint32_t start, goal, rejected_edges;
    uint32_t steps; /* search slices run */
    bool start_rejected, goal_rejected;
};

/* Request metadata is small. Only the four workspaces own A* scratch arrays
   and a found route. */
struct sn_jobs_client {
    struct sn_key key;
    struct sn_jobs_request request;
    struct sn_jobs_diagnostics diagnostics;
    uint64_t token, revision, found_order;
    uint32_t workspace, steps;
    enum sn_status status;
    bool occupied, waiting, straightening;
};

struct sn_jobs {
    struct sn_query queries[SN_JOBS_WORKSPACES_MAX];
    struct sn_route routes[SN_JOBS_WORKSPACES_MAX];
    struct sn_jobs_client clients[SN_JOBS_CLIENTS_MAX];
    uint32_t owners[SN_JOBS_WORKSPACES_MAX];
    uint32_t waiting[SN_JOBS_CLIENTS_MAX], waiting_count;
    uint64_t last_tick, found_count;
    /* What the last step did: begins, expansion budget granted (a query that
       finishes early uses less) and routes string-pulled. */
    uint32_t starts_this_tick, work_this_tick, straightens_this_tick;
    bool tick_valid;
};

/* Resolve the client's current unit through a checked datum lookup here. The
   token identifies the request, and no unit pointer survives between ticks.
   The callback must be bounded and must not reenter/mutate the scheduler. */
typedef bool (*sn_jobs_segment_clear)(void *context, uint32_t client,
    uint64_t token, struct sn_point a, uint32_t a_polygon,
    struct sn_point b, uint32_t b_polygon);

/* Required before first use and on map invalidation. Does not clear large query
   arrays: sn_query_begin initializes all scratch it will read when reused. */
void sn_jobs_reset(struct sn_jobs *jobs);

/* Queue a request at the back. False, changing nothing, for a busy client
   (take or cancel its request first), a resource that is not ready, a NULL
   request, non-finite endpoints or ground limits out of range. Use a new
   token for every request, so a delayed consumer cannot take another's work. */
bool sn_jobs_submit(struct sn_jobs *jobs, uint32_t client, uint64_t token,
    const struct sn_resource *resource, const struct sn_jobs_request *request);
/* Replace a waiting request's endpoints, keeping its place in the queue. False
   once its query has begun (a begun query's endpoints never change) and for
   non-finite points. */
bool sn_jobs_refresh(struct sn_jobs *jobs, uint32_t client, uint64_t token,
    struct sn_point start, struct sn_point goal);

/* Advance the queue once per tick; repeated calls with the same tick do no
   work. Waiting clients begin in order as workspaces free. Each query gets at
   most SN_JOBS_QUERY_STEPS_MAX slices before SN_CAPACITY. A completed query
   keeps its workspace (and its route) until take/cancel, so consumers must
   drain results for waiting clients to progress. Found routes waiting to be
   string-pulled are taken oldest first. A request whose key, revision or
   obstacle revision no longer matches becomes SN_STALE; a NULL resource
   makes every request stale. */
void sn_jobs_step(struct sn_jobs *jobs, const struct sn_resource *resource,
    uint64_t obstacle_revision, uint64_t tick, sn_jobs_segment_clear clear,
    void *context);

/* Whether the client still holds this request: waiting, under way, or with a
   result to take. */
bool sn_jobs_active(const struct sn_jobs *jobs, uint32_t client, uint64_t token);
/* SN_RUNNING while waiting, searching or being string-pulled; else the
   result. A missing request or mismatched token is SN_INVALID (so is a query
   that failed as invalid: tell them apart with sn_jobs_active). Polling never
   changes scheduler state. */
enum sn_status sn_jobs_status(const struct sn_jobs *jobs, uint32_t client,
    uint64_t token);
/* Terminal results only: copy into route, optionally return diagnostics, and
   release the slot. SN_RUNNING leaves the request and outputs untouched. A
   NULL route or mismatched token returns SN_INVALID without releasing work.
   Resource/key/revisions are checked again before a found route is copied. */
enum sn_status sn_jobs_take(struct sn_jobs *jobs, uint32_t client, uint64_t token,
    const struct sn_resource *resource, uint64_t obstacle_revision,
    struct sn_route *route, struct sn_jobs_diagnostics *diagnostics);
bool sn_jobs_cancel(struct sn_jobs *jobs, uint32_t client, uint64_t token);

#endif
