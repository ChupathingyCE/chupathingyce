"""Ninja rules for the native 64-bit FreeBSD build (``ninja freebsd64``).

The 64-bit Linux build's game and platform layer (tools/linux64_build.py,
tools/lp64_build.py: HALO_64BIT and the `long` rewrite), compiled as native
x86-64 code on FreeBSD with its own libc, the ports' SDL3 and desktop
OpenGL. It links build/freebsd64/halo.

What differs from the Linux build is only the host: FreeBSD's C library
restricted to ISO C (_C99_SOURCE, where glibc is by __STRICT_ANSI__), SDL3
from /usr/local (devel/sdl3), no libdl (dlopen is in libc) and libexecinfo
for backtrace(), FFmpeg built with --target-os=freebsd, and no portable
build (there is no system root to build against: --portable only means
"any x86-64 processor", as everywhere).

This build is not published or tested by the project: it is built by hand,
never updates itself (port/linux/src/updater.c) and has no profile
(the committed profiles are the 32-bit builds').
"""

import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, List

from .embed_assets import hud_assets_build, hud_configure_inputs, ui_fonts_build
from .linux_build import OPTIMISATION, ffmpeg_build, lto_flags, march_flag
from .lp64_build import (
    LINUX_PORT_CONFIG,
    Lp64Build,
    Lp64Host,
    load_json,
    lp64_configure_inputs,
    lp64_excluded,
    lp64_game_flags,
)
from .ninja_syntax import Writer

FREEBSD64_FALLBACK_TARGET = "--target=x86_64-unknown-freebsd"

# FreeBSD's libc restricted to ISO C 1999 (sys/_visible.h: no POSIX, no BSD
# names), as the Linux build restricts glibc with __STRICT_ANSI__: the game's
# own names (random, index...) must not collide with the C library's
FREEBSD64_GAME_FLAGS = lp64_game_flags(["-D_C99_SOURCE"])

# Platform files named posix_*.c talk to the C library only, with the host's
# own ABI and no rewrite (their boundary types are posix.h's).
FREEBSD64_POSIX_FLAGS = [
    "-std=gnu11",
    "-D_GNU_SOURCE",
    "-D_FILE_OFFSET_BITS=64",
    OPTIMISATION,
    "-g",
    "-Wall",
    "-Werror=incompatible-pointer-types",
    "-Werror=int-conversion",
]


