# Delta: ChupathingyCE's network family

Status (October 5, 2026): in progress. Delta List, Stats, Link and Control
run today under the names in the table below; the legacy number's table and
its automation are built (see "The legacy number"); Delta Peer is designed
and not built yet. The site's page for players: https://halo.milenko.org/delta

Delta is the name for everything ChupathingyCE's machines say to each other
and to our services beyond the game protocol OpenCE defines. It has one rule
above all: **the game protocol stays OpenCE's, byte for byte.** Delta sits
beside it, never inside it, and every part of Delta falls back silently to
plain OpenCE behavior when the other side doesn't speak it.

## The family

Delta is one name with qualifiers, because its parts cross different trust
boundaries:

| Part | Between | What it is | Exists today as |
| --- | --- | --- | --- |
| **Delta Peer** | ChupathingyCE machines in one game | protocol major, capabilities, and our own messages | planned as "the Chupathingy channel" |
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

- A socket of our own on a port beside the game's, so our message numbers
  can never clash with OpenCE's. On the internet it runs inside the same
  invite tunnel as the game (`p2p.c` passes the port through), so it is
  encrypted and authenticated like everything else in the tunnel. On a LAN it
  is plain UDP, like the game itself.
- Halo is a star: clients talk to the host, never to each other. Delta Peer
  is the same: client to host, and the host relays what others need.
- A host that speaks Delta sets a flag in its advertisement's spare
  reserved bytes (the flags byte at `HALO_PORT_ADVERTISED_FLAGS_OFFSET`;
  0x01 and 0x02 are taken, so 0x04). OpenCE machines ignore the bit.

### Handshake

Right after a client joins (the legacy join, unchanged), if the host's
advertisement has the Delta flag:

1. The client sends **HELLO**: Delta major, its capability set, its build
   (version string), platform, and the legacy number it runs.
2. The host answers **WELCOME**: its major, its capability set, and the
   intersection both will use. A host with a different major answers
   **LEGACY** and nothing else is said.
3. No answer within a few seconds means legacy only. The game is never held
   up waiting.

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
| 9 | `console_slots` | the client is a console (16 slots), for hosts that adapt |

The registry lives in the repository next to the compatibility table and is
the single source for the bit numbers.

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
- **The invite tunnel (`p2p.c`):** carries Delta Peer's port next to the
  game's, inside the same encryption.
- **The dedicated server:** a host that speaks Delta Peer, announces through
  Delta List, reports through Delta Stats, and is run through Delta Control
  and Delta Link.
- **Warthog (Xbox):** speaks the legacy protocol with OpenCE's layout (the
  cross-play work) and, later, Delta Peer with `console_slots`, so hosts can
  adapt to its 16 slots.
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

- **The table as signed data.** The built-in table stays the floor; a table
  signed with ChupathingyCE's key (Ed25519, the key kept in CI) and served
  through Delta List may only add newer additive rows. Clients and servers
  cache it and fall back to the built-in one offline; CI publishes it after
  the cross-play test passes; a dedicated server's admin can override it
  locally, with a warning in the log. Then a raise needs no release at all.

- **Per-peer announcing.** A host compatible with several legacy numbers
  could give each joining machine its own number in the advertisement it
  sends it through the tunnel, so OpenCE players on different builds share
  one [D] server. An experiment for our servers first; it must never claim
  more than the table allows.
- **Proposing capabilities upstream.** OpenCE's exact match splits its own
  players on every raise. Running Delta first makes the proposal concrete.

## Phases

1. This document, the compatibility table and the capability registry.
   Done: `delta.h` and `test_delta.py`.
2. The announced number and join range checked against the table. Done;
   generating them from it comes with the signed table.
3. The watch workflow's classifier and automatic raise (done), and CI's
   cross-play test against OpenCE builds (next).
4. Delta Peer: the port, the flag, the handshake, and its first
   capabilities (`platform`, `profile`).
5. Delta Link for servers, and Delta Stats events (in progress separately).
6. The per-peer host experiment, and the proposal to OpenCE.
