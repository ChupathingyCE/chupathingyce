"""SPV3 (OpenSauce) maps surveyed offline, without running the game.

Reads each Custom Edition cache (version 609) given, .yelo or .map, and
writes what SPV3 mode logs at a map load (port/linux/game/spv3_mode.c), so a
player's install can be checked, and the work it needs ranked, from the
files alone:

- the cache header and OpenSauce's yelo header (at 0x70) and its flags;
- the tag data's size and the tag count against the stock and OpenSauce
  limits, and against the game's;
- every tag group with its count, stock or OpenSauce or unknown;
- the scenario's structure BSPs: their sizes and the addresses they are
  linked at, against the tag window;
- every script function and global the scenario's scripts name that the
  game's tables lack (read from this source tree), with the number of calls;
- the indexed bitmaps and sounds against the resource maps beside the maps.

Nothing is written to the maps. Formats: Halo's cache files (the game's own
cache_files.c and scenario_definitions.h); OpenSauce's header, groups and
limits as OpenSauce (Kornner Studios, GPL-3.0) defines them
(cache_files_structures_yelo.hpp, blam_memory_upgrades.hpp): facts only, no
code of theirs.

    python3 tools/spv3_scan.py <maps folder or files...> [--tsv out.tsv] [--md out.md]
"""

import argparse
import collections
import os
import re
import struct
import sys
from pathlib import Path

CE_TAG_BASE = 0x40440000
STOCK_TAG_CACHE_SIZE = 0x01700000
OPENSAUCE_TAG_CACHE_SIZE = 0x02280000
# the game's own window (platform.h): the tag cache and the conversion room
GAME_TAG_CACHE_SIZE = 0x01700000
GAME_CONVERSION_ROOM = 0x004C0000
STOCK_TAG_COUNT = 5120
OPENSAUCE_TAG_COUNT = 7680
STOCK_BSPS = 16

YELO_FLAGS = ["uses_memory_upgrades", "uses_mod_data_files", "is_protected", "uses_game_state_upgrades",
              "has_compression_params"]

# Halo PC / Custom Edition's tag groups
STOCK_GROUPS = set("""actr actv ant! antr bipd bitm boom cdmg coll colo cont ctrl deca DeLa devc devi dobc effe
elec eqip flag fog  font foot garb glw! grhi hmt  hud# hudg item itmc jpt! lens lifi ligh lsnd mach matg metr
mgs2 mod2 mode mply ngpr obje part pctl phys plac pphy proj rain sbsp scen scex schi scnr senv sgla shdr sky
smet snd! snde soso sotr Soul spla ssce str# swat tagc trak udlg unhi unic unit ustr vcky vehi weap wind wphi
""".replace("fog  ", "fog| ").replace("hmt  ", "hmt| ").replace("sky\n", "sky|\n").split())
STOCK_GROUPS = {g.replace("|", " ") for g in STOCK_GROUPS}
# OpenSauce's groups (their names, for the survey)
OPENSAUCE_GROUPS = {
    "yelo": "project_yellow", "gelo": "project_yellow_globals", "gelc": "project_yellow_globals_cv",
    "shpp": "shader_postprocess", "shpg": "shader_postprocess_generic", "sppg": "shader_postprocess_globals",
    "efpp": "effect_postprocess", "efpc": "effect_postprocess_collection", "efpg": "effect_postprocess_generic",
    "avtc": "actor_variant_transform_collection", "avti": "actor_variant_transform_in",
    "avto": "actor_variant_transform_out", "magy": "model_animations_yelo", "eqhi": "equipment_hud_interface",
    "sidy": "string_id_yelo", "sily": "text_value_pair_definition", "tag+": "tag_database",
    "memg": "cache_memory_gestalt", "meml": "cache_memory_layout_table", "unit_transform": "unit_transform",
}

SCENARIO_SCRIPT_SYNTAX = 0x474
SCENARIO_SCRIPT_STRINGS = 0x488
SCENARIO_SCRIPTS = 0x49C
SCENARIO_GLOBALS = 0x4A8
SCENARIO_BSPS = 0x5A4
SYNTAX_NODE_SIZE = 0x14
PRIMITIVE, SCRIPT, VARIABLE = 1, 2, 4


def fourcc(value):
    return struct.pack(">I", value).decode("latin-1")


