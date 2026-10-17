#!/usr/bin/env python3
"""Fails if a Linux build needs a newer glibc than its baseline.

The portable Linux builds (configure.py --portable: tools/linux_sysroot.py)
are linked against Debian 11's glibc 2.31, so that they start on SteamOS and
older distributions; an executable or library that asks for a later symbol
version (GLIBC_2.43, from the build machine's glibc) would not start there.
This reads each ELF file's symbol versions (readelf -V) and rejects any
GLIBC_ version above the baseline, and glibc's private symbols.

    python tools/linux_glibc_check.py [--max 2.31] [--static] <file or folder>...

A folder is searched for ELF files (the executables and libSDL3.so.0 of a
dist/ folder). With --static (the dedicated servers, linked against musl:
tools/server_build.py), each file must also need no shared library at all.
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List, Tuple

BASELINE = "2.31"
GLIBC_VERSION = re.compile(r"\bGLIBC_(\d+(?:\.\d+)+)\b")
ELF_MAGIC = b"\x7fELF"


def readelf() -> List[str]:
    tool = shutil.which("readelf") or shutil.which("llvm-readelf")
    if tool is None:
        sys.exit("error: no readelf or llvm-readelf to read the builds' symbol versions")
    return [tool]


def version_tuple(text: str) -> Tuple[int, ...]:
    return tuple(int(part) for part in text.split("."))


def elf_files(paths: List[Path]) -> List[Path]:
    files = []
    for path in paths:
        candidates = sorted(p for p in path.rglob("*") if p.is_file()) if path.is_dir() else [path]
        for candidate in candidates:
            if candidate.is_symlink():
                continue
            with open(candidate, "rb") as f:
                if f.read(4) == ELF_MAGIC:
                    files.append(candidate)
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--max", default=BASELINE, help=f"the newest glibc allowed (default {BASELINE})")
    parser.add_argument("--static", action="store_true", help="each file must need no shared library")
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()
    limit = version_tuple(args.max)
    tool = readelf()

    files = elf_files(args.paths)
    if not files:
        print(f"error: no ELF file in {', '.join(map(str, args.paths))}", file=sys.stderr)
        return 1
    failed = False
    for path in files:
        versions = subprocess.run([*tool, "-V", "-W", str(path)], check=True, capture_output=True,
                                  text=True).stdout
        found = sorted(set(GLIBC_VERSION.findall(versions)), key=version_tuple)
        newest = found[-1] if found else None
        problems = []
        too_new = [v for v in found if version_tuple(v) > limit]
        if too_new:
            problems.append(f"needs GLIBC_{', GLIBC_'.join(too_new)}, newer than {args.max}")
        if "GLIBC_PRIVATE" in versions:
            problems.append("uses glibc's private symbols (GLIBC_PRIVATE)")
        if args.static:
            dynamic = subprocess.run([*tool, "-d", "-W", str(path)], check=True, capture_output=True,
                                     text=True).stdout
            needed = re.findall(r"\(NEEDED\).*\[(.*)\]", dynamic)
            if needed:
                problems.append(f"is not static: needs {', '.join(needed)}")
        if problems:
            failed = True
            for problem in problems:
                print(f"{path}: {problem}", file=sys.stderr)
        else:
            print(f"{path}: {'needs GLIBC_' + newest + ' at most' if newest else 'needs no glibc version'}"
                  f"{' (static)' if args.static else ''}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
