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
import re
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


def mirrors_document(mirrors, serial=4, issued=NOW - 100, lifetime=3 * 86400) -> dict:
    return {"map_mirrors": 1, "serial": serial, "issued": issued, "expires": issued + lifetime, "mirrors": list(mirrors)}


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


def run(program, *arguments, environment=None) -> str:
    result = subprocess.run([str(program), *map(str, arguments)], capture_output=True, text=True, timeout=60,
                            env=dict(os.environ, **(environment or {})))
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
                                 "https://example.org/../x", "https://example.org/", "ftp://example.org", "https://",
                                 "http://localhost.example.org", "http://127.0.0.1.example.org/maps",
                                 "http://localhostevil.org"])
def test_mirror_addresses_refused(driver, signed, url):
    document = mirrors_document([mirror("m", url)])
    assert run(driver, "mirrors", *signed.write(document, context=MIRRORS_CONTEXT), NOW) == \
        "refused: a mirror's base_url is wrong\n"


@pytest.mark.parametrize("url", ["http://localhost", "http://localhost:8080/maps", "http://127.0.0.1:9/x",
                                 "https://maps.example.org/files"])
def test_mirror_addresses_taken(driver, signed, url):
    document = mirrors_document([mirror("m", url)])
    assert run(driver, "mirrors", *signed.write(document, context=MIRRORS_CONTEXT), NOW) == "ok 4 1\n"


def test_control_characters_refused(driver, signed):
    # (a field's name is logged when it is wrong: never one with a control
    # character, escaped or not)
    raw = dumps(index_document([]))[:-2] + b',\n "bad\\nname": "x"\n}\n'
    assert run(driver, "index", *signed.write(None, raw=raw), NOW) == "refused: a field is not one\n"
    raw = dumps(index_document([]))[:-2] + b',\n "bad\x1bname": 1\n}\n'
    assert run(driver, "index", *signed.write(None, raw=raw), NOW) == "refused: a field is not one\n"
    # (a terminal's escape as UTF-8's C1 characters, in a text shown and logged)
    raw = dumps(index_document([], halt=True, halt_reason="HALTREASON")).replace(b"HALTREASON", b"x\xc2\x9b31m")
    assert run(driver, "index", *signed.write(None, raw=raw), NOW) == "refused: its halt_reason is wrong\n"
    raw = dumps(index_document([], blacklist=[{"hash": "ab" * 32, "kind": "sha256", "name": "x", "reason": "RR",
                                               "since": NOW}])).replace(b'"RR"', b'"a\\tb"')
    assert run(driver, "index", *signed.write(None, raw=raw), NOW).startswith("refused: ")


@pytest.mark.parametrize("field", ["kind", "status"])
def test_a_text_field_of_another_type_refused(driver, signed, field):
    # (a value that isn't a string is never compared as one)
    document = index_document([map_entry("bigass", DATA, **{field: 7})])
    assert run(driver, "index", *signed.write(document), NOW).startswith("refused: a map's ")
    document = mirrors_document([mirror("m", "https://maps.example.org", status=7)])
    assert run(driver, "mirrors", *signed.write(document, context=MIRRORS_CONTEXT), NOW) == \
        "refused: a mirror's status is wrong\n"


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
    """paths answered on this machine: bytes (200), or a status (either
    given by a function, asked each time)"""

    def __init__(self):
        self.routes = {}
        self.asked = []
        repository = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                repository.asked.append(self.path)
                answer = repository.routes.get(self.path, 404)
                if callable(answer):
                    answer = answer()
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
    assert lines[1] == "refresh 0 the index was refused: its serial is 12, older than the 13 taken"
    assert lines[-1] == "failed 0 it is blocked: found to crash"


def test_mirror_list_refused_index_kept(driver, repository, keys, tmp_path):
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry]), mirrors_document([mirror("m", repository.url + "/m", 1)]))
    repository.routes["/mirrors.json.sig"] = b"00" * 64 + b"\n"
    lines = download(driver, repository, tmp_path / "x", "bigass", entry["version"])
    assert lines[0] == "refresh 1 the mirror list was refused: its signature does not match"
    assert lines[-1] == "failed 0 Chupathingy maps has no mirror list that checks"


