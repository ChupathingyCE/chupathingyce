"""Runs the C tests of the medals' rules (port/linux/src/medal_rules.h) and
the local service record (port/linux/src/service_record.c), with the build
machine's C compiler, as C89 with GNU extensions too (the consoles' game
code). The units have no SDL or game dependency, so they compile anywhere."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [
    ROOT / "port" / "linux" / "tests" / "medal_rules_test.c",
    ROOT / "port" / "linux" / "src" / "service_record.c",
]


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_medal_rules(tmp_path):
    """Builds the rules and the record with their tests, warnings as errors,
    and runs them (saving a record into a scratch folder)."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "medal_rules_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", *map(str, SOURCES), "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary), str(tmp_path / "service_record.txt")], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert (tmp_path / "service_record.txt").read_text().startswith("service_record 1\n")
    assert not (tmp_path / "service_record.txt.tmp").exists()


def test_rules_header_builds_as_gnu89(tmp_path):
    """The game's code (game_stats.c, on the consoles too) is built as gnu89:
    the header must build there, unused, with no warnings."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "unit.c"
    source.write_text('#include "medal_rules.h"\nint unit(void) { return NUMBER_OF_MEDALS; }\n')
    build = subprocess.run(
        [cc, "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-c", "-I", str(ROOT / "port" / "linux" / "src"),
         str(source), "-o", str(tmp_path / "unit.o")],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr


def test_medal_keys_match_the_docs():
    """Every medal's key is in docs/delta.md's list, in the same order."""
    header = (ROOT / "port" / "linux" / "src" / "medal_rules.h").read_text()
    keys = re.search(r"static char const \*const keys\[NUMBER_OF_MEDALS\] = \{(.*?)\};", header, re.S).group(1)
    keys = re.findall(r'"([a-z_]+)"', keys)
    enum = re.search(r"enum medal_kind\s*\{(.*?)NUMBER_OF_MEDALS", header, re.S).group(1)
    assert len(keys) == len(re.findall(r"MEDAL_[A-Z_]+", enum))
    docs = (ROOT / "docs" / "delta.md").read_text()
    section = docs[docs.index("### Medals"):]
    section = section[:section.index("\n### ", 4)]
    assert re.findall(r"`([a-z_]+)`", section)[:len(keys)] == keys


def test_recorders_share_the_rules():
    """The game list's recorder and Delta Stats' log count medals by the
    one set of rules, and the pop-ups are behind display.medals (off)."""
    stats = (ROOT / "port" / "linux" / "game" / "game_stats.c").read_text()
    log = (ROOT / "port" / "linux" / "src" / "event_log.c").read_text()
    for source in (stats, log):
        assert '#include "' in source and "medal_rules.h" in source
        assert "medal_rules_kill(" in source
    assert 'config_boolean("display.medals")' in stats
    config = (ROOT / "port" / "linux" / "src" / "port_config.c").read_text()
    assert re.search(r'\{ "display\.medals", _config_boolean, "false"', config)


# ---------- the medals' names and icons (port/assets/medals, tools/medal_assets.py: the site's)

ASSETS = ROOT / "port" / "assets" / "medals"
ICONS_HEADER = ROOT / "port" / "linux" / "src" / "medal_icons.h"


def rules_medals():
    """medal_rules.h's keys and names, in its order."""
    header = (ROOT / "port" / "linux" / "src" / "medal_rules.h").read_text()
    keys = re.search(r"static char const \*const keys\[NUMBER_OF_MEDALS\] = \{(.*?)\};", header, re.S).group(1)
    names = re.search(r"static char const \*const names\[NUMBER_OF_MEDALS\] = \{(.*?)\};", header, re.S).group(1)
    return re.findall(r'"([a-z_]+)"', keys), re.findall(r'"([^"]+)"', names)


def icons_header_medals():
    """medal_icons.h's entries: key, name, description, tier."""
    return re.findall(r'^\t\{ "([a-z_]+)", "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)", "([a-z]+)", 0x', ICONS_HEADER.read_text(),
                      re.M)


