/*
CE_TRANSPORT.C

Halo Custom Edition 1.0.10's join wire (ce_transport.h), written from our
own captures of its client and dedicated server and from reading the
retail binaries' behavior. No other project's code.

One UDP port carries everything, GameSpy Transport 2 style:
  - control packets start FE FE <type>: 01 the client's challenge (32
    characters), 02 the server's answer to it and its own challenge, 03
    the client's answer, its key agreement value (16 bytes, big-endian) and
    its connect message (the version, 616999, 4 bytes little-endian), 04
    the server's acceptance with its own value, 00 a reliable message
    (FE FE 00 <sequence:2> <next expected:2> <payload>, big-endian), 64 an
    acknowledgement (<next expected:2>), 68 closed. The handshake packets
    count in the sequence: 01 and 02 are each side's 0, 03 and 04 their 1;
  - anything else from a connected client is an unreliable payload (one
    that would start FE FE is sent as FE FE FE FE ...).
  - a challenge is 32 characters 0x21 to 0x7e with a parity rule; its
    answer is worked from it and a fixed key, character by character,
    except positions 0 and 13 (anything);
  - the key agreement is Diffie-Hellman modulo 65537, base 3; the secret, as
    16 big-endian bytes, is the key of a 32-round TEA (little-endian words)
    over each payload in 8-byte blocks, a last partial block covered by
    enciphering the last 8 bytes again, a payload under 8 bytes as it is.
    A CRC32 (reflected, no final inversion; little-endian) ends each
    payload before it is enciphered.

A payload is a bit stream, least significant bit first: 11 bits of its
length in bytes, then messages, each a bit 0 and a 16-bit header (4 bits
of flags, 0xc, and the message's size with the header in bytes), then the
message's bytes (a version byte, its fields, its type last), or a bit 1
and the game's other stream (state and object updates; not read here).
Fields are big-endian.

A connection goes: handshake, the server's new client challenge (type 4:
the challenge and the map's name), the client's join request (type 14),
with its word that it has the map (type 21) when the map is one of its
stock maps, then, for now, machine rejected (type 6) with a reason, and the
connection closed.

Nothing here logs an address.
*/

#include "ce_transport.h"
#include "ce_messages.h"

/* (the sockets' addresses, on Linux and Windows) */
#include "server_host.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define getpid _getpid
#endif

enum
{
	TYPE_DATA = 0x00,
	TYPE_CLIENT_CHALLENGE = 0x01,
	TYPE_SERVER_CHALLENGE = 0x02,
	TYPE_CLIENT_RESPONSE = 0x03,
	TYPE_ACCEPT = 0x04,
	TYPE_ACK = 0x64,
	TYPE_CLOSED = 0x68,

	MESSAGE_NEW_CLIENT_CHALLENGE = 4,
	MESSAGE_MACHINE_REJECTED = 6,
	MESSAGE_JOIN_GAME_REQUEST = 14,
	MESSAGE_MAP_IS_PRECACHED = 21,
	MESSAGE_VERSION = 1,
	/* a game message's header flags */
	MESSAGE_FLAGS = 0xc,

	MODULUS = 65537,
	GENERATOR = 3,

	/* client updates kept for the game, at most (a client sends at most 64
	before one is taken) */
	UPDATE_QUEUE = 64,
	/* the game's answer to a join, at most this late (a client gives up
	on an unanswered join after about 15 seconds; the game waits up to 12
	for a game to start, server_ce.c) */
	AWAITING_MILLISECONDS = 14000,
	/* the server's keep-alive to a client in the game */
	KEEP_ALIVE_MILLISECONDS = 3000,
	/* reliable messages not yet acknowledged, at most, and their resends */
	OUTGOING = 8,
	RESEND_MILLISECONDS = 1000,
	/* a connection with nothing heard from it this long is dropped */
	IDLE_MILLISECONDS = 30000,
	/* and a refused one this long after the refusal */
	REFUSED_MILLISECONDS = 5000,
};

enum
{
	PEER_FREE,
	/* the server's challenge sent */
	PEER_CHALLENGED,
	/* accepted: messages flow */
	PEER_CONNECTED,
	/* the join refused: closed once acknowledged */
	PEER_REFUSED,
	/* the join accepted (only while testing: ce_transport_set_accept) */
	PEER_ACCEPTED,
};

static const char transport_key[] = "3b8dd8995f7c40a9a5c5b7dd5b481341";
static const char join_protocol[] = "message in a bot";

/* every public function holds it: the listing's thread and the game's both call in */
static pthread_mutex_t transport_mutex = PTHREAD_MUTEX_INITIALIZER;

struct outgoing
{
	int used;
	unsigned int sequence;
	unsigned int sent_time;
	int length;
	unsigned char packet[CE_PACKET_SIZE + 16];
};

struct peer
{
	int state;
	struct sockaddr_in address;
	unsigned int heard_time;
	unsigned int refused_time;
	unsigned char challenge[CE_CHALLENGE_SIZE];
	unsigned char expected[CE_CHALLENGE_SIZE];
	/* the server's handshake packet, sent again when the client repeats its own */
	unsigned char handshake[96];
	int handshake_length;
	unsigned int exponent;
	unsigned int key[4];
	/* the next sequence to send, and the next one expected */
	unsigned int out_sequence;
	unsigned int in_sequence;
	struct outgoing outgoing[OUTGOING];
	int joined;
	int precached;
	int loaded;
	unsigned int keep_alive_time;
	/* the game was told of the join (events), and when */
	int game_known;
	int awaiting;
	unsigned int awaiting_time;
	/* the client's actions so far (its update deltas) */
	struct ce_client_update update;
	int updates;
	/* each update as it came, for the game to play in order */
	struct ce_client_update queue[UPDATE_QUEUE];
	int queue_first;
	int queue_count;
	struct ce_join_request request;
};

