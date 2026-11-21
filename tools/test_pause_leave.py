"""Tests of a network game's LEAVE GAME with a gamepad: the C tests of whose
player leaves (port/linux/game/pause_leave.h,
port/linux/tests/pause_leave_test.c), built with the build machine's C
compiler, and what the sources must keep that the C tests cannot reach: the
pause screen's LEAVE GAME asks pause_leave_controller, with whether the
press's controller has a player in the game."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
TEST_C = ROOT / "port" / "linux" / "tests" / "pause_leave_test.c"
HANDLERS_C = ROOT / "source" / "interface" / "ui_widget_event_handler_functions.c"
GLOBALS_C = ROOT / "source" / "networking" / "network_game_globals.c"
GLOBALS_H = ROOT / "source" / "networking" / "network_game_globals.h"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def function(source, signature):
    """a function's text, from its signature to its closing brace (the
    definition: the last of its declarations)"""
    start = source.rindex("\n" + signature)
    end = re.search(r"\n\}\n", source[start:])
    return source[start:start + end.end()]


@pytest.mark.parametrize("standard", ["c11", "gnu89"])
def test_rules(tmp_path, standard):
    """Builds the rule with its tests, warnings as errors, as C11 and as the
    game's gnu89, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "pause_leave_test"
    build = subprocess.run(
        [cc, f"-std={standard}", "-Wall", "-Wextra", "-Werror", "-O1", str(TEST_C), "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "all passed" in result.stdout


def test_leave_game_asks_the_rule():
    """LEAVE GAME (network_game_remove_local_player) takes out the player of
    pause_leave_controller, told whether the press's controller has one."""
    source = HANDLERS_C.read_text(encoding="utf-8")
    assert '#include "pause_leave.h"' in source
    leave = function(source, "static boolean network_game_remove_local_player(")
    assert re.search(
        r"controller_index = pause_leave_controller\(\s*event \? event->controller_index : NONE,\s*"
        r"event && network_game_client_local_player_exists\(event->controller_index\),\s*"
        r"widget->local_player_index\);",
        leave,
    )
    assert leave.index("pause_leave_controller(") < leave.index("network_game_client_local_player_quit(controller_index)")


def test_exists_and_quit_search_alike():
    """Whether a controller has a player and the player who quits come from
    the same search."""
    source = GLOBALS_C.read_text(encoding="utf-8")
    assert "boolean network_game_client_local_player_exists(" in GLOBALS_H.read_text(encoding="utf-8")
    exists = function(source, "boolean network_game_client_local_player_exists(")
    quit_ = function(source, "void network_game_client_local_player_quit(")
    assert "network_game_client_local_player(controller_index)" in exists
    assert "network_game_client_local_player(controller_index)" in quit_
