#!/usr/bin/env python3

# root build script: writes build.ninja for the native ports (Linux, Windows,
# Android, macOS) and the dedicated server (Linux), and for the console ports
# given with --console-port (tools/console_build.py). Run from a console
# repository, with the game as its submodule, it writes that repository's
# build.ninja, with the console's targets only (docs/building.md)

import argparse
import io
import os
import sys
from pathlib import Path
from types import SimpleNamespace

from tools import ninja_syntax
from tools.android_build import android_configure_inputs, generate_android_build
from tools.android64_build import android64_configure_inputs, generate_android64_build
from tools.console_build import (GAME_ROOT, add_console_arguments, console_configure_inputs, load_console_port,
                                 relative)
from tools.linux_build import generate_linux_build, linux_configure_inputs
from tools.linux64_build import generate_linux64_build, linux64_configure_inputs
from tools.macos_build import generate_macos_build, macos_configure_inputs
from tools.server_build import generate_server_build, server_configure_inputs
from tools.version import commit_inputs
from tools.windows_build import generate_windows_build, windows_configure_inputs

# the console ports (--console-port), whose own options join the others
console_arguments = argparse.ArgumentParser(add_help=False)
console_arguments.add_argument("--console-port", type=Path, action="append", default=[])
console_ports = [load_console_port(directory) for directory in console_arguments.parse_known_args()[0].console_port]
# (run from outside the game's tree, a console repository's root: the
# console's targets only, the desktop builds' paths being the game's own)
console_only = relative(GAME_ROOT) != Path(".")
if console_only and not console_ports:
    sys.exit(f"configure.py: run it from the game's root ({relative(GAME_ROOT)}), or name a console port with "
             "--console-port (docs/building.md)")

# arguments
parser = argparse.ArgumentParser()
parser.add_argument(
    "--linux-cc",
    metavar="BINARY",
    help="compiler for the native Linux builds, `ninja linux` and `ninja linux64`, and the dedicated "
    "server, `ninja server` (default: clang)",
)
parser.add_argument(
    "--compiler-launcher",
    metavar="BINARY",
    help="a program that runs each compile, such as ccache "
    "(links and the Android host's few units run the compiler directly)",
)
parser.add_argument(
    "--release",
    action="store_true",
    help="release builds (Linux, Windows, Android): assertions are not checked",
)
parser.add_argument(
    "--game-browser",
    action=argparse.BooleanOptionalAction,
    default=True,
    help="every build (Linux, Windows, macOS, Android; always the server's): the game list and server browser of stats.chupathingy.org "
    "(HALO_GAME_BROWSER; port/linux/src/browser.c); on unless --no-game-browser",
)
parser.add_argument(
    "--renderer",
    choices=["gl", "bgfx"],
    default="gl",
    help="macOS: what draws the game. gl (the default) is OpenGL (port/linux/src/d3d8_gl.c); bgfx also builds "
    "the bgfx test backend (Metal; port/linux/src/d3d8_bgfx.inl), chosen at run time with display.renderer = "
    "\"bgfx\" or HALO_RENDERER=bgfx. It fetches bgfx, bx and bimg at pinned commits into build/macos/third_party "
    "and links Homebrew's shaderc and SPIRV-Cross",
)
parser.add_argument(
    "--console-limits",
    action="store_true",
    help="native ports (Linux, macOS): the original Xbox build's network and object limits "
    "(HALO_CONSOLE_LIMITS: 16 players and machines, 2,048 objects), to test the console's side of "
    "cross-play against the PC builds on one computer (docs/cross-play.md); never a build to ship",
)
parser.add_argument(
    "--window-rebase",
    action="store_true",
    help="the 32-bit Linux build (ninja linux) with its Xbox memory window wherever HALO_WINDOW_BASE puts it "
    "(HALO_WINDOW_REBASE: source/cseries/xbox_address.h), to test on the desktop what the Switch's guest "
    "does; never a build to ship",
)
parser.add_argument(
    "--packaged",
    action="store_true",
    help="desktop builds: a package manager's build (HALO_PACKAGED; port/linux/src/packaged_install.h): "
    "the game never updates itself, and on Linux keeps its data and settings in ~/.local/share/chupathingyce. "
    "A package of a release's own executable sets HALO_PACKAGED=1 or puts a file named packaged beside it instead",
)
parser.add_argument(
    "--lto",
    choices=["full", "thin", "off"],
    default="full",
    help="link-time optimisation (Linux, Windows): full (the default: the whole game "
    "optimised as one module, the fastest code and the slowest links), thin (parallel and incremental) or off",
)
parser.add_argument(
    "--portable",
    action="store_true",
    help="x86 builds (Linux, Windows): code for any x86-64 processor (SSE2) rather than for this "
    "machine's (-march=native, the default); use it for builds that run on other computers. On Linux (32- and "
    "64-bit) it also builds against Debian 11's glibc 2.31 rather than this machine's, and brings its own SDL 3 "
    "(libSDL3.so.0, beside the executable), so that it starts on SteamOS and older distributions: the first build "
    "downloads the Debian packages and SDL's source (build/linux*/third_party, tools/linux_sysroot.py), and needs "
    "CMake, pkgconf and wayland-scanner, not the system's SDL 3",
)
parser.add_argument(
    "--pgo",
    nargs="?",
    const="train",
    default="use",
    choices=["use", "train", "off"],
    help="profile-guided optimisation: use (the default) optimises with the profiles in pgo/, "
    "train (or plain --pgo) first records this platform's profile if it is missing, by letting an instrumented "
    "build play every campaign level (needs the game data in assets/ and a display; Linux and Windows), off does "
    "without",
)
parser.add_argument(
    "--pgo-profile",
    metavar="PROFDATA",
    type=Path,
    help="profile-guided optimisation from this profile instead",
)
parser.add_argument(
    "--android-ndk",
    type=str,
    help="Android NDK for `ninja android` and `ninja android64` (default: ANDROID_NDK_HOME, or the newest under the Android SDK)",
)
parser.add_argument(
    "--android-guest-cc",
    type=str,
    help="clang with the arm64_32 target for the Android guest (default: clang)",
)
parser.add_argument(
    "--without-ffmpeg",
    dest="with_ffmpeg",
    action="store_false",
    help="leave the movies' FFmpeg out (the Bink decoder behind the SDK's entry points: LGPL, "
         "static, built for each target): the stubs build and the game skips the movies",
)
# (the default since every desktop build plays the movies; kept for the
# scripts that still ask for it)
parser.add_argument("--with-ffmpeg", dest="with_ffmpeg", action="store_true", help=argparse.SUPPRESS)
parser.set_defaults(with_ffmpeg=True)
add_console_arguments(parser)
for port in console_ports:
    if hasattr(port.module, "add_arguments"):
        port.module.add_arguments(parser.add_argument_group(f"{port.name} ({port.root})"))
