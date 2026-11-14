/*
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later

DELTA_REF.H

A small reference client for Delta Peer, for other people's ports of Halo:
the handshake (HELLO, WELCOME, LEGACY, BYE, and the ROSTER's room fields),
the capability bits, and a game's advertisement (the reserved bytes a host
announces and a client reads). It is the minimum a port needs to join the
Delta network for plain cross-play; docs/delta-for-ports.md is the guide,
docs/delta.md the full spec.

Plain C99. No allocation (every buffer is the caller's), no threads, no
clock, no randomness and no OS calls: the port passes in the time, a random
session number and a function that sends a datagram. Every number on the
wire is little-endian and is read and written a byte at a time, so the same
code runs on big-endian machines (the Dreamcast is little-endian, the N64 and
the PowerPC consoles big-endian).

Every reader takes its bytes as hostile: sizes are checked before anything
is read, and nothing is written past a buffer's size.
*/

#ifndef DELTA_REF_H
#define DELTA_REF_H

#include <stdint.h>

/* ---------- numbers from docs/delta.md */

enum
{
	/* Delta Peer's major (the header's third byte) */
	DREF_MAJOR = 1,
	/* the header: "DP", major, type, payload length (2), reserved (2),
	session (4) */
	DREF_HEADER_SIZE = 12,
	/* a whole datagram, header included, at most */
	DREF_MAXIMUM_DATAGRAM = 1200,
	/* a host's Delta Peer port is its game port plus this (5150 + 10 = 5160) */
	DREF_PORT_OFFSET = 10,
	DREF_HOST_PORT = 5160,
	/* a build's version text, at most (printable ASCII) */
	DREF_BUILD_SIZE = 31,
	/* a machine index nobody has */
	DREF_NO_MACHINE = 0xFF,
};

/* the legacy table serial that says "this machine takes no tables": a host
then sends it none of the signed table's pieces */
#define DREF_NO_TABLES 0xFFFFFFFFu

/* message types (never reused; a type a machine does not know is ignored) */
enum
{
	DREF_HELLO = 1,
	DREF_WELCOME = 2,
	DREF_LEGACY = 3,
	DREF_ROSTER = 4,
	DREF_BYE = 5
};

/* the capability registry: one bit per optional feature (1u << bit). A port
that plays plainly offers DREF_CAP_PLATFORM alone. */
enum
{
	DREF_CAP_PLATFORM = 0,
	DREF_CAP_PROFILE = 1,
	DREF_CAP_SERVER_MESSAGES = 2,
	DREF_CAP_CHAT = 3,
	DREF_CAP_CE_MAPS = 4,
	/* retired before use: never set */
	DREF_CAP_MD_MAPS = 5,
	DREF_CAP_COOP = 6,
	DREF_CAP_AI_SYNC = 7,
	DREF_CAP_VOTE = 8,
	/* retired before use: never set */
	DREF_CAP_CONSOLE_SLOTS = 9,
	DREF_CAP_MODERATION = 10,
	DREF_CAP_BOMB = 11,
	/* the game list's alone: never in a HELLO or WELCOME */
	DREF_CAP_PING = 12,
	DREF_CAP_BOMB_MOVEMENT = 13,
	DREF_CAP_BOMB_TRAITS = 14,
	DREF_CAP_TEAM_CHANGE = 15,
	DREF_CAP_SERVER_BADGE = 16,
	DREF_CAP_OBJECT_SYNC = 17,
	/* the game list's alone: never in a HELLO or WELCOME */
	DREF_CAP_HOST_CHEATS = 18,
	DREF_CAP_HOST_RULES = 19,
	DREF_CAP_IDENTITY = 20,
	DREF_CAP_PARTY_TEAMS = 21,
	DREF_CAP_VOICE = 22,
	DREF_CAP_REPORTS = 23,
	DREF_CAP_MCC_MAPS = 24,
	DREF_CAP_GAME_MODES = 25
};

/* the bits a HELLO or WELCOME never carries (retired, or the list's alone) */
#define DREF_CAPS_NEVER_SAID \
	((1u << DREF_CAP_MD_MAPS) | (1u << DREF_CAP_CONSOLE_SLOTS) | (1u << DREF_CAP_PING) | (1u << DREF_CAP_HOST_CHEATS))

