/*
PING_TEST.C

Tests of Online Games' pings before joining (p2p_probe.c) and its
estimates (browser_ping.c): the formats against hostile input, the host's
limits (a probe is never more than a few same-sized answers), the prober's
measuring, the anchors and the Ping column's order. tools/test_ping.py
builds and runs them.
*/

#include "../src/p2p_probe.h"
#include "../src/browser_ping.h"

#include <stdio.h>
#include <stdlib.h>
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

static void fill(unsigned char *bytes, int size, int seed)
{
	int index;

	for (index = 0; index < size; index++)
		bytes[index] = (unsigned char)(seed * 31 + index * 7);
}

static struct p2p_probe_address address(unsigned long a, unsigned short port)
{
	struct p2p_probe_address result;

	result.address = a;
	result.port = port;
	return result;
}

static void test_packet(void)
{
	unsigned char packet[P2P_PROBE_PACKET_SIZE + 4], id[P2P_PROBE_ID_SIZE], read_id[P2P_PROBE_ID_SIZE];
	unsigned short sequence;
	unsigned long clock;
	int type;

	fill(id, sizeof(id), 1);
	CHECK(p2p_probe_put_packet(packet, P2P_PROBE_PING, 513, id, 0xDEADBEEFul) == P2P_PROBE_PACKET_SIZE);
	CHECK(p2p_probe_get_packet(packet, P2P_PROBE_PACKET_SIZE, &type, &sequence, read_id, &clock));
	CHECK(type == P2P_PROBE_PING && sequence == 513 && clock == 0xDEADBEEFul && !memcmp(id, read_id, sizeof(id)));
	/* exactly the size; the magic; a known type */
	CHECK(!p2p_probe_get_packet(packet, P2P_PROBE_PACKET_SIZE - 1, &type, &sequence, read_id, &clock));
	CHECK(!p2p_probe_get_packet(packet, P2P_PROBE_PACKET_SIZE + 1, &type, &sequence, read_id, &clock));
	packet[0] = 0x69;
	CHECK(!p2p_probe_get_packet(packet, P2P_PROBE_PACKET_SIZE, &type, &sequence, read_id, &clock));
	packet[0] = P2P_PROBE_MAGIC;
	packet[1] = 9;
	CHECK(!p2p_probe_get_packet(packet, P2P_PROBE_PACKET_SIZE, &type, &sequence, read_id, &clock));
}

static void test_messages(void)
{
	unsigned char message[P2P_PROBE_MAXIMUM_MESSAGE], id[P2P_PROBE_ID_SIZE], reply[P2P_PROBE_ID_SIZE];
	unsigned char read_id[P2P_PROBE_ID_SIZE], read_reply[P2P_PROBE_ID_SIZE], key[P2P_PROBE_KEY_SIZE];
	unsigned char read_key[P2P_PROBE_KEY_SIZE], text[P2P_PROBE_MAXIMUM_MESSAGE + 32];
	struct p2p_probe_address addresses[P2P_PROBE_MAXIMUM_ADDRESSES], read[P2P_PROBE_MAXIMUM_ADDRESSES];
	int size, count, signed_size, cut, seed;

	fill(id, sizeof(id), 2);
	fill(reply, sizeof(reply), 3);
	fill(key, sizeof(key), 4);
	addresses[0] = address(0x0100007F, 0x3412);
	addresses[1] = address(0x0A0B0C0D, 0x5000);
	size = p2p_probe_put_request(message, id, reply, addresses, 2);
	CHECK(size == 2 + 32 + 1 + 12);
	CHECK(p2p_probe_get_request(message, size, read_id, read_reply, read, &count));
	CHECK(count == 2 && read[1].address == 0x0A0B0C0D && read[1].port == 0x5000 && !memcmp(read_reply, reply, 16));
	/* (a later version may append: read for the fields known) */
	CHECK(p2p_probe_get_request(message, size + 5, read_id, read_reply, read, &count));
	for (cut = 0; cut < size; cut++)
		CHECK(!p2p_probe_get_request(message, cut, read_id, read_reply, read, &count));
	message[2 + 32] = P2P_PROBE_MAXIMUM_ADDRESSES + 1;
	CHECK(!p2p_probe_get_request(message, sizeof(message), read_id, read_reply, read, &count));
	message[0] = 'J';
	CHECK(!p2p_probe_get_request(message, size, read_id, read_reply, read, &count));
	/* (too many addresses are cut to the most) */
	{
		struct p2p_probe_address many[8];
		int index;

		for (index = 0; index < 8; index++)
			many[index] = address(0x01020304u + (unsigned long)index, 1);
		size = p2p_probe_put_request(message, id, reply, many, 8);
		CHECK(p2p_probe_get_request(message, size, read_id, read_reply, read, &count) &&
			count == P2P_PROBE_MAXIMUM_ADDRESSES);
	}

	size = p2p_probe_put_answer(message, id, key, addresses, 2);
	memset(message + size, 0xAB, P2P_PROBE_SIGNATURE_SIZE);
	CHECK(p2p_probe_get_answer(message, size + P2P_PROBE_SIGNATURE_SIZE, read_id, read_key, read, &count,
		&signed_size));
	CHECK(signed_size == size && count == 2 && !memcmp(read_key, key, sizeof(key)));
	CHECK(p2p_probe_signed_text(message, signed_size, text) == 17 + size && !memcmp(text, "chupathingy probe", 17));
	/* the signature right after the addresses, nothing more, nothing less */
	CHECK(!p2p_probe_get_answer(message, size + P2P_PROBE_SIGNATURE_SIZE - 1, read_id, read_key, read, &count,
		&signed_size));
	CHECK(!p2p_probe_get_answer(message, size + P2P_PROBE_SIGNATURE_SIZE + 1, read_id, read_key, read, &count,
		&signed_size));
	/* random bytes: never past the buffer, rarely an answer */
	for (seed = 0; seed < 20000; seed++)
	{
		unsigned char noise[P2P_PROBE_MAXIMUM_MESSAGE];
		int length = rand() % (int)sizeof(noise);
		int index;

		for (index = 0; index < length; index++)
			noise[index] = (unsigned char)rand();
		if (length)
			noise[0] = (unsigned char)(seed & 1 ? 'P' : 'Q');
		if (length > 1)
			noise[1] = 1;
		if (p2p_probe_get_request(noise, length, read_id, read_reply, read, &count))
			CHECK(count >= 0 && count <= P2P_PROBE_MAXIMUM_ADDRESSES);
		if (p2p_probe_get_answer(noise, length, read_id, read_key, read, &count, &signed_size))
			CHECK(count >= 0 && count <= P2P_PROBE_MAXIMUM_ADDRESSES && signed_size + 64 == length);
	}
}

