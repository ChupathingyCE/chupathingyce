"""Runs the C tests of when a failed web request is asked again
(port/linux/src/retry_backoff.c): 2 s doubling to a minute, Retry-After
taken on a busy answer, never a request a frame. The unit has no SDL or game
dependency, so it compiles anywhere."""
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


def test_retry_backoff(tmp_path):
    """Builds the backoff unit with its tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "retry_backoff_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "retry_backoff_test.c"),
         str(ROOT / "port" / "linux" / "src" / "retry_backoff.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_retry_backoff_address_sanitizer(tmp_path):
    """The same tests under AddressSanitizer and UndefinedBehaviorSanitizer,
    skipped where the compiler has no sanitizers."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "retry_backoff_test_asan"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-fno-omit-frame-pointer",
         "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
         str(ROOT / "port" / "linux" / "tests" / "retry_backoff_test.c"),
         str(ROOT / "port" / "linux" / "src" / "retry_backoff.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    if build.returncode != 0 and "sanitize" in (build.stdout + build.stderr):
        pytest.skip("no sanitizers here: " + (build.stdout + build.stderr)[-200:])
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
