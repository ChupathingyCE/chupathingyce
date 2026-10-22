#!/usr/bin/env python3
"""Compares the simulation data of two cache files of one stock multiplayer
map: Halo PC retail's (cache version 7) and Custom Edition's (609).

    compare_stock_maps.py PC.map CE.map           one pair
    compare_stock_maps.py --folders PC_DIR CE_DIR every map both folders have;
                                                  prints map_stock_pairs.h rows
                                                  for the pairs that match

A pair matches when both files have the same tags (class and path) and, for
the tags the simulation reads (scenario, objects, units, weapons, vehicles,
projectiles, damage effects, collision, physics, animations, globals, model
markers) and the structure BSPs, the same bytes once the file's own addresses
and tag identifiers are set aside (they differ with the layout of the rest of
the file). Sounds, bitmaps, shaders, UI and the rendering data are not
compared. See docs/stock-map-pairs.md.
"""
import hashlib
import struct
import sys
from pathlib import Path

TAG_BASE = 0x40440000
HEADER_SIZE = 0x800
SIMULATION_CLASSES = {
    "scnr", "sbsp", "matg", "bipd", "vehi", "weap", "eqip", "garb", "proj", "scen", "mach", "ctrl", "lifi",
    "plac", "ssce", "devi", "obje", "unit", "item", "itmc", "coll", "phys", "antr", "jpt!", "mod2", "hlmt",
    "hmt ", "grhi", "pphy", "lsnd", "trak", "sgp!", "foot", "ant!", "devc", "ligh", "tagc", "mply", "ngpr",
    "nhdt", "wphi", "unhi", "grhi",
}


class CacheError(Exception):
    pass


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def class_name(raw):
    return raw[::-1].decode("latin-1")


def load(path):
    data = Path(path).read_bytes()
    if len(data) < HEADER_SIZE or data[:4] != b"daeh":
        raise CacheError(f"{path}: not a cache file")
    version = u32(data, 4)
    if version not in (7, 609):
        raise CacheError(f"{path}: cache version {version}")
    tag_offset, tag_size = u32(data, 0x10), u32(data, 0x14)
    if tag_offset + tag_size > len(data):
        raise CacheError(f"{path}: tag data past the file")
    return data, version, tag_offset, tag_size


def read_tags(data, tag_offset, tag_size):
    index = tag_offset
    array_address, scenario_id, _, count = struct.unpack_from("<IIII", data, index)
    tags = []
    base = index + (array_address - TAG_BASE)
    for number in range(count):
        entry = data[base + 32 * number: base + 32 * number + 32]
        cls = class_name(entry[0:4])
        identifier, name_address, data_address, indexed = struct.unpack_from("<IIII", entry, 12)
        name_offset = tag_offset + name_address - TAG_BASE
        end = data.index(b"\0", name_offset)
        tags.append({"class": cls, "id": identifier, "name": data[name_offset:end].decode("latin-1"),
                     "name_address": name_address, "address": data_address, "indexed": indexed})
    return tags, scenario_id


def normalize(chunk, ranges, identifiers):
    """The chunk with every dword that is an address in ranges or a tag
    identifier replaced by a fixed token"""
    out = bytearray(chunk)
    for position in range(0, len(out) - 3, 4):
        value = struct.unpack_from("<I", out, position)[0]
        if value in identifiers or any(low <= value < high for low, high in ranges):
            struct.pack_into("<I", out, position, 0xFFFFFFFE)
    return bytes(out)


