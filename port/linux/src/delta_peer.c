/*
DELTA_PEER.C

Delta Peer's sessions (delta_peer.h): the host's handshakes and roster, the
client's handshake, and what each knows of the game's machines. No sockets
and no platform here: struct delta_peer_env sends, logs and draws random
numbers, so the tests (tools/test_delta_peer.py) run a host and clients in
one process.
*/

#include "delta_peer.h"
#include "delta_chat.h"

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum
{
	_mode_off = 0,
	_mode_client,
	_mode_host
};

/* the capabilities this build knows (the retired md_maps and console_slots
bits never count) */
#define KNOWN_CAPABILITIES \
	((((delta_u32)1 << NUMBER_OF_DELTA_CAPABILITIES) - 1) & ~((delta_u32)1 << _delta_capability_md_maps) & \
		~((delta_u32)1 << _delta_capability_console_slots))
#define CAPABILITY(bit) ((delta_u32)1 << (bit))
/* TRAITS (22) needs both: bomb alone is a Grifball test build 2 or 3's,
which says TRAITS as 18 */
#define BOMB_CAPABILITIES (CAPABILITY(_delta_capability_bomb) | CAPABILITY(_delta_capability_bomb_traits))

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

/* (delta.h's registry, its keys) */
#define PLATFORM_KEY(platform, key, tag, name, caps_name) key,
static const char *const platform_names[] = { DELTA_PLATFORM_NAMES(PLATFORM_KEY) };
#undef PLATFORM_KEY
typedef char platform_names_check[sizeof(platform_names) / sizeof(platform_names[0]) == NUMBER_OF_DELTA_PLATFORMS ?
	1 : -1];

static const char *platform_name(int platform)
{
	return platform >= 0 && platform < NUMBER_OF_DELTA_PLATFORMS ? platform_names[platform] : "unknown";
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

	/* (a host without moderation does not offer it) */
	if (peer->mode == _mode_host && !peer->moderation_host)
		capabilities &= ~CAPABILITY(_delta_capability_moderation);
	/* (nor team changes without its team host) */
	if (peer->mode == _mode_host && !peer->team_host)
		capabilities &= ~CAPABILITY(_delta_capability_team_change);
	/* (nor reports without its report host) */
	if (peer->mode == _mode_host && !peer->report_host)
		capabilities &= ~CAPABILITY(_delta_capability_reports);
	/* (nor voice without its rules) */
	if (peer->mode == _mode_host && !peer->voice_hosting)
		capabilities &= ~CAPABILITY(_delta_capability_voice);
	/* (nor verified names without a way to check a proof) */
	if (!peer->env.identity_check)
		capabilities &= ~CAPABILITY(_delta_capability_identity);
	/* (nor a badge without one) */
	if (peer->mode == _mode_host && !peer->badge_host)
		capabilities &= ~CAPABILITY(_delta_capability_server_badge);
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

static void host_forget_moderation(struct delta_peer *peer, int machine_index);
/* (verified names: below) */
static void host_start_identity(struct delta_peer *peer, int machine_index);
static void host_withdraw_all(struct delta_peer *peer);
static void host_send_identity_challenge(struct delta_peer *peer, int machine_index, delta_u32 now);
static void host_identity_proof(struct delta_peer *peer, delta_u32 now, int machine_index,
	const struct delta_wire_header *header, const unsigned char *payload);
static void host_identity_frame(struct delta_peer *peer, delta_u32 now);
static void client_identity_receive(struct delta_peer *peer, delta_u32 now, const struct delta_wire_header *header,
	const unsigned char *payload);
static void client_identity_frame(struct delta_peer *peer, delta_u32 now);

static void forget_all(struct delta_peer *peer)
{
	int index;

	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
		host_forget_moderation(peer, index);
	/* (every verified card's moderation told it is gone: nothing outlives
	the session) */
	if (peer->mode == _mode_host)
		host_withdraw_all(peer);
	memset(&peer->client_moderation, 0, sizeof(peer->client_moderation));
	memset(peer->game_machines, 0, sizeof(peer->game_machines));
	peer->game_machine_count = 0;
	memset(peer->peers, 0, sizeof(peer->peers));
	memset(peer->rates, 0, sizeof(peer->rates));
	memset(peer->table_checked, 0, sizeof(peer->table_checked));
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
	memset(&peer->host_map, 0, sizeof(peer->host_map));
	peer->host_map_generation = 0;
	memset(&peer->host_traits, 0, sizeof(peer->host_traits));
	peer->host_traits_generation = 0;
	memset(&peer->host_rules, 0, sizeof(peer->host_rules));
	peer->host_rules_generation = 0;
	peer->host_number = 0;
	peer->host_number_generation = 0;
	memset(peer->machine_seen, 0, sizeof(peer->machine_seen));
	memset(peer->machine_seen_ipv4, 0, sizeof(peer->machine_seen_ipv4));
	memset(peer->machine_seen_time, 0, sizeof(peer->machine_seen_time));
	memset(peer->machines, 0, sizeof(peer->machines));
	peer->room_capabilities = 0;
	peer->room_players = DELTA_PEER_NO_LIMIT;
	memset(peer->player_machines, -1, sizeof(peer->player_machines));
	memset(peer->chat_muted, 0, sizeof(peer->chat_muted));
	memset(peer->chat_mute_until, 0, sizeof(peer->chat_mute_until));
	memset(&peer->client_chat, 0, sizeof(peer->client_chat));
	memset(peer->voice_talking_until, 0, sizeof(peer->voice_talking_until));
	memset(peer->voice_rates, 0, sizeof(peer->voice_rates));
	memset(&peer->voice_world, 0, sizeof(peer->voice_world));
	peer->client_voice_has_config = 0;
	memset(&peer->client_voice_config, 0, sizeof(peer->client_voice_config));
	peer->client_voice_said = 0;
	memset(&peer->client_voice_rate, 0, sizeof(peer->client_voice_rate));
	memset(peer->cards, 0, sizeof(peer->cards));
	peer->identity_nonce_made = 0;
	peer->identity_own_time = 0;
	peer->identity_own_binding_length = 0;
	memset(peer->identity_own_binding, 0, sizeof(peer->identity_own_binding));
	peer->identity_challenged = 0;
	memset(&peer->identity_challenge, 0, sizeof(peer->identity_challenge));
	memset(peer->identity_sent, 0, sizeof(peer->identity_sent));
	memset(&peer->client_badge, 0, sizeof(peer->client_badge));
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
	peer->chat_mode = _delta_chat_mode_on;
	peer->chat_cooldown = DELTA_PEER_CHAT_COOLDOWN;
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

			peer->table_out_serial = size > 0 && size <= (int)sizeof(peer->table_out) ? serial : 0;
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
	/* (a signature check a minute a machine: a later pass brings it again;
	a client's is kept by machine, as a new session would start it over) */
	if (from >= 0 ? peer->table_checked[from] && !elapsed(now, peer->table_check_time[from],
		DELTA_PEER_TABLE_CHECK_GAP) : relay->checked && !elapsed(now, relay->check_time, DELTA_PEER_TABLE_CHECK_GAP))
	{
		say(peer, "Delta Peer: legacy table %u from %s set aside: one check a minute", (unsigned)in->serial, name);
		return;
	}
	if (from >= 0)
	{
		peer->table_checked[from] = 1;
		peer->table_check_time[from] = now;
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

static void host_withdraw_machine(struct delta_peer *peer, int machine_index, delta_u32 now);

static void forget_peer(struct delta_peer *peer, int machine_index, const char *why)
{
	if (peer->peers[machine_index].used)
	{
		say(peer, "Delta Peer: machine %d %s", machine_index, why);
		/* (its cards withdrawn as of the last roster's time: kept until every
		client hears) */
		host_withdraw_machine(peer, machine_index, peer->roster_time);
		host_forget_moderation(peer, machine_index);
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

/* ---------- moderation: the host's */

/* whether a signature is a moderator key's, of message: never for a key of
small order (anyone can sign for one) */
static int moderation_verify(const unsigned char *key, const unsigned char *message, int size,
	const unsigned char *signature)
{
	static const unsigned char zero[32];
	static const unsigned char scalar[32] = { 1 };
	unsigned char x25519[32];
	unsigned char product[32];

	crypto_eddsa_to_x25519(x25519, key);
	crypto_x25519(product, scalar, x25519);
	if (!crypto_verify32(product, zero))
		return 0;
	return crypto_ed25519_check(signature, key, message, (size_t)size) == 0;
}

/* whether a client's session agreed to moderation, with a host that has it */
static int host_moderating(const struct delta_peer *peer, int machine_index)
{
	return valid_machine(machine_index) && peer->moderation_host && peer->peers[machine_index].used &&
		(peer->peers[machine_index].agreed & offered(peer) & CAPABILITY(_delta_capability_moderation));
}

/* a machine's key forgotten: its session ended, or another took its place */
static void host_forget_moderation(struct delta_peer *peer, int machine_index)
{
	struct delta_peer_host_moderation *moderation = &peer->peers[machine_index].moderation;

	if (peer->peers[machine_index].used && moderation->verified && peer->moderation_host &&
		peer->moderation_host->machine_key)
	{
		peer->moderation_host->machine_key(peer->moderation_host->context, machine_index, NULL);
	}
	memset(moderation, 0, sizeof(*moderation));
}

static void host_send_challenge(struct delta_peer *peer, int machine_index)
{
	struct delta_peer_host_peer const *client = &peer->peers[machine_index];
	struct delta_wire_mod_challenge challenge;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];

	if (!host_moderating(peer, machine_index) || !client->moderation.challenged)
		return;
	memset(&challenge, 0, sizeof(challenge));
	memcpy(challenge.nonce, client->moderation.nonce, sizeof(challenge.nonce));
	challenge.binding_length = client->moderation.binding_length;
	memcpy(challenge.binding, client->moderation.binding, sizeof(challenge.binding));
	peer_send(peer, client->ipv4, client->port, data, delta_wire_write_mod_challenge(data, client->session, &challenge));
}

/* a new session that agreed to moderation: its nonce, and the challenge */
static void host_start_moderation(struct delta_peer *peer, int machine_index)
{
	struct delta_peer_host_moderation *moderation = &peer->peers[machine_index].moderation;
	int index;

	memset(moderation, 0, sizeof(*moderation));
	if (!host_moderating(peer, machine_index) || !peer->env.random_number)
		return;
	for (index = 0; index < DELTA_WIRE_MODERATION_NONCE_SIZE; index += 4)
	{
		delta_u32 value = peer->env.random_number(peer->env.context);

		moderation->nonce[index] = (unsigned char)value;
		moderation->nonce[index + 1] = (unsigned char)(value >> 8);
		moderation->nonce[index + 2] = (unsigned char)(value >> 16);
		moderation->nonce[index + 3] = (unsigned char)(value >> 24);
	}
	moderation->binding_length = peer->moderation_binding_length;
	memcpy(moderation->binding, peer->moderation_binding, sizeof(moderation->binding));
	moderation->challenged = 1;
	host_send_challenge(peer, machine_index);
}

/* MOD_STATE to a machine signed in: always, or only if its role changed */
static void host_send_state(struct delta_peer *peer, int machine_index, int always)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];
	struct delta_peer_host_moderation *moderation = &client->moderation;
	struct delta_wire_mod_state state;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	delta_u32 permissions = 0, ban_minutes = 0;
	int role = _delta_moderation_role_none;

	if (!host_moderating(peer, machine_index) || !moderation->verified)
		return;
	if (peer->moderation_host->machine_role)
		role = peer->moderation_host->machine_role(peer->moderation_host->context, machine_index, moderation->key,
			&permissions, &ban_minutes);
	else if (peer->moderation_host->key_role)
		role = peer->moderation_host->key_role(peer->moderation_host->context, moderation->key, &permissions,
			&ban_minutes);
	memset(&state, 0, sizeof(state));
	if (role > _delta_moderation_role_none && role <= _delta_moderation_role_owner)
	{
		state.role = (unsigned char)role;
		state.permissions = permissions;
		state.ban_minutes = ban_minutes;
		if (peer->moderation_host->actions)
			state.actions = (unsigned char)(peer->moderation_host->actions(peer->moderation_host->context) &
				DELTA_WIRE_MOD_STATE_KNOWN);
	}
	if (!always && moderation->state_sent && !memcmp(&state, &moderation->state, sizeof(state)))
		return;
	if (moderation->state_sent && memcmp(&state, &moderation->state, sizeof(state)))
		say(peer, "Delta Peer: machine %d's moderator role is now %d", machine_index, (int)state.role);
	moderation->state = state;
	moderation->state_sent = 1;
	peer_send(peer, client->ipv4, client->port, data, delta_wire_write_mod_state(data, client->session, &state));
}

static void host_send_bind(struct delta_peer *peer, int machine_index)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];

	peer_send(peer, client->ipv4, client->port, data,
		delta_wire_write_mod_bind(data, client->session, &client->moderation.bind));
}

/* a key proved for a machine's session */
static void host_verified(struct delta_peer *peer, int machine_index, const unsigned char *key)
{
	struct delta_peer_host_moderation *moderation = &peer->peers[machine_index].moderation;

	if (moderation->verified && !memcmp(moderation->key, key, sizeof(moderation->key)))
		return;
	if (moderation->verified && peer->moderation_host->machine_key)
		peer->moderation_host->machine_key(peer->moderation_host->context, machine_index, NULL);
	memcpy(moderation->key, key, sizeof(moderation->key));
	moderation->verified = 1;
	moderation->sequence = 0;
	moderation->state_sent = 0;
	say(peer, "Delta Peer: machine %d signed in with its moderator key", machine_index);
	if (peer->moderation_host->machine_key)
		peer->moderation_host->machine_key(peer->moderation_host->context, machine_index, key);
	host_send_state(peer, machine_index, 1);
}

