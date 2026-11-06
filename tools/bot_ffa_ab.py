#!/usr/bin/env python3
"""Compares the bots of the even and the odd player slots in games without
teams, with some free-for-all habits (HALO_BOT_OFF2 names) turned off for
one half: first the even half, then the odd half, with the same seeds.
The result is the difference for each game between the half with the
habits and the half without them (score, kills, deaths, trades, unseen
deaths, time to the first contact), with its mean and standard error.

    python3 tools/bot_ffa_ab.py --build build/linux64/halo --data ~/halo \\
        --features third,focus --games chillout:slayer:8,prisoner:slayer:8 --seeds 1-4

Each game is played by tools/bot_games.py (see there for --seconds and
--speed)."""

import argparse
import json
import math
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NAMES = ["score", "kills", "deaths", "trades", "unseen", "contact"]


def mean_error(values):
    mean = sum(values) / len(values)
    if len(values) < 2:
        return mean, 0.0
    return mean, math.sqrt(sum((x - mean) ** 2 for x in values) / (len(values) - 1) / len(values))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--features", default="all")
    parser.add_argument("--games", default="chillout:slayer:8,hangemhigh:slayer:8,prisoner:slayer:8,damnation:slayer:8")
    parser.add_argument("--seeds", default="1-3")
    parser.add_argument("--skill", default="10")
    parser.add_argument("--seconds", default="300")
    parser.add_argument("--parallel", default="2")
    arguments = parser.parse_args()
    differences = {name: [] for name in NAMES}
    with tempfile.TemporaryDirectory() as work:
        for off_half, variable in ((0, "HALO_BOT_OFF2_EVEN"), (1, "HALO_BOT_OFF2_ODD")):
            out = os.path.join(work, "half%d.json" % off_half)
            subprocess.run([sys.executable, os.path.join(HERE, "bot_games.py"), "--build", arguments.build, "--data", arguments.data,
                            "--games", arguments.games, "--seeds", arguments.seeds, "--seconds", arguments.seconds, "--speed", "100",
                            "--parallel", arguments.parallel, "--skill", arguments.skill, "--env", "%s=%s" % (variable, arguments.features),
                            "--json", out], check=False, stdout=subprocess.DEVNULL)
            if not os.path.exists(out):
                continue
            on_half = 1 - off_half
            for game in json.load(open(out)):
                figures = game.get("figures") or {}
                if "e%d_kills" % on_half not in figures:
                    continue
                for name in NAMES:
                    differences[name].append(float(figures["e%d_%s" % (on_half, name)]) - float(figures["e%d_%s" % (off_half, name)]))
    if not differences["score"]:
        print("no games with the figures (a build that writes e0_ and e1_ in its bot stats lines is needed)")
        return
    print("the half with %s, against the half without, a game (%d games):" % (arguments.features, len(differences["score"])))
    for name in NAMES:
        mean, error = mean_error(differences[name])
        print("  %-8s %+6.2f  ± %.2f" % (name, mean, error))


if __name__ == "__main__":
    main()
