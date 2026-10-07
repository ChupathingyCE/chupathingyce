#!/usr/bin/env python3
"""Cross-play test: a ChupathingyCE build against an OpenCE build, both ways.

Plays system link games on one Linux machine's loopback addresses, headless,
driven by the games' own automated test (port/linux/game/network_test.c,
in every build of both): our host with their client, and their host with
our client. Each direction plays Slayer to a short score on an Xbox map,
with the client leaving part way and coming back (a new copy, joining the
game in progress), then CTF. A ChupathingyCE dedicated server can also take
an OpenCE client and one of ours (--server).

    python3 tools/crossplay_test.py build --repo . --ref v0.7.0b --out /tmp/ours --server
    python3 tools/crossplay_test.py build --repo ../opence --ref build-145 --out /tmp/theirs
    python3 tools/crossplay_test.py run \\
        --ours /tmp/ours/build/linux/halo --theirs /tmp/theirs/build/linux/halo \\
        --server /tmp/ours/build/server-x86/chupathingyce-server \\
        --maps ~/halo/maps --ours-table table.json --work /tmp/crossplay --out result.json
    python3 tools/crossplay_test.py check --work /tmp/crossplay --out result.json

`build` makes a release build of a commit (git archive, configure.py
--release --pgo=off --lto=off, ninja linux), ours or OpenCE's: about two
minutes on 12 cores. `check` checks an earlier run's logs again.

--ours-table names a legacy table file (docs/delta.md, "Local override")
for our copies: how a build is made to announce, and join, an OpenCE number
its built-in table doesn't have yet, which is the claim the test checks
before a published table makes it. Without it ours uses what it was built
with. Nothing reaches the internet or a list server.

**Baselines.** Each build also plays itself the same way (control-ours,
control-theirs), at the same time. A step that fails across builds but
also fails in the host build's own baseline is a limit of that build or of
its test hooks, not a cross-play difference: it is reported, and doesn't
fail the run. (OpenCE build-145's host, for one, starts no game once a
client has changed team in its lobby, and drops a lobby that is quiet for
15 seconds: its second game never starts, with anyone.)

Each step is checked from every machine's log (the network test logs every
player's position, health, kills, deaths and score each second, at the same
game ticks on every machine):

| Step | Pass when |
| --- | --- |
| versions | the client's search finds the host and joins it |
| join | both machines log the game with both players |
| spawn | both players alive on both machines; positions logged at the same tick within 2 world units (median) |
| kills | every death the host counts (its scripted kills and the shots) is counted on the client within 2 s with the body dead there, and no body stands on the client a second after the host's died |
| client_damage | the client's shots reach the host (its reports dealt) |
| score | at the end of the Slayer game both machines agree on every player's score, kills and deaths, and someone has the variant's score |
| end_of_game | both machines reach the end (scores shown), then both play the next game (CTF) |
| rejoin | the client leaves; a new copy joins the game in progress, plays with both players and sees a death |
| stability | no copy stops by itself (assertions a release build logs and goes on from are warnings) |

The result, JSON:

    {"pass": true, "ours": {...}, "theirs": {...}, "directions": [
        {"name": "ours-host", "pass": true, "steps": {"join": {"pass": true,
        "detail": "...", "baseline_fails": false}, ...}, "warnings": [...],
        "versions": [...], "logs": "<folder>"}, ...]}

Exit status 0 for a pass, 1 for a fail, 2 for a broken run (a binary or the
maps missing). Linux (loopback addresses 127.0.0.10 to .79 need nothing set
up there); 3 to 4 minutes, every direction at once, 11 copies at most.
"""

import argparse
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time

# when each part happens
JOIN_DELAY = 0            # seconds from the host's launch to the client's
START_DELAY = 4           # seconds from the host's setup to its start (network_test_start): before the
                          # client's team change (5 s after it joins), after which OpenCE's host starts no game
KILL_INTERVAL = 5         # the host kills the last player this often (network_test_kill)
SHOOT_INTERVAL = 2        # each machine hits the next this often (network_test_shoot)
SCORE_TO_WIN = 4          # Slayer's score (network_test_score)
LATE_AT = 5               # seconds into the first game a second client joins it in progress ("late")
LEAVE_AT = 11             # ... the first client quits
BACK_AT = 14              # ... and comes back, a new copy on its address ("back")
SECOND_GAME_PLAY = 30     # seconds of the second game (CTF) played before a direction ends
QUIET_LIMIT = 45          # ... or seconds the host logs no play after the first game, before it ends
NO_GAME_LIMIT = 90        # ... or seconds with no game at all
HARD_LIMIT = 300          # everything stops
SERVER_RUN_SECONDS = 120

# steps an OpenCE host's test hooks don't do reliably, whoever its clients
# are: reported, never failing a run when the host is OpenCE's. Its host
# starts no game once a client has changed team in its lobby (the network
# test's clients do, 5 s after joining, and again in each lobby), and drops
# a lobby quiet for 15 s, so the second game's start is a race it can lose.
THEIR_HOST_QUIRKS = {"next_game"}