static void host_moderation_receive(struct delta_peer *peer, delta_u32 now, int machine_index,
	const struct delta_wire_header *header, const unsigned char *payload)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];
	struct delta_peer_host_moderation *moderation = &client->moderation;
	unsigned char message[DELTA_WIRE_MODERATION_MESSAGE_SIZE];
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int length;

	if (!host_moderating(peer, machine_index) || !moderation->challenged)
	{
		peer->dropped++;
		return;
	}
	switch (header->type)
	{
	case _delta_message_mod_proof:
	{
		struct delta_wire_mod_proof proof;

		if (!delta_wire_read_mod_proof(payload, header->length, &proof) ||
			!delta_rate_take(&moderation->signature_rate, now, DELTA_PEER_MODERATION_SIGNATURE_RATE,
				DELTA_PEER_MODERATION_SIGNATURE_BURST))
		{
			peer->dropped++;
			break;
		}
		length = delta_wire_moderation_proof_message(message, moderation->nonce, moderation->binding,
			moderation->binding_length);
		if (!moderation_verify(proof.key, message, length, proof.signature))
		{
			say(peer, "Delta Peer: machine %d's moderator sign-in is not signed right", machine_index);
			peer->dropped++;
			break;
		}
		host_verified(peer, machine_index, proof.key);
		break;
	}
	case _delta_message_mod_action:
	{
		struct delta_wire_mod_action action;
		struct delta_wire_mod_result result;
		char text[DELTA_WIRE_MODERATION_TEXT_SIZE + 1];

		if (!moderation->verified || !delta_wire_read_mod_action(payload, header->length, &action) ||
			!delta_rate_take(&moderation->action_rate, now, DELTA_PEER_MODERATION_ACTION_RATE,
				DELTA_PEER_MODERATION_ACTION_BURST))
		{
			peer->dropped++;
			break;
		}
		/* (a replay, or one sent before: never done twice) */
		if (action.sequence <= moderation->sequence)
		{
			peer->dropped++;
			break;
		}
		length = delta_wire_moderation_action_message(message, moderation->nonce, &action);
		if (!moderation_verify(moderation->key, message, length, action.signature))
		{
			say(peer, "Delta Peer: machine %d's moderator action is not signed right", machine_index);
			peer->dropped++;
			break;
		}
		moderation->sequence = action.sequence;
		memset(&result, 0, sizeof(result));
		result.sequence = action.sequence;
		text[0] = 0;
		if (action.action < _delta_moderation_action_warn || action.action >= NUMBER_OF_DELTA_MODERATION_ACTIONS)
			snprintf(text, sizeof(text), "This server does not know that action.");
		else if (!peer->moderation_host->action)
			snprintf(text, sizeof(text), "This server takes no actions from the game.");
		else
		{
			result.ok = (unsigned char)(peer->moderation_host->action(peer->moderation_host->context, machine_index,
				moderation->key, action.action, action.target, action.minutes, action.reason, text,
				(int)sizeof(text)) ? 1 : 0);
		}
		text[sizeof(text) - 1] = 0;
		memcpy(result.text, text, sizeof(result.text));
		peer_send(peer, client->ipv4, client->port, data, delta_wire_write_mod_result(data, client->session, &result));
		break;
	}
	case _delta_message_mod_bind_answer:
	{
		struct delta_wire_mod_bind_answer answer;

		if (!delta_wire_read_mod_bind_answer(payload, header->length, &answer) || !moderation->binding_request ||
			answer.request != moderation->bind.request ||
			!delta_rate_take(&moderation->signature_rate, now, DELTA_PEER_MODERATION_SIGNATURE_RATE,
				DELTA_PEER_MODERATION_SIGNATURE_BURST))
		{
			peer->dropped++;
			break;
		}
		/* (a session signed in answers with the key it signed in with) */
		if (moderation->verified && memcmp(moderation->key, answer.key, sizeof(answer.key)))
		{
			peer->dropped++;
			break;
		}
		length = delta_wire_moderation_bind_message(message, moderation->nonce, answer.request, answer.accepted,
			moderation->binding, moderation->binding_length);
		if (!moderation_verify(answer.key, message, length, answer.signature))
		{
			say(peer, "Delta Peer: machine %d's answer to a link is not signed right", machine_index);
			peer->dropped++;
			break;
		}
		moderation->binding_request = 0;
		say(peer, "Delta Peer: machine %d %s the link to %s", machine_index, answer.accepted ? "accepted" : "declined",
			moderation->bind.account);
		host_verified(peer, machine_index, answer.key);
		if (peer->moderation_host->bind_answer)
		{
			peer->moderation_host->bind_answer(peer->moderation_host->context, machine_index, answer.request,
				answer.accepted, answer.key);
		}
		break;
	}
	default:
		break;
	}
}

/* each frame: the challenge again to a machine not signed in, MOD_STATE to
those signed in (at once when roles changed), a waiting bind again or
given up */
static void host_moderation_frame(struct delta_peer *peer, delta_u32 now)
{
	int periodic = elapsed(now, peer->moderation_time, DELTA_PEER_ROSTER_INTERVAL);
	int dirty = peer->moderation_roles_dirty;
	int index;

	if (!peer->moderation_host)
		return;
	if (periodic)
		peer->moderation_time = now;
	peer->moderation_roles_dirty = 0;
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		struct delta_peer_host_moderation *moderation = &peer->peers[index].moderation;

		if (!host_moderating(peer, index))
			continue;
		if (moderation->binding_request && elapsed(now, moderation->bind_time, DELTA_PEER_MODERATION_BIND_TIME))
		{
			moderation->binding_request = 0;
			say(peer, "Delta Peer: machine %d did not answer the link to %s", index, moderation->bind.account);
			if (peer->moderation_host->bind_answer)
			{
				peer->moderation_host->bind_answer(peer->moderation_host->context, index, moderation->bind.request, 0,
					NULL);
			}
		}
		if (!periodic && !dirty)
			continue;
		if (moderation->verified)
			host_send_state(peer, index, periodic);
		else if (periodic)
			host_send_challenge(peer, index);
		if (periodic && moderation->binding_request)
			host_send_bind(peer, index);
	}
}

void delta_peer_set_moderation_host(struct delta_peer *peer, const struct delta_peer_moderation_host *host)
{
	int index;

	if (peer->moderation_host == host)
		return;
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
		host_forget_moderation(peer, index);
	peer->moderation_host = host;
}

void delta_peer_identity_recheck(struct delta_peer *peer)
{
	peer->identity_recheck = 1;
}

void delta_peer_set_moderation_binding(struct delta_peer *peer, const char *binding)
{
	int length = 0;

	while (binding && length < DELTA_WIRE_MODERATION_BINDING_SIZE && binding[length] >= 0x20 &&
		binding[length] <= 0x7E)
	{
		peer->moderation_binding[length] = binding[length];
		length++;
	}
	peer->moderation_binding[length] = 0;
	peer->moderation_binding_length = length;
}

int delta_peer_moderation_key(const struct delta_peer *peer, int machine_index, unsigned char *key)
{
	if (peer->mode != _mode_host || !host_moderating(peer, machine_index) ||
		!peer->peers[machine_index].moderation.verified)
	{
		return 0;
	}
	if (key)
		memcpy(key, peer->peers[machine_index].moderation.key, DELTA_WIRE_MODERATION_KEY_SIZE);
	return 1;
}

int delta_peer_moderation_capable(const struct delta_peer *peer, int machine_index)
{
	return peer->mode == _mode_host && host_moderating(peer, machine_index);
}

int delta_peer_moderation_notice(struct delta_peer *peer, int machine_index, int kind, const char *text)
{
	struct delta_peer_host_peer *client;
	struct delta_wire_mod_notice notice;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];

	if (peer->mode != _mode_host || !host_moderating(peer, machine_index) || !text)
		return 0;
	client = &peer->peers[machine_index];
	memset(&notice, 0, sizeof(notice));
	notice.kind = (unsigned char)kind;
	snprintf(notice.text, sizeof(notice.text), "%s", text);
	peer_send(peer, client->ipv4, client->port, data, delta_wire_write_mod_notice(data, client->session, &notice));
	return 1;
}

int delta_peer_moderation_bind(struct delta_peer *peer, delta_u32 now, int machine_index, delta_u32 request,
	const char *account, const char *server)
{
	struct delta_peer_host_moderation *moderation;

	if (peer->mode != _mode_host || !host_moderating(peer, machine_index) || !account || !server)
		return 0;
	moderation = &peer->peers[machine_index].moderation;
	if (!moderation->challenged)
		return 0;
	memset(&moderation->bind, 0, sizeof(moderation->bind));
	moderation->bind.request = request;
	snprintf(moderation->bind.account, sizeof(moderation->bind.account), "%s", account);
	snprintf(moderation->bind.server, sizeof(moderation->bind.server), "%s", server);
	moderation->binding_request = 1;
	moderation->bind_time = now;
	host_send_bind(peer, machine_index);
	return 1;
}

void delta_peer_moderation_roles_changed(struct delta_peer *peer)
{
	peer->moderation_roles_dirty = 1;
}

/* ---------- chat (docs/delta.md, Chat) */

/* whether a machine's session takes lines of a kind: a player's need chat
agreed, the host's own server_messages */
static int host_chat_takes(const struct delta_peer *peer, int machine_index, int kind)
{
	int capability = kind == _delta_chat_kind_server || kind == _delta_chat_kind_system ?
		_delta_capability_server_messages : _delta_capability_chat;

	return peer->mode == _mode_host && valid_machine(machine_index) && peer->peers[machine_index].used &&
		(peer->peers[machine_index].agreed & offered(peer) & CAPABILITY(capability));
}

/* a machine's chat is told the host's (either capability) */
static int host_chat_configured(const struct delta_peer *peer, int machine_index)
{
	return host_chat_takes(peer, machine_index, _delta_chat_kind_player) ||
		host_chat_takes(peer, machine_index, _delta_chat_kind_server);
}

int delta_peer_chat_muted(const struct delta_peer *peer, delta_u32 now, int machine_index)
{
	if (!valid_machine(machine_index) || !peer->chat_muted[machine_index])
		return 0;
	/* (a timed mute that has ended: until is in the past) */
	if (peer->chat_mute_until[machine_index] && (delta_u32)(now - peer->chat_mute_until[machine_index]) < 0x80000000u)
		return 0;
	return 1;
}

/* CHAT_CONFIG for a machine as it is now */
static void host_chat_config(const struct delta_peer *peer, delta_u32 now, int machine_index,
	struct delta_wire_chat_config *config)
{
	memset(config, 0, sizeof(*config));
	config->mode = peer->chat_mode;
	config->flags = (unsigned char)(peer->chat_flags & DELTA_CHAT_CONFIG_FILTERED);
	if (delta_peer_chat_muted(peer, now, machine_index))
	{
		config->flags |= DELTA_CHAT_CONFIG_MUTED;
		if (peer->chat_mute_until[machine_index])
			config->mute_seconds = (peer->chat_mute_until[machine_index] - now + 999u) / 1000u;
	}
}

static void host_send_chat_config(struct delta_peer *peer, delta_u32 now, int machine_index)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];

	host_chat_config(peer, now, machine_index, &client->chat.config);
	client->chat.config_sent = 1;
	client->chat.config_time = now;
	peer_send(peer, client->ipv4, client->port, data, delta_wire_write_chat_config(data, client->session,
		&client->chat.config));
}

/* each frame: mutes of machines gone forgotten, timed ones ended; CHAT_CONFIG
to each machine whose changed (no sooner than the roster's gap), and again
every DELTA_PEER_CHAT_CONFIG_INTERVAL */
static void host_chat_frame(struct delta_peer *peer, delta_u32 now)
{
	int index;

	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		int present = 0, game;

		if (!peer->chat_muted[index])
			continue;
		for (game = 0; game < peer->game_machine_count && !present; game++)
			present = peer->game_machines[game].machine_index == index;
		if (!present || !delta_peer_chat_muted(peer, now, index))
		{
			peer->chat_muted[index] = 0;
			peer->chat_mute_until[index] = 0;
			peer->chat_dirty = 1;
		}
	}
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		struct delta_peer_host_peer *client = &peer->peers[index];
		struct delta_wire_chat_config config;

		if (!host_chat_configured(peer, index))
			continue;
		host_chat_config(peer, now, index, &config);
		/* (the seconds left alone are no change) */
		if (!client->chat.config_sent || elapsed(now, client->chat.config_time, DELTA_PEER_CHAT_CONFIG_INTERVAL) ||
			((config.mode != client->chat.config.mode || config.flags != client->chat.config.flags) &&
				elapsed(now, client->chat.config_time, DELTA_PEER_ROSTER_GAP)))
		{
			host_send_chat_config(peer, now, index);
		}
	}
	peer->chat_dirty = 0;
}

static void host_chat_refuse_paused(struct delta_peer *peer, int machine_index, delta_u32 sequence, int reason,
	delta_u32 paused_milliseconds)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];
	struct delta_wire_chat_refused refused;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	delta_u32 seconds = (paused_milliseconds + 999) / 1000;

	memset(&refused, 0, sizeof(refused));
	refused.sequence = sequence;
	refused.reason = (unsigned char)reason;
	refused.paused_seconds = (unsigned short)(seconds > 0xFFFF ? 0xFFFF : seconds);
	peer_send(peer, client->ipv4, client->port, data, delta_wire_write_chat_refused(data, client->session, &refused));
}

static void host_chat_refuse(struct delta_peer *peer, int machine_index, delta_u32 sequence, int reason)
{
	host_chat_refuse_paused(peer, machine_index, sequence, reason, 0);
}

static void host_chat_say(struct delta_peer *peer, delta_u32 now, int machine_index,
	const struct delta_wire_header *header, const unsigned char *payload)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];
	struct delta_wire_chat_say say;
	delta_u32 fingerprint;
	int reason;

	if (!host_chat_takes(peer, machine_index, _delta_chat_kind_player) || !peer->chat_host ||
		!delta_wire_read_chat_say(payload, header->length, &say) || say.sequence <= client->chat.sequence)
	{
		peer->dropped++;
		return;
	}
	client->chat.sequence = say.sequence;
	/* (paused: refused, saying how much longer, until the pause is over) */
	if (client->chat.paused)
	{
		delta_u32 left = client->chat.paused_until - now;

		if (left && left < 0x80000000u)
		{
			host_chat_refuse_paused(peer, machine_index, say.sequence, _delta_chat_refused_too_fast, left);
			return;
		}
		client->chat.paused = 0;
	}
	if (!delta_rate_take(&client->chat.rate, now, DELTA_PEER_CHAT_SAY_RATE, DELTA_PEER_CHAT_SAY_BURST))
	{
		if (peer->chat_cooldown)
		{
			client->chat.paused = 1;
			client->chat.paused_until = now + peer->chat_cooldown;
		}
		host_chat_refuse_paused(peer, machine_index, say.sequence, _delta_chat_refused_too_fast, peer->chat_cooldown);
		return;
	}
	say.length = delta_chat_trim(say.text, say.length);
	if (delta_wire_chat_text_valid(say.text, say.length, DELTA_WIRE_CHAT_SAY_CHARACTERS) < 0)
	{
		host_chat_refuse(peer, machine_index, say.sequence, _delta_chat_refused_bad_text);
		return;
	}
	if (peer->chat_mode == _delta_chat_mode_off)
	{
		host_chat_refuse(peer, machine_index, say.sequence, _delta_chat_refused_off);
		return;
	}
	if (delta_peer_chat_muted(peer, now, machine_index))
	{
		host_chat_refuse(peer, machine_index, say.sequence, _delta_chat_refused_muted);
		return;
	}
	fingerprint = delta_chat_fingerprint(say.text, say.length);
	if (client->chat.has_last && client->chat.last_fingerprint == fingerprint &&
		!elapsed(now, client->chat.last_time, DELTA_CHAT_REPEAT_TIME))
	{
		host_chat_refuse(peer, machine_index, say.sequence, _delta_chat_refused_too_fast);
		return;
	}
	reason = peer->chat_host->say(peer->chat_host->context, machine_index, say.scope, say.text, say.length);
	if (reason)
	{
		host_chat_refuse(peer, machine_index, say.sequence, reason);
		return;
	}
	client->chat.has_last = 1;
	client->chat.last_fingerprint = fingerprint;
	client->chat.last_time = now;
}

void delta_peer_set_chat_host(struct delta_peer *peer, const struct delta_peer_chat_host *host)
{
	peer->chat_host = host;
}

void delta_peer_set_chat_mode(struct delta_peer *peer, int mode, int flags)
{
	if (mode < _delta_chat_mode_off || mode > _delta_chat_mode_on)
		mode = _delta_chat_mode_on;
	if (peer->chat_mode != mode || peer->chat_flags != (unsigned char)flags)
	{
		peer->chat_mode = (unsigned char)mode;
		peer->chat_flags = (unsigned char)flags;
		peer->chat_dirty = 1;
	}
}

void delta_peer_set_chat_cooldown(struct delta_peer *peer, int seconds)
{
	peer->chat_cooldown = seconds > 0 ? (delta_u32)seconds * 1000 : 0;
}

