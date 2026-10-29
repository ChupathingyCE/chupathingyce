"""Builds and runs the host test of the consoles' HTTPS client
(port/console/https/console_https.c, through port/console/tests/console_https_test.c)
with the consoles' Mbed TLS configuration (console_mbedtls_config.h), against
a local TLS server (Python's ssl), over TLS 1.2 and 1.3 (1.2 alone where this
Python has no 1.3).

What it checks:
- the site (stats.chupathingy.org) by its pinned key: a pinned key is taken;
  another key refused, even under a valid chain; a certificate naming
  another host refused, even with the pinned key;
- the clock never matters: certificates long expired, or not valid for
  decades, are taken by their pinned key or root;
- GitHub's (raw.githubusercontent.com) by its chain: one to a pinned root is
  taken (an RSA chain, as GitHub's), one to another root refused, and one
  naming another host refused;
- a host it does not know, no random numbers, no port, an answer larger
  than the caller's buffer: each refused with its own error;
- a POST's body and headers as the server saw them;
- Mbed TLS's arena: its peak under CONSOLE_HTTPS_ARENA_SIZE, and empty at
  the end of every request; nothing on the heap (the client's own malloc
  is never called: the library has no other).

Needs a C compiler. Usage: test_console_https.py [--cc clang] [--keep DIR]
"""

import argparse
import concurrent.futures
import os
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MBEDTLS = ROOT / "port/third_party/mbedtls"
HTTPS = ROOT / "port/console/https"
TESTS = ROOT / "port/console/tests"
MONOCYPHER = ROOT / "port/third_party/monocypher"
PINS_TOOL = ROOT / "tools/console_pins.py"
SITE = "stats.chupathingy.org"
GITHUB = "raw.githubusercontent.com"
NOW = ("20200101000000", "20991231235959")
EXPIRED = ("20000101000000", "20010101000000")
FUTURE = ("20900101000000", "20991231235959")


def build_archive(cc, folder, flags):
    folder.mkdir(parents=True, exist_ok=True)
    sources = sorted((MBEDTLS / "library").glob("*.c"))

    def compile_one(source):
        obj = folder / (source.stem + ".o")
        result = subprocess.run([cc, "-std=c99", "-O1", "-w", *flags, f"-I{MBEDTLS / 'include'}",
                                 f"-I{MBEDTLS / 'library'}", "-c", str(source), "-o", str(obj)],
                                capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(f"{source.name}: {result.stderr}")
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        objects = list(pool.map(compile_one, sources))
    archive = folder / "libmbedtls.a"
    subprocess.run([shutil.which("ar") or "ar", "rcs", str(archive), *map(str, objects)], check=True)
    return archive


def c_bytes(key):
    return ",".join(f"0x{byte:02x}" for byte in key)


def build(cc, folder, pin_keys):
    console_flags = ['-DMBEDTLS_CONFIG_FILE="console_mbedtls_config.h"', "-DCONSOLE_HTTPS_TESTS", f"-I{HTTPS}"]
    default = build_archive(cc, folder / "default", [])
    console = build_archive(cc, folder / "console", console_flags)
    pin_flags = [f"-DCONSOLE_PINS_TEST_KEY={c_bytes(pin_keys[0])}", f"-DCONSOLE_PINS_TEST_RECOVERY_KEY={c_bytes(pin_keys[1])}",
                 f"-I{MONOCYPHER}"]
    certs = folder / "console_https_certs"
    subprocess.run([cc, "-std=c99", "-O1", "-w", f"-I{MBEDTLS / 'include'}", str(TESTS / "console_https_certs.c"),
                    str(default), "-o", str(certs)], check=True)
    client = folder / "console_https_test"
    subprocess.run([cc, "-std=c99", "-D_DEFAULT_SOURCE", "-O1", "-g", "-Wall", "-Wextra", "-Werror", *console_flags,
                    *pin_flags, f"-I{MBEDTLS / 'include'}", str(TESTS / "console_https_test.c"),
                    str(HTTPS / "console_https.c"), str(HTTPS / "console_https_trust.c"),
                    str(HTTPS / "console_https_roots.c"), str(HTTPS / "console_pins.c"), str(console),
                    str(MONOCYPHER / "monocypher.c"), str(MONOCYPHER / "monocypher-ed25519.c"),
                    "-o", str(client)], check=True)
    return certs, client


class Keys:
    def __init__(self, certs, folder):
        self.certs, self.folder = certs, folder

    def run(self, *arguments):
        return subprocess.run([str(self.certs), *map(str, arguments)], check=True, capture_output=True,
                              text=True).stdout.strip()

    def key(self, name, kind="ec"):
        path = self.folder / f"{name}.key"
        self.run("key", path, kind)
        return path

    def cert(self, name, subject_key, issuer_key, issuer, subject, host, ca, dates):
        path = self.folder / f"{name}.crt"
        self.run("cert", path, subject_key, issuer_key, issuer, subject, host, int(ca), *dates)
        return path

    def pin(self, key):
        return self.run("pin", key)

    def der(self, cert):
        path = cert.with_suffix(".der")
        self.run("der", cert, path)
        return path


class Server:
    """One TLS server on the loopback address, answering every request with
    its method, path, Host, User-Agent and body."""

    def __init__(self, chain, key, version):
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version = version
        self.context.maximum_version = version
        self.context.load_cert_chain(str(chain), str(key))
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(4)
        self.port = self.listener.getsockname()[1]
        self.seen = []
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        while True:
            try:
                connection, _ = self.listener.accept()
            except OSError:
                return
            connection.settimeout(10)
            try:
                with self.context.wrap_socket(connection, server_side=True) as tls:
                    data = b""
                    while b"\r\n\r\n" not in data:
                        chunk = tls.recv(4096)
                        if not chunk:
                            break
                        data += chunk
                    head, _, body = data.partition(b"\r\n\r\n")
                    length = re.search(rb"Content-Length: (\d+)", head)
                    while length and len(body) < int(length.group(1)):
                        body += tls.recv(4096)
                    lines = head.decode().split("\r\n")
                    headers = dict(line.split(": ", 1) for line in lines[1:] if ": " in line)
                    self.seen.append((lines[0], headers, body.decode()))
                    if "/big" in lines[0]:
                        answer = b"x" * 6000
                    else:
                        answer = f"{lines[0]}|{headers.get('Host')}|{headers.get('User-Agent')}|{body.decode()}".encode()
                    tls.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s" %
                                (len(answer), answer))
            except (ssl.SSLError, OSError):
                pass
            finally:
                connection.close()

    def close(self):
        self.listener.close()


