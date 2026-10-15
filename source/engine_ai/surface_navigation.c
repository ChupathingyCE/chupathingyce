#include "surface_navigation.h"
#include <assert.h>
#include <math.h>
#include <string.h>

typedef char sn_route_capacity_check[SN_WAYPOINTS_MAX >= 2*SN_ROUTE_POLYGONS_MAX+1 ? 1 : -1];
typedef char sn_heap_slot_check[SN_POLYGONS_MAX < SN_HEAP_CLOSED ? 1 : -1];

static bool finite_point(struct sn_point p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z) &&
        fabsf(p.x) < 1000000.f && fabsf(p.y) < 1000000.f && fabsf(p.z) < 1000000.f;
}
static float distance_squared(struct sn_point a, struct sn_point b)
{
    float x = a.x-b.x, y = a.y-b.y, z = a.z-b.z;
    return x*x+y*y+z*z;
}
static struct sn_point midpoint(struct sn_point a, struct sn_point b)
{
    struct sn_point p = {(a.x+b.x)*0.5f, (a.y+b.y)*0.5f, (a.z+b.z)*0.5f};
    return p;
}
/* Horizontal distance between segments ab and cd (0 when they cross). */
static float point_segment_distance(struct sn_point p, struct sn_point a, struct sn_point b)
{
    float dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy,t=0.f,x,y;
    if (length>0.f) t=((p.x-a.x)*dx+(p.y-a.y)*dy)/length;
    if (t<0.f) t=0.f; else if (t>1.f) t=1.f;
    x=p.x-a.x-t*dx; y=p.y-a.y-t*dy;
    return sqrtf(x*x+y*y);
}
static float side(struct sn_point o, struct sn_point p, struct sn_point q)
{
    return (p.x-o.x)*(q.y-o.y)-(p.y-o.y)*(q.x-o.x);
}
static float segment_distance(struct sn_point a, struct sn_point b, struct sn_point c, struct sn_point d)
{
    float best=point_segment_distance(a,c,d),other;
    if ((side(a,b,c)>0.f)!=(side(a,b,d)>0.f) && (side(c,d,a)>0.f)!=(side(c,d,b)>0.f)) return 0.f;
    other=point_segment_distance(b,c,d); if (other<best) best=other;
    other=point_segment_distance(c,a,b); if (other<best) best=other;
    other=point_segment_distance(d,a,b); if (other<best) best=other;
    return best;
}
static float polygon_area(const struct sn_polygon *p)
{
    float area = 0.f;
    uint32_t i;
    for (i=0; i<p->vertex_count; i++) {
        struct sn_point a=p->vertices[i], b=p->vertices[(i+1)%p->vertex_count];
        area += a.x*b.y-b.x*a.y;
    }
    return area;
}
static float edge_distance(const struct sn_polygon *p, uint32_t edge, struct sn_point q)
{
    struct sn_point a=p->vertices[edge], b=p->vertices[(edge+1)%p->vertex_count];
    float dx=b.x-a.x, dy=b.y-a.y, length=sqrtf(dx*dx+dy*dy);
    float cross=dx*(q.y-a.y)-dy*(q.x-a.x);
    return (polygon_area(p)>0.f ? cross : -cross)/length;
}
static bool contains(const struct sn_resource *r, uint32_t index, struct sn_point point);
static bool valid_agent(struct sn_agent a)
{
    return isfinite(a.radius) && a.radius>0.f && a.radius<10.f &&
        isfinite(a.height) && a.height>=2.f*a.radius && a.height<20.f &&
        isfinite(a.minimum_normal_z) && a.minimum_normal_z>0.f && a.minimum_normal_z<=1.f &&
        isfinite(a.projection_height) && a.projection_height>0.f && a.projection_height<10.f;
}
/* -1 malformed, 0 valid but unsuitable, 1 usable. Validate before filtering. */
static int prepare_polygon(struct sn_polygon *p, struct sn_agent agent, uint32_t sources)
{
    uint32_t i,j;
    struct sn_point center={0.f,0.f,0.f};
    float norm=p->normal.x*p->normal.x+p->normal.y*p->normal.y+p->normal.z*p->normal.z;
    if (p->vertex_count<3 || p->vertex_count>SN_VERTICES_MAX || !finite_point(p->normal) ||
        fabsf(norm-1.f)>0.01f || !isfinite(p->width) || !isfinite(p->height) ||
        p->width<0.f || p->height<0.f || ((p->width==0.f)!=(p->height==0.f))) return -1;
    for (i=0; i<p->vertex_count; i++) {
        struct sn_point a=p->vertices[i], b=p->vertices[(i+1)%p->vertex_count];
        float dx=b.x-a.x,dy=b.y-a.y;
        struct sn_point delta={a.x-p->vertices[0].x,a.y-p->vertices[0].y,a.z-p->vertices[0].z};
        if (!finite_point(a) || dx*dx+dy*dy<0.00000001f ||
            fabsf(delta.x*p->normal.x+delta.y*p->normal.y+delta.z*p->normal.z)>0.03f ||
            (p->neighbors[i]!=SN_NONE && p->neighbors[i]>=sources)) return -1;
        center.x+=a.x; center.y+=a.y; center.z+=a.z;
    }
    if (fabsf(polygon_area(p))<0.000001f) return -1;
    for (i=0; i<p->vertex_count; i++)
        for (j=0; j<p->vertex_count; j++)
            if (edge_distance(p,i,p->vertices[j]) < -0.003f) return -1;
    p->center.x=center.x/p->vertex_count;
    p->center.y=center.y/p->vertex_count;
    p->center.z=center.z/p->vertex_count;
    return p->normal.z>=agent.minimum_normal_z && (p->width==0.f ||
        (p->width>=2.f*agent.radius && p->height>=agent.height));
}
enum sn_status sn_build_begin(struct sn_build *b, struct sn_resource *out,
    uint32_t count, struct sn_key key, uint64_t revision, struct sn_agent agent)
{
    assert(b && out);
    out->ready=false; out->requires_clearance=false; out->polygon_count=0;
    b->status=SN_INVALID;
    if (!valid_agent(agent) || key.format_version!=SN_FORMAT_VERSION || !revision || !count) return b->status;
    if (count>SN_SOURCE_MAX) return b->status=SN_CAPACITY;
    memset(b->mapping,0xff,count*sizeof(b->mapping[0]));
    b->source_count=count; b->cursor=0; b->phase=0;
    out->key=key; out->agent=agent; out->revision=revision;
    return b->status=SN_RUNNING;
}
static enum sn_status read_row(struct sn_build *b, struct sn_resource *out,
    sn_read_polygon read, void *context)
{
    struct sn_polygon p;
    int result;
    memset(&p,0,sizeof(p));
    result=read(context,b->cursor,&p);
    if (result<0 || result>1) return SN_INVALID;
    if (!result) return SN_RUNNING;
    result=prepare_polygon(&p,out->agent,b->source_count);
    if (result<0) return SN_INVALID;
    if (!result) return SN_RUNNING;
    if (out->polygon_count==SN_POLYGONS_MAX) return SN_CAPACITY;
    if (p.width==0.f) out->requires_clearance=true;
    p.source_index=b->cursor;
    b->mapping[b->cursor]=out->polygon_count;
    out->polygons[out->polygon_count++]=p;
    return SN_RUNNING;
}
static void map_row(const struct sn_build *b, struct sn_resource *out, uint32_t row)
{
    struct sn_polygon *p=&out->polygons[row];
    uint32_t e;
    for (e=0; e<p->vertex_count; e++) {
        uint32_t next=p->neighbors[e];
        struct sn_point a=p->vertices[e],z=p->vertices[(e+1)%p->vertex_count];
        float dx=a.x-z.x,dy=a.y-z.y;
        p->neighbors[e]=(next!=SN_NONE && dx*dx+dy*dy>4.f*out->agent.radius*out->agent.radius) ?
            b->mapping[next] : SN_NONE;
    }
}
static bool valid_adjacency(const struct sn_resource *r, uint32_t row)
{
    const struct sn_polygon *p=&r->polygons[row];
    uint32_t e,j;
    for (e=0; e<p->vertex_count; e++) {
        uint32_t n=p->neighbors[e];
        bool found=false;
        if (n==SN_NONE) continue;
        if (n>=r->polygon_count || n==row) return false;
        for (j=0; j<r->polygons[n].vertex_count; j++) {
            const struct sn_polygon *q=&r->polygons[n];
            struct sn_point a=p->vertices[e],b=p->vertices[(e+1)%p->vertex_count];
            struct sn_point c=q->vertices[j],d=q->vertices[(j+1)%q->vertex_count];
            if (q->neighbors[j]==row && ((distance_squared(a,c)<0.0001f && distance_squared(b,d)<0.0001f) ||
                (distance_squared(a,d)<0.0001f && distance_squared(b,c)<0.0001f))) found=true;
        }
        if (!found) return false;
    }
    return true;
}
/* Union-find over static adjacency; the root is always the lowest index, so the
   result is independent of build budget. Path halving keeps each step bounded
   in practice; components[] is the parent array until the flatten phase. */
