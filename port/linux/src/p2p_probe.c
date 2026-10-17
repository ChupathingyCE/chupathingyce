/*
P2P_PROBE.C

Online Games' pings before joining (p2p_probe.h; docs/delta.md, "Pings
before joining"): the formats, the host's table of probes and the
prober's, without sockets, clocks or keys, so that the unit tests
(port/linux/tests/ping_test.c) run it as the game does.

The host keeps a probe only as an id in a small table, for a few seconds:
a probe never becomes a peer of the tunnel, and the game never hears of it.
It answers a PING only when the PING carries a live id, with a PONG of the
same size, a few times an id; new ids are taken from a budget (a burst,
then a few a second). So a spoofed PING gets nothing, and an answer is
never larger than what asked for it.

Times are the milliseconds of p2p_now: unsigned, compared by difference, as
the clock wraps.
*/

#include "p2p_probe.h"

#include <string.h>

static const char signed_label[] = "chupathingy probe";

static int since(unsigned long now, unsigned long then, unsigned long interval)
{
	return (unsigned int)(now - then) >= (unsigned int)interval;
}

static void put_addresses(unsigned char *message, const struct p2p_probe_address *addresses, int count)
{
	int index;

	message[0] = (unsigned char)count;
	for (index = 0; index < count; index++)
	{
		unsigned int address = (unsigned int)addresses[index].address;

		memcpy(message + 1 + index * 6, &address, 4);
		memcpy(message + 1 + index * 6 + 4, &addresses[index].port, 2);
	}
}

/* the addresses at message (size bytes left): the bytes read, or -1 */
static int get_addresses(const unsigned char *message, int size, struct p2p_probe_address *addresses, int *count)
{
	int index;

	if (size < 1)
		return -1;
	*count = message[0];
	if (*count > P2P_PROBE_MAXIMUM_ADDRESSES || size < 1 + *count * 6)
		return -1;
	for (index = 0; index < *count; index++)
	{
		unsigned int address;

		memcpy(&address, message + 1 + index * 6, 4);
		addresses[index].address = address;
		memcpy(&addresses[index].port, message + 1 + index * 6 + 4, 2);
	}
	return 1 + *count * 6;
}

/* ---------- formats */

int p2p_probe_put_packet(unsigned char *packet, int type, unsigned short sequence, const unsigned char *id,
	unsigned long clock)
{
	unsigned int stamp = (unsigned int)clock;

	packet[0] = P2P_PROBE_MAGIC;
	packet[1] = (unsigned char)type;
	packet[2] = (unsigned char)(sequence & 0xFF);
	packet[3] = (unsigned char)(sequence >> 8);
	memcpy(packet + 4, id, P2P_PROBE_ID_SIZE);
	packet[20] = (unsigned char)(stamp & 0xFF);
	packet[21] = (unsigned char)(stamp >> 8 & 0xFF);
	packet[22] = (unsigned char)(stamp >> 16 & 0xFF);
	packet[23] = (unsigned char)(stamp >> 24 & 0xFF);
	return P2P_PROBE_PACKET_SIZE;
}

int p2p_probe_get_packet(const unsigned char *packet, int size, int *type, unsigned short *sequence,
	unsigned char *id, unsigned long *clock)
{
	if (size != P2P_PROBE_PACKET_SIZE || packet[0] != P2P_PROBE_MAGIC ||
		(packet[1] != P2P_PROBE_PING && packet[1] != P2P_PROBE_PONG && packet[1] != P2P_PROBE_PUNCH))
	{
		return 0;
	}
	*type = packet[1];
	*sequence = (unsigned short)(packet[2] | packet[3] << 8);
	memcpy(id, packet + 4, P2P_PROBE_ID_SIZE);
	*clock = (unsigned long)((unsigned int)packet[20] | (unsigned int)packet[21] << 8 |
		(unsigned int)packet[22] << 16 | (unsigned int)packet[23] << 24);
	return 1;
}

