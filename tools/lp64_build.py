"""Ninja rules shared by the native 64-bit builds (``ninja macos``,
``ninja linux64``).

The game and most of the platform layer are the Linux build's
(tools/linux_build.py, port/linux); the 64-bit builds compile them as native
64-bit code, which the 32-bit builds never are. Two things make that work
without changing what the other builds compile:

  - HALO_64BIT selects the game's 64-bit code paths: pointers inside Xbox
    data are 32-bit Xbox addresses (source/cseries/xbox_address.h) into a
    4 GB region the platform layer reserves (port/linux/src/xbox_memory.c).
  - the sources compiled with the Xbox's ABI are compiled from a copy in
    which `long` is spelled `int` (tools/lp64_rewrite.py): MSVC's `long` is
    32 bits, an LP64 host's is 64.

What differs between the hosts (the target, the C library's feature macros,
the libraries and the host's own units) is an Lp64Host; the rest is here.
See port/macos/README.md.
"""

import json
import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Sequence, Set

from .linux_build import (
    CUSTOM_EDITION_DEFINES,
    EXPAT_DIR,
    EXPAT_SOURCES,
    GAME_FLAGS as LINUX_GAME_FLAGS,
    KCP_DIR,
    MBEDTLS_DIR,
    MINIUPNPC_DEFINES,
    MINIUPNPC_DIR,
    MONOCYPHER_DIR,
    MUSL_MATH_DIR,
    OPTIMISATION,
    PLATFORM_FLAGS as LINUX_PLATFORM_FLAGS,
    QRCODEGEN_DIR,
    STB_DIR,
    TOML_DIR,
    XDK_INCLUDE,
    ZLIB_DEFINES,
    ZLIB_DIR,
    ZLIB_SOURCES,
    compile_launcher,
    game_sources,
    game_browser_defines,
    miniupnpc_sources,
    musl_math_sources,
    updater_defines,
)
from .ninja_syntax import Writer
from .version import VERSION_SOURCES

LINUX_PORT_DIR = Path("port/linux")
LINUX_PORT_CONFIG = LINUX_PORT_DIR / "port.json"

# The Xbox ABI the sources were written against, as far as a 64-bit host can
# reproduce it (compare linux_build.LINUX_ABI_FLAGS): 16-bit wchar_t, MSVC's
# extensions, tentative definitions shared between units, no optimisations
# that assume the absence of UB MSVC tolerated, and the floating point
# results of the other ports (no fused multiply-adds).
LP64_ABI_FLAGS = [
    "-fms-extensions",
    "-fshort-wchar",
    "-fcommon",
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fno-delete-null-pointer-checks",
    "-fno-omit-frame-pointer",
    "-ffp-contract=off",
    "-DHALO_64BIT",
    # Halo PC's Custom Edition maps (linux_build.py, CUSTOM_EDITION_DEFINES)
    *CUSTOM_EDITION_DEFINES,
    # the C library's checked printf macros collide with the MSVC names
    "-D_FORTIFY_SOURCE=0",
    OPTIMISATION,
    "-g",
    *(f"-fno-builtin-{name}" for name in (
        "wcslen", "wcsnlen", "wcschr", "wcsrchr", "wcscmp", "wcsncmp", "wcscpy",
        "wcsncpy", "wcscat", "wcsncat", "wmemchr", "wmemcmp", "wmemcpy",
        "wmemmove", "wmemset",
    )),
]


def lp64_game_flags(host_flags: List[str]) -> List[str]:
    """The Linux build's game flags, with the conversions that truncate a
    64-bit pointer made errors: each is a place that still treats an Xbox
    address as a pointer or the reverse. host_flags restrict the host's C
    library to ISO C, as __STRICT_ANSI__ (in the Linux flags) does glibc."""
    return [
        *(flag for flag in LINUX_GAME_FLAGS if flag not in ("-w", "-Wno-error=int-conversion",
                                                           "-Wno-error=implicit-function-declaration")),
        *host_flags,
        "-ferror-limit=0",
        # -w would also hide the errors below
        "-Wno-everything",
        "-Werror=int-to-pointer-cast",
        "-Werror=pointer-to-int-cast",
        "-Werror=void-pointer-to-int-cast",
        "-Werror=int-conversion",
        "-Werror=pointer-integer-compare",
        # an undeclared function returns int, truncating a returned pointer
        "-Werror=implicit-function-declaration",
        "-Werror=format",
    ]


LP64_PLATFORM_FLAGS = [
    *LINUX_PLATFORM_FLAGS,
    "-Werror=incompatible-pointer-types",
    "-Werror=int-to-pointer-cast",
    "-Werror=pointer-to-int-cast",
    "-Werror=void-pointer-to-int-cast",
    "-Werror=int-conversion",
    "-Werror=format",
]


