#!/usr/bin/env python3
"""Input-replay check of the simulation (port/linux/game/determinism_check.c).

Plays a short game three times on one machine, headless (SDL's dummy video
and audio drivers):

1. record: a solo Slayer game on an Xbox map, hosted by the automated
   network test (debug.network_test "host:<map>:slayer", one player), its
   player driven by the scripted bot (debug.test_input "bot:<seed>"); every
   tick's player actions are written to a file;
2. replay A and 3. replay B: the same map from the same seed, no input of
   their own, the recording's actions put in place of the controllers'.

After each tick every run writes a line of hashes of the game's state (the
random seed, the players, every object's position, velocities, facing and
vitality, the scores). The check passes when the record run and both
replays agree on every tick: a difference is reported at its first tick
with the part that differs (rng, players, objects, scores).

    python3 tools/test_determinism.py --binary build/linux/halo --maps ~/halo/maps
    python3 tools/test_determinism.py --binary build/linux/halo --maps ~/halo/maps \\
        --map bloodgulch --ticks 900 --work /tmp/determinism

Needs a Linux (or macOS) build and the stock Xbox maps (NTSC). CI has no
maps, so under pytest the check is skipped unless HALO_DETERMINISM_BINARY
and HALO_DETERMINISM_MAPS name a build and a maps folder. Nothing reaches
the internet or a list server (no game list, so no legacy table fetch); the copies use their own save folders.
"""
import argparse
import os
import random
import shutil
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DEFAULT_MAP = "bloodgulch"
DEFAULT_TICKS = 900  # 30 seconds of play
DEFAULT_SEED = 1
BOT_SEED = 7
HOST_START = 2  # seconds from hosting to the game's start
# (map loading on a slow machine, the menus, and the ticks themselves)
TIME_LIMIT = 240


def log(text):
    print(f"determinism: {text}", flush=True)


def copy_environment(folder, maps, address):
    """Makes a copy's folders (data with the maps, saves, home) and returns
    its environment: offline, headless, its own save root."""
    shutil.rmtree(folder, ignore_errors=True)
    for sub in ("home", "data", "saves"):
        os.makedirs(os.path.join(folder, sub))
    os.symlink(os.path.abspath(maps), os.path.join(folder, "data", "maps"))
    env = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "HOME": os.path.join(folder, "home"),
        "HALO_DATA_ROOT": os.path.join(folder, "data"),
        "HALO_SAVE_ROOT": os.path.join(folder, "saves"),
        "HALO_UPDATE_AUTO": "false",
        "HALO_DISCORD_APPLICATION": "",
        "HALO_CRASH_REPORTS": "no",
        "HALO_NET_ONLINE": "false",
        "HALO_NET_LIST_GAMES": "false",
        "HALO_NET_REPORT_GAMES": "false",
        # (no game list: nor its thread, which fetches the legacy table)
        "HALO_NET_BROWSER": "",
        "HALO_NET_JOIN_FROM_CLIPBOARD": "false",
        "HALO_NET_ALLOW_UPNP": "false",
        # (a loopback address of its own: another copy on the machine may
        # have the system link port on 127.0.0.1)
        "HALO_NET_ADDRESS": address,
        "HALO_NET_BROADCAST": address,
        "HALO_DEDICATED_PUBLIC": "false",
        "SDL_VIDEO_DRIVER": "dummy",
        "SDL_AUDIO_DRIVER": "dummy",
    }
    if "LD_LIBRARY_PATH" in os.environ:
        env["LD_LIBRARY_PATH"] = os.environ["LD_LIBRARY_PATH"]
    return env


