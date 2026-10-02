"""Ninja rules for the native iOS build (``ninja ios``).

The macOS build (tools/macos_build.py) compiled for iPhoneOS: the same
64-bit game (HALO_64BIT, the LP64 rewrite) and Linux platform layer, with
OpenGL ES 3 in place of desktop OpenGL (HALO_GLES, the ES paths written for
Android) and SDL 3 built for iOS. The game's main() becomes halo_main(),
which the app (port/ios/app) calls once SDL has started UIKit.

The result is build/ios/libhalo.a, which the Xcode project in port/ios/app
links into Halo.app. See port/ios/README.md.
"""

import os
import platform
import subprocess
from pathlib import Path
from typing import Any, List

from .linux_build import (
    KCP_DIR,
    MBEDTLS_DIR,
    MINIUPNPC_DEFINES,
    MINIUPNPC_DIR,
    MUSL_MATH_DIR,
    OPTIMISATION,
    TOML_DIR,
    XDK_INCLUDE,
    compile_launcher,
    game_sources,
    miniupnpc_sources,
    musl_math_sources,
    updater_defines,
)
from .macos_build import (
    LINUX_PORT_CONFIG,
    LINUX_PORT_DIR,
    MACOS_ABI_FLAGS,
    MACOS_GAME_FLAGS,
    MACOS_PLATFORM_FLAGS,
    MACOS_POSIX_FLAGS,
    _load,
    _quote,
    _rewritten_inputs,
)
from .embed_assets import hud_assets_build, hud_configure_inputs
from .ninja_syntax import Writer

PORT_DIR = Path("port/ios")
PORT_CONFIG = PORT_DIR / "port.json"

IOS_MINIMUM = "26.0"

# SDL 3 built for iPhoneOS and for the simulator (port/ios/README.md)
SDL3_IOS = Path(os.environ.get("HALO_IOS_SDL3", Path.home() / "dev" / "sdl3-ios"))
SDL3_IOS_SIMULATOR = Path(os.environ.get("HALO_IOS_SIMULATOR_SDL3", Path.home() / "dev" / "sdl3-ios-sim"))

# (ninja target, SDK, target triple suffix, SDL): the phone, and the
# simulator, where the game can be tried and debugged on the Mac
VARIANTS = [
    ("ios", "iphoneos", "", SDL3_IOS),
    ("ios-sim", "iphonesimulator", "-simulator", SDL3_IOS_SIMULATOR),
]

# the game's main(), which SDL's UIKit start-up calls through halo_main
MAIN_SOURCE = Path("source/shell/shell_xbox.c")


def ios_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    if not PORT_CONFIG.is_file():
        return [Path(__file__)]
    return [PORT_CONFIG, Path(__file__), LINUX_PORT_CONFIG, LINUX_PORT_DIR / "src", LINUX_PORT_DIR / "game",
            *hud_configure_inputs()]


