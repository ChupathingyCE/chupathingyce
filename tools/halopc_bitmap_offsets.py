#!/usr/bin/env python3
"""Writes port/linux/game/halopc_bitmap_offsets.h, which bitmap of which tag
each pixels' offset in Halo PC's own bitmaps.map holds, from that file:

    python3 tools/halopc_bitmap_offsets.py path/to/bitmaps.map

A HaloMD map (a Halo PC retail map) gives each stock bitmap's pixels as an
offset in Halo PC's bitmaps.map (HaloMD's is the same file), not by its
tag's name, and the tools that made some of them left their stock tags
renamed: a copy of the HUD's reticles named after the Warthog's tires. The
game reads those maps with Custom Edition's bitmaps.map, laid out
differently (port/linux/game/ce_resources.c), and finds each bitmap there by
its offset's tag: the table keeps, for each offset, a hash of the tag's path
(FNV-1a, 32 bits, of the path in lowercase) and the bitmap's index in it.
Only offsets, hashes and indices are written: no names, pixels or other data
of the file.
"""

import argparse
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "port/linux/game/halopc_bitmap_offsets.h"

HEADER = """/*
HALOPC_BITMAP_OFFSETS.H

Halo PC's bitmaps.map (the retail game's, and HaloMD's): each stock bitmap's
pixels' offset there, by offset, with its tag's path's hash (FNV-1a, 32
bits, of the path in lowercase: ce_resources.c, ce_path_hash) and its index
in the tag (ce_retail_bitmaps). Written by tools/halopc_bitmap_offsets.py;
not edited by hand.
*/

static struct
{
\tunsigned long offset;
\tunsigned long path_hash;
\tunsigned short index;
} const halopc_bitmap_offsets[] =
{
"""


def path_hash(path: bytes) -> int:
    value = 0x811C9DC5
    for byte in path.lower():
        value = ((value ^ byte) * 0x01000193) & 0xFFFFFFFF
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("bitmaps", type=Path, help="Halo PC's bitmaps.map")
    args = parser.parse_args()

    data = args.bitmaps.read_bytes()
    kind, paths_offset, resources_offset, count = struct.unpack_from("<IIII", data, 0)
    if kind != 1:
        raise SystemExit(f"{args.bitmaps}: not a bitmaps resource map (type {kind})")
    rows = []
    for index in range(count):
        name_offset, _size, offset = struct.unpack_from("<III", data, resources_offset + index * 12)
        start = paths_offset + name_offset
        name = data[start:data.index(b"\0", start)]
        # (each bitmap is named after its tag, with its index: <path>_<n>)
        path, _, number = name.rpartition(b"_")
        if not path or not number.isdigit():
            continue
        rows.append((offset, path_hash(path), int(number)))
    rows.sort()
    text = "".join(f"\t{{ 0x{offset:08x}, 0x{value:08x}, {number} }},\n" for offset, value, number in rows)
    OUTPUT.write_text(HEADER + text + "};\n", encoding="utf-8")
    print(f"{OUTPUT.relative_to(ROOT)}: {len(rows)} bitmaps")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
