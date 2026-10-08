/*
EVENT_LOG.C

Delta Stats' event log (event_log.h, docs/delta.md): one game's players and
events, kept within fixed limits, and the game's batch made of them, as
JSON (schema 1).

Built with the platform layer's ABI, with nothing of the game's: the unit
tests (server/tests/events_test.c) build it as it is.

Limits (bounded memory, whatever the game): a game keeps at most
event_log_capacity() events (network.events_limit), and its players' slots,
tags and per-player weapons, vehicles and pickups to the sizes in
event_log.h. When the events are full the samples (positions, pings) thin
out first: those of every other second are dropped, then every other of
what is left, and so on; then normal events (pickups, rides, spawns,
medals, health) give way to the ones kept above all (kills, objectives,
moderation, flags), newest first; then new events are dropped. Every drop
is counted in the batch's "limits". Totals (each player's kills, weapons,
damage, sprees, medals) are counted as events come, before any is dropped,
so they stay exact.

Medals are worked out here from the kills, as Halo announces them: a
multikill is each kill within four seconds of the one before (the game's
own window, game_statistics_record_kill), a spree kills without dying; and
the kinds of kill the site has medals for (melee, sniper, stuck, splatter).
*/

#include "event_log.h"

#include "monocypher.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	_priority_keep,
	_priority_normal,
	_priority_sample,
	NUMBER_OF_PRIORITIES
};

enum
{
	/* the samples thinned at most this often (2^20 seconds: all of them) */
	MAXIMUM_SAMPLE_LEVEL = 20,
	/* the game's multikill window: 4 seconds */
	MULTIKILL_TICKS = 4 * EVENT_LOG_TICKS_PER_SECOND,
	/* the sessions a player's line keeps (joined and left again) */
	MAXIMUM_SESSIONS = 8,
	/* the hash of a hardware ID (bytes) */
	IDENT_SIZE = 32,
};

static char const *const event_names[EVENT_LOG_NUMBER_OF_TYPES] = {
	"kill", "medal", "objective", "pickup", "ride", "spawn", "position", "ping", "health", "moderation", "flag",
};

static unsigned char const event_priorities[EVENT_LOG_NUMBER_OF_TYPES] = {
	_priority_keep, _priority_normal, _priority_keep, _priority_normal, _priority_normal, _priority_normal,
	_priority_sample, _priority_sample, _priority_normal, _priority_keep, _priority_keep,
};

static char const *const damage_names[EVENT_LOG_NUMBER_OF_DAMAGES] = {
	"other", "bullet", "plasma", "melee", "grenade", "explosion", "vehicle", "fall",
};

static char const *const objective_names[EVENT_LOG_NUMBER_OF_OBJECTIVES] = {
	"flag_grab", "flag_return", "flag_score", "ball_grab", "ball_drop", "hill_enter", "hill_exit", "race_lap",
};

static char const *const seat_names[EVENT_LOG_NUMBER_OF_SEATS] = { "driver", "gunner", "passenger" };

static char const *const left_names[EVENT_LOG_NUMBER_OF_LEFTS] = { "end", "quit", "kick", "ban", "timeout" };

static char const *const moderation_names[EVENT_LOG_NUMBER_OF_MODERATIONS] = {
	"kick", "ban", "unban", "mute", "unmute", "warn", "other",
};

static char const *const end_names[EVENT_LOG_NUMBER_OF_ENDS] = { "score", "time", "admin", "empty", "error", "other" };

static char const *const client_names[EVENT_LOG_NUMBER_OF_CLIENTS] = { "other", "chupathingyce" };

/* the multikills' medals (2 kills to 5 and more), the sprees' (5, 10, 15,
20 kills) */
static char const *const multikill_medals[] = { "double_kill", "triple_kill", "killtacular", "killtrocity" };
static char const *const spree_medals[] = { "killing_spree", "killing_frenzy", "running_riot", "rampage" };
/* every medal this file gives, for the players' totals */
static char const *const medal_keys[] = {
	"double_kill", "triple_kill", "killtacular", "killtrocity", "killing_spree", "killing_frenzy", "running_riot",
	"rampage", "beat_down", "sniper_kill", "grenade_stick", "splatter", "killjoy", "from_the_grave",
};
enum { NUMBER_OF_MEDALS = sizeof(medal_keys) / sizeof(medal_keys[0]) };

/* the key a hardware ID is hashed with: the batch's "ident" is no other
use's hash of it (and the site hashes it again with its own secret key) */
static char const ident_key[] = "chupathingyce delta stats ident v1";

/* ---------- structures */

struct log_weapon
{
	short tag;
	int shots;
	int hits;
	int kills;
	int headshots;
	float damage;
};

struct log_vehicle
{
	short tag;
	short seat;
	int ticks;
};

struct log_pickup
{
	short tag;
	int count;
};

struct log_session
{
	int joined;
	int left;
	int how;
};

struct log_player
{
	struct event_log_player_identity identity;
	char ident[2 * IDENT_SIZE + 1];
	int session_count;
	struct log_session sessions[MAXIMUM_SESSIONS];
	int has_totals;
	struct event_log_player_totals totals;
	float damage_dealt;
	float damage_taken;
	int weapon_count;
	struct log_weapon weapons[EVENT_LOG_MAXIMUM_WEAPONS];
	/* (past the weapons kept) */
	struct log_weapon other_weapons;
	int vehicle_count;
	struct log_vehicle vehicles[EVENT_LOG_MAXIMUM_VEHICLES];
	int other_vehicle_ticks;
	int pickup_count;
	struct log_pickup pickups[EVENT_LOG_MAXIMUM_PICKUPS];
	int grenades[2];
	/* from the kills */
	int kills_counted;
	int deaths_counted;
	int spree;
	int best_spree;
	int chain;
	int last_kill_tick;
	int medals[NUMBER_OF_MEDALS];
	/* a moderator's kick or ban of them, which their leaving is then
	(EVENT_LOG_LEFT_*; 0 none) */
	int removed;
};

/* a growing text */
struct text
{
	char *data;
	size_t used;
	size_t size;
	int failed;
};

/* ---------- globals */

static struct
{
	int capacity_wanted;
	int recording;
	struct event_log_game game;
	char id[2 * 16 + 1];

	int capacity;
	int count;
	struct event_log_record *records;
	int priority_counts[NUMBER_OF_PRIORITIES];
	int sample_level;
	int dropped[EVENT_LOG_NUMBER_OF_TYPES];
	int dropped_total;
	int last_tick;
	int parts;

