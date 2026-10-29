"""Ninja rules for the native 64-bit builds on the Unix hosts other than Linux
and macOS (``ninja freebsd64``).

The 64-bit Linux build's game and platform layer (tools/linux64_build.py,
tools/lp64_build.py: HALO_64BIT and the `long` rewrite), compiled as native
x86-64 code with the host's own C library, the ports' SDL3 and desktop
OpenGL. It links build/<host>/halo.

What differs from the Linux build is only the host, and all of it is in
port/unix (headers force-included into the units, and the few functions they
declare: nothing of port/linux is edited, but three hooks that name no host)
and in UNIX_HOSTS below: the C library's restriction to ISO C, where SDL3 is,
the libraries to link, FFmpeg's target. A new host is an entry there, and what
port/unix says for it.

A host's builds are published only when its entry says so (published):
FreeBSD's are, built by the GitHub workflow in a FreeBSD virtual machine and
released as freebsd64, which its updater downloads (updater.c). Another host's
are built by hand and never update themselves (configure.py refuses a release
build). None has a profile (the committed profiles are the 32-bit builds').
"""

import os
import platform
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, List, Optional

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
from .version import release_build

UNIX_DIR = Path("port/unix")
UNIX_PREFIX = UNIX_DIR / "include" / "halo_unix_prefix.h"
UNIX_HOST_HEADER = UNIX_DIR / "include" / "halo_unix_host.h"
UNIX_HOST_SOURCE = UNIX_DIR / "src" / "unix_host.c"


@dataclass
class UnixHost:
    # platform.system()
    system: str
    # the target, and the build's folder in build/
    name: str
    # the name of the build identity (build_identity.c) and the logs
    platform_name: str
    # restricts the C library to ISO C for the game's units, as __STRICT_ANSI__
    # (in the Linux flags) does glibc: the game's own names (random, index...)
    # must not collide with the C library's
    c_library_flags: List[str]
    # FFmpeg's --target-os
    ffmpeg_os: str
    # the Delta platform of its builds (delta.h's enum delta_platform)
    delta_platform: str
    # a compiler's target for this host, from what it says it targets, or ""
    triple_marker: str
    # the target when the compiler does not say
    fallback_target: str
    # what port.json's Linux libraries lack or have too many of here
    drop_libraries: List[str] = field(default_factory=list)
    add_libraries: List[str] = field(default_factory=list)
    # how to install what the build needs, for the messages
    install_hint: str = ""
    # whether the project publishes its builds (chupathingyce-<name>-<config>
    # .zip, updater.c's UPDATE_PLATFORM for it): only then may a release
    # build (HALO_RELEASE_BUILD=1, which updates itself) be made
    published: bool = False


UNIX_HOSTS = {
    # FreeBSD's C library restricted to ISO C 1999 (sys/_visible.h: no POSIX,
    # no BSD names), no libdl (dlopen is in libc) and libexecinfo for backtrace()
    "FreeBSD": UnixHost(
        system="FreeBSD",
        name="freebsd64",
        platform_name="FreeBSD",
        c_library_flags=["-D_C99_SOURCE"],
        ffmpeg_os="freebsd",
        delta_platform="_delta_platform_pc_freebsd",
        triple_marker="freebsd",
        fallback_target="--target=x86_64-unknown-freebsd",
        drop_libraries=["dl"],
        add_libraries=["execinfo"],
        install_hint="pkg install llvm",
        # (built by the GitHub workflow in a FreeBSD virtual machine, and
        # released and updated as "freebsd64")
        published=True,
    ),
}


def unix_host() -> Optional[UnixHost]:
    """This computer's entry in UNIX_HOSTS, or None (Linux, macOS, Windows...)"""
    return UNIX_HOSTS.get(platform.system())


# Platform files named posix_*.c talk to the C library only, with the host's
# own ABI and no rewrite (their boundary types are posix.h's).
UNIX_POSIX_FLAGS = [
    "-std=gnu11",
    "-D_GNU_SOURCE",
    "-D_FILE_OFFSET_BITS=64",
    OPTIMISATION,
    "-g",
    "-Wall",
    "-Werror=incompatible-pointer-types",
    "-Werror=int-conversion",
]