void delta_peer_chat_mute(struct delta_peer *peer, delta_u32 now, int machine_index, int mute, int minutes)
{
	if (!valid_machine(machine_index))
		return;
	peer->chat_muted[machine_index] = (unsigned char)(mute ? 1 : 0);
	peer->chat_mute_until[machine_index] = 0;
	if (mute && minutes > 0)
	{
		/* (at most a day of milliseconds: the clock wraps in 49) */
		if (minutes > 24 * 60)
			minutes = 24 * 60;
		peer->chat_mute_until[machine_index] = now + (delta_u32)minutes * 60000u;
		/* (0 is "until the game ends") */
		if (!peer->chat_mute_until[machine_index])
			peer->chat_mute_until[machine_index] = 1;
	}
	if (peer->mode == _mode_host && host_chat_configured(peer, machine_index))
		host_send_chat_config(peer, now, machine_index);
}

int delta_peer_chat_capable(const struct delta_peer *peer, int machine_index, int kind)
{
	return host_chat_takes(peer, machine_index, kind);
}

int delta_peer_chat_line(struct delta_peer *peer, int machine_index, const struct delta_wire_chat_line *line)
{
	struct delta_peer_host_peer *client;
	struct delta_wire_chat_line sent;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int size;

	if (!line || !host_chat_takes(peer, machine_index, line->kind))
		return 0;
	client = &peer->peers[machine_index];
	sent = *line;
	sent.line = ++peer->chat_lines_sent;
	size = delta_wire_write_chat_line(data, client->session, &sent);
	if (!size)
		return 0;
	peer_send(peer, client->ipv4, client->port, data, size);
	return 1;
}

static int client_chatting(const struct delta_peer *peer, int capability)
{
	return peer->mode == _mode_client && peer->client_state == _delta_peer_client_delta &&
		(peer->agreed & offered(peer) & CAPABILITY(capability));
}

int delta_peer_client_chat_ready(const struct delta_peer *peer)
{
	return client_chatting(peer, _delta_capability_chat);
}

delta_u32 delta_peer_client_chat_say(struct delta_peer *peer, int scope, const char *text, int length)
{
	struct delta_wire_chat_say say;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int size;

	if (!delta_peer_client_chat_ready(peer) || !text || length < 1 || length > DELTA_WIRE_CHAT_SAY_TEXT_SIZE ||
		delta_wire_chat_text_valid(text, length, DELTA_WIRE_CHAT_SAY_CHARACTERS) < 0)
	{
		return 0;
	}
	memset(&say, 0, sizeof(say));
	say.sequence = ++peer->client_chat.sequence;
	if (!say.sequence)
		say.sequence = ++peer->client_chat.sequence;
	say.scope = (unsigned char)scope;
	say.length = length;
	memcpy(say.text, text, (size_t)length);
	size = delta_wire_write_chat_say(data, peer->session, &say);
	if (!size)
		return 0;
	peer_send(peer, peer->host_ipv4, peer->host_port, data, size);
	return say.sequence;
}

delta_u32 delta_peer_client_chat_received(const struct delta_peer *peer)
{
	return peer->client_chat.received;
}

int delta_peer_client_chat_get(const struct delta_peer *peer, delta_u32 index, struct delta_wire_chat_line *line)
{
	if (index >= peer->client_chat.received ||
		peer->client_chat.received - index > (delta_u32)DELTA_PEER_CHAT_LINES)
	{
		return 0;
	}
	if (line)
		*line = peer->client_chat.lines[index % DELTA_PEER_CHAT_LINES];
	return 1;
}

/* a chat message from the host */
static void client_chat_receive(struct delta_peer *peer, delta_u32 now, const struct delta_wire_header *header,
	const unsigned char *payload)
{
	struct delta_peer_client_chat *chat = &peer->client_chat;

	switch (header->type)
	{
	case _delta_message_chat_config:
	{
		struct delta_wire_chat_config config;

		if ((!client_chatting(peer, _delta_capability_chat) &&
			!client_chatting(peer, _delta_capability_server_messages)) ||
			!delta_wire_read_chat_config(payload, header->length, &config))
		{
			peer->dropped++;
			break;
		}
		chat->has_config = 1;
		chat->config = config;
		chat->config_time = now;
		break;
	}
	case _delta_message_chat_line:
	{
		struct delta_wire_chat_line line;
		int capability;

		if (!delta_wire_read_chat_line(payload, header->length, &line))
		{
			peer->dropped++;
			break;
		}
		capability = line.kind == _delta_chat_kind_server || line.kind == _delta_chat_kind_system ?
			_delta_capability_server_messages : _delta_capability_chat;
		/* (a name must be text too; a line's text is the host's at most) */
		if (!client_chatting(peer, capability) ||
			!delta_rate_take(&chat->rate, now, DELTA_PEER_CHAT_LINE_RATE, DELTA_PEER_CHAT_LINE_BURST) ||
			delta_wire_chat_text_valid(line.text, line.length, 0) < 0 ||
			(line.name_length && delta_wire_chat_text_valid(line.name, line.name_length, 0) < 0))
		{
			peer->dropped++;
			break;
		}
		chat->lines[chat->received % DELTA_PEER_CHAT_LINES] = line;
		chat->received++;
		break;
	}
	case _delta_message_chat_refused:
	{
		struct delta_wire_chat_refused refused;

		if (!client_chatting(peer, _delta_capability_chat) ||
			!delta_wire_read_chat_refused(payload, header->length, &refused))
		{
			peer->dropped++;
			break;
		}
		chat->refused = refused;
		chat->refused_count++;
		break;
	}
	default:
		break;
	}
}

/* ---------- voice */

int delta_peer_voice_route(const struct delta_wire_voice_config *rules, const struct delta_peer_voice_world *world,
	int speaker_machine, int listener_machine, int channel)
{
	const struct delta_peer_voice_member *speaker, *listener;

	if (!rules || !world || !valid_machine(speaker_machine) || !valid_machine(listener_machine) ||
		speaker_machine == listener_machine)
	{
		return _delta_voice_route_none;
	}
	speaker = &world->members[speaker_machine];
	listener = &world->members[listener_machine];
	if (!speaker->present || !listener->present)
		return _delta_voice_route_none;
	switch (channel)
	{
	case _delta_voice_channel_party:
		/* (the parties' tags are the caller's to match) */
		return rules->channels & DELTA_VOICE_PARTY ? _delta_voice_route_global : _delta_voice_route_none;
	case _delta_voice_channel_team:
	case _delta_voice_channel_proximity:
	case _delta_voice_channel_everyone:
		break;
	default:
		return _delta_voice_route_none;
	}
	/* (outside a game, the lobby: everyone hears everyone, if the host lets
	them, whatever the channel) */
	if (!world->in_game)
		return rules->channels & DELTA_VOICE_LOBBY ? _delta_voice_route_global : _delta_voice_route_none;
	switch (channel)
	{
	case _delta_voice_channel_everyone:
		return rules->channels & DELTA_VOICE_EVERYONE ? _delta_voice_route_global : _delta_voice_route_none;
	case _delta_voice_channel_team:
		/* (co-op: everyone is a teammate; free-for-all: nobody is) */
		if (!(rules->channels & DELTA_VOICE_TEAM))
			return _delta_voice_route_none;
		if (world->coop || (world->teams && speaker->team == listener->team))
			return _delta_voice_route_global;
		return _delta_voice_route_none;
	default:
	{
		/* (proximity: both alive, within the host's range) */
		float range = (float)rules->proximity / 10.0f;
		float dx = speaker->position[0] - listener->position[0];
		float dy = speaker->position[1] - listener->position[1];
		float dz = speaker->position[2] - listener->position[2];

		if (!(rules->channels & DELTA_VOICE_PROXIMITY) || !speaker->alive || !listener->alive)
			return _delta_voice_route_none;
		return dx * dx + dy * dy + dz * dz <= range * range ? _delta_voice_route_proximity : _delta_voice_route_none;
	}
	}
}

static int host_voice_takes(const struct delta_peer *peer, int machine_index)
{
	return peer->mode == _mode_host && peer->voice_hosting && valid_machine(machine_index) &&
		peer->peers[machine_index].used &&
		(peer->peers[machine_index].agreed & offered(peer) & CAPABILITY(_delta_capability_voice));
}

/* the host's own machine in its game (a dedicated server has none: -1) */
static int host_local_machine(const struct delta_peer *peer)
{
	int index;

	for (index = 0; index < peer->game_machine_count; index++)
	{
		if (peer->game_machines[index].local)
			return peer->game_machines[index].machine_index;
	}
	return -1;
}

static int voice_channel_bit(int channel)
{
	switch (channel)
	{
	case _delta_voice_channel_team: return DELTA_VOICE_TEAM;
	case _delta_voice_channel_proximity: return DELTA_VOICE_PROXIMITY;
	case _delta_voice_channel_everyone: return DELTA_VOICE_EVERYONE;
	case _delta_voice_channel_party: return DELTA_VOICE_PARTY;
	default: return 0;
	}
}

static int voice_party_set(const unsigned char *party)
{
	int index, any = 0;

	for (index = 0; index < DELTA_WIRE_VOICE_PARTY_TAG_SIZE; index++)
		any |= party[index];
	return any != 0;
}

/* whether the host relays a channel now: one of its channels in a game,
any but party in the lobby with lobby voice, party anywhere */
static int voice_channel_open(const struct delta_peer *peer, int channel)
{
	int bit = voice_channel_bit(channel);

	if (!bit)
		return 0;
	if (channel != _delta_voice_channel_party && !peer->voice_world.in_game)
		return (peer->voice_rules.channels & DELTA_VOICE_LOBBY) != 0;
	return (peer->voice_rules.channels & bit) != 0;
}

int delta_peer_voice_talking(const struct delta_peer *peer, delta_u32 now, int machine_index)
{
	return valid_machine(machine_index) && peer->voice_talking_until[machine_index] &&
		(delta_u32)(peer->voice_talking_until[machine_index] - now) < 0x80000000u;
}

/* VOICE_CONFIG for a machine as it is now */
static void host_voice_config(const struct delta_peer *peer, delta_u32 now, int machine_index,
	struct delta_wire_voice_config *config)
{
	*config = peer->voice_rules;
	config->flags = 0;
	config->mute_seconds = 0;
	if (delta_peer_chat_muted(peer, now, machine_index))
	{
		config->flags |= DELTA_VOICE_CONFIG_MUTED;
		if (peer->chat_mute_until[machine_index])
			config->mute_seconds = (peer->chat_mute_until[machine_index] - now + 999u) / 1000u;
	}
}

/* each frame: VOICE_CONFIG to each machine whose changed (no sooner than the
roster's gap), and again every DELTA_PEER_VOICE_CONFIG_INTERVAL */
static void host_voice_frame(struct delta_peer *peer, delta_u32 now)
{
	int index;

	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		struct delta_peer_host_peer *client = &peer->peers[index];
		struct delta_wire_voice_config config;
		unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];

		if (!host_voice_takes(peer, index))
			continue;
		host_voice_config(peer, now, index, &config);
		if (client->voice.config_sent && !elapsed(now, client->voice.config_time, DELTA_PEER_VOICE_CONFIG_INTERVAL) &&
			((config.channels == client->voice.config.channels && config.flags == client->voice.config.flags &&
				config.kbps == client->voice.config.kbps && config.proximity == client->voice.config.proximity) ||
				!elapsed(now, client->voice.config_time, DELTA_PEER_ROSTER_GAP)))
		{
			continue;
		}
		client->voice.config = config;
		client->voice.config_sent = 1;
		client->voice.config_time = now;
		peer_send(peer, client->ipv4, client->port, data, delta_wire_write_voice_config(data, client->session, &config));
	}
	peer->voice_dirty = 0;
}

/* a frame of a machine's player (the host's own: speaker_machine its
machine), checked: to each machine whose player may hear it */
static int host_voice_relay(struct delta_peer *peer, delta_u32 now, int speaker_machine, int channel,
	unsigned short sequence, const unsigned char *packet, int length, const unsigned char *speaker_party)
{
	struct delta_wire_voice_down down;
	int index, talkers = 0, sent = 0, local = host_local_machine(peer);

	if (!voice_channel_open(peer, channel) || length <= 0 ||
		length > delta_wire_voice_packet_cap(peer->voice_rules.kbps) || delta_peer_chat_muted(peer, now, speaker_machine))
	{
		peer->dropped++;
		return 0;
	}
	/* (at most DELTA_PEER_VOICE_TALKERS at once on a dedicated server, and
	DELTA_PEER_VOICE_TALKERS_LISTEN on a player's host; one talking keeps its
	place, a new one waits) */
	if (!delta_peer_voice_talking(peer, now, speaker_machine))
	{
		for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
			talkers += delta_peer_voice_talking(peer, now, index);
		if (talkers >= (local < 0 ? DELTA_PEER_VOICE_TALKERS : DELTA_PEER_VOICE_TALKERS_LISTEN))
		{
			peer->dropped++;
			return 0;
		}
	}
	peer->voice_talking_until[speaker_machine] = now + DELTA_PEER_VOICE_TALKING_TIME;
	if (!peer->voice_talking_until[speaker_machine])
		peer->voice_talking_until[speaker_machine] = 1;

	memset(&down, 0, sizeof(down));
	down.sequence = sequence;
	down.speaker_machine = (unsigned char)speaker_machine;
	down.speaker_player = peer->voice_world.members[speaker_machine].present ?
		peer->voice_world.members[speaker_machine].player : DELTA_WIRE_NO_MACHINE;
	down.channel = (unsigned char)channel;
	down.length = (unsigned char)length;
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		const unsigned char *party;
		int hears, route;

		if (index == speaker_machine)
			continue;
		if (index == local)
		{
			hears = peer->voice_local_hears;
			party = peer->voice_local_party;
			if (!peer->voice_client)
				continue;
		}
		else
		{
			const struct delta_peer_host_peer *client = &peer->peers[index];

			if (!host_voice_takes(peer, index) || !client->voice.has_hears ||
				elapsed(now, client->voice.hears_time, DELTA_PEER_VOICE_HEARS_EXPIRY))
			{
				continue;
			}
			hears = client->voice.hears;
			party = client->voice.party;
		}
		if (!(hears & voice_channel_bit(channel)))
			continue;
		if (channel == _delta_voice_channel_party &&
			(!speaker_party || !voice_party_set(speaker_party) ||
				memcmp(speaker_party, party, DELTA_WIRE_VOICE_PARTY_TAG_SIZE)))
		{
			continue;
		}
		route = delta_peer_voice_route(&peer->voice_rules, &peer->voice_world, speaker_machine, index, channel);
		if (route == _delta_voice_route_none)
			continue;
		down.route = (unsigned char)route;
		if (index == local)
		{
			peer->voice_client->heard(peer->voice_client->context, &down, packet);
		}
		else
		{
			const struct delta_peer_host_peer *client = &peer->peers[index];
			unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];

			peer_send(peer, client->ipv4, client->port, data,
				delta_wire_write_voice_down(data, client->session, &down, packet));
		}
		sent++;
	}
	peer->voice_relayed++;
	return sent;
}

static void host_voice_up(struct delta_peer *peer, delta_u32 now, int machine_index,
	const struct delta_wire_header *header, const unsigned char *payload)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];
	struct delta_wire_voice_up up;
	const unsigned char *packet;

	if (!host_voice_takes(peer, machine_index) || !delta_wire_read_voice_up(payload, header->length, &up, &packet))
	{
		peer->dropped++;
		return;
	}
	/* (any VOICE_UP says what it hears; one with no frame its party too) */
	client->voice.has_hears = 1;
	client->voice.hears = up.hears;
	client->voice.hears_time = now;
	if (!up.length)
	{
		memcpy(client->voice.party, up.party, sizeof(client->voice.party));
		return;
	}
	host_voice_relay(peer, now, machine_index, up.channel, up.sequence, packet, up.length, client->voice.party);
}