def test_refused_mirror_list_asked_again_after_five_minutes(driver, repository, keys):
    # (a full refresh that got no mirror list: the mirror list alone, not the
    # index, is asked for again after 5 minutes, then 10, not at the index's
    # next refresh an hour on)
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry]), mirrors_document([mirror("m", repository.url + "/m", 1)]))
    repository.routes["/mirrors.json.sig"] = b"00" * 64 + b"\n"
    lines = run(driver, "refreshes", repository.url, NOW, NOW + 120, NOW + 301, NOW + 700, NOW + 902).splitlines()
    assert lines == ["refresh 1 4", "refresh 1 4", "refresh 1 6", "refresh 1 6", "refresh 1 8"]
    assert repository.asked[4:] == ["/mirrors.json", "/mirrors.json.sig"] * 2


def test_refused_mirror_list_waits_longer_each_time(driver, repository, keys):
    # (a mirror list refused for good: its wait doubles up to an hour, and
    # the index taken at its own refresh doesn't start it over)
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry]), mirrors_document([mirror("m", repository.url + "/m", 1)]))
    repository.routes["/mirrors.json.sig"] = b"00" * 64 + b"\n"
    # (asked at 0; alone again at 300, 900 and 2100, not at 2000; the
    # index's hour at 3700 asks for both, and the mirror list's wait is then
    # the hour, so nothing at 5000 or 7000)
    lines = run(driver, "refreshes", repository.url, NOW, NOW + 300, NOW + 900, NOW + 2000, NOW + 2100, NOW + 3700,
                NOW + 5000, NOW + 7000).splitlines()
    assert lines == ["refresh 1 4", "refresh 1 6", "refresh 1 8", "refresh 1 8", "refresh 1 10", "refresh 1 14",
                     "refresh 1 14", "refresh 1 14"]


def test_mirror_list_that_checks_ends_the_wait(driver, repository, keys):
    # (refused once, then fixed: taken at the next try, then asked for only
    # with the index an hour on)
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry]), mirrors_document([mirror("m", repository.url + "/m", 1)]))
    good = repository.routes["/mirrors.json.sig"]
    answers = [b"00" * 64 + b"\n"]
    repository.routes["/mirrors.json.sig"] = lambda: answers.pop() if answers else good
    lines = run(driver, "refreshes", repository.url, NOW, NOW + 300, NOW + 900, NOW + 3000, NOW + 3700).splitlines()
    assert lines == ["refresh 1 4", "refresh 1 6", "refresh 1 6", "refresh 1 6", "refresh 1 10"]


def test_expired_mirror_list_never_used(driver, repository, keys, tmp_path):
    # (checked at use, not only when fetched: a list that expired since)
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry], lifetime=5 * 86400),
                       mirrors_document([mirror("m", repository.url + "/m", 1)], lifetime=86400))
    repository.host_file("/m", DATA)
    lines = run(driver, "download", repository.url, NOW, "bigass", entry["version"], tmp_path / "x",
                environment={"MAP_REPO_TEST_USE_AT": str(NOW + 2 * 86400)}).splitlines()
    assert lines[0] == "refresh 1 "
    assert lines[1] == "has 0"
    assert lines[-1] == "failed 0 Chupathingy maps has no mirror list that checks"
    assert not [path for path in repository.asked if path.startswith("/m/")]


