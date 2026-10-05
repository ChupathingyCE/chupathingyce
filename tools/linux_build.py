"""Ninja rules for the native Linux build (``ninja linux``).

It compiles the game sources with clang for 32-bit x86 Linux, adds the
platform layer in ``port/linux/src``, and links an ELF executable at
``build/linux/halo``. See port/linux/README.md for the design.
"""

import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple

from .embed_assets import hud_assets_build, hud_configure_inputs, ui_fonts_build
from .ninja_syntax import Writer
from .version import release_build, version

PORT_DIR = Path("port/linux")
PORT_CONFIG = PORT_DIR / "port.json"
# the Xbox SDK declarations the game and the platform layer use, in place of
# the SDK's headers (port/include/xdk/README.md)
XDK_INCLUDE = Path("port/include/xdk")


def xdk_headers() -> List[Path]:
    return sorted(XDK_INCLUDE.glob("*.h"))


def game_sources(config: Dict[str, Any]) -> List[Path]:
    """the game's C sources (port.json "game"): every one under its root but
    those excluded"""
    game = config["game"]
    excluded = set(game.get("exclude", []))
    return sorted(
        source for source in Path(game["root"]).rglob("*.c")
        if source.as_posix() not in excluded
    )


def game_defines_and_includes(config: Dict[str, Any]) -> str:
    """the game sources' defines and include directories (port.json "game")"""
    game = config["game"]
    return " ".join(
        [f"-D{define}" for define in game.get("defines", [])]
        + [f"-I{_quote(Path(directory))}" for directory in game.get("include_dirs", [])]
    )


def compile_launcher(sln: Any) -> str:
    """what the native ports' compile commands start with: the
    --compiler-launcher (ccache, say) and a space, or nothing"""
    launcher = getattr(sln, "compiler_launcher", None)
    return f"{launcher} " if launcher else ""

# The optimisation level of every unit, and of link-time optimisation.
OPTIMISATION = "-O2"

# Flags shared by the game and the XDK-facing half of the platform layer.
# They reproduce the MSVC/Xbox ABI the source was written against:
#  - 16-bit wchar_t (UTF-16 strings in tag data and saved games),
#  - MSVC struct layout for 64-bit members (-malign-double),
#  - __declspec, __int64 and calling conventions,
#  - C89 with tentative definitions shared between units (-fcommon),
#  - small structures and unions returned in EAX:EDX, as Win32 does
#    (hs_runtime.c calls union-returning converters through pointers typed
#    as returning long),
#  - no optimisations that assume the absence of MSVC-tolerated UB.
# (the target, first in LINUX_ABI_FLAGS and POSIX_FLAGS)
LINUX_TARGET_FLAGS = ["--target=i686-linux-gnu", "-m32"]
LINUX_ABI_FLAGS = [
    *LINUX_TARGET_FLAGS,
    "-fms-extensions",
    "-fshort-wchar",
    "-malign-double",
    "-fcommon",
    "-fno-pic",
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fno-delete-null-pointer-checks",
    "-freg-struct-return",
    # the game keeps EBP frames (MSVC /Oy-): get_return_eip and the stack
    # walker follow the frame chain
    "-fno-omit-frame-pointer",
    # the same floating point results on every port (every machine in a
    # system link game simulates it from the same inputs): no
    # fused multiply-adds, which -march=native and ARM64 would otherwise
    # emit (port/include/halo_math.h)
    "-ffp-contract=off",
    OPTIMISATION,
    "-g",
    # glibc's wide string functions assume a 32-bit wchar_t; stop clang from
    # turning loops into calls to them (it rewrites a counting loop as
    # wcslen even under -fshort-wchar)
    *(f"-fno-builtin-{name}" for name in (
        "wcslen", "wcsnlen", "wcschr", "wcsrchr", "wcscmp", "wcsncmp", "wcscpy",
        "wcsncpy", "wcscat", "wcsncat", "wmemchr", "wmemcmp", "wmemcpy",
        "wmemmove", "wmemset",
    )),
]

# The game is compiled with glibc restricted to ISO C so that POSIX names
# (random, strnlen, ...) cannot collide with the game's own declarations.
GAME_FLAGS = [
    "-std=gnu89",
    "-D__STRICT_ANSI__",
    "-w",
    "-Wno-error=incompatible-pointer-types",
    "-Wno-error=incompatible-function-pointer-types",
    "-Wno-error=int-conversion",
    "-Wno-error=implicit-function-declaration",
    "-Wno-error=implicit-int",
    "-Wno-error=return-type",
]

