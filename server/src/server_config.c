/*
SERVER_CONFIG.C

The dedicated server's files as text (server_config.h). Everything here is
read as a stranger's: a file in the data folder someone wrote by hand, or
text the control API was sent.
*/

#include "server_config.h"
#include "command_line.h"

#include <stdio.h>
#include <string.h>

/* ---------- the built-in game types */

const struct server_builtin server_builtins[] =
{
	{ "slayer", SERVER_ENGINE_SLAYER },
	{ "team_slayer", SERVER_ENGINE_SLAYER },
	{ "elimination", SERVER_ENGINE_SLAYER },
	{ "ctf", SERVER_ENGINE_CTF },
	{ "ironctf", SERVER_ENGINE_CTF },
	{ "king", SERVER_ENGINE_KING },
	{ "team_king", SERVER_ENGINE_KING },
	{ "oddball", SERVER_ENGINE_ODDBALL },
	{ "team_oddball", SERVER_ENGINE_ODDBALL },
	{ "stalker", SERVER_ENGINE_ODDBALL },
	{ "accumulation", SERVER_ENGINE_ODDBALL },
	{ "race", SERVER_ENGINE_RACE },
	{ "team_race", SERVER_ENGINE_RACE },
	{ "rally", SERVER_ENGINE_RACE },
};

const int server_builtin_count = (int)(sizeof(server_builtins) / sizeof(server_builtins[0]));

/* ---------- the game type settings' choices */

#define CHOICES(array) array, (int)(sizeof(array) / sizeof(array[0]))
#define NO_CHOICES NULL, 0

static const struct server_choice base_choices[] =
{
	{ "slayer", 0 }, { "team_slayer", 1 }, { "elimination", 2 }, { "ctf", 3 }, { "ironctf", 4 }, { "king", 5 },
	{ "team_king", 6 }, { "oddball", 7 }, { "team_oddball", 8 }, { "stalker", 9 }, { "accumulation", 10 },
	{ "race", 11 }, { "team_race", 12 }, { "rally", 13 },
};

/* (game_engine.c's game_engine_weapons) */
static const struct server_choice weapon_set_choices[] =
{
	{ "normal", 0 }, { "pistols", 1 }, { "assault_rifles", 2 }, { "plasma", 3 }, { "sniping", 4 },
	{ "no_sniping", 5 }, { "rocket_launchers", 6 }, { "shotguns", 7 }, { "short_range", 8 }, { "human", 9 },
	{ "no_grenades", 10 }, { "covenant", 11 }, { "classic", 12 }, { "heavy", 13 },
};

static const struct server_choice starting_equipment_choices[] = { { "map", 0 }, { "generic", 1 } };

/* (game_engine.h's loadouts and their weapons) */
static const struct server_choice loadout_choices[] = { { "weapon_set", 0 }, { "custom", 1 } };

static const struct server_choice weapon_choices[] =
{
	{ "none", 0 }, { "random", 1 }, { "assault_rifle", 2 }, { "pistol", 3 }, { "shotgun", 4 },
	{ "sniper_rifle", 5 }, { "rocket_launcher", 6 }, { "plasma_pistol", 7 }, { "plasma_rifle", 8 },
	{ "needler", 9 },
};

/* (game_engine.c's goal_radar) */
static const struct server_choice objectives_choices[] =
{
	{ "motion_tracker", 0 }, { "nav_points", 1 }, { "none", 2 },
};

static const struct server_choice radar_choices[] = { { "all", 0 }, { "friends", 1 }, { "none", 2 } };

static const struct server_choice friendly_fire_choices[] =
{
	{ "on", 0 }, { "off", 1 }, { "shields_only", 2 }, { "explosives_only", 3 },
};

/* (the vehicle sets: game_engine.c's, then a vehicle alone each, as the
gametype editor's presets; custom: the counts) */
static const struct server_choice vehicle_choices[] =
{
	{ "default", 0 }, { "none", 1 }, { "warthog", 2 }, { "ghost", 3 }, { "scorpion", 4 },
	{ "rocket_warthog", 5 }, { "banshee", 6 }, { "gun_turret", 7 }, { "custom", SERVER_VEHICLES_CUSTOM },
};

/* (game_engine_oddball.c's) */
static const struct server_choice speed_choices[] = { { "slow", 0 }, { "normal", 1 }, { "fast", 2 } };

static const struct server_choice trait_choices[] =
{
	{ "none", 0 }, { "invisible", 1 }, { "extra_damage", 2 }, { "damage_resistant", 3 },
};

static const struct server_choice ball_type_choices[] =
{
	{ "normal", 0 }, { "reverse_tag", 1 }, { "juggernaut", 2 },
};

/* (game_engine_race.c's) */
static const struct server_choice race_type_choices[] = { { "normal", 0 }, { "any_order", 1 }, { "rally", 2 } };

static const struct server_choice team_scoring_choices[] = { { "minimum", 0 }, { "maximum", 1 }, { "sum", 2 } };

#define SECONDS(maximum) SERVER_FIELD_INTEGER, 0, (maximum), NO_CHOICES
#define BOOLEAN SERVER_FIELD_BOOLEAN, 0, 1, NO_CHOICES
#define CHOICE(array) SERVER_FIELD_CHOICE, 0, 0, CHOICES(array)
#define VEHICLE_COUNT SERVER_FIELD_INTEGER, 0, SERVER_VEHICLES_MAXIMUM, NO_CHOICES

