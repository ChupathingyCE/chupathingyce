"""A controller's taps (gamepad_taps.c): a button or trigger pressed and let go of between two readings is seen once
(sdl_platform.c's gamepad presses, read by xinput_sdl.c's sdl_gamepad_state), with the port's own code for both.
Based on the OpenH1 team's test (g0atbutt, OpenH1 0473c477)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure  # noqa: E402

CASES = ["tap", "trigger", "ports", "stale", "dropped"]

PLATFORM_FUNCTIONS = ["gamepad_presses_find", "gamepad_button_pressed", "gamepad_trigger_moved",
                      "gamepad_presses_forget", "gamepad_presses_clear", "gamepad_presses_take"]
INPUT_FUNCTIONS = ["gamepad_button_down", "gamepad_trigger"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the taps never taken: no different from reading the state alone
    "taps-ignored": ("gamepad_button_down", ("|| (taps && (taps->buttons & (1u << button)) != 0)", ""), "tap"),
    # the taps kept after a reading: a tap held for ever
    "taps-kept": ("gamepad_presses_take", ("\t\t*buttons = presses->buttons;\n\t\t*triggers = presses->triggers;\n\t\tpresses->buttons = 0;\n", "\t\t*buttons = presses->buttons;\n\t\t*triggers = presses->triggers;\n"), "tap"),
    # a held trigger reported fully pulled
    "trigger-over-held": ("gamepad_trigger", (" && value < PLATFORM_GAMEPAD_TRIGGER_PRESSED", ""), "trigger"),
    # every move past the threshold a new pull
    "trigger-every-move": ("gamepad_trigger_moved", ("else if (!(presses->triggers_down & bit))", "else"), "trigger"),
    # one place for every gamepad's taps
    "shared-taps": ("gamepad_presses_find", ("if (gamepad_presses[index].id == id)", "if (gamepad_presses[index].id)"),
                    "ports"),
    # a tap kept however long nobody reads it
    "stale-kept": ("gamepad_presses_take", ("if (presses && gamepad_pumps - presses->pump > GAMEPAD_TAP_PUMPS)", "if (0)"),
                   "stale"),
    # a removed gamepad's taps kept
    "removal-kept": ("gamepad_presses_forget", ("memset(presses, 0, sizeof(*presses))", "memset(presses, 0, 0)"), "dropped"),
}


def generated(fault=None):
    platform = read("port/linux/src/sdl_platform.c")
    platform_header = read("port/linux/src/sdl_platform.h")
    inputs = read("port/linux/src/xinput_sdl.c")
    config = re.search(r"^#define PLATFORM_GAMEPAD_TRIGGER_PRESSED [^\n]+", platform_header, re.M).group(0)
    config += "\n" + re.search(r"^#define GAMEPAD_PRESSES_COUNT [^\n]+", platform, re.M).group(0) + "\n"
    config += re.search(r"^#define GAMEPAD_TAP_PUMPS [^\n]+", platform, re.M).group(0) + "\n"
    config += "static unsigned long gamepad_pumps;\n"
    # (SDL3's gamepad buttons and axes, in its order)
    config += "typedef enum {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, " \
              "SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START, " \
              "SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, " \
              "SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, " \
              "SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_MISC1, " \
              "SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, " \
              "SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, SDL_GAMEPAD_BUTTON_TOUCHPAD, SDL_GAMEPAD_BUTTON_MISC2, " \
              "SDL_GAMEPAD_BUTTON_MISC3, SDL_GAMEPAD_BUTTON_MISC4, SDL_GAMEPAD_BUTTON_MISC5, " \
              "SDL_GAMEPAD_BUTTON_MISC6, SDL_GAMEPAD_BUTTON_COUNT} SDL_GamepadButton;\n"
    config += "typedef enum {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, " \
              "SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, " \
              "SDL_GAMEPAD_AXIS_COUNT} SDL_GamepadAxis;\n"
    presses = re.search(r"^static struct gamepad_presses\n\{.*?^\} gamepad_presses\[GAMEPAD_PRESSES_COUNT\];",
                        platform, re.M | re.S)
    assert presses, "sdl_platform.c's gamepad_presses"
    code = presses.group(0) + "\n" + structure(inputs, "gamepad_taps") + "\n"
    for source, names in ((platform, PLATFORM_FUNCTIONS), (inputs, INPUT_FUNCTIONS)):
        for name in names:
            text = function(source, name)
            if fault and NEGATIVE_CONTROLS[fault][0] == name:
                text = mutated(text, *NEGATIVE_CONTROLS[fault][1])
            code += text + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("gamepad_taps", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("gamepad_taps", generated(control)), NEGATIVE_CONTROLS[control][2])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"


def test_the_game_reads_them():
    """The event pump records the presses and drops them on focus loss and removal; a gamepad's reading takes its
    taps once."""
    platform = read("port/linux/src/sdl_platform.c")
    inputs = read("port/linux/src/xinput_sdl.c")
    pump = function(platform, "platform_pump_events")
    assert "gamepad_pumps++;" in pump
    assert "gamepad_button_pressed(event.gbutton.which, event.gbutton.button)" in pump
    assert "gamepad_trigger_moved(event.gaxis.which, event.gaxis.axis, event.gaxis.value)" in pump
    assert "gamepad_presses_forget(event.gdevice.which)" in pump
    assert "gamepad_presses_clear()" in pump
    state = function(inputs, "sdl_gamepad_state")
    assert state.count("gamepad_taps_take(gamepad, &tapped)") == 1
    assert "SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)" not in state
