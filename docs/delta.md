# Delta: Chupathingy's network family

See also [Delta: compatibility](delta-compatibility.md), the design for how builds play together.

Status (November 2026, for 0.9.0): in progress. Delta List, Stats, Link
and Control run under the names in the table below. The legacy number's
table and its signed loader are built, and the cross-play gate that
publishes it runs on every OpenCE raise (see "The legacy number" and
"Following OpenCE"). Hosts announce the newest number the signed table
proves ("Auto-announce"). Delta Peer is built: its port, the Delta tag in
the advertisement, the handshake with silent fallback, platform keys, and
the capabilities listed as built under "Capability registry". Delta Stats' event log, server moderators and linking
servers to the site shipped in 0.7.1b. The site's page for players:
https://stats.chupathingy.org/delta

Delta is the name for everything Chupathingy's machines say to each other
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
| **Delta Peer** | Chupathingy machines in one game | protocol major, capabilities, and our own messages | `delta_peer.c`, UDP port 5160 |
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
  on its loopback alias), a split screen game binds it to the machine alone
  as it does the game's (nobody else can join, so no port of that game is
  opened to the network; `listen_address.h`), and on the internet the
  invite tunnel carries it exactly as it carries the game's datagrams. The
  tunnel learns every port
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
- A host that speaks Delta puts a tagged block at the tail of its
  advertisement's reserved bytes (the Delta tag, "The advertisement" below),
  only while its Delta socket is open (another copy on the same address may
  hold 5160: that game is then hosted without Delta, and says so). OpenCE
  machines ignore it, and the game's own messages are not changed by it.
  Hosts before chupa-39a set the flag 0x04 instead (the flags byte at
  `HALO_PORT_ADVERTISED_FLAGS_OFFSET`); OpenCE has since given that bit a
  meaning of its own (a dedicated server), so only a dedicated server still
  sets it, for released clients ("The transition" below).

### Handshake

Right after a client joins (the legacy join, unchanged), if the host's
advertisement says it speaks Delta (its Delta tag, or the old layout's flag):

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
| 13 | `bomb_movement` | TRAITS' movement and melee traits (jump, gravity, melee reach), for a player whose machine and host both agreed to it |
| 14 | `bomb_traits` | TRAITS at 22 (Grifball test builds 2 and 3 sent it as 18, chat's CHAT_CONFIG). Room-wide with `bomb`: a host playing a Bomb game type drops every machine without both |
| 15 | `team_change` | changing teams during a game: a client asks its host (TEAM_REQUEST), the host decides and answers (TEAM_RESULT); see "Team changes" |
| 16 | `server_badge` | a dedicated server's signed badge: a client asks its host (BADGE_ASK), the host answers with its certificate and proof of its key (BADGE); see "Server badges" and [server-badges.md](server-badges.md). A host offers it only with a certificate |
| 17 | `object_sync` | what the game's messages leave out of the host's objects: every unit's shield and health (UNIT_VITALS) and the scenario names of the objects the host made (OBJECT_NAMES); see "Object sync" |
| 18 | `host_cheats` | not a feature: the host's console and cheats may change its public game while others are in it (`network.host_cheats`); announced on Delta List only, never in a HELLO, and Online Games shows "Host cheats on" |
| 19 | `host_rules` | the host's rules for its players' extras (HOST_RULES): names above heads, the follow camera, team changes; a client whose host says nothing plays by the Xbox's rules. See "Host rules" |
| 20 | `identity` | verified names: the host challenges (ID_CHALLENGE), each local player with a device certificate proves it (ID_PROOF), the host relays the card (ID_CARD) and every client checks it again. Only verified players show by their Delta profile. See "Verified names" |
| 21 | `party_teams` | a party's members tell their host their party's tag (PARTY_TAG), and a host that keeps parties together puts them on one team where the teams stay even. See "Party teams" |
| 22 | `voice` | voice chat (VOICE_UP, VOICE_DOWN, VOICE_CONFIG): a player talks on a channel, the host relays each frame by its rules without decoding it. Agreed per pair, so OpenCE machines and builds without voice are never sent any. See "Voice" |
| 23 | `reports` | a dedicated server takes its players' reports of other players (REPORT, REPORT_RESULT); see "Reports" |
| 24 | `mcc_maps` | MCC maps (maps_mcc and Steam Workshop items) and their game messages (OpenCE 43's numbers 97 to 100) in a 43-layout session. Room-wide for an MCC map: a host playing one drops every Delta machine without it (an older build, a console); a machine without Delta (OpenCE 43) plays by its own MCC support |

The registry lives in the repository next to the compatibility table and is
the single source for the bit numbers. Built so far: `platform`, `profile`,
`ce_maps`, `moderation` (see "Moderation"), `chat` and `server_messages`
(see "Chat"), `bomb`, `bomb_movement` and `bomb_traits` ([bomb.md](bomb.md)),
`ping`, `team_change`, `object_sync`, `host_cheats`, `host_rules`,
`identity` (see "Verified names"), `party_teams` and `voice` (see "Voice"). No
build sets `coop`, `ai_sync`, `vote` or `server_badge` yet. The first two's values are
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
  since the ID is the same in every game. It proves nothing, so builds
  with `identity` never show a name from it: only a verified player shows
  by their profile ("Verified names").
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
Series in developer mode), 13 `pc_freebsd` (the PC build for FreeBSD,
[port/unix](../port/unix/README.md)), 14 `ps3`, 15 `ps4`, 16 `ps5`, 17 `n64`,
18 `pc_legacy` (DOS and Windows 9x PCs), 19 `steamlink` (the 2015 Steam
Link), 20 `ios` (iPhone and iPad, and the Vision Pro in iPad compatibility
mode), 21 `tvos` (the Apple TV). 10 to 12 and 14 to 21 are ports
others build on ours; their policy rows are placeholders until those ports
play, as the consoles' are. 13's is a PC's. How a port gets a number: see
"Platform numbers" below.

The original Xbox, Xbox 360 and Wii U builds are Delta Peer clients
(`HALO_CONSOLE_DELTA`). Each sends its own platform (6, 7 or 8) and a
console's capabilities: the platform key, text chat and the server's lines,
and a profile only if its player set one up. They never host on Delta, and
offer no `ce_maps`, team changes or moderation. Their build is "Warthog"
and the version.

### Platform policy

Console builds play with PC builds, with caveats Delta controls. The
caveats are policy, one table in `delta.h` (`DELTA_PLATFORM_POLICY`), the
defaults every machine's key starts from:

| Platform | Hosts at most | Joins at most | Opted in | Memory | Co-op |
| --- | --- | --- | --- | --- | --- |
| PC (Windows, macOS, Linux, FreeBSD) | 128 | 128 | 128 | 12 (4 GB) | yes |
| Steam Deck | 128 | 128 | 128 | 14 (16 GB) | yes |
| Android | 128 | 128 | 128 | 11 (2 GB) | yes |
| unknown | 128 | 128 | 128 | 0 | yes |
| Xbox | 16 | 32 (16 on a stock build) | 128 | 6 (64 MB) | no |
| Xbox 360, Wii U | 32 | 32 | 128 | 9, 10 (placeholders) | yes |
| Switch | 16 | 16 | 128 | 12 (placeholder until its port plays) | yes |
| PSP, Vita | 16 | 16 | 128 | 6, 9 (placeholders) | no |
| Xbox Series (developer mode) | 128 | 128 | 128 | 12 | yes |
| PS3 | 16 | 16 | 128 | 8 (256 MB; placeholder) | no |
| Steam Link | 0 (does not host yet) | 16 | 128 | 8 (256 MB) | no |
| iOS, Apple TV | 128 | 128 | 128 | 11 (2 GB) | yes |
| PS4, PS5 | 16 | 16 | 128 | 12, 13 (placeholders) | yes |
| Nintendo 64 | 0 (never hosts) | 8 | 16 | 3 (8 MB; placeholder) | no |
| Legacy PC (DOS, Windows 9x) | 16 | 16 | 128 | 7 (128 MB; placeholder) | no |

Co-op: a host offers no network co-op (the Map screen's campaign levels
refuse, and say why in the log) while a machine of a platform without it is
in its game, nor on such a platform itself (`delta_peer_room_coop`). Of the
platforms that play today, only the original Xbox is kept out.

The original Xbox hosts 16 players at most, on LAN or online, and joins
games of up to 32. A console build joins no more than its own tables hold
(`HALO_CONSOLE_PLAYERS`), so a stock Xbox build of 16 claims 16 in its key.

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

### Platform numbers

Every build that speaks Delta says what it runs on: one number from the
registry above. The scoreboard picks its logo from it, the site sorts
games by it, and hosts use its policy row to keep weaker machines safe.
This section is for people who port Chupathingy to something new.

**Numbers are never reused.** A number stays with its platform for good,
even if that port stops. New platforms go at the end of the list.

**Ask before you ship.** Don't pick a free number yourself: two ports could
pick the same one. Open an issue on
[ChupathingyCE/chupathingyce](https://github.com/ChupathingyCE/chupathingyce/issues),
or ask on the Discord. Tell us the platform, your port, and roughly how much
memory the game gets there. We add the number to `delta.h` and this page,
and the site learns it too.

**Identify your build.** Build with `-DHALO_DELTA_PLATFORM=_delta_platform_<name>`
(`_delta_platform_ps3`, say). The game sends that number in its platform
key. Without it, the game picks a number from what it's built for (Windows,
Mac, Linux, Android, FreeBSD, iOS, Apple TV and our console builds), and `unknown` (0)
otherwise. A build with no number of its own still plays;
it just shows no logo and gets the unknown row.

**What the policy row means.** Each platform has one row in
`DELTA_PLATFORM_POLICY` (`delta.h`):

- **Hosts at most:** the most players a game it hosts can have. 0 means it
  doesn't host.
- **Joins at most:** the most players a game it joins can have. A host
  keeps its game at or under this.
- **Opted in:** the limit when the player turns the caveats off
  (`network.platform_limits = "off"`).
- **Memory:** log2 of the megabytes the game gets. 6 is 64 MB, 8 is 256 MB,
  12 is 4 GB.
- **Co-op:** whether it plays network co-op. If not, a host offers no co-op
  while it's in the game.

A new port's row starts careful: 16 players, no co-op on small machines.
Once the port plays well, tell us what it handles and we raise it. A port
can also claim lower limits in its own key; a host takes the lower value.

**The numbers today:**

| Number | Name | Platform | Port |
| --- | --- | --- | --- |
| 0 | `unknown` | not known | |
| 1 | `pc_windows` | Windows | ours |
| 2 | `pc_macos` | macOS | ours |
| 3 | `pc_linux` | Linux | ours |
| 4 | `android` | Android | ours |
| 5 | `steam_deck` | Steam Deck | ours |
| 6 | `xbox` | original Xbox | ours (warthog-xbox) |
| 7 | `xbox360` | Xbox 360 | ours (warthog-360) |
| 8 | `wiiu` | Wii U | ours (warthog-wiiu) |
| 9 | `switch` | Nintendo Switch | warthog-switch |
| 10 | `psp` | PSP | Sheepdog01 |
| 11 | `vita` | PS Vita | Lord BirchWood |
| 12 | `xbox_uwp` | Xbox One and Series, developer mode | ReviveMe |
| 13 | `pc_freebsd` | FreeBSD | Renkoto (port/unix) |
| 14 | `ps3` | PlayStation 3 | Mayo |
| 15 | `ps4` | PlayStation 4 | community port |
| 16 | `ps5` | PlayStation 5 | community port |
| 17 | `n64` | Nintendo 64 | JackT |
| 18 | `pc_legacy` | DOS and Windows 9x PCs | JackT (Legacy PC) |
| 19 | `steamlink` | Steam Link (2015) | Jizzy |
| 20 | `ios` | iPhone, iPad, Vision Pro (iPad compatibility mode) | Jizzy (port/ios) |
| 21 | `tvos` | Apple TV | Jizzy (port/ios) |

The PSP, Vita, PS3, PS4 and PS5 share the PlayStation logo on the
scoreboard. Nintendo 64, Legacy PC, Steam Link, iOS and Apple TV have no
logo yet.

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
| 11 | MOD_STATE | host to client | role (1), the host's actions (1: 0x01 it takes LOCK and UNLOCK, 0x02 joins are locked now; 0 from an older host), reserved (2), permissions (4), longest timed ban in minutes (4) |
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
| 22 | TRAITS | host to client | kind (1: 0 none, 1 Bomb), reserved (3), everyone's then the carrier's player traits, 12 bytes each: health, damage, resistance, speed (2 each, percent), shields (1: 0 unchanged, 1 none, 2 normal, 3 overshield), highlight (1), reserved (2); then, from the `bomb_movement` builds on, a version (1), reserved (3), and everyone's then the carrier's movement traits, 8 bytes each: jump, gravity, melee reach (2 each, percent; 0 unchanged), reserved (2), 48 bytes in all (older builds read the first 28); values past their ranges ([bomb.md](bomb.md)) refused. Grifball test builds 2 and 3 sent it as 18, which is CHAT_CONFIG's (a host drops those builds from a Bomb game: `bomb_traits`) |
| 23 | TEAM_REQUEST | client to host | sequence (4, above the session's last), player (1, the game's player list index), team (1: 0 red, 1 blue, 255 the other one), reserved (2) |
| 24 | TEAM_RESULT | host to client | sequence (4, the TEAM_REQUEST's), result (1: 0 changed, 1 at the next respawn, 2 the host takes none, 3 the teams would be uneven, 4 no teams, 5 too soon, 6 not now, 7 no such player of the machine, 8 already on that team, 9 the last player of their team), team (1, the player's now), seconds until the player may change again (2; with 5) |
| 25 | BADGE_ASK | client to host | nonce (32, fresh for each session) |
| 26 | BADGE | host to client | nonce (32, the BADGE_ASK's), signature (64, the certificate's server key's, of the proof below), certificate length (2, at most 143; 0 for none), proof (1: 2, "chupathingy server badge proof v2\n", the nonce, the host's invite and the session, [server-badges.md](server-badges.md); 0, the first drafts' nonce alone, shows no badge), reserved (1), then the certificate |
| 27 | UNIT_VITALS | host to client | count (1, at most 98), flags (1: 0x01 the last datagram of a full pass), reserved (2), then 12-byte entries: the host's object (4, its datum index, which a client's copy shares), flags (1: 0x01 shield depleted, 0x02 shield charging, 0x04 shield over charging), reserved (1), body vitality (2), shield vitality (2), reserved (2); vitality in 16384ths, as the game's own unit states have it |
| 28 | OBJECT_NAMES | host to client | total (2, the host's named objects and removed names in all, at most 512), first (2, this datagram's first entry's place among them), count (1, at most 98), reserved (3), then 12-byte entries: the host's object (4; 0xFFFFFFFF for a removed name), its scenario name index (2), flags (1: 0x01 removed, a name the host had an object of this game and has none of now; others ignored), and for a removed name its unit's last vitals: shield flags (1, as UNIT_VITALS's), body vitality (2), shield vitality (2) (full for an object that was not a unit; 0 for the others) |
| 29 | HOST_RULES | host to client | version (1; 1 now, 0 refused, a later one read as far as this build knows it), flags (1: 0x01 names above heads allowed, 0x02 the follow camera allowed, 0x04 late joiners wait for the next game, 0x08 joins are locked; others ignored), team changes (1: 0 off, 1 balanced, 2 allow), their margin (1, 0 to 16), reserved (4); values past their ranges refused. Sent at once when they change and every 5 seconds |
| 30 | ID_CHALLENGE | host to client | version (1; 1 now, 0 refused, a later one read as this), binding length (1, at most 64), reserved (2), nonce (32: this session's), binding (printable ASCII) |
| 31 | ID_PROOF | client to host | version (1), local player (1, 0 to 3), certificate length (1: 0 withdraws the local player and nothing follows; else 148 to 168), name length (1, at most 20), flags (1: 0x01 bound), reserved (3), time (4, Unix seconds), signature (64), issuer certificate (CISS, 112), device certificate (CDEV, the length's bytes), name (the handle in the player's capitals: `A-Z a-z 0-9 _ -`) |
| 32 | ID_CARD | host to client | version (1), machine index (1), local player (1, 0 to 3), certificate length (1: 0 withdraws the card and nothing follows; else 148 to 168), name length (1, at most 20), reserved (3), flags (1, the proof's), reserved (3), time (4, the proof's), nonce (32: the one the host challenged that machine's session with), signature (64), CISS (112), CDEV, name. At most 412 bytes |
| 33 | PARTY_TAG | client to host | the party's tag (8; zeros: in no party); a longer payload is read as far as this build knows. Sent once both agreed to `party_teams`, at once when it changes, and every 5 seconds while in a party |
| 34 | VOICE_UP | client to host | sequence (2, the speaker's own count), length (1, 0 to 255: the Opus frame's bytes; 0 none), channel (1: 0 none, 1 team, 2 proximity, 3 everyone, 4 party; none with no frame), hears (1: 0x01 team, 0x02 proximity, 0x04 everyone, 0x08 party: the channels this machine plays; 0 none), reserved (3), then the frame (length bytes); with no frame, a party's tag (8) may follow (none: all 0 or left out). The length must be the bytes that came, exactly |
| 35 | VOICE_DOWN | host to client | sequence (2, the speaker's), speaker's machine (1), speaker's player (1, the game's player list index; 255 none), route (1: 1 global, played as said; 2 proximity, played from where the speaker's player is), channel (1, as VOICE_UP's), length (1, 1 to 255), reserved (1), then the frame. The length must be the bytes that came, exactly |
| 36 | VOICE_CONFIG | host to client | version (1; 1 now, 0 refused), channels (1: 0x01 team, 0x02 proximity, 0x04 everyone, 0x08 party, the channels the host relays; 0x10 lobby, everyone hears everyone outside a game), flags (1: 0x01 this machine is muted), quality (1, 8 to 64 kbps), proximity (2, tenths of a world unit, 50 to 1000), reserved (2), a timed mute's seconds left (4; 0 none, or until the game ends); values past their ranges refused |
| 37 | REPORT | client to host | sequence (4, above the session's last), target's machine (1), target's player (1, the absolute index of the player's datum in the host's player data, as the game's player datum index has it), reason (1: 1 cheating, 2 abuse in chat or voice, 3 griefing or betraying, 4 an offensive name, 5 something else), flags (1: 0x01 the reporter would attach a clip), reserved (16: zeros; a host ignores them) |
| 38 | REPORT_RESULT | host to client | sequence (4, the REPORT's), result (1: 0 taken, 1 too soon, 2 no such player, 3 not taken here, 4 already reported), flags (1: 0x01 send the clip: the server is linked to the site), reserved (2), clip token (16, the host's: zeros without 0x01). A host before the token sent the first 8 bytes alone, and its 0x01 is read as no clip |
| 39 | HOST_NUMBER | host to client, if both agreed to `mcc_maps` | version (1; 1 now, 0 refused, a later one read as far as this build knows it), reserved (1), the OpenCE network version the host's game is played at (2, little-endian; 0 refused), reserved (4). Sent at once when it changes, every second for the 20 seconds after, then every 5 seconds. When a host's lobby moves to an MCC map (its game at the MCC number, 44 from `chupa-44a`) or back, a client with `mcc_maps` plays the host's next settings record and messages at the new number without leaving (a record of another size from such a host is passed over, not fatal); the host sends its record again every 2 seconds for 20 seconds. A client that still has the map before never starts the game: the pregame load timeout drops it. Any other machine that joined at the old number is dropped with a notice |

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
| verified names: ID_CHALLENGE | at the handshake, and every 5 s |
| verified names: ID_PROOF a host takes (and checks) from one machine | 4 a second, 4 at once |
| verified names: ID_CARD to a client | at once when a card changes, every card again every 30 s, 8 a frame at most |
| verified names: a client's proof sent again, while its card has not come | every 10 s |
| verified names: a client proves again anyway | every 15 minutes |
| verified names: a withdrawn card kept (so every client hears it) | 30 s |
| voice: VOICE_UP a host takes from one machine | 50 a second, 10 at once, a budget of its own (the messages' above are left alone) |
| voice: a frame's bytes | at most `min(kbps * 1000 / 8 / 50 * 2 + 16, 255)`: 136 at 24 kbps |
| voice: machines talking at once | 8 on a dedicated server, 4 on a player's host (a machine talks for 500 ms after its last frame; one talking keeps its place, a new one waits) |
| voice: the channels heard (VOICE_UP with no frame) | every 2 s; a host sends a machine voice only while it said them within 6 s |
| voice: VOICE_DOWN a client takes from its host | 400 a second, 80 at once, a budget of its own |
| voice: VOICE_CONFIG | at the handshake, on a change (at most every 250 ms), and every 10 s; a client that hears none for 25 s has no voice |
| voice: datagrams read a frame | 256, beside the 64 of everything else |

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
  `maps_md/`), 3 Halo PC retail (`<file>@pc`, `maps_pc/`), 4 an Xbox map
  pack's (`<file>@<pack>`, `maps_<pack>/`: Halo CE+'s `bloodgulch@cex` in
  `maps_cex/`; named `<file>.<pack>` in a MAP, since a name has no `@`, and
  hashed as a Halo PC map is). A family number a build does not know is
  carried as it is, and its client checks nothing.
- **Names on the legacy wire** stay OpenCE's: a host names a Custom Edition
  map `custom_maps\<file>` in the game's settings (OpenCE's build-145 form,
  network version 22), and HaloMD and Halo PC retail maps
  `maps_md\<file>.md` and `maps_pc\<file>.pc`, and a map pack's
  `maps_<pack>\<file>.<pack>` (with the game record's map version its
  file's fingerprint, `halo_map_packs.h`), which OpenCE's clients do not
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
hosts offers none over the wire. Its host has the same Moderation screen
for its own game (kick and ban, no sign-in), done on the host's machine
with the console's kick and ban. OpenCE players can be warned, kicked and banned like
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
  it, 8 lock joins, 9 unlock them (no target; a host that takes them says
  so in MOD_STATE); a host refuses one it does not know. Whether the key may do it
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
that takes server messages but not chat is sent only those. OpenCE's own
text chat is bridged to it (below): the two meet in the host, and nothing
of Delta's goes into the game protocol.

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
  relaying their lines and tells their game in CHAT_CONFIG. A player who
  hosts does the same by typing `/mute <player> [minutes]` or `/unmute
  <player>` as a line (as OpenCE's hosts do); an OpenCE player is told by
  the host's notice. Typed in someone else's game, the commands are never
  sent: OpenCE hosts would show them to everyone. A player's own
  mutes are their game's alone and never sent.
- **The client checks again.** A CHAT_LINE comes only from its host, of its
  session, at most 10 a second; its text and name must pass the same text
  checks, and it is shown in the HUD's font (a character the font lacks as
  `?`), its blocked words starred out or the line hidden as the player's
  `chat.filter` says.
- **Nothing is kept.** Neither the host nor the dedicated server writes
  down what is said; the site never sees it. Only the admins' own actions
  (`sv_say`, mutes) are in the server's audit file.
- **OpenCE's chat (the bridge).** OpenCE's text chat goes over the game's
  distributed messages, which the bridge takes as they are (their format
  as of OpenCE's CC0 version of 2026-10-18; message types 87 to 89, which a
  build without them drops as unknown): `chat_up`, a client's line to its
  host (a 68-byte entry: the player's absolute index, flags, two bytes of
  0, and 64 bytes of text); `chat_down`, the host's line to a machine that
  hears it, the same entry (the flags: bit 0 the team, bit 1 the vehicle,
  bit 2 the host's notice, its text alone); and `chat_config`, the host's
  settings to each client (4 bytes: bit 0 chat allowed, bit 1 links
  allowed), sent as they change and every 3 seconds. Each takes one entry,
  reliably, over the sender's stream alone. Their text is printable ASCII
  without `|`, at most 63 characters.
  - **A Chupathingy host** sends each machine that has no Delta session
    taking its lines `chat_config`, from its own settings alone: allowed
    while its chat is on and `network.chat_legacy` (a dedicated server's
    `chat_legacy`, both on by default) lets OpenCE players chat, and the
    legacy table's kill switch hasn't turned Delta's chat off. Links are
    allowed, as Delta chat has no rule against them. OpenCE's own rule of
    no chat in a game listed in the server browser is theirs: our host
    doesn't apply it (an OpenCE client that joined from their browser keeps
    its chat off anyway). It takes `chat_up` from such a machine's own
    player only, and checks it as it checks CHAT_SAY: the same budget and
    cooldown, the repeat check, the text, the machine's mute, then its mode
    and filter. A refusal goes back as the host's notice. A line taken (and
    every line of a Delta machine, and the host's own player's) goes to
    each Delta machine as CHAT_LINE and to each OpenCE machine that may
    hear it as `chat_down`, its UTF-8 made ASCII (Latin-1's letters without
    their accents, typographic quotes and dashes as plain ones, anything
    else `?`) and cut to 63 characters. A server line (`sv_say`, the
    welcome) goes to OpenCE machines as the host's notice. With OpenCE
    players left out OpenCE machines are sent nothing but the setting, and
    a refusal to one that says a line anyway. A vehicle line from OpenCE is
    a team line here (Delta has no narrower scope), and a team line in a
    game without teams is everyone's, as OpenCE has it.
  - **A Chupathingy client** whose host has no Delta chat (an OpenCE host)
    follows that host's `chat_config`: it says its lines as `chat_up` once
    the host allows chat, and shows each `chat_down` as a line of the
    host's game (a notice as a system line). The player's own settings
    decide the rest, as for Delta's lines: `chat.enabled`, `chat.show`, the
    filter and their mutes. It takes nothing but the host's stream. Such a
    host's console notices (the game's notice message, which an OpenCE
    server uses for its server messages and its message of the day) show
    in the chat too, as a system line, blank ones and repeats within 15
    seconds left out; a host with Delta chat keeps them on the console. A host
    with Delta chat is never bridged: its lines come over Delta alone.
  - A message of the wrong size is dropped and the log says so once (the
    machine's index, never its address); nothing about chat ever ends a
    connection.

The game's side is `port/linux/game/chat.c` (keys, the log, routing, the
bridge), `port/linux/src/delta_chat.c` (the filter and the host's rules)
and `port/linux/src/delta_chat_bridge.c` (OpenCE's entries, the text's
conversion, the bridge's checks), both tested in `delta_test.c`, and the dedicated server's `sv_say`, `sv_chat`, `sv_mute`
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
  Chupathingy) is never asked, and its players' pause menus have no CHANGE
  TEAMS. A host whose rule is off (HOST_RULES, "Host rules" below) is not
  offered either.
- **The host's rule.** `network.team_change` for a player's host,
  `HALO_DEDICATED_TEAM_CHANGE` for a dedicated server: `off` (the default,
  as on the Xbox), `balanced` (no change that leaves one team more than the
  margin, 1 unless set, bigger than the other) or `allow`; a player's changes at
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
  player already dead changes at once. In a round-based game type (Bomb,
  Grifball, Open Pitch) the change waits for the player's next death or the
  next round, whichever comes first, so the round plays out: as a round
  starts the host kills a waiting player's unit as nobody's kill, and they
  start the round on the new team. A bomb carrier keeps the bomb for their
  own team until then (their death drops it; a new round takes it back).

The game's side is `port/linux/game/team_change.c`, its pause menu row in
all the menu sets (`port/linux/game/menu_tags.c`), and the dedicated
server's settings and `sv_team` (`server/docs/settings.md`).

### Host rules

Chupathingy plays multiplayer as the Xbox did. A few extras change what a
player sees of the others, so the host decides them for everyone in its
game, over `host_rules` (bit 19):

- **The host says them.** HOST_RULES goes to each client that agreed to
  `host_rules`, at once when they change (a dedicated server's `sv_set`)
  and every 5 seconds. A player's host takes them from its config.toml
  (`network.allow_player_names`, `network.allow_follow_camera`, its Host
  Setup), a dedicated server from its settings (`allow_player_names`,
  `allow_follow_camera`, `server/docs/settings.md`).
- **Names above heads.** A player's `display.player_names` ("all",
  "allies", "enemies") shows only while the host allows it. Without it a
  name shows only on the player aimed at, as on the Xbox.
- **The follow camera.** A player's `display.follow_camera` (watching a
  living player while they wait to respawn) works only while the host
  allows it. Without it the death camera alone, as on the Xbox.
- **Team changes.** The host's rule for CHANGE TEAMS ("Team changes"
  above) rides along, so a client offers it in the pause menu only where
  the host allows it. An older Chupathingy host that takes team changes
  but says no rules is asked as before.