void delta_peer_set_voice_rules(struct delta_peer *peer, const struct delta_wire_voice_config *rules)
{
	struct delta_wire_voice_config clean;

	if (!rules)
	{
		peer->voice_hosting = 0;
		return;
	}
	clean = *rules;
	clean.channels &= DELTA_VOICE_KNOWN_CONFIG;
	clean.flags = 0;
	clean.mute_seconds = 0;
	if (clean.kbps < DELTA_WIRE_VOICE_MINIMUM_KBPS)
		clean.kbps = DELTA_WIRE_VOICE_MINIMUM_KBPS;
	if (clean.kbps > DELTA_WIRE_VOICE_MAXIMUM_KBPS)
		clean.kbps = DELTA_WIRE_VOICE_MAXIMUM_KBPS;
	if (clean.proximity < DELTA_WIRE_VOICE_MINIMUM_PROXIMITY)
		clean.proximity = DELTA_WIRE_VOICE_MINIMUM_PROXIMITY;
	if (clean.proximity > DELTA_WIRE_VOICE_MAXIMUM_PROXIMITY)
		clean.proximity = DELTA_WIRE_VOICE_MAXIMUM_PROXIMITY;
	if (!peer->voice_hosting || memcmp(&clean, &peer->voice_rules, sizeof(clean)))
		peer->voice_dirty = 1;
	peer->voice_rules = clean;
	peer->voice_hosting = 1;
}

void delta_peer_set_voice_world(struct delta_peer *peer, const struct delta_peer_voice_world *world)
{
	if (world)
		peer->voice_world = *world;
	else
		memset(&peer->voice_world, 0, sizeof(peer->voice_world));
}

int delta_peer_host_voice_say(struct delta_peer *peer, delta_u32 now, int channel, unsigned short sequence,
	const unsigned char *packet, int length)
{
	int local = host_local_machine(peer);

	if (peer->mode != _mode_host || !peer->voice_hosting || local < 0 || !packet)
		return 0;
	return host_voice_relay(peer, now, local, channel, sequence, packet, length, peer->voice_local_party) >= 0;
}

void delta_peer_set_voice_local(struct delta_peer *peer, int hears, const unsigned char *party)
{
	peer->voice_local_hears = (unsigned char)(hears & DELTA_VOICE_CHANNELS);
	if (party)
		memcpy(peer->voice_local_party, party, sizeof(peer->voice_local_party));
	else
		memset(peer->voice_local_party, 0, sizeof(peer->voice_local_party));
}

void delta_peer_set_voice_client(struct delta_peer *peer, const struct delta_peer_voice_client *client)
{
	peer->voice_client = client;
}

static int client_voicing(const struct delta_peer *peer)
{
	return peer->mode == _mode_client && peer->client_state == _delta_peer_client_delta &&
		(peer->agreed & offered(peer) & CAPABILITY(_delta_capability_voice));
}

int delta_peer_client_voice_config(const struct delta_peer *peer, delta_u32 now, struct delta_wire_voice_config *config)
{
	if (!client_voicing(peer) || !peer->client_voice_has_config ||
		elapsed(now, peer->client_voice_config_time, DELTA_PEER_VOICE_CONFIG_EXPIRY))
	{
		if (config)
			memset(config, 0, sizeof(*config));
		return 0;
	}
	if (config)
		*config = peer->client_voice_config;
	return 1;
}

static void client_send_voice_hears(struct delta_peer *peer, delta_u32 now)
{
	struct delta_wire_voice_up up;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];

	memset(&up, 0, sizeof(up));
	up.hears = peer->client_voice_hears;
	memcpy(up.party, peer->client_voice_party, sizeof(up.party));
	peer->client_voice_said = 1;
	peer->client_voice_said_time = now;
	peer_send(peer, peer->host_ipv4, peer->host_port, data, delta_wire_write_voice_up(data, peer->session, &up, NULL));
}

/* (client, each frame) what it hears, every DELTA_PEER_VOICE_HEARS_INTERVAL
while the host has said its rules */
static void client_voice_frame(struct delta_peer *peer, delta_u32 now)
{
	if (!delta_peer_client_voice_config(peer, now, NULL))
		return;
	if (!peer->client_voice_said || elapsed(now, peer->client_voice_said_time, DELTA_PEER_VOICE_HEARS_INTERVAL))
		client_send_voice_hears(peer, now);
}

void delta_peer_client_set_voice_hears(struct delta_peer *peer, int hears, const unsigned char *party)
{
	unsigned char tag[DELTA_WIRE_VOICE_PARTY_TAG_SIZE];

	hears &= DELTA_VOICE_CHANNELS;
	if (party)
		memcpy(tag, party, sizeof(tag));
	else
		memset(tag, 0, sizeof(tag));
	if (hears == peer->client_voice_hears && !memcmp(tag, peer->client_voice_party, sizeof(tag)))
		return;
	peer->client_voice_hears = (unsigned char)hears;
	memcpy(peer->client_voice_party, tag, sizeof(tag));
	/* (said with the next frame's) */
	peer->client_voice_said = 0;
}

int delta_peer_client_voice_say(struct delta_peer *peer, delta_u32 now, int channel, unsigned short sequence,
	const unsigned char *packet, int length)
{
	struct delta_wire_voice_up up;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int size;

	if (!delta_peer_client_voice_config(peer, now, NULL) || !packet || length <= 0 ||
		length > delta_wire_voice_packet_cap(peer->client_voice_config.kbps))
	{
		return 0;
	}
	memset(&up, 0, sizeof(up));
	up.sequence = sequence;
	up.length = (unsigned char)length;
	up.channel = (unsigned char)channel;
	up.hears = peer->client_voice_hears;
	size = delta_wire_write_voice_up(data, peer->session, &up, packet);
	if (!size)
		return 0;
	peer_send(peer, peer->host_ipv4, peer->host_port, data, size);
	/* (a frame says what it hears as well) */
	peer->client_voice_said = 1;
	peer->client_voice_said_time = now;
	return 1;
}

static void client_voice_receive(struct delta_peer *peer, delta_u32 now, const struct delta_wire_header *header,
	const unsigned char *payload)
{
	if (header->type == _delta_message_voice_config)
	{
		struct delta_wire_voice_config config;

		if (!client_voicing(peer) || !delta_wire_read_voice_config(payload, header->length, &config))
		{
			peer->dropped++;
			return;
		}
		peer->client_voice_has_config = 1;
		peer->client_voice_config = config;
		peer->client_voice_config_time = now;
		return;
	}
	{
		struct delta_wire_voice_down down;
		const unsigned char *packet;

		/* (its own voice, a frame over the host's quality, or one while the
		player hears no voice: dropped) */
		if (!client_voicing(peer) || !peer->client_voice_has_config ||
			!delta_wire_read_voice_down(payload, header->length, &down, &packet) ||
			down.speaker_machine == peer->machine_index || !peer->client_voice_hears ||
			down.length > delta_wire_voice_packet_cap(peer->client_voice_config.kbps))
		{
			peer->dropped++;
			return;
		}
		peer->voice_heard++;
		if (peer->voice_client)
			peer->voice_client->heard(peer->voice_client->context, &down, packet);
	}
}

/* ---------- team changes */

void delta_peer_set_team_host(struct delta_peer *peer, const struct delta_peer_team_host *host)
{
	peer->team_host = host;
}

static void host_team_request(struct delta_peer *peer, delta_u32 now, int machine_index,
	const struct delta_wire_header *header, const unsigned char *payload)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];
	struct delta_wire_team_request request;
	struct delta_wire_team_result result;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int team_now = 0, seconds = 0;

	if (!(client->agreed & offered(peer) & CAPABILITY(_delta_capability_team_change)) || !peer->team_host ||
		!delta_wire_read_team_request(payload, header->length, &request) || request.sequence <= client->team_sequence ||
		!delta_rate_take(&client->team_rate, now, DELTA_PEER_TEAM_RATE, DELTA_PEER_TEAM_BURST))
	{
		peer->dropped++;
		return;
	}
	client->team_sequence = request.sequence;
	memset(&result, 0, sizeof(result));
	result.sequence = request.sequence;
	result.result = (unsigned char)peer->team_host->request(peer->team_host->context, machine_index, request.player,
		request.team, &team_now, &seconds);
	result.team = (unsigned char)(team_now == 1 ? 1 : 0);
	result.seconds = (unsigned short)(seconds < 0 ? 0 : seconds > 0xFFFF ? 0xFFFF : seconds);
	peer_send(peer, client->ipv4, client->port, data, delta_wire_write_team_result(data, client->session, &result));
}

/* ---------- server badges */

void delta_peer_set_badge_host(struct delta_peer *peer, const struct delta_peer_badge_host *host)
{
	peer->badge_host = host;
}

static void host_badge_ask(struct delta_peer *peer, delta_u32 now, int machine_index,
	const struct delta_wire_header *header, const unsigned char *payload)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];
	struct delta_wire_badge badge;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int length;

	if (!(client->agreed & offered(peer) & CAPABILITY(_delta_capability_server_badge)) || !peer->badge_host ||
		!delta_wire_read_badge_ask(payload, header->length, badge.nonce) ||
		!delta_rate_take(&client->badge_rate, now, DELTA_PEER_BADGE_RATE, DELTA_PEER_BADGE_BURST))
	{
		peer->dropped++;
		return;
	}
	memset(badge.signature, 0, sizeof(badge.signature));
	length = peer->badge_host->answer(peer->badge_host->context, badge.nonce, client->session, badge.signature,
		badge.certificate, DELTA_WIRE_BADGE_CERTIFICATE_SIZE);
	badge.proof = DELTA_WIRE_BADGE_PROOF_BOUND;
	/* (none now: said as none, so the client stops asking) */
	badge.certificate_length = (unsigned short)(length > 0 && length <= DELTA_WIRE_BADGE_CERTIFICATE_SIZE ? length : 0);
	peer_send(peer, client->ipv4, client->port, data, delta_wire_write_badge(data, client->session, &badge));
}

static void client_badge_frame(struct delta_peer *peer, delta_u32 now)
{
	struct delta_peer_client_badge *badge = &peer->client_badge;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int index;

	if (!client_chatting(peer, _delta_capability_server_badge) || badge->state == _delta_peer_badge_answered)
		return;
	if (badge->state == _delta_peer_badge_none)
	{
		/* (a nonce of this session's, never one the host could have signed
		before) */
		for (index = 0; index < DELTA_WIRE_BADGE_NONCE_SIZE; index += 4)
		{
			delta_u32 value = peer->env.random_number ? peer->env.random_number(peer->env.context) : 0;

			badge->nonce[index] = (unsigned char)value;
			badge->nonce[index + 1] = (unsigned char)(value >> 8);
			badge->nonce[index + 2] = (unsigned char)(value >> 16);
			badge->nonce[index + 3] = (unsigned char)(value >> 24);
		}
		badge->state = _delta_peer_badge_asking;
	}
	else if (badge->asks >= DELTA_PEER_BADGE_ASKS || !elapsed(now, badge->ask_time, DELTA_PEER_BADGE_INTERVAL))
		return;
	badge->asks++;
	badge->ask_time = now;
	peer_send(peer, peer->host_ipv4, peer->host_port, data, delta_wire_write_badge_ask(data, peer->session, badge->nonce));
}

static void client_badge_receive(struct delta_peer *peer, const struct delta_wire_header *header,
	const unsigned char *payload)
{
	struct delta_peer_client_badge *badge = &peer->client_badge;
	struct delta_wire_badge answer;

	/* (only the answer to this session's ask, once) */
	if (!client_chatting(peer, _delta_capability_server_badge) || badge->state != _delta_peer_badge_asking ||
		!delta_wire_read_badge(payload, header->length, &answer) ||
		memcmp(answer.nonce, badge->nonce, sizeof(badge->nonce)))
	{
		peer->dropped++;
		return;
	}
	badge->badge = answer;
	badge->state = _delta_peer_badge_answered;
}

int delta_peer_client_badge(const struct delta_peer *peer, struct delta_wire_badge *badge, delta_u32 *handshake_start)
{
	if (badge)
		*badge = peer->client_badge.badge;
	if (handshake_start)
		*handshake_start = peer->handshake_start;
	if (peer->mode != _mode_client || peer->client_state != _delta_peer_client_delta)
		return _delta_peer_badge_none;
	if (!client_chatting(peer, _delta_capability_server_badge))
		return _delta_peer_badge_unoffered;
	return peer->client_badge.state;
}

int delta_peer_client_team_ready(const struct delta_peer *peer)
{
	return client_chatting(peer, _delta_capability_team_change);
}

delta_u32 delta_peer_client_team_request(struct delta_peer *peer, int player, int team)
{
	struct delta_wire_team_request request;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int size;

	if (!delta_peer_client_team_ready(peer) || player < 0 || player > 255)
		return 0;
	memset(&request, 0, sizeof(request));
	request.sequence = ++peer->team_sequence;
	if (!request.sequence)
		request.sequence = ++peer->team_sequence;
	request.player = (unsigned char)player;
	request.team = (unsigned char)team;
	size = delta_wire_write_team_request(data, peer->session, &request);
	if (!size)
		return 0;
	peer_send(peer, peer->host_ipv4, peer->host_port, data, size);
	return request.sequence;
}

delta_u32 delta_peer_client_team_result(const struct delta_peer *peer, struct delta_wire_team_result *result)
{
	if (result)
		*result = peer->team_result;
	return peer->team_results;
}

/* ---------- reports */

void delta_peer_set_report_host(struct delta_peer *peer, const struct delta_peer_report_host *host)
{
	peer->report_host = host;
}

static void host_report(struct delta_peer *peer, delta_u32 now, int machine_index,
	const struct delta_wire_header *header, const unsigned char *payload)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];
	struct delta_wire_report report;
	struct delta_wire_report_result result;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int flags = 0;

	if (!(client->agreed & offered(peer) & CAPABILITY(_delta_capability_reports)) || !peer->report_host ||
		!delta_wire_read_report(payload, header->length, &report) || report.sequence <= client->report_sequence ||
		!delta_rate_take(&client->report_rate, now, DELTA_PEER_REPORT_RATE, DELTA_PEER_REPORT_BURST))
	{
		peer->dropped++;
		return;
	}
	client->report_sequence = report.sequence;
	memset(&result, 0, sizeof(result));
	result.sequence = report.sequence;
	/* (a report of the reporter's own machine is never taken) */
	if (report.target_machine == machine_index)
		result.result = _delta_report_no_player;
	else
	{
		result.result = (unsigned char)peer->report_host->report(peer->report_host->context, machine_index, &report,
			&flags, result.clip_token);
		if (result.result >= NUMBER_OF_DELTA_REPORT_RESULTS)
			result.result = _delta_report_not_taken;
	}
	result.flags = (unsigned char)(result.result == _delta_report_taken && (report.flags & DELTA_WIRE_REPORT_CLIP) ?
		(flags & DELTA_WIRE_REPORT_SEND_CLIP) : 0);
	if (!(result.flags & DELTA_WIRE_REPORT_SEND_CLIP))
		memset(result.clip_token, 0, sizeof(result.clip_token));
	peer_send(peer, client->ipv4, client->port, data, delta_wire_write_report_result(data, client->session, &result));
}

int delta_peer_client_report_ready(const struct delta_peer *peer)
{
	return client_chatting(peer, _delta_capability_reports);
}

