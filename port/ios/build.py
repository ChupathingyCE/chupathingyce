"""The iOS port's build (configure.py --console-port: chupathingyce's
tools/console_build.py), ``ninja ios``.

The game and the platform layer are the native 64-bit Android build's
(chupathingyce's tools/android64_build.py): the Linux port's (port/linux),
compiled as 64-bit code (tools/lp64_build.py: HALO_64BIT and the `long`
rewrite) with HALO_ANDROID, which gives it OpenGL ES 3, the touch controls'
hooks and the mobile storage, and HALO_IOS where iOS differs
(patches/chupathingyce). What the Android build's host does around the game
is port/ios/src: the entry point (ios_main.c), the OpenGL ES helpers
(ios_gl.c), the touch controls and their hooks (ios_touch.m) and the stack
the game runs on (ios_stack.S).

The result is build/ios/libhalo.a, a static library the app links
(app/project.yml), and build/ios/libhalo_check.dylib, the same
objects linked with SDL3 and iOS's frameworks, which fails if anything the
game calls is missing.

Run from the game's folder, which the 64-bit build's rules are written for:

    cd chupathingyce
    python3 configure.py --release --console-port ../port/ios --ios-sdl ../build/sdl3-ios
    ninja ios

--ios-sdl is SDL3 built and installed for iOS (.github/workflows/engine.yml
builds it): its include/ and lib/libSDL3.a. The movies' FFmpeg is built here
too (chupathingyce's tools/ffmpeg_build.py, as for macOS) and goes into
libhalo.a; --without-ffmpeg leaves it out, and the game skips the movies.
"""

import platform
import shutil
import subprocess
from pathlib import Path
from typing import Any, List

from tools.embed_assets import hud_assets_build, hud_configure_inputs, ui_fonts_build
from tools.linux_build import OPTIMISATION, ffmpeg_build
from tools.lp64_build import (
    Lp64Build,
    Lp64Host,
    Lp64Unit,
    _quote,
    lp64_configure_inputs,
    lp64_excluded,
    lp64_game_flags,
)

IOS_MINIMUM = "16.4"
IOS_TARGET = f"--target=arm64-apple-ios{IOS_MINIMUM}"
# the SDK's compiler and archiver (xcrun picks the active Xcode's)
IOS_CC = "xcrun --sdk iphoneos clang"
IOS_LIBTOOL = "xcrun --sdk iphoneos libtool"

# the game's units: the macOS build's (tools/macos_build.py), whose C library
# is iOS's too
IOS_GAME_FLAGS = lp64_game_flags(["-D_ANSI_SOURCE"])

# the posix_*.c units, with the host's own ABI (tools/macos_build.py's)
IOS_POSIX_FLAGS = [
    "-std=gnu11",
    "-D_DARWIN_C_SOURCE",
    OPTIMISATION,
    "-g",
    "-Wall",
    "-Werror=incompatible-pointer-types",
    "-Werror=int-conversion",
]

# what SDL3 (static) and the game need of iOS
IOS_FRAMEWORKS = [
    "Foundation", "UIKit", "OpenGLES", "QuartzCore", "CoreGraphics", "CoreVideo", "Metal",
    "CoreAudio", "AudioToolbox", "AVFoundation", "GameController", "CoreMotion", "CoreHaptics",
    "CoreBluetooth", "UniformTypeIdentifiers",
]
IOS_LIBRARIES = ["iconv", "m"]

# the game's entry point, renamed: the app has its own main
ENTRY_SOURCE = "source/shell/shell_xbox.c"


def ios_sdk(*arguments: str) -> str:
    """what xcrun answers of the iPhone SDK (its compiler's path, the SDK's)"""
    return subprocess.check_output(["xcrun", "--sdk", "iphoneos", *arguments], text=True).strip()


def add_arguments(parser: Any) -> None:
    parser.add_argument(
        "--ios-sdl",
        type=Path,
        metavar="DIR",
        help="the iOS port: SDL3 built and installed for iOS (include/ and lib/libSDL3.a)",
    )


def configure_inputs(port: Any) -> List[Path]:
    return [port.path("src"), port.path("include"), *lp64_configure_inputs(), *hud_configure_inputs()]


class IosBuild(Lp64Build):
    """the 64-bit build's units, with the game's main() renamed halo_main
    (port/ios/src/ios_main.c calls it)"""

    def units(self, host: Lp64Host, generated_sources: List[Path]) -> List[Lp64Unit]:
        units = super().units(host, generated_sources)
        for unit in units:
            if unit.source.as_posix().endswith(ENTRY_SOURCE):
                unit.cflags += " -Dmain=halo_main"
        return units


