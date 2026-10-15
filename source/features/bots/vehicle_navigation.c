/*
VEHICLE_NAVIGATION.C

port: routes for bots driving ground vehicles. The vehicle layer of the
walkable surfaces (navigation_world.c) is searched as the bots' foot routes
are, on a queue of its own: a route keeps a vehicle's width and height clear
of the structure and scenery, and its slope within a vehicle's. The driver
steers at a point two vehicle lengths along the route (a vehicle cannot turn
on the spot), slows into sharp turns and toward the end, waits for a vehicle
in its way, backs up when stuck, and gives the route up (a blocked polygon
learnt, as on foot) when backing up does not free it.

At most VEHICLE_NAVIGATION_DRIVERS_MAX drivers have a route at once; their
slots are let go when not asked after for a tick. Nothing here moves a
vehicle: the driver's facing and throttle go through the ordinary player
action, as a person's do.
*/

/* ---------- headers */

#include "cseries.h"
#include "game.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/vehicles.h"
#include "real_math.h"
#include "navigation_world.h"
#include "vehicle_navigation.h"
#include "engine_ai/surface_navigation_jobs.h"

/* port/linux/src/platform.h's */
void platform_log(char const *format, ...);

#ifdef HALO_FEATURE_BOTS

/* ---------- constants */

enum
{
	VEHICLE_NAVIGATION_PROGRESS_TICKS = TICKS_PER_SECOND,
	/* a second's stall backs up for this long, at most this many times a route */
	VEHICLE_NAVIGATION_REVERSE_TICKS = 3 * TICKS_PER_SECOND / 2,
	VEHICLE_NAVIGATION_REVERSES_MAX = 2,
	/* waypoints in the corridor ahead checked for a learnt blockage */
	VEHICLE_NAVIGATION_BLOCK_LOOKAHEAD = 6,
};

/* every waypoint gets this long, plus a time per unit of distance */
#define VEHICLE_NAVIGATION_WAYPOINT_SECONDS 4.f
#define VEHICLE_NAVIGATION_SECONDS_PER_UNIT 0.5f
/* a goal this far from the searched one is searched again */
#define VEHICLE_NAVIGATION_REPLAN_DISTANCE 8.f
/* less movement than this in a second, with the throttle open, is a stall */
#define VEHICLE_NAVIGATION_STALL_DISTANCE 0.3f
/* the route's ends are placed on the ground within these */
#define VEHICLE_NAVIGATION_GROUND_DEPTH 3.f
#define VEHICLE_NAVIGATION_GROUND_HORIZONTAL 1.5f
/* a later waypoint is steered at only within this angle of the nearer
one, so that looking ahead never cuts a corner the route turns */
#define VEHICLE_NAVIGATION_LOOKAHEAD_ANGLE 0.45f /* radians, about 25 degrees */
#define VEHICLE_NAVIGATION_FULL_THROTTLE_ANGLE 0.45f
#define VEHICLE_NAVIGATION_SLOW_ANGLE 1.75f /* about 100 degrees */
#define VEHICLE_NAVIGATION_SLOW_THROTTLE 0.35f
#define VEHICLE_NAVIGATION_MINIMUM_THROTTLE 0.3f

/* ---------- structures */

struct vehicle_driver
{
	short bot_number; /* 0: a free slot */
	long vehicle_index;
	long polled; /* the tick it was last asked after */
	uint64_t token;
	boolean searching;
	boolean following;
	boolean check_goal_segment;
	struct sn_point goal;
	struct sn_route route;
	uint32_t waypoint;
	long search_tick;
	long progress_tick;
	struct sn_point progress_position;
	short stalls;
	short reverses;
	long reverse_until;
	uint32_t deadline_waypoint;
	long waypoint_deadline;
};

/* ---------- globals */

/* Only this host's simulation thread uses these. Client n is slot n. Tokens
are never reused, so a delayed consumer cannot take another's work. */
static struct sn_jobs vehicle_jobs;
static struct vehicle_driver vehicle_drivers[VEHICLE_NAVIGATION_DRIVERS_MAX];
static uint64_t vehicle_token;

