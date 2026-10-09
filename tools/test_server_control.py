"""Runs the C tests of the dedicated server's command lines, control API and
web admin page (server/tests/control_test.c: server/src/command_line.c,
server/platform/control_protocol.c and control_web.c, with Monocypher and
the page's files as tools/embed_webui.py embeds them) with the build
machine's C compiler, under AddressSanitizer and UndefinedBehaviorSanitizer
where it has them. The units have no SDL, socket or game dependency, so
they compile anywhere.

Then the console, control API and web page whole (server_control.c, in
server/tests/control_harness.c, a stand-in for the game's main thread),
driven over real sockets on the loopback address: the page's files and
headers, logins and sessions, CSRF, bearer tokens, the limits, the
console's sv_admin_* commands, and junk requests."""
import json
import os
import random
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools.embed_webui import generate as generate_webui  # noqa: E402

MONOCYPHER = ROOT / "port" / "third_party" / "monocypher" / "monocypher.c"
QRCODEGEN = ROOT / "port" / "third_party" / "qrcodegen"
MBEDTLS = ROOT / "port" / "third_party" / "mbedtls"
PLATFORM = ROOT / "server" / "platform"
SOURCES = [
    ROOT / "server" / "tests" / "control_test.c",
    ROOT / "server" / "src" / "command_line.c",
    ROOT / "server" / "src" / "server_config.c",
    PLATFORM / "control_protocol.c",
    PLATFORM / "control_web.c",
    PLATFORM / "control_roles.c",
    PLATFORM / "control_accounts.c",
    PLATFORM / "control_link_protocol.c",
    MONOCYPHER,
    MONOCYPHER.parent / "monocypher-ed25519.c",
]
HARNESS_SOURCES = [
    ROOT / "server" / "tests" / "control_harness.c",
    ROOT / "server" / "src" / "command_line.c",
    PLATFORM / "server_control.c",
    PLATFORM / "server_roles.c",
    PLATFORM / "control_protocol.c",
    PLATFORM / "control_web.c",
    PLATFORM / "control_roles.c",
    PLATFORM / "control_accounts.c",
    PLATFORM / "control_tls.c",
    QRCODEGEN / "qrcodegen.c",
    MONOCYPHER,
]
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


_MBEDTLS_ARCHIVES = {}


def mbedtls_archive(cc, folder):
    """Mbed TLS's library as a static archive (built once a run, in folder):
    the accounts' TOTP (HMAC-SHA-1) and the control panel's TLS use it."""
    if cc not in _MBEDTLS_ARCHIVES:
        from tools.test_server_tls import build_mbedtls
        _MBEDTLS_ARCHIVES[cc] = build_mbedtls(cc, folder)
    return _MBEDTLS_ARCHIVES[cc]


def build(cc, binary, extra, sources=None, standard="c11"):
    """Compiles sources (the unit tests', by default) and the page's files
    into binary; the compiler's result."""
    webui = binary.parent / f"{binary.name}_webui.c"
    generate_webui(webui)
    archive = mbedtls_archive(cc, binary.parent / "mbedtls")
    return subprocess.run(
        [cc, f"-std={standard}", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *extra,
         f"-I{ROOT / 'port' / 'third_party' / 'monocypher'}", f"-I{MBEDTLS / 'include'}", f"-I{QRCODEGEN}",
         *(str(source) for source in (sources or SOURCES)), str(webui), str(archive), "-o", str(binary),
         *(["-lpthread"] if sources else [])],
        capture_output=True, text=True,
    )


def build_sanitized(cc, binary, sources=None, standard="c11"):
    result = build(cc, binary, SANITIZERS, sources, standard)
    if result.returncode != 0:
        # (a compiler without the sanitizers' runtime: unsanitized)
        result = build(cc, binary, [], sources, standard)
    assert result.returncode == 0, result.stdout + result.stderr


