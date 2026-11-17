# The Vulkan renderer

The Android app can draw the game with Vulkan instead of OpenGL ES. It is
opt-in: OpenGL ES stays the default, and the player turns Vulkan on in
`config.toml`. The renderer is thelinkin3000's, from his halo-ce-universal
fork and [PR #152](https://github.com/ChupathingyCE/chupathingyce/pull/152),
where it was built in phases and tested on an Adreno 750 (Lenovo TB321FU) on
the phone's own driver and on Turnip. It was written for the app's guest build
(the game as a 32-bit image under a host library); the native 64-bit app has
it as a backend of the game's Direct3D 8 device, the way the desktop's bgfx
renderer is one.

## Turning it on

In `config.toml`, in the `[display]` section:

| Setting | Meaning |
| --- | --- |
| `renderer = "gl"` | OpenGL ES, the default |
| `renderer = "vulkan"` | Vulkan; OpenGL ES if Vulkan cannot start |
| `vk_driver = ""` | the phone's own Vulkan driver, the default |
| `vk_driver = "auto"` | Turnip on an Adreno GPU, downloaded by the launcher before the game starts |
| `vk_driver = "<name>.zip"` | one of the Turnip builds the app knows by name, in its driver folder |

A driver archive is code the game runs, so the app only loads the builds it
knows (`Updater.java`'s table, the same in `host_vk_driver.c`): the launcher
downloads them into the app's private storage (`getFilesDir()/vk_driver`,
which no other app can write, unlike the external files folder on Android 9
and 10), checks the size and SHA-256 of each download, and the game checks
them again before it unpacks an archive and at every load. An archive that is
not a known build, or not whole, is not loaded and the phone's own driver is
used.

When Vulkan cannot start, that start draws with OpenGL ES and the game asks
once whether to switch to OpenGL ES for good ("Switch to OpenGL" writes
`display.renderer = "gl"`; "Not now" leaves the setting, and the next start
tries Vulkan again). A start with a driver archive leaves a note
(`vk_start.txt` in the saves folder) until frames are drawn; a start that
finds the note runs on the phone's own driver and asks whether to keep it,
so a driver that crashes cannot crash every start.

Both take effect when the game starts. The log (`adb logcat -s chupathingyce`)
has a line, `renderer: ...`, that says what runs and, when Vulkan was asked
for but did not start, why; `renderer=vulkan` is the line crash reports carry.
`debug.vk_validation` turns on the validation layer, when the app was
configured with `--android-vulkan-validation`; `debug.vk_present_marker` and
`debug.vk_self_test` are diagnostics (their help is in `config.toml`).

## How it works

- `port/linux/src/d3d8_vk.inl`, included at the end of `d3d8_gl.c` and
  sharing the device's state, is the game's half: at each draw it writes one
  record of everything the draw needs (its pipeline state, the data it reads,
  copied out of the game's memory then and there, its textures and uniforms)
  into a command stream (`port/android/host/vk_commands.h`), with the clears,
  the render target changes, the visibility tests and the present. The
  stream is handed to the backend at the end of the frame, or sooner when it
  fills.
- `port/android/host/host_vk*.c` is the backend: the Vulkan instance and
  device, the driver loading (`host_vk_driver.c`, with libadrenotools for
  Turnip), the shader service (GLSL compiled to SPIR-V by glslang on a thread
  of its own, with a SPIR-V cache and a pipeline cache on disk), the upload
  rings the draws' data lands in, the images, the occlusion queries and the
  swapchain.
- `port/linux/src/nv2a_vsh_vk.c` and `nv2a_psh_vk.c` write the shaders'
  GLSL for glslang, as the OpenGL renderer's generators write it for the
  driver; their blocks and bindings are `port/android/host/vk_shaders.h`'s.
- The game's textures are decoded by `xbox_textures.c`'s helpers, as the bgfx
  renderer decodes them, and sent as images; BC1 to BC3 go as they are where
  the device samples them. The high-res HUD, the text's atlas and the menus'
  art are images of the same kind.

The window is made without an OpenGL ES context when Vulkan is chosen
(`sdl_platform.c`; Android lets one API own a window), and made again with
one when Vulkan does not start.

## Not there yet

- The Guide and the other overlays (`ui_overlay.c` draws with OpenGL or
  bgfx): the game logs `overlay: not drawn with the Vulkan renderer yet`.
- The anti-aliasing passes, screenshots and clips, which are the OpenGL
  renderer's.
- The shaders are the generators' as PR #152 left them: no per-pixel model
  lighting or Light effects, which `nv2a_vsh.c` and `nv2a_psh.c` gained
  since.
- Video Setup has no rows for it: `config.toml` only.

## How it is built

`tools/android64_build.py` compiles the backend into `libmain.so` with
`HALO_VULKAN`, and `tools/android_build.py` fetches and builds, with the
NDK's CMake toolchain, what it loads at run time, staged beside it in the
app's native library folder:

- glslang (Khronos, 16.6.0) as `libhalo_glslang.so`, loaded with `dlopen`
  (`port/android/vulkan/glslang`);
- libadrenotools (Eden's fork, pinned, with `port/android/vulkan/adrenotools.patch`)
  and its hooks, which load a Turnip driver (`host_vk_driver.c`).

The app's Gradle module keeps the native libraries uncompressed
(`useLegacyPackaging`), so that they can be loaded by their paths.

## Licenses

libadrenotools and its linkernsbypass submodule are BSD-2-Clause (Billy
Laws), glslang is BSD-3-Clause and others (Khronos), and Turnip, which the
app downloads only at the player's request, is Mesa's, under the MIT
license. Their texts are in `port/notices/` and their entries for the
notices list in `port/notices/components.json`.
