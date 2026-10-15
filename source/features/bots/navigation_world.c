/* Host-local BSP polygon navigation adapter. No campaign actor changes.
   Double-buffer publication; all work/storage has explicit ceilings. */
#include "cseries.h"
#include "navigation_world.h"
#include "players.h"
#include "bots.h" /* port: effective private-lobby navigation demand */
#include "game.h"
#include "scenario/scenario.h"
#include "structures/structure_bsp_definitions.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collisions.h"
#include "physics/collision_features.h"
#include "units/units.h"
#include "units/vehicles.h"
#include "objects/object_definitions.h"
#include "physics/physics_definitions.h"
#include "units/biped_definitions.h"
#include "units/bipeds.h"
#include "physics/physics.h"
#include "cache/cache_files.h"
#include "extensions/extension_api.h"
#include "bot_scenario.h" /* port: a scenario arena's floor polygons */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef HALO_FEATURE_BOTS

int config_boolean(char const *name);
long config_integer(char const *name);
char const *config_string(char const *name);
void platform_log(char const *format, ...);
boolean network_game_distributed_client(void);

/* A temporarily blocked polygon. The entry is remembered for 30 seconds after
   it expires; blocked again in that time, it stays blocked twice as long (2 s
   up to 32 s), so a parked vehicle or an unclimbable lip is learned rather
   than retried every two seconds. */
enum { NAV_BLOCKS = 64, NAV_BLOCK_MEMORY_TICKS = 30*TICKS_PER_SECOND, NAV_BLOCK_STRIKES_MAX = 4 };
struct nav_block { uint32_t polygon; long until, forget; short strikes; };

/* port: two layers of the same walkable surfaces. Bipeds walk the first, built
   for the spawned biped's capsule. Bots driving ground vehicles use the second,
   built once the first is published and only on a map with a vehicle a bot can
   drive: its slope limit is a vehicle's, and every route segment is swept with
   a pill as wide and tall as the widest such vehicle's mass points (the spheres
   Halo's vehicle physics touches the world with), raised over the bumps its
   wheels ride. The vehicle layer has one buffer, not two: it is unavailable
   while a new map or BSP builds it again, as the foot layer is not. */
enum { NAV_LAYER_FOOT, NAV_LAYER_VEHICLE, NAV_LAYERS };
struct nav_layer {
    struct sn_resource *resources[2];
    struct sn_build build;
    struct sn_key requested;
    int active, staging;
    enum sn_status status;
    boolean requested_valid;
    clock_t cpu_time;
    uint64_t obstacles;
    struct nav_block blocks[NAV_BLOCKS];
    /* Static verdicts of the canonical center->portal (low two bits) and
       portal->center (next two bits) halves of each polygon edge, for the
       active resource: 0 unknown, 1 clear, 2 blocked. */
    uint8_t edge_cache[SN_POLYGONS_MAX][SN_VERTICES_MAX];
    unsigned long sweeps, cache_hits, stats_sweeps, stats_hits;
    /* the pill routes are checked with: its radius, center-to-center height
       and how far its lower sphere is raised above the ground */
    float sweep_radius, sweep_height, sweep_lift;
};
static struct sn_resource foot_resources[2], vehicle_resource;
static struct {
    struct nav_layer layers[NAV_LAYERS];
    uint64_t revision;
    long last_tick;
    FILE *dump;
    uint32_t dump_cursor;
    char dump_path[512], temporary_path[528];
    long stats_tick;
    /* the vehicle layer's body, from the map's drivable vehicles */
    boolean vehicle_body_valid;
    float vehicle_half_width, vehicle_half_length, vehicle_height;
    /* port: whether each vehicle-layer polygon's center has open sky above
       (0 not yet known, 1 open, 2 roofed), found NAV_OPEN_SKY_RAYS_PER_TICK
       polygons a tick after the layer is published; open_sky_cursor polygons
       done */
    uint8_t open_sky[SN_POLYGONS_MAX];
    uint32_t open_sky_cursor;
    uint32_t open_sky_roofed;
} world;

/* port: the vehicle layer's topology. Its polygons are linked as the foot
   layer's are (a radius this small keeps the terrain's short shared edges
   joined: a larger one cuts rolling ground into islands); clearance from walls
   and scenery is the swept pill's, at its full width. */
#define NAV_VEHICLE_TOPOLOGY_RADIUS 0.25f
/* steeper than this is not driven (about 41 degrees; bipeds' is their tag's) */
#define NAV_VEHICLE_MINIMUM_NORMAL_Z 0.75f
/* the bumps a vehicle's wheels ride over, at most */
#define NAV_VEHICLE_SWEEP_LIFT_MAXIMUM 0.2f
/* the vehicles examined for the layer's body, and a body's sane extents */
enum { NAV_VEHICLES_EXAMINED = 256, NAV_MASS_POINTS_MAXIMUM = 32, NAV_OPEN_SKY_RAYS_PER_TICK = 64 };
/* how high above a polygon's center a roof is looked for */
#define NAV_OPEN_SKY_HEIGHT 20.f
#define NAV_VEHICLE_EXTENT_MAXIMUM 10.f

