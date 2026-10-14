/*
BOTS.C

port: computer-controlled multiplayer players ("bots").

A bot is a real player: a player datum, with a team, a spawn, kills, deaths
and a score, whose unit the game engine spawns and respawns as it does
everyone's. It plays through the same pipeline as a person: each tick it
decides a player_action, which the host puts in its next update for the
player (update_server_set_player_action), exactly where a local player's
input goes, and players_update_before_game turns into unit control. Bots have
no extra health, damage or knowledge: skill changes reaction time, aim error,
turn speed, sight and tactics only, and a bot fights only enemies it has seen
or that have just hurt it.

Decisions follow Halo Infinite's (GDC 2022, "Thinking Like Players";
docs/bot_decisions.md), on the game-independent AI primitives (source/engine_ai):
  - utility.h: every behavior is scored, 0 to 1, from the bot's confidence,
    its target and how it knows it, and its opportunities; the highest runs.
    A decision is made every BOT_DECISION_TICKS (about a human's reaction
    time), staggered by slot, or at once when the running behavior can no
    longer run; hysteresis keeps a bot from thrashing between near scores;
  - always be shooting: whatever the behavior, a bot aims and fires at an
    enemy it sees; the behavior moves it;
  - bot_manager.h: the blackboard: the game mode's ambitions, and each team's
    sightings, shared by skills that share what they see;
  - behavior.h: a flat tree of the behaviors (VEHICLE, RETREAT, SCAVENGE,
    FIGHT, ROAM), whose leaves fail unless they are the chosen one;
  - behavior_intent.h / intent.h: the leaves emit intents (look, move, fire,
    jump, grenade, interact, exit vehicle), which a Halo-owned executor validates against the
    live player and unit before they become the player's action;
  - bot_navigation.h / navigation_world.h: per-bot routes over validated BSP
    walkable surfaces, checked with the live biped capsule; three map-relative
    lanes keep squads spread. The spawn graph is a delayed fallback.

Scope (a first, small slice):
  - only the host decides for its bots, and only in a game nobody else is in:
    a custom game you host by yourself (or a local one). Bots never join a
    game another machine is in, and leave if one joins. They are not
    replicated to other machines yet;
  - a game a bot has played in is never reported to the game list or Delta
    Stats (bots_game_had_bots);
  - free for all and team Slayer; visible weapon upgrades, BSP lane patrol,
    ground vehicles and fixed turrets; Spartan bots share observed enemy callouts;
  - basic ground driving only, including stopping near a teammate for an open
    seat; no aircraft, objectives, horn signal or advanced vehicle paths.
    debug.bot_sandbox is an opt-in test teleport, never normal spawn policy.

Configuration (port/linux/src/port_config.c): bots.count (0..31), bots.skill.
*/

/* ---------- headers */

#ifdef HALO_TRACE_ENABLED
#include "../../../port/linux/src/halo_trace.h"
#endif

#include "cseries.h"
#include "game.h"
#include "game_engine.h"
#include "players.h"
#include "players_runtime.h"
#include "player_queues_new.h"
#include "bots.h"
#include "navigation_probe.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "physics/collision_features.h" /* port: bounded capsule probe planes */
#include "real_math.h"
#include "scenario/scenario_definitions.h"
#include "units/units.h"
#include "units/bipeds.h" /* port: live capsule for final movement probes */
#include "units/unit_definitions.h"
#include "units/vehicles.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "engine_ai/fire_control.h"
#include "engine_ai/behavior.h"
#include "engine_ai/behavior_intent.h"
#include "engine_ai/intent.h"
#include "engine_ai/navigation.h"
#include "engine_ai/traversal.h"
#include "engine_ai/utility.h"
#include "engine_ai/aim.h"
#include "items/equipment.h"
#include "items/equipment_definitions.h"
#include "extensions/extension_api.h"
#include "bot_manager.h"
#include "halo_bot_objectives.h" /* port: standard CTF admission and state */
#include "human_callouts.h" /* port: nearby human incoming-fire cues */
#include "bot_navigation.h"
#include "vehicle_navigation.h" /* port: routes for driven vehicles */
#include "navigation_world.h"
#include "map_anchors.h"
#include "bot_scenario.h" /* port: debug.bot_scenario's controlled trials */

#include <time.h>

#ifdef HALO_FEATURE_BOTS

/* network_game_globals.c's */
boolean network_game_distributed_client(void);
/* port/linux/game/network_distributed.c's */
short distributed_client_machines(long *machine_indices, short maximum);
/* port/linux/src/port_config.c's */
long config_integer(char const *name);
int config_boolean(char const *name);
char const *config_string(char const *name);
/* port/linux/src/platform.h's */
void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	/* (a 32-player lobby: the host and 31 bots) */
	BOTS_MAXIMUM = 31,
	/* the machines bots belong to: no real machine's, so that nothing that
	works through the machines (their connections, their players' input) takes
	them for anyone's. A machine's player list has room for
	MAXIMUM_LOCAL_PLAYERS, so bots fill machines down from the last: bot n
	is on BOT_MACHINE_INDEX - n / MAXIMUM_LOCAL_PLAYERS. Bots never join a
	game another machine is in, so these never meet a real machine. */
	BOT_MACHINE_INDEX = HALO_PORT_MAXIMUM_NETWORK_MACHINES - 1,
	BOT_MACHINE_COUNT = (BOTS_MAXIMUM + MAXIMUM_LOCAL_PLAYERS - 1) / MAXIMUM_LOCAL_PLAYERS,
	BOT_PROGRESS_CHECK_TICKS = TICKS_PER_SECOND,
	BOT_NAV_NEIGHBORS = 6,
	BOT_NAV_EXPANSIONS_PER_TICK = 64,
	BOT_SURFACE_WAIT_TICKS = 10 * TICKS_PER_SECOND,
	BOT_BEHAVIOR_WORK_PER_TICK = 32,
	BOT_INTENT_SCHEMA = 1,
	/* a decision about every 267 ms: Halo Infinite's bots took a human's
	200-250 ms to react to new information, which also made them better
	practice partners */
	BOT_DECISION_TICKS = 8,
	/* the running behavior's bonus fades over a second */
	BOT_HYSTERESIS_TICKS = TICKS_PER_SECOND,
	BOT_SIGHTING_MEMORY_TICKS = 3 * TICKS_PER_SECOND,
	BOT_WEAPON_SEEK_RANGE = 30,
	/* items noticed by sight are remembered, not tracked: what a player
	would recall having seen on the map */
	BOT_KNOWN_ITEMS = 8,
	BOT_KNOWN_ITEM_MEMORY_TICKS = 30 * TICKS_PER_SECOND,
	BOT_ITEM_SIGHT_CHECKS = 12,
	BOT_ITEM_CLAIM_TICKS = 4 * TICKS_PER_SECOND,
	BOT_VEHICLE_SEEK_RANGE = 35,
	/* port: a seat's claim is renewed each tick while the bot heads for it */
	BOT_SEAT_CLAIM_TICKS = 2 * TICKS_PER_SECOND,
	/* a new driver waits this long at most for teammates boarding */
	BOT_VEHICLE_BOARD_WAIT_TICKS = 8 * TICKS_PER_SECOND,
	/* a driver with nobody to chase heads for open ground between the
	bases, and chooses another place after this long */
	BOT_VEHICLE_FIELD_GOAL_TICKS = 25 * TICKS_PER_SECOND,
	/* the nearest walkable places to a chosen point of the field, of which
	the first under open sky is driven to */
	BOT_VEHICLE_FIELD_CANDIDATES = 8,
	/* port: a driver looks for teammates to pick up once a second, and
	offers a ride for this long (a bot that does not get in is not asked
	again for BOT_RIDE_DECLINED_TICKS) */
	BOT_RIDE_SCAN_TICKS = TICKS_PER_SECOND,
	BOT_RIDE_OFFER_TICKS = 15 * TICKS_PER_SECOND,
	BOT_RIDE_DECLINED_TICKS = 30 * TICKS_PER_SECOND,
	/* a driver arrived beside its pickup waits this long for them */
	BOT_RIDE_BOARD_WAIT_TICKS = 6 * TICKS_PER_SECOND,
	/* drop-off places tried for one objective before the driver walks */
	BOT_VEHICLE_DROP_OFF_TRIES = 4,
	/* a target a vehicle could not reach is not chased again until then */
	BOT_VEHICLE_CHASE_BACKOFF_TICKS = 5 * TICKS_PER_SECOND,
};

/* the bots' machines stay clear of machine 0 (the host's) and of each other's
player lists */
typedef char bot_machines_clear_of_host_check[
	BOT_MACHINE_INDEX - (BOTS_MAXIMUM - 1) / MAXIMUM_LOCAL_PLAYERS > 0 ? 1 : -1];
typedef char bot_player_slots_check[
	BOT_MACHINE_COUNT * MAXIMUM_LOCAL_PLAYERS >= BOTS_MAXIMUM ? 1 : -1];
/* every bot number (slot + 1) has its own route search client */
typedef char bot_navigation_clients_check[
	BOTS_MAXIMUM <= BOT_NAVIGATION_AGENTS_MAX ? 1 : -1];
/* a scenario's bots are bots of this table */
typedef char bot_scenario_slots_check[
	BOT_SCENARIO_BOTS_MAX <= BOTS_MAXIMUM ? 1 : -1];

/* the behavior tree's leaves; a leaf's utility option is its id less one */
enum
{
	_bot_leaf_none = 0,
	_bot_leaf_retreat,
	_bot_leaf_fight,
	_bot_leaf_roam,
	_bot_leaf_scavenge,
	_bot_leaf_vehicle,
	_bot_leaf_objective, /* port: take, return, deliver or guard a CTF goal */
	BOT_BEHAVIOR_COUNT = _bot_leaf_objective
};

/* how a bot knows its target */
enum
{
	_bot_target_none = 0,
	_bot_target_seen,
	_bot_target_attacker, /* it has just hurt the bot */
	_bot_target_remembered, /* seen a moment ago */
	_bot_target_shared, /* a teammate's sighting (bot_manager.c) */
};

/* the intents the leaves emit, and the executor takes */
enum
{
	_bot_intent_look = 1,
	_bot_intent_move,
	_bot_intent_fire,
	_bot_intent_jump,
	_bot_intent_grenade,
	_bot_intent_melee,
	_bot_intent_interact,
	_bot_intent_exit_vehicle,
	_bot_intent_crouch,
};

/* why the executor turned an intent down */
enum
{
	_bot_reject_none = 0,
	_bot_reject_no_player,
	_bot_reject_no_unit,
	_bot_reject_bad_payload,
	_bot_reject_no_grenades,
	_bot_reject_unknown,
};

#define BOT_NAV_LINK_RANGE 30.f
#define BOT_NAV_NODE_MERGE_DISTANCE 1.f
#define BOT_NAV_EYE_HEIGHT 0.4f
#define BOT_WAYPOINT_REACHED_DISTANCE 1.2f
#define BOT_MINIMUM_ROAM_DISTANCE 12.f
#define BOT_TARGET_MEMORY_SECONDS 4.f
#define BOT_ATTACKER_AWARENESS_TICKS 15
#define BOT_GRENADE_COOLDOWN_SECONDS 8.f
#define BOT_VEHICLE_SEAT_MAXIMUM_SPEED_SQUARED 0.25f
/* a human teammate this near a bot's vehicle may be getting in */
#define BOT_VEHICLE_HUMAN_BOARD_DISTANCE 6.f
/* the height checked above a driving goal for a roof */
#define BOT_VEHICLE_OPEN_SKY_HEIGHT 20.f
/* port: the chauffeur's estimate (bot_ride_saving): world units a second
on foot and driving, the longer way a vehicle goes round, the time to get in */
#define BOT_RIDE_WALK_SPEED 2.f
#define BOT_RIDE_DRIVE_SPEED 6.f
#define BOT_RIDE_DRIVE_DETOUR 1.3f
#define BOT_RIDE_BOARD_SECONDS 3.f
/* a pickup is worth it when the rider saves this long, the rider is this
near, and the driver's own way is this much longer at most */
#define BOT_RIDE_MINIMUM_SAVING 5.f
#define BOT_RIDE_PICKUP_RANGE 30.f
#define BOT_RIDE_MAXIMUM_DETOUR 25.f
/* a driver stops this near its pickup; a rider gets in within this */
#define BOT_RIDE_STOP_DISTANCE 4.f
#define BOT_RIDE_BOARD_DISTANCE 5.f
/* beyond its nose, the room a vehicle leaves before a teammate on foot */
#define BOT_VEHICLE_STOPPING_DISTANCE 1.5f
/* a drop-off is chosen this far short of an objective, toward the vehicle,
and never nearer it than the clearance */
#define BOT_VEHICLE_DROP_OFF_BACK 8.f
#define BOT_VEHICLE_DROP_OFF_CLEARANCE 5.f
/* riders and drivers get out this near their objective */
#define BOT_VEHICLE_DISMOUNT_DISTANCE 6.f
/* CTF: an objective this far away is worth a vehicle */
#define BOT_CTF_DRIVE_DISTANCE 35.f
/* how far either way of its spawn facing a new bot looks about */
#define BOT_ORIENT_SWEEP_DEGREES 50.f
/* port: a starting location in the enemy's half of the map, rated as a
last resort: only when every one in the player's own half is refused */
#define BOT_ENEMY_HALF_SPAWN_RATING 0.01f
/* a target seen a moment ago that reappears this far away has teleported */
#define BOT_TELEPORT_DISTANCE 6.f
#define BOT_ITEM_NEAR_DISTANCE 4.f
/* port: friendly-fire safety (bot_attack_safe), in world units: conservative
geometry, not projectile flight. A shot's lane widens with range, as a
weapon's error spreads its shots; a grenade's blast is given a generous reach. */
#define BOT_FIRE_LANE_MARGIN 0.25f /* beyond a teammate's collision pill */
#define BOT_FIRE_LANE_SPREAD 0.035f /* more per unit of range, at least: about 2 degrees */
/* the widest shot cone a weapon's tags may claim (radians) */
#define BOT_FIRE_LANE_MAXIMUM_ERROR 0.5f
#define BOT_GRENADE_SAFETY_RADIUS 6.f
/* a thrown grenade's flight and fuse, for where teammates will be when it goes off */
#define BOT_GRENADE_FUSE_TICKS (2 * TICKS_PER_SECOND)
/* faster than any unit runs or drives, per tick: a velocity beyond it is not trusted */
#define BOT_MAXIMUM_SPEED_PER_TICK 2.f

/* ---------- structures */

struct bot_skill
{
	char const *name;
	real reaction_seconds;
	real aim_error_degrees;
	real turn_degrees_per_second;
	real sight_range;
	real field_of_view_degrees; /* half angle */
	real fire_tolerance_degrees;
	real burst_seconds; /* 0: no pauses */
	real burst_pause_seconds;
	boolean strafes;
	boolean throws_grenades;
	boolean retreats;
	boolean shares_sightings;
	/* port: imperfect, difficulty-scaled aim (engine_ai/aim.h). Sight_range
	spots a target; fire_range is how far the skill will shoot at it. */
	real fire_range;
	real accurate_range;
	real aim_settle_seconds;
	real moving_aim_penalty;
	real moving_turn_scale; /* turning while moving: below 1 struggles */
	boolean crouches;
	/* port: after spawning, at least this long (up to 1.6 times it) getting
	its bearings before it notices enemies */
	real orient_seconds;
};

/* The utility tuning (docs/bot_decisions.md, "Scores"): fixed values,
response curves, caps and the confidence's weights. Halo Infinite kept these
in Lua so that a game mode could change them; a mode with different needs
gets its own table. */
struct bot_utility_tuning
{
	real vehicle;
	real roam;
	/* fight: by how the target is known, times distance, times confidence */
	real fight_seen;
	real fight_attacker;
	real fight_remembered;
	real fight_shared_cap;
	engine_ai_utility_curve fight_distance;
	engine_ai_utility_curve fight_confidence;
	/* retreat: (1 - confidence) times the shields' response, capped */
	engine_ai_utility_curve retreat_shields;
	real retreat_cap;
	/* scavenge: the find's value times distance; in reach during a fight, a
	fixed value above fighting's */
	real scavenge_weapon;
	real scavenge_powerup;
	real scavenge_vehicle;
	real scavenge_in_reach;
	engine_ai_utility_curve scavenge_distance;
	engine_ai_utility_curve scavenge_vehicle_distance;
	/* confidence: own vitality, the target's lack of it, weapon against
	weapon, teammates near against enemies seen */
	float confidence_weights[4];
	real teammate_radius;
};

struct bot_look_payload
{
	real yaw;
	real pitch;
};

struct bot_move_payload
{
	real yaw; /* world direction */
	real speed; /* 0..1 */
};

struct bot_fire_payload
{
	real trigger;
};

/* what this tick's accepted intents add up to */
struct bot_pending_action
{
	boolean has_look;
	real_euler_angles2d look;
	boolean has_move;
	real move_yaw;
	real move_speed;
	real trigger;
	unsigned long control_flags;
};

struct bot
{
	boolean active;
	short slot;
	long player_index;
	long unit_index; /* the unit this life's state is for */

	struct engine_ai_rng32_state rng;
	engine_ai_behavior_state behavior;
	struct engine_ai_behavior_intent_state bridge;
	struct engine_ai_intent_batch batch;
	struct engine_ai_intent_report report;

	/* perception */
	/* port: a new life's bearings: until aware_time the bot looks about
	(bot_orient) and notices no enemy it has not been hurt by */
	long aware_time;
	long orient_start_time;
	real orient_yaw;
	real orient_direction;
	long target_player_index;
	boolean target_visible;
	long target_first_seen_time;
	long target_last_seen_time;
	real_point3d target_last_position;
	short target_source;
	short visible_enemy_count;
	/* where the target actually was the last time it was seen, for noticing a
	teleport (the destination is map knowledge, as for a player) */
	real_point3d target_observed_position;
	long target_observed_time;
	long target_observed_player_index;

	/* decisions */
	engine_ai_utility_selector selector;
	uint32_t chosen_leaf;
	boolean decide_now;
	boolean abs_suppressed; /* a leaf facing what it must (a seat, a pickup) */
	real confidence;
	float scores[BOT_BEHAVIOR_COUNT];
	/* port: copied CTF goal identity, never an ambition table offset. */
	boolean objective_valid;
	boolean objective_waiting;
	struct bot_ambition objective;
	long objective_deadline;
	long objective_retry_time;
	long objective_unreachable_index;
	short objective_retry_kind;

	/* combat */
	real_euler_angles2d facing;
	real_euler_angles2d aim_error;
	long aim_error_time;
	real strafe_sign;
	long strafe_change_time;
	long burst_change_time;
	boolean burst_firing;
	long grenade_time;
	long melee_time;
	long fire_weapon_index;
	struct engine_ai_fire_control fire_control;
	boolean charging;
	long charge_target_index;
	long next_charge_time;
	/* port: the friendly-fire gates' work since the last status line */
	long fire_held_ticks;
	long throws_refused;
	long retreat_start_time;
	long retreat_cooldown_time;
	boolean moved_last_tick;
	long crouch_until;
	long crouch_next_time;
	/* port: the trigger and controls of its last action, for scenarios */
	real last_trigger;
	unsigned long last_control_flags;

	/* navigation */
	boolean searching;
	boolean following;
	uint32_t goal_node;
	struct engine_ai_nav_search search;
	struct engine_ai_traversal_state traversal;
	real_point3d progress_position;
	long progress_time;
	short stuck_count;
	boolean jump_requested;
	struct bot_navigation_agent navigation_agent;
	long surface_nav_wait_time;
	boolean surface_fallback_logged;
	/* port: movement recovery is independent of behavior/path cancellation. */
	long movement_check_time;
	long movement_escape_until;
	real_point3d movement_position;
	real movement_escape_yaw;

	/* Bounded opportunity search; handles are revalidated before use. */
	long opportunity_index;
	long opportunity_team_index;
	boolean opportunity_vehicle;
	boolean opportunity_powerup;
	long unreachable_item_index;
	long unreachable_item_until;
	struct
	{
		long object_index; /* NONE: empty */
		long seen_time;
		real_point3d position; /* last seen, never a live tracking point */
	} known_items[BOT_KNOWN_ITEMS];
	real_point3d opportunity_position; /* last known item position */
	short opportunity_seat;
	long opportunity_scan_time;
	long opportunity_end_time;
	long interaction_time;
	long observed_vehicle_index;
	long observed_weapons[MAXIMUM_WEAPONS_PER_UNIT];
	long vehicle_progress_time;
	real_point3d vehicle_progress_position;
	/* port: a driver waits for boarding teammates until then (NONE: not set
	for this seat), and drives to a place in the field */
	long vehicle_wait_until;
	boolean vehicle_wait_logged;
	boolean vehicle_goal_valid;
	long vehicle_goal_time;
	real_point3d vehicle_goal;
	/* port: the chauffeur. A driver's pickup (a teammate's player, NONE), until
	when it is offered, and who declined last; a rider's offered vehicle. */
	long pickup_player_index;
	long pickup_until;
	boolean pickup_meet; /* no way to them by vehicle: they walk to it */
	/* port: where the driver stops for its objective, and the places tried */
	boolean vehicle_drop_off_valid;
	real_point3d vehicle_drop_off;
	real_point3d vehicle_drop_off_objective;
	uint32_t vehicle_drop_off_polygon;
	uint32_t vehicle_drop_off_failed[BOT_VEHICLE_DROP_OFF_TRIES];
	short vehicle_drop_off_failures;
	/* port: cached drivable target approach and failed-chase backoff */
	boolean vehicle_chase_valid;
	real_point3d vehicle_chase;
	real_point3d vehicle_chase_target;
	long vehicle_chase_blocked_until;
	long pickup_scan_time;
	long pickup_declined_player_index;
	long pickup_declined_until;
	long ride_vehicle_index;
	long ride_until;

	struct bot_pending_action pending;
};

struct bot_navigation
{
	boolean built;
	uint32_t node_count;
	real_point3d nodes[ENGINE_AI_NAV_NODES_MAX];
	struct engine_ai_nav_link links[ENGINE_AI_NAV_LINKS_MAX];
	struct engine_ai_nav_graph graph;
};

/* ---------- globals */

static struct bot_skill const bot_skills[] =
{
	/* name, reaction, aim error, turn speed, sight, fov, tolerance, burst, pause, strafe, grenades, retreat, share,
	fire range, accurate range, settle, moving aim penalty, moving turn scale, crouch, orient */
	{ "recruit", 0.9f, 7.f, 200.f, 30.f, 55.f, 10.f, 0.6f, 0.8f, FALSE, FALSE, FALSE, FALSE,
		16.f, 8.f, 1.2f, 3.f, 0.45f, FALSE, 1.8f },
	{ "marine", 0.5f, 4.f, 320.f, 40.f, 65.f, 8.f, 1.0f, 0.5f, TRUE, TRUE, FALSE, FALSE,
		24.f, 12.f, 0.8f, 1.8f, 0.7f, FALSE, 1.4f },
	{ "odst", 0.3f, 2.5f, 450.f, 50.f, 75.f, 6.f, 1.5f, 0.3f, TRUE, TRUE, TRUE, FALSE,
		32.f, 16.f, 0.55f, 1.35f, 0.85f, FALSE, 1.1f },
	{ "spartan", 0.18f, 1.5f, 720.f, 60.f, 85.f, 5.f, 1.2f, 0.45f, TRUE, TRUE, TRUE, TRUE,
		40.f, 22.f, 0.35f, 1.15f, 1.f, TRUE, 0.8f },
};

/* Slayer's tuning. Patrol remains a meaningful fallback; a teammate's
sighting is capped so a whole team does not abandon its lanes to one contact. */
static struct bot_utility_tuning const bot_slayer_tuning =
{
	0.95f, /* vehicle */
	0.15f, /* lane patrol */
	0.9f, 0.85f, 0.55f, 0.28f, /* fight: seen, attacker, remembered, shared cap */
	{ ENGINE_AI_UTILITY_LINEAR, 50.f, 0.f, 0.6f, 1.f, 0 }, /* fight distance: 50 m to 0 */
	{ ENGINE_AI_UTILITY_LINEAR, 0.f, 1.f, 0.5f, 1.f, 0 }, /* fight confidence */
	/* retreat: nothing above half shields, rising fast as they go */
	{ ENGINE_AI_UTILITY_POWER, 0.5f, 0.f, 0.f, 1.f, 2 },
	0.85f, /* retreat cap */
	0.6f, 0.8f, 0.75f, 0.95f, /* scavenge: weapon, powerup, vehicle, in reach */
	{ ENGINE_AI_UTILITY_LINEAR, 30.f, 0.f, 0.45f, 1.f, 0 }, /* weapon/powerup distance */
	{ ENGINE_AI_UTILITY_LINEAR, 35.f, 0.f, 0.2f, 1.f, 0 }, /* vehicle/turret distance */
	{ 3.f, 2.f, 1.f, 1.f }, /* confidence weights */
	15.f, /* teammate radius */
};

static engine_ai_utility_selector_config const bot_selector_config =
{
	BOT_BEHAVIOR_COUNT,
	BOT_DECISION_TICKS,
	BOT_HYSTERESIS_TICKS,
	0.15f, /* hysteresis bonus */
	0.05f, /* switching margin */
};

static char const *const bot_leaf_names[] = { "none", "retreat", "fight", "roam", "scavenge", "vehicle", "objective" };
typedef char bot_leaf_names_check[NUMBEROF(bot_leaf_names) == BOT_BEHAVIOR_COUNT + 1 ? 1 : -1];

/* The utility selector chooses; the tree only runs the chosen leaf (the
others fail at once) and gives it an intent emitter. The order is the old
priority order, which no longer matters. */
static engine_ai_behavior_node const bot_behavior_nodes[] =
{
	{ ENGINE_AI_BEHAVIOR_PRIORITY, 1, 6, 0 },
	{ ENGINE_AI_BEHAVIOR_LEAF, 0, 0, _bot_leaf_vehicle },
	{ ENGINE_AI_BEHAVIOR_LEAF, 0, 0, _bot_leaf_retreat },
	{ ENGINE_AI_BEHAVIOR_LEAF, 0, 0, _bot_leaf_scavenge },
	{ ENGINE_AI_BEHAVIOR_LEAF, 0, 0, _bot_leaf_fight },
	{ ENGINE_AI_BEHAVIOR_LEAF, 0, 0, _bot_leaf_roam },
	{ ENGINE_AI_BEHAVIOR_LEAF, 0, 0, _bot_leaf_objective },
};

static engine_ai_behavior_definition const bot_behavior =
{
	bot_behavior_nodes,
	NUMBEROF(bot_behavior_nodes),
	0
};

static struct
{
	boolean behavior_valid;
	boolean had_bots;
	boolean team_spawns; /* port: bots.team_spawns, for this map */
	boolean sandbox_done;
	boolean log_decisions;
	boolean navigation_only;
	struct bot_utility_tuning const *tuning;
	short desired_count;
	struct bot_skill const *skill;
	uint64_t epoch;
	struct bot bots[BOTS_MAXIMUM];
	struct bot_navigation navigation;
} bots_globals;

/* ---------- private prototypes */