def generate(n: Any, sln: Any, port: Any) -> None:
    if platform.system() != "Darwin" or not shutil.which("xcrun"):
        n.comment("iOS build (ninja ios): needs macOS and Xcode")
        return
    if not Path(ENTRY_SOURCE).is_file():
        n.comment("iOS build (ninja ios): run configure.py from the game's folder (port/ios/build.py)")
        return
    sdl = getattr(sln.args, "ios_sdl", None)
    if not sdl or not (Path(sdl) / "lib" / "libSDL3.a").is_file():
        n.comment("iOS build (ninja ios): no SDL3 for iOS (pass --ios-sdl)")
        return
    sdl = Path(sdl)
    build_dir: Path = sln.build_dir / "ios"
    library = build_dir / "libhalo.a"
    check = build_dir / "libhalo_check.dylib"

    n.comment("iOS build (ninja ios); see port/ios/build.py")
    lp64 = IosBuild(n, sln, "ios", IOS_CC)
    target_flags = [
        IOS_TARGET,
        "-DHALO_ANDROID=1",
        "-DHALO_IOS=1",
    ]
    # the movies' FFmpeg (configure.py --without-ffmpeg: the stubs). Its
    # configure takes a compiler, not xcrun's command line: the SDK's clang,
    # and the SDK as the system root
    ffmpeg_cflags: List[str] = []
    ffmpeg_archives: List[Path] = []
    ffmpeg_inputs: List[Path] = []
    if getattr(sln, "with_ffmpeg", True):
        ffmpeg_cflags, _, ffmpeg_stamp, ffmpeg_archives = ffmpeg_build(
            n, "ios", build_dir / "ffmpeg", ios_sdk("--find", "clang"), "darwin", "aarch64",
            [IOS_TARGET, f"-isysroot {ios_sdk('--show-sdk-path')}"], group=False)
        ffmpeg_inputs = [ffmpeg_stamp, *ffmpeg_archives]
    host = Lp64Host(
        name="ios",
        target_flags=target_flags,
        game_flags=IOS_GAME_FLAGS,
        posix_flags=IOS_POSIX_FLAGS,
        # SDL3, and the Khronos OpenGL ES headers gl.h includes (iOS's own
        # are OpenGLES/ES3/gl.h)
        host_include=f"-I{_quote(sdl / 'include')} -I{_quote(port.path('include'))}",
        excluded=set(lp64_excluded()),
        host_sources=sorted(port.path("src").glob("*.c")),
        ffmpeg_cflags=ffmpeg_cflags,
    )
    generated_sources = (hud_assets_build(n, "ios", build_dir / "generated" / "hud_hires_assets.c")
                         + ui_fonts_build(n, "ios", build_dir / "generated" / "ui_fonts.c", sln))
    objects = lp64.objects(host, generated_sources, build_dir / "obj", implicit_inputs=ffmpeg_inputs)

    # the game's stack (ios_stack.S), assembled on its own: the C units'
    # flags are not an assembler's
    n.rule(name="ios_as", command=f"{IOS_CC} {IOS_TARGET} -c $in -o $out", description="IOS AS $out")
    for source in sorted(port.path("src").glob("*.S")):
        obj = build_dir / "obj" / "port" / source.with_suffix(".o").name
        n.build(outputs=obj, rule="ios_as", inputs=source)
        objects.append(obj)

    # the touch controls' overlay (ios_touch.m), UIKit's: Objective-C with
    # ARC, which needs only SDL's headers
    n.rule(
        name="ios_objc",
        command=f"{IOS_CC} {IOS_TARGET} -fobjc-arc -std=gnu11 {OPTIMISATION} -g -Wall "
        f"-I{_quote(sdl / 'include')} -c $in -o $out",
        description="IOS OBJC $out",
    )
    for source in sorted(port.path("src").glob("*.m")):
        obj = build_dir / "obj" / "port" / source.with_suffix(".o").name
        n.build(outputs=obj, rule="ios_objc", inputs=source)
        objects.append(obj)

    # Apple's -filelist reads each line as a path, as it is: ninja quotes
    # the ones with spaces (source/saved games), so the quotes come off first
    unquote = "sed -e \"s/^'//\" -e \"s/'$$//\" $out.rsp > $out.list"
    n.rule(
        name="ios_library",
        command=f"rm -f $out && {unquote} && {IOS_LIBTOOL} -static -no_warning_for_no_symbols -o $out -filelist $out.list $archives",
        description="IOS LIBRARY $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    # (FFmpeg's archives in it too: the app links the one library)
    n.build(outputs=library, rule="ios_library", inputs=objects, implicit=ffmpeg_archives,
            variables={"archives": " ".join(_quote(archive) for archive in ffmpeg_archives)})

    n.rule(
        name="ios_check",
        command=f"{unquote} && {IOS_CC} {IOS_TARGET} -dynamiclib -o $out -Wl,-filelist,$out.list $libs",
        description="IOS LINK CHECK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.build(
        outputs=check,
        rule="ios_check",
        inputs=objects,
        implicit=[sdl / "lib" / "libSDL3.a", *ffmpeg_archives],
        variables={"libs": " ".join([
            *(_quote(archive) for archive in ffmpeg_archives),
            f"-L{_quote(sdl / 'lib')}", "-lSDL3",
            *(f"-l{name}" for name in IOS_LIBRARIES),
            *(f"-framework {name}" for name in IOS_FRAMEWORKS),
        ])},
    )
    n.build(outputs="ios", rule="phony", inputs=[library, check])
    n.newline()