static struct sn_point nav_point(real_point3d p)
{
    struct sn_point out={p.x,p.y,p.z};
    return out;
}
static boolean key_equal(struct sn_key a, struct sn_key b)
{
    return a.map_identity==b.map_identity && a.overlay_revision==b.overlay_revision &&
        a.checksum==b.checksum && a.bsp_index==b.bsp_index &&
        a.agent_identity==b.agent_identity && a.format_version==b.format_version;
}
static uint64_t name_hash(char const *name)
{
    uint64_t value=UINT64_C(14695981039346656037);
    short i;
    for (i=0; i<256 && name && name[i]; i++)
        value=(value^(unsigned char)name[i])*UINT64_C(1099511628211);
    return value;
}
static uint32_t agent_hash(struct sn_agent agent)
{
    float values[4]={agent.radius,agent.height,agent.minimum_normal_z,agent.projection_height};
    uint32_t hash=2166136261u;
    short i;
    for (i=0;i<4;i++) {
        uint32_t bits;
        memcpy(&bits,&values[i],sizeof(bits));
        hash=(hash^bits)*16777619u;
    }
    return hash;
}
static struct nav_layer *layer(int index)
{
    assert(index>=0 && index<NAV_LAYERS);
    return &world.layers[index];
}
static void layer_reset(struct nav_layer *l)
{
    l->active=-1; l->staging=0;
    l->requested_valid=FALSE; l->status=SN_INVALID;
    l->obstacles++;
    csmemset(l->blocks,0,sizeof(l->blocks));
    csmemset(l->edge_cache,0,sizeof(l->edge_cache));
}
/* (each layer's storage: the foot layer's two buffers, the vehicle's one) */
static void layers_attach(void)
{
    world.layers[NAV_LAYER_FOOT].resources[0]=&foot_resources[0];
    world.layers[NAV_LAYER_FOOT].resources[1]=&foot_resources[1];
    world.layers[NAV_LAYER_VEHICLE].resources[0]=&vehicle_resource;
    world.layers[NAV_LAYER_VEHICLE].resources[1]=&vehicle_resource;
}
void navigation_world_reset(void)
{
    if (world.dump) { fclose(world.dump); world.dump=NULL; }
    layers_attach();
    layer_reset(layer(NAV_LAYER_FOOT));
    layer_reset(layer(NAV_LAYER_VEHICLE));
    world.vehicle_body_valid=FALSE;
    world.open_sky_cursor=0; world.open_sky_roofed=0;
    world.last_tick=NONE;
    world.revision++;
    world.stats_tick=NONE;
}
static const struct sn_resource *layer_resource(const struct nav_layer *l)
{
    const struct sn_resource *r;
    if (!l->requested_valid || l->active<0 || !l->resources[l->active]) return NULL;
    r=l->resources[l->active];
    if (!r->ready || !key_equal(r->key,l->requested)) return NULL;
    /* an editor (Forge) changed the world since it was built */
    if (r->key.overlay_revision!=halo_extensions_world_edit_revision()) return NULL;
    return r;
}
const struct sn_resource *navigation_world_resource(void)
{
    return layer_resource(layer(NAV_LAYER_FOOT));
}
const struct sn_resource *navigation_world_vehicle_resource(void)
{
    return layer_resource(layer(NAV_LAYER_VEHICLE));
}
uint64_t navigation_world_obstacle_revision(void) { return world.layers[NAV_LAYER_FOOT].obstacles; }
bool navigation_world_vehicle_body(float *half_width, float *half_length, float *height)
{
    if (!world.vehicle_body_valid) return false;
    if (half_width) *half_width=world.vehicle_half_width;
    if (half_length) *half_length=world.vehicle_half_length;
    if (height) *height=world.vehicle_height;
    return true;
}

/* Decode only walkable, nonbreakable rings. Never invoke legacy NONE reads.
   Every dereference is preceded by count/index checks; loaded tag pointers have
   already passed the cache loader's address validation. */
