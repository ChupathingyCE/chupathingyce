"""A cinematic's skip (cinematic_skip_press.c): a press skips once, and A held on past the revert it makes skips
nothing more until it is let go (source/game/player_control.c)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["press", "held", "flicker", "again", "other"]

FUNCTIONS = ["player_control_skip_button_down", "player_control_skip_press_new"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the original game's: every frame with the accept skips
    "every-frame": ("player_control_skip_press_new", ("return accept && player_control_skip_up_reverts == reverts;",
                                                       "return accept;"), "held"),
    # the player's accept taken as the button: a frame without it is a let go
    "accept-only": ("player_control_skip_press_new", ("if (!accept && !player_control_skip_button_down())",
                                                       "if (!accept)"), "flicker"),
    # never let go: one skip for ever
    "never-up": ("player_control_skip_press_new", ("\t\tplayer_control_skip_up_reverts = reverts;\n", ""), "again"),
    # only the first controller's button
    "first-pad": ("player_control_skip_button_down", ("gamepad_index < MAXIMUM_GAMEPADS;", "gamepad_index < 1;"),
                  "other"),
}


def generated(fault=None):
    source = read("source/game/player_control.c")
    code = "static unsigned long player_control_skip_up_reverts;\n"
    for name in FUNCTIONS:
        text = function(source, name)
        if fault and NEGATIVE_CONTROLS[fault][0] == name:
            text = mutated(text, *NEGATIVE_CONTROLS[fault][1])
        code += text + "\n"
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("cinematic_skip_press", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("cinematic_skip_press", generated(control)), NEGATIVE_CONTROLS[control][2])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"


def test_the_game_asks_it():
    """The skip check asks it before skipping, and every revert is counted."""
    source = read("source/game/player_control.c")
    check = function(source, "player_control_action_test_check_reset_input_blob")
    assert "player_control_skip_press_new(input->accept != 0)" in check
    revert = function(read("source/saved games/game_state.c"), "game_state_revert")
    assert "game_state_revert_count++;" in revert
