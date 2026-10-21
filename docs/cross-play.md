# Cross-play: the console and the PC builds on one network

Warthog is to play system link with the PC builds (network version 11 when
this was written; 28 since the October 13 merge, with the same layout) on
one LAN, then internet games. The PCs' protocol is fixed: the console
matches them, never the other way round. This is what stands between them,
field by field, and the plan. (File and line references are this
repository's, October 4.)

## What already matches

- **Versions.** `HALO_PORT_NETWORK_GAME_MESSAGE_VERSION` (2) and
  `HALO_PORT_NETWORK_VERSION` (11, minimum and maximum 11) are the same in
  both builds. So the console finds the PCs' games and tries to join them,
  and they find the console's. Version gating doesn't keep them apart: the
  layout checks below do.
- **Message and packet limits.** `HALO_PORT_MAXIMUM_NETWORK_MESSAGE_SIZE`
  (0x1000), `HALO_PORT_NETWORK_PACKET_SIZE` (0x1100),
  `HALO_PORT_NETWORK_GAME_SETTINGS_FRAGMENT_SIZE` (0xE00), the datagram
  size (1,200) and the message header's 0xFFF are outside
  `HALO_XBOX_CONSOLE`, so every buffer they size is the same.
- **Array fields.** The packet codec writes an array's count in one byte
  for any maximum up to 255 (`data_encoding.c:158`), so 16 and 128 give
  the same bytes; only the accepted range differs.
- **The per-tick update** (`message_server_game_update`, sized by the
  player limit) is sent with a count of 0 by the distributed netcode's
  hosts (`network_server_manager.c:2342-2375`): the same bytes in both.
- **Join and lobby messages** (join request, add/remove/switch player,
  settings request) and **the advertisement** (0x114 bytes) are fixed
  size. Only the indices in them depend on the limits.
- **Objects' names on the wire.** An object goes as its full 32-bit datum
  index (salt and slot), never bit-packed by the array's size
  (`network_objects.c:246`, `network_distributed.c:290-292`,
  `network_damage.c:235-278`). The layout is the same at 2,048 and 8,192;
  only the values differ.

## What doesn't

### 1. The game settings record (`struct network_game`)

One declaration (`network_game_manager.h:45-68`), checked against
`halo_port_limits.h` in seven units. Its machine and player arrays are
**slot-indexed**: `machines[i]` is the machine in connection slot `i`
(`machine_index` `i`, else NONE), `players[i]` the player in slot `i`.

| | console (17 machines, 16 players) | PCs (128/128) |
|---|---|---|
| `player_count` | 0x598 | 0x2314 |
| `players` | 0x59A | 0x2316 |
| `random_seed` | 0x79C | 0x3318 |
| PC options | 0x7A4 | 0x3320 |
| size | 0x7C4 (1,988) | 0x3340 (13,120) |

It goes in 0xE00-byte pieces (`message_server_game_settings_update`):
four from a PC, one from the console. A client refuses any piece whose
`total_size` isn't its own `sizeof` ("a different game layout",
`network_client_message_handler.c:925-967`), so each side refuses the
other's game cleanly.

### 2. Player and machine slots