static uint32_t component_root(uint32_t *parent, uint32_t row)
{
    while (parent[row]!=row) { parent[row]=parent[parent[row]]; row=parent[row]; }
    return row;
}
/* Whether the straight line a-b across polygon row keeps the agent's radius
   from every edge with no neighbor (horizontally; at most 8 edges). */
static bool narrow_clear(const struct sn_resource *r, uint32_t row, struct sn_point a, struct sn_point b)
{
    const struct sn_polygon *p=&r->polygons[row];
    uint32_t e;
    for (e=0; e<p->vertex_count; e++)
        if (p->neighbors[e]==SN_NONE &&
            segment_distance(a,b,p->vertices[e],p->vertices[(e+1)%p->vertex_count])<r->agent.radius-0.0001f)
            return false;
    return true;
}
/* A polygon too narrow to stand in (a seam between two floors, a sliver
   under a door frame) can still be crossed from one shared edge to another:
   the straight line between the two portals' midpoints must keep the agent's
   radius from every edge with no neighbor. Such a polygon is never a route
   node or endpoint; it joins its two neighbors. */
static bool crossing(const struct sn_resource *r, uint32_t row, uint32_t in, uint32_t out)
{
    const struct sn_polygon *p=&r->polygons[row];
    if (in==out || p->neighbors[in]==SN_NONE || p->neighbors[out]==SN_NONE ||
        p->neighbors[in]==p->neighbors[out]) return false;
    return narrow_clear(r,row,midpoint(p->vertices[in],p->vertices[(in+1)%p->vertex_count]),
        midpoint(p->vertices[out],p->vertices[(out+1)%p->vertex_count]));
}
static void join(uint32_t *parent, uint32_t x, uint32_t y)
{
    uint32_t a=component_root(parent,x),b=component_root(parent,y);
    if (a<b) parent[b]=a; else if (b<a) parent[a]=b;
}
static void union_row(struct sn_resource *r, uint32_t row)
{
    const struct sn_polygon *p=&r->polygons[row];
    uint32_t e,f;
    if (r->components[row]!=SN_NONE) {
        for (e=0; e<p->vertex_count; e++) {
            uint32_t n=p->neighbors[e];
            if (n!=SN_NONE && r->components[n]!=SN_NONE) join(r->components,row,n);
        }
        return;
    }
    for (e=0; e<p->vertex_count; e++)
        for (f=e+1; f<p->vertex_count; f++) {
            uint32_t a=p->neighbors[e],b=p->neighbors[f];
            if (a!=SN_NONE && b!=SN_NONE && r->components[a]!=SN_NONE &&
                r->components[b]!=SN_NONE && crossing(r,row,e,f)) join(r->components,a,b);
        }
}
enum sn_status sn_project_below(const struct sn_resource *r, struct sn_point point,
    float depth, uint32_t *polygon, struct sn_point *projected)
{
    struct sn_point probe=point;
    float step;
    enum sn_status status;
    uint32_t band;
    assert(r && polygon && projected);
    *polygon=SN_NONE;
    if (!isfinite(depth) || depth<=0.f || depth>100.f) return SN_INVALID;
    status=sn_project(r,point,polygon,projected);
    if (status!=SN_NO_PATH) return status;
    step=r->agent.projection_height*0.5f;
    /* Cap repeated polygon scans even for an unusually small agent. */
    if (step<depth/64.f) step=depth/64.f;
    for (band=1; band<=64 && band<=(uint32_t)(depth/step)+1; band++) {
        float drop=(float)band*step;
        probe.z=point.z-(drop<depth ? drop : depth);
        status=sn_project(r,probe,polygon,projected);
        if (status!=SN_NO_PATH) return status;
    }
    return SN_NO_PATH;
}
static float plane_z(const struct sn_polygon *p, struct sn_point q)
{
    return p->vertices[0].z-(p->normal.x*(q.x-p->vertices[0].x)+
        p->normal.y*(q.y-p->vertices[0].y))/p->normal.z;
}
enum sn_status sn_project_near(const struct sn_resource *r, struct sn_point point,
    float depth, float horizontal, uint32_t *polygon, struct sn_point *projected)
{
    enum sn_status status;
    float best;
    uint32_t i;
    assert(r && polygon && projected);
    *polygon=SN_NONE;
    if (!isfinite(horizontal) || horizontal<=0.f || horizontal>10.f) return SN_INVALID;
    status=sn_project_below(r,point,depth,polygon,projected);
    if (status!=SN_NO_PATH) return status;
    best=horizontal*horizontal;
    for (i=0; i<r->polygon_count; i++) {
        const struct sn_polygon *p=&r->polygons[i];
        struct sn_point q=point,c=p->center;
        float dx,dy,lo=0.f,hi=1.f,floor;
        uint32_t bisect;
        if (r->components[i]==SN_NONE) continue;
        dx=c.x-point.x; dy=c.y-point.y;
        if (contains(r,i,point)) continue; /* (then sn_project rejected its height) */
        for (bisect=0; bisect<12; bisect++) {
            float t=(lo+hi)*0.5f;
            q.x=point.x+dx*t; q.y=point.y+dy*t;
            if (contains(r,i,q)) hi=t; else lo=t;
        }
        q.x=point.x+dx*hi; q.y=point.y+dy*hi;
        dx=q.x-point.x; dy=q.y-point.y;
        if (dx*dx+dy*dy>=best) continue;
        floor=plane_z(p,q);
        if (floor<point.z-depth || floor>point.z+r->agent.projection_height) continue;
        best=dx*dx+dy*dy; q.z=floor; *polygon=i; *projected=q;
        status=SN_FOUND;
    }
    return status;
}
uint32_t sn_component(const struct sn_resource *r, uint32_t polygon)
{
    assert(r);
    return r->ready && polygon<r->polygon_count ? r->components[polygon] : SN_NONE;
}
enum sn_status sn_build_step(struct sn_build *b, struct sn_resource *out,
    uint32_t budget, sn_read_polygon read, void *context)
{
    uint32_t work;
    assert(b && out);
    if (b->status!=SN_RUNNING) return b->status;
    if (!read || !budget || budget>256) return b->status=SN_INVALID;
    for (work=0; work<budget; work++) {
        uint32_t count=b->phase==0 ? b->source_count : out->polygon_count;
        if (b->cursor==count) {
            b->cursor=0;
            if (++b->phase==5) {
                out->ready=true;
                return b->status=SN_READY;
            }
            continue;
        }
        if (b->phase==0) b->status=read_row(b,out,read,context);
        else if (b->phase==1) map_row(b,out,b->cursor);
        else if (b->phase==2) {
            if (!valid_adjacency(out,b->cursor)) b->status=SN_INVALID;
            out->components[b->cursor]=contains(out,b->cursor,out->polygons[b->cursor].center) ?
                b->cursor : SN_NONE;
        } else if (b->phase==3) union_row(out,b->cursor);
        else if (out->components[b->cursor]!=SN_NONE)
            out->components[b->cursor]=component_root(out->components,b->cursor);
        if (b->status!=SN_RUNNING) return b->status;
        b->cursor++;
    }
    return b->status;
}
static bool contains(const struct sn_resource *r, uint32_t index, struct sn_point point)
{
    const struct sn_polygon *p=&r->polygons[index];
    uint32_t edge;
    for (edge=0; edge<p->vertex_count; edge++) {
        float clearance=p->neighbors[edge]==SN_NONE ? r->agent.radius : 0.f;
        if (edge_distance(p,edge,point)<clearance-0.0001f) return false;
    }
    return true;
}
enum sn_status sn_project(const struct sn_resource *r, struct sn_point point,
    uint32_t *polygon, struct sn_point *projected)
{
    uint32_t i;
    float best;
    assert(r && polygon && projected);
    *polygon=SN_NONE;
    if (!r->ready || !finite_point(point)) return SN_INVALID;
    best=r->agent.projection_height+0.00001f;
    for (i=0; i<r->polygon_count; i++) {
        const struct sn_polygon *p=&r->polygons[i];
        struct sn_point q=point;
        float dz;
        if (!contains(r,i,p->center) || !contains(r,i,point)) continue;
        q.z=p->vertices[0].z-(p->normal.x*(point.x-p->vertices[0].x)+
            p->normal.y*(point.y-p->vertices[0].y))/p->normal.z;
        dz=fabsf(q.z-point.z);
        if (dz<best) { best=dz; *polygon=i; *projected=q; }
    }
    return *polygon==SN_NONE ? SN_NO_PATH : SN_FOUND;
}
static float distance(struct sn_point a, struct sn_point b)
{
    return sqrtf(distance_squared(a,b));
}
/* Estimated total length through a polygon: its best length so far plus the
   straight line from its center (or the start point) to the goal point. */
