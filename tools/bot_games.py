#!/usr/bin/env python3
"""Runs a batch of games of bots without a window, and tells how they went;
or two builds over the same games, and how they differ.

    python tools/bot_games.py --build build/linux64/halo --data ~/halo \\
        --games bloodgulch:ctf:12,sidewinder:ctf:14 --seeds 1-4

    python tools/bot_games.py --build new/halo --compare old/halo --data ~/halo ...

Each game is the game's own network test (debug.network_test, a split
screen lobby on this machine, port/linux/game/network_test.c) with the
bots in it, played at --speed times its pace (HALO_GAME_SPEED) for
--seconds of game time, every one with its own seed (HALO_RANDOM_SEED),
so that two builds play the same games. The bots count how it goes
(HALO_BOT_STATS, port/linux/game/bot_stats.c) and the last count of each
game is its result.

--data is a folder with the game's maps/ in it (and, for the bots'
navigation baked beforehand, navcache/). A map is given as the game names
it: bloodgulch, or deathisland@ce for a Custom Edition map in maps/ce.

The results: for each map and for all of them, the figures per ten minutes
of game (captures, flags taken up, dropped and sent home, rides with the
flag, kills), per bot-hour (getting stuck, ways that failed) and as shares
of the bots' time (standing about, at a wheel, on a gun, riding, escorting
a carrier, with a person). With --compare, the second build's figures beside
the first's, the difference, and how many standard errors it is (|z| of 2
or more: probably not chance). --json writes every game's figures.
"""

import argparse
import json
import math
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Dict, List, Optional, Tuple

STATS_LINE = re.compile(r"bot stats: (.*)$")
# the games running, to be stopped if this is (Ctrl+C, a kill)
RUNNING: List[subprocess.Popen] = []
# (the network test going on to its next game: this one is over)
GAME_OVER = re.compile(r"network test: game [2-9]")

# (figure, how it is shown, how it is made from a game's counts)
PER_TEN_MINUTES = ["captures", "pickups", "drops", "returns", "carrier_rides", "kills"]
# (a game's own figures, not rates: how far apart the sides' scores ended)
PER_GAME = ["score_gap"]
PER_BOT_HOUR = ["stuck", "route_failed"]
SHARES = ["idle", "driving", "gunning", "riding", "escort", "with_people"]


def parse_seeds(text: str) -> List[int]:
    seeds: List[int] = []
    for part in text.split(","):
        if "-" in part:
            low, high = part.split("-", 1)
            seeds.extend(range(int(low), int(high) + 1))
        elif part:
            seeds.append(int(part))
    return seeds


def parse_games(text: str, bots: int) -> List[Tuple[str, str, int]]:
    games = []
    for item in text.split(","):
        fields = item.split(":")
        if not fields[0]:
            continue
        games.append((fields[0], fields[1] if len(fields) > 1 and fields[1] else "ctf",
                      int(fields[2]) if len(fields) > 2 and fields[2] else bots))
    return games


