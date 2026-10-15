#!/usr/bin/env python3
"""Offscreen backend-only V1/V2 repeated-material A/B, no game/window required.

Compiles the actual gpu_metal.m into a small fixture. Submission timings have
instrumentation disabled; lookup/binding counters use a separate untimed batch.
GPU completion/readback, compilation and clears are outside the timed region.
Not a DrawVertices/frontend benchmark or a game-wide performance claim.
Exit 77: macOS/toolchain/Metal device unavailable. No performance threshold is
asserted; image digests, command counts and resource/encoder transitions must pass.
"""
import argparse
import hashlib
import json
import os
import platform
import random
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = ("v1", "v2-native-off", "v2")


def build_probe(destination, v2):
    command = ["xcrun", "clang", "-std=gnu11", "-fobjc-arc", "-O2", "-g", "-Wall",
               "-Wno-unused-function", "-mmacosx-version-min=13.0",
               "-Iport/macos/metal", "-Iport/macos/metal/renderer",
               "tools/metal_draw_probe.m", "-framework", "Metal", "-framework", "Foundation",
               "-framework", "QuartzCore", "-framework", "MetalFX", "-Wl,-dead_strip", "-o", str(destination)]
    if v2:
        command.append("-DHALO_MACOS_METAL_V2=1")
    subprocess.run(command, cwd=ROOT, check=True)
    return command


def summarize(records):
    result = {}
    for record in records:
        key = record["variant"] + "/" + record["workload"]
        group = result.setdefault(key, {"run_medians_ns": [], "digest": record["digest"],
                                      "draws": record["draws"]})
        if record["digest"] != group["digest"]:
            raise ValueError("non-repeatable image digest: " + key)
        group["run_medians_ns"].append(statistics.median(record["ns_per_draw"]))
        group["lookups"] = {field: record[field] for field in
                            ("shader_lookups", "pipeline_lookups", "depth_lookups", "attribute_binds")}
    for key, group in result.items():
        group["median_ns"] = statistics.median(group["run_medians_ns"])
        workload = key.split("/", 1)[1]
        digests = {r["digest"] for r in records if r["workload"] == workload}
        if len(digests) != 1:
            raise ValueError("V1/V2 image mismatch: " + workload)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--validation", action="store_true", help="Metal API Validation; timings are not performance data")
    args = parser.parse_args(argv)
    if not 1 <= args.rounds <= 30:
        parser.error("--rounds must be between 1 and 30")
    if platform.system() != "Darwin" or not shutil.which("xcrun"):
        print("SKIP: macOS/Xcode required", file=sys.stderr)
        return 77
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    commands = {"v1": build_probe(output / "v1", False), "v2": build_probe(output / "v2", True)}
    env = os.environ.copy()
    # Do not accidentally report validation-instrumented CPU timing as normal.
    if not args.validation and (env.get("MTL_DEBUG_LAYER") == "1" or env.get("METAL_DEVICE_WRAPPER_TYPE")):
        parser.error("validation environment set; use --validation or clear it")
    if args.validation:
        env["MTL_DEBUG_LAYER"] = "1"
    records, orders = [], []
    randomizer = random.Random(0)
    for trial in range(args.rounds):
        order = list(VARIANTS)
        randomizer.shuffle(order)
        orders.append(order)
        for variant in order:
            command = [str(output / ("v1" if variant == "v1" else "v2"))]
            if variant == "v2-native-off":
                command.append("--native-off")
            run = subprocess.run(command, cwd=ROOT, env=env, text=True, capture_output=True, timeout=120)
            (output / f"{trial}-{variant}.log").write_text(run.stderr)
            (output / f"{trial}-{variant}.jsonl").write_text(run.stdout)
            if run.returncode == 77:
                print("SKIP: no Metal device", file=sys.stderr)
                return 77
            if run.returncode:
                raise RuntimeError(f"{variant} fixture failed ({run.returncode}): {run.stderr}")
            if args.validation and "Metal API Validation Enabled" not in run.stderr:
                raise RuntimeError("Metal API Validation banner missing")
            for line in run.stdout.splitlines():
                try:
                    record = json.loads(line)
                except json.JSONDecodeError as error:
                    raise RuntimeError(f"{variant} emitted invalid probe JSON: {line!r}") from error
                record.update(variant=variant, trial=trial)
                records.append(record)
    summary = {"scope": "offscreen gpu_metal_draw CPU submission only; not frontend/game FPS",
               "validation": args.validation, "system": platform.platform(), "machine": platform.machine(),
               "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
               "dirty": subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True),
               "source_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                 for path in (ROOT / "tools/metal_draw_probe.m", ROOT / "port/macos/metal/gpu_metal.m",
                                              ROOT / "port/macos/metal/metal_attribute_table.h",
                                              ROOT / "port/macos/metal/metal_state_cache.h",
                                              ROOT / "port/macos/metal/renderer/draw_profile.h")},
               "build_commands": commands, "trial_order": orders, "results": summarize(records)}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    for workload in ("same-state", "changing-constants", "changing-ranges"):
        rows = summary["results"]
        values = [rows[v + "/" + workload]["median_ns"] for v in VARIANTS]
        print(f"{workload}: V1 {values[0]:.1f}, V2/native-off {values[1]:.1f}, V2 {values[2]:.1f} ns/draw")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
