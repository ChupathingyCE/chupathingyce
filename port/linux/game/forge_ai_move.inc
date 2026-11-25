/*
FORGE_AI_MOVE.INC

(Included by forge.c, for its statics.) How a Forge game's AI characters
move: they hold where they were placed (HOLD, as before), walk a route or an
area (PATROL), or go after the enemies they know of (PURSUE); a character
set to take VEHICLES boards a free one near it and drives it, or guns it;
and a Pelican or Covenant dropship given a route flies it with no pilot,
lands where the route says and lets its riders out.

The map knowledge is the AI navigation's (port/linux/game/ai_nav.c, the
multiplayer bots' knowledge of a level made its own module): the walkable
surfaces with the ramps opened and the surfaces under machines and roofs
closed, the regions those surfaces make, and the jumps and drops that join
the regions. A character's way is found on it:
- within a region, by the engine's own path search (path.c) over the navigation's
  copy of the walkable surfaces, handed to the actor's own path refresh
  (actor_moving.c, forge_ai_move_path_structure), so the engine's actor
  movement follows it, steers, avoids and takes cover as it does in the
  campaign;
- from one region to another, by the navigation's hops (ai_nav_route_kinds:
  jumps up, drops and gaps; no teleporter or ladder, as the game sends only
  players through teleporters and the AI climbs no ladder): to the hop's
  entrance, then across it on the engine's own move orders (a direction,
  and a jump where the hop needs one).
The goals are this file's: a route's points in order (each one's stop: go
on, wait 5 or 15 seconds, or land), a random spot of a patrol area, the
nearest enemy the character knows of (its props: seen or heard; with none,
PURSUE heads for the nearest enemy on the map, as a search), and its idle
spot (where it was placed) once there is nothing to do. Close to a target it
sees, the engine's own fighting is left alone.

Vehicles are the engine's own: a character boards one by the AI's vehicle
action (actor_action_try_to_enter_vehicle, as the scripts' ai_go_to_vehicle
sends it), and drives it on the same goals with the actor's own vehicle
movement (its vehicle's pathfinding radius, the navigation's copy closed for
vehicles; a Banshee flies by the engine's 3D path). A seat the character's
unit has no animation for is not taken (an Elite drives no Warthog).

A route's flight (a Pelican or dropship with AI ROUTE set and no player in
it): the craft is controlled as a recorded animation controls a unit in the
campaign (unit_set_actively_controlled, unit_control each frame: facing,
throttle, climb and descend), its own human plane physics flying it - lift
off, fly to each point of its route at height, set down at a LAND stop,
its riders out, and on. Riders: characters set to take VEHICLES within
10 world units of it when the round starts, put in its riders' seats.

Local only, as Forge is: nothing of it is sent. Off by default: a character
placed holds, as before, until its BEHAVIOR or VEHICLES is changed. The
builds without the navigation (HALO_AI_NAV 0) have the settings and the layouts'
fields, and the characters hold.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "halo_port_features.h"
#include "ai/actions.h"
#include "ai/path.h"
#include "units/vehicles.h"
#if HALO_AI_NAV
#include "ai_nav.h"
#endif

/* ---------- constants */

/* the characters moved at once (Forge's AI alive at most) and the craft
flown */
#define FORGE_AI_MOVERS FORGE_MAXIMUM_AI
#define FORGE_AI_PILOTS 8
/* (a patrol area's radii, a stop's wait, the words: forge.c's tables) */
/* how near a goal is reached (on foot, driving), how near a target seen is
left to the engine's fighting (on foot, driving), how far a patrol leaves
its route or area for an enemy, how often a goal is chosen again (ticks) */
#define FORGE_AI_ARRIVE 1.5f
#define FORGE_AI_ARRIVE_DRIVING 4.f
#define FORGE_AI_ENGAGE 9.f
#define FORGE_AI_ENGAGE_DRIVING 18.f
#define FORGE_AI_PATROL_LEASH 14.f
#define FORGE_AI_REPLAN_TICKS 15
/* a hop crossed: at most this long on its move orders (ticks) */
#define FORGE_AI_CROSS_TICKS 45
/* no headway for this long (ticks): the goal left for a while */
#define FORGE_AI_STUCK_TICKS 120
/* a free vehicle looked for this near, this often (ticks) */
#define FORGE_AI_BOARD_DISTANCE 30.f
#define FORGE_AI_BOARD_TICKS 30
/* a route's flight: its height over a point, how near a point counts, how
near the ground a landing is done, how long the riders get out */
#define FORGE_AI_FLY_HEIGHT 7.f
#define FORGE_AI_FLY_ARRIVE 5.f
#define FORGE_AI_FLY_LANDED 2.2f
#define FORGE_AI_FLY_UNLOAD_TICKS 150
#define FORGE_AI_RIDER_DISTANCE 16.f

enum
{
	_forge_ai_goal_none = 0,
	_forge_ai_goal_home,
	_forge_ai_goal_route,
	_forge_ai_goal_area,
	_forge_ai_goal_enemy,
	NUMBER_OF_FORGE_AI_GOALS
};

static char const *const forge_ai_goal_log_names[NUMBER_OF_FORGE_AI_GOALS] = { "none", "home", "route", "area", "enemy" };

enum
{
	_forge_ai_fly_takeoff = 0,
	_forge_ai_fly_cruise,
	_forge_ai_fly_wait,
	_forge_ai_fly_descend,
	_forge_ai_fly_unload,
	NUMBER_OF_FORGE_AI_FLY_PHASES
};

static char const *const forge_ai_fly_log_names[NUMBER_OF_FORGE_AI_FLY_PHASES] = { "takeoff", "cruise", "wait", "descend", "unload" };

/* ---------- structures */

struct forge_ai_mover
{
	long actor_index;
	long unit_index;
	long placed_id;
	struct forge_ai_move_settings settings;
	real_point3d home;
	/* the goal and the way to it */
	short goal_kind;
	real_point3d goal;
	long goal_surface_index;
	long goal_tick;
	short route_cursor;
	long wait_until;
	long hop_index;
	long cross_until;
	/* headway: where it was when last it made some */
	real_point3d headway_position;
	long headway_tick;
	/* a goal left for a while (stuck): until when */
	long blocked_until;
	/* (VEHICLES) the vehicle it was sent to, when it looks again */
	long vehicle_index;
	long board_tick;
	/* this frame's table: still the layout's */
	boolean seen;
	/* (the log's) what it last did */
	short logged_goal;
	long hops_taken;
	long moves_failed;
	long moves;
	/* (a vehicle the engine's AI has no driving for: a Warthog, a
	Scorpion) the way found on the navigation's copy closed for vehicles, its
	steps, the one driven to, and when it was found */
	boolean drive_active;
	short drive_step_count;
	short drive_step;
	real_point3d drive_steps[4];
	long drive_tick;
};

struct forge_ai_pilot
{
	long vehicle_index;
	long placed_id;
	short route;
	short cursor;
	short phase;
	long phase_tick;
	boolean riders_loaded;
	boolean seen;
	short logged_phase;
	long landings;
	/* (cruising) the nearest it came to its point, when, and a climb over
	what stopped it until when */
	real best_distance;
	long progress_tick;
	long climb_until;
};

