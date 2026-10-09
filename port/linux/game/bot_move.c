/*
BOT_MOVE.C

How bots get where they're going (Quake III's be_ai_move), over the area
awareness system (bot_aas.c).

Every tick: the area we're in, the goal's area, and the route between as a
string of reachabilities (the corridor), up to the first one that's more
than a walk. Across the walks the way is pulled straight through the
portals (the funnel algorithm: the shortest line through the edges the
areas share, which can't touch a wall because the areas keep a body's width
off them), and we head for its first corner.

A reachability that's more than a walk is travelled the way its type
needs, once we're at its start:
	jump      run at the ledge, jump close to it, tuck the legs up
	drop      walk off the edge
	gap       run at the edge and jump off it
	ladder    walk into it facing it until our feet are over the floor at
	          the top, then step off onto it
	teleport  onto the pad
Taking too long over one, or falling, is a failure: this bot leaves it
alone for a while, and one that bots keep failing at costs everyone more
(aas_reach_failed).

Driving is the same over the vehicle areas (kept a vehicle's width off
walls, no stairs): the route pulled straight, then pure pursuit of a point
ahead on it, slowing for sharp turns, backing out when stuck, and never
over one of our own on foot.
*/

#include "bot_internal.h"

#include "objects/objects.h"
#include "physics/collisions.h"
#include "units/units.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static real bot_move_distance(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x;
	real dy = a->y - b->y;
	real dz = a->z - b->z;
	return (real)sqrt(dx * dx + dy * dy + dz * dz);
}

int bot_refined_is(int which)
{
	static int asked, on = 0x7fffffff;
	if (!asked)
	{
		asked = TRUE;
		if (getenv("HALO_BOT_REFINE"))
			on = atoi(getenv("HALO_BOT_REFINE"));
	}
	return (on & which) != 0;
}

void bot_move_reset(struct bot_move_state *move)
{
	long i;
	long avoid_reach[BOT_AVOID_REACHES];
	long avoid_type[BOT_AVOID_REACHES];
	long avoid_until[BOT_AVOID_REACHES];
	memcpy(avoid_reach, move->avoid_reach, sizeof(avoid_reach));
	memcpy(avoid_type, move->avoid_type, sizeof(avoid_type));
	memcpy(avoid_until, move->avoid_until, sizeof(avoid_until));
	memset(move, 0, sizeof(*move));
	memcpy(move->avoid_reach, avoid_reach, sizeof(avoid_reach));
	memcpy(move->avoid_type, avoid_type, sizeof(avoid_type));
	memcpy(move->avoid_until, avoid_until, sizeof(avoid_until));
	move->slip_reach = NONE;
	move->area = NONE;
	move->cost_slot = NONE;
	move->reach = NONE;
	move->last_first_reach = NONE;
	move->last_goal_area = NONE;
	move->route_goal_area = NONE;
	move->area_before = NONE;
	move->landed_reach = NONE;
	for (i = 0; i < BOT_AVOID_REACHES; i++)
		if (move->avoid_until[i] == 0)
			move->avoid_reach[i] = NONE;
}


/* a goal further off than this that has moved a little keeps its route
   (bot_move_to_goal) */
#define BOT_FAR_GOAL 12.f

/* how far from a corner of the route the look starts round it, and the
   feet */
#define BOT_LOOK_AHEAD 3.f
#define BOT_CORNER_ROUND 1.1f
/* ... and from how far a sharp turn over a drop or a bank is slowed for */
#define BOT_TURN_CARE 0.8f
/* ... and the room it gives a wall's corner, turning round it */
#define BOT_CORNER_ROOM 0.22f
/* held up: moving less than this much of what it should, for this long */
#define BOT_STALL_FRACTION 0.3f
#define BOT_STALL_TICKS 6
/* ... and on a reach (a jump, a gap, a ladder), this long of not moving at
   all: it is given up */
#define BOT_REACH_STALL_TICKS 15
/* how long the jump button is held for a jump that has not come yet */
#define BOT_JUMP_HOLD 8
/* how far off a bot has been in a second that is not stuck, back where it began or not */
#define BOT_STUCK_PATH 0.9f
/* ... and how long it has to have been standing up to be jumped from */
#define BOT_STAND_TICKS 8

static boolean bot_move_avoiding(void *context, long reach)
{
	struct bot_move_state const *move = (struct bot_move_state const *)context;
	long now = game_time_get();
	long i;
	/* the careful don't take long drops (a hard landing hurts) when
	   there's another way. A gap is a way like any other, whatever is under
	   it: everyone jumps it when it's the quickest, as people do */
	if (move->careful && move->type == AAS_FOOT)
	{
		struct aas_reach const *r = aas_reach(move->type, reach);
		if (r->travel == _aas_travel_drop && r->edge.z - r->end.z > 2.5f)
			return TRUE;
	}
	for (i = 0; i < BOT_AVOID_REACHES; i++)
		if (move->avoid_reach[i] == reach && move->avoid_type[i] == move->type && move->avoid_until[i] > now)
			return TRUE;
	/* just up a ladder: not straight back down to its foot, whatever the
	   quickest way to where the goal's got to now (someone we're after,
	   moving about, is nearer by the top one moment and the bottom the
	   next: up and down, and never there) */
	if (move->back_until > now && move->type == AAS_FOOT)
	{
		struct aas_reach const *r = aas_reach(move->type, reach);
		if (r->travel != _aas_travel_walk && bot_move_distance(&r->end, &move->back_point) < 1.5f)
			return TRUE;
	}
	return FALSE;
}

/* ...the ones we've given up on that are walks (somewhere too tight to
   get through, a way that got us stuck) still, but not a jump, a gap or
   a ladder (tried again, when it's the only way out) */
/* ...and, while a route is planned, the ways out that only lead back here
   (bot_move_plan) */
struct bot_move_plan_avoid
{
	struct bot_move_state *move;
	long back[4];
	long count;
};

static boolean bot_move_avoiding_back(void *context, long reach)
{
	struct bot_move_plan_avoid const *plan = (struct bot_move_plan_avoid const *)context;
	long i;
	for (i = 0; i < plan->count; i++)
		if (plan->back[i] == reach)
			return TRUE;
	return bot_move_avoiding(plan->move, reach);
}

/* (whether it keeps off anything just now: with nothing kept off, a route
   never leads back on itself) */
static boolean bot_move_avoids_any(struct bot_move_state const *move)
{
	long now = game_time_get();
	long i;
	if (move->careful)
		return TRUE;
	for (i = 0; i < BOT_AVOID_REACHES; i++)
		if (move->avoid_reach[i] != NONE && move->avoid_type[i] == move->type && move->avoid_until[i] > now)
			return TRUE;
	return FALSE;
}

static boolean bot_move_avoiding_walks(void *context, long reach)
{
	struct bot_move_state const *move = (struct bot_move_state const *)context;
	return aas_reach(move->type, reach)->travel == _aas_travel_walk && bot_move_avoiding(context, reach);
}

static void bot_move_avoid(struct bot_move_state *move, long reach, long ticks)
{
	long i;
	long oldest = 0;
	for (i = 0; i < BOT_AVOID_REACHES; i++)
	{
		if (move->avoid_reach[i] == reach && move->avoid_type[i] == move->type)
		{
			oldest = i;
			break;
		}
		if (move->avoid_until[i] < move->avoid_until[oldest])
			oldest = i;
	}
	move->avoid_reach[oldest] = reach;
	move->avoid_type[oldest] = move->type;
	move->avoid_until[oldest] = game_time_get() + ticks;
}

/* ---------- the funnel: the shortest way through a string of portals */

static real bot_cross(real_point3d const *o, real_point3d const *a, real_point3d const *b)
{
	return (a->x - o->x) * (b->y - o->y) - (a->y - o->y) * (b->x - o->x);
}

static boolean bot_same_point(real_point3d const *a, real_point3d const *b)
{
	return fabs(a->x - b->x) < 0.001f && fabs(a->y - b->y) < 0.001f;
}

/* portals[0] is (start, start), the last is (goal, goal); left and right
   as seen going through. The corners of the way through, up to maximum */
static long bot_funnel(real_point3d const *left, real_point3d const *right, long count, real_point3d *corners, long maximum)
{
	real_point3d apex = left[0];
	real_point3d funnel_left = left[0];
	real_point3d funnel_right = right[0];
	long apex_index = 0, left_index = 0, right_index = 0;
	long corner_count = 0;
	long i;

	for (i = 1; i < count && corner_count < maximum; i++)
	{
		/* the right side */
		if (bot_cross(&apex, &funnel_right, &right[i]) >= 0.f)
		{
			if (bot_same_point(&apex, &funnel_right) || bot_cross(&apex, &funnel_left, &right[i]) < 0.f)
			{
				funnel_right = right[i];
				right_index = i;
			}
			else
			{
				/* over the left side: its point is a corner */
				corners[corner_count++] = funnel_left;
				apex = funnel_left;
				apex_index = left_index;
				funnel_left = apex;
				funnel_right = apex;
				left_index = right_index = apex_index;
				i = apex_index;
				continue;
			}
		}
		/* the left side */
		if (bot_cross(&apex, &funnel_left, &left[i]) <= 0.f)
		{
			if (bot_same_point(&apex, &funnel_left) || bot_cross(&apex, &funnel_right, &left[i]) > 0.f)
			{
				funnel_left = left[i];
				left_index = i;
			}
			else
			{
				corners[corner_count++] = funnel_right;
				apex = funnel_right;
				apex_index = right_index;
				funnel_left = apex;
				funnel_right = apex;
				left_index = right_index = apex_index;
				i = apex_index;
				continue;
			}
		}
	}
	if (corner_count < maximum)
		corners[corner_count++] = left[count - 1];
	return corner_count;
}

/* ---------- planning */

/* the route out of our area toward the goal's, up to the first
   reachability that's more than a walk */
static void bot_move_plan(struct bot_move_state *move, long area, real_point3d const *from, long goal_area)
{
	long a = area;
	real_point3d point = *from;
	move->corridor_count = 0;
	move->corridor_complete = FALSE;
	while (move->corridor_count < BOT_CORRIDOR && a != goal_area)
	{
		struct aas_reach const *reach;
		float time;
		long r = aas_route_reach(move->type, a, &point, goal_area, bot_move_avoiding, move, move->type == AAS_FOOT ? move->cost_slot : NONE, &time);
		long i;
		if (r == NONE && move->careful)
		{
			/* the only way is the risky one: take it after all */
			int careful = move->careful;
			move->careful = FALSE;
			r = aas_route_reach(move->type, a, &point, goal_area, bot_move_avoiding, move, move->type == AAS_FOOT ? move->cost_slot : NONE, &time);
			move->careful = careful;
		}
		if (r == NONE && move->corridor_count == 0 && move->type == AAS_FOOT)
		{
			/* every way out of here is one we've failed at lately (a pit
			   whose only way out is a jump): try it again rather than stand
			   in the pit till we'd forget */
			r = aas_route_reach(move->type, a, &point, goal_area, bot_move_avoiding_walks, move, move->type == AAS_FOOT ? move->cost_slot : NONE, &time);
		}
		if (r == NONE && move->corridor_count == 0)
		{
			/* ...or the walk out of here is (a corner of floor at the foot
			   of a ladder, an area of its own with the one way off it: stuck
			   there once, and that way's given up for a while): again,
			   sooner than stand here till it's forgotten */
			r = aas_route_reach(move->type, a, &point, goal_area, NULL, NULL, move->type == AAS_FOOT ? move->cost_slot : NONE, &time);
		}
		if (r == NONE)
			break;
		/* A way out that only leads back here: the quickest way on from the
		   next area is one we are keeping off (failed at lately, or a long
		   drop and we are careful), so from there the route is back here
		   for it, and from here out again: to and fro across the one edge
		   till the keeping off wears out. Not a way: the next best out of
		   here; and when every way out is that, the quickest of all, kept
		   off or not. */
		if (move->corridor_count == 0)
		{
			struct bot_move_plan_avoid plan;
			boolean keeping_off = bot_move_avoids_any(move);
			long slot = move->type == AAS_FOOT ? move->cost_slot : NONE;
			plan.move = move;
			plan.count = 0;
			/* (looked into when something is kept off, or the way out is
			   back into the area we have just come from) */
			while (r != NONE && aas_reach(move->type, r)->travel == _aas_travel_walk &&
				(keeping_off || aas_reach(move->type, r)->to == move->area_before))
			{
				struct aas_reach const *first = aas_reach(move->type, r);
				float on_time;
				long on = first->to == goal_area ? NONE :
					aas_route_reach(move->type, first->to, &first->end, goal_area, bot_move_avoiding_back, &plan, slot, &on_time);
				if (on == NONE || aas_reach(move->type, on)->to != a)
					break;
				if (plan.count >= (long)(sizeof(plan.back) / sizeof(plan.back[0])))
				{
					r = NONE;
					break;
				}
				plan.back[plan.count++] = r;
				r = aas_route_reach(move->type, a, &point, goal_area, bot_move_avoiding_back, &plan, slot, &time);
			}
			if (r == NONE)
				r = aas_route_reach(move->type, a, &point, goal_area, NULL, NULL, slot, &time);
		}
		if (r == NONE)
			break;
		/* out of the area we're in: the way we were going, unless another is
		   clearly quicker (two ways about as quick: no dithering between them) */
		if (move->corridor_count == 0)
		{
			long previous = move->last_first_reach;
			if (previous != NONE && previous != r && move->last_goal_area == goal_area && previous < aas_reach_count(move->type) &&
				aas_reach(move->type, previous)->from == a && !bot_move_avoiding(move, previous))
			{
				float previous_time = aas_goal_time(move->type, previous, goal_area, move->type == AAS_FOOT ? move->cost_slot : NONE);
				if (previous_time >= 0.f)
				{
					real dx = point.x - aas_reach(move->type, previous)->start.x;
					real dy = point.y - aas_reach(move->type, previous)->start.y;
					previous_time += (float)(sqrt(dx * dx + dy * dy) / aas_speed(move->type));
					if (previous_time < time + 1.f)
						r = previous;
				}
			}
			move->last_first_reach = r;
			move->last_goal_area = goal_area;
		}
		/* (a loop: the avoided ones can send a route round in circles) */
		for (i = 0; i < move->corridor_count; i++)
			if (aas_reach(move->type, move->corridor[i])->from == aas_reach(move->type, r)->to)
				return;
		move->corridor[move->corridor_count++] = r;
		reach = aas_reach(move->type, r);
		if (reach->travel != _aas_travel_walk)
		{
			move->corridor_complete = TRUE;
			break;
		}
		point = reach->end;
		a = reach->to;
	}
	if (a == goal_area)
		move->corridor_complete = TRUE;
}

/* where to head across the walks of the corridor (its first corner and
   the one after), ending at final */
static long bot_move_corners(struct bot_move_state const *move, real_point3d const *from, real_point3d const *final, real_point3d *corners, long maximum)
{
	real_point3d left[BOT_CORRIDOR + 2];
	real_point3d right[BOT_CORRIDOR + 2];
	long count = 0;
	long i;
	left[count] = *from;
	right[count] = *from;
	count++;
	for (i = 0; i < move->corridor_count; i++)
	{
		struct aas_reach const *reach = aas_reach(move->type, move->corridor[i]);
		struct aas_area const *a;
		struct aas_area const *b;
		real dx, dy;
		real side;
		if (reach->travel != _aas_travel_walk)
			break;
		a = aas_area(move->type, reach->from);
		b = aas_area(move->type, reach->to);
		/* going from a's side of the edge to b's: which end is on the left */
		dx = (b->x0 + b->x1) - (a->x0 + a->x1);
		dy = (b->y0 + b->y1) - (a->y0 + a->y1);
		side = dx * (reach->left.y - reach->start.y) - dy * (reach->left.x - reach->start.x);
		if (side >= 0.f)
		{
			left[count] = reach->left;
			right[count] = reach->right;
		}
		else
		{
			left[count] = reach->right;
			right[count] = reach->left;
		}
		/* A little in from each end of the edge, where it is wide enough:
		   the ends are a body's half width from the walls and no more, and
		   a way that turns on them rubs along the wall's corner and catches
		   on every seam and lip of it. A person gives a corner room. */
		if (move->type == AAS_FOOT && move->human)
		{
			real_point3d l = left[count];
			real_point3d r = right[count];
			real width = bot_distance2d(&l, &r);
			if (width > BOT_CORNER_ROOM)
			{
				real f = MIN(BOT_CORNER_ROOM, (width - BOT_CORNER_ROOM) * 0.5f) / width;
				left[count].x = l.x + (r.x - l.x) * f;
				left[count].y = l.y + (r.y - l.y) * f;
				left[count].z = l.z + (r.z - l.z) * f;
				right[count].x = r.x + (l.x - r.x) * f;
				right[count].y = r.y + (l.y - r.y) * f;
				right[count].z = r.z + (l.z - r.z) * f;
			}
		}
		/* this trip's lane: through each wide doorway or across each open
		   stretch toward one side or the other (a bot's own way through,
		   not everyone down the same line hugging the same corners) */
		/* (not the doorway we're about to go through: walking along the
		   edge between two areas, the lane on it can be behind us from one
		   and gone from the other, and we'd turn back and forth) */
		if (move->type == AAS_FOOT && move->lane_set && i > 0)
		{
			real_point3d l = left[count];
			real_point3d r = right[count];
			real width = bot_distance2d(&l, &r);
			if (width > 1.2f)
			{
				real half = MAX(0.3f, width * 0.15f) / width;
				real f0 = MAX(0.f, move->lane - half);
				real f1 = MIN(1.f, move->lane + half);
				left[count].x = l.x + (r.x - l.x) * f0;
				left[count].y = l.y + (r.y - l.y) * f0;
				left[count].z = l.z + (r.z - l.z) * f0;
				right[count].x = l.x + (r.x - l.x) * f1;
				right[count].y = l.y + (r.y - l.y) * f1;
				right[count].z = l.z + (r.z - l.z) * f1;
			}
		}
		count++;
	}
	left[count] = *final;
	right[count] = *final;
	count++;
	return bot_funnel(left, right, count, corners, maximum);
}

