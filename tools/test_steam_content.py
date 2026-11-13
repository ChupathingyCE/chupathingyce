"""Runs the C tests of the Steam content client (port/linux/src/steam_content):
its protobuf codec, a chunk's and a manifest's decode from the recorded
fixtures, file names, the logon's LoginID and the player's error words
(port/linux/tests/steam_content_test.c), under AddressSanitizer and
UndefinedBehaviorSanitizer where the compiler has them; and a short run of
its fuzz target (steam_content_fuzz.c) where libFuzzer is available. No
network.

python3 tools/test_steam_content.py --live <folder> builds the hand-run
check against Steam itself (steam_content_live.c) into that folder.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
"""
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools import fuzz_support  # noqa: E402
from tools.linux_build import ZLIB_DEFINES, ZLIB_SOURCES, ZSTD_DEFINES, ZSTD_SOURCES  # noqa: E402

THIRD = ROOT / "port" / "third_party"
MODULE = ROOT / "port" / "linux" / "src" / "steam_content"
TESTS = ROOT / "port" / "linux" / "tests"
INCLUDES = [f"-I{THIRD / 'mbedtls' / 'include'}", f"-I{THIRD / 'zlib'}", f"-I{THIRD / 'zstd' / 'lib'}",
            f"-I{THIRD / 'lzma'}"]
FRAMEWORKS = ["-framework", "Security", "-framework", "CoreFoundation"] if sys.platform == "darwin" else []


def third_party_units():
    """(source, flags) of the libraries the client uses"""
    units = [(path, [f"-I{THIRD / 'mbedtls' / 'include'}", f"-I{THIRD / 'mbedtls' / 'library'}"])
             for path in sorted((THIRD / "mbedtls" / "library").glob("*.c"))]
    units += [(THIRD / "zlib" / name, list(ZLIB_DEFINES)) for name in ZLIB_SOURCES]
    units += [(THIRD / "zstd" / name, list(ZSTD_DEFINES)) for name in ZSTD_SOURCES]
    units.append((THIRD / "lzma" / "LzmaDec.c", []))
    return units


def build(cc, sources, output, objects_dir, extra, link_extra=None):
    """the module, its libraries and sources linked into output"""
    objects_dir.mkdir(parents=True, exist_ok=True)

    def compile_one(unit):
        source, flags = unit
        obj = objects_dir / (source.parent.name + "_" + source.stem + ".o")
        command = [cc, "-c", "-O1", "-g", "-w", "-std=gnu11", *extra, *flags, str(source), "-o", str(obj)]
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        return obj

    with ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        objects = list(pool.map(compile_one, third_party_units()))
    command = [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers", "-O1", "-g",
               "-pthread", *ZLIB_DEFINES, *INCLUDES, *(extra if link_extra is None else link_extra), *map(str, sorted(MODULE.glob("*.c"))),
               *map(str, sources), *map(str, objects), *FRAMEWORKS, "-o", str(output)]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def compiler():
    return next((path for path in map(shutil.which, ("clang", "cc", "gcc")) if path), None)


def sanitizers(cc, directory):
    """-fsanitize flags this compiler links, or none"""
    probe = directory / "probe.c"
    probe.write_text("int main(void) { return 0; }\n")
    flags = ["-fsanitize=address,undefined"]
    result = subprocess.run([cc, *flags, str(probe), "-o", str(directory / "probe")], capture_output=True)
    if result.returncode == 0:
        return flags
    fuzz_support.fallback("no sanitizer runtime")
    return []


def test_steam_content(tmp_path):
    """the unit tests, warnings as errors, run from the tests folder (the fixtures)"""
    import pytest

    cc = compiler()
    if not cc or os.name == "nt":
        pytest.skip("no POSIX C compiler")
    binary = tmp_path / "steam_content_test"
    build(cc, [TESTS / "steam_content_test.c"], binary, tmp_path / "objects", sanitizers(cc, tmp_path))
    result = subprocess.run([str(binary)], capture_output=True, text=True, cwd=TESTS, timeout=120,
                            env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
    assert result.returncode == 0, result.stdout + result.stderr


def test_steam_content_fuzz(tmp_path):
    """the fuzz target, from seeds of the fixtures, for a short while"""
    cc = fuzz_support.libfuzzer_compiler()
    if not cc:
        fuzz_support.skip("no clang with libFuzzer")
    binary = tmp_path / "steam_content_fuzz"
    build(cc, [TESTS / "steam_content_fuzz.c"], binary, tmp_path / "objects",
          ["-fsanitize=fuzzer-no-link,address,undefined"], ["-fsanitize=fuzzer,address,undefined"])
    seeds = tmp_path / "seeds"
    seeds.mkdir()
    fixtures = TESTS / "steam_content"
    (seeds / "manifest").write_bytes(b"\x01" + (fixtures / "manifest.zip").read_bytes())
    (seeds / "zip").write_bytes(b"\x04" + (fixtures / "manifest.zip").read_bytes())
    for name, size in (("chunk_lzma.bin", 6000), ("chunk_zstd.bin", 3000)):
        (seeds / name).write_bytes(b"\x03" + bytes([size & 255, size >> 8]) + (fixtures / name).read_bytes())
    (seeds / "proto").write_bytes(b"\x00\x0a\x04\x08\x96\x01\x10\x05")
    result = fuzz_support.run_fuzzer(binary, tmp_path, int(os.environ.get("HALO_FUZZ_SECONDS", "20")), seeds=[seeds],
                                     max_len=65536)
    assert result.returncode == 0, result.stdout[-4000:] + result.stderr[-4000:]


def test_steam_net_fuzz(tmp_path):
    """the HTTP and WebSocket readers under libFuzzer, fed over a local
    socket pair, for a short while"""
    cc = fuzz_support.libfuzzer_compiler()
    if not cc or os.name == "nt":
        fuzz_support.skip("no clang with libFuzzer")
    binary = tmp_path / "steam_net_fuzz"
    build(cc, [TESTS / "steam_net_fuzz.c"], binary, tmp_path / "objects",
          ["-DSC_FUZZING", "-fsanitize=fuzzer-no-link,address,undefined"],
          ["-DSC_FUZZING", "-fsanitize=fuzzer,address,undefined"])
    seeds = tmp_path / "seeds"
    seeds.mkdir()
    (seeds / "sized").write_bytes(b"\x00HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello")
    (seeds / "chunked").write_bytes(b"\x00HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n")
    (seeds / "frames").write_bytes(b"\x02\x82\x05hello\x82\x7e\x00\x04abcd\x88\x00")
    (seeds / "upgrade").write_bytes(b"\x01HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                    b"Connection: Upgrade\r\nSec-WebSocket-Accept: x\r\n\r\n\x82\x01a")
    result = fuzz_support.run_fuzzer(binary, tmp_path, int(os.environ.get("HALO_FUZZ_SECONDS", "20")), seeds=[seeds],
                                     max_len=8192)
    assert result.returncode == 0, result.stdout[-4000:] + result.stderr[-4000:]


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--live":
        out = Path(sys.argv[2])
        build(compiler(), [TESTS / "steam_content_live.c"], out / "steam_content_live", out / "objects", [])
        print(out / "steam_content_live")
    else:
        print(__doc__)
