"""Custom Edition cache files (version 609), read and written, for
tools/ce_asset_pack.py and tools/map_patch.py.

CacheFile reads a map: its header, its tag table and its tags. Builder makes
one: from a map (its tags, model data, BSPs and raw data kept where they
are) or from nothing (a stub scenario), and adds tags of other maps to it.

How a tag is added (Builder.add):
  - a tag of the same group and name as one the map has, and of its size,
    is the map's (a stock tag both have);
  - else its bytes (from its address to the next tag's: the tools write a
    tag's root and blocks together) go after the map's tags, and every 32
    bits of them that point into those bytes move with them; every 32 bits
    that are another tag's index (a tag reference's or a tag index) become
    that tag's new one, and a reference's name pointer its new name's (an
    index of a tag not added becomes none);
  - the pixels and samples the source map holds itself (not those in
    bitmaps.map or sounds.map) are copied in, and their offsets made the new
    file's;
  - a gbxmodel's parts' vertices and triangles are copied into the model
    data, after the map's, and their offsets made theirs.
What a tag uses is found the same way (CacheFile.closure). A number in a
tag that only looks like a pointer or a tag index is never checked against
the tag's layout; tools/map_patch.py's maps are checked as every Custom
Edition map is when the game opens them (port/linux/game/ce_map_checks.c).

Everything is little-endian and written in one order, so the same inputs
make the same bytes.
"""

import struct
import sys
import zlib

CE_VERSION = 609
TAG_CACHE_BASE = 0x40440000
HEADER_SIZE = 0x800
TAG_HEADER_SIZE = 0x28
INSTANCE_SIZE = 32
NONE = 0xFFFFFFFF
BUILD = b"01.00.00.0609"
MULTIPLAYER = 1

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
# a gbxmodel's geometries, a geometry's parts, a part's triangles' and
# vertices' counts and offsets in the model data (ce_models.c)
MODEL_GEOMETRIES = 0xD0
GEOMETRY_SIZE = 0x30
GEOMETRY_PARTS = 0x24
CE_PART_SIZE = 0x84
PART_TRIANGLES = 0x44
PART_VERTICES = 0x54
CE_VERTEX_SIZE = 0x44
# a scenario's root (scenario.h)
SCENARIO_SIZE = 0x5B0
# a weapon's label (weapon_definitions.h)
WEAPON_LABEL = 0x30C
WEAPON_LABEL_SIZE = 32

# tags a weapon's data names that are never the weapon's own
NOT_FOLLOWED = {"scnr", "sbsp", "matg"}


def group_text(group):
    return struct.pack(">I", group).decode("latin-1")


