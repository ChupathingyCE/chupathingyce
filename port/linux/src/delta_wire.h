/*
DELTA_WIRE.H

Delta Peer's wire format (docs/delta.md, "Wire format"): its datagrams'
header and messages, encoded and decoded. Every decoder takes the bytes as
hostile input: sizes are checked before anything is read, counts capped,
text kept to printable ASCII, and nothing is allocated. Trailing bytes after
the fields a message has are ignored, so a later version may append fields
without raising the major.

Plain C89 with no platform dependency (no stdint.h, no snprintf), so the
fuzz harness (port/linux/tests/delta_fuzz.c), the dedicated server and
Warthog's old toolchain build it as it is. Numbers are little-endian.
*/

#ifndef HALO_DELTA_WIRE_H
#define HALO_DELTA_WIRE_H

#include "delta.h"

/* (32 bits on every target the game has: ILP32, LP64 and LLP64) */
typedef unsigned int delta_u32;

enum
{
	/* "DP" */
	DELTA_WIRE_MAGIC_0 = 0x44,
	DELTA_WIRE_MAGIC_1 = 0x50,
	/* magic (2), major, type, payload length (2), reserved (2), session (4):
	this layout is the same in every major, so a machine of another major
	can always be answered with LEGACY */
	DELTA_WIRE_HEADER_SIZE = 12,
	/* a whole datagram, header included: under the game's own (1264) and
	the invite tunnel's (1400) */
	DELTA_WIRE_MAXIMUM_DATAGRAM = 1200,
	DELTA_WIRE_MAXIMUM_PAYLOAD = DELTA_WIRE_MAXIMUM_DATAGRAM - DELTA_WIRE_HEADER_SIZE,

	/* a build's version string, as sent (printable ASCII) */
	DELTA_WIRE_BUILD_SIZE = 31,
	/* a profile's player ID (raw bytes; 32 hex digits as the site shows it) */
	DELTA_WIRE_PLAYER_ID_SIZE = 16,

	DELTA_WIRE_HELLO_SIZE = 20,
	DELTA_WIRE_WELCOME_SIZE = 24,
	DELTA_WIRE_ROSTER_SIZE = 12,
	DELTA_WIRE_PROFILE_SIZE = 20,
	DELTA_WIRE_ROSTER_ENTRY_SIZE = 36,
	/* (32) */
	DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES =
		(DELTA_WIRE_MAXIMUM_PAYLOAD - DELTA_WIRE_ROSTER_SIZE) / DELTA_WIRE_ROSTER_ENTRY_SIZE,

	/* a machine index none has (a dedicated server's own machine, or a
	client not yet given one) */
	DELTA_WIRE_NO_MACHINE = 0xFF
};

/* the message types; a number is never reused, and a machine ignores types
it does not know */
enum delta_message_type
{
	/* client to host: who it is and what it speaks */
	_delta_message_hello = 1,
	/* host to client: the same, and what the two will use */
	_delta_message_welcome = 2,
	/* host to client: another major; nothing more is said (its number and
	empty payload are the same in every major) */
	_delta_message_legacy = 3,
	/* host to client: every machine's claims, and the room's capabilities */
	_delta_message_roster = 4,
	/* either way: leaving; the other side forgets the session */
	_delta_message_bye = 5,
	/* client to host, after WELCOME, if both agreed to profile: its player
	ID and profile revision */
	_delta_message_profile = 6
};

struct delta_wire_header
{
	unsigned char major;
	unsigned char type;
	unsigned short length;
	/* the client's, chosen for each handshake (never 0); every message of
	the session carries it */
	delta_u32 session;
};

struct delta_wire_hello
{
	/* DELTA_CAPABILITY bits (1 << _delta_capability_...) */
	delta_u32 capabilities;
	/* the OpenCE network version it plays (HALO_PORT_NETWORK_VERSION) */
	unsigned short legacy_version;
	/* its machine in the game the host gave it (DELTA_WIRE_NO_MACHINE: not
	known yet) */
	unsigned char machine_index;
	/* its legacy table's serial (0: the built-in table) */
	delta_u32 legacy_table_serial;
	struct delta_platform_key key;
	char build[DELTA_WIRE_BUILD_SIZE + 1];
};

