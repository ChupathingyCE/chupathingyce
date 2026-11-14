/*
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later

DELTA_REF.C

The reference client of delta_ref.h. Layouts are docs/delta.md's ("Wire
format", "The advertisement"); tools/delta-ref/golden.h holds bytes the
game's own code wrote, which the tests compare these with.
*/

#include "delta_ref.h"

#include <stddef.h>

/* ---------- bytes */

static uint16_t get16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (unsigned)p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put16(uint8_t *p, unsigned value)
{
	p[0] = (uint8_t)(value & 0xFF);
	p[1] = (uint8_t)(value >> 8 & 0xFF);
}

static void put32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)(value & 0xFF);
	p[1] = (uint8_t)(value >> 8 & 0xFF);
	p[2] = (uint8_t)(value >> 16 & 0xFF);
	p[3] = (uint8_t)(value >> 24 & 0xFF);
}

static void zero(void *to, int size)
{
	uint8_t *p = to;

	while (size-- > 0)
		*p++ = 0;
}

static int printable(unsigned character)
{
	return character >= 0x20 && character <= 0x7E;
}

/* ---------- the platform key */

static void key_read(const uint8_t *p, struct dref_platform_key *key)
{
	key->platform = p[0];
	key->version = p[1];
	key->flags = p[2];
	key->host_players = p[3];
	key->join_players = p[4];
	/* (an opt-in limit under the default one means the default) */
	key->join_players_opt_in = p[5] < p[4] ? p[4] : p[5];
	key->memory_class = p[6];
}

static void key_write(uint8_t *p, const struct dref_platform_key *key)
{
	p[0] = key->platform;
	p[1] = key->version;
	p[2] = key->flags;
	p[3] = key->host_players;
	p[4] = key->join_players;
	p[5] = key->join_players_opt_in;
	p[6] = key->memory_class;
	p[7] = 0;
}

/* ---------- the header */

int dref_header_read(const uint8_t *data, int size, struct dref_header *header)
{
	if (!data || size < DREF_HEADER_SIZE || size > DREF_MAXIMUM_DATAGRAM || data[0] != 'D' || data[1] != 'P')
		return 0;
	header->major = data[2];
	header->type = data[3];
	header->length = get16(data + 4);
	header->session = get32(data + 8);
	return header->length == size - DREF_HEADER_SIZE;
}

static void header_write(uint8_t *out, int major, int type, int length, uint32_t session)
{
	out[0] = 'D';
	out[1] = 'P';
	out[2] = (uint8_t)major;
	out[3] = (uint8_t)type;
	put16(out + 4, (unsigned)length);
	put16(out + 6, 0);
	put32(out + 8, session);
}

int dref_empty_write(uint8_t *out, int out_size, int major, int type, uint32_t session)
{
	if (!out || out_size < DREF_HEADER_SIZE)
		return 0;
	header_write(out, major, type, 0, session);
	return DREF_HEADER_SIZE;
}

/* ---------- HELLO and WELCOME

HELLO:   capabilities (4), legacy version (2), machine index (1), build
         length (1), table serial (4), platform key (8), build, modes (4)
WELCOME: capabilities (4), agreed (4), legacy version (2), the host's
         machine index (1), build length (1), table serial (4), platform
         key (8), build, modes (4)
The modes came with chupa-44b: an older build sends none (read as 0). */

static int build_length(const char *build)
{
	int length = 0;

	while (length < DREF_BUILD_SIZE && build[length])
		length++;
	return length;
}

static void build_put(uint8_t *p, const char *build, int length)
{
	int index;

	for (index = 0; index < length; index++)
		p[index] = printable((uint8_t)build[index]) ? (uint8_t)build[index] : (uint8_t)'?';
}

static void build_get(const uint8_t *p, int length, char *build)
{
	int index;

	for (index = 0; index < length; index++)
		build[index] = printable(p[index]) ? (char)p[index] : '?';
	build[length] = 0;
}

static int greeting_write(uint8_t *out, int out_size, uint32_t session, const struct dref_hello *message, int welcome)
{
	int fixed = welcome ? 24 : 20;
	int length = build_length(message->build);
	int total = DREF_HEADER_SIZE + fixed + length + 4;
	uint8_t *p = out + DREF_HEADER_SIZE;

	if (!out || out_size < total)
		return 0;
	put32(p, message->capabilities);
	if (welcome)
	{
		put32(p + 4, message->agreed);
		p += 4;
	}
	put16(p + 4, message->legacy_version);
	p[6] = message->machine_index;
	p[7] = (uint8_t)length;
	put32(p + 8, message->table_serial);
	key_write(p + 12, &message->key);
	build_put(p + 20, message->build, length);
	put32(p + 20 + length, message->modes);
	header_write(out, DREF_MAJOR, welcome ? DREF_WELCOME : DREF_HELLO, fixed + length + 4, session);
	return total;
}