static boolean bots_host_may_have_bots(void);
static void bots_refresh(void);
static void bots_join(void);
static boolean bot_join(struct bot *bot, short bot_index);
static boolean bot_opportunity_relevant(struct bot *bot);
static void bot_leave(struct bot *bot);
static void bot_think(struct bot *bot);
static void bot_submit_action(struct bot *bot);
static void bot_drop_opportunity(struct bot *bot, char const *reason);

/* ---------- public code */

/* port: one local match configured by the bot lobby, not config.toml. */
static struct
{
	boolean pending;
	short friendly_count;
	short enemy_count;
	short skill;
} lobby_match;
static short lobby_friendly_count = NONE;

boolean bots_prepare_local_match(short friendly_count, short enemy_count, short skill)
{
	if (friendly_count < 0 || enemy_count < 0 ||
		friendly_count > BOTS_MAXIMUM || enemy_count > BOTS_MAXIMUM - friendly_count ||
		skill < 0 || skill >= NUMBEROF(bot_skills))
		return FALSE;
	lobby_match.pending = TRUE;
	lobby_match.friendly_count = friendly_count;
	lobby_match.enemy_count = enemy_count;
	lobby_match.skill = skill;
	return TRUE;
}

void bots_initialize_for_new_map(
	void)
{
	char const *skill_name = config_string("bots.skill");
	short skill_index;
	long count = config_integer("bots.count");

	csmemset(&bots_globals.bots, 0, sizeof(bots_globals.bots));
	csmemset(&bots_globals.navigation, 0, sizeof(bots_globals.navigation));
	/* (the old map's bots' searches go with them) */
	bot_navigation_reset_all();
	vehicle_navigation_reset_all();
	bots_globals.had_bots = FALSE;
	bots_globals.sandbox_done = FALSE;
	bots_globals.log_decisions = config_boolean("debug.bot_decisions") != 0;
	bots_globals.navigation_only = config_boolean("debug.bot_navigation_only") != 0;
	bots_globals.team_spawns = config_boolean("bots.team_spawns") != 0;
	bots_globals.tuning = &bot_slayer_tuning;
	bot_manager_reset();
	bots_globals.epoch++;
	bots_globals.desired_count = (short)PIN(count, 0, BOTS_MAXIMUM);
	/* port: a scenario run has exactly the bots its trials use */
	bot_scenario_initialize_for_new_map();
	if (bot_scenario_enabled())
		bots_globals.desired_count = bot_scenario_bot_count();
	bots_globals.skill = &bot_skills[1];
	for (skill_index = 0; skill_index < NUMBEROF(bot_skills); skill_index++)
	{
		if (skill_name && !csstrcasecmp(skill_name, bot_skills[skill_index].name))
			bots_globals.skill = &bot_skills[skill_index];
	}
	/* port: consume the lobby override once for supported local modes.
	A different next map must not carry these teams into an ordinary host. */
	lobby_friendly_count = NONE;
	if (lobby_match.pending && game_connection() == _game_connection_local &&
		game_engine_get_variant() && (game_engine_get_variant()->game_engine_index == game_engine_slayer ||
		(config_boolean("bots.ctf") && game_engine_ctf_bot_variant_supported(game_engine_get_variant()))))
	{
		bots_globals.desired_count = lobby_match.friendly_count + lobby_match.enemy_count;
		bots_globals.skill = &bot_skills[lobby_match.skill];
		lobby_friendly_count = lobby_match.friendly_count;
	}
	lobby_match.pending = FALSE;
	bots_globals.behavior_valid =
		engine_ai_behavior_validate(&bot_behavior) == ENGINE_AI_BEHAVIOR_VALID;
}

void bots_update(
	void)
{
	short bot_index;

	if (bots_globals.desired_count == 0 && !bots_globals.had_bots)
		return;
	if (!bots_globals.behavior_valid)
		return;
	bots_refresh();
	if (!bots_host_may_have_bots())
	{
		/* The mode, host, remote-machine or postgame gate no longer permits
		bots. They are not replicated to other machines. */
		for (bot_index = 0; bot_index < BOTS_MAXIMUM; bot_index++)
		{
			if (bots_globals.bots[bot_index].active)
				bot_leave(&bots_globals.bots[bot_index]);
		}
		return;
	}
	bots_join();
	/* port: a scenario places its bots before they decide */
	bot_scenario_update();
	bot_manager_update();
	/* (the shared route searches advance before the bots decide, so that a
	route found this tick is followed this tick) */
	{
		clock_t before = clock();

		bot_navigation_update();
		vehicle_navigation_update();
		for (bot_index = 0; bot_index < BOTS_MAXIMUM; bot_index++)
		{
			if (bots_globals.bots[bot_index].active)
				bot_think(&bots_globals.bots[bot_index]);
		}
		bot_scenario_end_tick(1000.0 * (double)(clock() - before) / CLOCKS_PER_SEC);
	}
}

boolean bots_game_had_bots(
	void)
{
	return bots_globals.had_bots;
}

/* port: the private lobby leaves persistent bots.count unchanged. */
boolean bots_navigation_requested(void)
{
	return bots_globals.desired_count > 0;
}

boolean bots_player_is_bot(
	long player_index)
{
	short bot_index;

	for (bot_index = 0; bot_index < BOTS_MAXIMUM; bot_index++)
	{
		if (bots_globals.bots[bot_index].active && bots_globals.bots[bot_index].player_index == player_index)
			return TRUE;
	}
	return FALSE;
}

/* ---------- private code: math */

static real bot_angle_normalize(
	real angle)
{
	/* Bounded: a non-finite angle is no direction, and fmod (exact in IEEE
	arithmetic, halo_math.h) brings a large one within a turn first, so each
	loop below runs at most once. */
	if (!valid_real(angle))
		return 0.f;
	angle = (real)fmod(angle, 2.0 * M_PI);
	while (angle > (real)M_PI)
		angle -= 2.f * (real)M_PI;
	while (angle < -(real)M_PI)
		angle += 2.f * (real)M_PI;
	return angle;
}

static real bot_horizontal_distance(
	real_point3d const *a,
	real_point3d const *b)
{
	real i = b->x - a->x;
	real j = b->y - a->y;

	return square_root(i * i + j * j);
}

static real bot_yaw_to(
	real_point3d const *from,
	real_point3d const *to)
{
	return arctangent(to->y - from->y, to->x - from->x);
}

static real_euler_angles2d bot_angles_to(
	real_point3d const *from,
	real_point3d const *to)
{
	real_vector3d vector;
	real_euler_angles2d angles;

	vector.i = to->x - from->x;
	vector.j = to->y - from->y;
	vector.k = to->z - from->z;
	if (vector.i == 0.f && vector.j == 0.f && vector.k == 0.f)
		vector.i = 1.f;
	euler_angles2d_from_vector3d(&angles, &vector);
	return angles;
}

/* [0, 1) from the bot's own generator: bots never draw from the game's
random numbers, which every machine's simulation shares */
static real bot_random(
	struct bot *bot)
{
	uint32_t value = 0;

	engine_ai_rng32_next(&bot->rng, &value);
	return (real)((double)value / 4294967296.0);
}

static real bot_random_range(
	struct bot *bot,
	real minimum,
	real maximum)
{
	return minimum + (maximum - minimum) * bot_random(bot);
}

static long bot_seconds_to_ticks(
	real seconds)
{
	return (long)(seconds * TICKS_PER_SECOND + 0.5f);
}

/* ---------- private code: the world as a bot may know it */

static struct unit_datum *bot_living_unit(
	long unit_index)
{
	struct unit_datum *unit;

	if (unit_index == NONE)
		return NULL;
	unit = unit_try_and_get(unit_index);
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return NULL;
	return unit;
}

/* the machine of the bot in a slot */
static long bot_machine_index(
	short slot)
{
	return BOT_MACHINE_INDEX - slot / MAXIMUM_LOCAL_PLAYERS;
}

static struct player_datum *bot_player(
	struct bot const *bot)
{
	struct player_datum *player = player_try_and_get(bot->player_index);

	if (!player || player->network_player_data.machine_index != bot_machine_index(bot->slot) ||
		player->quit_out_of_game)
		return NULL;
	return player;
}

static boolean bot_line_clear(
	unsigned long flags,
	real_point3d const *from,
	real_point3d const *to,
	long ignore_object_index)
{
	struct collision_result collision;
	real_vector3d vector;

	vector.i = to->x - from->x;
	vector.j = to->y - from->y;
	vector.k = to->z - from->z;
	return !collision_test_vector(flags, from, &vector, ignore_object_index, &collision);
}

static void bot_aim_point(
	long unit_index,
	real_point3d *point)
{
	unit_get_head_position(unit_index, point);
	/* (the chest, below the head) */
	point->z -= 0.12f;
}

static boolean bot_players_are_enemies(
	struct player_datum const *player,
	long player_index,
	struct player_datum const *other,
	long other_index)
{
	if (player_index == other_index)
		return FALSE;
	if (game_engine_has_teams())
		return player->team_index != other->team_index;
	return TRUE;
}

/* ---------- private code: navigation */

#define BOT_NAV_FLAGS (FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit))
#define BOT_NAV_PROBE_STEP 0.75f
#define BOT_NAV_MAXIMUM_CLIMB 0.6f
#define BOT_NAV_MAXIMUM_DROP 3.f

/* the ground's height below the point, within depth */
static boolean bot_navigation_ground(
	real_point3d const *above,
	real depth,
	real *ground_z)
{
	struct collision_result collision;
	real_vector3d down;

	down.i = 0.f;
	down.j = 0.f;
	down.k = -depth;
	if (!collision_test_vector(BOT_NAV_FLAGS, above, &down, NONE, &collision))
		return FALSE;
	*ground_z = collision.point.z;
	return TRUE;
}

/* whether a biped can walk from a to b: stepping along the ground, nothing
in the way at its hips, no step up higher than it can climb, no gap and no
drop it would not survive. Directional: a drop is walkable one way only */
static boolean bot_navigation_walkable(
	real_point3d const *a,
	real_point3d const *b)
{
	real horizontal = bot_horizontal_distance(a, b);
	short steps = (short)PIN((long)(horizontal / BOT_NAV_PROBE_STEP) + 1, 1, 64);
	real_point3d probe;
	real ground_z;
	real end_z;
	short step;

	probe = *a;
	probe.z += 0.5f;
	if (!bot_navigation_ground(&probe, 2.5f, &ground_z))
		return FALSE;
	probe = *b;
	probe.z += 0.5f;
	if (!bot_navigation_ground(&probe, 2.5f, &end_z))
		return FALSE;
	for (step = 1; step <= steps; step++)
	{
		real t = (real)step / (real)steps;
		real_point3d from;
		real_point3d to;
		real next_z;

		from.x = a->x + (b->x - a->x) * (real)(step - 1) / (real)steps;
		from.y = a->y + (b->y - a->y) * (real)(step - 1) / (real)steps;
		from.z = ground_z + BOT_NAV_EYE_HEIGHT;
		to.x = a->x + (b->x - a->x) * t;
		to.y = a->y + (b->y - a->y) * t;
		to.z = from.z;
		/* (a wall, or a slope too steep) */
		if (!bot_line_clear(BOT_NAV_FLAGS, &from, &to, NONE))
			return FALSE;
		to.z = ground_z + BOT_NAV_MAXIMUM_CLIMB;
		if (!bot_navigation_ground(&to, BOT_NAV_MAXIMUM_CLIMB + BOT_NAV_MAXIMUM_DROP, &next_z))
			return FALSE;
		ground_z = next_z;
	}
	return (real)fabs(ground_z - end_z) < 1.f;
}

static boolean bot_navigation_linked(
	struct bot_navigation const *navigation,
	uint32_t link_count,
	uint32_t source,
	uint32_t destination)
{
	uint32_t index;

	for (index = 0; index < link_count; index++)
	{
		if (navigation->links[index].source_node == source &&
			navigation->links[index].destination_node == destination)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void bot_navigation_add_link(
	struct bot_navigation *navigation,
	uint32_t *link_count,
	uint32_t source,
	uint32_t destination,
	real distance)
{
	struct engine_ai_nav_link *link;

	if (*link_count >= ENGINE_AI_NAV_LINKS_MAX)
		return;
	link = &navigation->links[(*link_count)++];
	link->source_node = source;
	link->destination_node = destination;
	link->cost = (uint32_t)(distance * 100.f) + 1;
	link->required_capabilities = 0;
	link->traversal_id = 0;
	link->enabled = true;
}

/* the map's graph: its player starting locations (of every game type), each
linked both ways to its nearest few that a biped can walk to in a straight
line. Built once a map, when its first bot needs it */
static void bot_navigation_build(
	void)
{
	struct bot_navigation *navigation = &bots_globals.navigation;
	short location_count = player_get_starting_location_count();
	short location_index;
	uint32_t node;
	uint32_t link_count = 0;

	navigation->built = TRUE;
	navigation->node_count = 0;
	for (location_index = 0; location_index < location_count &&
		navigation->node_count < ENGINE_AI_NAV_NODES_MAX; location_index++)
	{
		struct player_starting_location const *location = player_get_starting_location(location_index);
		boolean duplicate = FALSE;

		if (!location)
			continue;
		for (node = 0; node < navigation->node_count; node++)
		{
			if (distance3d(&navigation->nodes[node], &location->position) < BOT_NAV_NODE_MERGE_DISTANCE)
				duplicate = TRUE;
		}
		if (!duplicate)
			navigation->nodes[navigation->node_count++] = location->position;
	}

	for (node = 0; node < navigation->node_count; node++)
	{
		/* (some starting locations are a little above the ground) */
		real_point3d probe = navigation->nodes[node];
		real ground_z;

		probe.z += 0.5f;
		if (bot_navigation_ground(&probe, 3.f, &ground_z))
			navigation->nodes[node].z = ground_z + 0.05f;
	}

	for (node = 0; node < navigation->node_count; node++)
	{
		uint32_t nearest[BOT_NAV_NEIGHBORS];
		real nearest_distance[BOT_NAV_NEIGHBORS];
		short nearest_count = 0;
		uint32_t other;
		short index;

		/* (the nearest in range, nearest first) */
		for (other = 0; other < navigation->node_count; other++)
		{
			real distance;
			short position;

			if (other == node)
				continue;
			distance = distance3d(&navigation->nodes[node], &navigation->nodes[other]);
			if (distance > BOT_NAV_LINK_RANGE)
				continue;
			for (position = nearest_count; position > 0 && nearest_distance[position - 1] > distance; position--)
			{
				if (position < BOT_NAV_NEIGHBORS)
				{
					nearest[position] = nearest[position - 1];
					nearest_distance[position] = nearest_distance[position - 1];
				}
			}
			if (position < BOT_NAV_NEIGHBORS)
			{
				nearest[position] = other;
				nearest_distance[position] = distance;
				if (nearest_count < BOT_NAV_NEIGHBORS)
					nearest_count++;
			}
		}
		for (index = 0; index < nearest_count; index++)
		{
			other = nearest[index];
			if (!bot_navigation_linked(navigation, link_count, node, other) &&
				bot_navigation_walkable(&navigation->nodes[node], &navigation->nodes[other]))
			{
				bot_navigation_add_link(navigation, &link_count, node, other, nearest_distance[index]);
			}
			if (!bot_navigation_linked(navigation, link_count, other, node) &&
				bot_navigation_walkable(&navigation->nodes[other], &navigation->nodes[node]))
			{
				bot_navigation_add_link(navigation, &link_count, other, node, nearest_distance[index]);
			}
		}
	}

	navigation->graph.links = navigation->links;
	navigation->graph.node_count = navigation->node_count;
	navigation->graph.link_count = link_count;
	navigation->graph.revision = bots_globals.epoch;
	platform_log("bots: navigation graph of %lu nodes and %lu links",
		(unsigned long)navigation->node_count, (unsigned long)link_count);
}

static struct engine_ai_nav_graph const *bot_navigation_graph(
	void)
{
	if (!bots_globals.navigation.built)
		bot_navigation_build();
	return bots_globals.navigation.node_count > 0 ? &bots_globals.navigation.graph : NULL;
}

/* the nearest node the point can walk to (of the few nearest), else the
nearest; NONE for none */
static long bot_navigation_nearest_node(
	real_point3d const *point)
{
	struct bot_navigation const *navigation = &bots_globals.navigation;
	uint32_t nearest[BOT_NAV_NEIGHBORS];
	real nearest_distance[BOT_NAV_NEIGHBORS];
	short nearest_count = 0;
	uint32_t node;
	short index;

	for (node = 0; node < navigation->node_count; node++)
	{
		real distance = distance3d(point, &navigation->nodes[node]);
		short position;

		for (position = nearest_count; position > 0 && nearest_distance[position - 1] > distance; position--)
		{
			if (position < BOT_NAV_NEIGHBORS)
			{
				nearest[position] = nearest[position - 1];
				nearest_distance[position] = nearest_distance[position - 1];
			}
		}
		if (position < BOT_NAV_NEIGHBORS)
		{
			nearest[position] = node;
			nearest_distance[position] = distance;
			if (nearest_count < BOT_NAV_NEIGHBORS)
				nearest_count++;
		}
	}
	for (index = 0; index < nearest_count; index++)
	{
		if (bot_navigation_walkable(point, &navigation->nodes[nearest[index]]))
			return (long)nearest[index];
	}
	return nearest_count > 0 ? (long)nearest[0] : NONE;
}

/* a link a bot could not get along: off, in a new revision of the graph
(every route through the old one is replanned: engine_ai_traversal_step) */
static void bot_navigation_disable_link(
	uint32_t link_index)
{
	struct bot_navigation *navigation = &bots_globals.navigation;

	if (link_index >= navigation->graph.link_count || !navigation->links[link_index].enabled)
		return;
	navigation->links[link_index].enabled = false;
	navigation->graph.revision++;
	platform_log("bots: link %lu (node %lu to %lu) is not walkable: off", (unsigned long)link_index,
		(unsigned long)navigation->links[link_index].source_node,
		(unsigned long)navigation->links[link_index].destination_node);
}

static void bot_navigation_reset(
	struct bot *bot)
{
	bot->searching = FALSE;
	bot->following = FALSE;
	bot->stuck_count = 0;
	bot->progress_time = NONE;
	bot_navigation_agent_reset_path(&bot->navigation_agent);
}

/* ---------- private code: perception */

static boolean bot_can_see(
	struct bot const *bot,
	long unit_index,
	real_point3d const *eye,
	real_point3d const *point)
{
	struct bot_skill const *skill = bots_globals.skill;
	real distance = distance3d(eye, point);
	real_euler_angles2d angles;
	real yaw_offset;

	if (distance > skill->sight_range)
		return FALSE;
	angles = bot_angles_to(eye, point);
	yaw_offset = (real)fabs(bot_angle_normalize(angles.yaw - bot->facing.yaw));
	/* (out of view, unless close enough to hear) */
	if (yaw_offset > DEGREES_TO_RADIANS(skill->field_of_view_degrees) && distance > 3.f)
		return FALSE;
	return bot_line_clear(_collision_test_for_line_of_sight_flags, eye, point, unit_index);
}

/* what the bot knows of its enemies: the nearest it sees (the one it already
fights first, while it sees it), or one that has just hurt it */
static void bot_perceive(
	struct bot *bot,
	struct player_datum const *player,
	struct unit_datum const *unit)
{
	long now = game_time_get();
	struct data_iterator iterator;
	struct player_datum *other;
	real_point3d eye;
	long best_index = NONE;
	real best_distance = 0.f;
	real_point3d best_position;
	short attacker_index;
	boolean shares = bots_globals.skill->shares_sightings;
	/* port: a bot still getting its bearings (bot_orient) sees nobody yet */
	boolean orienting = now < bot->aware_time;

	bot->visible_enemy_count = 0;
	unit_get_camera_position(bot->unit_index, &eye);
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		real_point3d point;
		real distance;

		if (orienting ||
			!bot_players_are_enemies(player, bot->player_index, other, iterator.datum_index) ||
			!bot_living_unit(other->unit_index) ||
			bot_scenario_ignores_player(iterator.datum_index)) /* port: outside a scenario's trial */
		{
			continue;
		}
		/* port: camouflage blocks fresh sightings, including near contacts
		and callouts. Fade-out amounts count too; memory and recent attackers
		remain separate knowledge, never visual confirmation. */
		{
			struct unit_datum const *target = bot_living_unit(other->unit_index);

			if (TEST_FLAG(target->unit.flags, _unit_active_camouflaged_bit) ||
				TEST_FLAG(target->unit.flags, _unit_super_camouflaged_bit) ||
				target->unit.active_camouflage > 0.f ||
				target->unit.active_camouflage_super_amount > 0.f)
			{
				continue;
			}
		}
		bot_aim_point(other->unit_index, &point);
		if (!bot_can_see(bot, bot->unit_index, &eye, &point))
			continue;
		bot->visible_enemy_count++;
		/* (called out to the team, by skills that do) */
		if (shares)
			bot_manager_report_sighting(player->team_index, iterator.datum_index,
				(short)(bot->slot + 1), &point);
		distance = distance3d(&eye, &point);
		/* (the current target is kept while it is seen) */
		if (iterator.datum_index == bot->target_player_index)
			distance *= 0.5f;
		if (best_index == NONE || distance < best_distance)
		{
			best_index = iterator.datum_index;
			best_distance = distance;
			best_position = point;
		}
	}

	/* (an enemy that has just hurt it, seen or not: it turns to it) */
	if (best_index == NONE)
	{
		for (attacker_index = 0; attacker_index < MAXIMUM_ATTACKERS_PER_UNIT; attacker_index++)
		{
			struct unit_attacker const *attacker = &unit->unit.attackers[attacker_index];
			struct player_datum *attacking_player;

			if (attacker->player_index == NONE ||
				now - (long)attacker->game_time_stamp > BOT_ATTACKER_AWARENESS_TICKS ||
				bot_scenario_ignores_player(attacker->player_index))
			{
				continue;
			}
			attacking_player = player_try_and_get(attacker->player_index);
			if (!attacking_player ||
				!bot_players_are_enemies(player, bot->player_index, attacking_player, attacker->player_index) ||
				!bot_living_unit(attacking_player->unit_index))
			{
				continue;
			}
			bot_aim_point(attacking_player->unit_index, &bot->target_last_position);
			/* (being hurt ends getting its bearings at once) */
			bot->aware_time = now;
			if (bot->target_player_index != attacker->player_index)
				bot->target_first_seen_time = now;
			bot->target_player_index = attacker->player_index;
			bot->target_last_seen_time = now;
			bot->target_visible = FALSE;
			bot->target_source = _bot_target_attacker;
			break;
		}
	}

	if (best_index != NONE)
	{
		/* (seen again at once, far from where it just was: a teleporter) */
		if (best_index == bot->target_observed_player_index &&
			bot->target_observed_time != NONE && now >= bot->target_observed_time &&
			now - bot->target_observed_time <= 2 &&
			distance3d(&best_position, &bot->target_observed_position) > BOT_TELEPORT_DISTANCE)
		{
			platform_log("bots: bot %d saw enemy %ld teleport to (%.1f %.1f %.1f)", bot->slot + 1,
				(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(best_index),
				best_position.x, best_position.y, best_position.z);
		}
		bot->target_observed_player_index = best_index;
		bot->target_observed_position = best_position;
		bot->target_observed_time = now;
		if (best_index != bot->target_player_index || !bot->target_visible)
			bot->target_first_seen_time = now;
		bot->target_player_index = best_index;
		bot->target_visible = TRUE;
		bot->target_last_seen_time = now;
		bot->target_last_position = best_position;
		bot->target_source = _bot_target_seen;
	}
	else if (bot->target_source != _bot_target_attacker || bot->target_last_seen_time != now)
	{
		struct player_datum *target = bot->target_player_index != NONE ?
			player_try_and_get(bot->target_player_index) : NULL;

		bot->target_visible = FALSE;
		/* (forgotten after a while, or once it is dead) */
		if (!target || !bot_living_unit(target->unit_index) ||
			now - bot->target_last_seen_time > bot_seconds_to_ticks(BOT_TARGET_MEMORY_SECONDS))
		{
			bot->target_player_index = NONE;
			bot->target_source = _bot_target_none;
		}
		else if (bot->target_source == _bot_target_seen)
			bot->target_source = _bot_target_remembered;
	}

	/* (with nobody to fight, where a teammate last saw an enemy: every bot
	reads the team's sightings, but only skills that share write them) */
	if (now >= bot->aware_time &&
		(bot->target_player_index == NONE || bot->target_source == _bot_target_shared))
	{
		real_point3d origin;
		long shared_index;
		real_point3d shared_position;

		object_get_origin(bot->unit_index, &origin);
		if (bot_manager_nearest_sighting(player->team_index, &origin, BOT_SIGHTING_MEMORY_TICKS,
			&shared_index, &shared_position))
		{
			if (shared_index != bot->target_player_index)
				bot->target_first_seen_time = now;
			bot->target_player_index = shared_index;
			bot->target_visible = FALSE;
			bot->target_last_seen_time = now;
			bot->target_last_position = shared_position;
			bot->target_source = _bot_target_shared;
		}
		else if (bot->target_source == _bot_target_shared)
		{
			bot->target_player_index = NONE;
			bot->target_source = _bot_target_none;
		}
	}
}

/* ---------- private code: behavior leaves */

static void bot_emit_look(
	struct engine_ai_behavior_intent_emitter *emitter,
	real_euler_angles2d const *angles)
{
	struct bot_look_payload payload;

	payload.yaw = angles->yaw;
	payload.pitch = angles->pitch;
	engine_ai_behavior_intent_emit(emitter, _bot_intent_look, BOT_INTENT_SCHEMA, &payload, sizeof(payload));
}

static void bot_emit_move(
	struct engine_ai_behavior_intent_emitter *emitter,
	real yaw,
	real speed)
{
	struct bot_move_payload payload;

	payload.yaw = yaw;
	payload.speed = speed;
	engine_ai_behavior_intent_emit(emitter, _bot_intent_move, BOT_INTENT_SCHEMA, &payload, sizeof(payload));
}

static void bot_emit_fire(
	struct engine_ai_behavior_intent_emitter *emitter,
	real trigger)
{
	struct bot_fire_payload payload;

	payload.trigger = trigger;
	engine_ai_behavior_intent_emit(emitter, _bot_intent_fire, BOT_INTENT_SCHEMA, &payload, sizeof(payload));
}

/* aim at the target with this skill's error, and shoot when the aim is near
enough, after its reaction time, in bursts. port: the error widens with
distance and while the bot moves, and narrows as it tracks the target
(engine_ai/aim.h); nothing beyond the skill's fire range is shot at, so a
distant sighting draws the bot in rather than rooting it to the spot. */
static void bot_engage(
	struct bot *bot,
	real_point3d const *eye,
	struct engine_ai_behavior_intent_emitter *emitter)
{
	struct bot_skill const *skill = bots_globals.skill;
	struct unit_datum const *unit = bot_living_unit(bot->unit_index);
	long now = game_time_get();
	real_euler_angles2d aim = bot_angles_to(eye, &bot->target_last_position);
	real distance = distance3d(eye, &bot->target_last_position);
	real offset;

	if (!unit)
		return;
	if (now >= bot->aim_error_time)
	{
		struct engine_ai_aim_skill aim_skill;
		real tracked = (real)(now - bot->target_first_seen_time) / (real)TICKS_PER_SECOND;
		real error;

		aim_skill.base_error_degrees = skill->aim_error_degrees;
		aim_skill.minimum_error_degrees = skill->aim_error_degrees * 0.4f;
		aim_skill.accurate_range = skill->accurate_range;
		aim_skill.settle_seconds = skill->aim_settle_seconds;
		aim_skill.moving_penalty = skill->moving_aim_penalty;
		error = DEGREES_TO_RADIANS(engine_ai_aim_error_degrees(&aim_skill, distance,
			bot->target_visible ? tracked : -1.f, bot->moved_last_tick));

		bot->aim_error.yaw = bot_random_range(bot, -error, error);
		bot->aim_error.pitch = bot_random_range(bot, -error, error) * 0.5f;
		bot->aim_error_time = now + bot_seconds_to_ticks(bot_random_range(bot, 0.3f, 0.7f));
	}
	aim.yaw = bot_angle_normalize(aim.yaw + bot->aim_error.yaw);
	aim.pitch += bot->aim_error.pitch;
	bot_emit_look(emitter, &aim);

	if (!bot->target_visible ||
		now - bot->target_first_seen_time < bot_seconds_to_ticks(skill->reaction_seconds) ||
		distance > skill->fire_range)
	{
		return;
	}
	offset = (real)fabs(bot_angle_normalize(aim.yaw - bot->facing.yaw)) + (real)fabs(aim.pitch - bot->facing.pitch);
	if (offset > DEGREES_TO_RADIANS(skill->fire_tolerance_degrees))
		return;
	if (skill->burst_seconds > 0.f && now >= bot->burst_change_time)
	{
		bot->burst_firing = !bot->burst_firing;
		bot->burst_change_time = now + bot_seconds_to_ticks(bot->burst_firing ?
			bot_random_range(bot, 0.5f, 1.f) * skill->burst_seconds :
			bot_random_range(bot, 0.5f, 1.f) * skill->burst_pause_seconds);
	}
	if (unit->object.parent_object_index == NONE &&
		distance < 1.5f && now >= bot->melee_time)
	{
		bot->melee_time = now + bot_seconds_to_ticks(1.f);
		engine_ai_behavior_intent_emit(emitter, _bot_intent_melee, BOT_INTENT_SCHEMA, NULL, 0);
	}
	else if (skill->burst_seconds <= 0.f || bot->burst_firing)
		bot_emit_fire(emitter, 1.f);
	if (unit->object.parent_object_index == NONE &&
		skill->throws_grenades && now >= bot->grenade_time && distance > 8.f && distance < 20.f)
	{
		bot->grenade_time = now + bot_seconds_to_ticks(BOT_GRENADE_COOLDOWN_SECONDS * bot_random_range(bot, 1.f, 2.f));
		engine_ai_behavior_intent_emit(emitter, _bot_intent_grenade, BOT_INTENT_SCHEMA, NULL, 0);
	}
}

static engine_ai_behavior_result bot_leaf_fight(
	struct bot *bot,
	struct engine_ai_behavior_intent_emitter *emitter)
{
	struct bot_skill const *skill = bots_globals.skill;
	long now = game_time_get();
	real_point3d eye;
	real_point3d origin;
	real distance;
	real toward;
	struct sn_point goal;
	struct bot_navigation_command command;
	enum bot_navigation_result navigation_result;

	if (bot->target_player_index == NONE)
		return ENGINE_AI_BEHAVIOR_FAILURE;
	unit_get_camera_position(bot->unit_index, &eye);
	object_get_origin(bot->unit_index, &origin);
	bot_engage(bot, &eye, emitter);

	distance = bot_horizontal_distance(&origin, &bot->target_last_position);
	toward = bot_yaw_to(&origin, &bot->target_last_position);
	if (!bot->target_visible || distance > 10.f)
	{
		/* Pursuit uses the BSP route too; no direct steering through walls. */
		goal.x = bot->target_last_position.x;
		goal.y = bot->target_last_position.y;
		goal.z = bot->target_last_position.z;
		navigation_result = bot_navigation_agent_move_to(&bot->navigation_agent,
			bot->unit_index, goal, 1.f, 1.f, &command);
		if (command.jump)
			engine_ai_behavior_intent_emit(emitter, _bot_intent_jump, BOT_INTENT_SCHEMA, NULL, 0);
		if (navigation_result == BOT_NAVIGATION_MOVING && command.has_move)
			bot_emit_move(emitter, command.yaw, command.speed);
		else if (navigation_result == BOT_NAVIGATION_UNAVAILABLE || bot->target_visible)
		{
			/* (in sight, with no route yet: close in directly rather than stand
			and trade shots from range) */
			bot_emit_move(emitter, toward, 1.f);
		}
		return ENGINE_AI_BEHAVIOR_RUNNING;
	}
	bot_navigation_agent_reset_path(&bot->navigation_agent);
	if (now >= bot->strafe_change_time)
	{
		bot->strafe_sign = bot_random(bot) < 0.5f ? -1.f : 1.f;
		/* (a skill that cannot strafe well changes direction seldom) */
		bot->strafe_change_time = now + bot_seconds_to_ticks(skill->strafes ?
			bot_random_range(bot, 0.5f, 1.4f) : bot_random_range(bot, 1.5f, 3.f));
	}
	if (distance < 3.f)
		bot_emit_move(emitter, toward + (real)M_PI, 1.f);
	else if (skill->strafes)
		bot_emit_move(emitter, toward + bot->strafe_sign * (real)M_PI_2, 1.f);
	else
	{
		/* port: a recruit never stands still to shoot, but its sidestep is a
		slow, clumsy drift half toward the target */
		bot_emit_move(emitter, toward + bot->strafe_sign * 0.9f, 0.45f);
	}
	/* port: the best skills crouch now and then mid-fight, as players
	crouch-strafe to shrink and steady themselves */
	if (skill->crouches && now >= bot->crouch_next_time)
	{
		bot->crouch_until = now + bot_seconds_to_ticks(bot_random_range(bot, 0.3f, 0.8f));
		bot->crouch_next_time = now + bot_seconds_to_ticks(bot_random_range(bot, 2.5f, 5.f));
	}
	if (now < bot->crouch_until)
		engine_ai_behavior_intent_emit(emitter, _bot_intent_crouch, BOT_INTENT_SCHEMA, NULL, 0);
	/* (a jump now and then, as players do) */
	if (skill->retreats && bot_random(bot) < 0.01f)
		engine_ai_behavior_intent_emit(emitter, _bot_intent_jump, BOT_INTENT_SCHEMA, NULL, 0);
	return ENGINE_AI_BEHAVIOR_RUNNING;
}

/* back away from the threat, still shooting, until the shields come back */
static engine_ai_behavior_result bot_leaf_retreat(
	struct bot *bot,
	struct engine_ai_behavior_intent_emitter *emitter)
{
	struct unit_datum *unit = bot_living_unit(bot->unit_index);
	long now = game_time_get();
	real_point3d eye;
	real_point3d origin;
	real away;

	if (!unit || bot->target_player_index == NONE)
		return ENGINE_AI_BEHAVIOR_FAILURE;
	/* (the selector chose it; it ends itself once the shields are back) */
	if (bot->retreat_start_time == NONE)
		bot->retreat_start_time = now;
	if (unit->object.shield_vitality > 0.9f || now - bot->retreat_start_time > bot_seconds_to_ticks(5.f))
	{
		bot->retreat_start_time = NONE;
		bot->retreat_cooldown_time = now + bot_seconds_to_ticks(6.f);
		return ENGINE_AI_BEHAVIOR_SUCCESS;
	}
	unit_get_camera_position(bot->unit_index, &eye);
	object_get_origin(bot->unit_index, &origin);
	bot_engage(bot, &eye, emitter);
	away = bot_yaw_to(&bot->target_last_position, &origin);
	if (now >= bot->strafe_change_time)
	{
		bot->strafe_sign = bot_random(bot) < 0.5f ? -1.f : 1.f;
		bot->strafe_change_time = now + bot_seconds_to_ticks(bot_random_range(bot, 0.6f, 1.2f));
	}
	bot_emit_move(emitter, away + bot->strafe_sign * 0.5f, 1.f);
	return ENGINE_AI_BEHAVIOR_RUNNING;
}

/* A coarse utility estimate, only for usable weapons. Empty battery or
   ammunition weapons must not lure a bot away from a usable loadout. */
static real bot_weapon_utility(long weapon_index)
{
	struct weapon_datum *weapon = weapon_try_and_get(weapon_index);
	struct weapon_definition *definition;
	struct weapon_trigger_definition *trigger;
	real rate;
	real damage;

	if (!weapon || TEST_FLAG(weapon->object.damage_flags, _object_dead_bit))
		return 0.f;
	definition = weapon_definition_get(weapon->definition_index);
	if (definition->weapon.triggers.count == 0 || weapon->weapon.age >= 0.99f)
		return 0.f;
	trigger = TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);
	if (trigger->rounds_per_shot > 0)
	{
		short magazine_index = trigger->magazine_index;
		if (magazine_index < 0 || magazine_index >= NUMBEROF(weapon->weapon.magazines))
			return 0.f;
		if (weapon->weapon.magazines[magazine_index].rounds_loaded <= 0 &&
			weapon->weapon.magazines[magazine_index].rounds_total <= 0)
			return 0.f;
	}
	damage = weapon_definition_get_damage_potential(weapon->definition_index, &rate);
	if (!valid_real(damage) || !valid_real(rate))
		return 0.f;
	return MAX(1.f, PIN(damage, 0.f, 1000.f) * PIN(rate, 1.f, 10.f));
}

/* An empty slot is useful; otherwise only a clear upgrade over the selected
   weapon warrants a swap. Duplicate definitions are handled as ammo by Halo. */
static boolean bot_wants_weapon(struct bot *bot, long weapon_index)
{
	struct unit_datum *unit = bot_living_unit(bot->unit_index);
	struct weapon_datum *candidate = weapon_try_and_get(weapon_index);
	short slot;
	boolean empty = FALSE;

	if (!unit || !candidate || bot_weapon_utility(weapon_index) <= 0.f ||
		candidate->object.parent_object_index != NONE ||
		!unit_can_use_weapon(bot->unit_index, weapon_index))
		return FALSE;
	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		long held_index = unit->unit.weapon_object_indices[slot];
		if (held_index == NONE)
			empty = TRUE;
		else if (weapon_get(held_index)->definition_index == candidate->definition_index)
			return FALSE;
	}
	if (empty)
		return TRUE;
	slot = unit->unit.current_weapon_index;
	return slot >= 0 && slot < MAXIMUM_WEAPONS_PER_UNIT &&
		bot_weapon_utility(weapon_index) >
		bot_weapon_utility(unit->unit.weapon_object_indices[slot]) * 1.35f;
}

