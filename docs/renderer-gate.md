# The renderer gate

`tools/renderer_gate.py` answers one question for the machine it runs on:
does bgfx draw the same picture as OpenGL, and does it draw it faster? bgfx
becomes a platform's default renderer only where both answers are yes.
OpenGL stays one setting away (`display.renderer = "gl"`) either way.

It prints `PASS` or `FAIL` with the reasons, and writes `report.md`,
`report.html` (the pictures side by side, the differences first) and
`results.json` to `--out`.

## Running it

macOS (Metal):

```
HALO_DATA_ROOT_SCRIPT=~/halo-maps/new-data-root.sh \
python3 tools/renderer_gate.py --binary build/macos/chupathingy \
    --maps ~/halo-maps/maps --maps-ce path/to/ce/maps --maps-mcc path/to/workshop/content/976730 \
    --bench --bench-map bloodgulch --bench-map damnation \
    --out /tmp/gate
```

Linux (Vulkan), with each copy on its own Xvfb display:

```
python3 tools/renderer_gate.py --binary build/linux64/chupathingy --xvfb :93 \
    --env HALO_VULKAN_BLOCKLIST=false --out /tmp/gate ...
```

`HALO_VULKAN_BLOCKLIST=false` lets bgfx run on Mesa's software Vulkan
(lavapipe), which the blocklist would otherwise skip for OpenGL. Pictures
from a software renderer are fine for the looks half; its frame times say
nothing about a real GPU.

Windows: `--second d3d11` checks Direct3D 11 instead of Vulkan; run it once
for each. `HALO_DATA_ROOT_SCRIPT` is run with `bash`, so use Git Bash's, or a
script of your own that makes the same links.

It needs Python 3 with numpy and Pillow, and a build from after the
`screenshot` console command and `debug.freeze_at_tick` were added. Maps come
from a data root of links made by the script `HALO_DATA_ROOT_SCRIPT` names
(as for `tools/picture_check.py`); nothing is copied.

## Pictures

For each map in `tools/renderer_gate_views.txt` the build starts once per
renderer in a solo game that stops at a fixed tick
(`debug.freeze_at_tick`, 300 by default, ten seconds in; campaign levels
stop later). A stopped game draws the same moment over and over, so both
renderers get the very same scene: the same particles, the same sky, the
same Marines. Frames between ticks are off (`display.interpolation`),
since a stopped game has no next tick to blend towards.

A view is one of:

- `player`: the player's own view, with the HUD and the weapon;
- `spawn <turn> [pitch]`: the debug camera at the player's eyes, turned;
- `camera x y z yaw pitch`: the debug camera at a fixed spot.

A solo multiplayer game puts the player at a random spawn, so `player` and
`spawn` views are for campaign levels, whose start is fixed. Multiplayer maps
get fixed cameras, and `action:<map>` views: the map in the recorded game of
`tools/test_determinism.py` (the scripted bot playing), recorded once and
played back on each renderer, stopped at its tick. That's where the HUD, the
weapon, shots and effects are checked. The recorded game is hosted on
127.0.0.1's game port, so the gate waits while another copy of the game on
the machine holds it.

The game saves each picture itself with the console's `screenshot <name>`
command (the same read-back as the screenshot key, the window as shown), so
nothing captures the screen and the tool works the same on every system. A
view is taken again until two pictures in a row are the same, which waits
out textures still streaming in and bgfx's shaders still compiling.

Each pair is compared three ways:

| Measure | What it catches | Limit (option) |
| --- | --- | --- |
| SSIM of the brightness, after a 2x2 average | missing or different detail, holes, a broken shader | `--min-ssim 0.97` |
| Share of pixels whose color moved by more than 24, after a 2x2 average | anything local: a missing light, a decal, an effect | `--max-changed 0.01` |
| Changed share of the worst part of a 16x9 grid | something small drawn differently: a HUD meter, the weapon, a tree | `--max-tile 0.08` |
| Change in average brightness | a picture that is darker or brighter overall (gamma, fog, lighting) | `--max-brightness 1.5` |

