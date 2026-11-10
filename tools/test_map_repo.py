"""Chupathingy maps (maps.chupathingy.org), the game's side: the signed index
and mirror list read (port/linux/src/map_index.c) and the client that
refreshes them and downloads from the mirrors (map_repo.c), built by
themselves with a test's keys (port/linux/tests/map_repo_test.c) and held
to documents written here as the map repository writes them (its
docs/index-format.md). The downloads go to a fake repository on this
machine: mirror order and failover, a file that isn't the index's, the
kill switch, the blacklist and an older index replayed.

The game itself, asking the community map repositories first and then
Chupathingy maps, is checked by test_game_asks_community_first_then_ours,
which needs a game built with the test key (HALO_MAP_REPO_GAME, see there)
and is skipped without one."""
import copy
import hashlib
import http.server
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import delta_table  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port" / "linux" / "src"
MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"
DRIVER = ROOT / "port" / "linux" / "tests" / "map_repo_test.c"
INDEX_CONTEXT = b"chupathingy-map-index 1\n"
MIRRORS_CONTEXT = b"chupathingy-map-mirrors 1\n"
NOW = 1793900000
PIECE = 0x40000


def compiler():
    for name in ("clang", "cc", "gcc"):
        if shutil.which(name):
            return name
    return None


def map_hash(data: bytes) -> str:
    """the whole-file hash hosts name maps by (map_archive_map_hash)"""
    whole = hashlib.blake2b(digest_size=32)
    whole.update(b"OpenCE map 1" + len(data).to_bytes(4, "little"))
    for at in range(0, len(data), PIECE):
        whole.update(hashlib.blake2b(data[at:at + PIECE], digest_size=32).digest())
    return whole.hexdigest()


def version_of(hash_text: str) -> str:
    return "00000001" if hash_text[:8] == "00000000" else hash_text[:8]


def map_entry(map_name: str, data: bytes, **changes) -> dict:
    whole = map_hash(data)
    entry = {"sha256": hashlib.sha256(data).hexdigest(), "name": map_name, "aliases": [], "kind": "ce", "size": len(data),
             "map_hash": whole, "version": version_of(whole), "blake2b": hashlib.blake2b(data, digest_size=32).hexdigest(),
             "md5": hashlib.md5(data).hexdigest(), "header_checksum": "1234abcd", "status": "approved",
             "files": ["map", "zip", "inv"]}
    entry.update(changes)
    return entry


def dumps(document: dict) -> bytes:
    # (as the map repository writes them: maprepo/index.py's dumps)
    return (json.dumps(document, sort_keys=True, indent=1, ensure_ascii=True) + "\n").encode()


def index_document(maps, blacklist=(), serial=12, issued=NOW - 100, lifetime=3 * 86400, **changes) -> dict:
    document = {"map_index": 1, "serial": serial, "issued": issued, "expires": issued + lifetime, "refresh": 3600,
                "halt": False, "halt_reason": "", "maps": list(maps), "blacklist": list(blacklist)}
    document.update(changes)
    return document


def mirrors_document(mirrors, serial=4, issued=NOW - 100) -> dict:
    return {"map_mirrors": 1, "serial": serial, "issued": issued, "expires": issued + 3 * 86400, "mirrors": list(mirrors)}


def mirror(identifier, base_url, priority=10, status="ok", **changes) -> dict:
    entry = {"id": identifier, "base_url": base_url, "region": "any", "priority": priority, "status": status,
             "operator": "someone"}
    entry.update(changes)
    return entry


def sign(key: Path, context: bytes, raw: bytes) -> bytes:
    return (delta_table.sign_bytes(key, context + raw).hex() + "\n").encode()


@pytest.fixture(scope="module")
def keys(tmp_path_factory):
    try:
        delta_table.openssl()
    except SystemExit:
        pytest.skip("needs OpenSSL 3")
    folder = tmp_path_factory.mktemp("keys")
    primary, recovery, other = folder / "primary.pem", folder / "recovery.pem", folder / "other.pem"
    return ((primary, delta_table.keygen(primary)), (recovery, delta_table.keygen(recovery)),
            (other, delta_table.keygen(other)))