delta_u32 delta_peer_client_report(struct delta_peer *peer, const struct delta_wire_report *report)
{
	struct delta_wire_report sent;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int size;

	if (!delta_peer_client_report_ready(peer) || !report)
		return 0;
	sent = *report;
	sent.sequence = ++peer->report_sequence;
	if (!sent.sequence)
		sent.sequence = ++peer->report_sequence;
	size = delta_wire_write_report(data, peer->session, &sent);
	if (!size)
		return 0;
	peer_send(peer, peer->host_ipv4, peer->host_port, data, size);
	return sent.sequence;
}

delta_u32 delta_peer_client_report_result(const struct delta_peer *peer, struct delta_wire_report_result *result)
{
	if (result)
		*result = peer->report_result;
	return peer->report_results;
}

/* ---------- object sync */

/* (whether a client's session takes object sync) */
static int host_objects_client(const struct delta_peer *peer, const struct delta_peer_host_peer *client)
{
	return peer->mode == _mode_host && client->used &&
		(client->agreed & offered(peer) & CAPABILITY(_delta_capability_object_sync));
}

int delta_peer_objects_clients(const struct delta_peer *peer)
{
	int count = 0;
	int index;

	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		if (host_objects_client(peer, &peer->peers[index]))
			count++;
	}
	return count;
}

int delta_peer_send_unit_vitals(struct delta_peer *peer, int machine_index, const struct delta_wire_unit_vitals *vitals)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int sent = 0;
	int index;

	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		struct delta_peer_host_peer *client = &peer->peers[index];
		int size;

		if ((machine_index >= 0 && index != machine_index) || !host_objects_client(peer, client))
			continue;
		size = delta_wire_write_unit_vitals(data, client->session, vitals);
		if (!size)
			return sent;
		peer_send(peer, client->ipv4, client->port, data, size);
		sent++;
	}
	return sent;
}

int delta_peer_send_object_names(struct delta_peer *peer, int machine_index, const struct delta_wire_object_names *names)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int sent = 0;
	int index;

	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		struct delta_peer_host_peer *client = &peer->peers[index];
		int size;

		if ((machine_index >= 0 && index != machine_index) || !host_objects_client(peer, client))
			continue;
		size = delta_wire_write_object_names(data, client->session, names);
		if (!size)
			return sent;
		peer_send(peer, client->ipv4, client->port, data, size);
		sent++;
	}
	return sent;
}

int delta_peer_client_objects_ready(const struct delta_peer *peer)
{
	return client_chatting(peer, _delta_capability_object_sync);
}

void delta_peer_set_objects_client(struct delta_peer *peer, const struct delta_peer_objects_client *client)
{
	peer->objects_client = client;
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
		host_send_challenge(peer, machine_index);
		host_send_identity_challenge(peer, machine_index, now);
		return;
	}
	if (client->used && !elapsed(now, client->session_time, DELTA_PEER_NEW_SESSION_GAP))
	{
		peer->dropped++;
		return;
	}
	/* (a new session: the old one's cards withdrawn, never inherited) */
	host_withdraw_machine(peer, machine_index, now);
	host_forget_moderation(peer, machine_index);
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
	host_start_moderation(peer, machine_index);
	host_start_identity(peer, machine_index);
	host_send_identity_challenge(peer, machine_index, now);
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
	/* (a voice frame takes from its own machine's voice budget, once its
	session names the machine: two players behind one address each talk at
	the full rate. 50 frames a second would take a machine's messages'
	budget, so voice never touches it) */
	if (!valid_machine(first) ||
		(!(size > 3 && data[3] == _delta_message_voice_up) &&
			!delta_rate_take(&peer->rates[first], now, DELTA_PEER_HOST_RATE, DELTA_PEER_HOST_BURST)))
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
	case _delta_message_mod_proof:
	case _delta_message_mod_action:
	case _delta_message_mod_bind_answer:
	{
		int machine_index = host_find_session(peer, ipv4, header.session);

		if (machine_index < 0)
		{
			peer->dropped++;
			break;
		}
		host_moderation_receive(peer, now, machine_index, &header, payload);
		break;
	}
	case _delta_message_chat_say:
	{
		int machine_index = host_find_session(peer, ipv4, header.session);

		if (machine_index < 0)
		{
			peer->dropped++;
			break;
		}
		host_chat_say(peer, now, machine_index, &header, payload);
		break;
	}
	case _delta_message_party_tag:
	{
		int machine_index = host_find_session(peer, ipv4, header.session);
		struct delta_peer_host_peer *client;
		unsigned char tag[DELTA_WIRE_PARTY_TAG_SIZE];
		static const unsigned char none[DELTA_WIRE_PARTY_TAG_SIZE];

		if (machine_index < 0 ||
			!(peer->peers[machine_index].agreed & offered(peer) & CAPABILITY(_delta_capability_party_teams)) ||
			!delta_wire_read_party_tag(payload, header.length, tag))
		{
			peer->dropped++;
			break;
		}
		client = &peer->peers[machine_index];
		if (client->has_party_tag != (memcmp(tag, none, sizeof(tag)) != 0) ||
			memcmp(client->party_tag, tag, sizeof(tag)))
		{
			/* (a change soon after the last one: dropped, the client sends it
			again every DELTA_PEER_PARTY_TAG_INTERVAL) */
			if (client->party_tag_changes &&
				(delta_u32)(now - client->party_tag_time) < (delta_u32)DELTA_PEER_PARTY_TAG_HOLD)
			{
				break;
			}
			client->party_tag_changes++;
			client->party_tag_time = now;
			client->has_party_tag = memcmp(tag, none, sizeof(tag)) != 0;
			memcpy(client->party_tag, tag, sizeof(tag));
			say(peer, "Delta Peer: machine %d is %s", machine_index, client->has_party_tag ? "in a party" :
				"in no party");
		}
		break;
	}
	case _delta_message_team_request:
	{
		int machine_index = host_find_session(peer, ipv4, header.session);

		if (machine_index < 0)
		{
			peer->dropped++;
			break;
		}
		host_team_request(peer, now, machine_index, &header, payload);
		break;
	}
	case _delta_message_report:
	{
		int machine_index = host_find_session(peer, ipv4, header.session);

		if (machine_index < 0)
		{
			peer->dropped++;
			break;
		}
		host_report(peer, now, machine_index, &header, payload);
		break;
	}
	case _delta_message_voice_up:
	{
		int machine_index = host_find_session(peer, ipv4, header.session);

		/* (a frame of no session takes from the address's messages, as
		anything else unknown does) */
		if (machine_index < 0)
		{
			delta_rate_take(&peer->rates[first], now, DELTA_PEER_HOST_RATE, DELTA_PEER_HOST_BURST);
			peer->dropped++;
			break;
		}
		if (!delta_rate_take(&peer->voice_rates[machine_index], now, DELTA_PEER_VOICE_UP_RATE,
			DELTA_PEER_VOICE_UP_BURST))
		{
			peer->dropped++;
			break;
		}
		host_voice_up(peer, now, machine_index, &header, payload);
		break;
	}
	case _delta_message_id_proof:
	{
		int machine_index = host_find_session(peer, ipv4, header.session);

		if (machine_index < 0)
		{
			peer->dropped++;
			break;
		}
		host_identity_proof(peer, now, machine_index, &header, payload);
		break;
	}
	case _delta_message_badge_ask:
	{
		int machine_index = host_find_session(peer, ipv4, header.session);

		if (machine_index < 0)
		{
			peer->dropped++;
			break;
		}
		host_badge_ask(peer, now, machine_index, &header, payload);
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

/* a machine's roster entry, for a client that agreed to recipient_agreed */
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

/* the game's map to each client that agreed to ce_maps: at once when it
changed, then every DELTA_PEER_MAP_INTERVAL */
static void host_send_maps(struct delta_peer *peer, delta_u32 now)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int index;

	if (!peer->map_generation || !(offered(peer) & CAPABILITY(_delta_capability_ce_maps)))
		return;
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		struct delta_peer_host_peer *client = &peer->peers[index];
		int size;

		if (!client->used || !(client->agreed & CAPABILITY(_delta_capability_ce_maps)))
			continue;
		if (client->map_sent_generation == peer->map_generation &&
			!elapsed(now, client->map_time, DELTA_PEER_MAP_INTERVAL))
		{
			continue;
		}
		size = delta_wire_write_map(data, client->session, &peer->map);
		if (!size)
			return;
		peer_send(peer, client->ipv4, client->port, data, size);
		client->map_sent_generation = peer->map_generation;
		client->map_time = now;
	}
}

void delta_peer_set_map(struct delta_peer *peer, const struct delta_wire_map *map)
{
	int index;

	if (!map)
	{
		memset(&peer->map, 0, sizeof(peer->map));
		peer->map_generation = 0;
		return;
	}
	if (peer->map_generation && !memcmp(&peer->map, map, sizeof(peer->map)))
		return;
	peer->map = *map;
	/* (never 0, which is none) */
	peer->map_generation = peer->map_generation + 1 ? peer->map_generation + 1 : 1;
	/* (each client is sent it at once: a generation it had before a none
	may be this one's number) */
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
		peer->peers[index].map_sent_generation = 0;
}

delta_u32 delta_peer_host_map(const struct delta_peer *peer, struct delta_wire_map *map)
{
	if (map)
		*map = peer->host_map;
	return peer->host_map_generation;
}

/* the game's player traits to each client that agreed to bomb: at once when
they changed, then every DELTA_PEER_TRAITS_INTERVAL */
static void host_send_traits(struct delta_peer *peer, delta_u32 now)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int index;

	if (!peer->traits_generation || (offered(peer) & BOMB_CAPABILITIES) != BOMB_CAPABILITIES)
		return;
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		struct delta_peer_host_peer *client = &peer->peers[index];
		int size;

		if (!client->used || (client->agreed & BOMB_CAPABILITIES) != BOMB_CAPABILITIES)
			continue;
		if (client->traits_sent_generation == peer->traits_generation &&
			!elapsed(now, client->traits_time, DELTA_PEER_TRAITS_INTERVAL))
		{
			continue;
		}
		size = delta_wire_write_traits(data, client->session, &peer->traits);
		if (!size)
			return;
		peer_send(peer, client->ipv4, client->port, data, size);
		client->traits_sent_generation = peer->traits_generation;
		client->traits_time = now;
	}
}

void delta_peer_set_traits(struct delta_peer *peer, const struct delta_wire_traits *traits)
{
	int index;

	if (!traits || !delta_wire_traits_valid(traits))
	{
		memset(&peer->traits, 0, sizeof(peer->traits));
		peer->traits_generation = 0;
		return;
	}
	if (peer->traits_generation && !memcmp(&peer->traits, traits, sizeof(peer->traits)))
		return;
	peer->traits = *traits;
	/* (never 0, which is none; each client is sent them at once) */
	peer->traits_generation = peer->traits_generation + 1 ? peer->traits_generation + 1 : 1;
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
		peer->peers[index].traits_sent_generation = 0;
}

delta_u32 delta_peer_host_traits(const struct delta_peer *peer, struct delta_wire_traits *traits)
{
	if (traits)
		*traits = peer->host_traits;
	return peer->host_traits_generation;
}

/* the host's rules to each client that agreed to host_rules: at once when
they changed (or the client is new), then every DELTA_PEER_HOST_RULES_INTERVAL */
static void host_send_rules(struct delta_peer *peer, delta_u32 now)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int index;

	if (!peer->rules_generation || !(offered(peer) & CAPABILITY(_delta_capability_host_rules)))
		return;
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		struct delta_peer_host_peer *client = &peer->peers[index];
		int size;

		if (!client->used || !(client->agreed & CAPABILITY(_delta_capability_host_rules)))
			continue;
		if (client->rules_sent_generation == peer->rules_generation &&
			!elapsed(now, client->rules_time, DELTA_PEER_HOST_RULES_INTERVAL))
		{
			continue;
		}
		size = delta_wire_write_host_rules(data, client->session, &peer->rules);
		if (!size)
			return;
		peer_send(peer, client->ipv4, client->port, data, size);
		client->rules_sent_generation = peer->rules_generation;
		client->rules_time = now;
	}
}

/* the host's number to each client that agreed to mcc_maps: at once when it
changed (or the client is new), then every DELTA_PEER_HOST_RULES_INTERVAL */
static void host_send_number(struct delta_peer *peer, delta_u32 now)
{
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int index;

	delta_u32 interval = DELTA_PEER_HOST_RULES_INTERVAL;

	if (!peer->number_generation || !(offered(peer) & CAPABILITY(_delta_capability_mcc_maps)))
		return;
	if (peer->number_burst_generation != peer->number_generation)
	{
		peer->number_burst_generation = peer->number_generation;
		peer->number_burst_time = now;
	}
	if (!elapsed(now, peer->number_burst_time, DELTA_PEER_HOST_NUMBER_BURST))
		interval = DELTA_PEER_HOST_NUMBER_BURST_INTERVAL;
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		struct delta_peer_host_peer *client = &peer->peers[index];
		int size;

		if (!client->used || !(client->agreed & CAPABILITY(_delta_capability_mcc_maps)))
			continue;
		if (client->number_sent_generation == peer->number_generation &&
			!elapsed(now, client->number_time, interval))
		{
			continue;
		}
		size = delta_wire_write_host_number(data, client->session, peer->number);
		if (!size)
			return;
		peer_send(peer, client->ipv4, client->port, data, size);
		client->number_sent_generation = peer->number_generation;
		client->number_time = now;
	}
}

void delta_peer_set_host_number(struct delta_peer *peer, unsigned number)
{
	int index;

	if (!number)
	{
		peer->number = 0;
		peer->number_generation = 0;
		return;
	}
	if (peer->number_generation && peer->number == number)
		return;
	peer->number = number;
	peer->number_generation = peer->number_generation + 1 ? peer->number_generation + 1 : 1;
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
		peer->peers[index].number_sent_generation = 0;
}

delta_u32 delta_peer_host_number(const struct delta_peer *peer, unsigned *number)
{
	if (number)
		*number = peer->host_number;
	return peer->host_number_generation;
}

void delta_peer_set_host_rules(struct delta_peer *peer, const struct delta_wire_host_rules *rules)
{
	int index;

	if (!rules)
	{
		memset(&peer->rules, 0, sizeof(peer->rules));
		peer->rules_generation = 0;
		return;
	}
	if (peer->rules_generation && !memcmp(&peer->rules, rules, sizeof(peer->rules)))
		return;
	peer->rules = *rules;
	/* (never 0, which is none; each client is sent them at once) */
	peer->rules_generation = peer->rules_generation + 1 ? peer->rules_generation + 1 : 1;
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
		peer->peers[index].rules_sent_generation = 0;
}

delta_u32 delta_peer_host_rules(const struct delta_peer *peer, struct delta_wire_host_rules *rules)
{
	if (rules)
		*rules = peer->host_rules;
	return peer->host_rules_generation;
}

/* (the game machine at the index, or NULL: the host's view) */
static const struct delta_peer_game_machine *host_game_machine(const struct delta_peer *peer, int machine_index)
{
	int index;

	for (index = 0; index < peer->game_machine_count; index++)
	{
		if (peer->game_machines[index].machine_index == machine_index)
			return &peer->game_machines[index];
	}
	return 0;
}

int delta_peer_machine_pending(const struct delta_peer *peer, delta_u32 now, int machine_index)
{
	const struct delta_peer_game_machine *machine;

	if (peer->mode != _mode_host || !valid_machine(machine_index))
		return 0;
	machine = host_game_machine(peer, machine_index);
	if (!machine || machine->local || peer->peers[machine_index].used || !peer->machine_seen[machine_index])
		return 0;
	return !elapsed(now, peer->machine_seen_time[machine_index], DELTA_PEER_HANDSHAKE_TIME);
}

