/*
DELTA_PEER.C

Delta Peer's sessions (delta_peer.h): the host's handshakes and roster, the
client's handshake, and what each knows of the game's machines. No sockets
and no platform here: struct delta_peer_env sends, logs and draws random
numbers, so the tests (tools/test_delta_peer.py) run a host and clients in
one process.
*/

#include "delta_peer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum
{
	_mode_off = 0,
	_mode_client,
	_mode_host
};

/* the capabilities this build knows (the retired console_slots bit never
counts) */
#define KNOWN_CAPABILITIES \
	((((delta_u32)1 << NUMBER_OF_DELTA_CAPABILITIES) - 1) & ~((delta_u32)1 << _delta_capability_console_slots))
#define CAPABILITY(bit) ((delta_u32)1 << (bit))

static void say(struct delta_peer *peer, const char *format, ...)
	__attribute__((format(printf, 2, 3)));

static void say(struct delta_peer *peer, const char *format, ...)
{
	char text[256];
	va_list arguments;

	if (!peer->env.log_line)
		return;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	peer->env.log_line(peer->env.context, text);
}

static const char *platform_name(int platform)
{
	static const char *const names[NUMBER_OF_DELTA_PLATFORMS] = {
		"unknown", "pc_windows", "pc_macos", "pc_linux", "android", "steam_deck", "xbox", "xbox360", "wiiu",
		"switch"
	};

	return platform >= 0 && platform < NUMBER_OF_DELTA_PLATFORMS ? names[platform] : "unknown";
}

static void peer_send(struct delta_peer *peer, delta_u32 ipv4, unsigned short port, const unsigned char *data, int size)
{
	if (size > 0 && peer->env.send_datagram)
		peer->env.send_datagram(peer->env.context, ipv4, port, data, size);
}

static int elapsed(delta_u32 now, delta_u32 then, delta_u32 time)
{
	return (delta_u32)(now - then) >= time;
}

static int valid_machine(int machine_index)
{
	return machine_index >= 0 && machine_index < DELTA_PEER_MAXIMUM_MACHINES;
}

/* what this machine offers now: what it has, but a capability the legacy
table's kill switch turns off (delta_capability_disabled) */
static delta_u32 offered(const struct delta_peer *peer)
{
	delta_u32 capabilities = peer->local.capabilities;
	int capability;

	if (!peer->env.capability_disabled)
		return capabilities;
	for (capability = 0; capability < NUMBER_OF_DELTA_CAPABILITIES; capability++)
	{
		if ((capabilities & CAPABILITY(capability)) && peer->env.capability_disabled(peer->env.context, capability))
			capabilities &= ~CAPABILITY(capability);
	}
	return capabilities;
}

static void policy(const struct delta_peer *peer, int platform, struct delta_platform_key *key)
{
	delta_platform_policy_default(platform, key);
	if (peer->env.platform_policy)
		peer->env.platform_policy(peer->env.context, platform, key);
}

int delta_peer_key_join_limit(const struct delta_peer *peer, const struct delta_platform_key *key)
{
	struct delta_platform_key row;
	int limit;

	/* (its player chose to take whatever the host runs) */
	if (key->flags & DELTA_PLATFORM_KEY_OPTED_IN)
		return DELTA_PEER_NO_LIMIT;
	policy(peer, key->platform, &row);
	limit = row.join_players ? row.join_players : DELTA_PEER_NO_LIMIT;
	/* (a key may claim less than its platform's row, never more) */
	if (key->join_players && key->join_players < limit)
		limit = key->join_players;
	return limit;
}

/* the most players this host's own game takes: its own row's (and key's)
host limit, none if its player opted in */
static int host_limit(const struct delta_peer *peer)
{
	struct delta_platform_key row;
	int limit;

	if (peer->local.key.flags & DELTA_PLATFORM_KEY_OPTED_IN)
		return DELTA_PEER_NO_LIMIT;
	policy(peer, peer->local.key.platform, &row);
	limit = row.host_players ? row.host_players : DELTA_PEER_NO_LIMIT;
	if (peer->local.key.host_players && peer->local.key.host_players < limit)
		limit = peer->local.key.host_players;
	return limit;
}

/* what this machine knows of itself, as a machine of the game */
static void local_machine(const struct delta_peer *peer, struct delta_peer_machine *machine, delta_u32 now)
{
	memset(machine, 0, sizeof(*machine));
	machine->known = 1;
	machine->flags = DELTA_ROSTER_DELTA | DELTA_ROSTER_PLATFORM;
	machine->capabilities = offered(peer);
	machine->key = peer->local.key;
	if (peer->local.has_profile && (machine->capabilities & CAPABILITY(_delta_capability_profile)))
	{
		machine->flags |= DELTA_ROSTER_PROFILE;
		machine->profile = peer->local.profile;
	}
	machine->seen_time = now;
}

static void copy_players(struct delta_peer *peer, const signed char *player_machines)
{
	int index;

	for (index = 0; index < DELTA_PEER_MAXIMUM_PLAYERS; index++)
	{
		int machine = player_machines ? player_machines[index] : -1;

		peer->player_machines[index] = (signed char)(valid_machine(machine) ? machine : -1);
	}
}