static int greeting_read(const uint8_t *p, int size, struct dref_hello *message, int welcome)
{
	int fixed = welcome ? 24 : 20;
	int length;

	zero(message, (int)sizeof(*message));
	if (!p || size < fixed || size > DREF_MAXIMUM_DATAGRAM - DREF_HEADER_SIZE)
		return 0;
	/* (the build's length: HELLO's byte 7, WELCOME's 11) */
	length = p[welcome ? 11 : 7];
	if (length > DREF_BUILD_SIZE || size < fixed + length)
		return 0;
	message->capabilities = get32(p);
	if (welcome)
	{
		message->agreed = get32(p + 4);
		p += 4;
	}
	message->legacy_version = get16(p + 4);
	message->machine_index = p[6];
	message->table_serial = get32(p + 8);
	key_read(p + 12, &message->key);
	build_get(p + 20, length, message->build);
	if (size >= fixed + length + 4)
		message->modes = get32(p + 20 + length);
	return 1;
}

int dref_hello_write(uint8_t *out, int out_size, uint32_t session, const struct dref_hello *hello)
{
	return greeting_write(out, out_size, session, hello, 0);
}

int dref_welcome_write(uint8_t *out, int out_size, uint32_t session, const struct dref_hello *welcome)
{
	return greeting_write(out, out_size, session, welcome, 1);
}

int dref_hello_read(const uint8_t *payload, int size, struct dref_hello *hello)
{
	return greeting_read(payload, size, hello, 0);
}

int dref_welcome_read(const uint8_t *payload, int size, struct dref_hello *welcome)
{
	return greeting_read(payload, size, welcome, 1);
}

/* ---------- ROSTER: room capabilities (4), room players (1), reserved
(3), count (1, at most 32), reserved (3), then 36-byte entries: machine (1),
flags (1), reserved (2), capabilities (4), platform key (8), profile
revision (4), player ID (16) */

enum
{
	ROSTER_FIXED = 12,
	ROSTER_ENTRY = 36,
	ROSTER_MOST = 32
};

int dref_roster_read(const uint8_t *payload, int size, struct dref_roster *roster)
{
	zero(roster, (int)sizeof(*roster));
	if (!payload || size < ROSTER_FIXED || size > DREF_MAXIMUM_DATAGRAM - DREF_HEADER_SIZE)
		return 0;
	if (payload[8] > ROSTER_MOST || size < ROSTER_FIXED + payload[8] * ROSTER_ENTRY)
		return 0;
	roster->room_capabilities = get32(payload);
	roster->room_players = payload[4];
	roster->count = payload[8];
	return 1;
}

int dref_roster_entry(const uint8_t *payload, int size, int index, struct dref_roster_entry *entry)
{
	struct dref_roster roster;
	const uint8_t *p;

	zero(entry, (int)sizeof(*entry));
	if (!dref_roster_read(payload, size, &roster) || index < 0 || index >= roster.count)
		return 0;
	p = payload + ROSTER_FIXED + index * ROSTER_ENTRY;
	entry->machine_index = p[0];
	entry->flags = p[1];
	entry->capabilities = get32(p + 4);
	key_read(p + 8, &entry->key);
	return 1;
}

uint32_t dref_capabilities_clean(uint32_t capabilities)
{
	return capabilities & ~(uint32_t)DREF_CAPS_NEVER_SAID;
}

/* ---------- the client's handshake */

static void client_hello(struct dref_client *client, uint32_t now)
{
	uint8_t data[DREF_HEADER_SIZE + 24 + DREF_BUILD_SIZE + 4];
	int size = dref_hello_write(data, (int)sizeof(data), client->session, &client->mine);

	client->hello_sent = now;
	if (size && client->send)
		client->send(client->user, data, size);
}

void dref_client_start(struct dref_client *client, const struct dref_hello *mine, uint32_t session, uint32_t now_ms,
	dref_send_fn send, void *user)
{
	zero(client, (int)sizeof(*client));
	client->mine = *mine;
	client->mine.agreed = 0;
	client->mine.capabilities = dref_capabilities_clean(mine->capabilities);
	client->session = session ? session : 1;
	client->started = now_ms;
	client->send = send;
	client->user = user;
	client->state = DREF_CLIENT_WAITING;
	client_hello(client, now_ms);
}

