"""The PC version's menus in port/assets/menus/ce carry every change
tools/port_settings.py's WIDGET_PATCHES makes to them, so a full
tools/ce_menus.py regeneration keeps the port's additions (#1086: the profile
manager's "Delta:" line and DELTA button were written into the generated file
by hand, and a regeneration would have dropped them). Checked for each
patched widget: its attributes set, its children added or inserted, and its
children's offsets."""
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import port_settings  # noqa: E402

MENUS = ROOT / "port/assets/menus"


def _widgets() -> dict:
    widgets = {}
    for path in sorted((MENUS / "ce").glob("*.xml")):
        for widget in ET.parse(path).getroot().iter("widget"):
            widgets[widget.get("name")] = widget
    return widgets


WIDGETS = _widgets()


def _offset(value) -> str:
    return str(value or 0)


@pytest.mark.parametrize("name", sorted(port_settings.WIDGET_PATCHES))
def test_patch_is_in_the_menus(name):
    patch = port_settings.WIDGET_PATCHES[name]
    widget = WIDGETS.get(name)
    assert widget is not None, f"{name}: patched, but in no file of ce/"
    for key, value in patch.get("set", {}).items():
        assert widget.get(key) == (None if value is None else str(value)), f"{name}: {key}"
    children = [(child.get("widget"), _offset(child.get("x")), _offset(child.get("y")))
                for child in widget.findall("child")]
    added = patch.get("children", []) + [line for lines in patch.get("insert_before", {}).values() for line in lines]
    for line in added:
        child = ET.fromstring(line)
        assert (child.get("widget"), _offset(child.get("x")), _offset(child.get("y"))) in children, \
            f"{name}: {line}"
    # (an offset is the PC version's child's: the widget in its place may be
    # another, "swap")
    for original, (x, y) in patch.get("offsets", {}).items():
        if original in patch.get("leave_out", ()):
            continue
        shown = patch.get("swap", {}).get(original, original)
        assert (shown, str(x), str(y)) in children, f"{name}: {shown} at {x}, {y}"
    for before, lines in patch.get("insert_before", {}).items():
        names = [child[0] for child in children]
        for line in lines:
            assert names.index(ET.fromstring(line).get("widget")) < names.index(
                patch.get("swap", {}).get(before, before)), f"{name}: {line} before {before}"


def test_port_widgets_are_generated():
    """the profile manager's own widgets come from port_settings.py, not the
    PC version's file"""
    generated = port_settings.generated_files()["ce/main_menu.profile_manager.port.xml"]
    for name in ("main_menu/profile_manager/delta_line", "main_menu/profile_manager/profile_manager_button_delta"):
        assert f'name="{name}"' in generated
        assert f'name="{name}"' not in (MENUS / "ce/main_menu.profile_manager.xml").read_text()