static void forget_all(struct delta_peer *peer)
{
	memset(peer->game_machines, 0, sizeof(peer->game_machines));
	peer->game_machine_count = 0;
	memset(peer->peers, 0, sizeof(peer->peers));
	memset(peer->rates, 0, sizeof(peer->rates));
	peer->roster_dirty = 0;
	peer->roster_sent = 0;
	peer->client_state = _delta_peer_client_off;
	peer->host_ipv4 = 0;
	peer->host_port = 0;
	peer->session = 0;
	peer->machine_index = DELTA_WIRE_NO_MACHINE;
	memset(&peer->welcome, 0, sizeof(peer->welcome));
	peer->agreed = 0;
	memset(&peer->host_rate, 0, sizeof(peer->host_rate));
	memset(&peer->host_relay, 0, sizeof(peer->host_relay));
	peer->table_in.active = 0;
	memset(peer->machines, 0, sizeof(peer->machines));
	peer->room_capabilities = 0;
	peer->room_players = DELTA_PEER_NO_LIMIT;
	memset(peer->player_machines, -1, sizeof(peer->player_machines));
}

void delta_peer_initialize(struct delta_peer *peer, const struct delta_peer_env *env,
	const struct delta_peer_local *local)
{
	memset(peer, 0, sizeof(*peer));
	peer->env = *env;
	peer->local = *local;
	/* (only what this build knows is offered) */
	peer->local.capabilities &= KNOWN_CAPABILITIES;
	peer->local.build[DELTA_WIRE_BUILD_SIZE] = 0;
	forget_all(peer);
}

void delta_peer_stop(struct delta_peer *peer)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int index;

	if (peer->mode == _mode_client &&
		(peer->client_state == _delta_peer_client_waiting || peer->client_state == _delta_peer_client_delta))
	{
		peer_send(peer, peer->host_ipv4, peer->host_port, data,
			delta_wire_write_empty(data, DELTA_MAJOR, _delta_message_bye, peer->session));
	}
	if (peer->mode == _mode_host)
	{
		for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
		{
			if (peer->peers[index].used)
			{
				peer_send(peer, peer->peers[index].ipv4, peer->peers[index].port, data,
					delta_wire_write_empty(data, DELTA_MAJOR, _delta_message_bye, peer->peers[index].session));
			}
		}
	}
	forget_all(peer);
	peer->mode = _mode_off;
}

static void enter_mode(struct delta_peer *peer, int mode)
{
	if (peer->mode != mode)
	{
		if (peer->mode != _mode_off)
			delta_peer_stop(peer);
		peer->mode = mode;
	}
}

/* ---------- the legacy table's relay

The side with the newer serial sends its signed table, in pieces paced
under the receiver's rate limit; the receiver puts them together and gives
the whole to delta.c (the signature, then the document, checked there),
then says its serial (TABLE_HAVE). A machine that takes no tables says so
(DELTA_WIRE_TABLE_NONE) and is sent none. */

/* this machine's serial now (DELTA_WIRE_TABLE_NONE: it takes no tables; no
relay in the env: its fixed serial, and none taken) */
static delta_u32 own_serial(const struct delta_peer *peer)
{
	return peer->env.legacy_table_serial ? peer->env.legacy_table_serial(peer->env.context) :
		peer->local.legacy_table_serial;
}

/* the serial as the handshake says it (0 for none or a table not taken) */
static delta_u32 handshake_serial(const struct delta_peer *peer)
{
	delta_u32 serial = own_serial(peer);

	return serial == DELTA_WIRE_TABLE_NONE ? 0 : serial;
}

static int relaying(const struct delta_peer *peer)
{
	return peer->env.legacy_table_serial && peer->env.legacy_table_signed && peer->env.legacy_table_offer;
}

/* a session's relay: their serial, and this machine's as its handshake
said it */
static void relay_start(struct delta_peer *peer, struct delta_peer_relay *relay, delta_u32 now,
	delta_u32 their_serial)
{
	memset(relay, 0, sizeof(*relay));
	relay->their_serial = their_serial;
	relay->start_time = now;
	relay->said = 1;
	relay->said_serial = handshake_serial(peer);
	relay->said_time = now - DELTA_PEER_TABLE_HAVE_GAP;
}

/* a machine's name in the log: "machine N", or "the host" */
static const char *relay_name(int from, char *name, int size)
{
	if (from < 0)
		snprintf(name, (size_t)size, "the host");
	else
		snprintf(name, (size_t)size, "machine %d", from);
	return name;
}

/* TABLE_HAVE with this machine's serial, if it changed since last said or
when asked (at most once a DELTA_PEER_TABLE_HAVE_GAP) */
static void relay_say(struct delta_peer *peer, struct delta_peer_relay *relay, delta_u32 now, delta_u32 ipv4,
	unsigned short port, delta_u32 session, int asked)
{
	unsigned char data[DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_TABLE_HAVE_SIZE];
	delta_u32 serial = own_serial(peer);

	if (!peer->env.legacy_table_serial)
		return;
	if (relay->said && relay->said_serial == serial && !asked)
		return;
	if (relay->said && !elapsed(now, relay->said_time, DELTA_PEER_TABLE_HAVE_GAP))
		return;
	relay->said = 1;
	relay->said_time = now;
	relay->said_serial = serial;
	peer_send(peer, ipv4, port, data, delta_wire_write_table_have(data, session, serial));
}