def test_index_alone_when_nothing_is_downloaded(driver, repository, keys, tmp_path):
    # (network.map_downloads "never", and the dedicated server: the
    # blacklist is fetched, the mirror list never asked for)
    entry = map_entry("bigass", DATA)
    blacklist = [{"hash": entry["sha256"], "kind": "sha256", "name": "bigass", "reason": "crashes", "since": NOW}]
    repository.publish("", keys, index_document([entry], blacklist=blacklist),
                       mirrors_document([mirror("m", repository.url + "/m", 1)]))
    out = run(driver, "blocked", NOW, entry["sha256"], "-", repository.url,
              environment={"MAP_REPO_TEST_INDEX_ONLY": "1"}).splitlines()
    assert out == ["refresh 1 ", "blocked crashes"]
    assert repository.asked == ["/index.json", "/index.json.sig"]


def test_failed_refresh_waits_longer_each_time(driver):
    # (a repository that doesn't answer: asked again after 5 minutes, then
    # 10, never in between)
    closed = FakeRepository()
    url = closed.url
    closed.close()
    lines = run(driver, "refreshes", url, NOW, NOW + 100, NOW + 301, NOW + 800, NOW + 902).splitlines()
    assert lines == ["refresh 0 1", "refresh 0 1", "refresh 0 2", "refresh 0 2", "refresh 0 3"]


def many_maps(count):
    """count small maps of their own: an index the size of a repository
    with a few hundred maps"""
    return [map_entry(f"map{number:04d}", f"map {number} bytes ".encode() * 40) for number in range(count)]


def test_index_larger_than_a_request_streamed(driver, repository, keys, tmp_path):
    # (the game's plain requests read 64 KB at most, the driver's as well:
    # the index and the mirror list come through the streamed download,
    # kept whole, and the file they were streamed into is gone after)
    environment, folder = kept(tmp_path)
    entry = map_entry("bigass", DATA)
    index = index_document([*many_maps(400), entry], blacklist=blacklisted(entry, "crashes"))
    assert len(dumps(index)) > 3 * 65536
    repository.publish("", keys, index, mirrors_document([mirror("m", repository.url + "/m", 1)]))
    out = run(driver, "blocked", NOW, entry["sha256"], "-", repository.url, environment=environment).splitlines()
    assert out == ["load 0 ", "refresh 1 ", "blocked crashes"]
    assert sorted(path.name for path in folder.iterdir()) == [
        "chupathingy_maps_floor.txt", "chupathingy_maps_index.signed", "chupathingy_maps_mirrors.signed"]
    assert (folder / "chupathingy_maps_index.signed").read_bytes().endswith(dumps(index))
    # (and read back whole in the next run, the repository gone)
    repository.close()
    out = run(driver, "blocked", NOW + 60, entry["sha256"], "-", environment=environment).splitlines()
    assert out == ["load 1 kept index 12 (unexpired)", "blocked crashes"]


def test_large_index_downloads(driver, repository, keys, tmp_path):
    entry = map_entry("bigass", DATA)
    index = index_document([*many_maps(400), entry])
    assert len(dumps(index)) > 65536
    repository.publish("", keys, index, mirrors_document([mirror("m", repository.url + "/m", 1)]))
    repository.host_file("/m", DATA)
    out = tmp_path / "bigass.map.download"
    lines = download(driver, repository, out, "bigass", entry["version"])
    assert lines[0] == "refresh 1 " and lines[1] == "has 1"
    assert lines[-1] == f"downloaded bigass {len(DATA)}"
    assert out.read_bytes() == DATA


def test_documents_past_their_limits_refused(driver, repository, keys, tmp_path):
    # (8 MB for the index, 256 KB for the mirror list: refused as they
    # come, not cut short)
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry]), mirrors_document([mirror("m", repository.url + "/m", 1)]))
    repository.routes["/mirrors.json"] = b" " * (256 * 1024 + 1)
    lines = download(driver, repository, tmp_path / "x", "bigass", entry["version"])
    assert lines[0] == "refresh 1 the mirror list was refused: it is larger than the 262144 bytes allowed"
    repository.routes["/index.json"] = b" " * (8 * 1024 * 1024 + 1)
    lines = download(driver, repository, tmp_path / "x", "bigass", entry["version"])
    assert lines[0] == "refresh 0 the index was refused: it is larger than the 8388608 bytes allowed"