static struct
{
	ce_send_function send;
	void *context;
	ce_notice_function notice;
	char map[CE_TEXT_SIZE];
	char name[CE_TEXT_SIZE];
	int maximum_players;
	int accept;
	unsigned int random;
	struct ce_event events[CE_EVENTS];
	int event_first;
	int event_count;
	unsigned int now;
	struct peer peers[CE_MAXIMUM_PEERS];
} transport;

/* ---------- small things */

static unsigned int next_random(void)
{
	/* (xorshift; challenges and exponents need no more) */
	unsigned int x = transport.random ? transport.random : 0x9e3779b9u;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	transport.random = x;
	return x;
}

static unsigned char random_character(void)
{
	return (unsigned char)(next_random() % 93 + 0x21);
}

static void say(const char *format, const char *a)
{
	if (transport.notice)
		transport.notice(format, a);
}

static unsigned int read_u16(const unsigned char *data)
{
	return ((unsigned int)data[0] << 8) | data[1];
}

static void write_u16(unsigned char *data, unsigned int value)
{
	data[0] = (unsigned char)(value >> 8);
	data[1] = (unsigned char)value;
}

/* ---------- the challenge */

int ce_challenge_valid(const unsigned char challenge[CE_CHALLENGE_SIZE])
{
	unsigned int first = challenge[0];
	unsigned int parity = 0;
	int index;

	for (index = 1; index < CE_CHALLENGE_SIZE; index++)
	{
		unsigned int previous = challenge[index - 1];

		parity ^= (((previous ^ first) ^ (unsigned int)index) & 1) ^ (previous < first ? 1 : 0) ^ (first < 0x4f ? 1 : 0);
		if ((challenge[index] & 1) != parity)
			return 0;
	}
	return 1;
}

static void new_challenge(unsigned char challenge[CE_CHALLENGE_SIZE])
{
	unsigned int parity = 0;
	int index;

	challenge[0] = random_character();
	for (index = 1; index < CE_CHALLENGE_SIZE; index++)
	{
		unsigned int previous = challenge[index - 1];
		unsigned char character = random_character();

		parity ^= (((previous ^ challenge[0]) ^ (unsigned int)index) & 1) ^ (previous < challenge[0] ? 1 : 0) ^
			(challenge[0] < 0x4f ? 1 : 0);
		if ((character & 1) != parity)
			character++;
		challenge[index] = character;
	}
}

void ce_challenge_response(const unsigned char challenge[CE_CHALLENGE_SIZE], unsigned char response[CE_CHALLENGE_SIZE])
{
	int key_length = (int)strlen(transport_key);
	int valid = ce_challenge_valid(challenge);
	int index;

	for (index = 0; index < CE_CHALLENGE_SIZE; index++)
	{
		int earlier, picked, value;

		if (!valid || index == 0 || index == 13)
		{
			response[index] = random_character();
			continue;
		}
		earlier = (index == 1 || index == 14) ? challenge[index] : challenge[index - 1];
		picked = (transport_key[(challenge[index] + index) % key_length] + challenge[index] * index) & 31;
		value = challenge[picked] ^ transport_key[(earlier * index * 0x4647) % key_length];
		if (value < 0)
			value = -value;
		response[index] = (unsigned char)(value % 93 + 0x21);
	}
}

int ce_response_matches(const unsigned char expected[CE_CHALLENGE_SIZE], const unsigned char response[CE_CHALLENGE_SIZE])
{
	int index;

	for (index = 0; index < CE_CHALLENGE_SIZE; index++)
	{
		if (index != 0 && index != 13 && expected[index] != response[index])
			return 0;
	}
	return 1;
}

/* ---------- the key and the cipher */

unsigned int ce_power(unsigned int base, unsigned int exponent)
{
	unsigned long long result = 1;
	unsigned long long square = base % MODULUS;

	while (exponent)
	{
		if (exponent & 1)
			result = result * square % MODULUS;
		square = square * square % MODULUS;
		exponent >>= 1;
	}
	return (unsigned int)result;
}

void ce_key_from_secret(unsigned int secret, unsigned int key[4])
{
	unsigned char bytes[16];
	int index;

	memset(bytes, 0, sizeof(bytes));
	bytes[12] = (unsigned char)(secret >> 24);
	bytes[13] = (unsigned char)(secret >> 16);
	bytes[14] = (unsigned char)(secret >> 8);
	bytes[15] = (unsigned char)secret;
	for (index = 0; index < 4; index++)
	{
		key[index] = (unsigned int)bytes[index * 4] | ((unsigned int)bytes[index * 4 + 1] << 8) |
			((unsigned int)bytes[index * 4 + 2] << 16) | ((unsigned int)bytes[index * 4 + 3] << 24);
	}
}

static unsigned int read_u32_le(const unsigned char *data)
{
	return (unsigned int)data[0] | ((unsigned int)data[1] << 8) | ((unsigned int)data[2] << 16) |
		((unsigned int)data[3] << 24);
}

static void write_u32_le(unsigned char *data, unsigned int value)
{
	data[0] = (unsigned char)value;
	data[1] = (unsigned char)(value >> 8);
	data[2] = (unsigned char)(value >> 16);
	data[3] = (unsigned char)(value >> 24);
}

