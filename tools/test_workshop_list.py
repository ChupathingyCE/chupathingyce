"""Runs the C tests of the Delta Guide's Workshop list document
(port/linux/src/workshop_list.c), with the build machine's C compiler. The
unit has no SDL, game or file system dependency, so it compiles anywhere.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
"""
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



def test_workshop_list_threads(tmp_path):
    """the readers on several threads at once, each answer its own (under
    ThreadSanitizer where the compiler has it)"""
    if os.name == "nt":
        pytest.skip("pthreads test")
    cc = next((path for path in map(shutil.which, ("clang", "cc", "gcc")) if path), None)
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "workshop_list_threads_test"
    sources = [str(ROOT / "port" / "linux" / "tests" / "workshop_list_threads_test.c"),
               str(ROOT / "port" / "linux" / "src" / "workshop_list.c")]
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-pthread"]
    build = subprocess.run([cc, *flags, "-fsanitize=thread", *sources, "-o", str(binary)],
                           capture_output=True, text=True)
    sanitized = build.returncode == 0
    if sanitized:
        probe = subprocess.run([str(binary)], capture_output=True, text=True,
                               env=dict(os.environ, TSAN_OPTIONS="halt_on_error=1"))
        # (a TSan runtime that can't start here, as on some kernels' address layouts: built without it)
        sanitized = "ThreadSanitizer: unexpected memory mapping" not in probe.stderr and \
            "FATAL: ThreadSanitizer" not in probe.stderr
    if not sanitized:
        build = subprocess.run([cc, *flags, *sources, "-o", str(binary)], capture_output=True, text=True)
        assert build.returncode == 0, build.stdout + build.stderr
        probe = subprocess.run([str(binary)], capture_output=True, text=True)
    assert probe.returncode == 0, probe.stdout + probe.stderr[-4000:]

def test_workshop_list_fuzz(tmp_path):
    """the readers of Steam's answers under libFuzzer, from seeds of each
    kind (and fuzzing from them, with HALO_FUZZ_SECONDS)"""
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
    (seeds / "authors").write_bytes(
        b"\x04" + b'\\"total_pages\\":23,\\"results\\":[{\\"publishedfileid\\":\\"5\\",\\"creator\\":\\"6\\"}],'
        b'\\"creator_player_link_details\\":[{\\"steamid\\":\\"6\\",\\"persona_name\\":\\"a\\\\\\"b\\u00e9\\"}]')
    (seeds / "profile").write_bytes(b"\x05" + b"<steamID><![CDATA[Name]]></steamID>")
    result = fuzz_support.run_fuzzer(binary, tmp_path, fuzz_support.fuzz_seconds(), seeds=[seeds],
                                     max_len=16384)
    assert result.returncode == 0, result.stdout[-4000:] + result.stderr[-4000:]