def test_mirror_list_fetched_when_downloads_turn_back_on(driver, repository, keys):
    # (network.map_downloads "never", then "ask" or "always": the next
    # refresh asks for the mirror list at once, not after the index's hour;
    # then nothing until the hour is up)
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry]), mirrors_document([mirror("m", repository.url + "/m", 1)]))
    lines = run(driver, "refreshes", repository.url, f"{NOW}:index", f"{NOW + 60}:index", NOW + 120, NOW + 180,
                NOW + 3800).splitlines()
    assert lines == ["refresh 1 2", "refresh 1 2", "refresh 1 4", "refresh 1 4", "refresh 1 8"]


# ---------- what is kept between runs

def kept(tmp_path):
    folder = tmp_path / "save"
    folder.mkdir(exist_ok=True)
    return {"MAP_REPO_TEST_SAVE": str(folder)}, folder


def blacklisted(entry, reason="found to crash"):
    return [{"hash": entry["sha256"], "kind": "sha256", "name": entry["name"], "reason": reason, "since": NOW}]


def test_kept_index_blocks_before_any_fetch(driver, repository, keys, tmp_path):
    environment, folder = kept(tmp_path)
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry], blacklist=blacklisted(entry)),
                       mirrors_document([mirror("m", repository.url + "/m", 1)]))
    out = run(driver, "blocked", NOW, entry["sha256"], "-", repository.url, environment=environment).splitlines()
    assert out == ["load 0 ", "refresh 1 ", "blocked found to crash"]
    assert sorted(path.name for path in folder.iterdir()) == [
        "chupathingy_maps_floor.txt", "chupathingy_maps_index.signed", "chupathingy_maps_mirrors.signed"]
    assert (folder / "chupathingy_maps_floor.txt").read_text().startswith("index 12 ")
    # (the next run, the repository gone: the kept index's blacklist)
    repository.close()
    out = run(driver, "blocked", NOW + 60, entry["sha256"], "-", environment=environment).splitlines()
    assert out == ["load 1 kept index 12 (unexpired)", "blocked found to crash"]


def test_expired_kept_index_blocks_but_never_downloads(driver, repository, keys, tmp_path):
    environment, _ = kept(tmp_path)
    entry = map_entry("bigass", DATA)
    other = map_entry("other", OTHER)
    repository.publish("", keys, index_document([entry, other], blacklist=blacklisted(entry)),
                       mirrors_document([mirror("m", repository.url + "/m", 1)]))
    repository.host_file("/m", OTHER)
    assert run(driver, "blocked", NOW, "-", "-", repository.url, environment=environment).splitlines()[1] == "refresh 1 "
    later = NOW + 10 * 86400
    repository.routes.clear()
    out = run(driver, "blocked", later, entry["sha256"], "-", environment=environment).splitlines()
    assert out == ["load 1 kept index 12 (expired: its blacklist alone used)", "blocked found to crash"]
    lines = run(driver, "download", repository.url, later, "other", other["version"], tmp_path / "x",
                environment=environment).splitlines()
    assert lines[0] == "load 1 kept index 12 (expired: its blacklist alone used)"
    assert lines[1].startswith("refresh 0 the index was refused")
    assert lines[2] == "has 0"
    assert lines[-1] == "failed 1 no index that checks"
    assert not [path for path in repository.asked if path.startswith("/m/")]


