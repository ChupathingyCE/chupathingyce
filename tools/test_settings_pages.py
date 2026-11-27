"""Checks the Guide's SETTINGS pages (port/linux/src/settings_rows.c), which
the setup screen's Settings follows too.

The pages and their lines live in settings_rows.c alone: the Guide
(port/linux/game/guide.c) and the setup screen (launcher.c) have no list of
their own. Every key a page names is a setting of port_config.c, and every
row of settings_rows.c is a key of it. The setup screen's tabs are the
pages but ABOUT, in their order, then its own."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port" / "linux" / "src"
GAME = ROOT / "port" / "linux" / "game"


def page_lines():
    rows = (SRC / "settings_rows.c").read_text()
    lines = {}
    for name in ("display", "sound", "game", "guide", "about"):
        match = re.search(r"static const char \*const " + name + r"_lines\[\] =\s*\{(.*?)\};", rows, re.S)
        assert match, name
        lines[name] = re.findall(r'"([^"]+)"', match.group(1))
    return lines


def config_keys():
    config = (SRC / "port_config.c").read_text()
    return set(re.findall(r'^\t\{ "([a-z0-9_]+\.[a-z0-9_]+)", _config_', config, re.M))


def test_one_list():
    guide = (GAME / "guide.c").read_text()
    launcher = (SRC / "launcher.c").read_text()
    for source in (guide, launcher):
        assert "_lines[] =" not in source
        assert '"#MENUS"' not in source


def test_page_keys_are_settings():
    keys = config_keys()
    for page, lines in page_lines().items():
        for line in lines:
            if line[0] in "#=@":
                continue
            assert line in keys, f"{page}: {line} is no setting of port_config.c"


def test_rows_are_settings():
    rows = (SRC / "settings_rows.c").read_text()
    keys = config_keys()
    for key in re.findall(r'^\t\{ _in_[a-z_| ]+, "[^"]+", "([a-z0-9_.]+)"', rows, re.M):
        assert key in keys, key


def test_tabs_follow_the_pages():
    rows = (SRC / "settings_rows.c").read_text()
    pages = re.findall(r'"(\w+)"', re.search(r"page_names\[NUMBER_OF_SETTINGS_PAGES\] =\s*\{(.*?)\};", rows, re.S)
        .group(1))
    assert pages == ["DISPLAY", "SOUND", "GAME", "GUIDE", "ABOUT"]
    launcher = (SRC / "launcher.c").read_text()
    tabs = re.findall(r'"(\w+)"', re.search(r"tab_names\[NUMBER_OF_TABS\] =\s*\{(.*?)\};", launcher, re.S).group(1))
    assert tabs[:4] == pages[:4], tabs
    assert tabs[4:] == ["NETWORK", "PATHS", "UPDATES", "ALL"], tabs
