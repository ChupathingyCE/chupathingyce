/*
FORGE.C

Forge: a sandbox editor in a multiplayer map (or a campaign mission, for
its AI characters: forge_ai.inc), played by the local player
(split screen's first) in a local game. The player is the monitor, flying,
and spawns, grabs, moves, turns and deletes the map's objects within a
budget, places the layout's own player spawn points, teleporters and game
type objects (a CTF flag's spot, hill points, oddball spawns, race
checkpoints), and saves the layout as a file (forge_files.h: the format and
its checks; forge_store.h: the folder). A saved layout is played as a
normal game of any game type: its objects placed as the game starts, its
spawn points and flags the game's.

Off unless game.forge is true (config.toml, HALO_FORGE): until then the
console's "forge" commands say so and nothing else of this file runs, and
nothing of a game, its objects or what is sent changes. Network games are
never Forge's: a Forge game is a local game, and a layout played is placed
by the machine that hosts a local game alone (forge_session_update).

The session: FORGE in the Multiplayer menu (the Chupathingy menus, with
game.forge on: forge_menu.c's lobby), or "forge start <map>" in the console,
starts a local game of a multiplayer map in Forge mode (forge_game_start):
free for all slayer with no score that can be reached and no time limit,
the map's own vehicles and weapons, no saved game read or written. When the
map's tags load (forge_map_loaded) the session is that map's, and as soon as
the player has spawned it is the monitor. Another map, or the main menu,
ends it. The lobby (or "forge start <map> <layout>") starts it with a saved
layout placed; its PLAY (or "forge play <map> <layout> [game type]") plays
one as a game of that type.

The monitor and the Spartan, flipped by Back and the right stick's click
together (controls.forge_monitor on the keyboard, or "forge" alone): as the
monitor the player's unit stays where it stood, hidden and unhurt, its
controls not read, and the director flies the flying camera from where the
player's eyes were, by the monitor's controls (forge_camera_input). Flipped
back, the Spartan stands where the monitor was and plays as ever.

The monitor's controls, in the Xbox's terms; the keyboard and mouse have
keys of their own for each (config.toml's controls.forge_*, halo_keyboard.h's
halo_forge_key, read as the actions' keys are), the movement keys fly and
the mouse looks:
- left stick: fly; right stick (or the mouse): look; A: fly up; the left
  stick's click: fly down;
- left trigger held: thrust (four times as fast); holding an object,
  rotate mode: the right stick turns it about the camera's view (across
  yaws it, up and down pitch it, with the right trigger held too across
  rolls it), the left stick pushes it away and pulls it nearer, and White
  steps its snapping (off, 15, 45, 90 degrees); with the right stick's
  click, nudge mode instead (forge_precision.inc);
- right trigger: grab the object or marker under the crosshair, and let
  go of the one held (it falls where it is let go);
- Y: delete the object held, or the one under the crosshair (held half a
  second instead, with game.forge_hold_to_delete: forge_delete_asked);
- X: the Forge menu (forge_menu.c): OBJECTS (the map's objects by
  category, the one chosen spawned in the monitor's hand), PROPERTIES (of
  what is held, or with nothing held of the layout's object or marker the
  crosshair is on: it opens there), LAYOUTS (save, save as new, load, delete)
  and OPTIONS (snap, speed, rotation snap, delete, grab range, held facing,
  undo, redo, new round, Spartan, the controls); what it asks is done here (forge_menu_link.inc);
- B: a copy of the object held or under the crosshair, held;
- White: the movement speed (slow, normal, fast); in rotate mode, its
  snapping;
- Black: the object held (or under the crosshair) set upright.
The HUD's prompts (forge_draw_prompts) say what the buttons do now, in the
prompts' buttons or the keyboard's keys.

The palette is every tag of the loaded map that the Forge menu offers, with a
readable name each (forge_names: our own names for the multiplayer maps'
objects; else the tag's own name made words): vehicles, weapons (grenades
among them; not a vehicle's gun, the flag, the ball or the unfinished
gravity rifle), equipment, and scenery (scenery, machines, controls). Only
the loaded map's.

An object spawned is the game's own: object_placement_data_new and
object_new, as the cheats' object_new spawns a weapon or vehicle, never
deleted on its own (_new_object_never_automatically_delete_bit); moved
with object_set_position, deleted with object_delete. The object under the
crosshair is the nearest the camera's line meets before the structure (a
weapon on the ground, which has no collision model a line would meet, by
its bounding sphere). The budget (forge_placed, the layout) counts the
objects and markers this session placed and has not deleted - one gone in
play (picked up, destroyed) is still the layout's, back with the next
round - at most FORGE_MAXIMUM_OBJECTS: past it the Forge menu's choice is
refused, said on the screen. Its table is a fixed array, each index
checked.

The map's own objects (its scenery, vehicles, weapons and item spawns) are
moved and deleted as the layout's are: one deleted is taken away from the
map's, one grabbed or turned becomes one of the layout's, and the layout
saves which of the map's it takes away (forge_map_objects.inc).

NEW ROUND (the Forge menu's) resets the map as the engine's map_reset does; the
layout, each object at its placed spot (where the editor put it, not where
play took it), is taken as the game's state goes (forge_map_resetting) and
placed again once the player has spawned.

Threads: forge_update runs on the main thread after the cameras update and
before the tick runs (main.c), so no object is touched while the tick
runs. forge_draw reads only what forge_update wrote (the HUD's lines and
the Forge menu's), never an object; forge_render_world draws the markers.

The console's commands (forge_command; each logs what it did, "forge: ..."):
  forge [on|off]           the monitor, flipped or set (a Forge game)
  forge start <map> [<layout>]  a Forge game of a multiplayer map (its
                           short name, bloodgulch), with a saved layout
  forge play <map> <layout> [slayer|ctf|king|oddball|race]  a saved layout
                           played as a local game of that game type
  forge layouts [<map>]    the saved layouts in the log
  forge save <title>       the layout saved (the session's file when the
                           title is its own)
  forge list [category]    the palette (each category's count; a
                           category's items, with their tags)
  forge spawn <name>       the first palette tag with name in its readable
                           name or its tag's, spawned in front of the
                           camera and held
  forge marker respawn|initial|sender|receiver|flag|hill|oddball|race|
                           route|area|kill|damage|gravity  a marker (or a
                           volume) placed and held
  forge effect|decal <name>  the first of the map's effects or decals with
                           name in its name or path placed and held
  forge grab [#id] | drop  the object or marker under the crosshair (or
                           the layout's of that id) grabbed, the held one
                           let go
  forge turn <degrees>     the held (or aimed at) object turned about up
  forge property team|respawn|start|yaw|pitch|roll|resetrot|physics|
                           aiteam|ai|behavior|route|vehicles [-]
                           the held object's property stepped (back with -)
  forge distance <wu>      the held object that far from the camera
  forge environment sky|fog|color|density|start|end [-]
                           the layout's sky and fog's option stepped
  forge snap [on|off]      snapping (the grid), flipped or set
  forge delete             the held (or aimed at) object deleted
  forge deleteall <cat>    every object of a category deleted
  forge undo | redo        the last step undone, the next redone
  forge round              a new round
  forge camera x y z yaw pitch   the monitor's camera put there (degrees)
  forge press <buttons> [frames] | stick lx ly rx ry [frames] | look y p
                           the tests' controller
  forge menu open|close    the Forge menu opened or closed, as X does
  forge clearai dead|all   the layout's dead AI characters, or all of
                           them, cleared (the Forge menu's CLEAR DEAD AI,
                           CLEAR ALL AI)
  forge kill #<id>         (tests) a layout's AI character killed
  forge board #<id>        (tests) the Spartan into a layout's vehicle;
                           "#<id> walk <seat>" at a seat's way in
  forge where | budget | objects | spawns | netflags | ai [move]  the state
                           in the log
  forge map [category]     the map's own objects the editor can take, and
                           the ones the layout takes away, in the log
  forge shot               (tests) a screenshot, as the screenshot key

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "real_math.h"
#include "ai/actor_definitions.h"
#include "ai/actors.h"
#include "ai/ai.h"
#include "ai/ai_script.h"
#include "ai/props.h"
#include "cache/cache_files.h"
#include "camera/observer.h"
#include "devices/device_controls.h"
#include "devices/device_definitions.h"
#include "devices/device_light_fixtures.h"
#include "models/model_definitions.h"
#include "effects/decal_definitions.h"
#include "effects/decals.h"
#include "effects/effect_definitions.h"
#include "effects/effects.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "input/input.h"
#include "interface/hud_messaging.h"
#include "items/equipment_definitions.h"
#include "items/item_definitions.h"
#include "items/items.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "main/main.h"
#include "main/console.h"
#include "hs/hs.h"
#include "objects/damage.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "physics/collisions.h"
#include "physics/physics.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/xbox/rasterizer_xbox_draw_primitives.h"
#include "render/render.h"
#include "render/render_cameras_internal.h"
#include "scenario/scenario.h"
#include "scenario/sky_definitions.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "units/unit_control_data.h"
#include "units/bipeds.h"
#include "units/units.h"
#include "units/vehicle_definitions.h"
#include "units/unit_definitions.h"

#include "forge.h"
#include "forge_files.h"
#include "../src/forge_store.h"
#include "../src/ui_overlay.h"
#include "forge_menu.h"
#include "forge_ai_tags.h"
#include "halo_port_features.h"
#include "ai_nav.h" /* port: port/linux/game/ai_nav.c */
#include "halo_keyboard.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HALO_CONSOLE_NO_FORGE
/* (the consoles leave Forge's hooks out: director.c has no monitor camera
there and the console's input no Forge keys. The Wii U's linker drops the
calls unused, the 360's links every one, so they are nothing here) */
#define director_forge_camera(local_player_index, fly, position, forward) \
	((void)(local_player_index), (void)(fly), (void)(position), (void)(forward))
#define halo_forge_keys() 0UL
#endif

void platform_log(const char *format, ...);
/* xinput_sdl.c's: the mouse's look since the last call */
int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch);
/* port_config.c's */
int config_boolean(char const *name);
long config_integer(char const *name);
char const *config_string(char const *name);
/* ui_overlay.c's: the keyboard and mouse used last (else a controller) */
int ui_overlay_keyboard(void);
long hud_get_font_index(void);
unsigned long system_milliseconds(void);
/* ui_widget.c's: a menu, or a screen of code over them, takes the input */
boolean ui_widgets_take_input(void);
/* game_engine.c's built-in game types */
struct game_variant *build_game_variant_slayer(struct game_variant *variant);
struct game_variant *build_game_variant_team_slayer(struct game_variant *variant);
struct game_variant *build_game_variant_ctf(struct game_variant *variant);
struct game_variant *build_game_variant_king(struct game_variant *variant);
struct game_variant *build_game_variant_oddball(struct game_variant *variant);
struct game_variant *build_game_variant_race(struct game_variant *variant);

/* ---------- constants */

enum
{
	/* the palette's most tags (a multiplayer map has some 60 of these
	groups' tags, a campaign mission some 300) */
	MAXIMUM_FORGE_PALETTE_TAGS = 512,
	/* the movement speeds (White) */
	_forge_speed_slow = 0,
	_forge_speed_normal,
	_forge_speed_fast,
	NUMBER_OF_FORGE_SPEEDS,
	/* a notice's time on the screen */
	FORGE_NOTICE_MILLISECONDS = 3000,
	FORGE_WARNING_MILLISECONDS = 7000,
	/* the undo history's steps */
	FORGE_UNDO_DEPTH = 32,
	/* the HUD's lines (UTF-8) */
	FORGE_LINE_SIZE = 128,
	/* how long an object let go, moved or placed is followed to where it
	comes to rest (its placed spot, forge_placed's home) */
	FORGE_SETTLE_MILLISECONDS = 3000,
	/* (a weapon's, a piece of equipment's) followed longer while it still
	moves - a long fall, a roll down a hill: its spot is where it comes to
	rest */
	FORGE_ITEM_SETTLE_MILLISECONDS = 20000,
};

/* (the scenery group's tag: no header of the game's names it) */
#define FORGE_SCENERY_DEFINITION_TAG 'scen'

/* (world units: one is ten feet, about 3 m) */
#define FORGE_CAMERA_THRUST 4.f
#define FORGE_CAMERA_LOOK_RATE 2.5f
#define FORGE_PICK_RANGE 60.f
/* (game.forge_grab_range) how far the crosshair takes an object or a
marker: "near", "medium", or "far" (FORGE_PICK_RANGE, the default) */
#define FORGE_GRAB_RANGE_NEAR 20.f
#define FORGE_GRAB_RANGE_MEDIUM 35.f
/* (forge_markers.inc) where the crosshair takes a marker: a sphere at just
over its ring */
#define FORGE_MARKER_PICK_RADIUS 0.45f
#define FORGE_PUSH_RATE 3.f
/* (the keyboard's wheel: a notch's push or pull) */
#define FORGE_WHEEL_STEP 0.5f
#define FORGE_MOUSE_TURN 1.f
#define FORGE_MINIMUM_HOLD_DISTANCE 0.5f
#define FORGE_MAXIMUM_HOLD_DISTANCE 40.f
#define FORGE_STICK_DEAD_ZONE 0.15f
/* (game.forge_hold_to_delete) how long the delete button is held to delete */
#define FORGE_DELETE_HOLD_MILLISECONDS 500
/* a placed object's respawn time unless its properties say */
#define FORGE_DEFAULT_RESPAWN_SECONDS 30
/* how far outside the structure's world bounds a layout's object may be
(a vehicle parked at the edge) */
#define FORGE_WORLD_BOUNDS_MARGIN 10.f
/* snapping: the grid along the ground (world units, about 75 cm) and the
turn's step (15 degrees) */
#define FORGE_SNAP_GRID 0.25f
#define FORGE_SNAP_ANGLE 0.2617994f
/* rotate mode: a stick's turn (radians a second, full over), a flick's
(snapping: past the first, back under the second to flick again), the
mouse's look that makes a snapped step, the exact angles' step with no
snapping (degrees) */
#define FORGE_ROTATE_RATE 2.f
#define FORGE_ROTATE_FLICK 0.6f
#define FORGE_ROTATE_REARM 0.3f
#define FORGE_ROTATE_MOUSE_STEP 0.1f
#define FORGE_ANGLE_STEP 15

/* (forge_precision.inc) */
#define FORGE_METRES_PER_WORLD_UNIT 3.048f

/* the nudge's steps, centimetres */
static short const forge_nudge_steps[] = { 5, 10, 25, 1 };

enum
{
	_forge_nudge_ahead,
	_forge_nudge_back,
	_forge_nudge_left,
	_forge_nudge_right,
	_forge_nudge_up,
	_forge_nudge_down,
	NUMBER_OF_FORGE_NUDGES
};

static real const forge_speeds[NUMBER_OF_FORGE_SPEEDS] = { 0.6f, 2.f, 5.f };
/* rotate mode's snapping (White in it), degrees: 0 off */
static short const forge_rotate_snaps[] = { 0, 15, 45, 90 };
/* rotate mode's axes, the exact angles' order */
enum
{
	_forge_axis_yaw,
	_forge_axis_pitch,
	_forge_axis_roll,
	NUMBER_OF_FORGE_AXES
};
static char const *const forge_speed_names[NUMBER_OF_FORGE_SPEEDS] =
{
	"Movement Speed: Slow", "Movement Speed: Normal", "Movement Speed: Fast",
};

/* the monitor's buttons: the game's (input.h) */
enum
{
	_forge_button_fly_up = _gamepad_analog_button_a,
	_forge_button_duplicate = _gamepad_analog_button_b,
	_forge_button_menu = _gamepad_analog_button_x,
	_forge_button_delete = _gamepad_analog_button_y,
	_forge_button_straighten = _gamepad_analog_button_black,
	_forge_button_speed = _gamepad_analog_button_white,
	_forge_button_alternate = _gamepad_analog_button_left_trigger,
	_forge_button_grab = _gamepad_analog_button_right_trigger,
	_forge_button_fly_down = _gamepad_binary_button_left_thumb,
};

/* the Forge menu's categories' names (forge.h's order) */
static char const *const forge_category_names[NUMBER_OF_FORGE_CATEGORIES] =
{
	"VEHICLES", "WEAPONS", "EQUIPMENT", "SCENERY", "AI", "CAMPAIGN VEHICLES", "CAMPAIGN OBJECTS", "TELEPORTERS",
	"OBJECTIVES", "SPAWNING", "AI ROUTES", "VOLUMES", "EFFECTS", "DECALS",
};
/* (the log's and the console's) */
static char const *const forge_category_log_names[NUMBER_OF_FORGE_CATEGORIES] =
{
	"vehicles", "weapons", "equipment", "scenery", "ai", "campaign_vehicles", "campaign_objects", "teleporters",
	"objectives", "spawning", "ai_routes", "volumes", "effects", "decals",
};
/* (forge_spawns.inc) the markers' categories' items (SPAWNING's,
TELEPORTERS', OBJECTIVES'), its kinds' names */
static char const *const forge_spawn_kind_names[NUMBER_OF_FORGE_MARKER_KINDS] =
{
	"Respawn Point", "Initial Spawn", "Teleporter Sender", "Teleporter Receiver",
	"CTF Flag", "Hill Point", "Oddball Spawn", "Race Checkpoint", "AI Go Here", "Patrol Area", "Kill Volume", "Damage Volume",
	"Gravity Volume", "Effect", "Decal",
};
static char const *const forge_spawn_kind_log_names[NUMBER_OF_FORGE_MARKER_KINDS] =
{
	"respawn point", "initial spawn", "teleporter sender", "teleporter receiver",
	"ctf flag", "hill point", "oddball spawn", "race checkpoint", "ai route point", "patrol area", "kill volume", "damage volume",
	"gravity volume", "effect", "decal",
};
/* (the console's "forge marker <word>") */
static char const *const forge_spawn_kind_words[NUMBER_OF_FORGE_MARKER_KINDS] =
{
	"respawn", "initial", "sender", "receiver", "flag", "hill", "oddball", "race", "route", "area", "kill", "damage", "gravity",
	"effect", "decal",
};
/* (forge_ai_move.inc) a patrol area's radius by its size (world units), a
route stop's wait (seconds), and their words */
static real const forge_ai_area_radii[FORGE_FILE_AI_AREA_SIZES] = { 3.f, 5.f, 8.f, 12.f, 20.f };
static short const forge_ai_stop_seconds[NUMBER_OF_FORGE_FILE_ROUTE_STOPS] = { 0, 5, 15, 5 };
static char const *const forge_ai_behavior_names[NUMBER_OF_FORGE_FILE_AI_BEHAVIORS] = { "HOLD", "PATROL", "PURSUE" };
static char const *const forge_ai_behavior_log_names[NUMBER_OF_FORGE_FILE_AI_BEHAVIORS] = { "hold", "patrol", "pursue" };
static char const *const forge_ai_stop_names[NUMBER_OF_FORGE_FILE_ROUTE_STOPS] = { "NONE", "WAIT 5 S", "WAIT 15 S", "LAND" };
/* (forge_environment.inc, the console's "forge environment <word>") the sky
and fog's options, SKY AND FOG's rows' order (forge.h) */
static char const *const forge_environment_row_words[NUMBER_OF_FORGE_ENVIRONMENT_ROWS] =
{
	"sky", "fog", "color", "density", "start", "end",
};
/* a markers' category's first kind and its kinds (NONE: not one) */
static short forge_marker_category_first(
	short category)
{
	return category == _forge_category_spawning ? _forge_spawn_respawn :
		category == _forge_category_teleporters ? _forge_spawn_teleporter_sender :
		category == _forge_category_objectives ? _forge_spawn_flag :
		category == _forge_category_ai_routes ? _forge_spawn_ai_route :
		category == _forge_category_volumes ? _forge_spawn_volume_kill : NONE;
}

static short forge_marker_category_count(
	short category)
{
	return category == _forge_category_spawning ? NUMBER_OF_FORGE_SPAWN_KINDS :
		category == _forge_category_teleporters ? FORGE_TELEPORTER_KINDS :
		category == _forge_category_objectives ? FORGE_OBJECTIVE_KINDS :
		category == _forge_category_ai_routes ? FORGE_AI_ROUTE_KINDS :
		category == _forge_category_volumes ? FORGE_VOLUME_KINDS : 0;
}

/* the maps Forge edits: the Xbox's multiplayer maps, then the campaign
missions (for their AI characters: forge_ai.inc). Their short names are
the layouts' map names (forge_files.h) */
static struct
{
	char const *path;
	char const *name;
	boolean campaign;
} const forge_maps[] =
{
	{ "levels\\test\\beavercreek\\beavercreek", "Battle Creek" },
	{ "levels\\test\\sidewinder\\sidewinder", "Sidewinder" },
	{ "levels\\test\\damnation\\damnation", "Damnation" },
	{ "levels\\test\\ratrace\\ratrace", "Rat Race" },
	{ "levels\\test\\prisoner\\prisoner", "Prisoner" },
	{ "levels\\test\\hangemhigh\\hangemhigh", "Hang 'Em High" },
	{ "levels\\test\\chillout\\chillout", "Chill Out" },
	{ "levels\\test\\carousel\\carousel", "Derelict" },
	{ "levels\\test\\boardingaction\\boardingaction", "Boarding Action" },
	{ "levels\\test\\bloodgulch\\bloodgulch", "Blood Gulch" },
	{ "levels\\test\\wizard\\wizard", "Wizard" },
	{ "levels\\test\\putput\\putput", "Chiron TL-34" },
	{ "levels\\test\\longest\\longest", "Longest" },
	{ "levels\\a10\\a10", "Pillar of Autumn", TRUE },
	{ "levels\\a30\\a30", "Halo", TRUE },
	{ "levels\\a50\\a50", "Truth and Reconciliation", TRUE },
	{ "levels\\b30\\b30", "The Silent Cartographer", TRUE },
	{ "levels\\b40\\b40", "Assault on the Control Room", TRUE },
	{ "levels\\c10\\c10", "343 Guilty Spark", TRUE },
	{ "levels\\c20\\c20", "The Library", TRUE },
	{ "levels\\c40\\c40", "Two Betrayals", TRUE },
	{ "levels\\d20\\d20", "Keyes", TRUE },
	{ "levels\\d40\\d40", "The Maw", TRUE },
};

/* our own names for the multiplayer maps' objects, by the last part of
the tag's name ("_mp" taken off); an ammunition's ("<weapon> ammo") is its
weapon's, "%s Ammo" */
static struct
{
	char const *tag;
	char const *name;
} const forge_names[] =
{
	/* vehicles */
	{ "warthog", "Warthog" },
	{ "rwarthog", "Rocket Warthog" },
	{ "scorpion", "Scorpion" },
	{ "ghost", "Ghost" },
	{ "banshee", "Banshee" },
	{ "c gun turret", "Shade" },
	{ "wraith", "Wraith" },
	{ "pelican", "Pelican" },
	{ "c_dropship", "Covenant Dropship" },
	/* (the campaign's objects, brought in from its maps: forge_ai_tags.c) */
	{ "c_storage", "Covenant Crate" },
	{ "c_storage_large", "Large Covenant Crate" },
	{ "c_field_generator", "Covenant Shield Generator" },
	{ "c_uplink", "Covenant Uplink" },
	{ "landing beacon", "Landing Beacon" },
	{ "pelican_crashed", "Crashed Pelican" },
	{ "c_dropship_crashed", "Crashed Covenant Dropship" },
	{ "metal_small", "Metal Barricade" },
	{ "metal_tall", "Tall Metal Barricade" },
	{ "metal_wide", "Wide Metal Barricade" },
	{ "door small", "Forerunner Door" },
	{ "holo display square", "Holo Display" },
	{ "holo display tall", "Tall Holo Display" },
	{ "holo display wide", "Wide Holo Display" },
	{ "holo control ring", "Holo Control Ring" },
	{ "holo control", "Holo Control Panel" },
	{ "light fixture ceiling1", "Forerunner Ceiling Light" },
	{ "light fixture floor1", "Forerunner Floor Light" },
	{ "light fixture floor2", "Forerunner Floor Light 2" },
	{ "light fixture wall1", "Forerunner Wall Light" },
	{ "light fixture wall3", "Forerunner Wall Light 2" },
	{ "tree_leafy", "Leafy Tree" },
	{ "tree_leafy_medium", "Medium Leafy Tree" },
	{ "tree_leafy_doublewide", "Wide Leafy Tree" },
	{ "tree_leafy_fallentrunk", "Fallen Trunk" },
	{ "tree_leafy_stump", "Tree Stump" },
	{ "vines_hanging", "Hanging Vines" },
	/* weapons */
	{ "assault rifle", "Assault Rifle" },
	{ "pistol", "Pistol" },
	{ "shotgun", "Shotgun" },
	{ "sniper rifle", "Sniper Rifle" },
	{ "rocket launcher", "Rocket Launcher" },
	{ "flamethrower", "Flamethrower" },
	{ "plasma pistol", "Plasma Pistol" },
	{ "plasma rifle", "Plasma Rifle" },
	{ "needler", "Needler" },
	{ "plasma_cannon", "Fuel Rod Gun" },
	{ "energy sword", "Energy Sword" },
	{ "frag grenade", "Frag Grenade" },
	{ "plasma grenade", "Plasma Grenade" },
	/* equipment */
	{ "active camouflage", "Active Camouflage" },
	{ "over shield", "Overshield" },
	{ "health pack", "Health Pack" },
	{ "double speed", "Double Speed" },
	{ "full-spectrum vision", "Full-Spectrum Vision" },
	/* scenery */
	{ "flag_base", "Flag Base" },
	{ "floor_arrow", "Floor Arrow" },
	{ "teleporter", "Teleporter" },
	{ "teleporter_base", "Teleporter Base" },
	{ "small blue beacon", "Small Blue Beacon" },
	{ "small red beacon", "Small Red Beacon" },
	{ "landing beacon", "Landing Beacon" },
	{ "blue landing beacon", "Blue Landing Beacon" },
	{ "hilltop", "Hill Marker" },
	{ "c_storage", "Covenant Crate" },
	{ "c_storage_large", "Large Covenant Crate" },
	{ "c_field_generator", "Field Generator" },
	{ "c_uplink", "Covenant Uplink" },
	{ "h_barricade_large", "Large Barricade" },
	{ "h_barricade_large_gap", "Large Barricade, Gap" },
	{ "h_barricade_small", "Small Barricade" },
	{ "h_barricade_small_visor", "Small Barricade, Visor" },
	{ "glowingdrip", "Glowing Drip" },
	{ "boulder", "Boulder" },
	{ "boulder_crouch", "Low Boulder" },
	{ "boulder_doublewide", "Wide Boulder" },
	{ "boulder_large_grey", "Large Gray Boulder" },
	{ "a50_rock_large", "Large Rock" },
	{ "snowrock", "Snow Rocks" },
	{ "snowrocksmall", "Small Snow Rocks" },
	{ "shrub_large", "Large Shrub" },
	{ "plant_broadleaf_short", "Short Broadleaf" },
	{ "plant_broadleaf_tall", "Tall Broadleaf" },
	{ "plant fern", "Fern" },
	{ "tree_desert_dead", "Dead Tree" },
	{ "tree_desert_whitebark", "Whitebark Tree" },
	{ "tree_leafy", "Leafy Tree" },
	{ "tree_leafy_medium", "Medium Leafy Tree" },
	{ "tree_leafy_sapling", "Sapling" },
	{ "tree_leafy_fallentrunk", "Fallen Trunk" },
	{ "tree_leafy_stump_crouch", "Tree Stump" },
	{ "tree_pine", "Pine Tree" },
	{ "tree_pine_snow", "Snowy Pine" },
	{ "tree_pine_snowsmall", "Small Snowy Pine" },
	{ "doorblinker", "Door Light" },
	{ "metal_small", "Small Metal Block" },
	{ "metal_tall", "Tall Metal Block" },
	{ "metal_wide", "Wide Metal Block" },
	{ "door_blast_collision", "Blast Door" },
	{ "h oxy tank", "Oxygen Tank" },
	{ "alert_light", "Alert Light" },
	{ "wiz_alert_light", "Alert Light" },
};


/* ---------- structures */

struct forge_pad
{
	boolean down[NUMBER_OF_GAMEPAD_BUTTONS];
	/* -1 to 1, the dead zone taken off */
	real sticks[NUMBER_OF_GAMEPAD_STICKS][2];
	/* the mouse's look this frame, radians */
	real look_yaw;
	real look_pitch;
	/* the monitor and the Spartan's combo held (Back + the right stick's
	click, or controls.forge_monitor) */
	boolean toggle;
	/* the keyboard's: controls.forge_fast held (thrust, as the left trigger
	is with nothing held), and controls.forge_nearer and _farther's wheel
	(what is held brought nearer, or sent away) */
	boolean fast;
	boolean nearer;
	boolean farther;
	/* the keyboard's delete key (controls.forge_delete), apart from the
	controller's Y: game.forge_hold_to_delete may treat them apart */
	boolean delete_key;
};

/* a tag the Forge menu offers */
struct forge_palette_entry
{
	long tag_index;
	short category;
	char name[FORGE_NAME_SIZE];
};

/* an object's place and facing */
struct forge_transform
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
};

/* an object this session placed: the layout (the budget's, what a layout
file saves and what a new round places again). An object gone in play
(equipment picked up, a grenade thrown) keeps its entry, its object NONE:
it is still the layout's and comes back with the next round, or after its
respawn time (forge_item_life_update); only the editor's delete (or undo)
takes an entry out */
/* (forge_ai_move.inc) a placed character's AI movement settings (a layout
record's, version 12): its behavior (FORGE_FILE_AI_*), its route (0 none,
else 1 up) and options (FORGE_FILE_AI_OPTION_*); a vehicle's route alone */
struct forge_ai_move_settings
{
	unsigned char behavior;
	unsigned char route;
	unsigned char options;
};