/* ---------- globals */

static struct
{
	short mover_count;
	struct forge_ai_mover movers[FORGE_AI_MOVERS];
	short pilot_count;
	struct forge_ai_pilot pilots[FORGE_AI_PILOTS];
	/* (the actors' hook: whether any is moved this game) */
	boolean active;
	long knowledge_logged;
	/* (the console's "forge ai move" log, the tests') */
	long arrivals;
	long pursuits;
	long boardings;
} forge_ai_move;

/* ---------- private code */

static real forge_ai_fly_height(real_point3d const *position);

static real forge_ai_distance2d(
	real_point3d const *a,
	real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y;

	return (real)sqrt(dx * dx + dy * dy);
}

/* a hash of a few numbers: the patrol's spots, the same from run to run */
static unsigned long forge_ai_hash(
	unsigned long a,
	unsigned long b)
{
	unsigned long hash = 2166136261UL;

	hash = (hash ^ a) * 16777619UL;
	hash = (hash ^ b) * 16777619UL;
	hash ^= hash >> 13;
	return hash * 2246822519UL;
}

/* the walkable surface under a point and the ground there (NONE: none
known) */
static long forge_ai_ground(
	real_point3d const *point,
	real_point3d *ground)
{
#if HALO_AI_NAV
	real_point3d raised = *point;
	real_point3d found;
	long surface_index;

	raised.z += 0.5f;
	surface_index = ai_nav_ground_surface(&raised, &found);
	if (surface_index != NONE)
		*ground = found;
	return surface_index;
#else
	(void)point;
	(void)ground;
	return NONE;
#endif
}

/* a route's points (the layout's AI GO HERE markers of the route, in their
order) and areas; the count, at most `maximum` */
static short forge_ai_route_markers(
	short route,
	short kind,
	struct forge_spawn const **markers,
	short maximum)
{
	short index;
	short count = 0;

	for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE && count < maximum; index++)
	{
		struct forge_spawn const *spawn = &forge_globals.spawns[index];

		if (spawn->kind == kind && spawn->channel == route)
			markers[count++] = spawn;
	}
	return count;
}

static boolean forge_ai_vehicle_flyable(
	long vehicle_index,
	short *flyable_index)
{
	struct object_datum *object = object_try_and_get(vehicle_index);
	short flyable;

	if (!object || object->object.type != _object_type_vehicle || !forge_ai_tags_source_map(object->definition_index))
		return FALSE;
	for (flyable = 0; flyable < (short)NUMBEROF(forge_flyables); flyable++)
	{
		if (!_stricmp(tag_get_name(object->definition_index), forge_flyables[flyable].path))
		{
			if (flyable_index)
				*flyable_index = flyable;
			return TRUE;
		}
	}
	return FALSE;
}

static struct forge_ai_mover *forge_ai_mover_of_actor(
	long actor_index)
{
	short index;

	for (index = 0; index < forge_ai_move.mover_count; index++)
	{
		if (forge_ai_move.movers[index].actor_index == actor_index)
			return &forge_ai_move.movers[index];
	}
	return NULL;
}

/* whether a unit is alive and an enemy of the team */
static boolean forge_ai_enemy_unit(
	long unit_index,
	short team_index)
{
	struct object_datum *object = object_try_and_get(unit_index);

	if (!object || !(FLAG(object->object.type) & _object_mask_biped) ||
		TEST_FLAG(object->object.damage_flags, _object_dead_bit))
	{
		return FALSE;
	}
	return game_team_is_enemy(team_index, object->object.owner_team_index);
}

/* the enemy a character goes for: the nearest of its props that is an
enemy it perceived in the last 20 seconds (where it last knew it); with
none, when it hunts, the nearest enemy biped on the map (its search).
FALSE for none */
static boolean forge_ai_enemy_goal(
	long actor_index,
	boolean hunt,
	real leash,
	real_point3d const *leash_center,
	real_point3d *goal)
{
	struct actor_datum *actor = actor_get(actor_index);
	struct prop_iterator props;
	struct prop_datum *prop;
	real best = REAL_MAX;
	long now = game_time_get();
	boolean found = FALSE;

	prop_iterator_new(&props, actor_index);
	while ((prop = prop_iterator_next(&props)) != NULL)
	{
		real distance;

		if (!prop->enemy || prop->dead || prop->unit_index == NONE ||
			now - prop->last_perceived_time > 20 * TICKS_PER_SECOND ||
			(forge_globals.active && prop->unit_index == forge_globals.unit_index))
		{
			continue;
		}
		/* (where it was last perceived; one perceived this second, where it
		is: a rider's own position is its seat's) */
		{
			real_point3d at = prop->last_perceived_body_position;

			if (now - prop->last_perceived_time < TICKS_PER_SECOND && object_try_and_get(prop->unit_index))
				object_get_origin(prop->unit_index, &at);
			if (at.x == 0.f && at.y == 0.f && at.z == 0.f)
				continue;
			if (leash_center && forge_ai_distance2d(&at, leash_center) > leash)
				continue;
			distance = distance3d(&at, &actor->input.position.body_position);
			if (distance < best)
			{
				best = distance;
				*goal = at;
				found = TRUE;
			}
		}
	}
	if (found || !hunt)
		return found;
	{
		struct object_iterator iterator;

		object_iterator_new(&iterator, _object_mask_biped, 0);
		while (object_iterator_next(&iterator))
		{
			real_point3d origin;
			real distance;

			/* (not the player's unit waiting while the monitor flies) */
			if (iterator.index == actor->meta.unit_index || !forge_ai_enemy_unit(iterator.index, actor->meta.team_index) ||
				(forge_globals.active && iterator.index == forge_globals.unit_index))
			{
				continue;
			}
			/* (where it is in the world: a rider's own position is its seat's) */
			object_get_origin(iterator.index, &origin);
			distance = distance3d(&origin, &actor->input.position.body_position);
			if (distance < best)
			{
				best = distance;
				*goal = origin;
				found = TRUE;
			}
		}
	}
	return found;
}

