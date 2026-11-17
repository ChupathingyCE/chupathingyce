"""A verified player's role on the dedicated server (server/tests/roles_test.c:
server/platform/server_roles.c): a Delta account's line in moderators.txt,
and the site's role list by account, never by a handle alone. Built with
the build machine's C compiler, warnings as errors, run in a folder of its
own."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
PLATFORM = ROOT / "server" / "platform"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_roles(tmp_path):
    cc = compiler()
    if not cc or sys.platform == "win32":
        pytest.skip("no C compiler")
    binary = tmp_path / "roles_test"
    build = subprocess.run(
        [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-O1", "-pthread",
         str(ROOT / "server" / "tests" / "roles_test.c"), str(PLATFORM / "server_roles.c"),
         str(PLATFORM / "control_roles.c"), str(PLATFORM / "control_protocol.c"), str(ROOT / "server" / "src" / "command_line.c"),
         "-I", str(ROOT / "port" / "third_party" / "monocypher"), str(ROOT / "port" / "third_party" / "monocypher" / "monocypher.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    data = tmp_path / "data"
    data.mkdir()
    run = subprocess.run([str(binary), str(data)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


def test_one_identity_layout():
    """The verified player the moderation hooks pass is delta_peer.h's: a
    copy declared elsewhere drifted once (its tag and bound fields), and
    the dedicated server then read every capability as 0."""
    import re
    pattern = re.compile(r"struct\s+delta_peer_identity\s*\{")
    found = []
    for folder in ("server", "port", "source"):
        for path in (ROOT / folder).rglob("*.[ch]"):
            if pattern.search(path.read_text(encoding="utf-8", errors="replace")):
                found.append(path.relative_to(ROOT).as_posix())
    assert found == ["port/linux/src/delta_peer.h"]
    assert '#include "../../port/linux/src/delta_moderation.h"' in \
        (ROOT / "server" / "src" / "server_moderation.c").read_text(encoding="utf-8")
