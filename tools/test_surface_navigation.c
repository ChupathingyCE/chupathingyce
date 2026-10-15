#include "../source/engine_ai/surface_navigation.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static struct sn_resource first,second;
static struct sn_build build;
static struct sn_query query;
static struct sn_route route;
static struct sn_route saved_route;
static struct sn_polygon source[8];
static struct sn_agent agent={0.2f,1.f,0.7f,0.6f};
static struct sn_key key={123,0,77,0,12,SN_FORMAT_VERSION};
static int read_polygon(void *context,uint32_t index,struct sn_polygon *out)
{
    (void)context;
    *out=source[index];
    return 1;
}
static void rectangle(uint32_t index,float x,float y,float z,float width,float length)
{
    uint32_t e;
    struct sn_polygon *p=&source[index];
    memset(p,0,sizeof(*p));
    p->vertices[0]=(struct sn_point){x,y,z};
    p->vertices[1]=(struct sn_point){x+width,y,z};
    p->vertices[2]=(struct sn_point){x+width,y+length,z};
    p->vertices[3]=(struct sn_point){x,y+length,z};
    p->vertex_count=4; p->normal.z=1.f; p->width=4.f; p->height=4.f;
    for(e=0;e<8;e++)p->neighbors[e]=SN_NONE;
}
static enum sn_status build_resource(struct sn_resource *r,uint32_t count,uint64_t revision)
{
    enum sn_status status=sn_build_begin(&build,r,count,key,revision,agent);
    uint32_t calls=0;
    while(status==SN_RUNNING && calls++<2000)
        status=sn_build_step(&build,r,64,read_polygon,NULL);
    assert(calls<2000);
    return status;
}
static enum sn_status find(struct sn_point start,struct sn_point goal,sn_segment_clear clear,void *context)
{
    uint32_t calls=0;
    enum sn_status status=sn_query_begin(&query,&first,start,goal,1);
    while(status==SN_RUNNING && calls++<SN_POLYGONS_MAX)
        status=sn_query_step(&query,&first,1,1,clear,context);
    assert(calls<SN_POLYGONS_MAX);
    if(status==SN_FOUND)status=sn_query_result(&query,&first,1,&route);
    return status;
}
static void test_floor_and_ramp(void)
{
    struct sn_point start={1.f,1.f,0.1f},goal={3.f,1.f,0.6f};
    rectangle(0,0,0,0,2,2); rectangle(1,2,0,0,2,2);
    source[0].neighbors[1]=1; source[1].neighbors[3]=0;
    source[1].vertices[1].z=source[1].vertices[2].z=1.f;
    source[1].normal.x=-1.f/sqrtf(5.f); source[1].normal.z=2.f/sqrtf(5.f);
    assert(build_resource(&first,2,1)==SN_READY);
    assert(find(start,goal,NULL,NULL)==SN_FOUND);
    /* start, portal, goal: no detour through either polygon's center */
    assert(route.count==3);
    assert(fabsf(route.points[route.count-1].z-0.5f)<0.001f);
    assert(build_resource(&second,2,1)==SN_READY);
    assert(first.polygon_count==second.polygon_count);
    assert(memcmp(first.polygons,second.polygons,2*sizeof(first.polygons[0]))==0);
    assert(find((struct sn_point){99,99,0},goal,NULL,NULL)==SN_BAD_START);
    assert(find(start,(struct sn_point){99,99,0},NULL,NULL)==SN_BAD_GOAL);
}
static void test_stacked_and_disconnected(void)
{
    uint32_t polygon;
    struct sn_point projected;
    rectangle(0,0,0,0,2,2); rectangle(1,0,0,3,2,2);
    assert(build_resource(&first,2,2)==SN_READY);
    assert(sn_project(&first,(struct sn_point){1,1,3.1f},&polygon,&projected)==SN_FOUND);
    assert(polygon==1 && projected.z==3.f);
    /* looking down: 2.5 above the upper floor finds it, never the lower one */
    assert(sn_project(&first,(struct sn_point){1,1,5.5f},&polygon,&projected)==SN_NO_PATH);
    assert(sn_project_below(&first,(struct sn_point){1,1,5.5f},3.f,&polygon,&projected)==SN_FOUND);
    assert(polygon==1 && projected.z==3.f);
    assert(sn_project_below(&first,(struct sn_point){1,1,9.f},3.f,&polygon,&projected)==SN_NO_PATH);
    assert(sn_project_below(&first,(struct sn_point){9,9,1.f},3.f,&polygon,&projected)==SN_NO_PATH);
    /* Static topology rejects these before any search or collision callback. */
    assert(find((struct sn_point){1,1,0.1f},(struct sn_point){1,1,3.1f},NULL,NULL)==SN_DISCONNECTED);
    assert(sn_component(&first,0)==0 && sn_component(&first,1)==1);
    rectangle(1,4,0,0,2,2);
    assert(build_resource(&first,2,3)==SN_READY);
    assert(find((struct sn_point){1,1,0.1f},(struct sn_point){5,1,0.1f},NULL,NULL)==SN_DISCONNECTED);
}
static void test_clearance(void)
{
    rectangle(0,0,0,0,2,0.3f);
    /* Even optimistic cooked width cannot override physical boundary clearance. */
    assert(build_resource(&first,1,4)==SN_READY);
    assert(find((struct sn_point){1,0.15f,0},(struct sn_point){1.5f,0.15f,0},NULL,NULL)==SN_BAD_START);
    rectangle(0,0,0,0,2,2); source[0].height=0.5f;
    assert(build_resource(&first,1,4)==SN_READY && first.polygon_count==0);
}
struct blockage { bool blocked; };
static bool clear_segment(void *context,struct sn_point a,uint32_t ap,struct sn_point b,uint32_t bp)
{
    struct blockage *state=context;
    (void)a;(void)b;
    return !state->blocked || (ap!=1 && bp!=1);
}
static void test_obstacles_and_revisions(void)
{
    struct blockage obstacle={true};
    struct sn_point start={1,1,0},goal={3,1,0};
    rectangle(0,0,0,0,2,2); rectangle(1,2,0,0,2,2);
    source[0].neighbors[1]=1; source[1].neighbors[3]=0;
    assert(build_resource(&first,2,5)==SN_READY);
    assert(sn_component(&first,0)==sn_component(&first,1));
    /* Clearance refusal inside one component is NO_PATH, not DISCONNECTED. */
    assert(find(start,goal,clear_segment,&obstacle)==SN_NO_PATH);
    assert(query.start_rejected && query.goal_rejected && query.rejected_edges==1);
    /* An occupied goal (a unit's position) skips the segment inside the goal
       polygon; reaching its portal is still checked. */
    assert(sn_query_begin(&query,&first,start,goal,1)==SN_RUNNING);
    query.check_goal_segment=false;
    while(query.status==SN_RUNNING) sn_query_step(&query,&first,1,1,clear_segment,&obstacle);
    assert(query.status==SN_FOUND && query.rejected_edges==0);
    obstacle.blocked=false; /* Deleted/moved obstacle: collision adapter no longer blocks it. */
    assert(find(start,goal,clear_segment,&obstacle)==SN_FOUND);
    assert(sn_route_current(&route,&first,1));
    assert(!sn_route_current(&route,&first,2));
    assert(sn_query_step(&query,&first,2,1,NULL,NULL)==SN_STALE);
    first.revision++;
    assert(!sn_route_current(&route,&first,1));
    first.revision--;
    first.key.bsp_index=1; /* Campaign BSP change, even if caller reused a revision. */
    assert(!sn_route_current(&route,&first,1));
    first.key=key; first.key.overlay_revision=1;
    assert(!sn_route_current(&route,&first,1));
    first.key=key; first.key.agent_identity++;
    assert(!sn_route_current(&route,&first,1));
    first.key=key; first.key.checksum++;
    assert(!sn_route_current(&route,&first,1));
    first.key=key; first.ready=false;
    assert(!sn_route_current(&route,&first,1));
    first.ready=true;
}
static void test_unknown_clearance(void)
{
    struct blockage obstacle={false};
    rectangle(0,0,0,0,2,2);
    source[0].width=source[0].height=0.f;
    assert(build_resource(&first,1,6)==SN_READY && first.requires_clearance);
    assert(find((struct sn_point){1,1,0},(struct sn_point){1.5f,1,0},NULL,NULL)==SN_INVALID);
    assert(find((struct sn_point){1,1,0},(struct sn_point){1.5f,1,0},clear_segment,&obstacle)==SN_FOUND);
    assert(sn_query_begin(&query,&first,(struct sn_point){1,1,0},(struct sn_point){1.5f,1,0},1)==SN_RUNNING);
    assert(sn_query_step(&query,&first,1,0,clear_segment,&obstacle)==SN_INVALID);
}
/* Components follow adjacency, not storage order or build budget; a polygon the
   agent cannot stand on splits the chain and is never a component member. */
