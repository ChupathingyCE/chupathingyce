#!/usr/bin/env python3
"""The game list server (configure.py --game-browser).

A copy of the game that hosts a system link game announces it here with its
invite (port/linux/src/browser.c), and repeats the announcement while it
hosts; copies of the game that browse get the list and join a game through
its invite, as with an invite link. Nothing of the game itself goes through
this server: it only keeps the list.

    server/list_server.py [--address 127.0.0.1] [--port 8390]

Behind a reverse proxy (Apache, nginx) that adds TLS, on the loopback
address. Standard library only.

Interface (version 1):

    POST /v1/announce   form fields: invite, name, map, engine, players,
                        maximum_players, open, version, score_limit, teams
                        -> 200 "ok <seconds until it expires>"
    POST /v1/withdraw   form field: invite -> 200 "ok"
    GET  /v1/games      -> 200 JSON {"games": [...]}
    GET  /v1/games.txt  -> 200 one game a line, tab separated:
                        invite name map engine players maximum_players
                        open version age score_limit teams
    GET  /v1/health     -> 200 "ok"
    GET  /              -> the site (server/web): the games hosted, each with
                        a Join link (halo://join/..., which the game
                        handles), the last games played and the leaders;
                        /games/N a carnage report, /players/NAME a service
                        record, /leaders, /medals; /static/* the pages'
                        styles and code, /art/* the game's pictures
                        (server/site_art.py)
    POST /v1/report     a finished game's carnage report (JSON, below), from
                        the address that lists its invite -> 200 "ok <id>"
    GET  /v1/reports    -> 200 JSON {"reports": [...]}: the last games, newest
                        first (?limit=, at most 50)
    GET  /v1/reports/N  -> 200 JSON: game N's report
    GET  /v1/history    -> 200 JSON {"reports": [...]}: the last games' full
                        reports, newest first (?limit=, at most 1000)
    GET  /v1/players/NAME -> 200 JSON {"reports": [...]}: the games a player
                        of that name (any case) played, newest first

A carnage report: {"invite", "map", "engine", "teams" (0 or 1),
"score_limit", "duration" (seconds), "team_scores" ([score of team 0,
...]), "players": [{"name", "team", "place" (1 first), "score", "kills",
"assists", "deaths", "betrayals", "suicides", "shots_fired", "shots_hit",
"multikills", and from newer games "color" (0 to 17), the game type's
"flag_grabs", "flag_returns", "flag_scores", "ball_time",
"ball_carrier_kills", "hill_time", "laps", and "medals" ({key: count})},
...]}. Only a game that ends (its host reaches the
postgame) sends one: a game that crashes or is quit is not recorded.
Reports are kept in reports.db in the --data folder, without addresses.

An announcement expires after EXPIRY seconds without another. Each address
may list MAXIMUM_GAMES_PER_ADDRESS games and send REQUESTS_PER_MINUTE
requests a minute. The addresses are kept only to apply those limits.
"""

import argparse
import mimetypes
import json
import re
import sqlite3
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote

EXPIRY = 75
MAXIMUM_GAMES = 512
MAXIMUM_GAMES_PER_ADDRESS = 4
REQUESTS_PER_MINUTE = 60
MAXIMUM_BODY = 2048
MAXIMUM_REPORT_BODY = 65536
MAXIMUM_REPORT_PLAYERS = 128
REPORT_INTERVAL = 60

# (64 digits since network version 8; 44 before, which older builds still list)
MEDAL_KEY = re.compile(r"^[a-z][a-z0-9_]{0,31}$")
PAGES = {"/": "index.html", "/index.html": "index.html", "/leaders": "leaders.html", "/medals": "medals.html"}
STATIC = re.compile(r"^/static/([a-z0-9_-]+\.(?:css|js))$")
ART = re.compile(r"^/art/((?:[a-z0-9_-]+/){0,2}[a-z0-9_-]+\.(?:png|jpg))$")
WEB = Path(__file__).resolve().parent / "web"
INVITE = re.compile(r"^(?:[0-9a-f]{64}|[0-9a-f]{44})$")
FIELDS = {
    "name": 32,
    "map": 64,
}


def clean_text(value: str, limit: int) -> str:
    """printable text only (no tabs or line breaks: the text list's
    separators), at most limit characters"""
    value = "".join(c for c in value if c.isprintable() and c not in "\t\r\n")
    return value.strip()[:limit]


def clean_integer(value: str, low: int, high: int) -> int:
    number = int(value)
    if not low <= number <= high:
        raise ValueError(value)
    return number