def test_floor_outlives_a_lost_index(driver, repository, keys, tmp_path):
    environment, folder = kept(tmp_path)
    entry = map_entry("bigass", DATA)
    mirrors = mirrors_document([mirror("m", repository.url + "/m", 1)])
    repository.publish("/new", keys, index_document([entry], blacklist=blacklisted(entry), serial=13), mirrors)
    repository.publish("/old", keys, index_document([entry], serial=12), mirrors)
    repository.publish("/same", keys, index_document([entry], serial=13, refresh=7200), mirrors)
    assert run(driver, "blocked", NOW, "-", "-", repository.url + "/new", environment=environment).splitlines()[1] == \
        "refresh 1 "
    (folder / "chupathingy_maps_index.signed").unlink()
    # (an older one replayed, its newer lost from the save folder: the floor
    # still refuses it)
    out = run(driver, "blocked", NOW, entry["sha256"], "-", repository.url + "/old", environment=environment)
    assert out.splitlines() == ["load 0 ", "refresh 0 the index was refused: its serial is 12, older than the 13 taken",
                                "clear"]
    # (another document of the same serial: refused too)
    out = run(driver, "blocked", NOW, entry["sha256"], "-", repository.url + "/same", environment=environment)
    assert out.splitlines()[1] == "refresh 0 the index was refused: its serial is 13, as the one taken, but it differs"
    # (the same document again: taken)
    out = run(driver, "blocked", NOW, entry["sha256"], "-", repository.url + "/new", environment=environment)
    assert out.splitlines()[1:] == ["refresh 1 ", "blocked found to crash"]


def test_kept_index_older_than_the_floor_dropped(driver, repository, keys, tmp_path):
    environment, folder = kept(tmp_path)
    entry = map_entry("bigass", DATA)
    mirrors = mirrors_document([mirror("m", repository.url + "/m", 1)])
    repository.publish("/old", keys, index_document([entry], serial=12), mirrors)
    repository.publish("/new", keys, index_document([entry], blacklist=blacklisted(entry), serial=13), mirrors)
    run(driver, "blocked", NOW, "-", "-", repository.url + "/old", environment=environment)
    old = (folder / "chupathingy_maps_index.signed").read_bytes()
    run(driver, "blocked", NOW, "-", "-", repository.url + "/new", environment=environment)
    (folder / "chupathingy_maps_index.signed").write_bytes(old)
    out = run(driver, "blocked", NOW, entry["sha256"], "-", environment=environment).splitlines()
    assert out == ["load 0 the kept index was dropped: its serial is 12, older than the 13 taken", "clear"]


def test_tampered_kept_index_dropped(driver, repository, keys, tmp_path):
    environment, folder = kept(tmp_path)
    entry = map_entry("bigass", DATA)
    repository.publish("", keys, index_document([entry], blacklist=blacklisted(entry)),
                       mirrors_document([mirror("m", repository.url + "/m", 1)]))
    run(driver, "blocked", NOW, "-", "-", repository.url, environment=environment)
    path = folder / "chupathingy_maps_index.signed"
    path.write_bytes(path.read_bytes().replace(b"found to crash", b"found to crasH"))
    out = run(driver, "blocked", NOW, entry["sha256"], "-", environment=environment).splitlines()
    assert out == ["load 0 the kept index was dropped: its signature does not match", "clear"]


def test_recovery_epoch_never_goes_back(driver, repository, keys, tmp_path):
    # (once a recovery-signed epoch is taken, epoch 0's primary-signed
    # documents never are again, whatever their serial, in this run or the
    # next)
    environment, _ = kept(tmp_path)
    entry = map_entry("bigass", DATA)
    mirrors = mirrors_document([mirror("m", repository.url + "/m", 1)])
    repository.publish("/recovered", keys, index_document([entry], blacklist=blacklisted(entry), serial=(1 << 24) + 1),
                       mirrors, index_key=keys[1][0])
    repository.publish("/primary", keys, index_document([entry], serial=500), mirrors)
    lines = download(driver, repository, tmp_path / "x", "bigass", entry["version"], NOW, "/primary",
                     repository.url + "/recovered", NOW)
    assert lines[0] == "refresh 1 "
    assert lines[1] == f"refresh 0 the index was refused: its serial is 500, older than the {(1 << 24) + 1} taken"
    run(driver, "blocked", NOW, "-", "-", repository.url + "/recovered", environment=environment)
    out = run(driver, "blocked", NOW, entry["sha256"], "-", repository.url + "/primary", environment=environment)
    assert out.splitlines() == [f"load 1 kept index {(1 << 24) + 1} (unexpired)",
                                f"refresh 0 the index was refused: its serial is 500, older than the {(1 << 24) + 1} "
                                "taken", "blocked found to crash"]


