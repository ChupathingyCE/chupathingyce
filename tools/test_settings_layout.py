# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""The settings screens' lists (tools/port_settings.py) fit above their help
line: in every list of every screen, on every platform, the last row's
bottom is at or above the help's top."""
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import port_settings  # noqa: E402

# (a row's top is 73 plus its place times the spacing, and it is 28 tall:
# port_settings._screen and _setting_row)
LIST_TOP = 73
ROW_HEIGHT = 28
PLATFORMS = ["desktop", "android", "switch", "console"]


def lists(spec):
    """each list's places of rows shown on each platform"""
    entries, _, _ = port_settings._layout(spec)
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
                shown.setdefault((view, name), set()).add(entry[3])
    return shown


@pytest.mark.parametrize("screen", sorted(port_settings.SCREENS))
def test_rows_fit_above_the_help(screen):
    spec = port_settings.SCREENS[screen]
    help_top = spec.get("help_top", 350)
    for (view, platform), places in sorted(lists(spec).items()):
        bottom = LIST_TOP + max(places) * spec["spacing"] + ROW_HEIGHT
        assert bottom <= help_top, f"{screen} {view} on {platform}: rows end at {bottom}, the help starts at {help_top}"
