"""Runs the C tests of Online Games' pings before joining
(port/linux/src/p2p_probe.c) and their estimates (browser_ping.c), with the
build machine's C compiler. Neither unit has a socket, SDL or game
dependency, so they compile anywhere."""
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


def test_ping(tmp_path):
    """Builds both units with their tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "ping_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "ping_test.c"),
         str(ROOT / "port" / "linux" / "src" / "p2p_probe.c"),
         str(ROOT / "port" / "linux" / "src" / "browser_ping.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
