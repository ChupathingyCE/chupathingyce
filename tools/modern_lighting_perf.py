#!/usr/bin/env python3
"""Display.ray_tracing's cost in fights,
each pass's GPU time, and pictures of the same paused frame in each mode,
for the APU work (an integrated GPU: DRI_PRIME names it).

    python3 tools/modern_lighting_perf.py --build build/linux64/chupathingy \\
        --maps ~/maps --out ~/rt-perf-out --gpu pci-0000_12_00_0 [--scene b30 ...] \\
        [--modes off,shadows,ao,glow] [--frames 600] [--label after]

Scenes (the game running, the fight going on, while each mode is timed):
- b30: the beach after the landing, the Marines against the Covenant
  (the player's own view, and a camera over the landing zone);
- a10: the corridor fight by the cryo bay (crossfire and first_contact
  placed, the player taken there);
- bloodgulch: the blue base with its vehicles, from the ground.

Each view: every mode timed for --frames frames (the lab's "time": average,
p99, worst, the worst stretch of 30 frames, each pass's GPU time), then the
game paused and each mode's picture taken of that same frame. Runs are
headless (tools/modern_lighting_lab.py's Run: SDL offscreen, EGL on the GPU
named, a network namespace of its own, own data/save/home folders, deleted
after). Out: <out>/<label>/<scene>-<view>-<mode>.png and <out>/<label>/
times.tsv.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import modern_lighting_lab as lab  # noqa: E402

BASE = ["hdr off", "geometry config", "debug 0", "glow off", "glow_intensity 0", "rtflags 0"]
MODES = {
    "off": BASE + ["rt off"],
    "shadows": BASE + ["rt shadows"],
    "ao": BASE + ["rt ao"],
    # (the owner's: glow on at 0.45)
    "glow": BASE + ["rt off", "glow on", "glow_intensity 0.45"],
    "ao_glow": BASE + ["rt ao", "glow on", "glow_intensity 0.45"],
    # (the trace's parts timed: the lab's rtflags leave one out)
    "ao_norefl": BASE + ["rt ao", "rtflags 1"],
    "ao_noocc": BASE + ["rt ao", "rtflags 2"],
}
QUALITIES = ("high", "medium", "low")


def look(cx, cy, cz, tx, ty, tz):
    dx, dy, dz = tx - cx, ty - cy, tz - cz
    return (cx, cy, cz, math.degrees(math.atan2(dy, dx)), math.degrees(math.atan2(dz, math.hypot(dx, dy))))


SCENES = {
    "b30": {
        "fast": 25,
        "setup": [],
        "views": {
            "beach_player": "player",
            "beach": (-55.44, 32.92, 5.41, 135, -15),
        },
    },
    "a10": {
        "fast": 40,
        "setup": [("pose", look(-41.0, -14.0, 0.9, -33.0, -18.0, 0.6)), ("hs", ["debug_camera_load"]), ("wait", 1.0),
                  ("hs", ["cheat_teleport_to_camera"]), ("wait", 1.0),
                  ("hs", ["(ai_place crossfire)", "(ai_place crossfire_anti)", "(ai_place first_contact)",
                          "(ai_place first_contact_anti)"]), ("wait", 3.0)],
        "views": {
            "corridor_fight": look(-41.0, -14.0, 1.4, -31.0, -18.5, 0.6),
        },
    },
    "bloodgulch": {
        "fast": 0,
        "setup": [],
        "views": {
            "vehicles": (96, -150, 3, 20, 8),
            "base": (44, -86, 3, -54, 2),
        },
    },
}


def settle(run, seconds):
    time.sleep(seconds)


def timed(run, label, frames):
    out = Path(str(run.lab) + ".out")
    before = out.read_text().count("passes ") if out.exists() else 0
    run.lab_commands([f"time {label} {frames}"])
    deadline = time.time() + max(90, frames * 0.25)
    while time.time() < deadline:
        if out.exists() and out.read_text().count("passes ") > before:
            break
        time.sleep(0.25)
    text = out.read_text() if out.exists() else ""
    times = [l for l in text.splitlines() if l.startswith(f"time {label} ")]
    passes = [l for l in text.splitlines() if l.startswith(f"passes {label} ")]
    fields = {}
    for line in (times[-1:] + passes[-1:]):
        fields.update(dict(f.split("=", 1) for f in line.split()[2:] if "=" in f))
    return fields


def apply_pose(run, pose):
    (run.save / "camera.txt").write_text(lab.camera_text(*pose))
    (run.data / "camera.txt").write_text(lab.camera_text(*pose))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--maps", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--label", default="run")
    parser.add_argument("--gpu", default="pci-0000_12_00_0", help="DRI_PRIME (the integrated GPU by default)")
    parser.add_argument("--egl-device", default="0", help="SDL's EGL device index (SDL offscreen picks by index, "
                        "not DRI_PRIME: eglinfo -B lists them; 0 is the integrated GPU here)")
    parser.add_argument("--scene", action="append", default=[])
    parser.add_argument("--view", action="append", default=[], help="only these views (all when none)")
    parser.add_argument("--modes", default="off,shadows,ao,glow")
    parser.add_argument("--qualities", default="", help="each RT mode at each of these (lab \"quality\"); "
                        "empty: the build's own (a build before the setting)")
    parser.add_argument("--size", default="1280x800")
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--settle", type=float, default=3.0)
    parser.add_argument("--no-shots", action="store_true")
    parser.add_argument("--no-time", action="store_true")
    parser.add_argument("--env", action="append", default=[])
    args = parser.parse_args()

    out = args.out / args.label
    out.mkdir(parents=True, exist_ok=True)
    work = args.out / f"work-{args.label}"
    work.mkdir(parents=True, exist_ok=True)
    env = {"DRI_PRIME": args.gpu, "SDL_HINT_EGL_DEVICE": args.egl_device, "HALO_EXIT_AFTER": "3000"}
    env.update(dict(item.split("=", 1) for item in args.env))
    qualities = [q for q in args.qualities.split(",") if q]
    modes = []
    for mode in args.modes.split(","):
        if qualities and mode not in ("off", "glow"):
            modes += [(f"{mode}_{q}", MODES[mode] + [f"quality {q}"]) for q in qualities]
        else:
            modes.append((mode, MODES[mode]))
    tsv = out / "times.tsv"
    keys = ("frames", "avg_ms", "p50_ms", "p95_ms", "p99_ms", "p999_ms", "worst_ms", "worst_stretch_ms", "spikes15",
            "spikes2", "over33", "gpu_frame_ms", "ml_gpu_ms",
            "rt_gpu_ms", "vk_build_ms", "vk_trace_ms", "pass_depth_copy", "pass_prepass", "pass_trace", "pass_upsample",
            "pass_temporal", "pass_blur", "pass_scene_copy", "pass_composite", "pass_reflections", "pass_bloom",
            "pass_bloom_bright", "pass_bloom_shafts", "pass_bloom_down", "pass_bloom_up", "pass_bloom_combine",
            "cpu_ml_ms", "cpu_trace_ms", "cpu_scene_ms")
    if not tsv.exists():
        tsv.write_text("\t".join(("scene", "view", "mode") + keys) + "\n")
    for scene_name in (args.scene or list(SCENES)):
        scene = SCENES[scene_name]
        run = lab.Run(args.build.resolve(), args.maps.resolve(), scene_name, work, args.size, env)
        try:
            print(f"{scene_name}: starting", flush=True)
            run.start()
            if scene["fast"]:
                run.console(["(game_speed 10)"])
                time.sleep(scene["fast"])
                run.console(["(game_speed 1)"])
            run.console(["(cinematic_show_letterbox 0)", "(show_hud 0)", "(show_hud_help_text 0)",
                         "(set cheat_deathless_player true)", "(set cheat_infinite_ammo true)"])
            time.sleep(3)
            for kind, value in scene["setup"]:
                if kind == "pose":
                    apply_pose(run, value)
                elif kind == "hs":
                    run.console(value)
                elif kind == "wait":
                    time.sleep(value)
            for view_name, pose in scene["views"].items():
                if args.view and view_name not in args.view:
                    continue
                if pose == "player":
                    run.console(["(camera_control 0)", "(game_speed 1)"])
                else:
                    apply_pose(run, pose)
                    run.console(["debug_camera_load", "(game_speed 1)"])
                time.sleep(2)
                print(f"  {view_name}: {run.say('where')}", flush=True)
                if not args.no_time:
                    for mode, commands in modes:
                        run.lab_commands(commands)
                        settle(run, args.settle)
                        label = f"{scene_name}/{view_name}/{mode}"
                        fields = timed(run, label, args.frames)
                        with tsv.open("a") as file:
                            file.write("\t".join([scene_name, view_name, mode] + [fields.get(k, "") for k in keys]) + "\n")
                        print(f"    {mode}: avg {fields.get('avg_ms')} p99 {fields.get('p99_ms')} worst {fields.get('worst_ms')} "
                              f"worst30 {fields.get('worst_stretch_ms')} spikes2 {fields.get('spikes2')} "
                              f"trace {fields.get('pass_trace')} vk {fields.get('vk_build_ms')}/{fields.get('vk_trace_ms')}",
                              flush=True)
                        if mode.startswith(("shadows", "ao")):
                            print(f"      {run.say(f'say {mode}')}", flush=True)
                if not args.no_shots:
                    # (the same frame for every mode: the game paused)
                    run.console(["(game_speed 0)"])
                    time.sleep(1.0)
                    for mode, commands in modes:
                        run.lab_commands(commands)
                        time.sleep(2.5)
                        png = out / f"{scene_name}-{view_name}-{mode}.png"
                        print(f"    shot {mode}: {'saved' if run.shot(png) else 'NO PICTURE'}", flush=True)
                    run.console(["(game_speed 1)"])
            print(f"  {run.say('say end')}", flush=True)
        except (RuntimeError, OSError) as reason:
            print(f"{scene_name}: FAILED: {reason}", flush=True)
        finally:
            run.stop()
            run.keep_logs(out)
            run.clean()
    import shutil
    shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