- **Machines.** A host numbers a machine by its connection slot
  (`network_server_manager.c:4029`), and pending connections, game list
  probes and its own loopback take slots too, so a PC host's fifth machine
  can be slot 16 or higher. A console refuses that cleanly ("bad
  machine_index", `network_client_manager.c:1821-1863`).
- **Players.** A player's slot (`player_list_index`) **is its player datum's
  index on every machine** (`network_game_manager.c:110-122`). The
  distributed netcode names players by it, one byte each
  (`distributed_player_to_byte`, `network_distributed.c:1075`). A PC host
  gives the first free slot not held by a player who quit mid-game
  (`:362-397`), so a game of 16 or fewer can still use slots past 16. The
  console drops any such byte (bounds checks at `network_distributed.c:1069`,
  `:1691`, `:2251`, `network_damage.c:1644`, `player_queues_new.c:1170`) and
  refuses the player (`network_game_add_player`): that player silently
  doesn't exist there.
- **Teams.** In free for all a player's team is his slot
  (`game_allegiance.c:205`), and the score arrays are indexed by it.

### 3. The game state message

`_distributed_message_game_state` (`network_distributed.c:3027-3060`,
`game_engine.c:9104-9160`) copies each game type's globals, which hold
arrays of one entry a player slot, and each reader wants its exact size:

| game type | console | PCs |
|---|---|---|
| Slayer | 176 | 1,408 |
| King | 428 | 1,436 |
| Oddball | 212 | 1,108 |
| Race | 352 | 2,704 |
| CTF | 0x38 | 0x38 |

So the two never take each other's scores or the end of a game.

### 4. Object slots

Allocation is kept in step by forcing the host's index: the host takes the
first free slot from 0 (`data.c:296-325`); a client makes the object at
exactly that datum, salt included (`network_objects.c:1880-1955`,
`objects.c:3288`, `data.c:264-293`), and looks remote indices up directly
(`objects_client_has[absolute] == object_index`, `:579`, `:1975`). A
client's own objects (projectiles, effects) take the upper half
(`LOCAL_OBJECTS_FIRST_INDEX`, `:130`: 1,024 on the console, 4,096 on the
PCs) and **never go on the wire**: clients only ever name the host's
objects (hit reports, vehicle predictions, units in player predictions).

On a console, a PC host object at 2,048 or above is refused
(`network_objects.c:1888`), counted as a failure, and the console asks for
the whole object list again every 11 to 60 seconds; the object, and
anything naming it, never exists there (an invisible player, missing
weapons). One at 1,024 or above evicts the console's own projectiles and
effects. A PC host with 16 players or fewer on the Xbox maps allocates low
and mostly stays under 2,048, but nothing guarantees it; above 16 players
its garbage limits scale up (`objects.c:3843-3849`).

Map placement must also come out the same everywhere (placed objects sit
at the same datum on every machine, `network_objects.c:1903-1915`): the
console's object pool (1 MB against the PCs' 8 MB) must not run out during
placement. The Xbox maps fit, as they shipped.

## The two ways, and the choice

**(a) The console runs with the PCs' limits.** 128 player and machine
slots and 8,192 object slots, with 16 kept as the game's own player
maximum. Everything above then matches byte for byte with no translation.
The cost is the game state, which is full (3,165,260 of its 3,166,208
bytes) and can't grow in place: the tag cache, to which the maps are
linked, starts where it ends.

- 128 player slots: the players table is game state, 128 × 0xD4 bytes,
  about 24 KB more. The rest that scales with players is outside the game
  state: the settings record copies (about 11 KB more each), the game types'
  globals, the netcode's arrays.
- 8,192 object slots: the object header array, its pool and the cluster
  references grow by more than the console has. vsod99's memory probe
  found the route on a 128 MB console (a devkit, or a modified retail
  one): the game state as ordinary virtual memory at 0x40000000, with the
  tag, texture and sound caches kept in the low 64 MB.

**(b) The console speaks the PCs' wire format with its own limits.** A
translation layer, valid only where no peer computes anything different.

- The settings record: folding to 16 slots and widening to 128 is exact
  while every used slot is under 16, a byte shuffle (built, below).
- The game state message: re-laid per game type, exact under the same
  condition.
- Objects: the netcode is host authoritative, and no peer computes
  anything from another machine's local index. So a console can keep a
  remote-to-local table (8,192 entries, 32 KB; local-to-remote, 8 KB) and
  translate at the decode and encode points: object creation and deletion,
  object states, inventories, unit states (unit and vehicle), damage
  events and hit reports, vehicle and player predictions, and the CTF and
  oddball state. The PCs see the same bytes. What it doesn't change is
  capacity: 2,048 objects and the 1 MB pool.
- Player slots: the slot is the player datum's index on every machine and
  is in nearly every distributed message, so a translation means rewriting
  every variable-length entry both ways, and FFA teams and score arrays
  with it. It breaks the "never change what peers compute" rule the moment
  a mapping is missed.

