/*
DELTA_PROFILE.C

The site's answers about Delta profiles, read (delta_profile.h). A small
JSON reader of its own: the answers are a few kilobytes, read once in a
while, into fixed fields; whatever does not fit is cut, whatever is not
known is skipped, and text keeps no control characters.
*/

#ifdef HALO_GAME_BROWSER

#include "delta_profile.h"
#include "social.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- the JSON reader */

struct reader
{
	const char *at;
	int failed;
};

static void space(struct reader *reader)
{
	while (*reader->at == ' ' || *reader->at == '\t' || *reader->at == '\r' || *reader->at == '\n')
		reader->at++;
}

/* the next character is c: taken */
static int take(struct reader *reader, char c)
{
	space(reader);
	if (*reader->at != c)
		return 0;
	reader->at++;
	return 1;
}

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* a character put in text as UTF-8, if it fits whole */
static void put_character(char *text, int size, int *used, unsigned int character)
{
	char bytes[4];
	int count, index;

	if (character < 0x20 || character == 0x7F)
		return;
	if (character < 0x80)
	{
		bytes[0] = (char)character;
		count = 1;
	}
	else if (character < 0x800)
	{
		bytes[0] = (char)(0xC0 | (character >> 6));
		bytes[1] = (char)(0x80 | (character & 0x3F));
		count = 2;
	}
	else
	{
		bytes[0] = (char)(0xE0 | (character >> 12));
		bytes[1] = (char)(0x80 | ((character >> 6) & 0x3F));
		bytes[2] = (char)(0x80 | (character & 0x3F));
		count = 3;
	}
	if (*used + count >= size)
		return;
	for (index = 0; index < count; index++)
		text[(*used)++] = bytes[index];
}

/* a string into text (NULL: skipped), cut to size without cutting a
character in two */
static int string_value(struct reader *reader, char *text, int size)
{
	int used = 0;

	if (!take(reader, '"'))
	{
		reader->failed = 1;
		return 0;
	}
	while (*reader->at && *reader->at != '"')
	{
		unsigned int character = (unsigned char)*reader->at++;

		if (character == '\\')
		{
			char escape = *reader->at++;

			switch (escape)
			{
			case 'n': case 'r': case 't': case 'b': case 'f': character = ' '; break;
			case 'u':
			{
				int index, digit;

				character = 0;
				for (index = 0; index < 4; index++)
				{
					digit = hex_digit(reader->at[index]);
					if (digit < 0)
					{
						reader->failed = 1;
						return 0;
					}
					character = character << 4 | (unsigned int)digit;
				}
				reader->at += 4;
				/* (a character past the BMP, a surrogate pair: a question mark) */
				if (character >= 0xD800 && character <= 0xDFFF)
				{
					if (character < 0xDC00 && reader->at[0] == '\\' && reader->at[1] == 'u')
						reader->at += 6;
					character = '?';
				}
				break;
			}
			case 0:
				reader->failed = 1;
				return 0;
			default: character = (unsigned char)escape; break;
			}
			if (text)
				put_character(text, size, &used, character);
			continue;
		}
		if (!text)
			continue;
		/* (UTF-8 as it is, whole characters only) */
		if (character >= 0x80)
		{
			int count = (character & 0xE0) == 0xC0 ? 2 : (character & 0xF0) == 0xE0 ? 3 : (character & 0xF8) == 0xF0 ? 4 : 1;
			int index;

			for (index = 1; index < count && ((unsigned char)reader->at[index - 1] & 0xC0) == 0x80; index++)
				;
			if (index < count || count == 1 || used + count >= size)
			{
				reader->at += index - 1;
				continue;
			}
			text[used++] = (char)character;
			for (index = 1; index < count; index++)
				text[used++] = *reader->at++;
			continue;
		}
		put_character(text, size, &used, character);
	}
	if (*reader->at != '"')
	{
		reader->failed = 1;
		return 0;
	}
	reader->at++;
	if (text && size > 0)
		text[used] = 0;
	return 1;
}

static int literal(struct reader *reader, const char *word)
{
	size_t length = strlen(word);

	space(reader);
	if (strncmp(reader->at, word, length))
		return 0;
	reader->at += length;
	return 1;
}

static void skip_value(struct reader *reader, int depth);

/* a number (null and false: 0, true: 1) */
static long long number_value(struct reader *reader)
{
	char *end;
	double value;

	space(reader);
	if (literal(reader, "null") || literal(reader, "false"))
		return 0;
	if (literal(reader, "true"))
		return 1;
	value = strtod(reader->at, &end);
	if (end == reader->at)
	{
		skip_value(reader, 0);
		return 0;
	}
	reader->at = end;
	/* (a number past what a long long holds, or not a number at all, is
	never cast as it is: that is undefined) */
	if (value != value)
		return 0;
	if (value >= 9.2e18)
		return LLONG_MAX;
	if (value <= -9.2e18)
		return LLONG_MIN;
	return (long long)value;
}

/* a number for an int field: past an int's range, held at its end */
static int int_value(struct reader *reader)
{
	long long value = number_value(reader);

	return value > INT_MAX ? INT_MAX : value < INT_MIN ? INT_MIN : (int)value;
}

static int boolean_value(struct reader *reader)
{
	return number_value(reader) != 0;
}

