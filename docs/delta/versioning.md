# Delta versioning

How Delta's own versions work, and how Chupathingy builds play the legacy
protocol (the game protocol Halo's ports share, versioned by a network
number) with every machine, old and new. The byte layouts this page refers
to are in [peer.md](peer.md).

## Principles

1. **Players play.** A join is refused only when two machines truly have no
   common ground. Version numbers differing is not that.
2. **Say why, offer the fix.** When a join is refused, the player is told
   which side is behind, by what, and what to do about it (update, or pick
   another game), the way a console title update works.
3. **Never desync.** Playing on a lower common level is always preferred to
   playing on a guess. If two machines cannot be shown to agree on the game's
   rules, they do not share a game. A mismatch must fail cleanly at join, not
   drift apart mid-match.
4. **Never drop an old protocol.** Every protocol change is additive and
   gated. Older builds keep working with newer ones. Removing a level needs
   an explicit decision and a public reason. A security fix may refuse one
   vulnerable message, never a whole version.
5. **The network is the authority, not each build.** A build knows its own
   code. It does not know what the other builds out there can do. That
   knowledge belongs to a signed document the network publishes and updates
   without a release.
6. **Other clients are peers.** Any client without Delta speaks
   one level of the game protocol. Delta interoperates at that level and
   never changes what such a client sees, reads or displays. Delta features
   are additions for Delta machines, not requirements on anyone else.

## Each part's versioning

| Part | Versioned by |
| --- | --- |
| Legacy number | the legacy protocol; matched through the compatibility table and the signed table |
| Delta Peer | Delta major plus capability bits |
| Delta List, Stats, Link (HTTP) | the path's `/v1`; a breaking change gets `/v2` beside it |
| Delta Stats events | the event schema's own version |
| Delta Control | the API's `/v1`; the web page ships with the server |

## The legacy number

The legacy protocol's network version (the "legacy number") goes in the
advertisement, and a client without Delta joins only a host with the same
number. The number goes up often, usually for additive changes (a message
older machines drop). Delta handles it with a compatibility table and a
signed table of the numbers each wire is proven to play.

What each side checks, since 0.7.5a:

