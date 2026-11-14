# Delta for ports

This page is for people porting Halo to something new: a Dreamcast, an N64,
an old PC, a handheld. It says what your port needs so its players can play
with everyone else's: Chupathingy on PC, Mac, Linux, Android and the
consoles, and OpenCE. It comes with a small reference client in C
([tools/delta-ref](../tools/delta-ref)), a script that runs our dedicated
server on your own machine to test against, and a script that checks your
packets.

The full specification is [delta.md](delta.md). This page picks out the
part a port needs and says what you can leave out. Where the two differ,
delta.md wins; tell us and we'll fix this page.

## The short version

1. Your port already plays OpenCE's game protocol at some network version
   (call it N). That's what carries the game itself. Delta doesn't replace
   it.
2. Ask us for a platform number (below). Don't pick one yourself.
3. Open one more UDP socket, the Delta socket. Once you've joined a game
   whose host says it speaks Delta, send HELLO to the host's port 5160 and
   wait up to 4 seconds for WELCOME. No answer means you play plainly, with
   nothing lost. That's the handshake.
4. Offer one capability, `platform` (bit 0). It tells the host what your
   machine can take, so the host keeps the game to your limits.
5. Read the host's advertisement as described below, and when you host,
   write yours without a Delta tag.
6. Run the tests in [Testing your port](#testing-your-port) until they pass.

Nobody needs an account for any of this. Accounts, profiles, friends,
parties, chat, voice and the rest are extras (see
[What you can leave out](#what-you-can-leave-out)).

## What plain cross-play needs

### The game protocol: yours already

Machines in a Halo game talk OpenCE's game protocol, the one OpenCE's ports
speak, at a network version number. Our builds play many versions of it
(11 to 45 today). Your port plays one, its N. Everything on this page sits
beside it and never changes a byte of it.

The kit doesn't cover the game protocol. If your port is built on OpenCE's
code, you have it already.

### The Delta socket

- A host's Delta Peer listens on **UDP 5160**: the game's server port, 5150,
  plus 10. A client sends from any port.
- Bind it the way you bind the game's own sockets: the same address, and
  through your internet tunnel if your port plays over the internet. The
  host only answers a machine whose address is one of its game's.
- Halo is a star, and so is Delta Peer: a client talks to its host, never to
  another client.
- Every datagram is at most 1200 bytes.

### The header

Every Delta datagram starts with 12 bytes, the same in every version:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | magic, `"DP"` (0x44 0x50) |
| 2 | 1 | Delta major: 1 |
| 3 | 1 | message type |
| 4 | 2 | payload length: the datagram's size minus 12, exactly |
| 6 | 2 | reserved: 0 |
| 8 | 4 | session: the client's, random and never 0, picked once per handshake |

Numbers are **little-endian**, always (see [Byte order](#byte-order)).

### The handshake

The order of events, for a port that joins a host:

1. Join the game the usual way (OpenCE's join). If the host's advertisement
   says it speaks Delta ([below](#the-advertisement)), you'll say HELLO.
2. Wait until the host gives your machine its machine index (the pregame).
   Then send **HELLO** to the host's address, port 5160.
3. Send HELLO again every 500 ms until the host answers.
4. **WELCOME** means Delta Peer for the rest of the game. **LEGACY** (the
   host speaks another major) or **no answer within 4 seconds** means you
   play plainly. Your game never waits on any of this: run it from your
   network idle, beside the game.
5. While in the game, the host sends **ROSTER** when its room changes (at
   most 4 times a second) and every 5 seconds. Read its room fields if you
   like, and skip the entries if you don't need them.
6. When you leave, send **BYE**.

Ignore every message type you don't know, and every datagram that isn't
from your host's address and port, or that carries another session. A
host sends other messages from time to time (TABLE_HAVE, for one); you can
ignore them all.

#### HELLO (type 1), client to host

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | capabilities you offer (bits, [below](#capability-bits)) |
| 4 | 2 | the OpenCE network version you play (N) |
| 6 | 1 | your machine index, as the host gave it |
| 7 | 1 | your build text's length, at most 31 |
| 8 | 4 | your legacy table's serial: send `0xFFFFFFFF` ("I take no tables") |
| 12 | 8 | your platform key ([below](#platform-key)) |
| 20 | length | your build text: printable ASCII, like `"n64-port 0.3"` |
| then | 4 | the game modes you play: send 0 |

A HELLO for an N64 port with machine index 2, playing version 44, is
48 bytes in all:

```
44 50 01 01 24 00 00 00 cd ab 34 12   header: HELLO, 36 bytes, session 0x1234abcd
01 00 00 00                           capabilities: platform
2c 00                                 network version 44
02                                    machine index 2
0c                                    build text: 12 bytes
ff ff ff ff                           takes no tables
11 01 00 00 08 10 03 00               platform key: n64, joins 8 (16 opted in), 8 MB
6e 36 34 2d 70 6f 72 74 20 30 2e 31   "n64-port 0.1"
00 00 00 00                           game modes: none
```

(These bytes are one of the golden packets in
[tools/delta-ref/golden.h](../tools/delta-ref/golden.h), written by our own
code.)

#### WELCOME (type 2), host to client

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | the host's capabilities |
| 4 | 4 | agreed: the bits you both use |
| 8 | 2 | the network version the host plays |
| 10 | 1 | the host's own machine index (255: a dedicated server, which has none) |
| 11 | 1 | the host's build text length |
| 12 | 4 | the host's legacy table serial |
| 16 | 8 | the host's platform key |
| 24 | length | the host's build text |
| then | 4 | the host's game modes (a host before them sends nothing here: read 0) |

Use only the bits in `agreed` that you offered. Never more.

#### LEGACY (type 3) and BYE (type 5)

Header only, no payload. A LEGACY's header carries the host's own major,
which may not be 1: accept a LEGACY of any major.

#### ROSTER (type 4), host to client

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | room capabilities: what every machine of the game has now |
| 4 | 1 | the room's player limit (the smallest of every machine's) |
| 5 | 3 | reserved |
| 8 | 1 | entry count, at most 32 |
| 9 | 3 | reserved |
| 12 | 36 each | entries: machine index, flags, reserved (2), agreed capabilities (4), platform key (8), profile revision (4), player ID (16) |

A roster with more than 32 machines comes in several datagrams.

#### Reading messages safely

Check every size before you read. Read a payload longer than the fields
you know for the fields you know: a later version may add fields at the end.
Replace text that isn't printable ASCII. The reference client does all of
this, and its tests feed it hundreds of thousands of random and cut-short
datagrams.

### Capability bits

One bit per optional feature, never reused
([the full registry](delta.md#capability-registry-first-draft)). For plain
cross-play, offer **bit 0, `platform`, alone**. Each other bit you set
promises the whole feature: its messages, its rate limits, its rules.

| Bit | Name | For a port |
| --- | --- | --- |
| 0 | `platform` | **offer it**: your platform key reaches the host and the scoreboards |
| 1 to 4, 6 to 8, 10, 11, 13 to 17, 19 to 25 | profiles, chat, Custom Edition maps, moderation, Bomb, team changes, verified names, voice, MCC maps, game modes and more | optional, each with its section in delta.md |
| 5, 9 | retired | never set |
| 12, 18 | the game list's alone (`ping`, `host_cheats`) | never in a HELLO |

Ignore bits you don't know. A host that sees a bit you don't offer simply
doesn't use it with you.

### Platform key

Eight bytes, in HELLO, WELCOME and the roster:

| Byte | Field |
| --- | --- |
| 0 | platform number |
| 1 | version: 1 |
| 2 | flags: 0x01 its player turned the caveats off; 0x02 a dedicated server |
| 3 | the most players a game it hosts can have (0: never hosts) |
| 4 | the most players of a game it joins |
| 5 | the same, with its player's opt-in |
| 6 | memory: log2 of its megabytes (3 is 8 MB, 4 is 16 MB, 6 is 64 MB); 0 unknown |
| 7 | reserved: 0 |

A host takes no more players than the smallest join limit of its Delta
machines. That's how a slow console stays safe in a game hosted on a PC,
and it's the main reason to do the handshake at all. A host never lies about
its game's size: it only takes fewer players. Our tests check this with our
real host code: an N64 port with a join limit of 8 joins a host and the
host's room drops to 8 players.

### The advertisement

A host's game advertisement has 26 (0x1A) reserved bytes. OpenCE fills them
from the front, Delta from the back:

| Bytes | Meaning |
| --- | --- |
| 0, 1 | the OpenCE network version the host announces (little-endian) |
| 2 | flags: 0x01 the distributed netcode (always set), 0x02 the game is under way, 0x04 a dedicated server (OpenCE 39 and later) |
| 3 | the game's kind (OpenCE 37 and later; 0 none) |
| 4 to 16 | zero (old Delta hosts wrote here; see below) |
| 17 to 25 | the Delta tag, when the host speaks Delta |

The Delta tag:

| Byte | Meaning |
| --- | --- |
| 17, 18 | `'D'`, `'L'` |
| 19 | the host's Delta major |
| 20 | the tag's layout version, 1 |
| 21, 22 | the host's wire: its number and its letter (`chupa-44b` is 44, `'b'`) |
| 23, 24 | its oldest compatible wire, the same way |
| 25 | check: 0xA5 xor the eight bytes before |

**Reading (joining).** A host speaks Delta when its tag is valid (both tag
bytes, layout 1, the check, letters `a` to `z`, the oldest wire no newer
than its own). Hosts older than the tag (before `chupa-39a`) set the flag
0x04 instead, with their major at byte 3 and a wire text like `chupa-32a`
from byte 4. From OpenCE's version 39, 0x04 means a dedicated server, so a
host announcing 39 or later counts as Delta by that flag only with a wire
text after it. `dref_advert_read` does all of this.

**Writing (hosting).** Write OpenCE's fields and leave bytes 4 to 25 zero.
A wire ID names a revision of *our* game code, so a port has none, and a
tag would tell our clients to join you by rules your code doesn't follow.
Our clients join your game by its number instead (below). Answering HELLO as
a host is not in this kit yet. See [Hosting with Delta](#hosting-with-delta).

### The game list (Delta List)

The game list at stats.chupathingy.org is optional for play: system link
and invites work without it.

- **Internet games through OpenCE's public lobby** show up on our list
  already. The site reads OpenCE's brokers, so a port that lists its games
  there is listed with us too, labeled as OpenCE's protocol.
- **Announcing to our list directly** (`POST /v1/announce`, form encoded)
  needs an internet invite our tunnel can reach. If you do it, add the Delta
  fields: `delta` (0, since you don't answer HELLO as a host), `platform_key`
  (16 lowercase hex digits), `arch`, `build` and `capabilities` (8 hex
  digits). `dref_announce_fields` writes exactly what our own builds write.
  The rest of the form is in delta.md, "Delta List".
- **Reading the list:** `GET /v1/games.txt` is plain text, one game a line,
  tab-separated; new fields only ever go at the end of a line. See delta.md,
  "The host's fields".

### Version rules

Each game has one OpenCE network version: the number its host announces.

- **Your client, joining a host without a Delta tag** (OpenCE's, or another
  port's): join only a host whose number you play. That's OpenCE's rule, an
  exact match, unless your code really plays a range.
- **Your client, joining a Delta host:** say your N in HELLO. Our host
  sends each Delta machine the game settings record of the older of its own
  number and the one in that machine's HELLO, and what a newer number adds
  is messages an older machine drops. That's how our own older console
  builds play on newer hosts. To join a Delta host that announces a *newer*
  number than your N, mark your join request (below) so the host waits for
  your HELLO before it sends the record. We haven't tested a port this way
  yet: try it against the loopback host with `--announce` set past your N
  before you rely on it, and when in doubt, join only at your own number.
- **Your host:** announce your N. Our clients join hosts from 11 to 45
  today (the range rises with each OpenCE number we follow).
- **Which number our hosts announce** comes from the signed legacy table
  (`/v1/delta/legacy` on the site, or the `delta-table` branch on GitHub), the
  row for each build's wire: the `announce` number, with `minimum` and
  `maximum` the range its clients join. Today the row for 0.9.1's wire
  (`chupa-44a`) says 32, and 44 for a game on an MCC map; a server's log
  and `--version` say the number it announces.
  You don't need the table to play. If you want it, check its Ed25519
  signature against the keys in delta.md, "The key", before you trust a
  byte of it.

**The join mark.** A client that will say HELLO puts two characters past the
end of its machine name in the join request. Of the name's 32 16-bit
characters, the name takes at most 28 and ends with a 0; characters 29 and
30 (counting from 0) are 0x0394 and 0x0001, and everything else past the
end is 0. A longer name goes unmarked. A Delta host clears it and waits up to 4 seconds for that
machine's HELLO before deciding which settings record it gets. An OpenCE
host never looks. `dref_join_mark` writes it.

## What you can leave out

Everything below is optional. A port that does the handshake and nothing
else plays with everyone, and none of these is needed for that.

| Extra | What it adds | Where |
| --- | --- | --- |
| Accounts and Quick Connect | a player's Delta profile, stats on the site | [delta-clients.md](delta-clients.md) (register a client ID first) |
| Profiles, verified names | names and pictures from the site on the scoreboard | delta.md, "Verified names" |
| Friends, parties, presence | invites, joining a friend, party teams | delta.md, "Party teams"; the site's `/v1/social` |
| Text chat, server messages | chat between Delta players, a server's notices | delta.md, "Chat" |
| Voice | Opus voice relayed by the host | delta.md, "Voice" |
| The signed legacy table | following OpenCE's numbers without a release | delta.md, "The legacy table as config" |
| Moderation, reports, team changes, Bomb, MCC maps, game modes | each one's feature | its section in delta.md |

**Nobody needs an account to play together.** Not your players, not ours.
An OpenCE player with no Delta at all plays in our games every day.

## Byte order

Every number in Delta is little-endian: low byte first. On a big-endian
machine (the N64, the GameCube and Wii, the PowerPC consoles, the PS3),
never copy a struct onto the wire or a `uint32_t` out of a buffer. Build
each number from its bytes:

```c
uint32_t value = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
```

The reference client reads and writes a byte at a time and never relies on
struct layout. CI runs its tests on a big-endian machine (s390x, under
QEMU) beside our own Delta code's, so both sides are checked for byte
order. The join mark's characters are 16-bit values in your machine name
array, so they travel however your game already sends that name.

## Slow links

A modem of about 3 KB/s (24 kbps) is the hard case. Here is what goes over
the link today, what you can't drop, and what you can.

**What we send today.** Measured on 2026-11-14 with a development build of
0.9.2 (wire `chupa-44b`): our dedicated server on loopback, Blood Gulch
Slayer, our own Linux clients joined by system link, bytes counted at each
client's sockets over a minute of play. The game runs 30 ticks a second.
Bytes are UDP payload; add 28 bytes of IP and UDP header for every datagram
on a real link.

| | Client to host | Host to client |
| --- | --- | --- |
| Game protocol, 1 player standing | 3,720 B/s, 40 datagrams/s (124 B a tick) | 7,735 B/s, 31 datagrams/s (258 B a tick) |
| Game protocol, 4 players moving and shooting (each client) | 3,720 to 3,850 B/s, 40 datagrams/s (about 125 B a tick) | 10,200 to 12,300 B/s, 31 datagrams/s (340 to 410 B a tick) |
| Delta Peer, our client with all its capabilities | 0 B/s in play | 163 B/s, 3.3 datagrams/s |
| Delta Peer, a port with `platform` alone | 0 B/s in play | a ROSTER every 5 s: 24 + 36 B per machine (about 40 B/s in a 5-machine game) |
| The handshake, once a game | HELLO: 32 B plus your build text, every 500 ms until answered (one or two, usually) | WELCOME: about 50 B, once |

With headers, one player's game stream is about 4.8 KB/s up and 8.6 KB/s
down, and the host's side grows with the players near you. **A 3 KB/s link
can't carry today's game stream in either direction, even alone.** That's
our game protocol's cost, not Delta's: Delta Peer adds well under 1% of it
for a port.

**Required** for plain cross-play:

- the game protocol at your N (all of it: it's the game);
- the handshake, once (about 100 to 400 bytes all told);
- the host's ROSTER, every 5 seconds (you can ignore what's in it, but it
  still arrives).

**Optional**, and what to do on a slow link:

- **The signed legacy table:** say `0xFFFFFFFF` in HELLO and no host sends
  you its pieces (up to 16.5 KB each time).
- **Chat, voice, profiles, verified names, object sync, every other
  capability:** don't offer them and no host sends you their messages.
  Voice alone is about 5 KB/s for each talker you hear.

The host already saves what it can: it sends you far-off players less often
(every second to sixth tick, by distance and sight), which is why a game
with players moving around you costs more than one where they stand still.

What would make a modem playable is a low-bandwidth mode in the game
protocol itself: a byte budget a client asks for, with state sent less
often. We don't have one yet. If we build it, it'll be a Delta capability a
client offers in HELLO, opt-in per client, with no new network version, so
a modem player's game changes nothing for anyone else's. Until then, a slow
port plays best on a LAN or a broadband link, or in small games.

## What we do on our side for a slow console

Two things for a slow port are ours to do, not yours:

**A platform number.** Every platform has one number, never reused, and a
policy row: the players it hosts and joins, its memory, whether it plays
co-op. Hosts use the row to keep the game safe for the weakest machine in
it. To get one, open an issue on
[ChupathingyCE/chupathingyce](https://github.com/ChupathingyCE/chupathingyce/issues)
(or ask on our Discord) with:

- the platform and your port's name and link;
- roughly how much memory the game gets there;
- how many players it handles, joining and hosting (0 if it never hosts);
- whether it plays network co-op.

We add the number to `delta.h`, delta.md and the site, and tell you which
it is. A new port's row starts careful (a few players, no co-op) and goes up
once you tell us it plays well. Your key may always claim lower limits than
your row; a host takes the lower one. Until you have a number, send 0
(unknown): you still play, with no logo and the unknown row. Numbers in use
today include 17 for the N64 and 18 for DOS and Windows 9x PCs.

**Not pinning everyone's floor.** Our builds share one pool with the
consoles through a floor: the oldest wire a console build in the wild plays
(`DELTA_WIRE_CONSOLE_FLOOR`, `chupa-32a` today). Every new build must still
play with it, so a console that never updates holds everyone to its level.
A port on this page never touches the floor: it has no wire, and our hosts
go by its OpenCE number and its HELLO, game by game. What a slow port does
need is for our hosts to keep serving its number's layouts. Today that is
every number from 11 on, and we never drop one ([delta-compatibility.md](delta-compatibility.md),
"Principles").

Planned, not built: a row of its own for each port in the signed legacy
table, saying the oldest network version our hosts must keep serving for
that platform and the limits its policy row starts from. That way a port's
needs can change without a release of ours and without moving the console
floor for everyone else. Until then, tell us your N when you ask for your
platform number, and we track it by hand.

## Hosting with Delta

A port that hosts plays fine with our clients with no Delta at all: they
join it by its number, as they join OpenCE's hosts. Answering HELLO as a
host (WELCOME, the roster, keeping the room to every machine's limit) is
more work, and its advertisement needs an answer we don't have yet: a tag
carries a wire ID, and wire IDs name revisions of our code. If your port
wants to host with Delta, open an issue and we'll work out the wire rule for
ports together. The reference client already writes WELCOME, so the bytes
are covered.

## Testing your port

The kit is in [tools/delta-ref](../tools/delta-ref):

| File | What it is |
| --- | --- |
| `delta_ref.h`, `delta_ref.c` | the reference client: C99, no allocation, no threads, no OS calls but the send function you pass in |
| `delta_ref_test.c`, `golden.h` | its tests, against golden packets our own code wrote |
| `conformance.py` | checks your packets, your advertisement and your announcement; or stands in for a host on 127.0.0.1 |
| `loopback_host.sh` | runs our dedicated server on 127.0.0.1 alone, to join from your port |
| `capture/` | how the golden packets are made, and the test against our real host code (run from our repository) |

**Build and test the reference client** (any C99 compiler; one command):

```sh
make -C tools/delta-ref
```

To use it, compile `delta_ref.c` into your port. You give it the time in
milliseconds, a random session number, and a function that sends a
datagram to the host's Delta port; you hand it every datagram that comes
from there:

```c
static void send_to_host(void *user, const uint8_t *data, int size)
{
	my_udp_send(user, host_address, DREF_HOST_PORT, data, size);
}

struct dref_client delta;
struct dref_hello mine = {0};

mine.capabilities = 1u << DREF_CAP_PLATFORM;
mine.legacy_version = MY_NETWORK_VERSION;
mine.machine_index = my_machine_index;
mine.table_serial = DREF_NO_TABLES;
mine.key = (struct dref_platform_key){ DREF_PLATFORM_N64, 1, 0, 0, 8, 16, 3 };
strcpy(mine.build, "n64-port 0.3");

/* once joined, if dref_advert_read said the host speaks Delta */
dref_client_start(&delta, &mine, my_random_nonzero(), now_ms(), send_to_host, socket);
/* every frame */
dref_client_tick(&delta, now_ms());
/* each datagram from the host's address and port 5160 */
dref_client_receive(&delta, now_ms(), data, size);
/* leaving */
dref_client_stop(&delta);
```

`delta.state` is `DREF_CLIENT_DELTA` once welcomed, `DREF_CLIENT_LEGACY` when
playing plainly; `delta.room_players` is the room's limit from the roster.

**Check your packets.** Log the datagrams your port sends to port 5160 as
hex, one a line, and run:

```sh
python3 tools/delta-ref/conformance.py check my-port-packets.txt
python3 tools/delta-ref/conformance.py advert 2c0001000000000000000000000000000000000000000000000000
python3 tools/delta-ref/conformance.py listen --seconds 60
```

`listen` is a stand-in host on 127.0.0.1:5160: it answers HELLO with
WELCOME, sends a roster every 5 seconds, and checks what you send. It plays
no game.

**Play against the real server.** On a Linux, macOS or Windows (WSL)
machine with the server program and your maps:

```sh
tools/delta-ref/loopback_host.sh --server path/to/chupathingyce-server --maps path/to/data \
    --map bloodgulch --gametype slayer --announce 44 --broadcast 127.0.0.2
```

`--broadcast` names the loopback addresses your port's client searches from
(here a client bound to 127.0.0.2; Linux answers on all of 127.0.0.0/8, macOS
needs `sudo ifconfig lo0 alias 127.0.0.2 up` first).

It hosts on 127.0.0.1 alone: system link bound to and announced on
127.0.0.1, internet play, UPnP, the public lobby, the game list, stats,
GameSpy, the site link and map downloads off, and a local legacy table, so
it fetches no table either. After it starts, and every few seconds while it
runs, it checks every socket the server holds is on 127.0.0.1, and stops the
server if one isn't. Join it from your port (or an emulator) on the same
machine, then read its log: it says when your machine speaks Delta, and the
room's player limit your key set. The server writes a `config.toml` beside
its program the first time if there is none; its settings are only
defaults, and the script's settings win over it.

## Your build passes if

- [ ] `make -C tools/delta-ref` passes on your machine (or your port's own
      build of `delta_ref.c` passes `delta_ref_test.c`), big-endian ports
      included.
- [ ] Your HELLO passes `conformance.py check`: magic, major 1, exact
      length, a nonzero session, `platform` offered, no retired or list-only
      bits, your platform number (or 0 until we give you one), network
      version N, a build text, and `0xFFFFFFFF` for tables.
- [ ] You say HELLO only to a host whose advertisement says Delta, only once
      you have a machine index, every 500 ms, and give up after 4 seconds.
- [ ] Your game never waits on the handshake: a host that never answers (an
      OpenCE host, a firewall) costs nothing.
- [ ] You take only WELCOME, LEGACY, BYE and ROSTER from your host's address
      and port with your session, and ignore every other type.
- [ ] You say BYE when you leave.
- [ ] Your advertisement passes `conformance.py advert`: your N, the flags,
      and no Delta tag.
- [ ] Against `loopback_host.sh`, your port joins, the server's log says
      your machine speaks Delta with your platform, and the game's player
      limit drops to your join limit.
- [ ] An OpenCE client and one of our clients both join a game your port
      hosts at your N.

## License

The kit is part of this repository and is GPL-3.0-or-later, like the rest of
it ([license.md](license.md)). The protocol itself is not licensed code:
speaking Delta puts no license duty on your port.