static int read_surface(void *context, uint32_t index, struct sn_polygon *out)
{
    struct structure_bsp *structure=global_structure_bsp_get();
    struct collision_bsp *bsp;
    struct collision_surface *surface;
    byte const *flags;
    real_plane3d plane;
    long edge_index;
    uint32_t count;
    (void)context;
    if (!structure || structure->collision_bsp.count!=1) return -1;
    /* port: after the BSP's surfaces, a scenario arena's floor (bot_scenario.h) */
    if (structure->pathfinding_surfaces.count>=0 && index>=(uint32_t)structure->pathfinding_surfaces.count)
        return bot_scenario_navigation_surface(index-(uint32_t)structure->pathfinding_surfaces.count,
            (uint32_t)structure->pathfinding_surfaces.count,out);
    bsp=TAG_BLOCK_GET_ELEMENT(&structure->collision_bsp,0,struct collision_bsp);
    if (index>=(uint32_t)bsp->surfaces.count || index>=(uint32_t)structure->pathfinding_surfaces.count) return -1;
    flags=xbox_pointer(structure->pathfinding_surfaces.address);
    if (!flags) return -1;
    if (!(flags[index]&0x40) || (flags[index]&0x80)) return 0;
    surface=TAG_BLOCK_GET_ELEMENT(&bsp->surfaces,index,struct collision_surface);
    if ((uint32_t)(surface->plane_designator&0x7fffffff)>=(uint32_t)bsp->bsp3d.planes.count) return -1;
    bsp3d_get_plane_from_designator(&bsp->bsp3d,surface->plane_designator,&plane);
    if (!valid_real_plane3d(&plane)) return -1;
    out->normal.x=plane.n.i; out->normal.y=plane.n.j; out->normal.z=plane.n.k;
    /* Retail multiplayer low-bit clearance semantics are not verified. Never
       interpret the unused quantization tables as a guaranteed biped envelope.
       Zero explicitly requires CE pill collision checks at query time. */
    out->width=0.f; out->height=0.f;
    edge_index=surface->first_edge_index;
    for (count=0;count<SN_VERTICES_MAX;count++) {
        struct collision_edge *edge;
        struct collision_vertex *vertex;
        short side;
        long neighbor;
        if (edge_index<0 || edge_index>=bsp->edges.count) return -1;
        edge=TAG_BLOCK_GET_ELEMENT(&bsp->edges,edge_index,struct collision_edge);
        side=edge->surface_indices[1]==(long)index;
        if (edge->surface_indices[side]!=(long)index ||
            edge->vertex_indices[side]<0 || edge->vertex_indices[side]>=bsp->vertices.count) return -1;
        neighbor=edge->surface_indices[!side];
        if (neighbor!=NONE && (neighbor<0 || neighbor>=bsp->surfaces.count)) return -1;
        vertex=TAG_BLOCK_GET_ELEMENT(&bsp->vertices,edge->vertex_indices[side],struct collision_vertex);
        out->vertices[count]=nav_point(vertex->point);
        out->neighbors[count]=neighbor==NONE ? SN_NONE : (uint32_t)neighbor;
        edge_index=edge->edge_indices[side];
        if (edge_index==surface->first_edge_index) {
            out->vertex_count=(uint8_t)(count+1);
            return count>=2 ? 1 : -1;
        }
    }
    return -1;
}
static boolean source_valid(struct structure_bsp *s)
{
    struct collision_bsp *bsp;
    if (!s || s->collision_bsp.count!=1 || !xbox_pointer(s->collision_bsp.address)) return FALSE;
    bsp=TAG_BLOCK_GET_ELEMENT(&s->collision_bsp,0,struct collision_bsp);
    return bsp->surfaces.count>0 && bsp->surfaces.count<=SN_SOURCE_MAX &&
        s->pathfinding_surfaces.count==bsp->surfaces.count &&
        xbox_pointer(s->pathfinding_surfaces.address) && xbox_pointer(bsp->surfaces.address) &&
        bsp->edges.count>0 && bsp->edges.count<=MAXIMUM_EDGES_PER_COLLISION_BSP && xbox_pointer(bsp->edges.address) &&
        bsp->vertices.count>0 && bsp->vertices.count<=MAXIMUM_VERTICES_PER_COLLISION_BSP && xbox_pointer(bsp->vertices.address) &&
        bsp->bsp3d.planes.count>0 && bsp->bsp3d.planes.count<=SN_SOURCE_MAX && xbox_pointer(bsp->bsp3d.planes.address);
}
static void dump_begin(const struct sn_resource *r)
{
    char const *path=config_string("debug.nav_dump");
    size_t length;
    if (!path || !path[0]) return;
    length=strlen(path);
    if (length>=sizeof(world.dump_path)) { platform_log("nav: dump path too long"); return; }
    if (length<4 || strcmp(path+length-4,".nav")) {
        platform_log("nav: diagnostic filename must end in .nav (never overwrite a map/overlay)");
        return;
    }
    if (world.dump && fclose(world.dump)) platform_log("nav: previous dump close failed");
    csstrncpy(world.dump_path,path,sizeof(world.dump_path)-1);
    world.dump_path[sizeof(world.dump_path)-1]=0;
    snprintf(world.temporary_path,sizeof(world.temporary_path),"%s.partial",path);
    world.dump=fopen(world.temporary_path,"w"); world.dump_cursor=0;
    if (!world.dump) { platform_log("nav: cannot open diagnostic dump"); return; }
    fprintf(world.dump,"NAV %u %u %.9g %.9g %.9g %.9g\n",SN_FORMAT_VERSION,r->polygon_count,
        r->agent.radius,r->agent.height,r->agent.minimum_normal_z,r->agent.projection_height);
    fprintf(world.dump,"KEY %llu %u %u %u %llu %llu\n",
        (unsigned long long)r->key.map_identity,r->key.checksum,r->key.bsp_index,
        r->key.agent_identity,(unsigned long long)r->key.overlay_revision,
        (unsigned long long)r->revision);
}
/* At most 64 polygons/file rows per simulation tick. Publish the dump atomically. */
static void dump_step(const struct sn_resource *r)
{
    uint32_t work;
    if (!world.dump || !r) return;
    for (work=0;work<64 && world.dump_cursor<r->polygon_count;work++,world.dump_cursor++) {
        const struct sn_polygon *p=&r->polygons[world.dump_cursor];
        uint32_t edge;
        fprintf(world.dump,"P %u %u %u %.9g %.9g %.9g %.9g %.9g",world.dump_cursor,
            p->source_index,p->vertex_count,p->width,p->height,p->normal.x,p->normal.y,p->normal.z);
        for (edge=0;edge<p->vertex_count;edge++)
            fprintf(world.dump," %.9g %.9g %.9g %u",p->vertices[edge].x,p->vertices[edge].y,p->vertices[edge].z,p->neighbors[edge]);
        fputc('\n',world.dump);
    }
    if (ferror(world.dump)) { fclose(world.dump); world.dump=NULL; platform_log("nav: dump write failed"); return; }
    if (world.dump_cursor==r->polygon_count) {
        int closed=fclose(world.dump); world.dump=NULL;
        if (closed || rename(world.temporary_path,world.dump_path)) platform_log("nav: dump publication failed");
        else platform_log("nav: walkable polygon diagnostic dump complete (%u polygons)",r->polygon_count);
    }
}
/* Start or continue one layer's build for this agent and key. The vehicle
   layer's single buffer is unpublished (active -1) while it builds. */
