/*
FORGE_FILES.H

Forge's layout files (forge_files.c): the format, its writer, and the
validator every file goes through before anything reads a byte of it, a
file made on this machine or one from anywhere else alike. Pure C with no
game dependency: the unit test (port/linux/tests/forge_files_test.c,
tools/test_forge_files.py) builds it on its own.

A layout is <stem>.hmv in the Forge folder's folder of its map
(forge_store.h: <save root>/forge/<map>/). Binary, little-endian, at most
64 KB (more than 256 records only from version 13 on, below):

	header  64 bytes
	  magic        char[4]  "HFRG"
	  version      u16      1 to 7, or 11 to 16 (below)
	  header size  u16      64
	  flags        u32      bit 0: a record is not a plain object (a spawn
	                        point, a teleporter, a game type object); bit
	                        2: the map's own objects block (version 11 on);
	                        no other bit
	  map name     char[32] the scenario's short name ("bloodgulch"),
	                        [a-z0-9_], zeros after it
	  map hash     u32      the map file's checksum when saved (a mismatch
	                        is only logged)
	  pack count   u16      0 to 8
	  path count   u16      0 to 256
	  record count u16      0 to 256 (version 13: 0 to 512)
	  meta size    u16      4 to 2048
	  payload size u32      the bytes after the header, exactly
	  checksum     u32      CRC-32 (IEEE) of those bytes
	packs   pack count x 32 bytes: recipe u32[2], name char[24] ([a-z0-9_ -],
	        zeros after it). A list of extra object packs the layout was
	        made with; this build has none, validates the list and
	        ignores it (records whose tags the map lacks are skipped)
	paths   path count x { group tag u32, length u8, path char[length] }:
	        the tags the records place, by path, never by index
	records record count x 48 bytes:
	  path index u16 (0xFFFF: none), kind u8, team i8, position f32[3]
	  (world units), yaw pitch roll f32[3] (radians), respawn seconds u16,
	  count u8, game types u16, variant u8, flags u8, AI team u8,
	  AI behavior u8, AI route u8, AI options u8 (version 12, else
	  zero), the kind's own bytes u8[9] (version 14 on: below; zero for
	  every other kind)
	map's own objects (version 11 on, header flag bit 2)
	        block version u8 (1), zero u8, categories taken away whole
	        u16, entry count u16 (0 to 1024), zero u16; then the entries,
	        4 bytes each: category u8 (0 to 9), kept u8, index u16 (0 to
	        8191)
	meta    meta size bytes: title, author, description and date, each
	        UTF-8 ended by a zero byte, in that order, nothing after them

Record kinds: 0 an object (a vehicle, weapon, equipment, scenery, machine,
control, light fixture, biped or AI character placed), 2 a player spawn
point, 5 a game type object (a netgame flag: a CTF flag's spot, a hill
point, an oddball spawn, a race checkpoint; its type in variant, its index
in team), 7 a teleporter sender, 8 a teleporter receiver (their channel in
team), 19 an AI route point and 20 an AI patrol area (version 12; their
route in team, a point's stop or an area's size in variant), 21 a volume
(version 14: below), 22 the layout's sky and fog (version 15: below),
23 an effect and 24 a decal (version 16: below).
Kinds 1, 3 and 4 (a squad, a weapon or vehicle spawn) are defined but
placed by nothing; 6, 9 to 18 and any other kind are refused.

A volume (version 14, kind 21): a zone of the layout that does something
to whoever is in it, the Forge menu's VOLUMES. No path; its place the
middle of its floor (its base), its yaw alone; team the players it acts on
(-1 everyone, 0 red, 1 blue); variant its type (FORGE_FILE_VOLUME_*: kill,
damage, gravity); nothing else of an object's. Its own bytes: shape u8
(FORGE_FILE_VOLUME_BOX, _SPHERE), size u8[3] (quarter world units, 1 to
255: a box's half width, half length and height; a sphere's radius, then
two zeros), amount u8 (a kill volume's 0; a damage volume's percent of full
health a second, 1 to 100; a gravity volume's percent of gravity, 0 to
200), target u8 (0 every unit, 1 players only; a team's always 1), then
three zeros.

The sky and fog (version 15, kind 22): how a layout's outdoors looks, one
record at most. Its path a sky tag of the map's (group sky), or none for
the map's own sky; its place, angles and the rest of an object's zero. Its
own bytes: what it sets u8 (FORGE_FILE_ENVIRONMENT_SKY, _FOG: at least one;
the sky bit set exactly when it has a path), the fog's color u8[3] (red,
green, blue), its density u8 (percent, 1 to 100), where it starts u8 (world
units, 0 to 254) and where it is whole u8 (two world units each, 1 to 255,
past where it starts); the fog's bytes zero without the fog bit; then two
zeros.

An effect (version 16, kind 23): one of the map's effects placed in the
layout, started where it stands, again every so often. Its path an effect
(group effe); its place, its yaw; nothing else of an object's. Its own
bytes: its repeat u8 (seconds between starts, 1 to 60; 0 once, kept
looping when the effect loops), then zeros.
A decal (version 16, kind 24): one of the map's decals laid on the ground
under its place. Its path a decal (group deca); its place, its yaw; nothing
else of an object's. Its own bytes: its size u8 (tenths of the decal's own,
1 to 40), then zeros.

Versions: each adds what a record may hold. A layout is written as the
lowest version that holds it, so a layout without the newer things stays
readable by older builds, and a file of a version must hold something of
that version (a file a writer would not make is refused).
	1  objects: place, facing, team, respawn time, placed at start
	2  an AI character's AI team (whom it fights) and its brain off
	3  an AI character's respawn after its time
	4  player spawn points: team (any, red, blue), game types, initial
	   spawn or respawn point
	5  teleporters: senders and receivers linked by channel
	6  game type objects: CTF flag, hill point, oddball spawn, race
	   checkpoint
	7  an object's physics: fixed (never moves) or phased (no collision)
	11 the map's own objects the layout takes away: a placement of the
	   scenario's (its category - scenery, bipeds, vehicles, weapons,
	   equipment, machines, controls, light fixtures, sound scenery, or a
	   multiplayer map's item spawns - and its index in the scenario's
	   block) that a game of the layout does not make. Nothing of the map
	   is in the file and the map's file is never changed. One the editor
	   moved is taken away and placed as one of the layout's objects
	   where it was let go. An entry of a category taken away whole is
	   one placement kept instead (kept 1), any other one taken away
	   (kept 0); the entries ascend strictly (category, then index), and
	   take something away or keep something
	12 AI movement: route points (a route's points in the records'
	   order; each one's stop: none, a wait of 5 or 15 seconds, or a
	   landing, where an AI pilot sets down and its riders get out),
	   patrol areas (a size each), an AI character's behavior (hold,
	   patrol or pursue), its route and whether it takes vehicles, and a
	   vehicle's route (its AI pilot's, or its drivers')
	13 more than 256 records, up to 512 (the desktop builds' Forge budget,
	   forge.h FORGE_MAXIMUM_OBJECTS): a build from before reads a layout
	   this big as one from a newer build, not as a damaged one
	14 volumes: kill, damage and gravity zones (above)
	15 the layout's sky and fog (above)
	16 effects and decals (above)
Versions 8 to 10 (game mode markers, boundaries, a lobby camera) and 17 on
are refused with _forge_file_newer_version: this build reads versions 1 to
7 and 11 to 16. Version 11 is the Vita build's number for the same block
(its "blank canvas"), so those layouts' blocks are read the same; 8 to 10
keep meaning what they mean there. Each version reads what the ones below
it add: a version 12 layout may hold the map's own objects block too, and a
version 13 one AI movement and the block, a version 14, 15 or 16 one all of them.

The validator (forge_file_decode), in order; a file is taken whole or not
at all:
- size: at least the 64-byte header, at most 64 KB;
- header: the magic; the version 1 to 7, 11, 12 or 13 (8 to 10 and above
  13: newer; 0: refused); the header size 64; no unknown flag; the map
  name [a-z0-9_], 1 to 31
  characters, only zeros after it, and the map expected (the folder's);
- counts: packs at most 8, paths at most 256, records at most 256 (version
  13: 512), meta at
  most 2048 bytes;
- the payload size exactly the bytes after the header, and its CRC-32;
- each pack: its name [a-z0-9_ -], 1 to 24 characters, zeros after it;
- each path: 1 to 127 characters of [a-z0-9_ \-.], no "..", no empty part,
  no \ at either end, within the file; a group Forge places (vehi weap eqip
  scen bipd mach ctrl lifi actv), a sky (sky , version 15), or an effect
  or decal (effe deca, version 16); no path twice;
- each record: within the file; a known kind; its path index below the
  path count and of a group its kind takes, or none for a kind that names
  no object; team -1 to 7 (a spawn point -1 to 1, a teleporter's channel 0
  to 15, a game type object's index below its type's limit), respawn at
  most 600 s, count at most 16 (an object 0 or 1), game types and variant
  in range, no flag the version or the kind does not have, the AI team and
  brain only on a character's record (version 2 on), its respawn only with
  a time (version 3 on), physics fixed or phased, never both, never a
  character's (version 7); the AI behavior, route and options (version
  12) on a character's record, a route alone on a vehicle's, the behavior
  one of three, the route 0 (none) to 16, no unknown option; a route point
  or patrol area (version 12) with no path, its route 0 to 15, its stop or
  size in range, its yaw alone, nothing else; a volume's type, shape,
  size, amount and target in range, its yaw alone, nothing else (version
  14); the sky and fog's sky a sky's path, its fog's bytes in range,
  nothing else (version 15); an effect's path an effect's, a decal's a
  decal's, their own bytes in range, their yaw alone, nothing else
  (version 16); the kind's own bytes zero for any other kind; every float finite,
  positions within +-5000 world units, angles within +-pi, a marker's
  pitch and roll zero; the reserved bytes zero;
- the layout: at most 32 teleporters, each sender's channel with a
  receiver; at most 64 route points and 16 patrol areas; at most 64 game type objects, a hill's points at most 12; at most 32
  volumes; one sky and fog at most; at most 16 effects and 32 decals; the
  flag bit 0 set exactly when a record is not a plain object; the version
  the lowest that holds it;
- the map's own objects (version 11 on, flag bit 2): its block's version 1
  and zeros, its categories and entries in range, the entries ascending,
  something taken away or kept, a kept entry's category taken away whole
  and a taken entry's not;
- the meta: four texts, each valid UTF-8 (no overlong form, surrogate or
  control character) within its room, the title not empty, the date
  "YYYY-MM-DD HH:MM" or empty, nothing after them.
Tag paths are never trusted to exist: when a layout is placed (forge.c),
a record whose tag the map does not have is skipped, and the budget
(FORGE_MAXIMUM_OBJECTS) and the map's bounds are checked again there.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __FORGE_FILES_H
#define __FORGE_FILES_H

/* ---------- constants */