The 2x2 average keeps anti-aliasing at edges from counting on its own. A
view that is inside all three limits matches. The report shows OpenGL, the
other renderer and a picture of the difference (OpenGL faded, the changed
pixels red) for each view.

`--repeat-first` runs each renderer twice. Where either differs from itself
(a campaign level whose AI or weather doesn't stop with the game, a
flickering light), or a view never held still, a view that differs is
`UNSTEADY`: it is shown with its numbers and isn't counted.

When the renderer's log says it has no pass for a setting ("anti-aliasing:
fxaa is not in the bgfx renderer yet, so msaa2x") or skips a kind of draw
("bgfx: wireframe (not drawn yet)"), the view is `NOT IN BGFX` whatever the
pictures say: an anti-aliasing pass changes little that the 2x2 average
keeps.

`--from-pictures` runs nothing: it compares the pictures already in `--out`
again (with other limits, say) and keeps the frame times of an earlier
`--bench`.

`--variant name:NAME=VALUE[,NAME=VALUE]` takes the pictures again with
settings changed on both renderers, such as
`--variant fxaa:HALO_ANTI_ALIASING=fxaa` or
`--variant ppl:HALO_PER_PIXEL_LIGHTING=true`. This is how the parity table
is checked. Only the default settings decide the verdict.

## Frame time

`--bench` runs `tools/bench.py` on each `--bench-map` (Xbox multiplayer
maps; Blood Gulch by default) for both renderers: a recorded game played
back in a window at `--size`, V-Sync off, no frame cap, `--bench-runs`
times, the median kept. bgfx passes on a map when:

- its mean frame time is at least `--bench-margin` (5%) lower than OpenGL's,
- its p99 frame time is no higher,
- its 1% low frame rate is no lower,
- and it did not fall back to OpenGL.

Nothing else should be running on the machine while it measures. The gate
notes the load average before and after each map, and a map measured with
a load of 1.0 or more, before or after, fails as "busy", to be measured
again.

## The verdict

`PASS` needs every default view to match, bgfx to have drawn every map
(no fallback), and the frame-time rules on every bench map. Without
`--bench` the best it says is `PASS (looks only)`.

## In CI

The runs are as fixed as they can be: a fixed tick, a fixed window size,
V-Sync off, no network, muted, their own home, save and data folders. On
Linux, `--xvfb` gives every copy its own display.

`.github/workflows/renderer-gate.yml` runs the pictures half every Monday on
main and by hand (Actions, Renderer gate, with a few maps if you like): the
64-bit Linux build, bgfx on lavapipe against OpenGL on llvmpipe, with
`--repeat-first`, the report in the run's summary and the pictures as an
artifact for two weeks. Software rendering is fine for looks and says
nothing about frame time, which is measured on real GPUs before a platform's
default changes.

GitHub's own runners have no maps, and the maps never go into a cache, an
artifact or a download. So the job runs only on a machine of ours that has
them: the repository variable `RENDERER_GATE_RUNNER` names its runner's
labels as JSON (`["self-hosted", "linux", "halo-maps"]`), and while it is
unset the job is skipped. The runner sets `HALO_GATE_MAPS` (the stock Xbox
maps' folder), `HALO_DATA_ROOT_SCRIPT` (the script that makes a data root of
links to them) and, for the Custom Edition views, `HALO_GATE_MAPS_CE`; it
needs Xvfb, Mesa's lavapipe and llvmpipe, and Python 3 with numpy and Pillow.

The checks that need no maps run on every pull request: bgfx's shaders
compiled for each backend (`tools/bgfx_shader_check.py`), and the gate's own
comparison (`tools/test_renderer_gate.py`).

`tools/picture_check.py` checks bgfx too: `--renderer bgfx
--reference-renderer gl` compares the build drawing with bgfx against the
same build drawing with OpenGL, at the pose list's spots.
