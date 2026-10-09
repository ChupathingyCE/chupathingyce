/*
SERVER_ADMIN.C

The dedicated server's files that its admins make (server/docs/admin.md,
playlists.md, gametypes.md, settings.md), and the commands that make them:
on the console, in the startup commands, through the control API and its
web admin page (server_commands.c runs them, on the game's main thread).

Everything the server writes is in the data folder's admin/ folder, which
must be writable (in a container, the instance's own data folder):
  admin/playlists/<name>.txt   playlists made or edited here
  admin/gametypes/<name>.toml  game types (game type files, SAPP style)
  admin/settings.toml          the settings sv_set and the page save, and
                               the playlist sv_playlist_use chose
The data folder's playlists/ (the server's own playlists, which a container
mounts read-only and a new release replaces) is read, never written: a
playlist of the same name edited here is saved in admin/playlists, and that
one is then played and listed by the name. Every file is written whole
(a new file beside it, then renamed over it) in one form
(server_config.c), after it is checked as a whole: its maps the server's,
its game types the built-ins or files that read right.

A game type file names a built-in game type (base) and the settings it
changes: those of the game's own gametype editor, and score limits past
its (the variant holds any; server_config.h's SERVER_SCORE_LIMIT_MAXIMUM
for the advertisement's 16 bits). The server builds the base's variant,
changes it so, with the game type's PC options (the network game's
variant_options, which every version 11 machine reads), and the lobby is
given both (dedicated.c). Nothing a client is sent is new.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game_engine.h"
#include "command_line.h"
#include "dedicated.h"
#include "server_admin.h"
#include "server_config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* (the game's: game_time.h) */
	ADMIN_TICKS_PER_SECOND = 30,
	/* the playlists or game types listed, at most */
	MAXIMUM_LISTED = 256,
	/* a path in the data folder, with its end */
	ADMIN_PATH_SIZE = SERVER_SETTINGS_PATH_SIZE,
};

#define ADMIN_FOLDER "admin"
#define PLAYLISTS_FOLDER "admin/playlists"
#define SHIPPED_PLAYLISTS_FOLDER "playlists"
#define GAMETYPES_FOLDER "admin/gametypes"
#define SETTINGS_FILE "admin/settings.toml"

/* ---------- structures */

/* a playlist by its name: the file it is (admin/playlists' if there is
one, else playlists'), and which there are */
struct playlist_file
{
	char name[SERVER_CONFIG_NAME_SIZE];
	char path[ADMIN_PATH_SIZE];
	boolean admin;
	boolean shipped;
};

struct name_list
{
	char names[MAXIMUM_LISTED][SERVER_CONFIG_NAME_SIZE];
	long count;
};

/* ---------- globals */

static char admin_text[SERVER_PLAYLIST_TEXT_SIZE + 1];
static char admin_problem[SERVER_CONFIG_ERROR_SIZE + 128];
static struct server_playlist admin_playlist;
static struct server_gametype admin_gametype;
static struct name_list admin_names;
static struct name_list admin_shipped_names;

/* ---------- files */

/* a path in the data folder ("admin/playlists/x.txt") as the game opens
it (d:\admin\playlists\x.txt) */
static void xbox_path(
	char const *path,
	char *out,
	long size)
{
	char *cursor;

	snprintf(out, (size_t)size, "d:\\%s", path);
	for (cursor = out; *cursor; cursor++)
	{
		if (*cursor == '/')
			*cursor = '\\';
	}
}

static boolean file_exists(
	char const *path)
{
	char game_path[ADMIN_PATH_SIZE + 8];
	FILE *file;

	xbox_path(path, game_path, sizeof(game_path));
	file = fopen(game_path, "rb");
	if (!file)
		return FALSE;
	fclose(file);
	return TRUE;
}

/* a file's text, at most maximum bytes, into buffer (maximum + 1 bytes):
1, 0 if it is there but cannot be read (why in problem), -1 if it is not
there */
static int read_text(
	char const *path,
	char *buffer,
	long maximum,
	long *length,
	char *problem,
	long problem_size)
{
	char game_path[ADMIN_PATH_SIZE + 8];
	FILE *file;
	size_t count;

	xbox_path(path, game_path, sizeof(game_path));
	*length = 0;
	buffer[0] = 0;
	errno = 0;
	file = fopen(game_path, "rb");
	if (!file)
	{
		if (errno == ENOENT || !errno)
			return -1;
		snprintf(problem, (size_t)problem_size, "cannot read %s (%s)", path, strerror(errno));
		return 0;
	}
	count = fread(buffer, 1, (size_t)maximum + 1, file);
	if (ferror(file))
	{
		snprintf(problem, (size_t)problem_size, "cannot read %s (%s)", path, strerror(errno));
		fclose(file);
		return 0;
	}
	fclose(file);
	if (count > (size_t)maximum)
	{
		snprintf(problem, (size_t)problem_size, "%s is larger than %ld bytes", path, maximum);
		return 0;
	}
	buffer[count] = 0;
	*length = (long)count;
	return 1;
}

/* a folder in the data folder made, if it is not there (its parent too) */
static void make_folder(
	char const *path)
{
	char game_path[ADMIN_PATH_SIZE + 8];

	xbox_path(ADMIN_FOLDER, game_path, sizeof(game_path));
	CreateDirectoryA(game_path, NULL);
	if (strcmp(path, ADMIN_FOLDER))
	{
		xbox_path(path, game_path, sizeof(game_path));
		CreateDirectoryA(game_path, NULL);
	}
}

/* a file written whole: a new file beside it, then renamed over it, so a
reader sees the old or the new one, never half of one. TRUE, else FALSE and
why in problem (nothing changed) */
static boolean write_text(
	char const *folder,
	char const *path,
	char const *text,
	char *problem,
	long problem_size)
{
	char game_path[ADMIN_PATH_SIZE + 8];
	char temporary[ADMIN_PATH_SIZE + 16];
	FILE *file;
	size_t length = strlen(text);

	make_folder(folder);
	xbox_path(path, game_path, sizeof(game_path));
	snprintf(temporary, sizeof(temporary), "%s.new", game_path);
	errno = 0;
	file = fopen(temporary, "wb");
	if (!file)
	{
		snprintf(problem, (size_t)problem_size, "cannot write in %s in the data folder (%s): nothing changed", folder,
			errno ? strerror(errno) : "it cannot be made");
		return FALSE;
	}
	if (fwrite(text, 1, length, file) != length || fflush(file) || ferror(file))
	{
		snprintf(problem, (size_t)problem_size, "cannot write %s (%s): nothing changed", path, strerror(errno));
		fclose(file);
		remove(temporary);
		return FALSE;
	}
	if (fclose(file))
	{
		snprintf(problem, (size_t)problem_size, "cannot write %s (%s): nothing changed", path, strerror(errno));
		remove(temporary);
		return FALSE;
	}
	if (rename(temporary, game_path))
	{
#ifdef _WIN32
		/* (Windows' rename does not replace a file) */
		if (!remove(game_path) && !rename(temporary, game_path))
			return TRUE;
#endif
		snprintf(problem, (size_t)problem_size, "cannot write %s (%s): nothing changed", path, strerror(errno));
		remove(temporary);
		return FALSE;
	}
	return TRUE;
}

static boolean delete_file(
	char const *path,
	char *problem,
	long problem_size)
{
	char game_path[ADMIN_PATH_SIZE + 8];

	xbox_path(path, game_path, sizeof(game_path));
	if (remove(game_path))
	{
		snprintf(problem, (size_t)problem_size, "cannot delete %s (%s)", path, strerror(errno));
		return FALSE;
	}
	return TRUE;
}

static int name_compare(
	void const *a,
	void const *b)
{
	return strcmp((char const *)a, (char const *)b);
}

/* the names of a folder's files of an extension (".txt") that are names
the server takes, sorted */
static void list_folder(
	char const *folder,
	char const *extension,
	struct name_list *list)
{
	char pattern[ADMIN_PATH_SIZE + 16];
	WIN32_FIND_DATAA data;
	HANDLE find;
	size_t extension_length = strlen(extension);

	list->count = 0;
	snprintf(pattern, sizeof(pattern), "%s/*%s", folder, extension);
	{
		char game_path[ADMIN_PATH_SIZE + 24];

		xbox_path(pattern, game_path, sizeof(game_path));
		find = FindFirstFileA(game_path, &data);
	}
	if (find == INVALID_HANDLE_VALUE)
		return;
	do
	{
		char name[SERVER_CONFIG_NAME_SIZE];
		size_t length = strlen(data.cFileName);

		if (length <= extension_length || length - extension_length >= sizeof(name) ||
			_stricmp(data.cFileName + length - extension_length, extension))
		{
			continue;
		}
		snprintf(name, sizeof(name), "%.*s", (int)(length - extension_length), data.cFileName);
		if (server_config_name_valid(name) && list->count < MAXIMUM_LISTED)
			snprintf(list->names[list->count++], sizeof(list->names[0]), "%s", name);
	}
	while (FindNextFileA(find, &data));
	CloseHandle(find);
	qsort(list->names, (size_t)list->count, sizeof(list->names[0]), name_compare);
}

/* ---------- output */

/* a map as the commands show it (a path's last part) */
static char const *map_display_name(
	char const *map)
{
	char const *base = map;
	char const *cursor;

	for (cursor = map; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			base = cursor + 1;
	}
	return base;
}

static void json_string_or_null(
	struct command_output *output,
	char const *text)
{
	if (text && text[0])
		command_output_json_string(output, text);
	else
		command_output_printf(output, "null");
}

