/*
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later

DELTA_REF_TEST.C

The reference client's tests: its bytes against golden.h (bytes the game's
own Delta code wrote), its readers on those bytes, the handshake's timing
and answers, and every reader fed random and cut-short input. Needs only the
C library: `make test` in tools/delta-ref.
*/

#include "delta_ref.h"
#include "golden.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("delta_ref_test: failed at line %d: %s\n", __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static int same(const uint8_t *a, int a_size, const uint8_t *b, int b_size)
{
	return a_size == b_size && !memcmp(a, b, (size_t)a_size);
}

static struct dref_platform_key key_of(int platform, int flags, int host, int join, int opt_in, int memory)
{
	struct dref_platform_key key;

	memset(&key, 0, sizeof(key));
	key.platform = (uint8_t)platform;
	key.version = 1;
	key.flags = (uint8_t)flags;
	key.host_players = (uint8_t)host;
	key.join_players = (uint8_t)join;
	key.join_players_opt_in = (uint8_t)opt_in;
	key.memory_class = (uint8_t)memory;
	return key;
}

static struct dref_hello port_hello(void)
{
	struct dref_hello hello;

	memset(&hello, 0, sizeof(hello));
	hello.capabilities = 1u << DREF_CAP_PLATFORM;
	hello.legacy_version = 44;
	hello.machine_index = 2;
	hello.table_serial = DREF_NO_TABLES;
	hello.key = key_of(DREF_PLATFORM_N64, 0, 0, 8, 16, 3);
	strcpy(hello.build, "n64-port 0.1");
	return hello;
}

static void test_numbers(void)
{
	CHECK(DREF_MAJOR == GOLDEN_MAJOR);
	CHECK(DREF_HOST_PORT == GOLDEN_PEER_PORT);
	CHECK(DREF_CAP_PLATFORM == GOLDEN_CAP_PLATFORM && DREF_CAP_PING == GOLDEN_CAP_PING);
	CHECK(DREF_CAP_HOST_CHEATS == GOLDEN_CAP_HOST_CHEATS && DREF_CAP_GAME_MODES == GOLDEN_CAP_GAME_MODES);
	CHECK(DREF_PLATFORM_N64 == GOLDEN_PLATFORM_N64 && DREF_PLATFORM_PC_LEGACY == GOLDEN_PLATFORM_PC_LEGACY);
	CHECK(dref_capabilities_clean(0xFFFFFFFFu) == 0xFFFBEDDFu);
}

static void test_written(void)
{
	uint8_t data[DREF_MAXIMUM_DATAGRAM];
	struct dref_hello hello = port_hello();
	struct dref_hello welcome;
	int size;

	size = dref_hello_write(data, (int)sizeof(data), 0x1234ABCDu, &hello);
	CHECK(same(data, size, golden_hello_port, (int)sizeof(golden_hello_port)));
	/* (too small a buffer: nothing) */
	CHECK(dref_hello_write(data, (int)sizeof(golden_hello_port) - 1, 0x1234ABCDu, &hello) == 0);

	memset(&hello, 0, sizeof(hello));
	hello.capabilities = 0x03FBEDDFu;
	hello.legacy_version = 44;
	hello.machine_index = 1;
	hello.table_serial = 12;
	hello.key = key_of(3, 0, 128, 128, 128, 12);
	strcpy(hello.build, "0.9.2");
	hello.modes = 3;
	size = dref_hello_write(data, (int)sizeof(data), 0x89ABCDEFu, &hello);
	CHECK(same(data, size, golden_hello_pc, (int)sizeof(golden_hello_pc)));

	memset(&welcome, 0, sizeof(welcome));
	welcome.capabilities = 0x03FBEDDFu;
	welcome.agreed = 1;
	welcome.legacy_version = 44;
	welcome.machine_index = DREF_NO_MACHINE;
	welcome.table_serial = 12;
	welcome.key = key_of(3, 0x02, 128, 128, 128, 12);
	strcpy(welcome.build, "0.9.2 server");
	welcome.modes = 3;
	size = dref_welcome_write(data, (int)sizeof(data), 0x1234ABCDu, &welcome);
	CHECK(same(data, size, golden_welcome, (int)sizeof(golden_welcome)));

	size = dref_empty_write(data, (int)sizeof(data), 2, DREF_LEGACY, 0x1234ABCDu);
	CHECK(same(data, size, golden_legacy_major2, (int)sizeof(golden_legacy_major2)));
	size = dref_empty_write(data, (int)sizeof(data), DREF_MAJOR, DREF_BYE, 0x1234ABCDu);
	CHECK(same(data, size, golden_bye, (int)sizeof(golden_bye)));
}

