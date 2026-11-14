# Delta registry

Every number Delta assigns, written down in one place. The same data is in
[registry.json](registry.json), which other projects can read and test
against. A test in the game's repository
fails if this file and the game's code disagree.

Numbers are never reused. A number stays with what it was given to, even if
that project stops.

## Platforms

A machine's platform number, the first byte of its platform key
([peer.md](peer.md#platform-keys)). The key is the name on the wire and in
the game list; the tag is the short label on a game list row. Its policy row
is in [peer.md](peer.md#platform-policy).

| Number | Key | Tag | Name | Class | For | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | `unknown` |  |  |  |  | a machine that doesn't say |
| 1 | `pc_windows` | WIN | Windows | pc | Chupathingy | ours |
| 2 | `pc_macos` | MAC | Mac | pc | Chupathingy | ours |
| 3 | `pc_linux` | LNX | Linux | pc | Chupathingy | ours |
| 4 | `android` | AND | Android | handheld | Chupathingy | ours |
| 5 | `steam_deck` | STEAMOS | SteamOS | handheld | Chupathingy | ours |
| 6 | `xbox` | XBOX | Xbox | console | Chupathingy (warthog-xbox) | ours |
| 7 | `xbox360` | 360 | Xbox 360 | console | Chupathingy (warthog-360) | ours |
| 8 | `wiiu` | WIIU | Wii U | console | Chupathingy (warthog-wiiu) | ours |
| 9 | `switch` | NX | Switch | console | Chupathingy (warthog-switch), port by thelinkin | ours |
| 10 | `psp` | PSP | PSP | handheld | Sheepdog01 | community port |
| 11 | `vita` | VITA | Vita | handheld | Lord BirchWood | community port |
| 12 | `xbox_uwp` | XBS | Xbox Series | console | ReviveMe | community port |
| 13 | `pc_freebsd` | BSD | FreeBSD | pc | Renkoto (port/unix) | in our tree |
| 14 | `ps3` | PS3 | PS3 | console | Mayo's fork | community port |
| 15 | `ps4` | PS4 | PS4 | console |  | reserved, no known port |
| 16 | `ps5` | PS5 | PS5 | console |  | reserved, no known port |
| 17 | `n64` | N64 | N64 | console | JackT | community port |
| 18 | `pc_legacy` | LGCY | Legacy PC | pc | JackT | community port |
| 19 | `steamlink` | LINK | Steam Link | console | Chupathingy32 | in progress |
| 20 | `ios` | IOS | iOS | handheld | JizzyRivers (`port/ios`) | in use (approved 2026-11-04) |
| 21 | `tvos` | ATV | Apple TV | console | JizzyRivers (`port/ios`) | in use (approved 2026-11-04) |
| 22 | (none yet) |  | Dreamcast | console | JiltedValkyrie's dc-halo | requested, pending owner |

### How to ask for a number

Open an issue on
[ChupathingyCE/chupathingyce](https://github.com/ChupathingyCE/chupathingyce/issues)
(or ask on our Discord) with the platform, your port's name and link,
roughly how much memory the game gets there, how many players it handles
joining and hosting (0 if it never hosts), and whether it plays network
co-op. We add the number here, to the game and to the site, and tell you
which it is. Until then, send 0 (unknown).

## Capability bits

Delta Peer's capability bits ([peer.md](peer.md#capability-registry)). A
bit is never reused. "Ports never send" marks the bits a HELLO or WELCOME
never carries: retired ones, and the game list's own.

| Bit | Name | Status | Ports never send |
| --- | --- | --- | --- |
| 0 | `platform` | in use |  |
| 1 | `profile` | in use |  |
| 2 | `server_messages` | in use |  |
| 3 | `chat` | in use |  |
| 4 | `ce_maps` | in use |  |
| 5 | `md_maps` | retired | yes |
| 6 | `coop` | assigned, not offered yet |  |
| 7 | `ai_sync` | assigned, not offered yet |  |
| 8 | `vote` | assigned, not offered yet |  |
| 9 | `console_slots` | retired | yes |
| 10 | `moderation` | in use |  |
| 11 | `bomb` | in use |  |
| 12 | `ping` | list only | yes |
| 13 | `bomb_movement` | in use |  |
| 14 | `bomb_traits` | in use |  |
| 15 | `team_change` | in use |  |
| 16 | `server_badge` | in use |  |
| 17 | `object_sync` | in use |  |
| 18 | `host_cheats` | list only | yes |
| 19 | `host_rules` | in use |  |
| 20 | `identity` | in use |  |
| 21 | `party_teams` | in use |  |
| 22 | `voice` | in use |  |
| 23 | `reports` | in use |  |
| 24 | `mcc_maps` | in use |  |
| 25 | `game_modes` | in use |  |

## Wire IDs

A wire ID names a revision of Chupathingy's game code
([versioning.md](versioning.md#wire-compatibility-ranges)). The current one
is `chupa-44b`. The console floor, the oldest wire every
newer one must still play with, is `chupa-32a`. The first
wire hosts advertised is `chupa-29a`.

| Wire | Builds | Plays back to | Newest network version |
| --- | --- | --- | --- |
| `chupa-20a` | 0.7.0b |  |  |
| `chupa-23a` | 0.7.1b |  |  |
| `chupa-24a` | 0.7.1c to 0.7.1e |  |  |
| `chupa-26a` | 0.7.2e, 0.7.3e |  |  |
| `chupa-28a` | 0.7.4e, 0.7.5 |  | 28 |
| `chupa-29a` | 0.7.5a and later 0.7.5 builds | `chupa-28a` | 29 |
| `chupa-32a` | 0.8.0 to 0.8.0g, consoles of 2026-10-21 | `chupa-28a` | 33 |
| `chupa-38a` | 0.8.0h | `chupa-28a` | 38 |
| `chupa-39a` | 0.8.1b, 0.8.1c | `chupa-28a` | 39 |
| `chupa-41a` | 0.8.1d to 0.9.0 | `chupa-28a` | 41 |
| `chupa-43a` | development builds before 0.9.1 (no release) | `chupa-28a` | 43 |
| `chupa-44a` | 0.9.1, 0.9.1a | `chupa-28a` | 44 |
| `chupa-44b` | 0.9.2 (in development) | `chupa-28a` | 44 |

## Network versions

The legacy protocol's network version, as hosts announce it
([versioning.md](versioning.md#the-legacy-number)). Our clients join hosts
of 11 to 45; 11 is the newest breaking version. Every
version's change is in [versioning.md](versioning.md#legacy-versions).

Numbers seen in the wild:

| Version | Who announces it |
| --- | --- |
| 22 | older Xbox and Chupathingy builds (as reported; not found in this repository's code, signed table or tests) |
| 32 | older Chupathingy and console builds; what 0.9.1's servers announce |
| 41 | 0.8.1d to 0.9.0 (chupa-41a's signed row); a 0.9.2 development build's built-in announce |
| 42 | a Dreamcast port (as reported) |
| 43 | hosts of MCC maps |
| 44 | current |
| version with bit 15 set (0x8000) | a GOAT H1 host closed to other builds: closed, never joinable |

## Other numbers

| What | Number |
| --- | --- |
| Delta major | 1 |
| the game's server port (UDP) | 5150 |
| system link search (UDP) | 5151 |
| Delta Peer (UDP) | 5160 |
| map sharing (TCP) | 5153 |
| the original Xbox build's title ID | 0x43480001 (set in warthog-xbox) |
