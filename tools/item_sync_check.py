#!/usr/bin/env python3
"""Checks that every machine of a test game has the same map items: the
weapons, equipment and vehicles, where they lie, who holds them, and the
same items coming back after they are taken.

    item_sync_check.py run --game BIN [--server BIN] --data DIR --map NAME@md
        --gametype NAME [--mode dedicated|listen] [--seconds 150] [--base 100]
        --work DIR
        a dedicated server (or a host playing, --mode listen) and two
        clients on loopback (127.0.0.<base> and the next two), offline
        (no game list, no Delta, no UPnP), for --seconds; the clients'
        bots wander and every 15 s the last player takes a weapon lying
        about (debug.network_test_pickup_every); every machine logs its
        items every 5 s (debug.network_test_items). Then compares them.
    item_sync_check.py compare DIR...
        compares the logs of a run's folders again (the first is the host)
    item_sync_check.py spawns MAP.map [GAME TYPE]
        the map's own item spawns (the scenario's netgame equipment block,
        read from the file as any Halo PC or Custom Edition build reads it),
        for the run's "spawn" lines to be checked against

--data is a folder with maps (Xbox, for ui.map), maps_ce (Custom Edition's
resource maps) and maps_md / maps_ce (the maps under test), linked into each
machine's own data root; --gametypes a folder of game type files
(server/events/h2-anniversary/gametypes) for the server. In listen mode
--gametype is a built-in game type (slayer, ctf, king, ...).

An item is out of step when, in two of the host's snapshots in a row, a
client has it missing or extra, as another item, or more than a world unit
from where the host has it (held items: held by another unit). Linux only
(the loopback addresses past 127.0.0.1).
"""
import argparse
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import time

ITEMS_INTERVAL = 5
PICKUP_AT = 20
PICKUP_EVERY = 15
TOLERANCE = 1.0
GAME_TYPES = {1: "ctf", 2: "slayer", 3: "oddball", 4: "king", 5: "race"}


def matches(game_type, types):
    """the engine's rule (game_engine.c match_game_type): 12 is every game
    type, 13 all but CTF, 14 all but CTF and Race"""
    return any(entry == game_type or entry == 12 or (entry == 13 and game_type != 1) or
        (entry == 14 and game_type not in (1, 5)) for entry in types)

ITEM = re.compile(r"network test: item (\d+) (\d+) (\S+) (\S+) (\S+) (-?\d+) (\d) (.*)$")
SPAWN = re.compile(r"network test: spawn (\d+) (\S+) (\S+) (-?\d+) (\S+) (\S+) (\S+) (-?\d+) (.*)$")


# ---------- running

def machine(work, name, binary, data, env):
    folder = os.path.join(work, name)
    shutil.rmtree(folder, ignore_errors=True)
    for sub in ("home", "data", "saves"):
        os.makedirs(os.path.join(folder, sub))
    for entry in os.listdir(data):
        os.symlink(os.path.join(os.path.abspath(data), entry), os.path.join(folder, "data", entry))
    full = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "HOME": os.path.join(folder, "home"),
        "HALO_DATA_ROOT": os.path.join(folder, "data"),
        "HALO_SAVE_ROOT": os.path.join(folder, "saves"),
        "HALO_UPDATE_AUTO": "false",
        "HALO_DISCORD_APPLICATION": "",
        "HALO_NET_ONLINE": "false",
        "HALO_NET_PUBLIC_LOBBY": "false",
        "HALO_NET_HOST_PUBLIC": "false",
        "HALO_NET_LIST_GAMES": "false",
        "HALO_NET_REPORT_GAMES": "false",
        "HALO_NET_BROWSER": "http://127.0.0.1:9",
        "HALO_NET_JOIN_FROM_CLIPBOARD": "false",
        "HALO_NET_ALLOW_UPNP": "false",
        "HALO_DEDICATED_PUBLIC": "false",
        "HALO_NETWORK_TEST_ITEMS": str(ITEMS_INTERVAL),
        "SDL_VIDEO_DRIVER": "dummy",
        "SDL_AUDIO_DRIVER": "dummy",
    }
    if "LD_LIBRARY_PATH" in os.environ:
        full["LD_LIBRARY_PATH"] = os.environ["LD_LIBRARY_PATH"]
    # (the caller's own HALO_ settings too, such as a debug setting to try)
    full.update({key: value for key, value in os.environ.items() if key.startswith("HALO_") and key not in full})
    full.update(env)
    return {"name": name, "folder": folder, "binary": os.path.abspath(binary), "env": full}