#define FORGE_FILE_EXTENSION ".hmv"
/* the newest version written and read, and the oldest read */
#define FORGE_FILE_VERSION 16
#define FORGE_FILE_FIRST_VERSION 1
/* (the versions this build does not read below FORGE_FILE_VERSION: the
Vita build's game mode markers, boundaries and lobby camera) */
#define FORGE_FILE_FIRST_SKIPPED_VERSION 8
#define FORGE_FILE_LAST_SKIPPED_VERSION 10
/* the first version with more than FORGE_FILE_EARLIER_MAXIMUM_RECORDS */
#define FORGE_FILE_MORE_RECORDS_VERSION 13
#define FORGE_FILE_EARLIER_MAXIMUM_RECORDS 256
/* the version of volumes (kind 21) and the kind's own bytes */
#define FORGE_FILE_VOLUMES_VERSION 14
/* the version of a layout's sky and fog (kind 22) */
#define FORGE_FILE_ENVIRONMENT_VERSION 15
/* the version of effects and decals (kinds 23 and 24) */
#define FORGE_FILE_EFFECTS_VERSION 16
#define FORGE_FILE_HEADER_SIZE 64
#define FORGE_FILE_MAXIMUM_SIZE 65536
#define FORGE_FILE_MAXIMUM_PATHS 256
#define FORGE_FILE_MAXIMUM_PACKS 8
/* (forge.h FORGE_MAXIMUM_OBJECTS: a desktop build's; version 13 on) */
#define FORGE_FILE_MAXIMUM_RECORDS 512
#define FORGE_FILE_MAXIMUM_META 2048
#define FORGE_FILE_PATH_MAXIMUM_LENGTH 127
#define FORGE_FILE_PACK_SIZE 32
#define FORGE_FILE_PACK_NAME_SIZE 24
#define FORGE_FILE_RECORD_SIZE 48
/* (version 14 on) a record's own bytes, after the AI's: a kind's that has
them (a volume's, a layout's sky and fog, an effect's or decal's), zeros
for any other */
#define FORGE_FILE_RECORD_PARAMS_SIZE 9
/* the texts' room, each one's terminator included */
#define FORGE_FILE_MAP_NAME_SIZE 32
#define FORGE_FILE_TITLE_SIZE 32
#define FORGE_FILE_AUTHOR_SIZE 32
#define FORGE_FILE_DESCRIPTION_SIZE 128
/* "YYYY-MM-DD HH:MM", or empty */
#define FORGE_FILE_DATE_SIZE 17
/* a file name: <stem>.hmv, the stem [a-z0-9_-] */
#define FORGE_FILE_NAME_SIZE 40
#define FORGE_FILE_STEM_MAXIMUM_LENGTH 28
/* every coordinate within this (world units) */
#define FORGE_FILE_MAXIMUM_COORDINATE 5000.f
#define FORGE_FILE_MAXIMUM_RESPAWN_SECONDS 600
#define FORGE_FILE_MAXIMUM_COUNT 16
#define FORGE_FILE_MAXIMUM_TEAM 7
#define FORGE_FILE_MAXIMUM_VARIANT 15
/* the game types (ctf, slayer, oddball, king of the hill, race; a player
spawn's infection too), a bit each in the engine's order */
#define FORGE_FILE_GAME_TYPES_MASK 0x1F
#define FORGE_FILE_GAME_TYPE_CTF (1 << 0)
#define FORGE_FILE_GAME_TYPE_SLAYER (1 << 1)
#define FORGE_FILE_GAME_TYPE_ODDBALL (1 << 2)
#define FORGE_FILE_GAME_TYPE_KING (1 << 3)
#define FORGE_FILE_GAME_TYPE_RACE (1 << 4)
#define FORGE_FILE_GAME_TYPE_INFECTION (1 << 5)
#define FORGE_FILE_SPAWN_GAME_TYPES_MASK 0x3F
/* a player spawn's teams: any (-1), red, blue */
#define FORGE_FILE_MAXIMUM_SPAWN_TEAM 1
/* (version 5) teleporters: their channels (a record's team), the most of
them in a layout */
#define FORGE_FILE_MAXIMUM_TELEPORTER_CHANNELS 16
#define FORGE_FILE_MAXIMUM_TELEPORTERS 32
/* (version 6) game type objects: the netgame flag types a record may be
(the engine's _netgame_flag_*, scenario_definitions.h), each one's indices,
the most of them in a layout and of one hill's points (the engine's
MAXIMUM_HILL_POINTS) */
#define FORGE_FILE_FLAG_TYPE_CTF 0
#define FORGE_FILE_FLAG_TYPE_ODDBALL 2
#define FORGE_FILE_FLAG_TYPE_RACE 3
#define FORGE_FILE_FLAG_TYPE_HILL 8
#define FORGE_FILE_MAXIMUM_CTF_TEAMS 2
#define FORGE_FILE_MAXIMUM_HILLS 16
#define FORGE_FILE_MAXIMUM_ODDBALLS 16
#define FORGE_FILE_MAXIMUM_RACE_CHECKPOINTS 32
#define FORGE_FILE_MAXIMUM_GAME_OBJECTS 64
#define FORGE_FILE_MAXIMUM_HILL_POINTS 12
/* (version 12) AI movement: a route's number (a record's team for a route
point or patrol area; 1 more in a record's AI route, 0 none), the most
route points and patrol areas in a layout, a route point's stops and a
patrol area's sizes (variant), a character's behaviors and options */
#define FORGE_FILE_MAXIMUM_ROUTES 16
#define FORGE_FILE_MAXIMUM_ROUTE_POINTS 64
#define FORGE_FILE_MAXIMUM_AI_AREAS 16
enum
{
	FORGE_FILE_ROUTE_STOP_NONE = 0,
	FORGE_FILE_ROUTE_STOP_WAIT_5,
	FORGE_FILE_ROUTE_STOP_WAIT_15,
	FORGE_FILE_ROUTE_STOP_LAND,
	NUMBER_OF_FORGE_FILE_ROUTE_STOPS
};
/* (a patrol area's radius by its size: 3, 5, 8, 12, 20 world units) */
#define FORGE_FILE_AI_AREA_SIZES 5
enum
{
	FORGE_FILE_AI_HOLD = 0,
	FORGE_FILE_AI_PATROL,
	FORGE_FILE_AI_PURSUE,
	NUMBER_OF_FORGE_FILE_AI_BEHAVIORS
};
enum
{
	/* the character boards a free vehicle near it and drives (or guns) */
	FORGE_FILE_AI_OPTION_VEHICLES = 1 << 0,
	FORGE_FILE_AI_OPTIONS_KNOWN = FORGE_FILE_AI_OPTION_VEHICLES,
};
/* (version 14) volumes: the most in a layout, their types (a record's
variant), shapes and targets (its own bytes), and their ranges */
#define FORGE_FILE_MAXIMUM_VOLUMES 32
enum
{
	FORGE_FILE_VOLUME_KILL = 0,
	FORGE_FILE_VOLUME_DAMAGE,
	FORGE_FILE_VOLUME_GRAVITY,
	NUMBER_OF_FORGE_FILE_VOLUME_TYPES
};
enum
{
	FORGE_FILE_VOLUME_BOX = 0,
	FORGE_FILE_VOLUME_SPHERE,
	NUMBER_OF_FORGE_FILE_VOLUME_SHAPES
};
enum
{
	FORGE_FILE_VOLUME_EVERY_UNIT = 0,
	FORGE_FILE_VOLUME_PLAYERS,
	NUMBER_OF_FORGE_FILE_VOLUME_TARGETS
};
/* (its own bytes' places) */
enum
{
	FORGE_FILE_VOLUME_PARAM_SHAPE = 0,
	FORGE_FILE_VOLUME_PARAM_SIZE,
	FORGE_FILE_VOLUME_PARAM_AMOUNT = FORGE_FILE_VOLUME_PARAM_SIZE + 3,
	FORGE_FILE_VOLUME_PARAM_TARGET,
};
/* a size byte's world units (a quarter each) */
#define FORGE_FILE_VOLUME_SIZE_UNIT 0.25f
#define FORGE_FILE_MAXIMUM_VOLUME_DAMAGE 100
#define FORGE_FILE_MAXIMUM_VOLUME_GRAVITY 200
/* (version 15) the layout's sky and fog: what it sets (its own bytes'
first), the places of the rest, their ranges */
enum
{
	FORGE_FILE_ENVIRONMENT_SKY = 1 << 0,
	FORGE_FILE_ENVIRONMENT_FOG = 1 << 1,
	FORGE_FILE_ENVIRONMENT_KNOWN = FORGE_FILE_ENVIRONMENT_SKY | FORGE_FILE_ENVIRONMENT_FOG,
};
enum
{
	FORGE_FILE_ENVIRONMENT_PARAM_SETS = 0,
	FORGE_FILE_ENVIRONMENT_PARAM_COLOR,
	FORGE_FILE_ENVIRONMENT_PARAM_DENSITY = FORGE_FILE_ENVIRONMENT_PARAM_COLOR + 3,
	FORGE_FILE_ENVIRONMENT_PARAM_START,
	FORGE_FILE_ENVIRONMENT_PARAM_OPAQUE,
};
#define FORGE_FILE_MAXIMUM_FOG_DENSITY 100
#define FORGE_FILE_MAXIMUM_FOG_START 254
/* (the whole fog's byte: two world units each) */
#define FORGE_FILE_FOG_OPAQUE_UNIT 2.f
/* the sky's group (sky ) */
#define FORGE_FILE_SKY_GROUP 0x736B7920
/* (version 16) effects and decals: the most in a layout, their groups,
their own bytes' ranges */
#define FORGE_FILE_MAXIMUM_EFFECTS 16
#define FORGE_FILE_MAXIMUM_DECALS 32
#define FORGE_FILE_EFFECT_GROUP 0x65666665
#define FORGE_FILE_DECAL_GROUP 0x64656361
#define FORGE_FILE_MAXIMUM_EFFECT_REPEAT 60
#define FORGE_FILE_MAXIMUM_DECAL_SIZE 40
enum
{
	FORGE_FILE_EFFECT_PARAM_REPEAT = 0,
	FORGE_FILE_DECAL_PARAM_SIZE = 0,
};

