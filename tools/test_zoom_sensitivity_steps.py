"""Zoom sensitivity's choices (tools/port_settings.py, #1422): 25% to 200%
in 5% steps, every value the menus offered before still one of them (a
player's setting shows as it is), and no more than a spinner holds
(menu_tags.c's MAXIMUM_STRINGS)."""

import re
from pathlib import Path

from tools import port_settings

ROOT = Path(__file__).resolve().parent.parent


def test_five_percent_steps():
    names = [name for name, _ in port_settings.ZOOM_SENSITIVITIES]
    values = [float(value) for _, value in port_settings.ZOOM_SENSITIVITIES]
    assert names == [f"{percent}%" for percent in range(25, 205, 5)]
    assert all(abs(value * 100 - int(name[:-1])) < 1e-9 for name, value in zip(names, values))
    for old in (0.25, 0.5, 0.75, 1, 1.25, 1.5, 2):
        assert f"{old:g}" in [value for _, value in port_settings.ZOOM_SENSITIVITIES]


def test_fits_a_spinner():
    tags = (ROOT / "port" / "linux" / "game" / "menu_tags.c").read_text()
    maximum = int(re.search(r"#define MAXIMUM_STRINGS (\d+)", tags).group(1))
    assert len(port_settings.ZOOM_SENSITIVITIES) <= maximum