typedef char vehicle_navigation_clients_check[
	VEHICLE_NAVIGATION_DRIVERS_MAX <= SN_JOBS_CLIENTS_MAX ? 1 : -1];

/* ---------- private code */

static real vehicle_nav_abs(real value)
{
	return value < 0.f ? -value : value;
}

static real vehicle_nav_horizontal(struct sn_point a, struct sn_point b)
{
	real dx = a.x - b.x;
	real dy = a.y - b.y;

	return square_root(dx * dx + dy * dy);
}

static real vehicle_nav_angle(real angle)
{
	/* (two turns at most: every input here is a difference of two angles) */
	if (angle > _pi)
		angle -= 2.f * _pi;
	if (angle > _pi)
		angle -= 2.f * _pi;
	if (angle < -_pi)
		angle += 2.f * _pi;
	if (angle < -_pi)
		angle += 2.f * _pi;
	return angle;
}

static real vehicle_nav_yaw(struct sn_point from, struct sn_point to)
{
	return arctangent(to.y - from.y, to.x - from.x);
}

static void vehicle_nav_reset_path(struct vehicle_driver *driver, uint32_t client)
{
	sn_jobs_cancel(&vehicle_jobs, client, driver->token);
	driver->searching = FALSE;
	driver->following = FALSE;
	driver->stalls = 0;
	driver->reverses = 0;
	driver->reverse_until = 0;
	driver->progress_tick = NONE;
	driver->waypoint_deadline = NONE;
}

static void vehicle_nav_free(struct vehicle_driver *driver, uint32_t client)
{
	vehicle_nav_reset_path(driver, client);
	driver->bot_number = 0;
	driver->vehicle_index = NONE;
}

/* the scheduler's collision check for a client: the vehicle layer's pill,
against the structure and scenery, ignoring the slot's vehicle */
static bool vehicle_nav_search_clear(void *context, uint32_t client, uint64_t token,
	struct sn_point a, uint32_t ap, struct sn_point b, uint32_t bp)
{
	struct unit_datum *vehicle;
	long vehicle_index;

	(void)context;
	if (client >= VEHICLE_NAVIGATION_DRIVERS_MAX || token != vehicle_drivers[client].token)
		return false;
	vehicle_index = vehicle_drivers[client].vehicle_index;
	vehicle = vehicle_index != NONE ? vehicle_try_and_get(vehicle_index) : NULL;
	if (!vehicle || TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit))
		return false;
	return navigation_world_vehicle_search_clear(&vehicle_index, a, ap, b, bp);
}

/* the bot's slot, else a free one (NONE if all are in use) */
static short vehicle_nav_slot(short bot_number, boolean allocate)
{
	short index;
	short free_slot = NONE;

	for (index = 0; index < VEHICLE_NAVIGATION_DRIVERS_MAX; index++)
	{
		if (vehicle_drivers[index].bot_number == bot_number)
			return index;
		if (free_slot == NONE && vehicle_drivers[index].bot_number == 0)
			free_slot = index;
	}
	if (!allocate || free_slot == NONE)
		return NONE;
	csmemset(&vehicle_drivers[free_slot], 0, sizeof(vehicle_drivers[free_slot]));
	vehicle_drivers[free_slot].bot_number = bot_number;
	vehicle_drivers[free_slot].vehicle_index = NONE;
	vehicle_drivers[free_slot].progress_tick = NONE;
	vehicle_drivers[free_slot].waypoint_deadline = NONE;
	return free_slot;
}

/* the vehicle's ground point and polygon on the layer (FALSE off it) */
static boolean vehicle_nav_ground(struct sn_resource const *resource, struct sn_point point,
	uint32_t *polygon, struct sn_point *ground)
{
	return sn_project_below(resource, point, VEHICLE_NAVIGATION_GROUND_DEPTH, polygon, ground) == SN_FOUND;
}