static void test_host(void)
{
	static struct p2p_probe_host host;
	unsigned char id[P2P_PROBE_ID_SIZE], other[P2P_PROBE_ID_SIZE], ping[P2P_PROBE_PACKET_SIZE];
	unsigned char answer[P2P_PROBE_PACKET_SIZE], packets[16][P2P_PROBE_PACKET_SIZE];
	struct p2p_probe_address addresses[2], to[16];
	unsigned long now = 1000;
	int index, count, admitted;

	memset(&host, 0, sizeof(host));
	fill(id, sizeof(id), 5);
	fill(other, sizeof(other), 6);
	addresses[0] = address(0x0100007F, 0x1111);
	addresses[1] = address(0x0200007F, 0x2222);

	/* a PING without a probe: nothing back (a spoofer gets nothing) */
	p2p_probe_put_packet(ping, P2P_PROBE_PING, 0, id, 77);
	CHECK(!p2p_probe_host_packet(&host, now, ping, sizeof(ping), answer));

	CHECK(p2p_probe_host_request(&host, now, id, addresses, 2));
	/* the resend: answered again only after a second, and costs nothing */
	CHECK(!p2p_probe_host_request(&host, now + 500, id, addresses, 2));
	CHECK(p2p_probe_host_request(&host, now + 1000, id, addresses, 2));
	CHECK(host.admitted == 1);

	/* punches: two rounds to each address, then none */
	count = p2p_probe_host_due(&host, now, packets, to, 16);
	CHECK(count == 2 && to[1].port == 0x2222 && packets[0][1] == P2P_PROBE_PUNCH);
	CHECK(p2p_probe_host_due(&host, now + 100, packets, to, 16) == 0);
	CHECK(p2p_probe_host_due(&host, now + 250, packets, to, 16) == 2);
	CHECK(p2p_probe_host_due(&host, now + 600, packets, to, 16) == 0);

	/* PONGs: the same bytes but the type, the same size, 8 at most */
	for (index = 0; index < P2P_PROBE_HOST_PONGS; index++)
	{
		CHECK(p2p_probe_host_packet(&host, now + 700, ping, sizeof(ping), answer));
		CHECK(answer[1] == P2P_PROBE_PONG && !memcmp(answer + 2, ping + 2, P2P_PROBE_PACKET_SIZE - 2));
	}
	CHECK(!p2p_probe_host_packet(&host, now + 700, ping, sizeof(ping), answer));
	/* (a PONG or PUNCH sent to a host is not answered: no loops) */
	{
		unsigned char pong[P2P_PROBE_PACKET_SIZE];

		p2p_probe_put_packet(pong, P2P_PROBE_PONG, 0, other, 1);
		CHECK(!p2p_probe_host_packet(&host, now, pong, sizeof(pong), answer));
	}
	/* the probe goes after its lifetime */
	p2p_probe_put_packet(ping, P2P_PROBE_PING, 0, other, 1);
	CHECK(p2p_probe_host_request(&host, now + 800, other, addresses, 0));
	CHECK(p2p_probe_host_packet(&host, now + 900, ping, sizeof(ping), answer));
	CHECK(!p2p_probe_host_packet(&host, now + 800 + P2P_PROBE_HOST_LIFETIME, ping, sizeof(ping), answer));

	/* the budget: a burst, then one each interval */
	memset(&host, 0, sizeof(host));
	admitted = 0;
	for (index = 0; index < 100; index++)
	{
		unsigned char flood[P2P_PROBE_ID_SIZE];

		fill(flood, sizeof(flood), 100 + index);
		admitted += p2p_probe_host_request(&host, 5000, flood, addresses, 1);
	}
	CHECK(admitted == P2P_PROBE_HOST_BURST);
	{
		unsigned char later[P2P_PROBE_ID_SIZE];

		fill(later, sizeof(later), 999);
		CHECK(p2p_probe_host_request(&host, 5000 + P2P_PROBE_HOST_INTERVAL, later, addresses, 1));
	}
	/* a whole lifetime of floods never holds more than the table */
	memset(&host, 0, sizeof(host));
	admitted = 0;
	for (index = 0; index < 2000; index++)
	{
		unsigned char flood[P2P_PROBE_ID_SIZE];

		fill(flood, sizeof(flood), 3000);
		flood[0] = (unsigned char)index;
		flood[1] = (unsigned char)(index >> 8);
		admitted += p2p_probe_host_request(&host, 10000 + (unsigned long)index * 5, flood, addresses, 1);
	}
	/* (10 s at 4 a second, and the burst; a repeated id is answered again,
	not taken again) */
	CHECK(admitted > 0 && host.admitted <= P2P_PROBE_HOST_BURST + 10000 / P2P_PROBE_HOST_INTERVAL + 1);
}

