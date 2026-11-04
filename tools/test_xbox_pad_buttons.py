"""The original Xbox pads' black and white buttons on Linux (issue #619):
port/linux/include/halo_xbox_pad_buttons.h, read in xinput_sdl.c."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "port" / "linux" / "include" / "halo_xbox_pad_buttons.h"
SOURCE = ROOT / "port" / "linux" / "src" / "xinput_sdl.c"


def test_header_checks_the_pads(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "pads.c"
    source.write_text(
        '#include <stdio.h>\n#include "halo_xbox_pad_buttons.h"\n'
        "int main(void)\n{\n"
        '\tprintf("%d %d %d %d\\n", xbox_pad_has_black_white_buttons(0x045e, 0x0285),\n'
        "\t\txbox_pad_has_black_white_buttons(0x045e, 0x0289),\n"
        "\t\txbox_pad_has_black_white_buttons(0x045e, 0x028e),\n"
        "\t\txbox_pad_has_black_white_buttons(0x046d, 0x0285));\n"
        "\treturn 0;\n}\n")
    binary = tmp_path / "pads"
    shutil.copy(HEADER, tmp_path / HEADER.name)
    subprocess.run([cc, "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-I", str(tmp_path),
                    str(source), "-o", str(binary)], check=True)
    out = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
    # (the Controller S yes; an Xbox 360 pad and another maker's pad no)
    assert out.split() == ["1", "1", "0", "0"]


def test_buttons_are_the_evdev_order():
    text = HEADER.read_text()
    # A 0, B 1, BTN_C 2, X 3, Y 4, BTN_Z 5
    assert "XBOX_PAD_BLACK_JOYSTICK_BUTTON 2" in text
    assert "XBOX_PAD_WHITE_JOYSTICK_BUTTON 5" in text
    source = SOURCE.read_text()
    assert "XINPUT_GAMEPAD_BLACK, SDL_GetJoystickButton(joystick, XBOX_PAD_BLACK_JOYSTICK_BUTTON)" in source
    assert "XINPUT_GAMEPAD_WHITE, SDL_GetJoystickButton(joystick, XBOX_PAD_WHITE_JOYSTICK_BUTTON)" in source
