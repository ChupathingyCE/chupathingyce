/*
EVENT_LOG.C

The dedicated server's event log (server/src/event_log.h,
server/docs/events.md): one game's players and events, kept within fixed
limits, and the batch made of them at the game's end, as JSON.

Built with the host's ABI, with nothing of the game's: the unit tests
(server/tests/events_test.c) build it as it is.

Limits (bounded memory, whatever the game): a game keeps at most
event_log_capacity() events (HALO_DEDICATED_EVENTS_LIMIT), its players'
slots, tags and per-player weapons and vehicles to the sizes in
event_log.h. When the events are full the samples (positions, network)
thin out first: those of every other second are dropped, then every other
of what is left, and so on; then normal events (pickups, vehicles,
grenades, multikills, sprees, health) give way to the ones kept above all
(kills, objectives, sessions, moderation, suspect flags), newest first;
then new events are dropped. Every drop is counted in the batch's
"limits". Totals (each player's kills, damage, weapons, ...) are counted as
events come, before any is dropped, so they stay exact.
*/

#include "../src/event_log.h"

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
	/* the longest run of multikills counted on its own ("2" to "10"; more
	count as "10") */
	MAXIMUM_MULTIKILL = 10,
	/* the key a hardware id is hashed with (server/docs/events.md) */
	ID_HASH_SIZE = 16,
};

static char const *const event_names[EVENT_LOG_NUMBER_OF_TYPES] = {
	"kill", "multikill", "spree", "spree_end", "grenade", "pickup", "vehicle_enter", "vehicle_exit",
	"flag_grab", "flag_capture", "flag_return", "flag_drop", "ball_grab", "ball_drop", "lap",
	"join", "leave", "team", "net", "pos", "health", "kick", "ban", "auto_kick", "suspect",
};

static unsigned char const event_priorities[EVENT_LOG_NUMBER_OF_TYPES] = {
	_priority_keep, _priority_normal, _priority_normal, _priority_normal, _priority_normal, _priority_normal,
	_priority_normal, _priority_normal,
	_priority_keep, _priority_keep, _priority_keep, _priority_keep, _priority_keep, _priority_keep, _priority_keep,
	_priority_keep, _priority_keep, _priority_keep, _priority_sample, _priority_sample, _priority_normal,
	_priority_keep, _priority_keep, _priority_keep, _priority_keep,
};

static char const *const pickup_names[EVENT_LOG_NUMBER_OF_PICKUPS] = {
	"weapon", "camouflage", "overshield", "full_spectrum_vision",
};

static char const *const suspect_names[EVENT_LOG_NUMBER_OF_SUSPECTS] = {
	"accuracy", "speed", "damage", "speed_hack",
};

static char const *const end_names[EVENT_LOG_NUMBER_OF_ENDS] = {
	"unknown", "limit", "idle", "empty", "command", "aborted",
};

/* the game's damage categories (damage.c), by their index */
static char const *const category_names[] = {
	"none", "falling", "bullet", "grenade", "high_explosive", "sniper", "melee", "flame", "mounted_weapon",
	"vehicle", "plasma", "needle", "shotgun",
};

static char const *const engine_names[] = {
	"none", "ctf", "slayer", "oddball", "king", "race", "terminator", "stub",
};

static char const id_hash_key[] = "chupathingyce-events hardware id v1";

/* ---------- structures */

struct log_weapon
{
	short tag;
	int shots;
	int hits;
	int kills;
	float damage;
};

struct log_vehicle
{
	short tag;
	int ticks;
};

