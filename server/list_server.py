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
                        of that name (any case) played, newest first; with
                        a player ID (32 hexadecimal digits), the games that
                        player confirmed
    POST /v1/claim      JSON {"invite", "name", "key"}: a player confirms
                        their line in a game that just ended -> 200 "ok
                        <player ID>", 404 while the game has no report yet,
                        403 if the line is not theirs to confirm

Confirmed players: a game's host tags each line of its report with a hash
of the invite and the address it had that player at (port/linux/src/
browser.c). The tags are kept apart from the report, never shown, and
dropped after CLAIM_WINDOW. A player's copy of the game then sends its
player key (32 random bytes, over HTTPS) with the invite and its name; the
line is confirmed if a tag matches the address the request comes from, the
name matches, and the line is not confirmed yet. Its player ID, the first
16 bytes of SHA-256("halo-ce-universal player id\n" + key), is the line's
from then on. No key is kept: the ID is worked out from it each time.

Profiles (/profile; JSON requests from the page carry "X-Halo: 1", which a
form or a page of another site cannot send, and the session cookie is
HttpOnly, Secure and SameSite=Strict):

    POST /v1/link       JSON {"key"} from the game (MY PROFILE) -> "ok <code>":
                        a sign-in code good once, for LINK_LIFETIME
    POST /v1/session/link    {"code"} -> signed in as the key's player;
                        JSON {"player_id", "profile", "key"}: the key comes
                        back to the page alone, for it to back it up
    POST /v1/session/login   {"handle", "auth"} -> signed in; JSON
                        {"player_id", "profile"} with the profile's backup
    GET  /v1/session    -> the signed-in player, or 401
    POST /v1/session/logout
    POST /v1/profile    {"handle", "display", "bio", "auth", "backup"}: made
                        (by a session from the game's link, which proved the
                        key), or "display" and "bio" changed (any session),
                        or "auth" and "backup" replaced (a linked session)
    GET  /v1/profiles/ID -> a player's public profile {"handle", "display", "bio"}

