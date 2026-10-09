/*
CONTROL_ROLES.C

Delta Control's roles as bytes (control_roles.h).
*/

#include "control_roles.h"

#include "../src/command_line.h"

#include <stdio.h>
#include <string.h>

/* ---------- roles */

static const char *const role_names[CONTROL_ROLE_COUNT] = { "none", "moderator", "admin", "owner" };

unsigned int control_role_permissions(int role)
{
	unsigned int moderator = CONTROL_PERMISSION_VIEW | CONTROL_PERMISSION_WARN | CONTROL_PERMISSION_KICK |
		CONTROL_PERMISSION_BAN_TIMED;
	unsigned int admin = moderator | CONTROL_PERMISSION_BAN | CONTROL_PERMISSION_UNBAN | CONTROL_PERMISSION_MAP |
		CONTROL_PERMISSION_SETTINGS | CONTROL_PERMISSION_INVITE;

	switch (role)
	{
	case CONTROL_ROLE_MODERATOR: return moderator;
	case CONTROL_ROLE_ADMIN: return admin;
	case CONTROL_ROLE_OWNER: return admin | CONTROL_PERMISSION_ROLES;
	default: return 0;
	}
}

const char *control_role_name(int role)
{
	return role >= 0 && role < CONTROL_ROLE_COUNT ? role_names[role] : "none";
}

int control_role_parse(const char *text)
{
	int role;

	for (role = CONTROL_ROLE_MODERATOR; role < CONTROL_ROLE_COUNT; role++)
	{
		if (!strcmp(text, role_names[role]))
			return role;
	}
	return -1;
}

/* ---------- the commands' permissions */

/* a ban's duration as command_line_duration reads one (minutes alone, or
numbers each with a unit s m h d w; forever and permanent 0): its seconds,
or -1 if it is none. (Its own copy: command_line.c is the game's unit,
whose long is not this unit's on every build) */
static int64_t duration_seconds(const char *text)
{
	int64_t total = 0;
	int parts = 0;

	if (!strcmp(text, "forever") || !strcmp(text, "permanent"))
		return 0;
	while (*text)
	{
		int64_t number = 0;
		int64_t unit;
		int digits = 0;

		while (*text >= '0' && *text <= '9')
		{
			if (++digits > 9)
				return -1;
			number = number * 10 + (*text++ - '0');
		}
		if (!digits)
			return -1;
		switch (*text | 0x20)
		{
		case 's': unit = 1; text++; break;
		case 'm': unit = 60; text++; break;
		case 'h': unit = 60 * 60; text++; break;
		case 'd': unit = 24 * 60 * 60; text++; break;
		case 'w': unit = 7 * 24 * 60 * 60; text++; break;
		default:
			if (*text || parts)
				return -1;
			unit = 60;
			break;
		}
		total += number * unit;
		if (total > (int64_t)COMMAND_LINE_MAXIMUM_DURATION)
			return -1;
		parts++;
	}
	return total > 0 ? total : -1;
}

struct command_permission
{
	const char *name;
	unsigned int permission;
	/* it changes something */
	int changes;
	/* with no argument, a read (sv_name, sv_maxplayers show the value) */
	int read_without_argument;
};

