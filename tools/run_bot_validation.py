"""Collect an isolated, hidden offline bot run with an external wall-clock watchdog.

This runner records evidence, not a movement or navigation pass verdict.
"""
import argparse
import hashlib
import json
import os
import signal
import subprocess
import time
from contextlib import suppress
from datetime import UTC, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run(args):
    binary = args.binary.resolve(strict=True)
    maps = (args.source_data / "maps").resolve(strict=True)
    for name in ("ui.map", f"{args.map}.map"):
        if not (maps / name).is_file():
            raise ValueError(f"missing Xbox map: {maps / name}")
    stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S-%fZ")
    output = args.output_root.resolve() / f"{args.map}-{args.bots}bots-{stamp}"
    data = output / "data"
    saves = output / "saves"
    data.mkdir(parents=True)
    saves.mkdir()
    (data / "maps").symlink_to(maps, target_is_directory=True)
    environment = os.environ.copy()
    # Isolate from a previous terminal's test settings and camera callouts.
    for key in tuple(environment):
        if key.startswith(("HALO_", "BOT_")):
            del environment[key]
    environment.update({
        "BOT_BINARY": str(binary), "BOT_DATA_ROOT": str(data), "BOT_SAVE_ROOT": str(saves),
        "BOT_MAP": args.map, "BOT_COUNT": str(args.bots), "BOT_SECONDS": str(args.seconds),
        "BOT_HIDDEN_WINDOW": "1", "HALO_BOT_HUMAN_CALLOUTS": "false",
        "HALO_TRACE_FILE": str(output / "trace.json"),
        # data-root relative: the map's anchors land in data/map_anchors.json
        "HALO_MAP_ANCHORS_DUMP": "map_anchors.json",
        # and the walkable polygons, for offline route and link analysis
        "HALO_NAV_DUMP": "walkable.nav",
    })
    if getattr(args, "navigation_only", False):
        environment["HALO_BOT_NAVIGATION_ONLY"] = "1"
    metadata = {
        "map": args.map, "bots": args.bots, "wall_seconds": args.seconds,
        "binary": str(binary), "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
        "hidden_window": True, "null_renderer": True, "human_callouts": False,
        "skill": "spartan", "game": "team_slayer", "loadout": "rifle_pistol",
        "navigation_only": getattr(args, "navigation_only", False),
        "started_utc": stamp, "stopped_by_watchdog": False,
    }
    started = time.monotonic()
    with (output / "game.log").open("w") as log:
        process = subprocess.Popen(["sh", str(ROOT / "tools/run_bot_match.sh")], cwd=ROOT,
                                   env=environment, stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            process.wait(timeout=args.seconds)
        except subprocess.TimeoutExpired:
            metadata["stopped_by_watchdog"] = True
        finally:
            # Also clean up on interruption, but never touch another game's PID.
            if process.poll() is None:
                # The owned process can exit between poll() and killpg().
                with suppress(ProcessLookupError):
                    os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    with suppress(ProcessLookupError):
                        os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
            metadata["returncode"] = process.returncode
            metadata["elapsed_wall_seconds"] = round(time.monotonic() - started, 3)
            (output / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(output)
    if not metadata["stopped_by_watchdog"] and process.returncode != 0:
        raise SystemExit(f"game failed with {process.returncode}; inspect {output / 'game.log'}")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", choices=("ratrace", "bloodgulch"), required=True)
    parser.add_argument("--bots", type=int, required=True)
    parser.add_argument("--seconds", type=int, default=90, help="maximum wall-clock run duration, including startup")
    parser.add_argument("--navigation-only", action="store_true",
                        help="bots only patrol lanes (debug.bot_navigation_only), for route-arrival checks")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/macos/halo")
    parser.add_argument("--source-data", type=Path, default=ROOT / "build/macos/botrun/data")
    parser.add_argument("--output-root", type=Path, default=ROOT / "build/macos/botrun/validation")
    args = parser.parse_args()
    if not 1 <= args.bots <= 31 or not 10 <= args.seconds <= 600:
        parser.error("bots must be 1–31 and wall seconds 10–600")
    run(args)


if __name__ == "__main__":
    main()