struct forge_placed
{
	long object_index;
	/* the palette tag it was spawned from (an actor variant for an AI
	character: ai) */
	long tag_index;
	boolean ai;
	/* its properties (a layout's record's): team (NONE neutral), respawn
	time, FORGE_RECORD_* flags; an AI character's AI team
	(FORGE_AI_TEAM_*) */
	signed char team;
	unsigned char flags;
	unsigned short respawn_seconds;
	unsigned char ai_team;
	/* (forge_ai_move.inc) a character's behavior, route and vehicles, a
	vehicle's route */
	struct forge_ai_move_settings move;
	/* its own number this session (the undo history names it by this) */
	long id;
	/* where the editor put it (its placed spot: saved, and where a new
	round puts it), followed while it is held and for a moment after it
	is let go or moved (it falls, it settles: settle_until), never by play
	(a Warthog driven away) */
	struct forge_transform home;
	boolean has_home;
	unsigned long settle_until;
	/* (an item) the game tick it was first seen gone at, plus one; 0 while
	it is there: its respawn counts from it. (An AI character) the tick it
	was first seen dead at, plus one: its respawn and its body's clearing
	count from it */
	long dead_tick;
	/* (an AI character) not placed yet for want of room - FORGE_MAXIMUM_AI
	alive already -, placed as soon as one alive is fewer; no object
	meanwhile */
	boolean waiting;
	/* (forge_physics.inc) its collision switch set by its PHASED physics */
	boolean phased_applied;
};

/* a step of the undo history */
enum
{
	_forge_undo_place,
	_forge_undo_delete,
	_forge_undo_move,
	NUMBER_OF_FORGE_UNDO_KINDS
};

struct forge_undo
{
	short kind;
	/* the object: a placed one's id, else (NONE) a map object's index */
	long id;
	long object_index;
	/* what brings it back: its tag, its properties */
	long tag_index;
	signed char team;
	unsigned char flags;
	unsigned short respawn_seconds;
	unsigned char ai_team;
	struct forge_ai_move_settings move;
	/* where it was before the step, and after */
	struct forge_transform from;
	struct forge_transform to;
};

/* (forge_spawns.inc) a marker of the layout: a player spawn point, a
teleporter's end or a game type object. No object: drawn as a marker
(forge_markers.inc), saved as its layout record */
struct forge_spawn
{
	/* its own number this session (the placed objects' numbers' series) */
	long id;
	/* _forge_spawn_* */
	short kind;
	/* NONE any, 0 red, 1 blue */
	signed char team;
	/* FORGE_FILE_GAME_TYPE_* bits, 0 for all */
	unsigned short game_types;
	/* (a teleporter's: forge_netgame.inc) its channel, 0 to
	FORGE_TELEPORTER_CHANNELS - 1; a game type object's index */
	short channel;
	/* (an AI route point's: forge_ai_move.inc) its stop
	(FORGE_FILE_ROUTE_STOP_*); a patrol area's size (0 to
	FORGE_FILE_AI_AREA_SIZES - 1) */
	short option;
	/* where, facing along the ground (a volume: its floor's middle) */
	struct forge_transform home;
	/* (a volume's: forge_volumes.inc) its shape, size (quarter world units:
	half width, half length, height; a sphere's radius), amount and target
	(FORGE_FILE_VOLUME_*) */
	unsigned char volume_shape;
	unsigned char volume_size[3];
	unsigned char volume_amount;
	unsigned char volume_target;
	/* (an effect's or a decal's: forge_effects.inc) its tag, its repeat
	(seconds; 0 again as soon as it ends) or size (tenths); what it shows
	now: its effect, when it started, the decal datums it laid, whether it
	was started this round (the round's count) */
	long effect_tag_index;
	unsigned char effect_value;
	long effect_index;
	long effect_start_tick;
	long decal_pieces[4];
	short decal_piece_count;
	boolean effect_started;
	long effect_round;
};

/* (the players a round's spawns are kept for: absolute indices) */
#define FORGE_SPAWN_PLAYERS 32

typedef char verify_forge_file_records[FORGE_FILE_MAXIMUM_RECORDS >= FORGE_MAXIMUM_OBJECTS ? 1 : -1];

/* ---------- globals */

/* (flying_camera.c) set while the monitor flies: the flying camera moves
along its pitch too */
int forge_monitor_flying = 0;
/* (objects.c) set while forge_place makes the object chosen: the game
type's weapon, vehicle and equipment sets do not put another in its place
(a multiplayer game may make a flamethrower a rocket launcher) */
int forge_placing_exact = 0;

static struct
{
	/* the session: a Forge game asked for, then played */
	boolean game_requested;
	char game_map[256];
	boolean game_active;
	/* a game asked for from within a game: the map loads again first - the
	session never starts on the map still loaded, which may be the same
	one (forge_map_loaded clears it) */
	boolean reload_pending;
	/* the monitor */
	boolean active;
	short local_player_index;
	/* the player's unit while it waits, and what it was before */
	long unit_index;
	boolean unit_could_take_damage;
	boolean unit_cast_shadow;
	/* the object held, how far from the camera, and its facing as held */
	long held_object_index;
	real held_distance;
	real_vector3d held_forward;
	real_vector3d held_up;
	short speed;
	/* the palette, category by category, each by name */
	struct forge_palette_entry palette[MAXIMUM_FORGE_PALETTE_TAGS];
	short palette_count;
	short palette_first[NUMBER_OF_FORGE_CATEGORIES];
	short palette_counts[NUMBER_OF_FORGE_CATEGORIES];
	boolean palette_built;
	/* the budget */
	struct forge_placed placed[FORGE_MAXIMUM_OBJECTS];
	short placed_count;
	/* (forge_ai.inc) the layout's AI characters, those not alive among
	them (dead, or waiting for room), and the waiting: only the living count
	against FORGE_MAXIMUM_AI and the objects' budget */
	short ai_count;
	short ai_dead_count;
	short ai_waiting_count;
	/* (forge_ai.inc) the session's map is a campaign mission */
	boolean campaign;
	/* the buttons down last frame */
	boolean was_down[NUMBER_OF_GAMEPAD_BUTTONS];
	boolean toggle_was_down;
	boolean nearer_was_down;
	boolean farther_was_down;
	/* (forge press, forge stick) the buttons and sticks the tests hold */
	boolean test_down[NUMBER_OF_GAMEPAD_BUTTONS];
	boolean test_toggle;
	short test_buttons_frames;
	real test_sticks[NUMBER_OF_GAMEPAD_STICKS][2];
	short test_sticks_frames;
	/* (forge look) the mouse's look the tests give, radians, taken once */
	real test_look_yaw;
	real test_look_pitch;
	/* the mouse's turn of the object held (forge_camera_input's, for
	forge_update): across, and up and down */
	real pending_mouse_turn;
	real pending_mouse_pitch;
	/* rotate mode: on this frame (L held while holding an object), its
	snapping (forge_rotate_snaps' index), and, snapping, each axis's stick
	flicked (let back to flick again) and the mouse's look not yet a
	step */
	boolean rotating;
	short rotate_snap;
	boolean rotate_flicked[NUMBER_OF_FORGE_AXES];
	real rotate_mouse[NUMBER_OF_FORGE_AXES];
	/* what the monitor did since it started, for the log */
	long spawned_count;
	long deleted_count;
	/* the HUD's lines (forge_draw), written by forge_update */
	char hud_title[FORGE_LINE_SIZE];
	char hud_budget[FORGE_LINE_SIZE];
	char hud_label[FORGE_LINE_SIZE];
	boolean hud_label_held;
	/* (holding an object) its angles and rotate mode's snapping */
	char hud_rotate[FORGE_LINE_SIZE];
	char notice[FORGE_LINE_SIZE];
	unsigned long notice_until;
	/* (a menu that pauses the game is over it: no HUD) */
	boolean hud_hidden;
	/* the layout the session plays (its title, its file: a save under its
	title replaces it), one read for the session to apply as it starts
	(forge_variant_layout) */
	char variant_title[FORGE_FILE_TITLE_SIZE];
	char variant_file[FORGE_FILE_NAME_SIZE];
	boolean variant_pending;
	/* what the last thing asked from the Forge menu came to */
	char menu_status[FORGE_STORE_WHY_SIZE];
	/* the undo history: undo_count steps, the first undo_cursor of them
	done (undo takes the one before the cursor, redo the one at it); the
	next placed id; where the object held was when it was grabbed, and
	whether it was placed by this hold (its drop is a placing, not a
	move) */
	struct forge_undo undo[FORGE_UNDO_DEPTH];
	short undo_count;
	short undo_cursor;
	long next_id;
	struct forge_transform held_from;
	boolean held_spawned;
	/* snapping on */
	boolean snap;
	/* a new round: asked (round_requested, done at the next forge_update
	with no menu over the game), the map reset by the engine
	(main_reset_map); the layout taken as the map resets
	(forge_map_resetting: round_layout) and placed again once the player
	has spawned (round_pending), the monitor again where it was if it flew
	(round_monitor, round_camera) */
	boolean round_requested;
	boolean round_resetting;
	boolean round_pending;
	boolean round_monitor;
	struct forge_transform round_camera;
	long round_count;
	/* the session is a layout played (forge play: a game of its game type
	with the layout's objects), not Forge: no monitor, no editing, the
	layout placed as the game starts */
	boolean playing;
	/* (forge_markers.inc) the marker the monitor holds (its id; 0 none),
	where it is held, and how many markers the last frame drew */
	long held_marker_id;
	real_point3d held_marker_position;
	short markers_drawn;
	/* (forge_spawns.inc) the layout's markers; the players that spawned
	this round (a bit each: a round's first spawn takes the initial spawns)
	and since when each waits for a blocked one */
	struct forge_spawn spawns[FORGE_SPAWN_TABLE];
	short spawn_count;
	unsigned long spawns_spawned;
	long spawn_wait_since[FORGE_SPAWN_PLAYERS];
	/* (the console's forge spawns) the map's own spawns the last frame drew */
	short map_markers_drawn;
	/* (forge_precision.inc) L's mode nudges instead of turning; the nudge's
	step (forge_nudge_steps' index), the hold's offset and the object it is
	of, each axis's stick flicked */
	boolean nudge_mode;
	short nudge_step;
	real_vector3d held_nudge;
	long nudge_object;
	boolean nudge_flicked[3];
	/* (the Forge menu's status line: when menu_status was written) */
	unsigned long menu_status_time;
	/* (game.forge_hold_to_delete) the delete button held since (system time, 0
	not held), done for this hold, how far along (0 to 1, for the HUD's
	fill); the delete that needs no hold down last frame */
	unsigned long delete_hold_since;
	boolean delete_hold_done;
	real delete_hold_fraction;
	boolean delete_press_was_down;
	/* (the Forge menu's PROPERTIES) with nothing held, the layout's object
	(its object index, NONE) or marker (its id, 0) the crosshair was on as
	X opened the menu: the properties are its while the menu is open; and
	whether the crosshair is on one now (the prompt's PROPERTIES) */
	long properties_object_index;
	long properties_marker_id;
	boolean hud_properties;
	/* (game.forge_held_facing "view") the hold the facing follows the view
	in (the object held, or the marker's id; NONE and 0 none), whether it
	still does (not once turned by hand), the camera's yaw it was last
	turned to and the facing it was given then */
	long follow_object_index;
	long follow_marker_id;
	boolean following;
	real follow_yaw;
	real_vector3d follow_forward;
} forge_globals = { FALSE, "", FALSE, FALSE, FALSE, 0, NONE, FALSE, NONE, 0.f, { 0.f }, { 0.f }, _forge_speed_normal };

/* ---------- private code */

/* ---- the markers (forge_markers.inc, forge_spawns.inc, forge_netgame.inc,
included at the end of this file for these statics) */
static boolean forge_markers_shown(void);
static long forge_marker_under_crosshair(real *distance);
static void forge_marker_grab(long id, real distance);
static void forge_marker_held_update(void);
static void forge_marker_drop(void);
static struct forge_spawn *forge_spawn_of_id(long id);
static short forge_spawns_of_kind(short kind);
static void forge_spawn_name(struct forge_spawn const *spawn, char *name, int size);
static void forge_spawns_clear(void);
static void forge_spawns_from_layout(struct forge_file_layout const *layout);
static short forge_spawns_capture(struct forge_file_layout *layout);
static void forge_spawn_property_apply(struct forge_spawn *spawn, short property, short step, boolean held);
static void forge_spawn_properties_get(struct forge_spawn const *spawn, struct forge_properties *properties);
static void forge_spawn_choose(short kind);
static void forge_spawn_delete(long id);
static void forge_spawn_log(char const *what, struct forge_spawn const *spawn);
/* ---- the volumes (forge_volumes.inc) */
static boolean forge_spawn_is_volume(struct forge_spawn const *spawn);
static short forge_volumes_count(void);
static void forge_volume_defaults(struct forge_spawn *spawn);
static void forge_volume_from_record(struct forge_spawn *spawn, struct forge_file_record const *record);
static void forge_volume_to_record(struct forge_spawn const *spawn, struct forge_file_record *record);
static boolean forge_volume_property_apply(struct forge_spawn *spawn, short property, short step);
static void forge_volume_properties_get(struct forge_spawn const *spawn, struct forge_properties *properties);
static void forge_volume_property_text(struct forge_properties const *properties, short row, char *label,
	int label_size, char *value, int value_size);
static void forge_volume_log(char const *what, struct forge_spawn const *spawn);
struct forge_marker;
static real forge_volume_bounding_radius(struct forge_spawn const *spawn);
static void forge_volume_color(struct forge_spawn const *spawn, struct forge_marker *marker);
static void forge_volume_draw(struct forge_spawn const *spawn, struct forge_marker const *marker);
static boolean forge_properties_hovered(long *object_index, long *marker_id);
/* ---- the effects and decals (forge_effects.inc) */
static boolean forge_spawn_is_effect(struct forge_spawn const *spawn);
static short forge_effects_count(short kind);
static void forge_effect_palette_reset(void);
static short forge_effect_category_count(short category);
static boolean forge_effect_category_item(short category, short item_index, struct forge_item *item);
static void forge_effect_choose(short category, short item_index);
static boolean forge_effect_choose_named(short category, char const *name);
static void forge_effect_name(struct forge_spawn const *spawn, char *name, int size);
static void forge_effect_log(char const *what, struct forge_spawn const *spawn);
static void forge_effect_release(struct forge_spawn *spawn);
static void forge_effect_set(struct forge_spawn *spawn, long tag_index, unsigned char value);
static boolean forge_effect_from_record(struct forge_file_layout const *layout, struct forge_file_record const *record);
static void forge_effects_from_layout_brought_in(struct forge_file_layout const *layout);
static boolean forge_effect_to_record(struct forge_spawn const *spawn, struct forge_file_layout *layout,
	struct forge_file_record *record);
static short forge_effect_property_count(void);
static void forge_effect_property_apply(struct forge_spawn *spawn, short property, short step, boolean held);
static void forge_effect_property_text(struct forge_properties const *properties, short row, char *label,
	int label_size, char *value, int value_size);
static void forge_spawn_yaw_step(struct forge_spawn *spawn, short property, short step, boolean held);
static struct forge_spawn *forge_spawn_add(long id, short kind, signed char team, unsigned short game_types,
	short channel, struct forge_transform const *home);
static void forge_netgame_changed(void);
static boolean forge_spawn_is_teleporter(struct forge_spawn const *spawn);
static boolean forge_spawn_is_objective(struct forge_spawn const *spawn);
static short forge_objective_indices(short kind);
static short forge_objectives_count(short kind, short index);
static boolean forge_marker_kind_room(short kind, short index, boolean notice);
static short forge_marker_new_index(short kind);
static short forge_teleporters_count(short kind, short channel);
static short forge_teleporter_new_channel(short kind);
static void forge_netgame_update(void);
static void forge_nudge_update(struct forge_pad const *pad);
static void forge_nudge_step_next(void);
static void forge_nudge(short direction);
static short forge_physics_mode(struct forge_placed const *placed);
static unsigned char forge_physics_flags(short mode);
static void forge_physics_apply(struct forge_placed *placed);
static void forge_physics_update(void);
static void forge_item_unburied(long object_index);
static char const *const forge_physics_log_names[NUMBER_OF_FORGE_PHYSICS_MODES] = { "normal", "fixed", "phased" };
static void forge_netgame_log(void);
static char const *forge_netgame_flags_warning(void);
static void forge_spawn_remove(struct forge_spawn *spawn);
/* (forge_map_objects.inc) */
static boolean forge_map_object_take(long object_index, boolean keep);
static void forge_map_objects_start(struct forge_file_layout const *layout);
static void forge_map_objects_capture(struct forge_file_layout *layout);
static void forge_map_made_clear(void);
static void forge_map_objects_log(char const *category_word);
static short forge_spawn_points(void);
static void forge_spawn_grounded(real_point3d *position);
static void forge_draw_line(long font_index, rectangle2d const *bounds, short justification, real red, real green,
	real blue, char const *text);
/* ---- the Forge menu's asks (forge_menu_link.inc) */
static void forge_menu_asks_run(void);
static boolean forge_placed_dead(struct forge_placed const *placed);
static short forge_packs_of(short pack_category);
static void forge_campaign_seats_tune(void);
static void forge_flyable_tune(void);
static void forge_ai_count(void);
static short forge_ai_alive(void);
static short forge_ai_dead_only(void);
static void forge_ai_apply(struct forge_placed const *placed);
static void forge_ai_attach(long object_index, long actor_variant_index);
static void forge_ai_hold(void);
static short forge_ai_clear(boolean all);
static void forge_ai_life_update(void);
static boolean forge_ai_property_apply(struct forge_placed *placed, short property, short step);
static void forge_ai_log(void);
struct forge_ai_move_settings;
static void forge_ai_move_update(void);
static void forge_ai_move_capture(struct forge_placed const *placed, unsigned long group_tag, struct forge_file_record *record);
static void forge_ai_move_from_record(struct forge_placed *placed, struct forge_file_record const *record);
static void forge_ai_move_properties_get(struct forge_placed const *placed, struct forge_properties *properties);
static boolean forge_ai_move_property_apply(struct forge_placed *placed, short property, short step);
static void forge_ai_move_log(void);
static boolean forge_campaign_map_loaded(void);
static short forge_campaign_spawn_location(long player_index, struct player_starting_location *location);
static void forge_category_choose(short category, short item_index);
static short forge_map_of_level(char const *level_name);
static void forge_rotate_snap_text(char *text, size_t size);
/* ---- the sky and fog (forge_environment.inc) */
static void forge_environment_clear(void);
static void forge_environment_from_layout(struct forge_file_layout const *layout);
static short forge_environment_capture(struct forge_file_layout *layout);
static void forge_environment_step(short row, short step);

static char const *forge_tag_leaf(
	long tag_index)
{
	char const *name = tag_get_name(tag_index);
	char const *slash = name ? strrchr(name, '\\') : NULL;

	return slash ? slash + 1 : (name ? name : "?");
}

/* our own name of a tag's last part (its "_mp" off), translated; NULL if
it has none */
static char const *forge_names_find(
	char const *leaf)
{
	short index;

	for (index = 0; index < (short)NUMBEROF(forge_names); index++)
	{
		if (!strcmp(forge_names[index].tag, leaf))
			return (forge_names[index].name);
	}
	return NULL;
}

/* a tag's last part as words: a level's prefix ("a50_") off, "_" a space,
each word's first letter a capital */
static void forge_name_words(
	char const *leaf,
	char *name,
	size_t size)
{
	size_t length = 0;
	boolean word_start = TRUE;

	if (leaf[0] >= 'a' && leaf[0] <= 'd' && leaf[1] >= '0' && leaf[1] <= '9' && leaf[2] >= '0' &&
		leaf[2] <= '9' && leaf[3] == '_' && leaf[4])
	{
		leaf += 4;
	}
	for (; *leaf && length + 1 < size; leaf++)
	{
		char character = *leaf == '_' ? ' ' : *leaf;

		if (word_start && character >= 'a' && character <= 'z')
			character = (char)(character - 'a' + 'A');
		word_start = character == ' ';
		name[length++] = character;
	}
	name[length] = 0;
}

/* a tag's readable name (forge_names, else its last part as words) */
static void forge_tag_readable_name(
	long tag_index,
	char *name,
	size_t size)
{
	char leaf[FORGE_NAME_SIZE];
	char const *found;
	size_t length;

	csstrncpy(leaf, forge_tag_leaf(tag_index), sizeof(leaf) - 1);
	leaf[sizeof(leaf) - 1] = 0;
	length = strlen(leaf);
	if (length > 3 && !strcmp(leaf + length - 3, "_mp"))
		leaf[length -= 3] = 0;
	/* (an ammunition: its weapon's name) */
	if (length > 5 && !strcmp(leaf + length - 5, " ammo"))
	{
		char weapon[FORGE_NAME_SIZE];

		leaf[length - 5] = 0;
		found = forge_names_find(leaf);
		if (found)
			snprintf(weapon, sizeof(weapon), "%s", found);
		else
			forge_name_words(leaf, weapon, sizeof(weapon));
		snprintf(name, size, ("%s Ammo"), weapon);
		return;
	}
	found = forge_names_find(leaf);
	if (found)
		snprintf(name, size, "%s", found);
	else
		forge_name_words(leaf, name, size);
}

/* whether no trigger of a weapon tag can be aimed (each fires along its
"primary trigger" marker, where the gun points in the holder's hands, not
where the holder looks: weapons.c weapon_trigger_fire) */
static boolean forge_weapon_cannot_be_aimed(
	long tag_index)
{
	struct weapon_definition *definition = weapon_definition_get(tag_index);
	long trigger_index;

	if (!definition || definition->weapon.triggers.count <= 0)
		return FALSE;
	for (trigger_index = 0; trigger_index < definition->weapon.triggers.count; trigger_index++)
	{
		struct weapon_trigger_definition *trigger = TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers,
			trigger_index, struct weapon_trigger_definition);

		if (!TEST_FLAG(trigger->flags, _weapon_trigger_projectiles_cannot_be_aimed_bit))
			return FALSE;
	}
	return TRUE;
}

/* whether an equipment tag is only the ammunition of weapons nobody can aim
(forge_weapon_cannot_be_aimed): their magazines' ammunition, and no other
weapon's */
static boolean forge_equipment_only_unaimed_ammunition(
	long tag_index)
{
	struct tag_iterator iterator;
	long weapon_tag_index;
	boolean found = FALSE;

	tag_iterator_new(&iterator, WEAPON_DEFINITION_TAG);
	while ((weapon_tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct weapon_definition *definition;
		long magazine_index;
		boolean feeds = FALSE;

		if (tag_get_group_tag(weapon_tag_index) != WEAPON_DEFINITION_TAG)
			continue;
		definition = weapon_definition_get(weapon_tag_index);
		for (magazine_index = 0; !feeds && magazine_index < definition->weapon.magazines.count; magazine_index++)
		{
			struct weapon_magazine_definition *magazine = TAG_BLOCK_GET_ELEMENT(&definition->weapon.magazines,
				magazine_index, struct weapon_magazine_definition);
			long object_index;

			for (object_index = 0; !feeds && object_index < magazine->ammunition_objects.count; object_index++)
			{
				struct weapon_ammunition_object *ammunition = TAG_BLOCK_GET_ELEMENT(&magazine->ammunition_objects,
					object_index, struct weapon_ammunition_object);

				feeds = ammunition->object.index == tag_index;
			}
		}
		if (!feeds)
			continue;
		if (!forge_weapon_cannot_be_aimed(weapon_tag_index))
			return FALSE;
		found = TRUE;
	}
	return found;
}

/* the browser's category of a tag of the loaded map, NONE if it offers
none of it: a vehicle's gun, the flag and the ball (the game's), anything
of a cinematic's, a character that names no unit (a vehicle's crew), a
weapon nobody can aim and its ammunition */
static short forge_tag_category(
	long tag_index)
{
	unsigned long group_tag = tag_get_group_tag(tag_index);
	char const *name = tag_get_name(tag_index);

	if (!name || strstr(name, "cinematic"))
		return NONE;
	/* (a tag brought in from a campaign map: a character, or one of a
	pack's own vehicles or objects, in the campaign's categories; what they
	need - their weapons, effects - none: forge_ai_tags.c) */
	if (forge_ai_tags_source_map(tag_index))
	{
		if (!forge_ai_tags_offered(tag_index))
			return NONE;
		if (group_tag == VEHICLE_DEFINITION_TAG)
			return _forge_category_campaign_vehicles;
		if (group_tag == FORGE_SCENERY_DEFINITION_TAG || group_tag == MACHINE_DEFINITION_TAG ||
			group_tag == CONTROL_DEFINITION_TAG || group_tag == LIGHT_FIXTURE_DEFINITION_TAG)
		{
			return _forge_category_campaign_objects;
		}
	}
	switch (group_tag)
	{
	case VEHICLE_DEFINITION_TAG:
		return _forge_category_vehicles;
	case WEAPON_DEFINITION_TAG:
		if (!strncmp(name, "vehicles\\", 9) || !strncmp(name, "weapons\\flag\\", 13) ||
			!strncmp(name, "weapons\\ball\\", 13))
		{
			return NONE;
		}
		/* (nor a character's own: a sentinel's beam) */
		if (!strncmp(name, "characters\\", 11))
			return NONE;
		/* (nor the unfinished gravity rifle, in every multiplayer
		map's tags though no game puts it down: it fires as the
		flamethrower, hurts nobody, has no first-person model, and its
		pickup prompt is the tag's "<need a string entry here>") */
		if (!strncmp(name, "weapons\\gravity rifle\\", 22))
			return NONE;
		/* (nor a weapon whose shots can't be aimed: the unfinished
		flamethrower, also in every Xbox multiplayer map's tags though no
		game puts it down (#1589). Its trigger can't be aimed, so its flames
		go where the gun points in the Spartan's hands, which have no
		animations for it: always the ground in front of their feet, a
		little to the left, wherever the player looks. Its HUD has no
		reticle and no ammunition. Halo PC's finished flamethrower is
		aimed, and would be offered) */
		if (forge_weapon_cannot_be_aimed(tag_index))
			return NONE;
		return _forge_category_weapons;
	case EQUIPMENT_DEFINITION_TAG:
		/* (nor the ammunition of a weapon left out for that) */
		if (forge_equipment_only_unaimed_ammunition(tag_index))
			return NONE;
		return strstr(name, "grenade") ? _forge_category_weapons : _forge_category_equipment;
	case FORGE_SCENERY_DEFINITION_TAG:
	case MACHINE_DEFINITION_TAG:
	case CONTROL_DEFINITION_TAG:
		return _forge_category_scenery;
	case ACTOR_VARIANT_DEFINITION_TAG:
		/* (an AI character: one that names its unit, not a vehicle's crew
		- a dropship's gunner, a turret's) */
		if (!strncmp(name, "vehicles\\", 9))
			return NONE;
		return actor_variant_definition_get(tag_index)->unit_reference.index != NONE ? _forge_category_ai : NONE;
	}
	return NONE;
}

static int forge_palette_compare(
	void const *a,
	void const *b)
{
	struct forge_palette_entry const *left = a;
	struct forge_palette_entry const *right = b;

	if (left->category != right->category)
		return left->category - right->category;
	return strcmp(left->name, right->name);
}

/* the palette: every tag the Forge menu offers of the loaded map, category by
category, each by its readable name (the same name twice numbered) */
static void forge_palette_build(
	void)
{
	static unsigned long const groups[] =
	{
		VEHICLE_DEFINITION_TAG, WEAPON_DEFINITION_TAG, EQUIPMENT_DEFINITION_TAG, FORGE_SCENERY_DEFINITION_TAG,
		MACHINE_DEFINITION_TAG, CONTROL_DEFINITION_TAG, ACTOR_VARIANT_DEFINITION_TAG, LIGHT_FIXTURE_DEFINITION_TAG,
	};
	short group_index;
	short index;
	short category;

	forge_globals.palette_count = 0;
	for (group_index = 0; group_index < (short)NUMBEROF(groups); group_index++)
	{
		struct tag_iterator iterator;
		long tag_index;

		tag_iterator_new(&iterator, groups[group_index]);
		while ((tag_index = tag_iterator_next(&iterator)) != NONE &&
			forge_globals.palette_count < MAXIMUM_FORGE_PALETTE_TAGS)
		{
			struct forge_palette_entry *entry;

			/* (an iterator of a group walks its children's tags too:
			only the group's own) */
			if (tag_get_group_tag(tag_index) != groups[group_index] || (category = forge_tag_category(tag_index)) == NONE)
				continue;
			entry = &forge_globals.palette[forge_globals.palette_count++];
			entry->tag_index = tag_index;
			entry->category = category;
			forge_tag_readable_name(tag_index, entry->name, sizeof(entry->name));
		}
	}
	qsort(forge_globals.palette, (size_t)forge_globals.palette_count, sizeof(forge_globals.palette[0]),
		forge_palette_compare);
	for (category = 0; category < NUMBER_OF_FORGE_CATEGORIES; category++)
	{
		forge_globals.palette_first[category] = 0;
		forge_globals.palette_counts[category] = 0;
	}
	for (index = forge_globals.palette_count - 1; index >= 0; index--)
	{
		category = forge_globals.palette[index].category;
		forge_globals.palette_first[category] = index;
		forge_globals.palette_counts[category]++;
	}
	/* (one name several times in a category: the second "<name> 2", the
	third "<name> 3"...) */
	{
		short first = 0;
		short repeat = 1;

		for (index = 1; index < forge_globals.palette_count; index++)
		{
			struct forge_palette_entry *entry = &forge_globals.palette[index];
			size_t length = strlen(forge_globals.palette[first].name);

			if (entry->category == forge_globals.palette[first].category &&
				!strncmp(entry->name, forge_globals.palette[first].name, length) && !entry->name[length])
			{
				repeat++;
				if (length + 6 < sizeof(entry->name))
					snprintf(entry->name + length, sizeof(entry->name) - length, " %d", repeat);
			}
			else
			{
				first = index;
				repeat = 1;
			}
		}
	}
	forge_globals.palette_built = TRUE;

	return;
}

/* the palette, built for the loaded map */
static void forge_palette_ready(
	void)
{
	if (!forge_globals.palette_built)
		forge_palette_build();
}

/* the palette entry of a tag, NONE if none */
static short forge_palette_of_tag(
	long tag_index)
{
	short index;

	for (index = 0; index < forge_globals.palette_count; index++)
	{
		if (forge_globals.palette[index].tag_index == tag_index)
			return index;
	}
	return NONE;
}

/* the palette entry named name (its readable name, any case), else the
first with name in its readable name or its tag's, or NONE */
static short forge_palette_find(
	char const *name)
{
	short index;

	for (index = 0; index < forge_globals.palette_count; index++)
	{
		if (!_stricmp(forge_globals.palette[index].name, name) ||
			!strcmp(forge_tag_leaf(forge_globals.palette[index].tag_index), name))
		{
			return index;
		}
	}
	for (index = 0; index < forge_globals.palette_count; index++)
	{
		char const *tag_name = tag_get_name(forge_globals.palette[index].tag_index);

		if ((tag_name && strstr(tag_name, name)) || strstr(forge_globals.palette[index].name, name))
			return index;
	}
	return NONE;
}

/* an object's readable name: its palette entry's, else its tag's */
static void forge_object_name(
	long object_index,
	char *name,
	size_t size)
{
	struct object_datum *object = object_try_and_get(object_index);
	short index;

	if (!object)
	{
		snprintf(name, size, "?");
		return;
	}
	for (index = 0; index < forge_globals.placed_count; index++)
	{
		if (forge_globals.placed[index].object_index == object_index)
		{
			short entry = forge_palette_of_tag(forge_globals.placed[index].tag_index);

			if (entry != NONE)
			{
				snprintf(name, size, "%s", forge_globals.palette[entry].name);
				return;
			}
		}
	}
	index = forge_palette_of_tag(object->definition_index);
	if (index != NONE)
		snprintf(name, size, "%s", forge_globals.palette[index].name);
	else
		forge_tag_readable_name(object->definition_index, name, size);
}

/* a line on the monitor's HUD for a while, and in the log */
static void forge_notice(
	char const *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(forge_globals.notice, sizeof(forge_globals.notice), format, arguments);
	va_end(arguments);
	forge_globals.notice_until = system_milliseconds() + FORGE_NOTICE_MILLISECONDS;
	platform_log("forge: %s", forge_globals.notice);
}

/* a notice that is also the Forge menu's status line */
static void forge_menu_notice(
	char const *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(forge_globals.menu_status, sizeof(forge_globals.menu_status), format, arguments);
	va_end(arguments);
	forge_globals.menu_status_time = system_milliseconds();
	forge_notice("%s", forge_globals.menu_status);
}

/* a warning: on the HUD and the Forge menu's status line, longer than a
notice (a whole sentence to read) */
static void forge_warning(
	char const *text)
{
	forge_menu_notice("%s", text);
	forge_globals.notice_until = system_milliseconds() + FORGE_WARNING_MILLISECONDS;
}

/* a warning on the HUD alone (the menu's status line left as it is) */
static void forge_warning_hud(
	char const *text)
{
	forge_notice("%s", text);
	forge_globals.notice_until = system_milliseconds() + FORGE_WARNING_MILLISECONDS;
}

/* ---- the budget */

/* whether an object is there and not being deleted (object_delete only
marks it: it goes at the next update of the objects, which a paused game -
the Forge menu over it - does not run) */
static boolean forge_object_alive(
	long object_index)
{
	return object_try_and_get(object_index) &&
		!TEST_FLAG(object_header_get(object_index)->flags, _object_header_being_deleted_bit);
}

static void forge_transform_get(long object_index, struct forge_transform *transform);

/* the layout's objects gone in play (equipment picked up, a grenade thrown)
kept, their object NONE (they come back with the next round). Only the
layout's: what the game spawns is never counted */
static void forge_placed_update(
	void)
{
	short index;

	forge_globals.placed_count = (short)PIN(forge_globals.placed_count, 0, FORGE_MAXIMUM_OBJECTS);
	for (index = 0; index < forge_globals.placed_count; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];

		if (placed->object_index != NONE && !forge_object_alive(placed->object_index))
			placed->object_index = NONE;
		if (placed->ai)
			continue;
		/* (a placed item - a weapon, ammunition, a health pack: a layout's
		pickup - stays while no one holds it: the game engine's purge deletes
		every item unowned for 30 s (game_engine_update_purge). Dropped by a
		player, it is the player's as any) */
		if (placed->object_index != NONE)
		{
			short type = object_get_type(placed->object_index);

			if (type == _object_type_weapon || type == _object_type_equipment)
			{
				struct item_datum *item = item_get(placed->object_index);

				if (item && !TEST_FLAG(item->item.flags, _item_attached_to_unit_bit) && item->object.owner_player_index == NONE)
					item->item.last_owned_time = game_time_get();
			}
		}
	}
	/* (the AI characters, the dead and the waiting among them: forge_ai.inc) */
	forge_ai_count();
}