def run_game(build: Path, data: Path, work: Path, slot: int, map_name: str, variant: str, bots: int, seed: int,
             args: argparse.Namespace) -> Optional[Dict[str, float]]:
    """one game: its last stats line, as figures (None: it gave none)"""
    folder = work / f"{map_name.replace('@', '_')}-{variant}-{seed}-{slot}"
    if folder.exists():
        shutil.rmtree(folder)
    (folder / "data").mkdir(parents=True)
    (folder / "save").mkdir()
    # (the game keeps its config.toml beside itself: a copy each)
    program = folder / "halo"
    try:
        os.link(build, program)
    except OSError:
        shutil.copy2(build, program)
    os.symlink((data / "maps").resolve(), folder / "data" / "maps")
    if (data / "navcache").is_dir():
        os.symlink((data / "navcache").resolve(), folder / "data" / "navcache")
    environment = dict(os.environ)
    environment.update({
        "SDL_VIDEO_DRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy", "HALO_NULL_RENDERER": "1", "HALO_NO_AUDIO": "1",
        "HALO_NET_ONLINE": "false", "HALO_UPDATE_AUTO": "false", "HALO_CRASH_REPORTS": "no",
        "HALO_DATA_ROOT": str(folder / "data"), "HALO_SAVE_ROOT": str(folder / "save"),
        # (each game its own address: the lobbies would otherwise find each other)
        "HALO_NET_ADDRESS": f"127.0.0.{20 + slot}", "HALO_NET_BROADCAST": f"127.0.0.{20 + slot}",
        "HALO_NETWORK_TEST": f"local:{map_name}:{variant}", "HALO_NETWORK_TEST_START": "4",
        "HALO_BOTS": str(bots), "HALO_RANDOM_SEED": str(seed), "HALO_BOT_SKILL": str(args.skill),
        "HALO_BOT_STATS": "10", "HALO_GAME_SPEED": str(args.speed),
    })
    for item in args.env:
        name, _, value = item.partition("=")
        environment[name] = value
    # (until the game has had its seconds: a game behind its pace, the
    # computer busy, is waited for, up to many times as long as it should take)
    limit = args.seconds / args.speed * 8 + 90
    stats = None
    begun = time.time()
    with open(folder / "out.txt", "w") as log:
        process = subprocess.Popen([str(program)], cwd=folder, env=environment, stdout=log, stderr=subprocess.STDOUT)
        RUNNING.append(process)
        lines_seen = 0
        changed = time.time()
        while process.poll() is None and time.time() - begun < limit:
            time.sleep(2)
            text = (folder / "out.txt").read_text(errors="replace")
            times = [float(found) for found in re.findall(r"bot stats: \S+ t=(\d+)", text)]
            # (its seconds had, or the game over: the next begun, or no more
            # counts for a while once there were some, the scores up)
            if (times and max(times) >= args.seconds) or GAME_OVER.search(text) or " over=1" in text:
                break
            if len(times) != lines_seen:
                lines_seen = len(times)
                changed = time.time()
            elif lines_seen and time.time() - changed > 40 + 30 / args.speed:
                break
        if process.poll() is None:
            process.kill()
            process.wait()
        RUNNING.remove(process)
    for line in (folder / "out.txt").read_text(errors="replace").splitlines():
        if GAME_OVER.search(line):
            break
        match = STATS_LINE.search(line)
        if match:
            fields = dict(field.split("=", 1) for field in match.group(1).split() if "=" in field)
            if float(fields.get("t", 0)) > args.seconds:
                break
            stats = fields
    if not args.keep:
        shutil.rmtree(folder, ignore_errors=True)
    if not stats:
        return None
    figures: Dict[str, float] = {}
    for name, value in stats.items():
        try:
            figures[name] = float(value)
        except ValueError:
            pass
    figures["captures"] = figures.get("caps0", 0.0) + figures.get("caps1", 0.0)
    figures["score_gap"] = abs(figures.get("score0", 0.0) - figures.get("score1", 0.0))
    return figures


def rates(game: Dict[str, float]) -> Dict[str, float]:
    """a game's figures as rates: per ten minutes, per bot-hour, shares"""
    minutes = max(game.get("t", 0.0), 1.0) / 60.0
    bot_hours = max(game.get("bot_seconds", 0.0), 1.0) / 3600.0
    result = {}
    for name in PER_TEN_MINUTES:
        result[name] = game.get(name, 0.0) / minutes * 10.0
    for name in PER_BOT_HOUR:
        result[name] = game.get(name, 0.0) / bot_hours
    for name in SHARES + PER_GAME:
        result[name] = game.get(name, 0.0)
    return result


def mean_and_error(values: List[float]) -> Tuple[float, float]:
    if not values:
        return 0.0, 0.0
    mean = sum(values) / len(values)
    if len(values) < 2:
        return mean, 0.0
    variance = sum((value - mean) ** 2 for value in values) / (len(values) - 1)
    return mean, math.sqrt(variance / len(values))


def unit(name: str) -> str:
    return "/10 min" if name in PER_TEN_MINUTES else "/bot-hour" if name in PER_BOT_HOUR else "a game" if name in PER_GAME else "%"


def report(title: str, first: List[Dict[str, float]], second: Optional[List[Dict[str, float]]]) -> None:
    print(f"\n{title}: {len(first)} games" + (f" and {len(second)}" if second is not None else ""))
    if second is None:
        print(f"  {'':24}{'mean':>10}{'± error':>10}")
    else:
        print(f"  {'':24}{'build':>10}{'compare':>10}{'diff':>10}{'z':>7}")
    for name in PER_TEN_MINUTES + PER_BOT_HOUR + SHARES + PER_GAME:
        a, a_error = mean_and_error([rates(game)[name] for game in first])
        label = f"{name} {unit(name)}"
        if second is None:
            print(f"  {label:<24}{a:10.2f}{a_error:10.2f}")
            continue
        b, b_error = mean_and_error([rates(game)[name] for game in second])
        error = math.sqrt(a_error ** 2 + b_error ** 2)
        z = (a - b) / error if error > 0 else 0.0
        print(f"  {label:<24}{a:10.2f}{b:10.2f}{a - b:+10.2f}{z:7.1f}")


