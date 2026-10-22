/*
DELTA.H

Delta, ChupathingyCE's network family (docs/delta.md): what our machines say
to each other and to our services beyond the game protocol OpenCE defines,
which stays OpenCE's byte for byte. This header holds Delta Peer's numbers
(its major, its advertisement flag, the capability registry), the legacy
number's compatibility table, this build's wire, and the signed legacy
table's calls (port/linux/src/delta.c).

The compatibility table documents the legacy numbers and checks
halo_port_limits.h against them (tools/test_delta.py). Delta Peer's wire
format is port/linux/src/delta_wire.h; its sessions are delta_peer.c.

Plain C89 (no // comments, no stdint.h): Warthog (the Xbox build, MSVC 7.1)
reads this header too.
*/

#ifndef HALO_DELTA_H
#define HALO_DELTA_H

/* ---------- Delta Peer */

/* Delta Peer's major: raised only for a breaking change to its own framing
or handshake. Machines of different majors fall back to the legacy protocol
alone. */
#define DELTA_MAJOR 1

/* a host that speaks Delta Peer sets this in its advertisement's flags
(HALO_PORT_ADVERTISED_FLAGS_OFFSET; 0x01 and 0x02 are the port's): OpenCE's
machines ignore it. Set only while the host's Delta socket is open and
network.protocol is not "opence" (delta_peer_advertised_flags). */
#define DELTA_ADVERTISED_FLAG 0x04
/* ... and with it, after the port's flags in the advertisement's reserved
bytes, the game wire the host plays: its Delta major (a byte), then
DELTA_WIRE, zero padded (delta_peer_advertised_wire). OpenCE's machines, and
hosts built before then, leave the bytes zero. A Delta client joins a host
of its own major and wire whatever OpenCE number either announces, and
refuses one of another wire; a host without the bytes, or of another major,
is joined by its OpenCE number, as before
(delta_peer_advertised_wire_matches). A host that writes the range bytes puts in the wire there its
OLDEST COMPATIBLE wire that a released client knows (one that checks
the bytes for an exact match, as 0.7.5a's does, then joins), and the host's
own wire and its compatible range follow in the range bytes below. */
#define DELTA_ADVERTISED_WIRE_OFFSET 3
#define DELTA_ADVERTISED_WIRE_SIZE 16
/* ... and after the wire, in the six bytes that were free to the end of the
advertisement's 0x1A reserved bytes (docs/delta.md, "Wire compatibility
ranges"): the range's layout version (1), the host's own wire (its number, then
its letter), its oldest compatible wire (number, letter), and a check byte
(0xA5 xor the five before). All zero when absent; machines that do not read
them (OpenCE's, released builds) leave and ignore them (delta_compat.h). */
#define DELTA_ADVERTISED_RANGE_OFFSET (DELTA_ADVERTISED_WIRE_OFFSET + 1 + DELTA_ADVERTISED_WIRE_SIZE)
#define DELTA_ADVERTISED_RANGE_SIZE 6
#define DELTA_ADVERTISED_RANGE_VERSION 1

/* Delta Peer's UDP port: the game's server port (NETWORK_GAME_SERVER_PORT,
5150) plus this, so 5160. A host listens there; a client sends from any
port. Its own port, so Delta's message numbers never meet OpenCE's. */
#define DELTA_PEER_PORT_OFFSET 10
#define DELTA_PEER_PORT (0x141E + DELTA_PEER_PORT_OFFSET)

