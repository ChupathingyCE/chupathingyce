"""The zip reader a mod archive (.pk3, .zip: docs/mods.md) goes through,
map_archive_entries and map_archive_extract_entry (port/linux/src/map_archive.c),
run through port/linux/tests/map_archive_test.c's "entries" and "entry"
modes on archives made here: a good one listed and unpacked whole (stored
and deflated), names that leave the folder refused (.., absolute, a drive, a
backslash), an entry held more than once, an entry over its size cap, a
deflate stream that unpacks to more than its stated size, a wrong CRC-32, a
damaged central directory, a ZIP64 marker, and the entry cap. Built as
tools/test_map_download.py builds the same test, with the port's zlib."""
import shutil
import struct
import subprocess
import zipfile
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"
TEST = ROOT / "port" / "linux" / "tests" / "map_archive_test.c"
ZLIB = ROOT / "port" / "third_party" / "zlib"
MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"
ZLIB_SOURCES = ["adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "zutil.c"]

SOUND = b"\0" * 0x24 + b"snd!" + b"\0" * 0x18


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
    directory = tmp_path_factory.mktemp("mod_archives")
    binary = directory / "map_archive_test"
    objects = []
    for source in [ZLIB / name for name in ZLIB_SOURCES] + [MONOCYPHER / "monocypher.c"]:
        obj = directory / (source.name + ".o")
        result = subprocess.run([cc, "-c", "-O1", "-g", "-w", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg", str(source),
                                 "-o", str(obj)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        objects.append(obj)
    result = subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg",
         f"-I{SOURCE}", f"-I{ZLIB}", f"-I{MONOCYPHER}", str(TEST), str(SOURCE / "map_archive.c"), *map(str, objects),
         "-o", str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return binary


def run(harness, *arguments):
    result = subprocess.run([str(harness), *map(str, arguments)], capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.strip()


def make_zip(path, entries, compression=zipfile.ZIP_DEFLATED):
    """entries: (name, bytes) in order; the same name may appear twice."""
    with zipfile.ZipFile(path, "w", compression) as archive:
        for name, data in entries:
            archive.writestr(name, data)
    return path


def test_entries_listed_and_unpacked(harness, tmp_path):
    sound = SOUND + bytes(range(256)) * 40
    archive = make_zip(tmp_path / "mod.pk3", [("mod.toml", b'name = "x"\n'),
                                                ("tags/sound/music/a.sound", sound),
                                                ("tags/sound/sfx/", b""),
                                                ("tags/Sound/SFX/b.sound", b"stored small")])
    listing = run(harness, "entries", archive)
    assert listing.endswith("ok")
    assert f"entry {len(sound)} tags/sound/music/a.sound" in listing
    assert "entry 0 tags/sound/sfx/" in listing
    assert "entry 12 tags/Sound/SFX/b.sound" in listing
    out = run(harness, "entry", archive, "tags/sound/music/a.sound", 1 << 20)
    assert out == f"ok {len(sound)} {zlib.crc32(sound):08x}"
    # (any case)
    out = run(harness, "entry", archive, "TAGS/sound/sfx/B.SOUND", 1 << 20)
    assert out == f"ok 12 {zlib.crc32(b'stored small'):08x}"
    assert "has no tags/none.sound" in run(harness, "entry", archive, "tags/none.sound", 1 << 20)


def test_stored_entries(harness, tmp_path):
    data = b"plain bytes" * 100
    archive = make_zip(tmp_path / "stored.zip", [("tags/x.sound", data)], zipfile.ZIP_STORED)
    assert run(harness, "entry", archive, "tags/x.sound", len(data)) == f"ok {len(data)} {zlib.crc32(data):08x}"


def rename_entry(path, placeholder, name):
    """Writes a raw entry name over its placeholder of the same length in
    the local and central headers (zipfile itself normalizes names)."""
    assert len(placeholder) == len(name)
    data = path.read_bytes().replace(placeholder.encode(), name.encode())
    path.write_bytes(data)


@pytest.mark.parametrize("name", ["../tags/x.sound", "tags/../../x.sound", "/tags/x.sound", "\\tags\\x.sound",
                                  "c:\\tags\\x.sound", "tags/..\\x.sound"])
def test_names_leaving_the_folder_refuse_the_archive(harness, tmp_path, name):
    placeholder = "p" * len(name)
    archive = make_zip(tmp_path / "bad.pk3", [("tags/good.sound", b"fine"), (placeholder, b"bad")])
    rename_entry(archive, placeholder, name)
    listing = run(harness, "entries", archive)
    # (the entries before it are given, then the walk stops: port_mods.c
    # takes back what it listed when the walk fails)
    assert listing.splitlines()[-1] == "refused: the archive names a path outside its folder"
    assert "refused:" in run(harness, "entry", archive, "tags/good.sound", 1 << 20)


def test_duplicate_entry_refused(harness, tmp_path):
    archive = make_zip(tmp_path / "twice.pk3", [("tags/x.sound", b"one"), ("tags/x.sound", b"two")])
    assert "more than once" in run(harness, "entry", archive, "tags/x.sound", 1 << 20)
    # (listed twice all the same: the walk names what is there)
    assert run(harness, "entries", archive).count("tags/x.sound") == 2


def test_entry_over_its_cap(harness, tmp_path):
    data = b"z" * 5000
    archive = make_zip(tmp_path / "big.pk3", [("tags/x.sound", data)])
    assert "at most 4999" in run(harness, "entry", archive, "tags/x.sound", 4999)
    assert run(harness, "entry", archive, "tags/x.sound", 5000).startswith("ok 5000")


def patch_sizes(path, name, uncompressed=None, crc=None):
    """Rewrites an entry's stated size or CRC in the local and central headers."""
    data = bytearray(path.read_bytes())
    encoded = name.encode()
    # local header
    local = data.find(b"PK\x03\x04")
    while local >= 0:
        name_length = struct.unpack_from("<H", data, local + 26)[0]
        if data[local + 30:local + 30 + name_length] == encoded:
            if crc is not None:
                struct.pack_into("<I", data, local + 14, crc)
            if uncompressed is not None:
                struct.pack_into("<I", data, local + 22, uncompressed)
        local = data.find(b"PK\x03\x04", local + 4)
    central = data.find(b"PK\x01\x02")
    while central >= 0:
        name_length = struct.unpack_from("<H", data, central + 28)[0]
        if data[central + 46:central + 46 + name_length] == encoded:
            if crc is not None:
                struct.pack_into("<I", data, central + 16, crc)
            if uncompressed is not None:
                struct.pack_into("<I", data, central + 24, uncompressed)
        central = data.find(b"PK\x01\x02", central + 4)
    path.write_bytes(data)


def test_deflate_longer_than_stated(harness, tmp_path):
    data = b"the same bytes over and over " * 200
    archive = make_zip(tmp_path / "long.pk3", [("tags/x.sound", data)])
    patch_sizes(archive, "tags/x.sound", uncompressed=100)
    out = run(harness, "entry", archive, "tags/x.sound", 1 << 20)
    assert "unpacks to more than its stated 100" in out


def test_wrong_crc(harness, tmp_path):
    data = b"checked bytes" * 50
    archive = make_zip(tmp_path / "crc.pk3", [("tags/x.sound", data)])
    patch_sizes(archive, "tags/x.sound", crc=(zlib.crc32(data) ^ 0xFFFF) & 0xFFFFFFFF)
    assert "CRC-32" in run(harness, "entry", archive, "tags/x.sound", 1 << 20)


def test_damaged_directory(harness, tmp_path):
    archive = make_zip(tmp_path / "damaged.pk3", [("tags/x.sound", b"abc")])
    data = bytearray(archive.read_bytes())
    central = data.find(b"PK\x01\x02")
    data[central] = ord("X")
    archive.write_bytes(data)
    assert "directory is damaged" in run(harness, "entries", archive)
    (tmp_path / "zeros.pk3").write_bytes(bytes(100))
    assert "not a zip" in run(harness, "entries", tmp_path / "zeros.pk3")


def test_zip64_marker_refused(harness, tmp_path):
    archive = make_zip(tmp_path / "zip64.pk3", [("tags/x.sound", b"abc")])
    data = bytearray(archive.read_bytes())
    end = data.rfind(b"PK\x05\x06")
    struct.pack_into("<I", data, end + 16, 0xFFFFFFFF)
    archive.write_bytes(data)
    assert "ZIP64" in run(harness, "entries", archive)


def test_entry_cap(harness, tmp_path):
    archive = make_zip(tmp_path / "many.pk3", [(f"tags/{index}.sound", b"x") for index in range(8193)],
                       zipfile.ZIP_STORED)
    assert "at most 8192" in run(harness, "entries", archive)