struct log_player
{
	struct event_log_player_identity identity;
	char id_hash[2 * ID_HASH_SIZE + 1];
	int joined_tick;
	int left_tick;
	int quit;
	int has_totals;
	struct event_log_player_totals totals;
	float damage_dealt;
	float damage_taken;
	int shots;
	int hits;
	int weapon_count;
	struct log_weapon weapons[EVENT_LOG_MAXIMUM_WEAPONS];
	/* (past the weapons kept) */
	struct log_weapon other_weapons;
	int vehicle_count;
	struct log_vehicle vehicles[EVENT_LOG_MAXIMUM_VEHICLES];
	int other_vehicle_ticks;
	int grenades[2];
	int pickups[EVENT_LOG_NUMBER_OF_PICKUPS];
	int multikills[MAXIMUM_MULTIKILL + 1];
	int headshots;
	int melee_kills;
	int grenade_kills;
	int splatters;
	int explosion_kills;
	int fall_deaths;
	int kills_counted;
	int deaths_counted;
	int flag_carry_ticks;
	int ball_carry_ticks;
	int vehicle_entries;
	long long ping_sum;
	int ping_count;
	int ping_maximum;
	long long jitter_sum;
	int jitter_count;
	long long loss_sum;
	int loss_count;
	unsigned int suspects;
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

	int tag_count;
	char tags[EVENT_LOG_MAXIMUM_TAGS][EVENT_LOG_TAG_SIZE];
	int tags_full;

	int player_count;
	struct log_player players[EVENT_LOG_MAXIMUM_PLAYERS];
	int players_full;
} event_log = { EVENT_LOG_DEFAULT_EVENTS };

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
	if (valid_tag(tag))
		text_string(text, event_log.tags[tag]);
	else
		text_append(text, "null");
}

static void text_slot(struct text *text, int slot)
{
	if (slot >= 0 && slot < event_log.player_count)
		text_append(text, "%d", slot);
	else
		text_append(text, "null");
}

/* a number for JSON (none that is not finite) */
static double finite(double value)
{
	return isfinite(value) ? value : 0.0;
}

static void text_point(struct text *text, float const *point)
{
	text_append(text, "[%.2f, %.2f, %.2f]", finite(point[0]), finite(point[1]), finite(point[2]));
}

