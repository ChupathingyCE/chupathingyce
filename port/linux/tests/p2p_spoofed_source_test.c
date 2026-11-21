/*
P2P_SPOOFED_SOURCE_TEST.C

Checks which sources internet play tells the game's sockets to drop
(p2p_spoofed_source, port/linux/src/p2p.c; port/linux/src/xnet.c asks it
for every datagram and connection the game receives). p2p_tunnel_fuzz.c,
included whole, gives p2p.c its stand-in platform and sets up a machine
with one joined peer (tools/test_p2p_spoofed_source.py builds and runs it):

- the peer's own address, as a source of the game's socket, is dropped;
- so is the address of a peer that has left (it was a peer's);
- the local address the peer's stand-ins use, another address of the
  peers' range that is no peer's, a LAN address and 127.0.0.1 are not;
- nothing is dropped while internet play isn't running.

    p2p_spoofed_source_test

0 when every check held, 1 with why.
*/

#include "p2p_tunnel_fuzz.c"

#include <stdio.h>

static int failures;

static void check(int condition, const char *what)
{
	if (!condition)
	{
		printf("failed: %s\n", what);
		failures++;
	}
}

static unsigned long address_of(int a, int b, int c, int d)
{
	return network_long((unsigned long)a << 24 | (unsigned long)b << 16 | (unsigned long)c << 8 | (unsigned long)d);
}

int main(void)
{
	struct peer *peer = set_up(1);
	unsigned long peer_address = peer->virtual_address;
	/* (the peer's address with its last byte changed: in the range, no peer's) */
	unsigned long other_address = peer_address ^ network_long(0x01);

	check(peer->used && is_virtual_address(peer_address), "the peer has an address in the peers' range");
	check(p2p_spoofed_source(peer_address), "the peer's own address as a source is dropped");
	check(!p2p_spoofed_source(other_address), "another address of the range, no peer's, is not dropped");
	check(!p2p_spoofed_source(p2p.local_address), "the stand-ins' local address is not dropped");
	check(!p2p_spoofed_source(address_of(127, 0, 0, 1)), "127.0.0.1 is not dropped");
	check(!p2p_spoofed_source(address_of(192, 168, 1, 20)), "a LAN address is not dropped");

	drop_peer(peer, "left");
	check(!peer->used, "the peer left");
	check(p2p_spoofed_source(peer_address), "the address of a peer that left is still dropped");

	p2p.running = 0;
	check(!p2p_spoofed_source(peer_address), "nothing is dropped while internet play isn't running");

	if (failures)
		return 1;
	printf("ok\n");
	return 0;
}