# the TOML parser the platform layer reads config.toml with (port_config.c)
TOML_DIR = Path("port/third_party/tomlc17")
EXPAT_DIR = Path("port/third_party/expat")
EXPAT_SOURCES = ("xmlparse.c", "xmlrole.c", "xmltok.c")
KCP_DIR = Path("port/third_party/kcp")
# Link Profile's QR code (port/linux/src/browser.c)
QRCODEGEN_DIR = Path("port/third_party/qrcodegen")
MONOCYPHER_DIR = Path("port/third_party/monocypher")
MUSL_MATH_DIR = Path("port/third_party/musl-math")
# the self-updater's TLS (port/linux/src/posix_update.c)
MBEDTLS_DIR = Path("port/third_party/mbedtls")
STB_DIR = Path("port/third_party/stb")
# internet play's UPnP (port/linux/src/posix_upnp.c)
MINIUPNPC_DIR = Path("port/third_party/miniupnpc")
# miniupnpc's own build's definitions (its Makefile), and a static library
MINIUPNPC_DEFINES = ["-DMINIUPNP_STATICLIB", "-DMINIUPNPC_SET_SOCKET_TIMEOUT", "-DMINIUPNPC_GET_SRC_ADDR",
                     "-D_BSD_SOURCE", "-D_DEFAULT_SOURCE"]


def miniupnpc_sources() -> List[Path]:
    """miniupnpc's library sources (port/third_party/miniupnpc/src)"""
    return sorted((MINIUPNPC_DIR / "src").glob("*.c"))


def updater_defines(release: bool) -> str:
    """the version's defines (port/linux/src/updater.c, the self-updater, has
    them, and gives the version to the rest): the version (tools/version.py),
    whether this build is a release's (only those look for updates), and its
    configuration"""
    flavor = "release" if release else "debug"
    return (f'-DHALO_VERSION=\\"{version()}\\" -DHALO_RELEASE_BUILD={int(release_build())} '
            f'-DHALO_BUILD_FLAVOR=\\"{flavor}\\"')

PLATFORM_FLAGS = [
    "-std=gnu11",
    "-D_GNU_SOURCE",
    "-DHALO_LINUX_PLATFORM_LAYER",
    "-Wall",
    "-Wno-unused-function",
    "-Wno-unknown-pragmas",
    "-Wno-microsoft-anon-tag",
    "-Wno-pragma-pack",
    "-Wno-ignored-attributes",
    "-Wno-duplicate-decl-specifier",
    "-Wno-missing-braces",
    "-Wno-unused-variable",
    "-Wno-ignored-pragmas",
]

# Platform files named posix_*.c talk to glibc only. They are built with the
# host's native ABI (no -malign-double, no 16-bit wchar_t, no XDK headers) so
# glibc structures such as struct stat have their real layout.
POSIX_FLAGS = [
    *LINUX_TARGET_FLAGS,
    "-std=gnu11",
    "-D_GNU_SOURCE",
    "-D_FILE_OFFSET_BITS=64",
    "-fno-pic",
    OPTIMISATION,
    "-g",
    "-Wall",
]

# Units compiled with a profile (-fprofile-use) that does not quite match
# them: new or changed functions simply go without.
PROFILE_USE_FLAGS = [
    "-Wno-profile-instr-unprofiled",
    "-Wno-profile-instr-out-of-date",
    "-Wno-profile-instr-missing",
    "-Wno-backend-plugin",
]


def musl_math_sources() -> List[Path]:
    """musl's maths functions the game uses (port/third_party/musl-math)"""
    return sorted((MUSL_MATH_DIR / "src").glob("*.c"))


def musl_math_cflags(abi: str) -> str:
    """their flags: the game's ABI, and the headers standing in for musl's"""
    return " ".join([abi, "-std=gnu11", "-w", f"-I{MUSL_MATH_DIR}/include",
                     f"-include {MUSL_MATH_DIR}/include/libm.h"])


# Halo PC's Custom Edition maps (maps/ce/<name>.map, played as <name>@ce):
# the port code of source/cache, sound, interface and text and
# port/linux/game/ce_*.c (HALO_CUSTOM_EDITION). The native desktop builds
# (Linux here, macOS: macos_build.py) have them; the Windows and Android
# builds do not yet.
CUSTOM_EDITION_DEFINES = ["-DHALO_CUSTOM_EDITION"]