/* Only empty compatible seats, no eviction. Prefer a gunner with a friendly
   driver, then a ground-vehicle driver. Aircraft need a separate controller. */
static short bot_vehicle_seat(struct bot *bot, long vehicle_index)
{
	struct unit_datum *vehicle = vehicle_try_and_get(vehicle_index);
	struct player_datum const *player = bot_player(bot);
	long team_index = player ? player->team_index : NONE;
	short bot_number = (short)(bot->slot + 1);
	struct unit_definition *definition;
	short seat_count;
	short seat_index;
	short best = NONE;
	short best_priority = 0;
	boolean driver_coming = FALSE;

	if (!vehicle || TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) ||
		vehicle->object.up.k < 0.6f ||
		(!vehicle_supports_bot_driver(vehicle_index) && !vehicle_is_fixed_turret(vehicle_index)) ||
		magnitude_squared3d(&vehicle->object.translational_velocity) >
			BOT_VEHICLE_SEAT_MAXIMUM_SPEED_SQUARED)
		return NONE;
	definition = unit_definition_get(vehicle->definition_index);
	seat_count = (short)MIN(definition->unit.seats.count, BOT_MANAGER_SEATS);
	/* port: a teammate already on the way to the driver's seat lets the others
	head for the rest; one bot per seat (bot_manager_claim_seat) */
	for (seat_index = 0; seat_index < seat_count; seat_index++)
	{
		struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat);

		if (TEST_FLAG(seat->flags, _unit_seat_driver_bit) &&
			bot_manager_seat_claimed_by_other(team_index, vehicle_index, seat_index, bot_number))
			driver_coming = TRUE;
	}
	for (seat_index = 0; seat_index < seat_count; seat_index++)
	{
		struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat);
		long occupant = NONE;
		short priority = TEST_FLAG(seat->flags, _unit_seat_driver_bit) ? 2 :
			(TEST_FLAG(seat->flags, _unit_seat_gunner_bit) ? 3 : 1);
		if (!TEST_FLAG(seat->flags, _unit_seat_driver_bit) && !driver_coming &&
			vehicle->unit.driver_object_index == NONE && vehicle_supports_bot_driver(vehicle_index))
			continue;
		if (bot_manager_seat_claimed_by_other(team_index, vehicle_index, seat_index, bot_number))
			continue;
		if (priority > best_priority && unit_can_enter_seat(bot->unit_index, vehicle_index, seat_index, &occupant))
		{
			best = seat_index;
			best_priority = priority;
		}
	}
	return best;
}

/* port: a new driver waits, at most BOT_VEHICLE_BOARD_WAIT_TICKS, while a
   teammate bot is on its way to one of the vehicle's seats or a human
   teammate on foot stands beside it with a seat open; then it drives. It no
   longer drives off to fetch teammates, nor waits for a full vehicle. */
static boolean bot_vehicle_should_wait(
	struct bot *bot,
	long vehicle_index)
{
	struct player_datum *player = bot_player(bot);
	struct unit_datum *vehicle = vehicle_try_and_get(vehicle_index);
	struct unit_definition *definition;
	struct data_iterator iterator;
	struct player_datum *teammate;
	long now = game_time_get();

	if (!player || !vehicle || !game_engine_has_teams() ||
		bot->vehicle_wait_until == NONE || now >= bot->vehicle_wait_until)
		return FALSE;
	if (bot_manager_vehicle_claimed_by_other(player->team_index, vehicle_index, (short)(bot->slot + 1)))
		return TRUE;
	definition = unit_definition_get(vehicle->definition_index);
	data_iterator_new(&iterator, player_data);
	/* (at most the player table's capacity) */
	while ((teammate = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct unit_datum *teammate_unit;
		short seat_index;

		if (iterator.datum_index == bot->player_index || teammate->team_index != player->team_index ||
			bots_player_is_bot(iterator.datum_index))
			continue;
		teammate_unit = bot_living_unit(teammate->unit_index);
		if (!teammate_unit || teammate_unit->object.parent_object_index != NONE ||
			distance3d(&vehicle->object.position, &teammate_unit->object.position) >
				BOT_VEHICLE_HUMAN_BOARD_DISTANCE)
			continue;
		for (seat_index = 0; seat_index < MIN(definition->unit.seats.count, BOT_MANAGER_SEATS); seat_index++)
		{
			struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(
				&definition->unit.seats, seat_index, struct unit_seat);
			long occupant = NONE;

			if (!TEST_FLAG(seat->flags, _unit_seat_driver_bit) &&
				unit_can_enter_seat(teammate->unit_index, vehicle_index, seat_index, &occupant))
				return TRUE;
		}
	}
	return FALSE;
}

/* ---------- private code: the chauffeur */

/* the active bot playing this player, NULL if none (BOTS_MAXIMUM looked at) */
static struct bot *bot_from_player(long player_index)
{
	short bot_index;

	if (player_index == NONE)
		return NULL;
	for (bot_index = 0; bot_index < BOTS_MAXIMUM; bot_index++)
	{
		if (bots_globals.bots[bot_index].active && bots_globals.bots[bot_index].player_index == player_index)
			return &bots_globals.bots[bot_index];
	}
	return NULL;
}

/* port: the seconds a teammate saves by riding rather than walking: walking
all the way, against waiting for the vehicle to come, getting in, and being
driven the (longer) way round. Distances are straight lines, as a player
judges them; negative, or not a number, is no saving. */
static real bot_ride_saving(
	real vehicle_to_rider,
	real rider_to_destination)
{
	real walk;
	real ride;

	if (!(vehicle_to_rider >= 0.f) || !(rider_to_destination >= 0.f))
		return 0.f;
	walk = rider_to_destination / BOT_RIDE_WALK_SPEED;
	ride = (vehicle_to_rider + rider_to_destination * BOT_RIDE_DRIVE_DETOUR) / BOT_RIDE_DRIVE_SPEED +
		BOT_RIDE_BOARD_SECONDS;
	return walk - ride;
}

/* where a bot is going: its objective (CTF), else nowhere it needs a ride to */
static boolean bot_destination(struct bot const *bot, real_point3d *destination)
{
	if (!bot->objective_valid || bot->objective.kind == _bot_ambition_guard)
		return FALSE;
	*destination = bot->objective.position;
	return TRUE;
}

/* whether a teammate on foot could take one of the vehicle's free seats
other than the driver's (none claimed by another bot) */
static boolean bot_ride_seat_open(struct bot *driver, long vehicle_index, long rider_player_index)
{
	struct unit_datum *vehicle = vehicle_try_and_get(vehicle_index);
	struct player_datum *rider = player_try_and_get(rider_player_index);
	struct player_datum const *player = bot_player(driver);
	struct unit_definition *definition;
	short seat_index;

	if (!vehicle || !rider || !player)
		return FALSE;
	definition = unit_definition_get(vehicle->definition_index);
	for (seat_index = 0; seat_index < MIN(definition->unit.seats.count, BOT_MANAGER_SEATS); seat_index++)
	{
		struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat);
		long occupant = NONE;

		if (TEST_FLAG(seat->flags, _unit_seat_driver_bit))
			continue;
		if (unit_can_enter_seat(rider->unit_index, vehicle_index, seat_index, &occupant) &&
			!bot_manager_seat_claimed_by_other(player->team_index, vehicle_index, seat_index,
				(short)(driver->slot + 1)))
			return TRUE;
	}
	return FALSE;
}

