#!/usr/bin/env python3
"""Makes an asset pack: a few weapons of a Custom Edition map (version 609)
and every tag they use, in a file the game loads beside an Xbox map
(port/linux/game/ce_assets.c), so that a game type can use them there:

    python tools/ce_asset_pack.py list --map maps_ce/grifball.map
    python tools/ce_asset_pack.py deps --map maps_ce/grifball.map \\
        --weapon "weapons\\grifball\\grifball"
    python tools/ce_asset_pack.py build --map maps_ce/grifball.map \\
        --out maps_ce/grifball_assets.map \\
        --weapon "h3mt\\weapons\\covenant\\gravity_hammer\\gravity_hammer" \\
        --weapon "weapons\\energy_sword\\energy_sword backup" \\
        --ball "weapons\\grifball\\grifball" \\
        --label "h3mt\\weapons\\covenant\\gravity_hammer\\gravity_hammer=ar" \\
        --label "weapons\\energy_sword\\energy_sword backup=hp"

list: the map's weapons. deps: a weapon's tags, by group, with where each
    one's data is (the map's tags, or the resource maps). build: the pack.

The tags stay where the map's tool put them, at their own addresses in
Custom Edition's tag cache (0x40440000 on), so that nothing in them has to
be moved: the pack holds the bytes of the tags it keeps (each tag's root and
blocks lie together, from its address to the next tag's), the tag table
(the tags it does not keep left with no group and no data) and the tags'
names, the map's model data (the gbxmodels' vertices and triangles), and
the pixels and samples of its bitmaps and sounds that the map holds itself,
their offsets made the pack's. A bitmap or sound whose data is in
bitmaps.map or sounds.map is read from there, as the map's are.

The game copies the bytes back where they were, makes them its own tags as
it makes a Custom Edition map's (models, shaders, sounds, HUD), adds the
kept ones to the loaded map's tag table and gives every reference to one its
new index. Which tag is which weapon the pack says (its roles).

A weapon is held by its label, which the player's biped's animation graph
must have (unit_can_use_weapon): a map's own biped may have labels the Xbox
cyborg's graph has not (the Grifball court's hammer is "gh", its sword "es"),
so --label gives a weapon one it has, in the pack (the assault rifle's "ar",
held in both hands, the pistol's "hp", in one).

What a tag uses is found by its bytes: any 32 bits of them that are another
tag's index (tag references and tag indices). A number that only looks like
one adds a tag that is not needed, never leaves one out.

The pack (all little-endian; 64-byte header):
    0   'pkac' (b"capk")    signature
    4   1                    version
    8   the map's version (609)
    12  the tag cache's address (0x40440000)
    16  the tags' size (the map's tag data: what the tags span)
    20  the tag table's address, 24 its count
    28  the chunks' count, 32 their offset: (address, size, offset) each,
        the tag cache's bytes in the file
    36  the model data's offset, 40 its size, 44 its vertices' size
    48  the roles' count, 52 their offset: (role, tag index) each:
        1 a weapon players get, the first in their hands; 2 the ball
    56  the map's checksum (its header's), 60 the pack's size

The pack is the map's own data, made smaller: it is shared only as the map
may be.
"""

import argparse
import struct
import sys

SIGNATURE = b"capk"
VERSION = 1
HEADER_SIZE = 64
CE_VERSION = 609
TAG_CACHE_BASE = 0x40440000
TAG_INSTANCE_SIZE = 32
NONE = 0xFFFFFFFF
SECTOR = 512

ROLE_WEAPON = 1
ROLE_BALL = 2

# a weapon's label (weapon_definitions.h)
WEAPON_LABEL = 0x30C
WEAPON_LABEL_SIZE = 32

# a bitmap group's bitmaps block, and a bitmap's flags, pixels' offset and
# size (bitmap_group.h); Halo PC's "external" flag: its pixels in bitmaps.map
BITMAP_GROUP_BITMAPS = 0x60
BITMAP_DATA_SIZE = 0x30
BITMAP_FLAGS = 0x0E
BITMAP_PIXELS_OFFSET = 0x18
BITMAP_PIXELS_SIZE = 0x1C
BITMAP_EXTERNAL_FLAG = 0x100