- **Late joiners wait** (0x04, off by default: a player's host's
  `network.late_join_hold`, a dedicated server's `late_join_hold`). A
  player who joins a game in progress doesn't spawn until the next game
  starts. They watch through the follow camera (LB and RB switch who),
  whatever their own `display.follow_camera` says, with "Waiting for the
  next game" under it. The host holds their spawn, so an OpenCE player who
  joins late waits too and sees Halo's own death view, with a line in
  their chat saying why. Nobody else's game changes. A joiner spawns at
  once when no one else is playing, the wait ends with the game, and it
  never lasts more than 5 minutes. A player who was in the game, dropped
  and came back (the same name) isn't held.
- **Joins are locked** (0x08): the host takes no new machines until it's
  unlocked (a dedicated server's `sv_lock`). Players already in stay, and
  a dropped player can't come back while it's locked (Custom Edition
  players are kept out too). A lock ends by itself 10 seconds after the
  host stops hosting or nobody it was for is in (a dedicated server's
  players, or a player's host's players of other machines). The host's Delta
  List announcement says `locked=1`, which the site keeps as
  `joins_locked` (games.txt's field 25), and the game list shows it.
- **A host that says nothing** (OpenCE's, an older Chupathingy, a console
  without Delta) gets the Xbox's rules: no extras. A game played alone
  (split screen) is its own host's.

The game's side is `port/linux/game/host_rules.c` and
`port/linux/game/late_join.c`.

### Reports

A player reports another player to a dedicated server's moderators over
`reports` (bit 23). A host offers it only when it takes reports (the
dedicated server registers them; a game a player hosts offers none).

- **REPORT** names the player by their machine and the absolute index of
  their player datum (the low 16 bits of the game's player datum index),
  and gives a reason from a short list: 1 cheating, 2 abuse in chat or
  voice, 3 griefing or betraying, 4 an offensive name, 5 something else.
  There is no free text. Its sequence must be above the session's last.
- **The host decides.** It refuses a report of a player who isn't there,
  of the reporter themselves, a second report of the same player in one
  game from one reporter, more than 3 a minute or 10 a game from one
  reporter, and more than 5 a game of one player from everyone
  (REPORT_RESULT says which). It counts by who reports and who is
  reported: a player's verified account when their proof is bound to this
  host ("Verified names"), else that player in this game, never by the
  machine's slot. So a player who joins a slot someone left starts with
  their own counts, and a verified player who leaves and comes back keeps
  theirs. The counts start over with each game. Each report it takes goes
  to its moderators' queue with what it knows: both players' game names,
  their verified handles when they proved one bound to this host, the map,
  the game type, and the host's refusal counts for the reported machine
  (`sv_evidence`). Never an address. The queue keeps 200: when they are all
  open, a new report is refused rather than one pushed out.
- **OpenCE players** can be reported by their game name. Their reports stay
  on the host: they never go to the site. A report by an OpenCE player isn't
  possible (they can't send REPORT).
- **Clips.** A reporter may attach a clip (the last one saved, or one from
  `clips/`); REPORT then says so. When the host is linked to the site
  (Delta Control) and the report is about a Delta player, REPORT_RESULT's
  0x01 asks for the clip and carries its token: 16 random bytes the host
  makes, never the reporter, and puts in the report it forwards. The copy
  uploads the clip to the site with that token (`/v1/game/clip/start`,
  then `/v1/game/clip/upload`). The site takes a clip only for a token a
  report of a linked server names, once, and keeps it beside that report,
  for that server's moderators and the site's admins alone, and deletes it
  after 14 days. A host that isn't linked takes the report without the
  clip.
- **Linked servers** forward reports to the site with their next poll
  ("The link to the site", `reports`), but only those about a Delta
  player.

The clip upload, as `/v1/game/*` (the copy's key in the body, or its
device token):

- `POST /v1/game/clip/start` `{"key", "token", "size", "seconds"}` answers
  `{"upload", "max"}`: an upload ID good for 30 minutes. 409 while no
  report names the token yet (the report rides the server's next poll: the
  copy asks again every 5 seconds, for up to 2 minutes), 413 when `size` is
  past the cap (48 MB), 429 past the copy's or the server's clips a day,
  507 when the site's clip store or the server's share of it is full. The
  site counts the bytes it has, not the sizes uploads promised.
- `POST /v1/game/clip/upload?upload=<id>`, the MP4 as the body
  (`video/mp4`, exactly `size` bytes), answers `{"ok": true}`. The site
  refuses a body that isn't an MP4 (its top-level boxes, `ftyp` first and
  a `moov`), and drops an upload that sends nothing for a minute.

The game's side is `port/linux/game/report_player.c` (the Delta Guide's PLAYERS
HERE and the scoreboard's REPORT), the dedicated server's
`server/src/server_reports.c` (`sv_reports`, the control panel's Reports).

### Server badges

A dedicated server's OFFICIAL, DEVELOPER or COMMUNITY badge, over
`server_badge` (bit 16). The badge is a certificate signed with our badge
key for the server's Delta Control link key; the format, the keys and how
one is issued are in [server-badges.md](server-badges.md).

- **Asking.** A client sends BADGE_ASK with a fresh nonce once the
  handshake is done (again every 2 seconds, 3 times at most, while
  unanswered). The host answers BADGE with its certificate and its link
  key's signature of the nonce, at most once a second for a machine. A
  host without a certificate does not offer `server_badge`.
- **Checking.** The client takes the badge only if the certificate is a
  badge key's, in date, of a tier it knows, and the signature is the key
  the certificate names, of the nonce, the invite the client joined and
  this session (proof 2). A host that does not offer the badge, does not
  answer in time, or fails the check shows no badge in Online Games from
  then on, whatever the list says; one that proves a badge shows the
  list's only while the list names the same tier, label and server key.

The game's side is `port/linux/src/server_badge.c` and
`port/linux/src/delta_peer_game.c`; the dedicated server's certificate is
`server_badge.cert` in its data folder (`server/platform/control_link.c`).

### Voice

Voice chat between Chupathingy players, over `voice` (bit 22). The host
decides who hears whom; a client never does. Voice is off on each player's
copy until they turn it on (`audio.voice_chat`: retail had none), and push
to talk is its default way once on. Nothing is recorded: frames go to the
host, which sends them on and keeps nothing, and nothing of voice goes to
the site.

- **Channels.** A player talks on one channel at a time
  (`audio.voice_channel`; B, or the D-pad left, switches it):
  - **PARTY** (4): their party. The host relays a party frame to the
    machines whose VOICE_UP said the same party's tag, wherever their
    players are. The tag is the party's to give (8 bytes, never an ID the
    site keeps; the same tag party teams' PARTY_TAG carries): no party, no tag, and the channel can't be picked.
    Anyone who knows a tag hears that party, so a tag must be at least 64
    bits from the system's secure random source (never a counter, a time,
    a name or an ID), and nothing ever echoes it: a host never puts it in
    a roster or a VOICE_DOWN, and party teams' messages never send it back
    to other machines.
  - **TEAM** (1): their team in the game; everyone in co-op; nobody in a
    game without teams.
  - **PROXIMITY** (2): anyone within the host's proximity whose player is
    alive, the speaker too, played from where the speaker's player stands
    (route 2) through the listener's game sound: its distance, pan or
    Headphone 3D, and the room's echo.
  - **EVERYONE** (3): everyone in the game, only when the host allows it
    (off unless set).
  - Outside a game (the pregame lobby, the postgame) everyone hears
    everyone on any channel but party, if the host's lobby voice is on.
- **The host's rules** go to each client that agreed to `voice` in
  VOICE_CONFIG: the channels it relays (a player can't talk on another,
  and the menus show it dimmed with one plain reason), lobby voice, its
  quality and its proximity. A player's host takes them from its
  config.toml (`network.voice`, `network.voice_team`,
  `network.voice_proximity`, `network.voice_everyone`,
  `network.voice_party`, `network.voice_lobby`, `network.voice_range`,
  `network.voice_kbps`), a dedicated server from its settings (`voice`,
  `voice_team`, `voice_proximity`, `voice_everyone`, `voice_party`,
  `voice_lobby`, `voice_range`, `voice_kbps`; `sv_set` and the control
  panel's Settings change them at once). A host with `network.voice` off
  doesn't offer the capability at all. A dedicated server's `voice` is off
  unless its host turns it on (`voice = true` in its settings, or
  `sv_set voice 1`), since relaying costs its upload.
- **Listening.** A client says the channels it plays and its party's tag in
  a VOICE_UP with no frame every 2 seconds (any frame it says counts too).
  The host sends it a frame only on a channel it plays, and nothing once
  it has said nothing for 6 seconds: a player with voice off, or with a
  channel's volume at 0, costs the host no upload. A build without voice
  never agrees to `voice`, so it is never sent any.
- **Relaying.** The host checks each VOICE_UP's session, the voice budget,
  its size (the length exactly the bytes that came, no more than its
  quality's cap), the channel (one it relays now), the machine's mute and
  the talkers at once (8 on a dedicated server, 4 on a player's host, whose
  upload is a home connection's), then sends the frame to each machine that may hear it,
  with the speaker's machine and player named by the host, never by the
  frame. It never decodes a frame and never sends one back to its
  speaker. A player's host is a speaker and a listener like any other.
- **Mutes.** A player muted by a moderator or the host (chat's mute,
  MOD_ACTION 6, `sv_mute`, `/mute`) is muted in voice too: the host relays
  none of their frames and tells them in VOICE_CONFIG (flag 0x01, and the
  seconds left), so their game stops sending. A player's own mutes (the
  chat log's, the Delta Guide's PLAYERS HERE) are their game's alone and never sent.
- **The codec** is Opus (our own copy of Xiph's release), set up as
  OpenCE's voice is, so that a frame could pass between the two unchanged:
  48 kHz mono, 20 ms frames, one a datagram, VOIP with the voice signal,
  variable bitrate at the host's quality, in-band FEC for 10% loss,
  complexity 5. Each speaker heard has its own decoder and a 60 ms jitter
  buffer; a lost frame is rebuilt from the next one's FEC, or concealed.
- **What it costs.** At 24 kbps a frame is about 60 bytes, about 105 on the
  wire with the headers: about 5 KB a second for each talker to each
  listener (a host's upload is that for each pair). Encoding takes about
  0.12 ms a frame on an Apple M4 (0.6% of a core while talking), decoding
  about 0.015 ms a frame for each speaker heard.

The game's side is `port/linux/game/voice_game.c` (the channels, the host's
rules and its view of the game), `port/linux/src/voice.c` (the microphone,
Opus and the voices' playback, through the game's mixer, `dsound_sdl.c`)
and `port/linux/src/voice_jitter.c`; Delta Peer's checks and routing are in
`delta_peer.c` (`delta_peer_voice_route`), all tested in `delta_test.c` and
`voice_test.c`. A dedicated server relays without any audio or Opus of its
own. The consoles leave voice out for now (`HALO_CONSOLE_NO_VOICE`): a
console is never sent any.

### Voice bridge (planned)

Not built. This is the behavior a bridge between OpenCE's voice and ours
must match, written from how their released clients and hosts behave (as
their voice shipped on 2026-10-09, before their license changed), so that
it can be built later without their code. Nothing of it is in any build,
and nothing here changes the game protocol: their voice already rides the
game's own distributed messages, and a build that doesn't know them drops
them.

**One module.** The bridge is one file (`opence_voice.c`), the only place
that knows OpenCE's voice messages. To the rest of voice it is "a frame from
machine M with sequence S on a channel" and "send this frame to machine L
with route R". For each machine it says which voice it speaks: Delta's,
OpenCE's or none. A machine that agreed to `voice` over Delta Peer always
uses Delta's.

**Their messages.** Three of the game's distributed message types, after
the usual 8-byte distributed header (the message header of 2 bytes with
type 2, the distributed type, a count of 1, the game time; little-endian):

| Type | Name | Way | Channel | Body |
| --- | --- | --- | --- | --- |
| 80 | voice_up | client to host | datagram | key (4), sequence (2), length (1), pad (1), then the Opus frame |
| 81 | voice_down | host to client | datagram | key (4), sequence (2), speaker machine (1), speaker player (1, absolute index; 0xFF none), route (1: 1 global, 2 proximity), length (1), pad (2), then the Opus frame |
| 82 | voice_config | host to client | the client's reliable stream | key (4), flags (1: 0x01 lobby voice), mode (1), kbps (1), pad (1), proximity in tenths of a world unit (2), pad (2) |

Their fixed parts are 8, 12 and 12 bytes.

**What their host does,** and so what ours must do for an OpenCE client:
- It gives each machine a random 32-bit key, never 0, when it first sees
  it, and says it only in voice_config, over that machine's reliable
  stream. Every voice_up must carry the key, and every voice_down to that
  machine carries it.
- It says voice_config when it first sees a machine, whenever its settings
  change, and every 10 seconds. Ours sends it to every machine that did not
  finish the Delta handshake. Its mode byte: 0 when our voice is off;
  otherwise always 4 (their "team, enemies near" mode). Their client uses
  the mode only to decide whether it may talk, and our host stamps each
  frame's route itself, so 4 is right for any of our rules. Never 5 or
  more, which their client reads as off. The lobby flag, quality and
  proximity are ours as they are.
- It takes voice_up on both its datagram and stream paths, before any "a
  game is running" check, so voice works in the lobby. It checks the key,
  that the length byte is the bytes that came, the size cap
  (`min(kbps * 1000 / 8 / 50 * 2 + 16, 255)`), 50 frames a second with 10 at
  once, that voice is on, and the 8 talkers. A voice_up of length 0 means
  "I hear voices".
- It sends voice_down only to a machine that sent a voice_up (empty or
  not) within 6 seconds and has its key; never a machine its own voice.
  It never decodes.
- An OpenCE player has no channels. Their frames are taken as said on
  TEAM in a team game and PROXIMITY otherwise, routed by our rules, and
  sent to them with our route. Party frames never go to them.

**What their client does,** and so what ours must do on an OpenCE host
(no Delta handshake):
- Take voice_config only from the host's stream; forget it after 25
  seconds without one (voice off). Voice is shown as available only while
  one has been heard, and the mode says whether push to talk works.
- Once configured, send an empty voice_up every 2 seconds, and each frame
  as voice_up with the key.
- Play voice_down only with its key, the right length, and not its own
  machine; route 1 as said, route 2 from where the speaker is.
- The channel row shows one choice, the host's, with "Channels need a
  Chupathingy host".

**Mismatches fail silent.** A frame that fails any check (size, key,
length, route, header) is dropped and counted, never mended, and the log
says "voice: OpenCE frames refused (N)" once. An OpenCE build without voice
never says it hears voices, so it is never sent any. If OpenCE changes the
format, every frame fails the checks and voice is quietly off for their
machines. The watch of their `network_format.txt` (the three structs'
hashes and the enum values 80 to 82) flags a change for review, and the
signed legacy table's kill-switch name `opence_voice` (no capability bit,
never sent in HELLO; a build that doesn't know it ignores it) turns the
bridge off in every build already out, while Delta voice keeps working.

**Tests to build with it:** the three parsers with exact, short, long,
bad-key and bad-length inputs; the router with OpenCE machines in the
room; and a cross-play `voice` step (ours hosting an OpenCE client, theirs
hosting ours), each side logging the other's test tone as heard.

### Party teams

A party (the Delta Guide's PARTY: up to 8 players who follow their leader into
games, kept by the site in memory) wants to play on one
team. Halo's teams are the host's to place, so the party asks and the host
decides, over `party_teams` (bit 21). It is additive and host-side: nothing
in the game protocol changes, and an OpenCE host, or one that says nothing,
places the party as it always did.

- **The tag.** The site gives each party a random 8-byte tag and hands it
  to the party's members only. It names no one and lasts the party's
  lifetime: a new party has a new one, and the site makes a new one (and a
  new code) when the leader kicks a member or asks for a new code. A
  member's game sends it to its game's host in PARTY_TAG (33) once both
  agreed to `party_teams`, at once when it changes (joined, left, a new
  party), and every 5 seconds while it is in a party, in case a datagram
  was lost. Zeros mean "in no party". The host keeps each machine's last
  tag and forgets it with the machine's session. It learns only which of
  its machines share a party; split screen players share their machine's.
- **The client's word.** The host can't check a tag: it takes it on the
  client's word, so a tag only ever moves the players who send it. A
  machine's tag, once it has one, changes no sooner than 30 seconds after
  its last change (a sooner one is dropped, and the next resend lands), so
  flipping it can't keep a lobby rearranging. A tag only one machine sends
  is no party: one machine's own players are never a party of their own.
- **Whether the host honors it.** A player's host does unless its
  `network.keep_parties_together` is off (on by default). A dedicated
  server does only with its `keep_parties_together` setting on (off by
  default; `sv_set keep_parties_together true`, `server/docs/settings.md`).
  Only in team games, never in co-op.
- **Where balance allows.** A split party goes to the team where most of
  its members are (a tie: the smaller team first). One member moves at a
  time, only when the teams then stay within one player of each other.
  Where a move would unbalance them, the party stays split: only party
  members are ever moved, never a player in no party (no trades).
- **When.** In the lobby, each time its players or their parties change
  (the host sends the lobby again); a player who picks the other team
  afterwards keeps their pick until the lobby changes again. In a game in
  progress, only a member who joined late and hasn't spawned yet moves (as
  a team change does, without a death); nobody playing is moved.
- **What it isn't.** The host's own team rules (auto team balance, team
  changes, an admin's `sv_team`) stay as they are, and a party never
  overrides them. A client that doesn't speak `party_teams` sends nothing
  and is placed as before.

The host's side is `port/linux/game/party_teams.c` (which change: the
plan in `port/linux/src/party_teams_plan.c`, tested by
`tools/test_party_teams.py`); the members' tag comes from
`port/linux/game/guide.c`.

### Object sync

What the game's own messages leave out of the host's objects, over
`object_sync` (bit 17). The game messages stay as they are; a host sends
these only to the clients that agreed to it, so OpenCE's machines, older
builds and the console builds that do not offer it play as they always
did. Built for Halo PC maps whose client-side scripts read objects the
game's messages never describe (Coldsnap 20th XL; credit to ugoboom for
the map and the report):

- **Unit vitals.** The game's messages carry shield and health only for
  players' units and the AI's. A placed biped or an empty vehicle kept its
  full shield on a client until the host deleted it, so a script that read
  it there (`unit_get_shield`) decided otherwise than the host's. The host
  sends UNIT_VITALS for every other unit: a change at once if a shield or
  body fell from full, emptied, or the shield's flags changed, other
  changes at most every 2 ticks, each change in 3 datagrams (one may be
  lost), and every half second the next 32 of the rest in turn (a client
  that came late, or lost all three). They go out before the tick's object
  changes, so a client sees a shield drop before a delete the host's
  scripts made of it. A client's copy takes the host's values (not its
  own player's unit, which the game's messages carry).
- **A client that comes late.** A client that joins, or loads the map
  after the host, would learn of the host's deletes and the shields that
  caused them at once, and a map's scripts would decide wrong. So when a
  client asks for the host's objects, the host sends it every unit's
  vitals first (a full pass, its last datagram marked), and the client
  keeps its own copies of what the host deleted until a tick after that
  pass came (a second at most; not at all with a host without
  `object_sync`). Its scripts run that tick with the host's vitals on the
  copies the map placed. In play, a client also applies the host's delete
  of a named object two ticks late, so that the vitals heard with it (often
  in the same frame) are there for one tick of its scripts first.
- **Object names.** A client makes its copy of a host's object without the
  scenario name (the creation message has none), so the names of objects
  the host's scripts made (`object_create_anew`) found nothing there. The
  host sends OBJECT_NAMES, all its named objects (by its name list, as its
  scripts read them), when they change, when a client agrees or asks for
  its objects, and every 5 seconds. A client names its copy as it comes,
  if the scenario gives that name to an object of its type and nothing
  else holds the name then (its own copy of the same name, which it
  deletes as not the host's, frees it).
- **Map-placed bipeds the scripts read.** A named biped that no player or
  AI has (Coldsnap's game type detectors) is read by the map's scripts on
  every machine. Each machine's scripts find the game type from these
  bipeds and then delete the other types' ones themselves. A client that
  only had the host's would find the host's deletes already done and read
  a missing biped (-1), so it would pick the wrong game type. So while the
  host has no object of that name (by OBJECT_NAMES), a client keeps its own:
  the copy it placed with the map, or the map's placement again in its own
  half of the object array after the host's delete (not after a death: a
  body the host cleared up stays gone). Its own scripts may delete it. Once
  the host names one of its objects so, the host's copy takes the name and
  the client's own copy goes.
- **Names the host removed.** OBJECT_NAMES also lists each name the host
  had an object of this game and has none of now, with that unit's vitals
  as the host last saw them. A client that came late would otherwise keep
  standing copies of the bipeds the host's scripts deleted or that died
  there. Its own copy of a removed name takes those vitals for a tick of
  its scripts (so they decide as the host's did), then goes at the tick
  after the next. A name the host never had an object of is not listed,
  and the client keeps its own copy of it as above.

It is offered by the builds with the game list (`HALO_GAME_BROWSER`: the
desktop builds, Android, the Switch and the dedicated server), not by the
original Xbox's. The game's side is `port/linux/game/network_objects.c`.

### Verified names

Over `identity` (bit 20), a player proves who they are with the device
certificate their copy holds ("Device identity" under Delta Link). The host
checks the proof, then relays it to every other client that agreed, and each
client checks it again itself. A host can't make a proof up, but it can
show one it saw elsewhere (below).

**Only verified players show by their Delta profile.** Profile names, clan
tags and badges show only for a player whose proof checked here. Everyone
else shows by their game name. That includes OpenCE players and players who
don't share. It also includes an older build's PROFILE claim, which proves
nothing. There is no "unverified" label that still shows a claimed name.
This is stage D of the profile transition, and it is in from the builds
that speak `identity`.

What OpenCE machines get doesn't change. They see the 11-character Halo
name, as the game's own messages carry it, and nothing of `identity`
reaches them. Older Delta builds keep working: they never agree to
`identity`, get none of its messages, and show game names as they did.
Network determinism is untouched. Nothing here feeds game state.

**Bound to the host, or a name only.** A proof says whether the client
checked that the challenge came from the host it connected to (the
**bound** flag, signed with the rest).

- A client marks a proof bound only when it reached its host through the
  invite tunnel, and the challenge's binding is that invite's identity (the
  first 32 hex digits of its key's hash, which the tunnel authenticates).
- A client that reached its host any other way (a LAN, an address from the
  game list or typed in) can't check, so it proves unbound.
- A client whose challenge carries a binding that isn't its invite's (a
  host passing on another host's challenge) proves nothing, and shows no one
  as verified in that game.
- A host takes the bound flag only while it has a binding of its own.
- A client shows another player's card as bound only when the card has
  the flag and its own join is bound (it came in through that invite).
  A client that joined by address shows every card unbound.
- **Only a bound proof gives a role or a ban.** An unbound proof shows a
  name and nothing more: the dedicated server's moderation never hears of
  it, and it is never written to bans.txt or matched against it.
- What an unbound proof can't stop: a host that relays a real player's
  proof from its own game into another, at the same time, can be shown
  there under that player's name. That is why it never carries a role.
- **Accepted risk: a card replayed by a host.** A host that joined another
  game as a client keeps the cards it was sent there. Each card is signed
  over its nonce and its binding, both chosen by that game's host, so a
  host can send a card it collected, bound or not, to its own players who
  joined by address, at the same machine index, with the other game's
  nonce and binding in its challenge, and they check it. The player then
  shows as verified in that game for up to about 40 minutes after they
  signed (a client's card age of 30 minutes plus the 10 minutes of
  clock grace), with no role and no ban. A client takes a card as bound
  only when its own join checks against the challenge's binding (it came
  in through that invite), so a replayed bound card shows unbound there,
  and players who joined by invite refuse a challenge that isn't their
  invite's. Closing this needs a nonce from every client in each proof,
  so a proof would be made again whenever anyone joins. We left that out
  for now.

**The flow.**

1. With `identity` agreed, the host sends **ID_CHALLENGE**: a nonce of 32
   random bytes made for that client's Delta session (a new one with every
   new session: every new HELLO, a machine joining, a machine taking
   another's index), and its binding. It says it again every 5 seconds.
2. For each local player (0 to 3, the player's controller in the network
   game) whose copy can prove them, the client sends **ID_PROOF**: the
   device certificate (CDEV), its issuer's (CISS), the flags, the time it
   signed (Unix seconds), the player's name in their capitals, and the
   device key's signature (below). A local player proves only when their
   player shares their profile (`network.share_profile`), their Halo
   profile is signed in (not a guest), and the copy holds an in-date
   certificate for that account with `play` and a handle. An ID_PROOF with
   a certificate length of 0 withdraws a local player (they signed out of
   Delta, or stopped sharing).
3. The host checks it (below). It keeps a checked proof as that player's
   **card**, with the nonce it checked it with, and sends it as **ID_CARD**
   to every client that agreed. A bound card is also told to its
   moderation (a dedicated server's). A host proves its own local players
   the same way, with a nonce of its own, and relays their cards too.
4. Each client checks each card itself, against the nonce the card carries
   and the binding of its own challenge, and keeps only cards that check.
   It forgets a machine's cards when the roster forgets the machine, or
   when the host sends a withdrawal (an ID_CARD with a certificate length
   of 0).

- **One identity, one player.** A host refuses a proof whose device or
  account is already verified for another player of the game, and a client
  refuses such a card. One exception: a bound proof takes the place of an
  unbound card of the same identity (an unbound card may be someone else's
  relay), and the unbound one is withdrawn. A bound card is never replaced
  by an unbound one.
- **Cards are checked again.** The host and every client check each card
  they keep again every 60 seconds, with all the checks below: its
  certificate's dates, the revocation list as it is now, and its age. The
  host withdraws a card whose proof is more than 25 minutes old (10
  minutes, what it takes, plus the 15 a client proves again in), and a
  client drops one more than 30 minutes old, however often the host sends
  it again. Both ages get the 10 minutes of clock grace on top, so in
  practice a card lasts up to about 35 minutes on the host and about 40
  on a client. When a new revocation list comes, every card is checked
  against it on the next frame.
- **What every player gets.** An ID_CARD goes to every client in the game
  that agreed to `identity`, and it holds the whole certificate: the
  account's player ID, the device's ID and its public key, the platform,
  the handle and the certificate's dates. Every other Delta player in the
  game can see them. No address and no secret travels in it.
- **Open: the local player isn't matched to a player.** The host takes a
  proof for local players 0 to 3 of a machine without checking that the
  machine has a player at that index in the game. A card for a local
  player who isn't playing shows nowhere, since names are looked up by a
  player's machine and controller, but it does take its identity, so
  that account can't be verified for another player of the game.
- **Nothing outlives a session.** The host withdraws a machine's cards when
  it takes a new session from that index, when the machine leaves, and
  every card when it stops hosting, and its moderation hears of each.
- **A new binding.** When the host's binding changes (its invite came or
  went), it withdraws every card, its own players' too, and challenges
  every client again at once.
- **Timing.** The host sends every card again to every client every 30
  seconds (8 a frame at most), at once when one changes, and a withdrawal
  before it forgets it. A client sends a local player's proof again every
  10 seconds until the host relays its card, and proves again every 15
  minutes, so its card is never older than the checks take.

**What the device signs** (the label, then the fields, byte by byte):

| Bytes | Field |
| --- | --- |
| 24 | `"delta identity proof v1\n"` |
| 32 | the nonce the host challenged this machine's session with |
| 1 | the binding's length B (0 to 64) |
| B | the binding (printable ASCII) |
| 1 | the machine index |
| 1 | the local player (0 to 3) |
| 8 | the device certificate's serial |
| 1 | the flags: 0x01 bound |
| 4 | the time it signed, Unix seconds, little-endian |
| 1 | the name's length T (0 to 20) |
| T | the name |

- **The nonce** is the host's for this one session, so a proof doesn't
  work in another session, even at the same machine index.
- **The machine index** ties a proof to the machine that sent it.
- **The binding and the bound flag** tie it to the host, as above.
- **The time** keeps an old proof out of a later game.
- **The serial** ties it to one certificate, so a device signed in to two
  accounts can't have its proof paired with the other.

**The checks** (the host's, and every client's of a card):

- The CISS is signed by one of the root keys (`identity_key.h`) and in
  date. A build never takes another player's certificate on its word, even
  with no root keys.
- The CDEV is signed by that issuer, issued inside the CISS's dates, and
  in date by this machine's clock, with 10 minutes of grace.
- The certificate has `play` (0x01).
- Its device isn't on the revocation list this copy last has (below).
- The proof is fresh by this machine's clock, with 10 minutes of grace
  (`IDENTITY_GRACE`) either way. The host takes it no more than 10
  minutes old (20 with the grace), keeps it to 25 (about 35), and a
  client takes a card no more than 30 minutes old (about 40).
- The signature is the device key's over the message above, as Monocypher
  4's `crypto_ed25519_check` checks it. A key of small order is refused.
- The name, if any, is the handle's characters (`A-Z a-z 0-9 _ -`). A
  name that isn't the certificate's handle in other capitals is dropped,
  and the handle shows instead.

A proof that fails is dropped, logged without its contents, and the
player shows by their game name.

**The revocation list.** Every build that speaks `identity`, and the
dedicated server, fetches `GET /v1/identity/revoked` every ten minutes.
- It checks the list: its issuer by the root keys, the list by its issuer,
  and a serial past the one it has.
- It keeps the list in `delta_revoked.bin` in the save root. A copy reads
  the saved list the first time it needs one, with or without a site, so a
  LAN game still checks against the last list.
- A list whose next update has passed is still used. The log says it is
  stale.
- A list fetched from the site is taken only while its issuer certificate
  is in date. The list a copy saved, taken while its issuer was in date,
  is still read after that issuer expires, and the log says it is stale.
  An issuer past its dates is no longer looked after, so a list it signed
  later could take revoked devices off.
- Hosts don't relay the list over Delta Peer yet.

**One name.** A Delta profile has one name, its **Delta Tag**: the handle,
3 to 20 of `a-z 0-9 _ -`, unique on the site however it's capitalized, and
the key of every lookup and URL. The account shows it in the capitals its
owner picked ("Milenko" for `milenko`), on the site, in the Delta Guide, in ID_CARD
and on the scoreboard. The capitals are ASCII, can change on the site at
any time, and the letters can't. There's no other display name. Profiles
from before keep their old free-form names on the site (`old_display`),
and show their handle until their owner picks its capitals.

**What shows.**
- The account and handle come from the certificate, and are signed.
- The capitals come from the site's public card for that account
  (`/v1/game/players`, asked by the proven account), or, before the card
  comes or with no site, from the proof. Either way they show only when
  they are the handle in other capitals; else the handle shows as the
  certificate has it. So no one can show a name that isn't their own
  handle.
- A clan tag shows before it ("[TAG] Name") when the card has one.
- A copy with no profile (a certificate with no handle) proves nothing, and
  its player shows by their game name.
- **Impersonation.** A player who isn't verified, but whose game name looks
  like the handle of a verified player of the same game (compared by the
  look-alike key below), shows as "Player N" (their place in the game's
  player list), never by that name. The name is compared as it is
  written and as it shows (every @ left out), against the whole handle
  and against the 11 characters a linked profile's game name is cut to,
  so "MilenkoTheG" is masked next to a verified "MilenkoTheGreat". A
  verified player whose handle the site marked as a look-alike of another (its card's `looks_like`) shows as
  "Player N" too.
- **The mark** (`display.verified_mark`, on by default): the scoreboard
  shows a small Delta mark beside a verified player, in the platform
  logos' column: after the logo with the Chupathingy menus, on its own with
  the Xbox menus, which show no logos. The name itself has no prefix (an @
  means a handle everywhere in Delta). The game's fonts have no Greek, so
  the mark is drawn as a plain outline triangle the height of their
  capitals, in the row's color. `display.scoreboard_icons` off (SCOREBOARD
  ICONS) leaves out the mark and the logos together: names and scores only.
  Every @ in anyone else's game name is left out, wherever it is, and so
  are spaces, dots, invisible marks, Deltas and up-pointing triangles left
  at its front, so " @Milenko", ".@Milenko", "·@Milenko", "ΔMilenko" and
  "▲ Milenko" all show as "Milenko" (and as "Player N" next to a verified
  Milenko). A player who isn't connected to Delta (no
  linked profile, or a guest) never has the mark.
- **The same name everywhere.** The scoreboard, the postgame report, the
  lobby, the Delta Guide, chat, the kill feed and the other HUD lines (a player
  quitting, the name under the crosshair) all show a player the same way:
  a verified player by their Delta name, an impersonation as "Player N".
- **On Delta you play under your verified name.** A Halo profile linked to
  a Delta profile (it would prove: shared, not a guest, with a certificate
  that has a handle) joins every game with the handle as its game name, in
  the capitals its owner picked, cut to Halo's 11 characters. The name
  typed into the Halo profile doesn't override it while it is linked.
- The mark and the profile never reach OpenCE. Their players see a plain
  game name, 11 characters, which the game's messages carry unchanged (for
  a linked profile, its handle).
- **Not connected, not recorded.** A guest Halo profile's line is never
  claimed, and a machine whose local players are all guests sends no
  report of a game it joined. The site takes a claim (`/v1/claim`) only
  from a player ID linked to a Delta profile: an unlinked one gets its 200
  and the line stays unowned. (Open: a copy's own report of a game it
  joined still names its player ID on its line, so a copy that links
  later pulls those games in.)

**Look-alikes.** The site and the game read names the same way
(`lookalike_key` in puma, `identity_lookalike_key` in the game, checked
against the same vectors):
1. Each character becomes the letter it looks like: A-Z as a-z; 0 o, 1 i,
   3 e, 4 a, 5 s, 7 t, 8 b; @ a, $ s, ! i, | i; the accented Latin letters
   (Latin-1 and Latin Extended-A, U+00C0 to U+017F) as their base letter
   ("ó" o, "Ä" a, "Ł" l, "ß" s); and the Cyrillic and Greek letters drawn
   like Latin ones.
2. The digits 2, 6 and 9 stay digits, so "milenko2" and "milenko9" are
   different names, as "milenko1" ("miienkoi") is from both.
3. Everything else is dropped (spaces, separators, signs not above, ×
   and ÷).
4. Then rn reads as m, vv as w and l as i.

On the site:
- A new handle that looks like the handle of a profile that proves who it
  is in games (it has a device that isn't revoked or signed out) is
  refused. It is checked when the name is asked for and again when the
  email link that makes the profile is opened, so a look-alike asked for
  early can't be made after the real player enrolls.
- Any other look-alike is made, but marked: its public card and service
  record carry `looks_like` with the older handle, and the admin panel
  lists it with the reserved and blocked names.
- The move to one name marks every existing pair, once: the older profile
  keeps its name clean, and each later one is marked.

**Each Halo profile's account.** A Halo profile's folder may hold
`delta_account.txt`. Its first line, with a byte order mark and spaces and
line ends around it left out, is:
- `guest`: the profile proves no one;
- an account's 32 hex digits: the profile proves as that account, with that
  account's certificate;
- anything else: the copy owner's own Halo profile (the first local
  player's) proves as the copy's own account, and any other profile is a
  guest.

Without the file, the profile proves as the copy's own account, so every
existing Halo profile keeps the link it had, and nobody is signed out by an
update. Writing the file belongs to signing a Halo profile in to another
account (sign-in by code), which keeps that account's certificate beside
the copy's. That writer isn't built here.

**Where names are drawn, and how long they can be.**
- A name can be 20 characters, and a clan tag adds up to 11 more. Where a
  column is shorter, the name is cut with "...".
- At the menus' 640x480 the columns hold: the in-game scoreboard 14
  characters, the pregame panel 16 for a player's own line and 12 for
  another machine's, the Chupathingy menus' lobby 32, and the postgame
  report 11 (as a Halo name: its column is drawn to that width).
- Those are lowercase letters' widths. Wide letters (M, W) count for more
  and narrow ones (i, l) for less, so a name of wide letters is cut sooner
  and the "..." always shows.
- A clan tag stays in front only while the name keeps 6 characters after
  it ("[TAG] Milen..."); else the name shows alone.
- Names above heads and the name of the player aimed at stay the Halo name
  (11 characters), as the Xbox draws them.

**Moderation by account.** A verified, bound certificate with `moderate`
(0x08) gives its player's machine the role its account has on a dedicated
server, beside the moderator key's:

- The server's role for a machine is the highest of its moderator key's
  role (as before) and the role of each bound, verified account on it with
  `moderate`.
- An account's role comes from the moderators file's account lines
  (`<role> <32 hex digits> [name]`; `sv_mod_add` takes an account or a
  verified player) and from the site's role list, by the account each entry
  carries. An entry from a site that sends no account gives no role: a
  handle alone never matches.
- The player still signs in from the Moderation screen (MOD_PROOF). A copy
  with no player key signs it, and its actions, with the device key when
  its certificate has `moderate`. The host takes the key the proof names,
  as always, and its role comes from the account.
- Old clients' MOD_PROOF with the derived moderator key keeps working, as
  do `moderators.txt` key lines and the site's moderator keys.
- A device enrolled with a player key gets all five capabilities, `moderate`
  among them, because the key could already do all of it.

**Bans by account.** When a dedicated server bans a machine (`sv_ban`, a
moderator's ban from the game, a cheater dropped), its bans.txt line also
names the accounts and the device of its players' bound proofs:
`account=<hex>[,<hex>]`, `device=<hex>`. A machine whose players then make
a bound proof of a banned account or device is dropped at once. A line with
only those fields is a ban too. These sit beside the address and hardware
ID bans, which still work alone. A ban by account only reaches players who
share their profile (`network.share_profile`); everyone is still checked by
address and hardware ID.

**The server panel** shows a Profile column on its Players page: the name
and the device's platform for a player with a bound proof, "No profile" for
a copy with no profile, and "-" for anyone else. Its Role column counts the
account's role too. `sv_players` has the same as `"profile"` (the name, or
null) and `"device"`.

**Consoles** leave all of it out for now (`HALO_CONSOLE_NO_DELTA_IDENTITY`):
they never offer `identity` and show game names. The messages are fixed
fields, little-endian, read byte by byte, so a console can take them up
later without a special case.

**Test vectors** (also in `port/linux/tests/identity_vectors.json`; the
device key and certificate are "Device identity"'s). The nonce is the bytes
00 to 1f, the binding `0123456789abcdef0123456789abcdef`, machine 3, local
player 1, the certificate's serial 01 02 ... 08, bound (flags 1), signed at
1790101000, and the name `Milenko`:

```
message      64656c7461206964656e746974792070726f6f662076310a000102030405060708
             090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20303132333435363738
             396162636465663031323334353637383961626364656603010102030405060708
             0108c6b26a074d696c656e6b6f
signature    bd00fd2c414b9170b8f9d5af61cf38cf32758b5ab8cf133bf78bfe036a8c3e8e
             166ebe9da09384d446f9192d05d1aa7235886d4af359f303c3c85c6028c7e400
LAN (no binding: its length 0, nothing after it; flags 0)
signature    610f07b2e9ff47d104f2daf970467e09962e6e33db53f230054394fd05da9c51
             e898419ea5f76ef19792f6d6e9681eedac2aae3a06e3fd28e2126e843f9ad00a
```

With the "Device identity" certificates, both check at 1790101000. They
fail once the certificate expires, or with the test device on the list.
The look-alike vectors are the JSON's `"lookalikes"`.

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
  Delta puts its Delta major and its wire in its advertisement's reserved
  bytes, in the Delta tag at their tail since chupa-39a
  (`DELTA_ADVERTISED_TAG_OFFSET`; "The advertisement", below), after the
  port's version and flags before it (`DELTA_ADVERTISED_WIRE_OFFSET`). Our client, unless set to `network.protocol =
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
- **One number a host, so one OpenCE generation a game.** Since that
  number is the only thing an OpenCE client goes by, a host can't hold
  OpenCE players of two numbers in one game, and from 35 they couldn't
  play together anyway (35's game settings record is 16 bytes longer, and
  a machine refuses a record of the other size). Delta players join every
  host whatever it announces: a host goes by each Delta machine's HELLO,
  whose legacy number says the newest version that build plays
  (`DELTA_LEGACY_NEWEST`), and sends it the record of the older of that and
  its own; an OpenCE machine gets the announced number's. A host that
  announces 35 or later holds a joining machine's record until its HELLO or
  the handshake's 4 seconds have passed, as only then is it known which
  (NETCODE.md, "Versions"). It holds only a machine whose HELLO could
  change the record: one that marked its join request (a Delta client
  joining a Delta host puts a two-character mark past the end of its
  machine name, which the host clears), or any machine when a build that
  may still join without the mark could say an older version with another
  record (a host that also advertises the old layout, or one announcing
  past its own built-in number with a longer record). An OpenCE player
  joining a host of ours otherwise starts at once. To serve OpenCE players of an older number
  and a newer one at once, run two hosts, one announcing each: a local
  table (`network.legacy_table`, below) sets a server's number.
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
cleanly. `chupa-43a` (the generic layout facts, "Layouts": OpenCE's 43 as
a table row; its game with every older number as `chupa-41a`'s), `chupa-41a`
(0.8.1d on: the longer join request to OpenCE hosts of 40 and later) and `chupa-39a` (0.8.1b and 0.8.1c: the Delta tag below)
play with `chupa-28a` and newer, and send the game's messages as
`chupa-38a`'s. `chupa-38a` (the builds with OpenCE's 33 to 38) plays with
`chupa-28a` and newer, as `chupa-32a` does: a host of 32 is the same game to
both, and a host of 35 or later sends each Delta machine the settings record
of the newest version its HELLO says it reads. `chupa-32a` plays with
`chupa-28a` and newer (OpenCE's 30 to 32 are additive: a machine of 29 drops
message 90 and ignores the co-op input byte; 31's keep-alive, stamp window
and stream-only messages were already in 29); `chupa-29a` plays with
`chupa-28a`. A row is written with its release and
never changed: a build can't learn of a newer one, so a host also says its
range in its advertisement.

**The advertisement.** Since chupa-39a a Delta host says what it plays in a
tagged block at the **tail** of the advertisement's 0x1A reserved bytes
(`DELTA_ADVERTISED_TAG_OFFSET`, 17; `DELTA_ADVERTISED_TAG_SIZE`, 9), and
leaves the bytes before it to OpenCE, which fills them from the front: the
version (bytes 0 and 1), the flags (2), and since its version 37 the game's
kind (3), which our hosts send as 0, as they play none of OpenCE's game
types:

| Byte | Meaning |
| --- | --- |
| 17, 18 | the tag, `'D'`, `'L'` |
| 19 | the host's Delta major |
| 20 | the tag's layout version, 1 |
| 21, 22 | the host's own wire: its number and its letter (39, 'a') |
| 23, 24 | its oldest compatible wire: number and letter (28, 'a') |
| 25 | a check: 0xA5 xor the eight bytes before |

A reader knows a Delta host by a valid tag and check, never by a flag bit.
The tag's last six bytes sit where the old layout's range bytes were, but
its check covers the whole tag, so a released client never reads them as
range bytes, and an old host is never read as a new
one: it has zeros where the tag's `'D'` and `'L'` go. OpenCE's machines
leave and ignore the bytes.

**Why it moved.** OpenCE gave two of the bytes Delta used meanings of its
own: byte 3 is the game's kind from its version 37 (so an OpenCE client of
37 or later read a Delta host's major, 1, as SWAT), and the flag 0x04 is a
dedicated server from its version 39 (so an OpenCE client of 39 read every
Delta host as dedicated). Hosts from chupa-39a on write them only where
they mean the same to OpenCE: 0x04 only on a dedicated server, and their
major at byte 3 only in a game OpenCE names no kind for (see "The
transition"). Otherwise byte 3 is 0. A dedicated server announcing 39 or
later also sets the internet listing's flag 32 (the listing never carried
Delta bytes).

**The old layout, still read.** Hosts from 0.7.5a to chupa-38a set the flag
0x04, then their major at byte 3 (`DELTA_ADVERTISED_WIRE_OFFSET`), then a
wire in the 16 bytes after it (their *oldest compatible wire that a released
client knows*, from `DELTA_WIRE_FIRST_ADVERTISED`, `chupa-29a`, the first
wire advertised, so that a released client, which compares the slot for an
exact match, joined), and from 0.8.0a the range bytes in the last six
(`DELTA_ADVERTISED_RANGE_OFFSET`, `DELTA_ADVERTISED_RANGE_SIZE`):

| Byte | Meaning |
| --- | --- |
| 20 | layout version, 1 |
| 21, 22 | the host's own wire: its number and its letter (32, 'a') |
| 23, 24 | its oldest compatible wire: number and letter (28, 'a') |
| 25 | a check: 0xA5 xor the five bytes before |

A client reads the tag first and the old layout when there is none, so a new
client still finds an old Delta host. From OpenCE's version 39 the flag may
be OpenCE's: an old-layout host announcing 39 or later counts as Delta only
with a wire ID in its slot (`DELTA_ADVERTISED_FLAG_TAKEN_VERSION`), and an
OpenCE host's kind at byte 3 is never read as a major without one.

**The client's decision** (`delta_compat_decide`): it joins when the host's
own wire is in the client's range (the client's oldest to its own), or the
client's wire is in the host's range; for an old-layout host without valid
range bytes, when the old slot's wire is the client's own (the rule since
0.7.5a). It refuses, naming both wires, only when neither holds. A host of
another major, or one that says no wire, is joined by its OpenCE number, as
before.

**The consoles decide the same way.** The original Xbox, Xbox 360 and Wii U
builds (`HALO_CONSOLE_DELTA` or `HALO_CONSOLE_ONLINE_GAMES`) take this path
too, so they join a Delta host whatever OpenCE number it announces, and fall
back to the numbers only for a host without Delta. A refused game gives a
short reason the console's Online Games screen shows: the host's wire, or
its network version and whether it is newer or older.

**The transition (decided 2026-10-27).** Released builds read only the old
layout: the PCs on chupa-38a (0.8.0f to 0.8.0h) and before, and the consoles
on chupa-32a (the Xbox and Switch builds). So a new host keeps writing the
old layout as well, but only where it doesn't clash with what OpenCE now
means by those bytes:

- **The flag 0x04** means a dedicated server to OpenCE's 39. A dedicated
  server that hosts with Delta sets it, which is true for both readers
  (`delta_peer_advertised_flags`). A host with a player at it never sets it.
- **Byte 3** is the game's kind to OpenCE's 37 and later, and its browser
  reads it only in a Slayer game (`DELTA_ADVERTISED_KIND_ENGINE`; a mode
  there names SWAT, Infection or Gun Game). With the flag, in a game of any
  other engine, a host writes its major there and the old slot after it:
  `chupa-29a`, ended before the tag (`delta_compat_advertise_old`). These
  clients take that slot as the host's wire, which is in all their ranges.

What a released client gets from a new host:

| Host | Released client |
| --- | --- |
| a dedicated server, not Slayer | joins by Delta (its wire is in the host's range), whatever number the host announces; Delta Peer |
| a dedicated server, Slayer | says HELLO (the flag) and gets Delta Peer; joins by the OpenCE number |
| a host with a player at it | joins by the OpenCE number; no Delta Peer |

Joining by number works when the client's range reaches the number the
host announces. The built-in number is 32, which every released build
joins. A host that follows the signed table to a newer number
("Auto-announce") is joined by number only by clients whose range reaches
it; `network.announce = 32` keeps the old number for older console builds.
Without Delta Peer a client loses profiles, chat,
team changes, platform icons, moderation sign-in, and Bomb game types (a
host plays those only with machines that agreed to them) until it updates.
A chupa-32a console can't join a Slayer game on a server announcing 39 by
number (it joins to 33), so a server for those consoles pins 32. **The transition has no end date.** Hosts write the old layout where
it fits for as long as released builds need it, and new builds read it.
Stopping either is a deliberate decision ("Never drop an old protocol").

**The cost of one slot** (the old layout, for the record). A released client
that checked the slot exactly saw the host's oldest compatible wire,
`chupa-29a` for a 0.8.0a host: 0.7.5 players joined it, and 0.8.0's
(`chupa-32a`, an exact-match client, out for a few hours before 0.8.0a) saw
another wire until they updated. 0.8.0a and later read the range bytes and
join both. The 32 row's first OpenCE build is build-244.

### The console floor

Every Chupathingy platform stays in one pool. The consoles move slowest
(their builds follow the main repository through a submodule), so they set
the floor: `DELTA_WIRE_CONSOLE_FLOOR` in `delta.h`, `chupa-32a` on
2026-10-27, the oldest wire a console build in the wild plays (the original
Xbox's, the Xbox 360's, the Wii U's and the Switch's). Every row of `DELTA_WIRE_COMPATIBILITY` from the
floor on must reach back to it, and `DELTA_WIRE`'s row must reach every
console wire in the wild; `tools/test_delta.py` (`test_console_floor`, in
CI) fails otherwise. Raise the floor only when every console port has
shipped a newer wire.

### Following policy

Chupathingy has its own code and roadmap. It doesn't follow OpenCE's code,
only OpenCE's **network version**, so players of both keep sharing games.

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
	X(32, "build-244", additive) /* the host's policy on aiming assist for the mouse and keyboard (message 90), a client's input saying whether its player plays co-op without friendly fire (wire chupa-32a) */ \
	X(33, "build-266", additive) /* co-op's second switch in the settings' co-op byte (bit 1: the players' hits spare their AI allies), the host's to act on; in no release of OpenCE's, its testing builds alone */ \
	X(34, "build-266", additive) /* no message changed: scenery and devices that are a shield alone are static targets too, every machine runs static targets' shields, a client reports no damage a static target deals, a static target's copy made during play found by the nearest; in no release */ \
	X(35, "build-266", additive) /* OpenCE's own game types (SWAT, Infection, Gun Game, Hardcore): the gametype's options 0x2C bytes, the settings record 16 bytes longer, which a machine of 34 cannot read; this build reads both, by the host's number, and leaves a game of one it does not play; in no release */ \
	X(36, "build-266", additive) /* the host's shield effects, a new message (91) a machine of 35 drops */ \
	X(37, "build-268", additive) /* a listed game's kind (an OpenCE game type and Hardcore) in a byte that was zero: the advertisement's fourth reserved byte and the internet listing's stamp; this build shows the name in the list and marks the games it does not play; no message or record changed */ \
	X(38, "build-268", additive) /* the shield effects message takes a seventh value, 6, for each machine's own look; this build draws stock for it, as for the others */ \
	X(39, "build-294", additive) /* a dedicated server's game: the advertisement's flag 0x04 and the internet listing's flag 32; no message or record changed (first build: the oldest published release whose commit has the change, by git ancestry, 2026-10-27; older releases were removed, so an earlier one may have had it) */ \
	X(40, "build-334", additive) /* the join request 234 bytes longer (a pass and a proof for OpenCE's own servers, sent as zeros), which a host of 40 needs and an older one ignores; a dedicated server's refusals 8 to 12; in no release of OpenCE's alone */ \
	X(41, "build-334", additive) /* a refusal 13 (too many machines from one address), messages 78 and 79 laid out anew and 94 and 95 (map votes) new, all dropped by this build; co-op's flags bit 2 (a dedicated host) ignored */
```

`tools/test_delta.py` (in CI) checks `halo_port_limits.h` against it: hosts
announce a version of the table, clients join back to the newest breaking
one and up to the newest and the next (`DELTA_LEGACY_PRESUMED`). Hosts may
announce an older version than the newest row: a client plays a newer
host's rules by the version it advertises (`network_rules_of_version`)
before our hosts take them on, as with 33 to 41, which clients play and
hosts announce only once the signed table proves them ("Auto-announce"). A change our client reads both sides of (35's
longer settings record, picked by the host's version) is `additive` here,
though a machine of the older version can't read it; its row says so. The numbers stay literal in `halo_port_limits.h`, which the command
repository's tools read.

Planned next, from it:

- the client's accepted range: the newest version, back to the last
  `breaking` row (`HALO_PORT_NETWORK_VERSION_MINIMUM` and `_MAXIMUM`);
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
| 33 | | Co-op's second switch: bit 1 of the settings' co-op byte (the byte bit 0 of which is 19's player collisions) says the players' hits do their AI allies no harm (OpenCE's Server Setup's ALLY FRIENDLY FIRE: OFF). Only a host acts on it; a client takes it as it is. In no release of OpenCE's (testing builds alone) | additive |
| 34 | | No message changed, but which objects are static targets and what a client does with them did: scenery and devices that are a shield alone (Rat Race's energy shields, Bigass's tripmines) are static targets too, which a client reports its hits on and applies the host's events to; every machine runs static targets' shields (stun and recharge), as no host sends them; a client reports no damage whose owner object is a static target (the host deals it); and an event naming an index that is no object on the client (the host's own copy of one made during play, a tripmine) goes to the client's nearest static target within its bounding sphere and 2.0 world units of the damage's origin (and an area effect's cutoff radius). OpenCE raised the number with the first of these alone, so a host of 34 may have any of them. In no release | additive |
| 35 | | OpenCE's own game types, SWAT, Infection, Gun Game and Hardcore: the gametype's PC options grow from 0x1C to 0x2C bytes (a mode, a hardcore flag, Infection's and Gun Game's options, 6 reserved), and the settings record with them, to 13,136 bytes at the network's slots. A machine of 34 can't read it (its pieces' total size is another), so it is breaking for OpenCE's; our clients take the size by the host's version, and leave a game of a game type they don't play, saying which. In no release | additive |
| 36 | build-266 | The host's shield effects, a new message (91, 12 bytes, reliable: the header, 0 to 5 as a 16-bit value, 16 bits of 0), sent to a client when it has loaded and once each game; a machine of 35 drops it. Our clients take it and draw stock, the only look they have | additive |
| 37 | build-268 | A listed game's kind: one byte, an OpenCE game type in its low seven bits (0 none, 1 SWAT, 2 Infection, 3 Gun Game) and Hardcore in bit 7. In the LAN advertisement it is the fourth reserved byte (zero before), in the internet listing the first byte of the 8-byte stamp before the signature (the signature covers it; the length is unchanged). A game type counts only on the Slayer engine; Hardcore alone names the engine (HARDCORE SLAYER). It decides only the name in the list. Our clients take any value, name the games they do not play and mark them like a game of a newer build; our own hosts send 0. A Delta host's fourth reserved byte is its Delta major (below), so an OpenCE 37 client reads a Delta host's kind as 1 | additive |
| 38 | build-268 | The shield effects message takes a seventh value, 6, "players": the host does not dictate and each machine uses its own player's settings. A machine of 37 or older drops it. Ours draws stock for every value | additive |
| 39 | build-294 | A dedicated server's game: the advertisement's flag 0x04 (until then Delta's, which moved to the Delta tag for it) and the internet listing's flag 32 (ours is 128; a dedicated server sets both). OpenCE's browser shows DEDICATED only for servers its master server lists, so ours appear under ALL GAMES, as we don't speak its master (below). No message or record changed. Our dedicated servers set the flag 0x04 when they announce 39 or later or host with Delta, and the listing's flag 32 when they announce 39 or later; the built-in announce stays 32, and a newer number comes from the signed table ("Auto-announce"). The row's first build is the oldest published OpenCE release whose commit has the change (checked by git ancestry on 2026-10-27); older releases were removed, so an earlier one may have had it | additive |
| 40 | build-334 | The client's join request is 234 bytes longer after the hardware id (a pass and a proof for OpenCE's own servers). A host of 40 or later drops a request of another length without a reply; an older host reads the first 0x70 bytes and ignores the rest. Our clients send the bytes as zeros to hosts of 40 and later (wire `chupa-41a`; the length is a layout fact, "Layouts"). A dedicated server's refusals 8 to 12 are new; our clients say them plainly. In no release of OpenCE's alone | additive |
| 41 | build-334 | A refusal 13 (too many machines from one address), messages 78 and 79 laid out anew, and 94 and 95 (map votes) new. Our machines drop the vote messages unread, and a host tells an OpenCE player who votes, once, that it has no votes. Co-op's flags bit 2 (a dedicated host) is ignored | additive |

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
4. **Verify.** Chupathingy's own CI builds and tests the branch. The game
   against OpenCE is the cross-play gate's, which tests the releases already
   out.
5. **Ship.** Additive and green: main moves to the branch, a release goes out
   (clients update themselves), and the [C] servers and Tex roll to it when
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
| `capabilities` | the capabilities it offers, 8 hexadecimal digits; two bits are set here alone: 12 (`ping`), by a host that answers pings before joining, and 18 (`host_cheats`), by a host with `network.host_cheats` on |
| `machines` | while hosting with Delta: the game's machines by platform, `pc_linux:2,xbox:1,legacy:1` (`legacy`: a machine without Delta; `unknown`: a Delta machine that does not share its platform; a dedicated server leaves itself out). Counts only, never a player (`delta_peer_game_host_summary`) |

Compatibility both ways: a site from before these fields ignores them (it
reads the fields it knows), and the site reads each on its own, leaving out
one it can't read rather than refusing the announcement, so older builds
(which send none) and newer ones (a longer platform key, a platform it
doesn't know) are listed as ever.

From them the site lists each game's `platform`, `hosting` (`official`: our
`[C]` servers, by their hosts; `dedicated`; `player`), `protocol` (`delta`,
`opence`), `family` (`xbox`, `ce`, `pc`, `md`, by the map's suffix) and
`machines`; OpenCE's public games (the brokers) are `opence` and nothing
more. The game's browser reads them from `/v1/games.txt?fields=28&coop=1` (field
13 the region; 14 to 17: platform, hosting, protocol, machines; 18 an
OpenCE game's gametype; 19 the announced capabilities; 20 to 22 an Xbox map
pack's map name, pack and tag; 23 the playlist; 24 `opence_server` for an
OpenCE dedicated server's game; 25 `1` when the host's joins are locked; 26
and 27 a server badge and its label; 28 the server key its certificate
names; `coop=1` asks for co-op games too, which the
list leaves out unless asked). A list from before a field sends fewer, and
the browser reads what it gets. It shows them: the Online
Games screen's rows (a platform tag before the name, `SRV` for a server), its details (a Host line; the machines beside IN GAME;
"Host cheats on" beside the status for a host with bit 18) and the PC menus'
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
set to false): our [C] servers, community servers and ChupathingyCE
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
excluded); medals and sprees are worked out from the kills by the rules
under "Medals"; objectives, rides,
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

### Medals

One set of rules gives every medal the game reports
(`port/linux/src/medal_rules.h`): Delta Stats' batch (`"medals"`, each
player's `"medals": {key: count}`), the game list's carnage reports and a
console's reports (each line's `"medals"`, from `game_stats.c`), the local
service record, and the HUD's pop-ups (`display.medals`, off by default).
They are worked out from the kills the machine saw and change nothing in
the game. A betrayal or a suicide earns none and ends the dead player's
sprees. The keys, in their order:

- Multikills, each kill within 4 seconds of the one before (the game's own
  window): `double_kill` (2), `triple_kill`, `killtacular`, `killtrocity`
  (5), as Halo: Combat Evolved has them, then Halo 3's `killimanjaro` (6),
  `killtastrophe`, `killpocalypse` and `killionaire` (9 and every kill
  after).
- Sprees, kills without dying: `killing_spree` (5), `killing_frenzy`,
  `running_riot`, `rampage` (20), `untouchable`, `invincible`,
  `inconceivable`, `unfriggenbelievable` (40).
- A kill's: `beat_down` (melee), `sniper_kill`, `grenade_stick` (Stuck),
  `splatter`, `from_the_grave` (the killer dead as it landed), `killjoy`
  (the end of a spree of 5 or more), `first_strike` (the game's first
  kill), `revenge` (the player who killed you last).
- A weapon's sprees, its kills without dying: `sniper_spree` (5) and
  `sharpshooter` (10), `shotgun_spree` and `open_season`, `splatter_spree`
  and `vehicular_manslaughter`, `stick_spree` (5 stuck).

Splatter and its sprees need the damage's category, which only the host's
Delta Stats has; the carnage reports leave them out. Older builds report
fewer keys (Halo CE's four multikills and four sprees, Killtrocity for 5
and more). The site takes any key (at most 64 a line) and counts the ones
it knows (`site.js`, `MEDALS`); its checks hold a line's multikill medals
to its kills, and its sprees, five kills each, too.

The names and icons are the site's too, one set for both. `tools/medal_assets.py`,
run by hand with a checkout of ChupathingyCE/puma, reads `site.js`'s
tables (`MEDALS`, `TIERS`, `SHAPES`, `GLYPHS`), draws each icon as the
site's `medalSVG` does, and writes `port/assets/medals` (`medals.json` and
an atlas of 64 pixels a medal, which the builds embed, and one of 32
for a console later) and `port/linux/src/medal_icons.h`.
puma's `test_medals_sync.py` keeps the digest of the tables the game was
made from, so a change on the site says to run it again;
`tools/test_medal_rules.py` checks every key of `medal_rules.h` has its
entry and icon.

With `display.medals` on, the HUD shows each medal you earn as its icon at
the end of the kill's line in the kill feed, at the text's height
(`port/linux/game/medal_hud.c`, from `source/interface/hud_messaging.c`,
drawn by the overlay). The icons come, fade and scroll with that line.
Medals no kill's line took get a line of their own, the icons alone. Off,
the kill feed is retail's. The icons need the game list
(`HALO_GAME_BROWSER`), so every console build and the dedicated server are
without them and show the medal's name as a line of the HUD's messages. The Delta Guide's SERVICE
RECORD shows your top medals with their icons, names and counts.

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

A verified player (Delta Peer's `identity`, "Verified names") is also known
by their Delta account, when their proof is bound to the server. A
machine's role is the highest of its key's and its bound, verified
accounts' (those whose certificates have `moderate`). An account's role
comes from `moderators.txt`'s account lines (`<role> <32 hex digits>
[name]`) and from the site's role list by each entry's account, under the
same cap. Bans can name accounts and devices too ("Verified names").

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

7. Newer servers add to the poll (an older site ignores them, an older
   server sends none, and the site's answer keeps its fields):
   - `status.locked` and `status.late_join_hold` (booleans);
   - `reports`: the reports taken since the last poll the site
     acknowledged, at most 20 a poll, each `{"ref", "time", "reason",
     "target": {"name", "handle", "account"}, "reporter": {"name",
     "handle", "account"}, "map", "game_type", "clip", "evidence"}`. `ref`
     is 32 hex digits, `clip` the clip token (32 hex digits) or null,
     `handle` and `account` null for a player who proved no one,
     `evidence` the refusal counts (`{"movement", "vehicle", "hits",
     "clock"}`). Never a report about an OpenCE player. The answer's
     `reports_taken` lists the refs the site kept and `reports_refused`
     those it never will; the rest go again.
   - `bans`: when the server shares its bans (`ban_share`), `{"digest",
     "entries"}` with every ban in bans.txt that names an account or a
     device, each `{"ref", "accounts", "device", "handles", "reason",
     "time", "until"}` (`until` 0 for ever). Never an address or hardware
     ID. It's sent only when its digest isn't the answer's last
     `bans_digest`; `{"digest": "", "entries": []}` withdraws them. The
     answer's `bans_sharing` says whether the owner shares that server's
     bans on the site.
   - `shared_bans_version`: the version of the subscribed lists the server
     holds. When the site's isn't that, its answer has `shared_bans`:
     `{"version", "entries"}`, each entry `{"list", "list_name", "ref",
     "accounts", "device", "handles", "reason", "time", "until",
     "source"}` (`source`: the name of the server that banned).

HTTPS requests in the server go one at a time across its threads (Mbed TLS
is built without locks), so polls are short; long polling waits for a build
of Mbed TLS with its threading on.

### Shared ban lists

A linked server's owner may share its bans and take other servers'. Both
are off until the owner turns them on, and both only go by Delta account
and device, never by address or hardware ID.

- **Sharing.** `ban_share` (`sv_set ban_share 1`, or "Share my bans" for
  each server on the site's My servers) sends the server's bans that name
  an account or device ("Verified names") to the site. They go into the
  owner's own list. The owner picks who may subscribe to it: servers they
  name, or every server, once a site admin marks the list trusted.
- **Subscribing.** On My servers, an owner picks which lists each of their
  servers takes: their own, those shared with that server, and the trusted
  ones. The site sends the entries with the server's poll. The server keeps
  them in `shared_bans.cache` (with the link secret's MAC, as the role
  cache) and checks each machine whose players prove an account or device
  against them, as it checks bans.txt. A match is kicked with "Banned on
  <list name>".
- **The server decides.** `sv_banlist` and the panel's Bans show the
  shared entries beside bans.txt's, with their list and the server that
  banned. `sv_unban` on one makes an exception on this server alone
  (`shared_unbans.txt`); the list keeps it. Taking a ban out of bans.txt on
  the server that made it takes it out of its list with the next poll.
  `sv_ban_lists` shows the lists the server takes and when they came.
- **Bans from the site.** `sv_ban_account <account> [duration] [reason]`
  bans a Delta account that isn't in the game (a bans.txt line with only
  `account=`), so a moderator can act on a report after the player left.

### The event log's hook

`server_roles_set_recorder()` (`server/src/server_roles.h`) hands every
audited change, as a `struct control_audit_event` (time, via, actor, role,
action, target, reason, ok, detail; never an address), to a recorder: Delta
Stats' event log records moderation through it.

## Delta Link

- **Players** (exists): Link Profile ties a game install to a site profile
  with a code (`/v1/link`, `/v1/connect`, `/v1/claim`). A link counts the
  install's games from the moment it's made. Games it played before count
  for no one until the profile's owner claims the ones that were theirs
  (the site's Past games from this device), since a shared computer or a
  second-hand console may have played someone else's. Claimed games and
  games played while linked stay with the profile after an unlink. Links
  made before this rule keep the games they had. Each device's line in a
  game is its own entry, so two of your devices in one game count once.
  A game that reaches the site late goes by the link that applied when it
  was played. A second-hand console: if someone else linked it first, the
  games from before their link stay theirs to claim, and the games it
  played while linked to them stay theirs; only the games after their
  unlink wait for you. An admin can move waiting games to another profile
  when they turn out to be someone else's.
- **Servers** (exists): see Delta Control, The link to the site.

### Delta Profile in the game

The player's own Delta profile, shown in the game
(`port/linux/game/profile_screen.c`). The game never makes, renames or
deletes a profile. The password, email, two-step sign-in, export and
deletion stay on the site, which OPEN ON THE WEB reaches. Linking is per
copy: every Halo profile on a copy counts for the same Delta profile.

- **Ways in:** the Chupathingy menus' Profiles have a DELTA row
  below Create New Profile, with a summary in the panel. In both menu
  sets, Online Games'
  Start opens it (the web page when `display.delta_profile` is false).
  A Halo profile's panel shows one quiet line, "Delta: @handle" or
  "Delta: not linked". With an empty `network.browser_url`, every way in
  is hidden.
- **Sections:** OVERVIEW (name, the player's Spartan, badges, record,
  recent games), DEVICES, ROLES, CLAN, PRIVACY, then LINK
  PROFILE or UNLINK THIS DEVICE, and OPEN ON THE WEB. The Chupathingy menus
  draw them with their own widgets
  (`port/assets/menus/chupathingy/delta_profile.xml`), and Privacy is one
  of their settings pages. The Xbox menus get a
  screen drawn as Online Games is. The dialogs (Link Profile, unlinking,
  and the pages of the site with a QR code) are the same in both.
- **Requests:** the game sends this copy's player key, over HTTPS only,
  to the game list: `POST /v1/game/profile` (the profile, its copies, roles
  and privacy), `/v1/game/unlink` (this copy only) and `/v1/game/privacy`
  (the site's own privacy settings). The site takes 6 requests a minute
  from a key, so the game asks at most every 10 seconds, and again when the
  answer is a minute old. Another copy is unlinked on the site
  (`/profile#copies`), signed in, never with a key alone. A copy with a
  device certificate sends a token in place of the key ("Device
  identity"). The answer is
  kept in memory only. `debug.txt` says whether it loaded, never what it
  holds.
- **Profile names:** in a game on Delta, a verified player (Delta Peer's
  `identity`: "Verified names") shows by their Delta Tag, in the
  capitals they picked, on the scoreboard, the postgame report, the lobby,
  chat, the kill feed and the HUD's other lines
  (`port/linux/game/profile_names.c`). The game asks the site for those
  players' public cards by their proven accounts (`POST /v1/game/players`,
  no key). Split-screen players each prove their own Halo profile's
  account. OpenCE players, players who don't share, an older build's
  unproven PROFILE claim, and what OpenCE clients see show game names.
  `display.profile_names = false` shows everyone by their game name.
- **Consoles:** `HALO_CONSOLE_NO_DELTA_PROFILE` leaves all of it out. A
  console that has the game list keeps Online Games' Link Profile panel.
- **Service record:** the profile's `"record"` has the site's totals of
  its confirmed games (host and verified: only games of linked copies
  count) and, from a site with them, `"best_spree"`, `"favorite_map"` (a
  map key), `"favorite_weapon"` (a weapon family, "sniper rifle": the
  second part of `weapons\...` or `vehicles\...` damage paths) and
  `"medal_counts"` ({key: count}, most first, the medals the games
  reported). An older site sends none of these and the game shows what it
  has. `GET /v1/profiles/ID/record` gives the same record to anyone, except
  for a locked profile or one that hides its service record (404).
- **The Delta Guide's SERVICE RECORD** (`port/linux/game/guide_record.c`) shows the
  first player's Halo profile's own record beside the site's. The game
  keeps it in the profile's folder (`service_record.txt`,
  `port/linux/src/service_record.c`): every multiplayer game the profile
  finished on that device, as `game_stats.c` adds it a few seconds after
  the end (the host's final statistics, the medals and weapons of the
  kills it saw). It is never sent. A win is counted as the site counts
  one (the best team, else first place). Its one row, MEDALS ON HUD, is
  `display.medals`.

### Link Profile on a console

The original Xbox, the Xbox 360 and the Wii U link with a key of their own
(`port/console/link`, the site's `console_link.py`). Their Online Games'
Start opens a DELTA PROFILE panel: linked to whom, or a code to enter at
stats.chupathingy.org/connect (and its QR code), then A to confirm.

- **The key:** made on the console the first time its player asks for a
  code (`delta_device.key` beside config.toml: an Ed25519 seed as 64 hex
  digits). It never leaves the console and is never logged. The console's
  player ID is the first 16 bytes of SHA-256("chupathingy console id v1\n"
  + its public key), so no one can claim another console's ID.
- **The code**, over the console list's plain HTTP (no TLS yet): the
  console sends its public key and a commitment to a secret
  (`/v1/console/link/start`), the site answers a nonce, the console sends
  the secret (`/v1/console/link/code`). Both work out the code
  (XXXX-XXXX, 10 minutes) from the key, the secret and the nonce, so a key
  swapped on the way gives a code that isn't the one on the screen. The
  console polls `/v1/console/link/status`, asks its player, and signs its
  answer over the profile it showed (`/v1/console/link/confirm`). Nothing
  sent can be used again by anyone who sees it.
- **HTTPS later:** once a console has Mbed TLS (the site's key pinned, so no
  clock is needed), its port sets the hook's `https_request` and the same
  calls go to stats.chupathingy.org over TLS.
- **Afterwards:** the console keeps the handle (`delta_link.txt`), and asks
  `/v1/console/profile` (a signed one-use challenge) for its name and clan
  tag when Online Games opens; unlinked on the site, it forgets it. A
  linked console shares its ID in Delta Peer's PROFILE
  (`HALO_CONSOLE_PROFILE_ID`), so its games count and Delta players see its
  Delta name. A console that never asked for a code sends nothing.
- **Its games:** after each online game it hosted or joined, a linked
  console claims its own line (`port/console/reports/console_claims.c`,
  `HALO_CONSOLE_GAME_CLAIMS`). It asks for a one-use challenge
  (`/v1/claim_challenge`), then signs it with its line's name and numbers
  and the game's invite or host name, map, length and how long ago it
  ended (`/v1/console/claim`). The site gives that line to the console's
  ID, so the game counts for the profile, even one the console hosted,
  which sends no report. Plain HTTP, nothing secret; tried again a few
  times while the game's reports come in. `report_joined_games = false`
  turns it off too.
- **Unlinking** is on the site (`/profile#copies`). The original Xbox's
  identity file (`D:\game_list_player.key`) still works, and wins when
  present.

### Device identity

Today a Delta profile rests on one secret, the player key
(`game_list_player.key`): whoever holds a copy of it is the player, and the
game sends it to the site with most requests. Device identity adds a key per
device whose private half never leaves it, and short-lived certificates
the site signs for those keys. Anyone can build a client, server or site
that speaks it from this section alone.

It comes in stages. **Stage A** (this section) runs beside the player key:
a new build proves its player key once, gets a device certificate, and from
then on sends the site a short-lived token instead of the key. Nothing
players see changes, the key keeps working everywhere it works today, and
OpenCE players see nothing at all. **Stage B** adds sign-in by code
(Quick Connect) and the device list with revoke, and **stage C** winds the
player key down per account ("Devices", below). Proof of identity over
Delta Peer, and showing only proven names in games, are "Verified names"
under Delta Peer.

**Keys.**

| Key | Who holds it | What it signs |
| --- | --- | --- |
| Root (the `identity` key set: primary and recovery) | GitHub environment secrets in command, made inside Actions like the update and delta sets. Nobody sees them | Issuer certificates (CISS) |
| Issuer | The site host, in a signer process of its own that the web process reaches over a Unix socket | Device certificates (CDEV) and the revocation list (CREV) |
| Device | Each install, made on first use. Never sent anywhere | Its requests to the site, and later its proofs in games |
| Player key (legacy) | Existing copies, as today | Nothing new. Sent once more, to enroll |

A build trusts both root keys (`identity_key.h`: the primary
`9c1c4454...468e` and the recovery `a3124502...d29f` of command's
`keys/identity.pub.json`, made 2026-11-09). The issuer key changes every few months: the site makes a new
pair and command's `identity-issuer.yml` signs a CISS for it with the root
key. A device certificate verifies while the issuer certificate that
vouches for it is in date.

**Encoding.** All three are binary with little-endian numbers, read byte by
byte (so big-endian hosts need no special case), and travel as lowercase
hex. Signatures are Ed25519 (RFC 8032), each over a label and then the
bytes before the signature, checked as Monocypher 4's `crypto_ed25519_check`
checks them: S must be below the group order, the check is cofactored
([8]R = [8]([S]B - [h]A)), and points that aren't canonically encoded are
allowed. A strictly cofactorless verifier can disagree on rare
deliberately-crafted signatures, so implementations should check the same
way. A verifier also refuses a public key of small order.

- **Key ID** of an issuer: the first 8 bytes of SHA-256(its public key).
- **Device ID**: the first 16 bytes of SHA-256(`"chupathingy device id\n"`
  + the device's public key).
- **Copy ID**: the player ID a player key has today, the first 16 bytes of
  SHA-256(`"halo-ce-universal player id\n"` + the key).

**CISS, the issuer certificate** (format 1, 112 bytes; signed by a root key
over `"chupathingy identity issuer v1\n"` + bytes 0 to 47):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `CISS` |
| 4 | 1 | format, 1 |
| 5 | 3 | reserved, 0 |
| 8 | 4 | valid from (Unix seconds) |
| 12 | 4 | valid until |
| 16 | 32 | the issuer's public key |
| 48 | 64 | signature |

**CDEV, the device certificate** (format 1, 148 + L bytes, 168 at most;
signed by the issuer over `"chupathingy device cert v1\n"` + bytes 0 to
83 + L):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `CDEV` |
| 4 | 1 | format, 1 |
| 5 | 1 | platform, the Delta platform number ("Platform numbers") |
| 6 | 1 | handle length L: 0 (no profile yet) or 3 to 20 |
| 7 | 1 | flags: 0x01 enrolled with a player key, 0x02 console |
| 8 | 4 | issued |
| 12 | 4 | expires |
| 16 | 8 | serial (random) |
| 24 | 8 | the issuer's key ID |
| 32 | 16 | account: the profile's player ID; the copy ID while it has no profile |
| 48 | 32 | the device's public key |
| 80 | 4 | capabilities |
| 84 | L | the handle, lowercase `a-z 0-9 _ -` |
| 84 + L | 64 | signature |

Capabilities say what the device may do as the account: 0x01 `play` (prove
identity in games), 0x02 `stats` (confirm stats lines, send reports),
0x04 `profile` (privacy and clan answers from the game), 0x08 `moderate`
(sign in to moderation; the server still checks the role), 0x10 `social`
(friends, presence, invites), 0x20 `devices` (approve another device's
code, revoke another device of the account; stage B). A device gets the
first five; only one signed in by code and approved from a password
session on the site also gets `devices`, so approving and revoking never
pass down a chain: not a device enrolled with a player key, not one
another device approved. A device of another app (a Delta client,
[delta-clients.md](delta-clients.md)) gets only the scopes it asked for
that its app has, and never `devices`. Unknown bits and flags are ignored. A certificate lasts 7 days on desktops and Android and 14 on
consoles, and the device refreshes it at half its life. The display name,
emblem and clan aren't in it; they change, and peers fetch them as today.

A certificate checks when its issuer's key ID is a trusted CISS's, the
issuer signed it, it was issued inside the CISS's validity, and now is
between issued and expires (10 minutes of grace either way). An expired
certificate never stops play; its player just isn't proven until it
refreshes.

**CREV, the revocation list** (format 1, 96 + 16 N bytes; signed by the
issuer over `"chupathingy device revocations v1\n"` + the bytes before the
signature):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `CREV` |
| 4 | 1 | format, 1 |
| 5 | 3 | reserved, 0 |
| 8 | 4 | serial, higher is newer |
| 12 | 4 | issued |
| 16 | 4 | next update |
| 20 | 8 | the issuer's key ID |
| 28 | 2 | count N, at most 4096 |
| 30 | 2 | reserved, 0 |
| 32 | 16 N | revoked device IDs, sorted, no repeats |
| 32 + 16 N | 64 | signature |

A device stays on the list for 15 days after it is revoked, longer than any
certificate it could still hold, so the list stays small.

**The site's endpoints** (Delta Link, HTTPS, JSON bodies; IDs, keys, nonces
and signatures in lowercase hex):

| Request | Answer |
| --- | --- |
| `GET /v1/identity/nonce` | `{"nonce": 32 hex, "expires_in": 300}`. Single use |
| `POST /v1/identity/legacy/enroll {"key", "device_key", "nonce", "signature", "platform", "build", "name"?}` | 200 with a certificate (below) |
| `POST /v1/identity/refresh {"device_id", "nonce", "signature"}` | 200 with a certificate |
| `POST /v1/identity/token {"device_id", "nonce", "signature"}` | `{"token", "expires_in": 3600, "scopes": [...]}` |
| `GET /v1/identity/revoked` | `{"revoked": CREV, "issuers": [CISS, ...], "serial"}` |

- **The audience.** Every device signature names the site it is for: one
  byte of length, then the site's host as the device addresses it
  (lowercase, no scheme or port; `stats.chupathingy.org`). A signature made
  for one site is no use at another.
- **Enroll** proves the player key one last time (`key`, as today) and
  registers `device_key`. The signature is the device key's, over
  `"chupathingy legacy enroll v1\n"` + audience + nonce (16) + copy ID (16)
  + device public key (32). The site enrolls only a copy it has seen before
  (a profile, a link or a confirmed game). Enrolling the same device key
  again for the same copy gives a new certificate. A device key that was
  revoked or signed out can't enroll again: the device makes a new key.
- **Refresh** and **token** are signed by the device key over
  `"chupathingy device refresh v1\n"` or `"chupathingy device token v1\n"`
  + audience + nonce (16) + device ID (16). No secret travels. Both are
  refused for a revoked, signed-out or locked device.
- **A certificate answer:** `{"device_id", "certificate": CDEV,
  "issuer": CISS, "account", "handle", "expires", "refresh_after"}`.
  `expires` is Unix seconds; `refresh_after` is a duration, the seconds
  from now until the device should refresh (half the certificate's life).
  The account and handle follow the copy's link: a copy linked to a
  profile after it enrolled gets the profile's at its next refresh.
- **Refusals:** 400 a malformed request; 401 `{"error": "nonce"}` (get a
  new nonce) or `{"error": "signature"}`; 403 with `"revoked": true`,
  `"signed_out": true`, `"locked": true` or `"unknown_copy": true`; 404
  `{"enroll": true}` for a device the site doesn't know (enroll again);
  409 with `"other_copy": true` for a key that belongs to another copy (the
  device makes a new key, once) or `"too_many": true`; 429 slow down; 503
  busy, or the site doesn't issue certificates yet.
- **The issuer's signer** signs only certificates issued now (within 5
  minutes of its clock) and revocation lists whose serial rises and isn't
  ahead of its clock. The site checks every signature it gets back.
- **Stage A's limit:** whoever holds a copy's player key can still enroll a
  new device for it. Revoking a device stops that device key, not the
  player key; later stages let an account turn its old key off.

**Tokens.** A token is opaque ASCII, at most 200 characters of
`A-Z a-z 0-9 . _ -`. It goes in a header, `Authorization: Delta <token>`,
in place of the `"key"` member, on every request that takes the player key
from the game today except Link Profile (`/v1/connect/start`), which sign-in
by code replaces on a site that speaks identity:

| Requests | Capability |
| --- | --- |
| `/v1/claim`, `/v1/client_report` | `stats` |
| `/v1/game/profile`, `/unlink`, `/privacy`, `/clan`, `/friends`, `/v1/link` (stage B) | `profile` |
| `/v1/presence`, `/v1/social/*` | `social` |
| `/v1/identity/devices` (stage B) | `profile` |
| `/v1/identity/device/revoke`, `/approve`, `/approved` (stage B) | `devices` |

The request acts for the copy the device enrolled with, exactly as its key
would. A token the site can't use gets 401 `{"token": "unknown" |
"expired" | "revoked" | "scope"}`. A site restart ends every token.

**How a client falls back.** Nobody is locked out at any step:

1. With no device certificate yet, the client sends its key as today. In
   the background it gets a nonce and enrolls. A 404 for
   `/v1/identity/nonce` means an older site: it tries again in an hour. A
   503 means busy or not issuing yet: it tries again in ten minutes, backing
   off to an hour. A 403 `unknown_copy` (a new install the site hasn't
   seen yet) tries again soon after the player's next confirmed game or
   report, or within an hour, whichever comes first. A 403 `locked` or a
   409 `too_many` waits a day, and those retries are capped.
2. With a certificate, it gets a token (nonce, then token) when it has none
   or the one it has is about to run out, and sends it instead of the key.
3. A request answered 401 with `"token"` drops the token, gets a new one,
   and sends the request again with it once. A request sent with a token
   goes with the key only when the site doesn't speak identity (its
   `/v1/identity/nonce` answers 404) or refuses the token as `"unknown"`
   and no new token can be had. Otherwise the answer is returned as it is,
   and a request the site answered is never sent again. From stage C a
   client with a certificate (or signed out by its player) never sends the
   key unless the site doesn't speak identity: without a token to send, the
   request waits and is tried again later.
4. At `refresh_after`, or on start when the saved certificate is past it or
   expired, it refreshes with the device key; it never enrolls again just
   because a certificate expired. A 404 with `"enroll"` enrolls again. A
   403 `revoked` or `signed_out` drops the certificate and keeps the key for
   this run.
5. A different player key (a restored backup, an Install in Game link)
   means a different copy. The device then drops its certificate and token
   and makes a new device key, which enrolls with the new player key. The
   copy ID is saved beside the certificate so this is caught at start too.
6. The key file stays. Old servers' `moderators.txt` list the moderator key
   derived from it, and older sites only know it.

**On the device.** `delta_device.key` in the save root holds the device
key's 32-byte seed, written and read like the player key (32 raw bytes,
readable by its owner alone, opened without following links).
`delta_device.cert` beside it holds the answer's certificate, issuer
certificate and the copy ID it was enrolled with (public data). Neither is ever logged. Consoles leave stage A
out (`HALO_CONSOLE_NO_DELTA_IDENTITY`) until their own endpoints come.

**Test vectors.** Each seed or key below is SHA-256 of its text:
the root key's `chupathingy test identity root`, the issuer's
`chupathingy test identity issuer`, the device's `chupathingy test device
key`, and the player key `chupathingy test player key`. The certificates
use the times 1790000000 (valid from), 1797776000 (valid until), 1790100000
(issued) and 1790704800 (expires); the device certificate is for platform 1
(Windows), handle `milenko`, serial 01 02 ... 08, all five capabilities,
legacy enrolled; the list names the test device and sixteen 0x11 bytes.
The nonce is the bytes 00 to 0f and the audience `stats.chupathingy.org`.
The same values are in
`port/linux/tests/identity_vectors.json`, and the game's, the site's and
command's tests all check them.

```
root public        12762975a2781f981d53c9bfd7625f10b56137eee3235fb5f3eb92c966f023fa
issuer public      fc284aa53690a1b91984e67b1820c0a28452dafbc58ed107d40066ad47814d90
issuer key ID      48810d1682965a46
device public      f6bcb81b396cf7f3cb5519a437d3699adc4ed40f2e4d22f30c3bfecb6ffcca8e
device ID          a0f42dfd380bf6aee133508172a12700
copy ID            31ea6b25b20006a252c816cd2077ff5b

CISS  4349535301000000803bb16a80e2276bfc284aa53690a1b91984e67b1820c0a2
      8452dafbc58ed107d40066ad47814d9038cc84f430b86054d1ede8b53244a90b
      da00b6012d4df101a0e00f5d388af8193c95238b1498d9fbd9314d4226ca2e6d
      40332529ea8163ad127e579cc26dcc0c

CDEV  434445560101070120c2b26aa0fcbb6a010203040506070848810d1682965a46
      31ea6b25b20006a252c816cd2077ff5bf6bcb81b396cf7f3cb5519a437d3699a
      dc4ed40f2e4d22f30c3bfecb6ffcca8e1f0000006d696c656e6b6f359728fbc6
      34431278f9352410620d865f517ebad416d36e0c845cdc85613fb2e8867b8ff2
      b8fe26c1d7b5b9abb3d99c462edea4d82ad78c975c6b970f239b02

CREV  435245560100000020c2b26a20c2b26aa013b46a48810d1682965a4602000000
      11111111111111111111111111111111a0f42dfd380bf6aee133508172a12700
      1169fc31fd48dcb2b10d79abd24a09fc115f4323eae5feebbd1f7f9b97d24693
      2b730bd2e50b6be603b751854cceefdf32c3311a4a20e8f48c9410e1c40ae50f

enroll signature   cec504f992252432a5d499113cbb7469a791b22e743cd44f6f15b278df2d4882
                   e89a5724ddcaffe73fb2a14fb8a74fd21f00160cf177e9e2f64561cdbd3b4305
refresh signature  f09ba64e6aa8ca29d8414d334dca2759d86d29f3757493a2b00be775a770f1ce
                   0f7dc0388671683b6868a493416b50a27f11a1458bb1e29fc89541849b00170d
token signature    0c02b24be2e9caa6385ef49de7bf80cb0740c7a73085d8a267c1f7f87978f1e8
                   331b870581b254a5a258c85465b3130d5153e68646b300265f8ea304f0fd0901
```

### Devices

Stages B and C of device identity, on top of "Device identity". Stage B
lets a copy sign in to a Delta profile with a short code instead of a key,
like Plex's or Jellyfin's Quick Connect, and gives every account a device
list with revoke. Stage C stops the player key doing new things once an
account no longer needs it. Old clients and old sites keep working: every
endpoint below is new, and the player key's endpoints stay.

**Sign-in by code (Quick Connect).** A copy with a device key and no
account (or one that signs in to another) asks for a code. Its player
approves the code where they're already signed in: on the site at
`/connect`, or from another signed-in device's LINK A DEVICE (in the Delta Guide). The device
asks its own player to confirm, then gets its certificate. The player key is
never sent or shown.

1. `GET /v1/identity/nonce`, then `POST /v1/identity/device/start
   {"device_key", "nonce", "signature", "platform", "build", "name"?,
   "client_id"?, "scopes"?}`,
   signed by the device key over `"chupathingy device start v1\n"` +
   audience + nonce (16) + device public key (32). `name` is what the
   device calls itself (printable ASCII, at most 32), shown when it is
   approved; the site defaults to the platform's name. `client_id` is the
   app the device belongs to, and `scopes` what it asks for (see
   [delta-clients.md](delta-clients.md)); without them, or with
   `"chupathingy"`, it is Chupathingy's own, as before. Our builds send
   `"chupathingy"` (`IDENTITY_CLIENT_ID`), on the enroll too.
   The answer: `{"code_nonce": 32 hex, "poll_token": 64 hex, "interval": 5,
   "expires_in": 600}`.
2. The device works its code out itself: 8 characters, each
   `ALPHABET[d[i] % 31]` for the first 8 bytes d of SHA-256(`"chupathingy
   device code v1\n"` + device public key + code nonce), with ALPHABET
   `23456789ABCDEFGHJKMNPQRSTUVWXYZ`. It shows them as ABCD-EFGH, with a QR
   code of `<site>/connect?code=ABCD-EFGH`. The site picks the nonce after
   it has seen the key, so someone in the middle who swaps in a key of
   their own gets a code that isn't the one on the screen. The alphabet,
   length and life are a console's Link Profile code's, so both go in the
   same box at `/connect`, under the same counts.
3. The player types the code in, signed in: `POST /v1/connect/claim
   {"code"}` on the site (a password session), or `POST
   /v1/identity/device/approve {"code"}` from another device with a token
   (capability `devices`). Either takes any code `/connect` takes. A
   device's code is shown first: 202 `{"shown": true, "handle",
   "confirm_seconds", "device": {"name", "platform", "build"}, "warning":
   "Only enter a code shown on your own screen."}`, so the page can say
   "Windows (Living room), build 0.9.1, wants to sign in as @handle". Then
   `POST /v1/connect/approve {"approve": true | false}` (the page) or
   `/v1/identity/device/approve {"approve": ...}` (the device) approves it
   (202 `{"waiting": true, ...}`) or denies it (`{"state": "declined"}`: the
   code is dead). The approver never sees which profile the device counted
   for before. A code is spent when it's typed, right or not.
   `/v1/connect/claimed` (the page) and `/v1/identity/device/approved` (a
   device) say how it went: `{"state": "shown" | "waiting" | "connected" |
   "declined" | "expired", ...}`.
4. The device polls `POST /v1/identity/device/poll {"poll_token"}` every
   `interval` seconds: `{"state": "pending"}` (shown too), `{"state": "confirm",
   "handle", "previous"}`, `{"state": "connected", "handle"}`, or
   `{"state": "declined" | "expired"}` (an unknown token too).
5. On "confirm" it asks its player (A to sign in, B not), then `POST
   /v1/identity/device/confirm {"poll_token", "device_id", "nonce",
   "accept", "signature"}`, signed over `"chupathingy device confirm v1\n"`
   + audience + nonce (16) + device ID (16) + one byte (1 yes, 0 no) + one
   byte of the handle's length + the handle it showed. A yes is answered
   with a certificate (the certificate answer, plus `"state":
   "connected"`), a no with `{"state": "declined"}`. The player has 120
   seconds after the approval to answer. Asked again after a lost answer,
   a yes gets a new certificate and nothing is done twice, unless the
   device was signed out, revoked or locked since.

What signing in does on the site:
- A device enrolled with its player key (stage A) brings its copy: the
  copy's games from then on count for the profile, as Link Profile links
  them (its older games wait to be claimed), and the certificate follows
  the copy as before.
- A device with no copy (a new install the site hasn't seen, or a second
  account on a copy) is signed in to the account itself. Its certificate's
  account is the profile's player ID, its flags have no 0x01, and its
  token acts for the profile: the lines it confirms and the reports it
  sends count for the profile directly.
- A device signed out before signs in again with the same key. A revoked
  one can't: `/device/start` answers 403 `{"revoked": true}`, and the
  device makes a new key, once.

Limits: 10 codes an hour an address, and a device key (counted once its
signature checks); 5 answers a code and 30 an address in 10 minutes; 400
polls a code and 2000 an address in 10 minutes (a code is counted only
when there is one); a code lives 10 minutes and is good once;
codes, poll tokens and approvals are kept in memory only, as hashes. 400,
401 (`nonce`, `signature`), 429 and 503 as for the other identity paths.
`/v1/connect/claim`'s wrong-code counts cover these codes too.

**Signing out on the device.** `POST /v1/identity/device/signout
{"device_id", "nonce", "signature"}`, signed over `"chupathingy device
signout v1\n"` + audience + nonce (16) + device ID (16). The site marks the
device signed out (no more refreshes or tokens) and answers `{"ok": true}`.
The device drops its certificate and token and keeps its key, and until it
signs in again it sends the site nothing as that account, the player key
neither. Signing in again is a new code.

**The device list.** Every device of an account that isn't revoked: those
signed in to it by code, and those of the copies whose games count for it
(its own player ID and the IDs linked to it). Each is `{"id"` (the device
ID, 32 hex), `"name"`, `"platform"`, `"build"`, `"created"`,
`"last_seen"` (day precision, or null), `"legacy"` (enrolled with a
player key), `"console"`, `"signed_out"`, `"this"}` (the device asking),
this device first, then the latest seen; at most 32. No address, ever,
and no key.

| Request | Who | Answer |
| --- | --- | --- |
| `POST /v1/session/devices {}` | The profile page, signed in (a page the game opened may read it) | `{"devices", "legacy_key"}` |
| `POST /v1/session/devices/revoke {"device_id"}` | The page, a password session | `{"ok", "devices"}`; 404 for another account's |
| `POST /v1/session/devices/rename {"device_id", "name"}` | The page, a password session | `{"ok", "devices"}` |
| `POST /v1/session/devices/signout_all {"auth", "code"?}` | The page, with the password (and the two-step code) | `{"ok", "revoked", "devices"}`: every device revoked and every other session of the account ended; consoles are unlinked, computers' copies aren't |
| `POST /v1/identity/devices {}` | A device's token (`profile`) | `{"devices"}` |
| `POST /v1/identity/device/revoke {"device_id"}` | A device's token (`devices`) | `{"ok", "devices"}`; 409 `{"this": true}` for itself (it signs out instead); 404 for a console (the site revokes those) |
| `POST /v1/game/profile` | As before | `"devices"` beside `"copies"`, without `"build"`, at most 16, cut to fit like the rest |

- **Revoke** stops the device's tokens at once, refuses its refreshes, and
  puts it on the revocation list (CREV) until its last certificate has run
  out. The device alone stops: a player key it was enrolled with keeps
  working for the copy's other installs (an older build, an Xbox identity
  file); a key that leaked is turned off with the account's switch, below,
  and the page and the mail say so when such a device is revoked. A
  computer's copy is unlinked once no other live device of that copy is
  left (the profile's own player ID has no link to take). A console's link
  is its key, so a console revoked is always unlinked: from the site, down
  an approval chain, and by signing out everywhere. A console still linked
  is never left out of the list. The site sessions it opened
  (`/v1/link` with its token) end. Every device it approved by
  code goes with it, and theirs: the site keeps who approved each device
  (`approved_by`), consoles approved from a device included, so a stolen
  device leaves none behind. Revoked from another device rather than the
  site, a console down the chain is revoked and unlinked all the same.
  Revoking doesn't turn the player key back on (stage C, below).
- **Consoles.** A console linked by its own Link Profile
  (`/v1/console/link/*`, "Link Profile on a console") is in the list as a
  device with `"console": true`, its device ID made from its console key as
  any device's is. Its key is its link, so revoking it unlinks it as well.
  Linked again by code later, it comes back. Consoles don't hold
  certificates yet; that's the next step, and verified names on consoles
  wait on it.
- **Tokens** are checked against the device's capabilities as they are
  now, not only the token's own.
- **Mail and the log.** A device added by code, a device revoked, signing
  out everywhere and the player key turned back on are each told to the
  account's confirmed address, and kept in the admin log (by its profile
  name, never an address).
- **The game** shows the list under Delta Profile's DEVICES, also opened
  from the Delta Guide's SETTINGS: this device offers SIGN OUT, another device REVOKE
  (asked first). The Delta Guide's SETTINGS (and X in the list) also have LINK A
  DEVICE, where the player enters another device's code with the d-pad: 8
  characters, or an older copy's 6-character Link Profile code. A device's
  code shows the device and the warning first, then APPROVE or DENY.
  The Delta Guide's SIGN IN shows a Quick Connect code right in its row, and A there
  answers the question; Link Profile's dialog shows the same code with its
  QR code.
- **The server panel** shows a player's verified profile name (from proof
  of identity over Delta Peer) and nothing of their devices: no platform,
  device name or ID.

**Stage C: the player key winds down.** Once every copy of an account has
its own device, the account's player key stops doing new things. It never
stops a device, and nobody is locked out:
- Counted per install, not per key. A copy counts as moved once a device
  was ever enrolled with its key or brought it by code (kept for good, past
  any purge; a later revoke doesn't count it back), or it is a console in
  the list. It still counts as on the key while a game used its key after
  it moved (a request the site took, a minute or more after), within the
  last 30 days: another install of the same key. Only game requests the
  site took count (`/v1/game/*`, `/v1/link`, `/v1/connect/start`,
  presence, social); claims and reports, which take the key whatever, and
  refused requests don't, so off stays off. A copy that proved itself a
  console with its own key (`/v1/console/claim` signed by its console key)
  within 60 days isn't on the key; a player key's signed claim never says
  so, nor a self-reported platform. An account for which an Xbox identity
  file was made keeps its key on. A profile's own player ID whose key no
  game request ever used (a profile made on the site) isn't waited for;
  copies that reported games before this came in count as used. Automatic
  turns the key off only once at least one copy has moved. An account with
  one copy not moved keeps its key.
- With the key off, `/v1/game/*`, `/v1/link`, `/v1/connect/start`,
  `/v1/presence`, `/v1/social/*` and enrolling a new device with that key
  are refused: 403 `{"error": "Update Chupathingy to keep using your
  profile.", "legacy_key_off": true}` (plain text for the text paths).
  `/v1/claim` and `/v1/client_report` keep taking it, so stats never stop.
  A device enrolled already enrolls again as before.
- Each account has a switch, `POST /v1/session/legacy_key {"setting":
  "auto" | "on" | "off", "auth", "code"?}` (the password, and the code with
  two-step sign-in): automatic (above, the default), always on, or off now.
  The answer and `/v1/session/devices` carry `"legacy_key": {"setting",
  "on", "waiting", "used"}` (copies not moved yet; `used` false for an
  account none of whose copies ever used the key from a game, shown as
  "Not used"). The profile page has it under
  Account and security, and the admin page shows it. Off is also how a
  leaked key is turned off; On takes the password (and the two-step code)
  like any change. Any change that turns the key back on is mailed.
- The profile page no longer offers Install in game, Copy key link or
  Download for Xbox. The `halo://key/` handler, `/v1/session/console_key`
  and the Xbox's identity file keep working for whoever has one.
- A client with a certificate never sends the key to a site that speaks
  identity ("How a client falls back", step 3). One the site answers 403
  `revoked` or `signed_out` counts itself signed out, on disk, and sends
  neither key nor token until it signs in again by code. Sign-in by code
  falls back to the old Link Profile with the key only when the site's
  `/v1/identity/nonce` is a 404.

**More than one account on a copy.** Each Halo profile on a copy may sign
in to its own Delta profile. A copy has one device key per account it signs
in to: the copy's own (`delta_device.key`, the one stage A enrolls) and,
for each Halo profile that signs in elsewhere, its own
(`delta_device_<n>.key` and `.cert`, n from 1), signed in by code with no
copy. The site needs nothing more, since each key is a device of its own
account, and two accounts can't be tied together by a shared key.
`delta_accounts.txt` in the save root maps a Halo profile (by its save
folder's name) to n; a Halo profile not in it uses the copy's own. Builds of
0.9.1 keep the file and its parsing, and show one account per copy.

**Test vectors** (the device of "Device identity", its nonce as the code
nonce and the nonce, handle `milenko`):

```
device code         DVG69E5J
start signature     14a41005d7d1e060eeb46b1af3de5cae53771df0ccf627ceb6d5c54b9a1ff6b1
                    822ee2de4135bcaaa4a4ebf7b7cf743aa6cbb5ba00a107ef660a1f798e8abc08
confirm signature   0ad07709c097a5b192b6c0763ce5f0859ff801d3eccad5c1249b0ac07ebdfdbd
                    9e0500b05b6c9ca512eb860968a70b1be1166a8720af25fae633431ee7fd1a02
decline signature   0f0f2b8e63f0ef1136a0aefaf3f51437b5fd37fb2f82e4806271bb37b7f38402
                    d5ba02d1f96430bac33a6e201d034f0fe657f236fb84c77e6a737bf2991ab80f
signout signature   f579b26e3db2c34573a7a4e9842d85ef6646513dbf73706aeae41c825a447805
                    1dddea806dbe0ecfd7a39b803e3e520190750980b2c4ff3689f2c19c1a66e80b
```

### The doorbell

The Delta Guide asks the site for its friends every 20 seconds in the menus and
every minute in a game (`POST /v1/social/state`; the site takes 6 a minute
from a copy). The doorbell makes invites, friend requests, party changes
and friends coming online show up within about a second instead: the site
rings, and the game asks at once.

- **No new connection:** the doorbell rides internet play's signalling
  (`p2p_signal.c`), on the connections it keeps to our own brokers, the
  `*.chupathingy.org` lines of `brokers.txt`: `mqtt.chupathingy.org:1883`
  (halo-mqtt) and `doorbell.chupathingy.org:1883` (the site's host). The
  game uses up to 5 brokers at once, so the public ones stay. When the game
  isn't hosting, joining or browsing, the doorbell starts signalling with
  our brokers alone; anything else that starts it brings in the rest.
- **A doorbell per copy:** the site hands each linked copy its doorbell in
  the friends state (`"doorbell"`: 32 hex digits, also in an unchanged
  answer). It's the first 128 bits of HMAC-SHA256 under a key only the
  site has, over `doorbell:<the copy's player ID>:<week>`: nobody can work
  out another copy's doorbell, or whose a doorbell is. It changes every
  week, so one that got out goes stale. The site rings the doorbell it last
  handed each copy, so a ring at the turn of the week still reaches it, and
  the copy's next poll hands it the new one. The topic is
  `doorbell/<doorbell>`.
- **Listening:** on our brokers only, signalling's CONNECT carries the
  doorbell as its MQTT user name (no password; the client ID stays
  signalling's own), and subscribes to `doorbell/<doorbell>` at QoS 0. A
  new doorbell makes those two connections anew; the others never see it.
- **The brokers' ACL** on the signalling listener (1883, anonymous):
  `topic readwrite hceu/#` and `pattern readwrite hceu/#` (signalling, for
  clients with and without a user name: plain `topic` lines cover only
  clients without one), and `pattern read doorbell/%u`. A client reads
  only the doorbell that is its user name (mosquitto checks each message as
  it delivers it, so a wildcard or someone else's doorbell gets nothing,
  and names with `+` or `#` never match), and nobody on that listener can
  publish to `doorbell/`. The site rings through a second listener (8883,
  TLS, `per_listener_settings`) as `doorbell-site`, which may only write
  `doorbell/+`. Signalling on 1883 is unchanged for every client, OpenCE's
  included.
- **Ringing:** a ring is QoS 0, not retained, one byte: what changed (1
  friends, 2 invite, 3 party, 4 presence, 5 clan; anything else, or none,
  is a ring all the same). No handle, no name, no game. The site rings both brokers.
- **Who hears what:** the site rings only copies that asked for their
  friends in the last 3 minutes, at most 12 times a minute each. A friend
  request rings the one it went to (never when they block the sender: a
  block stays silent and rings only the blocker's own copies), becoming
  friends or a removal rings both, a decline rings only the one who
  declined, an invite rings the friend invited, and a profile coming
  online, going into a game or turning "appear offline" off rings its
  friends (never while it appears offline or hides what it plays; turning
  it on rings nobody, and friends see it on their next poll).
  A party invite rings the friend invited; someone joining, leaving or
  kicked out, a new leader, rule or code, and the leader's go ring the
  other members (and the one kicked out), with kind 3. A clan's invite
  rings the player invited, with kind 5.
- **On a ring:** the game polls at once, at least 2 seconds after the last
  poll, and only while fewer than 2 polls went out early for a ring and
  fewer than 5 went out at all in the last minute. The schedule's own
  polls never use up a ring's room, so a ring speeds things up in the
  menus too, and the menus' 3 a minute plus rings stay at 5 or fewer, so a
  join or an invite still fits. A ring never cuts short a wait the site asked for
  (429, an unlinked copy).
- **Failing:** our brokers fail and come back as signalling's always have
  (its retries). Without a doorbell the polls go on as before.
- **What a watcher sees:** signalling on 1883 is plain MQTT, as it always
  was: someone on the network path could see a copy's doorbell and when it
  rings, never what changed or for whom.
- **The setting:** `network.doorbell` (on by default; it changes nothing in
  a game). Off, or a copy not linked, no user name and no subscription.
  Consoles keep polling (they have no Delta friends yet).
- **The code:** `p2p_signal.c` (`p2p_signal_doorbell`), `p2p.c`
  (`p2p_doorbell`, `p2p_doorbell_rings`), `browser.c` and `social.c` (the
  ring's poll), and the site's `doorbell.py`. Tests: `tools/test_doorbell.py`
  (signalling against a local mosquitto with the brokers' ACL, when
  installed), `tools/test_social.py`, and puma's `test_doorbell.py`.

### Clans

Clans are groups of Delta profiles, kept by the game list (its
`clans.py`). A clan has a name, a tag of 2 to 8 letters and digits, a
motto, an emblem and ranked members (leader, officers, members). Making a
clan, ranks, handing the clan to another member and the emblem are on the
site (`/profile#clan`, `/clans/TAG`). Inviting and kicking are on the site
and in the Delta Guide.

- **In the Delta Guide:** the CLAN section shows the clan's members by rank, each
  with where they are (in a game, online or offline, worked out as a
  friend's: hidden while they appear offline or hide their game). The
  leader and officers also see the invites they have out, and get INVITE
  FRIENDS (it opens FRIENDS, where a friend has INVITE TO CLAN) and, on a
  member of a lower rank, KICK (pressed twice). Everyone has LEAVE CLAN
  (pressed twice). With no clan, CLAN lists the clans' invites: JOIN CLAN
  and DECLINE. The clan's card (emblem, tag, name, motto) shows beside the
  player's own line.

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
  action: `accept`, `decline` or `leave` with the clan, `invite` or `kick`
  with a handle, or `roster`) answers with the same two, the members'
  handles and presence, and a leader's or officer's `clan_invited`. The
  friends poll (`/v1/social/state` with `"clan": true`) carries the same
  roster, so the Delta Guide spends no extra requests on it. A site from before
  these leaves them out, and the game shows what `/v1/game/profile` has.
- **Tags in games:** a verified player ("Verified names") who shows their
  clan tag is "[TAG] Name" on the scoreboard, the postgame report and the
  lobby, the name cut to fit with "...". The public card (`/v1/game/players`) carries
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
- **Warthog (the consoles):** speaks the legacy protocol with OpenCE's
  layout (the cross-play work) and joins as a Delta Peer client with its
  platform key (`HALO_CONSOLE_DELTA`), so hosts keep games to its limits
  unless its player opts in. The original Xbox hosts 16 players at most and
  joins games of up to 32. A stock Xbox build holds 16, so it joins 16.
- **Halo Custom Edition 1.0.10 players:** outside Delta. A dedicated
  server can list itself on the GameSpy masters (`gamespy_listing`), and
  with `ce_joins` (off by default) it answers a Custom Edition client's
  join with the handshake and a clean refusal. Playing in the game is
  still a development test
  ([server settings](../server/docs/settings.md#custom-edition-join-attempts)).
  Nothing of it reaches Delta Peer or the legacy number.
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
  one [C] server. An experiment for our servers first; it must never claim
  more than the table allows.
- **Proposing capabilities upstream.** OpenCE's exact match splits its own
  players on every raise. Running Delta first makes the proposal concrete.

## The legacy table as config

OpenCE raises its number often, mostly for changes older machines can live
with. Following it should not take a release each time, so the table our
builds check is config, not code: signed, served by Delta List, and passed
peer to peer over Delta Peer.

### What it says

One document, JSON, signed with Chupathingy's Ed25519 key (kept in CI;
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
  `delta.h`, `chupa-44a` today, from the build that follows OpenCE's 44; `chupa-43a` from 0.9.1; `chupa-41a` from 0.8.1d; `chupa-39a` was 0.8.1b's and 0.8.1c's, `chupa-38a` 0.8.0h's; `chupa-32a` was 0.8.0's to 0.8.0g's and the original Xbox build of 2026-10-21's; `chupa-29a` came before it; `chupa-28a` was 0.7.4e's and 0.7.5's, `chupa-26a` was 0.7.2e's and 0.7.3e's, `chupa-24a` was 0.7.1e's, `chupa-23a` 0.7.1b's, `chupa-20a` 0.7.0b's): the revision of the game protocol it
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
- **`requires`** (optional, in a row): the host protections a host needs
  to announce anything the row proves past its build's own number ("Host
  protections", below). Builds before 0.9.0 skip it as an unknown key.
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
- **`layouts`** (optional, in a row of `chupa-38a` and later wires): facts
  about OpenCE versions the wire's builds read by data ("Layouts", below).
  Builds of older wires, and every build reading another wire's row, pass
  over it.
- Unknown fields are skipped, so later fields don't break older builds. A key
  twice in one object is a broken table. Nothing nests more than eight deep
  (the document itself is one; another wire's row is three): every build's
  parser drops a deeper table whole, so `tools/delta_table.py` refuses one.

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
and, with `--from`, keeps the other wires' rows of the table before it. For a
wire that reads layouts it also writes this build's own facts (its versions
past the one it announces), keeps the facts past them the table before had,
and adds `--layout WIRE=VERSION:FIELD=VALUE[,...]` facts (`dropped_messages`
as kinds joined by `+`, for example `--layout
chupa-38a=39:options_size=48,dropped_messages=92`), refusing one its builds
would not take. `--row` and `--take-back` change a row's numbers and keep its
layouts.

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
  and every four hours (every thirty minutes on a dedicated server, and
  thirty minutes after a failure), from
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

### Layouts

The numbers say which OpenCE versions a wire plays; they can't say how. A
raise that only adds bytes at the end of the gametype's options, a message an
older machine drops, or a value of a known enum still needed a release,
because the build's reading of those was code. Since `chupa-38a` it is data:
a row's `layouts` gives, by OpenCE version, the facts a host of that version
sends that the build reads by number (`network_rules.h`, "layouts"):

```json
"chupa-38a": {
  "announce": 32, "minimum": 11, "maximum": 40,
  "layouts": {
    "35": { "settings_size": 13136, "options_size": 44 },
    "39": { "options_size": 48, "dropped_messages": [92] },
    "40": { "shield_effects_maximum": 7 }
  }
}
```

| Field | What it is | The limit |
| --- | --- | --- |
| `settings_size` | the game settings record's bytes at the network's 128 machines and players (13,120 to 34, 13,136 from 35); bytes past the options and the 4 of local data come after them, and a build has no use for them | at most 13,200 (`NETWORK_RULES_SETTINGS_MAXIMUM`: 35's and 64), and never fewer than the version before had, or than the options leave room for |
| `options_size` | the gametype's options' bytes in it (0x1C to 34, 0x2C from 35); the record grows with them, in front of the local data; a build reads the 0x2C it knows and ignores the rest (a host sends zeros) | never shorter than before, the record within its limit |
| `shield_effects_maximum` | the largest value the shield effects message (91) takes (6 built in); a value a build has no look for is drawn as stock | never lower than before, at most 15 |
| `dropped_messages` | distributed message kinds a host of the version sends that a machine that doesn't know them drops (91 from 36 built in; 78, 79, 94 and 95, OpenCE's votes, from 41) | kinds 1 to 255; only kinds the build doesn't know are affected |
| `join_request_extra` | (`chupa-41a` on) the bytes a client's join request carries after its first 0x70 (the Xbox's 0x50 and the hardware id) to a host of the version: 234 from 40 built in (OpenCE's pass and proof, sent as zeros), none before. A host of 40 or later drops a request of another length without a reply; an older host reads the 0x70 and ignores the rest | 0 to 1,024; it may shrink as well as grow |
| `join_request_fill` | what fills those bytes: `"zeros"`, the only fill a build writes (and the default). A fact naming another fill is not taken, and the build keeps the length before | `"zeros"` |
| `join_request_version_at` | (`chupa-43a` on) where in those bytes the client writes the host's network version, 16 bits big-endian (the packet codec's order; 43 is `00 2B`) (OpenCE's 42 on: their host admits only its own number, silently). A host of ours announcing the version drops, without a reply, any request without the word (of another length: an older build's or a console's, which can't play the version) or with another number; a host of a version without the word refuses none | within the request's bytes past 0x70 |
| `header_extra` | (`chupa-43a` on) the bytes every distributed message's header has after its 8, a batch's and each message in it too (whose size word counts them); zeros, but for an epoch's | 1 to 8 |
| `epoch_kind` | (`chupa-43a` on) the first 4 of those bytes are the checkpoint epoch, and this is the kind of the host's boundary message (OpenCE's 96): a host counts the epoch up (never 0) at each map and co-op revert, and sends the boundary (the header alone) over its stream to a client at its ready, before its objects, and to every loaded client at a revert; a client takes nothing of another epoch than the last boundary's (none before the first; in a batch, each message by its own epoch), stamps its own messages with it, and at a newer one lets go of what it replicated (projectiles, effects, damage, actors, co-op presentation), sends nothing but its ready until the host's snapshot ends, then says its identity again (never OpenCE's build report, 92); a host takes its clients' messages of its own epoch, and a ready of any | a kind from 93 (past every kind a build knows), with `header_extra` of 4 or more |
| `unit_state_parts` | (`chupa-43a` on) a unit state's parts a build passes over: `[{"bit": 7, "size": 28, "at": "after_up"}, ...]`; `after_up` (after the up, before the predicted time: alive and placed alone) or `last` (after the camouflage: alive alone); a state with a part where it cannot have one, or a bit neither built in nor said, is unreadable | bits 6 and 7 (0 to 5 are built in), 1 to 64 bytes; a place a build doesn't know is not taken |
| `rider_ejection_up` | (`chupa-43a` on) riders leave a vehicle on the ground whose up's height is below this, in thousandths (built in: below 0, upside down); only in a network game of the version | 1 to 999 |

So when OpenCE changes the join request's length again, a row like
`"43": { "join_request_extra": 300, "join_request_fill": "zeros" }` for
`chupa-41a` is the whole follow, without a release (`tools/delta_table.py make
--layout chupa-41a=43:join_request_extra=300`). Builds of older wires pass
over the field; a `chupa-41a` build reading a table without it goes by its
built-in 234 from 40.

With `chupa-43a`'s fields, OpenCE's 43 is a row too, with no code of its
own in the build (the facts are generic, so a later change of these is
another row):

```json
"43": {
  "settings_size": 13200, "join_request_extra": 236, "join_request_fill": "zeros",
  "join_request_version_at": 234, "header_extra": 4, "epoch_kind": 96,
  "unit_state_parts": [{"bit": 7, "size": 28, "at": "after_up"},
                       {"bit": 6, "size": 30, "at": "last"}],
  "rider_ejection_up": 200,
  "dropped_messages": [97, 98, 99, 100, 103, 104, 105, 106, 107]
}
```

From `chupa-43a` on, 42 and 43 are built in (`DELTA_LEGACY_NEWEST` 43): a
build joins 43's hosts with no table, and these facts in a row are its own
(a table's layouts go on past them). A host announces the MCC number for a
game it is asked to, `delta_legacy_host_game_at(delta_legacy_mcc_version())`
(an MCC map's: the newest MCC-capable number it or the table proves, 43
built in here, 44 from `chupa-44a`, a later one from a row's layouts and
`hosting`), and otherwise the usual number: with no row for its wire, a desktop build or a server hosts
41 (`DELTA_BUILT_IN_HOSTING`, the last release's row), a console's build 32. `tools/legacy_rows/chupa-43a.json` is the row for the wire (clients to
43's hosts, hosts announcing 41), and `chupa-43a-hosting-43.json` the same
with 43 in `hosting`, which moves our hosts to 43 and so away from OpenCE's
41 machines and our older builds and consoles (none of which join 43).
(`tools/delta_table.py make --layout chupa-43a=43:settings_size=13200,join_request_extra=236,join_request_version_at=234,header_extra=4,epoch_kind=96,unit_part_after_up=7+28,unit_part_last=6+30,rider_ejection_up=200,dropped_messages=97+98+99+100+103+104+105+106+107`.)
The row's maximum takes clients to 43's hosts; 43 in its `hosting` list takes
its hosts there too, only after the cross-play gate proves each. Rows of older
wires never name 43: their builds have none of these fields and would play a
43 game wrongly.

From `chupa-44a` on, OpenCE's 44 (their build-355) is built in too
(`DELTA_LEGACY_NEWEST` 44): the number alone changed, so 44 is 43's layout
and rules, and a build joins 44's hosts, plays them by its own rules, and
hosts at 44 when a table's `hosting` list proves it (or a game asks for
it: an MCC map's, `delta_legacy_mcc_version`). `tools/legacy_rows/chupa-44a.json`
is the wire's row for serial 12 (clients to 45, hosts announcing 44, follows
build-355) and `chupa-43a-hosting-44.json` the `chupa-43a` row widened the
same way (a `"44"` fact that is 43's, so `delta_legacy_host_game_at(44)` is
taken there too, and 44 in `hosting`); `serial-12.json` is the whole
document made from serial 11 with both
(`tools/delta_table.py make --serial 12 --from <serial 11> --row chupa-43a=41,11,44@build-355 --row chupa-44a=32,11,45@build-355 --hosting chupa-43a=44 --hosting chupa-44a=44 --layout chupa-43a=44:<43's facts>`).
A host of either wire announcing 44 is left by OpenCE's 43 and 41 machines
(one number per host, as ever); 44 is their Latest.

A fact holds for its version and every later one until the next. A build
uses facts only for versions past its own rules (`DELTA_LEGACY_NEWEST`, 38
for `chupa-38a`): for its own versions its rules are the facts, and the table
carries them too (`make` writes them, 33 to 38 for `chupa-38a`) only to say
so. A fact's field past its limit is not taken (the build keeps the version
before's), and the tool refuses to write one. The layout of a version is what
every part reads: the record size a client expects of a host of it (a piece
of another size ends the join, as before), how a client folds a longer
record into its own (`network_rules_settings_fold`) and a host makes one
(`network_rules_settings_layout_copy`), the newest version a Delta client's
HELLO says it reads (the table's newest fact past 38), so that our hosts send
it that version's record, the largest shield effects value a client takes,
and which unknown kinds are expected (a kind neither the build nor the table
knows is dropped too, and said in the log once a game, as the sign of a
change the table has not caught up with). The table only adds: nothing in it
can make a build read past a buffer, shrink what it reads, or handle a kind
it handles differently, and a layout the host doesn't actually have ends the
join cleanly rather than playing on.

So the next OpenCE raise that only adds trailing option bytes, a message an
older machine drops, or a new value of the shield effects message is a table
row: the cross-play gate (or a person) runs `delta-table.yml` with the wire's
widened numbers and its fact (`layouts`: `chupa-38a=39:options_size=48`), and
no release follows. A raise that changes what an existing byte means, or adds
behavior a machine must run alike (a new game type's rules, a message whose
loss desyncs), is still a release.

### When the wire changes

A row widens every build of its wire alike, so every build of a wire must
play the same OpenCE versions. **Bump the wire whenever a build learns a
legacy layout or behavior**: a newer `DELTA_LEGACY_NEWEST`, a row added to
`DELTA_LEGACY_VERSIONS`, or a layout read by code rather than the table.
`DELTA_WIRE_LEGACY_NEWEST` in `delta.h` records each wire's newest version
(`chupa-32a`'s is 33: 0.8.0e and the Xbox build of 2026-10-21 said
`chupa-32a` and play 32, its one co-op bit the host's, while 0.8.0f and 0.8.0g
said it too and play to 38, so no row could widen `chupa-32a` past 33), and
`tools/test_delta.py` fails when `DELTA_LEGACY_NEWEST` or the version table's
newest row passes the current wire's entry, or a released entry changes. A
new wire gets a `DELTA_WIRE_COMPATIBILITY` row (usually the same oldest wire
as the one before) so it keeps playing with the builds before it, and its own
row in the table once its gate passes.

A table needs no row for a wire to be safe for it: without one a build keeps
its built-in numbers. A row narrower than a build's built-in numbers makes
that build drop the whole table (and the kill switch and layouts with it), so
`chupa-32a` has no row: its builds' built-in maximums are 33, 37 and 39, and
any row either narrows the newer ones or widens 0.8.0e.

### Local override

`network.legacy_table` in config.toml (`HALO_LEGACY_TABLE` for servers)
names an unsigned file in the same format (beside config.toml unless a full
path), for testing and for an admin who knows better. Its row for the build's
wire is used as it is (it may narrow too), with its layouts; its serial is
not checked. It logs a warning at start, and while it is set no signed table
is fetched, taken or relayed. A file that can't be read, or has no row for
the wire, leaves the built-in numbers in use (and says so).

This is how one server announces a newer number than the published table,
for example a [C] server that announces 38 so that OpenCE build-268 players
(an exact match) list and join it, while our players of `chupa-28a` and later
join it as before (a Delta client joins by wire, and gets the record of its
own version). On a `chupa-38a` build (0.8.0g and earlier say `chupa-32a`:
use that row name there), `legacy-38.json` beside the server's config:

```json
{"delta_legacy": 1, "serial": 1,
 "wires": {"chupa-38a": {"announce": 38, "minimum": 11, "maximum": 39}}}
```

and `HALO_LEGACY_TABLE=legacy-38.json` (or `network.legacy_table =
"legacy-38.json"`). The server's log says "a local, unsigned legacy table is
in use ... announcing 38", and `sv_status` "Following OpenCE network version
38 (a local table)". It stops relaying signed tables meanwhile; remove the
setting once a published table announces the number. From auto-announce on (below),
a server follows the signed table's newest hosting number by itself, and
`HALO_NET_ANNOUNCE` pins an older one: a local table is for tests.

### Auto-announce

Hosts announce the newest OpenCE number they're proven to host, and they
follow the signed table on their own. Nobody has to edit a file when OpenCE
moves.

- **Which number.** A wire's row may carry `"hosting"`, a list of numbers
  the cross-play gate proved with our host and OpenCE's clients alone (the
  gate's ours-host and server directions passed; their host may still need
  a release for our clients). A build announces the newest of the row's
  `announce` and that list (`delta_legacy_hosting_newest`). Older builds
  skip the list and announce `announce`, as they always did. The join range
  stays the row's `minimum` to `maximum`: hosting 41 doesn't make our
  clients try OpenCE's 41 hosts.

  ```json
  "chupa-39a": {"announce": 39, "minimum": 11, "maximum": 40, "hosting": [41]}
  ```

  The list is oldest first, each number once, at most 8 (`DELTA_HOSTING_MAXIMUM`).
  `tools/delta_table.py make --hosting chupa-39a=41` adds to it, and a later
  `--row` keeps it.
- **When it changes.** When a newer table arrives (every 4 hours, every 30
  minutes on a dedicated server, at start, or from another machine over
  Delta Peer), the new number is taken at once, unless a game this machine
  hosts is under way. Then it waits for the lobby, so one game is played by
  one number (`delta_legacy_hold`, from the advertisement's state). The log
  says "now announcing OpenCE network version 41 (was 39)". The LAN
  advertisement, the broker listing (published again at once with the new
  number) and the game list all follow. No restart.
- **Pinning.** `network.announce` (`HALO_NET_ANNOUNCE` for servers) is
  `"latest"` by default. A number pins it, for example `32` for a server
  that console players on older builds join. A pin has to be one this
  machine may announce: from the row's minimum to the newest proven for
  hosting. Anything else is logged and the table is followed instead.
- **Our own players.** A Chupathingy client joins a Delta host by its wire,
  whatever number the host announces (the tag in the advertisement; the
  game list marks the host as Delta). So a `chupa-39a` client, whose window
  tops out at 40, joins one of our hosts that announces 41. The broker
  listing alone can't tell a Delta host from OpenCE's: without the game
  list, a client sees such a game marked as another version.
- **Local tables** (`network.legacy_table`) may carry `"hosting"` too, and
  `network.announce` applies to them. Once a server's local table only
  repeats the signed one, remove it and let the server follow the table.

### Host protections

The cross-play gate proves that games stay in sync. It doesn't prove that
our host enforces what OpenCE's clients of that number expect a host to
enforce. OpenCE's hosts check line of sight on reported hits since
2026-10-23, and 41 added a limit on machines from one address and map
votes. A table row that raised our announced number past what our host
actually does would put their players in games that are softer than they
expect, with no release and no review.

So a row can say what hosting it takes:

```json
"chupa-41a": {"announce": 32, "minimum": 11, "maximum": 42, "hosting": [41, 42],
              "requires": ["hit_line_of_sight"]}
```

- **The names** are `DELTA_HOST_PROTECTIONS` in `delta.h`, each with
  whether the build has it. A build marks a name as had when its hosts
  and dedicated servers enforce it by default. 0.9.0 marks two:
  `hit_report_checks` (the host checks each client's hit report: its own
  player, a weapon of theirs, the target where the host had it, within
  reach, no faster than the weapon fires; `network_damage.c`) and
  `hit_line_of_sight` (the host refuses hits with no line of sight,
  `network.hit_line_of_sight`, on by default). `address_machine_limit` and
  `map_votes` are named so a row can require them, and no build marks them
  as had yet. 0.9.0 can limit the machines from one address
  (`network.max_machines_per_address`), but that is off by default on a
  player's host (8 on a dedicated server), so the name stays at 0 and a
  row that requires it still makes 0.9.0 announce its own number. A name
  is never renamed or reused.
- **A build that lacks one** (or doesn't know the name) announces its own
  number (`HALO_PORT_NETWORK_VERSION`), whatever the row's `announce` and
  `hosting` say, and `network.announce` can't pin past it. It logs one
  line: "not hosting OpenCE network version 42 ...: it requires the host
  protection address_machine_limit, which this build does not have; announcing
  32". It still joins the row's range as a client: clients enforce
  nothing for a host.
- **A row without `requires`** works as before. Old tables, and the
  servers' local tables, are unchanged.
- **Builds before 0.9.0 skip `requires`**, and 0.8.1d (`chupa-41a`) reads
  `announce` alone. So a `chupa-41a` row that requires anything keeps
  `announce` at 32, and newer numbers go in `hosting`, which only builds
  that also read `requires` follow (nightlies between #615 and #632 read
  `hosting` without `requires`). Wires from `chupa-42a` on can raise
  `announce` freely. `tools/delta_table.py` refuses a row that breaks this,
  a `requires` on a wire older than `chupa-41a`, and any name `delta.h`
  doesn't list, so `tools/test_delta.py` fails on an unknown name.
- **Adding one**: `delta_table.py make --requires chupa-41a=hit_line_of_sight`
  (`delta-table.yml`'s `requires` input). A make only ever adds to the list.
  Taking a name away is a hand edit, and a person approving it.
- **When a build gains one**, set its 1 in `DELTA_HOST_PROTECTIONS` in the
  same change that enforces it, on listen hosts and dedicated servers alike.

### One listing per host

A host lists its game once, under the number it announces. OpenCE's
browsers show only listings of their exact number, so during a change some
of their players (those on the older build) don't see our hosts until they
update. Listing the same game twice (once per number) was considered and
left out for now. A listing's slot, its invite and the host's machine
identifier (in its XNADDR) all come from one key, so a second listing needs
a second key that joins end to end: a second identity in the signalling, an
XNADDR alias on the host so that a client joined through the second invite
reaches the game, and a second broker connection per broker for the slot's
will. Our browsers already show such copies once (`p2p_listing_hidden_copy`).

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
| `delta_legacy_layouts(facts, count)` | the table in use's layout facts for this wire, oldest first (`network_rules.c` asks, told of it at start) |

`tools/test_delta.py` checks the tool and builds `delta.c` with a test key
(`tools/delta_check.c`) to check what the game takes and drops.

### Never send message 92

OpenCE's distributed message 92 (`client_build`, added without a version
raise) sends a player's build, the repository it was made from and their
whole mod list to any host, which OpenCE's tools then mark and search by.
**Our machines never send it**, and our hosts drop it from OpenCE's clients
unread, as an unknown kind, without logging it or keeping what it says
(`DISTRIBUTED_CLIENT_BUILD_KIND`, `network_distributed.c`). Not sending it
costs no compatibility, as no version needs it. Don't add it "for
compatibility". If OpenCE ever makes it required to join or be listed,
following OpenCE ends there (README's "up to network version N",
`DELTA_LEGACY_PRESUMED` to 0) rather than sending it. The same holds for
OpenCE's master server and fleet tools: Delta List and Delta Control are
ours, and we don't speak theirs.

## Following OpenCE

When OpenCE raises its network version, every Chupathingy player and
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
     the run's input) the join range (`maximum`) is published at once;
     otherwise Donut posts the command and a person runs it.
   - **What hosts announce always needs a person.** A change to any row's
     announced number (`announce` or `hosting`), or a protection taken out
     of `requires`, is published only by a run a person started (the
     Actions page, `gh workflow run`, or the site's Quick Update panel),
     never by the gate or the watch on their own: `delta-table.yml` checks
     it (command's `tools/table_guard.py`) and fails a run the gate or the
     watch started. The gate publishes the widened join range by itself and
     posts the command for the rest; the person checks what OpenCE's hosts
     of that number enforce ("Host protections") and adds `requires` if
     ours lack it.
   - A hosting pass: our host and dedicated server passed with OpenCE's
     clients, but our clients couldn't join OpenCE's host (a change only
     their host needs, like 40's longer join request). The wire's row gets
     the number in its `"hosting"` list (`delta-table.yml`'s `hosting`
     input, `delta_table.py make --hosting`), so our hosts announce it
     ("Auto-announce", above), once a person publishes it, and the release
     work for our clients starts as for a fail.
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
python3 tools/crossplay_test.py run --ours /tmp/ours/build/linux/chupathingy \
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
Chupathingy, and nothing is kept secret but signing keys. An app that
wants to sign players in on our site registers a client ID first:
[delta-clients.md](delta-clients.md).

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
   override, the real keys, delivery over Delta Peer, epochs, layouts,
   auto-announce, the host protections a row requires, and CI
   publishing after the cross-play gate.
8. The per-peer host experiment, and the proposal to OpenCE.