static void test_components(void)
{
    struct blockage obstacle={false};
    enum sn_status status;
    uint32_t calls=0;
    rectangle(0,2,0,0,2,2); rectangle(1,0,0,0,2,2); rectangle(2,20,0,0,2,0.3f); rectangle(3,8,0,0,2,2);
    source[0].neighbors[3]=1; source[1].neighbors[1]=0;
    assert(build_resource(&first,4,8)==SN_READY);
    assert(sn_component(&first,0)==0 && sn_component(&first,1)==0);
    assert(sn_component(&first,2)==SN_NONE && sn_component(&first,3)==3);
    assert(sn_component(&first,99)==SN_NONE);
    status=sn_build_begin(&build,&second,4,key,8,agent);
    while(status==SN_RUNNING && calls++<100) status=sn_build_step(&build,&second,1,read_polygon,NULL);
    assert(status==SN_READY && memcmp(first.components,second.components,4*sizeof(first.components[0]))==0);
    assert(find((struct sn_point){1,1,0},(struct sn_point){9,1,0},clear_segment,&obstacle)==SN_DISCONNECTED);
    assert(find((struct sn_point){1,1,0},(struct sn_point){3,1,0},clear_segment,&obstacle)==SN_FOUND);
    assert(query.rejected_edges==0 && !query.start_rejected && !query.goal_rejected);
}
/* Refuses any segment touching the start polygon's center (1,1). */
static bool refuse_center(void *context,struct sn_point a,uint32_t ap,struct sn_point b,uint32_t bp)
{
    (void)context;(void)ap;(void)bp;
    return !(a.x==1.f && a.y==1.f) && !(b.x==1.f && b.y==1.f);
}
static void test_endpoints_skip_centers(void)
{
    rectangle(0,0,0,0,2,2); rectangle(1,2,0,0,2,2);
    source[0].neighbors[1]=1; source[1].neighbors[3]=0;
    assert(build_resource(&first,2,10)==SN_READY);
    assert(find((struct sn_point){0.5f,1.5f,0},(struct sn_point){3.5f,0.5f,0},refuse_center,NULL)==SN_FOUND);
    assert(route.count==3 && route.points[1].x==2.f && route.points[1].y==1.f);
    assert(find((struct sn_point){0.5f,1.5f,0},(struct sn_point){1.5f,0.5f,0},refuse_center,NULL)==SN_FOUND);
    assert(route.count==2);
}
/* Refuses only the portal-to-center half of a transition (portal at x=2). */
static bool refuse_portal(void *context,struct sn_point a,uint32_t ap,struct sn_point b,uint32_t bp)
{
    (void)context;(void)b;(void)ap;(void)bp;
    return a.x!=2.f;
}
static void test_rejected_edges(void)
{
    rectangle(0,0,0,0,2,2); rectangle(1,2,0,0,2,2);
    source[0].neighbors[1]=1; source[1].neighbors[3]=0;
    assert(build_resource(&first,2,9)==SN_READY);
    assert(find((struct sn_point){1,1,0},(struct sn_point){3,1,0},refuse_portal,NULL)==SN_NO_PATH);
    assert(query.rejected_edges==1 && query.start_rejected && query.goal_rejected);
}
static bool seam_blocked(void *context,struct sn_point a,uint32_t ap,struct sn_point b,uint32_t bp)
{
    (void)context;(void)a;(void)b;
    return ap!=1 && bp!=1;
}
/* A* follows length, not polygon count: the straight row A,C,D,G beats the
   two-polygon detour A,B,G through a long polygon B above them. */