# a sound's pitch ranges, a range's permutations, a permutation's samples
# (a tag data: size, flags, file offset) (sound_definitions.h)
SOUND_PITCH_RANGES = 0x98
PITCH_RANGE_SIZE = 0x48
PITCH_RANGE_PERMUTATIONS = 0x3C
PERMUTATION_SIZE = 0x7C
PERMUTATION_SAMPLES = 0x40
TAG_DATA_EXTERNAL_FLAG = 1

# tags a weapon's data names that are never the weapon's own
NOT_FOLLOWED = {"scnr", "sbsp", "matg"}


def group_text(group):
    return group[::-1].decode("latin-1")


class CacheFile:
    """A Custom Edition map's header, tag table and tags."""

    def __init__(self, path):
        with open(path, "rb") as file:
            self.data = file.read()
        data = self.data
        if data[0:4] != b"daeh" or data[0x7FC:0x800] != b"toof":
            sys.exit(f"{path}: not a cache file")
        self.version, self.file_length = struct.unpack_from("<II", data, 4)
        if self.version != CE_VERSION:
            sys.exit(f"{path}: version {self.version}, not Custom Edition's ({CE_VERSION})")
        self.tag_data_offset, self.tag_data_size = struct.unpack_from("<II", data, 0x10)
        self.checksum = struct.unpack_from("<I", data, 0x64)[0]
        (self.instances_address, self.scenario, _, self.tag_count, _, self.model_data_offset, _,
         self.vertex_data_size, self.model_data_size, signature) = struct.unpack_from(
            "<IIIIIIIIII", data, self.tag_data_offset)
        if signature != 0x74616773:
            sys.exit(f"{path}: its tag header is damaged")
        self.end = TAG_CACHE_BASE + self.tag_data_size
        self.tags = []
        for index in range(self.tag_count):
            group, _, _, tag_index, name, base, indexed, _ = struct.unpack_from(
                "<4sIIIIIII", data, self.offset(self.instances_address) + index * TAG_INSTANCE_SIZE)
            self.tags.append({
                "group": group_text(group),
                "id": tag_index,
                "name": self.string(name),
                "name_address": name,
                "base": base,
                "indexed": indexed,
            })
        self.by_id = {tag["id"]: index for index, tag in enumerate(self.tags)}
        # each tag's bytes: from its address to the next tag's (the tool
        # writes a tag's root and blocks together)
        in_tags = sorted((tag["base"], index) for index, tag in enumerate(self.tags)
                         if TAG_CACHE_BASE < tag["base"] < self.end)
        self.extent = {}
        for position, (base, index) in enumerate(in_tags):
            following = in_tags[position + 1][0] if position + 1 < len(in_tags) else self.end
            self.extent[index] = (base, following)
        self.names_start = min(tag["name_address"] for tag in self.tags)
        self.names_end = max(tag["name_address"] + len(tag["name"].encode("latin-1")) + 1 for tag in self.tags)

    def offset(self, address):
        if not TAG_CACHE_BASE <= address < self.end:
            raise ValueError(f"{address:08x} is outside the tags")
        return self.tag_data_offset + address - TAG_CACHE_BASE

    def string(self, address):
        start = self.offset(address)
        return self.data[start:self.data.index(b"\0", start)].decode("latin-1")

    def long(self, address):
        return struct.unpack_from("<I", self.data, self.offset(address))[0]

    def find(self, group, name):
        for index, tag in enumerate(self.tags):
            if tag["group"] == group and tag["name"].lower() == name.lower():
                return index
        sys.exit(f"there is no {group} tag {name}")

    def uses(self, index):
        if index not in self.extent:
            return set()
        base, following = self.extent[index]
        words = struct.unpack_from(f"<{(following - base) // 4}I", self.data, self.offset(base))
        return {self.by_id[word] for word in words if word in self.by_id and self.by_id[word] != index}

    def closure(self, roots):
        kept, stack = set(), list(roots)
        while stack:
            index = stack.pop()
            if index in kept:
                continue
            kept.add(index)
            stack.extend(used for used in self.uses(index) if self.tags[used]["group"] not in NOT_FOLLOWED)
        return kept

    def block(self, address):
        count, elements = struct.unpack_from("<II", self.data, self.offset(address))
        return count, elements

    def raw_fields(self, index):
        """(field address, size field address, external) of each bitmap's
        pixels or sound permutation's samples a tag the map holds has"""
        tag = self.tags[index]
        fields = []
        if index not in self.extent:
            return fields
        if tag["group"] == "bitm":
            count, elements = self.block(tag["base"] + BITMAP_GROUP_BITMAPS)
            for bitmap in range(count):
                at = elements + bitmap * BITMAP_DATA_SIZE
                flags = struct.unpack_from("<H", self.data, self.offset(at + BITMAP_FLAGS))[0]
                fields.append((at + BITMAP_PIXELS_OFFSET, at + BITMAP_PIXELS_SIZE,
                               bool(flags & BITMAP_EXTERNAL_FLAG)))
        elif tag["group"] == "snd!" and not tag["indexed"]:
            count, ranges = self.block(tag["base"] + SOUND_PITCH_RANGES)
            for pitch_range in range(count):
                permutation_count, permutations = self.block(
                    ranges + pitch_range * PITCH_RANGE_SIZE + PITCH_RANGE_PERMUTATIONS)
                for permutation in range(permutation_count):
                    samples = permutations + permutation * PERMUTATION_SIZE + PERMUTATION_SAMPLES
                    flags = self.long(samples + 4)
                    fields.append((samples + 8, samples, bool(flags & TAG_DATA_EXTERNAL_FLAG)))
        return fields