@dataclass
class Lp64Host:
    """What one 64-bit build compiles differently."""
    # the build's name: its folder in build/, its rules' prefix
    name: str
    # the target (and instruction set) of every unit
    target_flags: List[str]
    # lp64_game_flags(...)
    game_flags: List[str]
    # the posix_*.c units', with the host's own ABI and no rewrite (their
    # boundary types are posix.h's), after target_flags
    posix_flags: List[str]
    # where the host's libraries' headers are (SDL3), or ""
    host_include: str = ""
    # the third-party libraries' extra flags (mbedtls, miniupnpc)
    third_party_flags: List[str] = field(default_factory=list)
    # sources left out ("path/to/file.c")
    excluded: Set[str] = field(default_factory=set)
    # the host's own platform units, with posix_flags
    host_sources: List[Path] = field(default_factory=list)
    # more platform units with the Xbox's ABI, rewritten and built as
    # port/linux/src's (the dedicated server's: tools/server_build.py)
    platform_sources: List[Path] = field(default_factory=list)


@dataclass
class Lp64Unit:
    source: Path
    cflags: str
    # a unit with the host's ABI (and a 32-bit wchar_t): never optimised
    # together with the others at link time
    native: bool = False


def _quote(path: Any) -> str:
    text = str(path).replace(os.sep, "/")
    return f'"{text}"' if " " in text else text


def load_json(path: Path) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def lp64_excluded() -> List[str]:
    """the sources every 64-bit build leaves out (port/linux/port.json
    "lp64")"""
    return list(load_json(LINUX_PORT_CONFIG).get("lp64", {}).get("exclude", []))