def group_tag(text):
    return struct.unpack(">I", text.encode("latin-1"))[0]


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


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
        self.name = data[0x20:0x40].split(b"\0")[0].decode("latin-1")
        (self.instances_address, self.scenario, self.random, self.tag_count, self.model_part_count,
         self.model_data_offset, _, self.vertex_data_size, self.model_data_size, signature) = struct.unpack_from(
            "<IIIIIIIIII", data, self.tag_data_offset)
        if signature != 0x74616773:
            sys.exit(f"{path}: its tag header is damaged")
        self.end = TAG_CACHE_BASE + self.tag_data_size
        self.tags = []
        for index in range(self.tag_count):
            group, parent0, parent1, tag_id, name, base, indexed, _ = struct.unpack_from(
                "<IIIIIIII", data, self.offset(self.instances_address) + index * INSTANCE_SIZE)
            self.tags.append({"group": group_text(group), "group_tag": group, "parents": (parent0, parent1),
                              "id": tag_id, "name": self.string(name), "name_address": name, "base": base,
                              "indexed": indexed})
        self.by_id = {tag["id"]: index for index, tag in enumerate(self.tags)}
        self.by_name_address = {tag["name_address"]: index for index, tag in enumerate(self.tags)}
        # each tag's bytes: from its address to the next tag's, or to the
        # tag table or the names if they come next
        starts = sorted([(tag["base"], index) for index, tag in enumerate(self.tags)
                         if TAG_CACHE_BASE < tag["base"] < self.end] +
                        [(self.instances_address, None)] +
                        [(tag["name_address"], None) for tag in self.tags])
        self.extent = {}
        for position, (base, index) in enumerate(starts):
            if index is None:
                continue
            following = next((address for address, _ in starts[position + 1:] if address > base), self.end)
            self.extent[index] = (base, following)

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
        sys.exit(f"{self.name} has no {group} tag {name}")

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

    def raw_fields(self, index):
        """(offset field, size field, external) of each bitmap's pixels and
        each sound permutation's samples of a tag the map holds"""
        tag = self.tags[index]
        fields = []
        if index not in self.extent:
            return fields
        if tag["group"] == "bitm":
            count, elements = self.long(tag["base"] + BITMAP_GROUP_BITMAPS), self.long(tag["base"] + BITMAP_GROUP_BITMAPS + 4)
            for bitmap in range(count):
                at = elements + bitmap * BITMAP_DATA_SIZE
                flags = struct.unpack_from("<H", self.data, self.offset(at + BITMAP_FLAGS))[0]
                fields.append((at + BITMAP_PIXELS_OFFSET, at + BITMAP_PIXELS_SIZE, bool(flags & BITMAP_EXTERNAL_FLAG)))
        elif tag["group"] == "snd!" and not tag["indexed"]:
            count, ranges = self.long(tag["base"] + SOUND_PITCH_RANGES), self.long(tag["base"] + SOUND_PITCH_RANGES + 4)
            for pitch_range in range(count):
                at = ranges + pitch_range * PITCH_RANGE_SIZE + PITCH_RANGE_PERMUTATIONS
                permutation_count, permutations = self.long(at), self.long(at + 4)
                for permutation in range(permutation_count):
                    samples = permutations + permutation * PERMUTATION_SIZE + PERMUTATION_SAMPLES
                    fields.append((samples + 8, samples, bool(self.long(samples + 4) & TAG_DATA_EXTERNAL_FLAG)))
        return fields


