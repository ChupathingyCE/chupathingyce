/*
H2_MEDALS_PROTOCOL.H

A Halo 2 game's medals and post-game counts as data, with no game in it,
so that they are tested and fuzzed on their own (port/vita/tests/
net_fuzz_h2_medals.c): a player's record, the entry the host sends its
joiners between this project's builds, the joiner's table and its rate
limit. h2_medals.c counts the records in the game and carries the entries
in the distributed netcode's messages (never on the crossplay wire).

The medals are Halo 2's own 24 (the medal counters function_1970a0 of the
decompilation, src/unknown_196d20.cpp, is given: its awards in
src/unknown_157450.cpp, unknown_241620.cpp and unknown_2420a0.cpp), in the
order the postgame carnage report names them (src/unknown_232d43.cpp
c_postgame_statistics_list_459500::fill_row's string ids): the chains of
2..7+, sniper, collision, bash, stealth, vehicle destroyed, vehicle boarded,
plasma stick, the sprees of 5, 10, 15, 20 and 25, flag grab, flag carrier
kill, flag return, bomb plant, bomb carrier kill, bomb defused.
*/

#ifndef HALO_H2_MEDALS_PROTOCOL_H
#define HALO_H2_MEDALS_PROTOCOL_H

#include <stdint.h>

enum
{
	H2_MEDAL_MULTIPLE_KILL_2 = 0,
	H2_MEDAL_MULTIPLE_KILL_3,
	H2_MEDAL_MULTIPLE_KILL_4,
	H2_MEDAL_MULTIPLE_KILL_5,
	H2_MEDAL_MULTIPLE_KILL_6,
	H2_MEDAL_MULTIPLE_KILL_7,
	H2_MEDAL_SNIPER_KILL,
	H2_MEDAL_COLLISION_KILL,
	H2_MEDAL_BASH_KILL,
	H2_MEDAL_STEALTH_KILL,
	H2_MEDAL_KILLED_VEHICLE,
	H2_MEDAL_BOARDED_VEHICLE,
	H2_MEDAL_PLASMA_STICK,
	H2_MEDAL_5_KILLS_IN_A_ROW,
	H2_MEDAL_10_KILLS_IN_A_ROW,
	H2_MEDAL_15_KILLS_IN_A_ROW,
	H2_MEDAL_20_KILLS_IN_A_ROW,
	H2_MEDAL_25_KILLS_IN_A_ROW,
	H2_MEDAL_FLAG_GRAB,
	H2_MEDAL_FLAG_CARRIER_KILL,
	H2_MEDAL_FLAG_RETURN,
	H2_MEDAL_BOMB_PLANT,
	H2_MEDAL_BOMB_CARRIER_KILL,
	H2_MEDAL_BOMB_DEFUSED,
	H2_MEDAL_COUNT
};

/* the game's players (their absolute indices), and how many of them the
kills between pairs are kept for (Halo 2's own sixteen: the report's
"killed" and "killed by" columns) */
#define H2_MEDALS_PLAYERS 128
#define H2_MEDALS_VERSUS 16

#define H2_MEDALS_NET_VERSION 1
/* what a joiner takes of the host: this many entries at once, one more
each H2_MEDALS_REFILL_MS (the host sends a player's entry twice a second
when it changed, every player's once a second at most, and all of them
once to a machine that has loaded) */
#define H2_MEDALS_BURST 256
#define H2_MEDALS_REFILL_MS 100

/* a player's record: each medal's count (capped), the longest spree, the
ticks spent alive (the report's average life), kills of each of the first
sixteen players, and the game type's two counts of the report's player
page that this engine does not keep itself: King's kills on the hill and
from it (Halo 2's statistics 28 and 27), Territories' taken and lost (43
and 44) */
struct h2_medal_record
{
	uint8_t counts[H2_MEDAL_COUNT];
	uint16_t best_spree;
	uint16_t pad;
	uint32_t life_ticks;
	uint8_t kills_of[H2_MEDALS_VERSUS];
	uint16_t type_counts[2];
};

/* the host's entry for one player (52 bytes) */
struct h2_medals_net_entry
{
	uint8_t version;
	uint8_t player;
	uint16_t best_spree;
	uint32_t life_ticks;
	uint8_t counts[H2_MEDAL_COUNT];
	uint8_t kills_of[H2_MEDALS_VERSUS];
	uint16_t type_counts[2];
};

typedef char h2_medals_net_entry_size_check[sizeof(struct h2_medals_net_entry) == 52 ? 1 : -1];

struct h2_medals_rate
{
	int32_t tokens;
	uint32_t last_ms;
	uint8_t started;
};

struct h2_medals_table
{
	struct h2_medal_record records[H2_MEDALS_PLAYERS];
	uint8_t known[H2_MEDALS_PLAYERS];
	struct h2_medals_rate rate;
	uint32_t serial;
};

/* the medal's string id name in Halo 2's postgame string list
(ui\screens\game_shell\postgame_statistics\werds), and our own plain words
for it when the player's map has none */
char const *h2_medal_string_id(int medal);
char const *h2_medal_fallback_words(int medal);

void h2_medals_net_encode(struct h2_medals_net_entry *entry, int player, struct h2_medal_record const *record);
/* 1 and the player and record it carries; 0 for one that cannot be taken
(its version, its player out of range) */
int h2_medals_net_decode(struct h2_medals_net_entry const *entry, int *player, struct h2_medal_record *record);

void h2_medals_table_reset(struct h2_medals_table *table);
/* a joiner takes an entry of the host's: 1 when kept (its rate let it
through, it decoded, and it differs from what was kept) */
int h2_medals_table_take(struct h2_medals_table *table, struct h2_medals_net_entry const *entry, uint32_t now_ms);
struct h2_medal_record const *h2_medals_table_get(struct h2_medals_table const *table, int player);
int h2_medals_rate_take(struct h2_medals_rate *rate, uint32_t now_ms);

#endif