/* each frame, for each machine with a session: TABLE_HAVE when this
machine's serial changed (or it takes none), and the next piece of a pass
to a machine whose table is older */
static void relay_frame(struct delta_peer *peer, struct delta_peer_relay *relay, int to, delta_u32 now,
	delta_u32 ipv4, unsigned short port, delta_u32 session, int *sends)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	struct delta_wire_table piece;
	delta_u32 serial = own_serial(peer);
	char name[32];

	if (!peer->env.legacy_table_serial)
		return;
	/* (a serial other than the handshake's, or the one said since, is
	said: a machine that takes no tables says so at once) */
	relay_say(peer, relay, now, ipv4, port, session, 0);
	if (!relaying(peer) || serial == DELTA_WIRE_TABLE_NONE || !serial || relay->their_serial == DELTA_WIRE_TABLE_NONE ||
		serial <= relay->their_serial)
	{
		relay->sending = 0;
		return;
	}
	if (!relay->sending)
	{
		if (relay->passes >= DELTA_PEER_TABLE_PASSES || !elapsed(now, relay->start_time, DELTA_PEER_TABLE_DELAY) ||
			(relay->passes && !elapsed(now, relay->pass_time, DELTA_PEER_TABLE_PASS_GAP)) ||
			(sends && *sends >= DELTA_PEER_TABLE_SENDS))
		{
			return;
		}
		if (peer->table_out_serial != serial)
		{
			int size = peer->env.legacy_table_signed(peer->env.context, peer->table_out, (int)sizeof(peer->table_out));

			peer->table_out_serial = size > 0 && size <= DELTA_LEGACY_SIGNED_SIZE ? serial : 0;
			peer->table_out_size = peer->table_out_serial ? size : 0;
			if (!peer->table_out_serial)
				return;
		}
		relay->sending = 1;
		relay->sending_serial = serial;
		relay->offset = 0;
		relay->passes++;
		relay->piece_time = now - DELTA_PEER_TABLE_GAP;
		say(peer, "Delta Peer: sending legacy table %u to %s (it has %u; pass %d of %d)", (unsigned)serial,
			relay_name(to, name, (int)sizeof(name)), (unsigned)relay->their_serial, relay->passes,
			DELTA_PEER_TABLE_PASSES);
		if (sends)
			++*sends;
	}
	else if (sends)
		++*sends;
	/* (this machine took a newer table meanwhile: the pass ends; the next
	sends that one) */
	if (relay->sending_serial != serial || peer->table_out_serial != serial)
	{
		relay->sending = 0;
		relay->pass_time = now;
		return;
	}
	if (!elapsed(now, relay->piece_time, DELTA_PEER_TABLE_GAP))
		return;
	memset(&piece, 0, sizeof(piece));
	piece.serial = serial;
	piece.total = (delta_u32)peer->table_out_size;
	piece.offset = relay->offset;
	piece.length = piece.total - piece.offset < DELTA_WIRE_TABLE_CHUNK ? (int)(piece.total - piece.offset) :
		DELTA_WIRE_TABLE_CHUNK;
	piece.data = peer->table_out + piece.offset;
	peer_send(peer, ipv4, port, data, delta_wire_write_table(data, session, &piece));
	relay->piece_time = now;
	relay->offset += (delta_u32)piece.length;
	/* (the next pass a minute after this one's end: after the receiver's
	check, which is once a minute) */
	if (relay->offset >= piece.total)
	{
		relay->sending = 0;
		relay->pass_time = now;
	}
}

/* TABLE from a machine (from: the host's client, or -1 a client's host) */
static void relay_table(struct delta_peer *peer, struct delta_peer_relay *relay, int from, delta_u32 now,
	delta_u32 ipv4, unsigned short port, delta_u32 session, const unsigned char *payload, int size)
{
	struct delta_peer_table_in *in = &peer->table_in;
	struct delta_wire_table piece;
	delta_u32 serial = own_serial(peer);
	delta_u32 bit;
	char name[32];
	int taken;

	if (!delta_wire_read_table(payload, size, &piece))
	{
		peer->dropped++;
		return;
	}
	/* (it has one: its serial is newer than the one said) */
	if (relay->their_serial != DELTA_WIRE_TABLE_NONE && piece.serial > relay->their_serial)
		relay->their_serial = piece.serial;
	/* (one this machine takes no tables, or has: it says so, and is sent no
	more) */
	if (!relaying(peer) || serial == DELTA_WIRE_TABLE_NONE || piece.serial <= serial)
	{
		relay_say(peer, relay, now, ipv4, port, session, 1);
		return;
	}
	/* (one table at a time: another machine's, still coming, goes on) */
	if (in->active && in->from != from && !elapsed(now, in->time, DELTA_PEER_TABLE_STALE))
	{
		peer->dropped++;
		return;
	}
	if (!in->active || in->from != from || in->serial != piece.serial || in->total != piece.total)
	{
		in->active = 1;
		in->from = from;
		in->serial = piece.serial;
		in->total = piece.total;
		in->pieces = 0;
	}
	in->time = now;
	bit = (delta_u32)1 << (piece.offset / DELTA_WIRE_TABLE_CHUNK);
	if (!(in->pieces & bit))
	{
		memcpy(in->data + piece.offset, piece.data, (size_t)piece.length);
		in->pieces |= bit;
	}
	if (in->pieces != ((delta_u32)1 << ((in->total + DELTA_WIRE_TABLE_CHUNK - 1) / DELTA_WIRE_TABLE_CHUNK)) - 1)
		return;
	in->active = 0;
	relay_name(from, name, (int)sizeof(name));
	/* (a signature check a minute a machine: a later pass brings it again) */
	if (relay->checked && !elapsed(now, relay->check_time, DELTA_PEER_TABLE_CHECK_GAP))
	{
		say(peer, "Delta Peer: legacy table %u from %s set aside: one check a minute", (unsigned)in->serial, name);
		return;
	}
	relay->checked = 1;
	relay->check_time = now;
	taken = peer->env.legacy_table_offer(peer->env.context, in->data, (int)in->total);
	say(peer, "Delta Peer: legacy table %u from %s %s", (unsigned)in->serial, name, taken ? "taken" : "not taken");
	relay_say(peer, relay, now, ipv4, port, session, 1);
}

