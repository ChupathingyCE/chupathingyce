# Metal compatibility backlog

This is the known backlog when promoting the Metal port, not a claim of an
exhaustive Xbox/D3D8 audit. The source of truth for source drift is
`port/macos/metal/manifest.json`, tracking ChupathingyCE's `port/linux/src`.
The PR targets **LopezNoah/chupathingyce:main**, not upstream main.

## Fixed in this PR: live HUD/title-art replacement invalidation

The original probe confirmed stale cached bindings in both directions for HUD
and title art. The cause was cache-hit paths reusing replacement decisions made
at upload time without checking whether high-resolution settings changed.

`hud_hires_settings()` is now implemented following upstream's settings-generation
contract (commit `3bfb63b17`). Stage/recent cache hits require that generation to
match. The slow path reselects replacements on a mismatch without invalidating
unaffected original uploads. Turning a replacement off uploads the original
bitmap if necessary: the high-resolution path may have skipped it entirely.
The missing-symbol exception has been removed. The broader source drift remains
acknowledged because this is a targeted fix, not full fork reconciliation.

Run from the repository root:

```sh
python3 port/macos/metal/tests/probe_live_hires.py
# Result: exit 0, all 12 asset/direction/cache-path cases pass.
python3 -m unittest discover -s port/macos/metal/tests -p 'test_metal_live_hires.py'
```

The regression is now part of the regular CI test suite. It compiles production
settings/selection, font-enable and texture-cache code with strict warnings and
ASan/UBSan. GPU uploads, map/tag data, CRC and configuration storage are controlled
providers. Each case starts in a fresh process, binds a texture, changes the
setting/configuration generation, and binds the same unchanged resource again.
There is no restart, map reload or guest write between binds.

| Asset/path | Transition | Stage cache | Recent cache | Bucket lookup |
| --- | --- | --- | --- | --- |
| HUD replacement | off → on | Pass | Pass | Pass |
| HUD replacement | on → off | Pass | Pass | Pass |
| Title-art replacement | off → on | Pass | Pass | Pass |
| Title-art replacement | on → off | Pass | Pass | Pass |

The probe also checks font-enable updates, restored settings, hires/mipmap
metadata, original bitmap contents after disabling a replacement, unchanged
bindings after unrelated config edits, and no uploads for unaffected textures
or repeated cache hits. Disabling an active replacement permits one original
bitmap upload; enabling it does not reupload original pixels.

This establishes CPU selection/cache behavior, not on-screen glyph/atlas
correctness or Settings-menu UI behavior. An on-screen confirmation remains a
validation task, not a known outstanding cache defect.

## Previously deferred user-facing features

- GL-based Forge selectors and custom platform meshes are not implemented by
  Metal. Use OpenGL for those; the manual Metal launcher disables the unsupported
  collision board rather than leaving an invisible obstacle.
- Screenshot hotkey support is deferred; debug BMP captures are available.
- Scoreboard mouse selection is deferred.
- The controller-icon enhancement from OpenCE was not imported.

## Semantic and contract limits

- Signed blend variants use ordinary add/reverse-subtract; the NV2A signed-add
  bias is dropped (`xbox_compat/xbox_state.c`). This is an existing approximation,
  not evidence of full signed-blend equivalence.
- Reporting covers audited state/format/resource conversions and selected Metal
  shader/pipeline/command failures, not every unsupported Xbox semantic,
  allocation failure or backend path. Known approximations are not all reported.
- Symbol comparison verifies exported names, not signatures, ABI layout or
  rendering equivalence. Documented GL-only symbol exceptions map to native
  Metal implementations; they are not themselves missing Metal features.
- The backend remains one translation unit because its state is tightly coupled.
  A deeper module split remains deferred. This is architectural debt, not a
  demonstrated rendering defect.

## Outstanding source reconciliation

The following 12 forks/shadow headers have acknowledged, unreconciled drift:

- `d3d8_gl.c` → `xbox_compat/xbox_device.c`
- `xbox_textures.c` → `xbox_compat/xbox_textures.c`
- `nv2a_vsh.c` → `shaders/nv2a_vertex.c`
- `nv2a_psh.c` → `shaders/nv2a_pixel.c`
- `sdl_platform.c`, `port_config.c`, `hud_hires.c`, `text_hires.c` → `platform/`
- `xgpu.h` → `xbox_compat/xgpu.h`
- `platform.h`, `hud_hires.h`, `text_hires.h` → `platform/`

These are review obligations, **not 12 confirmed rendering bugs**. Initial
historical baseline pins are content-history estimates, not reviewed merge
bases. `seen` acknowledges the backlog without marking it reconciled.

```sh
python3 tools/metal_manifest.py check
python3 tools/metal_manifest.py check --against upstream/main
```

The fetched upstream preview additionally identifies five unclassified sources
(`delta_chat.c`, `delta_chat_words.c`, `discord_activity.c`, `map_art.c`,
`save_folder.c`) and further changes to `sdl_platform.c`, `port_config.c` and
`ui_overlay.c`. No upstream merge or feature import was performed here; rerun the
preview after fetching because this list is a snapshot, not a permanent pin.

## Validation still needed

- Intel Mac runtime behavior (compilation is not runtime validation).
- Additional maps, shader/state combinations and split-screen layouts beyond
  the existing fixtures; exhaustive Xbox/D3D8 equivalence is not established.
- AA quality and lighting in motion: shimmering, thin geometry and translucent
  edges cannot be judged from static captures alone.
- Fullscreen/resize, live vsync/frame-limit changes, ProMotion and longer play
  sessions. Hidden/offscreen captures do not measure presentation smoothness.
- On-screen Settings-menu validation of live HUD/title/font changes after the
  cache invalidation fix.

VisionOS stereo, synthetic foveation, headset pacing and stereo warming are
outside ordinary macOS mono scope, not missing acceptance criteria for this PR.
