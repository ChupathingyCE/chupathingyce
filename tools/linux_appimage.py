#!/usr/bin/env python3
"""Packs the 64-bit Linux build into an AppImage: one file that runs on its
own, made executable and started.

    python tools/linux_appimage.py [dist/chupathingyce-linux64-release]
        [--output dist/chupathingyce-linux64.AppImage] [--appimagetool path]

The build's folder (tools/ci_build.py linux64: the game, its SDL3 and the
files beside them, as the linux64 zip has them) goes into the AppImage's
usr/bin as it is, so the game finds what it finds beside itself in the zip.
Any library the game or SDL3 needs that is not one every desktop has goes
into usr/lib: what ldd finds, less KEEP_HOST (glibc, the graphics drivers'
libraries, X11, Wayland, sound, D-Bus and udev, which must be the system's
own). The portable build (--portable) needs none of these today, so usr/lib
is usually empty. Each library put there has its RUNPATH set to its own
folder with patchelf, since a RUNPATH only covers the file that has it, not
the libraries that file loads.

The AppImage mounts read-only, so the game keeps its settings, log and the
player's maps in the save root's usual place when it runs as one
(platform_app_folder, port/linux/src/port_config.c), and does not update
itself. Its glibc is the build's: tools/linux_glibc_check.py is run on it.

appimagetool (github.com/AppImage/appimagetool, the static type 2 runtime:
no libfuse2 needed to run the result) is taken from --appimagetool or
APPIMAGETOOL, else downloaded into build/appimage. It runs extracted, so it
needs no FUSE either.
"""

import argparse
import os
import re
import shutil
import stat
import subprocess
import sys
import urllib.request
from pathlib import Path
from typing import Dict

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "build/appimage"
APPIMAGETOOL_URL = (
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
)
# the icon, drawn from port/art/icon.svg (512 by 512)
ICON = ROOT / "port/android/art/android-icon.png"
NAME = "chupathingyce"

# the libraries that stay the system's: glibc, the GPU drivers' (OpenGL and
# Vulkan come with the driver), the display servers', sound's, D-Bus and
# udev
KEEP_HOST = re.compile(
    r"^(ld-linux|libc\.so|libm\.so|libdl\.so|libpthread|librt\.so|libgcc_s|libstdc\+\+|libGL|libEGL|"
    r"libGLX|libGLdispatch|libOpenGL|libvulkan|libdrm|libgbm|libX11|libXext\.|libXau|libXdmcp|libxcb|"
    r"libwayland|libdecor|libsystemd|libdbus|libudev|libasound|libpulse|libpipewire|libjack)"
)

APP_RUN = """#!/bin/sh
# The game, with the AppImage's own libraries found first. The caller's
# LD_LIBRARY_PATH is kept in CHUPATHINGYCE_HOST_LD_LIBRARY_PATH.
HERE="$(dirname "$(readlink -f "$0")")"
export CHUPATHINGYCE_HOST_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/halo" "$@"
"""

DESKTOP_ENTRY = f"""[Desktop Entry]
Type=Application
Name=Chupathingy
Comment=A port of the Halo: Combat Evolved decompilation
Exec=AppRun
Icon={NAME}
Categories=Game;ActionGame;
Terminal=false
"""


def libraries_needed(path: Path) -> Dict[str, Path]:
    """The shared libraries ldd finds for path, by name"""
    done = subprocess.run(["ldd", str(path)], capture_output=True, text=True)
    found = {}
    for line in done.stdout.splitlines():
        match = re.match(r"\s*(\S+)\s+=>\s+(\S+)", line)
        if match and Path(match.group(2)).is_file():
            found[match.group(1)] = Path(match.group(2))
    return found


def bundle_libraries(app: Path) -> int:
    """copies into usr/lib every library the game needs that is not
    KEEP_HOST's, until none is missing; how many"""
    library = app / "usr/lib"
    bundled = 0
    while True:
        files = [app / "usr/bin/halo", *sorted((app / "usr/bin").glob("*.so*")), *sorted(library.glob("*.so*"))]
        missing = {}
        for file in files:
            for name, path in libraries_needed(file).items():
                if KEEP_HOST.match(name) or (app / "usr/bin" / name).exists() or (library / name).exists():
                    continue
                missing[name] = path
        if not missing:
            return bundled
        for name, path in sorted(missing.items()):
            print(f"bundled: {name} ({path})", flush=True)
            shutil.copy2(path.resolve(), library / name)
            bundled += 1


def appimagetool(given: str) -> Path:
    if given:
        return Path(given)
    tool = WORK / "appimagetool-x86_64.AppImage"
    if not tool.is_file():
        print(f"downloading {APPIMAGETOOL_URL}", flush=True)
        with urllib.request.urlopen(APPIMAGETOOL_URL) as response, open(tool, "wb") as f:
            shutil.copyfileobj(response, f)
    tool.chmod(tool.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return tool


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("build", nargs="?", type=Path, default=ROOT / "dist/chupathingyce-linux64-release")
    parser.add_argument("--output", type=Path, default=ROOT / f"dist/{NAME}-linux64.AppImage")
    parser.add_argument("--appimagetool", default=os.environ.get("APPIMAGETOOL", ""))
    args = parser.parse_args()
    if not (args.build / "halo").is_file():
        print(f"error: no halo in {args.build} (tools/ci_build.py linux64 release)", file=sys.stderr)
        return 1

    WORK.mkdir(parents=True, exist_ok=True)
    app = WORK / "AppDir"
    if app.exists():
        shutil.rmtree(app)
    (app / "usr/lib").mkdir(parents=True)
    shutil.copytree(args.build, app / "usr/bin", symlinks=True)
    # (an artifact loses its executable bit)
    halo = app / "usr/bin/halo"
    halo.chmod(halo.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    if bundle_libraries(app):
        if not shutil.which("patchelf"):
            print("error: libraries to bundle, and no patchelf to set their RUNPATH", file=sys.stderr)
            return 1
        for library in sorted((app / "usr/lib").glob("*.so*")):
            subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(library)], check=True)

    (app / "AppRun").write_text(APP_RUN)
    (app / "AppRun").chmod(0o755)
    (app / f"{NAME}.desktop").write_text(DESKTOP_ENTRY)
    shutil.copy2(ICON, app / f"{NAME}.png")

    subprocess.run([sys.executable, str(ROOT / "tools/linux_glibc_check.py"), str(app)], check=True)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        args.output.unlink()
    environment = dict(os.environ, ARCH="x86_64", APPIMAGE_EXTRACT_AND_RUN="1")
    subprocess.run([str(appimagetool(args.appimagetool)), str(app), str(args.output)],
                   check=True, env=environment)
    print(f"{args.output.relative_to(ROOT) if args.output.is_relative_to(ROOT) else args.output}: "
          f"{args.output.stat().st_size / 1e6:.1f} MB", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
