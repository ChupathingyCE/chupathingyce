/* The shared search scheduler (surface_navigation_jobs.c) on a 10x10 grid of
   2 m floors: its bounds per tick, queue order, result lifetime and
   invalidation, without Halo. */
#include "../source/engine_ai/surface_navigation_jobs.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { GRID = 10 };
static struct sn_resource resource;
static struct sn_build build;
static struct sn_query query;
static struct sn_jobs jobs;
static struct sn_route route, plain;
static struct sn_jobs_diagnostics diagnostics;
static struct sn_agent agent = {0.2f, 1.f, 0.7f, 0.6f};
static struct sn_key key = {321, 0, 99, 0, 7, SN_FORMAT_VERSION};
static uint64_t tokens[SN_JOBS_CLIENTS_MAX], next_token = 100;
static uint32_t calls, refused_client = SN_NONE;
static uint64_t tick;

static int grid_polygon(void *context, uint32_t index, struct sn_polygon *out)
{
    uint32_t x = index % GRID, y = index / GRID, e;
    float left = 2.f * (float)x, bottom = 2.f * (float)y;
    (void)context;
    memset(out, 0, sizeof(*out));
    out->vertices[0] = (struct sn_point){left, bottom, 0.f};
    out->vertices[1] = (struct sn_point){left + 2.f, bottom, 0.f};
    out->vertices[2] = (struct sn_point){left + 2.f, bottom + 2.f, 0.f};
    out->vertices[3] = (struct sn_point){left, bottom + 2.f, 0.f};
    out->vertex_count = 4; out->normal.z = 1.f; out->width = 4.f; out->height = 4.f;
    for (e = 0; e < SN_VERTICES_MAX; e++) out->neighbors[e] = SN_NONE;
    out->neighbors[0] = y > 0 ? index - GRID : SN_NONE;
    out->neighbors[1] = x + 1 < GRID ? index + 1 : SN_NONE;
    out->neighbors[2] = y + 1 < GRID ? index + GRID : SN_NONE;
    out->neighbors[3] = x > 0 ? index - 1 : SN_NONE;
    return 1;
}

static void build_grid(void)
{
    uint32_t steps = 0;
    enum sn_status status = sn_build_begin(&build, &resource, GRID * GRID, key, 3, agent);
    while (status == SN_RUNNING && steps++ < 100)
        status = sn_build_step(&build, &resource, 256, grid_polygon, NULL);
    assert(status == SN_READY && resource.polygon_count == GRID * GRID);
}

/* Every call names the client whose request it checks, with its token. */
static bool clear(void *context, uint32_t client, uint64_t token, struct sn_point a,
    uint32_t ap, struct sn_point b, uint32_t bp)
{
    (void)context; (void)a; (void)b;
    assert(client < SN_JOBS_CLIENTS_MAX && token == tokens[client]);
    assert(ap < resource.polygon_count && bp < resource.polygon_count);
    calls++;
    return client != refused_client;
}

static struct sn_jobs_request request(float sx, float sy, float gx, float gy)
{
    struct sn_jobs_request r;
    memset(&r, 0, sizeof(r));
    r.start.x = sx; r.start.y = sy; r.goal.x = gx; r.goal.y = gy;
    r.check_goal_segment = true;
    r.straighten = true;
    return r;
}

static bool submit(uint32_t client, struct sn_jobs_request r)
{
    uint64_t token = next_token++;
    if (!sn_jobs_submit(&jobs, client, token, &resource, &r)) return false;
    tokens[client] = token;
    return true;
}

static void step(void)
{
    sn_jobs_step(&jobs, &resource, 0, ++tick, clear, NULL);
    assert(jobs.starts_this_tick <= SN_JOBS_WORKSPACES_MAX);
    assert(jobs.work_this_tick <= SN_JOBS_EXPANSIONS_PER_TICK);
    assert(jobs.straightens_this_tick <= SN_JOBS_STRAIGHTENS_PER_TICK);
}

static enum sn_status status_of(uint32_t client)
{
    return sn_jobs_status(&jobs, client, tokens[client]);
}