/* a string's text, or none for null (or another kind of value) */
static void text_value(struct reader *reader, char *text, int size)
{
	space(reader);
	text[0] = 0;
	if (*reader->at == '"')
		string_value(reader, text, size);
	else
		skip_value(reader, 0);
}

/* an object's next member: its name in key (1), or its end (0) */
static int member(struct reader *reader, char *key, int size, int *first)
{
	space(reader);
	if (take(reader, '}'))
		return 0;
	if (!*first && !take(reader, ','))
	{
		reader->failed = 1;
		return 0;
	}
	*first = 0;
	if (!string_value(reader, key, size) || !take(reader, ':'))
	{
		reader->failed = 1;
		return 0;
	}
	return 1;
}

/* an array's next item (1, its value next), or its end (0) */
static int item(struct reader *reader, int *first)
{
	space(reader);
	if (take(reader, ']'))
		return 0;
	if (!*first && !take(reader, ','))
	{
		reader->failed = 1;
		return 0;
	}
	*first = 0;
	return 1;
}

static void skip_value(struct reader *reader, int depth)
{
	char key[8];
	int first = 1;

	space(reader);
	if (depth > 16 || reader->failed)
	{
		reader->failed = 1;
		return;
	}
	if (take(reader, '{'))
	{
		while (!reader->failed && member(reader, key, sizeof(key), &first))
			skip_value(reader, depth + 1);
	}
	else if (take(reader, '['))
	{
		while (!reader->failed && item(reader, &first))
			skip_value(reader, depth + 1);
	}
	else if (*reader->at == '"')
		string_value(reader, NULL, 0);
	else if (!literal(reader, "null") && !literal(reader, "true") && !literal(reader, "false"))
	{
		char *end;

		strtod(reader->at, &end);
		if (end == reader->at)
			reader->failed = 1;
		reader->at = end;
	}
}

/* ---------- the profile */

/* the record's "medal_counts" ({key: count}): the most earned kept, most
first (ties: the order they came in) */
static void read_medal_counts(struct reader *reader, struct delta_profile *profile)
{
	char key[32];
	int first = 1;

	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		int count = int_value(reader);
		int place = profile->record_top_medal_count;

		if (count <= 0 || !key[0])
			continue;
		while (place > 0 && profile->record_top_medals[place - 1].count < count)
			place--;
		if (place >= DELTA_PROFILE_TOP_MEDALS)
			continue;
		if (profile->record_top_medal_count < DELTA_PROFILE_TOP_MEDALS)
			profile->record_top_medal_count++;
		memmove(&profile->record_top_medals[place + 1], &profile->record_top_medals[place],
			(size_t)(profile->record_top_medal_count - 1 - place) * sizeof(profile->record_top_medals[0]));
		snprintf(profile->record_top_medals[place].key, sizeof(profile->record_top_medals[place].key), "%s", key);
		profile->record_top_medals[place].count = count;
	}
}

static void read_record(struct reader *reader, struct delta_profile *profile)
{
	char key[32];
	int first = 1;

	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	profile->has_record = 1;
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "games"))
			profile->record_games = (int)number_value(reader);
		else if (!strcmp(key, "wins"))
			profile->record_wins = (int)number_value(reader);
		else if (!strcmp(key, "kills"))
			profile->record_kills = (int)number_value(reader);
		else if (!strcmp(key, "deaths"))
			profile->record_deaths = (int)number_value(reader);
		else if (!strcmp(key, "assists"))
			profile->record_assists = (int)number_value(reader);
		else if (!strcmp(key, "medals"))
			profile->record_medals = (int)number_value(reader);
		else if (!strcmp(key, "seconds"))
			profile->record_seconds = number_value(reader);
		else if (!strcmp(key, "best_type"))
			text_value(reader, profile->record_best_type, sizeof(profile->record_best_type));
		else if (!strcmp(key, "best_spree"))
			profile->record_best_spree = int_value(reader);
		else if (!strcmp(key, "favorite_map"))
			text_value(reader, profile->record_favorite_map, sizeof(profile->record_favorite_map));
		else if (!strcmp(key, "favorite_weapon"))
			text_value(reader, profile->record_favorite_weapon, sizeof(profile->record_favorite_weapon));
		else if (!strcmp(key, "medal_counts"))
			read_medal_counts(reader, profile);
		else
			skip_value(reader, 1);
	}
}

static void read_game(struct reader *reader, struct delta_profile_game *game)
{
	char key[32], tier[16];
	int first = 1;

	memset(game, 0, sizeof(*game));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "time"))
			game->time = number_value(reader);
		else if (!strcmp(key, "map"))
			text_value(reader, game->map, sizeof(game->map));
		else if (!strcmp(key, "type"))
			text_value(reader, game->type, sizeof(game->type));
		else if (!strcmp(key, "result"))
			text_value(reader, game->result, sizeof(game->result));
		else if (!strcmp(key, "kills"))
			game->kills = (int)number_value(reader);
		else if (!strcmp(key, "deaths"))
			game->deaths = (int)number_value(reader);
		else if (!strcmp(key, "tier"))
		{
			text_value(reader, tier, sizeof(tier));
			game->observed = !strcmp(tier, "player");
		}
		else
			skip_value(reader, 1);
	}
}