/* TABLE_HAVE from a machine */
static void relay_have(struct delta_peer *peer, struct delta_peer_relay *relay, const unsigned char *payload,
	int size)
{
	delta_u32 serial;

	if (!delta_wire_read_table_have(payload, size, &serial))
	{
		peer->dropped++;
		return;
	}
	relay->their_serial = serial;
}

/* ---------- the host */

static void forget_peer(struct delta_peer *peer, int machine_index, const char *why)
{
	if (peer->peers[machine_index].used)
	{
		say(peer, "Delta Peer: machine %d %s", machine_index, why);
		memset(&peer->peers[machine_index], 0, sizeof(peer->peers[machine_index]));
		if (peer->table_in.active && peer->table_in.from == machine_index)
			peer->table_in.active = 0;
		peer->roster_dirty = 1;
	}
}

/* the game's machine (its index) a datagram from ipv4 can be: the one of
machine_index there, or the only one there (DELTA_WIRE_NO_MACHINE); -1 if
none */
static int host_find_machine(const struct delta_peer *peer, delta_u32 ipv4, int machine_index)
{
	int found = -1;
	int index;

	for (index = 0; index < peer->game_machine_count; index++)
	{
		const struct delta_peer_game_machine *machine = &peer->game_machines[index];

		if (machine->local || machine->ipv4 != ipv4)
			continue;
		if (machine_index != DELTA_WIRE_NO_MACHINE)
		{
			if (machine->machine_index == machine_index)
				return machine_index;
		}
		else
		{
			if (found >= 0)
				return -1;
			found = machine->machine_index;
		}
	}
	return found;
}

/* the peer of a session from ipv4; -1 if none */
static int host_find_session(const struct delta_peer *peer, delta_u32 ipv4, delta_u32 session)
{
	int index;

	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		const struct delta_peer_host_peer *client = &peer->peers[index];

		if (client->used && client->ipv4 == ipv4 && client->session == session)
			return index;
	}
	return -1;
}

static void host_send_welcome(struct delta_peer *peer, int machine_index)
{
	struct delta_peer_host_peer const *client = &peer->peers[machine_index];
	struct delta_wire_welcome welcome;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int index;

	memset(&welcome, 0, sizeof(welcome));
	welcome.capabilities = offered(peer);
	welcome.agreed = client->agreed & welcome.capabilities;
	welcome.legacy_version = peer->local.legacy_version;
	welcome.host_machine_index = DELTA_WIRE_NO_MACHINE;
	for (index = 0; index < peer->game_machine_count; index++)
	{
		if (peer->game_machines[index].local)
			welcome.host_machine_index = peer->game_machines[index].machine_index;
	}
	welcome.legacy_table_serial = handshake_serial(peer);
	welcome.key = peer->local.key;
	memcpy(welcome.build, peer->local.build, sizeof(welcome.build));
	peer_send(peer, client->ipv4, client->port, data, delta_wire_write_welcome(data, client->session, &welcome));
}

static void host_hello(struct delta_peer *peer, delta_u32 now, delta_u32 ipv4, unsigned short port,
	const struct delta_wire_header *header, const unsigned char *payload)
{
	struct delta_wire_hello hello;
	struct delta_peer_host_peer *client;
	int machine_index;

	if (!delta_wire_read_hello(payload, header->length, &hello) || !header->session)
	{
		peer->dropped++;
		return;
	}
	machine_index = host_find_machine(peer, ipv4, hello.machine_index);
	if (!valid_machine(machine_index))
	{
		peer->dropped++;
		return;
	}
	client = &peer->peers[machine_index];
	/* (WELCOME lost: said again) */
	if (client->used && client->session == header->session && client->ipv4 == ipv4)
	{
		client->port = port;
		host_send_welcome(peer, machine_index);
		return;
	}
	if (client->used && !elapsed(now, client->session_time, DELTA_PEER_NEW_SESSION_GAP))
	{
		peer->dropped++;
		return;
	}
	memset(client, 0, sizeof(*client));
	client->used = 1;
	client->ipv4 = ipv4;
	client->port = port;
	client->session = header->session;
	client->session_time = now;
	client->hello = hello;
	client->agreed = hello.capabilities & offered(peer);
	relay_start(peer, &client->relay, now, hello.legacy_table_serial);
	if (peer->table_in.active && peer->table_in.from == machine_index)
		peer->table_in.active = 0;
	say(peer, "Delta Peer: machine %d speaks Delta (build %s, %s, network version %u); capabilities 0x%x, agreed 0x%x",
		machine_index, hello.build[0] ? hello.build : "?", platform_name(hello.key.platform),
		(unsigned)hello.legacy_version, (unsigned)hello.capabilities, (unsigned)client->agreed);
	host_send_welcome(peer, machine_index);
	peer->roster_dirty = 1;
}

