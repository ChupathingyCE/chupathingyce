#!/usr/bin/env python3
"""Writes port/linux/game/xbox_mod_maps.h, the library of known Xbox-format
map packs and their maps (each pack in its maps_<pack> folder: maps_cex for
Halo CE+, ...), from port/assets/maps/xbox_mod_maps.json (its format:
port/assets/maps/README.md):

    python3 tools/xbox_mod_maps.py            # the header, from the JSON
    python3 tools/xbox_mod_maps.py --check    # fails if the header is stale
    python3 tools/xbox_mod_maps.py --scan DIR --pack cex
                                              # entries for a pack's maps

The game names a pack's map by the entry whose fingerprint is its file's
(port/linux/game/map_packs.c), so a player without the pack's maplist.txt
still sees "Abyss (Halo CE+)". The fingerprint is the one a host sends as
the map's version and a joining machine compares: fingerprint() here and
map_pack_fingerprint_read() in map_packs.c compute the same.
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIBRARY = ROOT / "port/assets/maps/xbox_mod_maps.json"
OUTPUT = ROOT / "port/linux/game/xbox_mod_maps.h"

# the fingerprint: FNV-1a (32 bits) over the file's size (4 bytes, little
# endian) and 16 samples of at most 64 KB, at size / 16 * i (the first at
# the header); 0 is never one (it means "any version" in a game record)
SAMPLES = 16
SAMPLE_SIZE = 0x10000
FNV_OFFSET = 2166136261
FNV_PRIME = 16777619

# a pack's id is its folder's name after maps_: 1 to 8 letters and digits,
# lowercase here, none of the other families' suffixes nor the Xbox's
# language folders (halo_map_families.h)
PACK_ID = re.compile(r"^[a-z0-9]{1,8}$")
RESERVED_IDS = {"ce", "md", "pc", "de", "fr", "es", "it"}
PACK_FIELDS = ("name", "tag", "team", "credits", "source")
# the other names a pack's folder can have (maps_ceplus, maps_ce+ for cex): a
# list in the library, "ceplus,ce+" in the header; not ids, so a few
# characters more are allowed
ALIAS = re.compile(r"^[a-z0-9+_-]{1,16}$")
MAP_FIELDS = ("fingerprint", "pack", "file", "name", "pack_version", "author", "source")
TEXT_LIMIT = 95
CREDITS_LIMIT = 400
TAG_LIMIT = 4
# <file>@<pack> is a cache slot's name, in 31 characters
MAP_NAME_LIMIT = 31

HEADER = """/*
XBOX_MOD_MAPS.H

The library of known Xbox-format map packs and their maps (map_packs.c):
each pack by its folder's name (maps_<id>) and the other names the folder
can have, with its name, tag, team, credits and a public page about it; each
map by its file's fingerprint, with its pack, file, name, the pack's version,
its author and a public page about it, where known. Written by
tools/xbox_mod_maps.py from port/assets/maps/xbox_mod_maps.json; not edited by
hand.
*/

