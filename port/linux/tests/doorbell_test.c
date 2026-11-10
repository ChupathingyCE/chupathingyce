/*
DOORBELL_TEST.C

Tests of Delta's doorbell (port/linux/src/doorbell.c): its packets, the
subscription over a fake transport, pings, the brokers' failover and the
retries' backoff. With --broker host:port --id <doorbell> it instead
listens on a real MQTT broker over plain TCP and exits 0 once a ring came
(tools/test_doorbell.py runs a local mosquitto with the doorbell's ACL).
Built and run by tools/test_doorbell.py; exits nonzero on a failure.
*/

#define _POSIX_C_SOURCE 200809L

#include "../src/doorbell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

/* whether size bytes at data hold the length bytes of what */
static int contains(const unsigned char *data, int size, const char *what, int length)
{
	int at;

	for (at = 0; at + length <= size; at++)
	{
		if (!memcmp(data + at, what, (size_t)length))
			return 1;
	}
	return 0;
}

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

#define ID "0123456789abcdef0123456789abcdef"
#define OTHER "fedcba9876543210fedcba9876543210"

/* ---------- a fake transport: opens as told, hands out what was queued,
keeps what was written */

static struct
{
	int open_fails;
	int opens;
	char last_host[64];
	int handle;
	int closed;
	unsigned char incoming[2048];
	int incoming_size;
	/* at most this many bytes a read (0: all) */
	int chunk;
	int dead;
	unsigned char written[2048];
	int written_size;
} fake;

static int fake_open(void *context, const char *host, const char *port, char *error, int error_size)
{
	(void)context;
	(void)port;
	fake.opens++;
	snprintf(fake.last_host, sizeof(fake.last_host), "%s", host);
	if (fake.open_fails > 0)
	{
		fake.open_fails--;
		snprintf(error, (size_t)error_size, "refused");
		return 0;
	}
	fake.handle++;
	fake.closed = 0;
	fake.dead = 0;
	fake.incoming_size = 0;
	fake.written_size = 0;
	return fake.handle;
}

static int fake_write(void *context, int handle, const unsigned char *data, int size)
{
	(void)context;
	if (handle != fake.handle || fake.closed || fake.written_size + size > (int)sizeof(fake.written))
		return 0;
	memcpy(fake.written + fake.written_size, data, (size_t)size);
	fake.written_size += size;
	return 1;
}

static int fake_read(void *context, int handle, unsigned char *buffer, int size, int wait)
{
	int count = fake.incoming_size;

	(void)context;
	(void)wait;
	if (handle != fake.handle || fake.closed || fake.dead)
		return -1;
	if (fake.chunk && count > fake.chunk)
		count = fake.chunk;
	if (count > size)
		count = size;
	memcpy(buffer, fake.incoming, (size_t)count);
	memmove(fake.incoming, fake.incoming + count, (size_t)(fake.incoming_size - count));
	fake.incoming_size -= count;
	return count;
}

static void fake_close(void *context, int handle)
{
	(void)context;
	if (handle == fake.handle)
		fake.closed = 1;
}

static const struct doorbell_transport transport = { NULL, fake_open, fake_write, fake_read, fake_close };
static const char *const brokers[] = { "doorbell.example:8883", "mqtt.example:8883" };

static void queue(const unsigned char *data, int size)
{
	memcpy(fake.incoming + fake.incoming_size, data, (size_t)size);
	fake.incoming_size += size;
}

static void queue_publish(const char *id, unsigned char kind)
{
	unsigned char packet[128];
	char topic[64];
	int length;

	snprintf(topic, sizeof(topic), "doorbell/%s", id);
	length = (int)strlen(topic);
	packet[0] = 0x30;
	packet[1] = (unsigned char)(2 + length + 1);
	packet[2] = 0;
	packet[3] = (unsigned char)length;
	memcpy(packet + 4, topic, (size_t)length);
	packet[4 + length] = kind;
	queue(packet, 5 + length);
}