def game_tables(source):
    """the script function and external global names this source tree has"""
    functions, globals_ = set(), set()
    for path in list((source / "source/hs").glob("*.c")) + list((source / "port/linux/game").glob("*.c")):
        if path.name.startswith("stubbs"):
            continue
        text = path.read_text(errors="replace")
        functions.update(re.findall(r'hs_function_definition\w*\s+const\s+\w+\s*=\s*\{[^"]*?"([^"]+)"', text))
        globals_.update(re.findall(r'hs_external_global_definition\s+\w+\s*=\s*\{\s*"([^"]+)"', text))
    return functions, globals_


class Cache:
    def __init__(self, path):
        self.path = path
        self.file = open(path, "rb")
        self.size = os.path.getsize(path)
        header = self.read(0, 0x800)
        self.signature = header[0:4][::-1].decode("latin-1")
        (self.version, self.file_length, _, self.tag_data_offset, self.tag_data_size) = struct.unpack_from("<5I", header, 4)
        self.name = header[0x20:0x40].split(b"\0")[0].decode("latin-1")
        self.build = header[0x40:0x60].split(b"\0")[0].decode("latin-1")
        self.cache_type = struct.unpack_from("<I", header, 0x60)[0]
        yelo = header[0x70:0x170]
        self.yelo = yelo[0:4] == b"oley"
        self.yelo_version, self.yelo_flags = struct.unpack_from("<hH", yelo, 4) if self.yelo else (0, 0)
        self.yelo_upgrade = struct.unpack_from("<f", yelo, 0x0C)[0] if self.yelo else 0.0
        self.yelo_mod = yelo[0x40:0x60].split(b"\0")[0].decode("latin-1") if self.yelo else ""
        self.tags = self.read(self.tag_data_offset, self.tag_data_size)

    def read(self, offset, size):
        self.file.seek(offset)
        return self.file.read(size)

    def at(self, address, size):
        offset = address - CE_TAG_BASE
        if address < CE_TAG_BASE or offset + size > len(self.tags):
            return None
        return self.tags[offset:offset + size]


def resource_map(path):
    """{index: path} and {path} of a CE resource map, or None"""
    if not path.exists():
        return None
    with open(path, "rb") as f:
        kind, paths, resources, count = struct.unpack("<4I", f.read(16))
        f.seek(resources)
        table = f.read(count * 12)
        f.seek(paths)
        names = f.read(max(0, resources - paths))
    entries = {}
    for index in range(count):
        name_offset = struct.unpack_from("<I", table, index * 12)[0]
        end = names.find(b"\0", name_offset)
        entries[index] = names[name_offset:end].decode("latin-1")
    return entries


