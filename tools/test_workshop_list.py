"""Runs the C tests of the Delta Guide's Workshop list document
(port/linux/src/workshop_list.c), with the build machine's C compiler. The
unit has no SDL, game or file system dependency, so it compiles anywhere."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def test_workshop_list(tmp_path):
    """Builds the list unit with its tests, warnings as errors, and runs them."""
    cc = next((path for path in map(shutil.which, ("clang", "cc", "gcc")) if path), None)
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "workshop_list_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "workshop_list_test.c"),
         str(ROOT / "port" / "linux" / "src" / "workshop_list.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