/* a path as the playlist's is compared ("./playlists\X.txt": playlists/x.txt) */
static void normalized_path(
	char const *path,
	char *out,
	long size)
{
	long length = 0;

	while (path[0] == '.' && (path[1] == '/' || path[1] == '\\'))
		path += 2;
	for (; *path && length < size - 1; path++)
		out[length++] = *path == '\\' ? '/' : (char)tolower((unsigned char)*path);
	out[length] = 0;
}

static boolean same_path(
	char const *a,
	char const *b)
{
	char normalized_a[ADMIN_PATH_SIZE], normalized_b[ADMIN_PATH_SIZE];

	normalized_path(a, normalized_a, sizeof(normalized_a));
	normalized_path(b, normalized_b, sizeof(normalized_b));
	return a[0] && b[0] && !strcmp(normalized_a, normalized_b);
}

/* ---------- settings */

boolean server_admin_read_settings(
	struct server_settings *settings)
{
	static char text[SERVER_SETTINGS_TEXT_SIZE + 1];
	char problem[SERVER_CONFIG_ERROR_SIZE + 64];
	char parse_error[SERVER_CONFIG_ERROR_SIZE];
	long length;
	int result;

	csmemset(settings, 0, sizeof(*settings));
	result = read_text(SETTINGS_FILE, text, SERVER_SETTINGS_TEXT_SIZE, &length, problem, sizeof(problem));
	if (result < 0)
		return TRUE;
	if (!result)
	{
		error(_error_silent, "dedicated: %s; the settings file is left out", problem);
		return FALSE;
	}
	if (!server_settings_parse(text, (size_t)length, settings, parse_error, sizeof(parse_error)))
	{
		error(_error_silent, "dedicated: %s, %s; the settings file is left out until it is fixed", SETTINGS_FILE,
			parse_error);
		return FALSE;
	}
	error(_error_silent, "dedicated: settings from %s", SETTINGS_FILE);
	return TRUE;
}

/* the settings file changed by change (one setting given in settings is
taken into the file's): TRUE, else FALSE and why in the output */
static boolean write_settings(
	struct server_settings const *change,
	struct command_output *output)
{
	static struct server_settings settings;
	static char text[SERVER_SETTINGS_TEXT_SIZE + 1];
	char parse_error[SERVER_CONFIG_ERROR_SIZE];
	long length;
	int result;
	short setting;

	result = read_text(SETTINGS_FILE, text, SERVER_SETTINGS_TEXT_SIZE, &length, admin_problem, sizeof(admin_problem));
	csmemset(&settings, 0, sizeof(settings));
	if (!result)
	{
		command_output_printf(output, "%s\n", admin_problem);
		return FALSE;
	}
	if (result > 0 && !server_settings_parse(text, (size_t)length, &settings, parse_error, sizeof(parse_error)))
	{
		command_output_printf(output, "%s, %s: fix or delete it first (it is not changed)\n", SETTINGS_FILE,
			parse_error);
		return FALSE;
	}
	for (setting = 0; setting < SERVER_SETTINGS; setting++)
	{
		if (!change->set[setting])
			continue;
		settings.set[setting] = 1;
		settings.values[setting] = change->values[setting];
	}
	if (change->set[SERVER_SETTING_NAME])
		csstrcpy(settings.name, change->name);
	if (change->set[SERVER_SETTING_PLAYLIST])
		csstrcpy(settings.playlist, change->playlist);
	if (change->set[SERVER_SETTING_PLAYLIST_ENVIRONMENT])
		csstrcpy(settings.playlist_environment, change->playlist_environment);
	if (server_settings_format(&settings, text, sizeof(text)) < 0)
	{
		command_output_printf(output, "the settings do not fit in %s\n", SETTINGS_FILE);
		return FALSE;
	}
	if (!write_text(ADMIN_FOLDER, SETTINGS_FILE, text, admin_problem, sizeof(admin_problem)))
	{
		command_output_printf(output, "%s\n", admin_problem);
		return FALSE;
	}
	return TRUE;
}

/* the playlist played kept in the settings file, for the next start: TRUE,
else FALSE and why in the output */
static boolean keep_playlist(
	char const *path,
	struct command_output *output)
{
	static struct server_settings change;
	char const *environment = getenv("HALO_DEDICATED");

	csmemset(&change, 0, sizeof(change));
	change.set[SERVER_SETTING_PLAYLIST] = 1;
	change.set[SERVER_SETTING_PLAYLIST_ENVIRONMENT] = 1;
	snprintf(change.playlist, sizeof(change.playlist), "%s", path);
	snprintf(change.playlist_environment, sizeof(change.playlist_environment), "%s", environment ? environment : "");
	return write_settings(&change, output);
}

/* ---------- game types */

/* whether a game type's file reads right: its settings in gametype, else
why in problem */
static boolean gametype_read(
	char const *name,
	struct server_gametype *gametype,
	char *problem,
	long problem_size)
{
	static char text[SERVER_GAMETYPE_TEXT_SIZE + 1];
	char path[ADMIN_PATH_SIZE];
	char parse_error[SERVER_CONFIG_ERROR_SIZE];
	long length;
	int result;

	snprintf(path, sizeof(path), "%s/%s.toml", GAMETYPES_FOLDER, name);
	result = read_text(path, text, SERVER_GAMETYPE_TEXT_SIZE, &length, problem, problem_size);
	if (result < 0)
	{
		snprintf(problem, (size_t)problem_size, "no game type %s (a built-in, or %s)", name, path);
		return FALSE;
	}
	if (!result)
		return FALSE;
	if (!server_gametype_parse(text, (size_t)length, gametype, parse_error, sizeof(parse_error)))
	{
		snprintf(problem, (size_t)problem_size, "%s, %s", path, parse_error);
		return FALSE;
	}
	return TRUE;
}

static void set_variant_flag(
	struct game_variant *variant,
	long bit,
	boolean value)
{
	SET_FLAG(variant->universal_variant.flags, bit, value);
}

/* a game type's settings given to its base's variant and PC options, as
the game's gametype editor gives them (port/linux/game/menu_functions.c) */
static void gametype_apply(
	struct server_gametype const *gametype,
	struct game_variant *variant,
	struct game_variant_options *options)
{
	struct universal_variant *universal = &variant->universal_variant;
	union game_engine_variant *engine = &variant->game_engine_variant;
	short field;

