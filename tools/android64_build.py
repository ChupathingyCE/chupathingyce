"""Ninja rules for the native 64-bit Android build (``ninja android64``).

The game and the platform layer shared with Linux (port/linux), compiled as
native AArch64 code (LP64) for Android with the NDK's clang and bionic, as
the other 64-bit builds compile them (tools/lp64_build.py: HALO_64BIT and
the `long` rewrite), and linked into libmain.so, the library SDL3's
SDLActivity loads and whose SDL_main it calls. SDL3 and OpenGL ES 3 are
called directly: there is no guest image, loader or import table, so the
game's memory is not tied to the low 4 GB of the process, where ART's heap
lives (port/android/README.md).

HALO_ANDROID selects what the Android port does differently from the
desktop (OpenGL ES, touch controls, the app's storage); HALO_64BIT, the
64-bit code paths. port/android/native64 holds what the guest build's host
library did for it: the entry point, logging, the storage paths
(android64_main.c), and port/android/host/host_gl.c and host_touch.c are
shared with it.

The result, build/android64/jniLibs/arm64-v8a (libmain.so and the Android
build's libSDL3.so), is packaged by the Gradle module port/android/app64
(``ninja android64_apk``), an app that installs beside the guest build's.
"""

from pathlib import Path
from typing import Any, List

from .android_build import (
    ANDROID_API,
    SDL_DIR,
    _find_ndk,
    fetch_third_party,
    ndk_toolchain,
)
from .embed_assets import hud_assets_build, hud_configure_inputs, ui_fonts_build
from .linux_build import OPTIMISATION, lto_flags
from .lp64_build import (
    LINUX_PORT_CONFIG,
    Lp64Build,
    Lp64Host,
    _quote,
    lp64_configure_inputs,
    lp64_excluded,
    lp64_game_flags,
)
from .ninja_syntax import Writer

PORT_DIR = Path("port/android")
NATIVE_DIR = PORT_DIR / "native64"
ANDROID64_TARGET = f"--target=aarch64-linux-android{ANDROID_API}"
# the Android build's SDL3 (tools/android_build.py), an arm64-v8a library
ANDROID_SDL = Path("build/android/sdl3-build/libSDL3.so")

# bionic has no strict-ISO mode the game's headers could rely on; what the
# game needs is declared, which -Werror=implicit-function-declaration checks
ANDROID64_GAME_FLAGS = lp64_game_flags([])

# Platform files named posix_*.c talk to bionic only, with the host's own
# ABI and no rewrite (their boundary types are posix.h's).
ANDROID64_POSIX_FLAGS = [
    "-std=gnu11",
    "-D_GNU_SOURCE",
    OPTIMISATION,
    "-g",
    "-Wall",
    "-Werror=incompatible-pointer-types",
    "-Werror=int-conversion",
]

LIBRARIES = ["SDL3", "GLESv3", "EGL", "log", "android", "m", "dl"]


def android64_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    if not LINUX_PORT_CONFIG.is_file():
        return [Path(__file__)]
    return [Path(__file__), NATIVE_DIR, *lp64_configure_inputs(), *hud_configure_inputs()]


