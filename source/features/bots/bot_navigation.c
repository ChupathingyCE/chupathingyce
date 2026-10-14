/*
BOT_NAVIGATION.C

port: normal bots' BSP navigation and lane policy. The old start-location graph
is too sparse to represent walls, passages or the spaces between starts. This
adapter routes over CE's validated walkable collision surfaces, checking each
route edge with the spawned biped's live collision capsule before moving.

Each active bot owns one bounded route; its searches queue for four shared
workspaces (engine_ai/surface_navigation_jobs.h), which together place at most
four searches' endpoints, expand at most 64 polygons and string-pull one found
route per tick, however many bots there are. A blocked route waits for a
bounded interval, marks one polygon temporarily unavailable and replans. Team
bots patrol three lanes:
pairs travel together in a lane, with opposite lateral offsets, and pairs in a
lane occupy near/far positions. No map tags are changed and no movement bypasses
Halo physics.
*/

/* ---------- headers */

#include "cseries.h"
#include "game.h"
#include "game_engine.h"
#include "players_runtime.h"
#include "scenario/scenario_definitions.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/biped_definitions.h"
#include "units/bipeds.h"
#include "real_math.h"
#include "navigation_world.h"
#include "map_anchors.h"
#include "bot_navigation.h"
#include "engine_ai/surface_navigation_jobs.h"

/* port/linux/src/platform.h's */
void platform_log(char const *format, ...);

#ifdef HALO_FEATURE_BOTS

/* ---------- constants */

enum
{
	BOT_NAVIGATION_TICKS_PER_METRE = TICKS_PER_SECOND, /* (bots walk faster than 1 m/s) */
	BOT_NAVIGATION_PROGRESS_TICKS = TICKS_PER_SECOND,
	BOT_NAVIGATION_REPLAN_DISTANCE = 12,
	BOT_NAVIGATION_GOAL_ATTEMPTS = 16,
};

#define BOT_NAVIGATION_MINIMUM_GOAL_DISTANCE 12.f
#define BOT_NAVIGATION_LANE_SIDE_OFFSET 2.5f
#define BOT_NAVIGATION_NEAR_PROGRESS 0.48f
#define BOT_NAVIGATION_FAR_PROGRESS 0.82f

/* Bot queries and routes are keyed to the BSP resource only, not to the global
obstacle revision: one bot's temporary blockage must not discard every other
bot's route. Each bot checks its own next polygons instead (bot_nav_follow). */
#define BOT_NAVIGATION_ROUTE_OBSTACLES 0
#define BOT_NAVIGATION_BLOCK_LOOKAHEAD 6
/* An airborne bot, or a target in a seat or mid-jump, is above the floor it will
land on: look down this far, in bands of half the projection height; a point
against a wall or on a seam may move this far toward a usable polygon. */
#define BOT_NAVIGATION_GROUND_DEPTH 3.f
#define BOT_NAVIGATION_GROUND_HORIZONTAL 1.f

/* Only this host's simulation thread uses these. Client n is bot number n+1.
Tokens are never reused, even across map resets, so a delayed consumer cannot
take another request's work; no borrowed unit pointers survive a tick. */
static struct sn_jobs navigation_jobs;
static long navigation_units[BOT_NAVIGATION_AGENTS_MAX]; /* the body its checks ignore */
static uint64_t navigation_tokens[BOT_NAVIGATION_AGENTS_MAX]; /* its latest request */
static long navigation_polled[BOT_NAVIGATION_AGENTS_MAX]; /* when its agent last asked */
static uint64_t navigation_token;

/* ---------- private prototypes */

static real bot_nav_abs(real value);
static real bot_nav_horizontal_distance(struct sn_point a, struct sn_point b);
static real bot_nav_distance(struct sn_point a, struct sn_point b);
static struct sn_point bot_nav_position(long unit_index);
static boolean bot_nav_client(struct bot_navigation_agent const *agent, uint32_t *client);
static boolean bot_nav_refresh(struct bot_navigation_agent *agent, struct sn_point start,
	struct sn_point goal);
static boolean bot_nav_lane_axis(long team_index, real_point3d *home,
	real *forward_x, real *forward_y, real *length);
static uint32_t bot_nav_start_component(struct bot_navigation_agent *agent,
	struct sn_resource const *resource, struct sn_point start);
static struct sn_point bot_nav_ground(struct sn_resource const *resource,
	struct sn_point point);
static boolean bot_nav_lane_goal(struct bot_navigation_agent *agent,
	struct sn_resource const *resource, struct sn_point start, struct sn_point *goal);
static boolean bot_nav_random_goal(struct bot_navigation_agent *agent,
	struct sn_resource const *resource, struct sn_point start,
	struct engine_ai_rng32_state *rng, struct sn_point *goal);
static enum bot_navigation_result bot_nav_route_step(struct bot_navigation_agent *agent,
	long unit_index, struct sn_point goal, real speed, real arrival_distance,
	boolean patrol, struct bot_navigation_command *command);
static void bot_nav_log_failure(struct bot_navigation_agent *agent,
	struct sn_resource const *resource, enum sn_status status,
	struct sn_jobs_diagnostics const *query);