/* a tag's index as a short (EVENT_LOG_NONE if out of range) */
static short tag_index(int tag)
{
	return valid_tag(tag) ? (short)tag : (short)EVENT_LOG_NONE;
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

static int clamp_add(int total, int amount)
{
	long long sum = (long long)total + amount;

	return sum > 0x7FFFFFFF ? 0x7FFFFFFF : sum < 0 ? 0 : (int)sum;
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

/* a record's totals, counted whether or not it is kept */
static void count_totals(struct event_log_record const *record)
{
	struct log_player *player = valid_slot(record->player) ? &event_log.players[record->player] : NULL;
	struct log_player *other = valid_slot(record->other) ? &event_log.players[record->other] : NULL;

	switch (record->type)
	{
	case EVENT_LOG_KILL:
		if (other)
		{
			other->deaths_counted++;
			if (record->bits & EVENT_LOG_KILL_FALL)
				other->fall_deaths++;
		}
		if (player && !(record->bits & (EVENT_LOG_KILL_SUICIDE | EVENT_LOG_KILL_BETRAYAL)))
		{
			player->kills_counted++;
			if (record->bits & EVENT_LOG_KILL_HEADSHOT)
				player->headshots++;
			if (record->bits & EVENT_LOG_KILL_MELEE)
				player->melee_kills++;
			if (record->bits & EVENT_LOG_KILL_GRENADE)
				player->grenade_kills++;
			if (record->bits & EVENT_LOG_KILL_SPLATTER)
				player->splatters++;
			if (record->bits & EVENT_LOG_KILL_EXPLOSION)
				player->explosion_kills++;
		}
		break;
	case EVENT_LOG_MULTIKILL:
		if (player && record->value[0] >= 2)
			player->multikills[record->value[0] > MAXIMUM_MULTIKILL ? MAXIMUM_MULTIKILL : record->value[0]]++;
		break;
	case EVENT_LOG_GRENADE:
		if (player)
			player->grenades[record->value[0] ? 1 : 0]++;
		break;
	case EVENT_LOG_PICKUP:
		if (player && record->value[0] >= 0 && record->value[0] < EVENT_LOG_NUMBER_OF_PICKUPS)
			player->pickups[record->value[0]]++;
		break;
	case EVENT_LOG_VEHICLE_ENTER:
		if (player)
			player->vehicle_entries++;
		break;
	case EVENT_LOG_FLAG_CAPTURE:
	case EVENT_LOG_FLAG_DROP:
		if (player)
			player->flag_carry_ticks = clamp_add(player->flag_carry_ticks, record->value[0]);
		break;
	case EVENT_LOG_BALL_DROP:
		if (player)
			player->ball_carry_ticks = clamp_add(player->ball_carry_ticks, record->value[0]);
		break;
	case EVENT_LOG_NET:
		if (player)
		{
			if (record->value[0] >= 0)
			{
				player->ping_sum += record->value[0];
				player->ping_count++;
				if (record->value[0] > player->ping_maximum)
					player->ping_maximum = record->value[0];
			}
			if (record->value[1] >= 0)
			{
				player->jitter_sum += record->value[1];
				player->jitter_count++;
			}
			if (record->value[2] >= 0)
			{
				player->loss_sum += record->value[2];
				player->loss_count++;
			}
		}
		break;
	case EVENT_LOG_SUSPECT:
		if (player && record->value[0] >= 0 && record->value[0] < EVENT_LOG_NUMBER_OF_SUSPECTS)
			player->suspects |= 1u << record->value[0];
		break;
	}
}

/* ---------- the batch */

static void write_event(struct text *text, struct event_log_record const *record)
{
	text_append(text, "{\"t\": %d, \"e\": \"%s\"", record->tick, event_names[record->type]);
	switch (record->type)
	{
	case EVENT_LOG_KILL:
	{
		static struct
		{
			unsigned int bit;
			char const *name;
		} const kill_bits[] = {
			{ EVENT_LOG_KILL_HEADSHOT, "headshot" }, { EVENT_LOG_KILL_MELEE, "melee" },
			{ EVENT_LOG_KILL_GRENADE, "grenade" }, { EVENT_LOG_KILL_SPLATTER, "splatter" },
			{ EVENT_LOG_KILL_FALL, "fall" }, { EVENT_LOG_KILL_SUICIDE, "suicide" },
			{ EVENT_LOG_KILL_BETRAYAL, "betrayal" }, { EVENT_LOG_KILL_EXPLOSION, "explosion" },
			{ EVENT_LOG_KILL_WORLD, "world" },
		};
		int index;

		text_append(text, ", \"killer\": ");
		text_slot(text, record->player);
		text_append(text, ", \"victim\": ");
		text_slot(text, record->other);
		text_append(text, ", \"damage\": ");
		text_tag(text, record->tag[0]);
		if (record->value[0] >= 0 && record->value[0] < (int)(sizeof(category_names) / sizeof(category_names[0])))
			text_append(text, ", \"category\": \"%s\"", category_names[record->value[0]]);
		else
			text_append(text, ", \"category\": null");
		for (index = 0; index < (int)(sizeof(kill_bits) / sizeof(kill_bits[0])); index++)
		{
			if (record->bits & kill_bits[index].bit)
				text_append(text, ", \"%s\": true", kill_bits[index].name);
		}
		if (record->bits & EVENT_LOG_KILL_KILLER_POSITION)
		{
			text_append(text, ", \"killer_at\": ");
			text_point(text, record->position);
		}
		if (record->bits & EVENT_LOG_KILL_VICTIM_POSITION)
		{
			text_append(text, ", \"victim_at\": ");
			text_point(text, record->other_position);
		}
		if (valid_tag(record->tag[1]))
		{
			text_append(text, ", \"killer_vehicle\": ");
			text_tag(text, record->tag[1]);
			text_append(text, ", \"killer_seat\": %d", record->value[1]);
		}
		if (valid_tag(record->tag[2]))
		{
			text_append(text, ", \"victim_vehicle\": ");
			text_tag(text, record->tag[2]);
			text_append(text, ", \"victim_seat\": %d", record->value[2]);
		}
		break;
	}
	case EVENT_LOG_MULTIKILL:
	case EVENT_LOG_SPREE:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		text_append(text, ", \"count\": %d", record->value[0]);
		break;
	case EVENT_LOG_SPREE_END:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		text_append(text, ", \"count\": %d, \"by\": ", record->value[0]);
		text_slot(text, record->other);
		break;
	case EVENT_LOG_GRENADE:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		text_append(text, ", \"type\": \"%s\", \"at\": ", record->value[0] ? "plasma" : "frag");
		text_point(text, record->position);
		break;
	case EVENT_LOG_PICKUP:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		text_append(text, ", \"item\": \"%s\"", record->value[0] >= 0 && record->value[0] < EVENT_LOG_NUMBER_OF_PICKUPS ?
			pickup_names[record->value[0]] : "unknown");
		if (valid_tag(record->tag[0]))
		{
			text_append(text, ", \"tag\": ");
			text_tag(text, record->tag[0]);
		}
		text_append(text, ", \"at\": ");
		text_point(text, record->position);
		break;
	case EVENT_LOG_VEHICLE_ENTER:
	case EVENT_LOG_VEHICLE_EXIT:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		text_append(text, ", \"vehicle\": ");
		text_tag(text, record->tag[0]);
		text_append(text, ", \"seat\": %d", record->value[0]);
		if (record->type == EVENT_LOG_VEHICLE_EXIT)
			text_append(text, ", \"ticks\": %d", record->value[1]);
		text_append(text, ", \"at\": ");
		text_point(text, record->position);
		break;
	case EVENT_LOG_FLAG_CAPTURE:
	case EVENT_LOG_FLAG_DROP:
	case EVENT_LOG_BALL_DROP:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		text_append(text, ", \"%s\": %d, \"at\": ", record->type == EVENT_LOG_BALL_DROP ? "held" : "carried",
			record->value[0]);
		text_point(text, record->position);
		break;
	case EVENT_LOG_FLAG_GRAB:
	case EVENT_LOG_FLAG_RETURN:
	case EVENT_LOG_BALL_GRAB:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		text_append(text, ", \"at\": ");
		text_point(text, record->position);
		break;
	case EVENT_LOG_LAP:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		text_append(text, ", \"lap\": %d, \"ticks\": %d", record->value[0], record->value[1]);
		break;
	case EVENT_LOG_JOIN:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		break;
	case EVENT_LOG_LEAVE:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		text_append(text, ", \"quit\": %s", record->value[0] ? "true" : "false");
		break;
	case EVENT_LOG_TEAM:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		text_append(text, ", \"from\": %d, \"to\": %d", record->value[0], record->value[1]);
		break;
	case EVENT_LOG_NET:
	{
		static char const *const names[3] = { "ping", "jitter", "loss" };
		int index;

		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		for (index = 0; index < 3; index++)
		{
			if (record->value[index] >= 0)
				text_append(text, ", \"%s\": %d", names[index], record->value[index]);
			else
				text_append(text, ", \"%s\": null", names[index]);
		}
		break;
	}
	case EVENT_LOG_POSITION:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		text_append(text, ", \"at\": ");
		text_point(text, record->position);
		text_append(text, ", \"facing\": %d", record->value[0]);
		if (valid_tag(record->tag[0]))
		{
			text_append(text, ", \"vehicle\": ");
			text_tag(text, record->tag[0]);
		}
		break;
	case EVENT_LOG_HEALTH:
		text_append(text, ", \"tick_rate\": %.2f", finite(record->value[0] / 100.0));
		if (record->value[1] >= 0)
			text_append(text, ", \"cpu\": %.3f", record->value[1] / 1000.0);
		else
			text_append(text, ", \"cpu\": null");
		if (record->value[2] >= 0)
			text_append(text, ", \"memory_kb\": %d", record->value[2]);
		else
			text_append(text, ", \"memory_kb\": null");
		text_append(text, ", \"players\": %d", record->value[3]);
		break;
	case EVENT_LOG_KICK:
	case EVENT_LOG_BAN:
	case EVENT_LOG_AUTO_KICK:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		if (valid_tag(record->tag[1]))
		{
			text_append(text, ", \"name\": ");
			text_tag(text, record->tag[1]);
		}
		if (record->type == EVENT_LOG_BAN)
			text_append(text, ", \"duration\": %d", record->value[0]);
		text_append(text, ", \"reason\": ");
		text_tag(text, record->tag[0]);
		break;
	case EVENT_LOG_SUSPECT:
		text_append(text, ", \"player\": ");
		text_slot(text, record->player);
		text_append(text, ", \"check\": \"%s\", \"value\": %.3f",
			record->value[0] >= 0 && record->value[0] < EVENT_LOG_NUMBER_OF_SUSPECTS ?
				suspect_names[record->value[0]] : "unknown",
			record->value[1] / 1000.0);
		break;
	}
	text_append(text, "}");
}

static void write_player(struct text *text, int slot)
{
	struct log_player const *player = &event_log.players[slot];
	struct event_log_player_totals const *totals = &player->totals;
	int icon = player->identity.icon_index & 0xFFFF;
	int chupathingyce = (icon & 0xFF00) == 0x4300;
	int index;
	int any;

	text_append(text, "{\"slot\": %d, \"name\": ", slot);
	text_string(text, player->identity.name);
	text_append(text, ", \"id\": ");
	if (player->id_hash[0])
		text_append(text, "\"%s\"", player->id_hash);
	else
		text_append(text, "null");
	text_append(text, ", \"client\": \"%s\", \"platform\": ", chupathingyce ? "chupathingyce" : "unknown");
	if (chupathingyce)
		text_append(text, "%d", icon & 0xFF);
	else
		text_append(text, "null");
	text_append(text, ", \"machine\": %d, \"team\": %d, \"color\": %d, \"joined\": %d, \"left\": ",
		player->identity.machine_index, player->has_totals ? totals->team : player->identity.team,
		player->identity.color, player->joined_tick);
	if (player->left_tick >= 0)
		text_append(text, "%d", player->left_tick);
	else
		text_append(text, "null");
	text_append(text, ", \"quit\": %s", player->quit ? "true" : "false");
	/* (the game's counts; the kills and deaths seen, for a player whose
	totals never came) */
	text_append(text, ", \"score\": %d, \"kills\": %d, \"deaths\": %d, \"assists\": %d, \"betrayals\": %d, "
		"\"suicides\": %d, \"best_spree\": %d",
		player->has_totals ? totals->score : 0, player->has_totals ? totals->kills : player->kills_counted,
		player->has_totals ? totals->deaths : player->deaths_counted, totals->assists, totals->betrayals,
		totals->suicides, totals->best_spree);
	text_append(text, ", \"damage_dealt\": %.2f, \"damage_taken\": %.2f, \"shots\": %d, \"hits\": %d",
		finite(player->damage_dealt), finite(player->damage_taken), player->shots, player->hits);
	text_append(text, ", \"headshots\": %d, \"melee_kills\": %d, \"grenade_kills\": %d, \"splatters\": %d, "
		"\"explosion_kills\": %d, \"fall_deaths\": %d",
		player->headshots, player->melee_kills, player->grenade_kills, player->splatters, player->explosion_kills,
		player->fall_deaths);
	text_append(text, ", \"grenades\": {\"frag\": %d, \"plasma\": %d}, \"pickups\": {", player->grenades[0],
		player->grenades[1]);
	for (index = 0; index < EVENT_LOG_NUMBER_OF_PICKUPS; index++)
		text_append(text, "%s\"%s\": %d", index ? ", " : "", pickup_names[index], player->pickups[index]);
	text_append(text, "}, \"multikills\": {");
	for (index = 2, any = 0; index <= MAXIMUM_MULTIKILL; index++)
	{
		if (!player->multikills[index])
			continue;
		text_append(text, "%s\"%d\": %d", any ? ", " : "", index, player->multikills[index]);
		any = 1;
	}
	text_append(text, "}, \"weapons\": {");
	for (index = 0, any = 0; index <= player->weapon_count; index++)
	{
		struct log_weapon const *weapon = index < player->weapon_count ? &player->weapons[index] :
			&player->other_weapons;

		if (index == player->weapon_count && !weapon->shots && !weapon->hits && !weapon->kills && !(weapon->damage > 0.0f))
			continue;
		text_append(text, "%s", any ? ", " : "");
		if (index < player->weapon_count)
			text_tag(text, weapon->tag);
		else
			text_append(text, "\"other\"");
		text_append(text, ": {\"shots\": %d, \"hits\": %d, \"kills\": %d, \"damage\": %.2f}", weapon->shots,
			weapon->hits, weapon->kills, finite(weapon->damage));
		any = 1;
	}
	text_append(text, "}, \"vehicles\": {");
	for (index = 0, any = 0; index < player->vehicle_count; index++)
	{
		text_append(text, "%s", any ? ", " : "");
		text_tag(text, player->vehicles[index].tag);
		text_append(text, ": %d", player->vehicles[index].ticks);
		any = 1;
	}
	if (player->other_vehicle_ticks)
		text_append(text, "%s\"other\": %d", any ? ", " : "", player->other_vehicle_ticks);
	text_append(text, "}, \"objectives\": {\"flag_grabs\": %d, \"flag_returns\": %d, \"flag_scores\": %d, "
		"\"flag_carried\": %d, \"ball_ticks\": %d, \"ball_held\": %d, \"ball_carrier_kills\": %d, \"hill_ticks\": %d, "
		"\"laps\": %d, \"best_lap_ticks\": %d}",
		totals->flag_grabs, totals->flag_returns, totals->flag_scores, player->flag_carry_ticks, totals->ball_ticks,
		player->ball_carry_ticks, totals->ball_carrier_kills, totals->hill_ticks, totals->laps, totals->best_lap_ticks);
	text_append(text, ", \"net\": ");
	if (player->ping_count || player->jitter_count || player->loss_count)
	{
		text_append(text, "{\"ping_average\": ");
		if (player->ping_count)
			text_append(text, "%d, \"ping_maximum\": %d", (int)(player->ping_sum / player->ping_count),
				player->ping_maximum);
		else
			text_append(text, "null, \"ping_maximum\": null");
		text_append(text, ", \"jitter_average\": ");
		if (player->jitter_count)
			text_append(text, "%d", (int)(player->jitter_sum / player->jitter_count));
		else
			text_append(text, "null");
		text_append(text, ", \"loss_average\": ");
		if (player->loss_count)
			text_append(text, "%d", (int)(player->loss_sum / player->loss_count));
		else
			text_append(text, "null");
		text_append(text, ", \"samples\": %d}", player->ping_count > player->loss_count ? player->ping_count :
			player->loss_count);
	}
	else
	{
		text_append(text, "null");
	}
	text_append(text, ", \"suspect\": [");
	for (index = 0, any = 0; index < EVENT_LOG_NUMBER_OF_SUSPECTS; index++)
	{
		if (!(player->suspects & (1u << index)))
			continue;
		text_append(text, "%s\"%s\"", any ? ", " : "", suspect_names[index]);
		any = 1;
	}
	text_append(text, "]}");
}

static void hash_id(char const *hardware_id, char *hash)
{
	unsigned char digest[ID_HASH_SIZE];
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
	crypto_blake2b_keyed(digest, sizeof(digest), (unsigned char const *)id_hash_key, sizeof(id_hash_key) - 1,
		(unsigned char const *)lower, (size_t)length);
	for (index = 0; index < ID_HASH_SIZE; index++)
	{
		hash[2 * index] = digits[digest[index] >> 4];
		hash[2 * index + 1] = digits[digest[index] & 15];
	}
	hash[2 * ID_HASH_SIZE] = 0;
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
	event_log.game.variant[sizeof(event_log.game.variant) - 1] = 0;
	event_log.game.server_name[sizeof(event_log.game.server_name) - 1] = 0;
	event_log.game.build[sizeof(event_log.game.build) - 1] = 0;
	event_log.game.architecture[sizeof(event_log.game.architecture) - 1] = 0;
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
	event_log.tag_count = 0;
	event_log.tags_full = 0;
	event_log.player_count = 0;
	event_log.players_full = 0;
}

int event_log_recording(void)
{
	return event_log.recording;
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
	hash_id(player->identity.hardware_id, player->id_hash);
	/* (the id itself is not kept past this) */
	memset(player->identity.hardware_id, 0, sizeof(player->identity.hardware_id));
	player->joined_tick = tick;
	player->left_tick = -1;
	player->other_weapons.tag = EVENT_LOG_NONE;
	return event_log.player_count++;
}

void event_log_player_totals(int slot, struct event_log_player_totals const *totals)
{
	if (!valid_slot(slot))
		return;
	event_log.players[slot].totals = *totals;
	event_log.players[slot].has_totals = 1;
}

void event_log_player_left(int slot, int tick, int quit)
{
	if (!valid_slot(slot))
		return;
	event_log.players[slot].left_tick = tick;
	event_log.players[slot].quit = quit != 0;
}

void event_log_weapon(int slot, int tag, int shots, int hits, int kills, float damage)
{
	struct log_player *player;
	struct log_weapon *weapon;

	if (!valid_slot(slot))
		return;
	player = &event_log.players[slot];
	weapon = player_weapon(player, tag);
	weapon->shots = clamp_add(weapon->shots, shots);
	weapon->hits = clamp_add(weapon->hits, hits);
	weapon->kills = clamp_add(weapon->kills, kills);
	if (isfinite(damage) && damage > 0.0f)
		weapon->damage += damage;
	player->shots = clamp_add(player->shots, shots);
	player->hits = clamp_add(player->hits, hits);
}

void event_log_damage(int dealer, int taker, float amount)
{
	if (!isfinite(amount) || !(amount > 0.0f))
		return;
	if (valid_slot(dealer))
		event_log.players[dealer].damage_dealt += amount;
	if (valid_slot(taker))
		event_log.players[taker].damage_taken += amount;
}

void event_log_vehicle_time(int slot, int tag, int ticks)
{
	struct log_player *player;
	int index;

	if (!valid_slot(slot) || ticks <= 0)
		return;
	player = &event_log.players[slot];
	if (!valid_tag(tag))
	{
		player->other_vehicle_ticks = clamp_add(player->other_vehicle_ticks, ticks);
		return;
	}
	for (index = 0; index < player->vehicle_count; index++)
	{
		if (player->vehicles[index].tag == tag)
		{
			player->vehicles[index].ticks = clamp_add(player->vehicles[index].ticks, ticks);
			return;
		}
	}
	if (player->vehicle_count == EVENT_LOG_MAXIMUM_VEHICLES)
	{
		player->other_vehicle_ticks = clamp_add(player->other_vehicle_ticks, ticks);
		return;
	}
	player->vehicles[player->vehicle_count].tag = (short)tag;
	player->vehicles[player->vehicle_count].ticks = ticks;
	player->vehicle_count++;
}

int event_log_add(struct event_log_record const *source)
{
	struct event_log_record record;
	int priority;
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
		record.tag[index] = tag_index(record.tag[index]);
	if (record.tick < 0)
		record.tick = 0;
	if (record.tick > event_log.last_tick)
		event_log.last_tick = record.tick;
	count_totals(&record);
	priority = event_priorities[record.type];
	if (priority == _priority_sample && !sample_kept(record.tick))
	{
		count_dropped(&record);
		return 0;
	}
	if (event_log.count >= event_log.capacity)
	{
		int room = event_log.capacity > 0 && thin_samples();

		if (!room && priority == _priority_keep && event_log.capacity > 0)
			room = drop_newest_normal();
		/* (the samples thinned: this one may be of a second no longer kept) */
		if (!room || (priority == _priority_sample && !sample_kept(record.tick)))
		{
			count_dropped(&record);
			return 0;
		}
	}
	event_log.records[event_log.count++] = record;
	event_log.priority_counts[priority]++;
	return 1;
}

int event_log_count(void)
{
	return event_log.count;
}

int event_log_dropped(void)
{
	return event_log.dropped_total;
}

char *event_log_finish(struct event_log_end const *end, char const *invite, size_t *length)
{
	struct text text = { NULL, 0, 0, 0 };
	int engine = event_log.game.engine;
	int index;
	int any;

	*length = 0;
	if (!event_log.recording)
		return NULL;
	event_log.recording = 0;
	if (!event_log.player_count)
		return NULL;
	text_append(&text, "{\"schema\": \"%s\", \"version\": %d, \"invite\": ", EVENT_LOG_SCHEMA, EVENT_LOG_VERSION);
	text_string(&text, invite ? invite : "");
	text_append(&text, ", \"server\": {\"name\": ");
	text_string(&text, event_log.game.server_name);
	text_append(&text, ", \"build\": ");
	text_string(&text, event_log.game.build);
	text_append(&text, ", \"architecture\": ");
	text_string(&text, event_log.game.architecture);
	text_append(&text, ", \"network_version\": %d}", event_log.game.network_version);
	text_append(&text, ", \"game\": {\"id\": \"%s\", \"map\": ", event_log.id);
	text_string(&text, event_log.game.map);
	text_append(&text, ", \"variant\": ");
	text_string(&text, event_log.game.variant);
	text_append(&text, ", \"engine\": \"%s\", \"teams\": %s, \"score_limit\": %d, \"maximum_players\": %d, "
		"\"tick_rate\": %d, \"start\": %u, \"end\": %u, \"ticks\": %d, \"end_reason\": \"%s\", \"team_scores\": ",
		engine >= 0 && engine < (int)(sizeof(engine_names) / sizeof(engine_names[0])) ? engine_names[engine] : "unknown",
		event_log.game.teams ? "true" : "false", event_log.game.score_limit, event_log.game.maximum_players,
		EVENT_LOG_TICKS_PER_SECOND, event_log.game.start_time, end->end_time,
		end->tick > event_log.last_tick ? end->tick : event_log.last_tick,
		end->reason >= 0 && end->reason < EVENT_LOG_NUMBER_OF_ENDS ? end_names[end->reason] : "unknown");
	if (event_log.game.teams)
		text_append(&text, "[%d, %d]}", end->red_score, end->blue_score);
	else
		text_append(&text, "null}");
	text_append(&text, ", \"players\": [");
	for (index = 0; index < event_log.player_count; index++)
	{
		if (index)
			text_append(&text, ", ");
		write_player(&text, index);
	}
	text_append(&text, "], \"events\": [");
	for (index = 0; index < event_log.count; index++)
	{
		text_append(&text, index ? ",\n" : "\n");
		write_event(&text, &event_log.records[index]);
	}
	text_append(&text, "], \"limits\": {\"events\": %d, \"capacity\": %d, \"sample_seconds\": %u, \"dropped\": {",
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
	event_log.count = 0;
	if (text.failed)
	{
		free(text.data);
		return NULL;
	}
	*length = text.used;
	return text.data;
}
