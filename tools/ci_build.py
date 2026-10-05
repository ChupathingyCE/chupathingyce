#!/usr/bin/env python3
"""Builds one native port in one configuration, as the GitHub workflow does
(.github/workflows/build.yml), and collects what it built into dist/:

    python tools/ci_build.py linux debug
    python tools/ci_build.py android release
    python tools/ci_build.py server-x64 release --alpine

Builds are portable (any x86-64 processor; for the arm64 server, any
64-bit ARM), so they run on other computers. Debug builds skip link-time and profile-guided optimisation,
which only make the build slower; release builds use both, as a local
release build does (profile-guided optimisation needs clang 22 or later,
and is skipped with an older one). CI_COMPILER_LAUNCHER (ccache, say) is
passed on as --compiler-launcher.

The version comes from the environment (tools/version.py), which
ChupathingyCE's release workflows set: HALO_VERSION (0.5.0b, or
0.5.0b-nightly.42), HALO_RELEASE_BUILD=1 for a release (whose version must
be VERSION's), and HALO_BUILD_NUMBER, which orders the Android builds.
Without them, a build is VERSION's -dev and never looks for updates.

The dedicated server (server-x86, server-x64, server-arm64:
tools/server_build.py) is built against musl, so that it is one static
program: on a musl system (Alpine Linux) as it is, or with --alpine in
Alpine's Docker container of the server's architecture (an x86 one runs on
an x86-64 machine, an arm64 one on an arm64 machine); it goes into
dist/chupathingyce-server-linux-<arch> (-debug for a debug build), with
its README and playlists. A release's server there is stripped of its debug
information; a debug build's keeps it.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools.version import base_version, release_build, version  # noqa: E402

# what each port's build leaves, and what goes into dist/
OUTPUTS = {
    "linux": ["build/linux/halo"],
    # the native 64-bit build (ninja linux64; tools/linux64_build.py)
    "linux64": ["build/linux64/halo"],
    "windows": ["build/windows/halo.exe", "build/windows/SDL3.dll"],
    # the native 64-bit build (ninja windows64; tools/windows_build.py)
    "windows64": ["build/windows64/halo.exe", "build/windows64/SDL3.dll"],
    "android": [],  # the APK, below
    # the application (universal and self-contained: --portable), whole
    "macos": ["build/macos/ChupathingyCE.app"],
    # the dedicated server (ninja server-<arch>; tools/server_build.py)
    "server-x86": ["build/server-x86/chupathingyce-server"],
    "server-x64": ["build/server-x64/chupathingyce-server"],
    "server-arm64": ["build/server-arm64/chupathingyce-server"],
}
# the servers' Alpine Linux (--alpine), and Docker's name for each one's
# architecture
ALPINE_IMAGE = "alpine:3.22"
ALPINE_PACKAGES = ["clang", "lld", "llvm", "gcc", "musl-dev", "linux-headers", "python3", "samurai", "ccache"]
DOCKER_PLATFORMS = {"server-x86": "linux/386", "server-x64": "linux/amd64", "server-arm64": "linux/arm64"}
APKS = {
    "debug": "port/android/app/build/outputs/apk/debug/app-debug.apk",
    "release": "port/android/app/build/outputs/apk/release/app-release.apk",
}


def run(command, cwd=ROOT):
    print("+", " ".join(str(part) for part in command), flush=True)
    subprocess.run([str(part) for part in command], cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("platform", choices=sorted(OUTPUTS))
    parser.add_argument("config", choices=["debug", "release"])
    parser.add_argument("--alpine", action="store_true",
                        help="a server: build it in Alpine Linux's Docker container (musl, static)")
    args = parser.parse_args()
    server = args.platform.startswith("server-")
    if args.alpine:
        if not server:
            parser.error("--alpine builds the servers only")
        return alpine_build(args.platform, args.config)

    configure = [sys.executable, "configure.py", "--portable"]
    if server:
        # (the profiles are the 32-bit game's, which the servers' clang may
        # not read; a server's work is light)
        configure.append("--pgo=off")
    if args.config == "release":
        configure.append("--release")
    else:
        configure += ["--lto=off", "--pgo=off"]
    launcher = os.environ.get("CI_COMPILER_LAUNCHER")
    if launcher:
        configure += ["--compiler-launcher", launcher]
    # a release is VERSION's, and nothing else is one
    if release_build() and version() != base_version():
        print(f"error: a release build of {version()}, but VERSION is {base_version()}", file=sys.stderr)
        return 1
    print(f"version {version()}{' (a release)' if release_build() else ''}", flush=True)
    run(configure)

    if args.platform == "android":
        # the native part, then the app around it (Gradle's variant of the
        # same name: release is signed with the debug key, not debuggable)
        run(["ninja", "android"])
        gradlew = "gradlew.bat" if os.name == "nt" else "./gradlew"
        run([gradlew, "--console=plain", "-q", f"assemble{args.config.capitalize()}"], cwd=ROOT / "port/android")
        outputs = [APKS[args.config]]
    else:
        run(["ninja", args.platform])
        outputs = OUTPUTS[args.platform]

    if server:
        return server_dist(args.platform, args.config, outputs[0])

    dist = ROOT / "dist" / f"chupathingyce-{args.platform}-{args.config}"
    if dist.exists():
        shutil.rmtree(dist)
    dist.mkdir(parents=True)
    for output in outputs:
        if (ROOT / output).is_dir():
            # (a bundle: its symbolic links as they are, for its signature)
            shutil.copytree(ROOT / output, dist / Path(output).name, symlinks=True)
        else:
            shutil.copy2(ROOT / output, dist)
        print(f"{output} -> {dist.relative_to(ROOT)}", flush=True)
    if args.platform == "macos":
        # (the SDL3 inside it, the build's own: zlib license)
        shutil.copy2(ROOT / "build/macos/third_party/SDL3/LICENSE.txt", dist / "SDL3-LICENSE.txt")
    # the disc image readers (port/linux/src/xiso.c, and the Android app's
    # XisoExtractor.java) follow extract-xiso, whose license asks binaries
    # to carry its notice
    shutil.copy2(ROOT / "port/third_party/extract-xiso/LICENSE.TXT", dist / "extract-xiso-LICENSE.txt")
    if args.platform in ("linux", "linux64"):
        # the self-updater's TLS (port/third_party/mbedtls), whose Apache
        # license asks the same
        shutil.copy2(ROOT / "port/third_party/mbedtls/LICENSE", dist / "mbedtls-LICENSE.txt")
    # internet play's UPnP (port/third_party/miniupnpc), in every build,
    # whose BSD license asks binaries to carry its notice
    shutil.copy2(ROOT / "port/third_party/miniupnpc/LICENSE", dist / "miniupnpc-LICENSE.txt")
    # the text's fonts (port/assets/fonts), embedded in every build, whose
    # SIL Open Font License asks each copy to carry it
    shutil.copy2(ROOT / "port/assets/fonts/Overpass-OFL.txt", dist / "Overpass-OFL.txt")
    # the overlay's fonts (port/linux/ui/fonts), in every build: Noto Sans's
    # license asks the same; Kenney's Input Prompts are CC0, credited all
    # the same
    shutil.copy2(ROOT / "port/linux/ui/fonts/OFL.txt", dist / "NotoSans-OFL.txt")
    shutil.copy2(ROOT / "port/linux/ui/fonts/KENNEY-CC0.txt", dist / "Kenney-Input-Prompts-CC0.txt")
    # the menus' XML parser (port/third_party/expat), in every build, whose
    # MIT license asks copies to carry its notice
    shutil.copy2(ROOT / "port/third_party/expat/COPYING", dist / "expat-COPYING.txt")
    # internet play's MQTT brokers, a file beside the game (network.brokers_file;
    # Android's APK has its own copy)
    if args.platform != "android":
        shutil.copy2(ROOT / "port/assets/network/brokers.txt", dist / "brokers.txt")
    return 0


def alpine_build(platform: str, config: str) -> int:
    """the server, built by this script in Alpine's container of its
    architecture with the checkout mounted in it (and its compile cache,
    with CCACHE_DIR in the checkout)"""
    environment = []
    for name in ("CI_COMPILER_LAUNCHER", "CCACHE_DIR", "CCACHE_BASEDIR", "CCACHE_MAXSIZE", "HALO_VERSION",
                 "HALO_RELEASE_BUILD", "HALO_BUILD_NUMBER"):
        value = os.environ.get(name)
        if not value:
            continue
        # (the checkout is /src in the container)
        if name in ("CCACHE_DIR", "CCACHE_BASEDIR") and Path(value).resolve().is_relative_to(ROOT):
            value = (Path("/src") / Path(value).resolve().relative_to(ROOT)).as_posix()
        environment += ["-e", f"{name}={value}"]
    script = f"apk add --no-cache -q {' '.join(ALPINE_PACKAGES)} && python3 tools/ci_build.py {platform} {config}"
    run(["docker", "run", "--rm", "--platform", DOCKER_PLATFORMS[platform], "-v", f"{ROOT}:/src", "-w", "/src",
         *environment, ALPINE_IMAGE, "sh", "-c", script])
    return 0


def strip_debug(program: Path) -> None:
    """removes a program's debug information (its symbols stay, for crash
    reports): llvm-strip, or binutils' strip"""
    tool = shutil.which("llvm-strip") or shutil.which("strip")
    if tool is None:
        raise SystemExit("error: no llvm-strip or strip to remove the release server's debug information")
    before = program.stat().st_size
    run([tool, "--strip-debug", program])
    print(f"{program.relative_to(ROOT)}: {before / 1e6:.1f} MB -> {program.stat().st_size / 1e6:.1f} MB "
          "(debug information removed)", flush=True)


