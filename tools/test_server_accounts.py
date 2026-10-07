"""Delta Control's control panel whole (server/docs/moderation.md): the
console, control API and web page of server_control.c in its harness
(server/tests/control_harness.c), driven over real sockets on the loopback
address: the first owner's setup code, accounts with passwords and a TOTP
second factor, the backoff on wrong passwords, invitations and resets, each
role's permissions, CSRF, the owner requiring a second factor, binding an
account to a game, a file saved whole, the audit file, and the panel over
HTTPS (its certificate's fingerprint pinned) beyond the loopback address."""
import base64
import hashlib
import hmac
import json
import re
import socket
import ssl
import struct
import time

import pytest

from tools.test_server_control import Harness, Response, harness_binary, http  # noqa: F401

PASSWORD = "correct horse battery staple"


def totp(secret_base32, at=None, step_offset=0):
    """RFC 6238's code (SHA-1, 30 seconds, 6 digits) for a base32 secret."""
    secret = base64.b32decode(secret_base32 + "=" * (-len(secret_base32) % 8))
    step = int((at or time.time()) // 30) + step_offset
    digest = hmac.new(secret, struct.pack(">Q", step), hashlib.sha1).digest()
    offset = digest[19] & 15
    number = struct.unpack(">I", digest[offset:offset + 4])[0] & 0x7FFFFFFF
    return f"{number % 1000000:06d}"


class Browser:
    """A browser's session with the panel: its cookie and CSRF token."""

    def __init__(self, harness):
        self.harness = harness
        self.cookie = None
        self.csrf = None
        self.session = None

    def headers(self):
        headers = {"Origin": f"http://127.0.0.1:{self.harness.port}"}
        if self.cookie:
            headers["Cookie"] = f"chce_session={self.cookie}"
        if self.csrf:
            headers["X-CSRF-Token"] = self.csrf
        return headers

    def login(self, user, password=PASSWORD, code=None):
        body = {"user": user, "password": password}
        if code:
            body["code"] = code
        response = http(self.harness, "POST", "/v1/login", self.headers(), body)
        if response.status == 200:
            self.cookie = response.session
            self.session = response.json()
            self.csrf = self.session["csrf"]
        return response

    def get(self, path):
        return http(self.harness, "GET", path, self.headers())

    def post(self, path, body=None):
        return http(self.harness, "POST", path, self.headers(), body if body is not None else {})

    def command(self, line):
        return self.post("/v1/command", {"command": line})


def setup_code(harness):
    line = harness.wait_for("#setup=set_")
    return re.search(r"#setup=(set_[0-9a-f]{32})", line).group(1)


def invite_code(text):
    return re.search(r"(inv_[0-9a-f]{32})", text).group(1)


@pytest.fixture
def panel(harness_binary, tmp_path):  # noqa: F811
    running = Harness(harness_binary, tmp_path)
    yield running
    running.stop()


def make_owner(panel, name="owner"):
    """The first owner's account, from the setup code; logged in."""
    response = http(panel, "POST", "/v1/setup", {"Origin": f"http://127.0.0.1:{panel.port}"},
                    {"code": setup_code(panel), "user": name, "password": PASSWORD})
    assert response.status == 200, response.body
    browser = Browser(panel)
    assert browser.login(name).status == 200
    return browser


def invite(owner, role, name):
    """An account of a role, from an invitation the owner made; logged in."""
    response = owner.post("/v1/accounts/invite", {"role": role})
    assert response.status == 200, response.body
    code = response.json()["code"]
    response = http(owner.harness, "POST", "/v1/invite", {"Origin": f"http://127.0.0.1:{owner.harness.port}"},
                    {"code": code, "user": name, "password": PASSWORD})
    assert response.status == 200, response.body
    browser = Browser(owner.harness)
    assert browser.login(name).status == 200
    return browser


def test_setup_and_accounts(panel, tmp_path):
    """No owner until the setup code makes one; the code is good once, and
    never in the log; the accounts file keeps hashes and is private."""
    hello = http(panel, "GET", "/v1/hello")
    assert hello.status == 200 and hello.json() == {"setup": True, "tls": False, "fingerprint": None}
    code = setup_code(panel)
    assert not any(code in line for line in panel.lines if line.startswith("notice:"))
    origin = {"Origin": f"http://127.0.0.1:{panel.port}"}
    # (a wrong code, a weak password, a bad name: refused)
    assert http(panel, "POST", "/v1/setup", origin, {"code": "set_" + "0" * 32, "user": "a",
                                                     "password": PASSWORD}).status == 403
    assert http(panel, "POST", "/v1/setup", origin, {"code": code, "user": "owner", "password": "short"}).status == 400
    assert http(panel, "POST", "/v1/setup", origin, {"code": code, "user": "Bad Name", "password": PASSWORD}).status == 400
    # (from another site: refused)
    assert http(panel, "POST", "/v1/setup", {"Origin": "http://evil.example"},
                {"code": code, "user": "owner", "password": PASSWORD}).status == 403
    assert http(panel, "POST", "/v1/setup", origin, {"code": code, "user": "owner", "password": PASSWORD}).status == 200
    assert http(panel, "POST", "/v1/setup", origin, {"code": code, "user": "two", "password": PASSWORD}).status == 403
    assert http(panel, "GET", "/v1/hello").json()["setup"] is False
    accounts = (tmp_path / "control_accounts.txt").read_text()
    assert (tmp_path / "control_accounts.txt").stat().st_mode & 0o777 == 0o600
    assert re.search(r"^v1 account owner [0-9a-f]{8} owner ", accounts, re.M) and PASSWORD not in accounts
    owner = Browser(panel)
    assert owner.login("owner", "wrong password here").status == 401
    assert owner.login("nobody").status == 401
    assert owner.login("owner").status == 200
    assert owner.session["role"] == "owner" and owner.session["kind"] == "account" and not owner.session["totp"]
    session = owner.get("/v1/session").json()
    assert session["name"] == "owner" and session["permissions"] & 0x100
    audit = (tmp_path / "control_audit.log").read_text().splitlines()
    events = [json.loads(line) for line in audit]
    assert any(event["action"] == "setup" and event["ok"] for event in events)
    assert any(event["action"] == "login" and not event["ok"] for event in events)
    assert not any("127.0.0.1" in line for line in audit)


def test_second_factor(panel):
    """TOTP: begun, enabled with a code, then asked for at every login; a
    code is good once; disabling it needs the password and a code."""
    owner = make_owner(panel)
    begun = owner.post("/v1/account/totp/begin").json()
    secret = begun["secret"]
    assert re.fullmatch(r"[A-Z2-7]{32}", secret) and begun["uri"].startswith("otpauth://totp/")
    assert isinstance(begun["qr"], list) and set("".join(begun["qr"])) <= {"0", "1"}
    assert owner.post("/v1/account/totp/enable", {"code": "000000" if totp(secret) != "000000" else "111111"}).status \
        == 400
    code = totp(secret)
    assert owner.post("/v1/account/totp/enable", {"code": code}).status == 200
    browser = Browser(panel)
    response = browser.login("owner")
    assert response.status == 401 and response.json().get("need_code") is True
    # (the code used to enable it is spent; the next step's is good)
    assert browser.login("owner", code=code).status == 401
    assert browser.login("owner", code=totp(secret, step_offset=1)).status == 200
    assert browser.session["totp"] is True
    assert browser.post("/v1/account/totp/disable", {"password": "wrong password!!", "code": totp(secret)}).status == 403


def test_backoff_and_address_limits(panel):
    """Five wrong passwords lock the account a minute (whoever tries), and
    the address that tried; the server says how long."""
    make_owner(panel)
    browser = Browser(panel)
    for attempt in range(5):
        assert browser.login("owner", f"wrong password {attempt}").status == 401
    locked = browser.login("owner")
    assert locked.status == 429 and int(locked.headers["retry-after"]) > 0


def test_roles_and_permissions(panel, tmp_path):
    """Each role's powers, checked by the panel and again by the main
    thread; admins invite moderators only; the only owner stays one."""
    owner = make_owner(panel)
    admin = invite(owner, "admin", "ada")
    moderator = invite(owner, "moderator", "mo")
    assert moderator.session["role"] == "moderator"
    # a moderator: the players and kicks, timed bans up to 7 days; no more
    assert moderator.get("/v1/players").status == 200
    assert moderator.command("sv_kick 1").json()["ok"] is True
    assert moderator.command("sv_ban 1 2h").json()["ok"] is True
    for line in ("sv_ban 1 30d", "sv_ban 1", "sv_unban 1", "sv_map bloodgulch ctf", "sv_name x", "sv_mod_add 1 owner",
                 "sv_admin_add x"):
        assert moderator.command(line).status == 403, line
    assert moderator.get("/v1/accounts").status == 403
    assert moderator.post("/v1/accounts/invite", {"role": "moderator"}).status == 403
    # an admin: maps, settings, bans of any length, invitations for
    # moderators; not roles
    for line in ("sv_ban 1", "sv_unban 1", "sv_map bloodgulch ctf", "sv_name x"):
        assert admin.command(line).json()["ok"] is True, line
    assert admin.command("sv_mod_add 1 moderator").status == 403
    assert admin.post("/v1/accounts/invite", {"role": "moderator"}).status == 200
    assert admin.post("/v1/accounts/invite", {"role": "admin"}).status == 403
    assert admin.post("/v1/accounts/role", {"user": "mo", "role": "admin"}).status == 403
    # the owner: roles
    assert owner.post("/v1/accounts/role", {"user": "mo", "role": "admin"}).status == 200
    assert moderator.get("/v1/session").json()["role"] == "admin"
    assert owner.post("/v1/accounts/role", {"user": "owner", "role": "admin"}).status == 409
    assert owner.post("/v1/accounts/remove", {"user": "owner"}).status == 409
    listing = owner.get("/v1/accounts").json()
    assert {account["name"] for account in listing["accounts"]} == {"owner", "ada", "mo"}
    # (taken out: its sessions end at once)
    assert owner.post("/v1/accounts/remove", {"user": "ada"}).status == 200
    assert admin.get("/v1/status").status == 401
    # (each command the main thread ran, with its role's permissions)
    assert panel.wait_for("audit: web mo ")
    events = [json.loads(line) for line in (tmp_path / "control_audit.log").read_text().splitlines()]
    assert any(event["action"] == "account_role" and event["target"] == "mo" for event in events)
    assert any(event["action"] == "account_remove" and event["target"] == "ada" for event in events)


def test_csrf_and_restricted(panel):
    """A change needs the session's CSRF token from this server's page;
    when the owner requires a second factor, an account without one may do
    nothing but set it up."""
    owner = make_owner(panel)
    moderator = invite(owner, "moderator", "mo")
    good = moderator.headers()
    assert http(panel, "POST", "/v1/command", dict(good, **{"X-CSRF-Token": "0" * 64}), {"command": "sv_kick 1"}) \
        .status == 403
    assert http(panel, "POST", "/v1/command", dict(good, Origin="http://evil.example"), {"command": "sv_kick 1"}) \
        .status == 403
    assert owner.post("/v1/accounts/require_2fa", {"value": "true"}).status == 200
    assert moderator.get("/v1/session").json()["restricted"] is True
    assert moderator.command("sv_kick 1").status == 403
    assert moderator.get("/v1/players").status == 403
    secret = moderator.post("/v1/account/totp/begin").json()["secret"]
    assert moderator.post("/v1/account/totp/enable", {"code": totp(secret)}).status == 200
    assert moderator.command("sv_kick 1").json()["ok"] is True
    assert moderator.post("/v1/account/totp/disable", {"password": PASSWORD, "code": totp(secret, step_offset=1)}) \
        .status == 403


def test_invites_and_resets(panel):
    """An invitation is good once; a reset sets a new password (and turns
    the second factor off), for its own account only, and ends the old
    sessions; invitations can be revoked."""
    owner = make_owner(panel)
    response = owner.post("/v1/accounts/invite", {"role": "moderator"})
    code = response.json()["code"]
    origin = {"Origin": f"http://127.0.0.1:{panel.port}"}
    assert http(panel, "POST", "/v1/invite", origin, {"code": code, "user": "mo", "password": PASSWORD}).status == 200
    assert http(panel, "POST", "/v1/invite", origin, {"code": code, "user": "mo2", "password": PASSWORD}).status == 403
    moderator = Browser(panel)
    assert moderator.login("mo").status == 200
    reset = owner.post("/v1/accounts/reset", {"user": "mo"}).json()["code"]
    assert http(panel, "POST", "/v1/invite", origin, {"code": reset, "user": "owner",
                                                      "password": "a new long password"}).status == 400
    assert http(panel, "POST", "/v1/invite", origin, {"code": reset, "user": "mo",
                                                      "password": "a new long password"}).status == 200
    assert moderator.get("/v1/status").status == 401
    assert Browser(panel).login("mo").status == 401
    assert Browser(panel).login("mo", "a new long password").status == 200
    pending = owner.post("/v1/accounts/invite", {"role": "admin"}).json()
    listing = owner.get("/v1/accounts").json()
    assert len(listing["invites"]) == 1 and pending["code"] not in json.dumps(listing)
    assert owner.post("/v1/accounts/revoke", {"id": listing["invites"][0]["id"]}).status == 200
    assert http(panel, "POST", "/v1/invite", origin, {"code": pending["code"], "user": "x",
                                                      "password": PASSWORD}).status == 403
    # (the console: an invitation printed there)
    output = "\n".join(panel.console("sv_account_invite moderator", "#invite="))
    assert http(panel, "POST", "/v1/invite", origin, {"code": invite_code(output), "user": "con",
                                                      "password": PASSWORD}).status == 200
    assert "con" in "\n".join(panel.console("sv_account_list", "second factor required"))


def test_bind_to_game(panel):
    """An account bound to its player's game (the stand-in answers as the
    game would: player 1 accepts, 2 declines, others have no Delta)."""
    owner = make_owner(panel)
    assert owner.post("/v1/account/bind", {"player": "3"}).status == 200
    deadline = time.time() + 10
    while owner.get("/v1/account/bind").json()["state"] in ("pending", "sent") and time.time() < deadline:
        time.sleep(0.1)
    assert owner.get("/v1/account/bind").json()["state"] in ("not_delta", "none")
    assert owner.post("/v1/account/bind", {"player": "1"}).status == 200
    deadline = time.time() + 10
    state = None
    while time.time() < deadline:
        state = owner.get("/v1/account/bind").json()["state"]
        if state not in ("pending", "sent"):
            break
        time.sleep(0.1)
    assert state == "accepted"
    assert owner.get("/v1/session").json()["key"] == "11" * 32
    assert owner.post("/v1/account/unbind").status == 200
    assert owner.get("/v1/session").json()["key"] is None


def test_file_and_audit(panel):
    """A playlist's file saved whole (its text to the main thread with the
    command); reads of the audit file for those who may view."""
    owner = make_owner(panel)
    text = "bloodgulch slayer\nprisoner ctf\n"
    response = owner.post("/v1/file", {"kind": "playlist", "name": "mine", "text": text})
    assert response.status == 200 and response.json()["output"] == f"saved {len(text)} bytes\n"
    assert owner.post("/v1/file", {"kind": "script", "name": "x", "text": "y"}).status == 400
    moderator = invite(owner, "moderator", "mo")
    assert moderator.post("/v1/file", {"kind": "playlist", "name": "mine", "text": text}).status == 403
    audit = moderator.get("/v1/audit")
    assert audit.status == 200 and any(event["action"] == "invite" for event in audit.json()["lines"])
    # (a read, as JSON)
    assert owner.post("/v1/query", {"command": "sv_kick 1"}).status == 400
    assert owner.post("/v1/query", {"command": "sv_status"}).status == 200


def tls_request(port, fingerprint, method, path, headers=None, body=None):
    """A request over HTTPS, the certificate pinned by its fingerprint."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    with socket.create_connection(("127.0.0.1", port), timeout=30) as plain:
        with context.wrap_socket(plain) as connection:
            seen = hashlib.sha256(connection.getpeercert(binary_form=True)).hexdigest().upper()
            assert ":".join(seen[i:i + 2] for i in range(0, 64, 2)) == fingerprint
            lines = [f"{method} {path} HTTP/1.1", f"Host: 127.0.0.1:{port}"]
            payload = b""
            if body is not None:
                payload = json.dumps(body).encode()
                lines += ["Content-Type: application/json", f"Content-Length: {len(payload)}"]
            lines += [f"{name}: {value}" for name, value in (headers or {}).items()]
            connection.sendall(("\r\n".join(lines) + "\r\n\r\n").encode() + payload)
            chunks = []
            while True:
                try:
                    chunk = connection.recv(65536)
                except (ssl.SSLError, ConnectionResetError):
                    break
                if not chunk:
                    break
                chunks.append(chunk)
    return Response(b"".join(chunks))


def test_https_beyond_loopback(harness_binary, tmp_path):  # noqa: F811
    """Beyond the loopback address the panel is HTTPS alone: the setup link
    and the login over it, the cookie Secure, the fingerprint the server
    printed the one the browser sees; plain HTTP to it is told to use
    HTTPS."""
    panel = Harness(harness_binary, tmp_path, listen="0.0.0.0")
    try:
        line = panel.wait_for("#setup=set_")
        code = re.search(r"#setup=(set_[0-9a-f]{32})", line).group(1)
        fingerprint = re.search(r"fingerprint is ([0-9A-F:]{95})", panel.wait_for("notice: the control panel's certificate's SHA-256 fingerprint is")).group(1)
        origin = {"Origin": f"https://127.0.0.1:{panel.port}"}
        hello = tls_request(panel.port, fingerprint, "GET", "/v1/hello")
        assert hello.status == 200 and hello.json()["tls"] is True and hello.json()["fingerprint"] == fingerprint
        assert tls_request(panel.port, fingerprint, "POST", "/v1/setup", origin,
                           {"code": code, "user": "owner", "password": PASSWORD}).status == 200
        logged = tls_request(panel.port, fingerprint, "POST", "/v1/login", origin,
                             {"user": "owner", "password": PASSWORD})
        assert logged.status == 200 and any("Secure" in cookie for cookie in logged.cookies)
        plain = http(panel, "GET", "/v1/hello")
        assert plain.status == 400 and b"HTTPS" in plain.body
    finally:
        panel.stop()