class Builder:
    """A Custom Edition map being made."""

    def __init__(self, base=None):
        if base:
            self.header = bytearray(base.data[:HEADER_SIZE])
            # (its BSPs and raw data stay where they are)
            self.prefix = bytearray(base.data[HEADER_SIZE:base.model_data_offset])
            self.tags = bytearray(base.data[base.tag_data_offset:base.tag_data_offset + base.tag_data_size])
            model = base.data[base.model_data_offset:base.model_data_offset + base.model_data_size]
            self.vertices = bytearray(model[:base.vertex_data_size])
            self.triangles = bytearray(model[base.vertex_data_size:])
            self.instances = [struct.pack("<IIIIIIII", tag["group_tag"], tag["parents"][0], tag["parents"][1],
                                          tag["id"], tag["name_address"], tag["base"], tag["indexed"], 0)
                              for tag in base.tags]
            self.names = {(tag["group"], tag["name"].lower()): index for index, tag in enumerate(base.tags)}
            self.sizes = {index: base.extent[index][1] - base.extent[index][0] if index in base.extent else 0
                          for index in range(base.tag_count)}
            self.indexed = [tag["indexed"] for tag in base.tags]
            self.ids = [tag["id"] for tag in base.tags]
            self.name_addresses = [tag["name_address"] for tag in base.tags]
            self.scenario = base.scenario
            self.random = base.random
            self.parts = base.model_part_count
        else:
            self.header = bytearray(HEADER_SIZE)
            self.header[0:4] = b"daeh"
            self.header[0x7FC:0x800] = b"toof"
            struct.pack_into("<I", self.header, 4, CE_VERSION)
            self.header[0x40:0x40 + len(BUILD)] = BUILD
            struct.pack_into("<H", self.header, 0x60, MULTIPLAYER)
            self.prefix = bytearray()
            self.tags = bytearray(TAG_HEADER_SIZE)
            self.vertices, self.triangles = bytearray(), bytearray()
            self.instances, self.names, self.sizes, self.indexed, self.ids, self.name_addresses = [], {}, {}, [], [], []
            self.scenario, self.random, self.parts = NONE, 0, 0
        self.salt = (self.ids[0] >> 16) if self.ids else 0xE174

    def get(self, address):
        return struct.unpack_from("<I", self.tags, address - TAG_CACHE_BASE)[0]

    def put(self, address, value):
        struct.pack_into("<I", self.tags, address - TAG_CACHE_BASE, value)

    def append_tags(self, data, alignment=4):
        self.tags += bytes(align(len(self.tags), alignment) - len(self.tags))
        address = TAG_CACHE_BASE + len(self.tags)
        self.tags += data
        return address

    def new_instance(self, group, parents, name, base_address, indexed, size):
        index = len(self.instances)
        tag_id = (((self.salt + index) & 0xFFFF) << 16) | index
        name_address = self.append_tags(name.encode("latin-1") + b"\0", 1)
        self.instances.append(struct.pack("<IIIIIIII", group, parents[0], parents[1], tag_id, name_address,
                                          base_address, indexed, 0))
        self.names[(group_text(group), name.lower())] = index
        self.sizes[index] = size
        self.indexed.append(indexed)
        self.ids.append(tag_id)
        self.name_addresses.append(name_address)
        return index

    def add_stub_scenario(self, name):
        """an empty scenario (no BSPs, no blocks, no references), which a
        map must have for the tools to open it"""
        index = self.new_instance(group_tag("scnr"), (NONE, NONE), name, 0, 0, SCENARIO_SIZE)
        root = bytearray(SCENARIO_SIZE)
        # (its root's three references first: none)
        for reference in range(3):
            struct.pack_into("<IIII", root, reference * 16, NONE, 0, 0, NONE)
        address = self.append_tags(root, 4)
        self.instances[index] = self.instances[index][:20] + struct.pack("<I", address) + self.instances[index][24:]
        self.scenario = self.ids[index]
        return index

    def add(self, source, indices):
        """the source map's tags (indices) added (see the module's text):
        {source index: index here}"""
        mapping, moved = {}, {}
        for index in sorted(indices):
            tag = source.tags[index]
            size = source.extent[index][1] - source.extent[index][0] if index in source.extent else 0
            same = self.names.get((tag["group"], tag["name"].lower()))
            if same is not None and self.sizes.get(same) == size and bool(self.indexed[same]) == bool(tag["indexed"]):
                mapping[index] = same
                continue
            if same is not None:
                sys.exit(f"{tag['group']} {tag['name']}: another tag of its name is here already")
            # (an indexed bitmap's base is its resource's index)
            mapping[index] = self.new_instance(tag["group_tag"], tag["parents"], tag["name"], tag["base"],
                                               tag["indexed"], size)
            if index in source.extent:
                start, end = source.extent[index]
                address = self.append_tags(source.data[source.offset(start):source.offset(end)], 4)
                moved[index] = address - start
                self.set_instance_base(mapping[index], address)

        def new_id(old):
            used = source.by_id.get(old)
            if used is None or used not in mapping:
                return NONE, 0
            return self.ids[mapping[used]], self.name_addresses[mapping[used]]

        for index, delta in moved.items():
            start, end = source.extent[index]
            for old in range(start, end, 4):
                address = old + delta
                value = self.get(address)
                if start <= value < end:
                    self.put(address, value + delta)
                elif value in source.by_id:
                    tag_id, name_address = new_id(value)
                    self.put(address, tag_id)
                    # (a tag reference: group, name, name length, index)
                    if old - 8 >= start and self.get(address - 8) == source.tags[source.by_id[value]]["name_address"]:
                        self.put(address - 8, name_address)
            # its pixels and samples
            for offset_field, size_field, external in source.raw_fields(index):
                if external:
                    continue
                offset, size = source.long(offset_field), source.long(size_field)
                self.prefix += bytes(align(len(self.prefix), 16) - len(self.prefix))
                self.put(offset_field + delta, HEADER_SIZE + len(self.prefix))
                self.prefix += source.data[offset:offset + size]
            # its gbxmodel's vertices and triangles
            if source.tags[index]["group"] == "mod2":
                self.add_model(source, index, delta)
        return mapping

    def instance_base(self, index):
        return struct.unpack_from("<I", self.instances[index], 20)[0]

    def set_instance_base(self, index, address):
        self.instances[index] = self.instances[index][:20] + struct.pack("<I", address) + self.instances[index][24:]

    def add_model(self, source, index, delta):
        model = source.data[source.model_data_offset:source.model_data_offset + source.model_data_size]
        root = source.tags[index]["base"] + delta
        geometry_count, geometries = self.get(root + MODEL_GEOMETRIES), self.get(root + MODEL_GEOMETRIES + 4)
        for geometry in range(geometry_count):
            at = geometries + geometry * GEOMETRY_SIZE + GEOMETRY_PARTS
            part_count, parts = self.get(at), self.get(at + 4)
            for part in range(part_count):
                at = parts + part * CE_PART_SIZE
                vertex_count = self.get(at + PART_VERTICES + 4)
                vertex_offset = self.get(at + PART_VERTICES + 16)
                # (a strip of count + 2 indices of 2 bytes)
                triangle_count = self.get(at + PART_TRIANGLES + 4)
                triangle_offset = self.get(at + PART_TRIANGLES + 8)
                triangle_size = (triangle_count + 2) * 2
                self.put(at + PART_VERTICES + 16, len(self.vertices))
                self.vertices += model[vertex_offset:vertex_offset + vertex_count * CE_VERTEX_SIZE]
                self.put(at + PART_TRIANGLES + 8, len(self.triangles))
                triangles = source.vertex_data_size + triangle_offset
                self.triangles += model[triangles:triangles + triangle_size]
                self.triangles += bytes(align(len(self.triangles), 4) - len(self.triangles))
                self.parts += 1

    def relabel(self, index, label):
        at = self.instance_base(index) + WEAPON_LABEL - TAG_CACHE_BASE
        self.tags[at:at + WEAPON_LABEL_SIZE] = label.encode("latin-1").ljust(WEAPON_LABEL_SIZE, b"\0")

    def write(self, path, name, text=None):
        """the map, named name; text: the header's free space from 0x400
        (map_patch.py)"""
        table_address = self.append_tags(b"".join(self.instances), 16)
        prefix = self.prefix + bytes(align(len(self.prefix), 16) - len(self.prefix))
        model_offset = HEADER_SIZE + len(prefix)
        model = self.vertices + self.triangles
        tag_data_offset = model_offset + len(model)
        struct.pack_into("<IIIIIIIIII", self.tags, 0, table_address, self.scenario, self.random,
                         len(self.instances), self.parts, model_offset, self.parts, len(self.vertices), len(model),
                         0x74616773)
        data = self.header + prefix + model + self.tags
        struct.pack_into("<I", data, 8, len(data))
        struct.pack_into("<II", data, 0x10, tag_data_offset, len(self.tags))
        data[0x20:0x40] = name.encode("latin-1")[:31].ljust(32, b"\0")
        if text is not None:
            encoded = text.encode("latin-1")
            if len(encoded) >= 0x3F0:
                sys.exit("the text is too long for the header")
            data[0x400:0x7F0] = encoded.ljust(0x3F0, b"\0")
        struct.pack_into("<I", data, 0x64, zlib.crc32(data[HEADER_SIZE:]))
        with open(path, "wb") as file:
            file.write(data)
        return data