args = parser.parse_args()

# the settings the builds read
sln = SimpleNamespace(
    build_dir=Path("build"),
    linux_cc=args.linux_cc,
    compiler_launcher=args.compiler_launcher,
    port_release=args.release,
    game_browser=args.game_browser,
    console_limits=args.console_limits,
    window_rebase=args.window_rebase,
    packaged=args.packaged,
    port_lto=args.lto,
    renderer=args.renderer,
    port_portable=args.portable,
    port_pgo=args.pgo,
    port_pgo_profile=args.pgo_profile,
    android_ndk=args.android_ndk,
    android_guest_cc=args.android_guest_cc,
    with_ffmpeg=args.with_ffmpeg,
    console_no_debug_asserts=args.console_no_debug_asserts,
    console_debug_tools=args.console_debug_tools,
    console_players=args.console_players,
    # (every option, a console port's own too, by its name)
    args=args,
)


def is_windows() -> bool:
    return os.name == "nt"


# build.ninja
out = io.StringIO()
n = ninja_syntax.Writer(out)
n.variable("ninja_required_version", "1.3")
n.newline()

configure_script = Path(os.path.relpath(os.path.abspath(sys.argv[0])))
n.comment("The arguments passed to configure.py, for rerunning it.")
n.variable(
    "configure_args",
    [f'"{arg}"' if any(ch.isspace() for ch in arg) else arg for arg in sys.argv[1:]],
)
n.variable("python", f'"{sys.executable}"')
n.newline()

if not console_only:
    generate_linux_build(n, sln)
    generate_linux64_build(n, sln)
    generate_android_build(n, sln)
    generate_android64_build(n, sln)
    generate_windows_build(n, sln)
    generate_macos_build(n, sln)
    generate_server_build(n, sln)
for port in console_ports:
    port.module.generate(n, sln, port)

n.comment("Reconfigure on change")
n.rule(
    name="configure",
    command=f"$python {configure_script} $configure_args",
    generator=True,
    description=f"RUN {configure_script}",
)
n.build(
    outputs="build.ninja",
    rule="configure",
    implicit=[
        configure_script,
        relative(GAME_ROOT / "tools" / "ninja_syntax.py"),
        *([] if console_only else [
            *linux_configure_inputs(),
            *linux64_configure_inputs(),
            *android_configure_inputs(),
            *android64_configure_inputs(),
            *windows_configure_inputs(),
            *macos_configure_inputs(),
            *server_configure_inputs(),
        ]),
        *(console_configure_inputs(console_ports) if console_ports else []),
        # (the commit the builds record: tools/version.py)
        *commit_inputs(),
    ],
)
n.newline()

# the build for this computer, where it could be generated (the Windows
# build is left out when SDL cannot be fetched, for instance), or a console
# repository's own port
default = "windows" if is_windows() else "macos" if sys.platform == "darwin" else "linux"
if console_only:
    default = console_ports[0].name
if f"\nbuild {default}: " in out.getvalue():
    n.comment("Default rule: the build for this computer")
    n.default(default)

with open("build.ninja", "w", encoding="utf-8") as f:
    f.write(out.getvalue())
out.close()