static void read_copy(struct reader *reader, struct delta_profile_copy *copy)
{
	char key[32];
	int first = 1;

	memset(copy, 0, sizeof(*copy));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "label"))
			text_value(reader, copy->label, sizeof(copy->label));
		else if (!strcmp(key, "platform"))
			text_value(reader, copy->platform, sizeof(copy->platform));
		else if (!strcmp(key, "linked"))
			copy->linked = number_value(reader);
		else if (!strcmp(key, "last_seen"))
			copy->last_seen = number_value(reader);
		else if (!strcmp(key, "this"))
			copy->this_copy = boolean_value(reader);
		else if (!strcmp(key, "first"))
			copy->first = boolean_value(reader);
		else
			skip_value(reader, 1);
	}
}

static void read_device(struct reader *reader, struct delta_profile_device *device)
{
	char key[32];
	int first = 1;

	memset(device, 0, sizeof(*device));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "id"))
			text_value(reader, device->id, sizeof(device->id));
		else if (!strcmp(key, "name"))
			text_value(reader, device->name, sizeof(device->name));
		else if (!strcmp(key, "platform"))
			text_value(reader, device->platform, sizeof(device->platform));
		else if (!strcmp(key, "created"))
			device->created = number_value(reader);
		else if (!strcmp(key, "last_seen"))
			device->last_seen = number_value(reader);
		else if (!strcmp(key, "legacy"))
			device->legacy = boolean_value(reader);
		else if (!strcmp(key, "console"))
			device->console = boolean_value(reader);
		else if (!strcmp(key, "signed_out"))
			device->signed_out = boolean_value(reader);
		else if (!strcmp(key, "this"))
			device->this_device = boolean_value(reader);
		else
			skip_value(reader, 1);
	}
}

static void read_role(struct reader *reader, struct delta_profile_role *role)
{
	char key[32];
	int first = 1;

	memset(role, 0, sizeof(*role));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "name"))
			text_value(reader, role->name, sizeof(role->name));
		else if (!strcmp(key, "role"))
			text_value(reader, role->role, sizeof(role->role));
		else if (!strcmp(key, "source"))
			text_value(reader, role->source, sizeof(role->source));
		else if (!strcmp(key, "owner"))
			text_value(reader, role->owner, sizeof(role->owner));
		else if (!strcmp(key, "actions"))
			text_value(reader, role->actions, sizeof(role->actions));
		else if (!strcmp(key, "cached"))
			role->cached = boolean_value(reader);
		else if (!strcmp(key, "online"))
			role->online = boolean_value(reader);
		else
			skip_value(reader, 1);
	}
}

/* a badge's name ({"id", "name", "icon"}) */
static void read_badge(struct reader *reader, char *name, int size)
{
	char key[16];
	int first = 1;

	name[0] = 0;
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "name"))
			text_value(reader, name, size);
		else
			skip_value(reader, 1);
	}
}

static void read_privacy(struct reader *reader, struct delta_profile *profile)
{
	char key[32];
	int first = 1;

	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	profile->has_privacy = 1;
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "hide_stats"))
			profile->hide_stats = boolean_value(reader);
		else if (!strcmp(key, "hide_heatmaps"))
			profile->hide_heatmaps = boolean_value(reader);
		else if (!strcmp(key, "show_presence"))
			profile->show_presence = boolean_value(reader);
		else if (!strcmp(key, "show_clan_tag"))
			profile->show_clan_tag = boolean_value(reader);
		else
			skip_value(reader, 1);
	}
}

/* an array's items, each read by one of the readers here into a slot of
its own while there is one (the rest skipped); their count */
#define READ_ARRAY(reader, maximum, count, read_one) \
	do \
	{ \
		int array_first = 1; \
		(count) = 0; \
		if (!take((reader), '[')) \
		{ \
			skip_value((reader), 0); \
			break; \
		} \
		while (!(reader)->failed && item((reader), &array_first)) \
		{ \
			if ((count) < (maximum)) \
			{ \
				read_one; \
				(count)++; \
			} \
			else \
				skip_value((reader), 1); \
		} \
	} while (0)

/* a clan's emblem ({"fg", "bg", "c1", "c2", "c3"}) */
static void read_emblem(struct reader *reader, int *emblem)
{
	static const char *const keys[5] = { "fg", "bg", "c1", "c2", "c3" };
	char key[8];
	int first = 1, index;

	memset(emblem, 0, 5 * sizeof(emblem[0]));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		for (index = 0; index < 5 && strcmp(key, keys[index]); index++)
			;
		if (index < 5)
			emblem[index] = (int)number_value(reader);
		else
			skip_value(reader, 1);
	}
}

static void read_social_game(struct reader *reader, struct social_game *game);

static void read_clan_member(struct reader *reader, struct delta_profile_clan_member *line)
{
	char key[16];
	int first = 1;

	memset(line, 0, sizeof(*line));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "display"))
			text_value(reader, line->display, sizeof(line->display));
		else if (!strcmp(key, "handle"))
			text_value(reader, line->handle, sizeof(line->handle));
		else if (!strcmp(key, "rank"))
			text_value(reader, line->rank, sizeof(line->rank));
		else if (!strcmp(key, "you"))
			line->you = boolean_value(reader);
		else if (!strcmp(key, "online"))
			line->online = boolean_value(reader);
		else if (!strcmp(key, "playing"))
			line->playing = boolean_value(reader);
		else if (!strcmp(key, "game"))
			read_social_game(reader, &line->game);
		else
			skip_value(reader, 1);
	}
}