int p2p_probe_put_request(unsigned char *message, const unsigned char *id, const unsigned char *reply,
	const struct p2p_probe_address *addresses, int count)
{
	int size = 0;

	if (count > P2P_PROBE_MAXIMUM_ADDRESSES)
		count = P2P_PROBE_MAXIMUM_ADDRESSES;
	if (count < 0)
		count = 0;
	message[size++] = P2P_PROBE_MESSAGE_REQUEST;
	message[size++] = P2P_PROBE_VERSION;
	memcpy(message + size, id, P2P_PROBE_ID_SIZE);
	size += P2P_PROBE_ID_SIZE;
	memcpy(message + size, reply, P2P_PROBE_ID_SIZE);
	size += P2P_PROBE_ID_SIZE;
	put_addresses(message + size, addresses, count);
	return size + 1 + count * 6;
}

int p2p_probe_get_request(const unsigned char *message, int size, unsigned char *id, unsigned char *reply,
	struct p2p_probe_address *addresses, int *count)
{
	int fixed = 2 + 2 * P2P_PROBE_ID_SIZE;

	if (size < fixed + 1 || message[0] != P2P_PROBE_MESSAGE_REQUEST || message[1] != P2P_PROBE_VERSION)
		return 0;
	memcpy(id, message + 2, P2P_PROBE_ID_SIZE);
	memcpy(reply, message + 2 + P2P_PROBE_ID_SIZE, P2P_PROBE_ID_SIZE);
	return get_addresses(message + fixed, size - fixed, addresses, count) > 0;
}

int p2p_probe_put_answer(unsigned char *message, const unsigned char *id, const unsigned char *key,
	const struct p2p_probe_address *addresses, int count)
{
	int size = 0;

	if (count > P2P_PROBE_MAXIMUM_ADDRESSES)
		count = P2P_PROBE_MAXIMUM_ADDRESSES;
	if (count < 0)
		count = 0;
	message[size++] = P2P_PROBE_MESSAGE_ANSWER;
	message[size++] = P2P_PROBE_VERSION;
	memcpy(message + size, id, P2P_PROBE_ID_SIZE);
	size += P2P_PROBE_ID_SIZE;
	memcpy(message + size, key, P2P_PROBE_KEY_SIZE);
	size += P2P_PROBE_KEY_SIZE;
	put_addresses(message + size, addresses, count);
	return size + 1 + count * 6;
}

int p2p_probe_signed_text(const unsigned char *message, int size, unsigned char *text)
{
	int label = (int)sizeof(signed_label) - 1;

	if (size < 0 || size > P2P_PROBE_MAXIMUM_MESSAGE)
		size = 0;
	memcpy(text, signed_label, (size_t)label);
	memcpy(text + label, message, (size_t)size);
	return label + size;
}

int p2p_probe_get_answer(const unsigned char *message, int size, unsigned char *id, unsigned char *key,
	struct p2p_probe_address *addresses, int *count, int *signed_size)
{
	int fixed = 2 + P2P_PROBE_ID_SIZE + P2P_PROBE_KEY_SIZE;
	int read;

	if (size < fixed + 1 + P2P_PROBE_SIGNATURE_SIZE || size > P2P_PROBE_MAXIMUM_MESSAGE ||
		message[0] != P2P_PROBE_MESSAGE_ANSWER || message[1] != P2P_PROBE_VERSION)
	{
		return 0;
	}
	memcpy(id, message + 2, P2P_PROBE_ID_SIZE);
	memcpy(key, message + 2 + P2P_PROBE_ID_SIZE, P2P_PROBE_KEY_SIZE);
	read = get_addresses(message + fixed, size - fixed - P2P_PROBE_SIGNATURE_SIZE, addresses, count);
	/* (the signature right after the addresses, and nothing more) */
	if (read < 0 || fixed + read + P2P_PROBE_SIGNATURE_SIZE != size)
		return 0;
	*signed_size = fixed + read;
	return 1;
}

/* ---------- the host */

static struct p2p_probe_host_slot *host_slot(struct p2p_probe_host *host, unsigned long now, const unsigned char *id)
{
	int index;