static void host_receive(struct delta_peer *peer, delta_u32 now, delta_u32 ipv4, unsigned short port,
	const unsigned char *data, int size)
{
	struct delta_wire_header header;
	const unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int first;
	int index;

	/* (only the game's machines are heard, each address at the rate of its
	first machine's) */
	first = -1;
	for (index = 0; index < peer->game_machine_count && first < 0; index++)
	{
		if (!peer->game_machines[index].local && peer->game_machines[index].ipv4 == ipv4)
			first = peer->game_machines[index].machine_index;
	}
	if (!valid_machine(first) || !delta_rate_take(&peer->rates[first], now, DELTA_PEER_HOST_RATE,
		DELTA_PEER_HOST_BURST))
	{
		peer->dropped++;
		return;
	}
	if (!delta_wire_read_header(data, size, &header))
	{
		peer->dropped++;
		return;
	}
	if (header.major != DELTA_MAJOR)
	{
		unsigned char reply[DELTA_WIRE_HEADER_SIZE];

		if (header.type == _delta_message_hello)
		{
			say(peer, "Delta Peer: a machine speaks Delta major %u (this host's is %d): legacy only",
				(unsigned)header.major, DELTA_MAJOR);
			peer_send(peer, ipv4, port, reply, delta_wire_write_empty(reply, DELTA_MAJOR, _delta_message_legacy,
				header.session));
		}
		return;
	}
	switch (header.type)
	{
	case _delta_message_hello:
		host_hello(peer, now, ipv4, port, &header, payload);
		break;
	case _delta_message_profile:
	{
		int machine_index = host_find_session(peer, ipv4, header.session);
		struct delta_wire_profile profile;

		if (machine_index < 0 ||
			!(peer->peers[machine_index].agreed & offered(peer) & CAPABILITY(_delta_capability_profile)) ||
			!delta_wire_read_profile(payload, header.length, &profile))
		{
			peer->dropped++;
			break;
		}
		if (!peer->peers[machine_index].has_profile ||
			memcmp(&peer->peers[machine_index].profile, &profile, sizeof(profile)))
		{
			if (!peer->peers[machine_index].has_profile)
				say(peer, "Delta Peer: machine %d shares its profile", machine_index);
			peer->peers[machine_index].has_profile = 1;
			peer->peers[machine_index].profile = profile;
			peer->roster_dirty = 1;
		}
		break;
	}
	case _delta_message_bye:
	{
		int machine_index = host_find_session(peer, ipv4, header.session);

		if (machine_index >= 0)
			forget_peer(peer, machine_index, "left Delta");
		break;
	}
	case _delta_message_table:
	case _delta_message_table_have:
	{
		int machine_index = host_find_session(peer, ipv4, header.session);
		struct delta_peer_host_peer *client;

		if (machine_index < 0)
		{
			peer->dropped++;
			break;
		}
		client = &peer->peers[machine_index];
		if (header.type == _delta_message_table)
		{
			relay_table(peer, &client->relay, machine_index, now, client->ipv4, client->port, client->session,
				payload, header.length);
		}
		else
			relay_have(peer, &client->relay, payload, header.length);
		break;
	}
	default:
		/* (a type this build does not know, or one only a host sends) */
		break;
	}
}

/* the roster's entries for a client that agreed to these */
static void host_roster_entry(const struct delta_peer *peer, int machine_index, delta_u32 recipient_agreed,
	struct delta_wire_roster_entry *entry)
{
	const struct delta_peer_machine *machine = &peer->machines[machine_index];

	memset(entry, 0, sizeof(*entry));
	entry->machine_index = (unsigned char)machine_index;
	if (!machine->known)
		return;
	entry->flags = DELTA_ROSTER_DELTA;
	entry->capabilities = machine->capabilities;
	if ((recipient_agreed & machine->capabilities & CAPABILITY(_delta_capability_platform)) &&
		(machine->flags & DELTA_ROSTER_PLATFORM))
	{
		entry->flags |= DELTA_ROSTER_PLATFORM;
		entry->key = machine->key;
	}
	if ((recipient_agreed & machine->capabilities & CAPABILITY(_delta_capability_profile)) &&
		(machine->flags & DELTA_ROSTER_PROFILE))
	{
		entry->flags |= DELTA_ROSTER_PROFILE;
		entry->profile_revision = machine->profile.revision;
		memcpy(entry->player_id, machine->profile.player_id, sizeof(entry->player_id));
	}
}

static void host_send_rosters(struct delta_peer *peer)
{
	static struct delta_wire_roster roster;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int client_index;

	for (client_index = 0; client_index < DELTA_PEER_MAXIMUM_MACHINES; client_index++)
	{
		const struct delta_peer_host_peer *client = &peer->peers[client_index];
		int index;

		if (!client->used)
			continue;
		memset(&roster, 0, sizeof(roster));
		roster.room_capabilities = peer->room_capabilities;
		roster.room_players = peer->room_players;
		for (index = 0; index <= peer->game_machine_count; index++)
		{
			if (roster.count == DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES ||
				(index == peer->game_machine_count && roster.count))
			{
				peer_send(peer, client->ipv4, client->port, data, delta_wire_write_roster(data, client->session, &roster));
				roster.count = 0;
			}
			if (index < peer->game_machine_count)
			{
				host_roster_entry(peer, peer->game_machines[index].machine_index, client->agreed & offered(peer),
					&roster.entries[roster.count++]);
			}
		}
	}
}

