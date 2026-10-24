# Delta: compatibility

Status: design. Sections say what is built and what is planned. Written for
0.8.0; step one (0.8.0a) is in progress. For the protocol details this builds
on, see [delta.md](delta.md).

Delta is the network Halo never had on PC: one place that knows who is
playing, what they are running, and what can play with what. This document is
the part of that design about compatibility, the question every other feature
depends on: when two machines meet, do they play, and if not, why not and
what fixes it.

## 1. Principles

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
6. **Other clients are peers.** OpenCE and any other non-Delta client speak
   one level of the game protocol. Delta interoperates at that level and
   never changes what such a client sees, reads or displays. Delta features
   are additions for Delta machines, not requirements on anyone else.

## 2. Today (0.8.0)

### Wires

Each build has a wire ID, `DELTA_WIRE` in `port/linux/include/delta.h`
(`chupa-38a` from the release after 0.8.0g, `chupa-32a` in 0.8.0 to 0.8.0g;
`chupa-29a` and `chupa-28a` before it). It names the
revision of the game protocol the build actually speaks, which is not the same
thing as the legacy network number the original game protocol advertises. A
release that changes how a game plays gives itself a new wire.

A Delta host puts its Delta major and its wire ID in the reserved bytes of its
advertisement after the port's version and flags
(`DELTA_ADVERTISED_WIRE_OFFSET`). Clients that do not read those bytes see
nothing different.

### The exact-wire join rule

Today our client, in its default mode, joins a Delta host of its own major
and exactly its own wire, whatever legacy number either announces. A host of
another wire is refused with a message naming both wires. A host of another
major, or one old enough not to advertise a wire, is joined by legacy number
as before.

This rule is safe and it is too strict. It treats every wire change as
breaking, but most are not: 0.7.5 (`chupa-29a`) and 0.8.0 (`chupa-32a`) differ
by additive changes that machines of either side can live with. Exact matching
splits builds that would have played fine, and the split reaches the Xbox
build too.

### The signed legacy table

The legacy table (`DELTA_LEGACY_VERSIONS`, plus the signed document described
in "The legacy table as config" in [delta.md](delta.md)) tells a build which
legacy numbers to announce and join for its own wire. It is Ed25519-signed,
served by Delta List, passed between machines over Delta Peer, and extended
only by CI after the cross-play gate passes against the new OpenCE build.

What it can do for builds already released:

- widen the numbers a given wire announces and joins, when the cross-play
  gate has proven that wire plays with them;
- disable a capability network-wide (`disabled_capabilities`) and set platform
  policy.

What it cannot do:

- change the join rule itself. The exact-wire check is code in builds already
  out, and a table row for a wire is read only by builds of that wire.
- teach an old build a new message. A table can say a wire plays with a
  number; it cannot make the wire understand more.

So the fix for the 0.7.5 / 0.8.0 split has to ship in a build. Builds already
released stay on exact matching, and everything in this document is designed
to be safe with them: they keep working with each other, and we never rely on
them changing.

### Capabilities

Delta Peer's handshake (HELLO / WELCOME) agrees a capability set per pair of
machines: one bit per optional feature, never reused, unknown bits ignored.
Room-wide capabilities (`delta_peer_room_has`) are true only when every
machine in the game has them, and one non-Delta machine turns them off.
Capabilities gate Delta features. They do not gate the base game protocol, and
they are agreed after the join, so they cannot decide whether a join is
allowed.

## 3. Step one (0.8.0a, in progress)

Goal: stop the exact-wire split without changing anything the wire means.

- **The old slot says the oldest compatible wire.** A host advertises, in the
  slot builds already out read, the oldest wire it can still play with. A
  build of that older wire sees a host of its own wire and joins.
- **New bytes say the real wire and the range.** After the old slot, the
  advertisement carries the host's real wire and the range of wires it plays
  with. Builds that know the new bytes use them; builds that do not read the
  old slot exactly as before. Nothing is removed and no layout moves.
