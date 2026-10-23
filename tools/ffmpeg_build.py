"""FFmpeg for the desktop builds: the movies' decoder.

The port plays the movies through the game's own entry points
(port/linux/src/bink_ffmpeg.c) over FFmpeg, and FFmpeg is not on every build
machine (and a 32-bit build cannot take a 64-bit system's). This builds the
five libraries (LGPL, no GPL parts) for the build the port makes, static: the
32-bit Linux build (x86, against the Debian 11 i386 system root of
tools/linux_sysroot.py), the 64-bit Linux build, each architecture of the
macOS application and the two Windows builds (clang for the MSVC target, as
the game itself is built: tools/windows_build.py; FFmpeg's configure and make
run under MSYS2's shell there). The Android guest's is tools/android_ffmpeg.py, through
that build's own pipeline.

Only what the movies use is built: the Bink demuxer, its video and audio
decoders, the file protocol, swscale (the frame to the texture's BGRA) and
swresample (the sound to the mixer's 16-bit stereo). Every other demuxer,
decoder, parser, filter and protocol is left out (--disable-everything), so
the libraries cannot open anything but a Bink file on disk. No assembly, no
network, no programs, and no threads: the movies decode synchronously.

The libraries are called from the game's own units, so they are built with the
same ABI those units have (the build passes them with --flags): on 32-bit x86
the port's flags change how structures are laid out and how they come back from
a function (-malign-double, -freg-struct-return), and one mismatch is a crash
inside a call that returns a small structure, such as av_guess_frame_rate.

Usage: ffmpeg_build.py <build directory> [--target-os linux|darwin|win32|win64] [--arch x86|x86_64|aarch64]
                       [--sysroot <sysroot>] [--cc <compiler>]
                       [--target "<the compiler's target flags>"] [--flags "<the port's ABI flags>"]

The defaults are the 32-bit Linux build's: --target-os linux --arch x86
--target "--target=i686-linux-gnu -m32".
"""

import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

FFMPEG_VERSION = "7.1"
FFMPEG_URL = f"https://ffmpeg.org/releases/ffmpeg-{FFMPEG_VERSION}.tar.xz"
FFMPEG_SHA256 = "40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6"
# (the release's own tarball: this is the digest of the file signed by the
# FFmpeg release signing key, B4322F04D67658D8)
# what the link takes (tools/linux_build.py)
LIBRARIES = ["libavformat", "libavcodec", "libswscale", "libswresample", "libavutil"]


# Windows: MSYS2's shell, which FFmpeg's configure and make need (the runner
# image has it; its make comes from pacman, .github/workflows/build.yml)
MSYS2_BASH = Path(os.environ.get("MSYS2_ROOT", r"C:\msys64")) / "usr" / "bin" / "bash.exe"


def msys_path(path):
    """a Windows path as MSYS2 spells it (C:\\a\\b is /c/a/b): make cannot take
    a drive's colon in its paths"""
    text = str(path).replace("\\", "/")
    if len(text) > 1 and text[1] == ":":
        text = f"/{text[0].lower()}{text[2:]}"
    return text


def run(command, **kwargs):
    if sys.platform == "win32":
        # (through MSYS2's login shell, which keeps the runner's PATH, where
        # clang and LLVM's tools are)
        import shlex
        env = dict(os.environ, MSYS2_PATH_TYPE="inherit", CHERE_INVOKING="1", MSYSTEM="MSYS")
        line = " ".join(shlex.quote(msys_path(part) if isinstance(part, Path) else str(part))
                        for part in command)
        subprocess.run([str(MSYS2_BASH), "-lc", line], check=True, env=env, **kwargs)
        return
    subprocess.run([str(part) for part in command], check=True, **kwargs)