/* a record's path index when its kind names no object */
#define FORGE_FILE_NO_PATH 0xFFFF
/* (version 11) the map's own objects: the categories of the scenario's
placements (numbers saved in files: stable), the block's sizes and
ranges */
enum
{
	_forge_canvas_scenery = 0,
	_forge_canvas_bipeds,
	_forge_canvas_vehicles,
	_forge_canvas_weapons,
	_forge_canvas_equipment,
	_forge_canvas_machines,
	_forge_canvas_controls,
	_forge_canvas_light_fixtures,
	_forge_canvas_sound_scenery,
	/* a multiplayer map's item spawns (the scenario's netgame equipment) */
	_forge_canvas_items,
	NUMBER_OF_FORGE_CANVAS_CATEGORIES
};
#define FORGE_FILE_CANVAS_VERSION 1
#define FORGE_FILE_CANVAS_HEADER_SIZE 8
#define FORGE_FILE_CANVAS_ENTRY_SIZE 4
#define FORGE_FILE_MAXIMUM_CANVAS_ENTRIES 1024
#define FORGE_FILE_CANVAS_MAXIMUM_INDEX 8191
#define FORGE_FILE_CANVAS_CATEGORIES_MASK ((1 << NUMBER_OF_FORGE_CANVAS_CATEGORIES) - 1)

