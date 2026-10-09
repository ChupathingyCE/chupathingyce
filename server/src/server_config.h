/*
SERVER_CONFIG.H

The dedicated server's files as text (server/docs/playlists.md,
gametypes.md, settings.md): playlists, game types and the settings file,
read strictly and written back in one form. Plain C, with no game types and
no files, so the same unit is built into the game and into the tests
(server/tests/control_test.c), which fuzz it.

Game types and settings are TOML: what the parsers take is a small part of
it (top-level keys and [tables] of keys, each value a whole number, true or
false, or a string in quotes, and # comments), and anything else is
refused with the line it is on. A playlist is the server's own form: a
game a line, a map and a game type.
*/

#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#include <stddef.h>

enum
{
	/* a playlist's or a game type's name, with its end: 1 to 31 of a-z, 0-9,
	_ and -, the first a letter or digit */
	SERVER_CONFIG_NAME_SIZE = 32,
	/* a playlist's entries, at most (dedicated.h's) */
	SERVER_PLAYLIST_ENTRIES = 64,
	/* an entry's map (as a playlist names it) and game type, with their ends */
	SERVER_PLAYLIST_MAP_SIZE = 128,
	SERVER_PLAYLIST_TYPE_SIZE = 32,
	/* the files, at most */
	SERVER_PLAYLIST_TEXT_SIZE = 8192,
	SERVER_GAMETYPE_TEXT_SIZE = 4096,
	SERVER_SETTINGS_TEXT_SIZE = 4096,
	/* a game type's title (its name in the game, 11 characters) with its end */
	SERVER_GAMETYPE_TITLE_SIZE = 12,
	/* a path in the data folder the settings name, with its end */
	SERVER_SETTINGS_PATH_SIZE = 128,
	/* a parser's error, with its end */
	SERVER_CONFIG_ERROR_SIZE = 160,
};

/* whether text is a playlist's or game type's name */
int server_config_name_valid(const char *text);

/* ---------- the built-in game types (game_engine_get_variant_by_name's) */

enum
{
	SERVER_ENGINE_CTF = 1,
	SERVER_ENGINE_SLAYER,
	SERVER_ENGINE_ODDBALL,
	SERVER_ENGINE_KING,
	SERVER_ENGINE_RACE,
};

struct server_builtin
{
	const char *name;
	/* its game engine (game_engine.h's index) */
	int engine;
};

extern const struct server_builtin server_builtins[];
extern const int server_builtin_count;

/* the built-in game type named so (its index), or -1 */
int server_builtin_find(const char *name);
/* an engine's name ("ctf", "slayer", ...) */
const char *server_engine_name(int engine);

/* ---------- playlists */

struct server_playlist_entry
{
	char map[SERVER_PLAYLIST_MAP_SIZE];
	char game_type[SERVER_PLAYLIST_TYPE_SIZE];
};

struct server_playlist
{
	int count;
	struct server_playlist_entry entries[SERVER_PLAYLIST_ENTRIES];
};

/* a playlist's text (length bytes). Strict, as the server takes a playlist
it is given to save: printable ASCII, tabs and line ends; each line that is
not blank or a # comment is exactly a map's name (command_line.h's) and a
game type's (a built-in's or a game type file's); at most
SERVER_PLAYLIST_ENTRIES. Not strict, as the server has always read a
playlist file: each line's first two words, the rest ignored, the lines
after the most it takes ignored. 1, else 0 and why ("line 3: ...") in
error */
int server_playlist_parse(const char *text, size_t length, int strict, struct server_playlist *playlist, char *error,
	int error_size);

/* a playlist as its file's text, with a heading naming it: its length, or
-1 if it does not fit in size bytes */
int server_playlist_format(const struct server_playlist *playlist, const char *name, char *out, size_t size);

/* ---------- game types */

enum
{
	SERVER_FIELD_INTEGER,
	SERVER_FIELD_BOOLEAN,
	SERVER_FIELD_CHOICE,
	SERVER_FIELD_STRING,
};

/* a game type's settings, in the order a file has them */
enum
{
	/* (the title, a string kept in the game type's title) */
	SERVER_GAMETYPE_NAME,
	/* (a choice of server_builtins, by index) */
	SERVER_GAMETYPE_BASE,
	SERVER_GAMETYPE_SCORE_LIMIT,
	SERVER_GAMETYPE_TIME_LIMIT,
	SERVER_GAMETYPE_TEAMS,

