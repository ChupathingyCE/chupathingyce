/*
CE_TRANSPORT_FUZZ.C

libFuzzer's entry for Halo Custom Edition 1.0.10's join wire
(server/platform/ce_transport.c, ce_messages.c), as tools/test_host_fuzz.py
builds it. Each input is a list of steps a client or the game takes against
a freshly started transport: [kind][length][bytes], the bytes of a step cut
at its length.

  0  a packet as it came (any bytes)
  1  a client's challenge, then its response, the handshake done as a real
     client does it (the server's answer worked out here), so what follows
     reaches the enciphered paths; the step's first byte picks the client
  2  a payload the client sealed and sent reliably (its length field set
     to match, so its messages are read)
  3  the same, unreliably
  4  the same, marked with FE FE
  5  one game message (the bytes: version first, type last) in a payload,
     reliably
  6  a join request with the bytes laid over a valid one's name and
     password, reliably
  7  the game's side: accept, refuse, send, updates, drop, events, a step
     of time, chosen by the bytes
*/

#include "../platform/ce_messages.h"
#include "../platform/ce_transport.h"

#include <math.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLIENTS 3

struct client
{
	struct sockaddr_in address;
	int connected;
	unsigned int key[4];
	unsigned int out_sequence;
	unsigned int in_sequence;
};

static struct client clients[CLIENTS];
static unsigned char last_packet[CE_PACKET_SIZE + 32];
static int last_length;
static unsigned char accept_packet[32];
static int accept_length;
static unsigned int now;

static void send_function(void *context, const void *address, const unsigned char *packet, int length)
{
	(void)context;
	(void)address;
	if (length > 0 && length <= (int)sizeof(last_packet))
	{
		memcpy(last_packet, packet, (size_t)length);
		last_length = length;
		if (length == 23 && packet[0] == 0xfe && packet[1] == 0xfe && packet[2] == 4)
		{
			memcpy(accept_packet, packet, 23);
			accept_length = 23;
		}
	}
}

static void notice_function(const char *format, const char *a)
{
	if (getenv("CE_FUZZ_VERBOSE"))
	{
		fprintf(stderr, format, a);
		fputc('\n', stderr);
	}
}

static void put_u16(unsigned char *out, unsigned int value)
{
	out[0] = (unsigned char)(value >> 8);
	out[1] = (unsigned char)value;
}

static void packet_from(int index, const unsigned char *packet, int length)
{
	ce_transport_packet(packet, length, &clients[index].address, now);
}

static int new_valid_challenge(unsigned char challenge[CE_CHALLENGE_SIZE], unsigned int seed)
{
	unsigned int parity = 0;
	int index;

	challenge[0] = (unsigned char)(0x21 + seed % 93);
	for (index = 1; index < CE_CHALLENGE_SIZE; index++)
	{
		unsigned int previous = challenge[index - 1];
		unsigned char character = (unsigned char)(0x21 + (seed * 31 + (unsigned int)index * 17) % 93);

		parity ^= (((previous ^ challenge[0]) ^ (unsigned int)index) & 1) ^ (previous < challenge[0] ? 1 : 0) ^
			(challenge[0] < 0x4f ? 1 : 0);
		if ((character & 1) != parity)
			character++;
		challenge[index] = character;
	}
	return ce_challenge_valid(challenge);
}