/* a mover's goal chosen again: an enemy (PURSUE, or one near a patrol),
its route's next point, a spot of its area, or its idle spot */
static void forge_ai_goal_choose(
	struct forge_ai_mover *mover,
	boolean arrived)
{
	struct actor_datum *actor = actor_get(mover->actor_index);
	struct forge_spawn const *points[FORGE_FILE_MAXIMUM_ROUTE_POINTS];
	struct forge_spawn const *areas[FORGE_FILE_MAXIMUM_AI_AREAS];
	short route = mover->settings.route ? (short)(mover->settings.route - 1) : NONE;
	short point_count = route != NONE ? forge_ai_route_markers(route, _forge_spawn_ai_route, points, FORGE_FILE_MAXIMUM_ROUTE_POINTS) : 0;
	short area_count = route != NONE ? forge_ai_route_markers(route, _forge_spawn_ai_area, areas, FORGE_FILE_MAXIMUM_AI_AREAS) : 0;
	long now = game_time_get();
	real_point3d target;
	real_point3d const *center = NULL;
	real leash = FORGE_AI_PATROL_LEASH;

	mover->goal_tick = now;
	/* (an enemy: PURSUE hunts anywhere; a patrol only near its route or area) */
	if (mover->settings.behavior == FORGE_FILE_AI_PATROL)
	{
		center = area_count ? &areas[0]->home.position : point_count ? &points[PIN(mover->route_cursor, 0, point_count - 1)]->home.position :
			&mover->home;
		if (area_count)
			leash += forge_ai_area_radii[PIN(areas[0]->option, 0, FORGE_FILE_AI_AREA_SIZES - 1)];
	}
	if (now >= mover->blocked_until &&
		forge_ai_enemy_goal(mover->actor_index, mover->settings.behavior == FORGE_FILE_AI_PURSUE, leash, center, &target))
	{
		mover->goal_kind = _forge_ai_goal_enemy;
		mover->goal = target;
		mover->goal_surface_index = forge_ai_ground(&target, &mover->goal);
		return;
	}
	if (mover->settings.behavior == FORGE_FILE_AI_PATROL && point_count)
	{
		short stop;

		mover->route_cursor = (short)PIN(mover->route_cursor, 0, point_count - 1);
		if (arrived && mover->goal_kind == _forge_ai_goal_route)
		{
			stop = (short)PIN(points[mover->route_cursor]->option, 0, NUMBER_OF_FORGE_FILE_ROUTE_STOPS - 1);
			mover->wait_until = now + forge_ai_stop_seconds[stop] * TICKS_PER_SECOND;
			mover->route_cursor = (short)((mover->route_cursor + 1) % point_count);
			forge_ai_move.arrivals++;
		}
		mover->goal_kind = _forge_ai_goal_route;
		mover->goal_surface_index = forge_ai_ground(&points[mover->route_cursor]->home.position, &mover->goal);
		if (mover->goal_surface_index == NONE)
			mover->goal = points[mover->route_cursor]->home.position;
		return;
	}
	if (mover->settings.behavior == FORGE_FILE_AI_PATROL)
	{
		/* (an area of its route, else its idle spot's: a spot within it,
		another each time one is reached) */
		real_point3d middle = area_count ? areas[(mover->route_cursor & 0x7FFF) % area_count]->home.position : mover->home;
		real radius = area_count ? forge_ai_area_radii[PIN(areas[(mover->route_cursor & 0x7FFF) % area_count]->option, 0,
			FORGE_FILE_AI_AREA_SIZES - 1)] : 4.f;
		short tries;

		if (arrived && mover->goal_kind == _forge_ai_goal_area)
		{
			mover->wait_until = now + 2 * TICKS_PER_SECOND;
			mover->route_cursor++;
			forge_ai_move.arrivals++;
		}
		mover->goal_kind = _forge_ai_goal_area;
		for (tries = 0; tries < 6; tries++)
		{
			unsigned long hash = forge_ai_hash((unsigned long)mover->placed_id, (unsigned long)(mover->route_cursor * 8 + tries));
			real angle = (real)(hash & 0xFFFF) * (real)(2.0 * 3.14159265358979 / 65536.0);
			real reach = radius * (0.3f + 0.7f * (real)((hash >> 16) & 0xFF) / 255.f);
			real_point3d spot = middle;

			spot.x += cosine(angle) * reach;
			spot.y += sine(angle) * reach;
			mover->goal_surface_index = forge_ai_ground(&spot, &mover->goal);
			if (mover->goal_surface_index != NONE)
				return;
		}
		mover->goal = middle;
		mover->goal_surface_index = forge_ai_ground(&middle, &mover->goal);
		return;
	}
	/* (nothing to do: back to its idle spot, and there it holds) */
	mover->goal_kind = _forge_ai_goal_home;
	mover->goal_surface_index = forge_ai_ground(&mover->home, &mover->goal);
	if (mover->goal_surface_index == NONE)
		mover->goal = mover->home;
	(void)actor;
}

/* (a vehicle the AI has no driving for) the way to the goal on the navigation's
copy closed for vehicles, as near as the search reaches: its steps. As
the multiplayer bots drive, by path.c */
static boolean forge_ai_drive_plan(
	struct forge_ai_mover *mover,
	long vehicle_index)
{
#if HALO_AI_NAV
	static struct path_state state;
	struct path_input input;
	struct path_result result;
	struct structure_bsp *structure = ai_nav_structure(TRUE);
	real_point3d start, ground;
	long start_surface;
	short step;

	object_get_origin(vehicle_index, &start);
	start_surface = forge_ai_ground(&start, &ground);
	mover->drive_tick = game_time_get();
	mover->drive_step_count = 0;
	mover->drive_step = 0;
	if (!structure || start_surface == NONE || mover->goal_surface_index == NONE)
		return FALSE;
	path_input_new(&input, 1.0f, FALSE, vehicle_index);
	path_input_set_start(&input, &ground, start_surface);
	path_state_new(&input, &state, NULL);
	state.structure = structure;
	path_state_destination(&state, &mover->goal, mover->goal_surface_index, 1000.0f);
	if (!path_state_find(&state) || !path_state_build_path(&state, &result) || result.step_count <= 0)
		return FALSE;
	for (step = 0; step < result.step_count && step < 4; step++)
		mover->drive_steps[step] = result.steps[step].point;
	mover->drive_step_count = step;
	return TRUE;
#else
	(void)mover;
	(void)vehicle_index;
	return FALSE;
#endif
}

/* ---------- the actors' hooks (actors.c, actor_moving.c) */

/* (actors.c actor_update, before its controls go to its unit) a Forge
character driving a vehicle the AI has no driving for steered along its
way as a player drives: facing the next step, the throttle forward */
void forge_ai_move_actor_output(
	long actor_index)
{
	struct forge_ai_mover *mover;
	struct actor_datum *actor;
	real_vector3d to;
	real distance;
	real_point3d const *point;

	if (!forge_ai_move.active || (mover = forge_ai_mover_of_actor(actor_index)) == NULL || !mover->drive_active)
		return;
	actor = actor_get(actor_index);
	if (actor->input.vehicle_index == NONE || mover->drive_step >= mover->drive_step_count)
		return;
	point = &mover->drive_steps[mover->drive_step];
	to.i = point->x - actor->input.position.body_position.x;
	to.j = point->y - actor->input.position.body_position.y;
	to.k = 0.f;
	distance = normalize3d(&to);
	if (distance <= 0.f)
		return;
	actor->output.facing_vector = to;
	actor->output.aiming_vector = to;
	actor->output.looking_vector = to;
	{
		real_vector3d const *forward = &object_get(actor->input.vehicle_index)->object.forward;
		real along = to.i * forward->i + to.j * forward->j;

		actor->output.throttle.i = along > 0.5f ? 1.f : along > 0.f ? 0.6f : 0.3f;
		actor->output.throttle.j = 0.f;
		actor->output.throttle.k = 0.f;
	}
}

/* (actor_moving.c actor_path_refresh) a Forge character's search walks the
navigation's copy of the walkable surfaces (ramps open, the ground under machines
and roofs closed; a vehicle's, closed for vehicles too) */
void forge_ai_move_path_structure(
	long actor_index,
	struct path_state *state)
{
#if HALO_AI_NAV
	struct structure_bsp *structure;

