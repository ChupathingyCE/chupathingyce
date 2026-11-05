"""Runs the bots on their test levels (README.md) and adds up how they did.

  python3 test.py --game build/linux64/halo --data <data folder with maps/ce/bot_*.map>
                  [--levels bot_maze,bot_steps] [--seconds 120] [--speed 3]

For each level, three games with no window:
  - the tour: every bot sent round the level's places in turn
    (HALO_BOT_GOTO=tour), each leg timed (given up after a minute);
  - a game of the level's own (capture the flag, or slayer): the captures,
    kills, and how often bots were stuck (HALO_BOT_STATS);
  - on the driving levels, the driving benchmark (HALO_BOT_DRIVEBENCH=ground).
It prints a line for each and writes them all to bot_levels_results.json."""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def run(game, data, level, variant, seconds, speed, extra, octet):
    work = tempfile.mkdtemp(prefix="botlevel_")
    try:
        env = dict(os.environ)
        env.update({
            "SDL_VIDEO_DRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy", "HALO_NULL_RENDERER": "1", "HALO_NO_AUDIO": "1",
            "HALO_NET_ONLINE": "false", "HALO_UPDATE_AUTO": "false", "HALO_DATA_ROOT": data,
            "HALO_SAVE_ROOT": os.path.join(work, "save"), "HALO_NET_ADDRESS": "127.0.0.%d" % octet,
            "HALO_NET_BROADCAST": "127.0.0.%d" % octet, "HALO_NETWORK_TEST": "local:%s@ce:%s" % (level, variant),
            "HALO_NETWORK_TEST_START": "3", "HALO_BOTS": "8", "HALO_RANDOM_SEED": "5", "HALO_BOT_SKILL": "10",
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
    legs = re.findall(r"tour bot\d+ leg to [-\d.]+ [-\d.]+ [-\d.]+ (in|given up after) ([\d.]+) s", out)
    done = [float(t) for how, t in legs if how == "in"]
    given = [float(t) for how, t in legs if how != "in"]
    return {"legs": len(done), "given_up": len(given), "mean_seconds": round(sum(done) / len(done), 1) if done else None}


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--levels", default="")
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--speed", type=float, default=3)
    arguments = parser.parse_args()
    sys.path.insert(0, HERE)
    import levels as level_definitions
    wanted = set(filter(None, arguments.levels.split(",")))
    results = []
    octet = 180
    for level in level_definitions.all_levels():
        if wanted and level.name not in wanted:
            continue
        if not os.path.exists(os.path.join(arguments.data, "maps", "ce", level.name + ".map")):
            print("%-22s no map" % level.name)
            continue
        octet += 1
        tour = ";".join("%.2f,%.2f,%.2f" % p for p in level.tour)
        out = run(arguments.game, arguments.data, level.name, "slayer", arguments.seconds, arguments.speed,
                  {"HALO_BOT_GOTO": "tour", "HALO_BOT_TOUR": tour}, octet)
        entry = {"level": level.name, "tour": tour_numbers(out)}
        if "fault" in out.lower().replace("default", "") or "EXCEPTION" in out:
            entry["crashed"] = True
        out = run(arguments.game, arguments.data, level.name, level.game, arguments.seconds, arguments.speed, {}, octet)
        entry["game"] = stats(out)
        if level.drive:
            out = run(arguments.game, arguments.data, level.name, "ctf", arguments.seconds, arguments.speed,
                      {"HALO_BOT_DRIVEBENCH": "ground"}, octet)
            entry["drive"] = drive_numbers(out)
        results.append(entry)
        print("%-22s tour %s | game %s%s" % (level.name, entry["tour"], entry["game"], (" | drive %s" % entry["drive"]) if "drive" in entry else ""))
        sys.stdout.flush()
    with open("bot_levels_results.json", "w") as f:
        json.dump(results, f, indent=1)


if __name__ == "__main__":
    main()
