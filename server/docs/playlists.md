# Playlists

A playlist is a text file in the data folder (usually in `playlists/`) that
`HALO_DEDICATED` names. One game a line: the map, then the game type. Lines
starting with `#` are ignored. The server plays them in order and starts
over after the last.

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

The older folders still work: `maps/ce/` (ChupathingyCE 0.7.0b and before),
`md_maps/`, and OpenCE's `custom_maps/` for Custom Edition maps. A file
named `<name>@ce.map` (`@md`, `@pc`) in `maps/` plays too. The server never
moves folders itself unless `HALO_MOVE_OLD_MAP_FOLDERS=yes` (see
[Settings](settings.md)).

## Game types

`slayer`, `team_slayer`, `ctf`, `ironctf`, `king`, `team_king`, `oddball`,
`team_oddball`, `race`, `team_race`, `rally`, `elimination`, `stalker`,
`accumulation`.

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