	for (index = 0; index < P2P_PROBE_HOST_SLOTS; index++)
	{
		struct p2p_probe_host_slot *slot = &host->slots[index];

		if (slot->used && since(now, slot->created, P2P_PROBE_HOST_LIFETIME))
			slot->used = 0;
		if (slot->used && !memcmp(slot->id, id, P2P_PROBE_ID_SIZE))
			return slot;
	}
	return NULL;
}

/* whether a new probe may be taken now (spending one if so) */
static int host_budget(struct p2p_probe_host *host, unsigned long now)
{
	unsigned int earned;

	if (!host->budget_started)
	{
		host->budget = P2P_PROBE_HOST_BURST;
		host->budget_time = now;
		host->budget_started = 1;
	}
	earned = (unsigned int)(now - host->budget_time) / P2P_PROBE_HOST_INTERVAL;
	if (earned)
	{
		if (host->budget + (long)earned >= P2P_PROBE_HOST_BURST)
		{
			host->budget = P2P_PROBE_HOST_BURST;
			host->budget_time = now;
		}
		else
		{
			host->budget += (int)earned;
			host->budget_time += earned * P2P_PROBE_HOST_INTERVAL;
		}
	}
	if (host->budget <= 0)
		return 0;
	host->budget--;
	return 1;
}

int p2p_probe_host_request(struct p2p_probe_host *host, unsigned long now, const unsigned char *id,
	const struct p2p_probe_address *addresses, int count)
{
	struct p2p_probe_host_slot *slot = host_slot(host, now, id);
	int index;

	if (slot)
	{
		/* (the prober's resend: answered again, sparingly, and nothing
		else changes: no new punches, no new PONGs) */
		if (!since(now, slot->answered, P2P_PROBE_HOST_ANSWER_INTERVAL))
			return 0;
		slot->answered = now;
		return 1;
	}
	for (index = 0; index < P2P_PROBE_HOST_SLOTS && host->slots[index].used; index++)
		;
	if (index == P2P_PROBE_HOST_SLOTS || count < 0 || count > P2P_PROBE_MAXIMUM_ADDRESSES || !host_budget(host, now))
	{
		host->refused++;
		return 0;
	}
	slot = &host->slots[index];
	memset(slot, 0, sizeof(*slot));
	slot->used = 1;
	memcpy(slot->id, id, P2P_PROBE_ID_SIZE);
	memcpy(slot->addresses, addresses, sizeof(*addresses) * (size_t)count);
	slot->count = count;
	slot->created = now;
	slot->answered = now;
	host->admitted++;
	return 1;
}

int p2p_probe_host_packet(struct p2p_probe_host *host, unsigned long now, const unsigned char *packet, int size,
	unsigned char *answer)
{
	unsigned char id[P2P_PROBE_ID_SIZE];
	struct p2p_probe_host_slot *slot;
	unsigned short sequence;
	unsigned long clock;
	int type;

	if (!p2p_probe_get_packet(packet, size, &type, &sequence, id, &clock) || type != P2P_PROBE_PING)
	{
		host->dropped++;
		return 0;
	}
	slot = host_slot(host, now, id);
	if (!slot || slot->pongs >= P2P_PROBE_HOST_PONGS)
	{
		host->dropped++;
		return 0;
	}
	slot->pongs++;
	/* (it got through: no more punching) */
	slot->pinged = 1;
	host->pongs++;
	/* the same bytes back, but the type */
	memcpy(answer, packet, P2P_PROBE_PACKET_SIZE);
	answer[1] = P2P_PROBE_PONG;
	return 1;
}

int p2p_probe_host_due(struct p2p_probe_host *host, unsigned long now, unsigned char (*packets)[P2P_PROBE_PACKET_SIZE],
	struct p2p_probe_address *to, int maximum)
{
	int count = 0;
	int index;