/* the profile's clan, or null (*has_clan 0) */
static void read_clan(struct reader *reader, struct delta_profile_clan *clan, int *has_clan)
{
	char key[16];
	int first = 1;

	memset(clan, 0, sizeof(*clan));
	*has_clan = 0;
	space(reader);
	if (literal(reader, "null"))
		return;
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "id"))
			clan->id = (int)number_value(reader);
		else if (!strcmp(key, "name"))
			text_value(reader, clan->name, sizeof(clan->name));
		else if (!strcmp(key, "tag"))
			text_value(reader, clan->tag, sizeof(clan->tag));
		else if (!strcmp(key, "motto"))
			text_value(reader, clan->motto, sizeof(clan->motto));
		else if (!strcmp(key, "emblem"))
			read_emblem(reader, clan->emblem);
		else if (!strcmp(key, "created"))
			clan->created = number_value(reader);
		else if (!strcmp(key, "rank"))
			text_value(reader, clan->rank, sizeof(clan->rank));
		else if (!strcmp(key, "count"))
			clan->count = (int)number_value(reader);
		else if (!strcmp(key, "hidden"))
			clan->hidden = (int)number_value(reader);
		else if (!strcmp(key, "more"))
			clan->more = (int)number_value(reader);
		else if (!strcmp(key, "members"))
			READ_ARRAY(reader, DELTA_PROFILE_CLAN_MEMBERS, clan->member_count,
				read_clan_member(reader, &clan->members[clan->member_count]));
		else
			skip_value(reader, 1);
	}
	*has_clan = clan->name[0] && clan->tag[0];
}

static void read_invite(struct reader *reader, struct delta_profile_invite *invite)
{
	char key[16];
	int first = 1;

	memset(invite, 0, sizeof(*invite));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "id"))
			invite->id = (int)number_value(reader);
		else if (!strcmp(key, "name"))
			text_value(reader, invite->name, sizeof(invite->name));
		else if (!strcmp(key, "tag"))
			text_value(reader, invite->tag, sizeof(invite->tag));
		else if (!strcmp(key, "count"))
			invite->count = (int)number_value(reader);
		else
			skip_value(reader, 1);
	}
}

/* an invite a clan has out ({"handle", "time"}): its handle */
static void read_invited(struct reader *reader, char *handle, int size)
{
	char key[16];
	int first = 1;

	handle[0] = 0;
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "handle"))
			text_value(reader, handle, size);
		else
			skip_value(reader, 1);
	}
}

int delta_profile_parse(const char *text, struct delta_profile *profile)
{
	struct reader reader;
	char key[32];
	int first = 1, has_linked = 0;

	memset(profile, 0, sizeof(*profile));
	reader.at = text;
	reader.failed = 0;
	if (!take(&reader, '{'))
		return 0;
	while (!reader.failed && member(&reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "linked"))
		{
			profile->linked = boolean_value(&reader);
			has_linked = 1;
		}
		else if (!strcmp(key, "games"))
			profile->games = (int)number_value(&reader);
		else if (!strcmp(key, "locked"))
			profile->locked = boolean_value(&reader);
		else if (!strcmp(key, "hidden"))
			profile->hidden = boolean_value(&reader);
		else if (!strcmp(key, "handle"))
			text_value(&reader, profile->handle, sizeof(profile->handle));
		else if (!strcmp(key, "display"))
			text_value(&reader, profile->display, sizeof(profile->display));
		else if (!strcmp(key, "created"))
			profile->created = number_value(&reader);
		else if (!strcmp(key, "badges"))
			READ_ARRAY(&reader, DELTA_PROFILE_BADGES, profile->badge_count,
				read_badge(&reader, profile->badges[profile->badge_count], (int)sizeof(profile->badges[0])));
		else if (!strcmp(key, "record"))
			read_record(&reader, profile);
		else if (!strcmp(key, "recent"))
			READ_ARRAY(&reader, DELTA_PROFILE_RECENT, profile->recent_count,
				read_game(&reader, &profile->recent[profile->recent_count]));
		else if (!strcmp(key, "copies"))
			READ_ARRAY(&reader, DELTA_PROFILE_COPIES, profile->copy_count,
				read_copy(&reader, &profile->copies[profile->copy_count]));
		else if (!strcmp(key, "devices"))
		{
			profile->has_devices = 1;
			READ_ARRAY(&reader, DELTA_PROFILE_DEVICES, profile->device_count,
				read_device(&reader, &profile->devices[profile->device_count]));
		}
		else if (!strcmp(key, "more_devices"))
			profile->more_devices = (int)number_value(&reader);
		else if (!strcmp(key, "roles"))
			READ_ARRAY(&reader, DELTA_PROFILE_ROLES, profile->role_count,
				read_role(&reader, &profile->roles[profile->role_count]));
		else if (!strcmp(key, "more_copies"))
			profile->more_copies = (int)number_value(&reader);
		else if (!strcmp(key, "more_roles"))
			profile->more_roles = (int)number_value(&reader);
		else if (!strcmp(key, "privacy"))
			read_privacy(&reader, profile);
		else if (!strcmp(key, "clan"))
			read_clan(&reader, &profile->clan, &profile->has_clan);
		else if (!strcmp(key, "clan_invites"))
			READ_ARRAY(&reader, DELTA_PROFILE_CLAN_INVITES, profile->invite_count,
				read_invite(&reader, &profile->invites[profile->invite_count]));
		else
			skip_value(&reader, 1);
	}
	if (reader.failed || !has_linked || (profile->linked && !profile->handle[0]))
	{
		memset(profile, 0, sizeof(*profile));
		return 0;
	}
	return 1;
}