/* connected and subscribed at now: 1 if all went as it should */
static int subscribe(struct doorbell *doorbell, unsigned long now)
{
	static const unsigned char connack[] = { 0x20, 0x02, 0x00, 0x00 };
	static const unsigned char suback[] = { 0x90, 0x03, 0x00, 0x01, 0x00 };
	int ok = 1;

	doorbell_step(doorbell, now, 0);
	ok &= fake.written_size > 0 && fake.written[0] == 0x10;
	queue(connack, sizeof(connack));
	doorbell_step(doorbell, now, 0);
	ok &= fake.written[fake.written_size - 1] == 0 && memchr(fake.written, 0x82, (size_t)fake.written_size) != NULL;
	queue(suback, sizeof(suback));
	doorbell_step(doorbell, now, 0);
	ok &= doorbell->state == _doorbell_listening;
	return ok;
}

/* ---------- the tests */

static void test_packets(void)
{
	unsigned char buffer[256];
	static const unsigned char connect[] =
	{
		0x10, 12 + 32, 0, 4, 'M', 'Q', 'T', 'T', 4, 0x02, 0, 60, 0, 32,
	};
	static const unsigned char subscribe_head[] = { 0x82, 2 + 2 + 41 + 1, 0, 1, 0, 41 };
	int size;

	size = doorbell_connect_packet(buffer, sizeof(buffer), ID, 60);
	CHECK(size == (int)sizeof(connect) + 32);
	CHECK(!memcmp(buffer, connect, sizeof(connect)));
	CHECK(!memcmp(buffer + sizeof(connect), ID, 32));
	CHECK(doorbell_connect_packet(buffer, 10, ID, 60) == 0);

	size = doorbell_subscribe_packet(buffer, sizeof(buffer), 1, "doorbell/" ID);
	CHECK(size == (int)sizeof(subscribe_head) + 41 + 1);
	CHECK(!memcmp(buffer, subscribe_head, sizeof(subscribe_head)));
	CHECK(!memcmp(buffer + sizeof(subscribe_head), "doorbell/" ID, 41));
	CHECK(buffer[size - 1] == 0);

	CHECK(doorbell_packet_size((const unsigned char *)"\x20", 1) == 0);
	CHECK(doorbell_packet_size((const unsigned char *)"\x20\x02", 2) == 4);
	CHECK(doorbell_packet_size((const unsigned char *)"\x30\x80", 2) == 0);
	CHECK(doorbell_packet_size((const unsigned char *)"\x30\x80\x01", 3) == 3 + 128);
	CHECK(doorbell_packet_size((const unsigned char *)"\x30\xff\xff\xff\xff\x01", 6) == -1);

	CHECK(doorbell_id_valid(ID));
	CHECK(!doorbell_id_valid("0123456789ABCDEF0123456789abcdef"));
	CHECK(!doorbell_id_valid("#"));
	CHECK(!doorbell_id_valid("0123456789abcdef0123456789abcde+"));
	CHECK(!doorbell_id_valid(""));
	CHECK(!doorbell_id_valid(NULL));
}

