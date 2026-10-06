/*
DELTA.H

Delta, ChupathingyCE's network family (docs/delta.md): what our machines say
to each other and to our services beyond the game protocol OpenCE defines,
which stays OpenCE's byte for byte. This header holds Delta Peer's numbers
(its major, its advertisement flag, the capability registry) and the legacy
number's compatibility table.

The legacy table documents the legacy numbers and checks
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
	/* Halo PC maps' identity (name and hash) */
	_delta_capability_ce_maps = 4,
	/* HaloMD maps' identity */
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
	/* its memory: 1 up to 64 MB (the Xbox), 2 up to 512 MB, 3 up to 2 GB,
	4 more; 0 unknown */
	unsigned char memory_class;
	unsigned char reserved;
};

/* The platform policy: Delta's caveats, by platform, the defaults every
machine's key starts from and every host applies to protect the weakest
machine of its game (delta_peer.c): the most players it hosts, the most of
a game it joins (unless its player opted in: network.platform_limits =
"off"), the most it joins opted in, and its memory class. One row a
platform, in the registry's order. The consoles' rows are placeholders until
their ports play (the original Xbox's is the owner's: 16 and 16); the signed
legacy table's "platform_policy" section is to tune them without a release
(delta_peer_platform_policy, delta_peer_game.c). */
#define DELTA_PLATFORM_POLICY(X) \
	X(_delta_platform_unknown, 128, 128, 128, 0) \
	X(_delta_platform_pc_windows, 128, 128, 128, 4) \
	X(_delta_platform_pc_macos, 128, 128, 128, 4) \
	X(_delta_platform_pc_linux, 128, 128, 128, 4) \
	X(_delta_platform_android, 128, 128, 128, 3) \
	X(_delta_platform_steam_deck, 128, 128, 128, 4) \
	X(_delta_platform_xbox, 16, 16, 128, 1) \
	X(_delta_platform_xbox360, 16, 16, 128, 2) \
	X(_delta_platform_wiiu, 16, 16, 128, 3) \
	X(_delta_platform_switch, 16, 16, 128, 3)

/* ---------- the legacy number */

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
	X(18, "build-129", additive) /* followed from OpenCE: additive */

#endif
