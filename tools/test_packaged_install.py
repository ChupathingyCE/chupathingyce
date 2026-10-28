"""Runs the C tests of a packaged install's mark and where its files go
(port/linux/src/packaged_install.h) with the build machine's C compiler, in
a scratch folder: once as a normal build, once as configure.py --packaged's
(HALO_PACKAGED). What the settings and the updater do with it is checked by
tools/test_port_config.py and tools/test_update_install.py."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def compiler():
    """the first of clang, cc and gcc found, or None"""
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.mark.skipif(sys.platform == "win32", reason="the tests use POSIX file helpers")
@pytest.mark.parametrize("built", [False, True])
def test_packaged_install(tmp_path, built):
    """Builds the tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "packaged_install_test"
    build = subprocess.run(
        [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", *(["-DHALO_PACKAGED"] if built else []),
         str(ROOT / "port" / "linux" / "tests" / "packaged_install_test.c"), "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary), str(tmp_path), *(["built"] if built else [])],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
