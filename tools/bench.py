#!/usr/bin/env python3
"""The fixed benchmark: the same stretch of a Blood Gulch game on any build
and either renderer, for the bgfx against OpenGL frame-time gate and for
before/after comparisons of one change.

The game is a determinism replay (port/linux/game/determinism_check.c,
tools/test_determinism.py): a recording of a solo Slayer game whose player
is the scripted bot, played back with no input of its own, so every run
plays the same ticks whatever its frame rate. The first run records it
(headless) unless --inputs names one; keep the file to compare builds over
time. Each run draws in a window of a fixed size with V-Sync off and no
frame rate limit (display.max_fps -1), and writes a line for each frame
(debug.frame_log: the tick, the frame's microseconds, the game thread's CPU
microseconds); frames from --warmup ticks on are counted.

    python3 tools/bench.py --maps ~/halo-maps/maps                       # both renderers, this build
    python3 tools/bench.py --maps ~/halo-maps/maps --renderer gl --runs 3
    python3 tools/bench.py --maps ~/halo-maps/maps --binary old/chupathingy --binary build/macos/chupathingy \\
        --renderer gl --inputs bench/inputs.bin          # two builds, the same game

It prints, for each build and renderer, the median over the runs of the
frame times (mean, p50, p95, p99), the average frame rate, the 1% low (the
frame rate of the slowest 1% of frames) and the game thread's CPU a frame;
--json writes them. The copies are off the network, muted, with their own
data, save and home folders (--work). bgfx is in the 64-bit desktop builds
(Metal on the Mac, Vulkan elsewhere); a build without it falls back to
OpenGL, which the log says, and the run is marked so.

Based on work by goatbutt and the OpenH1 team (their tools/bench.py).
"""
import argparse
import json
import os
import re
import shutil
import statistics
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import test_determinism as determinism  # noqa: E402

DEFAULT_TICKS = 1800  # a minute of play
DEFAULT_WARMUP = 300  # its first ten seconds left out
DEFAULT_SIZE = "1280x960"


def default_binary():
    for path in ("build/macos/chupathingy", "build/linux64/chupathingy", "build/linux/chupathingy"):
        if os.path.exists(os.path.join(ROOT, path)):
            return os.path.join(ROOT, path)
    return os.path.join(ROOT, "build", "macos", "chupathingy")


def read_frame_log(path, warmup):
    """Returns [(frame_us, cpu_us)] of the frames at or past the warmup tick."""
    frames = []
    with open(path) as file:
        for line in file:
            if line.startswith("#"):
                continue
            fields = line.split()
            if len(fields) < 3:
                continue
            tick, micros, cpu = int(fields[0]), int(fields[1]), int(fields[2])
            if tick >= warmup and micros > 0:
                frames.append((micros, cpu))
    return frames


def percentile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        return 0.0
    index = min(len(ordered) - 1, max(0, int(round(fraction * (len(ordered) - 1)))))
    return float(ordered[index])