static void test_read(void)
{
	struct dref_header header;
	struct dref_hello hello;
	struct dref_roster roster;
	struct dref_roster_entry entry;
	const uint8_t *payload;

	CHECK(dref_header_read(golden_hello_port, (int)sizeof(golden_hello_port), &header));
	CHECK(header.major == 1 && header.type == DREF_HELLO && header.session == 0x1234ABCDu &&
		header.length == sizeof(golden_hello_port) - DREF_HEADER_SIZE);
	CHECK(dref_hello_read(golden_hello_port + DREF_HEADER_SIZE, header.length, &hello));
	CHECK(hello.capabilities == 1 && hello.legacy_version == 44 && hello.machine_index == 2 &&
		hello.table_serial == DREF_NO_TABLES && hello.key.platform == DREF_PLATFORM_N64 &&
		hello.key.join_players == 8 && hello.key.memory_class == 3 && !strcmp(hello.build, "n64-port 0.1") &&
		hello.modes == 0);
	/* (a length that is not the datagram's: not a header) */
	CHECK(!dref_header_read(golden_hello_port, (int)sizeof(golden_hello_port) - 1, &header));

	CHECK(dref_header_read(golden_welcome, (int)sizeof(golden_welcome), &header) && header.type == DREF_WELCOME);
	CHECK(dref_welcome_read(golden_welcome + DREF_HEADER_SIZE, header.length, &hello));
	CHECK(hello.capabilities == 0x03FBEDDFu && hello.agreed == 1 && hello.machine_index == DREF_NO_MACHINE &&
		hello.table_serial == 12 && hello.key.flags == 0x02 && !strcmp(hello.build, "0.9.2 server") &&
		hello.modes == 3);
	/* (a WELCOME of a build before the modes: read as 0) */
	CHECK(dref_welcome_read(golden_welcome + DREF_HEADER_SIZE, header.length - 4, &hello) && hello.modes == 0);

	CHECK(dref_header_read(golden_roster, (int)sizeof(golden_roster), &header) && header.type == DREF_ROSTER);
	payload = golden_roster + DREF_HEADER_SIZE;
	CHECK(dref_roster_read(payload, header.length, &roster));
	CHECK(roster.room_capabilities == 1 && roster.room_players == 8 && roster.count == 2);
	CHECK(dref_roster_entry(payload, header.length, 1, &entry));
	CHECK(entry.machine_index == 2 && entry.flags == 0x03 && entry.capabilities == 1 &&
		entry.key.platform == DREF_PLATFORM_N64 && entry.key.join_players_opt_in == 16);
	CHECK(!dref_roster_entry(payload, header.length, 2, &entry));
	CHECK(!dref_roster_read(payload, header.length - 1, &roster));
}

static void test_advert(void)
{
	struct dref_advert advert;
	uint8_t reserved[DREF_RESERVED_SIZE];
	char wire[16];

	CHECK(dref_advert_read(golden_advert_tag, (int)sizeof(golden_advert_tag), &advert));
	CHECK(advert.speaks_delta == GOLDEN_ADVERT_TAG_SPEAKS && advert.has_tag && advert.major == 1);
	CHECK(advert.version == 44 && advert.flags == 0x05 && advert.kind == 0);
	snprintf(wire, sizeof(wire), "chupa-%d%c", advert.wire.number, advert.wire.letter);
	CHECK(!strcmp(wire, GOLDEN_WIRE));
	CHECK(advert.oldest.number == 28 && advert.oldest.letter == 'a');

	CHECK(dref_advert_read(golden_advert_both, (int)sizeof(golden_advert_both), &advert));
	CHECK(advert.speaks_delta == GOLDEN_ADVERT_BOTH_SPEAKS && advert.has_tag && advert.version == 32);
	CHECK(dref_advert_read(golden_advert_old, (int)sizeof(golden_advert_old), &advert));
	CHECK(advert.speaks_delta == GOLDEN_ADVERT_OLD_SPEAKS && !advert.has_tag);
	CHECK(dref_advert_read(golden_advert_opence, (int)sizeof(golden_advert_opence), &advert));
	CHECK(advert.speaks_delta == GOLDEN_ADVERT_OPENCE_SPEAKS && !advert.has_tag && advert.version == 44);

	/* (a tag with a wrong check byte is no tag) */
	memcpy(reserved, golden_advert_tag, sizeof(reserved));
	reserved[DREF_TAG_OFFSET + 8] ^= 1;
	CHECK(dref_advert_read(reserved, (int)sizeof(reserved), &advert) && !advert.has_tag);
	CHECK(!dref_advert_read(reserved, DREF_RESERVED_SIZE - 1, &advert));

	/* (a port's own: OpenCE's fields, no tag) */
	memset(reserved, 0xCC, sizeof(reserved));
	dref_advert_write(reserved, 44, DREF_FLAG_DISTRIBUTED, 0);
	CHECK(reserved[0] == 44 && reserved[1] == 0 && reserved[2] == 1 && reserved[3] == 0);
	CHECK(dref_advert_read(reserved, (int)sizeof(reserved), &advert) && !advert.speaks_delta);
	{
		int index, clear = 1;

		for (index = 4; index < DREF_RESERVED_SIZE; index++)
			clear &= reserved[index] == 0;
		CHECK(clear);
	}
}

