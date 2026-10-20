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

import os
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

# the bgfx test backend (configure.py --renderer=bgfx; port/linux/src/d3d8_bgfx.inl):
# bgfx, bx and bimg (BSD-2-Clause, their LICENSE files stay with them), fetched at
# pinned commits and built here as three static libraries, plus Homebrew's shaderc
# and SPIRV-Cross (Apache-2.0), which turn the generated GLSL into Metal's shaders
# at run time
BGFX_THIRD_PARTY = Path("build/macos/third_party")
BGFX_REPOSITORIES = {
    "bx": ("https://github.com/bkaradzic/bx.git", "217823bc0a05a7dcf29e5166708b82910cda0623"),
    "bimg": ("https://github.com/bkaradzic/bimg.git", "5c7eabb1d73cb0c1a3c23c0268dd70e8dca972f7"),
    "bgfx": ("https://github.com/bkaradzic/bgfx.git", "9849fe96cba81355352e5355ee2d333ea70857ac"),
}
# our changes to bgfx, applied to the fetched sources (tools/bgfx-patches)
BGFX_PATCHES = sorted(Path("tools/bgfx-patches").glob("*.patch"))
# its limits for the game: views (each target change and clear after draws
# is one), visibility tests (lens flares, The Library's lights) and Metal's
# uniform space a frame
BGFX_DEFINES = ("-DBGFX_CONFIG_MAX_VIEWS=1024 -DBGFX_CONFIG_MAX_OCCLUSION_QUERIES=4096 "
                "-DBGFX_CONFIG_MTL_UNIFORM_BUFFER_SIZE=33554432")
BGFX_SPIRV_CROSS_LIBRARIES = ["c", "msl", "hlsl", "cpp", "glsl", "reflect", "core"]


def fetch_bgfx() -> bool:
    """bgfx's, bx's and bimg's sources (once, at their pinned commits): whether they are there"""
    for name, (url, commit) in BGFX_REPOSITORIES.items():
        directory = BGFX_THIRD_PARTY / name
        if (directory / "LICENSE").is_file():
            continue
        directory.mkdir(parents=True, exist_ok=True)
        print(f"Fetching {name} {commit[:12]} (the bgfx test backend's)")
        git = ["git", "-C", str(directory)]
        if not (subprocess.run([*git, "init", "-q"]).returncode == 0
                and subprocess.run([*git, "fetch", "-q", "--depth", "1", url, commit]).returncode == 0
                and subprocess.run([*git, "checkout", "-q", "FETCH_HEAD"]).returncode == 0):
            return False
    # (each patch once: one that applies in reverse is in already)
    bgfx = BGFX_THIRD_PARTY / "bgfx"
    for patch in BGFX_PATCHES:
        if subprocess.run(["git", "-C", str(bgfx), "apply", "--reverse", "--check", str(patch.resolve())],
                          capture_output=True).returncode == 0:
            continue
        print(f"Patching bgfx: {patch.name}")
        if subprocess.run(["git", "-C", str(bgfx), "apply", str(patch.resolve())]).returncode != 0:
            return False
    return True


