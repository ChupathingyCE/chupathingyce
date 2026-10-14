"""Run the controlled bot scenarios (debug.bot_scenario) and report them.

The game hosts a private, offline Team Slayer game on Blood Gulch, builds the
scenario arena above the valley, and runs every selected scenario at every
selected skill, trial after trial, in a window you can watch: a camera
follows the bot under test, with its behavior, scores, target knowledge and
route drawn over the view. The JSON-lines telemetry, the game's log and
(optionally) screenshots are kept in a fresh run directory, and the
scenarios' report (tools/check_bot_scenarios.py) is printed at the end.

    python3 tools/run_bot_scenarios.py                       # S01-S07, S09, S10 at Spartan, watched
    python3 tools/run_bot_scenarios.py --scenarios S02,S09 --skills all --trials 2
    python3 tools/run_bot_scenarios.py --hidden --trials 20 --skills all   # unattended

Legal Xbox maps are read from --source-data/maps (bloodgulch.map and ui.map);
they are symlinked, never copied. Only the process this runner starts is
stopped by its watchdog.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import signal
import subprocess
import sys
import time
from contextlib import suppress
from datetime import UTC, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import check_bot_scenarios  # noqa: E402

SCENARIOS = ("S01", "S02", "S03", "S04", "S05", "S06", "S07", "S09", "S10")
SKILLS = ("recruit", "marine", "odst", "spartan")
# a trial's wall seconds at most (its timeout, placing and the result), and
# the game's start: the watchdog's budget
TRIAL_SECONDS = {"S01": 15, "S02": 28, "S03": 28, "S04": 18, "S05": 18, "S06": 18, "S07": 18,
                 "S09": 15, "S10": 33}
STARTUP_SECONDS = 90


def default_binary() -> Path:
    # (a --portable build's bare executable cannot find SDL; the bundle's can)
    bundle = ROOT / "build/macos/ChupathingyCE.app/Contents/MacOS/halo"
    return bundle if bundle.is_file() else ROOT / "build/macos/halo"


def parse_list(text: str, names: tuple[str, ...], what: str) -> list[str]:
    chosen: list[str] = []
    for token in (part.strip() for part in text.split(",")):
        if not token:
            continue
        if token.lower() == "all":
            return list(names)
        match = next((name for name in names if name.lower() == token.lower()), None)
        if match is None:
            raise ValueError(f"unknown {what} {token!r}; choose from {', '.join(names)} or all")
        if match not in chosen:
            chosen.append(match)
    if not chosen:
        raise ValueError(f"no {what} chosen")
    return chosen


def environment_for(args, run: Path, scenarios: list[str], skills: list[str]) -> dict[str, str]:
    environment = os.environ.copy()
    # (a previous terminal's test settings must not leak in)
    for key in tuple(environment):
        if key.startswith(("HALO_", "BOT_")):
            del environment[key]
    environment.update({
        "HALO_DATA_ROOT": str(run / "data"), "HALO_SAVE_ROOT": str(run / "saves"),
        "HALO_NET_ONLINE": "false", "HALO_NET_PUBLIC_LOBBY": "false", "HALO_NET_HOST_PUBLIC": "false",
        "HALO_NET_LIST_GAMES": "false", "HALO_NET_REPORT_GAMES": "false", "HALO_NET_REPORT_EVENTS": "false",
        "HALO_NET_ALLOW_UPNP": "false", "HALO_UPDATE_AUTO": "false",
        "HALO_SOLO_GAME": "1", "HALO_NETWORK_TEST": f"host:{args.map}:team_slayer",
        "HALO_NETWORK_TEST_START": "3", "HALO_NETWORK_TEST_LOADOUT": "rifle_pistol",
        # (trials kill: a game that ends at the score limit would end the run)
        "HALO_NETWORK_TEST_SCORE": "999",
        "HALO_BOTS": "1", "HALO_BOT_HUMAN_CALLOUTS": "false", "HALO_BOT_DECISIONS": "1",
        "HALO_BOT_SCENARIO": ",".join(scenarios), "HALO_BOT_SCENARIO_SKILLS": ",".join(skills),
        "HALO_BOT_SCENARIO_TRIALS": str(args.trials), "HALO_BOT_SCENARIO_SEED": str(args.seed),
        "HALO_BOT_SCENARIO_LOG": "telemetry.jsonl", "HALO_BOT_SCENARIO_CAMERA": args.camera,
        "HALO_BOT_SCENARIO_CROWD": str(args.crowd), "HALO_BOT_SCENARIO_EXIT": "1",
    })
    if args.origin:
        environment["HALO_BOT_SCENARIO_ORIGIN"] = args.origin
    if args.hidden:
        environment["HALO_HIDDEN_WINDOW"] = "1"
    if args.screenshots:
        (run / "screenshots").mkdir()
        environment["HALO_SCREENSHOT_DIR"] = str(run / "screenshots")
        environment["HALO_SCREENSHOT_EVERY"] = str(args.screenshots)
    return environment


def run(args) -> Path:
    scenarios = parse_list(args.scenarios, SCENARIOS, "scenario")
    skills = parse_list(args.skills, SKILLS, "skill")
    binary = args.binary.resolve(strict=True)
    maps = (args.source_data / "maps").resolve(strict=True)
    for name in ("ui.map", f"{args.map}.map"):
        if not (maps / name).is_file():
            raise ValueError(f"missing map: {maps / name}")
    stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S-%fZ")
    output = args.output_root.resolve() / f"scenarios-{stamp}"
    (output / "data").mkdir(parents=True)
    (output / "saves").mkdir()
    (output / "data" / "maps").symlink_to(maps, target_is_directory=True)
    budget = STARTUP_SECONDS + sum(TRIAL_SECONDS[name] for name in scenarios) * len(skills) * args.trials
    budget = args.seconds or budget
    metadata = {
        "scenarios": scenarios, "skills": skills, "trials": args.trials, "seed": args.seed,
        "camera": args.camera, "hidden_window": args.hidden, "map": args.map, "crowd": args.crowd,
        "binary": str(binary), "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
        "started_utc": stamp, "watchdog_seconds": budget, "stopped_by_watchdog": False,
    }
    print(f"run directory: {output}")
    print(f"{len(scenarios)} scenarios x {len(skills)} skills x {args.trials} trials; "
          f"watchdog {budget} s{'' if args.hidden else ' (watch the game window)'}")
    started = time.monotonic()
    with (output / "game.log").open("w") as log:
        process = subprocess.Popen([str(binary)], cwd=ROOT, env=environment_for(args, output, scenarios, skills),
                                   stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            process.wait(timeout=budget)
        except subprocess.TimeoutExpired:
            metadata["stopped_by_watchdog"] = True
        except KeyboardInterrupt:
            metadata["interrupted"] = True
        finally:
            # (only our own process group: never another game's)
            if process.poll() is None:
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
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scenarios", default="all", help="comma-separated S01..S10 (S08 is not run), or all")
    parser.add_argument("--skills", default="spartan", help="comma-separated recruit,marine,odst,spartan, or all")
    parser.add_argument("--trials", type=int, default=1, help="trials of each scenario at each skill (1-100)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--camera", choices=("follow", "overview", "first_person", "off"), default="follow")
    parser.add_argument("--crowd", type=int, default=8, help="bots in S10 (2-24)")
    parser.add_argument("--hidden", action="store_true", help="hidden window (still rendered; for screenshots)")
    parser.add_argument("--screenshots", type=int, default=0, metavar="FRAMES",
                        help="save a screenshot every FRAMES rendered frames (0: none)")
    parser.add_argument("--map", default="bloodgulch")
    parser.add_argument("--origin", default="", help="arena center x,y[,z] (maps other than Blood Gulch)")
    parser.add_argument("--seconds", type=int, default=0, help="watchdog override (0: from the scenarios)")
    parser.add_argument("--binary", type=Path, default=default_binary(),
                        help="the game (default: the macOS bundle's, whose libraries it finds beside it)")
    parser.add_argument("--source-data", type=Path, default=ROOT / "build/macos/botrun/data")
    parser.add_argument("--output-root", type=Path, default=ROOT / "build/macos/botrun/scenarios")
    parser.add_argument("--json", action="store_true", help="print the report as JSON")
    args = parser.parse_args()
    if not 1 <= args.trials <= 100 or not 2 <= args.crowd <= 24:
        parser.error("trials must be 1-100 and crowd 2-24")
    try:
        output = run(args)
    except ValueError as error:
        parser.error(str(error))
    # (the game writes its files in its data root)
    telemetry = output / "data" / "telemetry.jsonl"
    if not telemetry.is_file():
        print(f"no telemetry: inspect {output / 'game.log'}", file=sys.stderr)
        return 1
    report = check_bot_scenarios.analyze(check_bot_scenarios.read(telemetry))
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(check_bot_scenarios.render(report))
    print(f"\nrun directory: {output}")
    return 0 if report["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