/* ---------- on foot: straight at a point, round what's in the way */

/* a step up just ahead that a body won't walk up (a curb, a door sill,
   the rim of a pit: a couple of tenths or more), with floor on top */
static int bot_move_step_ahead(struct bot_body const *body, real yaw)
{
	unsigned long const structure = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit);
	struct collision_result collision;
	real_point3d foot = body->position;
	real_vector3d ahead;
	real_point3d past;
	real ground_z;
	foot.z += 0.12f;
	ahead.i = (real)cos(yaw) * 0.4f;
	ahead.j = (real)sin(yaw) * 0.4f;
	ahead.k = 0.f;
	if (!collision_test_vector(structure, &foot, &ahead, body->unit_index, &collision) ||
		collision.plane.n.k > 0.6f || collision.plane.n.k < -0.6f)
		return FALSE;
	past = collision.point;
	past.x += (real)cos(yaw) * 0.12f;
	past.y += (real)sin(yaw) * 0.12f;
	past.z = body->position.z + 1.1f;
	return bot_ground(&past, 1.4f, body->unit_index, &ground_z) && ground_z - body->position.z > 0.2f &&
		ground_z - body->position.z < 0.75f;
}

/* Where a hop from here comes down, going the way and at the pace it is
   going: on something, or off the far side of what it is on. A hop is a
   second in the air, and at a run that is two world units, further than the
   top of a rock is wide: the bots going up the rock under the arch of Battle
   Creek hopped at a lump in it, sailed over the top and came down in the
   creek on the far side, round and round. FALSE: nothing to land on within
   a short fall of where it left the ground. */
#define BOT_HOP_VELOCITY 2.1f
#define BOT_HOP_GRAVITY 3.21f
#define BOT_HOP_FALL 0.9f
#define BOT_HOP_AIR 1.05f
static boolean bot_move_hop_lands(struct bot_body const *body, real yaw)
{
	real along = body->velocity.i * BOT_TICKS_PER_SECOND * (real)cos(yaw) + body->velocity.j * BOT_TICKS_PER_SECOND * (real)sin(yaw);
	real time;
	if (along < 0.5f)
		return TRUE; /* (as good as straight up) */
	for (time = 0.1f; time < 2.f; time += 0.1f)
	{
		real z = body->position.z + BOT_HOP_VELOCITY * time - 0.5f * BOT_HOP_GRAVITY * time * time;
		real_point3d top = body->position;
		real ground;
		/* (its legs go on working in the air, as it goes on being walked
		   forward: a unit a second faster each second, up to a run) */
		real faster = MIN(BOT_HOP_AIR * time, MAX(2.25f - along, 0.f));
		real gone = along * time + 0.5f * faster * time;
		top.x += (real)cos(yaw) * gone;
		top.y += (real)sin(yaw) * gone;
		top.z = MAX(z, body->position.z) + 0.4f;
		if (bot_ground(&top, top.z - (body->position.z - BOT_HOP_FALL), body->unit_index, &ground) && ground >= z - 0.05f)
		{
			/* (... and room to come to a stop on: it lands going as fast
			   as it left, and the top of a rock a foot wide is run off the
			   far side of in the step after) */
			real_point3d on = top;
			real further;
			on.x += (real)cos(yaw) * MIN((along + faster) * 0.2f, 0.45f);
			on.y += (real)sin(yaw) * MIN((along + faster) * 0.2f, 0.45f);
			on.z = ground + 0.5f;
			return bot_ground(&on, 0.5f + BOT_HOP_FALL, body->unit_index, &further);
		}
		if (z < body->position.z - BOT_HOP_FALL)
			return FALSE;
	}
	return FALSE;
}

/* units (people, vehicles) right in front: step round them */
static real bot_move_dodge(struct bot_mover *bot, struct bot_body const *body, real yaw)
{
	struct collision_result collision;
	real_point3d from = body->position;
	real_vector3d vector;
	unsigned long const flags = FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_bipeds_bit) |
		FLAG(_collision_test_objects_vehicles_bit) | FLAG(_collision_test_objects_scenery_bit) | FLAG(_collision_test_objects_machines_bit);
	from.z += 0.35f;
	vector.i = (real)cos(yaw) * 1.f;
	vector.j = (real)sin(yaw) * 1.f;
	vector.k = 0.f;
	if (bot->move.dodge_ticks > 0)
	{
		bot->move.dodge_ticks--;
		return bot_wrap(yaw + bot->move.dodge_sign * 0.9f);
	}
	/* ...and something low or thin the areas didn't know of (a railing on
	   posts, the lip of a ramp that's open underneath): at the knee and the
	   waist, just ahead */
	if (!collision_test_vector(flags, &from, &vector, body->unit_index, &collision))
	{
		unsigned long const structure = FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
			FLAG(_collision_test_objects_scenery_bit) | FLAG(_collision_test_objects_bit);
		real_point3d knee = body->position;
		real_vector3d ahead;
		long h;
		ahead.i = (real)cos(yaw) * 0.45f;
		ahead.j = (real)sin(yaw) * 0.45f;
		ahead.k = 0.f;
		for (h = 0; h < 2; h++)
		{
			knee.z = body->position.z + (h ? 0.55f : 0.3f);
			if (collision_test_vector(structure, &knee, &ahead, body->unit_index, &collision) &&
				collision.plane.n.k < 0.6f && collision.plane.n.k > -0.6f) /* (a face in the way, not the floor or a slope) */
			{
				/* stairs, a curb, a ramp: the floor just past the face rises
				   to about where it was hit (a railing's bar or a post has
				   the floor still at our feet past it) */
				real_point3d past = collision.point;
				real ground_z;
				real height = knee.z - body->position.z;
				past.x += (real)cos(yaw) * 0.12f;
				past.y += (real)sin(yaw) * 0.12f;
				past.z = body->position.z + 1.1f;
				if (bot_ground(&past, 1.4f, body->unit_index, &ground_z) && ground_z - body->position.z >= height - 0.3f &&
					ground_z - body->position.z < 1.f)
				{
					continue;
				}
				break;
			}
		}
		if (h == 2)
			return yaw;
	}
	{
		long i;
		real first = bot_mover_random(bot) < 0.5f ? 1.f : -1.f; /* (a fresh pick each time: not always the same side) */
		for (i = 0; i < 2; i++)
		{
			real sign = i ? -first : first;
			if (bot_move_direction_safe(body, bot_wrap(yaw + sign * 0.9f), 0.8f))
			{
				bot->move.dodge_sign = sign;
				bot->move.dodge_ticks = 8;
				return bot_wrap(yaw + sign * 0.9f);
			}
		}
	}
	return yaw;
}

boolean bot_move_direction_safe(struct bot_body const *body, real yaw, real distance)
{
	real_point3d to = body->position;
	to.x += (real)cos(yaw) * distance;
	to.y += (real)sin(yaw) * distance;
	if (!aas_ready())
		return TRUE;
	if (aas_line_in_areas(AAS_FOOT, &body->position, &to))
		return TRUE;
	/* by a wall (not in an area): on the floor, off the wall, not over a drop */
	return navm_line_walkable(&body->position, &to, 0.2f, 0.35f);
}

float bot_travel_time(struct bot_body const *body, long type, real_point3d const *to)
{
	if (!aas_ready())
		return bot_move_distance(&body->position, to) / aas_speed(type);
	return aas_travel_time(type, &body->position, to);
}

/* ... for something being weighed, that can do with the answer a tick or
   two on (aas_travel_time_soon): *soon, and a time below 0, when it is not
   worked out yet */
float bot_travel_time_soon(struct bot_body const *body, long type, real_point3d const *to, int *soon)
{
	boolean waits = FALSE;
	float time;
	*soon = FALSE;
	if (!aas_ready())
		return bot_move_distance(&body->position, to) / aas_speed(type);
	time = aas_travel_time_soon(type, &body->position, to, &waits);
	*soon = waits;
	return time;
}

/* ---------- reachabilities more than a walk */

/* others (bots) close by: on a ladder, at a ledge, crowding */
/* someone in the way of a jump over a gap: a bot in the air over the same
   gap, or about to be, or one standing where the jump comes down. Jumped
   into, either is the jump lost and a fall */
static boolean bot_move_gap_busy(struct bot_mover const *bot, long reach)
{
	struct aas_reach const *r = aas_reach(AAS_FOOT, reach);
	long i;
	for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
	{
		struct bot_mover const *other = bot_mover_get(i);
		struct object_datum *unit;
		if (!other || other == bot || other->unit_index == NONE || !bot_unit_alive(other->unit_index) ||
			!(unit = object_try_and_get(other->unit_index)))
			continue;
		if (bot_distance2d(&unit->object.position, &r->left) < 0.9f && fabs(unit->object.position.z - r->left.z) < 1.f)
			return TRUE;
		if (other->move.reach != NONE && other->move.reach_type == AAS_FOOT)
		{
			struct aas_reach const *theirs = aas_reach(AAS_FOOT, other->move.reach);
			if (theirs->travel == _aas_travel_gap && bot_distance2d(&theirs->left, &r->left) < 1.f && fabs(theirs->left.z - r->left.z) < 1.f)
				return TRUE;
		}
	}
	return FALSE;
}

static boolean bot_move_crowded(struct bot_mover const *bot)
{
	struct object_datum *me = bot->unit_index != NONE ? object_try_and_get(bot->unit_index) : NULL;
	long i;
	if (!me)
		return FALSE;
	for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
	{
		struct bot_mover const *other = bot_mover_get(i);
		struct object_datum *unit;
		if (!other || other == bot || other->unit_index == NONE || !(unit = object_try_and_get(other->unit_index)))
			continue;
		if (bot_distance2d(&unit->object.position, &me->object.position) < 1.5f && fabs(unit->object.position.z - me->object.position.z) < 3.f)
			return TRUE;
	}
	return FALSE;
}

/* how far ahead the floor's end is looked for on the run at a gap: the jump
   is asked for when it ends this near */
#define BOT_GAP_JUMP_AHEAD 0.27f

/* how long the legs stay tucked up from just short of a gap's far side: the
   last of the flight, and the landing */
#define BOT_GAP_TUCK_TICKS 18

/* The legs are tucked up for a ledge higher than this, and not for a lower
   one: a jump tops out at 0.69, and the feet are over a ledge of 0.55 for
   half a second of it without. (They were tucked at every jump up, and a
   bot folded in the air over a kerb is nothing anyone does. HALO_BOT_TUCKRISE:
   another, to try.) */
#define BOT_TUCK_RISE 0.55f
static boolean bot_move_tucks(real rise)
{
	static int asked;
	static real least = BOT_TUCK_RISE;
	if (!asked)
	{
		asked = TRUE;
		if (getenv("HALO_BOT_TUCKRISE"))
			least = (real)atof(getenv("HALO_BOT_TUCKRISE"));
	}
	return !bot_refined_is(512) || rise > least;
}

/* (the bot being moved: how much in its stride it takes its jumps) */
static real bot_move_fluency;
/* (testing, HALO_BOT_FLUENT=<a sum>: 1 the gaps, 2 the jumps up) */
static int bot_move_fluent(int which)
{
	static int asked, on = 3;
	if (!asked)
	{
		asked = TRUE;
		if (getenv("HALO_BOT_FLUENT"))
			on = atoi(getenv("HALO_BOT_FLUENT"));
	}
	return (on & which) != 0;
}

static void bot_move_reach_failed(struct bot_mover *bot, char const *why)
{
	struct bot_move_state *move = &bot->move;
	if (bot_logging())
	{
		struct aas_reach const *reach = aas_reach(move->reach_type, move->reach);
		bot_log("bot%ld: %s reachability %ld (travel %d from %.1f %.1f %.1f to %.1f %.1f %.1f) failed: %s; %s after %ld ticks (%ld of run-up), phase %ld, careful %d, landing %.2f %.2f %.2f, highest %.2f, pace %.2f%s%s, at %.2f %.2f %.2f%s",
			bot->index, move->reach_type == AAS_VEHICLE ? "vehicle" : "foot", move->reach, reach->travel, reach->edge.x, reach->edge.y, reach->edge.z,
			reach->end.x, reach->end.y, reach->end.z, why, move->reach_started ? "begun" : "not begun", move->reach_ticks, move->reach_runup_ticks,
			(long)move->reach_phase, (int)reach->careful, reach->left.x, reach->left.y, reach->left.z, move->reach_peak, move->reach_pace,
			move->reach_no_jump ? " no jump" : "", move->reach_hole == 2 ? " hole" : "", move->stall_point.x, move->stall_point.y, move->stall_point.z, bot_move_crowded(bot) ? ", crowded" : "");
	}
	if (bot_logging() && aas_reach(move->reach_type, move->reach)->travel == _aas_travel_jump)
		bot_log("bot%ld: jump up %.2f failed%s", bot->index, aas_reach(move->reach_type, move->reach)->left.z - aas_reach(move->reach_type, move->reach)->edge.z,
			move->reach_started ? "" : " (not begun)");
	/* in a crowd (someone in the way on the ladder, at the edge): not the
	   way's fault; this bot tries another way a while, nobody learns from it */
	if (!bot_move_crowded(bot))
		aas_reach_failed(move->reach_type, move->reach);
	bot_move_avoid(move, move->reach, BOT_SECONDS(bot_move_crowded(bot) ? 8 : 25));
	move->reach = NONE;
}

/* Going sideways to a jump that is about to be made: how fast, across the
   line from here to where it is to land. A jump keeps the speed it leaves
   the ground with, a second or so of it, and what it has across the line is
   where it comes down beside the place: up the rock under the arch of Battle
   Creek the last little jump onto its spine was come to at a run along the
   rock's side, and made from there it went over the spine and off the far
   side of it. */
#define BOT_DELICATE 3.f /* this near where a jump, a gap, a ladder or a drop starts: no weaving */
/* coming to where a gap is jumped from: from this far off, no faster than
   stops this short of it, slowing at this rate (a body's own, measured: a
   run to a stand in four tenths of a second), and never under this */
#define BOT_STOP_BY_GAP 1.4f
#define BOT_STOP_SHORT 0.25f
#define BOT_STOP_SLOWING 3.f /* world units a second, a second */
#define BOT_STOP_SPEED 0.35f /* world units a second */
#define BOT_STOP_ON_LINE 0.2f /* running at the gap this near the line it is jumped on: its run, not slowed */
#define BOT_JUMP_ACROSS_MOST 0.6f /* world units a second */
#define BOT_JUMP_SETTLE_TICKS 24 /* how long it is held back to lose that */
static real bot_move_jump_across(struct bot_body const *body, real_point3d const *landing)
{
	real yaw = bot_yaw_to(&body->position, landing);
	return (real)fabs(-body->velocity.i * (real)sin(yaw) + body->velocity.j * (real)cos(yaw)) * BOT_TICKS_PER_SECOND;
}

/* running at a gap already: in line with the jump, at speed, some way short
   of its edge and with floor all the way to it */
