# Native macOS Metal renderer (experimental, opt-in)

This is an actual gameplay backend, adapted from VISR revision
`2d1930a32c2f4534ad8252b31eed320bdaad8fac`. It renders Halo's D3D8 calls through
native Metal, with MSL shader translation and an SDL Metal view. It does not
use OpenGL translation, an iOS guest runtime, or visionOS lifecycle code.

## Build and play

```sh
python3 configure.py --macos-metal
ninja macos
build/macos/halo
```

The application bundle at `build/macos/ChupathingyCE.app` also uses Metal after
that build. Game data discovery is unchanged. If necessary:

```sh
HALO_DATA_ROOT="$HOME/Library/Application Support/ChupathingyCE" build/macos/halo
```

For bots, add `--bots` to configure. For the manual offline Blood Gulch launcher
(`tools/play_platform_macos.sh`), also add `--infection`: that flag compiles the
local launcher, not just Infection rules. The script keeps those rules off,
clears scripted input, and disables the unsupported custom collision board on
Metal (rather than leaving an invisible obstacle). Its OpenGL behavior is unchanged.

```sh
python3 configure.py --macos-metal --bots --infection
ninja macos
tools/play_platform_macos.sh
```

Confirm the backend in the startup log:
`Native macOS Metal window initialized` and `Metal on ...`.

The flag selects the renderer **at build time**. This opt-in executable is
Metal-only; there is no runtime GL fallback. Rebuild the usual OpenGL version
with `python3 configure.py --no-macos-metal && ninja macos`. The flag defaults
off. Linux, Linux64, Windows, and Android retain their original renderer and
sources regardless of this flag.

## Current scope and limitations

- Menu rendering, map geometry, textures, weapons, HUD, and ordinary gameplay
  run through Metal. macOS is mono; headset stereo is deliberately disabled.
- `display.per_pixel_lighting` supports the game's recognized model-lighting
  programs, using ChupathingyCE's ambient, distant, point/cone and translucency
  equation per pixel. Diffuse alpha is preserved; unrecognized programs retain
  vertex lighting. The existing default remains false.
- Online Games, Link Profile, and other custom overlay screens render through
  native Metal. The server list remains visible when online play is disabled or
  its server is unreachable. Shapes, text, button glyphs and map-preview cutouts
  are composited into the back buffer before presentation and screenshots.
  With reduced render resolution, overlays scale with the back buffer too.
- All `display.anti_aliasing` choices are implemented: `off`, `fxaa`, `smaa`
  (HIGH preset), `ssaa2x`, `msaa2x`, `msaa4x` and `msaa8x`. Post passes run
  before the HUD, preserve destination alpha/depth, and clamp to each view.
  MSAA resolves color/depth/stencil for subsequent sampling and readback;
  its sample count is capped to device support, as in OpenGL. On this M2 Pro,
  requesting 8× selects 4×, explicitly logged. SSAA preserves the existing
  original-resolution behavior and is bounded by the GPU's texture limit.
- Custom GL-based Forge selectors/platform meshes remain unsupported; use
  OpenGL for those. The screenshot hotkey, scoreboard mouse selection and
  controller-icon enhancement were explicitly deferred, not imported from
  OpenCE. Debug BMP frame capture remains available.
- High-resolution HUD/text art is supported. Map changes asynchronously preload
  HUD PNGs; textures not ready at first use retain the synchronous fallback.
- VisionOS pacing, synthetic foveation, texture replacement experiments, and
  stereo shader warming are not requirements for ordinary macOS gameplay.
- Only Apple silicon has been runtime-tested here; Intel Mac testing remains
  necessary. This is not a claim that all maps or rendering states are tested.

## Display sync and frame limit

- `display.vsync` sets the window's `CAMetalLayer.displaySyncEnabled`
  (`metal_host.m`, `metal_vsync.h`): on, drawables are shown at the display's
  vertical blank; off, at once (tearing is possible). It is applied at start-up
  and again when Settings is saved (`platform_display_apply`), with no restart.
  The Metal window has no GL context, so no GL swap interval is ever set.
- With vsync off, `display.max_fps` limits the frame rate after each
  `gpu_present` (`platform_video_frame_limit`, arithmetic in
  `renderer/frame_limit.h`), with the OpenGL renderer's rules: `0` is twice
  the display's refresh rate (60 Hz if unknown), a negative value is no
  limit. It is re-read whenever the configuration changes.
- `display.frame_pacing` (the visionOS refresh-hold scheduler) is not used on
  macOS: it needs a CADisplayLink refresh period this port does not have and
  would hold drawables even with vsync off. A log line says so if it is set.
- Limits: `metal-smoke` and unit tests do not measure on-screen frame times;
  vsync on/off, `max_fps` values, fullscreen/resize and ProMotion displays
  need checking in play (the Metal log's 600-frame "frames shown for" report
  helps). The OpenGL build's own vsync and limit are unchanged.

## Pipeline caching and HUD preload

`debug.metal_pipeline_archive` and `debug.shader_list_warm` default to true
following repeated clean background launches. Map shader lists are warmed at
map changes and learned into `<data-root>/metal-shader-lists`. Shader-list keys
include AA sample counts and per-pixel lighting variants.

Pipeline archives live under
`~/Library/Caches/io.github.chupathingyce/metal-pipelines/`. Previously loaded
segments are lookup-only; new pipelines enter a fresh segment, saved at exit.
This avoids a reproducible driver crash serializing an updated loaded archive.
The segment set resets after 16 segments to bound accumulation.

