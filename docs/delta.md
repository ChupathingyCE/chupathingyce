# Delta: ChupathingyCE's network family

Status (October 6, 2026): in progress. Delta List, Stats, Link and Control
run today under the names in the table below; the legacy number's table and
its automation are built (see "The legacy number"), and so is the signed
legacy table's loader, waiting for its key (see "The legacy table as
config"); Delta Peer's first layer is built: its port, the advertisement's
flag, the handshake with silent fallback, platform keys, and the `platform`
and `profile` capabilities (see "Delta Peer"). The site's page for players:
https://halo.milenko.org/delta

Delta is the name for everything ChupathingyCE's machines say to each other
and to our services beyond the game protocol OpenCE defines. It has one rule
above all: **the player base stays whole.** OpenCE's game protocol is the
common ground; Delta starts beside it, and when a capability does change the
game protocol (see "An open network"), every part of Delta still falls back
silently to plain OpenCE behavior when the other side doesn't speak it.

## The family

Delta is one name with qualifiers, because its parts cross different trust
boundaries:

| Part | Between | What it is | Exists today as |
| --- | --- | --- | --- |
| **Delta Peer** | ChupathingyCE machines in one game | protocol major, capabilities, and our own messages | `delta_peer.c`, UDP port 5160 |
| **Delta List** | game or server, and the site | announcing and listing games | `/v1/announce`, `/v1/withdraw`, `/v1/games`, the console list |
| **Delta Stats** | game or server, and the site | end-of-game reports and the event stream | `/v1/report`, `/v1/client_report`; events planned |
| **Delta Control** | an admin, and a server | commands, the control API, the web page | `sv_` commands, `HALO_DEDICATED_CONTROL` |
| **Delta Link** | a player or server, and the site | linking with a code, and site-relayed control | `/v1/link`, `/v1/connect`, `/v1/claim` (profiles); servers planned |

