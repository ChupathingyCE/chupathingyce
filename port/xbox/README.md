# Original Xbox

`ninja xbox` builds the game for the console that it was written for:
`build/xbox/default.xbe`. The build uses clang and lld-link as
[OXDK](https://github.com/MrMilenko/OXDK) does, on macOS or Linux. It is the
game only: the platform layer of the other ports (`port/linux`) is not in it,
because the game's Xbox code talks to the Xbox SDK directly.

## Requirements

- Python, [ninja](https://ninja-build.org/) and LLVM (clang and lld-link).
  On macOS: `brew install llvm ninja`. The build looks for LLVM in `LLVM_DIR`
  and then in Homebrew's folder.
- [OXDK](https://github.com/MrMilenko/OXDK) in `~/OXDK` (or give
  `--oxdk DIR`, or set `OXDK_DIR`), with its `cxbe` built.
- The Xbox SDK's libraries and headers in OXDK's `xbox/xdk/lib` and
  `xbox/xdk/include`, as the OXDK instructions tell. This repository does not
  supply them. The SDK comes as a Windows installer that installs into
  Microsoft Visual Studio .NET; you must install it (and a Visual Studio that
  it accepts) on a Windows computer or virtual machine, and then copy the
  files. The build was made with the 5933 SDK.
- An Aug 2001 build of the Direct3D library, `d3d8ntpr.lib`. This repository
  does not supply it. Refer to "Direct3D".
- The PAL (European) disc's maps. Refer to "Game data".

## Build

```sh
python3 configure.py --xbox-d3d8 /path/to/d3d8ntpr.lib
ninja xbox
```

`configure.py` records the options, so later runs of ninja keep them. If
OXDK or the Direct3D library is not found, there is no `xbox` target.

## Game data

The game is build 01.01.14.2342, the same as the PAL release, and it does
not load cache files of a different build. Use the maps of the PAL disc
(`maps/*.map` and `maps/loading.tga`). The NTSC maps (01.10.12.2276) stop
the game with "the cache file 'ui' belongs to a different build". The PC
ports accept both.

Put the files on the console's hard disk in one folder, for example:

```
E:\Games\HaloBeta\default.xbe
E:\Games\HaloBeta\maps\ui.map
E:\Games\HaloBeta\maps\loading.tga
E:\Games\HaloBeta\maps\a10.map ...
```

`D:\` is the folder of the XBE when the game runs, and the game writes its
log to `D:\debug.txt`. Saved games and profiles go to the utility drive
(`Z:\`), as on the disc. The game is a debug build: an assertion stops it
and shows the error on the screen and in `debug.txt`.

## Direct3D

The game compiles against the January 2002 SDK's declarations
(`port/include/xdk`), as the other ports do, and links with a later SDK's
libraries. Direct3D is the exception. Later SDKs renumbered the render
states and texture stage states that the game sets, so the game needs a
Direct3D of its own time. The reconstructed `libs/d3d8` needs the January
SDK's headers, which are not available. The build therefore links
`d3d8ntpr.lib`, the Aug 2001 Direct3D that the kernel's boot animation
used. Its state numbers are the January SDK's, and its device has the
layout that `source/main/d3d_intimacy.cpp` reads. `port/xbox/src/
xbox_support.cpp` supplies the memory functions that the kernel gave it.

## Differences from the SDK

`port/xbox/include/halo_xbox_prefix.h` is included before each game unit.
The files in `port/xbox/src` supply what the later SDK does not:

- `xbox_dsound.c`: the January SDK gave a voice's mix bins as a mask. Later
  SDKs take a list. The file translates the game's calls. The game's reverb
  image is for the January DirectSound, so it is not loaded: there is no
  reverb.
- `xbox_d3dx.c`: the four D3DX functions that the game calls. The SDK's
  D3DX library needs the SDK's own Direct3D.
- `xbox_support.cpp`: `fast_ftol_C` (not yet in the reconstruction), and the debug monitor's module functions. A title that
  imports `xbdm.dll` does not start on a retail console, so the game finds
  no modules.

`tools/xbox_sources.py` makes the pooled globals and the Bink stub from the
Linux port's files. `port/linux/game/msvc_comdat.c` supplies the header
inline functions that units call through a prototype.

## Status

The game starts on a modified retail console and on a development kit. It
shows the menus, plays the menu music and makes profiles. Known:

- There is no Bink video (the movies are skipped) and no reverb.
- The netcode is the original game's. It cannot play with the PC ports,
  which use newer netcode.
- Campaign and multiplayer levels are not yet tested.
