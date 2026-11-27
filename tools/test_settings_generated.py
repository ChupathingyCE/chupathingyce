# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""The settings screens' files are the ones tools/port_settings.py makes,
and each row's label, value and help line are its own setting's.

The rows' files in port/assets/menus (ce/'s settings screens and
chupathingy/settings_pages.xml) are written by tools/ce_menus.py --settings
from port_settings.py's tables. A row's label is string n of its screen's
labels and its help line string n + 1 of help_strings
(menu_functions.c's "port settings help"), so a file left behind after a
row is added shows every label below it one row off (#1682: two branches
each added a row to Video Setup, and their merge kept both files' old
indices). Checked here:

- every generated file is the same as the generator's output;
- in every settings screen of both menu sets, and Delta Profile's privacy
  page, each row's label, spinner and help come from that row's setting,
  and no two rows share a label string;
- the Chupathingy menus' pages hold the same rows as the screens they
  follow;
- the Guide's SETTINGS pages (settings_rows.c): each key is one row, on one
  page, and its voice rows have a label each."""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import port_settings  # noqa: E402

MENUS = ROOT / "port/assets/menus"
GENERATED = port_settings.generated_files()


@pytest.mark.parametrize("path", sorted(GENERATED))
def test_generated_file_is_current(path):
    committed = (MENUS / path).read_text()
    assert committed == GENERATED[path], (
        f"port/assets/menus/{path} is out of date with tools/port_settings.py: "
        "run python3 tools/ce_menus.py --settings and commit the result")


def committed_widgets():
    """every widget and string list of the committed generated files, by name"""
    widgets, strings = {}, {}
    for path in GENERATED:
        root = ET.parse(MENUS / path).getroot()
        for widget in root.iter("widget"):
            widgets[widget.get("name")] = widget
        for texts in root.iter("strings"):
            strings[texts.get("name")] = [text.get("text") for text in texts.iter("string")]
    return widgets, strings


WIDGETS, STRINGS = committed_widgets()


def settings_screens():
    """(base, spec) of every screen _setting_screen writes"""
    screens = [(f"{port_settings.PE}/{folder}", spec) for folder, spec in port_settings.SCREENS.items()]
    screens += [(f"{port_settings.PE}/{folder}", spec) for folder, spec in port_settings.CHUPATHINGY_ONLY.items()]
    screens.append((f"{port_settings.MT}/server_settings/host_options", port_settings.HOST_OPTIONS))
    return screens


SCREENS = settings_screens()


def label_index(base, key):
    label = WIDGETS.get(f"{base}/{key}_label")
    assert label is not None, f"{base}: no {key}_label"
    assert label.get("string_list") == f"{base}/labels", key
    return int(label.get("string_index"))


@pytest.mark.parametrize("base,spec", SCREENS, ids=[base.rsplit("/", 1)[-1] for base, _ in SCREENS])
def test_rows_are_their_settings(base, spec):
    labels = STRINGS[f"{base}/labels"]
    helps = STRINGS[f"{base}/help_strings"]
    entries, _, _ = port_settings._layout(spec)
    views = {view: (label, help_text) for view, label, help_text, _ in spec.get("views", [])}
    seen = {}
    for entry in entries:
        if entry[0] == "mark":
            continue
        if entry[0] == "link":
            key = entry[2]
            label, help_text = views[entry[1]]
        else:
            row, key = entry[1], entry[2]
            label, help_text = row[0], row[3]
            if not isinstance(row, port_settings.Action):
                spinner = WIDGETS[f"{base}/{key}_spinner"]
                assert spinner.get("setting") == row[1], key
                assert spinner.get("strings") == "|".join(shown for shown, _ in row[2]), key
                assert spinner.get("values") == "|".join(value for _, value in row[2]), key
        index = label_index(base, key)
        assert index not in seen, f"{key} and {seen.get(index)} show one label"
        seen[index] = key
        assert labels[index] == label, f"{key}: label {labels[index]!r}, not {label!r}"
        # (a help's line breaks are written as \n)
        assert helps[index + 1] == help_text.replace("\n", "\\n"), \
            f"{key}: help {helps[index + 1]!r}, not {help_text!r}"
    # (one label, and one help, for each row: the buttons' help is the first)
    assert len(seen) == len(labels), f"{len(labels)} labels for {len(seen)} rows"
    assert len(helps) == len(labels) + 1


def list_rows(name):
    return {child.get("widget") for child in WIDGETS[name].iter("child")
            if child.get("widget").rsplit("/", 1)[-1].startswith(("op_", "view_"))}


@pytest.mark.parametrize("folder", [*port_settings.SCREENS, port_settings.CONTROLS_SCREEN])
def test_chupathingy_pages_have_the_screens_rows(folder):
    base = f"{port_settings.PE}/{folder}"
    rows = list_rows(f"{base}/options_menu")
    assert rows
    assert list_rows(f"{port_settings.CHUPATHINGY}/{folder}_list") == rows
    for row in rows:
        assert row in WIDGETS, row


def guide_tables():
    rows = (ROOT / "port/linux/src/settings_rows.c").read_text()
    table = re.search(r"settings_rows\[\] =\s*\{(.*?)\n\};", rows, re.S).group(1)
    keys = re.findall(r'\{ [^,]+, "[^"]+", "([a-z0-9_.]+)"', table)
    pages = {}
    for name in ("display", "sound", "game", "guide", "about"):
        lines = re.search(r"static const char \*const " + name + r"_lines\[\] =\s*\{(.*?)\};", rows, re.S).group(1)
        pages[name] = [line for line in re.findall(r'"([^"]+)"', lines) if line[0] not in "#=@"]
    guide = (ROOT / "port/linux/game/guide.c").read_text()
    voice = {}
    for name in ("labels", "keys"):
        found = re.search(r"voice_setting_" + name + r"\[NUMBER_OF_VOICE_SETTINGS\] =\s*\{(.*?)\};", guide, re.S)
        voice[name] = re.findall(r'"([^"]+)"', found.group(1))
    return keys, pages, voice


def test_guide_rows_are_their_settings():
    keys, pages, voice = guide_tables()
    assert len(keys) == len(set(keys)), "a key with two rows"
    shown = [key for lines in pages.values() for key in lines]
    assert len(shown) == len(set(shown)), "a key on two pages, or twice on one"
    assert len(voice["labels"]) == len(voice["keys"]) == len(set(voice["keys"]))
    for key in shown:
        assert key in keys or key in voice["keys"], key
