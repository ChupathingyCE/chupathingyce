"""Checks of the console tag byte swap's generated tables
(port/console/game/tag_byte_swap_tables.c, tools/tag_swap_generate.py):
blocks whose elements the tag validator's schemas name by a typedef
(real_point3d) are swapped too. A structure bsp mirror's points were left
in the Xbox's byte order on the 360 and the Wii U, so a level with a mirror
(The Silent Cartographer's, Two Betrayals' floors) read them as garbage.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import tag_swap_generate  # noqa: E402

TABLES = ROOT / "port/console/game/tag_byte_swap_tables.c"


def layouts():
    """each layout's description and its operations, child blocks by their
    layout's description"""
    text = TABLES.read_text()
    ops_text = text.split("tag_swap_ops[] =", 1)[1].split("};", 1)[0]
    ops = [tuple(map(int, op)) for op in re.findall(r"^\t\{ (\d+), (\d+), (\d+) \},$", ops_text, re.M)]
    found = re.findall(r"^\t\{ (\d+), (\d+), (\d+) \}, /\* (.*) \*/$", text, re.M)
    names = {index: entry[3] for index, entry in enumerate(found)}
    result = {}
    for size, first, count, name in found:
        first, count = int(first), int(count)
        result[name] = (int(size), [(offset, kind, names[target] if kind == 0 else target)
                                    for offset, kind, target in ops[first:first + count]])
    return result


def test_schema_typedef_definition_is_read():
    line = ("static struct tag_schema_definition const real_point3d_schema =\n"
            "\tTAG_SCHEMA_DEFINITION(real_point3d, real_point3d, empty_fields);")
    assert tag_swap_generate.SCHEMA_DEFINITION.findall(line) == [("real_point3d_schema", "real_point3d")]
    records = {"union real_point3d": []}
    assert tag_swap_generate.schema_record(records, "real_point3d") == "union real_point3d"
    assert tag_swap_generate.schema_record(records, "struct mirror") == "struct mirror"
    assert tag_swap_generate.schema_record(records, "no_such_type") is None


def test_mirror_points_are_swapped():
    mirror = next(value for name, value in layouts().items() if "structure_visibility_mirror" in name)
    size, ops = mirror
    assert size == 64
    # (the points block at 52: its elements, three floats each)
    assert (52, 0, "union real_point3d") in ops


def test_portal_and_fog_plane_vertices_still_swapped():
    tables = layouts()
    portal = next(value for name, value in tables.items() if "structure_visibility_portal" in name)
    fog_plane = next(value for name, value in tables.items() if "structure_fog_plane_schema" in name)
    assert (52, 0, "union real_point3d") in portal[1]
    assert (20, 0, "union real_point3d") in fog_plane[1]