Measured Blood Gulch runs reduced pipelines compiled during play from 70–97
without caching/warming to 7 after learned lists and archive reuse. A baseline
recorded 70 pipelines in 24 ms; warmed map pipelines took about 2 ms. These are
compile counters/times, not a claim that all gameplay stalls disappear.
HUD preload decoded 69 pictures ahead of use, with no first-draw decodes after
map load in that run. Tests cover preload ownership and synchronization under
ASan/UBSan and ThreadSanitizer.

## Safety boundary

The frontend keeps Xbox addresses as 32-bit addresses and converts them with
`xbox_pointer`/`XBOX_ADDRESS`; native shader pointers are represented by a
bounded handle registry. Vertex uploads use checked 64-bit span arithmetic,
16 MiB upload limits, and a 65,536-vertex draw limit. Texture spans are checked
before CPU reads/protection. The texture cache conservatively accounts BGRA
storage against `graphics.texture_cache_mb`, protects current-frame textures
from eviction, and caps entries. Render targets and embedded art are outside
that cache budget. These checks reduce risk; they are not a complete security
audit of imported code.

## Validation performed

- Debug and release CI builds passed on Apple M2 Pro, producing arm64/x86_64
  universal bundles. The default OpenGL reference was rebuilt from this branch;
  Intel compilation is not Intel runtime validation.
- Captured and inspected the main menu and Blood Gulch gameplay screenshots.
- Ran a Blood Gulch Slayer match with three bots moving and fighting.
- Repeated gameplay with `MTL_DEBUG_LAYER=1`: Metal API Validation enabled,
  `MTLDebugDevice` active, no Metal validation errors observed.
- Reproduced the stagnant Online Games menu with scripted SDL key events, then
  verified the fixed screen and Link Profile navigation with online play off.
- Ran the bundled executable through the manual offline one-player Slayer
  launcher with Metal API validation enabled.
- Upload and overlay-layout boundary tests run under AddressSanitizer and
  UndefinedBehaviorSanitizer; launcher tests preserve OpenGL behavior.

```sh
python3 -m unittest tools.test_macos_metal_build tools.test_metal_upload_bounds tools.test_metal_overlay \
  tools.test_metal_frame_limit tools.test_metal_vsync
python3 -m unittest discover -s tools -p 'test_metal_*.py'
python3 tools/ci_build.py macos-metal debug
python3 tools/ci_build.py macos-metal release
ninja macos-metal-smoke
build/macos/metal-smoke
```

The smoke-test target exists only with `--macos-metal`. It is separate from the
game and tests shader compilation, triangle rendering, and GPU readback.

### Automated image regression

```sh
# Explicitly permit hidden/background launches; builds both renderers and
# restores the Metal build afterward. Installed config/saves are untouched.
HALO_ALLOW_LAUNCH=1 python3 tools/metal_regression.py --output /tmp/metal-regression-new
# Or run the same nine-case matrix through unittest:
HALO_ALLOW_LAUNCH=1 python3 -m unittest tools.test_metal_regression
```

The output directory must be new or empty. `--data-root` selects installed game
maps; optional `--gl-app` and `--metal-app` executable paths reuse prebuilt
bundles. Without prerequisites the CLI exits 77 and the runtime unittest skips.
Ordinary unit-test invocations do not launch the game without explicit permission.

The matrix covers the menu, idle Blood Gulch with high-resolution HUD, every AA
mode, and per-pixel lighting. Eighteen 14-second launches take about 4½ minutes,
plus builds/comparison time. `--case` restricts a CLI run. Captures, neighboring
frames, logs, exact RGB difference BMPs and JSON metrics are retained.

The no-op-by-default `HALO_RENDER_REGRESSION=1` test hook fixes the hosted game's
seed and pauses the menu world at simulation tick 30. Capture overrides disable
input/interpolation and use 1× anisotropy: the two APIs' 16× filters produce
substantial high-frequency texture differences. Normal defaults are unchanged.
Gameplay is timed idle, not a deterministic frame replay. The runner rejects
camera mismatches and rendering failures; it does not rescale, align or mask
images. Alpha is excluded because the game uses it as scratch state.

The full runtime unittest passed all nine cases with Metal API Validation
and unchanged tolerances: mean RGB error at most 3/255 and at most 3% of pixels
with any RGB-channel error above 16/255. The final run measured menu mean
0.004/255; gameplay/feature means 0.49–1.57/255 and bad-pixel fractions
0.69–2.60%. Native captures were 1280×960, SSAA 2560×1920. Windowless production
post-pass tests additionally check edge filtering, flat colors, exact alpha,
resize and split-view bounds. MSAA resolve tests cover retained samples;
lighting tests compare all 67 real programs and mutated microcode against the
OpenGL recognizer under sanitizers, and the production GPU equation against
CPU reference values.

### Remaining manual checks

- Judge AA quality, shimmering, thin geometry, translucent edges and model
  lighting in motion, on additional maps and split-screen layouts.
- Exercise fullscreen/resizing, live vsync/frame-limit changes, ProMotion and
  longer play sessions. Hidden captures do not measure presentation smoothness.
- Test Intel Mac runtime behavior and sample-count limits on other GPUs.
- Forge/platform meshes and the deferred screenshot/UI enhancements are still
  gaps, not features validated by this matrix.

Sources in `../visr/` preserve the earlier unresolved merge checkpoint and are
**not build inputs**. Active frontend sources are in `renderer/`; the backend
and SDL/native boundary are `gpu_metal.m` and `metal_host.m`.