static void test_shortest_route(void)
{
    struct sn_polygon *b=&source[4];
    rectangle(0,0,0,0,2,2); rectangle(1,2,0,0,2,2); rectangle(2,4,0,0,2,2); rectangle(3,6,0,0,2,2);
    rectangle(4,0,2,0,8,38);
    b->vertices[0]=(struct sn_point){0,2,0}; b->vertices[1]=(struct sn_point){2,2,0};
    b->vertices[2]=(struct sn_point){6,2,0}; b->vertices[3]=(struct sn_point){8,2,0};
    b->vertices[4]=(struct sn_point){8,40,0}; b->vertices[5]=(struct sn_point){0,40,0};
    b->vertex_count=6;
    source[0].neighbors[1]=1; source[0].neighbors[2]=4;
    source[1].neighbors[3]=0; source[1].neighbors[1]=2;
    source[2].neighbors[3]=1; source[2].neighbors[1]=3;
    source[3].neighbors[3]=2; source[3].neighbors[2]=4;
    b->neighbors[0]=0; b->neighbors[2]=3;
    assert(build_resource(&first,5,11)==SN_READY);
    assert(find((struct sn_point){1,1,0},(struct sn_point){7,1,0},NULL,NULL)==SN_FOUND);
    assert(route.count==7 && route.points[1].x==2.f && route.points[1].y==1.f);
    assert(route.polygons[2]==1 && route.polygons[4]==2);
}
/* String pulling: a straight corridor is one segment; an L turns once at the
   inner corner, held the agent radius (0.2) in from the wall's end. */