def host_target(host: UnixHost, cc: str) -> str:
    """The compiler's own target (x86_64-unknown-freebsd15.1, say), which keeps
    __FreeBSD__ at the release's value; a generic one if it cannot say."""
    try:
        triple = subprocess.run([cc, "-dumpmachine"], capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return host.fallback_target
    if triple.startswith("x86_64-") and host.triple_marker in triple:
        return f"--target={triple}"
    return host.fallback_target


def pkg_config_variable(package: str, variable: str, default: str) -> str:
    """A pkg-config variable of an installed package, or default"""
    try:
        value = subprocess.run(["pkg-config", f"--variable={variable}", package],
                               capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return default
    return value or default


def find_llvm_nm() -> Optional[str]:
    """llvm-nm, which reads the LLVM bitcode of a link-time optimised build:
    the plain name, a versioned one (llvm-nm19: the packages' on some hosts), or
    an `nm` that is LLVM's"""
    found = shutil.which("llvm-nm")
    if found:
        return found
    versioned = sorted({str(path) for directory in os.environ.get("PATH", "").split(os.pathsep) if directory
                        for path in Path(directory).glob("llvm-nm[0-9]*")}, reverse=True)
    if versioned:
        return versioned[0]
    try:
        version = subprocess.run(["nm", "--version"], capture_output=True, text=True).stdout
    except OSError:
        return None
    return shutil.which("nm") if "LLVM" in version else None


def unix64_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    if not LINUX_PORT_CONFIG.is_file():
        return [Path(__file__)]
    return [Path(__file__), *lp64_configure_inputs(), *hud_configure_inputs(), UNIX_PREFIX, UNIX_HOST_HEADER,
            UNIX_HOST_SOURCE]


def generate_unix64_build(n: Writer, sln: Any) -> None:
    host = unix_host()
    if host is None or not LINUX_PORT_CONFIG.is_file():
        # another system, or a checkout without the port (or a test fixture)
        return
    if release_build() and not host.published:
        # (updater.c would download a build the project does not publish)
        sys.exit(f"configure.py: HALO_RELEASE_BUILD=1 makes a build that updates itself, and the project publishes "
                 f"none for {host.platform_name}: unset it")
    config = load_json(LINUX_PORT_CONFIG)
    name = host.name
    build_dir: Path = sln.build_dir / name
    output = build_dir / "halo"
    cc = sln.linux_cc or "clang"
    target = host_target(host, cc)
    # SDL3 is the ports' (devel/sdl3), under /usr/local, which the compiler
    # and the linker do not search by themselves. After the project's own
    # headers (-idirafter): /usr/local/include has other copies of the
    # libraries the port bundles (zlib, expat)
    include_directory = pkg_config_variable("sdl3", "includedir", "/usr/local/include")
    library_directory = pkg_config_variable("sdl3", "libdir", "/usr/local/lib")

    n.comment(f"Native 64-bit {host.platform_name} build (ninja {name})")
    lp64 = Lp64Build(n, sln, name, cc)
    n.rule(name=f"{name}_copy", command="cp -L $in $out", description=f"{name.upper()} COPY $out")
    # the movies' FFmpeg, for this target (configure.py --without-ffmpeg: the
    # stubs, and the game skips the movies)
    ffmpeg_cflags: List[str] = []
    ffmpeg_libs: List[str] = []
    ffmpeg_inputs: List[Path] = []
    if getattr(sln, "with_ffmpeg", True):
        ffmpeg_cflags, ffmpeg_libs, ffmpeg_stamp, ffmpeg_archives = ffmpeg_build(
            n, name, build_dir / "ffmpeg", cc, host.ffmpeg_os, "x86_64", [target])
        ffmpeg_inputs = [ffmpeg_stamp, *ffmpeg_archives]

    # The link check (tools/linux_link_check.py) reads the objects, which
    # link-time optimisation makes LLVM bitcode: only llvm-nm can. It is
    # required: without it weak references that nothing defines would resolve
    # to address 0, and the C library's wide character functions, which assume
    # a 32-bit wchar_t, would go unnoticed. (--lto=off leaves objects any nm
    # reads.)
    llvm_nm = find_llvm_nm()
    if llvm_nm is None and getattr(sln, "port_lto", "full") == "off" and shutil.which("nm"):
        llvm_nm = shutil.which("nm")
    if llvm_nm is None:
        sys.exit(f"{name}: llvm-nm not found ({host.install_hint}), and the link check needs it "
                 "(or --lto=off with any nm)")
    n.rule(
        name=f"{name}_link",
        command=(
            f"${name}_cc $ldflags -o $out @$out.rsp $libs"
            f" && LLVM_NM={llvm_nm} $python tools/linux_link_check.py $out.rsp $out"
        ),
        description=f"{name.upper()} LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    generated_sources = (hud_assets_build(n, name, build_dir / "generated" / "hud_hires_assets.c")
                         + ui_fonts_build(n, name, build_dir / "generated" / "ui_fonts.c", sln))
    lp64_host = Lp64Host(
        name=name,
        target_flags=[target, march_flag(sln)],
        # (the game's units: the C library's restriction, and port/unix's prefix, ahead of halo_linux_prefix.h)
        game_flags=lp64_game_flags([*host.c_library_flags, "-include", str(UNIX_PREFIX)]),
        # (the platform units')
        platform_flags=["-include", str(UNIX_PREFIX), "-include", str(UNIX_HOST_HEADER),
                        f'-DHALO_UNIX_PLATFORM_NAME=\\"{host.platform_name}\\"',
                        f"-DHALO_UNIX_DELTA_PLATFORM={host.delta_platform}"],
        posix_flags=[*UNIX_POSIX_FLAGS, "-include", str(UNIX_HOST_HEADER),
                     f'-DHALO_UNIX_PLATFORM_NAME=\\"{host.platform_name}\\"'],
        host_include=f"-idirafter {include_directory}",
        # (no loop in them turned into libc's wcslen, which
        # linux_link_check.py rejects: the game's wchar_t is 16-bit)
        third_party_flags=["-fno-builtin-wcslen"],
        excluded=set(lp64_excluded()),
        host_sources=[UNIX_HOST_SOURCE],
        ffmpeg_cflags=ffmpeg_cflags,
    )
    # Link-time optimisation of the units with the Xbox's ABI, as in the Linux
    # builds (the host-ABI units have a 32-bit wchar_t and stay native objects)
    lto_cflags, lto_ldflags = lto_flags(sln, build_dir / "thinlto-cache")
    objects = lp64.objects(lp64_host, generated_sources, build_dir / "obj", lto_cflags, ffmpeg_inputs)
    # (port.json's libraries are Linux's)
    libraries = [library for library in config.get("libraries", []) if library not in host.drop_libraries]
    libraries += host.add_libraries
    n.build(
        outputs=output,
        rule=f"{name}_link",
        inputs=objects,
        variables={
            # (not position-independent, as the Linux builds: the addresses of
            # a crash report's calls, in debug.txt, are the executable's own,
            # the same from run to run)
            "ldflags": " ".join([target, "-g", f"-L{library_directory}", *lto_ldflags]),
            "libs": " ".join([*ffmpeg_libs, *(f"-l{library}" for library in libraries)]),
        },
        implicit=[Path("tools/linux_link_check.py"), *ffmpeg_inputs],
    )
    # internet play's MQTT brokers, a file beside the game (network.brokers_file)
    brokers = build_dir / "brokers.txt"
    n.build(outputs=brokers, rule=f"{name}_copy", inputs=Path("port/assets/network/brokers.txt"))
    n.build(outputs=name, rule="phony", inputs=[output, brokers])
    n.newline()