def lp64_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py (with the build's own)."""
    return [Path(__file__), LINUX_PORT_CONFIG, LINUX_PORT_DIR / "src", LINUX_PORT_DIR / "game"]


def rewritten_inputs(extra_roots: Sequence[Path] = ()) -> List[Path]:
    """Every file compiled or included with the Xbox's ABI: the rewrite's
    inputs. Headers come along whole, so that includes relative to the
    including file find the rewritten copies."""
    roots = [Path("source"), LINUX_PORT_DIR / "game", LINUX_PORT_DIR / "src", LINUX_PORT_DIR / "include",
             Path("port/include"), KCP_DIR, TOML_DIR, Path("server/src"), *extra_roots]
    files = []
    for root in roots:
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.suffix.lower() in (".c", ".h", ".inl", ".inc") and not path.name.startswith("posix_"):
                files.append(path)
    return files


class Lp64Build:
    """One 64-bit build's shared rules: the rewritten sources, the MSVC
    semantics headers and the compile rule (``{name}_cc``)."""

    def __init__(self, n: Writer, sln: Any, name: str, cc: str, extra_roots: Sequence[Path] = ()) -> None:
        self.n = n
        self.sln = sln
        self.name = name
        self.build_dir: Path = sln.build_dir / name
        self.lp64_dir = self.build_dir / "lp64"
        self.semantics_header = self.build_dir / "halo_msvc_semantics.h"
        self.platform_semantics_header = self.build_dir / "platform_msvc_semantics.h"
        label = name.upper()

        n.variable(f"{name}_cc", cc)
        n.rule(
            name=f"{name}_lp64",
            command="$python tools/lp64_rewrite.py --output $out $in",
            description=f"{label} LP64 $in",
            restat=True,
        )
        rewritten = []
        for source in rewritten_inputs(extra_roots):
            n.build(outputs=self.lp64(source), rule=f"{name}_lp64", inputs=source,
                    implicit=[Path("tools/lp64_rewrite.py")])
            rewritten.append(self.lp64(source))
        n.build(outputs=self.build_dir / "lp64.stamp", rule="phony", inputs=rewritten)

        n.rule(
            name=f"{name}_msvc_semantics",
            command="$python tools/linux_msvc_semantics.py --output $out $scan",
            description=f"{label} MSVC SEMANTICS $out",
            restat=True,
        )
        game_headers = sorted(p for p in Path("source").rglob("*") if p.suffix in (".c", ".h"))
        # (the scan reads the original sources: tags and inline names, not types)
        n.build(outputs=self.semantics_header, rule=f"{name}_msvc_semantics",
                implicit=[Path("tools/linux_msvc_semantics.py"), *game_headers, *sorted(XDK_INCLUDE.glob("*.h"))],
                variables={"scan": f"--all-inlines --tags source --inlines source --inlines {XDK_INCLUDE}"})
        n.build(outputs=self.platform_semantics_header, rule=f"{name}_msvc_semantics",
                implicit=[Path("tools/linux_msvc_semantics.py"), *sorted(XDK_INCLUDE.glob("*.h"))],
                variables={"scan": f"--inlines {XDK_INCLUDE}"})

        n.rule(
            name=f"{name}_cc",
            command=f"{compile_launcher(sln)}${name}_cc -MMD -MF $out.d $cflags -c $in -o $out",
            description=f"{label} CC $out",
            depfile="$out.d",
            deps="gcc",
        )

    def lp64(self, path: Path) -> Path:
        """the rewritten copy of path"""
        return self.lp64_dir / path

    def units(self, host: Lp64Host, generated_sources: List[Path]) -> List[Lp64Unit]:
        """every unit of the game for host, in link order"""
        sln = self.sln
        lp64 = self.lp64
        linux_config = load_json(LINUX_PORT_CONFIG)
        excluded = host.excluded
        release = ["-DHALO_RELEASE"] if getattr(sln, "port_release", False) else []
        abi = " ".join([*host.target_flags, *LP64_ABI_FLAGS, *release, *game_browser_defines(sln)])
        prefix_header = lp64(LINUX_PORT_DIR / "include" / "halo_linux_prefix.h")
        port_include = lp64(LINUX_PORT_DIR / "include")
        xdk = _quote(lp64(XDK_INCLUDE))
        units: List[Lp64Unit] = []

        def add(source: Path, cflags: str, native: bool = False) -> None:
            units.append(Lp64Unit(source, cflags, native))

        game = linux_config["game"]
        defines = " ".join(f"-D{d}" for d in game.get("defines", []))
        # (the rewritten copies first; then the originals, for what is not
        # rewritten, such as port/third_party/stb's)
        includes = " ".join([f"-I{_quote(lp64(Path(d)))}" for d in game.get("include_dirs", [])] +
                            [f"-idirafter {_quote(Path(d))}" for d in game.get("include_dirs", [])])
        game_cflags = " ".join([
            abi, " ".join(host.game_flags),
            f"-include {_quote(prefix_header)}", f"-include {_quote(self.semantics_header)}",
            defines, f"-I{_quote(port_include)}",
            # the headers of the port's own game units (port/linux/game), for
            # the game sources that call them
            f"-iquote {_quote(lp64(Path(linux_config['game_sources'])))}",
            includes, f"-idirafter {xdk}",
        ])
        for source in game_sources(linux_config):
            if source.as_posix() not in excluded:
                add(lp64(source), game_cflags)
        # the port's own units that see the game as its sources do (port/linux/game)
        for source in sorted(Path(linux_config["game_sources"]).glob("*.c")):
            if source.as_posix() not in excluded:
                add(lp64(source), game_cflags)
        # the dedicated server's director, with the game browser (server/)
        if getattr(sln, "game_browser", False):
            for source in sorted(Path("server/src").glob("*.c")):
                add(lp64(source), game_cflags)

        platform_dir = Path(linux_config["platform_sources"])
        platform_cflags = " ".join([
            abi, " ".join(LP64_PLATFORM_FLAGS),
            f"-include {_quote(prefix_header)}", f"-include {_quote(self.platform_semantics_header)}",
            f"-I{_quote(lp64(platform_dir))}", f"-I{_quote(port_include)}",
            f"-I{_quote(lp64(TOML_DIR))}", f"-I{_quote(lp64(KCP_DIR))}",
            # (the menus' XML parser's own headers, not rewritten: Expat is
            # built with the host's ABI, below)
            f"-I{EXPAT_DIR}",
            # (Link Profile's QR encoder's, likewise)
            f"-I{QRCODEGEN_DIR}",
            # (public games' signatures', likewise)
            f"-I{MONOCYPHER_DIR}",
            # (the port's zlib's, likewise: its API is its own types)
            f"-I{ZLIB_DIR}",
            f"-I{_quote(lp64(Path('source')))} -I{_quote(lp64(Path('source/cseries')))}",
            host.host_include, f"-idirafter {xdk}",
        ])
        posix_cflags = " ".join([*host.target_flags, *host.posix_flags, f"-I{platform_dir}", host.host_include,
                                 *game_browser_defines(sln)])
        mbedtls_include = f"-I{MBEDTLS_DIR / 'include'}"
        for source in sorted(platform_dir.glob("*.c")):
            if str(source) in excluded:
                continue
            if source.name in ("posix_update.c", "posix_browser.c"):
                add(source, f"{posix_cflags} {mbedtls_include}", native=True)
            elif source.name == "posix_upnp.c":
                add(source, f"{posix_cflags} -I{MINIUPNPC_DIR / 'include'} -DMINIUPNP_STATICLIB", native=True)
            elif source.name == "posix_ui_font.c":
                add(source, f"{posix_cflags} -I{STB_DIR}", native=True)
            elif source.name.startswith("posix_"):
                add(source, posix_cflags, native=True)
            elif source.name in VERSION_SOURCES:
                add(lp64(source), f"{platform_cflags} {updater_defines(getattr(sln, 'port_release', False))}")
            elif source.name == "text_hires.c":
                # (it includes stb_truetype by a path from its own folder: the
                # copy's folder has no third_party beside it, the original's has)
                add(lp64(source), f"{platform_cflags} -idirafter {LINUX_PORT_DIR / 'src'}")
            else:
                add(lp64(source), platform_cflags)
        # (the dedicated server's own platform units, with the version's
        # defines as updater.c: tools/server_build.py)
        for source in host.platform_sources:
            add(lp64(source), f"{platform_cflags} {updater_defines(getattr(sln, 'port_release', False))}")
        # the high-res HUD's textures (port/assets/hud; port/linux/src/hud_hires.c),
        # with the platform units' flags: its table is hud_hires.h's, from the
        # 64-bit tree
        for source in generated_sources:
            add(source, platform_cflags)
        # the host's own platform units, with its ABI
        for source in host.host_sources:
            if source.as_posix() not in excluded:
                add(source, posix_cflags, native=True)
        for source in sorted((MBEDTLS_DIR / "library").glob("*.c")):
            add(source, " ".join([*host.target_flags, "-std=gnu11", OPTIMISATION, "-g", "-w",
                                  *host.third_party_flags, mbedtls_include, f"-I{MBEDTLS_DIR / 'library'}"]),
                native=True)
        for source in miniupnpc_sources():
            add(source, " ".join([*host.target_flags, "-std=gnu11", OPTIMISATION, "-g", "-w",
                                  *host.third_party_flags, *MINIUPNPC_DEFINES,
                                  f"-I{MINIUPNPC_DIR / 'include'}", f"-I{MINIUPNPC_DIR / 'src'}"]),
                native=True)
        # the menus' XML parser (port/third_party/expat; menu_files.c), with the
        # host's ABI: it holds nothing of the Xbox's, and its API is its own
        # types (expat.h, which menu_files.c includes unrewritten)
        native_third_party = [*host.target_flags, "-std=gnu11", OPTIMISATION, "-g", "-w", *host.third_party_flags]
        for name in EXPAT_SOURCES:
            add(EXPAT_DIR / name, " ".join([*native_third_party, f"-I{EXPAT_DIR}"]), native=True)
        # Link Profile's QR encoder (port/third_party/qrcodegen; browser.c),
        # with the host's ABI too: its long is the host's (LONG_MAX)
        add(QRCODEGEN_DIR / "qrcodegen.c", " ".join(native_third_party), native=True)
        # public games' signatures (port/third_party/monocypher; p2p_crypto.c),
        # with the host's ABI: its API is bytes and size_t
        for name in ("monocypher.c", "monocypher-ed25519.c"):
            add(MONOCYPHER_DIR / name, " ".join(native_third_party), native=True)
        # the port's zlib (port/third_party/zlib; hud_hires.c, updater.c,
        # xgpu_post.c), with the host's ABI: its API is its own types (uLong)
        for name in ZLIB_SOURCES:
            add(ZLIB_DIR / name, " ".join([*native_third_party, *ZLIB_DEFINES]), native=True)
        third_party = " ".join([abi, "-std=gnu11", "-w"])
        add(lp64(TOML_DIR / "tomlc17.c"), third_party)
        add(lp64(KCP_DIR / "ikcp.c"), third_party)
        for source in musl_math_sources():
            add(source, " ".join([abi, "-std=gnu11", "-w", f"-I{MUSL_MATH_DIR}/include",
                                  f"-include {MUSL_MATH_DIR}/include/libm.h"]))
        return units

    def objects(self, host: Lp64Host, generated_sources: List[Path], obj_dir: Path,
                extra_cflags: List[str] = []) -> List[Path]:
        """the objects of every unit, compiled into obj_dir; extra_cflags go
        to the units with the Xbox's ABI (link-time optimisation)"""
        objects = []
        extra = " ".join(extra_cflags)
        for unit in self.units(host, generated_sources):
            # (a rewritten copy's object sits where the original's would)
            relative = unit.source.relative_to(self.lp64_dir) if self.lp64_dir in unit.source.parents else unit.source
            obj = obj_dir / relative.with_suffix(".o")
            objects.append(obj)
            self.n.build(outputs=obj, rule=f"{self.name}_cc", inputs=unit.source,
                         implicit=[self.semantics_header, self.platform_semantics_header],
                         order_only=[self.build_dir / "lp64.stamp"],
                         variables={"cflags": unit.cflags if unit.native or not extra
                                    else f"{unit.cflags} {extra}"})
        return objects
