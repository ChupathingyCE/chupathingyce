"""Asset-free checks of the bot scenario tooling (docs/bot-scenarios.md).

The report on synthetic telemetry (passing, failing and incomplete runs, a
truncated last line, a run without trials), the runner's list parsing, and
the settings the runner sets being the game's.

    python3 -m pytest -q tools/test_bot_scenarios.py
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import check_bot_scenarios  # noqa: E402
import run_bot_scenarios  # noqa: E402


def subject(**values) -> dict:
    base = {"slot": 0, "first_visible": None, "first_on_target": None, "first_fire": None, "first_hit": None,
            "first_shared": None, "first_retreat": None, "lost_sight": None, "reacquired": None,
            "arrived": None, "died": None, "killed_target": None, "visible_ticks": 0, "fire_ticks": 0,
            "hits": 0, "kills": 0, "crouch_ticks": 0, "jumps": 0, "strafe_changes": 0, "leaf_changes": 0,
            "retreat_ticks": 0, "hidden_updates": 0, "unseen_exact_ticks": 0, "stalled_ticks": 0,
            "route_ticks": 0, "damage_dealt": 0.0, "damage_taken": 0.0, "max_retreat_score": 0.0,
            "distance": 0.0, "hidden_drift": 0.0, "mean_aim_error": None}
    base.update(values)
    return base


def trial(number: int, scenario: str, skill: str = "spartan", variant: str = "") -> dict:
    return {"type": "trial", "trial": number, "scenario": scenario, "name": scenario, "skill": skill,
            "variant": 0, "variant_name": variant, "seed": 7}


def result(number: int, scenario: str, reason: str, subjects: list[dict], skill: str = "spartan",
           variant: str = "") -> dict:
    return {"type": "result", "trial": number, "scenario": scenario, "skill": skill, "variant": 0,
            "variant_name": variant, "reason": reason, "ticks": 90, "ai_ms_mean": 0.3, "ai_ms_max": 1.0,
            "subjects": subjects}


def tick(number: int, t: int, **values) -> dict:
    record = {"type": "tick", "trial": number, "t": t, "slot": 0, "pos": [0.0, 0.0, 0.0], "leaf": "fight",
              "scores": [0, 0, 0, 0, 0, 0], "source": 1, "visible": 1, "route": [0, 0, 0, 0]}
    record.update(values)
    return record


def write(tmp_path: Path, records: list, tail: str = "") -> Path:
    path = tmp_path / "telemetry.jsonl"
    path.write_text("".join(json.dumps(record) + "\n" for record in records) + tail, encoding="utf-8")
    return path


def statuses(report: dict, number: int) -> dict:
    entry = next(entry for entry in report["trials"] if entry["trial"] == number)
    return {item["check"]: item["status"] for item in entry["checks"]}


def test_open_field_pass_and_fail(tmp_path):
    records = [
        {"type": "run", "map": "bloodgulch", "seed": 1},
        trial(1, "S01", variant="distance 5"),
        result(1, "S01", "targets killed", [subject(first_visible=0, first_fire=5, hits=3, damage_dealt=2.0,
                                                    killed_target=60)]),
        trial(2, "S01", variant="distance 15"),
        result(2, "S01", "timeout", [subject()]),
    ]
    report = check_bot_scenarios.analyze(check_bot_scenarios.read(write(tmp_path, records)))
    assert report["complete"]
    assert statuses(report, 1) == {"saw the target": "PASS", "fired": "PASS", "hit": "PASS",
                                   "time to kill": "INFO", "aim": "INFO"}
    assert statuses(report, 2)["saw the target"] == "FAIL"
    assert report["failed_checks"] == 3
    text = check_bot_scenarios.render(report)
    assert "S01" in text and "FAIL" in text


def test_hidden_attacker_knowledge_is_a_failure(tmp_path):
    records = [trial(1, "S09"), tick(1, 0, source=2, visible=0),
               result(1, "S09", "reacquired", [subject(unseen_exact_ticks=4, hidden_updates=0)])]
    report = check_bot_scenarios.analyze(check_bot_scenarios.read(write(tmp_path, records)))
    found = statuses(report, 1)
    assert found["no unseen position knowledge"] == "FAIL"
    assert found["unseen tracking"] == "PASS"


@pytest.mark.parametrize("skill,variant,expected", [
    ("spartan", "shields 10%", "FAIL"),  # it should have retreated
    ("odst", "shields 10%", "FAIL"),
    ("recruit", "shields 10%", "INFO"),  # recruits never retreat
    ("spartan", "shields 40%", "INFO"),  # the curve is near zero there
    ("spartan", "shields 100%", "INFO"),
])
def test_low_shields_expectation_by_skill(tmp_path, skill, variant, expected):
    records = [trial(1, "S04", skill, variant), result(1, "S04", "subject died", [subject(died=40)], skill, variant)]
    report = check_bot_scenarios.analyze(check_bot_scenarios.read(write(tmp_path, records)))
    assert statuses(report, 1)["retreated" if expected == "FAIL" else "retreat"] == expected


def test_incomplete_and_truncated_runs(tmp_path, capsys):
    records = [trial(1, "S03"), result(1, "S03", "arrived", [subject(arrived=300, distance=22.0)]), trial(2, "S03")]
    path = write(tmp_path, records, tail='{"type":"tick","trial":2,"t":')
    report = check_bot_scenarios.analyze(check_bot_scenarios.read(path))
    assert "malformed" in capsys.readouterr().err
    assert not report["complete"]
    assert statuses(report, 1)["arrived"] == "PASS"
    assert report["trials"][1]["reason"] == "no result"
    # (no trial at all: not complete either)
    empty = check_bot_scenarios.analyze([{"type": "run"}])
    assert not empty["complete"] and empty["trials"] == []


def test_team_sharing_expected_of_spartans_only(tmp_path):
    shared = [subject(first_visible=0), subject(slot=2, first_shared=None, first_visible=None)]
    for skill, expected in (("spartan", "FAIL"), ("marine", "INFO")):
        records = [trial(1, "S06", skill), result(1, "S06", "timeout", shared, skill)]
        report = check_bot_scenarios.analyze(check_bot_scenarios.read(write(tmp_path, records)))
        assert statuses(report, 1)["bot B learned of it"] == expected


def test_runner_lists():
    assert run_bot_scenarios.parse_list("s01, S03", run_bot_scenarios.SCENARIOS, "scenario") == ["S01", "S03"]
    assert run_bot_scenarios.parse_list("all", run_bot_scenarios.SKILLS, "skill") == list(run_bot_scenarios.SKILLS)
    assert run_bot_scenarios.parse_list("S02,S02", run_bot_scenarios.SCENARIOS, "scenario") == ["S02"]
    for bad in ("S08", "S11", "", " , "):
        with pytest.raises(ValueError):
            run_bot_scenarios.parse_list(bad, run_bot_scenarios.SCENARIOS, "scenario")
    with pytest.raises(ValueError):
        run_bot_scenarios.parse_list("legendary", run_bot_scenarios.SKILLS, "skill")


def test_runner_settings_exist_in_the_game():
    """every HALO_BOT_SCENARIO_* the runner sets is a setting of the bots feature"""
    settings = (ROOT / "source/features/bots/settings.inc").read_text(encoding="utf-8")
    declared = set(re.findall(r'"(HALO_BOT_SCENARIO[A-Z_]*)"', settings))
    runner = (ROOT / "tools/run_bot_scenarios.py").read_text(encoding="utf-8")
    used = set(re.findall(r'"(HALO_BOT_SCENARIO[A-Z_]*)"', runner))
    assert used and used <= declared, used - declared
    assert set(run_bot_scenarios.TRIAL_SECONDS) == set(run_bot_scenarios.SCENARIOS)