def game_browser_defines(sln: Any) -> List[str]:
    """configure.py --game-browser: the game list and server browser
    (port/linux/src/browser.c), off in the builds the project ships"""
    return ["-DHALO_GAME_BROWSER"] if getattr(sln, "game_browser", False) else []


def march_flag(sln: Any) -> str:
    """The instruction set of the native x86 builds: this machine's
    (-march=native, the default), or with configure.py --portable the
    baseline every x86-64 processor has (SSE2), for builds that run on other
    computers. The game stays 32-bit code either way."""
    return "-march=x86-64" if getattr(sln, "port_portable", False) else "-march=native"


def lto_mode(sln: Any) -> str:
    """full, thin or off (configure.py --lto)"""
    return getattr(sln, "port_lto", "full")


def lto_flags(sln: Any, cache_dir: Path) -> Tuple[List[str], List[str]]:
    """compiler and linker flags for link-time optimisation with lld"""
    mode = lto_mode(sln)
    if mode == "off":
        return [], []
    flag = "-flto=thin" if mode == "thin" else "-flto=full"
    ldflags = [flag, "-fuse-ld=lld", OPTIMISATION]
    if mode == "thin":
        ldflags.append(f"-Wl,--thinlto-cache-dir={_quote(cache_dir)}")
    return [flag], ldflags


# Profiles recorded by playing the game (tools/pgo_train.py), kept in the
# repository so that every build is optimised with them. They are in the
# format of this LLVM version, which older compilers cannot read.
PGO_DIR = Path("pgo")
LINUX_PROFILE = PGO_DIR / "halo_linux.profdata"
WINDOWS_PROFILE = PGO_DIR / "halo_windows.profdata"
PROFILE_LLVM_MAJOR = 22

_clang_majors: Dict[str, Optional[int]] = {}


def clang_major(cc: str) -> Optional[int]:
    """the major version of the clang named cc, or None if unknown"""
    if cc not in _clang_majors:
        try:
            output = subprocess.run([cc, "--version"], capture_output=True, text=True, check=False).stdout
            match = re.search(r"clang version (\d+)", output)
            _clang_majors[cc] = int(match.group(1)) if match else None
        except OSError:
            _clang_majors[cc] = None
    return _clang_majors[cc]


def pgo_mode(sln: Any) -> str:
    """use, train or off (configure.py --pgo)"""
    return getattr(sln, "port_pgo", "use")


def pgo_profile(sln: Any, own: Optional[Path], others: Sequence[Path], cc: str) -> Optional[Path]:
    """The profile a native build is optimised with: --pgo-profile's; with
    --pgo=train, the build's own profile (trained if it is missing);
    otherwise the first committed profile there is, its own first. None with
    --pgo=off, or when cc is too old to read the profiles."""
    explicit = getattr(sln, "port_pgo_profile", None)
    if explicit:
        return explicit
    mode = pgo_mode(sln)
    if mode == "off":
        return None
    major = clang_major(cc)
    if major is not None and major < PROFILE_LLVM_MAJOR:
        print(f"{cc} is clang {major}; the profiles in {PGO_DIR} need clang {PROFILE_LLVM_MAJOR}: "
              "building without profile-guided optimisation", file=sys.stderr)
        return None
    if mode == "train" and own is not None:
        return own
    for profile in ([own] if own else []) + list(others):
        if profile.is_file():
            return profile
    return None


def profile_use_flags(profile: Any) -> List[str]:
    return [f"-fprofile-use={_quote(profile)}", *PROFILE_USE_FLAGS] if profile else []


def _load_port_config() -> Dict[str, Any]:
    with open(PORT_CONFIG, "r", encoding="utf-8") as f:
        return json.load(f)


def linux_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    if not PORT_CONFIG.is_file():
        return [Path(__file__)]
    # (the folders of the game's sources, so that adding or removing one
    # re-runs it)
    game_folders = sorted({source.parent for source in game_sources(_load_port_config())})
    return [PORT_CONFIG, Path(__file__), PORT_DIR / "src", PORT_DIR / "game", XDK_INCLUDE, *game_folders,
            *hud_configure_inputs()]


def _quote(path: Any) -> str:
    text = str(path).replace(os.sep, "/")
    return f'"{text}"' if " " in text else text


