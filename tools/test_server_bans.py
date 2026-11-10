"""Runs the C tests of the dedicated server's shared ban lists and players'
reports queue (server/tests/bans_test.c: server/platform/server_bans.c and
report_queue.c, with Monocypher and control_protocol.c's JSON) with the
build machine's C compiler, under the sanitizers where it has them, in a
data folder of its own."""
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools.test_server_control import SANITIZERS, compiler  # noqa: E402

MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"
PLATFORM = ROOT / "server" / "platform"
SOURCES = [
    ROOT / "server" / "tests" / "bans_test.c",
    PLATFORM / "server_bans.c",
    PLATFORM / "report_queue.c",
    PLATFORM / "control_protocol.c",
    MONOCYPHER / "monocypher.c",
]


def build(cc, binary, extra):
    return subprocess.run(
        [cc, "-std=c11", "-D_DEFAULT_SOURCE", "-D_DARWIN_C_SOURCE", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *extra,
         f"-I{MONOCYPHER}", *(str(source) for source in SOURCES), "-o", str(binary), "-lpthread"],
        capture_output=True, text=True,
    )


def test_server_bans(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "bans_test"
    result = build(cc, binary, SANITIZERS)
    if result.returncode != 0:
        result = build(cc, binary, [])
    assert result.returncode == 0, result.stdout + result.stderr
    data = tmp_path / "data"
    data.mkdir()
    run = subprocess.run([str(binary), str(data)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    # (the queue's file: no address in it)
    log = (data / "reports.log").read_text(encoding="utf-8")
    assert "\"event\": \"report\"" in log and "\"event\": \"closed\"" in log


def test_poll_sends_without_extras_before_failing():
    """a poll whose signed body would pass what the site reads
    (CONTROL_LINK_BODY_MOST) goes without the shared bans, then without the
    reports too, so the commands' results and the status never wait on them"""
    source = (ROOT / "server" / "platform" / "control_link.c").read_text(encoding="utf-8")
    start = source.index("static int run_poll(void)")
    body = source[start:source.index("\n}\n", start)]
    assert "for (extras_sent = 2; extras_sent >= 0; extras_sent--)" in body
    assert body.index("extras_sent >= 1 && ") < body.index("server_reports_site_payload(")
    assert body.index("extras_sent >= 2 && ") < body.index("server_bans_share_payload(")
    assert "if (status != REQUEST_TOO_BIG)\n\t\t\tbreak;" in body
    assert "return REQUEST_TOO_BIG;" in source
    protocol = (ROOT / "server" / "platform" / "control_link_protocol.h").read_text(encoding="utf-8")
    assert "CONTROL_LINK_BODY_MOST = 384 * 1024," in protocol
