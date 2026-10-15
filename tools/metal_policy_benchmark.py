#!/usr/bin/env python3
"""Offline, hidden ABBA benchmark of prebuilt bot-enabled Metal executables.

Reports medians of warmed reporting windows, not pooled frame percentiles.
Requires macOS, game data and HALO_ALLOW_LAUNCH=1. Never builds or edits config.
"""
import argparse
import contextlib
import hashlib
import json
import os
import re
import signal
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STAGES = ("call", "setup_streams", "mirror_range", "mirror_refresh", "stream upload")


def median(values):
    return statistics.median(values) if values else None


def warmed(rows):
    return rows[len(rows) // 3:]


def summarize(text, bots):
    joins = len(re.findall(r"bots: [^\n]*joined", text))
    if joins != bots:
        raise ValueError(f"expected {bots} bot joins, saw {joins}")
    if "renderer regression seed 12345678" not in text:
        raise ValueError("missing fixed-seed hook")
    errors = [line for line in text.splitlines() if any(token in line.lower() for token in
              ("failed assertion", "validation error", "fault at", "initialization failed",
               "cannot create pipeline", "cannot compile", "pipeline failed", "lighting failed"))
              or re.search(r"command buffer.*failed", line, re.I)]
    if errors:
        raise ValueError("rendering error: " + errors[0])
    stages = {}
    for stage in STAGES:
        rows = warmed(re.findall(r"draw cpu:\s*" + re.escape(stage) +
                                r"\s+(\d+) calls, (\d+) ns average, \d+ us worst, ([\d.]+) ms a frame", text))
        if not rows:
            raise ValueError(f"missing draw profiling: {stage}")
        stages[stage] = {"windows": len(rows), "median_ns_per_call": median([float(r[1]) for r in rows]),
                         "median_ms_per_frame": median([float(r[2]) for r in rows])}
    timing = warmed(re.findall(r"frame CPU ([\d.]+) ms, GPU ([\d.]+) ms \(medians; 95th ([\d.]+), ([\d.]+)\)", text))
    if len(timing) < 3:
        raise ValueError("too few warmed whole-frame timing windows; run longer")
    keys = ("cpu_median_ms", "gpu_median_ms", "cpu_p95_ms", "gpu_p95_ms")
    frame = {key: median([float(row[i]) for row in timing]) for i, key in enumerate(keys)}
    frame["windows"] = len(timing)
    geometry = warmed(re.findall(r"frame \d+: (\d+) draws, (\d+) immediate, [^\n]*?; (\d+) KB mirrored, (\d+) KB streamed", text))
    if not geometry:
        raise ValueError("missing geometry counters")
    counts = {key: median([int(row[i]) for row in geometry]) for i, key in enumerate(
        ("draws", "immediate", "mirrored_reference_kib", "streamed_kib"))}
    return {"bots_joined": joins, "stages": stages, "frame_window_medians": frame, "geometry_window_medians": counts}


def tail_text(path, size=65536):
    """Bound heartbeat I/O even when a long run produces a large log."""
    try:
        with path.open("rb") as stream:
            stream.seek(0, os.SEEK_END)
            stream.seek(max(0, stream.tell() - size))
            return stream.read(size).decode(errors="replace")
    except FileNotFoundError:
        return ""


def child_rss(pid):
    """RSS of the shell runner's direct child (exec'd game), not GPU residency."""
    try:
        result = subprocess.run(["ps", "-axo", "pid=,ppid=,rss="], check=True,
                                capture_output=True, text=True, timeout=3)
        children = [tuple(map(int, row.split())) for row in result.stdout.splitlines() if row.strip()]
        return [{"pid": child, "rss_kib": rss} for child, parent, rss in children if parent == pid]
    except (OSError, ValueError, subprocess.SubprocessError):
        return []


def run_with_progress(command, folder, seconds, env, *, interval=30):
    """Supervise one run with flushed heartbeats, RSS samples and group cleanup."""
    started = time.monotonic()
    samples = []
    print(f"START {folder.name}: {seconds}s; logs {folder / 'run.log'}", flush=True)
    with subprocess.Popen(command, cwd=ROOT, env=env, start_new_session=True) as process:
        try:
            while True:
                elapsed = time.monotonic() - started
                remaining = seconds + 110 - elapsed
                if remaining <= 0:
                    raise subprocess.TimeoutExpired(command, seconds + 110)
                try:
                    status = process.wait(timeout=min(interval, remaining))
                    break
                except subprocess.TimeoutExpired:
                    text = tail_text(folder / "run.log")
                    ticks = re.findall(r"network test: tick (\d+)", text)
                    frames = re.findall(r"frame (\d+): (\d+) draws", text)
                    sample = {"elapsed_seconds": round(time.monotonic() - started, 1),
                              "children": child_rss(process.pid),
                              "last_tick": ticks[-1] if ticks else None,
                              "last_frame_draws": frames[-1] if frames else None}
                    samples.append(sample)
                    folder.mkdir(parents=True, exist_ok=True)
                    (folder / "progress.json").write_text(json.dumps(samples, indent=2) + "\n")
                    print(f"  {folder.name} {sample['elapsed_seconds']:.0f}/{seconds}s: running", flush=True)
            if status:
                raise subprocess.CalledProcessError(status, command)
        except BaseException:
            # Never orphan the game on Ctrl-C, timeout or a failed progress probe.
            with contextlib.suppress(ProcessLookupError):
                os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
            raise
    print(f"DONE {folder.name}: {time.monotonic() - started:.1f}s, exit {status}", flush=True)
    return samples


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-app", type=Path, required=True)
    parser.add_argument("--candidate-app", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=75)
    parser.add_argument("--bots", type=int, default=31)
    parser.add_argument("--map", choices=("bloodgulch", "hangemhigh", "sidewinder"), default="bloodgulch")
    args = parser.parse_args(argv)
    if os.environ.get("HALO_ALLOW_LAUNCH") != "1":
        parser.error("HALO_ALLOW_LAUNCH=1 is required")
    if not 45 <= args.seconds <= 300 or not 1 <= args.bots <= 31:
        parser.error("seconds must be 45..300 and bots 1..31")
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
    overrides = {"HALO_BOTS": str(args.bots), "HALO_GPU_STATS": "1", "HALO_RENDER_REGRESSION": "1",
                 "HALO_WINDOW_SIZE": "1280x720", "HALO_DISPLAY_MODE": "windowed",
                 "HALO_NETWORK_TEST": f"host:{args.map}", "HALO_NETWORK_TEST_START": "2",
                 "HALO_TEST_INPUT": "bot:1", "HALO_FRAME_PACING": "off", "MTL_DEBUG_LAYER": "0",
                 "HALO_NO_VSYNC": "1", "HALO_MAX_FPS": "-1", "HALO_ANTI_ALIASING": "off",
                 "HALO_RESOLUTION_SCALING": "native", "HALO_PER_PIXEL_LIGHTING": "false"}
    report = {"apps": {name: {"path": str(app), "sha256": hashlib.sha256(app.read_bytes()).hexdigest()}
                       for name, app in apps.items()}, "seconds": args.seconds, "overrides": overrides,
              "notes": "First third of windows discarded independently per metric; p95 values are medians of window p95s, not pooled p95s. Moving bot scenes are not deterministic replays. Mirrored bytes are references, not uploads.",
              "runs": []}
    for index, name in enumerate(("reference", "candidate", "candidate", "reference"), 1):
        folder = output / f"{index}-{name}"
        env = {**os.environ, "HALO_APP": str(apps[name])}
        command = [str(ROOT / "tools/metal_scripted_run.sh"), str(folder), str(args.seconds)]
        command.extend(f"{key}={value}" for key, value in overrides.items())
        run_with_progress(command, folder, args.seconds, env)
        # Check both logs for errors, but parse only debug.txt to avoid double counting.
        text = (folder / "debug.txt").read_text(errors="replace")
        summary = summarize(text, args.bots)
        run_log = (folder / "run.log").read_text(errors="replace")
        summarize(run_log, args.bots)
        report["runs"].append({"variant": name, "folder": str(folder), **summary})
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        print(name, summary["frame_window_medians"], flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}; inspect retained output logs", file=sys.stderr)
        raise SystemExit(1) from error
