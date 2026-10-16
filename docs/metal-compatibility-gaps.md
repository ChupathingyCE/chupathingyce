# Metal compatibility backlog

This is the known backlog when promoting the Metal port, not a claim of an
exhaustive Xbox/D3D8 audit. The source of truth for source drift is
`port/macos/metal/manifest.json`, tracking ChupathingyCE's `port/linux/src`.
The PR targets **LopezNoah/chupathingyce:main**, not upstream main.

## Confirmed: live HUD/title-art replacement invalidation

`hud_hires_settings()` is missing from the Mac fork. Upstream commit
`3bfb63b17` uses it to invalidate cached replacement decisions when
`display.high_res_hud` or `display.high_res_text` changes. Metal currently
chooses a HUD/title-art replacement when uploading a texture but does not
reselect it on a content-cache hit after a settings change.

Reproduce from the repository root:

```sh
python3 port/macos/metal/tests/probe_live_hires.py
# Current result: exit 1, a confirmed known gap (not a compiler/test crash).
```

The standalone probe compiles production `hud_hires_override_find`,
`text_hires_enabled`/`text_enabled`, and texture-cache lookup/result functions
with `-Wall -Wextra -Werror` and ASan/UBSan. It reuses the existing texture-cache
fixture; GPU uploads, map/tag data, CRC and configuration storage are controlled
providers. Each case starts in a fresh process, binds a texture, changes the
setting/configuration generation, and binds the same unchanged resource again.
There is no restart, map reload, guest write or texture upload between binds.

| Asset/path | Transition | Selection sees new setting | Cached binding updates |
| --- | --- | --- | --- |
| HUD replacement | off → on | Pass | **Fail** |
| HUD replacement | on → off | Pass | **Fail** |
| Title-art replacement | off → on | Pass | **Fail** |
| Title-art replacement | on → off | Pass | **Fail** |
| Font text enable check | off → on | Pass | Not a replacement binding test |
| Font text enable check | on → off | Pass | Not a replacement binding test |

This narrows the earlier phrase “live text settings are missing”: font enable
checks do update; cached HUD/title-art replacement decisions do not. It does
**not** establish on-screen glyph/atlas correctness or exercise the Settings UI.
The probe intentionally remains separate from the passing unit suite and exits
nonzero until this gap is fixed; it does not encode stale behavior as success.

Follow-up acceptance: propagate a settings generation through cached replacement
selection, recheck enable/disable transitions on stage and global cache hits,
preserve unaffected cached uploads, and rerun the probe plus an on-screen
Settings-menu check. Remove the `hud_hires_settings` symbol exception only when
the missing contract is implemented. This PR records/tests the gap; it does not
claim to fix it.

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