/* port: a driver picks up a teammate when the ride is quicker for them than
walking, as a player driving for the team does (in CTF especially): the
teammate within BOT_RIDE_PICKUP_RANGE who saves the most time, at least
BOT_RIDE_MINIMUM_SAVING, for no more than BOT_RIDE_MAXIMUM_DETOUR on the
driver's own way. A bot rider goes where its objective is; a human's, or a
bot's with none, is taken to be the driver's destination. Once a second,
over the player table (its capacity). */
static void bot_choose_pickup(
	struct bot *bot,
	long vehicle_index,
	real_point3d const *destination)
{
	struct player_datum *player = bot_player(bot);
	struct unit_datum *vehicle = vehicle_try_and_get(vehicle_index);
	struct data_iterator iterator;
	struct player_datum *teammate;
	long now = game_time_get();
	real best_saving = BOT_RIDE_MINIMUM_SAVING;
	long best_index = NONE;
	real driver_direct;

	if (!player || !vehicle || !destination || !game_engine_has_teams() || now < bot->pickup_scan_time)
		return;
	bot->pickup_scan_time = now + BOT_RIDE_SCAN_TICKS + bot->slot;
	driver_direct = bot_horizontal_distance(&vehicle->object.position, destination);
	data_iterator_new(&iterator, player_data);
	while ((teammate = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct unit_datum *teammate_unit;
		struct bot *rider;
		real_point3d rider_destination = *destination;
		real to_rider;
		real saving;

		if (iterator.datum_index == bot->player_index || teammate->team_index != player->team_index)
			continue;
		if (iterator.datum_index == bot->pickup_declined_player_index && now < bot->pickup_declined_until)
			continue;
		teammate_unit = bot_living_unit(teammate->unit_index);
		if (!teammate_unit || teammate_unit->object.parent_object_index != NONE)
			continue;
		rider = bot_from_player(iterator.datum_index);
		/* (a bot another driver is coming for, or still getting its bearings) */
		if (rider && ((rider->ride_vehicle_index != NONE && rider->ride_vehicle_index != vehicle_index &&
			now < rider->ride_until) || now < rider->aware_time))
			continue;
		/* (a bot with a duty where it stands, a guard, is not taken away;
		one with none rides to where the driver goes) */
		if (rider && rider->objective_valid && !bot_destination(rider, &rider_destination))
			continue;
		to_rider = bot_horizontal_distance(&vehicle->object.position, &teammate_unit->object.position);
		if (to_rider > BOT_RIDE_PICKUP_RANGE ||
			to_rider + bot_horizontal_distance(&teammate_unit->object.position, destination) - driver_direct >
				BOT_RIDE_MAXIMUM_DETOUR)
			continue;
		saving = bot_ride_saving(to_rider, bot_horizontal_distance(&teammate_unit->object.position,
			&rider_destination));
		if (saving <= best_saving || !bot_ride_seat_open(bot, vehicle_index, iterator.datum_index))
			continue;
		best_saving = saving;
		best_index = iterator.datum_index;
	}
	if (best_index == NONE || best_index == bot->pickup_player_index)
		return;
	bot->pickup_player_index = best_index;
	bot->pickup_until = now + BOT_RIDE_OFFER_TICKS;
	bot->pickup_meet = FALSE;
	{
		struct bot *rider = bot_from_player(best_index);

		if (rider)
		{
			rider->ride_vehicle_index = vehicle_index;
			rider->ride_until = bot->pickup_until;
		}
	}
	platform_log("bots: bot %d picks up teammate %ld (saves %.0f s)", bot->slot + 1,
		(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(best_index), best_saving);
}

/* the driver's pickup, while it is still on foot and the offer stands */
static struct unit_datum *bot_pickup_unit(struct bot *bot)
{
	struct player_datum *rider;
	struct unit_datum *unit;
	long now = game_time_get();

	if (bot->pickup_player_index == NONE)
		return NULL;
	rider = player_try_and_get(bot->pickup_player_index);
	unit = rider ? bot_living_unit(rider->unit_index) : NULL;
	if (now >= bot->pickup_until || !unit || unit->object.parent_object_index != NONE)
	{
		/* (got in somewhere, or let the offer go by: not asked again for a while) */
		if (now >= bot->pickup_until && unit && unit->object.parent_object_index == NONE)
		{
			bot->pickup_declined_player_index = bot->pickup_player_index;
			bot->pickup_declined_until = now + BOT_RIDE_DECLINED_TICKS;
		}
		bot->pickup_player_index = NONE;
		return NULL;
	}
	return unit;
}

/* port: a bot offered a ride waits for it, facing the vehicle; once the
vehicle has stopped beside it, it takes a free seat (an ordinary vehicle
opportunity, its seat claimed). It fights whatever it sees meanwhile. TRUE
while it stands waiting this tick. */
static boolean bot_await_ride(struct bot *bot, struct unit_datum const *unit)
{
	struct unit_datum *vehicle;
	struct player_datum const *player = bot_player(bot);
	long now = game_time_get();
	real distance;
	short seat;

	if (bot->ride_vehicle_index == NONE)
		return FALSE;
	vehicle = vehicle_try_and_get(bot->ride_vehicle_index);
	if (!vehicle || now >= bot->ride_until || unit->object.parent_object_index != NONE || !player ||
		TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) || vehicle->unit.driver_object_index == NONE)
	{
		bot->ride_vehicle_index = NONE;
		return FALSE;
	}
	if (bot->target_visible)
		return FALSE;
	if (bot->opportunity_index == bot->ride_vehicle_index && bot->opportunity_vehicle)
		return FALSE;
	/* (a stopped vehicle with a seat for it: it walks there and gets in;
	one still coming: it waits) */
	seat = bot_vehicle_seat(bot, bot->ride_vehicle_index);
	distance = bot_horizontal_distance(&unit->object.position, &vehicle->object.position);
	if (seat != NONE && distance <= BOT_RIDE_PICKUP_RANGE)
	{
		bot_drop_opportunity(bot, "a ride came");
		if (!bot_manager_claim_seat(player->team_index, bot->ride_vehicle_index, seat,
			(short)(bot->slot + 1), BOT_SEAT_CLAIM_TICKS))
			return FALSE;
		bot->opportunity_index = bot->ride_vehicle_index;
		bot->opportunity_team_index = player->team_index;
		bot->opportunity_vehicle = TRUE;
		bot->opportunity_powerup = FALSE;
		bot->opportunity_seat = seat;
		bot->opportunity_position = vehicle->object.position;
		bot->opportunity_end_time = now + bot_seconds_to_ticks(10.f);
		bot->decide_now = TRUE;
		platform_log("bots: bot %d boards its ride, seat %d", bot->slot + 1, seat);
		return FALSE;
	}
	csmemset(&bot->pending, 0, sizeof(bot->pending));
	bot->pending.has_look = TRUE;
	bot->pending.look = bot_angles_to(&unit->object.position, &vehicle->object.position);
	bot->pending.look.pitch = 0.f;
	bot->objective_waiting = TRUE; /* (waiting on purpose: not a stall) */
	bot_submit_action(bot);
	return TRUE;
}

/* port: whether a teammate on foot stands in the vehicle's way: within its
   width (and a margin) ahead of it (behind, backing up), as far as it goes
   in three quarters of a second plus a stopping distance. A driver brakes
   for them; it does not run teammates over. Over the player table (its
   capacity). */
static boolean bot_vehicle_teammate_ahead(
	struct bot *bot,
	struct unit_datum const *vehicle,
	boolean reversing)
{
	struct player_datum const *player = bot_player(bot);
	struct data_iterator iterator;
	struct player_datum *teammate;
	float half_width = 0.64f;
	float half_length = 1.15f;
	real speed;
	real reach;
	real forward_x;
	real forward_y;
	real length;

	if (!player || !game_engine_has_teams())
		return FALSE;
	navigation_world_vehicle_body(&half_width, &half_length, NULL);
	forward_x = vehicle->object.forward.i;
	forward_y = vehicle->object.forward.j;
	length = square_root(forward_x * forward_x + forward_y * forward_y);
	if (!(length > 0.01f))
		return FALSE;
	forward_x /= length;
	forward_y /= length;
	if (reversing)
	{
		forward_x = -forward_x;
		forward_y = -forward_y;
	}
	speed = square_root(magnitude_squared3d(&vehicle->object.translational_velocity));
	reach = half_length + BOT_VEHICLE_STOPPING_DISTANCE +
		(valid_real(speed) ? MIN(speed, BOT_MAXIMUM_SPEED_PER_TICK) : 0.f) * TICKS_PER_SECOND * 0.75f;
	data_iterator_new(&iterator, player_data);
	while ((teammate = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct unit_datum *teammate_unit;
		real dx;
		real dy;
		real along;
		real across;

		if (iterator.datum_index == bot->player_index || teammate->team_index != player->team_index)
			continue;
		teammate_unit = bot_living_unit(teammate->unit_index);
		if (!teammate_unit || teammate_unit->object.parent_object_index != NONE)
			continue;
		dx = teammate_unit->object.position.x - vehicle->object.position.x;
		dy = teammate_unit->object.position.y - vehicle->object.position.y;
		along = dx * forward_x + dy * forward_y;
		across = -dx * forward_y + dy * forward_x;
		if (along > 0.f && along <= reach && (across < 0.f ? -across : across) <= half_width + 0.5f &&
			(teammate_unit->object.position.z - vehicle->object.position.z) < 2.f &&
			(teammate_unit->object.position.z - vehicle->object.position.z) > -2.f)
			return TRUE;
	}
	return FALSE;
}

/* nothing of the structure overhead: open ground, not a base's rooms */
static boolean bot_open_sky(
	real_point3d const *point)
{
	real_point3d from = *point;
	real_point3d to = *point;

	from.z += 0.5f;
	to.z += BOT_VEHICLE_OPEN_SKY_HEIGHT;
	return bot_line_clear(FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_back_facing_surfaces_bit) | FLAG(_collision_test_structure_bit),
		&from, &to, NONE);
}

/* port: a place a vehicle can be driven to near a point: of the walkable
   places nearest it (in the vehicle layer's set the vehicle is in, when there
   is one), the first under open sky and not among the excluded (places a
   route was not found to), else the nearest not excluded. FALSE if none. */
static boolean bot_vehicle_place(
	struct unit_datum const *vehicle,
	real_point3d const *desired,
	real_point3d const *avoided,
	uint32_t const *excluded,
	short excluded_count,
	real_point3d *place,
	uint32_t *place_polygon)
{
	struct sn_resource const *resource = navigation_world_vehicle_resource();
	uint32_t component = SN_NONE;
	uint32_t candidates[BOT_VEHICLE_FIELD_CANDIDATES];
	real costs[BOT_VEHICLE_FIELD_CANDIDATES];
	short candidate_count = 0;
	short index;
	uint32_t polygon;

	if (resource)
	{
		struct sn_point position;

		position.x = vehicle->object.position.x;
		position.y = vehicle->object.position.y;
		position.z = vehicle->object.position.z;
		component = vehicle_navigation_component(position);
	}
	else
		resource = navigation_world_resource();
	if (!resource || !valid_real_point3d(desired))
		return FALSE;
	/* the nearest standable places, kept sorted (resource->polygon_count, at
	most SN_POLYGONS_MAX, each tried once) */
	for (polygon = 0; polygon < resource->polygon_count && polygon < SN_POLYGONS_MAX; polygon++)
	{
		struct sn_point center = resource->polygons[polygon].center;
		real dx = center.x - desired->x;
		real dy = center.y - desired->y;
		real dz = center.z - desired->z;
		/* (another floor counts for more than its height: a base's roof
		is no way to a flag inside it) */
		real cost = square_root(dx * dx + dy * dy) + 3.f * (dz < 0.f ? -dz : dz);
		short at;
		boolean skip = FALSE;

		if (avoided && square_root((center.x - avoided->x) * (center.x - avoided->x) +
			(center.y - avoided->y) * (center.y - avoided->y)) < BOT_VEHICLE_DROP_OFF_CLEARANCE)
			continue;

		if (sn_component(resource, polygon) == SN_NONE ||
			(component != SN_NONE && sn_component(resource, polygon) != component))
			continue;
		/* (inside a building: never a place to drive to, once known) */
		if (component != SN_NONE && navigation_world_vehicle_open_sky(polygon) == 0)
			continue;
		for (index = 0; index < excluded_count; index++)
			skip |= excluded[index] == polygon;
		if (skip)
			continue;
		if (candidate_count == BOT_VEHICLE_FIELD_CANDIDATES && cost >= costs[candidate_count - 1])
			continue;
		at = candidate_count < BOT_VEHICLE_FIELD_CANDIDATES ? candidate_count++ : candidate_count - 1;
		while (at > 0 && costs[at - 1] > cost)
		{
			costs[at] = costs[at - 1];
			candidates[at] = candidates[at - 1];
			at--;
		}
		costs[at] = cost;
		candidates[at] = polygon;
	}
	if (candidate_count == 0)
		return FALSE;
	/* (at most BOT_VEHICLE_FIELD_CANDIDATES rays; the nearest if all are roofed) */
	polygon = candidates[0];
	for (index = 0; index < candidate_count; index++)
	{
		struct sn_point center = resource->polygons[candidates[index]].center;
		real_point3d point;

		point.x = center.x;
		point.y = center.y;
		point.z = center.z;
		if (bot_open_sky(&point))
		{
			polygon = candidates[index];
			break;
		}
	}
	place->x = resource->polygons[polygon].center.x;
	place->y = resource->polygons[polygon].center.y;
	place->z = resource->polygons[polygon].center.z;
	*place_polygon = polygon;
	return TRUE;
}

/* port: where a driver with nobody to chase goes: a place under open sky
   between the two bases (the CTF flags, else the team starts), 30 to 75
   percent of the way from its own, either side of the middle. The spawn
   graph's random points it used before were mostly in the bases. */
static boolean bot_vehicle_field_goal(
	struct bot *bot,
	struct unit_datum const *vehicle,
	real_point3d *goal)
{
	struct player_datum const *player = bot_player(bot);
	long now = game_time_get();
	long team_index;
	real_point3d home;
	real_point3d enemy;
	real_point3d desired;
	real forward_x;
	real forward_y;
	real length;
	real progress;
	real lateral;
	uint32_t polygon;

	if (bot->vehicle_goal_valid && now - bot->vehicle_goal_time < BOT_VEHICLE_FIELD_GOAL_TICKS &&
		bot_horizontal_distance(&vehicle->object.position, &bot->vehicle_goal) > 10.f)
	{
		*goal = bot->vehicle_goal;
		return TRUE;
	}
	bot->vehicle_goal_valid = FALSE;
	/* (free for all: either team's bases bound the same field) */
	team_index = player && game_engine_has_teams() ? player->team_index : 0;
	if (!bot_navigation_team_bases(team_index, &home, &enemy))
		return FALSE;
	forward_x = enemy.x - home.x;
	forward_y = enemy.y - home.y;
	length = square_root(forward_x * forward_x + forward_y * forward_y);
	if (!(length >= 10.f))
		return FALSE;
	forward_x /= length;
	forward_y /= length;
	progress = bot_random_range(bot, 0.3f, 0.75f);
	lateral = bot_random_range(bot, -0.25f, 0.25f) * length;
	desired.x = home.x + forward_x * progress * length - forward_y * lateral;
	desired.y = home.y + forward_y * progress * length + forward_x * lateral;
	desired.z = home.z + (enemy.z - home.z) * progress;
	if (!bot_vehicle_place(vehicle, &desired, NULL, NULL, 0, &bot->vehicle_goal, &polygon))
		return FALSE;
	bot->vehicle_goal_valid = TRUE;
	bot->vehicle_goal_time = now;
	platform_log("bots: bot %d drives to the field at (%.1f %.1f %.1f), %.0f%% of the way to the enemy base",
		bot->slot + 1, bot->vehicle_goal.x, bot->vehicle_goal.y, bot->vehicle_goal.z, progress * 100.f);
	*goal = bot->vehicle_goal;
	return TRUE;
}

/* port: where a driver taking itself (and its riders) to an objective stops:
   the drivable place under open sky nearest a point BOT_VEHICLE_DROP_OFF_BACK
   short of it on the way from the vehicle, no nearer it than
   BOT_VEHICLE_DROP_OFF_CLEARANCE (a flag in a base is reached on foot from
   there: the vehicle stays outside). A place no route was found to is not chosen again
   for this objective; after BOT_VEHICLE_DROP_OFF_TRIES of them, FALSE. */
static boolean bot_vehicle_drop_off(
	struct bot *bot,
	struct unit_datum const *vehicle,
	real_point3d const *objective,
	real_point3d *goal)
{
	if (bot->vehicle_drop_off_valid &&
		bot_horizontal_distance(&bot->vehicle_drop_off_objective, objective) <= 5.f)
	{
		*goal = bot->vehicle_drop_off;
		return TRUE;
	}
	if (bot_horizontal_distance(&bot->vehicle_drop_off_objective, objective) > 5.f)
		bot->vehicle_drop_off_failures = 0;
	bot->vehicle_drop_off_valid = FALSE;
	bot->vehicle_drop_off_objective = *objective;
	{
		real_point3d desired = *objective;
		real dx = vehicle->object.position.x - objective->x;
		real dy = vehicle->object.position.y - objective->y;
		real length = square_root(dx * dx + dy * dy);

		if (length > BOT_VEHICLE_DROP_OFF_BACK)
		{
			desired.x += dx / length * BOT_VEHICLE_DROP_OFF_BACK;
			desired.y += dy / length * BOT_VEHICLE_DROP_OFF_BACK;
		}
		if (bot->vehicle_drop_off_failures >= BOT_VEHICLE_DROP_OFF_TRIES ||
			!bot_vehicle_place(vehicle, &desired, objective, bot->vehicle_drop_off_failed,
				bot->vehicle_drop_off_failures, &bot->vehicle_drop_off, &bot->vehicle_drop_off_polygon))
			return FALSE;
	}
	bot->vehicle_drop_off_valid = TRUE;
	platform_log("bots: bot %d drives to (%.1f %.1f %.1f), %.1f from its objective", bot->slot + 1,
		bot->vehicle_drop_off.x, bot->vehicle_drop_off.y, bot->vehicle_drop_off.z,
		bot_horizontal_distance(&bot->vehicle_drop_off, objective));
	*goal = bot->vehicle_drop_off;
	return TRUE;
}

/* a drop-off no route was found to */
static void bot_vehicle_drop_off_failed(struct bot *bot)
{
	if (bot->vehicle_drop_off_valid && bot->vehicle_drop_off_failures < BOT_VEHICLE_DROP_OFF_TRIES)
		bot->vehicle_drop_off_failed[bot->vehicle_drop_off_failures++] = bot->vehicle_drop_off_polygon;
	bot->vehicle_drop_off_valid = FALSE;
}

/* port: every end of an item pursuit is logged with its claim's release, so a
   log shows who held each item when (tools/check_bot_awareness_log.py) */
static void bot_drop_opportunity(struct bot *bot, char const *reason)
{
	if (bot->opportunity_index == NONE)
		return;
	platform_log("bots: bot %d team %ld gave up %s %ld (%s)", bot->slot + 1,
		bot->opportunity_team_index,
		bot->opportunity_vehicle ? "vehicle" : (bot->opportunity_powerup ? "powerup" : "weapon"),
		bot->opportunity_index, reason);
	if (bot->opportunity_vehicle)
		bot_manager_release_seat(bot->opportunity_team_index, bot->opportunity_index,
			bot->opportunity_seat, (short)(bot->slot + 1));
	else
		bot_manager_release_item(bot->opportunity_team_index, bot->opportunity_index,
			(short)(bot->slot + 1));
	bot->opportunity_index = NONE;
	bot->opportunity_team_index = NONE;
}

/* port: an item's claim lasts while the bot still means to get it, even
   while another behavior (a fight) runs, and ends when the pursuit times out.
   A claim lost to another bot (only after an expiry) ends the pursuit. */
static void bot_maintain_opportunity(struct bot *bot)
{
	long now = game_time_get();

	struct object_datum *object;

	if (bot->opportunity_index == NONE)
		return;
	/* port: a vehicle's seat is checked and claimed again each tick: taken
	by someone, or by a teammate's claim, the bot takes another seat or gives
	up the vehicle (before, every bot kept running for the driver's seat) */
	if (bot->opportunity_vehicle)
	{
		short seat;

		object = object_try_and_get(bot->opportunity_index);
		seat = object ? bot_vehicle_seat(bot, bot->opportunity_index) : NONE;
		if (!object)
			bot_drop_opportunity(bot, "taken or gone");
		else if (now >= bot->opportunity_end_time)
			bot_drop_opportunity(bot, "timed out");
		else if (!bot_opportunity_relevant(bot))
			bot_drop_opportunity(bot, "no longer relevant");
		else if (seat == NONE)
			bot_drop_opportunity(bot, "no seat left");
		else
		{
			if (seat != bot->opportunity_seat)
			{
				bot_manager_release_seat(bot->opportunity_team_index, bot->opportunity_index,
					bot->opportunity_seat, (short)(bot->slot + 1));
				bot->opportunity_seat = seat;
			}
			if (bot_manager_claim_seat(bot->opportunity_team_index, bot->opportunity_index, seat,
				(short)(bot->slot + 1), BOT_SEAT_CLAIM_TICKS))
				return;
			bot_drop_opportunity(bot, "seat claimed by another bot");
		}
		bot->opportunity_scan_time = now + bot_seconds_to_ticks(3.f);
		return;
	}
	object = object_try_and_get(bot->opportunity_index);
	if (!object || object->object.parent_object_index != NONE)
		bot_drop_opportunity(bot, "taken or gone");
	else if (now >= bot->opportunity_end_time)
		bot_drop_opportunity(bot, "timed out");
	else if (!bot_opportunity_relevant(bot))
		bot_drop_opportunity(bot, "no longer relevant");
	else if (!bot_manager_claim_item(bot->opportunity_team_index, bot->opportunity_index,
		(short)(bot->slot + 1), BOT_ITEM_CLAIM_TICKS))
		bot_drop_opportunity(bot, "claimed by another bot");
	else
		return;
	bot->opportunity_scan_time = now + bot_seconds_to_ticks(3.f);
}

static void bot_observe_inventory(struct bot *bot, struct unit_datum const *unit)
{
	short slot;
	long vehicle_index = unit->object.parent_object_index;

	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		long weapon_index = unit->unit.weapon_object_indices[slot];
		if (weapon_index != bot->observed_weapons[slot])
		{
			if (weapon_index != NONE)
				platform_log("bots: bot %d inventory slot %d now weapon %lx", bot->slot + 1,
					slot, (unsigned long)weapon_get(weapon_index)->definition_index & 0xffff);
			bot->observed_weapons[slot] = weapon_index;
		}
	}
	if (vehicle_index != bot->observed_vehicle_index)
	{
		platform_log("bots: bot %d %s vehicle %ld seat %d", bot->slot + 1,
			vehicle_index == NONE ? "left" : "entered", (long)vehicle_index,
			(short)unit->unit.parent_seat_index);
		bot->observed_vehicle_index = vehicle_index;
		bot->vehicle_progress_time = NONE;
		bot->vehicle_wait_until = NONE;
		bot->vehicle_wait_logged = FALSE;
		bot->vehicle_goal_valid = FALSE;
		/* (a ride taken, or a vehicle left: offers and pickups end) */
		bot->ride_vehicle_index = NONE;
		bot->pickup_player_index = NONE;
		bot->vehicle_drop_off_valid = FALSE;
		bot->vehicle_drop_off_failures = 0;
		vehicle_navigation_release((short)(bot->slot + 1));
		/* (a pickup's time on foot starts again after a ride) */
		bot->objective_deadline = game_time_get() + 60 * TICKS_PER_SECOND;
		bot->goal_node = ENGINE_AI_NAV_NODES_MAX;
		bot_drop_opportunity(bot, vehicle_index == NONE ? "left a vehicle" : "entered a vehicle");
	}
}

static boolean bot_friendly_driver(struct bot *bot, long vehicle_index)
{
	struct unit_datum *vehicle = vehicle_try_and_get(vehicle_index);
	struct unit_datum *driver;
	struct player_datum const *player = bot_player(bot);
	struct player_datum const *driver_player;

	if (!player || !game_engine_has_teams() || !vehicle || vehicle->unit.driver_object_index == NONE)
		return FALSE;
	driver = bot_living_unit(vehicle->unit.driver_object_index);
	driver_player = driver && driver->unit.player_index != NONE ?
		player_try_and_get(driver->unit.player_index) : NULL;
	return driver_player && driver_player->team_index == player->team_index;
}

/* port: powerups worth a detour: overshield, camouflage, speed and vision
   always; health only when hurt. Grenade pickups are left to chance. */
static boolean bot_wants_powerup(struct bot *bot, long equipment_index)
{
	struct item_datum *item = equipment_try_and_get(equipment_index);
	struct unit_datum *unit = bot_living_unit(bot->unit_index);
	struct equipment_definition *definition;

	if (!item || !unit || item->object.parent_object_index != NONE)
		return FALSE;
	definition = equipment_definition_get(item->definition_index);
	switch (definition->equipment.powerup_type)
	{
	case _equipment_powerup_overshield:
	case _equipment_powerup_active_camouflage:
	case _equipment_powerup_double_speed:
	case _equipment_powerup_full_spectrum_vision:
		return TRUE;
	case _equipment_powerup_health:
		return unit->object.body_vitality < 0.7f;
	default:
		return FALSE;
	}
}

/* port: weapon awareness without omniscience. A bot knows an item it can see
   now (a bounded number of sight checks per scan), one it saw within the last
   thirty seconds, or one within arm's reach. It does not know about items it
   has never seen, and a remembered item that has since been taken is simply
   gone when the bot arrives. */
static boolean bot_item_known(
	struct bot *bot,
	long object_index,
	real_point3d const *eye,
	real_point3d *point,
	real distance,
	short *sight_checks)
{
	long now = game_time_get();
	real_point3d raised = *point;
	short index;
	short known = NONE;
	short empty = NONE;
	short oldest = 0;

	for (index = 0; index < BOT_KNOWN_ITEMS; index++)
	{
		if (bot->known_items[index].object_index == object_index &&
			now >= bot->known_items[index].seen_time &&
			now - bot->known_items[index].seen_time <= BOT_KNOWN_ITEM_MEMORY_TICKS)
		{
			known = index;
			break;
		}
		if (bot->known_items[index].object_index == NONE ||
			now < bot->known_items[index].seen_time ||
			now - bot->known_items[index].seen_time > BOT_KNOWN_ITEM_MEMORY_TICKS)
		{
			if (empty == NONE)
				empty = index;
		}
		else if (bot->known_items[index].seen_time < bot->known_items[oldest].seen_time)
			oldest = index;
	}
	if (distance <= BOT_ITEM_NEAR_DISTANCE)
	{
		index = known != NONE ? known : (empty != NONE ? empty : oldest);
		bot->known_items[index].object_index = object_index;
		bot->known_items[index].seen_time = now;
		bot->known_items[index].position = *point;
		return TRUE;
	}
	if (*sight_checks < BOT_ITEM_SIGHT_CHECKS)
	{
		raised.z += 0.2f;
		(*sight_checks)++;
		if (bot_can_see(bot, bot->unit_index, eye, &raised))
		{
			index = known != NONE ? known : (empty != NONE ? empty : oldest);
			bot->known_items[index].object_index = object_index;
			bot->known_items[index].seen_time = now;
			bot->known_items[index].position = *point;
			return TRUE;
		}
	}
	if (known != NONE)
	{
		*point = bot->known_items[known].position;
		return TRUE;
	}
	return FALSE;
}

/* Scan once a second, staggered by slot. Nearby weapons and powerups the bot
   knows of (bot_item_known) and empty vehicle or turret seats are candidates;
   BSP routes decide if a wall or other obstacle makes them unreachable. Only
   one bot per team goes for an item (bot_manager_claim_item); enemy-team bots
   have independent claims and a human can still race either bot there. */
static void bot_find_opportunity(struct bot *bot, boolean vehicles_only)
{
	struct player_datum *player = bot_player(bot);
	struct object_iterator iterator;
	real_point3d origin;
	real_point3d eye;
	real best_score = 0.f;
	short examined = 0;
	short sight_checks = 0;
	short bot_number = (short)(bot->slot + 1);

	if (!player || bot->opportunity_index != NONE || game_time_get() < bot->opportunity_scan_time ||
		bot_living_unit(bot->unit_index)->object.parent_object_index != NONE)
		return;
	bot->opportunity_scan_time = game_time_get() + TICKS_PER_SECOND + bot->slot * 3;
	bot->opportunity_index = NONE;
	object_get_origin(bot->unit_index, &origin);
	unit_get_camera_position(bot->unit_index, &eye);
	object_iterator_new(&iterator, _object_mask_weapon | _object_mask_vehicle | _object_mask_equipment, 0);
	while (examined++ < 256 && object_iterator_next(&iterator))
	{
		struct object_datum *object = object_get(iterator.index);
		real_point3d point;
		real distance;
		real score;
		short seat = NONE;
		boolean is_vehicle = object->object.type == _object_type_vehicle;
		boolean is_powerup = object->object.type == _object_type_equipment;

		if (vehicles_only && !is_vehicle)
			continue;

		object_get_origin(iterator.index, &point);
		distance = distance3d(&origin, &point);
		if (distance > (is_vehicle ? (real)BOT_VEHICLE_SEEK_RANGE : (real)BOT_WEAPON_SEEK_RANGE) ||
			object->object.parent_object_index != NONE)
			continue;
		if (bot->target_visible)
		{
			if (is_vehicle && !(distance < 8.f && bot_friendly_driver(bot, iterator.index)))
				continue;
			if (is_powerup && distance > 8.f)
				continue;
			if (!is_vehicle && !is_powerup && distance > 3.f)
				continue;
		}
		if (is_vehicle)
		{
			if (iterator.index == bot->unreachable_item_index &&
				game_time_get() < bot->unreachable_item_until)
				continue;
			seat = bot_vehicle_seat(bot, iterator.index);
			if (seat == NONE || !unit_get_seat_entrance_point(bot->unit_index,
				iterator.index, seat, &point, NULL, NULL))
				continue;
		}
		else
		{
			if (is_powerup ? !bot_wants_powerup(bot, iterator.index) :
				!bot_wants_weapon(bot, iterator.index))
				continue;
			if (bot_manager_item_claimed_by_other(player->team_index, iterator.index, bot_number))
				continue;
			if (iterator.index == bot->unreachable_item_index &&
				game_time_get() < bot->unreachable_item_until)
				continue;
			if (!bot_item_known(bot, iterator.index, &eye, &point, distance, &sight_checks))
				continue;
			distance = distance3d(&origin, &point);
			if (distance > (real)BOT_WEAPON_SEEK_RANGE)
				continue;
		}
		score = (is_vehicle ? 20.f : (is_powerup ? 6.f : 4.f)) / (1.f + distance);
		if (score <= best_score)
			continue;
		best_score = score;
		bot->opportunity_index = iterator.index;
		bot->opportunity_team_index = player->team_index;
		bot->opportunity_vehicle = is_vehicle;
		bot->opportunity_powerup = is_powerup;
		bot->opportunity_position = point;
		bot->opportunity_seat = seat;
		bot->opportunity_end_time = game_time_get() + bot_seconds_to_ticks(is_vehicle ? 35.f : 12.f);
	}
	if (bot->opportunity_index != NONE && (bot->opportunity_vehicle ?
		!bot_manager_claim_seat(bot->opportunity_team_index, bot->opportunity_index,
			bot->opportunity_seat, bot_number, BOT_SEAT_CLAIM_TICKS) :
		!bot_manager_claim_item(bot->opportunity_team_index, bot->opportunity_index,
			bot_number, BOT_ITEM_CLAIM_TICKS)))
	{
		bot->opportunity_index = NONE;
		bot->opportunity_team_index = NONE;
	}
	if (bot->opportunity_index != NONE)
	{
		bot->decide_now = TRUE;
		platform_log("bots: bot %d team %ld seeking %s %ld", bot_number, bot->opportunity_team_index,
			bot->opportunity_vehicle ? "vehicle" : (bot->opportunity_powerup ? "powerup" : "weapon"),
			bot->opportunity_index);
	}
}

static boolean bot_opportunity_relevant(struct bot *bot)
{
	struct object_datum *object = object_try_and_get(bot->opportunity_index);
	real_point3d origin;

	if (!object)
		return FALSE;
	if (!bot->target_visible)
		return TRUE;
	object_get_origin(bot->unit_index, &origin);
	if (bot->opportunity_vehicle)
		return bot_friendly_driver(bot, bot->opportunity_index) &&
			distance3d(&origin, &object->object.position) < 8.f;
	if (bot->opportunity_powerup)
		return distance3d(&origin, &bot->opportunity_position) < 8.f;
	return distance3d(&origin, &bot->opportunity_position) < 3.f;
}

/* port: CTF flags force ordinary auto-pickup, including friendly touch-return.
No special pickup intent, ownership write or score write is necessary. */
static boolean bot_carries_flag(struct unit_datum const *unit)
{
	short slot;
	if (!unit) return FALSE;
	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		long index = unit->unit.weapon_object_indices[slot];
		if (weapon_try_and_get(index) && weapon_is_flag(index)) return TRUE;
	}
	return FALSE;
}

static void bot_objective_release(struct bot *bot)
{
	if (bot->objective_valid && (bot->objective.kind == _bot_ambition_take ||
		bot->objective.kind == _bot_ambition_return))
		bot_manager_release_item(bot->objective.acting_team_index,
			bot->objective.object_index, (short)(bot->slot + 1));
}

static void bot_update_objective(struct bot *bot, struct player_datum const *player)
{
	struct bot_ambition goal;
	boolean valid;
	long now = game_time_get();
	long excluded = now < bot->objective_retry_time ? bot->objective_unreachable_index : NONE;
	valid = !bots_globals.navigation_only &&
		game_engine_ctf_bot_variant_supported(game_engine_get_variant()) &&
		bot_manager_ctf_goal(player->team_index, bot->player_index,
			(short)(bot->slot + 1), excluded, &goal);
	if (valid != bot->objective_valid || (valid &&
		(goal.kind != bot->objective.kind || goal.acting_team_index != bot->objective.acting_team_index ||
		goal.owner_team_index != bot->objective.owner_team_index ||
		goal.object_index != bot->objective.object_index ||
		goal.carrier_player_index != bot->objective.carrier_player_index)))
	{
		bot_objective_release(bot);
		bot_navigation_reset(bot);
		bot->decide_now = TRUE;
		bot->objective_deadline = now + 60 * TICKS_PER_SECOND;
		if (valid)
			platform_log("bots: bot %d CTF goal %d object %ld", bot->slot + 1, goal.kind, goal.object_index);
	}
	bot->objective_valid = valid;
	if (valid) bot->objective = goal;
}

static engine_ai_behavior_result bot_leaf_objective(struct bot *bot,
	struct engine_ai_behavior_intent_emitter *emitter)
{
	struct bot_navigation_command command;
	struct sn_point goal;
	enum bot_navigation_result result;
	long now = game_time_get();
	boolean pickup;
	if (!bot->objective_valid) return ENGINE_AI_BEHAVIOR_FAILURE;
	pickup = bot->objective.kind == _bot_ambition_take || bot->objective.kind == _bot_ambition_return;
	bot->abs_suppressed = bot->objective.kind == _bot_ambition_deliver;
	/* Backoff belongs to the failed duty, not every subsequent objective.
	   In particular, a guard or new carrier must not inherit pickup's wait. */
	if (now < bot->objective_retry_time && !pickup &&
		bot->objective.kind == bot->objective_retry_kind &&
		bot->objective.object_index == bot->objective_unreachable_index)
	{
		bot->objective_waiting = TRUE;
		return ENGINE_AI_BEHAVIOR_RUNNING;
	}
	goal.x = bot->objective.position.x;
	goal.y = bot->objective.position.y;
	goal.z = bot->objective.position.z;
	result = bot_navigation_agent_move_to(&bot->navigation_agent, bot->unit_index,
		goal, 1.f, bot->objective.radius, &command);
	if (result == BOT_NAVIGATION_FAILED || (pickup && now >= bot->objective_deadline))
	{
		bot_objective_release(bot);
		bot->objective_unreachable_index = bot->objective.object_index;
		bot->objective_retry_kind = bot->objective.kind;
		/* Pickups yield their claim for 15 s. Carriers/searchers replan after
		   one second instead of standing helpless through a combat encounter. */
		bot->objective_retry_time = now + (pickup ? 15 : 1) * TICKS_PER_SECOND;
		bot_navigation_reset(bot);
		bot->objective_waiting = TRUE;
		return ENGINE_AI_BEHAVIOR_RUNNING;
	}
	/* Arrival does not complete an objective. Only next tick's engine state
	   can establish acquisition, friendly return or a scored capture. */
	bot->objective_waiting = result != BOT_NAVIGATION_MOVING;
	if (command.jump)
		engine_ai_behavior_intent_emit(emitter, _bot_intent_jump, BOT_INTENT_SCHEMA, NULL, 0);
	if (result == BOT_NAVIGATION_MOVING && command.has_move)
	{
		real_euler_angles2d look;
		look.yaw = command.yaw;
		look.pitch = 0.f;
		bot_emit_look(emitter, &look);
		bot_emit_move(emitter, command.yaw, command.speed);
	}
	return ENGINE_AI_BEHAVIOR_RUNNING;
}

