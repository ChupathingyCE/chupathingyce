"""The About text's version fits its panel (#1150).

A test build's version, "0.9.1-testing.202611122017 (b43619e)", ran past the
right edge of the menus' ABOUT text (port/linux/game/menu_functions.c) and
lost its last digit. That line is now "Chupathingy <version>" where it fits,
the version alone where it doesn't (the description under it says "About
Chupathingy.", and the lines after it keep their room), and broken at its
spaces if it is wider still, measured in the text box's own font
(ui_widget_port_text_box_room). The Delta Guide's ABOUT (port/linux/src/
about.c) breaks its version and platform lines at their spaces too, and
draws a piece with no space to break at smaller, never past the edge.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MENUS = (ROOT / "port" / "linux" / "game" / "menu_functions.c").read_text(encoding="utf-8")
WIDGET = (ROOT / "source" / "interface" / "ui_widget.c").read_text(encoding="utf-8")
ABOUT = (ROOT / "port" / "linux" / "src" / "about.c").read_text(encoding="utf-8")


def function(source, name):
    match = re.search(r"^(?:static )?[a-z][^;\n]*\b" + name + r"\((?:[^;{]*?)\)\s*\n\{", source, re.M | re.S)
    assert match, name
    body = match.end() - 1
    return source[body:source.index("\n}\n", body)]


def test_menus_about_version_fits():
    about = function(MENUS, "chupathingy_settings_about")
    assert "chupathingy_about_version(about, version, first, sizeof(first));" in about
    # (the first line is the fitted one, not "Chupathingy %s" as it was)
    assert '"Chupathingy %s\\r\\n"' not in about
    fit = function(MENUS, "chupathingy_about_version")
    # (the whole line where it fits, in the text box's font and width)
    assert fit.index('snprintf(line, (size_t)size, "Chupathingy %s", version);') < fit.index(
        "ui_widget_port_text_box_room(about, &font_index, &width)")
    assert "draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);" in fit
    assert "if (chupathingy_about_width(line, (long)strlen(line)) <= width)\n\t\treturn;" in fit
    # (else the version alone, broken only at its spaces)
    assert 'snprintf(words, sizeof(words), "%s", version);' in fit
    assert "strchr(word, ' ')" in fit and "'\\r'" in fit and "'\\n'" in fit


def test_text_box_room():
    room = function(WIDGET, "ui_widget_port_text_box_room")
    assert "definition->text_font.index == NONE" in room
    assert "definition->bounds.x1 - definition->bounds.x0 - 2 * definition->horizontal_offset" in room


def test_guide_about_version_wraps():
    lines = function(ABOUT, "lines_make")
    assert "count = line_wrap(count, _line_version, text, width);" in lines
    assert "count = line_wrap(count, _line_platform, text, width);" in lines
    render = function(ABOUT, "about_render")
    assert "ui_style_text_scaled(UI_TEXT_NAME, shown > width ? width / shown : 1.0f" in render
