/*
SERVICE_RECORD.C

A Halo profile's own service record (service_record.h): its totals, kept
as text in the profile's folder.

The file, one value a line ("key value", a name last, as it may hold
spaces):

	service_record 1
	games 12
	wins 5
	kills 140
	...
	medal 9 double_kill
	map 4 bloodgulch
	weapon 31 sniper rifle

Lines it does not know (a later build's) are skipped, so an older build
reads a newer file. Every count is clamped to SERVICE_RECORD_MAXIMUM_COUNT.
*/

#include "service_record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	SERVICE_RECORD_VERSION = 1,
};

/* the totals, by their keys in the file (total_of's order) */
static char const *const totals[] = {
	"games", "wins", "kills", "deaths", "assists", "betrayals", "suicides", "seconds", "best_spree",
};

/* the multiplayer maps' names in the menus (as Online Games names them) */
static char const *const map_titles[][2] = {
	{ "beavercreek", "Battle Creek" }, { "bloodgulch", "Blood Gulch" }, { "boardingaction", "Boarding Action" },
	{ "carousel", "Derelict" }, { "chillout", "Chill Out" }, { "damnation", "Damnation" },
	{ "hangemhigh", "Hang 'Em High" }, { "longest", "Longest" }, { "prisoner", "Prisoner" },
	{ "putput", "Chiron TL-34" }, { "ratrace", "Rat Race" }, { "sidewinder", "Sidewinder" }, { "wizard", "Wizard" },
	{ "dangercanyon", "Danger Canyon" }, { "deathisland", "Death Island" }, { "gephyrophobia", "Gephyrophobia" },
	{ "icefields", "Ice Fields" }, { "infinity", "Infinity" }, { "timberland", "Timberland" },
};

/* ---------- private code */

static long *total_of(struct service_record *record, size_t index)
{
	long *const fields[] = {
		&record->games, &record->wins, &record->kills, &record->deaths, &record->assists, &record->betrayals,
		&record->suicides, &record->seconds, &record->best_spree,
	};

	return fields[index];
}

static long clamp_count(long value)
{
	if (value < 0)
		return 0;
	return value > SERVICE_RECORD_MAXIMUM_COUNT ? SERVICE_RECORD_MAXIMUM_COUNT : value;
}

static long add_count(long total, long value)
{
	if (value <= 0)
		return clamp_count(total);
	return total >= SERVICE_RECORD_MAXIMUM_COUNT - value ? SERVICE_RECORD_MAXIMUM_COUNT : total + value;
}

/* a name as kept: printable ASCII, no spaces at its ends, lowercase */
static void clean_name(char const *name, char *text, int size)
{
	int length = 0;

	if (size <= 0)
		return;
	while (name && (*name == ' ' || *name == '\t'))
		name++;
	for (; name && *name && length < size - 1; name++)
	{
		char character = *name;

		if ((unsigned char)character < 0x20 || (unsigned char)character >= 0x7F)
			continue;
		if (character >= 'A' && character <= 'Z')
			character = (char)(character - 'A' + 'a');
		text[length++] = character;
	}
	while (length > 0 && text[length - 1] == ' ')
		length--;
	text[length] = 0;
}

/* a name's count added to a table (a new name taken while there is room) */
static void add_named(struct service_record_count *table, int *count, int maximum, char const *name, long value)
{
	char clean[SERVICE_RECORD_NAME_LENGTH];
	int index;

	clean_name(name, clean, sizeof(clean));
	if (!clean[0] || value <= 0)
		return;
	for (index = 0; index < *count; index++)
	{
		if (!strcmp(table[index].name, clean))
		{
			table[index].count = add_count(table[index].count, value);
			return;
		}
	}
	if (*count >= maximum)
		return;
	strcpy(table[*count].name, clean);
	table[*count].count = clamp_count(value);
	(*count)++;
}

static char const *most(struct service_record_count const *table, int count)
{
	int index, best = -1;

	for (index = 0; index < count; index++)
	{
		if (table[index].count > 0 && (best < 0 || table[index].count > table[best].count ||
			(table[index].count == table[best].count && strcmp(table[index].name, table[best].name) < 0)))
		{
			best = index;
		}
	}
	return best >= 0 ? table[best].name : "";
}

/* a map's key: its path's last part ("levels\test\bloodgulch\bloodgulch"
is "bloodgulch") */
static void map_key(char const *map, char *text, int size)
{
	char const *last = map ? map : "";
	char const *at;

	for (at = last; *at; at++)
	{
		if (*at == '\\' || *at == '/')
			last = at + 1;
	}
	clean_name(last, text, size);
}