struct delta_wire_welcome
{
	delta_u32 capabilities;
	/* the bits both have: what this client and the host use */
	delta_u32 agreed;
	unsigned short legacy_version;
	/* the host's own machine (DELTA_WIRE_NO_MACHINE: a dedicated server's
	has no players) */
	unsigned char host_machine_index;
	delta_u32 legacy_table_serial;
	struct delta_platform_key key;
	char build[DELTA_WIRE_BUILD_SIZE + 1];
};

enum
{
	/* the machine speaks Delta Peer (else the rest is zeros) */
	DELTA_ROSTER_DELTA = 0x01,
	/* key holds its platform key (it agreed to platform) */
	DELTA_ROSTER_PLATFORM = 0x02,
	/* player_id and profile_revision hold its profile (it agreed to
	profile) */
	DELTA_ROSTER_PROFILE = 0x04
};

struct delta_wire_roster_entry
{
	unsigned char machine_index;
	unsigned char flags;
	/* what it agreed with the host */
	delta_u32 capabilities;
	struct delta_platform_key key;
	delta_u32 profile_revision;
	unsigned char player_id[DELTA_WIRE_PLAYER_ID_SIZE];
};

struct delta_wire_profile
{
	delta_u32 revision;
	unsigned char player_id[DELTA_WIRE_PLAYER_ID_SIZE];
};

struct delta_wire_roster
{
	/* the capabilities every machine in the game has now (0 while one has
	no Delta) */
	delta_u32 room_capabilities;
	/* the most players the room's machines take (delta_peer_room_limit) */
	unsigned char room_players;
	unsigned char count;
	struct delta_wire_roster_entry entries[DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES];
};

/* a datagram's header: 1 if it is Delta's (the magic) and its length is the
datagram's; the payload follows at data + DELTA_WIRE_HEADER_SIZE. A header of
another major is still read (to answer it with LEGACY) */
int delta_wire_read_header(const unsigned char *data, int size, struct delta_wire_header *header);
/* a header for a payload of length bytes into data (DELTA_WIRE_HEADER_SIZE) */
void delta_wire_write_header(unsigned char *data, int major, int type, int length, delta_u32 session);

/* a message's payload (the header's length): 1 if it holds the message.
Text that is not printable ASCII is replaced by '?', and a roster's count
is the entries it holds, at most DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES */
int delta_wire_read_hello(const unsigned char *payload, int size, struct delta_wire_hello *hello);
int delta_wire_read_welcome(const unsigned char *payload, int size, struct delta_wire_welcome *welcome);
int delta_wire_read_roster(const unsigned char *payload, int size, struct delta_wire_roster *roster);
int delta_wire_read_profile(const unsigned char *payload, int size, struct delta_wire_profile *profile);

/* a whole datagram of the message into data (DELTA_WIRE_MAXIMUM_DATAGRAM
bytes); returns its size, 0 if it does not fit */
int delta_wire_write_hello(unsigned char *data, delta_u32 session, const struct delta_wire_hello *hello);
int delta_wire_write_welcome(unsigned char *data, delta_u32 session, const struct delta_wire_welcome *welcome);
int delta_wire_write_roster(unsigned char *data, delta_u32 session, const struct delta_wire_roster *roster);
int delta_wire_write_profile(unsigned char *data, delta_u32 session, const struct delta_wire_profile *profile);
/* LEGACY and BYE: a header alone */
int delta_wire_write_empty(unsigned char *data, int major, int type, delta_u32 session);

/* a platform key's 8 bytes, each way (a key of a newer version is read for
the fields this one knows) */
void delta_wire_read_key(const unsigned char *data, struct delta_platform_key *key);
void delta_wire_write_key(unsigned char *data, const struct delta_platform_key *key);

/* the platform policy's defaults for a platform (DELTA_PLATFORM_POLICY in
delta.h) as a key: platform, version, limits and memory class; an unknown
platform's row for one this build does not know */
void delta_platform_policy_default(int platform, struct delta_platform_key *key);

/* A token bucket: up to burst messages at once, refilled rate a second.
1 if a message may be taken now (and takes it). Times in milliseconds,
wrapping */
struct delta_rate
{
	delta_u32 tokens_milli;
	delta_u32 time;
	int started;
};
int delta_rate_take(struct delta_rate *rate, delta_u32 now, int rate_per_second, int burst);

#endif