@pytest.fixture(scope="module")
def driver(keys, tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    folder = tmp_path_factory.mktemp("driver")
    primary = ",".join(f"0x{byte:02x}" for byte in keys[0][1])
    recovery = ",".join(f"0x{byte:02x}" for byte in keys[1][1])
    program = folder / "map_repo_test"
    sanitizers = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"] if sys.platform != "win32" else []
    sources = [DRIVER, SRC / "map_index.c", SRC / "map_repo.c", SRC / "playlist_manifest.c",
               MONOCYPHER / "monocypher.c", MONOCYPHER / "monocypher-ed25519.c"]
    command = [cc, "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
               f"-DHALO_MAPS_TEST_KEY={primary}", f"-DHALO_MAPS_TEST_RECOVERY_KEY={recovery}",
               "-I", str(SRC), "-I", str(MONOCYPHER), *map(str, sources), "-lpthread", "-o", str(program)]
    built = subprocess.run(command[:1] + sanitizers + command[1:], capture_output=True, text=True)
    if built.returncode != 0 and sanitizers:
        built = subprocess.run(command, capture_output=True, text=True)
    assert built.returncode == 0, built.stderr[-4000:]
    return program


def run(program, *arguments) -> str:
    result = subprocess.run([str(program), *map(str, arguments)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    return result.stdout


class Signed:
    """a document written and signed into a folder"""

    def __init__(self, folder: Path, keys):
        self.folder = folder
        self.keys = keys
        self.count = 0

    def write(self, document, context=INDEX_CONTEXT, key=None, raw=None, signature=None):
        self.count += 1
        raw = raw if raw is not None else dumps(document)
        path = self.folder / f"doc{self.count}.json"
        path.write_bytes(raw)
        sig = self.folder / f"doc{self.count}.json.sig"
        sig.write_bytes(signature if signature is not None else sign(key or self.keys[0][0], context, raw))
        return path, sig


@pytest.fixture
def signed(tmp_path, keys):
    return Signed(tmp_path, keys)


DATA = b"\x01\x02map data " * 5000
OTHER = b"another map's bytes " * 4000


# ---------- the index

def test_index_checks(driver, signed):
    document = index_document([map_entry("bigass", DATA)])
    assert run(driver, "index", *signed.write(document), NOW) == "ok 12 1 0 0 3600\n"


def test_index_as_the_repository_writes_it_with_its_example(driver, signed):
    # (the repository's own example fields, unknown ones among them, read)
    entry = map_entry("bigass_open_v1.1", DATA, aliases=["bigass_open_v1.1_3fa81c22"], later_field={"a": [1, 2]})
    document = index_document([entry], blacklist=[{"hash": "ab" * 32, "kind": "sha256", "name": "x",
                                                  "reason": "crashes the server on load", "since": NOW - 1000}])
    assert run(driver, "index", *signed.write(document), NOW) == "ok 12 1 1 0 3600\n"


def test_recovery_key_signs_later_epochs(driver, signed, keys):
    document = index_document([], serial=(1 << 24) + 1)
    assert run(driver, "index", *signed.write(document, key=keys[1][0]), NOW).startswith("ok ")
    # (the primary key may not open an epoch)
    assert "epoch" in run(driver, "index", *signed.write(document, key=keys[0][0]), NOW)


@pytest.mark.parametrize("case", ["other key", "changed byte", "mirror context", "short", "not hex", "empty"])
def test_bad_signatures_refused(driver, signed, keys, case):
    document = index_document([map_entry("bigass", DATA)])
    raw = dumps(document)
    if case == "other key":
        path = signed.write(document, key=keys[2][0])
    elif case == "changed byte":
        path = signed.write(document, raw=raw.replace(b"bigass", b"bigasz"), signature=sign(keys[0][0], INDEX_CONTEXT, raw))
    elif case == "mirror context":
        path = signed.write(document, context=MIRRORS_CONTEXT)
    elif case == "short":
        path = signed.write(document, signature=b"abcd\n")
    elif case == "not hex":
        path = signed.write(document, signature=b"z" * 128 + b"\n")
    else:
        path = signed.write(document, signature=b"")
    assert run(driver, "index", *path, NOW).startswith("refused: its signature")


def test_expired_refused(driver, signed):
    document = index_document([map_entry("bigass", DATA)], issued=NOW - 4 * 86400)
    assert run(driver, "index", *signed.write(document), NOW) == "refused: it has expired\n"
    # (the moment it expires it is still good)
    assert run(driver, "index", *signed.write(document), NOW - 86400).startswith("ok ")


@pytest.mark.parametrize("change, why", [
    ({"map_index": 2}, "its format is not one this build reads"),
    ({"serial": 0}, "its serial is wrong"),
    ({"refresh": 5}, "its refresh is wrong"),
    ({"halt": "yes"}, "its halt is wrong"),
    ({"expires": NOW - 200}, "it expires before it was issued"),
])
def test_index_fields_refused(driver, signed, change, why):
    document = index_document([map_entry("bigass", DATA)])
    document.update(change)
    assert run(driver, "index", *signed.write(document), NOW) == f"refused: {why}\n"


@pytest.mark.parametrize("change, why", [
    ({"size": 0}, "a map's size is wrong"),
    ({"size": 1024 * 1024 * 1024 + 1}, "a map's size is wrong"),
    ({"size": -5}, "a map's size is wrong"),
    ({"size": 1.5}, "a map's size is wrong"),
    ({"sha256": "AB" * 32}, "a map's sha256 is wrong"),
    ({"sha256": "ab" * 31}, "a map's sha256 is wrong"),
    ({"name": "../ui"}, "a map's name is wrong"),
    ({"name": "Big Ass"}, "a map's name is wrong"),
    ({"name": "x" * 32}, "a map's name is wrong"),
    ({"aliases": ["ok", "Not/Ok"]}, "a map's aliases is wrong"),
    ({"version": "00000000"}, "a map's version is not its map hash's"),
])
def test_bad_map_entries_refuse_the_index(driver, signed, change, why):
    document = index_document([map_entry("bigass", DATA, **change)])
    assert run(driver, "index", *signed.write(document), NOW) == f"refused: {why}\n"


def test_missing_field_refuses_the_index(driver, signed):
    entry = map_entry("bigass", DATA)
    del entry["map_hash"]
    assert run(driver, "index", *signed.write(index_document([entry])), NOW) == \
        "refused: a map lacks a field it must have\n"


def test_too_large_refused(driver, signed):
    document = index_document([map_entry("bigass", DATA)])
    raw = dumps(document)[:-2] + b" " * (8 * 1024 * 1024) + b"}\n"
    assert run(driver, "index", *signed.write(document, raw=raw), NOW) == "refused: it is too large\n"


def test_unknown_kinds_and_statuses_passed_over(driver, signed):
    maps = [map_entry("bigass", DATA, kind="n64"), map_entry("bigass", DATA, status="pending"),
            map_entry("other", OTHER)]
    assert run(driver, "index", *signed.write(index_document(maps)), NOW) == "ok 12 1 0 0 3600\n"


# ---------- finding a map

def write_plain(tmp_path, document, name="index.json"):
    path = tmp_path / name
    path.write_bytes(dumps(document))
    return path


def test_find_by_name_and_version(driver, tmp_path):
    entry = map_entry("bigass", DATA, aliases=["bigass_v3"])
    path = write_plain(tmp_path, index_document([entry, map_entry("other", OTHER)]))
    assert run(driver, "find", path, "BigAss", entry["version"]).split()[:2] == ["found", "bigass"]
    assert run(driver, "find", path, "bigass_v3", entry["version"]).startswith("found bigass ")
    # (an older host's version: the header's checksum)
    assert run(driver, "find", path, "bigass", "1234abcd").startswith("found bigass ")
    assert run(driver, "find", path, "bigass", "deadbeef") == "none: Chupathingy maps doesn't have it\n"
    assert run(driver, "find", path, "nothere", "0") == "none: Chupathingy maps doesn't have it\n"


def test_find_any_version_only_when_one(driver, tmp_path):
    first, second = map_entry("bigass", DATA), map_entry("bigass", OTHER, header_checksum="99999999")
    path = write_plain(tmp_path, index_document([first]))
    assert run(driver, "find", path, "bigass", "0").startswith("found bigass ")
    path = write_plain(tmp_path, index_document([first, second]))
    assert "several versions" in run(driver, "find", path, "bigass", "0")
    assert run(driver, "find", path, "bigass", second["version"]).split()[2] == second["sha256"]


def test_find_only_custom_edition_maps(driver, tmp_path):
    path = write_plain(tmp_path, index_document([map_entry("bigass", DATA, kind="pc")]))
    assert run(driver, "find", path, "bigass", "0").startswith("none")


# ---------- the blacklist

def test_blacklist_by_sha256_and_map_hash(driver, tmp_path):
    entry = map_entry("bigass", DATA)
    blacklist = [{"hash": entry["sha256"], "kind": "sha256", "name": "bigass", "reason": "crashes the server on load",
                  "since": NOW},
                 {"hash": map_hash(OTHER), "kind": "map_hash", "name": "other", "reason": "breaks the engine",
                  "since": NOW}]
    path = write_plain(tmp_path, index_document([], blacklist=blacklist))
    assert run(driver, "blacklisted", path, entry["sha256"], "-") == "blocked crashes the server on load\n"
    assert run(driver, "blacklisted", path, "-", map_hash(OTHER)) == "blocked breaks the engine\n"
    # (a hash of one kind never matches an entry of the other)
    assert run(driver, "blacklisted", path, "-", entry["sha256"]) == "clear\n"
    assert run(driver, "blacklisted", path, hashlib.sha256(OTHER).hexdigest(), map_hash(DATA)) == "clear\n"


@pytest.mark.parametrize("change", [{"hash": "nothex"}, {"reason": "line\nbreak"}, {"hash": "AB" * 32}])
def test_bad_blacklist_entries_refuse_the_index(driver, signed, change):
    block = {"hash": "ab" * 32, "kind": "sha256", "name": "x", "reason": "r", "since": NOW}
    block.update(change)
    assert run(driver, "index", *signed.write(index_document([], blacklist=[block])), NOW).startswith("refused: ")


# ---------- the mirror list

def test_mirrors_check(driver, signed):
    document = mirrors_document([mirror("chupathingy", "https://maps.chupathingy.org", 1)])
    assert run(driver, "mirrors", *signed.write(document, context=MIRRORS_CONTEXT), NOW) == "ok 4 1\n"
    # (signed as an index: refused)
    assert run(driver, "mirrors", *signed.write(document), NOW).startswith("refused: its signature")


@pytest.mark.parametrize("url", ["http://maps.example.org", "https://user@example.org/x", "https://example.org/a?b=c",
                                 "https://example.org/../x", "https://example.org/", "ftp://example.org", "https://"])
def test_mirror_addresses_refused(driver, signed, url):
    document = mirrors_document([mirror("m", url)])
    assert run(driver, "mirrors", *signed.write(document, context=MIRRORS_CONTEXT), NOW) == \
        "refused: a mirror's base_url is wrong\n"


def test_mirror_choice(driver, tmp_path):
    sha = hashlib.sha256(DATA).hexdigest()
    other_shard = "0" if sha[0] != "0" else "1"
    document = mirrors_document([
        mirror("late", "https://late.example.org", 50),
        mirror("bad", "https://bad.example.org", 1, status="bad"),
        mirror("stale", "https://stale.example.org", 1, status="stale"),
        mirror("future", "https://future.example.org", 1, status="resting"),
        mirror("sharded", "https://sharded.example.org", 5, shard=[sha[:2]]),
        mirror("elsewhere", "https://elsewhere.example.org", 2, shard=[other_shard]),
        mirror("main", "https://maps.chupathingy.org", 10),
        mirror("main2", "https://maps2.chupathingy.org", 10),
    ])
    path = write_plain(tmp_path, document, "mirrors.json")
    assert run(driver, "choose", path, sha) == "sharded main main2 late\n"


# ---------- downloads, from a fake repository

class FakeRepository:
    """paths answered on this machine: bytes (200), or a status"""

    def __init__(self):
        self.routes = {}
        self.asked = []
        repository = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                repository.asked.append(self.path)
                answer = repository.routes.get(self.path, 404)
                if isinstance(answer, int):
                    self.send_response(answer)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header("Content-Length", str(len(answer)))
                self.end_headers()
                self.wfile.write(answer)

            def log_message(self, *arguments):
                pass

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def publish(self, prefix, keys, index, mirrors, index_key=None):
        raw = dumps(index)
        self.routes[f"{prefix}/index.json"] = raw
        self.routes[f"{prefix}/index.json.sig"] = sign(index_key or keys[0][0], INDEX_CONTEXT, raw)
        raw = dumps(mirrors)
        self.routes[f"{prefix}/mirrors.json"] = raw
        self.routes[f"{prefix}/mirrors.json.sig"] = sign(keys[0][0], MIRRORS_CONTEXT, raw)

    def host_file(self, prefix, data, sha=None):
        sha = sha or hashlib.sha256(data).hexdigest()
        self.routes[f"{prefix}/maps/{sha[:2]}/{sha}.map"] = data

    def close(self):
        self.server.shutdown()
        self.server.server_close()


@pytest.fixture
def repository():
    fake = FakeRepository()
    yield fake
    fake.close()


def download(driver, repository, path, name="bigass", version="0", now=NOW, prefix="", *more):
    return run(driver, "download", repository.url + prefix, now, name, version, path, *more).splitlines()


def test_download_from_the_first_good_mirror(driver, repository, keys, tmp_path):
    entry = map_entry("bigass", DATA)
    mirrors = [mirror("down", repository.url + "/down", 1), mirror("liar", repository.url + "/liar", 2),
               mirror("short", repository.url + "/short", 3), mirror("good", repository.url + "/good", 4),
               mirror("never", repository.url + "/never", 5)]
    repository.publish("", keys, index_document([entry]), mirrors_document(mirrors))
    repository.host_file("/liar", OTHER, entry["sha256"])
    repository.host_file("/short", DATA[:-1], entry["sha256"])
    repository.host_file("/good", DATA)
    repository.host_file("/never", DATA)
    out = tmp_path / "bigass.map.download"
    lines = download(driver, repository, out, "bigass", entry["version"])
    assert lines[0] == "refresh 1 " and lines[1] == "has 1"
    assert [line.split("/")[3] for line in lines if line.startswith("GET ")] == ["down", "liar", "short", "good"]
    assert lines[-1] == f"downloaded bigass {len(DATA)}"
    assert out.read_bytes() == DATA


def test_hash_mismatch_refused_everywhere(driver, repository, keys, tmp_path):
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry]),
                       mirrors_document([mirror("liar", repository.url + "/liar", 1)]))
    repository.host_file("/liar", DATA[:-1] + b"X", entry["sha256"])
    out = tmp_path / "bigass.map.download"
    lines = download(driver, repository, out, "bigass", entry["version"])
    assert lines[-1].startswith("failed 0 no mirror gave it (mirror liar gave a file that isn't the index's")
    assert not out.exists()