The password never reaches the server: the page stretches it (PBKDF2,
600000 rounds, salted with the profile name) into a sign-in secret, "auth",
of which the server keeps a scrypt hash, and an encryption key that stays
in the page, with which the page encrypts the player key (AES-256-GCM):
"backup", {"iv", "data"}, is all the server keeps of it. So the server, or
anyone who takes its database, cannot read a player's key; signing in
returns the backup, for the page to decrypt and hand to the game
(halo://key/...). A link's key is held in memory only, until its code is
used or lapses.

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
import hashlib
import hmac
import ipaddress
import mimetypes
import json
import re
import secrets
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
# a finished game's lines may be confirmed for this long after its report
CLAIM_WINDOW = 600
PLAYER_KEY = re.compile(r"^[0-9a-f]{64}$")
PLAYER_ID = re.compile(r"^[0-9a-f]{32}$")
TAG = re.compile(r"^[0-9a-f]{64}$")
# profiles: a sign-in link from the game is good this long; a session this long
LINK_LIFETIME = 300
SESSION_LIFETIME = 30 * 24 * 3600
HANDLE = re.compile(r"^[a-z0-9_-]{3,20}$")
HEX = re.compile(r"^[0-9a-f]+$")
# failed sign-ins allowed an hour, for a profile name and for an address
SIGN_IN_FAILURES_PER_HANDLE = 10
SIGN_IN_FAILURES_PER_ADDRESS = 30

# (64 digits since network version 8; 44 before, which older builds still list)
MEDAL_KEY = re.compile(r"^[a-z][a-z0-9_]{0,31}$")
PAGES = {"/": "index.html", "/index.html": "index.html", "/leaders": "leaders.html", "/medals": "medals.html",
         "/profile": "profile.html"}
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
        self.database.execute("""CREATE TABLE IF NOT EXISTS claims (
            report INTEGER NOT NULL, line INTEGER NOT NULL, invite TEXT NOT NULL, name TEXT NOT NULL,
            tag TEXT NOT NULL, expires INTEGER NOT NULL)""")
        self.database.execute("CREATE INDEX IF NOT EXISTS claims_invite ON claims (invite)")
        self.database.execute("""CREATE TABLE IF NOT EXISTS profiles (
            player_id TEXT PRIMARY KEY, handle TEXT NOT NULL UNIQUE, display TEXT NOT NULL, bio TEXT NOT NULL,
            auth TEXT NOT NULL, backup TEXT NOT NULL, created INTEGER NOT NULL, updated INTEGER NOT NULL)""")
        self.database.execute("""CREATE TABLE IF NOT EXISTS sessions (
            token TEXT PRIMARY KEY, player_id TEXT NOT NULL, linked INTEGER NOT NULL, expires INTEGER NOT NULL)""")
        self.database.commit()

    def add(self, report: dict, invite: str = "", tags=()) -> int:
        with self.lock:
            cursor = self.database.execute(
                "INSERT INTO reports (time, map, engine, teams, host, winner, player_count, report) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                (report["time"], report["map"], report["engine"], report["teams"], report["host"], report["winner"],
                 len(report["players"]), json.dumps(report)))
            report_id = cursor.lastrowid
            # (the lines' tags, apart from the report: who may confirm them)
            expires = int(time.time()) + CLAIM_WINDOW
            for line, tag in enumerate(tags):
                if tag:
                    self.database.execute("INSERT INTO claims (report, line, invite, name, tag, expires) VALUES (?, ?, ?, ?, ?, ?)",
                                          (report_id, line, invite, report["players"][line]["name"], tag, expires))
            self.database.commit()
            return report_id

    def claim(self, invite: str, name: str, tag: str, player_id: str) -> str:
        """a line of a game of that invite confirmed as player_id: "ok", or
        "none" (the game has no report yet), or "refused" """
        now = int(time.time())
        with self.lock:
            self.database.execute("DELETE FROM claims WHERE expires < ?", (now,))
            rows = self.database.execute("SELECT rowid, report, line, name, tag FROM claims WHERE invite = ?",
                                         (invite,)).fetchall()
            if not rows:
                # (no report of that game, or none left to confirm: the
                # player's copy asks again a few times, then stops)
                self.database.commit()
                return "none"
            match = None
            for rowid, report_id, line, line_name, line_tag in rows:
                # (every row compared, each in constant time)
                if hmac.compare_digest(line_tag, tag) and line_name == name and match is None:
                    match = (rowid, report_id, line)
            if not match:
                self.database.commit()
                return "refused"
            rowid, report_id, line = match
            row = self.database.execute("SELECT report FROM reports WHERE id = ?", (report_id,)).fetchone()
            report = json.loads(row[0])
            report["players"][line]["player_id"] = player_id
            self.database.execute("UPDATE reports SET report = ? WHERE id = ?", (json.dumps(report), report_id))
            self.database.execute("DELETE FROM claims WHERE rowid = ?", (rowid,))
            self.database.commit()
            return "ok"

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

    def player(self, who: str, limit: int = 1000):
        """the games a confirmed player (by ID) or a name played, newest first"""
        if PLAYER_ID.match(who):
            return [report for report in self.history(5000)
                    if any(player.get("player_id") == who for player in report["players"])][:limit]
        name = who.lower()
        return [report for report in self.history(5000)
                if any(player["name"].lower() == name for player in report["players"])][:limit]

    def get(self, report_id: int):
        with self.lock:
            row = self.database.execute("SELECT id, report FROM reports WHERE id = ?", (report_id,)).fetchone()
        if not row:
            return None
        return dict(json.loads(row[1]), id=row[0])


def hash_secret(secret: bytes) -> str:
    """a sign-in secret's hash: scrypt where Python has it, else PBKDF2"""
    salt = secrets.token_bytes(16)
    if hasattr(hashlib, "scrypt"):
        digest = hashlib.scrypt(secret, salt=salt, n=2 ** 15, r=8, p=1, maxmem=64 * 1024 * 1024, dklen=32)
        return f"scrypt$15$8$1${salt.hex()}${digest.hex()}"
    digest = hashlib.pbkdf2_hmac("sha256", secret, salt, 600000)
    return f"pbkdf2$600000${salt.hex()}${digest.hex()}"


def secret_matches(secret: bytes, stored: str) -> bool:
    parts = stored.split("$")
    if parts[0] == "scrypt" and hasattr(hashlib, "scrypt"):
        n, r, p, salt, digest = int(parts[1]), int(parts[2]), int(parts[3]), parts[4], parts[5]
        computed = hashlib.scrypt(secret, salt=bytes.fromhex(salt), n=2 ** n, r=r, p=p,
                                  maxmem=64 * 1024 * 1024, dklen=32)
    elif parts[0] == "pbkdf2":
        computed = hashlib.pbkdf2_hmac("sha256", secret, bytes.fromhex(parts[2]), int(parts[1]))
        digest = parts[3]
    else:
        return False
    return hmac.compare_digest(computed.hex(), digest)