	for (field = 0; field < SERVER_GAMETYPE_FIELDS; field++)
	{
		long value = gametype->values[field];

		if (!gametype->set[field])
			continue;
		switch (field)
		{
		case SERVER_GAMETYPE_SCORE_LIMIT: universal->score_to_win = value; break;
		case SERVER_GAMETYPE_TIME_LIMIT: options->time_limit = (short)value; break;
		case SERVER_GAMETYPE_TEAMS: universal->teams = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_LIVES: universal->lives = value; break;
		case SERVER_GAMETYPE_HEALTH: universal->health = (real)value / 100.0f; break;
		case SERVER_GAMETYPE_SHIELDS: set_variant_flag(variant, _game_variant_no_shields_bit, !value); break;
		case SERVER_GAMETYPE_RESPAWN_TIME: universal->respawn_time = value * ADMIN_TICKS_PER_SECOND; break;
		case SERVER_GAMETYPE_RESPAWN_GROWTH: universal->respawn_time_growth = value * ADMIN_TICKS_PER_SECOND; break;
		case SERVER_GAMETYPE_SUICIDE_PENALTY: universal->suicide_penalty = value * ADMIN_TICKS_PER_SECOND; break;
		case SERVER_GAMETYPE_ODD_MAN_OUT: universal->odd_man_out = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_INVISIBLE: set_variant_flag(variant, _game_variant_always_invisible_bit, value != 0); break;
		case SERVER_GAMETYPE_WEAPON_SET: universal->weapon_set = value; break;
		case SERVER_GAMETYPE_STARTING_EQUIPMENT:
			set_variant_flag(variant, _game_variant_generic_starting_equipment_bit, value != 0);
			break;
		case SERVER_GAMETYPE_MAP_WEAPONS: options->no_map_weapons = value ? FALSE : TRUE; break;
		case SERVER_GAMETYPE_INFINITE_GRENADES:
			set_variant_flag(variant, _game_variant_infinite_grenades_bit, value != 0);
			break;
		case SERVER_GAMETYPE_LOADOUT: options->loadout = (byte)value; break;
		case SERVER_GAMETYPE_PRIMARY_WEAPON: options->primary_weapon = (byte)value; break;
		case SERVER_GAMETYPE_SECONDARY_WEAPON: options->secondary_weapon = (byte)value; break;
		case SERVER_GAMETYPE_OBJECTIVES: universal->goal_radar = value; break;
		case SERVER_GAMETYPE_PLAYERS_ON_RADAR:
			/* (and the Xbox's flag: other players on the tracker or not) */
			options->radar_players = (byte)value;
			set_variant_flag(variant, _game_variant_draw_object_in_motion_sensor_bit, value != _radar_players_none);
			break;
		case SERVER_GAMETYPE_FRIENDS_ON_SCREEN:
			set_variant_flag(variant, _game_variant_allow_friendly_navpoints_bit, value != 0);
			break;
		case SERVER_GAMETYPE_FRIENDLY_FIRE: options->friendly_fire = (short)value; break;
		case SERVER_GAMETYPE_FRIENDLY_FIRE_PENALTY: options->friendly_fire_penalty = (short)value; break;
		case SERVER_GAMETYPE_AUTO_BALANCE: options->auto_team_balance = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_VEHICLE_RESPAWN: options->vehicle_respawn_time = (short)value; break;
		case SERVER_GAMETYPE_RED_VEHICLES: options->vehicle_set[0] = (byte)value; break;
		case SERVER_GAMETYPE_BLUE_VEHICLES: options->vehicle_set[1] = (byte)value; break;
		case SERVER_GAMETYPE_CTF_ASSAULT: engine->ctf.assault = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_CTF_SINGLE_FLAG_TIME:
			engine->ctf.single_flag_time = value * ADMIN_TICKS_PER_SECOND;
			break;
		case SERVER_GAMETYPE_CTF_FLAG_MUST_RESET: engine->ctf.flag_must_reset = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_CTF_FLAG_AT_HOME_TO_SCORE: engine->ctf.flag_at_home_to_score = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_CTF_RESET_ON_CAPTURE: engine->ctf.reset_on_capture = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_SLAYER_DEATH_BONUS: engine->slayer.no_death_bonus = value ? FALSE : TRUE; break;
		case SERVER_GAMETYPE_SLAYER_KILL_PENALTY: engine->slayer.no_kill_penalty = value ? FALSE : TRUE; break;
		case SERVER_GAMETYPE_SLAYER_KILL_IN_ORDER: engine->slayer.kill_in_order = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_KING_MOVING_HILL: engine->king.moving_hill = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_ODDBALL_RANDOM_START: engine->oddball.random_start = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_ODDBALL_SPAWN_DELAY: engine->oddball.ball_spawn_delay = value ? TRUE : FALSE; break;
		case SERVER_GAMETYPE_ODDBALL_SPEED_WITH_BALL: engine->oddball.speed_with_ball = value; break;
		case SERVER_GAMETYPE_ODDBALL_TRAIT_WITH_BALL: engine->oddball.trait_with_ball = value; break;
		case SERVER_GAMETYPE_ODDBALL_TRAIT_WITHOUT_BALL: engine->oddball.trait_without_ball = value; break;
		case SERVER_GAMETYPE_ODDBALL_BALL_TYPE: engine->oddball.oddball_ball_type = value; break;
		case SERVER_GAMETYPE_ODDBALL_BALLS: engine->oddball.ball_spawn_count = value; break;
		case SERVER_GAMETYPE_RACE_TYPE: engine->race.race_type = value; break;
		case SERVER_GAMETYPE_RACE_TEAM_SCORING: engine->race.team_scoring = value; break;
		default:
			if (field >= SERVER_GAMETYPE_RED_WARTHOG && field <= SERVER_GAMETYPE_RED_GUN_TURRET)
				options->vehicle_counts[0][field - SERVER_GAMETYPE_RED_WARTHOG] = (byte)value;
			else if (field >= SERVER_GAMETYPE_BLUE_WARTHOG && field <= SERVER_GAMETYPE_BLUE_GUN_TURRET)
				options->vehicle_counts[1][field - SERVER_GAMETYPE_BLUE_WARTHOG] = (byte)value;
			break;
		}
	}
	/* (the Xbox's vehicle set: red's, if it is one of its sets, as the
	editor keeps it) */
	universal->vehicle_set = options->vehicle_set[0] <= 4 ? options->vehicle_set[0] : 0;
	/* its name in the game */
	{
		long index;

		for (index = 0; index < (long)NUMBEROF(variant->human_readable_game_description) - 1 && gametype->title[index];
			index++)
		{
			variant->human_readable_game_description[index] = (wchar_t)(unsigned char)gametype->title[index];
		}
		for (; index < (long)NUMBEROF(variant->human_readable_game_description); index++)
			variant->human_readable_game_description[index] = 0;
	}
}

/* a setting's value in a variant and its PC options (as a file gives it) */
static long gametype_value(
	short field,
	struct game_variant const *variant,
	struct game_variant_options const *options)
{
	struct universal_variant const *universal = &variant->universal_variant;
	union game_engine_variant const *engine = &variant->game_engine_variant;
	unsigned long flags = universal->flags;

	switch (field)
	{
	case SERVER_GAMETYPE_SCORE_LIMIT: return universal->score_to_win;
	case SERVER_GAMETYPE_TIME_LIMIT: return options->time_limit;
	case SERVER_GAMETYPE_TEAMS: return universal->teams != 0;
	case SERVER_GAMETYPE_LIVES: return universal->lives;
	case SERVER_GAMETYPE_HEALTH: return (long)(universal->health * 100.0f + 0.5f);
	case SERVER_GAMETYPE_SHIELDS: return !TEST_FLAG(flags, _game_variant_no_shields_bit);
	case SERVER_GAMETYPE_RESPAWN_TIME: return universal->respawn_time / ADMIN_TICKS_PER_SECOND;
	case SERVER_GAMETYPE_RESPAWN_GROWTH: return universal->respawn_time_growth / ADMIN_TICKS_PER_SECOND;
	case SERVER_GAMETYPE_SUICIDE_PENALTY: return universal->suicide_penalty / ADMIN_TICKS_PER_SECOND;
	case SERVER_GAMETYPE_ODD_MAN_OUT: return universal->odd_man_out != 0;
	case SERVER_GAMETYPE_INVISIBLE: return TEST_FLAG(flags, _game_variant_always_invisible_bit);
	case SERVER_GAMETYPE_WEAPON_SET: return universal->weapon_set;
	case SERVER_GAMETYPE_STARTING_EQUIPMENT: return TEST_FLAG(flags, _game_variant_generic_starting_equipment_bit);
	case SERVER_GAMETYPE_MAP_WEAPONS: return !options->no_map_weapons;
	case SERVER_GAMETYPE_INFINITE_GRENADES: return TEST_FLAG(flags, _game_variant_infinite_grenades_bit);
	case SERVER_GAMETYPE_LOADOUT: return options->loadout;
	case SERVER_GAMETYPE_PRIMARY_WEAPON: return options->primary_weapon;
	case SERVER_GAMETYPE_SECONDARY_WEAPON: return options->secondary_weapon;
	case SERVER_GAMETYPE_OBJECTIVES: return universal->goal_radar;
	case SERVER_GAMETYPE_PLAYERS_ON_RADAR: return options->radar_players;
	case SERVER_GAMETYPE_FRIENDS_ON_SCREEN: return TEST_FLAG(flags, _game_variant_allow_friendly_navpoints_bit);
	case SERVER_GAMETYPE_FRIENDLY_FIRE: return options->friendly_fire;
	case SERVER_GAMETYPE_FRIENDLY_FIRE_PENALTY: return options->friendly_fire_penalty;
	case SERVER_GAMETYPE_AUTO_BALANCE: return options->auto_team_balance != 0;
	case SERVER_GAMETYPE_VEHICLE_RESPAWN: return options->vehicle_respawn_time;
	case SERVER_GAMETYPE_RED_VEHICLES: return options->vehicle_set[0];
	case SERVER_GAMETYPE_BLUE_VEHICLES: return options->vehicle_set[1];
	case SERVER_GAMETYPE_CTF_ASSAULT: return engine->ctf.assault != 0;
	case SERVER_GAMETYPE_CTF_SINGLE_FLAG_TIME: return engine->ctf.single_flag_time / ADMIN_TICKS_PER_SECOND;
	case SERVER_GAMETYPE_CTF_FLAG_MUST_RESET: return engine->ctf.flag_must_reset != 0;
	case SERVER_GAMETYPE_CTF_FLAG_AT_HOME_TO_SCORE: return engine->ctf.flag_at_home_to_score != 0;
	case SERVER_GAMETYPE_CTF_RESET_ON_CAPTURE: return engine->ctf.reset_on_capture != 0;
	case SERVER_GAMETYPE_SLAYER_DEATH_BONUS: return !engine->slayer.no_death_bonus;
	case SERVER_GAMETYPE_SLAYER_KILL_PENALTY: return !engine->slayer.no_kill_penalty;
	case SERVER_GAMETYPE_SLAYER_KILL_IN_ORDER: return engine->slayer.kill_in_order != 0;
	case SERVER_GAMETYPE_KING_MOVING_HILL: return engine->king.moving_hill != 0;
	case SERVER_GAMETYPE_ODDBALL_RANDOM_START: return engine->oddball.random_start != 0;
	case SERVER_GAMETYPE_ODDBALL_SPAWN_DELAY: return engine->oddball.ball_spawn_delay != 0;
	case SERVER_GAMETYPE_ODDBALL_SPEED_WITH_BALL: return engine->oddball.speed_with_ball;
	case SERVER_GAMETYPE_ODDBALL_TRAIT_WITH_BALL: return engine->oddball.trait_with_ball;
	case SERVER_GAMETYPE_ODDBALL_TRAIT_WITHOUT_BALL: return engine->oddball.trait_without_ball;
	case SERVER_GAMETYPE_ODDBALL_BALL_TYPE: return engine->oddball.oddball_ball_type;
	case SERVER_GAMETYPE_ODDBALL_BALLS: return engine->oddball.ball_spawn_count;
	case SERVER_GAMETYPE_RACE_TYPE: return engine->race.race_type;
	case SERVER_GAMETYPE_RACE_TEAM_SCORING: return engine->race.team_scoring;
	}
	if (field >= SERVER_GAMETYPE_RED_WARTHOG && field <= SERVER_GAMETYPE_RED_GUN_TURRET)
		return options->vehicle_counts[0][field - SERVER_GAMETYPE_RED_WARTHOG];
	if (field >= SERVER_GAMETYPE_BLUE_WARTHOG && field <= SERVER_GAMETYPE_BLUE_GUN_TURRET)
		return options->vehicle_counts[1][field - SERVER_GAMETYPE_BLUE_WARTHOG];
	return 0;
}

