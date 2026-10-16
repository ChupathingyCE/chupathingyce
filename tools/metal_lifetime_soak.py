#!/usr/bin/env python3
"""Offline Metal API-validation soak or in-process map playlist, with progress.

Uses a prebuilt bot-enabled executable. RSS is process memory, not GPU residency.
No synthetic delayed GPU completion or image-parity proof is provided.
"""
import argparse
import hashlib
import json
import os
import re
import runpy
import statistics
import subprocess
import sys
from pathlib import Path

# Both tools are standalone CLIs; load helpers without an installed package.
benchmark = runpy.run_path(str(Path(__file__).with_name("metal_policy_benchmark.py")))
ROOT = benchmark["ROOT"]
run_with_progress = benchmark["run_with_progress"]


def validate_log(text, maps, bots, *, require_validation_banner=True):
    if require_validation_banner and "Metal API Validation Enabled" not in text:
        raise ValueError("Metal API Validation was not enabled")
    for line in text.splitlines():
        if any(token in line.lower() for token in
               ("failed assertion", "validation error", "fault at", "initialization failed",
                "cannot create pipeline", "cannot compile", "pipeline failed", "lighting failed")) \
                or re.search(r"command buffer.*failed", line, re.I):
            raise ValueError("rendering failure: " + line[:300])
    joins = len(re.findall(r"bots: [^\n]*joined", text))
    if joins < bots:
        raise ValueError(f"only {joins} bot joins, expected at least {bots}")
    rounds = list(re.finditer(r"network test: game (\d+), slayer", text))
    if len(rounds) != len(maps):
        raise ValueError(f"expected {len(maps)} games, observed {len(rounds)}")
    reports = []
    for index, (marker, name) in enumerate(zip(rounds, maps, strict=True)):
        segment = text[marker.end():rounds[index + 1].start() if index + 1 < len(rounds) else len(text)]
        ticks = [int(value) for value in re.findall(r"network test: tick (\d+)", segment)]
        draws = [int(value) for value in re.findall(r"frame \d+: (\d+) draws", segment)]
        named = sorted({int(value) for value in re.findall(r"network test: player (\d+) is named", segment)})
        if named != list(range(bots + 1)):
            raise ValueError(f"round {index + 1}: expected {bots + 1} player records, observed {named}")
        if "renderer regression seed 12345678" not in segment:
            raise ValueError(f"round {index + 1}: missing fixed-seed hook")
        if int(marker[1]) != index + 1 or "network test: starting the game" not in segment:
            raise ValueError(f"round {index + 1}: no game start")
        if f"shader list {name}:" not in segment or not ticks or max(ticks) < 150 or not draws or max(draws) < 100:
            raise ValueError(f"round {index + 1}/{name}: no confirmed loaded/rendered gameplay")
        reports.append({"game": index + 1, "map": name, "max_tick": max(ticks),
                        "tick_reports": len(ticks), "max_draws": max(draws), "players_named": named,
                        "rename_events_reported": sum(int(value) for value in re.findall(
                            r"Metal: (\d+) renames in the last 600 frames", segment))})
    return {"bots_joined": joins, "rounds": reports}


