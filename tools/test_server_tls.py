"""Runs the dedicated server's control TLS unit (server/platform/control_tls.c,
through server/tests/tls_test.c) with the build machine's C compiler, under
AddressSanitizer and UndefinedBehaviorSanitizer where it has them, against
the vendored Mbed TLS (built once for the module, without sanitizers).

What it checks: the server's own certificate made once (its key readable by
its owner alone) and kept across starts; a broken one made again; the
operator's certificate and key used as they are, and read again when they
change; and real handshakes on the loopback address in a nonblocking poll
loop, from Python's ssl client, over TLS 1.2 and 1.3, the certificate pinned
by the fingerprint the server prints; plain HTTP on the port answered
plainly."""
import concurrent.futures
import hashlib
import os
import re
import shutil
import socket
import ssl
import stat
import subprocess
import threading
import time
import warnings
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
MBEDTLS = ROOT / "port" / "third_party" / "mbedtls"
SOURCES = [ROOT / "server" / "tests" / "tls_test.c", ROOT / "server" / "platform" / "control_tls.c"]
# (the build machine's Python may have no TLS 1.3, as macOS's own LibreSSL
# Python: then TLS 1.2 alone is tried)
# (the server speaks TLS 1.2 alone: control_tls.c)
NEWEST = ssl.TLSVersion.TLSv1_2
NEWEST_NAME = "TLSv1.2"
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def build_mbedtls(cc, folder):
    """Mbed TLS's library, compiled into a static archive in folder."""
    folder.mkdir(parents=True, exist_ok=True)
    sources = sorted((MBEDTLS / "library").glob("*.c"))

    def compile_one(source):
        obj = folder / (source.stem + ".o")
        result = subprocess.run([cc, "-std=gnu11", "-O1", "-w", f"-I{MBEDTLS / 'include'}",
                                 f"-I{MBEDTLS / 'library'}", "-c", str(source), "-o", str(obj)],
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        objects = list(pool.map(compile_one, sources))
    archive = folder / "libmbedtls_all.a"
    ar = shutil.which("ar")
    assert ar, "no ar"
    result = subprocess.run([ar, "rcs", str(archive), *map(str, objects)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return archive


@pytest.fixture(scope="module")
def binary(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    if os.name != "posix":
        pytest.skip("the control unit is POSIX's")
    folder = tmp_path_factory.mktemp("tls")
    archive = build_mbedtls(cc, folder / "mbedtls")
    output = folder / "tls_test"
    base = [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", f"-I{MBEDTLS / 'include'}",
            f"-I{ROOT / 'server' / 'platform'}", *map(str, SOURCES), str(archive), "-o", str(output)]
    result = subprocess.run(base[:1] + SANITIZERS + base[1:], capture_output=True, text=True)
    if result.returncode != 0:
        # (a compiler without the sanitizers' runtime: unsanitized)
        result = subprocess.run(base, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return output


ENVIRONMENT = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")


def start(binary, data, *arguments):
    run = subprocess.run([str(binary), "start", str(data), *map(str, arguments)], capture_output=True, text=True,
                         env=ENVIRONMENT, timeout=120)
    fields = dict(line.split(" ", 1) for line in run.stdout.splitlines() if " " in line)
    return run, fields


def test_own_certificate_made_once(binary, tmp_path):
    run, first = start(binary, tmp_path, "", "", "My Server, with=commas")
    assert run.returncode == 0, run.stdout + run.stderr
    assert first["made"] == "1" and first["self_signed"] == "1"
    assert re.fullmatch(r"([0-9A-F]{2}:){31}[0-9A-F]{2}", first["fingerprint"])
    key = tmp_path / "control_tls.key"
    certificate = tmp_path / "control_tls.crt"
    assert stat.S_IMODE(key.stat().st_mode) == 0o600
    assert "BEGIN EC PRIVATE KEY" in key.read_text()
    der = ssl.PEM_cert_to_DER_cert(certificate.read_text())
    assert ":".join(f"{b:02X}" for b in hashlib.sha256(der).digest()) == first["fingerprint"]
    # the same certificate, the same fingerprint, on every start
    run, second = start(binary, tmp_path)
    assert run.returncode == 0 and second["made"] == "0"
    assert second["fingerprint"] == first["fingerprint"]
    # a key opened up to others is closed again
    key.chmod(0o644)
    start(binary, tmp_path)
    assert stat.S_IMODE(key.stat().st_mode) == 0o600
    assert not list(tmp_path.glob("*.new"))


def test_broken_own_certificate_made_again(binary, tmp_path):
    run, first = start(binary, tmp_path)
    assert run.returncode == 0
    (tmp_path / "control_tls.key").write_text("not a key\n")
    run, second = start(binary, tmp_path)
    assert run.returncode == 0 and second["made"] == "1"
    assert second["fingerprint"] != first["fingerprint"]
    # (a certificate whose key is another's)
    other = tmp_path / "other"
    other.mkdir()
    start(binary, other)
    shutil.copy(other / "control_tls.key", tmp_path / "control_tls.key")
    run, third = start(binary, tmp_path)
    assert run.returncode == 0 and third["made"] == "1"


def test_operator_certificate(binary, tmp_path):
    # (a pair the unit made, used as the operator's, under other names)
    maker = tmp_path / "maker"
    maker.mkdir()
    run, made = start(binary, maker)
    assert run.returncode == 0
    data = tmp_path / "data"
    (data / "tls").mkdir(parents=True)
    shutil.copy(maker / "control_tls.crt", data / "tls" / "fullchain.pem")
    shutil.copy(maker / "control_tls.key", data / "tls" / "privkey.pem")
    run, used = start(binary, data, "tls/fullchain.pem", "tls/privkey.pem")
    assert run.returncode == 0, run.stdout
    assert used["made"] == "0" and used["self_signed"] == "0"
    assert used["fingerprint"] == made["fingerprint"]
    assert not (data / "control_tls.crt").exists()
    # (absolute paths too)
    run, used = start(binary, data, data / "tls" / "fullchain.pem", data / "tls" / "privkey.pem")
    assert run.returncode == 0
    # missing, mismatched, or only one of the two: refused, never replaced
    run, fields = start(binary, data, "tls/missing.pem", "tls/privkey.pem")
    assert run.returncode == 1 and "certificate" in fields["error"]
    other = tmp_path / "other"
    other.mkdir()
    start(binary, other)
    shutil.copy(other / "control_tls.key", data / "tls" / "otherkey.pem")
    run, fields = start(binary, data, "tls/fullchain.pem", "tls/otherkey.pem")
    assert run.returncode == 1 and "not the certificate's" in fields["error"]
    run, fields = start(binary, data, "tls/fullchain.pem", "")
    assert run.returncode == 1 and "both" in fields["error"]


def test_operator_rsa_certificate(binary, tmp_path):
    openssl = shutil.which("openssl")
    if not openssl:
        pytest.skip("no openssl")
    result = subprocess.run([openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2", "-subj",
                             "/CN=localhost", "-keyout", str(tmp_path / "key.pem"), "-out", str(tmp_path / "cert.pem")],
                            capture_output=True, text=True)
    if result.returncode != 0:
        pytest.skip("openssl cannot make a certificate here")
    server = Server(binary, tmp_path, tmp_path / "cert.pem", tmp_path / "key.pem")
    try:
        assert server.fields["self_signed"] == "0"
        assert server.request(ssl.TLSVersion.TLSv1_2) == "TLSv1.2"
        assert server.request(NEWEST) == NEWEST_NAME
    finally:
        server.stop()


class Server:
    """tls_test serve: its port, fingerprint and output lines."""

    def __init__(self, binary, data, cert="", key=""):
        self.process = subprocess.Popen([str(binary), "serve", str(data), str(cert), str(key)],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        text=True, env=ENVIRONMENT)
        self.lines = []
        self.errors = []
        threading.Thread(target=self._read, args=(self.process.stdout, self.lines), daemon=True).start()
        threading.Thread(target=self._read, args=(self.process.stderr, self.errors), daemon=True).start()
        self.wait_for("port ")
        self.fields = dict(line.split(" ", 1) for line in self.lines if " " in line)
        self.port = int(self.fields["port"])

    @staticmethod
    def _read(stream, lines):
        for line in stream:
            lines.append(line.rstrip("\n"))

    def wait_for(self, prefix, start=0, timeout=60):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for line in self.lines[start:]:
                if line.startswith(prefix):
                    return line
            if self.process.poll() is not None:
                break
            time.sleep(0.02)
        raise AssertionError(f"no {prefix!r}: {self.lines} {self.errors}")

    def command(self, text):
        start = len(self.lines)
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()
        return self.wait_for(text.split(" ")[0] + " ", start)

    def context(self, version):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        context.minimum_version = version
        context.maximum_version = version
        context.set_alpn_protocols(["http/1.1"])
        return context

    def request(self, version, fingerprint=None):
        """One HTTPS request: the protocol the server says it agreed, after
        the certificate is checked against the fingerprint (pinned)."""
        with socket.create_connection(("127.0.0.1", self.port), timeout=30) as raw:
            with self.context(version).wrap_socket(raw) as tls:
                der = tls.getpeercert(binary_form=True)
                pin = ":".join(f"{b:02X}" for b in hashlib.sha256(der).digest())
                assert pin == (fingerprint or self.fields["fingerprint"])
                assert tls.selected_alpn_protocol() == "http/1.1"
                version = tls.version()
                tls.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
                data = b""
                while True:
                    try:
                        chunk = tls.recv(4096)
                    except (ssl.SSLZeroReturnError, ConnectionResetError):
                        break
                    if not chunk:
                        break
                    data += chunk
        head, _, body = data.partition(b"\r\n\r\n")
        assert head.startswith(b"HTTP/1.1 200")
        assert body.decode() == version
        return body.decode()

    def stop(self):
        try:
            self.process.stdin.write("quit\n")
            self.process.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass
        try:
            self.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        assert self.process.returncode == 0, self.errors


def test_handshakes(binary, tmp_path):
    server = Server(binary, tmp_path)
    try:
        assert server.request(NEWEST) == NEWEST_NAME
        assert server.request(ssl.TLSVersion.TLSv1_2) == "TLSv1.2"
        # (several at once, through the one poll loop)
        results = []
        threads = [threading.Thread(target=lambda: results.append(server.request(NEWEST)))
                   for _ in range(6)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        assert results == [NEWEST_NAME] * 6
        # plain HTTP: told so, plainly
        with socket.create_connection(("127.0.0.1", server.port), timeout=30) as plain:
            plain.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
            data = b""
            while True:
                chunk = plain.recv(4096)
                if not chunk:
                    break
                data += chunk
        assert data.startswith(b"HTTP/1.1 400") and b"HTTPS only" in data
        server.wait_for("plain")
        # junk that is no TLS: the connection dropped, the server goes on
        with socket.create_connection(("127.0.0.1", server.port), timeout=30) as junk:
            junk.sendall(bytes(range(1, 64)) * 4)
            junk.settimeout(10)
            try:
                while junk.recv(4096):
                    pass
            except (ConnectionResetError, socket.timeout):
                pass
        assert server.request(NEWEST) == NEWEST_NAME
        # TLS 1.1 is refused
        with socket.create_connection(("127.0.0.1", server.port), timeout=30) as raw:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE
            try:
                with warnings.catch_warnings():
                    warnings.simplefilter("ignore", DeprecationWarning)
                    context.minimum_version = ssl.TLSVersion.TLSv1_1
                    context.maximum_version = ssl.TLSVersion.TLSv1_1
            except (ValueError, ssl.SSLError):
                pytest.skip("this Python cannot offer TLS 1.1")
            with pytest.raises((ssl.SSLError, ConnectionResetError, OSError)):
                context.wrap_socket(raw).do_handshake()
    finally:
        server.stop()


def test_operator_reload(binary, tmp_path):
    first, second = tmp_path / "first", tmp_path / "second"
    first.mkdir()
    second.mkdir()
    _, one = start(binary, first)
    _, two = start(binary, second)
    data = tmp_path / "data"
    data.mkdir()
    shutil.copy(first / "control_tls.crt", data / "cert.pem")
    shutil.copy(first / "control_tls.key", data / "key.pem")
    server = Server(binary, data, "cert.pem", "key.pem")
    try:
        assert server.fields["fingerprint"] == one["fingerprint"]
        assert server.command("reload 1000").startswith("reload 0 ")
        # a renewal: the new pair from the next connection on
        shutil.copy(second / "control_tls.crt", data / "cert.pem")
        shutil.copy(second / "control_tls.key", data / "key.pem")
        os.utime(data / "cert.pem", (time.time() + 5, time.time() + 5))
        # (not looked at again within a minute)
        assert server.command("reload 1030").startswith("reload 0 ")
        assert server.command("reload 1061") == f"reload 1 {two['fingerprint']}"
        assert server.request(NEWEST, two["fingerprint"]) == NEWEST_NAME
        # a renewal half written: the old pair stays
        (data / "key.pem").write_text("half\n")
        line = server.command("reload 1200")
        assert line.startswith(f"reload -1 {two['fingerprint']}")
        assert server.request(ssl.TLSVersion.TLSv1_2, two["fingerprint"]) == "TLSv1.2"
    finally:
        server.stop()