The legacy number (OpenCE's `HALO_PORT_NETWORK_VERSION`) is not part of
Delta. Delta works around it (see "The legacy number" below).

## What lives where

```
             OpenCE game protocol (legacy, OpenCE's)
  client  <====================================================>  host
     \\    Delta Peer (our port, inside the same tunnel)           //
      \\  <==================================================>    //
       \\                                                       //
        \\  Delta List / Stats / Link (HTTPS)       Delta List / Stats / Link
         `------------------->  halo.milenko.org  <-------------'
                                      ^
                                      | Delta Link (server dials out)
                                      |
                              admin's browser  --- Delta Control --->  server
                                                  (local page or via the site)
```

The game itself stays peer to peer: one machine hosts, the others join it,
over LAN or the invite tunnel. The site is optional for play: invites,
OpenCE's public listings and LAN work without it.

## Delta Peer

### Transport

- A UDP socket of our own on a port beside the game's, so our message
  numbers can never clash with OpenCE's: a host listens on **5160**, the
  game's server port (5150) plus `DELTA_PEER_PORT_OFFSET` (10); a client
  sends from any port the system gives it. On a LAN it is plain UDP, like
  the game itself.
- The socket is opened through the game's own socket layer (`xnet.c`), so
  it goes wherever the game's traffic goes: `network.address` binds it to the
  same address as the game's sockets (several copies on one computer, each
  on its loopback alias), and on the internet the invite tunnel carries it
  exactly as it carries the game's datagrams. The tunnel learns every port
  the game's sockets are bound to (`p2p_socket_port`), and peers reach only
  those; Delta's socket is bound the same way, so the host's 5160 and the
  client's port are among them. A datagram to a peer's virtual address
  (100.64.0.0/10) is sealed onto the tunnel at once (`p2p_send_datagram`),
  and one arriving from a peer reaches the socket from the peer's stand-in
  and is reported as coming from the peer's virtual address. So Delta Peer
  on the internet is encrypted and bound to the invite's host like the game,
  with no new path through `p2p.c`.
- Halo is a star: clients talk to the host, never to each other. Delta Peer
  is the same: client to host, and the host relays what others need.
- A host that speaks Delta sets a flag in its advertisement's spare
  reserved bytes (the flags byte at `HALO_PORT_ADVERTISED_FLAGS_OFFSET`;
  0x01 and 0x02 are taken, so 0x04), only while its Delta socket is open
  (another copy on the same address may hold 5160: that game is then hosted
  without Delta, and says so). OpenCE machines ignore the bit. The game
  protocol itself is unchanged, byte for byte.

### Handshake

Right after a client joins (the legacy join, unchanged), if the host's
advertisement has the Delta flag:

1. Once the host has given the client its machine index (the client's
   pregame), the client sends **HELLO**: Delta major (in the header), its
   capability set, its build (version string), its platform key, its machine
   index, the legacy number it runs and its legacy table's serial. It says
   HELLO again every 500 ms until answered.
2. The host answers **WELCOME**: its major, its capability set, the
   intersection both will use, its build, its platform key, its own machine
   index and its legacy table's serial. A host with a different major
   answers **LEGACY** and nothing else is said. The host only answers a
   machine of its game: the HELLO's address must be that machine's.
3. No answer within 4 seconds means legacy only, for the rest of that game.
   The game is never held up waiting: the handshake runs beside it from the
   network's idle, and nothing in the game waits on its result.
4. With `profile` agreed, the client sends **PROFILE**; the host then sends
   every Delta client a **ROSTER** (below) whenever the room changes (at most
   every 250 ms) and every 5 seconds. Leaving, either side says **BYE**.

The handshake is per client. A game can mix Delta clients, OpenCE clients
and Delta clients with different capability sets; each pair uses what it
shares.

### Versioning

- **Delta major**: a small integer, raised only for a breaking change to
  Delta Peer's own framing or handshake. Different majors fall back to
  legacy.
- **Capabilities**: one bit per optional feature, never reused. Adding a
  feature adds a bit, without raising the major. A machine ignores bits it
  doesn't know.

### Capability registry (first draft)

| Bit | Capability | What it carries |
| --- | --- | --- |
| 0 | `platform` | each player's platform, for the scoreboard icons |
| 1 | `profile` | player ID and profile revision; the profile itself comes from the site |
| 2 | `server_messages` | a host's messages to players: welcome, notices, "next map" |
| 3 | `chat` | text chat between Delta players |
| 4 | `ce_maps` | Halo PC map identity (name and hash), so a client knows what it needs |
| 5 | `md_maps` | the same for HaloMD maps |
| 6 | `coop` | network co-op extensions beyond OpenCE's |
| 7 | `ai_sync` | AI sync extensions |
| 8 | `vote` | map and game type votes |
| 9 | `console_slots` | retired before use: a console's slots are its platform key's limits (below). Never set, never reused |

The registry lives in the repository next to the compatibility table and is
the single source for the bit numbers. Built so far: `platform` and
`profile`. Their values are claims, shown as claims, never used for game
state:

- **`platform`**: each machine's platform key (below), and so each player's
  platform: `delta_peer_player_platform(player)` for the scoreboard's icons
  (from the player's `machine_index`), unknown for an OpenCE machine.
- **`profile`**: a machine's player ID (the game list's, 16 bytes) and its
  profile revision (0 until the game knows it); the profile itself is the
  site's. Opt-in: a copy shares its own only with `network.share_profile`,
  since the ID is the same in every game.

Machines share what both sides of each pair agreed to: the host puts a
machine's platform or profile in a client's roster only if both that machine
and that client agreed to the capability.

### Room-wide capabilities

`delta_peer_room_has(capability)` says whether every machine in the game has
the capability now: the host's own set, ANDed with each client's agreed set,
and nothing for a machine without Delta (so one OpenCE machine turns every
room-wide capability off until it leaves). The host computes it; clients
read it from the roster. A room-wide capability is on only while this is
true.

### Platform keys

Every machine (Windows, macOS, Linux, Android, Steam Deck, the dedicated
server, and later the Xbox, 360, Wii U and Switch) sends a **platform key**
in HELLO and WELCOME: 8 bytes, fixed, readable in plain C89 with no
allocation (Warthog builds with MSVC 7.1):

| Byte | Field | |
| --- | --- | --- |
| 0 | platform | the registry below |
| 1 | version | 1; a newer version's key is read for the fields this one knows |
| 2 | flags | 0x01 opted in (its player turned its caveats off), 0x02 a dedicated server |
| 3 | host_players | the most players a game it hosts can have |
| 4 | join_players | the most players of a game it joins, by default |
| 5 | join_players_opt_in | the most it joins with its player's opt-in |
| 6 | memory_class | log2 of its megabytes: 6 for 64 MB, 7 128 MB, 8 256 MB, 9 512 MB, 10 1 GB, 11 2 GB, 12 4 GB...; 0 unknown |
| 7 | reserved | 0 |

The platform registry (`enum delta_platform`, one byte, never reused):
0 unknown, 1 `pc_windows`, 2 `pc_macos`, 3 `pc_linux`, 4 `android`,
5 `steam_deck` (Linux with Steam's `SteamDeck=1`), 6 `xbox`, 7 `xbox360`,
8 `wiiu`, 9 `switch`.

### Platform policy

Console builds play with PC builds, with caveats Delta controls. The
caveats are policy, one table in `delta.h` (`DELTA_PLATFORM_POLICY`), the
defaults every machine's key starts from:

| Platform | Hosts at most | Joins at most | Opted in | Memory | Co-op |
| --- | --- | --- | --- | --- | --- |
| PC (Windows, macOS, Linux) | 128 | 128 | 128 | 12 (4 GB) | yes |
| Steam Deck | 128 | 128 | 128 | 14 (16 GB) | yes |
| Android | 128 | 128 | 128 | 11 (2 GB) | yes |
| unknown | 128 | 128 | 128 | 0 | yes |
| Xbox | 16 | 16 | 128 | 6 (64 MB) | no |
| Xbox 360, Wii U, Switch | 16 | 16 | 128 | 9, 10, 12 (placeholders until their ports play) | yes |

Co-op: a host offers no network co-op (the Map screen's campaign levels
refuse, and say why in the log) while a machine of a platform without it is
in its game, nor on such a platform itself (`delta_peer_room_coop`). Only
the original Xbox is kept out today.

A host protects the weakest machine of its game: it takes no more players
than the smallest of its own hosting limit and every Delta machine's join
limit (`delta_peer_room_limit`, applied where the game counts a free
player slot, so the advertisement shows the game full). A machine's join
limit is its platform's row, or its key's own claim if lower (never more);
none if its player opted in. Without Delta (an OpenCE machine, or
`network.protocol = "opence"`) none of this applies: plain OpenCE behavior.
A host never lies about the game's size: it only takes fewer players. Players
already in a game stay when a weaker machine joins it; the host takes no
more until the game is back under the limit (a weaker machine's own build
should not join a game already above its limit: Warthog's side).

Overrides, on purpose:

- `network.platform_limits = "off"` on the weaker machine sets its key's
  opted-in flag: it accepts whatever the host runs. (You can roast your Xbox
  with 128 players if you want.) The default is `"on"`.
- `network.host_platform_limits = false` (`HALO_NET_HOST_PLATFORM_LIMITS` on
  a server) has a host ignore the caveats, co-op's included, for testing.

The signed legacy table is to carry a `platform_policy` section that tunes
the rows without a release; the hook is `delta_peer_platform_policy()` in
`delta_peer_game.c` (not built yet: the defaults stand).

### Wire format

Little-endian. Every datagram is a 12-byte header and a payload, at most
1200 bytes in all (under the game's 1264 and the tunnel's 1400). The header
is the same in every major, so a machine of another major can always be
answered with LEGACY:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | magic, `"DP"` (0x44 0x50) |
| 2 | 1 | Delta major (1) |
| 3 | 1 | type |
| 4 | 2 | payload length: the datagram's size less 12, exactly |
| 6 | 2 | reserved, 0 |
| 8 | 4 | session: the client's, random and never 0, chosen per handshake; every message of the session carries it |

Messages (a type number is never reused; unknown types are ignored; a
payload longer than its fields is read for the fields this version knows, so
a later version may append):

| Type | Name | Way | Payload |
| --- | --- | --- | --- |
| 1 | HELLO | client to host | capabilities (4), legacy number (2), machine index (1), build length (1, at most 31), legacy table serial (4), platform key (8), build (printable ASCII) |
| 2 | WELCOME | host to client | host's capabilities (4), agreed (4), legacy number (2), host's machine index (1; 255 none), build length (1), legacy table serial (4), platform key (8), build |
| 3 | LEGACY | host to client | none (the header's major is the host's) |
| 4 | ROSTER | host to client | room capabilities (4), room's player limit (1), reserved (3), entry count (1, at most 32), reserved (3), then 36-byte entries: machine index (1), flags (1: 0x01 Delta, 0x02 platform key present, 0x04 profile present), reserved (2), agreed capabilities (4), platform key (8), profile revision (4), player ID (16) |
| 5 | BYE | either | none |
| 6 | PROFILE | client to host | profile revision (4), player ID (16) |
| 7 | TABLE | either | serial (4), the signed table's whole size (4, at most 16513), offset (4, a multiple of 1024), length (2: 1024, or the rest), reserved (2), the piece's bytes |
| 8 | TABLE_HAVE | either | serial (4: 0 the built-in table, 0xFFFFFFFF takes no tables) |

A roster covers every machine of the game, in as many datagrams as it takes
(32 machines each). A client forgets a machine the roster has not named for
12 seconds, or that has no players.

Limits and timeouts (`delta_peer.h`):

| | |
| --- | --- |
| HELLO resent | every 500 ms |
| handshake given up (legacy only) | 4 s |
| roster | on change (at most every 250 ms) and every 5 s |
| a new session from one machine | at most one a second |
| messages a host takes from one machine | 10 a second, 20 at once |
| messages a client takes from its host | 50 a second, 100 at once |
| datagrams read a frame | 64 |
| legacy table: first pass after the handshake | 2 s |
| legacy table: a piece to one machine | at most every 200 ms (5 a second) |
| legacy table: passes to one machine | 3 a session, the next a minute after one ends |
| legacy table: signature checks of one machine's tables | 1 a minute |
| legacy table: a host sending to machines at once | 4 |

Who is heard: a host reads a datagram only from an address of a machine of
its game (anything else is dropped before parsing), and ties a HELLO to the
machine of its address and machine index; a client reads only its host's
address and port, of its session. Everything else is dropped and counted.
`port/linux/tests/delta_test.c` (unit tests and a seeded random-input test of
every parser and both sessions) and `delta_fuzz.c` (libFuzzer) run in CI
(`tools/test_delta_peer.py`).

### The protocol setting

`network.protocol` in config.toml (`HALO_NET_PROTOCOL`):

- `"auto"` (the default): the behavior above.
- `"opence"`: Delta Peer off entirely: no socket, no flag, no handshake; the
  machine is an OpenCE machine to everyone.
- `"delta"`: for now the same as auto; it marks the intent. The Delta-only
  rooms and the listings' protocol labels come later (see "The protocol
  choice").

### The legacy table's relay

HELLO and WELCOME carry each side's legacy table serial (0: the built-in
table). The side with the newer serial sends its signed table (as
`delta_legacy_signed()` gives it) in TABLE pieces of 1 KB, one transfer at a
time to a machine, paced at five pieces a second, under the host's ten
messages a second from a machine. The receiver puts the pieces together (one
table coming in at a time; another machine's waits until it has been quiet
for 5 s) and hands the whole to `delta_legacy_offer()`, which checks the
signature and the document as it does every other copy, and takes it only
if it is newer; it says its serial then in TABLE_HAVE, and again whenever
its serial changes (a table from Delta List mid-game is passed on the same
way). The relay needs no trust in the relay: only a table signed with the
built-in keys is taken.

Bounded: the first pass waits 2 s after the handshake; a machine is sent at
most three passes a session, the next a minute after one ends; its tables
are checked at most once a minute; a host sends to at most four machines at
once. A machine that takes no tables (a local table in use, or a build with
no key: `delta_legacy_relay()`) says TABLE_HAVE 0xFFFFFFFF after the
handshake, is sent none, and sends none. `delta_peer_game.c` wires the
session to `delta_legacy_serial()`, `delta_legacy_signed()` and
`delta_legacy_offer()`.

### LAN superset (planned)

An idea of thelinkin3000's. System link uses fixed ports (5150 and 5151),
so two hosts can't share one address on a LAN: the second can't bind 5150,
and plain OpenCE or Xbox clients would not find it anyway. A superset of
system link, never a replacement:

- A Delta host still listens on the standard port when it can, so OpenCE
  and Xbox clients see it as today, unchanged.
- If the standard port is taken (another instance on the same machine), it
  binds another free port for the game (and Delta Peer's beside it) and is
  reachable only by Delta clients.
- Delta clients find such hosts through a **Delta LAN announcement**: a
  small datagram on a port of Delta's own (planned 5161), broadcast like
  the game's advertisement in answer to a client's search, carrying the
  game's actual port, its Delta Peer port and the advertisement's fields:

  | Offset | Size | Field |
  | --- | --- | --- |
  | 0 | 12 | Delta Peer's header (type: a new LAN announcement type) |
  | 12 | 2 | the game's port |
  | 14 | 2 | the Delta Peer port |
  | 16 | ... | the game's advertisement (name, map, players, flags), as OpenCE's |

- A Delta client merges both lists: games heard on the standard
  advertisement, and games heard only through the Delta announcement (shown
  the same; one heard both ways is one game). Joining one on another port
  is the same join, to that port.
- Non-Delta clients see only the games on the standard port, as today.

Nothing of this is built; standard system link is unchanged. The code's
hook is the host's port choice in `delta_peer_game_host_frame` (5160 today,
and a port taken means "no Delta" rather than "another port").

### Security

- On the internet, Delta Peer is inside the tunnel, so it's encrypted and
  bound to the invite's host, like the game. On a LAN it is not, like the
  game.
- Nothing on Delta Peer is trusted for game state or stats. The host stays
  authoritative, as in the legacy protocol: a client's claim (its platform,
  its profile) is shown as a claim, and stats come from what the host saw.
- Every message has a size limit and is parsed as hostile input, to the
  same standard as the rest of the network code.

## The legacy number

OpenCE's number goes in the advertisement, and an OpenCE client joins only a
host with the same number. OpenCE raises it often, usually for additive
changes (a message older machines drop). Delta handles it with a
compatibility table and automation, so it stops being hand work.

### The compatibility table

Built: `port/linux/include/delta.h`, `DELTA_LEGACY_VERSIONS`, one row per
OpenCE network version with its first build and whether it is additive or
breaking:

```c
#define DELTA_LEGACY_VERSIONS(X) \
	X(11, "build-76", breaking) /* the gametype's PC options in the settings record */ \
	X(12, "build-118", additive) /* network co-op */ \
	...
	X(16, "build-125", additive) /* co-op: every machine stays on the host's BSP */
```

`tools/test_delta.py` (in CI) checks `halo_port_limits.h` against it: hosts
announce the newest version, and clients join back to the newest breaking
one. The numbers stay literal in `halo_port_limits.h`, which the command
repository's tools read.

Planned next, from it:

- the client's accepted range: the newest version, back to the last
  `breaking` row (`HALO_PORT_NETWORK_VERSION_MINIMUM` and `_MAXIMUM`);
- the number hosts announce: the newest row whose `first_build` OpenCE has
  released;
- the browser's and the broker listings' accepted range;
- the README's "Compatible with" line and the command repo's manifest.

Known history, from `port/linux/NETCODE.md` and OpenCE's commits:

| Version | First build | Change | Kind |
| --- | --- | --- | --- |
| 1 to 9 | | joins in progress, player slots, PAL maps, corrections, host's rules, cheaters, Discord user, hardware id | breaking (each) |
| 10 | build-73 | players' pings for the scoreboard | additive over 9 |
| 11 | build-76 | the gametype's PC options in the settings record | breaking |
| 12 | build-118 | network co-op | additive |
| 13 | build-119 | co-op extra enemies | additive |
| 14 | build-123 | co-op devices' positions, units opening and closing | additive |
| 15 | build-124 | co-op allegiances, loading zones, falling players | additive |
| 16 | build-125 | co-op: every machine stays on the host's BSP | additive |

### Automation

Built (command repository, `watch.yml` and `tools/follow.py`), running every
half hour since October 5, 2026; it followed versions 15 and 16 that day.
The cross-play test in step 4 is not built yet: until it is, the classifier
alone decides, and anything it can't call additive goes to a person.

1. **Watch.** The command repo's watch workflow sees an OpenCE release that
   changes the number.
2. **Classify.** A script diffs it against the previous build and proposes
   *additive* (new message kinds or enum values appended, new handlers,
   nothing changed in encoders, struct layouts, limits or the settings
   record) or *review* (anything else).
3. **Raise.** It pushes a branch to chupathingyce with the new number, the
   join range, the table row, the README line and the next patch version.
4. **Verify in CI:** golden packets of every listed version decode with our
   code, and CI builds OpenCE at that commit and plays a loopback game
   against ours in both directions, checking joins, kills, scores and
   positions.
5. **Ship.** Additive and green: main moves to the branch, a release goes out
   (clients update themselves), and the [D] servers and Tex roll to it when
   they're empty. Otherwise nothing ships, and Donut posts to #upstream-watch
   for a person.

## Delta List

What exists today, named: a host (ours, or a server) announces its game to
the site and withdraws it; clients and the console read the list. Delta's
additions:

- the host's Delta flag and capability summary in the announcement, so the
  list can show "ChupathingyCE" games, Halo PC maps, co-op and so on;
- the legacy number as announced, and the range a client accepts, so the
  list only offers games a client can join;
- the plain-text console list keeps its format; new fields go only at the
  end of a line, never changing existing ones.

## Delta Stats

- **Reports** (exists): end-of-game totals, with the host, verified and
  player tiers.
- **Events** (planned): a host's per-game event log (kills with weapon and
  position, accuracy, objectives, sessions, ping, server health, moderation
  and anti-cheat flags), sent once at game end. Never IP addresses; players
  are identified by name and hashed hardware id.
- **Trust:** our own [D] servers are authoritative. Other servers are "host"
  tier unless an admin links them (Delta Link) and the site decides to
  trust them.
- The event schema has its own version number, independent of everything
  else.

## Delta Control

What exists today: the `sv_` command table, the server's console, its
control API (`HALO_DEDICATED_CONTROL`, off by default, localhost only), and
the web page on it. Planned: playlists and game types as files, persistent
settings. Delta Control's rule: only the command table runs, never a shell,
and every action is audited.

## Delta Link

- **Players** (exists): Link Profile ties a game install to a site profile
  with a code (`/v1/link`, `/v1/connect`, `/v1/claim`).
- **Servers** (planned): `sv_link` shows a code; entered on a profile, it
  ties the server to that account. The server then keeps one outbound
  connection to the site, so no ports are opened. Over it the site relays
  Delta Control commands, signed with the site's key and limited to the
  command table, and the server sends status, logs and Delta Stats events.
  Off by default; `sv_unlink` on the server always wins; controlling a
  server from the site needs the site's two-factor sign-in; both ends log
  every command.

## How it meets everything else

- **OpenCE clients and hosts:** see only the legacy protocol and number.
  Nothing changes for them.
- **The invite tunnel (`p2p.c`):** carries Delta Peer's socket like the
  game's own (it is opened through the game's socket layer), inside the same
  encryption.
- **The dedicated server:** a host that speaks Delta Peer, announces through
  Delta List, reports through Delta Stats, and is run through Delta Control
  and Delta Link.
- **Warthog (Xbox):** speaks the legacy protocol with OpenCE's layout (the
  cross-play work) and, later, Delta Peer with its platform key, so hosts
  keep games to its 16 players unless its player opts in.
- **Android and the 64-bit builds:** the same Delta as every other build.
- **The site:** the other end of Delta List, Stats and Link; never in the
  path of a game.

## Each part's versioning

| Part | Versioned by |
| --- | --- |
| Legacy number | OpenCE; matched through the compatibility table |
| Delta Peer | Delta major plus capability bits |
| Delta List, Stats, Link (HTTP) | the path's `/v1`; a breaking change gets `/v2` beside it |
| Delta Stats events | the event schema's own version |
| Delta Control | the API's `/v1`; the web page ships with the server |

## Later

- **Per-peer announcing.** A host compatible with several legacy numbers
  could give each joining machine its own number in the advertisement it
  sends it through the tunnel, so OpenCE players on different builds share
  one [D] server. An experiment for our servers first; it must never claim
  more than the table allows.
- **Proposing capabilities upstream.** OpenCE's exact match splits its own
  players on every raise. Running Delta first makes the proposal concrete.

## The legacy table as config

OpenCE raises its number often, mostly for changes older machines can live
with. Following it should not take a release each time, so the table our
builds check is config, not code: signed, served by Delta List, and passed
peer to peer over Delta Peer.

### What it says

One document, JSON, signed with ChupathingyCE's Ed25519 key (kept in CI;
the public key is built in and published, so anyone building on Delta can
check it too):

```json
{
  "delta_legacy": 1,
  "serial": 42,
  "issued": 1791331200,
  "wires": {
    "chupa-20a": { "announce": 21, "minimum": 11, "maximum": 21 }
  },
  "disabled_capabilities": [],
  "platform_policy": {}
}
```

- **`delta_legacy`**: the document's format, 1. A build drops a table of a
  format it doesn't read.
- **`serial`**: 1 to 4294967295; each table published gets a higher one.
  **`issued`**: when it was made (Unix seconds), for people; optional.
- **Rows by wire, not by build.** Each build has a wire ID (`DELTA_WIRE` in
  `delta.h`, `chupa-20a` today): the revision of the game protocol it
  actually speaks. A row says which OpenCE numbers that wire was proven to
  play with: the number a host announces, and the range of hosts' numbers a
  client joins (each 1 to 65535, `minimum <= announce <= maximum`). An old
  build only follows numbers verified against its own wire, so a raise that
  needs new code (OpenCE's 19 and 20 were) never reaches a build that lacks
  it. A build ignores every other wire's row; a table without a row for its
  wire leaves it on its built-in numbers.
- **Rows are added only by CI.** When OpenCE raises, CI runs the cross-play
  test of each wire still in use against the new OpenCE build; a pass adds
  that number to the wire's row and publishes a new serial. A fail means a
  release (merge the code, new wire ID), as today.
- **`disabled_capabilities`**: the kill switch for a Delta capability found
  unsafe, until a fixed build ships: names from the capability registry
  (`platform`, `chat`, ...). Names a build doesn't know are ignored. Delta
  Peer never offers, agrees to or uses a capability the table in use turns
  off: not in HELLO or WELCOME, not in the room's set, and a table that
  turns one off mid-game takes it out of the room at the next roster.
- **`platform_policy`** (reserved, optional): per-platform limits Delta Peer
  reads, such as how many players a host or a joining machine of that
  platform may have, keyed by platform name (`xbox`, `xbox360`, `wiiu`,
  `switch`, `android`, `pc_windows`, ...): an object of objects of small
  whole numbers (0 to 4096; names of lowercase letters, digits and `_`). The
  Delta Peer work owns the platform names, the limits and their built-in
  defaults. A table may tune them without a release, but every value is
  **bounded by the build's own hard limits**: a build clamps it to the range
  it was built to allow, so a table can relax a limit only as far as the
  built-in default and tighten it only within the documented bounds. Until
  Delta Peer reads it, builds pass over it.
- Unknown fields are skipped, so later fields don't break older builds. A key
  twice in one object is a broken table.

The **signature** is Ed25519 (RFC 8032) over the document's exact bytes,
written as 128 hex digits. The servers keep the two as files side by side:

| File | What it is |
| --- | --- |
| `legacy.json` | the document, as signed (at most 16384 bytes) |
| `legacy.json.sig` | the signature's 128 hex digits and a line feed |

(The site keeps them in its data folder as `delta_legacy.json` and
`delta_legacy.json.sig`, and serves them as they are.) Between machines, and
in the cache, the two travel as one **signed table**:
the signature's 128 hex digits, a line feed, then the document's bytes.

`tools/delta_table.py` makes, signs and checks them:

```
delta_table.py make --serial N [--from legacy.json] [--out legacy.json]
delta_table.py sign --key KEY.pem legacy.json        (writes legacy.json.sig)
delta_table.py verify [--public-key HEX] legacy.json
delta_table.py keygen --out KEY.pem
```

`make` writes this build's wire and numbers (`delta.h`, `halo_port_limits.h`)
and, with `--from`, keeps the other wires' rows of the table before it.

### The key

`port/linux/src/delta_key.h` holds the public keys builds accept (several,
for rotating): the primary key, which signs every table, and a recovery key
kept apart for a lost or leaked primary. Both are published, with the CI
secrets that hold their private halves, as `keys/delta.pub.json` in
ChupathingyCE's command repository:

| Key | Public key (hex) |
| --- | --- |
| primary | `d51bd85346bf889b6bc5aa21dcc7a43488657b64f2432e17f6e0e7fe976933ce` |
| recovery | `97cde84d8b9b6ba8a275d412b49422d4c07970653ee53ae54285e96640a5e456` |

A build with no key (all zeros) fetches no table and accepts none: it
plays with its built-in numbers (and a local override).

### How it travels

- **Built in:** each build carries the table as it was at release. That is
  the floor: play never depends on reaching anything.
- **The cache:** the newest signed table taken is kept as
  `delta_legacy.signed` in the save root and read at start, checked again
  like any other copy.
- **Delta List:** clients and servers fetch it in the background at start
  and every four hours (thirty minutes after a failure), from
  `network.browser_url`'s `/v1/delta/legacy` and `/v1/delta/legacy.sig`
  (the two files, as they are). Nothing waits for it: until it arrives the
  game uses the cache or the built-in numbers. With `network.browser_url`
  empty (no list server) nothing is fetched at all.
- **GitHub, if the site can't be reached or gives no valid table:** the same
  two files, committed by CI to the `delta-table` branch of ChupathingyCE's
  repository and read from
  `raw.githubusercontent.com/ChupathingyCE/chupathingyce/delta-table/legacy.json`
  (and `legacy.json.sig`). A branch of its own keeps the table's updates out
  of main's history. Both copies are signed with the same key, so neither
  host is trusted; the newest serial of whatever is reached wins. While
  neither has a table (both answer 404), the log says so once a
  run, not at every retry.
- **Delta Peer:** in the handshake each side says its table's serial; the side
  with the newer one sends its signed table. A machine that never reaches the
  site still gets it from the first host or client that has it. It is
  signed, so relaying it needs no trust in the relay.

### Checks

- The size is checked first (16384 bytes of document), then the signature,
  and only then is the document read, by a strict parser (bounded nesting,
  whole numbers only where numbers are read, no trailing bytes, no repeated
  keys) that skips what it doesn't know.
- **Serials only go forward.** An older or equal serial is ignored, so an old
  copy can't be replayed to roll a machine back.
- A row may only **widen** the built-in one for the same wire: announce at
  least the built-in number, minimum at most the built-in minimum, maximum at
  least the built-in maximum. Nothing the network sends can make a build
  stricter than it shipped; a table whose row narrows is dropped whole.
- A table that doesn't verify is dropped quietly (one line in the log, never
  an address); nothing is shown to the player.

### Local override

`network.legacy_table` in config.toml (`HALO_LEGACY_TABLE` for servers)
names an unsigned file in the same format (beside config.toml unless a full
path), for testing and for an admin who knows better. Its row for the build's
wire is used as it is (it may narrow too); its serial is not checked. It logs
a warning at start, and while it is set no signed table is fetched, taken or
relayed. A file that can't be read, or has no row for the wire, leaves the
built-in numbers in use (and says so).

### In the code

The constants became calls (`delta_legacy_announce()`,
`delta_legacy_minimum()`, `delta_legacy_maximum()`, in
`halo_port_limits.h`) at the places the number is used: the listing
(`p2p_lobby.c`), the advertisement (`network_server_message_handler.c`), the
join check (`network_client_manager.c`), the browser (`browser.c`) and what
the server and reports print. `HALO_PORT_NETWORK_VERSION` stays as the
built-in announce and the wire's own number, so OpenCE's tools that read it
keep working.

`port/linux/src/delta.c` does the rest (declared in `delta.h`):

| Call | What it does |
| --- | --- |
| `delta_legacy_start()` | at start-up: the override or the cache, and the fetching thread |
| `delta_legacy_serial()` | the serial of the signed table in use; 0 for none or an override |
| `delta_legacy_signed(buffer, size)` | the signed table in use, for Delta Peer to send |
| `delta_legacy_offer(table, size)` | a signed table from a peer: checked, taken if newer |
| `delta_capability_disabled(bit)` | the kill switch |
| `delta_legacy_override()` | whether a local, unsigned table is in use |
| `delta_legacy_relay()` | whether Delta Peer relays tables: not with an override, nor with no key |

`tools/test_delta.py` checks the tool and builds `delta.c` with a test key
(`tools/delta_check.c`) to check what the game takes and drops.

## An open network

Delta is public: this document is its specification, and anyone may build a
client, server or tool that speaks it. Nothing in it depends on being
ChupathingyCE, and nothing is kept secret but signing keys.

Delta may grow past OpenCE's game protocol: a capability can change the game
protocol itself (co-op, CE map identity, sync), not only add messages beside
it. One rule keeps the player base whole: **every Delta game falls back to
OpenCE's protocol**, per connection, for a machine that doesn't share the
capability. A room-wide capability (one every machine must agree on) is on
only while every machine in the game has it.

### The protocol choice

A host chooses, in Server Setup (and a dedicated server in its settings):

| Choice | Who joins | Shown with it |
| --- | --- | --- |
| **Auto** (the default) | everyone: OpenCE machines on OpenCE's protocol, Delta machines with what they share | room-wide extras are on only while everyone has Delta |
| **Delta only** | Delta machines | OpenCE players can't see or join the game |
| **OpenCE only** | everyone, plainly | no Delta extras |

Browsers show every game with its protocol; a join takes the best protocol
both sides share. Nobody is told "incompatible" when a fallback exists.

## Security

Delta is open, so it is built for hostile peers, not friendly ones, without
asking players for anything:

- **Untrusted by default.** Every message is parsed as hostile input: sizes
  bounded before reading, counts capped, strings checked, and every parser
  fuzzed in CI. The host stays authoritative; a peer's claim (platform,
  profile, build) is shown as a claim.
- **Encrypted where the game is.** On the internet Delta Peer runs inside the
  invite tunnel (encrypted, bound to the invite's host); listings are signed
  by the host's key, as OpenCE's are.
- **Bounded cost.** Rate limits and work budgets per peer (signature checks,
  handshakes, relayed messages), so a peer can slow only itself.
- **Signed policy, offline floor.** The legacy table and a per-capability
  kill switch come signed from Delta List; the built-in table is the floor,
  and being offline never stops play.
- **No surprises for players.** No accounts needed to play, no prompts in the
  way; a profile is opt-in. Addresses are never shown (see the privacy
  rules); moderation (kick, ban, profile-linked moderators) is the host's.
- **Reviewed upstream changes.** An OpenCE raise is merged as code, checked by
  the cross-play test and reviewed before its number is announced; the
  number alone is never matched blind.

## Phases

1. This document, the compatibility table and the capability registry.
   Done: `delta.h` and `test_delta.py`.
2. The announced number and join range checked against the table. Done;
   generating them from it comes with the signed table.
3. The watch workflow's classifier (done); following becomes a reviewed
   merge of OpenCE's code gated by CI's cross-play test (next), starting
   with OpenCE's network versions 19 and 20.
4. Delta Peer: the port, the flag, the handshake with fallback, fuzzing,
   platform keys and their policy, and its first capabilities (`platform`,
   `profile`). Built; `server_messages` next, then the LAN superset.
5. The protocol choice (Auto, Delta only, OpenCE only) and the browsers'
   protocol labels; a dedicated server's listing marks it (flag 128).
6. Moderators for dedicated servers through Delta Link profiles; Delta
   Stats events.
7. The legacy table as config. Built: wire IDs, the signed document and
   its tool, the game's loader (cache, Delta List, GitHub), the local
   override, the real keys and delivery over Delta Peer. Next: CI publishing
   after cross-play.
8. The per-peer host experiment, and the proposal to OpenCE.