static enum bot_navigation_result vehicle_nav_search(struct vehicle_driver *driver, uint32_t client,
	struct sn_resource const *resource, struct sn_point start)
{
	struct sn_jobs_request request;
	struct sn_jobs_diagnostics diagnostics;
	enum sn_status status;
	long now = game_time_get();

	if (driver->searching && !sn_jobs_active(&vehicle_jobs, client, driver->token))
		driver->searching = FALSE; /* (dropped by a reset: ask again) */
	if (!driver->searching)
	{
		sn_jobs_cancel(&vehicle_jobs, client, driver->token);
		csmemset(&request, 0, sizeof(request));
		request.start = start;
		request.goal = driver->goal;
		request.ground_depth = VEHICLE_NAVIGATION_GROUND_DEPTH;
		request.ground_horizontal = VEHICLE_NAVIGATION_GROUND_HORIZONTAL;
		request.obstacle_revision = 0;
		request.check_goal_segment = driver->check_goal_segment ? true : false;
		request.straighten = true;
		driver->token = ++vehicle_token;
		if (!sn_jobs_submit(&vehicle_jobs, client, driver->token, resource, &request))
		{
			platform_log("bots: bot %d vehicle search not queued: start (%.1f %.1f %.1f) goal (%.1f %.1f %.1f)",
				driver->bot_number, start.x, start.y, start.z, driver->goal.x, driver->goal.y, driver->goal.z);
			vehicle_nav_reset_path(driver, client);
			return BOT_NAVIGATION_FAILED;
		}
		driver->searching = TRUE;
		driver->search_tick = now;
		return BOT_NAVIGATION_WAITING;
	}
	if (sn_jobs_status(&vehicle_jobs, client, driver->token) == SN_RUNNING)
		return BOT_NAVIGATION_WAITING;
	status = sn_jobs_take(&vehicle_jobs, client, driver->token, resource, 0, &driver->route, &diagnostics);
	driver->searching = FALSE;
	if (status != SN_FOUND || driver->route.count == 0)
	{
		platform_log("bots: bot %d vehicle search failed: %s start (%.1f %.1f %.1f) goal (%.1f %.1f %.1f) "
			"rejected-edges %lu", driver->bot_number, sn_status_name(status),
			diagnostics.start_point.x, diagnostics.start_point.y, diagnostics.start_point.z,
			diagnostics.goal_point.x, diagnostics.goal_point.y, diagnostics.goal_point.z,
			(unsigned long)diagnostics.rejected_edges);
		vehicle_nav_reset_path(driver, client);
		return BOT_NAVIGATION_FAILED;
	}
	driver->following = TRUE;
	driver->waypoint = driver->route.count > 1 ? 1 : 0;
	driver->progress_tick = now;
	driver->progress_position = start;
	driver->stalls = 0;
	driver->reverses = 0;
	driver->waypoint_deadline = NONE;
	platform_log("bots: bot %d vehicle route goal (%.1f %.1f %.1f) waypoints %lu search %ld ticks",
		driver->bot_number, driver->route.points[driver->route.count - 1].x,
		driver->route.points[driver->route.count - 1].y, driver->route.points[driver->route.count - 1].z,
		(unsigned long)driver->route.count, now - driver->search_tick);
	return BOT_NAVIGATION_MOVING;
}

/* a learnt blockage: the corridor polygon the vehicle is trying to reach */
static enum bot_navigation_result vehicle_nav_stuck(struct vehicle_driver *driver, uint32_t client,
	char const *reason)
{
	uint32_t polygon = driver->waypoint < driver->route.count ? driver->route.polygons[driver->waypoint] : SN_NONE;

	if (polygon != SN_NONE)
		navigation_world_vehicle_block(polygon);
	platform_log("bots: bot %d vehicle route blocked at polygon %ld (%s); choosing again",
		driver->bot_number, polygon == SN_NONE ? -1L : (long)polygon, reason);
	vehicle_nav_reset_path(driver, client);
	return BOT_NAVIGATION_FAILED;
}

