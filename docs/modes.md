# Game modes

Game modes are rule sets laid over a game engine: SWAT over Slayer, Hardcore
over any game type. They are OpenCE's game types, played by the rules of
OpenCE's network version 35 so that a SWAT game means the same thing on
every machine in it, ours or theirs. Our players join their SWAT and
Hardcore games, their players join ours, and the game types sit in our game
type list after Bomb, Grifball and Open Pitch: **SWAT**, **Hardcore**, **HC
CTF**, **HC Oddball** and **HC King**.

SWAT, Hardcore, Gun Game and Infection were first made by OpenCE's
contributors. Our code was written from our own description of how the
games behave and what the wire carries (nothing of theirs was read for it),
so the games stay in step across both projects. Thanks to them for the
modes; Gun Game and Infection come next.

## The modes

| Mode | Runs on | What changes |
| --- | --- | --- |
| SWAT | Slayer | No shields, whatever the variant says. A shot to the head of a player's biped kills: a bullet, a sniper round, plasma, a needle or shotgun pellets, not melee or an explosion. |
| Hardcore | any game type but Bomb | No motion tracker is drawn. No aim assist of any kind, for any input device: neither the autoaim that bends shots nor the magnetism that pulls the view. A player who kills another (not themselves, not a teammate) is restored: health to full, and shields to full where they were below it, no longer depleted or charging; an overshield past full is kept. |

The host applies the host's rules (SWAT's headshots, Hardcore's restore);
every machine applies its own (SWAT's no shields at the game's start,
Hardcore's tracker and aim), so a client that joins an OpenCE host plays
fair and stays in step.

A mode takes effect only where it belongs: SWAT on the Slayer engine (on
any other it plays as the plain game type), Hardcore on any engine but Bomb,
whose game types keep their own rules ([bomb.md](bomb.md)).

## The built-in game types

| Game type | Base | Settings |
| --- | --- | --- |
| SWAT | team Slayer | no shields, no grenades, no vehicles, a 5-second respawn, nobody on the motion tracker, goals by nav point, each player spawns with a pistol and an assault rifle, no weapons on the map |
| Hardcore | team Slayer | friendly fire on, nobody on the motion tracker |
| HC CTF | CTF | friendly fire on, nobody on the motion tracker |
| HC Oddball | team Oddball | friendly fire on, nobody on the motion tracker |
| HC King | team King | friendly fire on, nobody on the motion tracker |

They appear in the game type list of both menu sets, and the dedicated
server knows them by name in a playlist (`swat`, `hardcore`, `hc_ctf`,
`hc_oddball`, `hc_king`). A game type file ([the server's
docs](../server/docs/playlists.md)) may also give any Slayer game a `mode`
(`none` or `swat`) and any game type but Bomb `hardcore = true`.

## Editing

In the Chupathingy menus' GAME SETUP editor, the general page has **MODE**
(NONE, SWAT) for a Slayer base and **HARDCORE** (ON, OFF) for every base but
Bomb. The game type editor's Slayer rules screen has MODE and its player
options screen has HARDCORE. A saved game type keeps them: its options block
is version 2 (`GPVO`, 0x2C bytes); a version 1 file is read with none.

## On the wire

Everything a mode needs travels in the game settings record that every
machine already reads: the gametype's PC options grew from 0x1C to 0x2C
bytes in OpenCE's version 35, and the 16 further bytes carry the mode (byte
0x1C: 0 none, 1 SWAT, 2 Infection, 3 Gun Game), Hardcore (0x1D), and each
mode's own options (0x1E to 0x25; the rest reserved). Every machine
sanitizes them the same way: an out-of-range byte becomes its default,
never clamped. `port/linux/game/network_rules.c` holds the rules and
`port/linux/tests/network_rules_test.c` checks the bytes of every built-in.

- **As a client**: our build reads a 35+ host's record and plays SWAT and
  Hardcore. A game of Gun Game or Infection is still left cleanly with a
  message naming the game type, until those modes are built.
- **As a host**: our 35+ record carries the real bytes of the game. A host
  announcing 37 or later also puts the game's kind (the mode in the low 7
  bits, Hardcore in bit 7) in the advertisement's byte 3 and the internet
  listing's stamp, where OpenCE's browsers read it. A host never sends a
  mode it does not play.

## Who can play

An OpenCE client joins only a host of exactly its own number, so OpenCE
players meet our mode games on a host announcing 35 or later. Delta
machines play them on any host that has the mode. The rest is the host's
refusal, like a Bomb game's ([README.md](delta/README.md), "Game modes"):

| Machine | Host announces 35 or later | Host announces under 35 |
| --- | --- | --- |
| Delta, has the mode | plays | plays (its record comes by its HELLO's number) |
| Delta, without it (an older build, a console) | refused, with a notice | refused, with a notice |
| OpenCE (no Delta) | plays by its own code | refused: it would read a record without the mode |

The capability is `game_modes` (bit 25), with a modes field in HELLO and
WELCOME saying which modes a build plays, so a build that gains Gun Game
later needs no new bit. The wire is `chupa-44b`.

## Consoles

The layer is shared code, so the console builds take it with their next
releases. Until a console ships a build with the modes, its machines are
refused from mode games with the notice and play everything else as before.

## Tests

- `tools/test_network_rules.py`: the option bytes of every built-in, what
  a game needs, what this build plays, the kind byte, the host's refusals
  for every row of the table above, the mode bytes' place in a 35+ record.
- `tools/test_delta.py` and `tools/test_delta_peer.py`: the capability's
  name, the wire's rows, the modes field written and read, and a HELLO or
  WELCOME from a build before the field.
- `tools/test_determinism.py`: a stock Slayer game recorded before the
  layer replays identically on a build with it, which is the proof that the
  layer is inert when no mode is on.
- `tools/crossplay_test.py` (`swat` and `hardcore` directions): our host
  with an OpenCE client and their host with our client, both playing to a
  score with the mode's own facts in the logs.