- **Delta games go by our wire, not the network number.** A host that speaks
  Delta puts its Delta major and its wire in its advertisement's reserved
  bytes, in the Delta tag at their tail since chupa-39a
  (`DELTA_ADVERTISED_TAG_OFFSET`; "The advertisement", below), after the
  port's version and flags before it. Our client, unless set to `network.protocol =
  "opence"`, joins a host of its own major and wire whatever network number
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
  whose legacy number says the newest version that build plays,
and sends it the record of the older of that and
  its own; a machine without Delta gets the announced number's. A host that
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
  next network number (`DELTA_LEGACY_PRESUMED`, 1, past the one announced)
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

## Wire compatibility ranges

Since 0.8.0a a wire is not an island. `DELTA_WIRE_COMPATIBILITY` in
`delta.h` gives each wire the oldest wire it plays the game with, without a
desync and without losing what the older wire has: a feature the older wire
lacks is dropped or unused with it, and content it can't play ends the join
cleanly. Every wire from `chupa-29a` on plays with `chupa-28a` and newer
([registry.md](registry.md#wire-ids) lists each wire's builds):

- `chupa-44b` (0.9.2: the game modes layer) sends the game's messages as
  `chupa-44a`'s; a game of a mode refuses older wires with a notice rather
  than desyncing.
- `chupa-44a` (0.9.1: network version 44 built in, MCC games at the newest
  proven number, MCC package requests declined) sends them as `chupa-43a`'s.
- `chupa-43a` (the layout facts a table row can carry, "Layouts") plays every
  older number as `chupa-41a` does.
- `chupa-41a` (0.8.1d on: the longer join request to hosts of 40 and later)
  and `chupa-39a` (0.8.1b and 0.8.1c: the Delta tag) send the game's
  messages as `chupa-38a`'s.
- `chupa-38a` (the builds with 33 to 38) plays as `chupa-32a` does: a host of
  32 is the same game to both, and a host of 35 or later sends each Delta
  machine the settings record of the newest version its HELLO says it reads.
- `chupa-32a` (30 to 32 are additive: a machine of 29 drops message 90 and
  ignores the co-op input byte; 31's keep-alive, stamp window and
  stream-only messages were already in 29) and `chupa-29a` play with
  `chupa-28a`.

A row is written with its release and never changed: a build can't learn of
a newer one, so a host also says its range in its advertisement.

**The client's decision**: it joins when the host's
own wire is in the client's range (the client's oldest to its own), or the
client's wire is in the host's range; for an old-layout host without valid
range bytes, when the old slot's wire is the client's own (the rule since
0.7.5a). It refuses, naming both wires, only when neither holds. A host of
another major, or one that says no wire, is joined by its network number, as
before.

**The consoles decide the same way.** The original Xbox, Xbox 360 and Wii U
builds (`HALO_CONSOLE_DELTA` or `HALO_CONSOLE_ONLINE_GAMES`) take this path
too, so they join a Delta host whatever network number it announces, and fall
back to the numbers only for a host without Delta. A refused game gives a
short reason the console's Online Games screen shows: the host's wire, or
its network version and whether it is newer or older.

## The console floor

Every Chupathingy platform stays in one pool. The consoles move slowest
(their builds follow the main repository through a submodule), so they set
the floor: `DELTA_WIRE_CONSOLE_FLOOR` in `delta.h`, `chupa-32a` on
2026-10-27, the oldest wire a console build in the wild plays (the original
Xbox's, the Xbox 360's, the Wii U's and the Switch's). Every row of `DELTA_WIRE_COMPATIBILITY` from the
floor on must reach back to it, and `DELTA_WIRE`'s row must reach every
console wire in the wild; `tools/test_delta.py` (`test_console_floor`, in
CI) fails otherwise. Raise the floor only when every console port has
shipped a newer wire.

## Legacy versions

The network versions our builds know, each with the first release that had
it and whether a machine of the version before plays with it as it is
(additive: messages a machine of the older version drops) or not
(breaking). Versions 1 to 9 each changed the wire format. The newest
breaking one is 11, so our clients join hosts of 11 and later.

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
| 30 | build-206 | A rule every machine computes alike: the riders of an elevator are carried by its node's movement of the tick (sampled before the machine updates, applied once its node matrices are made), not the previous tick's, which went the wrong way on the tick an elevator reversed. A host takes only the across of a client's word on a unit riding an elevator, one that is moving or that the unit stands on at rest (a remembered elevator alone, after a jump, is not riding); a client discards the height of the host's correction while it stands on the elevator it recorded, up to 4.0 world units. No message changed: a machine of 29 carries riders a tick late, and its host's states correct them | additive |
| 31 | build-206 | The pregame keep-alive is a 4-byte message whose last two bytes are 0 (they were the sender's stack), and the packet table lists begin game, graceful exit and keep-alive each with its own fields (the bytes of the others as before). A datagram stamped more than 450 ticks (15 seconds) from the latest taken from its sender is dropped; over a sender's stream any stamp is taken. client_ready, client_identity and hit reports are taken from a client's stream alone. A second game on one computer: a client takes any free port if the client port is in use, and the host answers a machine at the port its datagrams came from, finds a client by address preferring a machine that is not its own, and answers a game search from another port at that port. Message layouts as before | additive |
| 32 | build-244 | The host's policy on aiming assist for the mouse and keyboard, a new message (90, 12 bytes, reliable: the header, 0 or 1 as a 16-bit value, 16 bits of 0), sent to a client when it has loaded and to every client when the setting changes; a client honors it, and is blocked until a host of 32 or later speaks. A client's input entry says in byte 2 (padding before) whether its player plays co-op without friendly fire; our hosts read it and keep their own friendly fire setting. A release (wire `chupa-32a`), so older builds' wire never gets a row for it. A machine of 29 to 31 drops message 90 and ignores the byte. | additive |
| 33 | build-266 | Co-op's second switch: bit 1 of the settings' co-op byte (the byte bit 0 of which is 19's player collisions) says the players' hits do their AI allies no harm (OpenCE's Server Setup's ALLY FRIENDLY FIRE: OFF). Only a host acts on it; a client takes it as it is. In no release of OpenCE's (testing builds alone) | additive |
| 34 | build-266 | No message changed, but which objects are static targets and what a client does with them did: scenery and devices that are a shield alone (Rat Race's energy shields, Bigass's tripmines) are static targets too, which a client reports its hits on and applies the host's events to; every machine runs static targets' shields (stun and recharge), as no host sends them; a client reports no damage whose owner object is a static target (the host deals it); and an event naming an index that is no object on the client (the host's own copy of one made during play, a tripmine) goes to the client's nearest static target within its bounding sphere and 2.0 world units of the damage's origin (and an area effect's cutoff radius). OpenCE raised the number with the first of these alone, so a host of 34 may have any of them. In no release | additive |
| 35 | build-266 | OpenCE's own game types, SWAT, Infection, Gun Game and Hardcore: the gametype's PC options grow from 0x1C to 0x2C bytes (a mode, a hardcore flag, Infection's and Gun Game's options, 6 reserved), and the settings record with them, to 13,136 bytes at the network's slots. A machine of 34 can't read it (its pieces' total size is another), so it is breaking for OpenCE's; our clients take the size by the host's version, and leave a game of a game type they don't play, saying which. In no release | additive |
| 36 | build-266 | The host's shield effects, a new message (91, 12 bytes, reliable: the header, 0 to 5 as a 16-bit value, 16 bits of 0), sent to a client when it has loaded and once each game; a machine of 35 drops it. Our clients take it and draw stock, the only look they have | additive |
| 37 | build-268 | A listed game's kind: one byte, an OpenCE game type in its low seven bits (0 none, 1 SWAT, 2 Infection, 3 Gun Game) and Hardcore in bit 7. In the LAN advertisement it is the fourth reserved byte (zero before), in the internet listing the first byte of the 8-byte stamp before the signature (the signature covers it; the length is unchanged). A game type counts only on the Slayer engine; Hardcore alone names the engine (HARDCORE SLAYER). It decides only the name in the list. Our clients take any value, name the games they do not play and mark them like a game of a newer build; our own hosts send 0. A Delta host's fourth reserved byte is its Delta major (below), so an OpenCE 37 client reads a Delta host's kind as 1 | additive |
| 38 | build-268 | The shield effects message takes a seventh value, 6, "players": the host does not dictate and each machine uses its own player's settings. A machine of 37 or older drops it. Ours draws stock for every value | additive |
| 39 | build-294 | A dedicated server's game: the advertisement's flag 0x04 (until then Delta's, which moved to the Delta tag for it) and the internet listing's flag 32 (ours is 128; a dedicated server sets both). OpenCE's browser shows DEDICATED only for servers its master server lists, so ours appear under ALL GAMES, as we don't speak its master (below). No message or record changed. Our dedicated servers set the flag 0x04 when they announce 39 or later or host with Delta, and the listing's flag 32 when they announce 39 or later; a desktop build or dedicated server with no table row for its wire hosts at 41 (the last release's row, `DELTA_BUILT_IN_HOSTING`), a console build at its own `HALO_PORT_NETWORK_VERSION` (32), and a newer number comes from the signed table ([Auto-announce](#auto-announce)). The row's first build is the oldest published OpenCE release whose commit has the change (checked by git ancestry on 2026-10-27); older releases were removed, so an earlier one may have had it | additive |
| 40 | build-334 | The client's join request is 234 bytes longer after the hardware id (a pass and a proof for OpenCE's own servers). A host of 40 or later drops a request of another length without a reply; an older host reads the first 0x70 bytes and ignores the rest. Our clients send the bytes as zeros to hosts of 40 and later (wire `chupa-41a`; the length is a layout fact, "Layouts"). A dedicated server's refusals 8 to 12 are new; our clients say them plainly. In no release of OpenCE's alone | additive |
| 41 | build-334 | A refusal 13 (too many machines from one address), messages 78 and 79 laid out anew, and 94 and 95 (map votes) new. Our machines drop the vote messages unread, and a host tells an OpenCE player who votes, once, that it has no votes. Co-op's flags bit 2 (a dedicated host) is ignored | additive |
| 42 | build-344 | In no release of its own: the join request gains a version word, the distributed header grows to 12 bytes (a checkpoint epoch, and the host's boundary message 96), a unit state gains a scripted pose (bit 6), the settings record is 64 bytes longer, and MCC-only messages are added. Played by the host's number | additive |
| 43 | build-344 | A unit state's deck part (bit 7) and message 107 (riding, which our machines drop), riders thrown from a vehicle on its side; with 42's changes, played by the host's number | additive |
| 44 | build-355 | No message, record or size changed: the number alone, with 43's layout and rules. Its MCC package sharing is declined by our hosts | additive |

## The signed table

The legacy number goes up often, mostly for changes older machines can live
with. Following it should not take a release each time, so the table our
builds check is config, not code: signed, served by Delta List, and passed
peer to peer over Delta Peer. Its old name is "the legacy table as config".

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
  `delta.h`, `chupa-44b` today, from the build with the game modes layer ([modes.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/modes.md)); `chupa-44a` was the build that follows OpenCE's 44 (0.9.1); `chupa-43a` came before it; `chupa-41a` from 0.8.1d; `chupa-39a` was 0.8.1b's and 0.8.1c's, `chupa-38a` 0.8.0h's; `chupa-32a` was 0.8.0's to 0.8.0g's and the original Xbox build of 2026-10-21's; `chupa-29a` came before it; `chupa-28a` was 0.7.4e's and 0.7.5's, `chupa-26a` was 0.7.2e's and 0.7.3e's, `chupa-24a` was 0.7.1e's, `chupa-23a` 0.7.1b's, `chupa-20a` 0.7.0b's): the revision of the game protocol it
  actually speaks. A row says which network numbers that wire was proven to
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
  (`platform`, `chat`,...). Names a build doesn't know are ignored. Delta
  Peer never offers, agrees to or uses a capability the table in use turns
  off: not in HELLO or WELCOME, not in the room's set, and a table that
  turns one off mid-game takes it out of the room at the next roster.
- **`platform_policy`** (reserved, optional): per-platform limits Delta Peer
  reads, such as how many players a host or a joining machine of that
  platform may have, keyed by platform name (`xbox`, `xbox360`, `wiiu`,
  `switch`, `android`, `pc_windows`,...): an object of objects of small
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

`make` writes this build's wire and numbers
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
up to its own last one: the
primary key's is 0, the recovery key's 255. A table whose epoch is past its
signing key's last is dropped whole, like one that doesn't verify.

That makes a leaked primary key recoverable. Serials only go forward, so a
leaked key that signs the highest serial it can (0x00FFFFFF) would leave
every build refusing every later table; but the recovery key signs a table
of the next epoch,
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
| `unit_state_parts` | (`chupa-43a` on) a unit state's parts a build passes over: `[{"bit": 7, "size": 28, "at": "after_up"},...]`; `after_up` (after the up, before the predicted time: alive and placed alone) or `last` (after the camouflage: alive alone); a state with a part where it cannot have one, or a bit neither built in nor said, is unreadable | bits 6 and 7 (0 to 5 are built in), 1 to 64 bytes; a place a build doesn't know is not taken |
| `rider_ejection_up` | (`chupa-43a` on) riders leave a vehicle on the ground whose up's height is below this, in thousandths (built in: below 0, upside down); only in a network game of the version | 1 to 999 |

So when OpenCE changes the join request's length again, a row like
`"43": { "join_request_extra": 300, "join_request_fill": "zeros" }` for
`chupa-41a` is the whole follow, without a release. Builds of older wires pass
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
document made from serial 11 with both.
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
record into its own and a host makes one,
the newest version a Delta client's
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

### Auto-announce

Hosts announce the newest network number they're proven to host, and they
follow the signed table on their own. Nobody has to edit a file when OpenCE
moves.

- **Which number.** A wire's row may carry `"hosting"`, a list of numbers
  the cross-play gate proved with our host and OpenCE's clients alone (the
  gate's ours-host and server directions passed; their host may still need
  a release for our clients). A build announces the newest of the row's
  `announce` and that list. Older builds
  skip the list and announce `announce`, as they always did. The join range
  stays the row's `minimum` to `maximum`: hosting 41 doesn't make our
  clients try OpenCE's 41 hosts.

  ```json
  "chupa-39a": {"announce": 39, "minimum": 11, "maximum": 40, "hosting": [41]}
  ```

  The list is oldest first, each number once, at most 8.
  `tools/delta_table.py make --hosting chupa-39a=41` adds to it, and a later
  `--row` keeps it.
- **When it changes.** When a newer table arrives (every 4 hours, every 30
  minutes on a dedicated server, at start, or from another machine over
  Delta Peer), the new number is taken at once, unless a game this machine
  hosts is under way. Then it waits for the lobby, so one game is played by
  one number (`delta_legacy_hold`, from the advertisement's state). The log
  says "now announcing network version 41 (was 39)". The LAN
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
- **Local tables** may carry `"hosting"` too, and
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
  as had yet. 0.9.0 can limit the machines from one address,
but that is off by default on a
  player's host (8 on a dedicated server), so the name stays at 0 and a
  row that requires it still makes 0.9.0 announce its own number. A name
  is never renamed or reused.
- **A build that lacks one** (or doesn't know the name) announces its own
  number, whatever the row's `announce` and
  `hosting` say, and `network.announce` can't pin past it. It logs one
  line: "not hosting network version 42...: it requires the host
  protection address_machine_limit, which this build does not have; announcing
  32". It still joins the row's range as a client: clients enforce
  nothing for a host.
- **A row without `requires`** works as before. Old tables, and the
  servers' local tables, are unchanged.
- **Builds before 0.9.0 skip `requires`**, and 0.8.1d (`chupa-41a`) reads
  `announce` alone. So a `chupa-41a` row that requires anything keeps
  `announce` at 32, and newer numbers go in `hosting`, which only builds
  that also read `requires` follow. Wires from `chupa-42a` on can raise
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

### Never send message 92

OpenCE's distributed message 92 (`client_build`, added without a version
raise) sends a player's build, the repository it was made from and their
whole mod list to any host, which OpenCE's tools then mark and search by.
**Our machines never send it**, and our hosts drop it from OpenCE's clients
unread, as an unknown kind, without logging it or keeping what it says.
Not sending it
costs no compatibility, as no version needs it. Don't add it "for
compatibility". If OpenCE ever makes it required to join or be listed,
following OpenCE ends there (README's "up to network version N",
`DELTA_LEGACY_PRESUMED` to 0) rather than sending it. The same holds for
OpenCE's master server and fleet tools: Delta List and Delta Control are
ours, and we don't speak theirs.

## What a table can and can't do

What it can do for builds already released:

- widen the numbers a given wire announces and joins, when the cross-play
  gate has proven that wire plays with them;
- disable a capability network-wide (`disabled_capabilities`);
- give the facts of a newer OpenCE version's layout that a wire reads by data
  (`layouts`, from `chupa-38a`), such as the join request's length;
- prove numbers for hosting alone (`hosting`), which hosts then announce on
  their own (auto-announce, from 0.9.0);
- name the host protections a host must have to announce past its own number
  (`requires`, read from 0.9.0).

`platform_policy` is reserved for per-platform limits; no build reads it
yet.

What it cannot do:

- change the join rule itself. The exact-wire check is code in builds already
  out, and a table row for a wire is read only by builds of that wire.
- teach an old build a new message. A table can say a wire plays with a
  number; it cannot make the wire understand more.

So a change to the join rule ships in a build. Builds already released keep
the rule they shipped with, and everything here is designed to be safe with
them: they keep working with each other, and nothing relies on them
changing.

## Capabilities and joins

Delta Peer's handshake (HELLO / WELCOME) agrees a capability set per pair of
machines: one bit per optional feature, never reused, unknown bits ignored.
Room-wide capabilities are true only when every
machine in the game has them, and one non-Delta machine turns them off.
Capabilities gate Delta features. They do not gate the base game protocol, and
they are agreed after the join, so they cannot decide whether a join is
allowed.

## Legacy compatibility

A row of the signed table is a claim that builds of a wire play a number.
The claim is added only after a real game proves it: the cross-play test
 plays release builds of each wire still in use
against a release that speaks the new number, in both directions, and
against our dedicated server. A pass may widen the join range; a fail adds
nothing, and the number needs a release. **What hosts announce always needs
a person**: a change to any row's `announce` or `hosting`, or a protection
taken out of `requires` ([Host protections](#host-protections)), is
published only by a run a person started, never by a test on its own.

Nothing promises to follow any other build's number. Our clients join the
next number from day one (presumed additive, above), and a number that
needs code takes a release like any other change.

### Taking a row back

A row found wrong is taken back with a newer table whose row for that
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

## Join reasons

- **The same words everywhere.** The game list on the site, the in-game list
  and a failed join use the same reason strings, so a player is not told
  something different in each place.
  Done for 0.8.1: the sentences are in `port/linux/include/halo_join_reasons.h`
  ("Needs a newer Chupathingy.", "The host is on an older version.", "Map not
  installed:...", "Game is full.", "Password needed." and the rest), used by
  Online Games, the PC Server Browser and the failed-join message. The site's
  list has the same ones (puma `web/site.js`, `JOIN_REASONS`), and
  `tools/test_join_reasons.py` holds the list both follow.
