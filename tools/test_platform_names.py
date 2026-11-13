"""One table names the platforms: delta.h's DELTA_PLATFORM_NAMES. No other
source file may keep its own list of platform keys, which falls behind when
a platform is added and shows the new one as a number or a bare key (the PSP,
platform 10, on 2026-11-13): python3 -m pytest tools/test_platform_names.py"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DELTA_H = ROOT / "port/linux/include/delta.h"
# files that may name several keys: the table itself, the icon font's build
# (it maps its logo files to the registry's numbers), and Delta Stats' few
# families (game_events.c: any platform it doesn't list counts as "other")
ALLOWED = {"port/linux/include/delta.h", "tools/platform_icons_font.py", "port/linux/game/game_events.c"}
# where a hand-written list is fine: tests and docs
SKIPPED_PARTS = {"tests", "docs", "third_party", "build"}


def registry():
    text = DELTA_H.read_text(encoding="latin-1")
    enum = re.search(r"enum delta_platform\s*\{(.*?)\};", text, re.S).group(1)
    numbers = [name for name in re.findall(r"(_delta_platform_\w+)\s*=", enum)]
    rows = re.findall(r"X\((_delta_platform_\w+), \"(\w*)\", \"([^\"]*)\", \"([^\"]*)\", \"([^\"]*)\"\)", text)
    return numbers, rows


def test_table_has_a_row_for_every_platform_in_order():
    numbers, rows = registry()
    assert [row[0] for row in rows] == numbers


def test_every_platform_has_names():
    _, rows = registry()
    for platform, key, tag, name, caps in rows[1:]:
        assert key and tag and name and caps, platform
        assert caps == caps.upper(), platform


def test_no_other_list_of_platform_keys():
    _, rows = registry()
    keys = {row[1] for row in rows[1:]}
    found = {}
    for path in ROOT.rglob("*"):
        relative = path.relative_to(ROOT).as_posix()
        if path.suffix not in (".c", ".h", ".inl", ".js", ".py") or relative in ALLOWED or path.name.startswith("test_") or \
                SKIPPED_PARTS & set(path.relative_to(ROOT).parts) or relative.startswith("."):
            continue
        named = keys & set(re.findall(r"\"([a-z0-9_]+)\"", path.read_text(encoding="latin-1")))
        if len(named) >= 4:
            found[relative] = sorted(named)
    assert not found, f"name platforms through delta.h's DELTA_PLATFORM_NAMES instead: {found}"