	if (!forge_ai_move.active || !forge_ai_mover_of_actor(actor_index))
		return;
	structure = ai_nav_structure(actor_get(actor_index)->input.vehicle_index != NONE);
	if (structure && state->structure == global_structure_bsp_get())
		state->structure = structure;
#else
	(void)actor_index;
	(void)state;
#endif
}

/* (actors.c actor_update, once its action has given its orders this tick)
a Forge character's movement: the engine's own orders kept while it fights
a target it sees, enters a vehicle, or rides; else to its goal */
void forge_ai_move_actor_control(
	long actor_index)
{
#if HALO_AI_NAV
	struct forge_ai_mover *mover;
	struct actor_datum *actor;
	long now;
	real_point3d position;
	long surface_index;
	boolean driving;
	real arrive;

	if (!forge_ai_move.active || (mover = forge_ai_mover_of_actor(actor_index)) == NULL)
		return;
	actor = actor_get(actor_index);
	mover->drive_active = FALSE;
	if (actor->meta.swarm || actor->state.action == _actor_action_vehicle ||
		(actor->input.vehicle_index != NONE && actor->input.vehicle_driver_type == _actor_vehicle_driver_none))
	{
		return;
	}
	if (mover->settings.behavior == FORGE_FILE_AI_HOLD)
		return;
	now = game_time_get();
	driving = actor->input.vehicle_index != NONE;
	arrive = driving ? FORGE_AI_ARRIVE_DRIVING : FORGE_AI_ARRIVE;
	position = actor->input.position.body_position;
	/* (a target it sees, near: the engine's fighting - its own moves, if any) */
	if (actor->target.target_prop_index != NONE)
	{
		struct prop_datum *prop = prop_get(actor->target.target_prop_index);

		/* (not the player's unit waiting while the monitor flies) */
		if (!(forge_globals.active && prop->unit_index == forge_globals.unit_index) &&
			now - prop->last_visible_time < TICKS_PER_SECOND &&
			distance3d(&prop->last_perceived_body_position, &position) < (driving ? FORGE_AI_ENGAGE_DRIVING : FORGE_AI_ENGAGE))
		{
			mover->headway_tick = now;
			return;
		}
	}
	/* (the engine moving it already this tick: a flee, a charge, an evasion) */
	if (actor->orders.move.destination.destination_type != _destination_none &&
		actor->orders.move.destination.destination_type != _destination_halt)
	{
		mover->headway_tick = now;
		return;
	}
	/* (a hop being crossed: on its move orders until it is over) */
	if (mover->hop_index != NONE && now < mover->cross_until)
	{
		struct ai_nav_hop const *hop = ai_nav_hop_get(mover->hop_index);

		if (hop && forge_ai_distance2d(&position, &hop->exit) > 0.8f)
		{
			real_vector3d direction;

			direction.i = hop->exit.x - position.x;
			direction.j = hop->exit.y - position.y;
			direction.k = 0.f;
			if (normalize3d(&direction) > 0.f)
			{
				actor->orders.move.override_movement_direction = TRUE;
				actor->orders.move.override_movement_direction_vector = direction;
				if (hop->needs_jump && hop->kind != _ai_nav_hop_drop &&
					forge_ai_distance2d(&position, &hop->entrance) < 1.2f)
				{
					actor->orders.move.jump = TRUE;
					actor->orders.move.jump_leap = FALSE;
				}
				return;
			}
		}
		mover->hop_index = NONE;
		mover->hops_taken++;
	}
	/* (waiting at a stop) */
	if (now < mover->wait_until)
		return;
	/* (the goal: chosen again now and then, and when reached) */
	if (mover->goal_kind == _forge_ai_goal_none || now - mover->goal_tick >= FORGE_AI_REPLAN_TICKS ||
		(mover->goal_kind == _forge_ai_goal_enemy && now - mover->goal_tick >= 5))
	{
		forge_ai_goal_choose(mover, FALSE);
	}
	if (forge_ai_distance2d(&position, &mover->goal) < arrive && (real)fabs(position.z - mover->goal.z) < 3.f)
	{
		if (mover->goal_kind == _forge_ai_goal_home || mover->goal_kind == _forge_ai_goal_enemy)
		{
			mover->headway_tick = now;
			if (mover->goal_kind == _forge_ai_goal_enemy)
				forge_ai_goal_choose(mover, TRUE);
			return;
		}
		forge_ai_goal_choose(mover, TRUE);
		if (now < mover->wait_until)
			return;
	}
	/* (no headway: the goal left a while, another chosen) */
	if (distance3d(&position, &mover->headway_position) > 1.0f)
	{
		mover->headway_position = position;
		mover->headway_tick = now;
	}
	else if (now - mover->headway_tick > FORGE_AI_STUCK_TICKS)
	{
		mover->headway_tick = now;
		mover->blocked_until = now + 6 * TICKS_PER_SECOND;
		if (mover->goal_kind == _forge_ai_goal_route || mover->goal_kind == _forge_ai_goal_area)
			forge_ai_goal_choose(mover, TRUE);
		else
			forge_ai_goal_choose(mover, FALSE);
	}
	/* (the way: a hop's entrance first when the goal is in another region) */
	{
		real_point3d waypoint = mover->goal;
		long waypoint_surface = mover->goal_surface_index;

		surface_index = actor->input.pathfinding_surface_index;
		if (!driving && surface_index != NONE && waypoint_surface != NONE && ai_nav_map_ready() &&
			ai_nav_region(surface_index) != NONE && ai_nav_region(waypoint_surface) != NONE &&
			ai_nav_region(surface_index) != ai_nav_region(waypoint_surface))
		{
			real cost;
			long hop_index = ai_nav_route_kinds(surface_index, &position, waypoint_surface, &mover->goal, now, &cost,
				(1UL << _ai_nav_hop_jump_up) | (1UL << _ai_nav_hop_drop) | (1UL << _ai_nav_hop_gap));
			struct ai_nav_hop const *hop = hop_index >= 0 ? ai_nav_hop_get(hop_index) : NULL;

			if (hop)
			{
				if (forge_ai_distance2d(&position, &hop->entrance) < 1.0f && (real)fabs(position.z - hop->entrance.z) < 1.5f)
				{
					mover->hop_index = hop_index;
					mover->cross_until = now + FORGE_AI_CROSS_TICKS;
					return;
				}
				waypoint = hop->entrance;
				waypoint_surface = hop->entrance_surface_index;
			}
		}
		if (waypoint_surface == NONE && !driving)
			return;
		/* (a vehicle the AI has no driving for: driven as a player drives,
		forge_ai_move_actor_output; its way found again now and then) */
		if (driving && actor->input.vehicle_driver_type == _actor_vehicle_driver_unknown)
		{
			if (mover->logged_goal != mover->goal_kind)
			{
				mover->logged_goal = mover->goal_kind;
				platform_log("forge: ai move: #%ld (%s, driving %s) goes for its %s at %.1f %.1f %.1f", mover->placed_id,
					forge_ai_behavior_log_names[mover->settings.behavior % NUMBER_OF_FORGE_FILE_AI_BEHAVIORS],
					forge_tag_leaf(object_get(actor->input.vehicle_index)->definition_index),
					forge_ai_goal_log_names[mover->goal_kind], mover->goal.x, mover->goal.y, mover->goal.z);
			}
			mover->moves++;
			if ((now - mover->drive_tick >= FORGE_AI_REPLAN_TICKS || mover->drive_step >= mover->drive_step_count) &&
				!forge_ai_drive_plan(mover, actor->input.vehicle_index))
			{
				mover->moves_failed++;
			}
			while (mover->drive_step < mover->drive_step_count &&
				forge_ai_distance2d(&position, &mover->drive_steps[mover->drive_step]) < 3.f)
			{
				mover->drive_step++;
			}
			mover->drive_active = mover->drive_step < mover->drive_step_count;
			return;
		}
		/* (a flier's goal over the ground, not in it) */
		if (actor->input.vehicle_driver_type == _actor_vehicle_driver_directional_flying)
			waypoint.z += 3.f;
		if (mover->logged_goal != mover->goal_kind)
		{
			mover->logged_goal = mover->goal_kind;
			if (mover->goal_kind == _forge_ai_goal_enemy)
				forge_ai_move.pursuits++;
			platform_log("forge: ai move: #%ld (%s%s) goes for its %s at %.1f %.1f %.1f from %.1f %.1f %.1f", mover->placed_id,
				forge_ai_behavior_log_names[mover->settings.behavior % NUMBER_OF_FORGE_FILE_AI_BEHAVIORS], driving ? ", driving" : "",
				forge_ai_goal_log_names[mover->goal_kind], mover->goal.x, mover->goal.y, mover->goal.z, position.x, position.y,
				position.z);
		}
		mover->moves++;
		if (!actor_move_to_point(actor_index, &waypoint, waypoint_surface, NONE))
		{
			/* (no path found to it: the log says so, once a goal) */
			if (!mover->moves_failed++ || !(mover->moves_failed % 300))
			{
				platform_log("forge: ai move: #%ld%s finds no path to %.1f %.1f %.1f (surface %ld, from %ld)", mover->placed_id,
					driving ? " (driving)" : "", waypoint.x, waypoint.y, waypoint.z, waypoint_surface, surface_index);
			}
		}
	}
#else
	(void)actor_index;
#endif
}

