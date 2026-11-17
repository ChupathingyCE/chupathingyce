"""The debug camera (source/camera/director.c): flown by mouse and keyboard
as in Custom Edition and Sapien (the middle mouse button held, the movement
keys, the mouse, R and F, the wheel for speed), by the controller as before
(the right stick's click turns it on and off), both by the player's own
movement and look as player control made them; debug_camera_save and
debug_camera_load on the first local player, with camera.txt in the save
root, and never from a map's scripts. Following Guven's and hispanorum's
debug camera work in OpenCE (clean-room, from a description).

The file's place is checked by compiling xbox_files.c's name matching with
the build machine's C compiler; the rest reads the source."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
DIRECTOR = ROOT / "source" / "camera" / "director.c"
PLAYER_CONTROL = ROOT / "source" / "game" / "player_control.c"
HS = ROOT / "source" / "hs" / "hs.c"
XINPUT = ROOT / "port" / "linux" / "src" / "xinput_sdl.c"
XBOX_FILES = ROOT / "port" / "linux" / "src" / "xbox_files.c"
FEATURES = ROOT / "port" / "linux" / "include" / "halo_port_features.h"


def read(path):
    return path.read_text(encoding="latin-1")


def function(text, start):
    """a function's definition (not a prototype of it): from its name to its
    closing brace"""
    at = text.index(start)
    while True:
        ends = [index for index in (text.find(";", at), text.find("{", at)) if index != -1]
        if text[min(ends)] == "{":
            break
        at = text.index(start, at + 1)
    body = text[at:]
    return body[:body.index("\n}\n") + 3]


def test_map_scripts_cannot_call_save_or_load():
    """a downloaded map must not write files: both stay out of the maps'
    list (the console may call any function)"""
    text = read(HS)
    allowed = text[text.index("static boolean const hs_function_allowed_in_maps[]="):]
    allowed = allowed[:allowed.index("};")]
    for name in ("debug_camera_save", "debug_camera_load"):
        line = re.search(r"^\s*(TRUE|FALSE), /\* {}\b".format(name), allowed, re.M)
        assert line and line.group(1) == "FALSE", name


def test_save_and_load_use_the_first_local_player():
    text = read(DIRECTOR)
    for name in ("void director_save_camera(", "void director_load_camera("):
        body = function(text, name)
        first = body.index("local_player_get_next(NONE)")
        assert body.index("if (local_player_index == NONE)\n\t\treturn;") > first, name
        # (nothing opened before the player is known, so none is written)
        assert body.index('fopen("d:\\\\camera.txt"') > body.index("if (local_player_index == NONE)"), name
        assert "director_get(0)" not in body and "observer_get_camera(0)" not in body, name
    load = function(text, "void director_load_camera(")
    # (the camera switched once)
    assert load.count("director_set_camera(") == 1


def test_the_mouse_never_writes_the_controllers_toggle():
    body = function(read(DIRECTOR), "static boolean director_update_controls(")
    writes = re.findall(r"director->debug_controls\s*=[^=][^;]*;", body)
    assert writes == ["director->debug_controls = !director->debug_controls;"]
    toggle = body.index("director->debug_controls = !director->debug_controls;")
    assert "_gamepad_binary_button_right_thumb] == 1" in body[toggle - 120:toggle]
    # (Backspace's hold count read once a frame: MAX evaluates its arguments
    # twice, and a second read made the first frame's count 2, never 1)
    assert "key_ticks = director_debug_switch_key_ticks(local_player_index, keys.switch_key);" in body
    assert "MAX(gamepad->buttons[_gamepad_analog_button_black], key_ticks)" in body
    # (the middle button flies only while held, and takes the frame)
    assert "if (keys.middle || director->debug_controls)" in body
    assert body.index("if (keys.middle)") < body.index("_gamepad_analog_button_right_trigger")


def test_the_camera_flies_by_the_players_processed_input():
    body = function(read(DIRECTOR), "static boolean director_update_controls(")
    assert "player_control_get_debug_camera_input(" in body
    # (no raw stick reads left: sensitivity and inversion apply)
    assert "sticks[" not in body
    assert "controls->facing_delta.yaw = facing_delta.yaw;" in body
    assert "controls->facing_delta.pitch = facing_delta.pitch;" in body
    for axis in ("i", "j"):
        assert re.search(r"controls->position_delta\.{0} = throttle\.{0} \*\s*director->debug_input_scale \* "
                         r"director_globals\.dtime \* debug_full_throttle_speed;".format(axis), body), axis
    # (R and F with the controller too, beside its triggers)
    assert "_gamepad_analog_button_right_trigger] != 0 || keys.up" in body
    assert "_gamepad_analog_button_left_trigger] != 0 || keys.down" in body


def test_full_throttle_matches_the_old_full_stick_speed():
    """the old formula: raw stick units (32767 at full deflection) times
    0.00005; the throttle is -1 to 1"""
    text = read(DIRECTOR)
    constant = re.search(r"static real const debug_full_throttle_speed = ([^;]+);", text).group(1)
    assert constant == "32767.f * 0.00005f"


def test_the_copy_is_kept_before_the_directors_inhibit():
    body = function(read(PLAYER_CONTROL), "static void handle_one_player_input(")
    menu = body.index("if (player_control_port_menu_has_controller(local_player_index))")
    copy = body.index("player_control_debug_camera_throttle[local_player_index] = input.throttle;")
    facing = body.index("player_control_debug_camera_facing[local_player_index] = input.facing_delta;")
    inhibit = body.index("if (director_inhibited_input(local_player_index))")
    assert menu < copy < inhibit and menu < facing < inhibit


def test_wheel_counts_only_while_the_middle_button_is_held():
    poll = function(read(XINPUT), "static void debug_camera_poll(")
    assert "!console_is_active() && !chat_typing && !text_typing && !input->menus" in poll
    assert "mouse = keyboard && !input->mouse_released;" in poll
    for name in ("debug_camera_middle", "debug_camera_up", "debug_camera_down"):
        assert re.search(r"{} = mouse && ".format(name), poll), name
    assert "if (debug_camera_middle)\n\t\tdebug_camera_wheel += input->mouse_wheel;\n\telse\n\t\tdebug_camera_wheel = 0.0f;" \
        in poll
    read_once = function(read(XINPUT), "int halo_linux_debug_camera_input(")
    # (each notch counted once: cleared as it is read)
    assert read_once.index("*wheel = debug_camera_wheel;") < read_once.index("debug_camera_wheel = 0.0f;")


def test_wheel_up_is_faster():
    """the wheel's notches go in as they come (up, away from the player,
    positive), and a positive speed change multiplies the speed"""
    body = function(read(DIRECTOR), "static boolean director_update_controls(")
    assert "controls->wheel_delta = keys.wheel;" in body
    process = function(read(DIRECTOR), "static void director_process_variables(")
    assert "pow((double)genius_boy, (double)speed_delta)" in process
    assert re.search(r"genius_boy = 1\.3f;", read(DIRECTOR))


def test_consoles_leave_the_mouse_out():
    text = read(DIRECTOR)
    declaration = text.index("int halo_linux_debug_camera_input(")
    assert text.rfind("#ifndef HALO_CONSOLE_NO_DEBUG_CAMERA_KEYS", 0, declaration) > text.rfind("#endif", 0, declaration)
    body = function(text, "static void director_debug_keys_read(")
    call = body.index("halo_linux_debug_camera_input(")
    assert body.rfind("#ifndef HALO_CONSOLE_NO_DEBUG_CAMERA_KEYS", 0, call) > body.rfind("#endif", 0, call)
    features = read(FEATURES)
    gate = features.index("#define HALO_CONSOLE_NO_DEBUG_CAMERA_KEYS")
    assert features.rfind("#if defined(HALO_CONSOLE) || defined(HALO_XBOX_CONSOLE)", 0, gate) != -1


PRELUDE = """
#include <ctype.h>
#include <stdio.h>
#include <string.h>
"""

MAIN = r"""
int main(void)
{
	const char *cases[][2] = {
		{ "d:\\camera.txt", "camera.txt" },
		{ "D:\\CAMERA.TXT", "camera.txt" },
		{ "d:/camera.txt", "camera.txt" },
		{ "d:\\maps\\camera.txt", "" },
		{ "e:\\camera.txt", "" },
		{ "d:\\debug.txt", "" },
		{ "d:\\camera.txt.bak", "" },
	};
	unsigned long index;

	for (index = 0; index < sizeof(cases) / sizeof(*cases); index++)
	{
		const char *name = save_root_file_name(cases[index][0]);

		printf("%s|%s\n", cases[index][0], name ? name : "");
	}
	printf("%s\n", log_file_name("d:\\debug.txt"));
	return 0;
}
"""


def test_camera_file_is_in_the_save_root(tmp_path):
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    text = read(XBOX_FILES)
    source = PRELUDE
    for name in ("static const char *d_file_name(", "static const char *log_file_name(",
                 "static const char *save_root_file_name("):
        source += function(text, name)
    source += MAIN
    (tmp_path / "names.c").write_text(source)
    subprocess.run([compiler, "-o", str(tmp_path / "names"), str(tmp_path / "names.c")], check=True)
    output = subprocess.run([str(tmp_path / "names")], capture_output=True, text=True, check=True).stdout
    lines = output.splitlines()
    expected = {"d:\\camera.txt": "camera.txt", "D:\\CAMERA.TXT": "camera.txt", "d:/camera.txt": "camera.txt",
                "d:\\maps\\camera.txt": "", "e:\\camera.txt": "", "d:\\debug.txt": "", "d:\\camera.txt.bak": ""}
    for line in lines[:-1]:
        path, name = line.split("|")
        assert expected[path] == name, path
    assert lines[-1] == "debug.txt"
    # (and the save root's is the place it goes, before any drive's)
    translate = function(text, "void platform_translate_path(")
    assert 'snprintf(host_path, host_path_size, "%s/%s", platform_save_root(), save_name);' in translate
    assert translate.index("if (save_name)") < translate.index("platform_data_root()")