static engine_ai_behavior_result bot_leaf_scavenge(struct bot *bot,
	struct engine_ai_behavior_intent_emitter *emitter)
{
	struct player_datum *player = bot_player(bot);
	struct object_datum *object = object_try_and_get(bot->opportunity_index);
	real_point3d origin;
	real_point3d point;
	struct sn_point nav_goal;
	struct bot_navigation_command command;
	enum bot_navigation_result navigation_result;
	long now = game_time_get();
	char const *reason = "taken or gone";

	if (bot->opportunity_index == NONE)
		return ENGINE_AI_BEHAVIOR_FAILURE;
	if (!object || now >= bot->opportunity_end_time || object->object.parent_object_index != NONE)
	{
		if (object && object->object.parent_object_index == NONE)
			reason = "timed out";
		goto abandon;
	}
	if (!bot_opportunity_relevant(bot))
	{
		reason = "no longer relevant";
		goto abandon;
	}
	if (bot->opportunity_vehicle)
	{
		if (bot_vehicle_seat(bot, bot->opportunity_index) == NONE ||
			!unit_get_seat_entrance_point(bot->unit_index, bot->opportunity_index,
			bot->opportunity_seat, &point, NULL, NULL))
			goto abandon;
	}
	else
	{
		reason = "no longer wanted";
		if (bot->opportunity_powerup ? !bot_wants_powerup(bot, bot->opportunity_index) :
			!bot_wants_weapon(bot, bot->opportunity_index))
			goto abandon;
		/* (the claim is renewed while it is pursued; lost to another bot, give up) */
		reason = "claimed by another bot";
		if (!bot_manager_claim_item(bot->opportunity_team_index, bot->opportunity_index,
			(short)(bot->slot + 1), BOT_ITEM_CLAIM_TICKS))
			goto abandon;
		point = bot->opportunity_position;
	}
	object_get_origin(bot->unit_index, &origin);
	/* (in reach, it faces the find, not the enemy) */
	bot->abs_suppressed = bot_horizontal_distance(&origin, &point) < 2.5f;
	nav_goal.x = point.x;
	nav_goal.y = point.y;
	nav_goal.z = point.z;
	navigation_result = bot_navigation_agent_move_to(&bot->navigation_agent,
		bot->unit_index, nav_goal, bot_horizontal_distance(&origin, &point) < 2.f ? 0.35f : 1.f,
		0.6f, &command);
	if (command.jump)
		engine_ai_behavior_intent_emit(emitter, _bot_intent_jump, BOT_INTENT_SCHEMA, NULL, 0);
	if (navigation_result == BOT_NAVIGATION_FAILED)
	{
		/* (no route there now: leave it alone for a while rather than retry
		the same unreachable find every scan) */
		reason = "unreachable";
		bot->unreachable_item_index = bot->opportunity_index;
		bot->unreachable_item_until = now + bot_seconds_to_ticks(15.f);
		goto abandon;
	}
	if (navigation_result == BOT_NAVIGATION_MOVING && command.has_move)
	{
		real_euler_angles2d look;
		look.yaw = command.yaw;
		look.pitch = 0.f;
		bot_emit_look(emitter, &look);
		bot_emit_move(emitter, command.yaw, command.speed);
	}
	/* (powerups are taken by walking over them; there is nothing to press) */
	if (!bot->opportunity_powerup && player->action_object_index == bot->opportunity_index &&
		now >= bot->interaction_time)
	{
		bot->interaction_time = now + TICKS_PER_SECOND;
		engine_ai_behavior_intent_emit(emitter, _bot_intent_interact, BOT_INTENT_SCHEMA, NULL, 0);
	}
	return ENGINE_AI_BEHAVIOR_RUNNING;
abandon:
	bot_drop_opportunity(bot, reason);
	bot->opportunity_scan_time = now + bot_seconds_to_ticks(3.f);
	return ENGINE_AI_BEHAVIOR_FAILURE;
}

static engine_ai_behavior_result bot_leaf_vehicle(struct bot *bot,
	struct engine_ai_behavior_intent_emitter *emitter)
{
	struct unit_datum *unit = bot_living_unit(bot->unit_index);
	struct unit_datum *vehicle;
	struct unit_definition *definition;
	struct unit_seat *seat;
	real_point3d eye;
	boolean drive_wanted = FALSE;
	long now = game_time_get();

	if (!unit || unit->object.parent_object_index == NONE)
		return ENGINE_AI_BEHAVIOR_FAILURE;
	vehicle = vehicle_try_and_get(unit->object.parent_object_index);
	if (!vehicle)
		return ENGINE_AI_BEHAVIOR_FAILURE;
	definition = unit_definition_get(vehicle->definition_index);
	if (unit->unit.parent_seat_index < 0 || unit->unit.parent_seat_index >= definition->unit.seats.count)
		return ENGINE_AI_BEHAVIOR_FAILURE;
	seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, unit->unit.parent_seat_index, struct unit_seat);
	unit_get_camera_position(bot->unit_index, &eye);
	if (bot->target_player_index != NONE &&
		(!TEST_FLAG(seat->flags, _unit_seat_driver_bit) || vehicle->unit.weapon_object_indices[0] != NONE))
		bot_engage(bot, &eye, emitter);
	if (TEST_FLAG(seat->flags, _unit_seat_driver_bit))
	{
		real_point3d goal;
		real_point3d destination;
		real_euler_angles2d look;
		struct vehicle_navigation_command drive;
		struct unit_datum *pickup;
		enum bot_navigation_result result;
		real arrival = 4.f;
		boolean has_destination;
		boolean unit_goal = FALSE;
		boolean objective_goal = FALSE;
		boolean chase_goal = FALSE;
		long vehicle_index = unit->object.parent_object_index;

		if (bot->vehicle_wait_until == NONE)
			bot->vehicle_wait_until = now + BOT_VEHICLE_BOARD_WAIT_TICKS;
		/* port: where the driver is going: its objective (CTF), else the field */
		objective_goal = bot_destination(bot, &destination);
		has_destination = objective_goal || bot_vehicle_field_goal(bot, vehicle, &destination);
		if (has_destination && bot->target_player_index == NONE)
			bot_choose_pickup(bot, vehicle_index, &destination);
		pickup = bot_pickup_unit(bot);
		if (bot->target_player_index != NONE && now >= bot->vehicle_chase_blocked_until)
		{
			/* (a target in a building: drive to the nearest open-sky place) */
			if (!bot->vehicle_chase_valid ||
				bot_horizontal_distance(&bot->vehicle_chase_target, &bot->target_last_position) > 4.f)
			{
				uint32_t polygon;

				bot->vehicle_chase_target = bot->target_last_position;
				bot->vehicle_chase_valid = bot_vehicle_place(vehicle, &bot->target_last_position,
					NULL, NULL, 0, &bot->vehicle_chase, &polygon);
			}
			goal = bot->vehicle_chase_valid ? bot->vehicle_chase : bot->target_last_position;
			unit_goal = !bot->vehicle_chase_valid;
			chase_goal = TRUE;
		}
		else if (bot_vehicle_should_wait(bot, vehicle_index))
		{
			/* (parked for boarding teammates: not a lack of progress) */
			if (!bot->vehicle_wait_logged)
				platform_log("bots: bot %d waits for teammates to board", bot->slot + 1);
			bot->vehicle_wait_logged = TRUE;
			bot->vehicle_progress_time = NONE;
			return ENGINE_AI_BEHAVIOR_RUNNING;
		}
		else if (pickup)
		{
			/* (to the teammate; beside them, or where they must walk to it,
			it waits for them to get in) */
			goal = pickup->object.position;
			arrival = BOT_RIDE_STOP_DISTANCE;
			unit_goal = TRUE;
			if (bot->pickup_meet ||
				bot_horizontal_distance(&vehicle->object.position, &goal) <= BOT_RIDE_STOP_DISTANCE + 1.f)
			{
				bot->vehicle_wait_until = now + BOT_RIDE_BOARD_WAIT_TICKS;
				bot->vehicle_progress_time = NONE;
				return ENGINE_AI_BEHAVIOR_RUNNING;
			}
		}
		else if (objective_goal)
		{
			/* (to the drivable place nearest it; none left to try: on foot) */
			if (!bot_vehicle_drop_off(bot, vehicle, &destination, &goal))
			{
				if (!bot->vehicle_wait_logged)
					platform_log("bots: bot %d cannot drive to its objective; getting out", bot->slot + 1);
				bot->vehicle_wait_logged = TRUE;
				engine_ai_behavior_intent_emit(emitter, _bot_intent_exit_vehicle, BOT_INTENT_SCHEMA, NULL, 0);
				return ENGINE_AI_BEHAVIOR_RUNNING;
			}
			arrival = 3.f;
		}
		else if (has_destination)
		{
			goal = destination;
			arrival = 8.f;
		}
		else
		{
			struct engine_ai_nav_graph const *graph = bot_navigation_graph();
			if (!graph || graph->node_count == 0)
				return ENGINE_AI_BEHAVIOR_RUNNING;
			if (bot->goal_node >= graph->node_count ||
				distance3d(&vehicle->object.position, &bots_globals.navigation.nodes[bot->goal_node]) < 6.f)
				engine_ai_rng32_bounded(&bot->rng, graph->node_count, &bot->goal_node);
			goal = bots_globals.navigation.nodes[bot->goal_node];
		}
		{
			struct sn_point nav_goal;

			nav_goal.x = goal.x;
			nav_goal.y = goal.y;
			nav_goal.z = goal.z;
			result = vehicle_navigation_drive((short)(bot->slot + 1), vehicle_index, nav_goal, arrival,
				!unit_goal, &drive);
		}
		if (result == BOT_NAVIGATION_ARRIVED)
		{
			/* (at its objective: out, and on foot from here; at a place in
			the field: another, next tick) */
			if (objective_goal && !unit_goal && !pickup)
			{
				if (!bot->vehicle_wait_logged)
					platform_log("bots: bot %d drove to its objective; getting out", bot->slot + 1);
				bot->vehicle_wait_logged = TRUE;
				engine_ai_behavior_intent_emit(emitter, _bot_intent_exit_vehicle, BOT_INTENT_SCHEMA, NULL, 0);
			}
			else if (!unit_goal)
				bot->vehicle_goal_valid = FALSE;
			bot->vehicle_progress_time = NONE;
			return ENGINE_AI_BEHAVIOR_RUNNING;
		}
		if (result == BOT_NAVIGATION_FAILED && chase_goal)
		{
			/* (no way to the target by vehicle: stop retrying, and resume
			ordinary driving while the gunner can still engage) */
			bot->vehicle_chase_valid = FALSE;
			bot->vehicle_chase_blocked_until = now + BOT_VEHICLE_CHASE_BACKOFF_TICKS;
			bot->vehicle_progress_time = NONE;
			return ENGINE_AI_BEHAVIOR_RUNNING;
		}
		if (result == BOT_NAVIGATION_FAILED)
		{
			/* (no way there by vehicle: another place in the field; a pickup
			or objective it cannot drive to is left to them on foot) */
			bot->vehicle_goal_valid = FALSE;
			if (pickup)
			{
				/* (they walk to it instead, for as long as that takes them,
				within the offer) */
				long walk = bot_seconds_to_ticks(bot_horizontal_distance(&vehicle->object.position,
					&pickup->object.position) / BOT_RIDE_WALK_SPEED + BOT_RIDE_BOARD_SECONDS);

				bot->pickup_meet = TRUE;
				bot->pickup_until = MIN(bot->pickup_until, now + walk);
				platform_log("bots: bot %d cannot drive to teammate %ld; waits for them",
					bot->slot + 1, (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(bot->pickup_player_index));
			}
			else if (objective_goal && !unit_goal)
				bot_vehicle_drop_off_failed(bot);
			bot->vehicle_progress_time = NONE;
			return ENGINE_AI_BEHAVIOR_RUNNING;
		}
		if (result == BOT_NAVIGATION_UNAVAILABLE)
		{
			/* (no vehicle layer: straight at the goal, at half throttle, as before) */
			look = bot_angles_to(&vehicle->object.position, &goal);
			look.pitch = 0.f;
			if (!bot->target_visible || vehicle->unit.weapon_object_indices[0] == NONE)
				bot_emit_look(emitter, &look);
			drive_wanted = bot_horizontal_distance(&vehicle->object.position, &goal) > 8.f &&
				!bot_vehicle_teammate_ahead(bot, vehicle, FALSE);
			if (drive_wanted)
				bot_emit_move(emitter, look.yaw, 0.5f);
		}
		else if (drive.has_drive)
		{
			/* (the vehicle steers toward where its driver looks; a gun on the
			driver's seat looks at a seen target instead, and the throttle is
			taken against that) */
			look.yaw = drive.yaw;
			look.pitch = 0.f;
			if (!bot->target_visible || vehicle->unit.weapon_object_indices[0] == NONE)
				bot_emit_look(emitter, &look);
			/* (a teammate in the way: brake, holding the wheel) */
			if (drive.throttle != 0.f && bot_vehicle_teammate_ahead(bot, vehicle, drive.throttle < 0.f))
				drive.throttle = drive.throttle > 0.f ? -0.5f : 0.5f;
			drive_wanted = drive.throttle != 0.f;
			if (drive.throttle > 0.f)
				bot_emit_move(emitter, drive.yaw, drive.throttle);
			else if (drive.throttle < 0.f)
				bot_emit_move(emitter, bot_angle_normalize(drive.yaw + _pi), -drive.throttle);
		}
		/* (a route under way or waiting has its own stall and deadline checks) */
		if (result != BOT_NAVIGATION_UNAVAILABLE)
			bot->vehicle_progress_time = NONE;
	}
	else if (bot->objective_valid && bot->objective.kind != _bot_ambition_guard &&
		bot_horizontal_distance(&vehicle->object.position, &bot->objective.position) <=
			BOT_VEHICLE_DISMOUNT_DISTANCE)
	{
		/* port: a rider gets out at its objective */
		engine_ai_behavior_intent_emit(emitter, _bot_intent_exit_vehicle, BOT_INTENT_SCHEMA, NULL, 0);
	}
	else if (vehicle_supports_bot_driver(unit->object.parent_object_index) &&
		vehicle->unit.driver_object_index == NONE &&
		magnitude_squared3d(&vehicle->object.translational_velocity) <= BOT_VEHICLE_SEAT_MAXIMUM_SPEED_SQUARED)
	{
		/* port: the driver got out (at its objective): so do its riders */
		engine_ai_behavior_intent_emit(emitter, _bot_intent_exit_vehicle, BOT_INTENT_SCHEMA, NULL, 0);
	}
	if (bot->vehicle_progress_time == NONE)
	{
		bot->vehicle_progress_time = now;
		bot->vehicle_progress_position = vehicle->object.position;
	}
	else if (now - bot->vehicle_progress_time >= bot_seconds_to_ticks(8.f))
	{
		if (vehicle->object.up.k < 0.5f || (drive_wanted &&
			distance3d(&vehicle->object.position, &bot->vehicle_progress_position) < 1.f) ||
			(!TEST_FLAG(seat->flags, _unit_seat_driver_bit) &&
			vehicle_supports_bot_driver(unit->object.parent_object_index) &&
			vehicle->unit.driver_object_index == NONE))
			engine_ai_behavior_intent_emit(emitter, _bot_intent_exit_vehicle, BOT_INTENT_SCHEMA, NULL, 0);
		bot->vehicle_progress_time = now;
		bot->vehicle_progress_position = vehicle->object.position;
	}
	return ENGINE_AI_BEHAVIOR_RUNNING;
}

static enum engine_ai_traversal_availability bot_traversal_availability(
	void *context,
	uint32_t traversal_id,
	uint32_t source_node,
	uint32_t destination_node)
{
	return ENGINE_AI_TRAVERSAL_AVAILABLE;
}

static enum engine_ai_traversal_reservation_result bot_traversal_reserve(
	void *context,
	uint32_t traversal_id,
	uint32_t source_node,
	uint32_t destination_node,
	uint64_t *reservation_id)
{
	*reservation_id = 1;
	return ENGINE_AI_TRAVERSAL_RESERVATION_GRANTED;
}

/* (the graph has ordinary links only so far; a special one is a jump) */
static enum engine_ai_traversal_execution_result bot_traversal_execute(
	void *context,
	uint32_t traversal_id,
	uint32_t source_node,
	uint32_t destination_node,
	uint64_t reservation_id)
{
	struct bot *bot = (struct bot *)context;

	bot->jump_requested = TRUE;
	return ENGINE_AI_TRAVERSAL_EXECUTION_COMPLETE;
}

static void bot_traversal_release(
	void *context,
	uint32_t traversal_id,
	uint64_t reservation_id,
	enum engine_ai_traversal_release_reason reason)
{
}

static struct engine_ai_traversal_callbacks const bot_traversal_callbacks =
{
	bot_traversal_availability,
	bot_traversal_reserve,
	bot_traversal_execute,
	bot_traversal_release,
};

/* walk the graph to a random far node; SUCCESS on arrival (the root then
picks another) */
static engine_ai_behavior_result bot_leaf_roam_spawn_graph(
	struct bot *bot,
	struct engine_ai_behavior_intent_emitter *emitter)
{
	struct engine_ai_nav_graph const *graph = bot_navigation_graph();
	long now = game_time_get();
	real_point3d origin;
	real_point3d const *target;
	uint32_t target_node;
	enum engine_ai_traversal_result result;
	real_euler_angles2d look;

	if (!graph)
		return ENGINE_AI_BEHAVIOR_FAILURE;
	object_get_origin(bot->unit_index, &origin);

	if (!bot->searching && !bot->following)
	{
		long start = bot_navigation_nearest_node(&origin);
		uint32_t attempt;

		if (start == NONE)
			return ENGINE_AI_BEHAVIOR_FAILURE;
		bot->goal_node = (uint32_t)start;
		for (attempt = 0; attempt < 8; attempt++)
		{
			uint32_t candidate = 0;

			engine_ai_rng32_bounded(&bot->rng, graph->node_count, &candidate);
			bot->goal_node = candidate;
			if (distance3d(&origin, &bots_globals.navigation.nodes[candidate]) > BOT_MINIMUM_ROAM_DISTANCE)
				break;
		}
		if (engine_ai_nav_begin(&bot->search, graph, (uint32_t)start, bot->goal_node, 0) != ENGINE_AI_NAV_RUNNING)
			return ENGINE_AI_BEHAVIOR_FAILURE;
		bot->searching = TRUE;
	}

	if (bot->searching)
	{
		enum engine_ai_nav_status status = engine_ai_nav_step(&bot->search, graph, BOT_NAV_EXPANSIONS_PER_TICK);

		if (status == ENGINE_AI_NAV_RUNNING)
			return ENGINE_AI_BEHAVIOR_RUNNING;
		bot->searching = FALSE;
		if (status == ENGINE_AI_NAV_FOUND)
		{
			struct engine_ai_nav_route route;

			if (engine_ai_nav_result(&bot->search, graph, &route) == ENGINE_AI_NAV_FOUND &&
				engine_ai_traversal_begin(&bot->traversal, graph, &route, 0))
			{
				bot->following = TRUE;
				bot->progress_time = NONE;
				bot->stuck_count = 0;
			}
		}
		if (!bot->following)
		{
			/* (no way there: another goal next time) */
			return ENGINE_AI_BEHAVIOR_FAILURE;
		}
	}

	if (!engine_ai_traversal_target_node(&bot->traversal, graph, &target_node))
	{
		/* (an empty route: there already) */
		bot_navigation_reset(bot);
		return ENGINE_AI_BEHAVIOR_SUCCESS;
	}
	target = &bots_globals.navigation.nodes[target_node];
	result = engine_ai_traversal_step(&bot->traversal, graph, graph->revision, 0,
		bot_horizontal_distance(&origin, target) < BOT_WAYPOINT_REACHED_DISTANCE,
		&bot_traversal_callbacks, bot);
	if (result == ENGINE_AI_TRAVERSAL_COMPLETED)
	{
		bot_navigation_reset(bot);
		return ENGINE_AI_BEHAVIOR_SUCCESS;
	}
	if (result != ENGINE_AI_TRAVERSAL_APPROACHING && result != ENGINE_AI_TRAVERSAL_EXECUTING &&
		result != ENGINE_AI_TRAVERSAL_WAITING)
	{
		bot_navigation_reset(bot);
		return ENGINE_AI_BEHAVIOR_FAILURE;
	}
	/* (the next waypoint, which may have changed) */
	if (engine_ai_traversal_target_node(&bot->traversal, graph, &target_node))
		target = &bots_globals.navigation.nodes[target_node];

	/* stuck: a jump, then another way */
	if (bot->progress_time == NONE)
	{
		bot->progress_time = now;
		bot->progress_position = origin;
	}
	else if (now - bot->progress_time >= BOT_PROGRESS_CHECK_TICKS)
	{
		if (distance3d(&origin, &bot->progress_position) < 0.5f)
		{
			bot->stuck_count++;
			bot->jump_requested = TRUE;
		}
		else
		{
			bot->stuck_count = 0;
		}
		bot->progress_time = now;
		bot->progress_position = origin;
		if (bot->stuck_count >= 3)
		{
			if (bot->traversal.next_link < bot->traversal.route.link_count)
				bot_navigation_disable_link(bot->traversal.route.link_indices[bot->traversal.next_link]);
			engine_ai_traversal_cancel(&bot->traversal, &bot_traversal_callbacks, bot);
			bot_navigation_reset(bot);
			return ENGINE_AI_BEHAVIOR_FAILURE;
		}
	}

	look.yaw = bot_yaw_to(&origin, target);
	look.pitch = 0.f;
	bot_emit_look(emitter, &look);
	bot_emit_move(emitter, look.yaw, 1.f);
	if (bot->jump_requested)
	{
		bot->jump_requested = FALSE;
		engine_ai_behavior_intent_emit(emitter, _bot_intent_jump, BOT_INTENT_SCHEMA, NULL, 0);
	}
	return ENGINE_AI_BEHAVIOR_RUNNING;
}

static engine_ai_behavior_result bot_leaf_roam(
	struct bot *bot,
	struct engine_ai_behavior_intent_emitter *emitter)
{
	struct bot_navigation_command command;
	enum bot_navigation_result navigation_result;
	long now = game_time_get();

	if (bot->surface_fallback_logged && navigation_world_resource())
	{
		bot_navigation_reset(bot);
		bot->surface_fallback_logged = FALSE;
	}
	navigation_result = bot_navigation_agent_roam(&bot->navigation_agent, bot->unit_index,
		&bot->rng, &command);
	if (command.jump)
		engine_ai_behavior_intent_emit(emitter, _bot_intent_jump, BOT_INTENT_SCHEMA, NULL, 0);
	if (navigation_result == BOT_NAVIGATION_UNAVAILABLE)
	{
		if (bot->surface_nav_wait_time == NONE)
			bot->surface_nav_wait_time = now;
		if (now - bot->surface_nav_wait_time < BOT_SURFACE_WAIT_TICKS)
			return ENGINE_AI_BEHAVIOR_RUNNING;
		if (!bot->surface_fallback_logged)
		{
			platform_log("bots: bot %d BSP navigation unavailable; using start graph fallback",
				bot->slot + 1);
			bot->surface_fallback_logged = TRUE;
		}
		return bot_leaf_roam_spawn_graph(bot, emitter);
	}

	bot->surface_nav_wait_time = NONE;
	if (navigation_result == BOT_NAVIGATION_FAILED)
		return ENGINE_AI_BEHAVIOR_FAILURE;
	if (navigation_result == BOT_NAVIGATION_ARRIVED)
		return ENGINE_AI_BEHAVIOR_SUCCESS;
	if (navigation_result == BOT_NAVIGATION_MOVING && command.has_move)
	{
		real_euler_angles2d look;
		look.yaw = command.yaw;
		look.pitch = 0.f;
		bot_emit_look(emitter, &look);
		bot_emit_move(emitter, command.yaw, command.speed);
	}
	return ENGINE_AI_BEHAVIOR_RUNNING;
}

static engine_ai_behavior_result bot_behavior_tick(
	void *context,
	uint32_t node,
	uint32_t leaf_id,
	struct engine_ai_behavior_intent_emitter *emitter)
{
	struct bot *bot = (struct bot *)context;
	engine_ai_behavior_result result;
	real_point3d eye;

	/* (only the selector's choice runs) */
	if (leaf_id != bot->chosen_leaf)
		return ENGINE_AI_BEHAVIOR_FAILURE;
	switch (leaf_id)
	{
	case _bot_leaf_retreat: result = bot_leaf_retreat(bot, emitter); break;
	case _bot_leaf_fight: result = bot_leaf_fight(bot, emitter); break;
	case _bot_leaf_roam: result = bot_leaf_roam(bot, emitter); break;
	case _bot_leaf_scavenge: result = bot_leaf_scavenge(bot, emitter); break;
	case _bot_leaf_vehicle: result = bot_leaf_vehicle(bot, emitter); break;
	case _bot_leaf_objective: result = bot_leaf_objective(bot, emitter); break;
	default: return ENGINE_AI_BEHAVIOR_ERROR;
	}

	/* Always Be Shooting: a bot doing anything else still aims and fires at
	an enemy it sees. Its move is the behavior's, in world space, so it walks
	to its goal while it shoots. (Fight, retreat and vehicle engage already.) */
	if (result == ENGINE_AI_BEHAVIOR_RUNNING && bot->target_visible && !bot->abs_suppressed &&
		!bots_globals.navigation_only && leaf_id != _bot_leaf_fight && leaf_id != _bot_leaf_retreat && leaf_id != _bot_leaf_vehicle)
	{
		unit_get_camera_position(bot->unit_index, &eye);
		bot_engage(bot, &eye, emitter);
	}
	return result;
}

static void bot_behavior_cancel(
	void *context,
	uint32_t node,
	uint32_t leaf_id)
{
	struct bot *bot = (struct bot *)context;

	if (leaf_id == _bot_leaf_roam && bot->following)
		engine_ai_traversal_cancel(&bot->traversal, &bot_traversal_callbacks, bot);
	bot_navigation_reset(bot);
	if (leaf_id == _bot_leaf_retreat)
		bot->retreat_start_time = NONE;
}