- **Table rows for older wires, proven.** Each older wire the new build claims
  to play with gets a row in the signed table, added only after the cross-play
  gate has run a real game between the two. No proof, no row, no claim.
- **Our own builds are the gate's first subjects.** The gate already runs
  per wire against each new OpenCE build; step one adds pairs of our own wires.

What step one does not do: it does not let a 0.7.5 client join a host that
changed something 0.7.5 cannot ignore. Those hosts advertise a range that
excludes it, and the player is told so (section 6).

## 4. Protocol levels and speaking down

A wire ID names a revision. A **level** is the unit of interoperability:
the set of game-protocol behaviors a machine speaks. Wires map to levels, and
a newer build can speak any older level it still carries.

### Gating every change

Every game-protocol change since a given level is gated, so a newer build can
fall back to the older behavior:

- a new message is sent only to a peer known to read it;
- a changed field meaning (for example the Custom Edition map version in the
  game record, which 29 changed from a header checksum to a hash prefix) has
  both meanings in the code, selected by the level in use;
- a changed rule of play is selected by level, not compiled in.

The test for a change is: can the build still produce the previous level's
bytes and behavior on request. If it cannot, the change is breaking and must
be called that (section 5). The cross-play gate checks this claim in CI, per
level pair, by playing a real game.

### Per-game negotiation

A game runs at the highest level all its machines share:

1. The host advertises its range (section 3).
2. A client whose range overlaps joins. The join does not carry the level;
   the host learns it from the handshake and from the client's wire.
3. The host fixes the game's level at the highest one every joined machine can
   speak, and lowers it when a machine of a lower level joins before the game
   starts. Once play has begun, the level does not change; a joiner who would
   need it lowered is refused with the reason.
4. Where a feature is per-peer (a message only one pair uses), the host gates
   it per peer instead of lowering the whole game. Features that change
   shared state (rules of play, map identity, anything every machine
   simulates) are per-game and use the game's level.

### Where capabilities fit

Capabilities stay what they are: optional features, agreed per pair after the
join, for things that can be absent without changing the game's rules (chat,
platform icons, profiles, moderation). Levels decide whether the machines play
the same game; capabilities decide what extras ride beside it. A feature that
changes the game's rules is a level change. A feature that does not is a
capability. A feature that starts as one and later has to change the rules
gets a level at that point; the capability bit is not stretched to cover it.

OpenCE and other non-Delta machines have no capabilities, so a room with one
of them is at their level for anything that touches the game, and has no
room-wide capabilities.

## 5. The compatibility map

Planned (0.8.1). The signed table grows from a list of announce numbers per
wire into the network's authority on what plays with what. It stays one signed
JSON document, the same key, the same distribution, the same format-version
rule (a build drops a document of a format it cannot read).

It carries:

- **Level interop.** For each wire, the level it speaks and the levels it can
  speak down to.
- **Number mapping.** For each OpenCE network number, the level it
  corresponds to, so a Delta client can decide whether a host of that number
  is joinable and at what level, and so our hosts know which numbers they may
  announce.
- **Network-wide feature disables.** Today's `disabled_capabilities`, extended
  to levels: a level or a behavior found broken can be switched off for every
  build that reads the map, without a release.
- **A minimum floor.** A wire or level below which builds cannot join, set
  only for a truly breaking change (a security flaw that cannot be gated, or a
  change that cannot be reproduced at the older level). The floor names its
  reason and the version that fixes it, and it is the only way a build is
  turned away on version alone.

Rules for changing the map:

- Entries are added by CI after the cross-play gate has played the pair. An
  untested pair is not in the map.
- Entries are never rewritten in place; a bad one is superseded by a higher
  serial that disables it and says why.
- The floor is raised by a person, not by the gate.

Distribution is unchanged: the site serves it (Delta List), the game caches
it, and Delta Peer passes it between machines, newest signed serial wins, so a
machine that cannot reach the site still learns it from a peer.

