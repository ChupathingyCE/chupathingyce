#include "harness.h"

/* Network co-op's campaign progress (port/linux/game/coop_progress.c): the
host records the level it won; a joiner records its level only when its
round ends and the host's lobby comes back on the campaign's next level. */

#include "config.inc"

enum { MAXIMUM_NUMBER_OF_LOCAL_PLAYERS = 4 };

/* (the fake world: four levels' names, the profiles that exist, the records made) */
static char const *const levels[NUMBER_OF_SINGLE_PLAYER_LEVELS] =
	{ "levels\\a10\\a10", "levels\\a30\\a30", "levels\\a50\\a50", "levels\\b30\\b30", "levels\\b40\\b40",
	  "levels\\c10\\c10", "levels\\c20\\c20", "levels\\c40\\c40", "levels\\d20\\d20", "levels\\d40\\d40" };
static long profiles[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
static int records, recorded_level, recorded_difficulty;
static int client_token;
static void *current_client;

static short main_get_solo_level_from_name(char const *name)
{
	short index;
	for (index = 0; index < NUMBER_OF_SINGLE_PLAYER_LEVELS; index++)
		if (!strcmp(name, levels[index])) return index;
	return NONE;
}
static void *global_network_game_client_get(void) { return current_client; }
static long player_ui_get_active_player_profile_index(short local_player_index) { return profiles[local_player_index]; }
static void player_profile_save_level_completed_at(short local_player_index, short level, short difficulty)
{
	(void)local_player_index;
	records++;
	recorded_level = level;
	recorded_difficulty = difficulty;
}
static void platform_log(char const *format, ...) { (void)format; }

#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	profiles[0] = 1; profiles[1] = NONE; profiles[2] = 7; profiles[3] = NONE;
	current_client = &client_token;
	CASE("host")
	{
		coop_progress_host_won(3, 2);
		CHECK(records == 2 && recorded_level == 3 && recorded_difficulty == 2, "the host records b30 on Legendary for its two profiles");
		coop_progress_host_won(NONE, 2);
		coop_progress_host_won(3, NUMBER_OF_GAME_DIFFICULTY_LEVELS);
		CHECK(records == 2, "no level or no difficulty records nothing");
		return 0;
	}
	CASE("joiner-won")
	{
		coop_progress_client_level_started(levels[0], 1);
		coop_progress_client_round_ended();
		coop_progress_client_lobby_map(levels[1]);
		CHECK(records == 2 && recorded_level == 0 && recorded_difficulty == 1, "the lobby on a30 after a10: a10 recorded");
		coop_progress_client_lobby_map(levels[1]);
		CHECK(records == 2, "only once");
		return 0;
	}
	CASE("joiner-maw")
	{
		coop_progress_client_level_started(levels[9], 3);
		coop_progress_client_round_ended();
		coop_progress_client_lobby_map(levels[0]);
		CHECK(records == 2 && recorded_level == 9, "after The Maw, The Pillar of Autumn: d40 recorded");
		return 0;
	}
	CASE("joiner-restart")
	{
		coop_progress_client_level_started(levels[2], 1);
		coop_progress_client_round_ended();
		coop_progress_client_lobby_map(levels[2]);
		coop_progress_client_lobby_map(levels[3]);
		CHECK(records == 0, "the same level's lobby, then a change to the next: nothing");
		return 0;
	}
	CASE("joiner-other")
	{
		coop_progress_client_level_started(levels[2], 1);
		coop_progress_client_round_ended();
		coop_progress_client_lobby_map("levels\\test\\bloodgulch\\bloodgulch");
		CHECK(records == 0, "another map's lobby: nothing");
		coop_progress_client_lobby_map(levels[3]);
		coop_progress_client_round_ended();
		coop_progress_client_lobby_map(levels[3]);
		CHECK(records == 0, "a round end with no level begun: nothing");
		return 0;
	}
	CASE("joiner-no-round-end")
	{
		coop_progress_client_level_started(levels[4], 1);
		coop_progress_client_lobby_map(levels[5]);
		CHECK(records == 0, "a settings update while the level plays: nothing");
		return 0;
	}
	CASE("joiner-other-game")
	{
		static int other_token;
		coop_progress_client_level_started(levels[4], 1);
		coop_progress_client_round_ended();
		current_client = &other_token;
		coop_progress_client_lobby_map(levels[5]);
		CHECK(records == 0, "another game's lobby after the round ended: nothing");
		return 0;
	}
	CASE("joiner-left")
	{
		static int other_token;
		coop_progress_client_level_started(levels[4], 1);
		current_client = &other_token;
		coop_progress_client_round_ended();
		coop_progress_client_lobby_map(levels[5]);
		CHECK(records == 0, "another game's round end and lobby: nothing");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