/* ---------- vehicles */

/* (VEHICLES) a character on foot sent to a free vehicle near it: the
nearest whose seats it can take (none of another character's choosing) */
static void forge_ai_board_update(
	struct forge_ai_mover *mover)
{
	struct actor_datum *actor = actor_get(mover->actor_index);
	struct object_iterator iterator;
	long now = game_time_get();
	long best_vehicle = NONE;
	real best = FORGE_AI_BOARD_DISTANCE;

	if (!(mover->settings.options & FORGE_FILE_AI_OPTION_VEHICLES) || actor->input.vehicle_index != NONE ||
		actor->state.action == _actor_action_vehicle || actor->meta.swarm || now < mover->board_tick)
	{
		return;
	}
	mover->board_tick = now + FORGE_AI_BOARD_TICKS;
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (object_iterator_next(&iterator))
	{
		struct unit_datum *vehicle = unit_get(iterator.index);
		short index;
		real distance;
		boolean taken = FALSE;

		if (TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) || forge_ai_vehicle_flyable(iterator.index, NULL))
			continue;
		distance = distance3d(&vehicle->object.position, &actor->input.position.body_position);
		if (distance >= best)
			continue;
		for (index = 0; index < forge_ai_move.mover_count; index++)
		{
			if (&forge_ai_move.movers[index] != mover && forge_ai_move.movers[index].vehicle_index == iterator.index &&
				vehicle->unit.driver_object_index == NONE)
			{
				taken = TRUE;
			}
		}
		/* (a player's: never; one already driven: its gunner's seat only) */
		if (taken || (vehicle->unit.driver_object_index != NONE &&
			object_try_and_get(vehicle->unit.driver_object_index) &&
			unit_get(vehicle->unit.driver_object_index)->unit.player_index != NONE))
		{
			continue;
		}
		best = distance;
		best_vehicle = iterator.index;
	}
	if (best_vehicle == NONE)
		return;
	/* (its driver's seat first, else any it can take: a gunner's, a
	passenger's) */
	if (actor_action_try_to_enter_vehicle(mover->actor_index, best_vehicle, "driver", NONE, 0, NULL) ||
		actor_action_try_to_enter_vehicle(mover->actor_index, best_vehicle, "", NONE, 0, NULL))
	{
		mover->vehicle_index = best_vehicle;
		forge_ai_move.boardings++;
		platform_log("forge: ai move: #%ld goes to board %s at %.1f units", mover->placed_id,
			forge_tag_leaf(object_get(best_vehicle)->definition_index), best);
	}
}

/* ---- a route's flight */

/* a craft's control this frame: face along the ground toward a point, the
throttle, climb or descend (the human plane's lift: jump climbs, crouch
descends, neither holds) */
static void forge_ai_fly_control(
	long vehicle_index,
	real_point3d const *toward,
	real throttle,
	short lift)
{
	struct unit_datum *vehicle = unit_get(vehicle_index);
	struct unit_control_data control;
	real_vector3d facing;

	csmemset(&control, 0, sizeof(control));
	facing = vehicle->object.forward;
	if (toward)
	{
		facing.i = toward->x - vehicle->object.position.x;
		facing.j = toward->y - vehicle->object.position.y;
	}
	facing.k = 0.f;
	if (normalize3d(&facing) == 0.f)
		facing = *global_forward3d;
	control.weapon_index = NONE;
	control.grenade_index = NONE;
	control.throttle.i = PIN(throttle, 0.f, 1.f);
	control.facing_vector = facing;
	control.aiming_vector = facing;
	control.looking_vector = facing;
	control.control_flags = (word)(lift > 0 ? FLAG(_unit_control_jump_bit) : lift < 0 ? FLAG(_unit_control_crouch_modifier_bit) : 0);
	unit_set_actively_controlled(vehicle_index, TRUE);
	unit_control(vehicle_index, &control);
}

/* the ground under a craft (its height over it), a long way down */
static real forge_ai_fly_height(
	real_point3d const *position)
{
	real_vector3d down = { 0.f, 0.f, -60.f };
	struct collision_result collision;

	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit), position,
		&down, NONE, &collision))
	{
		return 60.f * collision.t;
	}
	return 60.f;
}

