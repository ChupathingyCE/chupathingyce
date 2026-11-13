"""Runs the C tests of the Delta Guide Workshop's Steam bridge
(port/linux/src/steam_client_bridge.c, steam_bridge_api.c and steamcmd's text
in steam_bridge_cmd.c) with a fake steam_api library and the build machine's
C compiler. No Steam, steamcmd or game is needed.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
"""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port" / "linux" / "src"


def test_steam_bridge(tmp_path):
    """Builds the bridge with its tests, warnings as errors, and runs them in an empty folder."""
    cc = next((path for path in map(shutil.which, ("clang", "cc", "gcc")) if path), None)
    if not cc or os.name == "nt":
        pytest.skip("no POSIX C compiler")
    binary = tmp_path / "steam_bridge_test"
    build = subprocess.run(
        [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-Wno-missing-field-initializers", "-pthread", "-DSTEAM_BRIDGE_TESTS",
         str(ROOT / "port" / "linux" / "tests" / "steam_bridge_test.c"),
         str(SRC / "steam_client_bridge.c"), str(SRC / "steam_bridge_api.c"), str(SRC / "steam_bridge_cmd.c"),
         str(SRC / "steam_bridge_pipe.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True, cwd=tmp_path, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    # (the app id goes to the environment only: no steam_appid.txt anywhere)
    assert not (tmp_path / "steam_appid.txt").exists()