def stop_games(*_) -> None:
    for process in list(RUNNING):
        if process.poll() is None:
            process.kill()
    os._exit(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", required=True, type=Path, help="the game to run (a native Linux build)")
    parser.add_argument("--compare", type=Path, help="a second build, over the same games")
    parser.add_argument("--data", required=True, type=Path, help="a folder with maps/ (and navcache/) in it")
    parser.add_argument("--games", default="bloodgulch:ctf:12", help="map:variant:bots, separated by commas")
    parser.add_argument("--seeds", default="1-4", help="the seeds each game is played with: 1-4, or 3,7,9")
    parser.add_argument("--bots", type=int, default=12, help="bots in a game that does not say")
    parser.add_argument("--skill", type=int, default=8, help="bots.skill (1 to 10)")
    parser.add_argument("--seconds", type=float, default=1200, help="game seconds of each game")
    parser.add_argument("--speed", type=float, default=50, help="the game at this many times its pace")
    parser.add_argument("--parallel", type=int, default=4, help="games at once")
    parser.add_argument("--env", action="append", default=[], help="NAME=VALUE for every game (more than once)")
    parser.add_argument("--work", type=Path, help="where the games run (a new temporary folder if not given)")
    parser.add_argument("--keep", action="store_true", help="keep each game's folder and log")
    parser.add_argument("--json", type=Path, help="write every game's figures here")
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, stop_games)
    signal.signal(signal.SIGINT, stop_games)

    if not (args.data / "maps").is_dir():
        sys.exit(f"{args.data}: no maps folder in it")
    games = parse_games(args.games, args.bots)
    seeds = parse_seeds(args.seeds)
    builds = [args.build.resolve()] + ([args.compare.resolve()] if args.compare else [])
    work = args.work or Path(tempfile.mkdtemp(prefix="bot_games_"))
    work.mkdir(parents=True, exist_ok=True)

    jobs = [(build_index, map_name, variant, bots, seed)
            for map_name, variant, bots in games for seed in seeds for build_index in range(len(builds))]
    results: Dict[Tuple[int, str, str, int], Optional[Dict[str, float]]] = {}
    free_slots = list(range(args.parallel))
    started = time.time()

    def play(job):
        build_index, map_name, variant, bots, seed = job
        slot = free_slots.pop()
        try:
            return job, run_game(builds[build_index], args.data, work / f"build{build_index}", slot, map_name, variant,
                                 bots, seed, args)
        finally:
            free_slots.append(slot)

    with ThreadPoolExecutor(max_workers=args.parallel) as pool:
        for count, (job, figures) in enumerate(pool.map(play, jobs), 1):
            build_index, map_name, variant, _, seed = job
            results[(build_index, map_name, variant, seed)] = figures
            state = f"{figures.get('t', 0):.0f} s" if figures else "no figures"
            print(f"[{count}/{len(jobs)}, {time.time() - started:.0f} s] {'build' if build_index == 0 else 'compare'} "
                  f"{map_name} {variant} seed {seed}: {state}", flush=True)

    def chosen(build_index: int, map_name: Optional[str] = None) -> List[Dict[str, float]]:
        return [figures for (index, name, _, _), figures in results.items()
                if index == build_index and figures and (map_name is None or name == map_name)]

    for map_name, _, _ in games:
        report(map_name, chosen(0, map_name), chosen(1, map_name) if args.compare else None)
    if len(games) > 1:
        report("all maps", chosen(0), chosen(1) if args.compare else None)
    missing = sum(1 for figures in results.values() if not figures)
    if missing:
        print(f"\n{missing} game(s) gave no figures (look with --keep at their out.txt)")
    if args.json:
        args.json.write_text(json.dumps([
            {"build": "build" if index == 0 else "compare", "map": name, "variant": variant, "seed": seed, "figures": figures}
            for (index, name, variant, seed), figures in results.items()], indent=1))
    if not args.work and not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 0 if not missing else 1


if __name__ == "__main__":
    sys.exit(main())