/* the header's flags */
enum
{
	FORGE_FILE_FLAG_NETGAME_SPAWNS = 1 << 0,
	/* (version 11) the map's own objects block follows the records (bit
	1, the Vita build's game modes' settings, is not read) */
	FORGE_FILE_FLAG_CANVAS = 1 << 2,
	FORGE_FILE_FLAGS_KNOWN = FORGE_FILE_FLAG_NETGAME_SPAWNS | FORGE_FILE_FLAG_CANVAS,
};

/* a record's flags */
enum
{
	/* placed when the layout starts */
	FORGE_RECORD_PLACE_AT_START = 1 << 0,
	/* (version 2, a character's only) its AI's brain off: it stands as
	placed (the engine's ai_braindead) */
	FORGE_RECORD_AI_OFF = 1 << 1,
	/* (version 3, a character's only) dead, it is placed again after its
	respawn time */
	FORGE_RECORD_AI_RESPAWN = 1 << 2,
	/* (version 4, a player spawn's only) an initial spawn (a round's first
	spawns), else a respawn point */
	FORGE_RECORD_SPAWN_INITIAL = 1 << 3,
	/* (version 7, an object's that is no character's only) its physics:
	fixed (never moves), or phased (no collision); never both */
	FORGE_RECORD_PHYSICS_FIXED = 1 << 4,
	FORGE_RECORD_PHYSICS_PHASED = 1 << 5,
	FORGE_RECORD_PHYSICS_MASK = FORGE_RECORD_PHYSICS_FIXED | FORGE_RECORD_PHYSICS_PHASED,
	FORGE_RECORD_FLAGS_VERSION_1 = FORGE_RECORD_PLACE_AT_START,
	FORGE_RECORD_FLAGS_VERSION_2 = FORGE_RECORD_PLACE_AT_START | FORGE_RECORD_AI_OFF,
	FORGE_RECORD_FLAGS_VERSION_3 = FORGE_RECORD_PLACE_AT_START | FORGE_RECORD_AI_OFF | FORGE_RECORD_AI_RESPAWN,
	FORGE_RECORD_FLAGS_VERSION_4 = FORGE_RECORD_FLAGS_VERSION_3 | FORGE_RECORD_SPAWN_INITIAL,
	FORGE_RECORD_FLAGS_KNOWN = FORGE_RECORD_FLAGS_VERSION_4 | FORGE_RECORD_PHYSICS_MASK,
};