static void layer_build(struct nav_layer *l, char const *name, struct sn_agent agent)
{
    struct structure_bsp *structure=global_structure_bsp_get();
    struct sn_key key;
    clock_t before;
    key.map_identity=name_hash(cache_file_loaded_map_name()); key.checksum=(uint32_t)cache_files_get_checksum();
    key.bsp_index=(uint32_t)global_structure_bsp_index; key.agent_identity=agent_hash(agent);
    key.format_version=SN_FORMAT_VERSION;
    key.overlay_revision=halo_extensions_world_edit_revision();
    if (!l->requested_valid || !key_equal(key,l->requested)) {
        l->requested=key; l->requested_valid=TRUE; l->cpu_time=0;
        if (l==layer(NAV_LAYER_FOOT) && world.dump) { fclose(world.dump); world.dump=NULL; }
        l->staging=l->active==0 ? 1 : 0;
        if (l->active>=0 && l->resources[l->staging]==l->resources[l->active]) l->active=-1;
        if (!source_valid(structure)) { l->status=SN_INVALID; platform_log("nav: invalid BSP source blocks"); return; }
        /* (source_valid kept the BSP's count within SN_SOURCE_MAX; the arena's
        few polygons follow it only while the total fits) */
        {
            uint32_t sources=(uint32_t)structure->pathfinding_surfaces.count;
            uint32_t extra=bot_scenario_navigation_surface_count();
            if (extra<=SN_SOURCE_MAX-sources) sources+=extra;
            l->status=sn_build_begin(&l->build,l->resources[l->staging],sources,key,++world.revision,agent);
        }
        platform_log("nav: build %s BSP %d, radius %.3f height %.3f normal-k %.3f, source surfaces %ld",
            name,global_structure_bsp_index,agent.radius,agent.height,agent.minimum_normal_z,
            structure->pathfinding_surfaces.count);
    }
    if (l->status==SN_RUNNING) {
        before=clock();
        l->status=sn_build_step(&l->build,l->resources[l->staging],128,read_surface,NULL);
        l->cpu_time+=clock()-before;
        if (l->status==SN_READY) {
            const struct sn_resource *r=l->resources[l->staging];
            uint32_t i,usable=0,largest=0,count=0;
            l->active=l->staging; l->obstacles++;
            csmemset(l->blocks,0,sizeof(l->blocks));
            csmemset(l->edge_cache,0,sizeof(l->edge_cache));
            /* (the largest connected set, counted once: polygon_count steps
            twice, components being their lowest index) */
            for (i=0;i<r->polygon_count;i++) if (r->components[i]!=SN_NONE) usable++;
            for (i=0;i<r->polygon_count;i++) if (r->components[i]==i) {
                uint32_t j,size=0;
                count++;
                if (count>64) continue; /* (the largest of the first 64 sets) */
                for (j=i;j<r->polygon_count;j++) if (r->components[j]==i) size++;
                if (size>largest) largest=size;
            }
            platform_log("nav: published %s %u walkable polygons (%u usable, %u connected sets, largest %u), "
                "BSP %d, CPU %.2f ms, resource %lu bytes",name,r->polygon_count,usable,count,largest,
                global_structure_bsp_index,1000.0*(double)l->cpu_time/CLOCKS_PER_SEC,
                (unsigned long)sizeof(struct sn_resource));
            if (l==layer(NAV_LAYER_FOOT)) dump_begin(r);
            else { world.open_sky_cursor=0; world.open_sky_roofed=0; }
        } else if (l->status!=SN_RUNNING)
            platform_log("nav: %s build failed (%s), no partial publication",name,sn_status_name(l->status));
    }
}
static void update_resource(long unit_index)
{
    struct unit_datum *unit=unit_get(unit_index);
    struct biped_definition *definition=biped_definition_get(unit->definition_index);
    struct nav_layer *l=layer(NAV_LAYER_FOOT);
    struct sn_agent agent;
    agent.radius=definition->biped.collision_radius;
    agent.height=definition->biped.collision_height_standing;
    agent.minimum_normal_z=definition->biped.runtime_minimum_normal_k;
    agent.projection_height=agent.height;
    l->sweep_radius=agent.radius; l->sweep_height=agent.height-2.f*agent.radius; l->sweep_lift=0.f;
    layer_build(l,"foot",agent);
    dump_step(navigation_world_resource());
}
/* A vehicle's body from its physics mass points, in its own frame (x
   forward, y left, z up): the widest side, the longer end and the height they
   span. FALSE for a vehicle without usable physics. Untrusted tag values:
   counts and extents are checked. */
