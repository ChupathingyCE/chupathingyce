"""Runs the C tests of the handler of halo:// links (port/linux/src/
link_register.c, with discord_register.c, whose quoting and marks it shares),
with the build machine's C compiler. The units have no SDL or game
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


def test_link_register(tmp_path):
    """Builds the registration unit with its tests, warnings as errors, and runs
    them on a scratch folder (as ~/.local/share/applications): another
    program's handler is never replaced."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "link_register_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "link_register_test.c"),
         str(ROOT / "port" / "linux" / "src" / "link_register.c"),
         str(ROOT / "port" / "linux" / "src" / "discord_register.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    folder = tmp_path / "applications"
    folder.mkdir()
    result = subprocess.run([str(binary), str(folder)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
