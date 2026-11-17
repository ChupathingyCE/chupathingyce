"""Runs the C tests of the menus' text fit (port/linux/src/menu_text_fit.c),
with the build machine's C compiler: a map card's name wider than the card
is cut with "..." where it stops fitting, never clipped mid-word
("TRUTH AND RECONCILIATIO", "coldsnap_20th_"). The unit has no game
dependency, so it compiles anywhere."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def test_menu_text_fit(tmp_path):
    """Builds the fit with its tests, warnings as errors, and runs them."""
    cc = next((path for path in map(shutil.which, ("clang", "cc", "gcc")) if path), None)
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "menu_text_fit_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "menu_text_fit_test.c"),
         str(ROOT / "port" / "linux" / "src" / "menu_text_fit.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_map_list_names_are_fitted():
    """The map lists' strings go through the fit before a text box draws
    them (source/interface/ui_widget.c), and the Chupathingy menus' map and
    level rows too (port/linux/game/menu_functions.c)."""
    widget = (ROOT / "source" / "interface" / "ui_widget.c").read_text(encoding="utf-8")
    menus = (ROOT / "port" / "linux" / "game" / "menu_functions.c").read_text(encoding="utf-8")
    assert "menu_text_fit(text, size, width, MAX(lines, 1), ui_widget_port_text_measure, NULL)" in widget
    assert "ui_widget_port_text_box_fit(widget, fitted, NUMBEROF(fitted))" in widget
    assert "ui_widget_port_text_box_fit(row_text, text, ROW_TEXT_LENGTH)" in menus
