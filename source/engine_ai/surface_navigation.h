/* Geometry navigation: cooked convex collision surfaces, not the small region graph. */
#ifndef ENGINE_AI_SURFACE_NAVIGATION_H
#define ENGINE_AI_SURFACE_NAVIGATION_H
#include <stdbool.h>
#include <stdint.h>

#define SN_NONE UINT32_MAX
#define SN_FORMAT_VERSION 1u
#define SN_HEAP_CLOSED 0xffffu
#define SN_VIA_NONE 0xffffu
enum { SN_SOURCE_MAX = 131072, SN_POLYGONS_MAX = 16384, SN_VERTICES_MAX = 8,
       SN_ROUTE_POLYGONS_MAX = 512, SN_WAYPOINTS_MAX = 1025 };

struct sn_point { float x, y, z; };
struct sn_agent { float radius, height, minimum_normal_z, projection_height; };
struct sn_key {
    uint64_t map_identity, overlay_revision;
    uint32_t checksum, bsp_index, agent_identity, format_version;
};
/* SN_DISCONNECTED: both endpoints project, but no static adjacency joins their
   polygons. SN_NO_PATH within one component means query-time clearance refused it. */
enum sn_status { SN_INVALID = 0, SN_RUNNING, SN_READY, SN_FOUND, SN_NO_PATH,
    SN_BAD_START, SN_BAD_GOAL, SN_CAPACITY, SN_STALE, SN_DISCONNECTED };

/* Source indices are local to the current BSP. Width/height are validated clearance
   hints; both zero means unknown and REQUIRES a query-time collision callback.
   A source reader returns -1 malformed, 0 excluded, 1 present. */
struct sn_polygon {
    struct sn_point vertices[SN_VERTICES_MAX];
    uint32_t neighbors[SN_VERTICES_MAX];
    struct sn_point normal, center;
    float width, height;
    uint32_t source_index;
    uint8_t vertex_count;
};
typedef int (*sn_read_polygon)(void *context, uint32_t source_index, struct sn_polygon *out);
struct sn_resource {
    struct sn_key key;
    struct sn_agent agent;
    uint64_t revision;
    uint32_t polygon_count;
    bool ready, requires_clearance;
    struct sn_polygon polygons[SN_POLYGONS_MAX];
    /* Static topology, independent of collision callbacks: the lowest polygon index
       of each polygon's connected set, or SN_NONE when the agent cannot stand at
       its center (never a projection result, never expanded). */
    uint32_t components[SN_POLYGONS_MAX];
};
struct sn_build {
    uint32_t mapping[SN_SOURCE_MAX];
    uint32_t source_count, cursor, phase;
    enum sn_status status;
};

/* Build only into unpublished storage. Each step processes <=budget source
   polygons or adjacency rows (1..256). Caller publishes only SN_READY. */
enum sn_status sn_build_begin(struct sn_build *build, struct sn_resource *out,
    uint32_t source_count, struct sn_key key, uint64_t revision, struct sn_agent agent);
enum sn_status sn_build_step(struct sn_build *build, struct sn_resource *out,
    uint32_t budget, sn_read_polygon read, void *context);

/* Height-aware vertical projection inside an existing polygon, never snaps an
   arbitrary out-of-bounds point onto a distant island. No cross-floor links. */
enum sn_status sn_project(const struct sn_resource *resource, struct sn_point point,
    uint32_t *polygon, struct sn_point *projected);
/* sn_project, else the first floor found looking straight down at most depth
   (in bands of half the projection height): for an airborne agent, or a point
   in a seat or mid-jump. Never moves horizontally, so never crosses a wall. */
enum sn_status sn_project_below(const struct sn_resource *resource, struct sn_point point,
    float depth, uint32_t *polygon, struct sn_point *projected);
/* sn_project_below, else the closest candidate at most `horizontal` away
   along rays toward usable polygon centers, on a polygon whose floor
   there is between depth below and projection height above the point: for a
   target against a wall or on a seam too narrow to stand in. */
enum sn_status sn_project_near(const struct sn_resource *resource, struct sn_point point,
    float depth, float horizontal, uint32_t *polygon, struct sn_point *projected);
/* Static component of a polygon (see sn_resource.components), SN_NONE if unusable.
   Two projected endpoints can only be joined if their components are equal. */
uint32_t sn_component(const struct sn_resource *resource, uint32_t polygon);

/* Query-time geometry/obstacle check (mandatory when clearance is unknown). Own its revision separately from
   immutable topology; changing it invalidates queries/routes. It must be bounded.
   Segments are center->portal and portal->center, with valid polygon indices; in
   the start polygon from the start point, in the goal polygon to the goal point. */
typedef bool (*sn_segment_clear)(void *context, struct sn_point a, uint32_t a_polygon,
    struct sn_point b, uint32_t b_polygon);
