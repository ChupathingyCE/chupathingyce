"""The iOS port's build (configure.py --console-port: chupathingyce's
tools/console_build.py), ``ninja ios``, and the same for the Apple TV,
``ninja tvos``.

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
builds it): its include/ and lib/libSDL3.a.

The Apple TV's is the same game, built against tvOS's SDK (OpenGL ES 3 is
there too, deprecated as on iOS) into build/tvos/libhalo.a: --tvos-sdl in
--ios-sdl's place, and ``ninja tvos``. HALO_IOS still holds, with HALO_TVOS
beside it where the TV differs (no touch screen: ios_touch.m). The Vision
Pro has no build of its own: visionOS has no OpenGL ES for its own apps
(EAGLContext is unavailable there), so it runs the iPad build, as an iPad
app. The movies' FFmpeg is built here
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


class ApplePlatform:
    """one Apple platform the game builds for: its ninja target and build
    folder (name), xcrun's SDK, clang's target and its own defines"""

    def __init__(self, name: str, sdk: str, target: str, defines: List[str]) -> None:
        self.name = name
        self.sdk = sdk
        self.target = f"--target=arm64-apple-{target}{IOS_MINIMUM}"
        self.defines = defines
        # the SDK's compiler and archiver (xcrun picks the active Xcode's)
        self.cc = f"xcrun --sdk {sdk} clang"
        self.libtool = f"xcrun --sdk {sdk} libtool"


PLATFORMS = [
    ApplePlatform("ios", "iphoneos", "ios", []),
    ApplePlatform("tvos", "appletvos", "tvos", ["-DHALO_TVOS=1"]),
]

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

# what SDL3 (static) and the game need of iOS (and of tvOS, but CoreMotion)
IOS_FRAMEWORKS = [
    "Foundation", "UIKit", "OpenGLES", "QuartzCore", "CoreGraphics", "CoreVideo", "Metal",
    "CoreAudio", "AudioToolbox", "AVFoundation", "GameController", "CoreMotion", "CoreHaptics",
    "CoreBluetooth", "UniformTypeIdentifiers",
]
IOS_LIBRARIES = ["iconv", "m"]

# the game's entry point, renamed: the app has its own main
ENTRY_SOURCE = "source/shell/shell_xbox.c"


def apple_sdk(sdk: str, *arguments: str) -> str:
    """what xcrun answers of an SDK (its compiler's path, the SDK's)"""
    return subprocess.check_output(["xcrun", "--sdk", sdk, *arguments], text=True).strip()


def add_arguments(parser: Any) -> None:
    parser.add_argument(
        "--ios-sdl",
        type=Path,
        metavar="DIR",
        help="the iOS port: SDL3 built and installed for iOS (include/ and lib/libSDL3.a)",
    )
    parser.add_argument(
        "--tvos-sdl",
        type=Path,
        metavar="DIR",
        help="the iOS port's Apple TV build: SDL3 built and installed for tvOS",
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
        n.comment("iOS build (ninja ios, ninja tvos): needs macOS and Xcode")
        return
    if not Path(ENTRY_SOURCE).is_file():
        n.comment("iOS build (ninja ios, ninja tvos): run configure.py from the game's folder (port/ios/build.py)")
        return
    for apple in PLATFORMS:
        sdl = getattr(sln.args, f"{apple.name}_sdl", None)
        if not sdl or not (Path(sdl) / "lib" / "libSDL3.a").is_file():
            n.comment(f"iOS build (ninja {apple.name}): no SDL3 for it (pass --{apple.name}-sdl)")
            continue
        generate_platform(n, sln, port, apple, Path(sdl))


def generate_platform(n: Any, sln: Any, port: Any, apple: ApplePlatform, sdl: Path) -> None:
    name = apple.name
    build_dir: Path = sln.build_dir / name
    library = build_dir / "libhalo.a"
    check = build_dir / "libhalo_check.dylib"
    frameworks = [framework for framework in IOS_FRAMEWORKS if name == "ios" or framework != "CoreMotion"]

    n.comment(f"iOS build (ninja {name}); see port/ios/build.py")
    lp64 = IosBuild(n, sln, name, apple.cc)
    target_flags = [
        apple.target,
        "-DHALO_ANDROID=1",
        "-DHALO_IOS=1",
        *apple.defines,
    ]
    # the movies' FFmpeg (configure.py --without-ffmpeg: the stubs). Its
    # configure takes a compiler, not xcrun's command line: the SDK's clang,
    # and the SDK as the system root
    ffmpeg_cflags: List[str] = []
    ffmpeg_archives: List[Path] = []
    ffmpeg_inputs: List[Path] = []
    if getattr(sln, "with_ffmpeg", True):
        ffmpeg_cflags, _, ffmpeg_stamp, ffmpeg_archives = ffmpeg_build(
            n, name, build_dir / "ffmpeg", apple_sdk(apple.sdk, "--find", "clang"), "darwin", "aarch64",
            [apple.target, f"-isysroot {apple_sdk(apple.sdk, '--show-sdk-path')}"], group=False)
        ffmpeg_inputs = [ffmpeg_stamp, *ffmpeg_archives]
    host = Lp64Host(
        name=name,
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
    generated_sources = (hud_assets_build(n, name, build_dir / "generated" / "hud_hires_assets.c")
                         + ui_fonts_build(n, name, build_dir / "generated" / "ui_fonts.c", sln))
    objects = lp64.objects(host, generated_sources, build_dir / "obj", implicit_inputs=ffmpeg_inputs)

    # the game's stack (ios_stack.S), assembled on its own: the C units'
    # flags are not an assembler's
    label = name.upper()
    n.rule(name=f"{name}_as", command=f"{apple.cc} {apple.target} -c $in -o $out", description=f"{label} AS $out")
    for source in sorted(port.path("src").glob("*.S")):
        obj = build_dir / "obj" / "port" / source.with_suffix(".o").name
        n.build(outputs=obj, rule=f"{name}_as", inputs=source)
        objects.append(obj)

    # the touch controls' overlay (ios_touch.m), UIKit's: Objective-C with
    # ARC, which needs only SDL's headers and the port's own
    n.rule(
        name=f"{name}_objc",
        command=f"{apple.cc} {apple.target} -fobjc-arc -std=gnu11 {OPTIMISATION} -g -Wall "
        f"{' '.join(apple.defines)} -I{_quote(sdl / 'include')} -I{_quote(port.path('include'))} -c $in -o $out",
        description=f"{label} OBJC $out",
    )
    for source in sorted(port.path("src").glob("*.m")):
        obj = build_dir / "obj" / "port" / source.with_suffix(".o").name
        n.build(outputs=obj, rule=f"{name}_objc", inputs=source)
        objects.append(obj)

    # Apple's -filelist reads each line as a path, as it is: ninja quotes
    # the ones with spaces (source/saved games), so the quotes come off first
    unquote = "sed -e \"s/^'//\" -e \"s/'$$//\" $out.rsp > $out.list"
    n.rule(
        name=f"{name}_library",
        command=f"rm -f $out && {unquote} && {apple.libtool} -static -no_warning_for_no_symbols -o $out -filelist $out.list $archives",
        description=f"{label} LIBRARY $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    # (FFmpeg's archives in it too: the app links the one library)
    n.build(outputs=library, rule=f"{name}_library", inputs=objects, implicit=ffmpeg_archives,
            variables={"archives": " ".join(_quote(archive) for archive in ffmpeg_archives)})

    n.rule(
        name=f"{name}_check",
        command=f"{unquote} && {apple.cc} {apple.target} -dynamiclib -o $out -Wl,-filelist,$out.list $libs",
        description=f"{label} LINK CHECK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.build(
        outputs=check,
        rule=f"{name}_check",
        inputs=objects,
        implicit=[sdl / "lib" / "libSDL3.a", *ffmpeg_archives],
        variables={"libs": " ".join([
            *(_quote(archive) for archive in ffmpeg_archives),
            f"-L{_quote(sdl / 'lib')}", "-lSDL3",
            *(f"-l{name}" for name in IOS_LIBRARIES),
            *(f"-framework {framework}" for framework in frameworks),
        ])},
    )
    n.build(outputs=name, rule="phony", inputs=[library, check])
    n.newline()
