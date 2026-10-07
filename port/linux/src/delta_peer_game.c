/*
DELTA_PEER_GAME.C

The game's Delta Peer (delta_peer.h): one session, over a UDP socket of the
game's own Winsock layer (xnet.c), so that it goes where the game's traffic
goes: network.address binds it to the machine's loopback alias or
interface, and an invite's peers reach it through the invite tunnel (p2p.c
learns the socket's port when it is bound, as it does the game's, and a
datagram to a peer's virtual address goes onto the tunnel at once). A host
listens on DELTA_PEER_PORT; a client sends from a port the system picks.

The shared source calls the hooks from the network's main-thread idles
(network_server_manager.c, network_client_manager.c), under
HALO_GAME_BROWSER; network.protocol = "opence" turns all of it off.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "halo_port_limits.h"
#include "delta.h"
#include "delta_peer.h"
#ifdef HALO_GAME_BROWSER
#include "browser.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* updater.c's (server_platform.c's in the dedicated server) */
const char *updater_version(void);

enum
{
	_role_none = 0,
	_role_client,
	_role_host
};

static struct
{
	int ready;
	int protocol;
	struct delta_peer peer;
	int role;
	SOCKET socket;
	/* the socket's port (host order; 0: none open) */
	unsigned short port;
	/* a host whose port was taken said so once */
	int port_refused;
} delta_game = { 0 };

/* whether two settings' words are the same, case aside (ASCII; no
strings.h: Windows has none) */
static int same_word(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
		char y = *b >= 'A' && *b <= 'Z' ? (char)(*b - 'A' + 'a') : *b;

		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
}

/* ---------- this machine */

static int local_platform(void)
{
#if defined(_WIN32)
	/* (the Windows build under Proton on the Deck: Steam sets SteamDeck=1
	there too) */
	const char *deck = getenv("SteamDeck");

	return deck && deck[0] == '1' ? _delta_platform_steam_deck : _delta_platform_pc_windows;
#elif defined(__APPLE__)
	return _delta_platform_pc_macos;
#elif defined(HALO_ANDROID)
	/* (the Android game is a guest built as Linux code: HALO_ANDROID, not
	the NDK's __ANDROID__, is what it has; tools/android_build.py) */
	return _delta_platform_android;
#elif defined(__linux__)
	/* (Steam sets SteamDeck=1 in its games' environment on the Deck) */
	const char *deck = getenv("SteamDeck");

	return deck && deck[0] == '1' ? _delta_platform_steam_deck : _delta_platform_pc_linux;
#else
	return _delta_platform_unknown;
#endif
}

void delta_peer_platform_policy(int platform, struct delta_platform_key *key)
{
	/* delta-legacy-table: the signed table's "platform_policy" section,
	once it has one, replaces the platform's row here; until then
	delta.h's defaults (in key) stand */
	(void)platform;
	(void)key;
}

static void game_platform_policy(void *context, int platform, struct delta_platform_key *key)
{
	(void)context;
	delta_peer_platform_policy(platform, key);
}

void delta_peer_local_key(struct delta_platform_key *key)
{
	const char *limits = config_string("network.platform_limits");
	int platform = local_platform();

	/* the platform policy's row for this platform (DELTA_PLATFORM_POLICY) */
	delta_platform_policy_default(platform, key);
	delta_peer_platform_policy(platform, key);
	key->platform = (unsigned char)platform;
	key->version = DELTA_PLATFORM_KEY_VERSION;
	key->flags = 0;
	/* (its player takes whatever the host runs: no caveat protects it) */
	if (same_word(limits, "off"))
		key->flags |= DELTA_PLATFORM_KEY_OPTED_IN;
#ifdef HALO_SERVER
	key->flags |= DELTA_PLATFORM_KEY_DEDICATED;
#endif
}