const struct server_gametype_field server_gametype_fields[SERVER_GAMETYPE_FIELDS] =
{
	{ "name", SERVER_FIELD_STRING, 0, SERVER_GAMETYPE_TITLE_SIZE - 1, NO_CHOICES, 0, "Name in the game", "11 characters" },
	{ "base", CHOICE(base_choices), 0, "Built-in game type it starts from", NULL },
	{ "score_limit", SERVER_FIELD_INTEGER, 1, SERVER_SCORE_LIMIT_MAXIMUM, NO_CHOICES, 0, "Score limit",
		"kills, captures, laps; minutes for Oddball and King" },
	{ "time_limit", SERVER_FIELD_INTEGER, 0, 1440, NO_CHOICES, 0, "Time limit", "minutes; 0: none" },
	{ "teams", BOOLEAN, 0, "Teams", NULL },

	{ "players.lives", SERVER_FIELD_INTEGER, 0, 99, NO_CHOICES, 0, "Lives", "0: unlimited" },
	{ "players.health", SERVER_FIELD_INTEGER, 25, 400, NO_CHOICES, 0, "Health", "percent" },
	{ "players.shields", BOOLEAN, 0, "Shields", NULL },
	{ "players.respawn_time", SECONDS(300), 0, "Respawn time", "seconds" },
	{ "players.respawn_growth", SECONDS(300), 0, "Respawn time growth", "seconds" },
	{ "players.suicide_penalty", SECONDS(300), 0, "Suicide penalty", "seconds" },
	{ "players.odd_man_out", BOOLEAN, 0, "Odd man out", NULL },
	{ "players.invisible", BOOLEAN, 0, "Invisible players", NULL },

	{ "items.weapon_set", CHOICE(weapon_set_choices), 0, "Weapon set", NULL },
	{ "items.starting_equipment", CHOICE(starting_equipment_choices), 0, "Starting equipment", NULL },
	{ "items.map_weapons", BOOLEAN, 0, "Weapons on the map", NULL },
	{ "items.infinite_grenades", BOOLEAN, 0, "Infinite grenades", NULL },
	{ "items.loadout", CHOICE(loadout_choices), 0, "Starting weapons", "the weapon set's, or custom" },
	{ "items.primary_weapon", CHOICE(weapon_choices), 0, "Primary weapon", "custom starting weapons" },
	{ "items.secondary_weapon", CHOICE(weapon_choices), 0, "Secondary weapon", "custom starting weapons" },

	{ "indicators.objectives", CHOICE(objectives_choices), 0, "Objectives indicator", NULL },
	{ "indicators.players_on_radar", CHOICE(radar_choices), 0, "Players on the motion tracker", NULL },
	{ "indicators.friends_on_screen", BOOLEAN, 0, "Friends on screen", NULL },

	{ "team_play.friendly_fire", CHOICE(friendly_fire_choices), 0, "Friendly fire", NULL },
	{ "team_play.friendly_fire_penalty", SECONDS(300), 0, "Friendly fire penalty", "seconds" },
	{ "team_play.auto_balance", BOOLEAN, 0, "Auto team balance", NULL },

	{ "vehicles.respawn_time", SECONDS(3600), 0, "Vehicle respawn", "seconds; 0: never" },
	{ "vehicles.red", CHOICE(vehicle_choices), 0, "Red's vehicles", "free for all: everyone's" },
	{ "vehicles.blue", CHOICE(vehicle_choices), 0, "Blue's vehicles", NULL },
	{ "vehicles.red_warthog", VEHICLE_COUNT, 0, "Red's warthogs", "with custom" },
	{ "vehicles.red_ghost", VEHICLE_COUNT, 0, "Red's ghosts", "with custom" },
	{ "vehicles.red_scorpion", VEHICLE_COUNT, 0, "Red's scorpions", "with custom" },
	{ "vehicles.red_rocket_warthog", VEHICLE_COUNT, 0, "Red's rocket warthogs", "with custom" },
	{ "vehicles.red_banshee", VEHICLE_COUNT, 0, "Red's banshees", "with custom" },
	{ "vehicles.red_gun_turret", VEHICLE_COUNT, 0, "Red's gun turrets", "with custom" },
	{ "vehicles.blue_warthog", VEHICLE_COUNT, 0, "Blue's warthogs", "with custom" },
	{ "vehicles.blue_ghost", VEHICLE_COUNT, 0, "Blue's ghosts", "with custom" },
	{ "vehicles.blue_scorpion", VEHICLE_COUNT, 0, "Blue's scorpions", "with custom" },
	{ "vehicles.blue_rocket_warthog", VEHICLE_COUNT, 0, "Blue's rocket warthogs", "with custom" },
	{ "vehicles.blue_banshee", VEHICLE_COUNT, 0, "Blue's banshees", "with custom" },
	{ "vehicles.blue_gun_turret", VEHICLE_COUNT, 0, "Blue's gun turrets", "with custom" },

	{ "ctf.assault", BOOLEAN, SERVER_ENGINE_CTF, "Assault", NULL },
	{ "ctf.single_flag_time", SECONDS(3600), SERVER_ENGINE_CTF, "Single flag", "seconds a side; 0: off" },
	{ "ctf.flag_must_reset", BOOLEAN, SERVER_ENGINE_CTF, "Flag must reset", NULL },
	{ "ctf.flag_at_home_to_score", BOOLEAN, SERVER_ENGINE_CTF, "Flag at home to score", NULL },
	{ "ctf.reset_on_capture", BOOLEAN, SERVER_ENGINE_CTF, "Reset on capture", NULL },

	{ "slayer.death_bonus", BOOLEAN, SERVER_ENGINE_SLAYER, "Death bonus", NULL },
	{ "slayer.kill_penalty", BOOLEAN, SERVER_ENGINE_SLAYER, "Kill penalty", NULL },
	{ "slayer.kill_in_order", BOOLEAN, SERVER_ENGINE_SLAYER, "Kill in order", NULL },

	{ "king.moving_hill", BOOLEAN, SERVER_ENGINE_KING, "Moving hill", NULL },

	{ "oddball.random_start", BOOLEAN, SERVER_ENGINE_ODDBALL, "Random start", NULL },
	{ "oddball.spawn_delay", BOOLEAN, SERVER_ENGINE_ODDBALL, "Ball spawn delay", NULL },
	{ "oddball.speed_with_ball", CHOICE(speed_choices), SERVER_ENGINE_ODDBALL, "Speed with ball", NULL },
	{ "oddball.trait_with_ball", CHOICE(trait_choices), SERVER_ENGINE_ODDBALL, "Trait with ball", NULL },
	{ "oddball.trait_without_ball", CHOICE(trait_choices), SERVER_ENGINE_ODDBALL, "Trait without ball", NULL },
	{ "oddball.ball_type", CHOICE(ball_type_choices), SERVER_ENGINE_ODDBALL, "Ball type", NULL },
	{ "oddball.balls", SERVER_FIELD_INTEGER, 1, 16, NO_CHOICES, SERVER_ENGINE_ODDBALL, "Balls", NULL },

	{ "race.type", CHOICE(race_type_choices), SERVER_ENGINE_RACE, "Race type", NULL },
	{ "race.team_scoring", CHOICE(team_scoring_choices), SERVER_ENGINE_RACE, "Team scoring", NULL },
};

