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
	long reach_stall_ticks;
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
float bot_travel_time_soon(struct bot_body const *body, long type, real_point3d const *to, int *soon);
/* a reachability that's more than a walk under way (a jump, a ladder):
   not to be interrupted */
boolean bot_move_committed(struct bot_mover const *bot);
void bot_move_debug(struct bot_mover *bot, struct bot_body const *body);

/* ---------- flying (bot_flight.c) */

#define BOT_PILOT_TURNS 12

/* what a bot keeps of a craft it is flying */
struct bot_pilot
{
	long vehicle; /* the craft this is of (another: afresh) */
	real_point3d route[BOT_PILOT_TURNS]; /* the turns of the way there, the last of them the place */
	long route_count;
	long route_next;
	long route_tick; /* when it was found */
	real_point3d route_to; /* ... and where to */
	long phase; /* on the way, a run at someone, off from one, landing */
	long phase_tick;
	real break_yaw;
	long sight_tick; /* when it last saw who it is after */
	long land_tick;
	int indoors; /* under a roof: on the way out */
	real rest_z; /* how high it stood when it was taken */
	int hovering; /* a plane on its jets (slow, held up by the button), not yet on its wings */
	int exit_valid;
	real_point3d exit_point;
	long exit_tick;
	real_point3d stuck_point; /* held up: where it was, since when */
	long stuck_tick;
	real speed_before; /* how fast it was going a tick ago */
	long heavy_tick; /* its other gun (a Banshee's fuel rod): when it last let go of the trigger */
	long heavy_press; /* ... and for how many ticks more it holds it */
	real swerve; /* round something in its way: to which hand, until when */
	long swerve_until;
	int away; /* off from them hurt, to mend: not round for another run until it has */
	long over_tick; /* at the end of a run, over them with its guns on them: since when (0: not yet) */
	real trim; /* how far off its pilot's look the craft really climbs or sinks: found as it flies */
	real rest_over; /* how far over the ground under it its middle was, standing where it was taken */
	real hover_yaw; /* a plane on its jets, close over where it is going: the way it faces, kept */
	int down; /* ... and over the place: straight down now */
	long down_tick;
	long still_ticks; /* ... and for how long it has not moved */
	long gate; /* the way out of where it stands that the map's script gives it (bot_country.c: NONE none) */
	long gate_tick;
	real_point3d gate_entry;
	long taken_tick; /* when the craft was taken */
	int gate_from_rest; /* the gate is the one before where it stood: for it, roof or no roof */
	real exit_open; /* how far the way out it has found is clear */
	/* down at a place under a roof (a hangar in a hillside): the way in to it */
	int way_in; /* 0 none; 1 known, and making for the open air outside its mouth; 2 in along it; 3 in, and to the place */
	long way_in_tick;
	real_point3d way_in_for; /* the place it is the way in to */
	real_point3d way_in_outside; /* out in the open, in line with the way in */
	long way_in_trail; /* the way some craft came out by, gone back along (NONE: straight in) */
	long way_in_next; /* ... the turn of it being made for, */
	long way_in_last; /* ... and the last of them, the one nearest the place */
};

/* where a pilot is to go, and what for */
struct bot_flight_order
{
	real_point3d destination; /* the place, or who it is after */
	real_vector3d velocity; /* ... and how they move (a tick's) */
	int attack; /* someone to shoot at */
	int airborne; /* ... who is in the air */
	int armed; /* the craft has a gun that fires where it points */
	real range; /* ... which reaches this far */
	real shot_speed; /* ... at this speed, world units a second */
	int heavy; /* it has another gun as well, on the other trigger (a fuel rod, a bomb) */
	int heavy_ready; /* ... which will fire now (it has cooled from its last) */
	int heavy_bomb; /* ... and is let go, not fired: it has the craft's speed and no other */
	real heavy_speed; /* ... whose shot leaves at this speed (a tick's), */
	real heavy_slowing; /* ... loses this much of it each tick, */
	real heavy_slowest; /* ... down to this, */
	real heavy_gravity; /* ... and falls: of the world's gravity, this much */
	real look_yaw, look_pitch; /* where its pilot is looking now: its shots go there */
	int patrol; /* nobody to shoot at there now: round over the place, not down at it */
	int hurt; /* its pilot's shields are down: no time to be flying at anyone */
	real vitality; /* (testing: how whoever it is after stands, shields and health together) */
	int land; /* the craft is only the way there: down by the place, and out */
	int stay; /* ... or down by it and not out: it is there to set people down, or to take them up */
	real short_of; /* ... this far short of the place itself, on the way it comes at it */
	int tail_to; /* ... and its tail to the place: the people in it leave by its tail */
};

