/* A co-op client's activating cluster, the host's (objects_port_set_activating_cluster,
   objects_get_activating_cluster_index): one taken before this machine's
   structure BSP switch is not used after it, where it would name a cluster
   past the end of the new BSP's (a10's skipped opening cutscene: a halt in
   structure_bsp_get_cluster_pvs in a debug build). A cluster of this
   machine's own scripts is kept as it was. */
#include "harness.h"
#include "config.inc"
#pragma clang diagnostic ignored "-Wint-in-bool-context"

struct tag_block { long count; };
struct structure_bsp { struct tag_block clusters; };
struct object_header_datum { short type; void *datum; };
struct object_datum { struct { unsigned long flags; struct { short cluster_index; } location; } object; };
struct location { short cluster_index; };
struct scenario_cutscene_camera_point { const char *name; real_point3d position; };
struct scenario { int unused; };
struct object_globals_datum
{
	short pvs_activation_type;
	union { long object_index; short cluster_index; } pvs_activation;
};

static struct object_globals_datum globals;
static struct object_globals_datum *object_globals = &globals;
static struct structure_bsp bsps[2] = { { { 40 } }, { { 2 } } };
static short current_bsp;
static short camera_point_cluster;

static struct structure_bsp *global_structure_bsp_get(void) { return &bsps[current_bsp]; }
static short global_structure_bsp_index_get(void) { return current_bsp; }
static struct object_header_datum *object_header_try_and_get(long index) { return NULL; }
static struct object_datum *object_get(long index) { return NULL; }
static long object_get_ultimate_parent(long index) { return index; }
static struct scenario *global_scenario_get(void) { return NULL; }
static struct scenario_cutscene_camera_point camera_point;
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&camera_point)
static void scenario_location_from_point(struct location *location, real_point3d const *point)
{
	location->cluster_index = camera_point_cluster;
}
static void error(int level, const char *text, ...) { }
#define _error_silent 0
#define _object_connected_to_map_bit 0

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("same-bsp")
	{
		objects_port_set_activating_cluster(30);
		CHECK(objects_get_activating_cluster_index() == 30, "the host's cluster was not used");
	}
	CASE("switched-after")
	{
		/* taken in the 40-cluster BSP, then this machine switches to the 2-cluster one */
		objects_port_set_activating_cluster(30);
		current_bsp = 1;
		CHECK(objects_get_activating_cluster_index() == NONE, "cluster %d used in a BSP of %ld clusters",
			objects_get_activating_cluster_index(), bsps[1].clusters.count);
		objects_port_set_activating_cluster(1);
		CHECK(objects_get_activating_cluster_index() == 1, "the host's next cluster was not used");
	}
	CASE("own-script")
	{
		/* a scripted camera point's cluster, after a host's was taken: kept across a switch as before */
		objects_port_set_activating_cluster(30);
		camera_point_cluster = 1;
		object_pvs_set_camera_point(0);
		current_bsp = 1;
		CHECK(objects_get_activating_cluster_index() == 1, "this machine's own cluster was dropped");
	}
	return 0;
}
