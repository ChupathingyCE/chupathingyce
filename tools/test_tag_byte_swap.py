"""Build the console tag byte swap's host test (port/console/tests/
tag_byte_swap_test.c, with port/console/game/tag_byte_swap.c read as a
big-endian console reads) and run its model made in the test: parts that
share index ranges, overlap them, and overlap a triangles block, as Halo
CE+X maps have them. Every index word must be swapped once.

No map needed (the maps themselves: port/console/tests/run_tag_byte_swap_test.sh).
Needs a C compiler and zlib. Usage: test_tag_byte_swap.py [--cc clang]
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "port/console/tests"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cc", default=shutil.which("cc") or "cc")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as folder:
        program = Path(folder) / "tag_byte_swap_test"
        subprocess.run([args.cc, "-O1", "-Wall", "-Werror", "-DTAG_BYTE_SWAP_EMULATE_BIG_ENDIAN",
            "-I", str(TESTS / "stubs"), "-I", str(ROOT / "port/console/include"),
            "-o", str(program), str(TESTS / "tag_byte_swap_test.c"), str(ROOT / "port/console/game/tag_byte_swap.c"),
            str(ROOT / "port/console/game/tag_byte_swap_tables.c"), "-lz"], check=True)
        return subprocess.run([str(program), "--synthetic"]).returncode


if __name__ == "__main__":
    sys.exit(main())
