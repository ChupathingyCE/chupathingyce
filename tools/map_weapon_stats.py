#!/usr/bin/env python3
"""Lists every weapon tag of a cache file (version 7 or 609) with its key
gameplay stats: magazine, triggers (rate of fire, projectiles per shot, error
angle), the projectile (initial velocity) and the damage of its impact and
detonation damage effects.

    map_weapon_stats.py MAP [MAP ...]              text, one block per map
    map_weapon_stats.py --markdown MAP [MAP ...]   one Markdown table per map
                                                   plus a cross-map summary
    map_weapon_stats.py --reference MAP --markdown MAP [MAP ...]
                                                   the summary also compares
                                                   weapons with the same path
                                                   as REFERENCE's

Offsets follow source/objects/object_definitions.h (object part 0x17C bytes),
items/item_definitions.h (item part 0x18C bytes, so the weapon part starts at
0x308), items/weapon_definitions.h, items/projectile_definitions.h and
objects/damage_effect_definitions.h. Reads the maps only.
"""
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_stock_maps import TAG_BASE, load, read_tags  # noqa: E402

OBJECT_SIZE = 0x17C
WEAPON_BASE = 0x308                       # object part + item part
WEAPON_HEAT_THRESHOLDS = WEAPON_BASE + 0x44   # recovery, overheated, detonation
WEAPON_MAGAZINES = WEAPON_BASE + 0x1E8   # tag block: count, address, definition
WEAPON_TRIGGERS = WEAPON_BASE + 0x1F4
MAGAZINE_SIZE = 112
TRIGGER_SIZE = 0x114
PROJECTILE_VELOCITY = OBJECT_SIZE + 104
PROJECTILE_ATTACHED_DETONATION = OBJECT_SIZE + 152
PROJECTILE_IMPACT = OBJECT_SIZE + 168
DAMAGE_EFFECT_DAMAGE = 452                # damage_definition inside a damage effect


class Map:
    def __init__(self, path):
        self.path = Path(path)
        self.data, self.version, self.tag_offset, self.tag_size = load(path)
        self.tags, _ = read_tags(self.data, self.tag_offset, self.tag_size)
        self.by_id = {tag["id"]: tag for tag in self.tags}

    def at(self, address):
        return self.tag_offset + address - TAG_BASE

    def u32(self, offset):
        return struct.unpack_from("<I", self.data, offset)[0]

    def real(self, offset):
        return struct.unpack_from("<f", self.data, offset)[0]

    def short(self, offset):
        return struct.unpack_from("<h", self.data, offset)[0]

    def tag_start(self, tag):
        return self.at(tag["address"])

    def reference(self, offset):
        """The tag a tag reference at the offset points to, or None"""
        return self.by_id.get(self.u32(offset + 12))

    def block(self, offset, size):
        count, address = struct.unpack_from("<II", self.data, offset)
        if not count or address < TAG_BASE:
            return []
        return [self.at(address) + size * number for number in range(count)]

    def damage(self, tag):
        """(lower bound, upper bound) of a damage effect tag, or None"""
        if tag is None or tag["class"] != "jpt!":
            return None
        start = self.tag_start(tag) + DAMAGE_EFFECT_DAMAGE
        return self.real(start + 16), self.real(start + 20)

    def projectile(self, tag):
        if tag is None or tag["class"] != "proj":
            return None
        start = self.tag_start(tag)
        return {"name": tag["name"], "velocity": self.real(start + PROJECTILE_VELOCITY),
                "impact": self.damage(self.reference(start + PROJECTILE_IMPACT)),
                "detonation": self.damage(self.reference(start + PROJECTILE_ATTACHED_DETONATION))}

    def weapons(self):
        result = []
        for tag in self.tags:
            if tag["class"] != "weap" or tag["indexed"] or tag["address"] < TAG_BASE:
                continue
            start = self.tag_start(tag)
            magazines = []
            for offset in self.block(start + WEAPON_MAGAZINES, MAGAZINE_SIZE):
                magazines.append({"initial": self.short(offset + 6), "maximum": self.short(offset + 8),
                                  "loaded": self.short(offset + 10)})
            triggers = []
            for offset in self.block(start + WEAPON_TRIGGERS, TRIGGER_SIZE):
                triggers.append({
                    "rate": (self.real(offset + 4), self.real(offset + 8)),
                    "rounds_per_shot": self.short(offset + 34),
                    "per_shot": self.short(offset + 110),
                    "error": (math.degrees(self.real(offset + 124)), math.degrees(self.real(offset + 128))),
                    "heat": self.real(offset + 184),
                    "projectile": self.projectile(self.reference(offset + 148)),
                })
            label = self.data[start + WEAPON_BASE + 4: start + WEAPON_BASE + 36].split(b"\0")[0].decode("latin-1")
            name = tag["name"]
            if name == "<protected>":      # HaloMD hides some tag paths; the label tells them apart
                name = f"<protected:{label}>"
                while any(w["name"] == name for w in result):
                    name += "+"
            result.append({"name": name, "magazines": magazines, "triggers": triggers,
                           "overheat": self.real(start + WEAPON_HEAT_THRESHOLDS + 4)})
        return sorted(result, key=lambda weapon: weapon["name"])


def fmt(value, places=2):
    return f"{value:.{places}f}".rstrip("0").rstrip(".") if value is not None else "-"


def pair(low, high, places=2):
    return fmt(low, places) if abs(low - high) < 1e-6 else f"{fmt(low, places)}-{fmt(high, places)}"


def damage_text(damage):
    return "-" if damage is None else pair(*damage)


