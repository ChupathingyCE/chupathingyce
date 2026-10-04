"""Ninja rules for the native macOS build (``ninja macos``).

The game and most of the platform layer are the Linux build's
(tools/linux_build.py, port/linux); this build compiles them as native
64-bit code (arm64, or a universal application with x86_64), as
tools/lp64_build.py (shared with ``ninja linux64``) describes, with macOS's
C library, SDL3 and FFmpeg from Homebrew, and the macOS-only units in
port/macos/src.

The result is build/macos/halo and the application bundle
build/macos/ChupathingyCE.app. See port/macos/README.md.
"""

import platform
import subprocess
from pathlib import Path
from typing import Any, List

from .embed_assets import hud_assets_build, hud_configure_inputs, ui_fonts_build
from .linux_build import OPTIMISATION
from .lp64_build import (
    Lp64Build,
    Lp64Host,
    _quote,
    load_json,
    lp64_configure_inputs,
    lp64_excluded,
    lp64_game_flags,
)
from .ninja_syntax import Writer
from .version import version

PORT_DIR = Path("port/macos")
PORT_CONFIG = PORT_DIR / "port.json"
# the oldest macOS the build runs on
MACOS_MINIMUM = "13.0"
# SDL3 and the other libraries, from Homebrew
HOMEBREW = Path("/opt/homebrew")
# an application for other Macs (configure.py --portable) is built with an
# SDL3 of its own, for MACOS_MINIMUM (Homebrew's is for the Mac that has it):
# the Android build's release, built here with CMake
from .android_build import SDL_TAG, SDL_URL, check_sdl_commit  # noqa: E402
PORTABLE_SDL_DIR = Path("build/macos/third_party/SDL3")
PORTABLE_SDL_BUILD = Path("build/macos/third_party/SDL3-build")


def fetch_portable_sdl() -> bool:
    """SDL3's sources for the portable build (once): whether they are there"""
    if (PORTABLE_SDL_DIR / "CMakeLists.txt").is_file():
        return True
    PORTABLE_SDL_DIR.parent.mkdir(parents=True, exist_ok=True)
    print(f"Cloning SDL3 {SDL_TAG} (the portable macOS build's)")
    if subprocess.run(["git", "clone", "-q", "--depth", "1", "--branch", SDL_TAG, SDL_URL,
                       str(PORTABLE_SDL_DIR)]).returncode != 0:
        return False
    check_sdl_commit(PORTABLE_SDL_DIR)
    return True

# Apple's libc restricted to ISO C (glibc's is by __STRICT_ANSI__)
MACOS_GAME_FLAGS = lp64_game_flags(["-D_ANSI_SOURCE"])

# Platform files named posix_*.c talk to the C library only, with the host's
# own ABI and no rewrite (their boundary types are posix.h's).
MACOS_POSIX_FLAGS = [
    "-std=gnu11",
    "-D_DARWIN_C_SOURCE",
    OPTIMISATION,
    "-g",
    "-Wall",
    "-Werror=incompatible-pointer-types",
    "-Werror=int-conversion",
]


def macos_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    if not PORT_CONFIG.is_file():
        return [Path(__file__)]
    return [PORT_CONFIG, Path(__file__), *lp64_configure_inputs(), *hud_configure_inputs()]