void delta_peer_host_frame(struct delta_peer *peer, delta_u32 now, const struct delta_peer_game_machine *machines,
	int count, const signed char *player_machines)
{
	delta_u32 room_capabilities;
	int room_players;
	int index;

	enter_mode(peer, _mode_host);
	peer->game_machine_count = 0;
	for (index = 0; index < count && peer->game_machine_count < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		if (valid_machine(machines[index].machine_index))
			peer->game_machines[peer->game_machine_count++] = machines[index];
	}
	copy_players(peer, player_machines);

	/* (a machine gone from the game, or another now in its place, is
	forgotten) */
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		if (peer->peers[index].used && host_find_machine(peer, peer->peers[index].ipv4, index) != index)
			forget_peer(peer, index, "left the game");
	}

	memset(peer->machines, 0, sizeof(peer->machines));
	room_capabilities = offered(peer);
	room_players = peer->local.ignore_platform_limits ? DELTA_PEER_NO_LIMIT : host_limit(peer);
	for (index = 0; index < peer->game_machine_count; index++)
	{
		int machine_index = peer->game_machines[index].machine_index;
		struct delta_peer_machine *machine = &peer->machines[machine_index];
		const struct delta_peer_host_peer *client = &peer->peers[machine_index];

		if (peer->game_machines[index].local)
		{
			local_machine(peer, machine, now);
		}
		else if (client->used)
		{
			machine->known = 1;
			machine->flags = DELTA_ROSTER_DELTA;
			machine->capabilities = client->agreed & offered(peer);
			machine->key = client->hello.key;
			if (machine->capabilities & CAPABILITY(_delta_capability_platform))
				machine->flags |= DELTA_ROSTER_PLATFORM;
			if (client->has_profile)
			{
				machine->flags |= DELTA_ROSTER_PROFILE;
				machine->profile = client->profile;
			}
			machine->seen_time = now;
			/* (the platform policy counts whether or not it shares its
			platform for display) */
			if (!peer->local.ignore_platform_limits &&
				delta_peer_key_join_limit(peer, &client->hello.key) < room_players)
			{
				room_players = delta_peer_key_join_limit(peer, &client->hello.key);
			}
		}
		room_capabilities &= machine->known ? machine->capabilities : 0;
	}
	/* (said when it limits the game below the protocol's most, or stops) */
	if ((room_players < DELTA_PEER_MAXIMUM_PLAYERS ? room_players : DELTA_PEER_MAXIMUM_PLAYERS) !=
		(peer->room_players < DELTA_PEER_MAXIMUM_PLAYERS ? peer->room_players : DELTA_PEER_MAXIMUM_PLAYERS))
	{
		if (room_players >= DELTA_PEER_MAXIMUM_PLAYERS)
			say(peer, "Delta Peer: no machine of the game limits its players now");
		else
			say(peer, "Delta Peer: the game takes at most %d players now (its machines' platform limits)",
				room_players);
	}
	if (room_capabilities != peer->room_capabilities)
		say(peer, "Delta Peer: every machine of the game shares capabilities 0x%x", (unsigned)room_capabilities);
	if (room_capabilities != peer->room_capabilities || room_players != peer->room_players)
	{
		peer->room_capabilities = room_capabilities;
		peer->room_players = (unsigned char)room_players;
		peer->roster_dirty = 1;
	}

	if (!peer->roster_sent ||
		(peer->roster_dirty && elapsed(now, peer->roster_time, DELTA_PEER_ROSTER_GAP)) ||
		elapsed(now, peer->roster_time, DELTA_PEER_ROSTER_INTERVAL))
	{
		host_send_rosters(peer);
		peer->roster_sent = 1;
		peer->roster_time = now;
		peer->roster_dirty = 0;
	}

	{
		int sends = 0;

		for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
		{
			if (peer->peers[index].used && peer->peers[index].relay.sending)
				sends++;
		}
		for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
		{
			struct delta_peer_host_peer *client = &peer->peers[index];
			int was_sending = client->relay.sending;

			if (!client->used)
				continue;
			/* (counted once: those sending already are in sends) */
			if (was_sending)
				sends--;
			relay_frame(peer, &client->relay, index, now, client->ipv4, client->port, client->session, &sends);
		}
	}
}

/* ---------- the client */

static void client_send_hello(struct delta_peer *peer, delta_u32 now)
{
	struct delta_wire_hello hello;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];

	memset(&hello, 0, sizeof(hello));
	hello.capabilities = offered(peer);
	hello.legacy_version = peer->local.legacy_version;
	hello.machine_index = peer->machine_index;
	hello.legacy_table_serial = handshake_serial(peer);
	hello.key = peer->local.key;
	memcpy(hello.build, peer->local.build, sizeof(hello.build));
	peer_send(peer, peer->host_ipv4, peer->host_port, data, delta_wire_write_hello(data, peer->session, &hello));
	peer->hello_time = now;
}

static void client_send_profile(struct delta_peer *peer)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];

	if ((peer->agreed & offered(peer) & CAPABILITY(_delta_capability_profile)) && peer->local.has_profile)
	{
		peer_send(peer, peer->host_ipv4, peer->host_port, data,
			delta_wire_write_profile(data, peer->session, &peer->local.profile));
	}
}

static void client_legacy(struct delta_peer *peer, const char *why)
{
	int own = peer->machine_index;
	struct delta_peer_machine local = peer->machines[valid_machine(own) ? own : 0];

	say(peer, "Delta Peer: %s: the legacy protocol alone with this host", why);
	peer->client_state = _delta_peer_client_legacy;
	peer->agreed = 0;
	peer->room_capabilities = 0;
	peer->room_players = DELTA_PEER_NO_LIMIT;
	memset(&peer->host_relay, 0, sizeof(peer->host_relay));
	peer->table_in.active = 0;
	memset(peer->machines, 0, sizeof(peer->machines));
	if (valid_machine(own))
		peer->machines[own] = local;
}

