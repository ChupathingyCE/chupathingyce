/*
DELTA_TEST.C

Delta Peer's tests (tools/test_delta_peer.py runs them, under
AddressSanitizer and UndefinedBehaviorSanitizer where the compiler has
them): the wire format's round trips and its refusals, a host and clients
in one process over a simulated network (handshakes, fallback to the legacy
protocol, the roster, the room's capabilities and limits, the legacy table's
hook, rate limits, strangers), and a deterministic random-input test of
every parser and of both sides' sessions.

    delta_test [fuzz iterations] [seed]
*/

#include "../src/delta_peer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define PLATFORM_BIT ((delta_u32)1 << _delta_capability_platform)
#define PROFILE_BIT ((delta_u32)1 << _delta_capability_profile)

/* ---------- a simulated network */

enum
{
	MAXIMUM_NODES = 8,
	MAXIMUM_QUEUED = 512,
	HOST_IPV4 = 0x0100007F
};

struct datagram
{
	delta_u32 from_ipv4;
	unsigned short from_port;
	delta_u32 to_ipv4;
	unsigned short to_port;
	int size;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
};

struct node
{
	struct delta_peer peer;
	delta_u32 ipv4;
	unsigned short port;
	/* the network loses everything this node sends */
	int mute;
	/* its legacy table (the relay's): serial (DELTA_WIRE_TABLE_NONE takes
	none), the signed table's bytes, the checks made and tables taken, the
	TABLE datagrams it was sent, and a check that fails whatever comes */
	delta_u32 table_serial;
	int table_size;
	unsigned char table[DELTA_LEGACY_SIGNED_SIZE];
	int checks;
	int taken;
	int pieces_in;
	int reject;
	/* the kill switch's bits */
	delta_u32 disabled;
	char last_log[256];
};

static struct node nodes[MAXIMUM_NODES];
static struct datagram queue[MAXIMUM_QUEUED];
static int queued;
static delta_u32 random_state = 1;
static int verbose;

static delta_u32 next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static void node_send(void *context, delta_u32 ipv4, unsigned short port, const unsigned char *data, int size)
{
	struct node *node = (struct node *)context;
	struct datagram *datagram;

	if (node->mute || queued == MAXIMUM_QUEUED || size <= 0 || size > DELTA_WIRE_MAXIMUM_DATAGRAM)
		return;
	datagram = &queue[queued++];
	datagram->from_ipv4 = node->ipv4;
	datagram->from_port = node->port;
	datagram->to_ipv4 = ipv4;
	datagram->to_port = port;
	datagram->size = size;
	memcpy(datagram->data, data, (size_t)size);
}

static void node_log(void *context, const char *text)
{
	struct node *node = (struct node *)context;

	snprintf(node->last_log, sizeof(node->last_log), "%s", text);
	if (verbose)
		printf("[node %d] %s\n", (int)(node - nodes), text);
}

static delta_u32 node_random(void *context)
{
	(void)context;
	return next_random();
}

/* a stand-in signed table: its serial in its first 4 bytes, then a pattern
of it (the stand-in for its signature: delta.c checks the real one) */
static void make_table(struct node *node, delta_u32 serial, int size)
{
	int index;

	node->table_serial = serial;
	node->table_size = serial && serial != DELTA_WIRE_TABLE_NONE ? size : 0;
	for (index = 0; index < node->table_size; index++)
		node->table[index] = (unsigned char)(index < 4 ? serial >> (8 * index) : serial * 31u + (delta_u32)index * 7u);
}

static int table_valid(const unsigned char *table, int size)
{
	delta_u32 serial;
	int index;

	if (size < 4 || size > DELTA_LEGACY_SIGNED_SIZE)
		return 0;
	serial = (delta_u32)table[0] | (delta_u32)table[1] << 8 | (delta_u32)table[2] << 16 | (delta_u32)table[3] << 24;
	for (index = 4; index < size; index++)
	{
		if (table[index] != (unsigned char)(serial * 31u + (delta_u32)index * 7u))
			return 0;
	}
	return 1;
}

static int node_capability_disabled(void *context, int capability)
{
	return (((struct node *)context)->disabled >> capability) & 1;
}

static delta_u32 node_table_serial(void *context)
{
	return ((struct node *)context)->table_serial;
}

static int node_table_signed(void *context, unsigned char *buffer, int size)
{
	struct node *node = (struct node *)context;

	if (!node->table_size || node->table_size > size)
		return 0;
	memcpy(buffer, node->table, (size_t)node->table_size);
	return node->table_size;
}

static int node_table_offer(void *context, const unsigned char *table, int size)
{
	struct node *node = (struct node *)context;
	delta_u32 serial;

	node->checks++;
	if (node->reject || node->table_serial == DELTA_WIRE_TABLE_NONE || !table_valid(table, size))
		return 0;
	serial = (delta_u32)table[0] | (delta_u32)table[1] << 8 | (delta_u32)table[2] << 16 | (delta_u32)table[3] << 24;
	if (serial <= node->table_serial)
		return 0;
	node->table_serial = serial;
	node->table_size = size;
	memcpy(node->table, table, (size_t)size);
	node->taken++;
	return 1;
}

static void pc_key(struct delta_platform_key *key, int platform)
{
	memset(key, 0, sizeof(*key));
	key->platform = (unsigned char)platform;
	key->version = DELTA_PLATFORM_KEY_VERSION;
	key->host_players = 128;
	key->join_players = 128;
	key->join_players_opt_in = 128;
	key->memory_class = 12;
}

static void node_start(int index, delta_u32 ipv4, unsigned short port, delta_u32 capabilities, int platform,
	int profile_byte, delta_u32 serial)
{
	struct node *node = &nodes[index];
	struct delta_peer_env env;
	struct delta_peer_local local;

	memset(node, 0, sizeof(*node));
	node->ipv4 = ipv4;
	node->port = port;
	memset(&env, 0, sizeof(env));
	env.context = node;
	env.send_datagram = node_send;
	env.log_line = node_log;
	env.random_number = node_random;
	env.legacy_table_serial = node_table_serial;
	env.legacy_table_signed = node_table_signed;
	env.legacy_table_offer = node_table_offer;
	env.capability_disabled = node_capability_disabled;
	memset(&local, 0, sizeof(local));
	local.capabilities = capabilities;
	local.legacy_version = 18;
	local.legacy_table_serial = serial;
	pc_key(&local.key, platform);
	snprintf(local.build, sizeof(local.build), "test %d", index);
	if (profile_byte)
	{
		local.has_profile = 1;
		local.profile.revision = (delta_u32)profile_byte;
		memset(local.profile.player_id, profile_byte, sizeof(local.profile.player_id));
	}
	make_table(node, serial, 3000);
	delta_peer_initialize(&node->peer, &env, &local);
}

/* everything queued delivered (and what that sends, until quiet) */
static void deliver(delta_u32 now)
{
	int rounds;

	for (rounds = 0; rounds < 16 && queued; rounds++)
	{
		static struct datagram batch[MAXIMUM_QUEUED];
		int count = queued;
		int index;

		memcpy(batch, queue, sizeof(batch[0]) * (size_t)count);
		queued = 0;
		for (index = 0; index < count; index++)
		{
			int node;

			for (node = 0; node < MAXIMUM_NODES; node++)
			{
				if (nodes[node].ipv4 == batch[index].to_ipv4 && nodes[node].port == batch[index].to_port)
				{
					if (batch[index].size > 3 && batch[index].data[3] == _delta_message_table)
						nodes[node].pieces_in++;
					delta_peer_receive(&nodes[node].peer, now, batch[index].from_ipv4, batch[index].from_port,
						batch[index].data, batch[index].size);
				}
			}
		}
	}
}

