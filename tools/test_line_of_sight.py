"""Tests of the host's line of sight on clients' reported hits
(network.hit_line_of_sight): the C tests of its geometry (port/linux/tests/
line_of_sight_test.c: a wall between shooter and target refused, a clear
line taken, the target's edge, glass, a vehicle gunner's gun, a grenade
around a corner, an explosion's core, a projectile that homes in or arcs,
the budget), built with the build machine's C compiler, and what the C tests
cannot reach: the setting, its documentation, and that the host asks it for
shots and explosions, not melee blows or collisions."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
DAMAGE_C = ROOT / "port/linux/game/network_damage.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_geometry(tmp_path):
    """the geometry's C tests, warnings as errors"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "line_of_sight_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/line_of_sight_test.c"), str(ROOT / "port/linux/game/line_of_sight.c"),
         "-o", str(binary), "-lm"],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "line_of_sight_test: ok" in result.stdout


def test_setting_logs_by_default_and_documented():
    """off by default (#2022): the host logs a hit with no line of sight and
    deals it; on, it refuses it"""
    config = (ROOT / "port/linux/src/port_config.c").read_text(encoding="utf-8")
    assert re.search(r'"network\.hit_line_of_sight", _config_boolean, "false", "HALO_NET_HIT_LINE_OF_SIGHT"', config)
    assert "`network.hit_line_of_sight` | `false` | `HALO_NET_HIT_LINE_OF_SIGHT` |" in \
        (ROOT / "port/linux/README.md").read_text(encoding="utf-8")
    assert "`HALO_NET_HIT_LINE_OF_SIGHT` | `false` |" in \
        (ROOT / "server/docs/settings.md").read_text(encoding="utf-8")
    assert "network.hit_line_of_sight" in (ROOT / "port/linux/NETCODE.md").read_text(encoding="utf-8")


def test_default_logs_and_enforced_refuses():
    """the host's decision goes through line_of_sight_verdict (the C tests
    check it: not enforced, a blocked hit is logged and dealt; enforced, it
    is refused), off unless the setting says so, with one log line each:
    slots, the kind and the distance, nothing else of the player"""
    source = DAMAGE_C.read_text(encoding="utf-8")
    assert "static boolean damage_line_of_sight_enforced = FALSE;" in source
    assert 'damage_line_of_sight_enforced = config_boolean("network.hit_line_of_sight") != 0;' in source
    assert "line_of_sight_verdict(clear, damage_line_of_sight_enforced)" in source
    refuse = re.search(r"if \(verdict == _line_of_sight_refuse\)\s*\{\s*"
                       r"damage_report_refused_line_of_sight = TRUE;\s*return FALSE;", source)
    assert refuse
    # (refused only there: a logged hit goes on to be dealt)
    assert source.count("damage_report_refused_line_of_sight = TRUE;") == 1
    log = re.search(r'"damage: no line of sight: shooter %d target %ld %s %\.1f %s"', source)
    assert log and log.start() < refuse.start()


def test_host_checks_shots_and_explosions_only():
    source = DAMAGE_C.read_text(encoding="utf-8")
    assert 'config_boolean("network.hit_line_of_sight")' in source
    # (after the target's own checks; never for a melee blow or a collision)
    check = re.search(r"if \(!melee && !collision\)\s*\{\s*real sight_distance;\s*"
                      r"int clear = distributed_report_in_sight\(player_index, report, target, definition, "
                      r"&fired_reach, area,", source)
    assert check
    assert source.index("distributed_target_seen(report->object_index") < check.start()
    # (counted with the other refusals, and logged)
    assert "damage_line_of_sight_reports++" in source
    assert "with no line of sight" in source


def test_log_lines_are_limited_and_summarized():
    """the per-hit lines go through line_of_sight_log_allowed (the C tests
    check the limit: a burst logs at most its lines and one summary), and
    the 30-second summary prints for logged-only hits too, with them"""
    source = DAMAGE_C.read_text(encoding="utf-8")
    assert re.search(r"if \(verdict != _line_of_sight_take &&\s*"
                     r"line_of_sight_log_allowed\(&damage_line_of_sight_log_limit, now, REPORT_LOG_TICKS\)\)", source)
    assert source.count('"damage: no line of sight: shooter') == 1
    summary = source[source.index("static void distributed_log_reports("):]
    summary = summary[:summary.index("\n}\n")]
    assert "line_of_sight_summary_due(game_time_get(), REPORT_LOG_TICKS" in summary
    assert "damage_line_of_sight_logged - logged_line_of_sight_only" in summary
    assert "logged only" in summary and "held back" in summary
