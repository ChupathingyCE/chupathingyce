#!/usr/bin/env python3
"""Replays of movement tricks, so a change that breaks one fails a test.

The game plays as retail Xbox Halo, tricks included: grenade jumps, crouch
jumps and the like are part of it, and a change to the physics, the units
or the damage that stops one is a regression. Each trick here is a short
script of player actions played back through the input-replay check
(port/linux/game/determinism_check.c, tools/test_determinism.py):

1. record: a Slayer game on an Xbox map, hosted by the automated network
   test with two split screen players and no input at all, to get the
   game's ticks (the recording's format and timing are the build's). The
   network test's players spawn with no grenades, so its scripted kill
   (debug.network_test_kill) has the first kill the second, which gives the
   first two of each kind;
2. the trick's actions are written over the first player's in that recording
   (look down, throw a grenade at the feet, jump on a given tick, ...);
3. replay: the same game played from the edited recording, the player's
   unit position, velocity and vitality logged after every tick
   (debug.determinism_positions);
4. the trick's check: the outcome on those ticks (how high the player got,
   that they lived).

Each trick runs against a control (the same script without its key input,
a plain jump instead of a grenade jump), so the check measures the trick's
effect, not a spawn point's height.

    python3 tools/test_tricks.py --binary build/linux/chupathingy --maps ~/halo/maps
    python3 tools/test_tricks.py --binary build/linux/chupathingy --maps ~/halo/maps \\
        --trick grenade_jump --trace

The copies host a system link game: on a machine where another program has
the system link port (UDP 2302 on every address, a dedicated server), the
host cannot make its server and the run stops at its time limit. Run the
test in a network namespace of its own there:

    unshare -rn sh -c "ip link set lo up && python3 tools/test_tricks.py ..."

Needs a Linux (or macOS) build and the stock Xbox maps (NTSC). CI has no
maps, so under pytest the tricks are skipped unless HALO_DETERMINISM_BINARY
and HALO_DETERMINISM_MAPS name a build and a maps folder, as for
tools/test_determinism.py.

Adding a trick: write a script (a function of the tick since the player
settled and the recording's action, returning the action to play) and a
check of the logged positions, and add both to TRICKS. --trace prints the
player's height, speed and vitality tick by tick, to find the ticks a new
script needs (when a grenade lands, when it goes off).
"""
import argparse
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_determinism  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DEFAULT_MAP = "bloodgulch"
# ticks of each run: the player given grenades and settled, the trick
TICKS = 450
SEED = 1
# the scripted kill's period (seconds): each one gives the first player
# their grenades again
KILL_SECONDS = 3
# ticks after the player first has grenades before a script starts (any
# weapon picked up readied, the player standing still)
SETTLE_TICKS = 45

# units.h: the unit control flags a player action carries
CROUCH = 1 << 0
JUMP = 1 << 1
THROW_GRENADE = 1 << 13

# struct player_action (players.h): control flags, facing (yaw, pitch),
# throttle (i, j), primary trigger, desired weapon, grenade, zoom, pad
ACTION = struct.Struct("<Lfffffhhhh")
assert ACTION.size == 0x20


def read_recording(path):
    """Returns (header, long size, ticks): ticks are [tick, dequeued,
    [[player, action list]]]. The format's longs are the build's (4 bytes
    on a 32-bit build, 8 on a 64-bit Linux build)."""
    with open(path, "rb") as file:
        data = file.read()
    if data[:5] != b"HDET1":
        raise RuntimeError(f"{path} is no recording")
    long_size = 8 if struct.unpack_from("<Q", data, 8)[0] == ACTION.size else 4
    integer = "<q" if long_size == 8 else "<l"
    header_size = 8 + 3 * long_size
    header = data[:header_size]
    offset = header_size
    ticks = []
    while offset + 3 * long_size <= len(data):
        tick, dequeued, count = (struct.unpack_from(integer, data, offset + index * long_size)[0]
            for index in range(3))
        offset += 3 * long_size
        actions = []
        # (struct determinism_action: the player's index, then the action,
        # padded to the long's alignment)
        entry = long_size + ACTION.size
        entry += (-entry) % long_size
        for _ in range(count):
            player = struct.unpack_from(integer, data, offset)[0]
            actions.append([player, list(ACTION.unpack_from(data, offset + long_size))])
            offset += entry
        ticks.append([tick, dequeued, actions])
    return header, long_size, ticks


