/*
ADDRESS_LIMIT_TEST.C

Tests of a host's limit on the machines of one address (port/linux/game/
network_rules.c): which addresses are a local network's (never limited) and
which are counted, carrier NAT's among them, and when a joining machine
goes past the limit. tools/test_address_limit.py builds and runs them.
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

static void test_local(void)
{
	CHECK(network_rules_address_is_local(0));
	CHECK(network_rules_address_is_local(ADDRESS(127, 0, 0, 1)));
	CHECK(network_rules_address_is_local(ADDRESS(127, 255, 255, 254)));
	CHECK(network_rules_address_is_local(ADDRESS(10, 0, 0, 1)));
	CHECK(network_rules_address_is_local(ADDRESS(10, 255, 255, 255)));
	CHECK(network_rules_address_is_local(ADDRESS(172, 16, 0, 1)));
	CHECK(network_rules_address_is_local(ADDRESS(172, 31, 255, 255)));
	CHECK(network_rules_address_is_local(ADDRESS(192, 168, 1, 20)));
	CHECK(network_rules_address_is_local(ADDRESS(169, 254, 3, 4)));
	/* (next to them, and carrier NAT's: counted) */
	CHECK(!network_rules_address_is_local(ADDRESS(172, 15, 255, 255)));
	CHECK(!network_rules_address_is_local(ADDRESS(172, 32, 0, 0)));
	CHECK(!network_rules_address_is_local(ADDRESS(192, 167, 1, 1)));
	CHECK(!network_rules_address_is_local(ADDRESS(192, 169, 1, 1)));
	CHECK(!network_rules_address_is_local(ADDRESS(11, 0, 0, 1)));
	CHECK(!network_rules_address_is_local(ADDRESS(128, 0, 0, 1)));
	CHECK(!network_rules_address_is_local(ADDRESS(169, 253, 0, 1)));
	CHECK(!network_rules_address_is_local(ADDRESS(100, 64, 0, 1)));
	CHECK(!network_rules_address_is_local(ADDRESS(100, 127, 255, 254)));
	CHECK(!network_rules_address_is_local(ADDRESS(203, 0, 113, 7)));
}

static void test_full(void)
{
	const uint32_t public_address = ADDRESS(203, 0, 113, 7);
	const uint32_t other_address = ADDRESS(198, 51, 100, 2);
	const uint32_t lan = ADDRESS(192, 168, 1, 20);
	const uint32_t carrier = ADDRESS(100, 70, 1, 2);
	uint32_t others[16];
	int index;

	/* (no limit: never) */
	for (index = 0; index < 16; index++)
		others[index] = public_address;
	CHECK(!network_rules_address_full(public_address, others, 16, 0));
	CHECK(!network_rules_address_full(public_address, others, 16, -1));
	/* (the limit's machines in already: the next refused, others' not) */
	CHECK(network_rules_address_full(public_address, others, 2, 2));
	CHECK(!network_rules_address_full(public_address, others, 1, 2));
	CHECK(!network_rules_address_full(other_address, others, 16, 2));
	CHECK(network_rules_address_full(public_address, others, 1, 1));
	CHECK(!network_rules_address_full(public_address, others, 0, 1));
	/* (counted by the address alone, among others) */
	others[0] = other_address;
	others[1] = public_address;
	others[2] = lan;
	others[3] = public_address;
	others[4] = carrier;
	CHECK(network_rules_address_full(public_address, others, 5, 2));
	CHECK(!network_rules_address_full(public_address, others, 5, 3));
	CHECK(!network_rules_address_full(other_address, others, 5, 2));
	/* (carrier NAT's counted; a local network's never) */
	for (index = 0; index < 16; index++)
		others[index] = carrier;
	CHECK(network_rules_address_full(carrier, others, 8, 8));
	CHECK(!network_rules_address_full(carrier, others, 7, 8));
	for (index = 0; index < 16; index++)
		others[index] = lan;
	CHECK(!network_rules_address_full(lan, others, 16, 1));
	CHECK(!network_rules_address_full(0, others, 16, 1));
}

int main(void)
{
	test_local();
	test_full();
	if (failures)
	{
		printf("address_limit_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("address_limit_test: ok\n");
	return 0;
}
