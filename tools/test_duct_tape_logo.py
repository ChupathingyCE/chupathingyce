"""Tests of the main menu's duct tape logo: the C tests of its rules
(port/linux/game/duct_tape_logo.c, port/linux/tests/duct_tape_logo_test.c:
the Konami code, wrong and partial ones, the B and A it keeps from the menu,
the swap's timing), built with the build machine's C compiler, and what the
sources must keep that the C tests cannot reach: the setting (off unless
set, written when the code is entered), the menu drawing the tape over the
player's own Halo logo in the Chupathingy menus only, and the consoles and
the Xbox menus untouched."""
import json
import re
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "port" / "linux" / "game"
RULES_C = GAME / "duct_tape_logo.c"
TEST_C = ROOT / "port" / "linux" / "tests" / "duct_tape_logo_test.c"
MENUS_C = GAME / "menu_functions.c"
MENU_TAGS_C = GAME / "menu_tags.c"
UI_WIDGET_C = ROOT / "source" / "interface" / "ui_widget.c"
PORT_CONFIG_C = ROOT / "port" / "linux" / "src" / "port_config.c"
SETTINGS_MD = ROOT / "docs" / "settings.md"
MENUS = ROOT / "port" / "assets" / "menus"
MAIN_MENU_XML = MENUS / "chupathingy" / "main_menu.xml"
TAPE_PNG = MENUS / "chupathingy" / "main_menu" / "duct_tape__0.png"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def function(source, signature):
    """a function's text, from its signature to its closing brace"""
    start = source.index("\n" + signature)
    end = re.search(r"\n\}\n", source[start:])
    return source[start:start + end.end()]


@pytest.mark.parametrize("standard", ["c11", "gnu89"])
def test_rules(tmp_path, standard):
    """Builds the rules with their tests, warnings as errors, as C11 and as
    the game's gnu89, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "duct_tape_logo_test"
    build = subprocess.run(
        [cc, f"-std={standard}", "-Wall", "-Wextra", "-Werror", "-O1", str(TEST_C), str(RULES_C), "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "all passed" in result.stdout


def test_setting_off_unless_set():
    """display.duct_tape_logo: a boolean, false by default, documented."""
    source = PORT_CONFIG_C.read_text(encoding="utf-8")
    assert re.search(r'\{ "display\.duct_tape_logo", _config_boolean, "false",', source)
    assert "`display.duct_tape_logo`" in SETTINGS_MD.read_text(encoding="utf-8")


def test_code_writes_the_setting():
    """Entering the code toggles the logo and writes the setting; the logo
    reads the setting each frame, so one set by hand shows too."""
    source = MENUS_C.read_text(encoding="utf-8")
    press = function(source, "static boolean konami_press(int press)")
    assert "duct_tape_logo_toggle(" in press
    assert "config_write_boolean(DUCT_TAPE_LOGO_SETTING, shown)" in press
    update = function(source, "static void duct_tape_logo_update(struct widget_instance *logo)")
    assert "duct_tape_logo_sync(&duct_tape_logo, config_boolean(DUCT_TAPE_LOGO_SETTING))" in update
    assert '#define DUCT_TAPE_LOGO_SETTING "display.duct_tape_logo"' in source


def test_presses_mapped_from_the_menus_buttons():
    """The controller's d-pad and its B and A (the keyboard's arrows, Escape
    and Enter come as these), first presses only; the keyboard's B and A
    letters as well."""
    source = MENUS_C.read_text(encoding="utf-8")
    event = function(source, "boolean pc_menu_konami_event(boolean button, short index, short value)")
    assert "value != 1" in event
    for button, press in (("dpad_up", "up"), ("dpad_down", "down"), ("dpad_left", "left"),
                          ("dpad_right", "right")):
        assert f"case _gamepad_binary_button_{button}: press = _konami_{press};" in event
    assert "case _gamepad_analog_button_b: press = _konami_b;" in event
    assert "case _gamepad_analog_button_a: press = _konami_a;" in event
    assert "default: press = _konami_other;" in event
    keys = function(source, "void pc_menu_konami_keys(void)")
    assert "{ 'B', 'A' }" in keys
    assert "halo_keyboard_menu_letters()" in keys


def test_menu_draws_the_right_bitmap():
    """The tape is shown exactly when the logo's frame is the tape's, over
    the Halo logo, both moved by the swap's build-up."""
    source = MENUS_C.read_text(encoding="utf-8")
    update = function(source, "static void duct_tape_logo_update(struct widget_instance *logo)")
    assert "tape->visible = frame == DUCT_TAPE_LOGO_TAPE_FRAME;" in update
    assert "halo->vertical_offset = (short)drop;" in update
    assert "tape->vertical_offset = (short)drop;" in update
    assert 'else if (!strcmp(name, "port duct tape logo"))' in source
    assert '"port duct tape logo",' in MENU_TAGS_C.read_text(encoding="utf-8")