boolean server_gametype_resolve(
	char const *name,
	struct game_variant *variant,
	struct game_variant_options *options,
	char *problem,
	long problem_size)
{
	static struct server_gametype gametype;
	int builtin = server_builtin_find(name);

	if (problem_size > 0)
		problem[0] = 0;
	if (builtin >= 0)
	{
		game_engine_get_variant_by_name(variant, name);
		game_variant_options_default(variant, options);
		return TRUE;
	}
	if (!server_config_name_valid(name))
	{
		snprintf(problem, (size_t)problem_size, "no game type %s (slayer, ctf, ..., or a game type file's name)", name);
		return FALSE;
	}
	if (!gametype_read(name, &gametype, problem, problem_size))
		return FALSE;
	game_engine_get_variant_by_name(variant, server_builtins[gametype.values[SERVER_GAMETYPE_BASE]].name);
	game_variant_options_default(variant, options);
	gametype_apply(&gametype, variant, options);
	return TRUE;
}

/* a game type's engine, as a file's base gives it (0: none) */
static int gametype_engine(
	struct server_gametype const *gametype)
{
	return server_builtins[gametype->values[SERVER_GAMETYPE_BASE]].engine;
}

/* ---------- playlists */

/* a playlist by its name: admin/playlists' file, else playlists'; FALSE if
neither is there */
static boolean playlist_find(
	char const *name,
	struct playlist_file *found)
{
	char admin_path[ADMIN_PATH_SIZE], shipped_path[ADMIN_PATH_SIZE];

	csmemset(found, 0, sizeof(*found));
	if (!server_config_name_valid(name))
		return FALSE;
	snprintf(found->name, sizeof(found->name), "%s", name);
	snprintf(admin_path, sizeof(admin_path), "%s/%s.txt", PLAYLISTS_FOLDER, name);
	snprintf(shipped_path, sizeof(shipped_path), "%s/%s.txt", SHIPPED_PLAYLISTS_FOLDER, name);
	found->admin = file_exists(admin_path);
	found->shipped = file_exists(shipped_path);
	snprintf(found->path, sizeof(found->path), "%s", found->admin ? admin_path : shipped_path);
	return found->admin || found->shipped;
}

/* the name of the playlist a path is ("playlists/x.txt", "admin/playlists/x.txt"),
or empty */
static void playlist_name_of(
	char const *path,
	char *name,
	long size)
{
	char normalized[ADMIN_PATH_SIZE];
	char const *rest = NULL;
	size_t length;

	name[0] = 0;
	normalized_path(path, normalized, sizeof(normalized));
	if (!strncmp(normalized, PLAYLISTS_FOLDER "/", strlen(PLAYLISTS_FOLDER) + 1))
		rest = normalized + strlen(PLAYLISTS_FOLDER) + 1;
	else if (!strncmp(normalized, SHIPPED_PLAYLISTS_FOLDER "/", strlen(SHIPPED_PLAYLISTS_FOLDER) + 1))
		rest = normalized + strlen(SHIPPED_PLAYLISTS_FOLDER) + 1;
	if (!rest)
		return;
	length = strlen(rest);
	if (length <= 4 || strcmp(rest + length - 4, ".txt") || length - 4 >= (size_t)size)
		return;
	snprintf(name, (size_t)size, "%.*s", (int)(length - 4), rest);
	if (!server_config_name_valid(name))
		name[0] = 0;
}

/* a playlist's entries (as the server reads it), their maps as names: FALSE,
with why in the output, if it cannot be read */
static boolean playlist_read(
	struct playlist_file const *file,
	struct server_playlist *playlist,
	struct command_output *output)
{
	long length;
	int result = read_text(file->path, admin_text, SERVER_PLAYLIST_TEXT_SIZE, &length, admin_problem,
		sizeof(admin_problem));
	long index;

	if (result <= 0)
	{
		command_output_printf(output, "%s\n", result < 0 ? "the playlist is gone" : admin_problem);
		return FALSE;
	}
	server_playlist_parse(admin_text, (size_t)length, FALSE, playlist, NULL, 0);
	for (index = 0; index < playlist->count; index++)
	{
		char map[SERVER_PLAYLIST_MAP_SIZE];

		snprintf(map, sizeof(map), "%s", map_display_name(playlist->entries[index].map));
		csstrcpy(playlist->entries[index].map, map);
	}
	return TRUE;
}

/* why a playlist's entry cannot be played (empty if it can) */
static void entry_problem(
	struct server_playlist_entry const *entry,
	char *problem,
	long size)
{
	static char scratch[256];
	struct command_output output;
	struct game_variant variant;
	struct game_variant_options options;

	problem[0] = 0;
	command_output_begin(&output, scratch, sizeof(scratch));
	if (!command_line_map_name_valid(entry->map) || !server_map_playable(entry->map, &output))
	{
		snprintf(problem, (size_t)size, "%s", scratch[0] ? scratch : "not a map's name");
		if (problem[0] && problem[strlen(problem) - 1] == '\n')
			problem[strlen(problem) - 1] = 0;
		return;
	}
	if (!server_gametype_resolve(entry->game_type, &variant, &options, problem, size))
		return;
}

/* the playlist played: its name (empty if it is none of the folders'), and
whether name's file is the one */
static boolean playlist_is_playing(
	struct playlist_file const *file)
{
	struct dedicated_status status;

	dedicated_server_get_status(&status);
	return same_path(status.playlist, file->path) || same_path(status.next_playlist, file->path);
}

/* a playlist checked and saved as admin/playlists/<name>.txt; played from
the next game if it is the one played (or saved over the shipped one of
its name that is played). TRUE, else FALSE and why in the output */
static boolean playlist_store(
	char const *name,
	struct server_playlist const *playlist,
	struct command_output *output)
{
	static struct server_playlist check;
	struct playlist_file before;
	struct dedicated_status status;
	char path[ADMIN_PATH_SIZE];
	char parse_error[SERVER_CONFIG_ERROR_SIZE];
	boolean existed = playlist_find(name, &before);
	boolean playing;
	long index;

	for (index = 0; index < playlist->count; index++)
	{
		char problem[SERVER_CONFIG_ERROR_SIZE + 64];

		entry_problem(&playlist->entries[index], problem, sizeof(problem));
		if (problem[0])
		{
			command_output_printf(output, "entry %ld (%s %s): %s: nothing saved\n", index + 1,
				playlist->entries[index].map, playlist->entries[index].game_type, problem);
			return FALSE;
		}
	}
	if (server_playlist_format(playlist, name, admin_text, sizeof(admin_text)) < 0 ||
		!server_playlist_parse(admin_text, strlen(admin_text), TRUE, &check, parse_error, sizeof(parse_error)))
	{
		command_output_printf(output, "the playlist is not one the server can save: nothing saved\n");
		return FALSE;
	}
	snprintf(path, sizeof(path), "%s/%s.txt", PLAYLISTS_FOLDER, name);
	dedicated_server_get_status(&status);
	playing = existed && (same_path(status.playlist, before.path) || same_path(status.next_playlist, before.path) ||
		same_path(status.playlist, path));
	if (!write_text(PLAYLISTS_FOLDER, path, admin_text, admin_problem, sizeof(admin_problem)))
	{
		command_output_printf(output, "%s\n", admin_problem);
		return FALSE;
	}
	error(_error_silent, "dedicated: saved %s (%ld games)", path, (long)playlist->count);
	command_output_printf(output, "saved %s (%ld game%s)\n", path, (long)playlist->count, playlist->count == 1 ? "" :
		"s");
	if (existed && !before.admin)
	{
		command_output_printf(output, "it is used by the name %s from now on, instead of %s, which is left as it is\n",
			name, before.path);
	}
	if (playing)
	{
		char problem[SERVER_CONFIG_ERROR_SIZE + 64];

		if (!playlist->count)
			command_output_printf(output, "(it has no games: the server goes on with the one it has)\n");
		else if (dedicated_server_use_playlist(path, TRUE, problem, sizeof(problem)))
		{
			command_output_printf(output, "the server plays it from the next game\n");
			if (!same_path(status.playlist, path) && !keep_playlist(path, output))
				command_output_printf(output, "(the server plays it until it restarts)\n");
		}
		else
			command_output_printf(output, "the server cannot play it: %s\n", problem);
	}
	return TRUE;
}

/* a playlist to change, by name: FALSE, with why in the output, if none */
static boolean playlist_for_edit(
	char const *name,
	struct playlist_file *file,
	struct server_playlist *playlist,
	struct command_output *output)
{
	if (!server_config_name_valid(name))
	{
		command_output_printf(output, "%s is not a playlist's name (a-z, 0-9, _ and -, 31 at most)\n", name);
		return FALSE;
	}
	if (!playlist_find(name, file))
	{
		command_output_printf(output, "no playlist %s (sv_playlists lists them; sv_playlist_new makes one)\n", name);
		return FALSE;
	}
	return playlist_read(file, playlist, output);
}

/* the playlist played, to change (sv_mapcycle_add, sv_mapcycle_del): its
name, else FALSE with why */
static boolean playing_playlist_name(
	char *name,
	long size,
	struct command_output *output)
{
	struct dedicated_status status;

