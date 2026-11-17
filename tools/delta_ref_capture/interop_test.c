/*
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later

INTEROP_TEST.C

The reference client (tools/delta-ref) against the game's own Delta Peer
sessions (port/linux/src/delta_peer.c), in one process, datagrams passed by
hand: a port's machine joins a Chupathingy host and is welcomed, kept to its
platform's limit and let go; a HELLO from a stranger is never answered and
the client falls back on its own; and the reader takes a real client's
HELLO. Built and run by capture.py interop; never by a port.
*/

#include "delta.h"
#include "delta_wire.h"
#include "delta_peer.h"
#include "delta_ref.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("interop_test: failed at line %d: %s\n", __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* (addresses only compared, never used: the host's, the port's, a stranger's) */
#define HOST_IPV4 0x0100007Fu
#define PORT_IPV4 0x0200007Fu
#define STRANGER_IPV4 0x0300007Fu
#define PORT_PORT 40000

static struct delta_peer host;
static struct dref_client client;

/* what the host sent the port, waiting to be delivered */
static unsigned char to_port[64][DELTA_WIRE_MAXIMUM_DATAGRAM];
static int to_port_size[64];
static int to_port_count;
static int types_seen[64];

/* the address the port's datagrams come from (a stranger's, to test) */
static delta_u32 port_from = PORT_IPV4;
static delta_u32 now;

static void host_send(void *context, delta_u32 ipv4, unsigned short port, const unsigned char *data, int size)
{
	(void)context;
	if (ipv4 == PORT_IPV4 && port == PORT_PORT && to_port_count < 64)
	{
		memcpy(to_port[to_port_count], data, (size_t)size);
		to_port_size[to_port_count++] = size;
	}
}

static void host_log(void *context, const char *text)
{
	(void)context;
	printf("  host: %s\n", text);
}