	int tag_count;
	char tags[EVENT_LOG_MAXIMUM_TAGS][EVENT_LOG_TAG_SIZE];
	int tags_full;

	int player_count;
	struct log_player players[EVENT_LOG_MAXIMUM_PLAYERS];
	int players_full;
} event_log = { .capacity_wanted = EVENT_LOG_DEFAULT_EVENTS };

/* ---------- private code */

static int valid_slot(int slot)
{
	return event_log.recording && slot >= 0 && slot < event_log.player_count;
}

static int valid_tag(int tag)
{
	return tag >= 0 && tag < event_log.tag_count;
}

static void text_append(struct text *text, char const *format, ...)
{
	va_list arguments;
	int written;

	if (text->failed)
		return;
	for (;;)
	{
		size_t room = text->size - text->used;

		va_start(arguments, format);
		written = vsnprintf(text->data ? text->data + text->used : NULL, text->data ? room : 0, format, arguments);
		va_end(arguments);
		if (written < 0)
		{
			text->failed = 1;
			return;
		}
		if (text->data && (size_t)written < room)
		{
			text->used += (size_t)written;
			return;
		}
		{
			size_t size = text->size ? text->size : 65536;
			char *data;

			while (size - text->used <= (size_t)written)
				size *= 2;
			data = realloc(text->data, size);
			if (!data)
			{
				text->failed = 1;
				return;
			}
			text->data = data;
			text->size = size;
		}
	}
}

/* a JSON string: UTF-8 kept (a byte that is not of a whole character as
"?"), quotes, backslashes and control characters escaped */
static void text_string(struct text *text, char const *string)
{
	unsigned char const *cursor = (unsigned char const *)string;
	char chunk[256];
	int used = 0;

	chunk[used++] = '"';
	while (*cursor)
	{
		unsigned char character = *cursor;
		int length = character < 0x80 ? 1 : (character & 0xE0) == 0xC0 ? 2 : (character & 0xF0) == 0xE0 ? 3 :
			(character & 0xF8) == 0xF0 ? 4 : 0;
		int index;

		if (used > (int)sizeof(chunk) - 16)
		{
			chunk[used] = 0;
			text_append(text, "%s", chunk);
			used = 0;
		}
		/* (a whole character, of continuation bytes, not overlong) */
		for (index = 1; index < length; index++)
		{
			if ((cursor[index] & 0xC0) != 0x80)
				break;
		}
		if (length == 0 || index < length || (length == 2 && character < 0xC2) ||
			(length == 3 && character == 0xE0 && cursor[1] < 0xA0) ||
			(length == 3 && character == 0xED && cursor[1] >= 0xA0) ||
			(length == 4 && (character > 0xF4 || (character == 0xF0 && cursor[1] < 0x90) ||
				(character == 0xF4 && cursor[1] >= 0x90))))
		{
			chunk[used++] = '?';
			cursor++;
			continue;
		}
		if (length > 1)
		{
			memcpy(chunk + used, cursor, (size_t)length);
			used += length;
			cursor += length;
			continue;
		}
		if (character == '"' || character == '\\')
		{
			chunk[used++] = '\\';
			chunk[used++] = (char)character;
		}
		else if (character < 0x20 || character == 0x7F)
		{
			used += snprintf(chunk + used, sizeof(chunk) - (size_t)used, "\\u%04x", character);
		}
		else
		{
			chunk[used++] = (char)character;
		}
		cursor++;
	}
	chunk[used++] = '"';
	chunk[used] = 0;
	text_append(text, "%s", chunk);
}

static void text_tag(struct text *text, int tag)
{
	text_string(text, valid_tag(tag) ? event_log.tags[tag] : "");
}

/* a number for JSON (none that is not finite) */
static double json_number(double value)
{
	return isfinite(value) ? value : 0.0;
}

/* a world position, within the site's bounds (5000 units) */
static double world(float value)
{
	double kept = json_number(value);

	return kept > 5000.0 ? 5000.0 : kept < -5000.0 ? -5000.0 : kept;
}

static void text_point(struct text *text, float const *point)
{
	text_append(text, "[%.2f, %.2f, %.2f]", world(point[0]), world(point[1]), world(point[2]));
}

/* a tick as the batch's seconds */
static double seconds(int tick)
{
	return (double)(tick < 0 ? 0 : tick) / EVENT_LOG_TICKS_PER_SECOND;
}

static int clamp_add(int total, int amount)
{
	long long sum = (long long)total + amount;

	return sum > 0x7FFFFFFF ? 0x7FFFFFFF : sum < 0 ? 0 : (int)sum;
}

/* a tag's folder (its path less the last part: weapons\pistol\bullet is
weapons\pistol's), as a tag of its own */
static int folder_tag(int tag)
{
	char folder[EVENT_LOG_TAG_SIZE];
	char *slash;

	if (!valid_tag(tag))
		return EVENT_LOG_NONE;
	snprintf(folder, sizeof(folder), "%s", event_log.tags[tag]);
	slash = strrchr(folder, '\\');
	if (!slash)
		slash = strrchr(folder, '/');
	if (!slash || slash == folder)
		return tag;
	*slash = 0;
	return event_log_tag(folder);
}

static struct log_weapon *player_weapon(struct log_player *player, int tag)
{
	int index;

	if (!valid_tag(tag))
		return &player->other_weapons;
	for (index = 0; index < player->weapon_count; index++)
	{
		if (player->weapons[index].tag == tag)
			return &player->weapons[index];
	}
	if (player->weapon_count == EVENT_LOG_MAXIMUM_WEAPONS)
		return &player->other_weapons;
	memset(&player->weapons[player->weapon_count], 0, sizeof(player->weapons[0]));
	player->weapons[player->weapon_count].tag = (short)tag;
	return &player->weapons[player->weapon_count++];
}

/* whether a sample of the tick is kept at the log's level */
static int sample_kept(int tick)
{
	unsigned int second = (unsigned int)(tick < 0 ? 0 : tick) / EVENT_LOG_SAMPLE_TICKS;

	return event_log.sample_level == 0 || (second & ((1u << event_log.sample_level) - 1)) == 0;
}

static void count_dropped(struct event_log_record const *record)
{
	if (record->type >= 0 && record->type < EVENT_LOG_NUMBER_OF_TYPES)
		event_log.dropped[record->type]++;
	event_log.dropped_total++;
}