@dataclass
class Linux32Units:
    """What the 32-bit units are built for: `ninja linux` (the defaults) or
    the 32-bit dedicated server (tools/server_build.py)."""
    sln: Any
    config: Dict[str, Any]
    # the game list and the dedicated server (HALO_GAME_BROWSER)
    game_browser: bool
    semantics_header: Path
    platform_semantics_header: Path
    embedded_assets: List[Path]
    # the compile rule
    rule: str = "linux_cc"
    # the target, in place of LINUX_ABI_FLAGS's and POSIX_FLAGS's first two
    target_flags: List[str] = field(default_factory=lambda: list(LINUX_TARGET_FLAGS))
    # more flags for every unit (after the ABI's)
    extra_flags: List[str] = field(default_factory=list)
    # platform units left out ("port/linux/src/name.c")
    excluded: Set[str] = field(default_factory=set)
    # more platform units, with the platform layer's flags (after port/linux/src's)
    platform_sources: List[Path] = field(default_factory=list)
    # more units with the host's ABI, as posix_*.c (after the platform units)
    native_sources: List[Path] = field(default_factory=list)
    # include flags for the platform and host-ABI units (SDL's headers)
    include_flags: List[str] = field(default_factory=list)


def _retarget(flags: List[str], target_flags: List[str]) -> List[str]:
    """flags (LINUX_ABI_FLAGS or POSIX_FLAGS) for another target"""
    assert flags[:len(LINUX_TARGET_FLAGS)] == LINUX_TARGET_FLAGS
    return [*target_flags, *flags[len(LINUX_TARGET_FLAGS):]]