/* The capability registry: one bit per optional feature. A bit's number is
never reused, a machine ignores the bits it doesn't know, and adding one
doesn't raise DELTA_MAJOR. */
enum delta_capability
{
	/* each player's platform, for the scoreboard's icons */
	_delta_capability_platform = 0,
	/* a player's ID and profile revision (the profile is the site's) */
	_delta_capability_profile = 1,
	/* a host's messages to its players: welcome, notices, the next map */
	_delta_capability_server_messages = 2,
	/* text chat between Delta players */
	_delta_capability_chat = 3,
	/* Halo PC maps' identity (MAP: the game's map's family, file name,
	size and hash), for every family past the Xbox's: Custom Edition,
	HaloMD and Halo PC retail (port/linux/src/delta_maps.c) */
	_delta_capability_ce_maps = 4,
	/* retired before use: ce_maps carries every Halo PC family's identity.
	The bit stays taken and is never set */
	_delta_capability_md_maps = 5,
	/* network co-op beyond OpenCE's */
	_delta_capability_coop = 6,
	/* AI sync beyond OpenCE's */
	_delta_capability_ai_sync = 7,
	/* map and game type votes */
	_delta_capability_vote = 8,
	/* retired before use: a console's slots are its platform key's limits
	(struct delta_platform_key), which every Delta machine sends. The bit
	stays taken and is never set */
	_delta_capability_console_slots = 9,
	/* a dedicated server's moderators: a player signs in with a key made
	from their player key, and kicks and bans from the game
	(docs/delta.md, Moderation) */
	_delta_capability_moderation = 10,
	/* Bomb game types (Bomb, Grifball: docs/bomb.md) and their player traits
	(TRAITS). Room-wide: a host plays a Bomb game type only with machines
	that agreed to it, and drops any other (delta_peer_machine_lacks) */
	_delta_capability_bomb = 11,
	/* a host answers Online Games' pings before joining (p2p_probe.c):
	announced on Delta List only, never offered in a HELLO or WELCOME
	(docs/delta.md, Pings before joining) */
	_delta_capability_ping = 12,
	/* TRAITS' movement and melee traits (jump, gravity, melee reach): a
	player has them only while its machine and the host both agreed to it,
	so that each machine moves and strikes as the host has it, and an
	earlier build's players play as they always did */
	_delta_capability_bomb_movement = 13,
	/* TRAITS at its own number (22): Grifball test builds 2 and 3 sent it
	as 18, chat's CHAT_CONFIG. Room-wide with bomb: a host plays a Bomb game
	type only with machines that agreed to both, and drops any other */
	_delta_capability_bomb_traits = 14,
	/* changing teams during a game (docs/delta.md, Team changes): a client
	asks its host (TEAM_REQUEST), the host decides and answers
	(TEAM_RESULT); the change itself reaches every machine, OpenCE's too,
	through the game's own player respawn */
	_delta_capability_team_change = 15,

	NUMBER_OF_DELTA_CAPABILITIES
};

/* The platform registry: one byte in every machine's platform key. A
number is never reused; new platforms are added at the end. */
enum delta_platform
{
	_delta_platform_unknown = 0,
	_delta_platform_pc_windows = 1,
	_delta_platform_pc_macos = 2,
	_delta_platform_pc_linux = 3,
	_delta_platform_android = 4,
	_delta_platform_steam_deck = 5,
	/* the original Xbox (Warthog) */
	_delta_platform_xbox = 6,
	_delta_platform_xbox360 = 7,
	_delta_platform_wiiu = 8,
	_delta_platform_switch = 9,
	/* ports built on ours by others: the PSP (Sheepdog01), the Vita (Lord
	Birchwood), and the Xbox One/Series in developer mode (warthog-uwp) */
	_delta_platform_psp = 10,
	_delta_platform_vita = 11,
	_delta_platform_xbox_uwp = 12,

	NUMBER_OF_DELTA_PLATFORMS
};

/* A machine's platform key: its platform and what it can take, 8 bytes on
the wire in this order (delta_wire.c). Claims, never trusted for game state,
but what a host uses to protect weaker machines: a host keeps its game's
players at or under every Delta machine's limit (delta_peer_room_limit). */
#define DELTA_PLATFORM_KEY_SIZE 8
/* the limits' layout; a machine reads the fields it knows of a newer one */
#define DELTA_PLATFORM_KEY_VERSION 1
/* ... the player opted in to games above join_players, up to
join_players_opt_in (a console's setting; off by default) */
#define DELTA_PLATFORM_KEY_OPTED_IN 0x01
/* ... a dedicated server: hosts, and has no players of its own */
#define DELTA_PLATFORM_KEY_DEDICATED 0x02

struct delta_platform_key
{
	/* enum delta_platform */
	unsigned char platform;
	/* DELTA_PLATFORM_KEY_VERSION */
	unsigned char version;
	/* DELTA_PLATFORM_KEY_* */
	unsigned char flags;
	/* the most players a game this machine hosts can have (0: it does not
	host) */
	unsigned char host_players;
	/* the most players of a game it joins, by default; and with its
	player's opt-in (at least join_players) */
	unsigned char join_players;
	unsigned char join_players_opt_in;
	/* its memory, as log2 of megabytes: 6 for 64 MB (the Xbox), 7 128 MB,
	8 256 MB, 9 512 MB, 10 1 GB, 11 2 GB, 12 4 GB...; 0 unknown */
	unsigned char memory_class;
	unsigned char reserved;
};