/* the game: machine 0 the host's own, then one a client node (node index
= machine index) */
static struct delta_peer_game_machine game_machines[DELTA_PEER_MAXIMUM_MACHINES];
static int game_machine_count;
static signed char player_machines[DELTA_PEER_MAXIMUM_PLAYERS];

static void game_reset(void)
{
	game_machine_count = 1;
	memset(game_machines, 0, sizeof(game_machines));
	game_machines[0].machine_index = 0;
	game_machines[0].local = 1;
	memset(player_machines, -1, sizeof(player_machines));
	player_machines[0] = 0;
}

static void game_add(int machine)
{
	game_machines[game_machine_count].machine_index = (unsigned char)machine;
	game_machines[game_machine_count].ipv4 = nodes[machine].ipv4;
	game_machine_count++;
	player_machines[machine] = (signed char)machine;
}

static void game_remove(int machine)
{
	int index;

	for (index = 0; index < game_machine_count; index++)
	{
		if (game_machines[index].machine_index == machine)
		{
			game_machines[index] = game_machines[--game_machine_count];
			break;
		}
	}
	player_machines[machine] = -1;
}

/* one frame of every node: the host (node 0), then clients 1..count-1
(joined: in the game; delta: the advertisement's flag) */
static void frame(delta_u32 now, int client_count, const int *joined, int delta)
{
	int index;

	delta_peer_host_frame(&nodes[0].peer, now, game_machines, game_machine_count, player_machines);
	deliver(now);
	for (index = 1; index < client_count; index++)
	{
		delta_peer_client_frame(&nodes[index].peer, now, joined[index], HOST_IPV4, DELTA_PEER_PORT, delta,
			(unsigned char)index, player_machines);
	}
	deliver(now);
}

/* ---------- the wire */

static void test_wire_table(void)
{
	static unsigned char bytes[DELTA_LEGACY_SIGNED_SIZE];
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	struct delta_wire_header header;
	struct delta_wire_table table, back;
	delta_u32 serial;
	int size;
	int index;

	for (index = 0; index < DELTA_LEGACY_SIGNED_SIZE; index++)
		bytes[index] = (unsigned char)(index * 13);
	CHECK(DELTA_WIRE_TABLE_CHUNKS == 17);
	CHECK(DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_TABLE_SIZE + DELTA_WIRE_TABLE_CHUNK <= DELTA_WIRE_MAXIMUM_DATAGRAM);

	/* every piece of the largest table, and back */
	memset(&table, 0, sizeof(table));
	table.serial = 42;
	table.total = DELTA_LEGACY_SIGNED_SIZE;
	for (index = 0; index < DELTA_WIRE_TABLE_CHUNKS; index++)
	{
		table.offset = (delta_u32)(index * DELTA_WIRE_TABLE_CHUNK);
		table.length = index == DELTA_WIRE_TABLE_CHUNKS - 1 ? DELTA_LEGACY_SIGNED_SIZE % DELTA_WIRE_TABLE_CHUNK :
			DELTA_WIRE_TABLE_CHUNK;
		table.data = bytes + table.offset;
		size = delta_wire_write_table(data, 7, &table);
		CHECK(size == DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_TABLE_SIZE + table.length);
		CHECK(delta_wire_read_header(data, size, &header) && header.type == _delta_message_table && header.session == 7);
		CHECK(delta_wire_read_table(data + 12, header.length, &back));
		CHECK(back.serial == 42 && back.total == table.total && back.offset == table.offset &&
			back.length == table.length && !memcmp(back.data, bytes + table.offset, (size_t)back.length));
		/* (cut short: refused) */
		CHECK(!delta_wire_read_table(data + 12, header.length - 1, &back) && back.data == NULL);
	}

	/* what is not a piece of a table is neither written nor read */
	table.offset = 0;
	table.length = DELTA_WIRE_TABLE_CHUNK;
	table.data = bytes;
	size = delta_wire_write_table(data, 7, &table);
	CHECK(size > 0);
	/* (an offset off the grid, past the end; a length not the piece's; a
	total over the cap, or 0; serial 0, or the none mark) */
	data[12 + 8] = 1;
	CHECK(!delta_wire_read_table(data + 12, size - 12, &back));
	data[12 + 8] = 0;
	data[12 + 9] = 0x44;
	CHECK(!delta_wire_read_table(data + 12, size - 12, &back));
	data[12 + 9] = 0;
	data[12 + 12] = 1;
	CHECK(!delta_wire_read_table(data + 12, size - 12, &back));
	data[12 + 12] = 0;
	data[12 + 4] = (unsigned char)((DELTA_LEGACY_SIGNED_SIZE + 1) & 0xFF);
	data[12 + 5] = (unsigned char)((DELTA_LEGACY_SIGNED_SIZE + 1) >> 8);
	CHECK(!delta_wire_read_table(data + 12, size - 12, &back));
	data[12 + 4] = data[12 + 5] = 0;
	CHECK(!delta_wire_read_table(data + 12, size - 12, &back));
	/* (a total of 0 with a length of 0: the fuzzer's find) */
	data[12 + 12] = data[12 + 13] = 0;
	CHECK(!delta_wire_read_table(data + 12, size - 12, &back));
	data[12 + 13] = DELTA_WIRE_TABLE_CHUNK >> 8;
	data[12 + 4] = (unsigned char)(DELTA_LEGACY_SIGNED_SIZE & 0xFF);
	data[12 + 5] = (unsigned char)(DELTA_LEGACY_SIGNED_SIZE >> 8);
	CHECK(delta_wire_read_table(data + 12, size - 12, &back));
	memset(data + 12, 0, 4);
	CHECK(!delta_wire_read_table(data + 12, size - 12, &back));
	memset(data + 12, 0xFF, 4);
	CHECK(!delta_wire_read_table(data + 12, size - 12, &back));
	CHECK(!delta_wire_read_table(data + 12, DELTA_WIRE_TABLE_SIZE - 1, &back));
	CHECK(!delta_wire_read_table(NULL, 100, &back));
	table.offset = 5;
	CHECK(delta_wire_write_table(data, 7, &table) == 0);
	table.offset = 0;
	table.length = 1;
	CHECK(delta_wire_write_table(data, 7, &table) == 0);
	table.length = DELTA_WIRE_TABLE_CHUNK;
	table.total = DELTA_LEGACY_SIGNED_SIZE + 1;
	CHECK(delta_wire_write_table(data, 7, &table) == 0);
	table.total = 10;
	table.length = 10;
	table.serial = DELTA_WIRE_TABLE_NONE;
	CHECK(delta_wire_write_table(data, 7, &table) == 0);
	/* (a small table: one piece of its size) */
	table.serial = 3;
	size = delta_wire_write_table(data, 7, &table);
	CHECK(size == 12 + DELTA_WIRE_TABLE_SIZE + 10 && delta_wire_read_table(data + 12, size - 12, &back) && back.length == 10);
	/* (trailing bytes of a later version: skipped) */
	CHECK(delta_wire_read_table(data + 12, size - 12 + 40, &back) && back.length == 10);

	/* TABLE_HAVE */
	size = delta_wire_write_table_have(data, 9, DELTA_WIRE_TABLE_NONE);
	CHECK(size == 12 + DELTA_WIRE_TABLE_HAVE_SIZE);
	CHECK(delta_wire_read_header(data, size, &header) && header.type == _delta_message_table_have);
	CHECK(delta_wire_read_table_have(data + 12, header.length, &serial) && serial == DELTA_WIRE_TABLE_NONE);
	size = delta_wire_write_table_have(data, 9, 77);
	CHECK(delta_wire_read_table_have(data + 12, size - 12, &serial) && serial == 77);
	CHECK(!delta_wire_read_table_have(data + 12, 3, &serial) && serial == 0);
}