def fetch(root):
    """the FFmpeg source, once"""
    source = root / "src"
    if (source / "configure").is_file():
        return source
    archive = root / f"ffmpeg-{FFMPEG_VERSION}.tar.xz"
    print(f"{Path(__file__).name}: downloading FFmpeg {FFMPEG_VERSION}")
    urllib.request.urlretrieve(FFMPEG_URL, archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != FFMPEG_SHA256:
        archive.unlink()
        raise SystemExit(f"{Path(__file__).name}: {archive.name}: SHA-256 {digest}, "
                         f"expected {FFMPEG_SHA256}")
    with tarfile.open(archive) as tar:
        tar.extractall(root)
    (root / f"ffmpeg-{FFMPEG_VERSION}").rename(source)
    archive.unlink()
    return source


def main():
    arguments = [argument for argument in sys.argv[1:] if not argument.startswith("--")]
    if not arguments:
        raise SystemExit(__doc__)
    root = Path(arguments[0]).resolve()
    sysroot_argument = sys.argv[sys.argv.index("--sysroot") + 1] if "--sysroot" in sys.argv else None
    compiler = sys.argv[sys.argv.index("--cc") + 1] if "--cc" in sys.argv else "clang"
    abi = sys.argv[sys.argv.index("--flags") + 1].split() if "--flags" in sys.argv else []
    target_os = sys.argv[sys.argv.index("--target-os") + 1] if "--target-os" in sys.argv else "linux"
    arch = sys.argv[sys.argv.index("--arch") + 1] if "--arch" in sys.argv else "x86"
    target = (sys.argv[sys.argv.index("--target") + 1].split() if "--target" in sys.argv
              else ["--target=i686-linux-gnu", "-m32"])
    root.mkdir(parents=True, exist_ok=True)
    sysroot = Path(sysroot_argument).resolve() if sysroot_argument else None

    source = fetch(root)
    print(f"ffmpeg_build.py: FFmpeg {FFMPEG_VERSION} in {source} ({target_os}, {arch})")

    build = root / "build"
    # (the port's own target, and against its system root when it has one, so
    # that the libraries are the ones the build links to)
    sysroot_flags = []
    if sysroot:
        multiarch = "i386-linux-gnu" if arch == "x86" else f"{arch}-linux-gnu"
        sysroot_flags = [f"--sysroot={sysroot}", "-isystem", f"{sysroot}/usr/include/{multiarch}",
                         "-L", f"{sysroot}/usr/lib/{multiarch}"]
    windows = target_os in ("win32", "win64")
    # (Windows: LLVM's archiver, and lld for configure's test programs, as the
    # game links)
    windows_tools = ["--ar=llvm-ar", "--nm=llvm-nm", "--ranlib=llvm-ranlib"] if windows else []
    link_flags = ["-fuse-ld=lld"] if windows else []
    configure = [source / "configure",
                 f"--prefix={msys_path(root / 'out') if windows else root / 'out'}",
                 f"--target-os={target_os}", f"--arch={arch}", "--enable-cross-compile",
                 f"--cc={compiler}",
                 "--extra-cflags=" + " ".join(target + abi + sysroot_flags),
                 "--extra-ldflags=" + " ".join(target + link_flags + sysroot_flags),
                 *windows_tools,
                 "--disable-everything", "--disable-programs", "--disable-doc",
                 "--disable-network", "--disable-autodetect", "--disable-asm",
                 "--disable-shared", "--enable-static", "--disable-debug", "--enable-small",
                 "--disable-avfilter", "--disable-avdevice", "--disable-postproc",
                 "--disable-linux-perf", "--disable-pthreads", "--disable-w32threads",
                 # (the video decoder's configure name is "bink": "binkvideo" is only its
                 # codec id)
                 "--enable-demuxer=bink",
                 "--enable-decoder=bink,binkaudio_dct,binkaudio_rdft",
                 "--enable-protocol=file", "--enable-swscale", "--enable-swresample",
                 # (a shared library's, the 64-bit Android app's: position independent)
                 *(["--enable-pic"] if "-fPIC" in target else [])]
    stamp = root / "configure.stamp"
    if stamp.is_file() and stamp.read_text() != "\n".join(str(part) for part in configure):
        # (the options changed: the whole build starts over, generated sources
        # such as libavcodec/codec_list.c among them)
        shutil.rmtree(build)
    if not (build / "config.h").is_file():
        build.mkdir(parents=True, exist_ok=True)
        with open(root / "configure.log", "w") as log:
            run(configure, cwd=build, stdout=log, stderr=subprocess.STDOUT)
        stamp.write_text("\n".join(str(part) for part in configure))

    print(f"ffmpeg_build.py: building with {compiler}")
    with open(root / "build.log", "w") as log:
        try:
            run(["make", "-j", str(os.cpu_count() or 4)], cwd=build, stdout=log, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError:
            # (the compiler's own words are the only clue to a failure, and this
            # log is their only home: show the tail before giving up)
            print(f"ffmpeg_build.py: make failed, the tail of {root / 'build.log'}:")
            print("\n".join((root / "build.log").read_text(errors="replace").splitlines()[-40:]))
            raise

    missing = [library for library in LIBRARIES if not (build / library / f"{library}.a").is_file()]
    if missing:
        raise SystemExit("ffmpeg_build.py: missing " + ", ".join(missing))
    # (the stamp tools/linux_build.py waits for: the headers exist from here,
    # whatever the build's order; written only when its content changes, so that
    # a rebuild of these libraries does not relink the whole game)
    game_stamp = root / "ffmpeg.stamp"
    content = "\n".join(LIBRARIES) + "\n"
    if not game_stamp.is_file() or game_stamp.read_text() != content:
        game_stamp.write_text(content)
    print("ffmpeg_build.py: " + ", ".join(f"{library}.a" for library in LIBRARIES))


if __name__ == "__main__":
    main()
