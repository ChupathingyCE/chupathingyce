/*
H2_MEDALS.C

A Halo 2 level's medals and post-game counts (h2_medals.h). Halo 2's rules,
from the decompilation (halo2-decomp, GPL-3.0-or-later):

- A kill's medals (src/unknown_157450.cpp 3905-3922, at the attacker's kill
  of another player who is not a teammate): the chain's - its kills within
  four seconds of the last (unknown_1e9700.cpp function_1e9fa0: a threshold
  of 4 * ticks a second, function_1ea5b0: within it the chain grows, else it
  is 1; this engine's multiple_kills counts the same, game_statistics.c's
  120 ticks) - a medal at 2, 3, 4, 5, 6 and 7 or more; the spree's
  (consecutive kills without a death) at exactly 5, 10, 15, 20 and 25; a
  sniper rifle's or beam rifle's kill (damage types 0xd, 0xe) the sniper's;
  a collision's (damage type 2 or modifier 3) the collision's; a plasma
  grenade's attached detonation (type 0x16, modifier 2) the stick's; a
  melee's the bash's, from behind (modifier 2) the stealth's. Several can
  come of one kill.
- The flag's (unknown_2420a0.cpp): taken from its stand (v23, the first
  pickup since it was home), its carrier killed (v30), returned
  (unknown_241620.cpp); the bomb's the same in Assault (none here).
- The longest spree (function_1ea5b0: statistic 12, the most consecutive)
  and the time alive (the report's average life, statistic 13 over deaths
  + 1) are the report's player page.
- The kills between each pair of players (function_197160: the killer's
  kill of the victim, when hostile) are its "killed" and "killed by" page.
- A vehicle boarded (counter 0xb, unknown_157450.cpp function_15e7f0): a
  player's unit entering a seat with Halo 2's boarding seat flag (bit 11),
  with the general event 44.
- King's kills on the hill and from it (unknown_2bd960.cpp c_game_engine_a::v0:
  statistics 0x1c, the victim on the hill, and 0x1b, the killer on it, neither
  a betrayal nor a suicide), Territories' taken and lost (territory_set_holder:
  0x2b the new holder, 0x2c the old).
- A vehicle destroyed (medal 10): no award in the Xbox build (no call of
  function_1970a0 gives it), but MCC's halo2.dll names it (read as a string,
  _game_results_medal_new_destroy_opponents_vehicle_killing_driver): given by
  that name alone, to the killer of an opponent driving a vehicle that died.
The bomb's (21-23): no Assault here, never given.

Counted where the game is run (the host, a local game); a joiner is sent
the host's records in the distributed netcode's own message
(_distributed_message_h2_medals), only when the wire is this project's
(never a crossplay game's), host to client only, each entry checked and
rate limited (h2_medals_protocol.c, fuzzed by net_fuzz_h2_medals.c).
*/

/* ---------- headers */

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "items/weapons.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "objects/objects.h"
#include "units/units.h"

#include "h2_hud.h"
#include "h2_medals.h"
#include "h2_rules.h"
#include "network_distributed.h"
#include "network_wire.h"

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the entries of one message (a datagram's room: 52 bytes each) */
	H2_MEDALS_ENTRIES_PER_MESSAGE = 16,
	/* the host's sends: a change at most this often, every player once in
	H2_MEDALS_REFRESH_TICKS */
	H2_MEDALS_SEND_TICKS = 15,
	H2_MEDALS_REFRESH_TICKS = 60,
	H2_MEDALS_REFRESH_PLAYERS = 8,
	/* (type_counts: King's, Territories') */
	H2_MEDALS_TYPE_ON_HILL = 0,
	H2_MEDALS_TYPE_FROM_HILL = 1,
	H2_MEDALS_TYPE_TERRITORIES_TAKEN = 0,
	H2_MEDALS_TYPE_TERRITORIES_LOST = 1,
	/* (Halo 2's game events, multiplayer_globals' general list) */
	H2_HUD_EVENTS_GENERAL = 0,
	H2_HUD_GENERAL_BOARDED_VEHICLE = 44,
};

typedef char h2_medals_players_check[H2_MEDALS_PLAYERS == HALO_PORT_MAXIMUM_NETWORK_PLAYERS ? 1 : -1];

/* ---------- globals */