def generate_android64_build(n: Writer, sln: Any) -> None:
    if not LINUX_PORT_CONFIG.is_file() or not NATIVE_DIR.is_dir():
        return
    ndk = Path(sln.android_ndk) if getattr(sln, "android_ndk", None) else _find_ndk()
    if not ndk or not ndk.is_dir():
        n.comment("Native 64-bit Android build: no NDK found (set ANDROID_NDK_HOME or pass --android-ndk)")
        return
    toolchain = ndk_toolchain(ndk)
    if not toolchain:
        n.comment("Native 64-bit Android build: the NDK has no LLVM toolchain")
        return
    try:
        # (SDL3, built by the Android build's rules)
        fetch_third_party()
    except Exception as error:  # noqa: BLE001 - reported, the build left out
        n.comment(f"Native 64-bit Android build: cannot fetch SDL3 ({error})")
        return
    build_dir: Path = sln.build_dir / "android64"
    jni_dir = build_dir / "jniLibs" / "arm64-v8a"
    libmain = jni_dir / "libmain.so"
    staged_sdl = jni_dir / "libSDL3.so"
    cc = toolchain / "bin" / "clang"

    n.comment("Native 64-bit Android build (ninja android64); see port/android/README.md")
    lp64 = Lp64Build(n, sln, "android64", _quote(cc))
    target_flags = [
        ANDROID64_TARGET,
        # a shared library (SDLActivity loads it)
        "-fPIC",
        "-DHALO_ANDROID=1",
        # (char is unsigned on ARM Linux and Android; the game, as MSVC, has
        # it signed, as every other build does: a char field holding NONE
        # would read as 255)
        "-fsigned-char",
    ]
    host = Lp64Host(
        name="android64",
        target_flags=target_flags,
        game_flags=ANDROID64_GAME_FLAGS,
        posix_flags=ANDROID64_POSIX_FLAGS,
        host_include=f"-I{SDL_DIR / 'include'} -I{PORT_DIR / 'include'} -I{PORT_DIR / 'host'}",
        third_party_flags=["-fno-builtin-wcslen"],
        excluded=set(lp64_excluded()),
        # the app's entry point and services (port/android/native64), and,
        # shared with the guest build's host library, the OpenGL ES helpers
        # and the touch controls' link to their overlay
        host_sources=[*sorted(NATIVE_DIR.glob("*.c")), PORT_DIR / "host" / "host_gl.c",
                      PORT_DIR / "host" / "host_touch.c"],
    )
    generated_sources = (hud_assets_build(n, "android64", build_dir / "generated" / "hud_hires_assets.c")
                         + ui_fonts_build(n, "android64", build_dir / "generated" / "ui_fonts.c", sln))
    lto_cflags, lto_ldflags = lto_flags(sln, build_dir / "thinlto-cache")
    objects = lp64.objects(host, generated_sources, build_dir / "obj", lto_cflags)

    n.rule(
        name="android64_link",
        command=(
            "$android64_cc $ldflags -o $out @$out.rsp $libs"
            " && $python tools/linux_link_check.py $out.rsp"
        ),
        description="ANDROID64 LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.build(
        outputs=libmain,
        rule="android64_link",
        inputs=objects,
        implicit=[ANDROID_SDL, Path("tools/linux_link_check.py")],
        variables={
            "ldflags": " ".join([
                ANDROID64_TARGET, "-shared", "-g", "-fuse-ld=lld", *lto_ldflags,
                # the library's own definitions first, as an executable's are
                # (the game's names that bionic or SDL3 also define)
                "-Wl,-Bsymbolic",
                # 16 KB pages (Android 15 and later), as the guest build's
                "-Wl,-z,max-page-size=16384",
                "-Wl,--no-undefined",
                # (the C++ runtime is not needed: the game is C)
            ]),
            "libs": " ".join([f"-L{ANDROID_SDL.parent}", *(f"-l{lib}" for lib in LIBRARIES)]),
        },
    )
    n.rule(name="android64_copy", command="cp $in $out", description="ANDROID64 STAGE $out")
    n.build(outputs=staged_sdl, rule="android64_copy", inputs=ANDROID_SDL)
    n.build(outputs="android64", rule="phony", inputs=[libmain, staged_sdl])

    # the app (port/android/app64), a Gradle module of the Android project
    # that is only included when asked for (-Pnative64)
    apk = PORT_DIR / "app64" / "build" / "outputs" / "apk" / "debug" / "app64-debug.apk"
    n.rule(
        name="android64_gradle",
        command=(f"cd {PORT_DIR} && ./gradlew --console=plain -q -Pnative64 :app64:assembleDebug && "
                 "touch app64/build/outputs/apk/debug/app64-debug.apk"),
        description="ANDROID64 GRADLE $out",
        pool="console",
    )
    n.build(outputs=apk, rule="android64_gradle", inputs=[libmain, staged_sdl])
    n.build(outputs="android64_apk", rule="phony", inputs=apk)
    n.newline()