static boolean vehicle_body(long vehicle_index, float *half_width, float *half_length, float *height)
{
    struct object_datum *object=object_try_and_get(vehicle_index);
    struct object_definition *definition;
    struct physics_definition *physics;
    float width=0.f,length=0.f,low=0.f,high=0.f;
    long i;
    if (!object) return FALSE;
    definition=object_definition_get(object->definition_index);
    if (definition->object.physics.index==NONE) return FALSE;
    physics=physics_definition_get(definition->object.physics.index);
    if (physics->mass_points.count<=0 || physics->mass_points.count>NAV_MASS_POINTS_MAXIMUM) return FALSE;
    for (i=0;i<physics->mass_points.count;i++) {
        struct mass_point_definition *point=TAG_BLOCK_GET_ELEMENT(&physics->mass_points,i,struct mass_point_definition);
        float r=point->radius;
        if (!valid_real_point3d(&point->position) || !(r>=0.f && r<NAV_VEHICLE_EXTENT_MAXIMUM) ||
            !(fabsf(point->position.x)<NAV_VEHICLE_EXTENT_MAXIMUM) ||
            !(fabsf(point->position.y)<NAV_VEHICLE_EXTENT_MAXIMUM) ||
            !(fabsf(point->position.z)<NAV_VEHICLE_EXTENT_MAXIMUM)) return FALSE;
        width=MAX(width,fabsf(point->position.y)+r);
        length=MAX(length,fabsf(point->position.x)+r);
        if (!i || point->position.z-r<low) low=point->position.z-r;
        if (!i || point->position.z+r>high) high=point->position.z+r;
    }
    if (!(width>0.05f) || !(length>0.05f) || !(high-low>0.05f)) return FALSE;
    *half_width=width; *half_length=length; *height=high-low;
    return TRUE;
}
/* The widest of the map's ground vehicles a bot can drive (at most
   NAV_VEHICLES_EXAMINED objects): every one fits where it does. */
static boolean find_vehicle_body(void)
{
    struct object_iterator iterator;
    short examined=0;
    boolean found=FALSE;
    float width=0.f,length=0.f,height=0.f;
    object_iterator_new(&iterator,_object_mask_vehicle,0);
    while (examined++<NAV_VEHICLES_EXAMINED && object_iterator_next(&iterator)) {
        float w,l,h;
        if (!vehicle_supports_bot_driver(iterator.index) || !vehicle_body(iterator.index,&w,&l,&h)) continue;
        if (!found || w>width) width=w;
        if (!found || l>length) length=l;
        if (!found || h>height) height=h;
        found=TRUE;
    }
    if (!found) return FALSE;
    world.vehicle_half_width=width; world.vehicle_half_length=length; world.vehicle_height=height;
    return TRUE;
}
/* At most NAV_OPEN_SKY_RAYS_PER_TICK rays straight up from the vehicle
   layer's polygon centers: a roof (the structure, either face) within
   NAV_OPEN_SKY_HEIGHT marks a place inside a building, which a driver does
   not drive to. */
