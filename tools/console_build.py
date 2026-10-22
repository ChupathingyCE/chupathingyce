"""Console ports, built from a port directory of their own (configure.py
--console-port DIR).

A console port lives in a repository of its own with this one, the game, as
a git submodule, and holds only its port: Warthog's original Xbox
(ChupathingyCE/warthog-xbox), the Xbox 360's (ChupathingyCE/warthog-360), the
Switch's (ChupathingyCE/warthog-switch).
The game keeps what every console shares: the shared code's console hooks
(the HALO_CONSOLE_* capabilities), port/console, and this file. From the
console repository's root:

    python3 chupathingyce/configure.py --console-port port/xbox [options]
    ninja xbox

configure.py writes build.ninja where it is run, with paths from there: the
game's sources from the submodule (ConsolePort.game, "chupathingyce"), the
port's from its own directory (ConsolePort.root, "port/xbox"), the output in
build/. Run from a checkout of the game itself, the port's targets join the
desktop ones (a port directory inside the tree, or beside it).

A port directory holds build.py, which configure.py loads:

    add_arguments(parser)               optional: the port's own options
    configure_inputs(port) -> paths     what re-runs configure.py on change
    generate(n, sln, port)              its rules and targets; generate
                                        nothing when its toolchain is missing

with port a ConsolePort, and sln configure.py's settings (sln.build_dir,
sln.port_release and the console options below). A port's build.py imports
what it needs of the game's build from tools (this file, tools.ninja_syntax,
tools.version, tools.windows_build's inline export patterns): the game's
tools are importable while configure.py runs. docs/building.md, "Console
ports", has the layout and the commands.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
from types import ModuleType
from typing import Any, Dict, Iterator, List, Tuple

# the game's root, where this file's tools/ is
GAME_ROOT = Path(__file__).resolve().parent.parent

# the directories whose asserts a console's release-style build (configure.py
# --console-no-debug-asserts) keeps: the data arrays, memory pools, caches and
# packet codecs, and the memory and string functions (cseries.h's
# HALO_CONSOLE_KEEP_ASSERTS)
KEEP_ASSERTS_DIRECTORIES = ("source/memory/", "source/cseries/")
# the game's sources a console builds only with its developer tools
# (--console-debug-tools): libtiff, whose only caller is the screenshots'
# export (tiff_file.c)
DEBUG_TOOLS_DIRECTORIES = ("source/bitmaps/libtiff/",)


def relative(path: Path) -> Path:
    """a path as build.ninja names it: relative to where configure.py runs
    (and ninja after it)"""
    return Path(os.path.relpath(Path(path).resolve()))


class ConsolePort:
    """a console port's directory and the game's, as build.ninja names them
    (relative to where ninja runs)"""

    def __init__(self, directory: Path, module: ModuleType) -> None:
        self.root = relative(directory)
        self.game = relative(GAME_ROOT)
        self.module = module
        self.name = self.root.name

    def game_path(self, *parts: Any) -> Path:
        """a path in the game (the game's own spelling: "source/game/game.c")"""
        return self.game.joinpath(*parts)

    def path(self, *parts: Any) -> Path:
        """a path in the port's directory"""
        return self.root.joinpath(*parts)

    def output_path(self, directory: Path, path: Path) -> Path:
        """where in a build directory a file of the game's or the port's
        goes (an object, a generated copy): game/ and port/ for theirs, so
        that neither one's directory names (a port beside the game's tree,
        "../port/xbox") reach out of it"""
        path = Path(path)
        for name, root in (("port", self.root), ("game", self.game)):
            inside = Path(os.path.relpath(path, root))
            if inside.parts and inside.parts[0] != "..":
                return directory / name / inside
        return directory / Path(os.path.relpath(path)).as_posix().replace("../", "up/")

    def game_relative(self, path: Path) -> str:
        """a game path's own spelling, without the game's directory
        ("source/game/game.c"), for tests against the game's layout"""
        return Path(os.path.relpath(path, self.game)).as_posix()

    def load_port_config(self) -> Dict[str, Any]:
        """the native builds' inputs (port/linux/port.json): the game's
        sources, defines and include directories"""
        with open(GAME_ROOT / "port" / "linux" / "port.json", "r", encoding="utf-8") as f:
            return json.load(f)

    def game_sources(self, config: Dict[str, Any]) -> List[Path]:
        """the game's C sources (port.json "game", as tools/linux_build.py's
        game_sources), under the game's directory"""
        game = config["game"]
        excluded = set(game.get("exclude", []))
        root = GAME_ROOT / game["root"]
        return sorted(
            self.game / source.relative_to(GAME_ROOT) for source in root.rglob("*.c")
            if source.relative_to(GAME_ROOT).as_posix() not in excluded
        )

    def game_defines_and_includes(self, config: Dict[str, Any], quote=lambda text: str(text)) -> str:
        """the game sources' defines and include directories (port.json
        "game"), the directories under the game's"""
        game = config["game"]
        return " ".join(
            [f"-D{define}" for define in game.get("defines", [])]
            + [f"-I{quote(self.game / directory)}" for directory in game.get("include_dirs", [])]
        )

    def game_units(self, sln: Any, config: Dict[str, Any]) -> Iterator[Tuple[Path, List[str]]]:
        """the game's C sources a console builds, each with the flags of its
        own it takes: libtiff only with the developer tools, and the memory
        checks a release-style build keeps (HALO_CONSOLE_KEEP_ASSERTS)"""
        debug_tools = getattr(sln, "console_debug_tools", False)
        for source in self.game_sources(config):
            name = self.game_relative(source)
            if not debug_tools and name.startswith(DEBUG_TOOLS_DIRECTORIES):
                continue
            yield source, (["-DHALO_CONSOLE_KEEP_ASSERTS"] if name.startswith(KEEP_ASSERTS_DIRECTORIES) else [])

    def game_headers(self) -> List[Path]:
        """the game's sources and headers (what tools/linux_msvc_semantics.py
        scans), under the game's directory"""
        return sorted(self.game / path.relative_to(GAME_ROOT) for path in (GAME_ROOT / "source").rglob("*")
                      if path.suffix in (".c", ".h"))


def console_defines(sln: Any) -> List[str]:
    """the game's defines for configure.py's console options, which every
    console build gives its game units (the shared code's HALO_CONSOLE_*)"""
    return [
        # --release: a failed assertion is noted in the log and the game goes
        # on, as the retail game did (cseries.h)
        *(["-DHALO_RELEASE"] if getattr(sln, "port_release", False) else []),
        # --console-debug-tools: the debug decomp's developer tools stay in
        *(["-DHALO_CONSOLE_DEBUG_TOOLS"] if getattr(sln, "console_debug_tools", False) else []),
        # --console-no-debug-asserts: the asserts compiled out, as retail
        # compiled them out, but for the memory checks (cseries.h)
        *(["-DHALO_CONSOLE_NO_DEBUG_ASSERTS"] if getattr(sln, "console_no_debug_asserts", False) else []),
        # --console-players: the players the network tables hold
        f"-DHALO_CONSOLE_PLAYERS={int(getattr(sln, 'console_players', 16) or 16)}",
    ]


def add_console_arguments(parser: argparse.ArgumentParser) -> None:
    """configure.py's options for every console port"""
    parser.add_argument(
        "--console-port",
        metavar="DIR",
        type=Path,
        action="append",
        default=[],
        help="a console port's directory (its build.py: tools/console_build.py), whose targets build.ninja gets; "
        "from a console repository with the game as a submodule, only its. May be given more than once",
    )
    parser.add_argument(
        "--console-no-debug-asserts",
        action="store_true",
        help="console builds: a release-style build (HALO_CONSOLE_NO_DEBUG_ASSERTS): the debug decomp's asserts "
        "compiled out as retail compiled them out, except the memory checks of source/memory and source/cseries "
        "(source/cseries/cseries.h)",
    )
    parser.add_argument(
        "--console-debug-tools",
        action="store_true",
        help="console builds: the debug decomp's developer tools (HALO_CONSOLE_DEBUG_TOOLS: the profiler, AI, "
        "render and collision debug, stack walker, libtiff and the bitmap tools), left out by default",
    )
    parser.add_argument(
        "--console-players",
        type=int,
        choices=(16, 24, 32, 64),
        default=16,
        help="console builds: the players their network tables hold (HALO_CONSOLE_PLAYERS). 16 is the original "
        "Xbox's; more are test builds",
    )


def load_console_port(directory: Path) -> ConsolePort:
    """a console port's directory and its build.py"""
    script = Path(directory) / "build.py"
    if not script.is_file():
        raise SystemExit(f"configure.py: {script}: no such file (a console port's build: tools/console_build.py)")
    name = "console_port_" + "".join(ch if ch.isalnum() else "_" for ch in str(relative(directory)))
    spec = importlib.util.spec_from_file_location(name, script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for function in ("configure_inputs", "generate"):
        if not hasattr(module, function):
            raise SystemExit(f"configure.py: {script} has no {function}() (tools/console_build.py)")
    return ConsolePort(Path(directory), module)


def console_configure_inputs(ports: List[ConsolePort]) -> List[Path]:
    """Files whose change must re-run configure.py: this file, and each
    port's build.py and its own inputs."""
    inputs = [relative(Path(__file__))]
    for port in ports:
        inputs += [port.path("build.py"), *port.module.configure_inputs(port)]
    return inputs