def write_recording(path, header, long_size, ticks):
    integer = "<q" if long_size == 8 else "<l"
    entry_pad = (-(long_size + ACTION.size)) % long_size
    with open(path, "wb") as file:
        file.write(header)
        for tick, dequeued, actions in ticks:
            for value in (tick, dequeued, len(actions)):
                file.write(struct.pack(integer, value))
            for player, action in actions:
                file.write(struct.pack(integer, player))
                file.write(ACTION.pack(*action))
                file.write(b"\0" * entry_pad)


def read_positions(path, player=0):
    """Returns {tick: (x, y, z, vx, vy, vz, body, shield, frags, plasmas)}
    of a player's unit in the run's last game (the ticks start again at a
    game's start: the first are the lobby's, on the menus' map)."""
    positions = {}
    last = None
    with open(path) as file:
        for line in file:
            fields = line.split()
            if len(fields) == 13 and int(fields[1]) == player:
                tick = int(fields[0])
                if last is not None and tick <= last:
                    positions = {}
                last = tick
                positions[tick] = tuple(float(value) for value in fields[3:])
    return positions


def look_down(action):
    """The action, facing straight down (its yaw kept)."""
    action[2] = -1.5
    return action


def stand(tick, action):
    """The control: a plain jump, on the tick the grenade jump's comes."""
    action = look_down(action)
    action[0] = JUMP if tick in GRENADE_JUMP_TICKS else 0
    return action


# a grenade jump: a frag thrown at the feet, looking straight down, and a
# jump as it goes off (the jump's ticks found with --trace)
GRENADE_THROW_TICK = 10
GRENADE_JUMP_TICKS = (52, 53)


def grenade_jump(tick, action):
    action = look_down(action)
    action[0] = 0
    if GRENADE_THROW_TICK <= tick < GRENADE_THROW_TICK + 3:
        action[0] |= THROW_GRENADE
    if tick in GRENADE_JUMP_TICKS:
        action[0] |= JUMP
    # (frag grenades)
    action[7] = 0
    return action


def peak_rise(positions, start):
    """How far above its height at the script's start the unit got, and
    whether it was alive throughout."""
    base = positions[start][2]
    ticks = [tick for tick in positions if tick >= start]
    peak = max(positions[tick][2] for tick in ticks) - base
    alive = all(positions[tick][6] > 0.0 for tick in ticks) and max(ticks) >= max(positions) - 1
    return peak, alive


def check_grenade_jump(trick, control, start):
    """The grenade throws the player more than twice as high as a plain
    jump (1.66 world units against 0.65 on Blood Gulch's spawn point, a
    blast of a frag at the feet as the jump leaves the ground), and they
    live."""
    problems = []
    trick_rise, trick_alive = peak_rise(trick, start)
    control_rise, _ = peak_rise(control, start)
    if not trick_rise > 2.0 * control_rise:
        problems.append(f"grenade_jump: rose {trick_rise:.3f} world units, a plain jump {control_rise:.3f}")
    if not trick_alive:
        problems.append("grenade_jump: the player died")
    return problems, f"rose {trick_rise:.3f} world units (a plain jump {control_rise:.3f})"


TRICKS = {
    "grenade_jump": (grenade_jump, stand, check_grenade_jump),
}


def play(binary, maps, work, name, mode, inputs, game_map, bot=False):
    """One run: a split screen game of two (the second player stands
    still), where the host's scripted kill (debug.network_test_kill) gives
    the first player two grenades of each kind at the start: the network
    test's players spawn with none."""
    positions = os.path.join(os.path.abspath(work), name, "positions.txt")
    test_determinism.play(binary, maps, work, name, mode, inputs, game_map, TICKS, SEED, local_players=2,
        bot=bot, extra_env={"HALO_DETERMINISM_POSITIONS": positions, "HALO_NETWORK_TEST_KILL": str(KILL_SECONDS)})
    return read_positions(positions)


