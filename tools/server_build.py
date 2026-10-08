"""Ninja rules for ChupathingyCE's dedicated server (``ninja server``,
``ninja server-x86``, ``ninja server-x64``, ``ninja server-arm64``).

The server (server/README.md) is the game itself, the same sources and
network code as the Linux builds (tools/linux_build.py for x86,
tools/lp64_build.py for x64 and arm64), with the dedicated server's director
(server/src) and without anything a player sits in front of: SDL's window,
input and sound are replaced by server/platform's headless units, so the
program links only the C library. It needs SDL's headers to compile (the
renderer's OpenGL declarations, which it never calls), fetched once from
SDL's release (SDL_TARBALL_URL), never its library.

Linked against musl (Alpine Linux, as tools/ci_build.py --alpine builds it, or
any musl host), the server is one static executable that runs on any Linux
of its architecture: no libraries, no loader, no glibc version to match.
Against glibc (a developer's machine) it is an ordinary dynamically linked
program that needs only the C library.

Targets, where this machine can link them:
  server-x86    32-bit x86 (i686 and up, SSE2): an x86 host, or an x86-64
                glibc host with 32-bit glibc (as ``ninja linux``)
  server-x64    x86-64: an x86-64 host
  server-arm64  64-bit ARM (ARMv8-A): an arm64 host
  server        every one of them this machine can link
Each writes build/server-<arch>/chupathingyce-server.
"""

import glob
import hashlib
import platform
import sys
import tarfile
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from types import SimpleNamespace
from typing import Any, Dict, List, Optional

from .embed_assets import hud_assets_build, hud_configure_inputs, ui_fonts_build
from .embed_webui import webui_inputs
from .linux64_build import LINUX64_GAME_FLAGS, LINUX64_POSIX_FLAGS
from .linux_build import (
    MBEDTLS_DIR,
    MONOCYPHER_DIR,
    PORT_CONFIG,
    QRCODEGEN_DIR,
    Linux32Units,
    _load_port_config,
    compile_launcher,
    linux32_objects,
    lto_flags,
    march_flag,
)
from .lp64_build import Lp64Build, Lp64Host, lp64_configure_inputs, lp64_excluded
from .ninja_syntax import Writer

SERVER_DIR = Path("server")
SERVER_PLATFORM_DIR = SERVER_DIR / "platform"
# the server's own platform units, with the Xbox's ABI (they stand in for
# the ones it leaves out)
SERVER_PLATFORM_SOURCES = [SERVER_PLATFORM_DIR / "server_platform.c", SERVER_PLATFORM_DIR / "server_input.c"]
# and with the host's ABI: SDL's few utility functions, glibc's backtrace,
# and the console, control API and web admin page, and their HTTPS (server/docs/admin.md;
# the page's files are embedded by tools/embed_webui.py)
SERVER_NATIVE_SOURCES = [SERVER_PLATFORM_DIR / "sdl_headless.c", SERVER_PLATFORM_DIR / "backtrace.c",
                         SERVER_PLATFORM_DIR / "control_protocol.c", SERVER_PLATFORM_DIR / "control_web.c",
                         SERVER_PLATFORM_DIR / "control_tls.c", SERVER_PLATFORM_DIR / "server_control.c",
                         SERVER_PLATFORM_DIR / "control_roles.c", SERVER_PLATFORM_DIR / "control_accounts.c",
                         SERVER_PLATFORM_DIR / "server_roles.c", SERVER_PLATFORM_DIR / "control_link_protocol.c",
                         SERVER_PLATFORM_DIR / "control_link.c", SERVER_PLATFORM_DIR / "server_events.c"]
SERVER_WEBUI_DIR = SERVER_DIR / "webui"
# the window, input and self-updater the server has none of
SERVER_EXCLUDED = {
    "port/linux/src/sdl_platform.c",
    "port/linux/src/xinput_sdl.c",
    "port/linux/src/updater.c",
}
SERVER_DEFINES = ["-DHALO_SERVER"]
OUTPUT_NAME = "chupathingyce-server"

# SDL's headers (its release's source, the same version as the other ports'
# SDL: android_build.SDL_TAG); only include/ and the license are unpacked
SDL_VERSION = "3.4.16"
SDL_TARBALL_URL = f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL_VERSION}/SDL3-{SDL_VERSION}.tar.gz"
SDL_TARBALL_SHA256 = "7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68"
THIRD_PARTY = Path("build/server/third_party")
SDL_HEADERS_DIR = THIRD_PARTY / f"SDL3-{SDL_VERSION}"
SDL_INCLUDE = SDL_HEADERS_DIR / "include"

# a static program's threads get the stack glibc's do (musl's default is
# 128 KB; it takes its default from the executable's PT_GNU_STACK)
STATIC_LDFLAGS = ["-static", "-Wl,-z,stack-size=8388608"]