def test_not_in_the_index(driver, repository, keys, tmp_path):
    repository.publish("", keys, index_document([map_entry("bigass", DATA)]),
                       mirrors_document([mirror("m", repository.url + "/m", 1)]))
    lines = download(driver, repository, tmp_path / "x", "nothere", "0")
    assert lines[1] == "has 0"
    assert lines[-1] == "failed 1 Chupathingy maps doesn't have it"


def test_blacklisted_map_never_downloaded(driver, repository, keys, tmp_path):
    entry = map_entry("bigass", DATA)
    blacklist = [{"hash": entry["map_hash"], "kind": "map_hash", "name": "bigass", "reason": "crashes the server",
                  "since": NOW}]
    repository.publish("", keys, index_document([entry], blacklist=blacklist),
                       mirrors_document([mirror("m", repository.url + "/m", 1)]))
    repository.host_file("/m", DATA)
    lines = download(driver, repository, tmp_path / "x", "bigass", entry["version"])
    assert lines[1] == "has 0"
    assert lines[-1] == "failed 0 it is blocked: crashes the server"
    assert not [path for path in repository.asked if path.startswith("/m/")]


def test_kill_switch(driver, repository, keys, tmp_path):
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry], halt=True, halt_reason="checking a report"),
                       mirrors_document([mirror("m", repository.url + "/m", 1)]))
    repository.host_file("/m", DATA)
    lines = download(driver, repository, tmp_path / "x", "bigass", entry["version"])
    assert lines[-1] == "failed 0 Chupathingy maps has stopped all downloads for now (checking a report)"
    assert not [path for path in repository.asked if path.startswith("/m/")]


