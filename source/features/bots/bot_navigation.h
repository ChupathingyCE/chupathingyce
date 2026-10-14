/*
BOT_NAVIGATION.H

port: per-bot BSP navigation and lane assignment (bot_navigation.c). Each
agent owns one bounded route; its searches queue for four workspaces shared
by all bots. The interface hides BSP projections, route searches, collision
checks, blockage recovery and team lane targets.
*/

#ifndef HALO_BOT_NAVIGATION_H
#define HALO_BOT_NAVIGATION_H
#pragma once

#include "cseries/cseries.h"
#include "math/real_math.h"
#include "engine_ai/behavior_intent.h"
#include "engine_ai/surface_navigation_jobs.h"

/* ---------- constants */

enum
{
	/* bot numbers 1..BOT_NAVIGATION_AGENTS_MAX, one search client each */
	BOT_NAVIGATION_AGENTS_MAX = SN_JOBS_CLIENTS_MAX,
	BOT_NAVIGATION_LANES = 3,
	BOT_NAVIGATION_SQUAD_SIZE = 2,
};

/* ---------- structures */

struct bot_navigation_agent
{
	/* Two bots form a squad in one of three lanes. Pairs begin at different
	depths, and alternate between those points as they patrol. */
	short bot_number;
	long team_index;
	long team_rank;
	long squad_index;
	short lane_index;
	short lane_phase;
	short lane_side;
	boolean lane_assigned;

	uint64_t search_token; /* the scheduler's request, while searching */
	struct sn_route route;
	struct sn_point goal;
	struct sn_point progress_position;
	uint32_t waypoint;
	long progress_tick;
	long search_tick; /* when the current search began, for its log line */
	long waypoint_deadline; /* the current waypoint must be reached by then */
	uint32_t deadline_waypoint;
	long occupant_wait_ticks; /* at this waypoint, waiting for the live check */
	uint32_t jump_waypoint; /* last waypoint for which a jump was requested */
	long jump_tick;
	boolean jump_active;
	boolean jump_airborne;
	short stuck_count;
	boolean searching;
	boolean following;
	boolean goal_valid;
	boolean patrol_goal;
};

struct bot_navigation_command
{
	boolean has_move;
	boolean jump;
	real yaw;
	real speed;
};

enum bot_navigation_result
{
	BOT_NAVIGATION_UNAVAILABLE = 0, /* BSP resource is still building or absent */
	BOT_NAVIGATION_WAITING,
	BOT_NAVIGATION_MOVING,
	BOT_NAVIGATION_ARRIVED,
	BOT_NAVIGATION_FAILED,
};

/* Where team 0's or 1's base is, and the enemy's: the map's CTF flags, else
 * the average of each team's starting locations. FALSE for another team, or a
 * map with neither. */
boolean bot_navigation_team_bases(
	long team_index,
	real_point3d *home,
	real_point3d *enemy);

/* Drop every queued and running search: before a map, BSP or game-state
 * change, and with a new map's bots. Agents still searching ask again. */
void bot_navigation_reset_all(void);
/* Once per tick, before bots decide: lets go of searches no agent asked after
 * last tick or whose body is gone, then advances the shared searches: at most
 * four begin, 64 polygons are expanded and one found route is string-pulled
 * (at most SN_STRAIGHTEN_CHECKS_MAX capsule sweeps), whatever the bot count. */
void bot_navigation_update(void);

/* team_rank counts lower-slot bots on this team. NONE means no team lanes.
 * A search left by this slot's previous bot is cancelled. */
void bot_navigation_agent_initialize(
	struct bot_navigation_agent *agent,
	short bot_number,
	long team_index,
	long team_rank);

/* Cancel one route without changing the agent's assigned lane or patrol phase. */
void bot_navigation_agent_reset_path(
	struct bot_navigation_agent *agent);

/* Route to a tactical goal using the map's BSP walkable surfaces and live biped
 * capsule checks. ARRIVED means within arrival_distance horizontally and
 * 0.35 m of the goal's floor height; WAITING while the
 * search is queued or under way. A queued search follows a moving target; one
 * under way, or a route, replans when the target is 12 metres from its goal. */
enum bot_navigation_result bot_navigation_agent_move_to(
	struct bot_navigation_agent *agent,
	long unit_index,
	struct sn_point goal,
	real speed,
	real arrival_distance,
	struct bot_navigation_command *command);

/* Patrol the assigned team lane, or choose a separated random BSP goal when
 * lanes are not available. ARRIVED advances the lane patrol phase. */
enum bot_navigation_result bot_navigation_agent_roam(
	struct bot_navigation_agent *agent,
	long unit_index,
	struct engine_ai_rng32_state *rng,
	struct bot_navigation_command *command);

#endif /* HALO_BOT_NAVIGATION_H */
