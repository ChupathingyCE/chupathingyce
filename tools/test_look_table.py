# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""The look presets' table in docs/settings.md is the code's (tools/look_table.py): every setting in
port_config.c's config_look_knobs has a row, with what each look sets and what the Xbox look shows; the
Chupathingy look is the settings' own defaults (display.look's default changes nothing); and the Xbox look
sets only drawing settings, never a [tuning] performance knob that isn't a look of its own."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import look_table  # noqa: E402


def test_settings_doc_is_the_code():
    result = subprocess.run([sys.executable, str(ROOT / "tools" / "look_table.py"), "--check",
                             str(ROOT / "docs" / "settings.md")], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr


def test_every_setting_has_a_row():
    source = look_table.tuning_table.read(look_table.PORT_CONFIG)
    table = source[source.index("} config_look_knobs[] ="):]
    names = re.findall(r'^\t\{ "([a-z_.]+)", "[a-z_]+", \{', table[:table.index("};")], re.M)
    assert names, "no look settings read"
    made = look_table.table()
    for name in names:
        assert f"| `{name}` |" in made, f"{name}: no row"
    assert set(names) == {row["name"] for row in look_table.rows()}


def test_a_setting_without_a_line_fails(monkeypatch):
    name = next(iter(look_table.SHOWS))
    shows = dict(look_table.SHOWS)
    del shows[name]
    monkeypatch.setattr(look_table, "SHOWS", shows)
    try:
        look_table.rows()
    except KeyError as error:
        assert name in str(error)
    else:
        raise AssertionError("a setting without a line made a table")


def test_the_looks():
    """(Milenko, 11-20) Xbox: the scoreboard's panel, the Light effects and doors blocking light off, and 128
    shadows; Chupathingy: today's defaults, so the default look changes no one's game."""
    rows = {row["name"]: row for row in look_table.rows()}
    xbox = {name: row["looks"][1] for name, row in rows.items()}
    assert xbox["display.scoreboard_background"] == "false"
    assert xbox["display.light_effects"] == "off"
    assert xbox["display.doors_block_light"] == "false"
    assert xbox["display.shadow_resolution"] == "128"
    assert all(row["looks"][0] is None for row in rows.values()), "the Chupathingy look changes a default"
    # (each setting is a display setting: the look is drawing only)
    assert all(name.startswith("display.") for name in rows)
