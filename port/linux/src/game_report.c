/*
GAME_REPORT.C

A finished game's carnage report as JSON (game_report.h).

The buffer is sized from the lines themselves: a line of the scoreboard's
numbers alone (another player's, on a client's report) takes at most
GAME_REPORT_MINIMAL_LINE_SIZE, a full one GAME_REPORT_LINE_SIZE and its
extra. A console's report of a full game is a few kilobytes, not one
recorder's extra for every player.
*/

#include "game_report.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- text */

void game_report_utf8(const unsigned short *name, int length, char *text, int size)
{
	int used = 0;
	int index;

	for (index = 0; index < length && name[index]; index++)
	{
		unsigned int character = name[index];
		char bytes[3];
		int count;

		/* (no surrogate pairs in the game's names) */
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
		if (used + count >= size)
			break;
		memcpy(text + used, bytes, (size_t)count);
		used += count;
	}
	text[used] = 0;
}

int game_report_json_name(char *out, int size, const unsigned short *name, int length)
{
	char text[64];
	int used = 0;
	const char *cursor;

	game_report_utf8(name, length, text, sizeof(text));
	used += snprintf(out + used, (size_t)(size - used), "\"");
	for (cursor = text; *cursor && used < size - 8; cursor++)
	{
		unsigned char character = (unsigned char)*cursor;

		if (character == '"' || character == '\\')
			used += snprintf(out + used, (size_t)(size - used), "\\%c", character);
		else if (character < 0x20)
			used += snprintf(out + used, (size_t)(size - used), "\\u%04x", character);
		else
			out[used++] = (char)character;
	}
	used += snprintf(out + used, (size_t)(size - used), "\"");
	return used;
}

/* text appended to a report of size bytes at used, as far as it fits:
where its end is now (never past the buffer, whatever snprintf says it
would have written) */
static int clamped(int used, size_t size)
{
	return used < 0 ? 0 : (size_t)used >= size ? (int)size - 1 : used;
}

static int append(char *report, size_t size, int used, const char *format, ...)
{
	va_list arguments;
	int written;

	if ((size_t)used >= size - 1)
		return used;
	va_start(arguments, format);
	written = vsnprintf(report + used, size - (size_t)used, format, arguments);
	va_end(arguments);
	return written < 0 ? used : clamped(used + written, size);
}

/* ---------- the report */

/* whether a line's extra goes in it */
static int line_has_extra(const struct browser_report_player *player)
{
	return !player->minimal && player->extra && player->extra[0] && strlen(player->extra) < BROWSER_REPORT_EXTRA_SIZE;
}

/* the most a line takes, its separator with it */
static size_t line_size(const struct browser_report_player *player)
{
	if (player->minimal)
		return GAME_REPORT_MINIMAL_LINE_SIZE;
	return GAME_REPORT_LINE_SIZE + (line_has_extra(player) ? strlen(player->extra) + 4 : 0);
}

unsigned long game_report_size(const char *prefix, const struct browser_report_player *players, int count,
	const char *extra)
{
	size_t size = 256 + (prefix ? strlen(prefix) : 0) + (extra ? strlen(extra) + 4 : 0);
	int index;

	for (index = 0; index < count; index++)
		size += line_size(&players[index]);
	return (unsigned long)size;
}

char *game_report_json(const char *prefix, int teams, int red_score, int blue_score, int duration_seconds,
	const struct browser_report_player *players, int count, const char *extra, game_report_tag_function tag,
	void *tag_context)
{
	size_t size;
	char *report;
	int used = 0;
	int index;

	if (count <= 0)
		return NULL;
	size = game_report_size(prefix, players, count, extra);
	report = malloc(size);
	if (!report)
		return NULL;
	used = append(report, size, used, "{%s\"teams\": %d, \"duration\": %d, \"team_scores\": [%d, %d], \"players\": [",
		prefix ? prefix : "", teams != 0, duration_seconds, teams ? red_score : 0, teams ? blue_score : 0);
	for (index = 0; index < count && (size_t)used + line_size(&players[index]) < size; index++)
	{
		const struct browser_report_player *player = &players[index];
		char line_tag[65];

		used = append(report, size, used, "%s{\"name\": ", index ? ", " : "");
		used = clamped(used + game_report_json_name(report + used, (int)(size - (size_t)used), player->name,
			BROWSER_PLAYER_NAME_LENGTH), size);
		if (player->minimal)
		{
			/* (a player not on Chupathingy: this game's scoreboard, no more) */
			used = append(report, size, used, ", \"team\": %d, \"place\": %d, \"score\": %d, \"kills\": %d, \"deaths\": %d",
				player->team, player->place, player->score, player->kills, player->deaths);
		}
		else used = append(report, size, used,
			", \"team\": %d, \"place\": %d, \"score\": %d, \"kills\": %d, \"assists\": %d, \"deaths\": %d, "
			"\"betrayals\": %d, \"suicides\": %d, \"shots_fired\": %d, \"shots_hit\": %d, \"multikills\": %d, "
			"\"color\": %d, \"flag_grabs\": %d, \"flag_returns\": %d, \"flag_scores\": %d, \"ball_time\": %d, "
			"\"ball_carrier_kills\": %d, \"hill_time\": %d, \"laps\": %d",
			player->team, player->place, player->score, player->kills, player->assists, player->deaths,
			player->betrayals, player->suicides, player->shots_fired, player->shots_hit, player->multikills,
			player->color, player->flag_grabs, player->flag_returns, player->flag_scores, player->ball_time,
			player->ball_carrier_kills, player->hill_time, player->laps);
		/* (the line's tag: who may confirm it) */
		if (tag && tag(player, tag_context, line_tag))
		{
			line_tag[64] = 0;
			used = append(report, size, used, ", \"tag\": \"%s\"", line_tag);
		}
		if (line_has_extra(player))
			used = append(report, size, used, ", %s", player->extra);
		used = append(report, size, used, "}");
	}
	used = append(report, size, used, "]");
	if (extra && extra[0])
		used = append(report, size, used, ", %s", extra);
	append(report, size, used, "}");
	return report;
}
