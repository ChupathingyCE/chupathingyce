# Delta: ChupathingyCE's network family

See also [Delta: compatibility](delta-compatibility.md), the design for how builds play together.

Status (October 13, 2026, for 0.7.4e): in progress. Delta List, Stats, Link
and Control run under the names in the table below; the legacy number's
table and its signed loader are built, and the cross-play gate that
publishes it runs on every OpenCE raise (see "The legacy number" and
"Following OpenCE"); Delta Peer's first layer is built: its port, the
advertisement's flag, the handshake with silent fallback, platform keys,
and the `platform`, `profile`, `ce_maps` and `moderation` capabilities (see
"Delta Peer"). Delta Stats' event log, server moderators and linking
servers to the site shipped in 0.7.1b. The site's page for players:
https://stats.chupathingy.org/delta

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
| **Delta Stats** | game or server, and the site | end-of-game reports and the event stream | `/v1/report`, `/v1/client_report`, `/v1/events` |
| **Delta Control** | an admin or moderator, and a server | roles, commands, the control API and panel, in-game moderation, the site link | `sv_` commands, `HALO_DEDICATED_CONTROL`, `moderators.txt`, `sv_link` |
| **Delta Link** | a player or server, and the site | linking with a code, and site-relayed control | `/v1/link`, `/v1/connect`, `/v1/claim` (profiles); `sv_link` (servers) |

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
         `------------------->  stats.chupathingy.org  <-------------'
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
  without Delta, and says so). OpenCE machines ignore the bit, and the
  game's own messages are not changed by it.

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
| 4 | `ce_maps` | Halo PC map identity (family, file name, size and hash) for every Halo PC family: Custom Edition, HaloMD and Halo PC retail (see "Map identity") |
| 5 | `md_maps` | retired before use: `ce_maps` carries every Halo PC family. Never set, never reused |
| 6 | `coop` | network co-op extensions beyond OpenCE's |
| 7 | `ai_sync` | AI sync extensions |
| 8 | `vote` | map and game type votes |
| 9 | `console_slots` | retired before use: a console's slots are its platform key's limits (below). Never set, never reused |
| 10 | `moderation` | a dedicated server's moderators sign in and act from the game (below, "Moderation") |
| 11 | `bomb` | Bomb game types (Bomb, Grifball, Open Pitch: [bomb.md](bomb.md)) and their player traits (TRAITS). Room-wide: a host playing one drops every machine without it |
| 12 | `ping` | the host answers Online Games' pings before joining; announced on Delta List only, never in a HELLO (see "Pings before joining") |
| 13 | `bomb_movement` | TRAITS' movement and melee traits (jump, gravity, melee reach), for a player whose machine and host both agreed to it; taken, never set yet |
| 14 | `bomb_traits` | TRAITS at 22 (Grifball test builds 2 and 3 sent it as 18, chat's CHAT_CONFIG). Room-wide with `bomb`: a host playing a Bomb game type drops every machine without both; taken, never set yet |
| 15 | `team_change` | changing teams during a game: a client asks its host (TEAM_REQUEST), the host decides and answers (TEAM_RESULT); see "Team changes" |

The registry lives in the repository next to the compatibility table and is
the single source for the bit numbers. Built so far: `platform`, `profile`,
`ce_maps`, `moderation` (see "Moderation"), and `chat` and `server_messages`
(see "Chat"). The first two's values are
claims, shown as claims, never used for game state:

- **`platform`**: each machine's platform key (below), and so each player's
  platform: `delta_peer_player_platform(player)` for the scoreboard's icons
  (from the player's `machine_index`), unknown for an OpenCE machine. The
  scoreboard shows a logo only in a game played over Delta Peer
  (`delta_peer_game_on_delta`), and none for an unknown platform
  (`display.scoreboard_platforms`).
- **`profile`**: a machine's player ID (the game list's, 16 bytes) and its
  profile revision (0 until the game knows it); the profile itself is the
  site's. Opt-in: a copy shares its own only with `network.share_profile`,
  since the ID is the same in every game. Profiles are exchanged, but
  nothing in the game shows them yet.
- **`ce_maps`**: the game's map's identity, host to client (MAP, below), so
  a client with another file of the same name leaves rather than playing a
  map that is not the host's. See "Map identity".

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
8 `wiiu`, 9 `switch`, 10 `psp`, 11 `vita`, 12 `xbox_uwp` (the Xbox One and
Series in developer mode). 10 to 12 are ports others build on ours; their
policy rows are placeholders until those ports play, as the consoles' are.

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
| 9 | MOD_CHALLENGE | host to client | nonce (32), binding length (1, at most 64), reserved (3), binding (printable ASCII) |
| 10 | MOD_PROOF | client to host | moderator key (32), signature (64) |
| 11 | MOD_STATE | host to client | role (1), reserved (3), permissions (4), longest timed ban in minutes (4) |
| 12 | MOD_ACTION | client to host | sequence (4), action (1), target machine (1), minutes (2; 0 for ever), reason length (1, at most 63), reserved (3), signature (64), reason (printable ASCII) |
| 13 | MOD_RESULT | host to client | sequence (4), ok (1), text length (1, at most 127), reserved (2), text |
| 14 | MOD_NOTICE | host to client | kind (1: 1 warning, 2 notice), text length (1, at most 127), reserved (2), text |
| 15 | MOD_BIND | host to client | request (4), account length (1, at most 31), server name length (1, at most 31), reserved (2), account, server name |
| 16 | MOD_BIND_ANSWER | client to host | request (4), accepted (1), reserved (3), moderator key (32), signature (64) |
| 17 | MAP | host to client | family (1), flags (1: 0x01 the size and hash are the file's), name length (1, at most 63), reserved (1), file size (8: low word, then high), BLAKE2b-256 hash of the whole file (32; zeros without the flag), file name (letters, digits, `_`, `-`, `.` and space; no `..`) |
| 18 | CHAT_CONFIG | host to client | mode (1: 0 off, 1 team lines alone, 2 on), flags (1: 0x01 this player is muted, 0x02 the host filters blocked words), reserved (2), a timed mute's seconds left (4; 0 none, or until the game ends) |
| 19 | CHAT_SAY | client to host | sequence (4, above the session's last), scope (1: 0 everyone, 1 team), reserved (1), text length (1, 1 to 120), reserved (1), text (UTF-8) |
| 20 | CHAT_LINE | host to client | line (4, the host's count), kind (1: 0 player, 1 moderator, 2 server, 3 system), scope (1), speaker's machine (1; 255 none), speaker's player (1, the game's player index; 255 none), name length (1, at most 47), text length (1, 1 to 200), reserved (2), name (UTF-8), text (UTF-8) |
| 21 | CHAT_REFUSED | host to client | sequence (4, the CHAT_SAY's), reason (1: 1 chat off, 2 that scope is not taken, 3 muted, 4 too fast, 5 blocked words, 6 text that cannot be shown), reserved (1), seconds the player's chat is paused (2; with 4, 0 none: added in 0.8.0 where the field was reserved, so an older game reads none) |
| 22 | TRAITS | host to client | taken by the Bomb builds ([bomb.md](bomb.md)) |
| 23 | TEAM_REQUEST | client to host | sequence (4, above the session's last), player (1, the game's player list index), team (1: 0 red, 1 blue, 255 the other one), reserved (2) |
| 24 | TEAM_RESULT | host to client | sequence (4, the TEAM_REQUEST's), result (1: 0 changed, 1 at the next respawn, 2 the host takes none, 3 the teams would be uneven, 4 no teams, 5 too soon, 6 not now, 7 no such player of the machine, 8 already on that team, 9 the last player of their team), team (1, the player's now), seconds until the player may change again (2; with 5) |

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
| MAP | on change, and every 5 s |
| moderation: MOD_ACTION a host takes from one machine | 1 a second, 3 at once |
| moderation: signature checks of one machine's PROOF and BIND_ANSWER | 2 a second, 2 at once |
| moderation: MOD_CHALLENGE (not signed in), MOD_STATE (signed in) and a waiting MOD_BIND said again | every 5 s (MOD_STATE at once when roles change) |
| moderation: a MOD_BIND answered within | 2 minutes, else declined |
| chat: CHAT_SAY a host takes from one machine | 1 a second, 3 at once; the same line again within 10 s is too fast |
| chat: a machine's chat paused once it is too fast | 5 s (a dedicated server's `HALO_DEDICATED_CHAT_COOLDOWN`; 0 none) |
| chat: CHAT_LINE a client takes from its host | 10 a second, 20 at once |
| chat: CHAT_CONFIG | at the handshake, on a change (at most every 250 ms), and every 10 s |
| team changes: TEAM_REQUEST a host takes from one machine | 1 a second, 3 at once (and a player's changes 10 s apart: the host's rule) |

Who is heard: a host reads a datagram only from an address of a machine of
its game (anything else is dropped before parsing), and ties a HELLO to the
machine of its address and machine index; a client reads only its host's
address and port, of its session. Everything else is dropped and counted.
`port/linux/tests/delta_test.c` (unit tests and a seeded random-input test of
every parser and both sessions) run in CI (`tools/test_delta_peer.py`);
`delta_fuzz.c` (libFuzzer) runs there too where clang can link
`-fsanitize=fuzzer`, and is skipped where it can't (Apple's clang).

### Map identity

The legacy protocol names a game's map by a string alone, and two Halo PC
maps of one file name can be different maps (a map's versions, or two
authors' `bigass`). Between our machines, Delta Peer says which file the host
plays, with `ce_maps` agreed by both:

- **The families** (`halo_map_families.h`), each with the folder its files go
  in beside `maps/` (the Xbox's own): 0 the Xbox's (`maps/`), 1 Halo PC
  Custom Edition (`<file>@ce`, `maps_ce/`, with Custom Edition's
  `bitmaps.map`, `sounds.map` and `loc.map`), 2 HaloMD (`<file>@md`,
  `maps_md/`), 3 Halo PC retail (`<file>@pc`, `maps_pc/`). A family number a
  build does not know is carried as it is, and its client checks nothing.
- **Names on the legacy wire** stay OpenCE's: a host names a Custom Edition
  map `custom_maps\<file>` in the game's settings (OpenCE's build-145 form,
  network version 22), and HaloMD and Halo PC retail maps
  `maps_md\<file>.md` and `maps_pc\<file>.pc`, which OpenCE's clients do not
  have and are told they miss. Our clients read those back as `<file>@ce`,
  `@md` and `@pc`. MAP is what carries the real identity between our
  machines.
- **The host** sends MAP to each client that agreed to `ce_maps`: when the
  game's map changes, and again every 5 s (a datagram lost). An Xbox map is
  sent by name alone, without the flag: every copy of the game has the same
  Xbox maps. A Halo PC map is sent with its file's size and hash, once the
  hash is made; until then nothing is sent.
- **The hash** is BLAKE2b-256 (Monocypher's) of every byte of the file,
  made on a thread of its own (`port/linux/src/delta_maps.c`) so no frame
  waits for it, and kept by the file's path, size and modification time
  (16 files). On an Apple M4, a 17 MB map hashes in about 20 ms, so a map of
  several hundred megabytes in under a second, beside the game.
- **The client** looks for its own file of the MAP's family and name
  (`map_family_find`, in the family's folders and their fallbacks), hashes it
  the same way, and compares the size and hash once for each MAP that says
  another map. The same: logged once, and the game goes on. Different: it
  logs both sizes, tells the player (the main menu's error) that their
  `<file>.map` is not the host's and which folder to replace it in, and
  leaves the game. No such file: nothing more is said here, as the legacy
  join already tells the player the map is missing and where it goes
  (`cache_files_map_present`), or offers to download a Custom Edition map
  from the community map repositories (`port/linux/src/map_download.c`),
  checked against the version the host advertises in the legacy protocol
  (the first 32 bits of its whole-file hash since network version 29, its
  header's checksum from an older host). Once it is joined, MAP checks the
  whole file as for any other.
- **Hostile input**: a MAP is read only from the client's host, of its
  session, with `ce_maps` agreed and not turned off by the kill switch; a
  name that is not a plain file name, a Halo PC map without its hash, or
  unknown flags are refused, never mended. A host takes no MAP.
### Moderation

A dedicated server's moderators act from the game over Delta Peer (the
`moderation` capability). A host offers it only when it has moderation (the
dedicated server registers its own, `delta_moderation.h`); a game a player
hosts offers none. OpenCE players can be warned, kicked and banned like
anyone (the server acts through the game's own kick and ban), but only a
Delta client can be an in-game moderator.

- **The moderator key.** Each copy of the game makes an Ed25519 key pair
  from its player key: the seed is SHA-256 of
  `"halo-ce-universal moderator key\n"` and the 32-byte player key. The
  public half (64 hex digits) is the moderator key a server's moderators
  file names; the site makes the same one from the key a game sends it, so
  a site account's linked games give its moderator keys. The player key
  never leaves the machine but to the site.
- **Nothing is revealed until the player acts.** The host sends each client
  that agreed to `moderation` a MOD_CHALLENGE: a nonce of 32 random bytes
  for the session, and its binding. The client signs only when its player
  signs in (the Moderation screen, Y over the pause menu) or answers a link.
- **Binding.** A host on the internet binds its challenges to its identity:
  the first 32 hex digits of its invite (the hash of its tunnel key, which
  does not change while it runs). A client that reached its host through
  the invite tunnel signs nothing unless the binding is the invite it
  joined, so a server cannot pass another server's challenge on to a
  moderator and use the signature there. On a LAN a host binds nothing and
  any binding is signed (as the game itself, LAN play is not authenticated).
- **Sign in.** MOD_PROOF carries the key and its signature of
  `"delta moderation proof v1\n"`, the nonce and the binding. The host
  checks it (never a key of small order), tells its moderation which
  machine has the key, and answers MOD_STATE: the key's role (0 none, 1
  moderator, 2 admin, 3 owner), its permissions and its longest timed ban.
  A machine's key is forgotten when its session ends.
- **Actions.** MOD_ACTION is signed over `"delta moderation action v1\n"`,
  the nonce, the sequence, action, target machine, minutes and reason; its
  sequence must be above the session's last, so a replay does nothing.
  Actions: 1 warn, 2 kick, 3 ban (minutes, 0 for ever), 4 end the game, 5
  next map, 6 mute the target's chat (minutes, 0 until they leave), 7 unmute
  it; a host refuses one it does not know. Whether the key may do it
  is the host's moderation's decision (the server's permission table), and
  MOD_RESULT says what happened.
- **Notices.** MOD_NOTICE is a warning (or notice) the server shows the
  player for a few seconds.
- **Linking an account.** The server's control panel can ask a player's
  game to link to a panel account: MOD_BIND names the account and the
  server; the game asks its player (the Moderation screen); MOD_BIND_ANSWER
  carries the key and its signature of `"delta moderation bind v1\n"`, the
  nonce, the request, the answer and the binding. A bind not answered in 2
  minutes is a decline. Without Delta Peer (an OpenCE client) there is no
  link: such a player cannot be an in-game moderator anyway.

Permissions (MOD_STATE, `delta_wire.h`): 0x001 view, 0x002 warn, 0x004
kick, 0x008 ban up to the timed limit, 0x010 any ban, 0x020 unban, 0x040
map (end the game, next map), 0x080 settings, 0x100 roles.

### Chat

Text chat between Delta players, and the host's own lines (server messages),
over Delta Peer: `chat` (bit 3) carries players' lines, `server_messages`
(bit 2) the host's (CHAT_LINE of kind server or system). Each is agreed per
pair, so OpenCE machines never send or receive either, and a Delta client
that takes server messages but not chat is sent only those. Should OpenCE
ship a text chat of its own, it would be bridged here, never mixed into the
game protocol.

- **Saying.** A client sends CHAT_SAY: its scope (everyone or team) and up
  to 120 bytes of UTF-8 (80 characters). The host checks the session, the
  rising sequence, its budget (1 a second, 3 at once; the same line again
  within 10 seconds is too fast), the text (valid UTF-8 with no control
  characters, bidirectional overrides or isolates, or zero-width
  characters, and not blank), its mode and the machine's mute, then its
  own rules: a team line in a game without teams is refused (co-op: everyone
  is one team), as is an everyone line on a host set to team lines alone,
  and, with its filter on, a line with a word of the blocked names list. A
  refused line is answered with CHAT_REFUSED and its reason, and goes
  nowhere else.
- **Relaying.** The host names the speaker from its own player list (a
  client never names itself) and sends CHAT_LINE to every Delta machine
  that may hear it: everyone, or the speaker's team. A player's host is a
  listener and a speaker like any other.
- **The host's lines.** A dedicated server's `sv_say` (and its control
  panel's Say) is a CHAT_LINE of kind server, with no speaker, to every
  machine that agreed to `server_messages`. Warnings and notices stay
  MOD_NOTICE (the banner; the game also keeps them in its chat log).
- **Mutes.** A moderator mutes a player's chat (MOD_ACTION 6, `sv_mute`, the
  control panel) until they leave the game or for a while; the host stops
  relaying their lines and tells their game in CHAT_CONFIG. A player's own
  mutes are their game's alone and never sent.
- **The client checks again.** A CHAT_LINE comes only from its host, of its
  session, at most 10 a second; its text and name must pass the same text
  checks, and it is shown in the HUD's font (a character the font lacks as
  `?`), its blocked words starred out or the line hidden as the player's
  `chat.filter` says.
- **Nothing is kept.** Neither the host nor the dedicated server writes
  down what is said; the site never sees it. Only the admins' own actions
  (`sv_say`, mutes) are in the server's audit file.

The game's side is `port/linux/game/chat.c` (keys, the log, routing),
`port/linux/src/delta_chat.c` (the filter and the host's rules, tested in
`delta_test.c`) and the dedicated server's `sv_say`, `sv_chat`, `sv_mute`
and `sv_unmute` (`server/docs/admin.md`).

### Team changes

Changing teams during a team game, as Halo PC's in-game CHANGE TEAMS had it
(the Xbox let a player choose a team only in the pregame lobby), over
`team_change` (bit 15). The game protocol has no message for it, so the
ask goes over Delta Peer and the change itself through the game:

- **Asking.** A client sends TEAM_REQUEST for one of its own players (its
  index in the host's player list) and a team (or "the other one"). The
  host checks the session, the rising sequence and its budget (1 a second,
  3 at once), that the player is the machine's, and its rule, then answers
  TEAM_RESULT. A host without `team_change` (OpenCE's, an older
  Chupathingy) is never asked: the player is told the change needs a
  Chupathingy host.
- **The host's rule.** `network.team_change` for a player's host,
  `HALO_DEDICATED_TEAM_CHANGE` for a dedicated server: `balanced` (the
  default: no change that leaves one team more than the margin, 1 unless
  set, bigger than the other), `allow` or `off`; a player's changes at
  least 10 seconds apart; never the last player of a team while others
  play (a team game with everyone on one team is over). A dedicated
  server's `sv_team` moves anyone, the rule aside.
- **The change.** The host kills the player's unit as nobody's kill: no
  death, suicide or betrayal counted, no respawn penalty, no kill message;
  the player keeps their score and the team scores stay. It puts the
  player on the new team (as the game type's auto team balance does) and
  they respawn on it after the game type's respawn time. Every machine,
  OpenCE's too, takes the player's team from the unit the host spawns them
  with, so the result reaches everyone through the unchanged game protocol;
  other machines see a death of no killer ("died") and the respawn. A
  player already dead changes at once. In a round-based game type (Bomb)
  the change waits for the player's next death, so the round plays out.

The game's side is `port/linux/game/team_change.c`, its pause menu row in
all the menu sets (`port/linux/game/menu_tags.c`), and the dedicated
server's settings and `sv_team` (`server/docs/settings.md`).

### The protocol setting

`network.protocol` in config.toml (`HALO_NET_PROTOCOL`):

- `"auto"` (the default): the behavior above.
- `"opence"`: Delta Peer off entirely: no socket, no flag, no handshake; the
  machine is an OpenCE machine to everyone.
- `"delta"`: for now the same as auto; it marks the intent. Delta-only
  rooms come later (see "The protocol choice"). The browsers already label
  each listed game Delta or OpenCE by what its host announces.

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

What each side checks, since 0.7.5a:

- **Delta games go by our wire, not OpenCE's number.** A host that speaks
  Delta puts its Delta major and its wire ID (`chupa-28a`, `DELTA_WIRE`)
  in its advertisement, in reserved bytes after the port's version and
  flags (`DELTA_ADVERTISED_WIRE_OFFSET`; OpenCE's machines leave them zero
  and never read them). Our client, unless set to `network.protocol =
  "opence"`, joins a host of its own major and wire whatever OpenCE number
  either one announces, and refuses a host of another wire with a message
  naming both. A host of another major, or one built before the wire was
  advertised, is joined by its number, as before. The Online Games list
  keeps a Delta host whose number is out of range, so that it can be joined
  this way.
- **OpenCE hosts and clients are unchanged.** Our client joins an OpenCE
  host by its number: back to the newest breaking version, up to the
  maximum. Our host announces one number. It can't let in OpenCE clients
  of other numbers: an OpenCE client compares the advertised number with
  its own and joins only on an exact match, before it sends anything, and
  its join request doesn't say its number. So an OpenCE client of a newer
  number sees our games only once we announce that number (a signed table
  or a release, below).
- **The next number is presumed additive.** Our client joins hosts of the
  next OpenCE number (`DELTA_LEGACY_PRESUMED`, 1, past the one announced)
  from the day OpenCE raises it, before anything is followed. The built-in
  maximum is at least the built-in number plus one, and a signed table's
  row gets the same: a row that announces 30 joins up to at least 31,
  whatever its `maximum` says. The rule for such a game: what the client
  doesn't know it drops, as with any additive raise, and content it can't
  match ends its join instead of playing on: a map of another version
  (a Custom Edition map's version is in the game record) is refused as a
  missing map, before the game loads. A raise that changes an existing
  message, its layout or what it means, is breaking, not additive: it gets
  a `breaking` row and a release. Builds already out can't be narrowed by a
  table, so until they update they would still join such a host; that is
  the one risk of the window, the same the built-in maximum has carried
  since it was set two past the number (30 for 28). The join logs
  "presumed additive", so a report from such a game says so.

### Wire compatibility ranges

Since 0.8.0a a wire is not an island. `DELTA_WIRE_COMPATIBILITY` in
`delta.h` gives each wire the oldest wire it plays the game with, without a
desync and without losing what the older wire has: a feature the older wire
lacks is dropped or unused with it, and content it can't play ends the join
cleanly. `chupa-32a` plays with `chupa-28a` and newer (OpenCE's 30 to 32 are
additive: a machine of 29 drops message 90 and ignores the co-op input byte;
31's keep-alive, stamp window and stream-only messages were already in 29);
`chupa-29a` plays with `chupa-28a`. A row is written with its release and
never changed: a build can't learn of a newer one, so a host also says its
range in its advertisement.

**The advertisement.** A Delta host still writes its major and a wire in the
old slot (`DELTA_ADVERTISED_WIRE_OFFSET`), but the wire there is its *oldest
compatible wire that a released client knows* (from
`DELTA_WIRE_FIRST_ADVERTISED`, `chupa-29a`, the first wire advertised), so a
released client, which compares the slot for an exact match, joins. After the
slot, in six reserved bytes that were free (`DELTA_ADVERTISED_RANGE_OFFSET`,
`DELTA_ADVERTISED_RANGE_SIZE`), come the range bytes:

| Byte | Meaning |
| --- | --- |
| 0 | layout version, 1 |
| 1, 2 | the host's own wire: its number and its letter (32, 'a') |
| 3, 4 | its oldest compatible wire: number and letter (28, 'a') |
| 5 | a check: 0xA5 xor the five bytes before |

All zero when absent. OpenCE's machines, and released builds, leave and
ignore them.

**The client's decision** (`delta_compat_decide`): it joins when the host's
own wire is in the client's range (the client's oldest to its own), or the
client's wire is in the host's range; without valid range bytes, when the old
slot's wire is the client's own (the rule since 0.7.5a). It refuses, naming
both wires, only when neither holds. A host of another major, or one without
the slot, is joined by its OpenCE number, as before.

**The cost of one slot.** A released client that checks the slot exactly sees
the host's oldest compatible wire, `chupa-29a` for a 0.8.0a host: 0.7.5
players join it, and 0.8.0's (`chupa-32a`, an exact-match client, out for a
few hours before 0.8.0a) see another wire until they update. 0.8.0a and later
read the range bytes and join both. The 32 row's first OpenCE build is
build-244.

### Following policy

ChupathingyCE has its own code and roadmap. Since October 9, 2026 it takes no
code from OpenCE; it follows OpenCE's **network version**, so players of both
keep sharing games.

- **Delta players aren't held up.** Between our own builds the wire
  decides, not OpenCE's number, so a raise never splits our players from
  each other; a release that changes our game protocol gives itself a new
  wire ID, and builds of an older wire are told to update. Following
  OpenCE only decides which OpenCE players see our games.
- **The next number is joined at once.** Our clients join OpenCE hosts of
  the next number on day one (presumed additive, above). Our hosts keep
  announcing the number they have proven until the cross-play gate passes:
  announcing a number early would let in OpenCE clients that might not play
  the same game.
- **Bumps, not features, by hand.** When OpenCE raises its network version,
  Donut posts it in #crossplay-watch on the Discord, and a person decides
  whether to follow. Nothing follows on its own. An additive raise (messages
  older machines drop) is followed with the legacy table or a run of the
  watch, after the cross-play gate passes. A raise
  that changes how a game plays, or internet play's signalling, gets the
  smallest port that keeps games in agreement, in a release, and nothing else
  of that build.
- **Servers first.** Our dedicated servers move to each follow as soon as it
  ships, so OpenCE players always have a server to join.
- **Weekly at most, for ports.** A raise that needs ported
  code is batched into a release about once a week, unless cross-play is
  broken now. A port that would cost more than an evening is weighed against
  the players it keeps.
- **Security fixes** in shared code are ported by hand whenever they turn up.
- **Browser builds** aren't followed: their players can't join our games, nor
  we theirs.

### The compatibility table

Built: `port/linux/include/delta.h`, `DELTA_LEGACY_VERSIONS`, one row per
OpenCE network version with its first build and whether it is additive or
breaking:

```c
#define DELTA_LEGACY_VERSIONS(X) \
	X(11, "build-76", breaking) /* the gametype's PC options in the settings record */ \
	X(12, "build-118", additive) /* network co-op */ \
	...
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
| 17 | build-128 | followed from OpenCE | additive |
| 18 | build-129 | followed from OpenCE | additive |
| 19 | build-132 | co-op's player collisions switch, in a padding byte of the game settings | additive |
| 20 | build-133 | password games' internet listings (another listing layout) | additive |
| 21 | build-141 | killing blows and resting bodies sent again, co-op's BSP switched by the host's crossing alone, co-op's garbage throttle | additive |
| 22 | build-145 | a Custom Edition map named `custom_maps\<name>` in the game's settings (a joining machine without it is told which map it misses) | additive |
| 23 | build-147 | a Custom Edition map's blocks past the Xbox tools' limits kept whole, its vehicles placed by their spawn flags, and its header checksum sent as the map's version (a client of another version leaves); Xbox maps play as 22 | additive |
| 24 | build-149 | the gametype's PC vehicle set (0xFE), every vehicle the map places; on a Custom Edition map every placement whose spawn flags name the game type (a client of 23 places none of it) | additive |
| 25 | build-169 | units' integrated lights (flashlights) in the host's object states, two bits of their flags (a client of 24 ignores them and keeps toggling a light on presses); a banned machine told it is kept out, an existing rejection code | additive |
| 26 | build-184 | internet play's signalling (not the game's messages, which are 25's): JOIN and ACCEPT say each machine's WebRTC, for OpenCE's browsers, in signalling version 4, which OpenCE reads alone. A release, not a table row: `p2p_signal.c` reads and answers both 3 and 4, and asks in both until a host answers one (wire `chupa-26a`; no WebRTC, so browsers can't join our games) | additive |
| 27 | build-189 | OpenCE's Custom Edition map sharing: new messages, which a machine of 26 drops (we don't share maps: a joining machine without the map is told which one it misses, as before). Signalling as 26, a token joined more than once | additive |
| 28 | build-201 | no message, kind or struct changed: a multiplayer script's `switch_bsp` keeps the game's first BSP (a map with a copy of its place per sky no longer reloads), the flashlight and dynamic lights stay on past the Xbox's player and vehicle limits, and a second weapon picked up while firing is readied. A release (wire `chupa-28a`), so older builds' wire never gets a row for it: a host of 27 would reload such a map while its OpenCE clients of 28 kept it | additive |
| 29 | build-206 | no message or struct changed, but two fields mean something else: a Custom Edition map's level name is its own (`custom_maps\hugeass`, not `custom_maps\hugeass_<8 hex digits>`), and the game record's map version is the first 32 bits of the map's whole-file hash, where 23 to 28 sent its header checksum. Our clients of 29 take both meanings, so they still join hosts of 28; our clients of 28 refuse a host of 29's Custom Edition map as another version (Xbox maps play). A release (wire `chupa-29a`), so older builds' wire never gets a row for it: a host of 28 sends the header checksum, which OpenCE's clients of 29 take for another version | additive |
| 30 | | A rule every machine computes alike: the riders of an elevator are carried by its node's movement of the tick (sampled before the machine updates, applied once its node matrices are made), not the previous tick's, which went the wrong way on the tick an elevator reversed. A host takes only the across of a client's word on a unit riding an elevator, one that is moving or that the unit stands on at rest (a remembered elevator alone, after a jump, is not riding); a client discards the height of the host's correction while it stands on the elevator it recorded, up to 4.0 world units. No message changed: a machine of 29 carries riders a tick late, and its host's states correct them | additive |
| 31 | | The pregame keep-alive is a 4-byte message whose last two bytes are 0 (they were the sender's stack), and the packet table lists begin game, graceful exit and keep-alive each with its own fields (the bytes of the others as before). A datagram stamped more than 450 ticks (15 seconds) from the latest taken from its sender is dropped; over a sender's stream any stamp is taken. client_ready, client_identity and hit reports are taken from a client's stream alone. A second game on one computer: a client takes any free port if the client port is in use, and the host answers a machine at the port its datagrams came from, finds a client by address preferring a machine that is not its own, and answers a game search from another port at that port. Message layouts as before | additive |
| 32 | | The host's policy on aiming assist for the mouse and keyboard, a new message (90, 12 bytes, reliable: the header, 0 or 1 as a 16-bit value, 16 bits of 0), sent to a client when it has loaded and to every client when the setting (`network.block_mouse_aim_assist`) changes; a client honors it, and is blocked until a host of 32 or later speaks. A client's input entry says in byte 2 (padding before) whether its player plays co-op without friendly fire; our hosts read it and keep their own friendly fire setting. A release (wire `chupa-32a`), so older builds' wire never gets a row for it. A machine of 29 to 31 drops message 90 and ignores the byte. The rows' first builds are not recorded (the table keeps 29's) | additive |

### Automation

Built (command repository, `watch.yml` and `tools/follow.py`), running
since October 5, 2026; it followed versions 15 and 16 that day. The site's
host starts it within minutes of an OpenCE release, and a daily run catches
any start that never came. Beside the steps below, every change of the
number also starts the cross-play gate (`tools/crossplay_test.py` on every
wire still in use; see "Following OpenCE", below): a pass widens the legacy
table, so the builds already out follow too.

1. **Watch.** The command repo's watch workflow sees an OpenCE release that
   changes the number.
2. **Classify.** A script diffs it against the previous build and proposes
   *additive* (new message kinds or enum values appended, new handlers,
   nothing changed in encoders, struct layouts, limits or the settings
   record) or *review* (anything else).
3. **Raise.** It pushes a branch to chupathingyce with the new number, the
   join range, the table row, the README line and the next patch version.
4. **Verify.** ChupathingyCE's own CI builds and tests the branch. The game
   against OpenCE is the cross-play gate's, which tests the releases already
   out.
5. **Ship.** Additive and green: main moves to the branch, a release goes out
   (clients update themselves), and the [D] servers and Tex roll to it when
   they're empty. Otherwise nothing ships: an issue in the command repo and
   a Discord post ask a person. The repository variable `FOLLOW=off` sends
   every raise to a person.

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

### The host's fields (built)

A host's announcement (`POST /v1/announce`, form encoded; `browser.c`'s
`host_fields`) adds, after `version` and before the roster (which stays
last):

| Field | What it is |
| --- | --- |
| `delta` | the Delta major the game is hosted with (`DELTA_MAJOR`), or `0` for the legacy protocol alone (`network.protocol = "opence"`, or the Delta port taken); left out until Delta Peer has run a frame |
| `platform_key` | the host machine's platform key (see "Platform keys"), 16 hexadecimal digits |
| `arch` | `x64`, `x86`, `arm64` or `arm` |
| `build` | the build's version (`0.7.1b`) |
| `capabilities` | the capabilities it offers, 8 hexadecimal digits; bit 12 (`ping`) is set here alone, by a host that answers pings before joining |
| `machines` | while hosting with Delta: the game's machines by platform, `pc_linux:2,xbox:1,legacy:1` (`legacy`: a machine without Delta; `unknown`: a Delta machine that does not share its platform; a dedicated server leaves itself out). Counts only, never a player (`delta_peer_game_host_summary`) |

Compatibility both ways: a site from before these fields ignores them (it
reads the fields it knows), and the site reads each on its own, leaving out
one it can't read rather than refusing the announcement, so older builds
(which send none) and newer ones (a longer platform key, a platform it
doesn't know) are listed as ever.

From them the site lists each game's `platform`, `hosting` (`official`: our
`[D]` servers, by their hosts; `dedicated`; `player`), `protocol` (`delta`,
`opence`), `family` (`xbox`, `ce`, `pc`, `md`, by the map's suffix) and
`machines`; OpenCE's public games (the brokers) are `opence` and nothing
more. The game's browser reads them from `/v1/games.txt?fields=19` (field
13 the region; 14 to 17: platform, hosting, protocol, machines; 18 an
OpenCE game's gametype; 19 the announced capabilities) and shows them: the Online
Games screen's rows (a platform tag before the name, `SRV` for a server),
its details (a Host line; the machines beside IN GAME) and the PC menus'
Server Browser's Rules line. The console list adds platform, hosting and
protocol after its tenth field, which Warthog's reader passes over.

A player's own copy also says its platform (`"platform"`) when it confirms
its line (`/v1/claim`) or reports a game it joined (`/v1/client_report`),
for the site's "Played on" profile badges.

### Pings before joining (built, 0.8.0)

Online Games shows each listed game's ping before joining: a measured one
where the host answers, `~NN` (an estimate) where it can't, nothing where
nothing is known; sorting by Ping puts measured first, then estimated, then
none. The numbers fill in as they arrive.

There was nothing to reuse: Halo PC servers answer GameSpy queries on
their game port, but nothing of that is in the decomp, OpenCE or our
builds, and no client knows a host's address before signalling (the list
never gives one). Player hosts and dedicated servers are reached the same
way, so one mechanism serves both: a probe through signalling, then a
small ping on the host's tunnel socket bound to the probe.

Signalling (sealed with the invite token's key and published on the
host's topic, like JOIN; OpenCE and older builds act only on `J` there):

| | Layout |
| --- | --- |
| PROBE (`P`), prober to host | `'P'`, version 1, probe id (16, random), reply id (16, random a run; the answer goes to `hceu/3/<its hex>`), address count (1, at most 4), addresses (6 each) |
| PROBE_ANSWER (`Q`), host to prober | `'Q'`, version 1, probe id (16), the host's Ed25519 key (32), address count (1, at most 4), addresses (6 each), Ed25519 signature (64) of `"chupathingy probe"` and everything before it |

A prober takes an answer only when the key's X25519 form hashes to the
invite's host hash and the signature checks, so no one watching the brokers
can point it at another machine.

The tunnel socket (magic `0x6A`; the tunnel's is `0x69`; older builds drop
it unread): every datagram 24 bytes: magic (1), type (1: PING, 2: PONG, 3:
PUNCH), sequence (2), probe id (16), the prober's clock (4, echoed).

A probe is never a peer: no session, no X25519 work, no virtual address,
no stand-in, no broadcast, no slot, no player count; the game never hears
of it. The host keeps its id in a 16-entry table for 10 seconds, and
answers a PING only with a live id, with a PONG of the same 24 bytes, to
the PING's source.

| Limit (`p2p_probe.h`) | |
| --- | --- |
| new probes a host takes | 8 at once, then one each 250 ms |
| a probe's answer said again (the prober's resend) | at most once a second, at no new cost |
| PONGs a probe | 8 |
| the host's PUNCH (for a host behind NAT) | 2 rounds to at most 4 addresses, stopped by the first PING |
| a prober's probes at once | 4 |
| PROBE resent | every 1.5 s, 3 times; given up after 5 s |
| PINGs | every 200 ms to each address, until 3 PONGs or 5 s; the lowest round trip is shown |
| a host measured again | after 60 s (a failed one too) |

Amplification and spoofing: a PONG is never larger than its PING, and a
PING without a live id gets nothing. Probe ids can be read by anyone with a
listed game's token watching the brokers, so a spoofer can at most have 8
PONGs of 24 bytes sent to a victim per id, after paying for a sealed MQTT
publish (larger than that) within the brokers' own limits and the host's 4
new probes a second. The PUNCH goes to addresses the prober names, as
JOIN's punch does today, within the same budget. A forged PONG can only
make one row's number wrong; the number is never used for anything else.

Privacy: no address is shown or logged (the log says "answering pings",
"pinging the listed games", counts only). The prober learns a host's
addresses only through its signed answer, as joining does, and keeps them
in memory. A host sees a prober's public address, as it sees anyone's who
joins; `network.ping_games = false` turns probing off (estimates only).

Estimates: `GET /v1/anchors.txt` lists `anchor<TAB>host<TAB>port<TAB>
regions` lines (regions: continents `EU` or countries `EU-DE`, as a game's
region). The client times a TCP connection to each (twice, the lower kept;
nothing is sent), once a run, the first time Online Games opens with online
play on, and estimates a game from its region: the lowest anchor naming its
country, else its continent. The site's defaults are our own hosts for
North America and a cloud provider's regional endpoints elsewhere
(`--anchors` replaces them).

Compatibility: only hosts that announce bit 12 are probed; older hosts show
an estimate or nothing. A site from before field 19 gives no capabilities:
no probes, estimates only if it has anchors. Nothing else changes meaning,
and the legacy table's per-capability kill switch turns `ping` off (no
answering, no probing). No pings at all with online play off.

