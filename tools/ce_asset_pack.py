#!/usr/bin/env python3
"""Makes an asset pack: a few weapons of a Custom Edition map and every tag
they use, as a Custom Edition map of its own (version 609, a stub scenario
and those tags, its bitmaps and sounds in bitmaps.map and sounds.map as a
map's are), which the map tools open as any other, and which
tools/map_patch.py adds to a stock map as a recipe says:

    python tools/ce_asset_pack.py list --map maps_ce/grifball.map
    python tools/ce_asset_pack.py deps --map maps_ce/grifball.map \\
        --weapon "weapons\\grifball\\grifball"
    python tools/ce_asset_pack.py build --map maps_ce/grifball.map \\
        --out maps_ce/grifball_assets.map \\
        --weapon "h3mt\\weapons\\covenant\\gravity_hammer\\gravity_hammer" \\
        --weapon "weapons\\energy_sword\\energy_sword backup" \\
        --weapon "weapons\\grifball\\grifball"

list: the map's weapons. deps: a weapon's tags, by group, with where each
    one's data is (the map's tags, or the resource maps). build: the pack,
    named for its file.

What a weapon uses is found by its bytes, and its tags are copied as
tools/ce_cache.py says. The pack is the source map's own data, made
smaller: it is shared only as the map may be.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ce_cache  # noqa: E402


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
    cache = ce_cache.CacheFile(arguments.map)
    for index, tag in enumerate(cache.tags):
        if tag["group"] == "weap":
            print(f"{index:5} {tag['name']}")


def command_deps(arguments):
    cache = ce_cache.CacheFile(arguments.map)
    kept = cache.closure(cache.find("weap", name) for name in arguments.weapon)
    size = 0
    for index in sorted(kept, key=lambda index: (cache.tags[index]["group"], cache.tags[index]["name"])):
        base, following = cache.extent.get(index, (0, 0))
        size += following - base
        print(f"{cache.tags[index]['group']:4} {cache.tags[index]['name']:70} {following - base:7}  "
              f"{where(cache, index)}")
    print(f"{len(kept)} tags, {size} bytes of tags")


def command_build(arguments):
    cache = ce_cache.CacheFile(arguments.map)
    kept = cache.closure(cache.find("weap", name) for name in arguments.weapon)
    name = os.path.splitext(os.path.basename(arguments.out))[0]
    pack = ce_cache.Builder()
    pack.add_stub_scenario(f"levels\\{name}\\{name}")
    pack.add(cache, kept)
    data = pack.write(arguments.out, name)
    print(f"{arguments.out}: {len(kept)} tags of {cache.name}, {pack.parts} model parts: {len(data)} bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("list", "deps", "build"):
        command = commands.add_parser(name)
        command.add_argument("--map", required=True, help="the Custom Edition map")
        if name != "list":
            command.add_argument("--weapon", action="append", required=True, help="a weapon tag")
        if name == "build":
            command.add_argument("--out", required=True)
    arguments = parser.parse_args()
    {"list": command_list, "deps": command_deps, "build": command_build}[arguments.command](arguments)


if __name__ == "__main__":
    main()