def test_unsigned_index_never_used(driver, repository, keys, tmp_path):
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry]), mirrors_document([mirror("m", repository.url + "/m", 1)]),
                       index_key=keys[2][0])
    repository.host_file("/m", DATA)
    lines = download(driver, repository, tmp_path / "x", "bigass", entry["version"])
    assert lines[0] == "refresh 0 the index was refused: its signature does not match"
    assert lines[-1] == "failed 1 no index that checks"


def test_expired_index_never_used(driver, repository, keys, tmp_path):
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry]), mirrors_document([mirror("m", repository.url + "/m", 1)]))
    lines = download(driver, repository, tmp_path / "x", "bigass", entry["version"], NOW + 4 * 86400)
    assert lines[0] == "refresh 0 the index was refused: it has expired"


def test_older_index_never_replaces_a_newer(driver, repository, keys, tmp_path):
    entry = map_entry("bigass", DATA)
    blacklist = [{"hash": entry["sha256"], "kind": "sha256", "name": "bigass", "reason": "found to crash",
                  "since": NOW}]
    mirrors = mirrors_document([mirror("m", repository.url + "/m", 1)])
    # (the newer, with the map blacklisted, held first; then the older one,
    # still unexpired, offered again)
    repository.publish("/new", keys, index_document([entry], blacklist=blacklist, serial=13), mirrors)
    repository.publish("/old", keys, index_document([entry], serial=12), mirrors)
    repository.host_file("/m", DATA)
    lines = download(driver, repository, tmp_path / "x", "bigass", entry["version"], NOW, "/old",
                     repository.url + "/new", NOW)
    assert lines[0] == "refresh 1 "
    assert lines[1] == "refresh 0 the index was refused: its serial is 12, older than the 13 held"
    assert lines[-1] == "failed 0 it is blocked: found to crash"