/* ---------- the settings file's */

const struct server_setting server_settings_table[SERVER_SETTINGS] =
{
	{ "name", SERVER_FIELD_STRING, 1, 15, "HALO_DEDICATED_NAME", "Server name" },
	{ "maximum_players", SERVER_FIELD_INTEGER, 1, SERVER_SETTINGS_MAXIMUM_PLAYERS, "HALO_DEDICATED_MAXIMUM_PLAYERS",
		"Most players" },
	{ "minimum_players", SERVER_FIELD_INTEGER, 1, SERVER_SETTINGS_MAXIMUM_PLAYERS, "HALO_DEDICATED_MINIMUM_PLAYERS",
		"Players a game waits for" },
	{ "idle_limit", SERVER_FIELD_INTEGER, 0, SERVER_SETTINGS_MAXIMUM_IDLE_LIMIT, "HALO_DEDICATED_IDLE_LIMIT",
		"Minutes without a score that end a game (0: never)" },
	{ "public", SERVER_FIELD_BOOLEAN, 0, 1, "HALO_DEDICATED_PUBLIC", "Listed in the in-game Server Browser" },
	{ "playlist", SERVER_FIELD_STRING, 0, SERVER_SETTINGS_PATH_SIZE - 1, NULL, "The playlist sv_playlist_use chose" },
	{ "playlist_environment", SERVER_FIELD_STRING, 0, SERVER_SETTINGS_PATH_SIZE - 1, NULL,
		"HALO_DEDICATED when sv_playlist_use chose it" },
};

/* ---------- private code */

static void set_error(char *error, int error_size, int line, const char *format, const char *a, const char *b)
{
	char text[SERVER_CONFIG_ERROR_SIZE];

	if (!error || error_size <= 0)
		return;
	snprintf(text, sizeof(text), format, a ? a : "", b ? b : "");
	if (line > 0)
		snprintf(error, (size_t)error_size, "line %d: %s", line, text);
	else
		snprintf(error, (size_t)error_size, "%s", text);
}

/* whether text (length bytes) is what a file of the server's may hold:
printable ASCII, tabs, and lines ended by LF or CR LF; 0 and why if not */
static int text_valid(const char *text, size_t length, size_t maximum, char *error, int error_size)
{
	size_t index;
	int line = 1;

	if (length > maximum)
	{
		char size[24];

		snprintf(size, sizeof(size), "%d", (int)maximum);
		set_error(error, error_size, 0, "the text is longer than %s bytes", size, NULL);
		return 0;
	}
	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)text[index];

		if (character == '\n')
			line++;
		else if (character == '\r' && index + 1 < length && text[index + 1] == '\n')
			continue;
		else if ((character < 0x20 && character != '\t') || character > 0x7e)
		{
			set_error(error, error_size, line, "a character that is not printable ASCII%s", NULL, NULL);
			return 0;
		}
	}
	return 1;
}

static int is_space(char character)
{
	return character == ' ' || character == '\t';
}

/* the next line of text (from *offset): its start and length without its
end; 0 when there is none */
static int next_line(const char *text, size_t length, size_t *offset, const char **line, size_t *line_length)
{
	size_t start = *offset;
	size_t end = start;

	if (start >= length)
		return 0;
	while (end < length && text[end] != '\n')
		end++;
	*offset = end < length ? end + 1 : end;
	if (end > start && text[end - 1] == '\r')
		end--;
	*line = text + start;
	*line_length = end - start;
	return 1;
}

/* ---------- TOML's part the files are (server_config.h) */

enum
{
	TOML_INTEGER,
	TOML_BOOLEAN,
	TOML_STRING,
	TOML_KEY_SIZE = 64,
	TOML_STRING_SIZE = SERVER_SETTINGS_PATH_SIZE,
	TOML_TABLES = 16,
};

struct toml_value
{
	int kind;
	int integer;
	char string[TOML_STRING_SIZE];
};

typedef int (*toml_callback)(void *context, int line, const char *key, const struct toml_value *value, char *error,
	int error_size);

static int bare_key_character(char character)
{
	return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_';
}