	for (index = 0; index < P2P_PROBE_HOST_SLOTS; index++)
	{
		struct p2p_probe_host_slot *slot = &host->slots[index];
		int address;

		if (slot->used && since(now, slot->created, P2P_PROBE_HOST_LIFETIME))
			slot->used = 0;
		if (!slot->used || slot->pinged || slot->punch_rounds >= P2P_PROBE_HOST_PUNCH_ROUNDS ||
			(slot->punch_rounds && !since(now, slot->punched, P2P_PROBE_HOST_PUNCH_INTERVAL)) ||
			count + slot->count > maximum)
		{
			continue;
		}
		for (address = 0; address < slot->count; address++)
		{
			p2p_probe_put_packet(packets[count], P2P_PROBE_PUNCH, (unsigned short)slot->punch_rounds, slot->id, 0);
			to[count++] = slot->addresses[address];
		}
		slot->punch_rounds++;
		slot->punched = now;
	}
	return count;
}

/* ---------- the prober */

struct p2p_probe_target *p2p_probe_target(struct p2p_probe_prober *prober, unsigned long now,
	const unsigned char *host_hash, const unsigned char *token, int want)
{
	struct p2p_probe_target *oldest = NULL;
	struct p2p_probe_target *target;
	int index;

	for (index = 0; index < P2P_PROBE_TARGETS; index++)
	{
		target = &prober->targets[index];
		if (target->used && !memcmp(target->host_hash, host_hash, P2P_PROBE_HASH_SIZE) &&
			!memcmp(target->token, token, P2P_PROBE_TOKEN_SIZE))
		{
			if (want)
				target->wanted_time = now;
			return target;
		}
	}
	if (!want)
		return NULL;
	for (index = 0; index < P2P_PROBE_TARGETS; index++)
	{
		target = &prober->targets[index];
		if (!target->used)
			break;
		/* (one being measured stays) */
		if ((target->state == P2P_PROBE_ASKING || target->state == P2P_PROBE_PINGING))
			continue;
		if (!oldest || (unsigned int)(now - target->wanted_time) > (unsigned int)(now - oldest->wanted_time))
			oldest = target;
	}
	if (index == P2P_PROBE_TARGETS)
	{
		if (!oldest)
			return NULL;
		target = oldest;
	}
	memset(target, 0, sizeof(*target));
	target->used = 1;
	memcpy(target->host_hash, host_hash, P2P_PROBE_HASH_SIZE);
	memcpy(target->token, token, P2P_PROBE_TOKEN_SIZE);
	target->state = P2P_PROBE_IDLE;
	target->wanted_time = now;
	target->best = -1;
	target->ping = -1;
	return target;
}

static void finish(struct p2p_probe_target *target, unsigned long now)
{
	if (target->pongs)
	{
		target->state = P2P_PROBE_MEASURED;
		target->ping = target->best;
	}
	else
	{
		target->state = P2P_PROBE_FAILED;
	}
	target->measured = now;
}

static int pings(struct p2p_probe_target *target, unsigned long now, unsigned char (*packets)[P2P_PROBE_PACKET_SIZE],
	struct p2p_probe_address *to, int maximum)
{
	int count = 0;
	int index;

	for (index = 0; index < target->count && count < maximum; index++)
	{
		p2p_probe_put_packet(packets[count], P2P_PROBE_PING, target->sequence, target->id, now);
		to[count++] = target->addresses[index];
	}
	target->sequence++;
	target->pinged = now;
	return count;
}

struct p2p_probe_target *p2p_probe_prober_next(struct p2p_probe_prober *prober, unsigned long now,
	const unsigned char *random, int *action, unsigned char (*packets)[P2P_PROBE_PACKET_SIZE],
	struct p2p_probe_address *to, int maximum, int *packet_count)
{
	int in_flight = 0;
	int index;