def scan(path, functions, globals_, resources):
    cache = Cache(path)
    r = {"map": path.name, "file_size": cache.size, "signature": cache.signature, "version": cache.version,
         "file_length": cache.file_length, "name": cache.name, "build": cache.build, "cache_type": cache.cache_type,
         "yelo": cache.yelo, "yelo_version": cache.yelo_version, "yelo_flags": cache.yelo_flags,
         "yelo_flag_names": [n for i, n in enumerate(YELO_FLAGS) if cache.yelo_flags & (1 << i)],
         "yelo_upgrade": cache.yelo_upgrade, "yelo_mod": cache.yelo_mod, "tag_data_size": cache.tag_data_size}
    tags = cache.tags
    instances, scenario_index, _, tag_count = struct.unpack_from("<4I", tags, 0)
    r["tag_count"] = tag_count
    r["tag_instances_address"] = instances
    groups = collections.Counter()
    by_index = []
    for index in range(tag_count):
        raw = cache.at(instances + index * 0x20, 0x20)
        group, parent, grand, handle, name, data, indexed, _ = struct.unpack("<8I", raw)
        g = fourcc(group)
        groups[g] += 1
        tag_name = ""
        name_raw = cache.at(name, 256)
        if name_raw:
            tag_name = name_raw.split(b"\0")[0].decode("latin-1")
        by_index.append((g, tag_name, data, indexed))
    r["groups"] = dict(groups)
    r["groups_unknown"] = {g: c for g, c in groups.items() if g not in STOCK_GROUPS}
    # resources: indexed bitmaps (by index) and sounds (by path)
    misses = collections.Counter()
    looked = collections.Counter()
    if resources:
        bitmaps, sounds = resources.get("bitmaps"), resources.get("sounds")
        sound_paths = set(sounds.values()) if sounds else set()
        for g, tag_name, data, indexed in by_index:
            if not indexed:
                continue
            if g == "bitm":
                looked["bitm"] += 1
                if bitmaps is None or data not in bitmaps:
                    misses["bitm"] += 1
            elif g == "snd!":
                looked["snd!"] += 1
                if tag_name not in sound_paths:
                    misses["snd!"] += 1
    r["resources_looked_up"] = dict(looked)
    r["resources_missing"] = dict(misses)
    # scenario
    scenario_group, _, scenario_data, _ = by_index[scenario_index & 0xFFFF]
    r["scenario_group"] = scenario_group
    scenario = cache.at(scenario_data, 0x5B0)
    first_reference = struct.unpack_from("<I", scenario, 0)[0]
    r["scenario_yelo_reference"] = fourcc(first_reference) if first_reference not in (0, 0xFFFFFFFF) else ""
    bsp_count, bsp_address = struct.unpack_from("<iI", scenario, SCENARIO_BSPS)
    bsps = []
    for index in range(max(0, bsp_count)):
        raw = cache.at(bsp_address + index * 0x20, 0x20)
        file_offset, size, base, _, group, name, _, tag_index = struct.unpack("<8I", raw)
        bsps.append({"name": by_index[tag_index & 0xFFFF][1], "size": size, "base": base, "end": base + size})
    r["bsps"] = bsps
    r["bsp_count"] = len(bsps)
    r["bsp_lowest_base"] = min((b["base"] for b in bsps), default=0)
    r["bsp_highest_end"] = max((b["end"] for b in bsps), default=0)
    r["bsp_largest"] = max((b["size"] for b in bsps), default=0)
    # scripts
    syntax_size, _, _, syntax_address = struct.unpack_from("<4I", scenario, SCENARIO_SCRIPT_SYNTAX)
    strings_size, _, _, strings_address = struct.unpack_from("<4I", scenario, SCENARIO_SCRIPT_STRINGS)
    scripts_count = struct.unpack_from("<i", scenario, SCENARIO_SCRIPTS)[0]
    globals_count, globals_address = struct.unpack_from("<iI", scenario, SCENARIO_GLOBALS)
    syntax = cache.at(syntax_address, syntax_size) or b""
    strings = cache.at(strings_address, strings_size) or b""
    scenario_globals = set()
    for index in range(max(0, globals_count)):
        raw = cache.at(globals_address + index * 0x5C, 0x5C)
        if raw:
            scenario_globals.add(raw[:32].split(b"\0")[0].decode("latin-1"))

    def string(offset):
        if offset < 0 or offset >= len(strings):
            return ""
        return strings[offset:strings.find(b"\0", offset)].decode("latin-1")

    calls = collections.Counter()
    global_uses = collections.Counter()
    nodes = []
    if len(syntax) >= 0x38:
        maximum, element = struct.unpack_from("<hh", syntax, 0x20)
        element = element or SYNTAX_NODE_SIZE
        for index in range(max(0, maximum)):
            raw = syntax[0x38 + index * element:0x38 + (index + 1) * element]
            if len(raw) < SYNTAX_NODE_SIZE:
                break
            nodes.append(struct.unpack("<hhhhiii", raw[:SYNTAX_NODE_SIZE]))
    for salt, index_field, kind, flags, next_node, source, data in nodes:
        if not salt:
            continue
        if flags & PRIMITIVE:
            if flags & VARIABLE:
                global_uses[string(source)] += 1
            continue
        if flags & SCRIPT:
            continue
        predicate = data & 0xFFFF
        if predicate < len(nodes):
            calls[string(nodes[predicate][5])] += 1
    r["script_nodes"] = sum(1 for n in nodes if n[0])
    r["script_node_capacity"] = len(nodes)
    r["script_strings_size"] = strings_size
    r["scripts"] = scripts_count
    r["scenario_globals"] = len(scenario_globals)
    r["functions_called"] = len(calls)
    r["functions_missing"] = {n: c for n, c in calls.items() if n and n not in functions}
    r["globals_external_missing"] = {n: c for n, c in global_uses.items()
                                     if n and n not in scenario_globals and n not in globals_}
    r["window_need"] = max(cache.tag_data_size, r["bsp_highest_end"] - CE_TAG_BASE if bsps else 0)
    return r


def human(n):
    return "%.1f MB" % (n / 1048576)


