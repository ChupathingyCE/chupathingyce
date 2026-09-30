"""Ninja rules for an original Xbox build (``ninja xbox``).

The game compiled for the console it was written for, with clang and
lld-link as OXDK (https://github.com/MrMilenko/OXDK) uses them, into
build/xbox/default.xbe. The game compiles against the January 2002 SDK's
declarations (port/include/xdk), as the other ports do, and links with a
later SDK's libraries, except in two places where those changed:

  - Direct3D: later SDKs renumbered the render and texture stage states
    the game sets, so the game needs a Direct3D of its own time. The
    reconstructed libs/d3d8 is written against the January SDK's headers,
    which are not available, so the build links a prebuilt Aug 2001
    Direct3D library, whose numbering is the January SDK's
    (--xbox-d3d8, below).
  - DirectSound: port/xbox/src/xbox_dsound.c translates the January SDK's
    mix bin masks.

The build is generated when OXDK and the Direct3D library are found
(configure.py's options):

    --oxdk DIR          OXDK's checkout (default OXDK_DIR, or ~/OXDK): its
                        XDK libraries (xbox/xdk/lib) and headers, and cxbe
    --xbox-d3d8 LIBRARY an Aug 2001 Direct3D library (d3d8ntpr.lib, the
                        kernel's boot animation build of that time)

clang and lld-link come from LLVM_DIR, or Homebrew's llvm.

None of the SDK's files belong in this repository. See port/xbox/README.md.
"""

import os
import shlex
from pathlib import Path
from typing import Any, List, Optional

from .ninja_syntax import Writer
from .linux_build import _load_port_config, game_defines_and_includes, game_sources
from .windows_build import COMMENT, EXPORTED_INLINE

PORT_DIR = Path("port/xbox")
XDK_INCLUDE = Path("port/include/xdk")
LINUX_SRC = Path("port/linux/src")

GAME_FLAGS = [
    "-target", "i386-pc-windows-msvc",
    "-march=pentium3",
    "-fms-extensions",
    "-fms-compatibility",
    "-fms-compatibility-version=13.10",
    "-O2",
    # the game keeps EBP frames (MSVC /Oy-): its stack walker follows them
    "-fno-omit-frame-pointer",
    "-fcommon",
    "-D_XBOX", "-D_X86_", "-D_NTOS_", "-D_MT",
    "-w",
    "-Wno-error=incompatible-pointer-types",
    "-Wno-error=incompatible-function-pointer-types",
    "-Wno-error=int-conversion",
    "-Wno-error=implicit-function-declaration",
    "-Wno-error=implicit-int",
    "-Wno-error=return-type",
]

# the platform units, compiled with the SDK's own headers (OXDK's flags)
SUPPORT_FLAGS = [
    "-target", "i386-pc-windows-msvc",
    "-march=pentium3",
    "-fms-extensions",
    "-fms-compatibility",
    "-fms-compatibility-version=13.10",
    "-O2",
    "-D_XBOX", "-D_X86_", "-DWIN32_LEAN_AND_MEAN", "-D_NTOS_", "-D_MT",
    "-Wno-microsoft-include", "-Wno-pragma-pack", "-Wno-ignored-pragmas",
    "-Wno-deprecated-declarations", "-Wno-writable-strings", "-Wno-microsoft-cast",
    "-Wno-unknown-pragmas", "-Wno-extra-tokens", "-Wno-nonportable-include-path",
    "-Wno-typedef-redefinition", "-Wno-missing-prototype-for-cc", "-Wno-comment",
]
SUPPORT_CXX_FLAGS = ["-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"]

