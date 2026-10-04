/*
BOT_INTERNAL.H

What the bot modules share: bots.c (the bots: what to do) and
bot_move.c (movement over the area awareness system: how to get
there).
*/

#ifndef __BOT_INTERNAL_H
#define __BOT_INTERNAL_H

#include "bot_port.h"
#include "bot_nav.h"

#define BOT_PI 3.14159265f
#define BOT_TWO_PI 6.2831853f
#define BOT_TICK (1.f / BOT_TICKS_PER_SECOND)
#define BOT_SECONDS(s) ((long)((s) * BOT_TICKS_PER_SECOND))

/* ---------- goals (for movement) */

enum
{
	_bot_move_goal_position = 0, /* somewhere */
	_bot_move_goal_item, /* walk over it */
	_bot_move_goal_vehicle, /* a door: slow at the end */
};

struct bot_goal
{
	long kind;
	real_point3d origin;
	real radius; /* there, within this */
	long area; /* foot area (worked out by movement) */
};

/* ---------- movement (bot_move.c) */

#define BOT_AVOID_REACHES 8
#define BOT_CORRIDOR 24

struct bot_move_state
{
	long type; /* AAS_FOOT or AAS_VEHICLE */
	long cost_slot; /* whose danger to route round (NONE: none) */
	int careful; /* no gaps or long drops unless there's no other way */
	int human; /* corners rounded, and looked round before they are come to, as a person takes them */
	long area; /* the area we're in (or last were) */
	long area_before; /* ... and the one before it */
	/* the reachability being travelled, when it's more than a walk */
	long reach;
	long reach_type;
	long reach_ticks;
	int reach_started; /* the move itself (the jump, the climb) has begun */
	int reach_phase; /* a gap: 0 the run-up, 1 the run */
	long reach_runup_ticks; /* a gap: how long getting to the start of the run took */
	real reach_peak;
	long reach_rise_ticks; /* a ladder: when we last got higher */
	real reach_rise_z;
	/* reachabilities failed at: not taken again for a while */
	long avoid_reach[BOT_AVOID_REACHES];
	long avoid_type[BOT_AVOID_REACHES];
	long avoid_until[BOT_AVOID_REACHES];
	/* the route ahead, from the last planning */
	long corridor[BOT_CORRIDOR];
	long corridor_count;
	long last_first_reach; /* the way out of our area last time (to stick with it) */
	long last_goal_area;
	/* the goal the route is to: its area, the spot in it and where the
	   goal was then (a goal a long way off that has moved a little, someone
	   being chased, keeps its route: bot_move_to_goal) */
	long route_goal_area;
	long route_goal_type;
	real_point3d route_goal_spot;
	real_point3d route_goal_origin;
	int corridor_complete; /* to the goal's area, or to a reachability that's more than a walk */
	long corridor_goal_area;
	long corridor_type;
	long corridor_ticks;
	/* stuck */
	real_point3d progress_point;
	long progress_ticks;
	long stuck_count;
	long unstick_ticks;
	real unstick_yaw;
	/* held up: where we were last tick, and how many ticks hardly moving */
	real_point3d stall_point;
	int stall_valid;
	long stall_ticks;
	long jump_cooldown;
	long crouch_ticks;
	long reverse_ticks;
	real reverse_steer;
	long off_mesh_ticks;
	real progress_goal_distance; /* how far from the goal at the last stuck check */
	int unstick_check; /* the next stuck check comes after a sidestep: that moving isn't getting anywhere */
	real exit_yaw; /* just off a ladder: on this way a moment (exit_ticks) */
	long exit_ticks;
	real_point3d circle_goal; /* going round in circles: the goal, the nearest we've got to it, and for how long not */
	real circle_best;
	long circle_ticks;
	long circle_check;
	/* just up a ladder: not straight back down to its foot (until back_until) */
	real_point3d back_point;
	long back_until;
	int spot_latched; /* off the areas: over the nearest spot of them already (as near as it gets) */
	long wait_ticks; /* waiting a turn at a ladder */
	real last_distance;
	long dodge_ticks;
	real dodge_sign;
	real_point3d corner; /* where we last headed (testing) */
	real lane; /* this trip's way through doorways and open ground: 0 the left side .. 1 the right */
	int lane_set;
};

/* what movement needs of a bot */
struct bot_mover
{
	long index; /* player absolute index */
	long unit_index;
	unsigned long random;
	real yaw;
	real teamwork;
	struct bot_move_state move;
};

struct bot_body
{
	long unit_index;
	long vehicle_index; /* NONE on foot */
	int driving;
	real_point3d position; /* the unit's (on foot) or the vehicle's */
	real_vector3d velocity;
	real facing_yaw; /* where the vehicle points (driving) */
	long team;
	int teams; /* a team game */
	real speed_scale; /* how fast it runs, of the usual (slayer's bonus for dying, penalty for killing) */
};

struct bot_move_result
{
	int moving;
	real yaw; /* which way to move */
	real speed; /* 0..1 */
	int jump;
	int crouch;
	int look; /* the move needs us to look somewhere (a ladder): look_yaw, look_pitch */
	real look_yaw;
	real look_pitch;
	real path_yaw; /* which way the route goes (to look along it) */
	int exact; /* close to where a jump, a gap, a ladder or a drop starts: the feet exactly where it says */
	int arrived;
	int no_route; /* can't get there from here */
	long travel; /* what we're doing (_aas_travel_*) */
	/* driving */
	real throttle;
	real steer_yaw;
	int get_out;
};

void bot_move_reset(struct bot_move_state *move);
/* one tick's movement toward goal (on foot, or driving) */
void bot_move_to_goal(struct bot_mover *bot, struct bot_body const *body, struct bot_goal *goal, struct bot_move_result *result);
/* would moving this way (a strafe, a dodge) keep us on the areas (not
   into a wall, not off a ledge)? */
boolean bot_move_direction_safe(struct bot_body const *body, real yaw, real distance);
/* travel time on foot (or driving) from here to a point; <0 no way */
float bot_travel_time(struct bot_body const *body, long type, real_point3d const *to);
/* a reachability that's more than a walk under way (a jump, a ladder):
   not to be interrupted */
boolean bot_move_committed(struct bot_mover const *bot);
void bot_move_debug(struct bot_mover *bot, struct bot_body const *body);

/* ---------- shared helpers (bot_states.c) */

struct bot_mover *bot_mover_get(long index); /* NULL: no bot there */
real bot_mover_random(struct bot_mover *bot);
real bot_angle_difference(real a, real b);
real bot_wrap(real a);
real bot_distance2d(real_point3d const *a, real_point3d const *b);
real bot_yaw_to(real_point3d const *from, real_point3d const *to);
int bot_ground(real_point3d const *point, real depth, long ignore, real *ground_z);
real bot_probe(real_point3d const *from, real yaw, real length, long ignore);
int bot_unit_alive(long unit_index);
/* one of our side on foot in the lane ahead */
int bot_body_friendly_ahead(struct bot_body const *body, real_point3d const *from, real yaw, real length, real half_width);

#endif