int delta_peer_machine_legacy_version(const struct delta_peer *peer, int machine_index)
{
	if (peer->mode != _mode_host || !valid_machine(machine_index) || !peer->peers[machine_index].used)
		return 0;
	return peer->peers[machine_index].hello.legacy_version;
}

int delta_peer_machine_lacks(const struct delta_peer *peer, delta_u32 now, int machine_index, int capability)
{
	const struct delta_peer_game_machine *machine;
	const struct delta_peer_host_peer *client;

	if (peer->mode != _mode_host || !valid_machine(machine_index) || capability < 0 ||
		capability >= NUMBER_OF_DELTA_CAPABILITIES)
	{
		return 0;
	}
	machine = host_game_machine(peer, machine_index);
	if (!machine || machine->local)
		return 0;
	client = &peer->peers[machine_index];
	if (client->used)
		return !(client->agreed & offered(peer) & CAPABILITY(capability));
	if (!peer->machine_seen[machine_index])
		return 0;
	return elapsed(now, peer->machine_seen_time[machine_index], DELTA_PEER_HANDSHAKE_TIME);
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

	/* (when each machine was first seen in the game at its address: its
	handshake's window) */
	{
		unsigned char present[DELTA_PEER_MAXIMUM_MACHINES];

		memset(present, 0, sizeof(present));
		for (index = 0; index < peer->game_machine_count; index++)
		{
			int machine_index = peer->game_machines[index].machine_index;

			present[machine_index] = 1;
			if (!peer->machine_seen[machine_index] ||
				peer->machine_seen_ipv4[machine_index] != peer->game_machines[index].ipv4)
			{
				peer->machine_seen[machine_index] = 1;
				peer->machine_seen_ipv4[machine_index] = peer->game_machines[index].ipv4;
				peer->machine_seen_time[machine_index] = now;
			}
		}
		for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
		{
			if (!present[index])
				peer->machine_seen[index] = 0;
		}
	}

	/* (a machine gone from the game, or another now in its place, is
	forgotten) */
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		if (peer->peers[index].used && host_find_machine(peer, peer->peers[index].ipv4, index) != index)
			forget_peer(peer, index, "left the game");
	}
	host_moderation_frame(peer, now);
	host_chat_frame(peer, now);
	host_voice_frame(peer, now);

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
	host_send_maps(peer, now);
	host_send_traits(peer, now);
	host_send_rules(peer, now);
	host_send_number(peer, now);
	host_identity_frame(peer, now);

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

/* (client) the party tag to the host, if both agreed to party_teams */
static void client_send_party_tag(struct delta_peer *peer, delta_u32 now)
{
	unsigned char data[DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_PARTY_TAG_SIZE];

	if (!(peer->agreed & offered(peer) & CAPABILITY(_delta_capability_party_teams)))
		return;
	peer_send(peer, peer->host_ipv4, peer->host_port, data, delta_wire_write_party_tag(data, peer->session,
		peer->party_tag));
	peer->party_tag_time = now;
	peer->party_tag_sent = 1;
}

void delta_peer_client_set_party_tag(struct delta_peer *peer, const unsigned char *tag)
{
	static const unsigned char none[DELTA_WIRE_PARTY_TAG_SIZE];

	if (!tag)
		tag = none;
	if (memcmp(peer->party_tag, tag, DELTA_WIRE_PARTY_TAG_SIZE))
	{
		memcpy(peer->party_tag, tag, DELTA_WIRE_PARTY_TAG_SIZE);
		/* (sent at the next frame) */
		peer->party_tag_sent = 0;
	}
}

int delta_peer_machine_party_tag(const struct delta_peer *peer, int machine_index, unsigned char *tag)
{
	const struct delta_peer_host_peer *client;

	if (peer->mode != _mode_host || !valid_machine(machine_index))
		return 0;
	client = &peer->peers[machine_index];
	if (!client->used || !client->has_party_tag ||
		!(client->agreed & offered(peer) & CAPABILITY(_delta_capability_party_teams)))
	{
		return 0;
	}
	memcpy(tag, client->party_tag, DELTA_WIRE_PARTY_TAG_SIZE);
	return 1;
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
	memset(&peer->client_moderation, 0, sizeof(peer->client_moderation));
	memset(&peer->client_chat, 0, sizeof(peer->client_chat));
	memset(&peer->client_badge, 0, sizeof(peer->client_badge));
	peer->agreed = 0;
	peer->room_capabilities = 0;
	peer->room_players = DELTA_PEER_NO_LIMIT;
	memset(&peer->host_relay, 0, sizeof(peer->host_relay));
	peer->table_in.active = 0;
	memset(&peer->host_map, 0, sizeof(peer->host_map));
	peer->host_map_generation = 0;
	memset(&peer->host_traits, 0, sizeof(peer->host_traits));
	peer->host_traits_generation = 0;
	memset(&peer->host_rules, 0, sizeof(peer->host_rules));
	peer->host_rules_generation = 0;
	peer->host_number = 0;
	peer->host_number_generation = 0;
	memset(peer->machines, 0, sizeof(peer->machines));
	if (valid_machine(own))
		peer->machines[own] = local;
	memset(peer->cards, 0, sizeof(peer->cards));
	peer->identity_challenged = 0;
	memset(&peer->identity_challenge, 0, sizeof(peer->identity_challenge));
	memset(peer->identity_sent, 0, sizeof(peer->identity_sent));
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
		/* (the party tag: as it changes, and again now and then while in a
		party) */
		{
			static const unsigned char none[DELTA_WIRE_PARTY_TAG_SIZE];

			if (!peer->party_tag_sent || (memcmp(peer->party_tag, none, sizeof(none)) &&
				elapsed(now, peer->party_tag_time, DELTA_PEER_PARTY_TAG_INTERVAL)))
			{
				client_send_party_tag(peer, now);
			}
		}
		client_voice_frame(peer, now);
		/* (a bind the player did not answer in time) */
		if (peer->client_moderation.bind_waiting &&
			elapsed(now, peer->client_moderation.bind_time, DELTA_PEER_MODERATION_BIND_TIME))
		{
			peer->client_moderation.bind_waiting = 0;
		}
		client_identity_frame(peer, now);
		client_badge_frame(peer, now);
		break;
	}
}

/* ---------- moderation: the client's */

static int client_moderating(const struct delta_peer *peer)
{
	return peer->mode == _mode_client && peer->client_state == _delta_peer_client_delta &&
		(peer->agreed & offered(peer) & CAPABILITY(_delta_capability_moderation));
}

static void client_moderation_receive(struct delta_peer *peer, delta_u32 now, const struct delta_wire_header *header,
	const unsigned char *payload)
{
	struct delta_peer_client_moderation *moderation = &peer->client_moderation;

	if (!client_moderating(peer))
	{
		peer->dropped++;
		return;
	}
	switch (header->type)
	{
	case _delta_message_mod_challenge:
	{
		struct delta_wire_mod_challenge challenge;

		if (!delta_wire_read_mod_challenge(payload, header->length, &challenge))
		{
			peer->dropped++;
			break;
		}
		/* (a new nonce: whatever was signed with the last is over) */
		if (moderation->challenged && memcmp(moderation->nonce, challenge.nonce, sizeof(challenge.nonce)))
		{
			moderation->signed_in = 0;
			moderation->has_state = 0;
			moderation->sequence = 0;
		}
		moderation->challenged = 1;
		memcpy(moderation->nonce, challenge.nonce, sizeof(moderation->nonce));
		moderation->binding_length = challenge.binding_length;
		memcpy(moderation->binding, challenge.binding, sizeof(moderation->binding));
		break;
	}
	case _delta_message_mod_state:
	{
		struct delta_wire_mod_state state;

		if (!moderation->signed_in || !delta_wire_read_mod_state(payload, header->length, &state))
		{
			peer->dropped++;
			break;
		}
		if (state.role > _delta_moderation_role_owner)
			state.role = _delta_moderation_role_none;
		if (!moderation->has_state || memcmp(&state, &moderation->state, sizeof(state)))
			say(peer, "Delta Peer: this machine's moderator role here is %d", (int)state.role);
		moderation->has_state = 1;
		moderation->state = state;
		break;
	}
	case _delta_message_mod_result:
	{
		struct delta_wire_mod_result result;

		if (!delta_wire_read_mod_result(payload, header->length, &result) || !result.sequence ||
			result.sequence > moderation->sequence)
		{
			peer->dropped++;
			break;
		}
		moderation->result = result;
		moderation->result_count++;
		break;
	}
	case _delta_message_mod_notice:
	{
		struct delta_wire_mod_notice notice;

		if (!delta_wire_read_mod_notice(payload, header->length, &notice))
		{
			peer->dropped++;
			break;
		}
		moderation->notice = notice;
		moderation->notice_count++;
		break;
	}
	case _delta_message_mod_bind:
	{
		struct delta_wire_mod_bind bind;

		if (!moderation->challenged || !delta_wire_read_mod_bind(payload, header->length, &bind))
		{
			peer->dropped++;
			break;
		}
		/* (the same request said again keeps its time) */
		if (moderation->bind_waiting && moderation->bind.request == bind.request)
			break;
		moderation->bind = bind;
		moderation->bind_waiting = 1;
		moderation->bind_time = now;
		break;
	}
	default:
		break;
	}
}

/* whether this machine may sign for the host's challenge: its binding the
host this machine joined (none needed on a LAN) */
static int client_binding_matches(struct delta_peer *peer)
{
	struct delta_peer_client_moderation *moderation = &peer->client_moderation;
	char expected[DELTA_WIRE_MODERATION_BINDING_SIZE + 1];
	int result;

	if (!peer->env.moderation_binding)
		return 1;
	memset(expected, 0, sizeof(expected));
	result = peer->env.moderation_binding(peer->env.context, peer->host_ipv4, expected, (int)sizeof(expected));
	if (result == 0)
		return 1;
	if (result > 0 && (int)strlen(expected) == moderation->binding_length &&
		!memcmp(expected, moderation->binding, (size_t)moderation->binding_length))
	{
		return 1;
	}
	say(peer, "Delta Peer: the host's moderation challenge is not for the host this machine joined: nothing signed");
	return 0;
}

int delta_peer_client_moderation_ready(const struct delta_peer *peer)
{
	return client_moderating(peer) && peer->client_moderation.challenged;
}

int delta_peer_client_moderation_sign_in(struct delta_peer *peer)
{
	struct delta_peer_client_moderation *moderation = &peer->client_moderation;
	struct delta_wire_mod_proof proof;
	unsigned char message[DELTA_WIRE_MODERATION_MESSAGE_SIZE];
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int length;

	if (!delta_peer_client_moderation_ready(peer) || !peer->env.moderation_sign || !client_binding_matches(peer))
		return 0;
	length = delta_wire_moderation_proof_message(message, moderation->nonce, moderation->binding,
		moderation->binding_length);
	memset(&proof, 0, sizeof(proof));
	if (!peer->env.moderation_sign(peer->env.context, message, length, proof.key, proof.signature))
		return 0;
	moderation->signed_in = 1;
	peer_send(peer, peer->host_ipv4, peer->host_port, data, delta_wire_write_mod_proof(data, peer->session, &proof));
	return 1;
}

delta_u32 delta_peer_client_moderation_action(struct delta_peer *peer, int action, int target_machine, int minutes,
	const char *reason)
{
	struct delta_peer_client_moderation *moderation = &peer->client_moderation;
	struct delta_wire_mod_action message_action;
	unsigned char message[DELTA_WIRE_MODERATION_MESSAGE_SIZE];
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	unsigned char key[DELTA_WIRE_MODERATION_KEY_SIZE];
	int length;
	int index;

	if (!delta_peer_client_moderation_ready(peer) || !moderation->signed_in || !peer->env.moderation_sign ||
		action <= 0 || action > 255 || minutes < 0 || minutes > 0xFFFF || !client_binding_matches(peer))
	{
		return 0;
	}
	memset(&message_action, 0, sizeof(message_action));
	message_action.sequence = moderation->sequence + 1;
	message_action.action = (unsigned char)action;
	message_action.target = (unsigned char)(valid_machine(target_machine) ? target_machine : DELTA_WIRE_NO_MACHINE);
	message_action.minutes = (unsigned short)minutes;
	/* (printable ASCII only: it is signed as sent) */
	for (index = 0; reason && reason[index] && index < DELTA_WIRE_MODERATION_REASON_SIZE; index++)
	{
		char character = reason[index];

		message_action.reason[index] = character >= 0x20 && character <= 0x7E ? character : '?';
	}
	message_action.reason_length = index;
	length = delta_wire_moderation_action_message(message, moderation->nonce, &message_action);
	if (!peer->env.moderation_sign(peer->env.context, message, length, key, message_action.signature))
		return 0;
	moderation->sequence = message_action.sequence;
	peer_send(peer, peer->host_ipv4, peer->host_port, data,
		delta_wire_write_mod_action(data, peer->session, &message_action));
	return message_action.sequence;
}

int delta_peer_client_moderation_bind_answer(struct delta_peer *peer, int accepted)
{
	struct delta_peer_client_moderation *moderation = &peer->client_moderation;
	struct delta_wire_mod_bind_answer answer;
	unsigned char message[DELTA_WIRE_MODERATION_MESSAGE_SIZE];
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int length;

	if (!delta_peer_client_moderation_ready(peer) || !moderation->bind_waiting || !peer->env.moderation_sign)
		return 0;
	moderation->bind_waiting = 0;
	if (!client_binding_matches(peer))
		return 0;
	memset(&answer, 0, sizeof(answer));
	answer.request = moderation->bind.request;
	answer.accepted = (unsigned char)(accepted ? 1 : 0);
	length = delta_wire_moderation_bind_message(message, moderation->nonce, answer.request, answer.accepted,
		moderation->binding, moderation->binding_length);
	if (!peer->env.moderation_sign(peer->env.context, message, length, answer.key, answer.signature))
		return 0;
	/* (the host takes the key it answered with as signed in) */
	moderation->signed_in = 1;
	peer_send(peer, peer->host_ipv4, peer->host_port, data,
		delta_wire_write_mod_bind_answer(data, peer->session, &answer));
	return 1;
}