/* the records kept, those not (keep(record) FALSE) counted as dropped */
static void compact(int (*keep)(struct event_log_record const *record))
{
	int from, to = 0;

	for (from = 0; from < event_log.count; from++)
	{
		struct event_log_record const *record = &event_log.records[from];

		if (keep(record))
		{
			if (to != from)
				event_log.records[to] = *record;
			to++;
		}
		else
		{
			count_dropped(record);
			event_log.priority_counts[event_priorities[record->type]]--;
		}
	}
	event_log.count = to;
}

static int keep_sample(struct event_log_record const *record)
{
	return event_priorities[record->type] != _priority_sample || sample_kept(record->tick);
}

/* the samples thinned out until there is room (FALSE: none left to thin) */
static int thin_samples(void)
{
	while (event_log.count >= event_log.capacity && event_log.sample_level < MAXIMUM_SAMPLE_LEVEL)
	{
		event_log.sample_level++;
		if (event_log.priority_counts[_priority_sample])
			compact(keep_sample);
	}
	return event_log.count < event_log.capacity;
}

/* the newest normal event dropped, for one kept above all (FALSE: none) */
static int drop_newest_normal(void)
{
	int index;

	for (index = event_log.count - 1; index >= 0; index--)
	{
		struct event_log_record const *record = &event_log.records[index];

		if (event_priorities[record->type] == _priority_normal)
		{
			count_dropped(record);
			event_log.priority_counts[_priority_normal]--;
			memmove(&event_log.records[index], &event_log.records[index + 1],
				(size_t)(event_log.count - index - 1) * sizeof(*record));
			event_log.count--;
			return 1;
		}
	}
	return 0;
}

/* a record kept, if there is room for it (FALSE: dropped) */
static int keep(struct event_log_record const *record)
{
	int priority = event_priorities[record->type];

	if (priority == _priority_sample && !sample_kept(record->tick))
	{
		count_dropped(record);
		return 0;
	}
	if (event_log.count >= event_log.capacity)
	{
		int room = event_log.capacity > 0 && thin_samples();

		if (!room && priority == _priority_keep && event_log.capacity > 0)
			room = drop_newest_normal();
		/* (the samples thinned: this one may be of a second no longer kept) */
		if (!room || (priority == _priority_sample && !sample_kept(record->tick)))
		{
			count_dropped(record);
			return 0;
		}
	}
	event_log.records[event_log.count++] = *record;
	event_log.priority_counts[priority]++;
	return 1;
}

/* a medal: its event (its totals are the batch's own count of them) */
static void add_medal(int tick, int slot, char const *key)
{
	struct event_log_record medal;
	int index;

	for (index = 0; index < NUMBER_OF_MEDALS; index++)
	{
		if (!strcmp(medal_keys[index], key) && valid_slot(slot))
			event_log.players[slot].medals[index] = clamp_add(event_log.players[slot].medals[index], 1);
	}
	memset(&medal, 0, sizeof(medal));
	medal.tick = tick;
	medal.type = EVENT_LOG_MEDAL;
	medal.player = (short)slot;
	medal.other = EVENT_LOG_NONE;
	medal.tag[0] = (short)event_log_tag(key);
	medal.tag[1] = medal.tag[2] = EVENT_LOG_NONE;
	if (medal.tag[0] != EVENT_LOG_NONE)
		keep(&medal);
}

/* whether a tag's path begins with prefix (any case, either slash) */
static int tag_is(int tag, char const *prefix)
{
	char const *name;

	if (!valid_tag(tag))
		return 0;
	for (name = event_log.tags[tag]; *prefix; name++, prefix++)
	{
		char a = *name == '/' ? '\\' : *name;
		char b = *prefix == '/' ? '\\' : *prefix;

		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (!a || a != b)
			return 0;
	}
	return 1;
}

/* a kill counted: the killer's and the victim's totals, sprees, and the
medals it earns */
static void count_kill(struct event_log_record const *record)
{
	struct log_player *killer = valid_slot(record->player) ? &event_log.players[record->player] : NULL;
	struct log_player *victim = valid_slot(record->other) ? &event_log.players[record->other] : NULL;
	int counts = killer && killer != victim && !(record->bits & (EVENT_LOG_KILL_SUICIDE | EVENT_LOG_KILL_BETRAYAL));

	/* a killjoy: the end of someone else's spree of five or more */
	if (victim && counts && victim->spree >= 5)
		add_medal(record->tick, record->player, "killjoy");
	if (victim)
	{
		victim->deaths_counted++;
		victim->spree = 0;
		victim->chain = 0;
	}
	if (!counts)
		return;
	killer->kills_counted++;
	{
		struct log_weapon *weapon = player_weapon(killer, folder_tag(record->tag[0]));

		weapon->kills = clamp_add(weapon->kills, 1);
		if (record->bits & EVENT_LOG_KILL_HEADSHOT)
			weapon->headshots = clamp_add(weapon->headshots, 1);
	}
	/* a spree: kills without dying */
	killer->spree++;
	if (killer->spree > killer->best_spree)
		killer->best_spree = killer->spree;
	if (killer->spree % 5 == 0 && killer->spree <= 20)
		add_medal(record->tick, record->player, spree_medals[killer->spree / 5 - 1]);
	/* a multikill: each kill within the window of the one before */
	if (killer->chain > 0 && record->tick - killer->last_kill_tick <= MULTIKILL_TICKS)
		killer->chain++;
	else
		killer->chain = 1;
	killer->last_kill_tick = record->tick;
	if (killer->chain >= 2)
		add_medal(record->tick, record->player, multikill_medals[(killer->chain > 5 ? 5 : killer->chain) - 2]);
	/* the kinds of kill */
	if (record->value[0] == EVENT_LOG_DAMAGE_MELEE)
		add_medal(record->tick, record->player, "beat_down");
	if (tag_is(record->tag[0], "weapons\\sniper rifle\\"))
		add_medal(record->tick, record->player, "sniper_kill");
	if (record->bits & EVENT_LOG_KILL_STICK)
		add_medal(record->tick, record->player, "grenade_stick");
	if (record->value[0] == EVENT_LOG_DAMAGE_VEHICLE && valid_tag(record->tag[1]))
		add_medal(record->tick, record->player, "splatter");
	if (record->bits & EVENT_LOG_KILL_FROM_GRAVE)
		add_medal(record->tick, record->player, "from_the_grave");
}