int delta_profile_parse_privacy(const char *text, struct delta_profile *profile)
{
	struct reader reader;
	struct delta_profile read;
	char key[32];
	int first = 1;

	memset(&read, 0, sizeof(read));
	reader.at = text;
	reader.failed = 0;
	if (!take(&reader, '{'))
		return 0;
	while (!reader.failed && member(&reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "privacy"))
			read_privacy(&reader, &read);
		else
			skip_value(&reader, 1);
	}
	if (reader.failed || !read.has_privacy)
		return 0;
	profile->has_privacy = 1;
	profile->hide_stats = read.hide_stats;
	profile->hide_heatmaps = read.hide_heatmaps;
	profile->show_presence = read.show_presence;
	profile->show_clan_tag = read.show_clan_tag;
	return 1;
}

int delta_profile_parse_clan(const char *text, struct delta_profile *profile)
{
	struct reader reader;
	struct delta_profile_clan clan;
	struct delta_profile_invite invites[DELTA_PROFILE_CLAN_INVITES];
	char invited[DELTA_PROFILE_CLAN_MEMBERS][24];
	char key[32];
	int first = 1, has_clan = 0, invite_count = 0, invited_count = 0, seen = 0;

	memset(&clan, 0, sizeof(clan));
	reader.at = text;
	reader.failed = 0;
	if (!take(&reader, '{'))
		return 0;
	while (!reader.failed && member(&reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "clan"))
		{
			read_clan(&reader, &clan, &has_clan);
			seen = 1;
		}
		else if (!strcmp(key, "clan_invites"))
			READ_ARRAY(&reader, DELTA_PROFILE_CLAN_INVITES, invite_count, read_invite(&reader, &invites[invite_count]));
		else if (!strcmp(key, "clan_invited"))
			READ_ARRAY(&reader, DELTA_PROFILE_CLAN_MEMBERS, invited_count,
				read_invited(&reader, invited[invited_count], (int)sizeof(invited[0])));
		else
			skip_value(&reader, 1);
	}
	if (reader.failed || !seen)
		return 0;
	profile->clan = clan;
	profile->has_clan = has_clan;
	profile->invite_count = invite_count;
	memcpy(profile->invites, invites, sizeof(invites));
	/* (a member's answer has none: none out) */
	profile->clan_invited_count = invited_count;
	memcpy(profile->clan_invited, invited, sizeof(invited));
	return 1;
}

static void read_card(struct reader *reader, struct delta_profile_card *card, int *has_card)
{
	char key[16];
	int first = 1;

	memset(card, 0, sizeof(*card));
	*has_card = 0;
	space(reader);
	if (literal(reader, "null"))
		return;
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "handle"))
			text_value(reader, card->handle, sizeof(card->handle));
		else if (!strcmp(key, "display"))
			text_value(reader, card->display, sizeof(card->display));
		else if (!strcmp(key, "looks_like"))
			text_value(reader, card->looks_like, sizeof(card->looks_like));
		else if (!strcmp(key, "clan"))
		{
			char clan_key[16];
			int first_clan = 1;

			/* ({"tag", "name"}, or null: no tag shown) */
			space(reader);
			if (literal(reader, "null"))
				continue;
			if (!take(reader, '{'))
			{
				skip_value(reader, 0);
				continue;
			}
			while (!reader->failed && member(reader, clan_key, sizeof(clan_key), &first_clan))
			{
				if (!strcmp(clan_key, "tag"))
					text_value(reader, card->clan_tag, sizeof(card->clan_tag));
				else
					skip_value(reader, 1);
			}
		}
		else if (!strcmp(key, "badges"))
		{
			char name[48];
			int first_badge = 1, count = 0;

			/* (the first badge only) */
			if (!take(reader, '['))
			{
				skip_value(reader, 0);
				continue;
			}
			while (!reader->failed && item(reader, &first_badge))
			{
				read_badge(reader, name, (int)sizeof(name));
				if (!count++)
					memcpy(card->badge, name, sizeof(card->badge));
			}
		}
		else
			skip_value(reader, 1);
	}
	*has_card = card->handle[0] != 0;
}

