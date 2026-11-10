/*
DOORBELL.C

Delta's doorbell (doorbell.h): MQTT 3.1.1 at QoS 0 with one subscription,
the broker's failover and the retries' backoff. No sockets, no threads.
*/

#include "doorbell.h"

#include <stdio.h>
#include <string.h>

#define TOPIC_PREFIX "doorbell/"

static int passed(unsigned long now, unsigned long since, unsigned long interval)
{
	return now - since >= interval;
}

int doorbell_id_valid(const char *text)
{
	return text && strlen(text) == DOORBELL_ID_SIZE - 1 &&
		strspn(text, "0123456789abcdef") == DOORBELL_ID_SIZE - 1;
}

/* ---------- packets */

/* the remaining length's bytes at buffer: their count */
static int put_length(unsigned char *buffer, int length)
{
	int count = 0;

	do
	{
		unsigned char byte = (unsigned char)(length % 128);

		length /= 128;
		if (length > 0)
			byte |= 0x80;
		buffer[count++] = byte;
	} while (length > 0);
	return count;
}

static int put_string(unsigned char *buffer, const char *text, int length)
{
	buffer[0] = (unsigned char)(length >> 8);
	buffer[1] = (unsigned char)length;
	memcpy(buffer + 2, text, (size_t)length);
	return 2 + length;
}

int doorbell_connect_packet(unsigned char *buffer, int size, const char *client_id, int keep_alive)
{
	int id_length = (int)strlen(client_id);
	int rest = 10 + 2 + id_length;
	int at = 0;

	if (id_length > 64 || size < 1 + 4 + rest)
		return 0;
	buffer[at++] = 0x10;
	at += put_length(buffer + at, rest);
	at += put_string(buffer + at, "MQTT", 4);
	/* (3.1.1, a clean session: nothing kept at the broker between
	connections; no user name, no will) */
	buffer[at++] = 4;
	buffer[at++] = 0x02;
	buffer[at++] = (unsigned char)(keep_alive >> 8);
	buffer[at++] = (unsigned char)keep_alive;
	at += put_string(buffer + at, client_id, id_length);
	return at;
}

int doorbell_subscribe_packet(unsigned char *buffer, int size, unsigned short identifier, const char *topic)
{
	int topic_length = (int)strlen(topic);
	int rest = 2 + 2 + topic_length + 1;
	int at = 0;

	if (topic_length > 128 || size < 1 + 4 + rest)
		return 0;
	buffer[at++] = 0x82;
	at += put_length(buffer + at, rest);
	buffer[at++] = (unsigned char)(identifier >> 8);
	buffer[at++] = (unsigned char)identifier;
	at += put_string(buffer + at, topic, topic_length);
	/* (QoS 0: a ring missed is a poll a little later, nothing more) */
	buffer[at++] = 0;
	return at;
}

int doorbell_packet_size(const unsigned char *data, int size)
{
	int length = 0, multiplier = 1, at = 1;

	if (size < 2)
		return 0;
	for (;;)
	{
		unsigned char byte;

		if (at >= size)
			return 0;
		if (at > 4)
			return -1;
		byte = data[at++];
		length += (byte & 0x7F) * multiplier;
		multiplier *= 128;
		if (!(byte & 0x80))
			break;
	}
	return at + length;
}

/* ---------- the connection */

void doorbell_init(struct doorbell *doorbell, const struct doorbell_transport *transport,
	const char *const *brokers, int broker_count, unsigned long seed)
{
	int index;

	memset(doorbell, 0, sizeof(*doorbell));
	doorbell->transport = transport;
	doorbell->seed = seed;
	for (index = 0; index < broker_count && doorbell->broker_count < DOORBELL_MAXIMUM_BROKERS; index++)
	{
		const char *colon = strrchr(brokers[index], ':');
		int host_length = colon ? (int)(colon - brokers[index]) : 0;

		if (!colon || host_length <= 0 || host_length >= DOORBELL_HOST_SIZE || !colon[1] || strlen(colon + 1) >= 8)
			continue;
		memcpy(doorbell->hosts[doorbell->broker_count], brokers[index], (size_t)host_length);
		doorbell->hosts[doorbell->broker_count][host_length] = 0;
		snprintf(doorbell->ports[doorbell->broker_count], sizeof(doorbell->ports[0]), "%s", colon + 1);
		doorbell->broker_count++;
	}
}

static void close_connection(struct doorbell *doorbell)
{
	if (doorbell->handle)
		doorbell->transport->close(doorbell->transport->context, doorbell->handle);
	doorbell->handle = 0;
	doorbell->state = _doorbell_idle;
	doorbell->input_size = 0;
	doorbell->ping_out = 0;
	doorbell->listening[0] = 0;
}