def summary(weapon):
    """A weapon's stats as one comparable value"""
    def rounded(values):
        return None if values is None else tuple(round(v, 3) for v in values)
    triggers = []
    for t in weapon["triggers"]:
        p = t["projectile"]
        projectile = None if p is None else (round(p["velocity"], 3), rounded(p["impact"]), rounded(p["detonation"]))
        triggers.append((rounded(t["rate"]), t["per_shot"], t["rounds_per_shot"], rounded(t["error"]),
                         round(t["heat"], 4), projectile))
    return (tuple((m["loaded"], m["initial"], m["maximum"]) for m in weapon["magazines"]), tuple(triggers))


def print_text(game):
    print(f"== {game.path.name} (cache version {game.version}) ==")
    for weapon in game.weapons():
        magazines = ", ".join(f"{m['loaded']} per magazine, {m['initial']}/{m['maximum']} total"
                              for m in weapon["magazines"]) or "none"
        print(f"{weapon['name']}: magazine {magazines}; overheats at {fmt(weapon['overheat'])}")
        for number, trigger in enumerate(weapon["triggers"]):
            print(f"  trigger {number}: {pair(*trigger['rate'])} rounds/s, {trigger['per_shot']} projectile(s)/shot, "
                  f"error {pair(*trigger['error'])} deg, heat/round {fmt(trigger['heat'], 4)}")
            projectile = trigger["projectile"]
            if projectile:
                print(f"    projectile {projectile['name']}: velocity {fmt(projectile['velocity'])}, "
                      f"impact damage {damage_text(projectile['impact'])}, "
                      f"detonation damage {damage_text(projectile['detonation'])}")


def table(game):
    rows = ["| weapon | magazine | rate of fire | projectiles/shot | damage | velocity |",
            "|---|---|---|---|---|---|"]
    for weapon in game.weapons():
        magazine = "/".join(str(m["loaded"]) for m in weapon["magazines"]) or "-"
        rate = " ; ".join(pair(*t["rate"]) for t in weapon["triggers"]) or "-"
        shots = " ; ".join(str(t["per_shot"]) for t in weapon["triggers"]) or "-"
        damage, velocity = [], []
        for trigger in weapon["triggers"]:
            projectile = trigger["projectile"]
            text = damage_text(projectile["impact"]) if projectile else "-"
            if projectile and projectile["detonation"]:
                text += f" (+det {damage_text(projectile['detonation'])})"
            damage.append(text)
            velocity.append(fmt(projectile["velocity"]) if projectile else "-")
        rows.append(f"| {weapon['name']} | {magazine} | {rate} | {shots} | {' ; '.join(damage)} | "
                    f"{' ; '.join(velocity)} |")
    return "\n".join(rows)


def describe(weapon):
    magazine = "/".join(str(m["loaded"]) for m in weapon["magazines"]) or "-"
    parts = []
    for trigger in weapon["triggers"]:
        projectile = trigger["projectile"]
        parts.append(f"{pair(*trigger['rate'])}/s x{trigger['per_shot']} dmg "
                     f"{damage_text(projectile['impact']) if projectile else '-'} "
                     f"vel {fmt(projectile['velocity']) if projectile else '-'}")
    totals = "/".join(f"{m['initial']}-{m['maximum']}" for m in weapon["magazines"]) or "-"
    errors = "/".join(pair(*t["error"]) for t in weapon["triggers"]) or "-"
    return f"mag {magazine} (total {totals}), error {errors}, " + "; ".join(parts)


def markdown(games, reference):
    out = []
    for game in games:
        out.append(f"## {game.path.stem} (cache version {game.version})\n\n{table(game)}\n")
    out.append("## Cross-map summary\n")
    weapons = {game.path.stem: {w["name"]: w for w in game.weapons()} for game in games}
    names = {}
    for stem, found in weapons.items():
        for name in found:
            names.setdefault(name, []).append(stem)
    out.append("Weapons with the same tag path in more than one map, and whether their stats differ:\n")
    for name in sorted(names):
        stems = names[name]
        if len(stems) < 2:
            continue
        variants = {}
        for stem in stems:
            variants.setdefault(summary(weapons[stem][name]), []).append(stem)
        verdict = "identical" if len(variants) == 1 else f"{len(variants)} different versions"
        out.append(f"- `{name}` in {len(stems)} maps: {verdict}")
        if len(variants) > 1:
            for members in variants.values():
                out.append(f"    - {', '.join(members)}: {describe(weapons[members[0]][name])}")
    if reference:
        ref = {w["name"]: w for w in reference.weapons()}
        out.append(f"\nWeapons whose path matches one in {reference.path.stem}, and whether the stats match it:\n")
        for stem, found in weapons.items():
            if stem == reference.path.stem:
                continue
            for name, weapon in sorted(found.items()):
                if name in ref:
                    if summary(weapon) == summary(ref[name]):
                        out.append(f"- {stem}: `{name}` same as stock")
                    else:
                        out.append(f"- {stem}: `{name}` differs: {describe(weapon)} (stock: {describe(ref[name])})")
    return "\n".join(out)


def main(argv):
    args = argv[1:]
    reference = None
    if args[:1] == ["--reference"] and len(args) > 1:
        reference = Map(args[1])
        args = args[2:]
    as_markdown = args[:1] == ["--markdown"]
    if as_markdown:
        args = args[1:]
    if not args:
        print(__doc__)
        return 1
    games = [Map(path) for path in args]
    if as_markdown:
        print(markdown(games, reference))
    else:
        for game in games:
            print_text(game)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
