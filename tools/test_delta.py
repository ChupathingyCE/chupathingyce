"""Delta's numbers (port/linux/include/delta.h) against the network version
the game announces and joins (port/linux/include/halo_port_limits.h), the
capability registry's bits, the wire ID, and the signed legacy table: its
tool (tools/delta_table.py) and the game's loader (port/linux/src/delta.c,
through tools/delta_check.c). Plain text checks for the numbers: the command
repository's tools read halo_port_limits.h's numbers as text too."""
import json
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

from tools import delta_table

ROOT = Path(__file__).resolve().parent.parent
DELTA = ROOT / "port" / "linux" / "include" / "delta.h"
LIMITS = ROOT / "port" / "linux" / "include" / "halo_port_limits.h"

ROW = re.compile(r'X\((\d+),\s*"(build-\d+)",\s*(additive|breaking)\)')
CAPABILITY = re.compile(r"_delta_capability_(\w+)\s*=\s*(\d+)")


def define(text, name):
    match = re.search(rf"^#define {name} (\d+)", text, re.M)
    assert match, f"{name} is not a number in halo_port_limits.h"
    return int(match.group(1))


def rows():
    text = DELTA.read_text(encoding="utf-8")
    table = text[text.index("#define DELTA_LEGACY_VERSIONS"):]
    found = [(int(number), build, kind) for number, build, kind in ROW.findall(table)]
    assert found, "delta.h's DELTA_LEGACY_VERSIONS has no rows"
    return found


def test_table_is_in_order():
    numbers = [number for number, _, _ in rows()]
    assert numbers == sorted(set(numbers)), f"versions out of order or repeated: {numbers}"
    builds = [int(build.split("-")[1]) for _, build, _ in rows()]
    assert builds == sorted(builds), f"first builds out of order: {builds}"


def test_limits_follow_the_table():
    limits = LIMITS.read_text(encoding="utf-8")
    table = rows()
    newest = table[-1][0]
    last_breaking = max(number for number, _, kind in table if kind == "breaking")
    assert define(limits, "HALO_PORT_NETWORK_VERSION") == newest, \
        "hosts must announce the table's newest version"
    assert define(limits, "HALO_PORT_NETWORK_VERSION_MAXIMUM") == newest, \
        "clients must join hosts up to the table's newest version"
    assert define(limits, "HALO_PORT_NETWORK_VERSION_MINIMUM") == last_breaking, \
        "clients must join hosts back to the table's newest breaking version"


def test_capability_bits():
    found = [(name, int(bit)) for name, bit in CAPABILITY.findall(DELTA.read_text(encoding="utf-8"))]
    bits = [bit for _, bit in found]
    assert bits == list(range(len(bits))), f"capability bits must be 0, 1, 2... without gaps: {found}"
    assert len(bits) <= 32, "the handshake carries 32 capability bits"


def test_header_compiles():
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "delta_check.c"
        source.write_text(
            '#include "delta.h"\n'
            "#define ROW(number, build, kind) number,\n"
            "static const int versions[] = { DELTA_LEGACY_VERSIONS(ROW) };\n"
            "_Static_assert(NUMBER_OF_DELTA_CAPABILITIES <= 32, \"capabilities\");\n"
            "_Static_assert(DELTA_ADVERTISED_FLAG == 0x04, \"flag\");\n"
            "static const char wire[] = DELTA_WIRE;\n"
            "_Static_assert(sizeof(wire) > 1 && sizeof(wire) <= 33, \"wire\");\n"
            "const char *delta_wire(void) { return wire; }\n"
            "int main(void) { return versions[0] > 0 ? 0 : 1; }\n", encoding="utf-8")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-I", str(DELTA.parent), "-c", str(source),
                        "-o", str(Path(directory) / "delta_check.o")], check=True)


# ---------- the wire ID and the legacy table


DELTA_C = ROOT / "port" / "linux" / "src" / "delta.c"


def test_wire_id():
    """every build has a wire ID (DELTA_WIRE): what the legacy table's rows
    are keyed by"""
    match = re.search(r'^#define DELTA_WIRE "([^"]*)"', DELTA.read_text(encoding="utf-8"), re.M)
    assert match, "delta.h has no DELTA_WIRE"
    assert re.fullmatch(r"[a-z0-9][a-z0-9.-]{0,31}", match.group(1)), f"not a wire ID: {match.group(1)!r}"
    assert delta_table.wire() == match.group(1)