TICKS_PER_SECOND = 30
DEATH_SEEN_TICKS = 2 * TICKS_PER_SECOND
POSITION_TOLERANCE = 2.0

TICK_LINE = re.compile(r"network test: tick (\d+)(.*?) \| items .*? \| (playing|game over) to (-?\d+) \|.*? \| "
    r"hits (\d+) dealt (\d+) rejected (\d+) replayed (\d+) \| local (-?\d+)"
    r"(?:.*? \| vehicles (\d+) objects (\d+))?")
PLAYER = re.compile(r" player (\d+): (dead|\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) h(-?[\d.]+)/(-?[\d.]+).*?) "
    r"s(-?\d+) k(-?\d+) d(-?\d+) f(-?\d+) t(-?\d+) m(-?\d+)(?= player \d+:|$)")
VERSION_LINES = re.compile(r"(network version|legacy table: |legacy table is|joining a host|newer version of the network|"
    r"older version of the network|does not speak Delta)", re.I)
WARNING_LINES = re.compile(r"(EXCEPTION|assert)", re.I)
TROUBLE_LINES = re.compile(r"(Segmentation fault|SIGSEGV|SIGABRT|Aborted|stack trace|crashed)", re.I)


def log(*args):
    print("crossplay:", *args, flush=True)


# ---------- running copies of the game

