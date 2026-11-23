# Delta registry

Every number Delta assigns, written down in one place. The same data is in
[registry.json](registry.json), which other projects can read and test
against. Chupathingy's game repository has a test
(`tools/test_delta_registry.py`) that fails if its code disagrees with the
game's own copy of this registry (`docs/delta/registry.json` and
`registry.md` there), which is kept in step with this one. The test doesn't
read this repository's files yet.

Numbers are assigned by pull request to this repository and approved by the
spec's maintainers ([CONTRIBUTING.md](CONTRIBUTING.md#numbers)). Numbers
are never reused. A number stays with what it was given to, even if that
project stops.

## Platforms

A machine's platform number, the first byte of its platform key
([peer.md](peer.md#platform-keys)). The key is the name on the wire and in
the game list; the tag is the short label on a game list row. Its policy row
is in [peer.md](peer.md#platform-policy) and in registry.json's
`platform_policy`.

| Number | Key | Tag | Name | Class | For | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | `unknown` |  |  |  |  | a machine that doesn't say |
| 1 | `pc_windows` | WIN | Windows | pc | Chupathingy | in use |
| 2 | `pc_macos` | MAC | Mac | pc | Chupathingy | in use |
| 3 | `pc_linux` | LNX | Linux | pc | Chupathingy | in use |
| 4 | `android` | AND | Android | handheld | Chupathingy | in use |
| 5 | `steam_deck` | STEAMOS | SteamOS | handheld | Chupathingy | in use |
| 6 | `xbox` | XBOX | Xbox | console | Chupathingy (warthog-xbox) | in use |
| 7 | `xbox360` | 360 | Xbox 360 | console | Chupathingy (warthog-360) | in use |
| 8 | `wiiu` | WIIU | Wii U | console | Chupathingy (warthog-wiiu) | in use |
| 9 | `switch` | NX | Switch | console | Chupathingy (warthog-switch), port by thelinkin | in use |
| 10 | `psp` | PSP | PSP | handheld | Sheepdog01 | community port |
| 11 | `vita` | VITA | Vita | handheld | Lord BirchWood | community port |
| 12 | `xbox_uwp` | XBS | Xbox Series | console | ReviveMe | community port |
| 13 | `pc_freebsd` | BSD | FreeBSD | pc | Renkoto (port/unix) | in Chupathingy's tree |
| 14 | `ps3` | PS3 | PS3 | console | Mayo's fork | community port |
| 15 | `ps4` | PS4 | PS4 | console | MobSlicer152 | in use |
| 16 | `ps5` | PS5 | PS5 | console |  | reserved, no known port |
| 17 | `n64` | N64 | N64 | console | JackT | community port |
| 18 | `pc_legacy` | LGCY | Legacy PC | pc | JackT | community port |
| 19 | `steamlink` | LINK | Steam Link | console | Steam Link build (port work by JizzyRivers) | in progress |
| 20 | `ios` | IOS | iOS | handheld | JizzyRivers (`port/ios`) | in use (approved 2026-11-04) |
| 21 | `tvos` | ATV | Apple TV | console | JizzyRivers (`port/ios`) | in use (approved 2026-11-04) |
| 22 | `dreamcast` | DC | Dreamcast | console | Jilted Valkyrie and lamb2k (dc-halo) | community port (assigned 2026-11-14) |

### How to ask for a number

Open a pull request on
[ChupathingyCE/delta-spec](https://github.com/ChupathingyCE/delta-spec)
that adds the next number to this page and to registry.json, with its policy
row: the platform, your port's name and link, roughly how much memory the
game gets there, how many players it handles joining and hosting (0 if it
never hosts), and whether it plays network co-op. An issue first is fine if
you'd rather ask. The number is yours once the spec's maintainers approve
the pull request; each implementation then adds it to its own code and
site. Until then, send 0 (unknown).

## Capability bits

Delta Peer's capability bits ([peer.md](peer.md#capability-registry)). A
bit is never reused. "Ports never send" marks the bits a HELLO or WELCOME
never carries: retired ones, and the game list's own.

| Bit | Name | Status | Ports never send | Note |
| --- | --- | --- | --- | --- |
| 0 | `platform` | in use |  |  |
| 1 | `profile` | in use |  |  |
| 2 | `server_messages` | in use |  |  |
| 3 | `chat` | in use |  |  |
| 4 | `ce_maps` | in use |  |  |
| 5 | `md_maps` | retired | yes |  |
| 6 | `coop` | assigned, not offered yet |  |  |
| 7 | `ai_sync` | assigned, not offered yet |  |  |
| 8 | `vote` | assigned, not offered yet |  |  |
| 9 | `console_slots` | retired | yes |  |
| 10 | `moderation` | in use |  |  |
| 11 | `bomb` | in use |  |  |
| 12 | `ping` | list only | yes |  |
| 13 | `bomb_movement` | in use |  |  |
| 14 | `bomb_traits` | in use |  |  |
| 15 | `team_change` | in use |  |  |
| 16 | `server_badge` | built, keys not issued |  | BADGE_ASK and BADGE are built and clients offer the bit, but no badge key has been issued yet: no build accepts a certificate, and a host offers the bit only when it has one |
| 17 | `object_sync` | in use |  |  |
| 18 | `host_cheats` | list only | yes |  |
| 19 | `host_rules` | in use |  |  |
| 20 | `identity` | in use |  |  |
| 21 | `party_teams` | in use |  |  |
| 22 | `voice` | in use |  |  |
| 23 | `reports` | in use |  |  |
| 24 | `mcc_maps` | in use |  | a room-wide content gate for MCC maps, plus HOST_NUMBER (39). No map identity on the wire: MAP (17) carries families 0 to 4 only, so an MCC map is never identified by hash over Delta Peer |
| 25 | `game_modes` | in use |  |  |
| 26 | `lobby_ready` | in use |  |  |

## Wire IDs

A wire ID names a revision of Chupathingy's game code
([versioning.md](versioning.md#wire-compatibility-ranges)). The `chupa-*`
wire IDs are Chupathingy's own: they are listed here so others can read
them, but Chupathingy assigns them, not this registry's process. Another
implementation leaves the tag out ([ports.md](ports.md#the-advertisement)),
or uses a prefix of its own once the wire rule for other implementations is
settled. The current one
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

## Platform floors

Each platform's own floor in the signed legacy table (`platform_floors`,
[versioning.md](versioning.md#platform-floors)): the oldest network version a
Delta machine of that platform may say in its HELLO and still play with a
host that reads the section. A table carries at most 64 rows. A platform
without a row has no floor at run time; Chupathingy's consoles then keep the
build-time console floor, `chupa-32a`. A row is added here when a table that
carries it is signed.

| Platform | Floor | Since table | Why |
| --- | --- | --- | --- |

None yet.

## Network versions

The legacy protocol's network version, as hosts announce it
([versioning.md](versioning.md#the-legacy-number)). Chupathingy's clients join
hosts of 11 to 45; 11 is the newest breaking version. Every
version's change is in [versioning.md](versioning.md#legacy-versions).

Numbers seen in the wild:

| Version | Who announces it |
| --- | --- |
| 32 | older Chupathingy and console builds |
| 41 | chupa-41a's builds (0.8.1d to 0.9.0) |
| 42 | ports that play 42's layout; Chupathingy's clients play it by the host's number |
| 43 | OpenCE's 43 hosts; `chupa-43a`'s hosts of MCC maps (development builds, no release) |
| 44 | current: announced when the signed table's row for a wire lists it for hosting (with no row, a desktop build or server hosts 41 and a console 32; the number built into the code is 32). From `chupa-44a`, the number a host plays an MCC map's game at ([peer.md](peer.md), HOST_NUMBER) |
| version with bit 15 set (0x8000) | a GOAT H1 host closed to other builds: closed, never joinable |

## Platform key flags

The flags byte of a platform key ([peer.md](peer.md#platform-keys)).

| Value | Name | Meaning |
| --- | --- | --- |
| 0x01 | `opted_in` | the player opted in to games above join_players, up to join_players_opt_in |
| 0x02 | `dedicated` | a dedicated server: hosts, and has no players of its own |

## Internet listing

A host's listing on internet play's brokers: the signed record a browser
reads to show a public game and join it by its invite (signed with
`hceu-lobby-1`, [Labels](#labels)). OpenCE's builds publish and read the
same listing, so its flags and stamp are shared with them.

### Listing flags

The listing's flags byte. A browser ignores a flag it doesn't know.

| Value | Name | Meaning | Status |
| --- | --- | --- | --- |
| 1 | `open` | the game takes joins now | in use |
| 2 | `in_progress` | a game is under way | in use |
| 4 | `teams` | a team game | in use |
| 8 | `closed` | the host stopped: this listing ends its game (a tombstone) | in use |
| 16 | `password` | the invite's token is sealed with a password's key | in use |
| 32 | `dedicated_opence` | a dedicated server's game, as OpenCE's network version 39 says it ([versioning.md](versioning.md#legacy-versions)); a dedicated server that announces 39 or later sets it with 128 | in use |
| 64 |  | reserved for Content Evolved | reserved, pending its pull request |
| 128 | `dedicated` | a dedicated server's game, nobody playing at the host (Chupathingy's); either 32 or 128 lists a game as dedicated | in use |

### Listing stamp

The 8 bytes before the listing's signature. The signature covers them, so
a browser keeps every byte as it came, including the ones it doesn't read.

| Byte | Name | Meaning | Status |
| --- | --- | --- | --- |
| 0 | `kind` | the game's kind, from network version 37: an OpenCE game type in the low seven bits and Hardcore in bit 7 ([versioning.md](versioning.md#legacy-versions)). A host that announces an older number, and a closed listing, send 0 | in use |
| 1 |  | reserved for Content Evolved | reserved, pending its pull request |
| 2 to 7 |  | none yet: a host sends 0 | reserved |

### How to ask for a flag or a stamp byte

The same way as any number ([CONTRIBUTING.md](CONTRIBUTING.md#numbers)): a
pull request that adds it to this page and to registry.json, saying what it
means, who sets it and what a browser that doesn't know it does. Since
OpenCE's builds read the same listing, say too how theirs treat it. Until
the pull request is merged, send 0 there.

## Messages

Delta Peer's message types ([peer.md](peer.md), the message table). A type
is never reused, and a machine ignores a type it doesn't know. The
capability is the one both sides must have agreed to before the message is
sent; none means any Delta session.

| Type | Name | Way | Capability |
| --- | --- | --- | --- |
| 1 | HELLO | client to host | none |
| 2 | WELCOME | host to client | none |
| 3 | LEGACY | host to client | none |
| 4 | ROSTER | host to client | none |
| 5 | BYE | either | none |
| 6 | PROFILE | client to host | `profile` |
| 7 | TABLE | either | none |
| 8 | TABLE_HAVE | either | none |
| 9 | MOD_CHALLENGE | host to client | `moderation` |
| 10 | MOD_PROOF | client to host | `moderation` |
| 11 | MOD_STATE | host to client | `moderation` |
| 12 | MOD_ACTION | client to host | `moderation` |
| 13 | MOD_RESULT | host to client | `moderation` |
| 14 | MOD_NOTICE | host to client | `moderation` |
| 15 | MOD_BIND | host to client | `moderation` |
| 16 | MOD_BIND_ANSWER | client to host | `moderation` |
| 17 | MAP | host to client | `ce_maps` |
| 18 | CHAT_CONFIG | host to client | `chat` or `server_messages` |
| 19 | CHAT_SAY | client to host | `chat` |
| 20 | CHAT_LINE | host to client | `chat` or `server_messages` |
| 21 | CHAT_REFUSED | host to client | `chat` |
| 22 | TRAITS | host to client | `bomb` and `bomb_traits` |
| 23 | TEAM_REQUEST | client to host | `team_change` |
| 24 | TEAM_RESULT | host to client | `team_change` |
| 25 | BADGE_ASK | client to host | `server_badge` |
| 26 | BADGE | host to client | `server_badge` |
| 27 | UNIT_VITALS | host to client | `object_sync` |
| 28 | OBJECT_NAMES | host to client | `object_sync` |
| 29 | HOST_RULES | host to client | `host_rules` |
| 30 | ID_CHALLENGE | host to client | `identity` |
| 31 | ID_PROOF | client to host | `identity` |
| 32 | ID_CARD | host to client | `identity` |
| 33 | PARTY_TAG | client to host | `party_teams` |
| 34 | VOICE_UP | client to host | `voice` |
| 35 | VOICE_DOWN | host to client | `voice` |
| 36 | VOICE_CONFIG | host to client | `voice` |
| 37 | REPORT | client to host | `reports` |
| 38 | REPORT_RESULT | host to client | `reports` |
| 39 | HOST_NUMBER | host to client | `mcc_maps` |
| 40 | LOBBY_READY | client to host | `lobby_ready` |
| 41 | LOBBY_STATE | host to client | `lobby_ready` |

## Map families

MAP's family byte ([peer.md](peer.md), Map identity). A new family goes
last. There is no MCC family: `mcc_maps` carries no map identity.

| Number | Family |
| --- | --- |
| 0 | `xbox`: Xbox |
| 1 | `custom_edition`: Custom Edition |
| 2 | `halomd`: HaloMD |
| 3 | `halo_pc`: Halo PC retail |
| 4 | `pack`: an Xbox map pack's map |

## Host rules flags

HOST_RULES's flags byte ([peer.md](peer.md#host-rules)): what a host lets
its players turn on. A machine ignores a flag it doesn't know.

| Value | Name | Meaning |
| --- | --- | --- |
| 0x01 | `player_names` | names above other players' heads allowed |
| 0x02 | `follow_camera` | a follow camera while waiting to respawn allowed |
| 0x04 | `late_join_hold` | a player who joins a game in progress waits for the next game |
| 0x08 | `joins_locked` | the host takes no new machines now |
| 0x10 | `air_turn_aim` | a shot fired while turned around in mid-air goes where the player aims |

## Game modes

The game modes field of HELLO and WELCOME ([peer.md](peer.md#game-modes),
`game_modes`): the modes a machine plays, one bit each.

| Bit | Value | Mode |
| --- | --- | --- |
| 0 | 0x01 | SWAT |
| 1 | 0x02 | Hardcore |
| 2 | 0x04 | Gun Game |
| 3 | 0x08 | Infection |

## Permissions

A role's permission bits, the same on a server, in MOD_STATE and in the
site's role lists ([control.md](control.md)). MOD_STATE carries 0x001 to
0x100.

| Bit | Name | Meaning |
| --- | --- | --- |
| 0x001 | `view` | status, players, bans, log, audit, playlists, settings |
| 0x002 | `warn` | warn |
| 0x004 | `kick` | kick |
| 0x008 | `ban_timed` | ban for no longer than MOD_STATE's ban minutes |
| 0x010 | `ban` | ban for longer, or for ever |
| 0x020 | `unban` | unban |
| 0x040 | `map` | maps, the playlist, the game |
| 0x080 | `settings` | settings, game types |
| 0x100 | `roles` | roles: accounts, moderators, the link |
| 0x200 | `invite` | invite roles below one's own (the server's and the site's role lists; not in MOD_STATE) |
| 0x400 | `console` | the server console's own: tokens, accounts (the server's and the site's role lists; not in MOD_STATE) |

## Device certificates

A device certificate's (CDEV) flags and capabilities ([link.md](link.md#device-identity)).
Unknown bits are ignored.

| Flag | Name | Meaning |
| --- | --- | --- |
| 0x01 | `legacy` | enrolled with a player key |
| 0x02 | `console` | a console |

| Capability | Name | Meaning |
| --- | --- | --- |
| 0x01 | `play` | prove identity in games |
| 0x02 | `stats` | confirm stats lines, send reports |
| 0x04 | `profile` | privacy and clan answers from the game |
| 0x08 | `moderate` | sign in to moderation; the server still checks the role |
| 0x10 | `social` | friends, presence, invites |
| 0x20 | `devices` | approve another device's code, revoke another device of the account |

## Report reasons

REPORT's reason ([peer.md](peer.md#reports)). A host refuses one it doesn't know.

| Number | Name | Meaning |
| --- | --- | --- |
| 1 | `cheating` | cheating |
| 2 | `abuse` | abuse in chat or voice |
| 3 | `griefing` | griefing or betraying |
| 4 | `name` | an offensive name |
| 5 | `other` | something else |

## Moderation actions

MOD_ACTION's action ([peer.md](peer.md)). A host refuses one it doesn't know.

| Number | Name |
| --- | --- |
| 1 | `warn` |
| 2 | `kick` |
| 3 | `ban` |
| 4 | `end_game` |
| 5 | `next_map` |
| 6 | `mute` |
| 7 | `unmute` |
| 8 | `lock` |
| 9 | `unlock` |

## Labels

Text that goes into signatures, hashes and topic names. These are wire
constants: never change, translate or rename one, even where it names a
project, or every signature and hash made with it stops checking. `\n` is a
line feed and part of the label.

| Label | Kind | Used for |
| --- | --- | --- |
| `chupathingy identity issuer v1\n` | signature | the issuer certificate (CISS) ([link.md](link.md)) |
| `chupathingy device cert v1\n` | signature | a device certificate (CDEV) ([link.md](link.md)) |
| `chupathingy device revocations v1\n` | signature | the revocation list (CREV) ([link.md](link.md)) |
| `chupathingy device id\n` | hash | a device ID: the first 16 bytes of SHA-256 of the label and the key ([link.md](link.md)) |
| `chupathingy legacy enroll v1\n` | signature | enrolling a device with a player key ([link.md](link.md)) |
| `chupathingy device refresh v1\n` | signature | refreshing a device certificate ([link.md](link.md)) |
| `chupathingy device token v1\n` | signature | asking for a device token ([link.md](link.md)) |
| `chupathingy device start v1\n` | signature | starting sign-in by code ([link.md](link.md)) |
| `chupathingy device code v1\n` | hash | the code a device shows when signing in by code ([link.md](link.md)) |
| `chupathingy device confirm v1\n` | signature | confirming sign-in by code ([link.md](link.md)) |
| `chupathingy device signout v1\n` | signature | signing a device out |
| `halo-ce-universal player id\n` | hash | a player ID from a player key ([link.md](link.md)) |
| `halo-ce-universal moderator key\n` | hash | a moderator key's seed from a player key ([control.md](control.md)) |
| `delta identity proof v1\n` | signature | ID_PROOF, a player's proof in a game ([peer.md](peer.md)) |
| `delta moderation proof v1\n` | signature | MOD_PROOF ([peer.md](peer.md)) |
| `delta moderation action v1\n` | signature | MOD_ACTION ([peer.md](peer.md)) |
| `delta moderation bind v1\n` | signature | MOD_BIND_ANSWER ([peer.md](peer.md)) |
| `delta control request v1\n` | signature | a server's Delta Control request to the site ([control.md](control.md)) |
| `delta control response v1\n` | hash | the site's Delta Control response (keyed BLAKE2b) ([control.md](control.md)) |
| `chupathingy server badge v1\n` | signature | a server badge certificate |
| `chupathingy server badge proof v2\n` | signature | BADGE's proof ([peer.md](peer.md)) |
| `chupathingy server badge announce v1\n` | signature | the server's signature of its invite and the time in a game list announce |
| `chupathingy console id v1\n` | hash | a console's ID in the console link |
| `chupathingy console link v1\n` | signature | a linked console's requests (followed by confirm, profile or report) |
| `chupathingy console link commit v1\n` | hash | the console link's commitment |
| `chupathingy console link code v1\n` | hash | the console link's code |
| `chupathingy console claim v1\n` | signature | a console's stats claim |
| `chupathingy probe` | signature | PROBE_ANSWER, before joining ([list.md](list.md)) |
| `hceu-lobby-1` | signature | a host's internet listing over the brokers |
| `hceu/2` | hash | a keyed hash (HMAC) in internet play's signalling |
| `hceu/3/` | topic | the MQTT topic prefix of internet play's signalling (probe answers too) ([list.md](list.md)) |
| `doorbell/` | topic | the MQTT topic prefix of the doorbell ([link.md](link.md)) |

## Other numbers

| What | Number |
| --- | --- |
| Delta major | 1 |
| the game's server port (UDP) | 5150 |
| system link search (UDP) | 5151 |
| Delta Peer (UDP) | 5160 |
| map sharing (TCP) | 5153 |
| the original Xbox build's title ID | 0x43480001 (set in warthog-xbox) |