/* a record's totals, counted whether or not it is kept */
static void count_totals(struct event_log_record const *record)
{
	struct log_player *player = valid_slot(record->player) ? &event_log.players[record->player] : NULL;

	switch (record->type)
	{
	case EVENT_LOG_KILL:
		count_kill(record);
		break;
	case EVENT_LOG_PICKUP:
		if (player && valid_tag(record->tag[0]))
		{
			int index;

			for (index = 0; index < player->pickup_count; index++)
			{
				if (player->pickups[index].tag == record->tag[0])
					break;
			}
			if (index == player->pickup_count && player->pickup_count < EVENT_LOG_MAXIMUM_PICKUPS)
			{
				player->pickups[index].tag = record->tag[0];
				player->pickups[index].count = 0;
				player->pickup_count++;
			}
			if (index < player->pickup_count)
				player->pickups[index].count = clamp_add(player->pickups[index].count, 1);
		}
		break;
	case EVENT_LOG_RIDE:
		if (player)
		{
			int ticks = record->tick - record->value[1];
			int seat = record->value[0] >= 0 && record->value[0] < EVENT_LOG_NUMBER_OF_SEATS ? record->value[0] :
				EVENT_LOG_SEAT_PASSENGER;
			int index;

			if (ticks <= 0)
				break;
			for (index = 0; index < player->vehicle_count; index++)
			{
				if (player->vehicles[index].tag == record->tag[0] && player->vehicles[index].seat == seat)
					break;
			}
			if (index == player->vehicle_count)
			{
				if (!valid_tag(record->tag[0]) || player->vehicle_count == EVENT_LOG_MAXIMUM_VEHICLES)
				{
					player->other_vehicle_ticks = clamp_add(player->other_vehicle_ticks, ticks);
					break;
				}
				player->vehicles[index].tag = record->tag[0];
				player->vehicles[index].seat = (short)seat;
				player->vehicles[index].ticks = 0;
				player->vehicle_count++;
			}
			player->vehicles[index].ticks = clamp_add(player->vehicles[index].ticks, ticks);
		}
		break;
	}
}

/* ---------- the batch */

/* the records of a type, written as the batch's rows of it */
static void write_kills(struct text *text)
{
	int index, any = 0;

	text_append(text, ", \"kills\": [");
	for (index = 0; index < event_log.count; index++)
	{
		struct event_log_record const *record = &event_log.records[index];

		if (record->type != EVENT_LOG_KILL)
			continue;
		text_append(text, "%s\n{\"t\": %.2f, \"killer\": %d, \"victim\": %d, \"weapon\": ", any ? "," : "",
			seconds(record->tick), record->player, record->other);
		text_tag(text, record->tag[0]);
		text_append(text, ", \"damage\": \"%s\"",
			record->value[0] >= 0 && record->value[0] < EVENT_LOG_NUMBER_OF_DAMAGES ? damage_names[record->value[0]] :
				"other");
		if (record->bits & EVENT_LOG_KILL_KILLER_POSITION)
		{
			text_append(text, ", \"killer_pos\": ");
			text_point(text, record->position);
		}
		if (record->bits & EVENT_LOG_KILL_VICTIM_POSITION)
		{
			text_append(text, ", \"victim_pos\": ");
			text_point(text, record->other_position);
		}
		if (valid_tag(record->tag[1]))
		{
			text_append(text, ", \"killer_vehicle\": ");
			text_tag(text, record->tag[1]);
		}
		if (record->bits & EVENT_LOG_KILL_HEADSHOT)
			text_append(text, ", \"headshot\": true");
		if (record->bits & EVENT_LOG_KILL_BETRAYAL)
			text_append(text, ", \"betrayal\": true");
		if (record->bits & EVENT_LOG_KILL_SUICIDE)
			text_append(text, ", \"suicide\": true");
		if (record->bits & EVENT_LOG_KILL_STICK)
			text_append(text, ", \"stuck\": true");
		if (record->bits & EVENT_LOG_KILL_VICTIM_RIDING)
			text_append(text, ", \"victim_riding\": true");
		if (record->bits & EVENT_LOG_KILL_FROM_GRAVE)
			text_append(text, ", \"from_grave\": true");
		text_append(text, "}");
		any = 1;
	}
	text_append(text, "]");
}

