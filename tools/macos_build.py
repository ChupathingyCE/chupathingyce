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

import functools
import os
import platform
import subprocess
from pathlib import Path
from typing import Any

from .android_build import SDL_TAG, SDL_URL, check_sdl_commit
from .embed_assets import hud_assets_build, hud_configure_inputs, ui_fonts_build
from .linux_build import OPTIMISATION
from .lp64_build import (
    Lp64Build,
    Lp64Host,
    Lp64Unit,
    _quote,
    load_json,
    lp64_configure_inputs,
    lp64_excluded,
    lp64_game_flags,
)
from .metal_manifest import MAC_DIRS, MANIFEST, Manifest, ManifestError, drift, load, structure_problems
from .ninja_syntax import Writer
from .version import version

PORT_DIR = Path("port/macos")
PORT_CONFIG = PORT_DIR / "port.json"
METAL_DIR = PORT_DIR / "metal"
# the oldest macOS the build runs on
MACOS_MINIMUM = "13.0"
# SDL3 and the other libraries, from Homebrew
HOMEBREW = Path("/opt/homebrew")
# an application for other Macs (configure.py --portable) is built with an
# SDL3 of its own, for MACOS_MINIMUM (Homebrew's is for the Mac that has it):
# the Android build's release, built here with CMake
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


def macos_configure_inputs() -> list[Path]:
    """Files whose change must re-run configure.py."""
    if not PORT_CONFIG.is_file():
        return [Path(__file__)]
    return [PORT_CONFIG, Path(__file__), MANIFEST, Path("tools/metal_manifest.py"),
            *(METAL_DIR / folder for folder in MAC_DIRS), *lp64_configure_inputs(), *hud_configure_inputs()]


@functools.lru_cache(maxsize=None)
def metal_manifest() -> Manifest:
    """port/macos/metal/manifest.json, which must describe the tree: the
    Metal build is made from it, so a file it does not account for stops
    configure.py rather than silently joining or leaving the build"""
    try:
        manifest = load()
    except ManifestError as error:
        raise SystemExit(str(error))
    problems = structure_problems(manifest)
    if problems:
        raise SystemExit("port/macos/metal/manifest.json does not describe the tree:\n  " + "\n  ".join(problems))
    changed = [d.entry.upstream for d in drift(manifest) if not d.known]
    if changed:
        print(f"note: upstream changed {', '.join(changed)} since the Metal build's copies were reconciled: "
              "python3 tools/metal_manifest.py check")
    return manifest


def generate_macos_metal_build(n: Writer, sln: Any, architectures: list[str]) -> None:
    """Opt-in development probe only; never replaces or links into the game."""
    if not getattr(sln, "macos_metal", False):
        return
    build_dir = sln.build_dir / "macos"
    source = METAL_DIR / "renderer" / "metal_smoke.m"
    n.rule(
        name="macos_metal_smoke",
        command=("clang $target -std=gnu11 -fobjc-arc -Wall -Wextra -Werror "
                 "-DHALO_MACOS_METAL_EXPERIMENTAL=1 $in -framework Foundation "
                 "-framework Metal -o $out"),
        description="MACOS METAL SMOKE $out",
    )
    slices = []
    output = build_dir / "metal-smoke"
    for arch in architectures:
        executable = output if len(architectures) == 1 else build_dir / f"metal-smoke-{arch}"
        n.build(outputs=executable, rule="macos_metal_smoke", inputs=source,
                variables={"target": f"--target={arch}-apple-macos{MACOS_MINIMUM}"})
        slices.append(executable)
    if len(architectures) > 1:
        n.rule(name="macos_metal_lipo", command="lipo -create -output $out $in",
               description="MACOS METAL LIPO $out")
        n.build(outputs=output, rule="macos_metal_lipo", inputs=slices)
    n.build(outputs="macos-metal-smoke", rule="phony", inputs=output)


