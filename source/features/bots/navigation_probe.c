/* Opt-in walking acceptance probe. No teleports, physics writes or tactical AI. */
#include "cseries.h"
#include "players.h"
#include "game.h"
#include "navigation_probe.h"
#include "navigation_world.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/biped_definitions.h"
#include <math.h>

#ifdef HALO_FEATURE_BOTS

int config_boolean(char const *name);
char const *config_string(char const *name);
void platform_log(char const *format, ...);

static struct {
    struct sn_query query;
    struct sn_route route;
    struct sn_point goal, progress_origin;
    uint64_t revision;
    long unit, progress_tick;
    uint32_t candidate_cursor, waypoint, attempts, replans;
    enum sn_status status;
    boolean finished, explicit_goal;
} probe;

static struct sn_point position(long unit)
{
    real_point3d p;
    struct sn_point result;
    struct biped_definition *definition=biped_definition_get(unit_get(unit)->definition_index);
    object_get_origin(unit,&p);
    /* Resource/query points are ground-foot coordinates, not sphere centers. */
    if (TEST_FLAG(definition->biped.flags,_biped_pill_centered_at_origin_bit))
        p.z-=definition->biped.collision_radius;
    result.x=p.x; result.y=p.y; result.z=p.z;
    return result;
}
static float horizontal_distance(struct sn_point a, struct sn_point b)
{
    float x=a.x-b.x,y=a.y-b.y;
    return sqrtf(x*x+y*y);
}
static boolean select_goal(const struct sn_resource *r, struct sn_point start)
{
    char const *text=config_string("debug.nav_goal");
    uint32_t i;
    if (text && text[0]) {
        char extra;
        probe.explicit_goal=TRUE;
        return sscanf(text,"%f,%f,%f %c",&probe.goal.x,&probe.goal.y,&probe.goal.z,&extra)==3;
    }
    for (i=probe.candidate_cursor;i<r->polygon_count;i++) {
        struct sn_point p=r->polygons[i].center;
        float d=horizontal_distance(start,p);
        if (d>=4.f && d<=12.f && fabsf(p.z-start.z)<1.5f) {
            probe.candidate_cursor=i+1;
            probe.goal=p;
            return TRUE;
        }
    }
    return FALSE;
}
static void begin_query(const struct sn_resource *r, long unit, struct sn_point start)
{
    probe.status=sn_query_begin(&probe.query,r,start,probe.goal,
        navigation_world_obstacle_revision());
    platform_log("nav-probe: query start (%.3f %.3f %.3f) goal (%.3f %.3f %.3f): %s",
        start.x,start.y,start.z,probe.goal.x,probe.goal.y,probe.goal.z,sn_status_name(probe.status));
    (void)unit;
}
static void query_step(const struct sn_resource *r, long unit, struct sn_point start)
{
    if (probe.status!=SN_RUNNING) return;
    probe.status=sn_query_step(&probe.query,r,navigation_world_obstacle_revision(),
        2,navigation_world_segment_clear,&unit);
    if (probe.status==SN_FOUND) {
        probe.status=sn_query_result(&probe.query,r,navigation_world_obstacle_revision(),&probe.route);
        if (probe.status==SN_FOUND) {
            probe.waypoint=0; probe.progress_tick=game_time_get(); probe.progress_origin=start;
            platform_log("nav-probe: route found, %u waypoints",probe.route.count);
        }
    }
}
static void failed(void)
{
    platform_log("nav-probe: query/path failed (%s), attempt %u",sn_status_name(probe.status),probe.attempts);
    if (probe.explicit_goal || probe.attempts>=8) probe.finished=TRUE;
    else probe.status=SN_INVALID;
}
static void follow(const struct sn_resource *r, long unit, struct sn_point origin,
    struct player_action *action)
{
    struct sn_point target;
    float distance,yaw;
    if (!sn_route_current(&probe.route,r,navigation_world_obstacle_revision())) {
        begin_query(r,unit,origin);
        return;
    }
    while (probe.waypoint<probe.route.count &&
        horizontal_distance(origin,probe.route.points[probe.waypoint])<0.18f &&
        fabsf(origin.z-probe.route.points[probe.waypoint].z)<0.3f) probe.waypoint++;
    if (probe.waypoint==probe.route.count) {
        platform_log("nav-probe: reached goal (%.3f %.3f %.3f), error %.3f, normal player controls",
            origin.x,origin.y,origin.z,horizontal_distance(origin,probe.goal));
        probe.finished=TRUE;
        return;
    }
    target=probe.route.points[probe.waypoint];
    if (horizontal_distance(origin,probe.progress_origin)>0.15f) {
        probe.progress_tick=game_time_get(); probe.progress_origin=origin;
    }
    if (game_time_get()-probe.progress_tick>2*TICKS_PER_SECOND) {
        if (++probe.replans>3) { probe.status=SN_NO_PATH; failed(); return; }
        navigation_world_block(probe.route.polygons[probe.waypoint]);
        platform_log("nav-probe: stuck; temporary region block and bounded replan %u",probe.replans);
        begin_query(r,unit,origin);
        probe.progress_tick=game_time_get();
        return;
    }
    /* Wait briefly for moving occupants. Never move through a failed capsule
       sweep; persistent blockage takes the bounded replan branch above. */
    if (!navigation_world_segment_clear(&unit,origin,probe.route.polygons[probe.waypoint],
        target,probe.route.polygons[probe.waypoint])) return;
    distance=horizontal_distance(origin,target);
    yaw=arctangent(target.y-origin.y,target.x-origin.x); /* (the game's, not libm's atan2f) */
    action->desired_facing.yaw=yaw; action->desired_facing.pitch=0.f;
    action->throttle.i=MIN(0.7f,distance*2.f);
    action->throttle.j=0.f;
    if (game_time_get()%TICKS_PER_SECOND==0)
        platform_log("nav-probe: following waypoint %u/%u at (%.3f %.3f %.3f)",
            probe.waypoint,probe.route.count,origin.x,origin.y,origin.z);
}
boolean navigation_probe_action(long unit, struct player_action *action)
{
    const struct sn_resource *r;
    struct sn_point origin;
    if (!config_boolean("debug.nav_probe")) return FALSE;
    csmemset(action,0,sizeof(*action));
    action->desired_weapon_index=NONE; action->desired_grenade_index=NONE;
    action->desired_zoom_level=NONE;
    r=navigation_world_resource();
    if (!r) return TRUE; /* Idle while building rather than fall back to tactical AI. */
    origin=position(unit);
    if (probe.unit!=unit || probe.revision!=r->revision) {
        csmemset(&probe,0,sizeof(probe));
        probe.unit=unit; probe.revision=r->revision;
        platform_log("nav-probe: new life/resource at (%.3f %.3f %.3f)",origin.x,origin.y,origin.z);
    }
    if (probe.finished) return TRUE;
    if (probe.status==SN_INVALID) {
        if (!select_goal(r,origin)) {
            platform_log("nav-probe: invalid goal or no nearby candidate"); probe.finished=TRUE; return TRUE;
        }
        probe.attempts++; begin_query(r,unit,origin);
    }
    query_step(r,unit,origin);
    if (probe.status==SN_FOUND) follow(r,unit,origin,action);
    else if (probe.status!=SN_RUNNING) failed();
    return TRUE;
}

#endif /* HALO_FEATURE_BOTS */
