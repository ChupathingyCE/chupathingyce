"""Runs the C tests of the game lobby's layout (port/linux/src/lobby_layout.c):
at every screen shape and player count the list, the card, the facts and
the menu stay inside the screen and apart, the rows in their columns, and
the selected row in view. The unit has no SDL or game dependency, so it
compiles anywhere."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def compiler():
    """Finds a C compiler on the build machine: clang, cc or gcc, or None."""
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def build_and_run(tmp_path, name, extra):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / name
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", *extra,
         str(ROOT / "port" / "linux" / "tests" / "lobby_layout_test.c"),
         str(ROOT / "port" / "linux" / "src" / "lobby_layout.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    return build, binary


def test_lobby_layout(tmp_path):
    """Builds the layout unit with its tests, warnings as errors, and runs them."""
    build, binary = build_and_run(tmp_path, "lobby_layout_test", [])
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_lobby_layout_address_sanitizer(tmp_path):
    """The same under AddressSanitizer and UndefinedBehaviorSanitizer; skipped
    where the compiler has no sanitizers."""
    build, binary = build_and_run(tmp_path, "lobby_layout_test_asan",
                                  ["-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
                                   "-fno-sanitize-recover=all"])
    if build.returncode != 0 and "sanitize" in (build.stdout + build.stderr):
        pytest.skip("no sanitizers")
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