/* (version 2) a character's AI team: whom it fights. The engine's teams
(game.h _game_team_*), and the player's own */
enum
{
	/* the character's own (its unit's default team) */
	FORGE_AI_TEAM_DEFAULT = 0,
	/* the player's own team: fights for the player, never against */
	FORGE_AI_TEAM_PLAYER,
	FORGE_AI_TEAM_HUMAN,
	FORGE_AI_TEAM_COVENANT,
	FORGE_AI_TEAM_FLOOD,
	FORGE_AI_TEAM_SENTINEL,
	NUMBER_OF_FORGE_AI_TEAMS
};

/* a record's kind (numbers saved in files: stable) */
enum
{
	_forge_record_object = 0,
	_forge_record_squad,
	_forge_record_player_spawn,
	_forge_record_weapon_spawn,
	_forge_record_vehicle_spawn,
	/* (version 6) a game type object: variant its FORGE_FILE_FLAG_TYPE_*,
	team its index */
	_forge_record_flag,
	/* (never written: refused) */
	_forge_record_hill,
	/* (version 5) a teleporter's sender and its receiver: team its channel */
	_forge_record_teleporter_in,
	_forge_record_teleporter_out,
	/* (never written: refused) */
	_forge_record_kill_zone,
	NUMBER_OF_FORGE_RECORD_KINDS,
	/* (10 to 18: the Vita build's game mode markers, a boundary, the lobby
	camera; refused here) */
	/* (version 12) an AI route's point and a patrol area: team their route,
	variant a point's stop (FORGE_FILE_ROUTE_STOP_*) or an area's size */
	_forge_record_ai_route_point = 19,
	_forge_record_ai_area = 20,
	/* (version 14) a volume: variant its type, team the players it acts
	on, its own bytes its shape, size, amount and target */
	_forge_record_volume = 21,
	/* (version 15) the layout's sky and fog: its path the sky (or none),
	its own bytes what it sets and the fog */
	_forge_record_environment = 22,
	/* (version 16) an effect and a decal of the map's: its path the tag,
	its own bytes its repeat or size */
	_forge_record_effect = 23,
	_forge_record_decal = 24,
};