static int hex_digit(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

/* the profile's claim: this copy's public player ID (the site's), only if
its player chose to share it (network.share_profile: a profile is opt-in,
and the ID is the same in every game); the revision is not known to the
game yet (0). This copy still sees the profiles others share */
static int local_profile(struct delta_wire_profile *profile)
{
#ifdef HALO_GAME_BROWSER
	char hex[2 * DELTA_WIRE_PLAYER_ID_SIZE + 8];
	int index;

	memset(profile, 0, sizeof(*profile));
	if (!config_boolean("network.share_profile") || browser_headless() || !browser_player_id(hex, sizeof(hex)))
		return 0;
	for (index = 0; index < DELTA_WIRE_PLAYER_ID_SIZE; index++)
	{
		int high = hex_digit(hex[2 * index]);
		int low = high >= 0 ? hex_digit(hex[2 * index + 1]) : -1;

		if (low < 0)
			return 0;
		profile->player_id[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
#else
	(void)hex_digit;
	memset(profile, 0, sizeof(*profile));
	return 0;
#endif
}

/* ---------- the session's needs */

static void game_send(void *context, delta_u32 ipv4, unsigned short port, const unsigned char *data, int size)
{
	struct sockaddr_in address;

	(void)context;
	if (!delta_game.port)
		return;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = halo_ws_htons(port);
	address.sin_addr.s_addr = (unsigned long)ipv4;
	/* (lost as a datagram may be: the handshake says HELLO again) */
	halo_ws_sendto(delta_game.socket, (const char *)data, size, 0, (const struct sockaddr *)&address,
		sizeof(address));
}

static void game_log(void *context, const char *text)
{
	(void)context;
	platform_log("%s", text);
}

static delta_u32 game_random(void *context)
{
	delta_u32 value = 0;

	(void)context;
	posix_random_bytes(&value, sizeof(value));
	return value;
}

/* the legacy table's kill switch */
static int game_capability_disabled(void *context, int capability)
{
	(void)context;
	return delta_capability_disabled(capability);
}

/* the legacy table's relay: delta.c's table (none taken or sent with a
local table in use, or no key) */
static delta_u32 game_legacy_table_serial(void *context)
{
	(void)context;
	return delta_legacy_relay() ? delta_legacy_serial() : DELTA_WIRE_TABLE_NONE;
}

static int game_legacy_table_signed(void *context, unsigned char *buffer, int size)
{
	(void)context;
	return delta_legacy_relay() ? delta_legacy_signed((char *)buffer, size) : 0;
}

static int game_legacy_table_offer(void *context, const unsigned char *table, int size)
{
	(void)context;
	return delta_legacy_relay() && delta_legacy_offer((const char *)table, size);
}

int delta_peer_protocol(void)
{
	if (!delta_game.ready)
	{
		const char *protocol = config_string("network.protocol");
		struct delta_peer_env env;
		struct delta_peer_local local;

		delta_game.ready = 1;
		delta_game.socket = INVALID_SOCKET;
		if (same_word(protocol, "opence"))
			delta_game.protocol = _delta_peer_protocol_opence;
		else if (same_word(protocol, "delta"))
			delta_game.protocol = _delta_peer_protocol_delta;
		else
		{
			if (protocol[0] && !same_word(protocol, "auto"))
				platform_log("Delta Peer: network.protocol \"%s\" is not auto, delta or opence: auto", protocol);
			delta_game.protocol = _delta_peer_protocol_auto;
		}
		if (delta_game.protocol == _delta_peer_protocol_opence)
			platform_log("Delta Peer: off (network.protocol = \"opence\"): the legacy protocol alone");

		memset(&env, 0, sizeof(env));
		env.send_datagram = game_send;
		env.log_line = game_log;
		env.random_number = game_random;
		env.legacy_table_serial = game_legacy_table_serial;
		env.legacy_table_signed = game_legacy_table_signed;
		env.legacy_table_offer = game_legacy_table_offer;
		env.capability_disabled = game_capability_disabled;
		env.platform_policy = game_platform_policy;
		memset(&local, 0, sizeof(local));
		local.capabilities = (delta_u32)1 << _delta_capability_platform | (delta_u32)1 << _delta_capability_profile;
		local.legacy_version = HALO_PORT_NETWORK_VERSION;
		delta_peer_local_key(&local.key);
		snprintf(local.build, sizeof(local.build), "ChupathingyCE %s", updater_version());
		local.has_profile = local_profile(&local.profile);
		local.ignore_platform_limits = !config_boolean("network.host_platform_limits");
		{
			const char *limits = config_string("network.platform_limits");

			if (limits[0] && !same_word(limits, "on") && !same_word(limits, "off"))
				platform_log("Delta Peer: network.platform_limits \"%s\" is not on or off: on", limits);
			if (same_word(limits, "off"))
				platform_log("Delta Peer: platform limits off (network.platform_limits): this machine joins "
					"games of any size");
		}
		if (local.ignore_platform_limits)
			platform_log("Delta Peer: network.host_platform_limits is off: games this machine hosts ignore "
				"other machines' platform limits (for testing)");
		delta_peer_initialize(&delta_game.peer, &env, &local);
	}
	return delta_game.protocol;
}

/* ---------- the socket */

static void close_socket(void)
{
	if (delta_game.socket != INVALID_SOCKET)
		halo_ws_closesocket(delta_game.socket);
	delta_game.socket = INVALID_SOCKET;
	delta_game.port = 0;
}

/* the socket open on the port (0: any); 1 if it is */
static int open_socket(unsigned short port)
{
	struct sockaddr_in address;
	int length = sizeof(address);
	u_long nonblocking = 1;

	if (delta_game.socket != INVALID_SOCKET && (!port || delta_game.port == port))
		return 1;
	close_socket();
	delta_game.socket = halo_ws_socket(AF_INET, SOCK_DGRAM, 0);
	if (delta_game.socket == INVALID_SOCKET)
		return 0;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = halo_ws_htons(port);
	address.sin_addr.s_addr = INADDR_ANY;
	/* (bound through xnet.c: network.address, and the tunnel told the port) */
	if (halo_ws_ioctlsocket(delta_game.socket, FIONBIO, &nonblocking) == SOCKET_ERROR ||
		halo_ws_bind(delta_game.socket, (const struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR ||
		halo_ws_getsockname(delta_game.socket, (struct sockaddr *)&address, &length) == SOCKET_ERROR)
	{
		close_socket();
		return 0;
	}
	delta_game.port = halo_ws_ntohs(address.sin_port);
	return delta_game.port != 0;
}

static void receive(void)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM + 1];
	int count;

	for (count = 0; count < DELTA_PEER_FRAME_DATAGRAMS && delta_game.socket != INVALID_SOCKET; count++)
	{
		struct sockaddr_in from;
		int length = sizeof(from);
		int size = halo_ws_recvfrom(delta_game.socket, (char *)data, (int)sizeof(data), 0, (struct sockaddr *)&from,
			&length);

		if (size == SOCKET_ERROR)
			break;
		if (length < (int)sizeof(from) || from.sin_family != AF_INET)
			continue;
		/* (one too big is dropped by the session: read as one byte more) */
		delta_peer_receive(&delta_game.peer, GetTickCount(), (delta_u32)from.sin_addr.s_addr,
			halo_ws_ntohs(from.sin_port), data, size);
	}
}

static void stop(void)
{
	delta_peer_stop(&delta_game.peer);
	close_socket();
	delta_game.role = _role_none;
}

/* ---------- the hooks */

void delta_peer_game_host_frame(const struct delta_peer_game_machine *machines, int count,
	const signed char *player_machines)
{
	if (delta_peer_protocol() == _delta_peer_protocol_opence)
		return;
	if (delta_game.role != _role_host)
	{
		stop();
		delta_game.role = _role_host;
		delta_game.port_refused = 0;
	}
	if (!open_socket(DELTA_PEER_PORT))
	{
		/* (another copy on this address has it: this game is OpenCE's alone,
		and its advertisement says so) */
		if (!delta_game.port_refused)
			platform_log("Delta Peer: cannot listen on port %d (taken?): this game is hosted without Delta",
				DELTA_PEER_PORT);
		delta_game.port_refused = 1;
		return;
	}
	if (delta_game.port_refused)
		platform_log("Delta Peer: listening on port %d", DELTA_PEER_PORT);
	else if (!delta_game.peer.roster_sent)
		platform_log("Delta Peer: hosting with Delta on port %d", DELTA_PEER_PORT);
	delta_game.port_refused = 0;
	receive();
	delta_peer_host_frame(&delta_game.peer, GetTickCount(), machines, count, player_machines);
}

void delta_peer_game_client_frame(int joined, delta_u32 host_ipv4, int host_speaks_delta, int machine_index,
	const signed char *player_machines)
{
	if (delta_peer_protocol() == _delta_peer_protocol_opence)
		return;
	/* (a host's own client is the host's machine: the host's session has it) */
	if (delta_game.role == _role_host)
		return;
	if (!joined)
	{
		if (delta_game.role == _role_client)
			stop();
		return;
	}
	if (delta_game.role != _role_client)
	{
		stop();
		delta_game.role = _role_client;
	}
	if (host_speaks_delta && !open_socket(0))
		host_speaks_delta = 0;
	receive();
	delta_peer_client_frame(&delta_game.peer, GetTickCount(), joined, host_ipv4, DELTA_PEER_PORT, host_speaks_delta,
		(unsigned char)(machine_index >= 0 && machine_index < DELTA_PEER_MAXIMUM_MACHINES ? machine_index :
			DELTA_WIRE_NO_MACHINE),
		player_machines);
}

void delta_peer_game_stop(int host)
{
	if (delta_game.ready && delta_game.role == (host ? _role_host : _role_client))
		stop();
}

/* ---------- what the game reads */

unsigned char delta_peer_advertised_flags(void)
{
	return delta_peer_protocol() != _delta_peer_protocol_opence && delta_game.role == _role_host && delta_game.port ?
		DELTA_ADVERTISED_FLAG : 0;
}

int delta_peer_host_player_limit(void)
{
	if (!delta_game.ready || delta_game.role != _role_host)
		return DELTA_PEER_NO_LIMIT;
	return delta_peer_room_limit(&delta_game.peer, _delta_peer_limit_players);
}

int delta_peer_host_coop(void)
{
	if (!delta_game.ready || delta_game.role != _role_host)
		return 1;
	return delta_peer_room_coop(&delta_game.peer);
}

int delta_peer_machine_key(int machine_index, struct delta_platform_key *key)
{
	struct delta_peer_machine machine;

	if (!delta_game.ready || !delta_peer_machine(&delta_game.peer, machine_index, &machine) ||
		!(machine.flags & DELTA_ROSTER_PLATFORM))
	{
		return 0;
	}
	*key = machine.key;
	return 1;
}

int delta_peer_player_platform(int player_index)
{
	struct delta_platform_key key;

	if (!delta_game.ready || player_index < 0 || player_index >= DELTA_PEER_MAXIMUM_PLAYERS ||
		!delta_peer_machine_key(delta_game.peer.player_machines[player_index], &key))
	{
		return _delta_platform_unknown;
	}
	return key.platform < NUMBER_OF_DELTA_PLATFORMS ? key.platform : _delta_platform_unknown;
}

int delta_peer_machine_profile(int machine_index, unsigned char *player_id, delta_u32 *revision)
{
	struct delta_peer_machine machine;

	if (!delta_game.ready || !delta_peer_machine(&delta_game.peer, machine_index, &machine) ||
		!(machine.flags & DELTA_ROSTER_PROFILE))
	{
		return 0;
	}
	if (player_id)
		memcpy(player_id, machine.profile.player_id, DELTA_WIRE_PLAYER_ID_SIZE);
	if (revision)
		*revision = machine.profile.revision;
	return 1;
}

int delta_peer_game_room_has(int capability)
{
	return delta_game.ready && delta_game.role != _role_none && delta_peer_room_has(&delta_game.peer, capability);
}

int delta_peer_game_room_limit(int limit)
{
	return delta_game.ready && delta_game.role != _role_none ? delta_peer_room_limit(&delta_game.peer, limit) :
		DELTA_PEER_NO_LIMIT;
}

int delta_peer_game_client_state(void)
{
	return delta_game.ready && delta_game.role == _role_client ? delta_game.peer.client_state :
		_delta_peer_client_off;
}