def bgfx_build(n: Writer, architectures: List[str]) -> List[Path]:
    """the rules for bx, bimg and bgfx as static libraries: the libraries"""
    bx, bimg, bgfx = (BGFX_THIRD_PARTY / name for name in ("bx", "bimg", "bgfx"))
    obj_dir = BGFX_THIRD_PARTY / "obj"
    arch_flags = " ".join(f"-arch {arch}" for arch in architectures)
    common = (f"{arch_flags} -mmacosx-version-min={MACOS_MINIMUM} -O2 -g -std=c++20 -fno-exceptions -fno-rtti "
              f"-DBX_CONFIG_DEBUG=0 -I{bx / 'include'} -I{bx / 'include/compat/osx'} -I{bx / '3rdparty'} "
              f"-I{bimg / 'include'} -I{bimg / '3rdparty/astc-encoder/include'} -I{bimg / '3rdparty/astc-encoder/source'} "
              f"-I{bgfx / 'include'} -I{bgfx / '3rdparty'} -I{bgfx / '3rdparty/khronos'} "
              "-Wno-deprecated-declarations")
    n.rule(name="macos_bgfx_cxx", command=f"clang++ {common} $extra -MMD -MF $out.d -c $in -o $out",
           description="MACOS BGFX $in", depfile="$out.d", deps="gcc")
    n.rule(name="macos_bgfx_ar", command="rm -f $out && libtool -static -o $out $in 2>/dev/null",
           description="MACOS BGFX AR $out")
    libraries = []
    units = {
        "bx": [bx / "src/amalgamated.cpp"],
        "bimg": [bimg / "src/image.cpp", bimg / "src/image_gnf.cpp",
                 *sorted((bimg / "3rdparty/astc-encoder/source").glob("*.cpp"))],
        "bgfx": [bgfx / "src/amalgamated.cpp"],
    }
    for name, sources in units.items():
        objects = []
        for source in sources:
            obj = obj_dir / name / (source.stem + ".o")
            # (bgfx's renderers are Objective-C++ on macOS, for Metal and its layer)
            n.build(outputs=obj, rule="macos_bgfx_cxx", inputs=source, implicit=BGFX_PATCHES,
                    variables={"extra": f"-x objective-c++ {BGFX_DEFINES} -DBGFX_CONFIG_RENDERER_METAL=1 -DBGFX_CONFIG_RENDERER_OPENGL=0 "
                               "-DBGFX_CONFIG_RENDERER_VULKAN=0" if name == "bgfx" else ""})
            objects.append(obj)
        library = BGFX_THIRD_PARTY / f"lib{name}.a"
        n.build(outputs=library, rule="macos_bgfx_ar", inputs=objects)
        libraries.append(library)
    # (bgfx first: it needs bimg's and bx's symbols)
    return list(reversed(libraries))


# Apple's libc restricted to ISO C (glibc's is by __STRICT_ANSI__)
# HALO_LTO=1 builds the whole program with link-time optimisation (clang's
# -flto on both the objects and the link): a few percent in the renderer's
# hot paths, at the cost of a slower link
LTO_FLAGS = ["-flto"] if os.environ.get("HALO_LTO") == "1" else []
MACOS_GAME_FLAGS = [*lp64_game_flags(["-D_ANSI_SOURCE"]), *LTO_FLAGS]

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
    cc = "clang"
    portable = getattr(sln, "port_portable", False)
    # the Mac's own architecture; an application for other Macs, both (Apple
    # silicon and Intel: a universal application)
    architectures = ["arm64", "x86_64"] if portable else ["arm64" if platform.machine() == "arm64" else "x86_64"]
    if portable and not fetch_portable_sdl():
        raise SystemExit("the portable macOS build needs SDL3's sources (git clone failed)")
    # (its SDL: the headers before Homebrew's, the library built below)
    portable_sdl = PORTABLE_SDL_BUILD / "libSDL3.0.dylib"
    sdl_include = f"-I{PORTABLE_SDL_DIR / 'include'} " if portable else ""
    use_bgfx = getattr(sln, "renderer", "gl") == "bgfx"
    if use_bgfx and not fetch_bgfx():
        raise SystemExit("the bgfx test backend needs bgfx's, bx's and bimg's sources (git fetch failed)")
    # (the platform units' define and headers: d3d8_gl.c, sdl_platform.c)
    if use_bgfx:
        sdl_include += f"-DHALO_BGFX -I{BGFX_THIRD_PARTY / 'bgfx/include'} -I{BGFX_THIRD_PARTY / 'bx/include'} "

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

    bgfx_libraries = bgfx_build(n, architectures) if use_bgfx else []

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
        bgfx_libs = []
        if use_bgfx:
            frameworks = [*frameworks, "Metal", "QuartzCore", "Cocoa", "IOKit"]
            bgfx_libs = [*(str(library) for library in bgfx_libraries),
                         *(str(HOMEBREW / "lib" / f"libspirv-cross-{name}.a") for name in BGFX_SPIRV_CROSS_LIBRARIES),
                         str(HOMEBREW / "lib" / "libshaderc_combined.a"), "-lc++", "-lobjc"]
        n.build(
            outputs=arch_output,
            rule="macos_link",
            inputs=objects,
            implicit=([portable_sdl] if portable else []) + bgfx_libraries,
            variables={
                "ldflags": " ".join([target, "-g", *LTO_FLAGS]),
                "libs": " ".join([*bgfx_libs, *([f"-L{PORTABLE_SDL_BUILD}"] if portable else []), f"-L{HOMEBREW / 'lib'}",
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