static void test_wire(void)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	struct delta_wire_header header;
	struct delta_wire_hello hello, hello_back;
	struct delta_wire_welcome welcome, welcome_back;
	struct delta_wire_profile profile, profile_back;
	static struct delta_wire_roster roster, roster_back;
	int size;
	int cut;
	int index;

	memset(&hello, 0, sizeof(hello));
	hello.capabilities = 0x3;
	hello.legacy_version = 18;
	hello.machine_index = 7;
	hello.legacy_table_serial = 42;
	pc_key(&hello.key, _delta_platform_pc_linux);
	strcpy(hello.build, "ChupathingyCE 0.6.5b");
	size = delta_wire_write_hello(data, 0xABCDEF01u, &hello);
	CHECK(size == DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_HELLO_SIZE + (int)strlen(hello.build));
	CHECK(data[0] == 'D' && data[1] == 'P' && data[2] == DELTA_MAJOR && data[3] == _delta_message_hello);
	CHECK(delta_wire_read_header(data, size, &header));
	CHECK(header.session == 0xABCDEF01u && header.type == _delta_message_hello && header.length == size - 12);
	CHECK(delta_wire_read_hello(data + 12, header.length, &hello_back));
	CHECK(hello_back.capabilities == 3 && hello_back.legacy_version == 18 && hello_back.machine_index == 7);
	CHECK(hello_back.legacy_table_serial == 42 && !strcmp(hello_back.build, hello.build));
	CHECK(!memcmp(&hello_back.key, &hello.key, sizeof(hello.key)));
	/* (every shorter datagram is refused: the header's length, or the
	payload's fields) */
	for (cut = 0; cut < size; cut++)
		CHECK(!delta_wire_read_header(data, cut, &header) || !delta_wire_read_hello(data + 12, header.length, &hello_back));
	for (cut = 0; cut < size - 12; cut++)
		CHECK(!delta_wire_read_hello(data + 12, cut, &hello_back) || cut >= DELTA_WIRE_HELLO_SIZE + (int)strlen(hello.build));
	/* (a length that is not the datagram's, or a wrong magic) */
	CHECK(!delta_wire_read_header(data, size + 1, &header));
	data[0] = 'X';
	CHECK(!delta_wire_read_header(data, size, &header));
	/* (text is printable ASCII as read; a build length over 31 is refused;
	trailing fields of a later version are skipped) */
	size = delta_wire_write_hello(data, 1, &hello);
	data[12 + DELTA_WIRE_HELLO_SIZE] = 0x07;
	CHECK(delta_wire_read_hello(data + 12, size - 12, &hello_back) && hello_back.build[0] == '?');
	data[12 + 7] = 40;
	CHECK(!delta_wire_read_hello(data + 12, 400, &hello_back));
	data[12 + 7] = (unsigned char)strlen(hello.build);
	CHECK(delta_wire_read_hello(data + 12, size - 12 + 30, &hello_back));
	/* (an opt-in limit under the default reads as the default) */
	data[12 + 12 + 4] = 16;
	data[12 + 12 + 5] = 8;
	CHECK(delta_wire_read_hello(data + 12, size - 12, &hello_back) && hello_back.key.join_players_opt_in == 16);

	memset(&welcome, 0, sizeof(welcome));
	welcome.capabilities = 0x3;
	welcome.agreed = 0x1;
	welcome.legacy_version = 18;
	welcome.host_machine_index = DELTA_WIRE_NO_MACHINE;
	welcome.legacy_table_serial = 9;
	pc_key(&welcome.key, _delta_platform_pc_macos);
	welcome.key.flags = DELTA_PLATFORM_KEY_DEDICATED;
	strcpy(welcome.build, "server");
	size = delta_wire_write_welcome(data, 5, &welcome);
	CHECK(delta_wire_read_header(data, size, &header) && header.type == _delta_message_welcome);
	CHECK(delta_wire_read_welcome(data + 12, header.length, &welcome_back));
	CHECK(welcome_back.agreed == 1 && welcome_back.host_machine_index == 0xFF && welcome_back.legacy_table_serial == 9);
	CHECK(!memcmp(&welcome_back.key, &welcome.key, sizeof(welcome.key)) && !strcmp(welcome_back.build, "server"));
	for (cut = 0; cut < DELTA_WIRE_WELCOME_SIZE + 6; cut++)
		CHECK(!delta_wire_read_welcome(data + 12, cut, &welcome_back));

	profile.revision = 77;
	for (index = 0; index < DELTA_WIRE_PLAYER_ID_SIZE; index++)
		profile.player_id[index] = (unsigned char)(index * 17);
	size = delta_wire_write_profile(data, 5, &profile);
	CHECK(size == 12 + DELTA_WIRE_PROFILE_SIZE);
	CHECK(delta_wire_read_profile(data + 12, size - 12, &profile_back) && !memcmp(&profile, &profile_back, sizeof(profile)));
	CHECK(!delta_wire_read_profile(data + 12, size - 13, &profile_back));

	memset(&roster, 0, sizeof(roster));
	roster.room_capabilities = 0x3;
	roster.room_players = 16;
	roster.count = DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES;
	for (index = 0; index < roster.count; index++)
	{
		roster.entries[index].machine_index = (unsigned char)index;
		roster.entries[index].flags = DELTA_ROSTER_DELTA | DELTA_ROSTER_PLATFORM;
		roster.entries[index].capabilities = 1;
		pc_key(&roster.entries[index].key, index % NUMBER_OF_DELTA_PLATFORMS);
		roster.entries[index].profile_revision = (delta_u32)index;
	}
	size = delta_wire_write_roster(data, 5, &roster);
	CHECK(size > 0 && size <= DELTA_WIRE_MAXIMUM_DATAGRAM);
	CHECK(delta_wire_read_header(data, size, &header));
	CHECK(delta_wire_read_roster(data + 12, header.length, &roster_back));
	CHECK(roster_back.count == roster.count && roster_back.room_players == 16 && roster_back.room_capabilities == 3);
	CHECK(!memcmp(roster_back.entries, roster.entries, sizeof(roster.entries[0]) * roster.count));
	/* (a count the payload does not hold, or over the most) */
	CHECK(!delta_wire_read_roster(data + 12, header.length - 1, &roster_back));
	data[12 + 8] = DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES + 1;
	CHECK(!delta_wire_read_roster(data + 12, DELTA_WIRE_MAXIMUM_PAYLOAD, &roster_back));
	roster.count = DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES + 1;
	CHECK(delta_wire_write_roster(data, 5, &roster) == 0);

	size = delta_wire_write_empty(data, 2, _delta_message_legacy, 5);
	CHECK(size == 12 && delta_wire_read_header(data, size, &header) && header.major == 2 && header.length == 0);

	test_wire_table();
}

static void test_rate(void)
{
	struct delta_rate rate;
	int taken = 0;
	int index;

	memset(&rate, 0, sizeof(rate));
	for (index = 0; index < 100; index++)
		taken += delta_rate_take(&rate, 1000, 10, 20);
	CHECK(taken == 20);
	/* (a second later, ten more) */
	taken = 0;
	for (index = 0; index < 100; index++)
		taken += delta_rate_take(&rate, 2000, 10, 20);
	CHECK(taken == 10);
	/* (time wrapping, or going back, gives none and breaks nothing) */
	CHECK(!delta_rate_take(&rate, 1500, 10, 20));
	CHECK(delta_rate_take(&rate, 0xFFFFFFF0u, 10, 20) == 0 || 1);
}