void delta_peer_client_frame(struct delta_peer *peer, delta_u32 now, int joined, delta_u32 host_ipv4,
	unsigned short host_port, int host_speaks_delta, unsigned char machine_index, const signed char *player_machines)
{
	int index;

	if (!joined)
	{
		if (peer->mode == _mode_client)
			delta_peer_stop(peer);
		return;
	}
	enter_mode(peer, _mode_client);
	copy_players(peer, player_machines);
	if (machine_index != peer->machine_index && valid_machine(peer->machine_index))
		memset(&peer->machines[peer->machine_index], 0, sizeof(peer->machines[0]));
	peer->machine_index = machine_index;
	/* (this machine knows itself, Delta or not) */
	if (valid_machine(machine_index))
		local_machine(peer, &peer->machines[machine_index], now);

	switch (peer->client_state)
	{
	case _delta_peer_client_off:
		if (!host_speaks_delta)
		{
			say(peer, "Delta Peer: the host does not speak Delta: the legacy protocol alone");
			peer->client_state = _delta_peer_client_legacy;
			break;
		}
		/* (HELLO says which machine this is: once the host has said) */
		if (!valid_machine(machine_index))
			break;
		peer->host_ipv4 = host_ipv4;
		peer->host_port = host_port;
		do
			peer->session = peer->env.random_number ? peer->env.random_number(peer->env.context) : 1;
		while (!peer->session);
		peer->handshake_start = now;
		peer->client_state = _delta_peer_client_waiting;
		say(peer, "Delta Peer: the host speaks Delta: HELLO sent");
		client_send_hello(peer, now);
		break;
	case _delta_peer_client_waiting:
		if (elapsed(now, peer->handshake_start, DELTA_PEER_HANDSHAKE_TIME))
			client_legacy(peer, "no answer to HELLO");
		else if (elapsed(now, peer->hello_time, DELTA_PEER_HELLO_INTERVAL))
			client_send_hello(peer, now);
		break;
	case _delta_peer_client_delta:
		/* (machines the roster stopped naming, or with no players now, but
		the host's own) */
		for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
		{
			struct delta_peer_machine *machine = &peer->machines[index];
			int players = 0;
			int player;

			if (!machine->known || index == machine_index || index == peer->welcome.host_machine_index)
				continue;
			for (player = 0; player < DELTA_PEER_MAXIMUM_PLAYERS && !players; player++)
				players = peer->player_machines[player] == index;
			if (!players || elapsed(now, machine->seen_time, DELTA_PEER_ROSTER_EXPIRY))
				memset(machine, 0, sizeof(*machine));
		}
		relay_frame(peer, &peer->host_relay, -1, now, peer->host_ipv4, peer->host_port, peer->session, NULL);
		break;
	}
}