	dedicated_server_get_status(&status);
	playlist_name_of(status.next_playlist[0] ? status.next_playlist : status.playlist, name, size);
	if (!name[0])
	{
		command_output_printf(output, "the playlist %s is in neither playlists/ nor %s: edit it by hand, or "
			"sv_playlist_use one of those\n", status.playlist, PLAYLISTS_FOLDER);
		return FALSE;
	}
	return TRUE;
}

/* an entry's number (1 to maximum) from a command's word: FALSE if it is
none */
static boolean entry_number(
	char const *text,
	long maximum,
	long *number,
	struct command_output *output)
{
	if (maximum < 1 || !command_line_integer(text, 1, maximum, number))
	{
		if (maximum < 1)
			command_output_printf(output, "the playlist has no games\n");
		else
			command_output_printf(output, "%s is not an entry's number, 1 to %ld\n", text, maximum);
		return FALSE;
	}
	return TRUE;
}

/* ---------- the commands: playlists */

boolean server_admin_playlists(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct dedicated_status status;
	long admin_index = 0, shipped_index = 0;
	long count = 0;

	(void)line;
	dedicated_server_get_status(&status);
	list_folder(PLAYLISTS_FOLDER, ".txt", &admin_names);
	list_folder(SHIPPED_PLAYLISTS_FOLDER, ".txt", &admin_shipped_names);
	if (json)
		command_output_printf(output, "{\"playlists\": [");
	else
	{
		command_output_printf(output, "playlists (%s: made here; %s: the server's own, never written):\n",
			PLAYLISTS_FOLDER, SHIPPED_PLAYLISTS_FOLDER);
	}
	/* (both folders' names, in order, once each) */
	while (admin_index < admin_names.count || shipped_index < admin_shipped_names.count)
	{
		char const *name;
		struct playlist_file file;
		long entries = 0;
		boolean active, next;

		if (shipped_index >= admin_shipped_names.count || (admin_index < admin_names.count &&
			strcmp(admin_names.names[admin_index], admin_shipped_names.names[shipped_index]) <= 0))
		{
			name = admin_names.names[admin_index++];
			if (shipped_index < admin_shipped_names.count && !strcmp(name, admin_shipped_names.names[shipped_index]))
				shipped_index++;
		}
		else
			name = admin_shipped_names.names[shipped_index++];
		if (!playlist_find(name, &file))
			continue;
		{
			long length;

			if (read_text(file.path, admin_text, SERVER_PLAYLIST_TEXT_SIZE, &length, admin_problem,
				sizeof(admin_problem)) > 0)
			{
				server_playlist_parse(admin_text, (size_t)length, FALSE, &admin_playlist, NULL, 0);
				entries = admin_playlist.count;
			}
		}
		active = same_path(status.playlist, file.path);
		next = same_path(status.next_playlist, file.path);
		if (json)
		{
			command_output_printf(output, "%s{\"name\": ", count ? ", " : "");
			command_output_json_string(output, name);
			command_output_printf(output, ", \"path\": ");
			command_output_json_string(output, file.path);
			command_output_printf(output, ", \"entries\": %ld, \"editable\": %s, \"shipped\": %s, \"active\": %s, "
				"\"next\": %s}", entries, file.admin ? "true" : "false", file.shipped ? "true" : "false",
				active ? "true" : "false", next ? "true" : "false");
		}
		else
		{
			command_output_printf(output, "  %-24s %3ld game%s  %s%s%s\n", name, entries, entries == 1 ? " " : "s",
				file.path, active ? "  (playing)" : next ? "  (next)" : "",
				file.admin && file.shipped ? "  (instead of the server's own)" : "");
		}
		count++;
	}
	if (json)
	{
		command_output_printf(output, "], \"active\": ");
		command_output_json_string(output, status.playlist);
		command_output_printf(output, ", \"next\": ");
		json_string_or_null(output, status.next_playlist);
		command_output_printf(output, ", \"folder\": \"%s\"}", PLAYLISTS_FOLDER);
	}
	else
	{
		if (!count)
			command_output_printf(output, "  none\n");
		command_output_printf(output, "playing: %s%s%s\n", status.playlist, status.next_playlist[0] ?
			", then from the next game " : "", status.next_playlist);
	}
	return TRUE;
}

boolean server_admin_playlist(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct playlist_file file;
	long index;
	boolean active;

	if (!playlist_for_edit(line->words[1], &file, &admin_playlist, output))
	{
		if (json)
		{
			output->length = 0;
			output->text[0] = 0;
			command_output_printf(output, "{\"error\": \"no playlist %s\"}", server_config_name_valid(line->words[1]) ?
				line->words[1] : "by that name");
		}
		return FALSE;
	}
	active = playlist_is_playing(&file);
	if (json)
	{
		command_output_printf(output, "{\"name\": ");
		command_output_json_string(output, file.name);
		command_output_printf(output, ", \"path\": ");
		command_output_json_string(output, file.path);
		command_output_printf(output, ", \"editable\": %s, \"shipped\": %s, \"active\": %s, \"entries\": [",
			file.admin ? "true" : "false", file.shipped ? "true" : "false", active ? "true" : "false");
	}
	else
	{
		command_output_printf(output, "%s (%s%s%s):\n", file.name, file.path, file.admin ? "" :
			", the server's own: a change is saved in " PLAYLISTS_FOLDER, active ? ", playing" : "");
	}
	for (index = 0; index < admin_playlist.count; index++)
	{
		char problem[SERVER_CONFIG_ERROR_SIZE + 64];
		struct server_playlist_entry const *entry = &admin_playlist.entries[index];

		entry_problem(entry, problem, sizeof(problem));
		if (json)
		{
			command_output_printf(output, "%s{\"number\": %ld, \"map\": ", index ? ", " : "", index + 1);
			command_output_json_string(output, entry->map);
			command_output_printf(output, ", \"game_type\": ");
			command_output_json_string(output, entry->game_type);
			command_output_printf(output, ", \"problem\": ");
			json_string_or_null(output, problem);
			command_output_printf(output, "}");
		}
		else
		{
			command_output_printf(output, "%3ld  %-22s %-16s%s%s\n", index + 1, entry->map, entry->game_type,
				problem[0] ? "  ! " : "", problem);
		}
	}
	if (json)
		command_output_printf(output, "]}");
	else if (!admin_playlist.count)
		command_output_printf(output, "  no games (sv_playlist_add adds one)\n");
	return TRUE;
}

boolean server_admin_playlist_new(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct playlist_file file;
	char const *name = line->words[1];

	(void)json;
	if (!server_config_name_valid(name))
	{
		command_output_printf(output, "%s is not a playlist's name (a-z, 0-9, _ and -, 31 at most)\n", name);
		return FALSE;
	}
	if (playlist_find(name, &file))
	{
		command_output_printf(output, "there is a playlist %s already (%s)\n", name, file.path);
		return FALSE;
	}
	csmemset(&admin_playlist, 0, sizeof(admin_playlist));
	if (line->count == 3)
	{
		struct playlist_file from;

		if (!playlist_for_edit(line->words[2], &from, &admin_playlist, output))
			return FALSE;
	}
	return playlist_store(name, &admin_playlist, output);
}

boolean server_admin_playlist_add(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct playlist_file file;
	long position;
	long index;

	(void)json;
	if (!playlist_for_edit(line->words[1], &file, &admin_playlist, output))
		return FALSE;
	position = admin_playlist.count + 1;
	if (line->count == 5 && !entry_number(line->words[4], admin_playlist.count + 1, &position, output))
		return FALSE;
	if (admin_playlist.count >= SERVER_PLAYLIST_ENTRIES)
	{
		command_output_printf(output, "a playlist has %d games at most\n", (int)SERVER_PLAYLIST_ENTRIES);
		return FALSE;
	}
	if (!command_line_map_name_valid(line->words[2]))
	{
		command_output_printf(output, "%s is not a map's name (bloodgulch, name@ce, name@md)\n", line->words[2]);
		return FALSE;
	}
	if (strlen(line->words[3]) >= SERVER_PLAYLIST_TYPE_SIZE)
	{
		command_output_printf(output, "no game type %s\n", line->words[3]);
		return FALSE;
	}
	for (index = admin_playlist.count; index >= position; index--)
		admin_playlist.entries[index] = admin_playlist.entries[index - 1];
	snprintf(admin_playlist.entries[position - 1].map, sizeof(admin_playlist.entries[0].map), "%s", line->words[2]);
	snprintf(admin_playlist.entries[position - 1].game_type, sizeof(admin_playlist.entries[0].game_type), "%s",
		line->words[3]);
	admin_playlist.count++;
	return playlist_store(file.name, &admin_playlist, output);
}

boolean server_admin_playlist_remove(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct playlist_file file;
	long number;
	long index;

	(void)json;
	if (!playlist_for_edit(line->words[1], &file, &admin_playlist, output) ||
		!entry_number(line->words[2], admin_playlist.count, &number, output))
	{
		return FALSE;
	}
	for (index = number - 1; index < admin_playlist.count - 1; index++)
		admin_playlist.entries[index] = admin_playlist.entries[index + 1];
	admin_playlist.count--;
	return playlist_store(file.name, &admin_playlist, output);
}

boolean server_admin_playlist_move(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct playlist_file file;
	struct server_playlist_entry moved;
	long from, to;
	long index;

	(void)json;
	if (!playlist_for_edit(line->words[1], &file, &admin_playlist, output) ||
		!entry_number(line->words[2], admin_playlist.count, &from, output) ||
		!entry_number(line->words[3], admin_playlist.count, &to, output))
	{
		return FALSE;
	}
	moved = admin_playlist.entries[from - 1];
	if (from < to)
	{
		for (index = from - 1; index < to - 1; index++)
			admin_playlist.entries[index] = admin_playlist.entries[index + 1];
	}
	else
	{
		for (index = from - 1; index > to - 1; index--)
			admin_playlist.entries[index] = admin_playlist.entries[index - 1];
	}
	admin_playlist.entries[to - 1] = moved;
	return playlist_store(file.name, &admin_playlist, output);
}