def test_every_rules_medal_has_the_sites_entry_and_icon():
    """Every key medal_rules.h earns has its entry (the site's name) in
    medals.json and medal_icons.h, and a cell in the atlases; and the other
    way, every entry has its icon and every icon its entry."""
    import json

    data = json.loads((ASSETS / "medals.json").read_text())
    entries = {medal["key"]: medal for medal in data["medals"]}
    header = icons_header_medals()
    keys, names = rules_medals()
    for key, name in zip(keys, names):
        assert key in entries, f"{key}: not in port/assets/medals/medals.json (tools/medal_assets.py)"
        assert entries[key]["name"] == name, f"{key}: the game calls it {name!r}, the site {entries[key]['name']!r}"
    # (the C table is the data file's, in its order: the atlas's)
    assert [entry[0] for entry in header] == [medal["key"] for medal in data["medals"]]
    assert [entry[1] for entry in header] == [medal["name"] for medal in data["medals"]]
    assert [medal["index"] for medal in data["medals"]] == list(range(len(data["medals"])))
    count = re.search(r"MEDAL_ICON_COUNT = (\d+)", ICONS_HEADER.read_text()).group(1)
    assert int(count) == len(data["medals"])
    assert data["site_digest"] in ICONS_HEADER.read_text()


def test_atlases_hold_one_icon_per_medal():
    """Each atlas has a drawn cell for every entry and nothing past them."""
    import json
    import sys

    sys.path.insert(0, str(ROOT / "tools"))
    from embed_assets import png_rgba

    data = json.loads((ASSETS / "medals.json").read_text())
    columns = data["columns"]
    count = len(data["medals"])
    rows = (count + columns - 1) // columns
    for size in data["sizes"]:
        width, height, pixels = png_rgba((ASSETS / data["atlases"][str(size)]).read_bytes(), str(size))
        assert (width, height) == (columns * size, rows * size)
        for cell in range(columns * rows):
            left, top = (cell % columns) * size, (cell // columns) * size
            drawn = sum(1 for y in range(top, top + size) for x in range(left, left + size)
                        if pixels[(y * width + x) * 4 + 3] > 128)
            if cell < count:
                assert drawn > size * size // 4, f"{data['medals'][cell]['key']}: its {size}-pixel icon is empty"
            else:
                assert drawn == 0


def test_icons_header_builds_as_gnu89(tmp_path):
    """medal_icons.h, like medal_rules.h, builds as gnu89 with no warnings."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "unit.c"
    source.write_text('#include "medal_icons.h"\nint unit(void) { return medal_icon_find("double_kill"); }\n')
    build = subprocess.run(
        [cc, "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-c", "-I", str(ROOT / "port" / "linux" / "src"),
         str(source), "-o", str(tmp_path / "unit.o")],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr


def test_kill_feed_lines(tmp_path):
    """The kill feed's medals (medal_lines.h): two kills in one tick on one
    line, a line of another tick taking none, leftovers on their own line
    (and no blank line for none), as gnu89 and C11, warnings as errors."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    for standard in ("gnu89", "c11"):
        binary = tmp_path / f"medal_lines_test_{standard}"
        build = subprocess.run(
            [cc, f"-std={standard}", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "port" / "linux" / "src"),
             str(ROOT / "port" / "linux" / "tests" / "medal_lines_test.c"), "-o", str(binary)],
            capture_output=True, text=True,
        )
        assert build.returncode == 0, build.stdout + build.stderr
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr


def test_hud_medals_stay_behind_the_setting():
    """The kill feed's medals (medal_hud.c) only with display.medals on: off,
    nothing is kept and the kill feed is retail's."""
    hud = (ROOT / "port" / "linux" / "game" / "medal_hud.c").read_text()
    assert 'config_boolean("display.medals")' in hud
    messaging = (ROOT / "source" / "interface" / "hud_messaging.c").read_text()
    for call in ("medal_hud_printed(", "medal_hud_flush(", "hud_message_port_medals("):
        assert call in messaging
    assert messaging.count("#ifdef HALO_MEDAL_ICONS") >= 4
