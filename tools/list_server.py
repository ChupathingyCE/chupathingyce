#!/usr/bin/env python3
"""The game list server (configure.py --game-browser).

A copy of the game that hosts a system link game announces it here with its
invite (port/linux/src/browser.c), and repeats the announcement while it
hosts; copies of the game that browse get the list and join a game through
its invite, as with an invite link. Nothing of the game itself goes through
this server: it only keeps the list.

    tools/list_server.py [--address 127.0.0.1] [--port 8390]

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
    GET  /              -> the list as a web page, each game with a Join
                        link (halo://join/..., which the game handles), and
                        the last games played
    POST /v1/report     a finished game's carnage report (JSON, below), from
                        the address that lists its invite -> 200 "ok <id>"
    GET  /v1/reports    -> 200 JSON {"reports": [...]}: the last games, newest
                        first (?limit=, at most 50)
    GET  /v1/reports/N  -> 200 JSON: game N's report
    GET  /games/N       -> game N's carnage report as a web page

A carnage report: {"invite", "map", "engine", "teams" (0 or 1),
"score_limit", "duration" (seconds), "team_scores" ([score of team 0,
...]), "players": [{"name", "team", "place" (1 first), "score", "kills",
"assists", "deaths", "betrayals", "suicides", "shots_fired", "shots_hit",
"multikills"}, ...]}. Only a game that ends (its host reaches the
postgame) sends one: a game that crashes or is quit is not recorded.
Reports are kept in reports.db in the --data folder, without addresses.

An announcement expires after EXPIRY seconds without another. Each address
may list MAXIMUM_GAMES_PER_ADDRESS games and send REQUESTS_PER_MINUTE
requests a minute. The addresses are kept only to apply those limits.
"""

import argparse
import json
import re
import sqlite3
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

EXPIRY = 75
MAXIMUM_GAMES = 512
MAXIMUM_GAMES_PER_ADDRESS = 4
REQUESTS_PER_MINUTE = 60
MAXIMUM_BODY = 2048
MAXIMUM_REPORT_BODY = 65536
MAXIMUM_REPORT_PLAYERS = 128
REPORT_INTERVAL = 60

INVITE = re.compile(r"^[0-9a-f]{44}$")
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


PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Halo Games</title>
<style>
:root { --bg: #0b1320; --panel: #111c2e; --line: #23406b; --text: #d8e4f5; --dim: #8aa0bf; --accent: #3d8bff; --open: #56c46b; --full: #c46b56; }
@media (prefers-color-scheme: light) { :root:not([data-theme="dark"]) { --bg: #eef2f8; --panel: #ffffff; --line: #c5d3e8; --text: #102038; --dim: #5a6f8d; --accent: #1f6fe5; --open: #23843a; --full: #a23c2a; } }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 960px; margin: 0 auto; padding: 24px 16px 48px; }
h1 { margin: 0 0 4px; font-size: 26px; letter-spacing: 0.02em; }
p.lead { margin: 0 0 20px; color: var(--dim); }
.games { display: grid; gap: 10px; }
.game { display: grid; grid-template-columns: 1fr auto; gap: 4px 16px; align-items: center; background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 12px 14px; }
.name { font-weight: 600; font-size: 17px; overflow-wrap: anywhere; }
.info { color: var(--dim); font-size: 14px; }
.status { font-size: 13px; font-weight: 600; }
.status.open { color: var(--open); } .status.closed { color: var(--full); }
a.join { grid-row: 1 / span 2; grid-column: 2; background: var(--accent); color: #fff; text-decoration: none; font-weight: 600; padding: 9px 18px; border-radius: 8px; }
a.join[aria-disabled="true"] { opacity: 0.4; pointer-events: none; }
.empty { color: var(--dim); padding: 24px 0; }
h2 { margin: 32px 0 10px; font-size: 19px; }
.recent { display: grid; gap: 6px; }
.recent a { display: grid; grid-template-columns: 1fr auto; gap: 2px 12px; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 9px 12px; color: var(--text); text-decoration: none; }
.recent a:hover { border-color: var(--accent); }
.recent .when { color: var(--dim); font-size: 13px; text-align: right; }
.winner { color: var(--open); font-weight: 600; }
.scroll { overflow-x: auto; }
table { width: 100%; border-collapse: collapse; background: var(--panel); border: 1px solid var(--line); border-radius: 10px; overflow: hidden; font-variant-numeric: tabular-nums; }
th, td { padding: 8px 10px; text-align: right; white-space: nowrap; }
th:nth-child(2), td:nth-child(2) { text-align: left; }
th { color: var(--dim); font-size: 12px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.04em; border-bottom: 1px solid var(--line); }
tr + tr td { border-top: 1px solid var(--line); }
tr.first td { font-weight: 700; }
.team { margin: 22px 0 8px; font-size: 17px; font-weight: 700; }
.team.red { color: #e0524f; } .team.blue { color: #4f8fe0; }
.back { color: var(--accent); text-decoration: none; font-size: 14px; }
footer { margin-top: 28px; color: var(--dim); font-size: 13px; }
code { font-size: 12px; }
</style>
</head>
<body>
<main>
<h1>Halo system link games</h1>
<p class="lead">Games hosted by players of Halo: Combat Evolved (the native ports). Join opens the game and joins through the game's invite.</p>
<div class="games" id="games"><div class="empty">Loading…</div></div>
<h2>Last 10 games</h2>
<div class="recent" id="recent"><div class="empty">Loading…</div></div>
<footer>The game must be installed for Join to open it (it registers <code>halo://</code> links). The list refreshes every 10 seconds; a game stays listed while its host runs.</footer>
</main>
<script>
const ENGINES = ["", "Capture the Flag", "Slayer", "Oddball", "King of the Hill", "Race"];
const MAPS = { beavercreek: "Battle Creek", bloodgulch: "Blood Gulch", boardingaction: "Boarding Action", carousel: "Derelict",
  chillout: "Chill Out", damnation: "Damnation", hangemhigh: "Hang 'Em High", longest: "Longest", prisoner: "Prisoner",
  putput: "Chiron TL-34", ratrace: "Rat Race", sidewinder: "Sidewinder", wizard: "Wizard" };
function mapName(path) {
  const base = path.split(/[\\\\/]/).pop();
  return MAPS[base] || base;
}
function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}
async function refresh() {
  const list = document.getElementById("games");
  try {
    const response = await fetch("/v1/games", { cache: "no-store" });
    const games = (await response.json()).games;
    list.replaceChildren();
    if (!games.length) { list.append(element("div", "empty", "No games are being hosted right now.")); return; }
    for (const game of games) {
      const card = element("div", "game");
      const engine = (game.teams ? "Team " : "") + (ENGINES[game.engine] || "Game");
      card.append(element("div", "name", game.name));
      const join = element("a", "join", "Join");
      join.href = "halo://join/" + game.invite;
      if (!game.open) join.setAttribute("aria-disabled", "true");
      card.append(join);
      const info = element("div", "info");
      info.append(mapName(game.map) + " · " + engine + (game.score_limit ? " to " + game.score_limit : "") +
        " · " + game.players + "/" + game.maximum_players + " players · ");
      info.append(element("span", "status " + (game.open ? "open" : "closed"), game.open ? "Accepting players" : "In progress"));
      card.append(info);
      list.append(card);
    }
  } catch (error) {
    list.replaceChildren(element("div", "empty", "Could not reach the list server."));
  }
}
function ago(seconds) {
  const age = Math.max(0, Math.floor(Date.now() / 1000) - seconds);
  if (age < 60) return "just now";
  if (age < 3600) return Math.floor(age / 60) + " min ago";
  if (age < 86400) return Math.floor(age / 3600) + " h ago";
  return new Date(seconds * 1000).toLocaleDateString();
}
async function refreshRecent() {
  const list = document.getElementById("recent");
  try {
    const reports = (await (await fetch("/v1/reports?limit=10", { cache: "no-store" })).json()).reports;
    list.replaceChildren();
    if (!reports.length) { list.append(element("div", "empty", "No finished games yet.")); return; }
    for (const report of reports) {
      const row = element("a");
      row.href = "/games/" + report.id;
      const title = element("div", "name");
      title.style.fontSize = "15px";
      title.append(mapName(report.map) + " · " + (report.teams ? "Team " : "") + (ENGINES[report.engine] || "Game"));
      row.append(title);
      row.append(element("div", "when", ago(report.time)));
      const detail = element("div", "info");
      detail.append(element("span", "winner", report.winner + " won"));
      detail.append(" · " + report.player_count + " player" + (report.player_count == 1 ? "" : "s") + " · hosted by " + report.host);
      row.append(detail);
      list.append(row);
    }
  } catch (error) {
    list.replaceChildren(element("div", "empty", "Could not reach the list server."));
  }
}
refresh();
refreshRecent();
setInterval(refresh, 10000);
setInterval(refreshRecent, 30000);
</script>
</body>
</html>
"""


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


def report_page() -> str:
    """the carnage report page: the list page's style, its own content"""
    style = PAGE[PAGE.index("<style>"):PAGE.index("</style>") + len("</style>")]
    return REPORT_PAGE.replace("<!--STYLE-->", style)


REPORT_PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Carnage Report</title>
<!--STYLE-->
</head>
<body>
<main>
<a class="back" href="/">← All games</a>
<h1 id="title" style="margin-top:10px">Carnage Report</h1>
<p class="lead" id="summary"></p>
<div id="report"><div class="empty">Loading…</div></div>
</main>
<script>
const ENGINES = ["", "Capture the Flag", "Slayer", "Oddball", "King of the Hill", "Race"];
const MAPS = { beavercreek: "Battle Creek", bloodgulch: "Blood Gulch", boardingaction: "Boarding Action", carousel: "Derelict",
  chillout: "Chill Out", damnation: "Damnation", hangemhigh: "Hang 'Em High", longest: "Longest", prisoner: "Prisoner",
  putput: "Chiron TL-34", ratrace: "Rat Race", sidewinder: "Sidewinder", wizard: "Wizard" };
const TEAMS = ["Red Team", "Blue Team"];
function mapName(path) { const base = path.split(/[\\\\/]/).pop(); return MAPS[base] || base; }
function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}
function duration(seconds) { return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0"); }
function accuracy(player) { return player.shots_fired ? Math.round(100 * player.shots_hit / player.shots_fired) + "%" : "–"; }
function ratio(player) { return (player.kills / Math.max(1, player.deaths)).toFixed(2); }
function scoreboard(players, shots) {
  const wrap = element("div", "scroll");
  const table = element("table");
  const head = element("tr");
  /* (accuracy only where the game counted shots) */
  const titles = ["#", "Player", "Score", "Kills", "Assists", "Deaths", "K/D", "Betrayals", "Suicides", "Accuracy", "Multikills"];
  for (const title of titles)
    if (shots || title !== "Accuracy") head.append(element("th", "", title));
  table.append(head);
  for (const player of players) {
    const row = element("tr", player.place === 1 ? "first" : "");
    const values = [player.place, player.name, player.score, player.kills, player.assists, player.deaths, ratio(player),
      player.betrayals, player.suicides, accuracy(player), player.multikills];
    values.forEach((value, index) => { if (shots || titles[index] !== "Accuracy") row.append(element("td", "", String(value))); });
    table.append(row);
  }
  wrap.append(table);
  return wrap;
}
async function load() {
  const id = location.pathname.split("/").pop();
  const holder = document.getElementById("report");
  try {
    const response = await fetch("/v1/reports/" + encodeURIComponent(id), { cache: "no-store" });
    if (!response.ok) throw new Error("missing");
    const report = await response.json();
    const engine = (report.teams ? "Team " : "") + (ENGINES[report.engine] || "Game");
    document.title = "Carnage Report · " + mapName(report.map);
    document.getElementById("title").textContent = mapName(report.map) + " · " + engine;
    document.getElementById("summary").textContent = new Date(report.time * 1000).toLocaleString() +
      " · " + duration(report.duration) + (report.score_limit ? " · to " + report.score_limit : "") +
      " · hosted by " + report.host;
    holder.replaceChildren();
    const winner = element("div", "team");
    winner.append(element("span", "winner", report.winner + " won"));
    holder.append(winner);
    const players = report.players.slice().sort((a, b) => a.place - b.place);
    const shots = players.some(p => p.shots_fired > 0);
    if (report.teams) {
      const order = [...new Set(players.map(p => p.team))].sort((a, b) => (report.team_scores[b] || 0) - (report.team_scores[a] || 0));
      for (const team of order) {
        const heading = element("div", "team " + (team === 0 ? "red" : team === 1 ? "blue" : ""),
          (TEAMS[team] || "Team " + (team + 1)) + " · " + (report.team_scores[team] || 0));
        holder.append(heading, scoreboard(players.filter(p => p.team === team), shots));
      }
    } else {
      holder.append(scoreboard(players, shots));
    }
  } catch (error) {
    holder.replaceChildren(element("div", "empty", "There is no such game."));
  }
}
load();
</script>
</body>
</html>
"""


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
        if self.path in ("/", "/index.html"):
            return self.reply(200, PAGE, "text/html; charset=utf-8")
        match = re.fullmatch(r"/games/(\d+)", self.path)
        if match:
            return self.reply(200, report_page(), "text/html; charset=utf-8")
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