static boolean bot_move_gap_run_on(struct bot_body const *body, struct aas_reach const *reach)
{
	real_point3d const *position = &body->position;
	real run, past, across, along;
	if (reach->travel != _aas_travel_gap || reach->careful == AAS_WALKOVER || reach->careful == AAS_HOP)
		return FALSE;
	if (fabs(position->z - reach->edge.z) > 0.3f)
		return FALSE;
	run = bot_yaw_to(&reach->edge, &reach->left);
	past = (position->x - reach->edge.x) * (real)cos(run) + (position->y - reach->edge.y) * (real)sin(run);
	across = (real)fabs(-(position->x - reach->edge.x) * (real)sin(run) + (position->y - reach->edge.y) * (real)cos(run));
	/* (the better bots from a little nearer and a little further off the
	   line: the run itself steers them onto it) */
	if (past > -0.8f + 0.2f * bot_move_fluency || past < -2.4f || across > 0.25f + 0.2f * bot_move_fluency)
		return FALSE;
	along = (body->velocity.i * (real)cos(run) + body->velocity.j * (real)sin(run)) * BOT_TICKS_PER_SECOND;
	if (along < 1.9f)
		return FALSE;
	/* (... and floor along the way it will run, which is at where it
	   lands and not at the edge: beside the line, that way comes to the
	   end of the floor somewhere else. From the head of the ramp to the
	   overshield on Longest the floor ends square and the jump goes off it
	   cornerwise; a bot coming up beside the line ran off the square end
	   four tenths of a unit short of the corner, with the jump not yet
	   asked for, one time in seven.) */
	if (bot_refined_is(16384))
	{
		real to_landing = bot_yaw_to(position, &reach->left);
		real gain = (real)cos(bot_angle_difference(to_landing, run));
		if (gain > 0.3f && past < -0.15f)
		{
			real_point3d off = *position;
			real advance = (-0.1f - past) / gain;
			off.x += (real)cos(to_landing) * advance;
			off.y += (real)sin(to_landing) * advance;
			if (!navm_line_arrives(position, &off, 0.1f, 0.f, FALSE))
				return FALSE;
		}
	}
	return navm_line_arrives(position, &reach->edge, 0.15f, 0.f, FALSE);
}

/* A running jump (bot_aas.c's own numbers for one: world units and seconds) */
#define BOT_RUN_SPEED 2.25f
#define BOT_JUMP_VELOCITY 2.1f
#define BOT_JUMP_GRAVITY 3.21f
#define BOT_BODY_RADIUS 0.2f /* how far a body's middle is from a face it stands against */

/* How a gap is left: how fast, and with a jump or without.
   The areas call anything a gap that a jump at a full run would carry to or
   beyond: a ledge three units off and two down as much as a walkway's
   break. Taken at a full run with a jump, as all of them were, a ledge that
   is nearer than the jump carries is sailed over, and what is beyond and
   below it is where the bot comes down. So: where a jump at a full run
   comes down on floor about as high as it is meant to land, flat out as
   ever (the walkway goes on, and the further on to it the better); where
   it does not, as fast as lands it where it is meant to, and off the edge
   at a run with no jump at all when that carries far enough (somewhere near
   and well below is dropped onto, not leapt at). */
static void bot_move_gap_takeoff(struct bot_mover *bot, struct bot_body const *body, struct aas_reach const *reach)
{
	static real const beyonds[4] = { 0.5f, 0.35f, 0.2f, 0.05f };
	struct bot_move_state *move = &bot->move;
	real run = bot_yaw_to(&reach->edge, &reach->left);
	real across = bot_distance2d(&reach->edge, &reach->left);
	real down = reach->edge.z - reach->left.z;
	real under = BOT_JUMP_VELOCITY * BOT_JUMP_VELOCITY + 2.f * BOT_JUMP_GRAVITY * down;
	real speed = BOT_RUN_SPEED;
	real carried, wanted;
	real_point3d there;
	real ground;
	long k;

	move->reach_pace = 1.f;
	move->reach_no_jump = FALSE;
	move->reach_beyond = 0.05f;
	if ((reach->careful == AAS_WALKOVER && move->reach_hole != 2) || reach->careful == AAS_HOP)
		return;
	/* (where on it to come down: as far on from its near end as there is
	   floor for, and a step more: not on the lip of it, where a hand's
	   width short is a miss) */
	for (k = 0; k < 4; k++)
	{
		there.x = reach->left.x + (real)cos(run) * (beyonds[k] + 0.2f);
		there.y = reach->left.y + (real)sin(run) * (beyonds[k] + 0.2f);
		there.z = reach->left.z + 0.6f;
		if (bot_ground(&there, 1.1f, body->unit_index, &ground) && fabs(ground - reach->left.z) < 0.4f)
		{
			move->reach_beyond = beyonds[k];
			break;
		}
	}
	if (under <= 0.f)
		return;
	carried = speed * (BOT_JUMP_VELOCITY + (real)sqrt(under)) / BOT_JUMP_GRAVITY;
	/* (the jump is asked for a step before the edge: BOT_GAP_JUMP_AHEAD) */
	wanted = across + move->reach_beyond + 0.3f;
	there.x = reach->edge.x + (real)cos(run) * (carried - 0.3f);
	there.y = reach->edge.y + (real)sin(run) * (carried - 0.3f);
	there.z = reach->left.z + 1.f;
	if (carried <= wanted + 0.2f || (bot_ground(&there, 1.5f, body->unit_index, &ground) && ground > reach->left.z - 0.4f && ground < reach->left.z + 0.7f))
		return;
	if (down > 0.5f)
	{
		real walked = speed * (real)sqrt(2.f * down / BOT_JUMP_GRAVITY);
		if (across + move->reach_beyond <= walked)
		{
			move->reach_no_jump = TRUE;
			move->reach_pace = PIN((across + move->reach_beyond) / walked, 0.2f, 1.f);
			return;
		}
	}
	move->reach_pace = PIN(wanted / carried, 0.45f, 1.f);
}

/* In the air over a gap: steered to where it is to come down. A body in
   the air is pushed about a unit a second each second the way its legs
   are worked, which over a long jump is most of a unit either way: enough
   to come down on a ledge and not beside it, or short of it, or over it.
   Where it would come down as it goes, against where it is meant to: the
   difference, made up in the time that is left. (A gap run at flat out,
   onto floor that goes on, is not held back: only brought on, and into
   line.) FALSE when there is nothing to steer by. */
static boolean bot_move_gap_steer(struct bot_mover *bot, struct bot_body const *body, struct aas_reach const *reach, struct bot_move_result *result)
{
	struct bot_move_state *move = &bot->move;
	real_point3d const *position = &body->position;
	real run = bot_yaw_to(&reach->edge, &reach->left);
	real rise = body->velocity.k * BOT_TICKS_PER_SECOND;
	real over = position->z - reach->left.z;
	real under = rise * rise + 2.f * BOT_JUMP_GRAVITY * over;
	real left, along, cross, want_i, want_j, fast;
	real going_i = body->velocity.i * BOT_TICKS_PER_SECOND, going_j = body->velocity.j * BOT_TICKS_PER_SECOND;
	if (fabs(body->velocity.k) < 0.004f || under <= 0.f)
		return FALSE;
	left = (rise + (real)sqrt(under)) / BOT_JUMP_GRAVITY;
	if (left < 0.05f)
		return FALSE;
	{
		real to_x = reach->left.x + (real)cos(run) * move->reach_beyond - (position->x + going_i * left);
		real to_y = reach->left.y + (real)sin(run) * move->reach_beyond - (position->y + going_j * left);
		along = to_x * (real)cos(run) + to_y * (real)sin(run);
		cross = -to_x * (real)sin(run) + to_y * (real)cos(run);
	}
	if (move->reach_pace >= 1.f && !move->reach_no_jump && along < 0.f)
		along = 0.f;
	/* (the speed to be going at: as it goes, and the difference over half the time left) */
	want_i = going_i + ((real)cos(run) * along - (real)sin(run) * cross) * 2.f / MAX(left, 0.25f);
	want_j = going_j + ((real)sin(run) * along + (real)cos(run) * cross) * 2.f / MAX(left, 0.25f);
	fast = (real)sqrt(want_i * want_i + want_j * want_j);
	if (fast < 0.05f)
	{
		result->yaw = run;
		result->speed = 0.f;
		return TRUE;
	}
	result->yaw = (real)atan2(want_j, want_i);
	result->speed = MIN(fast / (BOT_RUN_SPEED * (body->speed_scale > 0.f ? body->speed_scale : 1.f)), 1.f);
	return TRUE;
}

/* How far back from a gap's edge the run at it starts. A body is at its
   full speed in a dozen ticks and two thirds of a unit; the second's run it
   used to take (1.6) was a walk back and a run forward of a second and a
   half over what the routes reckon a gap at, for every gap come to from the
   side. (HALO_BOT_RUNUP: another, to try) */
#define BOT_GAP_RUNUP 1.f
/* ... and from how far back of its edge a gap is jumped, when the jump from there carries */
#define BOT_GAP_STEEP 0.74f /* the ground a few ticks' run ahead as steep as this (its normal's up; 0.707 is where a body slides), */
#define BOT_GAP_FALLS 0.05f /* ... or this much lower than underfoot: jumped from here, */
#define BOT_GAP_STEEP_BACK 0.7f /* ... this near the edge */
static boolean bot_move_ground_goes(struct bot_body const *body, real_point3d const *ahead)
{
	real ground;
	/* (floor that falls away: not no floor at all, which is an edge, and
	   is jumped from nearer (the rule before this one). Taken for the
	   same, a bot going cornerwise at the gap between the ramp's head and
	   the camouflage on Longest jumped half a unit before the edge, at the
	   first notch in it, and came down a hand short of the far side.) */
	if (!bot_ground(ahead, 0.8f, body->unit_index, &ground))
		return FALSE;
	return ground < body->position.z - BOT_GAP_FALLS || bot_ground_level(ahead, 0.8f, body->unit_index) < BOT_GAP_STEEP;
}
#define BOT_GAP_EARLY 0.8f
static real bot_move_runup(void)
{
	static int asked;
	static real runup = BOT_GAP_RUNUP;
	if (!asked)
	{
		asked = TRUE;
		if (getenv("HALO_BOT_RUNUP"))
			runup = (real)atof(getenv("HALO_BOT_RUNUP"));
	}
	return runup;
}

/* how much higher than where a run begins the ground gets between there and
   where it ends (0: none, or no ground to be found) */
#define BOT_RUNUP_RISE 0.2f
static real bot_move_rise_between(struct bot_body const *body, real_point3d const *from, real_point3d const *to)
{
	real length = bot_distance2d(from, to);
	real first = 0.f, most = 0.f;
	real along;
	boolean known = FALSE;
	for (along = 0.f; along <= length + 0.01f; along += 0.15f)
	{
		real_point3d top;
		real ground;
		real part = length > 0.f ? along / length : 0.f;
		top.x = from->x + (to->x - from->x) * part;
		top.y = from->y + (to->y - from->y) * part;
		top.z = MAX(from->z, to->z) + 0.8f;
		if (!bot_ground(&top, 1.8f, body->unit_index, &ground))
			continue;
		if (!known)
		{
			known = TRUE;
			first = most = ground;
		}
		else if (ground > most)
			most = ground;
	}
	return most - first;
}

