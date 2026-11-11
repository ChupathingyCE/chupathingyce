"""Runs the C tests of DELTA's input rules (port/linux/src/delta_input_rules.c:
controls.delta's keys, Back held in the menus), with the build machine's C
compiler. The unit has no SDL or game dependency, so it compiles anywhere."""
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


def test_delta_input_rules(tmp_path):
    """Builds DELTA's input rules (its keys, Back held in the menus) with
    their tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "delta_input_rules_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "delta_input_rules_test.c"),
         str(ROOT / "port" / "linux" / "src" / "delta_input_rules.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