static void write_rows(struct text *text)
{
	int index, any, last_second;

	text_append(text, ", \"medals\": [");
	for (index = 0, any = 0; index < event_log.count; index++)
	{
		struct event_log_record const *record = &event_log.records[index];

		if (record->type != EVENT_LOG_MEDAL || !valid_slot(record->player))
			continue;
		text_append(text, "%s{\"t\": %.2f, \"player\": %d, \"medal\": ", any ? ", " : "", seconds(record->tick),
			record->player);
		text_tag(text, record->tag[0]);
		text_append(text, "}");
		any = 1;
	}
	text_append(text, "], \"objectives\": [");
	for (index = 0, any = 0; index < event_log.count; index++)
	{
		struct event_log_record const *record = &event_log.records[index];

		if (record->type != EVENT_LOG_OBJECTIVE)
			continue;
		text_append(text, "%s\n{\"t\": %.2f, \"player\": %d, \"team\": %d, \"kind\": \"%s\", \"pos\": ", any ? "," : "",
			seconds(record->tick), record->player, record->value[1] >= -1 && record->value[1] < 16 ? record->value[1] : -1,
			record->value[0] >= 0 && record->value[0] < EVENT_LOG_NUMBER_OF_OBJECTIVES ? objective_names[record->value[0]] :
				"other");
		text_point(text, record->position);
		text_append(text, "}");
		any = 1;
	}
	text_append(text, "], \"pickups\": [");
	for (index = 0, any = 0; index < event_log.count; index++)
	{
		struct event_log_record const *record = &event_log.records[index];

		if (record->type != EVENT_LOG_PICKUP || !valid_slot(record->player) || !valid_tag(record->tag[0]))
			continue;
		text_append(text, "%s\n{\"t\": %.2f, \"player\": %d, \"item\": ", any ? "," : "", seconds(record->tick),
			record->player);
		text_tag(text, record->tag[0]);
		text_append(text, ", \"pos\": ");
		text_point(text, record->position);
		text_append(text, "}");
		any = 1;
	}
	/* rides: [from, to, player, vehicle, seat] */
	text_append(text, "], \"rides\": [");
	for (index = 0, any = 0; index < event_log.count; index++)
	{
		struct event_log_record const *record = &event_log.records[index];

		if (record->type != EVENT_LOG_RIDE || !valid_slot(record->player) || !valid_tag(record->tag[0]))
			continue;
		text_append(text, "%s[%.2f, %.2f, %d, ", any ? ", " : "", seconds(record->value[1] < record->tick ?
			record->value[1] : record->tick), seconds(record->tick), record->player);
		text_tag(text, record->tag[0]);
		text_append(text, ", \"%s\"]", record->value[0] >= 0 && record->value[0] < EVENT_LOG_NUMBER_OF_SEATS ?
			seat_names[record->value[0]] : "passenger");
		any = 1;
	}
	/* spawns: [t, player, x, y, z] */
	text_append(text, "], \"spawns\": [");
	for (index = 0, any = 0; index < event_log.count; index++)
	{
		struct event_log_record const *record = &event_log.records[index];

		if (record->type != EVENT_LOG_SPAWN || !valid_slot(record->player))
			continue;
		text_append(text, "%s[%.2f, %d, %.2f, %.2f, %.2f]", any ? ", " : "", seconds(record->tick), record->player,
			world(record->position[0]), world(record->position[1]), world(record->position[2]));
		any = 1;
	}
	/* positions: [t, player, x, y, z], a line a second */
	text_append(text, "], \"positions\": [");
	for (index = 0, any = 0, last_second = -1; index < event_log.count; index++)
	{
		struct event_log_record const *record = &event_log.records[index];
		int second = record->tick / EVENT_LOG_TICKS_PER_SECOND;

		if (record->type != EVENT_LOG_POSITION || !valid_slot(record->player))
			continue;
		text_append(text, "%s[%.1f, %d, %.2f, %.2f, %.2f]", !any ? "\n" : second != last_second ? ",\n" : ", ",
			seconds(record->tick), record->player, world(record->position[0]), world(record->position[1]),
			world(record->position[2]));
		last_second = second;
		any = 1;
	}
	/* pings: [t, player, ms] */
	text_append(text, "], \"pings\": [");
	for (index = 0, any = 0; index < event_log.count; index++)
	{
		struct event_log_record const *record = &event_log.records[index];

		if (record->type != EVENT_LOG_PING || !valid_slot(record->player) || record->value[0] < 0)
			continue;
		text_append(text, "%s[%.1f, %d, %d]", any ? ", " : "", seconds(record->tick), record->player,
			record->value[0] > 10000 ? 10000 : record->value[0]);
		any = 1;
	}
	text_append(text, "], \"health\": [");
	for (index = 0, any = 0; index < event_log.count; index++)
	{
		struct event_log_record const *record = &event_log.records[index];
		int cpu = (int)record->bits;

		if (record->type != EVENT_LOG_HEALTH)
			continue;
		text_append(text, "%s\n{\"time\": %u, \"players\": %d, \"tick_ms\": %.2f, \"tick_ms_max\": %.2f", any ? "," : "",
			event_log.game.start_time + (unsigned int)(record->tick / EVENT_LOG_TICKS_PER_SECOND),
			record->value[0] < 0 ? 0 : record->value[0] > 255 ? 255 : record->value[0], record->value[1] / 100.0,
			record->value[2] / 100.0);
		if (cpu >= 0)
			text_append(text, ", \"cpu\": %.1f", cpu / 10.0);
		if (record->value[3] >= 0)
			text_append(text, ", \"memory_mb\": %.1f", record->value[3] / 1024.0);
		text_append(text, "}");
		any = 1;
	}
	text_append(text, "], \"moderation\": [");
	for (index = 0, any = 0; index < event_log.count; index++)
	{
		struct event_log_record const *record = &event_log.records[index];

		if (record->type != EVENT_LOG_MODERATION)
			continue;
		text_append(text, "%s\n{\"t\": %.2f, \"kind\": \"%s\", \"player\": %d, \"name\": ", any ? "," : "",
			seconds(record->tick), record->value[0] >= 0 && record->value[0] < EVENT_LOG_NUMBER_OF_MODERATIONS ?
				moderation_names[record->value[0]] : "other", valid_slot(record->player) ? record->player : -1);
		text_tag(text, record->tag[2]);
		text_append(text, ", \"by\": ");
		text_tag(text, record->tag[1]);
		text_append(text, ", \"reason\": ");
		text_tag(text, record->tag[0]);
		text_append(text, "}");
		any = 1;
	}
	text_append(text, "], \"flags\": [");
	for (index = 0, any = 0; index < event_log.count; index++)
	{
		struct event_log_record const *record = &event_log.records[index];

		if (record->type != EVENT_LOG_FLAG || !valid_slot(record->player) || !valid_tag(record->tag[0]))
			continue;
		text_append(text, "%s\n{\"t\": %.2f, \"player\": %d, \"kind\": ", any ? "," : "", seconds(record->tick),
			record->player);
		text_tag(text, record->tag[0]);
		text_append(text, ", \"severity\": %d, \"detail\": ", record->value[0] < 0 ? 0 : record->value[0] > 3 ? 3 :
			record->value[0]);
		text_tag(text, record->tag[1]);
		text_append(text, "}");
		any = 1;
	}
	text_append(text, "]");
}