static const struct command_permission command_permissions[] =
{
	{ "help", CONTROL_PERMISSION_VIEW, 0, 0 },
	{ "sv_status", CONTROL_PERMISSION_VIEW, 0, 0 },
	{ "sv_players", CONTROL_PERMISSION_VIEW, 0, 0 },
	{ "sv_banlist", CONTROL_PERMISSION_VIEW, 0, 0 },
	{ "sv_maps", CONTROL_PERMISSION_VIEW, 0, 0 },
	{ "sv_mapcycle", CONTROL_PERMISSION_VIEW, 0, 0 },
	{ "sv_playlists", CONTROL_PERMISSION_VIEW, 0, 0 },
	{ "sv_playlist", CONTROL_PERMISSION_VIEW, 0, 0 },
	{ "sv_gametypes", CONTROL_PERMISSION_VIEW, 0, 0 },
	{ "sv_gametype", CONTROL_PERMISSION_VIEW, 0, 0 },
	{ "sv_settings", CONTROL_PERMISSION_VIEW, 0, 0 },
	{ "sv_warn", CONTROL_PERMISSION_WARN, 1, 0 },
	{ "sv_kick", CONTROL_PERMISSION_KICK, 1, 0 },
	/* (sv_ban: BAN_TIMED or BAN by its duration, below) */
	{ "sv_ban", CONTROL_PERMISSION_BAN, 1, 0 },
	{ "sv_unban", CONTROL_PERMISSION_UNBAN, 1, 0 },
	{ "sv_map", CONTROL_PERMISSION_MAP, 1, 0 },
	{ "sv_mapcycle_next", CONTROL_PERMISSION_MAP, 1, 0 },
	{ "sv_end_game", CONTROL_PERMISSION_MAP, 1, 0 },
	{ "sv_mapcycle_add", CONTROL_PERMISSION_MAP, 1, 0 },
	{ "sv_mapcycle_del", CONTROL_PERMISSION_MAP, 1, 0 },
	{ "sv_playlist_new", CONTROL_PERMISSION_MAP, 1, 0 },
	{ "sv_playlist_add", CONTROL_PERMISSION_MAP, 1, 0 },
	{ "sv_playlist_remove", CONTROL_PERMISSION_MAP, 1, 0 },
	{ "sv_playlist_move", CONTROL_PERMISSION_MAP, 1, 0 },
	{ "sv_playlist_delete", CONTROL_PERMISSION_MAP, 1, 0 },
	{ "sv_playlist_use", CONTROL_PERMISSION_MAP, 1, 0 },
	{ "sv_playlist_save", CONTROL_PERMISSION_MAP, 1, 0 },
	{ "sv_maxplayers", CONTROL_PERMISSION_SETTINGS, 1, 1 },
	{ "sv_name", CONTROL_PERMISSION_SETTINGS, 1, 1 },
	{ "sv_set", CONTROL_PERMISSION_SETTINGS, 1, 0 },
	{ "sv_gametype_new", CONTROL_PERMISSION_SETTINGS, 1, 0 },
	{ "sv_gametype_set", CONTROL_PERMISSION_SETTINGS, 1, 0 },
	{ "sv_gametype_delete", CONTROL_PERMISSION_SETTINGS, 1, 0 },
	{ "sv_gametype_save", CONTROL_PERMISSION_SETTINGS, 1, 0 },
	{ "sv_mod_list", CONTROL_PERMISSION_ROLES, 0, 0 },
	{ "sv_mod_add", CONTROL_PERMISSION_ROLES, 1, 0 },
	{ "sv_mod_remove", CONTROL_PERMISSION_ROLES, 1, 0 },
	{ "sv_link", CONTROL_PERMISSION_ROLES, 1, 0 },
	{ "sv_unlink", CONTROL_PERMISSION_ROLES, 1, 0 },
	{ "sv_link_status", CONTROL_PERMISSION_VIEW, 0, 0 },
	{ "sv_admin_list", CONTROL_PERMISSION_CONSOLE, 0, 0 },
	{ "sv_admin_add", CONTROL_PERMISSION_CONSOLE, 1, 0 },
	{ "sv_admin_rotate", CONTROL_PERMISSION_CONSOLE, 1, 0 },
	{ "sv_admin_remove", CONTROL_PERMISSION_CONSOLE, 1, 0 },
	{ "sv_account_list", CONTROL_PERMISSION_CONSOLE, 0, 0 },
	{ "sv_account_invite", CONTROL_PERMISSION_CONSOLE, 1, 0 },
	{ "sv_account_role", CONTROL_PERMISSION_CONSOLE, 1, 0 },
	{ "sv_account_remove", CONTROL_PERMISSION_CONSOLE, 1, 0 },
	{ "sv_account_reset", CONTROL_PERMISSION_CONSOLE, 1, 0 },
	{ "sv_account_setup", CONTROL_PERMISSION_CONSOLE, 1, 0 },
};

unsigned int control_command_permission(const char *line, int *changes)
{
	struct command_line words;
	char problem[96];
	size_t index;

	if (changes)
		*changes = 0;
	if (!command_line_parse(line, &words, problem, sizeof(problem)) || !words.count)
		return 0;
	for (index = 0; index < sizeof(command_permissions) / sizeof(command_permissions[0]); index++)
	{
		const struct command_permission *command = &command_permissions[index];

		if (strcmp(command->name, words.words[0]))
			continue;
		if (command->read_without_argument && words.count == 1)
			return CONTROL_PERMISSION_VIEW;
		if (changes)
			*changes = command->changes;
		if (!strcmp(command->name, "sv_ban"))
		{
			int64_t seconds = words.count >= 3 ? duration_seconds(words.words[2]) : 0;

			/* (a ban for a while no longer than a moderator's; with no
			duration, or one that is not, for ever) */
			if (seconds > 0 && seconds <= (int64_t)CONTROL_ROLES_TIMED_BAN_MINUTES * 60)
			{
				return CONTROL_PERMISSION_BAN_TIMED;
			}
			return CONTROL_PERMISSION_BAN;
		}
		return command->permission;
	}
	return 0;
}

/* ---------- keys and names */

void control_key_text(const uint8_t key[CONTROL_KEY_BYTES], char text[CONTROL_KEY_TEXT + 1])
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < CONTROL_KEY_BYTES; index++)
	{
		text[2 * index] = digits[key[index] >> 4];
		text[2 * index + 1] = digits[key[index] & 15];
	}
	text[CONTROL_KEY_TEXT] = 0;
}