/* a line of the file ("word value", then the name if any) appended to
text; the length used, -1 once it does not fit */
static int append_line(char *text, int size, int used, char const *word, long value, char const *name)
{
	int written;

	if (used < 0 || used >= size)
		return -1;
	written = snprintf(text + used, (size_t)(size - used), "%s %ld%s%s\n", word, value, name ? " " : "",
		name ? name : "");
	if (written < 0 || written >= size - used)
		return -1;
	return used + written;
}

/* ---------- public code */

void service_record_clear(struct service_record *record)
{
	memset(record, 0, sizeof(*record));
}

void service_record_add_game(struct service_record *record, struct service_record_game const *game)
{
	char key[SERVICE_RECORD_NAME_LENGTH];
	int index;

	record->games = add_count(record->games, 1);
	if (game->won)
		record->wins = add_count(record->wins, 1);
	record->kills = add_count(record->kills, game->kills);
	record->deaths = add_count(record->deaths, game->deaths);
	record->assists = add_count(record->assists, game->assists);
	record->betrayals = add_count(record->betrayals, game->betrayals);
	record->suicides = add_count(record->suicides, game->suicides);
	record->seconds = add_count(record->seconds, game->seconds);
	if (game->best_spree > record->best_spree)
		record->best_spree = clamp_count(game->best_spree);
	for (index = 0; index < NUMBER_OF_MEDALS; index++)
		record->medals[index] = add_count(record->medals[index], game->medals[index]);
	map_key(game->map, key, sizeof(key));
	add_named(record->maps, &record->map_count, SERVICE_RECORD_MAPS, key, 1);
	for (index = 0; game->weapons && index < game->weapon_count; index++)
	{
		service_record_weapon_family(game->weapons[index].path, key, sizeof(key));
		add_named(record->weapons, &record->weapon_count, SERVICE_RECORD_WEAPONS, key, game->weapons[index].kills);
	}
}

int service_record_format(struct service_record const *record, char *text, int size)
{
	int used = 0;
	size_t index;
	int item;

	used = append_line(text, size, used, "service_record", SERVICE_RECORD_VERSION, NULL);
	for (index = 0; index < sizeof(totals) / sizeof(totals[0]); index++)
		used = append_line(text, size, used, totals[index], *total_of((struct service_record *)record, index), NULL);
	for (item = 0; item < NUMBER_OF_MEDALS; item++)
	{
		if (record->medals[item] > 0)
			used = append_line(text, size, used, "medal", record->medals[item], medal_rules_key(item));
	}
	for (item = 0; item < record->map_count; item++)
		used = append_line(text, size, used, "map", record->maps[item].count, record->maps[item].name);
	for (item = 0; item < record->weapon_count; item++)
		used = append_line(text, size, used, "weapon", record->weapons[item].count, record->weapons[item].name);
	return used;
}

int service_record_parse(struct service_record *record, char const *text)
{
	char const *line = text;
	int known = 0;

	service_record_clear(record);
	while (line && *line)
	{
		char const *end = strchr(line, '\n');
		size_t length = end ? (size_t)(end - line) : strlen(line);
		char buffer[128], *word, *name, *number;
		long value;

		if (length >= sizeof(buffer))
			length = sizeof(buffer) - 1;
		memcpy(buffer, line, length);
		buffer[length] = 0;
		if (length && buffer[length - 1] == '\r')
			buffer[length - 1] = 0;
		line = end ? end + 1 : NULL;
		/* (the word, its value, then the name, if any) */
		word = buffer;
		number = strchr(word, ' ');
		if (!number)
			continue;
		*number++ = 0;
		value = strtol(number, &name, 10);
		if (name == number)
			continue;
		while (*name == ' ')
			name++;
		value = clamp_count(value);
		if (!strcmp(word, "service_record"))
			known = 1;
		else if (!strcmp(word, "medal"))
		{
			int medal = medal_rules_find(name);

			if (medal >= 0)
				record->medals[medal] = value;
		}
		else if (!strcmp(word, "map"))
			add_named(record->maps, &record->map_count, SERVICE_RECORD_MAPS, name, value);
		else if (!strcmp(word, "weapon"))
			add_named(record->weapons, &record->weapon_count, SERVICE_RECORD_WEAPONS, name, value);
		else
		{
			size_t index;

			for (index = 0; index < sizeof(totals) / sizeof(totals[0]); index++)
			{
				if (!strcmp(word, totals[index]))
					*total_of(record, index) = value;
			}
		}
	}
	if (!known)
		service_record_clear(record);
	return known;
}