class Copy:
    """One copy of the game (or the server) with folders of its own."""

    def __init__(self, work, name, binary, kind, maps, env, seed=None, table=None, ce_maps=None):
        self.name = name
        self.kind = kind
        self.folder = os.path.join(work, name)
        self.process = None
        self.output = None
        self.returncode = None
        if binary is None:
            return  # (a run's logs read again)
        shutil.rmtree(self.folder, ignore_errors=True)
        for sub in ("home", "data", "saves", "bin"):
            os.makedirs(os.path.join(self.folder, sub))
        self.binary = os.path.join(self.folder, "bin", os.path.basename(binary))
        shutil.copy2(binary, self.binary)
        os.symlink(os.path.abspath(maps), os.path.join(self.folder, "data", "maps"))
        if ce_maps:
            # (Custom Edition maps and their resource maps: OpenCE's
            # custom_maps, which ChupathingyCE reads too)
            os.symlink(os.path.abspath(ce_maps), os.path.join(self.folder, "data", "custom_maps"))
        if seed:
            # (map caches made beforehand: OpenCE's precache on a new save
            # root can take longer than the test gives it)
            for entry in os.listdir(seed):
                source = os.path.join(seed, entry)
                target = os.path.join(self.folder, "saves", entry)
                if os.path.isdir(source):
                    shutil.copytree(source, target, symlinks=True)
                else:
                    shutil.copy2(source, target)
        self.env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "HOME": os.path.join(self.folder, "home"),
            "HALO_DATA_ROOT": os.path.join(self.folder, "data"),
            "HALO_SAVE_ROOT": os.path.join(self.folder, "saves"),
            "HALO_UPDATE_AUTO": "false",
            "HALO_DISCORD_APPLICATION": "",
            "HALO_NET_ONLINE": "false",
            "HALO_NET_LIST_GAMES": "false",
            "HALO_NET_REPORT_GAMES": "false",
            "HALO_NET_BROWSER": "http://127.0.0.1:9",
            "HALO_NET_JOIN_FROM_CLIPBOARD": "false",
            "HALO_NET_ALLOW_UPNP": "false",
            "HALO_DEDICATED_PUBLIC": "false",
            "SDL_VIDEO_DRIVER": "dummy",
            "SDL_AUDIO_DRIVER": "dummy",
        }
        if "LD_LIBRARY_PATH" in os.environ:
            self.env["LD_LIBRARY_PATH"] = os.environ["LD_LIBRARY_PATH"]
        if table and kind == "ours":
            copied = os.path.join(self.folder, "legacy-table.json")
            shutil.copy2(table, copied)
            self.env["HALO_LEGACY_TABLE"] = copied
        self.env.update(env)

    def start(self):
        self.output = open(os.path.join(self.folder, "out.log"), "wb")
        self.process = subprocess.Popen([self.binary], cwd=os.path.join(self.folder, "data"), env=self.env,
            stdout=self.output, stderr=subprocess.STDOUT, start_new_session=True)
        log(f"{self.name}: started")

    def running(self):
        return self.process is not None and self.process.poll() is None

    def stop(self, by_test=True):
        if self.process is None:
            return
        if self.process.poll() is None:
            try:
                os.killpg(self.process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            self.process.wait()
            self.returncode = "stopped by the test"
        else:
            self.returncode = self.process.returncode
        self.output.close()
        self.process = None

    def read(self, name):
        path = os.path.join(self.folder, name)
        if not os.path.exists(path):
            return []
        with open(path, "rb") as file:
            return file.read().decode("utf-8", "replace").splitlines()

    def out(self):
        return self.read("out.log")

    def debug(self):
        return self.read(os.path.join("data", "debug.txt"))


# ---------- reading a copy's log

class Game:
    """One game as a machine logged it: its seconds of play, in order. Each:
    (tick, {player: state}, playing, score_to_win, counters)."""

    def __init__(self):
        self.ticks = []
        self.host_game = None  # a client's: the host's game it is

    def at(self, tick):
        return next((entry[1] for entry in self.ticks if entry[0] == tick), None)

    def first(self):
        return self.ticks[0][0]

    def last(self):
        return self.ticks[-1][0]

    def over(self):
        return any(not entry[2] for entry in self.ticks)

    def end(self):
        """The players at the first second of the game's end, or None."""
        return next((entry[1] for entry in self.ticks if not entry[2]), None)

    def both(self):
        return sum(1 for entry in self.ticks if len(entry[1]) >= 2)


def read_games(lines):
    """A machine's games, split where the tick goes back (a new game), and
    the number of the host's scripted kills."""
    games = []
    scripted = 0
    last_tick = None
    for line in lines:
        if "network test: the first player kills the last" in line:
            scripted += 1
            continue
        match = TICK_LINE.search(line)
        if not match:
            continue
        tick = int(match.group(1))
        if last_tick is None or tick < last_tick:
            games.append(Game())
        last_tick = tick
        players = {}
        for player in PLAYER.finditer(match.group(2)):
            state = {"alive": player.group(2) != "dead", "score": int(player.group(8)), "kills": int(player.group(9)),
                "deaths": int(player.group(10)), "team": int(player.group(12)), "machine": int(player.group(13))}
            if state["alive"]:
                state["position"] = (float(player.group(3)), float(player.group(4)), float(player.group(5)))
                state["health"] = float(player.group(6))
            players[int(player.group(1))] = state
        counters = {"hits": int(match.group(5)), "dealt": int(match.group(6)), "rejected": int(match.group(7))}
        if match.group(10) is not None:
            # (builds with the counting hook: the world's vehicles and objects)
            counters["vehicles"] = int(match.group(10))
            counters["objects"] = int(match.group(11))
        games[-1].ticks.append((tick, players, match.group(3) == "playing", int(match.group(4)), counters))
    return games, scripted


class Seconds:
    """A game's seconds by tick, for looking up the one nearest a tick: a
    machine that joined a game in progress logs at other ticks than the host."""

    def __init__(self, game):
        self.ticks = [entry[0] for entry in game.ticks]
        self.players = [entry[1] for entry in game.ticks]

    def near(self, tick, within=TICKS_PER_SECOND // 2):
        import bisect
        index = bisect.bisect_left(self.ticks, tick)
        best = None
        for candidate in (index - 1, index):
            if 0 <= candidate < len(self.ticks) and abs(self.ticks[candidate] - tick) <= within and \
                    (best is None or abs(self.ticks[candidate] - tick) < abs(self.ticks[best] - tick)):
                best = candidate
        return (self.ticks[best], self.players[best]) if best is not None else (None, None)


def tally(players):
    return {index: (state["kills"], state["deaths"]) for index, state in players.items()}


def match_games(host_games, client_games):
    """Which of the host's games each client game is: the one whose seconds
    agree with it most (the same ticks with the same kills and deaths)."""
    floor = 0
    for game in client_games:
        best, best_score = None, 0
        for number in range(floor, len(host_games)):
            seconds = Seconds(host_games[number])
            score = 0
            for entry in game.ticks:
                _, players = seconds.near(entry[0])
                if players is not None and tally(players) == tally(entry[1]):
                    score += 1
            if score > best_score:
                best, best_score = number, score
        game.host_game = best
        if best is not None:
            floor = best


def progress(copy):
    """The seconds of play a running copy has logged, by game."""
    games = []
    last = None
    try:
        with open(os.path.join(copy.folder, "out.log"), "rb") as file:
            for line in file:
                if b"network test: tick " not in line:
                    continue
                tick = int(line.split(b"network test: tick ")[1].split(b" ")[0])
                if last is None or tick < last:
                    games.append(0)
                games[-1] += 1
                last = tick
    except (OSError, ValueError, IndexError):
        pass
    return games


def distance(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


# ---------- the checks

def step(passed, detail):
    return {"pass": bool(passed), "detail": detail}


def check_direction(host, client, late, back, variants):
    """The steps of one direction, from the host's and its clients' logs."""
    steps = {}
    host_games, scripted = read_games(host.out())
    clients = []
    for copy in (client, late, back):
        games, _ = read_games(copy.out())
        match_games(host_games, games)
        clients.append((copy, games))
    pairs = [(copy, game, host_games[game.host_game]) for copy, games in clients for game in games
        if game.host_game is not None]
    versions = sorted({line.strip() for copy in (host, client, late, back) for line in copy.out() + copy.debug()
        if VERSION_LINES.search(line)})

    joined = any("network test: joining" in line for line in client.out())
    steps["versions"] = step(joined, "the client found the host and joined" if joined else
        "the client never joined: its search found no host it would join (see the version lines)")
    played = bool(host_games) and any(game.both() for _, game, _ in pairs) and any(g.both() for g in host_games)
    steps["join"] = step(played, f"the host logged {sum(len(g.ticks) for g in host_games)} s of play, the clients "
        f"{sum(len(g.ticks) for _, games in clients for g in games)} s" if played else
        "no game with both players on both machines")
    if not played:
        for name in ("spawn", "kills", "client_damage", "score", "end_of_game", "next_game", "rejoin"):
            steps[name] = step(False, "not reached: no game joined")
        return steps, versions

    # positions at the same ticks
    gaps = []
    alive_both = False
    for _, game, host_game in pairs:
        seconds = Seconds(host_game)
        for tick, players, *_ in game.ticks:
            # (the same tick, or within a frame or two of it)
            _, other = seconds.near(tick, within=2)
            if not other:
                continue
            for index, state in players.items():
                seen = other.get(index)
                if state["alive"] and seen and seen["alive"]:
                    gaps.append(distance(state["position"], seen["position"]))
            if len(players) >= 2 and len(other) >= 2 and all(s["alive"] for s in players.values()) and \
                    all(s["alive"] for s in other.values()):
                alive_both = True
    if gaps:
        gaps.sort()
        median = statistics.median(gaps)
        detail = (f"both players alive on both machines: {alive_both}; positions compared at {len(gaps)} player-seconds, "
            f"median gap {median:.3f}, 95th {gaps[int(len(gaps) * 0.95)]:.3f}, largest {gaps[-1]:.3f} world units")
    else:
        median, detail = None, "no second logged by both machines with a living player"
    steps["spawn"] = step(alive_both and median is not None and median <= POSITION_TOLERANCE, detail)

    # deaths: every death the host counts is counted on the client present,
    # soon, with the body dead, and no body stands a second after the host's fell
    events = []
    for number, game in enumerate(host_games):
        before = {}
        for tick, players, *_ in game.ticks:
            for index, state in players.items():
                if index in before and state["deaths"] > before[index]:
                    events.append((number, tick, index, state["deaths"]))
                before[index] = state["deaths"]
    seen, delays, missed, standing, unwatched = 0, [], [], [], 0
    for number, tick, victim, count in events:
        watching = [(copy, game) for copy, game, _ in pairs if game.host_game == number and
            game.first() <= tick - TICKS_PER_SECOND and game.last() >= tick + DEATH_SEEN_TICKS]
        if not watching:
            unwatched += 1
            continue
        seconds = Seconds(host_games[number])
        for copy, game in watching:
            counted = next((entry[0] for entry in game.ticks if entry[0] >= tick - DEATH_SEEN_TICKS and
                victim in entry[1] and entry[1][victim]["deaths"] >= count), None)
            body = next((entry[0] for entry in game.ticks if tick - DEATH_SEEN_TICKS <= entry[0] <= tick + DEATH_SEEN_TICKS
                and victim in entry[1] and not entry[1][victim]["alive"]), None)
            if counted is not None and counted <= tick + DEATH_SEEN_TICKS and body is not None:
                seen += 1
                delays.append(max(counted, body) - tick)
            else:
                missed.append((copy.name.split("/")[-1], number + 1, tick, victim, f"counted at {counted}, body dead at {body}"))
            for entry in game.ticks:
                if tick + TICKS_PER_SECOND <= entry[0] <= tick + 2 * TICKS_PER_SECOND:
                    host_state = seconds.near(entry[0])[1] or {}
                    mine = entry[1].get(victim)
                    if mine and mine["alive"] and victim in host_state and not host_state[victim]["alive"]:
                        standing.append((copy.name.split("/")[-1], number + 1, entry[0], victim, mine["health"]))
    steps["kills"] = step(seen and not missed and not standing,
        f"{len(events)} deaths on the host ({scripted} scripted kill lines; {unwatched} while no client was there); "
        f"{seen} seen on the client, counted and the body dead, within {DEATH_SEEN_TICKS // TICKS_PER_SECOND} s"
        + (f" (at most {max(delays)} ticks after the host's second)" if delays else "")
        + (f"; not seen (client, game, tick, player): {missed}" if missed else "")
        + (f"; bodies alive on the client a second after the host's died (client, game, tick, player, health): {standing}"
           if standing else ""))

    dealt = max((entry[4]["dealt"] for game in host_games for entry in game.ticks), default=0)
    rejected = max((entry[4]["rejected"] for game in host_games for entry in game.ticks), default=0)
    sent = sum(max((entry[4]["hits"] for game in games for entry in game.ticks), default=0) for _, games in clients)
    steps["client_damage"] = step(dealt > 0, f"the clients reported {sent} hits; the host dealt {dealt}, refused {rejected}")

    # the first game's end, on the host and the client there then
    first = host_games[0]
    host_end = first.end()
    client_end = next((game.end() for _, game, _ in pairs if game.host_game == 0 and game.end()), None)

    def table(players):
        return {index: (state["score"], state["kills"], state["deaths"]) for index, state in (players or {}).items()}

    reached = max((score for score, _, _ in table(host_end).values()), default=0) >= SCORE_TO_WIN
    steps["score"] = step(host_end is not None and table(host_end) == table(client_end) and reached,
        f"at the end of game 1 (player: score, kills, deaths) the host {table(host_end) or 'never ended'}, the client "
        f"{table(client_end) or 'never ended'}; to win {SCORE_TO_WIN}")
    second = len(host_games) >= 2 and any(entry[2] for entry in host_games[1].ticks)
    second_client = any(game.host_game == 1 and any(entry[2] for entry in game.ticks) for _, game, _ in pairs)
    # (back in the lobby: the client's log after the game's end)
    lobby = any("switching to pregame" in line for copy, games in clients for line in copy.debug()
        if games and any(game.host_game == 0 and game.over() for game in games))
    steps["end_of_game"] = step(host_end is not None and client_end is not None and lobby,
        f"game 1 ({variants[0]}) over on the host: {host_end is not None}, on the client: {client_end is not None}; "
        f"the client back in the host's lobby: {lobby}")
    steps["next_game"] = step(second and second_client,
        f"game 2 ({variants[1]}) played on the host: {second}, on the client: {second_client}")

    # the world: with the counting hook, the vehicles each machine has, at
    # the same seconds (a map's placed vehicles all there on every machine)
    counts = []
    for copy, game, host_game in pairs:
        seconds = Seconds(host_game)
        host_counts = {entry[0]: entry[4] for entry in host_game.ticks}
        for entry in game.ticks:
            tick, _ = seconds.near(entry[0], within=2)
            if tick is not None and "vehicles" in entry[4] and "vehicles" in host_counts.get(tick, {}):
                counts.append((copy.name.split("/")[-1], entry[0], host_counts[tick]["vehicles"], entry[4]["vehicles"],
                               host_counts[tick]["objects"], entry[4]["objects"]))
    if counts:
        first = counts[min(5, len(counts) - 1)]
        differ = [c for c in counts if c[2] != c[3]]
        # (two seconds allowed to differ: a second's lag at a game's start or
        # end, as the clients see the world change)
        steps["vehicles"] = step(len(differ) <= 2, f"vehicles (host, client) at {len(counts)} seconds: e.g. {first[2]} and "
            f"{first[3]}, objects {first[4]} and {first[5]}; seconds that differ: {len(differ)}"
            + (f", first (client, tick, host, client): {[c[:4] for c in differ[:5]]}" if differ else ""))

    # join in progress: a second client joins game 1 under way, and the first,
    # having left, comes back to it
    parts = []
    passed = True
    for copy, games in clients[1:]:
        joined = any("network test: joining" in line for line in copy.out())
        in_progress = bool(games) and games[0].host_game == 0 and games[0].first() > 3 * TICKS_PER_SECOND
        both = sum(game.both() for game in games)
        death = any(not state["alive"] or state["deaths"] > 0 for game in games for entry in game.ticks
            for state in entry[1].values())
        passed = passed and joined and in_progress and both >= 5 and death
        role = copy.name.split("/")[-1]
        parts.append(f"{role}: joined {joined}, into game 1 in progress {in_progress}"
            + (f" (from tick {games[0].first()})" if games else "") + f", {both} s with the others, a death seen {death}")
    # (the first client's last second before the host's game 1 ended)
    left = bool(clients[0][1]) and clients[0][1][0].host_game == 0 and bool(host_games) and \
        clients[0][1][-1].last() < host_games[0].last() - 3 * TICKS_PER_SECOND
    steps["rejoin"] = step(passed and left, f"the first client left game 1: {left}; " + "; ".join(parts))
    return steps, versions


def check_server(server, theirs, ours):
    """A dedicated server of ours with their client and ours: both join and
    play the server's game and see the same game (each other's player where
    the other has it, the same kills and deaths). The network test's
    scripted kills and shots are a host's, which a server doesn't run, so
    this is the clients' agreement only."""
    steps = {}
    games = {copy.name: read_games(copy.out())[0] for copy in (theirs, ours)}
    for copy in (theirs, ours):
        played = sum(game.both() for game in games[copy.name])
        steps[f"join_{copy.kind}"] = step(played >= 10, f"{copy.name}: {played} s played with both players")
    ticks = {entry[0]: entry[1] for game in games[theirs.name] for entry in game.ticks}
    gaps, agree, differ = [], 0, []
    for game in games[ours.name]:
        for tick, players, *_ in game.ticks:
            other = ticks.get(tick)
            if not other or len(other) != len(players) or len(players) < 2:
                continue
            for index, state in players.items():
                seen = other.get(index)
                if state["alive"] and seen and seen["alive"]:
                    gaps.append(distance(state["position"], seen["position"]))
            if tally(players) == tally(other):
                agree += 1
            else:
                differ.append(tick)
    median = statistics.median(gaps) if gaps else None
    steps["same_game"] = step(bool(gaps) and median <= POSITION_TOLERANCE and agree > 0 and len(differ) <= 2,
        (f"positions compared at {len(gaps)} player-seconds, median gap {median:.3f}, largest {max(gaps):.3f}; "
        if gaps else "no second logged by both clients; ") +
        f"kills and deaths the same at {agree} seconds, different at {len(differ)} (two allowed: a second's lag)")
    return steps


def stability(copies):
    trouble, warnings = {}, {}
    for copy in copies:
        found = [line.strip() for line in copy.out() + copy.debug() if TROUBLE_LINES.search(line)][:3]
        if copy.returncode not in (None, "stopped by the test"):
            found.insert(0, f"exited by itself, status {copy.returncode}")
        if found:
            trouble[copy.name] = found
        warned = {}
        for line in copy.debug():
            if WARNING_LINES.search(line):
                key = re.sub(r"^\S+ \S+\s+", "", line.strip())
                key = re.sub(r"\(release build.*\)$", "(release build)", key)
                warned[key] = warned.get(key, 0) + 1
        if warned:
            warnings[copy.name] = [f"{text} x{count}" for text, count in sorted(warned.items())][:5]
    return step(not trouble, "every copy ran until the test stopped it" if not trouble else trouble), warnings


# ---------- a run

def addresses(base):
    return [f"127.0.0.{base + offset}" for offset in range(3)]


def host_map(name, kind):
    """the map as each build's network test names it: a Custom Edition map
    is custom_maps\\NAME to OpenCE (its name in the game's protocol too) and
    NAME@ce to ChupathingyCE"""
    if "\\" in name and kind == "ours":
        return name.split("\\", 1)[1] + "@ce"
    return name


def plan(args):
    global JOIN_DELAY, START_DELAY
    JOIN_DELAY = getattr(args, "join_delay", None) or JOIN_DELAY
    START_DELAY = getattr(args, "start_delay", None) or START_DELAY
    base = getattr(args, "address_base", None) or 10
    variants = ["slayer", "ctf"]
    test = {"HALO_NETWORK_TEST_START": str(START_DELAY), "HALO_NETWORK_TEST_SHOOT": str(SHOOT_INTERVAL)}
    host_test = dict(test, HALO_NETWORK_TEST=f"host:{args.map}:{','.join(variants)}",
        HALO_NETWORK_TEST_KILL=str(KILL_INTERVAL), HALO_NETWORK_TEST_SCORE=str(SCORE_TO_WIN))
    join_test = dict(test, HALO_NETWORK_TEST="join")
    layout = [("ours-host", "ours", "theirs", 0), ("theirs-host", "theirs", "ours", 10)]
    if not args.no_baseline:
        layout += [("control-ours", "ours", "ours", 40), ("control-theirs", "theirs", "theirs", 50)]
    directions = []
    for number, (name, host_kind, client_kind, offset) in enumerate(layout):
        if args.only and name not in args.only and not name.startswith("control-"):
            continue
        host_address, client_address, late_address = addresses(base + offset)
        # (no scripted input on the host: in the lobby it would pick other maps)
        host = dict(host_test, HALO_NET_ADDRESS=host_address, HALO_NET_BROADCAST=f"{client_address},{late_address}",
            HALO_NETWORK_TEST=f"host:{host_map(args.map, host_kind)}:{','.join(variants)}")
        client = dict(join_test, HALO_NET_ADDRESS=client_address, HALO_NET_BROADCAST=host_address,
            HALO_TEST_INPUT=f"bot:{17 + number}")
        late = dict(join_test, HALO_NET_ADDRESS=late_address, HALO_NET_BROADCAST=host_address,
            HALO_TEST_INPUT=f"bot:{29 + number}")
        back = dict(client, HALO_TEST_INPUT=f"bot:{37 + number}")
        directions.append({"name": name, "variants": variants, "copies": [
            (f"{name}/host-{host_kind}", host_kind, host, 0, HARD_LIMIT),
            (f"{name}/client-{client_kind}", client_kind, client, JOIN_DELAY, HARD_LIMIT),
            (f"{name}/late-{client_kind}", client_kind, late, None, HARD_LIMIT),
            (f"{name}/back-{client_kind}", client_kind, back, None, HARD_LIMIT)],
            "phase": 1 if name.startswith("control-") else 0})
    if getattr(args, "server", None) and (not args.only or "server" in args.only):
        server_address, first, second = addresses(base + 20)
        directions.append({"name": "server", "variants": ["slayer"], "server": True, "phase": 0, "copies": [
            ("server/server-ours", "server", {"HALO_NET_ADDRESS": server_address,
                "HALO_NET_BROADCAST": f"{first},{second}", "HALO_DEDICATED": "crossplay-playlist.txt",
                "HALO_DEDICATED_MINIMUM_PLAYERS": "2", "HALO_DEDICATED_CONSOLE": "false"}, 0, SERVER_RUN_SECONDS),
            ("server/client-theirs", "theirs", dict(join_test, HALO_NET_ADDRESS=first, HALO_NET_BROADCAST=server_address,
                HALO_TEST_INPUT="bot:41"), 4, SERVER_RUN_SECONDS),
            ("server/client-ours", "ours", dict(join_test, HALO_NET_ADDRESS=second, HALO_NET_BROADCAST=server_address,
                HALO_TEST_INPUT="bot:43"), 6, SERVER_RUN_SECONDS)]})
    return directions


def run(args):
    for path, what in ((args.ours, "--ours"), (args.theirs, "--theirs"), (args.maps, "--maps")):
        if not os.path.exists(path):
            log(f"{what} {path}: not there")
            return 2
    if args.server and not os.path.exists(args.server):
        log(f"--server {args.server}: not there")
        return 2
    if not sys.platform.startswith("linux"):
        log("not Linux: the loopback addresses 127.0.0.10 to .79 must be set up first")
    args.work = os.path.abspath(args.work)
    os.makedirs(args.work, exist_ok=True)
    directions = plan(args)
    builds = {"ours": (args.ours, args.ours_seed), "theirs": (args.theirs, args.theirs_seed),
        "server": (args.server, None)}
    for direction in directions:
        direction["live"] = []
        for name, kind, env, delay, until in direction["copies"]:
            binary, seed = builds[kind]
            copy = Copy(args.work, name, binary, "ours" if kind == "server" else kind, args.maps, env, seed,
                args.ours_table, args.ce_maps)
            if kind == "server":
                with open(os.path.join(copy.folder, "data", "crossplay-playlist.txt"), "w") as file:
                    file.write(f"{args.map.replace('custom_maps' + chr(92), '')}{'@ce' if chr(92) in args.map else ''} slayer\n")
            direction["live"].append({"copy": copy, "delay": delay, "until": until})
    started = time.time()
    # the cross-play directions (and the server) first, then the baselines:
    # at most about ten copies at once
    for phase in (0, 1):
        playing = [direction for direction in directions if direction["phase"] == phase]
        if playing:
            play(playing)
    return report(args, directions, time.time() - started)


def play(directions):
    """Runs the directions' copies on one clock. In each direction a second
    client joins the first game in progress some seconds in, the first
    client leaves, and comes back (a new copy at its address); the direction
    ends once the second game has been played a while, or the host has
    logged no play for a while (or none at all)."""
    started = time.time()
    everything = [entry for direction in directions for entry in direction["live"]]
    try:
        while True:
            now = time.time() - started
            for direction in directions:
                if direction.get("server"):
                    continue
                host, client, late, back = direction["live"]
                if host["copy"].process is None:
                    continue
                games = progress(host["copy"])
                if games != direction.get("seen"):
                    direction["seen"], direction["seen_at"] = games, now
                first = games[0] if games else 0
                if late["delay"] is None and first >= LATE_AT:
                    late["delay"] = now
                if client["until"] == HARD_LIMIT and first >= LEAVE_AT:
                    log(f"{direction['name']}: the first client leaves {first} s into game 1")
                    client["until"] = now
                if back["delay"] is None and first >= BACK_AT:
                    back["delay"] = now
                if (len(games) >= 2 and games[1] >= SECOND_GAME_PLAY) or \
                        (games and now - direction["seen_at"] >= QUIET_LIMIT) or (not games and now >= NO_GAME_LIMIT):
                    for entry in direction["live"]:
                        entry["until"] = min(entry["until"], now)
            busy = False
            for entry in everything:
                copy, delay, until = entry["copy"], entry["delay"], entry["until"]
                if delay is not None and copy.process is None and copy.returncode is None and delay <= now < until:
                    copy.start()
                if copy.process is not None and now >= until:
                    copy.stop()
                    log(f"{copy.name}: stopped at {int(now)} s")
                elif copy.process is not None and not copy.running():
                    copy.stop()
                    log(f"{copy.name}: exited by itself at {int(now)} s (status {copy.returncode})")
                if copy.process is not None or (delay is not None and copy.returncode is None and now < until):
                    busy = True
            if not busy:
                break
            time.sleep(1)
    finally:
        for entry in everything:
            entry["copy"].stop()


def report(args, directions, seconds):
    result = {"pass": True, "seconds": round(seconds), "map": args.map,
        "ours": {"binary": os.path.abspath(args.ours) if args.ours else None, "label": args.ours_label,
            "table": json.load(open(args.ours_table)) if args.ours_table else None},
        "theirs": {"binary": os.path.abspath(args.theirs) if args.theirs else None, "label": args.theirs_label},
        "directions": []}
    checked = {}
    for direction in directions:
        copies = [entry["copy"] for entry in direction["live"]]
        if direction.get("server"):
            steps, versions = check_server(*copies), []
        else:
            steps, versions = check_direction(*copies, direction["variants"])
        steps["stability"], warnings = stability(copies)
        checked[direction["name"]] = (steps, versions, warnings)
    for direction in directions:
        name = direction["name"]
        steps, versions, warnings = checked[name]
        host_kind = direction["copies"][0][1]
        baseline = checked.get(f"control-{host_kind}") if not name.startswith("control-") and name != "server" else None
        passed = True
        for step_name, value in steps.items():
            if value["pass"]:
                continue
            if host_kind == "theirs" and step_name in THEIR_HOST_QUIRKS:
                value["their_host_quirk"] = True
            elif baseline and step_name in baseline[0] and not baseline[0][step_name]["pass"] and \
                    baseline[0].get("join", {}).get("pass"):
                value["baseline_fails"] = True
            else:
                passed = False
        if not name.startswith("control-"):
            result["pass"] = result["pass"] and passed
        result["directions"].append({"name": name, "pass": passed, "steps": steps, "warnings": warnings,
            "versions": versions[:16], "logs": os.path.join(os.path.abspath(args.work), name)})
        log(f"{name}: {'PASS' if passed else 'FAIL'}")
        for step_name, value in steps.items():
            verdict = "pass" if value["pass"] else ("fails in the host build's baseline too" if value.get("baseline_fails")
                else "not counted (an OpenCE host's quirk)" if value.get("their_host_quirk") else "FAIL")
            log(f"  {step_name}: {verdict}: {value['detail']}")
    with open(args.out, "w") as file:
        json.dump(result, file, indent=2)
    log(f"{'PASS' if result['pass'] else 'FAIL'} ({result['seconds']} s); {args.out}")
    return 0 if result["pass"] else 1


def check_again(args):
    """The checks of an earlier run's logs, without playing again."""
    args.work = os.path.abspath(args.work)
    directions = plan(args)
    for direction in directions:
        direction["live"] = [{"copy": Copy(args.work, name, None, "ours" if kind == "server" else kind, None, None),
            "delay": 0, "until": 0} for name, kind, _env, _delay, _until in direction["copies"]]
        direction["live"] = [entry for entry in direction["live"] if os.path.isdir(entry["copy"].folder)] \
            if direction.get("server") else direction["live"]
        if direction.get("server") and len(direction["live"]) != 3:
            directions.remove(direction)
    directions = [d for d in directions if os.path.isdir(os.path.join(args.work, d["name"]))]
    return report(args, directions, 0)


# ---------- building a side

def build(args):
    """A release build of a commit, from a repository with it."""
    out = os.path.abspath(args.out)
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(out)
    archive = subprocess.Popen(["git", "-C", args.repo, "archive", args.ref], stdout=subprocess.PIPE)
    subprocess.run(["tar", "-x", "-C", out], stdin=archive.stdout, check=True)
    if archive.wait():
        log(f"git archive {args.ref}: failed")
        return 2
    subprocess.run([sys.executable, "configure.py", "--release", "--pgo=off", "--lto=off"], cwd=out, check=True,
        stdout=subprocess.DEVNULL)
    targets = ["linux"] + (["server-x86"] if args.server else [])
    subprocess.run(["ninja", "-j", str(os.cpu_count() or 4)] + targets, cwd=out, check=True)
    log(f"built {args.ref}: {os.path.join(out, 'build', 'linux', 'halo')}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    run_parser = commands.add_parser("run", help="play the matrix and write the result")
    run_parser.add_argument("--ours", required=True, help="ChupathingyCE's game (a Linux build)")
    run_parser.add_argument("--theirs", required=True, help="OpenCE's game (a Linux build)")
    run_parser.add_argument("--server", help="ChupathingyCE's dedicated server, for the server step")
    run_parser.add_argument("--ce-maps", help="a folder of Custom Edition maps (and bitmaps.map, sounds.map, loc.map), "
                            "as both builds' custom_maps; then --map custom_maps\\NAME plays one")
    run_parser.add_argument("--ours-table", help="a legacy table file for our copies (HALO_LEGACY_TABLE)")
    run_parser.add_argument("--theirs-seed", help="a save root to start OpenCE's copies from (map caches made)")
    run_parser.add_argument("--ours-seed", help="the same for our copies")
    run_parser.add_argument("--ours-label", default="")
    run_parser.add_argument("--theirs-label", default="")
    run_parser.add_argument("--start-delay", type=float, help=f"seconds from a host's setup to its start ({START_DELAY})")
    run_parser.add_argument("--join-delay", type=float, help=f"seconds from a host's launch to its client's ({JOIN_DELAY})")
    run_parser.add_argument("--address-base", type=int, help="the first loopback address's last byte (10): "
        "to run several at once (each takes 70)")
    check_parser = commands.add_parser("check", help="check the logs of an earlier run again")
    for sub in (run_parser, check_parser):
        sub.add_argument("--maps", required=sub is run_parser, help="the Xbox maps folder (NTSC)")
        sub.add_argument("--map", default="bloodgulch")
        sub.add_argument("--only", action="append", choices=["ours-host", "theirs-host", "server"])
        sub.add_argument("--no-baseline", action="store_true", help="leave out each build playing itself")
        sub.add_argument("--work", required=True, help="a scratch folder for the copies' folders and logs")
        sub.add_argument("--out", required=True, help="the JSON result")
    build_parser = commands.add_parser("build", help="a release build of a commit")
    build_parser.add_argument("--repo", required=True)
    build_parser.add_argument("--ref", required=True)
    build_parser.add_argument("--out", required=True)
    build_parser.add_argument("--server", action="store_true", help="the dedicated server too (ours)")
    args = parser.parse_args()
    if args.command == "build":
        return build(args)
    if args.command == "check":
        args.ours = args.theirs = None
        args.ours_table = None
        args.ours_label = args.theirs_label = ""
        args.server = os.path.isdir(os.path.join(args.work, "server")) or None
        return check_again(args)
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