/* (round start, and each frame) characters that take vehicles near a
craft with a route put in its riders' seats, as the campaign loads a
dropship before its flight */
static void forge_ai_pilot_load(
	struct forge_ai_pilot *pilot,
	short flyable)
{
	struct object_datum *craft = object_get(pilot->vehicle_index);
	short index;
	short loaded = 0;

	for (index = 0; index < forge_ai_move.mover_count; index++)
	{
		struct forge_ai_mover *mover = &forge_ai_move.movers[index];
		struct object_datum *unit = object_try_and_get(mover->unit_index);

		if (!unit || !(mover->settings.options & FORGE_FILE_AI_OPTION_VEHICLES) || unit->object.parent_object_index != NONE ||
			forge_ai_distance2d(&unit->object.position, &craft->object.position) > FORGE_AI_RIDER_DISTANCE)
		{
			continue;
		}
		/* (a riders' seat it has the animations for, free: the scripts'
		vehicle loading, by the seat's own label) */
		{
			struct unit_definition *definition = unit_definition_get(craft->definition_index);
			size_t prefix_length = strlen(forge_flyables[flyable].rider_prefix);
			short seat_index;

			for (seat_index = 0; seat_index < definition->unit.seats.count &&
				object_get(mover->unit_index)->object.parent_object_index == NONE; seat_index++)
			{
				struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat);

				if (!_strnicmp(seat->label, forge_flyables[flyable].rider_prefix, prefix_length) ||
					!_strnicmp(seat->label, "P-rider", 7))
				{
					unit_scripting_enter_vehicle(mover->unit_index, pilot->vehicle_index, seat->label);
				}
			}
		}
		if (object_get(mover->unit_index)->object.parent_object_index == pilot->vehicle_index)
		{
			mover->vehicle_index = pilot->vehicle_index;
			loaded++;
		}
	}
	if (loaded)
		pilot->riders_loaded = TRUE;
	if (loaded)
		platform_log("forge: ai move: %d riders in %s #%ld", loaded, forge_tag_leaf(craft->definition_index), pilot->placed_id);
}

/* its riders out (a landing) */
static short forge_ai_pilot_unload(
	struct forge_ai_pilot *pilot)
{
	short index;
	short unloaded = 0;

	for (index = 0; index < forge_ai_move.mover_count; index++)
	{
		struct forge_ai_mover *mover = &forge_ai_move.movers[index];
		struct object_datum *unit = object_try_and_get(mover->unit_index);

		if (!unit || unit->object.parent_object_index != pilot->vehicle_index)
			continue;
		unit_scripting_exit_vehicle(mover->unit_index);
		mover->vehicle_index = NONE;
		/* (a rider out does not board again at once) */
		mover->board_tick = game_time_get() + 20 * TICKS_PER_SECOND;
		unloaded++;
	}
	return unloaded;
}

static void forge_ai_pilot_update(
	struct forge_ai_pilot *pilot)
{
	struct forge_spawn const *points[FORGE_FILE_MAXIMUM_ROUTE_POINTS];
	short point_count = forge_ai_route_markers(pilot->route, _forge_spawn_ai_route, points, FORGE_FILE_MAXIMUM_ROUTE_POINTS);
	struct unit_datum *craft = unit_get(pilot->vehicle_index);
	real_point3d position = craft->object.position;
	real height = forge_ai_fly_height(&position);
	long now = game_time_get();
	short flyable = 0;
	struct forge_spawn const *point;
	real_point3d target;

	forge_ai_vehicle_flyable(pilot->vehicle_index, &flyable);
	if (!point_count)
	{
		forge_ai_fly_control(pilot->vehicle_index, NULL, 0.f, 0);
		return;
	}
	/* (its riders: those near it while it is still on the ground) */
	if (!pilot->riders_loaded && pilot->phase == _forge_ai_fly_takeoff && !pilot->landings && height < 3.f)
		forge_ai_pilot_load(pilot, flyable);
	pilot->cursor = (short)PIN(pilot->cursor, 0, point_count - 1);
	point = points[pilot->cursor];
	target = point->home.position;
	switch (pilot->phase)
	{
	case _forge_ai_fly_takeoff:
		/* (its first: 3 seconds on the ground for its riders) */
		if (!pilot->landings && now - pilot->phase_tick < 3 * TICKS_PER_SECOND)
		{
			forge_ai_fly_control(pilot->vehicle_index, NULL, 0.f, -1);
			break;
		}
		forge_ai_fly_control(pilot->vehicle_index, &target, 0.f, 1);
		if (height > FORGE_AI_FLY_HEIGHT * 0.6f || now - pilot->phase_tick > 9 * TICKS_PER_SECOND)
		{
			pilot->phase = _forge_ai_fly_cruise;
			pilot->phase_tick = now;
			pilot->best_distance = REAL_MAX;
			pilot->progress_tick = now;
		}
		break;
	case _forge_ai_fly_cruise:
	{
		real distance = forge_ai_distance2d(&position, &target);
		real wanted = target.z + FORGE_AI_FLY_HEIGHT;
		short lift = position.z < wanted - 1.f || height < 3.f ? 1 : position.z > wanted + 2.f && height > 4.f ? -1 : 0;

		/* (no nearer for 2 seconds: something in the way, climbed over
		for 2 seconds) */
		if (distance < pilot->best_distance - 1.f)
		{
			pilot->best_distance = distance;
			pilot->progress_tick = now;
		}
		else if (now - pilot->progress_tick > 2 * TICKS_PER_SECOND)
		{
			pilot->climb_until = now + 2 * TICKS_PER_SECOND;
			pilot->progress_tick = now;
			pilot->best_distance = distance;
		}
		if (now < pilot->climb_until)
			lift = 1;
		/* (slower near the point, and while turning toward it) */
		real_vector3d to;
		real facing_dot;

		to.i = target.x - position.x;
		to.j = target.y - position.y;
		to.k = 0.f;
		normalize3d(&to);
		facing_dot = to.i * craft->object.forward.i + to.j * craft->object.forward.j;
		forge_ai_fly_control(pilot->vehicle_index, &target, PIN(distance / 20.f, 0.25f, 1.f) * PIN(facing_dot, 0.f, 1.f), lift);
		if (distance < FORGE_AI_FLY_ARRIVE)
		{
			short stop = (short)PIN(point->option, 0, NUMBER_OF_FORGE_FILE_ROUTE_STOPS - 1);

			pilot->phase_tick = now;
			if (stop == FORGE_FILE_ROUTE_STOP_LAND)
				pilot->phase = _forge_ai_fly_descend;
			else if (stop != FORGE_FILE_ROUTE_STOP_NONE)
				pilot->phase = _forge_ai_fly_wait;
			else
				pilot->cursor = (short)((pilot->cursor + 1) % point_count);
			pilot->best_distance = REAL_MAX;
		}
		break;
	}
	case _forge_ai_fly_wait:
		forge_ai_fly_control(pilot->vehicle_index, &target, 0.f, 0);
		if (now - pilot->phase_tick > forge_ai_stop_seconds[PIN(point->option, 0, NUMBER_OF_FORGE_FILE_ROUTE_STOPS - 1)] *
			TICKS_PER_SECOND)
		{
			pilot->cursor = (short)((pilot->cursor + 1) % point_count);
			pilot->phase = _forge_ai_fly_cruise;
			pilot->phase_tick = now;
			pilot->best_distance = REAL_MAX;
			pilot->progress_tick = now;
		}
		break;
	case _forge_ai_fly_descend:
		forge_ai_fly_control(pilot->vehicle_index, NULL, 0.f, height > FORGE_AI_FLY_LANDED ? -1 : 0);
		if (height <= FORGE_AI_FLY_LANDED || now - pilot->phase_tick > 15 * TICKS_PER_SECOND)
		{
			short unloaded = forge_ai_pilot_unload(pilot);

			pilot->landings++;
			pilot->phase = _forge_ai_fly_unload;
			pilot->phase_tick = now;
			platform_log("forge: ai move: %s #%ld landed at %.1f %.1f %.1f (%.1f over the ground), %d riders out",
				forge_tag_leaf(craft->definition_index), pilot->placed_id, position.x, position.y, position.z, height, unloaded);
		}
		break;
	case _forge_ai_fly_unload:
		forge_ai_fly_control(pilot->vehicle_index, NULL, 0.f, -1);
		if (now - pilot->phase_tick > FORGE_AI_FLY_UNLOAD_TICKS)
		{
			pilot->cursor = (short)((pilot->cursor + 1) % point_count);
			pilot->phase = _forge_ai_fly_takeoff;
			pilot->phase_tick = now;
		}
		break;
	}
	if (pilot->logged_phase != pilot->phase)
	{
		pilot->logged_phase = pilot->phase;
		platform_log("forge: ai move: %s #%ld %s, route %d point %d of %d, at %.1f %.1f %.1f (%.1f over the ground)",
			forge_tag_leaf(craft->definition_index), pilot->placed_id, forge_ai_fly_log_names[pilot->phase], pilot->route + 1,
			pilot->cursor + 1, point_count, position.x, position.y, position.z, height);
	}
}

