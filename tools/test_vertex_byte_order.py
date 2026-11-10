"""A structure bsp's vertices read the same on a big-endian machine: the
readers of source/rasterizer/rasterizer_vertex_byte_order.h, which the level
culling, the visibility walk and object lighting use for the map's
little-endian vertex data (port/linux/tests/vertex_byte_order_test.c). Built
natively, and for a big-endian Linux under QEMU's user mode with the
toolchain tools/test_delta_big_endian.py finds (it skips without one unless
DELTA_BE_REQUIRED=1)."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_delta_big_endian import build_and_run  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "port" / "linux" / "tests" / "vertex_byte_order_test.c"


def test_native(tmp_path):
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "vertex_byte_order_test"
    build = subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", str(TEST), "-o", str(binary)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "vertex_byte_order_test: ok" in run.stdout


def test_big_endian(tmp_path):
    output = build_and_run(tmp_path, "vertex_byte_order_test", [TEST], [])
    assert "vertex_byte_order_test: ok (big-endian)" in output