def test_release_keys_are_the_command_repositorys():
    # (ChupathingyCE/command's keys/maps.pub.json: the primary, then the
    # recovery key)
    header = (SRC / "maps_key.h").read_text()
    table = header[header.index("maps_public_keys[][32] =\n{"):header.index("#else")]
    found = ["".join(row) for row in
             (re.findall(r"0x([0-9a-f]{2})", block) for block in re.findall(r"\{([^{}]*)\}", table))]
    assert found == ["cf69b2e5ac2fea2d2418013bb35d1c04ee0352bc6bcc348e567e3592823246ad",
                     "5ab67b8db3b60211b29fac31408bc08cf924dd4e95ab2be78d308a4aeae07d05"]


# ---------- the HTTP client's waits (port/linux/src/posix_browser.c)

def browser_function(source, name):
    at = re.search(r"\n(static [^\n]*\b" + name + r"\([^\n]*\n(?:[^\n]*\n)*?\}\n)", source).group(1)
    return at


@pytest.mark.skipif(sys.platform == "win32", reason="the POSIX half")
def test_http_client_gives_up(tmp_path):
    """A connection to a host that doesn't answer is given up after its
    timeout, and a read never runs past the connection's deadline."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    browser = (SRC / "posix_browser.c").read_text()
    source = r"""
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#define TIMEOUT_MILLISECONDS 10000
#define CONNECT_TIMEOUT_MILLISECONDS 1000
#define MBEDTLS_ERR_SSL_TIMEOUT -0x6800
typedef int socket_type;
typedef struct { int fd; } mbedtls_net_context;
typedef struct { int unused; } mbedtls_ssl_context, mbedtls_ssl_config;
struct connection { int secure; long long deadline; mbedtls_net_context net; mbedtls_ssl_context ssl;
	mbedtls_ssl_config config; };
static uint32_t asked = 0;
static int mbedtls_net_recv_timeout(void *context, unsigned char *buffer, size_t size, uint32_t timeout)
{ (void)context; (void)buffer; (void)size; asked = timeout; return 0; }
""" + browser_function(browser, "monotonic_milliseconds") + browser_function(browser, "receive_until_deadline") + \
        browser_function(browser, "connect_within") + r"""
