"""Runs the C tests of the host's party teams plan
(port/linux/src/party_teams_plan.c: keeping a party on one team), with the
build machine's C compiler. The unit has no game dependency, so it compiles
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


def test_party_teams(tmp_path):
    """Builds the plan with its tests, as C89 with warnings as errors (the
    consoles' compilers read it too), and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "party_teams_test"
    build = subprocess.run(
        [cc, "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "party_teams_test.c"),
         str(ROOT / "port" / "linux" / "src" / "party_teams_plan.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