/* why a file was refused (forge_file_decode), in the order checked */
enum
{
	_forge_file_ok = 0,
	_forge_file_too_small,
	_forge_file_too_large,
	_forge_file_bad_magic,
	_forge_file_bad_version,
	/* a version above FORGE_FILE_VERSION: made by a newer build */
	_forge_file_newer_version,
	_forge_file_bad_header_size,
	_forge_file_bad_flags,
	_forge_file_bad_map_name,
	_forge_file_wrong_map,
	_forge_file_too_many,
	_forge_file_bad_payload_size,
	_forge_file_bad_checksum,
	_forge_file_bad_pack,
	_forge_file_bad_path,
	_forge_file_bad_group,
	_forge_file_duplicate_path,
	_forge_file_bad_record,
	_forge_file_bad_meta,
	_forge_file_no_room,
	NUMBER_OF_FORGE_FILE_RESULTS
};

/* ---------- structures */

struct forge_file_pack
{
	unsigned int recipe[2];
	char name[FORGE_FILE_PACK_NAME_SIZE + 1];
};

struct forge_file_path
{
	unsigned int group_tag;
	/* (its length, at most FORGE_FILE_PATH_MAXIMUM_LENGTH) */
	char path[FORGE_FILE_PATH_MAXIMUM_LENGTH + 1];
};

