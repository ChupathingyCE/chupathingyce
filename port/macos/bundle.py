#!/usr/bin/env python3
"""Package build/macos/halo as a macOS application bundle (ninja macos).

The bundle holds the executable, port/macos/Info.plist (the name, the icon,
the version, and the halo:// and Discord URL schemes internet play invites
arrive by) and port/macos/AppIcon.icns, and is registered with Launch
Services so that those links open it.

With --self-contained (configure.py --portable: an application to give to
others), the libraries it uses that are not the system's (SDL3, from
Homebrew) are copied into it (Contents/Frameworks) and it is pointed at
them, and it is signed (ad hoc, unless MACOS_SIGNING_IDENTITY names a
Developer ID), so that it runs on a Mac without Homebrew.

The executable is put in place under a new name and renamed over the old
one: a copy of the game running from the bundle keeps its own.
"""

import argparse
import os
import plistlib
import shutil
import subprocess
from pathlib import Path
from typing import Dict, List

PORT_DIR = Path(__file__).resolve().parent
LSREGISTER = Path("/System/Library/Frameworks/CoreServices.framework/Frameworks/"
                  "LaunchServices.framework/Support/lsregister")
# where a library is the system's, and stays out of the bundle
SYSTEM_PREFIXES = ("/System/", "/usr/lib/")


def linked_libraries(binary: Path) -> List[str]:
    """the libraries a Mach-O file links (otool -L), its own name aside"""
    output = subprocess.run(["otool", "-L", str(binary)], capture_output=True, text=True, check=True).stdout
    # (a universal file lists each architecture's libraries under a header
    # line of its own, "<file> (architecture arm64):": not a library)
    names = [line.strip().split(" (")[0] for line in output.splitlines()[1:]
             if line.strip() and not line.rstrip().endswith(":")]
    own = subprocess.run(["otool", "-D", str(binary)], capture_output=True, text=True).stdout.splitlines()[1:]
    return list(dict.fromkeys(name for name in names if name not in own))


LIBRARY_PATH: List[str] = []


def resolve(name: str, loader: Path) -> Path:
    """a library's file, from its name as a binary links it"""
    if name.startswith("@loader_path/"):
        return (loader.parent / name[len("@loader_path/"):]).resolve()
    if name.startswith("@rpath/"):
        for folder in (*LIBRARY_PATH, "/opt/homebrew/lib", "/usr/local/lib"):
            candidate = Path(folder) / name[len("@rpath/"):]
            if candidate.exists():
                return candidate.resolve()
    return Path(name).resolve()


def bundle_libraries(executable: Path, frameworks: Path) -> List[Path]:
    """the executable's non-system libraries, and theirs, copied into
    frameworks and linked from there; the copies"""
    frameworks.mkdir(parents=True, exist_ok=True)
    copied: Dict[str, Path] = {}
    pending = [executable]
    while pending:
        binary = pending.pop()
        for name in linked_libraries(binary):
            if name.startswith(SYSTEM_PREFIXES) or name.startswith("@executable_path/"):
                continue
            source = resolve(name, binary)
            target = frameworks / Path(name).name
            if target.name not in copied:
                shutil.copy2(source, target)
                target.chmod(0o755)
                subprocess.run(["install_name_tool", "-id", f"@executable_path/../Frameworks/{target.name}",
                                str(target)], check=True, capture_output=True)
                copied[target.name] = target
                pending.append(target)
            subprocess.run(["install_name_tool", "-change", name,
                            f"@executable_path/../Frameworks/{target.name}", str(binary)],
                           check=True, capture_output=True)
    return list(copied.values())


def sign(paths: List[Path], bundle: Path):
    """the libraries, then the bundle, signed: with MACOS_SIGNING_IDENTITY
    (a Developer ID) for the hardened runtime, else ad hoc"""
    identity = os.environ.get("MACOS_SIGNING_IDENTITY", "-")
    options = ["--force", "--sign", identity]
    if identity != "-":
        options += ["--options", "runtime", "--timestamp"]
    for path in paths:
        subprocess.run(["codesign", *options, str(path)], check=True, capture_output=True)
    subprocess.run(["codesign", *options, str(bundle)], check=True, capture_output=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", default="dev", help="the version (tools/version.py), shown in Finder")
    parser.add_argument("--self-contained", action="store_true",
                        help="its libraries copied in, and signed (an application for other Macs)")
    parser.add_argument("--library-path", action="append", default=[],
                        help="where @rpath/ libraries are (before Homebrew's)")
    args = parser.parse_args()
    LIBRARY_PATH[:] = args.library_path

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
    staged = executable.with_name("halo.new")
    shutil.copy2(args.executable, staged)
    staged.chmod(0o755)
    if args.self_contained:
        if (contents / "Frameworks").exists():
            shutil.rmtree(contents / "Frameworks")
        libraries = bundle_libraries(staged, contents / "Frameworks")
        os.replace(staged, executable)
        sign(libraries, args.output)
    else:
        os.replace(staged, executable)
    if LSREGISTER.exists() and not os.environ.get("CI"):
        subprocess.run([str(LSREGISTER), "-f", str(args.output)], check=False)


if __name__ == "__main__":
    main()
