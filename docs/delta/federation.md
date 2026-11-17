# Federation (proposal)

**Status: proposal.** Nothing on this page is built yet. It describes how
any project can run its own network on the open network spec (the formats
on these pages), with its own accounts and servers, and still play and
verify names with every other network. Delta is Chupathingy's
implementation and network: one network among others, not the spec itself. Comments
are welcome on the issue tracker.

## The idea

Today there is one network, Delta: Chupathingy's site issues the
certificates that prove a player's name ([link.md](link.md#device-identity)). That works, but
it means every port, fork or community that wants verified names, friends or
profiles has to rely on one project's servers.

Federation removes that. Like email or Matrix, each community can run its own
network, and the networks work together:

- **Your network, your accounts.** A project runs its own site, its own
  sign-in and its own moderation. Nobody else gets access to its servers,
  its database or its players' data.
- **One player base.** A player from any network can join any game, and the
  game can check that player's name, whichever network issued it.
- **No live calls between networks.** Checking a name never contacts the
  other network. If a network goes down, its players keep playing; they
  only stop getting new certificates until it's back.
- **Optional for everyone.** A game, a port or a server that doesn't take
  part loses nothing. The legacy game protocol stays the common ground, as
  everywhere in the spec.

## Names

A name is a handle plus the network that issued it:

```
handle:domain
milenko:chupathingy.org
someone:example.org
```

- The handle follows today's rules (lowercase `a-z 0-9 _ -`, 3 to 20
  characters, [link.md](link.md)).
- The domain is the network's own: the one whose keys signed the player's
  certificate.
- Two networks can each have a `milenko`. They're different players, and the
  domain keeps them apart.
- A game may show names from its own network without the domain, and names
  from other networks with it, so a familiar name never hides where it came
  from.

## How a name is checked

Each network uses the same chain the spec already defines
([link.md](link.md#device-identity)), with its own keys:

1. **Root keys** (a primary and a recovery key), kept offline by the network.
   They sign the network's **issuer certificate** (CISS).
2. **The issuer** signs short-lived **device certificates** (CDEV, 7 days on
   desktops) that say "this device's key holds handle X on this network".
3. **A game or server** checks a certificate offline: the issuer certificate
   must be signed by a root key it trusts, and the device certificate by
   that issuer, inside its dates. At join, the device proves it holds its
   key by signing a challenge.

Federation adds one thing: **which networks' root keys to trust.**

- Every game ships with a **list of networks**: each one's domain and root
  keys. The list is a signed document, like the signed table of network versions
  ([versioning.md](versioning.md)), so it can be updated without a new
  release.
- A network asks to be listed by publishing its root keys at a well-known
  address on its domain, for example
  `https://example.org/.well-known/open-network/roots.json`, and asking the list's
  maintainers. Listing means "this network's names are checked", nothing
  more; it gives nobody any access to anyone's servers.
- A game, server or player may also add or remove networks locally (a
  setting), for private or test networks.
- Short-lived certificates mean no network has to run an always-on check
  for others: a removed device simply can't renew. Each network publishes
  its own revocation list (CREV) for the rest of a certificate's life.

## What each network runs

| Piece | What it does | Required? |
| --- | --- | --- |
| Root keys | sign the issuer certificate; kept offline | yes |
| Issuer | signs device certificates and the revocation list | yes |
| Sign-in and profiles | accounts, linking a game copy, handles | yes |
| Game list | its games, in the Delta List format ([list.md](list.md)) | optional |
| Stats | reports and service records ([stats.md](stats.md)) | optional |
| Moderation | bans and reports for its own players and servers | its own choice |

A network can be as small as one site and one signer process.

## Between networks

What works across networks from the start:

- Joining any game, with names checked as above.
- Seeing which network a player belongs to.
- Game lists: a game may read several networks' lists and show them together.

What would come later, network to network, only where both sides agree:

- Friends, invites and parties across networks.
- Shared ban lists: each network decides whose lists it reads, as servers
  already can with shared ban lists ([control.md](control.md)).

Bans, reports and moderation always stay with each network. One network
can't ban another network's player everywhere; a server owner decides who
plays on their server.

## For a port or fork

A port that only wants to play with everyone needs nothing here: the legacy
protocol and Delta Peer already work ([ports.md](ports.md)).

A port or fork that wants its own accounts and verified names:

1. Run an issuer and sign-in (the formats are in [link.md](link.md)).
2. Make root keys, publish them at your domain, and ask to be listed.
3. Your players' names then show and verify in every game that speaks the
   spec, as `handle:yourdomain`.

Today, before federation exists, a port can register an app and sign its
players in with Delta, Chupathingy's network ([clients.md](clients.md)).

## Order of work

1. Client IDs and scopes for apps (in progress), so apps don't share one
   identity.
2. Qualified names (`handle:domain`) in certificates, the game and the site,
   with today's names read as `:chupathingy.org`.
3. The signed list of networks, and a local setting to add or remove one.
4. A reference issuer that any network can run (part of the site's code,
   published once it has had a security review).
5. Later: friends, invites and parties between networks.