static struct
{
	/* the game counted here (the host's, a local game's) */
	boolean counting;
	struct h2_medal_record records[H2_MEDALS_PLAYERS];
	boolean present[H2_MEDALS_PLAYERS];
	/* who carried a flag at the last tick (a carrier's death this tick) */
	boolean carrying[H2_MEDALS_PLAYERS];
	/* the vehicle each drove at the last tick (a driver's death with it) */
	long driving[H2_MEDALS_PLAYERS];
	/* the host's sends: each record's checksum as last sent, the refresh cursor */
	unsigned long sent[H2_MEDALS_PLAYERS];
	short cursor;
	boolean due;
	/* a joiner's table of the host's */
	struct h2_medals_table taken;
	boolean any_taken;
} h2_medals;

struct h2_medals_message
{
	struct distributed_message_header header;
	struct h2_medals_net_entry entries[H2_MEDALS_ENTRIES_PER_MESSAGE];
};

void platform_log(char const *format, ...);
/* (h2_hud.c) a postgame string of the level's HUD file by its id's name */
char const *h2_hud_word(char const *id);
void h2_hud_mp_event(int kind, int event, long cause_player_index, long effect_player_index);

/* ---------- private code */

/* whether this machine runs the game (not a joiner) */
static boolean h2_medals_runs_game(void)
{
	return !network_game_distributed_client() && !global_network_game_client_get() ? TRUE :
		global_network_game_server_get() != NULL;
}

static boolean h2_medals_level(void)
{
	return h2_rules_level() && game_engine != NULL;
}

static struct h2_medal_record *h2_medals_own(long player_index)
{
	long absolute = player_index != NONE ? (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) : NONE;

	if (absolute < 0 || absolute >= H2_MEDALS_PLAYERS)
		return NULL;
	h2_medals.present[absolute] = TRUE;
	return &h2_medals.records[absolute];
}

static void h2_medals_award(long player_index, int medal)
{
	struct h2_medal_record *record = h2_medals_own(player_index);

	if (!record || medal < 0 || medal >= H2_MEDAL_COUNT)
		return;
	if (record->counts[medal] < 255)
		record->counts[medal]++;
	h2_medals.due = TRUE;
	if (getenv("HALO_H2_EVENT_LOG") || getenv("HALO_H2_MEDAL_LOG"))
		platform_log("h2 medals: player %ld: %s (%d)", (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index), h2_medal_string_id(medal),
			(int)record->counts[medal]);
}

static boolean h2_medals_unit_carries_flag(long unit_index)
{
	struct unit_datum *unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;
	long k;

	if (!unit)
		return FALSE;
	for (k = 0; k < MAXIMUM_WEAPONS_PER_UNIT; k++)
	{
		long weapon_index = unit->unit.weapon_object_indices[k];

		if (weapon_index != NONE && object_try_and_get(weapon_index) && weapon_is_flag(weapon_index))
			return TRUE;
	}
	return FALSE;
}

/* the vehicle a player's unit drives, NONE for none */
static long h2_medals_unit_driving(long unit_index)
{
	struct unit_datum *unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;

	if (!unit || unit->object.parent_object_index == NONE || !unit_try_and_get(unit->object.parent_object_index) ||
		object_get(unit->object.parent_object_index)->object.type != _object_type_vehicle ||
		!unit_seat_is_driver(unit->object.parent_object_index, unit->unit.parent_seat_index))
	{
		return NONE;
	}
	return unit->object.parent_object_index;
}

static boolean h2_medals_vehicle_dead(long vehicle_index)
{
	struct object_datum *vehicle = vehicle_index != NONE ? object_try_and_get(vehicle_index) : NULL;

	return vehicle && TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit);
}

static unsigned long h2_medals_checksum(struct h2_medal_record const *record)
{
	unsigned char const *bytes = (unsigned char const *)record;
	unsigned long checksum = 2166136261UL;
	size_t index;

	for (index = 0; index < sizeof(*record); index++)
		checksum = (checksum ^ bytes[index]) * 16777619UL;
	return checksum;
}

/* the distributed netcode carries them: this project's wire only */
static boolean h2_medals_network_wanted(void)
{
	return h2_medals.counting && global_network_game_server_get() != NULL && !network_wire_crossplay();
}

/* ---------- public code */