/* a value at text (up to end): into value, and where it ends in *after;
0 and why if it is none the files take */
static int toml_value(const char *text, const char *end, struct toml_value *value, const char **after, int line,
	char *error, int error_size)
{
	memset(value, 0, sizeof(*value));
	if (text < end && (*text == '"' || *text == '\''))
	{
		char quote = *text++;
		size_t length = 0;

		value->kind = TOML_STRING;
		for (;;)
		{
			char character;

			if (text >= end)
			{
				set_error(error, error_size, line, "a string is not closed%s", NULL, NULL);
				return 0;
			}
			character = *text++;
			if (character == quote)
				break;
			if (character == '\t')
			{
				set_error(error, error_size, line, "a tab in a string%s", NULL, NULL);
				return 0;
			}
			if (character == '\\' && quote == '"')
			{
				if (text >= end || (*text != '"' && *text != '\\'))
				{
					set_error(error, error_size, line, "a string's only escapes are \\\" and \\\\%s", NULL, NULL);
					return 0;
				}
				character = *text++;
			}
			if (length + 1 >= sizeof(value->string))
			{
				set_error(error, error_size, line, "a string is too long%s", NULL, NULL);
				return 0;
			}
			value->string[length++] = character;
		}
		value->string[length] = 0;
	}
	else if (end - text >= 4 && !memcmp(text, "true", 4) && (end - text == 4 || !bare_key_character(text[4])))
	{
		value->kind = TOML_BOOLEAN;
		value->integer = 1;
		text += 4;
	}
	else if (end - text >= 5 && !memcmp(text, "false", 5) && (end - text == 5 || !bare_key_character(text[5])))
	{
		value->kind = TOML_BOOLEAN;
		text += 5;
	}
	else
	{
		int negative = 0;
		int digits = 0;
		int number = 0;

		value->kind = TOML_INTEGER;
		if (text < end && (*text == '-' || *text == '+'))
			negative = *text++ == '-';
		while (text < end && *text >= '0' && *text <= '9')
		{
			if (++digits > 9)
			{
				set_error(error, error_size, line, "a number is too large%s", NULL, NULL);
				return 0;
			}
			if (digits == 2 && number == 0)
			{
				set_error(error, error_size, line, "a number begins with 0%s", NULL, NULL);
				return 0;
			}
			number = number * 10 + (*text++ - '0');
		}
		if (!digits || (text < end && !is_space(*text) && *text != '#'))
		{
			set_error(error, error_size, line,
				"a value is a whole number, true, false, or a string in quotes%s", NULL, NULL);
			return 0;
		}
		value->integer = negative ? -number : number;
	}
	*after = text;
	return 1;
}

/* each key of the text, in order, to the callback, with its table's name
before it ("players.lives"): 1, else 0 and why */
static int toml_each(const char *text, size_t length, size_t maximum, toml_callback callback, void *context,
	char *error, int error_size)
{
	char tables[TOML_TABLES][TOML_KEY_SIZE];
	int table_count = 0;
	char table[TOML_KEY_SIZE] = "";
	size_t offset = 0;
	const char *line_text;
	size_t line_length;
	int line = 0;

	if (error && error_size > 0)
		error[0] = 0;
	if (!text_valid(text, length, maximum, error, error_size))
		return 0;
	while (next_line(text, length, &offset, &line_text, &line_length))
	{
		const char *cursor = line_text;
		const char *end = line_text + line_length;

		line++;
		while (cursor < end && is_space(*cursor))
			cursor++;
		if (cursor >= end || *cursor == '#')
			continue;
		if (*cursor == '[')
		{
			size_t name_length = 0;
			int index;

			cursor++;
			while (cursor < end && bare_key_character(*cursor) && name_length + 1 < sizeof(table))
				table[name_length++] = *cursor++;
			table[name_length] = 0;
			if (!name_length || cursor >= end || *cursor != ']')
			{
				set_error(error, error_size, line, "a table's name is letters, digits and _ in [ ]%s", NULL, NULL);
				return 0;
			}
			cursor++;
			while (cursor < end && is_space(*cursor))
				cursor++;
			if (cursor < end && *cursor != '#')
			{
				set_error(error, error_size, line, "more after [%s]%s", table, NULL);
				return 0;
			}
			for (index = 0; index < table_count; index++)
			{
				if (!strcmp(tables[index], table))
				{
					set_error(error, error_size, line, "a second [%s]%s", table, NULL);
					return 0;
				}
			}
			if (table_count >= TOML_TABLES)
			{
				set_error(error, error_size, line, "too many tables%s", NULL, NULL);
				return 0;
			}
			snprintf(tables[table_count++], sizeof(tables[0]), "%s", table);
			continue;
		}
		{
			char key[TOML_KEY_SIZE * 2];
			size_t key_length = 0;
			struct toml_value value;

			if (table[0])
				key_length = (size_t)snprintf(key, sizeof(key), "%s.", table);
			if (cursor >= end || !bare_key_character(*cursor))
			{
				set_error(error, error_size, line, "a key is letters, digits and _%s", NULL, NULL);
				return 0;
			}
			while (cursor < end && bare_key_character(*cursor))
			{
				if (key_length + 1 >= sizeof(key))
				{
					set_error(error, error_size, line, "a key is too long%s", NULL, NULL);
					return 0;
				}
				key[key_length++] = *cursor++;
			}
			key[key_length] = 0;
			while (cursor < end && is_space(*cursor))
				cursor++;
			if (cursor >= end || *cursor != '=')
			{
				set_error(error, error_size, line, "%s needs = and a value%s", key, NULL);
				return 0;
			}
			cursor++;
			while (cursor < end && is_space(*cursor))
				cursor++;
			if (!toml_value(cursor, end, &value, &cursor, line, error, error_size))
				return 0;
			while (cursor < end && is_space(*cursor))
				cursor++;
			if (cursor < end && *cursor != '#')
			{
				set_error(error, error_size, line, "more after %s's value%s", key, NULL);
				return 0;
			}
			if (!callback(context, line, key, &value, error, error_size))
				return 0;
		}
	}
	return 1;
}