class GameList:
    def __init__(self):
        self.lock = threading.Lock()
        self.games = {}  # invite -> record
        self.requests = {}  # address -> [times]
        self.games_per_address = MAXIMUM_GAMES_PER_ADDRESS

    def allow(self, address: str) -> bool:
        now = time.monotonic()
        with self.lock:
            times = [t for t in self.requests.get(address, []) if now - t < 60]
            if len(times) >= REQUESTS_PER_MINUTE:
                self.requests[address] = times
                return False
            times.append(now)
            self.requests[address] = times
            return True

    def expire(self):
        now = time.monotonic()
        with self.lock:
            for invite in [i for i, g in self.games.items() if now - g["time"] > EXPIRY]:
                del self.games[invite]
            for address in [a for a, t in self.requests.items() if not t or now - t[-1] > 60]:
                del self.requests[address]

    def announce(self, address: str, fields: dict) -> str:
        invite = fields["invite"]
        record = {
            "invite": invite,
            "name": fields["name"],
            "map": fields["map"],
            "engine": fields["engine"],
            "players": fields["players"],
            "maximum_players": fields["maximum_players"],
            "open": fields["open"],
            "version": fields["version"],
            "score_limit": fields["score_limit"],
            "teams": fields["teams"],
            "address": address,
            "time": time.monotonic(),
        }
        with self.lock:
            existing = self.games.get(invite)
            if existing and existing["address"] != address:
                return "the invite is listed from another address"
            if not existing:
                if sum(1 for g in self.games.values() if g["address"] == address) >= self.games_per_address:
                    return "too many games from this address"
                if len(self.games) >= MAXIMUM_GAMES:
                    return "the list is full"
            self.games[invite] = record
        return ""

    def listed_game(self, address: str, invite: str):
        """the listing of an invite this address lists, which may report
        (one report a REPORT_INTERVAL): None otherwise"""
        now = time.monotonic()
        with self.lock:
            game = self.games.get(invite)
            if not game or game["address"] != address or now - game.get("reported", -REPORT_INTERVAL) < REPORT_INTERVAL:
                return None
            game["reported"] = now
            return dict(game)

    def withdraw(self, address: str, invite: str):
        with self.lock:
            game = self.games.get(invite)
            if game and game["address"] == address:
                del self.games[invite]

    def snapshot(self):
        now = time.monotonic()
        with self.lock:
            games = sorted(self.games.values(), key=lambda g: (-g["players"], g["name"].lower()))
            return [dict({k: v for k, v in g.items() if k not in ("address", "time")},
                         age=int(now - g["time"])) for g in games]


class Reports:
    """finished games' carnage reports, in SQLite"""

    def __init__(self, path: str):
        self.lock = threading.Lock()
        self.database = sqlite3.connect(path, check_same_thread=False)
        self.database.execute("""CREATE TABLE IF NOT EXISTS reports (
            id INTEGER PRIMARY KEY, time INTEGER NOT NULL, map TEXT NOT NULL, engine INTEGER NOT NULL,
            teams INTEGER NOT NULL, host TEXT NOT NULL, winner TEXT NOT NULL, player_count INTEGER NOT NULL,
            report TEXT NOT NULL)""")
        self.database.commit()

    def add(self, report: dict) -> int:
        with self.lock:
            cursor = self.database.execute(
                "INSERT INTO reports (time, map, engine, teams, host, winner, player_count, report) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                (report["time"], report["map"], report["engine"], report["teams"], report["host"], report["winner"],
                 len(report["players"]), json.dumps(report)))
            self.database.commit()
            return cursor.lastrowid

    def recent(self, limit: int):
        with self.lock:
            rows = self.database.execute(
                "SELECT id, time, map, engine, teams, host, winner, player_count FROM reports ORDER BY id DESC LIMIT ?",
                (limit,)).fetchall()
        return [dict(zip(("id", "time", "map", "engine", "teams", "host", "winner", "player_count"), row)) for row in rows]

    def history(self, limit: int):
        """the last games' full reports, newest first"""
        with self.lock:
            rows = self.database.execute("SELECT id, report FROM reports ORDER BY id DESC LIMIT ?", (limit,)).fetchall()
        return [dict(json.loads(row[1]), id=row[0]) for row in rows]

    def player(self, name: str, limit: int = 1000):
        """the games a player of that name played, newest first"""
        name = name.lower()
        return [report for report in self.history(5000)
                if any(player["name"].lower() == name for player in report["players"])][:limit]

    def get(self, report_id: int):
        with self.lock:
            row = self.database.execute("SELECT id, report FROM reports WHERE id = ?", (report_id,)).fetchone()
        if not row:
            return None
        return dict(json.loads(row[1]), id=row[0])