/* the connection failed (or one that held was lost): the next broker,
after a wait that grows once every broker failed */
static void failed(struct doorbell *doorbell, unsigned long now)
{
	unsigned long wait;
	int rounds;

	if (doorbell->state == _doorbell_listening)
	{
		doorbell->news |= _doorbell_news_lost;
		doorbell->failures = 0;
	}
	else
	{
		doorbell->news |= _doorbell_news_failed;
	}
	doorbell->news_broker = doorbell->broker;
	close_connection(doorbell);
	if (doorbell->failures < 64)
		doorbell->failures++;
	if (doorbell->broker_count > 0)
		doorbell->broker = (doorbell->broker + 1) % doorbell->broker_count;
	if (doorbell->broker_count > 1 && doorbell->failures % doorbell->broker_count)
	{
		wait = DOORBELL_NEXT_BROKER_WAIT;
	}
	else
	{
		rounds = doorbell->broker_count > 1 ? doorbell->failures / doorbell->broker_count : doorbell->failures;
		wait = DOORBELL_RETRY_FIRST;
		while (--rounds > 0 && wait < DOORBELL_RETRY_MOST)
			wait *= 2;
		if (wait > DOORBELL_RETRY_MOST)
			wait = DOORBELL_RETRY_MOST;
		/* (up to a quarter more, by the seed) */
		wait += wait / 4 * ((doorbell->seed + (unsigned long)doorbell->failures * 2654435761UL) % 256) / 256;
	}
	doorbell->retrying = 1;
	doorbell->retry_time = now + wait;
}

static int send_packet(struct doorbell *doorbell, const unsigned char *packet, int size, unsigned long now)
{
	if (size <= 0 || !doorbell->transport->write(doorbell->transport->context, doorbell->handle, packet, size))
		return 0;
	doorbell->sent_time = now;
	return 1;
}

int doorbell_want(struct doorbell *doorbell, const char *id)
{
	const char *wanted = id && doorbell_id_valid(id) ? id : "";

	if (!strcmp(doorbell->wanted, wanted))
		return 0;
	snprintf(doorbell->wanted, sizeof(doorbell->wanted), "%s", wanted);
	doorbell->refused = 0;
	doorbell->failures = 0;
	doorbell->retrying = 0;
	return 1;
}

static void open_connection(struct doorbell *doorbell, unsigned long now)
{
	unsigned char packet[128];
	char error[256];
	int size;

	doorbell->handle = doorbell->transport->open(doorbell->transport->context, doorbell->hosts[doorbell->broker],
		doorbell->ports[doorbell->broker], error, sizeof(error));
	if (doorbell->handle <= 0)
	{
		doorbell->handle = 0;
		failed(doorbell, now);
		return;
	}
	doorbell->state = _doorbell_connecting;
	doorbell->state_time = now;
	/* (the client ID is the doorbell: the brokers let a client read only
	the topic of its own ID) */
	size = doorbell_connect_packet(packet, sizeof(packet), doorbell->wanted, DOORBELL_KEEP_ALIVE_SECONDS);
	if (!send_packet(doorbell, packet, size, now))
		failed(doorbell, now);
}

/* one packet from the broker: its rings */
static int take_packet(struct doorbell *doorbell, const unsigned char *packet, int size, unsigned long now)
{
	int type = packet[0] >> 4;
	int at = 1;

	while (packet[at] & 0x80)
		at++;
	at++;
	if (type == 2 && doorbell->state == _doorbell_connecting)
	{
		/* CONNACK: its return code */
		unsigned char subscribe[192];
		char topic[sizeof(TOPIC_PREFIX) + DOORBELL_ID_SIZE];
		int length;

		if (size - at < 2 || packet[at + 1] != 0)
		{
			failed(doorbell, now);
			return 0;
		}
		snprintf(topic, sizeof(topic), TOPIC_PREFIX "%s", doorbell->wanted);
		length = doorbell_subscribe_packet(subscribe, sizeof(subscribe), 1, topic);
		doorbell->state = _doorbell_subscribing;
		doorbell->state_time = now;
		if (!send_packet(doorbell, subscribe, length, now))
			failed(doorbell, now);
	}
	else if (type == 9 && doorbell->state == _doorbell_subscribing)
	{
		/* SUBACK: 0x80, refused (not this client's topic, as far as the
		broker knows) */
		if (size - at < 3 || packet[at + 2] == 0x80)
		{
			close_connection(doorbell);
			doorbell->refused = 1;
			doorbell->news |= _doorbell_news_refused;
			doorbell->news_broker = doorbell->broker;
			return 0;
		}
		doorbell->state = _doorbell_listening;
		doorbell->state_time = now;
		doorbell->failures = 0;
		doorbell->retrying = 0;
		snprintf(doorbell->listening, sizeof(doorbell->listening), "%s", doorbell->wanted);
		doorbell->news |= _doorbell_news_listening;
		doorbell->news_broker = doorbell->broker;
	}
	else if (type == 3 && doorbell->state == _doorbell_listening)
	{
		/* PUBLISH: a ring if it is to this doorbell's topic */
		int qos = (packet[0] >> 1) & 3;
		int topic_length, prefix = (int)sizeof(TOPIC_PREFIX) - 1;

		if (size - at < 2)
			return 0;
		topic_length = packet[at] << 8 | packet[at + 1];
		at += 2;
		if (topic_length > size - at)
			return 0;
		if (topic_length != prefix + DOORBELL_ID_SIZE - 1 || memcmp(packet + at, TOPIC_PREFIX, (size_t)prefix) ||
			memcmp(packet + at + prefix, doorbell->listening, DOORBELL_ID_SIZE - 1))
			return 0;
		at += topic_length;
		if (qos)
			at += 2;
		doorbell->last_kind = at < size ? packet[at] : _doorbell_ring_any;
		return 1;
	}
	else if (type == 13)
	{
		/* PINGRESP */
		doorbell->ping_out = 0;
	}
	return 0;
}

