# Windows

`ninja windows` compiles the game with clang for 32-bit x86 Windows. The
result is a native executable, `build/windows/chupathingy.exe`, with `SDL3.dll`
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
- `llvm-rc` (LLVM's resource compiler, with the LLVM above) in the `PATH`.
  It puts the icon and the version information of `port/windows/halo.rc`
  in the executable. `python tools/windows_icon.py` makes the icon,
  `port/windows/halo.ico`, from `port/art/icon.svg` (needs `rsvg-convert`);
  it is committed, so a build does not need it.
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
`python configure.py --portable`. Refer to the build options in
[docs/building.md](../../docs/building.md).

For a new optimization profile (`--pgo=train`), the build compiles the
profile runtime of LLVM for 32-bit x86 (`pgo/halo_profile_runtime.c`).
LLVM for Windows supplies this runtime only for x86-64.

### 64-bit

`ninja windows64` builds the same game as native x64 code,
`build/windows64/chupathingy.exe`, with `SDL3.dll` for x64 next to it. It needs the
x64 MSVC libraries of Visual Studio instead of the x86 ones.

It is the 64-bit build of the other systems (`HALO_64BIT`: see
[port/macos/README.md](../macos/README.md)), with these differences:

- Windows keeps `long` 32 bits wide on x64, as the Xbox compiler did. The
  game sources need none of the `long` rewrite of the Linux and macOS
  64-bit builds (`tools/lp64_rewrite.py`).
- The platform layer reserves the 4 GB Xbox address space at 1 TB with
  `VirtualAlloc`, through the `mmap` of `src/win32_posix.c`. The game stops
  if it cannot get that address. The executable does not use high-entropy
  address randomization, which could put the first allocations of the
  process in that range.
- The write tracking and the crash reports of `src/win32_memory_watch.c`
  convert between Xbox addresses and the pointers of the process. A crash
  report gives the address of `chupathingy.exe` in that run, and the calls come
  from the unwind data of x64 code, not from a chain of frame pointers.
- It is not optimized with a profile. The committed profiles are those of
  the 32-bit builds.

Like the 32-bit build, it plays the Custom Edition maps of Halo PC
(`maps_ce/`), HaloMD's maps (`maps_md/`) and Halo PC's own (`maps_pc/`). It plays with the 32-bit builds
and the other ports over the network, and it is a dedicated server too
(`server/README.md`). Its releases are a separate download,
`chupathingyce-windows64-release.zip`, which its self-updater asks for.

## Build the game with Visual Studio

Visual Studio can build the game with no other tool: no Python, no ninja
and no separate LLVM.

1. In the Visual Studio Installer, add the workload "Desktop development
   with C++" and the component "C++ Clang tools for Windows". A Windows 10
   or 11 SDK is part of the workload.
2. Open `projects\windows\halo.sln`.
3. Select the configuration `Release` and the platform `x86` or `x64`.
4. Build the solution.

| Platform | Result | Contents |
| --- | --- | --- |
| `x86` | `build\windows\Release\halo.exe` | The 32-bit game, as `ninja windows`. It plays the Xbox maps. |
| `x64` | `build\windows64\Release\halo.exe` | The 64-bit game, as `ninja windows64` (refer to "64-bit" above). It also plays the Custom Edition maps and the HaloMD maps. |

`SDL3.dll` is next to each `halo.exe`. The first build needs a network
connection: it downloads the Visual C++ development package of SDL to
`build\windows\third_party` and compares its SHA-256 hash with the hash in
the project. The package has the libraries of the two platforms. To use a
package that is on the computer, set the environment variable `HALO_SDL3`
to its folder.

For the `x64` game, put the Custom Edition maps in `maps\ce` and the HaloMD
maps in `md_maps`, in the data root (the folder that contains `maps\`).
Refer to "Halo PC maps" in [README.md](../../README.md#halo-pc-maps). The
two games play together over the network on the Xbox maps.

The build also puts `bake-navigation.cmd` next to `halo.exe`. It makes the
navigation data of the bots for all the multiplayer maps in the `maps`
folder, before a game: refer to
[Baked navigation](../linux/README.md#baked-navigation).

| Configuration | Contents |
| --- | --- |
| `Release` | Optimized (`-O2`), as the ninja build with `--portable --lto=off --pgo=off`. |
| `Debug` | Not optimized, for the debugger. |

The debugger starts the game in the folder of `halo.exe`. Put the game data
there (`build\windows\Release\maps`, or `build\windows64\Release\maps`),
or give its folder in `paths.data` in the `config.toml` next to `halo.exe`.
One data folder can serve the two games.

The build has the game browser, the game list and the dedicated server, as
the ninja builds have them. Its version is the version of the `VERSION` file
with `-dev`, and it does not look for updates. To start a dedicated server,
refer to [docs/dedicated-server.md](../../docs/dedicated-server.md).

The project compiles the same sources with the same flags as
`ninja windows` (platform `x86`) and `ninja windows64` (platform `x64`),
through the `ClangCL` toolset (`clang-cl`). The `x64` platform does not
compile libtiff, as the other 64-bit builds:

- `tools/visual_studio.py` writes `projects\windows\halo.sln`,
  `halo.vcxproj` and `halo.vcxproj.filters`, and the files that the ninja
  build generates from the sources: `generated\halo_msvc_tags.h` and
  `generated\inline_exports`. All these files are in the repository. When
  you add, remove or rename a source file, enter
  `python tools/visual_studio.py` and commit the result.
- Before the build compiles, `tools\prebuild.ps1` (PowerShell) embeds the
  assets of `port\assets` in `build\windows\generated\hud_hires_assets.c`
  and the fonts of `port\linux\ui\fonts` in `ui_fonts.c`, writes the
  version to `halo_version.h`, and downloads SDL. These files are the same
  for the two platforms.

## Start the game

Enter `build\windows\chupathingy.exe`. The small `halo.exe` beside it starts
`chupathingy.exe`, for shortcuts made before the rename in 0.9.0.

The game finds the game data as on Linux. Refer to "Start the game" in
[port/linux/README.md](../linux/README.md#start-the-game).

| Item | Location |
| --- | --- |
| Settings | `config.toml` next to `chupathingy.exe` |
| Saved games | `%APPDATA%\ChupathingyCE`, or `paths.saves` in `config.toml`. The first start copies the saves from `%APPDATA%\halo`, which earlier versions used, and leaves that folder as it was |
| Log | `logs\debug.txt` in the data root (the folder that contains `maps\`), or `paths.logs` |
| Log of the port | The console. The release build has no console: `halo.log` in the same `logs\` folder |
| Crash reports | `logs\crashes\`, until the game sends them |

Before 0.8.0, `debug.txt` was in the data root itself, and `halo.log` and
`crashes\` were next to the program (`halo.exe` then). The game leaves the old files where
they are; crash reports still waiting there are sent from there.

## How the port operates

The game is 32-bit code, as on Linux, because its data contains 32-bit
pointers (the 64-bit build keeps that data in a 4 GB Xbox address space:
see "64-bit" above). The clang target `i686-pc-windows-msvc` gives the ABI of MSVC:
the structure layout, the 16-bit `wchar_t` and the calling conventions.
Thus the Windows build needs fewer changes than the Linux build.

The executable is large-address-aware, because the platform layer reserves
the Xbox memory at `0x80000000`.

The game plays the Custom Edition maps of Halo PC (`maps_ce/`), HaloMD's
maps (`maps_md/`) and Halo PC's own (`maps_pc/`), as the Linux build does (`HALO_CUSTOM_EDITION`). Their
tags are linked to `0x40440000`, outside the Xbox memory. When the game
starts, the platform layer takes those 23 MB at that address
(`port/linux/src/xbox_memory.c`, with `VirtualAlloc`). The executable is
at `0x00400000` and the system libraries are near the top of the lower
2 GB, so the address is normally free. If it is not (`cannot reserve Custom
Edition maps' tag cache` in the log), the game does not open those maps,
and the Xbox maps play as usual.

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
| `src/win32_memory_watch.c` | The write tracking of textures, with a vectored exception handler. |
| `src/win32_crash.c` | The crash reports. Refer to "Crash reports". |

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

## Crash reports

A crash writes the faulting address and the calls that led to it to
`debug.txt` and to the log. Releases and nightlies also send a crash report
to the ChupathingyCE site (`network.browser_url`, `POST /v1/crash`), which
groups the crashes and tells the developers:

1. The game starts a second copy of `chupathingy.exe` (`chupathingy.exe --crash-report`).
   This copy writes a minidump of the game to `logs\crashes\` (until
   0.8.0, `crashes\` next to the program), and walks the stack of the thread that crashed. Then the
   game stops. A minidump contains the stacks and the registers of the
   threads, and the list of the modules. It does not contain the other
   memory of the game.
2. At the first crash, the copy asks the player whether to send crash
   reports. `crash_reports.upload` in `config.toml` keeps the answer:
   `"yes"` sends this report and all the reports after it, and `"no"` sends
   no reports and does not ask again. The answer `"no"` deletes the report.
3. The copy sends the report, then deletes it. If it cannot send it (for
   example, with no network connection), the game sends it when it starts
   the next time (`chupathingy.exe --crash-upload`, in the background).
   `logs\crashes\` keeps at most 8 reports.

A report is JSON (`port/linux/src/crash_report.h`). It contains:

- The version, the channel and the commit of the build, the platform and
  the architecture, and the version of Windows (and of Wine, under Wine or
  Proton).
- The exception code, and up to 32 calls that led to it. Each call is a
  module and an offset in it, for example `chupathingy.exe+0x1a2b3c`.
- The last 200 lines of `halo.log`. The game replaces each public IP
  address in them with `[address]` before it sends them.
- The minidump, if it is 1 MB or smaller.

A report contains no player names other than the names in the log, and no
hardware IDs.

Each release has the PDBs as `chupathingyce-windows-<configuration>-symbols.zip` (private, for the maintainers).
`tools/symbolize_crash.py` uses them to add the function names and the
source lines to the crash lines of a `debug.txt` or a `halo.log`, and to
the calls of a report (`chupathingy.exe+0x...`):

```
python tools/symbolize_crash.py debug.txt chupathingy.exe
```

Give the `chupathingy.exe` of the build that crashed, with its `chupathingy.pdb` next to
it. The tool needs `llvm-symbolizer` (LLVM).

Local builds and the other builds of the `dev` and `custom` channels send
no crash reports. To test the crash reports with such a build, set the
`HALO_CRASH_REPORTS_ANY_BUILD` environment variable.

The crash reports also include the crashes of `abort()` and of an invalid
argument to a function of the C runtime.

## Limits

- The movies play through FFmpeg's Bink decoder
  (`port/linux/src/bink_ffmpeg.c`), built for Bink only and linked
  statically with the same clang (`tools/ffmpeg_build.py`, under MSYS2's
  shell); `configure.py --without-ffmpeg` leaves it out and the game skips
  the movies. The disc extraction at the first start copies the disc's
  `bink/` movies (only the ones that are missing) next to `maps\`.
- The game uses only the ANSI file functions. A path to the data or the
  saved games with characters that are not in the code page of the system
  does not always operate.