static short forge_objects_counted(
	void)
{
	/* (the markers too: forge_spawns.inc; not a dead AI character, which
	counts nowhere until it is back: forge_ai.inc) */
	return (short)(forge_globals.placed_count - forge_globals.ai_dead_count + forge_globals.spawn_count);
}

/* an entry of the layout taken out (the editor's delete, an undone
placing): the rest kept in their order */
static void forge_placed_remove(
	short index)
{
	if (index < 0 || index >= forge_globals.placed_count || index >= FORGE_MAXIMUM_OBJECTS)
		return;
	csmemmove(&forge_globals.placed[index], &forge_globals.placed[index + 1],
		sizeof(forge_globals.placed[0]) * (size_t)(forge_globals.placed_count - index - 1));
	forge_globals.placed_count--;
	forge_placed_update();
}

/* the entry of an object (NULL for NONE, or one not placed) */
static struct forge_placed *forge_placed_of_object(
	long object_index)
{
	short index;

	if (object_index == NONE)
		return NULL;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		if (forge_globals.placed[index].object_index == object_index)
			return &forge_globals.placed[index];
	}
	return NULL;
}

/* the editor took an object out of the layout: its entry goes (not kept
as gone in play) */
static void forge_placed_forget_object(
	long object_index)
{
	struct forge_placed *placed = forge_placed_of_object(object_index);

	if (placed)
		forge_placed_remove((short)(placed - forge_globals.placed));
}

static void forge_placed_forget_id(
	long id)
{
	short index;

	for (index = 0; id != NONE && index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		if (forge_globals.placed[index].id == id)
		{
			forge_placed_remove(index);
			return;
		}
	}
}

/* an object placed, let go or moved by the editor: its placed spot follows
it while it comes to rest */
static void forge_settle(
	long object_index)
{
	struct forge_placed *placed = forge_placed_of_object(object_index);

	if (!placed)
		return;
	if (forge_object_alive(object_index))
	{
		forge_transform_get(object_index, &placed->home);
		placed->has_home = TRUE;
	}
	placed->settle_until = system_milliseconds() + FORGE_SETTLE_MILLISECONDS;
}

/* (each frame) the placed spots of the objects held or settling followed:
not while something else carries them (a weapon in a hand, a rider in a
seat), not once dead or destroyed */
static void forge_homes_update(
	void)
{
	unsigned long now = system_milliseconds();
	short index;

	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];
		struct object_datum *object;

		if (!forge_object_alive(placed->object_index))
			continue;
		object = object_get(placed->object_index);
		if (placed->object_index != forge_globals.held_object_index &&
			(((long)(placed->settle_until - now) <= 0 &&
				!((FLAG(object->object.type) & _object_mask_item) && !TEST_FLAG(object->object.flags, _object_at_rest_bit) &&
					(long)(placed->settle_until + FORGE_ITEM_SETTLE_MILLISECONDS - FORGE_SETTLE_MILLISECONDS - now) > 0)) ||
				object->object.parent_object_index != NONE || TEST_FLAG(object->object.damage_flags, _object_dead_bit)))
		{
			continue;
		}
		/* (a fixed one let go: where it was let go, never where it falls -
		forge_physics_update holds it there) */
		if ((placed->flags & FORGE_RECORD_PHYSICS_FIXED) && placed->has_home &&
			placed->object_index != forge_globals.held_object_index)
		{
			continue;
		}
		/* (an AI character with its AI on, let go: only its fall followed,
		not where it walks off to) */
		if (placed->ai && placed->has_home && placed->object_index != forge_globals.held_object_index &&
			!(placed->flags & FORGE_RECORD_AI_OFF))
		{
			placed->home.position.z = object->object.position.z;
			continue;
		}
		forge_transform_get(placed->object_index, &placed->home);
		placed->has_home = TRUE;
	}
}

static void forge_placed_add(
	long object_index,
	long tag_index)
{
	struct forge_placed *placed;

	if (forge_globals.placed_count < 0 || forge_globals.placed_count >= FORGE_MAXIMUM_OBJECTS)
		return;
	placed = &forge_globals.placed[forge_globals.placed_count];
	csmemset(placed, 0, sizeof(*placed));
	placed->object_index = object_index;
	placed->tag_index = tag_index;
	placed->ai = tag_index != NONE && tag_get_group_tag(tag_index) == ACTOR_VARIANT_DEFINITION_TAG;
	placed->team = NONE;
	placed->flags = FORGE_RECORD_PLACE_AT_START;
	placed->respawn_seconds = FORGE_DEFAULT_RESPAWN_SECONDS;
	placed->ai_team = FORGE_AI_TEAM_DEFAULT;
	placed->dead_tick = 0;
	placed->id = ++forge_globals.next_id;
	forge_globals.placed_count++;
	if (placed->ai)
		forge_globals.ai_count++;
	forge_settle(object_index);
}

/* (DELETE ALL) the history's steps that name the objects taken
out of the layout (their ids): gone, the cursor kept on the same step -
nothing brings those back */
static void forge_undo_forget(
	long const *ids,
	short count)
{
	short index;
	short kept = 0;
	short cursor = forge_globals.undo_cursor;

	if (count <= 0)
		return;
	for (index = 0; index < forge_globals.undo_count; index++)
	{
		short id;

		for (id = 0; id < count && forge_globals.undo[index].id != ids[id]; id++)
			;
		if (forge_globals.undo[index].id != NONE && id < count)
		{
			if (index < forge_globals.undo_cursor)
				cursor--;
			continue;
		}
		forge_globals.undo[kept++] = forge_globals.undo[index];
	}
	forge_globals.undo_count = kept;
	forge_globals.undo_cursor = (short)PIN(cursor, 0, kept);
}

/* (DELETE ALL) whether an object of the layout is in use: carried or seated (a weapon in a
hand, a rider), or carrying a unit (a vehicle with someone in it). One in use
is left where it is, and in the layout: it is left for the editor's single
delete once let go */
static boolean forge_object_in_use(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	long child_index;

	if (!object)
		return FALSE;
	if (object->object.parent_object_index != NONE)
		return TRUE;
	for (child_index = object->object.first_child_object_index; child_index != NONE;
		child_index = object_get(child_index)->object.next_object_index)
	{
		if (FLAG(object_get(child_index)->object.type) & _object_mask_unit)
			return TRUE;
	}
	return FALSE;
}

/* how many of the layout's are in one of the browser's categories: its
objects placed (a category of markers: its spawn points, teleporters or
game type objects; AI: the characters, alive or dead) */
short forge_group_placed_count(
	short category)
{
	short index;
	short count = 0;
	short first = forge_marker_category_first(category);

	if (category < 0 || category >= NUMBER_OF_FORGE_CATEGORIES)
		return 0;
	if (first != NONE)
	{
		for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
		{
			short kind = forge_globals.spawns[index].kind;

			count += kind >= first && kind < first + forge_marker_category_count(category) ? 1 : 0;
		}
		return count;
	}
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed const *placed = &forge_globals.placed[index];

		if (forge_tag_category(placed->tag_index) == category)
			count++;
	}
	return count;
}

/* (DELETE ALL) every object of the layout in one of the Forge menu's categories
taken out of the layout and the world: they do not come back with the next
round, a layout saved after has them not, the history's steps that name
them go. A category of markers: its spawn points, teleporters or game type
objects. What is in use (forge_object_in_use) is left. The ones deleted,
counted */
static short forge_group_delete(
	short category)
{
	short index;
	short deleted = 0;
	short kept = 0;
	short first = forge_marker_category_first(category);
	long deleted_ids[FORGE_MAXIMUM_OBJECTS];

	if (category < 0 || category >= NUMBER_OF_FORGE_CATEGORIES)
		return 0;
	/* (the AI characters: CLEAR ALL AI, the dead with them) */
	if (category == _forge_category_ai)
		return forge_ai_clear(TRUE);
	if (first != NONE)
	{
		for (index = (short)(forge_globals.spawn_count - 1); index >= 0; index--)
		{
			struct forge_spawn *spawn = &forge_globals.spawns[index];

			if (spawn->kind < first || spawn->kind >= first + forge_marker_category_count(category))
				continue;
			forge_spawn_remove(spawn);
			deleted++;
		}
	}
	else
	{
		forge_placed_update();
		for (index = (short)(forge_globals.placed_count - 1); index >= 0; index--)
		{
			struct forge_placed *placed = &forge_globals.placed[index];

			if (forge_tag_category(placed->tag_index) != category)
				continue;
			/* (in use - a weapon in a hand, a vehicle with someone in it -:
			left) */
			if (forge_object_alive(placed->object_index) && forge_object_in_use(placed->object_index))
			{
				kept++;
				continue;
			}
			if (placed->object_index != NONE && placed->object_index == forge_globals.held_object_index)
			{
				forge_globals.held_object_index = NONE;
				forge_globals.held_spawned = FALSE;
			}
			if (forge_object_alive(placed->object_index))
				object_delete(placed->object_index);
			deleted_ids[deleted++] = placed->id;
			forge_placed_remove(index);
		}
		forge_undo_forget(deleted_ids, deleted);
		forge_placed_update();
	}
	forge_globals.deleted_count += deleted;
	platform_log("forge: deleted all %s: %d (%d in use kept); budget %d/%d objects", forge_category_log_names[category],
		deleted, kept, forge_objects_counted(), forge_objects_budget());
	if (!deleted && !kept)
		forge_menu_notice(("No %s to delete"), forge_category_name(category));
	else if (kept)
		forge_menu_notice(("Deleted all %s: %d (%d in use kept)"), forge_category_name(category), deleted, kept);
	else
		forge_menu_notice(("Deleted all %s: %d"), forge_category_name(category), deleted);
	return deleted;
}

static short forge_placed_of_tag(
	long tag_index)
{
	short index;
	short count = 0;

	for (index = 0; index < forge_globals.placed_count; index++)
	{
		if (forge_globals.placed[index].tag_index == tag_index && forge_object_alive(forge_globals.placed[index].object_index))
			count++;
	}
	return count;
}

/* ---- the controller */

static void forge_pad_get(
	struct forge_pad *pad,
	boolean mouse)
{
	long player_index = local_player_get_player_index(forge_globals.local_player_index);
	short button;
	short stick;

	csmemset(pad, 0, sizeof(*pad));
	if (player_index != NONE)
	{
		short gamepad_index = player_get(player_index)->local_player_index;

		if (gamepad_index != NONE && input_has_gamepad(gamepad_index))
		{
			struct gamepad_state const *gamepad = input_get_gamepad_state(gamepad_index);
			float yaw, pitch;

			for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
				pad->down[button] = gamepad->buttons[button] != 0;
			for (stick = 0; stick < NUMBER_OF_GAMEPAD_STICKS; stick++)
			{
				pad->sticks[stick][0] = (real)gamepad->sticks[stick].x / 32767.f;
				pad->sticks[stick][1] = (real)gamepad->sticks[stick].y / 32767.f;
			}
			/* (the mouse, on the keyboard's port: its motion since the last
			read, taken by this read, so only the camera's input asks; read
			and let go of while the console is open, as player_control.c's) */
			if (mouse && halo_linux_mouse_look(gamepad_index, &yaw, &pitch) && !console_is_active())
			{
				pad->look_yaw = yaw;
				pad->look_pitch = pitch;
			}
		}
	}
	/* (Back and the right stick's click together: the monitor and the
	Spartan flipped) */
	pad->toggle = pad->down[_gamepad_binary_button_back] && pad->down[_gamepad_binary_button_right_thumb];
	/* (the keyboard and mouse, the first controller's: Forge's own keys,
	controls.forge_*, as the buttons they stand for, and the movement keys
	as the left stick) */
	if (player_index != NONE && player_get(player_index)->local_player_index == 0)
	{
		unsigned long keys = halo_forge_keys();
		unsigned long actions = halo_keyboard_actions(0);
		boolean holding = object_try_and_get(forge_globals.held_object_index) != NULL;

		pad->down[_forge_button_fly_up] |= TEST_FLAG(keys, HALO_FORGE_KEY_UP) != 0;
		pad->down[_forge_button_fly_down] |= TEST_FLAG(keys, HALO_FORGE_KEY_DOWN) != 0;
		pad->down[_forge_button_grab] |= TEST_FLAG(keys, HALO_FORGE_KEY_GRAB) != 0;
		/* (rotate mode is the left trigger's with an object held) */
		pad->down[_forge_button_alternate] |= holding && TEST_FLAG(keys, HALO_FORGE_KEY_ROTATE);
		pad->down[_forge_button_duplicate] |= TEST_FLAG(keys, HALO_FORGE_KEY_COPY) != 0;
		pad->delete_key = TEST_FLAG(keys, HALO_FORGE_KEY_DELETE) != 0;
		pad->down[_forge_button_straighten] |= TEST_FLAG(keys, HALO_FORGE_KEY_UPRIGHT) != 0;
		pad->down[_forge_button_speed] |= TEST_FLAG(keys, HALO_FORGE_KEY_SPEED) != 0;
		pad->down[_forge_button_menu] |= TEST_FLAG(keys, HALO_FORGE_KEY_MENU) != 0;
		/* (nudge mode: rotate mode's right stick click) */
		pad->down[_gamepad_binary_button_right_thumb] |= TEST_FLAG(keys, HALO_FORGE_KEY_NUDGE) != 0;
		pad->toggle |= TEST_FLAG(keys, HALO_FORGE_KEY_MONITOR) != 0;
		pad->fast = TEST_FLAG(keys, HALO_FORGE_KEY_FAST) != 0;
		pad->nearer = TEST_FLAG(keys, HALO_FORGE_KEY_NEARER) != 0;
		pad->farther = TEST_FLAG(keys, HALO_FORGE_KEY_FARTHER) != 0;
		if (pad->sticks[_gamepad_stick_left][0] == 0.f && pad->sticks[_gamepad_stick_left][1] == 0.f)
		{
			real across = (TEST_FLAG(actions, HALO_KEYBOARD_STRAFE_RIGHT) ? 1.f : 0.f) -
				(TEST_FLAG(actions, HALO_KEYBOARD_STRAFE_LEFT) ? 1.f : 0.f);
			real ahead = (TEST_FLAG(actions, HALO_KEYBOARD_MOVE_FORWARD) ? 1.f : 0.f) -
				(TEST_FLAG(actions, HALO_KEYBOARD_MOVE_BACKWARD) ? 1.f : 0.f);
			real length = across != 0.f && ahead != 0.f ? 0.70710678f : 1.f;

			pad->sticks[_gamepad_stick_left][0] = across * length;
			pad->sticks[_gamepad_stick_left][1] = ahead * length;
		}
	}
	/* (forge press, forge stick) */
	if (forge_globals.test_buttons_frames > 0)
	{
		for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
			pad->down[button] |= forge_globals.test_down[button];
		pad->toggle |= forge_globals.test_toggle;
	}
	if (forge_globals.test_sticks_frames > 0)
	{
		for (stick = 0; stick < NUMBER_OF_GAMEPAD_STICKS; stick++)
		{
			pad->sticks[stick][0] = forge_globals.test_sticks[stick][0];
			pad->sticks[stick][1] = forge_globals.test_sticks[stick][1];
		}
	}
	for (stick = 0; stick < NUMBER_OF_GAMEPAD_STICKS; stick++)
	{
		short axis;

		for (axis = 0; axis < 2; axis++)
		{
			real value = pad->sticks[stick][axis];

			if (value > -FORGE_STICK_DEAD_ZONE && value < FORGE_STICK_DEAD_ZONE)
				value = 0.f;
			pad->sticks[stick][axis] = PIN(value, -1.f, 1.f);
		}
	}

	return;
}

/* ---- delete: pressed, or held (game.forge_hold_to_delete) */

enum
{
	/* (today's) a press deletes */
	_forge_delete_press = 0,
	/* the controller's Y held FORGE_DELETE_HOLD_MILLISECONDS deletes; the
	keyboard's key still at a press */
	_forge_delete_hold_controller,
	/* both held */
	_forge_delete_hold_all,
	NUMBER_OF_FORGE_DELETE_MODES
};

/* game.forge_hold_to_delete: "off" (the default), "controller" or "all";
anything else is "off" */
static short forge_delete_mode(
	void)
{
	char const *mode = config_string("game.forge_hold_to_delete");

	if (mode && !strcmp(mode, "controller"))
		return _forge_delete_hold_controller;
	if (mode && !strcmp(mode, "all"))
		return _forge_delete_hold_all;
	return _forge_delete_press;
}

/* whether a delete is asked this frame: a press of what deletes at a press,
or a hold of what deletes when held, once it has been held long enough
(once a hold: let go to delete again). The hold is counted in system time,
outside the game's tick: Forge's monitor is the editor's, never a game's
simulation */
static boolean forge_delete_asked(
	struct forge_pad const *pad)
{
	short mode = forge_delete_mode();
	boolean button = pad->down[_forge_button_delete];
	boolean held = (mode != _forge_delete_press && button) || (mode == _forge_delete_hold_all && pad->delete_key);
	boolean press = (mode == _forge_delete_press && (button || pad->delete_key)) ||
		(mode == _forge_delete_hold_controller && pad->delete_key);
	boolean pressed = press && !forge_globals.delete_press_was_down;
	unsigned long now = system_milliseconds();
	unsigned long elapsed;

	forge_globals.delete_press_was_down = press;
	forge_globals.delete_hold_fraction = 0.f;
	if (!held)
	{
		forge_globals.delete_hold_since = 0;
		forge_globals.delete_hold_done = FALSE;
		return pressed;
	}
	if (!forge_globals.delete_hold_since)
	{
		forge_globals.delete_hold_since = now ? now : 1;
		forge_globals.delete_hold_done = FALSE;
	}
	if (forge_globals.delete_hold_done)
		return pressed;
	elapsed = now - forge_globals.delete_hold_since;
	if (elapsed >= FORGE_DELETE_HOLD_MILLISECONDS)
	{
		forge_globals.delete_hold_done = TRUE;
		return TRUE;
	}
	forge_globals.delete_hold_fraction = (real)elapsed / (real)FORGE_DELETE_HOLD_MILLISECONDS;
	return pressed;
}

/* (a menu over the monitor) what is held there deletes nothing once it
is gone: let go first */
static void forge_delete_hold_reset(
	void)
{
	forge_globals.delete_hold_since = 1;
	forge_globals.delete_hold_done = TRUE;
	forge_globals.delete_hold_fraction = 0.f;
	forge_globals.delete_press_was_down = TRUE;
}

/* the delete prompt's word: "HOLD DELETE" when the device used last deletes
when held */
static char const *forge_delete_word(
	void)
{
	short mode = forge_delete_mode();
	boolean keyboard = FALSE;

	/* (ui_overlay.c's: the builds with the game list, HALO_GAME_BROWSER;
	without it, a controller's word) */
#ifdef HALO_GAME_BROWSER
	keyboard = ui_overlay_keyboard() != 0;
#endif
	if (mode == _forge_delete_hold_all || (mode == _forge_delete_hold_controller && !keyboard))
		return "HOLD DELETE";
	return "DELETE";
}

/* ---- the monitor's world */

/* the camera's place and direction (the last frame's) */
static boolean forge_camera_get(
	real_point3d *position,
	real_vector3d *forward)
{
	struct observer_result const *camera = observer_get_camera(forge_globals.local_player_index);

	if (!camera)
		return FALSE;
	*position = camera->position;
	*forward = camera->forward;
	return TRUE;
}

/* whether object_index is the player's unit or carried by it */
static boolean forge_object_is_players(
	long object_index)
{
	long player_index = local_player_get_player_index(forge_globals.local_player_index);
	long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;

	return (forge_globals.unit_index != NONE && object_get_ultimate_parent(object_index) == forge_globals.unit_index) ||
		(unit_index != NONE && object_get_ultimate_parent(object_index) == unit_index);
}

/* how far the crosshair takes an object or a marker (game.forge_grab_range;
anything but "near" or "medium" is "far", FORGE_PICK_RANGE). The
structure still stops it nearer */
static real forge_grab_range(
	void)
{
	char const *range = config_string("game.forge_grab_range");

	if (range && !strcmp(range, "near"))
		return FORGE_GRAB_RANGE_NEAR;
	if (range && !strcmp(range, "medium"))
		return FORGE_GRAB_RANGE_MEDIUM;
	return FORGE_PICK_RANGE;
}

/* how far along the camera's line the structure is (FORGE_PICK_RANGE if
nothing is that near) */
static real forge_structure_distance(
	real_point3d const *position,
	real_vector3d const *forward)
{
	struct collision_result collision;
	real_vector3d vector;

	vector.i = forward->i * FORGE_PICK_RANGE;
	vector.j = forward->j * FORGE_PICK_RANGE;
	vector.k = forward->k * FORGE_PICK_RANGE;
	if (collision_test_vector(
			FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
			position,
			&vector,
			NONE,
			&collision))
	{
		return collision.t * FORGE_PICK_RANGE;
	}
	return FORGE_PICK_RANGE;
}

/* the object under the crosshair: the first the camera's line meets - by
its collision model (a vehicle, a biped, scenery, a machine), or, for a
weapon or equipment on the ground, which has none a line would meet, by its
bounding sphere - before the structure; not the player's, nothing attached
to another, not the one held; NONE if none */
static long forge_object_under_crosshair_at(
	real *distance)
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d vector;
	struct collision_result collision;
	real range = forge_grab_range();
	real nearest = range;
	long nearest_index = NONE;
	long player_index = local_player_get_player_index(forge_globals.local_player_index);
	long ignored = forge_globals.unit_index != NONE ? forge_globals.unit_index :
		player_index != NONE ? player_get(player_index)->unit_index : NONE;
	struct object_iterator iterator;
	struct object_datum *object;

	if (!forge_camera_get(&position, &forward))
		return NONE;
	vector.i = forward.i * range;
	vector.j = forward.j * range;
	vector.k = forward.k * range;
	if (collision_test_vector(
			FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
			FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_bipeds_bit) |
			FLAG(_collision_test_objects_vehicles_bit) | FLAG(_collision_test_objects_scenery_bit) |
			FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_controls_bit)
#ifndef HALO_CONSOLE_NO_FORGE
			/* (PHASED objects too: not on the consoles, which leave Forge's
			hooks in collisions.c out) */
			| FLAG(_collision_test_forge_phased_objects_bit)
#endif
			,
			&position,
			&vector,
			ignored,
			&collision))
	{
		nearest = collision.t * range;
		if (collision.type == _collision_result_object && object_try_and_get(collision.object_index))
		{
			long hit = object_get_ultimate_parent(collision.object_index);

			if (!forge_object_is_players(hit) && hit != forge_globals.held_object_index)
				nearest_index = hit;
		}
	}
	/* (the items, nearer than that) */
	object_iterator_new(&iterator, _object_mask_weapon | _object_mask_equipment, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		real_vector3d to_center;
		real along;
		real across_squared;
		real radius = MAX(object->object.bounding_sphere_radius, 0.2f);
		real entry;

		if (object->object.parent_object_index != NONE || forge_object_is_players(iterator.index) ||
			iterator.index == forge_globals.held_object_index)
		{
			continue;
		}
		to_center.i = object->object.bounding_sphere_center.x - position.x;
		to_center.j = object->object.bounding_sphere_center.y - position.y;
		to_center.k = object->object.bounding_sphere_center.z - position.z;
		along = dot_product3d(&to_center, &forward);
		across_squared = dot_product3d(&to_center, &to_center) - along * along;
		if (along < 0.f || across_squared > radius * radius)
			continue;
		entry = along - square_root(radius * radius - across_squared);
		if (entry < nearest)
		{
			nearest = entry;
			nearest_index = iterator.index;
		}
	}
	if (distance)
		*distance = nearest;
	return nearest_index;
}

static long forge_object_under_crosshair(
	void)
{
	return forge_object_under_crosshair_at(NULL);
}

static void forge_undo_record(short kind, long object_index, struct forge_transform const *from);
static void forge_snapped(real_point3d *position, real_vector3d *forward, real_vector3d *up);

/* game.forge_held_facing: "view" turns what is held with the camera's yaw
until it is turned by hand; anything else ("keep", the default) keeps the
facing it was grabbed with */
static boolean forge_held_facing_follows(
	void)
{
	char const *facing = config_string("game.forge_held_facing");

	return facing && !strcmp(facing, "view");
}

/* (each frame, before what is held is placed) with game.forge_held_facing
"view", what is held turns about up as the camera turns, so that it keeps
facing the same way from the camera; its pitch and roll stay. Once its
facing is changed any other way (rotate mode, the properties' angles,
upright, a turn) it keeps its own until it is let go */
static void forge_held_follow_view(
	void)
{
	boolean object = object_try_and_get(forge_globals.held_object_index) != NULL;
	real_point3d position;
	real_vector3d forward;
	real yaw;
	real turn;
	real_vector3d axis = *global_up3d;

	if (!object && !forge_globals.held_marker_id)
	{
		forge_globals.follow_object_index = NONE;
		forge_globals.follow_marker_id = 0;
		forge_globals.following = FALSE;
		return;
	}
	if (!forge_camera_get(&position, &forward))
		return;
	/* (straight up or down: no yaw to follow this frame) */
	if (forward.i * forward.i + forward.j * forward.j < 0.0001f)
		return;
	yaw = arctangent(forward.j, forward.i);
	/* (a new hold: followed from here, when the setting says so) */
	if (forge_globals.follow_object_index != (object ? forge_globals.held_object_index : NONE) ||
		forge_globals.follow_marker_id != forge_globals.held_marker_id)
	{
		forge_globals.follow_object_index = object ? forge_globals.held_object_index : NONE;
		forge_globals.follow_marker_id = forge_globals.held_marker_id;
		forge_globals.following = forge_held_facing_follows();
		forge_globals.follow_yaw = yaw;
		forge_globals.follow_forward = forge_globals.held_forward;
		return;
	}
	if (!forge_globals.following)
		return;
	/* (turned by hand since the last frame: its own facing from now) */
	if (forge_globals.held_forward.i != forge_globals.follow_forward.i ||
		forge_globals.held_forward.j != forge_globals.follow_forward.j ||
		forge_globals.held_forward.k != forge_globals.follow_forward.k)
	{
		forge_globals.following = FALSE;
		platform_log("forge: the held facing no longer follows the view (turned by hand)");
		return;
	}
	turn = yaw - forge_globals.follow_yaw;
	if (turn != 0.f)
	{
		rotate_vector_about_axis(&forge_globals.held_forward, &axis, sine(turn), cosine(turn));
		rotate_vector_about_axis(&forge_globals.held_up, &axis, sine(turn), cosine(turn));
	}
	forge_globals.follow_yaw = yaw;
	forge_globals.follow_forward = forge_globals.held_forward;
}

