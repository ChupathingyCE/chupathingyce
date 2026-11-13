"""Checks the Delta Guide's color themes (port/linux/src/ui_overlay_style.c).

Every theme fills every named color, the themes keep their order (a saved
display.theme stays what it was: blue, red, green, orange, then live), and the setting's
words agree everywhere they are listed: the palette's names, the settings
row, and the config's checked words."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port" / "linux" / "src"


def enum_names(header, prefix, end):
    """Returns the names of the enum that ends with `end`, in order."""
    match = re.search(r"enum\s*\{([^}]*\b" + end + r"\b[^}]*)\}", header)
    assert match, end
    return [name for name in re.findall(r"\b(" + prefix + r"\w+)", match.group(1)) if name != end]


def test_theme_order():
    header = (SRC / "ui_overlay.h").read_text()
    assert enum_names(header, "UI_THEME_", "NUMBER_OF_UI_THEMES") == ["UI_THEME_BLUE", "UI_THEME_RED", "UI_THEME_GREEN", "UI_THEME_ORANGE", "UI_THEME_LIVE"]


def test_every_theme_fills_every_color():
    header = (SRC / "ui_overlay.h").read_text()
    colors = set(enum_names(header, "UI_COLOR_", "NUMBER_OF_UI_COLORS"))
    style = (SRC / "ui_overlay_style.c").read_text()
    # (the palettes' table alone: the cellwall's colors are by theme too)
    style = style.split("palettes[", 1)[1].split("\n};", 1)[0]
    tables = re.findall(r"\[(UI_THEME_\w+)\]\s*=\s*\{(.*?)\n\t\}", style, re.S)
    assert [name for name, _ in tables] == ["UI_THEME_BLUE", "UI_THEME_RED", "UI_THEME_GREEN", "UI_THEME_ORANGE", "UI_THEME_LIVE"]
    for name, body in tables:
        filled = re.findall(r"\[(UI_COLOR_\w+)\]\s*=\s*0x[0-9A-F]{8},", body)
        assert len(filled) == len(set(filled)), name
        assert set(filled) == colors, (name, colors ^ set(filled))


def test_words_agree():
    style = (SRC / "ui_overlay_style.c").read_text()
    named = dict(re.findall(r'\[(UI_THEME_\w+)\] = "(\w+)",', style))
    assert named == {"UI_THEME_BLUE": "blue", "UI_THEME_RED": "red", "UI_THEME_GREEN": "green", "UI_THEME_ORANGE": "orange", "UI_THEME_LIVE": "live"}
    rows = (SRC / "settings_rows.c").read_text()
    assert 'theme_choices[] = { "blue", "green", "orange", "live", "red", NULL };' in rows
    assert 'theme_names[] = { "HALO BLUE", "DASHBOARD GREEN", "VISOR ORANGE", "LIVE ORANGE", "CHUPATHINGY RED", NULL };' in rows
    config = (SRC / "port_config.c").read_text()
    assert '{ "display.theme", _config_string, "\\"blue\\""' in config
    assert '{ "display.theme", { "blue", "red", "green", "orange", "live", NULL }, "\\"blue\\"" },' in config
