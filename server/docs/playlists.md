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

**Halo PC (Custom Edition) maps**, in `maps/ce/`, go in as `<name>@ce`:

```
timberland@ce    team_slayer
```

**HaloMD maps**, in `md_maps/`, go in as `<name>@md`:

```
bgplus_5@md      ctf
```

Both need Custom Edition's `bitmaps.map`, `sounds.map` and `loc.map` in
`maps/ce/`, and the x64 or arm64 server. Players need the same map file to
join such a game.

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
| `gearbox.txt` | Halo PC's own maps (`@ce`), Slayer and Team Slayer in turn. |
