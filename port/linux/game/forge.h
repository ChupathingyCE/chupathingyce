/*
FORGE.H

Forge (forge.c): an editor in a multiplayer map, or in a campaign mission
(its AI characters: forge_ai.inc). In a local game started in
Forge mode (the Multiplayer menu's FORGE, forge_menu.c, or "forge start
<map>") the player is the monitor, flying, and
spawns, grabs, turns and deletes the map's objects and places the layout's
spawn points, teleporters and game type objects within a budget, or plays
the map as the Spartan, Back and the right stick's click together flipping
between them. A layout saved (forge_files.h, forge_store.h) is played as a
local game of a game type (the lobby's PLAY, or "forge play <map> <layout>
<type>"). Off unless
game.forge is true: nothing of it runs until then.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __FORGE_H
#define __FORGE_H

#include "cseries.h"
#include "real_math.h"
#include "forge_files.h"

/* ---------- constants */

/* the most a layout may hold, objects and markers, and AI characters alive
at once: the build's (game.forge_objects and game.forge_ai choose within
them: forge_objects_budget, forge_ai_maximum). A desktop build's, within
its engine's tables (halo_port_capacity.h: 8192 objects, 1024 drawn a
frame, 1024 actors, 8192 props, 128 swarms) with room left: 512 objects
and 128 characters (their weapons, about 400 objects) and the map's own
fit what a frame draws; 128 characters alive kept about 2,300 props (of
8192) in a measured fight; a build with
the console's tables (HALO_CONSOLE_LIMITS: 2048 objects, 256 actors, 768
props), the console's numbers */
#ifdef HALO_CONSOLE_LIMITS
#define FORGE_BUILD_MAXIMUM_OBJECTS 256
#define FORGE_BUILD_MAXIMUM_AI 32
#else
#define FORGE_BUILD_MAXIMUM_OBJECTS 512
#define FORGE_BUILD_MAXIMUM_AI 128
#endif
/* the settings' least */
#define FORGE_MINIMUM_OBJECTS 64
#define FORGE_MINIMUM_AI 8

enum
{
	/* the budget of a session's table: objects and markers placed (the
	budget itself: forge_objects_budget) */
	FORGE_MAXIMUM_OBJECTS = FORGE_BUILD_MAXIMUM_OBJECTS,
	/* a readable name's bytes (UTF-8) */
	FORGE_NAME_SIZE = 64,
	/* the markers of a layout (forge_spawns.inc: player spawn points,
	teleporters, game type objects, 64 of them; AI route points and patrol
	areas, forge_ai_move.inc, 80 more), within the objects' budget */
	FORGE_MAXIMUM_SPAWNS = 144,
	FORGE_SPAWN_TABLE = FORGE_MAXIMUM_SPAWNS,
	/* (forge_ai.inc) the AI characters of a layout alive at once, within
	the objects' budget, at most: past forge_ai_maximum the rest wait for
	room */
	FORGE_MAXIMUM_AI = FORGE_BUILD_MAXIMUM_AI,
};

/* the Forge menu's OBJECTS categories (forge_menu.c), in its order */
enum
{
	_forge_category_vehicles = 0,
	_forge_category_weapons,
	_forge_category_equipment,
	_forge_category_scenery,
	/* (AI characters: the map's actor variants that name their unit, none
	on a multiplayer map: forge_ai.inc) */
	_forge_category_ai,
	/* (on a multiplayer map: the campaign's vehicles and objects, brought
	in from the player's own campaign maps: forge_ai_tags.c) */
	_forge_category_campaign_vehicles,
	_forge_category_campaign_objects,
	/* (teleporters: SENDER and RECEIVER, linked by their channel, no tag of
	the map's: forge_netgame.inc) */
	_forge_category_teleporters,
	/* (game type objects: CTF FLAG, HILL POINT, ODDBALL SPAWN, RACE
	CHECKPOINT, the engine's netgame flags: forge_netgame.inc) */
	_forge_category_objectives,
	/* (the player spawn points: RESPAWN POINT and INITIAL SPAWN, no tag of
	the map's: forge_spawns.inc) */
	_forge_category_spawning,
	/* (AI movement: AI GO HERE, a route's point, and PATROL AREA, no tag of
	the map's: forge_ai_move.inc) */
	_forge_category_ai_routes,
	/* (volumes: KILL VOLUME, DAMAGE VOLUME, GRAVITY VOLUME, zones that act
	on whoever is in them: forge_volumes.inc) */
	_forge_category_volumes,
	/* (the map's effects and decals, placed as markers: forge_effects.inc) */
	_forge_category_effects,
	_forge_category_decals,
	NUMBER_OF_FORGE_CATEGORIES
};