@dataclass
class ServerArch:
    # x86, x64, arm64: the target's and the download's name
    name: str
    # the native 64-bit build's machinery (tools/lp64_build.py), or the
    # 32-bit one's (tools/linux_build.py)
    lp64: bool
    # platform.machine()'s names for it
    machines: List[str]
    # the target with glibc (as the other Linux builds give it); with musl,
    # the compiler's own (an Alpine host's), and only on its architecture
    gnu_target: List[str]
    musl_target: List[str]
    # every unit's further flags
    flags: List[str]


SERVER_ARCHES: Dict[str, ServerArch] = {
    "x86": ServerArch("x86", False, ["i386", "i486", "i586", "i686", "x86"],
                      ["--target=i686-linux-gnu", "-m32"], ["-m32"], []),
    "x64": ServerArch("x64", True, ["x86_64", "amd64"], ["--target=x86_64-linux-gnu"], [], []),
    # (char is unsigned on ARM Linux; the game, as MSVC, has it signed, as
    # every other build does)
    "arm64": ServerArch("arm64", True, ["aarch64", "arm64"], ["--target=aarch64-linux-gnu"], [], ["-fsigned-char"]),
}


def host_arch() -> Optional[str]:
    """this machine's architecture, by the server's names (None: none): its
    C library's, which for musl is its loader's name (a 32-bit system on a
    64-bit kernel, an i386 container, says x86_64 in uname)"""
    loaders = glob.glob("/lib/ld-musl-*.so.1")
    machine = (loaders[0][len("/lib/ld-musl-"):-len(".so.1")] if loaders else platform.machine()).lower()
    for arch in SERVER_ARCHES.values():
        if machine in arch.machines:
            return arch.name
    return None


def host_libc() -> str:
    """musl or glibc"""
    return "musl" if glob.glob("/lib/ld-musl-*.so.1") else "glibc"


def buildable_arches() -> List[str]:
    """the servers this machine can link (Linux only)"""
    if not sys.platform.startswith("linux"):
        return []
    arch = host_arch()
    if arch is None:
        return []
    if host_libc() == "glibc" and arch == "x64":
        # (the 32-bit one with the 32-bit glibc ``ninja linux`` uses)
        return ["x86", "x64"]
    return [arch]


def server_march(arch: ServerArch, sln: Any) -> str:
    """the instruction set: the x86 builds' (march_flag), or for arm64 the
    first 64-bit ARM (a Raspberry Pi 3 or 4) with --portable, this machine's
    otherwise"""
    if arch.name == "arm64":
        return "-march=armv8-a" if getattr(sln, "port_portable", False) else "-mcpu=native"
    return march_flag(sln)


def fetch_sdl_headers() -> bool:
    """SDL's headers (once): whether they are there"""
    if (SDL_INCLUDE / "SDL3" / "SDL.h").is_file():
        return True
    THIRD_PARTY.mkdir(parents=True, exist_ok=True)
    archive = THIRD_PARTY / f"SDL3-{SDL_VERSION}.tar.gz"
    print(f"Downloading {SDL_TARBALL_URL} (its headers, for the server)")
    try:
        with urllib.request.urlopen(SDL_TARBALL_URL) as response, open(archive, "wb") as f:
            f.write(response.read())
    except OSError as error:
        print(f"Server build disabled: cannot fetch SDL's headers ({error})", file=sys.stderr)
        return False
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != SDL_TARBALL_SHA256:
        archive.unlink()
        raise SystemExit(f"{SDL_TARBALL_URL}: SHA-256 {digest}, not the pinned {SDL_TARBALL_SHA256}")
    prefix = f"SDL3-{SDL_VERSION}/"
    with tarfile.open(archive) as tar:
        members = [m for m in tar.getmembers()
                   if m.isfile() and (m.name.startswith(prefix + "include/") or m.name == prefix + "LICENSE.txt")]
        # (only regular files under the prefix; and the extraction filter
        # where this Python has it)
        if hasattr(tarfile, "data_filter"):
            tar.extractall(THIRD_PARTY, members=members, filter="data")
        else:
            tar.extractall(THIRD_PARTY, members=members)
    archive.unlink()
    return (SDL_INCLUDE / "SDL3" / "SDL.h").is_file()


def server_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    if not PORT_CONFIG.is_file():
        return [Path(__file__)]
    return [Path(__file__), SERVER_PLATFORM_DIR, SERVER_WEBUI_DIR, Path("tools/embed_webui.py"),
            *lp64_configure_inputs(), *hud_configure_inputs()]


def webui_build(n: Writer, prefix: str, output: Path) -> List[Path]:
    """Emits the rule that embeds the web admin page's files
    (tools/embed_webui.py); returns [output]."""
    n.rule(
        name=f"{prefix}_embed_webui",
        command="$python tools/embed_webui.py $out",
        description=f"{prefix.upper()} EMBED $out",
    )
    n.build(outputs=output, rule=f"{prefix}_embed_webui", implicit=[Path("tools/embed_webui.py"), *webui_inputs()])
    return [output]