def where(cache, index):
    tag = cache.tags[index]
    if tag["indexed"]:
        resource = {"bitm": "bitmaps.map", "snd!": "sounds.map"}.get(tag["group"], "loc.map")
        return f"{resource} (indexed)"
    raw = cache.raw_fields(index)
    external = sum(1 for _, _, outside in raw if outside)
    if raw and external == len(raw):
        return "map tags, data in bitmaps.map"
    if raw:
        return f"map tags + {len(raw) - external} raw in map"
    return "map tags"


def command_list(arguments):
    cache = CacheFile(arguments.map)
    for index, tag in enumerate(cache.tags):
        if tag["group"] == "weap":
            print(f"{index:5} {tag['name']}")


def command_deps(arguments):
    cache = CacheFile(arguments.map)
    roots = [cache.find("weap", name) for name in arguments.weapon]
    kept = cache.closure(roots)
    size = 0
    for index in sorted(kept, key=lambda index: (cache.tags[index]["group"], cache.tags[index]["name"])):
        base, following = cache.extent.get(index, (0, 0))
        size += following - base
        print(f"{cache.tags[index]['group']:4} {cache.tags[index]['name']:70} {following - base:7}  "
              f"{where(cache, index)}")
    print(f"{len(kept)} tags, {size} bytes of tags")


def merge(ranges):
    merged = []
    for start, end in sorted(ranges):
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return merged