/* an object's physics (the properties' PHYSICS: normal, fixed, phased;
forge_files.h FORGE_RECORD_PHYSICS_*) */
enum
{
	_forge_physics_normal = 0,
	/* never moves: held where the editor put it */
	_forge_physics_fixed,
	/* no collision: players, vehicles and objects pass through it */
	_forge_physics_phased,
	NUMBER_OF_FORGE_PHYSICS_MODES
};

/* a layout's marker's kind (no object: drawn as a marker, forge_spawns.inc):
a player spawn point (the SPAWNING category's items, in its order), a
teleporter's end (the TELEPORTERS category's), a game type object (the
OBJECTIVES category's, each its index in channel - a CTF flag's team, a
hill's number, a ball's, a checkpoint's order) */
enum
{
	_forge_spawn_respawn = 0,
	_forge_spawn_initial,
	NUMBER_OF_FORGE_SPAWN_KINDS,
	_forge_spawn_teleporter_sender = NUMBER_OF_FORGE_SPAWN_KINDS,
	_forge_spawn_teleporter_receiver,
	_forge_spawn_flag,
	_forge_spawn_hill,
	_forge_spawn_oddball,
	_forge_spawn_race,
	/* (forge_ai_move.inc) an AI route's point, its route in channel; a
	patrol area, its route in channel */
	_forge_spawn_ai_route,
	_forge_spawn_ai_area,
	/* (a volume: its kind its type - forge_files.h FORGE_FILE_VOLUME_*'s
	order -, forge_volumes.inc) */
	_forge_spawn_volume_kill,
	_forge_spawn_volume_damage,
	_forge_spawn_volume_gravity,
	/* (an effect or a decal of the map's: forge_effects.inc) */
	_forge_spawn_effect,
	_forge_spawn_decal,
	NUMBER_OF_FORGE_MARKER_KINDS,
	FORGE_LAST_OBJECTIVE_KIND = _forge_spawn_race
};
/* each category of markers: how many kinds */
#define FORGE_TELEPORTER_KINDS 2
#define FORGE_OBJECTIVE_KINDS 4
#define FORGE_AI_ROUTE_KINDS 2
#define FORGE_VOLUME_KINDS 3
/* (forge_netgame.inc) a teleporter's channels (forge_files.h's) */
#define FORGE_TELEPORTER_CHANNELS 16

/* an object's properties (the Forge menu's PROPERTIES rows, in its order): its
team, respawn time, whether it is placed at the start, its exact angles
(yaw, pitch, roll: stepped by rotate mode's snapping, 15 degrees with none)
and their reset, its physics. A marker's rows are the first four: its team
(any, red, blue; a teleporter's channel, a game type object's index), its
game type in the respawn's row, its kind in the place at start's, its yaw */
enum
{
	_forge_property_team = 0,
	_forge_property_respawn,
	_forge_property_place_at_start,
	_forge_property_yaw,
	_forge_property_pitch,
	_forge_property_roll,
	_forge_property_reset_rotation,
	_forge_property_physics,
	/* (an AI character's: forge_ai.inc) its AI team and its AI on or off */
	_forge_property_ai_team,
	_forge_property_ai,
	/* (forge_ai_move.inc) a character's behavior (HOLD, PATROL, PURSUE),
	route and vehicles; a vehicle's route (its AI pilot's or drivers') */
	_forge_property_ai_move,
	_forge_property_ai_route,
	_forge_property_ai_vehicles,
	NUMBER_OF_FORGE_PROPERTIES
};

