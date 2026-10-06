# Feature parity across builds

The owner's rule: every build gives players the same experience wherever
that is technically possible. Where builds differ, the difference must be
chosen on purpose and written down here.

This audit was made on 2026-10-06 against `sync-network-20` (02fee99f).
Delta was read on `release-0.6.8b`. File:line references are to this
branch unless another branch is named. Each difference is marked:

- **(a) required**: something forces it (an OS API is missing, memory, the
  GPU, the form factor).
- **(b) accidental**: nobody decided it.
- **(c) chosen**: a deliberate product decision, with the reason recorded.

Accidental differences fixed on this branch are marked **fixed** with
their commit's subject. Every other accidental one is listed under
"Left to schedule" at the end.

## How builds get their features

- Every client compiles all of `port/linux/src/*.c`, apart from a few
  units each build replaces:
  - Windows: `port/windows/port.json` "replaced_platform_sources".
  - Android: the `posix_*` units go to the arm64 host instead (`tools/android_build.py:470-477`, `:581-610`).
  - macOS: `bink_null.c` in a non-portable build (`port/macos/port.json:3-5`, `tools/macos_build.py:135-141`).
  - The server: `sdl_platform.c`, `xinput_sdl.c` and `updater.c` (`tools/server_build.py:68-72`).
- Features are therefore switched by `#if` and at runtime, not by build
  lists.
- `HALO_GAME_BROWSER` is on in every build (`configure.py:40-46`, default
  on; the server forces it on at `tools/server_build.py:239`).
- `HALO_64BIT` is set for Windows x64, Linux x64, macOS and the 64-bit
  servers (`tools/windows_build.py:221`, `tools/lp64_build.py:73`).
- `HALO_CUSTOM_EDITION` is set for Linux x86 (`tools/linux_build.py:373`),
  Linux x64, macOS and the servers (`tools/lp64_build.py:75`), and Windows
  x64 (`tools/windows_build.py:223`). It is not set for Windows x86 or
  Android.
- In `port_config.c`, the `_platform_*` flags (`:51-58`, chosen at
  `:496-502`) only decide which settings a build writes into its
  config.toml. Every build can read every setting.
- Steam Deck runs the Linux x64 build under gamescope (or the Windows build
  under Proton). The only Deck-specific code is the gamescope fullscreen
  check (`port/linux/src/sdl_platform.c:502-525`).

## Matrix

Abbreviations:
- **W32** Windows x86, **W64** Windows x64
- **L32** Linux x86, **L64** Linux x64
- **Mac** macOS (the universal application the downloads carry)
- **And** Android
- **Deck** Linux x64 under gamescope
- **Srv** the dedicated server

| Feature | W32 | W64 | L32 | L64 | Mac | And | Deck | Srv |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Game list, Online Games, server browser | yes | yes | yes | yes | yes | yes | yes | yes (no UI: `browser.c:1580`) |
| halo:// links registered | yes (registry, `win32_p2p.c:59-83`) | yes | yes (.desktop, `posix_net.c:846+`) | yes | yes (Info.plist) | `halo://join` only (a) | yes | no (fixed) |
| `halo://key` links (Link Profile) | yes | yes | yes | yes | yes | **no (b)** | yes | n/a |
| Invite arriving before internet play starts | kept and joined (fixed) | yes | kept and joined (fixed) | yes | yes | kept and joined (fixed) | yes | n/a |
| Discord invites and presence | yes, "In Menus" (fixed) | yes | yes, "In Menus" (fixed) | yes | yes | no (a) | yes | no Discord |
| Delta Peer + legacy table (release-0.6.8b) | yes | yes | yes | yes | yes | yes, but said "Linux" (fixed on parity-delta) | yes; the Windows build said "Windows" (fixed on parity-delta) | yes |
| Hardware id (host bans) | yes | yes | yes | yes | **none** before (fixed) | yes | yes | yes |
| Halo PC / HaloMD maps | **no (b)** | yes | yes | yes | yes | no (a, memory) | yes | yes |
| PC menus or Xbox menus (`display.menus`) | both | both | both | both | both | both; Quit does nothing (a) | both | n/a |
| Port settings screens (Video, Mouse...) | PC menus only (c) | same | same | same | same | same; Mouse Settings shown (b) | same | n/a |
| High-res HUD and text | yes | yes | yes | yes | yes | yes, at 480 lines (a) | yes | n/a |
| FXAA / SMAA | yes | yes | yes | yes | **no** before (fixed) | FXAA; SMAA gives FXAA (a, GPU) | yes | n/a |
| SSAA 2x | yes | yes | yes | yes | yes | no (a, GPU) | yes | n/a |
| MSAA | yes | yes | yes | yes | yes | yes, no alpha sample mask (a, ES 3.2) | yes | n/a |
| Per-pixel lighting, shadows, reverb | yes | yes | yes | yes | yes | yes | yes | no sound |
| Window modes, resolution, scaling, max_fps | yes | yes | yes | yes | yes, starts windowed (c) | fullscreen, `screen_width` (a) | yes | n/a |
| V-Sync changed in Settings | yes | yes | yes | yes | needed a restart before (fixed) | yes | yes | n/a |
| Bink movies (intro, attract, credits) | no | no | no | no | no in downloads; yes when built on a Mac with FFmpeg (c) | no | no | n/a |
| Self-updater | yes | yes | yes | yes | **no (b)** | yes (Java) | yes | no (c, deploy scripts) |
| Version reported (title, stats) | yes | yes | yes | yes | yes | "dev" on sync-network-20; fixed on release-0.6.8b by e690685f ("Logs: say which ChupathingyCE build is running") | yes | yes |
| Stats reports to the site | yes | yes | yes | yes | yes | yes | yes | yes |
| Link Profile (QR code, link) | yes | yes | yes | yes | yes | yes, no key links (b, above) | yes | n/a |
| Platform icons | none anywhere yet | | | | | | | |
| Split screen (4 ports) | yes | yes | yes | yes | yes | yes; touch drives port 0 (a) | yes | n/a |
| Online co-op | yes | yes | yes | yes | yes | yes | yes | no, hosted from the game (c) |
| Touch controls | no | no | no | no | no | yes (a) | SDL touch-to-mouse (a) | n/a |
| Gamepads, rumble | yes | yes | yes | yes | yes | yes, plus phone vibration | yes | n/a |
| Gyro aiming | no | no | no | no | no | phone gyro with touch controls | **no (b)** | n/a |
| Telnet script console | yes | yes | yes | yes | yes | yes, device-local | yes | yes |
| `sv_*` console, control API, web admin | no | no | no | no | no | no | no | yes (c) |
| `screenshot_count` (TIFF) | yes | **no (b)** | yes | **no (b)** | **no (b)** | yes | **no (b)** | n/a |
| Debug frame dumps (`debug.screenshot_*`) | yes | yes | yes | yes | yes | yes | yes | n/a |
| Crash reports | local, and Sentry upload switched off (b) | same, symbols now kept (fixed) | local log | local log | local log | logcat | local log | backtrace |
| mesa_glthread hint | no (a: Mesa is rare on Windows) | no | yes | yes | yes (ignored) | no | yes | n/a |