static void test_submit_validation(void)
{
    struct sn_jobs_request r = request(1, 1, 19, 19);
    sn_jobs_reset(&jobs);
    assert(!sn_jobs_submit(&jobs, SN_JOBS_CLIENTS_MAX, 1, &resource, &r));
    assert(!sn_jobs_submit(&jobs, 0, 1, &resource, NULL));
    assert(!sn_jobs_submit(&jobs, 0, 1, NULL, &r));
    resource.ready = false;
    assert(!sn_jobs_submit(&jobs, 0, 1, &resource, &r));
    resource.ready = true;
    r.start.x = NAN; assert(!submit(0, r)); r.start.x = 1.f;
    r.goal.y = INFINITY; assert(!submit(0, r)); r.goal.y = 19.f;
    r.start.z = 2000000.f; assert(!submit(0, r)); r.start.z = 0.f;
    r.ground_depth = -1.f; assert(!submit(0, r));
    r.ground_depth = NAN; assert(!submit(0, r));
    r.ground_depth = 101.f; assert(!submit(0, r));
    r.ground_depth = 3.f; r.ground_horizontal = 0.f; assert(!submit(0, r));
    r.ground_horizontal = 11.f; assert(!submit(0, r));
    r.ground_horizontal = NAN; assert(!submit(0, r));
    assert(jobs.waiting_count == 0 && !jobs.clients[0].occupied);
    /* (no placement: the horizontal limit is unused) */
    r.ground_depth = 0.f; assert(submit(1, r));
    r.ground_depth = 3.f; r.ground_horizontal = 1.f; assert(submit(0, r));
    /* One request per client: a busy client is never replaced. */
    assert(!sn_jobs_submit(&jobs, 0, 999, &resource, &r));
    assert(sn_jobs_active(&jobs, 0, tokens[0]) && !sn_jobs_active(&jobs, 0, 999));
    assert(status_of(0) == SN_RUNNING && sn_jobs_status(&jobs, 0, 999) == SN_INVALID);
    assert(jobs.waiting_count == 2 && jobs.waiting[0] == 1 && jobs.waiting[1] == 0);
    /* Refresh needs the token and finite points. */
    assert(!sn_jobs_refresh(&jobs, 0, 999, r.start, r.goal));
    r.goal.x = NAN;
    assert(!sn_jobs_refresh(&jobs, 0, tokens[0], r.start, r.goal));
}

/* Six clients, four workspaces: four begin, two wait in order, and the
   whole tick stays within its bounds. */
static void test_queue_and_bounds(void)
{
    uint32_t client, found = 0, ticks = 0;
    struct sn_point start = {1.f, 1.f, 0.f}, goal = {19.f, 1.f, 0.f};
    sn_jobs_reset(&jobs);
    for (client = 0; client < 6; client++)
        assert(submit(client, request(1.f, 1.f + 3.f * (float)client, 19.f, 19.f - 3.f * (float)client)));
    calls = 0;
    step();
    assert(jobs.starts_this_tick == 4 && jobs.work_this_tick == SN_JOBS_EXPANSIONS_PER_TICK);
    assert(jobs.waiting_count == 2 && jobs.waiting[0] == 4 && jobs.waiting[1] == 5);
    for (client = 0; client < 4; client++)
        assert(jobs.clients[client].workspace == client && jobs.owners[client] == client);
    for (client = 0; client < 6; client++)
        assert(status_of(client) == SN_RUNNING || (client < 4 && status_of(client) == SN_FOUND));
    /* A begun query keeps its endpoints; a waiting one takes new ones in place. */
    assert(!sn_jobs_refresh(&jobs, 0, tokens[0], start, goal));
    assert(sn_jobs_refresh(&jobs, 5, tokens[5], start, goal));
    assert(jobs.waiting[0] == 4 && jobs.waiting[1] == 5);
    assert(jobs.clients[5].diagnostics.start_point.y == 1.f);
    /* The same tick again does nothing. */
    found = calls;
    sn_jobs_step(&jobs, &resource, 0, tick, clear, NULL);
    assert(calls == found && jobs.starts_this_tick == 4);
    /* Results keep their workspaces until taken: the waiting two never begin. */
    found = 0;
    while (found < 4 && ticks++ < 200) {
        uint32_t before = found;
        step();
        for (client = 0, found = 0; client < 4; client++) found += status_of(client) == SN_FOUND;
        assert(found - before <= SN_JOBS_STRAIGHTENS_PER_TICK);
        assert(jobs.waiting_count == 2 && status_of(4) == SN_RUNNING && status_of(5) == SN_RUNNING);
    }
    assert(found == 4 && ticks < 200);
    step();
    assert(jobs.starts_this_tick == 0 && jobs.work_this_tick == 0 && jobs.waiting_count == 2);
    /* Polling changes nothing; taking releases the workspace to the next. */
    assert(status_of(2) == SN_FOUND && sn_jobs_active(&jobs, 2, tokens[2]));
    assert(sn_jobs_take(&jobs, 2, tokens[2], &resource, 0, NULL, &diagnostics) == SN_INVALID);
    assert(sn_jobs_active(&jobs, 2, tokens[2]));
    assert(sn_jobs_take(&jobs, 2, tokens[2] + 1, &resource, 0, &route, &diagnostics) == SN_INVALID);
    assert(sn_jobs_take(&jobs, 2, tokens[2], &resource, 0, &route, &diagnostics) == SN_FOUND);
    assert(!sn_jobs_active(&jobs, 2, tokens[2]) && status_of(2) == SN_INVALID);
    assert(sn_route_current(&route, &resource, 0) && route.count >= 2);
    assert(route.points[0].x == 1.f && route.points[0].y == 7.f);
    assert(route.points[route.count - 1].x == 19.f && route.points[route.count - 1].y == 13.f);
    assert(diagnostics.steps > 1 && diagnostics.start != SN_NONE && diagnostics.goal != SN_NONE);
    assert(sn_jobs_take(&jobs, 2, tokens[2], &resource, 0, &route, &diagnostics) == SN_INVALID);
    step();
    assert(jobs.starts_this_tick == 1 && jobs.waiting_count == 1 && jobs.waiting[0] == 5);
    assert(jobs.clients[4].workspace == 2 && jobs.owners[2] == 4);
    /* Running work cannot be taken early. */
    assert(sn_jobs_take(&jobs, 4, tokens[4], &resource, 0, &route, &diagnostics) == SN_RUNNING);
    assert(sn_jobs_active(&jobs, 4, tokens[4]));
}

