"""Writes the bots' test levels (levels.py) for the editing kit:
data/levels/test/<name>/models/<name>.JMS, and a list of what each level has
placed in it for invader-edit (scenario.json), checking each mesh is closed.

  python3 build.py <output folder>"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geometry
import levels


def check_place(level, x, y, what):
    h = level.grid.height_at(x, y)
    if h >= level.grid.ceiling - 1.0:
        raise ValueError("%s: %s at %.1f %.1f is in a wall" % (level.name, what, x, y))
    x0, y0, x1, y1 = level.grid.bounds
    if not (x0 + 0.5 < x < x1 - 0.5 and y0 + 0.5 < y < y1 - 0.5):
        raise ValueError("%s: %s at %.1f %.1f is outside the level" % (level.name, what, x, y))


def main(out):
    report = []
    for level in levels.all_levels():
        vertices, triangles = geometry.mesh(level.grid)
        problems = geometry.check_closed(vertices, triangles)
        if problems:
            raise SystemExit("%s: the mesh is not closed (%d edges), e.g. %s" % (level.name, len(problems), problems[:3]))
        for x, y, z, yaw, team in level.spawns:
            check_place(level, x, y, "a spawn")
        for team, (x, y, z) in level.flags.items():
            check_place(level, x, y, "flag %d" % team)
        for (a, b, yaw, channel) in level.teleporters:
            check_place(level, a[0], a[1], "teleporter %d" % channel)
            check_place(level, b[0], b[1], "teleporter %d's end" % channel)
        for kind, x, y, z, yaw in level.vehicles:
            check_place(level, x, y, "a " + kind)
        folder = os.path.join(out, "data", "levels", "test", level.name, "models")
        os.makedirs(folder, exist_ok=True)
        materials = geometry.write_jms(os.path.join(folder, level.name + ".JMS"), vertices, triangles)
        report.append({
            "name": level.name, "title": level.title, "about": level.about, "game": level.game, "drive": level.drive,
            "materials": materials, "triangles": len(triangles),
            "spawns": level.spawns, "flags": {str(k): v for k, v in level.flags.items()},
            "teleporters": level.teleporters, "vehicles": level.vehicles, "weapons": level.weapons, "tour": level.tour,
        })
        print("%-22s %6d triangles  %2d spawns  %d teleporters  %d vehicles  materials %s" % (level.name, len(triangles), len(level.spawns),
            len(level.teleporters), len(level.vehicles), " ".join(materials)))
    with open(os.path.join(out, "levels.json"), "w") as f:
        json.dump(report, f, indent=1)
    # (and the batch file that makes them into maps, for the editing kit's folder)
    import windows
    with open(os.path.join(out, "build_bot_levels.bat"), "w", newline="") as f:
        f.write(windows.batch(json.loads(json.dumps(report))))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "out")