static float estimate(const struct sn_query *q, const struct sn_resource *r, uint32_t row)
{
    struct sn_point from=row==q->start ? q->start_point : r->polygons[row].center;
    return q->cost[row]+(row==q->goal ? 0.f : distance(from,q->goal_point));
}
static bool heap_before(const struct sn_query *q, const struct sn_resource *r, uint32_t a, uint32_t b)
{
    float x=estimate(q,r,a),y=estimate(q,r,b);
    return x<y || (x==y && a<b);
}
static void heap_place(struct sn_query *q, uint32_t slot, uint32_t row)
{
    q->heap[slot]=row; q->heap_slot[row]=(uint16_t)slot;
}
static void heap_up(struct sn_query *q, const struct sn_resource *r, uint32_t slot)
{
    uint32_t row=q->heap[slot];
    while (slot>0 && heap_before(q,r,row,q->heap[(slot-1)/2])) {
        heap_place(q,slot,q->heap[(slot-1)/2]);
        slot=(slot-1)/2;
    }
    heap_place(q,slot,row);
}
static void heap_push(struct sn_query *q, const struct sn_resource *r, uint32_t row)
{
    assert(q->heap_count<SN_POLYGONS_MAX);
    heap_place(q,q->heap_count++,row);
    heap_up(q,r,q->heap_count-1);
}
static uint32_t heap_pop(struct sn_query *q, const struct sn_resource *r)
{
    uint32_t top=q->heap[0],slot=0,row;
    row=q->heap[--q->heap_count];
    while (q->heap_count) {
        uint32_t child=2*slot+1;
        if (child>=q->heap_count) break;
        if (child+1<q->heap_count && heap_before(q,r,q->heap[child+1],q->heap[child])) child++;
        if (!heap_before(q,r,q->heap[child],row)) break;
        heap_place(q,slot,q->heap[child]);
        slot=child;
    }
    if (q->heap_count) heap_place(q,slot,row);
    q->heap_slot[top]=SN_HEAP_CLOSED;
    return top;
}
enum sn_status sn_query_begin(struct sn_query *q, const struct sn_resource *r,
    struct sn_point start, struct sn_point goal, uint64_t obstacles)
{
    enum sn_status status;
    assert(q && r);
    q->status=SN_INVALID;
    if (!r->ready) return q->status;
    q->key=r->key; q->revision=r->revision; q->obstacle_revision=obstacles;
    q->start=q->goal=SN_NONE; q->rejected_edges=0; q->start_rejected=q->goal_rejected=false;
    q->check_goal_segment=true;
    status=sn_project(r,start,&q->start,&q->start_point);
    if (status!=SN_FOUND) return q->status=status==SN_INVALID ? SN_INVALID : SN_BAD_START;
    status=sn_project(r,goal,&q->goal,&q->goal_point);
    if (status!=SN_FOUND) return q->status=status==SN_INVALID ? SN_INVALID : SN_BAD_GOAL;
    /* Projection only accepts polygons whose center is usable, so both have components. */
    if (r->components[q->start]!=r->components[q->goal]) return q->status=SN_DISCONNECTED;
    memset(q->previous,0xff,r->polygon_count*sizeof(q->previous[0]));
    q->previous[q->start]=q->start;
    q->via[q->start]=SN_VIA_NONE;
    q->cost[q->start]=0.f;
    q->heap_count=0;
    heap_push(q,r,q->start);
    return q->status=SN_RUNNING;
}
static bool same_key(struct sn_key a, struct sn_key b)
{
    return a.map_identity==b.map_identity && a.overlay_revision==b.overlay_revision &&
        a.checksum==b.checksum && a.bsp_index==b.bsp_index &&
        a.agent_identity==b.agent_identity && a.format_version==b.format_version;
}
static bool current(const struct sn_query *q, const struct sn_resource *r, uint64_t obstacles)
{
    return r->ready && same_key(q->key,r->key) &&
        r->revision==q->revision && q->obstacle_revision==obstacles;
}
/* Inside the start polygon a route leaves from the start point itself, and
   inside the goal polygon it goes straight to the goal point: both polygons are
   convex, so neither segment detours through an arbitrary center that another
   object (or the polygon's far side) may block. Other polygons pass through
   their centers. A unit-position goal skips the in-goal segment entirely. */
