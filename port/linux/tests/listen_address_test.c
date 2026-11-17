/* The address the game's listeners bind (port/linux/src/listen_address.h):
every interface for a game others may join, this machine alone for a split
screen game, network.address whenever it is set, and a socket that asked
for one address keeps it. Run by tools/test_listen_address.py. */
#include "../src/listen_address.h"
#include <stdio.h>

static int failures;

#define CHECK(condition) \
	do { \
		if (!(condition)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

int main(void)
{
	/* a System Link or Internet game's host, and a client's own socket:
	every interface, as asked (stock Halo's listeners) */
	CHECK(listen_address_choose(1, 0, 0) == _listen_address_asked);
	/* a split screen game: this machine alone, on every one of its sockets
	(the game's TCP and UDP, Delta Peer's, map sharing's) */
	CHECK(listen_address_choose(1, 0, 1) == _listen_address_loopback);
	/* network.address set: that address, whatever the game (the local
	client reaches the host at it, xnet.c's outgoing_address) */
	CHECK(listen_address_choose(1, 1, 0) == _listen_address_local_address);
	CHECK(listen_address_choose(1, 1, 1) == _listen_address_local_address);
	/* a socket that asked for one address keeps it (the telnet console's
	loopback, p2p's chosen address), split screen or not */
	CHECK(listen_address_choose(0, 0, 0) == _listen_address_asked);
	CHECK(listen_address_choose(0, 0, 1) == _listen_address_asked);
	CHECK(listen_address_choose(0, 1, 0) == _listen_address_asked);
	CHECK(listen_address_choose(0, 1, 1) == _listen_address_asked);
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("listen_address_test: ok\n");
	return 0;
}
