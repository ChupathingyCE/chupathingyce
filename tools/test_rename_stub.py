"""The halo stub the downloads carry after the rename to chupathingy
(port/linux/src/rename_stub.h, port/rename_stub/): the C tests of its rules, built
with the build machine's C compiler, and the Unix stub, halo.sh, run
beside a stand-in chupathingy: it passes every argument on as it was, from
any working folder and through a link, makes a chupathingy that lost its
executable bit executable again, and says so when there is none. Also that
the Windows stub carries the mark the game's clean-up keeps it by."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "port/rename_stub/halo.sh"
STUB = ROOT / "port/rename_stub/halo_stub.c"
MARK = "chupathingy-halo-stub 1"

posix = pytest.mark.skipif(sys.platform == "win32", reason="the Unix stub is a shell script")


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
        pytest.skip("no C compiler")
    binary = tmp_path / "rename_stub_test"
    build = subprocess.run([cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1",
                            str(ROOT / "port/linux/tests/rename_stub_test.c"), "-o", str(binary)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary), str(SCRIPT)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_marks():
    """Both stubs carry the mark; the header defines the same one."""
    assert SCRIPT.read_text().splitlines()[1] == f"# {MARK}"
    assert "RENAME_STUB_MARK" in STUB.read_text()
    assert f'#define RENAME_STUB_MARK "{MARK}"' in (ROOT / "port/linux/src/rename_stub.h").read_text()
    assert os.access(SCRIPT, os.X_OK), "halo.sh is committed executable (the builds copy it as it is)"


def install(folder: Path) -> Path:
    """halo (the stub) and a stand-in chupathingy that prints its
    arguments, one to a line, and its working folder"""
    folder.mkdir(parents=True, exist_ok=True)
    halo = folder / "halo"
    shutil.copy(SCRIPT, halo)
    halo.chmod(0o755)
    game = folder / "chupathingy"
    game.write_text('#!/bin/sh\necho "cwd=$(pwd -P)"\nfor a in "$@"; do echo "arg=$a"; done\nexit 7\n')
    game.chmod(0o755)
    return halo


@posix
def test_unix_stub_passes_arguments(tmp_path):
    halo = install(tmp_path / "game folder")
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    result = subprocess.run([str(halo), "-window", "two words", "", "halo://join/abc"], cwd=elsewhere,
                            capture_output=True, text=True)
    assert result.returncode == 7
    assert result.stdout.splitlines() == [f"cwd={elsewhere.resolve()}", "arg=-window", "arg=two words", "arg=",
                                          "arg=halo://join/abc"]


@posix
def test_unix_stub_through_a_link(tmp_path):
    halo = install(tmp_path / "game")
    bin_folder = tmp_path / "bin"
    bin_folder.mkdir()
    (bin_folder / "halo").symlink_to(halo)
    result = subprocess.run([str(bin_folder / "halo"), "x"], cwd=tmp_path, capture_output=True, text=True)
    assert result.returncode == 7 and result.stdout.splitlines()[1:] == ["arg=x"]


@posix
def test_unix_stub_restores_the_executable_bit(tmp_path):
    halo = install(tmp_path / "game")
    (tmp_path / "game/chupathingy").chmod(0o644)
    result = subprocess.run([str(halo)], cwd=tmp_path, capture_output=True, text=True)
    assert result.returncode == 7, result.stderr
    assert os.access(tmp_path / "game/chupathingy", os.X_OK)


@posix
def test_unix_stub_without_the_game(tmp_path):
    halo = install(tmp_path / "game")
    (tmp_path / "game/chupathingy").unlink()
    result = subprocess.run([str(halo)], cwd=tmp_path, capture_output=True, text=True)
    assert result.returncode != 0 and "chupathingy" in result.stderr