/* travelling a reachability: TRUE while it's still under way */
static boolean bot_move_travel_on(struct bot_mover *bot, struct bot_body const *body, struct bot_move_result *result)
{
	struct bot_move_state *move = &bot->move;
	struct aas_reach const *reach;
	real_point3d const *position = &body->position;
	long area;
	long limit;

	if (move->reach == NONE || move->reach_type != move->type)
	{
		move->reach = NONE;
		return FALSE;
	}
	reach = aas_reach(move->type, move->reach);
	area = aas_point_area(move->type, position);
	result->travel = reach->travel;
	move->reach_ticks++;
	move->reach_peak = MAX(move->reach_peak, position->z);
	/* (testing: how fast it was going when it left the ground, how long
	   after it set about this that was, and the slowest it was until then) */
	if (move->reach_ticks <= 1)
		move->reach_off_ticks = 0;
	if (move->reach_off_ticks == 0)
	{
		real going = (real)sqrt(body->velocity.i * body->velocity.i + body->velocity.j * body->velocity.j) * BOT_TICKS_PER_SECOND;
		move->reach_slowest = move->reach_ticks <= 1 ? going : MIN(move->reach_slowest, going);
		move->reach_off_speed = going;
	}
	/* made it (off a ladder: onto the floor at its top, whichever of the
	   areas up there it is. The one the ladder's tied to may be a step or
	   two on, round the hole it comes up through: from where we stand is
	   as good a place to go on from) */
	if ((area == reach->to && (move->reach_started || reach->travel == _aas_travel_walk) &&
			/* (a gap come at slowly, to come down on something small: not
			   made while it is still in the air over it. It was, and from
			   there the bot was walked on at a run toward wherever the way
			   went next, came down a step past the place and was off the far
			   side of it: the little gap half way up to the arch of Battle
			   Creek, every time) */
			!(reach->travel == _aas_travel_gap && move->reach_pace > 0.f && move->reach_pace < 1.f && !move->reach_no_jump &&
				position->z > reach->end.z + 0.3f && fabs(body->velocity.k) > 0.008f)) ||
		(reach->travel == _aas_travel_ladder && move->reach_started && area != NONE && area != reach->from &&
			fabs(position->z - reach->left.z) < 0.2f && fabs(body->velocity.k) < 0.008f))
	{
		/* (a jump, a gap: how long it took, against how long the routes
		   reckon it. One that is a walk back, a run-up and a hop of four
		   seconds to save a step is dearer than they think, and is taken
		   again and again for the step it saves: what it took over what it
		   was reckoned at is added to it, half the difference a time, and
		   taken off again as it goes better) */
		if ((reach->travel == _aas_travel_gap || reach->travel == _aas_travel_jump) && move->reach_started && move->reach_type == AAS_FOOT)
		{
			real took = (real)move->reach_ticks / BOT_TICKS_PER_SECOND - reach->time - 0.5f;
			real had = aas_reach_handicap_of(AAS_FOOT, move->reach);
			if (had < 100.f)
			{
				real now = had + 0.5f * (MIN(MAX(took, 0.f), 6.f) - had);
				/* (to the half second: down, when it is coming off. Rounded
				   to the nearest, the last half second never came off, and a
				   jump once taken slowly was half a second dearer for good.) */
				now = now < had && bot_refined_is(32768) ? (real)floor(now * 2.f) * 0.5f : (real)floor(now * 2.f + 0.5f) * 0.5f;
				if (now != had)
				{
					aas_reach_handicap(AAS_FOOT, move->reach, now);
					if (bot_logging())
						bot_log("bot%ld: foot reachability %ld reckoned %.1f s dearer (it was %.1f; this time %.1f over)", bot->index, move->reach, now, had, took);
				}
			}
			/* (... and whether it stays where it came down) */
			move->landed_reach = move->reach;
			move->landed_until = game_time_get() + BOT_SECONDS(1.5f);
			move->landed_z = reach->end.z;
		}
		/* off the top of a ladder: on away from it a few steps before
		   turning (straight round toward the goal, over the hole it comes
		   up through, is straight back down it) */
		if (reach->travel == _aas_travel_ladder)
		{
			move->exit_yaw = bot_yaw_to(&reach->edge, &reach->left);
			move->exit_ticks = BOT_SECONDS(0.4f);
			move->back_point = reach->edge;
			move->back_until = game_time_get() + BOT_SECONDS(6.f);
		}
		/* (testing: each one made, as each one failed is written, to say how
		   often a way of each kind is got across) */
		if (reach->travel != _aas_travel_walk && move->reach_started && bot_logging())
			bot_log("bot%ld: %s reachability %ld (travel %d) made in %ld ticks, careful %d%s, over %.2f pace %.2f%s; off at %.2f after %ld ticks, slowest %.2f, rise %.2f", bot->index, move->reach_type == AAS_VEHICLE ? "vehicle" : "foot", move->reach,
				reach->travel, move->reach_ticks, (int)reach->careful, move->reach_hole == 2 ? " hole" : "",
				(real)move->reach_ticks / BOT_TICKS_PER_SECOND - reach->time, move->reach_pace, move->reach_phase == 1 && move->reach_runup_ticks == 0 ? " run-on" : "",
				move->reach_off_speed, move->reach_off_ticks, move->reach_slowest, reach->left.z - reach->edge.z);
		if (reach->travel == _aas_travel_jump && move->reach_started && bot_logging())
			bot_log("bot%ld: jump up %.2f made", bot->index, reach->left.z - reach->edge.z);
		move->reach = NONE;
		return FALSE;
	}
	/* too long, or fell */
	switch (reach->travel)
	{
	case _aas_travel_ladder:
		limit = BOT_SECONDS(4.f + (reach->left.z - reach->edge.z) * 2.f + bot_distance2d(&reach->start, &reach->edge)) +
			(move->reach_rise_ticks > 0 ? MIN(move->reach_rise_ticks, BOT_SECONDS(12.f)) : 0);
		break;
	case _aas_travel_teleport:
		limit = BOT_SECONDS(5.f + bot_distance2d(&reach->start, &reach->edge));
		break;
	default:
		limit = BOT_SECONDS(3.f + bot_distance2d(&reach->start, &reach->edge) / 2.f);
		/* (... and the time of the fall, where it comes down a long way: a
		   jump off the top of Damnation was out of time in the air; and of
		   a run that is taken slowly) */
		if (reach->edge.z > reach->end.z)
			limit += BOT_SECONDS((real)sqrt(2.f * (reach->edge.z - reach->end.z) / BOT_JUMP_GRAVITY));
		if (reach->travel == _aas_travel_gap && move->reach_pace > 0.f && move->reach_pace < 1.f)
			limit += BOT_SECONDS(MIN(1.f / move->reach_pace - 1.f, 3.f));
		break;
	}
	/* (a gap's time is from the start of its run: walking back from the
	   edge for the run-up is up to three seconds more, and a bot that came
	   to the edge at a walk used to be out of time in the air over the far
	   side, the jump as good as made) */
	if (move->reach_ticks - move->reach_runup_ticks > limit)
	{
		bot_move_reach_failed(bot, "too long");
		return FALSE;
	}
	if (move->reach_started && (reach->travel == _aas_travel_jump || reach->travel == _aas_travel_gap || reach->travel == _aas_travel_ladder) &&
		position->z < MIN(reach->edge.z, reach->end.z) - 1.2f)
	{
		bot_move_reach_failed(bot, "fell");
		return FALSE;
	}
	/* fell off on the way to it (off the walkway on the run-up, knocked
	   off the ledge): it's up there, we're not */
	if (!move->reach_started && reach->travel != _aas_travel_teleport &&
		(position->z < MIN(reach->start.z, reach->edge.z) - 1.2f ||
			(position->z < MIN(reach->start.z, reach->edge.z) - 0.6f && fabs(body->velocity.k) < 0.008f)))
	{
		/* (... or standing on something lower than is hopped back up from:
		   it stepped off the ledge it was to jump from, and went through
		   the run-up and the jump on the floor below, at the wall) */
		/* (Once is a slip, and nothing to hold against the way. The same
		   one twice running is the way: off the end of the rock it starts
		   from each time it is come to. That was let pass for ever, and the
		   bot went round and up and off again until it was sent somewhere
		   else.) */
		if (move->slip_reach == move->reach && game_time_get() - move->slip_tick < BOT_SECONDS(20.f) && bot_refined_is(16384))
		{
			move->slip_reach = NONE;
			bot_move_reach_failed(bot, "fell off before it, twice");
			return FALSE;
		}
		move->slip_reach = move->reach;
		move->slip_tick = game_time_get();
		move->reach = NONE;
		return FALSE;
	}
	/* (and after the jump: down, standing, and well under both ends of it) */
	if (move->reach_started && (reach->travel == _aas_travel_jump || reach->travel == _aas_travel_gap) &&
		position->z < MIN(reach->edge.z, reach->end.z) - 0.6f && fabs(body->velocity.k) < 0.008f && move->reach_ticks > 12)
	{
		bot_move_reach_failed(bot, "fell");
		return FALSE;
	}
	/* (Up, and down on its feet on some other floor of the place it was
	   jumping onto, as high as where it meant to land: that is the jump
	   made, a step further on. It was steered back for the one area the
	   jump is tied to, against the way it was going, and on the top of a
	   rock that is a string of little areas it went off the side of the
	   rock doing it, jumped at the rock again from the ground and was out
	   of time: the jump at a run onto the rock by the west base of Battle
	   Creek, one time in four.) */
	if (bot_refined_is(16384) && move->reach_started && (reach->travel == _aas_travel_jump || reach->travel == _aas_travel_gap) &&
		area != NONE && area != reach->from && area != reach->to && !body->airborne && position->z > reach->end.z - 0.25f &&
		position->z < aas_area(move->type, area)->z1 + 0.25f && move->reach_ticks > 8)
	{
		if (bot_logging())
			bot_log("bot%ld: foot reachability %ld (travel %d) made in %ld ticks, past where it lands (area %ld)", bot->index, move->reach, reach->travel,
				move->reach_ticks, area);
		move->reach = NONE;
		return FALSE;
	}
	/* (landed somewhere else altogether: plan again from there. Landed:
	   not in the air over it on the way across, where a new plan would
	   steer us back for its own run-up and drop us in the gap) */
	if (move->reach_started && reach->travel != _aas_travel_ladder && reach->travel != _aas_travel_teleport && area != NONE && area != reach->from &&
		area != reach->to && (move->reach_ticks - move->reach_runup_ticks > BOT_SECONDS(1.5f) || position->z < reach->end.z - 1.f) &&
		(position->z < aas_area(move->type, area)->z1 + 0.12f && fabs(body->velocity.k) < 0.008f))
	{
		/* well below where it goes (a drop that doesn't land there, a
		   ledge too narrow to stand on): not a way */
		if (position->z < reach->end.z - 1.f)
		{
			bot_move_reach_failed(bot, "landed below");
			return FALSE;
		}
		move->reach = NONE;
		return FALSE;
	}

	result->moving = TRUE;
	result->speed = 1.f;
	switch (reach->travel)
	{
	case _aas_travel_jump:
	{
		/* at the ledge's foot: run at it and jump when close; up in the air,
		   tuck the legs up to clear the lip */
		real_point3d const *landing = &reach->left;
		real to_landing = bot_distance2d(position, landing);
		boolean ready;
		/* (which way it is jumped: from where it is taken, in the area below,
		   to where it lands; its edge is the foot of the ledge, right under) */
		real face = bot_yaw_to(bot_distance2d(&reach->start, landing) > 0.12f ? &reach->start : &reach->edge, landing);
		/* (whether its legs are tucked up for it: for a high one) */
		boolean tuck = bot_move_tucks(landing->z - reach->edge.z);
		/* (what it is jumping up onto, looked at once: a floor that goes on,
		   or the top of a rock a step across with a drop beyond it) */
		if (move->reach_pace <= 0.f)
		{
			real_point3d beyond = *landing;
			real ground;
			beyond.x += (real)cos(face) * 0.7f;
			beyond.y += (real)sin(face) * 0.7f;
			beyond.z += 0.5f;
			move->reach_pace = bot_ground(&beyond, 1.f, body->unit_index, &ground) ? 1.f : 0.5f;
			move->reach_beyond = 0.15f;
		}
		/* At a run. A bot went to the foot of the ledge, stood, turned to
		   face it and jumped: half of all the jumps up were left the ground
		   from under a unit a second. Someone who knows the map is running
		   at the ledge and jumps from where the run has brought him, far
		   enough off that he is as high as its top when he gets to it: he
		   is going up for a third of a second before his feet are over a
		   ledge 0.6 up, and three quarters of a unit along by then. So,
		   for the better bots, running at it within a little of square: on,
		   and the jump when the face is that far off and a little; not from
		   further than the fall would have him under it again. Onto a
		   floor that goes on, not the top of a rock. */
		if (!move->reach_started && bot->fluency > 0.25f && bot_move_fluent(2) && move->reach_pace >= 1.f && move->reach_ticks < BOT_SECONDS(2.5f) && to_landing > 0.5f &&
			landing->z > position->z + 0.1f && fabs(position->z - reach->edge.z) < 0.25f)
		{
			/* (how square to the face the straight way to the landing is: up
			   to 45 degrees off it the ledge is gone at where the bot comes
			   from, not from the place before its middle) */
			real at_it = bot_yaw_to(position, landing);
			real square = (real)cos(bot_angle_difference(at_it, face));
			real off_face = (reach->edge.x - position->x) * (real)cos(face) + (reach->edge.y - position->y) * (real)sin(face) - BOT_BODY_RADIUS;
			if (square > 0.72f - 0.1f * bot->fluency && navm_line_arrives(position, &reach->edge, 0.15f, 0.f, FALSE))
			{
				real going = (real)sqrt(body->velocity.i * body->velocity.i + body->velocity.j * body->velocity.j) * BOT_TICKS_PER_SECOND;
				real heading = (real)atan2(body->velocity.j, body->velocity.i);
				real closing = going * (real)cos(bot_angle_difference(heading, face));
				real clears = landing->z - position->z + 0.06f - (tuck ? 0.12f : 0.f);
				real under = BOT_JUMP_VELOCITY * BOT_JUMP_VELOCITY - 2.f * BOT_JUMP_GRAVITY * clears;
				real up_at = under > 0.f ? (BOT_JUMP_VELOCITY - (real)sqrt(under)) / BOT_JUMP_GRAVITY : 1.f;
				real down_at = under > 0.f ? (BOT_JUMP_VELOCITY + (real)sqrt(under)) / BOT_JUMP_GRAVITY : 0.f;
				result->yaw = at_it;
				if (body->can_jump && (!tuck || (move->crouch_ticks <= 0 && move->stand_ticks >= BOT_STAND_TICKS)) && going > 1.9f && closing > 1.4f &&
					under > 0.6f && fabs(bot_angle_difference(heading, at_it)) < 0.3f)
				{
					real face_in = off_face / closing; /* (seconds to the face, as it is going) */
					if (face_in >= up_at + 0.04f && face_in <= MIN(down_at - 0.2f, up_at + 0.22f))
					{
						/* (and where that brings it down is on the ledge: not past the end of it) */
						real_point3d down = *position;
						real ground;
						down.x += (real)cos(heading) * going * (face_in + 0.2f);
						down.y += (real)sin(heading) * going * (face_in + 0.2f);
						down.z = landing->z + 0.5f;
						if (bot_ground(&down, 0.8f, body->unit_index, &ground) && fabs(ground - landing->z) < 0.2f)
						{
							result->jump = TRUE;
							if (tuck)
								move->crouch_ticks = 30;
							move->reach_started = TRUE;
							if (bot_logging() && getenv("HALO_BOT_REACHLOG"))
								bot_log("bot%ld: a jump at a run, %.2f from the face at %.2f a second (%.2f s off it; its feet are that high in %.2f)", bot->index, off_face,
									closing, face_in, up_at);
							break;
						}
					}
				}
				/* (not at the place to jump from yet: on, at a run) */
				if (off_face > MAX(closing, 1.9f) * (up_at + 0.04f))
					break;
			}
		}
		if (!move->reach_started && bot_distance2d(position, &reach->start) > 0.5f && to_landing > 1.2f)
		{
			result->yaw = bot_yaw_to(position, &reach->start);
			break;
		}
		/* (come at from along the foot of it: round to in front of it first.
		   Jumped at from where it stood, going along the ledge, the bot went
		   up, along the face of it and off its end: the way onto the east
		   base of Battle Creek, from the strip beside the porch roof) */
		if (!move->reach_started && to_landing < 1.3f && to_landing > 0.25f && bot_distance2d(position, &reach->start) > 0.15f &&
			bot_distance2d(&reach->start, landing) > 0.12f && move->reach_ticks < BOT_SECONDS(1.5f) &&
			fabs(bot_angle_difference(bot_yaw_to(position, landing), face)) > 0.4f)
		{
			result->yaw = bot_yaw_to(position, &reach->start);
			result->speed = bot_distance2d(position, &reach->start) < 0.5f ? 0.5f : 1.f;
			break;
		}
		result->yaw = bot_yaw_to(position, move->reach_started && position->z > landing->z - 0.1f ? &reach->end : landing);
		/* (onto something small: at a walk, and brought down on it. At a
		   run it is up, over the top of the rock and down the far side) */
		if (move->reach_pace < 1.f)
		{
			if (!move->reach_started)
				result->speed = to_landing < 1.f ? 0.55f : 1.f;
			else if (bot_move_gap_steer(bot, body, reach, result))
				break;
		}
		/* (going across it: a moment to be going at it first) */
		/* (going across it: a moment to be going at it first; and less across
		   it for a high one, which is gone up beside the face of, a second
		   long, before the feet are over the top) */
		if (!move->reach_started && to_landing < 0.8f && to_landing > 0.2f && move->reach_settle_ticks < BOT_JUMP_SETTLE_TICKS &&
			bot_move_jump_across(body, landing) > (landing->z - position->z > 0.55f ? 0.3f : BOT_JUMP_ACROSS_MOST))
		{
			move->reach_settle_ticks++;
			result->speed = 0.4f;
			break;
		}
		/* (The jump is taken by a body that is on the ground and standing
		   up. The legs' tuck is what gets the feet over a high ledge: the
		   game lifts a body as it folds in the air, a fifth of a unit, and
		   not one that left the ground folded already. Asked for a tick
		   after it came down from the last try, still tucked, the jump
		   was no jump or a low one, and the bot went at the face of a
		   rock five times and was given up on) */
		/* (... which only a jump with the legs tucked wants) */
		ready = body->can_jump && (!tuck || (move->crouch_ticks <= 0 && move->stand_ticks >= BOT_STAND_TICKS));
		if (!move->reach_started && to_landing < 0.8f)
		{
			if (!ready && move->reach_ticks < BOT_SECONDS(2.f))
			{
				result->speed = to_landing < 0.45f ? 0.f : 0.4f;
				break;
			}
			result->jump = TRUE;
			if (tuck)
				move->crouch_ticks = 30;
			move->reach_started = TRUE;
		}
		else if (!move->reach_started && move->reach_ticks > BOT_SECONDS(1.5f) && to_landing < 1.3f && ready)
		{
			/* can't get closer (a lip in the way): jump from here */
			result->jump = TRUE;
			if (tuck)
				move->crouch_ticks = 30;
			move->reach_started = TRUE;
		}
		/* bounced off: again (down, and on its feet) */
		if (move->reach_started && position->z < landing->z - 0.3f && move->jump_cooldown <= 0 && move->reach_ticks > 20 &&
			to_landing < 1.f)
		{
			if (ready)
			{
				/* (bounced off a low one with the legs down: tucked, this time) */
				result->jump = TRUE;
				if (move->crouch_ticks <= 0 && move->stand_ticks >= BOT_STAND_TICKS)
					move->crouch_ticks = 30;
			}
			else if (body->can_jump && to_landing < 0.45f)
				result->speed = 0.f; /* (a step back from the face to go at it from: stood still, at least) */
		}
		break;
	}
	case _aas_travel_drop:
		/* to the edge and off it */
		if (!move->reach_started && bot_distance2d(position, &reach->edge) > 0.35f && position->z > reach->edge.z - 0.3f)
		{
			result->yaw = bot_yaw_to(position, &reach->edge);
			/* (onto a narrow ledge: up to the edge at a walk) */
			if (reach->careful && bot_distance2d(position, &reach->edge) < 1.2f)
				result->speed = 0.4f;
		}
		else
		{
			move->reach_started = TRUE;
			result->yaw = bot_yaw_to(position, &reach->end);
			if (reach->careful)
				result->speed = 0.3f;
		}
		/* walking off: past the edge, straight on */
		if (bot_distance2d(position, &reach->edge) < 0.2f)
			result->yaw = bot_yaw_to(&reach->edge, &reach->end);
		break;
	case _aas_travel_gap:
	{
		/* a run-up from the start, straight at the far side, and off the
		   edge with a jump */
		real_point3d const *landing = &reach->left;
		real run = bot_yaw_to(&reach->edge, landing);
		boolean steered = FALSE;
		/* (A gap to be walked over, the areas say: a slot a hand wide. Some
		   are, and some are as wide as a body the way they are crossed, a
		   crack between two decks of Boarding Action taken cornerwise: the
		   bot walked into it and went down it, every time. One with no
		   floor in it a step down is jumped, as any gap.) */
		if (reach->careful == AAS_WALKOVER && !move->reach_hole)
		{
			long part;
			move->reach_hole = 1;
			for (part = 1; part <= 3 && move->reach_hole == 1; part++)
			{
				real_point3d over;
				real ground;
				over.x = reach->edge.x + (landing->x - reach->edge.x) * part * 0.25f;
				over.y = reach->edge.y + (landing->y - reach->edge.y) * part * 0.25f;
				over.z = MAX(reach->edge.z, landing->z) + 0.3f;
				if (!bot_ground(&over, 0.3f + 0.6f + (real)fabs(reach->edge.z - landing->z), body->unit_index, &ground))
					move->reach_hole = 2;
			}
		}
		if (reach->careful == AAS_WALKOVER && move->reach_hole != 2)
		{
			/* floor all the way: to this side of it, then straight over */
			if (!move->reach_started && bot_distance2d(position, &reach->edge) > 0.25f)
				result->yaw = bot_yaw_to(position, &reach->edge);
			else
			{
				move->reach_started = TRUE;
				result->yaw = bot_yaw_to(position, bot_distance2d(position, landing) > 0.15f ? landing : &reach->end);
				/* (up a step that isn't being walked up: a hop) */
				/* (as soon as it is at it and on its feet: it was on every
				   twentieth tick, wherever the bot was by then, and a step
				   a foot high was bumped at for two seconds) */
				if (landing->z > position->z + 0.12f && bot_distance2d(position, landing) < 0.6f &&
					(bot_refined_is(4) ? body->can_jump && move->jump_cooldown <= 0 && bot_distance2d(position, landing) < 0.45f : (move->reach_ticks % 20) == 10))
				{
					result->jump = TRUE;
				}
			}
			break;
		}
		if (!move->reach_started)
		{
			real_point3d ahead = *position;
			real past = (position->x - reach->edge.x) * (real)cos(run) + (position->y - reach->edge.y) * (real)sin(run);
			real across = (real)fabs(-(position->x - reach->edge.x) * (real)sin(run) + (position->y - reach->edge.y) * (real)cos(run));
			real toward, enough;
			boolean early = FALSE;
			/* a run-up: back from the edge, in line with the jump, as far as
			   there's room (up to a second's run); then at it flat out */
			if (move->reach_phase == 0)
			{
				real_point3d runup = reach->edge;
				real back;
				/* (how fast it is to be taken first: one that is left at half
				   speed, or walked off, wants a step or two behind it, not
				   the second's run of a long jump. A legs' length of floor is
				   speed enough for any of them) */
				if (move->reach_pace <= 0.f)
					bot_move_gap_takeoff(bot, body, reach);
				for (back = bot_move_runup() * (move->reach_pace < 1.f || move->reach_no_jump ? MAX(0.5f, move->reach_pace) : 1.f); back > 0.3f; back -= 0.3f)
				{
					runup.x = reach->edge.x - (real)cos(run) * back;
					runup.y = reach->edge.y - (real)sin(run) * back;
					runup.z = reach->edge.z + 0.2f;
					/* (floor to run on, and not up over a rise on the way to
					   the edge: come over the crown of a rock at a run a body
					   is in the air, and the game takes no jump from it. The
					   run at the wall by the west base of Battle Creek began on
					   the shelf below the top of the rock it is jumped from,
					   and every one of them ran off the rock with the button
					   down) */
					if (navm_line_walkable(&reach->edge, &runup, 0.15f, 0.f) && bot_move_rise_between(body, &runup, &reach->edge) < BOT_RUNUP_RISE)
						break;
				}
				move->reach_no_room = back <= 0.3f;
				if (bot_distance2d(position, &runup) > 0.25f && move->reach_ticks < BOT_SECONDS(3.f) && (past > -back + 0.2f || across > 0.3f))
				{
					result->yaw = bot_yaw_to(position, &runup);
					result->speed = bot_distance2d(position, &runup) < 0.6f ? 0.5f : 1.f;
					move->reach_runup_ticks = move->reach_ticks;
					break;
				}
				move->reach_phase = 1;
			}
			result->yaw = bot_yaw_to(position, landing);
			if (move->reach_pace <= 0.f)
				bot_move_gap_takeoff(bot, body, reach);
			/* (off the edge itself, or a moment before where the ground
			   ends short of it: every bit of floor left unrun is so much
			   less jump, and the long ones need it all) */
			/* (the game takes a moment between the jump's being asked for and
			   the feet leaving the ground, and off the edge by then there is
			   no jump: three or four ticks' run ahead) */
			ahead.x += (real)cos(result->yaw) * BOT_GAP_JUMP_AHEAD;
			ahead.y += (real)sin(result->yaw) * BOT_GAP_JUMP_AHEAD;
			ahead.z += 0.3f;
			/* (something knee high in the way: up in good time, to be at the
			   top of the jump over it, not at the bottom against it) */
			/* (... and going at it: the speed along the line of the jump, not
			   any speed. Come to sideways, or backing off from a fight, a bot
			   was at the edge and moving, jumped where it stood and went down
			   the gap. At the edge without the run: back for it, while there
			   is the time) */
			toward = (body->velocity.i * (real)cos(run) + body->velocity.j * (real)sin(run)) * BOT_TICKS_PER_SECOND;
			enough = move->reach_no_jump || move->reach_no_room ? 0.3f : 0.7f * BOT_RUN_SPEED * (move->reach_pace > 0.f ? move->reach_pace : 1.f);
			if (past > -0.1f && toward < enough && move->reach_ticks < BOT_SECONDS(3.f))
			{
				/* (nowhere to run from: from where it stands, but going
				   the right way when it does) */
				if (!move->reach_no_room)
					move->reach_phase = 0;
				break;
			}
			/* (... and from further back than its edge, where the jump from
			   here carries: the game takes a jump only from a body that is
			   on the ground and standing on something it can stand on, and
			   the last step of a rock, rounded off as rocks are, is neither.
			   Run to its edge, the button was pressed where nothing came of
			   it and the bot ran off the end: the rock by the west base of
			   Battle Creek, from which the wall by the base is jumped onto,
			   and most of the gaps that were "fallen" at on rock. A gap run
			   at flat out, onto floor that goes on.) */
			if (!move->reach_no_jump && move->reach_pace > 0.f && reach->careful != AAS_HOP && past > -BOT_GAP_EARLY && past <= -0.1f &&
				toward >= enough && across < 0.3f)
			{
				real rise = landing->z - position->z;
				real under = BOT_JUMP_VELOCITY * BOT_JUMP_VELOCITY - 2.f * BOT_JUMP_GRAVITY * rise;
				if (under > 0.f)
				{
					/* (as fast as it is let go: its pace, or flat out; and
					   to where it is meant to come down, with a step to
					   spare where the floor goes on and none where it is
					   to be landed on exactly) */
					real air = (BOT_JUMP_VELOCITY + (real)sqrt(under)) / BOT_JUMP_GRAVITY;
					real most = BOT_RUN_SPEED * MIN(move->reach_pace, 1.f) * (body->speed_scale > 0.f ? MIN(body->speed_scale, 1.f) : 1.f);
					real faster = MIN(1.05f * air, MAX(most - toward, 0.f));
					early = toward * air + 0.5f * faster * air >= bot_distance2d(position, landing) + move->reach_beyond + (move->reach_pace >= 1.f ? 0.2f : 0.05f);
				}
			}
			/* (... or a moment before where the ground falls away or turns
			   too steep to stand on: the end of a rock is round, a body
			   that comes over the round of it at a run is in the air, the
			   game takes a jump from nobody who is, and the edge the areas
			   give is down the round. The rock by the west base of Battle
			   Creek again: of the bots that fell at the jump from it to the
			   porch, every one had run to the edge with the button down and
			   gone no higher than it stood, 19 times in 16 games. It left
			   the ground a fifth of a unit short of the edge, every time.) */
			if (past > -0.1f || (reach->careful == AAS_HOP && past > -0.7f && toward > 1.3f) ||
				(past > -0.4f && !bot_ground(&ahead, 0.8f, body->unit_index, NULL) && toward > 1.5f) || early ||
				(bot_refined_is(16384) && !move->reach_no_jump && move->reach_pace > 0.f && reach->careful != AAS_HOP && past > -BOT_GAP_STEEP_BACK && toward > 1.2f &&
					body->can_jump && bot_move_ground_goes(body, &ahead)))
			{
				/* (going across it, with floor to stand on meanwhile: a
				   moment to be going at it first) */
				if (move->reach_settle_ticks < BOT_JUMP_SETTLE_TICKS && bot_move_jump_across(body, landing) > BOT_JUMP_ACROSS_MOST &&
					bot_distance2d(position, landing) > 0.2f && bot_ground(&ahead, 0.8f, body->unit_index, NULL))
				{
					move->reach_settle_ticks++;
					result->speed = 0.4f;
					break;
				}
				result->jump = !move->reach_no_jump;
				/* (this jump whatever was jumped a moment ago, a hop at
				   something in the way on the run-up: held back for that, it's
				   a run straight off the edge) */
				move->jump_cooldown = 0;
				/* (asked for with floor still to run on, and not taken: on
				   with the run, the button down) */
				if (early && past <= -0.1f && !body->can_jump && body->velocity.k * BOT_TICKS_PER_SECOND < 1.f)
					break;
				move->reach_started = TRUE;
				/* (over something: legs tucked up to clear it) */
				if (reach->careful == AAS_HOP)
					move->crouch_ticks = 30;
			}
		}
		else
		{
			result->yaw = bot_yaw_to(position, position->z > landing->z - 0.1f ? &reach->end : landing);
			steered = bot_move_gap_steer(bot, body, reach, result);
			/* onto something higher: legs tucked up to clear its edge, as we
			   come to it and no sooner. A crouched body slows in the air as
			   it does on the ground, and a jump tucked from the start came
			   down a step short of a wide gap's far side (Chill Out's
			   broken bridge, from the low end). Not onto the level: tucked
			   there, the small gaps of high walkways were missed */
			if (landing->z > reach->edge.z + 0.15f && move->crouch_ticks <= 0 && position->z > reach->edge.z + 0.1f &&
				bot_distance2d(position, landing) < 0.9f && bot_distance2d(position, landing) > 0.15f)
				move->crouch_ticks = BOT_GAP_TUCK_TICKS;
		}
		/* (the jumps are worked out for the usual running speed: running
		   faster than that, slayer's bonus for the much killed, eased off
		   to it, or the jump carries clean over where it's going) */
		if (!steered)
		{
			if (body->speed_scale > 1.05f && (move->reach_started || move->reach_phase == 1))
				result->speed = 1.f / body->speed_scale;
			/* (... and no faster than lands it where it is going: bot_move_gap_takeoff) */
			if (move->reach_pace > 0.f && move->reach_pace < 1.f && (move->reach_started || move->reach_phase == 1) && !(reach->careful == AAS_WALKOVER && move->reach_hole != 2))
				result->speed *= move->reach_pace;
		}
		break;
	}
	case _aas_travel_ladder:
	{
		/* the foot, then into it facing it until our feet are over the
		   floor at the top; then over onto it */
		real into = bot_wrap((real)atan2(-reach->face_j, -reach->face_i));
		if (!move->reach_started)
		{
			if (bot_distance2d(position, &reach->edge) > 0.35f)
			{
				result->yaw = bot_yaw_to(position, &reach->edge);
				break;
			}
			move->reach_started = TRUE;
		}
		/* still going up? */
		if (position->z > move->reach_rise_z + 0.03f)
		{
			move->reach_rise_z = position->z;
			move->reach_rise_ticks = move->reach_ticks;
		}
		if (position->z < reach->left.z + 0.05f && move->reach_started && position->z > reach->edge.z + 0.5f &&
			move->reach_ticks - move->reach_rise_ticks > BOT_SECONDS(0.5f))
		{
			/* not going any higher, and not up yet (the ladder ends under a
			   lip, a railing at the top): try getting off over the top, then
			   off to either side, a while each, holding on (facing it, turned
			   no more than it lets us) */
			long tries = (move->reach_ticks - move->reach_rise_ticks - BOT_SECONDS(0.5f)) / BOT_SECONDS(1.2f);
			long way = tries % 4;
			/* (standing on a floor up here that isn't the one we set out
			   from: the ladder's taken us somewhere, if not where we thought;
			   the way on is worked out from here) */
			{
				long here = aas_point_area(move->type, position);
				if (here != NONE && here != reach->from && fabs(aas_area(move->type, here)->center.z - position->z) < 0.4f)
				{
					move->reach = NONE;
					return FALSE;
				}
			}
			real_point3d exit = reach->left;
			real turn;
			if (way == 1 || way == 2)
			{
				real side = way == 1 ? 1.f : -1.f;
				exit.x = reach->edge.x + reach->face_i * 0.2f - reach->face_j * side * 1.2f;
				exit.y = reach->edge.y + reach->face_j * 0.2f + reach->face_i * side * 1.2f;
			}
			else if (way == 3)
			{
				exit.x = reach->edge.x - reach->face_i * 0.9f;
				exit.y = reach->edge.y - reach->face_j * 0.9f;
				if ((move->reach_ticks % 15) == 0)
					result->jump = TRUE;
			}
			result->yaw = bot_yaw_to(position, &exit);
			turn = bot_angle_difference(result->yaw, into);
			result->look = TRUE;
			result->look_yaw = bot_wrap(into + PIN(turn, -1.f, 1.f));
			result->look_pitch = 0.f;
			/* (the time limit counts from when we stopped rising) */
			if (tries >= 8)
			{
				bot_move_reach_failed(bot, "stuck on the ladder");
				return FALSE;
			}
		}
		else if (position->z < reach->left.z + 0.05f)
		{
			/* into it, and across to its middle */
			real_point3d face = reach->edge;
			face.x -= reach->face_i * 0.45f;
			face.y -= reach->face_j * 0.45f;
			result->yaw = bot_yaw_to(position, &face);
			/* not going up (a short one, a lip at its foot): hop onto it */
			if (move->reach_ticks > BOT_SECONDS(1.f) && move->reach_peak < reach->edge.z + 0.15f && (move->reach_ticks % 20) == 0)
			{
				result->jump = TRUE;
			}
			result->look = TRUE;
			result->look_yaw = into;
			result->look_pitch = 0.25f;
		}
		else
		{
			/* over the top: toward the floor there, never facing back off
			   the ladder (that lets go) */
			real want = bot_yaw_to(position, &reach->left);
			real turn = bot_angle_difference(want, into);
			result->yaw = want;
			result->look = TRUE;
			result->look_yaw = bot_wrap(into + PIN(turn, -1.f, 1.f));
			result->look_pitch = 0.f;
			if (bot_distance2d(position, &reach->left) < 0.3f)
				result->yaw = bot_yaw_to(position, &reach->end);
		}
		break;
	}
	case _aas_travel_teleport:
		/* right onto the pad (the middle of it: it takes us within half a
		   unit). Not sent anywhere a moment after: we came onto it by
		   teleporter (a pad both ways: it won't send us back until we've
		   stepped off it), so off a step and on again */
		if (move->reach_started && bot_move_distance(position, &reach->edge) > 3.f)
		{
			move->reach = NONE; /* sent somewhere: done */
			return FALSE;
		}
		if (move->reach_phase == 1)
		{
			/* (off it far enough for the game to forget we came by it: a
			   unit and a bit, whichever way there's floor to walk on: back
			   the way to it first, then round from there) */
			real_point3d off = reach->start;
			real back = bot_yaw_to(&reach->edge, &reach->start);
			long i;
			int found = bot_distance2d(&reach->start, &reach->edge) >= 1.4f;
			for (i = 0; i < 8 && !found; i++)
			{
				real yaw = back + (real)((i + 1) / 2) * ((i & 1) ? 1.f : -1.f) * 0.7853982f;
				off.x = reach->edge.x + (real)cos(yaw) * 1.4f;
				off.y = reach->edge.y + (real)sin(yaw) * 1.4f;
				off.z = reach->edge.z;
				found = navm_line_walkable(&reach->edge, &off, 0.1f, 0.f);
			}
			if (!found)
			{
				/* (a pad with no room round it: another way from here) */
				bot_move_avoid(move, move->reach, BOT_SECONDS(12));
				move->reach = NONE;
				return FALSE;
			}
			result->yaw = bot_yaw_to(position, &off);
			if (bot_move_distance(position, &reach->edge) > 1.3f)
			{
				move->reach_phase = 0;
				move->wait_ticks = 0;
			}
			break;
		}
		result->yaw = bot_yaw_to(position, &reach->edge);
		result->speed = bot_move_distance(position, &reach->edge) < 1.f ? 0.5f : 1.f;
		if (bot_move_distance(position, &reach->edge) < 0.6f)
		{
			move->reach_started = TRUE;
			if (++move->wait_ticks > BOT_SECONDS(0.7f))
				move->reach_phase = 1;
		}
		break;
	default:
		move->reach = NONE;
		return FALSE;
	}
	/* (testing: the tick the jump is asked for, or a gap that is walked is begun) */
	if (move->reach_off_ticks == 0 && (result->jump || move->reach_started))
		move->reach_off_ticks = move->reach_ticks;
	result->path_yaw = result->yaw;
	return TRUE;
}

