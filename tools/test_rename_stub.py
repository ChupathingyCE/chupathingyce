"""The halo stub 0.9.0 and 0.9.1's downloads carried after the rename to
chupathingy (port/linux/src/rename_stub.h), which the game now removes: the
C tests of the rules that tell it from any other halo or halo.exe, built
with the build machine's C compiler and run on the Unix stub as it shipped
(tools/tests/rename_stub_halo.sh)."""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tools/tests/rename_stub_halo.sh"
MARK = "chupathingy-halo-stub 1"


def compiler():
    """the first of clang, cc and gcc found, or None"""
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_stub_rules(tmp_path):
    """Builds the C tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        # (pytest only here: the Windows build runs this file alone, without it)
        import pytest
        pytest.skip("no C compiler")
    binary = tmp_path / ("rename_stub_test.exe" if sys.platform == "win32" else "rename_stub_test")
    # (Windows' C library calls fopen deprecated: the other tools tests' define)
    windows = ["-D_CRT_SECURE_NO_WARNINGS"] if sys.platform == "win32" else []
    build = subprocess.run([cc, *windows, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1",
                            str(ROOT / "port/linux/tests/rename_stub_test.c"), "-o", str(binary)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary), str(SCRIPT)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_marks():
    """The stub carries the mark; the header defines the same one."""
    assert SCRIPT.read_text().splitlines()[1] == f"# {MARK}"
    assert f'#define RENAME_STUB_MARK "{MARK}"' in (ROOT / "port/linux/src/rename_stub.h").read_text()



if __name__ == "__main__":
    # (run alone, without pytest: the Windows build's check, .github/workflows/build.yml)
    with tempfile.TemporaryDirectory() as folder:
        if not compiler():
            sys.exit("no C compiler")
        test_stub_rules(Path(folder))
    test_marks()
    print("ok")