/* ---------- forge.c's */

/* (forge_update, each frame of a Forge game or a layout played) the movers
and pilots made again from the layout, the map's navigation stepped for
them, the craft flown and the characters sent to vehicles */
static void forge_ai_move_update(
	void)
{
	short index;
	short kept;
	long biped = NONE;

	forge_ai_move.active = FALSE;
	if (!forge_globals.game_active || game_connection() != _game_connection_local)
	{
		forge_ai_move.mover_count = 0;
		forge_ai_move.pilot_count = 0;
		return;
	}
	for (index = 0; index < forge_ai_move.mover_count; index++)
		forge_ai_move.movers[index].seen = FALSE;
	for (index = 0; index < forge_ai_move.pilot_count; index++)
		forge_ai_move.pilots[index].seen = FALSE;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed const *placed = &forge_globals.placed[index];
		struct object_datum *object;
		short slot;

		if (!forge_object_alive(placed->object_index))
			continue;
		object = object_get(placed->object_index);
		if (TEST_FLAG(object->object.damage_flags, _object_dead_bit))
			continue;
		/* (a craft with a route and no player in it: flown) */
		if (!placed->ai && placed->move.route && placed->object_index != forge_globals.held_object_index &&
			forge_ai_vehicle_flyable(placed->object_index, NULL))
		{
			struct unit_datum *craft = unit_get(placed->object_index);

			if (craft->unit.driver_object_index != NONE)
				continue;
			for (slot = 0; slot < forge_ai_move.pilot_count && forge_ai_move.pilots[slot].vehicle_index != placed->object_index; slot++)
				;
			if (slot >= forge_ai_move.pilot_count)
			{
				if (forge_ai_move.pilot_count >= FORGE_AI_PILOTS)
					continue;
				slot = forge_ai_move.pilot_count++;
				csmemset(&forge_ai_move.pilots[slot], 0, sizeof(forge_ai_move.pilots[slot]));
				forge_ai_move.pilots[slot].vehicle_index = placed->object_index;
				forge_ai_move.pilots[slot].phase_tick = game_time_get();
				forge_ai_move.pilots[slot].logged_phase = NONE;
			}
			forge_ai_move.pilots[slot].placed_id = placed->id;
			forge_ai_move.pilots[slot].route = (short)(placed->move.route - 1);
			forge_ai_move.pilots[slot].seen = TRUE;
			continue;
		}
		/* (not one held by the monitor) */
		if (!placed->ai || (placed->move.behavior == FORGE_FILE_AI_HOLD && !(placed->move.options & FORGE_FILE_AI_OPTION_VEHICLES)) ||
			(placed->flags & FORGE_RECORD_AI_OFF) || placed->object_index == forge_globals.held_object_index)
		{
			continue;
		}
		{
			long actor_index = forge_ai_actor(placed->object_index);
			struct forge_ai_mover *mover;

			if (actor_index == NONE)
				continue;
			for (slot = 0; slot < forge_ai_move.mover_count && forge_ai_move.movers[slot].actor_index != actor_index; slot++)
				;
			if (slot >= forge_ai_move.mover_count)
			{
				if (forge_ai_move.mover_count >= FORGE_AI_MOVERS)
					continue;
				slot = forge_ai_move.mover_count++;
				mover = &forge_ai_move.movers[slot];
				csmemset(mover, 0, sizeof(*mover));
				mover->actor_index = actor_index;
				mover->home = placed->has_home ? placed->home.position : object->object.position;
				mover->hop_index = NONE;
				mover->vehicle_index = NONE;
				mover->goal_surface_index = NONE;
				mover->logged_goal = NONE;
				mover->headway_position = object->object.position;
				mover->headway_tick = game_time_get();
			}
			mover = &forge_ai_move.movers[slot];
			mover->unit_index = placed->object_index;
			mover->placed_id = placed->id;
			mover->settings = placed->move;
			mover->seen = TRUE;
			/* (its idle spot: where the editor put it) */
			if (placed->has_home)
				mover->home = placed->home.position;
			if (biped == NONE)
				biped = object->definition_index;
		}
	}
	/* (the ones gone: dropped, the rest kept in order) */
	for (index = 0, kept = 0; index < forge_ai_move.mover_count; index++)
	{
		if (forge_ai_move.movers[index].seen)
			forge_ai_move.movers[kept++] = forge_ai_move.movers[index];
	}
	forge_ai_move.mover_count = kept;
	for (index = 0, kept = 0; index < forge_ai_move.pilot_count; index++)
	{
		if (forge_ai_move.pilots[index].seen)
			forge_ai_move.pilots[kept++] = forge_ai_move.pilots[index];
		else if (object_try_and_get(forge_ai_move.pilots[index].vehicle_index))
			unit_set_actively_controlled(forge_ai_move.pilots[index].vehicle_index, FALSE);
	}
	forge_ai_move.pilot_count = kept;
	if (!forge_ai_move.mover_count && !forge_ai_move.pilot_count)
		return;
	forge_ai_move.active = TRUE;
#if HALO_AI_NAV
	/* (the map's navigation, for the characters' biped: its jumps) */
	ai_nav_map_update(biped);
	if (ai_nav_map_ready() && !forge_ai_move.knowledge_logged)
	{
		forge_ai_move.knowledge_logged = TRUE;
		platform_log("forge: ai move: the map's navigation is ready (%ld hops)", ai_nav_hop_count());
	}
#endif
	if (game_time_get_paused())
		return;
	for (index = 0; index < forge_ai_move.pilot_count; index++)
		forge_ai_pilot_update(&forge_ai_move.pilots[index]);
	for (index = 0; index < forge_ai_move.mover_count; index++)
		forge_ai_board_update(&forge_ai_move.movers[index]);
}