int service_record_load(char const *path, struct service_record *record)
{
	FILE *file;
	char *text;
	size_t length;
	int read;

	service_record_clear(record);
	file = fopen(path, "rb");
	if (!file)
		return 1;
	text = (char *)malloc(SERVICE_RECORD_MAXIMUM_FILE + 1);
	if (!text)
	{
		fclose(file);
		return 0;
	}
	length = fread(text, 1, SERVICE_RECORD_MAXIMUM_FILE, file);
	fclose(file);
	text[length] = 0;
	read = service_record_parse(record, text);
	free(text);
	return read;
}

int service_record_save(char const *path, struct service_record const *record)
{
	char temporary[1024];
	char *text;
	int length, written;
	FILE *file;

	if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary))
		return 0;
	text = (char *)malloc(SERVICE_RECORD_MAXIMUM_FILE);
	if (!text)
		return 0;
	length = service_record_format(record, text, SERVICE_RECORD_MAXIMUM_FILE);
	file = length >= 0 ? fopen(temporary, "wb") : NULL;
	if (!file)
	{
		free(text);
		return 0;
	}
	written = fwrite(text, 1, (size_t)length, file) == (size_t)length;
	written = fclose(file) == 0 && written;
	free(text);
	if (!written)
	{
		remove(temporary);
		return 0;
	}
	/* (Windows' rename keeps a file that is there) */
	remove(path);
	return rename(temporary, path) == 0;
}

long service_record_medal_total(struct service_record const *record)
{
	long total = 0;
	int index;

	for (index = 0; index < NUMBER_OF_MEDALS; index++)
		total = add_count(total, record->medals[index]);
	return total;
}

int service_record_top_medals(struct service_record const *record, int *medals, long *counts, int maximum)
{
	unsigned char used[NUMBER_OF_MEDALS];
	int found = 0;

	memset(used, 0, sizeof(used));
	while (found < maximum)
	{
		int index, best = -1;

		for (index = 0; index < NUMBER_OF_MEDALS; index++)
		{
			if (!used[index] && record->medals[index] > 0 && (best < 0 || record->medals[index] > record->medals[best]))
				best = index;
		}
		if (best < 0)
			break;
		used[best] = 1;
		medals[found] = best;
		counts[found++] = record->medals[best];
	}
	return found;
}

char const *service_record_favorite_map(struct service_record const *record)
{
	return most(record->maps, record->map_count);
}

char const *service_record_favorite_weapon(struct service_record const *record)
{
	return most(record->weapons, record->weapon_count);
}

void service_record_map_title(char const *map, char *text, int size)
{
	char key[SERVICE_RECORD_NAME_LENGTH];
	size_t index;

	map_key(map, key, sizeof(key));
	for (index = 0; index < sizeof(map_titles) / sizeof(map_titles[0]); index++)
	{
		if (!strcmp(map_titles[index][0], key))
		{
			snprintf(text, (size_t)size, "%s", map_titles[index][1]);
			return;
		}
	}
	service_record_title(key, text, size);
}

void service_record_weapon_family(char const *path, char *text, int size)
{
	char parts[3][SERVICE_RECORD_NAME_LENGTH];
	int part = 0, length = 0;

	text[0] = 0;
	memset(parts, 0, sizeof(parts));
	for (; path && *path && part < 3; path++)
	{
		if (*path == '\\' || *path == '/')
		{
			part++;
			length = 0;
			continue;
		}
		if (length < SERVICE_RECORD_NAME_LENGTH - 1)
			parts[part][length++] = *path;
	}
	/* (weapons\<family>\..., vehicles\<family>\...: anything else, as the
	game's own effects, is no weapon) */
	if (parts[1][0] && (!strcmp(parts[0], "weapons") || !strcmp(parts[0], "vehicles")))
		clean_name(parts[1], text, size);
}

void service_record_title(char const *name, char *text, int size)
{
	int length = 0, start = 1;

	if (size <= 0)
		return;
	for (; name && *name && length < size - 1; name++)
	{
		char character = *name == '_' ? ' ' : *name;

		if (start && character >= 'a' && character <= 'z')
			character = (char)(character - 'a' + 'A');
		start = character == ' ';
		text[length++] = character;
	}
	text[length] = 0;
}
