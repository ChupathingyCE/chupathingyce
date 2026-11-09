"""Tests of a host's budget for its client machines' lobby messages: the C
tests (port/linux/tests/lobby_budget_test.c, of port/linux/game/
network_rules.c's budget), built with the build machine's C compiler, and
what the sources must hold that the C tests cannot reach: the messages the
host's handler keeps to the budget, the host's own machine left out, and the
budget made full when a machine joins and when it comes back to the lobby."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
HANDLER_C = ROOT / "source/networking/network_server_message_handler.c"
SERVER_MANAGER_C = ROOT / "source/networking/network_server_manager.c"
DISTRIBUTED_C = ROOT / "port/linux/game/network_distributed.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_budget(tmp_path):
    """the budget's C tests, warnings as errors"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "lobby_budget_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/lobby_budget_test.c"), str(ROOT / "port/linux/game/network_rules.c"),
         "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "lobby_budget_test: ok" in result.stdout


def test_handler_keeps_lobby_messages_to_the_budget():
    source = HANDLER_C.read_text(encoding="utf-8")
    start = source.index("boolean network_game_server_handle_client_message(")
    piece = source[start:source.index("switch (packet_type)", start)]
    for message in ("_message_client_settings_request", "_message_client_player_settings_request",
                    "_message_client_add_player_request_pregame", "_message_client_remove_player_request_pregame",
                    "_message_client_add_player_request_ingame", "_message_client_remove_player_request_ingame"):
        assert f"packet_type == {message}" in piece, message
    # (never the join request, the start or the loads: a game goes on)
    condition = piece[piece.index("if ((packet_type =="):piece.index("network_distributed_lobby_message_allowed")]
    for message in ("_message_client_join_game_request", "_message_client_game_start_request",
                    "_message_client_loaded", "_message_client_switch_to_pregame"):
        assert message not in condition, message
    assert "!network_game_server_client_machine_is_local(server, machine)" in piece
    assert "if (!network_distributed_lobby_message_allowed(machine_index))\n\t\t\t\t\t\t\tbreak;" in piece


def test_budget_full_on_join_and_lobby():
    source = SERVER_MANAGER_C.read_text(encoding="utf-8")
    accept = source[source.index("boolean network_game_server_accept_client_machine_into_game("):]
    accept = accept[:accept.index("\n}\n")]
    assert "network_distributed_lobby_budget_reset(machine_index);" in accept
    switch = source[source.index("boolean network_game_server_switch_machine_from_postgame_to_pregame("):]
    switch = switch[:switch.index("\n}\n")]
    assert "network_distributed_lobby_budget_reset(machine->machine_index);" in switch


def test_refusal_logged_once_without_address():
    source = DISTRIBUTED_C.read_text(encoding="utf-8")
    start = source.index("static void distributed_lobby_message_refused(")
    piece = source[start:source.index("\n}\n", start)]
    assert "logged = TRUE;" in piece
    assert "address" not in piece.split("{", 1)[1]
    assert "transport_address_to_string" not in piece and "log_address" not in piece