void dref_client_tick(struct dref_client *client, uint32_t now_ms)
{
	if (client->state != DREF_CLIENT_WAITING)
		return;
	/* (unsigned differences: the clock may wrap) */
	if ((uint32_t)(now_ms - client->started) >= DREF_HANDSHAKE_MS)
		client->state = DREF_CLIENT_LEGACY;
	else if ((uint32_t)(now_ms - client->hello_sent) >= DREF_HELLO_RESEND_MS)
		client_hello(client, now_ms);
}

int dref_client_receive(struct dref_client *client, uint32_t now_ms, const uint8_t *data, int size)
{
	struct dref_header header;
	const uint8_t *payload = data + DREF_HEADER_SIZE;

	(void)now_ms;
	if ((client->state != DREF_CLIENT_WAITING && client->state != DREF_CLIENT_DELTA) ||
		!dref_header_read(data, size, &header) || header.session != client->session)
	{
		return 0;
	}
	if (header.major != DREF_MAJOR)
	{
		/* (another major says LEGACY and nothing else) */
		if (header.type != DREF_LEGACY)
			return 0;
		client->state = DREF_CLIENT_LEGACY;
		return DREF_LEGACY;
	}
	switch (header.type)
	{
	case DREF_WELCOME:
	{
		struct dref_hello welcome;

		if (client->state != DREF_CLIENT_WAITING || !dref_welcome_read(payload, header.length, &welcome))
			return 0;
		client->host = welcome;
		/* (never more than this machine offered, whatever the host says) */
		client->agreed = welcome.agreed & welcome.capabilities & client->mine.capabilities;
		client->state = DREF_CLIENT_DELTA;
		return DREF_WELCOME;
	}
	case DREF_LEGACY:
	case DREF_BYE:
		client->state = DREF_CLIENT_LEGACY;
		return header.type;
	case DREF_ROSTER:
	{
		struct dref_roster roster;

		if (client->state != DREF_CLIENT_DELTA || !dref_roster_read(payload, header.length, &roster))
			return 0;
		client->room_capabilities = roster.room_capabilities & client->agreed;
		client->room_players = roster.room_players;
		return DREF_ROSTER;
	}
	default:
		return 0;
	}
}

void dref_client_stop(struct dref_client *client)
{
	if (client->state == DREF_CLIENT_DELTA && client->send)
	{
		uint8_t data[DREF_HEADER_SIZE];

		dref_empty_write(data, (int)sizeof(data), DREF_MAJOR, DREF_BYE, client->session);
		client->send(client->user, data, DREF_HEADER_SIZE);
	}
	client->state = DREF_CLIENT_OFF;
}

/* ---------- the advertisement */

/* a wire's number and letter as the tag has them: 1 if they are a wire */
static int wire_bytes(const uint8_t *p, struct dref_wire *wire)
{
	if (p[0] < 1 || p[1] < 'a' || p[1] > 'z')
		return 0;
	wire->number = p[0];
	wire->letter = p[1];
	return 1;
}

static int wire_older(const struct dref_wire *a, const struct dref_wire *b)
{
	return a->number != b->number ? a->number < b->number : a->letter < b->letter;
}

/* "chupa-" then 1 to 4 digits (1 to 255) then a letter, of length bytes */
static int wire_text(const uint8_t *text, int length)
{
	static const char prefix[] = "chupa-";
	int index, digits = 0, number = 0;

	if (length <= 7)
		return 0;
	for (index = 0; index < 6; index++)
	{
		if (text[index] != (uint8_t)prefix[index])
			return 0;
	}
	while (index < length - 1 && text[index] >= '0' && text[index] <= '9' && digits < 4)
	{
		number = number * 10 + (text[index] - '0');
		index++;
		digits++;
	}
	return digits && index == length - 1 && number >= 1 && number <= 255 && text[index] >= 'a' &&
		text[index] <= 'z';
}

