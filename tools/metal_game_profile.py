#!/usr/bin/env python3
"""Profile actual reference/candidate gameplay, not the offscreen backend fixture.

Hidden/offline launches require HALO_ALLOW_LAUNCH=1. Output contains isolated
configs/logs, executable hashes, exact commands and per-run window summaries.
Attribution and instrumentation-disabled frame runs are separate. Window p95s
are not pooled frame percentiles; scripted motion is not deterministic replay.
"""
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
from pathlib import Path

if __package__:
    from .metal_policy_benchmark import ROOT, run_with_progress
else:
    from metal_policy_benchmark import ROOT, run_with_progress

FRONTEND = ("call", "prepare_draw", "setup_streams", "gpu_draw", "stages_fill", "key",
            "shader lookup", "uniforms", "targets+memset", "raster", "mirror_range",
            "mirror_refresh", "stream upload")
BACKEND = ("pipeline lookup", "pass_begin", "depth_state", "visibility+dynamic state",
           "bind_stages", "bind_constants", "bind_attributes", "encode draw")


def warmed(rows):
    return rows[len(rows) // 3:]


def stage_summary(text, prefix, names):
    result = {}
    for name in names:
        rows = warmed(re.findall(re.escape(prefix) + r"\s*" + re.escape(name) +
                                 r"\s+(\d+) calls, (\d+) ns average, (\d+) us worst, "
                                 r"([\d.]+) ms a frame", text))
        if len(rows) < 3:
            raise ValueError(f"too few warmed windows for {prefix} {name}: {len(rows)}")
        result[name] = {"windows": len(rows), "median_ns_per_call": statistics.median(int(r[1]) for r in rows),
                        "median_ms_per_frame": statistics.median(float(r[3]) for r in rows),
                        "calls_in_retained_windows": sum(int(r[0]) for r in rows)}
    return result


def summarize(text, attribution, bots=0):
    if re.search(r"failed assertion|validation error|fault at|initialization failed|cannot compile|"
                 r"cannot create pipeline|pipeline failed|lighting failed|command buffer.*failed", text, re.I):
        raise ValueError("render failure; inspect run.log")
    if "Metal on " not in text or "renderer regression seed 12345678" not in text:
        raise ValueError("missing Metal device or fixed-seed hook")
    if "Metal API Validation Enabled" in text:
        raise ValueError("validation instrumentation contaminates timing")
    joins = len(re.findall(r"bots: [^\n]*joined", text))
    if joins != bots:
        raise ValueError(f"expected {bots} combat-bot joins, observed {joins}")
    sizes = re.findall(r"screen: \d+x\d+ drawn at (\d+)x(\d+)", text)
    marker = "network test: starting the game"
    if text.count(marker) != 1:
        raise ValueError("expected exactly one started game")
    text = text.split(marker, 1)[1]
    ticks = [int(t) for t in re.findall(r"network test: tick (\d+)", text)]
    if not ticks or max(ticks) < 600:
        raise ValueError("did not reach 20 seconds of gameplay")
    frames = warmed(re.findall(r"frame CPU ([\d.]+) ms, GPU ([\d.]+) ms "
                               r"\(medians; 95th ([\d.]+), ([\d.]+)\)", text))
    if len(frames) < 3:
        raise ValueError(f"too few warmed frame windows: {len(frames)}")
    frame = {key: statistics.median(float(r[i]) for r in frames) for i, key in enumerate(
        ("cpu_median_ms", "gpu_median_ms", "cpu_window_p95_ms", "gpu_window_p95_ms"))}
    result = {"bots_joined": joins, "max_tick": max(ticks), "frame": {**frame, "windows": len(frames)},
              "render_sizes": sorted({(int(width), int(height)) for width, height in sizes})}
    if not attribution:
        if re.search(r"draw cpu:\s*call\s+\d+ calls", text):
            raise ValueError("draw-stage timers enabled in throughput run")
        return result
    result["frontend"] = stage_summary(text, "chupathingyce: draw cpu:", FRONTEND)
    result["backend"] = stage_summary(text, "Metal draw cpu:", BACKEND)
    geometry = warmed(re.findall(r"frame (\d+): (\d+) draws, (\d+) immediate", text))
    if not geometry:
        raise ValueError("missing geometry counts")
    result["draws_per_frame"] = statistics.median(int(r[1]) + int(r[2]) for r in geometry)
    result["retained_frontend_frames"] = len(geometry) * 60
    binds = warmed(re.findall(r"Metal draw cpu: (\d+) attribute tables bound for (\d+) draws", text))
    result["attribute_binds_per_draw"] = statistics.median(int(a) / int(b) for a, b in binds if int(b))
    cache = {}
    for name in ("depth", "blend", "raster", "sampler", "fragment", "declaration", "shader key"):
        rows = warmed(re.findall(r"draw cache: " + re.escape(name) + r" (\d+) hits, (\d+) misses", text))
        hits, misses = (sum(int(r[i]) for r in rows) for i in (0, 1))
        if hits + misses:  # disabled domains report 0/0
            cache[name] = {"hits": hits, "misses": misses, "hit_fraction": hits / (hits + misses)}
    result["cache"] = cache
    return result


def aggregate(runs):
    """Median of independent run summaries, never pooled frame percentiles."""
    groups = {}
    for row in runs:
        key = "/".join(row[field] for field in ("scene", "mode", "variant"))
        groups.setdefault(key, []).append(row)
    summary = {}
    for key, rows in groups.items():
        result = {"runs": len(rows), "frame": {
            field: statistics.median(row["frame"][field] for row in rows)
            for field in rows[0]["frame"] if field != "windows"}}
        for domain in ("frontend", "backend"):
            if domain in rows[0]:
                result[domain + "_ns_per_call"] = {
                    stage: statistics.median(row[domain][stage]["median_ns_per_call"] for row in rows)
                    for stage in rows[0][domain]}
        if "draws_per_frame" in rows[0]:
            result["draws_per_frame"] = statistics.median(row["draws_per_frame"] for row in rows)
            result["attribute_binds_per_draw"] = statistics.median(row["attribute_binds_per_draw"] for row in rows)
            result["cache_hit_fractions"] = {
                name: statistics.median(row["cache"][name]["hit_fraction"] for row in rows)
                for name in rows[0]["cache"]}
        summary[key] = result
    return summary


def overrides(scene, attribution, bots=31):
    return {"HALO_BOTS": str(bots), "HALO_NETWORK_TEST_SCORE": "32767", "HALO_NETWORK_TEST_KILL": "0",
            "HALO_GPU_STATS": "1" if attribution else "0", "HALO_RENDER_REGRESSION": "1",
            "HALO_NETWORK_TEST": f"host:{scene}", "HALO_NETWORK_TEST_START": "2", "HALO_TEST_INPUT": "bot:1",
            "HALO_WINDOW_SIZE": "1280x720", "HALO_DISPLAY_MODE": "windowed", "HALO_RESOLUTION_SCALING": "native",
            "HALO_ANTI_ALIASING": "off", "HALO_PER_PIXEL_LIGHTING": "false", "HALO_FRAME_PACING": "off",
            "HALO_NO_VSYNC": "1", "HALO_MAX_FPS": "-1", "HALO_METAL_STATE_CACHE": "true",
            "MTL_DEBUG_LAYER": "0"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-app", type=Path, required=True)
    parser.add_argument("--candidate-app", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--maps", nargs="+", choices=("bloodgulch", "hangemhigh"), default=["bloodgulch", "hangemhigh"])
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--bots", type=int, default=31)
    parser.add_argument("--repeats", type=int, default=2, help="independent runs per variant, scene and mode")
    parser.add_argument("--modes", nargs="+", choices=("attribution", "throughput"), default=["attribution", "throughput"])
    parser.add_argument("--warm-lists", type=Path, help="copy identical learned shader lists into each isolated run")
    args = parser.parse_args(argv)
    if os.environ.get("HALO_ALLOW_LAUNCH") != "1" or platform.system() != "Darwin":
        parser.error("macOS and HALO_ALLOW_LAUNCH=1 required")
    if os.environ.get("METAL_DEVICE_WRAPPER_TYPE"):
        parser.error("clear METAL_DEVICE_WRAPPER_TYPE before timing")
    if not 45 <= args.seconds <= 300 or not 1 <= args.repeats <= 5 or not 1 <= args.bots <= 31:
        parser.error("seconds must be 45..300, repeats 1..5, bots 1..31")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("output must be empty")
    apps = {"reference": args.reference_app.resolve(), "candidate": args.candidate_app.resolve()}
    for app in apps.values():
        try:
            executable = app.is_file() and os.access(app, os.X_OK)
        except OSError as error:
            parser.error(f"cannot inspect executable {app}: {error}")
        if not executable:
            parser.error(f"missing executable: {app}")
    output.mkdir(parents=True, exist_ok=True)
    report = {"commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "dirty": subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True),
              "system": platform.platform(), "seconds": args.seconds, "bots": args.bots,
              "apps": {name: {"path": str(app), "sha256": hashlib.sha256(app.read_bytes()).hexdigest()}
                       for name, app in apps.items()}, "runs": []}
    if args.warm_lists:
        report["warm_lists_sha256"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                       for p in args.warm_lists.iterdir() if p.is_file()}
    total = len(args.maps) * len(args.modes) * args.repeats * 2
    for scene in args.maps:
        for mode in args.modes:
            for repeat in range(args.repeats):
                # ABBA with two repeats; balanced AB/BA pairs for larger runs.
                order = ("reference", "candidate") if repeat % 2 == 0 else ("candidate", "reference")
                for variant in order:
                    folder = output / f"{scene}-{mode}-{repeat + 1}-{variant}"
                    if args.warm_lists:
                        shutil.copytree(args.warm_lists, folder / "data/metal-shader-lists")
                    options = overrides(scene, mode == "attribution", args.bots)
                    command = [str(ROOT / "tools/metal_scripted_run.sh"), str(folder), str(args.seconds)]
                    command += [f"{key}={value}" for key, value in options.items()]
                    print(f"RUN {len(report['runs']) + 1}/{total}: {scene}, {variant}, {mode}", flush=True)
                    samples = run_with_progress(command, folder, args.seconds,
                                                {**os.environ, "HALO_APP": str(apps[variant])}, interval=15)
                    text = (folder / "run.log").read_text(errors="replace")
                    summary = summarize(text, mode == "attribution", args.bots)
                    row = {"scene": scene, "variant": variant, "mode": mode, "repeat": repeat + 1,
                           "command": command, "folder": str(folder), "process_samples": samples, **summary}
                    report["runs"].append(row)
                    report["summary"] = aggregate(report["runs"])
                    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
                    frame = summary["frame"]
                    detail = f"CPU/GPU {frame['cpu_median_ms']:.2f}/{frame['gpu_median_ms']:.2f} ms"
                    if mode == "attribution":
                        detail += (f"; Draw* {summary['frontend']['call']['median_ns_per_call']:.0f} ns/call"
                                   f"; {summary['draws_per_frame']:.0f} draws/frame")
                    print(f"RESULT {scene}/{variant}/{mode}: {detail}; {summary['bots_joined']} combat bots; "
                          f"tick {summary['max_tick']}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
