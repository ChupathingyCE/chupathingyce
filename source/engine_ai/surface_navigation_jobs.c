/* port: bounded shared search work; no per-client A* arrays or allocation. */
#include "surface_navigation_jobs.h"
#include <assert.h>
#include <math.h>
#include <string.h>

typedef char sn_jobs_work_check[
    SN_JOBS_WORKSPACES_MAX * SN_JOBS_EXPANSIONS_PER_WORKSPACE ==
    SN_JOBS_EXPANSIONS_PER_TICK ? 1 : -1];
typedef char sn_jobs_clients_check[
    SN_JOBS_WORKSPACES_MAX > 0 && SN_JOBS_WORKSPACES_MAX <= SN_JOBS_CLIENTS_MAX ? 1 : -1];
typedef char sn_jobs_slice_check[
    SN_JOBS_EXPANSIONS_PER_WORKSPACE > 0 && SN_JOBS_EXPANSIONS_PER_WORKSPACE <= 256 ? 1 : -1];
typedef char sn_jobs_steps_check[
    (uint64_t)SN_JOBS_QUERY_STEPS_MAX * SN_JOBS_EXPANSIONS_PER_WORKSPACE >= SN_POLYGONS_MAX ? 1 : -1];
typedef char sn_jobs_straightens_check[
    SN_JOBS_STRAIGHTENS_PER_TICK > 0 && SN_JOBS_STRAIGHTENS_PER_TICK <= SN_JOBS_WORKSPACES_MAX ? 1 : -1];

static bool same_key(struct sn_key a, struct sn_key b)
{
    return a.map_identity == b.map_identity && a.overlay_revision == b.overlay_revision &&
        a.checksum == b.checksum && a.bsp_index == b.bsp_index &&
        a.agent_identity == b.agent_identity && a.format_version == b.format_version;
}

static bool current(const struct sn_jobs_client *client, const struct sn_resource *resource,
    uint64_t obstacles)
{
    return resource && resource->ready && resource->polygon_count <= SN_POLYGONS_MAX &&
        same_key(client->key, resource->key) && client->revision == resource->revision &&
        client->request.obstacle_revision == obstacles;
}

static bool matches(const struct sn_jobs *jobs, uint32_t client, uint64_t token)
{
    return client < SN_JOBS_CLIENTS_MAX && jobs->clients[client].occupied &&
        jobs->clients[client].token == token;
}

static bool finite_point(struct sn_point p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z) &&
        fabsf(p.x) < 1000000.f && fabsf(p.y) < 1000000.f && fabsf(p.z) < 1000000.f;
}

/* sn_project_near's own limits, so that a begun query never asks it for less. */
static bool valid_ground(float depth, float horizontal)
{
    return depth == 0.f || (isfinite(depth) && depth > 0.f && depth <= 100.f &&
        isfinite(horizontal) && horizontal > 0.f && horizontal <= 10.f);
}

/* Remove one entry while preserving FIFO order; at most 32 reads/writes. */
static void remove_waiting(struct sn_jobs *jobs, uint32_t client)
{
    uint32_t read, write = 0;
    assert(jobs->waiting_count <= SN_JOBS_CLIENTS_MAX);
    for (read = 0; read < jobs->waiting_count; read++) {
        if (jobs->waiting[read] != client) {
            assert(write < SN_JOBS_CLIENTS_MAX);
            jobs->waiting[write++] = jobs->waiting[read];
        }
    }
    jobs->waiting_count = write;
    jobs->clients[client].waiting = false;
}

void sn_jobs_reset(struct sn_jobs *jobs)
{
    uint32_t index;
    assert(jobs);
    memset(jobs->clients, 0, sizeof(jobs->clients));
    /* At most four workspace owners; the query arrays need no bulk clearing. */
    for (index = 0; index < SN_JOBS_WORKSPACES_MAX; index++) jobs->owners[index] = SN_NONE;
    jobs->waiting_count = 0;
    jobs->last_tick = jobs->found_count = 0;
    jobs->starts_this_tick = jobs->work_this_tick = jobs->straightens_this_tick = 0;
    jobs->tick_valid = false;
}

bool sn_jobs_submit(struct sn_jobs *jobs, uint32_t client, uint64_t token,
    const struct sn_resource *resource, const struct sn_jobs_request *request)
{
    struct sn_jobs_client *entry;
    assert(jobs);
    if (client >= SN_JOBS_CLIENTS_MAX || jobs->clients[client].occupied || !request ||
        !resource || !resource->ready || resource->polygon_count > SN_POLYGONS_MAX ||
        !finite_point(request->start) || !finite_point(request->goal) ||
        !valid_ground(request->ground_depth, request->ground_horizontal) ||
        jobs->waiting_count >= SN_JOBS_CLIENTS_MAX) return false;
    entry = &jobs->clients[client];
    memset(entry, 0, sizeof(*entry));
    entry->key = resource->key;
    entry->revision = resource->revision;
    entry->request = *request;
    entry->token = token;
    entry->workspace = SN_NONE;
    entry->status = SN_RUNNING;
    entry->occupied = entry->waiting = true;
    entry->diagnostics.start = entry->diagnostics.goal = SN_NONE;
    entry->diagnostics.start_point = request->start;
    entry->diagnostics.goal_point = request->goal;
    assert(jobs->waiting_count < SN_JOBS_CLIENTS_MAX);
    jobs->waiting[jobs->waiting_count++] = client;
    return true;
}

