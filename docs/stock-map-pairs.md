# Stock map pairs (Halo PC and Custom Edition)

Halo PC retail (cache version 7, `maps_pc`) and Custom Edition (609,
`maps_ce`) both ship deathisland, gephyrophobia, icefields, infinity,
timberland, dangercanyon, sidewinder, beavercreek and the rest of the stock
multiplayer maps. The two editions' files are different files with different
versions (the first 32 bits of the whole-file hash, which a host sends with
its game). OpenCE's hosts announce either one as `custom_maps\<name>` and by
the bare file name on the game list.

## What a player sees

- The game list labels a bare name that is not an Xbox map (deathisland,
  hugeass) as a Halo PC map (HCE), not Xbox.
- A stock map is found in `maps_ce` or `maps_pc` (and the older places). The
  file with the host's version is used; a file of the other edition is used
  only for a pair in `port/linux/game/map_stock_pairs.h`.
- With no matching file, the player is told which edition the game needs when
  their files say so: they hold only the Halo PC file, so the game needs the
  Custom Edition one (copy it from the Custom Edition installer into
  `maps_ce`), or the other way round (`maps_pc`). With neither, both folders
  are named.

## Verified pairs

`map_stock_pairs.h` lists only pairs checked with
`tools/compare_stock_maps.py`, never by name. The tool reads both files'
tags and requires the same tags (class and path), and the same bytes for the
tags the simulation reads (scenario, objects, units, weapons, vehicles,
projectiles, damage effects, collision, physics, animations, globals, model
markers) and for every structure BSP, once the file's own addresses and tag
identifiers are set aside. Sounds, bitmaps, shaders, UI and rendering data
are not compared. Run it as

    tools/compare_stock_maps.py --folders <Halo PC maps> <Custom Edition maps>

and put the printed rows in the table. A map that prints "not
interchangeable" stays out: its game is joined with the host's edition of
the file only.

## Status

The table is empty: only Custom Edition files were at hand when this was
written, so no pair has been compared yet. To fill it, someone with both
editions' `.map` files for the stock multiplayer maps runs the command above.
`tools/test_compare_stock_maps.py` checks the tool on a Custom Edition file
made to pass for a Halo PC one.
