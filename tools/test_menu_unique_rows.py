# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""No settings screen shows the same row twice, in either menu set:

- the settings pages (tools/port_settings.py): in each list of each screen,
  on each platform, no two rows have one label;
- the Chupathingy menus' lists (port/assets/menus/chupathingy): no two rows
  are one widget or have one label;
- the Xbox menus' EDIT PROFILE SETTINGS: the rows ui_widget.c puts in
  (MORE SETTINGS, the setup screen's) have words of their own, none the
  Xbox's own rows have (0.9.2's smoke test found ADVANCED SETUP twice)."""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import port_settings  # noqa: E402

PLATFORMS = ["desktop", "android", "switch", "console"]
CHUPATHINGY = ROOT / "port/assets/menus/chupathingy"
# (the Xbox's EDIT PROFILE SETTINGS rows, as the game shows them:
# ui_widget.c's SETUP_ENTRY_ROWS)
XBOX_PROFILE_ROWS = ["CHANGE NAME", "CONTROLLER SETUP", "ADVANCED SETUP", "CHANGE COLOR", "SAVE CHANGES"]


def shown_labels(spec):
    """each list's labels shown on each platform: {(view, platform): [label]}"""
    entries, labels, _ = port_settings._layout(spec)
    views = {view: rows for view, _, _, rows in spec.get("views", [])}
    shown, view = {}, "main"
    for entry in entries:
        if entry[0] == "mark":
            view = entry[1]
            continue
        if entry[0] == "link":
            platform = None if any(port_settings._on_console(row[4]) for row in views[entry[1]]) else \
                port_settings.NOT_CONSOLE
        else:
            platform = entry[1][4]
            if entry[1][1] in spec.get("unplaced", ()):
                continue
        for name in PLATFORMS:
            if platform is None or name in platform.split():
                shown.setdefault((view, name), []).append(labels[entry[4]])
    return shown


def duplicates(items):
    seen, twice = set(), []
    for item in items:
        if item in seen:
            twice.append(item)
        seen.add(item)
    return twice


@pytest.mark.parametrize("screen", sorted(port_settings.SCREENS))
def test_settings_pages_rows_unique(screen):
    for (view, platform), labels in sorted(shown_labels(port_settings.SCREENS[screen]).items()):
        assert not duplicates(labels), f"{screen} {view} on {platform}: {duplicates(labels)} twice"


def chupathingy_widgets():
    widgets = {}
    for path in sorted(CHUPATHINGY.rglob("*.xml")):
        for widget in ET.parse(path).getroot().iter("widget"):
            widgets.setdefault(widget.get("name"), (path.name, widget))
    return widgets


def row_label(widgets, name):
    """a row's words: its text, its string list's entry, else its name (a
    widget of the game's own tags)"""
    found = widgets.get(name)
    if not found:
        return ("widget", name)
    widget = found[1]
    if widget.get("text"):
        return ("text", widget.get("text").strip().upper())
    if widget.get("string_list"):
        # (the main menu's rows are their pictures, the string theirs for
        # the description)
        return ("string", widget.get("string_list"), widget.get("string_index", "0"), widget.get("bitmap"))
    return ("widget", name)


def shown_children(widget):
    """a list's rows by its nested lists (a view_ child starts one: the
    settings pages' views) and platform: {(view, platform): [widget]}"""
    shown, view = {}, "main"
    for child in widget.iter("child"):
        name = child.get("widget")
        if name.rsplit("/", 1)[-1].startswith("view_"):
            view = name
            continue
        platform = child.get("platform")
        for each in PLATFORMS:
            if platform is None or each in platform.split():
                shown.setdefault((view, each), []).append(name)
    return shown


def chupathingy_lists():
    widgets = chupathingy_widgets()
    return [(f"{file}:{name}", shown_children(widget), widgets)
            for name, (file, widget) in sorted(widgets.items()) if widget.get("type") == "column_list"]


def test_chupathingy_lists_found():
    names = [name for name, _, _ in chupathingy_lists()]
    assert "settings.xml:chupathingy/settings/list" in names


@pytest.mark.parametrize("name,children,widgets", chupathingy_lists(), ids=lambda value: value if
                         isinstance(value, str) else "")
def test_chupathingy_lists_rows_unique(name, children, widgets):
    for (view, platform), rows in sorted(children.items()):
        where = f"{name} {view.rsplit('/', 1)[-1]} on {platform}"
        assert not duplicates(rows), f"{where}: {duplicates(rows)} twice"
        labels = [row_label(widgets, row) for row in rows]
        assert not duplicates(labels), f"{where}: {duplicates(labels)} twice"


def xbox_added_rows():
    source = (ROOT / "source/interface/ui_widget.c").read_text()
    match = re.search(r"ui_widget_setup_entry_label\(\s*short kind\)\s*\{(.*?)\n\}", source, re.S)
    assert match, "ui_widget.c has no ui_widget_setup_entry_label"
    return re.findall(r'L"([^"]+)"', match.group(1))


def test_xbox_profile_rows_unique():
    added = xbox_added_rows()
    assert len(added) == 2, added
    rows = XBOX_PROFILE_ROWS[:4] + added + XBOX_PROFILE_ROWS[4:]
    assert not duplicates(rows), f"Xbox EDIT PROFILE SETTINGS: {duplicates(rows)} twice"


def test_xbox_rows_match_ui_widget():
    """the Xbox rows above are as many as ui_widget.c's SETUP_ENTRY_ROWS"""
    source = (ROOT / "source/interface/ui_widget.c").read_text()
    match = re.search(r"SETUP_ENTRY_ROWS = (\d+)", source)
    assert match and int(match.group(1)) == len(XBOX_PROFILE_ROWS)