/* (forge_environment.inc) the sky and fog's options, SKY AND FOG's rows in
order */
enum
{
	_forge_environment_sky = 0,
	_forge_environment_fog,
	_forge_environment_color,
	_forge_environment_density,
	_forge_environment_start,
	_forge_environment_end,
	NUMBER_OF_FORGE_ENVIRONMENT_ROWS
};

/* ---------- structures */

/* the properties of the object or marker held */
struct forge_properties
{
	char name[FORGE_NAME_SIZE];
	/* NONE neutral, 0 red, 1 blue */
	short team;
	/* 0: never */
	short respawn_seconds;
	boolean place_at_start;
	/* its facing as yaw (from +x, about up), pitch (up from the ground)
	and roll (about forward), whole degrees */
	short yaw;
	short pitch;
	short roll;
	/* its physics: _forge_physics_* */
	boolean has_physics;
	short physics;
	/* where the object or marker is, meters (x, y, z) */
	boolean has_position;
	real position[3];
	/* a marker's (forge_spawns.inc): its kind (_forge_spawn_*) and its game
	type, named */
	boolean spawn;
	short spawn_kind;
	char game_type_name[32];
	/* (a teleporter's: its kind is spawn_kind) its channel, and whether the
	channel has the other end (a sender's receiver, a receiver's sender); a
	game type object's index */
	short channel;
	boolean linked;
	/* (an AI character's: forge_ai.inc) its AI team (FORGE_AI_TEAM_*) and
	whether its AI is on; respawn_seconds is 0 when it never respawns */
	boolean character;
	short ai_team;
	boolean ai_on;
	/* (forge_ai_move.inc) a character's behavior (FORGE_FILE_AI_*), route
	(NONE, or 0 up) and whether it takes vehicles; a vehicle's route
	(vehicle set). A route point's stop and a patrol area's size
	(FORGE_FILE_ROUTE_STOP_*, a size index): a marker's option */
	short ai_move;
	short ai_route;
	boolean ai_vehicles;
	boolean vehicle;
	short marker_option;
	/* (a volume's: forge_volumes.inc) its shape, size (forge_files.h's
	quarter world units), amount and TARGET's choice */
	boolean volume;
	unsigned char volume_shape;
	unsigned char volume_size[3];
	unsigned char volume_amount;
	short volume_target_index;
	/* (an effect's or a decal's: forge_effects.inc) its repeat (seconds, 0
	LOOP) or size (tenths) */
	boolean effect;
	short effect_value;
};

/* what the monitor's controls ask of the flying camera this frame */
struct forge_camera_input
{
	real_euler_angles2d facing_delta;
	/* along the camera's yaw: i forward, j left, k up */
	real_vector3d translation;
};

/* an object the Forge menu's OBJECTS offers */
struct forge_item
{
	/* its readable name (UTF-8) */
	char name[FORGE_NAME_SIZE];
	/* how many of it this session has placed that are still there */
	short placed;
	/* (an AI character brought in from a campaign map, on a multiplayer
	map: forge_ai_tags.c) its faction and the mission it came from
	("Covenant", "The Silent Cartographer"), else "" */
	char faction[32];
	char from[FORGE_NAME_SIZE];
};

struct game_variant;
struct player_starting_location;

/* ---------- prototypes/FORGE.C */

/* whether Forge is on (game.forge, HALO_FORGE): off, the console's forge
commands say so and nothing else of Forge runs */
boolean forge_enabled(
	void);

/* a console command that starts with "forge" (hs.c): what follows the
word. TRUE if it was understood */
boolean forge_command(
	char const *arguments);

/* (director.c, each frame before the cameras update) whether this local
player is the monitor: its flying camera then moves by input, and the
player's own controls are not read */
boolean forge_camera_input(
	short local_player_index,
	real seconds,
	struct forge_camera_input *input);

/* (main.c, each frame once the cameras have updated, before the tick
starts) the monitor's buttons and the object it holds */
void forge_update(
	void);

/* (cache_files.c) a map's tags loaded: a Forge game asked for on it starts,
anything of another map's is forgotten */
void forge_map_loaded(
	char const *scenario_name);