### Settings that exist only on some builds

From the `_platform_*` flags in `port_config.c`:

- **Desktop only.** `display.fullscreen`, `.mode`, `.resolution`,
  `.resolution_scaling`, `.window_size`, `.window_scale`, `.max_fps`,
  `.direct_camera`; `input.mouse_*`; `paths.*`; `discord.application_id`;
  `debug.update_answer`, `.hidden_window`, `.gpu_flush_draws`
  (`:90-130`, `:207-217`, `:276-280`, `:376`, `:446-460`).
  - Required: Android has no window, no pointer and no Discord IPC
    (`posix_net.c:916-919`).
  - Accidental: Android's PC menus still offer Mouse Settings (see below).
- **Android only.** `display.screen_width`, `input.touch_controls`,
  `debug.sample_seconds` (`:113`, `:202`, `:489`). Required.
- **Windows only.** `crash_reports.upload` (`:384`). Uploading is
  Windows-only because only `win32_crash.c` exists. See "Left to schedule".
- **Different defaults.** These are chosen, and the reason is in a comment:
  - macOS: `display.fullscreen` is false (`:72-77`) and
    `audio.buffer_frames` is 2048 (`:79-86`).
  - Android: the anti-aliasing values map to cheaper modes
    (`d3d8_gl.c:116-132`).
- `update.auto` is written on macOS, where nothing reads it, because macOS
  has no updater.

## Fixed on branch `parity` (from sync-network-20)

| Commit | Difference | Class | Verification |
| --- | --- | --- | --- |
| macOS: FXAA and SMAA build their programs in GLSL 4.10 | `xgpu_post.c` asked for `#version 450`. macOS GL 4.1 rejects it, so FXAA and SMAA silently did nothing. | (b) | The real post shaders were compiled on this Mac's GL 4.1 context: all 5 fail with 450 ("version '450' is not supported") and all 5 compile with 410. Built with `ninja macos`. |
| macOS: V-Sync changed in Settings takes effect | `platform_display_apply` set SDL's swap interval, which macOS keeps off (`macos_video.c`), so a change only took effect at the next start. | (b) | `ninja macos`; the Windows units compile. |
| Internet play: one way for an invite given before it starts, on every build | The 32-bit builds (W32, L32, Android) dropped an invite that arrived before p2p started, then logged "Internet play is off". The `HALO_64BIT` gates came from the macOS port's 32-bit neutrality work, not from a decision. | (b) | L32 and L64 were each started with a `halo://join/...` argument and each logged "joining ...'s game". Built for macOS, Linux x86 and x64, servers and Android; compiles for W32 and W64. |
| Discord: "In Menus" on every build | Only 64-bit builds sent the menu presence. | (b) | Same builds as above. |
| macOS: a hardware id, as the other builds have | macOS has no `/etc/machine-id`, so Macs sent an empty hardware id and host hardware bans could not hold them. Now taken from the hardware UUID via `gethostuuid`. | (b) | `ninja macos`; a standalone check reads a 36-character UUID; the binary imports `gethostuuid`. |
| Dedicated server: halo:// links stay the game's | The server registered itself as the `halo://` handler. | (b) | On boogerflix-1 the old server-x64 wrote `halo-ce-universal-halo.desktop` into its HOME; the new one writes nothing. |
| Builds: Mbed TLS's license in every download | Only the Linux zips carried it, though every build links Mbed TLS. | (b) | Python parses. CI packaging proves it. |
| Builds: the 64-bit Windows build's symbols | Only W32 kept `halo.pdb` and `SDL3.pdb`. | (b) | Python parses. CI proves it. |
| Docs: the game list is in every build; the macOS downloads skip movies | Stale `configure.py` help and `linux_build.py` text; `port/macos/README.md` claimed the Mac plays movies. | (b) | `configure.py --help`. |

