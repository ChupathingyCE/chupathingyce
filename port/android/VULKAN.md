# The Vulkan renderer

The Android app can draw the game with Vulkan instead of OpenGL ES. It is
opt-in: OpenGL ES stays the default, and the player turns Vulkan on in
`config.toml`. The renderer comes from the halo-ce-universal fork, where it was
built in phases and tested on an Adreno 750 (Lenovo TB321FU) on the phone's
own driver and on Turnip; the fork's `port/android/VULKAN.md` keeps the plan
and the measurements behind it.

## Turning it on

In `config.toml`, in the `[display]` section:

| Setting | Meaning |
| --- | --- |
| `renderer = "gl"` | OpenGL ES, the default |
| `renderer = "vulkan"` | Vulkan; OpenGL ES if Vulkan cannot start |
| `vk_driver = ""` | the phone's own Vulkan driver, the default |
| `vk_driver = "auto"` | Turnip on an Adreno GPU, downloaded by the launcher before the game starts |
| `vk_driver = "<name>.zip"` | an adrenotools driver archive left in the game's folder |

Both take effect when the game starts. The log (`adb logcat -s halo`) has one
line, `renderer: ...`, that says what runs and, when Vulkan was asked for but
did not start, why. `debug.vk_validation` turns on the validation layer, when the
APK was configured with `--android-vulkan-validation`; `debug.vk_present_marker`
and `debug.vk_self_test` are diagnostics (their help is in `config.toml`).

## How it is built

The Android build makes two guest images from the same game objects:

- `halo_guest.elf`, with `port/linux/src/d3d8_gl.c` (OpenGL ES);
- `halo_guest_vk.elf`, with `guest/d3d8_vk.c` and `guest/xbox_textures_vk.c` in
  their place, and the Vulkan shader generators `nv2a_vsh_vk.c` and
  `nv2a_psh_vk.c`.

The host (`host/host_vk*.c`) brings up Vulkan before the game starts
(`host_vk_startup`), and `host_main.c` loads the Vulkan image if that worked and
the GL image if it did not. The guest sends the host its work as a command
stream (`guest/vk_commands.h`), copying each draw's data out of the game's
memory when it is made, so the host never reads guest memory behind the
game's back. Shaders are written as GLSL by the guest and compiled by glslang,
which the host loads with `dlopen` (`libhalo_glslang.so`) and caches as SPIR-V.

Third-party code, fetched at configure time (`tools/android_build.py`):
glslang (Khronos, 16.6.0) and libadrenotools (Eden's fork, pinned, with
`vulkan/adrenotools.patch`), which loads the Turnip driver.

## Not here from the fork

The fork's Vulkan probe and its device reports, its GL-call profiler, its
memory-window move (the Vulkan files read the window through
`halo_port_window.h`, which here is the fixed 0x80000000 one), the menu rows
for the renderer and the driver, and the Switch port.
