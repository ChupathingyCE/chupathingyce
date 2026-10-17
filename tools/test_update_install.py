"""The desktop self-updater from end to end: a download and an install, not
just the list and the signatures (tools/test_update_manifest.py does those).

Two builds of the updater (tools/update_install_check.c: port/linux/src's
updater.c and posix_update.c whole, built for the test, HALO_UPDATE_TEST, with
a test key) are made, an old version and a new one. A local HTTPS server
answers as GitHub does (the latest release's JSON, the release's files behind
a redirect), with a certificate only the test's build trusts
(HALO_UPDATE_TEST_CA). The old build, installed in a folder of its own as
halo, then updates itself:

  - from a release with a signed list of update files (updates.json),
  - from a release without one (its zip's own .sig, as before the list),
  - not at all from a changed zip or a changed list, the folder left as it was,
  - from a zip that adds the game's executable by its later name, which
    arrives executable and is started (the list's "main").

Linux only (the updater finds itself in /proc/self/exe); the 32-bit build
where the system's SDL 3 is the i386 one. It needs OpenSSL 3's openssl for
the test key and certificate."""
import hashlib
import http.server
import json
import os
import shutil
import ssl
import stat
import subprocess
import sys
import threading
import time
import zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import delta_table  # noqa: E402
import update_sign  # noqa: E402

pytestmark = pytest.mark.skipif(not sys.platform.startswith("linux"), reason="the updater installs on Linux")

OLD, NEW = "0.8.0", "0.8.1"
ASSET = "chupathingyce-linux-release.zip"
REPOSITORY = "ChupathingyCE/chupathingyce"
MBEDTLS = ROOT / "port/third_party/mbedtls"
ZLIB_SOURCES = ("adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "uncompr.c", "zutil.c")
MONOCYPHER = ("monocypher.c", "monocypher-ed25519.c")


def abi():
    """the host's SDL 3, else the i386 one with -m32"""
    found = subprocess.run(["pkg-config", "--exists", "sdl3"], capture_output=True).returncode == 0 \
        if shutil.which("pkg-config") else False
    if found:
        return []
    if Path("/usr/lib/i386-linux-gnu/libSDL3.so").exists():
        return ["-m32"]
    pytest.skip("needs SDL 3's development files")


def compile_objects(cc, flags, sources, directory):
    def build(source):
        target = directory / (source.parent.name + "-" + source.stem + ".o")
        done = subprocess.run([cc, *flags, "-c", "-o", str(target), str(source)], capture_output=True, text=True)
        assert done.returncode == 0, done.stderr
        return str(target)
    with ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        return list(pool.map(build, sources))


@pytest.fixture(scope="module")
def keys(tmp_path_factory):
    try:
        os.environ["OPENSSL"] = delta_table.openssl()
    except SystemExit:
        pytest.skip("needs OpenSSL 3's openssl")
    directory = tmp_path_factory.mktemp("update_key")
    return [(directory / f"{name}.pem", delta_table.keygen(directory / f"{name}.pem")) for name in ("key", "other")]


@pytest.fixture(scope="module")
def builds(keys, tmp_path_factory):
    """{version: the update test's program}"""
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("no C compiler")
    target = abi()
    directory = tmp_path_factory.mktemp("update_install")
    library = compile_objects(cc, [*target, "-std=gnu11", "-O1", "-w", f"-I{MBEDTLS / 'include'}",
                                   f"-I{MBEDTLS / 'library'}"], sorted((MBEDTLS / "library").glob("*.c")), directory)
    library += compile_objects(cc, [*target, "-std=gnu11", "-O1", "-w", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg"],
                               [ROOT / "port/third_party/zlib" / name for name in ZLIB_SOURCES], directory)
    library += compile_objects(cc, [*target, "-std=gnu11", "-O1", "-w"],
                               [ROOT / "port/third_party/monocypher" / name for name in MONOCYPHER], directory)
    key = ",".join(f"0x{byte:02x}" for byte in keys[0][1])
    programs = {}
    for version in (OLD, NEW):
        program = directory / f"update-{version}"
        built = subprocess.run(
            [cc, *target, "-std=gnu11", "-O1", "-Wall", "-Werror", "-Wno-unused-function", f'-DHALO_VERSION="{version}"',
             "-DHALO_UPDATE_TEST", f"-DHALO_UPDATE_TEST_KEY={key}", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg",
             "-Iport/linux/src", "-Iport/third_party/zlib", "-Iport/third_party/monocypher",
             f"-I{MBEDTLS / 'include'}", "-o", str(program), "tools/update_install_check.c",
             "port/linux/src/posix_update.c", "port/linux/src/update_manifest.c",
             "port/linux/src/update_signature.c", *library, "-lSDL3", "-lpthread"],
            capture_output=True, text=True, cwd=ROOT)
        assert built.returncode == 0, built.stderr
        programs[version] = program
    return programs


class Server:
    """GitHub, as the updater sees it: the latest release's JSON, and the
    releases' files behind a redirect to another address"""

    def __init__(self, directory):
        authority = directory / "ca.pem"
        authority_key = directory / "ca.key"
        certificate = directory / "server.pem"
        key = directory / "server.key"
        openssl = os.environ["OPENSSL"]
        for command in (
                ["req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2", "-subj", "/CN=update test authority",
                 "-keyout", authority_key, "-out", authority],
                ["req", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=localhost", "-keyout", key,
                 "-out", directory / "server.csr"]):
            subprocess.run([openssl, *map(str, command)], check=True, capture_output=True)
        (directory / "san.ext").write_text("subjectAltName=DNS:localhost\n")
        subprocess.run([openssl, "x509", "-req", "-in", str(directory / "server.csr"), "-CA", str(authority),
                        "-CAkey", str(authority_key), "-CAcreateserial", "-days", "2", "-extfile",
                        str(directory / "san.ext"), "-out", str(certificate)], check=True, capture_output=True)
        self.authority = authority
        self.files = {}
        self.requests = []
        server = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *arguments):
                pass

            def do_GET(self):
                server.requests.append(self.path)
                prefix = f"/{REPOSITORY}/releases/download/"
                if self.path == f"/repos/{REPOSITORY}/releases/latest":
                    body = json.dumps({"tag_name": f"v{server.latest}", "name": server.latest}).encode()
                elif self.path.startswith(prefix):
                    if self.path[len(prefix):] not in server.files:
                        self.send_error(404)
                        return
                    self.send_response(302)
                    self.send_header("Location", f"{server.url}/files/{self.path[len(prefix):]}")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                elif self.path.startswith("/files/") and self.path[7:] in server.files:
                    body = server.files[self.path[7:]]
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(certificate), str(key))
        self.httpd.socket = context.wrap_socket(self.httpd.socket, server_side=True)
        self.url = f"https://localhost:{self.httpd.server_address[1]}"
        self.latest = NEW
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def release(self, version, directory, names):
        """the release's files (names, in directory) at v<version>"""
        self.files = {f"v{version}/{name}": (directory / name).read_bytes() for name in names}
        self.requests = []


