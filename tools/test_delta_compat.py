"""Wire compatibility ranges (docs/delta.md, "Wire compatibility ranges"): the
C tests of port/linux/src/delta_compat.c (the advertisement's bytes, the old
slot an exact-match client reads, the join decision for each pair of wires),
built with the build machine's C compiler, the table's rows, and the
documentation of the layout."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "port" / "linux" / "include"
DELTA = INCLUDE / "delta.h"
DOCS = ROOT / "docs" / "delta.md"
SOURCE = ROOT / "port" / "linux" / "src" / "delta_compat.c"
TEST = ROOT / "port" / "linux" / "tests" / "delta_compat_test.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def wire_key(wire):
    number, letter = re.fullmatch(r"chupa-(\d+)([a-z])", wire).groups()
    return int(number), letter


def test_rules(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "delta_compat_test"
    sanitize = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    for extra in (sanitize, []):
        build = subprocess.run(
            [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *extra, "-iquote", str(INCLUDE),
             str(TEST), str(SOURCE), "-o", str(binary)], capture_output=True, text=True)
        if build.returncode == 0:
            break
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "delta_compat_test: ok" in run.stdout


def test_source_is_c89(tmp_path):
    """the ports' older compilers read it, as they read the wire format"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    build = subprocess.run(
        [cc, "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-iquote", str(INCLUDE), "-c", str(SOURCE),
         "-o", str(tmp_path / "delta_compat.o")], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr


def test_rows():
    text = DELTA.read_text(encoding="utf-8")
    wire = re.search(r'^#define DELTA_WIRE "([^"]+)"', text, re.M).group(1)
    first = re.search(r'^#define DELTA_WIRE_FIRST_ADVERTISED "([^"]+)"', text, re.M).group(1)
    rows = re.findall(r'X\("(chupa-\d+[a-z])", "(chupa-\d+[a-z])"\)', text[text.index("DELTA_WIRE_COMPATIBILITY"):])
    assert rows, "no compatibility rows"
    seen = set()
    for row_wire, oldest in rows:
        assert row_wire not in seen, f"{row_wire} has two rows"
        seen.add(row_wire)
        assert wire_key(oldest) <= wire_key(row_wire), f"{row_wire}'s oldest is newer than itself"
        assert wire_key(row_wire) >= wire_key(first)
    assert wire_key(wire) >= wire_key(first)
    # (the layout fits the advertisement: the major and the old slot, then the six range bytes, to byte 0x1A)
    assert "#define DELTA_ADVERTISED_RANGE_SIZE 6" in text
    assert "#define DELTA_ADVERTISED_RANGE_OFFSET (DELTA_ADVERTISED_WIRE_OFFSET + 1 + DELTA_ADVERTISED_WIRE_SIZE)" in text
    assert 3 + 1 + 16 + 6 == 0x1A


def test_documented():
    docs = DOCS.read_text(encoding="utf-8")
    assert "### Wire compatibility ranges" in docs
    for needle in ("DELTA_WIRE_COMPATIBILITY", "DELTA_ADVERTISED_RANGE_OFFSET", "0xA5", "chupa-28a", "chupa-29a",
                   "chupa-32a", "build-244"):
        assert needle in docs, needle
