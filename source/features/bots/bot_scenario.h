/*
BOT_SCENARIO.H
port: controlled bot behavior scenarios (bot_scenario.c): a flat test arena
with walls and cover, built host-locally above an existing map, where a bot
meets scripted targets placed by the fixture, trial after trial, with every
tick of its decisions written to a JSON-lines log (docs/bot-scenarios.md).
A debug fixture: debug.bot_scenario, off in normal play.
*/

#ifndef HALO_BOT_SCENARIO_H
#define HALO_BOT_SCENARIO_H
#pragma once

#include <stdint.h>

#include "cseries/cseries.h"
#include "math/real_math.h"

struct player_action;
struct sn_polygon;
struct halo_world_geometry;
struct halo_editor;

/* ---------- constants */

enum
{
	/* bots.c's utility options: retreat, fight, roam, scavenge, vehicle, objective */
	BOT_SCENARIO_SCORES = 6,
	/* the route points a subject's telemetry and overlay carry */
	BOT_SCENARIO_ROUTE_POINTS = 16,
	/* the most bots a trial uses: the crowd test's (bots.c checks it
	against its own table) */
	BOT_SCENARIO_BOTS_MAX = 24,
};

/* how bots.c runs a bot this tick (bot_scenario_control) */
enum bot_scenario_control
{
	_bot_scenario_control_ai = 0, /* its own decisions: a subject in a running trial */
	_bot_scenario_control_action, /* the fixture's action: held still, or a scripted target */
	_bot_scenario_control_goal, /* its own route to the fixture's goal, without fighting */
};

/* how a subject knows its target (bots.c's _bot_target_*) */
enum
{
	_bot_scenario_target_none = 0,
	_bot_scenario_target_seen,
	_bot_scenario_target_attacker,
	_bot_scenario_target_remembered,
	_bot_scenario_target_shared,
};

/* ---------- structures */

/* a subject bot's state once it has decided this tick: what the scenario logs */
struct bot_scenario_observation
{
	short slot;
	char const *leaf_name;
	float scores[BOT_SCENARIO_SCORES];
	real confidence;
	long target_player_index;
	short target_source;
	boolean target_visible;
	boolean aware; /* past its spawn orientation */
	real_point3d target_belief; /* where it thinks its target is */
	real_euler_angles2d facing;
	boolean has_look;
	real_euler_angles2d look;
	boolean has_move;
	real move_yaw;
	real move_speed;
	real trigger;
	unsigned long control_flags;
	real strafe_sign;
	boolean route_searching;
	boolean route_following;
	short route_waypoint;
	short route_count; /* points copied, at most BOT_SCENARIO_ROUTE_POINTS */
	real_point3d route[BOT_SCENARIO_ROUTE_POINTS];
	short navigation_result; /* goal control: bot_navigation_result; NONE otherwise */
};

/* ---------- prototypes/BOT_SCENARIO.C (called by bots.c and navigation_world.c) */

/* the map's objects placed: read the settings, nothing built yet */
void bot_scenario_initialize_for_new_map(void);
/* the map left: the arena goes with it */
void bot_scenario_dispose_from_old_map(void);
/* whether debug.bot_scenario runs on this map (64-bit desktop builds only) */
boolean bot_scenario_enabled(void);
/* how many bots the selected scenarios need, and the team of each slot */
short bot_scenario_bot_count(void);
char bot_scenario_team(short slot);
/* the host's own players and the bots no trial uses: no bot sees them */
boolean bot_scenario_ignores_player(long player_index);
/* each tick, before the bots decide: the arena, trials, targets and damage */
void bot_scenario_update(void);
/* how the bot in the slot plays this tick: the action, or the goal */
short bot_scenario_control(short slot, struct player_action *action, real_point3d *goal);
/* a subject's decisions this tick, for the log, overlay and trial results */
void bot_scenario_observe(struct bot_scenario_observation const *observation);
/* after every bot has decided: their CPU time this tick */
void bot_scenario_end_tick(double milliseconds);

/* navigation_world.c: the arena's floor as walkable polygons after the BSP's
(their source indices from first_source_index); 0 when there is no arena */
uint32_t bot_scenario_navigation_surface_count(void);
int bot_scenario_navigation_surface(uint32_t index, uint32_t first_source_index, struct sn_polygon *out);

/* the extension parts bots.c registers where host-local world geometry is
built (HALO_EXTENSION_WORLD_GEOMETRY: 64-bit desktop): the arena's walls,
and the camera and overlay that watch a trial */
extern struct halo_world_geometry const bot_scenario_geometry;
extern struct halo_editor const bot_scenario_editor;

/* ---------- prototypes/BOTS.C (called by bot_scenario.c) */

/* the player of the active bot in the slot; NONE if there is none */
long bots_scenario_player_index(short slot);
/* a fresh life's state for the bot in the slot (a trial's start): facing
yaw, alert at once (no spawn orientation), its random numbers from seed */
boolean bots_scenario_prepare(short slot, real yaw, uint32_t seed);
/* the teams' shared sightings and claims forgotten: a trial starts afresh */
void bots_scenario_forget(void);
/* every bot's skill, by name (bots.skill's names); FALSE for an unknown one */
boolean bots_scenario_set_skill(char const *name);

#endif /* HALO_BOT_SCENARIO_H */