/* text in quotes, as a TOML basic string (the text is printable ASCII) */
static int quoted(const char *text, char *out, size_t size)
{
	size_t length = 0;

	if (size < 3)
		return 0;
	out[length++] = '"';
	for (; *text; text++)
	{
		if (length + 4 >= size)
			return 0;
		if (*text == '"' || *text == '\\')
			out[length++] = '\\';
		out[length++] = *text;
	}
	out[length++] = '"';
	out[length] = 0;
	return 1;
}

/* text appended to out (size bytes, *length used): 0 if it does not fit */
static int append(char *out, size_t size, size_t *length, const char *format, const char *a, const char *b)
{
	int written = snprintf(out + *length, size - *length, format, a ? a : "", b ? b : "");

	if (written < 0 || (size_t)written >= size - *length)
		return 0;
	*length += (size_t)written;
	return 1;
}

/* ---------- public code */

int server_config_name_valid(const char *text)
{
	int length;

	if (!text || !((text[0] >= 'a' && text[0] <= 'z') || (text[0] >= '0' && text[0] <= '9')))
		return 0;
	for (length = 0; text[length]; length++)
	{
		char character = text[length];

		if (length >= SERVER_CONFIG_NAME_SIZE - 1 ||
			!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
				character == '-'))
		{
			return 0;
		}
	}
	return 1;
}

int server_builtin_find(const char *name)
{
	int index;

	for (index = 0; name && index < server_builtin_count; index++)
	{
		if (!strcmp(server_builtins[index].name, name))
			return index;
	}
	return -1;
}

const char *server_engine_name(int engine)
{
	switch (engine)
	{
	case SERVER_ENGINE_CTF: return "ctf";
	case SERVER_ENGINE_SLAYER: return "slayer";
	case SERVER_ENGINE_ODDBALL: return "oddball";
	case SERVER_ENGINE_KING: return "king";
	case SERVER_ENGINE_RACE: return "race";
	}
	return "none";
}

/* ---------- playlists */

int server_playlist_parse(const char *text, size_t length, int strict, struct server_playlist *playlist, char *error,
	int error_size)
{
	size_t offset = 0;
	const char *line_text;
	size_t line_length;
	int line = 0;

	memset(playlist, 0, sizeof(*playlist));
	if (error && error_size > 0)
		error[0] = 0;
	if (strict && !text_valid(text, length, SERVER_PLAYLIST_TEXT_SIZE, error, error_size))
		return 0;
	while (next_line(text, length, &offset, &line_text, &line_length))
	{
		char words[3][SERVER_PLAYLIST_MAP_SIZE];
		size_t sizes[3] = { SERVER_PLAYLIST_MAP_SIZE, SERVER_PLAYLIST_TYPE_SIZE, SERVER_PLAYLIST_MAP_SIZE };
		const char *cursor = line_text;
		const char *end = line_text + line_length;
		const char *comment = memchr(line_text, '#', line_length);
		int count = 0;

		line++;
		if (comment)
			end = comment;
		while (count < 3)
		{
			size_t word_length = 0;

			while (cursor < end && (is_space(*cursor) || *cursor == '\r' || (!strict && (unsigned char)*cursor < 0x20)))
				cursor++;
			if (cursor >= end)
				break;
			while (cursor < end && !is_space(*cursor) && !(!strict && (unsigned char)*cursor < 0x20))
			{
				if (word_length + 1 >= sizes[count])
				{
					if (strict)
					{
						set_error(error, error_size, line, "a word is too long%s", NULL, NULL);
						return 0;
					}
					/* (as the server's reading has it: the rest is the next word) */
					break;
				}
				words[count][word_length++] = *cursor++;
			}
			words[count][word_length] = 0;
			count++;
		}
		if (!count)
			continue;
		if (count < 2)
		{
			if (strict)
			{
				set_error(error, error_size, line, "a map and a game type are needed%s", NULL, NULL);
				return 0;
			}
			continue;
		}
		if (strict)
		{
			if (count > 2)
			{
				set_error(error, error_size, line, "more than a map and a game type%s", NULL, NULL);
				return 0;
			}
			if (!command_line_map_name_valid(words[0]))
			{
				set_error(error, error_size, line, "%s is not a map's name (bloodgulch, name@ce, name@md)", words[0],
					NULL);
				return 0;
			}
			if (server_builtin_find(words[1]) < 0 && !server_config_name_valid(words[1]))
			{
				set_error(error, error_size, line, "%s is not a game type's name%s", words[1], NULL);
				return 0;
			}
		}
		if (playlist->count >= SERVER_PLAYLIST_ENTRIES)
		{
			if (strict)
			{
				char most[16];

				snprintf(most, sizeof(most), "%d", (int)SERVER_PLAYLIST_ENTRIES);
				set_error(error, error_size, line, "a playlist has %s games at most%s", most, NULL);
				return 0;
			}
			break;
		}
		snprintf(playlist->entries[playlist->count].map, sizeof(playlist->entries[0].map), "%s", words[0]);
		snprintf(playlist->entries[playlist->count].game_type, sizeof(playlist->entries[0].game_type), "%s", words[1]);
		playlist->count++;
	}
	return 1;
}

