#include "harness.h"
/* the real players_coop_unit_stranded and the tests it makes, against units
placed in or out of the loaded BSP, on foot or riding */
enum {_biped_airborne_bit = 0};
enum {_object_mask_biped = 1, _object_mask_unit = 2};
enum {_collision_test_front_facing_surfaces_bit = 0, _collision_test_objects_bit = 7};
enum {_collision_test_objects_all_types_flags = 0xFFF00, _collision_user_bipeds = 7, MAXIMUM_COLLISION_USER_STACK_DEPTH = 32};
struct collision_result {long object_index;};
static short global_current_collision_users[MAXIMUM_COLLISION_USER_STACK_DEPTH];
static short global_current_collision_user_depth;
struct biped_data {unsigned long flags; long elevator_object_index;};
struct unit_data {long driver_object_index; long player_index;};
struct location {long cluster_index;};
struct object_data {struct location location; long parent_object_index; real_point3d position;};
struct biped_datum {struct object_data object; struct unit_data unit; struct biped_data biped; int is_biped;};
static struct biped_datum objects[4];
static long object_get_ultimate_parent(long i){while(objects[i].object.parent_object_index!=NONE)i=objects[i].object.parent_object_index;return i;}
static struct biped_datum *object_get(long i){return &objects[i];}
static struct biped_datum *unit_get(long i){return &objects[i];}
static struct biped_datum *biped_try_and_get(long i){return i!=NONE&&objects[i].is_biped?&objects[i]:NULL;}
static void *object_try_and_get_and_verify_type(long i,int mask){(void)mask;return i!=NONE?&objects[i]:NULL;}
/* an object under the unit's feet, or none (the void): what the host's
look straight down finds */
static long object_underfoot = NONE;
static int downward_tests;
static boolean collision_test_vector(unsigned long flags,real_point3d const *p,real_vector3d const *v,long ignore,struct collision_result *c)
{
	CHECK(TEST_FLAG(flags,_collision_test_objects_bit),"the look down must find objects");
	CHECK(v->i==0.0f&&v->j==0.0f&&v->k<0.0f&&p->z>objects[ignore].object.position.z&&p->z+v->k<objects[ignore].object.position.z,"it looks straight down, through the feet");
	CHECK(global_current_collision_user_depth==1&&global_current_collision_users[0]==_collision_user_bipeds,"the collision user is pushed");
	downward_tests++;
	c->object_index=object_underfoot;
	return object_underfoot!=NONE;
}
#include "under_test.inc"

/* unit 0, a player's biped: in the BSP (cluster 3) or out of it, on foot */
static void on_foot(int inside,int airborne,long elevator)
{
 object_underfoot=NONE;
 for(int i=0;i<4;i++){objects[i].object.parent_object_index=NONE;objects[i].object.location.cluster_index=NONE;objects[i].biped.elevator_object_index=NONE;objects[i].unit.driver_object_index=NONE;objects[i].unit.player_index=NONE;}
 objects[0].is_biped=1;objects[0].unit.player_index=0;
 objects[0].object.location.cluster_index=inside?3:NONE;
 objects[0].biped.flags=airborne?FLAG(_biped_airborne_bit):0;
 objects[0].biped.elevator_object_index=elevator;
}

int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";
 CASE("inside") {on_foot(1,0,NONE);CHECK(!players_coop_unit_stranded(0),"a player standing in the BSP is not stranded");}
 else CASE("falling") {on_foot(0,1,NONE);CHECK(players_coop_unit_stranded(0),"a player falling outside the BSP is stranded");}
 else CASE("standing-in-void") {on_foot(0,0,NONE);CHECK(players_coop_unit_stranded(0),"a player left standing outside the BSP (the last one's floor) is stranded");}
 else CASE("elevator-shaft") {on_foot(0,0,2);objects[2].is_biped=0;CHECK(!players_coop_unit_stranded(0),"a player riding an elevator through its shaft is left alone");}
 else CASE("elevator-airborne") {on_foot(0,1,2);CHECK(players_coop_unit_stranded(0),"a player falling outside the BSP is stranded, even one an elevator dropped a moment ago");}
 else CASE("on-object") {on_foot(0,0,NONE);object_underfoot=3;CHECK(!players_coop_unit_stranded(0)&&downward_tests==1,"a player standing on an object outside the BSP (a vehicle's roof, scenery, a machine) is left alone");CHECK(global_current_collision_user_depth==0,"the collision user is popped");}
 else CASE("ai-pelican") {on_foot(0,0,NONE);objects[0].object.parent_object_index=1;objects[1].unit.driver_object_index=2;objects[2].unit.player_index=NONE;CHECK(!players_coop_unit_stranded(0),"a player in an AI-flown vehicle outside the BSP is left alone");}
 else CASE("player-vehicle") {on_foot(0,0,NONE);objects[0].object.parent_object_index=1;objects[1].unit.driver_object_index=0;CHECK(players_coop_unit_stranded(0),"a player-driven vehicle outside the BSP is stranded");}
 else return 2;
 return 0;
}