/* Offer polygon n, reached by the given segments (from -> first portal in the
   expanded polygon, then through an optional narrow polygon `via`, then to
   n's center or the goal point), at its new length. */
struct expansion {
    struct sn_query *q;
    const struct sn_resource *r;
    sn_segment_clear clear;
    void *context;
    uint32_t row, edge, candidates, accepted;
    struct sn_point from;
};
static void relax(struct expansion *x, uint32_t n, struct sn_point portal,
    uint32_t via, uint32_t via_edge, struct sn_point via_portal)
{
    struct sn_query *q=x->q;
    const struct sn_resource *r=x->r;
    bool into_goal=n==q->goal,seen=q->previous[n]!=SN_NONE;
    struct sn_point entry=via==SN_NONE ? portal : via_portal;
    struct sn_point to=into_goal ? q->goal_point : r->polygons[n].center;
    float length;
    if (seen && q->heap_slot[n]==SN_HEAP_CLOSED) return;
    length=q->cost[x->row]+distance(x->from,portal)+distance(portal,entry)+distance(entry,to);
    if (seen && length>=q->cost[n]) return;
    x->candidates++;
    if (x->clear && (!x->clear(x->context,x->from,x->row,portal,x->row) ||
        (via!=SN_NONE && !x->clear(x->context,portal,via,via_portal,via)) ||
        ((!into_goal || q->check_goal_segment) && !x->clear(x->context,entry,n,to,n)))) {
        q->rejected_edges++;
        if (into_goal) q->goal_rejected=true;
        return;
    }
    x->accepted++;
    q->previous[n]=x->row; q->previous_edge[n]=(uint8_t)x->edge; q->cost[n]=length;
    q->via[n]=(uint16_t)(via==SN_NONE ? SN_VIA_NONE : via);
    q->via_edge[n]=(uint8_t)via_edge;
    if (seen) heap_up(q,r,q->heap_slot[n]);
    else heap_push(q,r,n);
}
static bool expand(struct sn_query *q, const struct sn_resource *r, uint32_t row,
    sn_segment_clear clear, void *context)
{
    const struct sn_polygon *p=&r->polygons[row];
    struct expansion x;
    uint32_t e;
    x.q=q; x.r=r; x.clear=clear; x.context=context; x.row=row;
    x.candidates=x.accepted=0;
    x.from=row==q->start ? q->start_point : p->center;
    for (e=0; e<p->vertex_count; e++) {
        uint32_t n=p->neighbors[e],in,out;
        const struct sn_polygon *narrow;
        struct sn_point portal;
        if (n==SN_NONE) continue;
        x.edge=e;
        portal=midpoint(p->vertices[e],p->vertices[(e+1)%p->vertex_count]);
        if (r->components[n]!=SN_NONE) { relax(&x,n,portal,SN_NONE,0,portal); continue; }
        /* n is too narrow to stand in: cross it to each of its other neighbors */
        narrow=&r->polygons[n];
        for (in=0; in<narrow->vertex_count && narrow->neighbors[in]!=row; in++) {}
        if (in==narrow->vertex_count) continue;
        for (out=0; out<narrow->vertex_count; out++) {
            uint32_t m=narrow->neighbors[out];
            if (m==SN_NONE || m==row || r->components[m]==SN_NONE || !crossing(r,n,in,out)) continue;
            relax(&x,m,portal,n,out,midpoint(narrow->vertices[out],
                narrow->vertices[(out+1)%narrow->vertex_count]));
        }
    }
    if (row==q->start && x.candidates && !x.accepted) q->start_rejected=true;
    return true;
}
enum sn_status sn_query_step(struct sn_query *q, const struct sn_resource *r,
    uint64_t obstacles, uint32_t budget, sn_segment_clear clear, void *context)
{
    uint32_t work;
    assert(q && r);
    if (!current(q,r,obstacles)) return q->status=SN_STALE;
    if (q->status!=SN_RUNNING) return q->status;
    if (!budget || budget>256 || (r->requires_clearance && !clear)) return q->status=SN_INVALID;
    /* Within one polygon the route is the single segment start->goal. */
    if (clear && q->start==q->goal && q->check_goal_segment &&
        !clear(context,q->start_point,q->start,q->goal_point,q->goal)) {
        q->start_rejected=q->goal_rejected=true;
        return q->status=SN_NO_PATH;
    }
    for (work=0; work<budget && q->heap_count; work++) {
        uint32_t row=heap_pop(q,r);
        if (row==q->goal) return q->status=SN_FOUND;
        expand(q,r,row,clear,context);
    }
    return q->status=q->heap_count ? SN_RUNNING : SN_NO_PATH;
}
static void append(struct sn_route *route, struct sn_point point, uint32_t polygon, uint32_t step)
{
    assert(route->count<SN_WAYPOINTS_MAX && step<SN_ROUTE_POLYGONS_MAX);
    route->points[route->count]=point; route->polygons[route->count]=polygon;
    route->corridor_index[route->count++]=(uint16_t)step;
}
enum sn_status sn_query_result(const struct sn_query *q, const struct sn_resource *r,
    uint64_t obstacles, struct sn_route *route)
{
    /* chain[] runs goal to start; leave[i] is the edge chain[i] is left by
       toward chain[i-1] */
    uint32_t chain[SN_ROUTE_POLYGONS_MAX],count=0,row,i;
    uint8_t leave[SN_ROUTE_POLYGONS_MAX];
    assert(q && r && route);
    route->count=0;
    if (!current(q,r,obstacles)) return SN_STALE;
    if (q->status!=SN_FOUND) return q->status;
    row=q->goal;
    while (count<SN_ROUTE_POLYGONS_MAX) {
        chain[count++]=row;
        if (row==q->start) break;
        if (q->via[row]!=SN_VIA_NONE) {
            if (count==SN_ROUTE_POLYGONS_MAX) return SN_CAPACITY;
            leave[count]=q->via_edge[row];
            chain[count++]=q->via[row];
        }
        if (count<SN_ROUTE_POLYGONS_MAX) leave[count]=q->previous_edge[row];
        row=q->previous[row];
        if (row>=r->polygon_count) return SN_INVALID;
    }
    if (chain[count-1]!=q->start) return SN_CAPACITY;
    route->key=q->key; route->revision=q->revision; route->obstacle_revision=q->obstacle_revision;
    route->corridor_count=count;
    for (i=0; i<count; i++) route->corridor[i]=chain[count-1-i];
    append(route,q->start_point,q->start,0);
    for (i=count; i>0; i--) {
        uint32_t n=chain[i-1],step=count-i;
        const struct sn_polygon *p=&r->polygons[n];
        if (n!=q->start && n!=q->goal && r->components[n]!=SN_NONE) append(route,p->center,n,step);
        if (i>1) {
            uint32_t e=leave[i-1];
            append(route,midpoint(p->vertices[e],p->vertices[(e+1)%p->vertex_count]),n,step);
        }
    }
    append(route,q->goal_point,q->goal,count-1);
    assert(route->count<=SN_WAYPOINTS_MAX);
    return SN_FOUND;
}
/* Twice the signed area of a,b,c in the horizontal plane: positive when c is
   to the left of a->b. */
