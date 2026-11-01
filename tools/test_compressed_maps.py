"""Halo PC maps compressed with Chimera's chimera-compress
(port/linux/src/map_compressed.c, through port/linux/tests/map_compressed_test.c):
maps compressed here as chimera-compress compresses them
(tools/chimera_compress.py) expand to the original's bytes; several frames
are read; damaged, cut-off, oversized and padded files, and maps that are
not compressed, are refused. Built with the build machine's C compiler and
the port's zstd (port/third_party/zstd), under AddressSanitizer and
UndefinedBehaviorSanitizer where it has them. Compressing needs the zstd
program; those tests are skipped without it.

A real Custom Edition map is tried too when one is found: HALO_TEST_CE_MAP,
else sneak.map in the data root's maps_ce (assets/maps_ce, or the data root
HALO_DATA_ROOT names).

Also checked: only the desktop builds have the reader (the consoles that
play Halo PC maps, the Switch, take CUSTOM_EDITION_DEFINES and must not
get zstd), and the reader's files are whole behind HALO_COMPRESSED_MAPS for
the consoles that compile port/linux/src."""
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import chimera_compress  # noqa: E402

SOURCE = ROOT / "port" / "linux" / "src"
ZSTD = ROOT / "port" / "third_party" / "zstd"
TEST = ROOT / "port" / "linux" / "tests" / "map_compressed_test.c"
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
ZSTD_DEFINES = ["-DZSTD_DISABLE_ASM=1", "-DZSTD_LEGACY_SUPPORT=0", "-DZSTD_TRACE=0", "-DDEBUGLEVEL=0"]


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.fixture(scope="module")
def harness(tmp_path_factory):
    sys.path.insert(0, str(ROOT))
    from tools.linux_build import ZSTD_SOURCES

    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    directory = tmp_path_factory.mktemp("map_compressed")
    binary = directory / "map_compressed_test"
    objects = []
    sanitize = SANITIZERS
    for name in ZSTD_SOURCES:
        source = ZSTD / name
        obj = directory / (source.name + ".o")
        command = [cc, "-c", "-O1", "-g", "-w", "-std=gnu11", *ZSTD_DEFINES, str(source), "-o", str(obj)]
        result = subprocess.run(command + sanitize, capture_output=True, text=True)
        if result.returncode != 0 and sanitize:
            sanitize = []
            result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        objects.append(obj)
    result = subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *sanitize, "-DHALO_COMPRESSED_MAPS",
         f"-I{SOURCE}", f"-I{ZSTD / 'lib'}", str(TEST), str(SOURCE / "map_compressed.c"),
         *map(str, objects), "-o", str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return binary


def run(harness, *arguments):
    result = subprocess.run([str(harness), *map(str, arguments)], capture_output=True, text=True, timeout=300)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.strip()


needs_zstd = pytest.mark.skipif(not chimera_compress.zstd_program(), reason="no zstd program")


def make_map(size=0x40000, version=609, name=b"testmap", checksum=0x1234ABCD):
    """a Custom Edition cache file's header as chimera-compress keeps it,
    and bytes after it that compress, but not to nothing"""
    header = bytearray(0x800)
    header[0:4] = b"daeh"
    struct.pack_into("<I", header, 0x4, version)
    struct.pack_into("<I", header, 0x8, size)
    struct.pack_into("<I", header, 0x10, 0x800)
    struct.pack_into("<I", header, 0x14, 0x100)
    header[0x20:0x20 + len(name)] = name
    header[0x40:0x40 + 10] = b"01.00.00.0"
    struct.pack_into("<H", header, 0x60, 1)
    struct.pack_into("<I", header, 0x64, checksum)
    header[0x7FC:0x800] = b"toof"
    body = bytes(((index * 2654435761) >> 13) & 0xFF if index % 3 else index & 0x0F for index in range(size - 0x800))
    return bytes(header) + body