static void test_straighten(void)
{
    rectangle(0,0,0,0,2,2); rectangle(1,2,0,0,2,2); rectangle(2,2,2,0,2,2);
    source[0].neighbors[1]=1; source[1].neighbors[3]=0;
    source[1].neighbors[2]=2; source[2].neighbors[0]=1;
    assert(build_resource(&first,3,12)==SN_READY);
    assert(find((struct sn_point){1,1,0},(struct sn_point){3,3,0},NULL,NULL)==SN_FOUND);
    assert(route.corridor_count==3 && route.corridor[0]==0 && route.corridor[2]==2);
    sn_route_straighten(&route,&first);
    assert(route.count==4 && route.corridor_count==3);
    assert(fabsf(route.points[1].x-2.f)<0.001f && fabsf(route.points[1].y-1.8f)<0.001f);
    assert(fabsf(route.points[2].x-2.2f)<0.001f && fabsf(route.points[2].y-2.f)<0.001f);
    assert(route.polygons[1]==1 && route.polygons[2]==2 && route.corridor_index[3]==2);
    assert(route.points[3].x==3.f && route.points[3].y==3.f);
    test_shortest_route();
    sn_route_straighten(&route,&first);
    assert(route.count==2 && route.points[1].x==7.f);
}
/* Circular scenery footprints standing on flat floors: a segment is clear
   when it passes outside each, horizontally. */
