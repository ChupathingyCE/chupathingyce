# Patched maps (beta)

A patched map is a stock map with a few tags of another map added, written
as a new Custom Edition map (version 609) with a name of its own. The game
loads it as it loads any Custom Edition map, checks it the same way
(`ce_map_checks.c`), and names it in network games by its own hash, as any
Custom Edition map. The map tools open it as any other map. It's a stand-in
until a real map exists, and a file that modders can open and change.

The first is `openpitch_bloodgulch`: Halo Custom Edition's stock
`bloodgulch.map` with the gravity hammer, energy sword and ball of
"Grifball Beta" (`grifball.map`) by SlappyThePirate (2011), for Grifball's
Open Pitch (docs/bomb.md).

## The parts

- **An asset pack** (`grifball_assets.map`): a Custom Edition map that
  holds a few weapons of another map and every tag they use, with a stub
  scenario. Its bitmaps and sounds in `bitmaps.map` and `sounds.map` stay
  there, as with any Custom Edition map. `tools/ce_asset_pack.py` makes one
  from its source map. The pack is the unit that's shared and updated: it
  isn't in this repository.
- **A recipe** (`port/assets/map_recipes/<name>.json`): the base map
  (family, file, and the SHA-256 of the one stock version it applies to;
  no other map is patched), the packs with their credits, the weapons a
  game type hands out, the ball (the globals' multiplayer ball), the label
  each weapon is held by, the output's name (never the base map's) and the
  recipe's version. A new game type should need a new recipe, not new code.
- **The patcher** (`tools/map_patch.py`, which uses `tools/ce_cache.py`):
  writes the output. A pack tag that's a stock tag the base map has too (same
  group, name and size) becomes the base map's. The others are added after
  the base map's tags, with their pointers, references, models, pixels and
  samples moved with them. The same inputs make the same bytes on every
  machine.

The output's header (its free space from 0x400) says what it was made from,
as `key: value` lines: `patch`, `recipe` (the recipe's version), `base` (the
base map and its hash), `pack` (each pack and its hash), `weapon` (each
weapon the game type hands out), `ball` and `credit`. A machine whose base
map, packs or recipe differ makes a different file, with a different hash, so
it gets the usual missing or mismatched map handling, never a desync.

```sh
python3 tools/ce_asset_pack.py build --map maps_ce/grifball.map --out packs/grifball_assets.map \
    --weapon "h3mt\weapons\covenant\gravity_hammer\gravity_hammer" \
    --weapon "weapons\energy_sword\energy_sword backup" \
    --weapon "weapons\grifball\grifball"
python3 tools/map_patch.py --recipe port/assets/map_recipes/openpitch_bloodgulch.json \
    --maps <folder with bloodgulch.map and grifball_assets.map> --out maps_ce/openpitch_bloodgulch.map
```

Keep the pack out of `maps_ce` itself: it's a map with no BSP, which the
map lists would offer and the game would refuse.

## Not done yet

- The patcher is in Python. The game and the dedicated server are to run it
  themselves (a console command, and before a game on a patched map whose
  output is missing or out of date by its header's lines), which means
  porting `ce_cache.py` and `map_patch.py` to C.
- The pack's download: a link and "put it in maps_ce/packs" for the first
  test.
- The host announcing the pack (its name and hash) over Delta Peer, for a
  clearer message than "missing map".
- The cyborg holds the hammer as an assault rifle and the sword as a pistol
  (the court's own biped has their animations, "gh" and "es"; the stock
  cyborg's graph doesn't).