int doorbell_step(struct doorbell *doorbell, unsigned long now, int wait)
{
	int rings = 0;

	if (!doorbell->wanted[0] || doorbell->refused || !doorbell->broker_count)
	{
		close_connection(doorbell);
		return 0;
	}
	if (doorbell->handle && doorbell->state == _doorbell_listening && strcmp(doorbell->listening, doorbell->wanted))
		close_connection(doorbell);
	if (!doorbell->handle)
	{
		if (doorbell->retrying && (long)(doorbell->retry_time - now) > 0)
			return 0;
		doorbell->retrying = 0;
		open_connection(doorbell, now);
		if (!doorbell->handle)
			return 0;
	}

	/* (what came) */
	{
		int count = doorbell->transport->read(doorbell->transport->context, doorbell->handle,
			doorbell->input + doorbell->input_size, (int)sizeof(doorbell->input) - doorbell->input_size, wait);

		if (count < 0)
		{
			failed(doorbell, now);
			return 0;
		}
		doorbell->input_size += count;
	}
	while (doorbell->handle && doorbell->input_size > 0)
	{
		int size = doorbell_packet_size(doorbell->input, doorbell->input_size);

		if (size < 0 || (size == 0 && doorbell->input_size >= (int)sizeof(doorbell->input)) ||
			size > (int)sizeof(doorbell->input))
		{
			/* (not MQTT, or more than a doorbell's broker sends) */
			failed(doorbell, now);
			return rings;
		}
		if (size == 0 || size > doorbell->input_size)
			break;
		rings += take_packet(doorbell, doorbell->input, size, now);
		if (!doorbell->handle)
			return rings;
		memmove(doorbell->input, doorbell->input + size, (size_t)(doorbell->input_size - size));
		doorbell->input_size -= size;
	}

	/* (the waits) */
	if (doorbell->state == _doorbell_connecting || doorbell->state == _doorbell_subscribing)
	{
		if (passed(now, doorbell->state_time, DOORBELL_ANSWER_WAIT))
			failed(doorbell, now);
	}
	else if (doorbell->state == _doorbell_listening)
	{
		if (doorbell->ping_out)
		{
			if (passed(now, doorbell->sent_time, DOORBELL_ANSWER_WAIT))
				failed(doorbell, now);
		}
		else if (passed(now, doorbell->sent_time, DOORBELL_PING_AFTER))
		{
			static const unsigned char ping[2] = { 0xC0, 0x00 };

			if (send_packet(doorbell, ping, sizeof(ping), now))
				doorbell->ping_out = 1;
			else
				failed(doorbell, now);
		}
	}
	return rings;
}

void doorbell_stop(struct doorbell *doorbell)
{
	if (doorbell->handle && doorbell->state != _doorbell_idle)
	{
		static const unsigned char disconnect[2] = { 0xE0, 0x00 };

		doorbell->transport->write(doorbell->transport->context, doorbell->handle, disconnect, sizeof(disconnect));
	}
	close_connection(doorbell);
}

int doorbell_take_news(struct doorbell *doorbell, const char **host)
{
	int news = doorbell->news;

	doorbell->news = 0;
	if (host)
		*host = doorbell->broker_count ? doorbell->hosts[doorbell->news_broker] : "";
	return news;
}
