"""Runs the C tests of the Guide's pane layouts (port/linux/src/guide_layout.c):
the Workshop item's page and the Steam sign-in's window keep their pieces
apart and in their boxes at any length of text, and Steam's markup is
stripped. The unit has no SDL or game dependency, so it compiles anywhere."""
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


def test_guide_layout(tmp_path):
    """Builds the layout unit with its tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "guide_layout_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "guide_layout_test.c"),
         str(ROOT / "port" / "linux" / "src" / "guide_layout.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_guide_layout_address_sanitizer(tmp_path):
    """The same tests under AddressSanitizer and UndefinedBehaviorSanitizer:
    the wrap past its line's buffer and the plain text past its end the
    review found stay caught. Skipped where the compiler has no sanitizers."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "guide_layout_test_asan"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-fno-omit-frame-pointer",
         "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
         str(ROOT / "port" / "linux" / "tests" / "guide_layout_test.c"),
         str(ROOT / "port" / "linux" / "src" / "guide_layout.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    if build.returncode != 0 and "sanitize" in (build.stdout + build.stderr):
        pytest.skip("no sanitizers here: " + (build.stdout + build.stderr)[-200:])
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