void h2_medals_game_start(void)
{
	memset(&h2_medals, 0, sizeof(h2_medals));
	h2_medals_table_reset(&h2_medals.taken);
	memset(h2_medals.driving, 0xFF, sizeof(h2_medals.driving));
	if (!h2_medals_level())
		return;
	h2_medals.counting = h2_medals_runs_game();
	platform_log("h2 medals: a Halo 2 level's game: %s", h2_medals.counting ? "counted here" :
		network_wire_crossplay() ? "a joiner of a crossplay game: no medals" : "a joiner: the host's");
}

void h2_medals_update(void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (!h2_medals.counting || !h2_medals_level())
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute = (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct unit_datum *unit = player->unit_index != NONE ? unit_try_and_get(player->unit_index) : NULL;
		boolean alive = unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit);

		if (absolute < 0 || absolute >= H2_MEDALS_PLAYERS || player->quit_out_of_game)
			continue;
		h2_medals.present[absolute] = TRUE;
		if (alive && h2_medals.records[absolute].life_ticks < 0x7FFFFFFFUL)
			h2_medals.records[absolute].life_ticks++;
		h2_medals.carrying[absolute] = alive && h2_medals_unit_carries_flag(player->unit_index);
		h2_medals.driving[absolute] = alive ? h2_medals_unit_driving(player->unit_index) : NONE;
	}
}