static delta_u32 host_random(void *context)
{
	static delta_u32 state = 0x9E3779B9u;

	(void)context;
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

static void port_send(void *user, const uint8_t *data, int size)
{
	(void)user;
	delta_peer_receive(&host, now, port_from, PORT_PORT, data, size);
}

/* the host's datagrams to the port, delivered to the reference client */
static void deliver(void)
{
	int index;

	for (index = 0; index < to_port_count; index++)
	{
		if (to_port_size[index] > 3 && to_port[index][3] < 64)
			types_seen[to_port[index][3]]++;
		dref_client_receive(&client, now, to_port[index], to_port_size[index]);
	}
	to_port_count = 0;
}

static void host_start(void)
{
	struct delta_peer_env env;
	struct delta_peer_local local;

	memset(&env, 0, sizeof(env));
	env.send_datagram = host_send;
	env.log_line = host_log;
	env.random_number = host_random;
	memset(&local, 0, sizeof(local));
	/* (what a dedicated server offers: every capability a HELLO may carry) */
	local.capabilities = 0x03FBEDDFu;
	local.legacy_version = DELTA_LEGACY_NEWEST;
	local.key.platform = _delta_platform_pc_linux;
	local.key.version = DELTA_PLATFORM_KEY_VERSION;
	local.key.flags = 0x02;
	local.key.host_players = 16;
	local.key.join_players = 128;
	local.key.join_players_opt_in = 128;
	local.key.memory_class = 12;
	strcpy(local.build, "interop host");
	delta_peer_initialize(&host, &env, &local);
}

static struct dref_hello port_hello(void)
{
	struct dref_hello hello;

	memset(&hello, 0, sizeof(hello));
	hello.capabilities = 1u << DREF_CAP_PLATFORM;
	hello.legacy_version = 44;
	hello.machine_index = 2;
	hello.table_serial = DREF_NO_TABLES;
	hello.key.platform = DREF_PLATFORM_N64;
	hello.key.version = 1;
	hello.key.join_players = 8;
	hello.key.join_players_opt_in = 16;
	hello.key.memory_class = 3;
	strcpy(hello.build, "n64-port 0.1");
	return hello;
}

static void test_welcomed(void)
{
	struct delta_peer_game_machine machines[2];
	signed char players[DELTA_PEER_MAXIMUM_PLAYERS];
	struct dref_hello hello = port_hello();
	struct delta_peer_machine seen;
	int step;

	host_start();
	memset(machines, 0, sizeof(machines));
	machines[0].machine_index = 0;
	machines[0].local = 1;
	machines[1].machine_index = 2;
	machines[1].ipv4 = PORT_IPV4;
	memset(players, -1, sizeof(players));
	players[0] = 0;
	players[1] = 2;
	now = 1000;
	delta_peer_host_frame(&host, now, machines, 2, players);

	dref_client_start(&client, &hello, 0x51D3C0DEu, now, port_send, NULL);
	for (step = 0; step < 40; step++)
	{
		now += 33;
		delta_peer_host_frame(&host, now, machines, 2, players);
		dref_client_tick(&client, now);
		deliver();
	}
	CHECK(client.state == DREF_CLIENT_DELTA);
	CHECK(client.agreed == 1u << DREF_CAP_PLATFORM);
	CHECK(client.host.legacy_version == DELTA_LEGACY_NEWEST && !strcmp(client.host.build, "interop host"));
	CHECK(types_seen[DREF_WELCOME] >= 1 && types_seen[DREF_ROSTER] >= 1);
	/* (the port took no tables: none of their pieces came) */
	CHECK(types_seen[_delta_message_table] == 0);
	/* the host knows the port's machine, its platform and its number... */
	CHECK(delta_peer_machine(&host, 2, &seen) && seen.key.platform == DREF_PLATFORM_N64);
	CHECK(delta_peer_machine_legacy_version(&host, 2) == 44);
	/* ... and keeps the game to its join limit, as its roster says */
	CHECK(delta_peer_room_limit(&host, _delta_peer_limit_players) == 8);
	CHECK(client.room_players == 8);
	printf("  port welcomed: agreed 0x%x, room of %d players, %d WELCOME, %d ROSTER\n", (unsigned)client.agreed,
		client.room_players, types_seen[DREF_WELCOME], types_seen[DREF_ROSTER]);

	/* leaving: BYE, and the host forgets it */
	dref_client_stop(&client);
	now += 33;
	delta_peer_host_frame(&host, now, machines, 2, players);
	CHECK(!delta_peer_machine(&host, 2, &seen) || !(seen.flags & DELTA_ROSTER_DELTA));
	CHECK(delta_peer_room_limit(&host, _delta_peer_limit_players) != 8);
	delta_peer_stop(&host);
}

static void test_stranger(void)
{
	struct delta_peer_game_machine machines[2];
	signed char players[DELTA_PEER_MAXIMUM_PLAYERS];
	struct dref_hello hello = port_hello();
	int step;

	host_start();
	memset(machines, 0, sizeof(machines));
	machines[0].local = 1;
	machines[1].machine_index = 2;
	machines[1].ipv4 = PORT_IPV4;
	memset(players, -1, sizeof(players));
	memset(types_seen, 0, sizeof(types_seen));
	port_from = STRANGER_IPV4;
	now = 50000;
	dref_client_start(&client, &hello, 0x0BADF00Du, now, port_send, NULL);
	for (step = 0; step < 150; step++)
	{
		now += 33;
		delta_peer_host_frame(&host, now, machines, 2, players);
		dref_client_tick(&client, now);
		deliver();
	}
	/* (a machine not of the game is never answered; the client plays on
	without Delta once its 4 seconds are over) */
	CHECK(types_seen[DREF_WELCOME] == 0);
	CHECK(client.state == DREF_CLIENT_LEGACY);
	port_from = PORT_IPV4;
	delta_peer_stop(&host);
	printf("  stranger never answered: client fell back to legacy\n");
}

/* a real client's HELLO, read by the reference reader */
static unsigned char real_hello[DELTA_WIRE_MAXIMUM_DATAGRAM];
static int real_hello_size;

static void real_client_send(void *context, delta_u32 ipv4, unsigned short port, const unsigned char *data, int size)
{
	(void)context;
	(void)ipv4;
	(void)port;
	if (size > 3 && data[3] == _delta_message_hello && !real_hello_size)
	{
		memcpy(real_hello, data, (size_t)size);
		real_hello_size = size;
	}
}

static void test_real_hello(void)
{
	static struct delta_peer real;
	struct delta_peer_env env;
	struct delta_peer_local local;
	struct dref_header header;
	struct dref_hello read;
	signed char players[DELTA_PEER_MAXIMUM_PLAYERS];

	memset(&env, 0, sizeof(env));
	env.send_datagram = real_client_send;
	env.log_line = host_log;
	env.random_number = host_random;
	memset(&local, 0, sizeof(local));
	local.capabilities = (1u << _delta_capability_platform) | (1u << _delta_capability_chat);
	local.legacy_version = DELTA_LEGACY_NEWEST;
	local.modes = 3;
	local.key.platform = _delta_platform_xbox;
	local.key.version = DELTA_PLATFORM_KEY_VERSION;
	local.key.host_players = 16;
	local.key.join_players = 16;
	local.key.join_players_opt_in = 128;
	local.key.memory_class = 6;
	strcpy(local.build, "Warthog interop");
	delta_peer_initialize(&real, &env, &local);
	memset(players, -1, sizeof(players));
	delta_peer_client_frame(&real, 1000, 1, HOST_IPV4, DELTA_PEER_PORT, 1, 3, players);
	CHECK(real_hello_size > 0);
	CHECK(dref_header_read(real_hello, real_hello_size, &header) && header.type == DREF_HELLO && header.session);
	CHECK(dref_hello_read(real_hello + DREF_HEADER_SIZE, header.length, &read));
	CHECK(read.capabilities == local.capabilities && read.legacy_version == DELTA_LEGACY_NEWEST &&
		read.machine_index == 3 && read.key.platform == _delta_platform_xbox && read.key.memory_class == 6 &&
		!strcmp(read.build, "Warthog interop") && read.modes == 3);
	delta_peer_stop(&real);
	printf("  a real client's HELLO read: %d bytes\n", real_hello_size);
}

int main(void)
{
	test_welcomed();
	test_stranger();
	test_real_hello();
	if (failures)
	{
		printf("interop_test: %d failed\n", failures);
		return 1;
	}
	printf("interop_test: ok\n");
	return 0;
}
