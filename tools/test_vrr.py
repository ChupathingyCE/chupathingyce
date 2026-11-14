"""Runs the C tests of display.vrr's rules (port/linux/src/vrr.c): off
unless chosen, auto only where the system says VRR is active, the frames
held 3 under the refresh rate (or at a lower display.max_fps). The unit has
no SDL or game dependency, so it compiles anywhere."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def compiler():
    """Returns the path of clang, cc or gcc (the first found), or None."""
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.mark.parametrize("flags", [[], ["-m32"]], ids=["native", "32-bit"])
def test_vrr(tmp_path, flags):
    """Builds the unit with its tests, warnings as errors, and runs them;
    also 32-bit where the compiler can make it (the DRM structs are fixed
    width)."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "vrr_test"
    libraries = ["-lm"] + (["-ldl"] if sys.platform.startswith("linux") else [])
    build = subprocess.run(
        [cc, "-std=c11", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror", "-O1", *flags,
         str(ROOT / "port" / "linux" / "tests" / "vrr_test.c"),
         str(ROOT / "port" / "linux" / "src" / "vrr.c"),
         "-o", str(binary), *libraries],
        capture_output=True, text=True,
    )
    if build.returncode != 0 and flags:
        pytest.skip("no 32-bit build here: " + (build.stdout + build.stderr)[-200:])
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
