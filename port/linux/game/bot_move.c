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
	move->area = NONE;
	move->cost_slot = NONE;
	move->reach = NONE;
	move->corridor_goal_area = NONE;
	move->last_first_reach = NONE;
	move->last_goal_area = NONE;
	for (i = 0; i < BOT_AVOID_REACHES; i++)
		if (move->avoid_until[i] == 0)
			move->avoid_reach[i] = NONE;
}

/* a gap over a fall longer than this is for the daring (and for what's
   worth it): a jump that misses costs more than a walk round */
#define BOT_CAREFUL_GAP_FALL 3.5f
/* ... and so is one jumped down to a floor this much lower (Boarding
   Action's decks: over the rail and down, with space past the landing) */
#define BOT_CAREFUL_GAP_DOWN 1.5f

static boolean bot_move_avoiding(void *context, long reach)
{
	struct bot_move_state const *move = (struct bot_move_state const *)context;
	long now = game_time_get();
	long i;
	/* the careful don't take long drops, or jump gaps over one or down to
	   a much lower floor, when there's another way. A gap across to floor
	   about as high, with the floor not far under it (Chill Out's broken
	   bridge), is a way like any other: everyone jumps it when it's the
	   quickest, as people do */
	if (move->careful && move->type == AAS_FOOT)
	{
		struct aas_reach const *r = aas_reach(move->type, reach);
		if ((r->travel == _aas_travel_gap && r->careful != AAS_WALKOVER &&
				(aas_reach_fall(move->type, reach) > BOT_CAREFUL_GAP_FALL || r->edge.z - r->left.z > BOT_CAREFUL_GAP_DOWN)) ||
			(r->travel == _aas_travel_drop && r->edge.z - r->end.z > 2.5f))
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

static void bot_move_reach_failed(struct bot_mover *bot, char const *why)
{
	struct bot_move_state *move = &bot->move;
	if (bot_logging())
	{
		struct aas_reach const *reach = aas_reach(move->reach_type, move->reach);
		bot_log("bot%ld: %s reachability %ld (travel %d from %.1f %.1f %.1f to %.1f %.1f %.1f) failed: %s", bot->index,
			move->reach_type == AAS_VEHICLE ? "vehicle" : "foot", move->reach, reach->travel, reach->edge.x, reach->edge.y, reach->edge.z,
			reach->end.x, reach->end.y, reach->end.z, why);
	}
	/* in a crowd (someone in the way on the ladder, at the edge): not the
	   way's fault; this bot tries another way a while, nobody learns from it */
	if (!bot_move_crowded(bot))
		aas_reach_failed(move->reach_type, move->reach);
	bot_move_avoid(move, move->reach, BOT_SECONDS(bot_move_crowded(bot) ? 8 : 25));
	move->reach = NONE;
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
	if (past > -0.8f || past < -2.4f || across > 0.25f)
		return FALSE;
	along = (body->velocity.i * (real)cos(run) + body->velocity.j * (real)sin(run)) * BOT_TICKS_PER_SECOND;
	if (along < 1.9f)
		return FALSE;
	return navm_line_walkable(position, &reach->edge, 0.15f, 0.f);
}

/* travelling a reachability: TRUE while it's still under way */
static boolean bot_move_travel(struct bot_mover *bot, struct bot_body const *body, struct bot_move_result *result)
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
	/* made it (off a ladder: onto the floor at its top, whichever of the
	   areas up there it is. The one the ladder's tied to may be a step or
	   two on, round the hole it comes up through: from where we stand is
	   as good a place to go on from) */
	if ((area == reach->to && (move->reach_started || reach->travel == _aas_travel_walk)) ||
		(reach->travel == _aas_travel_ladder && move->reach_started && area != NONE && area != reach->from &&
			fabs(position->z - reach->left.z) < 0.2f && fabs(body->velocity.k) < 0.008f))
	{
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
	if (!move->reach_started && reach->travel != _aas_travel_teleport && position->z < MIN(reach->start.z, reach->edge.z) - 1.2f)
	{
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
		if (!move->reach_started && bot_distance2d(position, &reach->start) > 0.5f && to_landing > 1.2f)
		{
			result->yaw = bot_yaw_to(position, &reach->start);
			break;
		}
		result->yaw = bot_yaw_to(position, move->reach_started && position->z > landing->z - 0.1f ? &reach->end : landing);
		if (!move->reach_started && to_landing < 0.8f)
		{
			result->jump = TRUE;
			move->crouch_ticks = 30;
			move->reach_started = TRUE;
		}
		else if (!move->reach_started && move->reach_ticks > BOT_SECONDS(1.5f) && to_landing < 1.3f)
		{
			/* can't get closer (a lip in the way): jump from here */
			result->jump = TRUE;
			move->crouch_ticks = 30;
			move->reach_started = TRUE;
		}
		/* bounced off: again */
		if (move->reach_started && position->z < landing->z - 0.3f && move->jump_cooldown <= 0 && move->reach_ticks > 20 &&
			to_landing < 1.f)
		{
			result->jump = TRUE;
			move->crouch_ticks = 30;
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
		if (reach->careful == AAS_WALKOVER)
		{
			/* floor all the way: to this side of it, then straight over */
			if (!move->reach_started && bot_distance2d(position, &reach->edge) > 0.25f)
				result->yaw = bot_yaw_to(position, &reach->edge);
			else
			{
				move->reach_started = TRUE;
				result->yaw = bot_yaw_to(position, bot_distance2d(position, landing) > 0.15f ? landing : &reach->end);
				/* (up a step that isn't being walked up: a hop) */
				if (landing->z > position->z + 0.12f && bot_distance2d(position, landing) < 0.6f && (move->reach_ticks % 20) == 10)
					result->jump = TRUE;
			}
			break;
		}
		if (!move->reach_started)
		{
			real_point3d ahead = *position;
			real speed = (real)sqrt(body->velocity.i * body->velocity.i + body->velocity.j * body->velocity.j) * BOT_TICKS_PER_SECOND;
			real past = (position->x - reach->edge.x) * (real)cos(run) + (position->y - reach->edge.y) * (real)sin(run);
			real across = (real)fabs(-(position->x - reach->edge.x) * (real)sin(run) + (position->y - reach->edge.y) * (real)cos(run));
			/* a run-up: back from the edge, in line with the jump, as far as
			   there's room (up to a second's run); then at it flat out */
			if (move->reach_phase == 0)
			{
				real_point3d runup = reach->edge;
				real back;
				for (back = 1.6f; back > 0.3f; back -= 0.3f)
				{
					runup.x = reach->edge.x - (real)cos(run) * back;
					runup.y = reach->edge.y - (real)sin(run) * back;
					runup.z = reach->edge.z + 0.2f;
					if (navm_line_walkable(&reach->edge, &runup, 0.15f, 0.f))
						break;
				}
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
			if (past > -0.1f || (reach->careful == AAS_HOP && past > -0.7f && speed > 1.3f) ||
				(past > -0.4f && !bot_ground(&ahead, 0.8f, body->unit_index, NULL) && speed > 1.5f))
			{
				result->jump = TRUE;
				move->reach_started = TRUE;
				/* (this jump whatever was jumped a moment ago, a hop at
				   something in the way on the run-up: held back for that, it's
				   a run straight off the edge) */
				move->jump_cooldown = 0;
				/* (over something: legs tucked up to clear it) */
				if (reach->careful == AAS_HOP)
					move->crouch_ticks = 30;
			}
		}
		else
		{
			result->yaw = bot_yaw_to(position, position->z > landing->z - 0.1f ? &reach->end : landing);
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
		if (body->speed_scale > 1.05f && (move->reach_started || move->reach_phase == 1))
			result->speed = 1.f / body->speed_scale;
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
	result->path_yaw = result->yaw;
	return TRUE;
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
	struct object_datum *vehicle = object_get(body->vehicle_index);
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
	(void)vehicle;
}

/* a flying vehicle (a banshee): straight there, over things */
static void bot_move_fly(struct bot_mover *bot, struct bot_body const *body, real_point3d const *goal, struct bot_move_result *result)
{
	real_point3d aim = *goal;
	real distance = bot_distance2d(&body->position, goal);
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
	long goal_area;
	real to_goal = bot_distance2d(position, &goal->origin);

	memset(result, 0, sizeof(*result));
	result->yaw = bot->yaw;
	result->path_yaw = bot->yaw;
	result->speed = 1.f;
	result->travel = _aas_travel_walk;
	if (move->jump_cooldown > 0)
		move->jump_cooldown--;
	if (move->crouch_ticks > 0)
	{
		move->crouch_ticks--;
		if (move->crouch_ticks < 27)
			result->crouch = TRUE;
	}
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
		strstr(tag_get_name(object_get(body->vehicle_index)->definition_index), "banshee"))
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
	if (area != NONE)
		move->area = area;

	/* a reachability under way */
	if (type == AAS_FOOT && bot_move_travel(bot, body, result))
	{
		move->progress_ticks = 0;
		goto finish;
	}

	/* where the goal is */
	goal_area = aas_nearest_area(type, &goal->origin, type == AAS_VEHICLE ? 10.f : 3.f, &goal_spot);
	if (type == AAS_FOOT)
		goal->area = goal_area;
	if (area == NONE || goal_area == NONE)
	{
		/* off the map's areas altogether (on top of something, in the
		   air): straight at it, round whatever's in the way */
		result->no_route = goal_area == NONE;
		result->moving = TRUE;
		result->yaw = bot_yaw_to(position, area == NONE ? &goal->origin : &spot);
		if (area != NONE && bot_distance2d(position, &spot) < 0.3f)
			result->yaw = bot_yaw_to(position, &goal->origin);
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
			move->reach_ticks = 0;
			move->reach_started = FALSE;
			move->reach_phase = 1;
			move->reach_runup_ticks = 0;
			move->reach_peak = position->z;
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
				move->reach_ticks = 0;
				move->reach_started = FALSE;
				move->reach_phase = 0;
				move->reach_runup_ticks = 0;
				move->reach_peak = position->z;
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
		bot_move_drive(bot, body, &corners[0], corner_count > 1 ? &corners[1] : NULL, result);
		return;
	}
	result->moving = TRUE;
	result->yaw = bot_yaw_to(position, &corners[0]);
	result->path_yaw = result->yaw;
	move->corner = corners[0];
	/* close to the item: slow, so as not to run past it */
	if (goal->kind == _bot_move_goal_vehicle && to_goal < 1.5f)
		result->speed = to_goal > 0.6f ? 0.5f : 0.25f;
	/* a drop onto a narrow ledge coming up: slowing in good time (at a run
	   it's off the edge and clean over the ledge before there's any
	   stopping) */
	if (move->corridor_count > 0)
	{
		struct aas_reach const *next = aas_reach(type, move->corridor[move->corridor_count - 1]);
		if (next->travel == _aas_travel_drop && next->careful && bot_distance2d(position, &next->edge) < 1.5f &&
			fabs(position->z - next->edge.z) < 1.f)
		{
			result->speed = MIN(result->speed > 0.f ? result->speed : 1.f, 0.45f);
		}
	}

finish:
	if (type != AAS_FOOT)
		return;
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
		/* a step just too high to walk up (a curb, a sill): hop it */
		if (bot_move_step_ahead(body, result->yaw))
			result->jump = TRUE;
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
		float time = aas_travel_time(move->type, position, &goal->origin);
		real to_goal_now = time >= 0.f ? time * aas_speed(move->type) : bot_distance2d(position, &goal->origin);
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
			if (!nearer && (bot_distance2d(position, &move->progress_point) < 0.35f || move->unstick_check))
			{
				move->stuck_count++;
				if (bot_logging())
					bot_log("stuck bot%ld at %.2f %.2f %.2f area %ld goal %.1f %.1f %.1f corridor %ld reach %ld count %ld", bot->index,
						position->x, position->y, position->z, area, goal->origin.x, goal->origin.y, goal->origin.z, move->corridor_count,
						move->reach, move->stuck_count);
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
		}
		if (move->unstick_ticks > 0 && move->reach == NONE)
		{
			move->unstick_ticks--;
			result->yaw = move->unstick_yaw;
		}
	}
	else
		move->progress_ticks = 0;
	if (result->jump)
	{
		if (move->jump_cooldown > 0)
			result->jump = FALSE;
		else
			move->jump_cooldown = 20;
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
