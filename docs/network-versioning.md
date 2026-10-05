# Network versioning: design

Status: proposal (October 2026). Nothing here is built yet.

## The problem

OpenCE's builds carry one network version, `HALO_PORT_NETWORK_VERSION`, in
the reserved bytes of a host's game advertisement (`port/linux/NETCODE.md`,
"Versions"). An OpenCE client joins a host only if the two numbers are
equal. OpenCE raises the number with any change to what machines send each
other, however small, and does so often: 11 to 14 in about a day (October
5-6, 2026), each step for network co-op only.

Most of those changes are additive: a new message kind that a machine of the
older version drops because it doesn't know it. Multiplayer between the two
versions is unaffected. But because the number must match exactly, every
raise splits the players until both sides update:

- OpenCE players can't join ChupathingyCE hosts (the [D] servers included)
  until we announce their number.
- ChupathingyCE players can't join OpenCE hosts unless our accepted range
  covers their number.

Matching by hand (`network-version-13`, then `network-version-14`) costs a
pull request, a release and a server rollout each time, and the gap lasts
as long as that takes.

## Goals

1. OpenCE and ChupathingyCE players keep playing together across OpenCE's
   additive raises, with no hand work on our side for the common case.
2. A raise that really changes the wire format (a struct layout, an
   encoding) is caught and never shipped as compatible.
3. ChupathingyCE's own features stop depending on OpenCE's numbering.
4. Nothing changes for OpenCE machines: they see exactly what they see
   today.

Non-goals: changing how OpenCE versions its builds (that is a conversation
for later, see the end), or changing how games are joined.

## Two layers

### 1. The legacy layer: OpenCE's number

Kept exactly as it is on the wire, as a fallback that everyone understands.

- **Hosts** announce the number of OpenCE's newest *released* build that we
  are wire-compatible with, so OpenCE clients join them.
- **Clients** join hosts of every number in a compatible range (as
  `HALO_PORT_NETWORK_VERSION_MINIMUM` to `_MAXIMUM` already do), and the
  browser lists those games (as `p2p_lobby.c` does since 0.6.4b).
- We never invent numbers in this space. It stays OpenCE's.

### 2. Our protocol: version and capabilities

Negotiated over the Chupathingy channel when a ChupathingyCE client joins a
ChupathingyCE host. OpenCE machines never see it.

- **Protocol major** (a small integer), raised only for a breaking change
  to anything our machines exchange. Two ChupathingyCE machines of different
  majors fall back to the legacy layer only.
- **Capabilities**: one bit per optional feature, for example network
  co-op, AI sync, Halo PC maps, HaloMD maps, server messages, stats events,
  the platform icon. Each side sends its set; the game uses the
  intersection. A feature the other side lacks is simply not used, with no
  error.
- The handshake rides on the channel's first exchange after the join, and a
  machine that gets no answer treats the other as legacy-only.

This is the same pattern as the Chupathingy channel's existing plan: our
extras over our own channel, a capability handshake, and a silent fallback.

## The compatibility table

The core of the design: one machine-readable file in the repository,
`port/assets/network/versions.toml` (name to be settled), with a row per
OpenCE network version:

```toml
[[version]]
number = 13
first_build = "build-119"
change = "co-op: extra enemies per player"
kind = "additive"          # additive | breaking
messages = ["coop extra enemies"]
verified = "2026-10-05"    # the cross-play test that passed

[[version]]
number = 11
first_build = "build-76"
change = "the game settings record carries the gametype's PC options"
kind = "breaking"
```

Everything else is generated from it at build time:

- the client's accepted range: the newest version, back to the last
  `breaking` row;
- the number hosts announce: the newest row whose `first_build` OpenCE has
  released;
- the README's "Compatible with" line and the command repo's manifest.

Known history, from `NETCODE.md` and OpenCE's commits:

| Version | First build | Change | Kind |
| --- | --- | --- | --- |
| 1 to 9 | | the netcode's growth: joins in progress, player slots, PAL maps, corrections, host's rules, cheaters, Discord user, hardware id | breaking (each) |
| 10 | build-73 | players' pings for the scoreboard | additive over 9 |
| 11 | build-76 | the gametype's PC options in the settings record | breaking |
| 12 | build-118 | network co-op | additive |
| 13 | build-119 | co-op extra enemies | additive |
| 14 | after build-122 | co-op devices' positions, units opening and closing | additive |

## Automation

1. **Watch.** The command repo's watch workflow already sees OpenCE's
   releases. When `HALO_PORT_NETWORK_VERSION` changes in one, it starts the
   rest.
2. **Classify.** A script diffs the build against the previous one and
   proposes a kind:
   - *additive* when the changes are new message kinds or enum values
     appended at the end, new handlers, and nothing in the packet encoders,
     struct layouts, limits or the settings record changed;
   - *review* for anything else.
3. **Propose.** It opens a pull request adding the table row.
4. **Verify in CI**, which is what makes the automation safe:
   - golden packets: recorded packets of each listed version decode with
     our current code;
   - cross-play: CI builds OpenCE at that build's commit, then plays a
     loopback game against ours in both directions (their host and our
     client, our host and their client), checking joins, kills, scores and
     positions, as our two-copy tests do today.
5. **Ship.** If the row is additive and CI passes, the pull request merges,
   a release goes out (our clients update themselves), and the [D] servers
   and Tex roll to it when they're empty. If the row needs review or a test
   fails, nothing ships and Donut posts to #upstream-watch for a person.

The remaining gap is the time between OpenCE's release and ours finishing:
minutes to an hour, instead of a day of hand work.

## Later

- **Per-peer announcing ("chameleon" hosts).** A host compatible with
  versions 11 to 14 could put each joining machine's own number in the
  advertisement it sends that machine through the tunnel, so OpenCE players
  on different builds share one [D] server. It needs the peer's version
  before the advertisement (from the tunnel's handshake), and must never
  claim compatibility the table doesn't support. An experiment for our own
  servers first.
- **Proposing capabilities upstream.** The exact-match rule is what splits
  OpenCE's own players on every raise. Splitting the number into a protocol
  version for breaking changes plus capability flags for additions (the
  advertisement already has spare flag bytes) would end that for everyone.
  Running this design ourselves first makes that proposal concrete.

## Phases

1. This document, and the compatibility table with the known history.
2. The generated range, announced number and README line from the table.
3. The watch workflow's classifier, the automatic pull request, and the CI
   cross-play test against OpenCE builds.
4. The protocol major and capabilities over the Chupathingy channel.
5. The per-peer host experiment, and the proposal to OpenCE.