static float turn(struct sn_point a, struct sn_point b, struct sn_point c)
{
    return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}
static bool same_point(struct sn_point a, struct sn_point b)
{
    return distance_squared(a,b)<0.000001f;
}
/* The portal from corridor polygon `step-1` into `step`, as seen walking
   through it: its left and right ends, each moved in along the edge by the
   agent's radius (the edge is longer than the agent's width: map_row). */
static bool portal_ends(const struct sn_route *route, const struct sn_resource *r, uint32_t step,
    struct sn_point *left, struct sn_point *right)
{
    const struct sn_polygon *p=&r->polygons[route->corridor[step-1]];
    struct sn_point a,b,from;
    float length,t;
    uint32_t e;
    for (e=0; e<p->vertex_count; e++) if (p->neighbors[e]==route->corridor[step]) break;
    if (e==p->vertex_count) return false;
    a=p->vertices[e]; b=p->vertices[(e+1)%p->vertex_count];
    from=step==1 ? route->points[0] : p->center;
    length=sqrtf((b.x-a.x)*(b.x-a.x)+(b.y-a.y)*(b.y-a.y));
    t=length>0.f ? r->agent.radius/length : 0.5f;
    if (t>0.5f) t=0.5f;
    if (turn(from,midpoint(a,b),a)>=0.f) { *left=a; *right=b; }
    else { *left=b; *right=a; }
    {
        struct sn_point l=*left,q=*right;
        left->x=l.x+(q.x-l.x)*t; left->y=l.y+(q.y-l.y)*t; left->z=l.z+(q.z-l.z)*t;
        right->x=q.x+(l.x-q.x)*t; right->y=q.y+(l.y-q.y)*t; right->z=q.z+(l.z-q.z)*t;
    }
    return true;
}
/* port: Validate the public route before following any of its array indices.
   All scans are bounded by the route's fixed polygon/waypoint capacities. */