bool sn_jobs_refresh(struct sn_jobs *jobs, uint32_t client, uint64_t token,
    struct sn_point start, struct sn_point goal)
{
    struct sn_jobs_client *entry;
    assert(jobs);
    if (!matches(jobs, client, token) || !jobs->clients[client].waiting ||
        !finite_point(start) || !finite_point(goal)) return false;
    entry = &jobs->clients[client];
    entry->request.start = entry->diagnostics.start_point = start;
    entry->request.goal = entry->diagnostics.goal_point = goal;
    return true;
}

struct clear_context {
    sn_jobs_segment_clear clear;
    void *context;
    uint32_t client;
    uint64_t token;
};

static bool clear_segment(void *context, struct sn_point a, uint32_t a_polygon,
    struct sn_point b, uint32_t b_polygon)
{
    const struct clear_context *call = context;
    assert(call && call->clear);
    return call->clear(call->context, call->client, call->token, a, a_polygon, b, b_polygon);
}

static void capture_diagnostics(struct sn_jobs_client *client, const struct sn_query *query)
{
    client->diagnostics.start = query->start;
    client->diagnostics.goal = query->goal;
    /* A failed projection leaves its output undefined: preserve the submitted
       point until a polygon has actually been accepted. */
    if (query->start != SN_NONE) client->diagnostics.start_point = query->start_point;
    if (query->goal != SN_NONE) client->diagnostics.goal_point = query->goal_point;
    client->diagnostics.rejected_edges = query->rejected_edges;
    client->diagnostics.start_rejected = query->start_rejected;
    client->diagnostics.goal_rejected = query->goal_rejected;
    client->diagnostics.steps = client->steps;
}

/* An endpoint on the floor below it, or within the horizontal limit of it;
   unchanged when there is none (the query then reports a bad endpoint). */
static struct sn_point place(const struct sn_resource *resource, struct sn_point point,
    float depth, float horizontal)
{
    struct sn_point projected;
    uint32_t polygon;
    if (depth > 0.f && sn_project_near(resource, point, depth, horizontal, &polygon, &projected) ==
        SN_FOUND) return projected;
    return point;
}

/* The found route waiting longest to be string-pulled: a scan of four. */
static uint32_t oldest_found(const struct sn_jobs *jobs)
{
    uint32_t index, best = SN_NONE;
    for (index = 0; index < SN_JOBS_WORKSPACES_MAX; index++) {
        uint32_t owner = jobs->owners[index];
        if (owner == SN_NONE || !jobs->clients[owner].straightening) continue;
        if (best == SN_NONE ||
            jobs->clients[owner].found_order < jobs->clients[jobs->owners[best]].found_order)
            best = index;
    }
    return best;
}