On branch `parity-delta`, two commits on top of `release-0.6.8b`
(9fa5fec1), because the Delta files exist only there:

| Commit | Difference | Verification |
| --- | --- | --- |
| Delta Peer: the Android game says it is Android | `local_platform()` tested `__ANDROID__`, which the guest never defines, so Android reported `pc_linux` with Linux's memory class. | Guest unit built; macOS built. |
| Delta Peer: the Windows build on a Steam Deck says it is a Deck | Only the Linux build read `SteamDeck=1`, so W32/W64 under Proton reported `pc_windows`. | Compiles for W32, W64, macOS and the Android guest. |

On `release-0.6.8b`, the Delta sources are compiled into every client and
the server. No build script names them and none excludes them.
`delta_legacy_start()` runs in `sdl_platform.c` and `server_platform.c`.

## Left to schedule (accidental, not small)

1. **Halo PC / HaloMD maps on Windows x86.** Being handled on branch
   `ce-everywhere` (the other worker), together with Android. Not touched
   here.
2. **The macOS self-updater** (`updater.c:53-57`). Replacing a signed
   `.app` bundle (directory swap, quarantine attribute, codesign) differs
   from the flat-file `.old` scheme. About 1-2 days with testing. It should
   follow the update-signing work in `docs/builds.md`.
3. **Console screenshots on 64-bit builds.** `screenshot_count` uses
   libtiff, which the LP64 builds leave out (`source/bitmaps/tiff_file.c`;
   `port/linux/port.json` "lp64"). Writing the frame with stb_image_write
   (already in `port/third_party/stb`) as PNG on every build fixes it.
   About half a day. It changes the 32-bit output format too, so the owner
   should decide.
4. **Android `halo://key` links.** Two pieces are needed:
   - The manifest filter accepts `host="join"` only.
   - `poll_invite_file` passes the text to `p2p_invite_received`, which
     never reaches `browser_key_link` (`p2p.c`, `poll_invite_file`). The
     fix needs care with `p2p_lock`.

   About half a day, and it needs an on-device test.
5. **Windows crash uploads are off in every build.** `crash_reports_enabled`
   needs `HALO_BUILD_NUMBER > 0` (`win32_crash.c:734`), and no build has
   defined it since versions replaced build numbers (a687c8ce). Turning
   uploads back on (keyed to `HALO_RELEASE_BUILD`) is a one-line change.
   It sends minidumps to Sentry, so it is the owner's call. Crash upload on
   Linux, macOS and Android does not exist. A Breakpad-style minidump on
   POSIX is about 2-4 days per OS.
6. **PC menus on Android.**
   - Mouse Settings and Controls Setup are shown although Android has no
     pointer (`tools/port_settings.py:87-100`; profile edit XML). Tagging
     the rows `platform="desktop"` is small. Controls Setup may still be
     wanted for keyboards, so the owner should decide.
   - The main menu's Quit does nothing on Android (`sdl_platform.c:1300`).
     It could hide or finish the activity: small to medium.
7. **Controller gyro** (DualSense, Switch, the Deck's own pad) on every
   desktop build. Only Android's phone gyro exists today. SDL3 sensor
   support plus a setting: about 1-2 days.
8. **Android renders at 480 lines** (`d3d8_gl.c:176-189`, scale 1.0). The
   comment states it but gives no measured cost. Raising it is a
   performance question for the Android worker: medium.
9. **Bink movies everywhere except a Mac with Homebrew.** A bundled decoder
   (FFmpeg's Bink decoder alone, or a small Bink 1 decoder) for every build
   would restore the intro, attract and credits movies. About 3-5 days,
   plus licensing review. Today the downloads all behave the same way
   (none play movies), so this is parity already, only a missing feature.

## Chosen differences (keep, documented)

- **No Discord on Android.** There is no local Discord IPC to talk to.
- **Touch controls and phone gyro on Android only.**
- **Android's anti-aliasing and memory limits.** The phone GPU and the
  128 MB guest decide them (`halo_port_capacity.h`, `d3d8_gl.c:116-132`).
- **macOS starts windowed and uses larger audio buffers.** Both follow
  macOS conventions and an audio dropout fix.
- **The server has no updater, UI or sound, and skips campaign levels.**
  Its `sv_*` console, control API and web page belong to the server alone.
- **Port settings screens are in the PC menus only.** The Xbox menus stay
  retail.