static void client_receive(struct delta_peer *peer, delta_u32 now, delta_u32 ipv4, unsigned short port,
	const unsigned char *data, int size)
{
	struct delta_wire_header header;
	const unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	if ((peer->client_state != _delta_peer_client_waiting && peer->client_state != _delta_peer_client_delta) ||
		ipv4 != peer->host_ipv4 || port != peer->host_port ||
		/* (voice frames take from a budget of their own) */
		(size > 3 && data[3] == _delta_message_voice_down ?
			!delta_rate_take(&peer->client_voice_rate, now, DELTA_PEER_VOICE_DOWN_RATE, DELTA_PEER_VOICE_DOWN_BURST) :
			!delta_rate_take(&peer->host_rate, now, DELTA_PEER_CLIENT_RATE, DELTA_PEER_CLIENT_BURST)) ||
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
		client_send_party_tag(peer, now);
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
	case _delta_message_mod_challenge:
	case _delta_message_mod_state:
	case _delta_message_mod_result:
	case _delta_message_mod_notice:
	case _delta_message_mod_bind:
		client_moderation_receive(peer, now, &header, payload);
		break;
	case _delta_message_voice_config:
	case _delta_message_voice_down:
		client_voice_receive(peer, now, &header, payload);
		break;
	case _delta_message_chat_config:
	case _delta_message_chat_line:
	case _delta_message_chat_refused:
		client_chat_receive(peer, now, &header, payload);
		break;
	case _delta_message_id_challenge:
	case _delta_message_id_card:
		client_identity_receive(peer, now, &header, payload);
		break;
	case _delta_message_team_result:
	{
		struct delta_wire_team_result result;

		/* (only an answer to a request of this session's) */
		if (!delta_peer_client_team_ready(peer) || !delta_wire_read_team_result(payload, header.length, &result) ||
			!result.sequence || result.sequence > peer->team_sequence)
		{
			peer->dropped++;
			break;
		}
		peer->team_result = result;
		peer->team_results++;
		break;
	}
	case _delta_message_badge:
		client_badge_receive(peer, &header, payload);
		break;
	case _delta_message_report_result:
	{
		struct delta_wire_report_result result;

		/* (only an answer to a report of this session's) */
		if (!delta_peer_client_report_ready(peer) || !delta_wire_read_report_result(payload, header.length, &result) ||
			!result.sequence || result.sequence > peer->report_sequence)
		{
			peer->dropped++;
			break;
		}
		peer->report_result = result;
		peer->report_results++;
		break;
	}
	case _delta_message_map:
	{
		struct delta_wire_map map;

		if (peer->client_state != _delta_peer_client_delta ||
			!(peer->agreed & offered(peer) & CAPABILITY(_delta_capability_ce_maps)) ||
			!delta_wire_read_map(payload, header.length, &map))
		{
			peer->dropped++;
			break;
		}
		if (!peer->host_map_generation || memcmp(&peer->host_map, &map, sizeof(map)))
		{
			peer->host_map = map;
			peer->host_map_generation = peer->host_map_generation + 1 ? peer->host_map_generation + 1 : 1;
		}
		break;
	}
	case _delta_message_unit_vitals:
	case _delta_message_object_names:
	{
		static struct delta_wire_unit_vitals vitals;
		static struct delta_wire_object_names names;

		if (peer->client_state != _delta_peer_client_delta || !peer->objects_client ||
			!(peer->agreed & offered(peer) & CAPABILITY(_delta_capability_object_sync)))
		{
			peer->dropped++;
			break;
		}
		if (header.type == _delta_message_unit_vitals)
		{
			if (!delta_wire_read_unit_vitals(payload, header.length, &vitals))
				peer->dropped++;
			else if (peer->objects_client->vitals)
				peer->objects_client->vitals(peer->objects_client->context, &vitals);
		}
		else
		{
			if (!delta_wire_read_object_names(payload, header.length, &names))
				peer->dropped++;
			else if (peer->objects_client->names)
				peer->objects_client->names(peer->objects_client->context, &names);
		}
		break;
	}
	case _delta_message_traits:
	{
		struct delta_wire_traits traits;

		if (peer->client_state != _delta_peer_client_delta ||
			(peer->agreed & offered(peer) & BOMB_CAPABILITIES) != BOMB_CAPABILITIES ||
			!delta_wire_read_traits(payload, header.length, &traits))
		{
			peer->dropped++;
			break;
		}
		if (!peer->host_traits_generation || memcmp(&peer->host_traits, &traits, sizeof(traits)))
		{
			peer->host_traits = traits;
			peer->host_traits_generation =
				peer->host_traits_generation + 1 ? peer->host_traits_generation + 1 : 1;
			say(peer, "Delta Peer: the host's player traits: %s", traits.kind == DELTA_WIRE_TRAITS_BOMB ? "Bomb's" :
				"none");
		}
		break;
	}
	case _delta_message_host_number:
	{
		unsigned number;

		if (peer->client_state != _delta_peer_client_delta ||
			!(peer->agreed & offered(peer) & CAPABILITY(_delta_capability_mcc_maps)) ||
			!delta_wire_read_host_number(payload, header.length, &number))
		{
			peer->dropped++;
			break;
		}
		if (!peer->host_number_generation || peer->host_number != number)
		{
			peer->host_number = number;
			peer->host_number_generation =
				peer->host_number_generation + 1 ? peer->host_number_generation + 1 : 1;
			say(peer, "Delta Peer: the host's game is played at OpenCE network version %u", number);
		}
		break;
	}
	case _delta_message_host_rules:
	{
		struct delta_wire_host_rules rules;

		if (peer->client_state != _delta_peer_client_delta ||
			!(peer->agreed & offered(peer) & CAPABILITY(_delta_capability_host_rules)) ||
			!delta_wire_read_host_rules(payload, header.length, &rules))
		{
			peer->dropped++;
			break;
		}
		if (!peer->host_rules_generation || memcmp(&peer->host_rules, &rules, sizeof(rules)))
		{
			peer->host_rules = rules;
			peer->host_rules_generation =
				peer->host_rules_generation + 1 ? peer->host_rules_generation + 1 : 1;
			say(peer, "Delta Peer: the host's rules: names above heads %s, follow camera %s, team changes %s",
				rules.flags & DELTA_WIRE_HOST_PLAYER_NAMES ? "allowed" : "off",
				rules.flags & DELTA_WIRE_HOST_FOLLOW_CAMERA ? "allowed" : "off",
				rules.team_change == DELTA_WIRE_HOST_TEAM_CHANGE_ALLOW ? "allowed" :
				rules.team_change == DELTA_WIRE_HOST_TEAM_CHANGE_BALANCED ? "balanced" : "off");
		}
		break;
	}
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

/* ---------- verified names (docs/delta.md, Verified names) */

static int host_identity_agreed(const struct delta_peer *peer, int machine_index)
{
	return valid_machine(machine_index) && peer->peers[machine_index].used &&
		(peer->peers[machine_index].agreed & offered(peer) & CAPABILITY(_delta_capability_identity));
}

static int client_identity_agreed(const struct delta_peer *peer)
{
	return peer->mode == _mode_client && peer->client_state == _delta_peer_client_delta &&
		(peer->agreed & offered(peer) & CAPABILITY(_delta_capability_identity));
}

static struct delta_peer_card *find_card(struct delta_peer *peer, int machine_index, int local_player)
{
	int index;

	for (index = 0; index < DELTA_PEER_IDENTITY_CARDS; index++)
	{
		struct delta_peer_card *card = &peer->cards[index];

		if (card->used && card->machine_index == machine_index && card->local_player == local_player)
			return card;
	}
	return NULL;
}

/* a card's slot: its own, else a free one, else a withdrawn one (NULL:
none, every slot a verified player's) */
static struct delta_peer_card *card_slot(struct delta_peer *peer, int machine_index, int local_player)
{
	struct delta_peer_card *card = find_card(peer, machine_index, local_player);
	int index;

	for (index = 0; index < DELTA_PEER_IDENTITY_CARDS && !card; index++)
	{
		if (!peer->cards[index].used)
			card = &peer->cards[index];
	}
	for (index = 0; index < DELTA_PEER_IDENTITY_CARDS && !card; index++)
	{
		if (!peer->cards[index].proof.certificate_length)
			card = &peer->cards[index];
	}
	return card;
}

/* another slot's (machine and local player) verified card of the same
device or account: one identity is one player at a time (NULL: none) */
static struct delta_peer_card *identity_elsewhere(struct delta_peer *peer, int machine_index, int local_player,
	const struct delta_peer_identity *identity)
{
	int index;

	for (index = 0; index < DELTA_PEER_IDENTITY_CARDS; index++)
	{
		struct delta_peer_card *card = &peer->cards[index];

		if (!card->used || !card->proof.certificate_length ||
			(card->machine_index == machine_index && card->local_player == local_player))
		{
			continue;
		}
		if (!memcmp(card->identity.device_id, identity->device_id, sizeof(identity->device_id)) ||
			!memcmp(card->identity.account, identity->account, sizeof(identity->account)))
		{
			return card;
		}
	}
	return NULL;
}

static delta_u32 next_generation(struct delta_peer *peer)
{
	peer->identity_generation = peer->identity_generation + 1 ? peer->identity_generation + 1 : 1;
	return peer->identity_generation;
}

/* (the host) a machine's moderation told of a card (NULL: withdrawn), and
the roles said again. Only a bound proof reaches it: one that is not is
the name alone, never a role or a ban */
static void host_identity_changed(struct delta_peer *peer, int machine_index, int local_player,
	const struct delta_peer_identity *identity)
{
	if (peer->mode != _mode_host || !peer->moderation_host)
		return;
	if (peer->moderation_host->machine_identity)
	{
		peer->moderation_host->machine_identity(peer->moderation_host->context, machine_index, local_player,
			identity && identity->bound ? identity : NULL);
	}
	peer->moderation_roles_dirty = 1;
}

/* (the host) a verified card stored: 1 if it is new or changed */
static void host_withdraw_card(struct delta_peer *peer, struct delta_peer_card *card, delta_u32 now);

static int host_store_card(struct delta_peer *peer, delta_u32 now, int machine_index, const unsigned char *nonce,
	const struct delta_wire_id_proof *proof, const struct delta_peer_identity *identity)
{
	struct delta_peer_card *card, *other = identity_elsewhere(peer, machine_index, proof->local_player, identity);

	/* (one identity, one player: a bound proof takes the place of an
	unbound card of it, which anyone may have relayed; else the first
	stays) */
	if (other && identity->bound && !other->identity.bound)
	{
		say(peer, "Delta Peer: machine %d's player %d proved bound an identity shown for another player: that one "
			"withdrawn", machine_index, proof->local_player);
		host_withdraw_card(peer, other, now);
	}
	else if (other)
	{
		say(peer, "Delta Peer: machine %d's player %d proved an identity another player of the game has: refused",
			machine_index, proof->local_player);
		return 0;
	}
	card = card_slot(peer, machine_index, proof->local_player);
	if (!card)
		return 0;
	if (card->used && card->machine_index == machine_index && card->local_player == proof->local_player &&
		!memcmp(&card->proof, proof, sizeof(*proof)) && !memcmp(card->nonce, nonce, sizeof(card->nonce)))
	{
		return 0;
	}
	if (card->used && card->proof.certificate_length &&
		(card->machine_index != machine_index || card->local_player != proof->local_player))
	{
		/* (never: a verified card is taken only when every slot is) */
		return 0;
	}
	memset(card, 0, sizeof(*card));
	card->used = 1;
	card->machine_index = (unsigned char)machine_index;
	card->local_player = (unsigned char)proof->local_player;
	memcpy(card->nonce, nonce, sizeof(card->nonce));
	card->proof = *proof;
	card->identity = *identity;
	card->generation = next_generation(peer);
	card->checked_time = now;
	say(peer, "Delta Peer: machine %d's player %d is verified%s%s%s", machine_index, proof->local_player,
		identity->handle[0] ? " as @" : " (no profile)", identity->handle, identity->bound ? "" : " (shown only)");
	host_identity_changed(peer, machine_index, proof->local_player, identity);
	return 1;
}

/* (the host) a card withdrawn: every client is told, then it is let go */
static void host_withdraw_card(struct delta_peer *peer, struct delta_peer_card *card, delta_u32 now)
{
	if (!card->used || !card->proof.certificate_length)
		return;
	say(peer, "Delta Peer: machine %d's player %d is no longer verified", card->machine_index, card->local_player);
	memset(&card->proof, 0, sizeof(card->proof));
	card->proof.local_player = card->local_player;
	memset(&card->identity, 0, sizeof(card->identity));
	memset(card->nonce, 0, sizeof(card->nonce));
	card->generation = next_generation(peer);
	card->withdrawn_time = now;
	host_identity_changed(peer, card->machine_index, card->local_player, NULL);
}

static void host_withdraw_machine(struct delta_peer *peer, int machine_index, delta_u32 now)
{
	int index;

	if (peer->mode != _mode_host)
		return;
	for (index = 0; index < DELTA_PEER_IDENTITY_CARDS; index++)
	{
		if (peer->cards[index].used && peer->cards[index].machine_index == machine_index)
			host_withdraw_card(peer, &peer->cards[index], now);
	}
}

/* (the host, leaving: forget_all) every card's moderation told it is gone,
so nothing outlives the session */
static void host_withdraw_all(struct delta_peer *peer)
{
	int index;

	for (index = 0; index < DELTA_PEER_IDENTITY_CARDS; index++)
	{
		struct delta_peer_card *card = &peer->cards[index];

		if (card->used && card->proof.certificate_length)
			host_identity_changed(peer, card->machine_index, card->local_player, NULL);
	}
}

static void make_nonce(struct delta_peer *peer, unsigned char *nonce)
{
	int index;

	for (index = 0; index < DELTA_WIRE_IDENTITY_NONCE_SIZE; index += 4)
	{
		delta_u32 value = peer->env.random_number(peer->env.context);

		nonce[index] = (unsigned char)value;
		nonce[index + 1] = (unsigned char)(value >> 8);
		nonce[index + 2] = (unsigned char)(value >> 16);
		nonce[index + 3] = (unsigned char)(value >> 24);
	}
}

/* (the host's own players') */
static void make_identity_nonce(struct delta_peer *peer)
{
	if (peer->identity_nonce_made || !peer->env.random_number)
		return;
	make_nonce(peer, peer->identity_nonce);
	peer->identity_nonce_made = 1;
}

/* (the host) a new session's nonce: one of its own, never another's or an
earlier session's (host_hello calls it for every new session) */
static void host_start_identity(struct delta_peer *peer, int machine_index)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];

	client->identity_challenged = 0;
	if (!host_identity_agreed(peer, machine_index) || !peer->env.random_number)
		return;
	make_nonce(peer, client->identity_nonce);
	client->identity_challenged = 1;
}

static void host_send_identity_challenge(struct delta_peer *peer, int machine_index, delta_u32 now)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];
	struct delta_wire_id_challenge challenge;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];

	if (!host_identity_agreed(peer, machine_index) || !client->identity_challenged)
		return;
	memset(&challenge, 0, sizeof(challenge));
	memcpy(challenge.nonce, client->identity_nonce, sizeof(challenge.nonce));
	challenge.binding_length = peer->moderation_binding_length;
	memcpy(challenge.binding, peer->moderation_binding, (size_t)peer->moderation_binding_length);
	client->identity_challenge_time = now;
	peer_send(peer, client->ipv4, client->port, data, delta_wire_write_id_challenge(data, client->session, &challenge));
}

/* (the host) the flags it takes from a proof: bound only while it has a
binding of its own (on a LAN it has none, and nothing is bound) */
static int host_proof_bound(const struct delta_peer *peer, const struct delta_wire_id_proof *proof)
{
	return (proof->flags & DELTA_WIRE_IDENTITY_BOUND) && peer->moderation_binding_length > 0;
}

static void host_identity_proof(struct delta_peer *peer, delta_u32 now, int machine_index,
	const struct delta_wire_header *header, const unsigned char *payload)
{
	struct delta_peer_host_peer *client = &peer->peers[machine_index];
	struct delta_wire_id_proof proof;
	struct delta_peer_identity identity;

	if (!host_identity_agreed(peer, machine_index) || !client->identity_challenged ||
		!delta_wire_read_id_proof(payload, header->length, &proof) ||
		!delta_rate_take(&client->identity_rate, now, DELTA_PEER_IDENTITY_PROOF_RATE, DELTA_PEER_IDENTITY_PROOF_BURST))
	{
		peer->dropped++;
		return;
	}
	if (!proof.certificate_length)
	{
		struct delta_peer_card *card = find_card(peer, machine_index, proof.local_player);

		if (card)
			host_withdraw_card(peer, card, now);
		return;
	}
	memset(&identity, 0, sizeof(identity));
	if (!peer->env.identity_check(peer->env.context, &proof, client->identity_nonce, peer->moderation_binding,
		peer->moderation_binding_length, machine_index, DELTA_PEER_IDENTITY_HOST_AGE, &identity))
	{
		say(peer, "Delta Peer: machine %d's player %d's proof did not check", machine_index, proof.local_player);
		peer->dropped++;
		return;
	}
	identity.bound = host_proof_bound(peer, &proof);
	host_store_card(peer, now, machine_index, client->identity_nonce, &proof, &identity);
}