def test_server_control(tmp_path):
    """Builds the units with their tests, warnings as errors (sanitized if the
    compiler can), and runs them with a few thousand random inputs."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "control_test"
    build_sanitized(cc, binary)
    run = subprocess.run([str(binary), "5000"], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


# ---------- the console, control API and web page whole


@pytest.fixture(scope="module")
def harness_binary(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    if os.name != "posix":
        pytest.skip("the control unit is POSIX's")
    binary = tmp_path_factory.mktemp("harness") / "control_harness"
    build_sanitized(cc, binary, HARNESS_SOURCES, "gnu11")
    return binary


class Harness:
    """A running harness: its port, its output's lines (read as they come),
    the token it printed, and its console (standard input)."""

    def __init__(self, binary, data, listen="127.0.0.1", extra=None, expect="listen on"):
        data.mkdir(parents=True, exist_ok=True)
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            self.port = probe.getsockname()[1]
        environment = dict(os.environ, HARNESS_DATA=str(data), HALO_DEDICATED_CONTROL=f"{listen}:{self.port}",
                           HALO_DEDICATED_CONSOLE="true", ASAN_OPTIONS="detect_leaks=0", **(extra or {}))
        self.process = subprocess.Popen([str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, env=environment, text=True)
        self.lines = []
        self.errors = []
        threading.Thread(target=self._read, args=(self.process.stdout, self.lines), daemon=True).start()
        threading.Thread(target=self._read, args=(self.process.stderr, self.errors), daemon=True).start()
        self.wait_for(expect)
        tokens = [line.strip() for line in self.lines if re.fullmatch(r"\s*chce_[0-9a-f]{64}\s*", line)]
        self.token = tokens[0] if tokens else None

    @staticmethod
    def _read(stream, lines):
        for line in stream:
            lines.append(line.rstrip("\n"))

    def wait_for(self, text, start=0, timeout=60):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for line in self.lines[start:]:
                if text in line:
                    return line
            assert self.process.poll() is None, "\n".join(self.lines + self.errors)
            time.sleep(0.02)
        raise AssertionError(f"no {text!r} in:\n" + "\n".join(self.lines + self.errors))

    def console(self, line, until, timeout=60):
        start = len(self.lines)
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()
        self.wait_for(until, start, timeout)
        return self.lines[start:]

    def stop(self):
        alive = self.process.poll() is None
        self.process.terminate()
        self.process.wait(10)
        assert alive, "the harness died:\n" + "\n".join(self.errors[-40:])
        assert not any("Sanitizer" in line or "runtime error" in line for line in self.errors), \
            "\n".join(self.errors[-60:])


@pytest.fixture
def harness(harness_binary, tmp_path):
    running = Harness(harness_binary, tmp_path)
    yield running
    running.stop()


def raw(port, data, timeout=30):
    """Sends bytes as a request, its writing side closed after: the
    response's bytes."""
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as connection:
        connection.sendall(data)
        connection.shutdown(socket.SHUT_WR)
        chunks = []
        while True:
            chunk = connection.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
    return b"".join(chunks)