def test_mirror_list_refused_index_kept(driver, repository, keys, tmp_path):
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry]), mirrors_document([mirror("m", repository.url + "/m", 1)]))
    repository.routes["/mirrors.json.sig"] = b"00" * 64 + b"\n"
    lines = download(driver, repository, tmp_path / "x", "bigass", entry["version"])
    assert lines[0] == "refresh 1 the mirror list was refused: its signature does not match"
    assert lines[-1] == "failed 0 Chupathingy maps has no mirror list that checks"


# ---------- the game: the community map repositories first, then ours

GAME = os.environ.get("HALO_MAP_REPO_GAME")
GAME_KEY = os.environ.get("HALO_MAP_REPO_KEY")
GAME_MAP = os.environ.get("HALO_MAP_REPO_MAP")
GAME_MAPS = os.environ.get("HALO_MAP_REPO_XBOX_MAPS")
GAME_SKIP = "needs a game built with a test key: HALO_MAP_REPO_GAME, _KEY, _MAP and _XBOX_MAPS (see the test)"


def zipped(name: str, data: bytes) -> bytes:
    import io
    import zipfile
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(f"{name}.map", data)
    return buffer.getvalue()


def run_game(tmp_path, repository, name, version):
    """the game's download of name (debug.network_test "download:"), its
    own data and save folders in tmp_path: (its exit code, its log)"""
    data, save = tmp_path / "data", tmp_path / "save"
    (data / "maps_ce").mkdir(parents=True)
    save.mkdir()
    (data / "maps").symlink_to(Path(GAME_MAPS).resolve())
    environment = dict(os.environ, HALO_DATA_ROOT=str(data), HALO_SAVE_ROOT=str(save), HALO_NULL_RENDERER="1",
                       HALO_HIDDEN_WINDOW="1", HALO_NET_MAP_DOWNLOADS="always",
                       HALO_NETWORK_TEST=f"download:{name}:{version}",
                       HALO_MAP_REPOSITORIES=repository.url + "/hac2", HALO_NET_MAPS_URL=repository.url + "/ours")
    result = subprocess.run([GAME], env=environment, capture_output=True, text=True, timeout=300)
    logs = sorted(tmp_path.rglob("debug*.txt"))
    log = "\n".join(path.read_text(errors="replace") for path in logs) + result.stdout + result.stderr
    return result.returncode, log, data / "maps_ce"


