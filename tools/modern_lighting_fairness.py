#!/usr/bin/env python3
"""How much darker the lighting passes make
a player, for Chupathingy's rule that nothing may hide a player in a
network game. The player is put at a spot (cheat_teleport_to_camera), a
debug camera looks at it from a couple of units away, and each mode is
captured; the player's pixels are those the ray tracing's classification
puts on an object (debug view 4). Their mean luminance (Rec. 709,
linear) in each mode is compared with OFF's, with and without the network
game's floor (display.lighting_unit_floor, forced by the lab's "network on").

    python3 tools/modern_lighting_fairness.py --build build/linux64/chupathingy --maps ~/maps --out /tmp/fair

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import math
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import modern_lighting_lab as lab  # noqa: E402

MODES = [
    ("off", ["rt off", "hdr off", "glow off", "network off"]),
    ("trackA", ["rt ao", "hdr off", "glow off", "network off"]),
    ("trackA-floor", ["rt ao", "hdr off", "glow off", "network on"]),
]

# spot: where the player stands (the camera teleported to, it falls), and
# where the camera looks from, relative to the player's eye, yaw, pitch
SPOTS = {
    "bloodgulch": [
        ("shade", (66.0, -82.0, 3.0), (2.4, 0.4, 0.3, 190.0, -8.0)),
        ("sun", (70.0, -95.0, 3.0), (2.4, 0.4, 0.3, 190.0, -8.0)),
    ],
}


LINEAR = [((v / 255.0 + 0.055) / 1.055) ** 2.4 if v > 10 else v / 255.0 / 12.92 for v in range(256)]


def luminance(path):
    """the picture's linear luminance (Rec. 709) per pixel, from its 8-bit sRGB"""
    raw = subprocess.run(["magick", str(path), "-alpha", "off", "-depth", "8", "rgb:-"],
                         check=True, capture_output=True).stdout
    return [0.2126 * LINEAR[raw[i]] + 0.7152 * LINEAR[raw[i + 1]] + 0.0722 * LINEAR[raw[i + 2]]
            for i in range(0, len(raw), 3)]


def differs(a, b, threshold=0.03):
    return [abs(x - y) > threshold for x, y in zip(a, b)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--maps", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--map", default="bloodgulch")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    run = lab.Run(args.build.resolve(), args.maps.resolve(), args.map, args.out / "work", "1920x1080", {})
    report = []
    try:
        run.start()
        time.sleep(12)
        run.console(["(show_hud 0)", "(show_hud_help_text 0)", "(set cheat_deathless_player true)"])
        for name, stand, view in SPOTS[args.map]:
            # the player to the spot
            (run.save / "camera.txt").write_text(lab.camera_text(*stand, 0.0, -10.0))
            run.console(["(game_speed 1)", "debug_camera_load"])
            time.sleep(1.5)
            run.console(["(cheat_teleport_to_camera)", "(camera_control 0)"])
            time.sleep(3.0)
            where = run.say("where").split()
            eye = tuple(map(float, where[1:4]))
            pose = (eye[0] + view[0], eye[1] + view[1], eye[2] + view[2], view[3], view[4])
            (run.save / "camera.txt").write_text(lab.camera_text(*pose))
            run.console(["debug_camera_load", "(game_speed 0)"])
            time.sleep(2.0)
            shots = {}
            for mode, commands in MODES:
                run.lab_commands(commands)
                time.sleep(2.5)
                path = args.out / f"{args.map}-fairness_{name}-{mode}.png"
                run.shot(path)
                shots[mode] = path
            # the player's pixels: the trace's own classification (debug view
            # 4's blue: a pixel on an object's collision model), the
            # objects near the player being only it
            run.lab_commands(["rt ao", "hdr off", "network off", "debug 4"])
            time.sleep(2.5)
            classes = args.out / f"{args.map}-fairness_{name}-classes.png"
            run.shot(classes)
            run.lab_commands(["debug 0", "rt config", "hdr config"])
            run.console(["(game_speed 1)"])
            raw = subprocess.run(["magick", str(classes), "-alpha", "off", "-depth", "8", "rgb:-"],
                                 check=True, capture_output=True).stdout
            object_mask = [raw[i + 2] > 128 for i in range(0, len(raw), 3)]
            base = luminance(shots["off"])
            mask = object_mask
            count = sum(mask)
            mean_off = sum(v for v, m in zip(base, mask) if m) / max(count, 1)
            line = [f"{name}: player pixels {count}, off mean luminance {mean_off:.4f}"]
            for mode, _ in MODES[1:]:
                values = luminance(shots[mode])
                mean = sum(v for v, m in zip(values, mask) if m) / max(count, 1)
                # (the darkest 10% of the player's pixels, relative: where AO bites)
                ratios = sorted(v / b for v, b, m in zip(values, base, mask) if m and b > 0.01)
                low = ratios[len(ratios) // 10] if ratios else float("nan")
                line.append(f"  {mode}: mean {mean:.4f} ({mean / mean_off * 100:.1f}% of off), "
                            f"10th percentile pixel ratio {low:.3f}")
            report += line
            print("\n".join(line), flush=True)
    finally:
        run.stop()
        run.clean()
    (args.out / "fairness.txt").write_text("\n".join(report) + "\n")


if __name__ == "__main__":
    sys.exit(main())