def player_id_of(key_hex: str) -> str:
    return hashlib.sha256(b"halo-ce-universal player id\n" + bytes.fromhex(key_hex)).hexdigest()[:32]


class Profiles:
    """profiles, sessions and the game's sign-in links (beside the reports)"""

    def __init__(self, reports):
        self.lock = reports.lock
        self.database = reports.database
        self.links = {}  # sha256(code) -> (player_id, key, expires): memory only
        self.failures = {}  # handle or address -> [times]

    def add_link(self, key_hex: str) -> str:
        code = secrets.token_hex(32)
        now = time.time()
        with self.lock:
            for digest in [d for d, link in self.links.items() if link[2] < now]:
                del self.links[digest]
            if len(self.links) > 1000:
                raise ValueError("too many links")
            self.links[hashlib.sha256(code.encode()).hexdigest()] = (player_id_of(key_hex), key_hex, now + LINK_LIFETIME)
        return code

    def use_link(self, code: str):
        """the link's (player_id, key), once"""
        with self.lock:
            link = self.links.pop(hashlib.sha256(code.encode()).hexdigest(), None)
        if not link or link[2] < time.time():
            return None
        return link[0], link[1]

    def new_session(self, player_id: str, linked: bool) -> str:
        token = secrets.token_hex(32)
        now = int(time.time())
        with self.lock:
            self.database.execute("DELETE FROM sessions WHERE expires < ?", (now,))
            self.database.execute("INSERT INTO sessions (token, player_id, linked, expires) VALUES (?, ?, ?, ?)",
                                  (hashlib.sha256(token.encode()).hexdigest(), player_id, int(linked),
                                   now + SESSION_LIFETIME))
            self.database.commit()
        return token

    def session(self, token: str):
        """(player_id, linked) of a session, or None"""
        if not token or len(token) != 64 or not HEX.match(token):
            return None
        with self.lock:
            row = self.database.execute("SELECT player_id, linked, expires FROM sessions WHERE token = ?",
                                        (hashlib.sha256(token.encode()).hexdigest(),)).fetchone()
        if not row or row[2] < time.time():
            return None
        return row[0], bool(row[1])

    def end_session(self, token: str):
        with self.lock:
            self.database.execute("DELETE FROM sessions WHERE token = ?", (hashlib.sha256(token.encode()).hexdigest(),))
            self.database.commit()

    def get(self, player_id: str, private: bool = False):
        with self.lock:
            row = self.database.execute(
                "SELECT handle, display, bio, backup, created FROM profiles WHERE player_id = ?", (player_id,)).fetchone()
        if not row:
            return None
        profile = {"handle": row[0], "display": row[1], "bio": row[2], "created": row[4]}
        if private:
            profile["backup"] = json.loads(row[3])
        return profile

    def by_handle(self, handle: str):
        """(player_id, auth hash) of a profile name"""
        with self.lock:
            return self.database.execute("SELECT player_id, auth FROM profiles WHERE handle = ?", (handle,)).fetchone()

    def create(self, player_id: str, handle: str, display: str, bio: str, auth: bytes, backup: dict) -> str:
        now = int(time.time())
        with self.lock:
            if self.database.execute("SELECT 1 FROM profiles WHERE player_id = ?", (player_id,)).fetchone():
                return "this player already has a profile"
            if self.database.execute("SELECT 1 FROM profiles WHERE handle = ?", (handle,)).fetchone():
                return "that profile name is taken"
        stored = hash_secret(auth)
        with self.lock:
            try:
                self.database.execute(
                    "INSERT INTO profiles (player_id, handle, display, bio, auth, backup, created, updated) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                    (player_id, handle, display, bio, stored, json.dumps(backup), now, now))
                self.database.commit()
            except sqlite3.IntegrityError:
                return "that profile name is taken"
        return ""

    def update(self, player_id: str, display: str, bio: str, auth=None, backup=None):
        stored = hash_secret(auth) if auth is not None else None
        with self.lock:
            self.database.execute("UPDATE profiles SET display = ?, bio = ?, updated = ? WHERE player_id = ?",
                                  (display, bio, int(time.time()), player_id))
            if stored is not None and backup is not None:
                self.database.execute("UPDATE profiles SET auth = ?, backup = ? WHERE player_id = ?",
                                      (stored, json.dumps(backup), player_id))
            self.database.commit()

    def allow_sign_in(self, handle: str, address: str) -> bool:
        now = time.time()
        with self.lock:
            for key in ("handle:" + handle, "address:" + address):
                self.failures[key] = [t for t in self.failures.get(key, []) if now - t < 3600]
            return (len(self.failures["handle:" + handle]) < SIGN_IN_FAILURES_PER_HANDLE and
                    len(self.failures["address:" + address]) < SIGN_IN_FAILURES_PER_ADDRESS)

    def failed_sign_in(self, handle: str, address: str):
        with self.lock:
            for key in ("handle:" + handle, "address:" + address):
                self.failures.setdefault(key, []).append(time.time())


def clean_backup(raw) -> dict:
    """an encrypted key as the page sends it: AES-GCM's 12-byte IV, and the
    32-byte key with its 16-byte tag"""
    if not isinstance(raw, dict):
        raise ValueError("backup")
    iv, data = str(raw.get("iv", "")), str(raw.get("data", ""))
    if len(iv) != 24 or len(data) != 96 or not HEX.match(iv) or not HEX.match(data):
        raise ValueError("backup")
    return {"iv": iv, "data": data}


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
        tag = str(raw.get("tag", "")).lower()
        player["_tag"] = tag if TAG.match(tag) else ""
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
PROFILES = None


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

    def reply_json(self, code: int, body, cookie: str = None):
        data = json.dumps(body).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        if cookie is not None:
            # (an empty one ends the session)
            age = SESSION_LIFETIME if cookie else 0
            self.send_header("Set-Cookie", f"halo_session={cookie}; Path=/; Max-Age={age}; HttpOnly; Secure; SameSite=Strict")
        self.end_headers()
        self.wfile.write(data)

    def session_token(self) -> str:
        for part in self.headers.get("Cookie", "").split(";"):
            name, _, value = part.strip().partition("=")
            if name == "halo_session":
                return value
        return ""

    def page(self, name: str):
        return self.file(WEB / name, "no-cache")

    def log_message(self, format, *args):
        pass

    def profile_request(self, address: str, length: int):
        """the profile requests (see the top of this file)"""
        if length <= 0 or length > MAXIMUM_BODY:
            return self.reply(400, "bad length\n")
        try:
            body = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(body, dict):
                raise ValueError("body")
        except ValueError:
            return self.reply(400, "bad request\n")
        # the game's request for a sign-in link
        if self.path == "/v1/link":
            key = str(body.get("key", "")).lower()
            if not PLAYER_KEY.match(key):
                return self.reply(400, "bad key\n")
            try:
                return self.reply(200, f"ok {PROFILES.add_link(key)}\n")
            except ValueError:
                return self.reply(503, "busy\n")
        # the page's own (a form or another site's page cannot add the header)
        if self.headers.get("X-Halo") != "1":
            return self.reply(403, "not from the page\n")
        token = self.session_token()
        session = PROFILES.session(token)
        if self.path == "/v1/session/link":
            code = str(body.get("code", "")).lower()
            link = PROFILES.use_link(code) if len(code) == 64 and HEX.match(code) else None
            if not link:
                return self.reply_json(403, {"error": "That link was used already, or has lapsed. Press Y in the game's game list for another."})
            player_id, key = link
            return self.reply_json(200, {"player_id": player_id, "key": key,
                                         "profile": PROFILES.get(player_id, private=True)},
                                   PROFILES.new_session(player_id, True))
        if self.path == "/v1/session/login":
            handle = str(body.get("handle", "")).lower()
            auth = str(body.get("auth", "")).lower()
            if not HANDLE.match(handle) or len(auth) != 64 or not HEX.match(auth):
                return self.reply_json(400, {"error": "Enter your profile name and password."})
            if not PROFILES.allow_sign_in(handle, address):
                return self.reply_json(429, {"error": "Too many tries. Try again in an hour."})
            row = PROFILES.by_handle(handle)
            if not row or not secret_matches(bytes.fromhex(auth), row[1]):
                PROFILES.failed_sign_in(handle, address)
                return self.reply_json(403, {"error": "That profile name and password do not match."})
            return self.reply_json(200, {"player_id": row[0], "profile": PROFILES.get(row[0], private=True)},
                                   PROFILES.new_session(row[0], False))
        if self.path == "/v1/session/logout":
            if token:
                PROFILES.end_session(token)
            return self.reply_json(200, {"ok": True}, "")
        if not session:
            return self.reply_json(401, {"error": "Sign in first."})
        player_id, linked = session
        display = clean_text(str(body.get("display", "")), 24)
        bio = clean_text(str(body.get("bio", "")), 280)
        existing = PROFILES.get(player_id)
        try:
            auth = body.get("auth")
            auth = bytes.fromhex(str(auth).lower()) if auth is not None else None
            if auth is not None and len(auth) != 32:
                raise ValueError("auth")
            backup = clean_backup(body["backup"]) if body.get("backup") is not None else None
        except (ValueError, TypeError):
            return self.reply_json(400, {"error": "Bad request."})
        if not existing:
            handle = str(body.get("handle", "")).lower()
            if not linked:
                return self.reply_json(403, {"error": "Open your profile from the game to make it."})
            if not HANDLE.match(handle):
                return self.reply_json(400, {"error": "A profile name is 3 to 20 letters, digits, - or _."})
            if auth is None or backup is None:
                return self.reply_json(400, {"error": "Choose a password."})
            refused = PROFILES.create(player_id, handle, display or handle, bio, auth, backup)
            if refused:
                return self.reply_json(409, {"error": refused[0].upper() + refused[1:] + "."})
        else:
            if (auth is None) != (backup is None) or (auth is not None and not linked):
                return self.reply_json(403, {"error": "Open your profile from the game to change the password."})
            PROFILES.update(player_id, display or existing["handle"], bio, auth, backup)
        return self.reply_json(200, {"player_id": player_id, "profile": PROFILES.get(player_id, private=True)})

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
            who = unquote(match.group(1))
            # (a player ID as it is; a name as the reports keep names)
            who = who.lower() if PLAYER_ID.match(who.lower()) else clean_text(who, 16)
            answer = {"reports": REPORTS.player(who)}
            if PLAYER_ID.match(who):
                answer["profile"] = PROFILES.get(who)
            return self.reply(200, json.dumps(answer), "application/json")
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
        if path == "/v1/session":
            session = PROFILES.session(self.session_token())
            if not session:
                return self.reply_json(401, {"error": "not signed in"})
            return self.reply_json(200, {"player_id": session[0], "linked": session[1],
                                         "profile": PROFILES.get(session[0], private=True)})
        match = re.fullmatch(r"/v1/profiles/([0-9a-f]{32})", path)
        if match:
            profile = PROFILES.get(match.group(1))
            return self.reply_json(200 if profile else 404, profile or {"error": "no profile"})
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
        if self.path == "/v1/link" or self.path.startswith("/v1/session/") or self.path == "/v1/profile":
            return self.profile_request(address, length)
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
            except (ValueError, TypeError) as problem:
                return self.reply(400, f"bad report ({problem})\n")
            tags = [player.pop("_tag", "") for player in report["players"]]
            return self.reply(200, f"ok {REPORTS.add(report, invite, tags)}\n")
        if self.path == "/v1/claim":
            if length <= 0 or length > MAXIMUM_BODY:
                return self.reply(400, "bad length\n")
            try:
                body = json.loads(self.rfile.read(length).decode("utf-8"))
                invite = str(body["invite"]).lower()
                key = str(body["key"]).lower()
                name = clean_text(str(body["name"]), 16)
            except (ValueError, KeyError, TypeError, AttributeError):
                return self.reply(400, "bad claim\n")
            if not INVITE.match(invite) or not PLAYER_KEY.match(key) or not name:
                return self.reply(400, "bad claim\n")
            try:
                source = ipaddress.ip_address(address)
            except ValueError:
                return self.reply(400, "bad address\n")
            if source.version != 4:
                return self.reply(403, "claims come over IPv4\n")
            tag = hashlib.sha256(f"halo-ce-universal address\n{invite}\n{source}".encode()).hexdigest()
            player_id = hashlib.sha256(b"halo-ce-universal player id\n" + bytes.fromhex(key)).hexdigest()[:32]
            result = REPORTS.claim(invite, name, tag, player_id)
            if result == "ok":
                return self.reply(200, f"ok {player_id}\n")
            if result == "none":
                return self.reply(404, "no report of that game yet\n")
            return self.reply(403, "not a line of yours to confirm\n")
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
    global PROFILES
    PROFILES = Profiles(REPORTS)
    server = ThreadingHTTPServer((args.address, args.port), Handler)
    server.daemon_threads = True
    print(f"listening on {args.address}:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