int server_playlist_format(const struct server_playlist *playlist, const char *name, char *out, size_t size)
{
	size_t length = 0;
	int index;

	if (!size)
		return -1;
	out[0] = 0;
	if (!append(out, size, &length, "# %s: a playlist (server/docs/playlists.md), written by the server.\n", name, NULL) ||
		!append(out, size, &length, "# map                  game type\n", NULL, NULL))
	{
		return -1;
	}
	for (index = 0; index < playlist->count; index++)
	{
		int written = snprintf(out + length, size - length, "%-22s %s\n", playlist->entries[index].map,
			playlist->entries[index].game_type);

		if (written < 0 || (size_t)written >= size - length)
			return -1;
		length += (size_t)written;
	}
	return (int)length;
}

/* ---------- game types */

struct gametype_context
{
	struct server_gametype *gametype;
	int lines[SERVER_GAMETYPE_FIELDS];
};

int server_gametype_field_find(const char *key)
{
	int index;

	for (index = 0; key && index < SERVER_GAMETYPE_FIELDS; index++)
	{
		if (!strcmp(server_gametype_fields[index].key, key))
			return index;
	}
	return -1;
}

static int choice_find(const struct server_gametype_field *field, const char *name, int *value)
{
	int index;

	for (index = 0; index < field->choice_count; index++)
	{
		if (!strcmp(field->choices[index].name, name))
		{
			*value = field->choices[index].value;
			return 1;
		}
	}
	return 0;
}

static const char *choice_name(const struct server_gametype_field *field, int value)
{
	int index;

	for (index = 0; index < field->choice_count; index++)
	{
		if (field->choices[index].value == value)
			return field->choices[index].name;
	}
	return NULL;
}

/* a value of a setting's kind and range: into *result, else 0 and why */
static int field_value(int field_index, const struct toml_value *value, int line, int *result, char *error,
	int error_size)
{
	const struct server_gametype_field *field = &server_gametype_fields[field_index];
	char range[48];

	switch (field->kind)
	{
	case SERVER_FIELD_INTEGER:
		snprintf(range, sizeof(range), "%d to %d", field->minimum, field->maximum);
		if (value->kind != TOML_INTEGER || value->integer < field->minimum || value->integer > field->maximum)
		{
			set_error(error, error_size, line, "%s is a whole number from %s", field->key, range);
			return 0;
		}
		*result = value->integer;
		return 1;
	case SERVER_FIELD_BOOLEAN:
		if (value->kind != TOML_BOOLEAN)
		{
			set_error(error, error_size, line, "%s is true or false%s", field->key, NULL);
			return 0;
		}
		*result = value->integer;
		return 1;
	case SERVER_FIELD_CHOICE:
		if (value->kind != TOML_STRING || !choice_find(field, value->string, result))
		{
			char names[SERVER_CONFIG_ERROR_SIZE];
			size_t length = 0;
			int index;

			names[0] = 0;
			for (index = 0; index < field->choice_count && length + 24 < sizeof(names); index++)
			{
				length += (size_t)snprintf(names + length, sizeof(names) - length, "%s\"%s\"", index ? ", " : "",
					field->choices[index].name);
			}
			if (index < field->choice_count)
				snprintf(names + length, sizeof(names) - length, ", ...");
			set_error(error, error_size, line, "%s is one of %s", field->key, names);
			return 0;
		}
		return 1;
	}
	return 0;
}

static int gametype_key(void *context, int line, const char *key, const struct toml_value *value, char *error,
	int error_size)
{
	struct gametype_context *parse = (struct gametype_context *)context;
	struct server_gametype *gametype = parse->gametype;
	int field = server_gametype_field_find(key);

	if (field < 0)
	{
		set_error(error, error_size, line, "no setting %s (server/docs/gametypes.md)%s", key, NULL);
		return 0;
	}
	if (gametype->set[field])
	{
		set_error(error, error_size, line, "%s a second time%s", key, NULL);
		return 0;
	}
	if (field == SERVER_GAMETYPE_NAME)
	{
		size_t index;

		if (value->kind != TOML_STRING || strlen(value->string) >= sizeof(gametype->title))
		{
			set_error(error, error_size, line, "name is a string of 11 characters at most%s", NULL, NULL);
			return 0;
		}
		for (index = 0; value->string[index]; index++)
		{
			if (value->string[index] < 0x20 || value->string[index] > 0x7e)
			{
				set_error(error, error_size, line, "name is printable ASCII%s", NULL, NULL);
				return 0;
			}
		}
		snprintf(gametype->title, sizeof(gametype->title), "%s", value->string);
	}
	else if (!field_value(field, value, line, &gametype->values[field], error, error_size))
		return 0;
	gametype->set[field] = 1;
	parse->lines[field] = line;
	return 1;
}

