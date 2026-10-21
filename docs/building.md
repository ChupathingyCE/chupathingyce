# Building

The desktop, Android and server builds are in the README, "Building it
yourself": `python3 configure.py`, then `ninja` and a target. This page
covers what the README doesn't.

## Console ports

Each console port lives in a repository of its own, with this repository
(the game) pinned as a git submodule. The console repository holds only its
port; the game holds the shared code, its console hooks, `port/console` and
the build's generic side (`tools/console_build.py`).

| Repository | Consoles |
| --- | --- |
| [ChupathingyCE/warthog-xbox](https://github.com/ChupathingyCE/warthog-xbox) | original Xbox, Xbox 360 |
| [ChupathingyCE/warthog-switch](https://github.com/ChupathingyCE/warthog-switch) | Switch |

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
`console.*` settings. A console's own code, its build script and its
packaging stay in its repository. A change to the shared code goes to the
game first; the console repository then moves its submodule to it.
