#!/usr/bin/env python3
"""Server monitor: how a public game's host really behaves, from one quiet guest.

Runs the game headless as the server monitor (port/linux/game/net_monitor.c,
debug.network_test "monitor:<listed name>"): it joins the game listed under
that name from the game list, plays nothing, writes a line of JSON every ten
seconds and leaves. This tool starts it in a folder of its own (its own
HOME, saves and data root), waits for it, and writes a report: JSON and a
short Markdown page.

    python3 tools/server_monitor.py run --game build/linux/chupathingy \\
        --data-root-script ~/halo-maps/new-data-root.sh \\
        --server "[C] Red vs Blue" --minutes 10 --work /tmp/monitor --out reports/
    python3 tools/server_monitor.py report --work /tmp/monitor/<run> --out reports/
    python3 tools/server_monitor.py compare reports/a.json reports/b.json --out compare.md

The guest's rules are the game's own (net_monitor.c): one connection, a
plain player name (--name, "Chupa" unless set), no input, no chat, no voice,
room kept for players (--free-slots, 2 unless set), short sessions, and no
joining again when the host drops it. The tool never runs it twice at once
for a server, and never again after a failed join. Nothing in a report names
an address: the game is named by its listed name, and any address in a log
line copied into a report is replaced with a tag.
"""

import argparse
import datetime
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "port", "linux", "game", "network_distributed.h")
# (net_monitor.c's buckets: the gaps between the host's new ticks, ms)
GAP_BOUNDS = ["<40", "40-49", "50-69", "70-99", "100-149", "150-249", "250-499", "500-999", "1000+"]
CORRECTION_KINDS = ["own_player", "remote_player", "own_vehicle", "remote_vehicle", "object", "actor", "host"]
EXIT_REASONS = {0: "left as planned", 3: "not listed", 4: "not joinable", 5: "near full", 6: "join failed",
                7: "dropped by the host"}
ADDRESS = re.compile(r"\b(?:\d{1,3}\.){3}\d{1,3}(?::\d+)?\b|\b(?:[0-9a-f]{1,4}:){3,7}[0-9a-f]{1,4}\b", re.I)
# the game's log lines worth a look, and ones that only look like it
LOG_WORDS = ("warning", "desync", "out of sync", "mismatch", "refused", "dropped", "unable to join", "malformed",
             "unknown message", "timed out", "kicked", "disconnect")
LOG_IGNORED = ("server monitor", "network version", "movie", "lights:")
# a host tick rate below this, or a gap this long, makes a window a dip
DIP_TICKS = 27.0
DIP_GAP_MS = 250


def redact(text):
    return ADDRESS.sub("[address]", text)


def kind_names():
    """The distributed message kinds by number, from network_distributed.h."""
    names = {}
    try:
        text = open(HEADER, encoding="utf-8").read()
    except OSError:
        return names
    number = -1
    for match in re.finditer(r"^\s*_distributed_message_(\w+)(?:\s*=\s*(\d+))?\s*,", text, re.M):
        number = int(match.group(2)) if match.group(2) else number + 1
        names[number] = match.group(1)
    return names


def safe_name(name):
    return re.sub(r"[^A-Za-z0-9]+", "-", name).strip("-").lower() or "server"


# ---------- run