**The choice:**

1. **Players and machines: (a).** The console's index space becomes 128 and
   128, its game's maximum stays 16. That's about 24 KB of game state, and
   it removes every translation for the settings record, the game state
   message and every player byte. The 24 KB comes from the 128 MB layout
   first (devkits have 128 MB). For a stock 64 MB console, the
   room has to be found in the game state's own pools by measurement, not
   by guessing. Until then, (b)'s settings fold lets a console join a PC
   game whose used slots are all under 16.
2. **Objects: (b).** The translation table, console only, outside the game
   state (40 KB of ordinary memory). It keeps the PCs' bytes and the
   console's Xbox object capacity. A console in a game means 16 players at
   most. The console must not evict its own objects for a forced create;
   it collects them first, then refuses.
3. **Hosting.** A console host widens its record to the PCs' 13,120 bytes
   and sends it in their four pieces, and allocates host objects below
   2,048 (its own limit), which every PC can take.

## What is built

### The console as a PC host's client (October 4)

The first goal is narrower than the plan above: the console joins a
PC dedicated server's game (network version 11, the PCs' protocol
unchanged) and plays it. That needs neither the bigger game state nor the
object table, only folding what a PC host sends into the console's slots
and leaving cleanly what doesn't fold:

- **Limits.** `halo_port_limits.h` names the network's slots
  (`HALO_PORT_WIRE_NETWORK_PLAYERS`/`_MACHINES`, 128: the PCs', fixed) apart
  from the build's own (16 on the console). `HALO_PORT_CONSOLE_LIMITS` is set
  for the console, and for a desktop build made with `configure.py
  --console-limits` (`HALO_CONSOLE_LIMITS`: the console's players, machines,
  objects and effects on the desktop's game state), a test build only.
- **The settings record.** `port/linux/game/network_game_layout.c` (moved
  from `port/xbox/src`, built into every build, called only with the
  console's limits). The client takes the PCs' 13,120-byte record in its
  four pieces and folds it into its own
  (`network_client_message_handler.c`). A record with a machine or player
  in a slot past 16, or an entry naming one, is not taken: the console
  leaves the game as a full one (`_rejection_code_game_is_full`, "the game
  is full" on the main menu) and says why in `debug.txt` ("cross-play:
  leaving the host's game: ..."). A host's maximum above 16 is held to 16.
- **Joins and players in progress.** A machine index past 16 from the host
  (`message_server_machine_accepted`), or a player added in progress in a
  slot or from a machine past 16 (`message_server_add_player_ingame`),
  leaves the game the same way; before, the first waited in "joining" for
  nothing and the second made an invisible player.
- **Game type states.** Slayer's (the [D] servers' only game type: 1,408
  bytes from a PC, 176 on the console) is folded field by field
  (`network_game_layout_fold_fields`); CTF's is the same size on both.
  Oddball's, King's and Race's are not folded yet: their scores stay the
  console's own, told once in `debug.txt`.
- **Objects: measured, not translated.** The client logs how high the
  host's object indices go (each time they pass another 256), and each
  game's highest index, the creates that landed in the console's own half
  (1,024 and up: they take the place of its projectiles and effects) and
  past its 2,048 (not made). In the host-side runs below, two players on
  Blood Gulch never went past index 102. The table of plan (b) waits for a
  measurement on the console that needs it.
- **Joining by invite (internet).** The console joins a game as the PCs
  do, by its invite (`halo://join/` and 64 hex digits): ONLINE GAMES' A on
  a listed game (the console list's first field is the invite), or Y for
  the invite in `console.join` (`D:\config.toml`). The tunnel is the desktop builds' own
  `port/linux/src/p2p.c`, `p2p_signal.c` and `p2p_crypto.c` (with KCP and
  Monocypher), compiled for the console as they are (the Xbox's build,
  warthog-xbox's `port/xbox/build.py`, copies them into the build so their includes find `port/xbox/p2p`'s
  headers): MQTT to the public brokers over XNet's TCP, STUN and hole
  punching on one UDP socket, X25519 and ChaCha20-Poly1305. Its platform
  layer is `port/xbox/src/xbox_p2p.c` (sockets over XNet's Winsock,
  `XNetDnsLookup`, `XNetRandom`, threads, the settings' defaults), and
  `port/xbox/src/xbox_winsock_hooks.c` does what the desktop's `xnet.c`
  does: the game's datagrams to a peer's virtual address (100.64/10, which
  `XNetXnAddrToInAddr` gives for its XNADDR) go onto the tunnel, its
  connections go to the tunnel's stand-ins on 127.0.0.1, its broadcasts
  (the System Link search) go to every peer too, and the tunnel learns the
  game's ports. The console only joins: no UPnP (the [D] servers forward
  their own), no Discord, no public listing (`p2p_lobby.c`), no invite
  hand-off; `P2P_JOINER_ONLY` gives p2p.c room for one host's stand-ins
  and streams (about 160 KB, not a 128-machine host's 2.7 MB). Internet
  play is on only with `console.bypass_security`. The tunnel's thread logs
  to the debug monitor at once and to `debug.txt` through the main loop
  (`xbox_log_flush`): "Internet play: ..." lines, addresses only as
  tags.
- **Joining on a LAN.** System Link finds a PC host on the console's LAN by its
  broadcast advertisement, as it finds a console's. A host the broadcasts
  don't reach is joined by address: `console.join`, an IPv4 address; the console tries it once each time System Link opens
  (`port/xbox/src/xbox_direct_join.c`), with the address never logged.

Tested on the host: `port/xbox/tests/run.sh` (the record's fold, round trip
and refusals; the game type states' fold, Slayer's sizes, the entries
dropped; `console.join`'s parse, every refusal, every cut short). And two
copies on one Mac, on loopback addresses of their own: ChupathingyCE main's
macOS build as a dedicated server and as a network-test host (`HALO_NETWORK_TEST=
host:bloodgulch:slayer,slayer` with kills, shots and a vehicle), a
`--console-limits` build of this branch joining with `HALO_NETWORK_TEST=join`.
It joined, played, took the host's kills and scores (4 of 4, then 3 and 3
over two games) and each game's end, with no failed creates. And by invite:
main's build as an online dedicated server (not listed), the
`--console-limits` build joining with its `halo://join/` link over the
public brokers, the tunnel and the folded settings record together.

The tunnel's cost on the console's 733 MHz Pentium III, estimated from the
host (an Apple M4: ChaCha20-Poly1305 at 2.4 ns, about 10 cycles, a byte;
X25519 in 0.36 ms) with three times the cycles for 32-bit code: about 30
cycles a byte, 50 microseconds a full 1,200-byte packet, so a client's
traffic (well under 100 KB a second both ways) takes under 0.5% of the
CPU; X25519 and the run's Ed25519 key, once each, tens of milliseconds, on
the tunnel's thread. To be measured on the console.

### The settings record (first pass)

- `port/linux/game/network_game_layout.c`: the settings record between
  any two slot counts, byte for byte. A fold refuses any used machine or
  player slot, or index a used entry names, past the narrower layout (no
  guess), and holds the gametype's maximum to its slots. A widen fills the
  new slots as the game empties one. Tested on the host
  (`port/xbox/tests/network_game_layout_test.c`: the sizes the game
  asserts, an exact round trip, every refusal).

## Next, in order

1. Measure the console's game state by pool (the game logs its
   allocations) and the 128 MB layout's room; then 128 player and machine
   slots on the console, its game's maximum 16.
2. The object translation table, at the decode and encode points listed
   above, if the console's measurements (`debug.txt`, "cross-play: ...
   host's objects") show a PC host's indices reaching its own half.
3. Two machines on one LAN: a PC build's dedicated server, the devkit
   joining over system link (`console.bypass_security` on); then the reverse.
4. Internet joins through the PCs' p2p tunnel under
   `transport_endpoint_winsock.c` (the README's "Online Games on the Xbox").