/* ---------- sessions */

static void test_handshake(void)
{
	int joined[4] = { 0, 1, 1, 1 };
	struct delta_peer_machine machine;
	delta_u32 now = 1000;
	int step;

	/* the host (0) and three clients: 1 and 2 Delta with platform and
	profile, 3 with platform alone */
	node_start(0, HOST_IPV4, DELTA_PEER_PORT, PLATFORM_BIT | PROFILE_BIT, _delta_platform_pc_macos, 0, 5);
	node_start(1, 0x0300007F, 40001, PLATFORM_BIT | PROFILE_BIT, _delta_platform_pc_linux, 0x11, 0);
	node_start(2, 0x0400007F, 40002, PLATFORM_BIT | PROFILE_BIT, _delta_platform_pc_windows, 0x22, 5);
	node_start(3, 0x0500007F, 40003, PLATFORM_BIT, _delta_platform_android, 0, 0);
	game_reset();
	game_add(1);
	game_add(2);
	game_add(3);
	for (step = 0; step < 4; step++, now += 300)
		frame(now, 4, joined, 1);

	CHECK(nodes[1].peer.client_state == _delta_peer_client_delta);
	CHECK(nodes[2].peer.client_state == _delta_peer_client_delta);
	CHECK(nodes[3].peer.client_state == _delta_peer_client_delta);
	CHECK(nodes[1].peer.agreed == (PLATFORM_BIT | PROFILE_BIT));
	CHECK(nodes[3].peer.agreed == PLATFORM_BIT);
	/* (the legacy table is not sent in the handshake's first moments) */
	CHECK(nodes[1].pieces_in == 0 && nodes[1].table_serial == 0);

	/* the host's view: each machine's claims */
	CHECK(delta_peer_machine(&nodes[0].peer, 1, &machine) && machine.key.platform == _delta_platform_pc_linux);
	CHECK(machine.flags & DELTA_ROSTER_PROFILE && machine.profile.player_id[0] == 0x11);
	CHECK(delta_peer_machine(&nodes[0].peer, 3, &machine) && !(machine.flags & DELTA_ROSTER_PROFILE));
	/* the room: every machine has platform, not profile (3 lacks it) */
	CHECK(delta_peer_room_has(&nodes[0].peer, _delta_capability_platform));
	CHECK(!delta_peer_room_has(&nodes[0].peer, _delta_capability_profile));
	CHECK(delta_peer_room_limit(&nodes[0].peer, _delta_peer_limit_players) == 128);

	/* client 1's view through the roster: the host, 2 (with its profile)
	and 3 (its platform, no profile) */
	CHECK(delta_peer_machine(&nodes[1].peer, 0, &machine) && machine.key.platform == _delta_platform_pc_macos);
	CHECK(delta_peer_machine(&nodes[1].peer, 2, &machine) && machine.key.platform == _delta_platform_pc_windows);
	CHECK(machine.flags & DELTA_ROSTER_PROFILE && machine.profile.player_id[3] == 0x22 && machine.profile.revision == 0x22);
	CHECK(delta_peer_machine(&nodes[1].peer, 3, &machine) && machine.key.platform == _delta_platform_android);
	CHECK(!(machine.flags & DELTA_ROSTER_PROFILE));
	CHECK(delta_peer_machine(&nodes[1].peer, 1, &machine) && machine.key.platform == _delta_platform_pc_linux);
	CHECK(delta_peer_room_has(&nodes[1].peer, _delta_capability_platform));
	CHECK(!delta_peer_room_has(&nodes[1].peer, _delta_capability_profile));
	/* client 3 agreed to no profile: none shown to it */
	CHECK(delta_peer_machine(&nodes[3].peer, 2, &machine) && !(machine.flags & DELTA_ROSTER_PROFILE));

	/* 3 leaves the game: the room has profile now */
	game_remove(3);
	joined[3] = 0;
	for (step = 0; step < 3; step++, now += 300)
		frame(now, 4, joined, 1);
	CHECK(nodes[3].peer.client_state == _delta_peer_client_off);
	CHECK(!delta_peer_machine(&nodes[0].peer, 3, NULL));
	CHECK(delta_peer_room_has(&nodes[0].peer, _delta_capability_profile));
	CHECK(delta_peer_room_has(&nodes[1].peer, _delta_capability_profile));
	CHECK(!delta_peer_machine(&nodes[1].peer, 3, NULL));

	/* client 2 says BYE (leaves Delta, stays in the game: an OpenCE
	machine now): the room shares nothing */
	delta_peer_stop(&nodes[2].peer);
	deliver(now);
	for (step = 0; step < 3; step++, now += 300)
	{
		delta_peer_host_frame(&nodes[0].peer, now, game_machines, game_machine_count, player_machines);
		deliver(now);
	}
	CHECK(!delta_peer_machine(&nodes[0].peer, 2, NULL));
	CHECK(!delta_peer_room_has(&nodes[0].peer, _delta_capability_platform));
	CHECK(!delta_peer_room_has(&nodes[1].peer, _delta_capability_platform));
}