`port/linux/tests/ping_test.c` (`tools/test_ping.py`) tests the formats
with hostile input, the host's limits, the prober's measuring, the anchors
and the Ping column's order; `HALO_NETWORK_TEST=browse` opens Online Games
and logs each game's ping, for two copies on one machine.

## Delta Stats

- **Reports** (exists): end-of-game totals, with the host, verified and
  player tiers (`/v1/report`, `/v1/client_report`, `/v1/claim`). In the
  host's report, a line of a player not on Chupathingy (no Delta Peer
  platform, not the host's own) is only `name`, `team`, `place`, `score`,
  `kills`, `deaths` and the claim `tag`; such a player who left early is
  only `name`, `team`, `kills` and `deaths` in `departed`. In a client's
  report, only the reporter's own lines are in full; every other line is
  the same minimum.
- **Events** (built: `port/linux/src/event_log.c`, `event_upload.c`,
  `port/linux/game/game_events.c`; the site's `analytics.py`): what happened
  in a game, recorded by its host and sent when it ends.

### Who records

Only a host records. It is on by default and the host's operator or
player can turn it off (`network.report_events`, `HALO_NET_REPORT_EVENTS`
set to false): our [D] servers, community servers and ChupathingyCE
players who host, unless they opt out. That setting stops the events
batch only. The host's carnage report (`/v1/report`) is a separate upload:
it goes whenever the game is listed, and `network.report_hosted_games =
false` (`HALO_NET_REPORT_HOSTED`) stops it and the events batch while the
game stays listed; `network.list_hosted_games = false` (or an empty
`network.browser_url`) stops it too, with the listing. Everything is what the host saw, so it is exact; nothing a
client says is taken. Co-op (campaign) games are never recorded.

### Players

A player is their name, their client (`chupathingyce` when their machine
speaks Delta or is the host's own, else `other`) and platform. No hardware
ID, nor any hash of one, is sent (schema 1 had a keyed hash, `ident`; the
site no longer keeps it). A player whose client is `other` (an OpenCE
player) is only their name, client, team, bot, score, place, kills and
deaths: no other totals, and no event row is about them (their medals,
objectives, pickups, rides, spawns, positions, pings, sessions and flags
are left out; a kill of theirs, or of them, keeps their index but not
where they stood or their vehicle). A player becomes a confirmed
player (a player ID, so a profile) only the way carnage reports confirm
them: their own copy of the game claims their line of the same game's
report with its player key (`/v1/claim`); the site then marks the same
line of the events. A copy claims a game it joined only with
`network.report_joined_games` on (the default), the setting that also sends
its own report of the game; a game it hosted, unless
`network.report_hosted_games` is off. A host's word about who a player is is never taken. No
IP address is ever recorded or sent, by the game or the site.

### The batch (schema 2)

JSON, gzip on the wire. `t` is seconds since the game started; positions
are world units `[x, y, z]`; players are indexes into `players`, `-1`
nobody (the world). Later versions only add fields; unknown fields are
ignored.

```
{"schema": 2,
 "server": {"name", "build", "platform"},
 "game": {"id" (32 hex, random), "part", "final", "invite"?, "playlist"? (a
          dedicated server's playlist file's name), "map", "map_info"?
          (the map's facts, below), "engine",
          "gametype", "teams", "score_limit", "started", "ended" (unix),
          "end_reason": score|time|admin|empty|error|other, "team_scores"?},
 "players": [{"name", "client", "platform"?, "team", "bot", "color"?
              (armor, 0-17),
              "score", "place", "kills", "deaths", "assists", "betrayals",
              "suicides", "best_spree", "damage_dealt", "damage_taken",
              "shots", "hits", "grenades": {"frag", "plasma"},
              "objectives": {"flag_grabs", "flag_returns", "flag_scores",
                             "ball_time", "ball_kills", "hill_time", "laps"},
              "weapons": [{"weapon" (tag folder), "shots", "hits", "kills",
                           "headshots", "damage"}],
              "vehicles": [{"vehicle", "seat", "seconds"}],
              "pickups": {tag: count}, "medals": {key: count}}],
 "sessions": [{"player", "joined", "left", "reason": end|quit|kick|ban|timeout}],
 "kills": [{"t", "killer", "victim", "weapon" (damage tag), "damage":
            bullet|plasma|melee|grenade|explosion|vehicle|fall|other,
            "killer_pos"?, "victim_pos"?, "killer_vehicle"?, "headshot"?,
            "betrayal"?, "suicide"?, "stuck"?, "victim_riding"?, "from_grave"?}],
 "medals": [{"t", "player", "medal"}],
 "objectives": [{"t", "player", "team", "kind": flag_grab|flag_return|
                 flag_score|ball_grab|ball_drop|hill_enter|hill_exit|race_lap,
                 "pos"}],
 "pickups": [{"t", "player", "item", "pos"}],
 "rides": [[from, to, player, vehicle, seat]],
 "spawns": [[t, player, x, y, z]],
 "positions": [[t, player, x, y, z]],
 "pings": [[t, player, ms]],
 "health": [{"time", "players", "tick_ms", "tick_ms_max", "cpu", "memory_mb"}],
 "moderation": [{"t", "kind", "player", "name", "by", "reason"}],
 "flags": [{"t", "player", "kind", "severity", "detail"}],
 "limits": {"events", "capacity", "sample_seconds", "dropped": {type: n}}}
```

How each is known: kills and damage at the blow (`damage.c`), shots per
projectile a trigger makes (`weapons.c`; grenades thrown count as shots of
the grenade), hits per projectile that damaged another player (melee
excluded); medals and sprees are worked out from the kills as Halo does
(multikills within 4 seconds; sprees at 5, 10, 15, 20; Killjoy for ending
a spree of 5 or more; From the Grave for a kill that lands after its
killer died; Beat Down, Sniper Kill, Stuck, Splatter); objectives, rides,
pickups, powerups and spawns from each player's state each frame; positions
every `network.events_positions` seconds (2) per living player; ping every
10 seconds; health each minute (dedicated servers only).

Limits: a game keeps at most `network.events_limit` events (40000, about 60
bytes each). When full, position and ping samples thin out (every other
second, then every other of those, ...), then pickups, rides, spawns and
medals give way to kills, objectives and moderation; every drop is counted
in `limits`. Totals are counted before anything is dropped, so they stay
exact. The site refuses a batch over 2 MB sent or 8 MB of JSON.

Moderation: `event_log_moderation(kind, who, by, reason)` (event_log.h)
records a kick, ban, warning or other action in the game under way, and a
kicked or banned player's leaving is recorded as such. Delta Control's
audit hands each action to `server_roles_set_recorder()`'s recorder, on any
thread; `server/platform/server_events.c` queues it
(`event_upload_moderation`) for the game's thread. A game that ends with everyone gone is sent as `empty`; a
server that stops mid-game sends nothing for it. Anti-cheat `flags` are a
schema slot with nothing filling it yet.

### Map facts

`game.map_info` (optional; added in 0.8.0, still schema 2) describes the
map the game was played on, for the site's map catalog: what the map is and
its shape, so the site can list maps beyond the Xbox's that it can't read
itself. The host fills it in from the map it loaded
(`port/linux/game/game_events.c`, built by `port/linux/src/map_report.c`).
It holds facts about the map only. Nothing about players, machines or
addresses is in it, and no picture or file content.

```
"map_info": {"family": xbox|ce|md|pc,
             "file" (the file's name, without its family's suffix or ".map"),
             "size"? (bytes; not for an Xbox map, whose file is compressed),
             "blake2b"? (64 hex: the whole file's BLAKE2b-256, as Delta Peer's
                         MAP has it; every family but the Xbox's has it),
             "cache_version", "header_checksum" (8 hex), "build" (the cache
             header's build string), "scenario" (its tag path),
             "gametypes": [slayer|ctf|oddball|king|race] (slayer for any
                           spawn; the others for their netgame flags),
             "spawns", "bounds"?: {"x": [min, max], "y": [...], "z": [...]}
                                  (the structure BSP's world bounds)}
```

For example, Danger Canyon from Custom Edition:

```
{"family": "ce", "file": "dangercanyon", "size": 15165396,
 "blake2b": "2a97db833fec4a553a68ddfaeb53507e43cc2e2bcf9036697fb6e65b0bcf1f2e",
 "cache_version": 609, "header_checksum": "c410cd74", "build": "01.00.00.0609",
 "scenario": "levels\\test\\dangercanyon\\dangercanyon",
 "gametypes": ["slayer", "ctf", "oddball", "king", "race"], "spawns": 103,
 "bounds": {"x": [-75.449, 72.65], "y": [-23.019, 69.183], "z": [-12.178, 11.618]}}
```

A map past the Xbox's goes in only with its hash. While the hash is still
being computed, the next part asks again, so a game's first part can come
without `map_info`. A site that doesn't know the field ignores it, as with
any unknown field. Sites should treat it as a claim, like the other
fields: record a map's facts once two reporters agree on the same hash, or
one of our own servers reports them.

### The upload

`POST <network.browser_url>/v1/events`, `Content-Type: application/json`,
`Content-Encoding: gzip`, and `Authorization: Bearer <token>` for a trusted
server (`network.events_token`, `HALO_EVENTS_TOKEN`; made on the site's host
with `analytics.py server-add`, only its hash kept there; sent only over
HTTPS or to this computer). Without a token, the site takes a batch only
while the same address lists the game whose `invite` it carries. Answers:
`200 {"ok", "match", "tier", "duplicate", "ignored"}`; `400` a bad field
(named); `401` a wrong token; `403` not a game this address lists; `409` a
different batch of a finished game; `413` too large; `422` co-op; `429`
too many (one each 20 seconds, 60 an hour, per server).

A batch is sent 5 seconds after its game ends (after the carnage report),
again later after no answer, a 5xx or a 429 (doubling from 30 seconds, for
about a day), and at once when the program exits. A long game is also sent
as it stands every `network.events_part_minutes` (30) with `"final":
false`; the site keeps the latest part and replaces it with the end.
`network.events_folder` keeps a copy of each batch.

### On the site

Stored in `reports.db` (`analytics.py`): matches and players' lines always;
kills 180 days, timelines 180, replays (positions) 30, heatmap squares 365,
server health 90, moderation 365; past a size budget
(`--events-budget-mb`, 400) the oldest replays, then timelines, then kills
go first. Pages: `/matches/N`, `/players/<who>/stats`, `/maps`,
`/maps/<map>`, `/servers`, `/servers/N`, `/records`; the interface is on
the site's `/api`. Leaderboards and records count ranked matches only (two
or more players, no bots, a minute or more). A confirmed player can hide
their stats or their heatmaps from their profile.

## Delta Control

Everything that runs a dedicated server: its `sv_` command table, its
console, its control API and control panel (HTTP on the loopback address,
HTTPS beyond it), in-game moderation over Delta Peer, and the optional link
to the site. The operator's guide is `server/docs/moderation.md`. Its rule:
only the command table runs, never a shell; every change is checked against
a role and audited, on the server and (for the site's) on the site.
Player-hosted games keep OpenCE's own kick and ban; Delta Control is the
dedicated server's.

### Roles

Roles, lowest first: none (0), moderator (1), admin (2), owner (3). The
permission bits are the same on the server, in Delta Peer's MOD_STATE and in
the site's role lists:

| Bit | Permission | Roles |
| --- | --- | --- |
| 0x001 | view: status, players, bans, log, audit, playlists, settings | moderator, admin, owner |
| 0x002 | warn | moderator, admin, owner |
| 0x004 | kick | moderator, admin, owner |
| 0x008 | ban for up to 7 days | moderator, admin, owner |
| 0x010 | ban for longer, or for ever | admin, owner |
| 0x020 | unban | admin, owner |
| 0x040 | maps, the playlist, the game | admin, owner |
| 0x080 | settings, game types | admin, owner |
| 0x100 | roles: accounts, moderators, the link | owner |
| 0x200 | invite roles below one's own | admin, owner |
| 0x400 | the console's own (tokens, accounts) | the console |

The console, startup commands and control API tokens act as the owner. A
command's permission is decided by `control_command_permission`
(`server/platform/control_roles.c`), checked by the control thread before
it queues a request and again by the main thread before it runs it.

### Identity

A moderator is known by a **moderator key**: an Ed25519 key pair whose seed
is SHA-256("halo-ce-universal moderator key\n" || player key), the player
key being the game list's (`game_list_player.key`, never sent to a game
server). The public key (64 hex digits) is the identity. A game proves it
with Delta Peer's `moderation` capability (see Delta Peer, Moderation);
names and hardware ids never give a role. The site derives the same key
whenever a game sends it its player key, and keeps the public key on the
profile.

A key's role is the highest of: the server's `moderators.txt`; a control
panel account bound to the key (the server asks the game over Delta Peer,
its player confirms); the site's role list, when the server is linked,
never above `HALO_DEDICATED_LINK_ROLE` (admin unless set).

### Accounts and remote access

The control panel has an account for each person: a name, an Argon2id
password hash, a role, an optional TOTP second factor (RFC 6238, SHA-1,
30 s, 6 digits; the owner may require it), an optional bound moderator key.
The first owner is made from a one-time setup code the server prints on its
console; others from invitations (single use, 48 hours, hashes kept). Wrong
passwords and codes back off per account (a minute after five, doubling to
an hour) and per address (counted by a keyed hash, never kept). Sessions are
HttpOnly, SameSite=Strict cookies (Secure over HTTPS) with CSRF tokens.

Beyond the loopback address the panel is HTTPS only (Mbed TLS, TLS 1.2,
ECDHE and AEAD): the server's own ECDSA P-256 certificate, whose SHA-256
fingerprint it prints and the login page shows, or the operator's (PEM,
reloaded on renewal). No ACME client: it needs port 80 and a domain, and
certbot's files are taken as they are.

