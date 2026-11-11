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
    arrives executable and is started (the list's "main"),
  - to the release that renames the game to chupathingy, with halo the
    stub that starts it (port/rename_stub/halo.sh), from the list or
    without one, the stub kept; and the renamed game, started beside an
    old halo that is a game, removes it,
  - never in a packaged install (a file named "packaged" beside it, or
    HALO_PACKAGED=1): the new version is only pointed out,
  - as an AppImage (APPIMAGE set, as its runtime sets it): the AppImage file
    itself replaced by the list's AppImage, checked, and started, with
    nothing unpacked beside it; never from a release without the list or an
    AppImage in it, a changed AppImage, or a packaged install,
  - on the testing and nightly channels (update.channel): from the channel's
    feed (testing/updates.json, signed by the prerelease key or the release
    key), the newest of the feeds and the latest release; never on the
    release channel, never from a feed whose list is a release's, another
    key's or changed, and never a release list signed by the prerelease key.

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
# (a test build's and a nightly's, between OLD and NEW: update_manifest.h's order)
TESTING, NIGHTLY = "0.8.1-testing.202611101200", "0.8.1-nightly.202611110400"
ASSET = "chupathingyce-linux-release.zip"
APPIMAGE = "chupathingyce-linux-release.AppImage"
REPOSITORY = "ChupathingyCE/chupathingyce"
MBEDTLS = ROOT / "port/third_party/mbedtls"
ZLIB_SOURCES = ("adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "uncompr.c", "zutil.c")
MONOCYPHER = ("monocypher.c", "monocypher-ed25519.c")


# (UPDATE_INSTALL_REQUIRED=1: what this needs missing fails the test, as in
# CI, instead of skipping it)
REQUIRED = os.environ.get("UPDATE_INSTALL_REQUIRED") == "1"


def unavailable(reason):
    if REQUIRED:
        pytest.fail(f"{reason} (UPDATE_INSTALL_REQUIRED=1)")
    pytest.skip(reason)


def abi():
    """the target's flags, SDL 3's and its library: the SDL 3 a Linux build
    made (HALO_UPDATE_TEST_SDL, its build folder: build/linux, whose
    sdl3-build is the 32-bit SDL tools/linux_build.py builds, as CI has it;
    build/linux64 for the 64-bit one), else the host's, else the i386 one"""
    built = os.environ.get("HALO_UPDATE_TEST_SDL")
    if built:
        folder = Path(built).resolve()
        library = folder / "sdl3-build" / "libSDL3.so.0"
        include = folder / "third_party" / "SDL3" / "include"
        if not library.is_file() or not include.is_dir():
            unavailable(f"no SDL 3 built in {folder} (its sdl3-build and third_party/SDL3)")
        # (compiled and linked as that build is, against its system root,
        # Debian 11's: the host has no 32-bit kernel headers. The program then
        # runs on the host's own 32-bit C library, as the release's does)
        sysroot = folder / "third_party" / "sysroot"
        if not sysroot.is_dir():
            unavailable(f"no system root in {folder}")
        target = (["--target=i686-linux-gnu", "-m32"] if folder.name == "linux" else ["--target=x86_64-linux-gnu"])
        target += [f"--sysroot={sysroot}"]
        # (and with the build's _GNU_SOURCE: that C library hides strcasestr
        # without it, as tools/linux_build.py's PLATFORM_FLAGS know)
        return target, [f"-isystem{include}", "-D_GNU_SOURCE"], ["-fuse-ld=lld", str(library),
                                                                f"-Wl,-rpath,{library.parent}"]
    found = subprocess.run(["pkg-config", "--exists", "sdl3"], capture_output=True).returncode == 0 \
        if shutil.which("pkg-config") else False
    if found:
        return [], [], ["-lSDL3"]
    if Path("/usr/lib/i386-linux-gnu/libSDL3.so").exists():
        return ["-m32"], [], ["-lSDL3"]
    unavailable("needs SDL 3's development files")


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
        unavailable("needs OpenSSL 3's openssl")
    directory = tmp_path_factory.mktemp("update_key")
    # (the release key, another, and the prerelease key: never real ones)
    return [(directory / f"{name}.pem", delta_table.keygen(directory / f"{name}.pem"))
            for name in ("key", "other", "prerelease")]


@pytest.fixture(scope="module")
def builds(keys, tmp_path_factory):
    """{version: the update test's program}"""
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc:
        unavailable("no C compiler")
    target, sdl_flags, sdl_libraries = abi()
    directory = tmp_path_factory.mktemp("update_install")
    library = compile_objects(cc, [*target, "-std=gnu11", "-O1", "-w", f"-I{MBEDTLS / 'include'}",
                                   f"-I{MBEDTLS / 'library'}"], sorted((MBEDTLS / "library").glob("*.c")), directory)
    library += compile_objects(cc, [*target, "-std=gnu11", "-O1", "-w", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg"],
                               [ROOT / "port/third_party/zlib" / name for name in ZLIB_SOURCES], directory)
    library += compile_objects(cc, [*target, "-std=gnu11", "-O1", "-w"],
                               [ROOT / "port/third_party/monocypher" / name for name in MONOCYPHER], directory)
    key = ",".join(f"0x{byte:02x}" for byte in keys[0][1])
    prerelease_key = ",".join(f"0x{byte:02x}" for byte in keys[2][1])
    programs = {}
    for version in (OLD, NEW, TESTING, NIGHTLY):
        program = directory / f"update-{version}"
        built = subprocess.run(
            [cc, *target, *sdl_flags, "-std=gnu11", "-O1", "-Wall", "-Werror", "-Wno-unused-function",
             f'-DHALO_VERSION="{version}"',
             "-DHALO_UPDATE_TEST", f"-DHALO_UPDATE_TEST_KEY={key}",
             f"-DHALO_UPDATE_TEST_PRERELEASE_KEY={prerelease_key}", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg",
             "-Iport/linux/src", "-Iport/third_party/zlib", "-Iport/third_party/monocypher",
             f"-I{MBEDTLS / 'include'}", "-o", str(program), "tools/update_install_check.c",
             "port/linux/src/posix_update.c", "port/linux/src/update_manifest.c",
             "port/linux/src/update_signature.c", *library, *sdl_libraries, "-lpthread"],
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

    def add(self, tag, directory, names):
        """more files (names, in directory) at tag, beside the release's: a
        feed's list at testing or nightly, its builds at their pre-release"""
        self.files.update({f"{tag}/{name}": (directory / name).read_bytes() for name in names})


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


def release_zip(directory, builds, extra=(), version=NEW):
    """the new version's zip: its halo, a library, the extra (name, data, mode)"""
    directory.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(directory / ASSET, "w") as archive:
        add(archive, "halo", builds[version].read_bytes(), 0o755)
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


def run(tmp_path, folder, server, finished, extra_environment=None):
    """the old version, started as the player would; its log once a line
    has finished in it (the last program's "done"), or it times out"""
    log = tmp_path / "update.log"
    log.write_text("")
    home = tmp_path / "home"
    (home / "saves").mkdir(parents=True, exist_ok=True)
    environment = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home),
                   "HALO_SAVE_ROOT": str(home / "saves"), "HALO_UPDATE_TEST_SERVER": server.url,
                   "HALO_UPDATE_TEST_CA": str(server.authority), "HALO_UPDATE_TEST_LOG": str(log),
                   **(extra_environment or {})}
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
    assert after["SDL3-LICENSE.txt"][1] == 0o600
    # (the zip's halo is a game, not the stub: the renamed one removes it)
    assert has(lines, f"{NEW} chupathingy update: removing halo, the game from before its rename to chupathingy")
    assert "halo" not in after


STUB = (ROOT / "port/rename_stub/halo.sh").read_bytes()


def rename_zip(directory, builds):
    """the release of the rename: chupathingy, and halo, the stub that
    starts it (port/rename_stub/halo.sh), as the artifacts give them: no mode"""
    directory.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(directory / ASSET, "w") as archive:
        add(archive, "chupathingy", builds[NEW].read_bytes())
        add(archive, "halo", STUB)
        add(archive, "SDL3-LICENSE.txt", b"new data\n", 0o644)
    return directory / ASSET


@pytest.mark.parametrize("listed", [True, False])
def test_update_to_the_renamed_game(keys, builds, server, tmp_path, listed):
    """(f) the update to the release that renames the game: from the list,
    the old updater starts chupathingy (the list's main); from a release
    without the list (as an updater from before 0.8.0 does), it starts halo
    again, which is now the stub, and the stub starts chupathingy.
    Either way the stub stays, executable, and starting halo afterwards
    still starts the game"""
    folder = install(tmp_path, builds)
    release = tmp_path / "release"
    zip_path = rename_zip(release, builds)
    if listed:
        update_sign.manifest(keys[0][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40, [zip_path])
        assert json.loads((release / "updates.json").read_text())["files"][0]["main"] == "chupathingy"
        server.release(NEW, release, [ASSET, "updates.json", "updates.json.sig"])
    else:
        update_sign.sign(keys[0][0], NEW, [zip_path])
        server.release(NEW, release, [ASSET, f"{ASSET}.sig"])
    lines = run(tmp_path, folder, server, f"{NEW} chupathingy")
    assert has(lines, f"{OLD} halo update: starting version {NEW} ({'chupathingy' if listed else 'halo'})")
    assert has(lines, f"{NEW} chupathingy update: this is the latest version ({NEW})")
    assert not has(lines, "removing halo")
    after = snapshot(folder)
    assert after["chupathingy"][0] == hashlib.sha256(builds[NEW].read_bytes()).hexdigest()
    assert after["chupathingy"][1] & 0o111 and after["halo"][1] & 0o111
    assert (folder / "halo").read_bytes() == STUB
    assert not any(name.endswith(".old") for name in after)
    # (a shortcut to halo, later)
    lines = run(tmp_path, folder, server, f"{NEW} chupathingy")
    assert has(lines, f"{NEW} chupathingy update: this is the latest version ({NEW})")
    assert (folder / "halo").read_bytes() == STUB


def test_old_game_removed_beside_the_renamed_one(builds, server, tmp_path):
    """(g) chupathingy started beside an old halo (a game, no stub's
    mark) removes it; one started by another name removes nothing"""
    folder = tmp_path / "game"
    folder.mkdir()
    shutil.copy(builds[NEW], folder / "chupathingy")
    shutil.copy(builds[OLD], folder / "halo")
    shutil.copy(builds[NEW], folder / "my-copy")
    for name in ("chupathingy", "halo", "my-copy"):
        os.chmod(folder / name, 0o755)
    server.release(NEW, tmp_path / "empty", [])
    log = tmp_path / "update.log"
    log.write_text("")
    environment = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(tmp_path),
                   "HALO_SAVE_ROOT": str(tmp_path / "saves"), "HALO_UPDATE_TEST_SERVER": server.url,
                   "HALO_UPDATE_TEST_CA": str(server.authority), "HALO_UPDATE_TEST_LOG": str(log)}
    (tmp_path / "saves").mkdir()
    subprocess.run([str(folder / "my-copy")], cwd=folder, env=environment, timeout=120, capture_output=True)
    assert (folder / "halo").exists()
    subprocess.run([str(folder / "chupathingy")], cwd=folder, env=environment, timeout=120, capture_output=True)
    assert not (folder / "halo").exists()
    assert has(log.read_text().splitlines(), "removing halo, the game from before its rename to chupathingy")


@pytest.mark.parametrize("mark", ["file", "environment"])
def test_packaged_install_not_updated(keys, builds, server, tmp_path, mark):
    """(e) a packaged install (packaged_install.h) is its package manager's to
    update: the newer release is found and pointed out, and nothing is
    downloaded or changed, though the test answers yes"""
    folder = install(tmp_path, builds)
    environment = {}
    if mark == "file":
        (folder / "packaged").write_text("manager = \"pacman\"\n")
    else:
        environment["HALO_PACKAGED"] = "1"
    before = snapshot(folder)
    release = tmp_path / "release"
    zip_path = release_zip(release, builds)
    update_sign.manifest(keys[0][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40, [zip_path])
    server.release(NEW, release, [ASSET, "updates.json", "updates.json.sig"])
    lines = run(tmp_path, folder, server, f"{OLD} halo", environment)
    assert has(lines, f"{OLD} halo update: version {NEW} is available")
    assert has(lines, "update Chupathingy with your package manager")
    assert not has(lines, "downloading") and not any(line.startswith(NEW) for line in lines)
    assert not any(path.startswith("/files/") or path.endswith("updates.json") for path in server.requests)
    assert snapshot(folder) == before and not (folder / "update.partial").exists()


def install_appimage(tmp_path, builds):
    """the old version as an AppImage the player downloaded and renamed, in a
    folder of other files of theirs"""
    folder = tmp_path / "Applications"
    folder.mkdir()
    appimage = folder / "Chupathingy.AppImage"
    shutil.copy(builds[OLD], appimage)
    os.chmod(appimage, 0o755)
    (folder / "notes.txt").write_bytes(b"the player's\n")
    return folder, appimage


def appimage_release(tmp_path, builds, keys, names=(ASSET, APPIMAGE), listed=(ASSET, APPIMAGE)):
    """the new version's zip and AppImage, the listed ones in its signed list"""
    release = tmp_path / "release"
    release_zip(release, builds)
    shutil.copy(builds[NEW], release / APPIMAGE)
    if listed:
        update_sign.manifest(keys[0][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40, [release / name for name in listed])
    return release


def run_appimage(tmp_path, appimage, server, finished, extra_environment=None):
    """as run, started as the AppImage runtime starts the game: APPIMAGE, and
    the library path an AppRun gives it"""
    environment = {"APPIMAGE": str(appimage), "LD_LIBRARY_PATH": "/tmp/.mount_old/usr/lib:/opt/host/lib",
                   "CHUPATHINGYCE_HOST_LD_LIBRARY_PATH": "/opt/host/lib", **(extra_environment or {})}
    log = tmp_path / "update.log"
    log.write_text("")
    home = tmp_path / "home"
    (home / "saves").mkdir(parents=True, exist_ok=True)
    environment = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home),
                   "HALO_SAVE_ROOT": str(home / "saves"), "HALO_UPDATE_TEST_SERVER": server.url,
                   "HALO_UPDATE_TEST_CA": str(server.authority), "HALO_UPDATE_TEST_LOG": str(log), **environment}
    subprocess.run([str(appimage)], cwd=tmp_path, env=environment, timeout=120, capture_output=True)
    deadline = time.time() + 60
    while time.time() < deadline:
        lines = log.read_text().splitlines()
        if any(line.startswith(finished) and line.endswith(" done") for line in lines):
            return lines
        time.sleep(0.2)
    raise AssertionError("the update did not finish:\n" + log.read_text())


def test_appimage_updated(keys, builds, server, tmp_path):
    """(f) an AppImage: the list's AppImage replaces the AppImage file, by
    its own name, executable; the zip is never downloaded or unpacked, the
    folder's other files are left alone, and the new one starts with the
    library path its AppRun was given"""
    folder, appimage = install_appimage(tmp_path, builds)
    release = appimage_release(tmp_path, builds, keys)
    server.release(NEW, release, [ASSET, APPIMAGE, "updates.json", "updates.json.sig"])
    name = appimage.name
    lines = run_appimage(tmp_path, appimage, server, f"{NEW} {name}")
    assert has(lines, f"{OLD} {name} update: running as an AppImage ({appimage})")
    assert has(lines, f"{OLD} {name} update: the release's list names {APPIMAGE}")
    assert has(lines, f"{OLD} {name} update: the download's SHA-256 is the release list's")
    assert has(lines, f"{OLD} {name} update: starting version {NEW} ({name})")
    assert has(lines, f"{NEW} {name} library path: /opt/host/lib")
    assert has(lines, f"{NEW} {name} update: this is the latest version ({NEW})")
    assert not any(ASSET in path for path in server.requests)
    after = snapshot(folder)
    assert after[name][0] == hashlib.sha256(builds[NEW].read_bytes()).hexdigest()
    assert after[name][1] == 0o755
    # (nothing else: no zip's files, no .old once the new one started)
    assert set(after) == {name, "notes.txt"}, after
    assert not (folder / "update.partial").exists()


@pytest.mark.parametrize("change", ["no-list", "no-appimage-listed", "appimage", "other-key"])
def test_appimage_refused(keys, builds, server, tmp_path, change):
    """(g) an AppImage is not updated from a release without the list (its
    zip is no AppImage), from a list without an AppImage, from a changed
    AppImage or a list signed by another key: the file is as it was"""
    folder, appimage = install_appimage(tmp_path, builds)
    before = snapshot(folder)
    if change == "no-list":
        release = appimage_release(tmp_path, builds, keys, listed=())
        update_sign.sign(keys[0][0], NEW, [release / ASSET, release / APPIMAGE])
        names = [ASSET, f"{ASSET}.sig", APPIMAGE, f"{APPIMAGE}.sig"]
    elif change == "no-appimage-listed":
        release = appimage_release(tmp_path, builds, keys, listed=(ASSET,))
        names = [ASSET, APPIMAGE, "updates.json", "updates.json.sig"]
    else:
        release = appimage_release(tmp_path, builds, keys)
        if change == "other-key":
            update_sign.manifest(keys[1][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40,
                                 [release / ASSET, release / APPIMAGE])
        else:
            data = bytearray((release / APPIMAGE).read_bytes())
            data[len(data) // 2] ^= 0x01
            (release / APPIMAGE).write_bytes(bytes(data))
        names = [ASSET, APPIMAGE, "updates.json", "updates.json.sig"]
    server.release(NEW, release, names)
    name = appimage.name
    lines = run_appimage(tmp_path, appimage, server, f"{OLD} {name}")
    assert has(lines, f"{OLD} {name} update: failed: ")
    if change in ("no-list", "no-appimage-listed"):
        assert has(lines, "download the new AppImage")
    assert not has(lines, "starting version") and not any(line.startswith(NEW) for line in lines)
    assert not any(path.startswith("/files/") and path.endswith(ASSET) for path in server.requests)
    if change != "appimage":
        assert not any(path.startswith("/files/") and path.endswith(APPIMAGE) for path in server.requests)
    assert snapshot(folder) == before
    assert not (folder / "update.partial").exists()


def test_packaged_appimage_not_updated(keys, builds, server, tmp_path):
    """(h) a packaged install that runs as an AppImage (HALO_PACKAGED=1) is
    still its package manager's: nothing downloaded or changed"""
    folder, appimage = install_appimage(tmp_path, builds)
    before = snapshot(folder)
    release = appimage_release(tmp_path, builds, keys)
    server.release(NEW, release, [ASSET, APPIMAGE, "updates.json", "updates.json.sig"])
    name = appimage.name
    lines = run_appimage(tmp_path, appimage, server, f"{OLD} {name}", {"HALO_PACKAGED": "1"})
    assert has(lines, f"{OLD} {name} update: version {NEW} is available")
    assert has(lines, "update Chupathingy with your package manager")
    assert not has(lines, "downloading") and not any(line.startswith(NEW) for line in lines)
    assert not any(path.startswith("/files/") or path.endswith("updates.json") for path in server.requests)
    assert snapshot(folder) == before


# ---------- the update channels (update.channel: update_manifest.h)


def publish_feed(keys, builds, server, tmp_path, channel, version, key_index=2, listed_version=None):
    """a channel's feed: its build's zip on its pre-release (v0.8.1-testing,
    nightly), and the list naming it at <channel>/updates.json"""
    tag = "nightly" if channel == "nightly" else "v0.8.1-testing"
    directory = tmp_path / f"feed-{channel}"
    zip_path = release_zip(directory, builds, version=version)
    update_sign.manifest(keys[key_index][0], listed_version or version, tag, f"{tag}-debug", "0" * 40, [zip_path],
                         channel)
    server.add(tag, directory, [ASSET])
    server.add(channel, directory, ["updates.json", "updates.json.sig"])


def the_latest_release(server, tmp_path, builds, version=OLD):
    """the latest release, the installed one's (nothing newer there)"""
    directory = tmp_path / "latest"
    release_zip(directory, builds, version=version)
    server.latest = version
    server.release(version, directory, [ASSET])


def test_testing_channel_update(keys, builds, server, tmp_path):
    """Testing: the testing feed's build is newer than the latest release, so
    it is installed from the feed's list, signed by the prerelease key"""
    folder = install(tmp_path, builds)
    the_latest_release(server, tmp_path, builds)
    publish_feed(keys, builds, server, tmp_path, "testing", TESTING)
    lines = run(tmp_path, folder, server, f"{TESTING} halo", {"HALO_UPDATE_TEST_CHANNEL": "testing"})
    assert has(lines, f"{OLD} halo update: version {TESTING} is available (this is {OLD}; the testing channel)")
    assert has(lines, f"{OLD} halo update: the testing channel's list names {ASSET} (v0.8.1-testing")
    assert has(lines, f"{OLD} halo update: the download's SHA-256 is the release list's")
    assert has(lines, f"{TESTING} halo update: this is the latest version ({TESTING})")
    assert snapshot(folder)["halo"][0] == hashlib.sha256(builds[TESTING].read_bytes()).hexdigest()


def test_release_channel_never_reads_a_feed(keys, builds, server, tmp_path):
    """Release (the default): the feeds are never asked for"""
    folder = install(tmp_path, builds)
    before = snapshot(folder)
    the_latest_release(server, tmp_path, builds)
    publish_feed(keys, builds, server, tmp_path, "testing", TESTING)
    lines = run(tmp_path, folder, server, f"{OLD} halo")
    assert has(lines, f"{OLD} halo update: this is the latest version ({OLD})")
    assert not any(path.startswith(f"/{REPOSITORY}/releases/download/testing/") for path in server.requests)
    assert snapshot(folder) == before


def test_nightly_channel_takes_the_newest(keys, builds, server, tmp_path):
    """Nightly: the nightly and testing feeds and the latest release, the
    newest of them (here the nightly, built after the test build)"""
    folder = install(tmp_path, builds)
    the_latest_release(server, tmp_path, builds)
    publish_feed(keys, builds, server, tmp_path, "testing", TESTING)
    publish_feed(keys, builds, server, tmp_path, "nightly", NIGHTLY)
    lines = run(tmp_path, folder, server, f"{NIGHTLY} halo", {"HALO_UPDATE_TEST_CHANNEL": "nightly"})
    assert has(lines, f"{OLD} halo update: version {NIGHTLY} is available (this is {OLD}; the nightly channel)")
    assert snapshot(folder)["halo"][0] == hashlib.sha256(builds[NIGHTLY].read_bytes()).hexdigest()


def test_a_release_beats_its_test_builds(keys, builds, server, tmp_path):
    """Testing, with the release out: the release is newer than every test
    build of its version, and comes from the release's own list"""
    folder = install(tmp_path, builds)
    release = tmp_path / "release"
    zip_path = release_zip(release, builds)
    update_sign.manifest(keys[0][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40, [zip_path], "release")
    server.release(NEW, release, [ASSET, "updates.json", "updates.json.sig"])
    server.latest = NEW
    publish_feed(keys, builds, server, tmp_path, "testing", TESTING)
    lines = run(tmp_path, folder, server, f"{NEW} halo", {"HALO_UPDATE_TEST_CHANNEL": "testing"})
    assert has(lines, f"{OLD} halo update: version {NEW} is available (this is {OLD}; the latest release)")
    assert snapshot(folder)["halo"][0] == hashlib.sha256(builds[NEW].read_bytes()).hexdigest()


@pytest.mark.parametrize("change", ["release-version", "other-key", "changed-list", "no-signature"])
def test_feed_refused(keys, builds, server, tmp_path, change):
    """a feed that is not to be trusted is passed over: the folder is as it
    was. A list whose version is a release's (the prerelease key may not
    sign one), another key's, a changed one, or one without a signature"""
    folder = install(tmp_path, builds)
    before = snapshot(folder)
    the_latest_release(server, tmp_path, builds)
    publish_feed(keys, builds, server, tmp_path, "testing", TESTING, key_index=1 if change == "other-key" else 2,
                 listed_version=NEW if change == "release-version" else None)
    if change == "release-version":
        # (the list's version is a release's, so its channel's check refuses it)
        listed = tmp_path / "feed-testing" / "updates.json"
        assert f'"version": "{NEW}"' in listed.read_text()
    if change == "changed-list":
        listed = tmp_path / "feed-testing" / "updates.json"
        listed.write_text(listed.read_text().replace('"size": ', '"size": 1'))
        server.add("testing", tmp_path / "feed-testing", ["updates.json"])
    if change == "no-signature":
        del server.files["testing/updates.json.sig"]
    lines = run(tmp_path, folder, server, f"{OLD} halo", {"HALO_UPDATE_TEST_CHANNEL": "testing"})
    assert has(lines, f"{OLD} halo update: the testing channel's list")
    assert has(lines, f"{OLD} halo update: this is the latest version ({OLD})")
    assert not any(path.startswith("/files/v0.8.1-testing/") for path in server.requests)
    assert snapshot(folder) == before


def test_release_list_signed_by_the_prerelease_key_refused(keys, builds, server, tmp_path):
    """a release's list signed by the prerelease key is not used, on any
    channel: a leaked prerelease key never makes a release"""
    folder = install(tmp_path, builds)
    before = snapshot(folder)
    release = tmp_path / "release"
    zip_path = release_zip(release, builds)
    update_sign.manifest(keys[2][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40, [zip_path], "release")
    server.release(NEW, release, [ASSET, "updates.json", "updates.json.sig"])
    server.latest = NEW
    lines = run(tmp_path, folder, server, f"{OLD} halo", {"HALO_UPDATE_TEST_CHANNEL": "testing"})
    assert has(lines, f"{OLD} halo update: failed: the release's list of update files was not used")
    assert snapshot(folder) == before


def test_latest_release_tag_of_a_prerelease_refused(keys, builds, server, tmp_path):
    """a test build marked latest by mistake (or by anyone) is never taken on
    the release path: not from its list, not from its zip's own .sig"""
    folder = install(tmp_path, builds)
    before = snapshot(folder)
    release = tmp_path / "release"
    zip_path = release_zip(release, builds, version=TESTING)
    update_sign.sign(keys[0][0], TESTING, [zip_path])
    update_sign.manifest(keys[0][0], TESTING, f"v{TESTING}", f"v{TESTING}-debug", "0" * 40, [zip_path], "release")
    server.release(TESTING, release, [ASSET, f"{ASSET}.sig", "updates.json", "updates.json.sig"])
    server.latest = TESTING
    lines = run(tmp_path, folder, server, f"{OLD} halo")
    assert has(lines, f"{OLD} halo update: the latest release's tag v{TESTING} is not a release's version")
    assert not any(path.startswith("/files/") for path in server.requests)
    assert snapshot(folder) == before


# ---------- the Steam Deck's Game Mode (gamescope): no dialog or window, the
# version line says where to update, the game's own pages install it, and
# the new version is the same process (Steam and gamescope keep the game)

GAMESCOPE = {"GAMESCOPE_WAYLAND_DISPLAY": "gamescope-0"}
PAGE = {"HALO_UPDATE_TEST_PAGE": "1"}
# (no answer: the question, which under gamescope is only the version line)
NO_ANSWER = {"HALO_UPDATE_TEST_ANSWER": ""}


def pids(lines, version, name):
    """the process ids the version's runs logged (update_install_check.c's
    "pid <n>, parent <n>, group <n>")"""
    found = []
    for line in lines:
        prefix = f"{version} {name} pid "
        if line.startswith(prefix):
            found.append(tuple(int(part.split()[-1]) for part in line[len(f"{version} {name} "):].split(", ")))
    return found


def shown_nothing(lines):
    """no dialog and no window of the updater's"""
    return not any(" dialog: " in line or " window: " in line for line in lines)


def test_where_to_update_under_gamescope():
    """the version line's words under gamescope name the page that has
    INSTALL UPDATE (tools/port_settings.py), in both menu styles"""
    import port_settings
    header = (ROOT / "port/linux/src/update.h").read_text()
    assert '#define UPDATER_GAMESCOPE_WHERE "in Settings > Online > Updates"' in header
    assert '"from Desktop Mode"' not in (ROOT / "port/linux/src/updater.c").read_text()
    assert "build_identity_watermark_update(updater_latest_version, UPDATER_GAMESCOPE_WHERE)" in \
        (ROOT / "port/linux/src/updater.c").read_text()
    views = {view: rows for view, _, _, rows in port_settings.SCREENS["network_setup"]["views"]}
    assert port_settings.INSTALL_UPDATE in views["updates"]
    assert port_settings.SCREENS["network_setup"]["header"][0] == "header_profile_network_settings"


def test_install_row_wiring():
    """INSTALL UPDATE: in UPDATES' list in the Gearbox menus and the
    Chupathingy menus' page, as tools/port_settings.py writes them; A runs
    "port update install" (the Chupathingy menus' A, "port settings accept",
    installs on it too), which menu_functions.c handles; its help line is the
    screen's own text, so the install's progress and errors can be shown"""
    import port_settings
    base = "main_menu/settings_select/player_setup/player_profile_edit/network_setup"
    gearbox_name = f"{base}".replace("/", ".") + ".xml"
    gearbox = (ROOT / "port/assets/menus/ce" / gearbox_name).read_text()
    pages = (ROOT / "port/assets/menus/chupathingy/settings_pages.xml").read_text()
    # (the files are port_settings.py's, as written)
    assert gearbox == "\n".join(port_settings.settings_files()[gearbox_name])
    assert pages == "\n".join(port_settings.chupathingy_settings_pages())
    row = f'<child widget="{base}/op_install_update" x="54" y="133" platform="desktop"/>'
    for text in (gearbox, pages):
        updates = text.index(f'<child widget="{base}/view_updates"')
        assert text.index(row) > updates
        assert text.index(row) < text.index(f'<child widget="{base}/view_chat"')
    assert f'<widget name="{base}/op_install_update"' in gearbox
    assert '<on event="a" run="port update install"/>' in gearbox
    assert f'<widget name="{base}/help" type="text" controller="1" left="68" top="376" width="482" height="60" ' \
           'font="ui\\large_ui"' in gearbox
    assert '<string text="INSTALL UPDATE:"/>' in gearbox
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    assert '"port update install",' in (ROOT / "port/linux/game/menu_tags.c").read_text()
    assert 'else if (!strcmp(name, "port update install"))' in functions
    assert '!strcmp(list->focused_child->name, "op_install_update")' in functions
    assert "updater_install_begin()" in functions and "updater_ready(" in functions
    launcher = (ROOT / "port/linux/src/launcher.c").read_text()
    assert "updater_install_begin()" in launcher and "updater_install_status(" in launcher
    assert "updater_install()" not in launcher


def test_no_question_under_gamescope(keys, builds, server, tmp_path):
    """under gamescope nothing is asked in a dialog: the version line says
    where to update, and nothing is downloaded"""
    folder, appimage = install_appimage(tmp_path, builds)
    before = snapshot(folder)
    release = appimage_release(tmp_path, builds, keys)
    server.release(NEW, release, [ASSET, APPIMAGE, "updates.json", "updates.json.sig"])
    name = appimage.name
    lines = run_appimage(tmp_path, appimage, server, f"{OLD} {name}", {**GAMESCOPE, **NO_ANSWER})
    assert has(lines, f"{OLD} {name} update: {NEW} is out (this is {OLD}); not asking under gamescope")
    assert has(lines, f"{OLD} {name} watermark: {OLD} - {NEW} is out: update in Settings > Online > Updates")
    assert shown_nothing(lines)
    assert not has(lines, "downloading") and snapshot(folder) == before


def test_failure_shown_in_no_dialog_under_gamescope(keys, builds, server, tmp_path):
    """a failed update under gamescope opens no dialog and no progress
    window (each would crash the game there); on the desktop it does"""
    for environment, dialog in ((GAMESCOPE, False), ({}, True)):
        where = tmp_path / ("gamescope" if environment else "desktop")
        where.mkdir()
        folder, appimage = install_appimage(where, builds)
        before = snapshot(folder)
        release = appimage_release(where, builds, keys)
        data = bytearray((release / APPIMAGE).read_bytes())
        data[len(data) // 2] ^= 0x01
        (release / APPIMAGE).write_bytes(bytes(data))
        server.release(NEW, release, [ASSET, APPIMAGE, "updates.json", "updates.json.sig"])
        name = appimage.name
        lines = run_appimage(where, appimage, server, f"{OLD} {name}", environment)
        assert has(lines, f"{OLD} {name} update: failed: the download was not installed")
        assert (not shown_nothing(lines)) == dialog, lines
        if not dialog:
            assert has(lines, "not showing the failure in a dialog under gamescope")
        assert snapshot(folder) == before


def test_page_install_under_gamescope(keys, builds, server, tmp_path):
    """from end to end in Game Mode: the AppImage, under gamescope, installs
    the new version from the game's page (INSTALL UPDATE, the setup screen's
    UPDATE) with no dialog or window, the page showing the download; the new
    AppImage takes the old one's name, executable, the old one kept as
    <name>.old until the new one starts; and the new version is the same
    process, with the same parent and group, started with the library path
    its AppRun was given"""
    folder, appimage = install_appimage(tmp_path, builds)
    release = appimage_release(tmp_path, builds, keys)
    server.release(NEW, release, [ASSET, APPIMAGE, "updates.json", "updates.json.sig"])
    name = appimage.name
    lines = run_appimage(tmp_path, appimage, server, f"{NEW} {name}", {**GAMESCOPE, **NO_ANSWER, **PAGE})
    assert has(lines, f"{OLD} {name} watermark: {OLD} - {NEW} is out: update in Settings > Online > Updates")
    assert has(lines, f"{OLD} {name} page: installing")
    assert has(lines, f"{OLD} {name} update: installing version {NEW} from the game's own page (under gamescope")
    assert has(lines, f"{OLD} {name} page: Downloading {NEW}")
    assert has(lines, f"{OLD} {name} update: the download's SHA-256 is the release list's")
    assert has(lines, f"{OLD} {name} update: starting version {NEW} ({name})")
    assert has(lines, f"{NEW} {name} library path: /opt/host/lib")
    assert has(lines, f"{NEW} {name} update: this is the latest version ({NEW})")
    assert shown_nothing(lines), lines
    old, new = pids(lines, OLD, name), pids(lines, NEW, name)
    assert len(old) == 1 and len(new) == 1 and old == new, (old, new)
    after = snapshot(folder)
    assert after[name] == (hashlib.sha256(builds[NEW].read_bytes()).hexdigest(), 0o755)
    assert set(after) == {name, "notes.txt"}, after


def test_page_install_of_a_folder(keys, builds, server, tmp_path):
    """the zip's install from a page (a folder, not an AppImage), on the
    desktop: the same process too"""
    folder = install(tmp_path, builds)
    release = tmp_path / "release"
    zip_path = release_zip(release, builds)
    update_sign.manifest(keys[0][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40, [zip_path])
    server.release(NEW, release, [ASSET, "updates.json", "updates.json.sig"])
    lines = run(tmp_path, folder, server, f"{NEW} halo", {**NO_ANSWER, **PAGE})
    assert has(lines, f"{OLD} halo page: installing")
    assert has(lines, f"{OLD} halo update: starting version {NEW} (halo)")
    assert has(lines, f"{NEW} halo update: this is the latest version ({NEW})")
    # (the page's install opens no window of its own, on the desktop too)
    assert not any(" window: " in line for line in lines)
    assert pids(lines, OLD, "halo") == pids(lines, NEW, "halo")
    assert snapshot(folder)["halo"][0] == hashlib.sha256(builds[NEW].read_bytes()).hexdigest()


def test_page_install_failure_under_gamescope(keys, builds, server, tmp_path):
    """a page's install that fails under gamescope says why on the page,
    and the game goes on as it was"""
    folder, appimage = install_appimage(tmp_path, builds)
    before = snapshot(folder)
    release = appimage_release(tmp_path, builds, keys, listed=(ASSET,))
    server.release(NEW, release, [ASSET, APPIMAGE, "updates.json", "updates.json.sig"])
    name = appimage.name
    lines = run_appimage(tmp_path, appimage, server, f"{OLD} {name}", {**GAMESCOPE, **NO_ANSWER, **PAGE})
    assert has(lines, f"{OLD} {name} page: The update failed: ")
    assert has(lines, "download the new AppImage")
    assert shown_nothing(lines) and not any(line.startswith(NEW) for line in lines)
    assert snapshot(folder) == before


def test_page_install_when_the_new_version_cannot_start(keys, builds, server, tmp_path):
    """a new version in place that cannot be started (here, not executable:
    a noexec folder is the same to the check) leaves the old game running,
    and the page says to start it again, rather than the game quitting with
    nothing to start"""
    folder, appimage = install_appimage(tmp_path, builds)
    release = appimage_release(tmp_path, builds, keys)
    server.release(NEW, release, [ASSET, APPIMAGE, "updates.json", "updates.json.sig"])
    name = appimage.name
    # (the AppImage's own mode is the new one's: run through a copy, so the
    # file replaced is one with no execute bits)
    os.chmod(appimage, 0o644)
    runner = tmp_path / "runner"
    shutil.copy(builds[OLD], runner)
    os.chmod(runner, 0o755)
    log = tmp_path / "update.log"
    log.write_text("")
    home = tmp_path / "home"
    (home / "saves").mkdir(parents=True, exist_ok=True)
    environment = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home),
                   "HALO_SAVE_ROOT": str(home / "saves"), "HALO_UPDATE_TEST_SERVER": server.url,
                   "HALO_UPDATE_TEST_CA": str(server.authority), "HALO_UPDATE_TEST_LOG": str(log),
                   "APPIMAGE": str(appimage), **GAMESCOPE, **NO_ANSWER, **PAGE}
    subprocess.run([str(runner)], cwd=tmp_path, env=environment, timeout=120, capture_output=True)
    lines = log.read_text().splitlines()
    assert has(lines, f"{OLD} runner page: The update failed: the new version is in place, but it isn't "
                      "executable: start it again.")
    assert has(lines, f"{OLD} runner done") and not any(line.startswith(NEW) for line in lines)
    assert shown_nothing(lines)


def test_page_install_waits_for_the_game(keys, builds, server, tmp_path):
    """an install that is ready while a game is in progress waits: the files
    are put in place and the game restarts only once the player leaves the
    game (a host's game would end for everyone), the page and the version
    line saying so meanwhile"""
    folder, appimage = install_appimage(tmp_path, builds)
    release = appimage_release(tmp_path, builds, keys)
    server.release(NEW, release, [ASSET, APPIMAGE, "updates.json", "updates.json.sig"])
    name = appimage.name
    lines = run_appimage(tmp_path, appimage, server, f"{NEW} {name}",
                         {**GAMESCOPE, **NO_ANSWER, **PAGE, "HALO_UPDATE_TEST_IN_GAME": "1"})
    def at(text):
        return next(index for index, line in enumerate(lines) if text in line)
    waiting = at(f"{OLD} {name} page: {NEW} is ready. Chupathingy restarts when you leave the game.")
    assert has(lines, f"{OLD} {name} watermark: {OLD} - the update restarts Chupathingy when you leave the game")
    assert at(f"{OLD} {name} update: version {NEW} is ready; it is put in place when the game in progress ends") \
        < waiting < at(f"{OLD} {name} left the game") < at(f"{OLD} {name} update: starting version {NEW}")
    assert has(lines, f"{NEW} {name} update: this is the latest version ({NEW})")
    assert shown_nothing(lines)
    assert pids(lines, OLD, name) == pids(lines, NEW, name)


def test_restart_when_an_exit_handler_crashes(keys, builds, server, tmp_path):
    """the game becomes the new version in its last exit handler; if a
    handler of the game's kills it first, the watcher it left starts the new
    version (a process of its own then)"""
    folder = install(tmp_path, builds)
    release = tmp_path / "release"
    zip_path = release_zip(release, builds)
    update_sign.manifest(keys[0][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40, [zip_path])
    server.release(NEW, release, [ASSET, "updates.json", "updates.json.sig"])
    lines = run(tmp_path, folder, server, f"{NEW} halo", {"HALO_UPDATE_TEST_CRASH_AT_EXIT": "1"})
    assert has(lines, f"{OLD} halo update: restarting as version {NEW} in this same process")
    assert has(lines, f"{OLD} halo crashing at exit")
    assert has(lines, f"{NEW} halo update: this is the latest version ({NEW})")
    old, new = pids(lines, OLD, "halo"), pids(lines, NEW, "halo")
    assert len(old) == 1 and len(new) == 1 and old[0][0] != new[0][0]


def test_restart_keeps_the_arguments(keys, builds, server, tmp_path):
    """the new version is started with the arguments the old one had"""
    folder = install(tmp_path, builds)
    release = tmp_path / "release"
    zip_path = release_zip(release, builds)
    update_sign.manifest(keys[0][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40, [zip_path])
    server.release(NEW, release, [ASSET, "updates.json", "updates.json.sig"])
    log = tmp_path / "update.log"
    log.write_text("")
    home = tmp_path / "home"
    (home / "saves").mkdir(parents=True, exist_ok=True)
    environment = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home),
                   "HALO_SAVE_ROOT": str(home / "saves"), "HALO_UPDATE_TEST_SERVER": server.url,
                   "HALO_UPDATE_TEST_CA": str(server.authority), "HALO_UPDATE_TEST_LOG": str(log)}
    subprocess.run([str(folder / "halo"), "--setup", "two words"], cwd=folder, env=environment, timeout=120,
                   capture_output=True)
    lines = log.read_text().splitlines()
    assert has(lines, f"{OLD} halo arguments: --setup two words")
    assert has(lines, f"{NEW} halo arguments: --setup two words")


def test_launcher_restart_in_the_same_process(builds, tmp_path):
    """the launcher's restarts (SETUP in the menus: --setup; a mode that
    needs one: --mode=<id>) start the game again in this same process
    (update_start_in_place_with), with that one argument and not the old
    run's, so Steam and gamescope see one game, not two"""
    folder = install(tmp_path, builds)
    log = tmp_path / "update.log"
    home = tmp_path / "home"
    (home / "saves").mkdir(parents=True, exist_ok=True)
    for argument in ("--setup", "--mode=ce_plus"):
        log.write_text("")
        environment = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home),
                       "HALO_SAVE_ROOT": str(home / "saves"), "HALO_UPDATE_TEST_LOG": str(log),
                       "HALO_UPDATE_TEST_SERVER": "https://127.0.0.1:9", "HALO_UPDATE_TEST_RESTART_WITH": argument}
        subprocess.run([str(folder / "halo")], cwd=folder, env=environment, timeout=120, capture_output=True)
        deadline = time.time() + 60
        lines = []
        while time.time() < deadline:
            lines = log.read_text().splitlines()
            if sum(line.endswith(" done") for line in lines) >= 1:
                break
            time.sleep(0.2)
        # (and no third run after it: a watcher that took the same program for
        # the old one would start it again as it ends)
        time.sleep(2)
        lines = log.read_text().splitlines()
        assert has(lines, f"{OLD} halo restarting with {argument}"), "\n".join(lines)
        assert has(lines, f"{OLD} halo arguments: {argument}"), "\n".join(lines)
        runs = pids(lines, OLD, "halo")
        assert len(runs) == 2 and runs[0] == runs[1], runs
    launcher = (ROOT / "port/linux/src/launcher.c").read_text()
    for function in ("static int restart_with(const char *mode)", "int launcher_restart_to_setup(void)"):
        body = launcher[launcher.index(function):]
        body = body[:body.index("\n}\n")]
        assert body.index("update_start_in_place_with(") < body.index("update_launch_with(")
        assert "#ifdef __linux__" in body


def test_restart_waits_for_lobbies_too():
    """the game restarts into an update only at the main menu with no network
    game: the pregame lobby and the carnage report are main menu screens
    with the game's server or client still up (menu_functions.c)"""
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    body = functions[functions.index("int updater_restart_allowed(void)"):]
    body = body[:body.index("}") + 1]
    assert "main_menu_is_active()" in body
    assert "!global_network_game_server_get()" in body and "!global_network_game_client_get()" in body


def test_one_new_version_after_a_restart(keys, builds, server, tmp_path):
    """the watcher left behind starts nothing once the game has become the
    new version: one new version, the same process"""
    folder = install(tmp_path, builds)
    release = tmp_path / "release"
    zip_path = release_zip(release, builds)
    update_sign.manifest(keys[0][0], NEW, f"v{NEW}", f"v{NEW}-debug", "0" * 40, [zip_path])
    server.release(NEW, release, [ASSET, "updates.json", "updates.json.sig"])
    lines = run(tmp_path, folder, server, f"{NEW} halo")
    time.sleep(3)
    lines = (tmp_path / "update.log").read_text().splitlines()
    assert sum(1 for line in lines if line == f"{NEW} halo started") == 1, lines
    assert pids(lines, OLD, "halo") == pids(lines, NEW, "halo")