int delta_profile_parse_cards(const char *text, void (*found)(void *context, const char *id,
	const struct delta_profile_card *card), void *context)
{
	struct reader reader;
	char key[32], id[DELTA_PROFILE_ID_LENGTH + 8];
	int first = 1, count = 0, players = 0;

	reader.at = text;
	reader.failed = 0;
	if (!take(&reader, '{'))
		return -1;
	while (!reader.failed && member(&reader, key, sizeof(key), &first))
	{
		int first_player = 1;

		if (strcmp(key, "players") || !take(&reader, '{'))
		{
			skip_value(&reader, 1);
			continue;
		}
		players = 1;
		while (!reader.failed && member(&reader, id, sizeof(id), &first_player))
		{
			struct delta_profile_card card;
			int has_card;

			read_card(&reader, &card, &has_card);
			if (reader.failed)
				break;
			if (strlen(id) == DELTA_PROFILE_ID_LENGTH && strspn(id, "0123456789abcdef") == DELTA_PROFILE_ID_LENGTH)
			{
				found(context, id, has_card ? &card : NULL);
				count++;
			}
		}
	}
	return reader.failed || !players ? -1 : count;
}

/* ---------- the friends' state */

static void read_social_game(struct reader *reader, struct social_game *game)
{
	char key[32], mode[16];
	int first = 1;

	memset(game, 0, sizeof(*game));
	space(reader);
	if (!take(reader, '{'))
	{
		/* (null: not listed) */
		skip_value(reader, 0);
		return;
	}
	game->listed = 1;
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "name"))
			text_value(reader, game->name, sizeof(game->name));
		else if (!strcmp(key, "map"))
			text_value(reader, game->map, sizeof(game->map));
		else if (!strcmp(key, "engine"))
			game->engine = (int)number_value(reader);
		else if (!strcmp(key, "mode"))
		{
			text_value(reader, mode, sizeof(mode));
			game->coop = !strcmp(mode, "coop");
		}
		else if (!strcmp(key, "teams"))
			game->teams = boolean_value(reader);
		else if (!strcmp(key, "players"))
			game->players = (int)number_value(reader);
		else if (!strcmp(key, "maximum_players"))
			game->maximum_players = (int)number_value(reader);
		else
			skip_value(reader, 1);
	}
}

static void read_social_friend(struct reader *reader, struct social_friend *buddy)
{
	char key[32];
	int first = 1;

	memset(buddy, 0, sizeof(*buddy));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "handle"))
			text_value(reader, buddy->handle, sizeof(buddy->handle));
		else if (!strcmp(key, "display"))
			text_value(reader, buddy->display, sizeof(buddy->display));
		else if (!strcmp(key, "since"))
			buddy->since = number_value(reader);
		else if (!strcmp(key, "online"))
			buddy->online = boolean_value(reader);
		else if (!strcmp(key, "playing"))
			buddy->playing = boolean_value(reader);
		else if (!strcmp(key, "joinable"))
			buddy->joinable = boolean_value(reader);
		else if (!strcmp(key, "game"))
			read_social_game(reader, &buddy->game);
		else
			skip_value(reader, 1);
	}
}

static void read_social_request(struct reader *reader, struct social_request *request)
{
	char key[32];
	int first = 1;

	memset(request, 0, sizeof(*request));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "handle"))
			text_value(reader, request->handle, sizeof(request->handle));
		else if (!strcmp(key, "display"))
			text_value(reader, request->display, sizeof(request->display));
		else if (!strcmp(key, "time"))
			request->time = number_value(reader);
		else
			skip_value(reader, 1);
	}
}

static void read_social_recent(struct reader *reader, struct social_recent *player)
{
	char key[32];
	int first = 1;

	memset(player, 0, sizeof(*player));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "handle"))
			text_value(reader, player->handle, sizeof(player->handle));
		else if (!strcmp(key, "display"))
			text_value(reader, player->display, sizeof(player->display));
		else if (!strcmp(key, "time"))
			player->time = number_value(reader);
		else if (!strcmp(key, "map"))
			text_value(reader, player->map, sizeof(player->map));
		else if (!strcmp(key, "friend"))
			player->is_friend = boolean_value(reader);
		else if (!strcmp(key, "requested"))
			player->requested = boolean_value(reader);
		else
			skip_value(reader, 1);
	}
	/* (a handle the site could not have given is none) */
	if (!social_handle_valid(player->handle))
		player->handle[0] = 0;
}

static void read_social_invite(struct reader *reader, struct social_invite *invite)
{
	char key[32];
	int first = 1;

	memset(invite, 0, sizeof(*invite));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "id"))
			text_value(reader, invite->id, sizeof(invite->id));
		else if (!strcmp(key, "from"))
			text_value(reader, invite->from, sizeof(invite->from));
		else if (!strcmp(key, "display"))
			text_value(reader, invite->display, sizeof(invite->display));
		else if (!strcmp(key, "time"))
			invite->time = number_value(reader);
		else if (!strcmp(key, "expires"))
			invite->expires = number_value(reader);
		else if (!strcmp(key, "game"))
			read_social_game(reader, &invite->game);
		else
			skip_value(reader, 1);
	}
}

static void read_social_you(struct reader *reader, struct social_state *state)
{
	char key[32];
	int first = 1;

	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "handle"))
			text_value(reader, state->handle, sizeof(state->handle));
		else if (!strcmp(key, "display"))
			text_value(reader, state->display, sizeof(state->display));
		else if (!strcmp(key, "appear_offline"))
			state->appear_offline = boolean_value(reader);
		else
			skip_value(reader, 1);
	}
}