def start(copy):
    copy["out"] = open(os.path.join(copy["folder"], "out.log"), "wb")
    copy["process"] = subprocess.Popen([copy["binary"]], cwd=os.path.join(copy["folder"], "data"), env=copy["env"],
        stdout=copy["out"], stderr=subprocess.STDOUT, start_new_session=True)


def stop(copy):
    process = copy.get("process")
    if process and process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
    if copy.get("out"):
        copy["out"].close()


def run(args):
    work = os.path.abspath(args.work)
    os.makedirs(work, exist_ok=True)
    first, second, third = (f"127.0.0.{args.base + offset}" for offset in range(3))
    pickup = {"HALO_NETWORK_TEST_PICKUP": str(PICKUP_AT), "HALO_NETWORK_TEST_PICKUP_EVERY": str(PICKUP_EVERY)}
    join = dict(pickup, HALO_NETWORK_TEST="join", HALO_NETWORK_TEST_START="3")
    if args.mode == "dedicated":
        host = machine(work, "server", args.server, args.data, dict(pickup, HALO_NET_ADDRESS=first,
            HALO_NET_BROADCAST=f"{second},{third}", HALO_DEDICATED="item-check.txt",
            HALO_DEDICATED_MINIMUM_PLAYERS="2", HALO_DEDICATED_CONSOLE="false", HALO_NETWORK_TEST="watch"))
        data = host["env"]["HALO_DATA_ROOT"]
        os.makedirs(os.path.join(data, "playlists"), exist_ok=True)
        for name in ("item-check.txt", os.path.join("playlists", "item-check.txt")):
            with open(os.path.join(data, name), "w") as file:
                file.write(f"{args.map} {args.gametype}\n")
        if args.gametypes:
            os.makedirs(os.path.join(data, "admin"), exist_ok=True)
            shutil.copytree(args.gametypes, os.path.join(data, "admin", "gametypes"))
    else:
        host = machine(work, "host", args.game, args.data, dict(pickup, HALO_NET_ADDRESS=first,
            HALO_NET_BROADCAST=f"{second},{third}", HALO_NETWORK_TEST=f"host:{args.map}:{args.gametype}",
            HALO_NETWORK_TEST_START="25"))
    clients = [machine(work, f"client{number + 1}", args.game, args.data, dict(join, HALO_NET_ADDRESS=address,
        HALO_NET_BROADCAST=first, HALO_TEST_INPUT=f"bot:{17 + 12 * number}")) for number, address in
        enumerate((second, third))]
    copies = [host] + clients
    try:
        start(host)
        time.sleep(6)
        for client in clients:
            start(client)
            time.sleep(3)
        time.sleep(args.seconds)
    finally:
        for copy in copies:
            stop(copy)
    return compare([copy["folder"] for copy in copies], args.map, args.gametype, args.mode)


# ---------- comparing

def lines(folder):
    found = []
    for name in ("out.log", os.path.join("data", "logs", "debug.txt")):
        path = os.path.join(folder, name)
        if os.path.exists(path):
            with open(path, "rb") as file:
                found += file.read().decode("utf-8", "replace").splitlines()
        if found:
            break
    return found


def snapshots(folder):
    """{game time: {absolute index: (x, y, z, holder, at rest, tag)}}, per game"""
    games, current, last = [], {}, -1
    for line in lines(folder):
        match = ITEM.search(line)
        if not match:
            continue
        tick = int(match.group(1))
        if tick < last:
            games.append(current)
            current = {}
        last = tick
        current.setdefault(tick, {})[int(match.group(2))] = (float(match.group(3)), float(match.group(4)),
            float(match.group(5)), int(match.group(6)), int(match.group(7)), match.group(8).strip())
    if current:
        games.append(current)
    return games


