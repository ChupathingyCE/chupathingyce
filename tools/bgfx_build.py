"""Ninja rules for the bgfx renderer of the 64-bit desktop builds.

display.renderer = "bgfx" (or HALO_RENDERER=bgfx) draws the game with bgfx
instead of OpenGL (port/linux/src/d3d8_bgfx.inl): Metal on macOS, Vulkan on
Windows and Linux. OpenGL stays the default; the bgfx renderer is compiled
into every 64-bit desktop build (macOS, ``ninja linux64``, ``ninja
windows64``) unless configure.py is given --without-bgfx.

bgfx, bx and bimg (BSD-2-Clause, their LICENSE files stay with them) are
fetched once at pinned commits into build/bgfx, patched with
tools/bgfx-patches, and built here as static libraries, with two of bgfx's
own third-party libraries: glslang (BSD-3-Clause and others, its
LICENSE.txt), which compiles the translators' GLSL to SPIR-V at run time, and
on macOS SPIRV-Cross (Apache-2.0), which turns the SPIR-V into Metal's
shading language. Nothing is needed at run time but the system's Metal or
Vulkan driver, and on Windows Direct3D 11 with its shader compiler
(d3dcompiler_47.dll, part of Windows) for computers whose Vulkan does not
start. On Windows SPIRV-Cross turns the SPIR-V into HLSL for it.
"""

import hashlib
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, List

from .ninja_syntax import Writer

BGFX_SOURCES = Path("build/bgfx")
BGFX_REPOSITORIES = {
    "bx": ("https://github.com/bkaradzic/bx.git", "57548da89a07771c5b0f8a6711149be70e08a329"),
    "bimg": ("https://github.com/bkaradzic/bimg.git", "77bebb28e12956bfced82bb2cf7c19ac5735b6c8"),
    "bgfx": ("https://github.com/bkaradzic/bgfx.git", "4608478e119667eab7635226f721ede32b0935c3"),
}
# our changes to bgfx, applied to the fetched sources (tools/bgfx-patches)
BGFX_PATCHES = sorted(Path("tools/bgfx-patches").glob("*.patch"))
# its limits for the game: visibility tests (lens flares, The Library's
# lights). Views are fixed at 4096 in bgfx now, and Metal's uniform space
# grows as needed.
# (and not its NvOptimusEnablement and AmdPowerXpressRequestHighPerformance:
# the Windows builds export their own, port/windows/src)
BGFX_DEFINES = ["-DBGFX_CONFIG_MAX_OCCLUSION_QUERIES=4096", "-DBGFX_CONFIG_PREFER_DISCRETE_GPU=0"]
# bgfx's renderers, all off but the platform's
BGFX_RENDERERS = ["AGC", "DIRECT3D11", "DIRECT3D12", "GNM", "METAL", "NVN", "OPENGL", "OPENGLES", "VULKAN",
                  "WEBGPU"]
# SPIRV-Cross's units, with its C interface: Metal's shading language on
# macOS, HLSL for Direct3D 11 on Windows
SPIRV_CROSS_COMMON = ["spirv_cfg.cpp", "spirv_cross.cpp", "spirv_cross_parsed_ir.cpp", "spirv_cross_util.cpp",
                      "spirv_glsl.cpp", "spirv_parser.cpp", "spirv_cross_c.cpp"]
SPIRV_CROSS_UNITS = {"macos": SPIRV_CROSS_COMMON + ["spirv_msl.cpp"], "windows": SPIRV_CROSS_COMMON + ["spirv_hlsl.cpp"]}
SPIRV_CROSS_BACKENDS = {"macos": "-DSPIRV_CROSS_C_API_MSL=1", "windows": "-DSPIRV_CROSS_C_API_HLSL=1"}


def bgfx_wanted(sln: Any) -> bool:
    """whether the 64-bit desktop builds have the bgfx renderer (configure.py --without-bgfx)"""
    return getattr(sln, "with_bgfx", True)


