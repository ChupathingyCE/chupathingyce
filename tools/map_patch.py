#!/usr/bin/env python3
"""Makes a patched map: a stock Custom Edition map with tags of other maps
added, as a recipe (port/assets/map_recipes/<name>.json) says, written as a
new map of its own name that the game loads as it loads any Custom Edition
map, and that the map tools open as any other:

    python tools/map_patch.py --recipe port/assets/map_recipes/openpitch_bloodgulch.json \\
        --maps maps_ce --out maps_ce/openpitch_bloodgulch.map

A recipe names its base map (its family, its file and the SHA-256 of the one
stock version it applies to: no other map is patched), the content packs it
adds (asset packs made by tools/ce_asset_pack.py: Custom Edition maps of a
few tags, in the same folder, each with its credit), the weapons a game type hands out and the ball, the label
each weapon is held by, and the output's name, which is never the base's.
Grifball's Open Pitch on Blood Gulch is the first
(openpitch_bloodgulch.json). The output is the same bytes from the same
inputs on every machine, so its hash names it in a network game as any
Custom Edition map's does.

What a pack adds: its tags (all but its stub scenario), added as
tools/ce_cache.py says (a stock tag the base map has too is the base
map's); the ball, as the globals' multiplayer ball; the labels the recipe
gives its weapons; and in the header's free space (from 0x400), a text that
says what the map is: its recipe, its base map, the weapons the game type
hands out, the ball and the packs' credits ("key: value" lines, which the
game reads: cache_files_ce_patch_text).
"""
import argparse
import hashlib
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ce_cache  # noqa: E402

# the globals' multiplayer information, its ball (game_globals.h)
GLOBALS_MULTIPLAYER_INFORMATION = 0x164
MULTIPLAYER_INFORMATION_BALL = 0x4C


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def patch(recipe, maps, out):
    base_recipe = recipe["base"]
    if recipe["output"].lower() == base_recipe["map"].lower():
        sys.exit("a recipe's output has a name of its own")
    if base_recipe["family"] != "ce":
        sys.exit("only Custom Edition base maps can be patched")
    base_path = os.path.join(maps, base_recipe["map"] + ".map")
    if sha256(base_path) != base_recipe["sha256"]:
        sys.exit(f"{base_path} is not the stock map the recipe is for")
    base = ce_cache.CacheFile(base_path)
    builder = ce_cache.Builder(base)
    names = {}
    pack_hashes = []
    for pack_recipe in recipe["packs"]:
        pack_path = os.path.join(maps, pack_recipe["file"] + ".map")
        if "sha256" in pack_recipe and sha256(pack_path) != pack_recipe["sha256"]:
            sys.exit(f"{pack_path} is not the pack the recipe is for")
        pack_hashes.append(f"{pack_recipe['file']} {sha256(pack_path)}")
        pack = ce_cache.CacheFile(pack_path)
        mapping = builder.add(pack, [index for index, tag in enumerate(pack.tags) if tag["group"] != "scnr"])
        for index, here in mapping.items():
            names[(pack.tags[index]["group"], pack.tags[index]["name"].lower())] = here

    def tag(group, name):
        if (group, name.lower()) not in names:
            sys.exit(f"no pack has the {group} tag {name}")
        return names[(group, name.lower())]

    for name, label in recipe.get("labels", {}).items():
        builder.relabel(tag("weap", name), label)
    if recipe.get("ball"):
        ball = tag("weap", recipe["ball"])
        globals_index = next(index for index, base_tag in enumerate(base.tags) if base_tag["group"] == "matg")
        information = builder.get(base.tags[globals_index]["base"] + GLOBALS_MULTIPLAYER_INFORMATION + 4)
        reference = information + MULTIPLAYER_INFORMATION_BALL
        builder.put(reference + 4, builder.name_addresses[ball])
        builder.put(reference + 12, builder.ids[ball])

    # (what the map was made from: the game makes it again when any of them
    # changes)
    lines = [f"patch: {recipe['output']}", f"recipe: {recipe['recipe_version']}",
             f"base: {base_recipe['map']} {base_recipe['sha256']}"]
    lines += [f"pack: {pack_hash}" for pack_hash in pack_hashes]
    for name in recipe.get("weapons", []):
        tag("weap", name)
        lines.append(f"weapon: {name}")
    if recipe.get("ball"):
        lines.append(f"ball: {recipe['ball']}")
    lines += [f"credit: {pack_recipe['credit']}" for pack_recipe in recipe["packs"]]
    data = builder.write(out, recipe["output"], "\n".join(lines) + "\n")
    print(f"{out}: {len(builder.instances) - base.tag_count} tags added to {base.name}'s {base.tag_count}: "
          f"{len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--recipe", required=True)
    parser.add_argument("--maps", required=True, help="the folder of the base map and the packs (maps_ce)")
    parser.add_argument("--out", required=True)
    arguments = parser.parse_args()
    with open(arguments.recipe) as file:
        recipe = json.load(file)
    patch(recipe, arguments.maps, arguments.out)


if __name__ == "__main__":
    main()
