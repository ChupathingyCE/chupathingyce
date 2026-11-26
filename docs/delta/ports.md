# Delta for ports

This page is for people porting Halo to something new: a Dreamcast, an N64,
an old PC, a handheld. It's the path from "my port plays the game" to "my
port plays with everyone": Chupathingy on PC, Mac, Linux, Android and the
consoles, and every other build that speaks the game protocol. The facts are
in the spec pages it links to; this page says which ones you need, in what
order, and how to test them. It comes with a small reference client in C
([tools/delta-ref](../../tools/delta-ref)), a script that checks your
packets, and a script that runs our dedicated server on your own machine to
test against.

## The short version

1. Your port already plays the legacy game protocol at some network version
   (call it N). That's what carries the game itself. Delta doesn't replace
   it.
2. Ask us for a platform number ([registry.md](registry.md#how-to-ask-for-a-number)).
   Don't pick one yourself.
3. Open one more UDP socket, the Delta socket. Once you've joined a game
   whose host says it speaks Delta, send HELLO to the host's port 5160 and
   wait up to 4 seconds for WELCOME. No answer means you play plainly, with
   nothing lost. That's [the handshake](peer.md#handshake).
4. Offer one capability, `platform` (bit 0). It tells the host what your
   machine can take, so the host keeps the game to your limits.
5. Read the host's advertisement to know whether to say HELLO
   ([below](#the-advertisement)). If your port hosts games too, see
   [If you host games from your port](#if-you-host-games-from-your-port).
6. Run the tests in [Testing your port](#testing-your-port) until they pass.

Nobody needs an account for any of this. Accounts, profiles, friends,
parties, chat, voice and the rest are extras
([What you can leave out](#what-you-can-leave-out)).

## What plain cross-play needs

Read these in [peer.md](peer.md), in this order:

| What | Where | What you do with it |
| --- | --- | --- |
| The Delta socket | [Transport](peer.md#transport) | UDP, the host's 5160, sent from the address the host knows you by, through your internet tunnel if you have one |
| The header | [Wire format](peer.md#wire-format) | 12 bytes on every datagram; little-endian always |
| HELLO, WELCOME, LEGACY, BYE, ROSTER | [Handshake](peer.md#handshake), [Wire format](peer.md#wire-format), [Examples](peer.md#examples) | send HELLO, read WELCOME (use only `agreed` bits you offered), accept LEGACY of any major, send BYE when you leave, read the ROSTER's room limit if you like |
| Capability bits | [Capability registry](peer.md#capability-registry) | offer bit 0 alone; never 5, 9, 12 or 18; ignore bits you don't know |
| Platform key | [Platform keys](peer.md#platform-keys), [Platform policy](peer.md#platform-policy) | your number, your hosting and join limits, your memory |
| The advertisement | [The advertisement](peer.md#the-advertisement) | read the tag to know whether to say HELLO; if you host, what to write is [below](#if-you-host-games-from-your-port) |
| The join mark | [The join mark](peer.md#the-join-mark) | optional: mark your join so a Delta host waits for your HELLO |
| Version rules | [The legacy number](versioning.md#the-legacy-number), [The signed table](versioning.md#the-signed-table) | which numbers you join and announce |

Ignore every message type you don't know, and every datagram that isn't from
your host's address and port 5160 or that carries another session. Check
every size before you read; read a longer payload for the fields you know.
The reference client does all of this, and its tests feed it hundreds of
thousands of random and cut-short datagrams.

### The advertisement

When you join, read the host's tag (or the old layout) only to decide
whether to say HELLO; `dref_advert_read` does it. A host with a tag of
layout 1 runs Chupathingy's code; one of layout 2 runs another project's
(`project` says which). Say HELLO to both, and join both by your own
number rules: neither tag changes which numbers your port plays. What to
write in your own, if your port hosts, is in
[If you host games from your port](#if-you-host-games-from-your-port).

### The game list

The game list at stats.chupathingy.org is optional for play: system link
and invites work without it.

- **Internet games through OpenCE's public lobby** show up on our list
  already. The site reads OpenCE's brokers, so a port that lists its games
  there is listed with us too, labeled as OpenCE's protocol.
- **Announcing to our list directly** (`POST /v1/announce`, form encoded)
  needs an internet invite our tunnel can reach. If you do it, add the Delta
  fields: `delta` (0, or 1 if your host answers HELLO), `platform_key`
  (16 lowercase hex digits), `arch`, `build` and `capabilities` (8 hex
  digits). `dref_announce_fields` writes exactly what our own builds write.
  The rest of the form is in [Delta List](list.md#delta-list).
- **Reading the list:** `GET /v1/games.txt` is plain text, one game a line,
  tab-separated; new fields only ever go at the end of a line. See
  [The host's fields](list.md#the-hosts-fields).

### Version rules for a port

Each game has one network version: the number its host announces.

- **Your client, joining a host without a Delta tag** (OpenCE's, or another
  port's): join only a host whose number you play. That's OpenCE's rule, an
  exact match, unless your code really plays a range: the facts for that
  are in [Playing a range](versioning.md#playing-a-range).
- **Your client, joining a Delta host:** say your N in HELLO. Our host
  sends each Delta machine the game settings record of the older of its own
  number and the one in that machine's HELLO, and what a newer number adds
  is messages an older machine drops. That's how our own older console
  builds play on newer hosts. To join a Delta host that announces a *newer*
  number than your N, mark your join request (below) so the host waits for
  your HELLO before it sends the record. We haven't tested a port this way
  yet: try it against the loopback host with `--announce` set past your N
  before you rely on it, and when in doubt, join only at your own number.
- **If your port hosts:** announcing your N is what we recommend
  ([If you host games from your port](#if-you-host-games-from-your-port)).
  Our clients join hosts from 11 to 45 today (the range rises with each
  network number we follow).
- **Which number our hosts announce** comes from the signed legacy table
  (`/v1/delta/legacy` on the site, or the `delta-table` branch on GitHub), the
  row for each build's wire: the `announce` number, with `minimum` and
  `maximum` the range its clients join. Today the row for 0.9.1's wire
  (`chupa-44a`) says 32, and 44 for a game on an MCC map; a server's log
  and `--version` say the number it announces.
  You don't need the table to play. If you want it, check its Ed25519
  signature against the keys in [The key](versioning.md#the-key), before you trust a
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
| Accounts and Quick Connect | a player's Delta profile, stats on the site | [clients.md](clients.md) (register a client ID first) |
| Profiles, verified names | names and pictures from the site on the scoreboard | [Verified names](peer.md#verified-names) |
| Friends, parties, presence | invites, joining a friend, party teams | [Party teams](peer.md#party-teams); the site's `/v1/social` |
| Text chat, server messages | chat between Delta players, a server's notices | [Chat](peer.md#chat) |
| Voice | Opus voice relayed by the host | [Voice](peer.md#voice) |
| The signed legacy table | following the network numbers without a release | [The legacy table as config](versioning.md#the-signed-table) |
| Moderation, reports, team changes, Bomb, MCC maps, game modes | each one's feature | its section in [peer.md](peer.md) |

**Nobody needs an account to play together.** Not your players, not ours.
An OpenCE player with no Delta at all plays in our games every day.

## Byte order

Every number in Delta is little-endian. On a big-endian machine (the N64,
the GameCube and Wii, the PowerPC consoles), never copy a struct onto the
wire or a `uint32_t` out of a buffer; build each number from its bytes. The
reference client reads and writes a byte at a time, and CI runs its tests on
a big-endian machine (s390x, under QEMU). The join mark's characters are
16-bit values in your machine name array, so they travel however your game
already sends that name.

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
co-op ([Platform policy](peer.md#platform-policy)). Hosts use the row to
keep the game safe for the weakest machine in it. How to ask for one, and
every number given out so far: [registry.md](registry.md#platforms). A new
port's row starts careful and goes up once you tell us it plays well. Your
key may always claim lower limits than your row; a host takes the lower one.

**Not pinning everyone's floor.** Our builds share one pool with the
consoles through a floor: the oldest wire a console build in the wild plays
([The console floor](versioning.md#the-console-floor), `chupa-32a` today). Every new build must still
play with it, so a console that never updates holds everyone to its level.
A port on this page never touches that floor: it has no wire, and our hosts
go by its network number and its HELLO, game by game. What a slow port does
need is for our hosts to keep serving its number's layouts. Today that is
every number from 11 on, and we never drop one ([Principles](versioning.md#principles)).

**A floor of your own, if you want one.** From 0.9.2 the signed legacy
table can carry a floor for each platform: the oldest network version a
Delta machine of that platform may say in its HELLO and still play with our
hosts ([Platform floors](versioning.md#platform-floors)). It is yours
alone: raising it moves no other port's, and our consoles' moves nothing of
yours. A port has none until it asks for one, and then our hosts play its
machines at that number and newer. A machine of your platform under it is
told it needs a newer build instead of joining a game it might not play
right. That's useful once you ship a fix older builds don't have and you'd
rather those players update. Ask in the issue for your platform number, or
any time after; it changes with a new signed table, not a release of ours.

## If you host games from your port

A wire ID names a revision of Chupathingy's own game code, so a port of
other code has none, and it never writes the tag's layout 1: that would
tell our clients to join you by rules your code doesn't follow. You have
two ways to host.

**Without Delta** (the simple one). Write the network version, the flags
and the kind ([The advertisement](peer.md#the-advertisement)) and leave
bytes 4 to 25 zero (`dref_advert_write`). Our clients join your game by its
number, as they join any host without Delta, and say no HELLO. A port that
hosts plays fine with our clients this way, with no Delta at all.

**A floor row for your platform**, either way. If your older builds
shouldn't play with newer ones, ask for a floor in the signed table
([A floor of your own](#what-we-do-on-our-side-for-a-slow-console)). It
works on the joining side: our hosts turn away your platform's machines
under it, and you can do the same for machines that join yours. It doesn't
change how your advertisement looks.

**With Delta.** Your host answers HELLO, and says so with a tag of layout 2
([The tag](peer.md#the-tag)): your project's number and your project's own
version. It's more work than joining. Here is all of it:

1. **Get a project number** ([registry.md](registry.md#projects)), the
   same way as a platform number. One number for your project, on every
   platform. Never pick one, and never write 1 (Chupathingy's) or another
   project's.
2. **Write the tag** with `dref_advert_write_project`: OpenCE's fields at
   the front as before, bytes 4 to 16 zero, and the tag at 17 to 25 (`'D'`,
   `'L'`, major 1, layout 2, your project, your version in two bytes, a 0,
   the check). Your version is yours to define (a protocol revision, a
   release number); other projects' readers show it and never join by it.
   Write the tag only while your Delta socket is open on port 5160 and you
   answer HELLO, and clear it when you stop. A game whose version word is
   0xFF00 or more is Content Evolved's modded layout, whose bytes are all
   its own: no tag there.
3. **Answer HELLO** ([Handshake](peer.md#handshake)). Take a HELLO only
   from a machine of your game: from that machine's address, with the
   machine index you gave it. Answer WELCOME with your major, the
   capabilities you offer, `agreed` (the bits both of you offer, and only
   those you speak in full: `platform` alone is a fine start), your network
   version, your own machine index (255 for a dedicated server), your build
   text, your platform key and `0xFFFFFFFF` for the legacy table. A HELLO of
   another major gets LEGACY and nothing more. HELLO comes again every
   500 ms until answered: answer each one, and keep one session per machine.
   `dref_welcome_write` writes WELCOME.
4. **Send the roster** ([Wire format](peer.md#wire-format), ROSTER) to
   each Delta machine when the room changes (at most every 250 ms) and
   every 5 seconds, with the room's player limit: the smallest of your own
   hosting limit and every Delta machine's join limit
   ([Platform policy](peer.md#platform-policy)). Take no more players than
   that. `dref_roster_write` writes it. Say BYE to a Delta machine you
   drop, and forget its session when it says BYE or leaves.
5. **Clear the join mark** ([The join mark](peer.md#the-join-mark)). Every
   join request's machine name may have two characters past its end. Clear
   everything past the name's end before you use the name or send it to
   anyone, marked or not, so no other machine ever sees the mark.
6. **Wait for a marked machine's HELLO.** For a marked join, wait up to 4
   seconds for that machine's HELLO before you send it its game settings
   record. An unmarked machine starts at once: never hold an OpenCE player.
7. **Send the record of the older number.** Each Delta machine gets the
   game settings record of the older of two numbers: the one you announce,
   and the one in its HELLO. A machine without Delta, or whose HELLO didn't
   come in time, gets the record of the number you announce. If your code
   has only your own number's record, announce that number: a machine
   that can't read the record then drops out on its own, as the legacy
   protocol does with any record it doesn't know.

What our clients do with your tag: they say HELLO, mark their join, and
join your game by your number, exactly or within the range of numbers they
play (11 to 45 today), as with a host without Delta. They show your
project's name in their system link list. They never read a wire into it, and use
nothing a WELCOME didn't agree to. A build of ours from before the tag's
layout 2 sees no tag: it joins you by your number and says no HELLO, and
plays as before.

**What not to claim**, anywhere:

- no tag of layout 1, no wire ID, and no `chupa-` text in your tag, your
  build text or your listing;
- no project number but your own;
- nothing of the old layout ([peer.md](peer.md#the-old-layout)): byte 3 is
  the game's kind, bytes 4 to 16 stay zero, and the flag 0x04 is set only
  by a dedicated server announcing 39 or later;
- no capability in `agreed` that you don't speak in full, and none of the
  bits a HELLO never carries (5, 9, 12 and 18);
- no legacy table serial but `0xFFFFFFFF` unless your host checks and
  serves signed tables ([The signed table](versioning.md#the-signed-table));
- no tag while your Delta socket is closed, or while you don't answer
  HELLO.

**On our list**, announce `delta` `1` once your host answers HELLO
([Announcing from another client](https://github.com/ChupathingyCE/delta-spec/blob/main/list.md#announcing-from-another-client)).
The list doesn't carry your project yet: our Online Games list shows your
game as a Delta host, and our client learns your project from your
advertisement when it joins.

To test your host: `conformance.py advert` checks its advertisement
(including the tag), `conformance.py check --host` the WELCOME, ROSTER,
LEGACY and BYE it sends, and one of our clients against your host on
loopback the whole thing. Our client's log says "joining a Delta host of
another project" with your project's number and your version.

## Testing your port

The kit is in [tools/delta-ref](../../tools/delta-ref):

| File | What it is |
| --- | --- |
| `delta_ref.h`, `delta_ref.c` | the reference client: C99, no allocation, no threads, no OS calls but the send function you pass in |
| `delta_ref_test.c`, `golden.h` | its tests, against golden packets our own code wrote |
| `conformance.py` | checks your packets, your advertisement and your announcement; or stands in for a host on 127.0.0.1 |
| `loopback_host.sh` | runs our dedicated server on 127.0.0.1 alone, to join from your port |
| `tools/delta_ref_capture/` (the game's repository) | how the golden packets are made from our code, and the test against our real host code |

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
python3 tools/delta-ref/conformance.py advert 2c00010000000000000000000000000000000000000000000000
python3 tools/delta-ref/conformance.py listen --seconds 60
```

`listen` is a stand-in host on 127.0.0.1:5160: it answers HELLO with
WELCOME, sends a roster every 5 seconds, and checks what you send. It plays
no game. With `--project 2` (and `--project-version`, `--announce`) it
stands in for a port host of that project: it prints the advertisement
bytes such a host writes, for your advertisement reader, and its WELCOME
names that project.

If your port hosts with Delta, check what your host writes too: its
advertisement, and the datagrams it sends a client, as hex one a line:

```sh
python3 tools/delta-ref/conformance.py advert 2c00010000000000000000000000000000444c010202030000af
python3 tools/delta-ref/conformance.py check --host my-host-packets.txt
```

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
it fetches no table either. Those settings are what keep it on this
machine. As a second guard, it checks the server's sockets every few seconds
and stops the server if one is off the loopback address. Join it from your port (or an emulator) on the same
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
- [ ] Against `loopback_host.sh`, your port joins, the server's log says
      your machine speaks Delta with your platform, and the game's player
      limit drops to your join limit.

If your port hosts:

- [ ] Your advertisement passes `conformance.py advert`: your N, the flags,
      and no Delta tag, or, if your host answers HELLO, a tag of layout 2
      with your project's number.
- [ ] If your host answers HELLO, what it sends passes
      `conformance.py check --host`, it clears the join mark from every
      name, and one of our clients joins it and its log says your project.
- [ ] An OpenCE client and one of our clients both join a game your port
      hosts at your N.

## License

The reference client and the tools are GPL-3.0-or-later. The protocol itself
is not licensed code: speaking Delta puts no license duty on your port.
