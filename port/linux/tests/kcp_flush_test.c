/*
KCP_FLUSH_TEST.C

How long a stream's message waits between its write and its first send, in
the shape of p2p.c's loop (port/linux/src/p2p.c: stream_readable writes,
stream_update ticks KCP, then flushes what the pass wrote), over a loopback
of two KCP ends on a simulated clock. tools/test_kcp_flush.py builds and
runs it. Without the flush a message waits for KCP's next tick, at times
past the loop's 10 ms; with it the message leaves in the pass that wrote it.
Either way the other end gets the same messages in the same order, and a
pass of many writes still flushes once.
*/

#include "../../third_party/kcp/ikcp.h"

#include <stdio.h>
#include <string.h>

enum
{
	LOOP_INTERVAL = 10, /* p2p.c's */
	KCP_MTU = 1200,
	MESSAGE_COUNT = 2000,
	MESSAGE_SIZE = 40,
	IKCP_OVERHEAD = 24,
	IKCP_CMD_PUSH = 81,
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

static unsigned long now;
static ikcpcb *sender, *receiver;
static long written_at[MESSAGE_COUNT], sent_at[MESSAGE_COUNT];
static int datagrams_this_pass;

static unsigned int get32(const unsigned char *bytes)
{
	return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | (unsigned int)bytes[3] << 24;
}

static int sender_output(const char *buffer, int size, ikcpcb *kcp, void *user)
{
	const unsigned char *at = (const unsigned char *)buffer;

	(void)kcp;
	(void)user;
	datagrams_this_pass++;
	/* when each message first went out */
	while (at + IKCP_OVERHEAD <= (const unsigned char *)buffer + size)
	{
		unsigned int sn = get32(at + 12), length = get32(at + 20);

		if (at[4] == IKCP_CMD_PUSH && sn < MESSAGE_COUNT && sent_at[sn] < 0)
			sent_at[sn] = (long)now;
		at += IKCP_OVERHEAD + length;
	}
	ikcp_input(receiver, buffer, size);
	return 0;
}

static int receiver_output(const char *buffer, int size, ikcpcb *kcp, void *user)
{
	(void)kcp;
	(void)user;
	ikcp_input(sender, buffer, size);
	return 0;
}

static ikcpcb *new_end(int (*output)(const char *, int, ikcpcb *, void *))
{
	ikcpcb *kcp = ikcp_create(7, NULL);

	ikcp_setoutput(kcp, output);
	ikcp_nodelay(kcp, 1, LOOP_INTERVAL, 2, 1);
	ikcp_wndsize(kcp, 256, 256);
	ikcp_setmtu(kcp, KCP_MTU);
	return kcp;
}

static unsigned int random_state;

static unsigned int next_random(void)
{
	random_state = random_state * 1103515245u + 12345u;
	return random_state >> 16;
}

/* the messages' writes, at times the game picks (a few a 33 ms tick), and
one pass's burst of many; returns the mean wait in tenths of a ms */
static long run(int flush, long *worst, int *burst_datagrams)
{
	long write_time[MESSAGE_COUNT];
	long next_tick = 0, total = 0;
	int written = 0, received = 0, index, burst = MESSAGE_COUNT / 2;

	sender = new_end(sender_output);
	receiver = new_end(receiver_output);
	random_state = 1;
	for (index = 0; index < MESSAGE_COUNT; index++)
	{
		write_time[index] = (index ? write_time[index - 1] : 1) + (index == burst ? 0 : (long)(next_random() % 34));
		written_at[index] = sent_at[index] = -1;
	}
	/* (32 more at the burst's time) */
	for (index = burst + 1; index < burst + 32; index++)
		write_time[index] = write_time[burst];
	*burst_datagrams = 0;
	now = 0;
	while (received < MESSAGE_COUNT && now < 600000)
	{
		int wrote = 0, burst_pass;
		char message[MESSAGE_SIZE];

		/* the loop's wait: its interval, or until the game's socket has
		something */
		now = (unsigned long)next_tick;
		if (written < MESSAGE_COUNT && write_time[written] < next_tick)
			now = (unsigned long)write_time[written];
		next_tick = (long)now + LOOP_INTERVAL;
		datagrams_this_pass = 0;
		burst_pass = written <= burst && written < MESSAGE_COUNT && write_time[written] <= (long)now &&
			write_time[burst] <= (long)now;
		/* stream_readable */
		while (written < MESSAGE_COUNT && write_time[written] <= (long)now)
		{
			memset(message, 0, sizeof(message));
			memcpy(message, &written, sizeof(written));
			ikcp_send(sender, message, sizeof(message));
			written_at[written++] = (long)now;
			wrote = 1;
		}
		/* stream_update */
		ikcp_update(sender, (IUINT32)now);
		if (flush && wrote)
			ikcp_flush(sender);
		if (burst_pass)
			*burst_datagrams = datagrams_this_pass;
		ikcp_update(receiver, (IUINT32)now);
		for (;;)
		{
			int got;
			int size = ikcp_recv(receiver, message, sizeof(message));

			if (size < 0)
				break;
			CHECK(size == MESSAGE_SIZE);
			memcpy(&got, message, sizeof(got));
			CHECK(got == received);
			received++;
		}
	}
	CHECK(received == MESSAGE_COUNT);
	*worst = 0;
	for (index = 0; index < MESSAGE_COUNT; index++)
	{
		long wait = sent_at[index] - written_at[index];

		CHECK(sent_at[index] >= 0);
		total += wait;
		if (wait > *worst)
			*worst = wait;
	}
	ikcp_release(sender);
	ikcp_release(receiver);
	return total * 10 / MESSAGE_COUNT;
}

int main(void)
{
	long before_worst, after_worst, before_mean, after_mean;
	int before_burst, after_burst;

	before_mean = run(0, &before_worst, &before_burst);
	after_mean = run(1, &after_worst, &after_burst);
	printf("write to send, tick only: mean %ld.%ld ms, worst %ld ms; a pass of 32 writes: %d datagrams\n",
		before_mean / 10, before_mean % 10, before_worst, before_burst);
	printf("write to send, flushed:   mean %ld.%ld ms, worst %ld ms; a pass of 32 writes: %d datagrams\n",
		after_mean / 10, after_mean % 10, after_worst, after_burst);
	CHECK(before_worst > 0);
	CHECK(after_worst == 0);
	CHECK(after_mean < before_mean);
	/* (one flush a pass: the 32 messages, 2 KB with KCP's headers, go
	packed in MTU-sized datagrams, not one each) */
	CHECK(after_burst >= 1 && after_burst <= 3);
	if (failures)
		return 1;
	printf("kcp_flush_test: ok\n");
	return 0;
}