def server_dist(platform: str, config: str, output: str) -> int:
    """dist/chupathingyce-server-linux-<arch>[-debug]: the server, its
    README, its playlists and the notices its parts' licenses ask for"""
    arch = platform[len("server-"):]
    dist = ROOT / "dist" / f"chupathingyce-server-linux-{arch}{'' if config == 'release' else '-debug'}"
    if dist.exists():
        shutil.rmtree(dist)
    (dist / "playlists").mkdir(parents=True)
    shutil.copy2(ROOT / output, dist)
    if config == "release":
        # a release's download without its debug information (about 20 MB
        # of 33); build/server-<arch>/ keeps the full program, and a debug
        # build ships it
        strip_debug(dist / Path(output).name)
    shutil.copy2(ROOT / "server/README.md", dist / "README.md")
    for playlist in sorted((ROOT / "server/playlists").glob("*.txt")):
        shutil.copy2(playlist, dist / "playlists")
    for source, name in (("port/third_party/mbedtls/LICENSE", "mbedtls-LICENSE.txt"),
                         ("port/third_party/miniupnpc/LICENSE", "miniupnpc-LICENSE.txt"),
                         ("port/third_party/expat/COPYING", "expat-COPYING.txt"),
                         ("port/third_party/extract-xiso/LICENSE.TXT", "extract-xiso-LICENSE.txt"),
                         ("port/assets/fonts/Overpass-OFL.txt", "Overpass-OFL.txt"),
                         ("port/linux/ui/fonts/OFL.txt", "NotoSans-OFL.txt"),
                         ("port/linux/ui/fonts/KENNEY-CC0.txt", "Kenney-Input-Prompts-CC0.txt")):
        shutil.copy2(ROOT / source, dist / name)
    print(f"{output} -> {dist.relative_to(ROOT)}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