static void test_rings(void)
{
	static struct doorbell doorbell;
	const char *host;

	memset(&fake, 0, sizeof(fake));
	doorbell_init(&doorbell, &transport, brokers, 2, 0);
	/* (nothing wanted: no connection) */
	CHECK(doorbell_step(&doorbell, 0, 0) == 0);
	CHECK(fake.opens == 0);
	CHECK(doorbell_want(&doorbell, "not a doorbell") == 0);
	CHECK(doorbell_step(&doorbell, 0, 0) == 0 && fake.opens == 0);

	CHECK(doorbell_want(&doorbell, ID) == 1);
	CHECK(doorbell_want(&doorbell, ID) == 0);
	CHECK(subscribe(&doorbell, 1000));
	CHECK(!strcmp(fake.last_host, "doorbell.example"));
	/* (the client ID is the doorbell) */
	CHECK(contains(fake.written, fake.written_size, ID, 32));
	CHECK(doorbell_take_news(&doorbell, &host) == _doorbell_news_listening);
	CHECK(!strcmp(host, "doorbell.example"));

	queue_publish(ID, _doorbell_ring_invite);
	CHECK(doorbell_step(&doorbell, 2000, 0) == 1);
	CHECK(doorbell.last_kind == _doorbell_ring_invite);
	/* (another topic: no ring) */
	queue_publish(OTHER, _doorbell_ring_invite);
	CHECK(doorbell_step(&doorbell, 2100, 0) == 0);
	/* (two at once, and one cut across reads) */
	queue_publish(ID, _doorbell_ring_friends);
	queue_publish(ID, _doorbell_ring_presence);
	CHECK(doorbell_step(&doorbell, 2200, 0) == 2);
	fake.chunk = 7;
	queue_publish(ID, _doorbell_ring_party);
	{
		int rings = 0, turn;

		for (turn = 0; turn < 10; turn++)
			rings += doorbell_step(&doorbell, 2300, 0);
		CHECK(rings == 1);
		CHECK(doorbell.last_kind == _doorbell_ring_party);
	}
	fake.chunk = 0;

	/* (a ping after a quiet while; its answer keeps the connection) */
	fake.written_size = 0;
	doorbell_step(&doorbell, 2300 + DOORBELL_PING_AFTER, 0);
	CHECK(fake.written_size == 2 && fake.written[0] == 0xC0);
	queue((const unsigned char *)"\xd0\x00", 2);
	doorbell_step(&doorbell, 2300 + DOORBELL_PING_AFTER + 100, 0);
	CHECK(!doorbell.ping_out && doorbell.state == _doorbell_listening);
	/* (a ping without an answer: lost, the other broker a second later) */
	doorbell_step(&doorbell, 2300 + 2 * DOORBELL_PING_AFTER, 0);
	CHECK(doorbell.ping_out);
	doorbell_step(&doorbell, 2300 + 2 * DOORBELL_PING_AFTER + DOORBELL_ANSWER_WAIT, 0);
	CHECK(doorbell.state == _doorbell_idle && fake.closed);
	CHECK(doorbell_take_news(&doorbell, &host) & _doorbell_news_lost);
	CHECK(!strcmp(host, "doorbell.example"));
	{
		unsigned long lost = 2300 + 2 * DOORBELL_PING_AFTER + DOORBELL_ANSWER_WAIT;
		int opens = fake.opens;

		doorbell_step(&doorbell, lost + DOORBELL_NEXT_BROKER_WAIT - 1, 0);
		CHECK(fake.opens == opens);
		CHECK(subscribe(&doorbell, lost + DOORBELL_NEXT_BROKER_WAIT));
		CHECK(!strcmp(fake.last_host, "mqtt.example"));
	}

	/* (a connection that ends: lost) */
	fake.dead = 1;
	doorbell_step(&doorbell, 500000, 0);
	CHECK(doorbell.state == _doorbell_idle);

	/* (another doorbell: the connection made again for it) */
	doorbell_want(&doorbell, OTHER);
	CHECK(subscribe(&doorbell, 600000));
	CHECK(contains(fake.written, fake.written_size, OTHER, 32));
	queue_publish(ID, _doorbell_ring_any);
	CHECK(doorbell_step(&doorbell, 600100, 0) == 0);
	queue_publish(OTHER, _doorbell_ring_any);
	CHECK(doorbell_step(&doorbell, 600200, 0) == 1);

	/* (none: closed, with a DISCONNECT only from doorbell_stop) */
	doorbell_want(&doorbell, "");
	doorbell_step(&doorbell, 600300, 0);
	CHECK(fake.closed && doorbell.state == _doorbell_idle);
	doorbell_stop(&doorbell);
}

