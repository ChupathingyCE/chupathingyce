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

/* A host that speaks Delta Peer says so in its advertisement's 0x1A reserved
bytes with a tagged block at their TAIL (docs/delta.md, "The advertisement"):
OpenCE writes its own fields there from the front (the version, the flags,
since its version 37 the game's kind at byte 3), so the tail stays clear of
them. The block: 'D', 'L', the host's Delta major, the block's layout
version (1), the host's own wire (its number, then its letter), its oldest
compatible wire (number, letter), and a check byte (0xA5 xor the eight before;
the 2026-10-27 console builds read it so).
A reader knows Delta by a valid tag and check, never by a flag bit. Written
only while the host's Delta socket is open and network.protocol is not
"opence" (delta_peer_advertised_wire); OpenCE's machines leave and ignore the
bytes. */
#define DELTA_ADVERTISED_RESERVED_SIZE 0x1A
#define DELTA_ADVERTISED_TAG_SIZE 9
#define DELTA_ADVERTISED_TAG_OFFSET (DELTA_ADVERTISED_RESERVED_SIZE - DELTA_ADVERTISED_TAG_SIZE)
#define DELTA_ADVERTISED_TAG_FIRST 'D'
#define DELTA_ADVERTISED_TAG_SECOND 'L'
#define DELTA_ADVERTISED_TAG_VERSION 1