static void client_receive(struct delta_peer *peer, delta_u32 now, delta_u32 ipv4, unsigned short port,
	const unsigned char *data, int size)
{
	struct delta_wire_header header;
	const unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	if ((peer->client_state != _delta_peer_client_waiting && peer->client_state != _delta_peer_client_delta) ||
		ipv4 != peer->host_ipv4 || port != peer->host_port ||
		!delta_rate_take(&peer->host_rate, now, DELTA_PEER_CLIENT_RATE, DELTA_PEER_CLIENT_BURST) ||
		!delta_wire_read_header(data, size, &header) || header.session != peer->session)
	{
		peer->dropped++;
		return;
	}
	if (header.major != DELTA_MAJOR)
	{
		if (header.type == _delta_message_legacy)
		{
			char why[64];

			snprintf(why, sizeof(why), "the host speaks Delta major %u", (unsigned)header.major);
			client_legacy(peer, why);
		}
		return;
	}
	switch (header.type)
	{
	case _delta_message_welcome:
	{
		struct delta_wire_welcome welcome;
		struct delta_peer_machine *host;

		if (!delta_wire_read_welcome(payload, header.length, &welcome))
		{
			peer->dropped++;
			break;
		}
		if (peer->client_state == _delta_peer_client_delta)
			break;
		peer->welcome = welcome;
		/* (never more than this machine offered, whatever the host says) */
		peer->agreed = welcome.agreed & welcome.capabilities & offered(peer);
		peer->client_state = _delta_peer_client_delta;
		say(peer, "Delta Peer: the host speaks Delta (build %s, %s, network version %u); capabilities 0x%x, agreed 0x%x",
			welcome.build[0] ? welcome.build : "?", platform_name(welcome.key.platform),
			(unsigned)welcome.legacy_version, (unsigned)welcome.capabilities, (unsigned)peer->agreed);
		if (valid_machine(welcome.host_machine_index) && welcome.host_machine_index != peer->machine_index)
		{
			host = &peer->machines[welcome.host_machine_index];
			memset(host, 0, sizeof(*host));
			host->known = 1;
			host->flags = DELTA_ROSTER_DELTA;
			host->capabilities = welcome.capabilities;
			if (peer->agreed & CAPABILITY(_delta_capability_platform))
			{
				host->flags |= DELTA_ROSTER_PLATFORM;
				host->key = welcome.key;
			}
			host->seen_time = now;
		}
		relay_start(peer, &peer->host_relay, now, welcome.legacy_table_serial);
		client_send_profile(peer);
		break;
	}
	case _delta_message_legacy:
		client_legacy(peer, "the host answered LEGACY");
		break;
	case _delta_message_roster:
	{
		static struct delta_wire_roster roster;
		int index;

		if (peer->client_state != _delta_peer_client_delta || !delta_wire_read_roster(payload, header.length, &roster))
		{
			peer->dropped++;
			break;
		}
		if ((roster.room_capabilities & peer->agreed & offered(peer)) != peer->room_capabilities)
		{
			say(peer, "Delta Peer: every machine of the game shares capabilities 0x%x",
				(unsigned)(roster.room_capabilities & peer->agreed & offered(peer)));
		}
		peer->room_capabilities = roster.room_capabilities & peer->agreed & offered(peer);
		peer->room_players = roster.room_players ? roster.room_players : DELTA_PEER_NO_LIMIT;
		for (index = 0; index < roster.count; index++)
		{
			struct delta_wire_roster_entry *entry = &roster.entries[index];
			struct delta_peer_machine *machine;

			if (!valid_machine(entry->machine_index))
				continue;
			if (entry->machine_index == peer->machine_index)
			{
				/* (the host lost this machine's profile: sent again) */
				if (!(entry->flags & DELTA_ROSTER_PROFILE))
					client_send_profile(peer);
				continue;
			}
			machine = &peer->machines[entry->machine_index];
			/* (what this machine no longer offers is not kept: the kill
			switch, mid-game) */
			if (!(offered(peer) & CAPABILITY(_delta_capability_platform)))
				entry->flags &= ~DELTA_ROSTER_PLATFORM;
			if (!(offered(peer) & CAPABILITY(_delta_capability_profile)))
				entry->flags &= ~DELTA_ROSTER_PROFILE;
			/* (said when a machine's platform is first known, or changes) */
			if ((entry->flags & DELTA_ROSTER_PLATFORM) &&
				(!(machine->flags & DELTA_ROSTER_PLATFORM) || machine->key.platform != entry->key.platform))
			{
				say(peer, "Delta Peer: machine %d is %s%s", entry->machine_index, platform_name(entry->key.platform),
					entry->flags & DELTA_ROSTER_PROFILE ? ", with a profile" : "");
			}
			memset(machine, 0, sizeof(*machine));
			if (!(entry->flags & DELTA_ROSTER_DELTA))
				continue;
			machine->known = 1;
			machine->flags = entry->flags;
			machine->capabilities = entry->capabilities;
			if (entry->flags & DELTA_ROSTER_PLATFORM)
				machine->key = entry->key;
			if (entry->flags & DELTA_ROSTER_PROFILE)
			{
				machine->profile.revision = entry->profile_revision;
				memcpy(machine->profile.player_id, entry->player_id, sizeof(machine->profile.player_id));
			}
			machine->seen_time = now;
		}
		break;
	}
	case _delta_message_bye:
		client_legacy(peer, "the host stopped Delta");
		break;
	case _delta_message_table:
		if (peer->client_state != _delta_peer_client_delta)
		{
			peer->dropped++;
			break;
		}
		relay_table(peer, &peer->host_relay, -1, now, peer->host_ipv4, peer->host_port, peer->session, payload,
			header.length);
		break;
	case _delta_message_table_have:
		if (peer->client_state != _delta_peer_client_delta)
		{
			peer->dropped++;
			break;
		}
		relay_have(peer, &peer->host_relay, payload, header.length);
		break;
	default:
		break;
	}
}

void delta_peer_receive(struct delta_peer *peer, delta_u32 now, delta_u32 ipv4, unsigned short port,
	const unsigned char *data, int size)
{
	if (!data || size <= 0 || size > DELTA_WIRE_MAXIMUM_DATAGRAM)
	{
		peer->dropped++;
		return;
	}
	if (peer->mode == _mode_host)
		host_receive(peer, now, ipv4, port, data, size);
	else if (peer->mode == _mode_client)
		client_receive(peer, now, ipv4, port, data, size);
	else
		peer->dropped++;
}

/* ---------- what is known */

int delta_peer_machine(const struct delta_peer *peer, int machine_index, struct delta_peer_machine *machine)
{
	if (!valid_machine(machine_index) || !peer->machines[machine_index].known)
		return 0;
	if (machine)
		*machine = peer->machines[machine_index];
	return 1;
}

int delta_peer_room_has(const struct delta_peer *peer, int capability)
{
	if (capability < 0 || capability >= 32)
		return 0;
	if (peer->mode == _mode_client && peer->client_state != _delta_peer_client_delta)
		return 0;
	if (peer->mode == _mode_off)
		return 0;
	return (peer->room_capabilities & CAPABILITY(capability)) != 0;
}

int delta_peer_room_coop(const struct delta_peer *peer)
{
	int index;

	if (peer->mode != _mode_host || peer->local.ignore_platform_limits)
		return 1;
	if (!delta_platform_policy_coop(peer->local.key.platform))
		return 0;
	/* (the policy counts whether or not a machine shares its platform for
	display, as the player limit does) */
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		if (peer->peers[index].used && !delta_platform_policy_coop(peer->peers[index].hello.key.platform))
			return 0;
	}
	return 1;
}

int delta_peer_room_limit(const struct delta_peer *peer, int limit)
{
	if (limit != _delta_peer_limit_players || peer->mode == _mode_off)
		return DELTA_PEER_NO_LIMIT;
	return peer->room_players ? peer->room_players : DELTA_PEER_NO_LIMIT;
}