static int hex_value(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

int control_key_parse(const char *text, uint8_t key[CONTROL_KEY_BYTES])
{
	uint8_t parsed[CONTROL_KEY_BYTES];
	int index;

	if (strlen(text) != CONTROL_KEY_TEXT)
		return 0;
	for (index = 0; index < CONTROL_KEY_BYTES; index++)
	{
		int high = hex_value(text[2 * index]);
		int low = hex_value(text[2 * index + 1]);

		if (high < 0 || low < 0)
			return 0;
		parsed[index] = (uint8_t)(high << 4 | low);
	}
	memcpy(key, parsed, sizeof(parsed));
	return 1;
}

void control_key_short(const uint8_t key[CONTROL_KEY_BYTES], char text[9])
{
	char whole[CONTROL_KEY_TEXT + 1];

	control_key_text(key, whole);
	memcpy(text, whole, 8);
	text[8] = 0;
}

int control_display_name_valid(const char *text)
{
	size_t length = strlen(text);
	size_t index;
	int visible = 0;

	if (!length || length >= CONTROL_DISPLAY_NAME_SIZE)
		return 0;
	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)text[index];

		if (character < 0x20 || character > 0x7E || character == '"' || character == '\\')
			return 0;
		visible |= character != ' ';
	}
	return visible;
}

/* ---------- the moderators file */

int control_moderator_parse(const char *line, struct control_moderator *moderator, const char **reason)
{
	char role_text[16];
	char key_text[CONTROL_KEY_TEXT + 2];
	const char *cursor = line;
	size_t length;
	char name[CONTROL_DISPLAY_NAME_SIZE + 2];

	memset(moderator, 0, sizeof(*moderator));
	while (*cursor == ' ' || *cursor == '\t')
		cursor++;
	if (!*cursor || *cursor == '#' || *cursor == '\n' || *cursor == '\r')
		return 0;
	/* the role */
	length = strcspn(cursor, " \t\r\n");
	if (!length || length >= sizeof(role_text))
	{
		*reason = "the first word is a role: moderator, admin or owner";
		return -1;
	}
	memcpy(role_text, cursor, length);
	role_text[length] = 0;
	moderator->role = control_role_parse(role_text);
	if (moderator->role < 0)
	{
		*reason = "the first word is a role: moderator, admin or owner";
		return -1;
	}
	cursor += length;
	while (*cursor == ' ' || *cursor == '\t')
		cursor++;
	/* the key */
	length = strcspn(cursor, " \t\r\n");
	if (length != CONTROL_KEY_TEXT)
	{
		*reason = "the second word is a moderator key: 64 hex digits";
		return -1;
	}
	memcpy(key_text, cursor, length);
	key_text[length] = 0;
	if (!control_key_parse(key_text, moderator->key))
	{
		*reason = "the second word is a moderator key: 64 hex digits";
		return -1;
	}
	cursor += length;
	while (*cursor == ' ' || *cursor == '\t')
		cursor++;
	/* the name, the rest of the line (its end of line and trailing spaces
	gone) */
	length = strcspn(cursor, "\r\n");
	while (length && (cursor[length - 1] == ' ' || cursor[length - 1] == '\t'))
		length--;
	if (length)
	{
		if (length >= CONTROL_DISPLAY_NAME_SIZE)
		{
			*reason = "the name is 31 characters at most";
			return -1;
		}
		memcpy(name, cursor, length);
		name[length] = 0;
		if (!control_display_name_valid(name))
		{
			*reason = "the name is printable ASCII, without quotes or backslashes";
			return -1;
		}
		memcpy(moderator->name, name, length + 1);
	}
	return 1;
}

int control_moderator_line(const struct control_moderator *moderator, char *out, size_t size)
{
	char key[CONTROL_KEY_TEXT + 1];
	int length;

	if (moderator->role <= CONTROL_ROLE_NONE || moderator->role >= CONTROL_ROLE_COUNT ||
		(moderator->name[0] && !control_display_name_valid(moderator->name)))
	{
		return -1;
	}
	control_key_text(moderator->key, key);
	length = snprintf(out, size, "%-9s %s%s%s", control_role_name(moderator->role), key, moderator->name[0] ? " " : "",
		moderator->name);
	return length < 0 || (size_t)length >= size ? -1 : length;
}

/* ---------- how often a person acts */

/* (a change's units in the bucket: a minute's milliseconds) */
#define CHANGE ((int64_t)60000)

void control_actor_limiter_initialize(struct control_actor_limiter *limiter)
{
	memset(limiter, 0, sizeof(*limiter));
}