static void write_player(struct text *text, int slot, int end_tick)
{
	struct log_player const *player = &event_log.players[slot];
	struct event_log_player_totals const *totals = &player->totals;
	int shots = 0, hits = 0;
	int index;
	int any;

	text_append(text, "\n{\"name\": ");
	text_string(text, player->identity.name);
	if (player->ident[0])
		text_append(text, ", \"ident\": \"%s\"", player->ident);
	text_append(text, ", \"client\": \"%s\"",
		player->identity.client >= 0 && player->identity.client < EVENT_LOG_NUMBER_OF_CLIENTS ?
			client_names[player->identity.client] : "other");
	if (player->identity.platform[0])
	{
		text_append(text, ", \"platform\": ");
		text_string(text, player->identity.platform);
	}
	text_append(text, ", \"team\": %d, \"bot\": %s", player->has_totals ? totals->team : player->identity.team,
		player->identity.bot ? "true" : "false");
	if (player->identity.color >= 0 && player->identity.color < 18)
		text_append(text, ", \"color\": %d", player->identity.color);
	/* (the game's counts; the kills and deaths seen, for a player whose
	totals never came) */
	text_append(text, ", \"score\": %d, \"kills\": %d, \"deaths\": %d, \"assists\": %d, \"betrayals\": %d, "
		"\"suicides\": %d, \"best_spree\": %d",
		player->has_totals ? totals->score : player->kills_counted,
		player->has_totals ? totals->kills : player->kills_counted,
		player->has_totals ? totals->deaths : player->deaths_counted,
		totals->assists < 0 ? 0 : totals->assists, totals->betrayals < 0 ? 0 : totals->betrayals,
		totals->suicides < 0 ? 0 : totals->suicides, player->best_spree);
	if (player->has_totals && totals->place > 0)
		text_append(text, ", \"place\": %d", totals->place);
	text_append(text, ", \"damage_dealt\": %.1f, \"damage_taken\": %.1f", json_number(player->damage_dealt),
		json_number(player->damage_taken));
	text_append(text, ", \"grenades\": {\"frag\": %d, \"plasma\": %d}", player->grenades[0], player->grenades[1]);
	text_append(text, ", \"objectives\": {\"flag_grabs\": %d, \"flag_returns\": %d, \"flag_scores\": %d, "
		"\"ball_time\": %d, \"ball_kills\": %d, \"hill_time\": %d, \"laps\": %d}",
		totals->flag_grabs < 0 ? 0 : totals->flag_grabs, totals->flag_returns < 0 ? 0 : totals->flag_returns,
		totals->flag_scores < 0 ? 0 : totals->flag_scores, totals->ball_time < 0 ? 0 : totals->ball_time,
		totals->ball_kills < 0 ? 0 : totals->ball_kills, totals->hill_time < 0 ? 0 : totals->hill_time,
		totals->laps < 0 ? 0 : totals->laps);
	text_append(text, ", \"weapons\": [");
	for (index = 0, any = 0; index <= player->weapon_count; index++)
	{
		struct log_weapon const *weapon = index < player->weapon_count ? &player->weapons[index] : &player->other_weapons;
		int weapon_hits = weapon->hits > weapon->shots ? weapon->shots : weapon->hits;

		if (!weapon->shots && !weapon->kills && !(weapon->damage > 0.0f))
			continue;
		text_append(text, "%s{\"weapon\": ", any ? ", " : "");
		if (index < player->weapon_count)
			text_tag(text, weapon->tag);
		else
			text_append(text, "\"other\"");
		text_append(text, ", \"shots\": %d, \"hits\": %d, \"kills\": %d, \"headshots\": %d, \"damage\": %.1f}",
			weapon->shots, weapon_hits, weapon->kills, weapon->headshots, json_number(weapon->damage));
		shots = clamp_add(shots, weapon->shots);
		hits = clamp_add(hits, weapon_hits);
		any = 1;
	}
	text_append(text, "], \"shots\": %d, \"hits\": %d", shots, hits);
	text_append(text, ", \"vehicles\": [");
	for (index = 0, any = 0; index < player->vehicle_count; index++)
	{
		text_append(text, "%s{\"vehicle\": ", any ? ", " : "");
		text_tag(text, player->vehicles[index].tag);
		text_append(text, ", \"seat\": \"%s\", \"seconds\": %.1f}", seat_names[player->vehicles[index].seat],
			seconds(player->vehicles[index].ticks));
		any = 1;
	}
	text_append(text, "], \"pickups\": {");
	for (index = 0, any = 0; index < player->pickup_count; index++)
	{
		text_append(text, "%s", any ? ", " : "");
		text_tag(text, player->pickups[index].tag);
		text_append(text, ": %d", player->pickups[index].count);
		any = 1;
	}
	text_append(text, "}, \"medals\": {");
	for (index = 0, any = 0; index < NUMBER_OF_MEDALS; index++)
	{
		if (!player->medals[index])
			continue;
		text_append(text, "%s\"%s\": %d", any ? ", " : "", medal_keys[index], player->medals[index]);
		any = 1;
	}
	text_append(text, "}}");
	(void)end_tick;
}

static void write_sessions(struct text *text, int end_tick)
{
	int slot, index, any = 0;

	text_append(text, ", \"sessions\": [");
	for (slot = 0; slot < event_log.player_count; slot++)
	{
		struct log_player const *player = &event_log.players[slot];

		for (index = 0; index < player->session_count; index++)
		{
			struct log_session const *session = &player->sessions[index];

			text_append(text, "%s{\"player\": %d, \"joined\": %.2f, \"left\": ", any ? ", " : "", slot,
				seconds(session->joined));
			if (session->left >= 0)
				text_append(text, "%.2f, \"reason\": \"%s\"}", seconds(session->left < session->joined ? session->joined :
					session->left), session->how >= 0 && session->how < EVENT_LOG_NUMBER_OF_LEFTS ?
					left_names[session->how] : "end");
			else
				text_append(text, "null, \"reason\": \"end\"}");
			any = 1;
		}
	}
	text_append(text, "]");
	(void)end_tick;
}

static void hash_ident(char const *hardware_id, char *hash)
{
	unsigned char digest[IDENT_SIZE];
	char lower[EVENT_LOG_HARDWARE_ID_SIZE];
	static char const digits[] = "0123456789abcdef";
	int length = 0;
	int index;

	/* (as p2p_hardware_id_sanitize keeps it: lowercase hexadecimal) */
	for (; *hardware_id && length < EVENT_LOG_HARDWARE_ID_SIZE - 1; hardware_id++)
	{
		char character = *hardware_id >= 'A' && *hardware_id <= 'F' ? (char)(*hardware_id - 'A' + 'a') : *hardware_id;

		if ((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))
			lower[length++] = character;
	}
	lower[length] = 0;
	if (!length)
	{
		hash[0] = 0;
		return;
	}
	crypto_blake2b_keyed(digest, sizeof(digest), (unsigned char const *)ident_key, sizeof(ident_key) - 1,
		(unsigned char const *)lower, (size_t)length);
	for (index = 0; index < IDENT_SIZE; index++)
	{
		hash[2 * index] = digits[digest[index] >> 4];
		hash[2 * index + 1] = digits[digest[index] & 15];
	}
	hash[2 * IDENT_SIZE] = 0;
	crypto_wipe(lower, sizeof(lower));
}

/* ---------- public code */

void event_log_set_capacity(int capacity)
{
	event_log.capacity_wanted = capacity < EVENT_LOG_MINIMUM_EVENTS ? EVENT_LOG_MINIMUM_EVENTS :
		capacity > EVENT_LOG_MAXIMUM_EVENTS ? EVENT_LOG_MAXIMUM_EVENTS : capacity;
}