def spawn_lines(folder):
    found = {}
    for line in lines(folder):
        match = SPAWN.search(line)
        if match:
            found[int(match.group(1))] = match.groups()
    return found


def nearest(snaps, tick):
    if not snaps:
        return None, None
    best = min(snaps, key=lambda other: abs(other - tick))
    return (best, snaps[best]) if abs(best - tick) <= 45 else (None, None)


def differences(host, client):
    out = {}
    for index in set(host) | set(client):
        mine, theirs = host.get(index), client.get(index)
        if mine is None or theirs is None:
            out[index] = ("missing on the client" if theirs is None else "extra on the client", mine or theirs)
        elif mine[5] != theirs[5]:
            out[index] = (f"another item ({theirs[5]})", mine)
        elif mine[3] != theirs[3]:
            out[index] = (f"held by {theirs[3]} (host: {mine[3]})", mine)
        elif mine[3] == -1:
            gap = sum((a - b) ** 2 for a, b in zip(mine[:3], theirs[:3])) ** 0.5
            if gap > TOLERANCE:
                out[index] = (f"{gap:.1f} wu off ({theirs[0]:.1f} {theirs[1]:.1f} {theirs[2]:.1f})", mine)
    return out


def respawns(host_game):
    """items the host made past the start, not dropped by a unit: (tick, index, tag)"""
    ticks = sorted(host_game)
    made = []
    for before, after in zip(ticks, ticks[1:]):
        for index in set(host_game[after]) - set(host_game[before]):
            item = host_game[after][index]
            made.append((after, index, item[5]))
    return made


def compare(folders, map_name="", gametype="", mode=""):
    host_games = snapshots(folders[0])
    report = [f"## {map_name} {gametype} ({mode})" if map_name else f"## {folders[0]}"]
    trouble = 0
    if not host_games:
        print("\n".join(report + ["host logged no items: the game never ran"]))
        return 1
    host = host_games[-1] if len(host_games[-1]) >= 3 else host_games[0]
    spawns = spawn_lines(folders[0])
    kind = None
    for fields in spawns.values():
        kind = int(fields[7])
    matching = [fields for fields in spawns.values() if kind is not None and
        matches(kind, [int(value) for value in fields[2].split(",")])]
    first_tick = min(host)
    first = host[first_tick]
    loose = sum(1 for item in first.values() if item[3] == -1)
    report.append(f"host: {len(host)} snapshots, {len(spawns)} spawns in the map, {len(matching)} for this game type, "
        f"{loose} items lying about at {first_tick}")
    # (each of the game type's spawns with an item on it at the start: the
    # map's block read as the map has it)
    empty = []
    for fields in matching:
        spot = [float(value) for value in fields[4:7]]
        if not any(item[3] == -1 and sum((a - b) ** 2 for a, b in zip(item[:3], spot)) ** 0.5 < 1.5
                for item in first.values()):
            empty.append(f"{fields[0]} ({fields[8].split(chr(92))[-1]})")
    report.append(f"host: {len(matching) - len(empty)} of {len(matching)} spawns have their item at the start"
        + (f"; empty: {', '.join(empty[:12])}" if empty else ""))
    # (items the host itself has fall out of the map: well under the lowest
    # spawn; not the clients' doing, and they follow it down)
    floor = min([float(fields[6]) for fields in spawns.values()] or [0.0]) - 20.0
    gone, last_seen = {}, {}
    for tick in sorted(host):
        for index, item in host[tick].items():
            if item[3] == -1 and item[2] < floor and index not in gone:
                where = last_seen.get(index)
                gone[index] = (tick, item[5], where)
            elif item[2] >= floor:
                last_seen[index] = item[:3]
    for index, (tick, tag, where) in sorted(gone.items(), key=lambda entry: entry[1][0]):
        report.append(f"host: item {index} {tag} fell out of the map by {tick}" +
            (f", last seen at {where[0]:.1f} {where[1]:.1f} {where[2]:.1f}" if where else ""))
    for folder in folders[1:]:
        name = os.path.basename(folder.rstrip("/"))
        games = snapshots(folder)
        if not games:
            report.append(f"{name}: logged no items (never joined)")
            trouble += 1
            continue
        client = max(games, key=len)
        compared, flagged, previous = 0, {}, {}
        for tick in sorted(host):
            other_tick, other = nearest(client, tick)
            if other is None:
                continue
            compared += 1
            now = {index: entry for index, entry in differences(host[tick], other).items() if index not in gone}
            # (out of step in two snapshots in a row: not an item caught moving)
            for index, (what, item) in now.items():
                if index in previous:
                    flagged.setdefault((index, item[5], what), tick)
            previous = now
        report.append(f"{name}: {compared} snapshots compared, {len(flagged)} items out of step")
        for (index, tag, what), tick in sorted(flagged.items(), key=lambda entry: entry[1])[:20]:
            report.append(f"  t {tick} item {index} {tag}: {what}")
        trouble += len(flagged)
        made = respawns(host)
        seen = sum(1 for tick, index, tag in made if any(index in snap and snap[index][5] == tag
            for when, snap in client.items() if when >= tick - 45))
        report.append(f"{name}: {seen} of the host's {len(made)} new items (respawns, drops) seen")
    print("\n".join(report))
    return 1 if trouble else 0