static bool valid_route(const struct sn_route *route, const struct sn_resource *r)
{
    uint32_t i;
    if (!sn_route_current(route,r,route->obstacle_revision) || route->count<2 ||
        route->count>SN_WAYPOINTS_MAX || !route->corridor_count ||
        route->corridor_count>SN_ROUTE_POLYGONS_MAX) return false;
    for (i=0; i<route->corridor_count; i++)
        if (route->corridor[i]>=r->polygon_count) return false;
    for (i=0; i<route->count; i++)
        if (!finite_point(route->points[i]) || route->corridor_index[i]>=route->corridor_count ||
            route->polygons[i]!=route->corridor[route->corridor_index[i]] ||
            (i && route->corridor_index[i]<route->corridor_index[i-1])) return false;
    return route->corridor_index[0]==0 &&
        route->corridor_index[route->count-1]==route->corridor_count-1;
}
/* port: String-pulling scratch, shared by both straighteners (one simulation
   thread; neither is reentrant): each portal's inset ends, the route being
   built, and the checked straightener's pending spans. All the funnels of
   one checked straightening together run at most SN_STRAIGHTEN_WORK_MAX steps. */
enum { SN_STRAIGHTEN_SPANS_MAX = 32, SN_STRAIGHTEN_WORK_MAX = 16*SN_ROUTE_POLYGONS_MAX };
static struct {
    struct sn_point left[SN_ROUTE_POLYGONS_MAX], right[SN_ROUTE_POLYGONS_MAX];
    struct sn_route out; /* (20 KB; not on the caller's stack) */
    uint32_t span_from[SN_STRAIGHTEN_SPANS_MAX], span_to[SN_STRAIGHTEN_SPANS_MAX];
} pull;
/* Every portal's ends before any is used: an inconsistent corridor stays
   unsmoothed. */
static bool funnel_portals(const struct sn_route *route, const struct sn_resource *r)
{
    uint32_t i;
    for (i=1; i<route->corridor_count; i++)
        if (!portal_ends(route,r,i,&pull.left[i],&pull.right[i])) return false;
    return true;
}
/* Simple stupid funnel (Mononen): advance the left and right sides of a
   funnel from the apex through the portals; when one side would cross the
   other, its far end becomes a corner (the new apex) and the walk restarts
   from there. Here from `start` in corridor polygon `first` through portals
   first+1..last to `goal` in polygon `last`, appending the corners and the
   goal to pull.out (`start` is already its last point). False when *work
   reaches work_max or a corner would not fit; pull.out then holds a partial
   span for the caller to discard. */