def clean_report(body: dict, game: dict) -> dict:
    """a carnage report as sent, checked field by field; the game's own
    listing gives what the report does not"""
    players = []
    raw_players = body.get("players")
    if not isinstance(raw_players, list) or not 1 <= len(raw_players) <= MAXIMUM_REPORT_PLAYERS:
        raise ValueError("players")
    for raw in raw_players:
        if not isinstance(raw, dict):
            raise ValueError("player")
        player = {"name": clean_text(str(raw.get("name", "")), 16) or "Player"}
        for key, low, high in (("team", -1, 15), ("place", 1, MAXIMUM_REPORT_PLAYERS), ("score", -32768, 32767),
                               ("kills", 0, 32767), ("assists", 0, 32767), ("deaths", 0, 32767),
                               ("betrayals", 0, 32767), ("suicides", 0, 32767), ("shots_fired", 0, 2 ** 31 - 1),
                               ("shots_hit", 0, 2 ** 31 - 1), ("multikills", 0, 32767)):
            player[key] = clean_integer(str(raw.get(key, 0)), low, high)
        player["shots_hit"] = min(player["shots_hit"], player["shots_fired"])
        # (newer games' fields)
        if "color" in raw:
            player["color"] = clean_integer(str(raw["color"]), -1, 31)
        for key in ("flag_grabs", "flag_returns", "flag_scores", "ball_time", "ball_carrier_kills", "hill_time", "laps"):
            if key in raw:
                player[key] = clean_integer(str(raw[key]), 0, 32767)
        medals = raw.get("medals")
        if medals is not None:
            if not isinstance(medals, dict) or len(medals) > 64:
                raise ValueError("medals")
            player["medals"] = {key: clean_integer(str(count), 1, 999) for key, count in medals.items()
                                if MEDAL_KEY.match(str(key)) and str(count) != "0"}
        players.append(player)
    teams = clean_integer(str(body.get("teams", game["teams"])), 0, 1)
    team_scores = body.get("team_scores", [])
    if not isinstance(team_scores, list) or len(team_scores) > 16:
        raise ValueError("team_scores")
    team_scores = [clean_integer(str(score), -32768, 32767) for score in team_scores]
    first = min(players, key=lambda p: p["place"])
    if teams and team_scores:
        best = max(range(len(team_scores)), key=lambda team: team_scores[team])
        winner = ["Red Team", "Blue Team"][best] if best < 2 else f"Team {best + 1}"
    else:
        winner = first["name"]
    return {
        "time": int(time.time()),
        "map": clean_text(str(body.get("map", game["map"])), 64) or game["map"],
        "engine": clean_integer(str(body.get("engine", game["engine"])), 0, 15),
        "teams": teams,
        "score_limit": clean_integer(str(body.get("score_limit", game["score_limit"])), 0, 32767),
        "duration": clean_integer(str(body.get("duration", 0)), 0, 24 * 3600),
        "team_scores": team_scores,
        "host": game["name"],
        "winner": winner,
        "players": players,
    }


REPORTS = None


GAMES = GameList()


