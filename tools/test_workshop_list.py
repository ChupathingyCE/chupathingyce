"""Runs the C tests of the Delta Guide's Workshop list document
(port/linux/src/workshop_list.c), with the build machine's C compiler. The
unit has no SDL, game or file system dependency, so it compiles anywhere."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools import fuzz_support  # noqa: E402


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


def test_workshop_list_fuzz(tmp_path):
    """the readers of Steam's answers under libFuzzer, from seeds of each
    kind, for a short while"""
    cc = fuzz_support.libfuzzer_compiler()
    if not cc:
        fuzz_support.skip("no clang with libFuzzer")
    binary = tmp_path / "workshop_list_fuzz"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-fsanitize=fuzzer,address,undefined",
         str(ROOT / "port" / "linux" / "tests" / "workshop_list_fuzz.c"),
         str(ROOT / "port" / "linux" / "src" / "workshop_list.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    seeds = tmp_path / "seeds"
    seeds.mkdir()
    (seeds / "list").write_bytes(b"\x00" + b'{"version":1,"items":[{"id":"123","title":"Map","kind":"maps"}]}')
    (seeds / "details").write_bytes(
        b"\x01" + b'{"response":{"result":1,"resultcount":1,"publishedfiledetails":[{"publishedfileid":"123",'
        b'"result":1,"consumer_app_id":976730,"title":"Map","file_size":"1048576","description":"[b]x[/b]",'
        b'"tags":[{"tag":"Halo1"},{"tag":"Multiplayer"}]}]}}')
    (seeds / "browse").write_bytes(b"\x02" + b'<a href="https://steamcommunity.com/sharedfiles/filedetails/?id=123">')
    (seeds / "id").write_bytes(b"\x03" + b"2886943774")
    result = fuzz_support.run_fuzzer(binary, tmp_path, int(os.environ.get("HALO_FUZZ_SECONDS", "20")), seeds=[seeds],
                                     max_len=16384)
    assert result.returncode == 0, result.stdout[-4000:] + result.stderr[-4000:]
