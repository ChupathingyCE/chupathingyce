# Playlists

A playlist is a text file in the data folder (usually in `playlists/`) that
`HALO_DEDICATED` names. One game a line: the map, then the game type. Lines
starting with `#` are ignored. The server plays them in order and starts
over after the last. With `HALO_DEDICATED_PLAYLIST_SHUFFLE=true` (or
`sv_set playlist_shuffle true`) each pass after the first is in a new random
order, and the entry just played is never first in the next pass; the
playlist file is not changed, and `sv_mapcycle` lists the order in play.

```
# map            game type
bloodgulch       slayer
prisoner         slayer
damnation        team_slayer
chillout         slayer
```

## Maps

**The Xbox maps**, in the data folder's `maps/`: `beavercreek` (Battle
Creek), `bloodgulch`, `boardingaction`, `carousel` (Derelict), `chillout`,
`damnation`, `hangemhigh`, `longest`, `prisoner`, `putput` (Chiron TL-34),
`ratrace`, `sidewinder`, `wizard`. Use the North American (NTSC) disc's
maps, as the players do. The server needs `ui.map` and every one of them,
not just the playlist's.

**Halo PC (Custom Edition) maps**, in `maps_ce/` beside `maps/`, go in as
`<name>@ce`:

```
timberland@ce    team_slayer
```

**HaloMD maps**, in `maps_md/`, go in as `<name>@md`:

```
bgplus_5@md      ctf
```

**Halo PC's own maps** (the retail game's, from your Halo PC disc), in
`maps_pc/`, go in as `<name>@pc`:

```
bloodgulch@pc    slayer
```

All of them need Custom Edition's `bitmaps.map`, `sounds.map` and `loc.map`
in `maps_ce/`, and the x64 or arm64 server. Players need the same map file
to join such a game. OpenCE (build-147) players can join a Custom Edition
map's game with the map in their `custom_maps/`; HaloMD and Halo PC retail
games are ChupathingyCE's only (an OpenCE player is told the map is
missing).

**Xbox map packs** (community packs of Xbox maps, such as Halo CE+ X), each
in its own `maps_<pack>/` folder beside `maps/`, go in as `<name>@<pack>`:
Halo CE+'s maps are in `maps_cex/` and its Gothra is `wizard@cex`, whatever
retail map shares the file name:

```
wizard@cex       slayer
bloodgulch@cex   ctf
```

Copy the pack's `maps` folder (its `maplist.txt` too) into `maps_<pack>/`;
the retail maps stay in `maps/`. Pack maps are Xbox maps, so any server
plays them (32-bit or 64-bit, no Custom Edition files needed). The server's
listing names each by its pack's name ("Gothra", Halo CE+), from the
library of known packs (`port/assets/maps`) or the pack's `maplist.txt`.
Desktop players and Xbox consoles (Warthog) of a build with map packs join
with the same version of the pack in their own `maps_<pack>`; a player
without it is told which pack and folder it needs. OpenCE and older builds
can't join these games (they are told the map is missing). A pack's folder
name is 1 to 8 letters and digits, other than `ce`, `md`, `pc`, `de`, `fr`,
`es` and `it`.

The older folders still work: `maps/ce/` (ChupathingyCE 0.7.0b and before),
`md_maps/`, and OpenCE's `custom_maps/` for Custom Edition maps. A file
named `<name>@ce.map` (`@md`, `@pc`) in `maps/` plays too. The server never
moves folders itself unless `HALO_MOVE_OLD_MAP_FOLDERS=yes` (see
[Settings](settings.md)).

## Game types

`slayer`, `team_slayer`, `ctf`, `ironctf`, `king`, `team_king`, `oddball`,
`team_oddball`, `race`, `team_race`, `rally`, `elimination`, `stalker`,
`accumulation`, `bomb`, `grifball`, `grifball_open_pitch`.

### Bomb and Grifball

