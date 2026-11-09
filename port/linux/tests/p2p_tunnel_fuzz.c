/*
P2P_TUNNEL_FUZZ.C

libFuzzer's entry for what a machine's tunnel socket receives (port/linux/src/p2p.c,
included whole to reach its readers; tools/test_host_fuzz.py builds it). Two
machines are set up the way a joined game has them, a peer with its keys,
and the game's ports open. The first byte of an input says how the rest
arrives: bit 0, which side the machine is (host or joiner); bit 1, as raw
datagrams to the socket (STUN, probes, and what is no tunnel packet),
else as what a joined peer sends, sealed with its keys (the harness seals
each piece, so that what is behind the seal, the tunnel's own packets, the
game's datagrams and the streams, is read). The rest is pieces, each a
length byte and that many bytes. The streams are then served, as the
thread does each pass.
*/

#include "../src/p2p.c"

#include <stdint.h>

static unsigned long clock_now = 7000000;
static int next_socket = 10;

unsigned long GetTickCount(void)
{
	return clock_now;
}

void Sleep(unsigned long milliseconds)
{
	clock_now += milliseconds;
}

void platform_log(const char *format, ...)
{
	(void)format;
}

void posix_random_bytes(void *buffer, posix_ulong size)
{
	memset(buffer, 0x44, size);
}

int posix_user_secret(unsigned char *secret, int size)
{
	memset(secret, 0x55, (size_t)size);
	return 1;
}

int config_boolean(const char *name)
{
	(void)name;
	return 1;
}

const char *config_string(const char *name)
{
	(void)name;
	return "";
}

int config_chosen(const char *name)
{
	(void)name;
	return 0;
}

long config_integer(const char *name)
{
	(void)name;
	return 0;
}

double config_real(const char *name)
{
	(void)name;
	return 0;
}

int delta_capability_disabled(int capability)
{
	(void)capability;
	return 0;
}

void p2p_discord_set_join(const char *code, const char *party, int player_count, int maximum_player_count)
{
	(void)code;
	(void)party;
	(void)player_count;
	(void)maximum_player_count;
}

void p2p_discord_update(void)
{
}

void p2p_discord_user(char *id, int id_size, char *name, int name_size)
{
	(void)id_size;
	(void)name_size;
	id[0] = name[0] = 0;
}

int p2p_lobby_browsing(void)
{
	return 0;
}

int p2p_lobby_listed(void)
{
	return 0;
}

int p2p_lobby_open(void)
{
	return 0;
}

void p2p_lobby_quit(void)
{
}

void p2p_lobby_update(const unsigned char *token, int player_count, int maximum_player_count)
{
	(void)token;
	(void)player_count;
	(void)maximum_player_count;
}

void p2p_signal_host(const unsigned char *token)
{
	(void)token;
}

void p2p_signal_join(const unsigned char *host_hash, const unsigned char *token)
{
	(void)host_hash;
	(void)token;
}

void p2p_signal_probe(const unsigned char *host_hash, const unsigned char *token, const unsigned char *message,
	int size)
{
	(void)host_hash;
	(void)token;
	(void)message;
	(void)size;
}

void p2p_signal_probe_topic(const unsigned char *reply)
{
	(void)reply;
}

void p2p_signal_select_sets(int *read, int *read_count, int *write, int *write_count, int maximum_count)
{
	(void)read;
	(void)read_count;
	(void)write;
	(void)write_count;
	(void)maximum_count;
}

void p2p_signal_start(void)
{
}

void p2p_signal_stop_hosting(void)
{
}

void p2p_signal_stop_joining(void)
{
}

void p2p_signal_update(const int *read, int read_count, const int *write, int write_count)
{
	(void)read;
	(void)read_count;
	(void)write;
	(void)write_count;
}

int posix_command_line_argument(int index, char *buffer, posix_ulong size)
{
	(void)index;
	(void)buffer;
	(void)size;
	return 0;
}

int posix_hardware_id_source(char *text, int size)
{
	(void)text;
	(void)size;
	return 0;
}