/* (side-effect free and cheap: whether the leaf could run now; between
decisions, a running leaf that cannot is replaced at once) */
static boolean bot_leaf_viable(
	struct bot *bot,
	uint32_t leaf_id)
{
	struct unit_datum *unit = bot_living_unit(bot->unit_index);
	boolean seated = unit && unit->object.parent_object_index != NONE;

	if (!unit)
		return FALSE;
	switch (leaf_id)
	{
	case _bot_leaf_vehicle: return seated;
	case _bot_leaf_objective: return !seated && bot->objective_valid;
	case _bot_leaf_scavenge: return !seated && bot->opportunity_index != NONE && bot_opportunity_relevant(bot);
	case _bot_leaf_retreat: return !seated && bot->target_player_index != NONE;
	case _bot_leaf_fight: return !seated && bot->target_player_index != NONE;
	case _bot_leaf_roam: return !seated;
	}
	return FALSE;
}

static bool bot_behavior_relevant(
	void *context,
	uint32_t node)
{
	return node < NUMBEROF(bot_behavior_nodes) &&
		bot_leaf_viable((struct bot *)context, bot_behavior_nodes[node].leaf_id);
}

/* ---------- private code: utility */

/* shields count double, as a player reads a fight; 0 to 1 */
static real bot_unit_vitality(
	struct unit_datum const *unit)
{
	real shield = PIN(unit->object.shield_vitality, 0.f, 1.f);
	real body = PIN(unit->object.body_vitality, 0.f, 1.f);

	return (2.f * shield + body) / 3.f;
}

static real bot_unit_weapon_utility(
	struct unit_datum const *unit)
{
	short slot = unit->unit.current_weapon_index;

	if (slot < 0 || slot >= MAXIMUM_WEAPONS_PER_UNIT)
		return 0.f;
	return bot_weapon_utility(unit->unit.weapon_object_indices[slot]);
}

/* How sure the bot is of winning a fight now, 0 to 1 (Halo Infinite's
confidence): its vitality against its target's, weapon against weapon,
teammates near against enemies in sight. The target's vitality is what a
player reads from its shields' flare and how it took its last hits. */
static real bot_confidence(
	struct bot *bot,
	struct player_datum const *player,
	struct unit_datum const *unit)
{
	struct bot_utility_tuning const *tuning = bots_globals.tuning;
	/* port: remembered/shared contacts carry no live health or weapon data. */
	struct player_datum *target = bot->target_visible && bot->target_player_index != NONE ?
		player_try_and_get(bot->target_player_index) : NULL;
	struct unit_datum *target_unit = target ? bot_living_unit(target->unit_index) : NULL;
	float inputs[4];
	real own_weapon = bot_unit_weapon_utility(unit);
	real target_weapon = target_unit ? bot_unit_weapon_utility(target_unit) : 0.f;
	real enemies = (real)MAX(bot->visible_enemy_count, bot->target_player_index != NONE ? 1 : 0);
	real teammates = 0.f;

	inputs[0] = bot_unit_vitality(unit);
	inputs[1] = target_unit ? 1.f - bot_unit_vitality(target_unit) : 0.5f;
	if (target_unit && own_weapon + target_weapon > 0.f)
		inputs[2] = own_weapon / (own_weapon + target_weapon);
	else
		inputs[2] = own_weapon > 0.f ? 0.5f : 0.f;
	if (game_engine_has_teams())
	{
		struct data_iterator iterator;
		struct player_datum *other;

		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			struct unit_datum *other_unit;

			if (iterator.datum_index == bot->player_index || other->team_index != player->team_index ||
				bot_scenario_ignores_player(iterator.datum_index))
				continue;
			other_unit = bot_living_unit(other->unit_index);
			if (other_unit && distance3d(&other_unit->object.position, &unit->object.position) < tuning->teammate_radius)
				teammates += 1.f;
		}
	}
	inputs[3] = (teammates + 1.f) / (teammates + 1.f + enemies);
	return engine_ai_utility_weighted(inputs, tuning->confidence_weights, 4);
}

/* every behavior's score, 0 to 1 (docs/bot_decisions.md, "Scores"); zero
for one that cannot run */
static void bot_score_behaviors(
	struct bot *bot,
	struct player_datum const *player,
	struct unit_datum const *unit)
{
	struct bot_utility_tuning const *tuning = bots_globals.tuning;
	long now = game_time_get();
	real confidence = bot_confidence(bot, player, unit);
	short leaf;

	bot->confidence = confidence;
	for (leaf = 0; leaf < BOT_BEHAVIOR_COUNT; leaf++)
		bot->scores[leaf] = 0.f;

	/* (seated: the vehicle leaf drives, guns or rides, and gets out) */
	if (unit->object.parent_object_index != NONE)
	{
		bot->scores[_bot_leaf_vehicle - 1] = tuning->vehicle;
		return;
	}

	bot->scores[_bot_leaf_roam - 1] = tuning->roam;
	/* port: an offered ride, the vehicle stopped for it, is taken: the
	driver offered it only when it saves the rider time (bot_choose_pickup) */
	if (bot->ride_vehicle_index != NONE && bot->opportunity_index == bot->ride_vehicle_index &&
		bot->opportunity_vehicle && !bot->target_visible && bot_opportunity_relevant(bot))
	{
		bot->scores[_bot_leaf_scavenge - 1] = 1.f;
		return;
	}
	/* port: a vehicle (or an offered ride) that takes the bot to its
	objective sooner is the duty's way there: getting in is scored as the
	objective is, and walking to the objective is not scored meanwhile */
	if (bot->objective_valid && bot->objective.kind != _bot_ambition_guard &&
		bot->opportunity_index != NONE && bot->opportunity_vehicle && bot_opportunity_relevant(bot))
	{
		bot->scores[_bot_leaf_scavenge - 1] = bot->objective.value;
		return;
	}
	/* port: flag duties beat distractions. Guards can still fight and seek
	   supplies, but carriers/returners/attackers keep their objective route. */
	if (bot->objective_valid)
	{
		bot->scores[_bot_leaf_objective - 1] = bot->objective.value;
		if (bot->objective.kind != _bot_ambition_guard) return;
	}
	/* (test fixture: lane patrols only, holding fire, so every route can be
	checked to arrival) */
	if (bots_globals.navigation_only)
		return;

	if (bot->target_player_index != NONE)
	{
		real known;
		real fight;
		real_point3d origin;

		switch (bot->target_source)
		{
		case _bot_target_seen: known = tuning->fight_seen; break;
		case _bot_target_attacker: known = tuning->fight_attacker; break;
		case _bot_target_remembered: known = tuning->fight_remembered; break;
		default: known = tuning->fight_shared_cap; break;
		}
		object_get_origin(bot->unit_index, &origin);
		fight = known *
			engine_ai_utility_curve_evaluate(&tuning->fight_distance, distance3d(&origin, &bot->target_last_position)) *
			engine_ai_utility_curve_evaluate(&tuning->fight_confidence, confidence);
		/* (a teammate's callout is worth a look, never more than the cap) */
		if (bot->target_source == _bot_target_shared)
			fight = MIN(fight, tuning->fight_shared_cap);
		bot->scores[_bot_leaf_fight - 1] = (float)fight;

		/* (a bot that knows its target can fall back; as its shields come back
		and its confidence with them, fight overtakes it, or the leaf ends it
		and a cooldown starts) */
		if (bots_globals.skill->retreats && bot->target_source != _bot_target_shared &&
			now >= bot->retreat_cooldown_time)
		{
			bot->scores[_bot_leaf_retreat - 1] = (float)MIN(tuning->retreat_cap, (1.f - confidence) *
				engine_ai_utility_curve_evaluate(&tuning->retreat_shields, unit->object.shield_vitality));
		}
	}

	if (bot->opportunity_index != NONE && bot_opportunity_relevant(bot))
	{
		real_point3d origin;
		real_point3d point;
		real distance;

		object_get_origin(bot->unit_index, &origin);
		if (bot->opportunity_vehicle)
		{
			if (!unit_get_seat_entrance_point(bot->unit_index, bot->opportunity_index,
				bot->opportunity_seat, &point, NULL, NULL))
				return;
		}
		else
			point = bot->opportunity_position;
		distance = distance3d(&origin, &point);
		/* Keep the score stable when an enemy leaves and re-enters view. Near
		the pickup, commit to it whether or not the enemy is currently visible;
		ABS still fights independently. */
		if (bot_horizontal_distance(&origin, &point) < 2.5f)
			bot->scores[_bot_leaf_scavenge - 1] = tuning->scavenge_in_reach;
		else
			bot->scores[_bot_leaf_scavenge - 1] = (float)(
				(bot->opportunity_vehicle ? tuning->scavenge_vehicle :
				(bot->opportunity_powerup ? tuning->scavenge_powerup : tuning->scavenge_weapon)) *
				engine_ai_utility_curve_evaluate(bot->opportunity_vehicle ?
					&tuning->scavenge_vehicle_distance : &tuning->scavenge_distance, distance));
	}
}

static engine_ai_behavior_result bot_behavior_tick_plain(
	void *context,
	uint32_t node,
	uint32_t leaf_id)
{
	return ENGINE_AI_BEHAVIOR_FAILURE;
}

/* ---------- private code: the executor */

/* an intent, checked against the live player and unit as a person's input
would be, into this tick's action */
static struct engine_ai_intent_result bot_execute_intent(
	void *context,
	struct engine_ai_intent const *intent)
{
	struct bot *bot = (struct bot *)context;
	struct engine_ai_intent_result result;
	struct player_datum *player = bot_player(bot);
	struct unit_datum *unit;

	result.outcome = ENGINE_AI_INTENT_REJECTED;
	result.reason = _bot_reject_none;
	if (!player || intent->id.actor.index != (uint32_t)DATUM_INDEX_TO_ABSOLUTE_INDEX(bot->player_index) ||
		intent->id.actor.generation != (uint32_t)(unsigned short)(bot->player_index >> 16))
	{
		result.reason = _bot_reject_no_player;
		return result;
	}
	unit = bot_living_unit(player->unit_index);
	if (!unit || player->unit_index != bot->unit_index || !unit_controllable(player->unit_index))
	{
		result.reason = _bot_reject_no_unit;
		return result;
	}
	if (intent->schema_version != BOT_INTENT_SCHEMA)
	{
		result.reason = _bot_reject_bad_payload;
		return result;
	}

	switch (intent->action_kind)
	{
	case _bot_intent_look:
	{
		struct bot_look_payload payload;

		if (intent->payload_size != sizeof(payload))
		{
			result.reason = _bot_reject_bad_payload;
			return result;
		}
		csmemcpy(&payload, intent->payload, sizeof(payload));
		if (!valid_real(payload.yaw) || !valid_real(payload.pitch))
		{
			result.reason = _bot_reject_bad_payload;
			return result;
		}
		bot->pending.has_look = TRUE;
		bot->pending.look.yaw = bot_angle_normalize(payload.yaw);
		bot->pending.look.pitch = PIN(payload.pitch, -1.4f, 1.4f);
		break;
	}
	case _bot_intent_move:
	{
		struct bot_move_payload payload;

		if (intent->payload_size != sizeof(payload))
		{
			result.reason = _bot_reject_bad_payload;
			return result;
		}
		csmemcpy(&payload, intent->payload, sizeof(payload));
		if (!valid_real(payload.yaw) || !valid_real(payload.speed))
		{
			result.reason = _bot_reject_bad_payload;
			return result;
		}
		bot->pending.has_move = TRUE;
		bot->pending.move_yaw = bot_angle_normalize(payload.yaw);
		bot->pending.move_speed = PIN(payload.speed, 0.f, 1.f);
		break;
	}
	case _bot_intent_fire:
	{
		struct bot_fire_payload payload;

		if (intent->payload_size != sizeof(payload))
		{
			result.reason = _bot_reject_bad_payload;
			return result;
		}
		csmemcpy(&payload, intent->payload, sizeof(payload));
		if (!valid_real(payload.trigger))
		{
			result.reason = _bot_reject_bad_payload;
			return result;
		}
		bot->pending.trigger = PIN(payload.trigger, 0.f, 1.f);
		break;
	}
	case _bot_intent_interact:
		/* Never turn an unrelated prompt into a pickup or vehicle action. */
		if (unit->object.parent_object_index != NONE ||
			player->action_object_index != bot->opportunity_index)
		{
			result.reason = _bot_reject_bad_payload;
			return result;
		}
		if (!bot->opportunity_vehicle &&
			(player->action_result == _player_action_result_swap_for_weapon ||
			player->action_result == _player_action_result_add_weapon_to_inventory))
			bot->pending.control_flags |= FLAG(_unit_control_swap_weapons_bit);
		else if (bot->opportunity_vehicle &&
			player->action_result == _player_action_result_enter_vehicle &&
			player->action_seat_index == bot->opportunity_seat)
			bot->pending.control_flags |= FLAG(_unit_control_action_bit) | FLAG(UNIT_CONTROL_PORT_ACTION_ONLY_BIT);
		else
		{
			result.reason = _bot_reject_bad_payload;
			return result;
		}
		break;
	case _bot_intent_exit_vehicle:
		if (unit->object.parent_object_index == NONE || !unit_try_and_exit_seat(bot->unit_index))
		{
			result.reason = _bot_reject_bad_payload;
			return result;
		}
		bot->opportunity_scan_time = game_time_get() + bot_seconds_to_ticks(8.f);
		break;
	case _bot_intent_melee:
		bot->pending.control_flags |= FLAG(_unit_control_use_equipment_bit);
		break;
	case _bot_intent_jump:
		bot->pending.control_flags |= FLAG(_unit_control_jump_bit);
		break;
	case _bot_intent_crouch:
		if (unit->object.parent_object_index != NONE)
		{
			result.reason = _bot_reject_bad_payload;
			return result;
		}
		bot->pending.control_flags |= FLAG(_unit_control_crouch_modifier_bit);
		break;
	case _bot_intent_grenade:
		if (unit->unit.current_grenade_index < 0 ||
			unit->unit.current_grenade_index >= NUMBER_OF_UNIT_GRENADE_TYPES ||
			unit->unit.grenade_counts[(short)unit->unit.current_grenade_index] <= 0)
		{
			result.reason = _bot_reject_no_grenades;
			return result;
		}
		bot->pending.control_flags |= FLAG(_unit_control_throw_grenade_bit);
		break;
	default:
		result.reason = _bot_reject_unknown;
		return result;
	}

	result.outcome = ENGINE_AI_INTENT_ACCEPTED;
	result.reason = _bot_reject_none;
	return result;
}

/* port: whether `point` lies within `radius` of the lane from start to end,
the radius growing by `spread` per unit of the lane's length. A point behind
start is not in it (shots leave forward); a lane of no length is the sphere
about start. Non-finite input fails closed (TRUE). Double intermediates keep
the products of finite float coordinates in range. */
static boolean bot_lane_near(
	real_point3d const *start,
	real_point3d const *end,
	real_point3d const *point,
	real radius,
	real spread)
{
	double dx;
	double dy;
	double dz;
	double px;
	double py;
	double pz;
	double length_squared;
	double along = 0.0;
	double allowed;

	if (!start || !end || !point || !valid_real(radius) || radius < 0.f ||
		!valid_real(spread) || spread < 0.f ||
		!valid_real(start->x) || !valid_real(start->y) || !valid_real(start->z) ||
		!valid_real(end->x) || !valid_real(end->y) || !valid_real(end->z) ||
		!valid_real(point->x) || !valid_real(point->y) || !valid_real(point->z))
		return TRUE;
	dx = (double)end->x - start->x;
	dy = (double)end->y - start->y;
	dz = (double)end->z - start->z;
	px = (double)point->x - start->x;
	py = (double)point->y - start->y;
	pz = (double)point->z - start->z;
	length_squared = dx * dx + dy * dy + dz * dz;
	if (length_squared > 0.0)
	{
		/* (the fraction of the lane at the point's foot) */
		along = (px * dx + py * dy + pz * dz) / length_squared;
		if (along < 0.0)
			return FALSE;
		along = MIN(along, 1.0);
		px -= along * dx;
		py -= along * dy;
		pz -= along * dz;
	}
	allowed = (double)radius + (double)spread * along * sqrt(length_squared);
	return px * px + py * py + pz * pz <= allowed * allowed;
}

/* port: whether a teammate's body touches the lane: its collision pill,
sampled at the pill's lower, middle and upper sphere centers (less than a
radius apart), each widened by the margin; any other unit, its bounding
sphere. */
static boolean bot_lane_near_unit(
	real_point3d const *start,
	real_point3d const *end,
	long unit_index,
	struct unit_datum const *unit,
	real margin,
	real spread)
{
	real_point3d base;
	real height;
	real radius;
	short sample;

	if (unit->object.type != _object_type_biped)
		return bot_lane_near(start, end, &unit->object.bounding_sphere_center,
			unit->object.bounding_sphere_radius + margin, spread);
	biped_get_physics_pill(unit_index, &base, &height, &radius);
	if (!valid_real(height) || height < 0.f || height > 10.f)
		return TRUE;
	for (sample = 0; sample < 3; sample++)
	{
		real_point3d point = base;

		point.z += height * 0.5f * (real)sample;
		if (bot_lane_near(start, end, &point, radius + margin, spread))
			return TRUE;
	}
	return FALSE;
}

/* port: whether a grenade going off at `aim` or `target` would reach a unit,
where it is now or anywhere on its way to where its present velocity (its
vehicle's, riding) takes it within the fuse: a teammate running to the fight
meets the blast there. Its bounding sphere, widened by the blast radius. A
missing or implausible velocity fails closed (TRUE). */
static boolean bot_blast_reaches(
	real_point3d const *aim,
	real_point3d const *target,
	long unit_index,
	struct unit_datum const *unit)
{
	real_point3d const *center = &unit->object.bounding_sphere_center;
	real radius = unit->object.bounding_sphere_radius + BOT_GRENADE_SAFETY_RADIUS;
	struct object_datum const *mover = object_try_and_get(unit->object.parent_object_index != NONE ?
		object_get_ultimate_parent(unit_index) : unit_index);
	real_vector3d velocity;
	real_point3d later;

	if (!mover)
		return TRUE;
	velocity = mover->object.translational_velocity;
	if (!valid_real(velocity.i) || !valid_real(velocity.j) || !valid_real(velocity.k) ||
		(real)fabs(velocity.i) > BOT_MAXIMUM_SPEED_PER_TICK ||
		(real)fabs(velocity.j) > BOT_MAXIMUM_SPEED_PER_TICK ||
		(real)fabs(velocity.k) > BOT_MAXIMUM_SPEED_PER_TICK)
		return TRUE;
	later.x = center->x + velocity.i * BOT_GRENADE_FUSE_TICKS;
	later.y = center->y + velocity.j * BOT_GRENADE_FUSE_TICKS;
	later.z = center->z + velocity.k * BOT_GRENADE_FUSE_TICKS;
	/* (now; then on its way, a burst behind it being nearest now) */
	return bot_lane_near(aim, aim, center, radius, 0.f) ||
		bot_lane_near(target, target, center, radius, 0.f) ||
		bot_lane_near(center, &later, aim, radius, 0.f) ||
		bot_lane_near(center, &later, target, radius, 0.f);
}

/* port: how far a trigger's shots stray from the aim, per unit of range: the
tangent of the widest cone its projectiles leave in (the upper error bound
that sustained fire ramps to, or the inner cone if wider, plus the pellets'
distribution: weapon_trigger_fire), never less than BOT_FIRE_LANE_SPREAD. The
angles are a map's tags: a value that is not finite and positive counts as
none, and the sum is held to BOT_FIRE_LANE_MAXIMUM_ERROR. */
static real bot_trigger_spread(
	struct weapon_trigger_definition const *trigger)
{
	real error = 0.f;
	real distribution = 0.f;
	real angle;

	if (valid_real(trigger->projectile_error_angle_upper_bound) && trigger->projectile_error_angle_upper_bound > 0.f)
		error = trigger->projectile_error_angle_upper_bound;
	if (valid_real(trigger->projectile_error_angle_lower_bound) && trigger->projectile_error_angle_lower_bound > error)
		error = trigger->projectile_error_angle_lower_bound;
	if (valid_real(trigger->projectile_error_inner_cone_angle) && trigger->projectile_error_inner_cone_angle > error)
		error = trigger->projectile_error_inner_cone_angle;
	if (valid_real(trigger->projectile_distribution_angle) && trigger->projectile_distribution_angle > 0.f)
		distribution = trigger->projectile_distribution_angle;
	angle = MIN(MIN(error, BOT_FIRE_LANE_MAXIMUM_ERROR) + MIN(distribution, BOT_FIRE_LANE_MAXIMUM_ERROR),
		BOT_FIRE_LANE_MAXIMUM_ERROR);
	return MAX(BOT_FIRE_LANE_SPREAD, sine(angle) / cosine(angle));
}

/* port: friendly-fire safety for the attack this tick's action would make,
along the facing actually submitted (after turn speed and aim error), not the
direction to the enemy. A shot's lane runs the skill's fire range and widens
with it by `spread` per unit (bot_trigger_spread: the weapon's own cone); a
teammate in it holds fire, except one riding in the bot's own
vehicle, which its shots pass (a projectile ignores its owner's ultimate
parent: projectiles.c). A grenade is also refused when the bot is within its
blast radius of where it is aimed or of the target, when a teammate is or
will be within it before the fuse runs out (bot_blast_reaches), or when a
surface stands within that radius in front of the bot. Conservative geometry,
not projectile flight: no ricochets, blast tags, homing, or turns a teammate
has yet to make. At most every player once (the player table's capacity,
checked). */
static boolean bot_attack_safe(
	struct bot *bot,
	boolean grenade,
	real spread)
{
	struct player_datum const *player = bot_player(bot);
	struct unit_datum const *unit = bot_living_unit(bot->unit_index);
	struct data_iterator iterator;
	real_point3d eye;
	real_point3d end;
	real_vector3d direction;
	real range;
	real reach;
	long vehicle_index = NONE;
	short examined;

	if (!player || !unit || !valid_real(bot->facing.yaw) || !valid_real(bot->facing.pitch) ||
		!valid_real(spread) || spread < 0.f)
		return FALSE;
	if (!grenade && !game_engine_has_teams())
		return TRUE;
	unit_get_camera_position(bot->unit_index, &eye);
	range = grenade ? distance3d(&eye, &bot->target_last_position) : bots_globals.skill->fire_range;
	if (!valid_real(range) || range <= 0.f || range > 1000.f)
		return FALSE;
	direction.i = cosine(bot->facing.pitch) * cosine(bot->facing.yaw);
	direction.j = cosine(bot->facing.pitch) * sine(bot->facing.yaw);
	direction.k = sine(bot->facing.pitch);
	end.x = eye.x + direction.i * range;
	end.y = eye.y + direction.j * range;
	end.z = eye.z + direction.k * range;
	if (grenade)
	{
		real_point3d ahead;

		/* (its own blast, where it aims and where the target is) */
		if (bot_lane_near(&end, &end, &unit->object.bounding_sphere_center,
			unit->object.bounding_sphere_radius + BOT_GRENADE_SAFETY_RADIUS, 0.f) ||
			bot_lane_near(&bot->target_last_position, &bot->target_last_position,
			&unit->object.bounding_sphere_center,
			unit->object.bounding_sphere_radius + BOT_GRENADE_SAFETY_RADIUS, 0.f))
			return FALSE;
		/* (a wall, rock or vehicle in its face would throw it back) */
		reach = MIN(range, BOT_GRENADE_SAFETY_RADIUS);
		ahead.x = eye.x + direction.i * reach;
		ahead.y = eye.y + direction.j * reach;
		ahead.z = eye.z + direction.k * reach;
		if (!bot_line_clear(_collision_test_for_line_of_sight_flags, &eye, &ahead, bot->unit_index))
			return FALSE;
	}
	if (!game_engine_has_teams())
		return TRUE;
	if (unit->object.parent_object_index != NONE)
		vehicle_index = object_get_ultimate_parent(bot->unit_index);
	data_iterator_new(&iterator, player_data);
	for (examined = 0; examined <= HALO_PORT_MAXIMUM_NETWORK_PLAYERS; examined++)
	{
		struct player_datum const *other = (struct player_datum *)data_iterator_next(&iterator);
		struct unit_datum const *other_unit;

		if (!other)
			return TRUE;
		if (iterator.datum_index == bot->player_index || other->team_index != player->team_index)
			continue;
		other_unit = bot_living_unit(other->unit_index);
		if (!other_unit)
			continue;
		if (grenade)
		{
			if (bot_blast_reaches(&end, &bot->target_last_position, other->unit_index, other_unit))
				return FALSE;
		}
		else if (vehicle_index != NONE && other_unit->object.parent_object_index != NONE &&
			object_get_ultimate_parent(other->unit_index) == vehicle_index)
		{
			continue;
		}
		if (bot_lane_near_unit(&eye, &end, other->unit_index, other_unit,
			BOT_FIRE_LANE_MARGIN, spread))
			return FALSE;
	}
	/* (more players than the table holds: not shown to be safe) */
	return FALSE;
}

/* port: a refused throw never reaches the player action; it is tried again
in a second rather than after the whole cooldown. */
static void bot_control_grenade(struct bot *bot, struct player_action *action)
{
	long retry;

	/* port: a carried flag is melee-only; no stale grenade intent survives. */
	if (bot_carries_flag(bot_living_unit(bot->unit_index)))
	{
		action->control_flags &= ~FLAG(_unit_control_throw_grenade_bit);
		return;
	}

	if (!TEST_FLAG(action->control_flags, _unit_control_throw_grenade_bit) ||
		bot_attack_safe(bot, TRUE, BOT_FIRE_LANE_SPREAD))
		return;
	action->control_flags &= ~FLAG(_unit_control_throw_grenade_bit);
	retry = game_time_get() + bot_seconds_to_ticks(1.f);
	bot->grenade_time = MIN(bot->grenade_time, retry);
	bot->throws_refused++;
}

/* port: the other of a player's two weapon slots; NONE for any other slot. */
static short bot_other_weapon_slot(short slot)
{
	return slot == 0 ? 1 : slot == 1 ? 0 : NONE;
}

/* port: the trigger while a teammate is in the firing lane. A charging or
charged trigger is held, as letting go of it fires; a swap to the other
weapon, where there is one, then puts the weapon away at once (unit_update),
which drops the charge unfired. Should the lane clear first, the hold ends
and the charge goes at the enemy. Any other trigger is let go. */
static void bot_hold_fire(
	struct bot *bot,
	struct player_action *action,
	struct unit_datum const *unit,
	short slot,
	boolean holding_charge)
{
	short other = bot_other_weapon_slot(slot);

	bot->fire_held_ticks++;
	if (!holding_charge)
	{
		bot->fire_control.pressed = FALSE;
		bot->charging = FALSE;
		bot->pending.trigger = 0.f;
		return;
	}
	action->primary_trigger = 1.f;
	action->control_flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
	bot->fire_control.pressed = TRUE;
	bot->fire_control.release_tick = game_time_get();
	if (unit->object.type != _object_type_vehicle && other != NONE &&
		weapon_try_and_get(unit->unit.weapon_object_indices[other]))
		action->desired_weapon_index = other;
}