int server_gametype_parse(const char *text, size_t length, struct server_gametype *gametype, char *error,
	int error_size)
{
	struct gametype_context context;
	int engine;
	int field;

	memset(gametype, 0, sizeof(*gametype));
	memset(&context, 0, sizeof(context));
	context.gametype = gametype;
	if (!toml_each(text, length, SERVER_GAMETYPE_TEXT_SIZE, gametype_key, &context, error, error_size))
	{
		memset(gametype, 0, sizeof(*gametype));
		return 0;
	}
	if (!gametype->set[SERVER_GAMETYPE_BASE])
	{
		set_error(error, error_size, 0, "no base: the built-in game type it starts from (base = \"slayer\")%s", NULL,
			NULL);
		memset(gametype, 0, sizeof(*gametype));
		return 0;
	}
	engine = server_builtins[gametype->values[SERVER_GAMETYPE_BASE]].engine;
	for (field = 0; field < SERVER_GAMETYPE_FIELDS; field++)
	{
		if (gametype->set[field] && server_gametype_fields[field].engine &&
			server_gametype_fields[field].engine != engine)
		{
			char why[96];

			snprintf(why, sizeof(why), "a %s game type's, and base %s is %s", server_engine_name(
				server_gametype_fields[field].engine), server_builtins[gametype->values[SERVER_GAMETYPE_BASE]].name,
				server_engine_name(engine));
			set_error(error, error_size, context.lines[field], "%s is %s", server_gametype_fields[field].key, why);
			memset(gametype, 0, sizeof(*gametype));
			return 0;
		}
	}
	if (engine == SERVER_ENGINE_CTF && gametype->set[SERVER_GAMETYPE_TEAMS] && !gametype->values[SERVER_GAMETYPE_TEAMS])
	{
		set_error(error, error_size, context.lines[SERVER_GAMETYPE_TEAMS], "capture the flag is played in teams%s",
			NULL, NULL);
		memset(gametype, 0, sizeof(*gametype));
		return 0;
	}
	return 1;
}

void server_gametype_value_text(int field, int value, char *text, size_t size)
{
	const struct server_gametype_field *description;

	if (!size)
		return;
	text[0] = 0;
	if (field < 0 || field >= SERVER_GAMETYPE_FIELDS)
		return;
	description = &server_gametype_fields[field];
	if (description->kind == SERVER_FIELD_BOOLEAN)
		snprintf(text, size, "%s", value ? "true" : "false");
	else if (description->kind == SERVER_FIELD_CHOICE)
	{
		const char *name = choice_name(description, value);

		snprintf(text, size, "\"%s\"", name ? name : "?");
	}
	else
		snprintf(text, size, "%d", value);
}

int server_gametype_value_parse(int field, const char *text, int *value, char *error, int error_size)
{
	struct toml_value parsed;
	const char *end;
	char quoted_text[TOML_STRING_SIZE + 4];
	const struct server_gametype_field *description;

	if (field < 0 || field >= SERVER_GAMETYPE_FIELDS || field == SERVER_GAMETYPE_NAME || !text)
		return 0;
	description = &server_gametype_fields[field];
	/* (a choice may be given bare, as a command's word: oddball) */
	if (description->kind == SERVER_FIELD_CHOICE && text[0] != '"')
	{
		if (strlen(text) >= TOML_STRING_SIZE)
		{
			set_error(error, error_size, 0, "%s is too long%s", text, NULL);
			return 0;
		}
		snprintf(quoted_text, sizeof(quoted_text), "\"%s\"", text);
		text = quoted_text;
	}
	if (!toml_value(text, text + strlen(text), &parsed, &end, 0, error, error_size))
		return 0;
	if (*end)
	{
		set_error(error, error_size, 0, "more after %s's value%s", description->key, NULL);
		return 0;
	}
	return field_value(field, &parsed, 0, value, error, error_size);
}

int server_gametype_format(const struct server_gametype *gametype, const char *name, char *out, size_t size)
{
	size_t length = 0;
	char table[TOML_KEY_SIZE] = "";
	int field;

	if (!size)
		return -1;
	out[0] = 0;
	if (!append(out, size, &length, "# %s: a game type (server/docs/gametypes.md), written by the server.\n", name,
		NULL))
	{
		return -1;
	}
	for (field = 0; field < SERVER_GAMETYPE_FIELDS; field++)
	{
		const char *key = server_gametype_fields[field].key;
		const char *dot = strchr(key, '.');
		char value[TOML_STRING_SIZE + 8];

		if (!gametype->set[field])
			continue;
		if (dot && ((size_t)(dot - key) != strlen(table) || strncmp(key, table, (size_t)(dot - key))))
		{
			snprintf(table, sizeof(table), "%.*s", (int)(dot - key), key);
			if (!append(out, size, &length, "\n[%s]\n", table, NULL))
				return -1;
		}
		if (field == SERVER_GAMETYPE_NAME)
		{
			if (!quoted(gametype->title, value, sizeof(value)))
				return -1;
		}
		else
			server_gametype_value_text(field, gametype->values[field], value, sizeof(value));
		if (!append(out, size, &length, "%s = %s\n", dot ? dot + 1 : key, value))
			return -1;
	}
	return (int)length;
}

/* ---------- the settings file */

int server_settings_find(const char *key)
{
	int index;

	for (index = 0; key && index < SERVER_SETTINGS; index++)
	{
		if (!strcmp(server_settings_table[index].key, key))
			return index;
	}
	return -1;
}

int server_settings_path_valid(const char *text)
{
	int length;
	int part = 0;

	if (!text || !text[0] || text[0] == '/')
		return 0;
	for (length = 0; text[length]; length++)
	{
		char character = text[length];

		if (length >= SERVER_SETTINGS_PATH_SIZE - 1 || character < 0x20 || character > 0x7e || character == '"' ||
			character == '\\' || character == ':')
		{
			return 0;
		}
		if (character == '/')
		{
			if (length - part == 0 || (length - part == 2 && text[part] == '.' && text[part + 1] == '.'))
				return 0;
			part = length + 1;
		}
	}
	if (length - part == 0 || (length - part == 2 && text[part] == '.' && text[part + 1] == '.') ||
		(length - part == 1 && text[part] == '.'))
	{
		return 0;
	}
	return 1;
}