def run(args):
    if not os.path.exists(args.game):
        sys.exit(f"server_monitor: no game at {args.game}")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    folder = os.path.join(os.path.abspath(args.work), f"{safe_name(args.server)}-{stamp}")
    home, saves, data = (os.path.join(folder, part) for part in ("home", "saves", "data"))
    for path in (home, saves):
        os.makedirs(path)
    subprocess.run([os.path.expanduser(args.data_root_script), data], check=True, stdout=subprocess.DEVNULL)
    output = os.path.join(folder, "monitor.jsonl")
    env = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "HOME": home,
        "HALO_SAVE_ROOT": saves,
        "HALO_DATA_ROOT": data,
        "SDL_VIDEO_DRIVER": "dummy",
        "SDL_AUDIO_DRIVER": "dummy",
        # the monitor
        "HALO_NETWORK_TEST": f"monitor:{args.server}",
        "HALO_MONITOR_NAME": args.name,
        "HALO_MONITOR_OUTPUT": output,
        "HALO_MONITOR_MINUTES": str(args.minutes),
        "HALO_MONITOR_GAMES": str(args.games),
        "HALO_MONITOR_FREE_SLOTS": str(args.free_slots),
        # a quiet guest: internet play to join, nothing listed or reported,
        # no voice, no profile, no map downloads, no updates, no Discord
        "HALO_NET_ONLINE": "true",
        "HALO_NET_LIST_GAMES": "false",
        "HALO_NET_REPORT_GAMES": "false",
        "HALO_NET_ALLOW_UPNP": "false",
        "HALO_NET_JOIN_FROM_CLIPBOARD": "never",
        "HALO_NET_VOICE": "false",
        "HALO_VOICE_CHAT": "off",
        "HALO_NET_SHARE_PROFILE": "false",
        "HALO_NET_MAP_DOWNLOADS_FROM_HOST": "off",
        "HALO_UPDATE_AUTO": "false",
        "HALO_DISCORD_APPLICATION": "",
        "HALO_DISCORD_PRESENCE": "off",
        # (the Xbox's join: no lobby, whose preview of a game under way waits
        # for JOIN THE GAME, which nobody presses here)
        "HALO_LOBBY": "false",
    }
    if "LD_LIBRARY_PATH" in os.environ:
        env["LD_LIBRARY_PATH"] = os.environ["LD_LIBRARY_PATH"]
    meta = {"server": args.server, "player": args.name, "minutes": args.minutes, "games": args.games,
            "free_slots": args.free_slots, "started": datetime.datetime.now().astimezone().isoformat(timespec="seconds")}
    with open(os.path.join(folder, "run.json"), "w") as file:
        json.dump(meta, file, indent=1)
    print(f"server_monitor: {args.server}: running in {folder}", flush=True)
    # (the game leaves by itself; past its own limit and a margin, stopped)
    limit = (args.minutes + 25) * 60 if args.minutes > 0 else 3 * 3600
    with open(os.path.join(folder, "out.log"), "wb") as log:
        process = subprocess.Popen([os.path.abspath(args.game)], cwd=data, env=env, stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        started = time.time()
        while process.poll() is None and time.time() - started < limit:
            time.sleep(2)
        if process.poll() is None:
            print("server_monitor: past the time limit; stopping it", flush=True)
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    meta["exit_code"] = process.returncode
    meta["ended"] = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
    with open(os.path.join(folder, "run.json"), "w") as file:
        json.dump(meta, file, indent=1)
    # (the map isn't the monitor's to keep: the data root is links only)
    report = make_report(folder)
    write_report(report, args.out)
    if not args.keep:
        shutil.rmtree(os.path.join(folder, "home"), ignore_errors=True)


# ---------- report

def read_lines(folder):
    windows, events = [], []
    path = os.path.join(folder, "monitor.jsonl")
    if os.path.exists(path):
        for line in open(path, encoding="utf-8", errors="replace"):
            line = line.strip()
            if not line:
                continue
            try:
                entry = json.loads(line)
            except ValueError:
                continue
            (events if "event" in entry else windows).append(entry)
    return windows, events


def log_lines(folder):
    """The game's own log (debug.txt, wherever this run put it) and its output."""
    lines = []
    for base, _, files in os.walk(folder):
        for name in files:
            if name in ("debug.txt", "out.log"):
                try:
                    lines += open(os.path.join(base, name), encoding="utf-8", errors="replace").read().splitlines()
                except OSError:
                    pass
    return lines


def spread(values):
    values = [value for value in values if value is not None]
    if not values:
        return {"mean": None, "min": None, "max": None}
    return {"mean": round(statistics.fmean(values), 2), "min": round(min(values), 2), "max": round(max(values), 2)}


def corr(window, kind):
    """A window's corrections of a kind: count, snaps, mean and largest distance
    (none of a kind an older build didn't count)."""
    return window["corrections"].get(kind, [0, 0, 0.0, 0.0])


def make_report(folder):
    meta = json.load(open(os.path.join(folder, "run.json")))
    windows, events = read_lines(folder)
    names = kind_names()
    # (windows in a game, a full window long, measure the host)
    played = [w for w in windows if w.get("whole_game", w.get("state") == "game") and w.get("window", 0) >= 5]
    seconds = sum(w.get("window", 0) for w in windows)
    played_seconds = sum(w["window"] for w in played)
    report = {"server": meta["server"], "player": meta["player"], "started": meta.get("started"),
              "exit_code": meta.get("exit_code"), "exit": EXIT_REASONS.get(meta.get("exit_code"), "stopped"),
              "joined_seconds": round(seconds), "played_seconds": round(played_seconds)}
    for event in events:
        if event["event"] == "listed":
            report["listed"] = {key: event.get(key) for key in ("name", "map", "players", "maximum", "version",
                                                                 "opence_server", "region", "ping")}
        elif event["event"] == "leave":
            report["leave_reason"] = event.get("reason")
            report["games_ended"] = event.get("games_ended")
    report["maps"] = [{"t": e["t"], "map": e.get("map"), "variant": e.get("variant"), "players": e.get("players"),
                       "maximum": e.get("maximum")} for e in events if e["event"] == "map"]
    report["games_started"] = sum(1 for e in events if e["event"] == "game_start")
    # the host's ticks
    gaps = [0] * len(GAP_BOUNDS)
    for w in played:
        for index, count in enumerate(w["host"].get("gaps", [])[:len(GAP_BOUNDS)]):
            gaps[index] += count
    total_gaps = sum(gaps) or 1
    report["host"] = {
        "ticks_per_second": spread([w["host"]["ticks_per_second"] for w in played]),
        "gap_mean_ms": spread([w["host"]["gap_mean_ms"] for w in played]),
        "gap_spread_ms": spread([w["host"]["gap_spread_ms"] for w in played]),
        "gap_largest_ms": max([w["host"]["gap_largest_ms"] for w in played], default=None),
        "clock": spread([w["host"]["clock"] for w in played if w["host"].get("clock")]),
        "skipped_ticks": sum(w["host"]["skipped_ticks"] for w in played),
        "gaps": dict(zip(GAP_BOUNDS, gaps)),
        "gaps_over_100ms_percent": round(100.0 * sum(gaps[4:]) / total_gaps, 2),
    }
    # corrections: by kind, and as the net health line counts them (every
    # kind but the host's, a 10 second window)
    corrections = {}
    for index, kind in enumerate(CORRECTION_KINDS):
        counts = [corr(w, kind)[0] for w in played]
        snaps = sum(corr(w, kind)[1] for w in played)
        total = sum(counts)
        distance = sum(corr(w, kind)[0] * corr(w, kind)[2] for w in played)
        corrections[kind] = {"total": total, "per_window": spread(counts), "snaps": snaps,
                             "mean_distance": round(distance / total, 3) if total else None,
                             "largest_distance": max([corr(w, kind)[3] for w in played], default=None)}
    report["corrections"] = corrections
    health = [sum(corr(w, kind)[0] for kind in CORRECTION_KINDS if kind != "host") for w in played]
    own = [corr(w, "own_player")[0] + corr(w, "own_vehicle")[0] for w in played]
    report["net_health_corrections_per_window"] = spread(health)
    report["own_corrections_per_window"] = spread(own)
    # messages and datagrams
    def rates(key):
        totals = {}
        for w in windows:
            for kind, (count, size) in w.get(key, {}).items():
                name = names.get(int(kind), f"kind {kind}")
                entry = totals.setdefault(name, [0, 0])
                entry[0] += count
                entry[1] += size
        return {name: {"per_second": round(count / seconds, 2) if seconds else 0,
                       "bytes_per_second": round(size / seconds, 1) if seconds else 0}
                for name, (count, size) in sorted(totals.items(), key=lambda item: -item[1][0])}
    report["messages_in"] = rates("messages_in")
    report["messages_out"] = rates("messages_out")
    datagrams = {key: sum(w["datagrams"][key] for w in windows) for key in ("in", "in_bytes", "out", "out_bytes")}
    report["datagrams"] = {key: round(value / seconds, 1) if seconds else 0 for key, value in datagrams.items()}
    report["datagrams_per_second_in_game"] = {
        "in": spread([w["datagrams"]["in"] / w["window"] for w in played]),
        "out": spread([w["datagrams"]["out"] / w["window"] for w in played])}
    # chat
    chat = {key: sum(w["chat"][key] for w in windows) for key in ("server_lines", "server_bytes", "player_lines",
                                                                   "player_bytes")}
    chat["server_lines_per_minute"] = round(chat["server_lines"] * 60 / seconds, 2) if seconds else 0
    chat["player_lines_per_minute"] = round(chat["player_lines"] * 60 / seconds, 2) if seconds else 0
    chat["server_lines_heard"] = [{"text": redact(e.get("text", "")), "count": e.get("count")}
                                  for e in events if e["event"] == "server_line_total"]
    report["chat"] = chat
    # players and bots
    report["players"] = {
        "listed": spread([w["players"]["listed"] for w in played]),
        "maximum": max([w["players"]["maximum"] for w in windows], default=None),
        "people": spread([w["players"]["people"] for w in played]),
        "bots": spread([w["players"]["bots"] for w in played]),
        "most_on_one_machine": max([w["players"]["most_on_one_machine"] for w in played], default=None),
        "unit_states_per_second": {
            kind: spread([w["players"]["unit_states_per_second"][kind] for w in played
                          if w["players"]["unit_states_per_second"][kind]]) for kind in ("own", "person", "bot")},
    }
    report["warnings"] = {key: sum(w["warnings"][key] for w in windows)
                          for key in ("unknown_kind", "out_of_window", "stale", "malformed")}
    report["client"] = {
        "frame_longest_ms": max([w["client"]["frame_longest_ms"] for w in played], default=None),
        "frames_over_50ms": sum(w["client"]["frames_over_50ms"] for w in played),
        "frames_over_100ms": sum(w["client"]["frames_over_100ms"] for w in played),
        "frames_per_second": spread([w["client"]["frames"] / w["window"] for w in played]),
    }
    # dips: windows where the host's ticks came slow or with a long gap,
    # and whose side they were on (this machine's frames ran fine: the host's
    # side, or the way between)
    def wall(t):
        try:
            start = datetime.datetime.fromisoformat(meta["started"])
        except (KeyError, TypeError, ValueError):
            return None
        return (start + datetime.timedelta(seconds=t)).strftime("%H:%M:%S")

    dips = []
    for w in played:
        host = w["host"]
        if host["ticks_per_second"] < DIP_TICKS or host["gap_largest_ms"] >= DIP_GAP_MS:
            client_slow = w["client"]["frame_longest_ms"] >= host["gap_largest_ms"] * 0.8
            dips.append({"t": w["t"], "wall": wall(w["t"]), "ticks_per_second": host["ticks_per_second"],
                         "gap_largest_ms": host["gap_largest_ms"], "skipped_ticks": host["skipped_ticks"],
                         "clock": host.get("clock"), "frame_longest_ms": w["client"]["frame_longest_ms"],
                         "datagrams_in": w["datagrams"]["in"],
                         "corrections": sum(corr(w, k)[0] for k in CORRECTION_KINDS if k != "host"),
                         "side": "this machine" if client_slow else
                         "the host's clock" if host.get("clock") and host["clock"] < 0.95 else "the host or the way"})
    report["dips"] = dips
    report["dip_windows_percent"] = round(100.0 * len(dips) / len(played), 1) if played else None
    # the game's own warnings, by their words (numbers and addresses out)
    warned = {}
    for line in log_lines(folder):
        lower = line.lower()
        if any(word in lower for word in LOG_WORDS) and not any(word in lower for word in LOG_IGNORED):
            key = re.sub(r"\d+", "#", redact(line.split("] ", 1)[-1]))[:140]
            warned[key] = warned.get(key, 0) + 1
    report["log_warnings"] = [{"line": key, "count": count}
                              for key, count in sorted(warned.items(), key=lambda item: -item[1])[:15]]
    return report


def fmt(value, unit=""):
    if value is None:
        return "-"
    if isinstance(value, dict):
        if value.get("mean") is None:
            return "-"
        return f"{value['mean']}{unit} ({value['min']}-{value['max']})"
    return f"{value}{unit}"


def markdown(report):
    host, corrections = report["host"], report["corrections"]
    lines = [f"# Server monitor: {report['server']}", "",
             f"Started {report.get('started')}, joined {report['joined_seconds']} s, in a game {report['played_seconds']} s, "
             f"{report['games_started']} game(s) started, left: {report.get('leave_reason') or report['exit']}.", ""]
    listed = report.get("listed")
    if listed:
        lines += [f"Listed as \"{listed['name']}\" on {listed['map']}, {listed['players']}/{listed['maximum']} players, "
                  f"version {listed['version']}{', OpenCE server' if listed.get('opence_server') else ''}.", ""]
    if report["maps"]:
        lines += ["Maps: " + ", ".join(f"{m['map']} ({m['variant']})" for m in report["maps"]), ""]
    lines += ["| | |", "| --- | --- |",
              f"| Host ticks a second | {fmt(host['ticks_per_second'])} |",
              f"| Gap between ticks (ms) | {fmt(host['gap_mean_ms'])}, spread {fmt(host['gap_spread_ms'])}, largest {fmt(host['gap_largest_ms'])} |",
              f"| Gaps over 100 ms | {host['gaps_over_100ms_percent']}% |",
              f"| Ticks skipped | {host['skipped_ticks']} |",
              f"| Host clock vs ours | {fmt(host['clock'])} |",
              f"| Corrections per 10 s (net health) | {fmt(report['net_health_corrections_per_window'])} |",
              f"| ...of them our own player | {fmt(report['own_corrections_per_window'])} |",
              f"| Remote player corrections | {corrections['remote_player']['total']} total, {corrections['remote_player']['snaps']} snaps, mean {fmt(corrections['remote_player']['mean_distance'])} wu |",
              f"| Vehicle / other object corrections | {corrections['remote_vehicle']['total'] + corrections['own_vehicle']['total']} / {corrections['object']['total']} |",
              f"| Datagrams a second in / out | {fmt(report['datagrams'].get('in'))} / {fmt(report['datagrams'].get('out'))} |",
              f"| Bytes a second in / out | {fmt(report['datagrams'].get('in_bytes'))} / {fmt(report['datagrams'].get('out_bytes'))} |",
              f"| Players (people / bots) | {fmt(report['players']['listed'])} ({fmt(report['players']['people'])} / {fmt(report['players']['bots'])}) of {fmt(report['players']['maximum'])} |",
              f"| Unit updates a second: person / bot / own | {fmt(report['players']['unit_states_per_second']['person'])} / {fmt(report['players']['unit_states_per_second']['bot'])} / {fmt(report['players']['unit_states_per_second']['own'])} |",
              f"| Server lines | {report['chat']['server_lines']} ({report['chat']['server_lines_per_minute']}/min, {report['chat']['server_bytes']} bytes) |",
              f"| Player lines | {report['chat']['player_lines']} ({report['chat']['player_lines_per_minute']}/min) |",
              f"| Warnings | {', '.join(f'{k} {v}' for k, v in report['warnings'].items())} |",
              f"| Our frames: longest, over 50 ms | {fmt(report['client']['frame_longest_ms'])} ms, {report['client']['frames_over_50ms']} |",
              f"| Dip windows | {len(report['dips'])} ({fmt(report['dip_windows_percent'], '%')}) |", ""]
    lines += ["Gaps between the host's ticks: " + ", ".join(f"{k} ms {v}" for k, v in host["gaps"].items()), ""]
    top = list(report["messages_in"].items())[:8]
    if top:
        lines += ["Messages in, a second: " + ", ".join(f"{name} {v['per_second']}" for name, v in top), ""]
    if report["chat"]["server_lines_heard"]:
        lines += ["Server lines heard:", ""]
        lines += [f"- {entry['count']}x \"{entry['text']}\"" for entry in report["chat"]["server_lines_heard"]]
        lines.append("")
    if report["dips"]:
        lines += ["Dips (10 s windows):", "", "| time | ticks/s | largest gap | skipped | clock | our longest frame | corrections | side |",
                  "| --- | --- | --- | --- | --- | --- | --- | --- |"]
        lines += [f"| {d.get('wall') or d['t']} | {d['ticks_per_second']} | {d['gap_largest_ms']} | {d['skipped_ticks']} | {d['clock']} | "
                  f"{d['frame_longest_ms']} | {d['corrections']} | {d['side']} |" for d in report["dips"][:20]]
        lines.append("")
    if report["log_warnings"]:
        lines += ["Log lines worth a look:", ""]
        lines += [f"- {entry['count']}x `{entry['line']}`" for entry in report["log_warnings"]]
        lines.append("")
    return "\n".join(lines)


def write_report(report, out):
    os.makedirs(out, exist_ok=True)
    stamp = (report.get("started") or "")[:19].replace(":", "").replace("-", "")
    base = os.path.join(out, f"{safe_name(report['server'])}-{stamp}")
    with open(base + ".json", "w") as file:
        json.dump(report, file, indent=1)
    with open(base + ".md", "w") as file:
        file.write(markdown(report))
    print(f"server_monitor: report {base}.json, {base}.md", flush=True)


def compare(args):
    reports = [json.load(open(path)) for path in args.reports]
    rows = [
        ("Left", lambda r: r.get("leave_reason") or r["exit"]),
        ("In a game (s)", lambda r: r["played_seconds"]),
        ("Maps", lambda r: ", ".join(sorted({m["map"].split("\\")[-1] for m in r["maps"]})) or "-"),
        ("Players / bots", lambda r: f"{fmt(r['players']['listed'])} / {fmt(r['players']['bots'])}"),
        ("Host ticks a second", lambda r: fmt(r["host"]["ticks_per_second"])),
        ("Tick gap mean / spread (ms)", lambda r: f"{fmt(r['host']['gap_mean_ms']['mean'])} / {fmt(r['host']['gap_spread_ms']['mean'])}"),
        ("Largest gap (ms)", lambda r: fmt(r["host"]["gap_largest_ms"])),
        ("Gaps over 100 ms", lambda r: f"{r['host']['gaps_over_100ms_percent']}%"),
        ("Ticks skipped", lambda r: r["host"]["skipped_ticks"]),
        ("Host clock vs ours", lambda r: fmt(r["host"]["clock"])),
        ("Corrections / 10 s (net health)", lambda r: fmt(r["net_health_corrections_per_window"])),
        ("...our own player", lambda r: fmt(r["own_corrections_per_window"])),
        ("Remote player: total, snaps, mean wu", lambda r: f"{r['corrections']['remote_player']['total']}, {r['corrections']['remote_player']['snaps']}, {fmt(r['corrections']['remote_player']['mean_distance'])}"),
        ("Unit updates/s: person / bot", lambda r: f"{fmt(r['players']['unit_states_per_second']['person']['mean'])} / {fmt(r['players']['unit_states_per_second']['bot']['mean'])}"),
        ("Datagrams/s in / out", lambda r: f"{r['datagrams'].get('in')} / {r['datagrams'].get('out')}"),
        ("Bytes/s in / out", lambda r: f"{r['datagrams'].get('in_bytes')} / {r['datagrams'].get('out_bytes')}"),
        ("Server lines (per min)", lambda r: f"{r['chat']['server_lines']} ({r['chat']['server_lines_per_minute']})"),
        ("Warnings", lambda r: ", ".join(f"{k} {v}" for k, v in r["warnings"].items() if v) or "none"),
        ("Our longest frame (ms)", lambda r: fmt(r["client"]["frame_longest_ms"])),
        ("Dip windows", lambda r: f"{len(r['dips'])} ({fmt(r['dip_windows_percent'], '%')})"),
    ]
    lines = ["| | " + " | ".join(r["server"] for r in reports) + " |", "| --- |" + " --- |" * len(reports)]
    for label, get in rows:
        cells = []
        for r in reports:
            try:
                cells.append(str(get(r)))
            except (KeyError, TypeError):
                cells.append("-")
        lines.append(f"| {label} | " + " | ".join(cells) + " |")
    text = "\n".join(lines) + "\n"
    if args.out:
        open(args.out, "w").write(text)
    print(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("run", help="join one listed game and report")
    command.add_argument("--game", required=True, help="the game's binary (a desktop build with the game list)")
    command.add_argument("--data-root-script", required=True,
                         help="a script that makes a data root of links to the maps: <script> <dir>")
    command.add_argument("--server", required=True, help="the game's listed name")
    command.add_argument("--name", default="Chupa", help="the monitor's player name")
    command.add_argument("--minutes", type=float, default=10.0)
    command.add_argument("--games", type=int, default=0)
    command.add_argument("--free-slots", type=int, default=2)
    command.add_argument("--work", required=True)
    command.add_argument("--out", required=True)
    command.add_argument("--keep", action="store_true", help="keep the run's HOME")
    command = commands.add_parser("report", help="report an earlier run again")
    command.add_argument("--work", required=True, help="the run's folder")
    command.add_argument("--out", required=True)
    command = commands.add_parser("compare", help="reports side by side, as Markdown")
    command.add_argument("reports", nargs="+")
    command.add_argument("--out")
    args = parser.parse_args()
    if args.command == "run":
        run(args)
    elif args.command == "report":
        write_report(make_report(args.work), args.out)
    else:
        compare(args)


if __name__ == "__main__":
    main()