/* (hud.c, as the HUD draws) the monitor's HUD: the map, the budget, the
crosshair and what it is on, the notices, the prompts */
void forge_draw(
	short local_player_index);

/* (render.c, with the world's debug geometry) the layout's markers, and the
map's own player spawns, for the monitor */
void forge_render_world(
	void);

/* (director.c) puts a local player's director on the flying camera at
position, looking along forward (fly), or back on the player's own */
void director_forge_camera(
	short local_player_index,
	boolean fly,
	real_point3d const *position,
	real_vector3d const *forward);

/* the maps Forge edits (the Xbox's multiplayer maps): how many, each one's
level path, readable name and short name (bloodgulch: its layouts'
folder); a map by its index, its short name or its level path (NONE) */
short forge_map_count(
	void);
char const *forge_map_path(
	short map_index);
char const *forge_map_name(
	short map_index);
char const *forge_map_short(
	short map_index);
short forge_map_of_short(
	char const *map_name);
short forge_map_of_word(
	char const *word);

/* a local game of the map in Forge mode: the game variant (free for all, no
score or time limit) and the map set, and the game started as "main menu
switch to solo game" starts it; with a layout file of the map
(variant_file, else NULL), that read and checked first, and placed as the
session starts. FALSE, with why, if the layout could not be read: nothing
is started */
boolean forge_game_start(
	short map_index,
	char const *variant_file,
	char *why,
	int why_size);

/* a layout file of the map played as a local game of a game type
(game_type: slayer, teamslayer, ctf, king, oddball, race; NULL or "" for
slayer): its objects placed once the player has spawned, its spawn points
and game type objects the game's. FALSE, with why: nothing is started */
boolean forge_play_start(
	short map_index,
	char const *variant_file,
	char const *game_type,
	char *why,
	int why_size);

/* the Forge game's variant (free for all slayer, no score that can be
reached, no time limit, "Forge"), as forge_game_start plays it */
void forge_variant_build(
	struct game_variant *variant);

/* the layout the session plays (loaded or last saved), "" for none */
char const *forge_variant_title(
	void);

/* whether snapping is on: the object held on a grid and turned in steps */
boolean forge_snap_enabled(
	void);

/* the properties of what the monitor holds (FALSE: nothing it placed) */
boolean forge_properties_get(
	struct forge_properties *properties);

/* the loaded level's name as Forge says it */
void forge_level_name(
	char *name,
	short size);

/* how many of the layout's are in one of the Forge menu's categories */
short forge_group_placed_count(
	short category);

/* (main.c, a map reset: main_reset_map) before the game's state goes, the
layout of a Forge game taken; after it has started again, placed again
once the player has spawned */
void forge_map_resetting(
	void);
void forge_map_reset(
	void);

/* whether a Forge game is played (the session, not a layout played), and
whether its player is the monitor now */
boolean forge_game_active(
	void);
boolean forge_monitor_active(
	void);
/* whether a layout is played (forge play) */
boolean forge_variant_playing(
	void);

/* (hud.c) the Spartan's action prompt left undrawn for the monitor */
boolean forge_monitor_hides_prompt(
	short local_player_index);
/* (player_control.c) the mouse's look left for the monitor's camera */
boolean forge_monitor_takes_mouse(
	short local_player_index);

/* the Forge menu's OBJECTS: a category's name, how many items it has, each
item's name and placed count (FALSE past the end) */
char const *forge_category_name(
	short category);
short forge_category_item_count(
	short category);
boolean forge_category_item(
	short category,
	short item_index,
	struct forge_item *item);

/* the budget: objects and markers placed (the layout's, gone in play or
not) */
short forge_budget_objects(
	void);
/* the most objects and markers a layout may hold (game.forge_objects,
FORGE_MINIMUM_OBJECTS to FORGE_MAXIMUM_OBJECTS), and AI characters alive at
once (game.forge_ai, FORGE_MINIMUM_AI to FORGE_MAXIMUM_AI): read as a Forge
game's map loads */
short forge_objects_budget(
	void);
short forge_ai_maximum(
	void);

/* ---------- prototypes/FORGE_AI_MOVE.INC */

