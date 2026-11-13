# Picture checks

`tools/picture_check.py` takes pictures of a few maps from fixed camera spots
and compares them with a reference. It's there to catch things like the
Blood Gulch cliff holes before players do.

It runs our Linux build on its own Xvfb display, so it needs a Linux machine
with Xvfb, ImageMagick (`import`, `convert`) and
Python 3. Nothing else.

## Running it

```
python3 tools/picture_check.py \
    --build path/to/chupathingy \
    --reference-build path/to/a-build-you-trust/chupathingy \
    --maps-ce "path/to/Halo Custom Edition/maps" \
    --out /tmp/picture-check
```

For each map in `tools/picture_check_poses.txt` it starts the build in a solo
game, waits for the map to settle, hides the HUD, moves the debug camera to
each spot (`camera.txt` and `debug_camera_load`) and captures the screen. Then
it does the same with the reference and compares each pair.

The reference can be:

- `--reference-build`: another build, like the last release. A build that's
  known to draw the maps right is the usual choice.
- `--reference-env NAME=VALUE`: the same build again with a setting changed.
  For culling bugs, compare with the culling off once the build has a switch
  for it.
- `--reference DIR`: the pictures from an earlier run's `--out`. A run with no
  reference only takes pictures, so that's how you make one.

`--map bloodgulch` limits a run to one map. `--display` picks another Xvfb
display if `:91` is taken.

## What fails

Each spot gets one line in `report.txt`:

- **HOLES**: more than `--max-holes` pixels (150) are sky colored where the
  reference shows something else. That's what a missing cliff or piece of
  ground looks like. The pixels are marked red in `<map>_<spot>_holes.png`.
  The sky color comes from one pixel of the reference's picture, the middle
  of the top row unless the spot says otherwise.
- **CHANGED**: more than `--tolerance` of the picture (3%) differs by more
  than 40 in some color. That catches other picture regressions, like
  missing objects, wrong fog or a broken shader.

The script exits with 1 if any spot fails. The pictures, the game's logs and
the hole maps stay in `--out`.

## Maps

Maps only come through a data root of links, never copies. Set
`HALO_DATA_ROOT_SCRIPT` to a script of your own that takes a folder and
links your Xbox maps into it (`maps/`). `--maps-ce` links a folder of Custom
Edition maps as `maps_ce` when the data root doesn't have one, such as a
retail Halo CE install's own `maps` folder. Saves and settings go to a scratch folder that's deleted
afterward. The build runs from a folder of links, so its own `config.toml`
is never read or changed.

## Spots

`tools/picture_check_poses.txt` has one spot per line:

```
map name x y z yaw pitch [sky_x sky_y]
```

The map is written the way `init.txt` takes it, with `@ce` for a Custom
Edition map. Yaw 0 looks along +x, 90 along +y. Pitch is up from level. To
find a spot, run with a pose list of guesses and no reference, and look at
the pictures.

The list covers Blood Gulch's cliffs (from the red base toward the far end
and the rock arch, and across the canyon) and Timberland. Coldsnap 20th
Anniversary XL has no spots yet. The debug camera didn't hold still there,
and the spots I tried were inside its rock.

Before each capture the camera is loaded again, and the picture is taken
until two in a row match. A sky-colored pixel within 4 pixels of the
reference's own sky isn't counted as a hole, so a skyline that's a pixel off
doesn't fail a spot.

## Notes

- Use the same kind of build on both sides when you can. The picture checks
  compare pixels, so a release build against a debug build, or a 32-bit
  build against a 64-bit one, can differ a little. The tolerance is there
  for that.
- Retail Halo CE under Wine isn't used as a reference yet. Its camera can't
  be placed the same way, and its shaders differ from ours.
