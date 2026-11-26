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

    python3 tools/test_determinism.py --binary build/linux/chupathingy --maps ~/halo/maps
    python3 tools/test_determinism.py --binary build/linux/chupathingy --maps ~/halo/maps \\
        --map bloodgulch --ticks 900 --work /tmp/determinism
    python3 tools/test_determinism.py --binary build/macos/chupathingy --maps ~/halo-maps/maps --tuning-most
    python3 tools/test_determinism.py --binary OLD --maps ~/halo-maps/maps --save-golden DIR
    python3 tools/test_determinism.py --binary NEW --maps ~/halo-maps/maps --golden DIR

With --tuning-most the recording is made at the retail drawing and both
replays with every [tuning] drawing knob at its most (TUNING_MOST): the
knobs change only what is drawn, so the hashes must still agree. A replay
counts as a local game for them (render.c's render_tuning), so every one
goes as far as it can.

With --save-golden a build records and checks a game on each of the stock
maps in GOLDEN_MAPS (or --map) and keeps its inputs and hashes in a folder;
with --golden another build replays those inputs and must hash every tick
as the first build did. That checks a change that should play the same
(a cleanup, a move of code) against the build from before it.

Needs a Linux (or macOS) build and the stock Xbox maps (NTSC). CI has no
maps, so under pytest the check is skipped unless HALO_DETERMINISM_BINARY
and HALO_DETERMINISM_MAPS name a build and a maps folder. Nothing reaches
the internet or a list server (no game list, so no legacy table fetch, and
no Chupathingy maps index); the copies use their own save folders.

On macOS a copy uses 127.0.0.1 unless an alias is set up, and the system
link port is fixed: two checks at once there would take each other's port,
so a copy waits its turn for 127.0.0.1 (a lock file in the temporary
folder) and for any other copy of the game there to let go of its ports.
A copy whose server can't be made stops (network_test.c).
"""
import argparse
import os
import random
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DEFAULT_MAP = "bloodgulch"
# the stock maps --save-golden and --golden play, one game each
GOLDEN_MAPS = ("bloodgulch", "hangemhigh", "chillout", "prisoner", "damnation", "sidewinder")
DEFAULT_TICKS = 900  # 30 seconds of play
DEFAULT_SEED = 1
BOT_SEED = 7
HOST_START = 2  # seconds from hosting to the game's start
# (map loading on a slow machine, the menus, and the ticks themselves)
TIME_LIMIT = 240
# the ports a host takes: system link's and Delta's (a copy that can't have
# them makes no server and stops: network_test.c), and how long a copy on
# 127.0.0.1 waits for another program to let go of them
GAME_PORTS = (5150, 5160)
PORT_WAIT = 600
PORT_TRIES = 5


# every [tuning] knob that changes the drawing at its most (port_config.c's
# ranges), for --tuning-most and test_determinism_with_the_tuning_at_its_most
TUNING_MOST = {
    "HALO_PARTICLE_THINNING": "4",
    "HALO_TUNING_EFFECTS": "minimal",
    "HALO_SHADOW_PIXELS": "400",
    "HALO_OBJECT_CULL_PIXELS": "16",
    "HALO_PARTICLE_LIMIT": "256",
    "HALO_DYNAMIC_LIGHT_LIMIT": "1",
    "HALO_SPECULAR_LIGHT_LIMIT": "1",
}


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
        # (nor Chupathingy maps: its index, as network.online off has it too)
        "HALO_NET_MAPS_URL": "",
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


def loopback_address():
    """A loopback address of the copy's own, so that another copy on the
    machine with the system link port on 127.0.0.1 is left alone. Linux
    answers on all of 127.0.0.0/8; macOS only on 127.0.0.1 (and aliases
    added with ifconfig), where the copy could not make its server and fell
    back to the fast setup's campaign level, so it gets 127.0.0.1 there."""
    address = f"127.{random.randint(64, 250)}.{random.randint(1, 250)}.{random.randint(1, 250)}"
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.bind((address, 0))
    except OSError:
        address = "127.0.0.1"
    finally:
        probe.close()
    return address


def address_turn(address):
    """An open lock file held while a copy uses address 127.0.0.1, so that
    checks run side by side on a Mac (one loopback address there) take
    turns; None for an address of the copy's own."""
    if address != "127.0.0.1":
        return None
    import fcntl  # (POSIX: the check runs on Linux and macOS)

    lock = open(os.path.join(tempfile.gettempdir(), "chupathingy-test-127.0.0.1.lock"), "w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        log("waiting for another check on 127.0.0.1 to finish")
        fcntl.flock(lock, fcntl.LOCK_EX)
    # (and for any other copy of the game there, not one of these checks:
    # its ports free, PORT_WAIT seconds at most)
    waited = time.time()
    if not ports_free(address):
        log("waiting for another copy of the game to let go of 127.0.0.1's ports")
    while not ports_free(address):
        if time.time() - waited >= PORT_WAIT:
            lock.close()
            raise RuntimeError(f"another program kept 127.0.0.1's ports {GAME_PORTS} for {PORT_WAIT} s")
        time.sleep(2)
    return lock


def ports_free(address):
    """Whether the game's ports (GAME_PORTS) can be had on address."""
    for port in GAME_PORTS:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            probe.bind((address, port))
        except OSError:
            return False
        finally:
            probe.close()
    return True


def play(binary, maps, work, name, mode, inputs, game_map, ticks, seed, local_players=1, bot=True, extra_env=None):
    """Runs one copy to the end of its ticks. Returns the path of its hashes.
    A recording's player is the scripted bot unless bot is False (no input
    at all: tools/test_tricks.py writes its own actions over the recording's);
    extra_env adds settings (debug.determinism_positions)."""
    folder = os.path.join(work, name)
    address = loopback_address()
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
    if mode == "record" and bot:
        env["HALO_TEST_INPUT"] = f"bot:{BOT_SEED}"
    if extra_env:
        env.update(extra_env)
    for attempt in range(PORT_TRIES):
        turn = address_turn(address)
        try:
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
        finally:
            if turn:
                turn.close()
        # (another program took the port between the look and the copy's
        # server: played again, from a recording's start)
        if process.returncode != 2 or attempt == PORT_TRIES - 1:
            break
        with open(os.path.join(folder, "out.log"), "rb") as output:
            if b"its server could not be made" not in output.read():
                break
        log(f"{name}: its server could not be made (the port taken): again")
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


def run_check(binary, maps, work, game_map=DEFAULT_MAP, ticks=DEFAULT_TICKS, seed=DEFAULT_SEED, local_players=1,
              air_turn_aim=False, tuning_most=False, safe_zones=False):
    """Records a game and replays it twice. Returns (problems, ticks compared).
    With air_turn_aim the recording is made with the host's 180 glitch fix on
    (network.air_turn_aim) and the replays are run with it off in their own
    settings: they must play by the recording's rule, kept in it. With
    tuning_most the replays draw with every [tuning] knob at its most
    (TUNING_MOST), the recording as retail. With safe_zones the recording
    lays split screen out as the Xbox does (display.hud_safe_zone "xbox")
    and the replays with each view's window in its middle ("pc"): only the
    drawing moves, so the hashes must agree."""
    os.makedirs(work, exist_ok=True)
    inputs = os.path.join(os.path.abspath(work), "inputs.bin")
    started = time.time()
    record_env = {"HALO_NET_AIR_TURN_AIM": "true"} if air_turn_aim else None
    replay_env = {"HALO_NET_AIR_TURN_AIM": "false"} if air_turn_aim else None
    if tuning_most:
        replay_env = dict(replay_env or {}, **TUNING_MOST)
    if safe_zones:
        record_env = dict(record_env or {}, HALO_HUD_SAFE_ZONE="xbox")
        replay_env = dict(replay_env or {}, HALO_HUD_SAFE_ZONE="pc")
    record = play(binary, maps, work, "record", "record", inputs, game_map, ticks, seed, local_players,
        extra_env=record_env)
    replay_a = play(binary, maps, work, "replay-a", "replay", inputs, game_map, ticks, seed, local_players,
        extra_env=replay_env)
    replay_b = play(binary, maps, work, "replay-b", "replay", inputs, game_map, ticks, seed, local_players,
        extra_env=replay_env)
    if air_turn_aim:
        for name in ("record", "replay-a", "replay-b"):
            with open(os.path.join(work, name, "out.log"), "rb") as output:
                if b"air_turn_aim on" not in output.read():
                    raise RuntimeError(f"{name}: didn't play with the recording's air_turn_aim")
    if tuning_most:
        for name in ("replay-a", "replay-b"):
            with open(os.path.join(work, name, "out.log"), "rb") as output:
                text = output.read()
            if b"thinning=4(hand)" not in text or b"effects=minimal(hand)" not in text:
                raise RuntimeError(f"{name}: didn't draw with the tuning at its most")
    runs = {name: read_hashes(path) for name, path in
        (("record", record), ("replay-a", replay_a), ("replay-b", replay_b))}
    problems = []
    if len(runs["record"][0]) < ticks:
        problems.append(f"record: only {len(runs['record'][0])} of {ticks} ticks hashed")
    problems += compare("record", runs["record"], "replay-a", runs["replay-a"])
    problems += compare("replay-a", runs["replay-a"], "replay-b", runs["replay-b"])
    log(f"{len(runs['record'][0])} ticks compared in {time.time() - started:.0f} s")
    return problems, len(runs["record"][0])


def save_golden(binary, maps, work, folder, game_maps, ticks=DEFAULT_TICKS, seed=DEFAULT_SEED):
    """Records and checks a game on each map, then keeps its inputs and the
    recording's hashes in folder/<map>. Returns the problems."""
    problems = []
    for game_map in game_maps:
        map_work = os.path.join(work, game_map)
        found, _ = run_check(binary, maps, map_work, game_map, ticks, seed)
        problems += [f"{game_map}: {problem}" for problem in found]
        kept = os.path.join(folder, game_map)
        os.makedirs(kept, exist_ok=True)
        shutil.copyfile(os.path.join(map_work, "inputs.bin"), os.path.join(kept, "inputs.bin"))
        shutil.copyfile(os.path.join(map_work, "record", "hashes.txt"), os.path.join(kept, "hashes.txt"))
    return problems


def check_golden(binary, maps, work, folder, seed=DEFAULT_SEED):
    """Replays each map's kept inputs (save_golden) with this build. Returns
    (problems, maps checked): every tick must hash as the kept recording's."""
    problems = []
    checked = 0
    for game_map in sorted(os.listdir(folder)):
        kept = os.path.join(folder, game_map)
        inputs = os.path.join(kept, "inputs.bin")
        if not os.path.exists(inputs):
            continue
        golden = read_hashes(os.path.join(kept, "hashes.txt"))
        replay = play(binary, maps, os.path.join(work, game_map), "replay", "replay", os.path.abspath(inputs),
            game_map, len(golden[0]), seed)
        problems += [f"{game_map}: {problem}" for problem in
            compare("golden", golden, "replay", read_hashes(replay))]
        checked += 1
    return problems, checked


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


def test_determinism_keeps_the_host_rule(tmp_path):
    """pytest: a recording made with network.air_turn_aim on replays with it,
    whatever the replay's own setting (needs a build and maps, as above)."""
    import pytest

    binary = os.environ.get("HALO_DETERMINISM_BINARY")
    maps = os.environ.get("HALO_DETERMINISM_MAPS")
    if not binary or not maps or not os.path.exists(binary) or \
            not os.path.exists(os.path.join(maps, f"{DEFAULT_MAP}.map")):
        pytest.skip("no build or maps (HALO_DETERMINISM_BINARY, HALO_DETERMINISM_MAPS)")
    problems, _ = run_check(binary, maps, str(tmp_path), air_turn_aim=True)
    assert not problems, "\n".join(problems)


def test_determinism_with_the_tuning_at_its_most(tmp_path):
    """pytest: replays drawn with every [tuning] knob at its most agree with a
    recording drawn as retail, tick for tick (needs a build and maps, as above)."""
    import pytest

    binary = os.environ.get("HALO_DETERMINISM_BINARY")
    maps = os.environ.get("HALO_DETERMINISM_MAPS")
    if not binary or not maps or not os.path.exists(binary) or \
            not os.path.exists(os.path.join(maps, f"{DEFAULT_MAP}.map")):
        pytest.skip("no build or maps (HALO_DETERMINISM_BINARY, HALO_DETERMINISM_MAPS)")
    problems, _ = run_check(binary, maps, str(tmp_path), tuning_most=True)
    assert not problems, "\n".join(problems)


def test_determinism_against_golden(tmp_path):
    """pytest: this build replays another build's kept games (--save-golden,
    HALO_DETERMINISM_GOLDEN) tick for tick (needs a build and maps, as above)."""
    import pytest

    binary = os.environ.get("HALO_DETERMINISM_BINARY")
    maps = os.environ.get("HALO_DETERMINISM_MAPS")
    folder = os.environ.get("HALO_DETERMINISM_GOLDEN")
    if not binary or not maps or not folder or not os.path.exists(binary) or not os.path.isdir(folder):
        pytest.skip("no build, maps or kept games (HALO_DETERMINISM_BINARY, _MAPS, _GOLDEN)")
    problems, checked = check_golden(binary, maps, str(tmp_path), folder)
    assert checked, f"no kept games in {folder}"
    assert not problems, "\n".join(problems)


def test_recording_keeps_the_host_rule():
    """No build needed: the recording's header carries the host's rule after
    the seed, and a replay sets it over its own settings (determinism_check.c)."""
    source = open(os.path.join(ROOT, "port", "linux", "game", "determinism_check.c"), encoding="utf-8").read()
    assert '#define DETERMINISM_MAGIC "HDET2"' in source
    record = source.index("fwrite(&determinism_globals.seed")
    assert source.index("host_rules_host_air_turn_aim() ? FLAG(_determinism_rule_air_turn_aim_bit)", record) > record
    replay = source.index('"replay:"')
    assert source.index("host_rules_set_air_turn_aim(TEST_FLAG(rules, _determinism_rule_air_turn_aim_bit)", replay) > replay


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=os.path.join(ROOT, "build", "linux", "chupathingy"))
    parser.add_argument("--maps", required=True, help="a folder of the stock Xbox maps")
    parser.add_argument("--map", default=DEFAULT_MAP)
    parser.add_argument("--ticks", type=int, default=DEFAULT_TICKS)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--local-players", type=int, default=1, help="split screen players, 1 to 4")
    parser.add_argument("--work", default=os.path.join(ROOT, "build", "determinism"))
    parser.add_argument("--air-turn-aim", action="store_true",
        help="record with network.air_turn_aim on, replay with it off: the replays must keep the recording's")
    parser.add_argument("--tuning-most", action="store_true",
        help="record with the retail drawing, replay with every [tuning] drawing knob at its most: they must agree")
    parser.add_argument("--safe-zones", action="store_true",
        help="record with display.hud_safe_zone \"xbox\", replay with \"pc\" (split screen windows centered): "
        "they must agree")
    parser.add_argument("--save-golden", metavar="DIR",
        help="record and check a game on each stock map (GOLDEN_MAPS, or --map), keeping its inputs and hashes in DIR")
    parser.add_argument("--golden", metavar="DIR",
        help="replay the games kept in DIR (--save-golden) with this build: every tick must hash the same")
    args = parser.parse_args()
    if args.save_golden or args.golden:
        try:
            if args.save_golden:
                game_maps = [args.map] if args.map != DEFAULT_MAP else [
                    name for name in GOLDEN_MAPS if os.path.exists(os.path.join(args.maps, f"{name}.map"))]
                problems = save_golden(args.binary, args.maps, args.work, args.save_golden, game_maps, args.ticks,
                    args.seed)
                count = len(game_maps)
            else:
                problems, count = check_golden(args.binary, args.maps, args.work, args.golden, args.seed)
        except RuntimeError as error:
            log(str(error))
            return 1
        for problem in problems:
            log(problem)
        if problems or not count:
            log(f"FAILED (runs in {args.work})")
            return 1
        log(f"passed: {count} maps, every tick the same")
        return 0
    if not os.path.exists(os.path.join(args.maps, f"{args.map}.map")):
        log(f"no {args.map}.map in {args.maps}: skipped")
        return 0
    try:
        problems, count = run_check(args.binary, args.maps, args.work, args.map, args.ticks, args.seed,
            args.local_players, args.air_turn_aim, args.tuning_most, args.safe_zones)
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