/* platform numbers (docs/delta.md, "Platform numbers"). Ask for one before
you ship; never pick a free one yourself. */
enum
{
	DREF_PLATFORM_UNKNOWN = 0,
	DREF_PLATFORM_N64 = 17,
	DREF_PLATFORM_PC_LEGACY = 18
};

/* ---------- the platform key: 8 bytes in HELLO, WELCOME and the roster */

struct dref_platform_key
{
	uint8_t platform;
	/* 1 */
	uint8_t version;
	/* 0x01 its player turned the caveats off; 0x02 a dedicated server */
	uint8_t flags;
	/* the most players a game it hosts can have (0: it never hosts) */
	uint8_t host_players;
	/* the most players of a game it joins, and with its player's opt-in */
	uint8_t join_players;
	uint8_t join_players_opt_in;
	/* log2 of its megabytes (6: 64 MB, 3: 8 MB); 0 unknown */
	uint8_t memory_class;
};

/* ---------- messages */

struct dref_header
{
	uint8_t major;
	uint8_t type;
	uint16_t length;
	uint32_t session;
};

/* HELLO (client to host) and WELCOME (host to client) share a shape */
struct dref_hello
{
	uint32_t capabilities;
	/* WELCOME's alone: the bits both sides use */
	uint32_t agreed;
	/* the OpenCE network version this machine plays */
	uint16_t legacy_version;
	/* HELLO: this machine's index in the game; WELCOME: the host's own
	(DREF_NO_MACHINE: a dedicated server) */
	uint8_t machine_index;
	/* its signed legacy table's serial: 0 the built-in one, DREF_NO_TABLES
	none taken */
	uint32_t table_serial;
	struct dref_platform_key key;
	/* printable ASCII, ended */
	char build[DREF_BUILD_SIZE + 1];
	/* the game modes it plays (bit 0 SWAT, 1 Hardcore, 2 Gun Game, 3
	Infection); 0 for a plain port */
	uint32_t modes;
};

/* ROSTER's room fields (its entries are read one at a time) */
struct dref_roster
{
	/* what every machine of the game has now */
	uint32_t room_capabilities;
	/* the most players the room takes (0: no limit said) */
	uint8_t room_players;
	uint8_t count;
};

struct dref_roster_entry
{
	uint8_t machine_index;
	/* 0x01 speaks Delta, 0x02 platform key present, 0x04 profile present */
	uint8_t flags;
	uint32_t capabilities;
	struct dref_platform_key key;
};

/* a datagram's header: 1 if it is Delta Peer's (the magic) and its length
is the datagram's, else 0. Another major is still read (the host may have
answered LEGACY). */
int dref_header_read(const uint8_t *data, int size, struct dref_header *header);

/* HELLO and WELCOME into out (at most out_size bytes): the datagram's size,
or 0 if it does not fit */
int dref_hello_write(uint8_t *out, int out_size, uint32_t session, const struct dref_hello *hello);
int dref_welcome_write(uint8_t *out, int out_size, uint32_t session, const struct dref_hello *welcome);
/* LEGACY, BYE or any other message without a payload */
int dref_empty_write(uint8_t *out, int out_size, int major, int type, uint32_t session);

/* a payload (the header's length bytes after it): 1 if it holds the
message. Text that is not printable ASCII reads as '?'. */
int dref_hello_read(const uint8_t *payload, int size, struct dref_hello *hello);
int dref_welcome_read(const uint8_t *payload, int size, struct dref_hello *welcome);
int dref_roster_read(const uint8_t *payload, int size, struct dref_roster *roster);
/* the roster's entry index (below its count): 1 if read */
int dref_roster_entry(const uint8_t *payload, int size, int index, struct dref_roster_entry *entry);

/* capabilities with the bits a HELLO never carries taken out */
uint32_t dref_capabilities_clean(uint32_t capabilities);

/* ---------- the client's handshake */

typedef void (*dref_send_fn)(void *user, const uint8_t *data, int size);

enum
{
	/* not started */
	DREF_CLIENT_OFF = 0,
	/* HELLO sent, no answer yet */
	DREF_CLIENT_WAITING,
	/* WELCOME heard: Delta Peer for the rest of the game */
	DREF_CLIENT_DELTA,
	/* LEGACY, BYE or no answer within DREF_HANDSHAKE_MS: the legacy
	protocol alone until the machine leaves the game */
	DREF_CLIENT_LEGACY
};

enum
{
	DREF_HELLO_RESEND_MS = 500,
	DREF_HANDSHAKE_MS = 4000
};