struct forge_file_record
{
	unsigned short path_index;
	unsigned char kind;
	signed char team;
	float position[3];
	/* yaw, pitch, roll, radians, within +-pi */
	float angles[3];
	unsigned short respawn_seconds;
	unsigned char count;
	unsigned short game_types;
	unsigned char variant;
	unsigned char flags;
	/* (version 2, a character's only: FORGE_AI_TEAM_*; else 0) */
	unsigned char ai_team;
	/* (version 12) a character's behavior (FORGE_FILE_AI_*) and options
	(FORGE_FILE_AI_OPTION_*), a character's or a vehicle's route (1 to
	FORGE_FILE_MAXIMUM_ROUTES; 0 none); else 0 */
	unsigned char ai_behavior;
	unsigned char ai_route;
	unsigned char ai_options;
	/* (version 14 on) the kind's own bytes (forge_files.h's per kind); zeros
	for a kind that has none */
	unsigned char params[FORGE_FILE_RECORD_PARAMS_SIZE];
};

/* (version 11) the map's own objects a layout takes away: the categories
taken away whole, and the single placements kept of those or taken away of
the others */
struct forge_file_canvas_entry
{
	unsigned char category;
	unsigned char kept;
	unsigned short index;
};

struct forge_file_canvas
{
	unsigned short categories;
	short entry_count;
	struct forge_file_canvas_entry entries[FORGE_FILE_MAXIMUM_CANVAS_ENTRIES];
};