	SERVER_GAMETYPE_LIVES,
	SERVER_GAMETYPE_HEALTH,
	SERVER_GAMETYPE_SHIELDS,
	SERVER_GAMETYPE_RESPAWN_TIME,
	SERVER_GAMETYPE_RESPAWN_GROWTH,
	SERVER_GAMETYPE_SUICIDE_PENALTY,
	SERVER_GAMETYPE_ODD_MAN_OUT,
	SERVER_GAMETYPE_INVISIBLE,

	SERVER_GAMETYPE_WEAPON_SET,
	SERVER_GAMETYPE_STARTING_EQUIPMENT,
	SERVER_GAMETYPE_MAP_WEAPONS,
	SERVER_GAMETYPE_INFINITE_GRENADES,
	SERVER_GAMETYPE_LOADOUT,
	SERVER_GAMETYPE_PRIMARY_WEAPON,
	SERVER_GAMETYPE_SECONDARY_WEAPON,

	SERVER_GAMETYPE_OBJECTIVES,
	SERVER_GAMETYPE_PLAYERS_ON_RADAR,
	SERVER_GAMETYPE_FRIENDS_ON_SCREEN,

	SERVER_GAMETYPE_FRIENDLY_FIRE,
	SERVER_GAMETYPE_FRIENDLY_FIRE_PENALTY,
	SERVER_GAMETYPE_AUTO_BALANCE,

	SERVER_GAMETYPE_VEHICLE_RESPAWN,
	SERVER_GAMETYPE_RED_VEHICLES,
	SERVER_GAMETYPE_BLUE_VEHICLES,
	/* (a side's counts with "custom": warthog, ghost, scorpion, rocket
	warthog, banshee, gun turret, game_engine.h's order) */
	SERVER_GAMETYPE_RED_WARTHOG,
	SERVER_GAMETYPE_RED_GHOST,
	SERVER_GAMETYPE_RED_SCORPION,
	SERVER_GAMETYPE_RED_ROCKET_WARTHOG,
	SERVER_GAMETYPE_RED_BANSHEE,
	SERVER_GAMETYPE_RED_GUN_TURRET,
	SERVER_GAMETYPE_BLUE_WARTHOG,
	SERVER_GAMETYPE_BLUE_GHOST,
	SERVER_GAMETYPE_BLUE_SCORPION,
	SERVER_GAMETYPE_BLUE_ROCKET_WARTHOG,
	SERVER_GAMETYPE_BLUE_BANSHEE,
	SERVER_GAMETYPE_BLUE_GUN_TURRET,

	SERVER_GAMETYPE_CTF_ASSAULT,
	SERVER_GAMETYPE_CTF_SINGLE_FLAG_TIME,
	SERVER_GAMETYPE_CTF_FLAG_MUST_RESET,
	SERVER_GAMETYPE_CTF_FLAG_AT_HOME_TO_SCORE,
	SERVER_GAMETYPE_CTF_RESET_ON_CAPTURE,

	SERVER_GAMETYPE_SLAYER_DEATH_BONUS,
	SERVER_GAMETYPE_SLAYER_KILL_PENALTY,
	SERVER_GAMETYPE_SLAYER_KILL_IN_ORDER,

	SERVER_GAMETYPE_KING_MOVING_HILL,

	SERVER_GAMETYPE_ODDBALL_RANDOM_START,
	SERVER_GAMETYPE_ODDBALL_SPAWN_DELAY,
	SERVER_GAMETYPE_ODDBALL_SPEED_WITH_BALL,
	SERVER_GAMETYPE_ODDBALL_TRAIT_WITH_BALL,
	SERVER_GAMETYPE_ODDBALL_TRAIT_WITHOUT_BALL,
	SERVER_GAMETYPE_ODDBALL_BALL_TYPE,
	SERVER_GAMETYPE_ODDBALL_BALLS,

	SERVER_GAMETYPE_RACE_TYPE,
	SERVER_GAMETYPE_RACE_TEAM_SCORING,