struct dref_client
{
	int state;
	uint32_t session;
	uint32_t started;
	uint32_t hello_sent;
	struct dref_hello mine;
	/* the host's WELCOME, once heard */
	struct dref_hello host;
	/* the bits both use: never more than this machine offered */
	uint32_t agreed;
	/* the latest ROSTER's room fields (room_players 0 until one comes) */
	uint32_t room_capabilities;
	uint8_t room_players;
	dref_send_fn send;
	void *user;
};

/* Start the handshake once the host has given this machine its machine
index (its pregame) and only if the host's advertisement says it speaks
Delta (dref_advert_speaks_delta). mine->machine_index is that index.
session is random and never 0 (0 is taken as 1). Sends the first HELLO. */
void dref_client_start(struct dref_client *client, const struct dref_hello *mine, uint32_t session, uint32_t now_ms,
	dref_send_fn send, void *user);
/* every frame or so: HELLO again every DREF_HELLO_RESEND_MS, legacy after
DREF_HANDSHAKE_MS. Never blocks; the game never waits on it. */
void dref_client_tick(struct dref_client *client, uint32_t now_ms);
/* a datagram that came to the Delta socket from the host's address and
port (the caller drops everything else): the message type it took, 0 if
none. Unknown types are ignored (0). */
int dref_client_receive(struct dref_client *client, uint32_t now_ms, const uint8_t *data, int size);
/* leaving the game: BYE if the host speaks Delta with us, then off */
void dref_client_stop(struct dref_client *client);

/* ---------- the advertisement: a game's 0x1A reserved bytes */

enum
{
	DREF_RESERVED_SIZE = 0x1A,
	/* OpenCE's fields, from the front */
	DREF_RESERVED_VERSION = 0,
	DREF_RESERVED_FLAGS = 2,
	DREF_RESERVED_KIND = 3,
	/* the flags */
	DREF_FLAG_DISTRIBUTED = 0x01,
	DREF_FLAG_IN_PROGRESS = 0x02,
	/* a dedicated server's game (OpenCE 39 on; Delta's own flag before) */
	DREF_FLAG_DEDICATED = 0x04,
	/* Delta's tag at the tail: 'D', 'L', major, layout (1), wire number
	and letter, oldest wire number and letter, check */
	DREF_TAG_OFFSET = 17,
	DREF_TAG_SIZE = 9
};

/* a wire ID, "chupa-44b": number 44, letter 'b' */
struct dref_wire
{
	int number;
	int letter;
};

struct dref_advert
{
	/* the OpenCE network version the host announces */
	unsigned version;
	unsigned flags;
	/* the game's kind (OpenCE 37 on; 0 none) */
	unsigned kind;
	/* the host speaks Delta Peer: say HELLO (by its tag, or by the old
	layout of hosts before chupa-39a) */
	int speaks_delta;
	/* the tag, if valid: the host's major and wires */
	int has_tag;
	unsigned major;
	struct dref_wire wire;
	struct dref_wire oldest;
};

/* the reserved bytes (size of them, at least DREF_RESERVED_SIZE): 1 if
read (the size was enough) */
int dref_advert_read(const uint8_t *reserved, int size, struct dref_advert *advert);
/* a host's reserved bytes: OpenCE's version, flags and kind, the rest zero
(a port has no Delta tag of its own: docs/delta-for-ports.md) */
void dref_advert_write(uint8_t *reserved, unsigned version, unsigned flags, unsigned kind);

/* The join mark: a client that will say HELLO marks its join request in its
machine name (count 16-bit characters, the game's own array), past the
name's end, so a Delta host waits for its HELLO before sending its game
settings record. 1 if marked (a name too long for it is left as it is). */
enum
{
	DREF_JOIN_MARK_FIRST = 0x0394,
	DREF_JOIN_MARK_SECOND = 0x0001
};
int dref_join_mark(uint16_t *name, int count);

/* ---------- the game list (Delta List) */

/* The Delta fields of a host's announcement (POST /v1/announce, form
encoded), appended to the caller's form (an "&" first if it is not empty):
delta, platform_key, arch, build, capabilities. delta is DREF_MAJOR for a
host that answers HELLO, else 0. The form's length, or -1 if it would not
fit (the form is then left as it was). */
int dref_announce_fields(char *form, int size, int delta, const struct dref_platform_key *key, const char *arch,
	const char *build, uint32_t capabilities);

#endif
