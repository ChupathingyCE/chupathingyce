/* A host holding late joiners until the next game (port/linux/game/
   late_join.c): a player added a while into a game is held while someone
   else plays; never for more than 5 minutes; never one who was in this game
   and came back (the same name); an OpenCE player held is told once. */
#include <wchar.h>
#include "harness.h"
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define TICKS_PER_SECOND 30
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 16
#define MAXIMUM_LOCAL_PLAYERS 4
#define HALO_PORT_CHAT
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define csmemset memset
#define csmemcpy memcpy
#define csmemcmp memcmp

struct player_datum
{
	struct datum_header header;
	wchar_t name[12];
	boolean quit_out_of_game;
	long unit_index;
	short local_player_index;
	struct { short deaths; } statistics;
	struct { char machine_index; } network_player_data;
};
struct data_iterator { struct data_array *array; long next; long datum_index; };
struct network_game_server { int unused; };

static struct data_array *player_data;
static struct network_game_server host_server;
static long now_ticks;
static int told;

static struct network_game_server *global_network_game_server_get(void) { return &host_server; }
static boolean game_engine_running(void) { return TRUE; }
static boolean game_engine_showing_postgame(void) { return FALSE; }
static long game_time_get(void) { return now_ticks; }
static boolean host_rules_host_late_join_hold(void) { return TRUE; }
static boolean host_rules_late_join_hold(void) { return TRUE; }
static int chat_host_notice_to_machine(short machine, char const *text) { (void)machine; (void)text; told++; return 1; }
static struct player_datum *player_get(long index) { return datum_get(player_data, index); }
static struct player_datum *player_try_and_get(long index) { return datum_get(player_data, index); }
static void data_iterator_new(struct data_iterator *iterator, struct data_array *array)
{
	iterator->array = array;
	iterator->next = 0;
	iterator->datum_index = NONE;
}
static void *data_iterator_next(struct data_iterator *iterator)
{
	for (; iterator->next < iterator->array->maximum_count; iterator->next++)
	{
		struct datum_header *header = (struct datum_header *)(iterator->array->data + iterator->next * iterator->array->size);

		if (header->identifier)
		{
			iterator->datum_index = ((long)(unsigned short)header->identifier << 16) | iterator->next;
			iterator->next++;
			return header;
		}
	}
	return NULL;
}

#include "under_test.inc"

static long add_player(const wchar_t *name, int machine)
{
	long index = datum_new(player_data);
	struct player_datum *player = player_get(index);

	wcsncpy(player->name, name, 11);
	player->unit_index = NONE;
	player->network_player_data.machine_index = (char)machine;
	late_join_player_added(index);
	return index;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	long first, joiner, back;

	player_data = game_state_data_new("players", HALO_PORT_MAXIMUM_NETWORK_PLAYERS, sizeof(struct player_datum));
	now_ticks = 0;
	first = add_player(L"alpha", 1);
	add_player(L"bravo", 2);
	now_ticks = 60 * TICKS_PER_SECOND;
	CASE("held")
	{
		joiner = add_player(L"charlie", 3);
		CHECK(late_join_holds(joiner), "a late joiner spawned at once");
		CHECK(!late_join_holds(first), "a game's own player was held");
		return 0;
	}
	CASE("hold-ends")
	{
		joiner = add_player(L"charlie", 3);
		now_ticks += 4 * 60 * TICKS_PER_SECOND;
		CHECK(late_join_holds(joiner), "the hold ended before 5 minutes");
		now_ticks += 61 * TICKS_PER_SECOND;
		CHECK(!late_join_holds(joiner), "the hold lasted past 5 minutes");
		CHECK(!late_join_holds(joiner), "a hold that ended came back");
		return 0;
	}
	CASE("rejoined")
	{
		/* (alpha drops: the slot is reused, and alpha comes back) */
		player_get(first)->quit_out_of_game = TRUE;
		datum_delete(player_data, first);
		back = add_player(L"alpha", 4);
		CHECK(!late_join_holds(back), "a player back after dropping was held");
		joiner = add_player(L"delta", 5);
		CHECK(late_join_holds(joiner), "a new player wasn't held");
		return 0;
	}
	CASE("told-once")
	{
		joiner = add_player(L"charlie", 3);
		late_join_holds(joiner);
		late_join_holds(joiner);
		late_join_holds(joiner);
		CHECK(told == 1, "an OpenCE player held was told %d times", told);
		return 0;
	}
	CASE("next-game")
	{
		/* (a new game: the names start over, its players play) */
		now_ticks = 0;
		joiner = add_player(L"charlie", 3);
		CHECK(!late_join_holds(joiner), "a next game's player was held");
		now_ticks = 60 * TICKS_PER_SECOND;
		back = add_player(L"echo", 6);
		CHECK(late_join_holds(back), "a late joiner of the next game wasn't held");
		return 0;
	}
	CHECK(FALSE, "no such case: %s", case_name);
	return 0;
}