def _link_rule(n: Writer, name: str, static: bool) -> None:
    check = "tools/linux_link_check.py" + (" --static" if static else "")
    n.rule(
        name=f"{name}_link",
        command=f"${name}_cc $ldflags -o $out @$out.rsp $libs && $python {check} $out.rsp $out",
        description=f"{name.upper()} LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )


def _link_flags(static: bool, target: List[str], lto_ldflags: List[str]) -> str:
    # (not position-independent, as the other Linux builds: a crash report's
    # addresses are the executable's own; a static one is not anyway)
    return " ".join([*target, "-g", *(STATIC_LDFLAGS if static else ["-no-pie"]), *lto_ldflags])


def generate_server_build(n: Writer, sln: Any) -> None:
    if not PORT_CONFIG.is_file():
        # a checkout without the port (or a test fixture): nothing to emit
        return
    arches = buildable_arches()
    if not arches or not fetch_sdl_headers():
        return
    config = _load_port_config()
    libc = host_libc()
    static = libc == "musl"
    # (the game's 16-bit wchar_t over musl's: port/linux/include/halo_linux_prefix.h)
    defines = [*SERVER_DEFINES, *(["-DHALO_MUSL"] if static else [])]
    cc = sln.linux_cc or "clang"
    # the server always has the game list and the dedicated server's director
    server_sln = SimpleNamespace(**vars(sln))
    server_sln.game_browser = True
    libs = "-lm -lpthread"
    # (SDL's headers; Monocypher's, for the control API's credentials; Mbed
    # TLS's, for its HTTPS beyond the machine: control_tls.c, whose library
    # the game list's requests already link)
    include_flags = [f"-I{SDL_INCLUDE}", f"-I{MONOCYPHER_DIR}", f"-I{MBEDTLS_DIR / 'include'}", f"-I{QRCODEGEN_DIR}"]

    n.comment(f"The dedicated server (ninja server; tools/server_build.py): {', '.join(arches)}, "
              f"{'static, musl' if static else 'glibc'}")
    outputs = []
    for name in arches:
        arch = SERVER_ARCHES[name]
        rule_name = f"server-{name}"
        build_dir: Path = sln.build_dir / f"server-{name}"
        output = build_dir / OUTPUT_NAME
        target = arch.musl_target if static else arch.gnu_target
        march = server_march(arch, sln)
        lto_cflags, lto_ldflags = lto_flags(sln, build_dir / "thinlto-cache")
        generated = (hud_assets_build(n, rule_name, build_dir / "generated" / "hud_hires_assets.c")
                     + ui_fonts_build(n, rule_name, build_dir / "generated" / "ui_fonts.c", server_sln))
        # (the web admin page's files: plain bytes, with the host's ABI as
        # control_web.c, which serves them)
        native_sources = SERVER_NATIVE_SOURCES + webui_build(n, rule_name, build_dir / "generated" / "webui_assets.c")

        if arch.lp64:
            lp64 = Lp64Build(n, server_sln, rule_name, cc, extra_roots=[SERVER_PLATFORM_DIR])
            host = Lp64Host(
                name=rule_name,
                target_flags=[*target, march, *arch.flags, *defines],
                game_flags=LINUX64_GAME_FLAGS,
                posix_flags=LINUX64_POSIX_FLAGS,
                host_include=" ".join(include_flags),
                third_party_flags=["-fno-builtin-wcslen"],
                excluded=set(lp64_excluded()) | SERVER_EXCLUDED,
                host_sources=native_sources,
                platform_sources=SERVER_PLATFORM_SOURCES,
            )
            objects = lp64.objects(host, generated, build_dir / "obj", lto_cflags)
        else:
            n.variable(f"{rule_name}_cc", cc)
            n.rule(
                name=f"{rule_name}_cc",
                command=f"{compile_launcher(sln)}${rule_name}_cc -MMD -MF $out.d $cflags -c $in -o $out",
                description=f"{rule_name.upper()} CC $out",
                depfile="$out.d",
                deps="gcc",
            )
            # (the 32-bit game's MSVC semantics headers, ``ninja linux``'s)
            linux_dir = sln.build_dir / "linux"
            units = Linux32Units(
                sln=server_sln, config=config, game_browser=True,
                semantics_header=linux_dir / "halo_msvc_semantics.h",
                platform_semantics_header=linux_dir / "platform_msvc_semantics.h",
                embedded_assets=generated, rule=f"{rule_name}_cc", target_flags=target,
                extra_flags=[*arch.flags, *defines], excluded=SERVER_EXCLUDED,
                platform_sources=SERVER_PLATFORM_SOURCES, native_sources=native_sources,
                include_flags=include_flags,
            )
            objects = linux32_objects(n, units, build_dir / "obj", lto_cflags, [])
        _link_rule(n, rule_name, static)
        n.build(
            outputs=output,
            rule=f"{rule_name}_link",
            inputs=objects,
            variables={"ldflags": _link_flags(static, target, lto_ldflags), "libs": libs},
            implicit=[Path("tools/linux_link_check.py")],
        )
        n.build(outputs=f"server-{name}", rule="phony", inputs=output)
        outputs.append(output)
    n.build(outputs="server", rule="phony", inputs=outputs)
    n.newline()