int main(void)
{
	struct connection connection;
	struct sockaddr_in address;
	long long start;
	unsigned char byte;
	int descriptor, connected;

	memset(&connection, 0, sizeof(connection));
	if (receive_until_deadline(&connection, &byte, 1, 10000) != 0 || asked != 10000) return 10;
	connection.deadline = monotonic_milliseconds() + 400;
	if (receive_until_deadline(&connection, &byte, 1, 10000) != 0 || asked > 400 || asked < 100) return 11;
	connection.deadline = monotonic_milliseconds() - 1;
	if (receive_until_deadline(&connection, &byte, 1, 10000) != MBEDTLS_ERR_SSL_TIMEOUT) return 12;
	/* (TEST-NET-1: nothing answers there) */
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(9);
	inet_pton(AF_INET, "192.0.2.1", &address.sin_addr);
	descriptor = socket(AF_INET, SOCK_STREAM, 0);
	start = monotonic_milliseconds();
	connected = connect_within(descriptor, (struct sockaddr *)&address, (int)sizeof(address));
	close(descriptor);
	if (connected || monotonic_milliseconds() - start > 3000) return 13;
	printf("ok\n");
	return 0;
}
"""
    (tmp_path / "waits.c").write_text(source)
    program = tmp_path / "waits"
    built = subprocess.run([cc, "-std=c11", "-Wall", "-Werror", "-o", str(program), str(tmp_path / "waits.c")],
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stderr[-3000:]
    result = subprocess.run([str(program)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0 and result.stdout == "ok\n", (result.returncode, result.stdout)


@pytest.mark.skipif(sys.platform == "win32", reason="the POSIX half")
def test_host_name_lookup_gives_up(tmp_path):
    """A resolver that never answers doesn't hold the caller: the lookup is
    given up after its timeout and left to finish on its own thread, which
    frees it; one that answers is taken."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    browser = (SRC / "posix_browser.c").read_text()
    lookups = browser[browser.index("/* ---------- host name lookups"):browser.index("/* a TCP connection over IPv4")]
    source = r"""
#define _DEFAULT_SOURCE
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#define RESOLVE_TIMEOUT_MILLISECONDS 300
static int freed;
static int fake_getaddrinfo(const char *host, const char *port, const struct addrinfo *hints, struct addrinfo **found)
{
	(void)port; (void)hints;
	if (!strcmp(host, "slow")) usleep(1500 * 1000);
	*found = calloc(1, sizeof(**found));
	return 0;
}
static void fake_freeaddrinfo(struct addrinfo *addresses) { free(addresses); __sync_fetch_and_add(&freed, 1); }
#define getaddrinfo fake_getaddrinfo
#define freeaddrinfo fake_freeaddrinfo
""" + browser_function(browser, "monotonic_milliseconds") + lookups + r"""
int main(void)
{
	struct addrinfo *addresses;
	long long start = monotonic_milliseconds();

	if (resolve_within("slow", "80", &addresses) != RESOLVE_TIMED_OUT || addresses) return 10;
	if (monotonic_milliseconds() - start > 1000) return 11;
	if (resolve_within("fast", "80", &addresses) != 0 || !addresses) return 12;
	freeaddrinfo(addresses);
	/* (the slow lookup's thread, done later, frees what it found) */
	usleep(2000 * 1000);
	if (freed != 2) return 13;
	printf("ok\n");
	return 0;
}
"""
    (tmp_path / "lookups.c").write_text(source)
    program = tmp_path / "lookups"
    built = subprocess.run([cc, "-std=c11", "-Wall", "-Werror", "-o", str(program), str(tmp_path / "lookups.c"),
                            "-lpthread"], capture_output=True, text=True)
    assert built.returncode == 0, built.stderr[-3000:]
    result = subprocess.run([str(program)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0 and result.stdout == "ok\n", (result.returncode, result.stdout)


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


def run_game(tmp_path, repository, name, version, network_test=None, have_map=False, downloads="always",
             maps_url=None, seconds=300):
    """the game's download of name (debug.network_test "download:", or
    network_test), its own data and save folders in tmp_path, the map in its
    maps_ce (a link to HALO_MAP_REPO_MAP, never a copy) when have_map: (its
    exit code, its log)"""
    data, save = tmp_path / "data", tmp_path / "save"
    (data / "maps_ce").mkdir(parents=True)
    save.mkdir()
    (data / "maps").symlink_to(Path(GAME_MAPS).resolve())
    if have_map:
        (data / "maps_ce" / f"{name}.map").symlink_to(Path(GAME_MAP).resolve())
    environment = dict(os.environ, HALO_DATA_ROOT=str(data), HALO_SAVE_ROOT=str(save), HALO_NULL_RENDERER="1",
                       HALO_HIDDEN_WINDOW="1", HALO_NET_MAP_DOWNLOADS=downloads,
                       HALO_NETWORK_TEST=network_test or f"download:{name}:{version}",
                       HALO_MAP_REPOSITORIES=repository.url + "/hac2",
                       HALO_NET_MAPS_URL=repository.url + "/ours" if maps_url is None else maps_url)
    try:
        result = subprocess.run([GAME], env=environment, capture_output=True, text=True, timeout=seconds)
        code, output = result.returncode, result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        # (still hosting when the time was up: no exit code)
        code, output = None, ""
    logs = sorted(tmp_path.rglob("debug*.txt"))
    log = "\n".join(path.read_text(errors="replace") for path in logs) + output
    return code, log, data / "maps_ce"


@pytest.fixture
def game_map():
    if not (GAME and GAME_KEY and GAME_MAP and GAME_MAPS):
        pytest.skip(GAME_SKIP)
    data = Path(GAME_MAP).read_bytes()
    return Path(GAME_MAP).stem.lower(), data


def publish_for_game(repository, name, data, blacklist=(), more=()):
    key = Path(GAME_KEY)
    entry = map_entry(name, data)
    index = dumps(index_document([*more, entry], blacklist=blacklist, issued=int(time.time()) - 60))
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


def test_game_takes_ours_from_a_large_index(repository, game_map, tmp_path):
    """An index larger than the game's plain requests read (64 KB): a
    repository of a few hundred maps. The game streams it into its save
    folder, checks it, keeps it, and downloads from it."""
    name, data = game_map
    entry = publish_for_game(repository, name, data, more=many_maps(400))
    assert len(repository.routes["/ours/index.json"]) > 3 * 65536
    code, log, folder = run_game(tmp_path, repository, name, entry["version"])
    assert code == 0, log[-3000:]
    assert (folder / f"{name}.map").read_bytes() == data
    assert "from Chupathingy maps" in log
    save = tmp_path / "save"
    assert (save / "chupathingy_maps_index.signed").read_bytes().endswith(repository.routes["/ours/index.json"])
    assert not list(save.glob("*.part"))


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


def test_game_refuses_to_host_a_blacklisted_map(repository, game_map, tmp_path):
    """A map on the blacklist that the player already has isn't hosted
    (debug.network_test "host:", which asks as the menus' map choice does,
    cache_files_map_host_refused): the game says why and quits with 2. A
    dedicated server holds every map it would play to the same blacklist
    (server_map_playable)."""
    name, data = game_map
    blacklist = [{"hash": map_hash(data), "kind": "map_hash", "name": name, "reason": "crashes the server on load",
                  "since": NOW}]
    publish_for_game(repository, name, data, blacklist)
    code, log, _ = run_game(tmp_path, repository, name, 0, network_test=f"host:{name}@ce", have_map=True)
    assert code == 2, log[-3000:]
    assert "Chupathingy won't host network games on it" in log
    assert "crashes the server on load" in log


@pytest.mark.parametrize("kind", ["map_hash", "sha256"])
def test_game_refuses_to_host_by_either_hash_even_with_downloads_off(repository, game_map, tmp_path, kind):
    """The blacklist names a file by its map hash or its SHA-256 (both read
    in one pass, map_file_hash.c), and is fetched and held with
    network.map_downloads "never" too: only the index is asked for then."""
    name, data = game_map
    digest = map_hash(data) if kind == "map_hash" else hashlib.sha256(data).hexdigest()
    blacklist = [{"hash": digest, "kind": kind, "name": name, "reason": "crashes the server on load", "since": NOW}]
    publish_for_game(repository, name, data, blacklist)
    code, log, _ = run_game(tmp_path, repository, name, 0, network_test=f"host:{name}@ce", have_map=True,
                            downloads="never")
    assert code == 2, log[-3000:]
    assert "crashes the server on load" in log
    assert not [path for path in repository.asked if "mirrors.json" in path]


def test_game_with_no_maps_url_asks_nothing(repository, game_map, tmp_path):
    """An empty network.maps_url turns Chupathingy maps off: nothing asked,
    nothing blocked."""
    name, data = game_map
    blacklist = [{"hash": map_hash(data), "kind": "map_hash", "name": name, "reason": "crashes the server on load",
                  "since": NOW}]
    publish_for_game(repository, name, data, blacklist)
    code, log, _ = run_game(tmp_path, repository, name, 0, network_test=f"host:{name}@ce", have_map=True,
                            maps_url="", seconds=40)
    # (hosting it, until the time is up)
    assert code is None, log[-3000:]
    assert "crashes the server on load" not in log
    assert not [path for path in repository.asked if path.startswith("/ours/")]