def summarize(frames):
    """The frame statistics of a run, in milliseconds and frames a second."""
    times = [micros / 1000.0 for micros, _ in frames]
    cpus = [cpu / 1000.0 for _, cpu in frames if cpu >= 0]
    if not times:
        return None
    slowest = sorted(times, reverse=True)[:max(1, len(times) // 100)]
    return {
        "frames": len(times),
        "mean_ms": statistics.fmean(times),
        "p50_ms": percentile(times, 0.50),
        "p95_ms": percentile(times, 0.95),
        "p99_ms": percentile(times, 0.99),
        "fps": 1000.0 / statistics.fmean(times),
        "low_1pct_fps": 1000.0 / statistics.fmean(slowest),
        "cpu_ms": statistics.fmean(cpus) if cpus else -1.0,
    }


def median_of(runs):
    """The median of each statistic over the runs."""
    runs = [run for run in runs if run]
    if not runs:
        return None
    return {key: statistics.median(run[key] for run in runs) for key in runs[0]}


def record(binary, maps, work, ticks, seed):
    """Records the benchmark's game (headless, the scripted bot playing)."""
    inputs = os.path.join(os.path.abspath(work), "inputs.bin")
    determinism.log("bench: recording the game")
    determinism.play(binary, maps, work, "record", "record", inputs, determinism.DEFAULT_MAP, ticks, seed)
    return inputs


def run(binary, maps, work, name, inputs, renderer, ticks, seed, size):
    """Plays the recording once in a window on a renderer; returns the frame
    log's path and whether the renderer asked for was the one that drew."""
    folder = os.path.join(work, name)
    frame_log = os.path.join(os.path.abspath(folder), "frames.txt")
    extra = {
        "HALO_FRAME_LOG": frame_log,
        "HALO_RENDERER": renderer,
        "HALO_DISPLAY_MODE": "windowed",
        "HALO_WINDOW_SIZE": size,
        "HALO_RESOLUTION_SCALING": "native",
        "HALO_ANTI_ALIASING": "msaa2x",  # (bgfx has MSAA, not FXAA or SMAA)
        "HALO_NO_VSYNC": "1",
        "HALO_MAX_FPS": "-1",
        "HALO_VRR": "off",
    }
    # (a real window: the determinism check's copies draw nowhere)
    original = determinism.copy_environment

    def windowed(folder_path, maps_path, address):
        environment = original(folder_path, maps_path, address)
        environment.pop("SDL_VIDEO_DRIVER", None)
        return environment

    determinism.copy_environment = windowed
    try:
        determinism.play(binary, maps, work, name, "replay", inputs, determinism.DEFAULT_MAP, ticks, seed,
            bot=False, extra_env=extra)
    finally:
        determinism.copy_environment = original
    fell_back = False
    log_path = os.path.join(folder, "out.log")
    if renderer != "gl" and os.path.exists(log_path):
        # (bgfx says which backend drew: "bgfx: Metal on ...", "bgfx: Vulkan on ...")
        with open(log_path, errors="replace") as file:
            fell_back = not re.search(r"bgfx: [\w ]+ on ", file.read())
    return frame_log, fell_back


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", action="append", help="a build to measure (more than once: each)")
    parser.add_argument("--maps", required=True, help="a folder of the stock Xbox maps (NTSC)")
    parser.add_argument("--renderer", action="append", choices=["gl", "bgfx", "d3d11"],
        help="the renderer(s) to measure (default: gl and bgfx)")
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--ticks", type=int, default=DEFAULT_TICKS)
    parser.add_argument("--warmup", type=int, default=DEFAULT_WARMUP, help="ticks left out at the start")
    parser.add_argument("--seed", type=int, default=determinism.DEFAULT_SEED)
    parser.add_argument("--size", default=DEFAULT_SIZE, help="the window's size")
    parser.add_argument("--inputs", help="a recording to play (default: record one in --work)")
    parser.add_argument("--work", default=os.path.join(ROOT, "build", "bench"))
    parser.add_argument("--json", help="a file to write the results to")
    args = parser.parse_args()
    binaries = args.binary or [default_binary()]
    renderers = args.renderer or ["gl", "bgfx"]
    if not os.path.exists(os.path.join(args.maps, f"{determinism.DEFAULT_MAP}.map")):
        determinism.log(f"bench: no {determinism.DEFAULT_MAP}.map in {args.maps}")
        return 1
    if args.warmup >= args.ticks:
        determinism.log("bench: --warmup must be under --ticks")
        return 1
    os.makedirs(args.work, exist_ok=True)
    try:
        inputs = args.inputs or record(binaries[0], args.maps, args.work, args.ticks, args.seed)
        results = []
        for build_index, binary in enumerate(binaries):
            for renderer in renderers:
                summaries, fell_back = [], False
                for number in range(args.runs):
                    name = f"build{build_index}-{renderer}-{number}"
                    # (a run that stalls, as when another copy on the machine
                    # holds the system link port, is played once more)
                    for attempt in range(2):
                        shutil.rmtree(os.path.join(args.work, name), ignore_errors=True)
                        try:
                            frame_log, fell = run(binary, args.maps, args.work, name, inputs, renderer,
                                args.ticks, args.seed, args.size)
                        except RuntimeError as error:
                            determinism.log(f"bench: {error}{': again' if not attempt else ': left out'}")
                            continue
                        fell_back = fell_back or fell
                        summaries.append(summarize(read_frame_log(frame_log, args.warmup)))
                        break
                results.append({"binary": binary, "renderer": renderer, "fell_back_to_gl": fell_back,
                    "runs": len(summaries), **(median_of(summaries) or {})})
    except RuntimeError as error:
        determinism.log(f"bench: {error}")
        return 1
    print(f"{'build':<6} {'renderer':<9} {'frames':>7} {'mean':>7} {'p50':>7} {'p95':>7} {'p99':>7} "
        f"{'fps':>7} {'1% low':>7} {'cpu':>7}")
    for index, result in enumerate(results):
        build = binaries.index(result["binary"])
        if "frames" not in result:
            print(f"{build:<6} {result['renderer']:<9} no frames logged")
            continue
        mark = " (fell back to OpenGL)" if result["fell_back_to_gl"] else ""
        print(f"{build:<6} {result['renderer']:<9} {result['frames']:>7.0f} {result['mean_ms']:>6.2f}ms "
            f"{result['p50_ms']:>5.2f}ms {result['p95_ms']:>5.2f}ms {result['p99_ms']:>5.2f}ms "
            f"{result['fps']:>7.1f} {result['low_1pct_fps']:>7.1f} {result['cpu_ms']:>5.2f}ms{mark}")
    for index, binary in enumerate(binaries):
        print(f"build {index}: {binary}")
    if args.json:
        with open(args.json, "w") as file:
            json.dump({"inputs": inputs, "ticks": args.ticks, "warmup": args.warmup, "size": args.size,
                "results": results}, file, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