posix_ulong posix_local_ipv4_address(void)
{
	return 0x0100007F;
}

int posix_register_url_scheme(const char *scheme, const char *description)
{
	(void)scheme;
	(void)description;
	return 0;
}

posix_ulong posix_resolve_ipv4(const char *host)
{
	(void)host;
	return 0;
}

int posix_socket(int family, int type, int protocol)
{
	(void)family;
	(void)type;
	(void)protocol;
	return next_socket++;
}

int posix_socket_accept(int socket, void *address, int *address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_bind(int socket, const void *address, int address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return 0;
}

int posix_socket_close(int socket)
{
	(void)socket;
	return 0;
}

int posix_socket_connect(int socket, const void *address, int address_length)
{
	(void)socket;
	(void)address;
	(void)address_length;
	return 0;
}

int posix_socket_getsockname(int socket, void *address, int *address_length)
{
	struct sockaddr_in *name = address;

	memset(name, 0, sizeof(*name));
	name->sin_family = AF_INET;
	name->sin_port = (unsigned short)(socket * 7);
	*address_length = (int)sizeof(*name);
	return 0;
}

int posix_socket_last_error(void)
{
	return WSAEWOULDBLOCK;
}

int posix_socket_listen(int socket, int backlog)
{
	(void)socket;
	(void)backlog;
	return 0;
}

int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	(void)socket;
	(void)buffer;
	(void)length;
	(void)flags;
	return -1;
}

int posix_socket_recvfrom(int socket, void *buffer, int length, int flags, void *address, int *address_length)
{
	(void)socket;
	(void)buffer;
	(void)length;
	(void)flags;
	(void)address;
	(void)address_length;
	return -1;
}

int posix_socket_select(int *read, int *read_count, int *write, int *write_count, int *error, int *error_count,
	posix_long timeout_seconds, posix_long timeout_microseconds, int infinite)
{
	(void)read;
	(void)read_count;
	(void)write;
	(void)write_count;
	(void)error;
	(void)error_count;
	(void)timeout_seconds;
	(void)timeout_microseconds;
	(void)infinite;
	return 0;
}

int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	(void)socket;
	(void)buffer;
	(void)flags;
	return length;
}

int posix_socket_sendto(int socket, const void *buffer, int length, int flags, const void *address,
	int address_length)
{
	(void)socket;
	(void)buffer;
	(void)flags;
	(void)address;
	(void)address_length;
	return length;
}

int posix_socket_set_nodelay(int socket)
{
	(void)socket;
	return 0;
}

int posix_socket_set_nonblocking(int socket, int nonblocking)
{
	(void)socket;
	(void)nonblocking;
	return 0;
}

int posix_socket_setsockopt(int socket, int level, int name, const void *value, int length)
{
	(void)socket;
	(void)level;
	(void)name;
	(void)value;
	(void)length;
	return 0;
}

int posix_upnp_forward_udp(unsigned short port, unsigned short preferred_port, posix_ulong *external_address,
	unsigned short *external_port, char *error, int error_size)
{
	(void)port;
	(void)preferred_port;
	(void)external_address;
	(void)external_port;
	(void)error_size;
	error[0] = 0;
	return 0;
}

void posix_upnp_stop_forwarding_udp(unsigned short external_port)
{
	(void)external_port;
}

int thread_stop_create(const char *name, void *(*main)(void *), void *argument)
{
	(void)name;
	(void)main;
	(void)argument;
	return -1;
}

int thread_stop_requested(void)
{
	return 1;
}

static struct peer *set_up(int is_host)
{
	static const unsigned char other[P2P_IDENTIFIER_SIZE] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
	unsigned char secret[P2P_SHA256_SIZE];
	struct p2p_candidate candidate = { 0x0200007F, 0x3412 };
	int index;

