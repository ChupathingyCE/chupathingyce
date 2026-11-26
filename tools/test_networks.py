"""Runs the C tests of the networks a game trusts (port/linux/src/networks.c:
domains, names, the signed list of networks, local additions and removals,
and which network a certificate belongs to), with the build machine's C
compiler, identity.c and Monocypher, and checks that the C test's vectors
are port/linux/tests/federation_vectors.json's (delta-spec's
reference/federation_vectors.json, which delta-server's tests check too)."""
import json
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "port" / "linux" / "tests" / "networks_test.c"
VECTORS = ROOT / "port" / "linux" / "tests" / "federation_vectors.json"
MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def build(cc, out, extra=()):
    return subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", *extra,
         "-iquote", str(ROOT / "port" / "linux" / "include"), "-I", str(MONOCYPHER),
         str(TEST), str(ROOT / "port" / "linux" / "src" / "networks.c"),
         str(ROOT / "port" / "linux" / "src" / "identity.c"),
         str(ROOT / "port" / "linux" / "game" / "content_sha256.c"),
         str(MONOCYPHER / "monocypher.c"), str(MONOCYPHER / "monocypher-ed25519.c"),
         "-o", str(out)],
        capture_output=True, text=True,
    )


def c_vectors():
    """The C test's VECTOR_ macros: name -> text (strings joined, escapes read)."""
    text = TEST.read_text()
    found = {}
    for match in re.finditer(r"#define VECTOR_([A-Z_]+)((?:[^\n]*\\\n)*[^\n]*)", text):
        body = match.group(2)
        strings = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
        if strings:
            found[match.group(1).lower()] = "".join(strings).encode().decode("unicode_escape")
        else:
            found[match.group(1).lower()] = int(re.search(r"(\d+)UL", body).group(1))
    return found


def test_vectors_match():
    """Every vector the C test has is the JSON's."""
    vectors = json.loads(VECTORS.read_text())
    c = c_vectors()
    expected = {"now": vectors["now"],
                "list_key_public": vectors["list"]["key_public"],
                "list_document": vectors["list"]["document"],
                "list_signature": vectors["list"]["signature"],
                "home_account": vectors["checks"][0]["account"],
                "other_account": vectors["checks"][1]["account"]}
    for network, prefix in (("chupathingy.org", "home"), ("example.org", "other")):
        for name in ("root_primary", "root_recovery", "issuer_certificate", "device_certificate"):
            expected[f"{prefix}_{name}"] = vectors[network][name]
    assert c == expected


def run(tmp_path, extra=()):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "networks_test"
    result = build(cc, binary, extra)
    if extra and result.returncode != 0:
        pytest.skip("no such toolchain here")
    assert result.returncode == 0, result.stdout + result.stderr
    ran = subprocess.run([str(binary)], capture_output=True, text=True)
    if extra and ran.returncode != 0 and ("Bad CPU type" in ran.stderr or not ran.stdout):
        pytest.skip("can't run it here")
    assert ran.returncode == 0, ran.stdout + ran.stderr


def test_networks(tmp_path):
    """Builds networks.c with its tests, warnings as errors, and runs them."""
    run(tmp_path)


def test_networks_32_bit(tmp_path):
    """The same, as a 32-bit program, where the compiler can make one."""
    run(tmp_path, ("-m32",))


def test_list_keys_are_their_own():
    """The list of networks has its own key set, never the legacy table's
    (delta_key.h) nor the identity roots (identity_key.h)."""
    src = ROOT / "port" / "linux" / "src"
    def keys(name):
        text = (src / name).read_text()
        text = text[:text.index("#else")]
        return set(re.findall(r"((?:0x[0-9a-f]{2}, ){31}0x[0-9a-f]{2})", text))
    own = keys("networks_key.h")
    assert not own & keys("delta_key.h")
    assert not own & keys("identity_key.h")