def memory_summary(samples):
    values = [sum(child["rss_kib"] for child in row["children"]) for row in samples if row["children"]]
    if len(values) < 3:
        raise ValueError("too few process RSS samples")
    width = max(1, len(values) // 3)
    return {"samples": len(values), "peak_mib": max(values) / 1024,
            "first_third_median_mib": statistics.median(values[:width]) / 1024,
            "last_third_median_mib": statistics.median(values[-width:]) / 1024,
            "note": "Direct-child process RSS from ps; includes CPU/driver/cache memory, not a GPU residency or leak audit."}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=300)
    parser.add_argument("--maps", nargs="+", choices=("bloodgulch", "hangemhigh", "sidewinder"), default=["hangemhigh"])
    parser.add_argument("--bots", type=int, default=31)
    parser.add_argument("--score", type=int, help="defaults to 32767 for a soak, 1 for a playlist")
    parser.add_argument("--kill-interval", type=int,
                        help="scripted scoring kill interval; defaults to 0 for soak, 15s for playlist")
    args = parser.parse_args(argv)
    if os.environ.get("HALO_ALLOW_LAUNCH") != "1":
        parser.error("HALO_ALLOW_LAUNCH=1 is required")
    if not 60 <= args.seconds <= 1800 or not 1 <= args.bots <= 31:
        parser.error("seconds must be 60..1800, bots 1..31")
    score = args.score if args.score is not None else (32767 if len(args.maps) == 1 else 1)
    if not 1 <= score <= 32767:
        parser.error("score must be 1..32767")
    kill_interval = args.kill_interval if args.kill_interval is not None else (0 if len(args.maps) == 1 else 15)
    if not 0 <= kill_interval <= 300:
        parser.error("kill interval must be 0..300")
    playlist = "+".join(args.maps) + ":" + ",".join(["slayer"] * len(args.maps))
    if len(playlist) >= 64:
        parser.error("playlist exceeds the network fixture's 64-byte setting buffer")
    app, output = args.app.resolve(), args.output.resolve()
    try:
        executable = app.is_file() and os.access(app, os.X_OK)
    except OSError as error:
        parser.error(f"cannot inspect executable {app}: {error}")
    if not executable:
        parser.error(f"missing executable: {app}")
    if output.exists() and any(output.iterdir()):
        parser.error("output must be empty")
    output.mkdir(parents=True, exist_ok=True)
    overrides = {"HALO_BOTS": str(args.bots), "HALO_GPU_STATS": "1", "HALO_RENDER_REGRESSION": "1",
                 "HALO_NETWORK_TEST": "host:" + playlist, "HALO_NETWORK_TEST_START": "2",
                 "HALO_NETWORK_TEST_SCORE": str(score), "HALO_NETWORK_TEST_KILL": str(kill_interval),
                 "HALO_TEST_INPUT": "bot:1",
                 "HALO_WINDOW_SIZE": "1280x720", "HALO_DISPLAY_MODE": "windowed",
                 "HALO_RESOLUTION_SCALING": "native", "HALO_ANTI_ALIASING": "msaa4x",
                 "HALO_PER_PIXEL_LIGHTING": "false", "HALO_FRAME_PACING": "off",
                 "HALO_NO_VSYNC": "1", "HALO_MAX_FPS": "-1", "MTL_DEBUG_LAYER": "1"}
    report = {"app": str(app), "sha256": hashlib.sha256(app.read_bytes()).hexdigest(),
              "seconds": args.seconds, "maps": args.maps, "overrides": overrides}
    (output / "fixture.json").write_text(json.dumps(report, indent=2) + "\n")
    command = [str(ROOT / "tools/metal_scripted_run.sh"), str(output), str(args.seconds)]
    command.extend(f"{key}={value}" for key, value in overrides.items())
    samples = run_with_progress(command, output, args.seconds, {**os.environ, "HALO_APP": str(app)})
    report.update(validate_log((output / "run.log").read_text(errors="replace"), args.maps, args.bots))
    # The debug log lacks Apple's layer startup banner, but must pass all other checks.
    debug = (output / "debug.txt").read_text(errors="replace")
    validate_log(debug, args.maps, args.bots, require_validation_banner=False)
    if len(args.maps) == 1 and report["rounds"][0]["max_tick"] < (args.seconds - 30) * 30:
        raise ValueError("soak did not sustain gameplay for the requested duration")
    report["process_rss"] = memory_summary(samples)
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {len(report['rounds'])} map(s) rendered; "
          f"peak process RSS {report['process_rss']['peak_mib']:.0f} MiB; details {output / 'results.json'}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}; inspect retained output logs", file=sys.stderr)
        raise SystemExit(1) from error