static void update_open_sky(void)
{
    const struct sn_resource *r=navigation_world_vehicle_resource();
    struct collision_result collision;
    uint32_t work;
    if (!r || world.open_sky_cursor>=r->polygon_count) return;
    for (work=0;work<NAV_OPEN_SKY_RAYS_PER_TICK && world.open_sky_cursor<r->polygon_count;work++) {
        uint32_t i=world.open_sky_cursor++;
        real_point3d from;
        real_vector3d up;
        from.x=r->polygons[i].center.x; from.y=r->polygons[i].center.y; from.z=r->polygons[i].center.z+0.5f;
        up.i=0.f; up.j=0.f; up.k=NAV_OPEN_SKY_HEIGHT;
        world.open_sky[i]=collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit)|
            FLAG(_collision_test_back_facing_surfaces_bit)|FLAG(_collision_test_structure_bit),
            &from,&up,NONE,&collision) ? 2 : 1;
        if (world.open_sky[i]==2) world.open_sky_roofed++;
    }
    if (world.open_sky_cursor==r->polygon_count)
        platform_log("nav: vehicle layer open sky over %u of %u polygons",
            r->polygon_count-world.open_sky_roofed,r->polygon_count);
}
int navigation_world_vehicle_open_sky(uint32_t polygon)
{
    const struct sn_resource *r=navigation_world_vehicle_resource();
    if (!r || polygon>=r->polygon_count || polygon>=world.open_sky_cursor) return -1;
    return world.open_sky[polygon]==1;
}
static void update_vehicle_resource(void)
{
    struct nav_layer *l=layer(NAV_LAYER_VEHICLE);
    struct sn_agent agent;
    if (!navigation_world_resource()) return;
    if (!world.vehicle_body_valid) {
        if (!find_vehicle_body()) return;
        world.vehicle_body_valid=TRUE;
        platform_log("nav: vehicle body half-width %.2f half-length %.2f height %.2f",
            world.vehicle_half_width,world.vehicle_half_length,world.vehicle_height);
    }
    agent.radius=NAV_VEHICLE_TOPOLOGY_RADIUS;
    agent.height=world.vehicle_height;
    agent.minimum_normal_z=NAV_VEHICLE_MINIMUM_NORMAL_Z;
    agent.projection_height=MAX(world.vehicle_height,1.f);
    l->sweep_radius=world.vehicle_half_width;
    l->sweep_lift=MIN(NAV_VEHICLE_SWEEP_LIFT_MAXIMUM,0.5f*world.vehicle_half_width);
    l->sweep_height=MAX(0.f,world.vehicle_height-l->sweep_lift-2.f*l->sweep_radius);
    layer_build(l,"vehicle",agent);
    update_open_sky();
}
void navigation_world_update_for_players(void)
{
    struct data_iterator iterator;
    struct player_datum *player;
    long now=game_time_get();
    short i,j;
    char const *dump=config_string("debug.nav_dump");
    /* The ordinary bots now use this BSP resource too. Keep it dormant when
     * bots and diagnostics are all off so the default build does no mesh work. */
    if (!config_boolean("debug.nav_probe") && (!dump || !dump[0]) &&
        config_integer("bots.count") <= 0 && !bots_navigation_requested()) return;
    if (network_game_distributed_client() || (game_connection()!=_game_connection_local &&
        game_connection()!=_game_connection_network_server)) return;
    if (world.last_tick==now) return;
    layers_attach();
    if (world.last_tick!=NONE && now<world.last_tick) navigation_world_reset();
    world.last_tick=now;
    for (j=0;j<NAV_LAYERS;j++) for (i=0;i<NAV_BLOCKS;i++) if (world.layers[j].blocks[i].until &&
        now>=world.layers[j].blocks[i].until) {
        world.layers[j].blocks[i].until=0; world.layers[j].obstacles++;
    }
    /* one line every ten seconds: the cost of route searches' collision sweeps */
    if (world.stats_tick==NONE) world.stats_tick=now;
    else if (now-world.stats_tick>=10*TICKS_PER_SECOND) {
        for (j=0;j<NAV_LAYERS;j++) {
            struct nav_layer *l=layer(j);
            if (l->sweeps!=l->stats_sweeps || l->cache_hits!=l->stats_hits)
                platform_log("nav: %s search sweeps %lu, cached edge verdicts %lu (last 10 s)",
                    j==NAV_LAYER_FOOT ? "foot" : "vehicle",l->sweeps-l->stats_sweeps,l->cache_hits-l->stats_hits);
            l->stats_sweeps=l->sweeps; l->stats_hits=l->cache_hits;
        }
        world.stats_tick=now;
    }
    data_iterator_new(&iterator,player_data);
    while ((player=data_iterator_next(&iterator))!=NULL) {
        if (player->unit_index!=NONE && object_get(player->unit_index)->object.type==_object_type_biped) {
            update_resource(player->unit_index);
            break;
        }
    }
    /* (after the foot layer is published: one build's work per tick) */
    update_vehicle_resource();
}
static void layer_block(struct nav_layer *l, const struct sn_resource *r, uint32_t polygon)
{
    short i,slot=NONE;
    long now=game_time_get();
    struct nav_block *block;
    if (!r || polygon>=r->polygon_count) return;
    for (i=0;i<NAV_BLOCKS;i++) {
        block=&l->blocks[i];
        if ((block->until || now<block->forget) && block->polygon==polygon) { slot=i; break; }
        if (slot==NONE && !block->until && now>=block->forget) slot=i;
    }
    if (slot==NONE) {
        platform_log("nav: temporary blockage capacity exhausted; wait rather than corrupt topology");
        return;
    }
    block=&l->blocks[slot];
    if (block->polygon==polygon && (block->until || now<block->forget))
        block->strikes=(short)MIN(block->strikes+1,NAV_BLOCK_STRIKES_MAX);
    else block->strikes=0;
    block->polygon=polygon;
    block->until=now+(2*TICKS_PER_SECOND<<block->strikes);
    block->forget=block->until+NAV_BLOCK_MEMORY_TICKS;
    l->obstacles++;
    if (block->strikes)
        platform_log("nav: polygon %lu blocked again; %ld s",(unsigned long)polygon,2L<<block->strikes);
}
void navigation_world_block(uint32_t polygon)
{
    layer_block(layer(NAV_LAYER_FOOT),navigation_world_resource(),polygon);
}
void navigation_world_vehicle_block(uint32_t polygon)
{
    layer_block(layer(NAV_LAYER_VEHICLE),navigation_world_vehicle_resource(),polygon);
}
static bool layer_polygon_blocked(const struct nav_layer *l, uint32_t polygon)
{
    short i;
    for (i=0;i<NAV_BLOCKS;i++) if (l->blocks[i].until && l->blocks[i].polygon==polygon) return true;
    return false;
}
bool navigation_world_polygon_blocked(uint32_t polygon)
{
    return layer_polygon_blocked(layer(NAV_LAYER_FOOT),polygon);
}
bool navigation_world_vehicle_polygon_blocked(uint32_t polygon)
{
    return layer_polygon_blocked(layer(NAV_LAYER_VEHICLE),polygon);
}
/* The layer's pill swept from a to b. CE's pill base is the lower sphere
   CENTER; its height is the distance between sphere centers, not total
   standing height (biped_get_physics_pill). Raised by radius/normal-k to stay
   tangent to sloping ground, and by the layer's lift. */