# ---------- the map's own spawns

def map_spawns(path, gametype=None):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from compare_stock_maps import load, read_tags, TAG_BASE
    data, _, tag_offset, tag_size = load(path)
    tags, scenario_id = read_tags(data, tag_offset, tag_size)
    by_id = {tag["id"]: tag for tag in tags}
    base = tag_offset + by_id[scenario_id]["address"] - TAG_BASE
    count, address = struct.unpack_from("<II", data, base + 0x384)
    wanted = {name: number for number, name in GAME_TYPES.items()}.get(gametype)
    rows = []
    for index in range(count):
        at = tag_offset + address - TAG_BASE + 144 * index
        flags, = struct.unpack_from("<I", data, at)
        types = struct.unpack_from("<4h", data, at + 4)
        spawn_time, = struct.unpack_from("<h", data, at + 14)
        x, y, z, facing = struct.unpack_from("<4f", data, at + 0x40)
        collection = by_id.get(struct.unpack_from("<I", data, at + 0x5C)[0])
        period = spawn_time
        if not period and collection:
            item_base = tag_offset + collection["address"] - TAG_BASE
            period = struct.unpack_from("<h", data, item_base + 12)[0]
        if wanted and not matches(wanted, types):
            continue
        rows.append((index, flags, types, spawn_time, period or 30, x, y, z, collection["name"] if collection else "none"))
    for row in rows:
        index, flags, types, spawn_time, period, x, y, z, name = row
        print(f"{index:3} flags {flags:x} types {','.join(map(str, types))} spawn {spawn_time} "
            f"period {period} s at {x:.2f} {y:.2f} {z:.2f} {name}")
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    one = sub.add_parser("run")
    one.add_argument("--game", required=True)
    one.add_argument("--server")
    one.add_argument("--data", required=True)
    one.add_argument("--gametypes")
    one.add_argument("--map", required=True)
    one.add_argument("--gametype", required=True)
    one.add_argument("--mode", choices=["dedicated", "listen"], default="dedicated")
    one.add_argument("--seconds", type=int, default=150)
    one.add_argument("--base", type=int, default=100)
    one.add_argument("--work", required=True)
    again = sub.add_parser("compare")
    again.add_argument("folders", nargs="+")
    spawns = sub.add_parser("spawns")
    spawns.add_argument("map")
    spawns.add_argument("gametype", nargs="?")
    args = parser.parse_args()
    if args.command == "run":
        if args.mode == "dedicated" and not args.server:
            parser.error("--server for a dedicated server's run")
        return run(args)
    if args.command == "compare":
        return compare(args.folders)
    map_spawns(args.map, args.gametype)
    return 0


if __name__ == "__main__":
    sys.exit(main())