boolean server_admin_playlist_delete(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct playlist_file file;
	char const *name = line->words[1];

	(void)json;
	if (!server_config_name_valid(name) || !playlist_find(name, &file))
	{
		command_output_printf(output, "no playlist %s\n", name);
		return FALSE;
	}
	if (!file.admin)
	{
		command_output_printf(output, "%s is the server's own: it is never deleted here (delete it by hand)\n",
			file.path);
		return FALSE;
	}
	if (playlist_is_playing(&file))
	{
		command_output_printf(output, "%s is the playlist played: sv_playlist_use another first\n", name);
		return FALSE;
	}
	if (!delete_file(file.path, admin_problem, sizeof(admin_problem)))
	{
		command_output_printf(output, "%s\n", admin_problem);
		return FALSE;
	}
	error(_error_silent, "dedicated: deleted %s", file.path);
	command_output_printf(output, "deleted %s%s\n", file.path, file.shipped ?
		" (the server's own playlist of that name is used again)" : "");
	return TRUE;
}

boolean server_admin_playlist_use(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct playlist_file file;
	struct dedicated_status status;
	char problem[SERVER_CONFIG_ERROR_SIZE + 64];
	char const *name = line->words[1];

	(void)json;
	if (!server_config_name_valid(name) || !playlist_find(name, &file))
	{
		command_output_printf(output, "no playlist %s (sv_playlists lists them)\n", name);
		return FALSE;
	}
	if (!dedicated_server_use_playlist(file.path, TRUE, problem, sizeof(problem)))
	{
		command_output_printf(output, "%s\n", problem);
		return FALSE;
	}
	dedicated_server_get_status(&status);
	error(_error_silent, "dedicated: a command plays the playlist %s", file.path);
	command_output_printf(output, "the playlist is %s (%s), %s\n", name, file.path, status.next_playlist[0] ?
		"from the next game" : "now");
	if (keep_playlist(file.path, output))
		command_output_printf(output, "(kept when the server restarts, until HALO_DEDICATED is changed)\n");
	else
		command_output_printf(output, "(until the server restarts)\n");
	return TRUE;
}

boolean server_admin_playlist_save(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	char const *text = server_commands_payload();
	char parse_error[SERVER_CONFIG_ERROR_SIZE];

	(void)json;
	if (!text)
	{
		command_output_printf(output, "sv_playlist_save is the control API's (POST /v1/playlist); here, "
			"sv_playlist_new, sv_playlist_add, sv_playlist_remove and sv_playlist_move\n");
		return FALSE;
	}
	if (!server_config_name_valid(line->words[1]))
	{
		command_output_printf(output, "%s is not a playlist's name (a-z, 0-9, _ and -, 31 at most)\n", line->words[1]);
		return FALSE;
	}
	if (!server_playlist_parse(text, strlen(text), TRUE, &admin_playlist, parse_error, sizeof(parse_error)))
	{
		command_output_printf(output, "%s: nothing saved\n", parse_error);
		return FALSE;
	}
	return playlist_store(line->words[1], &admin_playlist, output);
}

boolean server_admin_mapcycle_add(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct playlist_file file;
	char name[SERVER_CONFIG_NAME_SIZE];

	(void)json;
	if (!playing_playlist_name(name, sizeof(name), output) || !playlist_for_edit(name, &file, &admin_playlist, output))
		return FALSE;
	if (admin_playlist.count >= SERVER_PLAYLIST_ENTRIES)
	{
		command_output_printf(output, "a playlist has %d games at most\n", (int)SERVER_PLAYLIST_ENTRIES);
		return FALSE;
	}
	if (!command_line_map_name_valid(line->words[1]) || strlen(line->words[2]) >= SERVER_PLAYLIST_TYPE_SIZE)
	{
		command_output_printf(output, "usage: sv_mapcycle_add <map> <game type>\n");
		return FALSE;
	}
	snprintf(admin_playlist.entries[admin_playlist.count].map, sizeof(admin_playlist.entries[0].map), "%s",
		line->words[1]);
	snprintf(admin_playlist.entries[admin_playlist.count].game_type, sizeof(admin_playlist.entries[0].game_type), "%s",
		line->words[2]);
	admin_playlist.count++;
	return playlist_store(name, &admin_playlist, output);
}

boolean server_admin_mapcycle_del(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	struct playlist_file file;
	char name[SERVER_CONFIG_NAME_SIZE];
	long number;
	long index;

	(void)json;
	if (!playing_playlist_name(name, sizeof(name), output) || !playlist_for_edit(name, &file, &admin_playlist, output) ||
		!entry_number(line->words[1], admin_playlist.count, &number, output))
	{
		return FALSE;
	}
	if (admin_playlist.count == 1)
	{
		command_output_printf(output, "it is the playlist's only game: the server needs one to play\n");
		return FALSE;
	}
	for (index = number - 1; index < admin_playlist.count - 1; index++)
		admin_playlist.entries[index] = admin_playlist.entries[index + 1];
	admin_playlist.count--;
	return playlist_store(name, &admin_playlist, output);
}

/* ---------- the commands: game types */

static char const *field_kind_name(
	int kind)
{
	switch (kind)
	{
	case SERVER_FIELD_INTEGER: return "integer";
	case SERVER_FIELD_BOOLEAN: return "boolean";
	case SERVER_FIELD_CHOICE: return "choice";
	}
	return "string";
}

boolean server_admin_gametypes(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	long index;

	(void)line;
	list_folder(GAMETYPES_FOLDER, ".toml", &admin_names);
	if (json)
	{
		command_output_printf(output, "{\"builtins\": [");
		for (index = 0; index < server_builtin_count; index++)
		{
			command_output_printf(output, "%s{\"name\": \"%s\", \"engine\": \"%s\"}", index ? ", " : "",
				server_builtins[index].name, server_engine_name(server_builtins[index].engine));
		}
		command_output_printf(output, "], \"gametypes\": [");
	}
	else
	{
		command_output_printf(output, "built in:");
		for (index = 0; index < server_builtin_count; index++)
			command_output_printf(output, " %s", server_builtins[index].name);
		command_output_printf(output, "\ngame type files (%s):\n", GAMETYPES_FOLDER);
	}
	for (index = 0; index < admin_names.count; index++)
	{
		char const *name = admin_names.names[index];
		char problem[SERVER_CONFIG_ERROR_SIZE + 64];
		boolean good = server_builtin_find(name) < 0 && gametype_read(name, &admin_gametype, problem, sizeof(problem));

		if (server_builtin_find(name) >= 0)
			snprintf(problem, sizeof(problem), "%s is a built-in's name: the built-in is played", name);
		if (json)
		{
			command_output_printf(output, "%s{\"name\": ", index ? ", " : "");
			command_output_json_string(output, name);
			command_output_printf(output, ", \"path\": \"%s/%s.toml\", \"title\": ", GAMETYPES_FOLDER, name);
			json_string_or_null(output, good ? admin_gametype.title : NULL);
			command_output_printf(output, ", \"base\": ");
			json_string_or_null(output, good ? server_builtins[admin_gametype.values[SERVER_GAMETYPE_BASE]].name : NULL);
			command_output_printf(output, ", \"engine\": ");
			json_string_or_null(output, good ? server_engine_name(gametype_engine(&admin_gametype)) : NULL);
			command_output_printf(output, ", \"problem\": ");
			json_string_or_null(output, good ? NULL : problem);
			command_output_printf(output, "}");
		}
		else if (good)
		{
			command_output_printf(output, "  %-24s %s%s%s%s\n", name,
				server_builtins[admin_gametype.values[SERVER_GAMETYPE_BASE]].name, admin_gametype.title[0] ? " (\"" : "",
				admin_gametype.title, admin_gametype.title[0] ? "\")" : "");
		}
		else
			command_output_printf(output, "  %-24s ! %s\n", name, problem);
	}
	if (json)
	{
		command_output_printf(output, "], \"folder\": \"%s\", \"fields\": [", GAMETYPES_FOLDER);
		for (index = 0; index < SERVER_GAMETYPE_FIELDS; index++)
		{
			struct server_gametype_field const *field = &server_gametype_fields[index];

			command_output_printf(output, "%s{\"key\": \"%s\", \"kind\": \"%s\", \"minimum\": %d, \"maximum\": %d, "
				"\"engine\": ", index ? ", " : "", field->key, field_kind_name(field->kind), field->minimum,
				field->maximum);
			if (field->engine)
				command_output_printf(output, "\"%s\"", server_engine_name(field->engine));
			else
				command_output_printf(output, "null");
			command_output_printf(output, ", \"label\": ");
			command_output_json_string(output, field->label);
			command_output_printf(output, ", \"unit\": ");
			json_string_or_null(output, field->unit);
			command_output_printf(output, ", \"choices\": [");
			{
				int choice;

				for (choice = 0; choice < field->choice_count; choice++)
				{
					command_output_printf(output, "%s\"%s\"", choice ? ", " : "", field->choices[choice].name);
				}
			}
			command_output_printf(output, "]}");
		}
		command_output_printf(output, "]}");
	}
	else if (!admin_names.count)
		command_output_printf(output, "  none (sv_gametype_new makes one)\n");
	return TRUE;
}