int event_log_capacity(void)
{
	return event_log.capacity_wanted;
}

void event_log_begin(struct event_log_game const *game, unsigned char const random_id[16])
{
	static char const digits[] = "0123456789abcdef";
	int index;

	/* (the records' memory kept from game to game, as long as its size is
	the one wanted) */
	if (!event_log.records || event_log.capacity != event_log.capacity_wanted)
	{
		free(event_log.records);
		event_log.capacity = event_log.capacity_wanted;
		event_log.records = malloc((size_t)event_log.capacity * sizeof(*event_log.records));
		if (!event_log.records)
			event_log.capacity = 0;
	}
	event_log.recording = 1;
	event_log.game = *game;
	event_log.game.map[sizeof(event_log.game.map) - 1] = 0;
	event_log.game.gametype[sizeof(event_log.game.gametype) - 1] = 0;
	event_log.game.server_name[sizeof(event_log.game.server_name) - 1] = 0;
	event_log.game.build[sizeof(event_log.game.build) - 1] = 0;
	event_log.game.platform[sizeof(event_log.game.platform) - 1] = 0;
	event_log.game.playlist[sizeof(event_log.game.playlist) - 1] = 0;
	for (index = 0; index < 16; index++)
	{
		event_log.id[2 * index] = digits[random_id[index] >> 4];
		event_log.id[2 * index + 1] = digits[random_id[index] & 15];
	}
	event_log.id[32] = 0;
	event_log.count = 0;
	memset(event_log.priority_counts, 0, sizeof(event_log.priority_counts));
	event_log.sample_level = 0;
	memset(event_log.dropped, 0, sizeof(event_log.dropped));
	event_log.dropped_total = 0;
	event_log.last_tick = 0;
	event_log.parts = 0;
	event_log.tag_count = 0;
	event_log.tags_full = 0;
	event_log.player_count = 0;
	event_log.players_full = 0;
}

int event_log_recording(void)
{
	return event_log.recording;
}

char const *event_log_game_id(void)
{
	return event_log.recording ? event_log.id : "";
}

int event_log_tag(char const *text)
{
	int index;

	if (!event_log.recording || !text || !text[0])
		return EVENT_LOG_NONE;
	for (index = 0; index < event_log.tag_count; index++)
	{
		if (!strncmp(event_log.tags[index], text, EVENT_LOG_TAG_SIZE - 1))
			return index;
	}
	if (event_log.tag_count == EVENT_LOG_MAXIMUM_TAGS)
	{
		event_log.tags_full++;
		return EVENT_LOG_NONE;
	}
	snprintf(event_log.tags[event_log.tag_count], EVENT_LOG_TAG_SIZE, "%s", text);
	return event_log.tag_count++;
}

int event_log_player(int tick, struct event_log_player_identity const *identity)
{
	struct log_player *player;

	if (!event_log.recording)
		return EVENT_LOG_NONE;
	if (event_log.player_count == EVENT_LOG_MAXIMUM_PLAYERS)
	{
		event_log.players_full++;
		return EVENT_LOG_NONE;
	}
	player = &event_log.players[event_log.player_count];
	memset(player, 0, sizeof(*player));
	player->identity = *identity;
	player->identity.name[EVENT_LOG_NAME_SIZE - 1] = 0;
	player->identity.hardware_id[EVENT_LOG_HARDWARE_ID_SIZE - 1] = 0;
	player->identity.platform[sizeof(player->identity.platform) - 1] = 0;
	hash_ident(player->identity.hardware_id, player->ident);
	/* (the ID itself is not kept past this) */
	memset(player->identity.hardware_id, 0, sizeof(player->identity.hardware_id));
	player->sessions[0].joined = tick < 0 ? 0 : tick;
	player->sessions[0].left = -1;
	player->session_count = 1;
	player->other_weapons.tag = EVENT_LOG_NONE;
	if (tick > event_log.last_tick)
		event_log.last_tick = tick;
	return event_log.player_count++;
}

void event_log_player_totals(int slot, struct event_log_player_totals const *totals)
{
	if (!valid_slot(slot))
		return;
	event_log.players[slot].totals = *totals;
	event_log.players[slot].has_totals = 1;
}

void event_log_player_left(int slot, int tick, int how)
{
	struct log_player *player;

	if (!valid_slot(slot))
		return;
	player = &event_log.players[slot];
	if (player->sessions[player->session_count - 1].left < 0)
	{
		player->sessions[player->session_count - 1].left = tick;
		player->sessions[player->session_count - 1].how = player->removed && how == EVENT_LOG_LEFT_QUIT ? player->removed : how;
	}
	player->removed = 0;
	player->spree = 0;
	player->chain = 0;
}

void event_log_player_rejoined(int slot, int tick)
{
	struct log_player *player;

	if (!valid_slot(slot))
		return;
	player = &event_log.players[slot];
	if (player->sessions[player->session_count - 1].left < 0)
		return;
	if (player->session_count == MAXIMUM_SESSIONS)
	{
		/* (the last session goes on) */
		player->sessions[player->session_count - 1].left = -1;
		return;
	}
	player->sessions[player->session_count].joined = tick;
	player->sessions[player->session_count].left = -1;
	player->session_count++;
}

void event_log_shots(int slot, int weapon_tag, int shots)
{
	struct log_weapon *weapon;

	if (!valid_slot(slot) || shots <= 0)
		return;
	weapon = player_weapon(&event_log.players[slot], weapon_tag);
	weapon->shots = clamp_add(weapon->shots, shots);
}

void event_log_hit(int slot, int weapon_tag, float damage)
{
	struct log_weapon *weapon;

	if (!valid_slot(slot))
		return;
	weapon = player_weapon(&event_log.players[slot], weapon_tag);
	weapon->hits = clamp_add(weapon->hits, 1);
	if (isfinite(damage) && damage > 0.0f && damage < 1e6f)
		weapon->damage += damage;
}

void event_log_grenade(int slot, int plasma)
{
	if (!valid_slot(slot))
		return;
	event_log.players[slot].grenades[plasma ? 1 : 0] = clamp_add(event_log.players[slot].grenades[plasma ? 1 : 0], 1);
}

void event_log_damage(int dealer, int taker, float amount)
{
	if (!isfinite(amount) || !(amount > 0.0f) || amount > 1e6f)
		return;
	if (valid_slot(dealer))
		event_log.players[dealer].damage_dealt += amount;
	if (valid_slot(taker))
		event_log.players[taker].damage_taken += amount;
}

