"""Runs the C tests of the save root's usual place and the one-time copy
of the shared saves (port/linux/src/save_folder.c) with the build machine's
C compiler, in a scratch folder. The unit needs only the file helpers of
port/linux/src/posix_files.c, so it compiles anywhere but Windows."""
import shutil
import subprocess
import sys
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


@pytest.mark.skipif(sys.platform == "win32", reason="the tests use POSIX file helpers")
def test_save_folder(tmp_path):
    """Builds the save folder unit with its tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "save_folder_test"
    scratch = tmp_path / "scratch"
    scratch.mkdir()
    libraries = ["-lpthread"] + (["-ldl"] if sys.platform.startswith("linux") else [])
    build = subprocess.run(
        [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-D_GNU_SOURCE", "-D_FILE_OFFSET_BITS=64",
         str(ROOT / "port" / "linux" / "tests" / "save_folder_test.c"),
         str(ROOT / "port" / "linux" / "src" / "save_folder.c"),
         str(ROOT / "port" / "linux" / "src" / "posix_files.c"),
         *libraries, "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary), str(scratch)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
