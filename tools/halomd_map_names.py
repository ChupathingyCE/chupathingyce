#!/usr/bin/env python3
"""Writes port/linux/game/halomd_map_names.h, the names of HaloMD's maps by
their files' names, from HaloMD's public mod list (mods.json or
mods.json.gz, https://halomd.macgamingmods.com/mods/mods.json.gz):

    python3 tools/halomd_map_names.py mods.json.gz

The menus' map list and the server browser name a map played as
<file>@md by it (port/linux/game/ui_map_list.c). Only each map's file name
(its "identifier") and its name are taken: no map, art or other data of
HaloMD's.
"""

import argparse
import gzip
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "port/linux/game/halomd_map_names.h"

HEADER = """/*
HALOMD_MAP_NAMES.H

HaloMD's maps' names, by their files' names (ui_map_list.c), as HaloMD's
public mod list names them. Written by tools/halomd_map_names.py; not
edited by hand.
*/

static struct
{
\tchar const *file;
\twchar_t const *name;
} const halomd_map_names[] =
{
"""


def wide_literal(text: str) -> str:
    """text as a 16-bit wide string literal: ASCII as it is, any other
    character as an escape in a literal of its own (so that the characters
    after it are not read as more of its digits)"""
    parts, plain = [], ""
    for character in text:
        if 32 <= ord(character) < 127 and character not in "\"\\":
            plain += character
        elif character in "\"\\":
            plain += "\\" + character
        else:
            if plain:
                parts.append(f'L"{plain}"')
                plain = ""
            parts.append(f'L"\\x{ord(character):04x}"')
    if plain or not parts:
        parts.append(f'L"{plain}"')
    return " ".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("mods", type=Path, help="HaloMD's mods.json or mods.json.gz")
    args = parser.parse_args()

    data = args.mods.read_bytes()
    if data[:2] == b"\x1f\x8b":
        data = gzip.decompress(data)
    mods = json.loads(data.decode("utf-8"))["Mods"]
    names = sorted({(mod["identifier"].lower(), mod["name"].strip()) for mod in mods
                    if mod.get("identifier") and mod.get("name")})
    lines = [HEADER]
    for identifier, name in names:
        if not all(c.isalnum() or c in "_-." for c in identifier):
            continue
        lines.append(f'\t{{ "{identifier}", {wide_literal(name)} }},\n')
    lines.append("};\n")
    OUTPUT.write_text("".join(lines), encoding="utf-8")
    print(f"{OUTPUT.relative_to(ROOT)}: {len(names)} maps")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