int event_log_add(struct event_log_record const *source)
{
	struct event_log_record record;
	int index;

	if (!event_log.recording || !source || source->type < 0 || source->type >= EVENT_LOG_NUMBER_OF_TYPES)
		return 0;
	record = *source;
	/* (what refers to nothing kept refers to none) */
	if (!valid_slot(record.player))
		record.player = EVENT_LOG_NONE;
	if (!valid_slot(record.other))
		record.other = EVENT_LOG_NONE;
	for (index = 0; index < 3; index++)
		record.tag[index] = valid_tag(record.tag[index]) ? record.tag[index] : (short)EVENT_LOG_NONE;
	if (record.tick < 0)
		record.tick = 0;
	if (record.tick > event_log.last_tick)
		event_log.last_tick = record.tick;
	/* (a kill of no one kept, and a player's event of no player, say
	nothing) */
	if ((record.type == EVENT_LOG_KILL && record.other == EVENT_LOG_NONE) ||
		(record.type != EVENT_LOG_KILL && record.type != EVENT_LOG_HEALTH && record.type != EVENT_LOG_MODERATION &&
			record.player == EVENT_LOG_NONE))
	{
		count_dropped(&record);
		return 0;
	}
	/* (the kill first, then the medals it earns) */
	if (record.type == EVENT_LOG_KILL)
	{
		int kept = keep(&record);

		count_totals(&record);
		return kept;
	}
	count_totals(&record);
	return keep(&record);
}

void event_log_moderation(int kind, char const *who, char const *by, char const *reason)
{
	struct event_log_record record;
	int slot;

	if (!event_log.recording)
		return;
	memset(&record, 0, sizeof(record));
	record.tick = event_log.last_tick;
	record.type = EVENT_LOG_MODERATION;
	record.player = EVENT_LOG_NONE;
	record.other = EVENT_LOG_NONE;
	record.value[0] = kind;
	/* (the player's latest slot of that name) */
	for (slot = event_log.player_count - 1; who && who[0] && slot >= 0; slot--)
	{
		if (!strcmp(event_log.players[slot].identity.name, who))
		{
			record.player = (short)slot;
			break;
		}
	}
	if (record.player != EVENT_LOG_NONE && (kind == EVENT_LOG_MODERATION_KICK || kind == EVENT_LOG_MODERATION_BAN))
		event_log.players[record.player].removed = kind == EVENT_LOG_MODERATION_KICK ? EVENT_LOG_LEFT_KICK : EVENT_LOG_LEFT_BAN;
	record.tag[0] = (short)event_log_tag(reason);
	record.tag[1] = (short)event_log_tag(by);
	record.tag[2] = record.player == EVENT_LOG_NONE ? (short)event_log_tag(who) : (short)EVENT_LOG_NONE;
	event_log_add(&record);
}

void event_log_free(char *json)
{
	free(json);
}

int event_log_count(void)
{
	return event_log.count;
}

int event_log_dropped(void)
{
	return event_log.dropped_total;
}

char *event_log_finish(struct event_log_end const *end, char const *invite, int final, size_t *length)
{
	struct text text = { NULL, 0, 0, 0 };
	int end_tick;
	unsigned int ended;
	int index;
	int any;

	*length = 0;
	if (!event_log.recording)
		return NULL;
	if (!event_log.player_count)
	{
		if (final)
			event_log.recording = 0;
		return NULL;
	}
	end_tick = end->tick > event_log.last_tick ? end->tick : event_log.last_tick;
	ended = end->end_time > event_log.game.start_time ? end->end_time : event_log.game.start_time;
	event_log.parts++;
	text_append(&text, "{\"schema\": %d, \"server\": {\"name\": ", EVENT_LOG_SCHEMA);
	text_string(&text, event_log.game.server_name);
	text_append(&text, ", \"build\": ");
	text_string(&text, event_log.game.build);
	text_append(&text, ", \"platform\": ");
	text_string(&text, event_log.game.platform);
	text_append(&text, "}, \"game\": {\"id\": \"%s\", \"part\": %d, \"final\": %s", event_log.id, event_log.parts,
		final ? "true" : "false");
	if (invite && invite[0])
	{
		text_append(&text, ", \"invite\": ");
		text_string(&text, invite);
	}
	if (event_log.game.playlist[0])
	{
		text_append(&text, ", \"playlist\": ");
		text_string(&text, event_log.game.playlist);
	}
	text_append(&text, ", \"map\": ");
	text_string(&text, event_log.game.map);
	text_append(&text, ", \"engine\": %d, \"gametype\": ", event_log.game.engine);
	text_string(&text, event_log.game.gametype);
	text_append(&text, ", \"teams\": %d, \"score_limit\": %d, \"started\": %u, \"ended\": %u, \"end_reason\": \"%s\"",
		event_log.game.teams ? 1 : 0, event_log.game.score_limit < 0 ? 0 : event_log.game.score_limit,
		event_log.game.start_time, ended,
		end->reason >= 0 && end->reason < EVENT_LOG_NUMBER_OF_ENDS ? end_names[end->reason] : "other");
	if (event_log.game.teams)
		text_append(&text, ", \"team_scores\": [%d, %d]", end->team_scores[0], end->team_scores[1]);
	text_append(&text, "}, \"players\": [");
	for (index = 0; index < event_log.player_count; index++)
	{
		if (index)
			text_append(&text, ",");
		write_player(&text, index, end_tick);
	}
	text_append(&text, "]");
	write_sessions(&text, end_tick);
	write_kills(&text);
	write_rows(&text);
	text_append(&text, ", \"limits\": {\"events\": %d, \"capacity\": %d, \"sample_seconds\": %u, \"dropped\": {",
		event_log.count, event_log.capacity, 1u << event_log.sample_level);
	for (index = 0, any = 0; index < EVENT_LOG_NUMBER_OF_TYPES; index++)
	{
		if (!event_log.dropped[index])
			continue;
		text_append(&text, "%s\"%s\": %d", any ? ", " : "", event_names[index], event_log.dropped[index]);
		any = 1;
	}
	text_append(&text, "}, \"players_dropped\": %d, \"tags_dropped\": %d}}\n", event_log.players_full,
		event_log.tags_full);
	if (final)
	{
		event_log.count = 0;
		event_log.recording = 0;
	}
	if (text.failed)
	{
		free(text.data);
		return NULL;
	}
	*length = text.used;
	return text.data;
}