def generate_macos_build(n: Writer, sln: Any) -> None:
    if not PORT_CONFIG.is_file() or platform.system() != "Darwin":
        return
    config = load_json(PORT_CONFIG)
    build_dir: Path = sln.build_dir / "macos"
    obj_dir = build_dir / "obj"
    output = build_dir / "halo"
    cc = getattr(sln, "macos_cc", None) or "clang"
    portable = getattr(sln, "port_portable", False)
    # the Mac's own architecture; an application for other Macs, both (Apple
    # silicon and Intel: a universal application)
    architectures = ["arm64", "x86_64"] if portable else ["arm64" if platform.machine() == "arm64" else "x86_64"]
    if portable and not fetch_portable_sdl():
        raise SystemExit("the portable macOS build needs SDL3's sources (git clone failed)")
    # (its SDL: the headers before Homebrew's, the library built below)
    portable_sdl = PORTABLE_SDL_BUILD / "libSDL3.0.dylib"
    sdl_include = f"-I{PORTABLE_SDL_DIR / 'include'} " if portable else ""

    n.comment("Native macOS build (ninja macos)")
    lp64 = Lp64Build(n, sln, "macos", cc)
    n.rule(
        name="macos_link",
        command="$macos_cc $ldflags -o $out @$out.rsp $libs",
        description="MACOS LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )

    # (the generated sources: one set, compiled for each architecture)
    generated_sources = (hud_assets_build(n, "macos", build_dir / "generated" / "hud_hires_assets.c")
                         + ui_fonts_build(n, "macos", build_dir / "generated" / "ui_fonts.c", sln))
    if portable:
        n.rule(
            name="macos_sdl3",
            command=(f"cmake -S {PORTABLE_SDL_DIR} -B {PORTABLE_SDL_BUILD} -G Ninja -DCMAKE_BUILD_TYPE=Release "
                     f"-DCMAKE_OSX_DEPLOYMENT_TARGET={MACOS_MINIMUM} '-DCMAKE_OSX_ARCHITECTURES={';'.join(architectures)}' "
                     "-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF "
                     f"> {PORTABLE_SDL_BUILD.parent}/sdl3-configure.log && ninja -C {PORTABLE_SDL_BUILD} "
                     f"> {PORTABLE_SDL_BUILD.parent}/sdl3-build.log"),
            description="MACOS SDL3 (portable)",
            pool="console",
        )
        n.build(outputs=portable_sdl, rule="macos_sdl3", implicit=[PORTABLE_SDL_DIR / "CMakeLists.txt"])

    def emit(arch: str, arch_obj_dir: Path, arch_output: Path) -> None:
        """the game for one architecture: its objects and its executable"""
        target = f"--target={arch}-apple-macos{MACOS_MINIMUM}"
        excluded = set(lp64_excluded()) | set(config.get("exclude_sources", []))
        # an application for other Macs (configure.py --portable): self-contained
        # (its libraries in it, bundle.py), and without FFmpeg, whose libraries
        # (and their licenses) would come with it: movies are skipped, as the
        # other platforms' builds do (bink_null.c, not port/macos/src/macos_bink.c)
        if portable:
            excluded.discard("port/linux/src/bink_null.c")
            excluded.add("port/macos/src/macos_bink.c")
        host = Lp64Host(
            name="macos",
            target_flags=[target],
            game_flags=MACOS_GAME_FLAGS,
            posix_flags=MACOS_POSIX_FLAGS,
            host_include=f"{sdl_include}-idirafter {HOMEBREW / 'include'}",
            excluded=excluded,
            # macOS-only platform units, with the host's ABI (port/macos/src)
            host_sources=sorted((PORT_DIR / "src").glob("*.c")),
        )
        objects = lp64.objects(host, generated_sources, arch_obj_dir)

        libraries = config.get("libraries", [])
        if portable:
            libraries = [library for library in libraries if library not in ("avcodec", "avformat", "swscale", "avutil")]
        frameworks = config.get("frameworks", [])
        n.build(
            outputs=arch_output,
            rule="macos_link",
            inputs=objects,
            implicit=[portable_sdl] if portable else [],
            variables={
                "ldflags": f"{target} -g",
                "libs": " ".join([*([f"-L{PORTABLE_SDL_BUILD}"] if portable else []), f"-L{HOMEBREW / 'lib'}",
                                  *(f"-l{lib}" for lib in libraries), *(f"-framework {fw}" for fw in frameworks)]),
            },
        )

    if len(architectures) == 1:
        emit(architectures[0], obj_dir, output)
    else:
        # (a universal application: each architecture's executable, joined)
        slices = []
        for arch in architectures:
            emit(arch, build_dir / f"obj-{arch}", build_dir / f"halo-{arch}")
            slices.append(build_dir / f"halo-{arch}")
        n.rule(name="macos_lipo", command="lipo -create -output $out $in", description="MACOS LIPO $out")
        n.build(outputs=output, rule="macos_lipo", inputs=slices)

    # the application bundle, which macOS shows with the game's name and icon
    bundle = build_dir / "ChupathingyCE.app"
    n.rule(
        name="macos_bundle",
        command=(f"$python {PORT_DIR / 'bundle.py'} --executable $in --output {_quote(bundle)} --version {version()}"
                 + (f" --self-contained --library-path {PORTABLE_SDL_BUILD}" if portable else "")),
        description="MACOS BUNDLE $out",
    )
    n.build(outputs=bundle / "Contents" / "MacOS" / "halo", rule="macos_bundle", inputs=output,
            implicit=[PORT_DIR / "bundle.py", PORT_DIR / "Info.plist", PORT_DIR / "AppIcon.icns"])
    n.build(outputs="macos", rule="phony", inputs=[output, bundle / "Contents" / "MacOS" / "halo"])
    n.newline()