def command_build(arguments):
    cache = CacheFile(arguments.map)
    roles = [(ROLE_WEAPON, cache.find("weap", name)) for name in arguments.weapon]
    if arguments.ball:
        roles.append((ROLE_BALL, cache.find("weap", arguments.ball)))
    kept = cache.closure(index for _, index in roles)

    # the tag cache's bytes: the header, the table, the names, the kept tags
    tags = bytearray(cache.data[cache.tag_data_offset:cache.tag_data_offset + cache.tag_data_size])

    def put(address, value):
        struct.pack_into("<I", tags, address - TAG_CACHE_BASE, value)

    for relabel in arguments.label:
        name, _, label = relabel.rpartition("=")
        if not name or len(label) >= WEAPON_LABEL_SIZE:
            sys.exit(f"--label {relabel}: not <weapon tag>=<label>")
        at = cache.tags[cache.find("weap", name)]["base"] + WEAPON_LABEL - TAG_CACHE_BASE
        tags[at:at + WEAPON_LABEL_SIZE] = label.encode("latin-1").ljust(WEAPON_LABEL_SIZE, b"\0")

    table = cache.instances_address
    for index, tag in enumerate(cache.tags):
        if index not in kept:
            # (no group and no data; its name stays)
            at = table + index * TAG_INSTANCE_SIZE
            for field in (0, 4, 8):
                put(at + field, NONE)
            put(at + 20, 0)
            put(at + 24, 0)
    ranges = [(TAG_CACHE_BASE, table + cache.tag_count * TAG_INSTANCE_SIZE),
              (cache.names_start, cache.names_end)]
    ranges += [cache.extent[index] for index in kept if index in cache.extent]
    chunks = merge(ranges)

    # the raw data the map holds itself, after the rest, each at a sector
    raw = bytearray()
    raw_offsets = {}
    pending = []
    for index in sorted(kept):
        for offset_field, size_field, external in cache.raw_fields(index):
            if external:
                continue
            offset = cache.long(offset_field)
            size = cache.long(size_field)
            if size and offset + size > len(cache.data):
                sys.exit(f"{cache.tags[index]['name']}: its data is past the map's end")
            if (offset, size) not in raw_offsets:
                raw_offsets[(offset, size)] = len(raw)
                raw += cache.data[offset:offset + size]
                raw += bytes(-len(raw) % SECTOR)
            pending.append((offset_field, (offset, size)))

    model = cache.data[cache.model_data_offset:cache.model_data_offset + cache.model_data_size]
    chunk_table_offset = HEADER_SIZE
    roles_offset = chunk_table_offset + 12 * len(chunks)
    chunk_data_offset = roles_offset + 8 * len(roles)
    chunk_offsets = []
    position = chunk_data_offset
    for start, end in chunks:
        chunk_offsets.append(position)
        position += end - start
    model_offset = (position + SECTOR - 1) // SECTOR * SECTOR
    raw_offset = (model_offset + len(model) + SECTOR - 1) // SECTOR * SECTOR
    for field, key in pending:
        put(field, raw_offset + raw_offsets[key])
    total = raw_offset + len(raw) + SECTOR

    out = bytearray(total)
    struct.pack_into("<4sIIIIIIIIIIIIIII", out, 0, SIGNATURE, VERSION, cache.version, TAG_CACHE_BASE,
                     cache.tag_data_size, table, cache.tag_count, len(chunks), chunk_table_offset,
                     model_offset, len(model), cache.vertex_data_size, len(roles), roles_offset,
                     cache.checksum, total)
    for number, (start, end) in enumerate(chunks):
        struct.pack_into("<III", out, chunk_table_offset + 12 * number, start, end - start, chunk_offsets[number])
        out[chunk_offsets[number]:chunk_offsets[number] + end - start] = \
            tags[start - TAG_CACHE_BASE:end - TAG_CACHE_BASE]
    for number, (role, index) in enumerate(roles):
        struct.pack_into("<II", out, roles_offset + 8 * number, role, index)
    out[model_offset:model_offset + len(model)] = model
    out[raw_offset:raw_offset + len(raw)] = raw
    with open(arguments.out, "wb") as file:
        file.write(out)
    print(f"{arguments.out}: {len(kept)} tags in {len(chunks)} chunks "
          f"({sum(end - start for start, end in chunks)} bytes), {len(model)} bytes of model data, "
          f"{len(raw_offsets)} raw blocks ({len(raw)} bytes): {total} bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("list", "deps", "build"):
        command = commands.add_parser(name)
        command.add_argument("--map", required=True, help="the Custom Edition map")
        if name != "list":
            command.add_argument("--weapon", action="append", default=[],
                                 help="a weapon tag players get (the first in their hands)")
        if name == "build":
            command.add_argument("--ball", help="the weapon tag that is the ball")
            command.add_argument("--out", required=True)
            command.add_argument("--label", action="append", default=[],
                                 help="<weapon tag>=<label>: the label a weapon is held by")
    arguments = parser.parse_args()
    if arguments.command == "deps" and not arguments.weapon:
        parser.error("deps needs a --weapon")
    {"list": command_list, "deps": command_deps, "build": command_build}[arguments.command](arguments)


if __name__ == "__main__":
    main()