static bool sweep_clear(const struct nav_layer *l, const struct sn_resource *r, long flags, long ignore,
    struct sn_point a, uint32_t ap, struct sn_point b, uint32_t bp)
{
    real_point3d from,clipped;
    real_vector3d delta,clipped_velocity;
    struct collision_plane planes[8];
    float target_z;
    from.x=a.x; from.y=a.y;
    from.z=a.z+l->sweep_radius/r->polygons[ap].normal.z+l->sweep_lift+0.02f;
    target_z=b.z+l->sweep_radius/r->polygons[bp].normal.z+l->sweep_lift+0.02f;
    delta.i=b.x-a.x; delta.j=b.y-a.y; delta.k=target_z-from.z;
    collision_move_pill(flags,&from,&delta,l->sweep_height,l->sweep_radius,ignore,
        &clipped,&clipped_velocity,8,planes);
    return fabsf(clipped.x-b.x)<0.08f && fabsf(clipped.y-b.y)<0.08f &&
        fabsf(clipped.z-target_z)<0.15f;
}
static bool blocked_pair(const struct nav_layer *l, uint32_t ap, uint32_t bp)
{
    short i;
    for (i=0;i<NAV_BLOCKS;i++) if (l->blocks[i].until &&
        (l->blocks[i].polygon==ap || l->blocks[i].polygon==bp)) return true;
    return false;
}
bool navigation_world_segment_clear(void *context, struct sn_point a, uint32_t ap,
    struct sn_point b, uint32_t bp)
{
    const struct nav_layer *l=layer(NAV_LAYER_FOOT);
    const struct sn_resource *r=navigation_world_resource();
    if (!context || !r || ap>=r->polygon_count || bp>=r->polygon_count) return false;
    if (blocked_pair(l,ap,bp)) return false;
    return sweep_clear(l,r,_collision_test_for_bipeds_living_flags,*(long *)context,a,ap,b,bp);
}
/* Following: vehicles and machines only. The ground was checked by the route
   search, and a straight sweep from the bot to its waypoint across uneven
   terrain reads a hill as an obstacle; other bipeds are avoided by the bot's
   own steering, which probes around them every tick. */
static bool layer_occupant_clear(int index, const struct sn_resource *r, void *context,
    struct sn_point a, uint32_t ap, struct sn_point b, uint32_t bp)
{
    const struct nav_layer *l=layer(index);
    long const flags=FLAG(_collision_test_objects_bit)|FLAG(_collision_test_objects_vehicles_bit)|
        FLAG(_collision_test_objects_machines_bit);
    if (!context || !r || ap>=r->polygon_count || bp>=r->polygon_count) return false;
    if (blocked_pair(l,ap,bp)) return false;
    return sweep_clear(l,r,flags,*(long *)context,a,ap,b,bp);
}
bool navigation_world_occupant_clear(void *context, struct sn_point a, uint32_t ap,
    struct sn_point b, uint32_t bp)
{
    return layer_occupant_clear(NAV_LAYER_FOOT,navigation_world_resource(),context,a,ap,b,bp);
}
bool navigation_world_vehicle_occupant_clear(void *context, struct sn_point a, uint32_t ap,
    struct sn_point b, uint32_t bp)
{
    return layer_occupant_clear(NAV_LAYER_VEHICLE,navigation_world_vehicle_resource(),context,a,ap,b,bp);
}
/* A short ascent on an existing corridor, never an inferred island link.
   Use the body's current momentum rather than assuming immediate run speed. */