/* the held object where the camera holds it */
static void forge_held_object_place(
	void)
{
	struct object_datum *object = object_try_and_get(forge_globals.held_object_index);
	real_point3d position;
	real_vector3d forward;

	if (!object)
	{
		forge_globals.held_object_index = NONE;
		return;
	}
	if (!forge_camera_get(&position, &forward))
		return;
	position.x += forward.i * forge_globals.held_distance;
	position.y += forward.j * forge_globals.held_distance;
	position.z += forward.k * forge_globals.held_distance;
	{
		real_vector3d facing = forge_globals.held_forward;
		real_vector3d up = forge_globals.held_up;

		forge_snapped(&position, &facing, &up);
		/* (forge_precision.inc: the hold's nudge - a new hold's none) */
		if (forge_globals.nudge_object != forge_globals.held_object_index)
		{
			forge_globals.nudge_object = forge_globals.held_object_index;
			forge_globals.held_nudge = *global_zero_vector3d;
		}
		position.x += forge_globals.held_nudge.i;
		position.y += forge_globals.held_nudge.j;
		position.z += forge_globals.held_nudge.k;
		object_set_position(forge_globals.held_object_index, &position, &facing, &up);
	}
	/* (it hangs where it is held: what the physics gave it this tick
	goes) */
	object->object.translational_velocity = *global_zero_vector3d;
	object->object.angular_velocity = *global_zero_vector3d;

	return;
}

static void forge_grab_local(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	real_point3d position;
	real_vector3d forward;
	char name[FORGE_NAME_SIZE];

	if (!object || !forge_camera_get(&position, &forward))
		return;
	forge_globals.held_object_index = object_index;
	forge_globals.held_forward = object->object.forward;
	forge_globals.held_up = object->object.up;
	{
		real_vector3d to_object;

		to_object.i = object->object.position.x - position.x;
		to_object.j = object->object.position.y - position.y;
		to_object.k = object->object.position.z - position.z;
		forge_globals.held_distance = PIN(
			dot_product3d(&to_object, &forward),
			FORGE_MINIMUM_HOLD_DISTANCE,
			FORGE_MAXIMUM_HOLD_DISTANCE);
	}
	forge_transform_get(object_index, &forge_globals.held_from);
	forge_globals.held_spawned = FALSE;
	forge_object_name(object_index, name, sizeof(name));
	platform_log("forge: grabbed %s (%s)", name, forge_tag_leaf(object->definition_index));

	return;
}

/* the object under the crosshair held (one of the map's own the
layout's from now on: forge_map_objects.inc) */
static void forge_grab(
	long object_index)
{
	if (!forge_map_object_take(object_index, TRUE))
		return;
	forge_grab_local(object_index);
}

static void forge_drop(
	void)
{
	struct object_datum *object = object_try_and_get(forge_globals.held_object_index);

	if (object)
	{
		char name[FORGE_NAME_SIZE];

		/* (snapped, its facing is the one shown) */
		if (forge_globals.snap)
		{
			real_point3d position = object->object.position;

			forge_snapped(&position, &forge_globals.held_forward, &forge_globals.held_up);
		}
		/* (a placing, or a move, in the undo history) */
		if (forge_globals.held_spawned)
			forge_undo_record(_forge_undo_place, forge_globals.held_object_index, NULL);
		else
			forge_undo_record(_forge_undo_move, forge_globals.held_object_index, &forge_globals.held_from);
		/* (it falls from where it was let go: its placed spot where it
		comes to rest) */
		SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);
		forge_item_unburied(forge_globals.held_object_index);
		forge_settle(forge_globals.held_object_index);
		forge_object_name(forge_globals.held_object_index, name, sizeof(name));
		platform_log("forge: dropped %s (%s) at %.2f %.2f %.2f facing %.2f %.2f %.2f", name,
			forge_tag_leaf(object->definition_index), object->object.position.x, object->object.position.y,
			object->object.position.z, object->object.forward.i, object->object.forward.j, object->object.forward.k);
	}
	forge_globals.held_object_index = NONE;
	forge_globals.held_spawned = FALSE;

	return;
}

/* the held object, or the one under the crosshair */
static long forge_target_object(
	void)
{
	if (object_try_and_get(forge_globals.held_object_index))
		return forge_globals.held_object_index;
	return forge_object_under_crosshair();
}

/* an object's facing set: the one held keeps it as it is held */
static void forge_set_facing(
	long object_index,
	real_vector3d const *forward,
	real_vector3d const *up)
{
	struct object_datum *object = object_try_and_get(object_index);

	if (!object)
		return;
	if (object_index == forge_globals.held_object_index)
	{
		forge_globals.held_forward = *forward;
		forge_globals.held_up = *up;
	}
	else
	{
		struct forge_transform from;

		/* (one of the map's own: the layout's from now on) */
		if (!forge_map_object_take(object_index, TRUE))
			return;
		forge_transform_get(object_index, &from);
		object_set_position(object_index, &object->object.position, forward, up);
		SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);
		forge_item_unburied(object_index);
		forge_undo_record(_forge_undo_move, object_index, &from);
		forge_settle(object_index);
	}
}

static void forge_turn(
	long object_index,
	real angle)
{
	struct object_datum *object = object_try_and_get(object_index);
	real_vector3d forward;
	real_vector3d up;
	real_vector3d axis = *global_up3d;

	if (!object)
		return;
	forward = object_index == forge_globals.held_object_index ? forge_globals.held_forward : object->object.forward;
	up = object_index == forge_globals.held_object_index ? forge_globals.held_up : object->object.up;
	rotate_vector_about_axis(&forward, &axis, sine(angle), cosine(angle));
	rotate_vector_about_axis(&up, &axis, sine(angle), cosine(angle));
	forge_set_facing(object_index, &forward, &up);

	return;
}

/* ---- rotate mode (L held while holding an object): the object turned
about the camera's view - yaw about the world's up, pitch about the
camera's left along the ground, roll about its forward along the ground -
smoothly, or in steps of the snapping D-pad right picks (Off, 15, 45, 90
degrees): one step a flick of the stick */

static void forge_angles_from_facing(real_vector3d const *forward, real_vector3d const *up, float angles[3]);
static void forge_facing_from_angles(float const angles[3], real_vector3d *forward, real_vector3d *up);
static real forge_round(real value);

static real forge_absolute(
	real value)
{
	return value < 0.f ? -value : value;
}

/* the axis an axis's turn is about: the world's up (a positive turn yaws
the object left, counterclockwise seen from above); the camera's left
along the ground (it pitches the object's top away from the camera, the
way the stick is pushed); the camera's forward along the ground (it rolls
the top to the right). Snapping, the camera's nearest of the world's own
axes, so that the steps keep the object square with the map */
static void forge_rotate_axis(
	short axis,
	real_vector3d *vector)
{
	real_point3d position;
	real_vector3d forward;

	if (axis == _forge_axis_yaw)
	{
		*vector = *global_up3d;
		return;
	}
	if (!forge_camera_get(&position, &forward))
		forward = *global_forward3d;
	forward.k = 0.f;
	if (normalize3d(&forward) < 0.0001f)
		forward = *global_forward3d;
	if (forge_rotate_snaps[forge_globals.rotate_snap] > 0)
	{
		if (forge_absolute(forward.i) >= forge_absolute(forward.j))
		{
			forward.i = forward.i < 0.f ? -1.f : 1.f;
			forward.j = 0.f;
		}
		else
		{
			forward.j = forward.j < 0.f ? -1.f : 1.f;
			forward.i = 0.f;
		}
	}
	if (axis == _forge_axis_pitch)
	{
		vector->i = -forward.j;
		vector->j = forward.i;
		vector->k = 0.f;
	}
	else
	{
		*vector = forward;
	}
}

/* the object held turned by angle (radians) about one of rotate mode's
axes, its facing kept square (forward and up at right angles, each of
length one) */
static void forge_rotate_held(
	short axis_kind,
	real angle)
{
	real_vector3d axis;
	real_vector3d forward = forge_globals.held_forward;
	real_vector3d up = forge_globals.held_up;
	real along;

	if (angle == 0.f)
		return;
	forge_rotate_axis(axis_kind, &axis);
	rotate_vector_about_axis(&forward, &axis, sine(angle), cosine(angle));
	rotate_vector_about_axis(&up, &axis, sine(angle), cosine(angle));
	if (normalize3d(&forward) < 0.0001f)
		return;
	along = dot_product3d(&up, &forward);
	up.i -= forward.i * along;
	up.j -= forward.j * along;
	up.k -= forward.k * along;
	if (normalize3d(&up) < 0.0001f)
		return;
	forge_globals.held_forward = forward;
	forge_globals.held_up = up;
}

/* (each frame of rotate mode) the right stick, or the mouse, turns the
object held: across yaws it, up and down pitch it; with R held too, across
rolls it. Snapping, a flick (past FORGE_ROTATE_FLICK, let back under
FORGE_ROTATE_REARM) or FORGE_ROTATE_MOUSE_STEP of the mouse turns it one
step. The left stick pushes it away (up) and pulls it nearer (down): the
camera stays while it turns */
static void forge_rotate_update(
	struct forge_pad const *pad,
	real seconds)
{
	boolean roll = pad->down[_forge_button_grab];
	real step = DEGREES_TO_RADIANS((real)forge_rotate_snaps[forge_globals.rotate_snap]);
	real stick[NUMBER_OF_FORGE_AXES];
	real mouse[NUMBER_OF_FORGE_AXES];
	short axis;

	stick[_forge_axis_yaw] = roll ? 0.f : -pad->sticks[_gamepad_stick_right][0];
	stick[_forge_axis_pitch] = roll ? 0.f : pad->sticks[_gamepad_stick_right][1];
	stick[_forge_axis_roll] = roll ? pad->sticks[_gamepad_stick_right][0] : 0.f;
	mouse[_forge_axis_yaw] = roll ? 0.f : forge_globals.pending_mouse_turn;
	mouse[_forge_axis_pitch] = roll ? 0.f : forge_globals.pending_mouse_pitch;
	mouse[_forge_axis_roll] = roll ? -forge_globals.pending_mouse_turn : 0.f;
	for (axis = 0; axis < NUMBER_OF_FORGE_AXES; axis++)
	{
		if (step <= 0.f)
		{
			forge_rotate_held(axis, stick[axis] * FORGE_ROTATE_RATE * seconds + mouse[axis] * FORGE_MOUSE_TURN);
			continue;
		}
		if (forge_globals.rotate_flicked[axis])
		{
			if (forge_absolute(stick[axis]) < FORGE_ROTATE_REARM)
				forge_globals.rotate_flicked[axis] = FALSE;
		}
		else if (forge_absolute(stick[axis]) > FORGE_ROTATE_FLICK)
		{
			forge_globals.rotate_flicked[axis] = TRUE;
			forge_rotate_held(axis, stick[axis] > 0.f ? step : -step);
		}
		forge_globals.rotate_mouse[axis] += mouse[axis];
		while (forge_globals.rotate_mouse[axis] >= FORGE_ROTATE_MOUSE_STEP)
		{
			forge_globals.rotate_mouse[axis] -= FORGE_ROTATE_MOUSE_STEP;
			forge_rotate_held(axis, step);
		}
		while (forge_globals.rotate_mouse[axis] <= -FORGE_ROTATE_MOUSE_STEP)
		{
			forge_globals.rotate_mouse[axis] += FORGE_ROTATE_MOUSE_STEP;
			forge_rotate_held(axis, -step);
		}
	}
	forge_globals.held_distance += pad->sticks[_gamepad_stick_left][1] * FORGE_PUSH_RATE * seconds;
}

/* rotate mode's snapping in words: "Off", "45°" */
static void forge_rotate_snap_text(
	char *text,
	size_t size)
{
	short degrees = forge_rotate_snaps[forge_globals.rotate_snap];

	if (degrees > 0)
		snprintf(text, size, "%d\xc2\xb0", degrees);
	else
		snprintf(text, size, "%s", ("Off"));
}

/* an object upright, facing along the ground as it faces */
static void forge_straighten(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	real_vector3d forward;
	char name[FORGE_NAME_SIZE];

	if (!object)
		return;
	forward = object_index == forge_globals.held_object_index ? forge_globals.held_forward : object->object.forward;
	forward.k = 0.f;
	if (normalize3d(&forward) < 0.0001f)
		forward = *global_forward3d;
	forge_set_facing(object_index, &forward, global_up3d);
	forge_object_name(object_index, name, sizeof(name));
	forge_notice(("%s set upright"), name);

	return;
}

static void forge_delete(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	char name[FORGE_NAME_SIZE];

	if (!object || forge_object_is_players(object_index))
		return;
	/* (one of the map's own: taken away from the map's, saved so) */
	if (!forge_map_object_take(object_index, FALSE))
		return;
	forge_object_name(object_index, name, sizeof(name));
	platform_log("forge: deleted %s (%s)", name, forge_tag_leaf(object->definition_index));
	forge_notice(("Deleted %s"), name);
	forge_undo_record(_forge_undo_delete, object_index, NULL);
	if (object_index == forge_globals.held_object_index)
		forge_globals.held_object_index = NONE;
	/* (out of the layout: not kept as gone in play) */
	forge_placed_forget_object(object_index);
	object_delete(object_index);
	forge_globals.deleted_count++;
	forge_placed_update();

	return;
}

/* why forge_place placed nothing */
enum
{
	_forge_place_ok,
	_forge_place_budget,
	/* (an AI character: FORGE_MAXIMUM_AI alive already) */
	_forge_place_ai_budget,
	_forge_place_refused,
};

/* a palette tag placed at position, facing forward with up up, within the
budget (an actor variant: its unit, given an actor - forge_ai.inc); the
object, or NONE with why in *refusal (the player told unless quiet) */
static long forge_place(
	long tag_index,
	real_point3d const *position,
	real_vector3d const *forward,
	real_vector3d const *up,
	boolean quiet,
	short *refusal)
{
	struct object_placement_data placement;
	long definition_index = tag_index;
	long actor_variant_index = NONE;
	long object_index;
	short entry = forge_palette_of_tag(tag_index);
	char name[FORGE_NAME_SIZE];

	*refusal = _forge_place_ok;
	if (entry != NONE)
		snprintf(name, sizeof(name), "%s", forge_globals.palette[entry].name);
	else
		forge_tag_readable_name(tag_index, name, sizeof(name));
	forge_placed_update();
	if (forge_objects_counted() >= forge_objects_budget() || forge_globals.placed_count >= FORGE_MAXIMUM_OBJECTS)
	{
		*refusal = _forge_place_budget;
		if (!quiet)
			forge_notice("Budget full: %d objects. Delete one to place another.", forge_objects_budget());
		return NONE;
	}
	/* (an AI character: its actor variant's unit, at most FORGE_MAXIMUM_AI
	alive) */
	if (tag_get_group_tag(tag_index) == ACTOR_VARIANT_DEFINITION_TAG)
	{
		if (forge_ai_alive() >= forge_ai_maximum())
		{
			*refusal = _forge_place_ai_budget;
			if (!quiet)
				forge_notice("AI full: %d alive.", forge_ai_maximum());
			return NONE;
		}
		actor_variant_index = tag_index;
		definition_index = actor_variant_definition_get(tag_index)->unit_reference.index;
		if (definition_index == NONE)
		{
			*refusal = _forge_place_refused;
			if (!quiet)
				forge_notice("%s cannot be placed here", name);
			return NONE;
		}
	}
	object_placement_data_new(&placement, definition_index, NONE);
	SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
	placement.position = *position;
	placement.forward = *forward;
	placement.up = *up;
	forge_placing_exact = 1;
	object_index = object_new(&placement);
	forge_placing_exact = 0;
	if (object_index == NONE)
	{
		*refusal = _forge_place_refused;
		if (!quiet)
			forge_notice("%s cannot be placed here", name);
		return NONE;
	}
	if (actor_variant_index != NONE)
		forge_ai_attach(object_index, actor_variant_index);
	forge_placed_add(object_index, tag_index);
	/* (a character's team, its default, at once: a multiplayer game gives
	its units none) */
	if (actor_variant_index != NONE)
		forge_ai_apply(forge_placed_of_object(object_index));
	forge_globals.spawned_count++;
	if (!quiet)
	{
		platform_log("forge: spawned %s (%s, %s) at %.2f %.2f %.2f facing %.2f %.2f %.2f; budget %d/%d objects, AI %d/%d",
			name, forge_tag_leaf(tag_index), entry != NONE ? forge_category_log_names[forge_globals.palette[entry].category] : "?",
			placement.position.x, placement.position.y, placement.position.z, placement.forward.i, placement.forward.j,
			placement.forward.k, forge_objects_counted(), forge_objects_budget(), forge_ai_alive(), forge_ai_maximum());
		forge_notice("Placed %s", name);
	}

	return object_index;
}

/* a palette tag spawned in front of the camera (before the structure),
and held, within the budget; the object, or NONE */
static long forge_spawn(
	long tag_index)
{
	real_point3d position;
	real_vector3d forward;
	real_point3d point;
	real_vector3d facing;
	long object_index;
	real distance;
	short refusal;

	if (!forge_camera_get(&position, &forward))
		return NONE;
	distance = MIN(4.f, forge_structure_distance(&position, &forward) - 1.f);
	distance = MAX(distance, FORGE_MINIMUM_HOLD_DISTANCE);
	point.x = position.x + forward.i * distance;
	point.y = position.y + forward.j * distance;
	point.z = position.z + forward.k * distance;
	/* (facing the camera, upright) */
	facing.i = -forward.i;
	facing.j = -forward.j;
	facing.k = 0.f;
	if (normalize3d(&facing) < 0.0001f)
		facing = *global_forward3d;
	object_index = forge_place(tag_index, &point, &facing, global_up3d, FALSE, &refusal);
	if (object_index == NONE)
		return NONE;
	forge_globals.held_object_index = object_index;
	forge_globals.held_distance = distance;
	forge_globals.held_forward = facing;
	forge_globals.held_up = *global_up3d;
	forge_globals.held_spawned = TRUE;

	return object_index;
}

/* a copy of the object held or under the crosshair, held where it was,
facing as it faced (the original left there) */
static void forge_duplicate(
	void)
{
	long object_index = forge_target_object();
	struct object_datum *object = object_try_and_get(object_index);
	long tag_index = NONE;
	real_vector3d forward, up;
	real distance = forge_globals.held_distance;
	boolean held = object_index == forge_globals.held_object_index;
	struct forge_placed original;
	boolean has_original = FALSE;
	short index;

	if (!object)
		return;
	for (index = 0; index < forge_globals.placed_count; index++)
	{
		if (forge_globals.placed[index].object_index == object_index)
		{
			tag_index = forge_globals.placed[index].tag_index;
			original = forge_globals.placed[index];
			has_original = TRUE;
		}
	}
	if (tag_index == NONE && forge_palette_of_tag(object->definition_index) != NONE)
		tag_index = object->definition_index;
	if (tag_index == NONE)
	{
		char name[FORGE_NAME_SIZE];

		forge_object_name(object_index, name, sizeof(name));
		forge_notice(("%s cannot be copied"), name);
		return;
	}
	forward = held ? forge_globals.held_forward : object->object.forward;
	up = held ? forge_globals.held_up : object->object.up;
	if (held)
		forge_drop();
	if (forge_spawn(tag_index) != NONE)
	{
		struct forge_placed *copy = forge_placed_of_object(forge_globals.held_object_index);

		forge_globals.held_forward = forward;
		forge_globals.held_up = up;
		if (held)
			forge_globals.held_distance = distance;
		/* (the copy keeps the original's properties - team, respawn, place at
		start, physics -, not the defaults) */
		if (copy && has_original)
		{
			copy->team = original.team;
			copy->flags = original.flags;
			copy->respawn_seconds = original.respawn_seconds;
			copy->ai_team = original.ai_team;
			copy->move = original.move;
			forge_physics_apply(copy);
			forge_ai_apply(copy);
		}
	}
}

/* the player's unit and what it carries shown or hidden */
static void forge_unit_set_visible(
	long unit_index,
	boolean visible)
{
	struct object_datum *unit = object_try_and_get(unit_index);
	long child_index;

	if (!unit)
		return;
	object_set_visibility(unit_index, visible);
	for (child_index = unit->object.first_child_object_index; child_index != NONE;)
	{
		struct object_datum *child = object_try_and_get(child_index);

		if (!child)
			break;
		object_set_visibility(child_index, visible);
		child_index = child->object.next_object_index;
	}

	return;
}

/* the player's unit as the monitor leaves it: hidden, unhurt, and casting
no shadow (hidden with its weapon, it would start one every frame that
nothing draws into: "empty shadow has been cast"); whatever it was is kept
for the Spartan */
static void forge_unit_hide(
	long unit_index)
{
	struct object_datum *unit = object_try_and_get(unit_index);

	forge_globals.unit_index = unit_index;
	if (!unit)
		return;
	forge_globals.unit_could_take_damage = !TEST_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit);
	SET_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit, TRUE);
	forge_globals.unit_cast_shadow = !TEST_FLAG(unit->object.flags, _object_shadowless_bit);
	SET_FLAG(unit->object.flags, _object_shadowless_bit, TRUE);
	forge_unit_set_visible(unit_index, FALSE);
}

/* the hidden unit as it was before the monitor */
static void forge_unit_show(
	void)
{
	struct object_datum *unit = object_try_and_get(forge_globals.unit_index);

	if (!unit)
		return;
	forge_unit_set_visible(forge_globals.unit_index, TRUE);
	if (forge_globals.unit_could_take_damage)
		SET_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit, FALSE);
	if (forge_globals.unit_cast_shadow)
		SET_FLAG(unit->object.flags, _object_shadowless_bit, FALSE);
}

static void forge_hud_title_update(
	void);

static boolean forge_begin(
	void)
{
	long player_index;
	real_point3d position;
	real_vector3d forward;

	if (forge_globals.active)
		return TRUE;
	if (game_connection() != _game_connection_local || !forge_globals.game_active || forge_globals.playing)
	{
		platform_log("forge: the monitor is only in a Forge game (forge start <map>)");
		return FALSE;
	}
	forge_globals.local_player_index = 0;
	player_index = local_player_get_player_index(forge_globals.local_player_index);
	if (player_index == NONE || !forge_camera_get(&position, &forward))
	{
		platform_log("forge: no local player");
		return FALSE;
	}
	forge_unit_hide(player_get(player_index)->unit_index);
	players_unzoom_all();
	forge_palette_ready();
	forge_globals.held_object_index = NONE;
	director_forge_camera(forge_globals.local_player_index, TRUE, &position, &forward);
	forge_globals.active = TRUE;
	forge_monitor_flying = 1;
	forge_hud_title_update();
	platform_log("forge: monitor mode at %.2f %.2f %.2f (%d objects in the palette)", position.x, position.y, position.z,
		forge_globals.palette_count);

	return TRUE;
}

static void forge_end(
	void)
{
	struct object_datum *unit;
	real_point3d position;
	real_vector3d forward;

	if (!forge_globals.active)
		return;
	if (object_try_and_get(forge_globals.held_object_index))
		forge_drop();
	if (forge_globals.held_marker_id)
		forge_marker_drop();
	unit = object_try_and_get(forge_globals.unit_index);
	if (unit)
	{
		forge_unit_show();
		/* (the player stands where the monitor was; not one in a vehicle's
		seat) */
		if (unit->object.parent_object_index == NONE && forge_camera_get(&position, &forward))
		{
			object_set_position(forge_globals.unit_index, &position, NULL, NULL);
			unit->object.translational_velocity = *global_zero_vector3d;
		}
	}
	director_forge_camera(forge_globals.local_player_index, FALSE, NULL, NULL);
	forge_globals.active = FALSE;
	forge_monitor_flying = 0;
	forge_menu_game_close();
	if (unit)
	{
		platform_log("forge: player mode at %.2f %.2f %.2f, %s (%ld spawned, %ld deleted)", unit->object.position.x,
			unit->object.position.y, unit->object.position.z,
			TEST_FLAG(unit->object.damage_flags, _object_cannot_take_damage_bit) ? "unhurt" : "can be hurt",
			forge_globals.spawned_count, forge_globals.deleted_count);
	}
	else
	{
		platform_log("forge: player mode, waiting to respawn (%ld spawned, %ld deleted)", forge_globals.spawned_count,
			forge_globals.deleted_count);
	}
	forge_globals.unit_index = NONE;

	return;
}

/* the loaded level's name as Forge says it (forge_maps', else the
scenario's own words) */
void forge_level_name(
	char *name,
	short size)
{
	char const *scenario = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
	short map_index;

	for (map_index = 0; scenario && map_index < forge_map_count(); map_index++)
	{
		if (!_stricmp(scenario, forge_maps[map_index].path))
		{
			snprintf(name, size, "%s", (forge_maps[map_index].name));
			return;
		}
	}
	{
		char const *slash = scenario ? strrchr(scenario, '\\') : NULL;

		forge_name_words(slash ? slash + 1 : scenario ? scenario : "", name, size);
	}
}

static void forge_hud_title_update(
	void)
{
	char words[FORGE_NAME_SIZE];

	forge_level_name(words, sizeof(words));
	snprintf(forge_globals.hud_title, sizeof(forge_globals.hud_title), "%s \xc2\xb7 %s", ("FORGE"), words);
}

/* the monitor's HUD's other lines: the budget, and the object held or
under the crosshair */
static void forge_hud_update(
	void)
{
	long object_index;

	/* (with AI characters: those alive, and the dead and waiting apart) */
	if (forge_globals.ai_count > 0 && forge_globals.ai_waiting_count > 0)
		snprintf(forge_globals.hud_budget, sizeof(forge_globals.hud_budget), "Objects %d/%d, AI %d/%d, %d dead, %d waiting",
			forge_objects_counted(), forge_objects_budget(), forge_ai_alive(), forge_ai_maximum(), forge_ai_dead_only(),
			forge_globals.ai_waiting_count);
	else if (forge_globals.ai_count > 0)
		snprintf(forge_globals.hud_budget, sizeof(forge_globals.hud_budget), "Objects %d/%d, AI %d/%d, %d dead",
			forge_objects_counted(), forge_objects_budget(), forge_ai_alive(), forge_ai_maximum(), forge_ai_dead_only());
	else
		snprintf(forge_globals.hud_budget, sizeof(forge_globals.hud_budget), "Objects %d/%d", forge_objects_counted(),
			forge_objects_budget());
	forge_globals.hud_label[0] = 0;
	forge_globals.hud_label_held = object_try_and_get(forge_globals.held_object_index) != NULL;
	object_index = forge_globals.hud_label_held ? forge_globals.held_object_index : forge_object_under_crosshair();
	if (object_index != NONE)
		forge_object_name(object_index, forge_globals.hud_label, sizeof(forge_globals.hud_label));
	/* (a marker held, or under the crosshair before what is behind it:
	"Initial Spawn, Red") */
	{
		real object_distance = forge_grab_range();
		real marker_distance = forge_grab_range();
		long marker = forge_globals.held_marker_id ? forge_globals.held_marker_id :
			forge_globals.hud_label_held ? NONE : forge_marker_under_crosshair(&marker_distance);

		if (marker != NONE && !forge_globals.held_marker_id)
		{
			forge_object_under_crosshair_at(&object_distance);
			if (marker_distance > object_distance + FORGE_MARKER_PICK_RADIUS)
				marker = NONE;
		}
		if (marker != NONE && forge_spawn_of_id(marker))
		{
			forge_spawn_name(forge_spawn_of_id(marker), forge_globals.hud_label, sizeof(forge_globals.hud_label));
			forge_globals.hud_label_held = forge_globals.held_marker_id != 0;
		}
	}
	/* (the layout's object or marker under the crosshair, nothing held: X
	opens its properties) */
	{
		long object_index_hovered;
		long marker_hovered;

		forge_globals.hud_properties = forge_globals.hud_label[0] && !forge_globals.hud_label_held &&
			forge_properties_hovered(&object_index_hovered, &marker_hovered);
	}
	/* (holding one: rotate mode's snapping; rotating, its angles too) */
	forge_globals.hud_rotate[0] = 0;
	if (forge_globals.hud_label_held)
	{
		char snap[16];
		char snapping[64];

		forge_rotate_snap_text(snap, sizeof(snap));
		snprintf(snapping, sizeof(snapping), "Rotation snap: %s", snap);
		if (forge_globals.rotating && forge_globals.nudge_mode)
		{
			snprintf(forge_globals.hud_rotate, sizeof(forge_globals.hud_rotate), "Nudge: %d cm. LT + RS click: rotate",
				forge_nudge_steps[PIN(forge_globals.nudge_step, 0, 3)]);
		}
		else if (forge_globals.rotating)
		{
			float angles[3];

			forge_angles_from_facing(&forge_globals.held_forward, &forge_globals.held_up, angles);
			snprintf(forge_globals.hud_rotate, sizeof(forge_globals.hud_rotate), "Yaw %d, pitch %d, roll %d. %s",
				(int)forge_round(RADIANS_TO_DEGREES(angles[_forge_axis_yaw])),
				(int)forge_round(RADIANS_TO_DEGREES(angles[_forge_axis_pitch])),
				(int)forge_round(RADIANS_TO_DEGREES(angles[_forge_axis_roll])), snapping);
		}
		else
		{
			snprintf(forge_globals.hud_rotate, sizeof(forge_globals.hud_rotate), "%s", snapping);
		}
	}
}