int dref_advert_read(const uint8_t *reserved, int size, struct dref_advert *advert)
{
	const uint8_t *tag = reserved + DREF_TAG_OFFSET;
	uint8_t check = 0xA5;
	int index;

	zero(advert, (int)sizeof(*advert));
	if (!reserved || size < DREF_RESERVED_SIZE)
		return 0;
	advert->version = get16(reserved + DREF_RESERVED_VERSION);
	advert->flags = reserved[DREF_RESERVED_FLAGS];
	advert->kind = reserved[DREF_RESERVED_KIND];
	for (index = 0; index < DREF_TAG_SIZE - 1; index++)
		check ^= tag[index];
	if (tag[0] == 'D' && tag[1] == 'L' && tag[3] == 1 && tag[8] == check && wire_bytes(tag + 4, &advert->wire) &&
		wire_bytes(tag + 6, &advert->oldest) && !wire_older(&advert->wire, &advert->oldest))
	{
		advert->has_tag = 1;
		advert->major = tag[2];
		advert->speaks_delta = 1;
		return 1;
	}
	zero(&advert->wire, (int)sizeof(advert->wire));
	zero(&advert->oldest, (int)sizeof(advert->oldest));
	/* (the old layout, hosts before chupa-39a: the flag 0x04, which from
	OpenCE's 39 is its dedicated flag, so from 39 it counts only with a
	wire ID after the major at byte 3) */
	if (advert->flags & DREF_FLAG_DEDICATED)
	{
		if (advert->version < 39)
			advert->speaks_delta = 1;
		else
		{
			const uint8_t *slot = reserved + 4;
			int length = 0;

			while (length < 16 && slot[length])
				length++;
			advert->speaks_delta = wire_text(slot, length);
		}
	}
	return 1;
}

void dref_advert_write(uint8_t *reserved, unsigned version, unsigned flags, unsigned kind)
{
	zero(reserved, DREF_RESERVED_SIZE);
	put16(reserved + DREF_RESERVED_VERSION, version);
	reserved[DREF_RESERVED_FLAGS] = (uint8_t)flags;
	reserved[DREF_RESERVED_KIND] = (uint8_t)kind;
}

int dref_join_mark(uint16_t *name, int count)
{
	int end = 0;
	int index;

	if (!name || count < 4)
		return 0;
	while (end < count && name[end])
		end++;
	if (end > count - 4)
		return 0;
	for (index = end; index < count; index++)
		name[index] = 0;
	name[count - 3] = DREF_JOIN_MARK_FIRST;
	name[count - 2] = DREF_JOIN_MARK_SECOND;
	return 1;
}

/* ---------- the game list */

static int form_put(char *form, int size, int *used, const char *name, const char *value)
{
	static const char digits[] = "0123456789ABCDEF";

	if (*used)
	{
		if (*used >= size - 1)
			return 0;
		form[(*used)++] = '&';
	}
	for (; *name; name++)
	{
		if (*used >= size - 1)
			return 0;
		form[(*used)++] = *name;
	}
	if (*used >= size - 1)
		return 0;
	form[(*used)++] = '=';
	for (; *value; value++)
	{
		unsigned character = (uint8_t)*value;

		if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.')
		{
			if (*used >= size - 1)
				return 0;
			form[(*used)++] = (char)character;
		}
		else
		{
			if (*used >= size - 3)
				return 0;
			form[(*used)++] = '%';
			form[(*used)++] = digits[character >> 4];
			form[(*used)++] = digits[character & 15];
		}
	}
	return 1;
}

static void hex(const uint8_t *bytes, int count, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < count; index++)
	{
		text[index * 2] = digits[bytes[index] >> 4];
		text[index * 2 + 1] = digits[bytes[index] & 15];
	}
	text[count * 2] = 0;
}

int dref_announce_fields(char *form, int size, int delta, const struct dref_platform_key *key, const char *arch,
	const char *build, uint32_t capabilities)
{
	uint8_t bytes[8];
	char key_text[17], number[11];
	int used = 0, start;

	if (!form || size <= 0)
		return -1;
	while (used < size && form[used])
		used++;
	if (used >= size)
		return -1;
	start = used;
	number[0] = (char)('0' + (delta > 0 && delta < 10 ? delta : 0));
	number[1] = 0;
	key_write(bytes, key);
	hex(bytes, 8, key_text);
	if (!form_put(form, size, &used, "delta", number) || !form_put(form, size, &used, "platform_key", key_text) ||
		!form_put(form, size, &used, "arch", arch ? arch : "") ||
		!form_put(form, size, &used, "build", build ? build : ""))
	{
		form[start] = 0;
		return -1;
	}
	/* (8 hex digits, most significant first) */
	bytes[0] = (uint8_t)(capabilities >> 24 & 0xFF);
	bytes[1] = (uint8_t)(capabilities >> 16 & 0xFF);
	bytes[2] = (uint8_t)(capabilities >> 8 & 0xFF);
	bytes[3] = (uint8_t)(capabilities & 0xFF);
	hex(bytes, 4, number);
	if (!form_put(form, size, &used, "capabilities", number))
	{
		form[start] = 0;
		return -1;
	}
	form[used] = 0;
	return used;
}
