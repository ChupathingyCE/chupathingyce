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