static void test_join_mark(void)
{
	uint16_t name[32];
	uint8_t bytes[64];
	int index;

	memset(name, 0xEE, sizeof(name));
	name[0] = 'N';
	name[1] = '6';
	name[2] = '4';
	name[3] = 0;
	CHECK(dref_join_mark(name, 32));
	for (index = 0; index < 32; index++)
	{
		bytes[index * 2] = (uint8_t)(name[index] & 0xFF);
		bytes[index * 2 + 1] = (uint8_t)(name[index] >> 8);
	}
	CHECK(same(bytes, 64, golden_join_mark_le, (int)sizeof(golden_join_mark_le)));
	/* (a name too long for the mark is left alone) */
	for (index = 0; index < 29; index++)
		name[index] = 'x';
	name[29] = 0;
	CHECK(!dref_join_mark(name, 32) && name[29] == 0);
}

static void test_announce(void)
{
	char form[256];
	struct dref_platform_key key = key_of(DREF_PLATFORM_N64, 0, 0, 8, 16, 3);

	form[0] = 0;
	CHECK(dref_announce_fields(form, (int)sizeof(form), 0, &key, "mips", "n64-port 0.1+test", 1) ==
		(int)strlen(golden_announce));
	CHECK(!strcmp(form, golden_announce));
	/* (after other fields, an & between) */
	strcpy(form, "name=x");
	CHECK(dref_announce_fields(form, (int)sizeof(form), 0, &key, "mips", "n64-port 0.1+test", 1) > 0);
	CHECK(!strncmp(form, "name=x&delta=0&", 15));
	/* (too small: left as it was) */
	strcpy(form, "name=x");
	CHECK(dref_announce_fields(form, 40, 0, &key, "mips", "n64-port 0.1+test", 1) == -1);
	CHECK(!strcmp(form, "name=x"));
}

/* ---------- the handshake */

static uint8_t sent[8][DREF_MAXIMUM_DATAGRAM];
static int sent_size[8];
static int sent_count;

static void capture(void *user, const uint8_t *data, int size)
{
	(void)user;
	if (sent_count < 8)
	{
		memcpy(sent[sent_count], data, (size_t)size);
		sent_size[sent_count] = size;
	}
	sent_count++;
}

static void test_handshake(void)
{
	struct dref_client client;
	struct dref_hello hello = port_hello();
	uint8_t data[DREF_MAXIMUM_DATAGRAM];
	int size;

	/* HELLO at once, again every 500 ms, legacy after 4 s */
	sent_count = 0;
	dref_client_start(&client, &hello, 0x1234ABCDu, 1000, capture, NULL);
	CHECK(client.state == DREF_CLIENT_WAITING && sent_count == 1);
	CHECK(same(sent[0], sent_size[0], golden_hello_port, (int)sizeof(golden_hello_port)));
	dref_client_tick(&client, 1499);
	CHECK(sent_count == 1);
	dref_client_tick(&client, 1500);
	CHECK(sent_count == 2);
	dref_client_tick(&client, 4999);
	CHECK(client.state == DREF_CLIENT_WAITING);
	dref_client_tick(&client, 5000);
	CHECK(client.state == DREF_CLIENT_LEGACY);
	/* (a WELCOME after the window changes nothing) */
	CHECK(!dref_client_receive(&client, 5001, golden_welcome, (int)sizeof(golden_welcome)));

	/* WELCOME: Delta, the bits both offered */
	sent_count = 0;
	dref_client_start(&client, &hello, 0x1234ABCDu, 0xFFFFFF00u, capture, NULL);
	/* (another session's: dropped) */
	memcpy(data, golden_welcome, sizeof(golden_welcome));
	data[8] ^= 1;
	CHECK(!dref_client_receive(&client, 0xFFFFFF10u, data, (int)sizeof(golden_welcome)));
	CHECK(dref_client_receive(&client, 0xFFFFFF10u, golden_welcome, (int)sizeof(golden_welcome)) == DREF_WELCOME);
	CHECK(client.state == DREF_CLIENT_DELTA && client.agreed == 1 && client.host.machine_index == DREF_NO_MACHINE);
	/* (the clock wraps: no resend, no timeout once Delta) */
	dref_client_tick(&client, 0x00000100u);
	CHECK(client.state == DREF_CLIENT_DELTA && sent_count == 1);
	CHECK(dref_client_receive(&client, 0, golden_roster, (int)sizeof(golden_roster)) == DREF_ROSTER);
	CHECK(client.room_players == 8 && client.room_capabilities == 1);
	/* (an unknown type: ignored) */
	memcpy(data, golden_bye, sizeof(golden_bye));
	data[3] = 200;
	CHECK(!dref_client_receive(&client, 0, data, (int)sizeof(golden_bye)) && client.state == DREF_CLIENT_DELTA);
	dref_client_stop(&client);
	CHECK(sent_count == 2 && same(sent[1], sent_size[1], golden_bye, (int)sizeof(golden_bye)));
	CHECK(client.state == DREF_CLIENT_OFF);

	/* a host of another major: LEGACY */
	dref_client_start(&client, &hello, 0x1234ABCDu, 0, capture, NULL);
	CHECK(dref_client_receive(&client, 1, golden_legacy_major2, (int)sizeof(golden_legacy_major2)) == DREF_LEGACY);
	CHECK(client.state == DREF_CLIENT_LEGACY);

	/* never more than offered, never a bit a HELLO doesn't carry */
	hello.capabilities = 0xFFFFFFFFu;
	sent_count = 0;
	dref_client_start(&client, &hello, 7, 0, capture, NULL);
	{
		struct dref_hello said;

		CHECK(dref_hello_read(sent[0] + DREF_HEADER_SIZE, sent_size[0] - DREF_HEADER_SIZE, &said));
		CHECK(!(said.capabilities & DREF_CAPS_NEVER_SAID));
	}
	hello = port_hello();
	dref_client_start(&client, &hello, 0, 0, capture, NULL);
	CHECK(client.session == 1);
	size = dref_welcome_write(data, (int)sizeof(data), 1, &client.mine);
	CHECK(size > 0);
}

