/*
DELTA_FUZZ.C

libFuzzer's entry for Delta Peer (tools/test_delta_peer.py builds it with
-fsanitize=fuzzer,address,undefined where clang has libFuzzer, and runs it
for a bounded number of inputs): each input is one datagram, given to every
parser and to a host's and a client's session that have shaken hands (and
relay a legacy table), with a frame of both now and then.

    clang -fsanitize=fuzzer,address,undefined -iquote port/linux/include \
        port/linux/tests/delta_fuzz.c port/linux/src/delta_peer.c port/linux/src/delta_wire.c
*/

#include "../src/delta_peer.h"

#include <stddef.h>
#include <string.h>

enum
{
	HOST_IPV4 = 0x0100007F,
	CLIENT_IPV4 = 0x0300007F,
	CLIENT_PORT = 40001
};

static struct delta_peer host, client;
static int ready;

/* (the sessions' replies are dropped: one input, one datagram) */
static void drop(void *context, delta_u32 ipv4, unsigned short port, const unsigned char *data, int size)
{
	(void)context;
	(void)ipv4;
	(void)port;
	(void)data;
	(void)size;
}

static delta_u32 fixed_random(void *context)
{
	(void)context;
	return 0x12345678u;
}

/* (the legacy table's relay: a serial, a small table to send, and a check
that takes nothing, so the pieces go together but are never taken) */
static delta_u32 table_serial(void *context)
{
	(void)context;
	return 3;
}

static int table_signed(void *context, unsigned char *buffer, int size)
{
	(void)context;
	if (size < 2000)
		return 0;
	memset(buffer, 'a', 2000);
	return 2000;
}

static int table_offer(void *context, const unsigned char *table, int size)
{
	(void)context;
	(void)table;
	(void)size;
	return 0;
}

static void start(void)
{
	struct delta_peer_env env;
	struct delta_peer_local local;
	struct delta_peer_game_machine machines[2];
	signed char players[DELTA_PEER_MAXIMUM_PLAYERS];

	memset(&env, 0, sizeof(env));
	env.send_datagram = drop;
	env.random_number = fixed_random;
	env.legacy_table_serial = table_serial;
	env.legacy_table_signed = table_signed;
	env.legacy_table_offer = table_offer;
	memset(&local, 0, sizeof(local));
	local.capabilities = 3;
	local.legacy_version = 18;
	delta_platform_policy_default(_delta_platform_pc_linux, &local.key);
	local.has_profile = 1;
	delta_peer_initialize(&host, &env, &local);
	delta_peer_initialize(&client, &env, &local);

	memset(machines, 0, sizeof(machines));
	machines[0].machine_index = 0;
	machines[0].local = 1;
	machines[1].machine_index = 1;
	machines[1].ipv4 = CLIENT_IPV4;
	memset(players, -1, sizeof(players));
	players[0] = 0;
	players[1] = 1;
	delta_peer_host_frame(&host, 1000, machines, 2, players);
	delta_peer_client_frame(&client, 1000, 1, HOST_IPV4, DELTA_PEER_PORT, 1, 1, players);
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	static struct delta_wire_roster roster;
	struct delta_wire_header header;
	struct delta_wire_hello hello;
	struct delta_wire_welcome welcome;
	struct delta_wire_profile profile;
	struct delta_wire_table table;
	delta_u32 serial;
	static delta_u32 now = 2000;

	if (!ready)
	{
		start();
		ready = 1;
	}
	if (size > DELTA_WIRE_MAXIMUM_DATAGRAM + 16)
		return 0;
	if (delta_wire_read_header(data, (int)size, &header))
	{
		delta_wire_read_hello(data + DELTA_WIRE_HEADER_SIZE, header.length, &hello);
		delta_wire_read_welcome(data + DELTA_WIRE_HEADER_SIZE, header.length, &welcome);
		delta_wire_read_profile(data + DELTA_WIRE_HEADER_SIZE, header.length, &profile);
		delta_wire_read_roster(data + DELTA_WIRE_HEADER_SIZE, header.length, &roster);
		if (delta_wire_read_table(data + DELTA_WIRE_HEADER_SIZE, header.length, &table) &&
			(table.offset + (delta_u32)table.length > table.total || table.total > DELTA_LEGACY_SIGNED_SIZE))
		{
			__builtin_trap();
		}
		delta_wire_read_table_have(data + DELTA_WIRE_HEADER_SIZE, header.length, &serial);
	}
	/* (time moves, so the rate limits refill) */
	now += 50;
	delta_peer_receive(&host, now, CLIENT_IPV4, CLIENT_PORT, data, (int)size);
	/* (the client's session as it is, whatever an earlier input made it) */
	delta_peer_receive(&client, now, HOST_IPV4, DELTA_PEER_PORT, data, (int)size);
	/* (now and then a frame: the relay's sends, its passes) */
	if (size && data[0] % 8 == 0)
	{
		struct delta_peer_game_machine machines[2];
		signed char players[DELTA_PEER_MAXIMUM_PLAYERS];

		memset(machines, 0, sizeof(machines));
		machines[0].local = 1;
		machines[1].machine_index = 1;
		machines[1].ipv4 = CLIENT_IPV4;
		memset(players, -1, sizeof(players));
		players[0] = 0;
		players[1] = 1;
		delta_peer_host_frame(&host, now, machines, 2, players);
		delta_peer_client_frame(&client, now, 1, HOST_IPV4, DELTA_PEER_PORT, 1, 1, players);
	}
	return 0;
}
