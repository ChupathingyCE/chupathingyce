"""Runs the C tests of one public game listed under every network version,
shown once (port/linux/src/p2p_copies.c), with the build machine's C
compiler. The unit has no SDL or game dependency, so it compiles
anywhere."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_p2p_copies(tmp_path):
    """Builds the copies unit with its tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "p2p_copies_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "p2p_copies_test.c"),
         str(ROOT / "port" / "linux" / "src" / "p2p_copies.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