"""


def fnv1a(value: int, data: bytes) -> int:
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & 0xFFFFFFFF
    return value


def fingerprint(path: Path) -> int:
    size = path.stat().st_size
    value = fnv1a(FNV_OFFSET, (size & 0xFFFFFFFF).to_bytes(4, "little"))
    with path.open("rb") as stream:
        for index in range(SAMPLES):
            offset = size // SAMPLES * index
            stream.seek(offset)
            value = fnv1a(value, stream.read(min(SAMPLE_SIZE, size - offset)))
    return value or 1


def maplist_names(text: str) -> dict:
    """a pack's maplist.txt: "file - Display Name" a line (the file without
    .map; the first '-' splits them, spaces around either part dropped)"""
    names = {}
    for line in text.splitlines():
        file, dash, name = line.partition("-")
        file, name = file.strip(), name.strip()
        if dash and file and name and file.lower() not in names:
            names[file.lower()] = name
    return names


def check_text(where: str, entry: dict, field: str, limit: int = TEXT_LIMIT) -> str:
    value = entry.get(field, "")
    if not isinstance(value, str):
        raise ValueError(f"{where}: {field} must be text")
    if len(value) > limit or any(not (32 <= ord(c) < 127) for c in value):
        raise ValueError(f"{where}: {field} must be printable ASCII of at most {limit} characters: {value!r}")
    if field == "source" and value and not value.startswith("https://"):
        raise ValueError(f"{where}: source is a public https page, never a download")
    return value


def c_literal(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def load(path: Path = LIBRARY) -> tuple:
    data = json.loads(path.read_text(encoding="utf-8"))
    packs = {}
    for pack_id, pack in sorted(data.get("packs", {}).items()):
        if not PACK_ID.match(pack_id) or pack_id in RESERVED_IDS:
            raise ValueError(f"pack {pack_id!r}: an id is 1 to 8 lowercase letters and digits, not {sorted(RESERVED_IDS)}")
        unknown = set(pack) - set(PACK_FIELDS) - {"aliases"}
        if unknown:
            raise ValueError(f"pack {pack_id}: unknown fields {sorted(unknown)}")
        aliases = pack.get("aliases", [])
        if not isinstance(aliases, list) or any(not isinstance(a, str) or not ALIAS.match(a) or
                                                a in RESERVED_IDS for a in aliases):
            raise ValueError(f"pack {pack_id}: aliases are folder names (after maps_) of 1 to 16 lowercase letters, "
                             f"digits, + _ -, not {sorted(RESERVED_IDS)}")
        row = {field: check_text(f"pack {pack_id}", pack, field, CREDITS_LIMIT if field == "credits" else
                                 TAG_LIMIT if field == "tag" else TEXT_LIMIT) for field in PACK_FIELDS}
        if not row["name"] or not row["tag"]:
            raise ValueError(f"pack {pack_id}: name and tag are needed")
        row["aliases"] = ",".join(aliases)
        packs[pack_id] = row
    names = {}
    for pack_id, row in packs.items():
        for name in [pack_id, *filter(None, row["aliases"].split(","))]:
            if names.setdefault(name, pack_id) != pack_id or (name != pack_id and name in packs):
                raise ValueError(f"pack {pack_id}: the folder name {name!r} is another pack's")
    maps, seen = [], set()
    for entry in data.get("maps", []):
        where = f"map {entry.get('pack', '?')}/{entry.get('file', '?')}"
        unknown = set(entry) - set(MAP_FIELDS)
        if unknown:
            raise ValueError(f"{where}: unknown fields {sorted(unknown)}")
        digits = check_text(where, entry, "fingerprint").lower()
        if len(digits) != 8 or any(c not in "0123456789abcdef" for c in digits) or digits == "00000000":
            raise ValueError(f"{where}: a fingerprint is 8 hex digits, not 0")
        if digits in seen:
            raise ValueError(f"{where}: fingerprint {digits} is listed twice")
        seen.add(digits)
        row = {field: check_text(where, entry, field) for field in MAP_FIELDS[1:]}
        if row["pack"] not in packs:
            raise ValueError(f"{where}: its pack is not in packs")
        if not row["file"] or not row["name"]:
            raise ValueError(f"{where}: file and name are needed")
        if any(c in row["file"] for c in "\\/:@.") or len(row["file"]) + 1 + len(row["pack"]) > MAP_NAME_LIMIT:
            raise ValueError(f"{where}: file is a map file's name without .map, <file>@<pack> at most "
                             f"{MAP_NAME_LIMIT} characters")
        maps.append((int(digits, 16), row))
    return packs, sorted(maps, key=lambda item: item[0])


def header_text(packs: dict, maps: list) -> str:
    text = HEADER + "static struct xbox_mod_pack const xbox_mod_packs[] =\n{\n"
    for pack_id, row in packs.items():
        text += (f"\t{{ {c_literal(pack_id)}, {c_literal(row['aliases'])}, " +
                 ", ".join(c_literal(row[field]) for field in PACK_FIELDS) + " },\n")
    text += "};\n\nstatic struct xbox_mod_map const xbox_mod_maps[] =\n{\n"
    for value, row in maps:
        text += f"\t{{ 0x{value:08x}UL, " + ", ".join(c_literal(row[field]) for field in MAP_FIELDS[1:]) + " },\n"
    return text + "};\n"


def scan(folder: Path, pack: str) -> int:
    maplist = folder / "maplist.txt"
    names = maplist_names(maplist.read_text(encoding="latin-1")) if maplist.is_file() else {}
    maps = []
    for path in sorted(folder.glob("*.map")):
        with path.open("rb") as stream:
            header = stream.read(0x64)
        # (an Xbox cache file, version 5: a multiplayer or campaign map, or
        # the pack's ui.map, its menus)
        if header[:4] != b"daeh" or int.from_bytes(header[4:8], "little") != 5:
            continue
        name = "Menus" if path.stem.lower() == "ui" else names.get(path.stem.lower(), path.stem)
        maps.append({"fingerprint": f"{fingerprint(path):08x}", "pack": pack, "file": path.stem, "name": name,
                     "pack_version": "", "author": "", "source": ""})
    json.dump(maps, sys.stdout, indent=2)
    print()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if the header is not the JSON's")
    parser.add_argument("--scan", type=Path, metavar="DIR", help="print entries for the maps in a pack's folder")
    parser.add_argument("--pack", default="", help="the pack's id (its folder's name after maps_), for --scan")
    args = parser.parse_args()
    if args.scan:
        return scan(args.scan, args.pack)
    text = header_text(*load())
    if args.check:
        if not OUTPUT.is_file() or OUTPUT.read_text(encoding="utf-8") != text:
            print(f"{OUTPUT.relative_to(ROOT)} is stale: run tools/xbox_mod_maps.py", file=sys.stderr)
            return 1
        return 0
    OUTPUT.write_text(text, encoding="utf-8")
    packs, maps = load()
    print(f"{OUTPUT.relative_to(ROOT)}: {len(packs)} packs, {len(maps)} maps")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