### The link to the site

Optional and outbound: the server dials the site; no port is opened.

1. `sv_link`: the server makes an Ed25519 key pair and a 32-byte secret,
   and sends the public key and the secret over HTTPS (the site's
   certificate checked): `POST /v1/control/link/start` `{"public_key",
   "secret", "name", "version"}` answers `{"code", "expires", "token"}`.
2. The owner signs in on the site and enters the code at `/servers/link`
   (good 10 minutes, once). The server asks `POST /v1/control/link/status`
   `{"token"}` every 3 s until `{"state": "linked", "server_id", "owner"}`,
   and keeps `delta_link.key` (0600).
3. Then every few seconds, `POST /v1/control/poll`, signed:
   `{"server_id", "time", "nonce", "payload", "signature"}`, the signature
   Ed25519 over `"delta control request v1\n" + path + "\n" + server_id +
   "\n" + time + "\n" + nonce + "\n" + payload`; the site refuses a time more
   than 300 s off, a nonce seen before, a revoked server. The payload has the
   server's state (name, map, players' names and numbers; never addresses or
   hardware ids) and the results of the commands it ran.
4. The site answers `{"payload", "mac"}`, the MAC BLAKE2b-256 keyed with the
   secret over `"delta control response v1\n" + nonce + "\n" + payload`; the
   server takes nothing from an answer whose MAC is wrong. The payload has
   the commands the site's accounts sent (`sv_warn`, `sv_kick`, `sv_ban`,
   `sv_unban`, `sv_map`, `sv_mapcycle_next`, `sv_end_game`, `sv_name`,
   `sv_maxplayers`, with a reason) and, when it changed, the role list
   (handles, roles, their profiles' moderator keys).
5. Each command is authorized on both sides: the site by the account's role
   on that server, the server by its copy of the role list, its own
   permission table and its cap. Both audit it.
6. Unlinking: `sv_unlink` deletes the credential at once and tells the site
   (`POST /v1/control/unlink`); unlinked on the site, the next poll's MAC'd
   answer says so and the server deletes it. `HALO_DEDICATED_LINK=false`
   turns the link off for good.

HTTPS requests in the server go one at a time across its threads (Mbed TLS
is built without locks), so polls are short; long polling waits for a build
of Mbed TLS with its threading on.

### The event log's hook

`server_roles_set_recorder()` (`server/src/server_roles.h`) hands every
audited change, as a `struct control_audit_event` (time, via, actor, role,
action, target, reason, ok, detail; never an address), to a recorder: Delta
Stats' event log records moderation through it.

## Delta Link

- **Players** (exists): Link Profile ties a game install to a site profile
  with a code (`/v1/link`, `/v1/connect`, `/v1/claim`).
- **Servers** (exists): see Delta Control, The link to the site.

### Delta Profile in the game

The player's own Delta profile, shown in the game
(`port/linux/game/profile_screen.c`). The game never makes, renames or
deletes a profile. The password, email, two-step sign-in, export and
deletion stay on the site, which OPEN ON THE WEB reaches. Linking is per
copy: every Halo profile on a copy counts for the same Delta profile.

- **Ways in:** the Chupathingy menus' Profiles have a DELTA row
  below Create New Profile, with a summary in the panel. Gearbox's Select
  Profile has a DELTA button. In every menu set, Online Games'
  Start opens it (the web page when `display.delta_profile` is false).
  A Halo profile's panel shows one quiet line, "Delta: @handle" or
  "Delta: not linked". With an empty `network.browser_url`, every way in
  is hidden.
- **Sections:** OVERVIEW (name, the player's Spartan, badges, record,
  recent games), LINKED COPIES, ROLES, CLAN, PRIVACY, then LINK
  PROFILE or UNLINK THIS COPY, and OPEN ON THE WEB. The Chupathingy menus
  draw them with their own widgets
  (`port/assets/menus/chupathingy/delta_profile.xml`), and Privacy is one
  of their settings pages. The Xbox and Gearbox menus get a
  screen drawn as Online Games is. The dialogs (Link Profile, unlinking,
  and the pages of the site with a QR code) are the same in all three.
- **Requests:** the game sends this copy's player key, over HTTPS only,
  to the game list: `POST /v1/game/profile` (the profile, its copies, roles
  and privacy), `/v1/game/unlink` (this copy only) and `/v1/game/privacy`
  (the site's own privacy settings). The site takes 6 requests a minute
  from a key, so the game asks at most every 10 seconds, and again when the
  answer is a minute old. Another copy is unlinked on the site
  (`/profile#copies`), signed in, never with a key alone. The answer is
  kept in memory only. `debug.txt` says whether it loaded, never what it
  holds.
- **Profile names:** in a game on Delta, a player whose copy shares its
  profile (`network.share_profile`, Delta Peer's `profile` capability)
  shows by the profile's display name on the scoreboard, the postgame
  report and the lobby (`port/linux/game/profile_names.c`). The game asks
  the site for those players' public cards (`POST /v1/game/players`, no
  key). A machine with more than one player keeps the game names, because
  a profile belongs to a copy. OpenCE players, players who don't share,
  and what OpenCE clients see are unchanged. `display.profile_names =
  false` shows everyone by their game name.
- **Consoles:** `HALO_CONSOLE_NO_DELTA_PROFILE` leaves all of it out. A
  console that has the game list keeps Online Games' Link Profile panel.

### Clans

Clans are groups of Delta profiles, kept by the game list (its
`clans.py`). A clan has a name, a tag of 2 to 8 letters and digits, a
motto, an emblem and ranked members (leader, officers, members). Making a
clan, inviting, ranks, kicking, handing the clan to another member and
the emblem are on the site (`/profile#clan`, `/clans/TAG`).

- **In the game:** Delta Profile's CLAN section shows the profile's clan:
  its emblem, name, tag, member count and motto, then its members by rank
  (the first nine, then how many more). With no clan it lists the clans
  that invited the profile. Its A opens a dialog: SHOW MY TAG and LEAVE
  CLAN, or the invites (A joins, X declines). A leader leaves once another
  member leads, or when alone (the clan goes with them).
- **The emblem:** a background shape in one color and a foreground shape in
  two, from Halo's 18 armor colors. The pieces are the site's
  (`GET /v1/clans/emblem`); `port/linux/src/clan_emblem_table.h` is made
  from them by `tools/clan_emblem_table.py`, and `clan_emblem.c` draws
  them the way the site does.
- **Requests:** `/v1/game/profile` carries `clan` and `clan_invites`, and
  its privacy `show_clan_tag`; `POST /v1/game/clan` (the player key, an
  action: `accept`, `decline` or `leave`) answers with the same two.
- **Tags in games:** a player who shares their profile and shows their tag
  is "[TAG] Name" on the scoreboard, the postgame report and the lobby, the
  name cut to fit with "...". The public card (`/v1/game/players`) carries
  the tag only while its owner shows it. OpenCE players and clients see
  nothing new.
- **Privacy:** SHOW CLAN TAG (Delta Profile's Privacy, and the clan dialog)
  is the site's own setting. Members who hide their service record are left
  out of the member lists others see, counted as hidden.

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
    "chupa-24a": { "announce": 24, "minimum": 11, "maximum": 24 }
  },
  "disabled_capabilities": [],
  "platform_policy": {}
}
```

- **`delta_legacy`**: the document's format, 1. A build drops a table of a
  format it doesn't read.
- **`serial`**: 1 to 4294967294 (0xFFFFFFFF says "takes no tables" on the wire); each table published gets a higher one.
  **`issued`**: when it was made (Unix seconds), for people; optional.
- **Rows by wire, not by build.** Each build has a wire ID (`DELTA_WIRE` in
  `delta.h`, `chupa-32a` today; `chupa-29a` came before it; `chupa-28a` was 0.7.4e's and 0.7.5's, `chupa-26a` was 0.7.2e's and 0.7.3e's, `chupa-24a` was 0.7.1e's, `chupa-23a` 0.7.1b's, `chupa-20a` 0.7.0b's): the revision of the game protocol it
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
  release (merge the code, new wire ID), as today ("Following OpenCE").
- **`follows`** (optional, in a row): the OpenCE build the cross-play test
  proved the row with (`"build-145"`: letters, digits, `.`, `-` and `_`, at
  most 31). Shown in the log and the server's status ("Following OpenCE
  build-145 (table 2)"); a row without it shows the network version. Builds
  before it skip it as an unknown key.
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

### Epochs

A serial's top byte is its **epoch** (serials 1 to 0x00FFFFFF are epoch 0,
0x01000001 starts epoch 1, and so on), and each key signs tables of epochs
up to its own last one (`delta_key_last_epochs` in `delta_key.h`): the
primary key's is 0, the recovery key's 255. A table whose epoch is past its
signing key's last is dropped whole, like one that doesn't verify.

That makes a leaked primary key recoverable. Serials only go forward, so a
leaked key that signs the highest serial it can (0x00FFFFFF) would leave
every build refusing every later table; but the recovery key signs a table
of the next epoch (`delta-table.yml` with `use_recovery` and `new_epoch`),
every build takes it as newer, and the leaked key can sign nothing past it.
Until a release adds a new primary key (whose last epoch is the one the
recovery key opened, the leaked key then dropped), tables are signed with
the recovery key. The wire is unchanged (a serial is still 4 bytes, and
0xFFFFFFFF still means "takes no tables"), and every table published before
is epoch 0, as valid as before. Builds from before epochs (0.7.0b and
older) take any key's table of any epoch, so for them the recovery
remains a release.

`tools/delta_table.py next-serial --from legacy.json [--new-epoch]` gives
the next serial, and `verify` checks the epoch against `delta_key.h`'s keys
(`--last-epoch` with `--public-key`).

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
  neither has a table (both answer 404), the log says so once a run, not
  at every retry.
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
- A taken row's maximum is at least its announce plus
  `DELTA_LEGACY_PRESUMED` (since 0.7.5a): the next number is presumed
  additive (see "The legacy number").
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
| `delta_legacy_following(text, size)` | "Following OpenCE build-145 (table 2)", for the log's header and `sv_status` |

`tools/test_delta.py` checks the tool and builds `delta.c` with a test key
(`tools/delta_check.c`) to check what the game takes and drops.

## Following OpenCE

When OpenCE raises its network version, every ChupathingyCE player and
server should stay visible to OpenCE players and joinable, without a new
download, whenever our wire still plays with theirs; and we should know
within hours when it doesn't.

1. **Detect.** The command repository's watch (`watch.yml`, started by
   donut_watch on the site's host) sees a new OpenCE release with another
   network version, and starts the gate (`crossplay.yml`; the variable
   `CROSSPLAY=off` stops that).
2. **Plan** (`tools/crossplay.py plan` in command). The wires in use: the
   last two releases' `DELTA_WIRE`s. For each, its row now (the published
   table's, or the release's built-in numbers) and the row a pass would
   give it: `announce` and `maximum` raised to the new number, `minimum`
   kept, `follows` the OpenCE build. A row that already reaches the number
   is not tested.
3. **Test** (`tools/crossplay_test.py`, on a self-hosted runner). Release
   builds of OpenCE's tag and of each release, cached by commit. Each
   release plays with the proposed row as a local table (the claim the
   table would make):
   - our host with OpenCE clients, and OpenCE's host with ours, on an Xbox
     map: join, spawn (positions at the same ticks), every death counted on
     every machine with the body dead within 2 seconds and none left
     standing, the clients' shots, the score at the end of Slayer, the end
     of the game, the next game (CTF), a client joining the game in
     progress, a client leaving and coming back;
   - our dedicated server with an OpenCE client and one of ours;
   - each build against itself, as a baseline: a step that fails there too
     is the build's or its test hooks', not cross-play's, and is reported
     without failing the run.

   The result is JSON, a pass or fail per step with what was seen, and
   every copy's logs. About six minutes a wire.
4. **Decide** (`tools/crossplay.py decide`).
   - A pass: the rows (`WIRE=ANNOUNCE,MINIMUM,MAXIMUM@build-N`) go to
     `delta-table.yml`, which makes the table (`tools/delta_table.py make
     --row`, which only widens), signs it and commits it to the
     `delta-table` branch. With `AUTO_PUBLISH` (a repository variable, or
     the run's input) that happens at once; otherwise Donut posts the
     command and a person runs it.
   - A fail publishes nothing. An issue on chupathingyce ("OpenCE build-N
     needs a release") lists the failing steps, and Donut posts to
     #upstream-watch. The release work takes it from there: the smallest
     port that keeps games in agreement ("Following policy", above), a new
     wire ID, its own row once its gate passes.
5. **Spread.** The site pulls the `delta-table` branch every five minutes
   (checking the signature, newer serials only) and serves it at
   `/v1/delta/legacy`. Builds fetch it at start and every four hours, and
   pass it to each other over Delta Peer. A build that takes it announces
   the new number in its listings and advertisements, so OpenCE's
   browser (an exact match) lists its games, and joins the new range.
6. **Show it.** The log says "Delta: legacy table N from ...: announcing X,
   joining A to B; following OpenCE build-N", and its header "Following
   OpenCE build-N (table N)"; the server's `sv_status` and
   `/v1/status` (`following`) too. The site's Delta page lists what each
   release follows, and its game list labels a game of an older number
   ("OpenCE v11: needs an older OpenCE").

### Running the test by hand

On Linux (the loopback addresses beyond 127.0.0.1 need nothing set up),
with the NTSC Xbox maps:

```
python3 tools/crossplay_test.py build --repo . --ref v0.7.0b --out /tmp/ours --server
python3 tools/crossplay_test.py build --repo ../opence --ref build-145 --out /tmp/theirs
echo '{"delta_legacy": 1, "serial": 1, "wires": {"chupa-20a": {"announce": 22, "minimum": 11, "maximum": 22}}}' > row.json
python3 tools/crossplay_test.py run --ours /tmp/ours/build/linux/halo \
    --server /tmp/ours/build/server-x86/chupathingyce-server \
    --theirs /tmp/theirs/build/linux/halo --maps ~/halo/maps \
    --ours-table row.json --work /tmp/crossplay --out result.json
```

Every copy has its own home, data and save folders under `--work`, and
nothing goes online. `check --work /tmp/crossplay --out result.json` checks
a run's logs again. The tool's header lists the steps and what passes.

What OpenCE build-145's own hooks can't do (seen against itself too): its
host starts no game once a client of the network test has changed team in
its lobby, and drops a lobby quiet for 15 seconds, so the test starts each
game 4 seconds after setting it up, and its host's next game (CTF) is not
counted. Every OpenCE copy also logs a release build's assertion in
`sound_dsound_xbox.c` (#980, a gain out of range) a few times; it is
reported, not counted.

### Taking a follow back

A follow found wrong is taken back with a newer table whose row for that
wire is the release's own numbers again (or has no row): builds check a row
against what they shipped with, not against the table before, so going
back to the shipped numbers is allowed with the primary key today
(`tools/delta_table.py make --serial N --from legacy.json --take-back
chupa-20a=20,11,20`, then delta-table.yml signs it as usual). Nothing can go
below the shipped numbers: a table whose row would is dropped whole by
every build of that wire.

What a newer table can't fix is a table no newer one can follow: one signed
with a leaked primary key at the highest serial. The recovery key opens the
next epoch for that ("Epochs", above).

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
  under unit tests and a fuzzer. The host stays authoritative; a peer's claim (platform,
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
- **Checked upstream raises.** Builds already out announce an OpenCE raise
  only after the cross-play gate has played them against it. A new release
  announces it only when the watch's classifier finds none of the wire's
  files changed, or after a person's review. The number alone is never
  matched blind.

## Phases

1. This document, the compatibility table and the capability registry.
   Done: `delta.h` and `test_delta.py`.
2. The announced number and join range checked against the table. Done.
   The built-in numbers are still written in `halo_port_limits.h`; the
   signed table (7) widens them without a release.
3. The watch workflow's classifier and the cross-play gate (done, in the
   command repository: "Following OpenCE"). A network version our wire
   doesn't play with still takes a release ("Following policy").
4. Delta Peer: the port, the flag, the handshake with fallback, fuzzing,
   platform keys and their policy, and its first capabilities (`platform`,
   `profile`, `ce_maps`, `moderation`). Shipped in 0.7.0b (`moderation` in
   0.7.1b); `chat` and `server_messages` (text chat, 0.8.0); the LAN superset
   is next.
5. The protocol choice (Auto, Delta only, OpenCE only). The browsers'
   protocol labels and a dedicated server's listing flag (128) are built;
   the choice in Server Setup and Delta-only rooms are not.
6. Moderators for dedicated servers: Delta Peer's `moderation`,
   site-granted roles through a server's link to the site (`sv_link`), and
   Delta Stats events with moderation in them. Shipped in 0.7.1b.
7. The legacy table as config. Built: wire IDs, the signed document and
   its tool, the game's loader (cache, Delta List, GitHub), the local
   override, the real keys, delivery over Delta Peer, epochs, and CI
   publishing after the cross-play gate.
8. The per-peer host experiment, and the proposal to OpenCE.