	/* (the last run's streams let go, so that nothing is left allocated) */
	for (index = 0; index < MAXIMUM_STREAMS; index++)
	{
		if (p2p.streams[index].used && p2p.streams[index].kcp)
			ikcp_release(p2p.streams[index].kcp);
	}
	memset(&p2p, 0, sizeof(p2p));
	next_socket = 10;
	clock_now = 7000000;
	for (index = 0; index < MAXIMUM_PROXIES; index++)
		p2p.proxies[index].socket = -1;
	for (index = 0; index < MAXIMUM_LISTENERS; index++)
		p2p.listeners[index].socket = -1;
	for (index = 0; index < MAXIMUM_STREAMS; index++)
		p2p.streams[index].socket = -1;
	p2p.hosting_socket = -1;
	p2p.local_address = 0x0100007F;
	p2p.tunnel_socket = 5;
	p2p.tunnel_port = 0x1111;
	p2p.running = 1;
	/* the game's ports: its datagram ports (the server's, the client's) and a stream that listens */
	p2p.game_ports[0].port = network_short(GAME_SERVER_PORT);
	p2p.game_ports[1].port = network_short(GAME_CLIENT_PORT);
	p2p.game_ports[2].port = network_short(2302);
	p2p.game_ports[3].port = network_short(2310);
	p2p.game_ports[3].stream = 1;
	p2p.game_ports[3].listening = 1;
	memset(secret, 0x66, sizeof(secret));
	p2p_identifier();
	p2p_peer_offered(other, secret, &candidate, 1, is_host);
	return &p2p.peers[0];
}

/* a tunnel packet from the peer: inner sealed with the key it sends with, numbered counter */
static int seal_packet(const struct peer *peer, unsigned long long counter, const unsigned char *inner, int size,
	unsigned char *packet)
{
	unsigned char nonce[P2P_NONCE_SIZE];
	int index;

	packet[0] = TUNNEL_MAGIC;
	memcpy(packet + 1, peer->identifier, P2P_IDENTIFIER_SIZE);
	for (index = 0; index < 8; index++)
		packet[1 + P2P_IDENTIFIER_SIZE + index] = (unsigned char)(counter >> (index * 8));
	packet_nonce(packet, nonce);
	return TUNNEL_HEADER_SIZE + p2p_aead_seal(peer->receive_key, nonce, packet, TUNNEL_HEADER_SIZE, inner, size,
		packet + TUNNEL_HEADER_SIZE);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	unsigned char packet[MAXIMUM_PACKET_SIZE + 64];
	struct sockaddr_in from;
	struct peer *peer;
	unsigned long long counter = 1;
	size_t at = 1;
	int index, mode;

	if (size < 2)
		return 0;
	mode = data[0];
	peer = set_up(mode & 1);
	memset(&from, 0, sizeof(from));
	from.sin_family = AF_INET;
	from.sin_addr.s_addr = 0x0200007F;
	from.sin_port = 0x3412;
	while (at < size)
	{
		int length = data[at++];

		if ((size_t)length > size - at)
			length = (int)(size - at);
		if (mode & 2)
		{
			unsigned char *own = malloc((size_t)length ? (size_t)length : 1);

			if (!own)
				return 0;
			memcpy(own, data + at, (size_t)length);
			/* (to the peer's identifier too, so that a packet that says it is the tunnel's finds it) */
			if (length >= 1 + P2P_IDENTIFIER_SIZE && own[0] == TUNNEL_MAGIC && (mode & 4))
				memcpy(own + 1, peer->identifier, P2P_IDENTIFIER_SIZE);
			tunnel_received(own, length, &from);
			free(own);
		}
		else if (length >= 1 && length <= MAXIMUM_INNER_SIZE)
		{
			int packet_size = seal_packet(peer, counter++ + (unsigned)(mode >> 3), data + at, length, packet);

			tunnel_received(packet, packet_size, &from);
		}
		at += (size_t)length;
		clock_now += 30;
		peer = &p2p.peers[0];
		for (index = 0; index < MAXIMUM_STREAMS; index++)
		{
			if (p2p.streams[index].used)
				stream_update(&p2p.streams[index]);
		}
		if (!peer->used)
			peer = set_up(mode & 1);
	}
	return 0;
}
