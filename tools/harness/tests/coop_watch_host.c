#include "harness.h"
#include <math.h>
/* the real client_watch_host_from_behind (network_coop.c), against a world
with or without a wall behind the host */
#define MAX(a,b) ((a)>(b)?(a):(b))
enum {_collision_test_front_facing_surfaces_bit, _collision_test_structure_bit};
enum {_collision_user_observer = 12};
struct collision_result {real t;};
static short global_current_collision_users[8];
static short global_current_collision_user_depth;
static real wall_t = 2.0f;
static int tests;
static struct
{
	boolean watching_host_valid;
	real_point3d watching_host_position;
	real_vector3d watching_host_forward;
} coop_presentation;
static real magnitude3d(real_vector3d const *v){return sqrtf(v->i*v->i+v->j*v->j+v->k*v->k);}
static real normalize3d(real_vector3d *v){real m=magnitude3d(v);if(m>0){v->i/=m;v->j/=m;v->k/=m;}return m;}
static void vector_from_points3d(real_point3d const *a,real_point3d const *b,real_vector3d *v){v->i=b->x-a->x;v->j=b->y-a->y;v->k=b->z-a->z;}
static void point_from_line3d(real_point3d const *p,real_vector3d const *v,real t,real_point3d *out){real_point3d r={p->x+v->i*t,p->y+v->j*t,p->z+v->k*t};*out=r;}
static void points_interpolate(real_point3d const *a,real_point3d const *b,real t,real_point3d *out){real_point3d r={a->x+(b->x-a->x)*t,a->y+(b->y-a->y)*t,a->z+(b->z-a->z)*t};*out=r;}
static void vectors_interpolate(real_vector3d const *a,real_vector3d const *b,real t,real_vector3d *out){real_vector3d r={a->i+(b->i-a->i)*t,a->j+(b->j-a->j)*t,a->k+(b->k-a->k)*t};*out=r;}
static boolean distributed_axes_make_valid(real_vector3d *f,real_vector3d *u){(void)f;(void)u;return TRUE;}
static void observer_up_from_forward(real_vector3d const *f,real_vector3d *u){(void)f;(void)u;}
static boolean valid_real_vector3d_axes2(real_vector3d const *f,real_vector3d const *u){(void)f;(void)u;return TRUE;}
/* a wall across the line at wall_t of the way from the host's eyes (none past 1) */
static boolean collision_test_vector(unsigned long flags,real_point3d const *p,real_vector3d const *v,long ignore,struct collision_result *c)
{
	(void)p;(void)v;(void)ignore;
	CHECK(TEST_FLAG(flags,_collision_test_structure_bit),"the camera must stop at the level's walls");
	CHECK(global_current_collision_user_depth==1&&global_current_collision_users[0]==_collision_user_observer,"the test is the camera's");
	tests++;
	if(wall_t>1.0f)return FALSE;
	c->t=wall_t;return TRUE;
}
#include "under_test.inc"

static real distance_to_eyes(real_point3d const *camera)
{
	real_vector3d v;real_point3d eyes={0,0,0};
	vector_from_points3d(&eyes,camera,&v);return magnitude3d(&v);
}

int main(int argc,char **argv)
{
	const char *case_name=argc>1?argv[1]:"";
	real_point3d position={0,0,0};real_vector3d forward={1,0,0},up={0,0,1};
	CASE("open")
	{
		wall_t=2.0f;
		CHECK(client_watch_host_from_behind(&position,&forward,&up),"the view is usable");
		CHECK(tests==1&&fabsf(position.x+WATCH_HOST_DISTANCE)<0.001f&&fabsf(position.z-WATCH_HOST_HEIGHT)<0.001f,"with nothing behind the host, the camera is behind and above them");
		CHECK(global_current_collision_user_depth==0,"the collision user is popped");
	}
	else CASE("wall")
	{
		real full=sqrtf(WATCH_HOST_DISTANCE*WATCH_HOST_DISTANCE+WATCH_HOST_HEIGHT*WATCH_HOST_HEIGHT);
		wall_t=0.5f;
		CHECK(client_watch_host_from_behind(&position,&forward,&up),"the view is usable");
		CHECK(fabsf(distance_to_eyes(&position)-(full*0.5f-WATCH_HOST_WALL_GAP))<0.001f,"a wall behind the host keeps the camera in front of it (%.3f)",distance_to_eyes(&position));
	}
	else CASE("wall-at-eyes")
	{
		wall_t=0.01f;
		CHECK(client_watch_host_from_behind(&position,&forward,&up),"the view is usable");
		CHECK(distance_to_eyes(&position)<0.001f,"a wall right behind the host puts the camera at their eyes, never behind the wall");
	}
	else CASE("eased")
	{
		/* the eased spot is what the wall test checks: easing out from the
		host into a wall stops at it each tick */
		int tick;
		wall_t=2.0f;
		client_watch_host_from_behind(&position,&forward,&up);
		wall_t=0.5f;
		for(tick=0;tick<10;tick++){real_point3d eyes={0,0,0};real_vector3d f={1,0,0},u={0,0,1};client_watch_host_from_behind(&eyes,&f,&u);position=eyes;}
		CHECK(distance_to_eyes(&position)<sqrtf(WATCH_HOST_DISTANCE*WATCH_HOST_DISTANCE+WATCH_HOST_HEIGHT*WATCH_HOST_HEIGHT)*0.5f,"the eased camera stays in front of the wall");
	}
	else return 2;
	return 0;
}
