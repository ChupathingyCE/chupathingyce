# Controlled offline bot scenarios

`debug.bot_scenario` builds a bounded, host-local test arena over a loaded map
and runs scripted behavior trials. It is a debug fixture, not a game mode. The
fixture controls targets/actions or goals, observes bot decisions, and writes
JSON-lines telemetry for `tools/check_bot_scenarios.py` to summarize.

The runner starts a private offline Team Slayer process with a fresh data/save
root, disables online/public networking and auto-updates, uses legal Xbox map
assets through symlinks (not copies), and stops only the process group it
started. The default map is Blood Gulch. The arena is generated in-game; no
proprietary map-derived dump belongs in the repository. No live scenario run or
network session was performed while porting bots to the ChupathingyCE baseline.

## Scenarios

| ID | Scenario | Measures |
| --- | --- | --- |
| S01 | Open-field first contact | First sight, aim, fire, hits, time to kill |
| S02 | Line of sight broken | Last sighting, pursuit, reacquisition |
| S03 | Route around a wall | Route following, arrival, path length, stalls |
| S04 | Low-shield disengage | Retreat score, retreat, survival |
| S05 | Pickup during combat | Scavenge choice and pickup |
| S06 | Team awareness, 2v2 | Shared sighting and teammate engagement |
| S07 | Strafe by difficulty | Strafe changes, crouches, jumps, hits |
| S08 | CTF priority | Defined but not run by the scenario fixture |
| S09 | Unseen attacker relocates | Whether hidden target positions are improperly updated |
| S10 | Crowd and routes | AI time per tick, route failures, stalls |

These are bounded acceptance checks, not proof of correctness on every map or
in live play. In particular, scenario telemetry and fake-world tests cannot
validate all game collision routines, tactical quality, or match outcomes.

## Running

First build with bots enabled and provide legal `maps/bloodgulch.map` and
`maps/ui.map` files in the source data root. Then run from the repository root:

```sh
python3 configure.py --bots
ninja macos
python3 tools/run_bot_scenarios.py
```

The default runs S01–S07, S09 and S10 once at Spartan skill in a visible window.
For unattended repetitions, `--hidden` hides the window but still renders. The
runner retains each run's `game.log`, `run.json`, JSON-lines telemetry, report
and optional screenshots in a fresh directory under its output root.

```sh
python3 tools/run_bot_scenarios.py --scenarios S02,S09 --skills all --trials 2
python3 tools/run_bot_scenarios.py --hidden --trials 20 --skills all
python3 tools/check_bot_scenarios.py /path/to/telemetry.jsonl
```

`--skills` accepts `recruit`, `marine`, `odst`, `spartan`, or `all`. `--crowd`
selects the S10 bot count (2–24). `--seconds` overrides the watchdog. The runner
clears inherited `HALO_*`/`BOT_*` variables before setting its isolated offline
configuration; do not point it at a public or production data/save root.

## Asset-free checks

```sh
python3 -m pytest -q tools/test_bot_scenarios.py
```

The tests cover report outcomes (passing, failing, incomplete and truncated
telemetry), runner argument parsing, and consistency between runner settings
and the game's feature settings. They do not launch Halo or require game assets.