def script_recording(base, path, script, start):
    """The base recording with player 0's actions from tick `start` on
    played by `script` (of the tick since `start` and the action)."""
    header, long_size, ticks = base
    edited = []
    # (the last game's ticks: where the ticks start again)
    game = max([0] + [index for index in range(1, len(ticks)) if ticks[index][0] <= ticks[index - 1][0]])
    for index, (tick, dequeued, actions) in enumerate(ticks):
        actions = [[player, list(action)] for player, action in actions]
        if index >= game and tick >= start:
            for entry in actions:
                if entry[0] == 0:
                    entry[1] = script(tick - start, entry[1])
        edited.append([tick, dequeued, actions])
    write_recording(path, header, long_size, edited)


def run_tricks(binary, maps, work, names, game_map=DEFAULT_MAP, trace=False):
    """Plays each trick and its control. Returns (problems, results)."""
    os.makedirs(work, exist_ok=True)
    base_path = os.path.join(os.path.abspath(work), "base.bin")
    idle = play(binary, maps, work, "base", "record", base_path, game_map)
    armed = [tick for tick in sorted(idle) if idle[tick][8] > 0]
    if not armed or armed[0] + SETTLE_TICKS + 200 > max(idle):
        return ["base: the player had no grenades in time for a trick"], []
    start = armed[0] + SETTLE_TICKS
    base = read_recording(base_path)
    problems = []
    results = []
    for name in names:
        script, control_script, check = TRICKS[name]
        runs = {}
        for label, run_script in ((name, script), (f"{name}-control", control_script)):
            path = os.path.join(os.path.abspath(work), f"{label}.bin")
            script_recording(base, path, run_script, start)
            runs[label] = play(binary, maps, work, label, "replay", path, game_map)
            if trace:
                for tick in sorted(runs[label]):
                    if tick >= start:
                        x, y, z, vx, vy, vz, body, shield, frags, plasmas = runs[label][tick]
                        print(f"{label} {tick - start:4d} z={z:9.4f} vz={vz:8.4f} "
                            f"speed={math.sqrt(vx * vx + vy * vy + vz * vz):7.4f} body={body:.3f} shield={shield:.3f} "
                            f"grenades={frags:.0f}/{plasmas:.0f}")
        if start not in runs[name] or start not in runs[f"{name}-control"]:
            problems.append(f"{name}: the player had no unit at tick {start}")
            continue
        trick_problems, result = check(runs[name], runs[f"{name}-control"], start)
        problems += trick_problems
        results.append(f"{name}: {result}")
    return problems, results


def test_tricks(tmp_path):
    """pytest: every trick, when a build and maps are named (CI has no maps)."""
    import pytest

    binary = os.environ.get("HALO_DETERMINISM_BINARY")
    maps = os.environ.get("HALO_DETERMINISM_MAPS")
    if not binary or not maps or not os.path.exists(binary) or \
            not os.path.exists(os.path.join(maps, f"{DEFAULT_MAP}.map")):
        pytest.skip("no build or maps (HALO_DETERMINISM_BINARY, HALO_DETERMINISM_MAPS)")
    problems, _ = run_tricks(binary, maps, str(tmp_path), list(TRICKS))
    assert not problems, "\n".join(problems)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=os.path.join(ROOT, "build", "linux", "chupathingy"))
    parser.add_argument("--maps", required=True, help="a folder of the stock Xbox maps")
    parser.add_argument("--map", default=DEFAULT_MAP)
    parser.add_argument("--trick", action="append", choices=sorted(TRICKS), help="(default: all)")
    parser.add_argument("--trace", action="store_true", help="print each run's ticks")
    parser.add_argument("--work", default=os.path.join(ROOT, "build", "tricks"))
    args = parser.parse_args()
    if not os.path.exists(os.path.join(args.maps, f"{args.map}.map")):
        print(f"tricks: no {args.map}.map in {args.maps}: skipped")
        return 0
    try:
        problems, results = run_tricks(args.binary, args.maps, args.work, args.trick or list(TRICKS),
            args.map, args.trace)
    except RuntimeError as error:
        print(f"tricks: {error}")
        return 1
    for line in results + problems:
        print(f"tricks: {line}")
    if problems:
        print(f"tricks: FAILED (runs in {args.work})")
        return 1
    print("tricks: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
