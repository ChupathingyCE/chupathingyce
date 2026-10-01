#!/usr/bin/env python3
"""Package build/macos/halo as a macOS application bundle (ninja macos).

The bundle holds the executable, port/macos/Info.plist (the name, the icon,
the version, and the halo:// and Discord URL schemes internet play invites
arrive by) and
port/macos/AppIcon.icns, and is registered with Launch Services so that
those links open it.
"""

import argparse
import os
import plistlib
import shutil
import subprocess
from pathlib import Path

PORT_DIR = Path(__file__).resolve().parent
LSREGISTER = Path("/System/Library/Frameworks/CoreServices.framework/Frameworks/"
                  "LaunchServices.framework/Support/lsregister")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", default="dev", help="the version (tools/version.py), shown in Finder")
    args = parser.parse_args()

    contents = args.output / "Contents"
    (contents / "MacOS").mkdir(parents=True, exist_ok=True)
    (contents / "Resources").mkdir(parents=True, exist_ok=True)
    with open(PORT_DIR / "Info.plist", "rb") as source:
        info = plistlib.load(source)
    info["CFBundleShortVersionString"] = args.version
    info["CFBundleVersion"] = args.version
    with open(contents / "Info.plist", "wb") as destination:
        plistlib.dump(info, destination)
    shutil.copy2(PORT_DIR / "AppIcon.icns", contents / "Resources" / "AppIcon.icns")
    executable = contents / "MacOS" / "halo"
    shutil.copy2(args.executable, executable)
    executable.chmod(0o755)
    if LSREGISTER.exists() and not os.environ.get("CI"):
        subprocess.run([str(LSREGISTER), "-f", str(args.output)], check=False)


if __name__ == "__main__":
    main()