class Response:
    def __init__(self, data):
        head, _, self.body = data.partition(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        self.status = int(lines[0].split()[1]) if lines[0] else 0
        self.headers = {}
        self.cookies = []
        for line in lines[1:]:
            name, _, value = line.partition(":")
            self.headers[name.strip().lower()] = value.strip()
            if name.strip().lower() == "set-cookie":
                self.cookies.append(value.strip())

    def json(self):
        return json.loads(self.body)

    @property
    def session(self):
        for cookie in self.cookies:
            match = re.match(r"chce_session=([0-9a-f]{64});", cookie)
            if match:
                return match.group(1)
        return None


def http(harness, method, path, headers=None, body=None):
    lines = [f"{method} {path} HTTP/1.1", f"Host: 127.0.0.1:{harness.port}"]
    payload = b""
    if body is not None:
        payload = (json.dumps(body) if not isinstance(body, (bytes, str)) else body)
        payload = payload.encode() if isinstance(payload, str) else payload
        lines.append("Content-Type: application/json")
        lines.append(f"Content-Length: {len(payload)}")
    for name, value in (headers or {}).items():
        lines.append(f"{name}: {value}")
    return Response(raw(harness.port, ("\r\n".join(lines) + "\r\n\r\n").encode() + payload))


def login(harness, token=None, headers=None):
    return http(harness, "POST", "/v1/login", dict({"Origin": f"http://127.0.0.1:{harness.port}"}, **(headers or {})),
                {"token": token or harness.token})


def test_web_files_and_headers(harness):
    """The page's files to anyone, each with the security headers; nothing
    else, by any path."""
    page = http(harness, "GET", "/")
    assert page.status == 200 and page.headers["content-type"].startswith("text/html")
    assert b'<script src="/app.js"' in page.body
    for response in (page, http(harness, "GET", "/v1/status"), http(harness, "GET", "/nothing")):
        csp = response.headers["content-security-policy"]
        assert "default-src 'self'" in csp and "script-src 'self'" in csp and "style-src 'self'" in csp
        assert "frame-ancestors 'none'" in csp and "unsafe" not in csp
        assert response.headers["x-frame-options"] == "DENY"
        assert response.headers["referrer-policy"] == "no-referrer"
        assert response.headers["cache-control"] == "no-store"
        assert response.headers["x-content-type-options"] == "nosniff"
    assert http(harness, "GET", "/index.html").body == page.body
    assert http(harness, "GET", "/app.js").headers["content-type"].startswith("text/javascript")
    assert http(harness, "GET", "/app.css").headers["content-type"].startswith("text/css")
    assert http(harness, "GET", "/icon.svg").headers["content-type"] == "image/svg+xml"
    for path in ("/../index.html", "/./index.html", "//index.html", "/index.html/", "/x/../app.js", "/INDEX.HTML",
                 "/control_credentials.txt", "/../control_credentials.txt", "/server_control.c", "/etc/passwd",
                 "/v1/../index.html", "/app.js.map"):
        response = http(harness, "GET", path)
        # (an API path's: not logged in)
        assert response.status == (401 if path.startswith("/v1/") else 404), path
        assert b"argon2id" not in response.body and b"<html" not in response.body
    for path in ("/%2e%2e/control_credentials.txt", "/..%2fcontrol_credentials.txt", "/index.html#x"):
        assert http(harness, "GET", path).status == 400, path
    assert http(harness, "POST", "/index.html", body={}).status == 405
    assert http(harness, "GET", "/", body="x").status == 400


def test_web_session_lifecycle(harness):
    """Log in, use the session, its CSRF token, log out; the cookie's flags;
    every command audited with the session's credential."""
    assert http(harness, "GET", "/v1/session").status == 401
    assert login(harness, "chce_" + "0" * 64).status == 401
    assert login(harness, "nonsense").status == 401
    response = login(harness)
    assert response.status == 200, response.body
    cookie = response.cookies[0]
    assert "HttpOnly" in cookie and "SameSite=Strict" in cookie and "Path=/" in cookie and "Secure" not in cookie
    session, csrf = response.session, response.json()["csrf"]
    assert session and re.fullmatch(r"[0-9a-f]{64}", csrf) and csrf != session
    assert response.json()["name"] == "admin"
    jar = {"Cookie": f"chce_session={session}"}
    assert http(harness, "GET", "/v1/session", jar).json()["csrf"] == csrf
    status = http(harness, "GET", "/v1/status", jar)
    assert status.status == 200 and status.json()["map"] == "bloodgulch"
    for path in ("/v1/players", "/v1/bans", "/v1/mapcycle", "/v1/maps", "/v1/log?since=0"):
        assert http(harness, "GET", path, jar).status == 200, path

    # a change: refused without the CSRF token, with a wrong one, from another
    # origin or site; done with it
    origin = {"Origin": f"http://127.0.0.1:{harness.port}"}
    kick = {"command": "sv_kick 1"}
    assert http(harness, "POST", "/v1/command", jar, kick).status == 403
    assert http(harness, "POST", "/v1/command", dict(jar, **{"X-CSRF-Token": "0" * 64}), kick).status == 403
    assert http(harness, "POST", "/v1/command", dict(jar, **{"X-CSRF-Token": session}), kick).status == 403
    good = dict(jar, **origin, **{"X-CSRF-Token": csrf})
    assert http(harness, "POST", "/v1/command", dict(good, Origin="http://evil.example"), kick).status == 403
    assert http(harness, "POST", "/v1/command", dict(good, Origin="null"), kick).status == 403
    assert http(harness, "POST", "/v1/command", dict(good, **{"Sec-Fetch-Site": "cross-site"}), kick).status == 403
    assert not any("sv_kick" in line for line in harness.lines)
    start = len(harness.lines)
    result = http(harness, "POST", "/v1/command", good, kick)
    assert result.status == 200 and result.json() == {"ok": True, "output": "ran sv_kick 1\n"}
    audit = harness.wait_for("sv_kick 1", start)
    assert re.fullmatch(r"audit: web admin [0-9a-f]{8}: sv_kick 1 \(owner 3ff\)", audit)
    assert harness.wait_for("web login: admin")

    # a login from another origin or site is refused (login CSRF)
    assert login(harness, headers={"Origin": "http://evil.example"}).status == 403
    assert login(harness, headers={"Sec-Fetch-Site": "cross-site"}).status == 403

    # over HTTPS (a reverse proxy says so): Secure
    secure = login(harness, headers={"X-Forwarded-Proto": "https"})
    assert secure.status == 200 and "; Secure" in secure.cookies[0]

    # logout: needs the CSRF token, clears the cookie, ends the session
    assert http(harness, "POST", "/v1/logout", jar, {}).status == 403
    out = http(harness, "POST", "/v1/logout", good, {})
    assert out.status == 200 and "Max-Age=0" in out.cookies[0]
    gone = http(harness, "GET", "/v1/status", jar)
    assert gone.status == 401 and "Max-Age=0" in gone.cookies[0]
    assert http(harness, "POST", "/v1/command", good, kick).status == 401
    harness.wait_for("web logout: admin")

    # scripts' bearer tokens work as before, with no CSRF token
    bearer = {"Authorization": f"Bearer {harness.token}"}
    assert http(harness, "GET", "/v1/status", bearer).status == 200
    start = len(harness.lines)
    assert http(harness, "POST", "/v1/command", bearer, {"command": "sv_name x"}).json()["ok"]
    assert re.fullmatch(r"audit: api admin [0-9a-f]{8}: sv_name x \(owner 3ff\)", harness.wait_for("sv_name x", start))
    assert http(harness, "GET", "/v1/session", bearer).status == 400
    # (the token never in the output, but where it was printed once)
    assert sum(harness.token in line for line in harness.lines) == 1


def test_web_login_limits(harness):
    """Wrong logins count as wrong tokens: five, and the address is refused;
    a session already made still works."""
    session = login(harness).session
    jar = {"Cookie": f"chce_session={session}"}
    for attempt in range(5):
        assert login(harness, "chce_" + f"{attempt:064x}").status == 401
    blocked = login(harness, "chce_" + "f" * 64)
    assert blocked.status == 429 and int(blocked.headers["retry-after"]) > 0
    assert http(harness, "GET", "/v1/status").status == 429
    assert http(harness, "GET", "/v1/status", jar).status == 200
    # (and the right token, checked right this run, still logs in)
    assert login(harness).status == 200
    harness.wait_for("a web login with a wrong token, from 127.0.0.1")


def test_admin_console(harness, tmp_path):
    """sv_admin_*: a token for each admin, rotated, taken out; the file
    keeps only hashes; a credential's sessions end with it."""
    credentials = tmp_path / "control_credentials.txt"
    assert len(re.findall(r"^v1 argon2id ", credentials.read_text(), re.M)) == 1
    output = harness.console("sv_admin_add ops", "can log in with it now")
    ops_token = next(line.strip() for line in output if re.fullmatch(r"\s*chce_[0-9a-f]{64}\s*", line))
    text = credentials.read_text()
    assert len(re.findall(r"^v1 argon2id ", text, re.M)) == 2 and ops_token not in text and harness.token not in text
    assert (credentials.stat().st_mode & 0o777) == 0o600
    assert "already" in "\n".join(harness.console("sv_admin_add ops", "already"))
    assert "a name is" in "\n".join(harness.console("sv_admin_add \"x\"", "a name is"))
    response = login(harness, ops_token)
    assert response.status == 200 and response.json()["name"] == "ops"
    jar = {"Cookie": f"chce_session={response.session}"}
    listing = "\n".join(harness.console("sv_admin_list", "web session"))
    assert re.search(r"ops\s+[0-9a-f]{8}\s+\(1 web session\)", listing)

    output = harness.console("sv_admin_rotate ops", "stop working now")
    new = next(line.strip() for line in output if re.fullmatch(r"\s*chce_[0-9a-f]{64}\s*", line))
    assert http(harness, "GET", "/v1/status", jar).status == 401
    assert login(harness, ops_token).status == 401
    assert login(harness, new).status == 200
    assert login(harness).status == 200

    harness.console("sv_admin_remove ops", "taken out")
    assert login(harness, new).status == 401
    assert len(re.findall(r"^v1 argon2id ", credentials.read_text(), re.M)) == 1
    assert "only credential" in "\n".join(harness.console("sv_admin_remove admin", "only credential"))
    assert login(harness).status == 200
    # (never the API's)
    bearer = {"Authorization": f"Bearer {harness.token}"}
    result = http(harness, "POST", "/v1/command", bearer, {"command": "sv_admin_add evil"})
    assert result.status == 403
    assert len(re.findall(r"^v1 argon2id ", credentials.read_text(), re.M)) == 1
    harness.wait_for("a credential taken out: ops")


def test_web_beyond_loopback_is_https(harness_binary, tmp_path):
    """Listening beyond the loopback address is HTTPS, its certificate's
    fingerprint printed; plain HTTP there is refused (TLS off) on a public
    address, and warned of on a private one."""
    running = Harness(harness_binary, tmp_path / "a", listen="0.0.0.0")
    try:
        assert running.wait_for("listen on 0.0.0.0")
        assert "(HTTPS;" in running.wait_for("listen on 0.0.0.0")
        assert running.wait_for("certificate's SHA-256 fingerprint is")
    finally:
        running.stop()
    refused = Harness(harness_binary, tmp_path / "b", listen="0.0.0.0", extra={"HALO_DEDICATED_CONTROL_TLS": "off"},
                      expect="control API is off")
    try:
        assert refused.wait_for("is a public address")
    finally:
        refused.stop()


def test_web_fuzz(harness):
    """Junk requests at the page's and the API's paths, with cookies, CSRF
    tokens and browsers' headers: answered or dropped, never a crash."""
    generator = random.Random(1234)
    session = login(harness).session
    paths = ["/", "/index.html", "/app.js", "/v1/login", "/v1/session", "/v1/logout", "/v1/command", "/v1/status",
             "/v1/log?since=1", "/../", "/v1/bans", "/v1/maps", "/" + "a" * 300]
    headers = ["Cookie: chce_session=" + session, "Cookie: chce_session=" + "0" * 64, "Cookie: a=b; chce_session=x",
               "X-CSRF-Token: " + "1" * 64, "X-CSRF-Token: x", "Origin: http://evil", "Origin: null",
               "Sec-Fetch-Site: cross-site", "X-Forwarded-Proto: https", "Forwarded: proto=https",
               "X-Background: 1", "Content-Type: application/json", "Content-Length: 5", "Authorization: Bearer x",
               "X-Forwarded-Host: " + "h" * 200, "Cookie: " + ";" * 100]
    bodies = [b"", b"{}", b'{"token": "chce_"}', b'{"command": "sv_kick 1"}', b'{"token": 1}', bytes(range(256))]
    for _ in range(400):
        lines = [f"{generator.choice(['GET', 'POST', 'PUT'])} {generator.choice(paths)} HTTP/1.1", "Host: x"]
        lines += generator.sample(headers, generator.randint(0, 5))
        data = ("\r\n".join(lines) + "\r\n\r\n").encode() + generator.choice(bodies)
        if generator.random() < 0.3:
            data = bytearray(data)
            for _ in range(generator.randint(1, 4)):
                data[generator.randrange(len(data))] = generator.randrange(256)
            data = bytes(data)
        if generator.random() < 0.2:
            data = data[:generator.randrange(len(data) + 1)]
        response = raw(harness.port, data)
        assert response == b"" or response.startswith(b"HTTP/1.1 ")
    assert http(harness, "GET", "/v1/status", {"Cookie": f"chce_session={session}"}).status == 200