`bomb`, `grifball` and `grifball_open_pitch` are Chupathingy's own game
types ([docs/bomb.md](../../docs/bomb.md)). Two teams carry a bomb (the
map's oddball ball) into the other team's goal (its CTF flag stand). They
play on any map with two CTF bases.

| Game type | Rules |
| --- | --- |
| `bomb` | 3 seconds in the goal to score; a goal starts a new round; first to 3. Normal weapons, no vehicles |
| `grifball` | One hit kills, no shields, melee only (the map's energy sword if it has one). The carrier is 150% fast, takes two thirds of the damage and is overshielded. Rounds of 3 minutes, a goal ends the round, first to 3. Respawn 3 seconds |
| `grifball_open_pitch` | Grifball for big maps (Blood Gulch): everyone 175% fast, the carrier 200%, rounds of 5 minutes |

Only players on a Chupathingy build with Bomb can play these. A client
without it (an OpenCE client, or an older Chupathingy) is dropped from the
lobby or the game about 4 seconds after it joins, and every player is told
why. The server plays on. Its other games are unchanged, and OpenCE clients
play those as before.

A game type file can start from these (`base = "grifball"`) and change their
own fields: `bomb.arm_time`, `bomb.reset_time` and `bomb.round_time`
(seconds), `bomb.new_round_on_score`, `bomb.melee_only`, and `bomb.traits`
(`"none"`, `"grifball"`, `"open_pitch"`).

Settings of the server's (`config.toml`, or their environment
variables) tune them:

| Setting | Variable | What it does |
| --- | --- | --- |
| `game.bomb_spawns` | `HALO_BOMB_SPAWNS` | Where the bomb spawns on a map: `"<map> <x> <y> [<z>]"`, comma separated (`"bloodgulch 63.31 -120.38, hangemhigh 22.8 -3.7"`), the map as the playlist names it. It rests on the ground under the point. A map not named has the game's own spot: Blood Gulch's halfway between its teleporters' exits, any other map's halfway between the goals |
| `game.bomb_open_pitch_carrier_health`, `game.bomb_open_pitch_carrier_resistance`, `game.bomb_open_pitch_carrier_shields` | `HALO_BOMB_OPEN_PITCH_CARRIER_HEALTH`, `_RESISTANCE`, `_SHIELDS` | The carrier in `grifball_open_pitch`: health in percent of everyone's (0: 400), damage resistance in percent (0: 400), and `"none"`, `"normal"` or `"overshield"`. The defaults let the carrier live through one melee blow, not two |
| `game.bomb_open_pitch_fumble` | `HALO_BOMB_OPEN_PITCH_FUMBLE` | `true` (the default): a melee blow the carrier lives through knocks the bomb loose |
| `game.bomb_open_pitch_speed` | `HALO_BOMB_OPEN_PITCH_SPEED` | Everyone's speed in `grifball_open_pitch`, in percent, 50 to 200 (0: 175). The carrier's 115% goes on top, up to 200%; at 200 the carrier is no faster than anyone |

A team game needs at least two players. While only one player is waiting,
the server skips ahead to the next game in the playlist that isn't a team
game; if there isn't one, that player waits for a second.

## The playlists included

| Playlist | Games |
| --- | --- |
| `free_for_all.txt` | Slayer on every map. |
| `small_maps.txt` | Slayer on the smaller maps. |
| `big_maps.txt` | Slayer on the roomier maps, for big games (32 players). |
| `team_slayer.txt` | Team Slayer on every map. |
| `slayer.txt` | Slayer and Team Slayer, every map. |
| `bloodgulch.txt` | Blood Gulch only, Team Slayer and Slayer in turn, for the biggest games. |
| `gearbox.txt` | Halo PC's stock maps as Custom Edition has them (`@ce`), Slayer and Team Slayer in turn. |
| `gearbox.txt` | Halo PC's own maps (`@ce`), Slayer and Team Slayer in turn. |
| `grifball.txt` | Grifball: Open Pitch on Blood Gulch, then Grifball on the smaller CTF maps. Chupathingy clients only. |

## Playlists made on the server

`sv_playlist_new`, `sv_playlist_add`, `sv_playlist_remove`,
`sv_playlist_move`, `sv_playlist_delete` and `sv_playlist_use` (or the
control panel's Playlists) make and edit playlists in the data folder's
`admin/playlists/`, which must be writable. The `playlists/` folder (the
server's own, read-only in our container images, replaced by a release) is
read and never written: a playlist of the same name edited here is saved
in `admin/playlists/` and played instead. `sv_playlist_use <name>` plays
one from the next game (at once in the lobby) and keeps it across restarts,
until `HALO_DEDICATED` is changed. `sv_mapcycle_add` and `sv_mapcycle_del`
change the playlist played. A playlist is checked whole before it is saved:
every map one the server has, every game type a built-in or a file that
reads right.

## Game type files

`admin/gametypes/<name>.toml`, made with `sv_gametype_new <name> <base>`
and changed with `sv_gametype_set <name> <setting> <value>` (or by hand),
are game types a playlist names like a built-in's. A file names its base,
a built-in game type, and the settings it changes: those of the game's own
editor, and score limits past its caps (up to 9999).

```toml
# admin/gametypes/oddball50.toml
base = "oddball"
name = "Oddball 50"
score_limit = 50
time_limit = 20

[players]
lives = 0
health = 150
```

`sv_gametype <name>` lists every setting, its value, and the file's;
`sv_gametypes` lists the files and the built-ins. A file that does not read
right is refused when it is saved, and a playlist entry naming one is
skipped (the log says why). Players need nothing new: the game type is sent
as the game's own variant.

A side's vehicles (`vehicles.red`, `vehicles.blue`) are a vehicle set,
`"pc"` (every vehicle the map places, as Halo PC places them) or
`"custom"` (the counts in `vehicles.red_warthog` and the rest).

The game writes the same files. Every gametype a player saves, in any of
the menus' editors (the Chupathingy menus' EDIT GAME TYPE, Gearbox's and the
Xbox's), is also saved as `gametypes/<name>.toml` in their save folder, and
a file put there shows up in the game's gametype lists the next time the
main menu loads. So a game type made in the game can be copied into a
server's `admin/gametypes/` and played there, and one from a server can be
hosted from the game; both read and play it with the same code
(`server/src/server_config.c`, `server/src/server_gametype_variant.c`).