static void test_prober(void)
{
	static struct p2p_probe_prober prober;
	unsigned char hash[P2P_PROBE_HASH_SIZE], token[P2P_PROBE_TOKEN_SIZE], random[P2P_PROBE_ID_SIZE];
	unsigned char packets[8][P2P_PROBE_PACKET_SIZE], pong[P2P_PROBE_PACKET_SIZE];
	struct p2p_probe_address to[8], host_addresses[1];
	struct p2p_probe_target *target, *next;
	unsigned long now = 0xFFFFF000ul;
	int action, count, requests;

	memset(&prober, 0, sizeof(prober));
	fill(hash, sizeof(hash), 7);
	fill(token, sizeof(token), 8);
	fill(random, sizeof(random), 9);
	host_addresses[0] = address(0x0300007F, 0x4444);

	/* not asked about: nothing */
	CHECK(!p2p_probe_target(&prober, now, hash, token, 0));
	CHECK(!p2p_probe_prober_next(&prober, now, random, &action, packets, to, 8, &count));
	target = p2p_probe_target(&prober, now, hash, token, 1);
	CHECK(target && target->ping == -1);
	next = p2p_probe_prober_next(&prober, now, random, &action, packets, to, 8, &count);
	CHECK(next == target && action == 1 && target->state == P2P_PROBE_ASKING);
	CHECK(!p2p_probe_prober_next(&prober, now, random, &action, packets, to, 8, &count));
	/* resent until answered, a few times */
	requests = 1;
	for (now += 100; now - 0xFFFFF000ul < 4900; now += 100)
	{
		p2p_probe_target(&prober, now, hash, token, 1);
		while ((next = p2p_probe_prober_next(&prober, now, random, &action, packets, to, 8, &count)) != NULL)
			requests += action == 1;
	}
	CHECK(requests == P2P_PROBE_REQUESTS);
	/* answered (across the clock's wrap): pings to its address */
	p2p_probe_prober_answer(&prober, now, target->id, host_addresses, 1);
	CHECK(target->state == P2P_PROBE_PINGING);
	next = p2p_probe_prober_next(&prober, now, random, &action, packets, to, 8, &count);
	CHECK(next == target && action == 2 && count == 1 && to[0].port == 0x4444);
	/* a PONG from elsewhere is not taken; from the host, 30 ms later, is */
	memcpy(pong, packets[0], sizeof(pong));
	pong[1] = P2P_PROBE_PONG;
	CHECK(!p2p_probe_prober_packet(&prober, now + 30, pong, sizeof(pong), 0x0400007F, 0x4444));
	CHECK(p2p_probe_prober_packet(&prober, now + 30, pong, sizeof(pong), 0x0300007F, 0x4444));
	CHECK(target->best == 30);
	/* two more (the lowest kept): measured */
	now += 200;
	next = p2p_probe_prober_next(&prober, now, random, &action, packets, to, 8, &count);
	CHECK(next == target && action == 2);
	memcpy(pong, packets[0], sizeof(pong));
	pong[1] = P2P_PROBE_PONG;
	CHECK(p2p_probe_prober_packet(&prober, now + 25, pong, sizeof(pong), 0x0300007F, 0x4444));
	CHECK(p2p_probe_prober_packet(&prober, now + 40, pong, sizeof(pong), 0x0300007F, 0x4444));
	CHECK(target->state == P2P_PROBE_MEASURED && target->ping == 25);
	/* not again until a minute later */
	CHECK(!p2p_probe_prober_next(&prober, now + 1000, random, &action, packets, to, 8, &count));
	p2p_probe_target(&prober, now + P2P_PROBE_REMEASURE + 100, hash, token, 1);
	next = p2p_probe_prober_next(&prober, now + P2P_PROBE_REMEASURE + 100, random, &action, packets, to, 8, &count);
	CHECK(next == target && action == 1 && target->ping == 25);

	/* a host that never answers: failed, no number */
	memset(&prober, 0, sizeof(prober));
	now = 100;
	target = p2p_probe_target(&prober, now, hash, token, 1);
	for (; now < 100 + P2P_PROBE_TIMEOUT + 200; now += 50)
	{
		p2p_probe_target(&prober, now, hash, token, 1);
		while (p2p_probe_prober_next(&prober, now, random, &action, packets, to, 8, &count))
			;
	}
	CHECK(target->state == P2P_PROBE_FAILED && target->ping == -1);

	/* at most a few at once */
	memset(&prober, 0, sizeof(prober));
	{
		int index, started = 0;

		for (index = 0; index < 10; index++)
		{
			hash[0] = (unsigned char)index;
			p2p_probe_target(&prober, 0, hash, token, 1);
		}
		while (p2p_probe_prober_next(&prober, 0, random, &action, packets, to, 8, &count))
			started += action == 1;
		CHECK(started == P2P_PROBE_IN_FLIGHT);
	}
}