static void read_party_member(struct reader *reader, struct social_party_member *entry)
{
	char key[32];
	int first = 1;

	memset(entry, 0, sizeof(*entry));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "id"))
			text_value(reader, entry->id, sizeof(entry->id));
		else if (!strcmp(key, "name"))
			text_value(reader, entry->name, sizeof(entry->name));
		else if (!strcmp(key, "handle"))
			text_value(reader, entry->handle, sizeof(entry->handle));
		else if (!strcmp(key, "platform"))
			text_value(reader, entry->platform, sizeof(entry->platform));
		else if (!strcmp(key, "leader"))
			entry->leader = boolean_value(reader);
		else if (!strcmp(key, "you"))
			entry->you = boolean_value(reader);
		else
			skip_value(reader, 1);
	}
}

static void read_party_go(struct reader *reader, struct social_party_go *go)
{
	char key[32];
	int first = 1;

	memset(go, 0, sizeof(*go));
	space(reader);
	if (!take(reader, '{'))
	{
		/* (null: none) */
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "serial"))
			go->serial = number_value(reader);
		else if (!strcmp(key, "time"))
			go->time = number_value(reader);
		else if (!strcmp(key, "game"))
			read_social_game(reader, &go->game);
		else if (!strcmp(key, "join"))
			text_value(reader, go->join, sizeof(go->join));
		else if (!strcmp(key, "reason"))
			text_value(reader, go->reason, sizeof(go->reason));
		else if (!strcmp(key, "error"))
			text_value(reader, go->error, sizeof(go->error));
		else
			skip_value(reader, 1);
	}
	/* (a join that isn't a game's invite link is none) */
	if (go->join[0] && strncmp(go->join, "halo://join/", 12))
		go->join[0] = 0;
	go->has = go->serial > 0;
}

static void read_party(struct reader *reader, struct social_party *party)
{
	char key[32];
	int first = 1;

	memset(party, 0, sizeof(*party));
	space(reader);
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		int item_first = 1;

		if (!strcmp(key, "id"))
			text_value(reader, party->id, sizeof(party->id));
		else if (!strcmp(key, "code"))
			text_value(reader, party->code, sizeof(party->code));
		else if (!strcmp(key, "leader"))
			party->leader = boolean_value(reader);
		else if (!strcmp(key, "who_can_join"))
			text_value(reader, party->who_can_join, sizeof(party->who_can_join));
		else if (!strcmp(key, "largest"))
			party->largest = (int)number_value(reader);
		else if (!strcmp(key, "tag"))
			text_value(reader, party->tag, sizeof(party->tag));
		else if (!strcmp(key, "go"))
			read_party_go(reader, &party->go);
		else if (!strcmp(key, "members") && take(reader, '['))
		{
			struct social_party_member skipped;

			while (!reader->failed && item(reader, &item_first))
			{
				if (party->member_count < SOCIAL_PARTY_LARGEST)
				{
					read_party_member(reader, &party->members[party->member_count]);
					if (party->members[party->member_count].id[0])
						party->member_count++;
				}
				else
					read_party_member(reader, &skipped);
			}
		}
		else
			skip_value(reader, 1);
	}
	party->has = party->id[0] && party->member_count > 0;
	if (party->largest <= 0 || party->largest > SOCIAL_PARTY_LARGEST)
		party->largest = SOCIAL_PARTY_LARGEST;
}

static void read_party_invite(struct reader *reader, struct social_party_invite *invite)
{
	char key[32];
	int first = 1;

	memset(invite, 0, sizeof(*invite));
	if (!take(reader, '{'))
	{
		skip_value(reader, 0);
		return;
	}
	while (!reader->failed && member(reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "party"))
			text_value(reader, invite->party, sizeof(invite->party));
		else if (!strcmp(key, "from"))
			text_value(reader, invite->from, sizeof(invite->from));
		else if (!strcmp(key, "display"))
			text_value(reader, invite->display, sizeof(invite->display));
		else if (!strcmp(key, "size"))
			invite->size = (int)number_value(reader);
		else if (!strcmp(key, "time"))
			invite->time = number_value(reader);
		else if (!strcmp(key, "expires"))
			invite->expires = number_value(reader);
		else
			skip_value(reader, 1);
	}
}

/* "party" and "party_invites" (a state's, or /v1/party's answer): 1 if the
key was one of them */
static int read_party_member_of_state(struct reader *reader, const char *key, struct social_state *state)
{
	int item_first = 1;

	if (!strcmp(key, "party"))
	{
		read_party(reader, &state->party);
		return 1;
	}
	if (strcmp(key, "party_invites"))
		return 0;
	if (!take(reader, '['))
	{
		skip_value(reader, 1);
		return 1;
	}
	while (!reader->failed && item(reader, &item_first))
	{
		struct social_party_invite skipped;

		if (state->party_invite_count < SOCIAL_MAXIMUM_PARTY_INVITES)
		{
			read_party_invite(reader, &state->party_invites[state->party_invite_count]);
			if (state->party_invites[state->party_invite_count].party[0])
				state->party_invite_count++;
		}
		else
			read_party_invite(reader, &skipped);
	}
	return 1;
}

