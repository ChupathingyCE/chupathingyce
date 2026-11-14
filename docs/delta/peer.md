# Delta Peer

Delta Peer is what machines in one Halo game say to each other beside the
game protocol: who they are, what they can do, and the extras they share.
This page is its byte-level specification, for every implementer. The game
protocol itself (the legacy protocol, at an network version) is not
changed by any of it; see [versioning.md](versioning.md) for how the two
meet. A port that only wants to play with everyone needs the handshake, the
platform key and the advertisement: [ports.md](ports.md) walks through them.

## Transport

- Delta Peer has a UDP socket of its own beside the game's, so its message
  numbers never clash with the game's. A host listens on **5160**, the
  game's server port (5150) plus 10. A client sends from any port.
- The socket goes wherever the game's sockets go: the same local address,
  and on the internet through the same tunnel as the game's datagrams. A
  host only hears machines whose addresses are in its game, so a client
  must send from the address the host knows it by.
- Halo is a star: clients talk to the host, never to each other. Delta Peer
  is the same: client to host, and the host relays what others need.
- A host that speaks Delta says so with a tag in its advertisement
  ([The advertisement](#the-advertisement)), only while its Delta socket is
  open. A host that can't open 5160 (another copy of the game on the same
  address holds it) hosts without Delta and leaves the tag out. Machines
  without Delta ignore the tag, and the game's own messages are not changed
  by it.
- Every datagram is at most 1200 bytes, under the game's 1264 and the
  internet tunnel's 1400.

## Handshake

Right after a client joins (the legacy join, unchanged), if the host's
advertisement says it speaks Delta:

1. Once the host has given the client its machine index (the client's
   pregame), the client sends **HELLO**: Delta major (in the header), its
   capability set, its build (version string), its platform key, its machine
   index, the legacy number it runs and its legacy table's serial. It says
   HELLO again every 500 ms until answered.
2. The host answers **WELCOME**: its major, its capability set, the
   intersection both will use, its build, its platform key, its own machine
   index and its legacy table's serial. A host with a different major
   answers **LEGACY** and nothing else is said. The host only answers a
   machine of its game: the HELLO's address must be that machine's, and its
   machine index the one the host gave it.
3. No answer within 4 seconds means legacy only, for the rest of that game.
   The game is never held up waiting: the handshake runs beside it, and
   nothing in the game waits on its result.
4. With `profile` agreed, the client sends **PROFILE**. The host sends every
   Delta client a **ROSTER** whenever the room changes (at most every
   250 ms) and every 5 seconds. Leaving, either side says **BYE**.

The handshake is per client. A game can mix Delta clients, machines without
Delta, and Delta clients with different capability sets; each pair uses what
it shares.

A client that will say HELLO may mark its join request so the host waits for
that HELLO before sending its game settings record
([The join mark](#the-join-mark)).

## Versioning

- **Delta major**: a small integer, raised only for a breaking change to
  Delta Peer's own framing or handshake. Different majors fall back to
  legacy. It is 1.
- **Capabilities**: one bit per optional feature, never reused. Adding a
  feature adds a bit, without raising the major. A machine ignores bits it
  doesn't know.
- **Messages**: a type number is never reused, and a machine ignores types
  it doesn't know. A payload longer than its fields is read for the fields a
  machine knows, so a later version may append fields.

How Delta's versions meet the game protocol's: [versioning.md](versioning.md).

## Capability registry

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
| 11 | `bomb` | Bomb game types (Bomb, Grifball, Open Pitch: [bomb.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/bomb.md)) and their player traits (TRAITS). Room-wide: a host playing one drops every machine without it |
| 12 | `ping` | the host answers Online Games' pings before joining; announced on Delta List only, never in a HELLO (see "Pings before joining") |
| 13 | `bomb_movement` | TRAITS' movement and melee traits (jump, gravity, melee reach), for a player whose machine and host both agreed to it |
| 14 | `bomb_traits` | TRAITS at 22 (Grifball test builds 2 and 3 sent it as 18, chat's CHAT_CONFIG). Room-wide with `bomb`: a host playing a Bomb game type drops every machine without both |
| 15 | `team_change` | changing teams during a game: a client asks its host (TEAM_REQUEST), the host decides and answers (TEAM_RESULT); see "Team changes" |
| 16 | `server_badge` | a dedicated server's signed badge: a client asks its host (BADGE_ASK), the host answers with its certificate and proof of its key (BADGE); see "Server badges" and [server-badges.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/server-badges.md). A host offers it only with a certificate |
| 17 | `object_sync` | what the game's messages leave out of the host's objects: every unit's shield and health (UNIT_VITALS) and the scenario names of the objects the host made (OBJECT_NAMES); see "Object sync" |
| 18 | `host_cheats` | not a feature: the host's console and cheats may change its public game while others are in it; announced on Delta List only, never in a HELLO, and Online Games shows "Host cheats on" |
| 19 | `host_rules` | the host's rules for its players' extras (HOST_RULES): names above heads, the follow camera, team changes; a client whose host says nothing plays by the Xbox's rules. See "Host rules" |
| 20 | `identity` | verified names: the host challenges (ID_CHALLENGE), each local player with a device certificate proves it (ID_PROOF), the host relays the card (ID_CARD) and every client checks it again. Only verified players show by their Delta profile. See "Verified names" |
| 21 | `party_teams` | a party's members tell their host their party's tag (PARTY_TAG), and a host that keeps parties together puts them on one team where the teams stay even. See "Party teams" |
| 22 | `voice` | voice chat (VOICE_UP, VOICE_DOWN, VOICE_CONFIG): a player talks on a channel, the host relays each frame by its rules without decoding it. Agreed per pair, so machines without Delta and builds without voice are never sent any. See "Voice" |
| 23 | `reports` | a dedicated server takes its players' reports of other players (REPORT, REPORT_RESULT); see "Reports" |
| 24 | `mcc_maps` | MCC maps (maps_mcc and Steam Workshop items) and their game messages (OpenCE 43's numbers 97 to 100) in a 43-layout session. Room-wide for an MCC map: a host playing one drops every Delta machine without it (an older build, a console); a machine without Delta (OpenCE 43) plays by its own MCC support |
| 25 | `game_modes` | the game modes layer ([modes.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/modes.md)): OpenCE's game types played by their version 35 rules. Which modes a machine plays rides in the modes field of its HELLO and WELCOME (bit 0 SWAT, 1 Hardcore, 2 Gun Game, 3 Infection), so one bit serves them all. Room-wide for a game of a mode: a host playing one drops every Delta machine without the capability or that mode's bit, and every machine without Delta when the host announces a number before 35 (it would read a record without the mode); a machine without Delta of 35 or later plays by its own code. See "Game modes" |

`platform` is the one capability a port needs for plain cross-play. Bits 5
and 9 were retired before use: never set them. Bits 12 (`ping`) and 18
(`host_cheats`) belong to the game list alone ([list.md](list.md)): never
send them in a HELLO or WELCOME. Bits 6 (`coop`), 7 (`ai_sync`) and 8
(`vote`) are assigned, but no build offers them yet. The
numbers are also in [registry.md](registry.md).

What the first ones carry:

- **`platform`**: each machine's platform key ([Platform keys](#platform-keys)),
  and so each player's platform. It is a claim, shown as a claim (a
  scoreboard logo), never used for game state. A machine without Delta has
  no platform.
- **`profile`**: a machine's player ID (16 bytes, the game list's) and its
  profile revision (0 until known); the profile itself is the site's
  ([link.md](link.md)). It is opt-in, since the ID is the same in every game,
  and it proves nothing: only a verified player shows by their profile
  ([Verified names](#verified-names)).
- **`ce_maps`**: the game's map's identity, host to client (MAP), so a client
  with another file of the same name leaves rather than playing a map that is
  not the host's ([Map identity](#map-identity)).

Machines share what both sides of each pair agreed to: the host puts a
machine's platform or profile in a client's roster only if both that machine
and that client agreed to the capability.

## Room-wide capabilities

A room-wide capability is on only while every machine in the game has it:
the host's own set, ANDed with each client's agreed set, and nothing for a
machine without Delta (so one machine without Delta turns every room-wide
capability off until it leaves). The host computes it and sends it in the
ROSTER's room capabilities; clients read it from there.

## Platform keys

Every machine sends a **platform key** in HELLO and WELCOME, and the host
repeats it in the roster: 8 bytes, fixed, readable with no allocation.

| Byte | Field | |
| --- | --- | --- |
| 0 | platform | its platform number ([registry.md](registry.md#platforms)) |
| 1 | version | 1; a newer version's key is read for the fields this one knows |
| 2 | flags | 0x01 opted in (its player turned its caveats off), 0x02 a dedicated server |
| 3 | host_players | the most players a game it hosts can have (0: it never hosts) |
| 4 | join_players | the most players of a game it joins, by default |
| 5 | join_players_opt_in | the most it joins with its player's opt-in (a value under byte 4 reads as byte 4) |
| 6 | memory_class | log2 of its megabytes: 3 for 8 MB, 6 for 64 MB, 8 256 MB, 10 1 GB, 12 4 GB...; 0 unknown |
| 7 | reserved | 0 |

## Platform policy

Weaker machines play with stronger ones, with caveats the host enforces.
Each platform has a policy row, the defaults its key starts from:

| Platform | Hosts at most | Joins at most | Opted in | Memory | Co-op |
| --- | --- | --- | --- | --- | --- |
| PC (Windows, macOS, Linux, FreeBSD) | 128 | 128 | 128 | 12 (4 GB) | yes |
| Steam Deck | 128 | 128 | 128 | 14 (16 GB) | yes |
| Android | 128 | 128 | 128 | 11 (2 GB) | yes |
| unknown | 128 | 128 | 128 | 0 | yes |
| Xbox | 16 | 32 (16 on a stock build) | 128 | 6 (64 MB) | no |
| Xbox 360, Wii U | 32 | 32 | 128 | 9, 10 (placeholders) | yes |
| Switch | 16 | 16 | 128 | 12 (placeholder) | yes |
| PSP, Vita | 16 | 16 | 128 | 6, 9 (placeholders) | no |
| Xbox One and Series (developer mode) | 128 | 128 | 128 | 12 | yes |
| PS3 | 16 | 16 | 128 | 8 (placeholder) | no |
| PS4, PS5 | 16 | 16 | 128 | 12, 13 (placeholders) | yes |
| Nintendo 64 | 0 (never hosts) | 8 | 16 | 3 (placeholder) | no |
| Legacy PC (DOS, Windows 9x) | 16 | 16 | 128 | 7 (placeholder) | no |
| Steam Link | 0 (doesn't host yet) | 16 | 128 | 8 | no |
| iOS, Apple TV | 128 | 128 | 128 | 11 (2 GB) | yes |

A host protects the weakest machine of its game. It takes no more players
than the smallest of its own hosting limit and every Delta machine's join
limit. A machine's join limit is its platform's row, or its key's own claim
if lower (never more); none if its player opted in. The host applies the
limit where it counts a free player slot, so its advertisement shows the
game full. A host never lies about the game's size: it only takes fewer
players. Players already in a game stay when a weaker machine joins it; the
host takes no more until the game is back under the limit. A weaker
machine's own build should not join a game already above its limit.

A host offers no network co-op while a machine of a platform without co-op
is in its game, nor on such a platform itself.

Without Delta (a machine that never said HELLO) none of this applies: plain
legacy behavior.

## Platform numbers

Every machine that speaks Delta says what it runs on: one number. Scoreboards
pick a logo from it, the game list sorts games by it, and hosts use its
policy row. The numbers, who each is for and how to ask for one are in
[registry.md](registry.md#platforms). Numbers are never reused; new ones go
at the end. Never pick a free number yourself: two ports could pick the same
one. Until you have one, send 0 (unknown): you still play, with no logo and
the unknown row.

What a policy row means: **hosts at most** is the most players a game it
hosts can have (0: it doesn't host); **joins at most** is the most players a
game it joins can have (a host keeps its game at or under it); **opted in**
is that limit when the player turns the caveats off; **memory** is log2 of
the megabytes the game gets; **co-op** says whether it plays network co-op.
A new platform's row starts careful (a few players, no co-op on small
machines) and goes up once the port plays well. A port may claim lower
limits in its own key; a host takes the lower value.

## Wire format

Little-endian, always (a big-endian machine builds each number from its
bytes). Every datagram is a 12-byte header and a payload, at most 1200 bytes
in all. The header is the same in every major, so a machine of another major
can always be answered with LEGACY:

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
| 1 | HELLO | client to host | capabilities (4), legacy number (2), machine index (1), build length (1, at most 31), legacy table serial (4), platform key (8), build (printable ASCII); then, from `chupa-44b`, the game modes it plays (4: bit 0 SWAT, 1 Hardcore, 2 Gun Game, 3 Infection; a build before them sends none, read as 0) |
| 2 | WELCOME | host to client | host's capabilities (4), agreed (4), legacy number (2), host's machine index (1; 255 none), build length (1), legacy table serial (4), platform key (8), build; then, from `chupa-44b`, the game modes the host plays (4, as HELLO's) |
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
| 22 | TRAITS | host to client | kind (1: 0 none, 1 Bomb), reserved (3), everyone's then the carrier's player traits, 12 bytes each: health, damage, resistance, speed (2 each, percent), shields (1: 0 unchanged, 1 none, 2 normal, 3 overshield), highlight (1), reserved (2); then, from the `bomb_movement` builds on, a version (1), reserved (3), and everyone's then the carrier's movement traits, 8 bytes each: jump, gravity, melee reach (2 each, percent; 0 unchanged), reserved (2), 48 bytes in all (older builds read the first 28); values past their ranges ([bomb.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/bomb.md)) refused. Grifball test builds 2 and 3 sent it as 18, which is CHAT_CONFIG's (a host drops those builds from a Bomb game: `bomb_traits`) |
| 23 | TEAM_REQUEST | client to host | sequence (4, above the session's last), player (1, the game's player list index), team (1: 0 red, 1 blue, 255 the other one), reserved (2) |
| 24 | TEAM_RESULT | host to client | sequence (4, the TEAM_REQUEST's), result (1: 0 changed, 1 at the next respawn, 2 the host takes none, 3 the teams would be uneven, 4 no teams, 5 too soon, 6 not now, 7 no such player of the machine, 8 already on that team, 9 the last player of their team), team (1, the player's now), seconds until the player may change again (2; with 5) |
| 25 | BADGE_ASK | client to host | nonce (32, fresh for each session) |
| 26 | BADGE | host to client | nonce (32, the BADGE_ASK's), signature (64, the certificate's server key's, of the proof below), certificate length (2, at most 143; 0 for none), proof (1: 2, "chupathingy server badge proof v2\n", the nonce, the host's invite and the session, [server-badges.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/server-badges.md); 0, the first drafts' nonce alone, shows no badge), reserved (1), then the certificate |
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
| 39 | HOST_NUMBER | host to client, if both agreed to `mcc_maps` | version (1; 1 now, 0 refused, a later one read as far as this build knows it), reserved (1), the network version the host's game is played at (2, little-endian; 0 refused), reserved (4). Sent at once when it changes, every second for the 20 seconds after, then every 5 seconds. When a host's lobby moves to an MCC map (its game at the MCC number, 44 from `chupa-44a`) or back, a client with `mcc_maps` plays the host's next settings record and messages at the new number without leaving (a record of another size from such a host is passed over, not fatal); the host sends its record again every 2 seconds for 20 seconds. A client that still has the map before never starts the game: the pregame load timeout drops it. Any other machine that joined at the old number is dropped with a notice |

A roster covers every machine of the game, in as many datagrams as it takes
(32 machines each). A client forgets a machine the roster has not named for
12 seconds, or that has no players.


Limits and timeouts:

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

## Examples

These are real datagrams, written by our own code and kept as the reference
client's golden packets ([golden.h](../../tools/delta-ref/golden.h)). A test
checks every block below against them byte for byte, so this page can't
drift from the code. The session is 0x1234abcd in each.

A port's HELLO: an N64 with machine index 2, playing network version 44,
offering `platform` alone and taking no tables (48 bytes):

```hex golden_hello_port
44 50 01 01 24 00 00 00 cd ab 34 12   header: HELLO, 36 bytes of payload, session 0x1234abcd
01 00 00 00                           capabilities: platform
2c 00                                 network version 44
02                                    machine index 2
0c                                    build text: 12 bytes
ff ff ff ff                           legacy table serial: takes no tables
11 01 00 00 08 10 03 00               platform key: n64, v1, hosts 0, joins 8 (16 opted in), 8 MB
6e 36 34 2d 70 6f 72 74 20 30 2e 31   build "n64-port 0.1"
00 00 00 00                           game modes: none
```

A dedicated server's WELCOME to it (52 bytes):

```hex golden_welcome
44 50 01 02 28 00 00 00 cd ab 34 12   header: WELCOME, 40 bytes of payload
df ed fb 03                           the host's capabilities
01 00 00 00                           agreed: platform
2c 00                                 network version 44
ff                                    the host's machine index: none (a dedicated server)
0c                                    build text: 12 bytes
0c 00 00 00                           legacy table serial 12
03 01 02 80 80 80 0c 00               platform key: pc_linux, v1, dedicated, 128/128/128, 4 GB
30 2e 39 2e 32 20 73 65 72 76 65 72   build "0.9.2 server"
03 00 00 00                           game modes: SWAT, Hardcore
```

A ROSTER of two machines, the room kept to the N64's 8 players (96 bytes):

```hex golden_roster
44 50 01 04 54 00 00 00 cd ab 34 12   header: ROSTER, 84 bytes of payload
01 00 00 00                           room capabilities: platform
08                                    room's player limit: 8
00 00 00                              reserved
02                                    2 entries
00 00 00                              reserved
01 03 00 00                           machine 1: Delta, platform key present
01 00 00 00                           agreed: platform
03 01 00 80 80 80 0c 00               platform key: pc_linux
00 00 00 00                           profile revision: none
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   player ID: none
02 03 00 00                           machine 2: Delta, platform key present
01 00 00 00                           agreed: platform
11 01 00 00 08 10 03 00               platform key: n64
00 00 00 00                           profile revision: none
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   player ID: none
```

LEGACY from a host of major 2, and a BYE (header only):

```hex golden_legacy_major2
44 50 02 03 00 00 00 00 cd ab 34 12   major 2, LEGACY, no payload
```

```hex golden_bye
44 50 01 05 00 00 00 00 cd ab 34 12   major 1, BYE, no payload
```

A dedicated server's advertisement reserved bytes, announcing 44 with the
Delta tag of wire `chupa-44b` ([The advertisement](#the-advertisement)):

```hex golden_advert_tag
2c 00                                 network version 44
05                                    flags: distributed netcode, dedicated server
00                                    the game's kind: none
00 00 00 00 00 00 00 00 00 00 00 00 00   bytes 4 to 16: zero
44 4c                                 the tag: 'D' 'L'
01                                    Delta major 1
01                                    the tag's layout version
2c 62                                 the host's wire: chupa-44b
1c 61                                 its oldest compatible wire: chupa-28a
9e                                    check: 0xa5 xor the eight bytes before
```

## The advertisement

A host's game advertisement (the legacy protocol's, on a LAN and through
the internet tunnel) has 26 (0x1A) reserved bytes. The game protocol fills
them from the front, Delta from the back:

| Bytes | Meaning |
| --- | --- |
| 0, 1 | the network version the host announces (little-endian) |
| 2 | flags: 0x01 the distributed netcode (always set), 0x02 the game is under way, 0x04 a dedicated server (version 39 and later) |
| 3 | the game's kind (version 37 and later: a game type in the low seven bits, 0 none, 1 SWAT, 2 Infection, 3 Gun Game; Hardcore in bit 7); 0 before 37 |
| 4 to 16 | zero, but for the old layout below |
| 17 to 25 | the Delta tag, when the host speaks Delta |

### The tag

| Byte | Meaning |
| --- | --- |
| 17, 18 | the tag, `'D'`, `'L'` |
| 19 | the host's Delta major |
| 20 | the tag's layout version, 1 |
| 21, 22 | the host's own wire: its number and its letter (44, 'b') |
| 23, 24 | its oldest compatible wire: number and letter (28, 'a') |
| 25 | a check: 0xA5 xor the eight bytes before |

A reader knows a Delta host by a valid tag: both tag bytes, layout 1, the
check, letters `a` to `z`, numbers from 1, and the oldest wire no newer than
the host's own. Never by a flag bit. Machines without Delta leave and ignore
the bytes. A wire ID names a revision of Chupathingy's game code
([versioning.md](versioning.md#wire-compatibility-ranges)), so only a build
of that code writes a tag; a port of other code writes none
([ports.md](ports.md#the-advertisement)).

The tag sits at the tail because the front is the game protocol's: byte 3
became the game's kind at version 37, and the flag 0x04 a dedicated server
at version 39. Delta used both before the tag.

### The old layout

Hosts before `chupa-39a` set the flag 0x04, then their major at byte 3, then
a wire ID as text in the 16 bytes after it (their oldest compatible wire
that released clients knew, from `chupa-29a`), and from 0.8.0a range bytes
in the last six:

| Byte | Meaning |
| --- | --- |
| 20 | layout version, 1 |
| 21, 22 | the host's own wire: its number and its letter |
| 23, 24 | its oldest compatible wire: number and letter |
| 25 | a check: 0xA5 xor the five bytes before |

A client reads the tag first and the old layout when there is none, so a new
client still finds an old Delta host. From version 39 the flag 0x04 may be a
dedicated server's: an old-layout host announcing 39 or later counts as
Delta only with a wire ID in its slot, and a kind at byte 3 is never read as
a major without one. The tag's check covers all nine of its bytes, so its
last six never pass for the old range bytes, and an old host is never read
as a new one: it has zeros where the tag's `'D'` and `'L'` go.

### The transition

Released builds before the tag read only the old layout: the PCs on
`chupa-38a` and before, and the consoles on `chupa-32a`. So a new host still
writes the old layout where it doesn't clash with what the bytes now mean:

- **The flag 0x04** means a dedicated server from version 39. A dedicated
  server that hosts with Delta sets it, which is true for both readers. A
  host with a player at it never sets it.
- **Byte 3** is the game's kind from version 37, read only in a Slayer game.
  With the flag, in a game of any other engine, a host writes its major
  there and the old slot after it (`chupa-29a`, ended before the tag).
  Older clients take that slot as the host's wire, which is in all their
  ranges.

What a released client gets from a new host:

| Host | Released client |
| --- | --- |
| a dedicated server, not Slayer | joins by Delta (its wire is in the host's range), whatever number the host announces; Delta Peer |
| a dedicated server, Slayer | says HELLO (the flag) and gets Delta Peer; joins by the network number |
| a host with a player at it | joins by the network number; no Delta Peer |

Joining by number works when the client's range reaches the number the host
announces. A console on `chupa-32a` can't join a Slayer game on a server
announcing 39 by number (it joins to 33), so a server for those consoles
announces 32. **The transition has no end date.** Hosts write the old layout
where it fits for as long as released builds need it, and new builds read
it. Stopping either is a deliberate decision
([versioning.md](versioning.md#principles), "Never drop an old protocol").

## The join mark

A client that will say HELLO to a Delta host marks its join request (the
legacy protocol's, otherwise as it is) in its machine name: of the name's 32
16-bit characters, the name takes at most 28 and ends with a 0; characters
29 and 30 (counting from 0) are 0x0394 and 0x0001, and everything else past
the name's end is 0. A longer name goes unmarked. Only a Delta host is sent
it. The host clears the mark, and for a marked machine waits up to 4 seconds
for its HELLO before deciding which game settings record it gets
([versioning.md](versioning.md#the-legacy-number)). A host without Delta
never looks, and machines without Delta never mark.


## Map identity

The legacy protocol names a game's map by a string alone, and two Halo PC
maps of one file name can be different maps (a map's versions, or two
authors' `bigass`). Between our machines, Delta Peer says which file the host
plays, with `ce_maps` agreed by both:

- **The families**, each with the folder its files go
  in beside `maps/` (the Xbox's own): 0 the Xbox's, 1 Halo PC
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
  made on a thread of its own so no frame
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
  join already tells the player the map is missing and where it goes,
or offers to download a Custom Edition map
  from the community map repositories,
  checked against the version the host advertises in the legacy protocol
  (the first 32 bits of its whole-file hash since network version 29, its
  header's checksum from an older host). Once it is joined, MAP checks the
  whole file as for any other.
- **Hostile input**: a MAP is read only from the client's host, of its
  session, with `ce_maps` agreed and not turned off by the kill switch; a
  name that is not a plain file name, a Halo PC map without its hash, or
  unknown flags are refused, never mended. A host takes no MAP.
## Moderation

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

## Chat

Text chat between Delta players, and the host's own lines (server messages),
over Delta Peer: `chat` (bit 3) carries players' lines, `server_messages`
(bit 2) the host's (CHAT_LINE of kind server or system). Each is agreed per
pair, so machines without Delta never send or receive either, and a Delta client
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
    welcome) goes to machines without Delta as the host's notice. With OpenCE
    players left out machines without Delta are sent nothing but the setting, and
    a refusal to one that says a line anyway. A vehicle line from OpenCE is
    a team line here (Delta has no narrower scope), and a team line in a
    game without teams is everyone's, as OpenCE has it.
  - **The OpenCE welcome** (off unless the host turns it on: a dedicated
    server's `opence_welcome`, a player's host's `network.opence_welcome`):
    a machine with no Delta session once its handshake's window is over
    gets one line, a few seconds after its player first spawns, once each
    time it joins, that this is a Chupathingy game. It goes as the game's
    notice message (17), the way OpenCE's own servers send their server
    messages, so their client shows it in its chat even where its chat is
    off; at most 63 characters of printable ASCII without `|`, one line of
    their chat. A Delta machine never gets it, nor does any machine while
    the host has no Delta (nobody can be told apart). `port/linux/game/
    opence_welcome.c` decides it, tested in `opence_welcome_test.c`.
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
and `sv_unmute`.

## Game modes

The game modes layer ([modes.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/modes.md)) plays OpenCE's game types by
their version 35 rules: SWAT and Hardcore in this build, Gun Game and
Infection to come. Nothing of a mode travels over Delta Peer: every rule
comes from the gametype's 16 further option bytes in the game settings
record, which machines without Delta carry too, and from the engine's own state.
Delta carries only who can play:

- **The capability**: `game_modes`, bit 25, with a **modes field** of 4
  bytes after the build text of HELLO and WELCOME (one bit a mode:
  `network_rules.h`'s `NETWORK_RULES_MODES_*`). A build before the field
  sends none, which reads as no modes. The signed table's kill switch
  turns the capability off like any other, and only between Delta
  machines: a host still hosts its mode game, but every Delta machine
  then lacks the capability and is dropped from it, while machines without Delta
  at 35 or later play on; our clients keep playing OpenCE hosts' modes,
  which need no capability. A build without the modes' rules (a console's)
  plays none and offers none: it leaves an OpenCE mode game with the plain
  notice, and its host sends no mode.
- **The refusal**: while the hosted game's options make it
  a mode game, a Delta machine without `game_modes` or the mode's bit is
  dropped with the rejection code `_rejection_code_version_too_old`, and
  every player gets a notice: "Player can't play SWAT: their build doesn't
  have it". A machine without Delta (OpenCE's, an older build's) plays on a
  host announcing 35 or later, where its record carries the mode, and is
  dropped on a host announcing an older number, where it would read a
  record without one and desync: "Player can't play SWAT: it needs network
  version 35 or later". The lobby waits for the handshake's window as a
  Bomb game's does ([bomb.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/bomb.md), "Delta only"); a machine joining a
  game in progress is dropped before it loads the game.
- **The wire**: `chupa-44b`. A mode game is content an older wire cannot
  play, so it ends the join cleanly; everything else plays as `chupa-44a`.
- **The kind** (OpenCE's 37): a host announcing 37 or later puts the game's
  kind (the mode in the low 7 bits, Hardcore in bit 7) in the
  advertisement's byte 3 and the internet listing's stamp, and leaves the
  old Delta layout out of such a game's advertisement, since byte 3 is the
  kind's. Our lists mark only the kinds this build does not play.

## Team changes

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
all the menu sets, and the dedicated
server's settings and `sv_team`.

## Host rules

Chupathingy plays multiplayer as the Xbox did. A few extras change what a
player sees of the others, so the host decides them for everyone in its
game, over `host_rules` (bit 19):

- **The host says them.** HOST_RULES goes to each client that agreed to
  `host_rules`, at once when they change (a dedicated server's `sv_set`)
  and every 5 seconds. A player's host takes them from its config.toml
  (`network.allow_player_names`, `network.allow_follow_camera`, its Host
  Setup), a dedicated server from its settings.
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

## Reports

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
  the game type, and the host's refusal counts for the reported machine.
Never an address. The queue keeps 200: when they are all
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

## Server badges

A dedicated server's OFFICIAL, DEVELOPER or COMMUNITY badge, over
`server_badge` (bit 16). The badge is a certificate signed with our badge
key for the server's Delta Control link key; the format, the keys and how
one is issued are in [server-badges.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/server-badges.md).

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
`server_badge.cert` in its data folder.

## Voice

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
  config.toml, a dedicated server from its settings (`voice`,
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
`delta_peer.c`, all tested in `delta_test.c` and
`voice_test.c`. A dedicated server relays without any audio or Opus of its
own. The consoles leave voice out for now: a
console is never sent any.

## Party teams

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

## Object sync

What the game's own messages leave out of the host's objects, over
`object_sync` (bit 17). The game messages stay as they are; a host sends
these only to the clients that agreed to it, so machines without Delta, older
builds and the console builds that do not offer it play as they always
did. Built for Halo PC maps whose client-side scripts read objects the
game's messages never describe (Coldsnap 20th XL; credit to ugoboom for
the map and the report):

- **Unit vitals.** The game's messages carry shield and health only for
  players' units and the AI's. A placed biped or an empty vehicle kept its
  full shield on a client until the host deleted it, so a script that read
  it there decided otherwise than the host's. The host
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
  the host's scripts made found nothing there. The
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

## Verified names

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

What machines without Delta get doesn't change. They see the 11-character Halo
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
   player shares their profile, their Halo
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

- The CISS is signed by one of the root keys and in
  date. A build never takes another player's certificate on its word, even
  with no root keys.
- The CDEV is signed by that issuer, issued inside the CISS's dates, and
  in date by this machine's clock, with 10 minutes of grace.
- The certificate has `play` (0x01).
- Its device isn't on the revocation list this copy last has (below).
- The proof is fresh by this machine's clock, with 10 minutes of grace
  either way. The host takes it no more than 10
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
from before keep their old free-form names on the site,
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
share their profile; everyone is still checked by
address and hardware ID.

**The server panel** shows a Profile column on its Players page: the name
and the device's platform for a player with a bound proof, "No profile" for
a copy with no profile, and "-" for anyone else. Its Role column counts the
account's role too. `sv_players` has the same as `"profile"` (the name, or
null) and `"device"`.

**Consoles** leave all of it out for now:
they never offer `identity` and show game names. The messages are fixed
fields, little-endian, read byte by byte, so a console can take them up
later without a special case.

**Test vectors** (also in `port/linux/tests/identity_vectors.json`; the
device key and certificate are "Device identity"'s). The nonce is the bytes
00 to 1f, the binding `0123456789abcdef0123456789abcdef`, machine 3, local
player 1, the certificate's serial 01 02... 08, bound (flags 1), signed at
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

## The legacy table's relay

HELLO and WELCOME carry each side's legacy table serial (0: the built-in
table; 0xFFFFFFFF: it takes no tables). The side with the newer serial sends
its signed table ([versioning.md](versioning.md#the-signed-table)) in TABLE
pieces of 1 KB, one transfer at a time to a machine, paced at five pieces a
second, under the host's ten messages a second from a machine. The receiver
puts the pieces together (one table coming in at a time; another machine's
waits until it has been quiet for 5 s), checks the signature and the
document as it does every other copy, and takes it only if it is newer. It
says its serial then in TABLE_HAVE, and again whenever its serial changes (a
table fetched mid-game is passed on the same way). The relay needs no trust
in the relay: only a table signed with the published keys is taken.

Bounded: the first pass waits 2 s after the handshake; a machine is sent at
most three passes a session, the next a minute after one ends; its tables
are checked at most once a minute; a host sends to at most four machines at
once. A machine that takes no tables says TABLE_HAVE 0xFFFFFFFF after the
handshake (or 0xFFFFFFFF in its HELLO), is sent none, and sends none.
