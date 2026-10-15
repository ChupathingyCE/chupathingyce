"""Report on a bot scenario run's telemetry (debug.bot_scenario_log).

Reads the JSON-lines file the game writes (docs/bot-scenarios.md, "Telemetry")
and prints each trial's measurements and checks, then a table by scenario and
skill. A check is PASS or FAIL where the bots' design says what should happen
(docs/bot_decisions.md), INFO where it only measures.

    python3 tools/check_bot_scenarios.py build/macos/botrun/scenarios/<run>/telemetry.jsonl
    python3 tools/check_bot_scenarios.py telemetry.jsonl --json

Exit status 1 when the run is incomplete (a trial without a result) or a check
failed and --strict is given.
"""
from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

TICKS_PER_SECOND = 30
# what each skill does by design (bots.c's bot_skills)
STRAFES = {"recruit": False, "marine": True, "odst": True, "spartan": True}
RETREATS = {"recruit": False, "marine": False, "odst": True, "spartan": True}
SHARES = {"recruit": False, "marine": False, "odst": False, "spartan": True}
CROUCHES = {"recruit": False, "marine": False, "odst": False, "spartan": True}


def read(path: Path) -> list[dict]:
    records = []
    with path.open(encoding="utf-8") as file:
        for number, line in enumerate(file, 1):
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as error:
                # (a run stopped mid-line leaves a partial last line)
                print(f"{path}:{number}: skipped malformed line ({error.msg})", file=sys.stderr)
    return records


def seconds(ticks) -> float | None:
    return None if ticks is None else ticks / TICKS_PER_SECOND


def difference(later, earlier) -> float | None:
    if later is None or earlier is None:
        return None
    return (later - earlier) / TICKS_PER_SECOND


def check(checks: list, name: str, ok: bool | None, detail: str) -> None:
    """ok None: a measurement without an expectation"""
    checks.append({"check": name, "status": "INFO" if ok is None else "PASS" if ok else "FAIL", "detail": detail})


def fmt(value, unit: str = "s", digits: int = 2) -> str:
    if value is None:
        return "-"
    if isinstance(value, float):
        return f"{value:.{digits}f}{unit}"
    return f"{value}{unit}"


def path_length(points: list[list[float]]) -> float:
    return sum(math.dist(a[:2], b[:2]) for a, b in zip(points, points[1:], strict=False))