boolean server_admin_gametype(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	char const *name = line->words[1];
	struct game_variant variant;
	struct game_variant_options options;
	char problem[SERVER_CONFIG_ERROR_SIZE + 64];
	int builtin = server_builtin_find(name);
	int engine;
	short field;
	boolean first = TRUE;

	csmemset(&admin_gametype, 0, sizeof(admin_gametype));
	if (builtin < 0 && (!server_config_name_valid(name) || !gametype_read(name, &admin_gametype, problem,
		sizeof(problem))))
	{
		if (!server_config_name_valid(name))
			snprintf(problem, sizeof(problem), "no game type by that name");
		if (json)
		{
			command_output_printf(output, "{\"error\": ");
			command_output_json_string(output, problem);
			command_output_printf(output, "}");
		}
		else
			command_output_printf(output, "%s\n", problem);
		return FALSE;
	}
	server_gametype_resolve(name, &variant, &options, problem, sizeof(problem));
	if (builtin >= 0)
	{
		admin_gametype.values[SERVER_GAMETYPE_BASE] = builtin;
		engine = server_builtins[builtin].engine;
	}
	else
		engine = gametype_engine(&admin_gametype);
	if (json)
	{
		command_output_printf(output, "{\"name\": ");
		command_output_json_string(output, name);
		command_output_printf(output, ", \"builtin\": %s, \"path\": ", builtin >= 0 ? "true" : "false");
		if (builtin >= 0)
			command_output_printf(output, "null");
		else
			command_output_printf(output, "\"%s/%s.toml\"", GAMETYPES_FOLDER, name);
		command_output_printf(output, ", \"title\": ");
		command_output_json_string(output, admin_gametype.title);
		command_output_printf(output, ", \"base\": \"%s\", \"engine\": \"%s\", \"values\": {",
			server_builtins[admin_gametype.values[SERVER_GAMETYPE_BASE]].name, server_engine_name(engine));
	}
	else
	{
		command_output_printf(output, "%s: %s%s%s (%s)\n", name, builtin >= 0 ? "built in" : "based on ",
			builtin >= 0 ? "" : server_builtins[admin_gametype.values[SERVER_GAMETYPE_BASE]].name,
			admin_gametype.title[0] ? "" : "", builtin >= 0 ? "its settings" : "* its file's settings, the rest its base's");
		if (admin_gametype.title[0])
			command_output_printf(output, "  name = \"%s\"\n", admin_gametype.title);
	}
	for (field = SERVER_GAMETYPE_SCORE_LIMIT; field < SERVER_GAMETYPE_FIELDS; field++)
	{
		char value[48];

		if (server_gametype_fields[field].engine && server_gametype_fields[field].engine != engine)
			continue;
		server_gametype_value_text(field, gametype_value(field, &variant, &options), value, sizeof(value));
		if (json)
		{
			command_output_printf(output, "%s\"%s\": %s", first ? "" : ", ", server_gametype_fields[field].key, value);
			first = FALSE;
		}
		else
		{
			command_output_printf(output, "%c %-32s %s\n", admin_gametype.set[field] ? '*' : ' ',
				server_gametype_fields[field].key, value);
		}
	}
	if (json)
	{
		command_output_printf(output, "}, \"set\": [");
		first = TRUE;
		for (field = 0; field < SERVER_GAMETYPE_FIELDS; field++)
		{
			if (!admin_gametype.set[field])
				continue;
			command_output_printf(output, "%s\"%s\"", first ? "" : ", ", server_gametype_fields[field].key);
			first = FALSE;
		}
		command_output_printf(output, "]}");
	}
	return TRUE;
}

/* a game type saved as its file (checked whole first): TRUE, else FALSE
and why in the output */
static boolean gametype_store(
	char const *name,
	struct server_gametype const *gametype,
	struct command_output *output)
{
	static char text[SERVER_GAMETYPE_TEXT_SIZE + 1];
	static struct server_gametype check;
	char path[ADMIN_PATH_SIZE];
	char parse_error[SERVER_CONFIG_ERROR_SIZE];
	boolean existed;

	if (server_gametype_format(gametype, name, text, sizeof(text)) < 0 ||
		!server_gametype_parse(text, strlen(text), &check, parse_error, sizeof(parse_error)))
	{
		if (server_gametype_format(gametype, name, text, sizeof(text)) < 0)
			snprintf(parse_error, sizeof(parse_error), "it does not fit in a game type file");
		command_output_printf(output, "%s: nothing saved\n", parse_error);
		return FALSE;
	}
	snprintf(path, sizeof(path), "%s/%s.toml", GAMETYPES_FOLDER, name);
	existed = file_exists(path);
	if (!write_text(GAMETYPES_FOLDER, path, text, admin_problem, sizeof(admin_problem)))
	{
		command_output_printf(output, "%s\n", admin_problem);
		return FALSE;
	}
	dedicated_server_gametypes_changed();
	error(_error_silent, "dedicated: saved the game type %s", path);
	command_output_printf(output, "%s %s (played from the next game that has it)\n", existed ? "saved" : "made", path);
	return TRUE;
}

/* a name a new game type file may have: FALSE, with why, if not */
static boolean gametype_name_free(
	char const *name,
	struct command_output *output)
{
	if (!server_config_name_valid(name))
	{
		command_output_printf(output, "%s is not a game type's name (a-z, 0-9, _ and -, 31 at most)\n", name);
		return FALSE;
	}
	if (server_builtin_find(name) >= 0)
	{
		command_output_printf(output, "%s is a built-in game type's name: give yours another\n", name);
		return FALSE;
	}
	return TRUE;
}

boolean server_admin_gametype_new(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	char const *name = line->words[1];
	char const *base = line->words[2];
	char path[ADMIN_PATH_SIZE];
	char problem[SERVER_CONFIG_ERROR_SIZE + 64];
	int builtin = server_builtin_find(base);

	(void)json;
	if (!gametype_name_free(name, output))
		return FALSE;
	snprintf(path, sizeof(path), "%s/%s.toml", GAMETYPES_FOLDER, name);
	if (file_exists(path))
	{
		command_output_printf(output, "there is a game type %s already (%s)\n", name, path);
		return FALSE;
	}
	csmemset(&admin_gametype, 0, sizeof(admin_gametype));
	if (builtin >= 0)
	{
		admin_gametype.values[SERVER_GAMETYPE_BASE] = builtin;
		admin_gametype.set[SERVER_GAMETYPE_BASE] = 1;
	}
	else if (!server_config_name_valid(base) || !gametype_read(base, &admin_gametype, problem, sizeof(problem)))
	{
		command_output_printf(output, "no game type %s to start from (a built-in, or a game type file)\n", base);
		return FALSE;
	}
	return gametype_store(name, &admin_gametype, output);
}

boolean server_admin_gametype_set(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	char const *name = line->words[1];
	char const *key = line->words[2];
	char const *value = line->words[3];
	char problem[SERVER_CONFIG_ERROR_SIZE + 64];
	int field = server_gametype_field_find(key);

	(void)json;
	if (!gametype_name_free(name, output))
		return FALSE;
	if (!gametype_read(name, &admin_gametype, problem, sizeof(problem)))
	{
		command_output_printf(output, "%s\n", problem);
		return FALSE;
	}
	if (field < 0)
	{
		command_output_printf(output, "no setting %s (sv_gametype %s lists them)\n", key, name);
		return FALSE;
	}
	if (field == SERVER_GAMETYPE_NAME)
	{
		long index;

		if (strlen(value) >= sizeof(admin_gametype.title))
		{
			command_output_printf(output, "a name is 11 characters at most\n");
			return FALSE;
		}
		for (index = 0; value[index]; index++)
		{
			if (value[index] < 0x20 || value[index] > 0x7e)
			{
				command_output_printf(output, "a name is printable ASCII\n");
				return FALSE;
			}
		}
		snprintf(admin_gametype.title, sizeof(admin_gametype.title), "%s", value);
	}
	else if (!server_gametype_value_parse(field, value, &admin_gametype.values[field], problem, sizeof(problem)))
	{
		command_output_printf(output, "%s\n", problem);
		return FALSE;
	}
	admin_gametype.set[field] = 1;
	return gametype_store(name, &admin_gametype, output);
}

boolean server_admin_gametype_delete(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	char const *name = line->words[1];
	char path[ADMIN_PATH_SIZE];
	long index;
	boolean used = FALSE;

	(void)json;
	if (!gametype_name_free(name, output))
		return FALSE;
	snprintf(path, sizeof(path), "%s/%s.toml", GAMETYPES_FOLDER, name);
	if (!file_exists(path))
	{
		command_output_printf(output, "no game type %s\n", name);
		return FALSE;
	}
	for (index = 0; index < dedicated_playlist_count(); index++)
	{
		if (!strcmp(dedicated_playlist_variant(index), name))
		{
			command_output_printf(output, "%s%ld", used ? ", " : "the playlist played has it (entries ", index + 1);
			used = TRUE;
		}
	}
	if (used)
	{
		command_output_printf(output, "): take them out first\n");
		return FALSE;
	}
	if (!delete_file(path, admin_problem, sizeof(admin_problem)))
	{
		command_output_printf(output, "%s\n", admin_problem);
		return FALSE;
	}
	dedicated_server_gametypes_changed();
	error(_error_silent, "dedicated: deleted the game type %s", path);
	command_output_printf(output, "deleted %s\n", path);
	return TRUE;
}