def metal_contract(n: Writer, lp64: Lp64Build, name: str, build_dir: Path, metal_objects: list[Path]) -> None:
    """build_dir/metal-contract.stamp: the upstream files the Metal build
    replaces or drops, compiled as upstream compiles them, define nothing the
    Metal build does not (or the manifest says why not): a function upstream
    adds to its renderer fails the build here, not at a call years later"""
    contract_dir = build_dir / "metal-contract"
    upstream_objects = []
    for unit in getattr(lp64, "contract_units", []):
        relative = unit.source.relative_to(lp64.lp64_dir) if lp64.lp64_dir in unit.source.parents else unit.source
        obj = contract_dir / relative.with_suffix(".o")
        n.build(outputs=obj, rule=f"{name}_cc", inputs=unit.source,
                implicit=[lp64.semantics_header, lp64.platform_semantics_header],
                order_only=[build_dir / "lp64.stamp"], variables={"cflags": unit.cflags})
        upstream_objects.append(obj)
    n.rule(
        name=f"{name}_metal_contract",
        command="$python tools/metal_manifest.py symbols --stamp $out --metal-list $out.rsp --upstream $upstream",
        description=f"{name.upper()} METAL CONTRACT $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.build(outputs=build_dir / "metal-contract.stamp", rule=f"{name}_metal_contract", inputs=metal_objects,
            implicit=[*upstream_objects, MANIFEST, Path("tools/metal_manifest.py")],
            variables={"upstream": " ".join(str(obj) for obj in upstream_objects)})


class MetalLp64Build(Lp64Build):
    """The Metal build's units, from port/macos/metal/manifest.json: each
    upstream platform source (port/linux/src) compiled as it is, replaced by
    its Mac file or dropped; the Mac-only sources added; and the Mac folders
    searched for headers before upstream's."""

    def units(self, host: Lp64Host, generated_sources: list[Path]) -> list[Lp64Unit]:
        manifest = metal_manifest()
        units = super().units(host, generated_sources)
        includes = " ".join(f"-I{self.lp64(METAL_DIR / folder)}" for folder in MAC_DIRS)
        metal_define = "-DHALO_MACOS_METAL_EXPERIMENTAL=1"
        # the upstream units the Metal build replaces or drops, as upstream
        # compiles them: the symbol contract's (tools/metal_manifest.py symbols)
        self.contract_units: list[Lp64Unit] = []
        platform_flags = None
        result = []
        for unit in units:
            rewritten = self.lp64_dir in unit.source.parents
            original = unit.source.relative_to(self.lp64_dir) if rewritten else unit.source
            if original.parent == manifest.upstream:
                name = original.name
                if name == "d3d8_gl.c":
                    platform_flags = unit.cflags
                if name in manifest.replaced or name in manifest.dropped:
                    self.contract_units.append(Lp64Unit(unit.source, unit.cflags, unit.native))
                if name in manifest.dropped:
                    continue
                if name in manifest.replaced:
                    replacement = manifest.replaced[name].mac
                    assert replacement is not None
                    unit.source = self.lp64(METAL_DIR / replacement)
                elif name not in manifest.shared:
                    raise SystemExit(f"{original} is not in port/macos/metal/manifest.json")
            if rewritten:
                unit.cflags = f"{includes} {unit.cflags} {metal_define}"
            result.append(unit)
        if platform_flags is None:
            raise SystemExit("the Metal build replaces port/linux/src/d3d8_gl.c, which the build no longer compiles")
        for source in manifest.mac_sources:
            result.append(Lp64Unit(self.lp64(METAL_DIR / source), f"{includes} {platform_flags} {metal_define}"))
        return result