static enum bot_navigation_result bot_nav_search(struct bot_navigation_agent *agent,
	long unit_index, struct sn_resource const *resource, struct sn_point start);
static enum bot_navigation_result bot_nav_stuck(struct bot_navigation_agent *agent,
	struct sn_resource const *resource, struct sn_point origin, boolean patrol,
	char const *reason);
static enum bot_navigation_result bot_nav_follow(struct bot_navigation_agent *agent,
	long unit_index, struct sn_resource const *resource, struct sn_point origin,
	real speed, boolean patrol, struct bot_navigation_command *command);

/* ---------- public code */

/* The scheduler's collision check for a client: the structure and scenery
(navigation_world_search_clear), ignoring the body its bot has now. */
static bool bot_nav_search_clear(void *context, uint32_t client, uint64_t token,
	struct sn_point a, uint32_t ap, struct sn_point b, uint32_t bp)
{
	struct unit_datum *unit;
	long unit_index;

	(void)context;
	if (client >= BOT_NAVIGATION_AGENTS_MAX || token != navigation_tokens[client])
		return false;
	unit_index = navigation_units[client];
	unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return false;
	return navigation_world_search_clear(&unit_index, a, ap, b, bp);
}

void bot_navigation_reset_all(void)
{
	uint32_t client;

	sn_jobs_reset(&navigation_jobs);
	for (client = 0; client < BOT_NAVIGATION_AGENTS_MAX; client++)
	{
		navigation_units[client] = NONE;
		navigation_tokens[client] = 0;
		navigation_polled[client] = NONE;
	}
}

void bot_navigation_update(void)
{
	struct sn_resource const *resource = navigation_world_resource();
	long now = game_time_get();
	uint32_t client;

	if (!resource)
	{
		bot_navigation_reset_all();
		return;
	}
	/* (at most 32 clients) A request its agent did not ask after last tick
	was abandoned: its leaf changed, or its bot left or was replaced. One whose
	body died or was deleted cannot be checked. Either gives its place back. */
	for (client = 0; client < BOT_NAVIGATION_AGENTS_MAX; client++)
	{
		struct unit_datum *unit;

		if (!sn_jobs_active(&navigation_jobs, client, navigation_tokens[client]))
			continue;
		unit = navigation_units[client] != NONE ? unit_try_and_get(navigation_units[client]) : NULL;
		if (unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit) &&
			navigation_polled[client] != NONE && navigation_polled[client] <= now &&
			now - navigation_polled[client] <= 1)
		{
			continue;
		}
		sn_jobs_cancel(&navigation_jobs, client, navigation_tokens[client]);
		navigation_units[client] = NONE;
	}
	sn_jobs_step(&navigation_jobs, resource, BOT_NAVIGATION_ROUTE_OBSTACLES,
		(uint64_t)now, bot_nav_search_clear, NULL);
}

void bot_navigation_agent_initialize(
	struct bot_navigation_agent *agent,
	short bot_number,
	long team_index,
	long team_rank)
{
	long squad;

	if (!agent)
		return;
	/* (a request left by this slot's last bot is not this one's) */
	if (bot_number > 0 && bot_number <= BOT_NAVIGATION_AGENTS_MAX)
	{
		sn_jobs_cancel(&navigation_jobs, (uint32_t)(bot_number - 1),
			navigation_tokens[bot_number - 1]);
		navigation_units[bot_number - 1] = NONE;
	}
	csmemset(agent, 0, sizeof(*agent));
	agent->bot_number = bot_number;
	agent->team_index = team_index;
	agent->team_rank = team_rank;
	agent->lane_index = NONE;
	agent->progress_tick = NONE;
	if (team_index == NONE || team_rank < 0)
		return;

	squad = team_rank / BOT_NAVIGATION_SQUAD_SIZE;
	agent->lane_index = (short)(squad % BOT_NAVIGATION_LANES);
	agent->squad_index = squad / BOT_NAVIGATION_LANES;
	agent->lane_phase = (short)(agent->squad_index % 2);
	agent->lane_side = team_rank % BOT_NAVIGATION_SQUAD_SIZE == 0 ? -1 : 1;
	agent->lane_assigned = TRUE;
}

void bot_navigation_agent_reset_path(
	struct bot_navigation_agent *agent)
{
	uint32_t client;

	if (!agent)
		return;
	if (bot_nav_client(agent, &client))
	{
		sn_jobs_cancel(&navigation_jobs, client, navigation_tokens[client]);
		navigation_units[client] = NONE;
	}
	if (agent->searching)
	{
		platform_log("bots: bot %d surface %s search abandoned after %ld ticks",
			agent->bot_number, agent->patrol_goal ? "patrol" : "tactical",
			game_time_get() - agent->search_tick);
	}
	agent->searching = FALSE;
	agent->following = FALSE;
	agent->goal_valid = FALSE;
	agent->waypoint = 0;
	agent->route.count = 0;
	agent->progress_tick = NONE;
	agent->stuck_count = 0;
}