/* ---- undo and redo: a bounded history of what the monitor did (placed,
deleted, moved or turned an object), FORGE_UNDO_DEPTH steps. An object is
named by its placed id (forge_placed: the same id again when an undo
brings it back) or, for one of the map's own, by its object index. What
cannot be undone any more (the object destroyed, the budget full) is said
and skipped, never guessed at */

static void forge_transform_get(
	long object_index,
	struct forge_transform *transform)
{
	struct object_datum *object = object_get(object_index);

	transform->position = object->object.position;
	if (object_index == forge_globals.held_object_index)
	{
		transform->forward = forge_globals.held_forward;
		transform->up = forge_globals.held_up;
	}
	else
	{
		transform->forward = object->object.forward;
		transform->up = object->object.up;
	}
}

static boolean forge_transforms_equal(
	struct forge_transform const *a,
	struct forge_transform const *b)
{
	return distance_squared3d(&a->position, &b->position) < 0.0001f &&
		dot_product3d(&a->forward, &b->forward) > 0.99999f && dot_product3d(&a->up, &b->up) > 0.99999f;
}

/* the object a history step names, NONE if it is gone */
static long forge_undo_object(
	struct forge_undo const *entry)
{
	short index;

	if (entry->id == NONE)
		return forge_object_alive(entry->object_index) ? entry->object_index : NONE;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		if (forge_globals.placed[index].id == entry->id)
			return forge_object_alive(forge_globals.placed[index].object_index) ? forge_globals.placed[index].object_index : NONE;
	}
	return NONE;
}

/* a step of an object filled: what names it, its tag, its properties */
static boolean forge_undo_fill(
	struct forge_undo *entry,
	short kind,
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct forge_placed *placed = forge_placed_of_object(object_index);

	if (!object)
		return FALSE;
	csmemset(entry, 0, sizeof(*entry));
	entry->kind = kind;
	entry->id = placed ? placed->id : NONE;
	entry->object_index = object_index;
	entry->tag_index = placed ? placed->tag_index : object->definition_index;
	entry->team = placed ? placed->team : NONE;
	entry->flags = placed ? placed->flags : FORGE_RECORD_PLACE_AT_START;
	entry->respawn_seconds = placed ? placed->respawn_seconds : FORGE_DEFAULT_RESPAWN_SECONDS;
	entry->ai_team = placed ? placed->ai_team : FORGE_AI_TEAM_DEFAULT;
	if (placed)
		entry->move = placed->move;
	else
		csmemset(&entry->move, 0, sizeof(entry->move));
	forge_transform_get(object_index, &entry->from);
	entry->to = entry->from;
	return TRUE;
}

static void forge_undo_push(
	struct forge_undo const *entry)
{
	/* (a new step: what could be redone is gone; the oldest goes when full) */
	forge_globals.undo_count = forge_globals.undo_cursor;
	if (forge_globals.undo_count >= FORGE_UNDO_DEPTH)
	{
		csmemmove(&forge_globals.undo[0], &forge_globals.undo[1], sizeof(forge_globals.undo[0]) * (FORGE_UNDO_DEPTH - 1));
		forge_globals.undo_count = FORGE_UNDO_DEPTH - 1;
	}
	forge_globals.undo[forge_globals.undo_count++] = *entry;
	forge_globals.undo_cursor = forge_globals.undo_count;
}

static void forge_undo_record(
	short kind,
	long object_index,
	struct forge_transform const *from)
{
	struct forge_undo entry;

	if (!forge_undo_fill(&entry, kind, object_index))
		return;
	if (from)
		entry.from = *from;
	if (kind == _forge_undo_move && forge_transforms_equal(&entry.from, &entry.to))
		return;
	forge_undo_push(&entry);
}

/* a step's words: "Place Warthog" */
static void forge_undo_describe(
	struct forge_undo const *entry,
	char *text,
	size_t size)
{
	static char const *const kinds[NUMBER_OF_FORGE_UNDO_KINDS] = { ("Place %s"), ("Delete %s"), ("Move %s") };
	char name[FORGE_NAME_SIZE];
	short palette = forge_palette_of_tag(entry->tag_index);

	if (palette != NONE)
		snprintf(name, sizeof(name), "%s", forge_globals.palette[palette].name);
	else
		forge_tag_readable_name(entry->tag_index, name, sizeof(name));
	snprintf(text, size, (kinds[PIN(entry->kind, 0, NUMBER_OF_FORGE_UNDO_KINDS - 1)]), name);
}

/* a step's object brought back at a place: under its id again (a map
object's steps given the new one's id); FALSE when it cannot be */
static boolean forge_undo_bring_back(
	struct forge_undo *entry,
	struct forge_transform const *transform)
{
	long object_index;
	short refusal;
	struct forge_placed *placed;

	/* (an entry of its id gone in play is this object: it comes back as
	itself, not twice) */
	forge_placed_forget_id(entry->id);
	object_index = forge_place(entry->tag_index, &transform->position, &transform->forward, &transform->up, TRUE, &refusal);
	if (object_index == NONE)
		return FALSE;
	placed = forge_placed_of_object(object_index);
	if (placed)
	{
		placed->team = entry->team;
		placed->flags = entry->flags;
		placed->respawn_seconds = entry->respawn_seconds;
		placed->ai_team = entry->ai_team;
		placed->move = entry->move;
		forge_physics_apply(placed);
		forge_ai_apply(placed);
		if (entry->id != NONE)
		{
			placed->id = entry->id;
		}
		else
		{
			short index;
			long old_object_index = entry->object_index;

			/* (a map object back as a placed one: every step of it named
			by its new id) */
			for (index = 0; index < forge_globals.undo_count; index++)
			{
				if (forge_globals.undo[index].id == NONE && forge_globals.undo[index].object_index == old_object_index)
					forge_globals.undo[index].id = placed->id;
			}
		}
	}
	return TRUE;
}

static void forge_undo_set_transform(
	long object_index,
	struct forge_transform const *transform)
{
	struct object_datum *object = object_get(object_index);

	object_set_position(object_index, &transform->position, &transform->forward, &transform->up);
	object->object.translational_velocity = *global_zero_vector3d;
	object->object.angular_velocity = *global_zero_vector3d;
	SET_FLAG(object->object.flags, _object_at_rest_bit, FALSE);
	forge_item_unburied(object_index);
	forge_settle(object_index);
}

/* what an undo or a redo came to */
enum
{
	_forge_undo_result_nothing,
	_forge_undo_result_budget,
	_forge_undo_result_gone,
	_forge_undo_result_done,
};

/* the last step undone (redo FALSE), or the next one redone: what came of
it, the step's kind and tag (NONE, NONE with none) */
static short forge_undo_step_result(
	boolean redo,
	short *kind,
	long *tag_index)
{
	struct forge_undo *entry;
	long object_index;
	char words[FORGE_LINE_SIZE];
	boolean done = TRUE;

	*kind = NONE;
	*tag_index = NONE;
	if (redo ? forge_globals.undo_cursor >= forge_globals.undo_count : forge_globals.undo_cursor <= 0)
		return _forge_undo_result_nothing;
	if (object_try_and_get(forge_globals.held_object_index))
		forge_drop();
	entry = &forge_globals.undo[redo ? forge_globals.undo_cursor : forge_globals.undo_cursor - 1];
	*kind = entry->kind;
	*tag_index = entry->tag_index;
	forge_undo_describe(entry, words, sizeof(words));
	object_index = forge_undo_object(entry);
	switch (entry->kind)
	{
	case _forge_undo_place:
	case _forge_undo_delete:
		/* (placing redone or deleting undone: the object back; placing
		undone or deleting redone: the object gone) */
		if ((entry->kind == _forge_undo_place) == redo)
		{
			if (object_index != NONE)
				break;
			if (!forge_undo_bring_back(entry, entry->kind == _forge_undo_place ? &entry->to : &entry->from))
				return _forge_undo_result_budget;
		}
		else
		{
			/* (out of the layout, gone in play or not) */
			if (object_index != NONE)
			{
				if (object_index == forge_globals.held_object_index)
					forge_globals.held_object_index = NONE;
				forge_placed_forget_object(object_index);
				object_delete(object_index);
			}
			forge_placed_forget_id(entry->id);
			forge_placed_update();
		}
		break;
	case _forge_undo_move:
		if (object_index == NONE)
			done = FALSE;
		else
			forge_undo_set_transform(object_index, redo ? &entry->to : &entry->from);
		break;
	}
	forge_globals.undo_cursor += redo ? 1 : -1;
	platform_log("forge: %s %s (%d of %d steps)", redo ? "redid" : "undid", words, forge_globals.undo_cursor,
		forge_globals.undo_count);
	return done ? _forge_undo_result_done : _forge_undo_result_gone;
}

/* the last step undone (redo FALSE), or the next one redone, said */
static void forge_undo_step(
	boolean redo)
{
	struct forge_undo entry;
	char words[FORGE_LINE_SIZE];
	short kind;
	long tag_index;
	short result;

	result = forge_undo_step_result(redo, &kind, &tag_index);
	csmemset(&entry, 0, sizeof(entry));
	entry.kind = (short)PIN(kind, 0, NUMBER_OF_FORGE_UNDO_KINDS - 1);
	entry.tag_index = tag_index;
	words[0] = 0;
	if (tag_index != NONE)
		forge_undo_describe(&entry, words, sizeof(words));
	switch (result)
	{
	case _forge_undo_result_nothing:
		forge_menu_notice("%s", redo ? "Nothing to redo" : "Nothing to undo");
		break;
	case _forge_undo_result_budget:
		forge_menu_notice(redo ? "Can't redo %s: the budget is full" : "Can't undo %s: the budget is full", words);
		break;
	case _forge_undo_result_gone:
		forge_menu_notice(redo ? "Can't redo %s: it is gone" : "Can't undo %s: it is gone", words);
		break;
	default:
		forge_menu_notice(redo ? "Redone: %s" : "Undone: %s", words);
		break;
	}
}

/* ---- snapping (the Forge menu's SNAP): the object held on a grid of
FORGE_SNAP_GRID along the ground, and turned in steps of FORGE_SNAP_ANGLE */

/* the nearest whole number (within the world's +-5000 over the grid) */
static real forge_round(
	real value)
{
	return value >= 0.f ? (real)(long)(value + 0.5f) : -(real)(long)(-value + 0.5f);
}

/* the place and facing an object held is shown at */
static void forge_snapped(
	real_point3d *position,
	real_vector3d *forward,
	real_vector3d *up)
{
	real yaw;
	real snapped;
	real turn;
	real_vector3d axis = *global_up3d;

	if (!forge_globals.snap)
		return;
	position->x = forge_round(position->x / FORGE_SNAP_GRID) * FORGE_SNAP_GRID;
	position->y = forge_round(position->y / FORGE_SNAP_GRID) * FORGE_SNAP_GRID;
	yaw = arctangent(forward->j, forward->i);
	snapped = forge_round(yaw / FORGE_SNAP_ANGLE) * FORGE_SNAP_ANGLE;
	turn = snapped - yaw;
	rotate_vector_about_axis(forward, &axis, sine(turn), cosine(turn));
	rotate_vector_about_axis(up, &axis, sine(turn), cosine(turn));
}

/* ---- object properties (the Forge menu's PROPERTIES, X while holding an
object): its team, respawn time, whether it is placed at the start and its
physics, kept in its placed entry and saved in the layout; its exact angles
(yaw, pitch, roll, forge_angles_from_facing's: the layout keeps all three)
and their reset. A marker's: forge_spawns.inc */

static short const forge_respawn_choices[] = { 0, 10, 20, 30, 45, 60, 90, 120, 180 };

/* the placed entry of the object held, else of the one the crosshair was
on as the menu opened (forge_properties_pick); NULL for none or one of the
map's */
static struct forge_placed *forge_properties_target(
	void)
{
	if (object_try_and_get(forge_globals.held_object_index))
		return forge_placed_of_object(forge_globals.held_object_index);
	if (forge_globals.held_marker_id)
		return NULL;
	if (forge_globals.properties_object_index != NONE && object_try_and_get(forge_globals.properties_object_index))
		return forge_placed_of_object(forge_globals.properties_object_index);
	return NULL;
}

/* the marker the properties are of (the one held, else the one picked),
NULL */
static struct forge_spawn *forge_properties_spawn(
	void)
{
	if (forge_globals.held_marker_id)
		return forge_spawn_of_id(forge_globals.held_marker_id);
	if (object_try_and_get(forge_globals.held_object_index) || !forge_globals.properties_marker_id)
		return NULL;
	return forge_spawn_of_id(forge_globals.properties_marker_id);
}

/* the layout's object or marker under the crosshair, nothing held (the
nearer of the two, as a grab takes): TRUE and which (object_index NONE or
marker_id 0 for the other); FALSE for none, or one of the map's own */
static boolean forge_properties_hovered(
	long *object_index,
	long *marker_id)
{
	real object_distance = FORGE_PICK_RANGE;
	real marker_distance = FORGE_PICK_RANGE;
	long object;
	long marker;

	*object_index = NONE;
	*marker_id = 0;
	if (object_try_and_get(forge_globals.held_object_index) || forge_globals.held_marker_id)
		return FALSE;
	object = forge_object_under_crosshair_at(&object_distance);
	marker = forge_marker_under_crosshair(&marker_distance);
	if (marker != NONE && marker_distance <= object_distance + FORGE_MARKER_PICK_RADIUS && forge_spawn_of_id(marker))
	{
		*marker_id = marker;
		return TRUE;
	}
	if (object != NONE && forge_placed_of_object(object))
	{
		*object_index = object;
		return TRUE;
	}
	return FALSE;
}

/* (X, nothing held) the properties made the ones of what the crosshair is
on: TRUE when it is the layout's */
static boolean forge_properties_pick(
	void)
{
	boolean picked = forge_properties_hovered(&forge_globals.properties_object_index,
		&forge_globals.properties_marker_id);

	if (forge_globals.properties_object_index != NONE)
	{
		char name[FORGE_NAME_SIZE];

		forge_object_name(forge_globals.properties_object_index, name, sizeof(name));
		platform_log("forge: properties of %s (aimed at)", name);
	}
	else if (forge_globals.properties_marker_id)
	{
		forge_spawn_log("properties of (aimed at)", forge_spawn_of_id(forge_globals.properties_marker_id));
	}
	return picked;
}

static void forge_properties_unpick(
	void)
{
	forge_globals.properties_object_index = NONE;
	forge_globals.properties_marker_id = 0;
}

/* the facing the properties show and step: the held object's as it is
held, else the object's own */
static void forge_properties_facing(
	struct forge_placed const *placed,
	real_vector3d *forward,
	real_vector3d *up)
{
	struct object_datum *object = object_try_and_get(placed->object_index);

	if (placed->object_index == forge_globals.held_object_index || !object)
	{
		*forward = forge_globals.held_forward;
		*up = forge_globals.held_up;
	}
	else
	{
		*forward = object->object.forward;
		*up = object->object.up;
	}
}

static void forge_property_apply(
	short property,
	short step)
{
	struct forge_placed *placed = forge_properties_target();
	struct forge_spawn *spawn = forge_properties_spawn();
	short index;

	/* (a marker's: forge_spawns.inc) */
	if (spawn)
	{
		forge_spawn_property_apply(spawn, property, step, spawn->id == forge_globals.held_marker_id);
		return;
	}
	if (!placed)
		return;
	/* (an AI character's AI team, AI and respawn: forge_ai.inc; its
	movement, or a vehicle's route: forge_ai_move.inc) */
	if (forge_ai_move_property_apply(placed, property, step))
		return;
	if (placed->ai && forge_ai_property_apply(placed, property, step))
		return;
	if (!placed->ai && (property == _forge_property_ai_team || property == _forge_property_ai))
		return;
	switch (property)
	{
	case _forge_property_team:
		/* (neutral, red, blue) */
		placed->team = (signed char)(((placed->team + 1 + step + 3) % 3) - 1);
		break;
	case _forge_property_respawn:
		for (index = 0; index < (short)NUMBEROF(forge_respawn_choices) - 1; index++)
		{
			if (forge_respawn_choices[index] >= placed->respawn_seconds)
				break;
		}
		index = (short)((index + step + (short)NUMBEROF(forge_respawn_choices)) % (short)NUMBEROF(forge_respawn_choices));
		placed->respawn_seconds = (unsigned short)forge_respawn_choices[index];
		break;
	case _forge_property_place_at_start:
		placed->flags ^= FORGE_RECORD_PLACE_AT_START;
		break;
	case _forge_property_yaw:
	case _forge_property_pitch:
	case _forge_property_roll:
	case _forge_property_reset_rotation:
	{
		/* (the exact angles: to the next whole step either way - rotate
		mode's snapping, else FORGE_ANGLE_STEP -, yaw and roll round the
		circle, pitch from straight down to straight up; or all none) */
		float angles[3];
		real stepping = (real)(forge_rotate_snaps[forge_globals.rotate_snap] > 0 ?
			forge_rotate_snaps[forge_globals.rotate_snap] : FORGE_ANGLE_STEP);
		real_vector3d forward, up;

		forge_properties_facing(placed, &forward, &up);
		forge_angles_from_facing(&forward, &up, angles);
		if (property == _forge_property_reset_rotation)
		{
			angles[0] = angles[1] = angles[2] = 0.f;
		}
		else
		{
			short axis = (short)(property - _forge_property_yaw);
			real degrees = RADIANS_TO_DEGREES(angles[axis]) / stepping;
			real whole = forge_round(degrees);

			/* (from between two steps, the next one; from a step, the one
			after) */
			if (step > 0)
				degrees = (degrees - whole < -0.01f ? whole : whole + 1.f) * stepping;
			else
				degrees = (degrees - whole > 0.01f ? whole : whole - 1.f) * stepping;
			if (axis == _forge_axis_pitch)
				degrees = PIN(degrees, -90.f, 90.f);
			else if (degrees > 180.f)
				degrees -= 360.f;
			else if (degrees <= -180.f)
				degrees += 360.f;
			angles[axis] = DEGREES_TO_RADIANS(degrees);
		}
		forge_facing_from_angles(angles, &forward, &up);
		forge_set_facing(placed->object_index, &forward, &up);
		break;
	}
	case _forge_property_physics:
	{
		/* (normal, fixed, phased, round: never an AI character's) */
		short mode = forge_physics_mode(placed);

		if (placed->ai)
			return;
		mode = (short)((mode + step + NUMBER_OF_FORGE_PHYSICS_MODES) % NUMBER_OF_FORGE_PHYSICS_MODES);
		placed->flags = (unsigned char)((placed->flags & ~FORGE_RECORD_PHYSICS_MASK) | forge_physics_flags(mode));
		forge_physics_apply(placed);
		platform_log("forge: #%ld's physics %s", placed->id, forge_physics_log_names[mode]);
		break;
	}
	}
	{
		float angles[3];
		char rotation[96];
		real_vector3d forward, up;

		forge_properties_facing(placed, &forward, &up);
		forge_angles_from_facing(&forward, &up, angles);
		snprintf(rotation, sizeof(rotation), "yaw %.1f, pitch %.1f, roll %.1f, facing %.3f %.3f %.3f up %.3f %.3f %.3f",
			RADIANS_TO_DEGREES(angles[0]), RADIANS_TO_DEGREES(angles[1]), RADIANS_TO_DEGREES(angles[2]),
			forward.i, forward.j, forward.k, up.i, up.j, up.k);
		platform_log("forge: properties: team %d, respawn %d s, %splaced at start, physics %s, %s", placed->team,
			placed->respawn_seconds, (placed->flags & FORGE_RECORD_PLACE_AT_START) ? "" : "not ",
			forge_physics_log_names[forge_physics_mode(placed)], rotation);
	}
}

/* ---- layouts (forge_files.h: the file; forge_store.h: the folder) */

/* the layout read for the session to apply as it starts, or the one
being saved (one at a time: the start's is applied before any save) */
static struct forge_file_layout forge_variant_layout;
/* a new round's layout: the session's, taken as the map resets
(forge_map_resetting), placed again once the player has spawned */
static struct forge_file_layout forge_round_layout;

/* a scenario's short name (levels\test\bloodgulch\bloodgulch: bloodgulch),
FALSE if it is not one a layout may name */
static boolean forge_map_short_name(
	char const *scenario,
	char *name,
	int size)
{
	char const *slash = scenario ? strrchr(scenario, '\\') : NULL;
	int index;

	snprintf(name, (size_t)size, "%s", slash ? slash + 1 : scenario ? scenario : "");
	for (index = 0; name[index]; index++)
	{
		if (name[index] >= 'A' && name[index] <= 'Z')
			name[index] = (char)(name[index] - 'A' + 'a');
	}
	return forge_file_map_name_valid(name);
}

/* a facing (forward, up) as yaw, pitch and roll, and back: yaw about up
from +x, pitch up from the ground, roll about forward (the up of no roll,
upright as yaw and pitch leave it, turned toward the left) */
static void forge_angles_from_facing(
	real_vector3d const *forward,
	real_vector3d const *up,
	float angles[3])
{
	real_vector3d left;
	real_vector3d upright;
	real yaw = arctangent(forward->j, forward->i);
	real pitch = arcsine(PIN(forward->k, -1.f, 1.f));

	left.i = -sine(yaw);
	left.j = cosine(yaw);
	left.k = 0.f;
	cross_product3d(forward, &left, &upright);
	angles[0] = forge_file_angle_reduce(yaw);
	angles[1] = forge_file_angle_reduce(pitch);
	angles[2] = forge_file_angle_reduce(arctangent(-dot_product3d(up, &left), dot_product3d(up, &upright)));
}

static void forge_facing_from_angles(
	float const angles[3],
	real_vector3d *forward,
	real_vector3d *up)
{
	real_vector3d left;
	real_vector3d upright;
	real cosine_pitch = cosine(angles[1]);

	forward->i = cosine_pitch * cosine(angles[0]);
	forward->j = cosine_pitch * sine(angles[0]);
	forward->k = sine(angles[1]);
	left.i = -sine(angles[0]);
	left.j = cosine(angles[0]);
	left.k = 0.f;
	cross_product3d(forward, &left, &upright);
	up->i = upright.i * cosine(angles[2]) - left.i * sine(angles[2]);
	up->j = upright.j * cosine(angles[2]) - left.j * sine(angles[2]);
	up->k = upright.k * cosine(angles[2]) - left.k * sine(angles[2]);
	if (normalize3d(forward) < 0.0001f)
		*forward = *global_forward3d;
	if (normalize3d(up) < 0.0001f)
		*up = *global_up3d;
}

/* the session's placed objects as a layout (its paths, its records: place,
facing and properties), and with spawns its markers (version 4's to 6's
records, after the objects': a save's - a new round keeps the markers as
they are); FALSE with why if the map is not one a layout may be of. The map's own objects are
not in it, as the budget does not count them */
static boolean forge_variant_capture(
	struct forge_file_layout *layout,
	char const *title,
	char const *author,
	boolean spawns,
	char *why,
	int why_size)
{
	short index;
	short skipped = 0;

	csmemset(layout, 0, sizeof(*layout));
	if (global_scenario_index == NONE ||
		!forge_map_short_name(tag_get_name(global_scenario_index), layout->map_name, sizeof(layout->map_name)))
	{
		snprintf(why, (size_t)why_size, "%s", ("Layouts can't be saved on this map."));
		return FALSE;
	}
	layout->map_hash = (unsigned int)cache_files_get_checksum();
	forge_placed_update();
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed const *placed = &forge_globals.placed[index];
		struct forge_transform home;
		struct forge_file_record *record;
		unsigned long group_tag = tag_get_group_tag(placed->tag_index);
		char const *name = tag_get_name(placed->tag_index);
		short path;
		short axis;

		/* (where the editor put it: its placed spot, not where play took
		it; one gone in play is still the layout's) */
		if (placed->has_home)
			home = placed->home;
		else if (forge_object_alive(placed->object_index))
			forge_transform_get(placed->object_index, &home);
		else
			continue;
		if (!name || !forge_file_group_allowed((unsigned int)group_tag) || !forge_file_path_valid(name, (int)strlen(name)) ||
			layout->record_count >= FORGE_FILE_MAXIMUM_RECORDS)
		{
			skipped++;
			continue;
		}
		for (path = 0; path < layout->path_count; path++)
		{
			if (layout->paths[path].group_tag == (unsigned int)group_tag && !strcmp(layout->paths[path].path, name))
				break;
		}
		if (path == layout->path_count)
		{
			if (layout->path_count >= FORGE_FILE_MAXIMUM_PATHS)
			{
				skipped++;
				continue;
			}
			layout->paths[path].group_tag = (unsigned int)group_tag;
			snprintf(layout->paths[path].path, sizeof(layout->paths[path].path), "%s", name);
			layout->path_count++;
		}
		record = &layout->records[layout->record_count];
		csmemset(record, 0, sizeof(*record));
		record->path_index = (unsigned short)path;
		record->kind = _forge_record_object;
		record->team = placed->team;
		record->respawn_seconds = (unsigned short)MIN(placed->respawn_seconds, FORGE_FILE_MAXIMUM_RESPAWN_SECONDS);
		record->count = 1;
		record->flags = (unsigned char)(placed->flags & FORGE_RECORD_FLAGS_KNOWN);
		/* (an AI character's settings: version 2 with an AI team or its AI
		off, 3 with a respawn - after a second or more -; never physics) */
		if (placed->ai && group_tag == ACTOR_VARIANT_DEFINITION_TAG)
		{
			record->ai_team = (unsigned char)(placed->ai_team % NUMBER_OF_FORGE_AI_TEAMS);
			record->flags &= (unsigned char)~FORGE_RECORD_PHYSICS_MASK;
			if ((record->flags & FORGE_RECORD_AI_RESPAWN) && !record->respawn_seconds)
				record->flags &= (unsigned char)~FORGE_RECORD_AI_RESPAWN;
		}
		else
		{
			record->flags &= (unsigned char)~(FORGE_RECORD_AI_OFF | FORGE_RECORD_AI_RESPAWN);
		}
		/* (AI movement: version 12, a character's behavior, route and
		options, a vehicle's route) */
		forge_ai_move_capture(placed, (unsigned long)group_tag, record);
		/* (an object's physics: version 7, never both) */
		if ((record->flags & FORGE_RECORD_PHYSICS_MASK) == FORGE_RECORD_PHYSICS_MASK)
			record->flags &= (unsigned char)~FORGE_RECORD_PHYSICS_MASK;
		record->position[0] = home.position.x;
		record->position[1] = home.position.y;
		record->position[2] = home.position.z;
		forge_angles_from_facing(&home.forward, &home.up, record->angles);
		for (axis = 0; axis < 3; axis++)
		{
			if (!(record->position[axis] > -FORGE_FILE_MAXIMUM_COORDINATE + 1.f &&
				record->position[axis] < FORGE_FILE_MAXIMUM_COORDINATE - 1.f))
			{
				break;
			}
		}
		if (axis < 3)
		{
			skipped++;
			continue;
		}
		layout->record_count++;
	}
	if (spawns)
	{
		skipped = (short)(skipped + forge_spawns_capture(layout));
		skipped = (short)(skipped + forge_environment_capture(layout));
		/* (the map's own objects it takes away) */
		forge_map_objects_capture(layout);
	}
	if (skipped)
		platform_log("forge: %d placed objects left out of the layout", skipped);
	snprintf(layout->title, sizeof(layout->title), "%s", title);
	if (author && forge_file_text_valid(author, (int)sizeof(layout->author)))
		snprintf(layout->author, sizeof(layout->author), "%s", author);
	forge_store_date_now(layout->date, sizeof(layout->date));
	return TRUE;
}

/* the session's objects and markers written as a layout titled title (the
session's own layout's file when it has that title), and what came of it
said; TRUE if it was written */
static boolean forge_variant_save(
	char const *title_text)
{
	char why[FORGE_STORE_WHY_SIZE];
	char saved[FORGE_FILE_NAME_SIZE];
	char title[FORGE_FILE_TITLE_SIZE];
	char status[FORGE_STORE_WHY_SIZE];
	char const *file_name = NULL;
	char const *warning;
	boolean done = FALSE;

	snprintf(title, sizeof(title), "%s", title_text ? title_text : "");
	why[0] = 0;
	if (!title[0] || !forge_file_text_valid(title, (int)sizeof(title)))
	{
		snprintf(status, sizeof(status), "%s", "That name can't be used.");
	}
	else if (!forge_variant_capture(&forge_variant_layout, title, NULL, TRUE, why, sizeof(why)))
	{
		snprintf(status, sizeof(status), "%s", why);
	}
	else
	{
		if (forge_globals.variant_file[0] && !strcmp(title, forge_globals.variant_title))
			file_name = forge_globals.variant_file;
		if (forge_store_save(&forge_variant_layout, file_name, saved, sizeof(saved), why, sizeof(why)))
		{
			snprintf(forge_globals.variant_title, sizeof(forge_globals.variant_title), "%s", title);
			snprintf(forge_globals.variant_file, sizeof(forge_globals.variant_file), "%s", saved);
			snprintf(status, sizeof(status), "Saved \"%s\".", title);
			platform_log("forge: layout \"%s\" saved as %s/%s, %d records", title, forge_variant_layout.map_name, saved,
				forge_variant_layout.record_count);
			done = TRUE;
		}
		else
		{
			snprintf(status, sizeof(status), "%s", why);
		}
	}
	if (!done)
		platform_log("forge: the layout was not saved: %s", status);
	/* (saved, but with the flags nearer the other team's spawns: that is
	said too, after the save message) */
	warning = done ? forge_netgame_flags_warning() : NULL;
	forge_menu_notice("%s", status);
	if (warning)
		forge_warning_hud(warning);
	return done;
}