def platforms(element):
    return set((element.get("platform") or "desktop android switch console").split())


def test_main_menu_layout():
    """The Chupathingy main menu: the logo widget on the desktop, Android and
    Switch, with ui.map's Halo logo (the player's own) and the tape after it,
    on top; the consoles keep the plain logo."""
    root = ElementTree.parse(MAIN_MENU_XML).getroot()
    widgets = {widget.get("name"): widget for widget in root.iter("widget")}
    bitmaps = {bitmap.get("name"): bitmap for bitmap in root.iter("bitmap")}
    main_menu = widgets["chupathingy/main_menu"]
    children = {child.get("widget"): child for child in main_menu.findall("child")}
    assert platforms(children["chupathingy/main_menu_logo"]) == {"desktop", "android", "switch"}
    assert platforms(children["main_menu/halo_logo"]) == {"console"}
    assert children["chupathingy/main_menu_logo"].get("y") == children["main_menu/halo_logo"].get("y")

    logo = widgets["chupathingy/main_menu_logo"]
    assert [data.get("input") for data in logo.findall("data")] == ["port duct tape logo"]
    inner = [widget.get("name") for widget in logo.findall("widget")]
    assert inner == ["chupathingy/main_menu_logo_halo", "chupathingy/main_menu_logo_tape"]
    assert widgets["chupathingy/main_menu_logo_halo"].get("bitmap") == "ui\\shell\\main_menu\\halo_logo"
    tape_bitmap = widgets["chupathingy/main_menu_logo_tape"].get("bitmap")
    frames = bitmaps[tape_bitmap].findall("frame")
    assert [frame.get("png") for frame in frames] == ["chupathingy/main_menu/duct_tape__0.png"]
    assert (frames[0].get("width"), frames[0].get("height")) == ("640", "256")


def test_tape_picture():
    """The tape: a 640x256 8-bit RGBA PNG (the logo's box), embedded."""
    data = TAPE_PNG.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    width, height, depth, color = struct.unpack(">IIBB", data[16:26])
    assert (width, height, depth, color) == (640, 256, 8, 6)
    files = json.loads((MENUS / "menus.json").read_text(encoding="utf-8"))["files"]
    assert "chupathingy/main_menu/duct_tape__0.png" in files


def test_hooks_left_out_of_the_consoles():
    """The consoles' builds (and the original Xbox's) never see the code:
    their menus keep their B and A."""
    source = UI_WIDGET_C.read_text(encoding="utf-8")
    guard = "#if !defined(HALO_CONSOLE) && !defined(HALO_XBOX_CONSOLE)"
    for call in ("pc_menu_konami_keys();", "pc_menu_konami_event("):
        index = source.index(call)
        assert source.rfind(guard, 0, index) > source.rfind("#endif", 0, index)
    update = function(source, "void process_ui_widgets(")
    assert "konami_on_main_menu = main_menu_screen_is_active() && pc_menus_chupathingy();" in update
