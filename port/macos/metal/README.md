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
- The normal game's vertex lighting is used. The local per-pixel lighting
  enhancement is not ported yet.
- Online Games, Link Profile, and other custom overlay screens render through
  native Metal. The server list remains visible when online play is disabled or
  its server is unreachable. Shapes, text, button glyphs and map-preview cutouts
  are composited into the back buffer before presentation and screenshots.
  With reduced render resolution, overlays scale with the back buffer too.
- MSAA/SMAA/FXAA post-processing and custom GL-based Forge selectors/platform
  meshes are not ported yet.
  Use the default OpenGL build if you need these features. This is not full
  renderer feature parity.
- High-resolution HUD/text art is supported, but HUD PNG decoding happens on
  first use rather than the old asynchronous preload worker.
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

- Built and bundled on Apple M2 Pro, including the default-off build path.
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
ninja macos-metal-smoke
build/macos/metal-smoke
```

The smoke-test target exists only with `--macos-metal`. It is separate from the
game and tests shader compilation, triangle rendering, and GPU readback.

Sources in `../visr/` preserve the earlier unresolved merge checkpoint and are
**not build inputs**. Active frontend sources are in `renderer/`; the backend
and SDL/native boundary are `gpu_metal.m` and `metal_host.m`.
