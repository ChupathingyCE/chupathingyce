#!/usr/bin/env python3
"""The game list server of the new networking (configure.py --new-networking).

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
                        link (halo://join/..., which the game handles)

An announcement expires after EXPIRY seconds without another. Each address
may list MAXIMUM_GAMES_PER_ADDRESS games and send REQUESTS_PER_MINUTE
requests a minute. The addresses are kept only to apply those limits.
"""

import argparse
import json
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

EXPIRY = 75
MAXIMUM_GAMES = 512
MAXIMUM_GAMES_PER_ADDRESS = 4
REQUESTS_PER_MINUTE = 60
MAXIMUM_BODY = 2048

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
footer { margin-top: 28px; color: var(--dim); font-size: 13px; }
code { font-size: 12px; }
</style>
</head>
<body>
<main>
<h1>Halo system link games</h1>
<p class="lead">Games hosted by players of Halo: Combat Evolved (the native ports). Join opens the game and joins through the game's invite.</p>
<div class="games" id="games"><div class="empty">Loading…</div></div>
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
refresh();
setInterval(refresh, 10000);
</script>
</body>
</html>
"""


class GameList:
    def __init__(self):
        self.lock = threading.Lock()
        self.games = {}  # invite -> record
        self.requests = {}  # address -> [times]

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
                if sum(1 for g in self.games.values() if g["address"] == address) >= MAXIMUM_GAMES_PER_ADDRESS:
                    return "too many games from this address"
                if len(self.games) >= MAXIMUM_GAMES:
                    return "the list is full"
            self.games[invite] = record
        return ""

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
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.address, args.port), Handler)
    server.daemon_threads = True
    print(f"listening on {args.address}:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