@pytest.fixture
def server(keys, tmp_path):
    directory = tmp_path / "server"
    directory.mkdir()
    running = Server(directory)
    yield running
    running.httpd.shutdown()


def add(archive, name, data, mode=None):
    """a zip entry: made on Unix with mode, or on DOS without one"""
    info = zipfile.ZipInfo(name, date_time=(2026, 10, 16, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    if mode is None:
        info.create_system = 0
    else:
        info.create_system = 3
        info.external_attr = (stat.S_IFREG | mode) << 16
    archive.writestr(info, data)


def release_zip(directory, builds, extra=()):
    """the new version's zip: its halo, a library, the extra (name, data, mode)"""
    directory.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(directory / ASSET, "w") as archive:
        add(archive, "halo", builds[NEW].read_bytes(), 0o755)
        add(archive, "SDL3-LICENSE.txt", b"new data\n", 0o644)
        for name, data, mode in extra:
            add(archive, name, data, mode)
    return directory / ASSET


def install(tmp_path, builds):
    """the old version's folder: halo, and a file of its own mode"""
    folder = tmp_path / "game"
    folder.mkdir()
    shutil.copy(builds[OLD], folder / "halo")
    os.chmod(folder / "halo", 0o755)
    (folder / "SDL3-LICENSE.txt").write_bytes(b"old data\n")
    os.chmod(folder / "SDL3-LICENSE.txt", 0o600)
    return folder


def snapshot(folder):
    return {path.name: (hashlib.sha256(path.read_bytes()).hexdigest(), stat.S_IMODE(path.stat().st_mode))
            for path in folder.iterdir() if path.is_file()}


def run(tmp_path, folder, server, finished):
    """the old version, started as the player would; its log once a line
    has finished in it (the last program's "done"), or it times out"""
    log = tmp_path / "update.log"
    log.write_text("")
    home = tmp_path / "home"
    (home / "saves").mkdir(parents=True, exist_ok=True)
    environment = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home),
                   "HALO_SAVE_ROOT": str(home / "saves"), "HALO_UPDATE_TEST_SERVER": server.url,
                   "HALO_UPDATE_TEST_CA": str(server.authority), "HALO_UPDATE_TEST_LOG": str(log)}
    subprocess.run([str(folder / "halo")], cwd=folder, env=environment, timeout=120, capture_output=True)
    deadline = time.time() + 60
    while time.time() < deadline:
        lines = log.read_text().splitlines()
        if any(line.startswith(finished) and line.endswith(" done") for line in lines):
            return lines
        time.sleep(0.2)
    raise AssertionError("the update did not finish:\n" + log.read_text())


def has(lines, text):
    return any(text in line for line in lines)