static void test_failover(void)
{
	static struct doorbell doorbell;
	unsigned long now = 0, wait;
	int round;

	memset(&fake, 0, sizeof(fake));
	doorbell_init(&doorbell, &transport, brokers, 2, 0);
	doorbell_want(&doorbell, ID);
	fake.open_fails = 1000;
	doorbell_step(&doorbell, now, 0);
	CHECK(fake.opens == 1 && !strcmp(fake.last_host, "doorbell.example"));
	/* (the other broker a second later) */
	doorbell_step(&doorbell, now + DOORBELL_NEXT_BROKER_WAIT - 1, 0);
	CHECK(fake.opens == 1);
	now += DOORBELL_NEXT_BROKER_WAIT;
	doorbell_step(&doorbell, now, 0);
	CHECK(fake.opens == 2 && !strcmp(fake.last_host, "mqtt.example"));
	/* (both down: 5 s, 10 s, ... up to 5 minutes, a quarter more at most) */
	wait = DOORBELL_RETRY_FIRST;
	for (round = 0; round < 12; round++)
	{
		int opens = fake.opens;
		unsigned long waited = doorbell.retry_time - now;

		CHECK(waited >= wait && waited <= wait + wait / 4);
		now = doorbell.retry_time;
		doorbell_step(&doorbell, now, 0);
		CHECK(fake.opens == opens + 1 && !strcmp(fake.last_host, "doorbell.example"));
		CHECK(doorbell.retry_time - now == DOORBELL_NEXT_BROKER_WAIT);
		now = doorbell.retry_time;
		doorbell_step(&doorbell, now, 0);
		CHECK(fake.opens == opens + 2);
		if (wait < DOORBELL_RETRY_MOST)
			wait = wait * 2 < DOORBELL_RETRY_MOST ? wait * 2 : DOORBELL_RETRY_MOST;
	}
	CHECK(doorbell.retry_time - now <= DOORBELL_RETRY_MOST + DOORBELL_RETRY_MOST / 4);

	/* (a broker back: listening, and the next failure starts over) */
	fake.open_fails = 0;
	now = doorbell.retry_time;
	CHECK(subscribe(&doorbell, now));
	CHECK(doorbell.failures == 0);

	/* (a broker that never answers the connect) */
	memset(&fake, 0, sizeof(fake));
	doorbell_init(&doorbell, &transport, brokers, 2, 0);
	doorbell_want(&doorbell, ID);
	doorbell_step(&doorbell, 0, 0);
	doorbell_step(&doorbell, DOORBELL_ANSWER_WAIT - 1, 0);
	CHECK(doorbell.state == _doorbell_connecting);
	doorbell_step(&doorbell, DOORBELL_ANSWER_WAIT, 0);
	CHECK(doorbell.state == _doorbell_idle && fake.closed);
	CHECK(doorbell_take_news(&doorbell, NULL) == _doorbell_news_failed);

	/* (a connect refused by the broker) */
	memset(&fake, 0, sizeof(fake));
	doorbell_init(&doorbell, &transport, brokers, 2, 0);
	doorbell_want(&doorbell, ID);
	doorbell_step(&doorbell, 0, 0);
	queue((const unsigned char *)"\x20\x02\x00\x05", 4);
	doorbell_step(&doorbell, 10, 0);
	CHECK(doorbell.state == _doorbell_idle && doorbell.retrying);

	/* (not MQTT: too long a packet) */
	memset(&fake, 0, sizeof(fake));
	doorbell_init(&doorbell, &transport, brokers, 2, 0);
	doorbell_want(&doorbell, ID);
	CHECK(subscribe(&doorbell, 0));
	queue((const unsigned char *)"\x30\xff\x7f", 3);
	doorbell_step(&doorbell, 10, 0);
	CHECK(doorbell.state == _doorbell_idle && fake.closed);
}

static void test_refused(void)
{
	static struct doorbell doorbell;
	int opens;

	memset(&fake, 0, sizeof(fake));
	doorbell_init(&doorbell, &transport, brokers, 2, 0);
	doorbell_want(&doorbell, ID);
	doorbell_step(&doorbell, 0, 0);
	queue((const unsigned char *)"\x20\x02\x00\x00", 4);
	doorbell_step(&doorbell, 0, 0);
	queue((const unsigned char *)"\x90\x03\x00\x01\x80", 5);
	doorbell_step(&doorbell, 0, 0);
	CHECK(doorbell.refused && fake.closed);
	CHECK(doorbell_take_news(&doorbell, NULL) == _doorbell_news_refused);
	/* (no more tries for that doorbell) */
	opens = fake.opens;
	doorbell_step(&doorbell, 10 * 60000, 0);
	CHECK(fake.opens == opens);
	/* (another one: tried) */
	doorbell_want(&doorbell, OTHER);
	doorbell_step(&doorbell, 10 * 60000, 0);
	CHECK(fake.opens == opens + 1);
}