def expand(harness, tmp_path, data, *extra):
    source = tmp_path / "compressed.map"
    output = tmp_path / "expanded.map"
    source.write_bytes(data)
    if output.exists():
        output.unlink()
    line = run(harness, "expand", source, output, *extra)
    return line, (output.read_bytes() if output.exists() else None)


# ---------- the header


def test_version_of_compressed_and_plain_headers(harness, tmp_path):
    plain = make_map(0x1000)
    for version, expected in ((0x861A0261, 609), (0x861A0007, 7), (609, 0), (7, 0), (5, 0),
                              (0x861A0006, 0), (0x861A0005, 0), (0x861A000D, 0), (0x86000261, 0)):
        data = bytearray(plain)
        struct.pack_into("<I", data, 4, version)
        path = tmp_path / "header.map"
        path.write_bytes(bytes(data))
        assert run(harness, "version", path) == str(expected), hex(version)
    (tmp_path / "short.map").write_bytes(b"daeh")
    assert run(harness, "version", tmp_path / "short.map") == "0"


def test_uncompressed_map_refused(harness, tmp_path):
    line, output = expand(harness, tmp_path, make_map(0x2000))
    assert line.startswith("refused: it is not a compressed")
    assert output is None


# ---------- expanding


@needs_zstd
@pytest.mark.parametrize("version", [609, 7])
def test_round_trip(harness, tmp_path, version):
    original = make_map(version=version)
    compressed = chimera_compress.compress(original)
    assert len(compressed) < len(original)
    assert struct.unpack_from("<I", compressed, 4)[0] == 0x861A0000 | version
    line, output = expand(harness, tmp_path, compressed)
    assert line == f"ok {len(original)}"
    assert output == original


@needs_zstd
def test_several_frames(harness, tmp_path):
    original = make_map(0x123456)
    compressed = chimera_compress.compress(original, frames=5)
    line, output = expand(harness, tmp_path, compressed)
    assert line == f"ok {len(original)}"
    assert output == original


@needs_zstd
def test_unstated_size_taken_as_expanded(harness, tmp_path):
    """an older compressed map may give no size: Chimera takes what it expands to"""
    original = make_map(0x8000)
    compressed = bytearray(chimera_compress.compress(original))
    struct.pack_into("<I", compressed, 8, 0)
    line, output = expand(harness, tmp_path, bytes(compressed))
    assert line == f"ok {len(original)}"
    assert output[8:12] == b"\0\0\0\0" and output[12:] == original[12:]