/* Found routes are string-pulled one per tick, oldest first; the request's
   flag and the step's callback choose how. */
static void test_straightening(void)
{
    struct sn_jobs_request r = request(5.f, 5.f, 13.f, 15.f);
    uint32_t client, polled;
    sn_jobs_reset(&jobs);
    for (client = 0; client < 3; client++) assert(submit(client, request(1.f, 1.f, 3.f, 1.5f)));
    step();
    /* All three found the route in their first slice; one is pulled. */
    assert(status_of(0) == SN_FOUND && status_of(1) == SN_RUNNING && status_of(2) == SN_RUNNING);
    assert(jobs.clients[1].straightening && jobs.clients[2].straightening);
    assert(jobs.clients[1].found_order < jobs.clients[2].found_order);
    step();
    assert(status_of(1) == SN_FOUND && status_of(2) == SN_RUNNING && jobs.straightens_this_tick == 1);
    step();
    assert(status_of(2) == SN_FOUND && jobs.straightens_this_tick == 1);
    for (client = 0; client < 3; client++) {
        assert(sn_jobs_take(&jobs, client, tokens[client], &resource, 0, &route, NULL) == SN_FOUND);
        assert(route.count == 2 && route.points[1].x == 3.f);
    }
    /* Unpulled: the center/portal points of sn_query_result. */
    assert(sn_query_begin(&query, &resource, r.start, r.goal, 0) == SN_RUNNING);
    while (query.status == SN_RUNNING) sn_query_step(&query, &resource, 0, 16, NULL, NULL);
    assert(sn_query_result(&query, &resource, 0, &plain) == SN_FOUND && plain.count > 3);
    r.straighten = false;
    assert(submit(0, r));
    for (polled = 0; status_of(0) == SN_RUNNING && polled < 100; polled++) step();
    assert(sn_jobs_take(&jobs, 0, tokens[0], &resource, 0, &route, NULL) == SN_FOUND);
    assert(route.count == plain.count && memcmp(route.points, plain.points, plain.count * sizeof(plain.points[0])) == 0);
    /* Pulled without a callback (known clearance): unchecked, and fewer points. */
    r.straighten = true;
    assert(submit(0, r));
    calls = 0;
    for (polled = 0; status_of(0) == SN_RUNNING && polled < 100; polled++)
        sn_jobs_step(&jobs, &resource, 0, ++tick, NULL, NULL);
    assert(calls == 0);
    assert(sn_jobs_take(&jobs, 0, tokens[0], &resource, 0, &route, NULL) == SN_FOUND);
    assert(route.count < plain.count && route.points[route.count - 1].y == 15.f);
}