/* a setting's value from a TOML value, into settings: 1, else 0 and why */
static int setting_value(int setting, const struct toml_value *value, int line, struct server_settings *settings,
	char *error, int error_size)
{
	const struct server_setting *description = &server_settings_table[setting];
	char range[48];

	switch (description->kind)
	{
	case SERVER_FIELD_INTEGER:
		snprintf(range, sizeof(range), "%d to %d", description->minimum, description->maximum);
		if (value->kind != TOML_INTEGER || value->integer < description->minimum ||
			value->integer > description->maximum)
		{
			set_error(error, error_size, line, "%s is a whole number from %s", description->key, range);
			return 0;
		}
		settings->values[setting] = value->integer;
		break;
	case SERVER_FIELD_BOOLEAN:
		if (value->kind != TOML_BOOLEAN)
		{
			set_error(error, error_size, line, "%s is true or false%s", description->key, NULL);
			return 0;
		}
		settings->values[setting] = value->integer;
		break;
	default:
		if (value->kind != TOML_STRING)
		{
			set_error(error, error_size, line, "%s is a string in quotes%s", description->key, NULL);
			return 0;
		}
		if (setting == SERVER_SETTING_NAME)
		{
			if (!command_line_server_name_valid(value->string))
			{
				set_error(error, error_size, line, "name is 1 to 15 printable ASCII characters%s", NULL, NULL);
				return 0;
			}
			snprintf(settings->name, sizeof(settings->name), "%s", value->string);
		}
		else
		{
			char *path = setting == SERVER_SETTING_PLAYLIST ? settings->playlist : settings->playlist_environment;

			/* (HALO_DEDICATED may have been anything, even nothing) */
			if (setting == SERVER_SETTING_PLAYLIST ? !server_settings_path_valid(value->string) :
				strlen(value->string) >= SERVER_SETTINGS_PATH_SIZE)
			{
				set_error(error, error_size, line, "%s is not a path in the data folder%s", description->key, NULL);
				return 0;
			}
			snprintf(path, SERVER_SETTINGS_PATH_SIZE, "%s", value->string);
		}
		break;
	}
	settings->set[setting] = 1;
	return 1;
}

static int settings_key(void *context, int line, const char *key, const struct toml_value *value, char *error,
	int error_size)
{
	struct server_settings *settings = (struct server_settings *)context;
	int setting = server_settings_find(key);

	if (setting < 0)
	{
		set_error(error, error_size, line, "no setting %s (server/docs/settings.md)%s", key, NULL);
		return 0;
	}
	if (settings->set[setting])
	{
		set_error(error, error_size, line, "%s a second time%s", key, NULL);
		return 0;
	}
	return setting_value(setting, value, line, settings, error, error_size);
}

int server_settings_parse(const char *text, size_t length, struct server_settings *settings, char *error,
	int error_size)
{
	memset(settings, 0, sizeof(*settings));
	if (!toml_each(text, length, SERVER_SETTINGS_TEXT_SIZE, settings_key, settings, error, error_size))
	{
		memset(settings, 0, sizeof(*settings));
		return 0;
	}
	return 1;
}

int server_settings_value_parse(int setting, const char *text, struct server_settings *settings, char *error,
	int error_size)
{
	struct toml_value value;
	const struct server_setting *description;

	if (setting < 0 || setting >= SERVER_SETTINGS || !text)
		return 0;
	description = &server_settings_table[setting];
	memset(&value, 0, sizeof(value));
	if (description->kind == SERVER_FIELD_STRING)
	{
		/* (a command's word is the string itself) */
		if (strlen(text) >= sizeof(value.string))
		{
			set_error(error, error_size, 0, "%s is too long%s", description->key, NULL);
			return 0;
		}
		value.kind = TOML_STRING;
		snprintf(value.string, sizeof(value.string), "%s", text);
	}
	else
	{
		const char *end;

		if (!strcmp(text, "on") || !strcmp(text, "yes"))
			text = "true";
		else if (!strcmp(text, "off") || !strcmp(text, "no"))
			text = "false";
		if (!toml_value(text, text + strlen(text), &value, &end, 0, error, error_size))
			return 0;
		if (*end || value.kind == TOML_STRING)
		{
			set_error(error, error_size, 0, "%s is %s", description->key, description->kind == SERVER_FIELD_BOOLEAN ?
				"true or false" : "a whole number");
			return 0;
		}
	}
	return setting_value(setting, &value, 0, settings, error, error_size);
}

int server_settings_format(const struct server_settings *settings, char *out, size_t size)
{
	size_t length = 0;
	int setting;

	if (!size)
		return -1;
	out[0] = 0;
	if (!append(out, size, &length,
		"# The dedicated server's settings (server/docs/settings.md), written by the server: the\n"
		"# web admin page's and sv_set's. A setting in the server's environment wins over this file.\n", NULL, NULL))
	{
		return -1;
	}
	for (setting = 0; setting < SERVER_SETTINGS; setting++)
	{
		const struct server_setting *description = &server_settings_table[setting];
		char value[SERVER_SETTINGS_PATH_SIZE + 8];

		if (!settings->set[setting])
			continue;
		if (description->kind == SERVER_FIELD_INTEGER)
			snprintf(value, sizeof(value), "%d", settings->values[setting]);
		else if (description->kind == SERVER_FIELD_BOOLEAN)
			snprintf(value, sizeof(value), "%s", settings->values[setting] ? "true" : "false");
		else if (!quoted(setting == SERVER_SETTING_NAME ? settings->name : setting == SERVER_SETTING_PLAYLIST ?
			settings->playlist : settings->playlist_environment, value, sizeof(value)))
		{
			return -1;
		}
		if (!append(out, size, &length, "%s = %s\n", description->key, value))
			return -1;
	}
	return (int)length;
}
