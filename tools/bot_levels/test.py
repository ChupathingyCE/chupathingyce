"""Runs the bots on their test levels (README.md) and adds up how they did.

  python3 test.py --game build/linux64/halo --data <data folder with maps/ce/bot_*.map>
                  [--levels bot_maze,bot_steps] [--seconds 120] [--speed 3]
                  [--parallel 2] [--json results.json] [--compare old.json]

For each level, three games with no window:
  - the tour: every bot sent round the level's places in turn
    (HALO_BOT_GOTO=tour), each leg timed (given up after a minute);
  - a game of the level's own (capture the flag, or slayer) with nobody shot at
    (HALO_BOT_PEACE; --fights: as in a real game): the captures, and how often
    bots were stuck (HALO_BOT_STATS);
  - on the driving levels, the driving benchmark (HALO_BOT_DRIVEBENCH=ground).
It prints a line for each and writes them all to --json (bot_levels_results.json).
With --compare, a file another build wrote: the two side by side, and what is
worse (legs finished, the time a leg takes, legs given up, stuck)."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def run(game, data, level, variant, seconds, speed, extra, octet, seed=5):
    work = tempfile.mkdtemp(prefix="botlevel_")
    try:
        env = dict(os.environ)
        env.update({
            "SDL_VIDEO_DRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy", "HALO_NULL_RENDERER": "1", "HALO_NO_AUDIO": "1",
            "HALO_NET_ONLINE": "false", "HALO_UPDATE_AUTO": "false", "HALO_DATA_ROOT": data,
            "HALO_SAVE_ROOT": os.path.join(work, "save"), "HALO_NET_ADDRESS": "127.0.0.%d" % octet,
            "HALO_NET_BROADCAST": "127.0.0.%d" % octet, "HALO_NETWORK_TEST": "local:%s@ce:%s" % (level, variant),
            "HALO_NETWORK_TEST_START": "3", "HALO_BOTS": "8", "HALO_RANDOM_SEED": str(seed), "HALO_BOT_SKILL": "10",
            "HALO_GAME_SPEED": str(speed), "HALO_BOT_STATS": "30", "HALO_BOT_LOG": "1",
        })
        env.update(extra)
        try:
            result = subprocess.run([os.path.abspath(game)], cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=seconds)
            out = result.stdout
        except subprocess.TimeoutExpired as expired:
            out = expired.stdout or b""
        return out.decode("utf-8", "replace")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def tour_numbers(out):
    legs = re.findall(r"tour bot\d+ leg to ([-\d.]+ [-\d.]+ [-\d.]+) (in|given up after) ([\d.]+) s", out)
    done = [float(t) for to, how, t in legs if how == "in"]
    given = [float(t) for to, how, t in legs if how != "in"]
    stops = {}
    for to, how, t in legs:
        stop = stops.setdefault(to, [0, 0, 0.0])
        if how == "in":
            stop[0] += 1
            stop[2] += float(t)
        else:
            stop[1] += 1
    return {"legs": len(done), "given_up": len(given), "mean_seconds": round(sum(done) / len(done), 1) if done else None,
            "stops": {to: {"in": a, "given_up": b, "mean": round(c / a, 1) if a else None} for to, (a, b, c) in stops.items()}}


def stats(out):
    lines = [l for l in out.splitlines() if "bot stats:" in l]
    if not lines:
        return {}
    figures = dict(re.findall(r"(\w+)=([-\d.]+)", lines[-1]))
    keep = ("t", "caps0", "caps1", "kills", "deaths", "stuck", "route_failed", "idle", "bailouts", "drive_speed")
    return {k: figures[k] for k in keep if k in figures}


def drive_numbers(out):
    legs = re.findall(r"drivetest bot\d+ \S+ leg \d+ (\d+) (in|given up|out|no way) in ([\d.]+) s", out)
    done = [(float(d), float(t)) for d, how, t in legs if how == "in"]
    return {"legs": len(done), "failed": len(legs) - len(done),
            "speed": round(sum(d for d, t in done) / max(sum(t for d, t in done), 1e-6), 2) if done else None}


def one_level(arguments, level, octet):
    if not os.path.exists(os.path.join(arguments.data, "maps", "ce", level.name + ".map")):
        return {"level": level.name, "missing": True}
    tour = ";".join("%.2f,%.2f,%.2f" % p for p in level.tour)
    out = run(arguments.game, arguments.data, level.name, "slayer", arguments.seconds, arguments.speed,
              {"HALO_BOT_GOTO": "tour", "HALO_BOT_TOUR": tour}, octet, arguments.seed)
    entry = {"level": level.name, "tour": tour_numbers(out)}
    if "EXCEPTION" in out or "segmentation fault" in out.lower():
        entry["crashed"] = True
    # (the level's own game with nobody shot at, unless --fights: how they get
    # about and get the flag home, not who shoots best)
    out = run(arguments.game, arguments.data, level.name, level.game, arguments.seconds, arguments.speed,
              {} if arguments.fights else {"HALO_BOT_PEACE": "1"}, octet, arguments.seed)
    entry["game"] = stats(out)
    if level.drive:
        out = run(arguments.game, arguments.data, level.name, "ctf", arguments.seconds, arguments.speed,
                  {"HALO_BOT_DRIVEBENCH": "ground"}, octet, arguments.seed)
        entry["drive"] = drive_numbers(out)
    return entry


def show(entry):
    if entry.get("missing"):
        return "%-22s no map" % entry["level"]
    tour = dict(entry["tour"])
    tour.pop("stops", None)
    return "%-22s tour %s | game %s%s%s" % (entry["level"], tour, entry["game"],
        (" | drive %s" % entry["drive"]) if "drive" in entry else "", " CRASHED" if entry.get("crashed") else "")


def compare(new, old):
    olds = {e["level"]: e for e in old}
    print("\n%-22s %-26s %-26s %s" % ("level", "legs/given up/mean s (old)", "legs/given up/mean s (new)", "worse"))
    for e in new:
        o = olds.get(e["level"])
        if not o or e.get("missing") or o.get("missing"):
            continue
        a, b = o["tour"], e["tour"]
        worse = []
        if b["legs"] < a["legs"] * 0.8 - 1:
            worse.append("fewer legs")
        if b["given_up"] > a["given_up"] + 1:
            worse.append("more given up")
        if a["mean_seconds"] and b["mean_seconds"] and b["mean_seconds"] > a["mean_seconds"] * 1.25 + 1:
            worse.append("slower legs")
        gs, go = e.get("game", {}), o.get("game", {})
        if float(gs.get("stuck", 0)) > float(go.get("stuck", 0)) * 1.5 + 3:
            worse.append("stuck more")
        caps = lambda g: float(g.get("caps0", 0)) + float(g.get("caps1", 0))
        if caps(go) >= 2 and caps(gs) < caps(go) * 0.6:
            worse.append("fewer captures")
        if "drive" in e and "drive" in o and o["drive"]["speed"] and e["drive"]["speed"] and e["drive"]["speed"] < o["drive"]["speed"] * 0.8:
            worse.append("slower driving")
        if e.get("crashed") and not o.get("crashed"):
            worse.append("CRASHED")
        print("%-22s %-26s %-26s %s" % (e["level"], "%d/%d/%s" % (a["legs"], a["given_up"], a["mean_seconds"]),
                                         "%d/%d/%s" % (b["legs"], b["given_up"], b["mean_seconds"]), ", ".join(worse) or "-"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--levels", default="")
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--speed", type=float, default=3)
    parser.add_argument("--parallel", type=int, default=1)
    parser.add_argument("--json", default="bot_levels_results.json")
    parser.add_argument("--compare", default="")
    parser.add_argument("--seed", type=int, default=5)
    parser.add_argument("--fights", action="store_true", help="the level's own game with the bots shooting each other")
    arguments = parser.parse_args()
    sys.path.insert(0, HERE)
    import levels as level_definitions
    wanted = set(filter(None, arguments.levels.split(",")))
    chosen = [level for level in level_definitions.all_levels() if not wanted or level.name in wanted]
    with ThreadPoolExecutor(max_workers=max(1, arguments.parallel)) as pool:
        futures = [pool.submit(one_level, arguments, level, 181 + k) for k, level in enumerate(chosen)]
        results = []
        for future in futures:
            entry = future.result()
            results.append(entry)
            print(show(entry))
            sys.stdout.flush()
    with open(arguments.json, "w") as f:
        json.dump(results, f, indent=1)
    if arguments.compare:
        compare(results, json.load(open(arguments.compare)))


if __name__ == "__main__":
    main()