int delta_profile_parse_party(const char *text, struct social_state *state)
{
	struct reader reader;
	char key[32];
	int first = 1, found = 0;

	memset(&state->party, 0, sizeof(state->party));
	state->party_invite_count = 0;
	reader.at = text ? text : "";
	reader.failed = 0;
	if (!take(&reader, '{'))
		return 0;
	while (!reader.failed && member(&reader, key, sizeof(key), &first))
	{
		if (read_party_member_of_state(&reader, key, state))
			found |= !strcmp(key, "party");
		else
			skip_value(&reader, 1);
	}
	if (reader.failed || !found)
	{
		memset(&state->party, 0, sizeof(state->party));
		state->party_invite_count = 0;
		return 0;
	}
	return 1;
}

int delta_profile_parse_social(const char *text, struct social_state *state)
{
	struct reader reader;
	char key[32];
	int first = 1, same = 0, whole = 0;

	memset(state, 0, sizeof(*state));
	reader.at = text;
	reader.failed = 0;
	if (!take(&reader, '{'))
		return 0;
	while (!reader.failed && member(&reader, key, sizeof(key), &first))
	{
		int item_first = 1;

		if (!strcmp(key, "cursor"))
			text_value(&reader, state->cursor, sizeof(state->cursor));
		else if (!strcmp(key, "doorbell"))
			text_value(&reader, state->doorbell, sizeof(state->doorbell));
		else if (!strcmp(key, "same"))
			same = boolean_value(&reader);
		else if (!strcmp(key, "you"))
		{
			read_social_you(&reader, state);
			whole = 1;
		}
		else if (!strcmp(key, "friends") && take(&reader, '['))
		{
			struct social_friend skipped;

			while (!reader.failed && item(&reader, &item_first))
			{
				/* (past the most kept: read and let go) */
				if (state->friend_count < SOCIAL_MAXIMUM_FRIENDS)
				{
					read_social_friend(&reader, &state->friends[state->friend_count]);
					if (state->friends[state->friend_count].handle[0])
						state->friend_count++;
				}
				else
					read_social_friend(&reader, &skipped);
			}
		}
		else if (!strcmp(key, "invites") && take(&reader, '['))
		{
			struct social_invite skipped;

			while (!reader.failed && item(&reader, &item_first))
			{
				if (state->invite_count < SOCIAL_MAXIMUM_INVITES)
				{
					read_social_invite(&reader, &state->invites[state->invite_count]);
					if (state->invites[state->invite_count].id[0] && state->invites[state->invite_count].from[0])
						state->invite_count++;
				}
				else
					read_social_invite(&reader, &skipped);
			}
		}
		else if (!strcmp(key, "recent") && take(&reader, '['))
		{
			struct social_recent skipped;

			while (!reader.failed && item(&reader, &item_first))
			{
				if (state->recent_count < SOCIAL_MAXIMUM_RECENT)
				{
					read_social_recent(&reader, &state->recent[state->recent_count]);
					if (state->recent[state->recent_count].handle[0])
						state->recent_count++;
				}
				else
					read_social_recent(&reader, &skipped);
			}
		}
		else if (!strcmp(key, "requests") && take(&reader, '{'))
		{
			char part[16];
			int part_first = 1;

			while (!reader.failed && member(&reader, part, sizeof(part), &part_first))
			{
				if (strcmp(part, "incoming") || !take(&reader, '['))
				{
					skip_value(&reader, 2);
					continue;
				}
				item_first = 1;
				while (!reader.failed && item(&reader, &item_first))
				{
					struct social_request skipped;

					if (state->request_count < SOCIAL_MAXIMUM_REQUESTS)
					{
						read_social_request(&reader, &state->requests[state->request_count]);
						if (state->requests[state->request_count].handle[0])
							state->request_count++;
					}
					else
						read_social_request(&reader, &skipped);
				}
			}
		}
		else if (!read_party_member_of_state(&reader, key, state))
			skip_value(&reader, 1);
	}
	if (reader.failed || !state->cursor[0])
		return 0;
	/* (a doorbell is 32 of 0-9 and a-f, else none: it names an MQTT topic) */
	if (strlen(state->doorbell) != SOCIAL_DOORBELL_SIZE - 1 ||
		strspn(state->doorbell, "0123456789abcdef") != SOCIAL_DOORBELL_SIZE - 1)
		state->doorbell[0] = 0;
	if (same)
		return 2;
	return whole ? 1 : 0;
}

void delta_profile_parse_text(const char *answer, const char *name, char *text, int size)
{
	struct reader reader;
	char key[32];
	int first = 1;

	if (size <= 0)
		return;
	text[0] = 0;
	reader.at = answer ? answer : "";
	reader.failed = 0;
	if (!take(&reader, '{'))
		return;
	while (!reader.failed && member(&reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, name))
			text_value(&reader, text, size);
		else
			skip_value(&reader, 1);
	}
	if (reader.failed)
		text[0] = 0;
}

int delta_profile_parse_join(const char *text, char *link, int size)
{
	struct reader reader;
	char key[32];
	int first = 1;

	if (size <= 0)
		return 0;
	link[0] = 0;
	reader.at = text ? text : "";
	reader.failed = 0;
	if (!take(&reader, '{'))
		return 0;
	while (!reader.failed && member(&reader, key, sizeof(key), &first))
	{
		if (!strcmp(key, "join"))
			text_value(&reader, link, size);
		else
			skip_value(&reader, 1);
	}
	if (reader.failed || strncmp(link, "halo://join/", 12))
	{
		link[0] = 0;
		return 0;
	}
	return 1;
}

#endif