struct scenery { struct sn_point position[8]; float radius[8]; uint32_t count,calls; bool refuse; };
static float gap(struct sn_point p,struct sn_point a,struct sn_point b)
{
    float dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy,t=0.f;
    if(length>0.f)t=((p.x-a.x)*dx+(p.y-a.y)*dy)/length;
    if(t<0.f)t=0.f;else if(t>1.f)t=1.f;
    dx=a.x+t*dx-p.x;dy=a.y+t*dy-p.y;
    return sqrtf(dx*dx+dy*dy);
}
static bool scenery_clear(void *context,struct sn_point a,uint32_t ap,struct sn_point b,uint32_t bp)
{
    struct scenery *s=context;
    uint32_t i;
    (void)ap;(void)bp;
    s->calls++;
    if(s->refuse)return false;
    for(i=0;i<s->count;i++)if(gap(s->position[i],a,b)<=s->radius[i])return false;
    return true;
}
static bool route_clear_of(const struct sn_route *r,const struct scenery *s)
{
    uint32_t i,j;
    for(i=1;i<r->count;i++)for(j=0;j<s->count;j++)
        if(gap(s->position[j],r->points[i-1],r->points[i])<=s->radius[j])return false;
    return true;
}
/* A straightened route keeps its endpoints and corridor, and indexes it in order. */
static void assert_route_shape(const struct sn_route *r,const struct sn_route *original)
{
    uint32_t i;
    assert(r->count>=2 && r->count<=original->count);
    assert(memcmp(&r->points[0],&original->points[0],sizeof(r->points[0]))==0);
    assert(memcmp(&r->points[r->count-1],&original->points[original->count-1],sizeof(r->points[0]))==0);
    assert(r->corridor_count==original->corridor_count);
    assert(memcmp(r->corridor,original->corridor,original->corridor_count*sizeof(r->corridor[0]))==0);
    for(i=0;i<r->count;i++){
        assert(r->corridor_index[i]<r->corridor_count && r->polygons[i]==r->corridor[r->corridor_index[i]]);
        assert(i==0 || r->corridor_index[i]>=r->corridor_index[i-1]);
    }
    assert(r->corridor_index[0]==0 && r->corridor_index[r->count-1]==r->corridor_count-1);
    assert(sn_route_current(r,&first,1));
}
static bool floor_segment_clear(void *context,struct sn_point a,uint32_t ap,
    struct sn_point b,uint32_t bp)
{
    uint32_t *calls=context;
    const struct sn_polygon *p=&first.polygons[ap];
    float az=p->vertices[0].z-(p->normal.x*(a.x-p->vertices[0].x)+
        p->normal.y*(a.y-p->vertices[0].y))/p->normal.z;
    float bz=p->vertices[0].z-(p->normal.x*(b.x-p->vertices[0].x)+
        p->normal.y*(b.y-p->vertices[0].y))/p->normal.z;
    (*calls)++;
    assert(ap==bp && fabsf(az-a.z)<0.0001f && fabsf(bz-b.z)<0.0001f);
    return true;
}
static void test_checked_straighten(void)
{
    struct scenery scenery;
    struct sn_point start={0.5f,0.5f,0.f},goal={3.5f,0.5f,0.f};
    uint32_t calls=0;
    memset(&scenery,0,sizeof(scenery));
    scenery.count=1;scenery.position[0]=(struct sn_point){2.5f,0.5f,0.f};scenery.radius[0]=0.15f;
    rectangle(0,0,0,0,2,2);rectangle(1,2,0,0,2,2);
    source[0].neighbors[1]=1;source[1].neighbors[3]=0;
    assert(build_resource(&first,2,15)==SN_READY);
    assert(find(start,goal,scenery_clear,&scenery)==SN_FOUND);
    assert(route.count==3);
    saved_route=route;
    /* The searched portal detour clears the rock; the pulled shortcut does
       not, and with no polygon center to pin, the original points stay. */
    scenery.calls=0;
    assert(sn_route_straighten_checked(&route,&first,scenery_clear,&scenery));
    assert(scenery.calls==2 && memcmp(&route,&saved_route,sizeof(route))==0);
    assert(!sn_route_straighten_checked(&route,&first,NULL,NULL));
    assert(memcmp(&route,&saved_route,sizeof(route))==0);
    scenery.position[0].y=1.5f;
    assert(sn_route_straighten_checked(&route,&first,scenery_clear,&scenery));
    assert(route.count==2 && route.corridor_count==2);

    /* A route within one polygon is the search's own checked segment. */
    goal.x=1.5f;
    assert(find(start,goal,scenery_clear,&scenery)==SN_FOUND);
    saved_route=route;scenery.position[0]=goal;scenery.calls=0;
    assert(sn_route_straighten_checked(&route,&first,scenery_clear,&scenery));
    assert(scenery.calls==0 && memcmp(&route,&saved_route,sizeof(route))==0);

    /* Ground-aware validation splits a shortcut at the flat/ramp boundary. */
    test_floor_and_ramp();
    assert(find((struct sn_point){1,1,0},(struct sn_point){3,1,0.5f},NULL,NULL)==SN_FOUND);
    assert(sn_route_straighten_checked(&route,&first,floor_segment_clear,&calls));
    assert(route.count==2 && calls==2);
    /* Invalid/stale public routes never index beyond the corridor resource. */
    assert(find((struct sn_point){1,1,0},(struct sn_point){3,1,0.5f},NULL,NULL)==SN_FOUND);
    saved_route=route;route.corridor[1]=first.polygon_count;
    assert(!sn_route_straighten_checked(&route,&first,floor_segment_clear,&calls));
    route=saved_route;route.corridor_index[1]=7;
    assert(!sn_route_straighten_checked(&route,&first,floor_segment_clear,&calls));
    route=saved_route;route.points[1].x=NAN;
    assert(!sn_route_straighten_checked(&route,&first,floor_segment_clear,&calls));
    route=saved_route;route.revision++;
    assert(!sn_route_straighten_checked(&route,&first,floor_segment_clear,&calls));
    route=saved_route;route.count=SN_WAYPOINTS_MAX+1;
    assert(!sn_route_straighten_checked(&route,&first,floor_segment_clear,&calls));
    assert(calls==2);
}
/* A rock on the straight line through a row of four polygons, clear of the
   searched path through their centers: the refused span is pulled again
   through the center nearest the rock, and both halves come out straight. */