/* The old layout, which hosts before the tag wrote (0.7.5a to 0.8.0h, wires
to chupa-38a) and which readers still take when there is no tag, so that a
new client still finds an old Delta host. Released clients read nothing
else, so a host still writes it where it does not clash with OpenCE's
meaning of the bytes (docs/delta.md, "The transition"): OpenCE took the flag
0x04 for a dedicated server (version 39) and byte 3 for the game's kind
(version 37), so a dedicated server alone sets the flag, and writes its
major at byte 3 only in a game whose engine is not Slayer, the one engine
OpenCE names a kind for (DELTA_ADVERTISED_KIND_ENGINE). The flag (in the
port's flags byte, HALO_PORT_ADVERTISED_FLAGS_OFFSET) said Delta... */
#define DELTA_ADVERTISED_FLAGS_OFFSET 2
#define DELTA_ADVERTISED_FLAG 0x04
/* ... and with it, in the bytes after the flags, the host's Delta major (a
byte), then a wire, zero padded: its OLDEST COMPATIBLE wire that a released
client knows (one that checks the bytes for an exact match, as 0.7.5a's
does, then joins). Readers stop at the first zero, so the tag may follow it
in the slot's last three bytes: a host writes the slot's wire only when it
is ended before the tag (delta_compat_advertise_old) */
#define DELTA_ADVERTISED_WIRE_OFFSET 3
#define DELTA_ADVERTISED_WIRE_SIZE 16
/* ... and after the wire, to the end of the reserved bytes, the range: its
layout version (1), the host's own wire (number, letter), its oldest
compatible wire (number, letter), and a check byte (0xA5 xor the five
before). The tag's last six bytes sit where these did, with a check of its
own, so a released client never reads them as range bytes: it takes the old
slot's wire (delta_compat.h). */
#define DELTA_ADVERTISED_RANGE_OFFSET (DELTA_ADVERTISED_WIRE_OFFSET + 1 + DELTA_ADVERTISED_WIRE_SIZE)
#define DELTA_ADVERTISED_RANGE_SIZE 6
#define DELTA_ADVERTISED_RANGE_VERSION 1
/* ... the OpenCE version from which the flag can be OpenCE's (a dedicated
server): an old-layout host announcing it or later is taken for Delta only
with a wire in the old slot */
#define DELTA_ADVERTISED_FLAG_TAKEN_VERSION 39
/* ... the game engine (its stock index, Slayer) of the one game whose kind
byte OpenCE's browsers read (its version 37): a major there names an OpenCE
game type (1, SWAT), so a host writes the old layout's major and slot only
in games of other engines */
#define DELTA_ADVERTISED_KIND_ENGINE 2

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
	/* reserved for server badges (a draft, not built yet): never set here */
	_delta_capability_server_badge = 16,
	/* what the game's own messages leave out of the host's objects
	(docs/delta.md, Object sync): every unit's shield and health as they
	change (UNIT_VITALS), so that a client's copy of a placed biped or an
	empty vehicle reads as the host's does, and the scenario names of the
	objects the host made (OBJECT_NAMES), so that a client's scripts find
	them by name */
	_delta_capability_object_sync = 17,

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
Give each release that changes what the machines send a new one, and each
build that learns a legacy layout or behavior (a newer DELTA_LEGACY_NEWEST):
a row widens every build of its wire, so a wire's builds must all play the
same OpenCE versions (docs/delta.md, "When the wire changes"). chupa-38a:
0.8.0e and the Xbox build of 2026-10-21 said chupa-32a and play 32 (33's one
bit is the host's), 0.8.0f and 0.8.0g said it too and play to 38. chupa-39a:
the tagged advertisement (a Delta host's block at the reserved bytes' tail,
no longer OpenCE's flag 0x04 and byte 3), hosts' message 91, co-op's ally
switch honored, and 39's dedicated flag on dedicated servers that announce
it. */
#define DELTA_WIRE "chupa-39a"

/* Wire compatibility ranges: the oldest wire each one plays the game with,
without a desync and without losing what the older wire has (a feature the
older wire lacks is dropped or unused with it; one it cannot play ends the
join cleanly). One row for a wire that plays with older ones; a wire with no
row plays with itself alone. A wire's row is added when the release is made
and is not changed afterward: an older build cannot learn of a newer row, so
a host also tells its newer clients its range in the advertisement. The first
wire a build advertised is DELTA_WIRE_FIRST_ADVERTISED: what a host of the
old layout put in the advertisement's old wire slot was its oldest compatible
wire from there on, as released clients compare it for an exact match
(delta_compat.h). Every row from DELTA_WIRE_CONSOLE_FLOOR on reaches back to
it (tools/test_delta.py fails otherwise): the oldest wire a console build in
the wild plays, so that every Chupathingy platform stays in one pool
(docs/delta.md, "The console floor"). Raise it only when every console port
has shipped a newer wire. */
#define DELTA_WIRE_CONSOLE_FLOOR "chupa-32a"
#define DELTA_WIRE_FIRST_ADVERTISED "chupa-29a"
#define DELTA_WIRE_COMPATIBILITY(X) \
	X("chupa-29a", "chupa-28a") /* maps by their own names: 29 also takes 28's hashed names and checksums */ \
	X("chupa-32a", "chupa-28a") /* 30 to 32 are additive; a host of 32 plays with 28 to 31 (docs/delta.md) */ \
	X("chupa-38a", "chupa-28a") /* 33 to 38 played by the host's number; a host announcing 32 plays as chupa-32a's */ \
	X("chupa-39a", "chupa-28a") /* the tagged advertisement and 39's dedicated flag; the game's messages as chupa-38a's, a host announcing 32 plays as chupa-32a's */

/* Each wire's newest OpenCE network version that every build of it plays by
its own rules (DELTA_LEGACY_NEWEST when it was made): the most a signed
table's row may widen that wire's builds to safely, without layouts. A row is
added with the wire and never changed (tools/test_delta.py holds the released
ones); DELTA_WIRE's is DELTA_LEGACY_NEWEST, so a build that learns a newer
version's rules has a new wire. */
#define DELTA_WIRE_LEGACY_NEWEST(X) \
	X("chupa-28a", 28) \
	X("chupa-29a", 29) \
	X("chupa-32a", 33) \
	X("chupa-38a", 38) \
	X("chupa-39a", 39)

/* the oldest network version a build that reads the tagged advertisement
says in its HELLO: chupa-39a's row above, the first such wire. A host whose
advertisement carries the tag alone hears none older; one that also carries
the old layout may (released builds before the tag said 32 or older). */
#define DELTA_TAG_OLDEST_LEGACY 39

/* how many OpenCE numbers past the newest this build knows (the table
below, and the one announced, the newest this build or the signed table has
proven as a host) are presumed additive: a client joins their hosts from the
day OpenCE raises, before a table follows. The built-in maximum
(HALO_PORT_NETWORK_VERSION_MAXIMUM) is at least the table's newest version
plus this (tools/test_delta.py), and a signed table's row at least its
announce plus this, and never below the built-in maximum (delta.c). Such a host's game is played as an additive
raise is: what the client does not know it drops, and content it can't
match (a map of another version) ends its join rather than playing on. A
raise that changes an existing message is breaking: a release follows it
(docs/delta.md, "The legacy number"). */
#define DELTA_LEGACY_PRESUMED 1

/* the newest OpenCE network version whose rules this build plays as a
client (DELTA_LEGACY_VERSIONS' newest row: tools/test_delta.py). A Delta
client says it in its HELLO, and a host goes by it, with the number it
announces, for what that client reads: the game settings record of 35's
layout, and whether it reports a static target's own damage
(network_rules_host_settings_size). Builds before it said their
HALO_PORT_NETWORK_VERSION there, 32 or older. */
#define DELTA_LEGACY_NEWEST 39

/* OpenCE's network versions (HALO_PORT_NETWORK_VERSION in its builds), the
first of its releases with each, and whether the version's change is one the
version before plays multiplayer with as it is (additive: messages a machine
of the older version drops) or not (breaking). A change this build plays
both sides of, by the host's number (35's longer settings record), is
additive here, and its row says what a machine of the older version can't
read. OpenCE's clients join only hosts of their exact version; ours join
every version back to the newest breaking one
(HALO_PORT_NETWORK_VERSION_MINIMUM), and our hosts announce the newest they
play as OpenCE's hosts do (HALO_PORT_NETWORK_VERSION, which may be older
than the newest row: a client plays by its host's version,
network_rules_of_version, before our hosts do). One row a version, oldest
first; the command repository's watch adds a row when it follows OpenCE's raise
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
	X(32, "build-244", additive) /* the host's policy on aiming assist for the mouse and keyboard (message 90), a client's input saying whether its player plays co-op without friendly fire (wire chupa-32a) */ \
	X(33, "build-266", additive) /* co-op's second switch in the settings' co-op byte (bit 1: the players' hits spare their AI allies), the host's to act on; in no release of OpenCE's, its testing builds alone */ \
	X(34, "build-266", additive) /* no message changed: scenery and devices that are a shield alone are static targets too, every machine runs static targets' shields, a client reports no damage a static target deals, a static target's copy made during play found by the nearest; in no release */ \
	X(35, "build-266", additive) /* OpenCE's own game types (SWAT, Infection, Gun Game, Hardcore): the gametype's options 0x2C bytes, the settings record 16 bytes longer, which a machine of 34 cannot read; this build reads both, by the host's number, and leaves a game of one it does not play; in no release */ \
	X(36, "build-266", additive) /* the host's shield effects, a new message (91) a machine of 35 drops */ \
	X(37, "build-268", additive) /* a listed game's kind (an OpenCE game type and Hardcore) in a byte that was zero: the advertisement's fourth reserved byte and the internet listing's stamp; this build shows the name in the list and marks the games it does not play; no message or record changed */ \
	X(38, "build-268", additive) /* the shield effects message takes a seventh value, 6, for each machine's own look; this build draws stock for it, as for the others */ \
	X(39, "build-294", additive) /* a dedicated server's game: the advertisement's flag 0x04 and the internet listing's flag 32; no message or record changed (first build: the oldest published release whose commit has the change, by git ancestry, 2026-10-27; older releases were removed, so an earlier one may have had it) */

/* ---------- the legacy table (port/linux/src/delta.c)

The numbers in use (delta_legacy_announce, _minimum and _maximum, in
halo_port_limits.h) are the built-in ones, widened by the newest signed
legacy table this machine has (or set by a local, unsigned override). */

/* at start-up, on the game's thread: loads the cached table, and fetches a
newer one in the background (never holding anything up) */
void delta_legacy_start(void);
/* a console with HTTPS (HALO_CONSOLE_HTTPS): one round of fetching the
signed table (the site, else GitHub), blocking, on a thread of the port's
(with CONSOLE_HTTPS_STACK_SIZE of stack to spare); 1 if a valid table came
(taken, or no newer than the one in use), 0 if none could be had */
int delta_legacy_refresh(void);
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

/* ---------- layouts: the table's facts about newer OpenCE versions

A wire from chupa-38a on reads "layouts" in its row: for a network version,
the facts a host of it sends that this build reads by data, not code (the
game settings record's size, the gametype's options' length, the distributed
messages a machine that does not know them drops, the shield effects
message's largest value; docs/delta.md, "Layouts"). A fact holds for its
version and every later one until the next fact, and is used only for
versions past DELTA_LEGACY_NEWEST (the build's own rules are the built-in
facts) and only within the limits network_rules.c sets (port/linux/game/
network_rules.h): a fact past them is not taken. Plain numbers, 0 for a fact
not said. */
#define DELTA_LAYOUT_FACTS 16
#define DELTA_LAYOUT_KIND_BYTES 32
struct delta_layout_fact
{
	unsigned short version;
	/* the game settings record's bytes at the network's 128 machines and
	128 players, and the gametype's options' bytes in it */
	unsigned short settings_size;
	unsigned short options_size;
	/* the largest value the shield effects message (91) takes */
	unsigned short shield_effects_maximum;
	/* the distributed message kinds a host of the version sends that a
	machine that does not know them drops: a bit a kind (kind 8 is bit 0 of
	byte 1) */
	unsigned char dropped[DELTA_LAYOUT_KIND_BYTES];
};

/* the table in use's facts for this build's wire (its own row's, or the
local override's), oldest first, into facts (at most count): how many */
int delta_legacy_layouts(struct delta_layout_fact *facts, int count);

/* the largest document, and signed table */
#define DELTA_LEGACY_DOCUMENT_SIZE 16384
#define DELTA_LEGACY_SIGNED_SIZE (128 + 1 + DELTA_LEGACY_DOCUMENT_SIZE)

#endif