struct bot_flight_result
{
	real yaw, pitch; /* where to look: the craft goes there */
	real throttle; /* -1..1 */
	int fire;
	int fire_heavy; /* its other gun as well */
	int climb; /* (a plane, slow: its jets up) */
	int descend;
	int get_out;
	int stuck; /* ... because it is wedged: not this craft again for a while */
	int arrived;
	int landed; /* down on the ground, and still */
};

/* the map from above, for flying over: made when the map's navigation is */
/* across country on the ground (bot_country.c) */
struct bot_traveller
{
	long vehicle; /* what it is in (another: afresh) */
	real_point3d next; /* where it is making for now */
	real_point3d stuck_point; /* held up: where it was, since when, how often of late */
	long stuck_tick;
	long stuck_last;
	long stuck_count;
	long escape_until; /* getting clear of what holds it: until when, and which way */
	real escape_yaw;
	long run_up_until; /* (backing off along a step's line for a run at it, until this tick) */
	real swerve; /* round something in the way: to which side, for how long yet */
	long swerve_ticks;
};

struct bot_country_result
{
	real yaw; /* steer this way */
	real throttle;
	real_point3d next; /* the point it is making for */
	boolean moving;
	boolean arrived;
	boolean soon; /* the way is still being worked out */
	boolean off; /* not on the country (under a roof, in a building) */
	boolean none; /* the country does not join here to there */
	boolean held_up; /* stuck time and again: out */
	boolean into_portal; /* the point is the way into a portal: straight into it */
	boolean escaping; /* getting clear of something: by its own steering, not the navigation's */
	boolean edge; /* going at an edge with nothing under it: stopping */
};

void bot_country_build(void);
void bot_country_dispose(void);
boolean bot_country_ready(void);
void bot_country_update(void); /* once a tick: its searches carried on */
boolean bot_country_on(real_point3d const *point);
boolean bot_country_nearest(real_point3d const *point, long rings, real_point3d *ground);
long bot_country_portals_of(long vehicle_index); /* the ways the map's script carries this vehicle (a set of them) */
/* (for a craft: the one of them in sight from where it stands, and into it) */
long bot_country_gate(long vehicle_index, real_point3d const *from, real facing_yaw, real within, long keep, real_point3d *entry, real_point3d *exit_point);
boolean bot_country_gate_enter(long vehicle_index, long gate);
boolean bot_country_walk(real_point3d const *from, real_point3d const *to, real_point3d *next, boolean *soon, boolean *off);
boolean bot_country_way(real_point3d const *from, real_point3d const *to, long portals, boolean drops, real ahead, real_point3d *next,
	boolean *soon, boolean *off, real *left);
real bot_country_distance(real_point3d const *from, real_point3d const *to, long portals);
boolean bot_nav_in_parts(void); /* (bot_nav.c: a map too large for its floors to be worked out whole) */
void bot_country_drive(struct bot_traveller *traveller, struct bot_body const *body, real_point3d const *to, struct bot_country_result *result);

void bot_air_build(void);
void bot_air_trails_update(void); /* once a tick: the ways craft come out from under roofs, kept */
void bot_air_dispose(void);
boolean bot_air_ready(void);
/* the highest ground, and the lowest sky, within radius of a point */
real bot_air_ground(real x, real y, real radius);
boolean bot_air_open(real_point3d const *point); /* under the sky there, not under a roof */
real bot_air_sky(real x, real y, real radius);
long bot_air_route(real_point3d const *from, real_point3d const *to, real need, real_point3d *points, long maximum);
/* a vehicle's kind, as its physics are (vehicles.c: the tank, the jeep, the
   boat, the plane, the scout (a Ghost), the fighter (a Banshee), a turret) */
enum
{
	_bot_vehicle_tank = 0,
	_bot_vehicle_jeep,
	_bot_vehicle_boat,
	_bot_vehicle_plane,
	_bot_vehicle_scout,
	_bot_vehicle_fighter,
	_bot_vehicle_turret,
	NUMBER_OF_BOT_VEHICLE_KINDS
};
long bot_vehicle_kind(long definition_index);
boolean bot_vehicle_flies(long definition_index);
real bot_vehicle_top_speed(long definition_index);
void bot_pilot_reset(struct bot_pilot *pilot);
void bot_fly(struct bot_pilot *pilot, struct bot_body const *body, struct bot_flight_order const *order, struct bot_flight_result *result);

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