/* a layout, as read or to be written */
struct forge_file_layout
{
	/* the file's version: as read; to write, 0 for the lowest that holds
	the layout (forge_file_layout_version) */
	unsigned short version;
	unsigned int flags;
	char map_name[FORGE_FILE_MAP_NAME_SIZE];
	unsigned int map_hash;
	short pack_count;
	short path_count;
	short record_count;
	struct forge_file_pack packs[FORGE_FILE_MAXIMUM_PACKS];
	struct forge_file_path paths[FORGE_FILE_MAXIMUM_PATHS];
	struct forge_file_record records[FORGE_FILE_MAXIMUM_RECORDS];
	char title[FORGE_FILE_TITLE_SIZE];
	char author[FORGE_FILE_AUTHOR_SIZE];
	char description[FORGE_FILE_DESCRIPTION_SIZE];
	char date[FORGE_FILE_DATE_SIZE];
	/* (version 11, with FORGE_FILE_FLAG_CANVAS; else zeros) */
	struct forge_file_canvas canvas;
};

/* ---------- prototypes/FORGE_FILES.C */

/* the CRC-32 (IEEE) of size bytes, from crc (0 to start) */
unsigned int forge_file_crc32(
	unsigned int crc,
	unsigned char const *bytes,
	int size);

/* a file read: every check above, in order, before anything is taken from
it (expected_map: the map it must be of, NULL for any). The layout is
filled only when it passes (else left empty); _forge_file_ok, else why not
(and, in why, its words for the log, when why is not NULL) */
int forge_file_decode(
	unsigned char const *bytes,
	int size,
	char const *expected_map,
	struct forge_file_layout *layout,
	char *why,
	int why_size);

/* a layout written: checked as a file read is (a layout that would not be
read back is not written); the bytes written, or -1 (why in result when
not NULL) */
int forge_file_encode(
	struct forge_file_layout const *layout,
	unsigned char *bytes,
	int capacity,
	int *result);

/* a layout's own checks (the ones a file read gets after its framing):
_forge_file_ok or why not */
int forge_file_layout_check(
	struct forge_file_layout const *layout,
	char const *expected_map);

/* the version a layout is written as: its own when set, else the lowest
that holds it (16 with an effect or decal, 15 with a sky and fog, 14 with a volume, 12 with AI movement, 11 with the map's own objects block, 7 with an object's physics, 6 with a game type object, 5
with a teleporter, 4 with a player spawn, 3 with a respawning character, 2
when a record has an AI setting, else 1) */
int forge_file_layout_version(
	struct forge_file_layout const *layout);

/* a result's words (English, for the log and the player) */
char const *forge_file_result_name(
	int result);

/* whether a group tag is one Forge places (vehi weap eqip scen bipd mach
ctrl lifi actv) */
int forge_file_group_allowed(
	unsigned int group_tag);

/* whether a group tag is one a layout's path may name: a group Forge
places, a sky (version 15), or an effect or a decal (version 16) */
int forge_file_path_group_allowed(
	unsigned int group_tag);

/* whether a tag path is one a file may name: 1 to 127 characters of
[a-z0-9_ \-.], no "..", no empty part, not starting or ending with \ */
int forge_file_path_valid(
	char const *path,
	int length);

/* whether a map's short name is [a-z0-9_], 1 to 31 characters, and not a
name Windows keeps for a device (forge_file_name_reserved: it names the
map's layout folder) */
int forge_file_map_name_valid(
	char const *name);

/* whether a text is valid UTF-8 of fewer than maximum bytes with no control
character */
int forge_file_text_valid(
	char const *text,
	int maximum);

/* whether a file name is a layout's: <stem>.hmv, the stem 1 to 28 of
[a-z0-9_-] and not a name Windows keeps for a device; nothing else is ever
opened in the Forge folder */
int forge_file_name_valid(
	char const *file_name);

/* whether a name is one Windows keeps for a device, in any case and with or
without an extension: con, prn, aux, nul, com1 to com9, lpt1 to lpt9
("nul.hmv" or "COM1.txt" open the device, not a file) */
int forge_file_name_reserved(
	char const *name);

/* a file name made from a title (lower case letters and digits, the rest
'_', at most 22), with a number after it when number > 1: "gulch_2.hmv";
a stem Windows keeps for a device is numbered too: "con_1.hmv" */
void forge_file_name_from_title(
	char const *title,
	int number,
	char *file_name,
	int size);

/* (version 6) a game type object's index limit for its flag type
(FORGE_FILE_FLAG_TYPE_*): the indices below it; 0 for a type not in the
list */
int forge_file_flag_type_indices(
	int flag_type);

/* an angle in radians brought within +-pi (the writer's) */
float forge_file_angle_reduce(
	float angle);

#endif