/* Charged shots are deliberate: a visible, shielded opponent at medium
   range, no more than once every few seconds. Continue aiming through burst
   pauses, but release if the target changes or disappears. */
static boolean bot_weapon_fire(struct bot *bot, struct weapon_trigger_definition const *trigger)
{
	long now = game_time_get();
	long period = bot_seconds_to_ticks(bot_random_range(bot, 0.35f, 0.55f));
	long hold_ticks = 1;
	boolean wants_fire = bot->pending.trigger > 0.f;
	boolean tap = trigger->charging_time > 0.f || TEST_FLAG(trigger->flags, _weapon_trigger_latched_bit);
	boolean pressed;

	if (trigger->initial_rate_of_fire > 0.f && valid_real(trigger->initial_rate_of_fire))
		period = MAX(period, (long)ceil((real)TICKS_PER_SECOND /
			PIN(trigger->initial_rate_of_fire, 0.1f, 30.f)));
	if (bot->charging)
	{
		wants_fire = bot->target_visible && bot->target_player_index == bot->charge_target_index;
	}
	else if (wants_fire && trigger->charging_time > 0.f && valid_real(trigger->charging_time) &&
		trigger->charging_time <= 5.f && !bot->fire_control.pressed &&
		now >= bot->fire_control.next_press_tick && now >= bot->next_charge_time)
	{
		/* (the target may have left since it was seen) */
		struct player_datum *target = bot->target_player_index != NONE ?
			player_try_and_get(bot->target_player_index) : NULL;
		struct unit_datum *target_unit = target ? bot_living_unit(target->unit_index) : NULL;
		real_point3d origin;
		real distance;

		object_get_origin(bot->unit_index, &origin);
		distance = distance3d(&origin, &bot->target_last_position);
		if (target_unit && bot->target_visible && target_unit->object.shield_vitality > 0.5f &&
			distance > 6.f && distance < 25.f)
		{
			hold_ticks = (long)ceil(trigger->charging_time * TICKS_PER_SECOND) + 2;
			bot->charging = TRUE;
			bot->charge_target_index = bot->target_player_index;
			bot->next_charge_time = now + bot_seconds_to_ticks(bot_random_range(bot, 5.f, 9.f));
		}
	}
	pressed = engine_ai_fire_control_step(&bot->fire_control, now, wants_fire,
		tap, (int32_t)period, (int32_t)hold_ticks);
	if (bot->charging && !pressed)
	{
		bot->charging = FALSE;
		/* A beat after releasing the overcharge, not an immediate tap. */
		bot->fire_control.next_press_tick = now + bot_seconds_to_ticks(0.6f);
	}
	return pressed;
}

/* Resolve the equipped weapon each tick; don't carry a held trigger across
   weapon swaps or respawns. Charging and latched triggers need press/release. */
static void bot_control_weapon(struct bot *bot, struct player_action *action)
{
	struct unit_datum *unit = bot_living_unit(bot->unit_index);
	struct weapon_datum *weapon;
	struct weapon_definition *definition;
	struct weapon_trigger_definition *trigger;
	long weapon_index;
	short slot;

	if (!unit)
		return;
	/* port: a flag must remain readied. Never auto-switch to the other gun,
	   reload it, or fire a stale trigger while delivering. */
	if (bot_carries_flag(unit))
	{
		bot->pending.trigger = 0.f;
		csmemset(&bot->fire_control, 0, sizeof(bot->fire_control));
		bot->charging = FALSE;
		return;
	}
	if (unit->object.parent_object_index != NONE)
	{
		struct unit_datum *parent = vehicle_try_and_get(unit->object.parent_object_index);
		struct unit_definition *definition;
		struct unit_seat *seat;

		if (!parent)
			return;
		definition = unit_definition_get(parent->definition_index);
		if (unit->unit.parent_seat_index < 0 || unit->unit.parent_seat_index >= definition->unit.seats.count)
			return;
		seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, unit->unit.parent_seat_index, struct unit_seat);
		if (TEST_FLAG(seat->flags, _unit_seat_driver_bit) || TEST_FLAG(seat->flags, _unit_seat_gunner_bit))
			unit = parent; /* Use the vehicle's weapon, not the carried pistol. */
	}
	slot = unit->unit.current_weapon_index;
	if (slot < 0 || slot >= MAXIMUM_WEAPONS_PER_UNIT)
		return;
	weapon_index = unit->unit.weapon_object_indices[slot];
	if (weapon_index == NONE)
		return;
	if (unit->object.type != _object_type_vehicle && !bot->charging && !bot->fire_control.pressed &&
		bot_other_weapon_slot(slot) != NONE)
	{
		short other = bot_other_weapon_slot(slot);
		long other_weapon = unit->unit.weapon_object_indices[other];

		if (other_weapon != NONE && bot_weapon_utility(other_weapon) > bot_weapon_utility(weapon_index) * 1.35f)
		{
			action->desired_weapon_index = other;
			return;
		}
	}
	if (weapon_index != bot->fire_weapon_index)
	{
		bot->fire_weapon_index = weapon_index;
		csmemset(&bot->fire_control, 0, sizeof(bot->fire_control));
		bot->charging = FALSE;
		return; /* Release before using the new weapon. */
	}
	weapon = weapon_try_and_get(weapon_index);
	if (!weapon)
		return;
	definition = weapon_definition_get(weapon->definition_index);
	if (definition->weapon.triggers.count <= 0)
		return;
	trigger = TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition);
	/* Battery weapons may reference an unused magazine with zero rounds. */
	if (trigger->rounds_per_shot > 0 && trigger->magazine_index >= 0 &&
		trigger->magazine_index < NUMBEROF(weapon->weapon.magazines))
	{
		struct weapon_magazine *magazine = &weapon->weapon.magazines[trigger->magazine_index];

		if (magazine->rounds_loaded <= 0)
		{
			if (magazine->rounds_total > 0)
				action->control_flags |= FLAG(_unit_control_weapon_reload_bit);
			else if (bot_other_weapon_slot(slot) != NONE &&
				unit->unit.weapon_object_indices[bot_other_weapon_slot(slot)] != NONE)
				action->desired_weapon_index = bot_other_weapon_slot(slot);
			bot->pending.trigger = 0.f;
		}
	}
	/* port: never fire through a teammate (bot_attack_safe). Custom tags whose
	charge discharges while held are outside this policy. */
	if ((bot->pending.trigger > 0.f || bot->fire_control.pressed || bot->charging) &&
		!bot_attack_safe(bot, FALSE, bot_trigger_spread(trigger)))
	{
		bot_hold_fire(bot, action, unit, slot, trigger->charging_time > 0.f &&
			(bot->fire_control.pressed || weapon_overcharged(weapon_index)));
		return;
	}
	if (bot_weapon_fire(bot, trigger))
	{
		action->primary_trigger = 1.f;
		action->control_flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
	}
}

/* port: request recovery movement while a route search waits or a leaf fails.
Stalled movement changes direction and requests a jump; final steering checks
these requests for collision before they become the player's throttle. */
static void bot_keep_moving(
	struct bot *bot)
{
	struct unit_datum *unit = bot_living_unit(bot->unit_index);
	real_point3d origin;
	long now = game_time_get();

	if (!unit || unit->object.parent_object_index != NONE)
		return;
	/* port: holding a stand or waiting to score is intentional, not a stall. */
	if (bot->objective_waiting)
	{
		bot->movement_check_time = now;
		bot->movement_escape_until = now;
		return;
	}
	object_get_origin(bot->unit_index, &origin);
	/* port: queued/shared searches are deliberate waits, not movement stalls. */
	if (bot->navigation_agent.searching)
	{
		bot->movement_check_time = now;
		bot->movement_position = origin;
		bot->movement_escape_until = now;
		return;
	}
	if (bot->movement_check_time == NONE)
	{
		bot->movement_check_time = now;
		bot->movement_position = origin;
	}
	else if (now - bot->movement_check_time >= TICKS_PER_SECOND)
	{
		if (bot_horizontal_distance(&origin, &bot->movement_position) < 0.5f)
		{
			bot->movement_escape_yaw = bot_angle_normalize(bot->movement_escape_yaw +
				(real)M_PI * 0.75f);
			bot->movement_escape_until = now + TICKS_PER_SECOND;
			SET_FLAG(bot->pending.control_flags, _unit_control_jump_bit, TRUE);
		}
		bot->movement_check_time = now;
		bot->movement_position = origin;
	}
	/* (a bot planning or following a BSP route that emits no move this tick is
	waiting on purpose, for its search or for an occupant to pass; running in
	the escape direction would take it off its route. A real stall still escapes.) */
	if (now < bot->movement_escape_until || ((!bot->pending.has_move ||
		bot->pending.move_speed <= 0.f) && !bot->navigation_agent.searching &&
		!bot->navigation_agent.following))
	{
		bot->pending.has_move = TRUE;
		bot->pending.move_yaw = bot->movement_escape_yaw;
		bot->pending.move_speed = 1.f;
	}
}

/* port: probe the live capsule, not a point ray or a planned route. Inflate
laterally for wall clearance, preserving vertical extents on level ground. */
static boolean bot_movement_heading_clear(
	struct bot const *bot,
	real yaw,
	real clearance,
	real probe_distance)
{
	real_point3d base;
	real_point3d clipped;
	real_vector3d delta;
	real_vector3d clipped_velocity;
	real height;
	real radius;
	struct biped_datum const *biped = biped_get(bot->unit_index);
	real_vector3d const *normal = &biped->biped.ground_plane.n;
	boolean on_slope = !TEST_FLAG(biped->biped.flags, _biped_airborne_bit) && normal->k > 0.5f;
	struct collision_plane planes[8];

	biped_get_physics_pill(bot->unit_index, &base, &height, &radius);
	/* port: a spherical/crouched pill cannot lose more height than it has. */
	clearance = MIN(clearance, height * 0.5f);
	base.z += on_slope ? clearance / normal->k : clearance;
	height -= 2.f * clearance;
	radius += clearance;
	delta.i = cosine(yaw) * probe_distance;
	delta.j = sine(yaw) * probe_distance;
	/* port: follow the support plane instead of probing horizontally into
	the floor of an uphill route. New steps/ledges still belong to physics. */
	delta.k = on_slope ? -(normal->i * delta.i + normal->j * delta.j) / normal->k : 0.f;
	collision_move_pill(_collision_test_for_bipeds_living_flags, &base, &delta,
		height, radius, bot->unit_index, &clipped, &clipped_velocity, NUMBEROF(planes), planes);
	/* port: collision may slide to a different heading; accept only the requested
	endpoint. Physics still owns the actual movement and velocity. */
	return (real)fabs(clipped.x - base.x - delta.i) < 0.02f &&
		(real)fabs(clipped.y - base.y - delta.j) < 0.02f &&
		(real)fabs(clipped.z - base.z - delta.k) < 0.15f;
}

/* port: all on-foot steering (fight, retreat, routes and stuck recovery)
passes here last. At most sixteen capsule sweeps; no sneaking policy. Prefer
roomy headings first, then allow an exact-capsule fit rather than deadlock. */
static void bot_steer_movement(
	struct bot *bot)
{
	static real const offsets[] = { 0.f, 0.7853982f, -0.7853982f,
		1.5707963f, -1.5707963f, 2.3561945f, -2.3561945f, 3.1415927f };
	struct unit_datum const *unit = bot_living_unit(bot->unit_index);
	real preferred = bot->pending.move_yaw;
	/* A capsule-checked BSP route may legitimately hug a wall. Inflating the
	pill first made steering choose a roomy sideways heading and miss the
	waypoint forever. Physics and the exact live sweep still own clearance. */
	real clearance = bot->navigation_agent.following ? 0.f :
		bot->chosen_leaf == _bot_leaf_retreat ? 0.02f : 0.12f;
	real probe_distance = 0.65f;
	short pass;
	short candidate;

	if (!unit || unit->object.parent_object_index != NONE ||
		unit->object.type != _object_type_biped || !bot->pending.has_move ||
		bot->pending.move_speed <= 0.f)
	{
		return;
	}
	/* Never test through a wall behind a nearby route waypoint. The biped
	needs to reach that point, not walk another 0.65 m beyond it. */
	if (bot->navigation_agent.following &&
		bot->navigation_agent.waypoint < bot->navigation_agent.route.count)
	{
		struct sn_point const *point = &bot->navigation_agent.route.points[bot->navigation_agent.waypoint];
		real_point3d origin;
		real_point3d target;
		object_get_origin(bot->unit_index, &origin);
		target.x = point->x;
		target.y = point->y;
		target.z = point->z;
		probe_distance = MIN(probe_distance, bot_horizontal_distance(&origin, &target));
	}
	for (pass = 0; pass < 2; pass++)
	{
		for (candidate = 0; candidate < (short)NUMBEROF(offsets); candidate++)
		{
			real yaw = bot_angle_normalize(preferred + offsets[candidate]);

			if (bot_movement_heading_clear(bot, yaw, clearance, probe_distance))
			{
				bot->pending.move_yaw = yaw;
				return;
			}
		}
		clearance = 0.f;
	}
	/* port: every heading is blocked. Do not submit unsafe recovery input. */
	bot->pending.move_speed = 0.f;
}

/* port: a patrol bot turns toward nearby human damage bearings. The cue is
not a target and never authorizes fire; other behaviors keep facing priority. */
static void bot_hear_human_alert(
	struct bot *bot,
	long team_index)
{
	real_point3d origin;
	real_point3d alert;

	if (bot->chosen_leaf != _bot_leaf_roam || bot->target_player_index != NONE)
		return;
	object_get_origin(bot->unit_index, &origin);
	if (human_callouts_nearest_alert(team_index, &origin, &alert))
	{
		unit_get_camera_position(bot->unit_index, &origin);
		bot->pending.look = bot_angles_to(&origin, &alert);
		bot->pending.has_look = TRUE;
	}
}

/* the accepted intents as the player's action: the facing turned toward the
look at this skill's speed, and the movement relative to it */
static void bot_submit_action(
	struct bot *bot)
{
	struct player_action action;
	real turn = DEGREES_TO_RADIANS(bots_globals.skill->turn_degrees_per_second) / TICKS_PER_SECOND;

	bot_keep_moving(bot);
	bot_steer_movement(bot); /* port: validate after recovery, before throttle */
	bot->moved_last_tick = bot->pending.has_move && bot->pending.move_speed > 0.3f;
	/* port: lower skills struggle to look around while they move */
	if (bot->moved_last_tick)
		turn *= bots_globals.skill->moving_turn_scale;
	if (bot->pending.has_look)
	{
		real yaw_delta = bot_angle_normalize(bot->pending.look.yaw - bot->facing.yaw);
		real pitch_delta = bot->pending.look.pitch - bot->facing.pitch;

		bot->facing.yaw = bot_angle_normalize(bot->facing.yaw + PIN(yaw_delta, -turn, turn));
		bot->facing.pitch += PIN(pitch_delta, -turn, turn);
	}
	csmemset(&action, 0, sizeof(action));
	action.control_flags = bot->pending.control_flags;
	action.desired_facing = bot->facing;
	if (bot->pending.has_move)
	{
		real relative = bot->pending.move_yaw - bot->facing.yaw;

		action.throttle.i = cosine(relative) * bot->pending.move_speed;
		action.throttle.j = sine(relative) * bot->pending.move_speed;
	}
	action.desired_weapon_index = NONE;
	action.desired_grenade_index = NONE;
	action.desired_zoom_level = NONE;
	bot_control_grenade(bot, &action);
	bot_control_weapon(bot, &action);
	bot->last_trigger = action.primary_trigger;
	bot->last_control_flags = action.control_flags;
	update_server_set_player_action(bot->player_index, &action);
}

/* port: a scenario's goal (debug.bot_scenario's route test): the bot walks
its own BSP route there, as a pursuit would, with the same recovery and
steering, and does nothing else */
static short bot_scenario_walk(
	struct bot *bot,
	real_point3d const *goal)
{
	struct bot_navigation_command command;
	struct sn_point point;
	enum bot_navigation_result result;

	csmemset(&bot->pending, 0, sizeof(bot->pending));
	point.x = goal->x;
	point.y = goal->y;
	point.z = goal->z;
	result = bot_navigation_agent_move_to(&bot->navigation_agent, bot->unit_index, point, 1.f, 0.6f, &command);
	if (result == BOT_NAVIGATION_MOVING && command.has_move)
	{
		bot->pending.has_move = TRUE;
		bot->pending.move_yaw = bot_angle_normalize(command.yaw);
		bot->pending.move_speed = PIN(command.speed, 0.f, 1.f);
		bot->pending.has_look = TRUE;
		bot->pending.look.yaw = bot->pending.move_yaw;
		bot->pending.look.pitch = 0.f;
	}
	if (command.jump)
		bot->pending.control_flags |= FLAG(_unit_control_jump_bit);
	/* (arrived: standing on purpose, not a stall to escape) */
	bot->objective_waiting = result == BOT_NAVIGATION_ARRIVED;
	bot_submit_action(bot);
	return (short)result;
}

/* port: what a scenario logs of a subject's tick */
static void bot_scenario_report(
	struct bot *bot,
	short navigation_result)
{
	struct bot_scenario_observation observation;
	uint32_t leaf = bot->behavior.running_node < NUMBEROF(bot_behavior_nodes) ?
		bot_behavior_nodes[bot->behavior.running_node].leaf_id : 0;
	short index;

	csmemset(&observation, 0, sizeof(observation));
	observation.slot = bot->slot;
	observation.leaf_name = navigation_result != NONE ? "goal" :
		bot_leaf_names[leaf < NUMBEROF(bot_leaf_names) ? leaf : 0];
	for (index = 0; index < BOT_SCENARIO_SCORES && index < BOT_BEHAVIOR_COUNT; index++)
		observation.scores[index] = bot->scores[index];
	observation.confidence = bot->confidence;
	observation.target_player_index = bot->target_player_index;
	observation.target_source = bot->target_source;
	observation.target_visible = bot->target_visible;
	observation.aware = game_time_get() >= bot->aware_time;
	observation.target_belief = bot->target_last_position;
	observation.facing = bot->facing;
	observation.has_look = bot->pending.has_look;
	observation.look = bot->pending.look;
	observation.has_move = bot->pending.has_move;
	observation.move_yaw = bot->pending.move_yaw;
	observation.move_speed = bot->pending.move_speed;
	observation.trigger = bot->last_trigger;
	observation.control_flags = bot->last_control_flags;
	observation.strafe_sign = bot->strafe_sign;
	observation.route_searching = bot->navigation_agent.searching;
	observation.route_following = bot->navigation_agent.following;
	observation.route_waypoint = (short)MIN(bot->navigation_agent.waypoint, 32767u);
	observation.route_count = 0;
	if (bot->navigation_agent.following)
	{
		uint32_t point;

		/* (from its place, the points still ahead) */
		object_get_origin(bot->unit_index, &observation.route[observation.route_count++]);
		for (point = bot->navigation_agent.waypoint; point < bot->navigation_agent.route.count &&
			observation.route_count < BOT_SCENARIO_ROUTE_POINTS; point++)
		{
			real_point3d *to = &observation.route[observation.route_count++];

			to->x = bot->navigation_agent.route.points[point].x;
			to->y = bot->navigation_agent.route.points[point].y;
			to->z = bot->navigation_agent.route.points[point].z + 0.1f;
		}
	}
	observation.navigation_result = navigation_result;
	bot_scenario_observe(&observation);
}

/* ---------- private code: a bot's tick */

static void bot_reset_life(
	struct bot *bot,
	long unit_index)
{
	engine_ai_behavior_callbacks callbacks;
	struct player_datum *player = bot_player(bot);
	struct unit_datum *unit = bot_living_unit(unit_index);

	/* (the last life's running leaf is let go first) */
	callbacks.context = bot;
	callbacks.tick = bot_behavior_tick_plain;
	callbacks.cancel = bot_behavior_cancel;
	callbacks.trace = NULL;
	if (bot->behavior.initialized)
		engine_ai_behavior_cancel(&bot->behavior, &bot_behavior, &callbacks);
	bot_drop_opportunity(bot, "new life");
	bot_objective_release(bot); /* port: no objective identity survives a life */
	bot->objective_valid = FALSE;
	bot->objective_waiting = FALSE;
	bot->objective_unreachable_index = NONE;
	bot->objective_retry_time = 0;
	bot->objective_retry_kind = _bot_ambition_none;
	engine_ai_behavior_begin(&bot->behavior, &bot_behavior);

	bot->unit_index = unit_index;
	bot->target_player_index = NONE;
	bot->target_visible = FALSE;
	bot->target_source = _bot_target_none;
	bot->visible_enemy_count = 0;
	bot->surface_nav_wait_time = NONE;
	bot->surface_fallback_logged = FALSE;
	/* (nothing chosen: the first tick of the life decides) */
	engine_ai_utility_selector_init(&bot->selector, (uint64_t)game_time_get());
	bot->chosen_leaf = _bot_leaf_none;
	bot->decide_now = FALSE;
	bot->abs_suppressed = FALSE;
	bot->confidence = 0.f;
	bot->aim_error_time = 0;
	bot->strafe_change_time = 0;
	bot->burst_change_time = 0;
	bot->burst_firing = FALSE;
	bot->fire_weapon_index = NONE;
	bot->charging = FALSE;
	bot->charge_target_index = NONE;
	bot->next_charge_time = game_time_get() + bot_seconds_to_ticks(bot_random_range(bot, 2.f, 4.f));
	csmemset(&bot->fire_control, 0, sizeof(bot->fire_control));
	bot->melee_time = game_time_get();
	bot->grenade_time = game_time_get() + bot_seconds_to_ticks(3.f);
	bot->retreat_start_time = NONE;
	bot->retreat_cooldown_time = 0;
	bot->jump_requested = FALSE;
	bot->opportunity_index = NONE;
	bot->opportunity_team_index = NONE;
	bot->opportunity_scan_time = game_time_get();
	bot->interaction_time = 0;
	bot->observed_vehicle_index = NONE;
	bot->observed_weapons[0] = bot->observed_weapons[1] = NONE;
	bot->vehicle_progress_time = NONE;
	bot->vehicle_wait_until = NONE;
	bot->vehicle_wait_logged = FALSE;
	bot->vehicle_goal_valid = FALSE;
	bot->pickup_player_index = NONE;
	bot->pickup_until = 0;
	bot->pickup_meet = FALSE;
	bot->pickup_scan_time = 0;
	bot->vehicle_drop_off_valid = FALSE;
	bot->vehicle_drop_off_failures = 0;
	bot->pickup_declined_player_index = NONE;
	bot->pickup_declined_until = 0;
	bot->ride_vehicle_index = NONE;
	bot->ride_until = 0;
	bot_navigation_reset(bot);
	if (unit)
		euler_angles2d_from_vector3d(&bot->facing, &unit->unit.desired_aiming_vector);
	/* port: a moment to get its bearings, looking about from where it faces */
	bot->orient_start_time = game_time_get();
	bot->aware_time = bot->orient_start_time + (unit ? bot_seconds_to_ticks(
		bots_globals.skill->orient_seconds * bot_random_range(bot, 1.f, 1.6f)) : 0);
	bot->orient_yaw = bot->facing.yaw;
	bot->orient_direction = bot_random_range(bot, 0.f, 1.f) < 0.5f ? -1.f : 1.f;
	bot->movement_check_time = NONE;
	bot->movement_escape_until = 0;
	bot->movement_escape_yaw = bot->facing.yaw;
	/* port: aim, crouch, teleport and item memory start over each life; a
	dead bot's item claims are let go at once */
	bot->moved_last_tick = FALSE;
	bot->crouch_until = 0;
	bot->crouch_next_time = game_time_get() + bot_seconds_to_ticks(bot_random_range(bot, 1.f, 3.f));
	bot->target_observed_player_index = NONE;
	bot->target_observed_time = NONE;
	bot->opportunity_powerup = FALSE;
	bot->unreachable_item_index = NONE;
	bot->unreachable_item_until = 0;
	{
		short item_index;

		for (item_index = 0; item_index < BOT_KNOWN_ITEMS; item_index++)
		{
			bot->known_items[item_index].object_index = NONE;
			bot->known_items[item_index].seen_time = 0;
		}
	}
	bot_manager_release_bot(player ? player->team_index : NONE, (short)(bot->slot + 1));
	platform_log("bots: bot %d team %ld cleared item claims (new life)", bot->slot + 1,
		player ? player->team_index : NONE);
}

/* Find a parked, empty ground vehicle with a driver and gunner seat. This
   fixture deliberately does not create vehicles or bypass the bot's entry. */
static long bot_sandbox_vehicle(struct bot *bot, short *driver_seat, short *gunner_seat)
{
	struct object_iterator iterator;
	real_point3d origin;
	real nearest = 100000.f;
	long best = NONE;
	short examined = 0;

	object_get_origin(bot->unit_index, &origin);
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (examined++ < 256 && object_iterator_next(&iterator))
	{
		struct unit_datum *vehicle = vehicle_get(iterator.index);
		struct unit_definition *definition = unit_definition_get(vehicle->definition_index);
		short driver = NONE;
		short gunner = NONE;
		short seat_index;
		real distance;

		if (!vehicle_supports_bot_driver(iterator.index) || vehicle->unit.driver_object_index != NONE ||
			TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) || vehicle->object.up.k < 0.6f)
			continue;
		for (seat_index = 0; seat_index < MIN(definition->unit.seats.count, 16); seat_index++)
		{
			if (unit_seat_is_driver(iterator.index, seat_index))
				driver = seat_index;
			if (unit_seat_is_gunner(iterator.index, seat_index))
				gunner = seat_index;
		}
		distance = distance3d(&origin, &vehicle->object.position);
		if (driver != NONE && gunner != NONE && distance < nearest)
		{
			best = iterator.index;
			nearest = distance;
			*driver_seat = driver;
			*gunner_seat = gunner;
		}
	}
	return best;
}

static void bot_debug_sandbox(struct bot *bot)
{
	struct data_iterator iterator;
	struct player_datum *host = NULL;
	struct player_datum *player;
	struct player_datum const *bot_player_datum = bot_player(bot);
	short driver = NONE;
	short gunner = NONE;
	long vehicle_index;
	struct unit_datum *vehicle;
	real_point3d point;
	real_point3d host_point;
	real ground_z;

	if (bots_globals.sandbox_done || bot->slot != 1 || !game_engine_has_teams() ||
		!bot_player_datum || !config_boolean("debug.bot_sandbox"))
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->local_player_index != NONE && bot_living_unit(player->unit_index) &&
			player->team_index == bot_player_datum->team_index)
		{
			host = player;
			break;
		}
	}
	if (!host || unit_get(host->unit_index)->object.parent_object_index != NONE)
		return;
	vehicle_index = bot_sandbox_vehicle(bot, &driver, &gunner);
	if (vehicle_index == NONE)
		return;
	vehicle = vehicle_get(vehicle_index);
	if (!unit_get_seat_entrance_point(bot->unit_index, vehicle_index, gunner, &point, NULL, NULL) ||
		!unit_get_seat_entrance_point(host->unit_index, vehicle_index, driver, &host_point, NULL, NULL))
		return;
	point.x += vehicle->object.forward.i * 2.f;
	point.y += vehicle->object.forward.j * 2.f;
	point.z += 1.f;
	if (!bot_navigation_ground(&point, 4.f, &ground_z))
		return;
	point.z = ground_z + 0.05f;
	/* A parked vehicle can be tilted. Preserve the biped's orthonormal
	   orientation rather than pairing vehicle forward with world up. */
	object_set_position(host->unit_index, &host_point, NULL, NULL);
	unit_get(host->unit_index)->object.translational_velocity = *global_zero_vector3d;
	if (!unit_enter_seat(host->unit_index, vehicle_index, driver))
		return;
	object_set_position(bot->unit_index, &point, NULL, NULL);
	unit_get(bot->unit_index)->object.translational_velocity = *global_zero_vector3d;
	bot_drop_opportunity(bot, "sandbox fixture");
	bot->opportunity_scan_time = game_time_get() + TICKS_PER_SECOND;
	bots_globals.sandbox_done = TRUE;
	platform_log("bots: sandbox host drives vehicle %ld; Bot 2 approaches gunner seat %d normally",
		vehicle_index, gunner);
}

