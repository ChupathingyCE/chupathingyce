# Building

## Desktop, Android and the server

You need Python 3, [ninja](https://ninja-build.org/) and clang. The game
supplies the Xbox SDK declarations it uses, so you don't need the SDK.

```sh
python3 configure.py
ninja            # the game for the computer you're on
```

| Target | Result | Instructions |
| --- | --- | --- |
| `ninja macos` | `build/macos/ChupathingyCE.app` | [port/macos/README.md](../port/macos/README.md) |
| `ninja linux64` | `build/linux64/halo` (64-bit) | [port/linux/README.md](../port/linux/README.md) |
| `ninja linux` | `build/linux/halo` (32-bit) | [port/linux/README.md](../port/linux/README.md) |
| `ninja windows` | `build/windows/halo.exe` | [port/windows/README.md](../port/windows/README.md) |
| `ninja windows64` | `build/windows64/halo.exe`, the 64-bit game | [port/windows/README.md](../port/windows/README.md#64-bit) |
| `ninja freebsd64` | `build/freebsd64/halo`, on FreeBSD | [port/unix/README.md](../port/unix/README.md) |
| `ninja android_apk` | the Android app | [port/android/README.md](../port/android/README.md) |
| `ninja server` | `build/server-<arch>/chupathingyce-server`, the dedicated server (Linux) | [server/docs/building.md](../server/docs/building.md) |

Useful `configure.py` options:

| Option | What it does |
| --- | --- |
| `--release` | A release build, as players get. Without it, a failed check stops the game. |
| `--portable` | A Linux or Windows build that runs on any x86-64 computer, or a universal Mac application, to give to others. The Linux builds also run on older distributions and on SteamOS: refer to "Portable build" in [port/linux/README.md](../port/linux/README.md#portable-build). |
| `--no-game-browser` | Leaves out the server list, stats and dedicated servers. |
| `--pgo=off`, `--lto=off` | Faster builds, without profile-guided or link-time optimisation. |

The console ports (original Xbox, Xbox 360, Switch, Wii U) are built from their own
repositories, with this one as a submodule: [Console ports](#console-ports), below.

The version being made is in `VERSION`. Releases are built and published by
the project's release workflow; the builds on this repository's Actions page
are for checking changes.

## Console ports

Each console port lives in a repository of its own, with this repository
(the game) pinned as a git submodule. The console repository holds only its
port; the game holds the shared code, its console hooks, `port/console` and
the build's generic side (`tools/console_build.py`).

| Repository | Console | Status | Maintained by |
| --- | --- | --- | --- |
| [ChupathingyCE/warthog-xbox](https://github.com/ChupathingyCE/warthog-xbox) | original Xbox | Test builds: plays online, up to 32 players, with Halo CE+ maps | Milenko |
| [ChupathingyCE/warthog-360](https://github.com/ChupathingyCE/warthog-360) | Xbox 360 | Plays and joins servers on RGH hardware; test builds on Discord | Milenko |
| [ChupathingyCE/warthog-switch](https://github.com/ChupathingyCE/warthog-switch) | Switch | Working: boots and plays on Switch hardware; CI builds it | [thelinkin3000](https://github.com/thelinkin3000) |
| [ChupathingyCE/warthog-wiiu](https://github.com/ChupathingyCE/warthog-wiiu) | Wii U | Plays on hardware and joins servers online; [public test builds](https://github.com/ChupathingyCE/warthog-wiiu/releases) | Milenko |

A console repository looks like this:

```
warthog-xbox/
    chupathingyce/      the game, a submodule
    port/xbox/          the port: its sources and build.py
    tools/              the port's own tools (packaging, the build's stamp)
    build.ninja         written by configure.py
    build/xbox/         the build's output
```

From the console repository's root:

```sh
git submodule update --init
python3 chupathingyce/configure.py --console-port port/xbox [options]
ninja xbox
```

`configure.py` writes `build.ninja` in the folder it runs from, with paths
from there: the game's sources from `chupathingyce/`, the port's from
`port/xbox/`, and the output in `build/`. Run this way it generates the
console's targets only. `ninja` reruns `configure.py` with the same options
when the game's build files or the port's `build.py` change, and after a
submodule update.

A port can also be built from a checkout of the game itself: `python3
configure.py --console-port ../warthog-xbox/port/xbox` in the game's root
adds the console's targets to the desktop ones.

### A port's build.py

`--console-port DIR` loads `DIR/build.py`, which has:

| Function | What it does |
| --- | --- |
| `add_arguments(parser)` | optional: the port's own `configure.py` options (`--xbox-d3d8`, `--oxdk`) |
| `configure_inputs(port)` | the port's files and folders whose change reruns `configure.py` |
| `generate(n, sln, port)` | writes the port's rules and targets; generates nothing when its toolchain is missing |

`port` is a `ConsolePort` (`tools/console_build.py`): `port.game` and
`port.root` are the game's and the port's folders as `build.ninja` names
them, and it lists the game's sources, defines and include folders
(`port/linux/port.json`) under the game's folder. `sln` holds
`configure.py`'s settings: `sln.build_dir`, `sln.port_release`, the console
options below, and every option by name in `sln.args`. While
`configure.py` runs, `build.py` can import the game's build modules
(`tools.console_build`, `tools.ninja_syntax`, `tools.version`).

### Options for every console

| Option | What it does |
| --- | --- |
| `--console-port DIR` | a console port's folder; may be given more than once |
| `--release` | assertions are not checked; a failed one is logged and the game goes on |
| `--console-players {16,24,32,64}` | the players the network tables hold (`HALO_CONSOLE_PLAYERS`). 16 is the original Xbox's; more are test builds |
| `--console-debug-tools` | keeps the debug decomp's developer tools (`HALO_CONSOLE_DEBUG_TOOLS`), which console builds leave out by default |
| `--console-no-debug-asserts` | the release-style build: the debug decomp's asserts compiled out as retail compiled them out, but for the memory checks of `source/memory` and `source/cseries` (`HALO_CONSOLE_NO_DEBUG_ASSERTS`) |

A port adds its own options (toolchains and SDK libraries) with
`add_arguments`; its README has them.

### What goes where

The shared code tests console capabilities, never a console's name, where
another console could use the same hook: `HALO_CONSOLE_*` defines and
`console.*` settings. A port turns a capability on from its own build's
define in `port/linux/include/halo_console_features.h`, which lists them
and what each hook asks of the port, or on its compile lines:

| Capability | What it is |
| --- | --- |
| `HALO_CONSOLE_PLAYERS` | the players the network tables hold, and the console's session limits (`halo_port_limits.h`) |
| `HALO_CONSOLE_MEMORY_CLASS` | the port answers for its memory class: the baseline's sizes, or larger caches and buffers above it (`halo_console_memory.h`) |
| `HALO_CONSOLE_DELTA` | Delta Peer as a client |
| `HALO_CONSOLE_NO_*` | the desktop extras a console leaves out (`halo_port_features.h`, a port's prefix header) |
| `HALO_CONSOLE_ONLINE_GAMES` | the console's own Online Games screen |
| `HALO_CONSOLE_MENU_PLATFORM` | the console's name for the menus (`x360`, `wiiu`): the Chupathingy menus' rows marked `console` or with that name in place of the desktop's, packed with no gaps, and ui.map's button pictures in the prompts (`port/assets/menus/README.md`, `tools/port_settings.py`) |
| `HALO_CONSOLE_PROFILE_ID` | on the port's compile line: Delta Peer's PROFILE carries `console_profile_id()`, the port's (a console linked with Link Profile, `port/console/link`) |
| `HALO_CONSOLE_HTTPS` | HTTPS over the console's own sockets, with pinned keys and roots (`port/console/https`, [console-https.md](console-https.md)) |
| `HALO_CONSOLE_LOG_BUFFER`, `HALO_CONSOLE_LOG_THROTTLE` | a log whose writes are slow |
| `HALO_CONSOLE_GPU_RESOURCE_HOOKS`, `HALO_CONSOLE_NO_WRITE_WATCH`, `HALO_CONSOLE_HOST_ART`, `HALO_CONSOLE_MAP_LOAD_HOOKS`, `HALO_CONSOLE_STATUS_LINE` | a console renderer of its own |
| `HALO_CONSOLE_GL_LAYER` | a console whose OpenGL is the port's own layer over another graphics API |
| `HALO_CONSOLE_XBOX_RENDER_SIZES` | the Xbox's texture cache and frame draw limits on a build that otherwise has the desktop's |
| `HALO_CONSOLE_DEBUG_CHANNEL`, `HALO_CONSOLE_HALT_TO_LAUNCHER` | a console without a standard error, whose halts go back to its launcher |
| `HALO_CONSOLE_BRINGUP_LOG` | with the developer tools, a console's bring-up logs (frame stages, the main loop's pulse, init.txt's `later` commands) |
| `HALO_BIG_ENDIAN` | a big-endian console: the maps' headers and tags byte-swapped as they load (`port/console/game`, `tools/tag_swap_generate.py`) |
| `HALO_WINDOW_TRANSLATED` | with `HALO_64BIT`, a 32-bit console's Xbox addresses in a window placed wherever it has room (`source/cseries/xbox_address.h`) |

### Link Profile on a console

`port/console/link` is a console's Link Profile (docs/delta.md, "Delta
Link"): `console_link.c` (the device key, the code, the requests, plain C)
and `console_link_panel.c` (the DELTA PROFILE panel over Online Games, game
code). A port builds both, with `port/third_party/qrcodegen` and the
internet play's `p2p_crypto.c`, fills a `struct console_link_port` (a
plain HTTP request to the console list's host, and later an HTTPS one; a
thread; a lock; random bytes; the clock; the log; where the key and the
link are kept), calls `console_link_initialize` at the start, opens the
panel with Start on its Online Games, and defines `console_profile_id`
with `-DHALO_CONSOLE_PROFILE_ID`. `tools/test_console_link.py` runs it on
the host.

A console's name stays in the shared code only for what is that console's
alone: `HALO_XBOX_CONSOLE` is the original Xbox's own target (the game as
the Xbox ran it, its SDK's calls, the game's calls into `port/xbox`), and
`HALO_SWITCH_CONSOLE` the Switch's settings and renderer calls.

A console's own code, its build script and its packaging stay in its
repository. A change to the shared code goes to the game first; the
console repository then moves its submodule to it.

### Desktop test builds of a console's side

| Option | What it does |
| --- | --- |
| `--console-limits` | a desktop build with the original Xbox's network and object limits, to test the console's side of cross-play on one computer ([cross-play.md](cross-play.md)); never a build to ship |
| `--window-rebase` | the 32-bit Linux build with its Xbox memory window wherever `HALO_WINDOW_BASE` puts it, as the Switch's guest has it; never a build to ship |
