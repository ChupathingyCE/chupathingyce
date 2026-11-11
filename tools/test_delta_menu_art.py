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
    assert re.search(r'<child widget="chupathingy/main_menu_delta"', main_menu)
    assert main_menu.count('run="port delta open"') >= 4
    menu_tags = (ROOT / "port" / "linux" / "game" / "menu_tags.c").read_text()
    assert '"port delta open"' in menu_tags
    functions = (ROOT / "port" / "linux" / "game" / "menu_functions.c").read_text()
    assert '!strcmp(name, "port delta open")' in functions