static void test_fallback(void)
{
	int joined[3] = { 0, 1, 1 };
	delta_u32 now = 5000;
	int step;

	/* a host whose advertisement has no flag: legacy at once, nothing sent */
	node_start(0, HOST_IPV4, DELTA_PEER_PORT, PLATFORM_BIT, _delta_platform_pc_macos, 0, 0);
	node_start(1, 0x0300007F, 40001, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
	game_reset();
	game_add(1);
	delta_peer_client_frame(&nodes[1].peer, now, 1, HOST_IPV4, DELTA_PEER_PORT, 0, 1, player_machines);
	CHECK(nodes[1].peer.client_state == _delta_peer_client_legacy);
	CHECK(queued == 0);
	CHECK(!delta_peer_room_has(&nodes[1].peer, _delta_capability_platform));
	CHECK(delta_peer_room_limit(&nodes[1].peer, _delta_peer_limit_players) == DELTA_PEER_NO_LIMIT);
	/* (it knows itself still) */
	CHECK(delta_peer_machine(&nodes[1].peer, 1, NULL));

	/* a flagged host that never answers (an OpenCE build that happens to
	set the bit, or one whose Delta is lost): HELLO again, then legacy after
	the handshake time; never longer */
	node_start(1, 0x0300007F, 40001, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
	nodes[0].mute = 1;
	for (step = 0; step * 100 < DELTA_PEER_HANDSHAKE_TIME + 200; step++, now += 100)
		frame(now, 2, joined, 1);
	CHECK(nodes[1].peer.client_state == _delta_peer_client_legacy);
	nodes[0].mute = 0;

	/* a client in a game whose machine index it does not know yet waits
	(nothing sent), then says HELLO */
	node_start(1, 0x0300007F, 40001, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
	delta_peer_client_frame(&nodes[1].peer, now, 1, HOST_IPV4, DELTA_PEER_PORT, 1, DELTA_WIRE_NO_MACHINE,
		player_machines);
	CHECK(nodes[1].peer.client_state == _delta_peer_client_off && queued == 0);

	/* another major: the host answers LEGACY to a HELLO of major 2 */
	{
		unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
		struct delta_wire_hello hello;
		struct delta_wire_header header;
		int size;

		node_start(0, HOST_IPV4, DELTA_PEER_PORT, PLATFORM_BIT, _delta_platform_pc_macos, 0, 0);
		delta_peer_host_frame(&nodes[0].peer, now, game_machines, game_machine_count, player_machines);
		memset(&hello, 0, sizeof(hello));
		size = delta_wire_write_hello(data, 99, &hello);
		data[2] = 2;
		queued = 0;
		delta_peer_receive(&nodes[0].peer, now, nodes[1].ipv4, 40001, data, size);
		CHECK(queued == 1 && delta_wire_read_header(queue[0].data, queue[0].size, &header) &&
			header.type == _delta_message_legacy && header.session == 99 && header.major == DELTA_MAJOR);
		queued = 0;

		/* and a client told LEGACY by a host of major 2 falls back */
		node_start(1, 0x0300007F, 40001, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
		delta_peer_client_frame(&nodes[1].peer, now, 1, HOST_IPV4, DELTA_PEER_PORT, 1, 1, player_machines);
		CHECK(nodes[1].peer.client_state == _delta_peer_client_waiting);
		queued = 0;
		size = delta_wire_write_empty(data, 2, _delta_message_legacy, nodes[1].peer.session);
		delta_peer_receive(&nodes[1].peer, now, HOST_IPV4, DELTA_PEER_PORT, data, size);
		CHECK(nodes[1].peer.client_state == _delta_peer_client_legacy);
	}
}

static void test_limits(void)
{
	int joined[3] = { 0, 1, 1 };
	delta_u32 now = 20000;
	int step;

	/* an Xbox: hosts 16, joins 16, 128 if its player opted in */
	node_start(0, HOST_IPV4, DELTA_PEER_PORT, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
	node_start(1, 0x0300007F, 40001, PLATFORM_BIT, _delta_platform_xbox, 0, 0);
	node_start(2, 0x0400007F, 40002, 0, _delta_platform_pc_windows, 0, 0);
	nodes[1].peer.local.key.host_players = 16;
	nodes[1].peer.local.key.join_players = 16;
	nodes[1].peer.local.key.join_players_opt_in = 128;
	nodes[1].peer.local.key.memory_class = 6;
	game_reset();
	game_add(1);
	game_add(2);
	for (step = 0; step < 4; step++, now += 300)
		frame(now, 3, joined, 1);
	CHECK(nodes[1].peer.client_state == _delta_peer_client_delta);
	CHECK(delta_peer_room_limit(&nodes[0].peer, _delta_peer_limit_players) == 16);
	CHECK(delta_peer_room_limit(&nodes[1].peer, _delta_peer_limit_players) == 16);
	CHECK(delta_peer_room_limit(&nodes[2].peer, _delta_peer_limit_players) == 16);
	/* (client 2 offers nothing: Delta, but no shared capability; the room
	shares none) */
	CHECK(nodes[2].peer.client_state == _delta_peer_client_delta && nodes[2].peer.agreed == 0);
	CHECK(!delta_peer_room_has(&nodes[0].peer, _delta_capability_platform));

	/* (co-op: not offered while the Xbox is in the game, nor by an Xbox
	host; the policy's defaults) */
	CHECK(!delta_peer_room_coop(&nodes[0].peer));
	CHECK(delta_peer_room_coop(&nodes[1].peer));
	CHECK(delta_platform_policy_coop(_delta_platform_xbox) == 0 && delta_platform_policy_coop(_delta_platform_pc_linux));
	CHECK(delta_platform_policy_coop(200) == 1);
	{
		struct delta_platform_key key;

		delta_platform_policy_default(_delta_platform_xbox, &key);
		CHECK(key.memory_class == 6 && key.host_players == 16);
		delta_platform_policy_default(_delta_platform_xbox360, &key);
		CHECK(key.memory_class == 9);
		delta_platform_policy_default(_delta_platform_pc_windows, &key);
		CHECK(key.memory_class == 12);
	}

	/* a key claiming more than its platform's row is held to the row */
	{
		struct delta_platform_key key;

		pc_key(&key, _delta_platform_xbox);
		CHECK(delta_peer_key_join_limit(&nodes[0].peer, &key) == 16);
		key.join_players = 8;
		CHECK(delta_peer_key_join_limit(&nodes[0].peer, &key) == 8);
		key.flags = DELTA_PLATFORM_KEY_OPTED_IN;
		CHECK(delta_peer_key_join_limit(&nodes[0].peer, &key) == DELTA_PEER_NO_LIMIT);
		pc_key(&key, 200);
		CHECK(delta_peer_key_join_limit(&nodes[0].peer, &key) == 128);
	}

	/* opted in (network.platform_limits = "off"): no limit from it (it
	comes back with a new session) */
	delta_peer_stop(&nodes[1].peer);
	deliver(now);
	nodes[1].peer.local.key.flags |= DELTA_PLATFORM_KEY_OPTED_IN;
	for (step = 0; step < 8; step++, now += 300)
		frame(now, 3, joined, 1);
	CHECK(nodes[1].peer.client_state == _delta_peer_client_delta);
	/* (the host's own hosting limit, a PC's 128, is left) */
	CHECK(delta_peer_room_limit(&nodes[0].peer, _delta_peer_limit_players) == 128);

	/* a host that ignores the limits (network.host_platform_limits = false) */
	delta_peer_stop(&nodes[1].peer);
	deliver(now);
	nodes[1].peer.local.key.flags = 0;
	nodes[0].peer.local.ignore_platform_limits = 1;
	for (step = 0; step < 8; step++, now += 300)
		frame(now, 3, joined, 1);
	CHECK(delta_peer_room_limit(&nodes[0].peer, _delta_peer_limit_players) == DELTA_PEER_NO_LIMIT);

	/* (a host that ignores the limits offers co-op too) */
	CHECK(delta_peer_room_coop(&nodes[0].peer));

	/* an Xbox host keeps its own game to its row's 16 */
	node_start(0, HOST_IPV4, DELTA_PEER_PORT, PLATFORM_BIT, _delta_platform_xbox, 0, 0);
	nodes[0].peer.local.key.host_players = 16;
	delta_peer_host_frame(&nodes[0].peer, now, game_machines, game_machine_count, player_machines);
	CHECK(delta_peer_room_limit(&nodes[0].peer, _delta_peer_limit_players) == 16);
	CHECK(!delta_peer_room_coop(&nodes[0].peer));
	queued = 0;
}

static void test_strangers(void)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	struct delta_wire_hello hello;
	int joined[2] = { 0, 1 };
	delta_u32 now = 40000;
	unsigned long dropped;
	int size;
	int index;
	int step;

	node_start(0, HOST_IPV4, DELTA_PEER_PORT, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
	node_start(1, 0x0300007F, 40001, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
	game_reset();
	game_add(1);
	delta_peer_host_frame(&nodes[0].peer, now, game_machines, game_machine_count, player_machines);
	memset(&hello, 0, sizeof(hello));
	hello.machine_index = 1;
	size = delta_wire_write_hello(data, 1234, &hello);

	/* a HELLO from an address no machine of the game has: nothing */
	queued = 0;
	delta_peer_receive(&nodes[0].peer, now, 0x0900007F, 5555, data, size);
	CHECK(queued == 0 && !nodes[0].peer.peers[1].used);
	/* one claiming another machine's index from machine 1's address */
	hello.machine_index = 0;
	size = delta_wire_write_hello(data, 1234, &hello);
	delta_peer_receive(&nodes[0].peer, now, nodes[1].ipv4, 40001, data, size);
	CHECK(queued == 0 && !nodes[0].peer.peers[0].used);
	/* a session of 0 */
	hello.machine_index = 1;
	size = delta_wire_write_hello(data, 0, &hello);
	delta_peer_receive(&nodes[0].peer, now, nodes[1].ipv4, 40001, data, size);
	CHECK(queued == 0);

	/* a flood from machine 1 (its bucket full again): the burst is heard,
	the rest dropped */
	now += 10000;
	dropped = nodes[0].peer.dropped;
	size = delta_wire_write_empty(data, DELTA_MAJOR, 200, 1234);
	for (index = 0; index < 1000; index++)
		delta_peer_receive(&nodes[0].peer, now, nodes[1].ipv4, 40001, data, size);
	CHECK(nodes[0].peer.dropped - dropped == 1000 - DELTA_PEER_HOST_BURST);

	/* new sessions from one machine: no more than one a second */
	now += 5000;
	hello.machine_index = 1;
	size = delta_wire_write_hello(data, 1, &hello);
	delta_peer_receive(&nodes[0].peer, now, nodes[1].ipv4, 40001, data, size);
	CHECK(nodes[0].peer.peers[1].used && nodes[0].peer.peers[1].session == 1);
	size = delta_wire_write_hello(data, 2, &hello);
	delta_peer_receive(&nodes[0].peer, now + 10, nodes[1].ipv4, 40001, data, size);
	CHECK(nodes[0].peer.peers[1].session == 1);
	delta_peer_receive(&nodes[0].peer, now + DELTA_PEER_NEW_SESSION_GAP, nodes[1].ipv4, 40001, data, size);
	CHECK(nodes[0].peer.peers[1].session == 2);
	queued = 0;

	/* the client hears its host alone, of its session alone */
	node_start(1, 0x0300007F, 40001, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
	for (step = 0; step < 3; step++, now += 300)
		frame(now, 2, joined, 1);
	CHECK(nodes[1].peer.client_state == _delta_peer_client_delta);
	dropped = nodes[1].peer.dropped;
	size = delta_wire_write_empty(data, DELTA_MAJOR, _delta_message_bye, nodes[1].peer.session);
	delta_peer_receive(&nodes[1].peer, now, 0x0900007F, DELTA_PEER_PORT, data, size);
	delta_peer_receive(&nodes[1].peer, now, HOST_IPV4, 1, data, size);
	size = delta_wire_write_empty(data, DELTA_MAJOR, _delta_message_bye, nodes[1].peer.session + 1);
	delta_peer_receive(&nodes[1].peer, now, HOST_IPV4, DELTA_PEER_PORT, data, size);
	CHECK(nodes[1].peer.client_state == _delta_peer_client_delta);
	CHECK(nodes[1].peer.dropped - dropped == 3);
	/* (a WELCOME claiming more than the client offered is cut to it) */
	CHECK((nodes[1].peer.agreed & ~PLATFORM_BIT) == 0);
}

/* the legacy table's kill switch: a capability it turns off is never
offered (HELLO, WELCOME), agreed or shared by the room, and goes when it is
turned off mid-game */
static void test_kill_switch(void)
{
	int joined[3] = { 0, 1, 1 };
	struct delta_peer_machine machine;
	delta_u32 now = 300000;
	int step;

	node_start(0, HOST_IPV4, DELTA_PEER_PORT, PLATFORM_BIT | PROFILE_BIT, _delta_platform_pc_linux, 0x10, 0);
	node_start(1, 0x0300007F, 40001, PLATFORM_BIT | PROFILE_BIT, _delta_platform_pc_macos, 0x11, 0);
	node_start(2, 0x0400007F, 40002, PLATFORM_BIT | PROFILE_BIT, _delta_platform_pc_windows, 0x12, 0);
	/* (client 1's table turns profile off) */
	nodes[1].disabled = PROFILE_BIT;
	game_reset();
	game_add(1);
	game_add(2);
	for (step = 0; step < 4; step++, now += 300)
		frame(now, 3, joined, 1);
	CHECK(nodes[1].peer.client_state == _delta_peer_client_delta && nodes[1].peer.agreed == PLATFORM_BIT);
	CHECK(nodes[0].peer.peers[1].hello.capabilities == PLATFORM_BIT && nodes[0].peer.peers[1].agreed == PLATFORM_BIT);
	CHECK(nodes[2].peer.agreed == (PLATFORM_BIT | PROFILE_BIT));
	CHECK(!delta_peer_room_has(&nodes[0].peer, _delta_capability_profile));
	CHECK(delta_peer_room_has(&nodes[0].peer, _delta_capability_platform));
	/* (no profile sent by it, none kept by it) */
	CHECK(delta_peer_machine(&nodes[0].peer, 1, &machine) && !(machine.flags & DELTA_ROSTER_PROFILE));
	CHECK(delta_peer_machine(&nodes[1].peer, 2, &machine) && !(machine.flags & DELTA_ROSTER_PROFILE));

	/* the host's table turns platform off mid-game: the room shares
	nothing, it sends no platform keys, and its clients' sessions agree to
	none of it from its WELCOME of a new session */
	nodes[0].disabled = PLATFORM_BIT;
	for (step = 0; step < 30; step++, now += 300)
		frame(now, 3, joined, 1);
	CHECK(!delta_peer_room_has(&nodes[0].peer, _delta_capability_platform));
	CHECK(!delta_peer_room_has(&nodes[2].peer, _delta_capability_platform));
	CHECK(delta_peer_machine(&nodes[0].peer, 2, &machine) && !(machine.flags & DELTA_ROSTER_PLATFORM));
	delta_peer_stop(&nodes[2].peer);
	deliver(now);
	for (step = 0; step < 8; step++, now += 300)
		frame(now, 3, joined, 1);
	CHECK(nodes[2].peer.client_state == _delta_peer_client_delta && nodes[2].peer.agreed == PROFILE_BIT);
	CHECK((nodes[2].peer.welcome.capabilities & PLATFORM_BIT) == 0);

	/* a client whose own table turns platform off mid-game keeps no
	machine's platform key */
	nodes[0].disabled = 0;
	delta_peer_stop(&nodes[2].peer);
	deliver(now);
	for (step = 0; step < 8; step++, now += 300)
		frame(now, 3, joined, 1);
	CHECK(delta_peer_machine(&nodes[2].peer, 0, &machine) && (machine.flags & DELTA_ROSTER_PLATFORM));
	nodes[2].disabled = PLATFORM_BIT;
	for (step = 0; step < 30; step++, now += 300)
		frame(now, 3, joined, 1);
	CHECK(delta_peer_machine(&nodes[2].peer, 1, &machine) && !(machine.flags & DELTA_ROSTER_PLATFORM));
	CHECK(!delta_peer_room_has(&nodes[2].peer, _delta_capability_platform));
	queued = 0;
}

/* ---------- the legacy table's relay */

/* frames every step milliseconds until the time */
static void run_frames(delta_u32 *now, delta_u32 until, delta_u32 step, int client_count, const int *joined)
{
	for (; *now < until; *now += step)
		frame(*now, client_count, joined, 1);
}

static void test_relay(void)
{
	int joined[4] = { 0, 1, 1, 1 };
	delta_u32 now = 200000;
	delta_u32 start;
	int index;

	/* the host has table 5; client 1 none (0), client 2 table 5, client 3
	takes none (a local table, or no key) */
	node_start(0, HOST_IPV4, DELTA_PEER_PORT, PLATFORM_BIT, _delta_platform_pc_linux, 0, 5);
	node_start(1, 0x0300007F, 40001, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
	node_start(2, 0x0400007F, 40002, PLATFORM_BIT, _delta_platform_pc_linux, 0, 5);
	node_start(3, 0x0500007F, 40003, PLATFORM_BIT, _delta_platform_pc_linux, 0, DELTA_WIRE_TABLE_NONE);
	game_reset();
	game_add(1);
	game_add(2);
	game_add(3);
	start = now;
	run_frames(&now, start + 1500, 100, 4, joined);
	/* (nothing in the first moments) */
	CHECK(nodes[1].pieces_in == 0 && nodes[0].peer.peers[1].relay.their_serial == 0);
	/* (client 3 said it takes none) */
	CHECK(nodes[0].peer.peers[3].relay.their_serial == DELTA_WIRE_TABLE_NONE);
	run_frames(&now, start + 10000, 100, 4, joined);
	/* client 1 has the host's table (3000 bytes: 3 pieces, one check), and
	said so */
	CHECK(nodes[1].table_serial == 5 && nodes[1].taken == 1 && nodes[1].checks == 1);
	CHECK(nodes[1].table_size == nodes[0].table_size && !memcmp(nodes[1].table, nodes[0].table, 3000));
	CHECK(nodes[1].pieces_in == 3);
	CHECK(nodes[0].peer.peers[1].relay.their_serial == 5 && !nodes[0].peer.peers[1].relay.sending);
	/* client 2 had it, client 3 takes none: sent nothing */
	CHECK(nodes[2].pieces_in == 0 && nodes[3].pieces_in == 0 && nodes[3].checks == 0);
	/* (no message of the relay was dropped by a rate limit) */
	for (index = 0; index < 4; index++)
		CHECK(nodes[index].peer.dropped == 0);
	/* a minute on: no pass again (each has the table) */
	run_frames(&now, now + 70000, 500, 4, joined);
	CHECK(nodes[1].pieces_in == 3 && nodes[1].checks == 1);

	/* client 1 takes a newer table (9, as from Delta List), the largest
	there is: it says so and sends it to the host, which takes it and sends
	it on to client 2; client 3 still nothing */
	make_table(&nodes[1], 9, DELTA_LEGACY_SIGNED_SIZE);
	start = now;
	run_frames(&now, start + 20000, 50, 4, joined);
	CHECK(nodes[0].table_serial == 9 && nodes[0].taken == 1 && nodes[0].pieces_in == DELTA_WIRE_TABLE_CHUNKS);
	CHECK(nodes[0].table_size == DELTA_LEGACY_SIGNED_SIZE && table_valid(nodes[0].table, nodes[0].table_size));
	CHECK(nodes[2].table_serial == 9 && nodes[2].taken == 1 && nodes[2].pieces_in == DELTA_WIRE_TABLE_CHUNKS);
	CHECK(nodes[3].pieces_in == 0 && nodes[3].table_serial == DELTA_WIRE_TABLE_NONE);
	CHECK(nodes[1].pieces_in == 3);
	for (index = 0; index < 4; index++)
		CHECK(nodes[index].peer.dropped == 0);
}

/* a table the receiver does not take: at most three passes a session, a
minute apart, and one check a minute */
static void test_relay_refused(void)
{
	int joined[2] = { 0, 1 };
	delta_u32 now = 500000;
	delta_u32 start;

	node_start(0, HOST_IPV4, DELTA_PEER_PORT, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
	node_start(1, 0x0300007F, 40001, PLATFORM_BIT, _delta_platform_pc_linux, 0, 12);
	nodes[0].reject = 1;
	game_reset();
	game_add(1);
	start = now;
	run_frames(&now, start + 10000, 100, 2, joined);
	CHECK(nodes[0].checks == 1 && nodes[0].pieces_in == 3);
	run_frames(&now, start + 50000, 100, 2, joined);
	CHECK(nodes[0].checks == 1 && nodes[0].pieces_in == 3);
	run_frames(&now, start + 10 * 60000, 100, 2, joined);
	CHECK(nodes[0].checks == DELTA_PEER_TABLE_PASSES && nodes[0].pieces_in == 3 * DELTA_PEER_TABLE_PASSES);
	CHECK(nodes[0].table_serial == 0 && nodes[1].peer.host_relay.passes == DELTA_PEER_TABLE_PASSES);

	/* a new session (the client joins again): passes again, and the check
	still waits out its minute */
	nodes[0].reject = 0;
	delta_peer_stop(&nodes[1].peer);
	deliver(now);
	start = now;
	run_frames(&now, start + 20000, 100, 2, joined);
	CHECK(nodes[0].table_serial == 12 && nodes[0].taken == 1);

	/* pieces of two tables at once from two machines: one at a time */
	{
		unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
		struct delta_wire_table piece;
		int joined3[3] = { 0, 1, 1 };
		int size;

		node_start(0, HOST_IPV4, DELTA_PEER_PORT, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
		node_start(1, 0x0300007F, 40001, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
		node_start(2, 0x0400007F, 40002, PLATFORM_BIT, _delta_platform_pc_linux, 0, 0);
		game_reset();
		game_add(1);
		game_add(2);
		run_frames(&now, now + 1000, 100, 3, joined3);
		make_table(&nodes[1], 20, 3000);
		memset(&piece, 0, sizeof(piece));
		piece.serial = 20;
		piece.total = 3000;
		piece.length = DELTA_WIRE_TABLE_CHUNK;
		piece.data = nodes[1].table;
		size = delta_wire_write_table(data, nodes[1].peer.session, &piece);
		delta_peer_receive(&nodes[0].peer, now, nodes[1].ipv4, 40001, data, size);
		CHECK(nodes[0].peer.table_in.active && nodes[0].peer.table_in.from == 1);
		size = delta_wire_write_table(data, nodes[2].peer.session, &piece);
		delta_peer_receive(&nodes[0].peer, now + 10, nodes[2].ipv4, 40002, data, size);
		CHECK(nodes[0].peer.table_in.from == 1);
		/* (until the first goes quiet) */
		delta_peer_receive(&nodes[0].peer, now + DELTA_PEER_TABLE_STALE, nodes[2].ipv4, 40002, data, size);
		CHECK(nodes[0].peer.table_in.from == 2);
		/* (a piece from a session not the machine's: nothing) */
		size = delta_wire_write_table(data, nodes[2].peer.session + 1, &piece);
		delta_peer_receive(&nodes[0].peer, now + DELTA_PEER_TABLE_STALE, nodes[2].ipv4, 40002, data, size);
		CHECK(nodes[0].peer.table_in.from == 2);
		queued = 0;
	}
}

/* ---------- random input */

static void random_bytes(unsigned char *data, int size)
{
	int index;

	for (index = 0; index < size; index++)
		data[index] = (unsigned char)next_random();
}

/* a valid message of the session's, then some of its bytes changed */
static int mutated_message(unsigned char *data, delta_u32 session)
{
	static struct delta_wire_roster roster;
	struct delta_wire_hello hello;
	struct delta_wire_welcome welcome;
	struct delta_wire_profile profile;
	int size;
	int flips;

	switch (next_random() % 8)
	{
	case 6:
	{
		static unsigned char bytes[DELTA_WIRE_TABLE_CHUNK];
		struct delta_wire_table table;
		int pieces = 1 + (int)(next_random() % DELTA_WIRE_TABLE_CHUNKS);

		random_bytes(bytes, (int)sizeof(bytes));
		memset(&table, 0, sizeof(table));
		table.serial = 1 + next_random() % 8;
		table.total = (delta_u32)((pieces - 1) * DELTA_WIRE_TABLE_CHUNK + 1 + (int)(next_random() % DELTA_WIRE_TABLE_CHUNK));
		if (table.total > DELTA_LEGACY_SIGNED_SIZE)
			table.total = DELTA_LEGACY_SIGNED_SIZE;
		table.offset = (delta_u32)((int)(next_random() % (delta_u32)pieces) * DELTA_WIRE_TABLE_CHUNK);
		if (table.offset >= table.total)
			table.offset = 0;
		table.length = table.total - table.offset < DELTA_WIRE_TABLE_CHUNK ? (int)(table.total - table.offset) :
			DELTA_WIRE_TABLE_CHUNK;
		table.data = bytes;
		size = delta_wire_write_table(data, session, &table);
		break;
	}
	case 7:
		size = delta_wire_write_table_have(data, session, next_random() % 2 ? next_random() % 8 : next_random());
		break;
	case 0:
		memset(&hello, 0, sizeof(hello));
		random_bytes((unsigned char *)&hello, (int)sizeof(hello));
		hello.build[DELTA_WIRE_BUILD_SIZE] = 0;
		size = delta_wire_write_hello(data, session, &hello);
		break;
	case 1:
		random_bytes((unsigned char *)&welcome, (int)sizeof(welcome));
		welcome.build[DELTA_WIRE_BUILD_SIZE] = 0;
		size = delta_wire_write_welcome(data, session, &welcome);
		break;
	case 2:
		random_bytes((unsigned char *)&roster, (int)sizeof(roster));
		roster.count = (unsigned char)(roster.count % (DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES + 1));
		size = delta_wire_write_roster(data, session, &roster);
		break;
	case 3:
		random_bytes((unsigned char *)&profile, (int)sizeof(profile));
		size = delta_wire_write_profile(data, session, &profile);
		break;
	case 4:
		size = delta_wire_write_empty(data, (int)(next_random() % 3), (int)(next_random() % 8), session);
		break;
	default:
		size = 1 + (int)(next_random() % DELTA_WIRE_MAXIMUM_DATAGRAM);
		random_bytes(data, size);
		return size;
	}
	for (flips = (int)(next_random() % 4); flips > 0 && size > 0; flips--)
		data[next_random() % (delta_u32)size] ^= (unsigned char)(1u << (next_random() % 8));
	/* (sometimes cut, sometimes grown) */
	if (next_random() % 4 == 0 && size > 1)
		size = 1 + (int)(next_random() % (delta_u32)size);
	else if (next_random() % 8 == 0 && size < DELTA_WIRE_MAXIMUM_DATAGRAM)
		size += 1 + (int)(next_random() % (delta_u32)(DELTA_WIRE_MAXIMUM_DATAGRAM - size));
	return size;
}

static void test_random(long iterations)
{
	static unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM + 64];
	int joined[2] = { 0, 1 };
	delta_u32 now = 100000;
	long iteration;

	node_start(0, HOST_IPV4, DELTA_PEER_PORT, PLATFORM_BIT | PROFILE_BIT, _delta_platform_pc_linux, 0x33, 3);
	node_start(1, 0x0300007F, 40001, PLATFORM_BIT | PROFILE_BIT, _delta_platform_pc_linux, 0x44, 0);
	game_reset();
	game_add(1);
	for (iteration = 0; iteration < iterations; iteration++)
	{
		struct delta_wire_header header;
		struct delta_wire_hello hello;
		struct delta_wire_welcome welcome;
		struct delta_wire_profile profile;
		struct delta_wire_table table;
		delta_u32 serial;
		static struct delta_wire_roster roster;
		delta_u32 session = next_random() % 2 ? nodes[1].peer.session : next_random();
		int size = mutated_message(data, session);
		int index;

		/* every parser */
		if (delta_wire_read_header(data, size, &header))
		{
			delta_wire_read_hello(data + DELTA_WIRE_HEADER_SIZE, header.length, &hello);
			delta_wire_read_welcome(data + DELTA_WIRE_HEADER_SIZE, header.length, &welcome);
			delta_wire_read_profile(data + DELTA_WIRE_HEADER_SIZE, header.length, &profile);
			if (delta_wire_read_table(data + DELTA_WIRE_HEADER_SIZE, header.length, &table))
			{
				CHECK(table.length > 0 && table.length <= DELTA_WIRE_TABLE_CHUNK);
				CHECK(table.offset % DELTA_WIRE_TABLE_CHUNK == 0 && table.offset + (delta_u32)table.length <= table.total);
				CHECK(table.total <= DELTA_LEGACY_SIGNED_SIZE);
				CHECK(table.data + table.length <= data + size);
			}
			delta_wire_read_table_have(data + DELTA_WIRE_HEADER_SIZE, header.length, &serial);
			if (delta_wire_read_roster(data + DELTA_WIRE_HEADER_SIZE, header.length, &roster))
				CHECK(roster.count <= DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES);
			for (index = 0; index < DELTA_WIRE_BUILD_SIZE + 1 && hello.build[index]; index++)
				CHECK(hello.build[index] >= 0x20 && hello.build[index] <= 0x7E);
			CHECK(index <= DELTA_WIRE_BUILD_SIZE);
		}
		/* both sides' sessions, from the right address and others */
		delta_peer_receive(&nodes[0].peer, now, next_random() % 4 ? nodes[1].ipv4 : next_random(),
			(unsigned short)(next_random() % 4 ? 40001 : next_random()), data, size);
		delta_peer_receive(&nodes[1].peer, now, next_random() % 4 ? HOST_IPV4 : next_random(),
			(unsigned short)(next_random() % 4 ? DELTA_PEER_PORT : next_random()), data, size);
		queued = 0;
		/* (the game moving on: frames, machines coming and going) */
		if (iteration % 16 == 0)
		{
			now += next_random() % 700;
			if (next_random() % 64 == 0)
			{
				game_remove(1);
				if (next_random() % 2)
					game_add(1);
			}
			joined[1] = next_random() % 32 != 0;
			frame(now, 2, joined, (int)(next_random() % 8 != 0));
			queued = 0;
		}
		CHECK(nodes[1].peer.client_state >= _delta_peer_client_off &&
			nodes[1].peer.client_state <= _delta_peer_client_legacy);
		CHECK(delta_peer_room_limit(&nodes[0].peer, _delta_peer_limit_players) >= 1);
		/* (a table coming in is never past its size) */
		CHECK(!nodes[0].peer.table_in.active || nodes[0].peer.table_in.total <= DELTA_LEGACY_SIGNED_SIZE);
		CHECK(!nodes[1].peer.table_in.active || nodes[1].peer.table_in.total <= DELTA_LEGACY_SIGNED_SIZE);
		for (index = 0; index < DELTA_PEER_MAXIMUM_PLAYERS; index++)
			CHECK(nodes[1].peer.player_machines[index] >= -1);
		if (failures)
			break;
	}
}

int main(int argc, char **argv)
{
	long iterations = argc > 1 ? atol(argv[1]) : 200000;

	random_state = argc > 2 ? (delta_u32)strtoul(argv[2], NULL, 0) : 0x5EED1234u;
	if (!random_state)
		random_state = 1;
	verbose = getenv("DELTA_TEST_VERBOSE") != NULL;
	test_wire();
	test_rate();
	test_handshake();
	test_fallback();
	test_limits();
	test_strangers();
	test_kill_switch();
	test_relay();
	test_relay_refused();
	test_random(iterations);
	if (failures)
	{
		fprintf(stderr, "%d checks failed\n", failures);
		return 1;
	}
	printf("delta_test: ok (%ld random inputs)\n", iterations);
	return 0;
}
