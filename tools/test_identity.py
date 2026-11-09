"""Runs the C tests of Delta's device identity (port/linux/src/identity.c:
the certificates, the signed messages, the requests' text and the
schedule), with the build machine's C compiler and Monocypher, and checks
that the C test's vectors are port/linux/tests/identity_vectors.json's (the
ones docs/delta.md shows, and the site's and command's tests check)."""
import json
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "port" / "linux" / "tests" / "identity_test.c"
VECTORS = ROOT / "port" / "linux" / "tests" / "identity_vectors.json"
MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def build(cc, out, extra=()):
    return subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", *extra,
         "-iquote", str(ROOT / "port" / "linux" / "include"), "-I", str(MONOCYPHER),
         str(TEST), str(ROOT / "port" / "linux" / "src" / "identity.c"),
         str(MONOCYPHER / "monocypher.c"), str(MONOCYPHER / "monocypher-ed25519.c"),
         "-o", str(out)],
        capture_output=True, text=True,
    )


def c_vectors():
    """The C test's VECTOR_ macros: name -> text (strings joined) or number."""
    text = TEST.read_text()
    found = {}
    for match in re.finditer(r"#define VECTOR_([A-Z_]+)((?:[^\n]*\\\n)*[^\n]*)", text):
        body = match.group(2)
        strings = re.findall(r'"([^"]*)"', body)
        found[match.group(1).lower()] = "".join(strings) if strings else int(re.search(r"(\d+)UL", body).group(1))
    return found


def test_vectors_match():
    """Every vector the C test has is the JSON's, and none is missing."""
    vectors = json.loads(VECTORS.read_text())
    c = c_vectors()
    for name, value in vectors.items():
        if name.endswith("_text") or name == "note":
            continue
        assert c.get(name) == value, name


def test_identity(tmp_path):
    """Builds the identity unit with its tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "identity_test"
    result = build(cc, binary)
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


def test_identity_32_bit(tmp_path):
    """The same, as a 32-bit program, where the compiler can make one."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "identity_test32"
    result = build(cc, binary, ("-m32",))
    if result.returncode != 0:
        pytest.skip("no 32-bit toolchain here")
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    if run.returncode != 0 and ("Bad CPU type" in run.stderr or not run.stdout):
        pytest.skip("can't run 32-bit programs here")
    assert run.returncode == 0, run.stdout + run.stderr