def trial_checks(trial: dict, result: dict, ticks: list[dict], targets: list[dict]) -> list[dict]:
    scenario = trial["scenario"]
    skill = trial["skill"]
    variant = trial.get("variant_name", "")
    subjects = {subject["slot"]: subject for subject in result.get("subjects", [])}
    main = subjects.get(0, {})
    checks: list[dict] = []
    reason = result.get("reason")
    own = [tick for tick in ticks if tick["slot"] == 0]

    if scenario == "S01":
        check(checks, "saw the target", main.get("first_visible") is not None,
              f"first sight {fmt(seconds(main.get('first_visible')))}")
        check(checks, "fired", main.get("first_fire") is not None,
              f"reaction (sight to first shot) {fmt(difference(main.get('first_fire'), main.get('first_visible')))}")
        check(checks, "hit", main.get("hits", 0) > 0, f"{main.get('hits', 0)} damage ticks, "
              f"{fmt(main.get('damage_dealt'), '', 2)} vitality")
        check(checks, "time to kill", None,
              f"{fmt(difference(main.get('killed_target'), main.get('first_visible')))} ({variant})")
        check(checks, "aim", None, f"mean error while in sight {fmt(main.get('mean_aim_error'), '°', 1)}, "
              f"on target after {fmt(difference(main.get('first_on_target'), main.get('first_visible')))}")
    elif scenario == "S02":
        lost = main.get("lost_sight")
        check(checks, "saw the target", main.get("first_visible") is not None,
              f"first sight {fmt(seconds(main.get('first_visible')))}")
        check(checks, "lost sight", lost is not None, f"at {fmt(seconds(lost))}")
        after = [tick["pos"] for tick in own if lost is not None and tick["t"] >= lost]
        pursued = path_length(after)
        check(checks, "pursued", pursued > 2.0 if lost is not None else None,
              f"{pursued:.1f} units walked after losing sight")
        check(checks, "reacquired", main.get("reacquired") is not None,
              f"after {fmt(difference(main.get('reacquired'), lost))} unseen")
        check(checks, "unseen position updates", None,
              f"{main.get('hidden_updates', 0)} ticks ({fmt(main.get('hidden_drift'), ' units', 1)})")
    elif scenario == "S03":
        arrived = main.get("arrived")
        straight = 18.0
        travelled = main.get("distance") or 0.0
        check(checks, "arrived", arrived is not None or reason == "arrived", f"after {fmt(seconds(arrived))}")
        check(checks, "path efficiency", None,
              f"{straight / travelled:.2f} (straight 18 / walked {travelled:.1f})" if travelled else "-")
        searching = [tick["t"] for tick in own if tick["route"][1]]
        check(checks, "route found", bool(searching) if own else None,
              f"following from {fmt(seconds(searching[0]) if searching else None)}")
        check(checks, "stalls", (main.get("stalled_ticks", 0) or 0) < 2 * TICKS_PER_SECOND,
              f"{fmt(seconds(main.get('stalled_ticks')))} moving without progress")
    elif scenario == "S04":
        # (the retreat curve is zero above half shields and small near it:
        # only the 10% start must retreat, by design)
        low = "10%" in variant and "100" not in variant
        retreated = main.get("first_retreat") is not None
        expected = RETREATS.get(skill) and low
        check(checks, "retreated" if expected else "retreat", True if expected and retreated else
              (False if expected else None),
              f"max retreat score {fmt(main.get('max_retreat_score'), '', 2)}, retreating "
              f"{fmt(seconds(main.get('retreat_ticks')))} from {fmt(seconds(main.get('first_retreat')))} ({variant})")
        check(checks, "survived", None, "died" if main.get("died") is not None else
              ("killed the target" if main.get("killed_target") is not None else "alive at the end"))
    elif scenario == "S05":
        low = variant.startswith("low")
        picked = reason == "picked up"
        check(checks, "picked up the weapon", picked if low else None,
              f"{'picked up' if picked else 'not picked up'} ({variant}); max scavenge score "
              f"{max((tick['scores'][3] for tick in own), default=0):.2f}")
        check(checks, "fired while in sight", None, f"{fmt(seconds(main.get('fire_ticks')))} firing")
    elif scenario == "S06":
        teammate = subjects.get(2, {})
        check(checks, "bot A saw its target", main.get("first_visible") is not None,
              f"at {fmt(seconds(main.get('first_visible')))}")
        engaged = teammate.get("first_visible") is not None or teammate.get("first_shared") is not None
        shared = teammate.get("first_shared") is not None
        check(checks, "bot B learned of it", shared if SHARES.get(skill) else None,
              f"shared sighting {fmt(seconds(teammate.get('first_shared')))}, own sight "
              f"{fmt(seconds(teammate.get('first_visible')))}{'' if engaged else ' (never engaged)'}")
    elif scenario == "S07":
        fight_ticks = sum(1 for tick in own if tick["leaf"] == "fight")
        minutes = max(fight_ticks, 1) / TICKS_PER_SECOND / 60.0
        changes = main.get("strafe_changes", 0)
        check(checks, "strafed", changes > 0 if STRAFES.get(skill) else None,
              f"{changes} direction changes ({changes / minutes:.0f}/min of fighting)")
        check(checks, "crouched", (main.get("crouch_ticks", 0) > 0) if CROUCHES.get(skill) else None,
              f"{fmt(seconds(main.get('crouch_ticks')))} crouched, {main.get('jumps', 0)} jumps")
        fired = main.get("fire_ticks", 0)
        check(checks, "hits per firing second", None,
              f"{main.get('hits', 0) / max(fired / TICKS_PER_SECOND, 1e-9):.1f} ({variant})" if fired else "never fired")
    elif scenario == "S09":
        exact = main.get("unseen_exact_ticks", 0)
        unseen = main.get("hidden_updates", 0)
        attacker_ticks = sum(1 for tick in own if tick["source"] == 2 and not tick["visible"])
        check(checks, "no unseen position knowledge", exact == 0,
              f"{fmt(seconds(exact))} knowing exactly where the unseen attacker was "
              f"({attacker_ticks} ticks knowing it only as the attacker)")
        check(checks, "unseen tracking", unseen == 0,
              f"{unseen} ticks moved its idea of the unseen attacker ({fmt(main.get('hidden_drift'), ' units', 1)})")
        check(checks, "found the attacker", None,
              f"first sight {fmt(seconds(main.get('first_visible')))}, reacquired {fmt(seconds(main.get('reacquired')))}")
    elif scenario == "S10":
        check(checks, "AI time per tick", (result.get("ai_ms_max") or 0) < 33.0,
              f"mean {fmt(result.get('ai_ms_mean'), ' ms', 3)}, worst {fmt(result.get('ai_ms_max'), ' ms', 2)}")
        stalled = sum(subject.get("stalled_ticks", 0) for subject in subjects.values())
        deaths = sum(1 for subject in subjects.values() if subject.get("died") is not None)
        kills = sum(subject.get("kills", 0) for subject in subjects.values())
        check(checks, "crowd", None, f"{len(subjects)} bots, {kills} kills, {deaths} died, "
              f"{fmt(seconds(stalled))} stalled in all")
    return checks