def linux32_objects(n: Writer, units: Linux32Units, obj_dir: Path, extra_cflags: List[str],
                    implicit_inputs: List[Path]) -> List[Path]:
    """the 32-bit units' objects, in link order, with the given extra flags"""
    sln = units.sln
    config = units.config
    prefix_header = PORT_DIR / "include" / "halo_linux_prefix.h"
    semantics_header = units.semantics_header
    platform_semantics_header = units.platform_semantics_header
    browser_defines = ["-DHALO_GAME_BROWSER"] if units.game_browser else []
    abi = " ".join(_retarget(LINUX_ABI_FLAGS, units.target_flags) + [march_flag(sln)]
                   + (["-DHALO_RELEASE"] if getattr(sln, "port_release", False) else [])
                   + browser_defines + CUSTOM_EDITION_DEFINES + units.extra_flags)
    port_include = PORT_DIR / "include"
    sdk_flags = f"-idirafter {XDK_INCLUDE}"
    posix_flags = _retarget(POSIX_FLAGS, units.target_flags)
    extra = " ".join(extra_cflags)
    # the posix_* units have glibc's 32-bit wchar_t, and LLVM will not
    # optimise them together with code that has a 16-bit one: they stay
    # native objects
    posix_extra = " ".join(flag for flag in extra_cflags if not flag.startswith("-flto"))
    objects: List[Path] = []

    def add_object(source: Path, cflags: str, posix: bool = False) -> None:
        obj = obj_dir / source.with_suffix(".o")
        objects.append(obj)
        n.build(
            outputs=obj,
            rule=units.rule,
            inputs=source,
            # (the SDK declarations are system headers, which the depfile
            # leaves out)
            implicit=[*xdk_headers(), prefix_header, semantics_header, platform_semantics_header,
                      *implicit_inputs],
            variables={"cflags": f"{cflags} {posix_extra if posix else extra}"},
        )

    game_cflags = " ".join([
        abi,
        " ".join(GAME_FLAGS),
        f"-include {prefix_header}",
        f"-include {semantics_header}",
        f"-I{port_include}",
        game_defines_and_includes(config),
        sdk_flags,
    ])
    for source in game_sources(config):
        add_object(source, game_cflags)
    # Port-specific units that must see the game exactly as its own
    # sources do (port/linux/game).
    for source in sorted(Path(config["game_sources"]).glob("*.c")):
        add_object(source, game_cflags)
    # the dedicated server's director, with the game browser (server/)
    if units.game_browser:
        for source in sorted(Path("server/src").glob("*.c")):
            add_object(source, game_cflags)

    platform_dir = Path(config["platform_sources"])
    platform_cflags = " ".join([
        abi,
        " ".join(PLATFORM_FLAGS),
        f"-include {prefix_header}",
        f"-include {platform_semantics_header}",
        f"-I{platform_dir}",
        f"-I{port_include}",
        f"-I{TOML_DIR}",
        f"-I{EXPAT_DIR}",
        f"-I{KCP_DIR}",
        f"-I{QRCODEGEN_DIR}",
        f"-I{MONOCYPHER_DIR}",
        "-Isource -Isource/cseries",
        sdk_flags,
        *units.include_flags,
    ])
    posix_cflags = " ".join(posix_flags + [march_flag(sln), f"-I{platform_dir}"] + browser_defines
                            + units.extra_flags + units.include_flags)
    mbedtls_include = f"-I{MBEDTLS_DIR / 'include'}"
    for source in sorted(platform_dir.glob("*.c")):
        if source.as_posix() in units.excluded:
            continue
        if source.name in ("posix_update.c", "posix_browser.c"):
            add_object(source, f"{posix_cflags} {mbedtls_include}", posix=True)
        elif source.name == "posix_upnp.c":
            add_object(source, f"{posix_cflags} -I{MINIUPNPC_DIR / 'include'} -DMINIUPNP_STATICLIB", posix=True)
        elif source.name == "posix_ui_font.c":
            # (the overlay's fonts: stb_truetype; their data, tools/embed_assets.py --fonts)
            add_object(source, f"{posix_cflags} -I{STB_DIR}", posix=True)
        elif source.name.startswith("posix_"):
            add_object(source, posix_cflags, posix=True)
        elif source.name == "updater.c":
            add_object(source, f"{platform_cflags} {updater_defines(getattr(sln, 'port_release', False))}")
        else:
            add_object(source, platform_cflags)
    # (the dedicated server's own: tools/server_build.py)
    for source in units.platform_sources:
        add_object(source, f"{platform_cflags} {updater_defines(getattr(sln, 'port_release', False))}")
    for source in units.embedded_assets:
        add_object(source, platform_cflags)
    for source in units.native_sources:
        add_object(source, posix_cflags, posix=True)
    # the self-updater's TLS (port/third_party/mbedtls), with the host's
    # ABI as the posix_*.c that use it (and no loop turned into glibc's
    # wcslen, which linux_link_check.py rejects: the game's wchar_t is
    # 16-bit)
    for source in sorted((MBEDTLS_DIR / "library").glob("*.c")):
        add_object(source, " ".join(posix_flags + [march_flag(sln), mbedtls_include,
                                                   f"-I{MBEDTLS_DIR / 'library'}", "-fno-builtin-wcslen",
                                                   "-w"]), posix=True)
    # internet play's UPnP (port/third_party/miniupnpc), with the host's
    # ABI as posix_upnp.c, which uses it
    for source in miniupnpc_sources():
        add_object(source, " ".join(posix_flags + [march_flag(sln), *MINIUPNPC_DEFINES,
                                                   f"-I{MINIUPNPC_DIR / 'include'}", f"-I{MINIUPNPC_DIR / 'src'}",
                                                   "-fno-builtin-wcslen", "-w"]), posix=True)
    # the settings file's parser (port/third_party/tomlc17), with the
    # platform layer's ABI (its structs hold doubles) and nothing else
    add_object(TOML_DIR / "tomlc17.c", " ".join([abi, "-std=gnu11", "-w"]))
    # the menus' XML parser (port/third_party/expat; menu_files.c)
    for name in EXPAT_SOURCES:
        add_object(EXPAT_DIR / name, " ".join([abi, "-std=gnu11", f"-I{EXPAT_DIR}", "-w"]))
    # internet play's reliable streams (port/third_party/kcp; p2p.c)
    add_object(KCP_DIR / "ikcp.c", " ".join([abi, "-std=gnu11", "-w"]))
    # Link Profile's QR code (port/third_party/qrcodegen; browser.c)
    add_object(QRCODEGEN_DIR / "qrcodegen.c", " ".join([abi, "-std=gnu11", "-w"]))
    # internet play's signatures, for public games' listings
    # (port/third_party/monocypher; p2p_crypto.c)
    for name in ("monocypher.c", "monocypher-ed25519.c"):
        add_object(MONOCYPHER_DIR / name, " ".join([abi, "-std=gnu11", "-w"]))
    # the game's sin, pow and the rest, the same on every port
    # (port/include/halo_math.h)
    for source in musl_math_sources():
        add_object(source, musl_math_cflags(abi))
    return objects