LINK_FLAGS = [
    "/nologo", "/subsystem:windows", "/fixed:no", "/base:0x00010000", "/stack:1048576",
    "/machine:x86", "/entry:mainCRTStartup", "/nodefaultlib", "/safeseh:no",
    "/merge:.edata=.edataxb", "/errorlimit:0", "--rsp-quoting=posix",
]
# (the SDK's D3DX is built on its own Direct3D; port/xbox/src/xbox_d3dx.c has
# the four functions the game calls)
LIBRARIES = [
    "libcmt.lib", "oldnames.lib", "xboxkrnl.lib", "dsound.lib", "xnet.lib", "xapilib.lib", "xkbd.lib",
]
# d3d_intimacy.cpp reads the device as the January library named it; the
# kernel's build of the Aug 2001 library has it in its own namespace (the
# same layout)
ALTERNATE_NAMES = [
    "/alternatename:?g_Device@D3D@@3VCDevice@1@A=?g_Device@D3DK@@3VCDevice@1@A",
]


def _quote(path: Any) -> str:
    text = str(path).replace(os.sep, "/")
    return shlex.quote(text)


def _oxdk_dir(sln: Any) -> Path:
    value = getattr(sln, "oxdk_dir", None) or os.environ.get("OXDK_DIR") or Path.home() / "OXDK"
    return Path(value).expanduser().resolve()


def _d3d8_library(sln: Any) -> Optional[Path]:
    value = getattr(sln, "xbox_d3d8", None)
    return Path(value).expanduser().resolve() if value else None


def _llvm_tool(name: str) -> str:
    llvm = os.environ.get("LLVM_DIR")
    for directory in ([Path(llvm) / "bin"] if llvm else []) + [Path("/opt/homebrew/opt/llvm/bin"),
                                                             Path("/usr/local/opt/llvm/bin")]:
        if (directory / name).is_file():
            return str(directory / name)
    return name


def _inline_export_wrapper(source: Path, build_dir: Path) -> Path:
    """As the Windows build's (tools/windows_build.py): a unit that defines
    an __inline function other units call through a prototype is compiled
    through a wrapper that takes the functions' addresses, so that clang
    emits them (as COMDATs) even where it inlined every call."""
    if source.suffix != ".c":
        return source
    names = EXPORTED_INLINE.findall(COMMENT.sub("", source.read_text(encoding="utf-8", errors="replace")))
    if not names:
        return source
    wrapper = build_dir / "inline_exports" / source
    text = (
        "/* generated by tools/xbox_build.py: see _inline_export_wrapper */\n"
        f'#include "{source.resolve().as_posix()}"\n'
        "static void *const halo_xbox_inline_exports[] __attribute__((used)) = {\n"
        + "".join(f"\t(void *){name},\n" for name in names)
        + "};\n"
    )
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    if not wrapper.is_file() or wrapper.read_text(encoding="utf-8") != text:
        wrapper.write_text(text, encoding="utf-8")
    return wrapper


def xbox_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    return [Path(__file__), PORT_DIR / "src"]