@pytest.fixture
def game_map():
    if not (GAME and GAME_KEY and GAME_MAP and GAME_MAPS):
        pytest.skip(GAME_SKIP)
    data = Path(GAME_MAP).read_bytes()
    return Path(GAME_MAP).stem.lower(), data


def publish_for_game(repository, name, data, blacklist=()):
    key = Path(GAME_KEY)
    entry = map_entry(name, data)
    index = dumps(index_document([entry], blacklist=blacklist, issued=int(time.time()) - 60))
    mirrors = dumps(mirrors_document([mirror("m", repository.url + "/mirror", 1)],
                                     issued=int(time.time()) - 60))
    repository.routes["/ours/index.json"] = index
    repository.routes["/ours/index.json.sig"] = sign(key, INDEX_CONTEXT, index)
    repository.routes["/ours/mirrors.json"] = mirrors
    repository.routes["/ours/mirrors.json.sig"] = sign(key, MIRRORS_CONTEXT, mirrors)
    repository.host_file("/mirror", data)
    return entry


def test_game_takes_ours_when_the_community_lacks_it(repository, game_map, tmp_path):
    """The game (HALO_MAP_REPO_GAME, its executable, built with
    port/linux/src/maps_key.h's primary key set to HALO_MAP_REPO_KEY's public
    half, a PEM file) downloads HALO_MAP_REPO_MAP (a Custom Edition map, read
    from ~/halo-maps, never copied into the repository) with debug.network_test
    "download:<name>:<version>", HALO_MAP_REPO_XBOX_MAPS its maps folder: the
    community repositories (debug.map_repositories, here the fake's /hac2) are
    asked first and don't have it, then Chupathingy maps gives it."""
    name, data = game_map
    entry = publish_for_game(repository, name, data)
    code, log, folder = run_game(tmp_path, repository, name, entry["version"])
    assert code == 0, log[-3000:]
    assert (folder / f"{name}.map").read_bytes() == data
    asked = [path for path in repository.asked if not path.startswith("/ours/index") and "mirrors.json" not in path]
    assert asked[0].startswith("/hac2/halonet/locator.php")
    assert asked[-1].startswith("/mirror/maps/")
    assert "from Chupathingy maps" in log


def test_game_takes_the_community_first(repository, game_map, tmp_path):
    name, data = game_map
    entry = publish_for_game(repository, name, data)
    repository.routes[f"/hac2/halonet/locator.php?format=zip&map={name}&type=halom"] = zipped(name, data)
    code, log, folder = run_game(tmp_path, repository, name, entry["version"])
    assert code == 0, log[-3000:]
    assert (folder / f"{name}.map").read_bytes() == data
    assert not [path for path in repository.asked if path.startswith("/mirror/")]


def test_game_refuses_a_blacklisted_community_map(repository, game_map, tmp_path):
    name, data = game_map
    blacklist = [{"hash": map_hash(data), "kind": "map_hash", "name": name, "reason": "crashes the server on load",
                  "since": NOW}]
    entry = publish_for_game(repository, name, data, blacklist)
    repository.routes[f"/hac2/halonet/locator.php?format=zip&map={name}&type=halom"] = zipped(name, data)
    code, log, folder = run_game(tmp_path, repository, name, entry["version"])
    assert code == 1, log[-3000:]
    assert "crashes the server on load" in log
    assert not (folder / f"{name}.map").exists()
    assert not [path for path in repository.asked if path.startswith("/mirror/")]
