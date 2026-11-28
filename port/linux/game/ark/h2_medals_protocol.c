/*
H2_MEDALS_PROTOCOL.C

See h2_medals_protocol.h. From the Halo 2 decompilation (halo2-decomp,
GPL-3.0-or-later): the medal counters' order is the postgame report's
(src/unknown_232d43.cpp, c_postgame_statistics_list_459500::fill_row: the
string ids 0x741..0x758, each named by its length and the postgame string
list's ids); nothing else of Halo 2's is here.
*/

#include "h2_medals_protocol.h"

#include <string.h>

/* the string id names of Halo 2's postgame string list
(ui\screens\game_shell\postgame_statistics\werds: "plase_stick" is the
list's own spelling) */
static char const *const h2_medal_string_ids[H2_MEDAL_COUNT] =
{
	"multiple_kill_2", "multiple_kill_3", "multiple_kill_4", "multiple_kill_5", "multiple_kill_6", "multiple_kill_7",
	"sniper_kill", "collision_kill", "bash_kill", "stealth_kill", "killed_vehicle", "boarded_vehicle", "plase_stick",
	"5_kills_in_a_row", "10_kills_in_a_row", "15_kills_in_a_row", "20_kills_in_a_row", "25_kills_in_a_row",
	"flag_grab", "flag_carrier_kill", "flag_return", "bomb_plant", "bomb_carrier_kill", "bomb_diffused",
};

/* our own plain words (not Halo 2's names: those come from the player's map) */
static char const *const h2_medal_fallbacks[H2_MEDAL_COUNT] =
{
	"2 kills together", "3 kills together", "4 kills together", "5 kills together", "6 kills together", "7 kills together",
	"Sniper kill", "Splatter", "Melee kill", "Melee from behind", "Vehicle destroyed", "Vehicle boarded", "Stuck",
	"5 in a row", "10 in a row", "15 in a row", "20 in a row", "25 in a row",
	"Flag taken", "Flag carrier kill", "Flag returned", "Bomb planted", "Bomb carrier kill", "Bomb defused",
};

char const *h2_medal_string_id(int medal)
{
	return medal >= 0 && medal < H2_MEDAL_COUNT ? h2_medal_string_ids[medal] : "";
}

char const *h2_medal_fallback_words(int medal)
{
	return medal >= 0 && medal < H2_MEDAL_COUNT ? h2_medal_fallbacks[medal] : "";
}

void h2_medals_net_encode(struct h2_medals_net_entry *entry, int player, struct h2_medal_record const *record)
{
	memset(entry, 0, sizeof(*entry));
	entry->version = H2_MEDALS_NET_VERSION;
	entry->player = (uint8_t)player;
	entry->best_spree = record->best_spree;
	entry->life_ticks = record->life_ticks;
	memcpy(entry->counts, record->counts, sizeof(entry->counts));
	memcpy(entry->kills_of, record->kills_of, sizeof(entry->kills_of));
	memcpy(entry->type_counts, record->type_counts, sizeof(entry->type_counts));
}

int h2_medals_net_decode(struct h2_medals_net_entry const *entry, int *player, struct h2_medal_record *record)
{
	if (entry->version != H2_MEDALS_NET_VERSION || entry->player >= H2_MEDALS_PLAYERS)
		return 0;
	*player = entry->player;
	memset(record, 0, sizeof(*record));
	record->best_spree = entry->best_spree;
	record->life_ticks = entry->life_ticks;
	memcpy(record->counts, entry->counts, sizeof(record->counts));
	memcpy(record->kills_of, entry->kills_of, sizeof(record->kills_of));
	memcpy(record->type_counts, entry->type_counts, sizeof(record->type_counts));
	return 1;
}

void h2_medals_table_reset(struct h2_medals_table *table)
{
	memset(table, 0, sizeof(*table));
}

int h2_medals_rate_take(struct h2_medals_rate *rate, uint32_t now_ms)
{
	uint32_t elapsed;

	if (!rate->started)
	{
		rate->started = 1;
		rate->tokens = H2_MEDALS_BURST;
		rate->last_ms = now_ms;
	}
	elapsed = now_ms - rate->last_ms;
	if (elapsed >= H2_MEDALS_REFILL_MS)
	{
		uint32_t gained = elapsed / H2_MEDALS_REFILL_MS;

		rate->tokens = gained >= H2_MEDALS_BURST ? H2_MEDALS_BURST : rate->tokens + (int32_t)gained;
		if (rate->tokens > H2_MEDALS_BURST)
			rate->tokens = H2_MEDALS_BURST;
		rate->last_ms += gained * H2_MEDALS_REFILL_MS;
	}
	if (rate->tokens <= 0)
		return 0;
	rate->tokens--;
	return 1;
}

int h2_medals_table_take(struct h2_medals_table *table, struct h2_medals_net_entry const *entry, uint32_t now_ms)
{
	struct h2_medal_record record;
	int player;

	if (!h2_medals_rate_take(&table->rate, now_ms))
		return 0;
	if (!h2_medals_net_decode(entry, &player, &record))
		return 0;
	if (table->known[player] && !memcmp(&table->records[player], &record, sizeof(record)))
		return 0;
	table->records[player] = record;
	table->known[player] = 1;
	table->serial++;
	return 1;
}

struct h2_medal_record const *h2_medals_table_get(struct h2_medals_table const *table, int player)
{
	if (player < 0 || player >= H2_MEDALS_PLAYERS || !table->known[player])
		return 0;
	return &table->records[player];
}