/* (the session's start) the layout read for it placed: each record's
tag found by its path among the palette's (the browser's tags: nothing
else is ever placed, but a weapon nobody can aim, or its ammunition, in a
layout saved before they were left out), its place inside the
structure's world, within the budget. A record whose tag the map does not have, or that cannot be
placed, is skipped and counted, never the variant refused (it passed the
validator whole before it was read). (A new round's layout, round, is
placed the same way, said as a new round) */
static void forge_variant_apply(
	struct forge_file_layout const *layout,
	boolean round)
{
	long tags[FORGE_FILE_MAXIMUM_PATHS];
	short placed = 0;
	short missing = 0;
	short outside = 0;
	short over_budget = 0;
	short refused = 0;
	short other_kinds = 0;
	short waiting = 0;
	short skipped;
	short index;
	struct structure_bsp *structure = global_structure_bsp_get();

	forge_palette_ready();
	/* (its effects and decals of the campaign maps' tags, brought in after
	the map's load: forge_effects.inc) */
	if (!round)
		forge_effects_from_layout_brought_in(layout);
	for (index = 0; index < layout->path_count && index < FORGE_FILE_MAXIMUM_PATHS; index++)
	{
		short entry;

		tags[index] = NONE;
		for (entry = 0; entry < forge_globals.palette_count; entry++)
		{
			long tag_index = forge_globals.palette[entry].tag_index;
			char const *name = tag_get_name(tag_index);

			if ((unsigned int)tag_get_group_tag(tag_index) == layout->paths[index].group_tag && name &&
				!_stricmp(name, layout->paths[index].path))
			{
				tags[index] = tag_index;
				break;
			}
		}
		/* (a weapon nobody can aim, or ammunition only such a weapon uses,
		left out of the palette (#1589) but in a layout saved before that:
		still placed) */
		if (tags[index] == NONE &&
			(layout->paths[index].group_tag == (unsigned int)WEAPON_DEFINITION_TAG ||
			layout->paths[index].group_tag == (unsigned int)EQUIPMENT_DEFINITION_TAG))
		{
			long tag_index = tag_loaded((long)layout->paths[index].group_tag, layout->paths[index].path);

			if (tag_index != NONE &&
				(layout->paths[index].group_tag == (unsigned int)WEAPON_DEFINITION_TAG ?
				forge_weapon_cannot_be_aimed(tag_index) : forge_equipment_only_unaimed_ammunition(tag_index)))
			{
				tags[index] = tag_index;
			}
		}
		/* (an effect's or a decal's path is found by forge_effect_from_record,
		which says so itself when it isn't on the map) */
		if (tags[index] == NONE && layout->paths[index].group_tag != (unsigned int)EFFECT_DEFINITION_TAG &&
			layout->paths[index].group_tag != (unsigned int)DECAL_GROUP_TAG)
		{
			platform_log("forge: the layout's %s is not on this map: its objects are skipped", layout->paths[index].path);
		}
	}
	for (index = 0; index < layout->record_count && index < FORGE_FILE_MAXIMUM_RECORDS; index++)
	{
		struct forge_file_record const *record = &layout->records[index];
		real_point3d position;
		real_vector3d forward;
		real_vector3d up;
		long object_index;
		short refusal;

		/* (a player spawn: taken as the map loaded, forge_spawns_from_layout) */
		if (record->kind == _forge_record_player_spawn && forge_file_layout_version(layout) >= 4)
			continue;
		/* (a teleporter, a game type object: as the map loaded too,
		forge_spawns_from_layout) */
		if ((record->kind == _forge_record_teleporter_in || record->kind == _forge_record_teleporter_out) &&
			forge_file_layout_version(layout) >= 5)
		{
			continue;
		}
		if (record->kind == _forge_record_flag && forge_file_layout_version(layout) >= 6)
			continue;
		/* (the sky and fog: as the map loaded, forge_environment_from_layout) */
		if (record->kind == _forge_record_environment && forge_file_layout_version(layout) >= FORGE_FILE_ENVIRONMENT_VERSION)
			continue;
		/* (a volume: as the map loaded, forge_spawns_from_layout) */
		if (record->kind == _forge_record_volume && forge_file_layout_version(layout) >= FORGE_FILE_VOLUMES_VERSION)
			continue;
		/* (an effect, a decal: as the map loaded, forge_spawns_from_layout) */
		if ((record->kind == _forge_record_effect || record->kind == _forge_record_decal) &&
			forge_file_layout_version(layout) >= FORGE_FILE_EFFECTS_VERSION)
		{
			continue;
		}
		if (record->kind != _forge_record_object)
		{
			other_kinds++;
			continue;
		}
		if (record->path_index >= layout->path_count || tags[record->path_index] == NONE)
		{
			missing++;
			continue;
		}
		position.x = record->position[0];
		position.y = record->position[1];
		position.z = record->position[2];
		if (structure && (position.x < structure->world_bounds.x0 - FORGE_WORLD_BOUNDS_MARGIN ||
			position.x > structure->world_bounds.x1 + FORGE_WORLD_BOUNDS_MARGIN ||
			position.y < structure->world_bounds.y0 - FORGE_WORLD_BOUNDS_MARGIN ||
			position.y > structure->world_bounds.y1 + FORGE_WORLD_BOUNDS_MARGIN ||
			position.z < structure->world_bounds.z0 - FORGE_WORLD_BOUNDS_MARGIN ||
			position.z > structure->world_bounds.z1 + FORGE_WORLD_BOUNDS_MARGIN))
		{
			outside++;
			continue;
		}
		forge_facing_from_angles(record->angles, &forward, &up);
		object_index = forge_place(tags[record->path_index], &position, &forward, &up, TRUE, &refusal);
		/* (an AI character past FORGE_MAXIMUM_AI alive: kept, waiting for
		room - placed as soon as one alive is fewer: forge_ai.inc) */
		if (object_index == NONE && refusal == _forge_place_ai_budget && forge_globals.placed_count < FORGE_MAXIMUM_OBJECTS)
		{
			struct forge_placed *entry;

			forge_placed_add(NONE, tags[record->path_index]);
			entry = &forge_globals.placed[forge_globals.placed_count - 1];
			entry->team = record->team;
			entry->flags = record->flags;
			entry->respawn_seconds = record->respawn_seconds;
			entry->ai_team = (unsigned char)(record->ai_team % NUMBER_OF_FORGE_AI_TEAMS);
			forge_ai_move_from_record(entry, record);
			entry->home.position = position;
			entry->home.forward = forward;
			entry->home.up = up;
			entry->has_home = TRUE;
			entry->waiting = TRUE;
			waiting++;
			continue;
		}
		if (object_index == NONE)
		{
			if (refusal == _forge_place_budget || refusal == _forge_place_ai_budget)
				over_budget++;
			else
				refused++;
			continue;
		}
		if (forge_globals.placed_count > 0)
		{
			struct forge_placed *entry = &forge_globals.placed[forge_globals.placed_count - 1];

			entry->team = record->team;
			entry->flags = record->flags;
			entry->respawn_seconds = record->respawn_seconds;
			forge_ai_move_from_record(entry, record);
			if (entry->ai)
			{
				entry->ai_team = (unsigned char)(record->ai_team % NUMBER_OF_FORGE_AI_TEAMS);
				entry->flags &= (unsigned char)~FORGE_RECORD_PHYSICS_MASK;
				forge_ai_apply(entry);
			}
			else
			{
				entry->flags &= (unsigned char)~(FORGE_RECORD_AI_OFF | FORGE_RECORD_AI_RESPAWN);
				forge_physics_apply(entry);
			}
		}
		placed++;
	}
	skipped = (short)(missing + outside + over_budget + refused + other_kinds);
	if (round)
	{
		platform_log("forge: new round %ld: %d placed again, %d skipped (%d missing tags, %d outside the map, %d past the "
			"budget, %d refused), %d AI waiting for room", forge_globals.round_count, placed, skipped, missing, outside,
			over_budget, refused, waiting);
		forge_notice(("New round: %d objects placed"), placed);
		return;
	}
	if (layout->map_hash != (unsigned int)cache_files_get_checksum())
		platform_log("forge: the layout was made with another copy of this map (%08x, this one %08lx)", layout->map_hash,
			(unsigned long)cache_files_get_checksum());
	platform_log("forge: layout \"%s\" applied: %d placed, %d skipped (%d missing tags, %d outside the map, %d past the "
		"budget, %d refused, %d not objects), %d AI waiting for room", layout->title, placed, skipped, missing, outside,
		over_budget, refused, other_kinds, waiting);
	if (over_budget)
	{
		char warning[FORGE_LINE_SIZE];

		snprintf(warning, sizeof(warning), "Layout %s: %d placed, %d past the budget of %d objects", layout->title, placed,
			over_budget, forge_objects_budget());
		forge_warning_hud(warning);
	}
	else if (skipped)
		forge_notice(("Layout %s: %d placed, %d skipped"), layout->title, placed, skipped);
	else
		forge_notice(("Layout %s: %d placed"), layout->title, placed);
}

/* the session: the Forge game asked for, once its map is the one loaded
and its player has spawned, starts with the monitor; a layout played is
placed once its player has spawned; another map's state is gone */
static void forge_session_update(
	void)
{
	long player_index;

	if (!forge_globals.game_active || forge_globals.reload_pending)
		return;
	/* (a multiplayer map's AI characters not brought in yet: the session,
	and its layout, wait for them - forge_ai_tags.c) */
	if (forge_ai_tags_pending())
		return;
	/* (a local game only: never a network game's, hosted or joined) */
	if (game_connection() != _game_connection_local)
	{
		forge_globals.game_active = FALSE;
		forge_globals.game_requested = FALSE;
		return;
	}
	if (forge_globals.game_requested && forge_globals.playing)
	{
		/* (a layout played: its layout placed once the player has spawned,
		the player the Spartan) */
		player_index = local_player_get_player_index(0);
		if (player_index != NONE && player_get(player_index)->unit_index != NONE)
		{
			forge_globals.game_requested = FALSE;
			forge_palette_ready();
			platform_log("forge: the layout \"%s\" is played on %s", forge_globals.variant_title, forge_globals.game_map);
			if (forge_globals.variant_pending)
			{
				forge_globals.variant_pending = FALSE;
				forge_variant_apply(&forge_variant_layout, FALSE);
			}
		}
	}
	if (forge_globals.game_requested && !forge_globals.playing)
	{
		player_index = local_player_get_player_index(0);
		if (player_index != NONE && player_get(player_index)->unit_index != NONE && forge_begin())
		{
			forge_globals.game_requested = FALSE;
			platform_log("forge: the Forge game of %s starts", forge_globals.game_map);
			/* (a multiplayer map: the AI characters its campaign maps gave,
			said, and the factions none of whose campaign maps is there) */
			if (forge_packs_of(NONE) > 0)
			{
				char found[96] = "";
				char missing[96] = "";
				short faction;

				for (faction = 0; faction < forge_ai_tags_faction_count(); faction++)
				{
					char const *name = "";
					char const *map = NULL;
					char *list;

					forge_ai_tags_faction(faction, &name, &map);
					list = map ? found : missing;
					snprintf(list + strlen(list), sizeof(found) - strlen(list), "%s%s", list[0] ? ", " : "", name);
				}
				if (missing[0])
					forge_warning_hud(found[0] ? "Campaign characters and objects: some of their campaign maps are missing." :
						"No campaign characters or objects: no campaign maps in the maps folder.");
				platform_log("forge: AI characters from the campaign maps: %s; none for: %s", found[0] ? found : "none",
					missing[0] ? missing : "none");
			}
			if (forge_globals.variant_pending)
			{
				forge_globals.variant_pending = FALSE;
				forge_variant_apply(&forge_variant_layout, FALSE);
			}
		}
	}
	/* (a new round: once the player has spawned again, the monitor where
	it flew, if it did, and the layout placed again) */
	if (forge_globals.round_pending && !forge_globals.game_requested)
	{
		player_index = local_player_get_player_index(0);
		if (player_index != NONE && player_get(player_index)->unit_index != NONE)
		{
			forge_globals.round_pending = FALSE;
			if (forge_globals.round_monitor && forge_begin())
			{
				director_forge_camera(forge_globals.local_player_index, TRUE, &forge_globals.round_camera.position,
					&forge_globals.round_camera.forward);
			}
			forge_variant_apply(&forge_round_layout, TRUE);
		}
	}
}

/* (forge_update, the Forge menu's NEW ROUND, no menu over the game) the
map reset by the engine, as its own map_reset does it: forge_map_resetting
takes the layout as the game's state goes, forge_map_reset and the session
place it again */
static void forge_new_round_start(
	void)
{
	real_point3d position;
	real_vector3d forward;

	if (object_try_and_get(forge_globals.held_object_index))
		forge_drop();
	forge_globals.round_monitor = forge_globals.active;
	if (forge_globals.active && forge_camera_get(&position, &forward))
	{
		forge_globals.round_camera.position = position;
		forge_globals.round_camera.forward = forward;
	}
	else
	{
		forge_globals.round_monitor = FALSE;
	}
	forge_globals.round_resetting = TRUE;
	platform_log("forge: new round asked: the map resets, the layout's %d objects placed again (as the %s)",
		forge_globals.placed_count, forge_globals.round_monitor ? "monitor" : "Spartan");
	main_reset_map();
}

/* ---------- public code */

/* the budget and the AI's most alive, as set (game.forge_objects,
game.forge_ai), clamped to the build's: read as a Forge game's map loads */
static short forge_budget_set = FORGE_MAXIMUM_OBJECTS;
static short forge_ai_set = FORGE_MAXIMUM_AI;

static void forge_limits_read(
	void)
{
	long objects = config_integer("game.forge_objects");
	long ai = config_integer("game.forge_ai");

	forge_budget_set = (short)PIN(objects, FORGE_MINIMUM_OBJECTS, FORGE_MAXIMUM_OBJECTS);
	forge_ai_set = (short)PIN(ai, FORGE_MINIMUM_AI, FORGE_MAXIMUM_AI);
	if (forge_budget_set != objects || forge_ai_set != ai)
		platform_log("forge: game.forge_objects %ld and game.forge_ai %ld taken as %d and %d (this build: %d to %d, %d to %d)",
			objects, ai, forge_budget_set, forge_ai_set, FORGE_MINIMUM_OBJECTS, FORGE_MAXIMUM_OBJECTS, FORGE_MINIMUM_AI,
			FORGE_MAXIMUM_AI);
}

short forge_objects_budget(
	void)
{
	return forge_budget_set;
}

short forge_ai_maximum(
	void)
{
	return forge_ai_set;
}

/* whether Forge is on (game.forge): off, nothing of it runs */
boolean forge_enabled(
	void)
{
	return config_boolean("game.forge") != 0;
}

short forge_map_count(
	void)
{
	return (short)NUMBEROF(forge_maps);
}

/* the map whose level `level_name` is, else NONE */
static short forge_map_of_level(
	char const *level_name)
{
	short map_index;

	for (map_index = 0; level_name && map_index < forge_map_count(); map_index++)
	{
		if (!_stricmp(level_name, forge_maps[map_index].path))
			return map_index;
	}
	return NONE;
}

/* a map by its index, its short name (bloodgulch) or its level path, NONE */
short forge_map_of_word(
	char const *word)
{
	short map_index;
	char *end = NULL;
	long number;

	if (!word || !word[0])
		return NONE;
	number = strtol(word, &end, 10);
	if (end && *end == 0)
		return number >= 0 && number < forge_map_count() ? (short)number : NONE;
	map_index = forge_map_of_short(word);
	if (map_index != NONE)
		return map_index;
	return forge_map_of_level(word);
}

char const *forge_map_path(
	short map_index)
{
	return map_index >= 0 && map_index < forge_map_count() ? forge_maps[map_index].path : NULL;
}

char const *forge_map_name(
	short map_index)
{
	return map_index >= 0 && map_index < forge_map_count() ? forge_maps[map_index].name : "";
}

char const *forge_map_short(
	short map_index)
{
	static char names[NUMBEROF(forge_maps)][FORGE_FILE_MAP_NAME_SIZE];

	if (map_index < 0 || map_index >= forge_map_count())
		return "";
	if (!names[map_index][0])
		forge_map_short_name(forge_maps[map_index].path, names[map_index], sizeof(names[map_index]));
	return names[map_index];
}

short forge_map_of_short(
	char const *map_name)
{
	short map_index;

	for (map_index = 0; map_name && map_index < forge_map_count(); map_index++)
	{
		if (!_stricmp(map_name, forge_map_short(map_index)))
			return map_index;
	}
	return NONE;
}

/* (free for all slayer: the map's own vehicles and weapons, players
respawning at once; a score no one alone reaches and no time limit) */
void forge_variant_build(
	struct game_variant *variant)
{
	static wchar_t const name[] = L"Forge";

	build_game_variant_slayer(variant);
	csmemset(variant->human_readable_game_description, 0, sizeof(variant->human_readable_game_description));
	csmemcpy(variant->human_readable_game_description, name, sizeof(name));
	variant->universal_variant.score_to_win = 999;
	variant->universal_variant.respawn_time = 0;
	variant->universal_variant.suicide_penalty = 0;
}

/* a layout of a map read and checked for the session (forge_variant_layout),
FALSE with why: nothing is changed then */
static boolean forge_layout_read(
	short map_index,
	char const *variant_file,
	char *why,
	int why_size)
{
	if (!forge_store_load(forge_map_short(map_index), variant_file, &forge_variant_layout, why, why_size))
	{
		platform_log("forge: the layout %s of %s was not read: %s", variant_file, forge_maps[map_index].path, why);
		forge_globals.variant_pending = FALSE;
		return FALSE;
	}
	snprintf(forge_globals.variant_title, sizeof(forge_globals.variant_title), "%s", forge_variant_layout.title);
	snprintf(forge_globals.variant_file, sizeof(forge_globals.variant_file), "%s", variant_file);
	forge_globals.variant_pending = TRUE;
	if (forge_variant_layout.pack_count)
		platform_log("forge: the layout names %d scene packs, which this build has not: their objects are skipped",
			forge_variant_layout.pack_count);
	platform_log("forge: layout \"%s\" (%s, %d records)", forge_variant_layout.title, variant_file,
		forge_variant_layout.record_count);
	return TRUE;
}

/* the game asked for: a local game of the map with the variant, as "main
menu switch to solo game" starts it */
static void forge_game_ask(
	short map_index,
	struct game_variant *variant,
	boolean playing)
{
	game_connection_set(_game_connection_local);
	game_set_game_variant(variant);
	main_set_map_name((char *)forge_map_path(map_index));
	/* (no saved game read or written: the campaign's is left alone) */
	main_disallow_persistent_storage();
	csstrncpy(forge_globals.game_map, forge_maps[map_index].path, sizeof(forge_globals.game_map) - 1);
	forge_globals.game_map[sizeof(forge_globals.game_map) - 1] = 0;
	forge_globals.game_requested = TRUE;
	forge_globals.playing = playing;
	forge_globals.reload_pending = forge_globals.game_active;
	platform_log("forge: a %s of %s asked for", playing ? "game of a layout" : "Forge game", forge_globals.game_map);
	main_menu_switch_to_single_player();
}

boolean forge_game_start(
	short map_index,
	char const *variant_file,
	char *why,
	int why_size)
{
	struct game_variant variant;

	if (map_index < 0 || map_index >= forge_map_count())
		return FALSE;
	forge_globals.playing = FALSE;
	forge_globals.variant_pending = FALSE;
	forge_globals.variant_title[0] = 0;
	forge_globals.variant_file[0] = 0;
	/* (the layout chosen, read and checked whole before the game is asked
	for: one that cannot be read starts nothing) */
	if (variant_file && variant_file[0] && !forge_layout_read(map_index, variant_file, why, why_size))
		return FALSE;
	forge_variant_build(&variant);
	forge_game_ask(map_index, &variant, FALSE);
	return TRUE;
}

/* the game types a layout is played as (forge play <map> <layout> <type>) */
static struct
{
	char const *word;
	struct game_variant *(*build)(struct game_variant *variant);
} const forge_play_types[] =
{
	{ "slayer", build_game_variant_slayer },
	{ "teamslayer", build_game_variant_team_slayer },
	{ "ctf", build_game_variant_ctf },
	{ "king", build_game_variant_king },
	{ "oddball", build_game_variant_oddball },
	{ "race", build_game_variant_race },
};

boolean forge_play_start(
	short map_index,
	char const *variant_file,
	char const *game_type,
	char *why,
	int why_size)
{
	struct game_variant variant;
	short type = 0;

	if (map_index < 0 || map_index >= forge_map_count())
		return FALSE;
	if (game_type && game_type[0])
	{
		for (type = 0; type < (short)NUMBEROF(forge_play_types); type++)
		{
			if (!_stricmp(game_type, forge_play_types[type].word))
				break;
		}
		if (type >= (short)NUMBEROF(forge_play_types))
		{
			snprintf(why, (size_t)why_size, "no game type \"%s\" (slayer, teamslayer, ctf, king, oddball, race)", game_type);
			return FALSE;
		}
	}
	forge_globals.variant_pending = FALSE;
	forge_globals.variant_title[0] = 0;
	forge_globals.variant_file[0] = 0;
	if (!variant_file || !variant_file[0] || !forge_layout_read(map_index, variant_file, why, why_size))
		return FALSE;
	forge_play_types[type].build(&variant);
	forge_game_ask(map_index, &variant, TRUE);
	return TRUE;
}

char const *forge_variant_title(
	void)
{
	return forge_globals.variant_title;
}

boolean forge_snap_enabled(
	void)
{
	return forge_globals.snap;
}

boolean forge_properties_get(
	struct forge_properties *properties)
{
	struct forge_placed *placed = forge_properties_target();
	struct forge_spawn *spawn = forge_properties_spawn();

	csmemset(properties, 0, sizeof(*properties));
	if (spawn)
	{
		forge_spawn_properties_get(spawn, properties);
		/* (where it stands, meters: forge_precision.inc) */
		{
			real_point3d const *at = spawn->id == forge_globals.held_marker_id ? &forge_globals.held_marker_position :
				&spawn->home.position;

			properties->has_position = TRUE;
			properties->position[0] = at->x * FORGE_METRES_PER_WORLD_UNIT;
			properties->position[1] = at->y * FORGE_METRES_PER_WORLD_UNIT;
			properties->position[2] = at->z * FORGE_METRES_PER_WORLD_UNIT;
		}
		return TRUE;
	}
	if (!placed)
		return FALSE;
	{
		struct object_datum *object = object_try_and_get(placed->object_index);
		real_point3d const *at = object ? &object->object.position : placed->has_home ? &placed->home.position : NULL;

		if (at)
		{
			properties->has_position = TRUE;
			properties->position[0] = at->x * FORGE_METRES_PER_WORLD_UNIT;
			properties->position[1] = at->y * FORGE_METRES_PER_WORLD_UNIT;
			properties->position[2] = at->z * FORGE_METRES_PER_WORLD_UNIT;
		}
	}
	forge_object_name(placed->object_index, properties->name, sizeof(properties->name));
	properties->team = placed->team;
	properties->respawn_seconds = placed->respawn_seconds;
	properties->place_at_start = (placed->flags & FORGE_RECORD_PLACE_AT_START) != 0;
	{
		float angles[3];
		real_vector3d forward, up;

		forge_properties_facing(placed, &forward, &up);
		forge_angles_from_facing(&forward, &up, angles);
		properties->yaw = (short)forge_round(RADIANS_TO_DEGREES(angles[0]));
		properties->pitch = (short)forge_round(RADIANS_TO_DEGREES(angles[1]));
		properties->roll = (short)forge_round(RADIANS_TO_DEGREES(angles[2]));
	}
	properties->has_physics = !placed->ai;
	properties->physics = forge_physics_mode(placed);
	/* (an AI character's: forge_ai.inc) */
	properties->character = placed->ai;
	properties->ai_team = (short)(placed->ai_team % NUMBER_OF_FORGE_AI_TEAMS);
	properties->ai_on = !(placed->flags & FORGE_RECORD_AI_OFF);
	forge_ai_move_properties_get(placed, properties);
	if (placed->ai && !(placed->flags & FORGE_RECORD_AI_RESPAWN))
		properties->respawn_seconds = 0;
	return TRUE;
}

void forge_map_resetting(
	void)
{
	char why[FORGE_STORE_WHY_SIZE];

	if (!forge_globals.game_active || forge_globals.game_requested)
	{
		forge_globals.round_resetting = FALSE;
		return;
	}
	/* (a reset not asked by NEW ROUND - the console's map_reset - keeps the
	layout too, the monitor where it was) */
	if (!forge_globals.round_resetting)
	{
		real_point3d position;
		real_vector3d forward;

		forge_globals.round_monitor = forge_globals.active && forge_camera_get(&position, &forward);
		if (forge_globals.round_monitor)
		{
			forge_globals.round_camera.position = position;
			forge_globals.round_camera.forward = forward;
		}
	}
	forge_placed_update();
	forge_homes_update();
	if (!forge_variant_capture(&forge_round_layout, "New Round", NULL, FALSE, why, sizeof(why)))
	{
		platform_log("forge: new round: the layout was not taken: %s", why);
		csmemset(&forge_round_layout, 0, sizeof(forge_round_layout));
	}
	forge_globals.round_count++;
	forge_globals.round_pending = TRUE;
	/* (the map's objects go: the placements make them again, but for the
	ones the session takes away) */
	forge_map_made_clear();
	platform_log("forge: new round %ld: the layout taken: %d objects", forge_globals.round_count,
		forge_round_layout.record_count);
}

void forge_map_reset(
	void)
{
	/* (every object is gone, its indices with it: the layout waits in
	forge_round_layout; the undo history's steps name objects no more) */
	forge_globals.placed_count = 0;
	forge_globals.ai_count = forge_globals.ai_dead_count = forge_globals.ai_waiting_count = 0;
	forge_globals.held_object_index = NONE;
	forge_globals.held_spawned = FALSE;
	forge_globals.undo_count = 0;
	forge_globals.undo_cursor = 0;
	forge_globals.round_requested = FALSE;
	forge_globals.round_resetting = FALSE;
	forge_menu_game_close();
	/* (the markers kept; a new round's first spawns at its initial spawns
	again) */
	forge_globals.spawns_spawned = 0;
	csmemset(forge_globals.spawn_wait_since, 0, sizeof(forge_globals.spawn_wait_since));
	forge_globals.held_marker_id = 0;
	if (forge_globals.active)
	{
		/* (the director starts over with the map) */
		forge_globals.active = FALSE;
		forge_monitor_flying = 0;
	}
	forge_globals.unit_index = NONE;
	if (forge_globals.game_active)
		platform_log("forge: the map was reset: the layout is placed again once the player has spawned");
}

void forge_map_loaded(
	char const *scenario_name)
{
#if HALO_AI_NAV
	/* (the last map's navigation let go: forge_ai_move.inc) */
	ai_nav_map_reset();
#endif
	/* (every object of the last map is gone, its indices with them) */
	forge_globals.placed_count = 0;
	forge_globals.ai_count = forge_globals.ai_dead_count = forge_globals.ai_waiting_count = 0;
	forge_globals.held_marker_id = 0;
	forge_globals.held_object_index = NONE;
	forge_globals.held_spawned = FALSE;
	forge_globals.undo_count = 0;
	forge_globals.undo_cursor = 0;
	forge_globals.menu_status[0] = 0;
	forge_globals.palette_built = FALSE;
	forge_effect_palette_reset();
	forge_globals.spawned_count = 0;
	forge_globals.deleted_count = 0;
	forge_globals.notice[0] = 0;
	forge_globals.round_requested = FALSE;
	forge_globals.round_resetting = FALSE;
	forge_globals.round_pending = FALSE;
	forge_menu_game_close();
	if (forge_globals.active)
	{
		/* (the director starts over with the map) */
		forge_globals.active = FALSE;
		forge_monitor_flying = 0;
		forge_globals.unit_index = NONE;
	}
	/* (the budget and the AI's most, as set now) */
	if (forge_globals.game_requested)
		forge_limits_read();
	/* (the map asked for is the one loaded, by its path; a local game only) */
	forge_globals.reload_pending = FALSE;
	forge_globals.game_active = forge_globals.game_requested && scenario_name &&
		!_stricmp(scenario_name, forge_globals.game_map) && game_connection() == _game_connection_local;
	if (forge_globals.game_requested && !forge_globals.game_active)
		platform_log("forge: a Forge game of %s was asked for, but %s loaded", forge_globals.game_map,
			scenario_name ? scenario_name : "nothing");
	/* (a campaign mission's: its scripts and encounters left out, forge_ai.inc) */
	forge_globals.campaign = forge_globals.game_active && forge_map_of_level(scenario_name) != NONE &&
		forge_maps[forge_map_of_level(scenario_name)].campaign;
	if (forge_globals.campaign)
		platform_log("forge: %s is a campaign mission: its scripts and encounters are left out", scenario_name);
	/* (the layout's markers: the layout's, before anyone spawns -
	forge_spawns.inc; none of another map's, nor in another game) */
	forge_spawns_clear();
	forge_environment_clear();
	if (forge_globals.game_active && forge_globals.variant_pending)
	{
		forge_spawns_from_layout(&forge_variant_layout);
		forge_environment_from_layout(&forge_variant_layout);
	}
	/* (the map's own objects it takes away, before the map makes them) */
	forge_map_objects_start(forge_globals.game_active && forge_globals.variant_pending ? &forge_variant_layout : NULL);
	/* (its teleporters and flags the engine's netgame flags as its game
	starts: forge_netgame.inc; any other game's the map's own) */
	forge_netgame_changed();
	if (!forge_globals.game_active)
	{
		forge_globals.game_requested = FALSE;
		forge_globals.playing = FALSE;
		forge_globals.variant_pending = FALSE;
	}
	else
	{
		platform_log("forge: %s loaded for a %s", scenario_name, forge_globals.playing ? "game of a layout" : "Forge game");
	}
}

boolean forge_game_active(
	void)
{
	/* (a layout played is the game's own game, its menus, scores and
	controls: not Forge's) */
	return forge_globals.game_active && !forge_globals.playing;
}

boolean forge_variant_playing(
	void)
{
	return forge_globals.game_active && forge_globals.playing;
}