boolean server_admin_gametype_save(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	char const *text = server_commands_payload();
	char parse_error[SERVER_CONFIG_ERROR_SIZE];

	(void)json;
	if (!text)
	{
		command_output_printf(output, "sv_gametype_save is the control API's (POST /v1/gametype); here, "
			"sv_gametype_new and sv_gametype_set\n");
		return FALSE;
	}
	if (!gametype_name_free(line->words[1], output))
		return FALSE;
	if (!server_gametype_parse(text, strlen(text), &admin_gametype, parse_error, sizeof(parse_error)))
	{
		command_output_printf(output, "%s: nothing saved\n", parse_error);
		return FALSE;
	}
	return gametype_store(line->words[1], &admin_gametype, output);
}

/* ---------- the commands: settings */

/* a setting's value now (the server's), as text */
static void setting_now(
	short setting,
	struct dedicated_status const *status,
	char *text,
	long size)
{
	switch (setting)
	{
	case SERVER_SETTING_NAME: snprintf(text, (size_t)size, "%s", status->name); break;
	case SERVER_SETTING_MAXIMUM_PLAYERS: snprintf(text, (size_t)size, "%ld", status->maximum_players); break;
	case SERVER_SETTING_MINIMUM_PLAYERS: snprintf(text, (size_t)size, "%ld", status->minimum_players); break;
	case SERVER_SETTING_IDLE_LIMIT: snprintf(text, (size_t)size, "%ld", status->idle_limit); break;
	case SERVER_SETTING_PUBLIC: snprintf(text, (size_t)size, "%s", status->public_game ? "true" : "false"); break;
	case SERVER_SETTING_PLAYLIST: snprintf(text, (size_t)size, "%s", status->playlist); break;
	default: text[0] = 0; break;
	}
}

static void setting_saved(
	short setting,
	struct server_settings const *settings,
	char *text,
	long size)
{
	text[0] = 0;
	if (!settings->set[setting])
		return;
	if (setting == SERVER_SETTING_NAME)
		snprintf(text, (size_t)size, "%s", settings->name);
	else if (setting == SERVER_SETTING_PLAYLIST)
		snprintf(text, (size_t)size, "%s", settings->playlist);
	else if (setting == SERVER_SETTING_PLAYLIST_ENVIRONMENT)
		snprintf(text, (size_t)size, "%s", settings->playlist_environment);
	else if (server_settings_table[setting].kind == SERVER_FIELD_BOOLEAN)
		snprintf(text, (size_t)size, "%s", settings->values[setting] ? "true" : "false");
	else
		snprintf(text, (size_t)size, "%d", settings->values[setting]);
}

static char const *environment_value(
	short setting)
{
	char const *variable = server_settings_table[setting].environment;
	char const *value = variable ? getenv(variable) : NULL;

	return value && value[0] ? value : NULL;
}

boolean server_admin_settings(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	static struct server_settings settings;
	static char text[SERVER_SETTINGS_TEXT_SIZE + 1];
	struct dedicated_status status;
	char parse_error[SERVER_CONFIG_ERROR_SIZE];
	char file_problem[SERVER_CONFIG_ERROR_SIZE + 64];
	long length;
	int result;
	short setting;

	(void)line;
	dedicated_server_get_status(&status);
	file_problem[0] = 0;
	csmemset(&settings, 0, sizeof(settings));
	result = read_text(SETTINGS_FILE, text, SERVER_SETTINGS_TEXT_SIZE, &length, file_problem, sizeof(file_problem));
	if (result > 0 && !server_settings_parse(text, (size_t)length, &settings, parse_error, sizeof(parse_error)))
		snprintf(file_problem, sizeof(file_problem), "%s, %s: it is left out until it is fixed", SETTINGS_FILE,
			parse_error);
	if (json)
		command_output_printf(output, "{\"file\": \"%s\", \"file_problem\": ", SETTINGS_FILE);
	if (json)
	{
		json_string_or_null(output, file_problem);
		command_output_printf(output, ", \"settings\": [");
	}
	else
	{
		command_output_printf(output, "settings (%s; a setting in the environment wins over it):\n", SETTINGS_FILE);
		if (file_problem[0])
			command_output_printf(output, "  ! %s\n", file_problem);
	}
	for (setting = 0; setting < SERVER_SETTINGS; setting++)
	{
		struct server_setting const *description = &server_settings_table[setting];
		char now[SERVER_SETTINGS_PATH_SIZE + 8], saved[SERVER_SETTINGS_PATH_SIZE + 8];
		char const *environment = environment_value(setting);
		char const *source;

		if (setting == SERVER_SETTING_PLAYLIST_ENVIRONMENT)
			continue;
		setting_now(setting, &status, now, sizeof(now));
		setting_saved(setting, &settings, saved, sizeof(saved));
		source = environment ? "environment" : saved[0] ? "file" : "default";
		if (setting == SERVER_SETTING_PLAYLIST)
		{
			source = same_path(status.playlist, settings.playlist) && saved[0] ? "file" : "environment";
			environment = getenv("HALO_DEDICATED");
		}
		if (json)
		{
			command_output_printf(output, "%s{\"key\": \"%s\", \"kind\": \"%s\", \"minimum\": %d, \"maximum\": %d, "
				"\"label\": ", setting ? ", " : "", description->key, field_kind_name(description->kind),
				description->minimum, description->maximum);
			command_output_json_string(output, description->label);
			command_output_printf(output, ", \"value\": ");
			command_output_json_string(output, now);
			command_output_printf(output, ", \"saved\": ");
			json_string_or_null(output, saved);
			command_output_printf(output, ", \"environment_variable\": ");
			json_string_or_null(output, setting == SERVER_SETTING_PLAYLIST ? "HALO_DEDICATED" : description->environment);
			command_output_printf(output, ", \"environment\": ");
			json_string_or_null(output, environment);
			command_output_printf(output, ", \"source\": \"%s\", \"applies\": \"%s\"}", source,
				setting == SERVER_SETTING_PUBLIC ? "restart" : setting == SERVER_SETTING_PLAYLIST ? "sv_playlist_use" :
				setting == SERVER_SETTING_IDLE_LIMIT ? "now" : "lobby");
		}
		else
		{
			command_output_printf(output, "  %-16s %-24s (%s%s%s)\n", description->key, now,
				!strcmp(source, "environment") ? "the environment's " : !strcmp(source, "file") ? "the file's" : "the default",
				!strcmp(source, "environment") ? (setting == SERVER_SETTING_PLAYLIST ? "HALO_DEDICATED" :
				description->environment) : "", saved[0] && strcmp(saved, now) ? "; the file says another" : "");
		}
	}
	if (json)
		command_output_printf(output, "]}");
	return TRUE;
}

boolean server_admin_set(
	struct command_line const *line,
	boolean json,
	struct command_output *output)
{
	static struct server_settings change;
	struct dedicated_status status;
	char problem[SERVER_CONFIG_ERROR_SIZE];
	int setting = server_settings_find(line->words[1]);
	char const *environment;

	(void)json;
	if (setting < 0 || setting == SERVER_SETTING_PLAYLIST || setting == SERVER_SETTING_PLAYLIST_ENVIRONMENT)
	{
		command_output_printf(output, "%s%s\n", setting < 0 ? "no setting " : "", setting < 0 ? line->words[1] :
			"the playlist is set with sv_playlist_use");
		if (setting < 0)
			command_output_printf(output, "(name, maximum_players, minimum_players, idle_limit, public)\n");
		return FALSE;
	}
	environment = environment_value((short)setting);
	if (environment)
	{
		command_output_printf(output, "%s is set in the server's environment (%s), which wins over the settings file: "
			"change it there (sv_name and sv_maxplayers change the name and the most players until a restart)\n",
			server_settings_table[setting].environment, environment);
		return FALSE;
	}
	csmemset(&change, 0, sizeof(change));
	if (!server_settings_value_parse(setting, line->words[2], &change, problem, sizeof(problem)))
	{
		command_output_printf(output, "%s\n", problem);
		return FALSE;
	}
	dedicated_server_get_status(&status);
	if (setting == SERVER_SETTING_MAXIMUM_PLAYERS && change.values[setting] < status.minimum_players)
	{
		command_output_printf(output, "a game waits for %ld players: no fewer\n", status.minimum_players);
		return FALSE;
	}
	if (setting == SERVER_SETTING_MINIMUM_PLAYERS && change.values[setting] > status.maximum_players)
	{
		command_output_printf(output, "a game takes %ld players at most: no more\n", status.maximum_players);
		return FALSE;
	}
	if (!write_settings(&change, output))
		return FALSE;
	switch (setting)
	{
	case SERVER_SETTING_NAME: dedicated_server_set_name(change.name); break;
	case SERVER_SETTING_MAXIMUM_PLAYERS: dedicated_server_set_maximum_players(change.values[setting]); break;
	case SERVER_SETTING_MINIMUM_PLAYERS: dedicated_server_set_minimum_players(change.values[setting]); break;
	case SERVER_SETTING_IDLE_LIMIT: dedicated_server_set_idle_limit(change.values[setting]); break;
	}
	error(_error_silent, "dedicated: a command sets %s to %s", server_settings_table[setting].key, line->words[2]);
	command_output_printf(output, "%s is %s, saved in %s (%s)\n", server_settings_table[setting].key, line->words[2],
		SETTINGS_FILE, setting == SERVER_SETTING_PUBLIC ? "from when the server restarts" :
		setting == SERVER_SETTING_IDLE_LIMIT ? "now" : status.state == _dedicated_state_lobby ? "now" :
		"from the next lobby");
	return TRUE;
}

#endif