struct sn_query {
    struct sn_key key;
    /* A* open set: a binary heap of polygons by estimated total length, with
       each polygon's heap slot (SN_HEAP_CLOSED once expanded) and best length. */
    uint32_t heap[SN_POLYGONS_MAX], previous[SN_POLYGONS_MAX];
    uint16_t heap_slot[SN_POLYGONS_MAX];
    float cost[SN_POLYGONS_MAX];
    uint8_t previous_edge[SN_POLYGONS_MAX];
    /* A polygon reached across a narrow one: that polygon (SN_VIA_NONE if
       none) and the edge it was left by. */
    uint16_t via[SN_POLYGONS_MAX];
    uint8_t via_edge[SN_POLYGONS_MAX];
    uint32_t heap_count, start, goal;
    uint64_t revision, obstacle_revision;
    struct sn_point start_point, goal_point;
    enum sn_status status;
    /* Diagnostics: transitions refused by the clear callback; start_rejected when
       every transition out of the start polygon was refused, goal_rejected when
       any transition into the goal polygon was. */
    uint32_t rejected_edges;
    bool start_rejected, goal_rejected;
    /* Set by sn_query_begin. A caller whose goal is a unit's position may clear it
       after begin: the target's own body occupies the last segment. */
    bool check_goal_segment;
};
struct sn_route {
    struct sn_key key;
    uint64_t revision, obstacle_revision;
    uint32_t count;
    struct sn_point points[SN_WAYPOINTS_MAX];
    uint32_t polygons[SN_WAYPOINTS_MAX];
    /* The polygons crossed, start to goal, and each point's index among them. */
    uint32_t corridor_count;
    uint32_t corridor[SN_ROUTE_POLYGONS_MAX];
    uint16_t corridor_index[SN_WAYPOINTS_MAX];
};
/* Deterministic A* over polygons, O((P+E) log P), no allocation or recursion.
   Minimizes the length of the start->portal->center...->portal->goal polyline
   (ties broken by polygon index); the heuristic is the straight-line distance
   to the goal. begin/projection is O(P*8). step expands <=budget polygons
   (1..256), each with <=8 edges. */
enum sn_status sn_query_begin(struct sn_query *query, const struct sn_resource *resource,
    struct sn_point start, struct sn_point goal, uint64_t obstacle_revision);
enum sn_status sn_query_step(struct sn_query *query, const struct sn_resource *resource,
    uint64_t obstacle_revision, uint32_t budget, sn_segment_clear clear, void *context);
enum sn_status sn_query_result(const struct sn_query *query, const struct sn_resource *resource,
    uint64_t obstacle_revision, struct sn_route *route);
/* Replace a found route's points by the shortest path through its corridor
   (string pulling): only the corners where it must turn, each held the
   agent's radius in from the portal's ends. The corridor is unchanged; each
   point's polygon is the corridor polygon it enters. O(corridor^2) worst case. */
void sn_route_straighten(struct sn_route *route, const struct sn_resource *resource);
/* port: Collision-checked string pulling, for a route whose corridor was
   searched with `clear`: the funnel knows only the polygons, so a pulled
   segment can cut through scenery standing on them that the searched
   center/portal path went around. `route` must be an unmodified
   sn_query_result whose consecutive points `clear` has already passed.
   Each new segment is checked one corridor polygon at a time on that
   polygon's floor (a piece across a seam must also keep the agent's radius
   from its open edges). A refused stretch is pulled again through the
   original polygon center nearest the refusal, and a stretch with no center
   left keeps its original points, so every segment of the result is either
   checked or the search's own. At most SN_STRAIGHTEN_CHECKS_MAX calls to
   `clear`: once they are spent the rest of the route stays original. A route
   within one polygon is returned as it is, uncalled. False, with the route
   untouched, only for no callback or an invalid or stale route. Uses the
   same static scratch as sn_route_straighten; neither is reentrant. */
enum { SN_STRAIGHTEN_CHECKS_MAX = 256 };
bool sn_route_straighten_checked(struct sn_route *route, const struct sn_resource *resource,
    sn_segment_clear clear, void *context);
bool sn_route_current(const struct sn_route *route, const struct sn_resource *resource,
    uint64_t obstacle_revision);
/* A short jump at current horizontal momentum. Units are per simulation tick;
   gravity is subtracted before moving, as in CE biped physics. Success requires
   reaching the landing tolerance on the descending half, within 60 ticks. This
   is only a candidate trajectory: the caller must sweep the actual capsule and
   confirm landing support before emitting a jump control. */
enum { SN_JUMP_TICKS_MAX = 60 };
struct sn_jump_arc { uint32_t ticks; struct sn_point points[SN_JUMP_TICKS_MAX+1]; };
bool sn_jump_candidate(struct sn_point start, struct sn_point goal,
    float velocity_x, float velocity_y, float impulse, float gravity,
    float landing_tolerance, struct sn_jump_arc *arc);
const char *sn_status_name(enum sn_status status);
#endif
