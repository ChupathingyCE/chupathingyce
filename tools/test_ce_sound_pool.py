"""The decoded sounds' file of a Custom Edition map (port/linux/game/
ce_sound_pool.h, used by ce_resources.c): filled to its budget, a new sound
still goes in, moving out the sound read longest ago that is not playing
(port/linux/tests/ce_sound_pool_test.c, built with the build machine's C
compiler)."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "port" / "linux" / "tests" / "ce_sound_pool_test.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_ce_sound_pool(tmp_path):
    """Builds the test, warnings as errors, and runs it."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "ce_sound_pool_test"
    build = subprocess.run([cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", str(TEST), "-o", str(binary)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
