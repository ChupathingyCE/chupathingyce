#!/usr/bin/env python3
"""Builds one native port in one configuration, as the GitHub workflow does
(.github/workflows/build.yml), and collects what it built into dist/:

    python tools/ci_build.py linux debug
    python tools/ci_build.py android release

Builds are portable (any x86-64 processor), so they run on other
computers. Debug builds skip link-time and profile-guided optimisation,
which only make the build slower; release builds use both, as a local
release build does (profile-guided optimisation needs clang 22 or later,
and is skipped with an older one). CI_COMPILER_LAUNCHER (ccache, say) is
passed on as --compiler-launcher.

The version comes from the environment (tools/version.py), which
ChupathingyCE's release workflows set: HALO_VERSION (0.5.0b, or
0.5.0b-nightly.42), HALO_RELEASE_BUILD=1 for a release (whose version must
be VERSION's), and HALO_BUILD_NUMBER, which orders the Android builds.
Without them, a build is VERSION's -dev and never looks for updates.
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
}
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
    args = parser.parse_args()

    configure = [sys.executable, "configure.py", "--portable"]
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