def _sdk_path(sdk_name: str) -> str:
    try:
        return subprocess.run(["xcrun", "--sdk", sdk_name, "--show-sdk-path"], check=True,
                              capture_output=True, text=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def generate_ios_build(n: Writer, sln: Any) -> None:
    if not PORT_CONFIG.is_file() or platform.system() != "Darwin":
        return
    n.comment("Native iOS build (ninja ios, ninja ios-sim)")
    n.variable("ios_cc", getattr(sln, "ios_cc", None) or "clang")
    _ios_rules(n, sln)
    for name, sdk_name, suffix, sdl in VARIANTS:
        sdk = _sdk_path(sdk_name)
        if sdk:
            _ios_variant(n, sln, name, sdk, suffix, sdl)
    n.newline()


def _ios_rules(n: Writer, sln: Any) -> None:
    n.rule(
        name="ios_lp64",
        command="$python tools/lp64_rewrite.py --output $out $in",
        description="IOS LP64 $in",
        restat=True,
    )
    n.rule(
        name="ios_msvc_semantics",
        command="$python tools/linux_msvc_semantics.py --output $out $scan",
        description="IOS MSVC SEMANTICS $out",
        restat=True,
    )
    n.rule(
        name="ios_cc",
        command=f"{compile_launcher(sln)}$ios_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description="IOS CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="ios_archive",
        # (ninja quotes paths with spaces in the response file; -filelist
        # takes each line as it is)
        command="rm -f $out && sed -e \"s/^'//\" -e \"s/'$$//\" $out.rsp > $out.list && "
                "xcrun libtool -static -no_warning_for_no_symbols -o $out -filelist $out.list",
        description="IOS LIBTOOL $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )


def _ios_variant(n: Writer, sln: Any, name: str, sdk: str, suffix: str, sdl: Path) -> None:
    config = _load(PORT_CONFIG)
    linux_config = _load(LINUX_PORT_CONFIG)
    build_dir: Path = sln.build_dir / name
    obj_dir = build_dir / "obj"
    lp64_dir = build_dir / "lp64"
    output = build_dir / "libhalo.a"
    target = f"--target=arm64-apple-ios{IOS_MINIMUM}{suffix} -isysroot {_quote(sdk)}"

    def lp64(path: Path) -> Path:
        return lp64_dir / path

    rewritten = []
    for source in _rewritten_inputs():
        n.build(outputs=lp64(source), rule="ios_lp64", inputs=source, implicit=[Path("tools/lp64_rewrite.py")])
        rewritten.append(lp64(source))
    n.build(outputs=build_dir / "lp64.stamp", rule="phony", inputs=rewritten)

    semantics_header = build_dir / "halo_msvc_semantics.h"
    platform_semantics_header = build_dir / "platform_msvc_semantics.h"
    game_headers = sorted(p for p in Path("source").rglob("*") if p.suffix in (".c", ".h"))
    n.build(outputs=semantics_header, rule="ios_msvc_semantics",
            implicit=[Path("tools/linux_msvc_semantics.py"), *game_headers, *sorted(XDK_INCLUDE.glob("*.h"))],
            variables={"scan": f"--all-inlines --tags source --inlines source --inlines {XDK_INCLUDE}"})
    n.build(outputs=platform_semantics_header, rule="ios_msvc_semantics",
            implicit=[Path("tools/linux_msvc_semantics.py"), *sorted(XDK_INCLUDE.glob("*.h"))],
            variables={"scan": f"--inlines {XDK_INCLUDE}"})
    xdk = _quote(lp64(XDK_INCLUDE))

    release = ["-DHALO_RELEASE"] if getattr(sln, "port_release", False) else []
    abi = " ".join([target, *MACOS_ABI_FLAGS, "-DHALO_IOS", "-DHALO_GLES", *release])
    prefix_header = lp64(LINUX_PORT_DIR / "include" / "halo_linux_prefix.h")
    port_include = lp64(LINUX_PORT_DIR / "include")
    sdl_include = f"-idirafter {_quote(sdl / 'include')}"
    excluded = set(config.get("exclude_sources", []))
    objects: List[Path] = []

    def add_object(source: Path, cflags: str) -> None:
        relative = source.relative_to(lp64_dir) if lp64_dir in source.parents else source
        obj = obj_dir / relative.with_suffix(".o")
        objects.append(obj)
        n.build(outputs=obj, rule="ios_cc", inputs=source,
                implicit=[semantics_header, platform_semantics_header],
                order_only=[build_dir / "lp64.stamp"],
                variables={"cflags": cflags})

    game = linux_config["game"]
    defines = " ".join(f"-D{d}" for d in game.get("defines", []))
    includes = " ".join(f"-I{_quote(lp64(Path(d)))}" for d in game.get("include_dirs", []))
    game_cflags = " ".join([
        abi, " ".join(MACOS_GAME_FLAGS),
        f"-include {_quote(prefix_header)}", f"-include {_quote(semantics_header)}",
        defines, f"-I{_quote(port_include)}", includes, f"-idirafter {xdk}",
    ])
    for source in game_sources(linux_config):
        if source.as_posix() in excluded:
            continue
        add_object(lp64(source), f"{game_cflags} -Dmain=halo_main" if source == MAIN_SOURCE else game_cflags)
    for source in sorted(Path(linux_config["game_sources"]).glob("*.c")):
        if source.as_posix() not in excluded:
            add_object(lp64(source), game_cflags)

    platform_dir = Path(linux_config["platform_sources"])
    platform_cflags = " ".join([
        abi, " ".join(MACOS_PLATFORM_FLAGS),
        f"-include {_quote(prefix_header)}", f"-include {_quote(platform_semantics_header)}",
        f"-I{_quote(lp64(platform_dir))}", f"-I{_quote(port_include)}",
        f"-I{_quote(lp64(TOML_DIR))}", f"-I{_quote(lp64(KCP_DIR))}",
        f"-I{_quote(lp64(Path('source')))} -I{_quote(lp64(Path('source/cseries')))}",
        sdl_include, f"-idirafter {xdk}",
    ])
    posix_cflags = " ".join([target, *MACOS_POSIX_FLAGS, "-DHALO_IOS", f"-I{platform_dir}", sdl_include])
    mbedtls_include = f"-I{MBEDTLS_DIR / 'include'}"
    for source in sorted(platform_dir.glob("*.c")):
        if source.as_posix() in excluded:
            continue
        if source.name == "posix_update.c":
            add_object(source, f"{posix_cflags} {mbedtls_include}")
        elif source.name == "posix_upnp.c":
            add_object(source, f"{posix_cflags} -I{MINIUPNPC_DIR / 'include'} -DMINIUPNP_STATICLIB")
        elif source.name.startswith("posix_"):
            add_object(source, posix_cflags)
        elif source.name == "updater.c":
            add_object(lp64(source), f"{platform_cflags} {updater_defines(getattr(sln, 'port_release', False))}")
        else:
            add_object(lp64(source), platform_cflags)
    for source in hud_assets_build(n, name, build_dir / "generated" / "hud_hires_assets.c"):
        add_object(source, platform_cflags)
    # iOS-only platform units, with the host's ABI (port/ios/src)
    for source in sorted((PORT_DIR / "src").glob("*.c")):
        add_object(source, posix_cflags)
    for source in sorted((MBEDTLS_DIR / "library").glob("*.c")):
        add_object(source, " ".join([target, "-std=gnu11", OPTIMISATION, "-g", "-w", mbedtls_include,
                                     f"-I{MBEDTLS_DIR / 'library'}"]))
    for source in miniupnpc_sources():
        add_object(source, " ".join([target, "-std=gnu11", OPTIMISATION, "-g", "-w", *MINIUPNPC_DEFINES,
                                     f"-I{MINIUPNPC_DIR / 'include'}", f"-I{MINIUPNPC_DIR / 'src'}"]))
    third_party = " ".join([abi, "-std=gnu11", "-w"])
    add_object(lp64(TOML_DIR / "tomlc17.c"), third_party)
    add_object(lp64(KCP_DIR / "ikcp.c"), third_party)
    for source in musl_math_sources():
        add_object(source, " ".join([abi, "-std=gnu11", "-w", f"-I{MUSL_MATH_DIR}/include",
                                     f"-include {MUSL_MATH_DIR}/include/libm.h"]))

    n.build(outputs=output, rule="ios_archive", inputs=objects)
    n.build(outputs=name, rule="phony", inputs=[output])