static enum bot_navigation_result vehicle_nav_follow(struct vehicle_driver *driver, uint32_t client,
	struct sn_resource const *resource, struct unit_datum const *vehicle, struct sn_point origin,
	struct vehicle_navigation_command *command)
{
	struct vehicle_steer_input input;
	float half_width;
	float half_length = 1.f;
	long now = game_time_get();
	uint32_t index;
	uint32_t polygon;
	struct sn_point ground;
	struct sn_point target;
	long vehicle_index = driver->vehicle_index;

	if (!sn_route_current(&driver->route, resource, 0))
	{
		vehicle_nav_reset_path(driver, client);
		return BOT_NAVIGATION_FAILED;
	}
	navigation_world_vehicle_body(&half_width, &half_length, NULL);
	input.origin = origin;
	input.forward_yaw = arctangent(vehicle->object.forward.j, vehicle->object.forward.i);
	input.half_length = half_length;
	input.points = driver->route.points;
	input.count = driver->route.count;
	input.waypoint = driver->waypoint;
	if (!vehicle_navigation_steer(&input, command))
	{
		platform_log("bots: bot %d vehicle route arrived at (%.1f %.1f %.1f)", driver->bot_number,
			origin.x, origin.y, origin.z);
		vehicle_nav_reset_path(driver, client);
		return BOT_NAVIGATION_ARRIVED;
	}
	driver->waypoint = input.waypoint;

	/* (a blockage learnt on the corridor ahead: search again) */
	for (index = driver->waypoint; index < driver->route.count &&
		index < driver->waypoint + VEHICLE_NAVIGATION_BLOCK_LOOKAHEAD; index++)
	{
		if (navigation_world_vehicle_polygon_blocked(driver->route.polygons[index]))
		{
			vehicle_nav_reset_path(driver, client);
			return BOT_NAVIGATION_FAILED;
		}
	}

	/* backing up out of a stall, holding the heading */
	if (now < driver->reverse_until)
	{
		command->has_drive = TRUE;
		command->yaw = input.forward_yaw;
		command->throttle = -1.f;
		return BOT_NAVIGATION_MOVING;
	}

	/* every waypoint has a deadline: a vehicle that circles one it cannot
	reach gives the route up */
	if (driver->waypoint_deadline == NONE || driver->deadline_waypoint != driver->waypoint)
	{
		driver->deadline_waypoint = driver->waypoint;
		driver->waypoint_deadline = now + (long)((VEHICLE_NAVIGATION_WAYPOINT_SECONDS +
			VEHICLE_NAVIGATION_SECONDS_PER_UNIT * vehicle_nav_horizontal(origin,
			driver->route.points[driver->waypoint])) * TICKS_PER_SECOND);
	}
	else if (now > driver->waypoint_deadline)
		return vehicle_nav_stuck(driver, client, "waypoint deadline");

	/* a vehicle or machine in the way: wait for it (the deadline still runs) */
	target = driver->route.points[input.target];
	polygon = driver->route.polygons[input.target];
	if (vehicle_nav_ground(resource, origin, &index, &ground) &&
		!navigation_world_vehicle_occupant_clear(&vehicle_index, ground, polygon, target, polygon))
	{
		command->throttle = 0.f;
		return BOT_NAVIGATION_WAITING;
	}

	if (driver->progress_tick == NONE)
	{
		driver->progress_tick = now;
		driver->progress_position = origin;
	}
	else if (now - driver->progress_tick >= VEHICLE_NAVIGATION_PROGRESS_TICKS)
	{
		driver->stalls = vehicle_nav_horizontal(origin, driver->progress_position) <
			VEHICLE_NAVIGATION_STALL_DISTANCE ? (short)(driver->stalls + 1) : 0;
		driver->progress_tick = now;
		driver->progress_position = origin;
		if (driver->stalls >= 2)
		{
			if (driver->reverses >= VEHICLE_NAVIGATION_REVERSES_MAX)
				return vehicle_nav_stuck(driver, client, "stalled after backing up");
			driver->reverses++;
			driver->stalls = 0;
			driver->reverse_until = now + VEHICLE_NAVIGATION_REVERSE_TICKS;
			/* (the waypoint's time starts again after backing up) */
			driver->waypoint_deadline = NONE;
			platform_log("bots: bot %d vehicle stalled at (%.1f %.1f %.1f); backing up",
				driver->bot_number, origin.x, origin.y, origin.z);
		}
	}
	return BOT_NAVIGATION_MOVING;
}