enum bot_navigation_result bot_navigation_agent_move_to(
	struct bot_navigation_agent *agent,
	long unit_index,
	struct sn_point goal,
	real speed,
	real arrival_distance,
	struct bot_navigation_command *command)
{
	if (!agent || !command)
		return BOT_NAVIGATION_FAILED;
	csmemset(command, 0, sizeof(*command));
	return bot_nav_route_step(agent, unit_index, goal, speed, arrival_distance,
		FALSE, command);
}

enum bot_navigation_result bot_navigation_agent_roam(
	struct bot_navigation_agent *agent,
	long unit_index,
	struct engine_ai_rng32_state *rng,
	struct bot_navigation_command *command)
{
	struct sn_resource const *resource;
	struct sn_point start;
	struct sn_point goal;
	boolean selected;
	enum bot_navigation_result result;

	if (!agent || !rng || !command)
		return BOT_NAVIGATION_FAILED;
	csmemset(command, 0, sizeof(*command));
	resource = navigation_world_resource();
	if (!resource)
		return BOT_NAVIGATION_UNAVAILABLE;

	if (agent->searching || agent->following)
		goal = agent->goal;
	else
	{
		start = bot_nav_position(unit_index);
		selected = agent->lane_assigned ?
			bot_nav_lane_goal(agent, resource, start, &goal) :
			bot_nav_random_goal(agent, resource, start, rng, &goal);
		if (!selected)
			return BOT_NAVIGATION_FAILED;
		agent->goal = goal;
		agent->goal_valid = TRUE;
	}

	result = bot_nav_route_step(agent, unit_index, goal, 1.f, 0.6f, TRUE, command);
	if (result == BOT_NAVIGATION_FAILED && agent->lane_assigned)
		agent->lane_phase = (short)(1 - agent->lane_phase);
	return result;
}

/* ---------- private code: positions and lane targets */

static real bot_nav_abs(
	real value)
{
	return value < 0.f ? -value : value;
}

static real bot_nav_horizontal_distance(
	struct sn_point a,
	struct sn_point b)
{
	real x = a.x - b.x;
	real y = a.y - b.y;

	return square_root(x * x + y * y);
}

static real bot_nav_distance(
	struct sn_point a,
	struct sn_point b)
{
	real x = a.x - b.x;
	real y = a.y - b.y;
	real z = a.z - b.z;

	return square_root(x * x + y * y + z * z);
}

/* An agent's scheduler client: its bot number less one. */
static boolean bot_nav_client(
	struct bot_navigation_agent const *agent,
	uint32_t *client)
{
	if (agent->bot_number <= 0 || agent->bot_number > BOT_NAVIGATION_AGENTS_MAX)
		return FALSE;
	*client = (uint32_t)(agent->bot_number - 1);
	return TRUE;
}

/* A queued search takes the bot's new position and goal in place, keeping
its turn; FALSE once it has begun. */
static boolean bot_nav_refresh(
	struct bot_navigation_agent *agent,
	struct sn_point start,
	struct sn_point goal)
{
	uint32_t client;

	return bot_nav_client(agent, &client) &&
		sn_jobs_refresh(&navigation_jobs, client, agent->search_token, start, goal);
}

/* Surface-navigation points use the ground-foot coordinate, not the pill's
centered origin. This is the same adjustment as the verified Bot 1 probe. */
static struct sn_point bot_nav_position(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct biped_definition *definition = biped_definition_get(unit->definition_index);
	real_point3d origin;
	struct sn_point point;

	object_get_origin(unit_index, &origin);
	if (TEST_FLAG(definition->biped.flags, _biped_pill_centered_at_origin_bit))
		origin.z -= definition->biped.collision_radius;
	point.x = origin.x;
	point.y = origin.y;
	point.z = origin.z;
	return point;
}

/* The direction of advance runs from this team's base to the enemy's: their
CTF flags, which a map places in its bases whatever the game type. A map with
no flags falls back to the average team start locations, which on a map like
Blood Gulch are spread far from the bases (a 22.6 m axis between flags 97.6 m
apart), so the flags come first. */
boolean bot_navigation_team_bases(
	long team_index,
	real_point3d *home,
	real_point3d *enemy)
{
	short count = player_get_starting_location_count();
	short index;
	long enemy_team = team_index == 0 ? 1 : 0;
	long home_count = 0;
	long enemy_count = 0;

	if (!home || !enemy || (team_index != 0 && team_index != 1))
		return FALSE;
	csmemset(home, 0, sizeof(*home));
	csmemset(enemy, 0, sizeof(*enemy));
	if (map_anchors_team_base(team_index, home) && map_anchors_team_base(enemy_team, enemy))
		return TRUE;
	csmemset(home, 0, sizeof(*home));
	csmemset(enemy, 0, sizeof(*enemy));
	/* (at most the scenario's starting locations, a tag block's count) */
	for (index = 0; index < count; index++)
	{
		struct player_starting_location const *location = player_get_starting_location(index);

		if (!location)
			continue;
		if (location->team_index == team_index)
		{
			home->x += location->position.x;
			home->y += location->position.y;
			home->z += location->position.z;
			home_count++;
		}
		else if (location->team_index == enemy_team)
		{
			enemy->x += location->position.x;
			enemy->y += location->position.y;
			enemy->z += location->position.z;
			enemy_count++;
		}
	}
	if (home_count == 0 || enemy_count == 0)
		return FALSE;
	home->x /= (real)home_count;
	home->y /= (real)home_count;
	home->z /= (real)home_count;
	enemy->x /= (real)enemy_count;
	enemy->y /= (real)enemy_count;
	enemy->z /= (real)enemy_count;
	return TRUE;
}