def generate_xbox_build(n: Writer, sln: Any) -> None:
    oxdk = _oxdk_dir(sln)
    xdk = oxdk / "xbox" / "xdk"
    cxbe = oxdk / "xbox" / "tools" / "cxbe" / "cxbe"
    d3d8 = _d3d8_library(sln)
    if not (xdk / "lib" / "xboxkrnl.lib").is_file() or not d3d8 or not d3d8.is_file():
        return
    build_dir: Path = sln.build_dir / "xbox"
    obj_dir = build_dir / "obj"
    gen_dir = build_dir / "gen"
    exe = build_dir / "halo.exe"
    xbe = build_dir / "default.xbe"

    n.comment("Original Xbox build (ninja xbox)")
    n.variable("xbox_cc", _llvm_tool("clang"))
    n.variable("xbox_link", _llvm_tool("lld-link"))
    n.rule(
        name="xbox_cc",
        command="$xbox_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description="XBOX CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="xbox_lld",
        command="$xbox_link $ldflags /map:$map /out:$out @$out.rsp",
        description="XBOX LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline $libs",
    )
    n.rule(
        name="xbox_cxbe",
        command=f"{_quote(cxbe)} -MODE:RETAIL -TITLE:\"Halo\" -LIMIT64MB:no -OUT:$out $in",
        description="XBOX XBE $out",
    )
    n.rule(
        name="xbox_source",
        command="$python tools/xbox_sources.py $kind $in $out",
        description="XBOX GEN $out",
        restat=True,
    )
    n.rule(
        name="xbox_msvc_semantics",
        command="$python tools/linux_msvc_semantics.py --output $out $scan",
        description="XBOX MSVC SEMANTICS $out",
        restat=True,
    )

    semantics_header = build_dir / "halo_msvc_semantics.h"
    game_headers = sorted(p for p in Path("source").rglob("*") if p.suffix in (".c", ".h"))
    # (the file-scope struct tags only: clang's MSVC target already gives a C
    # __inline function MSVC's pick-any COMDAT, which the other ports' weak
    # symbols stand in for)
    n.build(outputs=semantics_header, rule="xbox_msvc_semantics",
            implicit=[Path("tools/linux_msvc_semantics.py"), *game_headers],
            variables={"scan": "--tags source"})

    objects: List[Path] = []

    def add_object(source: Path, cflags: str, implicit: List[Path] = (), compiled: Optional[Path] = None) -> None:
        obj = obj_dir / (source.relative_to(build_dir) if build_dir in source.parents else source)
        obj = obj.with_suffix(obj.suffix + ".obj")
        objects.append(obj)
        n.build(outputs=obj, rule="xbox_cc", inputs=compiled or source,
                implicit=[*implicit, *([source] if compiled and compiled != source else [])],
                variables={"cflags": cflags})

    prefix_header = PORT_DIR / "include" / "halo_xbox_prefix.h"
    # the game's sources, defines and include directories (port/linux/port.json)
    config = _load_port_config()
    game_cflags = " ".join([
        " ".join(GAME_FLAGS), f"-include {_quote(prefix_header)}", f"-include {_quote(semantics_header)}",
        game_defines_and_includes(config), f"-idirafter {_quote(XDK_INCLUDE)}", f"-idirafter {_quote(xdk / 'include')}",
    ])
    for source in game_sources(config):
        add_object(source, f"-std=gnu89 {game_cflags}", [semantics_header, prefix_header],
                   _inline_export_wrapper(source, build_dir))
    # one pick-any copy of each header inline that units call through a
    # prototype (see the file)
    add_object(Path("port/linux/game/msvc_comdat.c"), f"-std=gnu89 {game_cflags}",
               [semantics_header, prefix_header])

    support_cflags = " ".join([*SUPPORT_FLAGS, f"-I{_quote(xdk / 'include')}"])
    for source in sorted((PORT_DIR / "src").glob("*.c")):
        add_object(source, support_cflags)
    for source in sorted((PORT_DIR / "src").glob("*.cpp")):
        add_object(source, " ".join([support_cflags, *SUPPORT_CXX_FLAGS]))
    for kind, source in (("common", LINUX_SRC / "halo_linker_common.c"), ("bink", LINUX_SRC / "bink_null.c")):
        generated = gen_dir / f"xbox_{source.name}"
        n.build(outputs=generated, rule="xbox_source", inputs=source, implicit=[Path("tools/xbox_sources.py")],
                variables={"kind": kind})
        add_object(generated, support_cflags)

    n.build(
        outputs=exe,
        rule="xbox_lld",
        inputs=objects,
        implicit=[d3d8],
        implicit_outputs=[build_dir / "halo.map"],
        variables={
            "ldflags": " ".join([*LINK_FLAGS, *ALTERNATE_NAMES, f"/libpath:{_quote(xdk / 'lib')}"]),
            "map": _quote(build_dir / "halo.map"),
            "libs": " ".join([_quote(d3d8), *LIBRARIES]),
        },
    )
    n.build(outputs=xbe, rule="xbox_cxbe", inputs=exe, implicit=[cxbe])
    n.build(outputs="xbox", rule="phony", inputs=xbe)
    n.newline()