/* (a travel that is over, or failed, leaves the route's walk as it would
   have been: its kind stayed on the result, and for that tick the fight
   took the bot for one on a jump or a ladder, and did not strafe) */
static boolean bot_move_travel(struct bot_mover *bot, struct bot_body const *body, struct bot_move_result *result)
{
	long travel = result->travel;
	if (bot_move_travel_on(bot, body, result))
		return TRUE;
	result->travel = travel;
	return FALSE;
}

/* someone else is on this reachability (a ladder) */
static boolean bot_move_reach_busy(struct bot_mover const *bot, long reach)
{
	long i;
	for (i = 0; i < BOT_MAXIMUM_PLAYERS; i++)
	{
		struct bot_mover const *other = bot_mover_get(i);
		if (other && other != bot && other->unit_index != NONE && other->move.reach == reach && other->move.reach_type == AAS_FOOT &&
			bot_unit_alive(other->unit_index))
		{
			/* on it, or at its foot first */
			struct aas_reach const *r = aas_reach(AAS_FOOT, reach);
			if (other->move.reach_started || bot_distance2d(&object_get(other->unit_index)->object.position, &r->edge) < 0.8f)
				return TRUE;
		}
	}
	return FALSE;
}

boolean bot_move_committed(struct bot_mover const *bot)
{
	return bot->move.reach != NONE && bot->move.reach_type == AAS_FOOT && bot->move.type == AAS_FOOT &&
		(bot->move.reach_started || aas_reach(AAS_FOOT, bot->move.reach)->travel != _aas_travel_walk);
}

/* ---------- driving */

static void bot_move_drive(struct bot_mover *bot, struct bot_body const *body, real_point3d const *target, real_point3d const *after,
	struct bot_move_result *result)
{
	struct bot_move_state *move = &bot->move;
	real speed = (real)sqrt(body->velocity.i * body->velocity.i + body->velocity.j * body->velocity.j) * BOT_TICKS_PER_SECOND;
	real heading = body->facing_yaw;
	real_point3d aim = *target;
	real distance = bot_distance2d(&body->position, target);
	real want;
	real off;