def test_capability_names():
    """delta.c's names for the kill switch are the registry's, in its order"""
    text = DELTA_C.read_text(encoding="utf-8")
    block = re.search(r"delta_capability_names\[\] =\s*\{([^}]*)\}", text).group(1)
    names = re.findall(r'"(\w+)"', block)
    registry = [name for name, _ in CAPABILITY.findall(DELTA.read_text(encoding="utf-8"))]
    assert names == registry


# the built-in keys, as published in ChupathingyCE/command's keys/delta.pub.json
PUBLISHED_KEYS = {
    "primary": "d51bd85346bf889b6bc5aa21dcc7a43488657b64f2432e17f6e0e7fe976933ce",
    "recovery": "97cde84d8b9b6ba8a275d412b49422d4c07970653ee53ae54285e96640a5e456",
}


def ed25519_point(key):
    """whether 32 bytes are an Ed25519 public key: a point on the curve
    (RFC 8032, 5.1.3's decoding)"""
    p = 2 ** 255 - 19
    d = -121665 * pow(121666, p - 2, p) % p
    number = int.from_bytes(key, "little")
    sign, y = number >> 255, number & ((1 << 255) - 1)
    if y >= p:
        return False
    u, v = (y * y - 1) % p, (d * y * y + 1) % p
    x = u * pow(v, 3, p) * pow(u * pow(v, 7, p), (p - 5) // 8, p) % p
    if v * x * x % p != u:
        x = x * pow(2, (p - 1) // 4, p) % p
        if v * x * x % p != u:
            return False
    return not (x == 0 and sign)


def test_built_in_keys():
    """delta_key.h's keys: the published primary and recovery keys, in that
    order, 32 bytes each, none zero, each a point on the curve (the tests
    build with their own key instead: HALO_DELTA_TEST_KEY)"""
    text = (ROOT / "port" / "linux" / "src" / "delta_key.h").read_text().split("#else", 1)[0]
    blocks = [re.findall(r"0x([0-9a-fA-F]{2})", block) for block in re.findall(r"\{([^{}]*)\}", text)]
    keys = [bytes(int(byte, 16) for byte in block) for block in blocks if block]
    assert [key.hex() for key in keys] == [PUBLISHED_KEYS["primary"], PUBLISHED_KEYS["recovery"]]
    assert all(len(key) == 32 and any(key) and ed25519_point(key) for key in keys)
    assert delta_table.header_keys() == keys
    assert not ed25519_point(bytes.fromhex("02" + "00" * 31))


def test_make_is_this_builds_numbers():
    table = json.loads(delta_table.make(7, issued=1))
    assert table["serial"] == 7 and table["delta_legacy"] == 1
    assert table["wires"] == {delta_table.wire(): delta_table.built_in()}


def floor():
    return delta_table.built_in()


def document(serial=10, row=None, wire=None, extra=None, **fields):
    table = {"delta_legacy": 1, "serial": serial, "issued": 1791331200,
             "wires": {wire or delta_table.wire(): row or dict(floor(), announce=floor()["announce"] + 1,
                                                              maximum=floor()["maximum"] + 1)},
             "disabled_capabilities": []}
    table.update(fields)
    if extra:
        table["wires"].update(extra)
    return json.dumps(table).encode()


@pytest.mark.parametrize("bad", [
    b"", b"[]", b"{", b'{"delta_legacy": 1, "serial": 3}',
    b'{"delta_legacy": 1, "serial": 3, "wires": {}} trailing',
    b'{"delta_legacy": 1, "serial": 3, "serial": 4, "wires": {}}',
    b'{"delta_legacy": 2, "serial": 3, "wires": {}}',
    b'{"delta_legacy": 1, "serial": 3.5, "wires": {}}',
    b'{"delta_legacy": 1, "serial": 0, "wires": {}}',
    b'{"delta_legacy": 1, "serial": 3, "wires": {"w": {"announce": 5, "minimum": 6, "maximum": 7}}}',
    b'{"delta_legacy": 1, "serial": 3, "wires": {"w": {"announce": 70000, "minimum": 1, "maximum": 70000}}}',
    b'\xff\xfe',
    b'{"delta_legacy": 1, "serial": 3, "wires": {}, "platform_policy": []}',
    b'{"delta_legacy": 1, "serial": 3, "wires": {}, "platform_policy": {"xbox": 16}}',
    b'{"delta_legacy": 1, "serial": 3, "wires": {}, "platform_policy": {"xbox": {"host_players": -1}}}',
    b'{"delta_legacy": 1, "serial": 3, "wires": {}, "platform_policy": {"xbox": {"host_players": 1.5}}}',
    b'{"delta_legacy": 1, "serial": 3, "wires": {}, "platform_policy": {"xbox": {"host_players": true}}}',
    b'{"delta_legacy": 1, "serial": 3, "wires": {}, "platform_policy": {"xbox": {"host_players": 100000}}}',
    b'{"delta_legacy": 1, "serial": 3, "wires": {}, "platform_policy": {"Xbox 360": {}}}',
])
def test_bad_documents(bad):
    with pytest.raises(delta_table.TableError):
        delta_table.parse(bad)


def test_platform_policy_is_optional_and_reserved():
    """platform_policy: an object of platforms' objects of small whole
    numbers (Delta Peer's); the game's loader passes over it"""
    delta_table.parse(document())
    delta_table.parse(document(platform_policy={}))
    table = delta_table.parse(document(platform_policy={"xbox": {"host_players": 16, "join_players": 0},
                                                        "pc_windows": {}}))
    assert table["platform_policy"]["xbox"]["host_players"] == 16


def test_size_cap():
    big = document(padding="x" * delta_table.DOCUMENT_SIZE)
    with pytest.raises(delta_table.TableError, match="more than"):
        delta_table.parse(big)


def test_narrowing():
    wire = delta_table.wire()
    for row in (dict(floor(), announce=floor()["announce"] - 1, minimum=floor()["minimum"] - 1),
                dict(floor(), minimum=floor()["minimum"] + 1),
                dict(floor(), maximum=floor()["maximum"] - 1, announce=floor()["maximum"] - 1)):
        with pytest.raises(delta_table.TableError, match="narrows"):
            delta_table.parse(document(row=row), wire)
    delta_table.parse(document(), wire)


@pytest.fixture(scope="module")
def keys(tmp_path_factory):
    """a test key pair, made for these tests (never a real key)"""
    try:
        delta_table.openssl()
    except SystemExit:
        pytest.skip("needs OpenSSL 3's openssl")
    directory = tmp_path_factory.mktemp("delta_key")
    private = directory / "delta_test_key.pem"
    public = delta_table.keygen(private)
    return private, public


def signed(keys, data):
    return delta_table.sign_bytes(keys[0], data)


def test_sign_and_verify(keys, tmp_path):
    private, public = keys
    path = tmp_path / "legacy.json"
    path.write_bytes(delta_table.make(3, issued=1))
    tool = [sys.executable, str(ROOT / "tools" / "delta_table.py")]
    subprocess.run([*tool, "sign", "--key", str(private), str(path)], check=True, capture_output=True)
    assert re.fullmatch(r"[0-9a-f]{128}\n", (tmp_path / "legacy.json.sig").read_text())
    result = subprocess.run([*tool, "verify", "--public-key", public.hex(), str(path)], capture_output=True,
                            text=True)
    assert result.returncode == 0, result.stderr
    assert "serial 3" in result.stdout
    # (a byte changed in the document, or another key: not signed)
    path.write_bytes(path.read_bytes().replace(b'"serial": 3', b'"serial": 4'))
    result = subprocess.run([*tool, "verify", "--public-key", public.hex(), str(path)], capture_output=True,
                            text=True)
    assert result.returncode == 1 and "signature" in result.stderr


def test_tampered_and_other_key(keys):
    data = document()
    signature = signed(keys, data)
    assert delta_table.check(data, signature.hex(), [keys[1]])["serial"] == 10
    with pytest.raises(delta_table.TableError):
        delta_table.check(data.replace(b"1791331200", b"1791331201"), signature.hex(), [keys[1]])
    with pytest.raises(delta_table.TableError):
        delta_table.check(data, signature.hex(), [bytes(31) + b"\x01"])
    with pytest.raises(delta_table.TableError):
        delta_table.check(data, signature.hex()[:-2], [keys[1]])


def test_make_from_keeps_other_wires(tmp_path):
    old = tmp_path / "old.json"
    old.write_bytes(document(serial=4, extra={"chupa-17a": {"announce": 17, "minimum": 11, "maximum": 19}}))
    result = subprocess.run([sys.executable, str(ROOT / "tools" / "delta_table.py"), "make", "--serial", "5",
                             "--from", str(old)], capture_output=True, text=True, check=True)
    table = json.loads(result.stdout)
    assert table["wires"]["chupa-17a"]["maximum"] == 19
    assert table["wires"][delta_table.wire()] == floor()
    result = subprocess.run([sys.executable, str(ROOT / "tools" / "delta_table.py"), "make", "--serial", "4",
                             "--from", str(old)], capture_output=True, text=True)
    assert result.returncode == 1


# ---------- the game's loader (delta.c), through tools/delta_check.c


@pytest.fixture(scope="module")
def checker(keys, tmp_path_factory):
    """delta_check, built with the flags ninja gives delta.c and the test key"""
    target = "build/macos/obj/port/linux/src/delta.o" if sys.platform == "darwin" else \
        "build/linux/obj/port/linux/src/delta.o"
    if not shutil.which("clang") or not shutil.which("ninja") or not (ROOT / "build.ninja").is_file():
        pytest.skip("needs clang, ninja and a configured build")
    commands = subprocess.run(["ninja", "-t", "commands", target], capture_output=True, text=True, cwd=ROOT)
    if commands.returncode != 0:
        pytest.skip(f"no {target} in build.ninja")
    words = shlex.split(commands.stdout.strip().splitlines()[-1])
    # (the compiler, and whatever runs it, ccache in CI: up to the first flag)
    while words and not words[0].startswith("-"):
        words = words[1:]
    flags, sources, skip = [], [], False
    for word in words:
        if skip:
            skip = False
        elif word in ("-MF", "-o"):
            skip = True
        elif word in ("-MMD", "-c") or word.startswith("-flto") or \
                word.startswith("-fprofile-use"):
            continue
        elif word.endswith(".c"):
            sources.append(word)
        else:
            flags.append(word)
    key = ",".join(f"0x{byte:02x}" for byte in keys[1])
    program = tmp_path_factory.mktemp("delta_check") / "delta_check"
    link = ["-Wl,-undefined,dynamic_lookup", "-Wl,-dead_strip"] if sys.platform == "darwin" else \
        ["-no-pie", "-Wl,--unresolved-symbols=ignore-all"]
    # (a failed fetch is tried again a tenth of a second later, not thirty minutes)
    built = subprocess.run(["clang", *flags, "-DHALO_GAME_BROWSER", f"-DHALO_DELTA_TEST_KEY={key}",
                            "-DDELTA_RETRY_INTERVAL=100", "-O1", *link, "-o", str(program),
                            *sources, "tools/delta_check.c", "port/third_party/monocypher/monocypher.c",
                            "port/third_party/monocypher/monocypher-ed25519.c", "-lpthread"],
                           capture_output=True, text=True, cwd=ROOT)
    assert built.returncode == 0, built.stderr[-4000:]
    return program


def run_checker(program, root, *steps, override=None, url=""):
    environment = {"DELTA_CHECK_ROOT": str(root), "PATH": "/usr/bin:/bin", "DELTA_CHECK_URL": url}
    if override:
        environment["DELTA_CHECK_OVERRIDE"] = str(override)
    result = subprocess.run([str(program), *map(str, steps)], capture_output=True, text=True, env=environment,
                            timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def state_line(output):
    return [line for line in output.splitlines() if line.startswith("state")]


def write_signed(keys, path, data, signature=None):
    path.write_bytes(delta_table.signed_table(data, signature or signed(keys, data)))
    return path


def test_loader_takes_newer_and_caches(keys, checker, tmp_path):
    base = floor()
    table = write_signed(keys, tmp_path / "t10", document(serial=10))
    output = run_checker(checker, tmp_path, "state", "offer", table, "state")
    assert state_line(output)[0].startswith(f"state {base['announce']} {base['minimum']} {base['maximum']} serial 0 ")
    assert "offer 1" in output
    size = len(table.read_bytes())
    assert state_line(output)[1] == (f"state {base['announce'] + 1} {base['minimum']} {base['maximum'] + 1} "
                                     f"serial 10 override 0 disabled 0 signed {size}")
    assert (tmp_path / "delta_legacy.signed").read_bytes() == table.read_bytes()
    # (a new run: the cache's table, at once)
    output = run_checker(checker, tmp_path, "state")
    assert state_line(output)[0].startswith(f"state {base['announce'] + 1} {base['minimum']} {base['maximum'] + 1} "
                                            "serial 10 ")


def test_loader_ignores_older_and_equal(keys, checker, tmp_path):
    newer = write_signed(keys, tmp_path / "t10", document(serial=10))
    same = write_signed(keys, tmp_path / "t10b", document(serial=10, row=dict(floor(), maximum=floor()["maximum"] + 5)))
    older = write_signed(keys, tmp_path / "t9", document(serial=9, row=dict(floor(), maximum=floor()["maximum"] + 5)))
    output = run_checker(checker, tmp_path, "offer", newer, "offer", same, "offer", older, "state")
    assert output.count("offer 1") == 1 and output.count("offer 0") == 2
    assert f" {floor()['maximum'] + 1} serial 10 " in state_line(output)[0]


@pytest.mark.parametrize("name,make", [
    ("narrows", lambda: document(row=dict(floor(), minimum=floor()["minimum"] + 1))),
    ("announces older", lambda: document(row=dict(floor(), announce=floor()["minimum"]))),
    ("too large", lambda: document(padding="x" * delta_table.DOCUMENT_SIZE)),
    ("trailing", lambda: document() + b" x"),
    ("format 2", lambda: document(delta_legacy=2)),
    ("float", lambda: document().replace(b'"serial": 10', b'"serial": 10.0')),
    ("twice", lambda: document().replace(b'"serial": 10', b'"serial": 10, "serial": 11')),
    ("two rows", lambda: document().replace(b'"wires": {', b'"wires": {"' + delta_table.wire().encode() +
                                            b'": {"announce": 30, "minimum": 1, "maximum": 30}, ')),
    ("not a range", lambda: document(row={"announce": 40, "minimum": 1, "maximum": 30})),
    ("deep", lambda: document().replace(b'"issued"', b'"x": ' + b"[" * 50 + b"]" * 50 + b', "issued"')),
    ("no serial", lambda: b'{"delta_legacy": 1, "wires": {}}'),
    ("nul", lambda: document().replace(b'"issued"', b'"\x00": 1, "issued"')),
])
def test_loader_drops_bad_tables(keys, checker, tmp_path, name, make):
    table = write_signed(keys, tmp_path / "bad", make())
    output = run_checker(checker, tmp_path, "offer", table, "state")
    assert "offer 0" in output, name
    assert "log: Delta: dropped" in output, name
    assert " serial 0 " in state_line(output)[0], name
    assert not (tmp_path / "delta_legacy.signed").exists()


def test_loader_drops_tampered_and_unsigned(keys, checker, tmp_path):
    data = document()
    signature = signed(keys, data)
    tampered = tmp_path / "tampered"
    tampered.write_bytes(delta_table.signed_table(data.replace(b"1791331200", b"1791331201"), signature))
    other = tmp_path / "other"
    other.write_bytes(delta_table.signed_table(data, bytes(64)))
    bare = tmp_path / "bare"
    bare.write_bytes(data)
    output = run_checker(checker, tmp_path, "offer", tampered, "offer", other, "offer", bare, "state")
    assert output.count("offer 0") == 3
    assert output.count("its signature does not match") == 2 and "not a signed table" in output
    assert " serial 0 " in state_line(output)[0]


def test_loader_reads_known_rows_and_kill_switch(keys, checker, tmp_path):
    """another wire's row is not this build's; disabled_capabilities sets the
    kill switch's bits for the names it knows"""
    data = document(serial=12, wire="chupa-other", disabled_capabilities=["chat", "unknown", "platform"],
                    unknown_field={"nested": [1, 2.5e3, None, True, "\u00e9"]},
                    platform_policy={"xbox": {"host_players": 16}, "android": {"join_players": 15}})
    output = run_checker(checker, tmp_path, "offer", write_signed(keys, tmp_path / "t12", data), "state")
    assert "offer 1" in output and "no row for" in output
    assert state_line(output)[0].startswith(
        f"state {floor()['announce']} {floor()['minimum']} {floor()['maximum']} serial 12 override 0 disabled 9 ")


def test_loader_override(keys, checker, tmp_path):
    """network.legacy_table: unsigned, used in place of every signed table,
    never passed on; it may set any range"""
    local = tmp_path / "local.json"
    local.write_bytes(document(serial=1, row={"announce": 20, "minimum": 19, "maximum": 21}))
    table = write_signed(keys, tmp_path / "t10", document(serial=10))
    output = run_checker(checker, tmp_path, "offer", table, "state", override=local)
    assert "WARNING: a local, unsigned legacy table is in use" in output
    assert "offer 0" in output
    assert state_line(output)[0] == "state 20 19 21 serial 0 override 1 disabled 0 signed 0"
    # (a file that cannot be used: the built-in numbers, still no signed table)
    output = run_checker(checker, tmp_path, "offer", table, "state", override=tmp_path / "missing.json")
    assert "is not used" in output and "offer 0" in output
    assert state_line(output)[0].startswith(f"state {floor()['announce']} {floor()['minimum']} ")


def serve(root, url, data):
    served = root / "served"
    served.mkdir(exist_ok=True)
    (served / re.sub(r"[^A-Za-z0-9.]", "_", url)).write_bytes(data)


GITHUB = "https://raw.githubusercontent.com/ChupathingyCE/chupathingyce/delta-table/legacy.json"
SITE = "https://list.example/v1/delta/legacy"


def test_loader_fetches_site_then_github(keys, checker, tmp_path):
    """at start: the cache, then Delta List's table, and GitHub's only when
    the site gives no valid one; the newest serial wins and is cached"""
    write_signed(keys, tmp_path / "delta_legacy.signed", document(serial=5))
    newer = document(serial=6)
    serve(tmp_path, SITE, newer)
    serve(tmp_path, SITE + ".sig", signed(keys, newer).hex().encode() + b"\n")
    output = run_checker(checker, tmp_path, "start", "wait", 6, "state", url="https://list.example/")
    assert "legacy table 5 from the cache" in output and "legacy table 6 from Delta List" in output
    assert "wait 1" in output and GITHUB not in output
    assert delta_table.signed_table(newer, signed(keys, newer)) == (tmp_path / "delta_legacy.signed").read_bytes()
    # (the site's signature broken: GitHub's table, newer still)
    github = document(serial=7)
    serve(tmp_path, SITE + ".sig", b"0" * 128 + b"\n")
    serve(tmp_path, GITHUB, github)
    serve(tmp_path, GITHUB + ".sig", signed(keys, github).hex().encode())
    output = run_checker(checker, tmp_path, "start", "wait", 7, url="https://list.example")
    assert "legacy table 6 from the cache" in output and "dropped the legacy table from Delta List" in output
    assert "legacy table 7 from GitHub" in output and "wait 1" in output


def test_loader_fetches_nothing_without_a_list_server(keys, checker, tmp_path):
    output = run_checker(checker, tmp_path, "start", "state", url="")
    assert "request:" not in output


def test_loader_says_once_that_no_table_is_published(checker, tmp_path):
    """neither Delta List nor GitHub has a table (404 from both): said once a
    run, however many times it looks again; a server it cannot reach is said
    each time"""
    output = run_checker(checker, tmp_path, "start", "sleep", 1000, url="https://list.example")
    assert output.count(f"request: {SITE}\n") >= 3 and output.count(f"request: {GITHUB}\n") >= 3
    assert output.count("no legacy table is published") == 1
    assert "log: Delta: no legacy table from" not in output
    output = run_checker(checker, tmp_path, "start", "sleep", 1000, url="https://unreachable.example")
    assert "no legacy table is published" not in output
    assert output.count("no legacy table from Delta List (could not connect)") >= 3
    assert output.count("no legacy table from GitHub (it has none)") >= 3
