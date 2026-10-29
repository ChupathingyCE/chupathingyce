"""Build and run the host test of a map's pixels as a big-endian console's
CPU reads them (port/console/tests/bitmap_byte_order_test.c, with
port/console/game/bitmap_byte_order.c and source/bitmaps/s3tc/s3tc.c): the
lightmap a particle or an object is lit by, and DXT blocks, come out as the
Xbox reads them. s3tc.c is built as the 64-bit builds build it, through
tools/lp64_rewrite.py. Needs a C compiler. Usage:
test_bitmap_byte_order.py [--cc clang]
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
        folder = Path(folder)
        s3tc = folder / "bitmaps" / "s3tc"
        s3tc.mkdir(parents=True)
        for name in ("s3tc.h", "s3tc.c"):
            subprocess.run([sys.executable, str(ROOT / "tools/lp64_rewrite.py"), "--output", str(s3tc / name),
                str(ROOT / "source/bitmaps/s3tc" / name)], check=True)
        program = folder / "bitmap_byte_order_test"
        subprocess.run([args.cc, "-O1", "-Wall", "-Werror",
            "-I", str(TESTS / "stubs"), "-I", str(folder), "-I", str(ROOT / "port/console/include"),
            "-o", str(program), str(TESTS / "bitmap_byte_order_test.c"),
            str(ROOT / "port/console/game/bitmap_byte_order.c"), str(s3tc / "s3tc.c"), "-lm"], check=True)
        return subprocess.run([str(program)]).returncode


if __name__ == "__main__":
    sys.exit(main())
