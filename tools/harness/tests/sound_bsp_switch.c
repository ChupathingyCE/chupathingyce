/* The real sound_reconnect_to_structure_bsp and track_loop_track_sound across
a structure BSP switch, with frames ordered as sound_render orders them, and
the real check compute_sound_obstruction makes before it reads a cluster
(sound_obstruction_clusters_valid). A looping sound refreshed in a frame
without sound_render still matches the manager's flip flop after the switch,
so it is kept; its track copies its location (refresh_sounds) and the
obstruction reads the cluster in the new structure's tables. Points name
their cluster by their x: cluster x when the structure has it, else none. */
#include "harness.h"
#include "types.inc"
struct structure_bsp { struct tag_block clusters; };
static struct structure_bsp structure;
static struct sound_manager_globals sound_manager_globals;
static struct data_array *sound_data, *looping_sound_data;
static void *datum_try_and_get(struct data_array *data, long index) {
 struct datum_header *header;
 if (index==NONE || DATUM_INDEX_TO_ABSOLUTE_INDEX(index)>=data->maximum_count) return NULL;
 header=datum_get(data,index);
 return header->identifier && header->identifier==(short)(index>>16) ? header : NULL;
}
static long data_next_index(struct data_array *data,long previous) {
 long i;
 for(i=previous==NONE?0:DATUM_INDEX_TO_ABSOLUTE_INDEX(previous)+1;i<data->maximum_count;i++) {
  struct datum_header *header=(void *)(data->data+i*data->size);
  if(header->identifier) return ((long)(unsigned short)header->identifier<<16)|i;
 }
 return NONE;
}
static void scenario_location_from_point(struct location *location, real_point3d const *point) {
 location->leaf_index=NONE;
 location->cluster_index=point->x>=0.f && point->x<(real)structure.clusters.count ? (short)point->x : NONE;
}
static struct structure_bsp *global_structure_bsp_get(void) { return &structure; }
#define sound_get(i) ((struct sound_datum *)datum_get(sound_data,(i)))
#define looping_sound_get(i) ((struct looping_sound_datum *)datum_get(looping_sound_data,(i)))
#define match_warn(file, line, condition) ((void)0)
#include "under_test.inc"
static long loop_new(real x) {
 long index=datum_new(looping_sound_data);
 struct looping_sound_datum *loop=looping_sound_get(index);
 loop->source.spatialization_mode=_sound_spatialization_mode_absolute;
 loop->source.location.position.x=x;
 scenario_location_from_point(&loop->source.location.game_location,&loop->source.location.position);
 loop->flip_flop=sound_manager_globals.flip_flop;
 return index;
}
static long track_new(long loop_index) {
 long index=datum_new(sound_data);
 struct sound_datum *sound=sound_get(index);
 sound->type=_sound_loop_track; sound->source_identifier=loop_index; sound->playing_channel_index=NONE;
 sound->track_proc=track_loop_track_sound;
 return index;
}
/* the structure switch: a new structure, then its reconnect procs */
static void switch_structure(short cluster_count) {
 structure.clusters.count=cluster_count;
 sound_reconnect_to_structure_bsp();
}
/* sound_render: the looping sounds not refreshed since the last one are
deleted, each track copies its looping sound, and an audible sound's
obstruction reads its cluster, as compute_sound_obstruction does (the
camera in cluster 0) */
static void render(void) {
 long i;
 for(i=data_next_index(looping_sound_data,NONE);i!=NONE;i=data_next_index(looping_sound_data,i))
  if(looping_sound_get(i)->flip_flop!=sound_manager_globals.flip_flop) datum_delete(looping_sound_data,i);
 for(i=data_next_index(sound_data,NONE);i!=NONE;i=data_next_index(sound_data,i)) {
  struct sound_datum *sound=sound_get(i);
  short cluster;
  if(!sound->track_proc(sound->source_identifier,NULL,&sound->source)) { datum_delete(sound_data,i); continue; }
  cluster=sound->source.location.game_location.cluster_index;
  if(cluster!=NONE && sound_obstruction_clusters_valid(cluster,0))
   CHECK(cluster<structure.clusters.count,"cluster %d read in a structure of %ld clusters",cluster,(long)structure.clusters.count);
 }
 sound_manager_globals.flip_flop=!sound_manager_globals.flip_flop;
}
int main(int argc,char **argv) {
 const char *case_name;
 long loop_index,track_index;
 CHECK(argc==2,"case name"); case_name=argv[1];
 sound_manager_globals.initialized=TRUE; sound_manager_globals.enabled=TRUE;
 sound_data=game_state_data_new("sounds",8,sizeof(struct sound_datum));
 looping_sound_data=game_state_data_new("looping sounds",4,sizeof(struct looping_sound_datum));
 structure.clusters.count=35;
 render();
 /* a loop in cluster 20 refreshed in a frame without sound_render, then a
 switch into a structure of two clusters, whose first frame does not
 refresh it (its object is out of the map) */
 CASE("kept-loop") {
  loop_index=loop_new(20.f); track_index=track_new(loop_index);
  switch_structure(2);
  render();
  CHECK(datum_try_and_get(looping_sound_data,loop_index),"the loop kept (its flip flop matched)");
  CHECK(looping_sound_get(loop_index)->source.location.game_location.cluster_index==NONE,"the loop has no cluster");
  CHECK(sound_get(track_index)->source.location.game_location.cluster_index==NONE,"its track has none");
  return 0;
 }
 /* a loop whose point is in both structures has the new one's cluster */
 CASE("loop-in-both") {
  loop_index=loop_new(1.f); track_index=track_new(loop_index);
  looping_sound_get(loop_index)->source.location.game_location.cluster_index=7;
  switch_structure(2);
  render();
  CHECK(sound_get(track_index)->source.location.game_location.cluster_index==1,"the new structure's cluster");
  return 0;
 }
 /* a sound's location made again, as before */
 CASE("sound") {
  long sound_index=datum_new(sound_data);
  struct sound_datum *sound=sound_get(sound_index);
  sound->source.spatialization_mode=_sound_spatialization_mode_absolute;
  sound->source.location.position.x=20.f; sound->source.location.game_location.cluster_index=20;
  switch_structure(2);
  CHECK(sound->source.location.game_location.cluster_index==NONE,"the sound has no cluster");
  return 0;
 }
 /* a location the reconnect missed is not read in the cluster tables */
 CASE("stale-cluster") {
  structure.clusters.count=2;
  CHECK(sound_obstruction_clusters_valid(1,0) && sound_obstruction_clusters_valid(0,1),"the structure's clusters are read");
  CHECK(!sound_obstruction_clusters_valid(2,0),"a source's cluster past the structure's is not");
  CHECK(!sound_obstruction_clusters_valid(0,20),"nor the camera's");
  return 0;
 }
 CHECK(FALSE,"unknown case");
 return 1;
}