boolean forge_monitor_active(
	void)
{
	return forge_globals.active;
}

/* whether the HUD leaves the Spartan's action prompt undrawn for local
player `local_player_index`: as the monitor, the player's unit stands
hidden where it was left, and the game still works out its action every
tick (a weapon let go at its feet: "Hold X to pick up"), but the monitor's
controls never reach it (hud.c hud_show_action_response) */
boolean forge_monitor_hides_prompt(
	short local_player_index)
{
	return forge_globals.active && local_player_index == forge_globals.local_player_index;
}

/* whether the mouse's look is the monitor's for local player
`local_player_index`: the player's own input (player_control.c) runs before
the cameras each frame, and took the motion the monitor's camera
(forge_camera_input) reads, for the hidden unit (#1587) */
boolean forge_monitor_takes_mouse(
	short local_player_index)
{
	return forge_globals.active && local_player_index == forge_globals.local_player_index;
}

char const *forge_category_name(
	short category)
{
	return category >= 0 && category < NUMBER_OF_FORGE_CATEGORIES ? (forge_category_names[category]) : "";
}

short forge_category_item_count(
	short category)
{
	if (category < 0 || category >= NUMBER_OF_FORGE_CATEGORIES)
		return 0;
	/* (EFFECTS, DECALS: the map's, in a Forge game) */
	if (category == _forge_category_effects || category == _forge_category_decals)
		return forge_globals.game_active && !forge_globals.playing ? forge_effect_category_count(category) : 0;
	/* (SPAWNING, TELEPORTERS, OBJECTIVES: their markers' kinds, in a Forge
	game) */
	if (forge_marker_category_first(category) != NONE)
		return forge_globals.game_active && !forge_globals.playing ? forge_marker_category_count(category) : 0;
	forge_palette_ready();
	return forge_globals.palette_counts[category];
}

boolean forge_category_item(
	short category,
	short item_index,
	struct forge_item *item)
{
	struct forge_palette_entry const *entry;

	if (item_index < 0 || item_index >= forge_category_item_count(category))
		return FALSE;
	csmemset(item, 0, sizeof(*item));
	if (category == _forge_category_effects || category == _forge_category_decals)
		return forge_effect_category_item(category, item_index, item);
	if (forge_marker_category_first(category) != NONE)
	{
		short kind = (short)(forge_marker_category_first(category) + item_index);

		snprintf(item->name, sizeof(item->name), "%s", forge_spawn_kind_names[kind]);
		item->placed = forge_spawns_of_kind(kind);
		return TRUE;
	}
	entry = &forge_globals.palette[forge_globals.palette_first[category] + item_index];
	csmemcpy(item->name, entry->name, sizeof(item->name));
	item->placed = forge_placed_of_tag(entry->tag_index);
	/* (brought in from a campaign map: its faction, or pack, and mission) */
	if (forge_ai_tags_source_map(entry->tag_index))
	{
		short faction = forge_ai_tags_faction_of(entry->tag_index);
		char const *name = "";
		char const *map = NULL;

		if (faction != NONE)
			forge_ai_tags_faction(faction, &name, &map);
		snprintf(item->faction, sizeof(item->faction), "%s", name);
		snprintf(item->from, sizeof(item->from), "%s", forge_mission_title(forge_ai_tags_source_map(entry->tag_index)));
	}
	return TRUE;
}

boolean forge_ai_tags_wanted(
	char const *scenario_name)
{
	short map_index;

	if (!forge_enabled() || !forge_globals.game_requested || !scenario_name ||
		_stricmp(scenario_name, forge_globals.game_map) || game_connection() != _game_connection_local)
	{
		return FALSE;
	}
	map_index = forge_map_of_level(scenario_name);
	return map_index != NONE && !forge_maps[map_index].campaign;
}

char const *forge_mission_title(
	char const *short_name)
{
	short map_index = short_name ? forge_map_of_short(short_name) : NONE;

	return map_index != NONE ? forge_maps[map_index].name : "";
}

/* the packs' category of one of the Forge menu's (NONE: none) */
static short forge_pack_category(
	short category)
{
	return category == _forge_category_ai ? FORGE_AI_PACK_AI :
		category == _forge_category_campaign_vehicles ? FORGE_AI_PACK_VEHICLES :
		category == _forge_category_campaign_objects ? FORGE_AI_PACK_OBJECTS : NONE;
}

/* (a multiplayer map's Forge game) the packs of a category, or all (NONE) */
static short forge_packs_of(
	short pack_category)
{
	short map_index = forge_globals.game_active ? forge_map_of_level(forge_globals.game_map) : NONE;
	short faction;
	short count = 0;

	if (map_index == NONE || forge_maps[map_index].campaign)
		return 0;
	for (faction = 0; faction < forge_ai_tags_faction_count(); faction++)
		count += pack_category == NONE || forge_ai_tags_faction_category(faction) == pack_category ? 1 : 0;
	return count;
}

short forge_ai_faction_lines(
	short category)
{
	short pack_category = forge_pack_category(category);

	return pack_category != NONE ? forge_packs_of(pack_category) : 0;
}

short forge_ai_faction_line(
	short category,
	short line_index,
	char *line,
	int size,
	boolean *missing)
{
	char const *name = "";
	char const *map = NULL;
	short pack_category = forge_pack_category(category);
	short faction;
	short seen = 0;

	line[0] = 0;
	*missing = FALSE;
	if (line_index < 0 || line_index >= forge_ai_faction_lines(category))
		return 0;
	for (faction = 0; faction < forge_ai_tags_faction_count(); faction++)
	{
		if (forge_ai_tags_faction_category(faction) == pack_category && seen++ == line_index)
			break;
	}
	if (faction >= forge_ai_tags_faction_count() || !forge_ai_tags_faction(faction, &name, &map))
		return 0;
	*missing = map == NULL;
	if (map)
		snprintf(line, (size_t)size, "%s: %s", name, forge_mission_title(map));
	else
		snprintf(line, (size_t)size, "%s", name);
	return 1;
}

/* (an import done) the campaign vehicles' seats a player could not take,
given animations the player's unit has: the Wraith's driver ("wraith-driver",
an Elite's) the Scorpion driver's. Only the copy this Forge game brought in
from a campaign map; a label already changed is not changed again */
static struct
{
	char const *path;
	char const *label;
	char const *player_label;
} const forge_campaign_seats[] =
{
	{ "vehicles\\wraith\\wraith", "wraith-driver", "scorpion-driver" },
};

static void forge_campaign_seats_tune(
	void)
{
	short entry;

	for (entry = 0; entry < (short)NUMBEROF(forge_campaign_seats); entry++)
	{
		long tag_index = tag_loaded(VEHICLE_DEFINITION_TAG, forge_campaign_seats[entry].path);
		struct unit_definition *definition;
		short seat_index;

		if (tag_index == NONE || !forge_ai_tags_source_map(tag_index))
			continue;
		definition = unit_definition_get(tag_index);
		for (seat_index = 0; seat_index < definition->unit.seats.count; seat_index++)
		{
			struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat);

			if (!_strnicmp(seat->label, forge_campaign_seats[entry].label, TAG_STRING_LENGTH))
			{
				csmemset(seat->label, 0, TAG_STRING_LENGTH + 1);
				csstrncpy(seat->label, forge_campaign_seats[entry].player_label, TAG_STRING_LENGTH);
				platform_log("forge: %s's seat %d a player's (%s)", forge_tag_leaf(tag_index), seat_index,
					forge_campaign_seats[entry].player_label);
			}
		}
	}
}

/* (the Forge menu's choice, forge_update's) an item of a category spawned in the
monitor's hand, or a marker of its kind held */
static void forge_category_choose(
	short category,
	short item_index)
{
	if (!forge_globals.active || item_index < 0 || item_index >= forge_category_item_count(category))
		return;
	if (forge_globals.held_marker_id)
		forge_marker_drop();
	if (object_try_and_get(forge_globals.held_object_index))
		forge_drop();
	if (category == _forge_category_effects || category == _forge_category_decals)
		forge_effect_choose(category, item_index);
	else if (forge_marker_category_first(category) != NONE)
		forge_spawn_choose((short)(forge_marker_category_first(category) + item_index));
	else
		forge_spawn(forge_globals.palette[forge_globals.palette_first[category] + item_index].tag_index);
}

short forge_budget_objects(
	void)
{
	return forge_objects_counted();
}

boolean forge_camera_input(
	short local_player_index,
	real seconds,
	struct forge_camera_input *input)
{
	struct forge_pad pad;
	real speed;
	boolean turning_object;

	if (!forge_globals.active || local_player_index != forge_globals.local_player_index)
		return FALSE;
	csmemset(input, 0, sizeof(*input));
	/* (the Forge menu open, or a menu over the game, has the buttons: the camera
	stays) */
	if (forge_menu_game_open() || forge_globals.hud_hidden)
		return TRUE;
	forge_pad_get(&pad, TRUE);
	/* (forge look: as the mouse's look, once) */
	pad.look_yaw += forge_globals.test_look_yaw;
	pad.look_pitch += forge_globals.test_look_pitch;
	forge_globals.test_look_yaw = 0.f;
	forge_globals.test_look_pitch = 0.f;
	turning_object = pad.down[_forge_button_alternate] && object_try_and_get(forge_globals.held_object_index);
	speed = forge_speeds[PIN(forge_globals.speed, 0, NUMBER_OF_FORGE_SPEEDS - 1)] * seconds;
	if ((pad.down[_forge_button_alternate] && !turning_object) || pad.fast)
		speed *= FORGE_CAMERA_THRUST;
	/* (rotate mode: the left stick pushes and pulls the object, the right
	one turns it - forge_rotate_update -; the camera stays) */
	if (!turning_object)
	{
		input->translation.i = pad.sticks[_gamepad_stick_left][1] * speed;
		input->translation.j = -pad.sticks[_gamepad_stick_left][0] * speed;
	}
	input->translation.k = ((pad.down[_forge_button_fly_up] ? 1.f : 0.f) - (pad.down[_forge_button_fly_down] ? 1.f : 0.f)) * speed;
	if (!turning_object)
	{
		input->facing_delta.yaw = -pad.sticks[_gamepad_stick_right][0] * FORGE_CAMERA_LOOK_RATE * seconds + pad.look_yaw;
		input->facing_delta.pitch = pad.sticks[_gamepad_stick_right][1] * FORGE_CAMERA_LOOK_RATE * seconds * 0.6f +
			pad.look_pitch;
	}
	else
	{
		/* (the mouse turns the object: its own update takes it) */
		forge_globals.pending_mouse_turn += pad.look_yaw;
		forge_globals.pending_mouse_pitch += pad.look_pitch;
	}

	return TRUE;
}

/* (each update of a local game) the layout's weapons and equipment. A
multiplayer game takes away every item nobody has held for 30 s
(game_engine.c game_engine_update_purge) and puts the map's own back from
its netgame equipment; the layout's are not the map's:
- one lying where the editor put it (its home) is never taken away, as the
  map's own lying at its spot is not;
- one gone - picked up and used up, thrown, or dropped elsewhere and taken
  away by the game - comes back at its home its RESPAWN time later (0: with
  the next round only), its properties and its id kept.
Never what anyone holds */
#define FORGE_TICKS_PER_SECOND 30
#define FORGE_ITEM_HOME_DISTANCE 0.75f
static boolean forge_placed_is_item(
	struct forge_placed const *placed)
{
	unsigned long group_tag;

	if (placed->tag_index == NONE)
		return FALSE;
	group_tag = tag_get_group_tag(placed->tag_index);
	return group_tag == WEAPON_DEFINITION_TAG || group_tag == EQUIPMENT_DEFINITION_TAG;
}

static void forge_item_life_update(
	void)
{
	long now;
	short index;

	if (!forge_globals.game_active || forge_globals.game_requested || forge_globals.round_pending ||
		forge_globals.round_resetting || !game_in_progress())
	{
		return;
	}
	now = game_time_get();
	forge_placed_update();
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];
		struct forge_placed kept;
		long object_index;
		short refusal;

		if (!forge_placed_is_item(placed))
			continue;
		if (placed->object_index != NONE)
		{
			struct object_datum *object = object_try_and_get(placed->object_index);

			placed->dead_tick = 0;
			if (object && (FLAG(object->object.type) & _object_mask_item) && object->object.parent_object_index == NONE &&
				placed->has_home)
			{
				struct item_datum *item = (struct item_datum *)object;
				real_vector3d from_home;

				from_home.i = object->object.position.x - placed->home.position.x;
				from_home.j = object->object.position.y - placed->home.position.y;
				from_home.k = object->object.position.z - placed->home.position.z;
				if (!TEST_FLAG(item->item.flags, _item_attached_to_unit_bit) &&
					dot_product3d(&from_home, &from_home) < FORGE_ITEM_HOME_DISTANCE * FORGE_ITEM_HOME_DISTANCE)
				{
					item->item.last_owned_time = now;
				}
			}
			continue;
		}
		if (!placed->dead_tick)
		{
			placed->dead_tick = now + 1;
			platform_log("forge: %s (#%ld) gone from its spot: back in %d s", forge_tag_leaf(placed->tag_index), placed->id,
				(int)placed->respawn_seconds);
		}
		if (!placed->has_home || !placed->respawn_seconds ||
			now + 1 - placed->dead_tick < (long)placed->respawn_seconds * FORGE_TICKS_PER_SECOND)
		{
			continue;
		}
		kept = *placed;
		forge_placed_remove(index);
		object_index = forge_place(kept.tag_index, &kept.home.position, &kept.home.forward, &kept.home.up, TRUE, &refusal);
		if (object_index == NONE || forge_globals.placed_count <= 0 ||
			forge_globals.placed[forge_globals.placed_count - 1].object_index != object_index)
		{
			/* (not placed: the entry kept, tried again its time later) */
			if (forge_globals.placed_count < FORGE_MAXIMUM_OBJECTS)
			{
				kept.object_index = NONE;
				kept.dead_tick = now + 1;
				forge_globals.placed[forge_globals.placed_count++] = kept;
				forge_placed_update();
			}
			platform_log("forge: %s (#%ld) could not respawn (%d)", forge_tag_leaf(kept.tag_index), kept.id, (int)refusal);
			index--;
			continue;
		}
		{
			struct forge_placed *entry = &forge_globals.placed[forge_globals.placed_count - 1];

			entry->id = kept.id;
			entry->team = kept.team;
			entry->flags = kept.flags;
			entry->respawn_seconds = kept.respawn_seconds;
			entry->home = kept.home;
			entry->has_home = TRUE;
			entry->dead_tick = 0;
		}
		platform_log("forge: %s (#%ld) respawned after %d s at %.2f %.2f %.2f", forge_tag_leaf(kept.tag_index), kept.id,
			(int)kept.respawn_seconds, kept.home.position.x, kept.home.position.y, kept.home.position.z);
		/* (the entry moved to the end: this index holds the next one) */
		index--;
	}
}

void forge_update(
	void)
{
	struct forge_pad pad;
	boolean pressed[NUMBER_OF_GAMEPAD_BUTTONS];
	boolean toggle_pressed;
	short button;
	real seconds = 1.f / 30.f;
	long player_index;

	if (!forge_globals.game_active && !forge_globals.game_requested)
		return;
	if (forge_globals.test_buttons_frames > 0)
		forge_globals.test_buttons_frames--;
	if (forge_globals.test_sticks_frames > 0)
		forge_globals.test_sticks_frames--;
	/* (a multiplayer map's AI characters, brought in from the player's
	campaign maps once its load is done: forge_ai_tags.c; the palette made
	again with them) */
	if (forge_globals.game_active && forge_ai_tags_update())
	{
		forge_globals.palette_built = FALSE;
		forge_campaign_seats_tune();
		forge_flyable_tune();
	}
	forge_session_update();
	/* (the layout's teleporters and flags the engine's, before the tick) */
	forge_netgame_update();
	/* (a layout played: the layout kept, nothing of Forge's asked for or
	shown - no monitor, menu, save or round of its own) */
	if (forge_globals.game_active && forge_globals.playing)
	{
		forge_globals.round_requested = FALSE;
		if (!forge_globals.game_requested && local_player_get_player_index(forge_globals.local_player_index) != NONE &&
			game_connection() == _game_connection_local)
		{
			forge_placed_update();
			forge_homes_update();
			forge_physics_update();
			/* (its weapons and equipment: kept at their spots, their RESPAWN) */
			forge_item_life_update();
			/* (its AI characters: their lives and AI, forge_ai.inc) */
			forge_ai_life_update();
			forge_ai_hold();
			forge_ai_move_update();
		}
		return;
	}
	if (!forge_globals.game_active)
		return;
	forge_item_life_update();
	forge_ai_life_update();
	/* (the characters' movement, the craft's flights: forge_ai_move.inc) */
	forge_ai_move_update();
	/* (NEW ROUND: once the Forge menu has closed and the game runs - the engine
	resets a map only then) */
	if (forge_globals.round_requested)
	{
		if (forge_globals.game_requested || forge_globals.round_pending || forge_globals.round_resetting)
		{
			forge_globals.round_requested = FALSE;
		}
		else if (!forge_menu_game_open() && !game_time_get_paused())
		{
			forge_globals.round_requested = FALSE;
			forge_new_round_start();
		}
	}
	player_index = local_player_get_player_index(forge_globals.local_player_index);
	if (player_index == NONE || game_connection() != _game_connection_local)
	{
		/* (no player, or not a local game: the monitor's state is gone; the
		player's own camera is the director's again) */
		if (forge_globals.active)
		{
			forge_globals.active = FALSE;
			forge_monitor_flying = 0;
			forge_globals.held_object_index = NONE;
			forge_globals.unit_index = NONE;
			forge_menu_game_close();
			if (player_index != NONE)
				director_forge_camera(forge_globals.local_player_index, FALSE, NULL, NULL);
			platform_log("forge: the monitor is gone (a new map, or no player)");
		}
		return;
	}

	/* (the buttons and sticks: the mouse's look is the camera's) */
	forge_pad_get(&pad, FALSE);
	for (button = 0; button < NUMBER_OF_GAMEPAD_BUTTONS; button++)
	{
		pressed[button] = pad.down[button] && !forge_globals.was_down[button];
		forge_globals.was_down[button] = pad.down[button];
	}
	toggle_pressed = pad.toggle && !forge_globals.toggle_was_down;
	forge_globals.toggle_was_down = pad.toggle;
	/* (the keyboard's wheel: what is held nearer or farther, a step a
	notch) */
	{
		boolean nearer_pressed = pad.nearer && !forge_globals.nearer_was_down;
		boolean farther_pressed = pad.farther && !forge_globals.farther_was_down;

		forge_globals.nearer_was_down = pad.nearer;
		forge_globals.farther_was_down = pad.farther;
		if (forge_globals.active && !forge_menu_game_open() &&
			(object_try_and_get(forge_globals.held_object_index) || forge_globals.held_marker_id) &&
			(nearer_pressed || farther_pressed))
		{
			forge_globals.held_distance = PIN(forge_globals.held_distance + (farther_pressed ? FORGE_WHEEL_STEP :
				-FORGE_WHEEL_STEP), FORGE_MINIMUM_HOLD_DISTANCE, FORGE_MAXIMUM_HOLD_DISTANCE);
		}
	}
	/* (a menu of the game's over it, pausing it: no HUD, no controls) */
	forge_globals.hud_hidden = game_time_get_paused();
	if (forge_globals.hud_hidden)
	{
		forge_delete_hold_reset();
		return;
	}
	forge_placed_update();
	forge_homes_update();
	forge_physics_update();
	forge_ai_hold();

	/* (the Spartan: the combo is the monitor) */
	if (!forge_globals.active)
	{
		if (toggle_pressed)
			forge_begin();
		return;
	}

	/* (the player respawned while the monitor flew: the new unit waits
	hidden as the last did) */
	if (player_get(player_index)->unit_index != forge_globals.unit_index)
	{
		forge_unit_show();
		forge_unit_hide(player_get(player_index)->unit_index);
	}
	if (toggle_pressed)
	{
		forge_end();
		return;
	}
	/* (the Forge menu's asks, done here before the tick; X opens it - on
	PROPERTIES holding something -, and while it is open its buttons are its
	own: forge_menu.c takes the controller and the keyboard) */
	forge_menu_asks_run();
	if (!forge_globals.active)
		return;
	/* (X with nothing held: the properties of the layout's object or marker
	the crosshair is on, the menu opened on them; none, the menu as ever) */
	if (!forge_menu_game_open())
		forge_properties_unpick();
	if (!forge_menu_game_open() && pressed[_forge_button_menu])
	{
		forge_properties_pick();
		forge_menu_game_open_now();
		if (!forge_menu_game_open())
			forge_properties_unpick();
	}
	if (forge_menu_game_open())
	{
		forge_globals.rotating = FALSE;
		forge_delete_hold_reset();
	}
	else
	{
		boolean delete_asked = forge_delete_asked(&pad);
		boolean rotating = pad.down[_forge_button_alternate] && object_try_and_get(forge_globals.held_object_index);

		/* (LT + the right stick's click: rotate mode and nudge mode
		flipped - forge_precision.inc) */
		if (pressed[_gamepad_binary_button_right_thumb] && rotating && !pad.toggle)
		{
			forge_globals.nudge_mode = !forge_globals.nudge_mode;
			forge_notice("%s", forge_globals.nudge_mode ? "LT: nudge" : "LT: rotate");
		}
		/* (White: in rotate mode its snapping, in nudge mode its step, else
		the movement speed) */
		if (pressed[_forge_button_speed] && rotating && forge_globals.nudge_mode)
		{
			forge_nudge_step_next();
		}
		else if (pressed[_forge_button_speed] && rotating)
		{
			char snap[16];

			forge_globals.rotate_snap = (short)((forge_globals.rotate_snap + 1) % (short)NUMBEROF(forge_rotate_snaps));
			forge_rotate_snap_text(snap, sizeof(snap));
			forge_notice("Rotation snap: %s", snap);
		}
		else if (pressed[_forge_button_speed])
		{
			forge_globals.speed = (short)((forge_globals.speed + 1) % NUMBER_OF_FORGE_SPEEDS);
			forge_notice("%s", forge_speed_names[forge_globals.speed]);
		}
		if (pressed[_forge_button_straighten] && !forge_globals.held_marker_id)
			forge_straighten(forge_target_object());
		if (pressed[_forge_button_duplicate] && !forge_globals.held_marker_id)
			forge_duplicate();
		/* (RT in rotate mode: roll, never a drop) */
		if (pressed[_forge_button_grab] && !rotating)
		{
			if (forge_globals.held_marker_id)
			{
				forge_marker_drop();
			}
			else if (object_try_and_get(forge_globals.held_object_index))
			{
				forge_drop();
			}
			else
			{
				/* (the nearer of an object and a marker) */
				real object_distance = forge_grab_range();
				real marker_distance = forge_grab_range();
				long object_index = forge_object_under_crosshair_at(&object_distance);
				long marker = forge_marker_under_crosshair(&marker_distance);

				if (marker != NONE && marker_distance <= object_distance + FORGE_MARKER_PICK_RADIUS)
					forge_marker_grab(marker, marker_distance);
				else if (object_index != NONE)
					forge_grab(object_index);
			}
		}
		/* (Y: a marker held, or under the crosshair before what is behind
		it, deleted; else the object held or aimed at) */
		if (delete_asked)
		{
			if (forge_globals.held_marker_id)
			{
				forge_spawn_delete(forge_globals.held_marker_id);
			}
			else
			{
				real object_distance = forge_grab_range();
				real marker_distance = forge_grab_range();
				long marker = NONE;

				if (!object_try_and_get(forge_globals.held_object_index))
				{
					forge_object_under_crosshair_at(&object_distance);
					marker = forge_marker_under_crosshair(&marker_distance);
				}
				if (marker != NONE && marker_distance <= object_distance + FORGE_MARKER_PICK_RADIUS)
					forge_spawn_delete(marker);
				else
					forge_delete(forge_target_object());
			}
		}
		rotating = rotating && object_try_and_get(forge_globals.held_object_index);
		if (rotating && forge_globals.nudge_mode)
		{
			forge_nudge_update(&pad);
		}
		else if (rotating)
		{
			forge_rotate_update(&pad, seconds);
		}
		else
		{
			short axis;

			for (axis = 0; axis < NUMBER_OF_FORGE_AXES; axis++)
			{
				forge_globals.rotate_flicked[axis] = FALSE;
				forge_globals.rotate_mouse[axis] = 0.f;
			}
			for (axis = 0; axis < 3; axis++)
				forge_globals.nudge_flicked[axis] = FALSE;
		}
		forge_globals.rotating = rotating;
	}
	forge_globals.pending_mouse_turn = 0.f;
	forge_globals.pending_mouse_pitch = 0.f;
	forge_held_follow_view();
	if (object_try_and_get(forge_globals.held_object_index))
		forge_held_object_place();
	if (forge_globals.held_marker_id)
		forge_marker_held_update();
	forge_hud_update();

	return;
}

/* a line of the monitor's HUD in a font, at bounds (justification: 0 left,
2 centered), in a color */
static void forge_draw_line(
	long font_index,
	rectangle2d const *bounds,
	short justification,
	real red,
	real green,
	real blue,
	char const *line)
{
	real_argb_color color;
	wchar_t text[FORGE_LINE_SIZE];
	short length;

	color.alpha = 1.f;
	color.red = red;
	color.green = green;
	color.blue = blue;
	draw_string_set_draw_mode(font_index, NONE, justification, 0, &color);
	/* (the HUD's fonts are Latin-1's: a two-byte character of it - the
	degree sign, the middle dot - one character, anything else of more than
	a byte a question mark) */
	for (length = 0; *line && length < FORGE_LINE_SIZE - 1; line++)
	{
		unsigned char character = (unsigned char)*line;

		if ((character == 0xC2 || character == 0xC3) && ((unsigned char)line[1] & 0xC0) == 0x80)
		{
			text[length++] = (wchar_t)(((character & 0x03) << 6) | ((unsigned char)line[1] & 0x3F));
			line++;
		}
		else if (character < 0x80)
		{
			text[length++] = (wchar_t)character;
		}
		else if ((character & 0xC0) != 0x80)
		{
			text[length++] = '?';
		}
	}
	text[length] = 0;
	rasterizer_draw_unicode_string(bounds, NULL, NULL, 0, text);
}

static short forge_font_height(
	long font_index)
{
	struct font_header *font = font_definition_get(font_index);

	return (short)(font->ascending_height + font->descending_height);
}

/* the monitor's prompts along the bottom of the view (forge_menu.c draws
them in the prompts' buttons, or the keyboard's keys): what its buttons do
now; the Spartan's, how to be the monitor again */
static void forge_draw_prompts(
	void)
{
	static char speed[32];
	static char snap[32];
	struct forge_hud_prompt prompts[6];
	short count = 0;
	boolean holding = object_try_and_get(forge_globals.held_object_index) != NULL || forge_globals.held_marker_id != 0;

	/* (a menu over the game, the pause menu's: its prompts alone) */
	if (ui_widgets_take_input())
		return;
#define FORGE_PROMPT(button_, key_, word_) \
	do { prompts[count].button = (button_); prompts[count].key = (key_); prompts[count++].word = (word_); } while (0)
	if (!forge_globals.active)
	{
		FORGE_PROMPT(UI_BUTTON_BACK, HALO_FORGE_KEY_MONITOR, "+ RS: MONITOR");
	}
	else if (forge_globals.rotating)
	{
		char text[16];

		forge_rotate_snap_text(text, sizeof(text));
		snprintf(snap, sizeof(snap), "SNAP: %s", text);
		FORGE_PROMPT(UI_BUTTON_LEFT_SHOULDER, HALO_FORGE_KEY_SPEED, forge_globals.nudge_mode ? "NUDGE STEP" : snap);
		FORGE_PROMPT(NONE, HALO_FORGE_KEY_NUDGE, forge_globals.nudge_mode ? "RS: ROTATE" : "RS: NUDGE");
		FORGE_PROMPT(UI_BUTTON_Y, HALO_FORGE_KEY_DELETE, forge_delete_word());
	}
	else if (holding)
	{
		FORGE_PROMPT(UI_BUTTON_RIGHT_TRIGGER, HALO_FORGE_KEY_GRAB, "LET GO");
		FORGE_PROMPT(UI_BUTTON_LEFT_TRIGGER, HALO_FORGE_KEY_ROTATE, "ROTATE");
		FORGE_PROMPT(UI_BUTTON_B, HALO_FORGE_KEY_COPY, "COPY");
		FORGE_PROMPT(UI_BUTTON_Y, HALO_FORGE_KEY_DELETE, forge_delete_word());
		FORGE_PROMPT(UI_BUTTON_X, HALO_FORGE_KEY_MENU, "PROPERTIES");
	}
	else
	{
		snprintf(speed, sizeof(speed), "SPEED: %s", forge_speed_name(forge_globals.speed));
		FORGE_PROMPT(UI_BUTTON_RIGHT_TRIGGER, HALO_FORGE_KEY_GRAB, "GRAB");
		if (forge_globals.hud_label[0])
		{
			FORGE_PROMPT(UI_BUTTON_B, HALO_FORGE_KEY_COPY, "COPY");
			FORGE_PROMPT(UI_BUTTON_Y, HALO_FORGE_KEY_DELETE, forge_delete_word());
		}
		FORGE_PROMPT(UI_BUTTON_X, HALO_FORGE_KEY_MENU, forge_globals.hud_properties ? "PROPERTIES" : "FORGE MENU");
		FORGE_PROMPT(UI_BUTTON_LEFT_SHOULDER, HALO_FORGE_KEY_SPEED, speed);
		FORGE_PROMPT(UI_BUTTON_BACK, HALO_FORGE_KEY_MONITOR, "+ RS: SPARTAN");
	}
#undef FORGE_PROMPT
	forge_menu_draw_prompts(prompts, count);
}

