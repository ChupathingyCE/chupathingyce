/*
P2P_PROBE_FUZZ.C

libFuzzer's entry for the pings before joining (port/linux/src/p2p_probe.c;
tools/test_host_fuzz.py builds it): the input is a run of operations, each a
byte that picks one, a length and that many bytes, with the clock moving
between them. A probe, a probe's answer and a datagram with the probe magic,
as a host takes them (its table of probes) and as a prober does (its table
of targets), all in allocations of exactly their size, so that a read past
them is caught.
*/

#include "../src/p2p_probe.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *own_copy(const uint8_t *data, size_t size)
{
	unsigned char *copy = malloc(size ? size : 1);

	if (copy && size)
		memcpy(copy, data, size);
	return copy;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static struct p2p_probe_host host;
	static struct p2p_probe_prober prober;
	unsigned long now = 100000;
	size_t at = 0;

	memset(&host, 0, sizeof(host));
	memset(&prober, 0, sizeof(prober));
	while (at + 3 <= size)
	{
		unsigned op = data[at] % 9, length = data[at + 1];
		unsigned char id[P2P_PROBE_ID_SIZE], reply[P2P_PROBE_ID_SIZE], key[P2P_PROBE_KEY_SIZE];
		unsigned char text[P2P_PROBE_MAXIMUM_MESSAGE + 32], answer[P2P_PROBE_PACKET_SIZE];
		unsigned char packets[P2P_PROBE_IN_FLIGHT][P2P_PROBE_PACKET_SIZE], random[16];
		struct p2p_probe_address addresses[P2P_PROBE_MAXIMUM_ADDRESSES], to[P2P_PROBE_IN_FLIGHT];
		struct p2p_probe_target *target;
		unsigned short sequence;
		unsigned long clock;
		unsigned char *bytes;
		int count, type, signed_size, action, packet_count, rounds;

		now += (unsigned long)data[at + 2] * 40;
		at += 3;
		if (length > size - at)
			length = (unsigned)(size - at);
		bytes = own_copy(data + at, length);
		at += length;
		if (!bytes)
			return 0;
		switch (op)
		{
		case 0:
			if (p2p_probe_get_request(bytes, (int)length, id, reply, addresses, &count))
			{
				p2p_probe_host_request(&host, now, id, addresses, count);
				p2p_probe_put_request(text, id, reply, addresses, count);
			}
			break;
		case 1:
			if (p2p_probe_get_answer(bytes, (int)length, id, key, addresses, &count, &signed_size))
			{
				p2p_probe_signed_text(bytes, signed_size, text);
				target = p2p_probe_target(&prober, now, key, key + 16, 1);
				if (target)
					memcpy(target->id, id, sizeof(id));
				p2p_probe_prober_answer(&prober, now, id, addresses, count);
			}
			break;
		case 2:
			p2p_probe_host_packet(&host, now, bytes, (int)length, answer);
			break;
		case 3:
			p2p_probe_prober_packet(&prober, now, bytes, (int)length, 0x0100007F, 0x1234);
			break;
		case 4:
			if (p2p_probe_get_packet(bytes, (int)length, &type, &sequence, id, &clock))
				p2p_probe_put_packet(answer, type, sequence, id, clock);
			break;
		case 5:
			p2p_probe_host_due(&host, now, packets, to, P2P_PROBE_IN_FLIGHT);
			break;
		case 6:
			memset(random, length, sizeof(random));
			for (rounds = 0; rounds < 8 &&
				p2p_probe_prober_next(&prober, now, random, &action, packets, to, P2P_PROBE_IN_FLIGHT, &packet_count);
				rounds++)
				;
			break;
		case 7:
			memset(key, 0, sizeof(key));
			memcpy(key, bytes, length < 32 ? length : 32);
			target = p2p_probe_target(&prober, now, key, key + 16, 1);
			if (target)
				memcpy(target->id, key, sizeof(id));
			break;
		default:
			if (length >= 8)
			{
				/* (an answer with every field, as the host's own makes it) */
				memset(addresses, 0, sizeof(addresses));
				memcpy(id, bytes, 8);
				memset(key, 7, sizeof(key));
				p2p_probe_put_answer(text, id, key, addresses, bytes[0] % 6 - 1);
			}
			break;
		}
		free(bytes);
	}
	return 0;
}