static boolean bot_nav_lane_axis(
	long team_index,
	real_point3d *home,
	real *forward_x,
	real *forward_y,
	real *length)
{
	real_point3d enemy;
	real dx;
	real dy;

	if (!bot_navigation_team_bases(team_index, home, &enemy))
		return FALSE;
	dx = enemy.x - home->x;
	dy = enemy.y - home->y;
	*length = square_root(dx * dx + dy * dy);
	if (*length < 1.f)
		return FALSE;
	*forward_x = dx / *length;
	*forward_y = dy / *length;
	return TRUE;
}

/* Find the floor below a point, or a usable point within 1 m when the point
lies at a wall or seam. The route's collision checks still decide whether the
projected endpoint can be approached. Failed projection leaves it unchanged. */
static struct sn_point bot_nav_ground(
	struct sn_resource const *resource,
	struct sn_point point)
{
	struct sn_point projected;
	uint32_t polygon;

	return sn_project_near(resource, point, BOT_NAVIGATION_GROUND_DEPTH, 1.f, &polygon, &projected) ==
		SN_FOUND ? projected : point;
}

/* The static component the bot stands in. Goals outside it cannot be reached
whatever the collision checks say, so they are never selected. */
static uint32_t bot_nav_start_component(
	struct bot_navigation_agent *agent,
	struct sn_resource const *resource,
	struct sn_point start)
{
	uint32_t polygon;
	struct sn_point projected;

	if (sn_project(resource, bot_nav_ground(resource, start), &polygon, &projected) != SN_FOUND)
	{
		platform_log("bots: bot %d patrol goal not chosen: start (%.1f %.1f %.1f) does not project "
			"(bad-start)", agent->bot_number, start.x, start.y, start.z);
		return SN_NONE;
	}
	return sn_component(resource, polygon);
}

/* Pick a walkable surface near this bot's lane and assigned depth, among the
polygons the bot can reach (same static component, center projectable). */
static boolean bot_nav_lane_goal(
	struct bot_navigation_agent *agent,
	struct sn_resource const *resource,
	struct sn_point start,
	struct sn_point *goal)
{
	real_point3d home;
	real forward_x;
	real forward_y;
	real length;
	real lateral_min = 0.f;
	real lateral_max = 0.f;
	real desired_progress;
	real desired_lateral;
	real best_cost = 100000000.f;
	real actual_progress;
	real actual_lateral;
	real unfiltered_cost = 100000000.f;
	uint32_t best = SN_NONE;
	uint32_t unfiltered = SN_NONE;
	uint32_t component;
	uint32_t index;
	boolean first = TRUE;

	if (!bot_nav_lane_axis(agent->team_index, &home, &forward_x, &forward_y, &length))
		return FALSE;
	component = bot_nav_start_component(agent, resource, start);
	if (component == SN_NONE)
		return FALSE;
	/* Lane widths span the reachable ground, not walls or separate floors. */
	for (index = 0; index < resource->polygon_count; index++)
	{
		struct sn_point center = resource->polygons[index].center;
		real lateral = -(center.x - home.x) * forward_y + (center.y - home.y) * forward_x;

		if (sn_component(resource, index) != component)
			continue;

		if (first || lateral < lateral_min)
			lateral_min = lateral;
		if (first || lateral > lateral_max)
			lateral_max = lateral;
		first = FALSE;
	}
	if (first)
		return FALSE;