bool navigation_world_jump_clear(long unit_index, struct sn_point start, struct sn_point goal)
{
    const struct sn_resource *r=navigation_world_resource();
    struct biped_datum *biped=biped_get(unit_index);
    struct biped_definition *definition=biped_definition_get(biped->definition_index);
    struct sn_jump_arc arc;
    struct sn_point landing;
    real_point3d base,from,clipped;
    real_vector3d delta,velocity;
    struct collision_plane planes[8];
    real height,radius,offset;
    uint32_t polygon,tick;
    float dx=goal.x-start.x,dy=goal.y-start.y,rise=goal.z-start.z;
    if (!r || TEST_FLAG(biped->biped.flags,_biped_airborne_bit) ||
        biped->biped.landing==1 || biped->unit.body_stun>0.f ||
        fabsf(biped->object.up.i)>0.01f || fabsf(biped->object.up.j)>0.01f ||
        biped->object.up.k<0.99f || rise<0.3f || rise>1.6f || dx*dx+dy*dy>6.25f) return false;
    if (sn_project(r,goal,&polygon,&landing)!=SN_FOUND ||
        fabsf(landing.z-goal.z)>0.15f || navigation_world_polygon_blocked(polygon)) return false;
    if (!sn_jump_candidate(start,goal,biped->object.translational_velocity.i,
        biped->object.translational_velocity.j,definition->biped.jump_velocity,
        global_gravity,0.25f,&arc)) return false;
    biped_get_physics_pill(unit_index,&base,&height,&radius);
    offset=base.z-start.z+0.02f;
    from=base; from.z+=0.02f;
    for (tick=1; tick<=arc.ticks; tick++) {
        struct sn_point p=arc.points[tick];
        delta.i=p.x-from.x; delta.j=p.y-from.y; delta.k=p.z+offset-from.z;
        collision_move_pill(_collision_test_for_bipeds_living_flags,&from,&delta,
            height,radius,unit_index,&clipped,&velocity,8,planes);
        if (fabsf(clipped.x-p.x)>0.05f || fabsf(clipped.y-p.y)>0.05f ||
            fabsf(clipped.z-p.z-offset)>0.1f) return false;
        from=clipped;
    }
    return true;
}
/* Which edge of polygon p has this midpoint; SN_VERTICES_MAX if none. */
static uint32_t portal_edge(const struct sn_polygon *p, struct sn_point portal)
{
    uint32_t e;
    for (e=0;e<p->vertex_count;e++) {
        struct sn_point u=p->vertices[e],v=p->vertices[(e+1)%p->vertex_count];
        float x=(u.x+v.x)*0.5f-portal.x,y=(u.y+v.y)*0.5f-portal.y,z=(u.z+v.z)*0.5f-portal.z;
        if (x*x+y*y+z*z<0.0001f) return e;
    }
    return SN_VERTICES_MAX;
}
/* Route search: the structure and scenery only, never units, vehicles or
   machines, which move. A route is a claim about the map; occupants are the
   follower's live check (navigation_world_occupant_clear), which waits and,
   if a bot stays stuck, blocks a polygon here. The canonical halves of an
   edge (center->portal, portal->center) are swept once per resource and the
   verdict cached; segments to or from a start or goal point are swept each time. */
static bool layer_search_clear(int index, const struct sn_resource *r, void *context,
    struct sn_point a, uint32_t ap, struct sn_point b, uint32_t bp)
{
    struct nav_layer *l=layer(index);
    long const flags=_collision_test_for_bipeds_living_flags &
        ~(FLAG(_collision_test_objects_bipeds_bit)|FLAG(_collision_test_objects_vehicles_bit)|
        FLAG(_collision_test_objects_machines_bit));
    const struct sn_polygon *p;
    uint32_t polygon=SN_NONE,edge=SN_VERTICES_MAX,shift=0;
    bool clear;
    if (!context || !r || ap>=r->polygon_count || bp>=r->polygon_count) return false;
    p=&r->polygons[ap];
    /* A block on the polygon the bot stands in must not keep it from leaving:
       the segment from the start point to one of its own portals ignores it. */
    if (!(ap==bp && !(a.x==p->center.x && a.y==p->center.y && a.z==p->center.z) &&
        portal_edge(p,b)<SN_VERTICES_MAX && portal_edge(p,a)==SN_VERTICES_MAX) &&
        blocked_pair(l,ap,bp)) return false;
    if (ap==bp) {
        if (a.x==p->center.x && a.y==p->center.y && a.z==p->center.z) {
            polygon=ap; edge=portal_edge(p,b); shift=0;
        } else if (b.x==p->center.x && b.y==p->center.y && b.z==p->center.z) {
            polygon=ap; edge=portal_edge(p,a); shift=2;
        }
    }
    if (edge<SN_VERTICES_MAX) {
        uint8_t state=(uint8_t)((l->edge_cache[polygon][edge]>>shift)&3);
        if (state) { l->cache_hits++; return state==1; }
    }
    l->sweeps++;
    clear=sweep_clear(l,r,flags,*(long *)context,a,ap,b,bp);
    if (edge<SN_VERTICES_MAX)
        l->edge_cache[polygon][edge]|=(uint8_t)((clear ? 1 : 2)<<shift);
    return clear;
}
bool navigation_world_search_clear(void *context, struct sn_point a, uint32_t ap,
    struct sn_point b, uint32_t bp)
{
    return layer_search_clear(NAV_LAYER_FOOT,navigation_world_resource(),context,a,ap,b,bp);
}
bool navigation_world_vehicle_search_clear(void *context, struct sn_point a, uint32_t ap,
    struct sn_point b, uint32_t bp)
{
    return layer_search_clear(NAV_LAYER_VEHICLE,navigation_world_vehicle_resource(),context,a,ap,b,bp);
}

#endif /* HALO_FEATURE_BOTS */
