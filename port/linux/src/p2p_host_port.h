/*
P2P_HOST_PORT.H

The UDP port of a host's tunnel (p2p.c), as network.tunnel_port gives it.
A port the player chose (in config.toml, or HALO_NET_TUNNEL_PORT) is the
tunnel's from the start, for joining as well as hosting, and nothing else is
tried. The default port (2302, Halo's own, on the desktop and Android) is a
host's only: every copy of the game starts on a random port, as a joiner
needs no fixed one, and moves to the default when it starts hosting, or to
the next free one of P2P_HOST_PORT_TRIES (another copy of the game, or a
dedicated server, on the same machine may hold it); with all of them taken,
the random port stays. 0 is a random port, also when hosting.

Header only, so that every build that has p2p.c has it (the consoles'
source lists too); tested by port/linux/tests/p2p_host_port_test.c.
*/

#ifndef P2P_HOST_PORT_H
#define P2P_HOST_PORT_H

/* the default port and the ones after it a host tries */
#define P2P_HOST_PORT_TRIES 5

/* the tunnel's port when the game starts: a chosen port, else 0 (random) */
static inline unsigned short p2p_start_port(long configured, int chosen)
{
	return chosen && configured > 0 && configured <= 65535 ? (unsigned short)configured : 0;
}

/* the ports a host's tunnel tries, in order, into ports (room for
P2P_HOST_PORT_TRIES): a chosen port alone, or the default and the ones
after it; none for 0 (random). Returns the count */
static inline int p2p_host_ports(long configured, int chosen, unsigned short *ports)
{
	int count = 0;

	if (configured <= 0 || configured > 65535)
		return 0;
	if (chosen)
	{
		ports[count++] = (unsigned short)configured;
		return count;
	}
	while (count < P2P_HOST_PORT_TRIES && configured + count <= 65535)
	{
		ports[count] = (unsigned short)(configured + count);
		count++;
	}
	return count;
}

/* whether the tunnel's port (bound) is one of the ports a host tries already */
static inline int p2p_host_port_kept(unsigned short bound, const unsigned short *ports, int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (ports[index] == bound)
			return 1;
	}
	return 0;
}

/* the first of ports that open(port, context) takes (nonzero), as its index;
-1 if none does */
static inline int p2p_host_port_open(const unsigned short *ports, int count,
	int (*open)(unsigned short port, void *context), void *context)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (open(ports[index], context))
			return index;
	}
	return -1;
}

#endif
