/*
P2P_COPIES_TEST.C

Tests of one public game listed under every network version, shown once
(port/linux/src/p2p_copies.c). Built and run by tools/test_p2p_copies.py;
exits nonzero on a failure.
*/

#include "../src/p2p.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

enum
{
	MAXIMUM = 16,
};

static struct p2p_listing games[MAXIMUM];
static unsigned long heard_times[MAXIMUM];
static int count;

static void reset(void)
{
	memset(games, 0, sizeof(games));
	memset(heard_times, 0, sizeof(heard_times));
	count = 0;
}

/* a listing of a game: its name and version, heard when; on Blood Gulch,
Slayer, 3 of 16 */
static struct p2p_listing *add(const char *name, unsigned short version, unsigned long heard_time)
{
	struct p2p_listing *game = &games[count];

	snprintf(game->invite, sizeof(game->invite), "halo://join/%02d", count);
	snprintf(game->name, sizeof(game->name), "%s", name);
	snprintf(game->map, sizeof(game->map), "bloodgulch");
	snprintf(game->gametype, sizeof(game->gametype), "Slayer");
	game->engine_type = 2;
	game->player_count = 3;
	game->maximum_player_count = 16;
	game->version = version;
	heard_times[count] = heard_time;
	count++;
	return game;
}

/* the games kept, as p2p_lobby_games keeps them: how many, and whether
the one at index is among them */
static int kept(void)
{
	int index, total = 0;

	for (index = 0; index < count; index++)
		total += !p2p_listing_older_copy(games, heard_times, count, index);
	return total;
}

static int shown(int index)
{
	return !p2p_listing_older_copy(games, heard_times, count, index);
}

static void test_one_game_under_every_version_shows_once(void)
{
	unsigned short version;

	reset();
	add(". SWAT pistols", 11, 100);
	for (version = 21; version <= 29; version++)
		add(". SWAT pistols", version, 100 + version);
	CHECK(kept() == 1);
	CHECK(shown(count - 1));
	CHECK(games[count - 1].version == 29);
}

static void test_the_newest_version_wins_whatever_the_order(void)
{
	reset();
	add("Game", 27, 300);
	add("Game", 29, 100);
	add("Game", 28, 200);
	CHECK(kept() == 1);
	CHECK(shown(1));
}

static void test_of_the_newest_copies_the_last_heard(void)
{
	reset();
	add("Game", 28, 100);
	add("Game", 29, 200);
	add("Game", 29, 300);
	CHECK(kept() == 1);
	CHECK(shown(2));
}

static void test_copies_heard_at_once_keep_the_first(void)
{
	reset();
	add("Game", 28, 100);
	add("Game", 29, 200);
	add("Game", 29, 200);
	CHECK(kept() == 1);
	CHECK(shown(1));
}

static void test_the_last_heard_across_the_clock_wrapping(void)
{
	reset();
	add("Game", 28, 5);
	add("Game", 29, (unsigned long)-10);
	add("Game", 29, 20);
	CHECK(kept() == 1);
	CHECK(shown(2));
}

static void test_two_hosts_games_of_one_version_stay_apart(void)
{
	reset();
	add("Game", 29, 100);
	add("Game", 29, 200);
	CHECK(kept() == 2);
}

static void test_names_alike_but_for_case_and_spaces(void)
{
	reset();
	add("  . swat PISTOLS ", 21, 100);
	add(". SWAT pistols", 29, 100);
	games[0].map[0] = 'B';
	snprintf(games[0].gametype, sizeof(games[0].gametype), "SLAYER");
	CHECK(kept() == 1);
	CHECK(shown(1));
}

static void test_games_that_differ_are_kept(void)
{
	struct p2p_listing *other;

	reset();
	add("Game", 29, 100);
	add("Game 2", 28, 100);
	other = add("Game", 28, 100);
	snprintf(other->map, sizeof(other->map), "hangemhigh");
	other = add("Game", 28, 100);
	snprintf(other->gametype, sizeof(other->gametype), "CTF");
	other = add("Game", 28, 100);
	other->engine_type = 1;
	other = add("Game", 28, 100);
	other->player_count = 4;
	other = add("Game", 28, 100);
	other->maximum_player_count = 8;
	other = add("Game", 28, 100);
	other->in_progress = 1;
	CHECK(kept() == count);
}

static void test_a_name_with_inner_spaces_differs(void)
{
	reset();
	add("Game  one", 29, 100);
	add("Game one", 28, 100);
	CHECK(kept() == 2);
}

static int hidden(int index)
{
	return p2p_listing_hidden_copy(games, heard_times, count, index, 20, 30);
}

static void test_a_version_outside_the_window_is_kept_marked_not_hidden(void)
{
	reset();
	add("Lobby", 36, 100);
	add("Other", 32, 100);
	CHECK(!hidden(0));
	CHECK(!hidden(1));
}

static void test_a_joinable_copy_stands_for_a_newer_one_outside_the_window(void)
{
	reset();
	add("Game", 36, 100);
	add("Game", 29, 100);
	CHECK(hidden(0));
	CHECK(!hidden(1));
}

static void test_copies_outside_the_window_keep_one(void)
{
	reset();
	add("Game", 35, 100);
	add("Game", 36, 100);
	CHECK(hidden(0));
	CHECK(!hidden(1));
}

int main(void)
{
	test_a_version_outside_the_window_is_kept_marked_not_hidden();
	test_a_joinable_copy_stands_for_a_newer_one_outside_the_window();
	test_copies_outside_the_window_keep_one();
	test_one_game_under_every_version_shows_once();
	test_the_newest_version_wins_whatever_the_order();
	test_of_the_newest_copies_the_last_heard();
	test_copies_heard_at_once_keep_the_first();
	test_the_last_heard_across_the_clock_wrapping();
	test_two_hosts_games_of_one_version_stay_apart();
	test_names_alike_but_for_case_and_spaces();
	test_games_that_differ_are_kept();
	test_a_name_with_inner_spaces_differs();
	printf("%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