/* (forge_variant_capture) a placed object's movement settings in its
record (version 12): a character's, a vehicle's route */
static void forge_ai_move_capture(
	struct forge_placed const *placed,
	unsigned long group_tag,
	struct forge_file_record *record)
{
	record->ai_behavior = 0;
	record->ai_route = 0;
	record->ai_options = 0;
	if (placed->ai && group_tag == ACTOR_VARIANT_DEFINITION_TAG)
	{
		record->ai_behavior = (unsigned char)(placed->move.behavior % NUMBER_OF_FORGE_FILE_AI_BEHAVIORS);
		record->ai_route = (unsigned char)MIN(placed->move.route, FORGE_FILE_MAXIMUM_ROUTES);
		record->ai_options = (unsigned char)(placed->move.options & FORGE_FILE_AI_OPTIONS_KNOWN);
	}
	else if (group_tag == VEHICLE_DEFINITION_TAG)
	{
		record->ai_route = (unsigned char)MIN(placed->move.route, FORGE_FILE_MAXIMUM_ROUTES);
	}
}

/* (a layout placed) a record's movement settings (checked by the
validator) */
static void forge_ai_move_from_record(
	struct forge_placed *placed,
	struct forge_file_record const *record)
{
	placed->move.behavior = (unsigned char)(record->ai_behavior % NUMBER_OF_FORGE_FILE_AI_BEHAVIORS);
	placed->move.route = (unsigned char)MIN(record->ai_route, FORGE_FILE_MAXIMUM_ROUTES);
	placed->move.options = (unsigned char)(record->ai_options & FORGE_FILE_AI_OPTIONS_KNOWN);
	if (!placed->ai)
	{
		placed->move.behavior = 0;
		placed->move.options = 0;
	}
}

static boolean forge_ai_move_is_vehicle(
	struct forge_placed const *placed)
{
	return !placed->ai && placed->tag_index != NONE && tag_get_group_tag(placed->tag_index) == VEHICLE_DEFINITION_TAG;
}

static void forge_ai_move_properties_get(
	struct forge_placed const *placed,
	struct forge_properties *properties)
{
	properties->ai_move = (short)(placed->move.behavior % NUMBER_OF_FORGE_FILE_AI_BEHAVIORS);
	properties->ai_route = placed->move.route ? (short)(placed->move.route - 1) : NONE;
	properties->ai_vehicles = (placed->move.options & FORGE_FILE_AI_OPTION_VEHICLES) != 0;
	properties->vehicle = forge_ai_move_is_vehicle(placed);
}

/* a character's BEHAVIOR, ROUTE and VEHICLES, a vehicle's AI ROUTE,
stepped; FALSE for another property */
static boolean forge_ai_move_property_apply(
	struct forge_placed *placed,
	short property,
	short step)
{
	switch (property)
	{
	case _forge_property_ai_move:
		if (!placed->ai)
			return TRUE;
		placed->move.behavior = (unsigned char)((placed->move.behavior + step + NUMBER_OF_FORGE_FILE_AI_BEHAVIORS) %
			NUMBER_OF_FORGE_FILE_AI_BEHAVIORS);
		break;
	case _forge_property_ai_route:
		if (!placed->ai && !forge_ai_move_is_vehicle(placed))
			return TRUE;
		placed->move.route = (unsigned char)((placed->move.route + step + FORGE_FILE_MAXIMUM_ROUTES + 1) %
			(FORGE_FILE_MAXIMUM_ROUTES + 1));
		break;
	case _forge_property_ai_vehicles:
		if (!placed->ai)
			return TRUE;
		placed->move.options ^= FORGE_FILE_AI_OPTION_VEHICLES;
		break;
	default:
		return FALSE;
	}
	platform_log("forge: ai move: #%ld behavior %s, route %d, vehicles %s", placed->id,
		forge_ai_behavior_log_names[placed->move.behavior % NUMBER_OF_FORGE_FILE_AI_BEHAVIORS], (int)placed->move.route,
		(placed->move.options & FORGE_FILE_AI_OPTION_VEHICLES) ? "on" : "off");
	return TRUE;
}

/* (the console's "forge ai move") the movers and pilots in the log */
static void forge_ai_move_log(
	void)
{
	short index;

	platform_log("forge: ai move: %d moving, %d flown; %ld arrivals, %ld pursuits, %ld boardings", forge_ai_move.mover_count,
		forge_ai_move.pilot_count, forge_ai_move.arrivals, forge_ai_move.pursuits, forge_ai_move.boardings);
	for (index = 0; index < forge_ai_move.mover_count; index++)
	{
		struct forge_ai_mover const *mover = &forge_ai_move.movers[index];
		struct object_datum *unit = object_try_and_get(mover->unit_index);
		struct object_datum *vehicle = unit && unit->object.parent_object_index != NONE ?
			object_try_and_get(unit->object.parent_object_index) : NULL;
		real_point3d origin;

		if (!unit)
			continue;
		object_get_origin(mover->unit_index, &origin);
		/* (the actor's own movement: its action, path, moving, throttle) */
		if (actor_try_and_get(mover->actor_index))
		{
			struct actor_datum *actor = actor_get(mover->actor_index);

			platform_log("forge: ai move:     actor action %d, driver type %d, destination %d, path %s, at it %d, moving %d, "
				"complete %d, thwarted %d, throttle %.2f %.2f %.2f, dormant %d", (int)actor->state.action,
				(int)actor->input.vehicle_driver_type, (int)actor->control.path.destination_orders.destination_type,
				actor->control.path.path.valid ? "valid" : "none", (int)actor->control.path.at_destination,
				(int)actor->control.moving, (int)actor->control.movement_complete, (int)actor->control.movement_thwarted,
				actor->output.throttle.i, actor->output.throttle.j, actor->output.throttle.k, (int)actor->meta.dormant);
		}
		platform_log("forge: ai move:   #%ld %s at %.1f %.1f %.1f, %s, goal %s %.1f %.1f %.1f, %ld hops, %ld of %ld moves "
			"with no path%s%s", 
			mover->placed_id, forge_tag_leaf(unit->definition_index), origin.x, origin.y, origin.z, forge_ai_behavior_log_names[mover->settings.behavior % NUMBER_OF_FORGE_FILE_AI_BEHAVIORS],
			forge_ai_goal_log_names[mover->goal_kind % NUMBER_OF_FORGE_AI_GOALS], mover->goal.x, mover->goal.y, mover->goal.z,
			mover->hops_taken, mover->moves_failed, mover->moves, vehicle ? ", in " : "", vehicle ? forge_tag_leaf(vehicle->definition_index) : "");
	}
	for (index = 0; index < forge_ai_move.pilot_count; index++)
	{
		struct forge_ai_pilot const *pilot = &forge_ai_move.pilots[index];
		struct object_datum *craft = object_try_and_get(pilot->vehicle_index);

		if (!craft)
			continue;
		platform_log("forge: ai move:   %s #%ld at %.1f %.1f %.1f, %s, route %d point %d, %ld landings",
			forge_tag_leaf(craft->definition_index), pilot->placed_id, craft->object.position.x, craft->object.position.y,
			craft->object.position.z, forge_ai_fly_log_names[pilot->phase % NUMBER_OF_FORGE_AI_FLY_PHASES], pilot->route + 1,
			pilot->cursor + 1, pilot->landings);
	}
}