def report_md(results):
    lines = ["# SPV3 maps: offline survey", ""]
    lines.append("| map | yelo flags | tag data | tags | BSPs | largest BSP | BSP top | window needed | scripts missing fns |")
    lines.append("|---|---|---|---|---|---|---|---|---|")
    for r in results:
        lines.append("| %s | %s | %s | %d | %d | %s | 0x%08X | %s | %d |" % (
            r["map"], ",".join(r["yelo_flag_names"]) or "-", human(r["tag_data_size"]), r["tag_count"],
            r["bsp_count"], human(r["bsp_largest"]), r["bsp_highest_end"], human(r["window_need"]),
            len(r["functions_missing"])))
    lines += ["", "Limits: stock tag cache %s, OpenSauce %s, the game's %s (+%s conversion room); tags stock %d, "
              "OpenSauce %d; BSPs stock %d." % (human(STOCK_TAG_CACHE_SIZE), human(OPENSAUCE_TAG_CACHE_SIZE),
                                                human(GAME_TAG_CACHE_SIZE), human(GAME_CONVERSION_ROOM),
                                                STOCK_TAG_COUNT, OPENSAUCE_TAG_COUNT, STOCK_BSPS), ""]
    # rankings
    def rank(key):
        maps, uses = collections.Counter(), collections.Counter()
        for r in results:
            for name, count in r[key].items():
                maps[name] += 1
                uses[name] += count
        return sorted(maps, key=lambda n: (-maps[n], -uses[n], n)), maps, uses

    for key, title in (("functions_missing", "Script functions the game lacks"),
                       ("globals_external_missing", "External globals the game lacks"),
                       ("groups_unknown", "Tag groups that are not stock")):
        names, maps, uses = rank(key)
        lines += ["## %s (%d)" % (title, len(names)), "", "| name | maps | uses |", "|---|---|---|"]
        for name in names:
            label = name
            if key == "groups_unknown" and name in OPENSAUCE_GROUPS:
                label = "%s (%s)" % (name, OPENSAUCE_GROUPS[name])
            lines.append("| %s | %d | %d |" % (label, maps[name], uses[name]))
        lines.append("")
    lines += ["## Per map", ""]
    for r in results:
        lines.append("### %s" % r["map"])
        lines.append("- header: %s v%d, %s, type %d, build %s, file %d bytes (header says %d)" % (
            r["signature"], r["version"], r["name"], r["cache_type"], r["build"], r["file_size"], r["file_length"]))
        lines.append("- yelo: %s v%d flags 0x%04X (%s), upgrade %.2f, mod '%s', scenario reference %s" % (
            r["yelo"], r["yelo_version"], r["yelo_flags"], ", ".join(r["yelo_flag_names"]) or "none",
            r["yelo_upgrade"], r["yelo_mod"], r["scenario_yelo_reference"] or "none"))
        lines.append("- scripts: %d, %d of %d syntax nodes, %d bytes of strings, %d functions called, %d globals" % (
            r["scripts"], r["script_nodes"], r["script_node_capacity"], r["script_strings_size"],
            r["functions_called"], r["scenario_globals"]))
        lines.append("- BSPs: " + ", ".join("%s %s at 0x%08X" % (b["name"].split("\\")[-1], human(b["size"]), b["base"])
                                            for b in r["bsps"]))
        lines.append("- resources: looked up %s, missing %s" % (r["resources_looked_up"], r["resources_missing"]))
        lines.append("- groups not stock: %s" % (r["groups_unknown"] or "none"))
        lines.append("")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--md", type=Path)
    parser.add_argument("--tsv", type=Path)
    args = parser.parse_args(argv)
    files = []
    for path in args.paths:
        if path.is_dir():
            files += sorted(p for p in path.iterdir() if p.suffix.lower() in (".yelo", ".map")
                            and p.stem.lower() not in ("bitmaps", "sounds", "loc"))
        else:
            files.append(path)
    functions, globals_ = game_tables(args.source)
    results = []
    resources_by_folder = {}
    for path in files:
        folder = path.parent
        if folder not in resources_by_folder:
            resources_by_folder[folder] = {k: resource_map(folder / ("%s.map" % k)) for k in ("bitmaps", "sounds")}
        try:
            results.append(scan(path, functions, globals_, resources_by_folder[folder]))
        except Exception as error:  # a map this tool can't read is said, and the rest go on
            print("%s: %s" % (path.name, error), file=sys.stderr)
    text = report_md(results)
    if args.md:
        args.md.write_text(text)
    else:
        print(text)
    if args.tsv:
        with open(args.tsv, "w") as out:
            out.write("map\tkind\tname\tcount\n")
            for r in results:
                for key in ("functions_missing", "globals_external_missing", "groups_unknown", "groups"):
                    for name, count in sorted(r[key].items()):
                        out.write("%s\t%s\t%s\t%d\n" % (r["map"], key, name, count))
    return 0


if __name__ == "__main__":
    sys.exit(main())
