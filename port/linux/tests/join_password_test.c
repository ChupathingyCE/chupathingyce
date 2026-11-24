/*
JOIN_PASSWORD_TEST.C

Tests of a host's rule for a game with a password (port/linux/game/
network_rules.c): which addresses join as a LAN's (a local network's, and
100.64/10's: internet play's peers, who came through the locked invite, and
private networks such as Tailscale's), and which a game with a password
refuses (the internet's, straight to the game's port), for each kind of
join, with and without a password. tools/test_join_password.py builds and
runs them.
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

/* an address, host byte order */
#define ADDRESS(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

static void test_lan(void)
{
	/* (a local network's) */
	CHECK(network_rules_address_joins_as_lan(0));
	CHECK(network_rules_address_joins_as_lan(ADDRESS(127, 0, 0, 1)));
	CHECK(network_rules_address_joins_as_lan(ADDRESS(127, 0, 0, 3)));
	CHECK(network_rules_address_joins_as_lan(ADDRESS(10, 0, 0, 1)));
	CHECK(network_rules_address_joins_as_lan(ADDRESS(10, 147, 20, 9)));
	CHECK(network_rules_address_joins_as_lan(ADDRESS(172, 16, 0, 1)));
	CHECK(network_rules_address_joins_as_lan(ADDRESS(172, 31, 255, 254)));
	CHECK(network_rules_address_joins_as_lan(ADDRESS(192, 168, 1, 20)));
	CHECK(network_rules_address_joins_as_lan(ADDRESS(169, 254, 3, 4)));
	/* (100.64/10: internet play's peers, Tailscale's) */
	CHECK(network_rules_address_joins_as_lan(ADDRESS(100, 64, 0, 1)));
	CHECK(network_rules_address_joins_as_lan(ADDRESS(100, 101, 7, 33)));
	CHECK(network_rules_address_joins_as_lan(ADDRESS(100, 127, 255, 254)));
	/* (the internet's, among them those right next to the ranges above) */
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(100, 63, 255, 255)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(100, 128, 0, 0)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(172, 15, 255, 255)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(172, 32, 0, 0)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(192, 167, 1, 1)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(192, 169, 1, 1)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(11, 0, 0, 1)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(9, 255, 255, 255)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(128, 0, 0, 1)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(169, 253, 0, 1)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(203, 0, 113, 7)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(198, 51, 100, 2)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(8, 8, 8, 8)));
	CHECK(!network_rules_address_joins_as_lan(ADDRESS(255, 255, 255, 255)));
}

/* each kind of join, as the host sees its address */
static const struct
{
	const char *kind;
	uint32_t address;
	int from_internet;
} joins[] =
{
	{ "the host's own machine", ADDRESS(127, 0, 0, 1), 0 },
	{ "a copy on a loopback alias", ADDRESS(127, 0, 0, 3), 0 },
	{ "system link on a home network", ADDRESS(192, 168, 1, 20), 0 },
	{ "system link on an office network", ADDRESS(10, 1, 2, 3), 0 },
	{ "system link on a 172.16/12 network", ADDRESS(172, 20, 0, 5), 0 },
	{ "system link over link-local", ADDRESS(169, 254, 3, 4), 0 },
	{ "internet play (its peer's stand-in)", ADDRESS(100, 64, 18, 77), 0 },
	{ "a Tailscale network's", ADDRESS(100, 100, 1, 2), 0 },
	{ "system link from the internet", ADDRESS(203, 0, 113, 7), 1 },
	{ "system link from another internet address", ADDRESS(198, 51, 100, 2), 1 },
	{ "system link from just past 100.64/10", ADDRESS(100, 128, 0, 1), 1 },
};

static void test_refused(void)
{
	unsigned index;

	for (index = 0; index < sizeof(joins) / sizeof(joins[0]); index++)
	{
		int with_password = network_rules_join_password_refused(joins[index].address, 1);
		int without_password = network_rules_join_password_refused(joins[index].address, 0);

		/* (a game without a password: every join as ever) */
		if (without_password)
		{
			printf("FAIL: %s refused without a password\n", joins[index].kind);
			failures++;
		}
		/* (with one: only the internet's, straight to the game's port) */
		if (with_password != joins[index].from_internet)
		{
			printf("FAIL: %s %s with a password\n", joins[index].kind, with_password ? "refused" : "taken");
			failures++;
		}
	}
	/* (any nonzero is a password) */
	CHECK(network_rules_join_password_refused(ADDRESS(203, 0, 113, 7), 2));
	CHECK(!network_rules_join_password_refused(0, 1));
}

int main(void)
{
	test_lan();
	test_refused();
	if (failures)
	{
		printf("join_password_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("join_password_test: ok\n");
	return 0;
}