	desired_progress = length * (agent->lane_phase ?
		BOT_NAVIGATION_FAR_PROGRESS : BOT_NAVIGATION_NEAR_PROGRESS);
	desired_lateral = lateral_min + (lateral_max - lateral_min) *
		((real)agent->lane_index + 0.5f) / BOT_NAVIGATION_LANES;
	desired_lateral += (real)agent->lane_side * BOT_NAVIGATION_LANE_SIDE_OFFSET;
	for (index = 0; index < resource->polygon_count; index++)
	{
		struct sn_point center = resource->polygons[index].center;
		real progress = (center.x - home.x) * forward_x + (center.y - home.y) * forward_y;
		real lateral = -(center.x - home.x) * forward_y + (center.y - home.y) * forward_x;
		real cost = bot_nav_abs(progress - desired_progress) +
			2.f * bot_nav_abs(lateral - desired_lateral) +
			0.25f * bot_nav_abs(center.z - home.z);

		if (progress < 0.f || progress > length * 1.2f)
			cost += length * 2.f;
		if (bot_nav_horizontal_distance(start, center) < BOT_NAVIGATION_MINIMUM_GOAL_DISTANCE)
			cost += length;
		if (cost < unfiltered_cost)
		{
			unfiltered_cost = cost;
			unfiltered = index;
		}
		if (sn_component(resource, index) == component && cost < best_cost)
		{
			best_cost = cost;
			best = index;
		}
	}
	if (best == SN_NONE)
	{
		platform_log("bots: bot %d lane target team %ld lane %d: no reachable polygon in component %lu",
			agent->bot_number, agent->team_index, agent->lane_index, (unsigned long)component);
		return FALSE;
	}
	if (unfiltered != best)
	{
		/* Record why the geometrically preferred polygon was not used. */
		platform_log("bots: bot %d lane target team %ld lane %d rejected polygon %lu (%.1f %.1f %.1f): %s",
			agent->bot_number, agent->team_index, agent->lane_index, (unsigned long)unfiltered,
			resource->polygons[unfiltered].center.x, resource->polygons[unfiltered].center.y,
			resource->polygons[unfiltered].center.z,
			sn_component(resource, unfiltered) == SN_NONE ? "center-unusable" : "disconnected");
	}
	*goal = resource->polygons[best].center;
	actual_progress = (goal->x - home.x) * forward_x + (goal->y - home.y) * forward_y;
	actual_lateral = -(goal->x - home.x) * forward_y + (goal->y - home.y) * forward_x;
	platform_log("bots: bot %d lane target team %ld lane %d phase %d home (%.1f %.1f) "
		"axis (%.2f %.2f) length %.1f cross %.1f..%.1f want %.1f/%.1f "
		"got %.1f/%.1f goal (%.1f %.1f %.1f) polygon %lu component %lu", agent->bot_number,
		agent->team_index, agent->lane_index, agent->lane_phase, home.x, home.y, forward_x, forward_y,
		length, lateral_min, lateral_max, desired_progress, desired_lateral,
		actual_progress, actual_lateral, goal->x, goal->y, goal->z, (unsigned long)best,
		(unsigned long)component);
	return TRUE;
}

static boolean bot_nav_random_goal(
	struct bot_navigation_agent *agent,
	struct sn_resource const *resource,
	struct sn_point start,
	struct engine_ai_rng32_state *rng,
	struct sn_point *goal)
{
	uint32_t attempt;
	uint32_t component;