def analyze(records: list[dict]) -> dict:
    run = next((record for record in records if record.get("type") == "run"), {})
    trials: dict[int, dict] = {}
    results: dict[int, dict] = {}
    ticks: dict[int, list] = defaultdict(list)
    targets: dict[int, list] = defaultdict(list)
    for record in records:
        kind = record.get("type")
        if kind == "trial":
            trials[record["trial"]] = record
        elif kind == "result":
            results[record["trial"]] = record
        elif kind == "tick":
            ticks[record["trial"]].append(record)
        elif kind == "target":
            targets[record["trial"]].append(record)
    report_trials = []
    for number in sorted(trials):
        trial = trials[number]
        result = results.get(number)
        entry = {"trial": number, "scenario": trial["scenario"], "name": trial["name"], "skill": trial["skill"],
                 "variant": trial.get("variant_name", ""), "seed": trial.get("seed")}
        if result is None:
            entry.update({"reason": "no result", "seconds": None, "checks": []})
        else:
            entry.update({"reason": result.get("reason"), "seconds": seconds(result.get("ticks")),
                          "subjects": result.get("subjects", []),
                          "checks": trial_checks(trial, result, ticks[number], targets[number])})
        report_trials.append(entry)
    table: dict[tuple, dict] = {}
    for entry in report_trials:
        row = table.setdefault((entry["scenario"], entry["skill"]),
                               {"scenario": entry["scenario"], "skill": entry["skill"], "trials": 0,
                                "pass": 0, "fail": 0, "durations": []})
        row["trials"] += 1
        row["pass"] += sum(1 for item in entry["checks"] if item["status"] == "PASS")
        row["fail"] += sum(1 for item in entry["checks"] if item["status"] == "FAIL")
        if entry["seconds"] is not None:
            row["durations"].append(entry["seconds"])
    summary = []
    for row in table.values():
        durations = row.pop("durations")
        row["median_seconds"] = statistics.median(durations) if durations else None
        summary.append(row)
    return {
        "map": run.get("map"), "seed": run.get("seed"), "center": run.get("center"),
        "complete": bool(trials) and len(results) == len(trials),
        "failed_checks": sum(row["fail"] for row in summary),
        "trials": report_trials, "summary": summary,
    }


def render(report: dict) -> str:
    lines = [f"Bot scenarios on {report.get('map')}, seed {report.get('seed')}"
             f"{'' if report['complete'] else '  (INCOMPLETE: a trial has no result)'}"]
    for entry in report["trials"]:
        lines.append("")
        lines.append(f"trial {entry['trial']}: {entry['scenario']} {entry['name']} - {entry['skill']}, "
                     f"{entry['variant']}: {entry['reason']} after {fmt(entry['seconds'], 's', 1)}")
        for item in entry["checks"]:
            lines.append(f"  {item['status']:<4}  {item['check']}: {item['detail']}")
    lines.append("")
    lines.append(f"{'scenario':<9}{'skill':<9}{'trials':>7}{'pass':>6}{'fail':>6}{'median':>9}")
    for row in report["summary"]:
        lines.append(f"{row['scenario']:<9}{row['skill']:<9}{row['trials']:>7}{row['pass']:>6}{row['fail']:>6}"
                     f"{fmt(row['median_seconds'], 's', 1):>9}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("telemetry", type=Path)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--strict", action="store_true", help="exit 1 when a check failed")
    args = parser.parse_args()
    report = analyze(read(args.telemetry))
    print(json.dumps(report, indent=2) if args.json else render(report))
    if not report["complete"] or (args.strict and report["failed_checks"]):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
