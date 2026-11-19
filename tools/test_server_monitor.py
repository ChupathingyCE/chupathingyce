# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""The server monitor's tool (tools/server_monitor.py) keeps its promises:
no address and no player's words in a report, one run a server at a time,
and no run without an end."""
import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import server_monitor  # noqa: E402

TOOL = Path(__file__).resolve().parent / "server_monitor.py"


@pytest.mark.parametrize("text", [
    "joined 203.0.113.5",
    "joined 203.0.113.5:2302",
    "host 2001:db8::1 answered",
    "host [fe80::1%en0]:2302 answered",
    "mapped ::ffff:203.0.113.5 here",
    "full 2001:0db8:0000:0000:0000:ff00:0042:8329",
    "loopback ::1",
    "[2001:db8::7]:2302",
])
def test_addresses_are_redacted(text):
    redacted = server_monitor.redact(text)
    assert "[address]" in redacted
    for piece in ("203.0.113", "db8", "fe80", "ffff", "8329", "::1"):
        assert piece not in redacted


@pytest.mark.parametrize("text", [
    "11.18.26 21:52:07  net health: client tick 7258",
    "version 0.9.2, capabilities 0x1baec1f",
    "clock 1.000 ratio",
    "map levels\\test\\bloodgulch\\bloodgulch",
])
def test_other_text_is_left(text):
    assert server_monitor.redact(text) == text


def test_one_lock_a_server(tmp_path):
    lock = tmp_path / "server.lock"
    assert server_monitor.take_lock(str(lock))
    assert not server_monitor.take_lock(str(lock))
    # (one left by a process that is gone is taken)
    lock.write_text("999999999")
    assert server_monitor.take_lock(str(lock))
    assert lock.read_text() == str(os.getpid())


def test_a_failed_join_stops_later_runs(tmp_path):
    (tmp_path / "halo-universal.join-failed").write_text("join failed at then")
    game = tmp_path / "game"
    game.write_text("")
    result = subprocess.run([sys.executable, str(TOOL), "run", "--game", str(game), "--data-root-script", "true",
                             "--server", "HALO UNIVERSAL", "--work", str(tmp_path), "--out", str(tmp_path / "out")],
                            capture_output=True, text=True)
    assert result.returncode != 0
    assert "not joining again" in result.stderr


def test_a_run_needs_an_end(tmp_path):
    result = subprocess.run([sys.executable, str(TOOL), "run", "--game", "x", "--data-root-script", "true",
                             "--server", "x", "--minutes", "0", "--work", str(tmp_path), "--out", str(tmp_path)],
                            capture_output=True, text=True)
    assert result.returncode != 0 and "needs an end" in result.stderr
    result = subprocess.run([sys.executable, str(TOOL), "run", "--game", "x", "--data-root-script", "true",
                             "--server", "x", "--free-slots", "0", "--work", str(tmp_path), "--out", str(tmp_path)],
                            capture_output=True, text=True)
    assert result.returncode != 0 and "--free-slots" in result.stderr


def test_reports_carry_no_line_text(tmp_path):
    """A run whose output (and log) has a server line naming a player: the
    report has its channel, length and hash, never its words."""
    run = tmp_path / "run"
    run.mkdir()
    (run / "run.json").write_text(json.dumps({"server": "HALO UNIVERSAL", "player": "Chupa",
                                              "started": "2026-11-18T22:15:39-05:00", "exit_code": 0}))
    events = [
        {"t": 15.5, "event": "server_line", "channel": "notice", "hash": "1a2b3c4d", "length": 47, "phase": "game"},
        {"t": 60.0, "event": "server_line_total", "channel": "notice", "hash": "1a2b3c4d", "count": 1},
        {"t": 60.0, "event": "leave", "reason": "time up", "games_ended": 1},
    ]
    (run / "monitor.jsonl").write_text("\n".join(json.dumps(event) for event in events) + "\n")
    (run / "debug.txt").write_text("the host: SomePlayer was kicked from 203.0.113.5\n")
    report = server_monitor.make_report(str(run))
    text = json.dumps(report) + server_monitor.markdown(report)
    assert "SomePlayer" not in text and "203.0.113" not in text
    assert "1a2b3c4d" in text
    assert report["log_warnings"] == [{"kind": "kicked", "count": 1}]
