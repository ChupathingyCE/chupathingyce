# Delta security

Delta is open, so it is built for hostile peers, not friendly ones, without
asking players for anything.

## Principles

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
- **Proven numbers.** Builds already out announce a new legacy number only
  after the cross-play test has played them against it, and only when a
  person publishes it ([versioning.md](versioning.md#legacy-compatibility)).
  The number alone is never matched blind.

## Delta Peer

- On the internet, Delta Peer is inside the tunnel, so it's encrypted and
  bound to the invite's host, like the game. On a LAN it is not, like the
  game.
- Nothing on Delta Peer is trusted for game state or stats. The host stays
  authoritative, as in the legacy protocol: a client's claim (its platform,
  its profile) is shown as a claim, and stats come from what the host saw.
- Every message has a size limit and is parsed as hostile input, to the
  same standard as the rest of the network code.

## Where the rest is

- Parsing and limits of every message: [peer.md](peer.md#wire-format).
- The signed table's checks, keys and epochs: [versioning.md](versioning.md#checks).
- Device certificates, revocation and verified names: [link.md](link.md#device-identity)
  and [peer.md](peer.md#verified-names).
- Server roles and the control API: [control.md](control.md).
- What a third-party app may ask for: [clients.md](clients.md).
