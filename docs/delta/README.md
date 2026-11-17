# Delta

Delta is Chupathingy's network: the part of Halo online that the original
game never had.

## For players

When you play online, your game talks to the host in Halo's own game
protocol, the one every port of the game shares. Delta rides beside it. It
lets games tell each other what platform they're on, keep a game small
enough for an Xbox or an N64, share chat and voice, check that everyone has
the same map, show verified names, and list games on the game list. It also
keeps your stats and your profile on the site, if you want that.

You don't need an account to play. A game without Delta (another port, an
older build) still plays with everyone: it just doesn't get the extras.
Nothing here ever shows your address to anyone.

How to get online: [Playing online](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/online.md).

## For developers

Delta is open. These pages are its specification, and anyone may build a
client, server, port or tool that speaks it. Nothing in it depends on being
Chupathingy, and nothing is secret but signing keys.

Delta has one rule above all: **the player base stays whole.** The legacy
game protocol is the common ground. Delta starts beside it, and when a
capability changes the game itself, every part of Delta still falls back,
per machine, to the plain game protocol for a machine that doesn't share
it. A room-wide capability (one every machine must have) is on only while
every machine in the game has it.

### The parts

Delta is one name for parts that cross different trust boundaries:

| Part | Between | What it is | Page |
| --- | --- | --- | --- |
| **Delta Peer** | machines in one game | Delta's own messages beside the game protocol: the handshake, capabilities, and the extras | [peer.md](peer.md) |
| **Delta List** | a host, and the site | announcing and listing games | [list.md](list.md) |
| **Delta Stats** | a host or player, and the site | end-of-game reports and the event stream | [stats.md](stats.md) |
| **Delta Control** | an admin or moderator, and a server | roles, commands, the control API, in-game moderation | [control.md](control.md) |
| **Delta Link** | a player or server, and the site | profiles, devices, linking with a code, clans | [link.md](link.md) |

The legacy number (the game protocol's network version) is not part of
Delta. Delta works around it: [versioning.md](versioning.md).

```
                   the legacy game protocol
  client  <====================================================>  host
     \\    Delta Peer (UDP 5160, inside the same tunnel)           //
      \\  <==================================================>    //
       \\                                                       //
        \\  Delta List / Stats / Link (HTTPS)       Delta List / Stats / Link
         `------------------->  stats.chupathingy.org  <-------------'
                                      ^
                                      | Delta Link (server dials out)
                                      |
                              admin's browser  --- Delta Control --->  server
```

The game itself stays peer to peer: one machine hosts, the others join it,
over LAN or the internet tunnel. The site is optional for play: invites,
public listings and LAN work without it, and the site is never in the path
of a game.

### The pages

| Page | What it covers |
| --- | --- |
| [peer.md](peer.md) | Delta Peer, byte by byte: transport, handshake, capability registry, platform keys and policy, the wire format with real examples, the advertisement, the join mark, and every feature's messages |
| [versioning.md](versioning.md) | the legacy number, wire IDs and their ranges, the signed table, legacy compatibility, join reasons |
| [registry.md](registry.md) | every assigned number in one place: platforms, capability bits, wire IDs, network versions, ports (and [registry.json](registry.json)) |
| [list.md](list.md) | the game list: a host's fields, pings before joining |
| [stats.md](stats.md) | reports, the event batch, medals, map facts |
| [control.md](control.md) | server roles, accounts, the link to the site, shared ban lists |
| [link.md](link.md) | profiles, device identity, devices, the doorbell, clans |
| [clients.md](clients.md) | registering an app that signs players in on the site |
| [security.md](security.md) | how Delta treats hostile input, and where each part's checks are |
| [ports.md](ports.md) | a port's path to playing with everyone, with the reference client and its tools |
| [federation.md](federation.md) | proposal: other networks running their own accounts and servers, with names checked across all of them |
| [MOVING.md](MOVING.md) | which files make up the spec, and which are the game's |

How Chupathingy's own game and server implement all of this (settings,
menus, files) is on the game side: [delta-implementation.md](https://github.com/ChupathingyCE/chupathingyce/blob/main/docs/delta-implementation.md).

### How it meets everything else

- **Machines without Delta** see only the legacy protocol and number.
  Nothing changes for them.
- **The internet tunnel** carries Delta Peer's socket like the game's own,
  inside the same encryption.
- **A dedicated server** is a host that speaks Delta Peer, announces through
  Delta List, reports through Delta Stats, and is run through Delta Control
  and Delta Link.
- **Consoles** join as Delta Peer clients with their platform key, so hosts
  keep games to their limits unless their player opts in.
- **Halo Custom Edition 1.0.10** players are outside Delta. Nothing of them
  reaches Delta Peer or the legacy number.
- **The site** is the other end of Delta List, Stats and Link.