def generate_macos_build(n: Writer, sln: Any) -> None:
    if not PORT_CONFIG.is_file() or platform.system() != "Darwin":
        return
    config = load_json(PORT_CONFIG)
    name = "macos"
    build_dir: Path = sln.build_dir / name
    obj_dir = build_dir / "obj"
    output = build_dir / "halo"
    cc = "clang"
    portable = getattr(sln, "port_portable", False)
    # the Mac's own architecture; an application for other Macs, both (Apple
    # silicon and Intel: a universal application)
    architectures = ["arm64", "x86_64"] if portable else ["arm64" if platform.machine() == "arm64" else "x86_64"]
    generate_macos_metal_build(n, sln, architectures)
    if portable and not fetch_portable_sdl():
        raise SystemExit("the portable macOS build needs SDL3's sources (git clone failed)")
    # (its SDL: the headers before Homebrew's, the library built below)
    portable_sdl = PORTABLE_SDL_BUILD / "libSDL3.0.dylib"
    sdl_include = f"-I{PORTABLE_SDL_DIR / 'include'} " if portable else ""

    n.comment("Native macOS build (ninja macos)")
    metal = getattr(sln, "macos_metal", False)
    manifest = metal_manifest() if metal else None
    build_type = MetalLp64Build if metal else Lp64Build
    lp64 = build_type(n, sln, name, cc,
                     extra_roots=[METAL_DIR / folder for folder in MAC_DIRS] if metal else [])
    n.rule(
        name=f"{name}_link",
        command=f"${name}_cc $ldflags -o $out @$out.rsp $libs",
        description=f"{name.upper()} LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )

    # (the generated sources: one set, compiled for each architecture)
    generated_sources = (hud_assets_build(n, name, build_dir / "generated" / "hud_hires_assets.c")
                         + ui_fonts_build(n, name, build_dir / "generated" / "ui_fonts.c", sln))
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
            name=name,
            target_flags=[target],
            game_flags=MACOS_GAME_FLAGS,
            posix_flags=MACOS_POSIX_FLAGS,
            host_include=f"{sdl_include}-idirafter {HOMEBREW / 'include'}",
            excluded=excluded,
            # macOS-only platform units, with the host's ABI (port/macos/src)
            host_sources=sorted((PORT_DIR / "src").glob("*.c")),
        )
        objects = lp64.objects(host, generated_sources, arch_obj_dir)
        if metal:
            assert manifest is not None
            metal_includes = " ".join(f"-I{METAL_DIR / folder}" for folder in MAC_DIRS)
            for relative in manifest.objc:
                source = METAL_DIR / relative
                obj = arch_obj_dir / source.with_suffix(".o")
                n.build(outputs=obj, rule=f"{name}_cc", inputs=source, variables={
                    "cflags": f"{target} -std=gnu11 -fobjc-arc -O2 -g -Wall {metal_includes} "
                              f"{sdl_include}-idirafter {HOMEBREW / 'include'}"})
                objects.append(obj)
            if arch == architectures[0]:
                metal_contract(n, lp64, name, build_dir, objects)

        libraries = config.get("libraries", [])
        if portable:
            libraries = [library for library in libraries if library not in ("avcodec", "avformat", "swscale", "avutil")]
        frameworks = [*config.get("frameworks", []), *(["Metal", "QuartzCore", "Foundation", "MetalFX"] if metal else [])]
        n.build(
            outputs=arch_output,
            rule=f"{name}_link",
            inputs=objects,
            implicit=[portable_sdl] if portable else [],
            variables={
                "ldflags": " ".join([target, "-g", *LTO_FLAGS]),
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
        n.rule(name=f"{name}_lipo", command="lipo -create -output $out $in", description=f"{name.upper()} LIPO $out")
        n.build(outputs=output, rule=f"{name}_lipo", inputs=slices)

    # the application bundle, which macOS shows with the game's name and icon
    bundle = build_dir / "ChupathingyCE.app"
    n.rule(
        name=f"{name}_bundle",
        command=(f"$python {PORT_DIR / 'bundle.py'} --executable $in --output {_quote(bundle)} --version {version()}"
                 + (f" --self-contained --library-path {PORTABLE_SDL_BUILD}" if portable else "")),
        description=f"{name.upper()} BUNDLE $out",
    )
    n.build(outputs=bundle / "Contents" / "MacOS" / "halo", rule=f"{name}_bundle", inputs=output,
            implicit=[PORT_DIR / "bundle.py", PORT_DIR / "Info.plist", PORT_DIR / "AppIcon.icns"])
    n.build(outputs=name, rule="phony", inputs=[output, bundle / "Contents" / "MacOS" / "halo",
                                                *([build_dir / "metal-contract.stamp"] if metal else [])])
    n.newline()