def play(binary, maps, work, name, mode, inputs, game_map, ticks, seed, local_players=1):
    """Runs one copy to the end of its ticks. Returns the path of its hashes."""
    folder = os.path.join(work, name)
    address = f"127.{random.randint(64, 250)}.{random.randint(1, 250)}.{random.randint(1, 250)}"
    env = copy_environment(folder, maps, address)
    hashes = os.path.join(folder, "hashes.txt")
    env.update({
        "HALO_NETWORK_TEST": f"host:{game_map}:slayer",
        # (split screen players join the lobby a second apart, from its
        # second second: network_test.c)
        "HALO_NETWORK_TEST_START": str(HOST_START + local_players),
        "HALO_SOLO_GAME": "true",
        "HALO_NETWORK_TEST_LOCAL_PLAYERS": str(local_players),
        "HALO_DETERMINISM": f"{mode}:{inputs}",
        "HALO_DETERMINISM_HASHES": hashes,
        "HALO_DETERMINISM_TICKS": str(ticks),
        "HALO_DETERMINISM_SEED": str(seed),
    })
    if mode == "record":
        env["HALO_TEST_INPUT"] = f"bot:{BOT_SEED}"
    log(f"{name}: playing {ticks} ticks on {game_map}")
    with open(os.path.join(folder, "out.log"), "wb") as output:
        process = subprocess.Popen([os.path.abspath(binary)], cwd=os.path.join(folder, "data"), env=env,
            stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            process.wait(timeout=TIME_LIMIT)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise RuntimeError(f"{name}: still running after {TIME_LIMIT} s (see {folder}/out.log)")
    if process.returncode != 0:
        raise RuntimeError(f"{name}: stopped with status {process.returncode} (see {folder}/out.log)")
    if not os.path.exists(hashes):
        raise RuntimeError(f"{name}: no hashes written (see {folder}/out.log)")
    return hashes


def read_hashes(path):
    """Returns [(tick, {part: hash})] and any notes (lines with #)."""
    ticks = []
    notes = []
    with open(path) as file:
        for line in file:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                notes.append(line)
                continue
            fields = line.split()
            parts = {"all": fields[1]}
            for field in fields[2:]:
                key, _, value = field.partition("=")
                parts[key] = value
            ticks.append((int(fields[0]), parts))
    return ticks, notes


def compare(first_name, first, second_name, second):
    """Compares two runs' hashes tick by tick. Returns a list of problems
    (empty when they agree)."""
    first_ticks, first_notes = first
    second_ticks, second_notes = second
    problems = [f"{name}: {note}" for name, notes in ((first_name, first_notes), (second_name, second_notes))
        for note in notes]
    for (tick_a, parts_a), (tick_b, parts_b) in zip(first_ticks, second_ticks):
        if tick_a != tick_b:
            problems.append(f"{first_name} and {second_name}: ticks out of step ({tick_a} and {tick_b})")
            break
        if parts_a["all"] != parts_b["all"]:
            differing = [part for part in ("rng", "players", "objects", "scores", "count")
                if parts_a.get(part) != parts_b.get(part)]
            problems.append(f"{first_name} and {second_name}: first difference at tick {tick_a}: "
                f"{', '.join(differing)} ({' '.join(f'{p}={parts_a.get(p)}/{parts_b.get(p)}' for p in differing)})")
            break
    if len(first_ticks) != len(second_ticks):
        problems.append(f"{first_name} has {len(first_ticks)} ticks, {second_name} {len(second_ticks)}")
    return problems


def run_check(binary, maps, work, game_map=DEFAULT_MAP, ticks=DEFAULT_TICKS, seed=DEFAULT_SEED, local_players=1):
    """Records a game and replays it twice. Returns (problems, ticks compared)."""
    os.makedirs(work, exist_ok=True)
    inputs = os.path.join(os.path.abspath(work), "inputs.bin")
    started = time.time()
    record = play(binary, maps, work, "record", "record", inputs, game_map, ticks, seed, local_players)
    replay_a = play(binary, maps, work, "replay-a", "replay", inputs, game_map, ticks, seed, local_players)
    replay_b = play(binary, maps, work, "replay-b", "replay", inputs, game_map, ticks, seed, local_players)
    runs = {name: read_hashes(path) for name, path in
        (("record", record), ("replay-a", replay_a), ("replay-b", replay_b))}
    problems = []
    if len(runs["record"][0]) < ticks:
        problems.append(f"record: only {len(runs['record'][0])} of {ticks} ticks hashed")
    problems += compare("record", runs["record"], "replay-a", runs["replay-a"])
    problems += compare("replay-a", runs["replay-a"], "replay-b", runs["replay-b"])
    log(f"{len(runs['record'][0])} ticks compared in {time.time() - started:.0f} s")
    return problems, len(runs["record"][0])


def test_determinism(tmp_path):
    """pytest: the check, when a build and maps are named (CI has no maps)."""
    import pytest

    binary = os.environ.get("HALO_DETERMINISM_BINARY")
    maps = os.environ.get("HALO_DETERMINISM_MAPS")
    if not binary or not maps or not os.path.exists(binary) or \
            not os.path.exists(os.path.join(maps, f"{DEFAULT_MAP}.map")):
        pytest.skip("no build or maps (HALO_DETERMINISM_BINARY, HALO_DETERMINISM_MAPS)")
    problems, _ = run_check(binary, maps, str(tmp_path))
    assert not problems, "\n".join(problems)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=os.path.join(ROOT, "build", "linux", "halo"))
    parser.add_argument("--maps", required=True, help="a folder of the stock Xbox maps")
    parser.add_argument("--map", default=DEFAULT_MAP)
    parser.add_argument("--ticks", type=int, default=DEFAULT_TICKS)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--local-players", type=int, default=1, help="split screen players, 1 to 4")
    parser.add_argument("--work", default=os.path.join(ROOT, "build", "determinism"))
    args = parser.parse_args()
    if not os.path.exists(os.path.join(args.maps, f"{args.map}.map")):
        log(f"no {args.map}.map in {args.maps}: skipped")
        return 0
    try:
        problems, count = run_check(args.binary, args.maps, args.work, args.map, args.ticks, args.seed,
            args.local_players)
    except RuntimeError as error:
        log(str(error))
        return 1
    for problem in problems:
        log(problem)
    if problems:
        log(f"FAILED (runs in {args.work})")
        return 1
    log(f"passed: record and two replays agree on all {count} ticks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