void forge_draw(
	short local_player_index)
{
	long font_index;
	rectangle2d bounds;
	rectangle2d window;
	short height;

	if (local_player_index != forge_globals.local_player_index || forge_globals.hud_hidden)
		return;
	/* (the Forge menu open: its panel alone, none of the monitor's lines) */
	if (forge_menu_game_open())
		return;
	/* (the Spartan of a Forge game: how to be the monitor again) */
	if (!forge_globals.active)
	{
		if (forge_globals.game_active && !forge_globals.playing && !forge_globals.game_requested)
			forge_draw_prompts();
		return;
	}
	font_index = hud_get_font_index();
	if (font_index == NONE)
		return;
	height = forge_font_height(font_index);
	window = render.camera.window_bounds;

	/* the map and the budget, top left */
	bounds = window;
	bounds.x0 = (short)(window.x0 + 24);
	bounds.y0 = (short)(window.y0 + 20);
	bounds.y1 = (short)(bounds.y0 + height);
	forge_draw_line(font_index, &bounds, 0, 0.93f, 0.95f, 0.97f, forge_globals.hud_title);
	bounds.y0 = bounds.y1;
	bounds.y1 = (short)(bounds.y0 + height);
	forge_draw_line(font_index, &bounds, 0, 0.62f, 0.71f, 0.80f, forge_globals.hud_budget);
	/* the notice, a while, at the left below the middle */
	if (forge_globals.notice[0] && system_milliseconds() < forge_globals.notice_until)
	{
		bounds.y0 = (short)(window.y0 + (window.y1 - window.y0) * 62 / 100);
		bounds.y1 = (short)(bounds.y0 + height);
		forge_draw_line(font_index, &bounds, 0, 0.62f, 0.82f, 1.f, forge_globals.notice);
	}
	/* (the Forge menu over the view: its own) */
	if (forge_menu_game_open())
		return;
	forge_draw_prompts();
	/* (game.forge_hold_to_delete) the delete held: its fill over the crosshair */
	if (forge_globals.delete_hold_fraction > 0.f &&
		(forge_globals.hud_label[0] || forge_globals.held_marker_id || object_try_and_get(forge_globals.held_object_index)))
	{
		forge_menu_draw_hold(forge_globals.delete_hold_fraction);
	}

	/* the crosshair, and under it what it is on (ice blue) or what is
	held (orange) */
	bounds = window;
	bounds.y0 = (short)((window.y0 + window.y1) / 2 - height / 2);
	bounds.y1 = (short)(bounds.y0 + height);
	if (forge_globals.hud_label_held)
		forge_draw_line(font_index, &bounds, 2, 0.95f, 0.62f, 0.30f, "[ + ]");
	else if (forge_globals.hud_label[0])
		forge_draw_line(font_index, &bounds, 2, 0.47f, 0.84f, 1.f, "[ + ]");
	else
		forge_draw_line(font_index, &bounds, 2, 0.85f, 0.9f, 0.95f, "( + )");
	if (forge_globals.hud_label[0])
	{
		bounds.y0 = (short)(bounds.y1 + 2);
		bounds.y1 = (short)(bounds.y0 + height);
		if (forge_globals.hud_label_held)
			forge_draw_line(font_index, &bounds, 2, 0.95f, 0.62f, 0.30f, forge_globals.hud_label);
		else
			forge_draw_line(font_index, &bounds, 2, 0.47f, 0.84f, 1.f, forge_globals.hud_label);
	}
	/* (holding one: its angles while rotating, the snapping) */
	if (forge_globals.hud_rotate[0])
	{
		bounds.y0 = (short)(bounds.y1 + 2);
		bounds.y1 = (short)(bounds.y0 + height);
		forge_draw_line(font_index, &bounds, 2, forge_globals.rotating ? 0.95f : 0.62f,
			forge_globals.rotating ? 0.78f : 0.71f, forge_globals.rotating ? 0.55f : 0.80f, forge_globals.hud_rotate);
	}
}

static boolean forge_word(
	char const **text,
	char *word,
	size_t size)
{
	size_t length = 0;
	char const *character = *text;

	while (*character == ' ' || *character == '\t' || *character == '"')
		character++;
	while (*character && *character != ' ' && *character != '\t' && *character != ')' && *character != '"')
	{
		if (length + 1 < size)
			word[length++] = *character;
		character++;
	}
	if (*character == '"')
		character++;
	word[length] = 0;
	*text = character;
	return length > 0;
}

/* the rest of the text, spaces, quotes and a closing parenthesis trimmed */
static void forge_rest(
	char const *text,
	char *rest,
	size_t size)
{
	size_t length;

	while (*text == ' ' || *text == '\t' || *text == '"')
		text++;
	csstrncpy(rest, text, size - 1);
	rest[size - 1] = 0;
	length = strlen(rest);
	while (length > 0 && (rest[length - 1] == ' ' || rest[length - 1] == ')' || rest[length - 1] == '"' ||
		rest[length - 1] == '\r' || rest[length - 1] == '\n'))
	{
		rest[--length] = 0;
	}

	return;
}

static short forge_button_from_name(
	char const *name)
{
	static struct
	{
		char const *name;
		short button;
	} const names[] =
	{
		{ "a", _gamepad_analog_button_a }, { "b", _gamepad_analog_button_b },
		{ "x", _gamepad_analog_button_x }, { "y", _gamepad_analog_button_y },
		{ "black", _gamepad_analog_button_black }, { "white", _gamepad_analog_button_white },
		{ "lt", _gamepad_analog_button_left_trigger }, { "rt", _gamepad_analog_button_right_trigger },
		{ "ls", _gamepad_binary_button_left_thumb }, { "rs", _gamepad_binary_button_right_thumb },
		{ "up", _gamepad_binary_button_dpad_up }, { "down", _gamepad_binary_button_dpad_down },
		{ "left", _gamepad_binary_button_dpad_left }, { "right", _gamepad_binary_button_dpad_right },
		{ "back", _gamepad_binary_button_back },
	};
	short index;

	for (index = 0; index < (short)NUMBEROF(names); index++)
	{
		if (!strcmp(names[index].name, name))
			return names[index].button;
	}
	return NONE;
}

/* the layout's object (or marker) of an id ("#12"), the held one, or the
one under the crosshair */
static long forge_command_target(
	char const *rest)
{
	if (rest[0] == '#')
	{
		long id = atol(rest + 1);
		short index;

		for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
		{
			if (forge_globals.placed[index].id == id && forge_object_alive(forge_globals.placed[index].object_index))
				return forge_globals.placed[index].object_index;
		}
		return NONE;
	}
	return forge_target_object();
}

boolean forge_command(
	char const *arguments)
{
	char word[32];
	char rest[160];
	char const *text = arguments;

	/* (off: game.forge) */
	if (!forge_enabled())
	{
		console_warning("Forge is off: set game.forge = true in config.toml (or HALO_FORGE=1) to use it");
		platform_log("forge: off (game.forge): \"forge%s\" refused", arguments ? arguments : "");
		return FALSE;
	}
	if (!forge_word(&text, word, sizeof(word)))
	{
		if (forge_globals.active)
			forge_end();
		else
			forge_begin();
		return TRUE;
	}
	forge_rest(text, rest, sizeof(rest));

	if (!strcmp(word, "on"))
		return forge_begin();
	if (!strcmp(word, "off"))
	{
		forge_end();
		return TRUE;
	}
	/* (a game: "start <map> [<layout>]", "play <map> <layout> [<type>]") */
	if (!strcmp(word, "start") || !strcmp(word, "play"))
	{
		char map_word[64];
		char file[FORGE_FILE_NAME_SIZE];
		char type[32];
		char why[FORGE_STORE_WHY_SIZE];
		char const *cursor = rest;
		short map_index;
		boolean started;

		map_word[0] = file[0] = type[0] = why[0] = 0;
		forge_word(&cursor, map_word, sizeof(map_word));
		forge_word(&cursor, file, sizeof(file));
		forge_word(&cursor, type, sizeof(type));
		map_index = forge_map_of_word(map_word);
		if (map_index == NONE)
		{
			console_warning("forge %s: no multiplayer map \"%s\" (bloodgulch, hangemhigh...)", word, map_word);
			return FALSE;
		}
		if (file[0] && !strstr(file, FORGE_FILE_EXTENSION) && strlen(file) + 5 < sizeof(file))
			strcat(file, FORGE_FILE_EXTENSION);
		if (!strcmp(word, "play") && !file[0])
		{
			console_warning("forge play <map> <layout> [slayer|teamslayer|ctf|king|oddball|race]");
			return FALSE;
		}
		started = !strcmp(word, "start") ? forge_game_start(map_index, file, why, sizeof(why)) :
			forge_play_start(map_index, file, type, why, sizeof(why));
		if (!started)
		{
			console_warning("forge %s: %s", word, why[0] ? why : "not started");
			platform_log("forge: %s refused: %s", word, why);
		}
		return started;
	}
	if (!strcmp(word, "layouts"))
	{
		static struct forge_store_entry entries[FORGE_STORE_MAXIMUM_LISTED];
		int count = forge_store_list(rest[0] ? rest : NULL, entries, FORGE_STORE_MAXIMUM_LISTED);
		int index;

		for (index = 0; index < count; index++)
		{
			platform_log("forge: layout %s/%s \"%s\"%s, %d records, %s", entries[index].map_name, entries[index].file_name,
				entries[index].title, entries[index].damaged ? " (refused: damaged)" : "", entries[index].objects,
				entries[index].date);
			console_printf(FALSE, "%s/%s \"%s\"%s", entries[index].map_name, entries[index].file_name, entries[index].title,
				entries[index].damaged ? " (damaged)" : "");
		}
		platform_log("forge: %d layouts listed", count);
		return TRUE;
	}
	if (!strcmp(word, "list"))
	{
		short index;
		short category;

		forge_palette_build();
		for (category = 0; category < NUMBER_OF_FORGE_CATEGORIES; category++)
		{
			platform_log("forge: palette %s: %d", forge_category_log_names[category],
				forge_marker_category_first(category) != NONE ? forge_marker_category_count(category) :
				forge_globals.palette_counts[category]);
			if (strcmp(rest, forge_category_log_names[category]) || forge_marker_category_first(category) != NONE)
				continue;
			for (index = 0; index < forge_globals.palette_counts[category]; index++)
			{
				struct forge_palette_entry const *entry =
					&forge_globals.palette[forge_globals.palette_first[category] + index];

				platform_log("forge: %s %d: %s (%s)", rest, index, entry->name, tag_get_name(entry->tag_index));
			}
		}
		return TRUE;
	}
	if (!strcmp(word, "budget"))
	{
		forge_placed_update();
		platform_log("forge: budget %d/%d (%d objects, %d markers); HUD: %s", forge_objects_counted(), forge_objects_budget(),
			forge_globals.placed_count, forge_globals.spawn_count, forge_globals.hud_budget);
		return TRUE;
	}
	if (!strcmp(word, "press") || !strcmp(word, "stick"))
	{
		/* (the tests' controller: with or without the monitor, so that a
		press can start it) */
		if (!strcmp(word, "press"))
		{
			char names[64];
			char *name;
			int frames = 2;

			names[0] = 0;
			sscanf(rest, "%63s %d", names, &frames);
			csmemset(forge_globals.test_down, 0, sizeof(forge_globals.test_down));
			forge_globals.test_toggle = FALSE;
			for (name = strtok(names, "+"); name; name = strtok(NULL, "+"))
			{
				short button = forge_button_from_name(name);

				if (button != NONE)
					forge_globals.test_down[button] = TRUE;
				/* ("combo": the monitor and the Spartan's) */
				if (!strcmp(name, "combo"))
				{
					forge_globals.test_down[_gamepad_binary_button_back] = TRUE;
					forge_globals.test_down[_gamepad_binary_button_right_thumb] = TRUE;
				}
			}
			forge_globals.test_buttons_frames = (short)PIN(frames, 1, 3000);
		}
		else
		{
			float lx = 0.f, ly = 0.f, rx = 0.f, ry = 0.f;
			int frames = 30;

			if (sscanf(rest, "%f %f %f %f %d", &lx, &ly, &rx, &ry, &frames) < 4)
				return FALSE;
			forge_globals.test_sticks[_gamepad_stick_left][0] = lx;
			forge_globals.test_sticks[_gamepad_stick_left][1] = ly;
			forge_globals.test_sticks[_gamepad_stick_right][0] = rx;
			forge_globals.test_sticks[_gamepad_stick_right][1] = ry;
			forge_globals.test_sticks_frames = (short)PIN(frames, 1, 3000);
		}
		return TRUE;
	}
	if (!strcmp(word, "look"))
	{
		/* ("look <yaw> <pitch>": the mouse's look, radians - positive yaw
		left, positive pitch up -, once) */
		float yaw = 0.f, pitch = 0.f;

		if (sscanf(rest, "%f %f", &yaw, &pitch) < 2)
			return FALSE;
		forge_globals.test_look_yaw += yaw;
		forge_globals.test_look_pitch += pitch;
		return TRUE;
	}
	if (!strcmp(word, "objects"))
	{
		short index;

		forge_placed_update();
		for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
		{
			struct forge_placed const *placed = &forge_globals.placed[index];
			struct object_datum *object = forge_object_alive(placed->object_index) ? object_get(placed->object_index) : NULL;
			short entry = forge_palette_of_tag(placed->tag_index);
			char const *name = entry != NONE ? forge_globals.palette[entry].name : forge_tag_leaf(placed->tag_index);
			char const *state = !object ? "gone" : TEST_FLAG(object->object.damage_flags, _object_dead_bit) ? "destroyed" :
				"there";

			platform_log("forge: object %d: %s (id %ld) %s at %.2f %.2f %.2f, placed at %.2f %.2f %.2f facing %.3f %.3f %.3f, "
				"team %d, respawn %d s, physics %s", index, name, placed->id, state, object ? object->object.position.x : 0.f,
				object ? object->object.position.y : 0.f, object ? object->object.position.z : 0.f, placed->home.position.x,
				placed->home.position.y, placed->home.position.z, placed->home.forward.i, placed->home.forward.j,
				placed->home.forward.k, placed->team, placed->respawn_seconds,
				forge_physics_log_names[forge_physics_mode(placed)]);
		}
		platform_log("forge: objects listed: %d", forge_globals.placed_count);
		return TRUE;
	}
	/* (the map's own objects: forge_map_objects.inc) */
	if (!strcmp(word, "map"))
	{
		forge_map_objects_log(rest);
		return TRUE;
	}
	if (!strcmp(word, "spawns"))
	{
		short index;

		platform_log("forge: %d markers: %d player spawn points (%d initial), %d teleporters, %d game type objects; %d markers "
			"drawn (%d of the map's), budget %d/%d", forge_spawn_points(),
			forge_spawns_of_kind(_forge_spawn_initial) + forge_spawns_of_kind(_forge_spawn_respawn), forge_spawns_of_kind(_forge_spawn_initial),
			forge_spawns_of_kind(_forge_spawn_teleporter_sender) +
			forge_spawns_of_kind(_forge_spawn_teleporter_receiver), forge_objectives_count(NONE, NONE),
			forge_globals.markers_drawn, forge_globals.map_markers_drawn, forge_objects_counted(), forge_objects_budget());
		for (index = 0; index < forge_globals.spawn_count && index < FORGE_SPAWN_TABLE; index++)
			forge_spawn_log("spawns:", &forge_globals.spawns[index]);
		return TRUE;
	}
#ifdef HALO_CAPTURE
	/* (tests) a screenshot, as the screenshot key takes one (capture.c) */
	if (!strcmp(word, "shot"))
	{
		void capture_request_screenshot(void);

		capture_request_screenshot();
		platform_log("forge: a screenshot asked for");
		return TRUE;
	}
#endif
	/* (the game type objects' and teleporters' netgame flags in the log) */
	if (!strcmp(word, "netflags"))
	{
		forge_netgame_log();
		return TRUE;
	}
	/* (the tests: the Spartan into the layout's vehicle "#<id>", its first
	seat the Spartan can take - as walking up to it and pressing action) */
	if (!strcmp(word, "board"))
	{
		long vehicle_index = rest[0] == '#' ? forge_command_target(rest) : NONE;
		long player_index = local_player_get_player_index(forge_globals.local_player_index);
		long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
		struct object_datum *vehicle = object_try_and_get(vehicle_index);
		short seat_index;

		if (!vehicle || unit_index == NONE || forge_globals.active || vehicle->object.type != _object_type_vehicle)
			return FALSE;
		/* ("#<id> walk <seat>": the Spartan put at the seat's way in, on
		foot, for the action button to board it as a player would) */
		{
			char const *walk = strstr(rest, " walk ");
			real_point3d entry;

			if (walk && sscanf(walk + 6, "%hd", &seat_index) == 1 &&
				seat_index >= 0 && seat_index < unit_definition_get(vehicle->definition_index)->unit.seats.count &&
				unit_get_seat_entrance_point(unit_index, vehicle_index, seat_index, &entry, NULL, NULL))
			{
				object_set_position(unit_index, &entry, NULL, NULL);
				platform_log("forge: the Spartan at seat %d's way in of %s, %.2f %.2f %.2f (the tests)", seat_index,
					forge_tag_leaf(vehicle->definition_index), entry.x, entry.y, entry.z);
				return TRUE;
			}
			if (walk)
				return FALSE;
		}
		for (seat_index = 0; seat_index < unit_definition_get(vehicle->definition_index)->unit.seats.count; seat_index++)
		{
			long occupant = NONE;

			if (unit_can_enter_seat(unit_index, vehicle_index, seat_index, &occupant) && occupant == NONE &&
				unit_enter_seat(unit_index, vehicle_index, seat_index))
			{
				platform_log("forge: the Spartan boards %s, seat %d (the tests)", forge_tag_leaf(vehicle->definition_index),
					seat_index);
				return TRUE;
			}
		}
		platform_log("forge: the Spartan can take no seat of %s", forge_tag_leaf(vehicle->definition_index));
		return TRUE;
	}
	/* (the tests: a layout's AI character, "#<id>", killed - its respawn
	and its body's clearing then run as after any death) */
	if (!strcmp(word, "kill"))
	{
		long object_index = rest[0] == '#' ? forge_command_target(rest) : NONE;
		struct forge_placed *placed = forge_placed_of_object(object_index);

		if (!placed || !placed->ai)
			return FALSE;
		unit_kill(object_index);
		platform_log("forge: #%ld killed (the tests)", placed->id);
		return TRUE;
	}
	/* (the layout's AI characters in the log: in a layout played too) */
	if (!strcmp(word, "ai"))
	{
		/* ("forge ai move": their movement, forge_ai_move.inc) */
		if (!strcmp(rest, "move"))
			forge_ai_move_log();
		else
			forge_ai_log();
		return TRUE;
	}
	if (!strcmp(word, "where"))
	{
		real_point3d position;
		real_vector3d forward;
		struct object_datum *held = object_try_and_get(forge_globals.held_object_index);
		long aimed = forge_globals.active ? forge_object_under_crosshair() : NONE;

		if (!forge_camera_get(&position, &forward))
		{
			platform_log("forge: no camera");
			return FALSE;
		}
		{
			long player_index = local_player_get_player_index(forge_globals.local_player_index);
			long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
			struct unit_datum *unit = unit_index != NONE ? unit_get(unit_index) : NULL;

			if (unit && unit->object.parent_object_index != NONE)
			{
				platform_log("forge: the Spartan rides %s, seat %d",
					forge_tag_leaf(object_get(unit->object.parent_object_index)->definition_index), unit->unit.parent_seat_index);
			}
		}
		platform_log("forge: %s; camera at %.2f %.2f %.2f looking %.2f %.2f %.2f; holding %s at %.2f; aiming at %s; "
			"%ld spawned, %ld deleted; budget %d/%d; snap %s; layout \"%s\"",
			forge_globals.active ? "monitor" : forge_globals.game_active ? "Spartan" : "no Forge game",
			position.x, position.y, position.z, forward.i, forward.j, forward.k,
			held ? forge_tag_leaf(held->definition_index) : forge_globals.held_marker_id ? "a marker" : "nothing",
			forge_globals.held_distance, aimed != NONE ? forge_tag_leaf(object_get(aimed)->definition_index) : "nothing",
			forge_globals.spawned_count, forge_globals.deleted_count, forge_objects_counted(), forge_objects_budget(),
			forge_globals.snap ? "on" : "off", forge_globals.variant_title);
		return TRUE;
	}
	if (!forge_globals.active)
	{
		platform_log("forge: %s: not the monitor (a Forge game: forge start <map>, then forge on)", word);
		return FALSE;
	}
	if (!strcmp(word, "menu"))
	{
		/* (the Forge menu opened or closed, as X and B do) */
		if (!strcmp(rest, "open") && !forge_menu_game_open())
		{
			forge_properties_pick();
			forge_menu_game_open_now();
			if (!forge_menu_game_open())
				forge_properties_unpick();
		}
		else if (!strcmp(rest, "close"))
			forge_menu_game_close();
		else
			return FALSE;
		return TRUE;
	}
	if (!strcmp(word, "save"))
	{
		if (!rest[0])
		{
			console_warning("forge save <title>");
			return FALSE;
		}
		return forge_variant_save(rest);
	}
	if (!strcmp(word, "round"))
	{
		forge_globals.round_requested = TRUE;
		return TRUE;
	}
	if (!strcmp(word, "undo") || !strcmp(word, "redo"))
	{
		forge_undo_step(!strcmp(word, "redo"));
		return TRUE;
	}
	if (!strcmp(word, "snap"))
	{
		forge_globals.snap = !strcmp(rest, "on") ? TRUE : !strcmp(rest, "off") ? FALSE : !forge_globals.snap;
		forge_menu_notice("%s", forge_globals.snap ? "Snap: On" : "Snap: Off");
		return TRUE;
	}
	if (!strcmp(word, "clearai"))
	{
		if (strcmp(rest, "dead") && strcmp(rest, "all"))
			return FALSE;
		forge_ai_clear(!strcmp(rest, "all"));
		return TRUE;
	}
	if (!strcmp(word, "deleteall"))
	{
		short category;

		for (category = 0; category < NUMBER_OF_FORGE_CATEGORIES && strcmp(rest, forge_category_log_names[category]); category++)
			;
		if (category >= NUMBER_OF_FORGE_CATEGORIES)
			return FALSE;
		forge_group_delete(category);
		return TRUE;
	}
	if (!strcmp(word, "spawn"))
	{
		short entry;

		forge_palette_ready();
		entry = forge_palette_find(rest);
		if (entry == NONE)
		{
			platform_log("forge: no object named %s on this map", rest);
			return FALSE;
		}
		if (forge_globals.held_marker_id)
			forge_marker_drop();
		if (object_try_and_get(forge_globals.held_object_index))
			forge_drop();
		return forge_spawn(forge_globals.palette[entry].tag_index) != NONE;
	}
	if (!strcmp(word, "effect") || !strcmp(word, "decal"))
	{
		/* (one of the map's effects or decals by name, placed and held) */
		if (forge_globals.held_marker_id)
			forge_marker_drop();
		if (object_try_and_get(forge_globals.held_object_index))
			forge_drop();
		return forge_effect_choose_named(!strcmp(word, "decal") ? _forge_category_decals : _forge_category_effects, rest);
	}
	if (!strcmp(word, "marker"))
	{
		short kind;

		for (kind = 0; kind < NUMBER_OF_FORGE_MARKER_KINDS && strcmp(rest, forge_spawn_kind_words[kind]); kind++)
			;
		/* (an effect or a decal takes a tag: forge effect|decal <name>) */
		if (kind >= NUMBER_OF_FORGE_MARKER_KINDS || kind == _forge_spawn_effect || kind == _forge_spawn_decal)
			return FALSE;
		if (forge_globals.held_marker_id)
			forge_marker_drop();
		if (object_try_and_get(forge_globals.held_object_index))
			forge_drop();
		forge_spawn_choose(kind);
		return forge_globals.held_marker_id != 0;
	}
	if (!strcmp(word, "grab"))
	{
		long object_index;

		/* ("#<id>": the layout's marker of that id, or its object) */
		if (rest[0] == '#' && forge_spawn_of_id(atol(rest + 1)))
		{
			struct forge_spawn *spawn = forge_spawn_of_id(atol(rest + 1));
			real_point3d position;
			real_vector3d forward;
			real distance = 5.f;

			if (forge_camera_get(&position, &forward))
			{
				real_vector3d to_home;

				to_home.i = spawn->home.position.x - position.x;
				to_home.j = spawn->home.position.y - position.y;
				to_home.k = spawn->home.position.z - position.z;
				distance = magnitude3d(&to_home) - FORGE_MARKER_PICK_RADIUS;
			}
			if (object_try_and_get(forge_globals.held_object_index))
				forge_drop();
			forge_marker_grab(spawn->id, distance);
			return TRUE;
		}
		object_index = rest[0] == '#' ? forge_command_target(rest) : forge_object_under_crosshair();
		if (object_index == NONE)
		{
			platform_log("forge: nothing to grab");
			return FALSE;
		}
		if (object_try_and_get(forge_globals.held_object_index))
			forge_drop();
		forge_grab(object_index);
		return TRUE;
	}
	if (!strcmp(word, "drop"))
	{
		if (forge_globals.held_marker_id)
			forge_marker_drop();
		else if (object_try_and_get(forge_globals.held_object_index))
			forge_drop();
		else
			return FALSE;
		return TRUE;
	}
	if (!strcmp(word, "delete"))
	{
		long object_index;

		if (forge_globals.held_marker_id)
		{
			forge_spawn_delete(forge_globals.held_marker_id);
			return TRUE;
		}
		if (rest[0] == '#' && forge_spawn_of_id(atol(rest + 1)))
		{
			forge_spawn_delete(atol(rest + 1));
			return TRUE;
		}
		object_index = forge_command_target(rest);
		if (object_index == NONE)
			return FALSE;
		forge_delete(object_index);
		return TRUE;
	}
	if (!strcmp(word, "turn"))
	{
		long object_index;

		/* (a marker held: its facing, as its YAW row turns it) */
		if (forge_globals.held_marker_id)
		{
			real angle = DEGREES_TO_RADIANS((real)atof(rest));
			real_vector3d axis = *global_up3d;

			rotate_vector_about_axis(&forge_globals.held_forward, &axis, sine(angle), cosine(angle));
			platform_log("forge: turned the marker held by %s degrees", rest);
			return TRUE;
		}
		object_index = forge_target_object();
		if (object_index == NONE)
			return FALSE;
		forge_turn(object_index, DEGREES_TO_RADIANS((real)atof(rest)));
		platform_log("forge: turned %s by %s degrees", forge_tag_leaf(object_get(object_index)->definition_index), rest);
		return TRUE;
	}
	if (!strcmp(word, "property"))
	{
		static char const *const properties[NUMBER_OF_FORGE_PROPERTIES] =
		{
			"team", "respawn", "start", "yaw", "pitch", "roll", "resetrot", "physics", "aiteam", "ai", "behavior", "route",
			"vehicles",
		};
		char name[32];
		char const *cursor = rest;
		char back[8];
		short property;

		name[0] = back[0] = 0;
		forge_word(&cursor, name, sizeof(name));
		forge_word(&cursor, back, sizeof(back));
		for (property = 0; property < NUMBER_OF_FORGE_PROPERTIES && strcmp(name, properties[property]); property++)
			;
		if (property >= NUMBER_OF_FORGE_PROPERTIES || (!forge_properties_target() && !forge_properties_spawn()))
			return FALSE;
		forge_property_apply(property, (short)(!strcmp(back, "-") ? -1 : 1));
		return TRUE;
	}
	if (!strcmp(word, "environment"))
	{
		/* (the sky and fog's option stepped, as SKY AND FOG's row: sky, fog,
		color, density, start, end; back with -) */
		char option[16];
		char back[8];
		char const *cursor = rest;
		short row;

		option[0] = back[0] = 0;
		forge_word(&cursor, option, sizeof(option));
		forge_word(&cursor, back, sizeof(back));
		for (row = 0; row < NUMBER_OF_FORGE_ENVIRONMENT_ROWS && strcmp(option, forge_environment_row_words[row]); row++)
			;
		if (row >= NUMBER_OF_FORGE_ENVIRONMENT_ROWS)
			return FALSE;
		forge_environment_step(row, (short)(!strcmp(back, "-") ? -1 : 1));
		return TRUE;
	}
	if (!strcmp(word, "distance"))
	{
		forge_globals.held_distance = PIN((real)atof(rest), FORGE_MINIMUM_HOLD_DISTANCE, FORGE_MAXIMUM_HOLD_DISTANCE);
		return TRUE;
	}
	if (!strcmp(word, "nudge"))
	{
		static char const *const directions[NUMBER_OF_FORGE_NUDGES] = { "ahead", "back", "left", "right", "up", "down" };
		short direction;

		for (direction = 0; direction < NUMBER_OF_FORGE_NUDGES && strcmp(rest, directions[direction]); direction++)
			;
		if (direction >= NUMBER_OF_FORGE_NUDGES)
			return FALSE;
		forge_nudge(direction);
		return TRUE;
	}
	if (!strcmp(word, "camera"))
	{
		float x, y, z, yaw, pitch;

		if (sscanf(rest, "%f %f %f %f %f", &x, &y, &z, &yaw, &pitch) == 5)
		{
			real_point3d position;
			real_euler_angles2d angles;
			real_vector3d forward;

			position.x = x;
			position.y = y;
			position.z = z;
			angles.yaw = DEGREES_TO_RADIANS(yaw);
			angles.pitch = DEGREES_TO_RADIANS(pitch);
			vector3d_from_euler_angles2d(&forward, &angles);
			director_forge_camera(forge_globals.local_player_index, TRUE, &position, &forward);
			return TRUE;
		}
		return FALSE;
	}

	platform_log("forge: unknown command %s", word);
	return FALSE;
}

/* ---------- the markers and the Forge menu's link (their statics shared
with this file's) */

#include "forge_markers.inc"
#include "forge_spawns.inc"
#include "forge_netgame.inc"
#include "forge_physics.inc"
#include "forge_precision.inc"
#include "forge_ai.inc"
#include "forge_flyable.inc"
#include "forge_ai_move.inc"
#include "forge_volumes.inc"
#include "forge_environment.inc"
#include "forge_effects.inc"
#include "forge_menu_link.inc"
#include "forge_map_objects.inc"
