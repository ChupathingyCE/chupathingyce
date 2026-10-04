# Windows

`ninja windows` compiles the game with clang for 32-bit x86 Windows. The
result is a native executable, `build/windows/halo.exe`, with `SDL3.dll`
next to it. You must build on Windows.

The Windows build uses the platform layer of the Linux build
(`port/linux/src`). The graphics, the sound, the input, the settings and
the multiplayer functions are the same as on Linux. Refer to
[port/linux/README.md](../linux/README.md).

## Requirements

You do not need the Xbox SDK.

- Visual Studio or the Visual Studio Build Tools, with the C++ workload.
  The build uses the 32-bit (x86) MSVC libraries and a Windows 10 or 11
  SDK. It does not use the MSVC compiler.
- LLVM (clang and lld), Python 3 and ninja in the `PATH`. For example,
  enter `scoop install llvm python ninja`.
- A network connection for the first build. `configure.py` downloads the
  Visual C++ development package of SDL 3.4.16 to
  `build/windows/third_party`.

## Build the game

1. Go to the root folder of the repository.
2. Enter `python configure.py`.
3. Enter `ninja windows`.

`configure.py` generates the Windows build only on Windows.

The build uses all the instructions of the processor of the computer that
builds it (`-march=native`). Such a build does not always start on a
different computer. To make a build for other computers, enter
`python configure.py --portable`. Refer to the build options in the main
[README](../../README.md#build-options).

For a new optimization profile (`--pgo=train`), the build compiles the
profile runtime of LLVM for 32-bit x86 (`pgo/halo_profile_runtime.c`).
LLVM for Windows supplies this runtime only for x86-64.

## Build the game with Visual Studio

Visual Studio can build the game with no other tool: no Python, no ninja
and no separate LLVM.

1. In the Visual Studio Installer, add the workload "Desktop development
   with C++" and the component "C++ Clang tools for Windows". A Windows 10
   or 11 SDK is part of the workload.
2. Open `projects\windows\halo.sln`.
3. Select the configuration `Release` and the platform `x86`.
4. Build the solution.

The result is `build\windows\Release\halo.exe`, with `SDL3.dll` next to
it. The first build needs a network connection: it downloads the Visual C++
development package of SDL to `build\windows\third_party`. To use a
package that is on the computer, set the environment variable `HALO_SDL3`
to its folder.

The build also puts `bake-navigation.cmd` next to `halo.exe`. It makes the
navigation data of the bots for all the multiplayer maps in the `maps`
folder, before a game: refer to
[Baked navigation](../linux/README.md#baked-navigation).

| Configuration | Contents |
| --- | --- |
| `Release` | Optimized (`-O2`), as `ninja windows` with `--portable --lto=off --pgo=off`. |
| `Debug` | Not optimized, for the debugger. |

The debugger starts the game in the folder of `halo.exe`. Put the game data
there (`build\windows\Release\maps`), or give its folder in `paths.data` in
the `config.toml` next to `halo.exe`.

The build has the game browser, the game list and the dedicated server, as
`ninja windows` has them. Its version is the version of the `VERSION` file
with `-dev`, and it does not look for updates. To start a dedicated server,
refer to [docs/dedicated-server.md](../../docs/dedicated-server.md).

The project compiles the same sources with the same flags as
`ninja windows`, through the `ClangCL` toolset (`clang-cl`):

- `tools/visual_studio.py` writes `projects\windows\halo.sln`,
  `halo.vcxproj` and `halo.vcxproj.filters`, and the files that the ninja
  build generates from the sources: `generated\halo_msvc_tags.h` and
  `generated\inline_exports`. All these files are in the repository. When
  you add, remove or rename a source file, enter
  `python tools/visual_studio.py` and commit the result.
- Before the build compiles, `tools\prebuild.ps1` (PowerShell) embeds the
  assets of `port\assets` in `build\windows\generated\hud_hires_assets.c`
  and the fonts of `port\linux\ui\fonts` in `ui_fonts.c`, writes the
  version to `halo_version.h`, and downloads SDL.

## Start the game

Enter `build\windows\halo.exe`.

The game finds the game data as on Linux. Refer to "Start the game" in
[port/linux/README.md](../linux/README.md#start-the-game).

| Item | Location |
| --- | --- |
| Settings | `config.toml` next to `halo.exe` |
| Saved games | `%APPDATA%\halo`, or `paths.saves` in `config.toml` |
| Log | `debug.txt` in the data root (the folder that contains `maps\`) |

## How the port operates

The game is 32-bit code, as on Linux, because its data contains 32-bit
pointers. The clang target `i686-pc-windows-msvc` gives the ABI of MSVC:
the structure layout, the 16-bit `wchar_t` and the calling conventions.
Thus the Windows build needs fewer changes than the Linux build.

The executable is large-address-aware, because the platform layer reserves
the Xbox memory at `0x80000000`.

### Headers

- The game and the platform layer use the Xbox SDK declarations of
  `port/include/xdk`, not the Windows SDK declarations with the same names.
  The compiler reads `port/include/xdk` before the Windows SDK.
- The C runtime is the static UCRT of Windows.
- `include/halo_windows_prefix.h` is the first header of each file. It gives
  Windows names to the Xbox SDK functions of the platform layer (for example
  `CreateFileA`, `ReadFile`, `Sleep`). Thus the code for Windows gets to
  Windows. The list of names is in `include/halo_windows_api_names.h`.
- `include/crt` changes some C runtime headers. The file functions of the
  game (`fopen`, `open`, `mkdir`) go through the translation of Xbox paths
  (`src/windows_crt.c`).
- `include/posix` declares the POSIX functions that the platform layer uses.

### Code for Windows

These files use only the Windows SDK:

| File | Contents |
| --- | --- |
| `src/win32_files.c`, `src/win32_net.c` | The file and socket functions of `port/linux/src/posix.h`. |
| `src/win32_posix.c` | The POSIX functions on Windows threads, critical sections, condition variables, `VirtualAlloc` and the performance counter. |
| `src/win32_memory_watch.c` | The write tracking of textures, with a vectored exception handler. It also writes reports of crashes. |

`port.json` gives the Linux files that these files replace, and the Windows
libraries of the link.

### Inline functions

MSVC makes a COMDAT copy of an external `__inline` function where it does
not inline a call. Some files call such functions through a prototype.
clang inlines more calls. Thus a function can have no copy. The build
prevents this:

- `port/linux/game/msvc_comdat.c` gives one weak definition of each header
  inline function.
- A wrapper takes the address of each `__inline` function in a `.c` file.
  Thus the compiler makes an external copy (`inline_export_wrapper` in
  `tools/windows_build.py`).

MSVC gives file scope to a structure tag in a prototype. clang does not.
Thus the build also includes the declarations of the Linux build
(`build/windows/halo_msvc_tags.h`).

## Limits

- Bink video is not available. The game skips the movies.
- The game uses only the ANSI file functions. A path to the data or the
  saved games with characters that are not in the code page of the system
  does not always operate.
