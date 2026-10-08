/*
DELTA_FUZZ.C

libFuzzer's entry for Delta Peer (tools/test_delta_peer.py builds it with
-fsanitize=fuzzer,address,undefined where clang has libFuzzer, and runs it
for a bounded number of inputs): each input is one datagram, given to every
parser and to a host's and a client's session (the client's HELLO unanswered,
each with a legacy table to relay; the host with moderation, which takes
any key's actions), with a frame of both now and then.

    clang -fsanitize=fuzzer,address,undefined -iquote port/linux/include \
        -I port/third_party/monocypher port/linux/tests/delta_fuzz.c port/linux/src/delta_peer.c \
        port/linux/src/delta_wire.c port/third_party/monocypher/monocypher.c \
        port/third_party/monocypher/monocypher-ed25519.c
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

/* (moderation: any key an owner, every action done) */
static int key_role(void *context, const unsigned char *key, delta_u32 *permissions, delta_u32 *ban_minutes)
{
	(void)context;
	(void)key;
	*permissions = 0x1FF;
	*ban_minutes = 60;
	return 3;
}

static int action(void *context, int machine_index, const unsigned char *key, int kind, int target, int minutes,
	const char *reason, char *result, int result_size)
{
	(void)context;
	(void)machine_index;
	(void)key;
	(void)kind;
	(void)target;
	(void)minutes;
	(void)reason;
	result[0] = 0;
	(void)result_size;
	return 1;
}

static const struct delta_peer_moderation_host moderation_host = { NULL, NULL, key_role, action, NULL };

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
	/* (platform, profile and ce_maps) */
	local.capabilities = 0x13 | (1u << _delta_capability_moderation);
	local.legacy_version = 18;
	delta_platform_policy_default(_delta_platform_pc_linux, &local.key);
	local.has_profile = 1;
	delta_peer_initialize(&host, &env, &local);
	delta_peer_initialize(&client, &env, &local);
	delta_peer_set_moderation_host(&host, &moderation_host);
	delta_peer_set_moderation_binding(&host, "0123456789abcdef0123456789abcdef");

	memset(machines, 0, sizeof(machines));
	machines[0].machine_index = 0;
	machines[0].local = 1;
	machines[1].machine_index = 1;
	machines[1].ipv4 = CLIENT_IPV4;
	memset(players, -1, sizeof(players));
	players[0] = 0;
	players[1] = 1;
	delta_peer_host_frame(&host, 1000, machines, 2, players);
	{
		struct delta_wire_map map;

		memset(&map, 0, sizeof(map));
		map.family = 1;
		map.flags = DELTA_WIRE_MAP_HASHED;
		strcpy(map.name, "fuzz");
		delta_peer_set_map(&host, &map);
	}
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
		{
			struct delta_wire_mod_challenge challenge;
			struct delta_wire_mod_proof proof;
			struct delta_wire_mod_state state;
			struct delta_wire_mod_action mod_action;
			struct delta_wire_mod_result result;
			struct delta_wire_mod_notice notice;
			struct delta_wire_mod_bind bind;
			struct delta_wire_mod_bind_answer answer;
			const unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

			if (delta_wire_read_mod_challenge(payload, header.length, &challenge) &&
				challenge.binding_length > DELTA_WIRE_MODERATION_BINDING_SIZE)
				__builtin_trap();
			delta_wire_read_mod_proof(payload, header.length, &proof);
			delta_wire_read_mod_state(payload, header.length, &state);
			if (delta_wire_read_mod_action(payload, header.length, &mod_action) &&
				mod_action.reason_length > DELTA_WIRE_MODERATION_REASON_SIZE)
				__builtin_trap();
			delta_wire_read_mod_result(payload, header.length, &result);
			delta_wire_read_mod_notice(payload, header.length, &notice);
			delta_wire_read_mod_bind(payload, header.length, &bind);
			delta_wire_read_mod_bind_answer(payload, header.length, &answer);
		}
		{
			struct delta_wire_map map;

			if (delta_wire_read_map(data + DELTA_WIRE_HEADER_SIZE, header.length, &map) &&
				((map.family && !(map.flags & DELTA_WIRE_MAP_HASHED)) || map.name[DELTA_WIRE_MAP_NAME_SIZE]))
			{
				__builtin_trap();
			}
		}
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
