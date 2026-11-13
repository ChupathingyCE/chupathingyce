"""The main menus' DELTA row (tools/delta_menu_art.py,
port/assets/menus/chupathingy/main_menu.xml): its pictures are what the
tool draws, the game embeds them, and the menus wire the row."""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import delta_menu_art  # noqa: E402

MENUS = ROOT / "port" / "assets" / "menus"


def test_pictures_are_the_tools():
    for state in (0, 1):
        committed = (MENUS / "chupathingy" / "main_menu" / f"menu_delta__{state}.svg").read_text()
        assert committed == delta_menu_art.svg(delta_menu_art.WORD, state == 1), \
            f"menu_delta__{state}.svg is not what tools/delta_menu_art.py draws: run it"


def test_letters_fit_and_center():
    places = delta_menu_art.placed("DELTA", delta_menu_art.glyphs("DELTA"))
    assert [letter for letter, _ in places] == list("DELTA")
    xs = [x for _, x in places]
    assert xs == sorted(xs) and xs[0] > 0 and xs[-1] < delta_menu_art.WIDTH


def test_embedded_and_wired():
    files = json.loads((MENUS / "menus.json").read_text())["files"]
    for state in (0, 1):
        assert f"chupathingy/main_menu/menu_delta__{state}.png" in files
    main_menu = (MENUS / "chupathingy" / "main_menu.xml").read_text()
    assert re.search(r'<widget name="chupathingy/main_menu_delta"', main_menu)
    assert main_menu.count('run="port delta open"') >= 4
    menu_tags = (ROOT / "port" / "linux" / "game" / "menu_tags.c").read_text()
    assert '"port delta open"' in menu_tags
    functions = (ROOT / "port" / "linux" / "game" / "menu_functions.c").read_text()
    assert '!strcmp(name, "port delta open")' in functions


def test_rows_only_with_the_setting():
    """delta.menu_item (off by default) puts the DELTA rows in the menus:
    the main menu's list has none of its own, and every place that adds one
    asks the setting as the menus load"""
    main_menu = (MENUS / "chupathingy" / "main_menu.xml").read_text()
    assert '<child widget="chupathingy/main_menu_delta"' not in main_menu
    config = (ROOT / "port" / "linux" / "src" / "port_config.c").read_text()
    assert re.search(r'\{ "delta\.menu_item", _config_boolean, "false"', config)
    # (on Android, on: a touch screen has no Guide button, so the rows are
    # the way in there)
    android = config[config.index("#ifdef HALO_ANDROID\n\t/* (a touch screen has no Guide button"):]
    assert re.match(r'#ifdef HALO_ANDROID\n(\t/\*[^*]*\*/\n)\t\{ "delta\.menu_item", _config_boolean, "true"', android)
    assert re.search(r'\{ "delta\.hint_shown", _config_integer, "0"', config)
    menu_tags = (ROOT / "port" / "linux" / "game" / "menu_tags.c").read_text()
    offered = menu_tags[menu_tags.index("static boolean main_menu_delta_offered(void)"):]
    offered = offered[:offered.index("\n}\n")]
    assert 'config_boolean("delta.menu_item")' in offered
    for patch in ("static void main_menu_delta_patch(void)", "static void multiplayer_delta_patch_gearbox(void)"):
        body = menu_tags[menu_tags.index(patch):]
        body = body[:body.index("\n}\n")]
        assert "main_menu_delta_offered()" in body, patch
    ui_widget = (ROOT / "source" / "interface" / "ui_widget.c").read_text()
    assert "pc_menu_delta_item_offered()" in ui_widget
    assert "online_games.delta &&" in ui_widget