@needs_zstd
def test_cut_off_refused(harness, tmp_path):
    compressed = chimera_compress.compress(make_map())
    for cut in (1, 7, len(compressed) // 3):
        line, output = expand(harness, tmp_path, compressed[:-cut])
        assert line.startswith("refused:"), cut
        assert output is None


@needs_zstd
def test_damaged_refused(harness, tmp_path):
    """a damaged frame header is always caught; damage inside the data only
    where the frame has a checksum (chimera-compress writes none, so its
    maps are as safe as Chimera reads them, and ce_map_checks.c reads the
    map whole before it is played)"""
    compressed = bytearray(chimera_compress.compress(make_map(), checksum=True))
    # (the frame's magic number, its header, then bytes in its blocks)
    for offset in (0x800, 0x804, len(compressed) // 2, len(compressed) - 2):
        damaged = bytearray(compressed)
        damaged[offset] ^= 0x5A
        line, output = expand(harness, tmp_path, bytes(damaged))
        assert line.startswith("refused:"), (offset, line)
        assert output is None


@needs_zstd
def test_trailing_bytes_refused(harness, tmp_path):
    compressed = chimera_compress.compress(make_map())
    line, output = expand(harness, tmp_path, compressed + b"\0" * 64)
    assert line.startswith("refused:")
    assert output is None


@needs_zstd
def test_wrong_stated_size_refused(harness, tmp_path):
    original = make_map()
    compressed = bytearray(chimera_compress.compress(original))
    for stated in (len(original) - 1, len(original) + 4096):
        struct.pack_into("<I", compressed, 8, stated)
        line, output = expand(harness, tmp_path, bytes(compressed))
        assert line.startswith("refused: it expanded to"), line
        assert output is None


@needs_zstd
def test_maximum_size(harness, tmp_path):
    original = make_map(0x20000)
    compressed = bytearray(chimera_compress.compress(original))
    line, _ = expand(harness, tmp_path, bytes(compressed), str(len(original) - 1))
    assert line.startswith("refused: it expands to")
    # (and one that claims no size, caught as it expands)
    struct.pack_into("<I", compressed, 8, 0)
    line, output = expand(harness, tmp_path, bytes(compressed), str(len(original) - 1))
    assert line.startswith("refused: it expands to more than")
    assert output is None
    line, output = expand(harness, tmp_path, bytes(compressed), str(len(original)))
    assert line == f"ok {len(original)}"


@needs_zstd
def test_stop(harness, tmp_path):
    path = tmp_path / "compressed.map"
    path.write_bytes(chimera_compress.compress(make_map()))
    assert run(harness, "stop", path) == "refused: stopped"


def real_map():
    named = os.environ.get("HALO_TEST_CE_MAP")
    if named:
        return Path(named)
    for root in (os.environ.get("HALO_DATA_ROOT"), ROOT / "assets", Path.home() / "halo-ce-decomp-port" / "assets"):
        if root and (Path(root) / "maps_ce" / "sneak.map").is_file():
            return Path(root) / "maps_ce" / "sneak.map"
    return None


@needs_zstd
@pytest.mark.skipif(real_map() is None, reason="no Custom Edition map to try (HALO_TEST_CE_MAP)")
def test_real_custom_edition_map(harness, tmp_path):
    original = real_map().read_bytes()
    compressed = chimera_compress.compress(original)
    line, output = expand(harness, tmp_path, compressed)
    assert line == f"ok {len(original)}"
    # (the body is the original's to the byte; the header is the original's
    # as chimera-compress cleans it: the same for a map whose header holds
    # only those fields)
    assert output[0x800:] == original[0x800:]
    assert output[:0x800] == bytes(chimera_compress.clean_header(original[:0x800], len(original)))


# ---------- the builds


def test_only_desktop_builds_have_it():
    linux = (ROOT / "tools" / "linux_build.py").read_text(encoding="utf-8")
    custom_edition = linux[linux.index("CUSTOM_EDITION_DEFINES = "):].split("\n", 1)[0]
    assert "HALO_COMPRESSED_MAPS" not in custom_edition
    assert 'COMPRESSED_MAP_DEFINES = ["-DHALO_COMPRESSED_MAPS"]' in linux
    for name in ("macos_build.py", "linux64_build.py", "server_build.py"):
        assert "compressed_maps=True" in (ROOT / "tools" / name).read_text(encoding="utf-8"), name
    for name in ("android_build.py", "android64_build.py"):
        text = (ROOT / "tools" / name).read_text(encoding="utf-8")
        assert "COMPRESSED_MAP" not in text and "compressed_maps" not in text, name
    assert "compressed_maps: bool = False" in (ROOT / "tools" / "lp64_build.py").read_text(encoding="utf-8")


def test_reader_is_gated_whole():
    for name in ("map_compressed.c", "map_compressed_files.c"):
        lines = [line for line in (SOURCE / name).read_text(encoding="utf-8").splitlines() if line.startswith("#")]
        assert lines[0] == "#ifdef HALO_COMPRESSED_MAPS" and lines[-1] == "#endif", name
    families = (ROOT / "port" / "linux" / "game" / "map_families.c").read_text(encoding="utf-8")
    assert families.count("map_compressed_file_resolve(") == 1
    download = (SOURCE / "map_download.c").read_text(encoding="utf-8")
    call = download.index("ok = expand_download(")
    assert download.rfind("#ifdef HALO_COMPRESSED_MAPS", 0, call) > download.rfind("#endif", 0, call)