void h2_medals_player_killed(long killing_player_index, long dead_player_index, boolean friendly_fire,
	short multiple_kills, short kills_in_a_row, short marks)
{
	struct player_datum *killer, *dead;
	short category = (short)(marks & 0xFF);
	long dead_absolute, killer_absolute;
	struct h2_medal_record *record;

	if (!h2_medals.counting || !h2_medals_level() || killing_player_index == NONE || dead_player_index == NONE ||
		killing_player_index == dead_player_index || friendly_fire)
	{
		return;
	}
	killer = player_try_and_get(killing_player_index);
	dead = player_try_and_get(dead_player_index);
	if (!killer || !dead)
		return;
	dead_absolute = (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(dead_player_index);
	killer_absolute = (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(killing_player_index);
	record = h2_medals_own(killing_player_index);
	if (!record)
		return;
	/* the chain (function_1970a0 counters 0..5) */
	if (multiple_kills >= 7)
		h2_medals_award(killing_player_index, H2_MEDAL_MULTIPLE_KILL_7);
	else if (multiple_kills >= 2)
		h2_medals_award(killing_player_index, H2_MEDAL_MULTIPLE_KILL_2 + multiple_kills - 2);
	/* the spree (counters 0xd..0x11) */
	if (kills_in_a_row == 5)
		h2_medals_award(killing_player_index, H2_MEDAL_5_KILLS_IN_A_ROW);
	else if (kills_in_a_row == 10)
		h2_medals_award(killing_player_index, H2_MEDAL_10_KILLS_IN_A_ROW);
	else if (kills_in_a_row == 15)
		h2_medals_award(killing_player_index, H2_MEDAL_15_KILLS_IN_A_ROW);
	else if (kills_in_a_row == 20)
		h2_medals_award(killing_player_index, H2_MEDAL_20_KILLS_IN_A_ROW);
	else if (kills_in_a_row == 25)
		h2_medals_award(killing_player_index, H2_MEDAL_25_KILLS_IN_A_ROW);
	/* the kill's kind (counters 6, 7, 0xc, 8 and 9) */
	if (category == H2_HUD_DAMAGE_SNIPER)
		h2_medals_award(killing_player_index, H2_MEDAL_SNIPER_KILL);
	if (category == H2_HUD_DAMAGE_VEHICLE)
		h2_medals_award(killing_player_index, H2_MEDAL_COLLISION_KILL);
	if (marks & 0x100)
		h2_medals_award(killing_player_index, H2_MEDAL_PLASMA_STICK);
	if (category == H2_HUD_DAMAGE_MELEE)
		h2_medals_award(killing_player_index, (marks & 0x200) ? H2_MEDAL_STEALTH_KILL : H2_MEDAL_BASH_KILL);
	/* the flag's carrier (v30: the victim holding a flag, not a suicide) */
	if (dead_absolute >= 0 && dead_absolute < H2_MEDALS_PLAYERS &&
		(h2_medals.carrying[dead_absolute] || h2_medals_unit_carries_flag(dead->unit_index)))
	{
		h2_medals_award(killing_player_index, H2_MEDAL_FLAG_CARRIER_KILL);
	}
	/* the opponent's vehicle destroyed, the driver with it (MCC's name of
	the medal; the last tick's driver: the wreck may have thrown him out) */
	if (dead_absolute >= 0 && dead_absolute < H2_MEDALS_PLAYERS &&
		(h2_medals_vehicle_dead(h2_medals.driving[dead_absolute]) || h2_medals_vehicle_dead(h2_medals_unit_driving(dead->unit_index))))
	{
		h2_medals_award(killing_player_index, H2_MEDAL_KILLED_VEHICLE);
	}
	/* the longest spree (statistic 12), the kills of each (function_197160) */
	if (kills_in_a_row > (short)record->best_spree)
		record->best_spree = (unsigned short)kills_in_a_row;
	if (dead_absolute >= 0 && dead_absolute < H2_MEDALS_VERSUS && record->kills_of[dead_absolute] < 255)
		record->kills_of[dead_absolute]++;
	h2_medals.due = TRUE;
	(void)killer_absolute;
}

void h2_medals_flag_grabbed(long player_index)
{
	if (h2_medals.counting && h2_medals_level())
		h2_medals_award(player_index, H2_MEDAL_FLAG_GRAB);
}

void h2_medals_flag_returned(long player_index)
{
	if (h2_medals.counting && h2_medals_level())
		h2_medals_award(player_index, H2_MEDAL_FLAG_RETURN);
}

void h2_medals_boarded(long player_index)
{
	/* (the event's line and announcer on every machine; the count on the host's) */
	if (!h2_medals_level() || player_index == NONE)
		return;
	h2_hud_mp_event(H2_HUD_EVENTS_GENERAL, H2_HUD_GENERAL_BOARDED_VEHICLE, player_index, NONE);
	if (h2_medals.counting)
		h2_medals_award(player_index, H2_MEDAL_BOARDED_VEHICLE);
}

static void h2_medals_type_count(long player_index, int which)
{
	struct h2_medal_record *record = h2_medals_own(player_index);

	if (!record)
		return;
	if (record->type_counts[which] < 0xFFFF)
		record->type_counts[which]++;
	h2_medals.due = TRUE;
	if (getenv("HALO_H2_MEDAL_LOG"))
		platform_log("h2 medals: player %ld: objective count %d (%d)", (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index), which,
			(int)record->type_counts[which]);
}

void h2_medals_king_kill(long killing_player_index, long dead_player_index, boolean friendly_fire, boolean killer_on_hill,
	boolean dead_on_hill)
{
	if (!h2_medals.counting || !h2_medals_level() || friendly_fire || killing_player_index == NONE ||
		killing_player_index == dead_player_index)
	{
		return;
	}
	if (dead_on_hill)
		h2_medals_type_count(killing_player_index, H2_MEDALS_TYPE_ON_HILL);
	if (killer_on_hill)
		h2_medals_type_count(killing_player_index, H2_MEDALS_TYPE_FROM_HILL);
}

void h2_medals_territory_changed(long previous_player_index, long holder_player_index)
{
	if (!h2_medals.counting || !h2_medals_level())
		return;
	if (previous_player_index != NONE && player_try_and_get(previous_player_index))
		h2_medals_type_count(previous_player_index, H2_MEDALS_TYPE_TERRITORIES_LOST);
	if (holder_player_index != NONE && player_try_and_get(holder_player_index))
		h2_medals_type_count(holder_player_index, H2_MEDALS_TYPE_TERRITORIES_TAKEN);
}

boolean h2_medals_active(void)
{
	return h2_medals_level() && (h2_medals.counting || h2_medals.any_taken);
}

boolean h2_medals_record(long player_index, struct h2_medal_record *record)
{
	long absolute = player_index != NONE ? (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) : NONE;

	memset(record, 0, sizeof(*record));
	if (absolute < 0 || absolute >= H2_MEDALS_PLAYERS || !h2_medals_level())
		return FALSE;
	if (h2_medals.counting)
	{
		*record = h2_medals.records[absolute];
		return h2_medals.present[absolute];
	}
	else
	{
		struct h2_medal_record const *taken = h2_medals_table_get(&h2_medals.taken, (int)absolute);

		if (!taken)
			return FALSE;
		*record = *taken;
		return TRUE;
	}
}

char const *h2_medals_word(char const *id, char const *fallback)
{
	char const *words = h2_hud_word(id);

	return words && words[0] ? words : fallback;
}

char const *h2_medals_words(int medal)
{
	return h2_medals_word(h2_medal_string_id(medal), h2_medal_fallback_words(medal));
}

/* ---------- the network */

unsigned short h2_medals_network_entry_size(void)
{
	return (unsigned short)sizeof(struct h2_medals_net_entry);
}

static void h2_medals_send_message(struct h2_medals_message *message, short count, long machine_index)
{
	word size = (word)(sizeof(message->header) + (size_t)count * sizeof(struct h2_medals_net_entry));

#ifdef HALO_H2_ONLINE
	if (machine_index == NONE)
		distributed_send(message, _distributed_message_h2_medals, count, size, _distributed_to_clients);
	else
		distributed_send_to_machine(machine_index, message, _distributed_message_h2_medals, count, size);
#else
	/* (port) Halo 2 mode plays offline and split screen for now: no joiner
	to send the host's medals to (PLAN-next: Delta) */
	(void)machine_index; (void)message; (void)size;
#endif
}

void h2_medals_network_send(boolean refresh)
{
	struct h2_medals_message message;
	short count = 0, refreshed = 0, step;
	long game_time = game_time_get();

	if (!h2_medals_network_wanted())
		return;
	if (!h2_medals.due && game_time % H2_MEDALS_SEND_TICKS != 0)
		return;
	h2_medals.due = FALSE;
	for (step = 0; step < H2_MEDALS_PLAYERS; step++)
	{
		short player_index = (short)((h2_medals.cursor + step) % H2_MEDALS_PLAYERS);
		unsigned long checksum;
		boolean force = refresh && refreshed < H2_MEDALS_REFRESH_PLAYERS;

		if (!h2_medals.present[player_index] || !distributed_player(player_index))
			continue;
		checksum = h2_medals_checksum(&h2_medals.records[player_index]);
		if (checksum == h2_medals.sent[player_index] && !force)
			continue;
		if (force)
			refreshed++;
		h2_medals.sent[player_index] = checksum;
		h2_medals_net_encode(&message.entries[count++], player_index, &h2_medals.records[player_index]);
		if (count == H2_MEDALS_ENTRIES_PER_MESSAGE)
		{
			h2_medals_send_message(&message, count, NONE);
			count = 0;
		}
	}
	if (refresh)
		h2_medals.cursor = (short)((h2_medals.cursor + H2_MEDALS_REFRESH_PLAYERS) % H2_MEDALS_PLAYERS);
	if (count)
		h2_medals_send_message(&message, count, NONE);
}

void h2_medals_network_send_all(long machine_index)
{
	struct h2_medals_message message;
	short count = 0, player_index;

	if (!h2_medals_network_wanted())
		return;
	for (player_index = 0; player_index < H2_MEDALS_PLAYERS; player_index++)
	{
		if (!h2_medals.present[player_index] || !distributed_player(player_index))
			continue;
		h2_medals_net_encode(&message.entries[count++], player_index, &h2_medals.records[player_index]);
		if (count == H2_MEDALS_ENTRIES_PER_MESSAGE)
		{
			h2_medals_send_message(&message, count, machine_index);
			count = 0;
		}
	}
	if (count)
		h2_medals_send_message(&message, count, machine_index);
}

void h2_medals_network_handle(void const *entries, short count, long machine_index)
{
	struct h2_medals_net_entry const *entry = (struct h2_medals_net_entry const *)entries;
	unsigned long now = system_milliseconds();
	short index;
	int taken = 0;

	/* (the host's alone - a client has no host of its own to send to -, on
	this project's wire, on a joiner of a Halo 2 level's game) */
	if (machine_index != NONE || h2_medals.counting || !network_game_distributed_client() || network_wire_crossplay() ||
		!h2_medals_level())
	{
		return;
	}
	for (index = 0; index < count; index++)
		taken += h2_medals_table_take(&h2_medals.taken, &entry[index], (uint32_t)now);
	if (taken)
	{
		h2_medals.any_taken = TRUE;
		if (getenv("HALO_H2_MEDAL_LOG"))
			platform_log("h2 medals: the host's records: %d of %d entries taken", taken, (int)count);
	}
}
