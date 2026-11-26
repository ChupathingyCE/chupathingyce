/*
SEARCH_ANSWER_TEST.C

Tests of a host's rule for a game search that came from a port other than
the game's client port (port/linux/game/network_rules.c,
network_rules_search_answered): a local network's and internet play's
searches answered as ever, whatever the setting and however many; an
internet address's only with network.direct_ip_host on, four at once and
four a second after, for all of them together; a multicast or broadcast
address's never. tools/test_search_answer.py builds and runs them.
*/

#include "../game/network_rules.h"

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

/* an address, host byte order */
#define ADDRESS(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

static const uint32_t local_addresses[] =
{
	ADDRESS(127, 0, 0, 1),
	ADDRESS(127, 0, 0, 3),
	ADDRESS(10, 1, 2, 3),
	ADDRESS(172, 20, 0, 5),
	ADDRESS(192, 168, 1, 20),
	ADDRESS(169, 254, 3, 4),
	/* (internet play's peers, Tailscale's) */
	ADDRESS(100, 64, 18, 77),
	ADDRESS(100, 127, 255, 254),
};

static const uint32_t internet_addresses[] =
{
	ADDRESS(203, 0, 113, 7),
	ADDRESS(198, 51, 100, 2),
	ADDRESS(100, 128, 0, 1),
	ADDRESS(8, 8, 8, 8),
	ADDRESS(223, 255, 255, 254),
};

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

/* a local network's searches: every one answered, the setting on or off,
and none of them taking from the internet's budget */
static void test_local_always(void)
{
	struct network_rules_search_budget budget;
	unsigned index;
	int round;

	memset(&budget, 0, sizeof(budget));
	for (round = 0; round < 100; round++)
	{
		for (index = 0; index < COUNT(local_addresses); index++)
		{
			CHECK(network_rules_search_answered(local_addresses[index], 0, 1000, &budget));
			CHECK(network_rules_search_answered(local_addresses[index], 1, 1000, &budget));
		}
	}
	CHECK(!budget.started);
}

/* the internet's with the setting off (the default): never */
static void test_internet_off(void)
{
	struct network_rules_search_budget budget;
	unsigned index;

	memset(&budget, 0, sizeof(budget));
	for (index = 0; index < COUNT(internet_addresses); index++)
		CHECK(!network_rules_search_answered(internet_addresses[index], 0, 1000, &budget));
	CHECK(!budget.started);
}

/* the internet's with it on: four at once, then four a second, for every
address together */
static void test_internet_on_budget(void)
{
	struct network_rules_search_budget budget;
	uint32_t now = 5000;
	int answered = 0;
	int index;

	memset(&budget, 0, sizeof(budget));
	for (index = 0; index < 40; index++)
		answered += network_rules_search_answered(internet_addresses[index % COUNT(internet_addresses)], 1, now,
			&budget);
	CHECK(answered == NETWORK_RULES_SEARCH_BURST);
	/* (a quarter of a second: one more) */
	CHECK(network_rules_search_answered(ADDRESS(203, 0, 113, 7), 1, now + 250, &budget));
	CHECK(!network_rules_search_answered(ADDRESS(198, 51, 100, 2), 1, now + 250, &budget));
	/* (a search every millisecond for ten seconds: four a second) */
	answered = 0;
	for (index = 1; index <= 10000; index++)
		answered += network_rules_search_answered(ADDRESS(203, 0, 113, 7), 1, now + 250 + (uint32_t)index, &budget);
	CHECK(answered == 10 * NETWORK_RULES_SEARCH_PER_SECOND);
	/* (a local network's still answered while the internet's are over it) */
	CHECK(network_rules_search_answered(ADDRESS(192, 168, 1, 20), 1, now + 10250, &budget));
	/* (quiet for a second: full again, no more than the burst) */
	answered = 0;
	for (index = 0; index < 20; index++)
		answered += network_rules_search_answered(ADDRESS(8, 8, 8, 8), 1, now + 20000, &budget);
	CHECK(answered == NETWORK_RULES_SEARCH_BURST);
}

/* multicast, the reserved range and broadcast: never, the setting on */
static void test_never_multicast(void)
{
	struct network_rules_search_budget budget;

	memset(&budget, 0, sizeof(budget));
	CHECK(!network_rules_search_answered(ADDRESS(224, 0, 0, 1), 1, 0, &budget));
	CHECK(!network_rules_search_answered(ADDRESS(239, 255, 255, 250), 1, 0, &budget));
	CHECK(!network_rules_search_answered(ADDRESS(240, 0, 0, 1), 1, 0, &budget));
	CHECK(!network_rules_search_answered(ADDRESS(255, 255, 255, 255), 1, 0, &budget));
	CHECK(!budget.started);
}

/* a clock that wraps, and one that goes back */
static void test_clock(void)
{
	struct network_rules_search_budget budget;
	int index;
	int answered = 0;

	memset(&budget, 0, sizeof(budget));
	for (index = 0; index < NETWORK_RULES_SEARCH_BURST; index++)
		CHECK(network_rules_search_answered(ADDRESS(203, 0, 113, 7), 1, 0xFFFFFF00u, &budget));
	CHECK(!network_rules_search_answered(ADDRESS(203, 0, 113, 7), 1, 0xFFFFFF00u, &budget));
	/* (past the wrap, 256 milliseconds on: one) */
	CHECK(network_rules_search_answered(ADDRESS(203, 0, 113, 7), 1, 0x00000000u, &budget));
	CHECK(!network_rules_search_answered(ADDRESS(203, 0, 113, 7), 1, 0x00000000u, &budget));
	/* (back in time: nothing added) */
	for (index = 0; index < 10; index++)
		answered += network_rules_search_answered(ADDRESS(203, 0, 113, 7), 1, 0xF0000000u, &budget);
	CHECK(answered == 0);
}

int main(void)
{
	test_local_always();
	test_internet_off();
	test_internet_on_budget();
	test_never_multicast();
	test_clock();
	if (failures)
	{
		printf("search_answer_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("search_answer_test: ok\n");
	return 0;
}