static bool funnel(const struct sn_route *route, uint32_t first, uint32_t last,
    struct sn_point start, struct sn_point goal, uint32_t *work, uint32_t work_max)
{
    struct sn_route *out=&pull.out;
    struct sn_point apex=start,left=start,right=start,l,q;
    uint32_t i,apex_step=first,left_step=first,right_step=first,base=out->count;
    for (i=first+1; i<=last+1; i++) {
        if (*work>=work_max) return false;
        (*work)++;
        if (i<=last) { l=pull.left[i]; q=pull.right[i]; }
        else l=q=goal;
        /* the right side narrows toward q */
        if (turn(apex,right,q)>=0.f) {
            if (same_point(apex,right) || turn(apex,left,q)<0.f) { right=q; right_step=i; }
            else {
                /* (a corner beyond capacity: keep the original route) */
                if (out->count>=SN_WAYPOINTS_MAX-1) return false;
                append(out,left,route->corridor[left_step<=last ? left_step : last],
                    left_step<=last ? left_step : last);
                apex=right=left; apex_step=right_step=left_step;
                i=apex_step; continue;
            }
        }
        /* the left side narrows toward l */
        if (turn(apex,left,l)<=0.f) {
            if (same_point(apex,left) || turn(apex,right,l)>0.f) { left=l; left_step=i; }
            else {
                if (out->count>=SN_WAYPOINTS_MAX-1) return false;
                append(out,right,route->corridor[right_step<=last ? right_step : last],
                    right_step<=last ? right_step : last);
                apex=left=right; apex_step=left_step=right_step;
                i=apex_step; continue;
            }
        }
    }
    /* (the last corner may already be the goal) */
    if (out->count>base && same_point(out->points[out->count-1],goal)) out->count--;
    if (out->count>=SN_WAYPOINTS_MAX) return false;
    append(out,goal,route->corridor[last],last);
    return true;
}
/* port: Intersect a horizontal shortcut with the next shared edge. Parallel,
   backwards or out-of-edge crossings conservatively retain the original path. */
static bool shortcut_portal(const struct sn_route *route, const struct sn_resource *r,
    uint32_t step, struct sn_point from, struct sn_point to, float *progress,
    struct sn_point *crossing_point)
{
    const struct sn_polygon *p=&r->polygons[route->corridor[step-1]];
    struct sn_point a,b;
    float dx=to.x-from.x,dy=to.y-from.y,ex,ey,denominator,t,u;
    uint32_t e;
    for (e=0; e<p->vertex_count; e++) if (p->neighbors[e]==route->corridor[step]) break;
    if (e==p->vertex_count) return false;
    a=p->vertices[e]; b=p->vertices[(e+1)%p->vertex_count];
    ex=b.x-a.x; ey=b.y-a.y; denominator=dx*ey-dy*ex;
    if (fabsf(denominator)<0.000001f) return false;
    t=((a.x-from.x)*ey-(a.y-from.y)*ex)/denominator;
    u=((a.x-from.x)*dy-(a.y-from.y)*dx)/denominator;
    if (t<*progress-0.0001f || t>1.0001f || u<-0.0001f || u>1.0001f) return false;
    if (t<*progress) t=*progress;
    if (t>1.f) t=1.f;
    *progress=t;
    crossing_point->x=from.x+dx*t; crossing_point->y=from.y+dy*t;
    crossing_point->z=plane_z(p,*crossing_point);
    return true;
}
/* port: Check the straight segment from `from` (corridor step first) to `to`
   (step last) one corridor polygon at a time, each piece on that polygon's
   own floor: a single chord across a hill hits the ground the walking route
   clears. A piece across a seam too narrow to stand in must also keep the
   agent's radius from the seam's open edges, as crossing() requires of the
   search. A piece of no length (a corner on its portal) needs no check. False,
   with *fail the step of the refused piece, on a refusal, an ambiguous portal
   crossing or *checks reaching SN_STRAIGHTEN_CHECKS_MAX. */
static bool segment_clear(const struct sn_route *route, const struct sn_resource *r,
    struct sn_point from, uint32_t first, struct sn_point to, uint32_t last,
    sn_segment_clear clear, void *context, uint32_t *checks, uint32_t *fail)
{
    struct sn_point a=from,b;
    float progress=0.f;
    uint32_t step;
    for (step=first; step<=last; step++) {
        uint32_t polygon=route->corridor[step];
        const struct sn_polygon *p=&r->polygons[polygon];
        *fail=step;
        if (step<last) {
            if (!shortcut_portal(route,r,step+1,from,to,&progress,&b)) return false;
        } else b=to;
        a.z=plane_z(p,a); b.z=plane_z(p,b);
        if (r->components[polygon]==SN_NONE && !narrow_clear(r,polygon,a,b)) return false;
        if (!same_point(a,b)) {
            if (*checks>=SN_STRAIGHTEN_CHECKS_MAX) return false;
            (*checks)++;
            if (!clear(context,a,polygon,b,polygon)) return false;
        }
        a=b;
    }
    return true;
}
/* The segments pull.out has gained since its point `from`, each checked. */
static bool span_clear(const struct sn_route *route, const struct sn_resource *r, uint32_t from,
    sn_segment_clear clear, void *context, uint32_t *checks, uint32_t *fail)
{
    const struct sn_route *out=&pull.out;
    uint32_t i;
    for (i=from+1; i<out->count; i++)
        if (!segment_clear(route,r,out->points[i-1],out->corridor_index[i-1],out->points[i],
            out->corridor_index[i],clear,context,checks,fail)) return false;
    return true;
}
/* A query result passes through the center of each polygon between its
   start and goal polygons, a seam excepted (sn_query_result). */
static bool center_point(const struct sn_route *route, const struct sn_resource *r, uint32_t j)
{
    struct sn_point c;
    if (j==0 || j+1>=route->count) return false;
    c=r->polygons[route->polygons[j]].center;
    return route->points[j].x==c.x && route->points[j].y==c.y && route->points[j].z==c.z;
}
/* Where to pin a refused span p..q: the center among its inner points whose
   corridor step is nearest the refusal (the first on a tie); SN_NONE if it
   has none. At most q-p-1 points. */
