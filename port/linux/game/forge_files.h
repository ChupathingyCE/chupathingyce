/*
FORGE_FILES.H

Forge's layout files (forge_files.c): the format, its writer, and the
validator every file goes through before anything reads a byte of it, a
file made on this machine or one from anywhere else alike. Pure C with no
game dependency: the unit test (port/linux/tests/forge_files_test.c,
tools/test_forge_files.py) builds it on its own.

A layout is <stem>.hmv in the Forge folder's folder of its map
(forge_store.h: <save root>/forge/<map>/). Binary, little-endian, at most
64 KB:

	header  64 bytes
	  magic        char[4]  "HFRG"
	  version      u16      1 to 7 (below)
	  header size  u16      64
	  flags        u32      bit 0: a record is not a plain object (a spawn
	                        point, a teleporter, a game type object); no
	                        other bit
	  map name     char[32] the scenario's short name ("bloodgulch"),
	                        [a-z0-9_], zeros after it
	  map hash     u32      the map file's checksum when saved (a mismatch
	                        is only logged)
	  pack count   u16      0 to 8
	  path count   u16      0 to 256
	  record count u16      0 to 256
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
	  reserved u8[12] (zero)
	meta    meta size bytes: title, author, description and date, each
	        UTF-8 ended by a zero byte, in that order, nothing after them

Record kinds: 0 an object (a vehicle, weapon, equipment, scenery, machine,
control, light fixture, biped or AI character placed), 2 a player spawn
point, 5 a game type object (a netgame flag: a CTF flag's spot, a hill
point, an oddball spawn, a race checkpoint; its type in variant, its index
in team), 7 a teleporter sender, 8 a teleporter receiver (their channel in
team). Kinds 1, 3 and 4 (a squad, a weapon or vehicle spawn) are defined
but placed by nothing; 6, 9 and any other kind are refused.

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
Versions 8 and later (game mode markers, boundaries, a lobby camera, a
blank canvas) are refused with _forge_file_newer_version: this build reads
versions 1 to 7.

The validator (forge_file_decode), in order; a file is taken whole or not
at all:
- size: at least the 64-byte header, at most 64 KB;
- header: the magic; the version 1 to 7 (above 7: newer; 0: refused); the
  header size 64; no unknown flag; the map name [a-z0-9_], 1 to 31
  characters, only zeros after it, and the map expected (the folder's);
- counts: packs at most 8, paths at most 256, records at most 256, meta at
  most 2048 bytes;
- the payload size exactly the bytes after the header, and its CRC-32;
- each pack: its name [a-z0-9_ -], 1 to 24 characters, zeros after it;
- each path: 1 to 127 characters of [a-z0-9_ \-.], no "..", no empty part,
  no \ at either end, within the file; a group Forge places (vehi weap eqip
  scen bipd mach ctrl lifi actv); no path twice;
- each record: within the file; a known kind; its path index below the
  path count and of a group its kind takes, or none for a kind that names
  no object; team -1 to 7 (a spawn point -1 to 1, a teleporter's channel 0
  to 15, a game type object's index below its type's limit), respawn at
  most 600 s, count at most 16 (an object 0 or 1), game types and variant
  in range, no flag the version or the kind does not have, the AI team and
  brain only on a character's record (version 2 on), its respawn only with
  a time (version 3 on), physics fixed or phased, never both, never a
  character's (version 7); every float finite, positions within +-5000
  world units, angles within +-pi, a marker's pitch and roll zero; the
  reserved bytes zero;
- the layout: at most 32 teleporters, each sender's channel with a
  receiver; at most 64 game type objects, a hill's points at most 12; the
  flag bit 0 set exactly when a record is not a plain object; the version
  the lowest that holds it;
- the meta: four texts, each valid UTF-8 (no overlong form, surrogate or
  control character) within its room, the title not empty, the date
  "YYYY-MM-DD HH:MM" or empty, nothing after them.
Tag paths are never trusted to exist: when a layout is placed (forge.c),
a record whose tag the map does not have is skipped, and the budget
(FORGE_MAXIMUM_OBJECTS) and the map's bounds are checked again there.
*/

#ifndef __FORGE_FILES_H
#define __FORGE_FILES_H

/* ---------- constants */

#define FORGE_FILE_EXTENSION ".hmv"
/* the newest version written and read, and the oldest read */
#define FORGE_FILE_VERSION 7
#define FORGE_FILE_FIRST_VERSION 1
#define FORGE_FILE_HEADER_SIZE 64
#define FORGE_FILE_MAXIMUM_SIZE 65536
#define FORGE_FILE_MAXIMUM_PATHS 256
#define FORGE_FILE_MAXIMUM_PACKS 8
/* (forge.h FORGE_MAXIMUM_OBJECTS) */
#define FORGE_FILE_MAXIMUM_RECORDS 256
#define FORGE_FILE_MAXIMUM_META 2048
#define FORGE_FILE_PATH_MAXIMUM_LENGTH 127
#define FORGE_FILE_PACK_SIZE 32
#define FORGE_FILE_PACK_NAME_SIZE 24
#define FORGE_FILE_RECORD_SIZE 48
#define FORGE_FILE_RECORD_RESERVED_SIZE 12
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
/* a record's path index when its kind names no object */
#define FORGE_FILE_NO_PATH 0xFFFF

/* the header's flags */
enum
{
	FORGE_FILE_FLAG_NETGAME_SPAWNS = 1 << 0,
	FORGE_FILE_FLAGS_KNOWN = FORGE_FILE_FLAG_NETGAME_SPAWNS,
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
	NUMBER_OF_FORGE_RECORD_KINDS
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
that holds it (7 with an object's physics, 6 with a game type object, 5
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

/* whether a tag path is one a file may name: 1 to 127 characters of
[a-z0-9_ \-.], no "..", no empty part, not starting or ending with \ */
int forge_file_path_valid(
	char const *path,
	int length);

/* whether a map's short name is [a-z0-9_], 1 to 31 characters */
int forge_file_map_name_valid(
	char const *name);

/* whether a text is valid UTF-8 of fewer than maximum bytes with no control
character */
int forge_file_text_valid(
	char const *text,
	int maximum);

/* whether a file name is a layout's: <stem>.hmv, the stem 1 to 28 of
[a-z0-9_-]; nothing else is ever opened in the Forge folder */
int forge_file_name_valid(
	char const *file_name);

/* a file name made from a title (lower case letters and digits, the rest
'_', at most 22), with a number after it when number > 1: "gulch_2.hmv" */
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