Verification is the cross-play gate in CI: for every pair of wires the map
claims, a real game on the real binaries, plus the OpenCE numbers it maps.
The map is only as honest as that gate, so the gate is the thing to protect.

## 6. Honest game lists and join failures

Planned, built on section 3's advertisement.

- **Advertisements carry ranges.** A host's entry in Online Games says its
  level range, not just one number. The list can then tell, per player,
  whether this machine can join it.
- **Joinable or not, and why.** Online Games shows each game as joinable or
  not for this machine, and for a game that is not, the reason in the player's
  terms: "needs a newer version (0.8.0a)", "this game is behind yours",
  "a map you do not have". Games are not hidden for being unjoinable; a player
  seeing why a friend's game does not appear is the point.
- **"Update to join."** When the reason is that the machine is behind, the
  prompt offers the update, the way a title update works: what the new version
  is, how big, and a path to install it (the updater where one exists, the
  download page where it does not). It never updates on its own.
- **The same words everywhere.** The game list on the site, the in-game list
  and a failed join use the same reason strings, so a player is not told
  something different in each place.
- **Failures stay clean.** A join that fails after the list said it would work
  (a stale map, a host that changed since) ends the join with the reason and
  does not start a game that might drift.

Games from non-Delta hosts are listed and joined by their legacy number as
today. They are marked for what they are, not degraded.

## 7. Beyond compatibility (the Xbox Live layer)

Compatibility is the first piece of a network. The others, in the order they
are likely to land, share its rules: each is additive, opt-in, available only
between Delta machines, and none changes what a non-Delta client sees.

- **Identity and profiles.** A Delta profile (handle, emblem, clan) shown in
  the scoreboard, roster, chat and Online Games for players who have linked
  one. The game's local name is the fallback, and non-Delta players keep
  showing their game names. Profile exchange exists today (`profile`); showing
  it in game is planned.
- **Presence.** Whether a player is online, in a game, and which. Friends and
  clan members can find each other from the list.
- **Invites.** An invite to a specific game carries the game's range, so the
  receiver sees at once whether they can join and the update prompt from
  section 6 if they cannot.
- **Title-update prompts.** The update path from section 6, extended to
  announcements the network chooses to push (a new version, a retired build),
  shown unobtrusively and never forced.
- **Stats.** Delta Stats already reports a game's events and results. Profiles
  tie them to players across machines and games.

Each of these is designed and shipped through the same gate as the rest:
capability-negotiated, privacy settings respected, fuzzed, and off for anyone
who has not opted in.

## 8. Phasing and open questions

| Phase | Contents | State |
| --- | --- | --- |
| 0.8.0a | Advertise the oldest compatible wire in the old slot, real wire and range in new bytes; table rows for older wires proven by the cross-play gate | in progress |
| 0.8.1 | Protocol levels and per-game negotiation; the compatibility map; honest lists and "update to join" | planned |
| Later | Presence, invites, profiles in game, title-update prompts | planned |

Open questions:

- **Where the host learns a client's level.** The join request does not carry
  it today. The handshake comes after the join; whether the level is read from
  the client's HELLO and a late refusal is acceptable, or the join request
  gains a field, depends on what a legacy host will tolerate.
- **Mid-game level changes.** Level is fixed once play starts. Whether a lobby
  between games may renegotiate, and how a host of a lower level is told, is
  undecided.
- **The floor's policy.** Who may raise it, how much notice, and how builds
  below it are told, beyond "only for a truly breaking change".
- **How many levels to keep.** Speaking down costs code in every build. A cap
  on how many levels back is kept would limit that, but conflicts with
  principle 4; any cap needs a decision and a public reason.
- **Consoles.** Consoles run the same Delta core; how much of the map they can
  hold and verify, and how they present the update prompt, needs checking per
  platform.
- **Update paths per platform.** The prompt needs somewhere to send the
  player. Platforms without an in-game updater (Mac downloads, consoles)
  need an honest message instead.
