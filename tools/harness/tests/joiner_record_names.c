#include "harness.h"

/* The names in a game's settings record as a joiner takes them from its host
(network_game_client_clean_record_names): the game's, every machine's and
every player's kept to what draws as one line of text by the game's own
player_name_clean; a name already clean stays as it is. */

#include "config.inc"
#include <wchar.h>

/* (network_game_manager.h's and players.h's, the names and the counts only) */
struct network_player { wchar_t name[12]; short primary_color_index; };
struct network_machine { wchar_t name[32]; char machine_index; };
struct network_game
{
	wchar_t name[16];
	struct network_machine machines[MACHINES];
	struct network_player players[PLAYERS];
};
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))

#include "under_test.inc"

static void set(wchar_t *name, long count, wchar_t const *text)
{
	long index;
	for (index = 0; index < count; index++) name[index] = 0;
	for (index = 0; index < count && text[index]; index++) name[index] = text[index];
}
static int same(wchar_t const *name, wchar_t const *text)
{
	return wcscmp(name, text) == 0;
}
/* (no control character, '|' or space at either end left in a name, and it ends in its field) */
static int draws(wchar_t const *name, long count)
{
	long index, length = 0;
	while (length < count && name[length]) length++;
	if (length == count) return 0;
	for (index = 0; index < length; index++)
		if (name[index] < 0x20 || name[index] == '|' || name[index] == 0x200B) return 0;
	return !length || (name[0] != ' ' && name[length - 1] != ' ');
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	static struct network_game game;
	long index;

	memset(&game, 0, sizeof(game));
	CASE("dirty")
	{
		set(game.name, NUMBEROF(game.name), L"|nHost\x01 game");
		set(game.machines[0].name, NUMBEROF(game.machines[0].name), L"  box\x200B\x7F|c");
		set(game.machines[MACHINES - 1].name, NUMBEROF(game.machines[0].name), L"last\x1B[0m");
		set(game.players[0].name, NUMBEROF(game.players[0].name), L"|tChief\r\n");
		set(game.players[PLAYERS - 1].name, NUMBEROF(game.players[0].name), L"\x7F\x9F" L"Arbiter ");
		network_game_client_clean_record_names(&game);
		CHECK(same(game.name, L"nHost game"), "the game's name: %ls", game.name);
		CHECK(same(game.machines[0].name, L"boxc"), "a machine's name: %ls", game.machines[0].name);
		CHECK(same(game.machines[MACHINES - 1].name, L"last[0m"), "the last machine's: %ls", game.machines[MACHINES - 1].name);
		CHECK(same(game.players[0].name, L"tChief"), "a player's name: %ls", game.players[0].name);
		CHECK(same(game.players[PLAYERS - 1].name, L"Arbiter"), "the last player's: %ls", game.players[PLAYERS - 1].name);
		return 0;
	}
	CASE("unterminated")
	{
		for (index = 0; index < NUMBEROF(game.name); index++) game.name[index] = 'G';
		for (index = 0; index < NUMBEROF(game.players[3].name); index++) game.players[3].name[index] = 'P';
		network_game_client_clean_record_names(&game);
		CHECK(draws(game.name, NUMBEROF(game.name)), "the game's name ends in its field");
		CHECK(draws(game.players[3].name, NUMBEROF(game.players[3].name)), "a player's name ends in its field");
		return 0;
	}
	CASE("every-name")
	{
		for (index = 0; index < MACHINES; index++)
			set(game.machines[index].name, NUMBEROF(game.machines[index].name), L"\x01m|a\x200B" L"chine ");
		for (index = 0; index < PLAYERS; index++)
			set(game.players[index].name, NUMBEROF(game.players[index].name), L" |pl\x02" L"ayer");
		network_game_client_clean_record_names(&game);
		for (index = 0; index < MACHINES; index++)
			CHECK(same(game.machines[index].name, L"machine"), "machine %ld: %ls", index, game.machines[index].name);
		for (index = 0; index < PLAYERS; index++)
			CHECK(same(game.players[index].name, L"player"), "player %ld: %ls", index, game.players[index].name);
		return 0;
	}
	CASE("clean-stays")
	{
		static struct network_game before;
		set(game.name, NUMBEROF(game.name), L"Halo game");
		set(game.machines[1].name, NUMBEROF(game.machines[1].name), L"Living room \x00E9t\x00E9");
		set(game.players[2].name, NUMBEROF(game.players[2].name), L"Chief 117");
		set(game.players[5].name, NUMBEROF(game.players[5].name), L"\x00C9lite \x00FC");
		memcpy(&before, &game, sizeof(game));
		network_game_client_clean_record_names(&game);
		CHECK(!memcmp(&before, &game, sizeof(game)), "names already clean are left as they are");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