	*action = 0;
	*packet_count = 0;
	for (index = 0; index < P2P_PROBE_TARGETS; index++)
	{
		struct p2p_probe_target *target = &prober->targets[index];

		if (!target->used)
			continue;
		if (target->state == P2P_PROBE_ASKING)
		{
			if (since(now, target->started, P2P_PROBE_TIMEOUT))
			{
				finish(target, now);
				continue;
			}
			if (target->requests < P2P_PROBE_REQUESTS && since(now, target->requested, P2P_PROBE_REQUEST_INTERVAL))
			{
				target->requests++;
				target->requested = now;
				*action = 1;
				return target;
			}
			in_flight++;
		}
		else if (target->state == P2P_PROBE_PINGING)
		{
			if (target->pongs >= P2P_PROBE_PONGS_WANTED || since(now, target->started, P2P_PROBE_TIMEOUT))
			{
				finish(target, now);
				continue;
			}
			if (since(now, target->pinged, P2P_PROBE_PING_INTERVAL))
			{
				*packet_count = pings(target, now, packets, to, maximum);
				*action = 2;
				return target;
			}
			in_flight++;
		}
		else if (since(now, target->wanted_time, P2P_PROBE_FORGET))
		{
			target->used = 0;
		}
	}
	/* a new measurement: of a target asked about lately, never measured, or
	not for a while */
	for (index = 0; index < P2P_PROBE_TARGETS && in_flight < P2P_PROBE_IN_FLIGHT; index++)
	{
		struct p2p_probe_target *target = &prober->targets[index];

		if (!target->used || target->state == P2P_PROBE_ASKING || target->state == P2P_PROBE_PINGING ||
			since(now, target->wanted_time, P2P_PROBE_REQUEST_INTERVAL) ||
			(target->state != P2P_PROBE_IDLE && !since(now, target->measured, P2P_PROBE_REMEASURE)))
		{
			continue;
		}
		memcpy(target->id, random, P2P_PROBE_ID_SIZE);
		target->state = P2P_PROBE_ASKING;
		target->started = now;
		target->requested = now;
		target->requests = 1;
		target->pongs = 0;
		target->best = -1;
		target->count = 0;
		*action = 1;
		return target;
	}
	return NULL;
}

void p2p_probe_prober_answer(struct p2p_probe_prober *prober, unsigned long now, const unsigned char *id,
	const struct p2p_probe_address *addresses, int count)
{
	int index;

	if (count <= 0 || count > P2P_PROBE_MAXIMUM_ADDRESSES)
		return;
	for (index = 0; index < P2P_PROBE_TARGETS; index++)
	{
		struct p2p_probe_target *target = &prober->targets[index];

		if (target->used && target->state == P2P_PROBE_ASKING && !memcmp(target->id, id, P2P_PROBE_ID_SIZE))
		{
			memcpy(target->addresses, addresses, sizeof(*addresses) * (size_t)count);
			target->count = count;
			target->state = P2P_PROBE_PINGING;
			/* (its own time for the pings) */
			target->started = now;
			target->pinged = now - P2P_PROBE_PING_INTERVAL;
			return;
		}
	}
}

int p2p_probe_prober_packet(struct p2p_probe_prober *prober, unsigned long now, const unsigned char *packet,
	int size, unsigned long address, unsigned short port)
{
	unsigned char id[P2P_PROBE_ID_SIZE];
	unsigned short sequence;
	unsigned long clock;
	int type;
	int index;

	if (!p2p_probe_get_packet(packet, size, &type, &sequence, id, &clock) || type != P2P_PROBE_PONG)
		return 0;
	for (index = 0; index < P2P_PROBE_TARGETS; index++)
	{
		struct p2p_probe_target *target = &prober->targets[index];
		unsigned int round_trip;
		int known;

		if (!target->used || target->state != P2P_PROBE_PINGING || memcmp(target->id, id, P2P_PROBE_ID_SIZE))
			continue;
		/* (from an address the host's answer gave) */
		for (known = 0; known < target->count; known++)
		{
			if (target->addresses[known].address == address && target->addresses[known].port == port)
				break;
		}
		if (known == target->count)
			return 0;
		round_trip = (unsigned int)now - (unsigned int)clock;
		if (round_trip >= 10000)
			return 0;
		if (target->best < 0 || (long)round_trip < target->best)
			target->best = (long)round_trip;
		target->pongs++;
		if (target->pongs >= P2P_PROBE_PONGS_WANTED)
			finish(target, now);
		return 1;
	}
	return 0;
}