static void test_checked_refinement(void)
{
    struct scenery scenery;
    uint32_t i;
    memset(&scenery,0,sizeof(scenery));
    scenery.count=1;scenery.position[0]=(struct sn_point){10.f,3.5f,0.f};scenery.radius[0]=0.3f;
    for(i=0;i<4;i++){
        rectangle(i,4.f*(float)i,0,0,4,4);
        source[i].neighbors[1]=i<3 ? i+1 : SN_NONE;
        source[i].neighbors[3]=i>0 ? i-1 : SN_NONE;
    }
    assert(build_resource(&first,4,17)==SN_READY);
    assert(find((struct sn_point){1,3.5f,0},(struct sn_point){15,3.5f,0},scenery_clear,&scenery)==SN_FOUND);
    assert(route.count==7 && route_clear_of(&route,&scenery));
    saved_route=route;scenery.calls=0;
    assert(sn_route_straighten_checked(&route,&first,scenery_clear,&scenery));
    assert_route_shape(&route,&saved_route);
    assert(route.count==3 && route.points[1].x==10.f && route.points[1].y==2.f);
    assert(route.polygons[1]==2 && route_clear_of(&route,&scenery));
    /* 3 pieces refused at the rock, then 3 and 2 for the halves */
    assert(scenery.calls==8);
    /* Without the rock the whole row is one segment. */
    route=saved_route;scenery.count=0;scenery.calls=0;
    assert(sn_route_straighten_checked(&route,&first,scenery_clear,&scenery));
    assert(route.count==2 && scenery.calls==4);
    /* A callback that refuses every new segment leaves the searched route,
       within the check budget. */
    route=saved_route;scenery.refuse=true;scenery.calls=0;
    assert(sn_route_straighten_checked(&route,&first,scenery_clear,&scenery));
    assert(memcmp(&route,&saved_route,sizeof(route))==0);
    assert(scenery.calls>0 && scenery.calls<=SN_STRAIGHTEN_CHECKS_MAX);
}
/* Random rocks on a 6x6 grid of 2 m floors: every straightened route keeps
   its shape, has no more points than the search's, never crosses a rock and
   never spends more than the check budget. */
enum { GRID=6 };
static uint32_t random_state=12345u;
static float random_unit(void)
{
    random_state=random_state*1664525u+1013904223u;
    return (float)(random_state>>8)/16777216.f;
}
static int grid_polygon(void *context,uint32_t index,struct sn_polygon *out)
{
    uint32_t x=index%GRID,y=index/GRID;
    (void)context;
    rectangle(0,2.f*(float)x,2.f*(float)y,0,2,2);
    *out=source[0];
    out->neighbors[0]=y>0 ? index-GRID : SN_NONE;
    out->neighbors[1]=x+1<GRID ? index+1 : SN_NONE;
    out->neighbors[2]=y+1<GRID ? index+GRID : SN_NONE;
    out->neighbors[3]=x>0 ? index-1 : SN_NONE;
    return 1;
}
static void test_checked_straighten_property(void)
{
    struct scenery scenery;
    uint32_t trial,calls=0,found=0,shortened=0,pinned=0;
    enum sn_status status=sn_build_begin(&build,&first,GRID*GRID,key,16,agent);
    while(status==SN_RUNNING && calls++<100) status=sn_build_step(&build,&first,256,grid_polygon,NULL);
    assert(status==SN_READY && first.polygon_count==GRID*GRID);
    for(trial=0;trial<600;trial++){
        struct sn_point start,goal;
        uint32_t i;
        memset(&scenery,0,sizeof(scenery));
        scenery.count=1+(uint32_t)(random_unit()*6.f);
        for(i=0;i<scenery.count;i++){
            scenery.position[i].x=12.f*random_unit();scenery.position[i].y=12.f*random_unit();
            scenery.radius[i]=0.15f+0.35f*random_unit();
        }
        start.x=0.3f+11.4f*random_unit();start.y=0.3f+11.4f*random_unit();start.z=0.f;
        goal.x=0.3f+11.4f*random_unit();goal.y=0.3f+11.4f*random_unit();goal.z=0.f;
        if(find(start,goal,scenery_clear,&scenery)!=SN_FOUND)continue;
        found++;saved_route=route;scenery.calls=0;
        assert(route_clear_of(&route,&scenery));
        assert(sn_route_straighten_checked(&route,&first,scenery_clear,&scenery));
        assert(scenery.calls<=SN_STRAIGHTEN_CHECKS_MAX);
        assert_route_shape(&route,&saved_route);
        assert(route_clear_of(&route,&scenery));
        if(route.count<saved_route.count)shortened++;
        for(i=1;i+1<route.count;i++){
            struct sn_point c=first.polygons[route.polygons[i]].center;
            if(route.points[i].x==c.x && route.points[i].y==c.y){pinned++;break;}
        }
    }
    printf("checked straightening: %u routes, %u shortened, %u pinned through a center\n",
        found,shortened,pinned);
    assert(found>=300 && shortened*10>=found*8 && pinned>=10);
}
/* A 0.13 m seam between two floors is never a node, yet joins them: the route
   crosses it, the corridor keeps it, and the straightened route is one segment. */