static void handshake(int index, unsigned int seed)
{
	struct client *client = &clients[index];
	unsigned char mine[CE_CHALLENGE_SIZE], theirs[CE_CHALLENGE_SIZE], answer[CE_CHALLENGE_SIZE];
	unsigned char packet[7 + 32 + 16 + 4];
	unsigned int exponent, server_value;

	new_valid_challenge(mine, seed);
	memset(packet, 0, sizeof(packet));
	packet[0] = packet[1] = 0xfe;
	packet[2] = 1;
	memcpy(packet + 7, mine, CE_CHALLENGE_SIZE);
	last_length = 0;
	packet_from(index, packet, 7 + CE_CHALLENGE_SIZE);
	if (last_length != 7 + 2 * CE_CHALLENGE_SIZE || last_packet[2] != 2)
		return;
	memcpy(theirs, last_packet + 7 + CE_CHALLENGE_SIZE, CE_CHALLENGE_SIZE);
	ce_challenge_response(theirs, answer);
	exponent = 2 + seed % 60000;
	memset(packet, 0, sizeof(packet));
	packet[0] = packet[1] = 0xfe;
	packet[2] = 3;
	put_u16(packet + 3, 1);
	put_u16(packet + 5, 1);
	memcpy(packet + 7, answer, CE_CHALLENGE_SIZE);
	{
		unsigned int value = ce_power(3, exponent);

		packet[7 + 32 + 13] = (unsigned char)(value >> 16);
		packet[7 + 32 + 14] = (unsigned char)(value >> 8);
		packet[7 + 32 + 15] = (unsigned char)value;
	}
	packet[7 + 32 + 16] = CE_CLIENT_VERSION & 255;
	packet[7 + 32 + 17] = (CE_CLIENT_VERSION >> 8) & 255;
	packet[7 + 32 + 18] = (CE_CLIENT_VERSION >> 16) & 255;
	packet[7 + 32 + 19] = (CE_CLIENT_VERSION >> 24) & 255;
	accept_length = 0;
	packet_from(index, packet, sizeof(packet));
	if (!accept_length)
		return;
	server_value = ((unsigned int)accept_packet[7 + 13] << 16) | ((unsigned int)accept_packet[7 + 14] << 8) | accept_packet[7 + 15];
	ce_key_from_secret(ce_power(server_value, exponent), client->key);
	client->connected = 1;
	client->out_sequence = 2;
	client->in_sequence = 2;
}

/* a payload (11 bits of length first) from raw bytes */
static int payload_from_bytes(const unsigned char *data, int length, unsigned char *out, int size)
{
	int total = length < 2 ? 2 : length;

	if (total > 0x7ff || total > size)
		return 0;
	memset(out, 0, (size_t)total);
	memcpy(out, data, (size_t)length);
	out[0] = (unsigned char)total;
	out[1] = (unsigned char)((out[1] & 0xf8) | (total >> 8));
	return total;
}

static void send_payload(int index, const unsigned char *payload, int length, int mode)
{
	struct client *client = &clients[index];
	unsigned char packet[CE_PACKET_SIZE + 32];
	int sealed;

	if (!client->connected || length <= 0)
		return;
	if (mode == 0)
	{
		sealed = ce_seal(client->key, payload, length, packet + 7, CE_PACKET_SIZE);
		if (!sealed)
			return;
		packet[0] = packet[1] = 0xfe;
		packet[2] = 0;
		put_u16(packet + 3, client->out_sequence);
		put_u16(packet + 5, client->in_sequence);
		client->out_sequence = (client->out_sequence + 1) & 0xffff;
		packet_from(index, packet, 7 + sealed);
	}
	else
	{
		sealed = ce_seal(client->key, payload, length, packet + 2, CE_PACKET_SIZE);
		if (!sealed)
			return;
		if (mode == 2)
		{
			packet[0] = packet[1] = 0xfe;
			packet_from(index, packet, 2 + sealed);
		}
		else
			packet_from(index, packet + 2, sealed);
	}
}

/* what the game plays from a client's update must be numbers it can turn by */
static void check_update(const struct ce_client_update *update)
{
	if (!isfinite(update->yaw) || !isfinite(update->pitch) || fabsf(update->yaw) > 1000.0f || fabsf(update->pitch) > 1000.0f)
		abort();
	if (update->ticks < 0 || update->ticks > 31 || update->throttle < 0 || update->throttle > 15)
		abort();
}