static void encipher_block(const unsigned int key[4], unsigned char *block)
{
	unsigned int v0 = read_u32_le(block), v1 = read_u32_le(block + 4);
	unsigned int sum = 0;
	int round;

	for (round = 0; round < 32; round++)
	{
		sum += 0x9e3779b9u;
		v0 += ((v1 << 4) + key[0]) ^ (v1 + sum) ^ ((v1 >> 5) + key[1]);
		v1 += ((v0 << 4) + key[2]) ^ (v0 + sum) ^ ((v0 >> 5) + key[3]);
	}
	write_u32_le(block, v0);
	write_u32_le(block + 4, v1);
}

static void decipher_block(const unsigned int key[4], unsigned char *block)
{
	unsigned int v0 = read_u32_le(block), v1 = read_u32_le(block + 4);
	unsigned int sum = 0xc6ef3720u;
	int round;

	for (round = 0; round < 32; round++)
	{
		v1 -= ((v0 << 4) + key[2]) ^ (v0 + sum) ^ ((v0 >> 5) + key[3]);
		v0 -= ((v1 << 4) + key[0]) ^ (v1 + sum) ^ ((v1 >> 5) + key[1]);
		sum -= 0x9e3779b9u;
	}
	write_u32_le(block, v0);
	write_u32_le(block + 4, v1);
}

unsigned int ce_crc32(const unsigned char *data, int length)
{
	unsigned int crc = 0xffffffffu;
	int index, bit;

	for (index = 0; index < length; index++)
	{
		crc ^= data[index];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
	}
	return crc;
}

int ce_seal(const unsigned int key[4], const unsigned char *payload, int length, unsigned char *out, int size)
{
	int total = length + 4;
	int offset;

	if (length < 0 || total > size)
		return 0;
	memcpy(out, payload, (size_t)length);
	write_u32_le(out + length, ce_crc32(payload, length));
	if (total >= 8)
	{
		for (offset = 0; offset + 8 <= total; offset += 8)
			encipher_block(key, out + offset);
		if (total % 8)
			encipher_block(key, out + total - 8);
	}
	return total;
}

int ce_open(const unsigned int key[4], const unsigned char *data, int length, unsigned char *out, int size)
{
	int offset;

	if (length < 4 || length > size)
		return -1;
	memcpy(out, data, (size_t)length);
	if (length >= 8)
	{
		if (length % 8)
			decipher_block(key, out + length - 8);
		for (offset = 0; offset + 8 <= length; offset += 8)
			decipher_block(key, out + offset);
	}
	if (read_u32_le(out + length - 4) != ce_crc32(out, length - 4))
		return -1;
	return length - 4;
}

/* ---------- the bit stream */

struct bits
{
	unsigned char *data;
	int size;
	int position;
};

static int read_bits(struct bits *bits, int count, unsigned int *value)
{
	int index;

	if (bits->position + count > bits->size * 8)
		return 0;
	*value = 0;
	for (index = 0; index < count; index++, bits->position++)
		*value |= (unsigned int)((bits->data[bits->position >> 3] >> (bits->position & 7)) & 1) << index;
	return 1;
}

static int write_bits(struct bits *bits, int count, unsigned int value)
{
	int index;

	if (bits->position + count > bits->size * 8)
		return 0;
	for (index = 0; index < count; index++, bits->position++)
	{
		unsigned char mask = (unsigned char)(1 << (bits->position & 7));

		if ((value >> index) & 1)
			bits->data[bits->position >> 3] |= mask;
		else
			bits->data[bits->position >> 3] &= (unsigned char)~mask;
	}
	return 1;
}

int ce_payload_messages(const unsigned char *payload, int length,
	void (*visit)(void *context, const unsigned char *message, int message_length), void *context)
{
	struct bits bits;
	unsigned int total, kind, header;
	int count = 0;

	bits.data = (unsigned char *)payload;
	bits.size = length;
	bits.position = 0;
	if (!read_bits(&bits, 11, &total) || (int)total != length)
		return -1;
	while (bits.size * 8 - bits.position >= 17)
	{
		unsigned char message[CE_PACKET_SIZE];
		int size, index;

		read_bits(&bits, 1, &kind);
		/* (the other stream runs to the payload's end) */
		if (kind)
			break;
		read_bits(&bits, 16, &header);
		size = (int)(header >> 4) - 2;
		if (size < 2 || size > (int)sizeof(message) || bits.position + size * 8 > bits.size * 8)
			return -1;
		for (index = 0; index < size; index++)
		{
			unsigned int byte;

			read_bits(&bits, 8, &byte);
			message[index] = (unsigned char)byte;
		}
		if (visit)
			visit(context, message, size);
		count++;
	}
	return count;
}

int ce_payload_from_message(const unsigned char *message, int message_length, unsigned char *out, int size)
{
	struct bits bits;
	int total_bits = 11 + 1 + 16 + message_length * 8;
	int total = (total_bits + 7) / 8;
	int index;

	if (message_length < 2 || message_length + 2 > 0xfff || total > size || total > 0x7ff)
		return 0;
	memset(out, 0, (size_t)total);
	bits.data = out;
	bits.size = total;
	bits.position = 0;
	write_bits(&bits, 11, (unsigned int)total);
	write_bits(&bits, 1, 0);
	write_bits(&bits, 16, MESSAGE_FLAGS | ((unsigned int)(message_length + 2) << 4));
	for (index = 0; index < message_length; index++)
		write_bits(&bits, 8, message[index]);
	return total;
}

/* ---------- messages */

int ce_message_new_client_challenge(const char *challenge, const char *map, unsigned char *out, int size)
{
	int map_length = (int)strnlen(map, 127);
	int length = 1 + 8 + 1 + 4 + 4 + map_length + 1 + 1;

	if (length > size)
		return 0;
	memset(out, 0, (size_t)length);
	out[0] = MESSAGE_VERSION;
	/* (8 bytes: the challenge's text and a NUL; then a byte, two longs) */
	memcpy(out + 1, challenge, strnlen(challenge, 7));
	/* (the byte after: 1 asks the client for the compact motion (the
	retail client's 0x4c6460: 16-bit velocities); Custom Edition's own
	server sends 0, the full motion ce_messages.c writes) */
	out[9] = 0;
	memcpy(out + 18, map, (size_t)map_length);
	out[length - 1] = MESSAGE_NEW_CLIENT_CHALLENGE;
	return length;
}