static void test_one_broker(void)
{
	static struct doorbell doorbell;
	static const char *const one[] = { "doorbell.example:8883", "no port", ":1", "host:" };

	memset(&fake, 0, sizeof(fake));
	doorbell_init(&doorbell, &transport, one, 4, 0);
	CHECK(doorbell.broker_count == 1);
	doorbell_want(&doorbell, ID);
	fake.open_fails = 10;
	doorbell_step(&doorbell, 0, 0);
	CHECK(doorbell.retry_time >= DOORBELL_RETRY_FIRST);
}

/* ---------- a real broker (plain TCP) */

#ifndef _WIN32
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int tcp_open(void *context, const char *host, const char *port, char *error, int error_size)
{
	struct addrinfo hints, *found;
	int descriptor;

	(void)context;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port, &hints, &found) != 0)
	{
		snprintf(error, (size_t)error_size, "no address");
		return 0;
	}
	descriptor = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
	if (descriptor < 0 || connect(descriptor, found->ai_addr, found->ai_addrlen) != 0)
	{
		if (descriptor >= 0)
			close(descriptor);
		freeaddrinfo(found);
		snprintf(error, (size_t)error_size, "no connection");
		return 0;
	}
	freeaddrinfo(found);
	return descriptor + 1;
}

static int tcp_write(void *context, int handle, const unsigned char *data, int size)
{
	(void)context;
	return send(handle - 1, data, (size_t)size, 0) == size;
}

static int tcp_read(void *context, int handle, unsigned char *buffer, int size, int wait)
{
	struct pollfd entry;
	long count;

	(void)context;
	entry.fd = handle - 1;
	entry.events = POLLIN;
	if (poll(&entry, 1, wait) <= 0)
		return 0;
	count = recv(handle - 1, buffer, (size_t)size, 0);
	return count > 0 ? (int)count : -1;
}

static void tcp_close(void *context, int handle)
{
	(void)context;
	close(handle - 1);
}

static unsigned long milliseconds(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return (unsigned long)now.tv_sec * 1000 + (unsigned long)now.tv_usec / 1000;
}

/* listens on broker for id: prints "listening" once subscribed, "ring" at
each ring, exits 0 after the first, 2 if refused, 1 after 20 s */
static int listen_on(const char *broker, const char *id)
{
	static const struct doorbell_transport tcp = { NULL, tcp_open, tcp_write, tcp_read, tcp_close };
	static struct doorbell doorbell;
	unsigned long start = milliseconds();

	setvbuf(stdout, NULL, _IONBF, 0);
	doorbell_init(&doorbell, &tcp, &broker, 1, 0);
	doorbell_want(&doorbell, id);
	while (milliseconds() - start < 20000)
	{
		int rings = doorbell_step(&doorbell, milliseconds(), 200);
		int news = doorbell_take_news(&doorbell, NULL);

		if (news & _doorbell_news_listening)
			printf("listening\n");
		if (news & _doorbell_news_refused)
		{
			printf("refused\n");
			return 2;
		}
		if (rings)
		{
			printf("ring %d\n", doorbell.last_kind);
			doorbell_stop(&doorbell);
			return 0;
		}
		if (!doorbell.handle)
			poll(NULL, 0, 50);
	}
	return 1;
}
#endif

int main(int argc, char **argv)
{
#ifndef _WIN32
	if (argc == 5 && !strcmp(argv[1], "--broker") && !strcmp(argv[3], "--id"))
		return listen_on(argv[2], argv[4]);
#else
	(void)argc;
	(void)argv;
#endif
	test_packets();
	test_rings();
	test_failover();
	test_refused();
	test_one_broker();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("doorbell: all passed\n");
	return 0;
}
