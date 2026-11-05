# The bots' test levels

Twenty small multiplayer maps, each made to test one thing the bots do.
`levels.py` describes them, `geometry.py` turns each into a closed mesh,
`build.py` writes the meshes as JMS files with a list of what each level has
placed in it, and `windows.py` writes the batch file that makes them into
Custom Edition maps with the MCC Halo CE Editing Kit and Invader.

| Map | What it tests |
|---|---|
| `bot_steps` | Jumping up again and again: blocks 0.4, 0.55 and 0.7 higher each, to a flag on top |
| `bot_jump_gaps` | Running jumps over gaps of 1, 1.5, 2, 2.5 and 2.8 units; a fall is into a pit with a ramp back up |
| `bot_climb` | A tower climbed by eleven ledges round it, each 0.6 above the last |
| `bot_ledges` | Blocks 0.1 to 1.2 high over a field: which are stepped up, which jumped, which gone round |
| `bot_ramps` | Ramps of 10, 20, 30, 40, 48 and 55 degrees up to a platform; the steepest are too steep to walk |
| `bot_hills` | Rolling hills, nothing flat: on foot and in a Warthog |
| `bot_teleport` | Four walled rooms joined only by one-way teleporters, round in a ring |
| `bot_teleport_chain` | Six rooms in a row with no doors, each left by a teleporter to the next |
| `bot_obstacles` | Pillars and wall pieces across an open field: the way round them |
| `bot_forest` | Dense trunks over gentle ground: weaving through on foot and in vehicles |
| `bot_open_drive` | A flat valley 240 long, a base at each end and vehicles: driving for the flag |
| `bot_badlands` | Wide hilly ground with rock walls: driving the long way round |
| `bot_maze` | A maze of 16 by 16 cells, one way through |
| `bot_maze_big` | A maze of 30 by 30 cells |
| `bot_maze_teleporters` | A 20 by 20 maze with twelve one-way teleporters: shortcuts and ways back to the start |
| `bot_loop` | A U-shaped canyon round a ridge (as Sidewinder): the bases 40 apart across it, 250 round it |
| `bot_loop_small` | A ring corridor round a solid middle, with a raised path on one side |
| `bot_corridors` | Nine rooms joined by corridors 1.5 wide and doorways 1 wide |
| `bot_arena` | A small arena: cover, ledges reached by ramps, a raised middle, weapons |
| `bot_course` | An obstacle course: a ramp, steps, gaps, a narrow ledge, a short maze, a teleporter home |

## How a level is made

A level is the space between a floor and a sky ceiling. Over a grid whose lines
fall wherever a feature has an edge, each cell has a floor: flat at a height,
sloped (a ramp, rolling ground: a height at each corner), or solid up to the
ceiling (a wall). The surface between solid and open space is the mesh: the
cells' tops, the vertical faces where neighbouring floors differ, and the sky
over every open cell. Every face is split where another face meets it, so
every edge has exactly two faces and there are no T-junctions, as tool.exe
needs; `build.py` checks it. Two blocks that meet only at a corner make an
edge with four faces, which tool.exe will not take: the levels place their
blocks so that it never happens (`levels.saddle_free`).

Materials: `floor` and `ground` are Blood Gulch's ground, `wall` its cliff,
`ramp` its ramp, `block` its metal, and `+sky` the sky. The sky is Blood
Gulch's.

## Making the maps

In the editing kit's folder (with Invader's programs in an `invader` folder
there, and the kit's tags unpacked):

```
python3 bot_levels\source\build.py .
build_bot_levels.bat            (every level)
build_bot_levels.bat bot_maze   (one of them)
```

For each level the batch file copies the shaders, runs `tool.exe structure`
(which also makes the scenario), makes the scenario a multiplayer one with
`invader-edit` (its sky, where players start, the two flags, the teleporters,
the vehicles, the weapons), lights it with `tool.exe lightmaps` (at low
quality), and builds `bot_levels\maps\<name>.map` with
`invader-build -g gbx-custom`. The first time, it points the kit's tags at
gbxmodel tags (`invader-refactor -g model gbxmodel`): the kit's tags refer
to models as `model` tags, and a Custom Edition map's are `gbxmodel`; and
it fixes the data in the kit's tags that Invader will not build with (an
index out of bounds in the Scorpion's lens flare, among others) with
`invader-bludgeon -b "*" -T everything`, as Invader's README says to. Each
level's log is `bot_levels\logs\<name>.txt`.

The maps go in the game's `maps` folder (a Custom Edition map: `<name>@ce`
in the game's map list).

## Testing the bots on them

`levels.json` has each level's tour: places the bots are sent round with
`HALO_BOT_GOTO=tour HALO_BOT_TOUR=...`, each leg timed in the log. The
driving levels (`"drive": true`) are for `HALO_BOT_DRIVEBENCH=ground` too.
`test.py` runs each level's tests and adds them up.