/* ---------- hostile input */

static uint32_t seed = 0x2545F491u;

static uint32_t next(void)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

static void test_random(long iterations)
{
	static const uint8_t *const goldens[] = { golden_hello_port, golden_hello_pc, golden_welcome, golden_roster,
		golden_bye };
	static const int sizes[] = { (int)sizeof(golden_hello_port), (int)sizeof(golden_hello_pc),
		(int)sizeof(golden_welcome), (int)sizeof(golden_roster), (int)sizeof(golden_bye) };
	/* (exactly the bytes given: a reader past them is caught by the
	sanitizers the tests build with) */
	static uint8_t data[DREF_MAXIMUM_DATAGRAM + 16];
	long iteration;

	for (iteration = 0; iteration < iterations; iteration++)
	{
		int pick = (int)(next() % 5);
		int size = (int)(next() % (DREF_MAXIMUM_DATAGRAM + 8));
		int index;
		struct dref_header header;
		struct dref_hello hello;
		struct dref_roster roster;
		struct dref_roster_entry entry;
		struct dref_advert advert;
		struct dref_client client;
		struct dref_hello mine = port_hello();

		if (next() & 1)
		{
			size = sizes[pick] - (int)(next() % 4);
			memcpy(data, goldens[pick], (size_t)sizes[pick]);
			for (index = (int)(next() % 4); index > 0; index--)
				data[next() % (uint32_t)sizes[pick]] = (uint8_t)next();
		}
		else
		{
			for (index = 0; index < size; index++)
				data[index] = (uint8_t)next();
			if (size > 4 && (next() & 1))
			{
				data[0] = 'D';
				data[1] = 'P';
				data[4] = (uint8_t)((size - DREF_HEADER_SIZE) & 0xFF);
				data[5] = (uint8_t)((size - DREF_HEADER_SIZE) >> 8);
			}
		}
		if (size < 0)
			size = 0;
		dref_header_read(data, size, &header);
		dref_hello_read(data, size, &hello);
		dref_welcome_read(data, size, &hello);
		dref_roster_read(data, size, &roster);
		dref_roster_entry(data, size, (int)(next() % 40), &entry);
		dref_advert_read(data, size, &advert);
		sent_count = 0;
		dref_client_start(&client, &mine, header.session, 0, capture, NULL);
		dref_client_receive(&client, 1, data, size);
		dref_client_receive(&client, 2, data, size);
		dref_client_tick(&client, next());
		dref_client_stop(&client);
	}
}

int main(int argc, char **argv)
{
	long iterations = argc > 1 ? atol(argv[1]) : 200000;

	test_numbers();
	test_written();
	test_read();
	test_advert();
	test_join_mark();
	test_announce();
	test_handshake();
	test_random(iterations);
	if (failures)
	{
		printf("delta_ref_test: %d failed\n", failures);
		return 1;
	}
	printf("delta_ref_test: ok (%ld random inputs)\n", iterations);
	return 0;
}
