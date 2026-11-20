#include "harness.h"

/* player_new's bookkeeping of a machine's players (machine_add_player): a
machine with its four entries taken, or one outside the table, gets no
more players. The game goes on: player_new makes nothing and returns
NONE, which its callers already take as a player that could not be added. */

#include "config.inc"
#include <wchar.h>
enum { _player_action_result_reload = 3, _error_silent = 0 };
struct network_player { wchar_t name[12]; long machine_index; long controller_index; };
struct player_datum
{
	short identifier;
	wchar_t name[12];
	short local_player_index;
	long unit_index, dead_unit_index, squad_index, cluster_index, aim_assist_unit_index;
	real speed_multiplier;
	short team_index;
	long action_result, action_object_index;
	long quit_out_of_game_time;
	boolean quit_out_of_game;
	struct network_player network_player_data;
};

static struct data_array *player_data;
/* (the table, and after it a row nobody may write: a machine past the table
would index it) */
static struct { long table[MAXIMUM_NETWORK_MACHINE_COUNT][MAXIMUM_LOCAL_PLAYERS]; long past[2][MAXIMUM_LOCAL_PLAYERS]; } machines;
#define machine_to_player_table machines.table
static int errors;

static long datum_new_at_index(struct data_array *array, long index)
{
	struct datum_header *header = datum_get(array, index);
	if (header->identifier) return NONE;
	memset(header, 0, (size_t)array->size);
	header->identifier = (short)(index >> 16);
	array->count++;
	return index;
}
static struct player_datum *player_get(long index)
{
	struct player_datum *player = datum_get(player_data, index);
	CHECK(index != NONE && player->identifier == (short)(index >> 16), "player 0x%lx is unused or changed", (unsigned long)index);
	return player;
}
static void player_delete(long index) { player_get(index); datum_delete(player_data, index); }
static wchar_t *ustrncpy(wchar_t *destination, wchar_t const *source, unsigned long count)
{
	unsigned long index;
	for (index = 0; index < count && source[index]; index++) destination[index] = source[index];
	for (; index < count; index++) destination[index] = 0;
	return destination;
}
#define csmemcpy memcpy
#define error(priority, ...) (errors++)
/* (the Xbox game's fatal error: the process ends) */
static void display_assert(char const *message, char const *file, int line, boolean halt)
{
	(void)file; (void)line; (void)halt;
	fprintf(stderr, "assertion: %s\n", message);
}
static void system_exit(int code) { (void)code; CHECK(FALSE, "system_exit: the process ended"); }
#include "under_test.inc"

static long players(void) { return player_data->count; }

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct network_player network_player;
	long made[MAXIMUM_LOCAL_PLAYERS];
	long index, extra;

	player_data = game_state_data_new("players", 16, sizeof(struct player_datum));
	memset(&machines, 0xFF, sizeof(machines));
	memset(&network_player, 0, sizeof(network_player));
	network_player.machine_index = 7;
	for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
	{
		network_player.controller_index = index;
		made[index] = player_new(7, NONE, NONE, &network_player);
		CHECK(made[index] != NONE, "a machine's player %ld is made", index);
	}
	CHECK(players() == MAXIMUM_LOCAL_PLAYERS && !errors, "four players");

	CASE("four")
	{
		CHECK(machine_get_player_list(7)[3] == made[3], "the machine lists its four players");
		return 0;
	}
	CASE("fifth")
	{
		extra = player_new(7, NONE, NONE, &network_player);
		CHECK(extra == NONE && players() == MAXIMUM_LOCAL_PLAYERS && errors == 1,
			"a fifth player of a full machine is refused, nothing made");
		return 0;
	}
	CASE("fifth-at-slot")
	{
		extra = player_new(7, ((long)0x9000 << 16) | 10, NONE, &network_player);
		CHECK(extra == NONE && players() == MAXIMUM_LOCAL_PLAYERS, "... also at a slot of its own");
		return 0;
	}
	CASE("machine-past-table")
	{
		extra = player_new(MAXIMUM_NETWORK_MACHINE_COUNT, NONE, NONE, &network_player);
		CHECK(extra == NONE && players() == MAXIMUM_LOCAL_PLAYERS, "a player of a machine past the table is refused");
		extra = player_new(-2, NONE, NONE, &network_player);
		CHECK(extra == NONE && players() == MAXIMUM_LOCAL_PLAYERS, "... and one of a negative machine");
		for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
			CHECK(machines.past[0][index] == NONE, "nothing written past the table");
		return 0;
	}
	CASE("room-again")
	{
		machine_remove_player(made[2]);
		player_delete(made[2]);
		extra = player_new(7, NONE, NONE, &network_player);
		CHECK(extra != NONE && machine_get_player_list(7)[2] == extra, "a player who left gives his entry to a new one");
		extra = player_new(8, NONE, NONE, &network_player);
		CHECK(extra != NONE && machine_get_player_list(8)[0] == extra, "another machine's player is made");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