static void test_seam(void)
{
    struct sn_point projected;
    struct sn_polygon *seam=&source[1];
    uint32_t polygon;
    /* floors offset by 0.5 m, so each long side of the seam is partly outside */
    rectangle(0,0,0,0,2,2); rectangle(1,2,0,0,0.13f,2.5f); rectangle(2,2.13f,0.5f,0,2,2);
    seam->vertices[0]=(struct sn_point){2,0,0}; seam->vertices[1]=(struct sn_point){2.13f,0,0};
    seam->vertices[2]=(struct sn_point){2.13f,0.5f,0}; seam->vertices[3]=(struct sn_point){2.13f,2.5f,0};
    seam->vertices[4]=(struct sn_point){2,2.5f,0}; seam->vertices[5]=(struct sn_point){2,2,0};
    seam->vertex_count=6;
    source[0].neighbors[1]=1; seam->neighbors[5]=0;
    seam->neighbors[2]=2; source[2].neighbors[3]=1;
    assert(build_resource(&first,3,13)==SN_READY);
    assert(sn_component(&first,1)==SN_NONE && sn_component(&first,0)==sn_component(&first,2));
    assert(sn_project(&first,(struct sn_point){2.06f,1,0},&polygon,&projected)==SN_NO_PATH);
    /* a point on the seam goes to the nearest floor the agent fits on */
    assert(sn_project_near(&first,(struct sn_point){2.06f,1,0},1.f,1.f,&polygon,&projected)==SN_FOUND);
    /* This edge is shared, not a wall: the valid floor begins at x=2. */
    assert(polygon==0 && fabsf(projected.x-2.f)<0.01f && fabsf(projected.y-1.f)<0.05f);
    assert(sn_project_near(&first,(struct sn_point){2.06f,1,2.f},1.f,1.f,&polygon,&projected)==SN_NO_PATH);
    assert(sn_project_near(&first,(struct sn_point){9,9,0},1.f,1.f,&polygon,&projected)==SN_NO_PATH);
    assert(find((struct sn_point){1,1,0},(struct sn_point){3.13f,1.5f,0},clear_segment,&(struct blockage){false})==SN_FOUND);
    assert(route.corridor_count==3 && route.corridor[1]==1);
    assert(route.count==4 && route.points[1].x==2.f && fabsf(route.points[2].x-2.13f)<0.0001f);
    saved_route=route;
    sn_route_straighten(&route,&first);
    assert(route.count==2);
    {
        uint32_t calls=0;
        struct scenery scenery;
        /* Checked, the straight line is three pieces: floor, seam, floor. */
        route=saved_route;
        assert(sn_route_straighten_checked(&route,&first,floor_segment_clear,&calls));
        assert(route.count==2 && calls==3);
        /* Pulled toward the seam's open end, the line would pass 0.15 from
           it, nearer than the agent's radius: the searched points stay. */
        memset(&scenery,0,sizeof(scenery));
        assert(find((struct sn_point){1,1.7f,0},(struct sn_point){2.5f,2.25f,0},scenery_clear,&scenery)==SN_FOUND);
        assert(route.count==4 && route.corridor[1]==1);
        saved_route=route;
        sn_route_straighten(&route,&first);
        assert(route.count==3);
        route=saved_route;
        assert(sn_route_straighten_checked(&route,&first,scenery_clear,&scenery));
        assert(memcmp(&route,&saved_route,sizeof(route))==0);
    }
    /* the seam's polygon is still checked: blocked, it cannot be crossed */
    assert(find((struct sn_point){1,1,0},(struct sn_point){3.13f,1.5f,0},seam_blocked,NULL)==SN_NO_PATH);
}
static int chain_polygon(void *context,uint32_t index,struct sn_polygon *out)
{
    uint32_t count=*(uint32_t *)context;
    rectangle(0,2.f*(float)index,0,0,2,2);
    *out=source[0];
    out->neighbors[1]=index+1<count ? index+1 : SN_NONE;
    out->neighbors[3]=index>0 ? index-1 : SN_NONE;
    return 1;
}
static void test_route_capacity(void)
{
    uint32_t count;
    for (count=SN_ROUTE_POLYGONS_MAX;count<=SN_ROUTE_POLYGONS_MAX+1;count++) {
        enum sn_status status=sn_build_begin(&build,&first,count,key,7,agent);
        uint32_t calls=0;
        while(status==SN_RUNNING && calls++<100)
            status=sn_build_step(&build,&first,256,chain_polygon,&count);
        assert(status==SN_READY);
        status=find((struct sn_point){1,1,0},(struct sn_point){2.f*(float)count-1.f,1,0},NULL,NULL);
        if(count==SN_ROUTE_POLYGONS_MAX) {
            uint32_t checks=0;
            assert(status==SN_FOUND && route.count==SN_WAYPOINTS_MAX-2);
            /* One straight line across 512 floors needs more checks than the
               budget allows: the searched points stay. */
            saved_route=route;
            assert(sn_route_straighten_checked(&route,&first,floor_segment_clear,&checks));
            assert(checks==SN_STRAIGHTEN_CHECKS_MAX && memcmp(&route,&saved_route,sizeof(route))==0);
            sn_route_straighten(&route,&first);
            assert(route.count==2 && route.corridor_count==SN_ROUTE_POLYGONS_MAX);
        } else {
            assert(status==SN_CAPACITY && route.count==0);
        }
    }
}
static int many_polygons(void *context,uint32_t index,struct sn_polygon *out)
{
    (void)context;(void)index;
    *out=source[0];
    return 1;
}
static void test_malformed_and_capacity(void)
{
    enum sn_status status;
    uint32_t calls=0;
    rectangle(0,0,0,0,2,2); source[0].neighbors[1]=99;
    assert(build_resource(&second,1,6)==SN_INVALID && !second.ready);
    rectangle(0,0,0,0,2,2); source[0].vertices[0].x=NAN;
    assert(build_resource(&second,1,6)==SN_INVALID);
    rectangle(0,0,0,0,2,2); source[0].vertex_count=9;
    assert(build_resource(&second,1,6)==SN_INVALID);
    rectangle(0,0,0,0,2,2);
    assert(sn_build_begin(&build,&second,SN_SOURCE_MAX+1,key,6,agent)==SN_CAPACITY);
    status=sn_build_begin(&build,&second,SN_POLYGONS_MAX+1,key,6,agent);
    while(status==SN_RUNNING && calls++<2000)
        status=sn_build_step(&build,&second,256,many_polygons,NULL);
    assert(status==SN_CAPACITY && !second.ready);
    /* A failed staging build never changes a separate previously published resource. */
    assert(first.ready);
}
static void test_jump_candidate(void)
{
    struct sn_jump_arc arc;
    struct sn_point start={0,0,0},goal={1.7f,0,0.85f};
    assert(sn_jump_candidate(start,goal,0.05f,0.f,0.2f,0.01f,0.1f,&arc));
    assert(arc.ticks==34 && arc.points[arc.ticks].z<arc.points[arc.ticks-1].z);
    assert(!sn_jump_candidate(start,goal,0.f,0.f,0.2f,0.01f,0.1f,&arc));
    assert(arc.ticks==0);
    assert(!sn_jump_candidate(start,(struct sn_point){1,0,3},0.05f,0.f,0.2f,0.01f,0.1f,&arc));
    assert(!sn_jump_candidate(start,goal,0.05f,0.f,0.2f,0.f,0.1f,&arc));
    assert(!sn_jump_candidate(start,goal,NAN,0.f,0.2f,0.01f,0.1f,&arc));
    assert(!sn_jump_candidate(start,goal,0.05f,0.f,0.2f,0.01f,1.f,&arc));
}
int main(void)
{
    test_floor_and_ramp(); test_stacked_and_disconnected(); test_clearance();
    test_obstacles_and_revisions(); test_unknown_clearance(); test_components();
    test_rejected_edges(); test_endpoints_skip_centers(); test_shortest_route();
    test_straighten(); test_checked_straighten(); test_checked_refinement();
    test_checked_straighten_property(); test_seam(); test_jump_candidate();
    test_route_capacity(); test_malformed_and_capacity();
    printf("surface navigation passed; resource=%zu build=%zu query=%zu route=%zu bytes\n",
        sizeof(first),sizeof(build),sizeof(query),sizeof(route));
    return 0;
}
