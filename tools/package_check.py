#!/usr/bin/env python3
"""Fails if a release package holds a player's own files.

A release's zip, AppImage or tarball is put over a player's install by the
updater, so a file of the player's in it would replace theirs: v0.9.2a's
64-bit Windows and Linux zips held a config.toml (the settings template a
run of the game in the build's folder wrote beside it), and every update
replaced the player's settings (#2029). This lists each package's files and
rejects:

- config.toml and its kin (config.toml.old, config.toml.new, ...);
- logs (debug.txt, halo.log, *.log) and the updater's leftovers (*.old);
- the game's own folders for a player's things: saves (u/), logs/, cache/,
  private/, gametypes/, screenshots/, clips/, forge/, crashes/, maps/ and
  the updater's update.partial/.

brokers.txt (internet play's brokers) is the release's own, and fine.

    python tools/package_check.py <folder, .zip, .tar[.gz|.xz|.bz2] or .AppImage>...

An AppImage is read with unsquashfs (squashfs-tools).
"""

import shutil
import subprocess
import struct
import sys
import tarfile
import zipfile
from pathlib import Path, PurePosixPath
from typing import Iterable, List

# (compared in lower case: Windows and macOS do not tell case apart)
PLAYER_FOLDERS = {"u", "logs", "cache", "private", "gametypes", "screenshots", "clips", "forge", "crashes", "maps",
                  "update.partial"}
PLAYER_FILES = {"debug.txt", "halo.log"}


def player_file(name: str) -> bool:
    """whether a package's path (with / between its parts) is a player's own"""
    parts = [part for part in PurePosixPath(name.replace("\\", "/")).parts if part not in ("", ".", "/")]
    if not parts:
        return False
    lowered = [part.lower() for part in parts]
    base = lowered[-1]
    if base == "config.toml" or base.startswith("config.toml."):
        return True
    if base in PLAYER_FILES or base.endswith(".log") or base.endswith(".old"):
        return True
    return any(part in PLAYER_FOLDERS for part in lowered)


def appimage_offset(path: Path) -> int:
    """where an AppImage's squashfs starts: the end of its ELF runtime"""
    with path.open("rb") as f:
        header = f.read(64)
    if header[:4] != b"\x7fELF":
        raise ValueError(f"{path}: not an ELF file")
    order = "<" if header[5] == 1 else ">"
    if header[4] == 2:
        shoff, = struct.unpack_from(order + "Q", header, 0x28)
        shentsize, shnum = struct.unpack_from(order + "HH", header, 0x3A)
    else:
        shoff, = struct.unpack_from(order + "I", header, 0x20)
        shentsize, shnum = struct.unpack_from(order + "HH", header, 0x2E)
    return shoff + shentsize * shnum


def names_in(path: Path) -> List[str]:
    """a package's files, with / between their parts"""
    if path.is_dir():
        return [file.relative_to(path).as_posix() for file in sorted(path.rglob("*"))]
    if path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as archive:
            return archive.namelist()
    if path.suffix.lower() == ".appimage":
        tool = shutil.which("unsquashfs")
        if not tool:
            raise RuntimeError("no unsquashfs (squashfs-tools) to list the AppImage's files")
        done = subprocess.run([tool, "-o", str(appimage_offset(path)), "-l", str(path)],
                              capture_output=True, text=True, check=True)
        # (each under squashfs-root/, the folder it would unpack to)
        names = []
        for line in done.stdout.splitlines():
            line = line.strip()
            if line.startswith("squashfs-root/"):
                names.append(line[len("squashfs-root/"):])
        return names
    if tarfile.is_tarfile(path):
        with tarfile.open(path) as archive:
            return archive.getnames()
    raise ValueError(f"{path}: not a folder, zip, tarball or AppImage")


def check(paths: Iterable[Path]) -> List[str]:
    """each player's file in the packages, as package: name"""
    found = []
    for path in paths:
        found += [f"{path}: {name}" for name in names_in(path) if player_file(name)]
    return found


def main(arguments: List[str]) -> int:
    if not arguments:
        print(__doc__.strip().splitlines()[0])
        print("usage: python tools/package_check.py <folder, .zip, .tar or .AppImage>...", file=sys.stderr)
        return 2
    try:
        found = check(Path(argument) for argument in arguments)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError, zipfile.BadZipFile,
            tarfile.TarError) as error:
        print(f"error: package check: {error}", file=sys.stderr)
        return 1
    for line in found:
        print(f"error: a player's own file in a release package: {line}", file=sys.stderr)
    if found:
        print("package check: FAIL (an update would put these over the player's own: #2029)", file=sys.stderr)
        return 1
    print(f"package check: {len(arguments)} package(s), no player's files")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