class PlainServer:
    """The console list's host's plain HTTP, for the pin document: the files
    of folder, HTTP/1.0"""

    def __init__(self, folder):
        import functools
        import http.server

        handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(folder))
        handler.log_message = lambda *arguments: None
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


def pins_tool(*arguments):
    return subprocess.run([sys.executable, str(PINS_TOOL), *map(str, arguments)], check=True, capture_output=True,
                          text=True).stdout


def publish_pins(folder, key, serial, pin, sign_other=None):
    """a signed pin document at folder's /v1/console/pins (and .sig), for the
    site with pin; sign_other: the signature of another document instead"""
    target = folder / "v1" / "console"
    target.mkdir(parents=True, exist_ok=True)
    document = target / "pins"
    pins_tool("make", "--serial", serial, "--pin", f"{SITE}={pin}", "--out", document)
    if sign_other:
        other = folder / "other.json"
        pins_tool("make", "--serial", serial + 1, "--pin", f"{SITE}={pin}", "--out", other)
        pins_tool("sign", "--key", key, other)
        (target / "pins.sig").write_text((folder / "other.json.sig").read_text())
    else:
        pins_tool("sign", "--key", key, document)


def request(client, port, host, *options):
    run = subprocess.run([str(client), str(port), host, *map(str, options)], capture_output=True, text=True,
                         timeout=60)
    fields = {}
    for line in run.stdout.splitlines():
        name, _, value = line.partition(" ")
        fields[name] = value
    fields["log"] = run.stderr
    return fields


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cc", default=shutil.which("clang") or shutil.which("cc") or "cc")
    parser.add_argument("--keep", help="build in this folder, and keep it")
    args = parser.parse_args()
    folder = Path(args.keep) if args.keep else Path(tempfile.mkdtemp(prefix="console_https_"))
    folder.mkdir(parents=True, exist_ok=True)
    failures = []

    def expect(name, fields, result, status=None, body=None):
        ok = fields.get("result", "").split(" ")[0] == str(result)
        if ok and status is not None:
            ok = fields.get("status") == str(status)
        if ok and body is not None:
            ok = body in fields.get("body", "")
        if ok and "arena was not empty" in fields["log"]:
            ok = False
        print(f"{'ok  ' if ok else 'FAIL'} {name}: {fields.get('result')}, peak {fields.get('peak')} bytes")
        if not ok:
            failures.append(name)
            print(fields)

    try:
        # (the console pin keys: the test's own pair, built into its client)
        pin_key, pin_recovery = folder / "pin.key", folder / "pin_recovery.key"
        pin_keys = []
        for path in (pin_key, pin_recovery):
            path.unlink(missing_ok=True)
            output = pins_tool("keygen", "--out", path)
            pin_keys.append(bytes.fromhex(re.search(r"public key: ([0-9a-f]{64})", output).group(1)))
        certs, client = build(args.cc, folder / "build", pin_keys)
        run = subprocess.run([str(client), "--parse"], capture_output=True, text=True)
        print(run.stdout.strip())
        if run.returncode:
            failures.append("the response parser")
        arena = int(re.search(r"CONSOLE_HTTPS_ARENA_SIZE \((\d+) \* 1024\)",
                              (HTTPS / "console_https.h").read_text()).group(1)) * 1024

        keys = Keys(certs, folder)
        # (the site: its key pinned; a chain to a root it does not pin)
        ca_key = keys.key("ca")
        ca_name = "CN=Test Root,O=Test"
        site_key, other_key = keys.key("site"), keys.key("other")
        site_name = f"CN={SITE}"
        site = keys.cert("site", site_key, ca_key, ca_name, site_name, SITE, False, NOW)
        site_other_key = keys.cert("site_other_key", other_key, ca_key, ca_name, site_name, SITE, False, NOW)
        site_wrong_name = keys.cert("site_wrong_name", site_key, ca_key, ca_name, "CN=elsewhere.example",
                                    "elsewhere.example", False, NOW)
        site_expired = keys.cert("site_expired", site_key, ca_key, ca_name, site_name, SITE, False, EXPIRED)
        site_future = keys.cert("site_future", site_key, ca_key, ca_name, site_name, SITE, False, FUTURE)
        site_self = keys.cert("site_self", site_key, site_key, site_name, site_name, SITE, False, EXPIRED)
        # (GitHub's: RSA, under a root of the test's set, or another)
        root_key, stranger_key, github_key = keys.key("root", "rsa"), keys.key("stranger", "rsa"), \
            keys.key("github", "rsa")
        root_name, stranger_name = "CN=Pinned Root,O=Test", "CN=Stranger Root,O=Test"
        root = keys.cert("root", root_key, root_key, root_name, root_name, "-", True, NOW)
        root_old = keys.cert("root_old", root_key, root_key, root_name, root_name, "-", True, EXPIRED)
        keys.cert("stranger", stranger_key, stranger_key, stranger_name, stranger_name, "-", True, NOW)
        github = keys.cert("github", github_key, root_key, root_name, "CN=*.github.io", GITHUB, False, NOW)
        github_expired = keys.cert("github_expired", github_key, root_key, root_name, "CN=*.github.io", GITHUB,
                                   False, EXPIRED)
        github_stranger = keys.cert("github_stranger", github_key, stranger_key, stranger_name, "CN=*.github.io",
                                    GITHUB, False, NOW)
        github_wrong_name = keys.cert("github_wrong_name", github_key, root_key, root_name, "CN=other.example",
                                      "other.example", False, NOW)
        pin = keys.pin(site_key)
        backup_pin = keys.pin(keys.key("backup"))
        root_der = keys.der(root)
        root_old_der = keys.der(root_old)
        pins = ["--pin", backup_pin, "--pin", pin]

        versions = [ssl.TLSVersion.TLSv1_2] + ([ssl.TLSVersion.TLSv1_3] if ssl.HAS_TLSv1_3 else [])
        for version in versions:
            tag = version.name

            def serve(cert, key, scenario, host, options, result, status=None, body=None):
                server = Server(cert, key, version)
                try:
                    fields = request(client, server.port, host, *options)
                    expect(f"{tag}: {scenario}", fields, result, status, body)
                    if fields.get("peak", "0").isdigit() and int(fields["peak"]) > arena:
                        failures.append(f"{tag}: {scenario}: the arena's peak {fields['peak']} is over {arena}")
                    return server, fields
                finally:
                    server.close()

            serve(site, site_key, "the site's pinned key", SITE, pins, 0, 200, f"GET /index.txt HTTP/1.0|{SITE}|")
            serve(site_other_key, other_key, "another key for the site", SITE, pins, 9)
            # (even a chain to a root it would trust: the site goes by its key)
            # (the backstop: a chain to the site's roots, Let's Encrypt's in the
            # build, takes a key it does not pin)
            server, fields = serve(site_other_key, other_key, "another key under the site's root (the backstop)",
                                   SITE, pins + ["--root", keys.der(keys.cert("ca", ca_key, ca_key, ca_name, ca_name,
                                                                              "-", True, NOW))], 0, 200)
            if "taken by its chain" not in fields["log"]:
                failures.append(f"{tag}: the backstop did not say so")
            serve(site_wrong_name, site_key, "the pinned key for another host", SITE, pins, 8)
            serve(site_expired, site_key, "the pinned key, its certificate expired in 2001", SITE, pins, 0, 200)
            serve(site_future, site_key, "the pinned key, its certificate valid from 2090", SITE, pins, 0, 200)
            serve(site_self, site_key, "the pinned key, self-signed", SITE, pins, 0, 200)
            server, fields = serve(site, site_key, "a POST", SITE,
                                   pins + ["--path", "/v1/console/link/start", "--body", '{"key":"00"}'], 0, 200,
                                   'POST /v1/console/link/start HTTP/1.0|stats.chupathingy.org|Warthog/1 (test)|{"key":"00"}')
            serve(site, site_key, "an answer larger than the buffer", SITE, pins + ["--path", "/big", "--size", "1000"],
                  13)

            serve(github, github_key, "GitHub's chain to a pinned root", GITHUB, ["--root", root_der], 0, 200,
                  f"GET /index.txt HTTP/1.0|{GITHUB}|")
            serve(github_expired, github_key, "GitHub's chain, expired, to an expired root", GITHUB,
                  ["--root", root_old_der], 0, 200)
            serve(github_stranger, github_key, "GitHub's chain to another root", GITHUB, ["--root", root_der], 9)
            serve(github_wrong_name, github_key, "a chain to a pinned root for another host", GITHUB,
                  ["--root", root_der], 8)

            serve(site, site_key, "a host it does not know", "example.com", pins, 2)
            serve(site, site_key, "no random numbers", SITE, pins + ["--no-random"], 5)
            serve(site, site_key, "no port given", SITE, pins + ["--no-port"], 1)

        # ---------- the pin document
        version = versions[-1]
        third_key = keys.key("third")
        site_third = keys.cert("site_third", third_key, ca_key, ca_name, site_name, SITE, False, NOW)
        plain_folder = folder / "plain"
        plain_folder.mkdir()
        plain = PlainServer(plain_folder)
        kept = folder / "console_pins.signed"

        def pinned(cert, key, scenario, options, result, says=None, absent=None):
            server = Server(cert, key, version)
            try:
                fields = request(client, server.port, SITE, "--pin", pin, "--pins", kept, *options)
            finally:
                server.close()
            expect(f"pins: {scenario}", fields, result)
            if says and says not in fields["log"]:
                failures.append(f"pins: {scenario}: the log does not say \"{says}\"")
                print(fields["log"])
            if absent and absent in fields["log"]:
                failures.append(f"pins: {scenario}: the log says \"{absent}\"")
            return fields

        try:
            other_pin, third_pin = keys.pin(other_key), keys.pin(third_key)
            publish_pins(plain_folder, pin_key, 2, other_pin)
            pinned(site_other_key, other_key, "a key it does not pin, then the pin document, then HTTPS",
                   ["--plain-port", plain.port], 0, "pin document 2 from")
            if not kept.is_file():
                failures.append("pins: the pin document was not kept")
            pinned(site_other_key, other_key, "the kept pin document, at the next start", [], 0,
                   absent="pin document")
            publish_pins(plain_folder, pin_key, 1, third_pin)
            pinned(site_third, third_key, "a valid document with a lower serial refused",
                   ["--plain-port", plain.port], 9, "not newer")
            kept.unlink()
            publish_pins(plain_folder, pin_key, 5, third_pin, sign_other=True)
            pinned(site_third, third_key, "a bad signature refused", ["--plain-port", plain.port], 9, "does not match")
            publish_pins(plain_folder, pin_key, (1 << 24) | 1, third_pin)
            pinned(site_third, third_key, "a new epoch from the primary key refused", ["--plain-port", plain.port], 9,
                   "epoch")
            publish_pins(plain_folder, pin_recovery, (1 << 24) | 1, third_pin)
            pinned(site_third, third_key, "a new epoch from the recovery key taken", ["--plain-port", plain.port], 0,
                   "pin document 16777217")
            (plain_folder / "v1" / "console" / "pins").write_text("not a document")
            kept.unlink()
            pinned(site_other_key, other_key, "a broken document refused", ["--plain-port", plain.port], 9,
                   "no new pin document")
        finally:
            plain.close()
    finally:
        if not args.keep:
            shutil.rmtree(folder, ignore_errors=True)
    if failures:
        print(f"{len(failures)} failed: {', '.join(failures)}")
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
