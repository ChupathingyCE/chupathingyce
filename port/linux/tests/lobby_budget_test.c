/*
LOBBY_BUDGET_TEST.C

Tests of a host's budget for a client machine's lobby messages (port/linux/
game/network_rules.c): a burst taken, the messages past it dropped, the
budget filled again over time and never past the burst, a clock that wraps
or goes back, and what a player in the menus sends never dropped.
tools/test_lobby_budget.py builds and runs them.
*/

#include "../game/network_rules.h"

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

/* how many of count messages, one every interval milliseconds from start,
the budget takes */
static int taken(struct network_rules_lobby_budget *budget, uint32_t start, uint32_t interval, int count)
{
	int index;
	int result = 0;

	for (index = 0; index < count; index++)
		result += network_rules_lobby_budget_take(budget, start + (uint32_t)index * interval);
	return result;
}

static void test_burst(void)
{
	struct network_rules_lobby_budget budget;
	/* (a rate's interval after 1000, rounded up) */
	const uint32_t later = 1000 + (1000 + NETWORK_RULES_LOBBY_PER_SECOND - 1) / NETWORK_RULES_LOBBY_PER_SECOND;

	network_rules_lobby_budget_reset(&budget);
	/* (a burst at one instant: the burst, then nothing) */
	CHECK(taken(&budget, 1000, 0, 100) == NETWORK_RULES_LOBBY_BURST);
	CHECK(!network_rules_lobby_budget_take(&budget, 1000));
	/* (a rate's interval later: one) */
	CHECK(network_rules_lobby_budget_take(&budget, later));
	CHECK(!network_rules_lobby_budget_take(&budget, later));
}

static void test_refill(void)
{
	struct network_rules_lobby_budget budget;

	network_rules_lobby_budget_reset(&budget);
	CHECK(taken(&budget, 0, 0, NETWORK_RULES_LOBBY_BURST) == NETWORK_RULES_LOBBY_BURST);
	/* (a long pause fills it, no more than the burst) */
	CHECK(taken(&budget, 600000, 0, 100) == NETWORK_RULES_LOBBY_BURST);
	/* (a flood for ten seconds: the burst and the rate) */
	network_rules_lobby_budget_reset(&budget);
	CHECK(taken(&budget, 5000, 1, 10001) == NETWORK_RULES_LOBBY_BURST + 10 * NETWORK_RULES_LOBBY_PER_SECOND);
	/* (a reset is a full budget) */
	network_rules_lobby_budget_reset(&budget);
	CHECK(taken(&budget, 20000, 0, NETWORK_RULES_LOBBY_BURST) == NETWORK_RULES_LOBBY_BURST);
}

static void test_clock(void)
{
	struct network_rules_lobby_budget budget;

	/* (across the clock's wrap: as any other time) */
	network_rules_lobby_budget_reset(&budget);
	CHECK(taken(&budget, 0xFFFFFF00u, 0, 100) == NETWORK_RULES_LOBBY_BURST);
	CHECK(network_rules_lobby_budget_take(&budget, 0xFFFFFF00u + 1000));
	/* (a clock that goes back adds nothing) */
	network_rules_lobby_budget_reset(&budget);
	CHECK(taken(&budget, 50000, 0, 100) == NETWORK_RULES_LOBBY_BURST);
	CHECK(!network_rules_lobby_budget_take(&budget, 10000));
	CHECK(!network_rules_lobby_budget_take(&budget, 10001));
}

static void test_players(void)
{
	struct network_rules_lobby_budget budget;

	/* (a machine joining with four players: each added, its settings, each
	player's settings, all at once) */
	network_rules_lobby_budget_reset(&budget);
	CHECK(taken(&budget, 0, 0, 9) == 9);
	/* (then four players each holding a key on their color, which a menu
	repeats every 250 milliseconds, for a minute: 16 a second) */
	CHECK(taken(&budget, 100, 1000 / 16, 960) == 960);
	/* (a held key's own repeat, alone, for ten minutes) */
	network_rules_lobby_budget_reset(&budget);
	CHECK(taken(&budget, 0, 250, 2400) == 2400);
}

int main(void)
{
	test_burst();
	test_refill();
	test_clock();
	test_players();
	if (failures)
	{
		printf("lobby_budget_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("lobby_budget_test: ok\n");
	return 0;
}