	SERVER_GAMETYPE_FIELDS,
	/* a side's vehicles, "custom": their counts */
	SERVER_VEHICLES_CUSTOM = 0xFF,
	/* a vehicle's count on a side, at most (game_engine.h's) */
	SERVER_VEHICLES_MAXIMUM = 4,
	/* a score limit, at most: any the game's variant holds, but the game's
	advertisement carries it as a 16-bit number, and its lobby shows it in
	a box of a few digits */
	SERVER_SCORE_LIMIT_MAXIMUM = 9999,
};

struct server_choice
{
	const char *name;
	int value;
};

struct server_gametype_field
{
	/* "score_limit", "players.lives": its table, a dot, its key */
	const char *key;
	int kind;
	int minimum;
	int maximum;
	const struct server_choice *choices;
	int choice_count;
	/* a game engine's only (SERVER_ENGINE_*), else 0 */
	int engine;
	/* for the web page: what it is, and its unit */
	const char *label;
	const char *unit;
};

extern const struct server_gametype_field server_gametype_fields[SERVER_GAMETYPE_FIELDS];

struct server_gametype
{
	char title[SERVER_GAMETYPE_TITLE_SIZE];
	int values[SERVER_GAMETYPE_FIELDS];
	/* the settings the file gives (the others are its base's) */
	unsigned char set[SERVER_GAMETYPE_FIELDS];
};

/* a game type file's text (length bytes, at most SERVER_GAMETYPE_TEXT_SIZE):
every key one of server_gametype_fields, once, with a value of its kind
in its range, a game engine's settings only for that engine's base, base
given. 1, else 0 and why ("line 4: ...") in error */
int server_gametype_parse(const char *text, size_t length, struct server_gametype *gametype, char *error,
	int error_size);

/* a game type as its file's text: the settings it gives, base first: its
length, or -1 if it does not fit */
int server_gametype_format(const struct server_gametype *gametype, const char *name, char *out, size_t size);

/* a setting's value as a file has it (a choice's name, true, a number) into
text; and a value given as text (as a command gives one) for a setting:
1, else 0 and why */
void server_gametype_value_text(int field, int value, char *text, size_t size);
int server_gametype_value_parse(int field, const char *text, int *value, char *error, int error_size);

/* the setting a key is ("players.lives"), or -1 */
int server_gametype_field_find(const char *key);

/* ---------- the settings file */

enum
{
	SERVER_SETTING_NAME,
	SERVER_SETTING_MAXIMUM_PLAYERS,
	SERVER_SETTING_MINIMUM_PLAYERS,
	SERVER_SETTING_IDLE_LIMIT,
	SERVER_SETTING_PUBLIC,
	/* the playlist sv_playlist_use chose, and HALO_DEDICATED's value then
	(once it is another, HALO_DEDICATED's is played) */
	SERVER_SETTING_PLAYLIST,
	SERVER_SETTING_PLAYLIST_ENVIRONMENT,
	SERVER_SETTINGS,
	/* a game's players, at most (halo_port_limits.h's) */
	SERVER_SETTINGS_MAXIMUM_PLAYERS = 128,
	SERVER_SETTINGS_MAXIMUM_IDLE_LIMIT = 1440,
};

struct server_setting
{
	const char *key;
	int kind;
	int minimum;
	int maximum;
	/* the environment variable that wins over it, if any */
	const char *environment;
	const char *label;
};

extern const struct server_setting server_settings_table[SERVER_SETTINGS];

struct server_settings
{
	unsigned char set[SERVER_SETTINGS];
	int values[SERVER_SETTINGS];
	char name[16];
	char playlist[SERVER_SETTINGS_PATH_SIZE];
	char playlist_environment[SERVER_SETTINGS_PATH_SIZE];
};

/* whether text is a path a setting may name: a file in the data folder,
"playlists/x.txt" (printable ASCII but quotes and backslashes, no part
"..", not absolute) */
int server_settings_path_valid(const char *text);

/* the settings file's text: 1, else 0 and why */
int server_settings_parse(const char *text, size_t length, struct server_settings *settings, char *error,
	int error_size);
/* the settings as its file's text: its length, or -1 */
int server_settings_format(const struct server_settings *settings, char *out, size_t size);
/* the setting a key is, or -1 */
int server_settings_find(const char *key);
/* a value given as text for a setting (a command's): 1, else 0 and why */
int server_settings_value_parse(int setting, const char *text, struct server_settings *settings, char *error,
	int error_size);

#endif
