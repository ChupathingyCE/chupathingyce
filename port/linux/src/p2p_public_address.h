/*
P2P_PUBLIC_ADDRESS.H

The public address a host advertises in place of the one the STUN servers
find (network.public_address): a host name or IPv4 address, with a port or
without (then the tunnel's own). It is for a host whose players come in
through a relay or a tunnel with its own address (frp, a VPS forwarding UDP,
a Docker bridge), where STUN finds the address of the connection out, which
no player can reach (#348).

Header only, so that every build that has p2p.c has it; tested by
port/linux/tests/p2p_public_address_test.c.
*/

#ifndef P2P_PUBLIC_ADDRESS_H
#define P2P_PUBLIC_ADDRESS_H

#include <stdlib.h>
#include <string.h>

/* the setting's text as host and port (0: the tunnel's); 1 if it names one,
0 if it is empty or not so made (the host too long, a second colon, a port
that is not 1 to 65535) */
static inline int p2p_parse_public_address(const char *text, char *host, int host_size, unsigned short *port)
{
	const char *end;
	const char *colon;
	int length;

	host[0] = 0;
	*port = 0;
	if (!text)
		return 0;
	while (*text == ' ' || *text == '\t')
		text++;
	end = text + strlen(text);
	while (end > text && (end[-1] == ' ' || end[-1] == '\t'))
		end--;
	colon = memchr(text, ':', (size_t)(end - text));
	length = (int)((colon ? colon : end) - text);
	if (length <= 0 || length >= host_size)
		return 0;
	if (colon)
	{
		const char *digit;
		long value = 0;

		if (colon + 1 == end || memchr(colon + 1, ':', (size_t)(end - colon - 1)))
			return 0;
		for (digit = colon + 1; digit < end; digit++)
		{
			if (*digit < '0' || *digit > '9')
				return 0;
			value = value * 10 + (*digit - '0');
			if (value > 65535)
				return 0;
		}
		if (!value)
			return 0;
		*port = (unsigned short)value;
	}
	memcpy(host, text, (size_t)length);
	host[length] = 0;
	return 1;
}

#endif
