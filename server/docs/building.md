# Building the server

The releases are built by ChupathingyCE's release workflow; this is for
building one yourself, or working on it.

## Targets

On Linux, `configure.py` adds the server targets this machine can link:

| Target | Result | Where it builds |
| --- | --- | --- |
| `ninja server-x64` | `build/server-x64/chupathingyce-server` | an x86-64 machine |
| `ninja server-x86` | `build/server-x86/chupathingyce-server` | a 32-bit x86 machine, or an x86-64 one with 32-bit glibc (as `ninja linux` needs) |
| `ninja server-arm64` | `build/server-arm64/chupathingyce-server` | a 64-bit ARM machine |
| `ninja server` | each of the above this machine can link | |

```sh
python3 configure.py --portable --release
ninja server
```

The compiler is clang (`--linux-cc` for another). `--portable` makes a
server for any machine of its architecture (SSE2 for x86 and x64, ARMv8-A
for arm64) instead of for this one. The first configure downloads SDL's
release once, for its headers (below).

## Static, with musl

Against musl, the server is one static program, which runs on any Linux of
its architecture whatever its C library and its version: what the releases
ship. Against glibc it is an ordinary program that needs only the C library
(no SDL, no OpenGL, no sound libraries), for working on it.

`configure.py` links statically when the machine's C library is musl, so a
release's server is built in Alpine Linux, in Docker, by the script the
workflows use:

```sh
python3 tools/ci_build.py server-x64 release --alpine
```

It runs Alpine's container of the server's architecture, builds there, and
leaves the server in `dist/chupathingyce-server-linux-x64/` with its README,
playlists and license notices. A release's server there is stripped of
its debug information (about 19 MB instead of 33); a debug build's keeps
it, and so does `build/server-x64/chupathingyce-server`, for reading a
crash report's addresses. An x86 container runs on an x86-64 machine;
an arm64 one needs an arm64 machine (or an emulator, which is slow). On an
Alpine machine, `python3 tools/ci_build.py server-x64 release` does the same
without Docker.

Why musl: glibc's static programs still load parts of glibc at run time
(name lookups among them) and must match its version, and a dynamically
linked program needs the system's loader and libraries. Missing, those give
the "required file not found" kind of error: a 32-bit program on a 64-bit
system without 32-bit libraries, or one built on a newer system than it
runs on. A static musl program has none of that.

## How the server differs from the game

The server is the game: the same sources, the same network code and
version (`configure.py --game-browser`'s, with the dedicated server's
director, `server/src`). The 32-bit server is `ninja linux`'s units, the
x64 and arm64 ones `ninja linux64`'s (64-bit, `tools/lp64_build.py`). What
it leaves out is what a player sits in front of:

| The game's | The server's (`server/platform`) |
| --- | --- |
| `sdl_platform.c`: SDL's window, events, clipboard | `server_platform.c`: no window; SIGTERM and SIGINT stop it; checks its maps and playlist at start-up |
| `xinput_sdl.c`: controllers, keyboard and mouse | `server_input.c`: one controller that never moves, as the game's dedicated mode had |
| `updater.c`: the self-updater | none: a server is updated as it was deployed |
| SDL's library | `sdl_headless.c`: the few SDL utility functions the shared units call (files, folders), and no sound device or OpenGL |
| glibc's `backtrace` | `backtrace.c`, where the C library has none (musl), for crash reports |

The renderer is the game's, with nothing to draw to (as with
`debug.null_renderer`), and the sound mixer the game's, on its own clock
with no device. SDL's headers are needed to compile (the renderer's OpenGL
declarations), never its library: the server links the C library only.

### arm64

The arm64 server is the 64-bit Linux build with ARM code: the 64-bit
builds' Xbox address space (`source/cseries/xbox_address.h`), `char` signed
(as MSVC's, and every other build's), and no fused multiply-adds
(`-ffp-contract=off`, as everywhere), so it computes what the other builds
compute. On ARM Linux the Xbox's 4 GB are reserved at 256 GB rather than
1 TB, below the top of the smallest address space an ARM Linux kernel gives
a program (512 GB). Memory pages of 4, 16 or 64 KB are all handled (16 KB is
Apple silicon's, which the macOS build runs on).

## Testing a change

- `chupathingyce-server --version` and a run without maps (a clear error,
  status 1) are what the build workflow checks for each architecture.
- Two copies of the game on one Linux machine can play on a server there,
  each on its own loopback address: the server with
  `HALO_NET_ADDRESS=127.0.0.20 HALO_NET_BROADCAST=127.0.0.21,127.0.0.22`, each
  client with its address and `HALO_NET_BROADCAST=127.0.0.20`, and
  `HALO_NETWORK_TEST=join` (`HALO_TEST_INPUT=bot:1` to move). For tests,
  keep them off the internet and the lists: `HALO_NET_ONLINE=false
  HALO_NET_LIST_GAMES=false HALO_DEDICATED_PUBLIC=false`, and give each its
  own `HALO_SAVE_ROOT`.