def _git_head(directory: Path) -> str:
    result = subprocess.run(["git", "-C", str(directory), "rev-parse", "HEAD"], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else ""


def fetch_bgfx() -> bool:
    """bgfx's, bx's and bimg's sources at their pinned commits, patched:
    whether they are there"""
    for name, (url, commit) in BGFX_REPOSITORIES.items():
        directory = BGFX_SOURCES / name
        if (directory / "LICENSE").is_file() and _git_head(directory) == commit:
            continue
        directory.mkdir(parents=True, exist_ok=True)
        print(f"Fetching {name} {commit[:12]} (the bgfx renderer's)")
        # (the files as they are, whatever the machine's line endings: the
        # patches are made against them)
        git = ["git", "-C", str(directory), "-c", "core.autocrlf=false"]
        if not ((directory / ".git").is_dir() or subprocess.run([*git, "init", "-q"]).returncode == 0):
            return False
        if not (subprocess.run([*git, "fetch", "-q", "--depth", "1", url, commit]).returncode == 0
                and subprocess.run([*git, "checkout", "-q", "-f", "FETCH_HEAD"]).returncode == 0):
            return False
        # (fresh sources: none of the patches)
        (directory / ".halo-patches").unlink(missing_ok=True)
    # (the patches once, in order, on the fetched sources: a stamp lists
    # those applied; other patches, the sources again and all of them. A
    # patch is read with its lines' ends as made, LF, whatever a Windows
    # checkout turned them into)
    bgfx = BGFX_SOURCES / "bgfx"
    stamp = bgfx / ".halo-patches"
    patches = [(patch.name, patch.read_bytes().replace(b"\r\n", b"\n")) for patch in BGFX_PATCHES]
    wanted = "".join(f"{name} {hashlib.sha256(text).hexdigest()}\n" for name, text in patches)
    if stamp.is_file() and stamp.read_text() == wanted:
        return True
    if subprocess.run(["git", "-C", str(bgfx), "-c", "core.autocrlf=false", "checkout", "-q", "-f", "--", "."]).returncode != 0:
        return False
    for name, text in patches:
        print(f"Patching bgfx: {name}")
        if subprocess.run(["git", "-C", str(bgfx), "apply", "-"], input=text).returncode != 0:
            return False
    stamp.write_text(wanted)
    return True


def bgfx_available(sln: Any) -> bool:
    """whether this build has the bgfx renderer: wanted (configure.py
    --without-bgfx), and its sources fetched (a machine without git or the
    network, such as the dedicated server's container, builds without it
    and says so)"""
    if not bgfx_wanted(sln):
        return False
    try:
        if fetch_bgfx():
            return True
    except OSError as error:
        print(f"bgfx: {error}")
    print("the bgfx renderer is left out: its sources could not be fetched (tools/bgfx_build.py)")
    return False


@dataclass
class BgfxTarget:
    """one build's bgfx: its name (rules' prefix), platform and compiler"""
    # the build's name (macos, linux64, windows64)
    name: str
    # "macos", "linux" or "windows"
    platform: str
    # the compile command's start (the launcher and the C compiler: units
    # are compiled as C++ with -x)
    cc: str
    # the target's flags (--target, -arch, --sysroot)
    target_flags: List[str]
    # the folder of the build's objects and libraries
    build_dir: Path
    # what makes a static library ($out from $in)
    archive_command: str
    # what the objects wait for (a portable build's system root)
    implicit: List[Path] = field(default_factory=list)


def bgfx_include_flags() -> List[str]:
    """the headers d3d8_gl.c includes for the bgfx renderer"""
    bgfx, bx, glslang = BGFX_SOURCES / "bgfx", BGFX_SOURCES / "bx", BGFX_SOURCES / "bgfx/3rdparty/glslang"
    return ["-DHALO_BGFX", f"-I{bgfx / 'include'}", f"-I{bx / 'include'}", f"-I{glslang}",
            f"-I{bgfx / '3rdparty/spirv-cross'}", f"-I{bgfx / '3rdparty/khronos'}"]


def bgfx_build(n: Writer, target: BgfxTarget) -> List[Path]:
    """the rules for bx, bimg, bgfx, glslang (and SPIRV-Cross on macOS) as
    static libraries: the libraries, in link order"""
    bx, bimg, bgfx = (BGFX_SOURCES / name for name in ("bx", "bimg", "bgfx"))
    glslang = bgfx / "3rdparty/glslang"
    spirv_cross = bgfx / "3rdparty/spirv-cross"
    obj_dir = target.build_dir / "obj"
    prefix = f"{target.name}_bgfx"
    label = target.name.upper()
    compat = {"macos": "osx", "linux": "linux", "windows": "msvc"}[target.platform]
    # (Windows: Vulkan, and Direct3D 11 for computers whose Vulkan does
    # not start)
    renderers = {"macos": ["METAL"], "linux": ["VULKAN"], "windows": ["VULKAN", "DIRECT3D11"]}[target.platform]
    platform_flags = {
        "macos": [],
        "linux": ["-fno-strict-aliasing"],
        "windows": ["-D_CRT_SECURE_NO_WARNINGS", "-D_HAS_EXCEPTIONS=0", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN"],
    }[target.platform]
    common = " ".join([
        # (line tables only: a crash's calls are named, and the executable,
        # which keeps its debug information, stays near its size)
        *target.target_flags, "-O2", "-gline-tables-only", "-std=c++20", "-fno-exceptions", "-fno-rtti", "-w",
        *platform_flags,
        "-DBX_CONFIG_DEBUG=0", f"-I{bx / 'include'}", f"-I{bx / 'include/compat' / compat}", f"-I{bx / '3rdparty'}",
        f"-I{bimg / 'include'}", f"-I{bimg / '3rdparty/astc-encoder/include'}",
        f"-I{bimg / '3rdparty/astc-encoder/source'}",
        f"-I{bgfx / 'include'}", f"-I{bgfx / '3rdparty'}", f"-I{bgfx / '3rdparty/khronos'}",
    ])
    n.rule(name=f"{prefix}_cxx", command=f"{target.cc} {common} $extra -MMD -MF $out.d -c $in -o $out",
           description=f"{label} BGFX $in", depfile="$out.d", deps="gcc")
    n.rule(name=f"{prefix}_ar", command=target.archive_command, description=f"{label} BGFX AR $out")
    bgfx_flags = " ".join([
        "-x objective-c++" if target.platform == "macos" else "-x c++", *BGFX_DEFINES,
        *(f"-DBGFX_CONFIG_RENDERER_{name}={1 if name in renderers else 0}" for name in BGFX_RENDERERS),
    ])
    # (glslang without its HLSL front end or SPIRV-Tools' optimiser, the
    # build's own threads: GLSL to SPIR-V only)
    glslang_os = "Windows" if target.platform == "windows" else "Unix"
    glslang_sources = sorted(
        source for source in [*(glslang / "glslang").rglob("*.cpp"), *(glslang / "SPIRV").rglob("*.cpp")]
        if "HLSL" not in source.parts and "OSDependent" not in source.parts)
    glslang_sources.append(glslang / "glslang/OSDependent" / glslang_os / "ossource.cpp")
    # (bx's x86 code needs SSE 4.2, its minimum there: bx, bimg and bgfx only,
    # which run only with the bgfx renderer; the game keeps its baseline)
    simd = "-Xarch_x86_64 -msse4.2" if target.platform == "macos" else "-msse4.2"
    units = {
        "bx": ([bx / "src/amalgamated.cpp"], f"-x c++ {simd}"),
        "bimg": ([bimg / "src/image.cpp", bimg / "src/image_encode_wic.cpp",
                  *sorted((bimg / "3rdparty/astc-encoder/source").glob("*.cpp"))], f"-x c++ {simd}"),
        "bgfx": ([bgfx / "src/amalgamated.cpp"], f"{bgfx_flags} {simd}"),
        "glslang": (glslang_sources, f"-x c++ -std=c++17 -fno-strict-aliasing -I{glslang} -I{glslang.parent}"),
    }
    if target.platform in SPIRV_CROSS_UNITS:
        # (on Windows with C++ exceptions, which its C interface catches: a
        # shader HLSL can't express fails alone instead of ending the game;
        # macOS keeps the assertions it has shipped with)
        errors = ("-fexceptions -fcxx-exceptions -U_HAS_EXCEPTIONS -D_HAS_EXCEPTIONS=1"
                  if target.platform == "windows" else "-DSPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS")
        units["spirv-cross"] = ([spirv_cross / name for name in SPIRV_CROSS_UNITS[target.platform]],
                                f"-x c++ {errors} -DSPIRV_CROSS_C_API_GLSL=1 "
                                f"{SPIRV_CROSS_BACKENDS[target.platform]} -I{spirv_cross / 'include'}")
    libraries = []
    for name, (sources, extra) in units.items():
        objects = []
        for source in sources:
            relative = source.relative_to(BGFX_SOURCES / ("bgfx" if name in ("glslang", "spirv-cross") else name))
            obj = obj_dir / name / relative.with_suffix(".o")
            n.build(outputs=obj, rule=f"{prefix}_cxx", inputs=source, implicit=[*BGFX_PATCHES, *target.implicit],
                    variables={"extra": extra})
            objects.append(obj)
        library = target.build_dir / f"lib{name}.a"
        n.build(outputs=library, rule=f"{prefix}_ar", inputs=objects)
        libraries.append(library)
    # (bgfx first: it needs bimg's and bx's symbols; the compilers after)
    order = ["bgfx", "bimg", "bx", "glslang", "spirv-cross"]
    return sorted(libraries, key=lambda library: order.index(library.stem[3:]))