static void test_cancel_stale_and_capacity(void)
{
    uint32_t client;
    sn_jobs_reset(&jobs);
    for (client = 0; client < 6; client++) assert(submit(client, request(1, 1, 19, 19)));
    step();
    /* A waiting request leaves the queue in order; a running one frees its
       workspace for the next. Wrong tokens change nothing. */
    assert(!sn_jobs_cancel(&jobs, 4, tokens[4] + 1) && jobs.waiting_count == 2);
    assert(!sn_jobs_cancel(&jobs, SN_JOBS_CLIENTS_MAX, 1));
    assert(sn_jobs_cancel(&jobs, 4, tokens[4]) && jobs.waiting_count == 1 && jobs.waiting[0] == 5);
    assert(!sn_jobs_cancel(&jobs, 4, tokens[4]) && status_of(4) == SN_INVALID);
    assert(sn_jobs_cancel(&jobs, 1, tokens[1]) && jobs.owners[1] == SN_NONE);
    step();
    assert(jobs.starts_this_tick == 1 && jobs.owners[1] == 5 && jobs.waiting_count == 0);
    /* A query out of slices reports capacity. */
    jobs.clients[0].steps = SN_JOBS_QUERY_STEPS_MAX - 1;
    step();
    assert(status_of(0) == SN_CAPACITY);
    assert(sn_jobs_take(&jobs, 0, tokens[0], &resource, 0, &route, &diagnostics) == SN_CAPACITY);
    assert(route.count == 0 && diagnostics.steps == SN_JOBS_QUERY_STEPS_MAX);
    /* A rebuilt resource (or another obstacle revision) makes everything
       stale, waiting or not; a stale result is never copied. */
    assert(submit(6, request(1, 1, 19, 19)) && submit(7, request(1, 1, 19, 19)));
    resource.revision++;
    step();
    for (client = 2; client < 8; client++)
        if (client != 4) assert(status_of(client) == SN_STALE);
    assert(jobs.waiting_count == 0);
    assert(sn_jobs_take(&jobs, 6, tokens[6], &resource, 0, &route, NULL) == SN_STALE && route.count == 0);
    resource.revision--;
    assert(sn_jobs_take(&jobs, 7, tokens[7], &resource, 0, &route, NULL) == SN_STALE);
    sn_jobs_reset(&jobs);
    assert(submit(0, request(1, 1, 19, 19)));
    sn_jobs_step(&jobs, &resource, 1, ++tick, clear, NULL);
    assert(status_of(0) == SN_STALE);
    assert(sn_jobs_take(&jobs, 0, tokens[0], &resource, 1, &route, NULL) == SN_STALE);
    assert(submit(0, request(1, 1, 19, 19)));
    sn_jobs_step(&jobs, NULL, 0, ++tick, clear, NULL);
    assert(status_of(0) == SN_STALE && jobs.waiting_count == 0 && jobs.starts_this_tick == 0);
    /* A found route the resource no longer matches at take is stale too. */
    sn_jobs_reset(&jobs);
    assert(submit(0, request(1, 1, 3, 1.5f)));
    step();
    assert(status_of(0) == SN_FOUND);
    resource.key.checksum++;
    assert(sn_jobs_take(&jobs, 0, tokens[0], &resource, 0, &route, NULL) == SN_STALE && route.count == 0);
    resource.key.checksum--;
}

/* Endpoints are placed on the floor when their query begins; failures keep
   the submitted point for the log, and the callback's refusals are counted. */
static void test_ground_and_diagnostics(void)
{
    struct sn_jobs_request r = request(1.f, 1.f, 9.f, 9.f);
    uint32_t polled;
    sn_jobs_reset(&jobs);
    r.start.z = 2.f; r.ground_depth = 3.f; r.ground_horizontal = 1.f;
    assert(submit(0, r));
    r.start.x = -0.5f; r.start.z = 0.f;
    assert(submit(1, r));
    r.start.x = -5.f;
    assert(submit(2, r));
    r.start.x = 1.f; r.start.z = 2.f; r.ground_depth = 0.f;
    assert(submit(3, r));
    step();
    assert(jobs.clients[0].diagnostics.start_point.z == 0.f && jobs.clients[0].diagnostics.start != SN_NONE);
    assert(jobs.clients[1].diagnostics.start_point.x >= 0.f && jobs.clients[1].diagnostics.start == 0);
    assert(status_of(2) == SN_BAD_START && jobs.clients[2].diagnostics.start == SN_NONE);
    assert(jobs.clients[2].diagnostics.start_point.x == -5.f);
    assert(status_of(3) == SN_BAD_START && jobs.clients[3].diagnostics.start_point.z == 2.f);
    for (polled = 0; (status_of(0) == SN_RUNNING || status_of(1) == SN_RUNNING) && polled < 100; polled++) step();
    assert(status_of(0) == SN_FOUND && status_of(1) == SN_FOUND);
    sn_jobs_reset(&jobs);
    refused_client = 1;
    assert(submit(1, request(1, 1, 9, 9)));
    for (polled = 0; status_of(1) == SN_RUNNING && polled < 100; polled++) step();
    assert(sn_jobs_take(&jobs, 1, tokens[1], &resource, 0, &route, &diagnostics) == SN_NO_PATH);
    assert(diagnostics.start_rejected && diagnostics.rejected_edges > 0 && route.count == 0);
    refused_client = SN_NONE;
}

int main(void)
{
    build_grid();
    test_submit_validation();
    test_queue_and_bounds();
    test_straightening();
    test_cancel_stale_and_capacity();
    test_ground_and_diagnostics();
    printf("surface navigation jobs passed; scheduler=%zu bytes (%u workspaces, %u clients)\n",
        sizeof(jobs), (unsigned)SN_JOBS_WORKSPACES_MAX, (unsigned)SN_JOBS_CLIENTS_MAX);
    return 0;
}