def freebsd_target(cc: str) -> str:
    """The compiler's own target (x86_64-unknown-freebsd15.1, say), which keeps
    __FreeBSD__ at the release's value; a generic one if it cannot say."""
    try:
        triple = subprocess.run([cc, "-dumpmachine"], capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return FREEBSD64_FALLBACK_TARGET
    if triple.startswith("x86_64-") and "freebsd" in triple:
        return f"--target={triple}"
    return FREEBSD64_FALLBACK_TARGET


def pkg_config_variable(package: str, variable: str, default: str) -> str:
    """A pkg-config variable of an installed package, or default"""
    try:
        value = subprocess.run(["pkg-config", f"--variable={variable}", package],
                               capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return default
    return value or default


def freebsd64_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    if not LINUX_PORT_CONFIG.is_file():
        return [Path(__file__)]
    return [Path(__file__), *lp64_configure_inputs(), *hud_configure_inputs()]


def generate_freebsd64_build(n: Writer, sln: Any) -> None:
    if not LINUX_PORT_CONFIG.is_file() or platform.system() != "FreeBSD":
        # a checkout without the port (or a test fixture), or another system
        return
    config = load_json(LINUX_PORT_CONFIG)
    build_dir: Path = sln.build_dir / "freebsd64"
    output = build_dir / "halo"
    cc = sln.linux_cc or "clang"
    target = freebsd_target(cc)
    # SDL3 is the ports' (devel/sdl3), under /usr/local, which the compiler
    # and the linker do not search by themselves. After the project's own
    # headers (-idirafter): /usr/local/include has other copies of the
    # libraries the port bundles (zlib, expat)
    include_directory = pkg_config_variable("sdl3", "includedir", "/usr/local/include")
    library_directory = pkg_config_variable("sdl3", "libdir", "/usr/local/lib")

    n.comment("Native 64-bit FreeBSD build (ninja freebsd64)")
    lp64 = Lp64Build(n, sln, "freebsd64", cc)
    n.rule(name="freebsd64_copy", command="cp -L $in $out", description="FREEBSD64 COPY $out")
    # the movies' FFmpeg, for this target (configure.py --without-ffmpeg: the
    # stubs, and the game skips the movies). FFmpeg's makefiles are GNU make's
    # (tools/ffmpeg_build.py runs gmake here)
    ffmpeg_cflags: List[str] = []
    ffmpeg_libs: List[str] = []
    ffmpeg_inputs: List[Path] = []
    if getattr(sln, "with_ffmpeg", True):
        ffmpeg_cflags, ffmpeg_libs, ffmpeg_stamp, ffmpeg_archives = ffmpeg_build(
            n, "freebsd64", build_dir / "ffmpeg", cc, "freebsd", "x86_64", [target])
        ffmpeg_inputs = [ffmpeg_stamp, *ffmpeg_archives]

    # The link check (tools/linux_link_check.py) reads the objects, which
    # link-time optimisation makes LLVM bitcode: only llvm-nm can. Without it
    # the link goes unchecked (weak references that nothing defines would
    # resolve to address 0, and the C library's wide character functions,
    # which assume a 32-bit wchar_t, would go unnoticed).
    link_check = shutil.which("llvm-nm") is not None
    if not link_check:
        print("freebsd64: llvm-nm not found (pkg install llvm): the link check is skipped", file=sys.stderr)
    n.rule(
        name="freebsd64_link",
        command=(
            "$freebsd64_cc $ldflags -o $out @$out.rsp $libs"
            + (" && $python tools/linux_link_check.py $out.rsp $out" if link_check else "")
        ),
        description="FREEBSD64 LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    generated_sources = (hud_assets_build(n, "freebsd64", build_dir / "generated" / "hud_hires_assets.c")
                         + ui_fonts_build(n, "freebsd64", build_dir / "generated" / "ui_fonts.c", sln))
    host = Lp64Host(
        name="freebsd64",
        target_flags=[target, march_flag(sln)],
        game_flags=FREEBSD64_GAME_FLAGS,
        posix_flags=FREEBSD64_POSIX_FLAGS,
        host_include=f"-idirafter {include_directory}",
        # (no loop in them turned into libc's wcslen, which
        # linux_link_check.py rejects: the game's wchar_t is 16-bit)
        third_party_flags=["-fno-builtin-wcslen"],
        excluded=set(lp64_excluded()),
        ffmpeg_cflags=ffmpeg_cflags,
    )
    # Link-time optimisation of the units with the Xbox's ABI, as in the Linux
    # builds (the host-ABI units have a 32-bit wchar_t and stay native objects)
    lto_cflags, lto_ldflags = lto_flags(sln, build_dir / "thinlto-cache")
    objects = lp64.objects(host, generated_sources, build_dir / "obj", lto_cflags, ffmpeg_inputs)
    # (port.json's libraries are Linux's: FreeBSD has no libdl, dlopen and
    # dladdr are in libc, and backtrace() is libexecinfo's)
    libraries = [library for library in config.get("libraries", []) if library != "dl"] + ["execinfo"]
    n.build(
        outputs=output,
        rule="freebsd64_link",
        inputs=objects,
        variables={
            # (not position-independent, as the Linux builds: the addresses of
            # a crash report's calls, in debug.txt, are the executable's own,
            # the same from run to run)
            "ldflags": " ".join([target, "-g", "-no-pie", f"-L{library_directory}", *lto_ldflags]),
            "libs": " ".join([*ffmpeg_libs, *(f"-l{library}" for library in libraries)]),
        },
        implicit=[Path("tools/linux_link_check.py"), *ffmpeg_inputs],
    )
    # internet play's MQTT brokers, a file beside the game (network.brokers_file)
    brokers = build_dir / "brokers.txt"
    n.build(outputs=brokers, rule="freebsd64_copy", inputs=Path("port/assets/network/brokers.txt"))
    n.build(outputs="freebsd64", rule="phony", inputs=[output, brokers])
    n.newline()