/* (actors.c actor_update, once its action has given its orders) a Forge
character's movement on the map's navigation (ai_nav.c): its route, area, pursuit
or idle spot */
void forge_ai_move_actor_control(
	long actor_index);
/* (actors.c actor_update, before its controls go to its unit) a Forge
character driving a vehicle the AI has no driving for (a Warthog) steered
as a player drives */
void forge_ai_move_actor_output(
	long actor_index);
/* (actor_moving.c actor_path_refresh) a Forge character's path search on
the navigation's copy of the walkable surfaces */
struct path_state;
void forge_ai_move_path_structure(
	long actor_index,
	struct path_state *state);

/* ---------- prototypes/FORGE_MENU_LINK.INC

The Forge menu (forge_menu.c) and the session: what it shows, and what it
asks, done by forge_update before the next tick */

/* what the menu asks */
enum
{
	/* an item of a category (first: the category, second: the item) placed
	in the monitor's hand */
	_forge_menu_ask_place = 0,
	/* a property of what is held (first: _forge_property_*) stepped by
	second (1 or -1) */
	_forge_menu_ask_property,
	_forge_menu_ask_undo,
	_forge_menu_ask_redo,
	_forge_menu_ask_snap,
	/* the movement speed and rotate mode's snapping, stepped by first */
	_forge_menu_ask_speed,
	_forge_menu_ask_rotate_snap,
	_forge_menu_ask_round,
	_forge_menu_ask_spartan,
	/* the layout saved under its title (a new one's: "<map> Layout"), or
	as a new layout titled file (the menu's "<map> Layout 2"...) */
	_forge_menu_ask_save,
	_forge_menu_ask_save_new,
	/* a layout of the session's map (file) loaded in place of this one, or
	deleted */
	_forge_menu_ask_load,
	_forge_menu_ask_delete,
	/* the sky and fog's option first (_forge_environment_*) stepped by
	second (1 or -1) */
	_forge_menu_ask_environment,
	/* (forge_ai.inc) the layout's dead AI characters cleared (first 0), or
	all of them (first 1) */
	_forge_menu_ask_clear_ai,
	NUMBER_OF_FORGE_MENU_ASKS
};

/* the session as the menu shows it */
struct forge_menu_view
{
	/* a Forge game (not a layout played), and whether the monitor flies
	(else the Spartan plays) and holds something */
	boolean session;
	boolean monitor;
	boolean holding;
	/* the session's map (NONE: none), the budget's objects */
	short map_index;
	short objects;
	/* (forge_ai.inc) the layout's AI characters alive, dead, and waiting
	for room */
	short ai_alive;
	short ai_dead;
	short ai_waiting;
	short undo, redo;
	boolean snap;
	short speed;
	char rotate_snap[16];
	/* the layout's title and file ("" for none yet), the title SAVE gives
	it, and the map's name as Forge says it */
	char title[FORGE_FILE_TITLE_SIZE];
	char file[FORGE_FILE_NAME_SIZE];
	char save_title[FORGE_FILE_TITLE_SIZE];
	char level[FORGE_NAME_SIZE];
	/* what the last thing asked came to, and when (system_milliseconds) */
	char status[160];
	unsigned long status_time;
};

void forge_menu_view_get(
	struct forge_menu_view *view);
void forge_menu_ask(
	short ask,
	short first,
	short second,
	char const *file);
/* a movement speed's name (SLOW, NORMAL, FAST) */
char const *forge_speed_name(
	short speed);
/* the PROPERTIES rows of what is held (0: nothing of the layout's), each
one's label and value ("" for an action, RESET ROTATION), and the property
(_forge_property_*) a row is */
short forge_property_count(
	void);
short forge_property_of_row(
	short row);
boolean forge_property_text(
	short row,
	char *label,
	int label_size,
	char *value,
	int value_size);

/* ---------- prototypes/FORGE_SPAWNS.INC */

/* (players.c, a player's spawn with no starting location chosen yet) one of
the layout's player spawn points for the player (absolute index), its
team's and the game type's, free and away from enemies: 1 found (location
filled), 0 none (the map's own are used), 2 wait (some fit, every one is
blocked: the spawn waits a little) */
enum
{
	_forge_spawn_location_map = 0,
	_forge_spawn_location_found,
	_forge_spawn_location_wait,
};
short forge_spawn_location(
	long player_index,
	struct player_starting_location *location);