/* Every behavior scored and the best chosen, at the decision cadence, or at
once when the running leaf can no longer run or has finished. Between
decisions the running leaf continues: a bot reacts as a person does, after a
moment, not every tick. */
static void bot_decide(
	struct bot *bot,
	struct player_datum const *player,
	struct unit_datum const *unit)
{
	long now = game_time_get();
	boolean first = bot->chosen_leaf == _bot_leaf_none;
	boolean force = bot->decide_now || (!first && !bot_leaf_viable(bot, bot->chosen_leaf));
	uint32_t chosen = ENGINE_AI_UTILITY_NONE;
	uint32_t leaf;
	engine_ai_utility_decision decision;

	if (!force && !first && (uint64_t)now < bot->selector.next_decision_tick)
		return;
	bot->decide_now = FALSE;
	bot_score_behaviors(bot, player, unit);
	decision = engine_ai_utility_select(&bot->selector, &bot_selector_config, bot->scores,
		(uint64_t)now, force, &chosen);
	if (decision == ENGINE_AI_UTILITY_INVALID)
		return;
	/* (bots that spawn together decide on different ticks from then on) */
	if (first)
		bot->selector.next_decision_tick += (uint64_t)(bot->slot % BOT_DECISION_TICKS);
	leaf = chosen < BOT_BEHAVIOR_COUNT ? chosen + 1 : _bot_leaf_none;
	if (leaf != bot->chosen_leaf && bots_globals.log_decisions)
	{
		platform_log("bots: bot %d tick %ld %s -> %s (conf %.2f) retreat %.2f fight %.2f roam %.2f scavenge %.2f vehicle %.2f, target %s",
			bot->slot + 1, game_time_get(), bot_leaf_names[bot->chosen_leaf], bot_leaf_names[leaf], bot->confidence,
			bot->scores[_bot_leaf_retreat - 1], bot->scores[_bot_leaf_fight - 1], bot->scores[_bot_leaf_roam - 1],
			bot->scores[_bot_leaf_scavenge - 1], bot->scores[_bot_leaf_vehicle - 1],
			bot->target_source == _bot_target_seen ? "seen" :
			bot->target_source == _bot_target_attacker ? "attacker" :
			bot->target_source == _bot_target_remembered ? "remembered" :
			bot->target_source == _bot_target_shared ? "shared" : "none");
	}
	if (bot->opportunity_index != NONE && leaf != _bot_leaf_scavenge)
	{
		bot_drop_opportunity(bot, leaf == bot->chosen_leaf ? "behavior not selected" : "behavior changed");
		bot->opportunity_scan_time = now + bot_seconds_to_ticks(3.f);
	}
	if (leaf == bot->chosen_leaf)
		return;
	/* (the old leaf lets go: its route, its retreat; the tree starts again
	and descends to the new one) */
	{
		engine_ai_behavior_callbacks callbacks;

		callbacks.context = bot;
		callbacks.tick = bot_behavior_tick_plain;
		callbacks.cancel = bot_behavior_cancel;
		callbacks.trace = NULL;
		if (bot->behavior.initialized)
			engine_ai_behavior_cancel(&bot->behavior, &bot_behavior, &callbacks);
		engine_ai_behavior_begin(&bot->behavior, &bot_behavior);
	}
	bot->chosen_leaf = leaf;
}

/* port: in CTF, a vehicle is worth taking to an objective this far away to
take or return a flag, or to hunt a carrier; never for a guard or a carrier */
static boolean bot_ctf_vehicle_wanted(struct bot *bot)
{
	struct unit_datum const *unit = bot_living_unit(bot->unit_index);

	if (!unit || !bot->objective_valid || bot_carries_flag(unit) ||
		(bot->objective.kind != _bot_ambition_take && bot->objective.kind != _bot_ambition_return &&
		bot->objective.kind != _bot_ambition_hunt))
		return FALSE;
	return bot_horizontal_distance(&unit->object.position, &bot->objective.position) > BOT_CTF_DRIVE_DISTANCE;
}

/* port: a bot that has just spawned gets its bearings before it acts, as a
player does: it stands and looks about, either side of where it faces, until
aware_time (its skill's orient_seconds, randomly up to 1.6 times), rather
than firing at the first enemy in its first second. It notices no enemy
meanwhile (bot_perceive); being hurt ends it at once. */
static boolean bot_orient(
	struct bot *bot)
{
	long now = game_time_get();
	real seconds;

	if (now >= bot->aware_time)
		return FALSE;
	seconds = (real)(now - bot->orient_start_time) / (real)TICKS_PER_SECOND;
	csmemset(&bot->pending, 0, sizeof(bot->pending));
	bot->pending.has_look = TRUE;
	/* (a look to one side and back across to the other every two seconds;
	the skill's turn speed limits it) */
	bot->pending.look.yaw = bot_angle_normalize(bot->orient_yaw + bot->orient_direction *
		DEGREES_TO_RADIANS(BOT_ORIENT_SWEEP_DEGREES) * sine(seconds * (real)M_PI));
	bot->pending.look.pitch = 0.f;
	/* (standing on purpose: not a stall for bot_keep_moving to escape) */
	bot->objective_waiting = TRUE;
	bot_submit_action(bot);
	return TRUE;
}

static void bot_think(
	struct bot *bot)
{
	struct player_datum *player = bot_player(bot);
	struct unit_datum *unit;
	struct engine_ai_actor_id actor;
	struct engine_ai_tick_context tick;
	struct engine_ai_behavior_intent_callbacks callbacks;
	engine_ai_behavior_result result;

	if (!player)
		return;
	unit = bot_living_unit(player->unit_index);
	if (!unit || !unit_controllable(player->unit_index))
	{
		/* (dead, or waiting to respawn) */
		if (bot->unit_index != NONE)
			bot_reset_life(bot, NONE);
		return;
	}
	if (player->unit_index != bot->unit_index)
		bot_reset_life(bot, player->unit_index);

	/* port: a scenario holds its bots between trials, scripts its targets, and
	sends a route test's subject to its goal (bot_scenario.h) */
	{
		struct player_action scenario_action;
		real_point3d scenario_goal;

		switch (bot_scenario_control(bot->slot, &scenario_action, &scenario_goal))
		{
		case _bot_scenario_control_action:
			bot->facing = scenario_action.desired_facing;
			update_server_set_player_action(bot->player_index, &scenario_action);
			return;
		case _bot_scenario_control_goal:
			bot->objective_waiting = FALSE;
			bot_scenario_report(bot, bot_scenario_walk(bot, &scenario_goal));
			return;
		default:
			break;
		}
	}

	/* Geometry acceptance probe takes only Bot 1, after the normal authority,
	   lifecycle and unit-control gates. Normal tactics/spawns stay unchanged. */
	if (bot->slot == 0)
	{
		struct player_action probe_action;
		if (navigation_probe_action(player->unit_index, &probe_action))
		{
			update_server_set_player_action(bot->player_index, &probe_action);
			return;
		}
	}
	/* port: CTF countdown/results never produce combat or recovery movement. */
	if (game_engine_ctf_bot_variant_supported(game_engine_get_variant()) && !game_engine_can_score())
	{
		struct player_action action;
		csmemset(&action, 0, sizeof(action));
		action.desired_facing = bot->facing;
		action.desired_weapon_index = action.desired_grenade_index = action.desired_zoom_level = NONE;
		update_server_set_player_action(bot->player_index, &action);
		return;
	}
	bot->objective_waiting = FALSE;
	bot_debug_sandbox(bot);
	bot_perceive(bot, player, unit);
	bot_observe_inventory(bot, unit);
	if (bot_orient(bot))
		return;
	bot_update_objective(bot, player);
	if (bot_await_ride(bot, unit))
		return;
	/* port: CTF bots go on foot, except that an attacker or returner whose
	   objective is far takes a vehicle (and drives teammates there: the
	   chauffeur), and a bot offered a ride takes it. Defenders and carriers
	   keep to their duty on foot. */
	if (game_engine_ctf_bot_variant_supported(game_engine_get_variant()))
	{
		boolean riding = bot->ride_vehicle_index != NONE && bot->opportunity_index == bot->ride_vehicle_index;

		if (bot_ctf_vehicle_wanted(bot) || riding)
		{
			if (bot->opportunity_index != NONE && !bot->opportunity_vehicle)
				bot_drop_opportunity(bot, "CTF on-foot objective policy");
			bot_find_opportunity(bot, TRUE);
			bot_maintain_opportunity(bot);
		}
		else
			bot_drop_opportunity(bot, "CTF on-foot objective policy");
	}
	else
	{
		bot_find_opportunity(bot, FALSE);
		bot_maintain_opportunity(bot);
	}
	bot_decide(bot, player, unit);

	actor.index = (uint32_t)DATUM_INDEX_TO_ABSOLUTE_INDEX(bot->player_index);
	actor.generation = (uint32_t)(unsigned short)(bot->player_index >> 16);
	tick.id.epoch = bots_globals.epoch;
	tick.id.tick = (uint64_t)game_time_get();
	tick.revision_id = 0;
	if (engine_ai_intent_begin(&bot->batch, actor, &tick, ENGINE_AI_INTENTS_MAX) != ENGINE_AI_INTENT_OK)
		return;
	engine_ai_behavior_intent_state_init(&bot->bridge);
	callbacks.context = bot;
	callbacks.tick = bot_behavior_tick;
	callbacks.cancel = bot_behavior_cancel;
	callbacks.relevant = bot_behavior_relevant;
	callbacks.trace = NULL;

	bot->abs_suppressed = FALSE;
	result = ENGINE_AI_BEHAVIOR_RUNNING;
	if (engine_ai_behavior_intent_step(&bot->bridge, &bot->behavior, &bot_behavior,
		BOT_BEHAVIOR_WORK_PER_TICK, &callbacks, &bot->batch, &result) != ENGINE_AI_BEHAVIOR_INTENT_OK)
	{
		/* (a faulted tree: start again next tick) */
		bot_reset_life(bot, bot->unit_index);
	}
	/* (the chosen leaf finished or gave up: choose again next tick) */
	else if (result != ENGINE_AI_BEHAVIOR_RUNNING && result != ENGINE_AI_BEHAVIOR_YIELDED)
		bot->decide_now = TRUE;

	csmemset(&bot->pending, 0, sizeof(bot->pending));
	engine_ai_intent_execute(&bot->batch, &tick, &bot->report, bot, bot_execute_intent);
	bot_hear_human_alert(bot, player->team_index); /* port: directional cue only */
	bot_submit_action(bot);
	if (bot_scenario_enabled())
		bot_scenario_report(bot, NONE);

	/* (what it is doing, every ten seconds, to the log) */
	if (game_time_get() % (10 * TICKS_PER_SECOND) == bot->slot * 7)
	{
		real_point3d origin;
		uint32_t leaf = bot->behavior.running_node < NUMBEROF(bot_behavior_nodes) ?
			bot_behavior_nodes[bot->behavior.running_node].leaf_id : 0;

		object_get_origin(bot->unit_index, &origin);
		platform_log("bots: bot %d at (%.1f %.1f %.1f) yaw %.0f %s (conf %.2f), target %ld%s, shield %.2f, kills %d deaths %d"
			", betrayals %d, held fire %ld ticks, refused %ld throws",
			bot->slot + 1, origin.x, origin.y, origin.z, RADIANS_TO_DEGREES(bot->facing.yaw),
			bot_leaf_names[leaf < NUMBEROF(bot_leaf_names) ? leaf : 0], bot->confidence,
			(long)(bot->target_player_index == NONE ? -1 : DATUM_INDEX_TO_ABSOLUTE_INDEX(bot->target_player_index)),
			bot->target_visible ? " (seen)" : "", unit->object.shield_vitality,
			player->statistics.kills[0], player->statistics.deaths,
			player->statistics.friendly_fire_kills, bot->fire_held_ticks, bot->throws_refused);
		/* (the friendly-fire gates' counts are per line) */
		bot->fire_held_ticks = 0;
		bot->throws_refused = 0;
	}
}

/* ---------- private code: joining and leaving */

/* the host of a game with a game engine that nobody else is in */
static boolean bots_host_may_have_bots(
	void)
{
	long machine_indices[1];

	/* port: only Slayer and standard two-team CTF have goal execution. */
	if (!game_engine_get_variant() ||
		(game_engine_get_variant()->game_engine_index != game_engine_slayer &&
		(!config_boolean("bots.ctf") || !game_engine_ctf_bot_variant_supported(game_engine_get_variant()))))
	{
		return FALSE;
	}
	if (!game_time_initialized() || !game_engine_running() || game_engine_showing_postgame() ||
		network_game_distributed_client())
	{
		return FALSE;
	}
	switch (game_connection())
	{
	case _game_connection_local:
		return TRUE;
	case _game_connection_network_server:
		return distributed_client_machines(machine_indices, 1) == 0;
	}
	return FALSE;
}

/* bots whose player is gone (a player who joined took its slot) */
static void bots_refresh(
	void)
{
	short bot_index;

	for (bot_index = 0; bot_index < BOTS_MAXIMUM; bot_index++)
	{
		struct bot *bot = &bots_globals.bots[bot_index];

		if (bot->active && !bot_player(bot))
		{
			struct unit_datum *unit = bot_living_unit(bot->unit_index);

			/* (its unit, no one's now, goes with it) */
			if (unit && unit->unit.player_index == NONE)
				unit_kill_no_statistics(bot->unit_index);
			bot->active = FALSE;
		}
	}
}

static short bots_free_machine_slots(
	long machine_index)
{
	long *player_list = machine_get_player_list(machine_index);
	short index;
	short count = 0;

	for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
	{
		if (player_list[index] == NONE)
			count++;
	}
	return count;
}

/* the team with fewer players */
static char bots_choose_team(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long counts[2] = { 0, 0 };

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (!player->quit_out_of_game && player->team_index >= 0 && player->team_index < 2)
			counts[player->team_index]++;
	}
	return counts[1] < counts[0] ? 1 : 0;
}

/* every bot missing, at once, until there are as many as configured: a new
game's bots join in its first tick and spawn with its players */
static void bots_join(
	void)
{
	short active_count = 0;
	short bot_index;

	for (bot_index = 0; bot_index < BOTS_MAXIMUM; bot_index++)
	{
		if (bots_globals.bots[bot_index].active)
			active_count++;
	}
	for (bot_index = 0; bot_index < BOTS_MAXIMUM && active_count < bots_globals.desired_count; bot_index++)
	{
		if (!bots_globals.bots[bot_index].active && bot_join(&bots_globals.bots[bot_index], bot_index))
			active_count++;
	}
}

/* Halo's player-name field is 12 wchar_t entries, including its terminator. */
static char const *const bot_names[] =
{
	"343 Parfit", "343 Locke", "343 Hume", "343 Aloysiu", "343 Hobbes",
	"343 Nando", "343 CreamCo", "343 Godfath", "343 Darksta", "343 Meowlni",
	"343 SaltBar", "343 FreeMon", "343 Ritzy", "343 Beard", "343 Hundy",
	"343 Razzle", "343 Dazzle"
};

static void bot_name_set(
	struct network_player *network_player,
	short bot_index,
	char name[12])
{
	char const *source;
	short character;

	csmemset(name, 0, 12);
	if (bot_index < NUMBEROF(bot_names))
		source = bot_names[bot_index];
	else
	{
		name[0] = 'B'; name[1] = 'o'; name[2] = 't'; name[3] = ' ';
		if (bot_index + 1 >= 10)
		{
			name[4] = (char)('0' + (bot_index + 1) / 10);
			name[5] = (char)('0' + (bot_index + 1) % 10);
		}
		else
			name[4] = (char)('1' + bot_index);
		source = name;
	}
	for (character = 0; character < 11 && source[character]; character++)
	{
		name[character] = source[character];
		network_player->name[character] = (wchar_t)source[character];
	}
}

/* Rank this bot among the teammates that got a bot slot before it. */
static long bots_team_rank(
	struct bot *bot)
{
	struct player_datum *player = bot_player(bot);
	short index;
	long rank = 0;

	if (!player)
		return NONE;
	for (index = 0; index < bot->slot; index++)
	{
		struct bot *other = &bots_globals.bots[index];
		struct player_datum *other_player;

		if (!other->active)
			continue;
		other_player = bot_player(other);
		if (other_player && other_player->team_index == player->team_index)
			rank++;
	}
	return rank;
}

/* port: the lobby's friendlies follow controller 1's team; ordinary
configured bots keep the existing balanced-team policy. */
static char bots_match_team(short bot_index)
{
	struct player_datum *host;
	short team;

	/* port: a scenario's slots have fixed teams (bot_scenario_team) */
	if (bot_scenario_enabled() && game_engine_has_teams())
		return bot_scenario_team(bot_index);
	if (lobby_friendly_count == NONE || !game_engine_has_teams())
		return bots_choose_team();
	host = player_try_and_get(local_player_get_player_index(0));
	team = host && host->team_index >= 0 && host->team_index < 2 ?
		(short)host->team_index : 0;
	return (char)(bot_index < lobby_friendly_count ? team : 1 - team);
}

/* port: local player admission alternates teams even when a network_player
supplied a team. Restore the lobby's selection after admission, before the
player is published to the update pipeline or gets a body. */
static void bots_player_added(long player_index, short bot_index)
{
	game_engine_player_added(player_index);
	if (((lobby_friendly_count != NONE && game_connection() == _game_connection_local) ||
		bot_scenario_enabled()) && game_engine_has_teams())
	{
		struct player_datum *player = player_try_and_get(player_index);
		if (player)
		{
			player->team_index = bots_match_team(bot_index);
			player->network_player_data.team_index = (char)player->team_index;
		}
	}
}

/* the bot in the slot joins as a player; FALSE if there is no room for it */
static boolean bot_join(
	struct bot *bot,
	short bot_index)
{
	long now = game_time_get();
	struct network_player network_player;
	long player_index;
	char name[12];
	long machine_index = bot_machine_index(bot_index);

	if (bots_free_machine_slots(machine_index) == 0)
		return FALSE;

	csmemset(&network_player, 0, sizeof(network_player));
	bot_name_set(&network_player, bot_index, name);
	network_player.primary_color_index = (short)((3 + bot_index * 5) % 18);
	network_player.machine_index = (char)machine_index;
	network_player.controller_index = (char)(bot_index % MAXIMUM_LOCAL_PLAYERS);
	network_player.team_index = bots_match_team(bot_index); /* port: lobby's team counts */
	network_player.player_list_index = NONE;

	player_index = player_new(machine_index, NONE, NONE, &network_player);
	if (player_index == NONE)
		return FALSE;
	/* (as a player who joins a game in progress:
	network_game_client_add_player_to_game) */
	bots_player_added(player_index, bot_index); /* port: preserve the lobby's team */
	update_client_add_player(player_index);
	update_server_add_player(player_index);

	csmemset(bot, 0, sizeof(*bot));
	bot->active = TRUE;
	bot->slot = bot_index;
	bot->player_index = player_index;
	bot->unit_index = NONE;
	bot->target_player_index = NONE;
	bot->opportunity_index = NONE;
	bot->opportunity_team_index = NONE;
	bot->retreat_start_time = NONE;
	bot->progress_time = NONE;
	bot->surface_nav_wait_time = NONE;
	engine_ai_rng32_seed(&bot->rng, bots_globals.epoch * 7919u + (uint64_t)now, (uint64_t)bot_index);
	{
		struct player_datum *player = bot_player(bot);
		long team_index = game_engine_has_teams() && player ? player->team_index : NONE;
		long team_rank = team_index != NONE ? bots_team_rank(bot) : NONE;

		bot_navigation_agent_initialize(&bot->navigation_agent, bot_index + 1, team_index, team_rank);
		if (bot->navigation_agent.lane_assigned)
		{
			platform_log("bots: bot %d assigned team %ld lane %d squad %ld side %d phase %d",
				bot_index + 1, team_index, bot->navigation_agent.lane_index,
				bot->navigation_agent.squad_index, bot->navigation_agent.lane_side,
				bot->navigation_agent.lane_phase);
		}
	}
	engine_ai_behavior_begin(&bot->behavior, &bot_behavior);
	bots_globals.had_bots = TRUE;
	platform_log("bots: %s joined (%s) at tick %ld", name, bots_globals.skill->name, now);
	return TRUE;
}

/* a bot leaves as a player who quits does (game_update_quit_players) */
static void bot_leave(
	struct bot *bot)
{
	struct player_datum *player = bot_player(bot);

	if (player && player->quit_out_of_game_time == NONE)
		player->quit_out_of_game_time = game_time_get();
	bot_drop_opportunity(bot, "left match");
	bot_manager_release_bot(player ? player->team_index : NONE, (short)(bot->slot + 1));
	machine_remove_player(bot->player_index);
	platform_log("bots: bot %d team %ld cleared item claims (left match)", bot->slot + 1,
		player ? player->team_index : NONE);
	bot->active = FALSE;
	platform_log("bots: a bot left (solo Slayer host gate no longer permits bots)");
}

/* ---------- scenario fixture (bot_scenario.h) */

long bots_scenario_player_index(
	short slot)
{
	struct bot *bot;

	if (slot < 0 || slot >= BOTS_MAXIMUM)
		return NONE;
	bot = &bots_globals.bots[slot];
	return bot->active && bot_player(bot) ? bot->player_index : NONE;
}

/* a trial's start: the last life's decisions, target, route and claims go;
the bot faces yaw, alert at once, its numbers from seed */
boolean bots_scenario_prepare(
	short slot,
	real yaw,
	uint32_t seed)
{
	struct bot *bot;
	struct player_datum *player;

	if (slot < 0 || slot >= BOTS_MAXIMUM || !bots_globals.bots[slot].active)
		return FALSE;
	bot = &bots_globals.bots[slot];
	player = bot_player(bot);
	if (!player || !bot_living_unit(player->unit_index))
		return FALSE;
	engine_ai_rng32_seed(&bot->rng, (uint64_t)seed, (uint64_t)slot);
	bot_reset_life(bot, player->unit_index);
	bot->facing.yaw = bot_angle_normalize(yaw);
	bot->facing.pitch = 0.f;
	bot->orient_yaw = bot->facing.yaw;
	bot->movement_escape_yaw = bot->facing.yaw;
	bot->aware_time = game_time_get();
	bot->strafe_sign = 1.f;
	bot->last_trigger = 0.f;
	bot->last_control_flags = 0;
	csmemset(bot->scores, 0, sizeof(bot->scores));
	return TRUE;
}

/* a trial's start: no team sighting, claim or callout from the last trial */
void bots_scenario_forget(
	void)
{
	bot_manager_reset();
}

boolean bots_scenario_set_skill(
	char const *name)
{
	short skill_index;

	for (skill_index = 0; skill_index < NUMBEROF(bot_skills); skill_index++)
	{
		if (name && !csstrcasecmp(name, bot_skills[skill_index].name))
		{
			bots_globals.skill = &bot_skills[skill_index];
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------- extension registration (source/extensions/extension_api.h) */

/* each tick, before the players' actions: the navigation world first, then
the host's bots join or leave and decide their actions for its next update,
as its local players' input goes there */
static void bots_controller_update(
	void)
{
#ifdef HALO_TRACE_ENABLED
	halo_trace_zone_begin(HALO_TRACE_ZONE_BOT_AI);
#endif
	navigation_world_update_for_players();
	map_anchors_update();
	bots_update();
#ifdef HALO_TRACE_ENABLED
	halo_trace_zone_end(HALO_TRACE_ZONE_BOT_AI);
#endif
}

/* the walkable resource and the anchors placed on it go together */
static void bots_navigation_reset(
	void)
{
	bot_navigation_reset_all();
	vehicle_navigation_reset_all();
	navigation_world_reset();
	map_anchors_reset();
}

/* port: bots.team_spawns. In a team game with bots, each player spawns in
the half of the map nearer its own base (bot_navigation_team_bases: the CTF
flags, which every map places in its bases), as a team plays from its side
of the field. Halo's own Team Slayer spawns anywhere a team's starting
locations allow, which on most maps puts both teams in both bases. Only
this host's game is changed: bots never play with other machines. */
static real bots_starting_location_rating(
	long player_index,
	union real_point3d const *position)
{
	struct player_datum *player;
	real_point3d home;
	real_point3d enemy;
	real home_x;
	real home_y;
	real enemy_x;
	real enemy_y;

	if (!bots_globals.team_spawns || (bots_globals.desired_count == 0 && !bots_globals.had_bots) ||
		!game_engine_has_teams() || !position || !valid_real_point3d(position))
		return 1.f;
	player = player_try_and_get(player_index);
	if (!player || !bot_navigation_team_bases(player->team_index, &home, &enemy))
		return 1.f;
	home_x = position->x - home.x;
	home_y = position->y - home.y;
	enemy_x = position->x - enemy.x;
	enemy_y = position->y - enemy.y;
	return home_x * home_x + home_y * home_y <= enemy_x * enemy_x + enemy_y * enemy_y ?
		1.f : BOT_ENEMY_HALF_SPAWN_RATING;
}

/* the map left: its derived navigation, and a scenario's arena */
static void bots_dispose_from_old_map(
	void)
{
	bot_scenario_dispose_from_old_map();
	bots_navigation_reset();
}

static struct halo_player_controller const bots_controller =
{
	.update = bots_controller_update,
	.controls_player = bots_player_is_bot,
	.starting_location_rating = bots_starting_location_rating,
};

struct halo_extension const bots_extension =
{
	.name = "bots",
	/* no bots yet; they join once the game is under way */
	.objects_placed = bots_initialize_for_new_map,
	/* derived, host-local navigation never outlives its map, a restored
	game or the tags it was built from */
	.dispose_from_old_map = bots_dispose_from_old_map,
	.invalidate_derived_state = bots_navigation_reset,
	/* a game bots played in is not reported to the game list, Delta Stats
	or the event log */
	.suppress_game_report = bots_game_had_bots,
	.player_controller = &bots_controller,
#ifdef HALO_EXTENSION_WORLD_GEOMETRY
	/* port: debug.bot_scenario's arena, and its camera and overlay */
	.editor = &bot_scenario_editor,
	.world_geometry = &bot_scenario_geometry,
#endif
};

#endif /* HALO_FEATURE_BOTS */