def generate_linux_build(n: Writer, sln: Any) -> None:
    if not PORT_CONFIG.is_file():
        # a checkout without the port (or a test fixture): nothing to emit
        return
    config = _load_port_config()
    build_dir: Path = sln.build_dir / "linux"
    obj_dir = build_dir / "obj"
    output = build_dir / "halo"
    cc = sln.linux_cc or "clang"
    prefix_header = PORT_DIR / "include" / "halo_linux_prefix.h"
    semantics_header = build_dir / "halo_msvc_semantics.h"
    platform_semantics_header = build_dir / "platform_msvc_semantics.h"

    n.comment("Native Linux build (ninja linux)")
    n.variable("linux_cc", cc)
    n.rule(
        name="linux_msvc_semantics",
        command="$python tools/linux_msvc_semantics.py --output $out $scan",
        description="LINUX MSVC SEMANTICS $out",
        restat=True,
    )
    game_headers = sorted(
        p for p in Path("source").rglob("*") if p.suffix in (".c", ".h")
    )
    # The game sees its own tags and inline functions and those of the SDK
    # declarations (port/include/xdk); the platform layer only includes the
    # SDK declarations and so only needs their inline functions.
    n.build(
        outputs=semantics_header,
        rule="linux_msvc_semantics",
        implicit=[Path("tools/linux_msvc_semantics.py"), *xdk_headers(), *game_headers],
        variables={
            "scan": f"--all-inlines --tags source --inlines source --inlines {XDK_INCLUDE}"
        },
    )
    n.build(
        outputs=platform_semantics_header,
        rule="linux_msvc_semantics",
        implicit=[Path("tools/linux_msvc_semantics.py"), *xdk_headers()],
        variables={"scan": f"--inlines {XDK_INCLUDE}"},
    )
    n.rule(
        name="linux_cc",
        command=f"{compile_launcher(sln)}$linux_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description="LINUX CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="linux_link",
        command=(
            "$linux_cc $ldflags -o $out @$out.rsp $libs"
            " && $python tools/linux_link_check.py $out.rsp $out"
        ),
        description="LINUX LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )

    n.rule(
        name="linux_pgo_train",
        command="$python tools/pgo_train.py --binary $binary --work $work --output $out",
        description="LINUX PGO TRAINING: playing levels in the instrumented build",
        pool="console",
    )

    # the high-res HUD's textures (port/assets/hud; port/linux/src/hud_hires.c)
    embedded_assets = (hud_assets_build(n, "linux", build_dir / "generated" / "hud_hires_assets.c")
                       + ui_fonts_build(n, "linux", build_dir / "generated" / "ui_fonts.c", sln))

    libs = " ".join(f"-l{lib}" for lib in config.get("libraries", []))
    units = Linux32Units(sln=sln, config=config, game_browser=getattr(sln, "game_browser", False),
                         semantics_header=semantics_header, platform_semantics_header=platform_semantics_header,
                         embedded_assets=embedded_assets)

    def emit(obj_dir: Path, output: Path, extra_cflags: List[str], extra_ldflags: List[str],
             implicit_inputs: List[Path]) -> None:
        """the objects and the executable, with the given extra flags"""
        objects = linux32_objects(n, units, obj_dir, extra_cflags, implicit_inputs)
        n.build(
            outputs=output,
            rule="linux_link",
            inputs=objects,
            variables={
                "ldflags": " ".join(["--target=i686-linux-gnu", "-m32", "-no-pie", "-g", *extra_ldflags]),
                "libs": libs,
            },
            implicit=[Path("tools/linux_link_check.py")],
        )


    # Profile-guided optimisation: with the committed profile, or with
    # --pgo=train one that an instrumented build records while playing
    # (tools/pgo_train.py). A profile is trained once: code changed since
    # simply goes without, and deleting it trains a new one.
    profile = pgo_profile(sln, LINUX_PROFILE, [], cc)
    if pgo_mode(sln) == "train" and profile == LINUX_PROFILE:
        instrumented = build_dir / "pgo-generate" / "halo"
        emit(build_dir / "pgo-generate" / "obj", instrumented, ["-fprofile-generate"], ["-fprofile-generate"], [])
        n.build(
            outputs=profile,
            rule="linux_pgo_train",
            order_only=[instrumented],
            variables={"binary": str(instrumented), "work": str(sln.build_dir / "pgo" / "linux")},
        )

    # Link-time optimisation: the objects are LLVM bitcode, optimised and
    # compiled together when lld links them.
    cflags, ldflags = lto_flags(sln, build_dir / "thinlto-cache")
    cflags += profile_use_flags(profile)
    emit(obj_dir, output, cflags, ldflags, [profile] if profile else [])
    # internet play's MQTT brokers, a file beside the game (network.brokers_file)
    brokers = build_dir / "brokers.txt"
    n.rule(name="linux_copy", command="cp $in $out", description="LINUX COPY $out")
    n.build(outputs=brokers, rule="linux_copy", inputs=Path("port/assets/network/brokers.txt"))
    n.build(outputs="linux", rule="phony", inputs=[output, brokers])
    n.newline()
