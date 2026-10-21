"""The map downloader's checks (port/linux/src/map_archive.c, through
port/linux/tests/map_archive_test.c): map names, network.map_downloads's
words, the zip archives the community map repositories send (stored and
deflated maps, and archives that try to write elsewhere, claim more than
the limit, lie about their sizes or CRC-32, or are damaged), and the Custom
Edition header a downloaded map must have. Built with the build machine's
C compiler and the port's zlib, under AddressSanitizer and
UndefinedBehaviorSanitizer where it has them.

Also the hashed names other builds give the maps their hosts share
(<base>_<8 hex digits>, the digits the start of the map's whole-file hash):
the names read, and the hash against a reference written here in Python.

The last tests download real maps from the community map repository
(maps.halonet.net) and check them as the game does (sneak's whole-file hash
is the one a host named Sneak_af352483 by); they are skipped when the
repository can't be reached, or when HALO_MAP_DOWNLOAD_ONLINE=0."""
import io
import os
import shutil
import struct
import subprocess
import urllib.request
import hashlib
import zipfile
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"
ZLIB = ROOT / "port" / "third_party" / "zlib"
ZLIB_SOURCES = ["adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "zutil.c"]
MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"
TEST = ROOT / "port" / "linux" / "tests" / "map_archive_test.c"
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
LIMIT = 1024 * 1024 * 1024

# a small, well-known community Custom Edition map on the repository, and its
# header's checksum (what a host advertises as its version). Never one of
# Halo PC's own maps, which the downloader never asks for (STOCK_MAPS)
ONLINE_MAP = "sneak"
ONLINE_CHECKSUM = "4f8d6f69"
# a map a host shared by a hashed name (Sneak_af352483): the repository's
# file, its header's checksum and its whole-file hash's first digits
HASHED_MAP = "sneak"
HASHED_CHECKSUM = "4f8d6f69"
HASHED_PREFIX = "af352483"
ONLINE_URL = "https://maps.halonet.net/halonet/locator.php?format=zip&map={}&type=halom"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.fixture(scope="module")
def harness(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    directory = tmp_path_factory.mktemp("map_archive")
    binary = directory / "map_archive_test"
    objects = []
    # (zlib as the port builds it: prefixed, its own warnings its own)
    # (and Monocypher's BLAKE2b, for the hashed names)
    sanitize = SANITIZERS
    for source in [ZLIB / name for name in ZLIB_SOURCES] + [MONOCYPHER / "monocypher.c"]:
        obj = directory / (source.name + ".o")
        result = subprocess.run([cc, "-c", "-O1", "-g", "-w", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg",
                                 str(source), "-o", str(obj), *sanitize], capture_output=True, text=True)
        if result.returncode != 0 and sanitize:
            sanitize = []
            result = subprocess.run([cc, "-c", "-O1", "-g", "-w", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg",
                                     str(source), "-o", str(obj)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        objects.append(obj)
    result = subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *sanitize, "-DZ_PREFIX",
         "-Dz_errmsg=z_port_errmsg", f"-I{SOURCE}", f"-I{ZLIB}", f"-I{MONOCYPHER}", str(TEST),
         str(SOURCE / "map_archive.c"),
         *map(str, objects), "-o", str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return binary


def run(harness, *arguments):
    result = subprocess.run([str(harness), *map(str, arguments)], capture_output=True, text=True, timeout=300)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.strip()


def make_map(name="testmap", size=0x3000, version=609, checksum=0x1234ABCD, kind=1, file_length=None,
             tag_offset=0x800, tag_size=0x100, head=b"daeh", foot=b"toof", raw_name=None):
    """a Custom Edition cache file's header, and bytes after it"""
    header = bytearray(0x800)
    header[0:4] = head
    struct.pack_into("<I", header, 0x4, version)
    struct.pack_into("<I", header, 0x8, size if file_length is None else file_length)
    struct.pack_into("<I", header, 0x10, tag_offset)
    struct.pack_into("<I", header, 0x14, tag_size)
    field = raw_name if raw_name is not None else name.encode("ascii")
    header[0x20:0x20 + len(field)] = field
    header[0x40:0x40 + 10] = b"01.00.00.0"
    struct.pack_into("<H", header, 0x60, kind)
    struct.pack_into("<I", header, 0x64, checksum)
    header[0x7FC:0x800] = foot
    body = bytes((index * 7 + 3) & 0xFF for index in range(size - 0x800))
    return bytes(header) + body


def make_zip(entries, method=zipfile.ZIP_DEFLATED):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=method) as archive:
        for name, data in entries:
            archive.writestr(zipfile.ZipInfo(name, (2004, 4, 17, 18, 2, 0)), data, compress_type=method)
    return bytearray(buffer.getvalue())


def central(archive, name):
    """the offset of a central directory entry by its name"""
    offset = archive.find(b"PK\x01\x02")
    while offset >= 0:
        length = struct.unpack_from("<H", archive, offset + 28)[0]
        if bytes(archive[offset + 46:offset + 46 + length]) == name.encode():
            return offset
        offset = archive.find(b"PK\x01\x02", offset + 4)
    raise KeyError(name)


def write(tmp_path, data, name="archive.zip"):
    path = tmp_path / name
    path.write_bytes(bytes(data))
    return path


# ---------- names and the setting


@pytest.mark.parametrize("name", ["sneak", "bigass_v2.1", "Coldsnap", "a", "x" * 31, "ze-ro_1.2"])
def test_valid_names(harness, name):
    assert run(harness, "name", name) == "valid"


@pytest.mark.parametrize("name", [".hidden", "a..b", "../x", "a/b", "a\\b", "a b", "x" * 32, "c:x", "map%20",
                                  "bigassv2,104", "a?b", "a&type=halor"])
def test_invalid_names(harness, name):
    assert run(harness, "name", name) == "invalid"


def test_empty_name(harness):
    assert run(harness, "name", "") == "invalid"


@pytest.mark.parametrize("text,word", [("ask", "ask"), ("always", "always"), ("NEVER", "never"),
                                       ("Always", "always"), ("", "ask"), ("yes", "ask"), ("no", "ask")])
def test_setting_words(harness, text, word):
    assert run(harness, "setting", text) == word


def test_selftest(harness):
    assert run(harness, "selftest") == "ok"


def test_setting_is_documented_and_defaults_to_ask():
    config = (SOURCE / "port_config.c").read_text(encoding="utf-8")
    assert '{ "network.map_downloads", _config_string, "\\"ask\\""' in config
    assert '{ "network.map_downloads", { "ask", "always", "never", NULL }' in config
    readme = (ROOT / "port" / "linux" / "README.md").read_text(encoding="utf-8")
    assert "| `network.map_downloads` |" in readme


# ---------- archives


@pytest.mark.parametrize("method", [zipfile.ZIP_DEFLATED, zipfile.ZIP_STORED])
def test_extracts_the_map(harness, tmp_path, method):
    data = make_map()
    path = write(tmp_path, make_zip([("readme.txt", b"hello"), ("testmap.map", data)], method))
    out = tmp_path / "out.map"
    assert run(harness, "extract", path, "testmap", out) == f"ok {len(data)} {zlib.crc32(data):08x}"
    assert out.read_bytes() == data


def test_entry_name_in_any_case(harness, tmp_path):
    data = make_map()
    path = write(tmp_path, make_zip([("TestMap.MAP", data)]))
    assert run(harness, "extract", path, "testmap").startswith("ok ")


def test_no_such_map(harness, tmp_path):
    path = write(tmp_path, make_zip([("other.map", make_map("other"))]))
    assert "has no testmap.map" in run(harness, "extract", path, "testmap")


def test_map_in_a_folder_is_not_taken(harness, tmp_path):
    path = write(tmp_path, make_zip([("maps/testmap.map", make_map())]))
    assert "has no testmap.map" in run(harness, "extract", path, "testmap")


@pytest.mark.parametrize("evil", ["../testmap.map", "../../etc/passwd", "/testmap.map", "\\testmap.map",
                                  "C:/testmap.map", "maps/../../testmap.map", "maps\\..\\..\\x.map", ".."])
def test_paths_outside_the_folder_refuse_the_archive(harness, tmp_path, evil):
    path = write(tmp_path, make_zip([("testmap.map", make_map()), (evil, b"evil")]))
    output = run(harness, "extract", path, "testmap")
    assert output.startswith("refused: the archive names a path outside its folder"), output
    assert output.endswith("(0 bytes written)")


def test_twice_the_map(harness, tmp_path):
    path = write(tmp_path, make_zip([("testmap.map", make_map()), ("TESTMAP.map", make_map())]))
    assert "more than once" in run(harness, "extract", path, "testmap")


def test_stated_size_over_the_limit(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    struct.pack_into("<I", archive, central(archive, "testmap.map") + 24, LIMIT + 1)
    output = run(harness, "extract", write(tmp_path, archive), "testmap")
    assert "MB unpacked (at most 1024 MB)" in output and output.endswith("(0 bytes written)")


def test_unpacking_past_the_stated_size_stops(harness, tmp_path):
    # (a bomb: highly compressible data that says it is small)
    data = make_map(size=4 * 1024 * 1024)
    data = data[:0x800] + bytes(len(data) - 0x800)
    archive = make_zip([("testmap.map", data)])
    struct.pack_into("<I", archive, central(archive, "testmap.map") + 24, 0x10000)
    output = run(harness, "extract", write(tmp_path, archive), "testmap")
    assert "unpacks to more than its stated 65536 bytes" in output
    written = int(output.rsplit("(", 1)[1].split()[0])
    assert written <= 0x10000


def test_unpacking_short_of_the_stated_size(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    struct.pack_into("<I", archive, central(archive, "testmap.map") + 24, 0x3000 + 100)
    assert "not its stated" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_crc_mismatch(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    offset = central(archive, "testmap.map") + 16
    struct.pack_into("<I", archive, offset, struct.unpack_from("<I", archive, offset)[0] ^ 1)
    assert "CRC-32" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_damaged_deflate(harness, tmp_path):
    data = make_map()
    archive = make_zip([("testmap.map", data)])
    local = archive.find(b"PK\x03\x04")
    start = local + 30 + struct.unpack_from("<H", archive, local + 26)[0] + struct.unpack_from("<H", archive, local + 28)[0]
    for index in range(start + 10, start + 60):
        archive[index] ^= 0x5A
    assert run(harness, "extract", write(tmp_path, archive), "testmap").startswith("refused:")


def test_cut_short(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    assert "not a zip archive" in run(harness, "extract", write(tmp_path, archive[:len(archive) // 2]), "testmap")


def test_data_outside_the_archive(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    struct.pack_into("<I", archive, central(archive, "testmap.map") + 20, 0x7FFFFFF0)
    assert "lies outside it" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_local_header_outside_the_archive(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    struct.pack_into("<I", archive, central(archive, "testmap.map") + 42, 0x7FFFFFF0)
    assert "entry is damaged" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_encrypted(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    offset = central(archive, "testmap.map") + 8
    struct.pack_into("<H", archive, offset, struct.unpack_from("<H", archive, offset)[0] | 1)
    assert "encrypted" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_other_compression(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    struct.pack_into("<H", archive, central(archive, "testmap.map") + 10, 12)
    assert "method 12" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_zip64_marker(harness, tmp_path):
    archive = make_zip([("testmap.map", make_map())])
    end = archive.rfind(b"PK\x05\x06")
    struct.pack_into("<HH", archive, end + 8, 0xFFFF, 0xFFFF)
    assert "ZIP64" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_too_many_entries(harness, tmp_path):
    archive = make_zip([(f"file{index}.txt", b"x") for index in range(300)] + [("testmap.map", make_map())],
                       zipfile.ZIP_STORED)
    assert "entries (at most 256)" in run(harness, "extract", write(tmp_path, archive), "testmap")


def test_cancel_stops(harness, tmp_path):
    data = make_map(size=2 * 1024 * 1024)
    path = write(tmp_path, make_zip([("testmap.map", data)]))
    output = run(harness, "extract", path, "testmap", "-", 1)
    assert output.startswith("refused: cancelled")


def test_random_damage_never_crashes(harness, tmp_path):
    import random

    generator = random.Random(205)
    pristine = make_zip([("readme.txt", b"hello" * 50), ("testmap.map", make_map())])
    for round_index in range(200):
        archive = bytearray(pristine)
        for _ in range(generator.randint(1, 12)):
            archive[generator.randrange(len(archive))] = generator.randrange(256)
        output = run(harness, "extract", write(tmp_path, archive, f"r{round_index}.zip"), "testmap")
        assert output.startswith("ok ") or output.startswith("refused:"), output


# ---------- headers


def header(harness, tmp_path, data, name="testmap", version="0"):
    return run(harness, "header", write(tmp_path, data, "test.map"), name, version)


def test_header_valid(harness, tmp_path):
    assert header(harness, tmp_path, make_map()) == "ok"
    assert header(harness, tmp_path, make_map(), "TESTMAP") == "ok"
    assert header(harness, tmp_path, make_map(), version="1234abcd") == "ok"
    # (Invader's maps may give their length as 0)
    assert header(harness, tmp_path, make_map(file_length=0)) == "ok"


@pytest.mark.parametrize("kwargs,why", [
    ({"head": b"xxxx"}, "not a cache file"),
    ({"foot": b"xxxx"}, "not a cache file"),
    ({"version": 7}, "version 7"),
    ({"version": 5}, "version 5"),
    ({"kind": 0}, "not a multiplayer map"),
    ({"name": "othermap"}, "named othermap inside"),
    ({"raw_name": b"x" * 32}, "does not end"),
    ({"file_length": 0x5000}, "header says"),
    ({"file_length": 0x100}, "header says"),
    ({"tag_offset": 0x100}, "tag data"),
    ({"tag_offset": 0x2F00, "tag_size": 0x200}, "tag data"),
    ({"tag_offset": 0x900, "tag_size": 0xFFFFFFFF}, "tag data"),
])
def test_header_refused(harness, tmp_path, kwargs, why):
    output = header(harness, tmp_path, make_map(**kwargs))
    assert output.startswith("refused:") and why in output, output


def test_header_other_version_than_the_hosts(harness, tmp_path):
    output = header(harness, tmp_path, make_map(), version="deadbeef")
    assert "another version of the map" in output


def test_header_too_small(harness, tmp_path):
    assert header(harness, tmp_path, make_map()[:0x400]).startswith("refused:")


# ---------- hashed names


def reference_hash(data):
    """the whole-file hash other builds name shared maps by, written out
    plainly: BLAKE2b-256 of "OpenCE map 1", the size (4 bytes, little-endian)
    and each 0x40000-byte piece's BLAKE2b-256"""
    whole = hashlib.blake2b(digest_size=32)
    whole.update(b"OpenCE map 1" + struct.pack("<I", len(data)))
    for offset in range(0, len(data), 0x40000):
        whole.update(hashlib.blake2b(data[offset:offset + 0x40000], digest_size=32).digest())
    return whole.hexdigest()


@pytest.mark.parametrize("name,base,digits", [
    ("Sneak_af352483", "Sneak", "af352483"),
    ("sneak_AF352483", "sneak", "af352483"),
    ("a_00000000", "a", "00000000"),
    ("bigass_v2_ffffffff", "bigass_v2", "ffffffff"),
    ("x_12345678_9abcdef0", "x_12345678", "9abcdef0"),
])
def test_hashed_names(harness, name, base, digits):
    assert run(harness, "hashed", name) == f"hashed {base} {digits}"


@pytest.mark.parametrize("name", [
    "sneak", "Sneakaf352483", "_af352483", "af352483", "Sneak_af35248", "Sneak_af3524830", "Sneak_af35248g",
    "Sneak-af352483", "Sneak_ af35248", "Sneak_0xaf3524", "", "Sneak_",
])
def test_plain_names(harness, name):
    assert run(harness, "hashed", name) == "plain"


@pytest.mark.parametrize("name,display", [("Sneak_af352483", "sneak"), ("Bigass_v2_DEADBEEF", "bigass_v2"),
                                          ("Sneak", "Sneak"), ("coldsnap", "coldsnap")])
def test_display_names(harness, name, display):
    assert run(harness, "display", name) == display


@pytest.mark.parametrize("size", [0, 1, 0x800, 0x3FFFF, 0x40000, 0x40001, 3 * 0x40000 + 5])
def test_whole_file_hash(harness, tmp_path, size):
    data = bytes((index * 31 + (index >> 9)) & 0xFF for index in range(size))
    path = write(tmp_path, data, "hashed.map")
    expected = reference_hash(data)
    assert run(harness, "hash", path) == f"{expected} {expected[:8]}"


# ---------- map versions (network version 29)


def test_version_is_the_hash_or_the_checksum(harness, tmp_path):
    """a host of network version 29 sends the hash's first 32 bits, read
    big-endian; older hosts the header checksum; 0 is any"""
    data = make_map(checksum=0x1234ABCD, size=0x50000)
    path = write(tmp_path, data, "versioned.map")
    hash_version = reference_hash(data)[:8]
    assert run(harness, "version", path, hash_version) == f"{hash_version} 1234abcd host's"
    assert run(harness, "version", path, "1234abcd") == f"{hash_version} 1234abcd host's"
    assert run(harness, "version", path, "0") == f"{hash_version} 1234abcd host's"
    assert run(harness, "version", path, "deadbeef") == f"{hash_version} 1234abcd other"
    # (the bytes of the hash's version in the order they are written, not a
    # little-endian read of them)
    swapped = bytes.fromhex(hash_version)[::-1].hex()
    if swapped != hash_version:
        assert run(harness, "version", path, swapped).endswith(" other")


@pytest.mark.parametrize("hash_hex,version", [
    ("af352483" + "00" * 28, "af352483"),
    ("0000000100" + "ff" * 27, "00000001"),
    # (a hash beginning with 32 zero bits is version 1: 0 means any)
    ("00000000" + "ab" * 28, "00000001"),
    ("ffffffff" + "00" * 28, "ffffffff"),
])
def test_hash_version(harness, hash_hex, version):
    assert run(harness, "hashversion", hash_hex) == version


def test_whole_file_hash_sees_every_byte(harness, tmp_path):
    data = bytearray(make_map(size=0x90000))
    first = run(harness, "hash", write(tmp_path, data, "a.map"))
    data[0x8FFFF] ^= 1
    assert run(harness, "hash", write(tmp_path, data, "b.map")) != first


# ---------- Halo PC's own maps: never downloaded

STOCK_MAPS = ["beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout", "damnation", "dangercanyon",
              "deathisland", "gephyrophobia", "hangemhigh", "icefields", "infinity", "longest", "prisoner", "putput",
              "ratrace", "sidewinder", "timberland", "wizard",
              "a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40"]
STOCK_FILES = ["ui", "bitmaps", "sounds", "loc"]


@pytest.mark.parametrize("name", STOCK_MAPS + [name.upper() for name in STOCK_MAPS[:3]] + ["BloodGulch"])
def test_stock_maps(harness, name):
    kind, message = run(harness, "stock", name).split("\n")
    assert kind == "1"
    base = name.lower()
    assert message == (f"{base}.map comes with Halo PC. Copy it "
                       "from your Halo PC or Custom Edition install into maps_ce, then join again.")


@pytest.mark.parametrize("name", STOCK_FILES + ["UI", "Bitmaps"])
def test_stock_files(harness, name):
    kind, message = run(harness, "stock", name).split("\n")
    assert kind == "2"
    assert message.startswith(f"{name.lower()}.map comes with Halo PC.")


@pytest.mark.parametrize("name,base", [("bloodgulch_1234abcd", "bloodgulch"), ("BloodGulch_DEADBEEF", "bloodgulch"),
                                       ("ui_00000000", "ui"), ("putput_af2f0b84", "putput")])
def test_stock_hashed_names(harness, name, base):
    output = run(harness, "stock", name).split("\n")
    assert output[0] in ("1", "2")
    assert output[1].startswith(f"{base}.map comes with Halo PC.")


@pytest.mark.parametrize("name", ["sneak", "coldsnap", "Sneak_af352483", "bloodgulch2", "bloodgulch_v2", "hugeass",
                                  "bigass_v2.1", "putputs", "a100", "uix", "bloodgulch_1234abc", ""])
def test_community_maps_are_not_stock(harness, name):
    assert run(harness, "stock", name) == "0"


def test_downloader_never_asks_for_stock_maps():
    """map_download_possible and map_download_start both refuse them, before
    any repository is asked"""
    download = (SOURCE / "map_download.c").read_text(encoding="utf-8")
    possible = download[download.index("int map_download_possible("):download.index("int map_download_start(")]
    assert "!map_archive_stock_name(name)" in possible
    start = download[download.index("int map_download_start("):]
    refused = start.index("map_archive_stock_name(name)")
    assert refused < start.index("pthread_create")
    assert "is one of the game's own maps; not downloaded" in start[:start.index("pthread_create")]


def test_stock_message_in_the_missing_map_paths():
    cache = (ROOT / "source" / "cache" / "cache_files.c").read_text(encoding="utf-8")
    assert "Install this map yourself" in cache and "map_archive_stock_message(file" in cache
    browser = (ROOT / "port" / "linux" / "game" / "browser_screen.c").read_text(encoding="utf-8")
    assert "MAP_ARCHIVE_STOCK_TITLE" in browser and "%s.map comes with Halo PC." in browser


# ---------- the real repository


def test_real_repository_map(harness, tmp_path):
    if os.environ.get("HALO_MAP_DOWNLOAD_ONLINE") == "0":
        pytest.skip("HALO_MAP_DOWNLOAD_ONLINE=0")
    request = urllib.request.Request(ONLINE_URL.format(ONLINE_MAP), headers={"User-Agent": "ChupathingyCE tests"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read(LIMIT + 1)
    except OSError as failure:
        pytest.skip(f"the repository can't be reached: {failure}")
    assert len(data) <= LIMIT
    archive = write(tmp_path, data, f"{ONLINE_MAP}.zip")
    out = tmp_path / f"{ONLINE_MAP}.map"
    output = run(harness, "extract", archive, ONLINE_MAP, out)
    assert output.startswith("ok "), output
    assert run(harness, "header", out, ONLINE_MAP, ONLINE_CHECKSUM) == "ok"
    assert "another version" in run(harness, "header", out, ONLINE_MAP, "00000001")


def download(name, tmp_path):
    if os.environ.get("HALO_MAP_DOWNLOAD_ONLINE") == "0":
        pytest.skip("HALO_MAP_DOWNLOAD_ONLINE=0")
    request = urllib.request.Request(ONLINE_URL.format(name), headers={"User-Agent": "ChupathingyCE tests"})
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            data = response.read(LIMIT + 1)
    except OSError as failure:
        pytest.skip(f"the repository can't be reached: {failure}")
    assert len(data) <= LIMIT
    return write(tmp_path, data, f"{name}.zip")


def test_real_hashed_map(harness, tmp_path):
    """the repository's sneak.map is the map a host named Sneak_af352483"""
    archive = download(HASHED_MAP, tmp_path)
    out = tmp_path / f"{HASHED_MAP}.map"
    assert run(harness, "extract", archive, HASHED_MAP, out).startswith("ok ")
    assert run(harness, "header", out, HASHED_MAP, HASHED_CHECKSUM) == "ok"
    digest, prefix = run(harness, "hash", out).split()
    assert prefix == HASHED_PREFIX
    # (what a host of network version 29 sends as its version, and what an
    # older one sends: both are this map)
    assert run(harness, "version", out, HASHED_PREFIX) == f"{HASHED_PREFIX} {HASHED_CHECKSUM} host's"
    assert run(harness, "version", out, HASHED_CHECKSUM) == f"{HASHED_PREFIX} {HASHED_CHECKSUM} host's"
    assert digest == reference_hash(out.read_bytes())
    assert run(harness, "hashed", "Sneak_" + HASHED_PREFIX) == f"hashed Sneak {HASHED_PREFIX}"


# ---------- a host moving its game on to a map the client lacks

# Two copies of the game on this machine's loopback addresses (opt in:
# HALO_MAP_DOWNLOAD_E2E names the game's binary, HALO_MAP_DOWNLOAD_E2E_DATA
# the NTSC data's folder: maps/ with the Xbox maps and maps/ce/ with Custom
# Edition's resource maps and putput; maps_ce/sneak.map beside it; Linux
# has every 127.x address, macOS needs `sudo ifconfig lo0 alias 127.0.0.2`
# and .3). The host's network test plays Blood Gulch, then the next game on
# a Custom Edition map the client doesn't have (port/linux/game/network_test.c,
# "host:bloodgulch+<map>@ce"); the client, as network.map_downloads says,
# leaves, downloads it from the real repository and joins the same host again
# (browser_screen.c), or says the map is missing. Each copy's home, save root
# and data are folders of the test's own.

E2E_BINARY = os.environ.get("HALO_MAP_DOWNLOAD_E2E")
E2E_DATA = os.environ.get("HALO_MAP_DOWNLOAD_E2E_DATA")
E2E_SECONDS = 80


def e2e_copy(folder, address, other, test, setting, ce_map, ce_source, changed=False):
    data = folder / "data"
    for sub in ("home", "saves", "data/maps", "data/maps_ce"):
        (folder / sub).mkdir(parents=True)
    maps = Path(E2E_DATA) / "maps"
    for entry in maps.glob("*.map"):
        if entry.name != "putput.map":
            (data / "maps" / entry.name).symlink_to(entry)
    for name in ("bitmaps.map", "sounds.map", "loc.map"):
        (data / "maps_ce" / name).symlink_to(maps / "ce" / name)
    if ce_map:
        target = data / "maps_ce" / f"{ce_map}.map"
        if changed:
            # (another version than the repository's: its header's checksum)
            contents = bytearray(ce_source.read_bytes())
            contents[0x64] ^= 0x5A
            target.write_bytes(contents)
        else:
            target.symlink_to(ce_source)
    env = {"PATH": "/usr/bin:/bin", "HOME": str(folder / "home"), "HALO_DATA_ROOT": str(data),
           "HALO_SAVE_ROOT": str(folder / "saves"), "HALO_UPDATE_AUTO": "false", "HALO_DISCORD_APPLICATION": "",
           "HALO_NET_ONLINE": "false", "HALO_NET_LIST_GAMES": "false", "HALO_NET_REPORT_GAMES": "false",
           "HALO_NET_BROWSER": "http://127.0.0.1:9", "HALO_NET_JOIN_FROM_CLIPBOARD": "false",
           "HALO_NET_ALLOW_UPNP": "false", "HALO_NET_ADDRESS": address, "HALO_NET_BROADCAST": other,
           "HALO_NETWORK_TEST": test, "HALO_NETWORK_TEST_START": "4", "HALO_NETWORK_TEST_KILL": "3",
           "HALO_NETWORK_TEST_SCORE": "2", "HALO_NET_MAP_DOWNLOADS": setting,
           "SDL_VIDEO_DRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy"}
    output = open(folder / "out.log", "wb")
    return subprocess.Popen([E2E_BINARY], cwd=data, env=env, stdout=output, stderr=subprocess.STDOUT,
                            start_new_session=True)


@pytest.mark.skipif(not E2E_BINARY or not E2E_DATA, reason="HALO_MAP_DOWNLOAD_E2E and _DATA not set")
@pytest.mark.parametrize("scenario,setting", [("download", "always"), ("never", "never"), ("other_version", "always"),
                                              ("stock", "always")])
def test_host_changes_map(tmp_path, scenario, setting):
    import signal
    import time

    ce_map = "putput" if scenario == "stock" else ONLINE_MAP
    ce_source = (Path(E2E_DATA) / "maps" / "ce" / "putput.map" if scenario == "stock" else
                 Path(E2E_DATA) / "maps_ce" / f"{ONLINE_MAP}.map")
    if not ce_source.exists():
        pytest.skip(f"no {ce_source}")
    host = e2e_copy(tmp_path / "host", "127.0.0.2", "127.0.0.3", f"host:bloodgulch+{ce_map}@ce:slayer,slayer,slayer",
                    setting, ce_map, ce_source, changed=scenario == "other_version")
    time.sleep(3)
    client = e2e_copy(tmp_path / "client", "127.0.0.3", "127.0.0.2", "join", setting, None, None)
    try:
        time.sleep(E2E_SECONDS)
    finally:
        for process in (host, client):
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
    log = (tmp_path / "client" / "out.log").read_text(errors="replace")
    if "network test: joining" not in log:
        pytest.skip("the client found no host (loopback addresses 127.0.0.2 and .3?)")
    if scenario == "download":
        if "map download: GET" in log and "installed in maps_ce" not in log:
            pytest.skip("the repository can't be reached")
        assert f"(host's checksum {ONLINE_CHECKSUM})" in log
        assert f"map download: {ce_map}.map in place: joining the game again" in log
        assert f"Delta Peer: map identity: {ce_map}@ce is the host's" in log
    elif scenario == "never":
        assert f"map missing: You don't have the HALO PC map {ce_map}." in log
        assert "map download: GET" not in log
    elif scenario == "other_version":
        if "map download: GET" in log and "unpacked" not in log:
            pytest.skip("the repository can't be reached")
        assert "another version of the map" in log
        assert "the game is not joined again" in log
        assert "joining the game again" not in log
    else:
        assert "map download: putput is one of the game's own maps; not downloaded" in log
        assert "map download: GET" not in log
