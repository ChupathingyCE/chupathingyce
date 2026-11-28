#!/usr/bin/env python3
"""Debug views of one camera spot
(tools/modern_lighting_lab.py's run), for tuning:

    python3 tools/modern_lighting_debug.py --build build/linux64/chupathingy --maps ~/maps \\
        --out /tmp/dbg --map bloodgulch --pose "96 -150 3 126 2" --shot "rt ao;debug 4"

Each --shot is lab commands separated by ';' and gives <out>/<map>-<n>.png.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import modern_lighting_lab as lab  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--maps", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--map", required=True)
    parser.add_argument("--pose", action="append", default=[])
    parser.add_argument("--console", action="append", default=[])
    parser.add_argument("--shot", action="append", default=[])
    parser.add_argument("--settle", type=float, default=15.0)
    parser.add_argument("--size", default="1920x1080")
    parser.add_argument("--fast", type=float, default=0.0, help="seconds of game_speed 10 first (cutscenes)")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    run = lab.Run(args.build.resolve(), args.maps.resolve(), args.map, args.out / "work", args.size, {})
    try:
        run.start()
        if args.fast:
            run.console(["(game_speed 10)"])
            time.sleep(args.fast)
            run.console(["(game_speed 1)"])
        time.sleep(args.settle)
        commands = ["(game_speed 1)", "(show_hud 0)", "(cinematic_show_letterbox 0)",
                    "(set cheat_deathless_player true)"] + args.console
        start = run.say("where").split()
        origin = tuple(map(float, start[1:4]))
        print("player", origin, flush=True)
        for index, pose in enumerate(args.pose or [None]):
            if pose:
                fields = pose.split()
                if fields[0] == "rel":
                    values = list(map(float, fields[1:]))
                    values[0] += origin[0]
                    values[1] += origin[1]
                    values[2] += origin[2]
                    print("pose", " ".join("%.2f" % v for v in values), flush=True)
                else:
                    values = list(map(float, fields))
                text = lab.camera_text(*values)
                (run.save / "camera.txt").write_text(text)
                run.console(commands + ["debug_camera_load", "(game_speed 0)"])
                time.sleep(2.0)
                run.console(["debug_camera_load"])
                time.sleep(2.0)
            else:
                run.console(commands)
                time.sleep(2.0)
            print(run.say("where"), flush=True)
            for number, shot in enumerate(args.shot):
                run.lab_commands([c.strip() for c in shot.split(";")])
                time.sleep(2.0)
                path = args.out / f"{args.map}-{index}-{number}.png"
                print(path, run.shot(path), flush=True)
            run.lab_commands(["debug 0", "rt config", "hdr config"])
        print(run.say("say end"))
    finally:
        run.stop()
        run.keep_logs(args.out)
        run.clean()


if __name__ == "__main__":
    sys.exit(main())