	/* pure pursuit: close to the corner, round it toward the next */
	if (after && distance < MAX(2.f, speed * 0.7f))
	{
		real blend = 1.f - distance / MAX(2.f, speed * 0.7f);
		aim.x = target->x + (after->x - target->x) * blend;
		aim.y = target->y + (after->y - target->y) * blend;
	}
	want = bot_yaw_to(&body->position, &aim);
	off = (real)fabs(bot_angle_difference(want, heading));
	result->moving = TRUE;
	result->steer_yaw = want;
	/* hard turns slowly; a sharp corner coming up fast: ease off */
	result->throttle = off < 0.5f ? 1.f : off < 1.2f ? 0.75f : 0.55f;
	if (after && distance < speed * 1.2f)
	{
		real corner = (real)fabs(bot_angle_difference(bot_yaw_to(target, after), bot_yaw_to(&body->position, target)));
		if (corner > 1.f && speed > 3.f)
			result->throttle = speed > 5.f ? -0.3f : 0.2f;
	}
	/* one of ours on foot in the way: round them, or stop */
	if (bot_body_friendly_ahead(body, &body->position, heading, 3.f + speed * 0.8f, 1.2f + bot->teamwork * 0.8f))
	{
		long i;
		boolean round = FALSE;
		for (i = -1; i <= 1; i += 2)
		{
			real yaw = bot_wrap(want + i * 0.7f);
			if (!bot_body_friendly_ahead(body, &body->position, yaw, 3.f + speed * 0.8f, 1.5f) &&
				bot_probe(&body->position, yaw, 3.f, body->vehicle_index) > 2.8f)
			{
				result->steer_yaw = yaw;
				round = TRUE;
				break;
			}
		}
		if (!round)
			result->throttle = speed > 1.f ? -0.5f : 0.f;
	}
	/* stuck (against something, hung up on a rock): back out, wheels
	   turned the other way, then on again */
	if (move->reverse_ticks > 0)
	{
		move->reverse_ticks--;
		result->throttle = -1.f;
		result->steer_yaw = bot_wrap(heading + move->reverse_steer);
		return;
	}
	if (result->throttle > 0.3f && speed < 0.5f)
	{
		if (++move->progress_ticks > BOT_SECONDS(1.2f))
		{
			move->progress_ticks = 0;
			move->reverse_ticks = BOT_SECONDS(1.f + bot_mover_random(bot) * 0.6f);
			move->reverse_steer = bot_angle_difference(want, heading) > 0.f ? -0.6f : 0.6f;
			move->stuck_count++;
			if (move->stuck_count >= 4)
				result->get_out = TRUE; /* not getting anywhere in this: on foot */
		}
	}
	else
	{
		move->progress_ticks = 0;
		if (speed > 2.f)
			move->stuck_count = 0;
	}
}

/* a flying vehicle (a banshee): straight there, over things */
static void bot_move_fly(struct bot_mover *bot, struct bot_body const *body, real_point3d const *goal, struct bot_move_result *result)
{
	real_point3d aim = *goal;
	real distance = bot_distance2d(&body->position, goal);
	real over = body->position.z - (goal->z + 2.f);
	/* (no lower than it is while it is on its way, over whatever is between;
	   and down to the place once that is near enough to come down to: three
	   along for one down, from further off the higher it is) */
	if (over > 0.f && distance < 20.f + 3.f * over)
		aim.z = goal->z + 2.f;
	else
		aim.z = MAX(goal->z + 2.f, body->position.z) + (distance > 20.f ? 2.f : 0.f);
	result->moving = distance > 2.f;
	result->steer_yaw = bot_yaw_to(&body->position, &aim);
	result->look = TRUE;
	result->look_yaw = result->steer_yaw;
	result->look_pitch = (real)atan2(aim.z - body->position.z, MAX(distance, 1.f)) * 0.6f;
	result->throttle = 1.f;
	/* a wall ahead: up */
	if (bot_probe(&body->position, result->steer_yaw, 8.f, body->vehicle_index) < 7.f)
		result->look_pitch = 0.6f;
	(void)bot;
}

/* ---------- the goal */

/* On a map too large for the areas to cover (bot_country.c): on foot off
   the areas, or bound for a place they do not lead to. Over the ground the
   vehicles cross, to somewhere not too far to walk: the last steps to a
   flag from where the Warthog stopped, back to the Warthog, to the next
   one. TRUE when the result is filled in (with a way, or with none: not
   straight at the place, which on these maps is over a cliff). */
#define BOT_FOOT_ACROSS_FARTHEST 220.f
static boolean bot_move_foot_across(real_point3d const *position, real_point3d const *to, real to_goal, struct bot_move_result *result)
{
	real_point3d next;
	boolean soon = FALSE, off = FALSE;
	if (!bot_nav_in_parts() || !bot_country_ready())
		return FALSE;
	if (to_goal > BOT_FOOT_ACROSS_FARTHEST)
	{
		result->no_route = TRUE;
		result->moving = FALSE;
		return TRUE;
	}
	if (bot_country_walk(position, to, &next, &soon, &off))
	{
		result->moving = TRUE;
		result->no_route = FALSE;
		result->yaw = bot_yaw_to(position, &next);
		result->path_yaw = result->yaw;
		return TRUE;
	}
	/* (not on the country at all, under a roof: as it was) */
	if (off)
		return FALSE;
	result->moving = FALSE;
	result->no_route = !soon;
	return TRUE;
}