/* The platform policy: Delta's caveats, by platform, the defaults every
machine's key starts from and every host applies to protect the weakest
machine of its game (delta_peer.c): the most players it hosts, the most of
a game it joins (unless its player opted in: network.platform_limits =
"off"), the most it joins opted in, its memory class (log2 of megabytes),
and whether it plays network co-op (a host offers no co-op while a machine
that does not is in its game: delta_peer_room_coop). One row a platform, in
the registry's order. The consoles' rows are placeholders until their ports
play (the original Xbox's is the owner's: 16 and 16, no co-op); the signed
legacy table's "platform_policy" section is to tune them without a release
(delta_peer_platform_policy, delta_peer_game.c). */
#define DELTA_PLATFORM_POLICY(X) \
	X(_delta_platform_unknown, 128, 128, 128, 0, 1) \
	X(_delta_platform_pc_windows, 128, 128, 128, 12, 1) \
	X(_delta_platform_pc_macos, 128, 128, 128, 12, 1) \
	X(_delta_platform_pc_linux, 128, 128, 128, 12, 1) \
	X(_delta_platform_android, 128, 128, 128, 11, 1) \
	X(_delta_platform_steam_deck, 128, 128, 128, 14, 1) \
	X(_delta_platform_xbox, 16, 16, 128, 6, 0) \
	X(_delta_platform_xbox360, 16, 16, 128, 9, 1) \
	X(_delta_platform_wiiu, 16, 16, 128, 10, 1) \
	X(_delta_platform_switch, 16, 16, 128, 12, 1) \
	X(_delta_platform_psp, 16, 16, 128, 6, 0) \
	X(_delta_platform_vita, 16, 16, 128, 9, 0) \
	X(_delta_platform_xbox_uwp, 128, 128, 128, 12, 1)

/* ---------- the legacy number */

/* This build's wire: the revision of the game protocol it speaks. A signed
legacy table (port/linux/src/delta.c; docs/delta.md, "The legacy table as
config") has a row of OpenCE numbers for each wire, which CI adds to only
after a cross-play test of that wire; a build reads its own wire's row alone.
Give each release that changes what the machines send a new one. */
#define DELTA_WIRE "chupa-32a"

/* Wire compatibility ranges: the oldest wire each one plays the game with,
without a desync and without losing what the older wire has (a feature the
older wire lacks is dropped or unused with it; one it cannot play ends the
join cleanly). One row for a wire that plays with older ones; a wire with no
row plays with itself alone. A wire's row is added when the release is made
and is not changed afterward: an older build cannot learn of a newer row, so
a host also tells its newer clients its range in the advertisement. The first
wire a build advertised is DELTA_WIRE_FIRST_ADVERTISED: what a host puts in
the advertisement's old wire slot is its oldest compatible wire from there on,
as released clients compare it for an exact match (delta_compat.h). */
#define DELTA_WIRE_FIRST_ADVERTISED "chupa-29a"
#define DELTA_WIRE_COMPATIBILITY(X) \
	X("chupa-29a", "chupa-28a") /* maps by their own names: 29 also takes 28's hashed names and checksums */ \
	X("chupa-32a", "chupa-28a") /* 30 to 32 are additive; a host of 32 plays with 28 to 31 (docs/delta.md) */

/* how many OpenCE numbers past the one announced (the newest this build, or
the signed table, has proven) are presumed additive: a client joins their
hosts from the day OpenCE raises, before a table follows. The built-in
maximum (HALO_PORT_NETWORK_VERSION_MAXIMUM) is at least the built-in number
plus this (tools/test_delta.py), and a signed table's row at least its
announce plus this (delta.c). Such a host's game is played as an additive
raise is: what the client does not know it drops, and content it can't
match (a map of another version) ends its join rather than playing on. A
raise that changes an existing message is breaking: a release follows it
(docs/delta.md, "The legacy number"). */
#define DELTA_LEGACY_PRESUMED 1