class Handler(BaseHTTPRequestHandler):
    server_version = "halo-list/1"
    trusted_proxies = ("127.0.0.1", "::1")

    def client_address_text(self) -> str:
        address = self.client_address[0]
        forwarded = self.headers.get("X-Forwarded-For")
        if forwarded and address in self.trusted_proxies:
            # (the address the proxy saw: the last one it added)
            address = forwarded.split(",")[-1].strip()
        return address

    def reply(self, code: int, body: str, content_type: str = "text/plain; charset=utf-8"):
        data = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def file(self, path: Path, cache: str):
        try:
            data = path.read_bytes()
        except OSError:
            return self.reply(404, "not found\n")
        self.send_response(200)
        self.send_header("Content-Type", mimetypes.guess_type(path.name)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", cache)
        self.end_headers()
        self.wfile.write(data)

    def page(self, name: str):
        return self.file(WEB / name, "no-cache")

    def log_message(self, format, *args):
        pass

    def do_GET(self):
        if not GAMES.allow(self.client_address_text()):
            return self.reply(429, "slow down\n")
        GAMES.expire()
        if self.path == "/v1/games":
            return self.reply(200, json.dumps({"games": GAMES.snapshot()}), "application/json")
        if self.path == "/v1/games.txt":
            lines = ["\t".join(str(g[k]) for k in ("invite", "name", "map", "engine", "players",
                                                      "maximum_players", "open", "version", "age",
                                                      "score_limit", "teams"))
                     for g in GAMES.snapshot()]
            return self.reply(200, "".join(line + "\n" for line in lines))
        path = self.path.split("?")[0]
        if path in PAGES:
            return self.page(PAGES[path])
        if re.fullmatch(r"/games/\d+", path):
            return self.page("game.html")
        if re.fullmatch(r"/players/[^/]{1,64}", path):
            return self.page("player.html")
        match = STATIC.match(path)
        if match:
            return self.file(WEB / match.group(1), "no-cache")
        match = ART.match(path)
        if match:
            return self.file(WEB / "art" / match.group(1), "public, max-age=86400")
        match = re.fullmatch(r"/v1/history(?:\?limit=(\d+))?", self.path)
        if match:
            limit = min(1000, int(match.group(1) or 100))
            return self.reply(200, json.dumps({"reports": REPORTS.history(limit)}), "application/json")
        match = re.fullmatch(r"/v1/players/([^/?]{1,96})", self.path)
        if match:
            name = clean_text(unquote(match.group(1)), 16)
            return self.reply(200, json.dumps({"reports": REPORTS.player(name)}), "application/json")
        match = re.fullmatch(r"/v1/reports(?:\?limit=(\d+))?", self.path)
        if match:
            limit = min(50, int(match.group(1) or 10))
            return self.reply(200, json.dumps({"reports": REPORTS.recent(limit)}), "application/json")
        match = re.fullmatch(r"/v1/reports/(\d+)", self.path)
        if match:
            report = REPORTS.get(int(match.group(1)))
            if not report:
                return self.reply(404, "not found\n")
            return self.reply(200, json.dumps(report), "application/json")
        if self.path == "/v1/health":
            return self.reply(200, "ok\n")
        return self.reply(404, "not found\n")

    def do_POST(self):
        address = self.client_address_text()
        if not GAMES.allow(address):
            return self.reply(429, "slow down\n")
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return self.reply(400, "bad length\n")
        if self.path == "/v1/report":
            if length <= 0 or length > MAXIMUM_REPORT_BODY:
                return self.reply(400, "bad length\n")
            try:
                body = json.loads(self.rfile.read(length).decode("utf-8"))
                invite = str(body.get("invite", "")).lower()
            except (ValueError, AttributeError):
                return self.reply(400, "bad report\n")
            if not INVITE.match(invite):
                return self.reply(400, "bad invite\n")
            GAMES.expire()
            game = GAMES.listed_game(address, invite)
            if not game:
                return self.reply(403, "not a game this address lists, or it reported less than a minute ago\n")
            try:
                report = clean_report(body, game)
            except (ValueError, TypeError):
                return self.reply(400, "bad report\n")
            return self.reply(200, f"ok {REPORTS.add(report)}\n")
        if length <= 0 or length > MAXIMUM_BODY:
            return self.reply(400, "bad length\n")
        form = {k: v[0] for k, v in parse_qs(self.rfile.read(length).decode("utf-8", "replace")).items()}
        invite = form.get("invite", "").lower()
        if not INVITE.match(invite):
            return self.reply(400, "bad invite\n")
        GAMES.expire()
        if self.path == "/v1/withdraw":
            GAMES.withdraw(address, invite)
            return self.reply(200, "ok\n")
        if self.path != "/v1/announce":
            return self.reply(404, "not found\n")
        try:
            fields = {
                "invite": invite,
                "name": clean_text(form.get("name", ""), FIELDS["name"]) or "Halo",
                "map": clean_text(form.get("map", ""), FIELDS["map"]),
                "engine": clean_integer(form.get("engine", "0"), 0, 15),
                "players": clean_integer(form.get("players", "0"), 0, 255),
                "maximum_players": clean_integer(form.get("maximum_players", "0"), 0, 255),
                "open": clean_integer(form.get("open", "0"), 0, 1),
                "version": clean_integer(form.get("version", "0"), 0, 65535),
                "score_limit": clean_integer(form.get("score_limit", "0"), 0, 32767),
                "teams": clean_integer(form.get("teams", "0"), 0, 1),
            }
        except ValueError:
            return self.reply(400, "bad field\n")
        if not fields["map"]:
            return self.reply(400, "bad map\n")
        refused = GAMES.announce(address, fields)
        if refused:
            return self.reply(403, refused + "\n")
        return self.reply(200, f"ok {EXPIRY}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--address", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8390)
    parser.add_argument("--data", default=".", help="the folder of reports.db")
    parser.add_argument("--games-per-address", type=int, default=MAXIMUM_GAMES_PER_ADDRESS,
                        help="(more for testing on one machine)")
    args = parser.parse_args()
    GAMES.games_per_address = args.games_per_address
    global REPORTS
    REPORTS = Reports(f"{args.data.rstrip('/')}/reports.db")
    server = ThreadingHTTPServer((args.address, args.port), Handler)
    server.daemon_threads = True
    print(f"listening on {args.address}:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