static void test_anchors(void)
{
	char text[] =
		"# anchors\n"
		"anchor\thalo.milenko.org\t443\tNA\n"
		"anchor\teu.example.org\t443\tEU,EU-DE\r\n"
		"anchor\tbad host\t443\tEU\n"
		"anchor\tnoregions.example.org\t443\tnope,x\n"
		"anchor\tport.example.org\t70000\tEU\n"
		"other\tthing\n"
		"anchor\tde.example.org\t80\tEU-DE";
	struct browser_ping_anchor anchors[BROWSER_PING_ANCHORS];
	int count = browser_ping_parse_anchors(text, anchors, BROWSER_PING_ANCHORS);

	CHECK(count == 3);
	CHECK(!strcmp(anchors[1].host, "eu.example.org") && anchors[1].region_count == 2 && anchors[1].port == 443);
	CHECK(anchors[0].ping == -1);
	/* nothing timed: no estimate */
	CHECK(browser_ping_estimate(anchors, count, "EU-DE") == -1);
	anchors[0].ping = 40;
	anchors[1].ping = 110;
	anchors[2].ping = 95;
	/* the country first, then the continent; another continent: none */
	CHECK(browser_ping_estimate(anchors, count, "EU-DE") == 95);
	CHECK(browser_ping_estimate(anchors, count, "EU-FR") == 110);
	CHECK(browser_ping_estimate(anchors, count, "NA-US") == 40);
	CHECK(browser_ping_estimate(anchors, count, "AS-JP") == -1);
	CHECK(browser_ping_estimate(anchors, count, "") == -1);
	CHECK(browser_ping_estimate(anchors, count, "eu-de") == -1);
	CHECK(browser_ping_region_valid("NA") && browser_ping_region_valid("NA-US") && !browser_ping_region_valid("NA-") &&
		!browser_ping_region_valid("NA-USA") && !browser_ping_region_valid(NULL));
}

static void test_order(void)
{
	/* measured (lowest first), then estimated, then none */
	CHECK(browser_ping_order(80, 0, 20, 1) < 0);
	CHECK(browser_ping_order(20, 1, 80, 0) > 0);
	CHECK(browser_ping_order(30, 0, 50, 0) < 0);
	CHECK(browser_ping_order(30, 1, 50, 1) < 0);
	CHECK(browser_ping_order(-1, 0, 500, 1) > 0);
	CHECK(browser_ping_order(-1, 0, -1, 1) == 0);
}

int main(void)
{
	test_packet();
	test_messages();
	test_host();
	test_prober();
	test_anchors();
	test_order();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