	if (resource->polygon_count == 0)
		return FALSE;
	component = bot_nav_start_component(agent, resource, start);
	if (component == SN_NONE)
		return FALSE;
	for (attempt = 0; attempt < BOT_NAVIGATION_GOAL_ATTEMPTS; attempt++)
	{
		uint32_t candidate = 0;

		if (engine_ai_rng32_bounded(rng, resource->polygon_count, &candidate) != ENGINE_AI_RNG32_OK)
			return FALSE;
		if (sn_component(resource, candidate) == component &&
			bot_nav_horizontal_distance(start, resource->polygons[candidate].center) >=
			BOT_NAVIGATION_MINIMUM_GOAL_DISTANCE)
		{
			*goal = resource->polygons[candidate].center;
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------- private code: bounded route search and following */

static enum bot_navigation_result bot_nav_route_step(
	struct bot_navigation_agent *agent,
	long unit_index,
	struct sn_point goal,
	real speed,
	real arrival_distance,
	boolean patrol,
	struct bot_navigation_command *command)
{
	struct sn_resource const *resource;
	struct sn_point origin = bot_nav_position(unit_index);
	enum bot_navigation_result result;

	if ((agent->searching || agent->following) && agent->patrol_goal != patrol)
		bot_navigation_agent_reset_path(agent);
	agent->patrol_goal = patrol;
	if (patrol)
		agent->goal = goal;
	/* A flag/item directly below a roof is not reached from that roof.
	   Match the follower's floor tolerance before accepting XY proximity. */
	if (!patrol && bot_nav_horizontal_distance(origin, goal) <= arrival_distance &&
		bot_nav_abs(origin.z - goal.z) < 0.35f)
	{
		bot_navigation_agent_reset_path(agent);
		return BOT_NAVIGATION_ARRIVED;
	}

	resource = navigation_world_resource();
	if (!resource)
		return BOT_NAVIGATION_UNAVAILABLE;
	/* (a search still queued takes a moving target's new position without
	losing its turn; one under way, or a route, replans once it is far off) */
	if (agent->searching && bot_nav_refresh(agent, origin, goal))
		agent->goal = goal;
	else if ((agent->searching || agent->following) &&
		bot_nav_distance(agent->goal, goal) > BOT_NAVIGATION_REPLAN_DISTANCE)
	{
		bot_navigation_agent_reset_path(agent);
	}
	if (!agent->searching && !agent->following)
	{
		agent->goal = goal;
		agent->goal_valid = TRUE;
		return bot_nav_search(agent, unit_index, resource, origin);
	}
	if (agent->searching)
	{
		result = bot_nav_search(agent, unit_index, resource, origin);
		if (result != BOT_NAVIGATION_MOVING)
			return result;
	}
	return bot_nav_follow(agent, unit_index, resource, origin, speed, patrol, command);
}

/* One line per failed search. The kind separates an endpoint that does not
project (bad-start, bad-goal), endpoints in different static components
(disconnected), and a search refused by collision checks within one component
(clearance: at the start or goal segment, or rejected-edges between polygons;
static geometry and transient occupants are not distinguished). Polygon pairs
let a log reader count distinct failures apart from retries of the same goal.
The points are where the search placed its endpoints, or, for one it could
not place, where it was asked to begin or end. */
static void bot_nav_log_failure(
	struct bot_navigation_agent *agent,
	struct sn_resource const *resource,
	enum sn_status status,
	struct sn_jobs_diagnostics const *query)
{
	char const *kind = sn_status_name(status);

	if (status == SN_NO_PATH)
		kind = query->start_rejected && query->goal_rejected ? "clearance-start-goal" :
			query->start_rejected ? "clearance-start" :
			query->goal_rejected ? "clearance-goal" : "clearance-edges";
	platform_log("bots: bot %d surface %s search failed: %s kind %s start (%.1f %.1f %.1f) "
		"polygon %ld component %ld goal (%.1f %.1f %.1f) polygon %ld component %ld "
		"rejected-edges %lu slices %lu", agent->bot_number, agent->patrol_goal ? "patrol" : "tactical",
		sn_status_name(status), kind, query->start_point.x, query->start_point.y, query->start_point.z,
		query->start == SN_NONE ? -1L : (long)query->start,
		query->start == SN_NONE ? -1L : (long)sn_component(resource, query->start),
		query->goal_point.x, query->goal_point.y, query->goal_point.z,
		query->goal == SN_NONE ? -1L : (long)query->goal,
		query->goal == SN_NONE ? -1L : (long)sn_component(resource, query->goal),
		(unsigned long)query->rejected_edges, (unsigned long)query->steps);
}

/* Ask the shared scheduler for a route from start to agent->goal, or collect
its answer. A request waits its turn for a workspace (WAITING); its route
comes back string-pulled with the same collision checks as the search. */
static enum bot_navigation_result bot_nav_search(
	struct bot_navigation_agent *agent,
	long unit_index,
	struct sn_resource const *resource,
	struct sn_point start)
{
	struct sn_jobs_request request;
	struct sn_jobs_diagnostics diagnostics;
	enum sn_status status;
	uint32_t client;
	long now = game_time_get();

	if (!bot_nav_client(agent, &client))
		return BOT_NAVIGATION_FAILED;
	/* (asked after this tick, checked with the body the bot has now) */
	navigation_units[client] = unit_index;
	navigation_polled[client] = now;
	if (agent->searching && !sn_jobs_active(&navigation_jobs, client, agent->search_token))
	{
		/* (dropped by a reset of the scheduler: ask again, at the back) */
		platform_log("bots: bot %d surface %s search request lost after %ld ticks; asking again",
			agent->bot_number, agent->patrol_goal ? "patrol" : "tactical", now - agent->search_tick);
		agent->searching = FALSE;
	}
	if (!agent->searching)
	{
		/* (one request per client: any older one of this slot's is let go) */
		sn_jobs_cancel(&navigation_jobs, client, navigation_tokens[client]);
		csmemset(&request, 0, sizeof(request));
		request.start = start;
		request.goal = agent->goal;
		request.ground_depth = BOT_NAVIGATION_GROUND_DEPTH;
		request.ground_horizontal = BOT_NAVIGATION_GROUND_HORIZONTAL;
		request.obstacle_revision = BOT_NAVIGATION_ROUTE_OBSTACLES;
		/* A tactical goal is a unit's or item's position: an enemy standing there
		would refuse the last segment, though the route to it is open. */
		request.check_goal_segment = agent->patrol_goal ? true : false;
		request.straighten = true;
		navigation_tokens[client] = ++navigation_token;
		if (!sn_jobs_submit(&navigation_jobs, client, navigation_tokens[client], resource, &request))
		{
			platform_log("bots: bot %d surface %s search not queued: start (%.1f %.1f %.1f) "
				"goal (%.1f %.1f %.1f)", agent->bot_number, agent->patrol_goal ? "patrol" : "tactical",
				start.x, start.y, start.z, agent->goal.x, agent->goal.y, agent->goal.z);
			bot_navigation_agent_reset_path(agent);
			return BOT_NAVIGATION_FAILED;
		}
		agent->search_token = navigation_tokens[client];
		agent->searching = TRUE;
		agent->search_tick = now;
		return BOT_NAVIGATION_WAITING;
	}
	if (sn_jobs_status(&navigation_jobs, client, agent->search_token) == SN_RUNNING)
		return BOT_NAVIGATION_WAITING;
	status = sn_jobs_take(&navigation_jobs, client, agent->search_token, resource,
		BOT_NAVIGATION_ROUTE_OBSTACLES, &agent->route, &diagnostics);
	agent->searching = FALSE; /* (finished, not abandoned) */
	if (status != SN_FOUND)
	{
		bot_nav_log_failure(agent, resource, status, &diagnostics);
		bot_navigation_agent_reset_path(agent);
		return BOT_NAVIGATION_FAILED;
	}

	agent->following = TRUE;
	/* (waypoint 0 is where the bot stood when the search began) */
	agent->waypoint = agent->route.count > 1 ? 1 : 0;
	agent->search_tick = now - agent->search_tick;
	agent->progress_tick = now;
	agent->progress_position = start;
	agent->stuck_count = 0;
	agent->waypoint_deadline = NONE;
	agent->jump_waypoint = SN_NONE;
	agent->jump_active = FALSE;
	agent->jump_airborne = FALSE;
	platform_log("bots: bot %d team %ld lane %d squad %ld surface %s route goal "
		"(%.1f %.1f %.1f) waypoints %lu search %ld ticks, %lu slices", agent->bot_number,
		agent->team_index, agent->lane_index, agent->squad_index,
		agent->patrol_goal ? "patrol" : "tactical",
		agent->route.points[agent->route.count - 1].x,
		agent->route.points[agent->route.count - 1].y,
		agent->route.points[agent->route.count - 1].z, (unsigned long)agent->route.count,
		agent->search_tick, (unsigned long)diagnostics.steps);
	return BOT_NAVIGATION_MOVING;
}

static enum bot_navigation_result bot_nav_follow(
	struct bot_navigation_agent *agent,
	long unit_index,
	struct sn_resource const *resource,
	struct sn_point origin,
	real speed,
	boolean patrol,
	struct bot_navigation_command *command)
{
	struct sn_point target;
	uint32_t polygon;
	uint32_t index;
	real distance;
	long now = game_time_get();
	long ignored_unit = unit_index;

	if (!agent->following || !sn_route_current(&agent->route, resource,
		BOT_NAVIGATION_ROUTE_OBSTACLES))
	{
		bot_navigation_agent_reset_path(agent);
		return BOT_NAVIGATION_FAILED;
	}
	if (agent->jump_active)
	{
		boolean airborne = TEST_FLAG(biped_get(unit_index)->biped.flags, _biped_airborne_bit);
		if (airborne)
			agent->jump_airborne = TRUE;
		else if (agent->jump_airborne || now - agent->jump_tick > 3)
		{
			struct sn_point landing = agent->route.points[agent->jump_waypoint];
			platform_log("bots: bot %d short jump %s waypoint %lu horizontal %.2f vertical %.2f",
				agent->bot_number, agent->jump_airborne ? "landed" : "not-started",
				(unsigned long)agent->jump_waypoint, bot_nav_horizontal_distance(origin, landing),
				bot_nav_abs(origin.z - landing.z));
			agent->jump_active = FALSE;
		}
	}
	/* A waypoint is reached within 0.45 m, or passed: the bot is near it and
	already closer to the following waypoint than the waypoint itself is.
	Without the second test a bot that slides by at 0.5 m turns back for it. */
	while (agent->waypoint < agent->route.count)
	{
		struct sn_point next = agent->route.points[agent->waypoint];
		real away = bot_nav_horizontal_distance(origin, next);

		if (away >= 0.45f || bot_nav_abs(origin.z - next.z) >= 0.35f)
		{
			struct sn_point after;

			if (agent->waypoint + 1 >= agent->route.count || away >= 2.f)
				break;
			after = agent->route.points[agent->waypoint + 1];
			if (bot_nav_horizontal_distance(origin, after) >= bot_nav_horizontal_distance(next, after))
				break;
		}
		agent->waypoint++;
	}
	if (agent->waypoint >= agent->route.count)
	{
		struct sn_point end = agent->route.points[agent->route.count - 1];

		platform_log("bots: bot %d team %ld lane %d surface %s arrived goal (%.1f %.1f %.1f) "
			"at (%.1f %.1f %.1f) error %.2f waypoints %lu", agent->bot_number, agent->team_index,
			agent->lane_index, patrol ? "patrol" : "tactical", end.x, end.y, end.z,
			origin.x, origin.y, origin.z, bot_nav_horizontal_distance(origin, end),
			(unsigned long)agent->route.count);
		bot_navigation_agent_reset_path(agent);
		if (patrol && agent->lane_assigned)
			agent->lane_phase = (short)(1 - agent->lane_phase);
		return BOT_NAVIGATION_ARRIVED;
	}

	/* Replan only when a blockage lies on this bot's own next few corridor
	polygons. The bot is somewhere between the previous waypoint's polygon and
	this one's; the first of those is skipped, being usually the one underfoot. */
	index = agent->waypoint > 0 ? agent->route.corridor_index[agent->waypoint - 1] + 1U : 1U;
	for (; index < agent->route.corridor_count &&
		index <= (uint32_t)agent->route.corridor_index[agent->waypoint] + BOT_NAVIGATION_BLOCK_LOOKAHEAD;
		index++)
	{
		if (navigation_world_polygon_blocked(agent->route.corridor[index]))
		{
			platform_log("bots: bot %d surface route crosses blocked polygon %lu; replanning",
				agent->bot_number, (unsigned long)agent->route.corridor[index]);
			bot_navigation_agent_reset_path(agent);
			return BOT_NAVIGATION_FAILED;
		}
	}

	if (agent->progress_tick == NONE)
	{
		agent->progress_tick = now;
		agent->progress_position = origin;
	}
	else if (now - agent->progress_tick >= BOT_NAVIGATION_PROGRESS_TICKS)
	{
		if (bot_nav_distance(origin, agent->progress_position) < 0.5f)
			agent->stuck_count++;
		else
			agent->stuck_count = 0;
		agent->progress_tick = now;
		agent->progress_position = origin;
		if (agent->stuck_count >= 2)
			return bot_nav_stuck(agent, resource, origin, patrol, "no movement");
	}

	/* Displacement alone misses a bot that circles or slides beside a waypoint
	it cannot reach: each waypoint gets two seconds plus walking time. */
	target = agent->route.points[agent->waypoint];
	if (agent->waypoint_deadline == NONE || agent->deadline_waypoint != agent->waypoint)
	{
		agent->deadline_waypoint = agent->waypoint;
		agent->occupant_wait_ticks = 0;
		agent->waypoint_deadline = now + 2 * TICKS_PER_SECOND +
			(long)(bot_nav_distance(origin, target) * BOT_NAVIGATION_TICKS_PER_METRE);
	}
	else if (now > agent->waypoint_deadline)
	{
		platform_log("bots: bot %d waypoint %lu/%lu not reached: at (%.1f %.1f %.1f) "
			"waypoint (%.1f %.1f %.1f) horizontal %.2f vertical %.2f occupant-wait %ld ticks", agent->bot_number,
			(unsigned long)agent->waypoint, (unsigned long)agent->route.count,
			origin.x, origin.y, origin.z, target.x, target.y, target.z,
			bot_nav_horizontal_distance(origin, target), bot_nav_abs(origin.z - target.z),
			agent->occupant_wait_ticks);
		return bot_nav_stuck(agent, resource, origin, patrol, "waypoint deadline");
	}

	polygon = agent->route.polygons[agent->waypoint];
	/* Never emit movement toward a waypoint through a vehicle or machine. A
	 * moving one blocks briefly; a persistent block replans.
	 * A tactical route's last waypoint is the target itself, within the goal's
	 * own polygon: the target's body is expected there, and physics still stops us. */
	if ((patrol || agent->waypoint + 1 < agent->route.count) &&
		!navigation_world_occupant_clear(&ignored_unit, origin, polygon, target, polygon))
	{
		agent->occupant_wait_ticks++;
		return BOT_NAVIGATION_WAITING;
	}
	distance = bot_nav_horizontal_distance(origin, target);
	if (agent->jump_waypoint != agent->waypoint &&
		navigation_world_jump_clear(unit_index, origin, target))
	{
		agent->jump_waypoint = agent->waypoint;
		agent->jump_active = TRUE;
		agent->jump_airborne = FALSE;
		agent->jump_tick = now;
		command->jump = TRUE;
		platform_log("bots: bot %d validated short jump to waypoint %lu at (%.2f %.2f %.2f)",
			agent->bot_number, (unsigned long)agent->waypoint, target.x, target.y, target.z);
	}
	command->has_move = TRUE;
	command->yaw = arctangent(target.y - origin.y, target.x - origin.x);
	command->speed = MIN(speed, distance * 2.f);
	return BOT_NAVIGATION_MOVING;
}

/* Block the next polygon ahead, never the one underfoot: blocking that would
refuse this bot's own start segment on every replan. */
static enum bot_navigation_result bot_nav_stuck(
	struct bot_navigation_agent *agent,
	struct sn_resource const *resource,
	struct sn_point origin,
	boolean patrol,
	char const *reason)
{
	struct sn_point projected;
	uint32_t current_polygon;
	uint32_t polygon = SN_NONE;
	uint32_t index;

	if (sn_project(resource, bot_nav_ground(resource, origin), &current_polygon, &projected) != SN_FOUND)
		current_polygon = SN_NONE;
	/* (the corridor polygon after the one underfoot; off the corridor, the
	polygon the bot was walking into) */
	for (index = 0; index < agent->route.corridor_count; index++)
	{
		if (agent->route.corridor[index] == current_polygon)
			break;
	}
	if (index + 1 < agent->route.corridor_count)
		polygon = agent->route.corridor[index + 1];
	else if (index == agent->route.corridor_count && agent->waypoint < agent->route.count &&
		agent->route.polygons[agent->waypoint] != current_polygon)
		polygon = agent->route.polygons[agent->waypoint];
	if (polygon != SN_NONE)
		navigation_world_block(polygon);
	platform_log("bots: bot %d surface route blocked at polygon %ld (standing on %ld, %s); "
		"replanning", agent->bot_number, polygon == SN_NONE ? -1L : (long)polygon,
		current_polygon == SN_NONE ? -1L : (long)current_polygon, reason);
	bot_navigation_agent_reset_path(agent);
	if (patrol && agent->lane_assigned)
		agent->lane_phase = (short)(1 - agent->lane_phase);
	return BOT_NAVIGATION_FAILED;
}

#endif /* HALO_FEATURE_BOTS */
