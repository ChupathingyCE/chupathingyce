/*
P2P_PUBLIC_ADDRESS_TEST.C

Tests of reading network.public_address (port/linux/src/p2p_public_address.h):
a host alone, a host and port, spaces around it, and the texts it refuses.
Built and run by tools/test_p2p_public_address.py; exits nonzero on a
failure.
*/

#include "../src/p2p_public_address.h"

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

int main(void)
{
	char host[128];
	unsigned short port;

	CHECK(!p2p_parse_public_address("", host, sizeof(host), &port));
	CHECK(!p2p_parse_public_address("   ", host, sizeof(host), &port));
	CHECK(!p2p_parse_public_address(NULL, host, sizeof(host), &port));

	CHECK(p2p_parse_public_address("relay.example.com", host, sizeof(host), &port));
	CHECK(!strcmp(host, "relay.example.com") && port == 0);

	CHECK(p2p_parse_public_address(" relay.example.com:7777 ", host, sizeof(host), &port));
	CHECK(!strcmp(host, "relay.example.com") && port == 7777);

	CHECK(p2p_parse_public_address("203.0.113.7:65535", host, sizeof(host), &port));
	CHECK(!strcmp(host, "203.0.113.7") && port == 65535);

	CHECK(!p2p_parse_public_address("relay.example.com:", host, sizeof(host), &port));
	CHECK(!p2p_parse_public_address("relay.example.com:0", host, sizeof(host), &port));
	CHECK(!p2p_parse_public_address("relay.example.com:65536", host, sizeof(host), &port));
	CHECK(!p2p_parse_public_address("relay.example.com:77a", host, sizeof(host), &port));
	CHECK(!p2p_parse_public_address("relay.example.com:7777:1", host, sizeof(host), &port));
	CHECK(!p2p_parse_public_address(":7777", host, sizeof(host), &port));
	CHECK(!p2p_parse_public_address("relay.example.com", host, 8, &port));
	CHECK(host[0] == 0 && port == 0);

	if (failures)
		return 1;
	printf("p2p_public_address: all passed\n");
	return 0;
}