/* ---------- public code */

void vehicle_navigation_reset_all(void)
{
	uint32_t client;

	sn_jobs_reset(&vehicle_jobs);
	for (client = 0; client < VEHICLE_NAVIGATION_DRIVERS_MAX; client++)
	{
		csmemset(&vehicle_drivers[client], 0, sizeof(vehicle_drivers[client]));
		vehicle_drivers[client].vehicle_index = NONE;
		vehicle_drivers[client].progress_tick = NONE;
		vehicle_drivers[client].waypoint_deadline = NONE;
	}
}

void vehicle_navigation_update(void)
{
	struct sn_resource const *resource = navigation_world_vehicle_resource();
	long now = game_time_get();
	uint32_t client;

	/* (VEHICLE_NAVIGATION_DRIVERS_MAX slots) a slot nobody asked after last
	tick or this one is let go: its bot stopped driving, left or died */
	for (client = 0; client < VEHICLE_NAVIGATION_DRIVERS_MAX; client++)
	{
		struct vehicle_driver *driver = &vehicle_drivers[client];

		if (driver->bot_number != 0 && (driver->polled > now || now - driver->polled > 1))
			vehicle_nav_free(driver, client);
	}
	if (!resource)
		return;
	sn_jobs_step(&vehicle_jobs, resource, 0, (uint64_t)now, vehicle_nav_search_clear, NULL);
}

void vehicle_navigation_release(short bot_number)
{
	short slot = bot_number > 0 ? vehicle_nav_slot(bot_number, FALSE) : NONE;

	if (slot != NONE)
		vehicle_nav_free(&vehicle_drivers[slot], (uint32_t)slot);
}

uint32_t vehicle_navigation_component(struct sn_point point)
{
	struct sn_resource const *resource = navigation_world_vehicle_resource();
	struct sn_point ground;
	uint32_t polygon;

	if (!resource || !vehicle_nav_ground(resource, point, &polygon, &ground))
		return SN_NONE;
	return sn_component(resource, polygon);
}

enum bot_navigation_result vehicle_navigation_drive(
	short bot_number,
	long vehicle_index,
	struct sn_point goal,
	real arrival_distance,
	boolean check_goal_segment,
	struct vehicle_navigation_command *command)
{
	struct sn_resource const *resource = navigation_world_vehicle_resource();
	struct unit_datum *vehicle = vehicle_index != NONE ? vehicle_try_and_get(vehicle_index) : NULL;
	struct vehicle_driver *driver;
	struct sn_point origin;
	short slot;
	long now = game_time_get();

	if (!command)
		return BOT_NAVIGATION_FAILED;
	csmemset(command, 0, sizeof(*command));
	if (!resource || bot_number <= 0)
		return BOT_NAVIGATION_UNAVAILABLE;
	if (!vehicle || TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) ||
		!valid_real(goal.x) || !valid_real(goal.y) || !valid_real(goal.z) || !(arrival_distance >= 0.f))
		return BOT_NAVIGATION_FAILED;
	slot = vehicle_nav_slot(bot_number, TRUE);
	if (slot == NONE)
		return BOT_NAVIGATION_UNAVAILABLE;
	driver = &vehicle_drivers[slot];
	driver->polled = now;
	if (driver->vehicle_index != vehicle_index)
	{
		vehicle_nav_reset_path(driver, (uint32_t)slot);
		driver->vehicle_index = vehicle_index;
	}
	origin.x = vehicle->object.position.x;
	origin.y = vehicle->object.position.y;
	origin.z = vehicle->object.position.z;
	if (vehicle_nav_horizontal(origin, goal) <= arrival_distance)
	{
		vehicle_nav_reset_path(driver, (uint32_t)slot);
		return BOT_NAVIGATION_ARRIVED;
	}
	if ((driver->searching || driver->following) &&
		(vehicle_nav_horizontal(driver->goal, goal) > VEHICLE_NAVIGATION_REPLAN_DISTANCE ||
		vehicle_nav_abs(driver->goal.z - goal.z) > VEHICLE_NAVIGATION_REPLAN_DISTANCE))
		vehicle_nav_reset_path(driver, (uint32_t)slot);
	if (!driver->searching && !driver->following)
	{
		driver->goal = goal;
		driver->check_goal_segment = check_goal_segment;
	}
	if (!driver->following)
	{
		enum bot_navigation_result result = vehicle_nav_search(driver, (uint32_t)slot, resource, origin);

		if (result != BOT_NAVIGATION_MOVING)
			return result;
	}
	return vehicle_nav_follow(driver, (uint32_t)slot, resource, vehicle, origin, command);
}

