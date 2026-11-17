/*
PARTY_TEAMS_TEST.C

Tests of the host's party teams plan (port/linux/src/party_teams_plan.c).
Built and run by tools/test_party_teams.py; exits nonzero on a failure.
*/

#include "../src/party_teams_plan.h"

#include <stdio.h>

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

/* the plan's changes made until none is left: how many */
static int settle(struct party_teams_player *players, int count)
{
	struct party_teams_change change;
	int made = 0;

	while (made < 64 && party_teams_plan(players, count, &change))
	{
		/* (only a party member is ever moved) */
		CHECK(players[change.player].party >= 0);
		players[change.player].team = change.team;
		made++;
	}
	return made;
}

static int teams_apart(const struct party_teams_player *players, int count)
{
	int counts[2] = { 0, 0 }, index;

	for (index = 0; index < count; index++)
	{
		if (players[index].team == 0 || players[index].team == 1)
			counts[players[index].team]++;
	}
	return counts[0] > counts[1] ? counts[0] - counts[1] : counts[1] - counts[0];
}

static void test_never_a_trade(void)
{
	/* (a party of two split, the teams 2 and 2: a move would make them 3 and
	1, and a player in no party is never traded for it: left as is) */
	struct party_teams_player players[] =
	{
		{ 0, 7, 1 }, { 1, 7, 1 }, { 0, -1, 1 }, { 1, -1, 1 },
	};
	struct party_teams_change change;

	CHECK(party_teams_plan(players, 4, &change) == 0);
	CHECK(settle(players, 4) == 0);
	CHECK(players[2].team == 0 && players[3].team == 1);
}

static void test_move_into_the_smaller_team(void)
{
	/* (red 1, blue 2: the party's blue member moves to red, 2 and 1) */
	struct party_teams_player players[] =
	{
		{ 0, 3, 1 }, { 1, 3, 1 }, { 1, -1, 1 },
	};
	struct party_teams_change change;

	CHECK(party_teams_plan(players, 3, &change) == 1);
	CHECK(change.player == 1 && change.team == 0);
}

static void test_majority_wins(void)
{
	/* (two of the party blue, one red, red 3 and blue 2: the red one goes
	blue, 2 and 3) */
	struct party_teams_player players[] =
	{
		{ 1, 1, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, -1, 1 }, { 0, -1, 1 },
	};

	CHECK(settle(players, 5) == 1);
	CHECK(players[2].team == 1 && players[3].team == 0 && players[4].team == 0 && teams_apart(players, 5) == 1);
}

static void test_never_unbalanced_or_unmovable(void)
{
	/* (a move would make 3 and 1: left as is) */
	struct party_teams_player players[] =
	{
		{ 0, 2, 1 }, { 1, 2, 1 }, { 0, 5, 1 }, { 1, 5, 0 },
	};
	struct party_teams_change change;

	CHECK(party_teams_plan(players, 4, &change) == 0);
	/* (a player who can't be moved stays) */
	players[2].party = -1;
	players[2].movable = 0;
	CHECK(party_teams_plan(players, 4, &change) == 0);
}

static void test_two_parties(void)
{
	/* (two parties of two, crossed, red 3 and blue 2: each ends up
	together by moves alone, the teams within one) */
	struct party_teams_player players[] =
	{
		{ 0, 1, 1 }, { 1, 1, 1 }, { 0, 2, 1 }, { 1, 2, 1 }, { 0, -1, 1 },
	};

	CHECK(settle(players, 5) == 2);
	CHECK(players[0].team == players[1].team && players[2].team == players[3].team);
	CHECK(players[4].team == 0 && teams_apart(players, 5) <= 1);
}

static void test_players_in_no_party_never_move(void)
{
	/* (every arrangement of 6 players, 3 of them in one party: whatever the
	plan does, only the party's members move) */
	struct party_teams_player players[6];
	int arrangement, index;

	for (arrangement = 0; arrangement < 64; arrangement++)
	{
		for (index = 0; index < 6; index++)
		{
			players[index].team = (arrangement >> index) & 1;
			players[index].party = index < 3 ? 4 : -1;
			players[index].movable = 1;
		}
		settle(players, 6);
		for (index = 3; index < 6; index++)
			CHECK(players[index].team == ((arrangement >> index) & 1));
	}
}

static void test_off_teams_left_out(void)
{
	struct party_teams_player players[] =
	{
		{ -1, 1, 1 }, { 1, 1, 1 }, { 0, -1, 1 },
	};
	struct party_teams_change change;

	CHECK(party_teams_plan(players, 3, &change) == 0);
	CHECK(party_teams_plan(players, 0, &change) == 0);
}

int main(void)
{
	test_never_a_trade();
	test_move_into_the_smaller_team();
	test_majority_wins();
	test_never_unbalanced_or_unmovable();
	test_two_parties();
	test_players_in_no_party_never_move();
	test_off_teams_left_out();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}
