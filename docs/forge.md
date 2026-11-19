# Forge

Forge is a layout editor for the multiplayer maps. You fly the map as a monitor, place, move, turn and delete objects, put down your own spawn points, teleporters and game type objects (CTF flags, hill points, oddball spawns, race checkpoints), and save the result as a layout file. A saved layout can then be played as a normal game of any game type.

It's off by default. Turn it on with `game.forge = true` in `config.toml` (or `HALO_FORGE=1`). Until then the `forge` console commands only say that it's off, and nothing else of it runs. Forge is local only: a Forge game is a game you play alone, and a layout is played as a local game. Nothing of it is sent over the network (co-op editing on Delta is planned separately).

## Starting

From the console:

- `forge start bloodgulch` starts a Forge game of Blood Gulch (any of the Xbox multiplayer maps, by its short name: `beavercreek`, `hangemhigh`, `longest`...). You start as the monitor.
- `forge start bloodgulch my_layout` starts it with a saved layout placed.
- `forge play bloodgulch my_layout ctf` plays a saved layout as a game of that type: `slayer` (the default), `teamslayer`, `ctf`, `king`, `oddball` or `race`.
- `forge layouts` lists the saved layouts (a damaged one is listed as such, never read).

## Controls

As the monitor (the keyboard uses your bindings of these buttons):

| Button | Does |
|---|---|
| Left stick, right stick / mouse | Fly, look |
| A / left stick click | Fly up / down |
| Left trigger | Hold to fly faster; holding an object: rotate it (right stick; right trigger too to roll). Right stick click switches to nudge mode (small steps) |
| Right trigger | Grab the object or marker under the crosshair, let go of the one held |
| Y | Delete the object held, or the one under the crosshair |
| X | The Forge panel; holding something: its properties |
| B | Copy the object held or under the crosshair |
| White | Movement speed; in rotate mode, rotation snapping (off, 15, 45, 90 degrees) |
| Black | Set the object upright |
| Back + right stick click | Switch between the monitor and playing |

The Forge panel: D-pad left and right pick a category (VEHICLES, WEAPONS, EQUIPMENT, SCENERY, TELEPORTERS, OBJECTIVES, SPAWNING) or FORGE (undo, redo, snap, save layout, new round, play as Spartan); up and down pick a row; A chooses, B closes.

Properties: team, respawn time, placed at start, exact yaw, pitch and roll, and physics (normal, fixed: never moves, phased: no collision). A spawn point's are its team, game type, kind and facing; a teleporter's its channel; a game type object's its index (a CTF flag's team, a hill's number).

The budget is 256 objects and markers. A layout's spawn points replace the map's own for the players they fit (the game type, the team); a game type object replaces the map's own of that type (a CTF flag: that team's), any type the layout has none of keeps the map's.

## Layout files

Layouts are saved in `forge/<map>/<name>.hmv` in the save root. The format is small and fixed, so layouts can be shared as files:

- a 64-byte header: `HFRG`, version, header size, flags, the map's short name, the map's checksum, the counts, the payload's size and its CRC-32;
- the tag paths the records place (never tag indices);
- 48-byte records: kind, path index, team, position, yaw, pitch and roll, respawn time, game types, flags;
- the title, author, description and date, as UTF-8.

All little-endian. Versions 1 to 7 are read. Each version adds what a record may hold (AI settings, spawn points, teleporters, game type objects, physics), and a layout is written as the lowest version that holds it. `port/linux/game/forge_files.h` describes every byte.

Every file goes through the validator before anything is read from it, whether you made it or it came from somewhere else. It checks the size, the header, the version, the map, the counts against the limits, the payload size and CRC-32, every path (characters, length, groups Forge places), every record (kind, path index, every field in range, finite floats inside the world, no unknown flags) and the texts (terminated UTF-8). A file that fails any check is refused whole and the log says why. When a layout is placed, a record whose tag the map doesn't have is skipped, and the budget and the map's bounds are checked again. `tools/test_forge_files.py` tests the validator against malformed files, and fuzzes it.

## Console commands

Besides `start`, `play` and `layouts`: `forge` (switch monitor and Spartan), `forge save <title>`, `forge spawn <name>`, `forge marker respawn|initial|sender|receiver|flag|hill|oddball|race`, `forge grab [#id]`, `forge drop`, `forge delete [#id]`, `forge turn <degrees>`, `forge property <name> [-]`, `forge distance <world units>`, `forge nudge <direction>`, `forge snap [on|off]`, `forge undo`, `forge redo`, `forge deleteall <category>`, `forge round`, `forge camera x y z yaw pitch`, `forge list [category]`, and `forge where`, `budget`, `objects`, `spawns`, `netflags` to log the state. The tests' controller: `forge press`, `forge stick`, `forge look`, `forge panel <step>`, `forge shot`.