static uint32_t span_pin(const struct sn_route *route, const struct sn_resource *r,
    uint32_t p, uint32_t q, uint32_t fail)
{
    uint32_t j,best=SN_NONE,best_gap=UINT32_MAX;
    for (j=p+1; j<q; j++) {
        uint32_t step=route->corridor_index[j],gap=step>fail ? step-fail : fail-step;
        if (gap<best_gap && center_point(route,r,j)) { best=j; best_gap=gap; }
    }
    return best;
}
static void straighten_begin(const struct sn_route *route)
{
    pull.out=*route; pull.out.count=0;
    append(&pull.out,route->points[0],route->corridor[0],0);
}
/* Replace a found route's points by the shortest path through its corridor
   (see the header). Keeps the original route when the corridor is
   inconsistent or the funnel needs more than 4*SN_ROUTE_POLYGONS_MAX^2 steps. */
void sn_route_straighten(struct sn_route *route, const struct sn_resource *r)
{
    uint32_t work=0;
    assert(route && r);
    if (!valid_route(route,r) || route->corridor_count<2 || !funnel_portals(route,r)) return;
    straighten_begin(route);
    if (!funnel(route,0,route->corridor_count-1,route->points[0],route->points[route->count-1],
        &work,4u*SN_ROUTE_POLYGONS_MAX*SN_ROUTE_POLYGONS_MAX)) return;
    *route=pull.out;
}
/* port: Collision-checked string pulling (see the header). The route is cut
   into spans between pins, original points the path must keep: first only
   its start and goal. A span is pulled tight and each new segment checked; a
   refused span is split at the original polygon center nearest the refusal,
   and a span with no center left to pin keeps its original points, which the
   search cleared. Spans are taken from a stack left to right: each pop emits a
   span or pins a center not pinned before, so at most 2*count pops; a split
   that would overflow the stack also keeps the original points, as does every
   span left when the checks or the funnel work run out. */
bool sn_route_straighten_checked(struct sn_route *route, const struct sn_resource *r,
    sn_segment_clear clear, void *context)
{
    uint32_t depth=1,checks=0,work=0,pops=0;
    assert(route && r);
    if (!clear || !valid_route(route,r) || !funnel_portals(route,r)) return false;
    straighten_begin(route);
    pull.span_from[0]=0; pull.span_to[0]=route->count-1;
    while (depth) {
        uint32_t p,q,j,base=pull.out.count;
        assert(pops<2*SN_WAYPOINTS_MAX);
        pops++;
        depth--;
        p=pull.span_from[depth]; q=pull.span_to[depth];
        if (q>p+1 && checks<SN_STRAIGHTEN_CHECKS_MAX && work<SN_STRAIGHTEN_WORK_MAX) {
            uint32_t pin,fail=(route->corridor_index[p]+route->corridor_index[q])/2;
            if (funnel(route,route->corridor_index[p],route->corridor_index[q],route->points[p],
                    route->points[q],&work,SN_STRAIGHTEN_WORK_MAX) &&
                span_clear(route,r,base-1,clear,context,&checks,&fail)) continue;
            pull.out.count=base;
            pin=span_pin(route,r,p,q,fail);
            if (pin!=SN_NONE && checks<SN_STRAIGHTEN_CHECKS_MAX && work<SN_STRAIGHTEN_WORK_MAX &&
                depth+2<=SN_STRAIGHTEN_SPANS_MAX) {
                pull.span_from[depth]=pin; pull.span_to[depth]=q; depth++;
                pull.span_from[depth]=p; pull.span_to[depth]=pin; depth++;
                continue;
            }
        }
        /* (no more than the original route's own points: it fits) */
        for (j=p+1; j<=q; j++)
            append(&pull.out,route->points[j],route->polygons[j],route->corridor_index[j]);
    }
    *route=pull.out;
    return true;
}
bool sn_route_current(const struct sn_route *route, const struct sn_resource *r, uint64_t obstacles)
{
    assert(route && r);
    return route->count>0 && r->ready && same_key(route->key,r->key) &&
        route->revision==r->revision && route->obstacle_revision==obstacles;
}
bool sn_jump_candidate(struct sn_point start, struct sn_point goal,
    float velocity_x, float velocity_y, float impulse, float gravity,
    float landing_tolerance, struct sn_jump_arc *arc)
{
    uint32_t tick;
    float vertical=impulse;
    struct sn_point point=start;
    assert(arc);
    arc->ticks=0;
    if (!finite_point(start) || !finite_point(goal) || !isfinite(velocity_x) ||
        !isfinite(velocity_y) || !isfinite(impulse) || !isfinite(gravity) ||
        !isfinite(landing_tolerance) || fabsf(velocity_x)>1.f || fabsf(velocity_y)>1.f ||
        impulse<=0.f || impulse>1.f ||
        gravity<=0.f || gravity>impulse || landing_tolerance<=0.f ||
        landing_tolerance>0.5f) return false;
    arc->points[0]=start;
    for (tick=1; tick<=SN_JUMP_TICKS_MAX; tick++) {
        float dx,dy;
        vertical-=gravity;
        point.x+=velocity_x; point.y+=velocity_y; point.z+=vertical;
        arc->points[tick]=point;
        dx=point.x-goal.x; dy=point.y-goal.y;
        if (vertical<0.f && fabsf(point.z-goal.z)<landing_tolerance &&
            dx*dx+dy*dy<landing_tolerance*landing_tolerance) {
            arc->ticks=tick;
            return true;
        }
        if (vertical<0.f && point.z<goal.z-landing_tolerance) return false;
    }
    return false;
}
const char *sn_status_name(enum sn_status status)
{
    static const char *const names[]={"invalid","running","ready","found","no-path",
        "bad-start","bad-goal","capacity","stale","disconnected"};
    return (unsigned)status<sizeof(names)/sizeof(names[0]) ? names[status] : "invalid";
}