int control_actor_allowed(struct control_actor_limiter *limiter, const char *actor, int64_t now, int64_t *retry_after)
{
	int index;
	int found = -1;
	int oldest = 0;

	for (index = 0; index < CONTROL_ACTOR_SLOTS; index++)
	{
		if (limiter->slots[index].used && !strncmp(limiter->slots[index].actor, actor, CONTROL_ACTOR_NAME_SIZE - 1))
		{
			found = index;
			break;
		}
		if (!limiter->slots[index].used || (limiter->slots[oldest].used &&
			limiter->slots[index].last < limiter->slots[oldest].last))
		{
			oldest = index;
		}
	}
	if (found < 0)
	{
		/* (a new actor takes a free slot, else the one least recently used) */
		found = oldest;
		memset(&limiter->slots[found], 0, sizeof(limiter->slots[found]));
		limiter->slots[found].used = 1;
		snprintf(limiter->slots[found].actor, sizeof(limiter->slots[found].actor), "%s", actor);
		limiter->slots[found].tokens = (int64_t)CONTROL_ACTOR_BURST * CHANGE;
		limiter->slots[found].refilled = now;
	}
	{
		int64_t elapsed = now - limiter->slots[found].refilled;

		if (elapsed > 0)
		{
			/* (CONTROL_ACTOR_PER_MINUTE changes a minute: as many units a
			millisecond, a change CHANGE of them, so nothing is lost to
			rounding) */
			limiter->slots[found].tokens += elapsed * CONTROL_ACTOR_PER_MINUTE;
			if (limiter->slots[found].tokens > (int64_t)CONTROL_ACTOR_BURST * CHANGE)
				limiter->slots[found].tokens = (int64_t)CONTROL_ACTOR_BURST * CHANGE;
			limiter->slots[found].refilled = now;
		}
	}
	limiter->slots[found].last = now;
	if (limiter->slots[found].tokens < CHANGE)
	{
		if (retry_after)
		{
			int64_t missing = CHANGE - limiter->slots[found].tokens;

			*retry_after = (missing / CONTROL_ACTOR_PER_MINUTE + 999) / 1000;
			if (*retry_after < 1)
				*retry_after = 1;
		}
		return 0;
	}
	limiter->slots[found].tokens -= CHANGE;
	return 1;
}

/* ---------- the audit file */

/* text as a JSON string with its quotes, at out (size bytes left): its
length, or -1 if it does not fit (anything not printable ASCII a ?) */
static int json_text(const char *text, char *out, size_t size)
{
	size_t length = 0;

	if (size < 3)
		return -1;
	out[length++] = '"';
	for (; *text; text++)
	{
		unsigned char character = (unsigned char)*text;
		const char *escape = NULL;

		if (character == '"')
			escape = "\\\"";
		else if (character == '\\')
			escape = "\\\\";
		if (escape)
		{
			if (length + 3 >= size)
				return -1;
			out[length++] = escape[0];
			out[length++] = escape[1];
			continue;
		}
		if (length + 2 >= size)
			return -1;
		out[length++] = character >= 0x20 && character <= 0x7E ? (char)character : '?';
	}
	out[length++] = '"';
	out[length] = 0;
	return (int)length;
}

int control_audit_line(const struct control_audit_event *event, char *out, size_t size)
{
	size_t length = 0;
	const char *names[6] = { "via", "actor", "action", "target", "reason", "detail" };
	const char *values[6];
	int index;
	int written;

	values[0] = event->via ? event->via : "";
	values[1] = event->actor ? event->actor : "";
	values[2] = event->action ? event->action : "";
	values[3] = event->target;
	values[4] = event->reason;
	values[5] = event->detail;
	written = snprintf(out, size, "{\"time\": %lld", (long long)event->time);
	if (written < 0 || (size_t)written >= size)
		return -1;
	length = (size_t)written;
	for (index = 0; index < 6; index++)
	{
		char detail[200];
		const char *value = values[index];

		if (!value)
			continue;
		/* (the detail is the output's first line, cut short) */
		if (index == 5)
		{
			size_t line = strcspn(value, "\r\n");

			if (line >= sizeof(detail))
				line = sizeof(detail) - 1;
			memcpy(detail, value, line);
			detail[line] = 0;
			value = detail;
		}
		written = snprintf(out + length, size - length, ", \"%s\": ", names[index]);
		if (written < 0 || (size_t)written >= size - length)
			return -1;
		length += (size_t)written;
		written = json_text(value, out + length, size - length);
		if (written < 0)
			return -1;
		length += (size_t)written;
		if (index == 1)
		{
			written = snprintf(out + length, size - length, ", \"role\": \"%s\"", control_role_name(event->role));
			if (written < 0 || (size_t)written >= size - length)
				return -1;
			length += (size_t)written;
		}
	}
	written = snprintf(out + length, size - length, ", \"ok\": %s}\n", event->ok ? "true" : "false");
	if (written < 0 || (size_t)written >= size - length)
		return -1;
	return (int)(length + (size_t)written);
}