void sn_jobs_step(struct sn_jobs *jobs, const struct sn_resource *resource,
    uint64_t obstacles, uint64_t tick, sn_jobs_segment_clear clear, void *context)
{
    uint32_t index;
    assert(jobs);
    if (jobs->tick_valid && jobs->last_tick == tick) return;
    jobs->tick_valid = true;
    jobs->last_tick = tick;
    jobs->starts_this_tick = jobs->work_this_tick = jobs->straightens_this_tick = 0;
    /* At most 32 requests, including completed results awaiting consumption. */
    for (index = 0; index < SN_JOBS_CLIENTS_MAX; index++) {
        struct sn_jobs_client *client = &jobs->clients[index];
        if (!client->occupied || current(client, resource, obstacles)) continue;
        if (client->waiting) remove_waiting(jobs, index);
        client->status = SN_STALE;
        client->straightening = false;
    }
    /* At most four begins and four bounded search slices. A terminal query
       retains its workspace, so it cannot be overwritten before take/cancel. */
    for (index = 0; index < SN_JOBS_WORKSPACES_MAX; index++) {
        struct sn_jobs_client *client;
        struct sn_query *query = &jobs->queries[index];
        struct clear_context call;
        enum sn_status status;
        uint32_t owner = jobs->owners[index];
        if (owner == SN_NONE && jobs->waiting_count) {
            owner = jobs->waiting[0];
            assert(owner < SN_JOBS_CLIENTS_MAX);
            client = &jobs->clients[owner];
            assert(client->occupied && client->waiting && client->workspace == SN_NONE);
            assert(resource); /* (a missing resource made every request stale above) */
            remove_waiting(jobs, owner);
            jobs->owners[index] = owner;
            client->workspace = index;
            client->status = sn_query_begin(query, resource,
                place(resource, client->request.start, client->request.ground_depth,
                    client->request.ground_horizontal),
                place(resource, client->request.goal, client->request.ground_depth,
                    client->request.ground_horizontal), obstacles);
            query->check_goal_segment = client->request.check_goal_segment;
            capture_diagnostics(client, query);
            jobs->starts_this_tick++;
        }
        if (owner == SN_NONE) continue;
        assert(owner < SN_JOBS_CLIENTS_MAX);
        client = &jobs->clients[owner];
        assert(client->occupied && client->workspace == index && !client->waiting);
        if (client->status != SN_RUNNING || client->straightening) continue;
        call.clear = clear;
        call.context = context;
        call.client = owner;
        call.token = client->token;
        assert(client->steps < SN_JOBS_QUERY_STEPS_MAX);
        status = sn_query_step(query, resource, obstacles,
            SN_JOBS_EXPANSIONS_PER_WORKSPACE, clear ? clear_segment : NULL, &call);
        client->steps++;
        jobs->work_this_tick += SN_JOBS_EXPANSIONS_PER_WORKSPACE;
        if (status == SN_RUNNING && client->steps == SN_JOBS_QUERY_STEPS_MAX)
            status = SN_CAPACITY;
        /* (the route lives in the workspace until it is taken) */
        if (status == SN_FOUND)
            status = sn_query_result(query, resource, obstacles, &jobs->routes[index]);
        if (status == SN_FOUND && client->request.straighten) {
            client->straightening = true;
            client->found_order = ++jobs->found_count;
            status = SN_RUNNING;
        }
        client->status = status;
        capture_diagnostics(client, query);
    }
    /* At most one route string-pulled per tick, each with at most
       SN_STRAIGHTEN_CHECKS_MAX collision checks; the rest wait their turn. */
    for (index = 0; index < SN_JOBS_STRAIGHTENS_PER_TICK; index++) {
        uint32_t workspace = oldest_found(jobs);
        struct sn_jobs_client *client;
        struct clear_context call;
        if (workspace == SN_NONE) break;
        client = &jobs->clients[jobs->owners[workspace]];
        assert(resource && client->status == SN_RUNNING);
        if (clear) {
            call.clear = clear;
            call.context = context;
            call.client = jobs->owners[workspace];
            call.token = client->token;
            /* (an invalid route stays as it was found) */
            (void)sn_route_straighten_checked(&jobs->routes[workspace], resource, clear_segment, &call);
        } else sn_route_straighten(&jobs->routes[workspace], resource);
        client->straightening = false;
        client->status = SN_FOUND;
        jobs->straightens_this_tick++;
    }
    assert(jobs->starts_this_tick <= SN_JOBS_WORKSPACES_MAX);
    assert(jobs->work_this_tick <= SN_JOBS_EXPANSIONS_PER_TICK);
    assert(jobs->straightens_this_tick <= SN_JOBS_STRAIGHTENS_PER_TICK);
}

bool sn_jobs_active(const struct sn_jobs *jobs, uint32_t client, uint64_t token)
{
    assert(jobs);
    return matches(jobs, client, token);
}

enum sn_status sn_jobs_status(const struct sn_jobs *jobs, uint32_t client, uint64_t token)
{
    assert(jobs);
    return matches(jobs, client, token) ? jobs->clients[client].status : SN_INVALID;
}

bool sn_jobs_cancel(struct sn_jobs *jobs, uint32_t client, uint64_t token)
{
    struct sn_jobs_client *request;
    assert(jobs);
    if (!matches(jobs, client, token)) return false;
    request = &jobs->clients[client];
    if (request->waiting) remove_waiting(jobs, client);
    if (request->workspace != SN_NONE) {
        assert(request->workspace < SN_JOBS_WORKSPACES_MAX);
        assert(jobs->owners[request->workspace] == client);
        jobs->owners[request->workspace] = SN_NONE;
    }
    request->occupied = false;
    request->straightening = false;
    request->workspace = SN_NONE;
    request->status = SN_INVALID;
    return true;
}

enum sn_status sn_jobs_take(struct sn_jobs *jobs, uint32_t client, uint64_t token,
    const struct sn_resource *resource, uint64_t obstacles, struct sn_route *route,
    struct sn_jobs_diagnostics *diagnostics)
{
    struct sn_jobs_client *request;
    enum sn_status status;
    assert(jobs);
    if (!route || !matches(jobs, client, token)) return SN_INVALID;
    request = &jobs->clients[client];
    if (request->status == SN_RUNNING) return SN_RUNNING;
    route->count = route->corridor_count = 0;
    status = current(request, resource, obstacles) ? request->status : SN_STALE;
    if (status == SN_FOUND) {
        const struct sn_route *found;
        assert(request->workspace < SN_JOBS_WORKSPACES_MAX);
        found = &jobs->routes[request->workspace];
        if (sn_route_current(found, resource, obstacles)) *route = *found;
        else status = SN_STALE;
    }
    if (diagnostics) *diagnostics = request->diagnostics;
    sn_jobs_cancel(jobs, client, token);
    return status;
}