void bot_move_to_goal(struct bot_mover *bot, struct bot_body const *body, struct bot_goal *goal, struct bot_move_result *result)
{
	struct bot_move_state *move = &bot->move;
	long type = body->vehicle_index != NONE && body->driving ? AAS_VEHICLE : AAS_FOOT;
	real_point3d const *position = &body->position;
	real_point3d spot = *position;
	real_point3d goal_spot;
	real_point3d corners[2];
	long corner_count;
	long area;
	long goal_area = NONE;
	real to_goal = bot_distance2d(position, &goal->origin);

	bot_move_fluency = bot_move_fluent(1) ? bot->fluency : 0.f;
	memset(result, 0, sizeof(*result));
	result->yaw = bot->yaw;
	result->path_yaw = bot->yaw;
	result->speed = 1.f;
	result->travel = _aas_travel_walk;
	/* (not asked the way for a while, a fight: the step aside it was in the
	   middle of, the way off a ladder's top, were for where it stood then) */
	if (game_time_get() - move->called_tick > 2 && bot_refined_is(32768))
	{
		move->unstick_ticks = 0;
		move->unstick_check = FALSE;
		move->exit_ticks = 0;
		move->dodge_ticks = 0;
	}
	move->called_tick = game_time_get();
	if (move->jump_cooldown > 0)
		move->jump_cooldown--;
	if (move->crouch_ticks > 0)
	{
		move->crouch_ticks--;
		move->stand_ticks = 0;
		if (move->crouch_ticks < 27)
			result->crouch = TRUE;
	}
	else if (move->stand_ticks < 1000)
		move->stand_ticks++;
	if (type != move->type)
	{
		move->type = type;
		move->reach = NONE;
		move->corridor_count = 0;
		move->stuck_count = 0;
		move->progress_ticks = 0;
	}

	/* there */
	if (to_goal < goal->radius && fabs(position->z - goal->origin.z) < 1.5f &&
		!(type == AAS_FOOT && move->reach != NONE && move->reach_type == AAS_FOOT && move->reach_started)) /* (not halfway up a ladder) */
	{
		result->arrived = TRUE;
		result->moving = FALSE;
		move->reach = NONE;
		return;
	}
	if (type == AAS_VEHICLE && body->vehicle_index != NONE && object_try_and_get(body->vehicle_index) &&
		bot_vehicle_flies(object_get(body->vehicle_index)->definition_index))
	{
		bot_move_fly(bot, body, &goal->origin, result);
		return;
	}
	if (!aas_ready())
	{
		result->moving = TRUE;
		result->yaw = bot_yaw_to(position, &goal->origin);
		result->steer_yaw = result->yaw;
		result->throttle = 1.f;
		return;
	}

	/* where we are */
	area = aas_point_area(type, position);
	if (area == NONE)
	{
		area = aas_nearest_area(type, position, type == AAS_VEHICLE ? 4.f : 2.f, &spot);
		move->off_mesh_ticks++;
	}
	else
		move->off_mesh_ticks = 0;
	if (area != NONE && area != move->area)
	{
		move->area_before = move->area;
		move->area = area;
	}

	/* (over a jump or a gap a moment ago, and well below where it came
	   down: it did not stay there. A place to land that nothing stands on,
	   a jump that carries past it: not the way it looked) */
	if (move->landed_reach != NONE)
	{
		if (type != AAS_FOOT || game_time_get() > move->landed_until || move->reach != NONE)
			move->landed_reach = NONE;
		else if (position->z < move->landed_z - 1.2f)
		{
			if (bot_logging())
				bot_log("bot%ld: foot reachability %ld made, and off the far side of it (at %.2f %.2f %.2f)", bot->index, move->landed_reach,
					position->x, position->y, position->z);
			if (!bot_move_crowded(bot))
				aas_reach_failed(AAS_FOOT, move->landed_reach);
			bot_move_avoid(move, move->landed_reach, BOT_SECONDS(30));
			move->landed_reach = NONE;
		}
	}
	/* a reachability under way */
	if (type == AAS_FOOT && bot_move_travel(bot, body, result))
	{
		move->progress_ticks = 0;
		/* (testing, HALO_BOT_REACHTRACE: every other tick of a jump, a gap,
		   a drop or a ladder, to see how one goes wrong) */
		if (result->travel != _aas_travel_walk && move->reach != NONE && (game_time_get() & 1) == 0 && bot_logging())
		{
			static int asked, on;
			if (!asked)
			{
				asked = TRUE;
				on = getenv("HALO_BOT_REACHTRACE") != NULL;
			}
			if (on)
				bot_log("reachtrace bot%ld t%ld reach %ld travel %d at %.2f %.2f %.2f going %.2f %.2f %.2f yaw %.2f speed %.2f%s%s phase %ld tick %ld crouch %ld", bot->index,
					(long)game_time_get(), move->reach, (int)result->travel, position->x, position->y, position->z, body->velocity.i * BOT_TICKS_PER_SECOND,
					body->velocity.j * BOT_TICKS_PER_SECOND, body->velocity.k * BOT_TICKS_PER_SECOND, result->yaw, result->speed, result->jump ? " JUMP" : "",
					move->reach_started ? " begun" : "", (long)move->reach_phase, move->reach_ticks, (long)move->crouch_ticks);
		}
		goto finish;
	}

	/* Where the goal is. A route is a search out from the goal's area, so
	   a goal that moves (someone being chased) is a new search each time
	   it crosses into another area, and from far off a search of most of
	   the map: from far off the route stays the one to where the goal was,
	   until it has moved a fifth as far as we are from it (four world
	   units at most). Near it, where the search is short, the goal itself. */
	if (move->route_goal_area != NONE && move->route_goal_type == type && to_goal > BOT_FAR_GOAL &&
		bot_move_distance(&goal->origin, &move->route_goal_origin) < MIN(4.f, to_goal * 0.2f))
	{
		goal_area = move->route_goal_area;
		goal_spot = move->route_goal_spot;
	}
	else
	{
		goal_area = aas_nearest_area(type, &goal->origin, type == AAS_VEHICLE ? 10.f : 3.f, &goal_spot);
		move->route_goal_area = goal_area;
		move->route_goal_type = type;
		move->route_goal_spot = goal_spot;
		move->route_goal_origin = goal->origin;
	}
	if (type == AAS_FOOT)
		goal->area = goal_area;
	/* (off the areas, there is no route by them: the one planned when it
	   last stood on one is not what it is on now. Left as it was, its first
	   link was the one given up, for everyone, when the bot went round in
	   circles or stuck out here.) */
	if (area == NONE || goal_area == NONE)
		move->corridor_count = 0;
	if (type == AAS_FOOT && (area == NONE || goal_area == NONE) && bot_move_foot_across(position, &goal->origin, to_goal, result))
		goto finish;
	if (area == NONE || goal_area == NONE)
	{
		/* off the map's areas altogether (on top of something, in the
		   air): straight at it, round whatever's in the way */
		result->no_route = goal_area == NONE;
		result->moving = TRUE;
		result->yaw = bot_yaw_to(position, area == NONE ? &goal->origin : &spot);
		if (area != NONE && bot_distance2d(position, &spot) < 0.3f)
			result->yaw = bot_yaw_to(position, &goal->origin);
		/* (On a map too large for the areas to cover, what is straight
		   ahead off the areas is a canyon as often as not: the flag of
		   Coldsnap's north-west base stands out over one. Not a step where
		   there is no ground to step onto.) */
		if (type == AAS_FOOT && bot_nav_in_parts())
		{
			real reach;
			for (reach = 1.2f; reach <= 2.5f; reach += 1.2f)
			{
				real_point3d ahead = *position;
				real ground;
				ahead.x += (real)cos(result->yaw) * reach;
				ahead.y += (real)sin(result->yaw) * reach;
				ahead.z += 1.f;
				if (!bot_ground(&ahead, 3.5f, NONE, &ground))
				{
					result->moving = FALSE;
					result->no_route = TRUE;
					break;
				}
			}
		}
		if (type == AAS_VEHICLE)
		{
			bot_move_drive(bot, body, area != NONE ? &spot : &goal->origin, NULL, result);
			if (goal_area == NONE && to_goal < 25.f)
				result->get_out = TRUE; /* somewhere a vehicle doesn't go */
			return;
		}
		goto finish;
	}

	/* the route */
	bot_move_plan(move, area, &spot, goal_area);
	if (type == AAS_FOOT && area != goal_area && move->corridor_count == 0 && bot_move_foot_across(position, &goal->origin, to_goal, result))
		goto finish;
	if (area != goal_area && move->corridor_count == 0)
	{
		/* no way there (from here) */
		result->no_route = TRUE;
		result->moving = FALSE;
		if (type == AAS_VEHICLE)
			result->get_out = to_goal < 40.f;
		return;
	}
	{
		/* the goal itself when it's in its area; else the nearest spot of
		   it (an item by a wall, off the areas) */
		real_point3d final = goal_spot;
		struct aas_area const *g = aas_area(type, goal_area);
		if (goal->origin.x > g->x0 && goal->origin.x < g->x1 && goal->origin.y > g->y0 && goal->origin.y < g->y1)
			final = goal->origin;
		if (move->corridor_count > 0)
		{
			struct aas_reach const *last = aas_reach(type, move->corridor[move->corridor_count - 1]);
			if (last->travel != _aas_travel_walk)
				final = last->start;
			else if (!move->corridor_complete)
				final = last->end; /* (as far as the route goes) */
		}
		corner_count = bot_move_corners(move, area == aas_point_area(type, position) ? position : &spot, &final, corners, 2);
		/* by the goal (in its area, or in the last one before it): the
		   goal itself, even off the areas (items by walls) */
		if (area == goal_area && to_goal < 2.5f)
		{
			corners[0] = goal->origin;
			corner_count = 1;
		}
	}
	/* off the areas (by a wall): back onto them first. Once we're about
	   over the spot that's as near as it gets (on a rock beside the way,
	   above it), on along the way from there, not back and forth over
	   the spot; and stuck up there a while, a hop down off it */
	if (area != aas_point_area(type, position) && !(area == goal_area && to_goal < 2.5f))
	{
		real off = bot_distance2d(position, &spot);
		if (off > 0.6f)
			move->spot_latched = FALSE;
		else if (off < 0.25f)
			move->spot_latched = TRUE;
		if (!move->spot_latched && off > 0.15f)
		{
			corners[1] = corners[0];
			corners[0] = spot;
			corner_count = 2;
		}
		if (move->off_mesh_ticks > BOT_SECONDS(0.6f) && move->spot_latched && position->z > spot.z + NAVM_STEP &&
			(move->off_mesh_ticks % BOT_SECONDS(0.7f)) == 0)
		{
			result->jump = TRUE;
		}
	}
	else
		move->spot_latched = FALSE;

	/* a reachability that's more than a walk, next: at its start, take it */
	if (type == AAS_FOOT && move->corridor_count > 0)
	{
		long r = move->corridor[move->corridor_count - 1];
		struct aas_reach const *reach = aas_reach(type, r);
		/* a gap we're running at already, in line with the jump: over it
		   from here, with the run we have (not up to its edge, back for a
		   run-up and at it again) */
		if (move->corridor_count <= 3 && bot_move_gap_run_on(body, reach) && !bot_move_gap_busy(bot, r))
		{
			move->wait_ticks = 0;
			move->reach = r;
			move->reach_type = type;
			move->progress_ticks = 0;
			move->stuck_count = 0;
			if (bot_refined_is(32768))
				move->unstick_ticks = 0; /* (nor is a step aside on the way to it kept for the far side of it) */
			move->reach_ticks = 0;
			move->reach_started = FALSE;
			move->reach_phase = 1;
			move->reach_runup_ticks = 0;
			move->reach_peak = position->z;
			move->reach_pace = 0.f;
			move->reach_no_jump = FALSE;
			move->reach_hole = 0;
			move->reach_no_room = FALSE;
			move->reach_settle_ticks = 0;
			move->reach_rise_ticks = 0;
			move->reach_rise_z = position->z;
			if (getenv("HALO_BOT_REACHLOG"))
				bot_log("bot%ld takes reach %ld travel %d at %.1f %.1f %.1f (at a run)", bot->index, r, reach->travel, reach->edge.x, reach->edge.y, reach->edge.z);
			if (bot_move_travel(bot, body, result))
				goto finish;
		}
		else
		if (reach->travel != _aas_travel_walk && reach->from == area && move->corridor_count == 1 &&
			(bot_distance2d(position, &reach->start) < 0.6f || corner_count <= 1 || bot_same_point(&corners[0], &reach->start)))
		{
			/* a ladder takes one at a time: wait a step back for whoever's on
			   it (a while; then another way) */
			if (reach->travel == _aas_travel_ladder && bot_distance2d(position, &reach->start) < 1.5f && bot_move_reach_busy(bot, r))
			{
				result->moving = FALSE;
				result->look = TRUE;
				result->look_yaw = bot_yaw_to(position, &reach->edge);
				result->look_pitch = 0.3f;
				if (++move->wait_ticks > BOT_SECONDS(4.f))
				{
					/* (another way round, meanwhile) */
					bot_move_avoid(move, r, BOT_SECONDS(10));
					move->wait_ticks = 0;
				}
				return;
			}
			/* a gap is jumped one at a time too: a moment at its edge for
			   whoever's over it or where it lands (then anyway) */
			if (reach->travel == _aas_travel_gap && reach->careful != AAS_WALKOVER && bot_distance2d(position, &reach->start) < 0.6f &&
				move->wait_ticks <= BOT_SECONDS(2.f) && bot_move_gap_busy(bot, r))
			{
				move->wait_ticks++;
				result->moving = FALSE;
				result->look = TRUE;
				result->look_yaw = bot_yaw_to(position, &reach->left);
				result->look_pitch = 0.f;
				return;
			}
			move->wait_ticks = 0;
			if (bot_distance2d(position, &reach->start) < 0.6f ||
				((reach->travel == _aas_travel_drop || reach->travel == _aas_travel_teleport) && bot_distance2d(position, &reach->start) < 1.5f))
			{
				move->reach = r;
				move->reach_type = type;
				move->progress_ticks = 0; /* (the approach's being stuck isn't this link's) */
				move->stuck_count = 0;
				if (bot_refined_is(32768))
					move->unstick_ticks = 0;
				move->reach_ticks = 0;
				move->reach_started = FALSE;
				move->reach_phase = 0;
				move->reach_runup_ticks = 0;
				move->reach_peak = position->z;
				move->reach_pace = 0.f;
				move->reach_no_jump = FALSE;
				move->reach_hole = 0;
				move->reach_no_room = FALSE;
				move->reach_settle_ticks = 0;
				move->reach_rise_ticks = 0;
				move->reach_rise_z = position->z;
				if (getenv("HALO_BOT_REACHLOG"))
					bot_log("bot%ld takes reach %ld travel %d at %.1f %.1f %.1f", bot->index, r, reach->travel, reach->edge.x, reach->edge.y, reach->edge.z);
				if (bot_move_travel(bot, body, result))
					goto finish;
			}
		}
	}

	if (type == AAS_VEHICLE)
	{
		/* a drop or a teleporter next (the route stops at its start): drive
		   on over the edge, or into it, not round and round its start */
		if (move->corridor_count == 1)
		{
			long r = move->corridor[0];
			struct aas_reach const *reach = aas_reach(type, r);
			if (reach->travel != _aas_travel_walk && reach->from == area && bot_distance2d(position, &reach->start) < 3.f)
			{
				bot_move_drive(bot, body, &reach->edge, &reach->end, result);
				if (++move->wait_ticks > BOT_SECONDS(6.f))
				{
					bot_move_avoid(move, r, BOT_SECONDS(20)); /* (it won't go: another way) */
					move->wait_ticks = 0;
				}
				return;
			}
		}
		move->wait_ticks = 0;
		/* (a straight way through gives its end twice: no corner after it.
		   Taken for one, the turn there was reckoned from due east, and a
		   vehicle bound any other way braked at the end of every straight.) */
		bot_move_drive(bot, body, &corners[0], corner_count > 1 && !(bot_same_point(&corners[0], &corners[1]) && bot_refined_is(32768)) ?
			&corners[1] : NULL, result);
		return;
	}
	result->moving = TRUE;
	result->yaw = bot_yaw_to(position, &corners[0]);
	result->path_yaw = result->yaw;
	/* (the last steps to where we are going: onto it, not round it) */
	if (to_goal < 2.f)
		result->exact = TRUE;
	if (move->human && corner_count > 1 && type == AAS_FOOT)
	{
		real to_corner = bot_distance2d(position, &corners[0]);
		real leg = bot_distance2d(&corners[0], &corners[1]);
		/* the eyes go round a corner before the feet: coming up to one, the
		   look is on round it, toward the way after (a person looks where
		   they are going next, and takes the corner sideways-on a step) */
		if (to_corner < BOT_LOOK_AHEAD && leg > 0.5f)
		{
			real after = bot_yaw_to(&corners[0], &corners[1]);
			result->path_yaw = bot_wrap(result->yaw + bot_angle_difference(after, result->yaw) * (1.f - to_corner / BOT_LOOK_AHEAD));
		}
		/* ... and the feet round it, not up to it and off at an angle: close
		   to the corner, for a spot as far along the way after it as we are
		   inside that (the turn is a curve of a few steps), where the floor
		   between here and there is clear of walls and edges */
		/* (not onto the start of a jump, a gap, a ladder or a drop: those
		   are come at along the line they are taken on) */
		if (to_corner < BOT_CORNER_ROUND && leg > 0.3f &&
			!(move->corridor_count > 0 && aas_reach(type, move->corridor[move->corridor_count - 1])->travel != _aas_travel_walk &&
				bot_distance2d(&corners[1], &aas_reach(type, move->corridor[move->corridor_count - 1])->start) < 0.05f))
		{
			real along = MIN(leg, BOT_CORNER_ROUND - to_corner) / leg;
			real_point3d spot;
			long nearer;
			/* (as far along the way after the corner as the floor from here
			   is clear to: where it is not clear to all of that, to half of
			   it, to a quarter. It was all or nothing, and a corner whose
			   inside the line across just clipped was steered at, then
			   round, then at again, from one tick to the next: the feet
			   wagged into every such turn) */
			for (nearer = 0; nearer < (bot_refined_is(2) ? 3 : 1); nearer++)
			{
				spot.x = corners[0].x + (corners[1].x - corners[0].x) * along;
				spot.y = corners[0].y + (corners[1].y - corners[0].y) * along;
				spot.z = corners[0].z + (corners[1].z - corners[0].z) * along;
				if (fabs(spot.z - position->z) < 0.6f && navm_line_arrives(position, &spot, 0.3f, 0.35f, FALSE))
				{
					result->yaw = bot_yaw_to(position, &spot);
					break;
				}
				along *= 0.5f;
				if (along * leg < 0.12f)
					break;
			}
		}
	}
	/* (testing, with HALO_BOT_BOXTRACE: a turn right round in its walk, and
	   what changed with it: the place it is going to, the area that is in,
	   the first link of its way) */
	{
		static real was_yaw[32];
		static long was_first[32], was_goal_area[32], was_tick[32], was_area[32];
		static real_point3d was_goal[32];
		long k = bot->index & 31;
		long first = move->corridor_count > 0 ? move->corridor[0] : NONE;
		if (getenv("HALO_BOT_BOXTRACE") && was_tick[k] == game_time_get() - 1 && fabs(bot_angle_difference(result->yaw, was_yaw[k])) > 2.2f)
			bot_log("uturn bot%ld t%ld at %.2f %.2f %.2f: yaw %.2f -> %.2f, goal moved %.2f, goal area %ld -> %ld, area %ld -> %ld, first %ld (travel %d to %ld) -> %ld (travel %d to %ld), corridor %ld, avoiding %d",
				bot->index, (long)game_time_get(), position->x, position->y, position->z, was_yaw[k], result->yaw, bot_move_distance(&goal->origin, &was_goal[k]),
				was_goal_area[k], goal_area, was_area[k], area, was_first[k], was_first[k] != NONE ? (int)aas_reach(type, was_first[k])->travel : -1,
				was_first[k] != NONE ? aas_reach(type, was_first[k])->to : NONE, first, first != NONE ? (int)aas_reach(type, first)->travel : -1,
				first != NONE ? aas_reach(type, first)->to : NONE, move->corridor_count, (int)bot_move_avoids_any(move));
		was_yaw[k] = result->yaw;
		was_first[k] = first;
		was_goal_area[k] = goal_area;
		was_goal[k] = goal->origin;
		was_tick[k] = game_time_get();
		was_area[k] = area;
	}
	move->corner = corners[0];
	/* A sharp turn with nothing past it to overrun onto: taken at a walk. A
	   body at a run goes a step past a corner before it is going the new
	   way; on a floor that is nothing, and on the shoulder of a rock or the
	   end of a ledge it is the step that goes off it (the turn onto the
	   rock under the arch of Battle Creek, come to from the south, was run
	   past onto a bank that slid the bot back to the bottom, time after
	   time). A person slows for those and for no others. */
	if (type == AAS_FOOT && corner_count > 1 && move->human)
	{
		real to_corner = bot_distance2d(position, &corners[0]);
		if (to_corner < BOT_TURN_CARE && bot_distance2d(&corners[0], &corners[1]) > 0.15f)
		{
			real going = to_corner > 0.15f ? bot_yaw_to(position, &corners[0]) : (real)atan2(body->velocity.j, body->velocity.i);
			real turn = (real)fabs(bot_angle_difference(bot_yaw_to(&corners[0], &corners[1]), going));
			if (turn > 0.9f)
			{
				real_point3d beyond = corners[0];
				beyond.x += (real)cos(going) * 0.5f;
				beyond.y += (real)sin(going) * 0.5f;
				beyond.z = MAX(position->z, corners[0].z) + 0.5f;
				if (bot_ground_level(&beyond, 1.4f, body->unit_index) < 0.7f)
					result->speed = MIN(result->speed, to_corner < 0.4f ? 0.45f : 0.65f);
			}
		}
	}
	/* close to the item: slow, so as not to run past it */
	if (goal->kind == _bot_move_goal_vehicle && to_goal < 1.5f)
		result->speed = to_goal > 0.6f ? 0.5f : 0.25f;
	/* a drop onto a narrow ledge coming up: slowing in good time (at a run
	   it's off the edge and clean over the ledge before there's any
	   stopping) */
	if (move->corridor_count > 0)
	{
		struct aas_reach const *next = aas_reach(type, move->corridor[move->corridor_count - 1]);
		if (next->travel != _aas_travel_walk && bot_distance2d(position, &next->start) < 2.f)
			result->exact = TRUE;
		/* (where a jump or a gap is taken from, with nothing past it the way
		   it is come to: up to it at a walk. The top of a rock a step
		   across, got onto at a run, is run off the far side of before the
		   jump from it is so much as begun) */
		if (type == AAS_FOOT && (next->travel == _aas_travel_gap || next->travel == _aas_travel_jump) && move->corridor_count == 1 &&
			bot_distance2d(position, &next->start) < BOT_TURN_CARE && bot_distance2d(position, &next->start) > 0.1f &&
			fabs(position->z - next->start.z) < 0.5f)
		{
			real going = bot_yaw_to(position, &next->start);
			real_point3d beyond = next->start;
			beyond.x += (real)cos(going) * 0.5f;
			beyond.y += (real)sin(going) * 0.5f;
			beyond.z = MAX(position->z, next->start.z) + 0.5f;
			if (bot_ground_level(&beyond, 1.2f, body->unit_index) < 0.7f)
				result->speed = MIN(result->speed, bot_distance2d(position, &next->start) < 0.4f ? 0.45f : 0.65f);
		}
		if (next->travel == _aas_travel_drop && next->careful && bot_distance2d(position, &next->edge) < 1.5f &&
			fabs(position->z - next->edge.z) < 1.f)
		{
			result->speed = MIN(result->speed > 0.f ? result->speed : 1.f, 0.45f);
		}
	}
	/* (A jump, a gap, a ladder or a drop a few steps on by the way, through
	   the little areas a rock is made of: the way is narrow hereabouts.
	   bots.c weaves a bot that is going somewhere with someone in sight,
	   and a bot going up to the rocket launcher on the arch of Battle Creek
	   with a fight below was weaved off the rocks: thirty seconds on the
	   way up, down twice, and shot.) */
	if (type == AAS_FOOT && bot_refined_is(16384))
	{
		long i;
		for (i = move->corridor_count - 1; i >= 0 && i >= move->corridor_count - 8; i--)
		{
			struct aas_reach const *next = aas_reach(type, move->corridor[i]);
			if (next->travel == _aas_travel_walk)
				continue;
			/* (not by a ladder, whose foot is a floor like any other) */
			if (next->travel != _aas_travel_teleport && next->travel != _aas_travel_ladder && bot_distance2d(position, &next->start) < BOT_DELICATE &&
				fabs(position->z - next->start.z) < 1.5f)
			{
				result->delicate = TRUE;
			}
			break;
		}
		if (move->landed_reach != NONE && game_time_get() < move->landed_until)
			result->delicate = TRUE;
	}
	/* Coming to where a gap is jumped from, with nothing past it the way it
	   is come to: no faster than it can stop from by then. The rule above
	   is for the last area before the gap, and eases the stick; a body at a
	   run takes its time to slow, and the top of a rock is a string of
	   little areas, so that it was still at a run a step from the end. On
	   the rock by the west base of Battle Creek, whose far end is rounded
	   and slides, a bot came to the start of the jump to the porch at two
	   thirds of a run, could not stop for its run-up, slid off, went round
	   and did it again, every four seconds for as long as it was sent
	   there. On the ground the stick is let go until it is slow enough; in
	   the air (a hop at a lump on the way) that is all that can be done,
	   and is enough: a body in the air only gathers speed while it is
	   walked forward. (The gap: the first thing on the way that is not a
	   walk.) */
	if (type == AAS_FOOT && move->reach == NONE && result->moving && bot_refined_is(16384))
	{
		long i;
		for (i = move->corridor_count - 1; i >= 0 && i >= move->corridor_count - 8; i--)
		{
			struct aas_reach const *next = aas_reach(type, move->corridor[i]);
			real off, toward, going;
			if (next->travel == _aas_travel_walk)
				continue;
			off = bot_distance2d(position, &next->start);
			if (next->travel != _aas_travel_gap || next->careful == AAS_WALKOVER || off > BOT_STOP_BY_GAP || off < 0.05f ||
				fabs(position->z - next->start.z) > 1.f)
			{
				break;
			}
			going = bot_yaw_to(position, &next->start);
			toward = (body->velocity.i * (real)cos(going) + body->velocity.j * (real)sin(going)) * BOT_TICKS_PER_SECOND;
			/* (not one it is running at already along the line it is jumped
			   on: that is its run, and the jump is made from it or the gap's
			   own rules turn it back. Slowed there, the bots that ran along
			   the broken bridge of Chill Out at its break came to the break
			   with half their run and went off beside the far end.) */
			{
				real run = bot_yaw_to(&next->edge, &next->left);
				real heading = (real)atan2(body->velocity.j, body->velocity.i);
				real beside = (real)fabs(-(position->x - next->edge.x) * (real)sin(run) + (position->y - next->edge.y) * (real)cos(run));
				if (toward > 1.f && beside < BOT_STOP_ON_LINE && fabs(bot_angle_difference(heading, run)) < 0.35f)
					break;
			}
			if (toward > (real)sqrt(2.f * BOT_STOP_SLOWING * MAX(off - BOT_STOP_SHORT, 0.f)) + BOT_STOP_SPEED)
			{
				real_point3d beyond = next->start;
				beyond.x += (real)cos(going) * 0.5f;
				beyond.y += (real)sin(going) * 0.5f;
				beyond.z = MAX(position->z, next->start.z) + 0.5f;
				if (bot_ground_level(&beyond, 1.2f, body->unit_index) < 0.7f)
					result->speed = body->airborne ? 0.f : MIN(result->speed, 0.1f);
			}
			/* (... and the floor may end beside the way to it, not beyond
			   it: where it would be, going as it is going, by the time it
			   could have stopped. The corner of a deck the jump goes off
			   cornerwise, come to along the deck's side: the place it is
			   jumped from was still a step off, to one side, and the end
			   of the deck was under its feet.) */
			{
				real speed = (real)sqrt(body->velocity.i * body->velocity.i + body->velocity.j * body->velocity.j) * BOT_TICKS_PER_SECOND;
				if (speed > 0.8f && !body->airborne)
				{
					real_point3d there = *position;
					real reach_of = speed * speed / (2.f * BOT_STOP_SLOWING) + 0.12f;
					there.x += body->velocity.i * BOT_TICKS_PER_SECOND / speed * reach_of;
					there.y += body->velocity.j * BOT_TICKS_PER_SECOND / speed * reach_of;
					there.z += 0.3f;
					if (!bot_ground(&there, 0.3f + 0.7f, body->unit_index, NULL))
						result->speed = MIN(result->speed, 0.1f);
				}
			}
			break;
		}
	}

finish:
	if (type != AAS_FOOT)
		return;
	/* (testing, HALO_BOT_BOXTRACE=x0,y0,x1,y1: every other tick of whoever is
	   on foot in a box: where, how fast, which way it means to go and to
	   which corner, by which areas) */
	{
		static int asked, on;
		static real box[4];
		if (!asked)
		{
			asked = TRUE;
			on = getenv("HALO_BOT_BOXTRACE") != NULL && sscanf(getenv("HALO_BOT_BOXTRACE"), "%f,%f,%f,%f", &box[0], &box[1], &box[2], &box[3]) == 4;
		}
		if (on && (game_time_get() & 1) == 0 && position->x >= box[0] && position->x <= box[2] && position->y >= box[1] && position->y <= box[3])
		{
			bot_log("boxtrace bot%ld t%ld at %.2f %.2f %.2f going %.2f %.2f %.2f yaw %.2f speed %.2f%s%s area %ld (in %ld) reach %ld next %ld/%ld corner %.2f %.2f %.2f",
				bot->index, (long)game_time_get(), position->x, position->y, position->z, body->velocity.i * BOT_TICKS_PER_SECOND,
				body->velocity.j * BOT_TICKS_PER_SECOND, body->velocity.k * BOT_TICKS_PER_SECOND, result->yaw, result->speed, result->jump ? " JUMP" : "",
				result->moving ? "" : " still", area, aas_point_area(AAS_FOOT, position), move->reach,
				move->corridor_count > 0 ? move->corridor[move->corridor_count - 1] : NONE, move->corridor_count, move->corner.x, move->corner.y, move->corner.z);
		}
	}
	if (move->exit_ticks > 0 && move->reach == NONE)
	{
		move->exit_ticks--;
		result->yaw = move->exit_yaw;
		result->moving = TRUE;
	}
	/* people in the way */
	if (result->moving && move->reach == NONE)
	{
		result->yaw = bot_move_dodge(bot, body, result->yaw);
		/* a step just too high to walk up (a curb, a sill): hop it (not
		   off the far side of what the step is on) */
		if ((body->can_jump || !bot_refined_is(16)) && bot_move_step_ahead(body, result->yaw) && bot_move_hop_lands(body, result->yaw))
		{
			result->jump = TRUE;
			if (getenv("HALO_BOT_BOXTRACE"))
				bot_log("hop bot%ld t%ld: a step ahead, at %.2f %.2f %.2f", bot->index, (long)game_time_get(), position->x, position->y, position->z);
		}
	}
	/* going round in circles: moving, but no nearer the goal for a good
	   while (back and forth among the little areas at the mouth of a
	   crack, each with its own way on): that first way's no good. Nearer
	   by the way there, not as the crow flies (a way round that goes away
	   first is still getting there); a look every quarter second */
	if (move->reach != NONE || !result->moving || move->corridor_count == 0)
		move->circle_ticks = 0;
	else if (++move->circle_check >= 8)
	{
		/* (to the area the route is to, from the area we are in: the same
		   search as the last look's, while the goal is where it was) */
		float time = -1.f;
		real to_goal_now;
		if (area != NONE && goal_area != NONE && area != goal_area)
			aas_route_reach(move->type, area, position, goal_area, NULL, NULL, NONE, &time);
		else
			time = aas_travel_time(move->type, position, &goal->origin);
		to_goal_now = time >= 0.f ? time * aas_speed(move->type) : bot_distance2d(position, &goal->origin);
		move->circle_check = 0;
		if (move->circle_ticks == 0 || bot_distance2d(&goal->origin, &move->circle_goal) > 2.f || to_goal_now < move->circle_best - 1.f)
		{
			move->circle_goal = goal->origin;
			move->circle_best = to_goal_now;
			move->circle_ticks = 1;
		}
		else if ((move->circle_ticks += 8) > BOT_SECONDS(4.f))
		{
			if (bot_logging())
				bot_log("circling bot%ld at %.2f %.2f %.2f area %ld: giving up link %ld", bot->index, position->x, position->y, position->z,
					area, move->corridor[0]);
			if (!bot_move_crowded(bot))
				aas_reach_failed(move->type, move->corridor[0]);
			bot_move_avoid(move, move->corridor[0], BOT_SECONDS(12));
			move->circle_ticks = 0;
		}
	}
	/* Held up: walking, and hardly moving (a lip, a seam between two
	   faces, a corner clipped, a rock the areas did not know): a hop over it
	   at once, and a step toward the side with more room, as a person does
	   the moment they feel it; not a second of pushing at it first. */
	if (type == AAS_FOOT && move->human && result->moving && move->reach == NONE && move->unstick_ticks <= 0)
	{
		real expected = aas_speed(type) * BOT_TICK * (result->speed > 0.f ? result->speed : 1.f) * (body->speed_scale > 0.f ? body->speed_scale : 1.f);
		/* (on the ground: a body in the air goes as it was thrown, slowly
		   when its legs are tucked up, and is held up by nothing. Counted
		   as held up there, it was hopped again the tick it came down,
		   and went along a ledge it had just jumped onto in hops. One that
		   slips on a bank of rock is held up, and the hop and the step
		   aside are what get it off the bank) */
		if (move->stall_valid && (!body->airborne || !bot_refined_is(1)) && bot_distance2d(position, &move->stall_point) < expected * BOT_STALL_FRACTION)
			move->stall_ticks++;
		else
			move->stall_ticks = 0;
		if (move->stall_ticks >= BOT_STALL_TICKS)
		{
			real_point3d side;
			real room[2];
			long i;
			for (i = 0; i < 2; i++)
			{
				real yaw = bot_wrap(result->yaw + (i ? -1.2f : 1.2f));
				side = *position;
				side.x += (real)cos(yaw) * 0.4f;
				side.y += (real)sin(yaw) * 0.4f;
				room[i] = navm_wall_clearance(&side, NULL);
			}
			move->stall_ticks = 0;
			result->jump = TRUE;
			if (getenv("HALO_BOT_BOXTRACE"))
				bot_log("hop bot%ld t%ld: held up, at %.2f %.2f %.2f", bot->index, (long)game_time_get(), position->x, position->y, position->z);
			move->unstick_ticks = 5;
			move->unstick_yaw = bot_wrap(result->yaw + (room[0] >= room[1] ? 0.7f : -0.7f));
			/* (Held up again where it was held up a moment ago: the hop and
			   the half step did not get it past. A good step out from
			   whatever it is, to the side with more room, and at it from
			   there. At the foot of the ladder by the hangar of Boarding
			   Action a bot that comes to it along the wall is held by the
			   ladder's own side, a hand from where it is taken: it hopped
			   there four seconds, gave the way up, and came back to do it
			   again, eleven times in a game. Bots with someone in sight
			   were weaved clear of it; one with nobody about was not.) */
			if (bot_refined_is(16384))
			{
				if (move->held_count > 0 && game_time_get() - move->held_tick < BOT_SECONDS(3.f) && bot_distance2d(position, &move->held_point) < 0.6f)
				{
					move->held_count++;
					move->unstick_ticks = 10 + 4 * MIN(move->held_count, 3);
					move->unstick_yaw = bot_wrap(result->yaw + (room[0] >= room[1] ? 1.f : -1.f) * (move->held_count & 1 ? 1.8f : 2.4f));
				}
				else
					move->held_count = 1;
				move->held_tick = game_time_get();
				move->held_point = *position;
			}
		}
	}
	else
		move->stall_ticks = 0;
	/* ... and on a jump, a gap, a drop or a ladder, pushing and not moving at
	   all (up, down or along) for half a second: it is not going to start
	   from here (come at from a fight, from the wrong side of a rail).
	   Given up at once, for another way, not after three seconds of pushing
	   at it. */
	if (type == AAS_FOOT && move->human && result->moving && move->reach != NONE && result->speed > 0.5f)
	{
		real expected = aas_speed(type) * BOT_TICK * result->speed * (body->speed_scale > 0.f ? body->speed_scale : 1.f);
		if (move->stall_valid && distance3d(position, &move->stall_point) < expected * 0.15f)
			move->reach_stall_ticks++;
		else
			move->reach_stall_ticks = 0;
		/* (pushing at something on the way to where it is going, which is
		   higher than its feet: the bank of a stream at the foot of a way
		   across it, a lip a hand too high to walk up. A hop at it, as a
		   walk that is held up gets, before it is given up) */
		if (move->reach_stall_ticks == BOT_REACH_STALL_TICKS / 3 && move->reach != NONE && move->jump_cooldown <= 0)
		{
			struct aas_reach const *held = aas_reach(move->reach_type, move->reach);
			real_point3d const *to = move->reach_started ? &held->left : &held->edge;
			if ((held->travel == _aas_travel_gap || held->travel == _aas_travel_jump) && to->z > position->z + 0.12f &&
				bot_distance2d(position, to) < 1.2f)
			{
				result->jump = TRUE;
				move->crouch_ticks = 20;
				if (bot_logging())
					bot_log("bot%ld: a hop at what holds it on reachability %ld (%.2f up to it)", bot->index, move->reach, to->z - position->z);
			}
		}
		if (move->reach_stall_ticks >= BOT_REACH_STALL_TICKS)
		{
			move->reach_stall_ticks = 0;
			bot_move_reach_failed(bot, "stalled");
		}
	}
	else
		move->reach_stall_ticks = 0;
	/* (how far from where it was last looked at it has been since) */
	move->progress_path = MAX(move->progress_path, bot_distance2d(position, &move->progress_point));
	move->stall_point = *position;
	move->stall_valid = result->moving;
	/* (testing, HALO_BOT_WALKTRACE: a walk that is being held up, every third tick) */
	if (type == AAS_FOOT && result->moving && move->reach == NONE && (move->stuck_count > 0 || move->progress_ticks > 12) && (game_time_get() % 3) == 0 && bot_logging())
	{
		static int asked, on;
		if (!asked)
		{
			asked = TRUE;
			on = getenv("HALO_BOT_WALKTRACE") != NULL;
		}
		if (on && bot_distance2d(position, &move->progress_point) < 0.5f)
		{
			real_point3d over = *position, ahead = *position;
			real ahead_z = 0.f;
			over.z += 0.3f;
			ahead.x += (real)cos(result->yaw) * 0.4f;
			ahead.y += (real)sin(result->yaw) * 0.4f;
			ahead.z += 0.9f;
			bot_ground(&ahead, 1.6f, body->unit_index, &ahead_z);
			bot_log("walktrace bot%ld t%ld at %.2f %.2f %.2f going %.2f %.2f %.2f yaw %.2f speed %.2f%s stuck %ld progress %ld: level %.2f, ahead %.2f up (level %.2f), wall %.2f, corridor %ld first %ld travel %d",
				bot->index, (long)game_time_get(), position->x, position->y, position->z, body->velocity.i * BOT_TICKS_PER_SECOND, body->velocity.j * BOT_TICKS_PER_SECOND,
				body->velocity.k * BOT_TICKS_PER_SECOND, result->yaw, result->speed, result->jump ? " JUMP" : "", move->stuck_count, move->progress_ticks,
				bot_ground_level(&over, 1.f, body->unit_index), ahead_z - position->z, bot_ground_level(&ahead, 1.6f, body->unit_index),
				navm_wall_clearance(position, NULL), move->corridor_count, move->corridor_count > 0 ? move->corridor[0] : NONE,
				move->corridor_count > 0 ? (int)aas_reach(type, move->corridor[0])->travel : -1);
		}
	}
	/* stuck: not getting anywhere for a second */
	if (result->moving)
	{
		if (++move->progress_ticks >= BOT_SECONDS(1.f))
		{
			/* (across the ground: a jump on the spot isn't getting anywhere;
			   and the sidestep below moves us without that being progress) */
			real to_goal_now = bot_distance2d(position, &goal->origin);
			int nearer = to_goal_now < move->progress_goal_distance - 0.5f; /* (closer to where we're going: that's progress) */
			move->progress_goal_distance = to_goal_now;
			/* (... and having gone nowhere: a bot that turned right round
			   and is back where it was a second ago has been a unit away
			   and come back, and is held up by nothing. It was hopped and
			   stepped aside for it, as one that is stuck is: a jump at
			   nothing after every turn about) */
			if (!nearer && (bot_distance2d(position, &move->progress_point) < 0.35f || move->unstick_check) &&
				!(bot_refined_is(8) && move->progress_path > BOT_STUCK_PATH && !move->unstick_check))
			{
				move->stuck_count++;
				if (bot_logging())
				{
					real_point3d over = *position;
					over.z += 0.3f;
					bot_log("stuck bot%ld at %.2f %.2f %.2f area %ld goal %.1f %.1f %.1f corridor %ld reach %ld count %ld level %.2f yaw %.2f moved %.2f speed %.2f", bot->index,
						position->x, position->y, position->z, area, goal->origin.x, goal->origin.y, goal->origin.z, move->corridor_count,
						move->reach, move->stuck_count, bot_ground_level(&over, 1.f, body->unit_index), result->yaw,
						bot_distance2d(position, &move->progress_point), result->speed);
				}
				/* a jump gets over most small things; then a step aside;
				   then that way's no good just now */
				result->jump = TRUE;
				if (move->stuck_count >= 2)
				{
					real sign = bot_mover_random(bot) < 0.5f ? -1.f : 1.f;
					move->unstick_ticks = BOT_SECONDS(0.5f);
					move->unstick_check = TRUE;
					move->unstick_yaw = bot_wrap(result->yaw + sign * 1.6f);
					if (!bot_move_direction_safe(body, move->unstick_yaw, 0.6f))
						move->unstick_yaw = bot_wrap(result->yaw - sign * 1.6f);
				}
				if (move->stuck_count >= 3)
				{
					if (move->reach != NONE)
						bot_move_reach_failed(bot, "stuck");
					else if (move->corridor_count > 0)
					{
						/* (a walk that gets bots stuck again and again, a crack
						   the areas think a body fits through: everyone learns,
						   as for a jump that fails; not in a crowd) */
						if (!bot_move_crowded(bot))
							aas_reach_failed(move->type, move->corridor[0]);
						bot_move_avoid(move, move->corridor[0], BOT_SECONDS(8));
					}
					move->stuck_count = 0;
				}
			}
			else
				move->stuck_count = 0;
			if (move->unstick_ticks <= 0)
				move->unstick_check = FALSE;
			move->progress_point = *position;
			move->progress_ticks = 0;
			move->progress_path = 0.f;
		}
		if (move->unstick_ticks > 0 && move->reach == NONE)
		{
			move->unstick_ticks--;
			result->yaw = move->unstick_yaw;
		}
	}
	else
		move->progress_ticks = 0;
	if (result->jump && move->jump_cooldown <= 0)
	{
		move->jump_cooldown = 20;
		move->jump_hold = BOT_JUMP_HOLD;
	}
	else if (move->jump_hold <= 0)
		result->jump = FALSE;
	else
	{
		/* The button held down until the body is off the ground, as a thumb
		   holds it. The game takes a jump only from a body that has stood
		   on the ground six ticks together, and one running over a rough
		   rock is off it every other step: the one tick the button was down
		   for was as often as not a tick the game would not take it, and the
		   bot ran off the edge of what it was to jump from with no jump at
		   all (most of the gaps it "fell" at). */
		move->jump_hold--;
		if (body->velocity.k * BOT_TICKS_PER_SECOND > 1.f || type != AAS_FOOT)
		{
			move->jump_hold = 0;
			result->jump = FALSE;
		}
		else
			result->jump = TRUE;
	}
}

void bot_move_debug(struct bot_mover *bot, struct bot_body const *body)
{
	struct bot_move_state const *move = &bot->move;
	struct aas_reach const *first = move->corridor_count > 0 ? aas_reach(move->type, move->corridor[0]) : NULL;
	struct aas_reach const *last = move->corridor_count > 0 ? aas_reach(move->type, move->corridor[move->corridor_count - 1]) : NULL;
	bot_log("move bot%ld at %.2f %.2f %.2f type %ld area %ld (in %ld) reach %ld corridor %ld corner %.2f %.2f first %d %.2f %.2f last %d start %.2f %.2f %.2f edge %.2f %.2f %.2f",
		bot->index, body->position.x, body->position.y, body->position.z, move->type, move->area,
		aas_point_area(move->type, &body->position), move->reach, move->corridor_count, move->corner.x, move->corner.y,
		first ? first->travel : -1, first ? first->start.x : 0.f, first ? first->start.y : 0.f,
		last ? last->travel : -1, last ? last->start.x : 0.f, last ? last->start.y : 0.f, last ? last->start.z : 0.f,
		last ? last->edge.x : 0.f, last ? last->edge.y : 0.f, last ? last->edge.z : 0.f);
}