/* OpenCE's network versions (HALO_PORT_NETWORK_VERSION in its builds), the
first of its releases with each, and whether the version's change is one the
version before plays multiplayer with as it is (additive: messages a machine
of the older version drops) or not (breaking). OpenCE's clients join only
hosts of their exact version; ours join every version back to the newest
breaking one (HALO_PORT_NETWORK_VERSION_MINIMUM), and our hosts announce the
newest (HALO_PORT_NETWORK_VERSION). One row a version, oldest first; the
command repository's watch adds a row when it follows OpenCE's raise
(tools/follow.py there). Versions 1 to 9 each changed the wire format
(port/linux/NETCODE.md, "Versions"). */
#define DELTA_LEGACY_VERSIONS(X) \
	X(10, "build-73", additive) /* players' pings for the scoreboard */ \
	X(11, "build-76", breaking) /* the gametype's PC options in the settings record */ \
	X(12, "build-118", additive) /* network co-op */ \
	X(13, "build-119", additive) /* co-op's extra enemies */ \
	X(14, "build-123", additive) /* co-op devices' positions, units opening and closing */ \
	X(15, "build-124", additive) /* co-op allegiances, loading zones, falling players */ \
	X(16, "build-125", additive) /* co-op: every machine stays on the host's BSP */ \
	X(17, "build-128", additive) /* followed from OpenCE: additive */ \
	X(18, "build-129", additive) /* followed from OpenCE: additive */ \
	X(19, "build-132", additive) /* co-op's player collisions switch, in a padding byte of the game settings */ \
	X(20, "build-133", additive) /* password games' internet listings (another listing layout); game messages as 19 */ \
	X(21, "build-141", additive) /* killing blows and resting bodies resent, co-op BSP switches by the host's crossing */ \
	X(22, "build-145", additive) /* a Custom Edition map named custom_maps\\<name> in the game's settings */ \
	X(23, "build-147", additive) /* a Custom Edition map's blocks past the Xbox tools' limits kept, its version in the game's settings; Xbox maps as 22 */ \
	X(24, "build-149", additive) /* the gametype's PC vehicle set: every vehicle the map places */ \
	X(25, "build-169", additive) /* units' integrated lights in the object states; a banned machine told so */ \
	X(26, "build-184", additive) /* internet play's signalling, version 4 (WebRTC, for browsers; p2p_signal.c reads 3 and 4); game messages as 25 */ \
	X(27, "build-189", additive) /* Custom Edition map sharing's messages (a machine of 26 drops them); game messages as 25 */ \
	X(28, "build-201", additive) /* no message changed: a script's switch_bsp keeps a multiplayer game's first, the flashlight past the Xbox limits, a second weapon readied while firing (wire chupa-28a) */ \
	X(29, "build-206", additive) /* a Custom Edition map by its own name, its hash's first 32 bits its version in the game record (a client of 29 also takes 28's hashed names and header checksums; wire chupa-29a) */ \
	X(30, "build-206", additive) /* elevator riders carried by the tick's own movement, a host takes only the across of a client's word on a unit riding an elevator (moving, or stood on at rest), a client discards the height of a correction on its elevator up to 4.0 */ \
	X(31, "build-206", additive) /* the pregame keep-alive's fourth byte pair sent as zero and the packet table's field lists on their own messages, datagrams stamped more than 450 ticks from the sender's latest dropped, client_ready, client_identity and hit reports taken from a stream alone, a second game on one computer (its own port) */ \
	X(32, "build-244", additive) /* the host's policy on aiming assist for the mouse and keyboard (message 90), a client's input saying whether its player plays co-op without friendly fire (wire chupa-32a) */

/* ---------- the legacy table (port/linux/src/delta.c)

The numbers in use (delta_legacy_announce, _minimum and _maximum, in
halo_port_limits.h) are the built-in ones, widened by the newest signed
legacy table this machine has (or set by a local, unsigned override). */

/* at start-up, on the game's thread: loads the cached table, and fetches a
newer one in the background (never holding anything up) */
void delta_legacy_start(void);
/* the serial of the signed table in use; 0 for none (or a local override,
which is never passed on) */
unsigned int delta_legacy_serial(void);
/* the signed table in use as it travels between machines (the signature's
128 hex digits, a line feed, the document) into buffer: its size, or 0 for
none or when it does not fit (DELTA_LEGACY_SIGNED_SIZE always does) */
int delta_legacy_signed(char *buffer, int size);
/* a signed table from elsewhere (another machine): checked, and used if it
is newer than the one in use and widens no less than the built-in numbers;
1 if it was taken */
int delta_legacy_offer(const char *signed_table, int size);
/* whether the table in use turns a capability off (its kill switch) */
int delta_capability_disabled(int capability);
/* what the build follows, for its log and status: "Following OpenCE
build-145 (table 2)" (the table's row names the OpenCE build the cross-play
gate proved it with), "Following OpenCE network version 20 (built in)" */
#define DELTA_FOLLOWS_SIZE 32
void delta_legacy_following(char *text, int size);

/* whether a local, unsigned table (network.legacy_table) is in use */
int delta_legacy_override(void);
/* whether this machine relays signed tables (Delta Peer: takes them from
and sends them to other machines): not with a local table in use, nor
without a key to check one with */
int delta_legacy_relay(void);

/* the largest document, and signed table */
#define DELTA_LEGACY_DOCUMENT_SIZE 16384
#define DELTA_LEGACY_SIGNED_SIZE (128 + 1 + DELTA_LEGACY_DOCUMENT_SIZE)

#endif
