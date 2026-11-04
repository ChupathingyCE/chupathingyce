"""Tests of the host's refusal counts for the server's moderators: the C
tests of port/linux/game/host_evidence.c (port/linux/tests/
host_evidence_test.c), built with the build machine's C compiler, and what
the sources must keep that the C tests cannot reach: each check that refuses
a client's word counts it, the counts start over with each game and each new
machine, sv_evidence is a moderator's command and the control API's
/v1/evidence, and nothing of it is a message of the game's wire."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "port" / "linux" / "game"
DISTRIBUTED_C = GAME / "network_distributed.c"
OBJECTS_C = GAME / "network_objects.c"
DAMAGE_C = GAME / "network_damage.c"
EVIDENCE_C = GAME / "host_evidence.c"
COMMANDS_C = ROOT / "server" / "src" / "server_commands.c"
ROLES_C = ROOT / "server" / "platform" / "control_roles.c"
CONTROL_C = ROOT / "server" / "platform" / "server_control.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def function(source, name):
    """a function's text, from its name to the next function's"""
    start = source.index("\n" + name + "(\n")
    end = re.search(r"\n\}\n", source[start:])
    return source[start:start + end.end()]


def test_counts(tmp_path):
    """the counts' C tests, warnings as errors"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "host_evidence_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/host_evidence_test.c"), str(EVIDENCE_C), "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "host_evidence_test: ok" in result.stdout


def test_module_needs_no_game():
    """the counts include the C library alone (no game, socket or address)"""
    source = EVIDENCE_C.read_text(encoding="utf-8")
    header = (GAME / "host_evidence.h").read_text(encoding="utf-8")
    includes = re.findall(r'#include [<"]([^>"]+)[>"]', source + header)
    assert sorted(set(includes)) == ["host_evidence.h", "stddef.h", "stdio.h", "string.h"]
    assert "address" not in source


def test_refusals_counted():
    """each refusal of a client's prediction, vehicle prediction, hit report
    and fast clock counts it for the sender's machine"""
    distributed = DISTRIBUTED_C.read_text(encoding="utf-8")
    take = function(distributed, "static void distributed_take_prediction")
    # (the tolerance, the anchor's reach and the ceiling: every return that refuses)
    assert take.count("host_evidence_note(distributed_player_machine(player_index), _host_evidence_movement);") == 3
    clock = function(distributed, "static void distributed_note_client_clock")
    assert "host_evidence_note(machine_index, _host_evidence_clock);" in clock
    vehicles = function(OBJECTS_C.read_text(encoding="utf-8"), "void network_objects_apply_vehicle_predictions")
    assert vehicles.count("_host_evidence_vehicle);") == 4
    reports = function(DAMAGE_C.read_text(encoding="utf-8"), "void network_damage_handle_reports")
    assert "host_evidence_note(machine_index, _host_evidence_hit_report);" in reports


def test_counts_start_over():
    """a new game, and a new machine at an index, start from zero"""
    distributed = DISTRIBUTED_C.read_text(encoding="utf-8")
    assert "host_evidence_reset();" in function(distributed, "void network_distributed_new_game")
    assert "host_evidence_reset_machine(machine_index);" in function(distributed, "static boolean distributed_machine_loaded")


def test_command_and_api():
    """sv_evidence is in the commands' table, a moderator's (WARN) read, and
    the control API's /v1/evidence with the same permission"""
    commands = COMMANDS_C.read_text(encoding="utf-8")
    assert '{ "sv_evidence", "sv_evidence [player]",' in commands
    assert '{ "sv_evidence", CONTROL_PERMISSION_WARN, 0, 0 },' in ROLES_C.read_text(encoding="utf-8")
    assert '{ "/v1/evidence", "sv_evidence", CONTROL_PERMISSION_WARN },' in CONTROL_C.read_text(encoding="utf-8")
    evidence = function(commands, "static boolean command_evidence")
    # (no address, and nothing written: no audit, no file, no site)
    for word in ("address", "fopen", "server_audit", "server_link"):
        assert word not in evidence, word


def test_no_wire_change():
    """the counts are the host's own: no message type, nothing sent"""
    for path in (EVIDENCE_C, GAME / "host_evidence.h"):
        source = path.read_text(encoding="utf-8")
        assert "_distributed_message_" not in source
        assert "distributed_send" not in source
