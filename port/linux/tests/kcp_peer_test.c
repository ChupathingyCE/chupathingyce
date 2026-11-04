/*
KCP_PEER_TEST.C

What a peer's segments can do to the tunnel's KCP (port/third_party/kcp/ikcp.c;
tools/test_kcp_peer.py builds and runs it, under the sanitizers where there
are any): an ACK that echoes a time the peer picked, so that the round trip
it makes is any 31-bit number, must not overflow the round trip arithmetic.
*/

#include "../../third_party/kcp/ikcp.h"

#include <stdio.h>
#include <string.h>

/* (ikcp.c's own) */
enum
{
	IKCP_OVERHEAD = 24,
	IKCP_CMD_ACK = 82,
	IKCP_RTO_MAX = 60000,
};

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static int output(const char *buffer, int size, ikcpcb *kcp, void *user)
{
	(void)buffer;
	(void)kcp;
	(void)user;
	return size;
}

static void put32(unsigned char *bytes, unsigned int value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

/* an ACK segment (IKCP_OVERHEAD bytes) of conversation 7 that echoes ts */
static void ack(unsigned char *segment, unsigned int ts)
{
	memset(segment, 0, IKCP_OVERHEAD);
	put32(segment, 7);
	segment[4] = IKCP_CMD_ACK;
	segment[6] = 0;
	segment[7] = 1;
	put32(segment + 8, ts);
}

int main(void)
{
	unsigned char segment[IKCP_OVERHEAD];
	static const unsigned int rounds[] = { 0, 1, 250, 59999, 60000, 60001, 0x7FFFFFF0u, 0x7FFFFFFFu };
	ikcpcb *kcp = ikcp_create(7, NULL);
	unsigned int index;

	CHECK(kcp);
	ikcp_setoutput(kcp, output);
	ikcp_update(kcp, 0x90000000u);
	for (index = 0; index < sizeof(rounds) / sizeof(rounds[0]); index++)
	{
		ack(segment, 0x90000000u - rounds[index]);
		CHECK(ikcp_input(kcp, (const char *)segment, (long)sizeof(segment)) == 0);
		/* (the retransmission timeout stays within its bounds, whatever was echoed) */
		CHECK(kcp->rx_rto >= (IINT32)kcp->rx_minrto && kcp->rx_rto <= IKCP_RTO_MAX);
		CHECK(kcp->rx_srtt >= 0 && kcp->rx_rttval >= 0);
	}
	ikcp_release(kcp);
	if (failures)
		return 1;
	printf("kcp_peer_test: ok\n");
	return 0;
}
