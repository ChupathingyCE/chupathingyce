/*
P2P_HOST_PORT_TEST.C

Tests of the port a host's tunnel takes (port/linux/src/p2p_host_port.h):
the default 2302 when it is free, the next free one when it is taken, a
random port when all are, and a port the player chose as it is. Built and
run by tools/test_p2p_host_port.py; exits nonzero on a failure.
*/

#include "../src/p2p_host_port.h"

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

/* the ports other programs hold, and the ones the test was asked to open */
struct machine
{
	unsigned short taken[8];
	int taken_count;
	unsigned short tried[8];
	int tried_count;
};

static int machine_open(unsigned short port, void *context)
{
	struct machine *machine = context;
	int index;

	machine->tried[machine->tried_count++] = port;
	for (index = 0; index < machine->taken_count; index++)
	{
		if (machine->taken[index] == port)
			return 0;
	}
	return 1;
}

/* the port a host gets, from config (configured, chosen) on machine: 0 for
the random one it started on */
static unsigned short host_port(long configured, int chosen, struct machine *machine)
{
	unsigned short ports[P2P_HOST_PORT_TRIES];
	int count = p2p_host_ports(configured, chosen, ports);
	int index = p2p_host_port_open(ports, count, machine_open, machine);

	return index < 0 ? 0 : ports[index];
}

static void test_default_free_takes_2302(void)
{
	struct machine machine = { { 0 }, 0, { 0 }, 0 };

	CHECK(host_port(2302, 0, &machine) == 2302);
	CHECK(machine.tried_count == 1);
}

static void test_default_taken_takes_2303(void)
{
	struct machine machine = { { 2302 }, 1, { 0 }, 0 };

	CHECK(host_port(2302, 0, &machine) == 2303);
	CHECK(machine.tried_count == 2 && machine.tried[0] == 2302 && machine.tried[1] == 2303);
}

static void test_default_two_taken_takes_2304(void)
{
	struct machine machine = { { 2302, 2303 }, 2, { 0 }, 0 };

	CHECK(host_port(2302, 0, &machine) == 2304);
}

static void test_default_all_taken_stays_random(void)
{
	struct machine machine = { { 2302, 2303, 2304, 2305, 2306 }, 5, { 0 }, 0 };

	CHECK(host_port(2302, 0, &machine) == 0);
	CHECK(machine.tried_count == P2P_HOST_PORT_TRIES);
	CHECK(machine.tried[P2P_HOST_PORT_TRIES - 1] == 2306);
}

static void test_chosen_port_alone(void)
{
	struct machine machine = { { 0 }, 0, { 0 }, 0 };
	struct machine taken = { { 2310 }, 1, { 0 }, 0 };

	/* (as it is, also 2302: nothing else is tried) */
	CHECK(host_port(2310, 1, &machine) == 2310);
	CHECK(host_port(2310, 1, &taken) == 0 && taken.tried_count == 1);
	memset(&taken, 0, sizeof(taken));
	taken.taken[taken.taken_count++] = 2302;
	CHECK(host_port(2302, 1, &taken) == 0 && taken.tried_count == 1);
}

static void test_zero_is_random(void)
{
	unsigned short ports[P2P_HOST_PORT_TRIES];

	CHECK(p2p_host_ports(0, 0, ports) == 0);
	CHECK(p2p_host_ports(0, 1, ports) == 0);
	CHECK(p2p_host_ports(-5, 1, ports) == 0);
	CHECK(p2p_host_ports(70000, 1, ports) == 0);
}

static void test_start_port(void)
{
	/* a joiner keeps a random port unless the player chose one */
	CHECK(p2p_start_port(2302, 0) == 0);
	CHECK(p2p_start_port(2302, 1) == 2302);
	CHECK(p2p_start_port(2310, 1) == 2310);
	CHECK(p2p_start_port(0, 1) == 0);
	CHECK(p2p_start_port(70000, 1) == 0);
}

static void test_kept_port(void)
{
	unsigned short ports[P2P_HOST_PORT_TRIES];
	int count = p2p_host_ports(2302, 0, ports);

	CHECK(count == P2P_HOST_PORT_TRIES);
	CHECK(p2p_host_port_kept(2304, ports, count));
	CHECK(!p2p_host_port_kept(51234, ports, count));
}

static void test_top_of_the_range(void)
{
	unsigned short ports[P2P_HOST_PORT_TRIES];

	CHECK(p2p_host_ports(65534, 0, ports) == 2 && ports[1] == 65535);
}

int main(void)
{
	test_default_free_takes_2302();
	test_default_taken_takes_2303();
	test_default_two_taken_takes_2304();
	test_default_all_taken_stays_random();
	test_chosen_port_alone();
	test_zero_is_random();
	test_start_port();
	test_kept_port();
	test_top_of_the_range();
	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