def simulation_digests(path):
    data, version, tag_offset, tag_size = load(path)
    tags, scenario_id = read_tags(data, tag_offset, tag_size)
    identifiers = {tag["id"] for tag in tags}
    ranges = [(TAG_BASE, TAG_BASE + tag_size)]
    # (a tag's data runs to the next tag's data or name)
    boundaries = sorted({tag["address"] for tag in tags if tag["address"] >= TAG_BASE} |
                        {tag["name_address"] for tag in tags} | {TAG_BASE + tag_size})
    result = {}
    scenario = None
    for tag in tags:
        if tag["id"] == scenario_id:
            scenario = tag
        if tag["class"] not in SIMULATION_CLASSES or tag["class"] == "sbsp" or tag["indexed"] or \
                tag["address"] < TAG_BASE or tag["address"] >= TAG_BASE + tag_size:
            continue
        following = [b for b in boundaries if b > tag["address"]]
        end = following[0] if following else TAG_BASE + tag_size
        start = tag_offset + tag["address"] - TAG_BASE
        chunk = data[start: tag_offset + end - TAG_BASE]
        result[(tag["class"], tag["name"])] = hashlib.sha256(normalize(chunk, ranges, identifiers)).hexdigest()
    # (the structure BSPs: the scenario's block of them, 32 bytes each,
    # whose file offset, size and address are the BSP's own)
    if scenario is not None:
        scnr = tag_offset + scenario["address"] - TAG_BASE
        bsp_count, bsp_address = struct.unpack_from("<II", data, scnr + 0x5A4)
        block = tag_offset + bsp_address - TAG_BASE
        bsp_tags = [tag for tag in tags if tag["class"] == "sbsp"]
        for number in range(bsp_count):
            start, size, address = struct.unpack_from("<III", data, block + 32 * number)
            if start + size > len(data):
                raise CacheError(f"{path}: BSP past the file")
            name = bsp_tags[number]["name"] if number < len(bsp_tags) else f"bsp{number}"
            normalized = normalize(data[start: start + size], [(address, address + size), *ranges], identifiers)
            result[("sbsp", name)] = hashlib.sha256(normalized).hexdigest()
    names = sorted((tag["class"], tag["name"]) for tag in tags)
    return version, names, result


def file_version(path):
    """The version a host of network version 29 sends for the file: the first
    32 bits of its whole-file hash (map_archive.c), 1 for 0"""
    data = Path(path).read_bytes()
    piece_size = 0x40000
    whole = hashlib.blake2b(digest_size=32)
    whole.update(b"OpenCE map 1" + struct.pack("<I", len(data) & 0xFFFFFFFF))
    for offset in range(0, len(data), piece_size):
        whole.update(hashlib.blake2b(data[offset: offset + piece_size], digest_size=32).digest())
    return int.from_bytes(whole.digest()[:4], "big") or 1


def compare(pc_path, ce_path):
    """(whether they match, what differs)"""
    pc_version, pc_names, pc_digests = simulation_digests(pc_path)
    ce_version, ce_names, ce_digests = simulation_digests(ce_path)
    if (pc_version, ce_version) != (7, 609):
        return False, [f"versions {pc_version} and {ce_version}, not 7 and 609"]
    problems = []
    if pc_names != ce_names:
        only_pc = sorted(set(pc_names) - set(ce_names))
        only_ce = sorted(set(ce_names) - set(pc_names))
        problems.append(f"tags differ: {len(only_pc)} only in Halo PC {only_pc[:3]}, "
                        f"{len(only_ce)} only in Custom Edition {only_ce[:3]}")
    for key in sorted(set(pc_digests) & set(ce_digests)):
        if pc_digests[key] != ce_digests[key]:
            problems.append(f"{key[0]} {key[1]} differs")
    if not pc_digests:
        problems.append("no simulation data found")
    return not problems, problems


def main(argv):
    if len(argv) == 4 and argv[1] == "--folders":
        pc_dir, ce_dir = Path(argv[2]), Path(argv[3])
        for pc in sorted(pc_dir.glob("*.map")):
            ce = ce_dir / pc.name
            if not ce.exists():
                continue
            try:
                same, problems = compare(pc, ce)
            except CacheError as error:
                same, problems = False, [str(error)]
            if same:
                print(f'\t{{ "{pc.stem}", 0x{file_version(pc):08x}, 0x{file_version(ce):08x} }},')
            else:
                print(f"/* {pc.stem}: not interchangeable: {problems[0]} */")
        return 0
    if len(argv) == 3:
        same, problems = compare(argv[1], argv[2])
        print("same simulation data" if same else "differs:\n  " + "\n  ".join(problems[:20]))
        print(f"Halo PC version {file_version(argv[1]):08x}, Custom Edition version {file_version(argv[2]):08x}")
        return 0 if same else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
