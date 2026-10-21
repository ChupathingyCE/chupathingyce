/*
DELTA_PROFILE.C

The site's answers about Delta profiles, read (delta_profile.h). A small
JSON reader of its own: the answers are a few kilobytes, read once in a
while, into fixed fields; whatever does not fit is cut, whatever is not
known is skipped, and text keeps no control characters.
*/

#ifdef HALO_GAME_BROWSER

#include "delta_profile.h"

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
	return (long long)value;
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
		else if (!strcmp(key, "rank"))
			text_value(reader, line->rank, sizeof(line->rank));
		else if (!strcmp(key, "you"))
			line->you = boolean_value(reader);
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
	char key[32];
	int first = 1, has_clan = 0, invite_count = 0, seen = 0;

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
		else
			skip_value(&reader, 1);
	}
	if (reader.failed || !seen)
		return 0;
	profile->clan = clan;
	profile->has_clan = has_clan;
	profile->invite_count = invite_count;
	memcpy(profile->invites, invites, sizeof(invites));
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

#endif