/* (the host) its binding changed (its invite came or went): every card,
its own players' too, was proved for the old one, so all are withdrawn and
every client challenged again at once */
static void host_identity_binding_changed(struct delta_peer *peer, delta_u32 now)
{
	int index;

	if (peer->mode != _mode_host)
		return;
	for (index = 0; index < DELTA_PEER_IDENTITY_CARDS; index++)
		host_withdraw_card(peer, &peer->cards[index], now);
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
		peer->peers[index].identity_challenge_time = now - DELTA_PEER_IDENTITY_CHALLENGE_INTERVAL;
	peer->identity_own_time = 0;
}

/* (the host, each frame) its own players proved; the challenge said
again; cards of machines gone withdrawn, and withdrawn ones let go once
every client heard; each client sent the cards it lacks */
static void host_identity_frame(struct delta_peer *peer, delta_u32 now)
{
	int index, own = -1;

	if (!(offered(peer) & CAPABILITY(_delta_capability_identity)))
		return;
	make_identity_nonce(peer);
	if (!peer->identity_nonce_made)
		return;
	/* (a binding not the one the cards were proved with: all again) */
	if (peer->identity_own_binding_length != peer->moderation_binding_length ||
		memcmp(peer->identity_own_binding, peer->moderation_binding, (size_t)peer->moderation_binding_length))
	{
		peer->identity_own_binding_length = peer->moderation_binding_length;
		memcpy(peer->identity_own_binding, peer->moderation_binding, sizeof(peer->identity_own_binding));
		host_identity_binding_changed(peer, now);
	}
	for (index = 0; index < peer->game_machine_count; index++)
	{
		if (peer->game_machines[index].local)
			own = peer->game_machines[index].machine_index;
	}
	/* (its own players: each challenge's time, a check of its own; proved
	again before their cards get old) */
	if (valid_machine(own) && peer->env.identity_prove &&
		(!peer->identity_own_time || elapsed(now, peer->identity_own_time, DELTA_PEER_IDENTITY_CHALLENGE_INTERVAL)))
	{
		int local, flags = peer->moderation_binding_length ? DELTA_WIRE_IDENTITY_BOUND : 0;

		peer->identity_own_time = now ? now : 1;
		for (local = 0; local < DELTA_WIRE_IDENTITY_LOCAL_PLAYERS; local++)
		{
			struct delta_peer_card *card = find_card(peer, own, local);
			struct delta_wire_id_proof proof;
			struct delta_peer_identity identity;

			if (!peer->env.identity_prove(peer->env.context, local, peer->identity_nonce, peer->moderation_binding,
				peer->moderation_binding_length, own, flags, NULL))
			{
				if (card)
					host_withdraw_card(peer, card, now);
				continue;
			}
			if (card && card->proof.certificate_length && !elapsed(now, card->withdrawn_time, DELTA_PEER_IDENTITY_RENEW))
				continue;
			memset(&proof, 0, sizeof(proof));
			memset(&identity, 0, sizeof(identity));
			if (peer->env.identity_prove(peer->env.context, local, peer->identity_nonce, peer->moderation_binding,
				peer->moderation_binding_length, own, flags, &proof) &&
				peer->env.identity_check(peer->env.context, &proof, peer->identity_nonce, peer->moderation_binding,
					peer->moderation_binding_length, own, DELTA_PEER_IDENTITY_HOST_AGE, &identity))
			{
				identity.bound = host_proof_bound(peer, &proof);
				if (host_store_card(peer, now, own, peer->identity_nonce, &proof, &identity))
				{
					/* (when it was proved: proved again RENEW later) */
					find_card(peer, own, local)->withdrawn_time = now;
				}
			}
		}
	}
	for (index = 0; index < DELTA_PEER_IDENTITY_CARDS; index++)
	{
		struct delta_peer_card *card = &peer->cards[index];
		int machine;

		if (!card->used)
			continue;
		if (!card->proof.certificate_length)
		{
			if (elapsed(now, card->withdrawn_time, DELTA_PEER_IDENTITY_WITHDRAWN_TIME))
				memset(card, 0, sizeof(*card));
			continue;
		}
		machine = card->machine_index;
		if (machine != own && !host_identity_agreed(peer, machine))
		{
			host_withdraw_card(peer, card, now);
			continue;
		}
		/* (checked again: a proof grown too old, a certificate past its
		date, a device revoked since) */
		if (peer->identity_recheck || elapsed(now, card->checked_time, DELTA_PEER_IDENTITY_RECHECK))
		{
			struct delta_peer_identity again;

			card->checked_time = now;
			memset(&again, 0, sizeof(again));
			if (!peer->env.identity_check(peer->env.context, &card->proof, card->nonce, peer->moderation_binding,
				peer->moderation_binding_length, machine, DELTA_PEER_IDENTITY_KEPT_AGE, &again))
			{
				say(peer, "Delta Peer: machine %d's player %d's card no longer checks", machine, card->local_player);
				host_withdraw_card(peer, card, now);
			}
		}
	}
	peer->identity_recheck = 0;
	for (index = 0; index < DELTA_PEER_MAXIMUM_MACHINES; index++)
	{
		struct delta_peer_host_peer *client = &peer->peers[index];
		unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
		int card_index, sent = 0;

		if (!host_identity_agreed(peer, index))
			continue;
		if (elapsed(now, client->identity_challenge_time, DELTA_PEER_IDENTITY_CHALLENGE_INTERVAL))
			host_send_identity_challenge(peer, index, now);
		if (elapsed(now, client->identity_cards_time, DELTA_PEER_IDENTITY_CARD_INTERVAL))
		{
			client->identity_cards_time = now;
			memset(client->identity_card_sent, 0, sizeof(client->identity_card_sent));
		}
		for (card_index = 0; card_index < DELTA_PEER_IDENTITY_CARDS && sent < DELTA_PEER_IDENTITY_CARDS_A_FRAME;
			card_index++)
		{
			const struct delta_peer_card *card = &peer->cards[card_index];
			struct delta_wire_id_card message;

			if (!card->used || client->identity_card_sent[card_index] == card->generation)
				continue;
			client->identity_card_sent[card_index] = card->generation;
			memset(&message, 0, sizeof(message));
			message.machine_index = card->machine_index;
			memcpy(message.nonce, card->nonce, sizeof(message.nonce));
			message.proof = card->proof;
			peer_send(peer, client->ipv4, client->port, data, delta_wire_write_id_card(data, client->session, &message));
			sent++;
		}
	}
}

/* how the binding a challenge carries stands with the host this machine
connected to: 1 bound (it is the invite this machine joined through the
invite tunnel), 0 not bound (a host reached any other way: a LAN, an
address: the name shows, nothing more), -1 another host's (a relay: it
proves nothing, and shows no one) */
static int client_identity_binding(struct delta_peer *peer)
{
	char expected[DELTA_WIRE_IDENTITY_BINDING_SIZE + 1];
	int result;

	if (!peer->env.moderation_binding)
		return 0;
	memset(expected, 0, sizeof(expected));
	result = peer->env.moderation_binding(peer->env.context, peer->host_ipv4, expected, (int)sizeof(expected));
	if (result == 0)
		return 0;
	if (result > 0 && (int)strlen(expected) == peer->identity_challenge.binding_length &&
		!memcmp(expected, peer->identity_challenge.binding, (size_t)peer->identity_challenge.binding_length))
	{
		return 1;
	}
	return -1;
}

static void client_identity_receive(struct delta_peer *peer, delta_u32 now, const struct delta_wire_header *header,
	const unsigned char *payload)
{
	if (!client_identity_agreed(peer))
	{
		peer->dropped++;
		return;
	}
	if (header->type == _delta_message_id_challenge)
	{
		struct delta_wire_id_challenge challenge;

		if (!delta_wire_read_id_challenge(payload, header->length, &challenge))
		{
			peer->dropped++;
			return;
		}
		/* (another nonce or binding: the cards checked with the last are
		over, and the proofs are made again) */
		if (peer->identity_challenged && memcmp(&challenge, &peer->identity_challenge, sizeof(challenge)))
		{
			memset(peer->cards, 0, sizeof(peer->cards));
			memset(peer->identity_sent, 0, sizeof(peer->identity_sent));
		}
		peer->identity_challenge = challenge;
		peer->identity_challenged = 1;
		if (client_identity_binding(peer) < 0)
		{
			say(peer, "Delta Peer: the host's identity challenge is not for the host this machine joined: nothing "
				"proved, and no one shown as verified");
		}
		return;
	}
	if (header->type == _delta_message_id_card)
	{
		struct delta_wire_id_card card;
		struct delta_peer_card *slot;
		struct delta_peer_identity identity;

		if (!peer->identity_challenged || client_identity_binding(peer) < 0 ||
			!delta_wire_read_id_card(payload, header->length, &card) || !valid_machine(card.machine_index))
		{
			peer->dropped++;
			return;
		}
		slot = find_card(peer, card.machine_index, card.proof.local_player);
		if (!card.proof.certificate_length)
		{
			if (slot)
				memset(slot, 0, sizeof(*slot));
			return;
		}
		/* (the same card again: checked already) */
		if (slot && !memcmp(&slot->proof, &card.proof, sizeof(card.proof)) &&
			!memcmp(slot->nonce, card.nonce, sizeof(card.nonce)))
		{
			return;
		}
		memset(&identity, 0, sizeof(identity));
		if (!peer->env.identity_check(peer->env.context, &card.proof, card.nonce, peer->identity_challenge.binding,
			peer->identity_challenge.binding_length, card.machine_index, DELTA_PEER_IDENTITY_CARD_AGE, &identity))
		{
			/* (a host's relay that doesn't check: not shown, and never the
			name it claims) */
			if (slot)
				memset(slot, 0, sizeof(*slot));
			say(peer, "Delta Peer: the host's card for machine %d's player %d did not check", card.machine_index,
				card.proof.local_player);
			peer->dropped++;
			return;
		}
		/* (bound only when this machine joined through that invite: a card
		replayed from an invite game to a host joined by its address keeps
		its flag, and is shown unbound) */
		identity.bound = (card.proof.flags & DELTA_WIRE_IDENTITY_BOUND) && peer->identity_challenge.binding_length > 0 &&
			client_identity_binding(peer) > 0;
		/* (one identity, one player: a card naming one already shown for
		another player is not taken, but for a bound card where an unbound
		one was) */
		{
			struct delta_peer_card *other = identity_elsewhere(peer, card.machine_index, card.proof.local_player,
				&identity);

			if (other && identity.bound && !other->identity.bound)
				memset(other, 0, sizeof(*other));
			else if (other)
			{
				if (slot)
					memset(slot, 0, sizeof(*slot));
				peer->dropped++;
				return;
			}
		}
		slot = card_slot(peer, card.machine_index, card.proof.local_player);
		if (!slot)
			return;
		memset(slot, 0, sizeof(*slot));
		slot->used = 1;
		slot->machine_index = (unsigned char)card.machine_index;
		slot->local_player = (unsigned char)card.proof.local_player;
		memcpy(slot->nonce, card.nonce, sizeof(slot->nonce));
		slot->proof = card.proof;
		slot->identity = identity;
		slot->generation = next_generation(peer);
		slot->checked_time = now;
		return;
	}
}

/* (the client, each frame) each local player's proof, sent while the host
has not relayed it (again every DELTA_PEER_IDENTITY_PROOF_GAP) and again
every DELTA_PEER_IDENTITY_RENEW, or its withdrawal; cards of machines the
roster dropped forgotten */
static void client_identity_frame(struct delta_peer *peer, delta_u32 now)
{
	const struct delta_wire_id_challenge *challenge = &peer->identity_challenge;
	int index, binding, flags;

	if (!client_identity_agreed(peer))
		return;
	for (index = 0; index < DELTA_PEER_IDENTITY_CARDS; index++)
	{
		struct delta_peer_card *card = &peer->cards[index];
		struct delta_peer_identity again;

		if (card->used && !peer->machines[card->machine_index].known)
		{
			memset(card, 0, sizeof(*card));
			continue;
		}
		/* (checked again: grown too old, its certificate past its date, its
		device revoked since; the host sends the same card again and again,
		but a card only lasts as long as it checks) */
		if (!card->used || !card->proof.certificate_length ||
			(!peer->identity_recheck && !elapsed(now, card->checked_time, DELTA_PEER_IDENTITY_RECHECK)))
		{
			continue;
		}
		card->checked_time = now;
		memset(&again, 0, sizeof(again));
		if (!peer->env.identity_check(peer->env.context, &card->proof, card->nonce, peer->identity_challenge.binding,
			peer->identity_challenge.binding_length, card->machine_index, DELTA_PEER_IDENTITY_CARD_AGE, &again))
		{
			memset(card, 0, sizeof(*card));
		}
	}
	peer->identity_recheck = 0;
	if (!peer->identity_challenged || !peer->env.identity_prove || !valid_machine(peer->machine_index))
		return;
	binding = client_identity_binding(peer);
	if (binding < 0)
		return;
	flags = binding > 0 ? DELTA_WIRE_IDENTITY_BOUND : 0;
	for (index = 0; index < DELTA_WIRE_IDENTITY_LOCAL_PLAYERS; index++)
	{
		struct delta_peer_card *card = find_card(peer, peer->machine_index, index);
		struct delta_wire_id_proof proof;
		unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
		int would = peer->env.identity_prove(peer->env.context, index, challenge->nonce, challenge->binding,
			challenge->binding_length, peer->machine_index, flags, NULL);

		if (!would)
		{
			/* (proved before, or still shown: withdrawn, once) */
			if ((peer->identity_sent[index] == 1 || card) && peer->identity_sent[index] != 2)
			{
				memset(&proof, 0, sizeof(proof));
				proof.local_player = index;
				peer_send(peer, peer->host_ipv4, peer->host_port, data,
					delta_wire_write_id_proof(data, peer->session, &proof));
				peer->identity_sent[index] = 2;
			}
			continue;
		}
		if (peer->identity_sent[index] == 1 &&
			!elapsed(now, peer->identity_sent_time[index], card ? DELTA_PEER_IDENTITY_RENEW : DELTA_PEER_IDENTITY_PROOF_GAP))
		{
			continue;
		}
		memset(&proof, 0, sizeof(proof));
		if (!peer->env.identity_prove(peer->env.context, index, challenge->nonce, challenge->binding,
			challenge->binding_length, peer->machine_index, flags, &proof))
		{
			continue;
		}
		proof.local_player = index;
		peer_send(peer, peer->host_ipv4, peer->host_port, data, delta_wire_write_id_proof(data, peer->session, &proof));
		peer->identity_sent[index] = 1;
		peer->identity_sent_time[index] = now;
	}
}

int delta_peer_identity(const struct delta_peer *peer, int machine_index, int local_player,
	struct delta_peer_identity *identity)
{
	int index;

	if (peer->mode == _mode_off || (peer->mode == _mode_client && !client_identity_agreed(peer)))
		return 0;
	for (index = 0; index < DELTA_PEER_IDENTITY_CARDS; index++)
	{
		const struct delta_peer_card *card = &peer->cards[index];

		if (card->used && card->proof.certificate_length && card->machine_index == machine_index &&
			card->local_player == local_player)
		{
			if (identity)
				*identity = card->identity;
			return 1;
		}
	}
	return 0;
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