def test_update_from_the_list(keys, builds, server, tmp_path):
    """(a) the release's signed updates.json names the zip: it is installed,
    and the new version started in the same file, halo"""
    folder = install(tmp_path, builds)
    release = tmp_path / "release"
    zip_path = release_zip(release, builds)
    update_sign.manifest(keys[0][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40, [zip_path])
    server.release(NEW, release, [ASSET, "updates.json", "updates.json.sig"])
    lines = run(tmp_path, folder, server, f"{NEW} halo")
    assert has(lines, f"{OLD} halo update: the release's list names {ASSET}")
    assert has(lines, f"{OLD} halo update: the download's SHA-256 is the release list's")
    assert has(lines, f"{OLD} halo update: starting version {NEW} (halo)")
    assert has(lines, f"{NEW} halo update: this is the latest version ({NEW})")
    assert not any(path.endswith(".sig") and ASSET in path for path in server.requests)
    after = snapshot(folder)
    assert after["halo"][0] == hashlib.sha256(builds[NEW].read_bytes()).hexdigest()
    # (a file that was there keeps its mode; the .old files are gone at the new start)
    assert after["halo"][1] == 0o755 and after["SDL3-LICENSE.txt"][1] == 0o600
    assert (folder / "SDL3-LICENSE.txt").read_bytes() == b"new data\n"
    assert not any(name.endswith(".old") for name in after) and not (folder / "update.partial").exists()


def test_update_without_a_list(keys, builds, server, tmp_path):
    """(b) a release from before the list: its zip and the zip's .sig"""
    folder = install(tmp_path, builds)
    release = tmp_path / "release"
    zip_path = release_zip(release, builds)
    update_sign.sign(keys[0][0], NEW, [zip_path])
    server.release(NEW, release, [ASSET, f"{ASSET}.sig"])
    lines = run(tmp_path, folder, server, f"{NEW} halo")
    assert has(lines, f"{OLD} halo update: no list of update files")
    assert has(lines, f"{OLD} halo update: the download's signature checks")
    assert has(lines, f"{NEW} halo update: this is the latest version ({NEW})")
    assert snapshot(folder)["halo"][0] == hashlib.sha256(builds[NEW].read_bytes()).hexdigest()


@pytest.mark.parametrize("change", ["zip", "list", "zip-without-list", "other-key"])
def test_changed_update_refused(keys, builds, server, tmp_path, change):
    """(c) a zip or a list changed after it was signed (or signed by another
    key) is not installed: the folder is as it was, and the old version runs"""
    folder = install(tmp_path, builds)
    before = snapshot(folder)
    release = tmp_path / "release"
    zip_path = release_zip(release, builds)
    if change == "zip-without-list":
        update_sign.sign(keys[0][0], NEW, [zip_path])
        names = [ASSET, f"{ASSET}.sig"]
    else:
        update_sign.manifest(keys[1 if change == "other-key" else 0][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40,
                             [zip_path])
        names = [ASSET, "updates.json", "updates.json.sig"]
    if change in ("zip", "zip-without-list"):
        data = bytearray(zip_path.read_bytes())
        data[len(data) // 2] ^= 0x01
        zip_path.write_bytes(bytes(data))
    elif change == "list":
        listed = release / "updates.json"
        listed.write_text(listed.read_text().replace(f'"size": {zip_path.stat().st_size}',
                                                     f'"size": {zip_path.stat().st_size + 1}'))
    server.release(NEW, release, names)
    lines = run(tmp_path, folder, server, f"{OLD} halo")
    assert has(lines, f"{OLD} halo update: failed: ")
    assert not has(lines, "starting version") and not any(line.startswith(NEW) for line in lines)
    if change in ("list", "other-key"):
        # (a list that is not the release's is no reason to take the zip without it)
        assert not any(path.startswith("/files/") and path.endswith(ASSET) for path in server.requests)
    after = snapshot(folder)
    assert after == before, (before, after)


def test_new_executable_started(keys, builds, server, tmp_path):
    """(d) a zip adds the game's executable by its later name (made on DOS:
    no mode in the zip), a tool marked executable in the zip and a text file:
    the two arrive executable, the text file not, and the list's main,
    chupathingy, is the program started"""
    folder = install(tmp_path, builds)
    release = tmp_path / "release"
    zip_path = release_zip(release, builds, [("chupathingy", builds[NEW].read_bytes(), None),
                                             ("helper-tool", b"#!/bin/sh\nexit 0\n", 0o755),
                                             ("notes-new.txt", b"notes\n", 0o644)])
    update_sign.manifest(keys[0][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40, [zip_path])
    assert json.loads((release / "updates.json").read_text())["files"][0]["main"] == "chupathingy"
    server.release(NEW, release, [ASSET, "updates.json", "updates.json.sig"])
    lines = run(tmp_path, folder, server, f"{NEW} chupathingy")
    assert has(lines, f"{OLD} halo update: starting version {NEW} (chupathingy)")
    assert has(lines, f"{NEW} chupathingy started")
    assert has(lines, f"{NEW} chupathingy update: this is the latest version ({NEW})")
    after = snapshot(folder)
    for name in ("chupathingy", "helper-tool"):
        assert after[name][1] & 0o111, (name, oct(after[name][1]))
    assert not after["notes-new.txt"][1] & 0o111
    assert after["halo"][1] == 0o755 and after["SDL3-LICENSE.txt"][1] == 0o600