boolean vehicle_navigation_steer(
	struct vehicle_steer_input *input,
	struct vehicle_navigation_command *command)
{
	real reach;
	real lookahead;
	real yaw;
	real error;
	real throttle;
	real remaining;
	uint32_t target;
	struct sn_point end;

	if (!input || !command || !input->points || input->count == 0)
		return FALSE;
	csmemset(command, 0, sizeof(*command));
	reach = MAX(0.8f * input->half_length, 0.6f);
	lookahead = MAX(4.f * input->half_length, 2.f);
	/* (at most count waypoints are passed; one up to the lookahead away
	counts as passed once the vehicle is beyond it along the route: past the
	line through it square to the way on to the next) */
	while (input->waypoint < input->count)
	{
		struct sn_point next = input->points[input->waypoint];
		real away = vehicle_nav_horizontal(input->origin, next);

		if (away >= reach)
		{
			struct sn_point after;

			if (input->waypoint + 1 >= input->count || away >= lookahead)
				break;
			after = input->points[input->waypoint + 1];
			if ((input->origin.x - next.x) * (after.x - next.x) +
				(input->origin.y - next.y) * (after.y - next.y) <= 0.f)
				break;
		}
		input->waypoint++;
	}
	if (input->waypoint >= input->count)
		return FALSE;

	/* look ahead along a straight stretch, never round a corner (at most
	count waypoints) */
	target = input->waypoint;
	while (target + 1 < input->count &&
		vehicle_nav_horizontal(input->origin, input->points[target]) < lookahead &&
		vehicle_nav_abs(vehicle_nav_angle(vehicle_nav_yaw(input->origin, input->points[target + 1]) -
			vehicle_nav_yaw(input->origin, input->points[input->waypoint]))) < VEHICLE_NAVIGATION_LOOKAHEAD_ANGLE)
		target++;
	input->target = target;

	yaw = vehicle_nav_yaw(input->origin, input->points[target]);
	error = vehicle_nav_abs(vehicle_nav_angle(yaw - input->forward_yaw));
	if (error <= VEHICLE_NAVIGATION_FULL_THROTTLE_ANGLE)
		throttle = 1.f;
	else if (error >= VEHICLE_NAVIGATION_SLOW_ANGLE)
		throttle = VEHICLE_NAVIGATION_SLOW_THROTTLE;
	else
		throttle = 1.f - (1.f - VEHICLE_NAVIGATION_SLOW_THROTTLE) *
			(error - VEHICLE_NAVIGATION_FULL_THROTTLE_ANGLE) /
			(VEHICLE_NAVIGATION_SLOW_ANGLE - VEHICLE_NAVIGATION_FULL_THROTTLE_ANGLE);
	end = input->points[input->count - 1];
	remaining = vehicle_nav_horizontal(input->origin, end);
	if (remaining < 4.f * input->half_length)
		throttle = MIN(throttle, MAX(VEHICLE_NAVIGATION_MINIMUM_THROTTLE, remaining / (4.f * input->half_length)));
	command->has_drive = TRUE;
	command->yaw = yaw;
	command->throttle = throttle;
	return TRUE;
}

#endif /* HALO_FEATURE_BOTS */