int ce_message_machine_rejected(int reason, unsigned char *out, int size)
{
	if (size < 6)
		return 0;
	out[0] = MESSAGE_VERSION;
	out[1] = (unsigned char)((unsigned int)reason >> 24);
	out[2] = (unsigned char)((unsigned int)reason >> 16);
	out[3] = (unsigned char)((unsigned int)reason >> 8);
	out[4] = (unsigned char)reason;
	out[5] = MESSAGE_MACHINE_REJECTED;
	return 6;
}

/* big-endian 16-bit characters as ASCII */
static void wide_text(const unsigned char *data, int count, char *out)
{
	int index;

	for (index = 0; index < count; index++)
	{
		unsigned int character = read_u16(data + index * 2);

		if (!character)
			break;
		out[index] = character >= 0x20 && character < 0x7f ? (char)character : '?';
	}
	out[index] = 0;
}

int ce_parse_join_request(const unsigned char *message, int length, struct ce_join_request *request)
{
	int index;

	/* version 1: protocol 16, password 9 shorts, CD key 73, a byte, name 12
	shorts, 2 shorts, 4 bytes, the answer (a long), then the type */
	memset(request, 0, sizeof(*request));
	if (length != 146 || message[0] != MESSAGE_VERSION || message[length - 1] != MESSAGE_JOIN_GAME_REQUEST)
		return 0;
	memcpy(request->protocol, message + 1, 16);
	request->protocol[16] = 0;
	wide_text(message + 17, 9, request->password);
	request->password[9] = 0;
	for (index = 0; index < 32 && message[35 + index]; index++)
	{
		char character = (char)message[35 + index];

		request->cdkey_hash[index] = ((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
			(character >= 'A' && character <= 'F')) ? character : '?';
	}
	request->cdkey_hash[index] = 0;
	wide_text(message + 109, 12, request->name);
	request->answer = ((unsigned int)message[141] << 24) | ((unsigned int)message[142] << 16) |
		((unsigned int)message[143] << 8) | message[144];
	return 1;
}

/* ---------- connections */

static struct peer *find_peer(const struct sockaddr_in *address)
{
	int index;

	for (index = 0; index < CE_MAXIMUM_PEERS; index++)
	{
		struct peer *peer = &transport.peers[index];

		if (peer->state != PEER_FREE && peer->address.sin_addr.s_addr == address->sin_addr.s_addr &&
			peer->address.sin_port == address->sin_port)
		{
			return peer;
		}
	}
	return NULL;
}

static void send_raw(struct peer *peer, const unsigned char *packet, int length)
{
	if (transport.send)
		transport.send(transport.context, &peer->address, packet, length);
}

static void send_control(struct peer *peer, int type)
{
	unsigned char packet[5];

	packet[0] = 0xfe;
	packet[1] = 0xfe;
	packet[2] = (unsigned char)type;
	if (type == TYPE_ACK)
	{
		write_u16(packet + 3, peer->in_sequence);
		send_raw(peer, packet, 5);
	}
	else
		send_raw(peer, packet, 3);
}

/* a payload, reliably */
static int send_payload(struct peer *peer, const unsigned char *payload, int payload_length, unsigned int now)
{
	struct outgoing *slot = NULL;
	int sealed, index;

	for (index = 0; index < OUTGOING; index++)
	{
		if (!peer->outgoing[index].used)
		{
			slot = &peer->outgoing[index];
			break;
		}
	}
	if (!slot || payload_length <= 0)
		return 0;
	sealed = ce_seal(peer->key, payload, payload_length, slot->packet + 7, CE_PACKET_SIZE);
	if (!sealed)
		return 0;
	slot->packet[0] = 0xfe;
	slot->packet[1] = 0xfe;
	slot->packet[2] = TYPE_DATA;
	write_u16(slot->packet + 3, peer->out_sequence);
	write_u16(slot->packet + 5, peer->in_sequence);
	slot->length = 7 + sealed;
	slot->sequence = peer->out_sequence;
	slot->sent_time = now;
	slot->used = 1;
	peer->out_sequence = (peer->out_sequence + 1) & 0xffff;
	send_raw(peer, slot->packet, slot->length);
	return 1;
}

/* a payload, unreliably */
static void send_unreliable(struct peer *peer, const unsigned char *payload, int payload_length)
{
	unsigned char packet[CE_PACKET_SIZE + 16];
	int sealed;

	if (payload_length <= 0)
		return;
	sealed = ce_seal(peer->key, payload, payload_length, packet + 2, CE_PACKET_SIZE);
	if (!sealed)
		return;
	/* (one that would start FE FE is marked with FE FE first) */
	if (packet[2] == 0xfe && packet[3] == 0xfe)
	{
		packet[0] = 0xfe;
		packet[1] = 0xfe;
		send_raw(peer, packet, sealed + 2);
	}
	else
		send_raw(peer, packet + 2, sealed);
}

static void send_keep_alive(struct peer *peer, unsigned int now)
{
	unsigned char out[32];
	struct ce_payload payload;

	peer->keep_alive_time = now;
	ce_payload_begin(&payload, out, sizeof(out));
	ce_payload_put_server_keep_alive(&payload, 0, 0);
	send_unreliable(peer, out, ce_payload_end(&payload));
}

/* a game message, reliably */
static int send_message(struct peer *peer, const unsigned char *message, int length, unsigned int now)
{
	unsigned char payload[CE_PACKET_SIZE];

	return send_payload(peer, payload, ce_payload_from_message(message, length, payload, sizeof(payload)), now);
}

static void queue_update(void *context, const struct ce_client_update *update)
{
	struct peer *peer = context;

	if (peer->queue_count == UPDATE_QUEUE)
	{
		peer->queue_first = (peer->queue_first + 1) % UPDATE_QUEUE;
		peer->queue_count--;
	}
	peer->queue[(peer->queue_first + peer->queue_count) % UPDATE_QUEUE] = *update;
	peer->queue_count++;
}

static int peer_number(const struct peer *peer);

static void push_event(const struct peer *peer, int type)
{
	struct ce_event *event;

	if (transport.event_count == CE_EVENTS)
	{
		transport.event_first = (transport.event_first + 1) % CE_EVENTS;
		transport.event_count--;
	}
	event = &transport.events[(transport.event_first + transport.event_count) % CE_EVENTS];
	event->peer = peer_number(peer);
	event->type = type;
	snprintf(event->name, sizeof(event->name), "%s", peer->request.name);
	event->address = ntohl(peer->address.sin_addr.s_addr);
	transport.event_count++;
}

static int peer_number(const struct peer *peer)
{
	return (int)(peer - transport.peers);
}

static void forget(struct peer *peer)
{
	if (peer->game_known)
		push_event(peer, CE_EVENT_LEFT);
	memset(peer, 0, sizeof(*peer));
	peer->state = PEER_FREE;
}

static void acknowledged(struct peer *peer, unsigned int next_expected)
{
	int index;

	for (index = 0; index < OUTGOING; index++)
	{
		struct outgoing *slot = &peer->outgoing[index];

		/* (a sequence before the next expected one, allowing for the wrap) */
		if (slot->used && ((next_expected - slot->sequence - 1) & 0xffff) < 0x8000)
			slot->used = 0;
	}
}

static int all_acknowledged(const struct peer *peer)
{
	int index;

	for (index = 0; index < OUTGOING; index++)
	{
		if (peer->outgoing[index].used)
			return 0;
	}
	return 1;
}

static void refuse(struct peer *peer, int reason, unsigned int now)
{
	unsigned char message[8];
	int length = ce_message_machine_rejected(reason, message, sizeof(message));

	send_message(peer, message, length, now);
	peer->state = PEER_REFUSED;
	peer->refused_time = now;
}

static void message_received(void *context, const unsigned char *message, int length)
{
	struct peer *peer = context;
	int type = message[length - 1];

	if (type == MESSAGE_JOIN_GAME_REQUEST && !peer->joined)
	{
		char line[CE_TEXT_SIZE * 2];

		if (!ce_parse_join_request(message, length, &peer->request))
		{
			say("ce: a Custom Edition join request could not be read%s", "");
			return;
		}
		peer->joined = 1;
		snprintf(line, sizeof(line), "\"%s\"%s", peer->request.name,
			strcmp(peer->request.protocol, join_protocol) ? " (an unknown protocol)" : "");
		say("ce: a Custom Edition player asks to join: %s", line);
	}
	else if (type == MESSAGE_MAP_IS_PRECACHED)
	{
		peer->precached = 1;
	}
	else if (type == CE_MESSAGE_CLIENT_LOADED && !peer->loaded)
	{
		peer->loaded = 1;
		say("ce: a Custom Edition player loaded the map%s", "");
		if (peer->game_known)
			push_event(peer, CE_EVENT_LOADED);
	}
}

static void client_challenge(const unsigned char *packet, int length, const struct sockaddr_in *address, unsigned int now)
{
	struct peer *peer = find_peer(address);
	unsigned char *reply;
	int index;

	if (length < 7 + CE_CHALLENGE_SIZE)
		return;
	if (peer && peer->state == PEER_CHALLENGED)
	{
		/* (the client's challenge again: the same answer) */
		if (peer->handshake_length)
			send_raw(peer, peer->handshake, peer->handshake_length);
		return;
	}
	/* (a new connection from an address that had one: the client was
	restarted without closing the old one, which is gone) */
	if (peer)
		forget(peer);
	for (index = 0; index < CE_MAXIMUM_PEERS && transport.peers[index].state != PEER_FREE; index++)
		;
	/* (none free: the place of the challenge left unanswered longest. A
	first packet is anyone's, from any address: challenges alone never keep
	a player out) */
	if (index == CE_MAXIMUM_PEERS)
	{
		int oldest = -1;

		for (index = 0; index < CE_MAXIMUM_PEERS; index++)
		{
			if (transport.peers[index].state == PEER_CHALLENGED &&
				(oldest < 0 || now - transport.peers[index].heard_time > now - transport.peers[oldest].heard_time))
			{
				oldest = index;
			}
		}
		if (oldest < 0)
			return;
		index = oldest;
	}
	peer = &transport.peers[index];
	forget(peer);
	peer->state = PEER_CHALLENGED;
	peer->address = *address;
	peer->heard_time = now;
	peer->in_sequence = 1;
	new_challenge(peer->challenge);
	ce_challenge_response(peer->challenge, peer->expected);
	reply = peer->handshake;
	reply[0] = 0xfe;
	reply[1] = 0xfe;
	reply[2] = TYPE_SERVER_CHALLENGE;
	write_u16(reply + 3, 0);
	write_u16(reply + 5, peer->in_sequence);
	ce_challenge_response(packet + 7, reply + 7);
	memcpy(reply + 7 + CE_CHALLENGE_SIZE, peer->challenge, CE_CHALLENGE_SIZE);
	peer->handshake_length = 7 + 2 * CE_CHALLENGE_SIZE;
	peer->out_sequence = 1;
	send_raw(peer, reply, peer->handshake_length);
	say("ce: a Custom Edition client is connecting%s", "");
}

static void client_response(struct peer *peer, const unsigned char *packet, int length, unsigned int now)
{
	unsigned char challenge_message[200];
	char challenge_text[8];
	unsigned char *reply;
	unsigned int client_value, server_value, version;
	int index, message_length;

	if (peer->state != PEER_CHALLENGED)
	{
		/* (its response again: the same acceptance) */
		if (peer->state == PEER_CONNECTED && peer->handshake_length)
			send_raw(peer, peer->handshake, peer->handshake_length);
		return;
	}
	if (length < 7 + CE_CHALLENGE_SIZE + 16 + 4)
		return;
	if (!ce_response_matches(peer->expected, packet + 7))
	{
		say("ce: a Custom Edition client's answer was wrong%s", "");
		send_control(peer, TYPE_CLOSED);
		forget(peer);
		return;
	}
	version = read_u32_le(packet + 7 + CE_CHALLENGE_SIZE + 16);
	if (version != CE_CLIENT_VERSION)
	{
		say("ce: a Custom Edition client of another version was turned away%s", "");
		send_control(peer, TYPE_CLOSED);
		forget(peer);
		return;
	}
	/* (the value, big-endian in 16 bytes, is below 65537) */
	client_value = ((unsigned int)packet[7 + CE_CHALLENGE_SIZE + 13] << 16) |
		((unsigned int)packet[7 + CE_CHALLENGE_SIZE + 14] << 8) | packet[7 + CE_CHALLENGE_SIZE + 15];
	for (index = 0; index < 13; index++)
	{
		if (packet[7 + CE_CHALLENGE_SIZE + index])
			client_value = 0;
	}
	if (client_value == 0 || client_value >= MODULUS)
	{
		send_control(peer, TYPE_CLOSED);
		forget(peer);
		return;
	}
	peer->exponent = 2 + next_random() % (MODULUS - 3);
	server_value = ce_power(GENERATOR, peer->exponent);
	ce_key_from_secret(ce_power(client_value, peer->exponent), peer->key);
	peer->in_sequence = 2;
	reply = peer->handshake;
	memset(reply, 0, 7 + 16);
	reply[0] = 0xfe;
	reply[1] = 0xfe;
	reply[2] = TYPE_ACCEPT;
	write_u16(reply + 3, 1);
	write_u16(reply + 5, peer->in_sequence);
	reply[7 + 13] = (unsigned char)(server_value >> 16);
	reply[7 + 14] = (unsigned char)(server_value >> 8);
	reply[7 + 15] = (unsigned char)server_value;
	peer->handshake_length = 7 + 16;
	peer->out_sequence = 2;
	peer->state = PEER_CONNECTED;
	send_raw(peer, reply, peer->handshake_length);

	/* (the challenge: 7 lowercase letters) */
	for (index = 0; index < 7; index++)
		challenge_text[index] = (char)('a' + next_random() % 26);
	challenge_text[7] = 0;
	message_length = ce_message_new_client_challenge(challenge_text, transport.map[0] ? transport.map : "unknown",
		challenge_message, sizeof(challenge_message));
	send_message(peer, challenge_message, message_length, now);
}

/* the client's updates in a payload; an update with an impossible facing is
refused (counted in ce_messages.c, noted once) */
static int read_updates(struct peer *peer, const unsigned char *payload, int payload_length)
{
	static int noted;
	int before = ce_refused_facings();
	int count = ce_payload_read_client_updates(payload, payload_length, &peer->update, queue_update, peer);

	if (ce_refused_facings() != before && !noted)
	{
		noted = 1;
		say("ce: a Custom Edition client's update with an impossible facing was refused%s", "");
	}
	return count;
}

static void reliable_data(struct peer *peer, const unsigned char *packet, int length, unsigned int now)
{
	unsigned char payload[CE_PACKET_SIZE];
	unsigned int sequence = read_u16(packet + 3);
	int payload_length;

	acknowledged(peer, read_u16(packet + 5));
	if (sequence != peer->in_sequence)
	{
		/* (one already had, or one too early: the client sends it again) */
		send_control(peer, TYPE_ACK);
		return;
	}
	payload_length = ce_open(peer->key, packet + 7, length - 7, payload, sizeof(payload));
	if (payload_length < 0)
		return;
	peer->in_sequence = (peer->in_sequence + 1) & 0xffff;
	if (peer->state == PEER_ACCEPTED)
	{
		ce_payload_messages(payload, payload_length, message_received, peer);
		peer->updates += read_updates(peer, payload, payload_length);
	}
	if (peer->state == PEER_CONNECTED)
	{
		ce_payload_messages(payload, payload_length, message_received, peer);
		/* (the server answers the join request itself: a client asked for a
		map that isn't one of its stock maps tells nothing more until then).
		With the game taking joins, it decides (ce_transport_accept or
		ce_transport_refuse) */
		if (peer->joined && transport.accept && !peer->game_known)
		{
			peer->game_known = 1;
			peer->awaiting = 1;
			peer->awaiting_time = now;
			push_event(peer, CE_EVENT_JOIN);
			send_control(peer, TYPE_ACK);
			return;
		}
		if (peer->joined && !transport.accept)
		{
			char reason[16];

			/* (joining isn't there yet: a clean refusal) */
			snprintf(reason, sizeof(reason), "%d", CE_REJECT_UNKNOWN);
			refuse(peer, CE_REJECT_UNKNOWN, now);
			say("ce: the join was refused (reason %s): Custom Edition players can't play here yet", reason);
			return;
		}
	}
	send_control(peer, TYPE_ACK);
}

/* ---------- public code */

void ce_transport_start(ce_send_function send, void *context, ce_notice_function notice)
{
	memset(&transport, 0, sizeof(transport));
	transport.send = send;
	transport.context = context;
	transport.notice = notice;
	transport.random = (unsigned int)time(NULL) ^ ((unsigned int)getpid() << 16) ^ 0x5bd1e995u;
	next_random();
}

static void ce_transport_set_game_locked(const char *name, const char *map, int maximum_players)
{
	snprintf(transport.name, sizeof(transport.name), "%s", name ? name : "");
	snprintf(transport.map, sizeof(transport.map), "%s", map ? map : "");
	transport.maximum_players = maximum_players;
}

static void ce_transport_set_accept_locked(int accept)
{
	transport.accept = accept ? 1 : 0;
}

static int ce_transport_packet_locked(const unsigned char *packet, int length, const void *address, unsigned int now_milliseconds)
{
	const struct sockaddr_in *from = address;
	struct peer *peer;

	if (length >= 3 && packet[0] == 0xfe && packet[1] == 0xfe)
	{
		/* (an unreliable payload that starts FE FE, marked) */
		if (length >= 4 && packet[2] == 0xfe && packet[3] == 0xfe)
		{
			peer = find_peer(from);
			if (peer)
			{
				unsigned char payload[CE_PACKET_SIZE];
				int payload_length;

				peer->heard_time = now_milliseconds;
				if (peer->state == PEER_ACCEPTED)
				{
					payload_length = ce_open(peer->key, packet + 2, length - 2, payload, sizeof(payload));
					if (payload_length > 0)
						peer->updates += read_updates(peer, payload, payload_length);
				}
			}
			return peer != NULL;
		}
		if (packet[2] == TYPE_CLIENT_CHALLENGE)
		{
			client_challenge(packet, length, from, now_milliseconds);
			return 1;
		}
		peer = find_peer(from);
		if (!peer)
			return packet[2] == TYPE_DATA || packet[2] == TYPE_CLIENT_RESPONSE || packet[2] == TYPE_ACK ||
				packet[2] == TYPE_CLOSED;
		peer->heard_time = now_milliseconds;
		switch (packet[2])
		{
		case TYPE_CLIENT_RESPONSE:
			client_response(peer, packet, length, now_milliseconds);
			break;
		case TYPE_DATA:
			if (length >= 7 + 4 && peer->state != PEER_CHALLENGED)
				reliable_data(peer, packet, length, now_milliseconds);
			break;
		case TYPE_ACK:
			if (length >= 5)
				acknowledged(peer, read_u16(packet + 3));
			break;
		case TYPE_CLOSED:
			forget(peer);
			break;
		}
		return 1;
	}
	/* (an unreliable payload: the client's updates, keep-alives) */
	peer = find_peer(from);
	if (peer)
	{
		unsigned char payload[CE_PACKET_SIZE];
		int payload_length;

		peer->heard_time = now_milliseconds;
		if (peer->state == PEER_ACCEPTED)
		{
			payload_length = ce_open(peer->key, packet, length, payload, sizeof(payload));
			if (payload_length > 0)
				peer->updates += read_updates(peer, payload, payload_length);
		}
		return 1;
	}
	return 0;
}

static void ce_transport_update_locked(unsigned int now_milliseconds)
{
	int index, slot;

	for (index = 0; index < CE_MAXIMUM_PEERS; index++)
	{
		struct peer *peer = &transport.peers[index];

		if (peer->state == PEER_FREE)
			continue;
		if (now_milliseconds - peer->heard_time > IDLE_MILLISECONDS ||
			(peer->state == PEER_REFUSED && (all_acknowledged(peer) ||
			now_milliseconds - peer->refused_time > REFUSED_MILLISECONDS)))
		{
			send_control(peer, TYPE_CLOSED);
			forget(peer);
			continue;
		}
		if (peer->awaiting && now_milliseconds - peer->awaiting_time > AWAITING_MILLISECONDS)
		{
			peer->awaiting = 0;
			refuse(peer, CE_REJECT_UNKNOWN, now_milliseconds);
			say("ce: the game did not answer a join in time: refused%s", "");
		}
		if (peer->state == PEER_ACCEPTED && now_milliseconds - peer->keep_alive_time >= KEEP_ALIVE_MILLISECONDS)
			send_keep_alive(peer, now_milliseconds);
		for (slot = 0; slot < OUTGOING; slot++)
		{
			struct outgoing *outgoing = &peer->outgoing[slot];

			if (outgoing->used && now_milliseconds - outgoing->sent_time >= RESEND_MILLISECONDS)
			{
				outgoing->sent_time = now_milliseconds;
				send_raw(peer, outgoing->packet, outgoing->length);
			}
		}
	}
}

static void ce_transport_stop_locked(void)
{
	int index;

	for (index = 0; index < CE_MAXIMUM_PEERS; index++)
	{
		if (transport.peers[index].state != PEER_FREE)
		{
			send_control(&transport.peers[index], TYPE_CLOSED);
			forget(&transport.peers[index]);
		}
	}
}

static int ce_transport_peer_count_locked(void)
{
	int index, count = 0;

	for (index = 0; index < CE_MAXIMUM_PEERS; index++)
		count += transport.peers[index].state != PEER_FREE;
	return count;
}

/* ---------- the locked entries */

void ce_transport_set_game(const char *name, const char *map, int maximum_players)
{
	pthread_mutex_lock(&transport_mutex);
	ce_transport_set_game_locked(name, map, maximum_players);
	pthread_mutex_unlock(&transport_mutex);
}

void ce_transport_set_accept(int accept)
{
	pthread_mutex_lock(&transport_mutex);
	ce_transport_set_accept_locked(accept);
	pthread_mutex_unlock(&transport_mutex);
}

int ce_transport_packet(const unsigned char *packet, int length, const void *address, unsigned int now_milliseconds)
{
	int result;

	pthread_mutex_lock(&transport_mutex);
	result = ce_transport_packet_locked(packet, length, address, now_milliseconds);
	pthread_mutex_unlock(&transport_mutex);
	return result;
}

void ce_transport_update(unsigned int now_milliseconds)
{
	pthread_mutex_lock(&transport_mutex);
	transport.now = now_milliseconds;
	ce_transport_update_locked(now_milliseconds);
	pthread_mutex_unlock(&transport_mutex);
}

void ce_transport_stop(void)
{
	pthread_mutex_lock(&transport_mutex);
	ce_transport_stop_locked();
	pthread_mutex_unlock(&transport_mutex);
}

int ce_transport_peer_count(void)
{
	int result;

	pthread_mutex_lock(&transport_mutex);
	result = ce_transport_peer_count_locked();
	pthread_mutex_unlock(&transport_mutex);
	return result;
}

int ce_transport_next_event(struct ce_event *event)
{
	int found = 0;

	pthread_mutex_lock(&transport_mutex);
	if (transport.event_count)
	{
		*event = transport.events[transport.event_first];
		transport.event_first = (transport.event_first + 1) % CE_EVENTS;
		transport.event_count--;
		found = 1;
	}
	pthread_mutex_unlock(&transport_mutex);
	return found;
}

static struct peer *game_peer(int peer)
{
	if (peer < 0 || peer >= CE_MAXIMUM_PEERS || !transport.peers[peer].game_known)
		return NULL;
	return &transport.peers[peer];
}

int ce_transport_accept(int peer, const unsigned char *payload, int length)
{
	struct peer *accepted;
	int taken = 0;

	pthread_mutex_lock(&transport_mutex);
	accepted = game_peer(peer);
	if (accepted && accepted->awaiting)
	{
		accepted->awaiting = 0;
		accepted->state = PEER_ACCEPTED;
		send_payload(accepted, payload, length, transport.now);
		say("ce: a Custom Edition player was taken into the game%s", "");
		taken = 1;
	}
	pthread_mutex_unlock(&transport_mutex);
	return taken;
}

int ce_transport_awaiting(int peer)
{
	struct peer *joining;
	int awaiting;

	pthread_mutex_lock(&transport_mutex);
	joining = game_peer(peer);
	awaiting = joining && joining->awaiting;
	pthread_mutex_unlock(&transport_mutex);
	return awaiting;
}

void ce_transport_refuse(int peer, int reason)
{
	struct peer *refused;

	pthread_mutex_lock(&transport_mutex);
	refused = game_peer(peer);
	if (refused && refused->awaiting)
	{
		refused->awaiting = 0;
		refuse(refused, reason, transport.now);
	}
	pthread_mutex_unlock(&transport_mutex);
}

int ce_transport_send(int peer, const unsigned char *payload, int length, int reliable)
{
	struct peer *target;
	int sent = 0;

	pthread_mutex_lock(&transport_mutex);
	target = game_peer(peer);
	if (target && target->state == PEER_ACCEPTED && length > 0)
	{
		if (reliable)
			sent = send_payload(target, payload, length, transport.now);
		else
		{
			send_unreliable(target, payload, length);
			sent = 1;
		}
	}
	pthread_mutex_unlock(&transport_mutex);
	return sent;
}

int ce_transport_client_update(int peer, struct ce_client_update *update)
{
	struct peer *source;
	int result = 0;

	pthread_mutex_lock(&transport_mutex);
	source = game_peer(peer);
	if (source && source->state == PEER_ACCEPTED)
	{
		*update = source->update;
		result = 1 + source->updates;
	}
	pthread_mutex_unlock(&transport_mutex);
	return result;
}

void ce_transport_drop(int peer)
{
	struct peer *dropped;

	pthread_mutex_lock(&transport_mutex);
	dropped = game_peer(peer);
	if (dropped)
	{
		/* (the game asked: no event back) */
		dropped->game_known = 0;
		send_control(dropped, TYPE_CLOSED);
		forget(dropped);
	}
	pthread_mutex_unlock(&transport_mutex);
}

void ce_transport_kick(int peer, int reason)
{
	struct peer *kicked;

	pthread_mutex_lock(&transport_mutex);
	kicked = game_peer(peer);
	if (kicked)
	{
		/* (the game asked: no event back; closed once the reason is taken,
		as a refusal is) */
		kicked->game_known = 0;
		refuse(kicked, reason, transport.now);
	}
	pthread_mutex_unlock(&transport_mutex);
}

int ce_transport_next_update(int peer, struct ce_client_update *update)
{
	struct peer *source;
	int found = 0;

	pthread_mutex_lock(&transport_mutex);
	source = game_peer(peer);
	if (source && source->state == PEER_ACCEPTED && source->queue_count)
	{
		*update = source->queue[source->queue_first];
		source->queue_first = (source->queue_first + 1) % UPDATE_QUEUE;
		source->queue_count--;
		found = 1;
	}
	pthread_mutex_unlock(&transport_mutex);
	return found;
}

int ce_transport_pending_updates(int peer)
{
	struct peer *source;
	int count = 0;

	pthread_mutex_lock(&transport_mutex);
	source = game_peer(peer);
	if (source && source->state == PEER_ACCEPTED)
		count = source->queue_count;
	pthread_mutex_unlock(&transport_mutex);
	return count;
}