/* ---------- prototypes/FORGE_AI.INC */

/* (forge_ai_tags.c) whether a map loading is a Forge game's (or a layout
played) of a multiplayer map, with Forge on: its AI characters are brought
in from the player's campaign maps; a mission's name as Forge says it
("The Silent Cartographer" for "b30"), "" for none */
boolean forge_ai_tags_wanted(
	char const *scenario_name);
char const *forge_mission_title(
	char const *short_name);

/* (vehicles.c, a human plane's tick) a Pelican or dropship a player flies
in Forge (forge_flyable.inc): its velocity into the level's structure
this tick taken away */
void forge_flyable_sweep(
	long vehicle_index);
/* (following_camera.c) a unit's chase camera distance scale: a flown
Pelican's or dropship's, else 1 */
real forge_flyable_camera_scale(
	long unit_index);
/* (the same) its hover's lift for the campaign's factor */
real forge_flyable_lift(
	long vehicle_index,
	real factor);

/* (the Forge menu, a multiplayer map) a category's packs brought in from
the campaign maps (the AI's factions, the campaign's vehicles and
objects) and where each came from, or that no campaign map of it is
there: how many lines, each "Covenant: The Silent Cartographer" or "Flood"
with missing set; 0 off a multiplayer map or for another category */
short forge_ai_faction_line(
	short category,
	short line_index,
	char *line,
	int size,
	boolean *missing);
short forge_ai_faction_lines(
	short category);

/* (hs_runtime.c, encounters.c) whether this is a Forge game, or a layout
played, on a campaign mission: the mission's scripts do not run and its
encounters are not created (its own AI and story stay out of it) */
boolean forge_mission_off(
	void);

/* ---------- prototypes/FORGE_VOLUMES.INC */

/* (game.c's game_tick) the layout's volumes act on what is in them: a
Forge game's or a layout played's, a tick at a time */
void forge_volumes_tick(
	void);

/* ---------- prototypes/FORGE_ENVIRONMENT.INC */

struct sky_atmospheric_fog;

/* (scenario.c) the sky a part of the map shows: the layout's in a Forge
game or a layout played, when it has one (definition_index NONE: none, as
ever); else definition_index */
long forge_sky_definition_index(
	long definition_index);

/* (scenario.c) the outdoor fog drawn: the layout's in a Forge game or a
layout played, when it has one; else fog */
struct sky_atmospheric_fog const *forge_outdoor_fog(
	struct sky_atmospheric_fog const *fog);

/* (forge_menu.c's SKY AND FOG) a sky and fog row's value (SKY: MAP'S or the
sky's name; FOG: MAP'S or CUSTOM; FOG COLOR; FOG DENSITY; FOG START; FOG
END) */
boolean forge_environment_text(
	short row,
	char *value,
	int size);


/* ---------- prototypes/FORGE_EFFECTS.INC */

/* (game.c's game_tick) the layout's effects started when due and its decals
laid: a Forge game's or a layout played's, a tick at a time */
void forge_effects_tick(
	void);

/* ---------- prototypes/FORGE_NETGAME.INC */

/* (game.c, before the game type reads the map's netgame flags) the layout's
teleporters and game type objects made the engine's flags for this game */
void forge_netgame_flags_prepare(
	void);

/* (game_engine_ctf.c) whether this game's CTF flags are a layout's */
boolean forge_netgame_custom_flags(
	void);

/* ---------- prototypes/FORGE_MAP_OBJECTS.INC */

/* (objects.c, game_engine.c) a Forge game or a layout played is up: the
map's own objects' hooks are asked */
extern int forge_map_objects_active;

/* (game_engine.c game_engine_update_item_spawn) whether the session takes
an item spawn of the map's away, and the item a spawn made */
boolean forge_map_item_spawn_skipped(
	long equipment_index);
void forge_map_item_spawn_made(
	long equipment_index,
	long object_index);

#endif