static void game_side(const unsigned char *data, int length)
{
	struct ce_event event;
	struct ce_client_update update;
	unsigned char payload[256];
	int action = length > 0 ? data[0] % 9 : 0;
	int peer = length > 1 ? (int)(data[1] % 18) - 1 : 0;
	int rest = length > 2 ? length - 2 : 0;

	switch (action)
	{
	case 0:
		ce_transport_accept(peer, data + 2, rest);
		break;
	case 1:
		ce_transport_refuse(peer, length > 2 ? data[2] : 0);
		break;
	case 2:
		ce_transport_send(peer, data + 2, rest, length > 2 ? data[2] & 1 : 0);
		break;
	case 3:
		if (ce_transport_client_update(peer, &update))
			check_update(&update);
		break;
	case 4:
		if (ce_transport_next_update(peer, &update))
			check_update(&update);
		break;
	case 5:
		ce_transport_drop(peer);
		break;
	case 6:
		while (ce_transport_next_event(&event))
			;
		break;
	case 7:
	{
		/* (a real first payload: the game's own, a keep-alive, so the client's
		sealed payloads after it reach the game's paths) */
		struct ce_payload built;
		int built_length;

		ce_payload_begin(&built, payload, sizeof(payload));
		ce_payload_put_server_keep_alive(&built, 0, 0);
		built_length = ce_payload_end(&built);
		ce_transport_accept(peer, payload, built_length);
		break;
	}
	default:
		now += rest ? (unsigned int)data[2] * 40u : 1000u;
		ce_transport_update(now);
		break;
	}
}

static void join_message(const unsigned char *data, int length, unsigned char message[146])
{
	int index;

	memset(message, 0, 146);
	message[0] = 1;
	memcpy(message + 1, "message in a bot", 16);
	for (index = 0; index < 9; index++)
		put_u16(message + 17 + index * 2, index < 3 ? 'a' + (unsigned)index : 0);
	memcpy(message + 35, "0123456789abcdef0123456789abcdef", 32);
	message[108] = 1;
	for (index = 0; index < 12; index++)
		put_u16(message + 109 + index * 2, index < 6 ? 'A' + (unsigned)index : 0);
	message[145] = CE_MESSAGE_JOIN_GAME_REQUEST;
	if (length > 0)
		memcpy(message + 17, data, (size_t)(length > 128 ? 128 : length));
	message[0] = 1;
	message[145] = CE_MESSAGE_JOIN_GAME_REQUEST;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	size_t position = 0;
	int index;

	memset(clients, 0, sizeof(clients));
	for (index = 0; index < CLIENTS; index++)
	{
		clients[index].address.sin_family = AF_INET;
		clients[index].address.sin_addr.s_addr = 0x0100007fu + (unsigned int)index * 0x01000000u;
		clients[index].address.sin_port = (unsigned short)(2400 + index);
	}
	now = 1000;
	ce_transport_start(send_function, NULL, notice_function);
	ce_transport_set_game("fuzz", "bloodgulch", 16);
	ce_transport_set_accept(size && (data[0] & 1));
	if (size)
		position = 1;
	while (position + 2 <= size)
	{
		int kind = data[position] % 8;
		size_t length = data[position + 1];
		const uint8_t *step;
		unsigned char payload[CE_PACKET_SIZE];
		unsigned char message[146];
		int payload_length, client;

		position += 2;
		if (length > size - position)
			length = size - position;
		step = data + position;
		position += length;
		client = length ? step[0] % CLIENTS : 0;
		now += 10;
		switch (kind)
		{
		case 0:
			packet_from(client, step, (int)length);
			break;
		case 1:
			handshake(client, length > 1 ? step[1] : 1);
			break;
		case 2:
		case 3:
		case 4:
			payload_length = payload_from_bytes(step + (length ? 1 : 0), (int)length - (length ? 1 : 0), payload, sizeof(payload));
			send_payload(client, payload, payload_length, kind - 2);
			break;
		case 5:
			payload_length = ce_payload_from_message(step + (length ? 1 : 0), (int)length - (length ? 1 : 0), payload, sizeof(payload));
			send_payload(client, payload, payload_length, 0);
			break;
		case 6:
			join_message(step + (length ? 1 : 0), (int)length - (length ? 1 : 0), message);
			payload_length = ce_payload_from_message(message, 146, payload, sizeof(payload));
			send_payload(client, payload, payload_length, 0);
			break;
		default:
			game_side(step, (int)length);
			break;
		}
	}
	ce_transport_stop();
	return 0;
}
